// Offline checks of vr/hand_swap.h ([hands] left_handed): the two controllers traded once where they come in - the poses, trigger, grip, stick,
// buttons and touch go with the controller, the menu button stays on the one that pressed it, a second trade gives the frame back; the shared
// block's right / left hands and their valid bits; the physical controller a hand reads; F4 only; BodyWalk's tracking traded under FlatVR
// alone (a native session's is made from our traded frame); and the wrist panels' palm test on the right controller once it is hand 0.
// Run: build/Release/hand_swap_test.exe (exit code 0 = passed)

#include <bodywalk/game_link.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../vr/hand_swap.h"
#include "../../vr/hand_vitals.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }

// A hand that says which controller it came from in every field.
static D2RVR_XrHand MakeHand(float tag, uint32_t buttons) {
    D2RVR_XrHand h{};
    h.grip.pos[0] = tag; h.grip.pos[1] = tag + 0.1f; h.grip.pos[2] = tag + 0.2f;
    h.grip.rot[3] = 1.0f; h.grip.valid = 1;
    h.aim.pos[0] = -tag; h.aim.rot[3] = 1.0f; h.aim.valid = 1;
    h.trigger = tag * 0.1f;
    h.squeeze = tag * 0.2f;
    h.stickX = tag * 0.3f;
    h.stickY = -tag * 0.3f;
    h.buttons = buttons;
    h.active = 1;
    return h;
}

static bool SameHand(const D2RVR_XrHand& a, const D2RVR_XrHand& b, uint32_t ignoreButtons = 0) {
    return std::memcmp(&a.grip, &b.grip, sizeof a.grip) == 0 && std::memcmp(&a.aim, &b.aim, sizeof a.aim) == 0 && a.trigger == b.trigger &&
           a.squeeze == b.squeeze && a.stickX == b.stickX && a.stickY == b.stickY && a.active == b.active &&
           (a.buttons & ~ignoreButtons) == (b.buttons & ~ignoreButtons);
}

static void TestMirrorHands() {
    const uint32_t menu = BW_LINK_XRB_MENU;
    const D2RVR_XrHand left = MakeHand(1.0f, BW_LINK_XRB_PRIMARY | BW_LINK_XRB_MENU), right = MakeHand(2.0f, BW_LINK_XRB_TRIGGER | BW_LINK_XRB_STICK);
    D2RVR_XrHand h[2] = {left, right};
    uint32_t touch[2] = {0x1u, 0x8u};
    handswap::MirrorHands(h, touch, menu);
    // the right controller is hand 0 now, the left one hand 1: everything of each but the menu button went with it
    CHECK(SameHand(h[0], right, menu));
    CHECK(SameHand(h[1], left, menu));
    CHECK((h[1].buttons & BW_LINK_XRB_PRIMARY) != 0);   // the left controller's X is the "right" hand's primary now
    CHECK((h[0].buttons & BW_LINK_XRB_TRIGGER) != 0);   // the right controller's trigger is the off hand's
    CHECK((h[1].buttons & BW_LINK_XRB_TRIGGER) == 0);
    // the menu button stays on hand 0, where the runtime put it (Touch's left menu; PS VR2's right Options button)
    CHECK((h[0].buttons & menu) != 0);
    CHECK((h[1].buttons & menu) == 0);
    CHECK(touch[0] == 0x8u && touch[1] == 0x1u);
    // traded again: the frame as it came
    handswap::MirrorHands(h, touch, menu);
    CHECK(SameHand(h[0], left));
    CHECK(SameHand(h[1], right));
    CHECK(touch[0] == 0x1u && touch[1] == 0x8u);
    // a menu press on the right controller (a runtime that binds one there) stays there too; no touch array is fine
    D2RVR_XrHand g[2] = {MakeHand(1.0f, 0), MakeHand(2.0f, BW_LINK_XRB_MENU | BW_LINK_XRB_SECONDARY)};
    handswap::MirrorHands(g, nullptr, menu);
    CHECK(g[1].buttons == BW_LINK_XRB_MENU);
    CHECK(g[0].buttons == BW_LINK_XRB_SECONDARY);
    // nothing kept: a plain trade
    D2RVR_XrHand k[2] = {left, right};
    handswap::MirrorHands(k, nullptr, 0);
    CHECK(SameHand(k[0], right) && SameHand(k[1], left));
}

static void TestMirrorShared() {
    D2RVR_Shared s{};
    for (int i = 0; i < 3; ++i) { s.rightHand[i] = 1.0f + i; s.leftHand[i] = -1.0f - i; }
    for (int i = 0; i < 4; ++i) { s.rightRot[i] = 0.1f * (i + 1); s.leftRot[i] = -0.1f * (i + 1); }
    s.rightGrip = 0.9f; s.leftGrip = 0.2f;
    s.handsValid = 1u;   // the right hand alone
    s.headYawDeg = 33.0f;
    handswap::MirrorShared(&s);
    CHECK(Near(s.rightHand[0], -1.0f) && Near(s.rightHand[2], -3.0f) && Near(s.leftHand[1], 2.0f));
    CHECK(Near(s.rightRot[3], -0.4f) && Near(s.leftRot[0], 0.1f));
    CHECK(Near(s.rightGrip, 0.2f) && Near(s.leftGrip, 0.9f));
    CHECK(s.handsValid == 2u);       // the controller that was the right is the left hand now
    CHECK(Near(s.headYawDeg, 33.0f));  // the head is not a hand
    s.handsValid = 3u | 0x10u;       // both, and a bit that is not a hand's
    handswap::MirrorShared(&s);
    CHECK(s.handsValid == (3u | 0x10u));
    s.handsValid = 2u;
    handswap::MirrorShared(&s);
    CHECK(s.handsValid == 1u);
    handswap::MirrorShared(nullptr);   // no block: nothing
}

static void TestPhysAndGates() {
    CHECK(handswap::PhysOf(0, false) == 0 && handswap::PhysOf(1, false) == 1);
    CHECK(handswap::PhysOf(0, true) == 1 && handswap::PhysOf(1, true) == 0);
    CHECK(handswap::PhysOf(3, true) == 0);   // (any int: its low bit)
    CHECK(handswap::Decide(true, true));
    CHECK(!handswap::Decide(true, false));   // not F4: the controllers as they are
    CHECK(!handswap::Decide(false, true));
    handswap::SetNow(false);
    CHECK(handswap::Phys(1) == 1);
    handswap::SetNow(true);
    CHECK(handswap::Phys(1) == 0 && handswap::Phys(0) == 1);
    handswap::SetNow(false);
    handswap::SetWanted(true);
    CHECK(handswap::Wanted());
    handswap::SetWanted(false);
    CHECK(!handswap::Wanted());
    // BodyWalk's tracking: traded under FlatVR (no native frame lately), not under a native session (made from our traded frame)
    CHECK(!handswap::NativeFresh(0, 1000));
    CHECK(handswap::NativeFresh(1000, 1200));
    CHECK(!handswap::NativeFresh(1000, 1600));
    CHECK(!handswap::NativeFresh(2000, 1000));   // (a clock read before the note: not fresh, never a wrap to "fresh")
    CHECK(handswap::MirrorBodyWalk(true, true, false));
    CHECK(!handswap::MirrorBodyWalk(true, true, true));
    CHECK(!handswap::MirrorBodyWalk(true, false, false));
    CHECK(!handswap::MirrorBodyWalk(false, true, false));
}

// The wrist panels (hand_vitals_wire / wrist_status_wire) measure the palm from the controller's own axes: OpenXR's grip +X is out of the
// LEFT palm and into the RIGHT one. Once traded, hand 0 is the right controller: the wires XOR the flip with the swap, so the palm stays right.
static void TestPalmAfterSwap() {
    const hset::Q id{0.0f, 0.0f, 0.0f, 1.0f};
    const hset::V3 leftPalm = handvitals::PalmNormal(id, 0, false), rightPalm = handvitals::PalmNormal(id, 1, false);
    CHECK(Near(leftPalm.x, 1.0f) && Near(rightPalm.x, -1.0f));
    const bool swapped = true, debugFlip = false;
    const hset::V3 hand0 = handvitals::PalmNormal(id, 0, debugFlip != swapped);   // hand 0 = the right controller
    CHECK(Near(hand0.x, rightPalm.x));
    const hset::V3 hand1 = handvitals::PalmNormal(id, 1, debugFlip != swapped);   // hand 1 = the left controller
    CHECK(Near(hand1.x, leftPalm.x));
    // [debug] hand_vitals_flip still turns it round on top
    CHECK(Near(handvitals::PalmNormal(id, 0, true != swapped).x, leftPalm.x));
}

int main() {
    TestMirrorHands();
    TestMirrorShared();
    TestPhysAndGates();
    TestPalmAfterSwap();
    std::printf("hand_swap_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
