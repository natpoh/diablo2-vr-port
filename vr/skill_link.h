#pragma once
// [gestures] skill_link: the hero's skills to BodyWalk, and the skill BodyWalk says to cast - a button or a gesture the player gave that
// skill in BodyWalk (Game Link's Skills and Casts blocks, shared/skill_link_abi.h). The pure part: the Skills block made from the skill
// panel's snapshot (vr/wheel_types.h), the new casts read from the Casts ring, and how a cast goes to the game - the skill's own pad slot
// pressed, or, when it is on none, the skill put on the spare slot first through the game's own setter (wheel_game.cpp, as the panel's
// editor does), then that slot pressed. Never a server call of our own: the press is the game's pad poll, the cast is the game's.
// The wiring (the game's thread, the press, F4) is vr/skill_link_wire.cpp. Test: cleanroom/tests/skill_link_test.cpp.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../shared/skill_link_abi.h"
#include "wheel_types.h"

namespace skilllink {

constexpr uint32_t kStaleStamp = 5000;        // 0.1 ms units: a cast older than half a second is not acted on (we were not reading)
constexpr uint64_t kAssignWaitMs = 1500;      // a skill put on the spare slot must show there within this, or the cast is given up
constexpr uint64_t kPublishMs = 1000;         // the Skills block is written again at least this often (BodyWalk takes 3 s still as gone)
constexpr uint32_t kTapMs = 65, kAltRowMs = 180;   // a slot's press: the left trigger's row needs the longer one (shield_smite.h PulseMs)
constexpr uint32_t kHeldMs = 80;              // a held skill's press is renewed every tick, and runs out this long after the last
constexpr int kDefaultSpareSlot = 7;          // LT + RB: the slot a skill on no slot is put on before its cast ([gestures] skill_link_slot)

// The order the slots are looked in when a skill sits on more than one: the plain face buttons, then A and RT, then the left trigger's row.
constexpr int kSlotOrder[wheel::kSlotCount] = {0, 1, 2, 3, 8, 9, 4, 5, 6, 7, 10, 11};

// The bindings' set: the hero's class ("sorceress"), and its name to show. kClassKeys are skel::kHeroClasses (vr/skeletons.h).
constexpr const char* kClassKeys[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
constexpr const char* kClassNames[8] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};
inline const char* ClassKey(int32_t cls) { return cls >= 0 && cls < 8 ? kClassKeys[cls] : ""; }
inline const char* ClassName(int32_t cls) { return cls >= 0 && cls < 8 ? kClassNames[cls] : ""; }

// A skill a gesture or a button may cast: not one of the game's plain actions (the basic attack, pick up / loot - the layout's own).
inline bool Bindable(const wheel::Skill& k) { return k.known && k.available && !wheel::IsPlainAction(k.id); }

// The slot (wheel numbering, 0..11) a skill sits on, kSlotOrder first; -1 none. owner -2 = any.
inline int SlotOf(const wheel::Snapshot& s, int32_t id, int32_t owner = -2) {
    for (int slot : kSlotOrder) {
        const wheel::Skill& k = s.slot[slot];
        if (k.known && k.id == id && (owner == -2 || k.owner == owner)) return slot;
    }
    return -1;
}

// The catalog's entry of a skill; null when the hero has it not.
inline const wheel::Skill* Find(const wheel::Snapshot& s, int32_t id) {
    for (uint32_t i = 0; i < s.count && i < (uint32_t)wheel::kCatalogMax; ++i)
        if (s.catalog[i].id == id && Bindable(s.catalog[i])) return &s.catalog[i];
    return nullptr;
}

// A slot's press as the game's pad reads it: its button (none for RT, the right trigger), the left trigger for the second row.
struct Press {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    uint32_t ms = 0;
};
inline Press SlotPress(int slot) {
    Press p;
    if (slot < 0 || slot >= wheel::kSlotCount) return p;
    const wheel::SlotDef& d = wheel::kSlotDefs[slot];
    p.buttons = d.button;
    p.rt = d.button ? 0 : 255;
    p.lt = d.alt ? 255 : 0;
    p.ms = d.alt ? kAltRowMs : kTapMs;
    return p;
}

// The Skills block's content (everything after the header) from the snapshot; the record wish and the revision are the caller's.
// heroName may be null. Returns false when there is nothing to list (no hero read).
inline bool Fill(const wheel::Snapshot& s, const char* heroName, BW_LinkSkills* out) {
    const uint32_t keepSeq = out->recordSequence, keepRev = out->revision;
    const int32_t keepSkill = out->recordSkill;
    std::memset((char*)out + sizeof(BW_LinkHeader), 0, sizeof(BW_LinkSkills) - sizeof(BW_LinkHeader));
    out->recordSequence = keepSeq;
    out->recordSkill = keepSkill;
    out->revision = keepRev;
    std::snprintf(out->gameId, sizeof out->gameId, "%s", "d2r");
    for (int i = 0; i < wheel::kSlotCount && i < (int)BW_LINK_SKILL_SLOTS; ++i)
        std::snprintf(out->slotName[i], sizeof out->slotName[i], "%s", wheel::kSlotDefs[i].label);
    if (!s.ready || s.playerClass < 0) return false;
    std::snprintf(out->setKey, sizeof out->setKey, "%s", ClassKey(s.playerClass));
    std::snprintf(out->setName, sizeof out->setName, "%s", ClassName(s.playerClass));
    if (heroName) std::snprintf(out->heroName, sizeof out->heroName, "%s", heroName);
    if (s.treesKnown) {
        std::snprintf(out->groupName[0], sizeof out->groupName[0], "%s", "General / Items");
        for (int t = 0; t < 3; ++t) std::snprintf(out->groupName[t + 1], sizeof out->groupName[t + 1], "%s", s.treeName[t]);
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < s.count && i < (uint32_t)wheel::kCatalogMax && n < BW_LINK_SKILLS_MAX; ++i) {
        const wheel::Skill& k = s.catalog[i];
        if (!Bindable(k)) continue;
        bool dup = false;   // one entry per skill id: BodyWalk binds by id (an item's copy of a skill the hero has is the same cast)
        for (uint32_t j = 0; j < n && !dup; ++j) dup = out->skill[j].id == k.id;
        if (dup) continue;
        BW_LinkSkill& o = out->skill[n++];
        o.id = k.id;
        o.flags = BW_LINK_SKILL_LEARNED | (k.owner >= 0 ? BW_LINK_SKILL_ITEM : 0u);
        o.level = (int16_t)(k.level < 0 ? 0 : k.level > 32767 ? 32767 : k.level);
        o.group = k.tree;
        const int slot = SlotOf(s, k.id);
        o.slot = (int8_t)slot;
        if (slot >= 0) o.flags |= BW_LINK_SKILL_ON_SLOT;
        std::snprintf(o.name, sizeof o.name, "%s", k.name);
    }
    out->count = n;
    return true;
}

// Whether two Skills contents differ in what BodyWalk shows (the revision and the record wish left out).
inline bool SameList(const BW_LinkSkills& a, const BW_LinkSkills& b) {
    if (a.count != b.count || std::strcmp(a.setKey, b.setKey) || std::strcmp(a.setName, b.setName) || std::strcmp(a.heroName, b.heroName) ||
        std::memcmp(a.groupName, b.groupName, sizeof a.groupName) || std::memcmp(a.slotName, b.slotName, sizeof a.slotName))
        return false;
    return std::memcmp(a.skill, b.skill, a.count * sizeof(BW_LinkSkill)) == 0;
}

// The new casts in BodyWalk's ring, oldest first: each sequence once, none older than kStaleStamp. A BodyWalk started again (another
// writer, or its sequence back below ours) counts from the start.
class CastReader {
    uint32_t last_ = 0;
    uint32_t pid_ = 0;

public:
    int Step(const BW_LinkCasts& c, uint32_t now, BW_LinkCast* out, int max) {
        if (c.header.writerPid != pid_ || c.lastSequence < last_) {
            pid_ = c.header.writerPid;
            last_ = 0;
        }
        int n = 0;
        const uint32_t ring = BW_LINK_CASTS_RING;
        uint32_t from = last_ + 1;
        if (c.lastSequence >= ring && from < c.lastSequence - ring + 1) from = c.lastSequence - ring + 1;
        for (uint32_t seq = from; seq <= c.lastSequence && seq != 0; ++seq) {
            const BW_LinkCast& e = c.ring[seq % ring];
            if (e.sequence != seq || e.skill < 0) continue;
            if ((uint32_t)(now - e.stamp) > kStaleStamp) continue;
            if (n < max) out[n++] = e;
        }
        last_ = c.lastSequence;
        return n;
    }
    void Reset() { *this = CastReader{}; }
};

// One cast on its way to the game. Step it with each snapshot: it says what to do now - press a slot, ask the game's setter to put the
// skill on the spare slot, or nothing (waiting for that, or done).
struct Plan {
    enum class Do : uint8_t { Nothing, Press, Assign, GiveUp };
    Do what = Do::Nothing;
    int slot = -1;
    wheel::Request assign;    // Do::Assign: what to Submit (its sequence is the caller's)
    const char* why = "";     // Do::GiveUp: why
};

class Caster {
    int32_t skill_ = -1;
    bool assigned_ = false;   // the setter was asked; now waiting to see the skill on the spare slot
    uint64_t since_ = 0;

public:
    bool Busy() const { return skill_ >= 0; }
    int32_t Skill() const { return skill_; }
    // A new cast replaces one still waiting.
    void Start(int32_t skill, uint64_t now) {
        skill_ = skill;
        assigned_ = false;
        since_ = now;
    }
    void Reset() { *this = Caster{}; }

    // spareSlot: where a skill on no slot goes (-1: nowhere - only skills on a slot are cast). assignRefused: the setter answered no.
    Plan Step(const wheel::Snapshot& s, uint64_t now, int spareSlot, bool assignRefused = false) {
        Plan p;
        if (skill_ < 0) return p;
        auto giveUp = [&](const char* why) {
            p.what = Plan::Do::GiveUp;
            p.why = why;
            skill_ = -1;
            return p;
        };
        if (!s.ready) return now - since_ > kAssignWaitMs ? giveUp("the game's skills could not be read") : p;
        const int on = SlotOf(s, skill_);
        if (on >= 0) {
            p.what = Plan::Do::Press;
            p.slot = on;
            skill_ = -1;
            return p;
        }
        if (assignRefused) return giveUp("the game's setter refused to put it on the spare slot");
        if (assigned_) return now - since_ > kAssignWaitMs ? giveUp("it did not show on the spare slot in time") : p;
        const wheel::Skill* k = Find(s, skill_);
        if (!k) return giveUp("the hero does not have that skill now");
        if (spareSlot < 0 || spareSlot >= wheel::kSlotCount) return giveUp("it is on no slot, and no spare slot is set");
        if (!s.editable) return giveUp("it is on no slot, and the game is not in its controller layout (its slots cannot be set)");
        p.what = Plan::Do::Assign;
        p.slot = spareSlot;
        p.assign.op = wheel::Op::Assign;
        p.assign.session = s.session;
        p.assign.slot = spareSlot;
        p.assign.skill = k->id;
        p.assign.owner = k->owner;
        p.assign.bank = s.bank;
        p.assign.prevSkill = s.slot[spareSlot].known ? s.slot[spareSlot].id : -1;
        p.assign.prevOwner = s.slot[spareSlot].known ? s.slot[spareSlot].owner : -1;
        assigned_ = true;
        since_ = now;
        return p;
    }
};

}  // namespace skilllink
