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
//
// The box's WIDTH follows the camera's projection (2026-10-26, 0.183): the game sizes its map's box and draws its tiles by the world
// camera's x scale against its y scale. Its own camera keeps them to the picture's shape; our VR first person ([openxr] runtime_fov=1,
// on since 0.181) draws one centred frustum from the headset's angles whatever the window's shape. In a square window that is the same
// (1.04); in a wide one (the author's: the game started flat in a maximized 3840x2136 window, the headset switched on in the game) the
// map's box came 0.66 of the picture wide - x 1258-3792, y 0-818, its tiles' iso diamonds ~3.5:1 instead of 2:1 - past the half of
// the picture a box may have, so the map stayed in the picture, at its top: high over the eyes in the headset. So the width's bounds
// are taken in the map's own shape: the box's width over Stretch, the projection's x scale over the one the picture's shape gives.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace minimapbox {

// How much wider than the game's own camera would have it the map is drawn: (P00 / P11) of the projection in force times the canvas's
// width over its height. 1 for a projection made for the picture's shape (the game's own, our flat and other views); 1 when not known
// (0 given), or a number that makes no sense.
inline float Stretch(float projX, float projY, uint32_t w, uint32_t h) {
    if (!(projX > 0.0f) || !(projY > 0.0f) || !w || !h || !std::isfinite(projX) || !std::isfinite(projY)) return 1.0f;
    const float s = projX / projY * (float)w / (float)h;
    return s >= 0.25f && s <= 4.0f ? s : 1.0f;
}

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
// 0 either, 1 the left one only, 2 the right one only. `stretch`: Stretch - the box's width bounds in the map's own shape (it never
// reaches past the picture).
inline int Corner(const Box& r, const Canvas& c, float safe, int corner, float stretch = 1.0f) {
    if (c.w < 64 || c.h < 64 || !std::isfinite(safe) || safe <= 0.0f) return 0;
    const float s = std::clamp(safe, kSafeMin, kSafeMax), W = (float)c.w, H = (float)c.h;
    const float l = (float)(r.left - c.x), t = (float)(r.top - c.y), rr = (float)(r.right - c.x), b = (float)(r.bottom - c.y);
    const float rw = rr - l, rh = b - t;
    const float f = std::isfinite(stretch) ? std::clamp(stretch, 0.25f, 4.0f) : 1.0f;
    if (!(rw > W / 8.0f * f && rw < std::min(W / 2.0f * f, W) && rh > H / 8.0f && rh < 0.6f * H)) return 0;
    const float side = 0.5f * W * (1.0f - s), top = 0.5f * H * (1.0f - s), tol = kTol * H;
    if (std::fabs(t - top) >= tol) return 0;
    const bool atLeft = l + rr < W && l > -tol && l < side + tol;
    const bool atRight = l + rr > W && rr < W + tol && rr > W - side - tol;
    if (atLeft && corner != 2) return 1;
    if (atRight && corner != 1) return 2;
    return 0;
}

// Why Corner says 0 for a box, for the log (hud.cpp TellNoMap; 2026-10-28, a player at 2764x2780 whose log could not tell the corner rule from the
// size): "no area" (no canvas or interface size), "width", "height", "top" (not at the area's top), "side" (in neither top corner), "corner" (in the
// other corner than [hud] map_corner takes); nullptr when Corner takes it. The same steps as Corner.
inline const char* WhyNot(const Box& r, const Canvas& c, float safe, int corner, float stretch = 1.0f) {
    if (c.w < 64 || c.h < 64 || !std::isfinite(safe) || safe <= 0.0f) return "no area";
    const float s = std::clamp(safe, kSafeMin, kSafeMax), W = (float)c.w, H = (float)c.h;
    const float l = (float)(r.left - c.x), t = (float)(r.top - c.y), rr = (float)(r.right - c.x), b = (float)(r.bottom - c.y);
    const float rw = rr - l, rh = b - t;
    const float f = std::isfinite(stretch) ? std::clamp(stretch, 0.25f, 4.0f) : 1.0f;
    if (!(rw > W / 8.0f * f && rw < std::min(W / 2.0f * f, W))) return "width";
    if (!(rh > H / 8.0f && rh < 0.6f * H)) return "height";
    const float side = 0.5f * W * (1.0f - s), top = 0.5f * H * (1.0f - s), tol = kTol * H;
    if (std::fabs(t - top) >= tol) return "top";
    const bool atLeft = l + rr < W && l > -tol && l < side + tol;
    const bool atRight = l + rr > W && rr < W + tol && rr > W - side - tol;
    if (!atLeft && !atRight) return "side";
    if ((atLeft && corner == 2) || (atRight && corner == 1)) return "corner";
    return nullptr;
}

// The map's box at either guess of the area: the game's own (SafeFromScale) first, the file's second. 0 / 1 / 2 as Corner.
inline int Find(const Box& r, const Canvas& c, float safeGame, float safeFile, int corner, float stretch = 1.0f) {
    if (const int k = Corner(r, c, safeGame, corner, stretch)) return k;
    return Corner(r, c, safeFile, corner, stretch);
}

}  // namespace minimapbox
