// Offline checks of vr/flat_raw.h: BodyWalk's raw controllers through Game Link (Tracking 2) made into the frame a native OpenXR session makes -
// the head put back together from the angles BodyWalk took apart (its universal_output.cpp, reproduced here), the hands placed from the head,
// the buttons in the native frame's bits, the stamps as time, and what is not a raw block.
// Run: build/Release/flat_raw_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>

#include "../../vr/flat_raw.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static void Rotate(const float q[4], const float v[3], float o[3]) {
    const float qv[4] = {v[0], v[1], v[2], 0.0f};
    const float qc[4] = {-q[0], -q[1], -q[2], q[3]};
    float t[4], r[4];
    flatraw::QMul(q, qv, t);
    flatraw::QMul(t, qc, r);
    o[0] = r[0]; o[1] = r[1]; o[2] = r[2];
}

// BodyWalk's own split of a head rotation (universal_output.cpp): yaw from forward, pitch = asin(forward.y), roll = -asin(right.y)
static void Split(const float q[4], float* yawDeg, float* pitchDeg, float* rollDeg) {
    const float f0[3] = {0, 0, -1}, r0[3] = {1, 0, 0};
    float fwd[3], right[3];
    Rotate(q, f0, fwd);
    Rotate(q, r0, right);
    *yawDeg = std::atan2(-fwd[0], -fwd[2]) * 57.2957795f;
    *pitchDeg = std::asin(std::fmax(-1.0f, std::fmin(1.0f, fwd[1]))) * 57.2957795f;
    *rollDeg = -std::asin(std::fmax(-1.0f, std::fmin(1.0f, right[1]))) * 57.2957795f;
}

static BW_LinkTracking Sample() {
    BW_LinkTracking t{};
    t.header.magic = BW_LINK_MAGIC;
    t.header.version = BW_LINK_TRACKING_VERSION;
    t.flags = BW_LINK_TRACK_HEAD | BW_LINK_TRACK_PITCH | BW_LINK_TRACK_ROLL | BW_LINK_TRACK_ROOM | BW_LINK_TRACK_CONTROLS;
    t.head.valid = 1;
    t.head.pos[0] = 0.5f; t.head.pos[1] = 1.65f; t.head.pos[2] = 0.25f;
    t.head.yawDeg = 10.0f;      // since BodyWalk's recenter
    t.head.yawRoomDeg = 40.0f;  // in the room
    t.left.valid = t.right.valid = 1;
    t.left.pos[0] = -0.25f; t.left.pos[1] = -0.45f; t.left.pos[2] = -0.35f;
    t.left.rot[3] = 1.0f;
    t.right.pos[0] = 0.2f; t.right.pos[1] = -0.4f; t.right.pos[2] = -0.4f;
    t.right.rot[0] = 0.0f; t.right.rot[1] = 0.70710678f; t.right.rot[2] = 0.0f; t.right.rot[3] = 0.70710678f;
    return t;
}

static void TestHeadRot() {
    // every head BodyWalk can send comes back as the angles it sent. BodyWalk's roll is -asin(right.y) and right.y = sin(r) cos(pitch), so a
    // head pitched by p can only report |roll| <= asin(cos p): the angles past that are no head (75 deg up and 40 deg of roll is not one).
    int good = 0, all = 0;
    for (float y = -170.0f; y <= 180.0f; y += 35.0f)
        for (float p = -75.0f; p <= 75.0f; p += 25.0f)
            for (float r = -40.0f; r <= 40.0f; r += 20.0f) {
                if (std::fabs(std::sin(r * 0.0174532925f)) > std::cos(p * 0.0174532925f)) continue;
                float q[4], ys, ps, rs;
                flatraw::HeadRot(y, p, r, q);
                CHECK(Near(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0f, 1e-4f));
                Split(q, &ys, &ps, &rs);
                float dy = ys - y;
                dy -= 360.0f * std::floor((dy + 180.0f) / 360.0f);
                ++all;
                if (Near(dy, 0.0f, 0.05f) && Near(ps, p, 0.05f) && Near(rs, r, 0.05f)) ++good;
                else std::printf("  yaw %.0f pitch %.0f roll %.0f -> %.2f %.2f %.2f\n", y, p, r, ys, ps, rs);
            }
    CHECK(all > 250 && good == all);   // (297 heads)
    // straight ahead: no rotation; a quarter turn to the left looks along -x
    float q[4];
    flatraw::HeadRot(0, 0, 0, q);
    CHECK(Near(q[3], 1.0f) && Near(q[1], 0.0f));
    flatraw::HeadRot(90.0f, 0, 0, q);
    const float f0[3] = {0, 0, -1};
    float f[3];
    Rotate(q, f0, f);
    CHECK(Near(f[0], -1.0f) && Near(f[2], 0.0f));
}

static void TestFrame() {
    BW_LinkTracking t = Sample();
    t.right.trigger = 0.9f;
    t.right.grip = 0.3f;
    t.right.buttons = flatraw::kOvrPrimary | flatraw::kOvrStickClick;
    t.left.stickX = -0.5f; t.left.stickY = 1.2f;   // a stick past its edge is clamped
    t.left.buttons = flatraw::kOvrMenu | flatraw::kOvrSecondary;
    D2RVR_XrInput in;
    flatraw::ToXrInput(t, 123456789ll, 7, 4242, &in);
    CHECK(in.version == D2RVR_XR_INPUT_VERSION && in.counter == 7 && in.stamp == 4242 && in.displayTime == 123456789ll);
    CHECK(in.focused == 1 && in.head.valid == 1);
    CHECK(Near(in.head.pos[0], 0.5f) && Near(in.head.pos[1], 1.65f) && Near(in.head.pos[2], 0.25f));
    {   // the head's yaw is the ROOM's (the hands are in the room's axes), not the recentred one
        float ys, ps, rs;
        Split(in.head.rot, &ys, &ps, &rs);
        CHECK(Near(ys, 40.0f, 0.05f) && Near(ps, 0.0f, 0.05f));
    }
    // the grips: head + the hand's offset, the rotation as sent; the aim the same (BodyWalk sends none)
    CHECK(in.hand[0].grip.valid == 1 && Near(in.hand[0].grip.pos[0], 0.25f) && Near(in.hand[0].grip.pos[1], 1.2f) && Near(in.hand[0].grip.pos[2], -0.1f));
    CHECK(Near(in.hand[1].grip.pos[0], 0.7f) && Near(in.hand[1].grip.rot[1], 0.70710678f) && Near(in.hand[1].grip.rot[3], 0.70710678f));
    CHECK(in.hand[1].aim.valid == 1 && Near(in.hand[1].aim.pos[0], 0.7f));
    // the controls
    CHECK(Near(in.hand[1].trigger, 0.9f) && Near(in.hand[1].squeeze, 0.3f));
    CHECK(in.hand[1].buttons == (BW_LINK_XRB_PRIMARY | BW_LINK_XRB_STICK | BW_LINK_XRB_TRIGGER));   // 0.9 of a pull is past the click; 0.3 of a squeeze is not
    CHECK(in.hand[0].buttons == (BW_LINK_XRB_MENU | BW_LINK_XRB_SECONDARY));
    CHECK(Near(in.hand[0].stickX, -0.5f) && Near(in.hand[0].stickY, 1.0f));
    CHECK(in.hand[0].active == 1 && in.hand[1].active == 1);
}

static void TestButtons() {
    using namespace flatraw;
    CHECK(Buttons(0, 0.5f, 0.0f) == 0);                                        // half a pull: no click
    CHECK(Buttons(kOvrTriggerClick, 0.6f, 0.0f) == 0);                         // BodyWalk's click bit (past 0.5) is not the native 0.8
    CHECK(Buttons(kOvrTriggerClick, 0.0f, 0.0f) == BW_LINK_XRB_TRIGGER);       // a click with no pull: the click
    CHECK(Buttons(0, 0.81f, 0.0f) == BW_LINK_XRB_TRIGGER);
    CHECK(Buttons(kOvrGrip, 0.0f, 0.0f) == BW_LINK_XRB_SQUEEZE);               // a grip button with no squeeze value
    CHECK(Buttons(0, 0.0f, 1.0f) == BW_LINK_XRB_SQUEEZE);
    CHECK(Buttons(kOvrGrip, 0.0f, 0.6f) == 0);
    CHECK(Buttons(1ull << 34, 0.0f, 0.0f) == 0);                               // a stick touch is nothing the layout reads
}

static void TestNotRaw() {
    D2RVR_XrInput in;
    BW_LinkTracking t = Sample();
    t.header.version = 1;   // an older game_link.dll: its controls are not vouched for
    CHECK(!flatraw::Usable(t));
    flatraw::ToXrInput(t, 0, 1, 0, &in);
    CHECK(in.head.valid == 0 && in.focused == 0 && in.hand[1].grip.valid == 0);
    t = Sample();
    t.flags &= ~BW_LINK_TRACK_CONTROLS;   // a game's own frames came back (native)
    CHECK(!flatraw::Usable(t));
    t = Sample();
    t.head.valid = 0;
    CHECK(!flatraw::Usable(t));
    // no hand tracked (FlatVR without the focus): not focused, controls as sent (they are let go anyway)
    t = Sample();
    t.left.valid = t.right.valid = 0;
    flatraw::ToXrInput(t, 0, 1, 0, &in);
    CHECK(in.focused == 0 && in.head.valid == 1 && in.hand[0].grip.valid == 0 && in.hand[1].active == 0);
    // nonsense values are no values
    t = Sample();
    t.right.trigger = NAN; t.right.stickX = INFINITY;
    flatraw::ToXrInput(t, 0, 1, 0, &in);
    CHECK(in.hand[1].trigger == 0.0f && in.hand[1].stickX == 0.0f);
}

static void TestClock() {
    flatraw::Clock c;
    CHECK(flatraw::Step(c, 100) == 0);
    CHECK(flatraw::Step(c, 210) == 110ll * 100000);
    CHECK(flatraw::Step(c, 200) == 110ll * 100000);   // a stamp that went back adds nothing
    CHECK(flatraw::Step(c, 300) == 210ll * 100000);
    flatraw::Clock w;
    flatraw::Step(w, 0xFFFFFF00u);
    CHECK(flatraw::Step(w, 0x00000010u) == 0x110ll * 100000);   // across the clock's wrap
    CHECK(flatraw::Fresh(1000, 1000 + 4999) && !flatraw::Fresh(1000, 1000 + 5000) && flatraw::Fresh(0xFFFFFFF0u, 0x10u));
}

int main() {
    TestHeadRot();
    TestFrame();
    TestButtons();
    TestNotRaw();
    TestClock();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("flat_raw_test: all %d checks passed\n", g_checks);
    return 0;
}
