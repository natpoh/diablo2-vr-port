#pragma once
// low_hp_eq.h - the low-life and low-mana vignettes' "equalizer" (Serg, 2026-11-03: "how strongly the vignette closes in over the view at
// each % of life"): [hud] low_hp_cover_eq for the red all round (vr/low_hp.h), low_mp_cover_eq for the blue at the bottom (vr/low_mp.h).
// Pure: no game, no Windows, no globals; tested in cleanroom/tests/low_hp_eq_test.cpp.
//
// The player sets the COVER at fixed points of the life (or the mana), a vertical slider each like the bands of a sound equalizer: 60%,
// 55% ... 5% of the maximum. The cover is 0 - 100: how far the colour closes in - for the red, how far in from the edges it reaches, in %
// of the view's angle (the place of "Vignette size", 100 = up to the very middle); for the blue, how far up from the bottom edge, in % of
// the view's half-height (the place of "Height", 100 = up to the middle row). A small cover is also fainter (under kFullAt the colour's
// strength falls with it, to nothing at 0), so 0 is no vignette at all and the steps up from it never show a thin solid rim. Between two
// points the cover goes in a straight line of the life; above the highest point it is the highest point's, under the lowest the lowest's.
// The vignette still comes on only under its threshold: a point above it never shows. The beat (its rate, its shape) is as without it.
//
// Off (the tick unticked, or missing): the slider of the size as before, nothing of this.

#include <cmath>

namespace lowhp::eq {

constexpr int kBands = 12;
constexpr int kLifePct[kBands] = {60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5};   // the points, % of the maximum, highest first
constexpr float kCoverMin = 0.0f, kCoverMax = 100.0f;
constexpr float kFullAt = 0.30f;   // a cover (0 - 1) from here up has the colour's full strength; under it the strength falls in a line to 0

struct Bands {
    bool on = false;                // the tick
    float cover[kBands] = {};       // % (0 - 100), at kLifePct
};

// Each point put in range; one that is not a number takes its default.
inline Bands Sanitized(Bands b, const float (&defaults)[kBands]) {
    for (int k = 0; k < kBands; ++k) {
        const float v = b.cover[k];
        b.cover[k] = !std::isfinite(v) ? defaults[k] : v < kCoverMin ? kCoverMin : v > kCoverMax ? kCoverMax : v;
    }
    return b;
}
inline Bands Filled(const float (&values)[kBands], bool on = false) {
    Bands b;
    b.on = on;
    for (int k = 0; k < kBands; ++k) b.cover[k] = values[k];
    return b;
}

// The cover at a life (life over maximum life, 0 - 1), 0 - 1: the points' straight lines, held past the ends. `b` is a Sanitized one.
inline float CoverAt(const Bands& b, float frac) {
    const float pct = std::isfinite(frac) ? frac * 100.0f : 0.0f;
    float v = b.cover[kBands - 1];
    if (pct >= (float)kLifePct[0]) v = b.cover[0];
    else
        for (int k = 0; k + 1 < kBands; ++k) {
            const float hi = (float)kLifePct[k], lo = (float)kLifePct[k + 1];
            if (pct >= lo) { v = b.cover[k + 1] + (b.cover[k] - b.cover[k + 1]) * (pct - lo) / (hi - lo); break; }
        }
    return v * 0.01f;
}

// The colour's strength at a cover (0 - 1): full from kFullAt up, a straight line down to nothing at 0.
inline float Strength(float cover) {
    if (!std::isfinite(cover) || cover <= 0.0f) return 0.0f;
    return cover >= kFullAt ? 1.0f : cover / kFullAt;
}

}  // namespace lowhp::eq
