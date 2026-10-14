// weapon_back.h - where a weapon that was let go and has landed is DRAWN: slung on the hero's back ([weight] to_back). Pure maths.
//
// The game's weapon stays the game's equipped weapon; only its drawing moves. The weapon's attach bone is posed relative to a bone of the
// hero's body (the one the skeleton hook knows as the spine's base: the whole upper body hangs on it), so the weapon follows the body - its
// turn, its lean, a crouch - and does not shake: the place is defined once in the BIND pose (the hero standing straight, facing +z, his left
// +x), as a matrix in model space, and carried by "the bone now against the bone then". The bone's own axes need not be known.
//
// A weapon is a long one (a staff, a spear, a polearm, a javelin, anything two-handed) or a short one. A long one lies across the back
// diagonally, its tip up over the shoulder; a short one lies across the lower back, its tip to the hero's left. Each class has its own
// default and its own live tuning (x / y / z in cm and pitch / yaw / roll in degrees, in the back's axes: x to the hero's right, y up,
// z back - as the hands are told in the head's frame), so a staff and a sword can be set apart.
//
// Row-vector matrices as vr/skeletons.cpp has them: v' = v * M, the basis vectors are rows 0..2, the translation row 3 (floats 12..14).
#pragma once

#include <cmath>
#include <cstdlib>

namespace weaponback {

struct Mat { float m[16]; };
struct V3 { float x, y, z; };

enum class Class { Short, Long };

// The tuning of one class: cm and degrees, in the back's axes (x right, y up, z back; pitch about x, yaw about y, roll about the shaft).
struct Tuning { float x = 0, y = 0, z = 0, pitchDeg = 0, yawDeg = 0, rollDeg = 0; };

// The default place: where the shaft's middle lies, metres from the bone's origin in the back's axes (x right, y up, z back), and the
// direction of the tip in the same axes (unit).
struct Place { V3 at, tip; };
inline Place DefaultPlace(Class c) {
    if (c == Class::Long) return {{0.00f, 0.26f, 0.17f}, {0.50f, 0.866f, 0.0f}};    // across the back, the tip up over the right shoulder
    return {{0.08f, 0.04f, 0.15f}, {-0.98f, 0.20f, 0.0f}};                          // across the lower back, the tip to the hero's left
}

inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline V3 Norm(V3 a) { const float l = Len(a); return l > 1e-6f ? Mul(a, 1.0f / l) : V3{0, 0, 1}; }
inline V3 Row(const Mat& a, int r) { return {a.m[r * 4], a.m[r * 4 + 1], a.m[r * 4 + 2]}; }
inline V3 Pos(const Mat& a) { return {a.m[12], a.m[13], a.m[14]}; }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// v turned about the unit axis by deg degrees (right-handed, Rodrigues).
inline V3 Turn(V3 v, V3 axis, float deg) {
    const float a = deg * 0.0174532925f, c = std::cos(a), s = std::sin(a);
    return Add(Add(Mul(v, c), Mul(Cross(axis, v), s)), Mul(axis, Dot(axis, v) * (1.0f - c)));
}

inline Mat Mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k) {
            const float x = a.m[row * 4 + k];
            for (int col = 0; col < 4; ++col) r.m[row * 4 + col] += x * b.m[k * 4 + col];
        }
    return r;
}
// The inverse of an affine row-vector matrix.
inline Mat InvAffine(const Mat& a) {
    const float* m = a.m;
    const float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    const float id = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
    Mat r{};
    r.m[0] = c00 * id;  r.m[1] = (m[2] * m[9] - m[1] * m[10]) * id;  r.m[2] = (m[1] * m[6] - m[2] * m[5]) * id;
    r.m[4] = c01 * id;  r.m[5] = (m[0] * m[10] - m[2] * m[8]) * id;  r.m[6] = (m[2] * m[4] - m[0] * m[6]) * id;
    r.m[8] = c02 * id;  r.m[9] = (m[1] * m[8] - m[0] * m[9]) * id;   r.m[10] = (m[0] * m[5] - m[1] * m[4]) * id;
    for (int j = 0; j < 3; ++j) r.m[12 + j] = -(m[12] * r.m[j] + m[13] * r.m[4 + j] + m[14] * r.m[8 + j]);
    r.m[15] = 1.0f;
    return r;
}
inline float Det3(const Mat& a) {
    const float* m = a.m;
    return m[0] * (m[5] * m[10] - m[6] * m[9]) + m[1] * (m[6] * m[8] - m[4] * m[10]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

// The rotation whose columns are a0 a1 a2 (the images of x y z; orthonormal, right-handed) as a unit quaternion x y z w, with q v q* = R v.
// For telling the weight model, in its own frame, where the weapon's body frame was drawn on the back.
inline void QuatFromAxes(V3 a0, V3 a1, V3 a2, float q[4]) {
    const float r00 = a0.x, r10 = a0.y, r20 = a0.z, r01 = a1.x, r11 = a1.y, r21 = a1.z, r02 = a2.x, r12 = a2.y, r22 = a2.z;
    const float tr = r00 + r11 + r22;
    if (tr > 0.0f) {
        const float k = std::sqrt(tr + 1.0f) * 2.0f;
        q[0] = (r21 - r12) / k; q[1] = (r02 - r20) / k; q[2] = (r10 - r01) / k; q[3] = 0.25f * k;
    } else if (r00 > r11 && r00 > r22) {
        const float k = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q[0] = 0.25f * k; q[1] = (r01 + r10) / k; q[2] = (r02 + r20) / k; q[3] = (r21 - r12) / k;
    } else if (r11 > r22) {
        const float k = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q[0] = (r01 + r10) / k; q[1] = 0.25f * k; q[2] = (r12 + r21) / k; q[3] = (r02 - r20) / k;
    } else {
        const float k = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q[0] = (r02 + r20) / k; q[1] = (r12 + r21) / k; q[2] = 0.25f * k; q[3] = (r10 - r01) / k;
    }
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-6f) { for (int i = 0; i < 4; ++i) q[i] /= l; }
    else { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; }
}

// The weapon's attach bone on the back, model space, as the body is NOW.
//   boneBind, boneNow: the body bone the weapon hangs from, in the bind pose and as posed now (model space);
//   weaponNow: the weapon's attach bone as it stands now in the hand - only its scale (the length of each row) and its handedness are
//              kept (a left attach bone is a mirror: an item drawn from a mirror-less matrix would be inside out);
//   tipAxis: the attach bone's axis toward the weapon's tip, +-1..3 (x y z);
//   midM: metres from the attach bone's origin to the middle of the shaft, along the tip;
//   upm: model units per metre at the hero's size.
// Returns false (and the matrix as given) for numbers that are not usable.
inline bool BackMatrix(const Mat& boneBind, const Mat& boneNow, const Mat& weaponNow, int tipAxis, float midM, float upm, Class cls,
                       const Tuning& t, Mat* out) {
    *out = weaponNow;
    if (!(upm > 0.0f) || !std::isfinite(midM) || tipAxis == 0 || tipAxis < -3 || tipAxis > 3) return false;
    // the back's axes in the bind pose (model space: y up, the hero faces +z, his left is +x)
    const V3 right{-1, 0, 0}, up{0, 1, 0}, back{0, 0, -1};
    const Place d = DefaultPlace(cls);
    // the tip: the default turned by the tuning, in the back's axes (pitch about the hero's right, yaw about up)
    V3 tip = Norm(Add(Add(Mul(right, d.tip.x), Mul(up, d.tip.y)), Mul(back, d.tip.z)));
    tip = Norm(Turn(tip, right, t.pitchDeg));
    tip = Norm(Turn(tip, up, t.yawDeg));
    // the shaft's middle: the default and the tuning's cm
    const V3 at = Add(Add(Mul(right, d.at.x + t.x * 0.01f), Mul(up, d.at.y + t.y * 0.01f)), Mul(back, d.at.z + t.z * 0.01f));
    const V3 anchor = Add(Pos(boneBind), Mul(at, upm));
    // the weapon's frame: its tip axis along the tip, the other two square to it (the roll about the shaft from the tuning)
    const int ax = std::abs(tipAxis) - 1, a = (ax + 1) % 3, b = (ax + 2) % 3;
    const float sign = tipAxis < 0 ? -1.0f : 1.0f;
    V3 helper = up;
    if (std::fabs(Dot(helper, tip)) > 0.95f) helper = right;
    V3 ra = Norm(Cross(helper, tip));
    ra = Norm(Turn(ra, tip, t.rollDeg));
    const V3 rb = Cross(tip, ra);   // ra x rb = tip: a right-handed set in the order (a, b, ax)
    V3 rows[3];
    rows[ax] = Mul(tip, sign);
    rows[a] = ra;
    rows[b] = rb;
    if (sign < 0) {   // the row is the tip's opposite: keep the set right-handed
        rows[a] = Mul(rows[a], -1.0f);
    }
    // scale and handedness as the weapon has them now
    float len[3];
    for (int i = 0; i < 3; ++i) {
        len[i] = Len(Row(weaponNow, i));
        if (!(len[i] > 1e-6f)) len[i] = 1.0f;
    }
    if (Det3(weaponNow) < 0.0f) rows[a] = Mul(rows[a], -1.0f);
    Mat wb{};
    for (int i = 0; i < 3; ++i) {
        const V3 r = Mul(rows[i], len[i]);
        wb.m[i * 4] = r.x; wb.m[i * 4 + 1] = r.y; wb.m[i * 4 + 2] = r.z;
    }
    const V3 origin = Add(anchor, Mul(tip, -midM * upm));
    wb.m[12] = origin.x; wb.m[13] = origin.y; wb.m[14] = origin.z; wb.m[15] = 1.0f;
    *out = Mul(Mul(wb, InvAffine(boneBind)), boneNow);
    return Finite(Pos(*out));
}

}  // namespace weaponback
