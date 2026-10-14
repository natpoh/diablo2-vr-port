// throw_cast.h - which of the game's own commands a throw by gesture presses, and how it learns what the game answered. Pure (no game
// calls, no globals); unit test cleanroom/tests/throw_cast_test.cpp.
//
// What the game's own tables say (data/global/excel/skills.txt and itemtypes.txt of 3.3, read from the archive):
//   Attack (skill 0, the pad's A): itypea1 = weap, etypea1 = tpot, TargetableOnly = 1, SearchEnemyXY = 1.
//       - a throwing POTION (item type tpot, "Missile Potion") is EXCLUDED: the game answers that the hero cannot do it;
//       - for a throwing knife, axe or javelin (item type comb: a melee AND a thrown weapon) it is allowed, but it is a skill that needs an
//         enemy: it searches one at the point it is given, and finds none on an empty cell.
//   Throw (skill 2): itypea1 = thro (potions, and the combo weapons through their second parent), range = rng, NO TargetableOnly / SearchEnemyXY:
//       a throw at a LOCATION, any clear cell - how a potion is thrown at the ground, and a knife, axe or javelin with no monster in the way.
// So a throw that has to fly where the hand threw it, with or without a monster, is the game's Throw: the pad's own button of the skill slot
// that holds it (B X Y RB, the left trigger with them for the second row), pressed as the skill wheel presses a slot. Which slot is the
// player's (the wheel's snapshot of the controller's table); none holds it: the A attack as before, and the log says what to do.
#pragma once

#include <cstdint>

#include "wheel_types.h"

namespace throwcast {

inline constexpr int32_t kSkillThrow = 2;   // skills.txt "Throw"

// The pad slot that holds Throw: the first of B X Y RB, then the same four with the left trigger, then LT + A - the slots whose cast is a
// pad button of their own (wheel_types.h kSlotDefs). A slot's skill must be there now (the player has it). A snapshot older than maxAgeMs
// (or one that was never read) finds nothing.
struct Pick {
    int slot = -1;
    uint16_t button = 0;
    bool alt = false;       // with the left trigger held
    const char* label = "";
};
inline Pick FindSlot(const wheel::Snapshot& s, uint64_t now, uint64_t maxAgeMs = 3000) {
    Pick p;
    if (!s.ready || now < s.at || now - s.at > maxAgeMs) return p;
    static const int kOrder[] = {0, 1, 2, 3, 4, 5, 6, 7, 10};
    for (const int i : kOrder) {
        const wheel::Skill& k = s.slot[i];
        const wheel::SlotDef& d = wheel::kSlotDefs[i];
        if (k.id != kSkillThrow || !k.available || d.cast != wheel::Cast::Pad || d.button == 0) continue;
        p.slot = i;
        p.button = d.button;
        p.alt = d.alt;
        p.label = d.label;
        return p;
    }
    return p;
}

// The swap the hook of the slot->skill read makes ([hands] throw_auto_skill): the pad's A is bound to Attack (skill 0); while a throw is live and a
// thrown weapon / potion is in hand, the two reads that pick the cast's skill (the slot selection's and the hand-skill setter's) answer Throw instead,
// exactly as if the slot held Throw. Only a plain Attack binding is swapped (a slot the player gave another skill keeps it), only at those two reads.
inline int SwapSkill(int bound, bool atCastSite, bool enabled, bool thrownInHand, bool throwLive) {
    return bound == 0 && atCastSite && enabled && thrownInHand && throwLive ? kSkillThrow : bound;
}

// A pad cast of a slot, frame by frame: the left trigger alone first for the second row (the game reads it apart from the face button: the
// wheel's cast lead), then the button, held until minPressS after it began. Step(time) says what is down now; the caller pulses it into the
// game's pad poll every frame (directinput::PadPulse) and stops when `active` is false.
class Press {
public:
    static constexpr double kLeadS = 0.09, kMinPressS = 0.12;
    struct Out {
        bool active = false;
        uint16_t buttons = 0;
        uint8_t lt = 0;
    };
    void Start(double t, const Pick& p) {
        if (p.slot < 0) return;
        start_ = t;
        button_ = p.button;
        alt_ = p.alt;
        on_ = true;
    }
    void Stop() { on_ = false; }
    bool Active() const { return on_; }
    Out Step(double t) {
        Out o;
        if (!on_) return o;
        const double age = t - start_;
        const double lead = alt_ ? kLeadS : 0.0;
        if (age >= lead + kMinPressS) {
            on_ = false;
            return o;
        }
        o.active = true;
        if (alt_) o.lt = 255;
        if (age >= lead) o.buttons = button_;
        return o;
    }

private:
    bool on_ = false, alt_ = false;
    double start_ = 0.0;
    uint16_t button_ = 0;
};

// The hero's mode (the game's player modes) by number, for the log.
inline const char* ModeName(uint32_t m) {
    switch (m) {
        case 0: return "death";
        case 1: return "neutral";
        case 2: return "walk";
        case 3: return "run";
        case 4: return "hit";
        case 5: return "town neutral";
        case 6: return "town walk";
        case 7: return "attack 1";
        case 8: return "attack 2";
        case 9: return "block";
        case 10: return "cast";
        case 11: return "throw";
        case 12: return "kick";
        case 13: case 14: case 15: case 16: return "skill";
        case 17: return "dead";
        default: return "?";
    }
}
inline bool IsActionMode(uint32_t m) { return m == 7 || m == 8 || m == 10 || m == 11 || (m >= 13 && m <= 16); }

// What the game answered to a press: watched in the hero's mode after it. Arm(t, mode) at the press; Poll(t, mode) every frame with the
// hero's mode now (ok = false: it could not be read). One event: the hero went into an action mode (attack, cast, throw, skill) - the
// answer is "accepted", with the mode and the delay - or the window ran out with none ("refused or ignored").
class AnswerWatch {
public:
    static constexpr double kWindowS = 0.8;
    enum class Kind : uint8_t { None, Accepted, NoAnswer, Unreadable };
    struct Event {
        Kind kind = Kind::None;
        uint32_t mode = 0;      // Accepted: the mode the hero went into; NoAnswer: the last mode read
        double afterS = 0.0;    // seconds after the press
        uint32_t before = 0;    // the mode at the press
    };
    void Arm(double t, bool ok, uint32_t mode) {
        armed_ = true;
        at_ = t;
        before_ = mode;
        beforeAction_ = ok && IsActionMode(mode);
        sawIdle_ = false;
        last_ = mode;
        anyRead_ = ok;
    }
    bool Armed() const { return armed_; }
    Event Poll(double t, bool ok, uint32_t mode) {
        Event e;
        if (!armed_) return e;
        if (ok) {
            last_ = mode;
            anyRead_ = true;
            if (!IsActionMode(mode)) sawIdle_ = true;
        }
        const double after = t - at_;
        // (a hero still in the animation of an earlier throw is not an answer until he has been out of it)
        if (ok && IsActionMode(mode) && (!beforeAction_ || sawIdle_)) {
            armed_ = false;
            e = {Kind::Accepted, mode, after, before_};
            return e;
        }
        if (after >= kWindowS) {
            armed_ = false;
            e = {anyRead_ ? Kind::NoAnswer : Kind::Unreadable, last_, after, before_};
        }
        return e;
    }

private:
    bool armed_ = false, beforeAction_ = false, sawIdle_ = false, anyRead_ = false;
    double at_ = 0.0;
    uint32_t before_ = 0, last_ = 0;
};

}  // namespace throwcast
