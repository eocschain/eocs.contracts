#include "icp.token.hpp"

#include <eosiolib/datastream.hpp>
#include <eosiolib/action.hpp>

#include "token.cpp"

namespace icp {

   token::token(name s, name code, datastream<const char*> ds) : contract(s, code, ds) {
      co_singleton co(_self, _self.value);
      _co = co.get_or_default(collaborative_contract{});
   }

   void token::setcontracts(name icp, name peer) {
      require_auth(_self);

      co_singleton co(_self, _self.value);
      eosio_assert(!co.exists(), "contracts already exist");

      co.set(collaborative_contract{icp, peer}, _self);
   }

   struct transfer_args {
      name  from;
      name  to;
      asset         quantity;
      string        memo;
   };

   struct icp_transfer_args {
      name  contract;
      name  from;
      name  to;
      asset         quantity;
      string        memo;
      uint8_t       refund;
   };

   void token::icp_transfer(name contract, name from, name icp_to, asset quantity, string memo, uint32_t expiration, bool refund) {
      eosio_assert(bool(_co.peer), "empty remote peer contract");
      eosio_assert(bool(_co.icp), "empty local icp contract");

      auto seq = eosio::next_packet_seq(_co.icp);

      auto icp_send = action(vector<permission_level>{}, _co.peer, "icpreceive"_n,
                             icp_transfer_args{contract, from, icp_to, quantity, memo, refund});
      auto icp_receive = action(vector<permission_level>{}, _self, "icpreceipt"_n, false); // here action data won't be used

      locked l(_self, _self.value);
      l.emplace(from, [&](auto& o) {
         o.seq = seq;
         o.contract = contract;
         o.account = from;
         o.balance = quantity;
         o.refund = refund;
      });

      auto send_action = pack(icp_send);
      auto receive_action = pack(icp_receive);
      action(permission_level{_co.icp, "sendaction"_n}, _co.icp, "sendaction"_n, icp_sendaction{seq, send_action, expiration, receive_action}).send(); // TODO: permission
   }

   void token::icpreceive(name contract, name icp_from, name to, asset quantity, string memo, uint8_t refund) {
      // NB: this permission should be authorized to icp contract's `eosio.code` permission
      require_auth2(_self.value, "callback"_n.value);

      eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

      if (!refund) {
         mint(contract, to, quantity);
      } else {
         action(permission_level{_self, "active"_n}, contract, "transfer"_n,
                transfer_args{_self, to, quantity, memo}).send();
      }
   }

   void token::icpreceipt(uint64_t seq, uint8_t status, bytes data) {
      // NB: this permission should be authorized to icp contract's `eosio.code` permission
      require_auth2(_self.value, "callback"_n.value);

      locked l(_self, _self.value);
      auto it = l.find(seq);
      if (it != l.end()) {
         if (static_cast<receipt_status>(status) == receipt_status::expired) { // icp transfer transaction expired or failed, so release locked asset
            if (!it->refund) {
               action(permission_level{_self, "active"_n}, it->contract, "transfer"_n,
                      transfer_args{_self, it->account, it->balance, "icp release locked asset"}).send();
            } else {
               mint(it->contract, it->account, it->balance);
            }
         }

         l.erase(it);
      }
   }

   void token::icprefund(name contract, name from, name icp_to, asset quantity, string memo, uint32_t expiration) {
      require_auth(from);

      eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

      burn(contract, from, quantity);

      icp_transfer(contract, from, icp_to, quantity, std::move(memo), expiration, true); // TODO: original memo?
   }

   void token::icptransfer(name contract, name from, name icp_to, asset quantity, string memo, uint32_t expiration) {
      require_auth(from);

      deposits dps(_self, contract.value);
      auto by_account_asset = dps.get_index<"accountasset"_n>();
      const auto &dp = by_account_asset.get(account_asset_key(from, quantity), "no deposit object found");
      eosio_assert(dp.balance.amount >= quantity.amount, "overdrawn balance");

      if (dp.balance.amount == quantity.amount) {
         dps.erase(dp);
      } else {
         dps.modify(dp, from, [&](auto &a) {
            a.balance -= quantity;
         });
      }

      icp_transfer(contract, from, icp_to, quantity, std::move(memo), expiration, false); // TODO: original memo?
   }

   void token::icp_transfer_or_deposit(name contract, name from, name to, asset quantity, string memo) {
      // only care about token receiving
      print("icp_transfer_or_deposit");
      if (to != _self) {
         return;
      }

      if (memo.find("icp ") == 0) { // it is an icp call
         print("icp");
         auto account_end = memo.find(' ', 4);
         eosio_assert(account_end != std::string::npos, "invalid icp token transfer memo");
         auto n = memo.substr(4, account_end - 4);
         auto icp_to = eosio::name(n);
         auto h = memo.substr(account_end + 1);
         auto icp_expiration = static_cast<uint32_t>(std::stoul(h));

         // TODO: auth `from`
         icp_transfer(contract, from, icp_to, quantity, memo, icp_expiration, false); // TODO: original memo?

      } else { // deposit
         print("deposit");
         deposits dps(_self, contract.value);
         auto by_account_asset = dps.get_index<"accountasset"_n>();
         auto it = by_account_asset.find(account_asset_key(from, quantity));
         if (it == by_account_asset.end()) {
            // NB: Must set payer to self, otherwise, "Cannot charge RAM to other accounts during notify".
            // TODO: charge manually
            dps.emplace(_self, [&](auto &a) {
               a.pk = dps.available_primary_key();
               a.account = from;
               a.balance = quantity;
            });
         } else {
            by_account_asset.modify(it, same_payer, [&](auto &a) {
               a.balance += quantity;
            });
         }
      }
   }

   void token::mint(name contract, name to, asset quantity) {
      require_auth(_self);

      eosio_assert(is_account(to), "to account does not exist");
      eosio_assert(quantity.is_valid(), "invalid quantity");
      eosio_assert(quantity.amount > 0, "must mint positive quantity");

      auto sym_name = quantity.symbol.code().raw();
      stats statstable(_self, contract.value);
      auto& st = statstable.get(sym_name, "token with symbol does not exist, create token before mint");

      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(quantity.amount <= std::numeric_limits<int64_t>::max() - st.supply.amount, "quantity exceeds available supply");

      require_recipient(to);

      statstable.modify(st, same_payer, [&](auto &s) {
         s.supply += quantity;
      });

      add_balance(contract, to, quantity, _self); // TODO: self as ram payer?
   }

   void token::burn(name contract, name from, asset quantity) {
      eosio_assert(is_account(from), "from account does not exist");
      eosio_assert( quantity.is_valid(), "invalid quantity" );
      eosio_assert( quantity.amount > 0, "must burn positive quantity" );

      auto sym_name = quantity.symbol.code().raw();
      stats statstable(_self, contract.value);
      auto& st = statstable.get(sym_name, "token with symbol does not exist, create token before burn");

      eosio_assert(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      eosio_assert(quantity.amount <= st.supply.amount, "quantity exceeds available supply");

      require_recipient(from);

      statstable.modify(st, same_payer, [&](auto &s) {
         s.supply -= quantity;
      });

      sub_balance(contract, from, quantity);
   }

}

extern "C" {
   void apply(uint64_t receiver, uint64_t code, uint64_t action) {
      auto self = receiver;
      if (action == "onerror"_n.value) {
         /* onerror is only valid if it is for the "eosio" code account and authorized by "eosio"'s "active permission */
         eosio_assert(code == "eosio"_n.value, "onerror action's are only valid from the \"eosio\" system account");
      }
      if (code == self || action == "onerror"_n.value) {
         switch (action) {
            EOSIO_DISPATCH_HELPER(icp::token, (setcontracts)(create)(transfer)(icpreceive)(icpreceipt)(icptransfer)(icprefund))
         }
      }
      if (code != self && action == "transfer"_n.value) {
         eosio::execute_action(eosio::name(self), eosio::name(code), &icp::token::icp_transfer_or_deposit);
      }
   }
}
