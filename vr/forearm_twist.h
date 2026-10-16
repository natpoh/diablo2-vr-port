// forearm_twist.h - [hands] forearm_twist: the forearm turns with the wrist. The pure part: no game calls, no globals.
//
// In first person (F4) the arm is solved from the shoulder to the controller (skeletons.cpp Reach): the upper arm and the forearm
// are aimed by the smallest turn from the game's animated pose, and the hand gets the controller's whole turn. So a roll of the
// controller (pronation / supination) turned the hand alone on a forearm that kept the animation's roll - the wrist screwed round
// on a still arm, and a shield strapped to the arm did not follow. A real forearm does that roll itself (the radius turns round
// the ulna), little at the elbow, all of it at the wrist.
//
// Here: how far the hand is turned about the forearm's own line, against how the rig's bind pose holds the hand on the forearm
// (TwistDeg), kept continuous from frame to frame (Unwrap), and how much of it each bone along the forearm takes (Share): the
// forearm bone `share` of it, a twist bone or a shield at fraction t from the elbow to the wrist share + (1 - share) * t.
//
// Matrices are 3x3 rotations in the row-vector form skeletons.cpp uses (v' = v * M, rows = the bone's axes in the world); a
// mirrored bone (the left side of some rigs, det -1) is fine - only the turn between two of the same handedness is measured.
#pragma once

#include <cmath>

namespace forearmtwist {

// The row-vector turn by deg about the unit axis k (skeletons.cpp TurnAbout, the same numbers): right-handed about k.
inline void Turn(const float k[3], float deg, float out[9]) {
    const float t = deg * 0.0174532925f, cs = std::cos(t), sn = std::sin(t), C = 1.0f - cs;
    out[0] = cs + k[0] * k[0] * C;        out[1] = k[1] * k[0] * C + k[2] * sn; out[2] = k[2] * k[0] * C - k[1] * sn;
    out[3] = k[0] * k[1] * C - k[2] * sn; out[4] = cs + k[1] * k[1] * C;        out[5] = k[2] * k[1] * C + k[0] * sn;
    out[6] = k[0] * k[2] * C + k[1] * sn; out[7] = k[1] * k[2] * C - k[0] * sn; out[8] = cs + k[2] * k[2] * C;
}

inline void Mul3(const float a[9], const float b[9], float out[9]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}

// Each row made unit length (a bone's matrix carries its scale in the rows). False for a row of no length.
inline bool Unit(const float in[9], float out[9]) {
    for (int r = 0; r < 3; ++r) {
        const float l = std::sqrt(in[r * 3] * in[r * 3] + in[r * 3 + 1] * in[r * 3 + 1] + in[r * 3 + 2] * in[r * 3 + 2]);
        if (!(l > 1e-6f)) return false;
        for (int c = 0; c < 3; ++c) out[r * 3 + c] = in[r * 3 + c] / l;
    }
    return true;
}

// The twist of `now` against `ref` about the unit axis (world), degrees in (-180, 180]: ref turned by Turn(axis, result) has
// now's turn about that axis (the swing that is left, a wrist bending, does not count). NaN when a matrix has no turn to read.
inline float TwistDeg(const float ref[9], const float now[9], const float axis[3]) {
    float a[9], b[9];
    if (!Unit(ref, a) || !Unit(now, b)) return NAN;
    // now = ref * Y, Y = ref^T * now (ref is orthogonal: its inverse is its transpose, mirrored or not)
    float at[9];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) at[i * 3 + j] = a[j * 3 + i];
    float Y[9];
    Mul3(at, b, Y);
    // Y in row-vector form is the column-vector rotation Y^T: its quaternion (x y z w).
    const float m00 = Y[0], m01 = Y[3], m02 = Y[6], m10 = Y[1], m11 = Y[4], m12 = Y[7], m20 = Y[2], m21 = Y[5], m22 = Y[8];
    float qx, qy, qz, qw;
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        qw = 0.25f * s; qx = (m21 - m12) / s; qy = (m02 - m20) / s; qz = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        qw = (m21 - m12) / s; qx = 0.25f * s; qy = (m01 + m10) / s; qz = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        qw = (m02 - m20) / s; qx = (m01 + m10) / s; qy = 0.25f * s; qz = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        qw = (m10 - m01) / s; qx = (m02 + m20) / s; qy = (m12 + m21) / s; qz = 0.25f * s;
    }
    const float along = qx * axis[0] + qy * axis[1] + qz * axis[2];
    if (std::fabs(along) < 1e-7f && std::fabs(qw) < 1e-7f) return NAN;   // a half turn square to the axis: no twist to read
    float deg = 2.0f * std::atan2(along, qw) * 57.2957795f;
    while (deg > 180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// The twist kept continuous: of now + 360 k the one nearest the last frame's, so a roll past half a turn from the bind pose
// does not flip the forearm round. Held to +-limit (a forearm turns about 90 degrees either way from its middle; the bind
// pose's middle is not the real one, so the limit is generous); past it the value read this frame starts afresh.
inline float Unwrap(float last, float now, float limit = 240.0f) {
    if (!std::isfinite(now)) return last;
    if (!std::isfinite(last)) return now;
    float v = now;
    while (v - last > 180.0f) v -= 360.0f;
    while (v - last < -180.0f) v += 360.0f;
    return std::fabs(v) > limit ? now : v;
}

// How much of the twist a bone at fraction t (0 the elbow, 1 the wrist) along the forearm takes: the forearm bone itself
// (t = 0) `share`, the wrist all of it.
inline float Share(float share, float t) {
    const float s = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
    const float u = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return s + (1.0f - s) * u;
}

}  // namespace forearmtwist
