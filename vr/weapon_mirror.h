// weapon_mirror.h - [weapon_<kind>] (Weapon in hand: Move X/Y/Z, Turn X/Y/Z) on the LEFT hand's weapon: the mirror image of what it does on the right.
// Pure: no game calls, no globals (cleanroom/tests/weapon_mirror_test.cpp feeds it).
//
// The sliders move and turn a weapon in its attach bone's own axes. The two attach bones are not each other's copies: the left one's axes are the
// right one's reflected in the body's plane and then some turned over (the Amazon's L_weapon_attach: its x the other way, its y - the shaft - and z
// the same). The same numbers in both bones' axes turned the left weapon the same way as the right one, not the mirror way: Turn about Y 14 on the
// right was 14, not -14, on the left (the user, 2026-10-30).
//
// So the axes' correspondence K is read off the rig (no hand-set signs): the left bone = K * (the right bone reflected), a signed permutation of the
// bone's own axes. The turn and the move given for the right bone are carried over by it: turn K R K^T, move v K^T. Then, were the hands each other's
// mirror images, the left weapon is exactly the right one's mirror image, adjustment and all.
//
// Matrices are vr/skeletons.cpp's: row vectors, 16 floats, the rows the bone's axes in the model, the place in 12..14. The body's plane is the model's
// x = const (the bind pose: his left at +x).
#pragma once

#include <cmath>

namespace weaponmirror {

// A signed permutation of the three axes: row i of K is sign[i] times unit axis col[i].
struct K {
    int col[3] = {0, 1, 2};
    int sign[3] = {1, 1, 1};
    bool identity() const { return col[0] == 0 && col[1] == 1 && col[2] == 2 && sign[0] > 0 && sign[1] > 0 && sign[2] > 0; }
};

// The 3x3 turn of a 16-float matrix with its rows made unit length (a bone's scale, the game's shrink taken out). False when a row is ~0 or not finite.
inline bool Rot(const float m[16], double r[9]) {
    for (int i = 0; i < 3; ++i) {
        const double x = m[i * 4], y = m[i * 4 + 1], z = m[i * 4 + 2], l = std::sqrt(x * x + y * y + z * z);
        if (!(l > 1e-6) || !std::isfinite(l)) return false;
        r[i * 3] = x / l; r[i * 3 + 1] = y / l; r[i * 3 + 2] = z / l;
    }
    return true;
}

// a * b (3x3, row-major).
inline void Mul3(const double a[9], const double b[9], double out[9]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}
inline void T3(const double a[9], double out[9]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) out[i * 3 + j] = a[j * 3 + i];
}

// The signed permutation nearest m: each row's largest entry, at least kSnap, the three in different columns. False otherwise (no clear
// correspondence; *k untouched).
constexpr double kSnap = 0.8;
inline bool Snap(const double m[9], K* k) {
    bool used[3] = {false, false, false};
    K out;
    for (int i = 0; i < 3; ++i) {
        int best = 0;
        for (int j = 1; j < 3; ++j)
            if (std::fabs(m[i * 3 + j]) > std::fabs(m[i * 3 + best])) best = j;
        const double v = m[i * 3 + best];
        if (!(std::fabs(v) >= kSnap) || used[best]) return false;
        used[best] = true;
        out.col[i] = best;
        out.sign[i] = v < 0.0 ? -1 : 1;
    }
    *k = out;
    return true;
}
inline void ToMat(const K& k, double m[9]) {
    for (int i = 0; i < 9; ++i) m[i] = 0.0;
    for (int i = 0; i < 3; ++i) m[i * 3 + k.col[i]] = k.sign[i];
}

// K from two bones that stand as each other's mirror images (the bind pose's left and right ones, in the model): left = K * right * X,
// X the reflection in x. (Their turns are near orthonormal: the inverse of the right one is its transpose.)
inline bool FromMirrorPair(const float right[16], const float left[16], K* k) {
    double r[9], l[9], rt[9], lx[9], m[9];
    if (!Rot(right, r) || !Rot(left, l)) return false;
    for (int i = 0; i < 3; ++i) { lx[i * 3] = -l[i * 3]; lx[i * 3 + 1] = l[i * 3 + 1]; lx[i * 3 + 2] = l[i * 3 + 2]; }   // left * X
    T3(r, rt);
    Mul3(lx, rt, m);   // left X right^-1 (X = X^-1)
    return Snap(m, k);
}

// K for the weapon bones from the game's own grip in each hand (each attach bone against its wrist, as the animation has them) and the wrists' own
// correspondence kh (FromMirrorPair of the bind pose's hands): with the hands mirrored (left hand = kh * right hand * X), the left weapon is
// aL * kh * aR^-1 * (the right weapon) * X - that turn, snapped.
inline bool FromGrips(const float attachRight[16], const float attachLeft[16], const K& kh, K* k) {
    double ar[9], al[9], art[9], h[9], t[9], m[9];
    if (!Rot(attachRight, ar) || !Rot(attachLeft, al)) return false;
    ToMat(kh, h);
    T3(ar, art);
    Mul3(al, h, t);
    Mul3(t, art, m);
    return Snap(m, k);
}

// The right bone's turn (3x3 of a 16-float row-vector matrix, applied in the bone's own frame: Mul(turn, bone)) and move (along the bone's own unit
// axes, any unit) as the left bone has them: turn K turn K^T, move v K^T.
inline void Carry(const K& k, const float turn[16], const float move[3], float turnOut[16], float moveOut[3]) {
    double km[9], kt[9], r[9], t[9], o[9];
    ToMat(k, km);
    T3(km, kt);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = turn[i * 4 + j];
    Mul3(km, r, t);
    Mul3(t, kt, o);
    for (int i = 0; i < 16; ++i) turnOut[i] = turn[i];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) turnOut[i * 4 + j] = (float)o[i * 3 + j];
    for (int j = 0; j < 3; ++j) moveOut[j] = (float)(move[0] * kt[j] + move[1] * kt[3 + j] + move[2] * kt[6 + j]);   // v K^T
}

}  // namespace weaponmirror
