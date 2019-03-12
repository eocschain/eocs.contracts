#pragma once

#include <eosiolib/singleton.hpp>

#include "icp.hpp"

typedef unsigned __int128 uint128_t;

namespace eosio {

using eosio::multi_index;
using eosio::const_mem_fun;
using eosio::indexed_by;
using eosio::singleton;

key256 to_key256(const checksum256& c) {
    return key256(c.get_array());
}

using stored_block_header_ptr = std::shared_ptr<struct stored_block_header>;
using stored_block_header_state_ptr = std::shared_ptr<struct stored_block_header_state>;

/* Irreversible block header */
struct [[eosio::table, eosio::contract("icp")]] stored_block_header {
    uint64_t pk;

    checksum256 id;
    uint32_t block_num;

    checksum256 previous;

    checksum256 action_mroot = checksum256{};

    bool has_action_mroot() const {
        checksum256 zeros{};
        return action_mroot != zeros;
    }

    auto primary_key() const { return pk; }
    key256 by_blockid() const { return to_key256(id); }
    key256 by_prev() const { return to_key256(previous); }
    uint64_t by_blocknum() const { return uint64_t(block_num); }
};

typedef multi_index<"block"_n, stored_block_header,
        indexed_by<"blockid"_n, const_mem_fun<stored_block_header, key256, &stored_block_header::by_blockid>>,
        indexed_by<"prev"_n, const_mem_fun<stored_block_header, key256, &stored_block_header::by_prev>>,
        indexed_by<"blocknum"_n, const_mem_fun<stored_block_header, uint64_t, &stored_block_header::by_blocknum>>
> stored_block_header_table;

/* Block header state */
struct [[eosio::table, eosio::contract("icp")]] stored_block_header_state {
    uint64_t pk;

    checksum256 id;
    uint32_t block_num;

    checksum256 previous;

    uint32_t dpos_irreversible_blocknum;
    uint32_t bft_irreversible_blocknum;

    bytes blockroot_merkle; // merkle root of block ids

    uint32_t last_irreversible_blocknum() {
       return std::max(dpos_irreversible_blocknum, bft_irreversible_blocknum);
    }

    auto primary_key() const { return pk; }
    key256 by_blockid() const { return to_key256(id); }
    key256 by_prev() const { return to_key256(previous); }
    uint64_t by_blocknum() const { return uint64_t(block_num); }
    uint128_t by_lib_block_num() const {
       return std::numeric_limits<uint128_t>::max() - ((uint128_t(dpos_irreversible_blocknum) << 64) + (uint128_t(bft_irreversible_blocknum) << 32) + block_num);
    }
};

typedef multi_index<"blockstate"_n, stored_block_header_state,
        indexed_by<"blockid"_n, const_mem_fun<stored_block_header_state, key256, &stored_block_header_state::by_blockid>>,
        indexed_by<"prev"_n, const_mem_fun<stored_block_header_state, key256, &stored_block_header_state::by_prev>>,
        indexed_by<"blocknum"_n, const_mem_fun<stored_block_header_state, uint64_t, &stored_block_header_state::by_blocknum>>,
        indexed_by<"libblocknum"_n, const_mem_fun<stored_block_header_state, uint128_t, &stored_block_header_state::by_lib_block_num>>
> stored_block_header_state_table;

struct [[eosio::table("activesched"), eosio::contract("icp")]] stored_producer_schedule {
   bytes producer_schedule;
};
typedef singleton<"activesched"_n, stored_producer_schedule> producer_schedule_singleton;

struct [[eosio::table("pendingsched"), eosio::contract("icp")]] pending_schedule {
   uint32_t pending_schedule_lib_num; // TODO
   checksum256 pending_schedule_hash; // TODO
   bytes pending_schedule;
};
typedef singleton<"pendingsched"_n, pending_schedule> pending_schedule_singleton;

struct [[eosio::table("storemeter"), eosio::contract("icp")]] store_meter {
   uint32_t max_blocks;
   uint32_t current_blocks;
};
typedef singleton<"storemeter"_n, store_meter> store_meter_singleton;

using fork_store_ptr = std::shared_ptr<class fork_store>;

class fork_store {
public:
    fork_store(name code);

    void init_seed_block(const block_header_state& block_state);
    void reset(uint8_t clear_all, uint32_t max_num);
    void set_max_blocks(uint32_t max);
    void add_block_header_with_merkle_path(const block_header_state& h, const vector<checksum256>& merkle_path);
    void add_block_header(const block_header& h);
    void cutdown(uint32_t block_num, uint32_t& max_num);
    checksum256 get_action_mroot(const checksum256& block_id);

private:
    bool is_producer(name name, const eosio::public_key& key);
    producer_schedule get_producer_schedule();
    incremental_merkle get_block_mroot(const checksum256& block_id);
    void validate_block_state(const block_header_state& block_state);
    void add_block_state(const block_header_state& block_state);
    template <typename Index>
    void add_block_id(const Index& by_blockid_index, const checksum256& block_id, const checksum256& previous) {
      eosio_assert(by_blockid_index.find(to_key256(block_id)) == by_blockid_index.end(), "already existing block");

      _blocks.emplace(_code, [&](auto& o) {
         o.pk = _blocks.available_primary_key();
         o.id = block_id;
         o.block_num = block_header::num_from_id(block_id);
         o.previous = previous;
         // absent `action_mroot`
      });
   }
    void update_active_schedule(const producer_schedule &schedule, bool clear_pending = true);
    void set_pending_schedule(uint32_t lib_num, const digest_type& hash, const producer_schedule& schedule);
    void prune(const stored_block_header_state& block_state);
    void remove(const checksum256& id);

    void meter_add_blocks(uint32_t num);
    void meter_remove_blocks(uint32_t num = std::numeric_limits<uint32_t>::max());

    name _code;
    stored_block_header_state_table _block_states;
    stored_block_header_table _blocks;
    producer_schedule_singleton _active_schedule;
    pending_schedule_singleton _pending_schedule;
    store_meter_singleton _store_meter;
};

}
