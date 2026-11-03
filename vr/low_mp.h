#pragma once
// low_mp.h - [hud] low_mp_vignette: the low-MANA vignette (Serg, 2026-11-03: "for the mana a vignette with an equalizer too, but it
// blinks only at the bottom, not all round - so it does not drown the life's one"; the equalizer sets how far it closes in). Pure: no game, no Windows, no globals, no clock;
// tested in cleanroom/tests/low_mp_test.cpp (with the effect's own HLSL, LOWMP marks in reshade/D2R_DepthFog.fx). The plumbing - the mana
// read beside the life, the beat stepped once a pair, the effect's uniforms - is vr/low_hp.cpp, beside the life's.
//
// What the player sees: while the hero's mana is under its threshold a deep BLUE band rises from the BOTTOM edge of the view and beats -
// the life vignette's double beat (its plain rate, 60 up to 90 a minute). How far up it reaches is the Height slider, or - with
// [hud] low_mp_cover_eq - the mana's own equalizer (vr/low_hp_eq.h: the cover at 12 points of the mana, 60% ... 5%; 100 = up to the
// middle row, 0 = none). The sides and the top stay clear, so the red all round of the low life (vr/low_hp.h) is never covered:
// the effect lays the blue first and the red over it (LowHp(LowMp(...))), so where both are - the bottom edge - the red's beat still reads
// at its full strength, and the blue is lower and thinner by default. Nothing when dead, in a menu, or anywhere but first person VR (F4).
//
// The beat is lowhp::Heartbeat's (the fade, the hysteresis, the double beat, the equalizer's cover) fed with the mana instead of the life
// (Beat below): one rhythm for both, written once. It has no pulse in the controllers - the low-mana alert in the hand is [openxr]
// haptics_low_mana's, a pulse every ten seconds, as before.

#include <algorithm>
#include <cmath>

#include "low_hp.h"

namespace lowmp {

struct Config {
    bool on = false;                 // [hud] low_mp_vignette
    float threshold = 0.30f;         // [hud] low_mp_threshold, a fraction of the maximum mana (the ini has percent, 10 - 60)
    float intensityStart = 0.20f;    // [hud] low_mp_intensity_start: the blue's opacity at the top of a beat just under the threshold
    float intensityEnd = 0.55f;      // [hud] low_mp_intensity_end: the same at no mana; in between a straight line of the mana
    float height = 0.25f;            // [hud] low_mp_height: how far up from the bottom edge it reaches, a share of the view's half-angle (the ini has percent, 10 - 60)
    float softness = 0.80f;          // [hud] low_mp_softness: 1 a fade all the way up, 0 a short ramp then flat to the edge
    lowhp::eq::Bands coverEq = lowhp::eq::Filled(kCoverDefault);   // [hud] low_mp_cover_eq, low_mp_cover_eq_<percent>: how far up at points of the mana; off = height

    // The points' defaults: low at 60%, the height's own default (25) at the default threshold (30%), over half of the lower half at 5%.
    static constexpr float kCoverDefault[lowhp::eq::kBands] = {12, 13, 15, 17, 19, 22, 25, 29, 34, 40, 47, 55};
};

constexpr float kThresholdMin = 0.10f, kThresholdMax = 0.60f, kHeightMin = 0.10f, kHeightMax = 0.60f;
constexpr const wchar_t* kCoverKeys[lowhp::eq::kBands] = {L"low_mp_cover_eq_60", L"low_mp_cover_eq_55", L"low_mp_cover_eq_50", L"low_mp_cover_eq_45",
                                                          L"low_mp_cover_eq_40", L"low_mp_cover_eq_35", L"low_mp_cover_eq_30", L"low_mp_cover_eq_25",
                                                          L"low_mp_cover_eq_20", L"low_mp_cover_eq_15", L"low_mp_cover_eq_10", L"low_mp_cover_eq_5"};

inline Config Sanitized(Config c) {
    const Config d;
    c.threshold = lowhp::Clamp(c.threshold, kThresholdMin, kThresholdMax, d.threshold);
    c.intensityStart = lowhp::Clamp(c.intensityStart, lowhp::kIntensityMin, lowhp::kIntensityMax, d.intensityStart);
    c.intensityEnd = lowhp::Clamp(c.intensityEnd, lowhp::kIntensityMin, lowhp::kIntensityMax, d.intensityEnd);
    c.height = lowhp::Clamp(c.height, kHeightMin, kHeightMax, d.height);
    c.softness = lowhp::Clamp(c.softness, 0.0f, 1.0f, d.softness);
    c.coverEq = lowhp::eq::Sanitized(c.coverEq, Config::kCoverDefault);
    return c;
}

// ---- the beat: the life's Heartbeat, fed the mana ---------------------------------------------------------------

// The life vignette's config that beats as this one: its threshold and intensities, the mana's equalizer, never gentle, no haptics.
inline lowhp::Config BeatConfig(const Config& config) {
    const Config c = Sanitized(config);
    lowhp::Config b;
    b.on = c.on;
    b.threshold = c.threshold;
    b.intensityStart = c.intensityStart;
    b.intensityEnd = c.intensityEnd;
    b.coverEq = c.coverEq;
    return b;
}
// What Heartbeat is told of the mana. No mana at all is the most urgent, not "no read" (the life's 0 is death): it goes in as a hair over 0.
// `dead`: the HERO is dead (the life's read) - then nothing, as the life's vignette.
constexpr float kNoMana = 1e-4f;
inline lowhp::Heartbeat::In BeatIn(bool inView, bool menu, bool manaKnown, float manaFrac, bool dead, float dt) {
    lowhp::Heartbeat::In in;
    in.inView = inView;
    in.menu = menu;
    in.lifeKnown = manaKnown && std::isfinite(manaFrac);
    in.dead = dead;
    in.frac = std::isfinite(manaFrac) ? std::max(manaFrac, kNoMana) : 1.0f;
    in.dt = dt;
    return in;
}

// ---- the band at the bottom (mirrored in reshade/D2R_DepthFog.fx, LowMp) ---------------------------------------

// How far down a picture point is, in parts of the view's vertical ANGLE: 0 at the middle row and above, 1 at the bottom edge's middle
// row. With the field of view (tanY, the half-height as a tangent) the angle it looks at; 0 = the picture's own linear v.
inline float Down(float v, float tanY = 0.0f) {
    float y = std::max(0.0f, v * 2.0f - 1.0f);
    if (tanY > 0.0f) y = std::atan(y * tanY) / std::atan(tanY);
    return y;
}

// Where the blue is: nothing above `inner`, rising to all of it at `outer` and flat past that to the edge (the life's ramp rule:
// lowhp::kShortestRamp, kOverEdge).
struct Shape {
    float inner = 0.75f, outer = 1.10f, tanY = 0.0f;
};
// `cover` (0 - 1, the equalizer's: Heartbeat::Out::cover) is the height instead of the config's - up to the middle row (1); -1 = the height.
inline Shape ShapeOf(const Config& config, float tanY = 0.0f, float cover = -1.0f) {
    const Config c = Sanitized(config);
    const float h = cover >= 0.0f && std::isfinite(cover) ? std::clamp(cover, lowhp::kCoverReachMin, 1.0f) : c.height;
    Shape s;
    s.inner = 1.0f - h;
    s.outer = s.inner + (h + lowhp::kOverEdge) * (lowhp::kShortestRamp + (1.0f - lowhp::kShortestRamp) * c.softness);
    s.tanY = tanY > 0.0f && std::isfinite(tanY) ? tanY : 0.0f;
    return s;
}
inline float Mask(float u, float v, const Shape& s) {
    (void)u;   // (the band runs the whole width)
    return lowhp::Smoothstep(s.inner, s.outer, Down(v, s.tanY));
}

// A deep mana blue (tuned beside the life's red; dark, so it reads as a shade, not a light).
constexpr float kBlue[3] = {0.01f, 0.07f, 0.36f};

inline void Apply(const float in[3], float u, float v, float alpha, const Shape& shape, const float color[3], float out[3]) {
    const float k = lowhp::Clamp(alpha, 0.0f, 1.0f, 0.0f) * Mask(u, v, shape);
    for (int i = 0; i < 3; ++i) out[i] = k > 0.0f ? in[i] + (color[i] - in[i]) * k : in[i];
}

// What the effect is handed each frame: the opacity, the shape (inner, outer, tanY) and the colour. (The effect's own defaults, before
// it is handed any: this struct's.)
struct Uniforms {
    float alpha = 0.0f;
    float shape[4] = {0.75f, 1.10f, 0.0f, 0.0f};
    float color[3] = {0.01f, 0.07f, 0.36f};
};
inline Uniforms MakeUniforms(float alpha, const Config& config, float tanY = 0.0f, float cover = -1.0f) {
    Uniforms u;
    u.alpha = lowhp::Clamp(alpha, 0.0f, 1.0f, 0.0f);
    const Shape s = ShapeOf(config, tanY, cover);
    u.shape[0] = s.inner;
    u.shape[1] = s.outer;
    u.shape[2] = s.tanY;
    for (int i = 0; i < 3; ++i) u.color[i] = kBlue[i];
    return u;
}

}  // namespace lowmp
