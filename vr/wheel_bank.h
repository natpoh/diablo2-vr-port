#pragma once
// The skill wheel's logic against the game's controller skill slots: reading the twelve slots and the skills the player
// can assign, and assigning one through the game's OWN setter with a read-back. The game is behind the Game interface,
// so the whole of it is tested with a fake (cleanroom/tests/skill_wheel_test.cpp); vr/wheel_game.cpp is the real one.
//
// The rules this keeps:
//   - the wheel never writes the game's skill records or tables itself: the only writer is the game's setter
//     (AssignSkill / ClearSkill, the function of its skill-select button), called with the same arguments the button
//     gives it, and only while the game is in its controller layout (the setter picks the controller's table by that);
//   - a request is refused, not forced, when the session, the weapon set's bank, the slot's old skill or the skill's
//     availability is not what the player chose from;
//   - "applied" is said only when the controller table, read back, holds what was asked for.
// After bearstonem/d2r-pcvr (MIT, vr/native_ui.cpp Capture / Assign), written again for our own types.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "wheel_types.h"

namespace wheel {

struct Binding {
    int32_t skill = -1, owner = -1;
};

struct Learned {
    int32_t id = -1, owner = -1, level = 0;
};

class Game {
public:
    virtual ~Game() = default;
    virtual bool ControllerLayout() = 0;                      // the game is in its controller layout now
    virtual int Bank() = 0;                                   // the weapon set's half of the controller table: 0 or 16
    virtual int ButtonSlot(uint16_t button, bool alt) = 0;    // the game's slot index (0..15) of a pad button, -1 none
    virtual bool ReadBinding(int index, Binding* out) = 0;    // the controller table's record at bank + slot
    virtual bool HasSkill(int32_t id, int32_t owner, int32_t* level) = 0;   // the player's skill node, its base level
    virtual bool LeftFlag(int32_t id, int32_t owner, bool* left) = 0;       // the Skills row's left-hand flag
    virtual bool LearnedSkills(std::vector<Learned>* out) = 0;              // active skills the player has (passives out)
    virtual bool Name(int32_t id, char* out, int n) = 0;                    // the game's own text for a skill
    virtual void DoAssign(int slotIndex, bool left, int32_t skill, int32_t owner) = 0;   // the game's setter
    virtual void DoClear(int slotIndex) = 0;                                              // the game's clearer
    // The skill trees (kCategories), from the player's own game. Defaults: nothing known, so an editor without them has the one list.
    virtual int PlayerClass() { return -1; }                                // the player's class (the game's own index), -1 not known
    virtual bool TreesKnown() { return false; }                             // which skill is on which tree, and the trees' names, are known
    virtual int Tree(int32_t id, int32_t owner) { (void)id; (void)owner; return kTreeUnknown; }   // 1..3 the class's tree, 0 General / Items, -1 not known
    virtual bool TreeName(int tree, char* out, int n) { (void)tree; (void)out; (void)n; return false; }   // the game's own name of tree 1..3
    virtual int RunState() { return kRunUnknown; }                          // RunState: the game's run / walk switch now
    // [openxr] skill_rt_a: the right trigger and A are slot controls, so the list offers the basic attack (kSkillAttack) beside Interact - the way to
    // put a slot back to the plain action. Off: the list is what it was.
    virtual bool OfferAttack() { return false; }
};

// The catalog lists the town portal once (the scroll and the tome are one entry, as the game's own list does) and has no
// identify scroll or tome (those are used on items, not bound).
inline int32_t CatalogId(int32_t id) {
    if (id == 217 || id == 218) return -1;
    if (id == kSkillScroll || id == kSkillTome) return kSkillTownPortal;
    return id;
}

// The game's slot indices of the slots. The wheel's eight (0..7) must all be there, each different: a layout this does not know must
// not edit or cast a different slot - false otherwise. The other four (A, RT, LT + A, LT + RT; 8..11) are filled in when they can be
// told, else -1 (the slot is shown as not there and refuses an edit):
//   - A and LT + A come from the game's own lookup of the A button, and must be the same base six apart (the lookup adds 6 for the
//     left trigger) and not one the other buttons use;
//   - RT has no button bit the lookup could be asked for, so it is the ONE base left of the game's six (0..5: the lookup's table has six
//     records and adds 6 for the left trigger) once B, X, Y, RB and A are taken - and only then. Anything else leaves RT unknown.
inline bool SlotIndices(Game& g, int out[kSlotCount]) {
    unsigned used = 0;
    for (int i = 0; i < kWheelSlots; ++i) {
        const int s = g.ButtonSlot(kSlotDefs[i].button, kSlotDefs[i].alt);
        if (s < 0 || s > 15 || (used & (1u << s))) return false;
        used |= 1u << s;
        out[i] = s;
    }
    for (int i = kWheelSlots; i < kSlotCount; ++i) out[i] = -1;
    unsigned bases = 0;   // the six bases (0..5) the first row's buttons take
    for (int i = 0; i < 4; ++i) {
        if (out[i] > 5) return true;   // (not the layout of six bases: the extra four stay unknown)
        bases |= 1u << out[i];
    }
    const int a = g.ButtonSlot(kPadA, false), aAlt = g.ButtonSlot(kPadA, true);
    if (a >= 0 && a <= 5 && aAlt == a + 6 && !(bases & (1u << a)) && !(used & (1u << a)) && !(used & (1u << aAlt))) {
        out[8] = a;
        out[10] = aAlt;
        used |= (1u << a) | (1u << aAlt);
        bases |= 1u << a;
        const unsigned left = ~bases & 0x3Fu;   // the bases nobody has taken
        if (left && !(left & (left - 1))) {      // exactly one
            int r = 0;
            while (!(left & (1u << r))) ++r;
            if (!(used & (1u << r)) && !(used & (1u << (r + 6)))) {
                out[9] = r;
                out[11] = r + 6;
            }
        }
    }
    return true;
}

inline void SetName(Game& g, Skill* s) {
    if (!g.Name(s->id, s->name, kNameLen) || !s->name[0]) std::snprintf(s->name, kNameLen, "Skill %d", (int)s->id);
}

// Fills `s` (everything but session and at: the caller stamps those). s->ready stays false when the slots cannot be read.
inline bool Capture(Game& g, Snapshot* s) {
    s->ready = s->editable = false;
    s->bank = -1;
    s->count = 0;
    const int bank = g.Bank();
    if (bank != 0 && bank != 16) return false;
    int idx[kSlotCount];
    if (!SlotIndices(g, idx)) return false;
    const bool attackOffered = g.OfferAttack();
    Skill slot[kSlotCount];
    for (int i = 0; i < kSlotCount; ++i) {
        if (idx[i] < 0) continue;   // (a slot this cannot place: not known, not shown as one)
        Binding b;
        if (!g.ReadBinding(bank + idx[i], &b) || b.skill < -1 || b.skill > 4095) {
            if (i < kWheelSlots) return false;   // (the wheel's own eight must be readable; one of the extras that is not is just not there)
            slot[i] = Skill{};
            continue;
        }
        Skill& k = slot[i];
        k.known = true;
        k.id = b.skill;
        k.owner = b.owner;
        if (k.id < 0) {
            std::snprintf(k.name, kNameLen, "Unassigned");
            continue;
        }
        int32_t level = 0;
        if (k.id == kSkillTownPortal) {   // the scroll while there is one, else the tome
            k.available = g.HasSkill(kSkillScroll, -1, &level) && level > 0;
            if (!k.available) k.available = g.HasSkill(kSkillTome, -1, &level);
        } else {
            k.available = k.id >= kSkillInteract || (k.id == kSkillAttack && attackOffered) || g.HasSkill(k.id, k.owner, &level);
        }
        k.level = level;
        SetName(g, &k);
    }
    // the skills the player can bind: Interact first, then the learned ones by name
    std::vector<Learned> learned;
    const bool haveList = g.LearnedSkills(&learned);
    std::vector<Skill> catalog;
    if (haveList) {
        Skill interact;
        interact.id = kSkillInteract;
        interact.known = interact.available = true;
        SetName(g, &interact);
        catalog.push_back(interact);
        if (attackOffered) {   // the basic attack, second: the way to put a slot back to the plain action
            Skill attack;
            attack.id = kSkillAttack;
            attack.known = attack.available = true;
            SetName(g, &attack);
            catalog.push_back(attack);
        }
        for (const Learned& l : learned) {
            const int32_t id = CatalogId(l.id);
            if (id < 0) continue;
            const int32_t owner = id == kSkillTownPortal ? -1 : l.owner;
            const bool dup = std::any_of(catalog.begin(), catalog.end(), [&](const Skill& c) { return c.id == id && c.owner == owner; });
            if (dup) continue;
            Skill k;
            k.id = id;
            k.owner = owner;
            k.level = l.level;
            k.known = k.available = true;
            SetName(g, &k);
            catalog.push_back(k);
        }
        // the skill trees: each entry's tree (a skill an item grants is General / Items whatever the game's table says of the skill)
        if (g.TreesKnown())
            for (Skill& k : catalog) k.tree = (int8_t)(k.owner != -1 ? 0 : std::min(3, std::max(0, g.Tree(k.id, k.owner))));
        std::sort(catalog.begin() + (attackOffered ? 2 : 1), catalog.end(), [](const Skill& a, const Skill& b) {
            const int c = std::strncmp(a.name, b.name, kNameLen);
            return c ? c < 0 : a.owner < b.owner;
        });
        if (catalog.size() > (size_t)kCatalogMax) catalog.resize(kCatalogMax);
    }
    for (int i = 0; i < kSlotCount; ++i) s->slot[i] = slot[i];
    s->playerClass = g.PlayerClass();
    s->treesKnown = haveList && s->playerClass >= 0 && g.TreesKnown();
    for (int t = 0; t < 3; ++t) {
        s->treeName[t][0] = 0;
        if (s->treesKnown && (!g.TreeName(t + 1, s->treeName[t], kNameLen) || !s->treeName[t][0])) std::snprintf(s->treeName[t], kNameLen, "Tree %d", t + 1);
    }
    s->run = (uint8_t)g.RunState();
    s->count = (uint32_t)catalog.size();
    for (size_t i = 0; i < catalog.size(); ++i) s->catalog[i] = catalog[i];
    s->bank = bank;
    s->ready = true;
    s->editable = haveList && g.ControllerLayout();
    return true;
}

// Whether the snapshot's catalog offers this skill to bind.
inline bool Offered(const Snapshot& s, int32_t id, int32_t owner) {
    for (uint32_t i = 0; i < s.count && i < (uint32_t)kCatalogMax; ++i)
        if (s.catalog[i].id == id && s.catalog[i].owner == owner && s.catalog[i].available) return true;
    return false;
}

// Carries a request out on the UI thread (the caller's), against the snapshot the player chose from and the game as it is
// now. Applied only when the controller table, read back, holds the request.
inline Result Apply(Game& g, const Request& r, const Snapshot& chosenFrom, uint64_t now, const char** why) {
    const char* dummy = nullptr;
    if (!why) why = &dummy;
    *why = "";
    auto refuse = [&](const char* text) { *why = text; return Result::Refused; };
    if (r.op != Op::Assign && r.op != Op::Clear) return refuse("no such operation");
    if (r.slot < 0 || r.slot >= kSlotCount) return refuse("no such slot");
    if (!Fresh(chosenFrom, now, r.session)) return refuse("the game's state is out of date");
    if (!chosenFrom.editable) return refuse("the game is not in its controller layout");
    if (chosenFrom.bank != r.bank || (r.bank != 0 && r.bank != 16) || g.Bank() != r.bank) return refuse("the weapon set changed");
    if (!g.ControllerLayout()) return refuse("the game is not in its controller layout");
    int idx[kSlotCount];
    if (!SlotIndices(g, idx)) return refuse("the controller's slots are not the ones this knows");
    const int index = idx[r.slot];
    if (index < 0) return refuse("that slot is not one this can tell");
    Binding old;
    if (!g.ReadBinding(r.bank + index, &old)) return refuse("the slot could not be read");
    if (old.skill != r.prevSkill || old.owner != r.prevOwner) return refuse("the slot changed meanwhile");
    if (r.op == Op::Assign) {
        if (r.skill < 0 || !Offered(chosenFrom, r.skill, r.owner)) return refuse("that skill is not available");
        bool left = r.skill == kSkillInteract || r.skill == kSkillLoot || r.skill == kSkillCubeLoot;
        if (r.skill == kSkillAttack) {   // the basic attack: a left-hand row of the player's own list if the game has it there, else the command it is
            int32_t level = 0;
            bool flag = false;
            left = g.HasSkill(kSkillAttack, -1, &level) && g.LeftFlag(kSkillAttack, -1, &flag) ? flag : true;
        } else if (!left && r.skill != kSkillTownPortal) {
            int32_t level = 0;
            bool flag = false;
            if (!g.HasSkill(r.skill, r.owner, &level) || !g.LeftFlag(r.skill, r.owner, &flag)) return refuse("that skill is not available");
            left = flag;
        } else if (r.skill == kSkillTownPortal) {
            bool flag = false;
            int32_t level = 0;
            const int32_t of = g.HasSkill(kSkillScroll, -1, &level) && level > 0 ? kSkillScroll : kSkillTome;
            if (!g.HasSkill(of, -1, &level) || !g.LeftFlag(of, -1, &flag)) return refuse("that skill is not available");
            left = flag;
        }
        g.DoAssign(index, left, r.skill, r.owner);
    } else {
        if (r.skill != -1 || r.owner != -1) return refuse("a clear names no skill");
        g.DoClear(index);
    }
    Binding now2;
    if (!g.ReadBinding(r.bank + index, &now2)) return refuse("the slot could not be read back");
    // (a cleared slot is a slot with no skill: what the game leaves in its owner field is of no interest)
    if (r.op == Op::Clear ? now2.skill != -1 : (now2.skill != r.skill || now2.owner != r.owner)) return refuse("the game did not take it");
    return Result::Applied;
}

}  // namespace wheel
