// Offline checks of vr/player_bodies.h ([multiplayer] pose_share_bodies): the other players' capsules from their poses, into the weight model's snapshot
// (render metres, ids no game unit has, weapons held 1 / bodies held 2), the draw, the echo stood ahead, and the knock of steel on steel.
// Run: player_bodies_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "player_bodies.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace playerbodies;
using poseshare::Pose;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static Pose Standing(bool body, int weapons) {
    Pose p;
    p.flags = poseshare::kHead | poseshare::kRight | poseshare::kLeft | poseshare::kF4;
    p.eyeM = 1.6f;
    p.heading = 0.0f;
    p.head.p[1] = 1.6f;
    p.right.p[0] = 0.3f; p.right.p[1] = 1.0f; p.right.p[2] = -0.3f;
    p.left.p[0] = -0.3f; p.left.p[1] = 1.0f; p.left.p[2] = -0.3f;
    if (body) {
        p.flags |= poseshare::kBody;
        const float sh[2][3] = {{0.18f, 1.40f, 0.0f}, {-0.18f, 1.40f, 0.0f}}, el[2][3] = {{0.25f, 1.12f, -0.05f}, {-0.25f, 1.12f, -0.05f}};
        std::memcpy(p.shoulder, sh, sizeof sh);
        std::memcpy(p.elbow, el, sizeof el);
    }
    for (int w = 0; w < weapons; ++w) {
        p.flags |= w ? poseshare::kWeapon1 : poseshare::kWeapon0;
        const float x = w ? -0.3f : 0.3f;
        const float butt[3] = {x, 1.0f, -0.25f}, tip[3] = {x, 1.0f, -1.15f};   // straight ahead, 0.9 m
        std::memcpy(p.weapon[w].butt, butt, sizeof butt);
        std::memcpy(p.weapon[w].tip, tip, sizeof tip);
        p.weapon[w].radius = 0.03f;
        p.weapon[w].hand = (uint8_t)w;
    }
    return p;
}

int main() {
    // ids: no game unit's, weapons told from bodies, below weight.cpp's kDualId (0xFFFFFFF0)
    CHECK(IsPlayerId(BodyId(0)) && IsPlayerId(WeaponId(kMaxPlayers - 1)) && !IsPlayerId(BodyId(kMaxPlayers)));
    CHECK(IsWeaponId(WeaponId(3)) && !IsWeaponId(BodyId(3)));
    CHECK(WeaponId(kMaxPlayers - 1) < 0xFFFFFFF0u);
    CHECK(!IsPlayerId(12345u));

    // the capsules: head, trunk, legs, two upper arms and forearms, two weapons
    {
        Cap c[kMaxCaps];
        const Pose p = Standing(true, 2);
        const int n = Capsules(p, true, true, c, kMaxCaps);
        CHECK(n == 9);
        CHECK(c[0].part == kHeadPart && Near(c[0].r, kHeadR * 1.6f, 1e-5f) && c[0].a[1] == 1.6f && c[0].b[1] == 1.6f);
        CHECK(c[1].part == kTrunk && Near(c[1].a[1], kHipOfEye * 1.6f, 1e-4f) && c[1].b[1] < 1.40f && c[1].b[1] > c[1].a[1]);   // from the hip to under the neck
        CHECK(Near(c[1].a[0], 0.0f, 1e-6f));                                                                                   // under the shoulders' middle
        CHECK(c[2].part == kLegs && Near(c[2].a[1], kLegsR * 1.6f, 1e-5f) && Near(c[2].b[1], c[1].a[1], 1e-5f));               // the legs stand on the floor
        CHECK(c[3].part == kUpperR && Near(c[3].a[0], 0.18f, 1e-6f) && Near(c[3].b[1], 1.12f, 1e-6f));
        CHECK(c[4].part == kForeR && Near(c[4].b[0], 0.3f, 1e-6f));   // elbow -> the controller
        CHECK(c[7].part == kWeaponPart0 && Near(c[7].r, 0.03f, 1e-6f) && Near(c[7].b[2], -1.15f, 1e-6f));
        CHECK(c[8].part == kWeaponPart1);
        CHECK(Capsules(p, false, true, c, kMaxCaps) == 2);   // the weapons alone
        CHECK(Capsules(p, true, false, c, kMaxCaps) == 7);
        CHECK(Capsules(p, true, true, c, 3) == 3);
        // no body from the sender: the neck under the head, no arms
        const Pose bare = Standing(false, 0);
        CHECK(Capsules(bare, true, true, c, kMaxCaps) == 3);
        CHECK(Near(c[1].b[1], 1.6f - kNeckUnderHead * 1.6f - 0.5f * kTrunkR * 1.6f, 1e-4f));
        // a crouch brings the hip down; never under the legs' own thickness
        Pose low = bare;
        low.head.p[1] = 0.8f;
        Capsules(low, true, false, c, kMaxCaps);
        CHECK(c[1].a[1] < kHipOfEye * 1.6f * 0.6f && c[1].a[1] >= 2.0f * kLegsR * 1.6f - 1e-6f);
        // nothing that is not finite
        Pose bad = Standing(false, 1);
        bad.weapon[0].tip[0] = NAN;
        CHECK(Capsules(bad, false, true, c, kMaxCaps) == 0);
    }

    // into the render world: heading 0 looks along -z; the sender's metres over its eye height, on our hero's eye height; then over our units
    {
        Cap c[kMaxCaps];
        const Pose p = Standing(true, 1);
        const int n = Capsules(p, true, true, c, kMaxCaps);
        monsterbodies::Snapshot snap;
        const float feet[3] = {100.0f, 20.0f, 50.0f};
        // our hero's eyes 16 units: the sender's 1.6 m = 16 units -> 10 units a metre of theirs; ours 8 units a metre (our eyes at 2 m)
        CHECK(AddBodies(c, n, 2, feet, 0.0f, 10.0f, 8.0f, &snap) == n);
        CHECK(snap.n == n);
        const monsterbodies::Body& head = snap.b[0];
        CHECK(head.id == BodyId(2) && head.held == 2 && !head.doll && !head.hostile && head.rig && head.bone == kHeadPart);
        CHECK(Near(head.bottom[0], 100.0f / 8.0f, 1e-4f) && Near(head.bottom[1], (20.0f + 16.0f) / 8.0f, 1e-4f));
        CHECK(Near(head.radius, kHeadR * 1.6f * 10.0f / 8.0f, 1e-5f));
        const monsterbodies::Body& w = snap.b[n - 1];
        CHECK(w.id == WeaponId(2) && w.held == 1 && w.bone == kWeaponPart0);
        CHECK(Near(w.top[2], (50.0f - 1.15f * 10.0f) / 8.0f, 1e-4f));   // its tip ahead (-z)
        CHECK(Near(w.top[0], (100.0f + 0.3f * 10.0f) / 8.0f, 1e-4f));   // on its right (+x)
        // turned half round: ahead is +z, its right -x
        monsterbodies::Snapshot turned;
        AddBodies(c, n, 2, feet, poseshare::kPi, 10.0f, 8.0f, &turned);
        CHECK(Near(turned.b[n - 1].top[2], (50.0f + 1.15f * 10.0f) / 8.0f, 1e-3f) && Near(turned.b[n - 1].top[0], (100.0f - 3.0f) / 8.0f, 1e-3f));
        // the snapshot full: no more
        monsterbodies::Snapshot full;
        full.n = monsterbodies::kMax - 2;
        CHECK(AddBodies(c, n, 0, feet, 0.0f, 10.0f, 8.0f, &full) == 2 && full.n == monsterbodies::kMax);
        CHECK(AddBodies(c, n, kMaxPlayers, feet, 0.0f, 10.0f, 8.0f, &snap) == 0);
        // the draw: world units from the eye, a ball given a stub, the weapon's kind
        float a[16][4], b[16][4];
        const float eye[3] = {100.0f, 36.0f, 50.0f};
        CHECK(Pack(snap.b, snap.n, 8.0f, eye, 40, 41, a, b, 16) == n);
        CHECK(Near(a[0][1], 0.0f, 1e-3f) && Near(b[0][1], 0.08f, 1e-3f) && b[0][3] == 40.0f);   // the head: at the eye, a 1 cm stub (0.08 units)
        CHECK(b[n - 1][3] == 41.0f);
        monsterbodies::Snapshot mixed;
        mixed.n = 1;
        mixed.b[0].id = 77;   // a monster: not ours to draw
        CHECK(Pack(mixed.b, mixed.n, 8.0f, eye, 40, 41, a, b, 16) == 0);
    }

    // the echo stood ahead, facing us
    {
        const float feet[3] = {10.0f, 0.0f, 10.0f};
        float out[3], h = 0.0f;
        EchoAhead(feet, 0.0f, 1.5f, 10.0f, out, &h);
        CHECK(Near(out[0], 10.0f, 1e-4f) && Near(out[2], -5.0f, 1e-4f));   // 1.5 m along -z
        CHECK(Near(std::fabs(h), poseshare::kPi, 1e-4f));
        // its weapon pointing "ahead" points back at us
        const float tipLocal[3] = {0.0f, 1.0f, -1.0f};
        float w[3];
        poseshare::ToWorld(tipLocal, out, h, 10.0f, w);
        CHECK(Near(w[2], 5.0f, 1e-3f));
    }

    // steel on steel: one knock per meeting, as loud as they closed, again only once parted and 350 ms on
    {
        Clash clash;
        Clash::Blade ours[1];
        Clash::Theirs theirs[1];
        theirs[0].c = parry::Capsule{{0.0f, 1.0f, -1.0f}, {0.0f, 1.0f, -2.0f}, 0.03f};
        theirs[0].id = WeaponId(0);
        Clash::Event ev[4];
        int knocks = 0;
        float closing = 0.0f;
        uint64_t t = 1000;
        // our blade comes in from the right at 3 m/s (0.03 m every 10 ms), crossing theirs
        for (int i = 0; i < 30; ++i, t += 10) {
            const float x = 0.45f - 0.03f * (float)i;
            ours[0].c = parry::Capsule{{x, 0.8f, -1.5f}, {x, 1.2f, -1.5f}, 0.03f};
            ours[0].hand = 0;
            const int n = clash.Step(t, ours, 1, theirs, 1, ev, 4);
            if (n) { ++knocks; closing = ev[0].closing; CHECK(ev[0].hand == 0 && ev[0].id == WeaponId(0) && ev[0].loud.heard); }
        }
        CHECK(knocks == 1);
        std::printf("clash: closing %.2f m/s\n", closing);
        CHECK(closing > 2.0f && closing < 3.6f);
        // parted, then back within 350 ms: nothing; later: a second knock
        ours[0].c = parry::Capsule{{0.5f, 0.8f, -1.5f}, {0.5f, 1.2f, -1.5f}, 0.03f};
        clash.Step(t, ours, 1, theirs, 1, ev, 4);
        t += 100;
        ours[0].c = parry::Capsule{{0.0f, 0.8f, -1.5f}, {0.0f, 1.2f, -1.5f}, 0.03f};
        CHECK(clash.Step(t, ours, 1, theirs, 1, ev, 4) == 0);
        ours[0].c = parry::Capsule{{0.5f, 0.8f, -1.5f}, {0.5f, 1.2f, -1.5f}, 0.03f};
        t += 400;
        clash.Step(t, ours, 1, theirs, 1, ev, 4);
        t += 10;
        ours[0].c = parry::Capsule{{0.0f, 0.8f, -1.5f}, {0.0f, 1.2f, -1.5f}, 0.03f};
        const int n2 = clash.Step(t, ours, 1, theirs, 1, ev, 4);
        CHECK(n2 == 1 && ev[0].loud.heard && ev[0].closing > 4.0f);   // 0.5 m in 10 ms: hard
        // laid on it gently: met, but not heard
        Clash soft;
        ours[0].c = parry::Capsule{{0.2f, 0.8f, -1.5f}, {0.2f, 1.2f, -1.5f}, 0.03f};
        soft.Step(5000, ours, 1, theirs, 1, ev, 4);
        int gentle = 0;
        for (int i = 1; i <= 20; ++i) {
            const float x = 0.2f - 0.002f * (float)i * 5.0f;   // 1 m/s... slowing: 0.01 m per 10 ms = 1 m/s; then the last steps 0.2 m/s
            ours[0].c = parry::Capsule{{x, 0.8f, -1.5f}, {x, 1.2f, -1.5f}, 0.03f};
            const int n = soft.Step(5000 + 10ull * i, ours, 1, theirs, 1, ev, 4);
            if (n) gentle += ev[0].loud.heard ? 10 : 1;
        }
        CHECK(gentle == 10);   // 1 m/s: heard (over 0.5)
    }

    // the other players among hit by touch's bodies: ids of their own (above the poses'), a slot kept while a player is near, the whole capsule a man's
    {
        for (int k = 0; k < kTouchPlayers; ++k) {
            CHECK(IsPlayerId(PlayerBodyId(k)) && !IsWeaponId(PlayerBodyId(k)));
            for (int i = 0; i < 8; ++i) CHECK(PlayerBodyId(k) != BodyId(i) && PlayerBodyId(k) != WeaponId(i));
        }
        PlayerSlots slots;
        CHECK(slots.Slot(5, 0) == 0 && slots.Slot(9, 0) == 1 && slots.Slot(5, 100) == 0);
        CHECK(slots.Slot(0, 100) == -1);   // no unit, no slot
        for (uint32_t u = 20; u < 25; ++u) CHECK(slots.Slot(u, 200) >= 0);
        CHECK(slots.Slot(99, 200) == -1);   // seven held: an eighth has none
        CHECK(slots.Slot(99, 200 + PlayerSlots::kForgetMs + 1) >= 0);   // one not seen for a while gives its slot up
        CHECK(slots.Slot(5, 200 + PlayerSlots::kForgetMs + 2) >= 0);
        stroke::Body body;
        CHECK(meleetouch::BodyFor(kPlayerRow, 100.0f, 50.0f, 0.0f, 10.0f, body));
        std::printf("a player's whole capsule at 10 units a metre: radius %.2f m, %.2f .. %.2f m\n", body.radius, body.bottom.y - body.radius, body.top.y + body.radius);
        CHECK(Near(body.top.y + body.radius, 1.7f, 0.06f) && body.radius > 0.15f && body.radius < 0.25f);
        CHECK(Near(body.bottom.x, 10.0f, 1e-4f) && Near(body.bottom.z, 5.0f, 1e-4f));
    }

    std::printf("player_bodies_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
