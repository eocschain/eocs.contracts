/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosio.system/eosio.system.hpp>

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/datastream.hpp>
#include <eosiolib/serialize.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/privileged.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/transaction.hpp>
#include <eosio.token/eosio.token.hpp>

#include <algorithm>
#include <cmath>

namespace eosiosystem {
   using eosio::indexed_by;
   using eosio::const_mem_fun;
   using eosio::singleton;
   using eosio::transaction;
   using eosio::print;
   using eosio::same_payer;
  
   void system_contract::regproducer( const name producer, const public_key& producer_key, const std::string& url, uint16_t location , const name regaccount){
       check( url.size() < 512, "url too long" );
       check( producer_key != eosio::public_key(), "public key should not be the default value" );
       require_auth( regaccount );
       if(producer == "eosio"_n){
          return;
       }
       
       check( is_account( producer ), "producer account does not exist");
       check( is_account( regaccount ), "regaccount account does not exist");
       //注册人不能给自己注册
       if (regaccount.value == producer.value){
            return;
       }

       //注册人不是eosio
       if(regaccount != "eosio"_n){
          //查找注册人是否是生产者
          auto reg = _producers.find( regaccount.value );
          check(reg != _producers.end(), "regaccount should be in producers");
          if (reg == _producers.end()){
             return;
          }
       }

      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();
      if ( prod != _producers.end() ) {
             _producers.modify( prod, producer, [&]( producer_info& info ){
             info.producer_key = producer_key;
             info.is_active    = true;
             info.url          = url;
             info.location     = location;
             if ( info.last_claim_time == time_point() )
                info.last_claim_time = ct;
          });

          auto prod2 = _producers2.find( producer.value );
          if ( prod2 == _producers2.end() ) {
             _producers2.emplace( producer, [&]( producer_info2& info ){
                info.owner                     = producer;
                info.last_votepay_share_update = ct;
             });
          }
      } else {
          _producers.emplace( producer, [&]( producer_info& info ){
             info.owner           = producer;
             info.total_votes     = 0;
             info.producer_key    = producer_key;
             info.is_active       = true;
             info.url             = url;
             info.location        = location;
             info.last_claim_time = ct;
          });
          _producers2.emplace( producer, [&]( producer_info2& info ){
             info.owner                     = producer;
             info.last_votepay_share_update = ct;
          });
       }


   }

   

   void system_contract::unregprod( const name producer, const name unregaccount ){
       if (unregaccount != "eosio"_n){
          require_auth( unregaccount );
          check(unregaccount.value == producer.value, "usaul account can only unreg self");
       }
       const auto& prod = _producers.get( producer.value, "producer not found" );
       _producers.modify( prod, same_payer, [&]( producer_info& info ){
          info.deactivate();
       });
   }


   void system_contract::update_elected_producers( block_timestamp block_time ) {
      print( "enter update_elected_producers" );
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      std::vector< std::pair<eosio::producer_key,uint16_t> > top_producers;
      top_producers.reserve(_gstate.max_producer_schedule_size);

      for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < _gstate.max_producer_schedule_size && 0 < it->total_votes && it->active(); ++it ) {
         top_producers.emplace_back( std::pair<eosio::producer_key,uint16_t>({{it->owner, it->producer_key}, it->location}) );
      }

      print("top_producers.size() is ",top_producers.size());
      print("last producer schedule size is ", _gstate.last_producer_schedule_size);
      if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }
      print("begin sort");
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;

      producers.reserve(top_producers.size());
      for( const auto& item : top_producers )
         producers.push_back(item.first);

      auto packed_schedule = pack(producers);

      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
         print("set proposed producers");
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }

  
   
   void system_contract::voteproducer( const name voter_name, const name proxy, const std::vector<name>& producers ) {
     
      require_auth( voter_name );
      check( is_account( voter_name ), "voter_name account does not exist");
      update_votes( voter_name, proxy, producers, true );
   }

   void system_contract::update_votes( const name voter_name, const name proxy, const std::vector<name>& producers, bool voting ) {
      auto prod = _producers.find(voter_name.value);
      check( prod != _producers.end(), "voter must be producer" ); 
     
      if ( prod != _producers.end() ) {
          _producers.modify( prod, same_payer, [&]( producer_info& info ){
           info.total_votes = info.total_votes+1;
           info.is_active = true;
          });
      }
   }

} /// namespace eosiosystem
