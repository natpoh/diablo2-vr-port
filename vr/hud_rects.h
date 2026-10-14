#pragma once
// hud_rects.h - where the toolbar's strip is in the game's interface layer, the box vr/hud.cpp takes out of the picture ([hud] hide = 1, "The toolbar") and copies for the
// toolbar in the room. Pure: no game, no Windows (cleanroom/tests/hud_rects_test.cpp checks it).
//
// The game lays its interface out in units of a 3840 x 2160 screen, scaled to the layer's height and to Safe Screen Percent (s, 0.3 - 1): k = H / 2160 * s pixels a unit, the
// interface's area standing in the middle of the layer, so its bottom edge is at H (0.5 + 0.5 s). The toolbar (HUDPanelHD.json: the bar 412 units tall, its wings and the statues'
// horns above it) hangs from that bottom edge across the whole width: its strip is 0.215 H s above the bottom (464 units) to 0.004 H under it, the whole width of the layer.
// Measured on a real dump of the layer (1920 x 1920, s = 0.8) the art reaches 409 units up (the devil's horn) - 55 units under the strip's top.
//
// Two sliders move the strip's edges, for a setup where the art goes further than that (the user's 2048 x 2048 picture at 80 %: the horns, the statues' hands and the globes' tops stayed
// in the picture, 10 - 55 px over the cut): `upPct` more above the strip and `downPct` more below it, both in percent of the layer's height (0 = as it always was).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hudrects {

struct Box { int32_t left = 0, top = 0, right = 0, bottom = 0; };

constexpr float kStripUp = 0.215f;     // x H x s above the interface area's bottom edge
constexpr float kStripDown = 0.004f;   // x H under it
constexpr float kUpMin = -5.0f, kUpMax = 15.0f, kDownMin = -2.0f, kDownMax = 5.0f;   // the sliders' ranges, percent of the layer's height
constexpr float kLeastHeight = 0.01f;  // the strip is never thinner than this, over the layer's height (the sliders can only shrink it so far)

inline float Pct(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : 0.0f; }
inline float UpPct(float v) { return Pct(v, kUpMin, kUpMax); }
inline float DownPct(float v) { return Pct(v, kDownMin, kDownMax); }

inline Box Clamped(float l, float t, float r, float b, uint32_t w, uint32_t h) {
    return Box{std::max(0, (int32_t)l), std::max(0, (int32_t)t), std::min((int32_t)w, (int32_t)r), std::min((int32_t)h, (int32_t)b)};
}

// The toolbar's strip in a layer of w x h with the interface area at `safe` (Safe Screen Percent over 100). upPct / downPct: see above.
inline Box Toolbar(uint32_t w, uint32_t h, float safe, float upPct = 0.0f, float downPct = 0.0f) {
    const float s = std::isfinite(safe) ? std::clamp(safe, 0.3f, 1.0f) : 1.0f, H = (float)h;
    const float bottom = H * (0.5f + 0.5f * s);
    const float top = bottom - kStripUp * H * s - UpPct(upPct) * 0.01f * H;
    const float end = std::max(bottom + kStripDown * H + DownPct(downPct) * 0.01f * H, top + kLeastHeight * H);
    return Clamped(0.0f, top, (float)w, end, w, h);
}

}  // namespace hudrects
