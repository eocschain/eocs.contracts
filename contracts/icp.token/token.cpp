#include "icp.token.hpp"

namespace icp {

   string trim(const string& str) {
      size_t first = str.find_first_not_of(' ');
      if (string::npos == first)
      {
         return str;
      }
      size_t last = str.find_last_not_of(' ');
      return str.substr(first, (last - first + 1));
   }

   constexpr uint8_t max_precision = 18;

   symbol string_to_symbol(const string& from) {
         auto s = trim(from);
         check(!s.empty(), "creating symbol from empty string");
         auto comma_pos = s.find(',');
         check(comma_pos != string::npos, "missing comma in symbol");
         auto prec_part = s.substr(0, comma_pos);
         uint8_t p = std::stoull(prec_part);
         string name_part = s.substr(comma_pos + 1);
         check( p <= max_precision, "precision should be <= 18");
         return symbol(name_part, p);
   }

   void token::create(name contract, string symbol) {
      require_auth(_self);

      auto sym = string_to_symbol(symbol);
      check(sym.is_valid(), "invalid symbol name");

      stats statstable(_self, contract.value);
      auto existing = statstable.find(sym.code().raw());
      check(existing == statstable.end(), "token with symbol already exists");

      statstable.emplace(_self, [&](auto &s) {
         s.supply.symbol = sym;
      });
   }

   void token::transfer(name contract, name from, name to, asset quantity, string memo) {
      check(from != to, "cannot transfer to self");
      require_auth(from);
      check(is_account(to), "to account does not exist");
      auto sym = quantity.symbol.code().raw();
      stats statstable(_self, contract.value);
      const auto &st = statstable.get(sym);

      require_recipient(from);
      require_recipient(to);

      check(quantity.is_valid(), "invalid quantity");
      check(quantity.amount > 0, "must transfer positive quantity");
      check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
      check(memo.size() <= 256, "memo has more than 256 bytes");

      sub_balance(contract, from, quantity);
      add_balance(contract, to, quantity, from);
   }

   void token::sub_balance(name contract, name owner, asset value) {
      accounts from_acnts(_self, contract.value);

      auto by_account_asset = from_acnts.get_index<"accountasset"_n>();
      const auto& from = by_account_asset.get(account_asset_key(owner, value), "no balance object found");
      check(from.balance.amount >= value.amount, "overdrawn balance");

      if (from.balance.amount == value.amount) {
         from_acnts.erase(from);
      } else {
         from_acnts.modify(from, owner, [&](auto &a) {
            a.balance -= value;
         });
      }
   }

   void token::add_balance(name contract, name owner, asset value, name ram_payer) {
      accounts to_acnts(_self, contract.value);
      auto by_account_asset = to_acnts.get_index<"accountasset"_n>();
      auto to = by_account_asset.find(account_asset_key(owner, value));
      if (to == by_account_asset.end()) {
         to_acnts.emplace(ram_payer, [&](auto &a) {
            a.pk = to_acnts.available_primary_key();
            a.account = owner;
            a.balance = value;
         });
      } else {
         to_acnts.modify(*to, same_payer, [&](auto &a) {
            a.balance += value;
         });
      }
   }

}
