// held_bounds.h - [weight] knock_down: a body drawn away from where the game has it (held up in the air, thrown, walking home) kept in view. Pure: no game,
// no windows.h (cleanroom/tests/knock_pose_test.cpp feeds it).
//
// Serg on 0.192.33: «почему-то модельки нпс пропадают когда я их поднимаю». The renderer keeps a box per entity (its bounds component, 0x60 bytes: the
// local min / max as vec4s, then the world min / max - vr/knock_props.cpp found it for the props) and leaves out what that box says is out of view. A
// unit's box is its game place's: the body we draw lifted 1.5 m up or 2 m away is left out as soon as that place is - the model vanishes. Each part
// entity's local box is grown, while it is drawn so, to hold where it is drawn (its matrix run back), and its world box with it; then put back as it was.
#pragma once

#include <algorithm>
#include <cmath>

namespace heldbounds {

// The box b (24 floats: local min xyz_ max xyz_, world min xyz_ max xyz_ - the component as the game keeps it) holds lo..hi in the world?
inline bool Holds(const float b[24], const float lo[3], const float hi[3]) {
    for (int k = 0; k < 3; ++k)
        if (!(b[16 + k] <= lo[k] && b[20 + k] >= hi[k])) return false;
    return true;
}

// An affine row-vector matrix's inverse (the translation in [12..14], as the game's); false when it does not invert.
inline bool Invert(const float m[16], float o[16]) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (!(std::fabs(det) > 1e-12f) || !std::isfinite(det)) return false;
    const float r[9] = {(e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det, (f * g - d * k) / det, (a * k - c * g) / det,
                        (c * d - a * f) / det, (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) o[i * 4 + j] = r[i * 3 + j];
        o[i * 4 + 3] = 0.0f;
    }
    for (int j = 0; j < 3; ++j) o[12 + j] = -(m[12] * r[j] + m[13] * r[3 + j] + m[14] * r[6 + j]);
    o[15] = 1.0f;
    return true;
}

// p through m (row vector).
inline void Through(const float m[16], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
}

// The world box of a local box under m (every corner through it).
inline void WorldOf(const float lmin[3], const float lmax[3], const float m[16], float wmin[3], float wmax[3]) {
    for (int k = 0; k < 3; ++k) { wmin[k] = 1e30f; wmax[k] = -1e30f; }
    for (int q = 0; q < 8; ++q) {
        const float p[3] = {q & 1 ? lmax[0] : lmin[0], q & 2 ? lmax[1] : lmin[1], q & 4 ? lmax[2] : lmin[2]};
        float w[3];
        Through(m, p, w);
        for (int k = 0; k < 3; ++k) { wmin[k] = std::min(wmin[k], w[k]); wmax[k] = std::max(wmax[k], w[k]); }
    }
}

// The bounds grown to hold the world box lo..hi: `orig` (24 floats, as the game had them) with its local box grown by lo..hi run back through m (the entity's
// world matrix now) and its world box made of that under m - into out (24 floats; the w lanes kept). False when m does not invert or a number is not finite.
inline bool Grown(const float orig[24], const float m[16], const float lo[3], const float hi[3], float out[24]) {
    float inv[16];
    if (!Invert(m, inv)) return false;
    for (int i = 0; i < 24; ++i) out[i] = orig[i];
    float lmin[3] = {orig[0], orig[1], orig[2]}, lmax[3] = {orig[4], orig[5], orig[6]};
    for (int q = 0; q < 8; ++q) {
        const float p[3] = {q & 1 ? hi[0] : lo[0], q & 2 ? hi[1] : lo[1], q & 4 ? hi[2] : lo[2]};
        float l[3];
        Through(inv, p, l);
        for (int k = 0; k < 3; ++k) { lmin[k] = std::min(lmin[k], l[k]); lmax[k] = std::max(lmax[k], l[k]); }
    }
    float wmin[3], wmax[3];
    WorldOf(lmin, lmax, m, wmin, wmax);
    for (int k = 0; k < 3; ++k) {
        out[k] = lmin[k];
        out[4 + k] = lmax[k];
        out[16 + k] = wmin[k];
        out[20 + k] = wmax[k];
    }
    for (int i = 0; i < 24; ++i)
        if (!std::isfinite(out[i])) return false;
    return true;
}

}  // namespace heldbounds
