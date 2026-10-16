#pragma once
// How the right stick turns: [turn] smooth, snap and speed. One place for the rule, so the camera (vrcam.cpp), the
// in-headset settings panel (vr/hset_store.cpp) and D2R VR Settings (tools/settings) agree.
//
//   smooth  1 = smooth turning at `speed`, 0 = one snap of `snap` degrees per flick. Its own key, so switching never
//           loses the angle (and the other way round).
//   snap    degrees per flick, 15 - 180, used while smooth is off.
//   speed   degrees per second at full deflection, 10 - 720, used while smooth is on.
//
// An older ini has no `smooth` and says "snap=0" for smooth turning (snap > 0 for snap turning). It keeps its mode: without
// the key, snap=0 is smooth turning, and a snap angle chosen later starts from 30.
//
// The keys, their ranges and the legacy rule are bearstonem/d2r-pcvr's (steamvr/vr_settings.h, MIT:
// SmoothTurning / SnapDegrees / EffectiveSnapAngle), written again here.

#include <algorithm>
#include <cmath>

namespace turnmode {

constexpr float kSnapMin = 15.0f, kSnapMax = 180.0f, kSnapDefault = 30.0f;
constexpr float kSpeedMin = 10.0f, kSpeedMax = 720.0f, kSpeedDefault = 120.0f;

// `snapRaw`: [turn] snap as the file has it (0 when missing). `smoothKey`: [turn] smooth as the file has it, any
// negative number (or not a number) when the key is missing.
inline bool Smooth(float snapRaw, float smoothKey) {
    if (!(smoothKey >= 0.0f)) return snapRaw == 0.0f;   // an older ini: snap=0 is smooth turning
    return smoothKey > 0.5f;
}
// The angle a snap turn uses (and the panel shows): 0 or not a number is the default, the rest is kept inside the range.
inline float SnapDegrees(float snapRaw) {
    if (!std::isfinite(snapRaw) || snapRaw == 0.0f) return kSnapDefault;
    return std::clamp(snapRaw, kSnapMin, kSnapMax);
}
// What the camera does with a flick: 0 = smooth turning, else the angle of one snap.
inline float EffectiveSnapAngle(float snapRaw, float smoothKey) { return Smooth(snapRaw, smoothKey) ? 0.0f : SnapDegrees(snapRaw); }
// The smooth turn's speed, deg/s.
inline float Speed(float raw) { return std::isfinite(raw) ? std::clamp(raw, kSpeedMin, kSpeedMax) : kSpeedDefault; }

}  // namespace turnmode
