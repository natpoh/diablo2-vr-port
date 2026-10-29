// full_reach.h - [arms] full_reach: the drawn hand always reaches its controller. Pure maths, no game calls, no globals
// (cleanroom/tests/full_reach_test.cpp); vr/skeletons.cpp Reach is its wiring.
//
// The arm's two-bone IK (skeletons.cpp Elbow) pulls a wrist target that is farther from the shoulder than the arm is long back
// onto the arm's length: the drawn hand then stops short of the controller and the weapon it carries with it. The hero's shoulder
// joints are his own - a barbarian's stand wider apart than the player's - and fixed to his chest, so a hand reaching across the
// body (a slash from one side to the other, a two-handed hilt held at the far hip) is farther from the hero's shoulder on that side
// than from the player's, and the hand fell short the more the farther across it went: "the arms shrink as they pass the torso",
// and on a two-handed weapon the second hand, short of its place on the shaft, came off it.
//
// A real arm reaching across the body brings its shoulder along: the collarbone turns about its joint at the breastbone (forward,
// up). So, for a target out of reach: first the hero's own collarbone (the upper arm's parent bone) is turned about ITS joint toward
// the target, the least turn that brings the shoulder within reach, and never more than the breastbone joint goes (kShoulderTurnDeg);
// then, if it is still out of reach, the arm's bones are lengthened about the shoulder until the wrist stands on the target - never
// more than kMaxStretch (past that it is a tracking jump, not an arm: the hand stops short as before). Drawn bones only: no bone,
// length or place the game reads.
#pragma once

#include <algorithm>
#include <cmath>

namespace fullreach {

// The collarbone's turn at the breastbone (sternoclavicular) joint: about 20 degrees forward and well past it upward in a person -
// the shared bound, so a shoulder never comes off the chest.
constexpr float kShoulderTurnDeg = 20.0f;
// The most the arm is lengthened by: an arm drawn 35 % longer than the hero's (fitted) arm is far past a person's ape index - a
// target farther still is a controller jumping, and it is left short.
constexpr float kMaxStretch = 1.35f;
// The IK reaches 0.999 of the arm's length (skeletons.cpp Elbow); the turn aims a little inside it so the hand is not left on the edge.
constexpr float kReach = 0.998f;

struct V { float x = 0, y = 0, z = 0; };
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Mul(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// v turned by `rad` about the unit axis k (right-handed: Rodrigues).
inline V Turn(V v, V k, float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    return Add(Add(Mul(v, c), Mul(Cross(k, v), s)), Mul(k, Dot(k, v) * (1.0f - c)));
}

// The collarbone's turn: about `axis` (unit, through the collarbone's joint P) by `rad` (>= 0, right-handed: it carries the shoulder
// toward the target). turn = false: no turn (in reach already, no collarbone, nothing to turn toward).
struct Turned {
    bool turn = false;
    V axis{};
    float rad = 0.0f;
    V shoulder{};   // where the shoulder joint stands after it
};

// P the collarbone's joint (its bone's origin), S the shoulder joint (the upper arm's origin), T the wrist's target, L the arm's
// length (upper arm + forearm). Any one frame and unit for all.
inline Turned TurnShoulder(V P, V S, V T, float L, float maxDeg = kShoulderTurnDeg) {
    Turned out;
    out.shoulder = S;
    if (!Finite(P) || !Finite(S) || !Finite(T) || !std::isfinite(L) || !(L > 1e-5f) || !(maxDeg > 0.0f)) return out;
    const float reach = L * kReach;
    if (Len(Sub(T, S)) <= reach) return out;   // in reach: nothing to do
    const V u = Sub(S, P), w = Sub(T, P);
    const float r = Len(u), a = Len(w);
    if (!(r > 1e-5f) || !(a > 1e-5f)) return out;
    const float cosPhi = std::clamp(Dot(u, w) / (r * a), -1.0f, 1.0f);
    const float phi = std::acos(cosPhi);   // the angle at P between the shoulder and the target
    V axis = Cross(u, w);
    float al = Len(axis);
    if (al < 1e-6f * r * a) {
        if (cosPhi > 0.0f) return out;   // the shoulder already points straight at the target: a turn only takes it away
        // straight away from the target: any axis square to the collarbone (the one nearest the vertical's square)
        axis = Cross(u, std::fabs(u.y) < 0.9f * r ? V{0, 1, 0} : V{1, 0, 0});
        al = Len(axis);
        if (!(al > 1e-9f)) return out;
    }
    axis = Mul(axis, 1.0f / al);
    // d(theta)^2 = r^2 + a^2 - 2 r a cos(phi - theta): the least theta with d <= reach, else all the way round to the target (phi)
    const float k = (r * r + a * a - reach * reach) / (2.0f * r * a);
    const float need = k > 1.0f ? phi : phi - std::acos(std::max(-1.0f, k));
    const float rad = std::clamp(need, 0.0f, std::min(phi, maxDeg * 0.0174532925f));
    if (!(rad > 1e-6f)) return out;
    out.turn = true;
    out.axis = axis;
    out.rad = rad;
    out.shoulder = Add(P, Turn(u, axis, rad));
    return out;
}

// How much the arm is lengthened about its shoulder: the wrist as the IK left it is `wristM` from the shoulder, the target `targetM`.
// 1 = none (it is there, or nothing to go by); else the factor that puts the wrist on the target, at most `maxStretch` (a target
// farther than that is left short).
inline float Stretch(float targetM, float wristM, float maxStretch = kMaxStretch) {
    if (!std::isfinite(targetM) || !std::isfinite(wristM) || !(wristM > 1e-5f) || !(maxStretch >= 1.0f)) return 1.0f;
    if (targetM <= wristM * 1.0005f) return 1.0f;   // (a hair past it is the IK's 0.999)
    return std::min(targetM / wristM, maxStretch);
}

}  // namespace fullreach
