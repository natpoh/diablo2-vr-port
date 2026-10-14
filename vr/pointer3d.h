// pointer3d.h - the game's mouse pointer in F1 and F3 in 3D (native OpenXR, docs/plan_native_f1_f3.md): the pure maths.
//
// In F1 and F3 the game is played with the mouse, and how a click reaches the game is not ours to touch. What was missing in 3D is the
// pointer to SEE: with the flat picture the game's cursor was drawn on the picture where the mouse is, in 3D there is no picture - the
// scene stands in the room. The game's mouse position is a pixel of its window; the game's own mouse code (d2rcam's ScreenToRay, from OUR
// left-eye camera) turns it into a ray and the ray into a point of the ground. This finds that same point - so the pointer lies where a
// click would walk to - and where it stands in the room:
//
//   1. the pixel (u, v from the window's top left, 0..1) goes through the left eye's view and projection into a world ray (the game's own
//      maths: d2rcam m4::RayThrough),
//   2. the ray meets the game's ground plane (the hero's ground height),
//   3. the world point goes back into the frame the pair's eyes are in (the room for F3, the window's frame for F1) by the inverse of the
//      one similarity the camera is built with (`Similarity`: world = s R r + t).
//
// Pure: no OpenXR, no game. vr/xr.cpp (the pointer's layer) and vr/vrcam.cpp (the maps) use it; cleanroom/tests/pointer3d_test.cpp checks it.
#pragma once

#include "views3d.h"

#include "mat4.h"

#include <cmath>

namespace pointer3d {

using views3d::Vec;

// world = s * R * r + t, R a rotation (row-major), in the frame the pair's eyes are in (xr.h Eyes: the room for F3, the window's
// frame for F1).
struct Similarity {
    float s = 1.0f;
    float R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    Vec t{0, 0, 0};
};

inline Vec Rot(const Similarity& m, Vec v) {
    return {m.R[0] * v.x + m.R[1] * v.y + m.R[2] * v.z, m.R[3] * v.x + m.R[4] * v.y + m.R[5] * v.z, m.R[6] * v.x + m.R[7] * v.y + m.R[8] * v.z};
}
inline Vec ToWorld(const Similarity& m, Vec r) { return Rot(m, r) * m.s + m.t; }
// R^T v: a world direction back into the frame
inline Vec DirFromWorld(const Similarity& m, Vec v) {
    return {m.R[0] * v.x + m.R[3] * v.y + m.R[6] * v.z, m.R[1] * v.x + m.R[4] * v.y + m.R[7] * v.z, m.R[2] * v.x + m.R[5] * v.y + m.R[8] * v.z};
}
inline Vec FromWorld(const Similarity& m, Vec w) { return DirFromWorld(m, w - m.t) * (1.0f / m.s); }

// F3, the diorama: world = hero + s * Ry(phi) * (r - (fx, floorY + lift, fz)) (views3d.h TableEye; RoomToWorldDir is Ry(phi)).
inline Similarity ForTable(const views3d::Table& t, Vec hero) {
    Similarity m;
    m.s = t.scale;
    const float c = std::cos(t.phi), sn = std::sin(t.phi);
    const float R[9] = {c, 0, sn, 0, 1, 0, -sn, 0, c};   // {x c + z s, y, -x s + z c}
    for (int i = 0; i < 9; ++i) m.R[i] = R[i];
    const Vec o{t.fx, t.floorY + t.lift, t.fz};
    m.t = hero - Rot(m, o) * m.s;
    return m;
}

// F1, the window: world = hero + upm * (right x + up y - fwd z) for a point (x, y, z) of the window's frame (x along it, y up, z out
// of it towards the viewer) - the overhead camera's axes in the game's world, the hero on the window's middle.
inline Similarity ForWindow(Vec hero, Vec right, Vec up, Vec fwd, float upm) {
    Similarity m;
    m.s = upm;
    m.R[0] = right.x; m.R[1] = up.x; m.R[2] = -fwd.x;
    m.R[3] = right.y; m.R[4] = up.y; m.R[5] = -fwd.y;
    m.R[6] = right.z; m.R[7] = up.z; m.R[8] = -fwd.z;
    m.t = hero;
    return m;
}

// Where the game's mouse points: the ground under a pixel of its window.
struct Under {
    bool hit = false;
    Vec world{0, 0, 0};   // the point in the game's world
    Vec at{0, 0, 0};      // ... and in the frame of the pair's eyes
    float dist = 0.0f;    // from the left eye, in the frame's metres
};

// The world ray through the pixel (u, v) of the window - the left eye's `view` and `proj`, row-vector matrices as the game's own - against
// the ground plane y = groundY. A pixel above the horizon (the ray does not go down), behind the eye or not finite: no hit.
inline Under GroundUnderPixel(const Similarity& m, float groundY, const float view[16], const float proj[16], float u, float v) {
    Under h;
    float inv[16], o[3], d[3];
    if (!d2rcam::m4::Inverse(view, inv)) return h;
    if (!d2rcam::m4::RayThrough(inv, proj, 1.0f, 1.0f, u, v, o, d)) return h;
    if (!(d[1] < -1e-4f) || !(m.s > 1e-6f)) return h;
    const float t = (groundY - o[1]) / d[1];
    if (!(t > 0.0f)) return h;
    h.world = {o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t};
    if (!std::isfinite(h.world.x + h.world.y + h.world.z)) return h;
    h.at = FromWorld(m, h.world);
    h.dist = t / m.s;
    h.hit = true;
    return h;
}

}  // namespace pointer3d
