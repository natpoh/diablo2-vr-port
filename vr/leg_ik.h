// leg_ik.h - the gait's legs ([multiplayer] pose_share_legs, [body] gait_legs_third / gait_legs_f4): the knee and the turns of the thigh and the shin.
// Pure maths, no game; skeletons.cpp RemoteLegs uses it, cleanroom/tests/leg_ik_test.cpp runs it.
//
// The author's first headset test of the gait on his own hero (2026-10-26): "the legs work great, except one thing: sometimes the knees bend the
// other way, not anatomically". The knee's place was always ahead of the hip-ankle line (crouch::Knee), but the bones were aimed by the shortest swing
// from the game's own walk pose (skeletons.cpp AimChain): the game's leg ahead and ours behind is a swing of near half a turn, and such a swing turns
// the bone round its own length - the kneecap to the back, the leg bent like a bird's. Here the thigh and the shin take their whole turn from the rig's
// rest pose and the leg's bend plane alone, never from the game's pose: the knee ahead of the hip-ankle line on its rest side (turned with the pelvis),
// its hinge the body's lateral axis, between straight and kMaxFlexDeg of bend (a hard limit: never past straight, never sideways), and continuous
// wherever the foot goes - under the hip, behind it, to the side.
#pragma once

#include <algorithm>
#include <cmath>

namespace legik {

struct V { float x = 0, y = 0, z = 0; };
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(V a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline V Unit(V a, V fallback) {
    const float l = Length(a);
    return l > 1e-6f && std::isfinite(l) ? a * (1.0f / l) : fallback;
}

constexpr float kMaxFlexDeg = 150.0f;   // the most a knee bends (0 straight); never past straight
constexpr float kSoftReach = 0.985f;   // the leg's reach eased from this share of straight on (no snap of the knee as the leg goes straight)

// The way this leg's knee points at rest, the body facing `ahead` (model space, y up): its rest knee's side off the rest hip-ankle line, flat on the
// ground, when that stands ahead (a rig's knees splay out a little); else `ahead`. Unit, level.
inline V KneeAhead(V hip0, V knee0, V foot0, V ahead) {
    const V flatAhead = Unit({ahead.x, 0.0f, ahead.z}, {0.0f, 0.0f, 1.0f});
    const V axis = foot0 - hip0;
    const float l2 = Dot(axis, axis);
    V side = knee0 - hip0;
    if (l2 > 1e-12f) side = side - axis * (Dot(side, axis) / l2);
    side.y = 0.0f;
    const float thigh = Length(knee0 - hip0);
    if (Finite(side) && Length(side) > 0.01f * std::max(thigh, 1e-3f) && Dot(side, flatAhead) > 0.5f * Length(side)) return Unit(side, flatAhead);
    return flatAhead;
}

// The body's lateral axis for a knee pointing `kneeAhead` (level): the knee's hinge with the leg straight down.
inline V Lateral(V kneeAhead) { return Unit({-kneeAhead.z, 0.0f, kneeAhead.x}, {-1.0f, 0.0f, 0.0f}); }

// Across the hip-ankle line toward the knee (unit): the leg's line turned a quarter forward about the body's lateral axis - ahead with the foot under
// the hip, up with the foot raised ahead, down with the heel kicked back (a pole that tilts up put that knee over the top). For every foot under the
// hip it stands ahead of the line (its share ahead is the cosine of the leg's lean), it never turns round, and it is continuous in the foot; only a
// foot straight out sideways at the hip's height (not reached walking) leaves it to `kneeAhead`.
inline V KneeSide(V axis, V kneeAhead) {
    V p = Cross(Lateral(kneeAhead), axis);
    if (Dot(p, p) < 1e-8f) p = kneeAhead - axis * Dot(kneeAhead, axis);
    if (Dot(p, p) < 1e-10f) p = Cross(axis, {1.0f, 0.0f, 0.0f});
    return Unit(p, {0.0f, 0.0f, 1.0f});
}

// The knee's hinge (unit): across the leg's bend plane, the same for the thigh and the shin. At rest and bent alike by one rule, so a leg back at its
// rest has its rest turn.
inline V Hinge(V hip, V foot, V kneeAhead) {
    const V axis = Unit(foot - hip, {0.0f, -1.0f, 0.0f});
    return Unit(Cross(axis, KneeSide(axis, kneeAhead)), {1.0f, 0.0f, 0.0f});
}

struct Bend {
    V knee, foot, hinge;
    float flexDeg = 0.0f;   // 0 straight .. kMaxFlexDeg
};

// Two-bone leg: the hip at `hip`, bones `thigh` and `shin` long, the foot wanted at `foot`, the knee's way at rest `kneeAhead` (KneeAhead). The foot
// pulled into reach along the same line, between the leg straight and bent maxFlexDeg; the knee ahead of the line. False: no leg (bad lengths, NaN).
inline bool Solve(V hip, float thigh, float shin, V foot, V kneeAhead, float maxFlexDeg, Bend& out) {
    if (!Finite(hip) || !Finite(foot) || !Finite(kneeAhead) || !std::isfinite(thigh + shin + maxFlexDeg) || thigh <= 0.01f * (thigh + shin) ||
        shin <= 0.01f * (thigh + shin))
        return false;
    const V delta = foot - hip;
    float d = Length(delta);
    const V axis = d > 1e-6f ? delta * (1.0f / d) : V{0.0f, -1.0f, 0.0f};
    const float flex = std::clamp(maxFlexDeg, 0.0f, 179.0f) * 0.017453292f;   // the inner angle at the knee is pi - flex
    const float low = std::max(std::sqrt(std::max(0.0f, thigh * thigh + shin * shin - 2.0f * thigh * shin * std::cos(3.14159265f - flex))),
                               std::fabs(thigh - shin) * 1.001f + 1e-4f);
    const float high = (thigh + shin) * 0.9999f;
    if (!(high > low)) return false;
    const float soft = std::max(low, high * kSoftReach);   // past it the reach eases to straight (its slope kept at 1 where it starts)
    if (d > soft && high > soft) d = soft + (high - soft) * (1.0f - std::exp(-(d - soft) / (high - soft)));
    d = std::clamp(d, low, high);
    const V p = KneeSide(axis, kneeAhead);
    const float along = (thigh * thigh - shin * shin + d * d) / (2.0f * d);
    out.foot = hip + axis * d;
    out.knee = hip + axis * along + p * std::sqrt(std::max(0.0f, thigh * thigh - along * along));
    out.hinge = Unit(Cross(axis, p), {1.0f, 0.0f, 0.0f});
    const float inner = std::acos(std::clamp((thigh * thigh + shin * shin - d * d) / (2.0f * thigh * shin), -1.0f, 1.0f));
    out.flexDeg = 180.0f - inner * 57.29578f;
    return Finite(out.knee) && Finite(out.hinge) && std::isfinite(out.flexDeg);
}

// The turn (row vectors, v' = v R; R row-major, 3 x 3) that carries a bone from its rest - along a0, the leg's hinge h0 - to along a1 with the hinge
// h1: the bone's frame (along, across the hinge, the hinge) at rest onto the same frame now. Never the shortest swing from somewhere else, so no turn
// round the bone's own length. False: a direction with no length, or a hinge along the bone.
inline bool Turn(V a0, V h0, V a1, V h1, float R[9]) {
    auto frame = [](V a, V h, V e[3]) {
        e[0] = Unit(a, {0.0f, 0.0f, 0.0f});
        const V c = h - e[0] * Dot(h, e[0]);
        if (Dot(e[0], e[0]) < 0.5f || Dot(c, c) < 1e-8f) return false;
        e[2] = Unit(c, {0.0f, 0.0f, 0.0f});
        e[1] = Cross(e[2], e[0]);
        return true;
    };
    V b[3], n[3];
    if (!frame(a0, h0, b) || !frame(a1, h1, n)) return false;
    const float bm[3][3] = {{b[0].x, b[0].y, b[0].z}, {b[1].x, b[1].y, b[1].z}, {b[2].x, b[2].y, b[2].z}};
    const float nm[3][3] = {{n[0].x, n[0].y, n[0].z}, {n[1].x, n[1].y, n[1].z}, {n[2].x, n[2].y, n[2].z}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R[i * 3 + j] = bm[0][i] * nm[0][j] + bm[1][i] * nm[1][j] + bm[2][i] * nm[2][j];   // B^T N
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(R[i])) return false;
    return true;
}

// [body] walk_back: the game's own walk mirrored front to back, for a hero turned with the look who walks the other way (the 2026-10-27 report on his own
// hero: "the legs cross over each other when I move backwards"). The game faces him down his path and plays its FORWARD walk; turned whole to the look, his
// legs walked forward while he went back - the planted foot slid along at twice his speed and the swinging leg passed it the wrong way. A foot's place along
// the body's forward (unit, on the ground) is reflected about the plane through `centre` square to it, w of the way (0 none, 1 whole): the planted foot then
// goes the body's way over the ground as fast as he travels - it stands still - and the swing goes back; across and up nothing changes, so the left foot stays
// left. The knee is solved again for it (Solve: ahead, never bent backward).
inline V MirrorAlong(V foot, V centre, V forward, float w) {
    const V f = Unit({forward.x, 0.0f, forward.z}, {0.0f, 0.0f, 1.0f});
    const float along = Dot(foot - centre, f);
    return foot - f * (2.0f * std::clamp(w, 0.0f, 1.0f) * along);
}

}  // namespace legik
