/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosiolib/singleton.hpp>
#include <eosiolib/icp.hpp>

#include "types.hpp"
#include "fork.hpp"

namespace eosio {

struct [[eosio::contract("icp")]] icp : public contract {
   explicit icp(name s, name code, datastream<const char*> ds);

   [[eosio::action]]
   void setpeer(name peer);
   [[eosio::action]]
   void setmaxpackes(uint32_t maxpackets); // limit the maximum stored packets, to support icp rate limiting
   [[eosio::action]]
   void setmaxblocks(uint32_t maxblocks);

   [[eosio::action]]
   void openchannel(const bytes& data); // initialize with a block_header_state as trust seed
   [[eosio::action]]
   void closechannel(uint8_t clear_all, uint32_t max_num);

   [[eosio::action]]
   void addblocks(const bytes& data);
   [[eosio::action]]
   void addblock(const bytes& data);
   [[eosio::action]]
   void sendaction(uint64_t seq, const bytes& send_action, uint32_t expiration, const bytes& receipt_action);
   [[eosio::action]]
   void onpacket(const icpaction& ia);
   [[eosio::action]]
   void onreceipt(const icpaction& ia);
   [[eosio::action]]
   void onreceiptend(const icpaction& ia);
   [[eosio::action]]
   void genproof(uint64_t packet_seq, uint64_t receipt_seq, uint8_t finalised_receipt); // regenerate a proof of old packet/receipt
   [[eosio::action]]
   void dummy(name from);
   [[eosio::action]]
   void cleanup(uint32_t max_num);

   uint64_t next_packet_seq() const;

private:
   enum class incoming_type : uint8_t {
      packet, receipt, receiptend
   };

   bytes extract_action(const icpaction& ia, const capi_name& name, incoming_type type);
   void update_peer();

   void set_last_incoming_block_num(const capi_checksum256& id, incoming_type type);

   void meter_add_packets(uint32_t num);
   void meter_remove_packets(uint32_t num = std::numeric_limits<uint32_t>::max());

   struct [[eosio::table("icpmeter"), eosio::contract("icp")]] icp_meter {
       uint32_t max_packets;
       uint32_t current_packets;
   };

   typedef eosio::singleton<"icpmeter"_n, icp_meter> meter_singleton;

   peer_contract _peer;
   std::unique_ptr<fork_store> _store;
};

}
