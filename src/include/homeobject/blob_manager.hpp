#pragma once
#include <memory>
#include <optional>
#include <string>

#include <sisl/fds/buffer.hpp>

#include "common.hpp"

namespace homeobject {

ENUM(BlobError, uint16_t, UNKNOWN = 1, TIMEOUT, INVALID_ARG, NOT_LEADER, UNKNOWN_SHARD, UNKNOWN_BLOB,
     CHECKSUM_MISMATCH);

struct Blob {
    Blob(sisl::io_blob_safe b, std::string const& u, uint64_t o) : body(std::move(b)), user_key(u), object_off(o) {}

    Blob clone() const;

    sisl::io_blob_safe body;
    std::string user_key;
    uint64_t object_off;
    std::optional< peer_id > current_leader{std::nullopt};
};

class BlobManager : public Manager< BlobError > {
public:
    virtual AsyncResult< blob_id > put(shard_id shard, Blob&&) = 0;
    virtual AsyncResult< Blob > get(shard_id shard, blob_id const& blob, uint64_t off = 0, uint64_t len = 0) const = 0;
    virtual NullAsyncResult del(shard_id shard, blob_id const& blob) = 0;
};

} // namespace homeobject
