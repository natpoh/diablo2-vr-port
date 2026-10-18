// durability_figure.h - the game's item-condition figure (the knight with a sword, bracers and boots that go red as the items wear), found and moved.
//
// What it is. The game's layout data\global\ui\Layouts\HUDWarningsHD.json (read with tools\casc_extract, 2026-10-17): a panel "HUDWarnings" (type HUDWarningsPanel),
// anchored at the TOP RIGHT of the interface area (anchor 1, 0) with rect -40, 12; its children are the battery and automap info texts (BatteryInfoText,
// InfoText, TerrorZoneInfoText), the HostileWarning picture, the QuantityWarning (low arrows / potions) and the one this is about, "DurabilityWarning" of type
// DurabilityWidget (requiredDamageLevel medium: shown from 50 % worn, the colours by the worst item's damage). A native widget: no picture file, no size in
// the layout; its rect is only known from the widget service (gamestate PollDurability).
//
// Why it ends in the sky. The game draws the interface once, in the picture's area (the picture scaled by the Safe Screen Percent, in the middle), and D2R VR
// leaves what it does not cut out (the toolbar, the map) where the game puts it. The top right corner of that area is the top right of the eye's picture, up
// and to the right of the middle by more than the headset's view reaches in a square picture: the figure hangs at the edge of the view, half behind it.
//
// What this does. The effect (reshade/D2R_DepthFog.fx, GameLabels) draws the interface layer back; for the figure it draws the box of its pixels again where
// [hud] durability_x / durability_y put them, [hud] durability_scale sized about the middle of the box - or not at all, [hud] durability_hide - and leaves its
// old place empty. First person (F4) only; all four at their defaults: nothing is done and the picture is the game's.
//
// This header is the part a test can hold: the box (from the widget's rect, or a calibrated one when the widget cannot be read) and, as C++, what the effect
// does with a pixel (LayerPixel mirrors GameLabels' figure branch).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace durab {

// ---- what the player asked for -----------------------------------------------------------------------------------------------------------------
struct Look {
    bool hide = false;          // [hud] durability_hide
    float x = 0.0f, y = 0.0f;   // [hud] durability_x / durability_y, uv of the picture (+x right, +y DOWN; the ini's y is + up, percent)
    float scale = 1.0f;         // [hud] durability_scale, 1 = the game's
};
inline bool Moved(const Look& l) { return l.hide || std::fabs(l.x) > 0.0005f || std::fabs(l.y) > 0.0005f || std::fabs(l.scale - 1.0f) > 0.005f; }

// ---- where the figure is -------------------------------------------------------------------------------------------------------------------------
// The interface area, as the plate's box and the allies' corner have it (vrcam PlateBoxUv, AlliesBoxUv): the picture scaled by the Safe Screen Percent
// (s * 2160 / the picture's height), in the middle; s the game's interface scale, layout units to pixels. Its top right corner is the panel's anchor.
struct Area {
    float right = 0.0f, top = 0.0f;   // pixels of the picture: the area's top right corner
};
inline bool AreaOf(float s, uint32_t sw, uint32_t sh, Area* a) {
    if (!sw || !sh) return false;
    if (!(s > 0.05f && s < 20.0f)) s = static_cast<float>(sh) / 2160.0f;
    const float safe = std::clamp(s * 2160.0f / static_cast<float>(sh), 0.3f, 1.0f);
    a->right = static_cast<float>(sw) - 0.5f * static_cast<float>(sw) * (1.0f - safe);
    a->top = 0.5f * static_cast<float>(sh) * (1.0f - safe);
    return true;
}

// A shot of the figure on a 1536 x 1536 picture at the interface scale 0.569 (Safe Screen Percent 80; Serg's test-57, 2026-10-17): it filled x 1228 - 1350,
// y 203 - 355, the area's corner at 1382, 154. In the layout's units off that corner: 271 - 57 left of it, 87 - 354 down. The box that holds it when the
// widget cannot be read: those, a margin of 10 units each way.
constexpr float kFallbackUnits[4] = {-281.0f, 77.0f, -47.0f, 364.0f};   // x0, y0, x1, y1 off the area's top right corner
constexpr float kMarginUnits = 8.0f;                                    // round a rect the widget gives (the figure's shadow, an edge)

// The figure's box in uv of a sw x sh picture. panel / widget: the rects the service gave (x, y, width, height; layout units, local to the parent - the panel's
// off its anchor, the widget's off the panel's place) when `widgetRead`. A widget rect that is not a figure's - empty, or nowhere near the corner where
// the calibration saw it, or not its size - is not used: the calibrated box is (`fromWidget` false). An empty widget (width or height 0: nothing worn out, the
// widget not shown) when it WAS read is no box at all: false.
inline bool BoxUv(bool widgetRead, const int32_t panel[4], const int32_t widget[4], float s, uint32_t sw, uint32_t sh, float box[4], bool* fromWidget) {
    Area a;
    if (!AreaOf(s, sw, sh, &a)) return false;
    if (!(s > 0.05f && s < 20.0f)) s = static_cast<float>(sh) / 2160.0f;
    float u0 = kFallbackUnits[0], v0 = kFallbackUnits[1], u1 = kFallbackUnits[2], v1 = kFallbackUnits[3];   // off the corner, layout units
    bool used = false;
    if (widgetRead) {
        if (widget[2] <= 0 || widget[3] <= 0) return false;
        const float wx0 = static_cast<float>(panel[0] + widget[0]), wy0 = static_cast<float>(panel[1] + widget[1]);
        const float wx1 = wx0 + static_cast<float>(widget[2]), wy1 = wy0 + static_cast<float>(widget[3]);
        const float fcx = 0.5f * (kFallbackUnits[0] + kFallbackUnits[2]), fcy = 0.5f * (kFallbackUnits[1] + kFallbackUnits[3]);
        const float fw = kFallbackUnits[2] - kFallbackUnits[0] - 2.0f * kMarginUnits, fh = kFallbackUnits[3] - kFallbackUnits[1] - 2.0f * kMarginUnits;
        const float cx = 0.5f * (wx0 + wx1), cy = 0.5f * (wy0 + wy1), w = wx1 - wx0, h = wy1 - wy0;
        const bool closeBy = std::fabs(cx - fcx) <= 200.0f && std::fabs(cy - fcy) <= 200.0f;
        const bool sizeOk = w >= 0.4f * fw && w <= 2.5f * fw && h >= 0.4f * fh && h <= 2.5f * fh;
        if (closeBy && sizeOk) {
            u0 = wx0 - kMarginUnits; v0 = wy0 - kMarginUnits; u1 = wx1 + kMarginUnits; v1 = wy1 + kMarginUnits;
            used = true;
        }
    }
    if (fromWidget) *fromWidget = used;
    const float x0 = a.right + u0 * s, y0 = a.top + v0 * s, x1 = a.right + u1 * s, y1 = a.top + v1 * s;
    box[0] = std::clamp(x0 / static_cast<float>(sw), 0.0f, 1.0f);
    box[1] = std::clamp(y0 / static_cast<float>(sh), 0.0f, 1.0f);
    box[2] = std::clamp(x1 / static_cast<float>(sw), 0.0f, 1.0f);
    box[3] = std::clamp(y1 / static_cast<float>(sh), 0.0f, 1.0f);
    return std::isfinite(box[0] + box[1] + box[2] + box[3]) && box[2] > box[0] && box[3] > box[1];
}

// ---- the effect's figure branch, as C++ ---------------------------------------------------------------------------------------------------------
// Which pixel of the interface layer an output pixel shows, for the part of GameLabels that concerns the figure. (su, sv): the label plane's coordinates of the
// output pixel (GameLabels' src - the layer's shift for this eye already taken off); box x0 y0 x1 y1 in uv, empty (x1 <= x0) = no figure.
enum class Kind { Layer, Empty, Other };   // Other: not the figure's business - the rest of GameLabels goes on
struct Pick {
    Kind kind;
    float u, v;
};
inline bool InBox(float u, float v, const float b[4]) { return u >= b[0] && v >= b[1] && u < b[2] && v < b[3]; }
// before the rest: the figure where it was moved to (the moved copy lies over whatever else is there)
inline Pick MovedFigure(float su, float sv, const Look& look, const float box[4]) {
    if (!(box[2] > box[0]) || !Moved(look)) return {Kind::Other, 0.0f, 0.0f};
    const float cu = 0.5f * (box[0] + box[2]), cv = 0.5f * (box[1] + box[3]);
    const float k = look.scale > 1e-3f ? look.scale : 1.0f;
    const float qu = cu + ((su - look.x) - cu) / k, qv = cv + ((sv - look.y) - cv) / k;
    if (!look.hide && InBox(qu, qv, box)) return {Kind::Layer, qu, qv};
    return {Kind::Other, 0.0f, 0.0f};
}
// after the early returns: the figure's old place, left empty
inline bool OldPlaceEmpty(float su, float sv, const Look& look, const float box[4]) {
    return box[2] > box[0] && Moved(look) && InBox(su, sv, box);
}

}  // namespace durab
