// double_swing.h - [hands] double_swing_by_hands: with a weapon in each hand, the SECOND hand's touch on the monster the first hand just struck casts the game's own Double Swing (or Frenzy). Pure: no game calls, no
// globals, no windows.h (cleanroom/tests/double_swing_test.cpp). vr/double_swing_game.cpp is the game-facing half (the pad slots, the skills' levels, the hero's mana, the log).
//
// The player's own words (2026-10-18): "a tick 'double swing', on when the skill is there and the target was struck by both hands within a period. The player runs up to a monster, attacks with one hand -
// the game counts it as an ordinary blow; then with the other - and the game checks for the double swing: with mana and the skill, the second blow goes as a double". So:
//   - the first hand's touch on a monster is the game's normal attack, as ever;
//   - the OTHER hand's touch on the SAME monster within a window (a slider, 0.7 s) is the dual skill instead of a normal attack - when it sits on a pad slot, the hero has a level in it and can pay its mana;
//   - otherwise the second touch is a normal attack too. A pair is spent by the dual skill: the next touch starts a new pair.
// The press is the game's own pad button of the slot the skill sits in (as the shield bash's Smite does); the game does the swings, the damage, the animation, the mana. Nothing is called that attacks. With
// the game's own rule on top: the dual skills carry the Skills.txt flag AttackNoMana, so a press the hero cannot pay is the game's plain attack anyway.
//
// What the two skills are (Skills.txt of D2R 3.3, extracted from the game's own archive 2026-10-18): Double Swing 133 and Frenzy 147, both h2h, both weapon-select 3 (a weapon in each hand: itypea1 / itypeb1
// "mele"), both AttackNoMana. Double Throw (140, two thrown weapons) does not fit: the touch swings ONE thrown weapon (the blade of the second hand is not made) and a throw is not a blow in reach.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "bow_mana.h"   // bowmana::Estimate: the hero's mana at the press (the game takes a cast's mana a moment after the press)

namespace doubleswing {

constexpr int kDoubleSwing = 133, kFrenzy = 147;
constexpr int kSkillCount = 2;   // 0 Double Swing, 1 Frenzy
constexpr int32_t kSkillId[kSkillCount] = {kDoubleSwing, kFrenzy};

// ---- what a cast costs -----------------------------------------------------------------------------------------------------------
// Skills.txt: mana, lvlmana, manashift, minmana. The game's own arithmetic (bow_mana.h's, the stats being fixed point, 1/256 of a point):
//     cost = (mana + lvlmana * (level - 1)) << manashift   (at least 0), and at least minmana points
// Double Swing: (8 - (level - 1)) << 5 - 1.0 at level 1, 0.125 at level 8, none from level 9; Frenzy: 3 << 7 - 1.5 at every level.
struct Row {
    int32_t mana, lvlMana, shift, minMana;
};
inline constexpr Row kRows[kSkillCount] = {
    {8, -1, 5, 0},   // Double Swing
    {3, 0, 7, 1},    // Frenzy
};
inline const char* SkillName(int index) { return index == 0 ? "Double Swing" : index == 1 ? "Frenzy" : "?"; }

// The cost of skill `index` at `level` in the game's fixed point; -1 for an index that is not one of the two and for a level below 1 (not known).
inline int64_t CostRaw(int index, int level) {
    if (index < 0 || index >= kSkillCount || level < 1) return -1;
    const Row& r = kRows[index];
    int64_t v = (int64_t)r.mana + (int64_t)r.lvlMana * (int64_t)(level - 1);
    if (v < 0) v = 0;
    const int64_t cost = v << r.shift, least = (int64_t)r.minMana << 8;
    return cost < least ? least : cost;
}
inline float CostPoints(int index, int level) {
    const int64_t c = CostRaw(index, level);
    return c < 0 ? -1.0f : (float)c / 256.0f;
}
// Whether the hero cannot pay it with `manaPoints` (negative or not a number: not known - then the game decides alone, and false is said).
inline bool Short(int index, int level, float manaPoints) {
    const int64_t cost = CostRaw(index, level);
    if (cost < 0 || !std::isfinite(manaPoints) || manaPoints < 0.0f) return false;
    return (int64_t)std::floor((double)manaPoints * 256.0 + 0.5) < cost;
}

// ---- the window ------------------------------------------------------------------------------------------------------------------
constexpr double kMinWindowMs = 200.0, kMaxWindowMs = 2000.0, kDefaultWindowMs = 700.0;
inline double ClampWindowMs(double ms) { return std::isfinite(ms) ? std::clamp(ms, kMinWindowMs, kMaxWindowMs) : kDefaultWindowMs; }

// ---- the skills as the game's tables say -----------------------------------------------------------------------------------------
struct SkillState {
    int slot = -1;    // the pad slot (0..9, shield_smite.h's order) that holds it, -1 none of the ten
    int level = -1;   // the hero's total level in it: -1 not read, 0 not learned, 1.. the level
};
struct Facts {
    SkillState skill[kSkillCount];
    int attackSkill = -1;   // the skill on the pad's A (the attack button), -1 not read
    float mana = -1.0f;     // the hero's mana now in points (less what our last cast took, if that is not in the read yet); < 0 not read
    int prefer = 0;         // 0 Double Swing first (Frenzy when Double Swing is not on a slot), 1 Frenzy first
};

// ---- the verdict -----------------------------------------------------------------------------------------------------------------
enum class Why : uint8_t {
    Off,               // [hands] double_swing_by_hands is off
    NotDual,           // not a melee weapon in each hand
    First,             // the first hand of a pair: a normal attack; the other hand has the window
    OtherMonster,      // the other hand's last strike was on another monster: this one is a first
    TooLate,           // the other hand's last strike is older than the window: this one is a first
    Dual,              // the second hand, in the window, on the same monster: the dual skill is pressed
    AlreadyOnAttack,   // the attack button already carries Double Swing / Frenzy: every touch casts it already
    NoSlot,            // neither skill sits on the pad's ten slots
    NotLearned,        // a skill sits on a slot but the hero has no level in it
    NoMana,            // the hero cannot pay it
};
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::Off: return "off";
        case Why::NotDual: return "not a weapon in each hand";
        case Why::First: return "the first hand of a pair";
        case Why::OtherMonster: return "the other hand struck another monster";
        case Why::TooLate: return "the other hand's strike was too long ago";
        case Why::Dual: return "the second hand within the window on the same monster";
        case Why::AlreadyOnAttack: return "the attack button already carries it";
        case Why::NoSlot: return "neither skill is on a pad slot";
        case Why::NotLearned: return "the hero has no level in it";
        case Why::NoMana: return "not enough mana";
    }
    return "?";
}

struct Verdict {
    bool dual = false;       // the touch is the dual skill (the pad slot is pressed) instead of the normal attack
    Why why = Why::Off;
    int skill = -1;          // the skill's index (Dual: the one pressed; AlreadyOnAttack / NoMana / NotLearned: the one looked at)
    int slot = -1;           // Dual: its pad slot
    int level = 0;           // Dual / NoMana: the level the cost was worked out at
    bool levelGuessed = false;   // the level could not be read: the first level's cost (the dearest) stood in
    double gapMs = -1.0;     // since the other hand's last strike, when there was one
    float cost = -1.0f, mana = -1.0f;
};

// The skill of a second strike: the one that sits on a pad slot, has a level and can be paid, in the player's order; else why not.
inline void Choose(const Facts& f, Verdict* v) {
    if (f.attackSkill == kDoubleSwing || f.attackSkill == kFrenzy) {
        v->why = Why::AlreadyOnAttack;
        v->skill = f.attackSkill == kDoubleSwing ? 0 : 1;
        return;
    }
    const int order[kSkillCount] = {f.prefer == 1 ? 1 : 0, f.prefer == 1 ? 0 : 1};
    bool anySlot = false, anyLearned = false, anyShort = false;
    Verdict shortOne;
    for (const int i : order) {
        const SkillState& s = f.skill[i];
        if (s.slot < 0) continue;
        anySlot = true;
        if (s.level == 0) continue;
        anyLearned = true;
        const int level = s.level > 0 ? s.level : 1;   // not read: the first level's cost, the dearest (the game decides anyway)
        if (f.mana >= 0.0f && Short(i, level, f.mana)) {
            if (!anyShort) {
                anyShort = true;
                shortOne.skill = i;
                shortOne.level = level;
                shortOne.levelGuessed = s.level < 0;
                shortOne.cost = CostPoints(i, level);
                shortOne.slot = s.slot;
            }
            continue;
        }
        v->dual = true;
        v->why = Why::Dual;
        v->skill = i;
        v->slot = s.slot;
        v->level = level;
        v->levelGuessed = s.level < 0;
        v->cost = CostPoints(i, level);
        v->mana = f.mana;
        return;
    }
    v->mana = f.mana;
    if (anyShort) {
        v->why = Why::NoMana;
        v->skill = shortOne.skill;
        v->slot = shortOne.slot;
        v->level = shortOne.level;
        v->levelGuessed = shortOne.levelGuessed;
        v->cost = shortOne.cost;
    } else {
        v->why = !anySlot ? Why::NoSlot : Why::NotLearned;
    }
}

// ---- the pairs ----------------------------------------------------------------------------------------------------------------------
// Each hand remembers its last accepted strike (the monster, when). A strike of a hand is the SECOND of a pair when the other hand's last strike was on the same monster no longer ago than the window.
// A pair the dual skill took is spent (both memories are cleared); a second strike that could not be a dual skill is the first of a new pair for its own hand. The timer's thread only.
class Tracker {
public:
    void Reset() { last_[0] = last_[1] = Mem{}; }

    // A strike noted without a verdict: the first of two blades that touched one monster in the same pass - the second's Strike then sees it at gap 0.
    void Record(int hand, uint32_t monster, uint64_t nowMs) { last_[hand & 1] = Mem{monster, nowMs, true}; }

    // An accepted strike of `hand` (0 right, 1 left) on `monster` at `nowMs`. dualWield: a melee weapon in each hand. windowMs: [hands] double_swing_window_ms. facts: the skills and the mana now.
    Verdict Strike(int hand, uint32_t monster, uint64_t nowMs, bool dualWield, double windowMs, const Facts& facts) {
        Verdict v;
        hand &= 1;
        if (!dualWield) {
            v.why = Why::NotDual;
            Reset();
            return v;
        }
        const Mem& other = last_[hand ^ 1];
        bool pair = false;
        if (!other.valid) {
            v.why = Why::First;
        } else {
            v.gapMs = nowMs >= other.at ? (double)(nowMs - other.at) : 0.0;   // (a clock that went back: no time passed)
            if (other.monster != monster) v.why = Why::OtherMonster;
            else if (v.gapMs > ClampWindowMs(windowMs)) v.why = Why::TooLate;
            else pair = true;
        }
        if (pair) {
            Choose(facts, &v);
            if (v.dual) {
                Reset();   // the pair is spent
                return v;
            }
        }
        Record(hand, monster, nowMs);   // a normal attack: the first of a (new) pair for this hand
        return v;
    }

private:
    struct Mem {
        uint32_t monster = 0;
        uint64_t at = 0;
        bool valid = false;
    };
    Mem last_[2];
};

}  // namespace doubleswing
