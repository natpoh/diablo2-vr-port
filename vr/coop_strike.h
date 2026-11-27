// coop_strike.h - [gameplay] melee_direct_client_host: Direct hit for a JOINED player, struck by the host with the time put back (Serg,
// 2026-11-26: «давай чинить директ хит по медвежьему принципу, но с поправкой на время»). Off by default; who hosts decides.
//
// The idea of the host resolving a joined player's touch is from D2R VR Unleashed, multiplayer PR #519 by geekstreetstudios, merged by
// Bear (Berk) into bearstonem/d2r-pcvr (MPL 2.0; releases/analysis/bear_coop_direct_hit_2026-11-26.md). Nothing of their code is here:
// written anew on our own channel, units and Direct hit. Ours on top: the host checks the touch where the units WERE when the player saw
// it (the ping put back), the attack rate is read on the host from the hero's own server unit (a client never says its cycle), and the
// client's own rules (melee_direct_client.h) still pick which touch is a direct hit.
//
// Why. A joined player has no server in his process. Today his touch goes as the game's command "attack this monster" (cmd 6): the host's
// server walks the hero up to a monster the VR blade reached beyond the game's reach, waits for the attack rate and plays the animation.
// Bear's first version did the same and saw the hero stop on every swing and walk to the next target; their later one, and this, send the
// touch itself: the host's plugin strikes it with the server's attack for the joined hero (melee_direct.cpp, Attack 0x42FEB0) - the roll,
// the damage, the wear are the game's, the animation, the walk and the attack-rate gate are not. That is Serg's Direct hit exception
// (never-break-game-mechanics) extended to a joined player, by his word.
//
// The lag. The player saw the monster where the host's server had it one trip ago, and he swung `age` ms before his message went out; the
// message took another trip. So on the host the touch is judged at now - age - rtt (RewindOf): the monster's place from the host's own
// history of the server's units (Track, sampled on the game's thread), the hero's place matched against what the player says it was.
// The rtt is the host's own measure (its ping, the client's pong), never the client's word; the rewind is capped (kMaxRewindMs).
//
// The checks (Judge), all on the host's server units, a touch struck only when every one holds:
//   - the monster was alive at the rewound time (and is there now), hostile to the joined hero, no wall between (the game's Line);
//   - the hero's server place over the window matches the place the player reported (kHeroTolCells): no strike from where he is not;
//   - the monster at the rewound time within kReachCells + kTolCells of the hero's matched server place (and the player's own picture
//     agrees: his hero and the monster as he saw them within the same reach);
//   - the pace (Pace): not faster than the hero's attack as the game plays it on the HOST (measured from his server unit's mode; until
//     measured kDefaultAttackMs); a two-handed swing's further monsters (the cleave) only with the host's cleave box, once each, within
//     the attack, at most kMaxExtras.
// A refused touch goes back to the player, who presses the normal attack for it (never nothing, Serg's rule of 2026-10-27).
//
// This header is pure: the messages, the history, the rewind, the checks, the pace. The game's side is vr/coop_strike_wire.cpp; the test
// is cleanroom/tests/coop_strike_test.cpp.
#pragma once

#include <cmath>
#include <cstdint>

namespace coopstrike {

// ---- the messages: on melee_direct_client's channel (kChannel 0x4448 "DH", the same token), new ids next to its strike / hello / rule ----
constexpr uint16_t kMsgSwing = 4;       // client -> host: a touch to strike
constexpr uint16_t kMsgVerdict = 5;     // host -> client: struck or refused (why), the rewind used
constexpr uint16_t kMsgPing = 6;        // host -> client: the host's clock
constexpr uint16_t kMsgPong = 7;        // client -> host: that clock back, and the client's hero
constexpr uint16_t kMsgSwingRule = 8;   // host -> client, with the answer to each hello: the host strikes joined players' touches
constexpr uint8_t kVersion = 1;

inline void Put16(uint8_t* b, uint32_t v) { b[0] = static_cast<uint8_t>(v); b[1] = static_cast<uint8_t>(v >> 8); }
inline void Put32(uint8_t* b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i)); }
inline uint32_t Get16(const uint8_t* b) { return static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8; }
inline uint32_t Get32(const uint8_t* b) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[i]) << (8 * i); return v; }

// A measure that may be unknown (-1): fixed point in 16 bits, 0xFFFF = unknown, clamped below it.
constexpr uint32_t kUnknown16 = 0xFFFF;
inline uint32_t Fix16(float v, float scale) {
    if (!(v >= 0.0f) || !std::isfinite(v)) return kUnknown16;
    const float s = v * scale + 0.5f;
    return s >= static_cast<float>(kUnknown16 - 1) ? kUnknown16 - 1 : static_cast<uint32_t>(s);
}
inline float Unfix16(uint32_t v, float scale) { return v == kUnknown16 ? -1.0f : static_cast<float>(v) / scale; }
// A place on the grid in cells (the server's 16.16 path coordinates), as 24.8 fixed point.
constexpr float kPosScale = 256.0f;
inline uint32_t FixPos(float cells) {
    if (!(cells >= 0.0f) || !std::isfinite(cells) || cells >= 16777215.0f / kPosScale) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(cells * kPosScale + 0.5f);
}
inline float UnfixPos(uint32_t v) { return v == 0xFFFFFFFFu ? -1.0f : static_cast<float>(v) / kPosScale; }

// The touch, as the player's plugin saw it.
struct Swing {
    uint16_t seq = 0;            // per client, wraps
    uint8_t hand = 0;            // the controller whose blade touched (skeleton side: 0 right, 1 left)
    bool extra = false;          // the client's rules call it a further monster of a two-handed swing (the cleave)
    bool twoHanded = false, twoHandsOn = false;
    uint32_t hero = 0, monster = 0;
    uint16_t ageMs = 0;          // from the touch to the send, the client's clock
    float speed = -1.0f;         // m/s where it touched (-1 unknown)
    float force = -1.0f;         // N s, the weight model's strike score (-1 unknown)
    float length = -1.0f;        // m, the controller's path in the swing (-1 unknown)
    float heroX = -1.0f, heroY = -1.0f;   // the client's hero, cells (its own client unit)
    float monX = -1.0f, monY = -1.0f;     // the monster as the client had it, cells
    int16_t skill = -1;          // the hero's left skill (for the log)
};
constexpr uint32_t kSwingBytes = 39;
inline uint32_t BuildSwing(const Swing& s, uint8_t* b, uint32_t cap) {
    if (!b || cap < kSwingBytes || !s.hero || !s.monster) return 0;
    b[0] = kVersion;
    Put16(b + 1, s.seq);
    b[3] = s.hand & 1;
    b[4] = static_cast<uint8_t>((s.extra ? 1 : 0) | (s.twoHanded ? 2 : 0) | (s.twoHandsOn ? 4 : 0));
    Put32(b + 5, s.hero);
    Put32(b + 9, s.monster);
    Put16(b + 13, s.ageMs);
    Put16(b + 15, Fix16(s.speed, 100.0f));
    Put16(b + 17, Fix16(s.force, 100.0f));
    Put16(b + 19, Fix16(s.length, 1000.0f));
    Put32(b + 21, FixPos(s.heroX));
    Put32(b + 25, FixPos(s.heroY));
    Put32(b + 29, FixPos(s.monX));
    Put32(b + 33, FixPos(s.monY));
    Put16(b + 37, static_cast<uint16_t>(s.skill));
    return kSwingBytes;
}
inline bool ParseSwing(const void* data, uint32_t size, Swing* s) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !s || size != kSwingBytes || b[0] != kVersion || b[3] > 1 || b[4] > 7) return false;
    Swing o;
    o.seq = static_cast<uint16_t>(Get16(b + 1));
    o.hand = b[3];
    o.extra = (b[4] & 1) != 0;
    o.twoHanded = (b[4] & 2) != 0;
    o.twoHandsOn = (b[4] & 4) != 0;
    o.hero = Get32(b + 5);
    o.monster = Get32(b + 9);
    o.ageMs = static_cast<uint16_t>(Get16(b + 13));
    o.speed = Unfix16(Get16(b + 15), 100.0f);
    o.force = Unfix16(Get16(b + 17), 100.0f);
    o.length = Unfix16(Get16(b + 19), 1000.0f);
    o.heroX = UnfixPos(Get32(b + 21));
    o.heroY = UnfixPos(Get32(b + 25));
    o.monX = UnfixPos(Get32(b + 29));
    o.monY = UnfixPos(Get32(b + 33));
    o.skill = static_cast<int16_t>(Get16(b + 37));
    if (!o.hero || !o.monster || o.heroX < 0.0f || o.heroY < 0.0f) return false;   // (the hero's place is a must: it is checked)
    *s = o;
    return true;
}

// Why the host struck a touch or did not. Every one but Struck is refused - the player's plugin presses the normal attack for it.
enum class Why : uint8_t {
    Struck = 0,
    Off,            // the host's box is unticked (or it cannot strike: no server game learned, Direct hit's hooks not in)
    PeerHero,       // the peer named a hero not its own (another peer's, or the host's)
    TooOld,         // the touch is older than the host puts back (kMaxRewindMs) - it would be judged on guesses
    NoHero,         // the joined hero has no history on the host (not on the server, or just arrived)
    HeroMode,       // the joined hero is dead, dying, in a portal
    HeroMoved,      // the hero's server place over the window is not where the player says he stood
    NoMonster,      // the monster has no history at the rewound time (not on the server, or not near the hero)
    MonsterDead,    // dead or dying at the rewound time
    NotHostile,     // not an enemy of the joined hero (the server's Relation, now)
    Wall,           // a wall between (the server's Line, now)
    Far,            // beyond the reach at the rewound time (or in the player's own picture)
    TooFast,        // sooner than the hero's attack takes (his attack as the host's server plays it)
    Again,          // the same monster again within one attack (a cleave counts each monster once)
    Busy,           // too many at once - the queue is full
    kCount
};
inline const char* WhyName(Why w) {
    switch (w) {
        case Why::Struck: return "struck";
        case Why::Off: return "off on the host (its box unticked, or it cannot strike now)";
        case Why::PeerHero: return "the peer named another hero than its own";
        case Why::TooOld: return "too old to put back";
        case Why::NoHero: return "no history of the hero on the host";
        case Why::HeroMode: return "the hero is dead, dying or in a portal";
        case Why::HeroMoved: return "the hero was not where the player says";
        case Why::NoMonster: return "no history of the monster at that time";
        case Why::MonsterDead: return "the monster was dead or dying then";
        case Why::NotHostile: return "not hostile";
        case Why::Wall: return "a wall between";
        case Why::Far: return "beyond the reach then";
        case Why::TooFast: return "faster than the hero's attack";
        case Why::Again: return "the same monster again in one attack";
        case Why::Busy: return "too many at once";
        default: return "?";
    }
}

struct Verdict { uint16_t seq = 0; uint8_t hand = 0; Why why = Why::Off; uint16_t rewindMs = 0; };
constexpr uint32_t kVerdictBytes = 7;
inline uint32_t BuildVerdict(const Verdict& v, uint8_t* b, uint32_t cap) {
    if (!b || cap < kVerdictBytes) return 0;
    b[0] = kVersion;
    Put16(b + 1, v.seq);
    b[3] = v.hand & 1;
    b[4] = static_cast<uint8_t>(v.why);
    Put16(b + 5, v.rewindMs);
    return kVerdictBytes;
}
inline bool ParseVerdict(const void* data, uint32_t size, Verdict* v) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !v || size != kVerdictBytes || b[0] != kVersion || b[3] > 1 || b[4] >= static_cast<uint8_t>(Why::kCount)) return false;
    v->seq = static_cast<uint16_t>(Get16(b + 1));
    v->hand = b[3];
    v->why = static_cast<Why>(b[4]);
    v->rewindMs = static_cast<uint16_t>(Get16(b + 5));
    return true;
}

// The ping (the host's clock, low 32 bits of its ms) and the pong (that clock back + the client's hero).
constexpr uint32_t kPingBytes = 5, kPongBytes = 9;
inline uint32_t BuildPing(uint32_t stamp, uint8_t* b, uint32_t cap) {
    if (!b || cap < kPingBytes) return 0;
    b[0] = kVersion;
    Put32(b + 1, stamp);
    return kPingBytes;
}
inline bool ParsePing(const void* data, uint32_t size, uint32_t* stamp) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !stamp || size != kPingBytes || b[0] != kVersion) return false;
    *stamp = Get32(b + 1);
    return true;
}
inline uint32_t BuildPong(uint32_t stamp, uint32_t hero, uint8_t* b, uint32_t cap) {
    if (!b || cap < kPongBytes || !hero) return 0;
    b[0] = kVersion;
    Put32(b + 1, stamp);
    Put32(b + 5, hero);
    return kPongBytes;
}
inline bool ParsePong(const void* data, uint32_t size, uint32_t* stamp, uint32_t* hero) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !stamp || !hero || size != kPongBytes || b[0] != kVersion) return false;
    *stamp = Get32(b + 1);
    *hero = Get32(b + 5);
    return *hero != 0;
}
constexpr uint32_t kSwingRuleBytes = 2;
inline uint32_t BuildSwingRule(bool on, uint8_t* b, uint32_t cap) {
    if (!b || cap < kSwingRuleBytes) return 0;
    b[0] = kVersion;
    b[1] = on ? 1 : 0;
    return kSwingRuleBytes;
}
inline bool ParseSwingRule(const void* data, uint32_t size, bool* on) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || !on || size != kSwingRuleBytes || b[0] != kVersion || b[1] > 1) return false;
    *on = b[1] != 0;
    return true;
}
// The round trip from a pong: the host's ms now against the stamp it sent (32-bit wrap safe). A trip over kMaxRttMs is not one.
constexpr uint32_t kMaxRttMs = 2000;
inline bool RttOf(uint32_t nowLow, uint32_t stamp, uint32_t* rtt) {
    const uint32_t d = nowLow - stamp;
    if (d > kMaxRttMs) return false;
    *rtt = d;
    return true;
}

// ---- the round trip, per peer: smoothed, the lower samples weigh more (a late frame is not the network) ----
class Rtt {
 public:
    void Add(uint32_t ms) {
        if (ms > kMaxRttMs) return;
        if (!n_) avg_ = static_cast<float>(ms);
        else avg_ += (static_cast<float>(ms) - avg_) * (static_cast<float>(ms) < avg_ ? 0.25f : 0.125f);
        ++n_;
    }
    bool Known() const { return n_ >= 2; }
    // The round trip in ms; until two pongs came, kDefaultRttMs.
    uint32_t Ms() const { return Known() ? static_cast<uint32_t>(avg_ + 0.5f) : kDefaultRttMs; }
    void Clear() { n_ = 0; avg_ = 0.0f; }
    static constexpr uint32_t kDefaultRttMs = 100;
 private:
    unsigned n_ = 0;
    float avg_ = 0.0f;
};

// ---- when the player saw what he struck, on the host's clock ----
constexpr uint32_t kMaxRewindMs = 600;   // the most the host puts back (a touch older is TooOld)
constexpr uint32_t kHeroSlackMs = 120;   // the hero's window reaches this much further back (the client's own prediction and frame)
struct Times {
    uint64_t monsterAt = 0;          // the monster's place as the player saw it: now - age - rtt
    uint64_t heroFrom = 0, heroTo = 0;   // the hero's server places the reported one may match (his own place leads on the client)
    uint64_t swingAt = 0;            // the swing itself on the host's clock: now - age - rtt / 2 (the pace's)
    uint32_t rewindMs = 0;
    bool tooOld = false;
};
inline Times RewindOf(uint64_t nowMs, uint32_t rttMs, uint32_t ageMs) {
    Times t;
    const uint64_t back = static_cast<uint64_t>(ageMs) + rttMs;
    t.tooOld = back > kMaxRewindMs;
    t.rewindMs = static_cast<uint32_t>(back > kMaxRewindMs ? kMaxRewindMs : back);
    const uint64_t swingBack = static_cast<uint64_t>(ageMs) + rttMs / 2;
    t.monsterAt = nowMs > t.rewindMs ? nowMs - t.rewindMs : 0;
    t.swingAt = nowMs > swingBack ? nowMs - swingBack : 0;
    t.heroFrom = t.monsterAt > kHeroSlackMs ? t.monsterAt - kHeroSlackMs : 0;
    t.heroTo = nowMs;
    return t;
}

// ---- the host's history of the server's units: each a ring of places ----
struct Sample { uint64_t t = 0; float x = 0.0f, y = 0.0f; bool alive = true; };
class Track {
 public:
    static constexpr int kN = 64;   // at the host's 25 ms: 1.6 s
    uint32_t id = 0;
    uint64_t seenAt = 0;            // the newest sample's time
    void Reset(uint32_t newId) { id = newId; n_ = 0; head_ = 0; seenAt = 0; }
    void Push(const Sample& s) {
        if (n_ && s.t <= s_[(head_ + kN - 1) % kN].t) return;   // (time only goes on)
        s_[head_] = s;
        head_ = (head_ + 1) % kN;
        if (n_ < kN) ++n_;
        seenAt = s.t;
    }
    int Count() const { return n_; }
    const Sample& Nth(int i) const { return s_[(head_ + kN - n_ + i) % kN]; }   // 0 the oldest
    // The place at time t: between two samples, the line between them (alive: the earlier one's). After the newest, the newest while it is
    // younger than kStaleMs; before the oldest, nothing.
    static constexpr uint64_t kStaleMs = 200;
    bool At(uint64_t t, Sample* out) const {
        if (!n_) return false;
        const Sample& first = Nth(0);
        const Sample& last = Nth(n_ - 1);
        if (t < first.t) return false;
        if (t >= last.t) {
            if (t - last.t > kStaleMs) return false;
            *out = last;
            out->t = t;
            return true;
        }
        for (int i = n_ - 1; i > 0; --i) {
            const Sample& a = Nth(i - 1);
            const Sample& b = Nth(i);
            if (t < a.t || t > b.t) continue;
            const float k = b.t > a.t ? static_cast<float>(t - a.t) / static_cast<float>(b.t - a.t) : 0.0f;
            *out = {t, a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.alive};
            return true;
        }
        return false;
    }
    // The sample nearest to (px, py) over [t0, t1] (the ends taken by At). False: nothing there.
    bool Nearest(float px, float py, uint64_t t0, uint64_t t1, float* dist, Sample* at) const {
        bool any = false;
        float best = 0.0f;
        const auto Try = [&](const Sample& s) {
            const float d = std::sqrt((s.x - px) * (s.x - px) + (s.y - py) * (s.y - py));
            if (!any || d < best) { best = d; *at = s; any = true; }
        };
        Sample e;
        if (At(t0, &e)) Try(e);
        if (At(t1, &e)) Try(e);
        for (int i = 0; i < n_; ++i) {
            const Sample& s = Nth(i);
            if (s.t >= t0 && s.t <= t1) Try(s);
        }
        if (any) *dist = best;
        return any;
    }
 private:
    Sample s_[kN];
    int head_ = 0, n_ = 0;
};

class History {
 public:
    static constexpr int kTracks = 128;
    // The track of unit `id` (a new one in a free or the stalest slot when `make`).
    Track* Get(uint32_t id, bool make) {
        Track* stalest = &t_[0];
        for (Track& t : t_) {
            if (t.id == id && id) return &t;
            if (!t.id || t.seenAt < stalest->seenAt) stalest = &t;
        }
        if (!make || !id) return nullptr;
        stalest->Reset(id);
        return stalest;
    }
    const Track* Find(uint32_t id) const {
        for (const Track& t : t_) if (t.id == id && id) return &t;
        return nullptr;
    }
    void Push(uint32_t id, const Sample& s) { if (Track* t = Get(id, true)) t->Push(s); }
    // Units not seen for keepMs are let go of (they died long ago, left the area).
    void Prune(uint64_t nowMs, uint64_t keepMs) { for (Track& t : t_) if (t.id && nowMs > t.seenAt + keepMs) t.Reset(0); }
    void Clear() { for (Track& t : t_) t.Reset(0); }
 private:
    Track t_[kTracks];
};

// ---- the checks ----
struct Limits {
    float reachCells = 6.0f;      // the hero's centre to the monster's: the arm, the longest blade and a big monster's body (a cell is ~0.55 m)
    float tolCells = 2.0f;        // the lag's slack on top (the monster moved within a sample, the client's smoothing)
    float heroTolCells = 3.0f;    // how far the reported hero may be from his nearest server place in the window
};
constexpr float kReachCells = 6.0f, kTolCells = 2.0f, kHeroTolCells = 3.0f;

struct Facts {
    bool off = false;             // the host's box, its server game and Direct hit's hooks: not all there
    bool peerHero = true;         // the peer owns this hero
    bool heroTrack = false;       // the hero's history matched over the window (heroDist, heroAt)
    float heroDist = 0.0f;
    Sample heroAt;
    bool heroOk = true;           // his server mode now: not dead, dying, in a portal
    bool monsterTrack = false;    // the monster's place at the rewound time (monAt)
    Sample monAt;
    bool monsterNow = true;       // the monster's server unit is there now
    bool hostile = false;         // the server's Relation for the joined hero, now
    bool wall = true;             // the server's Line for the joined hero, now
};
inline float Dist(float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }
// The touch against what the host knows. The pace is Pace's (after this says Struck). *reach: the distance judged (cells; -1 not got that far).
inline Why Judge(const Swing& s, const Times& t, const Facts& f, const Limits& l, float* reach = nullptr) {
    if (reach) *reach = -1.0f;
    if (f.off) return Why::Off;
    if (!f.peerHero) return Why::PeerHero;
    if (t.tooOld) return Why::TooOld;
    if (!f.heroTrack) return Why::NoHero;
    if (!f.heroOk) return Why::HeroMode;
    if (f.heroDist > l.heroTolCells) return Why::HeroMoved;
    if (!f.monsterTrack || !f.monsterNow) return Why::NoMonster;
    if (!f.monAt.alive) return Why::MonsterDead;
    if (!f.hostile) return Why::NotHostile;
    if (f.wall) return Why::Wall;
    const float d = Dist(f.heroAt.x, f.heroAt.y, f.monAt.x, f.monAt.y);
    if (reach) *reach = d;
    const float most = l.reachCells + l.tolCells;
    if (d > most) return Why::Far;
    if (s.monX >= 0.0f && s.monY >= 0.0f && Dist(s.heroX, s.heroY, s.monX, s.monY) > most) return Why::Far;   // (his own picture, too)
    return Why::Struck;
}

// ---- the pace, per joined hero: never faster than his attack as the host's server plays it ----
constexpr uint64_t kDefaultAttackMs = 600;   // until his attack is measured on the host (melee_direct_client.h AttackClock on his server unit)
constexpr float kPaceSlack = 0.85f;          // the swings' times are guesses (the trip, the frame): this much of the attack time is enough
constexpr int kMaxExtras = 4;                // a cleave's further monsters in one attack
class Pace {
 public:
    // `swingAt` the swing on the host's clock (Times::swingAt), `attackMs` the hero's attack time, `cleave` the further monster may be one
    // (the host's cleave box, a two-handed weapon in both hands, the client's rules said extra).
    Why Check(uint64_t swingAt, uint32_t monster, bool cleave, uint64_t attackMs) const {
        const bool within = attackAt_ && swingAt >= attackAt_ && swingAt - attackAt_ < attackMs;
        if (within) {
            for (int i = 0; i < n_; ++i) if (struck_[i] == monster) return Why::Again;
            if (cleave && extras_ < kMaxExtras) return Why::Struck;   // a further monster of this attack
        }
        if (attackAt_ && swingAt >= attackAt_ && static_cast<float>(swingAt - attackAt_) < static_cast<float>(attackMs) * kPaceSlack) return Why::TooFast;
        return Why::Struck;
    }
    // A strike went out: a further monster of the attack under way, or a new attack.
    void Note(uint64_t swingAt, uint32_t monster, bool cleave, uint64_t attackMs) {
        const bool within = attackAt_ && swingAt >= attackAt_ && swingAt - attackAt_ < attackMs;
        if (within && cleave && extras_ < kMaxExtras) {
            ++extras_;
        } else {
            attackAt_ = swingAt ? swingAt : 1;
            n_ = 0;
            extras_ = 0;
        }
        if (n_ < kMaxExtras + 1) struck_[n_++] = monster;
    }
    void Clear() { attackAt_ = 0; n_ = 0; extras_ = 0; }
    uint64_t AttackAt() const { return attackAt_; }
 private:
    uint64_t attackAt_ = 0;
    uint32_t struck_[kMaxExtras + 1] = {};
    int n_ = 0, extras_ = 0;
};

// ---- the host's peers: each peer's hero (bound at its first hello / pong / touch; never the host's own), its round trip and pace ----
struct Peer {
    uint64_t peer = 0;
    uint32_t hero = 0;
    Rtt rtt;
    Pace pace;
    uint64_t pingAt = 0;      // the last ping sent (the host's ms)
    uint64_t seenAt = 0;      // the last message from it
};
class Peers {
 public:
    static constexpr int kSlots = 8;
    // The peer's slot for `hero`: bound now if new; nullptr when the peer named another hero than the one bound, another peer's, or the
    // host's own (`ownHero`).
    Peer* Bind(uint64_t peer, uint32_t hero, uint32_t ownHero) {
        if (!peer || !hero || hero == ownHero) return nullptr;
        Peer* free = nullptr;
        for (Peer& p : p_) {
            if (p.peer == peer) return p.hero == hero ? &p : nullptr;
            if (p.hero == hero) return nullptr;
            if (!p.peer && !free) free = &p;
        }
        if (!free) return nullptr;
        *free = Peer{};
        free->peer = peer;
        free->hero = hero;
        return free;
    }
    Peer* Of(uint64_t peer) { for (Peer& p : p_) if (p.peer == peer && peer) return &p; return nullptr; }
    void Forget(uint64_t peer) { for (Peer& p : p_) if (p.peer == peer) p = Peer{}; }
    void Clear() { for (Peer& p : p_) p = Peer{}; }
    int Count() const { int n = 0; for (const Peer& p : p_) n += p.peer ? 1 : 0; return n; }
    Peer& operator[](int i) { return p_[i]; }
 private:
    Peer p_[kSlots];
};

// ---- the client: the touches sent and waiting for the host's word ----
constexpr uint64_t kAnswerMs = 600;     // a verdict later than this finds nothing (the normal attack is not pressed late)
class Pending {
 public:
    static constexpr int kSlots = 16;
    struct Item { uint16_t seq = 0; uint32_t monster = 0; uint8_t hand = 0; uint64_t at = 0; bool used = false; };
    void Add(uint16_t seq, uint32_t monster, uint8_t hand, uint64_t at) {
        Item* oldest = &i_[0];
        for (Item& it : i_) {
            if (!it.used) { oldest = &it; break; }
            if (it.at < oldest->at) oldest = &it;
        }
        *oldest = {seq, monster, hand, at ? at : 1, true};
    }
    // The touch `seq` (taken out): false when unknown, answered already, or older than kAnswerMs.
    bool Take(uint16_t seq, uint64_t nowMs, Item* out) {
        for (Item& it : i_) {
            if (!it.used || it.seq != seq) continue;
            it.used = false;
            if (nowMs < it.at || nowMs - it.at > kAnswerMs) return false;
            *out = it;
            return true;
        }
        return false;
    }
    void Clear() { for (Item& it : i_) it = {}; }
 private:
    Item i_[kSlots];
};

}  // namespace coopstrike
