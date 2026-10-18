// quiver.h - the quick choice of the skill of the next throw, taken "from the quiver" ([hands] javelin_quiver). Pure: no game, no clock, no globals;
// unit test cleanroom/tests/quiver_test.cpp.
//
// A javelin or a throwing weapon in the hand can be used with more than the plain Throw: an Amazon's Poison Javelin, Lightning Bolt, Plague Javelin and
// Lightning Fury (they need a javelin), a Barbarian's Double Throw (two thrown weapons), the spear skills Jab, Power Strike, Impale, Charged Strike, Fend and
// Lightning Strike (the game counts a javelin as a spear), a Necromancer's Poison Dagger (a throwing knife is a knife). The game decides what a weapon
// allows by skills.txt: a skill's required item type (itypea1) against the weapon's type and the types it counts as (itemtypes.txt Equiv1 / Equiv2):
//     javelin   jave  -> comb, spea            Amazon javelin  ajav -> jave          throwing knife  tkni -> comb, knif
//     throwing axe  taxe -> comb, axe          comb -> mele, thro                    missile potion  tpot -> thro
// This header is that table for the skills a thrown weapon can use, the list of what is OFFERED (learned, allowed with the weapon in hand, the default first)
// and the cursor that cycles it. It chooses nothing in the game: vr/quiver_wire.cpp publishes the selected skill, and the game's own cast does the rest -
// the pad's A is read as that skill (vr/vrcam.cpp HookSlotSkill), so its checks (the skill is the hero's, mana, the weapon, range, ammunition) all stay the game's.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace quiver {

// -- what is in the hand ---------------------------------------------------------------------------------------------------------------------------------
enum class Kind : uint8_t { None, Javelin, Knife, Axe, Potion };

// An item code as gamestate::HandCode gives it: four characters, the first in the low byte, padded with spaces ("jav ").
inline uint32_t Code(const char* c3) {
    return (uint32_t)(unsigned char)c3[0] | ((uint32_t)(unsigned char)c3[1] << 8) | ((uint32_t)(unsigned char)c3[2] << 16) | (' ' << 24);
}

// weapons.txt of 3.3: the javelins (jave: 15, with the glaives and the throwing spears; ajav: the Amazon's three), the throwing knives (tkni), the throwing axes
// (taxe) and the missile potions (tpot). Anything else is no thrown kind.
inline Kind KindOfCode(uint32_t code) {
    static const char* const javelins[] = {"jav", "pil", "ssp", "glv", "tsp", "9ja", "9pi", "9s9", "9gl", "9ts", "7ja", "7pi", "7s7", "7gl", "7ts", "am5", "ama", "amf"};
    static const char* const knives[] = {"tkf", "bkf", "9tk", "9bk", "7tk", "7bk"};
    static const char* const axes[] = {"tax", "bal", "9ta", "9b8", "7ta", "7b8"};
    static const char* const potions[] = {"gps", "ops", "gpm", "opm", "gpl", "opl"};
    for (const char* c : javelins) if (code == Code(c)) return Kind::Javelin;
    for (const char* c : knives) if (code == Code(c)) return Kind::Knife;
    for (const char* c : axes) if (code == Code(c)) return Kind::Axe;
    for (const char* c : potions) if (code == Code(c)) return Kind::Potion;
    return Kind::None;
}
inline bool IsThrownKind(Kind k) { return k != Kind::None; }
inline const char* KindName(Kind k) {
    switch (k) {
        case Kind::Javelin: return "javelin";
        case Kind::Knife: return "throwing knife";
        case Kind::Axe: return "throwing axe";
        case Kind::Potion: return "throwing potion";
        default: return "none";
    }
}

// -- what a skill needs (skills.txt itypea1) ---------------------------------------------------------------------------------------------------------------
enum class Type : uint8_t { Thro, Jave, Spea, Knif };

// Does a weapon of this kind count as that item type? (itemtypes.txt, followed through Equiv1 / Equiv2)
inline bool Counts(Kind w, Type need) {
    switch (w) {
        case Kind::Javelin: return need == Type::Thro || need == Type::Jave || need == Type::Spea;   // jave -> comb (-> mele, thro), spea
        case Kind::Knife: return need == Type::Thro || need == Type::Knif;                              // tkni -> comb, knif
        case Kind::Axe: return need == Type::Thro;                                                      // taxe -> comb, axe
        case Kind::Potion: return need == Type::Thro;                                                   // tpot -> thro
        default: return false;
    }
}

constexpr int32_t kDefault = -1;       // no choice: a throw is the game's Throw (2), a plain attack its Attack (0)
constexpr int32_t kSkillThrow = 2;

struct Rule {
    int32_t id;
    const char* name;      // the fallback name (the game's own text is used when it is known)
    Type need;
    bool ranged;           // skills.txt range "rng": a cast at a spot, which a throw by gesture may use; false = "h2h", a blow in reach
    bool dual;             // needs a thrown weapon in the other hand too (Double Throw: itypeb1 thro)
};
// The skills a thrown weapon can use, in the order they are offered: the ranged ones first, then the blows in reach. (skills.txt, D2R 3.3: every one is a
// left AND right skill; the passives Critical Strike and Penetrate are not offered.)
inline constexpr Rule kRules[] = {
    {15, "Poison Javelin", Type::Jave, true, false},
    {20, "Lightning Bolt", Type::Jave, true, false},
    {25, "Plague Javelin", Type::Jave, true, false},
    {35, "Lightning Fury", Type::Jave, true, false},
    {140, "Double Throw", Type::Thro, true, true},
    {10, "Jab", Type::Spea, false, false},
    {14, "Power Strike", Type::Spea, false, false},
    {19, "Impale", Type::Spea, false, false},
    {24, "Charged Strike", Type::Spea, false, false},
    {30, "Fend", Type::Spea, false, false},
    {34, "Lightning Strike", Type::Spea, false, false},
    {73, "Poison Dagger", Type::Knif, false, false},
};
inline const Rule* RuleOf(int32_t id) {
    for (const Rule& r : kRules) if (r.id == id) return &r;
    return nullptr;
}

// -- the list that is offered --------------------------------------------------------------------------------------------------------------------------
constexpr int kNameLen = 48;
struct Entry {
    int32_t id = kDefault;
    bool ranged = true;
    char name[kNameLen] = "Throw";
};
constexpr int kMaxEntries = 16;

inline void CopyName(char* dst, const char* src) {
    int i = 0;
    for (; src && src[i] && i < kNameLen - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

// A skill the hero has (the skill wheel's catalog: vr/wheel_types.h Skill - active skills at a level above 0, with the game's own text).
struct Learned {
    int32_t id = -1;
    int32_t level = 0;
    const char* name = nullptr;   // the game's text, or null
};

struct Pool {
    Entry e[kMaxEntries];
    int n = 0;
};

struct Needs {
    Kind weapon = Kind::None;
    bool otherHandThrown = false;   // the other hand holds a thrown weapon too (Double Throw)
};

// The default first, then every learned skill the weapon allows. A potion or nothing in the hand offers nothing (n = 0): the plain Throw is the only throw.
inline Pool Build(const Needs& need, const Learned* learned, size_t nLearned) {
    Pool p;
    if (need.weapon == Kind::None || need.weapon == Kind::Potion) return p;
    Entry& d = p.e[p.n++];
    d.id = kDefault;
    d.ranged = true;
    CopyName(d.name, "Throw");
    for (const Rule& r : kRules) {
        if (p.n >= kMaxEntries) break;
        if (!Counts(need.weapon, r.need) || (r.dual && !need.otherHandThrown)) continue;
        const Learned* have = nullptr;
        for (size_t i = 0; i < nLearned && learned; ++i)
            if (learned[i].id == r.id && learned[i].level > 0) { have = &learned[i]; break; }
        if (!have) continue;
        Entry& e = p.e[p.n++];
        e.id = r.id;
        e.ranged = r.ranged;
        const char* nm = have->name && have->name[0] ? have->name : r.name;
        CopyName(e.name, nm);
    }
    return p;
}

// The cursor over the pool. A pool of the default alone (or none) has nothing to cycle.
class Selector {
public:
    // The pool as it is now (the weapon, the learned list): the choice stays where it still can; else it goes back to the default.
    // True when the choice changed because of it.
    bool Rebuild(const Pool& p) {
        const int32_t was = idx_ < pool_.n ? pool_.e[idx_].id : kDefault;
        pool_ = p;
        int at = 0;
        for (int i = 0; i < pool_.n; ++i) if (pool_.e[i].id == was) { at = i; break; }
        const bool changed = was != (pool_.n ? pool_.e[at].id : kDefault);
        idx_ = at;
        return changed;
    }
    int Count() const { return pool_.n; }
    bool CanCycle() const { return pool_.n > 1; }
    // dir +1 next, -1 previous; wraps. False when there is nothing to cycle.
    bool Step(int dir) {
        if (!CanCycle() || dir == 0) return false;
        idx_ = ((idx_ + (dir > 0 ? 1 : pool_.n - 1)) % pool_.n);
        return true;
    }
    void Reset() { idx_ = 0; }
    const Entry& Current() const { static const Entry none; return idx_ < pool_.n ? pool_.e[idx_] : none; }
    int32_t Id() const { return Current().id; }
    // 1-based position and the size, for "2 of 5".
    int Position() const { return idx_ < pool_.n ? idx_ + 1 : 0; }
    const Pool& Pool_() const { return pool_; }

private:
    Pool pool_;
    int idx_ = 0;
};

// -- what the game's cast is told ---------------------------------------------------------------------------------------------------------------------
// The A slot is bound to Attack (0) and the hook of the slot->skill read answers for it. `swapped` is what the throw's own swap already answered (Throw for
// a throw by gesture, else the binding). With a skill chosen in the quiver and a thrown weapon in the hand the plain attack is that skill, like a skill hotkey
// (a hand skill: the game's own setter and cast do the rest) - except that a throw by gesture is a cast at a spot, so a blow in reach (a spear skill) chosen
// is not thrown: the gesture's throw stays the Throw.
inline int32_t Answer(int32_t swapped, int32_t bound, bool atCastSite, bool on, bool thrownInHand, int32_t chosen, bool chosenRanged, bool throwLive) {
    if (bound != 0 || !atCastSite || !on || !thrownInHand || chosen < 0) return swapped;
    if (throwLive && !chosenRanged) return swapped;
    return chosen;
}

}  // namespace quiver
