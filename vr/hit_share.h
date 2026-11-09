#pragma once
// hit_share.h - [multiplayer] share_hit_outcomes: the host tells each joined player what his own blows did, so a joined player's swing timer
// shows hit / miss / blocked / dodged / avoided and the life taken (a crit too) as the host's does. Serg, 2026-11-08: "нужно видеть хит, мисс
// и т.п., для чего всё это делалось, если этого не видно" - and "кто сервер, того галочка и учитывается при передаче".
//
// Why the host: the roll (0x44B720) and the damage (0x44CE80) run on the game's server only. A joined client gets the consequences (the
// monster's life in 1/128, its get-hit / block mode), never a miss, the exact life or a crit, nor whose blow it was. The host's two read-only
// observers ([debug] hit_outcome, vr/hit_outcome.h) see every player's blow with the attacker's unit id; the unit ids of the heroes are the
// same on the server and on every client (the game's own packets name units by them).
//
// The channel ("HS", D2RLoader's plugin network, as vr/host_rules.h's "HR"):
//   - a joined client with its swing timer's numbers on says hello once a second: the version, its process's random number, its hero's id;
//   - the host answers each hello with its number and whether its box is ticked (only the HOST's box counts: a joined player's own box
//     means nothing in that game);
//   - with the box ticked, the host sends each peer the outcomes of that peer's hero only, a few records a message, as they come.
// D2RLoader connects a single-player game and a host to their own channel too: a hello with the host's own number is our own process and
// gets no outcomes (its own timer reads them straight from the observers).
//
// Only shown: nothing here changes a roll, a blow or what the game sends. Pure (no Windows, no game): cleanroom/tests/hit_share_test.cpp.
// The game's side: vr/hit_share_wire.cpp.

#include <cstdint>

#include "hit_outcome.h"

namespace hitshare {

constexpr uint16_t kChannel = 0x4853;                    // "HS"
constexpr uint64_t kToken = 0x3153484D56523244ull;       // "D2RVMHS1": client and host connect only with the same
constexpr uint16_t kMsgHello = 1, kMsgState = 2, kMsgOutcomes = 3;
constexpr uint8_t kVersion = 1;
constexpr uint32_t kMaxPayload = 224;                    // D2RLoader's Network::MaxPayloadSize
constexpr uint32_t kHelloBytes = 9;                      // version, our number, our hero's id
constexpr uint32_t kStateBytes = 10;                     // version, the asker's number, the host's number, the host's box
constexpr uint32_t kOutHead = 6;                         // version, the host's number, the count
constexpr uint32_t kRecBytes = 11;                       // kind, flags (2), life taken (4, 1/256), attacker (4)
constexpr int kRecsPerMsg = 16;
static_assert(kOutHead + kRecsPerMsg * kRecBytes <= kMaxPayload, "an outcomes message must fit D2RLoader's payload");
constexpr uint64_t kHelloMs = 1000, kFreshMs = 3500;     // a peer that said nothing for this long gets nothing more
constexpr int kMaxPeers = 8;                             // a TCP/IP game holds eight heroes

inline void Put16(uint8_t* b, uint16_t v) { b[0] = static_cast<uint8_t>(v); b[1] = static_cast<uint8_t>(v >> 8); }
inline uint16_t Get16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | b[1] << 8); }
inline void Put32(uint8_t* b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i)); }
inline uint32_t Get32(const uint8_t* b) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[i]) << (8 * i);
    return v;
}
inline bool Fresh(uint64_t now, uint64_t at, uint64_t forMs = kFreshMs) { return at && now >= at && now - at <= forMs; }

// ---- the hello (client -> host) ----
inline uint32_t BuildHello(uint32_t nonce, uint32_t hero, uint8_t* b, uint32_t cap) {
    if (!b || cap < kHelloBytes || !nonce || !hero) return 0;
    b[0] = kVersion;
    Put32(b + 1, nonce);
    Put32(b + 5, hero);
    return kHelloBytes;
}
inline bool ParseHello(const void* data, uint32_t size, uint32_t* nonce, uint32_t* hero) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size != kHelloBytes || b[0] != kVersion) return false;
    const uint32_t n = Get32(b + 1), h = Get32(b + 5);
    if (!n || !h) return false;
    *nonce = n;
    *hero = h;
    return true;
}

// ---- the host's answer: its number and its box (host -> client) ----
inline uint32_t BuildState(uint32_t asker, uint32_t host, bool sharing, uint8_t* b, uint32_t cap) {
    if (!b || cap < kStateBytes || !host) return 0;
    b[0] = kVersion;
    Put32(b + 1, asker);
    Put32(b + 5, host);
    b[9] = sharing ? 1 : 0;
    return kStateBytes;
}
inline bool ParseState(const void* data, uint32_t size, uint32_t* asker, uint32_t* host, bool* sharing) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size != kStateBytes || b[0] != kVersion) return false;
    *asker = Get32(b + 1);
    *host = Get32(b + 5);
    *sharing = b[9] != 0;
    return *host != 0;
}

// ---- the outcomes (host -> client) ----
// One outcome as it travels: a roll (its flags) or a blow that landed (its result flags with the crit bit, the life it took in 1/256).
struct Rec {
    hitoutcome::Kind kind = hitoutcome::Kind::None;
    uint16_t flags = 0;
    int32_t took = 0;
    uint32_t attacker = 0;
};

// The life a landed blow took, 1/256: as the swing timer counts it (vr/swing_timer_wire.cpp TakeNews) - the monster's life before and after
// the game's call, else the damage record's own number.
inline int32_t TookOf(const hitoutcome::Record& r) {
    if (r.lifeBefore >= 0 && r.lifeAfter >= 0) return r.lifeBefore - r.lifeAfter;
    return r.recordLife >= 0 ? r.recordLife : 0;
}
inline bool RecOf(const hitoutcome::Record& r, Rec* out) {
    if (r.kind != hitoutcome::Kind::Roll && r.kind != hitoutcome::Kind::Damage) return false;
    if (!r.attacker) return false;
    out->kind = r.kind;
    out->flags = r.flags;
    out->took = r.kind == hitoutcome::Kind::Damage ? TookOf(r) : 0;
    out->attacker = r.attacker;
    return true;
}
// Back into a record for the client's swing timer (vr/hit_outcome.h LabelRing): stamped with the client's own clock at arrival, the life
// taken as the record's number (TakeNews reads it when before / after are not known).
inline hitoutcome::Record RecordOf(const Rec& x, uint64_t nowMs) {
    hitoutcome::Record r;
    r.kind = x.kind;
    r.flags = x.flags;
    r.attacker = x.attacker;
    r.recordLife = x.kind == hitoutcome::Kind::Damage ? (x.took > 0 ? x.took : 0) : -1;
    r.ms = nowMs;
    return r;
}

inline uint32_t BuildOutcomes(uint32_t host, const Rec* recs, int n, uint8_t* b, uint32_t cap) {
    if (!b || !recs || n <= 0 || n > kRecsPerMsg || !host) return 0;
    const uint32_t size = kOutHead + static_cast<uint32_t>(n) * kRecBytes;
    if (cap < size) return 0;
    b[0] = kVersion;
    Put32(b + 1, host);
    b[5] = static_cast<uint8_t>(n);
    uint8_t* p = b + kOutHead;
    for (int i = 0; i < n; ++i, p += kRecBytes) {
        p[0] = static_cast<uint8_t>(recs[i].kind);
        Put16(p + 1, recs[i].flags);
        Put32(p + 3, static_cast<uint32_t>(recs[i].took));
        Put32(p + 7, recs[i].attacker);
    }
    return size;
}
// False: not an outcomes message of this version, or a malformed one. A record of a kind this client does not know is skipped.
inline bool ParseOutcomes(const void* data, uint32_t size, uint32_t* host, Rec* out, int cap, int* n) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !out || !n || size < kOutHead || b[0] != kVersion) return false;
    const int count = b[5];
    if (count < 1 || count > kRecsPerMsg || size != kOutHead + static_cast<uint32_t>(count) * kRecBytes) return false;
    *host = Get32(b + 1);
    int got = 0;
    const uint8_t* p = b + kOutHead;
    for (int i = 0; i < count; ++i, p += kRecBytes) {
        const uint8_t kind = p[0];
        if (kind != static_cast<uint8_t>(hitoutcome::Kind::Roll) && kind != static_cast<uint8_t>(hitoutcome::Kind::Damage)) continue;
        if (got >= cap) break;
        Rec& r = out[got++];
        r.kind = static_cast<hitoutcome::Kind>(kind);
        r.flags = Get16(p + 1);
        r.took = static_cast<int32_t>(Get32(p + 3));
        r.attacker = Get32(p + 7);
    }
    *n = got;
    return *host != 0;
}

// ---- the host's peers: who spoke lately, for which hero ----
struct Peer {
    uint64_t handle = 0;     // D2RLoader's peer handle
    uint32_t nonce = 0;      // its process's number
    uint32_t hero = 0;       // its hero's unit id
    uint64_t helloMs = 0;
};

class Peers {
public:
    // A hello: that peer speaks for that hero now. False: our own process (its nonce is ours), or no room (eight fresh peers already).
    bool Hello(uint64_t handle, uint32_t nonce, uint32_t hero, uint32_t ownNonce, uint64_t now) {
        if (!handle || !nonce || !hero || nonce == ownNonce) return false;
        Peer* free = nullptr;
        for (Peer& p : p_) {
            if (p.handle == handle) {
                p.nonce = nonce;
                p.hero = hero;
                p.helloMs = now;
                return true;
            }
            if (!free && (!p.handle || !hitshare::Fresh(now, p.helloMs))) free = &p;
        }
        if (!free) return false;
        *free = Peer{handle, nonce, hero, now};
        return true;
    }
    void Drop(uint64_t handle) {
        for (Peer& p : p_) if (p.handle == handle) p = Peer{};
    }
    void Clear() { for (Peer& p : p_) p = Peer{}; }
    // The fresh peer whose hero this is (0 none).
    uint64_t HandleOf(uint32_t hero, uint64_t now) const {
        if (!hero) return 0;
        for (const Peer& p : p_) if (p.handle && p.hero == hero && hitshare::Fresh(now, p.helloMs)) return p.handle;
        return 0;
    }
    int Count(uint64_t now) const {
        int n = 0;
        for (const Peer& p : p_) if (p.handle && hitshare::Fresh(now, p.helloMs)) ++n;
        return n;
    }
    const Peer* All() const { return p_; }

private:
    Peer p_[kMaxPeers];
};

// The host's sorting of the outcomes into one batch per peer: Add each record of a drain, then Take each peer's batch.
class Batches {
public:
    // False: no fresh peer has that hero (the host's own, a merc's master gone, a player without the mod) - dropped.
    bool Add(const Peers& peers, const Rec& r, uint64_t now) {
        const uint64_t h = peers.HandleOf(r.attacker, now);
        if (!h) return false;
        for (Batch& b : b_) {
            if (b.handle == h) {
                if (b.n < kMaxPerPeer) b.r[b.n++] = r;
                else ++b.over;
                return true;
            }
        }
        for (Batch& b : b_) {
            if (!b.handle) {
                b.handle = h;
                b.r[b.n++] = r;
                return true;
            }
        }
        return false;
    }
    struct Batch {
        uint64_t handle = 0;
        int n = 0;
        uint32_t over = 0;   // past kMaxPerPeer in one drain: dropped (counted for the log)
        Rec r[64];
    };
    static constexpr int kMaxPerPeer = 64;
    const Batch* All() const { return b_; }
    void Clear() { for (Batch& b : b_) { b.handle = 0; b.n = 0; b.over = 0; } }

private:
    Batch b_[kMaxPeers];
};

}  // namespace hitshare
