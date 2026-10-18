#pragma once
// world_anchor.h - what stays put in the game's WORLD while the room moves under it. Pure maths (cleanroom/tests/world_anchor_test.cpp).
//
// The weight model lives in the room (the headset's tracking space). Walking and turning with the sticks move the game's world under the room, not the room - so a weapon let go and
// lying in the room walks with the hero. This says how the room's axes move against the world from one head sample to the next, so a thing that lies in the world can be carried
// the other way.
//
// The mapping the plugin already uses for everything drawn on the hero (vrcam's `world()`): a point of the HEAD FRAME - metres from the head, x right, y up, z back, turned with the
// head's yaw alone - stands in the world at   eye + upm * (v.x * camR - v.z * camF)   (horizontally; camR / camF are the head frame's right and forward in the world, eye the camera
// there, upm the world units per metre). A point p of the room is v = R(-psi) * (p - H) in the head frame (H the head in the room, psi its yaw, R the turn about +y: x' = x cos + z sin,
// z' = -x sin + z cos). A thing that stays in the world is then carried, between two samples, by the map  p_t = A * p_(t-1) + b  with A a turn about the vertical.
// A reflection in the mapping (a left-handed world) cancels: A is built from both samples and is always a proper turn.

#include <cmath>

namespace worldanchor {

struct Snap {
    bool ok = false;
    double hx = 0, hz = 0, psi = 0;                // the head in the room: its place (x, z) and its yaw (rad, about +y)
    double ex = 0, ez = 0;                         // the eye in the world (x, z)
    double rx = 0, rz = 0, fx = 0, fz = 0;         // the head frame's right and forward in the world (x, z)
    double upm = 0;                                // world units per metre
};

// A snapshot from the numbers the caller has; ok false when any is not a number or the frame is degenerate.
inline Snap Make(const float headRoom[3], float headYawDeg, const float eye[3], const float camR[3], const float camF[3], float upm) {
    Snap s;
    const double v[] = {headRoom[0], headRoom[2], headYawDeg, eye[0], eye[2], camR[0], camR[2], camF[0], camF[2], upm};
    for (double x : v)
        if (!std::isfinite(x)) return s;
    s.hx = headRoom[0]; s.hz = headRoom[2];
    s.psi = (double)headYawDeg * 0.017453292519943295;
    s.ex = eye[0]; s.ez = eye[2];
    s.rx = camR[0]; s.rz = camR[2]; s.fx = camF[0]; s.fz = camF[2];
    s.upm = upm;
    const double det = s.fx * s.rz - s.rx * s.fz;
    s.ok = s.upm > 1e-6 && std::fabs(det) > 1e-6;
    return s;
}

struct Move {
    bool ok = false;
    double delta = 0;      // the turn about +y (rad): the world's turn against the room, as a room point sees it
    double sx = 0, sz = 0; // then this shift (m, the room's x and z)
};

// The room point of the world point that stood at p in `a`, now that the sample is `b`:  p' = R(delta) * p + (sx, sz). Not ok when either snapshot is not, or the two do not agree on a
// scale and a turn (the data of two moments that do not belong together).
inline Move Between(const Snap& a, const Snap& b) {
    Move m;
    if (!a.ok || !b.ok) return m;
    const auto ry = [](double th, double x, double z, double* ox, double* oz) { const double c = std::cos(th), s = std::sin(th); *ox = x * c + z * s; *oz = -x * s + z * c; };
    // C = [camR | -camF] (columns): world offset = upm * C * v. Its inverse for b.
    const double detB = b.fx * b.rz - b.rx * b.fz;                     // det of [[rx, -fx], [rz, -fz]]
    const double ib00 = -b.fz / detB, ib01 = b.fx / detB, ib10 = -b.rz / detB, ib11 = b.rx / detB;
    // Mt = R(psi_b) * C_b^-1  (columns of C_b^-1 turned)
    double m00, m10, m01, m11;
    ry(b.psi, ib00, ib10, &m00, &m10);
    ry(b.psi, ib01, ib11, &m01, &m11);
    // X = C_a * R(-psi_a)  (columns: C_a's columns are camR, -camF; applied to R(-psi_a) v)
    double ra0, ra1, rb0, rb1;   // R(-psi_a) applied to the unit vectors x and z
    ry(-a.psi, 1, 0, &ra0, &ra1);
    ry(-a.psi, 0, 1, &rb0, &rb1);
    const auto ca = [&](double vx, double vz, double* ox, double* oz) { *ox = a.rx * vx - a.fx * vz; *oz = a.rz * vx - a.fz * vz; };
    double x0x, x0z, x1x, x1z;   // C_a * R(-psi_a) * e_x and * e_z
    ca(ra0, ra1, &x0x, &x0z);
    ca(rb0, rb1, &x1x, &x1z);
    // A_raw = Mt * (C_a R(-psi_a)), columns for e_x and e_z
    const double a00 = m00 * x0x + m01 * x0z, a10 = m10 * x0x + m11 * x0z;
    const double a01 = m00 * x1x + m01 * x1z, a11 = m10 * x1x + m11 * x1z;
    const double det = a00 * a11 - a01 * a10, n = std::sqrt(a00 * a00 + a10 * a10);
    const double scale = b.upm > 0 ? a.upm / b.upm : 1.0;
    if (!(n > 1e-9) || !(det > 0.5 * n * n) || std::fabs(n * scale - 1.0) > 0.25) return m;   // not a turn about the vertical (or a mirror): the two samples do not belong together
    // the first column of a turn R(delta) is (cos delta, -sin delta)
    m.delta = std::atan2(-a10, a00);
    // shift = H_b + Mt * (eye_a - eye_b) / upm_b - R(delta) * H_a
    double tx, tz, hx, hz;
    tx = m00 * (a.ex - b.ex) + m01 * (a.ez - b.ez);
    tz = m10 * (a.ex - b.ex) + m11 * (a.ez - b.ez);
    ry(m.delta, a.hx, a.hz, &hx, &hz);
    m.sx = b.hx + tx / b.upm - hx;
    m.sz = b.hz + tz / b.upm - hz;
    m.ok = std::isfinite(m.delta + m.sx + m.sz);
    return m;
}

}  // namespace worldanchor
