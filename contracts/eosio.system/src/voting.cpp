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

  
   void system_contract::regproducer( const name producer, const public_key& producer_key, const std::string& url, uint16_t location , const name regaccount){
       check( url.size() < 512, "url too long" );
       check( producer_key != eosio::public_key(), "public key should not be the default value" );
       require_auth( regaccount );
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
             update_total_votepay_share( ct, 0.0, prod->total_votes );
             // When introducing the producer2 table row for the first time, the producer's votes must also be accounted for in the global total_producer_votepay_share at the same time.
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
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      std::vector< std::pair<eosio::producer_key,uint16_t> > top_producers;
      top_producers.reserve(_gstate.max_producer_schedule_size);

      for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < _gstate.max_producer_schedule_size && 0 < it->total_votes && it->active(); ++it ) {
      //for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < _gstate.max_producer_schedule_size &&  it->active(); ++it ) {
         top_producers.emplace_back( std::pair<eosio::producer_key,uint16_t>({{it->owner, it->producer_key}, it->location}) );
      }

      if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      /// sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<eosio::producer_key> producers;

      producers.reserve(top_producers.size());
      for( const auto& item : top_producers )
         producers.push_back(item.first);

      auto packed_schedule = pack(producers);

      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }

   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      double weight = int64_t( (now() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
      return double(staked) * std::pow( 2, weight );
   }

   double system_contract::update_total_votepay_share( time_point ct,
                                                       double additional_shares_delta,
                                                       double shares_rate_delta )
   {
      double delta_total_votepay_share = 0.0;
      if( ct > _gstate3.last_vpay_state_update ) {
         delta_total_votepay_share = _gstate3.total_vpay_share_change_rate
                                       * double( (ct - _gstate3.last_vpay_state_update).count() / 1E6 );
      }

      delta_total_votepay_share += additional_shares_delta;
      if( delta_total_votepay_share < 0 && _gstate2.total_producer_votepay_share < -delta_total_votepay_share ) {
         _gstate2.total_producer_votepay_share = 0.0;
      } else {
         _gstate2.total_producer_votepay_share += delta_total_votepay_share;
      }

      if( shares_rate_delta < 0 && _gstate3.total_vpay_share_change_rate < -shares_rate_delta ) {
         _gstate3.total_vpay_share_change_rate = 0.0;
      } else {
         _gstate3.total_vpay_share_change_rate += shares_rate_delta;
      }

      _gstate3.last_vpay_state_update = ct;

      return _gstate2.total_producer_votepay_share;
   }

   double system_contract::update_producer_votepay_share( const producers_table2::const_iterator& prod_itr,
                                                          time_point ct,
                                                          double shares_rate,
                                                          bool reset_to_zero )
   {
      double delta_votepay_share = 0.0;
      if( shares_rate > 0.0 && ct > prod_itr->last_votepay_share_update ) {
         delta_votepay_share = shares_rate * double( (ct - prod_itr->last_votepay_share_update).count() / 1E6 ); // cannot be negative
      }

      double new_votepay_share = prod_itr->votepay_share + delta_votepay_share;
      _producers2.modify( prod_itr, same_payer, [&](auto& p) {
         if( reset_to_zero )
            p.votepay_share = 0.0;
         else
            p.votepay_share = new_votepay_share;

         p.last_votepay_share_update = ct;
      } );

      return new_votepay_share;
   }

   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *  @pre if proxy is set then no producers can be voted for
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *  @pre every listed producer or proxy must have been previously registered
    *  @pre voter must authorize this action
    *  @pre voter must have previously staked some amount of CORE_SYMBOL for voting
    *  @pre voter->staked must be up to date
    *
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *  @post every producer newly voted for will have vote increased by new vote amount
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    */
   void system_contract::voteproducer( const name voter_name, const name proxy, const std::vector<name>& producers ) {
      require_auth( voter_name );
      update_votes( voter_name, proxy, producers, true );
      /*vote_stake_updater( voter_name );
      update_votes( voter_name, proxy, producers, true );
      auto rex_itr = _rexbalance.find( voter_name.value );
      if( rex_itr != _rexbalance.end() && rex_itr->rex_balance.amount > 0 ) {
         check_voting_requirement( voter_name, "voter holding REX tokens must vote for at least 21 producers or for a proxy" );
      }*/
   }

   void system_contract::update_votes( const name voter_name, const name proxy, const std::vector<name>& producers, bool voting ) {
      //validate input
     
      auto prod = _producers.find(voter_name.value);
      check( prod != _producers.end(), "voter must be producer" ); 
     
      if ( prod != _producers.end() ) {
          _producers.modify( prod, same_payer, [&]( producer_info& info ){
           info.total_votes = info.total_votes+1;
           info.is_active = true;
          });
      }
   }
   /**
    *  An account marked as a proxy can vote with the weight of other accounts which
    *  have selected it as a proxy. Other accounts must refresh their voteproducer to
    *  update the proxy's weight.
    *
    *  @param isproxy - true if proxy wishes to vote on behalf of others, false otherwise
    *  @pre proxy must have something staked (existing row in voters table)
    *  @pre new state must be different than current state
    */
   void system_contract::regproxy( const name proxy, bool isproxy ) {
      require_auth( proxy );

      auto pitr = _voters.find( proxy.value );
      if ( pitr != _voters.end() ) {
         check( isproxy != pitr->is_proxy, "action has no effect" );
         check( !isproxy || !pitr->proxy, "account that uses a proxy is not allowed to become a proxy" );
         _voters.modify( pitr, same_payer, [&]( auto& p ) {
               p.is_proxy = isproxy;
               p.last_change_time = current_time_point();
            });
         propagate_weight_change( *pitr );
      } else {
         _voters.emplace( proxy, [&]( auto& p ) {
               p.owner  = proxy;
               p.is_proxy = isproxy;
               p.last_change_time = current_time_point();
            });
      }
   }

   void system_contract::propagate_weight_change( const voter_info& voter ) {
      check( !voter.proxy || !voter.is_proxy, "account registered as a proxy is not allowed to use a proxy" );
      double new_weight = stake2vote( voter.staked );
      if ( voter.is_proxy ) {
         new_weight += voter.proxied_vote_weight;
      }

      /// don't propagate small changes (1 ~= epsilon)
      if ( fabs( new_weight - voter.last_vote_weight ) > 1 )  {
         if ( voter.proxy ) {
            auto& proxy = _voters.get( voter.proxy.value, "proxy not found" ); //data corruption
            _voters.modify( proxy, same_payer, [&]( auto& p ) {
                  p.proxied_vote_weight += new_weight - voter.last_vote_weight;
                  p.last_change_time = current_time_point();
               }
            );
            propagate_weight_change( proxy );
         } else {
            auto delta = new_weight - voter.last_vote_weight;
            const auto ct = current_time_point();
            double delta_change_rate         = 0;
            double total_inactive_vpay_share = 0;
            for ( auto acnt : voter.producers ) {
               auto& prod = _producers.get( acnt.value, "producer not found" ); //data corruption
               const double init_total_votes = prod.total_votes;
               _producers.modify( prod, same_payer, [&]( auto& p ) {
                  p.total_votes += delta;
                  _gstate.total_producer_vote_weight += delta;
               });
               auto prod2 = _producers2.find( acnt.value );
               if ( prod2 != _producers2.end() ) {
                  const auto last_claim_plus_3days = prod.last_claim_time + microseconds(3 * _gstate.useconds_per_day);
                  bool crossed_threshold       = (last_claim_plus_3days <= ct);
                  bool updated_after_threshold = (last_claim_plus_3days <= prod2->last_votepay_share_update);
                  // Note: updated_after_threshold implies cross_threshold

                  double new_votepay_share = update_producer_votepay_share( prod2,
                                                ct,
                                                updated_after_threshold ? 0.0 : init_total_votes,
                                                crossed_threshold && !updated_after_threshold // only reset votepay_share once after threshold
                                             );

                  if( !crossed_threshold ) {
                     delta_change_rate += delta;
                  } else if( !updated_after_threshold ) {
                     total_inactive_vpay_share += new_votepay_share;
                     delta_change_rate -= init_total_votes;
                  }
               }
            }

            update_total_votepay_share( ct, -total_inactive_vpay_share, delta_change_rate );
         }
      }
      _voters.modify( voter, same_payer, [&]( auto& v ) {
            v.last_vote_weight = new_weight;
            v.last_change_time = current_time_point();
         }
      );
   }

} /// namespace eosiosystem
