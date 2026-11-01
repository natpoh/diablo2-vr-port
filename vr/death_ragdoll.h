// death_ragdoll.h - [weight] death_ragdoll: a humanoid monster the game kills falls as our ragdoll in place of the game's own death animation - as drawn
// (docs/notes/knock_down.md). Pure (cleanroom/tests/death_ragdoll_test.cpp).
//
// Serg, 2026-11-01: on a kill the monster falls as a ragdoll instead of the game's death animation; only the drawing. The game says a unit died by its mode
// (0 dying, 12 dead - the touch's scan lists them apart from the living: melee_touch.cpp ScanUnits). vr/knock_down.cpp hands the body to the knock-down
// library (vr/fall_lib.h) with the joints the game drew it in a tick before (its last pose alive - read the same way as for a knock-down) and the push of the
// blow that killed it: the hand's weapon's last strike on it if there was one a moment ago (the swing and the steel and arm behind it, as vr/knock_trigger.h
// judges them), else a small push away from the hero (a spell, an arrow, a hireling's blow). It falls, and once the library has it lying still its pose
// is kept as it lies (Corpses: the joints in the model's own frame) and drawn so for as long as the game keeps the corpse - it never gets up, never walks home,
// and never goes back to the game's own lying pose (the drawn corpse lies where it fell, a step or so from the game's: a jump back would show). A corpse out
// of the bodies' reach (6 m, the nearest 16 skeletons) is drawn as the game has it, eased over the pose's 300 ms; back in reach ours eases in again.
//
// Left to the game (LeftToGame): a unit whose MonStats row does not read as its own; a boss or a prime evil (their deaths are the game's scripted ones -
// Andariel, Duriel, Mephisto, Diablo, Baal, the quest bosses); one that bursts when it dies (MonStats deathDmg: the suicide minions - no body falls); a town
// NPC. Not a humanoid read alive a moment before (an animal, a demon; one that died out of sight and is a corpse already when first seen) is left too.
// A unit the game leaves no corpse for after all (shattered by cold, a summon that crumbles) goes from the hash, its skeleton with it: our drawing goes as the
// game's would. The unit's mode, its place, its loot and the corpse are the game's: nothing is sent, nothing of the game's is written.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "d2r_vr_fall.h"

namespace deathragdoll {

// The MonStats row's flags (row + 0x3C; vr/mob_bars.h has the same layout: enabled 25, killable 15). The row is the one the unit's own monster data points
// at (UnitRowFlags) - NOT the row the touch's getter (melee_touch.cpp RowNow, RVA 0x975B0) hands: that one holds the footprint and the pixel height, a
// MonStats2 row, and its 0x3C had bit 7 set for every monster (0.195.1, Serg's log: zombie1, fallen1, quillrat1 all "a prime evil", nothing fell).
constexpr uint32_t kRowBoss = 1u << 6, kRowPrimeEvil = 1u << 7, kRowNpc = 1u << 8, kRowInTown = 1u << 10, kRowDeathDmg = 1u << 20;
constexpr int kModeDying = 0, kModeDead = 12;
inline bool Dead(int mode) { return mode == kModeDying || mode == kModeDead; }

constexpr uint64_t kAliveFreshMs = 300;   // its last read alive no older than this: the pose it died in (a scan is 30 ms; one older died out of sight)
constexpr double kBlowS = 0.6;            // the hand's last strike on it no older than this killed it (the game's hit comes a few frames after the touch)
constexpr float kMinMs = 0.8f;            // the speed the killing push gives the body: at least this (it falls, never just sags) ...
constexpr float kMaxMs = 2.5f;            // ... at most this (it lands a step or so from where the game keeps its corpse, never across the room)
constexpr float kAwayMs = 1.0f;           // a death by no blow of the hand: pushed this fast away from the hero
constexpr float kLieS = 30.0f;            // how long the library is told it lies (the most it takes): it is taken off the library long before
constexpr uint64_t kGoneMs = 1000;        // a dead unit not in the scan this long: gone (the game took its corpse away, or it is out of the scan's reach)
constexpr uint64_t kEaseMs = 300;         // a kept corpse back in reach eases in over this (vr/melee_touch.cpp kPoseEaseMs eases it out)
constexpr float kSameRootM = 0.5f;        // its skeleton is the one it died in: the same instance, standing within this of where it stood (metres)

// A monster unit's MonStats flags, read the way the game's own plate code does (vr/mob_bars.h HidesPlate, confirmed in the image): unit + 0x00 its type (1),
// + 0x04 its class, + 0x10 its monster data; data + 0x00 its MonStats row; the row's id (u16 at + 0x00) must be the class, its flags at + 0x3C. False: a
// read failed or the chain is not as expected (the death stays the game's). read(address, value) fills a uint16_t / uint32_t / uintptr_t.
template <class Read>
bool UnitRowFlags(uintptr_t unit, Read read, uint32_t* flags) {
    constexpr uintptr_t kMinPointer = 0x10000;
    uint32_t type = 0, cls = 0, f = 0;
    uintptr_t data = 0, row = 0;
    uint16_t rowId = 0;
    if (!flags || unit < kMinPointer || !read(unit + 0x00, type) || type != 1u) return false;
    if (!read(unit + 0x04, cls) || cls > 0xFFFFu) return false;
    if (!read(unit + 0x10, data) || data < kMinPointer) return false;
    if (!read(data + 0x00, row) || row < kMinPointer) return false;
    if (!read(row + 0x00, rowId) || rowId != cls) return false;
    if (!read(row + 0x3C, f)) return false;
    *flags = f;
    return true;
}

// Why the game keeps this death as its own (the log's word), null: ours. rowOk: the row read and is the unit's own (its id the unit's class).
inline const char* LeftToGame(bool rowOk, uint32_t flags) {
    if (!rowOk) return "its MonStats row did not read";
    if (flags & kRowPrimeEvil) return "a prime evil (its death is the game's own)";
    if (flags & kRowBoss) return "a boss (its death is the game's own)";
    if (flags & kRowDeathDmg) return "it bursts when it dies";
    if (flags & (kRowNpc | kRowInTown)) return "a town NPC";
    return nullptr;
}

// A death the scan reports: do we draw it falling? aliveRead: the unit was read as a humanoid, alive, at aliveAgeMs ago (its joints known); hostile: the
// server called it so. why (may be null): the reason when not.
enum class Start { Ours, LeftToGame, NotHostile, NotSeenAlive };
inline Start Decide(bool rowOk, uint32_t flags, bool aliveRead, uint64_t aliveAgeMs, bool hostile, const char** why) {
    const char* w = nullptr;
    Start s = Start::Ours;
    if ((w = LeftToGame(rowOk, flags)) != nullptr) s = Start::LeftToGame;
    else if (!aliveRead || aliveAgeMs > kAliveFreshMs) { s = Start::NotSeenAlive; w = "not read alive as a humanoid a moment before"; }
    else if (!hostile) { s = Start::NotHostile; w = "not a hostile monster"; }
    if (why) *why = w;
    return s;
}

// The hand's weapon's strikes on the bodies (the weight model's pushes, vr/knock_down.cpp NotePush): the last of each, for the push that killed it.
struct Blow {
    uint32_t id = 0;
    double t = -1e9;
    float impulse[3] = {}, point[3] = {};   // N s, render metres: its way; where it met the body
    float swingMs = 0.0f, heftKg = 0.0f;    // the weapon's own speed into the body, the steel and the arm behind it
};
class Blows {
public:
    static constexpr int kMax = 16;
    void Note(uint32_t id, const float impulse[3], const float point[3], float swingMs, float heftKg, double t) {
        if (!id || !impulse || !point || !std::isfinite(impulse[0] + impulse[1] + impulse[2] + point[0] + point[1] + point[2] + t)) return;
        int slot = -1;
        for (int i = 0; i < kMax && slot < 0; ++i)
            if (b_[i].id == id) slot = i;
        if (slot < 0) {   // a new body: the slot struck longest ago
            slot = 0;
            for (int i = 1; i < kMax; ++i)
                if (b_[i].t < b_[slot].t) slot = i;
        }
        Blow& b = b_[slot];
        const bool strike = b.id != id || t - b.t > 0.12;   // (a strike's steps: its first carries the swing - later ones press on, slower)
        b.id = id;
        b.t = t;
        if (strike || swingMs * heftKg > b.swingMs * b.heftKg) {
            std::memcpy(b.impulse, impulse, sizeof b.impulse);
            std::memcpy(b.point, point, sizeof b.point);
            b.swingMs = std::isfinite(swingMs) ? std::max(0.0f, swingMs) : 0.0f;
            b.heftKg = std::isfinite(heftKg) ? std::max(0.0f, heftKg) : 0.0f;
        }
    }
    // The last strike on body id if it is no older than kBlowS at t, else null.
    const Blow* Last(uint32_t id, double t) const {
        for (const Blow& b : b_)
            if (id && b.id == id && t - b.t <= kBlowS && t >= b.t - 1e-6) return &b;
        return nullptr;
    }
    void Reset() { *this = Blows{}; }

private:
    Blow b_[kMax];
};

// The push the body falls by (render metres, N s): the killing blow's way and its heft x swing over the body's mass, held between kMinMs and kMaxMs, at the
// point it met the body; no blow: kAwayMs across the ground away from the hero (hero null or under it: straight back from its chest - the library takes
// a push of no way as backward), at its chest.
struct Push {
    float impulse[3] = {}, point[3] = {};
    float gaveMs = 0.0f;   // the speed it gives the body
    bool byBlow = false;
    float swingMs = 0.0f;
};
inline Push KillingPush(const Blow* blow, const float chest[3], float massKg, const float hero[3]) {
    Push p;
    const float kg = std::isfinite(massKg) ? std::clamp(massKg, 5.0f, 2000.0f) : 50.0f;
    float dir[3] = {};
    if (blow) {
        const float j = std::sqrt(blow->impulse[0] * blow->impulse[0] + blow->impulse[1] * blow->impulse[1] + blow->impulse[2] * blow->impulse[2]);
        if (j > 1e-6f) {
            for (int k = 0; k < 3; ++k) dir[k] = blow->impulse[k] / j;
            p.byBlow = true;
            p.swingMs = blow->swingMs;
            p.gaveMs = std::clamp(blow->heftKg * blow->swingMs / kg, kMinMs, kMaxMs);
            std::memcpy(p.point, blow->point, sizeof p.point);
        }
    }
    if (!p.byBlow) {
        p.gaveMs = kAwayMs;
        if (chest) std::memcpy(p.point, chest, sizeof p.point);
        if (chest && hero && std::isfinite(hero[0] + hero[2])) {
            const float dx = chest[0] - hero[0], dz = chest[2] - hero[2], l = std::sqrt(dx * dx + dz * dz);
            if (l > 1e-3f) { dir[0] = dx / l; dir[2] = dz / l; }
        }
    }
    for (int k = 0; k < 3; ++k) p.impulse[k] = dir[k] * p.gaveMs * kg;
    return p;
}

// What a death's body in the library does next, by its phase: wait while it falls (or a hand holds it); kept once it lies (the library has it still on the
// ground: LYING - or a step into the get-up it may have begun within one step); fell anew if it died on its feet (staggering, walking home, back).
enum class Next { Wait, Keep, Fell };
inline Next OnPhase(int phase) {
    switch (phase) {
        case D2RF_LYING:
        case D2RF_GETTING_UP: return Next::Keep;
        case D2RF_STAGGERING:
        case D2RF_WALKING:
        case D2RF_BACK: return Next::Fell;
        default: return Next::Wait;
    }
}

// How much of a kept corpse is ours at nowMs: all of it, but eased in over kEaseMs since it came back into reach (backMs; 0 never away).
inline float Weight(uint64_t nowMs, uint64_t backMs) {
    if (!backMs) return 1.0f;
    if (nowMs <= backMs) return 0.0f;
    const float w = static_cast<float>(nowMs - backMs) / static_cast<float>(kEaseMs);
    return std::clamp(w, 0.0f, 1.0f);
}

// The corpses kept as they lie: each a dead unit's skeleton (its instance and where it stood, world units), its bone count, and its joints in the model's own
// frame (as dollpose::Knock::at has them). Up to kMax; a new one when full takes the place of the one seen longest ago.
constexpr int kMaxCorpses = 24;
struct Corpse {
    uint32_t id = 0;
    uintptr_t self = 0;
    float root[3] = {};
    int bones = 0;
    float at[D2RF_JOINTS][3] = {};
    uint64_t seenMs = 0;     // last in the scan's dead
    uint64_t drawnMs = 0;    // last drawn (its skeleton read)
    uint64_t backMs = 0;     // came back into reach (eased in from here); 0 never away
    float offM = 0.0f;       // its pelvis drawn this far from the game's (the log)
};
class Corpses {
public:
    // Keeps (or replaces) id's pose. Returns the corpse that made room (its id), 0 none.
    uint32_t Keep(const Corpse& c) {
        for (Corpse& k : c_)
            if (k.id == c.id) { k = c; return 0; }
        for (Corpse& k : c_)
            if (!k.id) { k = c; ++n_; return 0; }
        Corpse* old = &c_[0];
        for (Corpse& k : c_)
            if (k.seenMs < old->seenMs) old = &k;
        const uint32_t was = old->id;
        *old = c;
        return was;
    }
    Corpse* Of(uint32_t id) {
        for (Corpse& k : c_)
            if (id && k.id == id) return &k;
        return nullptr;
    }
    // Every corpse not in the scan's dead for kGoneMs (dead[]: the ids in it now) dropped; the ids that are seen get seenMs. How many dropped.
    int Forget(const uint32_t* dead, int nDead, uint64_t nowMs) {
        int dropped = 0;
        for (Corpse& k : c_) {
            if (!k.id) continue;
            bool there = false;
            for (int i = 0; i < nDead && !there; ++i) there = dead[i] == k.id;
            if (there) k.seenMs = nowMs;
            else if (nowMs - k.seenMs > kGoneMs) { k = Corpse{}; --n_; ++dropped; }
        }
        return dropped;
    }
    int Count() const { return n_; }
    Corpse* At(int i) { return i >= 0 && i < kMaxCorpses && c_[i].id ? &c_[i] : nullptr; }
    void Reset() { *this = Corpses{}; }

private:
    Corpse c_[kMaxCorpses];
    int n_ = 0;
};

// The deaths already decided (taken or left to the game): each id once - until it is out of the scan's dead for kGoneMs (an id the game gives anew).
class Told {
public:
    static constexpr int kMax = 64;
    bool Has(uint32_t id) const {
        for (const E& e : e_)
            if (id && e.id == id) return true;
        return false;
    }
    void Add(uint32_t id, uint64_t nowMs) {
        E* slot = &e_[0];
        for (E& e : e_) {
            if (e.id == id) { e.seenMs = nowMs; return; }
            if (e.seenMs < slot->seenMs) slot = &e;
        }
        *slot = {id, nowMs};
    }
    void Seen(const uint32_t* dead, int nDead, uint64_t nowMs) {
        for (E& e : e_) {
            if (!e.id) continue;
            bool there = false;
            for (int i = 0; i < nDead && !there; ++i) there = dead[i] == e.id;
            if (there) e.seenMs = nowMs;
            else if (nowMs - e.seenMs > kGoneMs) e = E{};
        }
    }
    void Reset() { *this = Told{}; }

private:
    struct E { uint32_t id = 0; uint64_t seenMs = 0; };
    E e_[kMax];
};

// Is the skeleton read now (self, root world units, its bones; upm world units a metre) the one a body died in (wasSelf, wasRoot, wasBones)? The same
// instance with as many bones, standing where it stood - an instance the game freed and made again for another unit stands elsewhere.
inline bool SameSkeleton(uintptr_t wasSelf, const float wasRoot[3], int wasBones, uintptr_t self, const float root[3], int bones, float upm) {
    if (!self || self != wasSelf || bones != wasBones || !root || !wasRoot || !(upm > 1e-3f)) return false;
    const float dx = root[0] - wasRoot[0], dy = root[1] - wasRoot[1], dz = root[2] - wasRoot[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz) <= kSameRootM * upm;
}

// A kept corpse drawn at nowMs: one not drawn for kBackGapMs was out of reach (its pose went stale - the skinning eased it back to the game's), so it eases
// in from now.
constexpr uint64_t kBackGapMs = 150;   // (vr/melee_touch.cpp kPoseFreshMs)
inline void Drawn(Corpse& c, uint64_t nowMs) {
    if (c.drawnMs && nowMs - c.drawnMs > kBackGapMs) c.backMs = nowMs;
    c.drawnMs = nowMs;
}

// What hangs on a dead body's bones - a zombie's arm pieces, a weapon (0.195.3, Serg: the torso lay on the ground, an arm and a hand floated where they had
// stood). Such a part is no skinned mesh: it is its own entity hung on a bone, and its world matrix is the game's world-matrix function's (vr/knock_down.cpp
// OnXform carries it to the bone as drawn each time the game makes it). The game makes it only while the body animates: once its death animation is over it
// stops - our fall goes on, and the part stayed where the last carry put it. So each part met on a dead body is kept with its world matrix as the game last
// made it and its bone then (the model's frame); once a frame (the same hook) it is put on its bone as drawn now (knockpose::Carry), while its body is dead
// and drawn by us; when the body is the game's again (out of reach, gone, the key off) the game's matrix is put back once - the game will not.
struct Hang {
    uint32_t entity = 0;
    uintptr_t self = 0;          // the SkeletonInstance it hangs on
    int bone = -1;
    float game[16] = {};         // its world matrix as the game last made it
    float boneGame[16] = {};     // ... from its bone as the game had it then (the model's frame)
    bool ours = false;           // our matrix is in it now: the game's is owed back
};
enum class HangDo { Nothing, Carry, PutBack };
// selfDead: its body is a dead one drawn by us (falling or kept); poseNow: that pose is there to lay it on this frame.
inline HangDo HangNext(bool selfDead, bool poseNow, bool ours) {
    if (selfDead && poseNow) return HangDo::Carry;
    return ours ? HangDo::PutBack : HangDo::Nothing;
}
class Hangs {
public:
    static constexpr int kMax = 64;
    // The game made part `entity`'s world matrix (game) from bone `bone` of self (boneGame). A new one when full: the slot of a part not ours now, else none.
    void Note(uint32_t entity, uintptr_t self, int bone, const float game[16], const float boneGame[16]) {
        if (!entity || !self || bone < 0 || !game || !boneGame) return;
        Hang* slot = nullptr;
        for (Hang& h : h_)
            if (h.entity == entity) { slot = &h; break; }
        if (!slot)
            for (Hang& h : h_)
                if (!h.entity) { slot = &h; break; }
        if (!slot)
            for (Hang& h : h_)
                if (!h.ours) { slot = &h; break; }
        if (!slot) return;
        slot->entity = entity;
        slot->self = self;
        slot->bone = bone;
        std::memcpy(slot->game, game, sizeof slot->game);
        std::memcpy(slot->boneGame, boneGame, sizeof slot->boneGame);
        slot->ours = false;   // (the game just made it: its own matrix is in it)
    }
    Hang* At(int i) { return i >= 0 && i < kMax && h_[i].entity ? &h_[i] : nullptr; }
    void Drop(Hang& h) { h = Hang{}; }
    int Count() const {
        int n = 0;
        for (const Hang& h : h_) n += h.entity ? 1 : 0;
        return n;
    }

private:
    Hang h_[kMax];
};

}  // namespace deathragdoll
