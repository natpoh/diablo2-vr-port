// Offline checks of vr/pad_presence.h: when pad 0 must be answered present, and what the log says about it.
// Run: build/Release/pad_presence_test.exe (exit code 0 = passed)

#include <cstdio>

#include "../../vr/pad_presence.h"

using namespace padpresence;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestMustStay() {
    CHECK(!MustStayPresent(false, true, true));     // a flat game: never touched
    CHECK(!MustStayPresent(true, false, false));    // VR, the plugin never was the pad: the real XInput's word stands
    CHECK(MustStayPresent(true, true, false));      // direct input on: pad 0 is there from the first poll
    CHECK(MustStayPresent(true, false, true));      // BodyWalk's (or the plugin's) pad was given once: it does not vanish
    CHECK(MustStayPresent(true, true, true));
}

static void TestTracker() {
    Tracker t;
    Source was = Source::Real;
    CHECK(t.Step(Source::Direct, &was) && was == Source::Absent);   // the first answer is always news
    CHECK(!t.Step(Source::Direct, &was) && was == Source::Direct);
    // the stall of 15:06: the plugin's pad goes stale, a neutral one is kept, then the plugin's comes back
    CHECK(t.Step(Source::Kept, &was) && was == Source::Direct);
    CHECK(!t.Step(Source::Kept, &was));
    CHECK(t.Step(Source::Direct, &was) && was == Source::Kept);
    // the fault, should it ever happen again
    CHECK(t.Step(Source::Absent, &was) && was == Source::Direct);
}

static void TestSaid() {
    Said s;
    uint32_t said = 0;
    for (int i = 0; i < 1000; ++i) if (s.Next()) ++said;
    CHECK(s.Count() == 1000);
    CHECK(said == Said::kHead + (1000 / Said::kEvery - Said::kHead / Said::kEvery));   // the first 80, then every 50th
    CHECK(said < 100);
}

static void TestNeutralPacket() {
    NeutralPacket p;
    const uint32_t a = p.Next(), b = p.Next(), c = p.Next();
    CHECK(a != b && b != c && b == a + 1 && c == b + 1);
}

// The poll's decision as vrcam's XDetour makes it, on the cases of the log and the ones around it.
struct Poll {
    bool vr, directOn, seen;
    bool realOk, pluginPad;   // what the real XInput says, and whether the plugin has a pad of its own this poll
};
static Source Decide(const Poll& p, bool* answeredOk) {
    bool ok = p.realOk || p.pluginPad;
    Source src = p.pluginPad ? Source::Direct : p.realOk ? Source::Real : Source::Absent;
    if (!ok && MustStayPresent(p.vr, p.directOn, p.seen)) { ok = true; src = Source::Kept; }
    *answeredOk = ok;
    return src;
}

static void TestDecisions() {
    bool ok = false;
    // the plugin's pad is there
    CHECK(Decide({true, true, true, false, true}, &ok) == Source::Direct && ok);
    // the stall: the plugin's pad is stale, the real XInput has no pad - the game must still see one
    CHECK(Decide({true, true, true, false, false}, &ok) == Source::Kept && ok);
    // the session ended and nothing has started again; direct input still on
    CHECK(Decide({true, true, false, false, false}, &ok) == Source::Kept && ok);
    // BodyWalk's pad stalled for a second (it had been given), nothing real plugged in
    CHECK(Decide({true, false, true, false, false}, &ok) == Source::Kept && ok);
    // VR with no pad of the plugin's at all: untouched, as ever
    CHECK(Decide({true, false, false, false, false}, &ok) == Source::Absent && !ok);
    // a real pad plugged in is the real pad
    CHECK(Decide({true, true, true, true, false}, &ok) == Source::Real && ok);
    // flat: untouched
    CHECK(Decide({false, true, true, false, false}, &ok) == Source::Absent && !ok);
}

// [input] flat_pad_on_touch: a pad at rest (or drifting) is not touched; a button, a trigger or a stick pushed is.
static void TestRealPadTouched() {
    CHECK(!RealPadTouched(0, 0, 0, 0, 0, 0, 0));
    CHECK(!RealPadTouched(0, 20, 10, 6000, -7000, 3000, -11000));   // a worn pad's drift, triggers resting
    CHECK(RealPadTouched(0x1000, 0, 0, 0, 0, 0, 0));                // A
    CHECK(RealPadTouched(0, 200, 0, 0, 0, 0, 0));                   // the left trigger
    CHECK(RealPadTouched(0, 0, 0, 0, 30000, 0, 0));                 // the left stick up
    CHECK(RealPadTouched(0, 0, 0, 0, 0, -20000, 0));                // the right stick left
}

int main() {
    TestRealPadTouched();
    TestMustStay();
    TestTracker();
    TestSaid();
    TestNeutralPacket();
    TestDecisions();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("pad_presence_test: all %d checks passed\n", g_checks);
    return 0;
}
