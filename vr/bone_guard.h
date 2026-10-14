// bone_guard.h - a bone matrix that lost an axis, and what to do about it. Pure maths.
//
// "The staff got flattened into a plane" (test-21): the attach bone the item is drawn on had its scale shrunk to nearly nothing - the game's own way of hiding what leaves a hand
// (it shrinks the attach bone of the hand that holds nothing) - and the weight model carried that shrunk bone into the weapon it draws: the weapon's matrix is made from the game's
// grip taken ONCE per weapon (skeletons.cpp Grip), and a grip taken while the game had the bone hidden (a weapon swap in progress, the item hung on the other wrist) is a shrunk
// one for as long as it is kept. Nothing was wrong with the matrix maths itself (vr/grip_fit.h and the pass in skeletons.cpp turn a hand's rows by rotations only: the sweep test
// staff_flat_test runs the hook over the whole sphere of the staff's directions); the matrix handed to the skeleton must keep three axes of the scale it should have.
//
// Row-vector 4x4 matrices as vr/skeletons.cpp has them (rows 0..2 the bone's axes, row 3 its place), floats in a 16 array.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace boneguard {

inline double RowLen(const float m[16], int r) { return std::sqrt((double)m[r * 4] * m[r * 4] + (double)m[r * 4 + 1] * m[r * 4 + 1] + (double)m[r * 4 + 2] * m[r * 4 + 2]); }
inline double RowDot(const float m[16], int a, int b) { return (double)m[a * 4] * m[b * 4] + (double)m[a * 4 + 1] * m[b * 4 + 1] + (double)m[a * 4 + 2] * m[b * 4 + 2]; }
inline double Det(const float m[16]) {
    return (double)m[0] * ((double)m[5] * m[10] - (double)m[6] * m[9]) - (double)m[1] * ((double)m[4] * m[10] - (double)m[6] * m[8]) + (double)m[2] * ((double)m[4] * m[9] - (double)m[5] * m[8]);
}
inline bool Finite(const float m[16]) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

// A proper bone matrix: finite, its three rows square to each other and all of the scale it should have (`scale`, within 1 %), its handedness the one `detSign` says (+1 / -1).
inline bool Proper(const float m[16], double scale, int detSign, std::string* why = nullptr) {
    if (!Finite(m)) { if (why) *why = "not finite"; return false; }
    for (int r = 0; r < 3; ++r)
        if (std::fabs(RowLen(m, r) - scale) > 0.01 * scale) {
            if (why) { char b[96]; std::snprintf(b, sizeof b, "row %d is %.4f long, wanted %.4f", r, RowLen(m, r), scale); *why = b; }
            return false;
        }
    const double tol = 0.01 * scale * scale;
    if (std::fabs(RowDot(m, 0, 1)) > tol || std::fabs(RowDot(m, 0, 2)) > tol || std::fabs(RowDot(m, 1, 2)) > tol) {
        if (why) *why = "rows not square to each other";
        return false;
    }
    const double d = Det(m);
    if ((d > 0.0 ? 1 : -1) != detSign || std::fabs(std::fabs(d) - scale * scale * scale) > 0.03 * scale * scale * scale) {
        if (why) { char b[96]; std::snprintf(b, sizeof b, "determinant %.5f, wanted %c%.5f", d, detSign > 0 ? '+' : '-', scale * scale * scale); *why = b; }
        return false;
    }
    return true;
}

// What was wrong with a weapon's matrix, for the log.
enum class Fault { None, Shrunk, Degenerate, NotFinite };

// The weapon's matrix as the weight model drew it, against the scale it should have (`want`: the game's own attach bone as drawn, times the hand's): shrunk rows (the game hides a
// bone by shrinking it) are brought back to `want` along their own directions; a row with no direction left is made from the other two; the handedness is kept (detSign: +1 or
// -1 - the caller knows which a bone should have, a mirrored left bone is -1; 0 = as the matrix has it, which a matrix with a row gone cannot tell: then right-handed). Returns what was wrong (None: nothing, the matrix is untouched). Unfixable (NaN, no two rows with directions): NotFinite / Degenerate, untouched.
inline Fault Restore(float m[16], double want, int detSign = 0) {
    if (!Finite(m)) return Fault::NotFinite;
    if (!(want > 1e-6)) return Fault::None;
    double len[3] = {RowLen(m, 0), RowLen(m, 1), RowLen(m, 2)};
    const double floorLen = 0.5 * want;
    if (len[0] >= floorLen && len[1] >= floorLen && len[2] >= floorLen) return Fault::None;
    const double det0 = Det(m);
    const int sign = detSign != 0 ? detSign : (det0 < 0.0 ? -1 : 1);
    // the rows with a direction left: scaled to `want` along it; the others to be made
    float r[3][3];
    bool have[3];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        have[i] = len[i] > 1e-9;
        if (have[i]) {
            for (int c = 0; c < 3; ++c) r[i][c] = (float)(m[i * 4 + c] / len[i] * want);
            ++n;
        }
    }
    auto cross = [&](int a, int b, int out, double s) {
        const double x = (double)r[a][1] * r[b][2] - (double)r[a][2] * r[b][1], y = (double)r[a][2] * r[b][0] - (double)r[a][0] * r[b][2], z = (double)r[a][0] * r[b][1] - (double)r[a][1] * r[b][0];
        const double l = std::sqrt(x * x + y * y + z * z);
        if (!(l > 1e-3 * want * want)) return false;   // (two rows within a twentieth of a degree of each other make no third)
        r[out][0] = (float)(x / l * want * s); r[out][1] = (float)(y / l * want * s); r[out][2] = (float)(z / l * want * s);
        return true;
    };
    if (n == 3) {
        // all three still point somewhere: only the scale was lost (the rows keep their own directions)
    } else if (n == 2) {
        const int miss = !have[0] ? 0 : !have[1] ? 1 : 2, a = (miss + 1) % 3, b = (miss + 2) % 3;
        // rows x y z of a right-handed matrix: x = y cross z, y = z cross x, z = x cross y; a left-handed one has them all the other way round
        if (!cross(a, b, miss, sign)) return Fault::Degenerate;
    } else {
        return Fault::Degenerate;
    }
    const bool shrunk = n == 3;
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) m[i * 4 + c] = r[i][c];
    return shrunk ? Fault::Shrunk : Fault::Degenerate;
}

inline const char* FaultName(Fault f) {
    switch (f) {
    case Fault::Shrunk: return "shrunk (the game's hidden bone)";
    case Fault::Degenerate: return "an axis gone (rebuilt from the other two)";
    case Fault::NotFinite: return "not a number";
    default: return "fine";
    }
}

}  // namespace boneguard
