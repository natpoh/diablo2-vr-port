// weapon_shaft.h - the weapon's shaft as DRAWN, against the weight model's body. Pure maths.
//
// Hit by touch builds its blade from the weapon's attach bone as the finished pose draws it: the bone's origin and its Y axis, the tip a kind's (and base
// item's) length ahead of the origin on the side tipSign says, the butt behind it (vr/blade_shape.h FromBone, ForItem). The weight model's shaft - the line its
// grips take the weapon by, the red line the debug draws, the lever and the lag - must lie on that very line, so it is made from the same bone: this measures
// the bone against the weight model's body frame (the first fist's frame), each pose, and Hold::SetDrawnShaft (cleanroom/heft/hold.h) puts the shaft there. Not
// the first guess from an axis choice ([hands] staff_shaft_axis: the old logic's, weight off), not a per-kind constant of the weight model's own: the line a
// grip can take is where the staff IS.
//
// Matrices are row-vector 4x4 as vr/skeletons.cpp has them (v' = v * M, the basis vectors are rows 0..2, the translation row 3), both in one frame.
// `upm` is that frame's units per metre (1 for a frame in metres).
#pragma once

#include <cmath>

namespace weaponshaft {

struct Line {
    bool ok = false;
    double at[3] = {0, 0, 0};    // where the shaft passes the body's origin (the point of it nearest the origin), metres in the body's frame
    double dir[3] = {0, 1, 0};   // toward the tip, unit, in the body's frame
    double butt = -0.1, tip = 0.9;   // the wood's ends, metres along dir from `at` (butt <= tip)
    double boneAlong = 0.0;      // the bone's origin, metres along dir from `at`
};

namespace detail {
// The inverse of an affine row-vector matrix (its 3x3 by the full inverse), as skeletons.cpp's InvAffine.
inline bool InvAffine(const float a[16], double r[16]) {
    const double c00 = a[5] * a[10] - a[6] * a[9], c01 = a[6] * a[8] - a[4] * a[10], c02 = a[4] * a[9] - a[5] * a[8];
    const double det = a[0] * c00 + a[1] * c01 + a[2] * c02;
    if (!(std::fabs(det) > 1e-12)) return false;
    const double id = 1.0 / det;
    r[0] = c00 * id;  r[1] = (a[2] * a[9] - a[1] * a[10]) * id;  r[2] = (a[1] * a[6] - a[2] * a[5]) * id;  r[3] = 0;
    r[4] = c01 * id;  r[5] = (a[0] * a[10] - a[2] * a[8]) * id;  r[6] = (a[2] * a[4] - a[0] * a[6]) * id;  r[7] = 0;
    r[8] = c02 * id;  r[9] = (a[1] * a[8] - a[0] * a[9]) * id;   r[10] = (a[0] * a[5] - a[1] * a[4]) * id; r[11] = 0;
    for (int j = 0; j < 3; ++j) r[12 + j] = -(a[12] * r[j] + a[13] * r[4 + j] + a[14] * r[8 + j]);
    r[15] = 1;
    return true;
}
}  // namespace detail

// The shaft of the weapon whose attach bone is `bone`, in the frame of `body`. tipAxis: the bone's axis toward the tip, +-1..3 (x y z) - the blade's: +2 or
// -2 with bladeshape::Shape::tipSign. tipM, buttM: the wood's extent ahead of and behind the bone's origin, metres (bladeshape::ForItem, tuned).
inline Line Relative(const float bone[16], const float body[16], double upm, int tipAxis, double tipM, double buttM) {
    Line out;
    const int ax = std::abs(tipAxis) - 1;
    if (ax < 0 || ax > 2 || !(upm > 1e-6) || !(tipM + buttM > 1e-3)) return out;
    double inv[16];
    if (!detail::InvAffine(body, inv)) return out;
    // rel = bone * inv(body): the bone in the body's coordinates
    double rel[16] = {};
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k)
            for (int col = 0; col < 4; ++col) rel[row * 4 + col] += (double)bone[row * 4 + k] * inv[k * 4 + col];
    double d[3] = {rel[ax * 4], rel[ax * 4 + 1], rel[ax * 4 + 2]};
    const double dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dl > 1e-9)) return out;
    const double sign = tipAxis < 0 ? -1.0 : 1.0;
    for (int i = 0; i < 3; ++i) d[i] = d[i] / dl * sign;
    const double p[3] = {rel[12] / upm, rel[13] / upm, rel[14] / upm};
    const double along = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
    if (!std::isfinite(along + p[0] + p[1] + p[2])) return out;
    for (int i = 0; i < 3; ++i) {
        out.dir[i] = d[i];
        out.at[i] = p[i] - d[i] * along;
    }
    out.boneAlong = along;
    out.tip = along + tipM;
    out.butt = along - buttM;
    out.ok = true;
    return out;
}

// [gameplay] melee_head_volume for the weight model ([weight] dual_collide): the head's own volume (vr/metal_head.h Volume: two ends and a radius in the weapon
// model's units, its own x y z - the attach bone's axes) where the drawn `bone` puts it, in the frame of `body`: metres, the same frame Relative's line is in.
// The radius by the bone's Y row (the model's unit as drawn). False: a matrix that cannot be inverted, an axis with no length.
struct Head {
    double a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
    double radius = 0.0;
};
inline bool HeadIn(const float bone[16], const float body[16], double upm, const float va[3], const float vb[3], float vr, Head* out) {
    if (!(upm > 1e-6) || !(vr > 0.0f) || !out) return false;
    double inv[16];
    if (!detail::InvAffine(body, inv)) return false;
    double rel[16] = {};
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k)
            for (int col = 0; col < 4; ++col) rel[row * 4 + col] += (double)bone[row * 4 + k] * inv[k * 4 + col];
    const double unit = std::sqrt(rel[4] * rel[4] + rel[5] * rel[5] + rel[6] * rel[6]) / upm;   // metres per model unit, as drawn
    if (!(unit > 1e-6) || !std::isfinite(unit)) return false;
    for (int i = 0; i < 3; ++i) {   // p * rel: the model's point carried by the bone, in the body's units, then metres
        out->a[i] = (va[0] * rel[i] + va[1] * rel[4 + i] + va[2] * rel[8 + i] + rel[12 + i]) / upm;
        out->b[i] = (vb[0] * rel[i] + vb[1] * rel[4 + i] + vb[2] * rel[8 + i] + rel[12 + i]) / upm;
    }
    out->radius = vr * unit;
    return std::isfinite(out->a[0] + out->a[1] + out->a[2] + out->b[0] + out->b[1] + out->b[2] + out->radius);
}

}  // namespace weaponshaft
