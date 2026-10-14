// grip_pose.h - the hand that TAKES a weapon gets the game's own weapon-grip pose of the fingers and the thumb. Pure maths: no game, no skeleton.
//
// The hand the game holds the weapon in has the animation's fingers: wrapped round the shaft, the thumb on it - the game's own grip pose, which wraps better than any generic curl
// ([hands] fist's table). The hand that takes the weapon by its grip had only that generic curl, closing and opening with the squeeze (and none at all with the fist tick off).
// Here the pose is READ off the game's hand as it is drawn - every finger and thumb bone's world orientation, in the HAND'S OWN FRAME (the wrist, and the knuckles' lines) - and
// put on the other hand's bones the same way, whichever hand it is and however the rig labels its bones.
//
// How a pose crosses from one hand to the other without knowing the rig's bone axes:
//   - a hand's frame E (rows: along the fingers f, along the knuckles k, out of the palm n) is made from its wrist and its three first knuckles, with the chirality the hook's palm
//     way has (n = f x k, turned over on the right hand): the two hands' frames are MIRROR IMAGES, so the same coordinates in them are mirror images;
//   - a bone's world orientation R (unit rows = its axes) in that frame: C = R E^T (its axes as coordinates in the hand);
//   - the rig's two hands label their bones' axes in ways of their own (a left bone is a mirror, its axes may be swapped or turned over): read from the BIND pose, where both hands
//     are open - the relabelling P = A_taker A_base^T, A = the bind pose's C of a bone;
//   - the taking hand's bone: C' = P C (the pose's), R' = C' E' (back to the world in the taking hand's own frame). An open pose (the base's own bind) carries to the taker's own bind.
// The bones' places follow from the chain (the bind pose's own local offsets, the new local rotations R'_child R'_parent^T): the taking hand's own finger lengths.
//
// Row-vector convention, 3x3 as 9 floats, rows = vectors (the skeleton's bone matrices' rotation block).
#pragma once

#include <cmath>
#include <vector>

namespace grippose {

struct V { float x = 0, y = 0, z = 0; };
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V Scale(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline bool Unit(V a, V* out) {
    const float l = Len(a);
    if (!(l > 1e-9f) || !std::isfinite(l)) return false;
    *out = Scale(a, 1.0f / l);
    return true;
}

struct M3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    V Row(int i) const { return {m[i * 3], m[i * 3 + 1], m[i * 3 + 2]}; }
    void SetRow(int i, V v) { m[i * 3] = v.x; m[i * 3 + 1] = v.y; m[i * 3 + 2] = v.z; }
};
inline M3 Mul(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}
inline M3 Transpose(const M3& a) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[j * 3 + i];
    return r;
}
inline float Det(const M3& a) {
    return a.m[0] * (a.m[4] * a.m[8] - a.m[5] * a.m[7]) - a.m[1] * (a.m[3] * a.m[8] - a.m[5] * a.m[6]) + a.m[2] * (a.m[3] * a.m[7] - a.m[4] * a.m[6]);
}
inline bool Finite(const M3& a) {
    for (float v : a.m)
        if (!std::isfinite(v)) return false;
    return true;
}

// The hand's frame: f along the fingers (wrist -> middle knuckle), k along the knuckles (little finger -> index, made square to f), n = f x k turned over on the right hand (side 0),
// as the hook's palm way is. False when the hand's bones give no frame.
inline bool HandFrame(V wrist, V mid, V ind, V pnk, int side, M3* out) {
    V f, k, n;
    if (!Unit(Sub(mid, wrist), &f)) return false;
    const V row = Sub(ind, pnk);
    if (!Unit(Sub(row, Scale(f, Dot(row, f))), &k)) return false;
    if (!Unit(Cross(f, k), &n)) return false;
    if (side == 0) n = Scale(n, -1.0f);
    out->SetRow(0, f); out->SetRow(1, k); out->SetRow(2, n);
    return true;
}

// A bone's orientation: the unit rows of its world matrix's rotation block (a bone's scale and handedness stay: the rows' directions only). `m` the 4x4 as 16 floats.
inline bool Orientation(const float m[16], M3* out) {
    for (int i = 0; i < 3; ++i) {
        V r{m[i * 4], m[i * 4 + 1], m[i * 4 + 2]}, u;
        if (!Unit(r, &u)) return false;
        out->SetRow(i, u);
    }
    return true;
}

inline M3 Coords(const M3& world, const M3& frame) { return Mul(world, Transpose(frame)); }   // C = R E^T
inline M3 Relabel(const M3& bindBase, const M3& bindTaker) { return Mul(bindTaker, Transpose(bindBase)); }   // P = A_taker A_base^T
inline M3 FromCoords(const M3& coords, const M3& frame) { return Mul(coords, frame); }   // R = C E
inline M3 Local(const M3& child, const M3& parent) { return Mul(child, Transpose(parent)); }   // L = R_child R_parent^T

// The taking hand's bone orientation for a bone of the base hand: pose coordinates `poseBase` (C of the game's pose in the base hand's frame), the relabelling `p`, the taking hand's
// frame now.
inline M3 TakerOrientation(const M3& poseBase, const M3& p, const M3& takerFrame) { return FromCoords(Mul(p, poseBase), takerFrame); }

// Which of the taking hand's fingers answers each of the base hand's: by kind when every chain has one (the rig's names say index / middle / ring / pinky / thumb), else by place in the
// list when the two lists have the same length and the same shape (thumb or not, bones). out[i] = the taker's finger for base finger i; false when they cannot be matched.
struct FingerShape { int kind; bool thumb; int bones; };
inline bool MatchFingers(const std::vector<FingerShape>& base, const std::vector<FingerShape>& taker, std::vector<int>* out) {
    out->assign(base.size(), -1);
    if (base.empty() || base.size() != taker.size()) return false;
    bool allKinds = true;
    for (const FingerShape& f : base) allKinds = allKinds && f.kind >= 0;
    for (const FingerShape& f : taker) allKinds = allKinds && f.kind >= 0;
    std::vector<char> used(taker.size(), 0);
    for (size_t i = 0; i < base.size(); ++i) {
        int j = -1;
        if (allKinds) {
            for (size_t q = 0; q < taker.size(); ++q)
                if (!used[q] && taker[q].kind == base[i].kind) { j = (int)q; break; }
        } else {
            j = (int)i;
        }
        if (j < 0 || used[j] || taker[j].thumb != base[i].thumb || taker[j].bones != base[i].bones) { out->assign(base.size(), -1); return false; }
        used[j] = 1;
        (*out)[i] = j;
    }
    return true;
}

}  // namespace grippose
