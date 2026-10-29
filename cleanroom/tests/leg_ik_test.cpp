// Offline checks of vr/leg_ik.h: the gait's knee and the turns of the thigh and the shin - the knee always ahead of the hip-ankle line, between straight
// and kMaxFlexDeg, the kneecap facing ahead (the 2026-10-26 headset test: "sometimes the knees bend the other way"), continuous as the foot goes
// backward, sideways, straight under the hip and behind it. Run: leg_ik_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <vector>

#include "leg_ik.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using legik::V;

static V Apply(V v, const float R[9]) {   // row vector: v R
    return {v.x * R[0] + v.y * R[3] + v.z * R[6], v.x * R[1] + v.y * R[4] + v.z * R[7], v.x * R[2] + v.y * R[5] + v.z * R[8]};
}
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// One leg of a rest rig facing `ahead` (+z or turned): the hip 9.5 high, the knee 5 (a hair ahead), the ankle 0.9; thigh 4.5, shin 4.1.
struct Leg {
    V hip0, knee0, foot0, ahead;
};
static Leg RestLeg(float yaw, float side) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    auto turn = [&](V v) { return V{v.x * c + v.z * s, v.y, -v.x * s + v.z * c}; };
    return {turn({side, 9.5f, 0.0f}), turn({side * 1.05f, 5.0f, 0.12f}), turn({side * 1.1f, 0.9f, -0.05f}), turn({0.0f, 0.0f, 1.0f})};
}

struct Path {
    int bad = 0, behind = 0, past = 0, kneecap = 0, flip = 0, jump = 0, samples = 0;
    float maxFlex = 0.0f, maxJump = 0.0f;
};

// The foot taken along `pts` (in small steps between them) for a leg at rest `L`, as RemoteLegs does it: the knee by Solve, the thigh and the shin
// turned by Turn from the rest; each checked.
static Path Walk(const Leg& L, const std::vector<V>& pts, float hipDrop = 0.0f) {
    Path r;
    const float thigh = legik::Length(L.knee0 - L.hip0), shin = legik::Length(L.foot0 - L.knee0);
    const V ahead = legik::KneeAhead(L.hip0, L.knee0, L.foot0, L.ahead);
    const V h0 = legik::Hinge(L.hip0, L.foot0, ahead);
    const V kneecap0 = legik::KneeSide(legik::Unit(L.foot0 - L.hip0, {0, -1, 0}), ahead);   // the kneecap's way at rest
    const V hip = L.hip0 - V{0.0f, hipDrop, 0.0f};
    bool have = false;
    V lastKnee{}, lastHinge{}, lastWant{};
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        for (int k = 0; k < 40; ++k) {
            const float t = k / 40.0f;
            const V want = pts[i] + (pts[i + 1] - pts[i]) * t;
            legik::Bend b;
            ++r.samples;
            if (!legik::Solve(hip, thigh, shin, want, ahead, legik::kMaxFlexDeg, b)) { ++r.bad; continue; }
            // the knee ahead of the hip-ankle line (or on it, straight)
            const V axis = legik::Unit(b.foot - hip, {0, -1, 0});
            const V off = (b.knee - hip) - axis * legik::Dot(b.knee - hip, axis);
            if (legik::Dot(off, L.ahead) < -1e-3f) ++r.behind;
            if (b.flexDeg < -0.01f || b.flexDeg > legik::kMaxFlexDeg + 0.05f) ++r.past;
            r.maxFlex = std::max(r.maxFlex, b.flexDeg);
            // the bones' turns from the rest: along the new bones, and the kneecap (the thigh's front at rest) toward the knee's side, ahead
            float Rt[9], Rs[9];
            if (!legik::Turn(L.knee0 - L.hip0, h0, b.knee - hip, b.hinge, Rt) || !legik::Turn(L.foot0 - L.knee0, h0, b.foot - b.knee, b.hinge, Rs)) {
                ++r.bad;
                continue;
            }
            const V side = legik::KneeSide(axis, ahead);
            // (the thigh leans off the hip-ankle line by under 90 degrees within the bend's range, so its front keeps a share toward the knee's side)
            // (and the shin's by under 90 too, near it at the deepest bend)
            if (legik::Dot(Apply(kneecap0, Rt), side) < 0.1f || legik::Dot(Apply(kneecap0, Rs), side) < -0.2f) ++r.kneecap;
            if (legik::Dot(b.hinge, h0) < 0.3f) ++r.kneecap;   // the hinge on the body's own side (never turned round)
            if (have) {
                if (legik::Dot(b.hinge, lastHinge) < 0.98f) ++r.flip;
                const float j = legik::Length(b.knee - lastKnee);
                r.maxJump = std::max(r.maxJump, j);
                if (j > 3.0f * legik::Length(want - lastWant) + 0.05f) ++r.jump;
            }
            lastKnee = b.knee; lastHinge = b.hinge; lastWant = want; have = true;
        }
    return r;
}

int main() {
    // the turn: orthonormal, proper, carries the rest bone and hinge onto the new ones
    {
        float R[9];
        CHECK(legik::Turn({0, -1, 0}, {1, 0, 0}, {0, -0.6f, 0.8f}, {1, 0, 0}, R));
        const V a = Apply({0, -1, 0}, R);
        CHECK(Near(a.x, 0.0f, 1e-5f) && Near(a.y, -0.6f, 1e-5f) && Near(a.z, 0.8f, 1e-5f));
        const V h = Apply({1, 0, 0}, R);
        CHECK(Near(h.x, 1.0f, 1e-5f) && Near(h.y, 0.0f, 1e-5f) && Near(h.z, 0.0f, 1e-5f));
        const float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) + R[2] * (R[3] * R[7] - R[4] * R[6]);
        CHECK(Near(det, 1.0f, 1e-4f));
        // a half turn of the bone (straight down to straight up, the hinge kept): no turn round the bone - the hinge stays the hinge
        CHECK(legik::Turn({0, -1, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, R));
        const V h2 = Apply({1, 0, 0}, R);
        CHECK(Near(h2.x, 1.0f, 1e-5f));
        CHECK(!legik::Turn({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, R));
        CHECK(!legik::Turn({1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, R));   // a hinge along the bone
    }
    // at rest: the rest turn (identity), the rest knee's side kept
    {
        const Leg L = RestLeg(0.0f, -1.0f);
        const float thigh = legik::Length(L.knee0 - L.hip0), shin = legik::Length(L.foot0 - L.knee0);
        const V ahead = legik::KneeAhead(L.hip0, L.knee0, L.foot0, L.ahead);
        CHECK(ahead.z > 0.9f && Near(ahead.y, 0.0f, 1e-6f));
        legik::Bend b;
        CHECK(legik::Solve(L.hip0, thigh, shin, L.foot0, ahead, legik::kMaxFlexDeg, b));
        const V h0 = legik::Hinge(L.hip0, L.foot0, ahead);
        float R[9];
        CHECK(legik::Turn(L.knee0 - L.hip0, h0, b.knee - L.hip0, b.hinge, R));
        const V a = Apply(legik::Unit(L.knee0 - L.hip0, {}), R), w = legik::Unit(b.knee - L.hip0, {});
        CHECK(legik::Dot(a, w) > 0.9999f);
        CHECK(Near(R[0], 1.0f, 0.02f) && Near(R[4], 1.0f, 0.02f) && Near(R[8], 1.0f, 0.02f));   // near the rest turn (the rest knee is a hair off the line)
        CHECK(b.flexDeg < 20.0f);   // (the soft reach: a leg at its full stretch keeps about 12 degrees)
    }
    // the bend's range: a foot pulled up to the hip stops at kMaxFlexDeg; out of reach, straight (never past)
    {
        legik::Bend b;
        CHECK(legik::Solve({0, 9.5f, 0}, 4.5f, 4.1f, {0, 9.3f, 0.1f}, {0, 0, 1}, legik::kMaxFlexDeg, b));
        CHECK(Near(b.flexDeg, legik::kMaxFlexDeg, 0.1f));
        CHECK(legik::Solve({0, 9.5f, 0}, 4.5f, 4.1f, {0, -10.0f, 0}, {0, 0, 1}, legik::kMaxFlexDeg, b));
        CHECK(b.flexDeg >= 0.0f && b.flexDeg < 15.0f && b.knee.z >= 0.0f);   // out of reach: near straight, never past it
        CHECK(!legik::Solve({0, 9.5f, 0}, 0.0f, 4.1f, {0, 0, 0}, {0, 0, 1}, legik::kMaxFlexDeg, b));
        CHECK(!legik::Solve({0, NAN, 0}, 4.5f, 4.1f, {0, 0, 0}, {0, 0, 1}, legik::kMaxFlexDeg, b));
    }
    // the steps, both legs, the body facing four ways: forward, backward (the foot from ahead to well behind and back, lifted between), sideways
    // both ways and crossing, straight under the hip at every height, and behind and up (the heel kicked back); with the hips dropped too
    for (float yaw : {0.0f, 1.3f, 3.1f, -2.2f}) {
        const float c = std::cos(yaw), s = std::sin(yaw);
        auto turn = [&](V v) { return V{v.x * c + v.z * s, v.y, -v.x * s + v.z * c}; };
        for (float side : {-1.0f, 1.0f}) {
            const Leg L = RestLeg(yaw, side);
            const float x = side * 1.1f;
            const std::vector<V> forward = {turn({x, 0.9f, -2.0f}), turn({x, 2.0f, 0.0f}), turn({x, 0.9f, 2.5f}), turn({x, 0.9f, -2.0f})};
            const std::vector<V> backward = {turn({x, 0.9f, 2.0f}), turn({x, 1.8f, 0.0f}), turn({x, 0.9f, -3.0f}), turn({x, 0.9f, 2.0f})};
            const std::vector<V> sideways = {turn({x - 3.0f, 0.9f, 0.0f}), turn({x, 2.0f, 0.0f}), turn({x + 3.0f, 0.9f, 0.0f}), turn({-x * 0.5f, 0.9f, 0.3f})};
            for (float drop : {0.0f, 2.5f}) {
                const float top = 9.5f - drop - 1.0f;   // (a foot never above the hip walking)
                const std::vector<V> under = {turn({x * 0.9f, 0.5f, 0.0f}), turn({x * 0.9f, top, 0.0f}), turn({x * 0.9f, 3.0f, -0.01f}), turn({x * 0.9f, 0.5f, 0.0f})};
                const std::vector<V> kick = {turn({x, 0.9f, -1.0f}), turn({x, 4.0f - drop, -3.5f}), turn({x, top, -2.5f}), turn({x, 0.9f, 0.5f})};
                for (const auto* p : {&forward, &backward, &sideways, &under, &kick}) {
                    const Path r = Walk(L, *p, drop);
                    CHECK(r.samples > 0 && r.bad == 0);
                    CHECK(r.behind == 0);   // the knee never behind the hip-ankle line
                    CHECK(r.past == 0);     // between straight and kMaxFlexDeg
                    CHECK(r.kneecap == 0);  // the kneecap faces the knee's side (ahead), the thigh's and the shin's
                    CHECK(r.flip == 0);     // the hinge turns smoothly
                    CHECK(r.jump == 0);     // the knee moves on with the foot, no leap
                    if (r.behind || r.kneecap || r.flip || r.jump)
                        std::printf("  yaw %.1f side %.0f path %d drop %.1f: behind %d kneecap %d flip %d jump %d (max %.2f), max flex %.0f\n", yaw, side,
                                    (int)(p == &forward ? 0 : p == &backward ? 1 : p == &sideways ? 2 : p == &under ? 3 : 4), drop, r.behind, r.kneecap,
                                    r.flip, r.jump, r.maxJump, r.maxFlex);
                }
            }
        }
    }

    // [body] walk_back: the hero turned whole to the look (+z) while the game walks him the other way (-z, its forward walk turned half a turn with him). The
    // game's feet against his hips, a forward walk: the planted foot from +half a stride to -half along ITS forward (here +z after the turn... the game's forward
    // turned with the body is his forward), the swing back ahead, the legs half a cycle apart. As drawn before, a planted foot slid over the ground at twice
    // his speed; mirrored (MirrorAlong, whole), it stands still on the ground, the swing goes his way, and the left foot stays left of the right one - also
    // through the crossfade (w 0..1) and the knees, solved again for the mirrored feet, stay ahead of the hip-ankle line.
    {
        const float kPi = 3.14159265f, stride = 3.0f, speed = 6.0f, dt = 1.0f / 90.0f;   // model units: a stride, his speed back (units a second)
        const V forward{0.0f, 0.0f, 1.0f};
        auto gameFoot = [&](float phi, float side, float* lift) {   // a forward walk against the hips (x across, z along his forward)
            phi = std::fmod(phi, 2.0f * kPi);
            if (phi < 0.0f) phi += 2.0f * kPi;
            float along;
            if (phi < kPi) { along = 0.5f - phi / kPi; *lift = 0.0f; }
            else { const float t = (phi - kPi) / kPi; along = -0.5f + t * t * (3.0f - 2.0f * t); *lift = std::sin(kPi * t); }
            return V{side, 0.9f + *lift * 0.8f, along * stride};
        };
        for (int mirror = 0; mirror < 2; ++mirror) {
            float worstPlanted = 0.0f, worstSide = 1e9f;
            bool kneeOk = true;
            float phase = 0.0f, bodyZ = 0.0f;
            V was[2];
            bool wasPlanted[2] = {false, false};
            for (int f = 0; f < 270; ++f) {
                phase += kPi * speed / stride * dt;   // the game's stride runs on his speed
                bodyZ -= speed * dt;                  // he goes back: -z, his body facing +z
                const float w = mirror ? std::min(1.0f, f / 45.0f) : 0.0f;   // (the crossfade in over half a second)
                V world[2];
                bool planted[2];
                for (int i = 0; i < 2; ++i) {
                    float lift = 0.0f;
                    const float side = i ? 1.0f : -1.0f;   // [0] right (-x), [1] left (+x): his left is +x facing +z
                    V foot = gameFoot(phase + (i ? kPi : 0.0f), side, &lift);
                    foot = legik::MirrorAlong(foot, {0.0f, 0.0f, 0.0f}, forward, w);
                    planted[i] = lift == 0.0f;
                    world[i] = {foot.x, foot.y, foot.z + bodyZ};
                    // the knee for it, from a hip 9.5 up over the foot's own side
                    const Leg leg = RestLeg(0.0f, side);
                    legik::Bend b;
                    const V hip{side, 9.5f, 0.0f};
                    if (!legik::Solve(hip, 4.5f, 4.1f, foot, legik::KneeAhead(leg.hip0, leg.knee0, leg.foot0, forward), legik::kMaxFlexDeg, b)) kneeOk = false;
                    else {
                        const V axis = b.foot - hip, toKnee = b.knee - hip;
                        const V off = toKnee - axis * (legik::Dot(toKnee, axis) / std::max(1e-6f, legik::Dot(axis, axis)));
                        if (legik::Dot(off, forward) < -1e-3f) kneeOk = false;   // a knee behind the hip-ankle line: bent backward
                    }
                }
                worstSide = std::min(worstSide, world[1].x - world[0].x);   // the left foot's lead across over the right one
                if (f > 45)
                    for (int i = 0; i < 2; ++i)
                        if (planted[i] && wasPlanted[i]) worstPlanted = std::max(worstPlanted, std::hypot(world[i].x - was[i].x, world[i].z - was[i].z) / dt);
                for (int i = 0; i < 2; ++i) { was[i] = world[i]; wasPlanted[i] = planted[i]; }
            }
            CHECK(worstSide > 1.5f && kneeOk);   // the feet keep their sides either way (the swap was in the steps, not across)
            if (mirror) CHECK(worstPlanted < 0.05f * speed);   // a planted foot stands still on the ground
            else CHECK(worstPlanted > 1.9f * speed);           // (as before: it slid along at twice his speed - the legs walked forward while he went back)
        }
        // half way: a foot half a stride ahead goes to under the hips; whole: to half a stride behind; across and up untouched
        const V a = legik::MirrorAlong({0.4f, 1.0f, 1.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}, 0.5f);
        CHECK(Near(a.x, 0.4f, 1e-6f) && Near(a.y, 1.0f, 1e-6f) && Near(a.z, 0.0f, 1e-6f));
        const V b = legik::MirrorAlong({0.4f, 1.0f, 1.5f}, {0.0f, 0.0f, 0.5f}, {0.0f, 0.3f, 1.0f}, 1.0f);   // (about a centre half a unit ahead; the forward's up left out)
        CHECK(Near(b.z, -0.5f, 1e-5f) && Near(b.x, 0.4f, 1e-6f));
    }

    std::printf("leg_ik_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
