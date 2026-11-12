// band_icons.h - [render] ui_band_corner_icons: two small icons over the top right corner of a menu's panel in F4 (vr/xr.cpp HangPanel), a square
// and a 16:9 rectangle - the laser's click on one switches [render] ui_band_16x9 right in the game and keeps it (the ini, as Settings writes it):
// the square the whole picture (0), the rectangle the 16:9 band while the inventory is open (2; 0.214: "always", 1, is gone - the toolbar hung
// mid-view in the world, Serg: "Yes, remove Always"). Serg (2026-11-12): "в углу вверху квадратик и прямоугольник", "в правом верхнем углу, над оверлеем там где он заканчивается". They hang
// OUTSIDE the game's picture (in the panel's plane, above its top edge), so they never cover a button of the game's, and the laser's click on them
// is never the game's mouse. Also the band's reach tally for the log (Reach). Pure C++ (cleanroom/tests/band_icons_test.cpp).
#pragma once

#include <cstdint>

namespace bandicons {

enum Icon : int { kNone = 0, kSquare = 1, kRect = 2 };

// One icon's side in metres for a panel `panelW` wide: 6 % of it, 3.5..9 cm (the inventory's own button beside the other corner: 9 %).
inline float Side(float panelW) {
    const float s = 0.06f * panelW;
    return s < 0.035f ? 0.035f : s > 0.09f ? 0.09f : s;
}
// The strip of both icons (square left, rectangle right), its middle in the panel's own frame (x right, y up, from the panel's middle):
// right edge on the panel's right edge, a fifth of a side above its top edge (the inventory's own button hangs BESIDE that corner, under its top edge).
struct Strip { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
inline Strip Place(float panelW, float panelH) {
    const float s = Side(panelW);
    return Strip{0.5f * panelW - s, 0.5f * panelH + 0.7f * s, 2.0f * s, s};
}
// Which icon a point of the strip's own frame (x right, y up, from its middle) is on.
inline Icon At(float lx, float ly, float w, float h) {
    if (!(w > 0.0f) || !(h > 0.0f) || lx < -0.5f * w || lx > 0.5f * w || ly < -0.5f * h || ly > 0.5f * h) return kNone;
    return lx < 0.0f ? kSquare : kRect;
}

// The band changes anything only on a picture taller than 16:9 (vr_resolution.h UiCanvas: the band is min(h, w*9/16) high) - on the main menu
// drawn 16:9 ([render] menu_wide) both icons would be the same picture.
inline bool Matters(uint32_t w, uint32_t h) { return w > 0 && h > w * 9u / 16u; }
// Shown: the key on, F4, the menu's panel in front (not on the hand), [render] vr_resolution_ui on (the band needs its hook), not the campfire
// (its boards force the band), and the picture's shape lets the band matter.
struct Facts {
    bool keyOn = false, f4 = false, inFront = false, vrResUi = false, campfire = false;
    uint32_t w = 0, h = 0;
};
inline bool Shown(const Facts& f) { return f.keyOn && f.f4 && f.inFront && f.vrResUi && !f.campfire && Matters(f.w, f.h); }

// The lit icon for [render] ui_band_16x9 `mode`.
inline Icon Lit(int mode) { return mode == 1 || mode == 2 ? kRect : kSquare; }
// The band mode the rectangle gives: 2, with the inventory open, whatever was last (0.214: 1, always, no longer offered; `last` kept for the callers).
inline int RectMode(int /*last*/) { return 2; }
// What a click on `icon` writes to ui_band_16x9, the mode being `mode` and the rectangle's last one `last`; -1: nothing (no icon, or the lit one).
inline int ClickMode(Icon icon, int mode, int last) {
    if (icon == kNone || icon == Lit(mode)) return -1;
    return icon == kSquare ? 0 : RectMode(last);
}

// The click's mode is in force at once; the ini, re-read twice a second, still says the old one until the click's own write lands - held over it
// until the ini says the same, or `holdMs` passed (the write failed: the ini's again).
struct Hold {
    int mode = -1;
    uint64_t at = 0;
    void Pick(int m, uint64_t nowMs) { mode = m; at = nowMs; }
    int Apply(int ini, uint64_t nowMs, uint64_t holdMs = 5000) {
        if (mode < 0) return ini;
        if (ini == mode || nowMs - at > holdMs) { mode = -1; return ini; }
        return mode;
    }
};

// -- the band's reach for the log ([render] ui_band_16x9 in force, a menu's panel in front, F4) ----------------------------------------------
// Serg (2026-11-12): does a player's laser reach everything in the 16:9 menu? A tally, told every 60 s (no line a click): the laser's presses on the
// band, on its very edge (the outer 1 %: a reach stopped there), on the picture below the band (nothing of the game's there), on nothing at all;
// and the farthest the laser pointed on the band each way, as shares of the band (0 left / top, 1 right / bottom).
// The band's share of the picture's height (UiCanvas: min(h, w*9/16) of h, at its top); 1 for a picture not taller than 16:9.
inline float BandShare(uint32_t w, uint32_t h) {
    if (!w || !h) return 1.0f;
    const uint32_t band = w * 9u / 16u;
    return band >= h ? 1.0f : (float)band / (float)h;
}
enum Press : int { kOnBand = 0, kEdge = 1, kBelow = 2, kNothing = 3 };
struct Reach {
    uint32_t n[4] = {};
    float left = 1.0f, right = 0.0f, top = 1.0f, bottom = 0.0f;   // the farthest pointed (none yet: left > right)
    // A press: `hit` it was on the picture, at u, v (shares of the picture from its top left); `share` = BandShare.
    static Press Classify(bool hit, float u, float v, float share, float edge = 0.01f) {
        if (!hit) return kNothing;
        if (!(share > 0.0f) || v >= share) return kBelow;
        const float bv = v / share;
        return u < edge || u > 1.0f - edge || bv < edge || bv > 1.0f - edge ? kEdge : kOnBand;
    }
    void AddPress(bool hit, float u, float v, float share) { ++n[Classify(hit, u, v, share)]; }
    // The laser on the picture this frame (not pressed): the band's farthest points.
    void AddPoint(float u, float v, float share) {
        if (!(share > 0.0f) || v < 0.0f || v >= share || u < 0.0f || u > 1.0f) return;
        const float bv = v / share;
        if (u < left) left = u;
        if (u > right) right = u;
        if (bv < top) top = bv;
        if (bv > bottom) bottom = bv;
    }
    bool Pointed() const { return right >= left; }
    uint32_t Presses() const { return n[0] + n[1] + n[2] + n[3]; }
    bool Any() const { return Presses() > 0 || Pointed(); }
    void Clear() { *this = Reach{}; }
};

}  // namespace bandicons
