// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed:
//   7c642e33d2c8c04043c6f2ac47e7169b54e5693c "Apply foveated rendering to the visible shading pass and add strength"
//       (steamvr/foveation_math.h FixedRate, FoveationStrengthPreset; steamvr/foveation.cpp - which draws get the rate image: opaque,
//       depth-tested, single-sample draws into a full-size colour + depth pair, the eye's own optical centre) - kRate*, FixedRate,
//       StrengthPreset, the rule in Eligible below;
//   63f76f4 "Add eye-tracked foveated rendering for Steam Frame" .. 7c642e3 (steamvr/foveation_math.h GazeGrid, GazeCell,
//       GazeQuantizationMargin; steamvr/gaze.h Fresh, Stability) - the gaze grid, its margin, the freshness and jump rules below.
//   (Published before Unleashed moved to the MPL on 2026-10-28: also available under its MIT notice. Kept MPL here, as the other ports.)
// Taken into D2R VR Authentic behind a switch of its own ([render] foveated, off = the picture as before). What is ours: the region measured
// in the part of the picture the headset shows (Region, RegionFor - our eyes may be drawn with a wider frustum than the runtime's own,
// vr/eye_fov.h), the part never shown at the coarsest rate (unseen), and the mask built whole (BuildMask). The rest of D2R VR Authentic is
// MIT; this file stays MPL 2.0.
//
// Fixed foveated rendering with D3D12 variable rate shading (tier 2, a shading-rate image): the game's opaque world draws shade one pixel
// in 2x2 (or 4x4) outside an ellipse round the lens centre of each eye; inside it every pixel is shaded. Pure: tested in
// cleanroom/tests/unleashed_foveation_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace unleashed::foveation {

// D3D12_SHADING_RATE values (d3d12.h): 1x1, 2x2, 4x4. 4x4 needs AdditionalShadingRatesSupported.
constexpr uint8_t kRate1x1 = 0, kRate2x2 = 5, kRate4x4 = 10;

// ---- Bear's rule (7c642e3), as he wrote it ----
// Keep every tile touching the protected ellipse at 1x1. Coordinates include viewport offsets and asymmetric eye frusta; areas outside the
// viewport stay full quality. cx, cy: the centre in fractions of the viewport; radius: the ellipse's half-axes in halves of the viewport's
// width and height.
inline uint8_t FixedRate(uint32_t x, uint32_t y, uint32_t tile, float vx, float vy, float vw, float vh, float cx, float cy, float radius,
                         uint8_t coarse = kRate2x2) {
    if (!tile || vw <= 0 || vh <= 0 || !std::isfinite(radius)) return 0;
    const float left = float(x * tile), top = float(y * tile);
    if (left >= vx + vw || top >= vy + vh || left + tile <= vx || top + tile <= vy) return 0;
    const float centerX = vx + vw * cx, centerY = vy + vh * cy;
    const float dx = std::max({left - centerX, centerX - (left + tile), 0.f}) / (vw * .5f);
    const float dy = std::max({top - centerY, centerY - (top + tile), 0.f}) / (vh * .5f);
    return dx * dx + dy * dy <= radius * radius ? 0 : coarse;
}

// Bear's strength levels (VR Settings > Performance > Edge detail reduction): Low, Medium, High, Maximum - the protected radius and the
// periphery's rate (2: 2x2, 4: 4x4, which falls back to 2x2 without additional shading rates). Medium is his default.
// gazeRadius: the same with the eyes' gaze followed (eye tracking), smaller - the centre is where the eye looks.
struct Preset {
    float radius;
    int rate;
    float gazeRadius;
};
constexpr int kStrengthCount = 4, kDefaultStrength = 1;
inline Preset StrengthPreset(int level) {
    constexpr Preset presets[kStrengthCount] = {{.70f, 2, .45f}, {.55f, 2, .35f}, {.55f, 4, .35f}, {.35f, 4, .30f}};
    return presets[level >= 0 && level < kStrengthCount ? level : kDefaultStrength];
}

// Which pipelines' draws take the rate image (Bear's foveation.cpp): a pixel shader, depth tested (D2R lays depth down in one opaque pass,
// then shades the visible colour in a second, depth-tested pass that does not write depth - both count), no blending (transparency,
// effects, the interface stay whole), one sample. Zero = eligible, else the first reason it is not.
enum Reject : uint8_t { kUnknown = 1, kNoPixel = 2, kNoDepth = 4, kBlended = 8, kSamples = 16 };
inline uint8_t Classify(bool known, bool hasPixel, bool depthTest, bool opaque, bool singleSample) {
    if (!known) return kUnknown;
    return uint8_t((hasPixel ? 0 : kNoPixel) | (depthTest ? 0 : kNoDepth) | (opaque ? 0 : kBlended) | (singleSample ? 0 : kSamples));
}

// The draw's targets and viewport (Bear's Apply / BindTargets): a 2D colour target and a depth of its size, each at least 45% of the eye's
// picture and of its aspect (within .04), one sample; the viewport the same size class and inside the target.
struct Bound {
    uint32_t w = 0, h = 0;          // the colour target
    uint32_t dw = 0, dh = 0;        // the depth-stencil (0: none)
    bool texture2d = false, single = false;
    float vx = 0, vy = 0, vw = 0, vh = 0;   // the first viewport
};
inline bool Eligible(const Bound& b, uint32_t screenW, uint32_t screenH) {
    if (!screenW || !screenH || !b.w || !b.h || !b.texture2d || !b.single || b.dw != b.w || b.dh != b.h) return false;
    const float aspect = float(screenW) / float(screenH);
    if (b.w < screenW * .45f || b.h < screenH * .45f || std::abs(float(b.w) / float(b.h) - aspect) >= .04f) return false;
    if (!(b.vw >= screenW * .45f) || !(b.vh >= screenH * .45f)) return false;
    if (b.vx < 0 || b.vy < 0 || b.vx + b.vw > b.w + 1 || b.vy + b.vh > b.h + 1) return false;
    return std::abs(b.vw / b.vh - aspect) < .04f;
}

// ---- Ours: the region in the part of the picture the headset shows ----
// Tangents of the half-angles (left and down negative), as vr/eye_fov.h.
struct Tan {
    float l, r, u, d;
};
// The protected ellipse and the shown window, in fractions of the viewport (x from the left, y from the TOP).
struct Region {
    float cx = .5f, cy = .5f;       // the lens centre: where the eye looks straight ahead
    float ax = .275f, ay = .275f;   // the ellipse's half-axes
    float x0 = 0, x1 = 1, y0 = 0, y1 = 1;   // what the headset shows of the picture; outside it: never seen
    bool ok = false;
};
// An eye drawn with frustum `drawn` whose own straight-ahead lies at tangents (fx, fy) in drawn's frame (0, 0 unless the displays are
// canted and the eye is drawn turned), the headset showing `seen` of it (the runtime's own view in the same frame; seenOk false: all of
// it). radius as Bear's - in halves of the SHOWN part's width and height: where the picture is drawn wider than the runtime's view
// ([openxr] symmetric_fov / runtime_fov), the protected angle stays what it is in Unleashed, whose picture is the runtime's view.
inline Region RegionFor(const Tan& drawn, float fx, float fy, const Tan& seen, bool seenOk, float radius) {
    Region g;
    const float w = drawn.r - drawn.l, h = drawn.u - drawn.d;
    if (!(w > 0) || !(h > 0) || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(radius)) return g;
    auto c01 = [](float v) { return std::min(1.0f, std::max(0.0f, v)); };
    g.cx = c01((fx - drawn.l) / w);
    g.cy = c01((drawn.u - fy) / h);
    if (seenOk && seen.r > seen.l && seen.u > seen.d) {
        g.x0 = c01((seen.l - drawn.l) / w);
        g.x1 = c01((seen.r - drawn.l) / w);
        g.y0 = c01((drawn.u - seen.u) / h);
        g.y1 = c01((drawn.u - seen.d) / h);
        if (!(g.x1 > g.x0) || !(g.y1 > g.y0)) { g.x0 = g.y0 = 0; g.x1 = g.y1 = 1; }
    }
    g.ax = .5f * radius * (g.x1 - g.x0);
    g.ay = .5f * radius * (g.y1 - g.y0);
    g.ok = true;
    return g;
}

// A tile's rate: 0 (1x1) outside the viewport and where it touches the ellipse; `unseen` (0: as `coarse`) where it lies wholly outside the
// shown window; else `coarse`. With the whole picture shown and ax = ay = radius / 2 this is FixedRate exactly.
inline uint8_t TileRate(uint32_t x, uint32_t y, uint32_t tile, float vx, float vy, float vw, float vh, const Region& g, uint8_t coarse,
                        uint8_t unseen = 0) {
    if (!tile || vw <= 0 || vh <= 0 || !g.ok || !(g.ax > 0) || !(g.ay > 0)) return 0;
    const float left = float(x * tile), top = float(y * tile), right = left + tile, bottom = top + tile;
    if (left >= vx + vw || top >= vy + vh || right <= vx || bottom <= vy) return 0;
    const float centerX = vx + vw * g.cx, centerY = vy + vh * g.cy;
    // in halves of the viewport, as Bear's - and his very expression when the half-axes are equal there (no rounding apart)
    const float dx = std::max({left - centerX, centerX - right, 0.f}) / (vw * .5f);
    const float dy = std::max({top - centerY, centerY - bottom, 0.f}) / (vh * .5f);
    const float rx = 2.0f * g.ax, ry = 2.0f * g.ay;
    if (rx == ry ? dx * dx + dy * dy <= rx * rx : (dx / rx) * (dx / rx) + (dy / ry) * (dy / ry) <= 1.0f) return 0;
    if (unseen && (right <= vx + vw * g.x0 || left >= vx + vw * g.x1 || bottom <= vy + vh * g.y0 || top >= vy + vh * g.y1)) return unseen;
    return coarse;
}

// The whole image for a target of w x h pixels (tiles of `tile`, rounded up), row by row; how many tiles are coarse (`unseen` among them).
struct MaskCount {
    uint32_t coarse = 0, unseen = 0;
};
inline uint32_t Tiles(uint32_t pixels, uint32_t tile) { return tile ? (pixels + tile - 1) / tile : 0; }
inline MaskCount BuildMask(uint32_t w, uint32_t h, uint32_t tile, float vx, float vy, float vw, float vh, const Region& g, uint8_t coarse,
                           uint8_t unseen, std::vector<uint8_t>& out) {
    const uint32_t tw = Tiles(w, tile), th = Tiles(h, tile);
    out.assign(size_t(tw) * th, 0);
    MaskCount n;
    for (uint32_t y = 0; y < th; ++y)
        for (uint32_t x = 0; x < tw; ++x) {
            const uint8_t r = TileRate(x, y, tile, vx, vy, vw, vh, g, coarse, unseen);
            out[size_t(y) * tw + x] = r;
            if (r) ++n.coarse;
            if (r && unseen && r == unseen && unseen != coarse) ++n.unseen;
        }
    return n;
}

// ---- Bear's gaze rules (63f76f4 .. 7c642e3) ----
// The masks for a followed gaze are made once, a bank of 9 x 9 centres over the picture, never rewritten while the eyes move; the
// centre used is the nearest. That is at most half a cell off on each axis: the protected ellipse grows by that diagonal, in halves of
// the viewport (UV distance * 2).
constexpr int kGazeGrid = 9, kGazeMasks = kGazeGrid * kGazeGrid;
constexpr float kGazeMargin = 1.414213563f / (kGazeGrid - 1);
inline int GazeCell(float u, float v) {
    const auto cell = [](float t) { return int(std::round(std::clamp(t, 0.f, 1.f) * (kGazeGrid - 1))); };
    return cell(v) * kGazeGrid + cell(u);
}
inline float GazeGridCoordinate(int i) { return float(i) / (kGazeGrid - 1); }
// A gaze sample is used only when the runtime timed it and it is at most 100 ms older (20 ms newer) than the frame's display time.
inline bool GazeFresh(int64_t display, int64_t sample) {
    return display > 0 && sample > 0 && sample >= display - 100'000'000 && sample <= display + 20'000'000;
}
// Every pixel shaded on the pass after the gaze came back or jumped more than 0.12 of the picture (a saccade): the new fixation is never
// shaded coarsely for a frame; then followed at once (no smoothing: it would lag behind the eye).
class GazeJump {
    float pu = .5f, pv = .5f;
    bool have = false;
public:
    void Reset() { have = false; }
    bool Steady(float u, float v) {   // false: whole this pass
        const bool steady = have && std::hypot(u - pu, v - pv) <= .12f;
        pu = u; pv = v; have = true;
        return steady;
    }
};

// ---- Ours: the gaze in the picture drawn ----
// Where the gaze direction (tangents gx, gy in the frame the eye is drawn in) falls in the picture drawn with `drawn`: u from the left,
// v from the top. False: outside the picture (the eye looks beyond what is drawn) or nothing sensible in.
inline bool GazeUV(const Tan& drawn, float gx, float gy, float* u, float* v) {
    const float w = drawn.r - drawn.l, h = drawn.u - drawn.d;
    if (!(w > 0) || !(h > 0) || !std::isfinite(gx) || !std::isfinite(gy)) return false;
    *u = (gx - drawn.l) / w;
    *v = (drawn.u - gy) / h;
    return *u >= 0 && *u <= 1 && *v >= 0 && *v <= 1;
}
// The region of bank cell `cell` for an eye whose fixed region is `fixed` (its shown window and unseen part kept): centred on the cell,
// the gaze radius in halves of the shown part plus Bear's margin.
inline Region GazeRegion(const Region& fixed, int cell, float gazeRadius) {
    Region g = fixed;
    if (!fixed.ok || cell < 0 || cell >= kGazeMasks || !std::isfinite(gazeRadius)) { g.ok = false; return g; }
    g.cx = GazeGridCoordinate(cell % kGazeGrid);
    g.cy = GazeGridCoordinate(cell / kGazeGrid);
    g.ax = .5f * (gazeRadius * (fixed.x1 - fixed.x0) + kGazeMargin);
    g.ay = .5f * (gazeRadius * (fixed.y1 - fixed.y0) + kGazeMargin);
    return g;
}

}  // namespace unleashed::foveation
