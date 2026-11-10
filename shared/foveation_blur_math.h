// foveation_blur_math.h - [render] foveated_blur (reshade/D2R_DepthFog.fx FovBlur*): foveated rendering's coarse pixels softened by a blur of
// their own (Serg, 2026-11-10: "a tick to blur the coarse pixels round the edge so they don't cut the eyes" - "we already have a blur
// filter" - "blur with another effect", and the distance blur keeps working by distance). The blur is the distance blur's ([render]
// distance_blur, D2R_DepthFog.fx DistBlur*): the same two passes, across then down, the same 9 Gaussian taps a quarter of the radius apart,
// the interface neither blurred nor taken in - but a separate effect with its own texture, switch and strength; the radius comes from how
// coarse the pixel's tile was shaded, not from its distance. The distance blur is untouched; both on: this one runs after it, the two add
// (as two panes of frosted glass, one behind the other).
//
// Where and how much: the rate image's own rule (unleashed_foveation.h TileRate, that eye's region - the lens centre or the gaze cell):
// the block size of the pixel's tile (1, 2 or 4) times strength x 1.5 pixels, faded in over the first 30 % past the sharp ellipse's edge (a
// ring at the edge would be the tiles' outline again). 1x1 tiles: no blur. This is the CPU reference of the shader's sums; ours (D2R VR
// Authentic, MIT), calling into unleashed_foveation.h (MPL 2.0). Tested: cleanroom/tests/foveation_blur_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "unleashed_foveation.h"

namespace foveablur {

// The eye's region of its last pass, in the picture's pixels (what vrcam hands the effect as uniforms)
struct Region {
    float vx = 0, vy = 0, vw = 0, vh = 0;   // the scene viewport
    uint32_t tile = 16;
    uint8_t coarse = unleashed::foveation::kRate2x2, unseen = 0;
    unleashed::foveation::Region region;
    bool ok = false;
};

inline int BlockOf(uint8_t rate) { return rate == unleashed::foveation::kRate4x4 ? 4 : rate == unleashed::foveation::kRate2x2 ? 2 : 1; }

// The block size of the tile under pixel (x, y)
inline int BlockAt(const Region& r, float x, float y) {
    if (!r.ok || !r.tile || x < 0 || y < 0) return 1;
    return BlockOf(unleashed::foveation::TileRate((uint32_t)x / r.tile, (uint32_t)y / r.tile, r.tile, r.vx, r.vy, r.vw, r.vh, r.region, r.coarse,
                                                  r.unseen));
}

// How far past the sharp ellipse the pixel lies, in the ellipse's own measure (1 = on its edge); TileRate's dx, dy from the pixel itself
inline float EllipseDistance(const Region& r, float x, float y) {
    const auto& g = r.region;
    if (!g.ok || !(g.ax > 0) || !(g.ay > 0) || !(r.vw > 0) || !(r.vh > 0)) return 0.0f;
    const float cx = r.vx + r.vw * g.cx, cy = r.vy + r.vh * g.cy;
    const float dx = std::abs(x - cx) / (r.vw * .5f) / (2.0f * g.ax), dy = std::abs(y - cy) / (r.vh * .5f) / (2.0f * g.ay);
    return std::sqrt(dx * dx + dy * dy);
}

inline float Smoothstep(float a, float b, float v) {
    const float t = std::min(1.0f, std::max(0.0f, (v - a) / (b - a)));
    return t * t * (3.0f - 2.0f * t);
}

// The blur's radius at pixel (x, y), pixels: 0 where the tile was shaded whole
inline float Radius(const Region& r, float x, float y, float strength) {
    const int b = BlockAt(r, x, y);
    if (b <= 1 || !(strength > 0)) return 0.0f;
    return std::min(1.0f, strength) * 1.5f * (float)b * Smoothstep(1.0f, 1.3f, EllipseDistance(r, x, y));
}

// The 9 taps' weights: the distance blur's (D2R_DepthFog.fx kDistBlurW)
constexpr float kWeights[5] = {1.0f, 0.88f, 0.61f, 0.32f, 0.14f};

}  // namespace foveablur
