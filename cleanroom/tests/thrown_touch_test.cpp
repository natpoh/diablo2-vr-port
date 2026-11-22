// Offline checks of hit by touch with a thrown weapon (vr/thrown_touch.h): what keeps the touch and the throw by gesture from both firing for one swing.
// No game, no headset. Run: build/Release/thrown_touch_test.exe   (exit code 0 = all passed)

#include "../../vr/blade_shape.h"
#include "../../vr/quiver.h"
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

// [gameplay] melee_touch_thrown_melee: the touch's own press with a thrown weapon is read as the plain Attack (the game's one-handed blow) whenever the skill it
// would carry throws the weapon; a blow skill, every other read, and the throw by gesture keep theirs.
static void MeleeAnswerTests() {
    MeleeAsk a;
    a.on = true;
    a.touchPress = true;
    a.atCastSite = true;
    a.attackSlot = true;
    a.thrownInHand = true;
    // the throwing skills: Throw on A, Left Hand Throw, the quiver's ranged javelin skills, Double Throw - read as Attack
    const int32_t throws[] = {2, 4, 15, 20, 25, 35, 140};
    for (const int32_t s : throws) {
        Expect(ThrowsTheWeapon(s), "melee: a throwing skill is known as one", s);
        Expect(MeleeAnswer(s, a) == kSkillAttack, "melee: a throwing skill on the touch's press is read as the plain Attack", s);
    }
    // the plain Attack stays (it is already the blow); the blow skills stay what they are (Jab, Power Strike, Impale, Charged Strike, Fend, Lightning
    // Strike, Poison Dagger, Bash, Zeal); an empty slot (-1) and a command (Interact 357) are not this switch's
    const int32_t blows[] = {0, 10, 14, 19, 24, 30, 34, 73, 126, 106, -1, 357, 1, 3, 5};
    for (const int32_t s : blows) {
        Expect(!ThrowsTheWeapon(s), "melee: a blow or another skill is no throw", s);
        Expect(MeleeAnswer(s, a) == s, "melee: a blow skill or another binding stays", s);
    }
    // the switch's own gates - each one off leaves the answer as it was (a Throw on A stays a Throw)
    MeleeAsk g = a;
    g.on = false;
    Expect(MeleeAnswer(2, g) == 2, "melee: unticked - the touch casts what A holds, as before");
    g = a;
    g.touchPress = false;
    Expect(MeleeAnswer(2, g) == 2, "melee: the player's own A press (no touch pending) keeps its Throw");
    g = a;
    g.atCastSite = false;
    Expect(MeleeAnswer(2, g) == 2, "melee: any other read of the slot (the interface drawing it) is answered as it is");
    g = a;
    g.attackSlot = false;
    Expect(MeleeAnswer(35, g) == 35, "melee: another slot (B X Y RB) keeps its skill");
    g = a;
    g.thrownInHand = false;
    Expect(MeleeAnswer(2, g) == 2, "melee: no thrown weapon in hand - nothing to do (a potion is never swung)");
    g = a;
    g.throwLive = true;
    Expect(MeleeAnswer(2, g) == 2, "melee: a throw by gesture is live - its press keeps the Throw");
    Expect(MeleeAnswer(35, g) == 35, "melee: ... and the quiver's Lightning Fury");
    // the list agrees with the javelin quiver's table: every ranged skill a thrown weapon can use is a throw here, every blow is not (vr/quiver.h kRules)
    for (const quiver::Rule& r : quiver::kRules) Expect(ThrowsTheWeapon(r.id) == r.ranged, "melee: the quiver's ranged flag and ThrowsTheWeapon agree", r.id);
    Expect(ThrowsTheWeapon(quiver::kSkillThrow), "melee: the quiver's Throw is a throw");
}

// Serg, 2026-11-21: «то должна магия срабатывать» - a skill on the button the touch presses fires as it is; only the throws become the one-handed blow.
// Each row is the game's own skills.txt of 3.3: range h2h (a blow in reach - kept) or rng (a cast at a spot that throws the weapon - the blow instead).
static void SkillOnTheButtonTests() {
    MeleeAsk a;
    a.on = a.touchPress = a.atCastSite = a.attackSlot = a.thrownInHand = true;
    struct Row { int32_t id; const char* name; bool range_h2h; };
    static const Row rows[] = {
        {10, "Jab (h2h, SQ)", true},
        {14, "Power Strike (h2h, A1)", true},
        {19, "Impale (h2h, SQ)", true},
        {24, "Charged Strike (h2h, A1; its bolts come out of the blow)", true},
        {30, "Fend (h2h, A1)", true},
        {34, "Lightning Strike (h2h, A1; its chain comes out of the blow)", true},
        {73, "Poison Dagger (h2h, a throwing knife)", true},
        {2, "Throw (rng, TH)", false},
        {4, "Left Hand Throw (rng)", false},
        {15, "Poison Javelin (rng, TH)", false},
        {20, "Lightning Bolt (rng, TH)", false},
        {25, "Plague Javelin (rng, TH)", false},
        {35, "Lightning Fury (rng, TH)", false},
        {140, "Double Throw (rng)", false},
    };
    for (const Row& r : rows) {
        const int32_t got = MeleeAnswer(r.id, a);
        if (r.range_h2h) {
            if (got != r.id) std::printf("  %s\n", r.name);
            Expect(got == r.id, "button: a melee skill on the touch's button fires as it is", r.id);
        } else {
            if (got != kSkillAttack) std::printf("  %s\n", r.name);
            Expect(got == kSkillAttack, "button: a throw skill on the touch's button becomes the plain Attack (the one-handed blow)", r.id);
        }
    }
    // a spell or skill that is no throw (a Sorceress's Fire Bolt 36, Teleport 54, a Barbarian's Bash 126, a Paladin's Zeal 106) stays: the game casts it at
    // the monster the blade touched, as with the switch off
    const int32_t others[] = {36, 54, 126, 106};
    for (const int32_t s : others) Expect(MeleeAnswer(s, a) == s, "button: another skill or spell fires as it is", s);
    // the switch off: every skill on the button, the throws included, is what the touch casts (as before)
    MeleeAsk off = a;
    off.on = false;
    for (const Row& r : rows) Expect(MeleeAnswer(r.id, off) == r.id, "button: unticked - the touch casts what the button holds", r.id);
}

int main() {
    WhyNotTests();
    SkillOnTheButtonTests();
    WhyToldTests();
    AllowedTests();
    HoldOffTests();
    PotionsAreNotSwungTests();
    MeleeAnswerTests();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("thrown_touch_test: all passed\n");
    return 0;
}
