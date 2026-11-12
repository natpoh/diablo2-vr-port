// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/cull_volume.h - commits 853607f (transparent_filter), 133465a (profiler_accounting), 30083a1 (draw_debug_names), 9b9e28e (selection_walk, shadows_once, cull_once), 1289b9a (cull volume), at tag v0.3.33-windows-preview.
// Pure arithmetic, unchanged.
// This file stays MPL 2.0.
//
#pragma once
// [game_patches] cull_once: one cull volume for both eyes (vr/unleashed_game_patches.cpp, docs/notes/game_patches.md; Unleashed's steamvr/PERFORMANCE.md
// "Once per pair"). Pure arithmetic, tested in cleanroom/tests/unleashed_cull_volume_test.cpp.
//
// The game culls with a 4x4 matrix, row-major with row vectors (clip = p * M): a box is kept when, for each of
// x > -w, x < w, y > -w, y < w, z > -w, z < w, at least one of its corners passes, and dropped at once when w < 0
// at every corner. Only the x, y and w columns say where the picture's edges are; the z column is the near
// plane. So a matrix is a pyramid: an apex, and four edges through the picture's corners.
//
// Combine makes one pyramid that holds two of them (the two eyes'): its axis between theirs, its sides as
// wide as the widest edge of either plus a margin, and its apex far enough behind both for each apex to be
// inside. A pyramid is a convex cone from its apex, so a pyramid whose apex and four edge directions are
// inside another lies inside it whole: that is what Contains tests, and what the caller relies on before a
// second eye's cull is left out. The combined matrix has no near plane (its z column is zero): what is nearer
// than an eye's near plane is kept, never dropped.
#include <cmath>
#include <cstddef>
#include <initializer_list>

namespace cullvolume {

struct Pyramid {
    bool ok = false;
    double apex[3] = {};
    double forward[3] = {};     // unit: the way w grows
    double right[3] = {};       // the x column's direction (not unit, not square to forward)
    double edge[4][3] = {};     // unit, through the corners (-1,-1) (1,-1) (-1,1) (1,1) of the picture
};

namespace detail {
inline double Dot(const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline bool Unit(double* v) {
    const double n = std::sqrt(Dot(v, v));
    if (!(n > 1e-12) || !std::isfinite(n)) return false;
    v[0] /= n; v[1] /= n; v[2] /= n;
    return true;
}
// rows * x = rhs, by Cramer's rule. False when the rows do not span space.
inline bool Solve(const double r[3][3], const double rhs[3], double x[3]) {
    const double det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                       r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    const double scale = std::sqrt(Dot(r[0], r[0]) * Dot(r[1], r[1]) * Dot(r[2], r[2]));
    if (!std::isfinite(det) || !(std::fabs(det) > 1e-9 * scale) || !(scale > 0)) return false;
    for (int c = 0; c < 3; ++c) {
        double m[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m[i][j] = j == c ? rhs[i] : r[i][j];
        x[c] = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) / det;
        if (!std::isfinite(x[c])) return false;
    }
    return true;
}
}  // namespace detail

// The pyramid of a cull matrix. Not ok when the matrix is no perspective view (its x, y and w columns do not
// meet in a point).
inline Pyramid Decompose(const float m[16]) {
    Pyramid p;
    if (!m) return p;
    double rows[3][3], t[3];
    const int col[3] = {0, 1, 3};
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 3; ++i) rows[k][i] = m[i * 4 + col[k]];
        t[k] = m[12 + col[k]];
        if (!std::isfinite(t[k])) return p;
    }
    const double none[3] = {-t[0], -t[1], -t[2]};
    if (!detail::Solve(rows, none, p.apex)) return p;
    for (int i = 0; i < 3; ++i) { p.forward[i] = rows[2][i]; p.right[i] = rows[0][i]; }
    if (!detail::Unit(p.forward)) return p;
    for (int e = 0; e < 4; ++e) {
        const double corner[3] = {e & 1 ? 1.0 : -1.0, e & 2 ? 1.0 : -1.0, 1.0};
        if (!detail::Solve(rows, corner, p.edge[e]) || !detail::Unit(p.edge[e])) return p;
        if (!(detail::Dot(p.edge[e], p.forward) > 0)) return p;
    }
    p.ok = true;
    return p;
}

// True when the pyramid lies inside the matrix's x and y sides and in front of its apex (w > 0), each by at
// least `slack` (a share of w; 0 = touching counts).
inline bool Contains(const float c[16], const Pyramid& p, double slack = 0.0) {
    if (!c || !p.ok) return false;
    const auto inside = [&](const double* v, double one) {
        double clip[4];
        for (int j = 0; j < 4; ++j) clip[j] = v[0] * c[j] + v[1] * c[4 + j] + v[2] * c[8 + j] + one * c[12 + j];
        const double w = clip[3], reach = w * (1.0 - slack);
        return std::isfinite(w) && w > 0 && std::fabs(clip[0]) <= reach && std::fabs(clip[1]) <= reach;
    };
    if (!inside(p.apex, 1.0)) return false;
    for (int e = 0; e < 4; ++e) if (!inside(p.edge[e], 0.0)) return false;
    return true;
}

// A matrix whose pyramid holds both `a` and `b`. margin: added to each side, as a tangent (0.03 = 1.7 degrees).
// The apex is set back half the apexes' distance beyond what is needed: room for an eye that is not quite
// where it was expected (a float matrix far from the world's origin places its apex to a thousandth of a unit).
// False (out untouched) when the two look more than a right angle apart, or a side would be more than about
// 80 degrees from the axis.
inline constexpr double SetBack = 0.5;
inline bool Combine(const Pyramid& a, const Pyramid& b, double margin, float out[16]) {
    using detail::Dot;
    if (!a.ok || !b.ok || !out || !(margin >= 0)) return false;
    double f[3], r[3], u[3];
    for (int i = 0; i < 3; ++i) f[i] = a.forward[i] + b.forward[i];
    if (!detail::Unit(f) || !(Dot(f, a.forward) > 0.7) || !(Dot(f, b.forward) > 0.7)) return false;
    const double along = Dot(a.right, f);
    for (int i = 0; i < 3; ++i) r[i] = a.right[i] - along * f[i];
    if (!detail::Unit(r)) return false;
    u[0] = f[1] * r[2] - f[2] * r[1]; u[1] = f[2] * r[0] - f[0] * r[2]; u[2] = f[0] * r[1] - f[1] * r[0];
    double lo[2] = {0, 0}, hi[2] = {0, 0};
    for (const Pyramid* p : {&a, &b})
        for (int e = 0; e < 4; ++e) {
            const double depth = Dot(p->edge[e], f);
            if (!(depth > 0.17)) return false;   // an edge more than 80 degrees from the axis
            const double t[2] = {Dot(p->edge[e], r) / depth, Dot(p->edge[e], u) / depth};
            for (int k = 0; k < 2; ++k) { lo[k] = std::fmin(lo[k], t[k]); hi[k] = std::fmax(hi[k], t[k]); }
        }
    for (int k = 0; k < 2; ++k) {
        lo[k] -= margin; hi[k] += margin;
        if (!(lo[k] < -1e-3) || !(hi[k] > 1e-3) || !std::isfinite(lo[k]) || !std::isfinite(hi[k])) return false;
    }
    // The apex: on the axis through the middle of the two, set back until both are inside.
    double mid[3], back = 0, apart = 0;
    for (int i = 0; i < 3; ++i) { mid[i] = 0.5 * (a.apex[i] + b.apex[i]); apart += (a.apex[i] - b.apex[i]) * (a.apex[i] - b.apex[i]); }
    apart = std::sqrt(apart);
    for (const Pyramid* p : {&a, &b}) {
        const double q[3] = {p->apex[0] - mid[0], p->apex[1] - mid[1], p->apex[2] - mid[2]};
        const double side[2] = {Dot(q, r), Dot(q, u)}, depth = Dot(q, f);
        for (int k = 0; k < 2; ++k) back = std::fmax(back, (side[k] > 0 ? side[k] / hi[k] : side[k] / lo[k]) - depth);
        back = std::fmax(back, -depth);
    }
    back += SetBack * apart + 1e-3;
    if (!std::isfinite(back)) return false;
    double apex[3];
    for (int i = 0; i < 3; ++i) apex[i] = mid[i] - back * f[i];
    // clip x = (2 X - (hi + lo) D) / (hi - lo), with X, D the point's side and depth from the apex; y alike; w = D.
    double col[4][4] = {};
    const double* axis[2] = {r, u};
    for (int k = 0; k < 2; ++k) {
        for (int i = 0; i < 3; ++i) col[k][i] = (2.0 * axis[k][i] - (hi[k] + lo[k]) * f[i]) / (hi[k] - lo[k]);
        col[k][3] = -Dot(apex, col[k]);
    }
    for (int i = 0; i < 3; ++i) col[3][i] = f[i];
    col[3][3] = -Dot(apex, f);
    float m[16];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        m[i * 4 + j] = float(col[j][i]);
        if (!std::isfinite(m[i * 4 + j])) return false;
    }
    for (int i = 0; i < 16; ++i) out[i] = m[i];
    return true;
}

// out = inverse(a) * b: what takes a's clip space to b's. For two eyes of one head it does not change as the
// head moves. False when a cannot be inverted.
inline bool Relation(const float a[16], const float b[16], double out[16]) {
    if (!a || !b || !out) return false;
    double m[4][8];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { m[i][j] = a[i * 4 + j]; m[i][4 + j] = i == j ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int best = c;
        for (int i = c + 1; i < 4; ++i) if (std::fabs(m[i][c]) > std::fabs(m[best][c])) best = i;
        if (!(std::fabs(m[best][c]) > 1e-12) || !std::isfinite(m[best][c])) return false;
        if (best != c) for (int j = 0; j < 8; ++j) { const double t = m[c][j]; m[c][j] = m[best][j]; m[best][j] = t; }
        const double d = m[c][c];
        for (int j = 0; j < 8; ++j) m[c][j] /= d;
        for (int i = 0; i < 4; ++i) {
            if (i == c) continue;
            const double k = m[i][c];
            if (k != 0) for (int j = 0; j < 8; ++j) m[i][j] -= k * m[c][j];
        }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        double s = 0;
        for (int k = 0; k < 4; ++k) s += m[i][4 + k] * double(b[k * 4 + j]);
        if (!std::isfinite(s)) return false;
        out[i * 4 + j] = s;
    }
    return true;
}

// out = a * relation: the other eye's matrix for this head, from the relation of an earlier pair.
inline void Apply(const float a[16], const double relation[16], float out[16]) {
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        double s = 0;
        for (int k = 0; k < 4; ++k) s += double(a[i * 4 + k]) * relation[k * 4 + j];
        out[i * 4 + j] = float(s);
    }
}

// The game's own test of a box against a cull matrix, for the tests: kept (true) when every one of the six
// sides has a corner on its inner side; dropped at once when w < 0 at every corner.
inline bool BoxKept(const float m[16], const float lo[3], const float hi[3]) {
    unsigned sides = 0;
    bool anyFront = false;
    for (int c = 0; c < 8; ++c) {
        const double p[3] = {c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]};
        double clip[4];
        for (int j = 0; j < 4; ++j) clip[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
        if (!(clip[3] < 0)) anyFront = true;
        for (int k = 0; k < 3; ++k) {
            if (clip[k] > -clip[3]) sides |= 1u << (2 * k);
            if (clip[k] < clip[3]) sides |= 1u << (2 * k + 1);
        }
    }
    return anyFront && sides == 0x3F;
}

}  // namespace cullvolume
