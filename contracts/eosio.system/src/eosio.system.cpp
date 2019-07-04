#include <eosio.system/eosio.system.hpp>
#include <eosiolib/dispatcher.hpp>
#include <eosiolib/crypto.h>

#include "producer_pay.cpp"
/*
#include "delegate_bandwidth.cpp"
#include "exchange_state.cpp"
#include "rex.cpp"*/
#include "voting.cpp"

namespace eosiosystem {

   system_contract::system_contract( name s, name code, datastream<const char*> ds )
   :native(s,code,ds),
    _voters(_self, _self.value),
    _voterbonus(_self, _self.value),
    _producers(_self, _self.value),
    _producers2(_self, _self.value),
    _global(_self, _self.value),
    _global2(_self, _self.value),
    _global3(_self, _self.value)
    
   {
      //print( "construct system\n" );
      _gstate  = _global.exists() ? _global.get() : get_default_parameters();
      _gstate2 = _global2.exists() ? _global2.get() : eosio_global_state2{};
      _gstate3 = _global3.exists() ? _global3.get() : eosio_global_state3{};
   }

   eosio_global_state system_contract::get_default_parameters() {
      eosio_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }

   time_point system_contract::current_time_point() {
      const static time_point ct{ microseconds{ static_cast<int64_t>( current_time() ) } };
      return ct;
   }

   time_point_sec system_contract::current_time_point_sec() {
      const static time_point_sec cts{ current_time_point() };
      return cts;
   }

   block_timestamp system_contract::current_block_time() {
      const static block_timestamp cbt{ current_time_point() };
      return cbt;
   }
   	
   symbol system_contract::core_symbol()const {
      //const static auto sym = get_core_symbol( _rammarket );
      symbol sym;
      return sym;
   }

   system_contract::~system_contract() {
      _global.set( _gstate, _self );
      _global2.set( _gstate2, _self );
      _global3.set( _gstate3, _self );
   }

  

   void system_contract::update_ram_supply() {
      auto cbt = current_block_time();

      if( cbt <= _gstate2.last_ram_increase ) return;

     
      auto new_ram = (cbt.slot - _gstate2.last_ram_increase.slot)*_gstate2.new_ram_per_block;
      _gstate.max_ram_size += new_ram;

     
      _gstate2.last_ram_increase = cbt;
   }


   void system_contract::setparams( const eosio::blockchain_parameters& params ) {
      require_auth( _self );
      (eosio::blockchain_parameters&)(_gstate) = params;
      check( 3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3" );
      set_blockchain_parameters( params );
   }

   void system_contract::setpriv( name account, uint8_t ispriv ) {
      require_auth( _self );
      set_privileged( account.value, ispriv );
   }

   void system_contract::rmvproducer( name producer ) {
      require_auth( _self );
      auto prod = _producers.find( producer.value );
      check( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.deactivate();
         });
   }

   void system_contract::updtrevision( uint8_t revision ) {
      require_auth( _self );
      check( _gstate2.revision < 255, "can not increment revision" ); // prevent wrap around
      check( revision == _gstate2.revision + 1, "can only increment revision by one" );
      check( revision <= 1, // set upper bound to greatest revision supported in the code
                    "specified revision is not yet supported by the code" );
      _gstate2.revision = revision;
   }

   void system_contract::bidname( name bidder, name newname, asset bid ) {
      require_auth( bidder );
      check( newname.suffix() == newname, "you can only bid on top-level suffix" );

      check( (bool)newname, "the empty name is not a valid account name to bid on" );
      check( (newname.value & 0xFull) == 0, "13 character names are not valid account names to bid on" );
      check( (newname.value & 0x1F0ull) == 0, "accounts with 12 character names and no dots can be created without bidding required" );
      check( !is_account( newname ), "account already exists" );
      check( bid.symbol == core_symbol(), "asset must be system token" );
      check( bid.amount > 0, "insufficient bid" );

      INLINE_ACTION_SENDER(eosio::token, transfer)(
         token_account, { {bidder, active_permission} },
         { bidder, names_account, bid, std::string("bid name ")+ newname.to_string() }
      );

      name_bid_table bids(_self, _self.value);
      print( name{bidder}, " bid ", bid, " on ", name{newname}, "\n" );
      auto current = bids.find( newname.value );
      if( current == bids.end() ) {
         bids.emplace( bidder, [&]( auto& b ) {
            b.newname = newname;
            b.high_bidder = bidder;
            b.high_bid = bid.amount;
            b.last_bid_time = current_time_point();
         });
      } else {
         check( current->high_bid > 0, "this auction has already closed" );
         check( bid.amount - current->high_bid > (current->high_bid / 10), "must increase bid by 10%" );
         check( current->high_bidder != bidder, "account is already highest bidder" );

         bid_refund_table refunds_table(_self, newname.value);

         auto it = refunds_table.find( current->high_bidder.value );
         if ( it != refunds_table.end() ) {
            refunds_table.modify( it, same_payer, [&](auto& r) {
                  r.amount += asset( current->high_bid, core_symbol() );
               });
         } else {
            refunds_table.emplace( bidder, [&](auto& r) {
                  r.bidder = current->high_bidder;
                  r.amount = asset( current->high_bid, core_symbol() );
               });
         }

         transaction t;
         t.actions.emplace_back( permission_level{_self, active_permission},
                                 _self, "bidrefund"_n,
                                 std::make_tuple( current->high_bidder, newname )
         );
         t.delay_sec = 0;
         uint128_t deferred_id = (uint128_t(newname.value) << 64) | current->high_bidder.value;
         cancel_deferred( deferred_id );
         t.send( deferred_id, bidder );

         bids.modify( current, bidder, [&]( auto& b ) {
            b.high_bidder = bidder;
            b.high_bid = bid.amount;
            b.last_bid_time = current_time_point();
         });
      }
   }

   void system_contract::bidrefund( name bidder, name newname ) {
      bid_refund_table refunds_table(_self, newname.value);
      auto it = refunds_table.find( bidder.value );
      check( it != refunds_table.end(), "refund not found" );
      INLINE_ACTION_SENDER(eosio::token, transfer)(
         token_account, { {names_account, active_permission}, {bidder, active_permission} },
         { names_account, bidder, asset(it->amount), std::string("refund bid on name ")+(name{newname}).to_string() }
      );
      refunds_table.erase( it );
   }


   /**
    *  Called after a new account is created. This code enforces resource-limits rules
    *  for new accounts as well as new account naming conventions.
    *
    *  Account names containing '.' symbols must have a suffix equal to the name of the creator.
    *  This allows users who buy a premium name (shorter than 12 characters with no dots) to be the only ones
    *  who can create accounts with the creator's name as a suffix.
    *
    */
   void native::newaccount( name              creator,
                            name              newact,
                            ignore<authority> owner,
                            ignore<authority> active ) {

      if( creator != _self ) {
         uint64_t tmp = newact.value >> 4;
         bool has_dot = false;

         for( uint32_t i = 0; i < 12; ++i ) {
           has_dot |= !(tmp & 0x1f);
           tmp >>= 5;
         }
         if( has_dot ) { // or is less than 12 characters
            auto suffix = newact.suffix();
            if( suffix == newact ) {
               name_bid_table bids(_self, _self.value);
               auto current = bids.find( newact.value );
               check( current != bids.end(), "no active bid for name" );
               check( current->high_bidder == creator, "only highest bidder can claim" );
               check( current->high_bid < 0, "auction for name is not closed yet" );
               bids.erase( current );
            } else {
               check( creator == suffix, "only suffix may create this account" );
            }
         }
      }

   }

   void native::setabi( name acnt, const std::vector<char>& abi ) {
      eosio::multi_index< "abihash"_n, abi_hash >  table(_self, _self.value);
      auto itr = table.find( acnt.value );
      if( itr == table.end() ) {
         table.emplace( acnt, [&]( auto& row ) {
            row.owner= acnt;
            sha256( const_cast<char*>(abi.data()), abi.size(), &row.hash );
         });
      } else {
         table.modify( itr, same_payer, [&]( auto& row ) {
            sha256( const_cast<char*>(abi.data()), abi.size(), &row.hash );
         });
      }
   }

   void system_contract::setglobal( std::string name, std::string value ) {
      require_auth( _self );

      if ( name == "to_voter_bonus_rate" ) {
         auto rate = static_cast<double>(std::stoi(value)) / 1e6;

         check( rate >= 0 && rate <= 1, "to_voter_bonus_rate must be in range [0, 1]" ); // TODO

         _gstate.to_voter_bonus_rate = rate;
         return;
      }

      check( _gstate.total_activated_stake < _gstate.min_activated_stake, "minimum activated stake has reached" );

      if (name == "max_producer_schedule_size") {
         auto sched_size = std::stoi(value);

         check( sched_size >= 3 && sched_size <= 51, "producers number must be in range [3, 51]" ); // TODO
         check( sched_size % 2 == 1, "producers number must be odd" );

         _gstate.max_producer_schedule_size = static_cast<uint8_t>(sched_size);
      } else if (name == "min_pervote_daily_pay") {
         auto min_vpay = std::stoll(value);

         check( min_vpay >= 0 && min_vpay <= 100'0000, "minimum pervote daily pay must be in range [0, 100'0000]" ); // TODO

         _gstate.min_pervote_daily_pay = min_vpay;
      } else if ( name == "min_activated_stake") {
         auto min_activated_stake = std::stoll(value);

         check( min_activated_stake >= 0 && min_activated_stake <= 150'000'000'0000, "minimum activated stake must be in range [0, 150'000'000'0000]" ); // TODO

         _gstate.min_activated_stake = min_activated_stake;
      } else if ( name == "useconds_per_day") {
         auto useconds_per_day = std::stoll(value);

         check( useconds_per_day > 0, "useconds_per_day must be > 0" );

         _gstate.useconds_per_day = useconds_per_day;
      } else if ( name == "continuous_rate") {
         auto rate = static_cast<double>(std::stoi(value)) / 1e6;

         check( rate >= 0 && rate <= 1, "continuous rate must be in range [0, 1]" ); // TODO

         _gstate.continuous_rate = rate;
      } else if ( name == "to_producers_rate") {
         auto rate = static_cast<double>(std::stoi(value)) / 1e6;

         check( rate >= 0 && rate <= 1, "to_producers_rate must be in range [0, 1]" ); // TODO

         _gstate.to_producers_rate = rate;
      } else if ( name == "to_bpay_rate") {
         auto rate = static_cast<double>(std::stoi(value)) / 1e6;

         check( rate >= 0 && rate <= 1, "to_bpay_rate must be in range [0, 1]" ); // TODO

         _gstate.to_bpay_rate = rate;
      } else if ( name == "refund_delay_sec" ) {
         auto refund_delay_sec = std::stoul(value);

         check(refund_delay_sec >= 0 && refund_delay_sec <= std::numeric_limits<uint32_t>::max(), "refund_delay_sec must be uint32_t");

         _gstate.refund_delay_sec = static_cast<uint32_t>(refund_delay_sec);
      } else if ( name == "ram_gift_bytes" ) {
         auto ram_gift_bytes = std::stoll(value);

         check( ram_gift_bytes >= _gstate.ram_gift_bytes, "ram_gift_bytes cannot be reduced" );

         _gstate.ram_gift_bytes = static_cast<uint64_t>(ram_gift_bytes);
      }
   }


   void system_contract::updtbwlist(uint8_t type, const std::vector<std::string>& add, const std::vector<std::string>& rmv) {
      require_auth(_self);
      update_blackwhitelist();
   }

} /// eosio.system


EOSIO_DISPATCH( eosiosystem::system_contract,
    
     (newaccount)(updateauth)(deleteauth)(linkauth)(unlinkauth)(canceldelay)(onerror)(setabi)
     
     (setparams)(setpriv)
     (rmvproducer)(updtrevision)(bidname)(bidrefund)
     (setglobal)(updtbwlist)
    
     (regproducer)(unregprod)(voteproducer)
     
     (onblock)(claimrewards)(claimbonus)
)
