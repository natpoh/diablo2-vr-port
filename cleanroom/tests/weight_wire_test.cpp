// The weight model's grips wired to direct_input.h (vr/weight_wire.cpp), with stand-ins for what it calls: direct_input's Listen / Take /
// Latest and the weight model's Placed / HoldsWeapon. No game, no headset.
//
//   weight_wire_test
//
// Checks: nothing is taken while the model holds no weapon; the grip of each hand on the weapon (and only that one) is taken, in
// direct_input's hand numbering; a menu, the lost focus and another view take nothing; the real grips come from the frame (a taken grip
// reads as let go in the shared block, so the model needs them), and only while there are frames and the model has a weapon placed.

#include "../../vr/direct_input.h"
#include "../../vr/view_gate.h"
#include "../../vr/weight.h"
#include "../../vr/weight_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

// ---- the stand-ins ----
namespace weight {
bool g_placed = false;
bool g_holds[2] = {false, false};   // [0 right, 1 left]
bool Placed() { return g_placed; }
bool HoldsWeapon(int side) { return side >= 0 && side < 2 && g_holds[side]; }
}   // namespace weight

namespace directinput {
FrameFn g_fn = nullptr;
int g_takes = 0;
int g_takeHand[4] = {-1, -1, -1, -1};
uint32_t g_takeMask[4] = {};
Source g_takeSrc = kSrcLayout;
bool g_haveFrame = false;
Frame g_frame{};
bool Listen(FrameFn fn, void*) { g_fn = fn; return true; }
void Take(int hand, uint32_t buttons, Source src) {
    if (g_takes < 4) { g_takeHand[g_takes] = hand; g_takeMask[g_takes] = buttons; }
    ++g_takes;
    g_takeSrc = src;
}
bool Latest(Frame* out) {
    if (!g_haveFrame) return false;
    *out = g_frame;
    return true;
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

void NewFrame(uint32_t flags, float rightSqueeze, float leftSqueeze) {
    using namespace directinput;
    g_frame.version = kVersion;
    ++g_frame.counter;
    g_frame.flags = flags;
    g_frame.hand[1].squeeze = rightSqueeze;   // direct_input's numbering: 0 left, 1 right
    g_frame.hand[0].squeeze = leftSqueeze;
    g_haveFrame = true;
    g_takes = 0;
    g_takeHand[0] = g_takeHand[1] = -1;
    g_fn(g_frame, nullptr);
}

}  // namespace

int main() {
    using namespace directinput;
    viewgate::Set([]() { return g_f4; });
    weightwire::Init(nullptr);
    EXPECT(g_fn != nullptr);

    // nothing placed: nothing taken, and the model's grips are the shared block's
    NewFrame(kFocused, 1.0f, 0.0f);
    EXPECT(g_takes == 0);
    float raw[2];
    EXPECT(!weightwire::RawGrips(raw));

    // placed, no hand on it (it lies on the floor): nothing taken; the real grips are given
    weight::g_placed = true;
    NewFrame(kFocused, 0.9f, 0.2f);
    EXPECT(g_takes == 0);
    EXPECT(weightwire::RawGrips(raw) && raw[0] > 0.89f && raw[0] < 0.91f && raw[1] > 0.19f && raw[1] < 0.21f);   // [0] right, [1] left

    // the right hand on it: the right grip taken, as direct_input's hand 1
    weight::g_holds[0] = true;
    NewFrame(kFocused, 1.0f, 0.0f);
    EXPECT(g_takes == 1 && g_takeHand[0] == 1);
    EXPECT((g_takeMask[0] & (kSqueeze | kTakeSqueeze)) == (kSqueeze | kTakeSqueeze));
    EXPECT(g_takeSrc == kSrcOther1);
    EXPECT(weightwire::RawGrips(raw) && raw[0] > 0.99f && raw[1] < 0.01f);

    // both hands on it: both grips taken
    weight::g_holds[1] = true;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 2 && g_takeHand[0] == 1 && g_takeHand[1] == 0);

    // only the left on it
    weight::g_holds[0] = false;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 1 && g_takeHand[0] == 0);

    // a menu, the lost focus, another view, the frame's other-view flag: nothing taken
    NewFrame(kFocused | kInMenu, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    NewFrame(0, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    NewFrame(kFocused | kOtherView, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    g_f4 = false;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    g_f4 = true;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 1);   // back in F4: taken again, at once

    // the model has let go of everything: nothing taken, nothing to read specially
    weight::g_holds[1] = false;
    NewFrame(kFocused, 1.0f, 1.0f);
    EXPECT(g_takes == 0);
    weight::g_placed = false;
    EXPECT(!weightwire::RawGrips(raw));

    // no frame at all (no session): no real grips, the shared block's are used
    weight::g_placed = true;
    g_haveFrame = false;
    EXPECT(!weightwire::RawGrips(raw));

    if (g_fails) {
        std::printf("weight_wire_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("weight_wire_test: not placed, floor, one hand, both hands, the other hand, menu / focus / other view / F4, real grips: all passed\n");
    return 0;
}
