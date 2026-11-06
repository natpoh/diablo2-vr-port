// Offline checks of vr/quick_blow.h ([gameplay] quick_blow): the AnimData record's events moved to the first frame, whose attack is moved,
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

static void TestShift() {
    uint8_t in[kRecordBytes], out[kRecordBytes];
    // a barbarian's swing: 16 frames, the blow on frame 8
    Record(in, 16, {{8, 1}});
    CHECK(ShiftEvents(in, out) == 8);
    CHECK(out[kEventsAt + 0] == 1);
    for (size_t i = 1; i < kMaxFrames; ++i) CHECK(out[kEventsAt + i] == 0);
    CHECK(std::memcmp(out, in, kEventsAt) == 0);   // the name, the frames, the speed: as they were
    // two events keep their order and their distance; a sound (3) before the blow is moved past (cut off)
    Record(in, 20, {{2, 3}, {7, 1}, {12, 2}});
    CHECK(ShiftEvents(in, out) == 7);
    CHECK(out[kEventsAt + 0] == 1 && out[kEventsAt + 5] == 2);
    int nonzero = 0;
    for (size_t i = 0; i < kMaxFrames; ++i) nonzero += out[kEventsAt + i] != 0;
    CHECK(nonzero == 2);
    // the blow on frame 0 already: nothing moved
    Record(in, 12, {{0, 1}});
    CHECK(ShiftEvents(in, out) == 0 && std::memcmp(in, out, kRecordBytes) == 0);
    // no action: -1, the copy is the record
    Record(in, 12, {{4, 3}, {6, 4}});
    CHECK(ShiftEvents(in, out) == -1 && std::memcmp(in, out, kRecordBytes) == 0);
    // an action past the record's frames is not looked at (the animation never reaches it)
    Record(in, 6, {{9, 1}});
    CHECK(ShiftEvents(in, out) == -1);
    // frames 0 or past 144 (not read right): the whole table is looked at
    Record(in, 0, {{30, 1}});
    CHECK(ShiftEvents(in, out) == 30 && out[kEventsAt] == 1);
    Record(in, 9999, {{143, 2}});
    CHECK(ShiftEvents(in, out) == 143 && out[kEventsAt] == 2);
    for (size_t i = 1; i < kMaxFrames; ++i) CHECK(out[kEventsAt + i] == 0);
}

static void TestJudge() {
    Ask a;
    a.on = a.player = a.own = a.f4 = a.record = true;
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
    TestJudge();
    TestRoute();
    TestFaster();
    TestNotes();
    std::printf("quick_blow_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
