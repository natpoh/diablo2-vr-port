// doll_pose.h - [weight] monster_ragdoll_pose: the monster the game draws gives way with its doll (docs/notes/monster_ragdoll.md). Pure
// (cleanroom/tests/doll_pose_test.cpp).
//
// The doll (the weight library's D2rd*, vr/doll_lib.h) moves capsules: a limb's far end out of the game's place, the whole body less. Here those offsets
// become turns of the monster's own bones, written into its pose after the game made it (skel::Hook, the world pose at SkeletonInstance+0x20 - as the
// hero's arms are): the struck limb turns about its joint so its far end goes where the doll has it, its children with it; the whole body leans about its
// feet so its hips go where the doll has the body. As the doll's muscles bring the capsules back, the turns shrink to nothing and the game's own
// animation is all there is again. Only the drawing: the unit's place, its mode and everything the server knows stay the game's.
//
// Frames: the world pose is the model's (row vectors, a bone's axes in rows 0-2, its origin in row 3); the model stands in the world through its entity's
// transform, model x M + t (M: rotation and the model's scale, rows). The doll's offsets are render metres (world units / upm).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace dollpose {

constexpr int kMaxBones = 256;
constexpr int kMaxTurns = 8;        // the limbs of one monster that sway at once (and the lean)
constexpr float kMaxAngle = 0.7f;   // radians: no limb or lean turns further (the doll holds its offsets to 25 / 20 cm anyway)
constexpr float kLeanAt = 0.6f;     // the lean: the body's offset is where this share of its height goes

// A turn of some bones about a point: the bones in `mask` (bit i: bone i), about the origin of bone `pivot` as it is posed (-1: the model's origin, its
// feet), by `angle` about `axis` (the model's frame, unit).
struct Turn {
    int16_t pivot = -1;
    float axis[3] = {0.0f, 1.0f, 0.0f};
    float angle = 0.0f;
    uint64_t mask[kMaxBones / 64] = {};
    void Set(int i) { if (i >= 0 && i < kMaxBones) mask[i >> 6] |= 1ull << (i & 63); }
    bool Has(int i) const { return i >= 0 && i < kMaxBones && (mask[i >> 6] >> (i & 63) & 1ull); }
};

struct Pose {
    uintptr_t self = 0;     // the SkeletonInstance it is for
    uint32_t id = 0;        // the monster (the game's unit id)
    int bones = 0;          // its skeleton's bone count (a pose for another count is not applied)
    int n = 0;
    Turn t[kMaxTurns];      // the limbs' turns first, the lean last
    uint64_t tickMs = 0;
    uint64_t fadeFromMs = 0;   // let go of at this tick (0: not): eased back to the animation (Faded)
};

// What Build needs of the rig: the entity's model -> world rows (3x3), where it stands (world units), its skeleton's parents (parents come first: the game
// keeps them so), and its limb capsules (world units) by bone.
struct Rig {
    float m3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float root[3] = {};
    int bones = 0;
    const int16_t* parent = nullptr;
    struct Cap { float a[3], b[3]; int bone; };
    const Cap* caps = nullptr;
    int nCaps = 0;
};

namespace detail {
inline void Cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline float Len(const float a[3]) { return std::sqrt(Dot(a, a)); }
// v (a row) x the inverse of m3 (rows): the world -> the model's frame, for a direction.
inline bool ToModel(const float m3[9], const float v[3], float o[3]) {
    const float* m = m3;
    const float c00 = m[4] * m[8] - m[5] * m[7], c01 = m[5] * m[6] - m[3] * m[8], c02 = m[3] * m[7] - m[4] * m[6];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!(std::fabs(det) > 1e-12f)) return false;
    // inverse (rows): inv[i][j] = cofactor[j][i] / det
    const float inv[9] = {c00 / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                          c01 / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                          c02 / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    for (int j = 0; j < 3; ++j) o[j] = v[0] * inv[j] + v[1] * inv[3 + j] + v[2] * inv[6 + j];
    return true;
}
inline float Det(const float m[9]) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}
// A turn in the world (axis, angle) as the model's: the axis taken into its frame; a mirroring transform turns the other way.
inline bool AxisToModel(const float m3[9], const float axisW[3], float angle, float axisM[3], float* angleM) {
    float a[3];
    if (!ToModel(m3, axisW, a)) return false;
    const float l = Len(a);
    if (!(l > 1e-9f)) return false;
    for (int k = 0; k < 3; ++k) axisM[k] = a[k] / l;
    *angleM = Det(m3) < 0.0f ? -angle : angle;
    return true;
}
// v turned by angle about the unit axis (Rodrigues), in place.
inline void RotateAbout(const float axis[3], float angle, float v[3]) {
    const float c = std::cos(angle), s = std::sin(angle), d = Dot(axis, v);
    float x[3];
    Cross(axis, v, x);
    for (int k = 0; k < 3; ++k) v[k] = v[k] * c + x[k] * s + axis[k] * d * (1.0f - c);
}
}  // namespace detail

// Which end of a limb's capsule is its joint (the end its bone turns about): the end nearer the capsule of the nearest bone above it that has one (the shoulder
// end of an upper arm, the elbow end of a forearm, however the arm hangs); with none above it, the end nearer the monster's middle. (The middle alone was wrong for
// a hanging arm - its elbow, its wrist nearer the middle than its shoulder - and turned it the other way: the headset 0.183, «тянется в обратную сторону».)
inline bool JointAtB(const Rig& rig, const Rig::Cap& c, const float mid[3]) {
    using namespace detail;
    const Rig::Cap* above = nullptr;
    for (int p = c.bone >= 0 && c.bone < rig.bones ? rig.parent[c.bone] : -1, guard = 0; p >= 0 && p < rig.bones && !above && guard < rig.bones; p = rig.parent[p], ++guard)
        for (int i = 0; i < rig.nCaps; ++i)
            if (rig.caps[i].bone == p) { above = &rig.caps[i]; break; }
    const auto distTo = [](const float p[3], const float* a, const float* b) {   // from p to the segment a..b
        float ab[3], ap[3];
        for (int k = 0; k < 3; ++k) { ab[k] = b[k] - a[k]; ap[k] = p[k] - a[k]; }
        const float l2 = Dot(ab, ab);
        const float t = l2 > 1e-12f ? std::clamp(Dot(ap, ab) / l2, 0.0f, 1.0f) : 0.0f;
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = ap[k] - ab[k] * t;
        return Len(d);
    };
    if (above) return distTo(c.b, above->a, above->b) < distTo(c.a, above->a, above->b);
    float da = 0.0f, db = 0.0f;
    for (int k = 0; k < 3; ++k) { da += (c.a[k] - mid[k]) * (c.a[k] - mid[k]); db += (c.b[k] - mid[k]) * (c.b[k] - mid[k]); }
    return db < da;
}

// The turns for one monster: bodyOff (render metres, the doll's whole-body sway; may be null), limbs[i] = (bone, offset of its far end, render metres).
// False when nothing turns.
struct LimbOff { int bone; float off[3]; };
inline bool Build(const Rig& rig, float upm, const float* bodyOff, const LimbOff* limbs, int nLimbs, Pose* out) {
    using namespace detail;
    out->n = 0;
    out->bones = rig.bones;
    if (!(upm > 1e-3f) || rig.bones <= 0 || rig.bones > kMaxBones || !rig.parent || rig.nCaps <= 0) return false;
    float mid[3] = {}, top = -1e30f;
    for (int i = 0; i < rig.nCaps; ++i)
        for (int k = 0; k < 3; ++k) {
            mid[k] += 0.5f * (rig.caps[i].a[k] + rig.caps[i].b[k]) / static_cast<float>(rig.nCaps);
            top = k == 1 ? std::max(top, std::max(rig.caps[i].a[1], rig.caps[i].b[1])) : top;
        }
    // Each limb's far end is where its offset puts it, from its joint (the line's absolute turn): its bone is turned from the line the turns before it left it on (a
    // limb's parent's turn carries it - a chain held by the hand, [weight] grab_bodies) to that line, about its joint. (One limb alone: from its rest line.)
    float wAxis[kMaxTurns][3] = {}, wAngle[kMaxTurns] = {};   // the turns made so far, in the world
    for (int l = 0; l < nLimbs && out->n < kMaxTurns - 1; ++l) {
        const Rig::Cap* c = nullptr;
        for (int i = 0; i < rig.nCaps; ++i)
            if (rig.caps[i].bone == limbs[l].bone) c = &rig.caps[i];
        if (!c || c->bone < 0 || c->bone >= rig.bones) continue;
        const bool atB = JointAtB(rig, *c, mid);
        const float* joint = atB ? c->b : c->a;
        const float* tipEnd = atB ? c->a : c->b;
        float dir[3], want[3];
        for (int k = 0; k < 3; ++k) { dir[k] = tipEnd[k] - joint[k]; want[k] = dir[k] + limbs[l].off[k] * upm; }
        const float len = Len(dir), wl = Len(want);
        if (!(len > 1e-6f) || !(wl > 1e-6f)) continue;
        float cur[3] = {dir[0], dir[1], dir[2]};   // the line the turns before left it on
        for (int ti = 0; ti < out->n; ++ti)
            if (out->t[ti].Has(c->bone) && std::fabs(wAngle[ti]) > 1e-9f) RotateAbout(wAxis[ti], wAngle[ti], cur);
        float axisW[3];
        Cross(cur, want, axisW);
        const float al = Len(axisW);
        if (!(al > 1e-9f * len * wl)) continue;
        const float angle = std::atan2(al, Dot(cur, want));
        for (int k = 0; k < 3; ++k) axisW[k] /= al;
        Turn& t = out->t[out->n];
        t = Turn{};
        const float a = std::min(kMaxAngle, angle);
        if (!AxisToModel(rig.m3, axisW, a, t.axis, &t.angle)) continue;
        t.pivot = static_cast<int16_t>(c->bone);
        t.Set(c->bone);
        for (int i = c->bone + 1; i < rig.bones; ++i)
            if (t.Has(rig.parent[i])) t.Set(i);
        for (int k = 0; k < 3; ++k) wAxis[out->n][k] = axisW[k];
        wAngle[out->n] = a;
        ++out->n;
    }
    if (bodyOff) {   // the lean: about the feet (the model's origin), so the body at kLeanAt of its height goes where the doll has it
        float off[3] = {bodyOff[0] * upm, 0.0f, bodyOff[2] * upm};
        const float h = (top - rig.root[1]) * kLeanAt;
        const float across = Len(off);
        if (across > 1e-6f && h > 1e-6f) {
            const float up[3] = {0.0f, 1.0f, 0.0f};
            float axisW[3];
            Cross(up, off, axisW);
            for (int k = 0; k < 3; ++k) axisW[k] /= across;
            Turn& t = out->t[out->n];
            t = Turn{};
            if (AxisToModel(rig.m3, axisW, std::min(kMaxAngle, std::atan2(across, h)), t.axis, &t.angle)) {
                t.pivot = -1;
                for (int i = 0; i < rig.bones; ++i) t.Set(i);
                ++out->n;
            }
        }
    }
    return out->n > 0;
}

// The pose eased toward none: weight 1 as it is, 0 the game's animation alone (each turn's angle times it) - a monster let go of (it died, it went out of
// reach, the doll was switched off) comes back to its animation over a moment, not at once (bearstonem/d2r-pcvr's 0.3 s ease back, e865def, MIT).
inline Pose Faded(Pose p, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    for (int i = 0; i < p.n && i < kMaxTurns; ++i) p.t[i].angle *= weight;
    return p;
}

// The pose's turns on the model's world pose (n matrices of 16 floats, row vectors), in order. A pose for another bone count is not applied.
inline bool Apply(const Pose& p, float (*mats)[16], int n) {
    if (n != p.bones || n <= 0 || n > kMaxBones) return false;
    for (int ti = 0; ti < p.n; ++ti) {
        const Turn& t = p.t[ti];
        if (!(std::fabs(t.angle) > 1e-7f)) continue;
        // the turn as a matrix for row vectors: v' = v R (R the transpose of the column-vector rotation about axis)
        const float x = t.axis[0], y = t.axis[1], z = t.axis[2], c = std::cos(t.angle), s = std::sin(t.angle), k = 1.0f - c;
        const float rc[9] = {c + x * x * k, x * y * k - z * s, x * z * k + y * s,
                             y * x * k + z * s, c + y * y * k, y * z * k - x * s,
                             z * x * k - y * s, z * y * k + x * s, c + z * z * k};   // column-vector rotation
        float r[9];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r[i * 3 + j] = rc[j * 3 + i];
        float pivot[3] = {};
        if (t.pivot >= 0 && t.pivot < n)
            for (int q = 0; q < 3; ++q) pivot[q] = mats[t.pivot][12 + q];
        for (int b = 0; b < n; ++b) {
            if (!t.Has(b)) continue;
            float* m = mats[b];
            for (int row = 0; row < 3; ++row) {
                const float v[3] = {m[row * 4 + 0], m[row * 4 + 1], m[row * 4 + 2]};
                for (int j = 0; j < 3; ++j) m[row * 4 + j] = v[0] * r[j] + v[1] * r[3 + j] + v[2] * r[6 + j];
            }
            const float v[3] = {m[12] - pivot[0], m[13] - pivot[1], m[14] - pivot[2]};
            for (int j = 0; j < 3; ++j) m[12 + j] = v[0] * r[j] + v[1] * r[3 + j] + v[2] * r[6 + j] + pivot[j];
        }
    }
    return true;
}

}  // namespace dollpose
