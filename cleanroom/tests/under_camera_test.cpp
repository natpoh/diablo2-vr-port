// Offline checks of vr/under_camera.h ([body] under_camera, VR F4): the drawn body kept under the camera through a leap and while the game's
// camera trails the hero. Played here as vrcam and the skeleton run it frame by frame - the pose (the drawn body moved to where the camera
// stands on, across the floor at once and up at the end of the pose; the hands' targets from the eye in model space), then the view built on
// the look-at (rising by the lift the pose asked for, [body] leap_camera_up) - with:
//  - the author's leap (2026-10-26, PS VR2, native OpenXR, direct input, room-scale walking: the 0.182 fix did nothing): the game lifts the
//    hero's model matrix 12 - 13.7 units over the look-at on the arc, the look-at stays on the ground and on him across the floor (no
//    camera spring on that path); the hips 0.75 over standing at most;
//  - a pad's camera spring, as the game's follow has it (CameraTranslation::RenderTick 0x7BD350), under a leap and a knockback;
// with model matrices turned, scaled and mirrored - checking that the drawn head stays exactly at the camera with it on (leap_camera_up on:
// the camera up with the arc; off: the body down at the camera), the hands exactly on the controllers through the whole leap with the eye's
// height over the feet kept for their scale (the author's headset test of the first leap fix: the leap took the arms - their targets were
// 7 - 17 units up where the game's model was), that without it the body flew metres up out from under the camera (the report), that
// walking moves nothing, the bounds, and the log's one line per leap with what was done.
// Run: build/Release/under_camera_test.exe (exit code 0 = passed)

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "../../vr/under_camera.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }

// The hero's model-to-world matrix as the renderer has it: row vectors (p' = p * M), turned `yaw` about the vertical, scaled, mirrored
// across the model's x if asked, at (x, y, z).
struct Model { float m[16]; };
static Model MakeModel(float yaw, float scale, bool mirror, float x, float y, float z) {
    const float c = std::cos(yaw), s = std::sin(yaw), mx = mirror ? -1.0f : 1.0f;
    Model r{};
    r.m[0] = mx * c * scale; r.m[2] = mx * -s * scale;
    r.m[5] = scale;
    r.m[8] = s * scale; r.m[10] = c * scale;
    r.m[12] = x; r.m[13] = y; r.m[14] = z; r.m[15] = 1.0f;
    return r;
}
static void ToWorld(const Model& M, const float p[3], float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = p[0] * M.m[k] + p[1] * M.m[4 + k] + p[2] * M.m[8 + k] + M.m[12 + k];
}
// A drawn bone moved by the move (PoseBody: newW m[12] += x, m[13] += up, m[14] += z) and drawn by the hero's real matrix.
static void Drawn(const Model& M, const float bone[3], const float move[3], float out[3]) {
    const float p[3] = {bone[0] + move[0], bone[1] + move[1], bone[2] + move[2]};
    ToWorld(M, p, out);
}
// World -> model for a direction: dotted with each row over its squared length (as vrcam takes the eye into model space).
static void ToModelDir(const Model& M, const float d[3], float out[3]) {
    for (int r = 0; r < 3; ++r) {
        const float* row = M.m + r * 4;
        out[r] = (d[0] * row[0] + d[1] * row[1] + d[2] * row[2]) / (row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
    }
}
static float Dist(const float a[3], const float b[3]) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }

// The game's follow, as RenderTick 0x7BD350 has it (settings +0x1F61 .. +0x1F78); `smoothing` off: the look-at is the hero point.
struct Spring {
    bool smoothing = true;     // cameraSmoothingEnabled (and the gate before it)
    float maxDist = 6.0f;      // cameraSpringMaxDistance
    float k = 40.0f;           // cameraSpringConstant
    float damping = 9.0f;      // cameraSpringDamping
    float mass = 1.0f;         // cameraMass
    float jump = 30.0f;        // cameraJumpDistance
    float p[3] = {};           // the look-at
    float v = 0.0f;            // the spring's speed (+0x1C)
    bool placed = false;
    void Tick(const float t[3], float dt) {
        if (!placed) { for (int i = 0; i < 3; ++i) p[i] = t[i]; placed = true; return; }
        const float d0[3] = {t[0] - p[0], t[1] - p[1], t[2] - p[2]};
        const float d = std::sqrt(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]);
        if (!smoothing || d > jump || d < 1.19209e-07f) { for (int i = 0; i < 3; ++i) p[i] = t[i]; v = 0.0f; return; }
        const float n[3] = {d0[0] / d, d0[1] / d, d0[2] / d};
        const float force = -k * d - damping * v;
        v += dt * force / mass;
        const float step = dt * v;
        for (int i = 0; i < 3; ++i) p[i] -= n[i] * step;
        const float e[3] = {p[0] - t[0], p[1] - t[1], p[2] - t[2]};
        if (std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) > maxDist)
            for (int i = 0; i < 3; ++i) p[i] = t[i] - n[i] * maxDist;
    }
};

static void TestMaths() {
    // every turn, scale and mirror: the moved model origin lands on the target, any bone keeps its place against it
    for (int mir = 0; mir < 2; ++mir)
        for (float yaw = -3.0f; yaw <= 3.1f; yaw += 0.37f)
            for (float scale : {0.93f, 1.0f, 1.4f}) {
                const Model M = MakeModel(yaw, scale, mir == 1, 8627.0f, 25.0f, 10857.0f);
                const float to[3] = {8624.3f, 12.5f, 10859.1f};
                float move[3];
                CHECK(undercam::MoveInModel(M.m, to, move));
                const float origin[3] = {0.0f, 0.0f, 0.0f}, head[3] = {0.4f, 6.6f, 0.35f}, none[3] = {0.0f, 0.0f, 0.0f};
                float w[3], a[3], b[3];
                Drawn(M, origin, move, w);
                CHECK(Dist(w, to) < 0.01f);
                Drawn(M, head, move, a);
                Drawn(M, head, none, b);
                for (int k = 0; k < 3; ++k) CHECK(Near(a[k] - b[k], to[k] - M.m[12 + k], 0.01f));
            }
    // Measure: across the floor and up apart, the bounds
    {
        const Model M = MakeModel(0.5f, 0.93f, false, 100.0f, 13.0f, 200.0f);
        float gap = -1.0f, lift = -1.0f;
        const float look[3] = {103.0f, 0.5f, 204.0f};
        CHECK(undercam::Measure(M.m, look, &gap, &lift) && Near(gap, 5.0f) && Near(lift, 12.5f));
        const float far[3] = {100.0f + undercam::kMaxGap + 1.0f, 13.0f, 200.0f};
        CHECK(!undercam::Measure(M.m, far, &gap, &lift));
        const float high[3] = {100.0f, 13.0f - undercam::kMaxGap - 1.0f, 200.0f};
        CHECK(!undercam::Measure(M.m, high, &gap, &lift));
        const float bad[3] = {NAN, 0.0f, 0.0f};
        CHECK(!undercam::Measure(M.m, bad, &gap, &lift));
    }
    // Lift: nothing under kLiftFrom (walking), the whole from twice it, eased between (no step), a lift down is none
    {
        CHECK(undercam::Lift(0.0f) == 0.0f && undercam::Lift(undercam::kLiftFrom) == 0.0f && undercam::Lift(-5.0f) == 0.0f && undercam::Lift(NAN) == 0.0f);
        CHECK(Near(undercam::Lift(2.0f * undercam::kLiftFrom), 2.0f * undercam::kLiftFrom, 1e-5f) && Near(undercam::Lift(13.0f), 13.0f, 1e-5f));
        float last = 0.0f;
        bool steady = true;
        for (float l = 0.0f; l <= 1.5f; l += 0.001f) {
            const float v = undercam::Lift(l);
            if (v < last - 1e-6f || v - last > 0.01f) steady = false;
            last = v;
        }
        CHECK(steady);
    }
    // matrices with no turn, not numbers
    {
        Model M = MakeModel(0.0f, 1.0f, false, 0.0f, 0.0f, 0.0f);
        const float to[3] = {1.0f, 0.0f, 1.0f};
        float move[3];
        M.m[4] = M.m[5] = M.m[6] = 0.0f;
        CHECK(!undercam::MoveInModel(M.m, to, move) && move[0] == 0.0f && move[1] == 0.0f && move[2] == 0.0f);
        M = MakeModel(0.0f, 1.0f, false, INFINITY, 0.0f, 0.0f);
        CHECK(!undercam::MoveInModel(M.m, to, move));
    }
}

// One leap, frame by frame, as vrcam runs it. The hero point from `from` to `to` (ground, x z) in `secs`, eased as the leap's own path; the
// game lifts his model matrix on a sine arc peaking at `peak` units; the look-at follows by `sp` (smoothing off: on him). Each frame: the view
// (the camera on the look-at, up by the lift the pose asked for with leap_camera_up), then the pose (the drawn body moved to where that
// camera stands on, the next view's lift asked for). `on`: [body] under_camera.
struct Leap {
    float worst = 0.0f;        // the drawn head's furthest from where it is with the hero standing under the camera (world units)
    float worstUp = 0.0f;      // ... of that, up
    float cameraTop = 0.0f;    // the camera's highest over the ground
    float bodyTop = 0.0f;      // the drawn origin's highest over the ground
    int lines = 0;             // log lines (trail ends)
    float worstHand = 0.0f;    // the drawn hands' furthest from the controllers (world units)
    float worstEyeHeight = 0.0f;   // the eye's height over the feet in model space against the eyes' (world units; with it on)
    undercam::TrailEnd end;
};
static Leap RunLeap(Spring sp, const float from[2], const float to[2], float secs, float peak, float yaw, bool mirror, bool on, bool cameraUp,
                    float spin = 0.0f) {   // spin: the game turning him, radians a second (Whirlwind)
    Leap r;
    const float dt = 1.0f / 45.0f;   // the author's pairs: 44.7 a second
    const float ground = 3.0f, eyeH = 6.5f;
    const float head[3] = {0.0f, eyeH / 0.93f, 0.2f};   // the bind head over the model's origin (model units: eyeH at scale 0.93)
    float lastViewLift = 0.0f, lastEyeOverLook[3] = {0.0f, eyeH, 0.0f};   // g_viewLift and g_eyeFromLook: the last view's
    undercam::Trail trail;
    const int frames = (int)((secs + 1.0f) / dt);
    for (int f = 0; f <= frames; ++f) {
        const float t = std::fmin(1.0f, f * dt / secs);
        const float e = t * t * (3.0f - 2.0f * t);
        const float hero[3] = {from[0] + (to[0] - from[0]) * e, ground, from[1] + (to[1] - from[1]) * e};
        const float arc = t < 1.0f ? peak * std::sin(3.14159265f * t) : 0.0f;
        sp.Tick(hero, dt);
        // the pose (before its pass rebuilds the view): the hero's matrix with the game's arc in it
        const float turn = yaw + spin * (f * dt);
        const Model M = MakeModel(turn, 0.93f, mirror, hero[0], hero[1] + arc, hero[2]);
        float gap = 0.0f, lift = 0.0f, move[3] = {0.0f, 0.0f, 0.0f};
        CHECK(undercam::Measure(M.m, sp.p, &gap, &lift));
        const float ask = on && cameraUp ? undercam::Lift(lift) : 0.0f;   // g_underLift
        bool moved = false;
        float eyesAt[3] = {M.m[12], M.m[13], M.m[14]};   // what the eyes are taken against (off: his matrix)
        if (on) {
            float target[3];
            undercam::Target(sp.p, ask, target);
            moved = undercam::MoveInModel(M.m, target, move);
            CHECK(moved);
            undercam::EyesOrigin(M.m, sp.p, lastViewLift, eyesAt);
        }
        // the eye as the pose puts it together: the look-at now and the last view's eye over its look-at
        const float eyeAtPose[3] = {sp.p[0] + lastEyeOverLook[0], sp.p[1] + lastEyeOverLook[1], sp.p[2] + lastEyeOverLook[2]};
        // the view: JumpLift's arc (UnderLiftForView)
        const float viewLift = ask;
        const float camera[3] = {sp.p[0], sp.p[1] + eyeH + viewLift, sp.p[2]};
        r.cameraTop = std::fmax(r.cameraTop, camera[1] - ground);
        lastViewLift = viewLift;
        for (int k = 0; k < 3; ++k) lastEyeOverLook[k] = camera[k] - sp.p[k];
        // The arms (skeletons.cpp PoseBody, as it runs): the move across the floor is in the bones before the arms, the move up only added at
        // the end (t_poseUp); the hands' targets are the eye in model space (against eyesAt) plus the controller's place from the eye. Drawn
        // through his real matrix they must land exactly on the controllers - the drawn camera plus the hand's place from it - and the eye's
        // height in model space must stay the eyes' height over the feet (the hands' scale reads it).
        {
            float eyeModel[3], d[3];
            for (int k = 0; k < 3; ++k) d[k] = eyeAtPose[k] - eyesAt[k];
            ToModelDir(M, d, eyeModel);
            for (const auto& hand : {std::array<float, 3>{1.4f, -1.6f, 2.1f}, std::array<float, 3>{-1.2f, -2.4f, 1.0f}, std::array<float, 3>{0.3f, 1.5f, 2.6f}}) {
                float handModel[3], target[3], world[3];
                ToModelDir(M, hand.data(), handModel);
                for (int k = 0; k < 3; ++k) target[k] = eyeModel[k] + handModel[k];
                target[1] += move[1];   // the end of the pose
                ToWorld(M, target, world);
                const float want[3] = {camera[0] + hand[0], camera[1] + hand[1], camera[2] + hand[2]};
                r.worstHand = std::fmax(r.worstHand, Dist(world, want));
            }
            if (on) r.worstEyeHeight = std::fmax(r.worstEyeHeight, std::fabs(eyeModel[1] * 0.93f - eyeH));
        }
        undercam::TrailEnd te;
        if (undercam::Step(trail, gap, lift, moved, on ? nullptr : "off ([body] under_camera=0)", (uint64_t)(f * dt * 1000.0f), &te)) { ++r.lines; r.end = te; }
        // the drawn head against the camera: with him standing under it, the head is the camera, turned with him
        float drawnHead[3], origin[3];
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        Drawn(M, head, move, drawnHead);
        Drawn(M, zero, move, origin);
        const Model atCam = MakeModel(turn, 0.93f, mirror, camera[0], camera[1] - eyeH, camera[2]);
        float expect[3];
        ToWorld(atCam, head, expect);
        r.worst = std::fmax(r.worst, Dist(drawnHead, expect));
        r.worstUp = std::fmax(r.worstUp, drawnHead[1] - expect[1]);
        r.bodyTop = std::fmax(r.bodyTop, origin[1] - ground);
    }
    return r;
}

static void TestHisLeap() {
    // The author's setup: direct input on PS VR2 - no camera spring (the look-at on the hero across the floor), the arc in the matrix.
    Spring direct;
    direct.smoothing = false;
    const float a[2] = {9053.4f, 11287.4f}, b[2] = {9075.4f, 11288.4f};   // Leap: 11 cells along x (his log, 09:39:11)
    {   // as 0.182 shipped it, or off: the body flies 13 units (3.5 m) up out from under the camera
        const Leap r = RunLeap(direct, a, b, 0.6f, 13.7f, 1.48f, false, false, true);
        CHECK(r.worstUp > 12.0f);
        CHECK(r.cameraTop < 6.6f);
        CHECK(r.lines == 1 && Near(r.end.lift, 13.7f, 0.1f) && r.end.gap < 0.01f && r.end.moved == 0 && r.end.why != nullptr);
    }
    {   // on, leap_camera_up on: the camera rises with the arc, the head stays exactly at it all through
        const Leap r = RunLeap(direct, a, b, 0.6f, 13.7f, 1.48f, false, true, true);
        CHECK(r.worst < 0.01f);
        CHECK(r.worstHand < 0.01f && r.worstEyeHeight < 0.01f);   // the arms on the controllers, the hands at their scale
        CHECK(r.cameraTop > 6.5f + 12.0f);
        CHECK(r.bodyTop > 12.0f);
        CHECK(r.lines == 1 && r.end.moved > 0 && r.end.left == 0 && Near(r.end.lift, 13.7f, 0.1f));
    }
    {   // on, leap_camera_up off (the default): the camera stays on the ground, the body down with it
        const Leap r = RunLeap(direct, a, b, 0.6f, 13.7f, 1.48f, false, true, false);
        CHECK(r.worst < 0.01f);
        CHECK(r.worstHand < 0.01f && r.worstEyeHeight < 0.01f);   // the arms on the controllers, the hands at their scale
        CHECK(r.cameraTop < 6.6f && r.bodyTop < 0.01f);
    }
    {   // Leap Attack: 16.6 cells, a mirrored model turned the other way, a lower arc
        const float c[2] = {500.0f, 700.0f}, d[2] = {477.0f, 676.0f};
        const Leap r = RunLeap(direct, c, d, 0.7f, 12.0f, -2.2f, true, true, true);
        CHECK(r.worst < 0.01f);
        CHECK(r.worstHand < 0.01f && r.worstEyeHeight < 0.01f);   // the arms on the controllers, the hands at their scale
    }
    {   // walking (no arc, no spring): nothing to move, no line
        const float c[2] = {0.0f, 0.0f}, d[2] = {8.0f, 3.0f};
        const Leap r = RunLeap(direct, c, d, 1.0f, 0.0f, 0.3f, false, true, true);
        CHECK(r.worst < 0.01f && r.cameraTop < 6.51f && r.bodyTop < 0.01f && r.lines == 0);
    }
}

// The arms are ours in whatever the game animates - no animation mode gives them back (the author: "the arms must stay owned by the VR
// headset"): what the game does to his model in each, and the hands on the controllers through it, the body under the camera.
static void TestEveryMode() {
    Spring direct;
    direct.smoothing = false;
    const Spring pad;
    const float a[2] = {100.0f, 100.0f};
    struct Case { const char* name; const Spring* sp; float to[2]; float secs, peak, spin; };
    const Case cases[] = {
        {"Leap", &direct, {122.0f, 101.0f}, 0.6f, 13.7f, 0.0f},
        {"Leap Attack", &direct, {83.0f, 124.0f}, 0.7f, 16.8f, 0.0f},      // his log: up to 16.8 units on a Leap Attack
        {"a knockback on a pad", &pad, {97.6f, 98.2f}, 0.15f, 0.0f, 0.0f},
        {"Whirlwind", &pad, {112.0f, 92.0f}, 1.5f, 0.0f, 12.0f},           // the game spinning him ~2 turns a second while he moves
        {"a cast, a hit, a block (in place)", &direct, {100.0f, 100.0f}, 0.5f, 0.0f, 0.0f},
        {"running", &pad, {130.0f, 100.0f}, 2.5f, 0.0f, 0.0f},
    };
    for (const Case& c : cases)
        for (int up = 0; up < 2; ++up) {
            const Leap r = RunLeap(*c.sp, a, c.to, c.secs, c.peak, 0.6f, false, true, up == 1, c.spin);
            const bool ok = r.worst < 0.01f && r.worstHand < 0.01f && r.worstEyeHeight < 0.01f;
            CHECK(ok);
            if (!ok) std::printf("  %s (leap_camera_up %d): head %.3f, hands %.3f, eye height %.3f off\n", c.name, up, r.worst, r.worstHand, r.worstEyeHeight);
        }
}

static void TestPadSpring() {
    const Spring pad;
    {   // a leap on a pad: the spring trails him across the floor as well as the arc lifting him
        const float a[2] = {1000.0f, 2000.0f}, b[2] = {1025.0f, 2026.0f};
        const Leap off = RunLeap(pad, a, b, 0.6f, 13.0f, 0.75f, false, false, true);
        CHECK(off.worst > 12.0f);
        CHECK(off.lines == 1 && off.end.gap > 4.0f && off.end.gap <= pad.maxDist + 0.01f);
        const Leap on = RunLeap(pad, a, b, 0.6f, 13.0f, 0.75f, false, true, true);
        CHECK(on.worst < 0.01f && on.worstHand < 0.01f && on.worstEyeHeight < 0.01f);
    }
    {   // a knockback: 3 units back in 0.15 s, no arc, the model mirrored
        const float a[2] = {50.0f, 60.0f}, b[2] = {47.6f, 58.2f};
        const Leap off = RunLeap(pad, a, b, 0.15f, 0.0f, -2.2f, true, false, true);
        CHECK(off.worst > 0.5f);
        const Leap on = RunLeap(pad, a, b, 0.15f, 0.0f, -2.2f, true, true, true);
        CHECK(on.worst < 0.01f && on.cameraTop < 6.51f && on.worstHand < 0.01f && on.worstEyeHeight < 0.01f);
    }
    {   // a waypoint (past cameraJumpDistance): taken at once, nothing apart
        Spring w = pad;
        const float a[3] = {0.0f, 0.0f, 0.0f}, b[3] = {500.0f, 0.0f, 0.0f};
        w.Tick(a, 0.011f);
        w.Tick(b, 0.011f);
        const Model M = MakeModel(0.0f, 0.93f, false, b[0], b[1], b[2]);
        float gap = 1.0f, lift = 1.0f;
        CHECK(undercam::Measure(M.m, w.p, &gap, &lift) && gap == 0.0f && lift == 0.0f);
    }
}

static void TestTrail() {
    using undercam::Step;
    undercam::Trail t;
    undercam::TrailEnd e;
    // a walk's small lag never starts one
    for (int i = 0; i < 100; ++i) CHECK(!Step(t, 0.6f, 0.2f, true, nullptr, 1000 + i * 11, &e));
    CHECK(!t.on);
    // a leap: up 13, across 1.5, back under half the threshold - one line, with the largest of each, how long, and what was done
    CHECK(!Step(t, 0.2f, 1.5f, true, nullptr, 2000, &e));
    CHECK(!Step(t, 1.5f, 13.0f, true, nullptr, 2200, &e));
    CHECK(!Step(t, 0.3f, 6.0f, false, "not VR first person (F4)", 2400, &e));
    CHECK(!Step(t, 0.1f, 0.7f, true, nullptr, 2500, &e));   // under the threshold, not yet under half: still on
    CHECK(Step(t, 0.1f, 0.2f, true, nullptr, 2600, &e));
    CHECK(Near(e.gap, 1.5f) && Near(e.lift, 13.0f) && e.ms == 600 && e.moved == 4 && e.left == 1);
    CHECK(e.why && e.why[0] == 'n');
    CHECK(!Step(t, 0.2f, 0.0f, true, nullptr, 2700, &e));   // over: nothing more
    CHECK(!Step(t, NAN, 0.0f, true, nullptr, 2800, &e));
    // a new one starts afresh (nothing carried over)
    CHECK(!Step(t, 2.0f, 0.0f, false, nullptr, 3000, &e));
    CHECK(Step(t, 0.0f, 0.0f, false, nullptr, 3100, &e) && Near(e.gap, 2.0f) && Near(e.lift, 0.0f) && e.ms == 100 && e.moved == 0 && e.why == nullptr);
}

int main() {
    TestMaths();
    TestHisLeap();
    TestEveryMode();
    TestPadSpring();
    TestTrail();
    std::printf("under_camera_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
