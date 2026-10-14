// grip_fit.h - [weight] grip_fit: the hand that TAKES a weapon sits on its shaft the way a base grip does, with live correction sliders. Pure maths.
//
// The skeleton hook draws a hand on the weapon from where the weight model has it (the controller's pose moved onto the shaft: Hold::OnWeapon). What that gives
// depends on the rig, on [hands] wrist and on fist_x/y/z - the drawn palm could float beside the shaft and the knuckles lie askew, and the user could only
// move BOTH hands the same way. This lays the drawn hand on the weapon as it is DRAWN (the weapon bone's own axis - the line the debug draws red):
//   - snap:  the palm's middle (wrist to middle knuckle, halfway - the point every grip in the hook is laid by) goes onto the shaft's axis at its grasp point
//            (where the palm stands along it now); fist_x/y/z keep deciding WHERE along the shaft the hand takes it;
//   - align: the line through the knuckles (little finger to index) is laid along the shaft by the smallest turn (the roll about the shaft stays the controller's);
//   - then the user's corrections, per hand, in the SHAFT'S OWN FRAME at the grasp point: along (toward the tip), across (toward the fingertips), up (out of the palm),
//     the hand turned as it holds - so a correction found for one grip is right at any roll; and a roll about the shaft and the two tilts about the other two axes.
// Not snapped / not aligned (the hand the game holds the weapon in: its base grip is the game's): the transform is the corrections alone, about the grasp point.
//
// Frames: any right-handed space the hook has (model space), row vectors (v' = v * R: the rows of R are the images of x, y, z). The hand is a rigid body; Solve
// returns the transform of its bone: p' = pivot + (p - pivot) * R + shift, for a point p and for the rows of the bone's matrix (row_i' = row_i * R).
#pragma once

#include <algorithm>
#include <cmath>

namespace gripfit {

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x + a.y + a.z); }
// the unit vector; false (and `fallback`) for one too short to have a direction
inline bool Unit(V3 a, V3* out, V3 fallback = {0.0f, 0.0f, 1.0f}) {
    const float l = Len(a);
    if (!(l > 1e-6f)) { *out = fallback; return false; }
    *out = Mul(a, 1.0f / l);
    return true;
}

// A 3x3 rotation, row-vector form: v' = v * R.
struct Rot {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
inline V3 Apply(V3 v, const Rot& r) {
    return {v.x * r.m[0] + v.y * r.m[3] + v.z * r.m[6], v.x * r.m[1] + v.y * r.m[4] + v.z * r.m[7], v.x * r.m[2] + v.y * r.m[5] + v.z * r.m[8]};
}
// first A, then B: v * A * B
inline Rot Then(const Rot& a, const Rot& b) {
    Rot r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}
// The turn by `deg` about the unit axis k (right-handed: a vector is carried toward k x v).
inline Rot About(V3 k, float deg) {
    const float t = deg * 0.0174532925f, cs = std::cos(t), sn = std::sin(t), C = 1.0f - cs;
    Rot r;
    r.m[0] = cs + k.x * k.x * C;       r.m[1] = k.y * k.x * C + k.z * sn; r.m[2] = k.z * k.x * C - k.y * sn;
    r.m[3] = k.x * k.y * C - k.z * sn; r.m[4] = cs + k.y * k.y * C;       r.m[5] = k.z * k.y * C + k.x * sn;
    r.m[6] = k.x * k.z * C + k.y * sn; r.m[7] = k.y * k.z * C - k.x * sn; r.m[8] = cs + k.z * k.z * C;
    return r;
}
// The smallest turn taking direction a onto b.
inline Rot Between(V3 a, V3 b) {
    V3 ua, ub;
    if (!Unit(a, &ua) || !Unit(b, &ub)) return Rot{};
    const V3 axis = Cross(ua, ub);
    const float s = Len(axis), c = Dot(ua, ub);
    if (s < 1e-6f) {
        if (c > 0.0f) return Rot{};
        V3 p;   // opposite: a half turn about any axis square to a
        Unit(std::fabs(ua.x) < 0.9f ? Cross(ua, {1, 0, 0}) : Cross(ua, {0, 1, 0}), &p);
        return About(p, 180.0f);
    }
    return About(Mul(axis, 1.0f / s), std::atan2(s, c) * 57.2957795f);
}

// One hand's tuning: along, across, up in cm; roll about the shaft, tilt about `across`, tilt about `up` in degrees. The left hand's turns are mirrored (the same numbers
// make the mirror image of the right hand's), as [hands] wrist's yaw and roll are.
struct Tuning {
    float off[3] = {0.0f, 0.0f, 0.0f};
    float rot[3] = {0.0f, 0.0f, 0.0f};
    bool Any() const { return off[0] != 0.0f || off[1] != 0.0f || off[2] != 0.0f || rot[0] != 0.0f || rot[1] != 0.0f || rot[2] != 0.0f; }
};

// The drawn hand: its wrist bone's position, the palm's middle (halfway wrist to middle knuckle), and its own axes read off the knuckles as Fist and
// PlanWrist do: point = wrist -> middle knuckle, knuckles = little finger -> index knuckle, out = out of the palm (point x knuckles, mirrored on the right).
struct Hand {
    V3 wrist, palm, point, knuckles, out;
    int side = 0;   // 0 right, 1 left
};
struct Shaft {
    V3 at, dir;     // a point on the axis, the direction toward the tip
};
struct Mode {
    bool snap = false;    // the palm's middle onto the axis, at the grasp point
    bool align = false;   // the knuckle line along the shaft
};

struct Out {
    bool ok = false;
    V3 pivot;             // the palm's middle as it was
    Rot r;                // the hand's turn (bone rows: row_i' = row_i * r)
    V3 shift;             // p' = pivot + (p - pivot) * r + shift
    V3 grasp;             // the point of the axis the palm stands at (along the shaft where it was)
    V3 anchor;            // the palm's middle after: grasp plus the offsets
    V3 axes[3];           // along, across, up as the corrected hand holds
    float offAxis = 0.0f; // how far the palm's middle was off the axis (input units)
    float alignDeg = 0.0f;// how far the knuckle line was off the shaft (degrees, 0..90)
};

// The shaft's own frame at a hand's grasp, tied to the hand: along the shaft (toward the tip), across = the fingers' way (square to it), up = out of the palm - of the hand
// as it is once its knuckle line is laid along the shaft (align), else as it is.
struct Frame {
    bool ok = false;
    V3 d, across, up;     // unit, mutually square
    Rot r1;               // the turn about the palm that lays the knuckle line along the shaft (identity without align)
    V3 grasp;             // where the palm stands on the axis (the foot of the palm's perpendicular)
    float offAxis = 0.0f; // how far the palm's middle is off the axis (input units)
    float alignDeg = 0.0f;// how far the knuckle line is off the shaft (degrees, 0..90)
};
inline Frame FrameOf(const Hand& hand, const Shaft& shaft, bool align) {
    Frame f;
    V3 d;
    if (!Finite(hand.palm) || !Finite(hand.wrist) || !Finite(shaft.at) || !Unit(shaft.dir, &d)) return f;
    V3 f0, k0, n0;
    if (!Unit(hand.point, &f0) || !Unit(hand.knuckles, &k0) || !Unit(hand.out, &n0)) return f;
    f.d = d;
    f.alignDeg = std::acos(std::clamp(std::fabs(Dot(k0, d)), 0.0f, 1.0f)) * 57.2957795f;
    f.grasp = Add(shaft.at, Mul(d, Dot(Sub(hand.palm, shaft.at), d)));
    f.offAxis = Len(Sub(hand.palm, f.grasp));
    // the knuckle line laid along the shaft (either way round: the roll stays the controller's)
    f.r1 = align ? Between(k0, Dot(k0, d) >= 0.0f ? d : Mul(d, -1.0f)) : Rot{};
    const V3 f1 = Apply(f0, f.r1), n1 = Apply(n0, f.r1);
    if (!Unit(Sub(f1, Mul(d, Dot(f1, d))), &f.across)) {   // the fingers along the shaft: the palm's way gives the frame instead
        V3 nn;
        Unit(Sub(n1, Mul(d, Dot(n1, d))), &nn);
        f.across = Cross(nn, d);
    }
    if (!Unit(Sub(Sub(n1, Mul(d, Dot(n1, d))), Mul(f.across, Dot(n1, f.across))), &f.up)) Unit(Cross(d, f.across), &f.up);
    f.ok = true;
    return f;
}

// unitsPerCm: the space's units per centimetre (the tuning's cm into it).
inline Out Solve(const Hand& hand, const Shaft& shaft, const Tuning& t, const Mode& mode, float unitsPerCm) {
    Out out;
    const Frame fr = FrameOf(hand, shaft, mode.align);
    if (!fr.ok) return out;
    const V3 d = fr.d, across = fr.across, up = fr.up;
    // The turns are the frame's: its handedness decides which way a positive angle goes, so the same numbers make the same lean in the frame (along, across, up) on either hand.
    // On the usual grip (the knuckle line toward the tip) that is the mirror rule of the wrist offsets - the left's turns reversed; a hand held the other way round (its knuckles
    // toward the butt) has the frame the other way round, and so do its turns.
    const float mir = Dot(Cross(d, across), up) >= 0.0f ? 1.0f : -1.0f;
    out.pivot = hand.palm;
    out.alignDeg = fr.alignDeg;
    out.grasp = fr.grasp;       // where the palm stands on the axis, and how far off it
    out.offAxis = fr.offAxis;
    // the corrections' turn about the grasp: the roll about the shaft, then the tilt about `across`, then the tilt about `up` - all three about the axes as the hand
    // holds BEFORE the correction (fixed axes, one after the other)
    const Rot rc = Then(Then(About(d, mir * t.rot[0]), About(across, mir * t.rot[1])), About(up, mir * t.rot[2]));
    out.r = Then(fr.r1, rc);
    // the carried axes: where they point after the tilts
    out.axes[0] = d;
    out.axes[1] = Apply(across, rc);
    out.axes[2] = Apply(up, rc);
    const V3 delta = Mul(Add(Add(Mul(d, t.off[0]), Mul(across, t.off[1])), Mul(up, t.off[2])), unitsPerCm);
    // p1 = palm + (p - palm) r1 [about the palm]; p2 = p1 + (grasp - palm) when snapped; p3 = grasp + (p2 - grasp) rc; p4 = p3 + delta
    //   snapped: p4 = grasp + (p - palm) (r1 rc) + delta;  else the palm stays where it was and the tilts turn it about the grasp point.
    const V3 base = mode.snap ? out.grasp : Add(out.grasp, Apply(Sub(hand.palm, out.grasp), rc));
    out.shift = Add(Sub(base, hand.palm), delta);
    out.anchor = Add(hand.palm, out.shift);
    out.ok = Finite(out.shift) && std::isfinite(out.r.m[0] + out.r.m[4] + out.r.m[8]);
    return out;
}

// How a hand sits on the shaft, as the Tuning that carries a hand FITTED on it (palm's middle on the axis, knuckle line along it) to this hand's pose - measured on the hand
// the game laid (the base grip: its own animation), so another hand given the same Tuning (by Solve with snap and align) sits on the shaft the same way: the same offset of the
// palm from the axis across and up the hand, the same lean of the knuckle line. Expressed for the right hand (the left's turns are un-mirrored): the same numbers on either side
// make mirror images. The along-the-shaft part is 0 - where a hand holds it is its own. ok false: the hand or the shaft is no use.
inline Tuning Relation(const Hand& hand, const Shaft& shaft, bool* ok = nullptr) {
    Tuning t;
    if (ok) *ok = false;
    const Frame fr = FrameOf(hand, shaft, true);
    if (!fr.ok) return t;
    const V3 off = Sub(hand.palm, fr.grasp);
    t.off[0] = 0.0f;
    t.off[1] = Dot(off, fr.across);   // (input units; the caller scales to cm)
    t.off[2] = Dot(off, fr.up);
    // the turn back from the fitted hand to this one is r1 undone; in the frame (d, across, up) it is M = E * r1^T * E^T, and the corrections' order is roll about d, tilt about
    // across, tilt about up: M = Rx(r) Ry(b) Rz(c) in the frame's COORDINATES - which is what Solve takes (it turns the coordinates' way by the frame's handedness, so the
    // numbers carry to any hand, mirrored or held the other way round, and sit the same way on the shaft)
    const V3 e[3] = {fr.d, fr.across, fr.up};
    Rot rt;   // r1 transposed (its inverse)
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) rt.m[i * 3 + j] = fr.r1.m[j * 3 + i];
    float M[3][3];
    for (int i = 0; i < 3; ++i) {
        const V3 img = Apply(e[i], rt);   // the image of basis vector i
        M[i][0] = Dot(img, e[0]); M[i][1] = Dot(img, e[1]); M[i][2] = Dot(img, e[2]);
    }
    // column-convention matrix of the coordinate turn is M transposed: C = M^T = Rz(c) Ry(b) Rx(r)
    const float C20 = M[0][2], C10 = M[0][1], C00 = M[0][0], C21 = M[1][2], C22 = M[2][2];
    const float r = std::atan2(C21, C22), b = -std::asin(std::clamp(C20, -1.0f, 1.0f)), c = std::atan2(C10, C00);
    t.rot[0] = r * 57.2957795f;
    t.rot[1] = b * 57.2957795f;
    t.rot[2] = c * 57.2957795f;
    if (ok) *ok = Finite({t.off[1], t.off[2], t.rot[0]}) && std::isfinite(t.rot[1] + t.rot[2]);
    return t;
}

// What the skeleton hook does to a hand on the weapon, from the state it is in. grabs: how many times that hand has closed its fist on the shaft (weight::Out::grabs; 0 = the
// hand the game put the weapon in, as placed: its own base grip). user: the user's corrections FOR THAT HAND (the right hand's, the left's - not a role's). like: the base grip's
// Relation (cm) when it is known and the taking hand is to sit on the shaft as the base hand does; null: the palm exactly on the axis.
//   - the base grip (grabs 0): not snapped, not aligned - the game's own pose, only the user's corrections (nothing to do when there are none);
//   - a hand that took it (grabs > 0), whichever hand and whichever of them carries the weapon: snapped and aligned, then the base's relation (if wanted) plus the user's corrections.
struct Plan {
    bool act = false;
    Mode mode;
    Tuning tune;
    bool likeBase = false;   // the base grip's relation is in `tune`
};
inline Plan PlanFor(unsigned grabs, const Tuning& user, bool wantLike, const Tuning* like) {
    Plan p;
    if (grabs == 0) {
        p.act = user.Any();
        p.tune = user;
        return p;
    }
    p.act = true;
    p.mode = Mode{true, true};
    p.tune = user;
    if (wantLike && like) {
        for (int i = 0; i < 3; ++i) { p.tune.off[i] += like->off[i]; p.tune.rot[i] += like->rot[i]; }
        p.likeBase = true;
    }
    return p;
}

// The same Relation with its offsets in centimetres (from input units).
inline Tuning InCm(Tuning t, float unitsPerCm) {
    if (unitsPerCm > 1e-9f) for (int i = 0; i < 3; ++i) t.off[i] /= unitsPerCm;
    return t;
}

// How far a hand on the weapon closes for the controller's squeeze (0..1; < 0 not known): fully for a tight grip, never less than a little over half while it
// still holds (a grip is held from 0.6 down to 0.4), so the fingers close as it tightens and do not open round the shaft.
inline float Close(float squeeze) {
    if (!(squeeze >= 0.0f)) return 1.0f;
    return 0.55f + 0.45f * std::clamp((squeeze - 0.4f) / 0.6f, 0.0f, 1.0f);
}

}  // namespace gripfit
