// Offline checks of hit by touch with a thrown weapon (vr/thrown_touch.h): what keeps the touch and the throw by gesture from both firing for one swing.
// No game, no headset. Run: build/Release/thrown_touch_test.exe   (exit code 0 = all passed)

#include "../../vr/blade_shape.h"
#include "../../vr/thrown_touch.h"
#include "../../vr/touch_why.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace meleetouch;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// Why a touch is held off: the reason is the one the log tells (vr/touch_why.h).
static void WhyNotTests() {
    ThrownState s;
    s.nowMs = 10000;
    Expect(ThrownWhyNot(s) == ThrownWhy::None, "why not: nothing in the way");
    s.gripOwns = true;
    s.gripHeld = false;
    Expect(ThrownWhyNot(s) == ThrownWhy::GripOpen, "why not: the grip gesture on, the hand open");
    s.gripHeld = true;
    Expect(ThrownWhyNot(s) == ThrownWhy::None, "why not: the grip squeezed - nothing");
    s.lastThrowMs = 9800;
    Expect(ThrownWhyNot(s) == ThrownWhy::AfterThrow, "why not: 200 ms after a released throw");
    s.gripHeld = false;
    Expect(ThrownWhyNot(s) == ThrownWhy::AfterThrow, "why not: the follow-through is named first");
    // the grip's value is the gesture's: a gripOwns frame with the grip squeezed must never be refused because of a shared block that reads 0 - the
    // state carries only what throwwire::GripHeld says, so a caller that asks the shared block's grip is the bug this guards against (it was one)
    s = ThrownState{};
    s.gripOwns = true;
    s.nowMs = 5;
    Expect(s.gripHeld && ThrownTouchAllowed(s), "why not: the state starts as held - a caller that has no reading does not refuse");
    // [gameplay] melee_touch_thrown_held: in the hand from the equip, like a sword - the open grip refuses nothing, the follow-through still does
    s = ThrownState{};
    s.nowMs = 10000;
    s.gripOwns = true;
    s.gripHeld = false;
    s.heldAtOnce = true;
    Expect(ThrownWhyNot(s) == ThrownWhy::None, "held at once: an open grip is no reason");
    s.lastThrowMs = 9800;
    Expect(ThrownWhyNot(s) == ThrownWhy::AfterThrow, "held at once: 200 ms after a released throw still stands down");
    s.lastThrowMs = 9000;
    Expect(ThrownTouchAllowed(s), "held at once: 1 s after a throw it touches again");
    s.heldAtOnce = false;
    Expect(ThrownWhyNot(s) == ThrownWhy::GripOpen, "unticked: the open grip refuses, as ever");
}

static void WhyToldTests() {
    WhyTold t;
    const uint32_t jav = WeaponKey(D2RVR_TYPE_JAVELIN, bladeshape::Code('j', 'a', 'v', ' '), bladeshape::Code('b', 'u', 'c', ' '));
    const uint32_t staff = WeaponKey(D2RVR_TYPE_STAFF, bladeshape::Code('s', 's', 't', ' '), 0);
    Expect(jav != staff, "told: two weapons have two keys");
    Expect(!t.First(jav, Why::None), "told: nothing to say is not said");
    Expect(t.First(jav, Why::GripOpen), "told: the first time for a weapon, a reason is said");
    Expect(!t.First(jav, Why::GripOpen), "told: ... once");
    Expect(t.First(jav, Why::AfterThrow) && !t.First(jav, Why::AfterThrow), "told: another reason for the same weapon is said once too");
    Expect(t.First(staff, Why::GripOpen), "told: a new weapon starts afresh");
    Expect(t.First(jav, Why::GripOpen), "told: and the first weapon again, when it is taken back, says its reasons again");
    Expect(!t.First(jav, Why::kCount), "told: a number that is no reason is not said");
    t.Reset();
    Expect(t.First(jav, Why::NoBlade), "told: reset");
    // every reason has a text of its own, none empty or the placeholder
    for (unsigned i = 1; i < static_cast<unsigned>(Why::kCount); ++i) {
        const char* text = WhyText(static_cast<Why>(i));
        Expect(text && text[0] && text[0] != '?', "told: every reason has a text", i);
        for (unsigned k = i + 1; k < static_cast<unsigned>(Why::kCount); ++k) Expect(std::strcmp(text, WhyText(static_cast<Why>(k))) != 0, "told: the texts differ", k);
    }
}

static void AllowedTests() {
    ThrownState s;
    s.nowMs = 10000;
    Expect(ThrownTouchAllowed(s), "allowed: nothing thrown lately, the grip gesture not on - the touch may fire");
    s.gripOwns = true;
    s.gripHeld = true;
    Expect(ThrownTouchAllowed(s), "allowed: the grip gesture on and the grip squeezed on the weapon - the touch may fire (a stab)");
    s.gripHeld = false;
    Expect(!ThrownTouchAllowed(s), "allowed: the grip gesture on, the hand open - the weapon is not 'in the hand': no touch");
    s.gripOwns = false;
    Expect(ThrownTouchAllowed(s), "allowed: the grip gesture off (the trigger's gesture or none): the grip's state does not matter");
    // after a released throw: 0.7 s of stand-down
    s = ThrownState{};
    s.lastThrowMs = 5000;
    s.nowMs = 5000;
    Expect(!ThrownTouchAllowed(s), "after a throw: not in the frame of the release");
    s.nowMs = 5000 + kAfterThrowMs - 1;
    Expect(!ThrownTouchAllowed(s), "after a throw: not just before 0.7 s");
    s.nowMs = 5000 + kAfterThrowMs;
    Expect(ThrownTouchAllowed(s), "after a throw: from 0.7 s on");
    s.nowMs = 4000;   // the clock went back (it cannot, but a wrap or a bad stamp must not lock the touch)
    Expect(ThrownTouchAllowed(s), "after a throw: a stamp in the future locks nothing");
    s.lastThrowMs = 0;
    s.nowMs = 10;
    Expect(ThrownTouchAllowed(s), "after a throw: none yet (0) - nothing held off");
    // both conditions at once
    s = ThrownState{};
    s.gripOwns = true;
    s.gripHeld = true;
    s.lastThrowMs = 100;
    s.nowMs = 200;
    Expect(!ThrownTouchAllowed(s), "both: the squeezed grip does not override the follow-through of a throw");
    Expect(kAfterThrowMs >= 500 + 70, "the stand-down covers the throw's press and the ticks the game takes to read it (70 + 450 ms)");
}

static void HoldOffTests() {
    Expect(HoldOffUntil(1000) == 1000 + kHoldOffMs, "hold-off: from the touch's press for kHoldOffMs");
    Expect(kHoldOffMs >= 500, "hold-off: long enough for the release at the end of the swing that touched");
    // the two windows do not trap each other: a touch, its hold-off over, then a throw released, then the stand-down - never both at once for one swing
    const uint64_t touchAt = 1000;
    const uint64_t until = HoldOffUntil(touchAt);
    ThrownState s;
    s.nowMs = until;
    s.lastThrowMs = 0;
    Expect(ThrownTouchAllowed(s), "hold-off: the touch itself is not held off by its own hold-off");
}

static void PotionsAreNotSwungTests() {
    // the plumbing the touch uses (melee_touch.cpp): the weapon's kind and item code decide whether a thrown-kind item is swung at all
    const uint32_t rancid = bladeshape::Code('g', 'p', 's', ' ');
    Expect(!bladeshape::TouchKind(D2RVR_TYPE_THROWING, rancid, true), "potions: a throwing potion is not swung, the switch on");
    Expect(bladeshape::TouchKind(D2RVR_TYPE_THROWING, bladeshape::Code('9', 't', 'k', ' '), true), "potions: a battle dart is");
    Expect(bladeshape::TouchKind(D2RVR_TYPE_JAVELIN, bladeshape::Code('7', 'j', 'a', ' '), true), "potions: a hyperion javelin is");
}

int main() {
    WhyNotTests();
    WhyToldTests();
    AllowedTests();
    HoldOffTests();
    PotionsAreNotSwungTests();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("thrown_touch_test: all passed\n");
    return 0;
}
