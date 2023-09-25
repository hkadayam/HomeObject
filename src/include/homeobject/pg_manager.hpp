#pragma once
#include <set>
#include <string>

#include <sisl/utility/enum.hpp>

#include "common.hpp"

namespace homeobject {

ENUM(PGError, uint16_t, UNKNOWN = 1, INVALID_ARG, TIMEOUT, UNKNOWN_PG, UNKNOWN_PEER, UNSUPPORTED_OP);

struct PGMember {
    explicit PGMember(peer_id_t _id) : id(_id) {}
    PGMember(peer_id_t _id, std::string const& _name) : id(_id), name(_name) {}
    PGMember(peer_id_t _id, std::string const& _name, int32_t _priority) : id(_id), name(_name), priority(_priority) {}
    peer_id_t id;
    std::string name;
    int32_t priority{0}; // <0 (Arbiter), ==0 (Follower), >0 (F|Leader)
};

using MemberSet = std::set< PGMember >;

struct PGInfo {
    explicit PGInfo(pg_id_t _id) : id(_id) {}
    pg_id_t id;
    mutable MemberSet members;
};

class PGManager : public Manager< PGError > {
public:
    virtual NullAsyncResult create_pg(PGInfo&& pg_info) = 0;
    virtual NullAsyncResult replace_member(pg_id_t id, peer_id_t const& old_member, PGMember const& new_member) = 0;
};

inline bool operator<(homeobject::PGMember const& lhs, homeobject::PGMember const& rhs) { return lhs.id < rhs.id; }
inline bool operator<(homeobject::PGInfo const& lhs, homeobject::PGInfo const& rhs) { return lhs.id < rhs.id; }

} // namespace homeobject
