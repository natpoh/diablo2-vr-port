// The hands without controllers (vr/hand_keep.h): the kept hand by the head and its speed limit, the palm joint as a grip and an aim,
// the pinch that reaches only the panels, the panels' key. No game, no headset.
//
//   hand_keep_test     (exit code 0 = all passed)

#include <cmath>
#include <cstdio>

#include "hand_keep.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace handkeep;
using math::V3;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
static bool NearV(V3 a, V3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
static float Dist(const Pose& a, const Pose& b) {
    const float dx = a.p[0] - b.p[0], dy = a.p[1] - b.p[1], dz = a.p[2] - b.p[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
static Pose At(float x, float y, float z, float yawDeg = 0.0f) {
    Pose p;
    p.p[0] = x; p.p[1] = y; p.p[2] = z;
    math::YawQ(yawDeg * 3.14159265f / 180.0f, p.q);
    return p;
}

int main() {
    // -- never seen: the rest place below and in front of the head, on its own side, at once (nothing to come from)
    {
        Keeper k;
        Pose g, a;
        CHECK(!k.Keep(At(0, 1.7f, 0), false, 0.011f, 1, &g, &a));   // no head, never a place: nothing
        CHECK(k.Keep(At(0, 1.7f, 0), true, 0.011f, 1, &g, &a));
        CHECK(Near(g.p[0], 0.20f) && Near(g.p[1], 1.25f) && Near(g.p[2], -0.28f));
        CHECK(k.Kept() && !k.Seen());
        Keeper l;
        CHECK(l.Keep(At(0, 1.7f, 0), true, 0.011f, 0, &g, &a));
        CHECK(Near(g.p[0], -0.20f));
        // pointing ahead and down: its -z
        const V3 f = math::Rot(g.q, {0, 0, -1});
        CHECK(f.z < -0.8f && f.y < -0.4f);
        // the head turned 90 degrees left at rest: the rest place comes round with it (put there: never placed before)
        Keeper t;
        CHECK(t.Keep(At(0, 1.7f, 0, 90.0f), true, 0.011f, 1, &g, &a));
        CHECK(Near(g.p[0], -0.28f) && Near(g.p[2], -0.20f));
    }
    // -- seen, then lost: it stays where it was by the head; the head turns, it follows - never faster than the limits
    {
        Keeper k;
        const Pose head = At(0.5f, 1.6f, 0.3f);
        Pose grip = At(0.8f, 1.2f, -0.1f, 10.0f), aim = grip;
        aim.p[2] -= 0.05f;   // the aim 5 cm ahead of the grip
        k.Live(grip, aim, head);
        CHECK(!k.Kept() && k.Seen());
        Pose g, a;
        CHECK(k.Keep(head, true, 0.011f, 1, &g, &a));
        CHECK(Dist(g, grip) < 1e-4f);            // the same head: the same place
        CHECK(Near(a.p[2], grip.p[2] - 0.05f));  // the aim kept in the grip's frame
        CHECK(math::Angle(g.q, grip.q) < 1e-3f);
        // the head turns 180 degrees in one frame: the hand comes round 0.5 m/s at most
        const Pose turned = At(0.5f, 1.6f, 0.3f, 180.0f);
        Pose prev = g;
        float most = 0.0f;
        for (int i = 0; i < 400; ++i) {
            CHECK(k.Keep(turned, true, 0.011f, 1, &g, &a));
            const float v = Dist(g, prev) / 0.011f;
            most = v > most ? v : most;
            prev = g;
        }
        CHECK(most <= kMaxSpeedMS + 1e-3f);
        // ... and gets there: the same place by the turned head (0.3 m right, 0.4 m down, 0.4 m ahead before: now behind the old place)
        CHECK(Near(g.p[0], 0.5f - 0.3f, 2e-3f) && Near(g.p[1], 1.2f, 2e-3f) && Near(g.p[2], 0.3f + 0.4f, 2e-3f));
        // the turn limit: one 11 ms frame turns at most 90 deg/s
        Keeper r;
        r.Live(At(0, 1.2f, -0.3f), At(0, 1.2f, -0.3f), At(0, 1.6f, 0));
        Pose before, b;
        r.Keep(At(0, 1.6f, 0), true, 0.011f, 1, &before, &b);
        r.Keep(At(0, 1.6f, 0, 120.0f), true, 0.011f, 1, &g, &a);
        CHECK(math::Angle(before.q, g.q) <= kMaxTurnDegS * 3.14159265f / 180.0f * 0.011f + 1e-4f);
        // a long frame (a hitch) is counted as 0.1 s at most: no jump
        r.Keep(At(0, 1.6f, 0, -120.0f), true, 5.0f, 1, &g, &a);
        r.Keep(At(0, 1.6f, 0, -120.0f), true, 5.0f, 1, &b, &a);
        CHECK(Dist(g, b) <= kMaxSpeedMS * 0.1f + 1e-4f);
        // a nonsense frame time: nothing moves
        Pose c;
        r.Keep(At(0, 1.6f, 0, 60.0f), true, NAN, 1, &c, &a);
        CHECK(Dist(c, b) < 1e-6f);
    }
    // -- the head lost while kept: the hand stays where it is (in the room)
    {
        Keeper k;
        k.Live(At(0.2f, 1.2f, -0.3f), At(0.2f, 1.2f, -0.3f), At(0, 1.6f, 0));
        Pose g, a, h;
        k.Keep(At(0, 1.6f, 0), true, 0.011f, 1, &g, &a);
        CHECK(k.Keep(At(5, 5, 5, 90.0f), false, 0.011f, 1, &h, &a));
        CHECK(Dist(g, h) < 1e-6f);
    }
    // -- live again: at once where the controller is; Reset forgets
    {
        Keeper k;
        k.Live(At(0.2f, 1.2f, -0.3f), At(0.2f, 1.2f, -0.3f), At(0, 1.6f, 0));
        Pose g, a;
        k.Keep(At(0, 1.6f, 0), true, 0.011f, 1, &g, &a);
        CHECK(k.Kept());
        k.Live(At(1, 1, 1), At(1, 1, 1), At(0, 1.6f, 0));
        CHECK(!k.Kept());
        k.Reset();
        CHECK(!k.Seen() && !k.Kept());
    }
    // -- the palm joint as a grip: the axes by the two conventions
    {
        // a right hand held palm down, the fingers ahead: the palm joint's -z ahead, +y up (the back of the hand), x right
        Pose palm = At(0.3f, 1.1f, -0.4f);
        const Pose g = GripFromPalm(palm, 1);
        CHECK(Near(g.p[0], 0.3f) && Near(g.p[2], -0.4f));
        CHECK(NearV(math::Rot(g.q, {1, 0, 0}), {0, 1, 0}));    // right grip +x into the palm: from below, up
        CHECK(NearV(math::Rot(g.q, {0, 0, -1}), {-1, 0, 0}));  // -z little finger (right) to thumb (left)
        CHECK(NearV(math::Rot(g.q, {0, 1, 0}), {0, 0, 1}));    // +y toward the wrist
        const Pose l = GripFromPalm(palm, 0);
        CHECK(NearV(math::Rot(l.q, {1, 0, 0}), {0, -1, 0}));   // left grip +x out of the palm: down
        CHECK(NearV(math::Rot(l.q, {0, 0, -1}), {1, 0, 0}));   // little finger (left) to thumb (right)
        CHECK(NearV(math::Rot(l.q, {0, 1, 0}), {0, 0, 1}));
        // turned with the hand: the palm turned 90 degrees left, the grip turns with it
        const Pose palm2 = At(0, 1, 0, 90.0f);
        const Pose g2 = GripFromPalm(palm2, 1);
        CHECK(NearV(math::Rot(g2.q, {0, 0, -1}), math::Rot(palm2.q, {-1, 0, 0})));
        CHECK(Near(g2.q[0] * g2.q[0] + g2.q[1] * g2.q[1] + g2.q[2] * g2.q[2] + g2.q[3] * g2.q[3], 1.0f));
        // the aim is the palm as it is: ahead along the fingers
        const Pose a = AimFromPalm(palm);
        CHECK(NearV(math::Rot(a.q, {0, 0, -1}), {0, 0, -1}));
    }
    // -- FromColumns: identity and a quarter turn
    {
        float q[4];
        math::FromColumns({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, q);
        CHECK(Near(q[3], 1.0f));
        math::FromColumns({0, 0, -1}, {0, 1, 0}, {1, 0, 0}, q);   // 90 degrees about +y: x -> -z
        CHECK(NearV(math::Rot(q, {1, 0, 0}), {0, 0, -1}));
        CHECK(NearV(math::Rot(q, {0, 0, 1}), {1, 0, 0}));
        math::FromColumns({-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, q);  // 180 about z
        CHECK(NearV(math::Rot(q, {1, 0, 0}), {-1, 0, 0}));
        CHECK(NearV(math::Rot(q, {0, 1, 0}), {0, -1, 0}));
    }
    // -- the pinch: only to a panel that is up, never one carried over
    {
        PinchGate p;
        CHECK(p.Step(false, 1.0f) == 0.0f);   // no panel: nothing
        CHECK(p.Step(true, 1.0f) == 0.0f);    // the panel came up with the pinch down: still nothing
        CHECK(p.Step(true, 0.0f) == 0.0f);    // let go
        CHECK(p.Step(true, 0.5f) == 0.0f);    // under the press
        CHECK(p.Step(true, 0.9f) > 0.8f);     // a fresh pinch on the panel: passed
        CHECK(p.Step(true, 0.5f) > 0.4f);     // held (over the let-go)
        CHECK(p.Step(false, 0.9f) == 0.0f);   // the panel closed with it down: nothing
        CHECK(p.Step(true, 0.9f) == 0.0f);    // ... even if a panel comes up again, until let go
        CHECK(p.Step(true, 0.1f) == 0.0f);
        CHECK(p.Step(true, 1.0f) == 1.0f);
        p.Reset();
        CHECK(p.Step(false, 0.0f) == 0.0f);
    }
    // -- the panels' key
    {
        CHECK(KeyVk(0) == 0 && KeyVk(1) == 0x2D && KeyVk(2) == 0x24 && KeyVk(3) == 0x23 && KeyVk(4) == 0 && KeyVk(-1) == 0);
        CHECK(KeyName(2)[0] == 'H');
    }

    std::printf("hand_keep_test: %d of %d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
