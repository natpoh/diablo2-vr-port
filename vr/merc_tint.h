// merc_tint.h - [hud] merc_tint: in VR F4 a wounded mercenary is tinted red, drawn by us (vr/merc_tint.cpp, the effect's MERC block).
//
// What it shows. While the hero's own mercenary is below [hud] merc_tint_threshold of its life (50 % by default), the effect
// (reshade/D2R_DepthFog.fx, MercTint) tints red what the game drew inside the merc's body: per eye and per pixel the game's depth
// puts the drawn point back in the world, and a point within the merc's capsule takes the red - the merc's own body, its armour,
// its weapon, but not the ground under it (the capsule ends a radius above the floor) and not what stands in front of it (that
// point is elsewhere). The capsule is the one hit by touch sweeps (vr/melee_bodies.h BodyFor): the game's MonStats footprint and
// height of the merc's class at its place on the grid.
//
// What it does not do. The idea is bearstonem/d2r-pcvr's (v0.3.17, vr/melee.cpp): there a state is switched on the merc on the
// server and the game's colour update is hooked so its red wins. Here nothing of the game's is written or called that acts: the merc
// is found in the client's unit hash, its life read through the game's stat getter (read-only, as the hero's for the low-life
// vignette), and the colour is only ours, laid over the finished picture. Off (the default) nothing is read and the picture is as it was.
//
// Pure: no game, no Windows, no clock; cleanroom/tests/merc_tint_test.cpp drives it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "melee_bodies.h"

namespace merctint {

// The hireling classes of the game's own tables (MonStats rows of hireling.txt: roguehire, act2hire, act3hire, act5hire1, act5hire2) -
// offline the one live monster of these classes in the client's units is the hero's own merc. The list is bearstonem/d2r-pcvr's (MIT).
constexpr uint32_t kHirelingClasses[] = {271, 338, 359, 560, 561};
constexpr size_t kHirelingCount = sizeof kHirelingClasses / sizeof kHirelingClasses[0];
inline bool IsHireling(uint32_t cls) {
    for (uint32_t c : kHirelingClasses) if (c == cls) return true;
    return false;
}

// The game's life stats are kept with eight fractional bits (stat 6 the life, 7 its maximum).
constexpr float kStatScale = 256.0f;
constexpr int kStatLife = 6, kStatMaxLife = 7;

struct Config {
    bool on = false;          // [hud] merc_tint
    float threshold = 0.5f;   // [hud] merc_tint_threshold (percent in the ini): below this fraction of its life the merc is tinted
    float strength = 0.6f;    // [hud] merc_tint_strength 0 - 1: how far the merc's colours go toward the red
};

inline Config Sanitized(Config c) {
    if (!std::isfinite(c.threshold)) c.threshold = 0.5f;
    if (!std::isfinite(c.strength)) c.strength = 0.6f;
    c.threshold = std::clamp(c.threshold, 0.05f, 0.95f);
    c.strength = std::clamp(c.strength, 0.0f, 1.0f);
    return c;
}

// The ini's percent (10 - 90) as the fraction.
inline float ThresholdFromPercent(float pct) { return std::isfinite(pct) ? pct * 0.01f : 0.5f; }

// The merc's life from the game's two raw stats: known only when they make sense (a maximum above 0, the life not above it by far).
struct Life { bool known = false; float frac = 1.0f; };
inline Life LifeFromStats(int32_t life, int32_t maxLife) {
    Life l;
    if (maxLife <= 0 || life < 0 || life > maxLife * 2) return l;
    l.known = true;
    l.frac = std::clamp((float)life / (float)maxLife, 0.0f, 1.0f);
    return l;
}

// Tinted now: on, the life known and fresh, alive (above 0) and below the threshold.
inline bool Show(const Config& c, const Life& l, bool fresh) {
    return c.on && l.known && fresh && l.frac > 0.0f && l.frac < Sanitized(c).threshold;
}

// A life read is fresh this long (the UI thread reads it five times a second; a load or a stall that stops it ends the tint).
constexpr uint64_t kFreshMs = 1000;
inline bool Fresh(uint64_t nowMs, uint64_t atMs) { return atMs != 0 && nowMs >= atMs && nowMs - atMs <= kFreshMs; }

// The capsule as the effect takes it: MercA = (bottom, radius), MercB = (top, strength), world units relative to where the hands
// hang (melee_boxes.h Pack's frame: render metres * upm - eyeWorld). False when the body does not read like one.
struct Uniforms { float a[4] = {}, b[4] = {}; };
inline bool Pack(const stroke::Body& body, float upm, const float eyeWorld[3], float strength, Uniforms* out) {
    if (!out || !std::isfinite(upm) || upm <= 0.0f || !stroke::Finite(body.bottom) || !stroke::Finite(body.top) ||
        !std::isfinite(body.radius) || body.radius <= 0.0f)
        return false;
    out->a[0] = body.bottom.x * upm - eyeWorld[0]; out->a[1] = body.bottom.y * upm - eyeWorld[1]; out->a[2] = body.bottom.z * upm - eyeWorld[2];
    out->a[3] = body.radius * upm;
    out->b[0] = body.top.x * upm - eyeWorld[0]; out->b[1] = body.top.y * upm - eyeWorld[1]; out->b[2] = body.top.z * upm - eyeWorld[2];
    out->b[3] = std::clamp(strength, 0.0f, 1.0f);
    return true;
}

// The effect's sums on the CPU (the MERC block of D2R_DepthFog.fx does the same): how much of the tint a drawn point q gets - 1 well
// inside the capsule, 0 outside, a soft edge over the outer fifth of the radius.
inline float Inside(const float q[3], const Uniforms& u) {
    const float ab[3] = {u.b[0] - u.a[0], u.b[1] - u.a[1], u.b[2] - u.a[2]};
    const float aq[3] = {q[0] - u.a[0], q[1] - u.a[1], q[2] - u.a[2]};
    const float abab = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    const float k = abab > 1e-8f ? std::clamp((aq[0] * ab[0] + aq[1] * ab[1] + aq[2] * ab[2]) / abab, 0.0f, 1.0f) : 0.0f;
    const float d[3] = {q[0] - (u.a[0] + ab[0] * k), q[1] - (u.a[1] + ab[1] * k), q[2] - (u.a[2] + ab[2] * k)};
    const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float r = u.a[3];
    if (!(r > 0.0f)) return 0.0f;
    const float x = std::clamp((r - dist) / (0.2f * r), 0.0f, 1.0f);   // smoothstep(r, 0.8 r, dist)
    return x * x * (3.0f - 2.0f * x);
}
// The colour: toward the red by strength * inside - the brightness kept in the red, green and blue let down.
inline void Tint(float rgb[3], float amount) {
    const float luma = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
    const float red[3] = {std::max(rgb[0], luma * 1.25f), rgb[1] * 0.3f, rgb[2] * 0.3f};
    const float t = std::clamp(amount, 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) rgb[i] += (red[i] - rgb[i]) * t;
}

}  // namespace merctint
