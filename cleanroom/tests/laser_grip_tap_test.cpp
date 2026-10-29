// Offline checks of vr/laser_grip_tap.h ([openxr] laser_grip_tap): a grip the laser took over a game menu, tapped, is the game's own press - the
// bumper that turns the tabbed menu's page (a player's report, 0.182: the Options tab out of the laser's reach). Held, or used with a click: the
// laser's, as before. No OpenXR, no headset. Run: laser_grip_tap_test.exe (exit 0 = all passed).
#include <cstdio>

#include "../../vr/laser_grip_tap.h"
#include "../../vr/menu_zoom.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using lasergrip::In;
using lasergrip::Tap;

static In Frame(uint64_t ms, bool grip, bool click = false, bool claimed = true, bool allowed = true, bool spent = false) {
    In in;
    in.allowed = allowed;
    in.claimed = claimed;
    in.grip = grip;
    in.click = click;
    in.spent = spent;
    in.nowMs = ms;
    return in;
}

// A hold from `from` to `to` ms, frames every 11 ms (90 Hz); returns whether any frame up to `to` + 300 ms gave the press, and how long.
static uint64_t Pulsed(Tap& t, uint64_t from, uint64_t to, bool clickMid = false, bool claimed = true, bool allowed = true, bool spentMid = false) {
    uint64_t on = 0;
    for (uint64_t ms = from; ms < to + 300; ms += 11) {
        const bool grip = ms < to;
        const bool mid = grip && ms >= from + (to - from) / 2 && ms < from + (to - from) / 2 + 30;
        if (t.Step(Frame(ms, grip, clickMid && mid, claimed, allowed, spentMid && mid))) on += 11;
    }
    return on;
}

static void TestTap() {
    Tap t;
    const uint64_t on = Pulsed(t, 1000, 1150);
    CHECK(on > 0);                                     // a quick squeeze: the page turns
    CHECK(on >= lasergrip::kPulseMs - 11 && on <= lasergrip::kPulseMs + 11);   // for about the pulse, no longer
    CHECK(lasergrip::kPulseMs == menuzoom::kGripTapMs);   // the same press the menu zoom gives a grip let go
}

static void TestNotWhilePressed() {
    // nothing goes to the game while the grip is down (the laser has it: it may be a right click's or Shift's)
    Tap t;
    for (uint64_t ms = 0; ms < 200; ms += 11) CHECK(!t.Step(Frame(ms, true)));
}

static void TestHold() {
    Tap t;
    CHECK(Pulsed(t, 0, lasergrip::kTapMaxMs + 50) == 0);   // held past the tap: a modifier, no page
    CHECK(Pulsed(t, 5000, 5000 + lasergrip::kTapMaxMs - 20) > 0);   // just inside: a tap
}

static void TestClick() {
    Tap t;
    CHECK(Pulsed(t, 0, 300, true) == 0);   // grip + trigger: the right click (or Shift + click) - no page
    // a trigger already down when the grip comes: not a tap either
    Tap u;
    u.Step(Frame(0, false, true));
    u.Step(Frame(11, true, true));
    u.Step(Frame(22, true, false));
    bool any = false;
    for (uint64_t ms = 33; ms < 300; ms += 11) any = u.Step(Frame(ms, false)) || any;
    CHECK(!any);
}

static void TestNotClaimed() {
    Tap t;
    CHECK(Pulsed(t, 0, 150, false, false) == 0);   // the laser did not take the grip: it went to the game itself, nothing to add
    Tap u;
    CHECK(Pulsed(u, 0, 150, false, true, false) == 0);   // the box off / not F4 / no menu: as before
    Tap v;
    CHECK(Pulsed(v, 0, 300, false, true, true, true) == 0);   // the zoom moved the panel in this hold: the zoom's
}

static void TestClaimLostMidHold() {
    // the laser gave the grip back mid-hold (the pad took over): the game had the rest of the press, no second one
    Tap t;
    t.Step(Frame(0, true));
    t.Step(Frame(11, true, false, false));
    bool any = false;
    for (uint64_t ms = 22; ms < 300; ms += 11) any = t.Step(Frame(ms, false)) || any;
    CHECK(!any);
}

static void TestMenuGoesMidPulse() {
    Tap t;
    t.Step(Frame(0, true));
    CHECK(t.Step(Frame(100, false)));               // let go: the press at once
    CHECK(t.Step(Frame(111, false)));               // pulsing
    CHECK(!t.Step(Frame(122, false, false, true, false)));   // the menu closed: the press ends at once
    CHECK(!t.Step(Frame(133, false)));
}

static void TestTwoTaps() {
    Tap t;
    CHECK(Pulsed(t, 0, 120) > 0);
    CHECK(Pulsed(t, 500, 620) > 0);   // the next tap, the next page
}

int main() {
    TestTap();
    TestNotWhilePressed();
    TestHold();
    TestClick();
    TestNotClaimed();
    TestClaimLostMidHold();
    TestMenuGoesMidPulse();
    TestTwoTaps();
    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
