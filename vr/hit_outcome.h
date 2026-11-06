#pragma once
// hit_outcome.h - [debug] hit_outcome: what came of the hero's blow on a monster, told in the log. Stage 1 of the plan for the labels and
// damage numbers over monsters (docs/notes/3d/ru/hit-outcome-labels.md, branch claude/3d-notes): only the log, nothing drawn, no rule touched.
//
// What the lines are for: to check on the players' build (3.3.94995) what was taken from D2MOO 1.10 and the d2r-pcvr fork - the roll's flag
// values (only 0x10 was ever seen in D2R), the damage function 0x44CE80, the crit bit 0x2000 in the result flags at +0x04 of its damage record,
// the life it takes (stat 6, 1/256) - and that Direct hit rolls to hit as the game's own attack does.
//
// Two observers, both read-only: vr/haptics.cpp's hook on the hit flags 0x44B720 (one per roll) and vr/hit_outcome_wire.cpp's on the damage
// 0x44CE80 (one per blow that lands). They run on the game's server thread and only push a record here: no lock, no log, no allocation. vrcam's
// timer drains the records into the log twice a second (vr/hit_outcome_wire.cpp). This header is plain logic: the ring, the names, the lines
// (cleanroom/tests/hit_outcome_test.cpp).
//
// [hud] swing_timer (vr/swing_timer.h) reads the same two observers for its numbers: a ring of its own (LabelRing), filled while it wants it,
// whether or not this log is on; each record carries the attacker's id, so it keeps our hero's blows only.

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace hitoutcome {

// The roll's answer (0x44B720), as D2MOO 1.10 names its bits; in D2R only 0x10 is confirmed (d2r-pcvr vr/combat.cpp).
constexpr uint16_t kHit = 0x0001, kBlockShield = 0x0010, kDodge = 0x0080, kAvoid = 0x0100, kBlockWeapon = 0x8000;
// The damage record's result flags (+0x04): the crit (Critical Strike, Deadly Strike, weapon mastery - D2MOO; d2r-pcvr reads the same word).
constexpr uint16_t kCrit = 0x2000;

enum class Kind : uint8_t { None = 0, Roll = 1, Damage = 2 };
enum class Outcome : uint8_t { Hit, Blocked, Dodged, Avoided, Zero, Other, Count };

inline Outcome OutcomeOf(uint16_t f) {
    if (f & kHit) return Outcome::Hit;
    if (f & (kBlockShield | kBlockWeapon)) return Outcome::Blocked;
    if (f & kDodge) return Outcome::Dodged;
    if (f & kAvoid) return Outcome::Avoided;
    if (f == 0) return Outcome::Zero;
    return Outcome::Other;
}

// What a label would say. 0 is not only a miss: out of reach, a bad unit and an evade come back as 0 too (D2MOO) - this log is to see how often.
inline const char* RollText(uint16_t f) {
    if (f & kHit) return "hit";
    if (f & kBlockShield) return "blocked (shield)";
    if (f & kBlockWeapon) return "blocked (weapon)";
    if (f & kDodge) return "dodged";
    if (f & kAvoid) return "avoided";
    if (f == 0) return "0: missed, or out of reach / evaded / not rolled";
    return "other bits";
}

struct Record {
    Kind kind = Kind::None;
    bool direct = false;       // inside Direct hit's own call of the attack (vr/melee_direct.cpp)
    uint16_t flags = 0;        // a roll: what 0x44B720 returned; damage: the record's result flags (+0x04)
    uint32_t hitFlags = 0;     // damage: the record's first dword (+0x00)
    uint32_t monClass = 0, monId = 0;
    uint32_t attacker = 0;     // the attacking hero's unit id (the swing timer keeps its own hero's)
    int32_t lifeBefore = -1, lifeAfter = -1;   // damage: the monster's stat 6 (1/256) before and after the game's call; -1 = not read
    int32_t recordLife = -1;   // damage: the record's life damage (+0x134, d2r-pcvr), read after the call; -1 = not read
    int32_t path = -1;         // damage: the call's flag (0 melee, else a missile or a spell - d2r-pcvr)
    uint64_t ms = 0;
};

// Many writers (the server's thread; more in theory), one reader (vrcam's timer). A seqlock per slot over atomic words: a slot overwritten
// while it was read is counted as lost, never told half old and half new.
class Ring {
public:
    static constexpr uint32_t kSize = 256;

    void Push(const Record& r) {
        uint64_t w[kWords];
        Pack(r, w);
        const uint64_t n = head_.fetch_add(1, std::memory_order_relaxed);
        Slot& s = slot_[n % kSize];
        s.seq.store(2 * n + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < kWords; ++i) s.w[i].store(w[i], std::memory_order_relaxed);
        s.seq.store(2 * n + 2, std::memory_order_release);
    }

    // The reader's. true = *out is the next record; false = nothing more yet. *lost grows by the records overwritten before they were read.
    bool Pop(Record* out, uint32_t* lost) {
        for (;;) {
            const uint64_t head = head_.load(std::memory_order_acquire);
            if (tail_ >= head) return false;
            if (head - tail_ > kSize) {
                *lost += (uint32_t)(head - tail_ - kSize);
                tail_ = head - kSize;
            }
            Slot& s = slot_[tail_ % kSize];
            const uint64_t s1 = s.seq.load(std::memory_order_acquire);
            if (s1 < 2 * tail_ + 2) return false;   // taken by a writer that has not finished: next time
            uint64_t w[kWords];
            for (int i = 0; i < kWords; ++i) w[i] = s.w[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t s2 = s.seq.load(std::memory_order_relaxed);
            ++tail_;
            if (s1 != 2 * (tail_ - 1) + 2 || s2 != s1) { ++*lost; continue; }   // a later lap wrote it: gone
            Unpack(w, out);
            return true;
        }
    }

private:
    static constexpr int kWords = 6;
    struct Slot {
        std::atomic<uint64_t> seq{0};
        std::atomic<uint64_t> w[kWords] = {};
    };
    static void Pack(const Record& r, uint64_t* w) {
        w[0] = r.ms;
        w[1] = (uint64_t)r.kind | (uint64_t)(r.direct ? 1 : 0) << 8 | (uint64_t)r.flags << 16 | (uint64_t)r.hitFlags << 32;
        w[2] = (uint64_t)r.monClass | (uint64_t)r.monId << 32;
        w[3] = (uint64_t)(uint32_t)r.lifeBefore | (uint64_t)(uint32_t)r.lifeAfter << 32;
        w[4] = (uint64_t)(uint32_t)r.recordLife | (uint64_t)(uint32_t)r.path << 32;
        w[5] = (uint64_t)r.attacker;
    }
    static void Unpack(const uint64_t* w, Record* r) {
        r->ms = w[0];
        r->kind = (Kind)(w[1] & 0xFF);
        r->direct = ((w[1] >> 8) & 1) != 0;
        r->flags = (uint16_t)(w[1] >> 16);
        r->hitFlags = (uint32_t)(w[1] >> 32);
        r->monClass = (uint32_t)w[2];
        r->monId = (uint32_t)(w[2] >> 32);
        r->lifeBefore = (int32_t)(uint32_t)w[3];
        r->lifeAfter = (int32_t)(uint32_t)(w[3] >> 32);
        r->recordLife = (int32_t)(uint32_t)w[4];
        r->path = (int32_t)(uint32_t)(w[4] >> 32);
        r->attacker = (uint32_t)w[5];
    }
    Slot slot_[kSize];
    std::atomic<uint64_t> head_{0};
    uint64_t tail_ = 0;   // (the reader's)
};

// The counts for the 10 s line (the reader's).
struct Tally {
    uint32_t rolls[(int)Outcome::Count] = {};
    uint32_t directRolls = 0, damage = 0, crits = 0, kills = 0, notTold = 0, lost = 0;

    void Add(const Record& r) {
        if (r.kind == Kind::Roll) {
            ++rolls[(int)OutcomeOf(r.flags)];
            if (r.direct) ++directRolls;
        } else if (r.kind == Kind::Damage) {
            ++damage;
            if (r.flags & kCrit) ++crits;
            if (Killed(r)) ++kills;
        }
    }
    bool Any() const {
        uint32_t n = damage + notTold + lost;
        for (uint32_t c : rolls) n += c;
        return n != 0;
    }
    static bool Killed(const Record& r) { return r.lifeBefore > 0 && r.lifeAfter == 0; }
};

// "monster class 12 id 345: ..." - one line per record, without the "vrcam: hit outcome - " the wire puts in front.
inline int Line(const Record& r, char* out, size_t n) {
    if (!out || !n) return 0;
    const char* direct = r.direct ? ", in Direct hit's call" : "";
    if (r.kind == Kind::Roll)
        return snprintf(out, n, "roll: monster class %u id %u: flags 0x%04X = %s%s", r.monClass, r.monId, r.flags, RollText(r.flags), direct);
    if (r.kind != Kind::Damage) return snprintf(out, n, "?");
    char life[96];
    if (r.lifeBefore >= 0 && r.lifeAfter >= 0)
        snprintf(life, sizeof life, "life %.1f -> %.1f (-%.1f%s)", r.lifeBefore / 256.0, r.lifeAfter / 256.0, (r.lifeBefore - r.lifeAfter) / 256.0,
                 Tally::Killed(r) ? ", killed: what was left, not the whole blow" : "");
    else
        snprintf(life, sizeof life, "life not read");
    char rec[40];
    if (r.recordLife >= 0) snprintf(rec, sizeof rec, "%.1f", r.recordLife / 256.0);
    else snprintf(rec, sizeof rec, "not read");
    char path[48];
    if (r.path == 0) snprintf(path, sizeof path, "melee (flag 0)");
    else snprintf(path, sizeof path, "missile or spell (flag %d)", r.path);
    return snprintf(out, n, "damage: monster class %u id %u: %s, record +0x134 %s, result flags 0x%04X%s, hit flags 0x%08X, %s%s", r.monClass, r.monId, life,
                    rec, r.flags, (r.flags & kCrit) ? " CRIT" : "", r.hitFlags, path, direct);
}

// "rolls 12 (hit 7, blocked 1, ...)" for the 10 s line.
inline int TallyLine(const Tally& t, char* out, size_t n) {
    if (!out || !n) return 0;
    uint32_t rolls = 0;
    for (uint32_t c : t.rolls) rolls += c;
    return snprintf(out, n,
                    "rolls %u (hit %u, blocked %u, dodged %u, avoided %u, 0 %u, other %u; %u of them in Direct hit's call), damage %u (crit %u, killed %u), "
                    "not told %u (the cap of lines), lost %u (the ring was full)",
                    rolls, t.rolls[(int)Outcome::Hit], t.rolls[(int)Outcome::Blocked], t.rolls[(int)Outcome::Dodged], t.rolls[(int)Outcome::Avoided],
                    t.rolls[(int)Outcome::Zero], t.rolls[(int)Outcome::Other], t.directRolls, t.damage, t.crits, t.kills, t.notTold, t.lost);
}

// ---- the shared state (inline: one copy in the DLL) ----
// [debug] hit_outcome: the log.
inline std::atomic<bool>& WantedFlag() {
    static std::atomic<bool> on{false};
    return on;
}
inline bool LogWanted() { return WantedFlag().load(std::memory_order_relaxed); }
// [hud] swing_timer's numbers (vr/swing_timer_wire.cpp sets it).
inline std::atomic<bool>& LabelsFlag() {
    static std::atomic<bool> on{false};
    return on;
}
inline bool LabelsWanted() { return LabelsFlag().load(std::memory_order_relaxed); }
// Either: the observers go in and push.
inline bool Wanted() { return LogWanted() || LabelsWanted(); }
inline Ring& Global() {
    static Ring r;
    return r;
}
inline Ring& LabelRing() {
    static Ring r;
    return r;
}
// Set by vr/melee_direct.cpp around its one call of the server's attack, on that thread: the roll and the damage inside it are Direct hit's.
inline thread_local bool t_direct = false;

// The observers' (server thread). The unit's header has been read by the caller.
inline void PushRoll(uint32_t monClass, uint32_t monId, uint16_t flags, uint64_t ms, uint32_t attacker = 0) {
    Record r;
    r.kind = Kind::Roll;
    r.direct = t_direct;
    r.flags = flags;
    r.monClass = monClass;
    r.monId = monId;
    r.attacker = attacker;
    r.ms = ms;
    if (LogWanted()) Global().Push(r);
    if (LabelsWanted()) LabelRing().Push(r);
}
inline void PushDamage(Record r) {
    r.kind = Kind::Damage;
    r.direct = t_direct;
    if (LogWanted()) Global().Push(r);
    if (LabelsWanted()) LabelRing().Push(r);
}

}  // namespace hitoutcome
