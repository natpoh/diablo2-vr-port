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

// Whose report is kept (the right stick the turn reads): Serg's 0.190.2 F2 run - the plugin's controller merged into a real pad's report at
// pad 0 (mirror stale), pad 1 answering with an empty report a millisecond later, every 16 ms frame. The turn's 1 ms tick must see the
// stick pushed nearly all the time - with the old rule (any pad kept once the mirror was a second old) it saw it 1 ms in 16.
static void TestKeepsReport() {
    // the four simple cases
    CHECK(KeepsReport(true, 0, 5000, 4990, 4990));          // the plugin's own pad: kept
    CHECK(!KeepsReport(false, 1, 5000, 4990, 4990));        // the plugin's pad is live: another pad's report is not
    CHECK(KeepsReport(false, 0, 5000, 3000, 4999));         // F1-F3 with a real pad: pad 0's merged report is kept
    CHECK(!KeepsReport(false, 1, 5000, 3000, 4999));        // ... and pad 1's empty one is not (the bug)
    CHECK(KeepsReport(false, 1, 5000, 0, 0));               // a pad at index 1 alone (no pad 0 ever): kept, as before
    CHECK(KeepsReport(false, 1, 5000, 0, 4000));            // pad 0 gone for a second: the other pad's report counts again
    CHECK(KeepsReport(false, 0, 5000, 0, 0));               // flat, a real pad at 0, never a mirror: kept
    // the frames: pad 0 at t, pad 1 at t + 1; the turn reads the kept stick every millisecond
    auto run = [](bool oldRule) {
        float rightX = 0.0f;
        uint64_t pad0At = 0;
        const uint64_t mirrorOkAt = 1000;   // the plugin's pad last lived in F4, then F2
        int pushed = 0, ticks = 0;
        for (uint64_t now = 3000; now < 4000; ++now) {
            const uint64_t f = now % 16;
            if (f == 0 || f == 1) {
                const uint32_t pad = f == 0 ? 0u : 1u;
                const float rx = pad == 0 ? 0.9f : 0.0f;   // the merged controller's stick at pad 0, the real pad 1 at rest
                if (pad == 0) pad0At = now;
                const bool keep = oldRule ? (now - mirrorOkAt > 1000) : KeepsReport(false, pad, now, mirrorOkAt, pad0At);
                if (keep) rightX = rx;
            }
            ++ticks;
            if (rightX > 0.5f) ++pushed;
        }
        return (float)pushed / ticks;
    };
    const float before = run(true), after = run(false);
    CHECK(before < 0.1f);    // the bug, reproduced: the stick seen ~6 % of the time
    CHECK(after > 0.99f);    // fixed: all of it
}

int main() {
    TestKeepsReport();
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
