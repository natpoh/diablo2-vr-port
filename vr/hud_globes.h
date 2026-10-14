#pragma once
// hud_globes.h - where the game's own life and mana globes are in its interface layer, so that vr/hud.cpp can cut each one out as a round picture
// ([hud] bar_place = Wrists with the look "the game's own globes", hand_vitals_style = 2) and the effect can hang it on the wrist as a medallion.
// Pure: no game, no Windows (the test, cleanroom/tests/hand_orbs_test.cpp, checks the geometry and the cut against synthetic layers).
//
// The numbers come from the game's own layout of the HUD (data\global\ui\Layouts\HUDPanelHD.json, read once from the archives for reference; only the
// numbers are here, nothing of the file): the panel is anchored at the bottom middle of the safe area, its rect from (-1454, -412), 2952 wide, and the two
// AttributeBallWidgets sit in it at x 294 (health) and x 2268 (mana), y 65, 348 x 350 - in the layout's units, which scale with the screen's height over
// 2160 and with Safe Screen Percent (hud.cpp: ToolbarRect's 0.215 H s is the same rule: 464 units tall). So at layer W x H with the safe fraction s:
//
//     k        = H / 2160 * s                    pixels per unit
//     bottom   = H * (0.5 + 0.5 s)               the safe area's bottom edge
//     centre x = W / 2 + (-1454 + 294 + 174) k   (health),  W / 2 + (-1454 + 2268 + 174) k  (mana)
//     centre y = bottom - 178 k                  (the widget's top is 347 k above the bottom; its picture, a sphere ~340 units across, ends a little
//                                                 above its rect's bottom: measured on a real dump of the layer at 1920 x 1920, s = 0.8, where the
//                                                 predicted top of the sphere matched to a pixel and the middle was within ~5 px)
//     radius   = 170 k                           the sphere's, from the same dump (the cut takes 160 k: inside the sphere, off the statues' hands)
//
// The pictures show the game's real level of liquid (the globes fill from the bottom as the hero's life and mana do), because they are the game's own.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hudglobes {

constexpr float kRefHeight = 2160.0f;
constexpr float kLifeX = -986.0f, kManaX = 988.0f;   // from the screen's middle, in layout units
constexpr float kBelowBottom = -178.0f;              // the middle's y from the safe area's bottom edge, in layout units (negative: above it)
constexpr float kSphereR = 170.0f, kCutR = 160.0f;   // the sphere's radius, and the radius of the cut disc
constexpr float kMargin = 1.06f;                     // the square cut round the disc: half its side is the disc's radius times this

struct Boxes {
    bool ok = false;
    float cx[2] = {};          // [0] life, [1] mana: the middle in layer pixels
    float cy = 0.0f;
    float radius = 0.0f;       // the disc's radius in pixels (kCutR k)
    int half = 0;              // half the side of each square, pixels
    int left[2] = {}, top = 0; // each square's top left corner, pixels (inside the layer when ok)
    float fit = 0.0f;          // the disc's radius over half the square (0 - 1): how the effect maps the picture onto the ball
};

// `safe`: Safe Screen Percent over 100 (the game's interface size, 0.3 - 1). Not ok: a layer too small, or a square that would leave the layer
// (cut off at a side: the globe is not whole there, nothing is cut).
inline Boxes Compute(uint32_t w, uint32_t h, float safe) {
    Boxes b;
    if (w < 320 || h < 320 || !std::isfinite(safe) || safe < 0.3f || safe > 1.0f) return b;
    const float k = (float)h / kRefHeight * safe;
    const float bottom = (float)h * (0.5f + 0.5f * safe);
    b.cx[0] = 0.5f * (float)w + kLifeX * k;
    b.cx[1] = 0.5f * (float)w + kManaX * k;
    b.cy = bottom + kBelowBottom * k;
    b.radius = kCutR * k;
    b.half = (int)std::ceil(b.radius * kMargin);
    b.fit = b.radius / (float)b.half;
    b.top = (int)std::lround(b.cy) - b.half;
    for (int i = 0; i < 2; ++i) b.left[i] = (int)std::lround(b.cx[i]) - b.half;
    const int side = 2 * b.half;
    bool inside = b.top >= 0 && b.top + side <= (int)h;
    for (int i = 0; i < 2; ++i) inside = inside && b.left[i] >= 0 && b.left[i] + side <= (int)w;
    b.ok = inside && b.half >= 8 && b.half <= 2048;
    return b;
}

}  // namespace hudglobes
