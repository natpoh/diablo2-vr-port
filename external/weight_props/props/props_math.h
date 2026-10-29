// SPDX-License-Identifier: MIT
// props_math.h - the props world's small vector arithmetic (floats: Box3D's own precision), its conversions to and from Box3D, and the point of a prop's shape nearest a point.
// Shared by props_world.cpp and what adds to the world (props_world.h's World::Holder).
//
// Copyright (c) 2026 natpoh
// Copyright (c) 2026 bearstonem (the knockable props' rules and materials, bearstonem/d2r-pcvr)
// Box3D: Copyright (c) 2026 Erin Catto (MIT), used unmodified.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#pragma once

#include <algorithm>
#include <cmath>

#include <box3d/box3d.h>

#include "d2r_vr_props.h"

namespace props::math {

struct V {
    float x = 0, y = 0, z = 0;
};
struct Q {
    float x = 0, y = 0, z = 0, w = 1;
};
inline V operator+(V a, V b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V operator-(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V operator*(V a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V a) { return std::isfinite(a.x + a.y + a.z); }
inline bool Finite(Q q) { return std::isfinite(q.x + q.y + q.z + q.w); }
inline Q Mul(Q a, Q b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
inline Q Conj(Q q) { return { -q.x, -q.y, -q.z, q.w }; }
inline Q Norm(Q q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? Q{ q.x / n, q.y / n, q.z / n, q.w / n } : Q{};
}
inline V Rotate(Q q, V v) {
    const V u{ q.x, q.y, q.z };
    const V t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
// The turn from +y to the unit vector d.
inline Q FromUp(V d) {
    const V up{ 0, 1, 0 };
    const float c = Dot(up, d);
    if (c < -0.9999f) return { 1, 0, 0, 0 };
    const V ax = Cross(up, d);
    return Norm({ ax.x, ax.y, ax.z, 1.0f + c });
}
// Rotation vector (axis * angle) of q.
inline V Log(Q q) {
    if (q.w < 0) q = { -q.x, -q.y, -q.z, -q.w };
    const V v{ q.x, q.y, q.z };
    const float s = Len(v);
    if (s < 1e-7f) return v * 2.0f;
    return v * (2.0f * std::atan2(s, q.w) / s);
}
inline Q Exp(V r) {
    const float a = Len(r);
    if (a < 1e-7f) return Norm({ r.x * 0.5f, r.y * 0.5f, r.z * 0.5f, 1 });
    const float s = std::sin(a * 0.5f) / a;
    return { r.x * s, r.y * s, r.z * s, std::cos(a * 0.5f) };
}
inline float Angle(Q a, Q b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::min(1.0f, d));
}

inline b3Vec3 B(V v) { return { v.x, v.y, v.z }; }
inline b3Quat B(Q q) { return { { q.x, q.y, q.z }, q.w }; }
inline V F(b3Vec3 v) { return { v.x, v.y, v.z }; }
inline Q F(b3Quat q) { return { q.v.x, q.v.y, q.v.z, q.s }; }
inline V F3(const float* f) { return { f[0], f[1], f[2] }; }
inline Q F4(const float* f) { return Norm({ f[0], f[1], f[2], f[3] }); }
inline void Put(V v, float* f) { f[0] = v.x; f[1] = v.y; f[2] = v.z; }
inline void Put(Q q, float* f) { f[0] = q.x; f[1] = q.y; f[2] = q.z; f[3] = q.w; }

inline void Destroy(b3BodyId& id) {
    if (!B3_IS_NULL(id) && b3Body_IsValid(id)) b3DestroyBody(id);
    id = b3_nullBodyId;
}

constexpr float kMinHalf = 0.008f;   // m: the thinnest half extent of a prop's shape

// The point of a prop's shape (in its body's frame: centre c, half extents h) nearest p, and p's distance from its surface (< 0 inside: the nearest way out).
inline V NearestOnShape(int shape, V c, V h, V p, float* dist) {
    const V d = p - c;
    if (shape == D2RP_SHAPE_BALL) {
        const float r = std::max(kMinHalf, (h.x + h.y + h.z) / 3.0f), l = Len(d);
        *dist = l - r;
        return l > 1e-6f ? c + d * (r / l) : c + V{ 0, r, 0 };
    }
    const float y = std::clamp(d.y, -h.y, h.y);
    if (shape == D2RP_SHAPE_ROUND) {   // an upright elliptic cylinder: its wall (the ellipse scaled out along the way to p - near enough for a vessel's), its caps
        const float ex = d.x / h.x, ez = d.z / h.z, rr = std::sqrt(ex * ex + ez * ez);
        const V wall = rr > 1e-6f ? V{ d.x / rr, y, d.z / rr } : V{ h.x, y, 0 };
        if (rr <= 1.0f && std::fabs(d.y) <= h.y) {
            const float toWall = (1.0f - rr) * std::min(h.x, h.z), toCap = h.y - std::fabs(d.y);
            if (toCap < toWall) {
                *dist = -toCap;
                return c + V{ d.x, d.y < 0 ? -h.y : h.y, d.z };
            }
            *dist = -toWall;
            return c + wall;
        }
        const V q = rr <= 1.0f ? V{ d.x, y, d.z } : wall;
        *dist = Len(d - q);
        return c + q;
    }
    const V q{ std::clamp(d.x, -h.x, h.x), y, std::clamp(d.z, -h.z, h.z) };
    if (std::fabs(d.x) <= h.x && std::fabs(d.y) <= h.y && std::fabs(d.z) <= h.z) {   // inside: out through the nearest face
        const float fx = h.x - std::fabs(d.x), fy = h.y - std::fabs(d.y), fz = h.z - std::fabs(d.z);
        V o = d;
        if (fx <= fy && fx <= fz) o.x = d.x < 0 ? -h.x : h.x;
        else if (fy <= fz) o.y = d.y < 0 ? -h.y : h.y;
        else o.z = d.z < 0 ? -h.z : h.z;
        *dist = -std::min({ fx, fy, fz });
        return c + o;
    }
    *dist = Len(d - q);
    return c + q;
}

}  // namespace props::math
