// flat_shift.h - [camera] flat_shift_x / flat_shift_y: flat mode with our camera (F2, F3), the whole picture moved on the screen by a share of its
// width and height - an off-axis projection, so what is drawn under a pixel is still what the game's mouse ray through it meets (d2rcam's
// ScreenToRay inverts the same projection). And where a world point lands on the picture, for the log that says where the hero stands on it.
// Pure; the test is cleanroom/tests/flat_shift_test.cpp, the wiring vr/vrcam.cpp (VrProjInner, FlatShiftLog).
//
// A player (2026-10-27, flat): "would be amazing if the centering would be fixed in the Flat version, I think that's why it feels so clunky melee
// attacking and whiffing every attack" - his screenshot has the hero drawn up and to the left of the gold ring under him. The author wants a slider
// to move the picture himself now; the log line gives the numbers a default from the game could come from later.
#pragma once

#include <algorithm>
#include <cmath>

namespace flatshift {

constexpr float kMaxPct = 50.0f;   // half the picture either way

inline float Clamp(float pct) { return std::isfinite(pct) ? std::clamp(pct, -kMaxPct, kMaxPct) : 0.0f; }

// The projection (row vectors, clip = v * M, w = -z: the game's and ours) moved by pctX of the picture's width to the right and pctY of its
// height down. ndc.x = (M[0] x + M[8] z) / -z = ... - M[8]: the picture moves right by -dM[8]; ndc.y likewise by -dM[9], and ndc +y is up.
inline void Apply(float M[16], float pctX, float pctY) {
    M[8] -= 0.02f * Clamp(pctX);
    M[9] += 0.02f * Clamp(pctY);
}

// Where the world point p lands on the picture: the share of the width right of the middle and of the height below it, in percent (the slider's
// terms - moving the picture by minus these puts p in the middle). False: behind the eye or no number.
inline bool OnPicture(const float view[16], const float proj[16], const float p[3], float* pctX, float* pctY) {
    float v[4], c[4];
    for (int j = 0; j < 4; ++j) v[j] = p[0] * view[0 * 4 + j] + p[1] * view[1 * 4 + j] + p[2] * view[2 * 4 + j] + view[3 * 4 + j];
    for (int j = 0; j < 4; ++j) c[j] = v[0] * proj[0 * 4 + j] + v[1] * proj[1 * 4 + j] + v[2] * proj[2 * 4 + j] + v[3] * proj[3 * 4 + j];
    if (!(c[3] > 1e-6f)) return false;
    *pctX = 50.0f * c[0] / c[3];
    *pctY = -50.0f * c[1] / c[3];
    return std::isfinite(*pctX) && std::isfinite(*pctY);
}

}  // namespace flatshift
