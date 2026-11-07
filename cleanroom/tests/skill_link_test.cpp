// Offline checks of vr/skill_link.h and shared/skill_link_abi.h ([gestures] skill_link): the Skills / Casts blocks' wire layout (byte
// offsets BodyWalk reads the same), the Skills list made from the skill panel's snapshot, the Casts ring read once each and never stale,
// and how a cast reaches the game (the skill's pad slot, or the spare slot through the game's setter first). Run: skill_link_test.exe
// (exit 0 = all passed).
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>

#include "../../vr/skill_link.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestLayout() {
    // the offsets BodyWalk's own header has (sdk/include/bodywalk/game_link.h, claude/skill-gestures): a change on either side shows here
    CHECK(offsetof(BW_LinkSkill, id) == 0 && offsetof(BW_LinkSkill, flags) == 4 && offsetof(BW_LinkSkill, level) == 8);
    CHECK(offsetof(BW_LinkSkill, group) == 10 && offsetof(BW_LinkSkill, slot) == 11 && offsetof(BW_LinkSkill, name) == 12);
    CHECK(offsetof(BW_LinkSkills, gameId) == 24 && offsetof(BW_LinkSkills, revision) == 56 && offsetof(BW_LinkSkills, count) == 60);
    CHECK(offsetof(BW_LinkSkills, setKey) == 64 && offsetof(BW_LinkSkills, setName) == 96 && offsetof(BW_LinkSkills, heroName) == 160);
    CHECK(offsetof(BW_LinkSkills, groupName) == 192 && offsetof(BW_LinkSkills, slotName) == 384);
    CHECK(offsetof(BW_LinkSkills, recordSequence) == 640 && offsetof(BW_LinkSkills, recordSkill) == 644 && offsetof(BW_LinkSkills, skill) == 648);
    CHECK(sizeof(BW_LinkSkills) == 12168);
    CHECK(offsetof(BW_LinkCasts, stamp) == 24 && offsetof(BW_LinkCasts, skillsRevision) == 28 && offsetof(BW_LinkCasts, setKey) == 32);
    CHECK(offsetof(BW_LinkCasts, lastSequence) == 64 && offsetof(BW_LinkCasts, heldCount) == 68 && offsetof(BW_LinkCasts, held) == 72);
    CHECK(offsetof(BW_LinkCasts, ring) == 104 && offsetof(BW_LinkCasts, recordSequence) == 360 && offsetof(BW_LinkCasts, recordState) == 368);
    CHECK(offsetof(BW_LinkCasts, boundCount) == 372 && offsetof(BW_LinkCasts, bound) == 376 && sizeof(BW_LinkCasts) == 1912);
    // the names and versions both sides open
    CHECK(std::wcscmp(BW_LINK_SKILLS_NAME, L"Local\\BodyWalk.Link.Skills") == 0 && std::wcscmp(BW_LINK_CASTS_NAME, L"Local\\BodyWalk.Link.Casts") == 0);
    CHECK(BW_LINK_SKILLS_VERSION == 1 && BW_LINK_CASTS_VERSION == 1 && BW_LINK_CASTS_RING == 16);
    // the slot names fit the block, and the wheel's twelve slots fit its sixteen
    CHECK(wheel::kSlotCount <= (int)BW_LINK_SKILL_SLOTS);
    for (const wheel::SlotDef& d : wheel::kSlotDefs) CHECK(std::strlen(d.label) < 16);
}

static void TestGestureLayout() {
    // the gesture overlay's blocks: the offsets BodyWalk's header has (its tests/test_game_skills.cpp checks the same)
    CHECK(offsetof(BW_LinkGesture, skill) == 8 && offsetof(BW_LinkGesture, rgba) == 12 && offsetof(BW_LinkGesture, threshold) == 20);
    CHECK(offsetof(BW_LinkGesture, name) == 24 && offsetof(BW_LinkGesture, pts) == 72 && offsetof(BW_LinkGesture, joints) == 456);
    CHECK(offsetof(BW_LinkGesture, grip) == 516 && offsetof(BW_LinkGesture, jointValid) == 524 && offsetof(BW_LinkGesture, button) == 528);
    CHECK(sizeof(BW_LinkGesture) == 560);
    CHECK(offsetof(BW_LinkGestures, setKey) == 32 && offsetof(BW_LinkGestures, count) == 64 && offsetof(BW_LinkGestures, cmdDone) == 68);
    CHECK(offsetof(BW_LinkGestures, cmdResult) == 72 && offsetof(BW_LinkGestures, recordState) == 76 && offsetof(BW_LinkGestures, recordGesture) == 80);
    CHECK(offsetof(BW_LinkGestures, recordType) == 84 && offsetof(BW_LinkGestures, recordSkill) == 88 && offsetof(BW_LinkGestures, lastStamp) == 92);
    CHECK(offsetof(BW_LinkGestures, lastPercent) == 100 && offsetof(BW_LinkGestures, lastMatched) == 108 && offsetof(BW_LinkGestures, g) == 112);
    CHECK(sizeof(BW_LinkGestures) == 35952 && sizeof(BW_LinkGestures) <= BW_LINK_BLOCK_BYTES);
    CHECK(offsetof(BW_LinkGestureCmd, op) == 4 && offsetof(BW_LinkGestureCmd, gesture) == 8 && offsetof(BW_LinkGestureCmd, skill) == 12);
    CHECK(offsetof(BW_LinkGestureCmd, type) == 16 && offsetof(BW_LinkGestureCmd, rgba) == 20 && offsetof(BW_LinkGestureCmd, value) == 24);
    CHECK(offsetof(BW_LinkGestureCmds, ring) == 32 && sizeof(BW_LinkGestureCmds) == 256);
    CHECK(std::wcscmp(BW_LINK_GESTURES_NAME, L"Local\\BodyWalk.Link.Gestures") == 0);
    CHECK(std::wcscmp(BW_LINK_GESTURECMDS_NAME, L"Local\\BodyWalk.Link.GestureCmds") == 0);
    CHECK(BW_LINK_GT_GLYPH == 1 && BW_LINK_GT_POSE == 2 && BW_LINK_GT_MOTION == 3 && BW_LINK_GT_BUTTON == 4);
    CHECK(BW_LINK_GC_RECORD == 1 && BW_LINK_GC_THRESHOLD == 8 && BW_LINK_GESTURECMDS_RING == 8);
}

static wheel::Skill Sk(int32_t id, const char* name, int8_t tree = 1, int32_t owner = -1) {
    wheel::Skill k;
    k.id = id;
    k.owner = owner;
    k.level = 5;
    k.tree = tree;
    k.known = k.available = true;
    std::snprintf(k.name, sizeof k.name, "%s", name);
    return k;
}

static std::unique_ptr<wheel::Snapshot> Sorc() {
    auto s = std::make_unique<wheel::Snapshot>();
    s->ready = s->editable = true;
    s->session = 7;
    s->bank = 0;
    s->playerClass = 1;
    s->treesKnown = true;
    std::snprintf(s->treeName[0], wheel::kNameLen, "Feuer");
    std::snprintf(s->treeName[1], wheel::kNameLen, "Blitz");
    std::snprintf(s->treeName[2], wheel::kNameLen, "Kälte");
    for (wheel::Skill& k : s->slot) { k = wheel::Skill{}; k.known = true; k.id = -1; }
    s->catalog[0] = Sk(wheel::kSkillInteract, "Interact", 0);
    s->catalog[1] = Sk(36, "Fire Bolt", 1);
    s->catalog[2] = Sk(39, "Ice Bolt", 3);
    s->catalog[3] = Sk(54, "Teleport", 2);
    s->catalog[4] = Sk(54, "Teleport", 0, 1234);   // an item's copy of the same skill
    s->catalog[5] = Sk(wheel::kSkillTownPortal, "Town Portal", 0);
    s->count = 6;
    s->slot[1] = Sk(36, "Fire Bolt");     // X
    s->slot[6] = Sk(54, "Teleport");      // LT + Y
    return s;
}

static void TestFill() {
    auto s = Sorc();
    auto b = std::make_unique<BW_LinkSkills>();
    std::memset(b.get(), 0, sizeof *b);
    b->recordSequence = 9;
    b->recordSkill = 39;
    b->revision = 4;
    CHECK(skilllink::Fill(*s, "Tal-Rasha", b.get()));
    CHECK(b->recordSequence == 9 && b->recordSkill == 39 && b->revision == 4);   // the caller's: kept
    CHECK(std::strcmp(b->gameId, "d2r") == 0 && std::strcmp(b->setKey, "sorceress") == 0 && std::strcmp(b->setName, "Sorceress") == 0);
    CHECK(std::strcmp(b->heroName, "Tal-Rasha") == 0);
    CHECK(std::strcmp(b->groupName[1], "Feuer") == 0 && std::strcmp(b->groupName[3], "Kälte") == 0 && b->groupName[0][0]);
    CHECK(std::strcmp(b->slotName[0], "B") == 0 && std::strcmp(b->slotName[4], "LT + B") == 0 && std::strcmp(b->slotName[9], "RT") == 0);
    // Interact (a plain action) left out; Teleport once (the item's copy is the same cast); Town Portal is a skill like any other
    CHECK(b->count == 4);
    CHECK(b->skill[0].id == 36 && b->skill[0].slot == 1 && (b->skill[0].flags & BW_LINK_SKILL_ON_SLOT) && (b->skill[0].flags & BW_LINK_SKILL_LEARNED));
    CHECK(b->skill[1].id == 39 && b->skill[1].slot == -1 && !(b->skill[1].flags & BW_LINK_SKILL_ON_SLOT) && b->skill[1].group == 3);
    CHECK(b->skill[2].id == 54 && b->skill[2].slot == 6 && !(b->skill[2].flags & BW_LINK_SKILL_ITEM));
    CHECK(b->skill[3].id == wheel::kSkillTownPortal && std::strcmp(b->skill[3].name, "Town Portal") == 0 && b->skill[3].level == 5);
    // the same list again is the same; a skill moved to another slot is not
    auto c = std::make_unique<BW_LinkSkills>(*b);
    CHECK(skilllink::Fill(*s, "Tal-Rasha", c.get()) && skilllink::SameList(*b, *c));
    s->slot[2] = Sk(39, "Ice Bolt");
    CHECK(skilllink::Fill(*s, "Tal-Rasha", c.get()) && !skilllink::SameList(*b, *c) && c->skill[1].slot == 2);
    // no hero: an empty list, no set
    s->playerClass = -1;
    CHECK(!skilllink::Fill(*s, nullptr, c.get()) && c->count == 0 && c->setKey[0] == 0);
    // a class past the eight known has no set key
    CHECK(std::strcmp(skilllink::ClassKey(8), "") == 0 && std::strcmp(skilllink::ClassKey(7), "warlock") == 0);
}

static void TestSlotOrder() {
    auto s = Sorc();
    s->slot[4] = Sk(36, "Fire Bolt");   // LT + B as well as X: X (a plain face button) first
    CHECK(skilllink::SlotOf(*s, 36) == 1);
    s->slot[1] = wheel::Skill{};
    CHECK(skilllink::SlotOf(*s, 36) == 4);
    s->slot[9] = Sk(36, "Fire Bolt");   // RT before the left trigger's row
    CHECK(skilllink::SlotOf(*s, 36) == 9);
    CHECK(skilllink::SlotOf(*s, 999) == -1);
    // presses: the button; the right trigger for RT; the left trigger and the longer press for its row
    const skilllink::Press x = skilllink::SlotPress(1), rt = skilllink::SlotPress(9), ltb = skilllink::SlotPress(4), ltrt = skilllink::SlotPress(11);
    CHECK(x.buttons == wheel::kPadX && !x.lt && !x.rt && x.ms == skilllink::kTapMs);
    CHECK(rt.buttons == 0 && rt.rt == 255 && !rt.lt);
    CHECK(ltb.buttons == wheel::kPadB && ltb.lt == 255 && ltb.ms == skilllink::kAltRowMs);
    CHECK(ltrt.buttons == 0 && ltrt.rt == 255 && ltrt.lt == 255);
    CHECK(skilllink::SlotPress(-1).buttons == 0 && skilllink::SlotPress(12).ms == 0);
}

static void TestCastReader() {
    auto c = std::make_unique<BW_LinkCasts>();
    std::memset(c.get(), 0, sizeof *c);
    c->header.writerPid = 100;
    auto put = [&](uint32_t seq, int32_t skill, uint32_t stamp) {
        BW_LinkCast& e = c->ring[seq % BW_LINK_CASTS_RING];
        e.sequence = seq;
        e.skill = skill;
        e.source = BW_LINK_CAST_GESTURE;
        e.stamp = stamp;
        c->lastSequence = seq;
    };
    skilllink::CastReader r;
    BW_LinkCast out[32];
    CHECK(r.Step(*c, 1000, out, 32) == 0);                 // nothing yet
    put(1, 36, 1000);
    CHECK(r.Step(*c, 1100, out, 32) == 1 && out[0].skill == 36);
    CHECK(r.Step(*c, 1200, out, 32) == 0);                 // once
    put(2, 39, 2000);
    put(3, 54, 2010);
    CHECK(r.Step(*c, 2100, out, 32) == 2 && out[0].skill == 39 && out[1].skill == 54);   // oldest first
    put(4, 36, 2100);
    CHECK(r.Step(*c, 2100 + skilllink::kStaleStamp + 1, out, 32) == 0);   // too old: we were not reading
    // a burst past the ring: only the last sixteen are there to read
    for (uint32_t s = 5; s <= 40; ++s) put(s, (int32_t)s, 9000);
    CHECK(r.Step(*c, 9000, out, 32) == (int)BW_LINK_CASTS_RING && out[0].sequence == 25 && out[15].sequence == 40);
    // the stamp's wrap (0.1 ms, 32 bits: every five days)
    put(41, 41, 0xFFFFFF00u);
    CHECK(r.Step(*c, 0x00000010u, out, 32) == 1 && out[0].skill == 41);
    // BodyWalk started again: a new writer counting from 1
    std::memset(c->ring, 0, sizeof c->ring);
    c->header.writerPid = 200;
    put(1, 36, 50000);
    CHECK(r.Step(*c, 50001, out, 32) == 1 && out[0].skill == 36);
    // an entry whose sequence is not the one asked (half written over): skipped
    c->lastSequence = 2;
    CHECK(r.Step(*c, 50002, out, 32) == 0);
}

static void TestCaster() {
    auto s = Sorc();
    skilllink::Caster k;
    // on a slot: pressed at once
    k.Start(36, 1000);
    skilllink::Plan p = k.Step(*s, 1000, skilllink::kDefaultSpareSlot);
    CHECK(p.what == skilllink::Plan::Do::Press && p.slot == 1 && !k.Busy());
    // on no slot: put on the spare slot (LT + RB) through the setter, with what the slot held, then pressed once it shows there
    s->slot[7] = Sk(36, "Fire Bolt");   // the spare slot holds something: the request names it (refused if it changed)
    s->slot[1] = wheel::Skill{};
    s->slot[1].known = true;
    s->slot[1].id = -1;
    s->slot[7].owner = 77;
    k.Start(39, 2000);
    p = k.Step(*s, 2000, 7);
    CHECK(p.what == skilllink::Plan::Do::Assign && p.slot == 7);
    CHECK(p.assign.op == wheel::Op::Assign && p.assign.slot == 7 && p.assign.skill == 39 && p.assign.owner == -1 && p.assign.bank == 0);
    CHECK(p.assign.session == 7 && p.assign.prevSkill == 36 && p.assign.prevOwner == 77);
    CHECK(k.Busy() && k.Step(*s, 2500, 7).what == skilllink::Plan::Do::Nothing);   // waiting for the game
    s->slot[7] = Sk(39, "Ice Bolt");
    // shown: not pressed at once - the game refuses a press right after the set; kSettleMs later it is
    CHECK(k.Step(*s, 2600, 7).what == skilllink::Plan::Do::Nothing && k.Busy());
    CHECK(k.Step(*s, 2600 + skilllink::kSettleMs - 1, 7).what == skilllink::Plan::Do::Nothing);
    p = k.Step(*s, 2600 + skilllink::kSettleMs, 7);
    CHECK(p.what == skilllink::Plan::Do::Press && p.slot == 7 && !k.Busy());
    CHECK(p.settledMs == (int64_t)skilllink::kSettleMs);   // the log says how long it waited
    // a skill that was on a slot already is pressed at once (no settle: nothing was set)
    k.Start(39, 2800);
    p = k.Step(*s, 2800, 7);
    CHECK(p.what == skilllink::Plan::Do::Press && p.slot == 7 && p.settledMs == -1);
    // [gestures] skill_link_settle off (settleMs 0): pressed as soon as the game shows it
    s->slot[7] = Sk(36, "Fire Bolt");
    s->slot[7].owner = 77;
    k.Start(39, 2900);
    CHECK(k.Step(*s, 2900, 7, false, 0).what == skilllink::Plan::Do::Assign);
    s->slot[7] = Sk(39, "Ice Bolt");
    p = k.Step(*s, 2920, 7, false, 0);
    CHECK(p.what == skilllink::Plan::Do::Press && p.slot == 7 && p.settledMs == 0);
    // it never shows: given up after kAssignWaitMs
    s->slot[7] = Sk(36, "Fire Bolt");
    k.Start(39, 3000);
    CHECK(k.Step(*s, 3000, 7).what == skilllink::Plan::Do::Assign);
    CHECK(k.Step(*s, 3000 + skilllink::kAssignWaitMs, 7).what == skilllink::Plan::Do::Nothing);
    CHECK(k.Step(*s, 3001 + skilllink::kAssignWaitMs, 7).what == skilllink::Plan::Do::GiveUp && !k.Busy());
    // the setter said no
    k.Start(39, 4000);
    CHECK(k.Step(*s, 4000, 7).what == skilllink::Plan::Do::Assign);
    CHECK(k.Step(*s, 4010, 7, true).what == skilllink::Plan::Do::GiveUp);
    // no spare slot: only skills on a slot are cast
    k.Start(39, 5000);
    CHECK(k.Step(*s, 5000, -1).what == skilllink::Plan::Do::GiveUp);
    // the game not in its controller layout: its slots cannot be set
    s->editable = false;
    k.Start(39, 6000);
    CHECK(k.Step(*s, 6000, 7).what == skilllink::Plan::Do::GiveUp);
    // a skill the hero does not have (another class's gesture that slipped through): nothing pressed
    s->editable = true;
    k.Start(64, 7000);
    p = k.Step(*s, 7000, 7);
    CHECK(p.what == skilllink::Plan::Do::GiveUp);
    // a plain action (pick up) is never cast this way
    k.Start(wheel::kSkillInteract, 8000);
    CHECK(k.Step(*s, 8000, 7).what == skilllink::Plan::Do::GiveUp);
    // a newer cast replaces one waiting
    k.Start(39, 9000);
    CHECK(k.Step(*s, 9000, 7).what == skilllink::Plan::Do::Assign);
    k.Start(54, 9100);
    p = k.Step(*s, 9100, 7);
    CHECK(p.what == skilllink::Plan::Do::Press && p.slot == 6);
}

static void TestCastWatch() {
    using W = skilllink::CastWatch;
    W w;
    CHECK(w.Step(0, 1) == W::Ev::None);            // not started: nothing
    // standing (1), pressed: the hero goes into the cast (10) 120 ms later - a cast
    w.Start(1000, 1);
    CHECK(w.Step(1050, 1) == W::Ev::None);
    CHECK(w.Step(1120, 10) == W::Ev::Cast && !w.On());
    // standing, nothing happens for kCastSeenMs: no cast (refused, or an aura)
    w.Start(2000, 1);
    CHECK(w.Step(2000 + skilllink::kCastSeenMs, 1) == W::Ev::None);
    CHECK(w.Step(2001 + skilllink::kCastSeenMs, 1) == W::Ev::NoCast);
    // pressed in the middle of an attack (7): still the attack is no cast of ours; the attack ends, then the cast - a cast
    w.Start(3000, 7);
    CHECK(w.Step(3050, 7) == W::Ev::None);
    CHECK(w.Step(3100, 1) == W::Ev::None);
    CHECK(w.Step(3140, 10) == W::Ev::Cast);
    // ... or straight from the attack into another cast mode (a skill's, 13): a cast
    w.Start(4000, 7);
    CHECK(w.Step(4060, 13) == W::Ev::Cast);
    // walking (2) into Leap's sequence (18): a cast; the unit not known (-1) is no cast
    w.Start(5000, 2);
    CHECK(w.Step(5010, -1) == W::Ev::None);
    CHECK(w.Step(5080, 18) == W::Ev::Cast);
    CHECK(skilllink::IsCastMode(10) && skilllink::IsCastMode(18) && !skilllink::IsCastMode(1) && !skilllink::IsCastMode(4) && !skilllink::IsCastMode(-1));
    CHECK(skilllink::kSettleMs < 150 && skilllink::kPressTries == 2);
}

int main() {
    TestLayout();
    TestGestureLayout();
    TestFill();
    TestSlotOrder();
    TestCastReader();
    TestCaster();
    TestCastWatch();
    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
