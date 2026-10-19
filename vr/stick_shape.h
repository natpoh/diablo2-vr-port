// stick_shape.h - a thumbstick's dead zone against drift: [direct] stick_deadzone_left_pct / stick_deadzone_right_pct and stick_outer_left_pct /
// stick_outer_right_pct. Pure: no Windows, no OpenXR, no game; the unit test is cleanroom/tests/stick_shape_test.cpp, the wiring is
// vr/direct_input.cpp (the keys, read live) and the two places a VR controller's stick comes into the plugin: vr/xr.cpp's Publish (native OpenXR)
// and vrcam's FlatRawTick (FlatVR's raw controllers through Game Link).
//
// A player's wish (2026-10-18): "are you able to add stick dead zone %'s like to counter stick drifts people might have on their controllers".
// A stick that drifts reads a little off the middle at rest (0.05 - 0.1 is common, worn ones more). The game's own dead zone on the pad (0.316,
// 10349 of 32767) hides most of it from the walk, but not from what reads the stick under that: the right stick's smooth turn (past 0.25),
// the belt flick's and the skill panel's re-arm (back inside 0.30 / 0.25), the room walk's slow tilt, the laser's "the pad is navigating" (0.35).
//
// The shape is RADIAL (the length of the stick decides, not each axis on its own: a diagonal is not cut sooner than a straight push, and the
// direction is kept) and RESCALED: just past the inner dead zone the length starts again from 0, so there is no jump, and at the outer edge it
// reaches 1 - every reader after this sees a stick that goes 0..1 over the part of the throw that is left, and its own thresholds (a flick at 0.7,
// the game's dead zone, the turn's) keep meaning "that much of a full push". The outer edge saturates: a stick that never quite reaches its rim
// still gives a full push from there on.
//
//     length <= inner              -> 0
//     inner < length < outer       -> (length - inner) / (outer - inner), the same direction
//     length >= outer              -> 1, the same direction
//
// The defaults (inner 0 %, outer 100 %) are no shape at all: the stick goes on exactly as the runtime gave it, bit for bit, a corner past 1 and a
// NaN included - what the plugin did before these keys.
#pragma once

#include <cmath>

namespace stickshape {

constexpr float kInnerMaxPct = 50.0f;     // the largest inner dead zone: half the throw
constexpr float kOuterMinPct = 60.0f;     // the smallest outer edge (always past the largest inner one)
constexpr float kInnerDefaultPct = 0.0f;  // the defaults: no shape (as before)
constexpr float kOuterDefaultPct = 100.0f;

// One stick's shape, as fractions of a full push.
struct Zone {
    float inner = 0.0f;   // at or under this length: the middle
    float outer = 1.0f;   // at or past this length: a full push
};

// The ini's percentages made a Zone: inner 0..50, outer 60..100; a value that is not a number takes the default.
inline Zone FromPercent(float innerPct, float outerPct) {
    if (!std::isfinite(innerPct)) innerPct = kInnerDefaultPct;
    if (!std::isfinite(outerPct)) outerPct = kOuterDefaultPct;
    innerPct = innerPct < 0.0f ? 0.0f : innerPct > kInnerMaxPct ? kInnerMaxPct : innerPct;
    outerPct = outerPct < kOuterMinPct ? kOuterMinPct : outerPct > 100.0f ? 100.0f : outerPct;
    return Zone{innerPct / 100.0f, outerPct / 100.0f};
}

// No dead zone and no saturation: Apply leaves the stick as it is.
inline bool IsIdentity(const Zone& z) { return !(z.inner > 0.0f) && !(z.outer < 1.0f); }

// The length a stick of length `len` (>= 0) comes out with.
inline float ShapeLength(const Zone& z, float len) {
    if (!(len > z.inner)) return 0.0f;
    if (len >= z.outer) return 1.0f;
    return (len - z.inner) / (z.outer - z.inner);
}

// The stick (x right, y up, about -1..1) shaped in place. Identity: untouched. A value that is not a number: untouched too (every reader
// already takes such a stick as centred).
inline void Apply(const Zone& z, float* x, float* y) {
    if (IsIdentity(z) || !std::isfinite(*x) || !std::isfinite(*y)) return;
    const float len = std::sqrt(*x * *x + *y * *y);
    const float out = ShapeLength(z, len);
    if (out <= 0.0f) { *x = 0.0f; *y = 0.0f; return; }
    const float k = out / len;
    *x *= k;
    *y *= k;
}

}  // namespace stickshape
