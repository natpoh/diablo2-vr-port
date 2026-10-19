// stick_pitch.h - the right stick's up / down tilts the camera in VR F2 (third person): [camera] stick_pitch_f2, stick_pitch_speed,
// stick_pitch_min / stick_pitch_max and stick_pitch_sign. Pure: no Windows, no game; the unit test is cleanroom/tests/stick_pitch_test.cpp,
// the wiring is vrcam's TurnTick (the stick, on the 1 ms timer next to the turn) and TargetPitch (the tilt on top of [camera] pitch).
//
// A player's wish (GitHub issue 11): "there is no up/down camera with a controller in VRmode" - with an Xbox pad on "Mono - a flat
// screen" only the mouse and the headset tilt the view. The right stick is already the plugin's while [turn] right_stick is on (its X
// turns, the game never sees the stick), so its Y is free for this.
//
// The tilt is an offset on top of [camera] pitch, in the same degrees (more negative = looks higher, -60 = the horizon). It moves at
// `speed` degrees a second at a full push, with the turn's shaping: nothing inside the dead zone (0.25, the smooth turn's), then the
// square of the push past it, so a small push is a slow tilt. Only a push that is more up / down than sideways tilts: a turn pushed a
// little off the horizontal does not tilt by accident. Up looks higher (sign 1), -1 the other way round.
//
// The limits are pitches of their own, [camera] pitch's units and range: the camera never looks higher than `min` nor lower than
// `max` through the stick. A [camera] pitch already outside them is not pulled in: the stick at rest is the camera as before, and the
// stick only ever moves it back towards the limits, never past where [camera] pitch put it.
#pragma once

#include <cmath>

namespace stickpitch {

constexpr float kDead = 0.25f;                          // the smooth turn's dead zone (vrcam TurnTick)
constexpr float kSpeedMin = 10.0f, kSpeedMax = 360.0f, kSpeedDefault = 60.0f;
constexpr float kPitchMin = -150.0f, kPitchMax = 60.0f; // [camera] pitch's own range (vrcam's ini read, TargetPitch)
constexpr float kMinDefault = -90.0f;                   // 30 degrees above the horizon
constexpr float kMaxDefault = 0.0f;                     // the game's own steep camera

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// The speed, deg/s at a full push: a value that is not a number takes the default.
inline float Speed(float raw) { return std::isfinite(raw) ? Clamp(raw, kSpeedMin, kSpeedMax) : kSpeedDefault; }

// The limits from the ini: each in [camera] pitch's range (not a number: the default), the higher look first.
struct Limits { float min = kMinDefault, max = kMaxDefault; };
inline Limits FromIni(float minRaw, float maxRaw) {
    Limits l;
    l.min = std::isfinite(minRaw) ? Clamp(minRaw, kPitchMin, kPitchMax) : kMinDefault;
    l.max = std::isfinite(maxRaw) ? Clamp(maxRaw, kPitchMin, kPitchMax) : kMaxDefault;
    if (l.min > l.max) { const float t = l.min; l.min = l.max; l.max = t; }
    return l;
}

// How far the tilt moves (degrees, + looks lower) in `dt` seconds for a stick at (x, y) (-1..1, y + up). 0 inside the dead zone, for a
// push more sideways than up / down, and for a stick that is not a number.
inline float Step(float x, float y, float dt, float speed, float sign) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dt) || !(dt > 0.0f)) return 0.0f;
    const float a = fabsf(y);
    if (a < kDead || a <= fabsf(x)) return 0.0f;
    const float k = Clamp((a - kDead) / (1.0f - kDead), 0.0f, 1.0f);   // 0..1 past the dead zone
    return -(y > 0.0f ? 1.0f : -1.0f) * (sign < 0.0f ? -1.0f : 1.0f) * k * k * speed * dt;
}

// The offset moved by `step` and kept so that `base` ([camera] pitch) + the offset stays inside the limits - or, with `base` itself outside
// them, between `base` and the nearer limit (0 is always allowed: the stick at rest is the camera as before).
inline float Apply(float offset, float step, float base, const Limits& l) {
    const float lo = (base < l.min ? base : l.min) - base;
    const float hi = (base > l.max ? base : l.max) - base;
    if (!std::isfinite(offset)) offset = 0.0f;
    const float v = offset + step;
    return std::isfinite(v) ? Clamp(v, lo, hi) : Clamp(offset, lo, hi);
}

}  // namespace stickpitch
