#include <chrono>

#include "homeobject.hpp"

namespace homeobject {

uint64_t ShardManager::max_shard_size() { return Gi; }

ShardManager::Result< ShardInfo > MemoryHomeObject::_create_shard(pg_id pg_owner, uint64_t size_bytes) {
    auto const now = get_current_timestamp();
    auto info = ShardInfo(0ull, pg_owner, ShardInfo::State::OPEN, now, now, size_bytes, size_bytes, 0);
    {
        auto lg = std::scoped_lock(_pg_lock, _shard_lock);
        auto pg_it = _pg_map.find(pg_owner);
        if (_pg_map.end() == pg_it) return folly::makeUnexpected(ShardError::UNKNOWN_PG);

        auto& s_list = pg_it->second.shards;
        info.id = make_new_shard_id(pg_owner, s_list.size());
        auto iter = s_list.emplace(s_list.end(), Shard(info));
        LOGDEBUG("Creating Shard [{}]: in Pg [{}] of Size [{}b]", info.id & shard_mask, pg_owner, size_bytes);
        auto [_, s_happened] = _shard_map.emplace(info.id, iter);
        RELEASE_ASSERT(s_happened, "Duplicate Shard insertion!");
    }
    auto [it, happened] = _in_memory_index.try_emplace(info.id, std::make_unique< ShardIndex >());
    RELEASE_ASSERT(happened, "Could not create BTree!");
    return info;
}

ShardManager::Result< ShardInfo > MemoryHomeObject::_seal_shard(shard_id id) {
    auto lg = std::scoped_lock(_shard_lock);
    auto shard_it = _shard_map.find(id);
    RELEASE_ASSERT(_shard_map.end() != shard_it, "Missing ShardIterator!");
    auto& shard_info = (*shard_it->second).info;
    shard_info.state = ShardInfo::State::SEALED;
    return shard_info;
}

} // namespace homeobject
