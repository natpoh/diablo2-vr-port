// shield_shape.h - [weight] shield_collide: the hero's own shield as the weight model meets it - its face's capsules (vr/shield_parts.h, read off the game's
// own mesh by tools/gen_shield_parts.py) put where the game draws the shield. Pure maths, no game calls (cleanroom/tests/shield_collide_test.cpp).
//
// Where a shield's mesh is drawn. The game hangs a shield on the hero's shield bone (shield_attach; a rig without it, the left weapon attach bone - the
// bones vr/skeletons.cpp straps to the forearm) through the shield definition's OverrideAttachmentTransformComponent for the hero's class
// (data/hd/items/armor/shield/<asset>.json: a translation, a rotation in degrees about x, y and z, a scale). So a point of the mesh, p, is drawn at
//
//     p * Fit(class) * Bone        (row vectors, the skeleton's own convention: the translation in m[12..14])
//
// where Fit = Scale * Rx * Ry * Rz * Translate - the scale first, then the turns about x, y, z in that order, then the move (the turns are a few degrees,
// 15 at most in the game's files, so their order changes a point by well under a millimetre per degree squared). The hero's model space is then turned
// into the hands' frame (the head's turn-only frame the controllers are in: metres from the eye, x right, y up, z back) the way the skeleton turns the
// hands into his model (skeletons.cpp ToModel, and the hands' offset): the weight model's room is built from that frame.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "shield_parts.h"

namespace shieldshape {

struct V {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Mul(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct Capsule {
    V a, b;
    float r = 0.0f;
};

// Row-vector 4x4 matrices (p' = p * M), as the skeleton's Mat.
inline void MatMul(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    for (int i = 0; i < 16; ++i) out[i] = t[i];
}
inline V Point(const float m[16], V p) {
    return {p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12], p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13], p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]};
}
// How much the matrix scales a length (the cube root of its 3x3 part's |determinant|: a mirrored bone - the left side's - scales as much as a proper one).
inline float Scale(const float m[16]) {
    const double d = (double)m[0] * ((double)m[5] * m[10] - (double)m[6] * m[9]) - (double)m[1] * ((double)m[4] * m[10] - (double)m[6] * m[8]) +
                     (double)m[2] * ((double)m[4] * m[9] - (double)m[5] * m[8]);
    return (float)std::cbrt(std::fabs(d));
}

// The class's fit as a matrix (see the top): Scale * Rx * Ry * Rz * Translate. No fit for that class (`have` 0): the identity.
inline void FitMatrix(const shieldparts::Fit& f, float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    if (!f.have) return;
    const float k = 3.14159265358979f / 180.0f;
    const auto rot = [&](int axis, float deg, float out[16]) {   // p * R turns p about the axis by deg (right-handed)
        for (int i = 0; i < 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        const float c = std::cos(deg * k), s = std::sin(deg * k);
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        out[u * 4 + u] = c;
        out[u * 4 + v] = s;
        out[v * 4 + u] = -s;
        out[v * 4 + v] = c;
    };
    float s[16] = {f.s[0], 0, 0, 0, 0, f.s[1], 0, 0, 0, 0, f.s[2], 0, 0, 0, 0, 1};
    float r[16];
    for (int axis = 0; axis < 3; ++axis) {
        rot(axis, f.rotDeg[axis], r);
        MatMul(s, r, s);
    }
    s[12] += f.t[0];
    s[13] += f.t[1];
    s[14] += f.t[2];
    for (int i = 0; i < 16; ++i) m[i] = s[i];
}

// The shield's face in his model space: the shape's capsules through the class's fit and the shield bone as drawn (`bone`: his model space, row vectors).
// cls: skel::HeroClass() (out of 0 .. 7: no fit). Returns how many (at most cap); 0 when the bone does not draw it (shrunk to a point: put away, hidden by the
// game; not finite).
inline int InModel(const shieldparts::Shape& sh, int cls, const float bone[16], Capsule* out, int cap) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(bone[i])) return 0;
    float fit[16], m[16];
    static const shieldparts::Fit kNone{};
    FitMatrix(cls >= 0 && cls < shieldparts::kClasses ? sh.fit[cls] : kNone, fit);
    MatMul(fit, bone, m);
    const float k = Scale(m), boneK = Scale(bone);
    if (!(boneK > 1e-3f) || !(k > 1e-3f)) return 0;   // (the game shrinks the bone of an item it does not draw; so do we, to put the shield away)
    int n = 0;
    for (int i = 0; i < sh.n && n < cap; ++i) {
        const shieldparts::Part& p = sh.p[i];
        Capsule c{Point(m, {p.a[0], p.a[1], p.a[2]}), Point(m, {p.b[0], p.b[1], p.b[2]}), p.r * k};
        if (!Finite(c.a) || !Finite(c.b) || !std::isfinite(c.r)) return 0;
        out[n++] = c;
    }
    return n;
}

// His model space -> the hands' frame (skeletons.cpp ToModel undone: model = eye + ToModel(hands + offset) * upm). c, s: the cosine and sine of the camera's
// yaw in his model space; upm: his model's units a metre; offset: the hands' offset (Input::handOffset).
inline V ModelToHands(V p, V eye, float upm, float c, float s, V offset) {
    const V d = Mul(Sub(p, eye), 1.0f / upm);
    const float a = d.x * c - d.z * s, b = d.x * s + d.z * c;   // (ToModel: out.x = a c + b s, out.z = -a s + b c, with a = -v.x, b = -v.z)
    return Sub(V{-a, d.y, -b}, offset);
}
// ... and back (as ToModel), for the tests and the debug view.
inline V HandsToModel(V h, V eye, float upm, float c, float s, V offset) {
    const V v = Add(h, offset);
    const float a = -v.x, b = -v.z;
    return Add(eye, Mul(V{a * c + b * s, v.y, -a * s + b * c}, upm));
}

// The face in the hands' frame, metres: each capsule of InModel turned so. Returns how many.
inline int InHands(const shieldparts::Shape& sh, int cls, const float bone[16], V eye, float upm, float c, float s, V offset, Capsule* out, int cap) {
    if (!(upm > 0.0f) || !std::isfinite(upm)) return 0;
    const int n = InModel(sh, cls, bone, out, cap);
    for (int i = 0; i < n; ++i) {
        out[i].a = ModelToHands(out[i].a, eye, upm, c, s, offset);
        out[i].b = ModelToHands(out[i].b, eye, upm, c, s, offset);
        out[i].r /= upm;
    }
    return n;
}

// The face's size for the log: its capsules' extent (metres) along the two longest ways and how thick, their radii.
struct Size {
    float longM = 0.0f, wideM = 0.0f, rMinM = 0.0f, rMaxM = 0.0f;
    V centre;
};
inline Size SizeOf(const Capsule* c, int n) {
    Size z;
    if (n <= 0) return z;
    V lo = c[0].a, hi = c[0].a;
    z.rMinM = z.rMaxM = c[0].r;
    for (int i = 0; i < n; ++i)
        for (const V& p : {c[i].a, c[i].b}) {
            lo = {std::min(lo.x, p.x - c[i].r), std::min(lo.y, p.y - c[i].r), std::min(lo.z, p.z - c[i].r)};
            hi = {std::max(hi.x, p.x + c[i].r), std::max(hi.y, p.y + c[i].r), std::max(hi.z, p.z + c[i].r)};
            z.rMinM = std::min(z.rMinM, c[i].r);
            z.rMaxM = std::max(z.rMaxM, c[i].r);
        }
    float e[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    std::sort(e, e + 3);
    z.longM = e[2];
    z.wideM = e[1];
    z.centre = Mul(Add(lo, hi), 0.5f);
    return z;
}

}  // namespace shieldshape
