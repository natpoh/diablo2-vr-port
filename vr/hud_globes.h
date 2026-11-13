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

// The interface's scale (2026-11-13, Serg on 0.215.7: "the game's toolbar globes are offset"): the place above follows the layer's height times the file's
// Safe Screen Percent. The game's own interface scale (pixels a layout unit, hud::SetGameInterfaceScale - what the corner map's box is found by, which
// stops growing at about 640 px on large layers: vr/minimap_box.h) is the truth when it is known: hud.cpp gives Compute the area fraction it makes of this
// layer (minimapbox::SafeFromScale), the file's only when the game's is not known or fits no area of this layer.

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
    bool onLayer = false;      // both middles on the layer: the squares fit it (made smaller round the same middles when !ok, the disc with them) - cut
};

// [hud] wrist_globe_in (+-5) / _dy (+-3) / _scale: a nudge on top of the derived place, a fallback only (0 / 0 / 1 = none) - `in` moves the two middles toward the
// toolbar's middle (- apart), mirrored, by that much of the disc's radius; dy moves both down (y down: the layer's own way); scale times the disc's radius.
// (0.215.8's wrist_globe_dx moved both the same way: Serg - "it must move the pictures in opposite directions".)
struct Nudge { float in = 0.0f, dy = 0.0f, scale = 1.0f; };

// The game's two HUD layouts (2026-11-13, Serg on 0.215.8 - the life globe wanted a globe's radius toward the middle, the mana globe the same the other
// way): the numbers above are the keyboard-and-mouse one (data\global\ui\Layouts\HUDPanelHD.json); in controller layout - what VR plays in - the game
// takes data\global\ui\Layouts\Controller\HUDPanelHD.json: the panel at x -1473, y -417 (its width the base's, 2952), HealthBall at x 141, ManaBall at
// 2453, y 68, 348 x 350 - the middles 1158 and 1154 units out, the same measured 6 units up as the first, at the interface's own scale: the panel runs
// past the safe area on a narrow picture and is NOT drawn smaller (0.215.9 shrank it to the safe width - Serg pulled the globes apart to the slider's end
// and made the cut 1.46 x bigger: on his 2048 square at 80 % the globes are at x 146 / 1899, radius 121 px, where these numbers put them; in 0.215.8 he
// had already parked the mana globe at 1895 with the old one-way slider).
struct Layout { float lifeX, manaX, belowBottom, panelW; bool fitWidth; };
constexpr Layout kKeyboardLayout{-986.0f, 988.0f, -178.0f, 2952.0f, false};
constexpr Layout kControllerLayout{-1473.0f + 141.0f + 174.0f, -1473.0f + 2453.0f + 174.0f, -417.0f + 68.0f + 175.0f - 6.0f, 2952.0f, false};

// `safe`: Safe Screen Percent over 100 (the game's interface size, 0.3 - 1). Not ok: a layer too small, or a square that would leave the layer
// (cut off at a side: the globe is not whole there, nothing is cut).
inline Boxes Compute(uint32_t w, uint32_t h, float safe, Nudge n = {}, const Layout& lay = kKeyboardLayout) {
    Boxes b;
    if (w < 320 || h < 320 || !std::isfinite(safe) || safe < 0.3f || safe > 1.0f) return b;
    float k = (float)h / kRefHeight * safe;
    if (lay.fitWidth && lay.panelW > 0.0f) k *= std::min(1.0f, (float)w * safe / (lay.panelW * k));   // (the controller panel shrunk to the safe area's width)
    const float bottom = (float)h * (0.5f + 0.5f * safe);
    const float sc = std::isfinite(n.scale) ? std::clamp(n.scale, 0.5f, 2.0f) : 1.0f;
    const float in = std::isfinite(n.in) ? std::clamp(n.in, -5.0f, 5.0f) : 0.0f, dy = std::isfinite(n.dy) ? std::clamp(n.dy, -3.0f, 3.0f) : 0.0f;
    b.cx[0] = 0.5f * (float)w + lay.lifeX * k + in * kCutR * k;   // (the life globe left of the middle: toward it is +x)
    b.cx[1] = 0.5f * (float)w + lay.manaX * k - in * kCutR * k;   // (the mana globe right of it: toward it is -x)
    b.cy = bottom + lay.belowBottom * k + dy * kCutR * k;
    b.radius = kCutR * k * sc;
    b.half = (int)std::ceil(b.radius * kMargin);
    b.fit = b.radius / (float)b.half;
    b.top = (int)std::lround(b.cy) - b.half;
    for (int i = 0; i < 2; ++i) b.left[i] = (int)std::lround(b.cx[i]) - b.half;
    const int side = 2 * b.half;
    bool inside = b.top >= 0 && b.top + side <= (int)h;
    for (int i = 0; i < 2; ++i) inside = inside && b.left[i] >= 0 && b.left[i] + side <= (int)w;
    b.ok = inside && b.half >= 8 && b.half <= 2048;
    // On the layer but not whole (0.215.11: the controller layout puts the globes near the picture's sides, and a bigger cut ([hud] wrist_globe_scale) ran
    // past them - nothing was cut, and with no picture the effect drew no medallion at all, its base gone too): the squares made as big as the layer lets
    // round the same middles, the disc cut that much smaller with them (its usual margin: it never reads past its own square).
    if (!b.ok && b.cy >= 0.0f && b.cy < (float)h && b.cx[0] >= 0.0f && b.cx[0] < (float)w && b.cx[1] >= 0.0f && b.cx[1] < (float)w && b.half <= 2048) {
        const float room = std::min({b.cx[0], (float)w - b.cx[0], b.cx[1], (float)w - b.cx[1], b.cy, (float)h - b.cy});
        const int half = std::min(b.half, (int)std::floor(room) - 1);
        if (half >= 8) {
            b.half = half;
            b.radius = (float)half / kMargin;
            b.fit = b.radius / (float)half;
            b.top = (int)std::lround(b.cy) - half;
            for (int i = 0; i < 2; ++i) b.left[i] = (int)std::lround(b.cx[i]) - half;
            const int sideNow = 2 * half;
            b.onLayer = b.top >= 0 && b.top + sideNow <= (int)h && b.left[0] >= 0 && b.left[0] + sideNow <= (int)w && b.left[1] >= 0 && b.left[1] + sideNow <= (int)w;
        }
    } else {
        b.onLayer = b.ok;
    }
    return b;
}

}  // namespace hudglobes
