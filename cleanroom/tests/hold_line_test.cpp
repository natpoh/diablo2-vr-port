// hold_line_test.cpp - a game key command (Show items, Alt: cmd 0x25) bound to a long press or a touch line of direct input is held as long as the
// control is (vr/direct_layout.h Layout::Step, vr/hold_tap.h), not sent as a 100 ms pulse; everything else on those lines stays a pulse. Pure: no
// game, no OpenXR. A player: "when setting a button to 'show items on the ground (alt)' it only shows them for a split second then goes away, even if
// you hold the button down".
#include <cstdio>

#include "direct_layout.h"

using namespace directinput::layout;
namespace holdtap = directinput::holdtap;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

constexpr uint32_t kAlt = 0x25;
static bool Alt(const Outputs& o) { return (o.commands[kAlt / 64] >> (kAlt % 64)) & 1; }

static Controls Press(Ctl c, bool down, bool touch = false) {
    Controls k;
    k.v[c] = down ? 1.0f : 0.0f;
    k.touch[c] = touch || down;
    return k;
}

int main() {
    // 1. the plain line (as before): held as long as the button
    {
        Table t = DefaultTable();
        t.plain[kRB] = Bind(OutCmd(kAlt));
        Layout l; l.SetWorld(t);
        bool all = true;
        for (uint64_t ms = 0; ms <= 3000; ms += 11) all &= Alt(l.Step(Mode::World, Press(kRB, true), ms));
        Check(all, "plain line: Alt held for 3 s of a held B");
        Check(!Alt(l.Step(Mode::World, Press(kRB, false), 3011)), "plain line: Alt let go with B");
    }
    // 2. the long press (hold line): Alt from the hold on, until the release - not a 100 ms pulse
    {
        Table t = DefaultTable();
        t.hold[kRB] = Bind(OutCmd(kAlt), OutAction(act::kMap));
        Layout l; l.SetWorld(t);
        bool before = false, after = true, mapLate = false;
        for (uint64_t ms = 0; ms <= 3000; ms += 10) {
            const Outputs o = l.Step(Mode::World, Press(kRB, true), ms);
            if (ms < t.holdMs) before |= Alt(o);
            else after &= Alt(o);
            if (ms >= t.holdMs + holdtap::kPulseMs + 10) mapLate |= (o.actions & act::kMap) != 0;
        }
        Check(!before, "hold line: no Alt before hold_ms");
        Check(after, "hold line: Alt held from hold_ms to 3 s while B is held");
        Check(!mapLate, "hold line: an action next to it is still one pulse");
        Check(!Alt(l.Step(Mode::World, Press(kRB, false), 3010)), "hold line: Alt let go with B");
        Check(!Alt(l.Step(Mode::World, Press(kRB, false), 3020)), "hold line: and stays off");
    }
    // 3. the hold line, then a menu (context change): let go at once, and not again until B is pressed anew
    {
        Table t = DefaultTable();
        t.hold[kRB] = Bind(OutCmd(kAlt));
        Layout l; l.SetWorld(t);
        for (uint64_t ms = 0; ms <= 1000; ms += 10) l.Step(Mode::World, Press(kRB, true), ms);
        Check(!Alt(l.Step(Mode::Menu, Press(kRB, true), 1010)), "hold line: a menu lets Alt go");
        Check(!Alt(l.Step(Mode::World, Press(kRB, true), 1020)), "hold line: back in the world, the old press holds nothing");
        l.Cancel();
        Check(!Alt(l.Step(Mode::World, Press(kRB, true), 1030)), "hold line: Cancel (focus) holds nothing");
    }
    // 4. the touch line: Alt while the finger stays, let go with the finger; an action there is still a pulse
    {
        Table t = DefaultTable();
        t.touch[kRB] = Bind(OutCmd(kAlt), OutAction(act::kMap));
        Layout l; l.SetWorld(t);
        l.Step(Mode::World, Press(kRB, false, false), 0);   // the first frame learns where the fingers are
        bool all = true, mapLate = false;
        for (uint64_t ms = 10; ms <= 2000; ms += 10) {
            const Outputs o = l.Step(Mode::World, Press(kRB, false, true), ms);
            all &= Alt(o);
            if (ms >= 10 + holdtap::kPulseMs) mapLate |= (o.actions & act::kMap) != 0;
        }
        Check(all, "touch line: Alt held while the finger stays");
        Check(!mapLate, "touch line: an action is still one pulse");
        Check(!Alt(l.Step(Mode::World, Press(kRB, false, false), 2010)), "touch line: Alt let go with the finger");
    }
    // 5. a finger already on the control when the frames start is no touch: nothing held
    {
        Table t = DefaultTable();
        t.touch[kRB] = Bind(OutCmd(kAlt));
        Layout l; l.SetWorld(t);
        bool any = false;
        for (uint64_t ms = 0; ms <= 500; ms += 10) any |= Alt(l.Step(Mode::World, Press(kRB, false, true), ms));
        Check(!any, "touch line: a finger there from the start holds nothing");
    }
    // 6. the default left stick click (Alt at once, the map on a tap, the inventory on the long press) is unchanged
    {
        Layout l;
        bool early = true, late = false;
        for (uint64_t ms = 0; ms <= 1000; ms += 10) {
            const Outputs o = l.Step(Mode::World, Press(kLStick, true), ms);
            if (ms < holdtap::kDefaultHoldMs) early &= Alt(o); else late |= Alt(o);
        }
        Check(early && !late, "default left stick click: Alt until the long press, as before");
    }
    std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
