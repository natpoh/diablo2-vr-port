// parry.h - [weight] parry_*: the monster's blow met by the weapon or the shield in the hands (vr/parry_wire.cpp, melee_touch.cpp, shield_smite.cpp).
// Pure maths, no game calls (cleanroom/tests/parry_test.cpp). The author, 2026-10-24: «давай делать визуал 1 и 2 у паладина есть удар щитом его можно
// включать как парирование и блок у ассасина отдельная галочка удар по монстру по сути это тоже как будто парирование».
//
//   feel        (parry_feel) the held weapon meets a monster's weapon (its held capsule, vr/monster_rig.h) while the monster is in an attack: a clash -
//               a hard pulse in the hand, the weapon knocked aside along the monster's blow. Whether the monster hits is never changed: the game rolls it.
//   block shown (parry_block_show; the Assassin's claws: parry_claw_block) the game's own block - the hero's unit goes into its block mode - is felt as a
//               parry: the pulse, harder the nearer the weapon or the shield was to the attacking monster's weapon. Reading only.
//   shield      (parry_shield_smite) a Paladin's shield into the monster's weapon in its swing casts Smite at that monster - the shield bash's own press
//               (vr/shield_smite.cpp), only with Smite on a pad slot, and the game decides the rest (mana, reach, the cast).
//   counts hit  (parry_counts_hit, off by default) a blow on the monster's weapon in its swing is a blow on the monster: hit by touch presses the game's
//               attack as for its body, the game's own rolls decide. A shield stays no hit.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stroke_contact.h"

namespace parry {

// The game's unit modes (Diablo II's MonMode / PlrMode rows). A monster: 4 A1, 5 A2 (its attacks), 8 .. 11 S1 .. S4 (its skills - a melee monster's
// special blows among them), 14 SQ (a sequence: a skill's chain of blows). A hero: 9 BL, the block.
inline bool MonsterAttacking(int mode) { return mode == 4 || mode == 5 || (mode >= 8 && mode <= 11) || mode == 14; }
constexpr int kHeroBlock = 9;
inline bool HeroBlocking(int mode) { return mode == kHeroBlock; }

struct Capsule {
    stroke::Vec a, b;
    float r = 0.0f;
};
// The gap between two capsules' surfaces (metres; under 0 they overlap).
inline float Gap(const Capsule& p, const Capsule& q) {
    return std::sqrt(stroke::SegmentDistanceSquared(p.a, p.b, q.a, q.b)) - p.r - q.r;
}

// One clash per meeting: a monster's weapon and the hand's weapon (or the shield) come within kTouchM while the monster attacks - a new clash; then none
// for that monster until they have parted (beyond kPartM) and kRearmMs have gone. Up to kSlots monsters at once (the oldest is forgotten).
constexpr float kTouchM = 0.02f, kPartM = 0.06f;
constexpr uint64_t kRearmMs = 350;
class Clashes {
public:
    // `gap`: this frame's gap between the two (NaN or huge: apart). True: a new clash now.
    bool Step(uint32_t id, bool attacking, float gap, uint64_t nowMs) {
        Slot* s = Find(id, nowMs);
        const bool isNear = gap <= kTouchM;
        if (!isNear) {
            if (!(gap <= kPartM)) s->touching = false;
            return false;
        }
        if (s->touching || !attacking || (s->at && nowMs - s->at < kRearmMs)) return false;
        s->touching = true;
        s->at = nowMs;
        return true;
    }
    void Reset() { for (Slot& s : s_) s = Slot{}; }

private:
    struct Slot {
        uint32_t id = 0;
        bool touching = false;
        uint64_t at = 0, seen = 0;
    };
    static constexpr int kSlots = 16;
    Slot s_[kSlots];
    Slot* Find(uint32_t id, uint64_t nowMs) {
        Slot* oldest = &s_[0];
        for (Slot& s : s_) {
            if (s.id == id) { s.seen = nowMs; return &s; }
            if (s.seen < oldest->seen) oldest = &s;
        }
        *oldest = Slot{};
        oldest->id = id;
        oldest->seen = nowMs;
        return oldest;
    }
};

// The pulse of a parry, by its strength 0 .. 1: always a hard one (the PS VR2's motor shows nothing under about 0.5 - the caller lifts it with
// haptics::ForMotor too).
struct Feel {
    float amplitude = 0.0f;
    uint32_t ms = 0;
};
inline Feel ClashFeel(float strength) {
    const float s = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
    return {0.65f + 0.35f * s, static_cast<uint32_t>(45.0f + 35.0f * s)};
}

// The game's block shown as a parry: how hard, by how near the weapon or the shield was to the attacking monster's weapon (gapM; NaN = no attacking
// weapon known) - in its path (touching) 1, kFarM away or more (or none known) kFloor.
constexpr float kFloor = 0.35f, kFarM = 0.4f;
inline float BlockStrength(float gapM) {
    if (!std::isfinite(gapM) || gapM >= kFarM) return kFloor;
    if (gapM <= 0.0f) return 1.0f;
    return std::max(kFloor, 1.0f - gapM / kFarM * (1.0f - kFloor));
}

// The knock of a clash: the weapon in the hands pushed aside along the monster's blow - its weapon's velocity there (render metres a second). How far, by
// the blow's speed: kKnockMinM at a slow one, kKnockMaxM from 4 m/s; none when the monster's weapon hardly moves (under kKnockSpeed).
constexpr float kKnockSpeed = 0.5f, kKnockMinM = 0.03f, kKnockMaxM = 0.09f;
constexpr int kKnockMs = 160;
struct Knock {
    bool ok = false;
    stroke::Vec dir;   // unit
    float metres = 0.0f;
};
inline Knock KnockOf(stroke::Vec vel) {
    Knock k;
    const float sp = stroke::Length(vel);
    if (!(sp >= kKnockSpeed) || !std::isfinite(sp)) return k;
    k.ok = true;
    k.dir = vel * (1.0f / sp);
    k.metres = kKnockMinM + (kKnockMaxM - kKnockMinM) * std::clamp((sp - kKnockSpeed) / (4.0f - kKnockSpeed), 0.0f, 1.0f);
    return k;
}
// The knock's offset at `ms` since it began: up at once, eased back to nothing over kKnockMs.
inline float KnockShare(int ms) {
    if (ms < 0 || ms >= kKnockMs) return 0.0f;
    const float u = static_cast<float>(ms) / static_cast<float>(kKnockMs);
    return (1.0f - u) * (1.0f - u);
}

// Each monster's weapon's velocity, from where its capsule's middle was at the snapshots before (render metres, ms). Up to kSlots monsters.
class WeaponMotion {
public:
    stroke::Vec Feed(uint32_t id, stroke::Vec centre, uint64_t nowMs) {
        Slot* s = Find(id);
        if (s->have && nowMs == s->at) return s->v;
        if (s->have && nowMs > s->at && nowMs - s->at <= 120) s->v = s->v * 0.4f + (centre - s->p) * (600.0f / static_cast<float>(nowMs - s->at));   // (0.6 of the new)
        else s->v = stroke::Vec{};
        s->p = centre;
        s->at = nowMs;
        s->have = true;
        return s->v;
    }

private:
    struct Slot {
        uint32_t id = 0;
        bool have = false;
        stroke::Vec p, v;
        uint64_t at = 0;
    };
    static constexpr int kSlots = 16;
    Slot s_[kSlots];
    Slot* Find(uint32_t id) {
        Slot* oldest = &s_[0];
        for (Slot& s : s_) {
            if (s.id == id) return &s;
            if (s.at < oldest->at) oldest = &s;
        }
        *oldest = Slot{};
        oldest->id = id;
        return oldest;
    }
};

// The game's block entered: true once each time the hero's mode goes into the block (and not again while it stays there).
class BlockWatch {
public:
    bool Step(int heroMode) {
        const bool now = HeroBlocking(heroMode);
        const bool began = now && !was_;
        was_ = now;
        return began;
    }

private:
    bool was_ = false;
};

}  // namespace parry
