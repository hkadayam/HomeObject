#include "homestore_backend/hs_homeobject.hpp"

namespace homeobject {

PGError toPgError(ReplServiceError const& e) {
    switch (e) {
    case ReplServiceError::BAD_REQUEST:
        [[fallthrough]];
    case ReplServiceError::CANCELLED:
        [[fallthrough]];
    case ReplServiceError::CONFIG_CHANGING:
        [[fallthrough]];
    case ReplServiceError::SERVER_ALREADY_EXISTS:
        [[fallthrough]];
    case ReplServiceError::SERVER_IS_JOINING:
        [[fallthrough]];
    case ReplServiceError::SERVER_IS_LEAVING:
        [[fallthrough]];
    case ReplServiceError::RESULT_NOT_EXIST_YET:
        [[fallthrough]];
    case ReplServiceError::NOT_LEADER:
        [[fallthrough]];
    case ReplServiceError::TERM_MISMATCH:
        return PGError::INVALID_ARG;
    case ReplServiceError::CANNOT_REMOVE_LEADER:
        return PGError::UNKNOWN_PEER;
    case ReplServiceError::TIMEOUT:
        return PGError::TIMEOUT;
    case ReplServiceError::SERVER_NOT_FOUND:
        return PGError::UNKNOWN_PG;
    case ReplServiceError::OK:
        DEBUG_ASSERT(false, "Should not process OK!");
        [[fallthrough]];
    case ReplServiceError::FAILED:
        return PGError::UNKNOWN;
    }
    return PGError::UNKNOWN;
}

static homestore::ReplDev& pg_repl_dev(PG const& pg) { return *(static_cast< HS_PG const& >(pg).repl_dev_); }

PGManager::NullAsyncResult HSHomeObject::_create_pg(PGInfo&& pg_info, std::set< std::string, std::less<> > peers) {
    pg_info.repl_dev_uuid = boost::uuids::random_generator()();
    return hs_repl_service()
        .create_repl_dev(pg_info.repl_dev_uuid, std::move(peers), std::make_unique< ReplicationStateMachine >(this))
        .thenValue([this, pg_info = std::move(pg_info)](auto&& v) -> PGManager::NullResult {
            if (v.hasError()) { return folly::makeUnexpected(PGError::INVALID_ARG); }
            add_pg_to_map(std::make_shared< HS_PG >(std::move(pg_info), std::move(v.value())));
            return folly::Unit();
        });
}

PGManager::Result< HS_PG* > HSHomeObject::_get_pg(pg_id_t pgid) const {
    std::shared_lock lg{_pg_lock};
    if (auto const it = _pg_map.find(pgid); it != _pg_map.cend()) {
        return it->second.get();
    } else {
        return folly::makeUnexpected(PGError::UNKNOWN_PG);
    }
}

PGManager::NullAsyncResult HSHomeObject::_replace_member(pg_id_t id, peer_id_t const& old_member,
                                                         PGMember const& new_member) {
    return folly::makeUnexpected(PGError::UNSUPPORTED_OP);
}

void HSHomeObject::add_pg_to_map(shared< HS_PG > hs_pg) {
    RELEASE_ASSERT_EQ(pg_info.replica_set_uuid, rdev->group_id(),
                      "PGInfo replica set uuid mismatch with ReplDev instance");
    auto lg = std::scoped_lock(_pg_lock);
    auto [it1, _] = _pg_map.try_emplace(pg_info.id, std::move(hs_pg));
    RELEASE_ASSERT(_pg_map.end() != it1, "Unknown map insert error!");
}

std::string HSHomeObject::serialize_pg_info(PGInfo const& pginfo) {
    nlohmann::json j;
    j["pg_info"]["pg_id_t"] = pginfo.id;
    j["pg_info"]["repl_uuid"] = boost::uuids::to_string(pginfo.replica_set_uuid);

    nlohmann::json members_j = {};
    for (auto const& member : pginfo.members) {
        nlohmann::json member_j;
        member_j["member_id"] = member.id;
        member_j["name"] = member.name;
        member_j["priority"] = member.priority;
        members_j.push_back(member_j);
    }
    j["pg_info"]["members"] = members_j;
    return j.dump();
}

PGInfo HSHomeObject::deserialize_pg_info(std::string const& json_str) {
    auto pg_json = nlohmann::json::parse(json_str);

    PGInfo pg_info;
    pg_info.id = pg_json["pg_info"]["pg_id_t"].get< pg_id_t >();
    pg_info.replica_set_uuid = boost::uuids::string_generator()(pg_json["pg_info"]["repl_uuid"].get< std::string >());

    for (auto const& m : pg_info["pg_info"]["members"]) {
        PGMember member;
        member.id = m["member_id"].get< pg_id_t >();
        member.name = m["name"].get< std::string >();
        member.priority = m["priority"].get< int32_t >();
        pg_info.members.emplace(std::move(member));
    }
    return pg_info;
}

void HSHomeObject::on_pg_meta_blk_found(sisl::byte_view const& buf, void* meta_cookie) {
    homestore::superblk< pg_info_superblk > pg_sb;
    pg_sb.load(buf, meta_cookie);

    hs_repl_service()
        .open_repl_dev(pg_sb->repl_dev_uuid, std::make_unique< ReplicationStateMachine >(this))
        .thenValue([this, pg_sb = std::move(pg_sb)](auto&& v) {
            if (v.hasError()) {
                LOGERROR("open_repl_dev for group_id={} has failed", pg_sb->repl_dev_uuid);
                return;
            }
            add_pg_to_map(std::make_shared< HS_PG >(pg_sb, std::move(v.value())));
        });
}

static PGInfo pg_info_from_sb(homestore::superblk< pg_info_superblk > const& sb) {
    PGInfo pginfo;
    pginfo.id = sb->id;

    auto mptr = r_cast< pg_members* >(uintptr_cast(sb.get()) + sizeof(pg_info_superblk));
    for (uint32_t i{0}; i < sb->num_members; ++i, ++mptr) {
        pginfo.members.emplace(mptr->id, std::string(mptr->name), mptr->priority);
    }
    return pginfo;
}

HSHomeObject::HS_PG(PGInfo info, shared< homestore::ReplDev > rdev) :
        PG{std::move(info)}, pg_sb_{"PG"}, repl_dev_{std::move(rdev)} {
    auto* hdr = pg_sb_.create(sizeof(pg_info_superblk) + (pg_info_.members.size() * sizeof(pg_members)));
    pg_sb_->id = pg_info_.id;
    pg_sb_->num_members = pg_info_.members.size();
    pg_sb_->repl_dev_uuid = repl_dev_->group_id();

    auto mptr = r_cast< pg_members* >(uintptr_cast(hdr) + sizeof(pg_info_superblk));
    for (auto const& m : members) {
        mptr->id = m.id;
        std::strncpy(mptr->name, m.name, std::min(m.name.size(), pg_member_serialized::max_name_len));
        mptr->priority = m.priority;
        ++mptr;
    }

    pg_sb_.write();
}

HSHomeObject::HS_PG(homestore::superblk< pg_info_superblk > const& sb, shared< homestore::ReplDev > rdev) :
        PG{pg_info_from_sb(sb)}, pg_sb_{sb}, repl_dev_{std::move(rdev)} {}

template < typename T >
T HSHomeObject::HS_PG::write_and_wait() {}

} // namespace homeobject