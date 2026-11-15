// Offline checks of vr/hand_swap.h ([hands] left_handed): the two sticks traded and nothing else - the layout's left stick (the walk) reads the
// right stick's push and its right stick (the turn) the left one's, each click with its stick (its value, touch, lean and a gesture's take of
// it); the triggers, grips, X / Y, A / B and the menus stay. BodyWalk's copy: each stick's X / Y and click bit traded, the rest its own. A second
// trade gives the frame back; F4 only; the frame's verdict.
// Run: build/Release/hand_swap_test.exe (exit code 0 = passed)

#include <bodywalk/game_link.h>

#include <cstdio>
#include <cstring>

#include "../../vr/hand_swap.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace directinput::layout;

static void TestSwapSticks() {
    Controls c;
    for (int i = 0; i < kCtlCount; ++i) c.v[i] = 0.05f * (i + 1);   // every control its own value
    c.v[kLStick] = 0.0f; c.v[kRStick] = 1.0f;      // the right stick clicked
    c.stick[0][0] = 0.1f; c.stick[0][1] = 0.9f;    // the left stick pushed forward
    c.stick[1][0] = -0.7f; c.stick[1][1] = 0.0f;   // the right stick to the left
    c.lean[0] = 0.9f; c.lean[1] = 0.7f;
    c.touch[kRStick] = true; c.touch[kRA] = true; c.touch[kLTrigger] = true;
    const Controls was = c;
    bool taken[kCtlCount] = {};
    taken[kLStick] = true;   // a gesture took the left stick's click
    taken[kRGrip] = true;    // and the right hand's grip (the weapon in it)
    handswap::SwapSticks(&c, taken);
    // the sticks traded: the walk (the layout's left stick) is the right stick's push, the turn the left one's - each click with its stick
    CHECK(c.stick[0][0] == was.stick[1][0] && c.stick[0][1] == was.stick[1][1]);
    CHECK(c.stick[1][0] == was.stick[0][0] && c.stick[1][1] == was.stick[0][1]);
    CHECK(c.v[kLStick] == 1.0f && c.v[kRStick] == 0.0f);   // (the right stick's long press binding - the skill panel - is the left stick's now)
    CHECK(c.touch[kLStick] && !c.touch[kRStick]);
    CHECK(c.lean[0] == was.lean[1] && c.lean[1] == was.lean[0]);
    CHECK(taken[kRStick] && !taken[kLStick]);
    // every other button where it was: triggers, grips, X Y A B, the menus - and their touch, and the grip's take
    for (int i = 0; i < kCtlCount; ++i) {
        if (i == kLStick || i == kRStick) continue;
        CHECK(c.v[i] == was.v[i] && c.touch[i] == was.touch[i]);
    }
    CHECK(taken[kRGrip] && !taken[kLGrip]);
    // traded again: as it came
    handswap::SwapSticks(&c, taken);
    CHECK(std::memcmp(c.stick, was.stick, sizeof c.stick) == 0 && std::memcmp(c.v, was.v, sizeof c.v) == 0);
    CHECK(std::memcmp(c.touch, was.touch, sizeof c.touch) == 0 && c.lean[0] == was.lean[0]);
    CHECK(taken[kLStick] && !taken[kRStick]);
    handswap::SwapSticks(&c, nullptr);   // no takes: fine
    CHECK(c.v[kLStick] == 1.0f);
}

// A hand that says which controller it is in every field.
static D2RVR_XrHand MakeHand(float tag, uint32_t buttons) {
    D2RVR_XrHand h{};
    h.grip.pos[0] = tag; h.grip.rot[3] = 1.0f; h.grip.valid = 1;
    h.aim.pos[0] = -tag; h.aim.rot[3] = 1.0f; h.aim.valid = 1;
    h.trigger = tag * 0.1f;
    h.squeeze = tag * 0.2f;
    h.stickX = tag * 0.3f;
    h.stickY = -tag * 0.3f;
    h.buttons = buttons;
    h.active = 1;
    return h;
}

static void TestSwapStickAxes() {
    const D2RVR_XrHand left = MakeHand(1.0f, BW_LINK_XRB_PRIMARY | BW_LINK_XRB_MENU | BW_LINK_XRB_STICK);   // the left stick clicked
    const D2RVR_XrHand right = MakeHand(2.0f, BW_LINK_XRB_TRIGGER | BW_LINK_XRB_SECONDARY);
    D2RVR_XrHand h[2] = {left, right};
    handswap::SwapStickAxes(h, BW_LINK_XRB_STICK);
    CHECK(h[0].stickX == right.stickX && h[0].stickY == right.stickY);
    CHECK(h[1].stickX == left.stickX && h[1].stickY == left.stickY);
    CHECK((h[0].buttons & BW_LINK_XRB_STICK) == 0 && (h[1].buttons & BW_LINK_XRB_STICK) != 0);   // the click went with its stick
    // every other button, the trigger, the grip, the poses: each controller's own
    CHECK((h[0].buttons & ~BW_LINK_XRB_STICK) == (left.buttons & ~BW_LINK_XRB_STICK) && (h[1].buttons & ~BW_LINK_XRB_STICK) == right.buttons);
    CHECK(h[0].trigger == left.trigger && h[1].squeeze == right.squeeze);
    CHECK(std::memcmp(&h[0].grip, &left.grip, sizeof left.grip) == 0 && std::memcmp(&h[1].aim, &right.aim, sizeof right.aim) == 0);
    handswap::SwapStickAxes(h, BW_LINK_XRB_STICK);
    CHECK(std::memcmp(&h[0], &left, sizeof left) == 0 && std::memcmp(&h[1], &right, sizeof right) == 0);
}

static void TestGates() {
    CHECK(handswap::Decide(true, true));
    CHECK(!handswap::Decide(true, false));   // not F4: as before
    CHECK(!handswap::Decide(false, true));
    handswap::SetNow(true);
    CHECK(handswap::Now());
    handswap::SetNow(false);
    CHECK(!handswap::Now());
    handswap::SetWanted(true);
    CHECK(handswap::Wanted());
    handswap::SetWanted(false);
    CHECK(!handswap::Wanted());
}

int main() {
    TestSwapSticks();
    TestSwapStickAxes();
    TestGates();
    std::printf("hand_swap_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
