// Offline checks of vr/quick_blow.h ([gameplay] quick_blow): the AnimData record's events moved to the attack's start frame (the game's scheduler model), whose attack is moved,
// and the touch's route. No game, no clock.
// Run: build/Release/quick_blow_test.exe (exit code 0 = passed)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "../../vr/quick_blow.h"

using namespace quickblow;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void Record(uint8_t* r, uint32_t frames, std::initializer_list<std::pair<int, int>> events) {
    std::memset(r, 0, kRecordBytes);
    std::memcpy(r, "BAA1HTH", 8);
    std::memcpy(r + kFramesAt, &frames, sizeof frames);
    const uint32_t speed = 256;
    std::memcpy(r + 0xC, &speed, sizeof speed);
    for (const auto& e : events) r[kEventsAt + e.first] = static_cast<uint8_t>(e.second);
}

// The game's table of attack start frames (0x1D00E20, [weapon class][hero class], read from 3.3.93787's image): the Amazon (0) and the Sorceress (1)
// start bare-handed at 1 and with weapon classes 2 - 6 at 2; the Necromancer, Paladin, Barbarian, Druid, Assassin at 0.
static const int kStartTable[7][8] = {
    {1, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0},
    {2, 2, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0}, {2, 2, 0, 0, 0, 0, 0, 0}};

static void TestShift() {
    uint8_t in[kRecordBytes], out[kRecordBytes];
    // a barbarian's swing (start frame 0): 16 frames, the blow on frame 8 -> frame 0
    Record(in, 16, {{8, 1}});
    CHECK(ShiftEvents(in, out, 0) == 8);
    CHECK(out[kEventsAt + 0] == 1);
    for (size_t i = 1; i < kMaxFrames; ++i) CHECK(out[kEventsAt + i] == 0);
    CHECK(std::memcmp(out, in, kEventsAt) == 0);   // the name, the frames, the speed: as they were
    // Serg's Amazon: AMA11HS / AMA21HS after quick strike - ONE action on frame 2; her sword attack starts at frame 2: on it already, nothing moved
    Record(in, 16, {{2, 1}});
    CHECK(ShiftEvents(in, out, 2) == 0 && std::memcmp(in, out, kRecordBytes) == 0);
    // ... her game's own row (the action on 10) -> frame 2, not below it
    Record(in, 16, {{10, 1}});
    CHECK(ShiftEvents(in, out, 2) == 8 && out[kEventsAt + 2] == 1 && out[kEventsAt + 0] == 0 && out[kEventsAt + 1] == 0);
    // ... a row cut below her start (quick strike's frame 1): moved UP to 2, where the game reads it
    Record(in, 16, {{1, 1}, {6, 3}});
    CHECK(ShiftEvents(in, out, 2) == -1 && out[kEventsAt + 1] == 0 && out[kEventsAt + 2] == 1 && out[kEventsAt + 7] == 3);
    // a dual-wield row (two blows): both kept, their distance kept, the first on the start frame
    Record(in, 16, {{2, 1}, {5, 1}});
    CHECK(ShiftEvents(in, out, 0) == 2 && out[kEventsAt + 0] == 1 && out[kEventsAt + 3] == 1 && out[kEventsAt + 2] == 0);
    // two events keep their order and their distance; a sound (3) before the blow is moved past (cut off)
    Record(in, 20, {{2, 3}, {7, 1}, {12, 2}});
    CHECK(ShiftEvents(in, out, 1) == 6);
    CHECK(out[kEventsAt + 1] == 1 && out[kEventsAt + 6] == 2);
    int nonzero = 0;
    for (size_t i = 0; i < kMaxFrames; ++i) nonzero += out[kEventsAt + i] != 0;
    CHECK(nonzero == 2);
    // the blow on the start frame already: nothing moved
    Record(in, 12, {{0, 1}});
    CHECK(ShiftEvents(in, out, 0) == 0 && std::memcmp(in, out, kRecordBytes) == 0);
    // no action, or the start frame past the record's frames: kNoAction, the copy is the record
    Record(in, 12, {{4, 3}, {6, 4}});
    CHECK(ShiftEvents(in, out, 0) == kNoAction && std::memcmp(in, out, kRecordBytes) == 0);
    Record(in, 6, {{9, 1}});   // an action past the record's frames is not looked at (the animation never reaches it)
    CHECK(ShiftEvents(in, out, 0) == kNoAction);
    Record(in, 6, {{3, 1}});
    CHECK(ShiftEvents(in, out, 6) == kNoAction && std::memcmp(in, out, kRecordBytes) == 0);
    // frames 0 or past 144 (not read right): the whole table is looked at
    Record(in, 0, {{30, 1}});
    CHECK(ShiftEvents(in, out, 2) == 28 && out[kEventsAt + 2] == 1 && out[kEventsAt] == 0);
    Record(in, 9999, {{143, 2}});
    CHECK(ShiftEvents(in, out, 1) == 142 && out[kEventsAt + 1] == 2);
    for (size_t i = 0; i < kMaxFrames; ++i) if (i != 1) CHECK(out[kEventsAt + i] == 0);
}

// The game's scheduler (FirstActionAt, 0x491240 as read): what server frame the first action is queued for, with a NON-ZERO start frame.
static void TestScheduler() {
    uint8_t in[kRecordBytes], out[kRecordBytes];
    // the bug, 0.206.7: the Amazon's sword row (action on 2, start 2) moved to frame 1 - the scheduler starts reading at 2: nothing queued
    // ([debug] quick_blow_probe: "MODECHANGE pending for 0" - no event at all); 0.204 - 0.206.6 moved it to 0: the same
    Record(in, 15, {{1, 1}});
    CHECK(FirstActionAt(in, 2, 256) == 0);
    Record(in, 15, {{0, 1}});
    CHECK(FirstActionAt(in, 2, 256) == 0);
    // quick strike's own frame 2 at start 2: the next server frame
    Record(in, 15, {{2, 1}});
    CHECK(FirstActionAt(in, 2, 256) == 1);
    // the start frame itself is read on the first server frame, at any speed (IAS slows or speeds pos, S is read before pos moves past it)
    for (int32_t speed : {64, 128, 200, 256, 300, 512}) {
        CHECK(FirstActionAt(in, 2, speed) == 1);
        Record(in, 15, {{0, 1}});
        CHECK(FirstActionAt(in, 0, speed) == 1);
        Record(in, 15, {{2, 1}});
    }
    // every class x weapon class of the table, every row shape: one action on frame 4..12, or two (dual wield), or a sound before -
    // the moved copy's first action is queued for the next server frame; the game's own row later; nothing ever below the start
    int rows = 0;
    for (int wc = 0; wc < 7; ++wc)
        for (int cls = 0; cls < 7; ++cls) {
            const uint32_t start = (uint32_t)kStartTable[wc][cls];
            for (int action = 3; action <= 12; ++action)
                for (int shape = 0; shape < 3; ++shape) {
                    if (shape == 0) Record(in, 16, {{action, 1}});
                    else if (shape == 1) Record(in, 16, {{action, 1}, {action + 3, 1}});
                    else Record(in, 16, {{1, 3}, {action, 2}});
                    for (int32_t speed : {128, 256, 384}) {
                        const int game = FirstActionAt(in, start, speed);
                        const int shift = ShiftEvents(in, out, start);
                        CHECK(shift == action - (int)start);
                        CHECK(FirstActionAt(out, start, speed) == 1);
                        CHECK(game >= 1);
                        for (uint32_t f = 0; f < start; ++f) CHECK(!ActionEvent(out[kEventsAt + f]));   // (a sound moved there is never read: harmless)
                        if (shape == 1) CHECK(out[kEventsAt + start + 3] == 1);   // the second blade's blow kept, its distance kept
                        ++rows;
                    }
                }
        }
    CHECK(rows == 7 * 7 * 10 * 3 * 3);
}

static void TestJudge() {
    Ask a;
    a.on = a.player = a.own = a.f4 = a.record = a.start = true;
    a.mode = 7;
    a.weapon = 4;   // a sword
    CHECK(Judge(a) == Why::Shift);
    a.mode = 8;
    CHECK(Judge(a) == Why::Shift);
    for (uint32_t m : {0u, 1u, 2u, 3u, 4u, 9u, 10u, 11u, 12u, 13u, 16u, 18u, 19u}) {
        a.mode = m;
        CHECK(Judge(a) == Why::Mode);
    }
    a.mode = 7;
    for (uint32_t w : {0u, 4u, 5u, 6u, 9u, 10u, 11u, 12u, 13u, 14u, 15u}) {
        a.weapon = w;
        CHECK(Judge(a) == Why::Shift);
    }
    for (uint32_t w : {2u, 3u}) {
        a.weapon = w;
        CHECK(Judge(a) == Why::Weapon);
    }
    a.weapon = 4;
    a.f4 = false;
    CHECK(Judge(a) == Why::View);
    a.own = false;   // a joined hero: the host decides, whatever his view
    CHECK(Judge(a) == Why::Shift);
    a.sequence = true;
    CHECK(Judge(a) == Why::Sequence);
    a.sequence = false;
    a.record = false;
    CHECK(Judge(a) == Why::NoRecord);
    a.record = true;
    a.start = false;   // the start frame not read: the game's own frames
    CHECK(Judge(a) == Why::NoStart);
    a.start = true;
    a.player = false;
    CHECK(Judge(a) == Why::NotHero);
    a.player = true;
    a.on = false;
    CHECK(Judge(a) == Why::Off);
    for (int w = 0; w <= static_cast<int>(Why::Shift); ++w) CHECK(WhyText(static_cast<Why>(w))[0] != 0);
}

static void TestRoute() {
    CHECK(TouchDirect(false, false, false) && TouchDirect(false, true, false) && TouchDirect(false, true, true));   // off: as before
    CHECK(TouchDirect(true, false, false) && TouchDirect(true, false, true));   // Always (2026-11-06): direct, quick blow or not
    CHECK(!TouchDirect(true, true, false));   // By the rules, scope missing, the first monster: the key (0.204)
    CHECK(TouchDirect(true, true, true));     // ... a cleave's further monster: direct
    // melee_direct_scope (Serg 2026-11-06): 0 the two-handed extras only, 1 every touch by the rules - quick blow or not
    CHECK(!TouchDirect(true, true, false, 0) && !TouchDirect(false, true, false, 0) && TouchDirect(true, true, true, 0) && TouchDirect(false, true, true, 0));
    CHECK(TouchDirect(true, true, false, 1) && TouchDirect(false, true, false, 1) && TouchDirect(true, true, true, 1));
    CHECK(TouchDirect(true, false, false, 0) && TouchDirect(true, false, false, 1));   // Always ignores the scope
}

static void TestFaster() {
    FasterAsk a;
    a.on = a.player = a.own = a.f4 = a.target = a.quickAttack = a.blowOut = true;
    a.mode = a.newMode = 7;
    a.original = kGateRefused;
    a.blowFrame = 100;
    a.nowFrame = 105;
    a.endFrame = 120;   // 15 frames left: the game refuses
    a.minMs = 200;      // 5 frames
    CHECK(MinFrames(200) == 5 && MinFrames(0) == 0 && MinFrames(1) == 1 && MinFrames(40) == 1 && MinFrames(41) == 2);
    CHECK(JudgeFaster(a) == FastWhy::Let);
    a.nowFrame = 104;
    CHECK(JudgeFaster(a) == FastWhy::TooSoon);
    a.minMs = 0;
    CHECK(JudgeFaster(a) == FastWhy::Let);
    a.nowFrame = 99;   // before the blow
    CHECK(JudgeFaster(a) == FastWhy::NotOut);
    a.nowFrame = 105;
    a.blowOut = false;
    CHECK(JudgeFaster(a) == FastWhy::NotOut);
    a.blowOut = true;
    // the gate's queue (4) with 2-5 left; its 4 or 1 otherwise is not the animation's: the game's
    a.original = kGateNotYet;
    a.endFrame = 109;
    CHECK(JudgeFaster(a) == FastWhy::Let);
    a.endFrame = 120;
    CHECK(JudgeFaster(a) == FastWhy::Game);
    a.original = kGateRefused;
    a.endFrame = 108;
    CHECK(JudgeFaster(a) == FastWhy::Game);
    a.original = kGateIn;
    CHECK(JudgeFaster(a) == FastWhy::Game);
    a.original = kGateRefused;
    a.endFrame = 120;
    a.target = false;   // the stick's walk: not this
    CHECK(JudgeFaster(a) == FastWhy::NotAttack);
    a.target = true;
    a.newMode = 2;
    CHECK(JudgeFaster(a) == FastWhy::NotAttack);
    a.newMode = 8;
    a.mode = 11;
    CHECK(JudgeFaster(a) == FastWhy::NotAttack);
    a.mode = 8;
    a.quickAttack = false;
    CHECK(JudgeFaster(a) == FastWhy::NotQuick);
    a.quickAttack = true;
    a.f4 = false;
    CHECK(JudgeFaster(a) == FastWhy::View);
    a.own = false;
    CHECK(JudgeFaster(a) == FastWhy::Let);
    a.on = false;
    CHECK(JudgeFaster(a) == FastWhy::Off);
    for (int w = 0; w <= static_cast<int>(FastWhy::Let); ++w) CHECK(FastWhyText(static_cast<FastWhy>(w))[0] != 0);
    CHECK(ClampMinMs(-5) == 0 && ClampMinMs(5000) == 1000 && ClampMinMs(150) == 150);
}

static void TestNotes() {
    QuickNotes n;
    CHECK(!n.Is(1, 50));
    n.Note(1, 50);
    CHECK(n.Is(1, 50) && !n.Is(1, 51) && !n.Is(2, 50));
    n.Note(1, 70);   // his next attack
    CHECK(n.Is(1, 70) && !n.Is(1, 50));
    for (uint32_t h = 2; h <= 9; ++h) n.Note(h, 10 * (int32_t)h);   // eight more heroes: the oldest (hero 1) gives his place
    CHECK(!n.Is(1, 70) && n.Is(9, 90) && n.Is(2, 20));
}

int main() {
    TestShift();
    TestScheduler();
    TestJudge();
    TestRoute();
    TestFaster();
    TestNotes();
    std::printf("quick_blow_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
