#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/crypto.h>

namespace eosio {

using digest_type = capi_checksum256;

template <typename T>
capi_checksum256 sha256(const T& value) {
    auto digest = pack(value);
    ::capi_checksum256 hash;
    ::sha256(digest.data(), uint32_t(digest.size()), &hash);
    return hash;
}

}
