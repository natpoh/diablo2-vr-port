#pragma once
// minimap_box.h - which of the game's scissor rectangles is its corner map, in an interface layer of any size, shape and interface size
// (vr/hud.cpp IsMapScissor, [hud] map=1: the map out of the picture, into the hand / the orb). Pure: no game, no Windows
// (cleanroom/tests/minimap_box_test.cpp).
//
// The game draws its corner map as one run of draws under a scissor rectangle that is the map's box (1920x1200 at 100 %: 0,53 - 640,452;
// at 1920x1440 ~640 wide; 1699x1274 at 80 % about 0.533 H s wide). The box hangs in a TOP CORNER of the interface area - the picture scaled
// by the interface size s in the middle (vr/durability_figure.h AreaOf, vrcam PlateBoxUv): its top a little under the area's top, its outer
// edge at the area's side.
//
// Up to 0.173 the box was looked for with s read from Settings.json ("Safe Screen Percent") once, at the plugin's start, against the
// texture's size: its top within 0.07 H of the area's top, and its outer edge within 0.07 H of the area's side, s in from the picture's
// edge. Three ways that misses the box at a window size other than the shipped square (a player: "once I change Stereo > Square window
// size, px, the map stops working"):
//   - s is the file's, not the game's: a file the game has not written yet, no key or a number out of 30 - 100 read as 1 (the whole
//     picture), and a size changed in the game's options or the picture's size changed while it runs - the area is not where it is
//     looked for; at 80 % the area's side is 0.1 W in and its top 0.1 H down, past the 0.07 H tolerance;
//   - the area's side: in the square it was measured on the box's outer edge is s in from the picture's edge, but the game's panels lay
//     out in a frame as wide as the PICTURE (docs/DEVELOPER_NOTES.md, shared/safe_screen.h: W / k = aspect * 2160 / s units) and which
//     of the two a picture of another shape puts the map at was never measured. A box at the picture's edge (2560x1440 at 80 %: the
//     area's side 256 px in, the tolerance 101) was not taken;
//   - the canvas: the area was worked out from the layer TEXTURE's size; drawn through a smaller viewport (a texture made at an earlier,
//     bigger size) the area is the viewport's, and was looked for elsewhere.
// So now: the area from the game's OWN interface scale k (pixels a layout unit: the area is 2160 k tall - vrcam asks the game,
// hud::SetGameInterfaceScale), the file's s only as a second guess; the canvas the viewport the interface is drawn through when there is
// one; and the box's outer edge anywhere from the picture's edge to the area's side. The box's size and its top stay as they were, as
// fractions of the canvas: nothing measured in pixels.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace minimapbox {

struct Box { int32_t left = 0, top = 0, right = 0, bottom = 0; };
// The picture the interface is laid out over, in the layer's pixels: the viewport it is drawn through, or the whole layer.
struct Canvas { int32_t x = 0, y = 0; uint32_t w = 0, h = 0; };

constexpr float kRefHeight = 2160.0f;      // the layout's units: a 2160-high screen at 100 %
constexpr float kSafeMin = 0.3f, kSafeMax = 1.0f;
constexpr float kTol = 0.07f;              // x the canvas's height: the box's top off the area's top, its outer edge past the area's side

// The interface area as a fraction of the canvas's height from the game's interface scale k: its 2160 units over the height. 0: not known
// (no scale, or one that makes no area of this canvas - the scale of another canvas).
inline float SafeFromScale(float k, uint32_t h) {
    if (!h || !std::isfinite(k) || k <= 0.05f || k >= 20.0f) return 0.0f;
    const float s = k * kRefHeight / (float)h;
    return s >= kSafeMin - 0.02f && s <= kSafeMax + 0.02f ? std::clamp(s, kSafeMin, kSafeMax) : 0.0f;
}

// The canvas: the largest viewport seen while the layer was bound when it lies inside the layer and is not a piece of it (at least half
// of it each way), else the layer.
inline Canvas CanvasOf(uint32_t layerW, uint32_t layerH, float vx, float vy, float vw, float vh) {
    Canvas c{0, 0, layerW, layerH};
    if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vw) || !std::isfinite(vh)) return c;
    if (vx < -0.5f || vy < -0.5f || vw < 0.5f * (float)layerW || vh < 0.5f * (float)layerH) return c;
    if (vx + vw > (float)layerW + 0.5f || vy + vh > (float)layerH + 0.5f) return c;
    return Canvas{(int32_t)std::lround(vx), (int32_t)std::lround(vy), (uint32_t)std::lround(vw), (uint32_t)std::lround(vh)};
}

// 0: not the map's box at the area fraction `safe`; 1: a box in the top left corner; 2: the top right one. `corner` ([hud] map_corner):
// 0 either, 1 the left one only, 2 the right one only.
inline int Corner(const Box& r, const Canvas& c, float safe, int corner) {
    if (c.w < 64 || c.h < 64 || !std::isfinite(safe) || safe <= 0.0f) return 0;
    const float s = std::clamp(safe, kSafeMin, kSafeMax), W = (float)c.w, H = (float)c.h;
    const float l = (float)(r.left - c.x), t = (float)(r.top - c.y), rr = (float)(r.right - c.x), b = (float)(r.bottom - c.y);
    const float rw = rr - l, rh = b - t;
    if (!(rw > W / 8.0f && rw < W / 2.0f && rh > H / 8.0f && rh < 0.6f * H)) return 0;
    const float side = 0.5f * W * (1.0f - s), top = 0.5f * H * (1.0f - s), tol = kTol * H;
    if (std::fabs(t - top) >= tol) return 0;
    const bool atLeft = l + rr < W && l > -tol && l < side + tol;
    const bool atRight = l + rr > W && rr < W + tol && rr > W - side - tol;
    if (atLeft && corner != 2) return 1;
    if (atRight && corner != 1) return 2;
    return 0;
}

// The map's box at either guess of the area: the game's own (SafeFromScale) first, the file's second. 0 / 1 / 2 as Corner.
inline int Find(const Box& r, const Canvas& c, float safeGame, float safeFile, int corner) {
    if (const int k = Corner(r, c, safeGame, corner)) return k;
    return Corner(r, c, safeFile, corner);
}

}  // namespace minimapbox
