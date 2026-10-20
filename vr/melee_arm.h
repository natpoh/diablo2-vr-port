// melee_arm.h - [gameplay] melee_skill_armed: a melee attack skill on a pad skill button is ARMED by its press, not cast, and every hit by touch then is the game's attack with that skill, paying
// its mana; with too little mana the hit is the plain attack. Pure: no game calls, no globals, no windows.h (cleanroom/tests/melee_arm_test.cpp). vr/melee_arm_game.cpp is the game-facing half.
//
// The author's words (2026-10-19): "for the Barbarian's and the Paladin's attack skills (and whoever else has them), when we bind a skill to a button, pressing it does not activate it but remembers it
// as the chosen one, like the Amazon's, and on every blow it fires and spends mana; if there is not enough mana, a normal blow." So, in VR first person (F4) with hit by touch swinging a melee weapon:
//   - a press of a pad skill button (B X Y RB, with and without LT) whose slot holds a melee attack skill is not passed to the game: that skill becomes the ARMED one (Press). The same button again, or a
//     button whose slot holds the plain attack (Attack, 0), disarms; another melee skill's button re-arms with that one. Any other skill's press (a spell, a buff, a warcry) is the game's, as ever.
//   - every touch the game's attack is pressed for then reads the attack slot's skill as the armed one (vrcam's slot-skill hook, the bow's [bow] quiver_plain_no_mana path the other way round): the game's
//     whole cast is the skill's - its checks, weapon, mana, cooldown, animation, the touched monster as its target. One touch, one press, one cast: never more hits than swings.
//   - a touch the hero cannot pay (Decide: the game's own arithmetic on the skill's Skills.txt row, the skill's level and the hero's mana, bow_mana.h) is the plain attack, the skill staying armed.
//
// Which skills (kRows): derived from the game's own Skills.txt (D2R 3.3, the rows the game ships; the bow rows of bow_mana.h and the two of double_swing.h read back identically from the same extract):
// a class's skill (charclass set) that may sit on the attack button (leftskill 1), has a server function (srvdofunc) and whose range is "h2h" - the game's mark of a blow in melee reach. That gives
//   Amazon      Jab, Power Strike, Impale, Charged Strike, Fend, Lightning Strike
//   Necromancer Poison Dagger
//   Paladin     Sacrifice, Smite, Zeal, Vengeance, Conversion
//   Barbarian   Bash, Double Swing, Stun, Concentrate, Frenzy, Berserk
//   Druid       Feral Rage, Maul, Rabies, Fire Claws, Hunger, Fury
//   Assassin    Tiger Strike, Dragon Talon, Fists of Fire, Dragon Claw, Cobra Strike, Claws of Thunder, Dragon Tail, Blades of Ice, Royal Strike
//   Warlock     Cleave
// and leaves out, by the same columns: the movement attacks (Charge, Leap Attack, Whirlwind, Dragon Flight: range not h2h - in kRows all the same, unticked by default, below), Mirrored Blades
// (range "both", like Attack), the Barbarian's corpse skills (Find Potion, Find Item, Grim Ward: not leftskill), the bow and javelin skills (rng) and every spell. A skill not in kRows is never armed.
//
// Which of them are armed (the author's list, 2026-10-19: "a list of skills with a tick each"): [melee_armed] s<id>=1 / 0, one key a skill - the [autotarget] s<id> pattern. The 34 above
// are ticked by default; the four movement attacks are in the list too, UNticked by default (kRows' `byDefault`): ticked, the skill's button arms it and a touch is the game's cast of it
// with the touched monster as its target - the game's own path moves the hero (Whirlwind whirls to the monster's place, Leap Attack leaps at it, Charge charges it, Dragon Flight
// flies to it). Their rows are the same Skills.txt columns as the others' (the movers' read from the same table the 34 rows above match number for number). An unticked skill's button
// casts it as ever. `mover` marks them for [gameplay] attack_in_place (vr/stand_still.h MovesHero): a touch read as one is never held with Stand Still - the game's mover must move.
#pragma once

#include <cmath>
#include <cstdint>

#include "bow_mana.h"   // bowmana::Row, RowCostRaw, ShortOf: the game's cost arithmetic, shared with the bow's quiver_plain_no_mana

namespace meleearm {

struct Skill {
    bowmana::Row row;        // id, then Skills.txt mana, lvlmana, manashift, minmana
    const char* name;
    const char* cls;         // the class whose skill it is (Settings groups the ticks by it)
    bool byDefault = true;   // ticked when [melee_armed] has no key for it
    bool mover = false;      // the skill moves the hero (Charge, Leap Attack, Whirlwind, Dragon Flight)
};
inline constexpr Skill kRows[] = {
    {{10, 8, 1, 6, 1}, "Jab", "Amazon"},
    {{14, 8, 1, 6, 1}, "Power Strike", "Amazon"},
    {{19, 3, 0, 8, 1}, "Impale", "Amazon"},
    {{24, 16, 1, 6, 1}, "Charged Strike", "Amazon"},
    {{30, 5, 0, 8, 1}, "Fend", "Amazon"},
    {{34, 9, 0, 8, 1}, "Lightning Strike", "Amazon"},
    {{73, 12, 1, 6, 1}, "Poison Dagger", "Necromancer"},
    {{96, 0, 0, 8, 0}, "Sacrifice", "Paladin"},   // (costs life, not mana: never short)
    {{97, 2, 0, 8, 1}, "Smite", "Paladin"},
    {{106, 2, 0, 8, 1}, "Zeal", "Paladin"},
    {{111, 16, 1, 6, 1}, "Vengeance", "Paladin"},
    {{116, 4, 0, 8, 1}, "Conversion", "Paladin"},
    {{107, 9, 0, 8, 1}, "Charge", "Paladin", false, true},
    {{126, 2, 0, 8, 1}, "Bash", "Barbarian"},
    {{133, 8, -1, 5, 0}, "Double Swing", "Barbarian"},
    {{139, 2, 0, 8, 1}, "Stun", "Barbarian"},
    {{144, 2, 0, 8, 1}, "Concentrate", "Barbarian"},
    {{147, 3, 0, 7, 1}, "Frenzy", "Barbarian"},
    {{152, 4, 0, 8, 1}, "Berserk", "Barbarian"},
    {{143, 10, 0, 8, 1}, "Leap Attack", "Barbarian", false, true},
    {{151, 25, 1, 7, 1}, "Whirlwind", "Barbarian", false, true},
    {{232, 3, 0, 8, 1}, "Feral Rage", "Druid"},
    {{233, 3, 0, 8, 1}, "Maul", "Druid"},
    {{238, 10, 0, 8, 1}, "Rabies", "Druid"},
    {{239, 4, 0, 8, 1}, "Fire Claws", "Druid"},
    {{242, 3, 0, 8, 1}, "Hunger", "Druid"},
    {{248, 4, 0, 8, 1}, "Fury", "Druid"},
    {{254, 1, 0, 8, 1}, "Tiger Strike", "Assassin"},
    {{255, 6, 0, 8, 1}, "Dragon Talon", "Assassin"},
    {{259, 2, 0, 8, 1}, "Fists of Fire", "Assassin"},
    {{260, 2, 0, 8, 1}, "Dragon Claw", "Assassin"},
    {{265, 2, 0, 8, 1}, "Cobra Strike", "Assassin"},
    {{269, 4, 0, 8, 1}, "Claws of Thunder", "Assassin"},
    {{270, 10, 0, 8, 1}, "Dragon Tail", "Assassin"},
    {{274, 3, 0, 8, 1}, "Blades of Ice", "Assassin"},
    {{280, 4, 0, 8, 1}, "Royal Strike", "Assassin"},
    {{275, 15, 0, 8, 1}, "Dragon Flight", "Assassin", false, true},
    {{391, 3, 0, 8, 1}, "Cleave", "Warlock"},
};
constexpr int32_t kAttack = 0;   // Skills.txt: the plain attack
constexpr int kCount = (int)(sizeof kRows / sizeof kRows[0]);
static_assert(kCount <= 64, "Allowed keeps a bit a row");

inline const Skill* Find(int32_t id) {
    for (const Skill& s : kRows)
        if (s.row.id == id) return &s;
    return nullptr;
}
inline int IndexOf(int32_t id) {
    for (int i = 0; i < kCount; ++i)
        if (kRows[i].row.id == id) return i;
    return -1;
}
inline bool IsMelee(int32_t id) { return Find(id) != nullptr; }   // in the list (ticked or not)
inline bool IsMover(int32_t id) {
    const Skill* s = Find(id);
    return s && s->mover;
}
inline const char* Name(int32_t id) {
    const Skill* s = Find(id);
    return s ? s->name : id == kAttack ? "Attack" : "?";
}

// The weapon an armed skill needs (Skills.txt itypea): the Amazon's spear skills a spear or a javelin, Poison Dagger a dagger; the others
// any melee weapon (a form or two claws the game checks itself). An armed skill the weapon in hand cannot use is the plain attack: armed
// with a spear and swung with a sword, every touch went to the game as Jab - refused, no attack at all ("on the Amazon hit by touch
// stopped working: the monster's capsule turns red but the strike is not pressed", the author, 2026-10-20).
enum class Need : uint8_t { Any, Spear, Dagger };
inline Need NeedOf(int32_t id) {
    switch (id) {
        case 10: case 14: case 19: case 24: case 30: case 34: return Need::Spear;   // Jab, Power Strike, Impale, Charged Strike, Fend, Lightning Strike
        case 73: return Need::Dagger;                                               // Poison Dagger
        default: return Need::Any;
    }
}
// spear: a spear or a javelin in hand; dagger: a dagger.
inline bool Fits(int32_t id, bool spear, bool dagger) {
    switch (NeedOf(id)) {
        case Need::Spear: return spear;
        case Need::Dagger: return dagger;
        default: return true;
    }
}

// ---- the list's ticks ([melee_armed] s<id>) -----------------------------------------------------------------------------------------
class Allowed {
public:
    static Allowed Defaults() {
        Allowed a;
        for (int i = 0; i < kCount; ++i)
            if (kRows[i].byDefault) a.bits_ |= 1ull << i;
        return a;
    }
    bool Has(int32_t id) const {
        const int i = IndexOf(id);
        return i >= 0 && ((bits_ >> i) & 1);
    }
    // One ini key: s<id> = v (0 unticked, anything else ticked). A skill that is not in the list is ignored (false).
    bool Set(int32_t id, int v) {
        const int i = IndexOf(id);
        if (i < 0) return false;
        if (v) bits_ |= 1ull << i;
        else bits_ &= ~(1ull << i);
        return true;
    }
    uint64_t Bits() const { return bits_; }
    static Allowed FromBits(uint64_t b) {
        Allowed a;
        a.bits_ = b & (kCount >= 64 ? ~0ull : (1ull << kCount) - 1);
        return a;
    }
    int Count() const {
        int n = 0;
        for (int i = 0; i < kCount; ++i) n += (int)((bits_ >> i) & 1);
        return n;
    }
    bool operator==(const Allowed& o) const { return bits_ == o.bits_; }

private:
    uint64_t bits_ = 0;
};

// ---- the press: arm, re-arm, disarm --------------------------------------------------------------------------------------------
enum class Outcome { Pass, Armed, Switched, Disarmed };
inline const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::Armed: return "armed";
        case Outcome::Switched: return "armed instead";
        case Outcome::Disarmed: return "disarmed";
        default: return "passed to the game";
    }
}
class Arm {
public:
    // A press of a skill button whose slot holds `skill` (-1 none). Pass: the press is the game's (a spell, an unticked skill, an empty slot, the plain attack with nothing armed).
    Outcome Press(int32_t skill) {
        if (allowed_.Has(skill)) {
            if (armed_ == skill) {
                armed_ = -1;
                return Outcome::Disarmed;
            }
            const bool had = armed_ >= 0;
            armed_ = skill;
            return had ? Outcome::Switched : Outcome::Armed;
        }
        if (skill == kAttack && armed_ >= 0) {
            armed_ = -1;
            return Outcome::Disarmed;
        }
        return Outcome::Pass;
    }
    int32_t Armed() const { return armed_; }   // -1 none
    void Clear() { armed_ = -1; }
    // The list's ticks (live): an armed skill that is unticked now is disarmed (true then).
    bool SetAllowed(Allowed a) {
        allowed_ = a;
        if (armed_ >= 0 && !allowed_.Has(armed_)) {
            armed_ = -1;
            return true;
        }
        return false;
    }

private:
    int32_t armed_ = -1;
    Allowed allowed_ = Allowed::Defaults();
};

// ---- the pad: which press is ours -------------------------------------------------------------------------------------------------
// The game's pad skill slots (shield_smite.h SlotPress): B X Y RB are slots 0..3, with the left trigger held 4..7. A (8, 9) is the attack the touch presses itself and is never taken.
inline constexpr uint16_t kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000, kPadRB = 0x0200;
inline constexpr uint16_t kButtons[4] = {kPadB, kPadX, kPadY, kPadRB};
inline constexpr uint16_t kMask = kPadB | kPadX | kPadY | kPadRB;
inline constexpr uint8_t kLtDown = 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD: the left trigger counts as held (the second bank)

// Every poll of the game's pad: the buttons as they would go to the game, the left trigger. A button that goes down while `armable` (F4, hit by touch swinging a melee weapon, the hook in) on a slot
// whose skill Press does not pass is the arming's: it is taken out of what the game sees (strip) until it is let go - the game never sees that press. A button already down when it became armable,
// and every other button, is left alone.
struct Step {
    uint16_t strip = 0;       // the bits to take out of the game's pad this poll
    bool event = false;       // a press was the arming's this poll
    Outcome outcome = Outcome::Pass;
    int slot = -1;            // ... its slot (0..7)
    int32_t skill = -1;       // ... and the skill the slot holds
};
class PadFilter {
public:
    // skillOf(slot) -> the game's skill on that slot (-1 none, not known).
    template <class F>
    Step Poll(uint16_t buttons, uint8_t lt, bool armable, Arm& arm, F skillOf) {
        Step r;
        const uint16_t down = buttons & kMask;
        for (int i = 0; i < 4; ++i) {
            const uint16_t b = kButtons[i];
            if (held_ & b) {
                if (down & b) r.strip |= b;
                else held_ &= (uint16_t)~b;
                continue;
            }
            if (!(down & b) || (prev_ & b) || !armable) continue;
            const int slot = i + (lt > kLtDown ? 4 : 0);
            const int32_t skill = skillOf(slot);
            const Outcome o = arm.Press(skill);
            if (o == Outcome::Pass) continue;
            held_ |= b;
            r.strip |= b;
            r.event = true;
            r.outcome = o;
            r.slot = slot;
            r.skill = skill;
        }
        prev_ = down;
        return r;
    }
    void Reset() { prev_ = held_ = 0; }

private:
    uint16_t prev_ = 0, held_ = 0;
};

// ---- the hit: the skill, or the plain attack --------------------------------------------------------------------------------------
enum class Why { NotArmed, Skill, ManaUnknown, NoMana, NotLearned };
inline const char* WhyName(Why w) {
    switch (w) {
        case Why::Skill: return "the armed skill";
        case Why::ManaUnknown: return "the armed skill (the mana not read - the game decides)";
        case Why::NoMana: return "the plain attack - too little mana for the armed skill";
        case Why::NotLearned: return "the plain attack - the hero has no level in the armed skill";
        default: return "the plain attack - nothing armed";
    }
}
struct Hit {
    int32_t skill = kAttack;   // what the touch's press is read as: the armed skill or Attack (0)
    Why why = Why::NotArmed;
    float cost = -1.0f;        // the armed skill's cost in mana points at the level used (-1 not known)
    bool levelGuessed = false; // the level was not read: the first level's cost stands in
};
// `armed` (-1 none), the hero's total level in it (-1 not read, 0 none), the hero's mana in points (negative: not read).
inline Hit Decide(int32_t armed, int32_t level, float manaPoints) {
    Hit h;
    const Skill* s = Find(armed);
    if (!s) return h;
    if (level == 0) {
        h.why = Why::NotLearned;
        return h;
    }
    h.levelGuessed = level < 1;
    const int64_t raw = bowmana::RowCostRaw(&s->row, level < 1 ? 1 : level);
    h.cost = raw < 0 ? -1.0f : (float)raw / 256.0f;
    if (bowmana::ShortOf(raw, manaPoints)) {
        h.why = Why::NoMana;
        return h;
    }
    h.skill = armed;
    h.why = !std::isfinite(manaPoints) || manaPoints < 0.0f ? Why::ManaUnknown : Why::Skill;
    return h;
}

// ---- the press in flight ---------------------------------------------------------------------------------------------------------
// The skill decided for the touch's press (Decide), answered to the game's two reads of the attack slot (the cast's slot selection, the unit's hand-skill setter) while that press is young: the same
// window as the touch's own target lookups (melee_press.h, 350 ms). Not the armed skill: Attack (0) stands for "as the game has it".
inline constexpr uint64_t kWindowMs = 350;
class InFlight {
public:
    void Set(int32_t skill, uint64_t nowMs) {
        skill_ = skill;
        at_ = nowMs;
    }
    int32_t Answer(uint64_t nowMs) const { return skill_ > kAttack && nowMs >= at_ && nowMs - at_ <= kWindowMs ? skill_ : kAttack; }
    void Clear() { skill_ = kAttack; }

private:
    int32_t skill_ = kAttack;
    uint64_t at_ = 0;
};

}  // namespace meleearm
