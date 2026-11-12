// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/cull_volume_test.cpp - commits 853607f (transparent_filter), 133465a (profiler_accounting), 30083a1 (draw_debug_names), 9b9e28e (selection_walk, shadows_once, cull_once), 1289b9a (cull volume), at tag v0.3.33-windows-preview.
// Its test; asserts kept on in a Release build.
// This file stays MPL 2.0.
//
#undef NDEBUG   // the checks are asserts: kept in a Release build
// [game_patches] cull_once: the volume that holds both eyes' views (cull_volume.h).
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "unleashed_cull_volume.h"

using namespace cullvolume;

namespace {
struct Rng {
    uint32_t s;
    double Next() { s = s * 1664525u + 1013904223u; return (s >> 8) / double(1u << 24); }   // [0, 1)
    double In(double lo, double hi) { return lo + (hi - lo) * Next(); }
};

void Mul(const double a[16], const double b[16], double out[16]) {
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        double s = 0;
        for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
        out[i * 4 + j] = s;
    }
}

// A view as the game has it: row vectors, right-handed, looking down -Z. `yaw` about +y, `pitch` about x,
// the eye at `at`.
void View(const double at[3], double yaw, double pitch, double roll, double out[16]) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    // the eye's axes in the world: right, up, back (it looks along -back)
    const double back[3] = {sy * cp, sp, cy * cp};
    double right[3] = {cy, 0, -sy}, up[3] = {back[1] * right[2] - back[2] * right[1], back[2] * right[0] - back[0] * right[2], back[0] * right[1] - back[1] * right[0]};
    double r2[3], u2[3];
    for (int i = 0; i < 3; ++i) { r2[i] = cr * right[i] + sr * up[i]; u2[i] = -sr * right[i] + cr * up[i]; }
    const double* axis[3] = {r2, u2, back};
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) out[i * 4 + j] = axis[j][i];
        out[12 + j] = -(at[0] * axis[j][0] + at[1] * axis[j][1] + at[2] * axis[j][2]);
    }
    out[3] = out[7] = out[11] = 0; out[15] = 1;
}

// A projection as the game has it: infinite far, reverse Z, the sides given as tangents.
void Projection(double left, double right, double down, double up, double nearZ, double out[16]) {
    std::memset(out, 0, 16 * sizeof(double));
    out[0] = 2.0 / (right - left);
    out[5] = 2.0 / (up - down);
    out[8] = (right + left) / (right - left);
    out[9] = (up + down) / (up - down);
    out[11] = -1.0;
    out[14] = nearZ;
}

void ToFloat(const double m[16], float out[16]) { for (int i = 0; i < 16; ++i) out[i] = float(m[i]); }

// Is the world point in the picture of this matrix (in front of the near plane, inside the four sides)?
bool Sees(const float m[16], const double p[3]) {
    double c[4];
    for (int j = 0; j < 4; ++j) c[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
    return c[3] > 0 && std::fabs(c[0]) < c[3] && std::fabs(c[1]) < c[3] && c[2] < c[3];
}

struct Eyes { float left[16], right[16]; };
// Two eyes of one head: `half` to each side of the head, each turned out by `cant`, the outer side wider.
Eyes Head(const double head[3], double yaw, double pitch, double roll, double half, double cant, double inner, double outer, double down, double up, double nearZ) {
    double centre[16];
    View(head, yaw, pitch, roll, centre);
    Eyes eyes;
    for (int e = 0; e < 2; ++e) {
        const double side = e ? 1.0 : -1.0;
        // the eye in the head's frame: moved along its right, turned about its up; then the view of that
        const double at[3] = {side * half, 0, 0};
        double local[16], view[16], proj[16], both[16];
        View(at, -side * cant, 0, 0, local);
        Mul(centre, local, view);
        Projection(e ? -inner : -outer, e ? outer : inner, -down, up, nearZ, proj);
        Mul(view, proj, both);
        ToFloat(both, e ? eyes.right : eyes.left);
    }
    return eyes;
}

void Pyramids() {
    const double at[3] = {120.0, 7.0, -340.0};
    double view[16], proj[16], both[16];
    View(at, 0.7, -0.3, 0.1, view);
    Projection(-1.2, 0.9, -1.0, 1.1, 0.08, proj);
    Mul(view, proj, both);
    float m[16];
    ToFloat(both, m);
    const Pyramid p = Decompose(m);
    assert(p.ok);
    for (int i = 0; i < 3; ++i) assert(std::fabs(p.apex[i] - at[i]) < 2e-3);
    // forward is the eye's -back
    const double back[3] = {std::sin(0.7) * std::cos(-0.3), std::sin(-0.3), std::cos(0.7) * std::cos(-0.3)};
    for (int i = 0; i < 3; ++i) assert(std::fabs(p.forward[i] + back[i]) < 1e-4);
    // each edge runs along a corner of the picture: a point far along it is on two sides at once
    for (int e = 0; e < 4; ++e) {
        double c[4];
        const double far[3] = {p.apex[0] + 50 * p.edge[e][0], p.apex[1] + 50 * p.edge[e][1], p.apex[2] + 50 * p.edge[e][2]};
        for (int j = 0; j < 4; ++j) c[j] = far[0] * m[j] + far[1] * m[4 + j] + far[2] * m[8 + j] + m[12 + j];
        assert(c[3] > 1.0);
        assert(std::fabs(c[0] / c[3] - (e & 1 ? 1.0 : -1.0)) < 2e-3 && std::fabs(c[1] / c[3] - (e & 2 ? 1.0 : -1.0)) < 2e-3);
    }
    // Not inside itself with room asked for; inside the same view from a step back with wider sides.
    assert(!Contains(m, p, 0.05));
    const double stepBack[3] = {at[0] + 0.5 * back[0], at[1] + 0.5 * back[1], at[2] + 0.5 * back[2]};
    float wider[16];
    View(stepBack, 0.7, -0.3, 0.1, view);
    Projection(-1.3, 1.0, -1.1, 1.2, 0.08, proj);
    Mul(view, proj, both);
    ToFloat(both, wider);
    assert(Contains(wider, p) && Contains(wider, p, 0.02));
    assert(!Contains(m, Decompose(wider)));   // and not the other way round
    // no pyramid: nothing, an orthographic matrix (its w column does not grow anywhere), a null
    const float zero[16] = {};
    const float ortho[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    assert(!Decompose(zero).ok && !Decompose(ortho).ok && !Decompose(nullptr).ok);
    assert(!Contains(m, Decompose(zero)) && !Contains(nullptr, p));
    float out[16] = {9};
    assert(!Combine(p, Decompose(zero), 0.03, out) && out[0] == 9);
}

// Many heads: the volume holds both eyes; every point either eye sees is inside it; every box either eye's
// own test keeps for a point it sees is kept by the volume's test.
void Volumes() {
    Rng rng{20261110};
    int points = 0, boxes = 0;
    for (int n = 0; n < 400; ++n) {
        const double head[3] = {rng.In(-3000, 3000), rng.In(-20, 60), rng.In(-3000, 3000)};
        const double half = rng.In(0.02, 0.5), cant = n % 3 == 0 ? rng.In(0.0, 0.18) : 0.0;
        const double inner = rng.In(0.6, 1.0), outer = rng.In(0.9, 1.5), down = rng.In(0.8, 1.4), up = rng.In(0.7, 1.2), nearZ = rng.In(0.01, 0.5);
        const Eyes eyes = Head(head, rng.In(-3.1, 3.1), rng.In(-1.2, 1.2), rng.In(-0.4, 0.4), half, cant, inner, outer, down, up, nearZ);
        const Pyramid l = Decompose(eyes.left), r = Decompose(eyes.right);
        assert(l.ok && r.ok);
        float volume[16];
        assert(Combine(l, r, 0.03, volume));
        assert(Contains(volume, l) && Contains(volume, r));
        assert(Combine(r, l, 0.03, volume) && Contains(volume, l) && Contains(volume, r));   // either order
        assert(Combine(l, r, 0.03, volume));
        for (int k = 0; k < 200; ++k) {
            // a point somewhere around the head, near and far
            const double reach = k % 4 == 0 ? 2.0 : k % 4 == 1 ? 40.0 : 600.0;
            const double p[3] = {head[0] + rng.In(-reach, reach), head[1] + rng.In(-reach, reach), head[2] + rng.In(-reach, reach)};
            const bool seen = Sees(eyes.left, p) || Sees(eyes.right, p);
            if (!seen) continue;
            ++points;
            double c[4];
            for (int j = 0; j < 4; ++j) c[j] = p[0] * volume[j] + p[1] * volume[4 + j] + p[2] * volume[8 + j] + volume[12 + j];
            assert(c[3] > 0 && std::fabs(c[0]) < c[3] && std::fabs(c[1]) < c[3]);
            // a box around it, of any size
            const double size = rng.In(0.01, 30.0);
            const float lo[3] = {float(p[0] - size * rng.Next()), float(p[1] - size * rng.Next()), float(p[2] - size * rng.Next())};
            const float hi[3] = {float(p[0] + size * rng.Next()), float(p[1] + size * rng.Next()), float(p[2] + size * rng.Next())};
            assert(BoxKept(eyes.left, lo, hi) || BoxKept(eyes.right, lo, hi));
            assert(BoxKept(volume, lo, hi));
            ++boxes;
        }
    }
    assert(points > 5000 && boxes == points);
}

// The second eye from the first and the pair before: the head has moved, the eyes' relation has not.
void Relations() {
    Rng rng{77};
    for (int n = 0; n < 200; ++n) {
        const double half = rng.In(0.03, 0.4), cant = n % 2 ? rng.In(0.0, 0.15) : 0.0, nearZ = rng.In(0.02, 0.3);
        const double before[3] = {rng.In(-2000, 2000), rng.In(0, 30), rng.In(-2000, 2000)};
        const double now[3] = {before[0] + rng.In(-5, 5), before[1] + rng.In(-1, 1), before[2] + rng.In(-5, 5)};
        const Eyes a = Head(before, rng.In(-3, 3), rng.In(-1, 1), rng.In(-0.3, 0.3), half, cant, 0.8, 1.2, 1.1, 0.9, nearZ);
        const Eyes b = Head(now, rng.In(-3, 3), rng.In(-1, 1), rng.In(-0.3, 0.3), half, cant, 0.8, 1.2, 1.1, 0.9, nearZ);
        double relation[16];
        assert(Relation(a.left, a.right, relation));
        float guess[16], volume[16];
        Apply(b.left, relation, guess);
        const Pyramid mine = Decompose(b.left), other = Decompose(guess), real = Decompose(b.right);
        assert(mine.ok && other.ok && real.ok);
        for (int i = 0; i < 3; ++i) assert(std::fabs(other.apex[i] - real.apex[i]) < 0.02);
        assert(Combine(mine, other, 0.03, volume));
        assert(Contains(volume, mine) && Contains(volume, real));   // the eye as it turned out is inside
    }
    // An eye that is not where the pair before had it (the eyes twice as far apart now): not inside, so it is culled itself.
    {
        const double at[3] = {10, 5, 10};
        const Eyes a = Head(at, 0.3, 0.1, 0, 0.1, 0, 0.8, 1.2, 1.1, 0.9, 0.05);
        const Eyes wide = Head(at, 0.3, 0.1, 0, 0.6, 0.3, 0.8, 1.6, 1.1, 0.9, 0.05);
        double relation[16];
        assert(Relation(a.left, a.right, relation));
        float guess[16], volume[16];
        Apply(wide.left, relation, guess);
        assert(Combine(Decompose(wide.left), Decompose(guess), 0.03, volume));
        assert(!Contains(volume, Decompose(wide.right)));
    }
    // No relation from a matrix that cannot be inverted.
    const float zero[16] = {};
    double relation[16];
    const double at[3] = {0, 0, 0};
    const Eyes any = Head(at, 0, 0, 0, 0.1, 0, 1, 1, 1, 1, 0.1);
    assert(!Relation(zero, any.right, relation) && !Relation(nullptr, any.right, relation));
}

// What cannot be one volume is refused.
void Refusals() {
    const double at[3] = {0, 0, 0};
    double view[16], proj[16], both[16];
    float ahead[16], behind[16], side[16], wide[16], out[16];
    Projection(-1, 1, -1, 1, 0.1, proj);
    View(at, 0, 0, 0, view); Mul(view, proj, both); ToFloat(both, ahead);
    View(at, 3.14159265, 0, 0, view); Mul(view, proj, both); ToFloat(both, behind);
    View(at, 1.7, 0, 0, view); Mul(view, proj, both); ToFloat(both, side);
    Projection(-20, 20, -1, 1, 0.1, proj);
    View(at, 0, 0, 0, view); Mul(view, proj, both); ToFloat(both, wide);
    assert(!Combine(Decompose(ahead), Decompose(behind), 0.03, out));   // back to back
    assert(!Combine(Decompose(ahead), Decompose(side), 0.03, out));     // more than a right angle apart
    assert(!Combine(Decompose(wide), Decompose(wide), 0.03, out));      // a side more than 80 degrees from the axis
    assert(!Combine(Decompose(ahead), Decompose(ahead), -1.0, out));
    assert(Combine(Decompose(ahead), Decompose(ahead), 0.001, out) && Contains(out, Decompose(ahead)));   // the same eye twice: itself, a little wider
    // the game's box test, as read: kept when it straddles a side, dropped when wholly outside one or behind
    const float in0[3] = {-1, -1, -11}, in1[3] = {1, 1, -9}, out0[3] = {40, -1, -11}, out1[3] = {42, 1, -9}, back0[3] = {-1, -1, 5}, back1[3] = {1, 1, 7},
                over0[3] = {-500, -500, -500}, over1[3] = {500, 500, 500};
    assert(BoxKept(ahead, in0, in1) && !BoxKept(ahead, out0, out1) && !BoxKept(ahead, back0, back1) && BoxKept(ahead, over0, over1));
}
}  // namespace

int main() {
    Pyramids();
    Volumes();
    Relations();
    Refusals();
    std::puts("cull volume: a matrix's pyramid, the volume of two eyes (points and boxes either eye keeps), the second eye from the pair before, and refusals passed");
    return 0;
}
