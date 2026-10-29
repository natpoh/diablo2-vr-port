// [belt] hold_takes_trigger: the triggers while a belt bottle is in a hand (vr/belt_trigger.h, pure, and its wiring vr/belt_trigger_wire.cpp), with stand-ins for
// direct_input's Listen / Take. No game, no headset.
//
//   belt_trigger_test
//
// The bug this guards (Serg's test of test-54, 2026-10-17): a potion taken with trigger + grip and brought to the mouth was not drunk, the grip alone drank. The game reads a belt key
// with its "shift" (vr/belt_trigger.h: the controller-mode shift is the pad's LEFT trigger, the code 0x400), and the layout passed the squeezed left trigger on as LT - so the drink
// was a Shift + belt key. Checked here: whichever hand holds a bottle, the left trigger is taken from the layout (the pad then carries no LT); a right hand's bottle takes its own
// trigger (no attack) too; a left hand's bottle leaves the right trigger the layout's; nothing is taken with nothing in a hand, with the key off, in a menu / panel / another view /
// without focus, or when the belt's word is stale.

#include "../../vr/belt_trigger.h"
#include "../../vr/belt_trigger_wire.h"
#include "../../vr/direct_input.h"
#include "../../vr/view_gate.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

// ---- the stand-ins ----
namespace directinput {
FrameFn g_fn = nullptr;
int g_takes = 0;
int g_takeHand[4] = {-1, -1, -1, -1};
uint32_t g_takeMask[4] = {};
Source g_takeSrc = kSrcLayout;
bool Listen(FrameFn fn, void*) { g_fn = fn; return true; }
void Take(int hand, uint32_t buttons, Source src) {
    if (g_takes < 4) { g_takeHand[g_takes] = hand; g_takeMask[g_takes] = buttons; }
    ++g_takes;
    g_takeSrc = src;
}
}   // namespace directinput

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
        }                                                      \
    } while (0)

bool g_f4 = true;
directinput::Frame g_frame{};

// a headset frame with the left / right trigger squeezed (direct_input's numbering: 0 left, 1 right); the listener runs, its takes are counted
void NewFrame(uint32_t flags, float leftTrigger, float rightTrigger) {
    using namespace directinput;
    g_frame.version = kVersion;
    ++g_frame.counter;
    g_frame.flags = flags;
    g_frame.hand[0].trigger = leftTrigger;
    g_frame.hand[1].trigger = rightTrigger;
    g_takes = 0;
    g_takeHand[0] = g_takeHand[1] = -1;
    g_fn(g_frame, nullptr);
}

bool Took(int hand) {
    for (int i = 0; i < directinput::g_takes && i < 4; ++i)
        if (directinput::g_takeHand[i] == hand &&
            (directinput::g_takeMask[i] & (directinput::kTrigger | directinput::kTakeTrigger)) == (directinput::kTrigger | directinput::kTakeTrigger))
            return true;
    return false;
}

}  // namespace

int main() {
    using namespace directinput;

    // ---- the decision (pure) ----
    {
        belttrigger::In in;
        auto o = belttrigger::Decide(in);
        EXPECT(!o.left && !o.right);                       // nothing in a hand: nothing taken

        in.holds[1] = true;                                // the left hand holds a bottle (the bug's case: trigger + grip with the left hand)
        o = belttrigger::Decide(in);
        EXPECT(o.left && !o.right);                        // the left trigger (the game's shift) is taken; the right one stays the layout's (the right hand may still attack)

        in = belttrigger::In{};
        in.holds[0] = true;                                // the right hand holds one
        o = belttrigger::Decide(in);
        EXPECT(o.left && o.right);                         // the right trigger is the pad's A: not an attack; and the left one is still the shift

        in.holds[1] = true;                                // both hands
        o = belttrigger::Decide(in);
        EXPECT(o.left && o.right);

        in.enabled = false;                                // [belt] hold_takes_trigger=0: as before, whatever is held
        o = belttrigger::Decide(in);
        EXPECT(!o.left && !o.right);
    }

    // ---- the wiring ----
    viewgate::Set([]() { return g_f4; });
    belttrigger::Init(nullptr);
    EXPECT(g_fn != nullptr);

    // no word from the belt yet: nothing taken, the left trigger passes
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);

    // the belt says: the left hand holds a bottle (taken with trigger + grip); the left trigger is squeezed - it is taken, as direct_input's hand 0
    belttrigger::Publish(true, false, true);
    NewFrame(kFocused, 1.0f, 0.0f);
    EXPECT(g_takes == 1 && Took(0) && !Took(1));
    EXPECT(g_takeSrc == kSrcOther3);
    NewFrame(kFocused, 0.0f, 0.0f);                        // the trigger need not be squeezed yet: it is taken every frame the bottle is in the hand
    EXPECT(g_takes == 1 && Took(0));

    // the right hand holds one: both triggers taken (hand 0 = the shift, hand 1 = the attack)
    belttrigger::Publish(true, true, false);
    NewFrame(kFocused, 0.0f, 1.0f);
    EXPECT(g_takes == 2 && Took(0) && Took(1));

    // both hands
    belttrigger::Publish(true, true, true);
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 2 && Took(0) && Took(1));

    // the key off: nothing taken, the triggers are the layout's as before the fix
    belttrigger::Publish(false, true, true);
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);

    // a menu, a panel of ours, the lost focus, another view, the frame's other-view flag: nothing taken
    belttrigger::Publish(true, true, true);
    NewFrame(kFocused | kInMenu, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    NewFrame(kFocused | kUiUp, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    NewFrame(0, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    NewFrame(kFocused | kOtherView, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    g_f4 = false;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    g_f4 = true;
    belttrigger::Publish(true, true, true);
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 2);                                   // back in F4: taken again, at once

    // the bottle is put back / drunk: the belt's word says nothing is held - nothing taken (the layout's release gate keeps a trigger still squeezed masked until it is let go)
    belttrigger::Publish(true, false, false);
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);

    // the belt stops talking (BeltFrame not called: another view, the belt off): its last word is not "now" after 300 ms
    belttrigger::Publish(true, false, true);
    NewFrame(kFocused, 1.0f, 0.0f);
    EXPECT(g_takes == 1);
    Sleep(400);
    NewFrame(kFocused, 1.0f, 0.0f);
    EXPECT(g_takes == 0);

    if (g_fails) {
        std::printf("belt_trigger_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("belt_trigger_test: the decision (left hand, right hand, both, key off), the left trigger = the shift taken for either hand, the right hand's own trigger, "
                "key off / menu / panel / focus / other view / stale word: all passed\n");
    return 0;
}
