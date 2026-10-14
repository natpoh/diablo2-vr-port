#pragma once
// The skill panel's second look - TILES: where each tile, button and grid cell is on the window, and what a point on the window
// hits. The painter (vr/tiles_paint.cpp) and the hit test read the same numbers, so what is drawn is what is clicked. Pure: no
// Windows, no OpenXR, no game. The wheel (vr/wheel_geom.h) is the other look; both give the same cells (wheel::Cell), so the model,
// the cast and the game side behind them are one.
//
// The window is laid out like the game's own controller skill-rebind dialog: TWO ROWS OF SIX tiles - A X B Y RB RT, and the same six
// with the left trigger held (marked by an LT chip at the left end of the row) - the game's twelve controller slots, a button mark
// under each tile. The editor shows the skills the player can assign as a grid of tiles of the same make.
//
// The window is kW x kH reference pixels (the picture is painted at exactly that size); a point arrives as u, v in 0..1 (u right,
// v down) and is turned into pixels here.

#include <cmath>
#include <cstdint>

#include "wheel_geom.h"
#include "wheel_types.h"

namespace tiles {

using wheel::Cell;
using wheel::Page;
using PRect = wheel::Rect;   // (pixels here)

constexpr int kW = 1600, kH = 1100;
constexpr float kAspect = float(kW) / float(kH);    // the window's width over its height

// -- the two rows of tiles ------------------------------------------------------------------------------------------------------
constexpr float kTile = 176.0f;      // a tile is a square this big
constexpr float kGap = 14.0f;        // between tiles of a row
constexpr int kCols = 6, kRows = 2;
constexpr float kRowWidth = kCols * kTile + (kCols - 1) * kGap;
constexpr float kRowLeft = 0.5f * (kW - kRowWidth);
constexpr float kRowTop[kRows] = {168.0f, 500.0f};

// The slot at each place of the rows, as the game's dialog orders them: A X B Y RB RT, then the same with LT held. (Slot numbers are
// wheel_types.h's: 0 B, 1 X, 2 Y, 3 RB, 4..7 the same with LT, 8 A, 9 RT, 10 LT + A, 11 LT + RT.)
constexpr int kSlotAt[kRows][kCols] = {{8, 1, 0, 2, 3, 9}, {10, 5, 4, 6, 7, 11}};

struct Place { int row, col; };
inline Place PlaceOf(int slot) {
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            if (kSlotAt[r][c] == slot) return {r, c};
    return {0, 0};
}
inline PRect SlotRect(int slot) {
    const Place p = PlaceOf(slot);
    const float l = kRowLeft + p.col * (kTile + kGap);
    return {l, kRowTop[p.row], l + kTile, kRowTop[p.row] + kTile};
}
// What goes under a tile: the button's mark, the skill's name (two lines), the controller hint.
inline PRect GlyphRect(int slot) {
    const PRect r = SlotRect(slot);
    const float c = 0.5f * (r.l + r.r);
    return {c - 40.0f, r.b + 10.0f, c + 40.0f, r.b + 56.0f};
}
inline PRect NameRect(int slot) {
    const PRect r = SlotRect(slot);
    return {r.l - 6.0f, r.b + 58.0f, r.r + 6.0f, r.b + 112.0f};
}
inline PRect HintRect(int slot) {
    const PRect r = SlotRect(slot);
    return {r.l - 6.0f, r.b + 114.0f, r.r + 6.0f, r.b + 140.0f};
}
// The LT chip at the left end of the second row (the row is the left trigger's), and the swap arrows under it.
inline PRect LtChipRect() {
    const PRect r = SlotRect(kSlotAt[1][0]);
    return {r.l - 128.0f, 0.5f * (r.t + r.b) - 46.0f, r.l - 32.0f, 0.5f * (r.t + r.b) - 2.0f};
}
inline PRect LtArrowsRect() {
    const PRect c = LtChipRect();
    return {c.l + 22.0f, c.b + 10.0f, c.r - 22.0f, c.b + 54.0f};
}

// -- the window's bands --------------------------------------------------------------------------------------------------------------
constexpr PRect kTitlePlate{60.0f, 36.0f, 1540.0f, 108.0f};      // the recessed plate the title is cut in
constexpr PRect kPanel{40.0f, 124.0f, 1560.0f, 1066.0f};         // the near-black panel
constexpr PRect kRedLine{160.0f, 836.0f, 1440.0f, 886.0f};       // the state, in red small capitals, as the game prints under its dialog
constexpr PRect kHintLine{160.0f, 888.0f, 1440.0f, 926.0f};      // and what to do, dim

// -- the tab strip -------------------------------------------------------------------------------------------------------------------
// With [openxr] headset_settings on, the title plate of the window's top level holds two tabs - SKILLS and SETTINGS - in place of its
// title; the editor's pages (choose a slot, choose a skill) keep their titles, and the strip is not there.
constexpr int kTabCount = 2;
constexpr float kTabW = 400.0f, kTabGap = 28.0f;
inline PRect TabRect(int i) {
    const float l = 0.5f * (kW - (kTabCount * kTabW + (kTabCount - 1) * kTabGap)) + i * (kTabW + kTabGap);
    return {l, 44.0f, l + kTabW, 100.0f};
}
inline Cell TabCell(int i) { return i == 0 ? wheel::kTabSkills : wheel::kTabSettings; }

// -- the flip hints ------------------------------------------------------------------------------------------------------------------
// The window flips like pages (a stick's flick, a grip's tap: vr/wheel_flip.h): an arrow at its left and right edge, where there is a place back /
// on - clickable, one step - and the places as dots at its foot (the first, SKILLS, a little apart from the Settings tab's pages).
constexpr PRect kFlipZone[2] = {{46.0f, 470.0f, 106.0f, 690.0f}, {1494.0f, 470.0f, 1554.0f, 690.0f}};
inline PRect FlipRect(int i) { return kFlipZone[i]; }
inline Cell FlipCell(int i) { return i == 0 ? wheel::kFlipPrev : wheel::kFlipNext; }
constexpr float kDotY = 1043.0f, kDotStep = 30.0f, kDotApart = 16.0f;
inline float DotX(int place, int places) {
    const float width = (places - 1) * kDotStep + kDotApart;
    return 0.5f * kW - 0.5f * width + place * kDotStep + (place > 0 ? kDotApart : 0.0f);
}

// -- the Settings tab's sheet ---------------------------------------------------------------------------------------------------------
// The settings pages are laid out on a sheet of kSheetW x kSheetH reference pixels (vr/hset_model.h: its kW, kH - checked where both are
// known); here that sheet is scaled by kSheetScale and put on the window so that its page tabs (sheet y 84) stand just under the title plate
// and its buttons end above the panel's foot. A point of the window is turned into the sheet's u, v (0..1 over the sheet; outside it when the
// point is off the sheet).
constexpr int kSheetW = 1024, kSheetH = 720;
constexpr float kSheetScale = 1.4f;
constexpr float kSheetX = 0.5f * (kW - kSheetW * kSheetScale);
constexpr float kSheetY = 150.0f - 84.0f * kSheetScale;
inline void SheetPoint(float u, float v, float* su, float* sv) {
    *su = (u * kW - kSheetX) / kSheetScale / float(kSheetW);
    *sv = (v * kH - kSheetY) / kSheetScale / float(kSheetH);
}

// -- the editor's grid ----------------------------------------------------------------------------------------------------------------
constexpr int kGridCols = 8, kGridRows = 3, kGridCount = kGridCols * kGridRows;
constexpr float kCellTile = 160.0f, kCellGap = 16.0f, kCellH = 210.0f, kCellRowGap = 12.0f;
constexpr float kGridLeft = 0.5f * (kW - (kGridCols * kCellTile + (kGridCols - 1) * kCellGap)), kGridTop = 144.0f;
// A cell is the tile and the skill's name under it (the whole is what the pointer picks).
inline PRect GridRect(int i) {
    const float l = kGridLeft + (i % kGridCols) * (kCellTile + kCellGap), t = kGridTop + (i / kGridCols) * (kCellH + kCellRowGap);
    return {l, t, l + kCellTile, t + kCellH};
}
inline PRect GridTileRect(int i) {
    const PRect r = GridRect(i);
    return {r.l, r.t, r.r, r.t + kCellTile};
}
inline PRect GridNameRect(int i) {
    const PRect r = GridRect(i);
    return {r.l - 4.0f, r.t + kCellTile + 4.0f, r.r + 4.0f, r.b};
}

// -- the footer's buttons ------------------------------------------------------------------------------------------------------------
constexpr float kFooterTop = 940.0f, kFooterBottom = 1020.0f, kButtonW = 340.0f, kButtonGap = 24.0f;
inline int ButtonCount(Page page) { return page == Page::Wheel ? 2 : page == Page::PickSlot ? 1 : 4; }
inline Cell ButtonCell(Page page, int index) {
    if (page == Page::Wheel) return index == 0 ? wheel::kEdit : wheel::kClose;
    if (page == Page::PickSlot) return wheel::kBack;
    return index == 0 ? wheel::kPrev : index == 1 ? wheel::kNext : index == 2 ? wheel::kCentre : wheel::kBack;   // (kCentre: CLEAR SLOT)
}
inline PRect ButtonRect(Page page, int index) {
    const int n = ButtonCount(page);
    const float total = n * kButtonW + (n - 1) * kButtonGap, left = 0.5f * (kW - total) + index * (kButtonW + kButtonGap);
    return {left, kFooterTop, left + kButtonW, kFooterBottom};
}

// -- what is where ----------------------------------------------------------------------------------------------------------------------

// A pointer this many pixels outside the cell it was on keeps it (jitter).
constexpr float kHoldPx = 8.0f;

inline bool Has(const PRect& r, float x, float y) { return x >= r.l && x < r.r && y >= r.t && y < r.b; }
inline PRect Inflate(const PRect& r, float d) { return {r.l - d, r.t - d, r.r + d, r.b + d}; }

// The rectangle of a cell on a page, or false when that cell is not there. `options`: how many grid cells the editor shows.
inline bool RectOf(Page page, int cell, int options, PRect* out) {
    for (int i = 0; i < ButtonCount(page); ++i)
        if (ButtonCell(page, i) == cell) { *out = ButtonRect(page, i); return true; }
    if (page == Page::PickSkill) {
        if (cell >= 0 && cell < kGridCount && cell < options) { *out = GridRect(cell); return true; }
        return false;
    }
    if (cell >= 0 && cell < wheel::kSlotCount) { *out = SlotRect(cell); return true; }
    return false;
}

// What a point on the window hits on this page: the cell (a slot 0..11, a grid entry, a button), or kNone. `previous` is the hover of
// the frame before (or kNone): a point just outside its edge keeps it, so a pointer shaking over a gap does not flicker. `tabs`: the strip
// is there (the top level only); `settings`: and the Settings tab is the one shown - then only the strip is hit here (the pages are the
// settings model's: vr/hset_model.h HitTest on SheetPoint).
inline int HitTest(Page page, float u, float v, int previous, int options, bool tabs = false, bool settings = false) {
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return wheel::kNone;
    const float x = u * kW, y = v * kH;
    if (tabs && page == Page::Wheel) {
        for (int i = 0; i < kTabCount; ++i)
            if (Has(TabRect(i), x, y)) return TabCell(i);
        for (int i = 0; i < 2; ++i)
            if (Has(FlipRect(i), x, y)) return FlipCell(i);
        if (settings) return wheel::kNone;
    }
    for (int i = 0; i < ButtonCount(page); ++i)
        if (Has(ButtonRect(page, i), x, y)) return ButtonCell(page, i);
    if (page == Page::PickSkill) {
        for (int i = 0; i < kGridCount && i < options; ++i)
            if (Has(GridRect(i), x, y)) return i;
    } else {
        for (int k = 0; k < wheel::kSlotCount; ++k)
            if (Has(SlotRect(k), x, y)) return k;
    }
    PRect r;
    if (previous != wheel::kNone && RectOf(page, previous, options, &r) && Has(Inflate(r, kHoldPx), x, y)) return previous;
    return wheel::kNone;
}

// The window's height in metres for a width: the picture is kW x kH, so the height follows.
inline float HeightFor(float widthM) { return widthM / kAspect; }

}  // namespace tiles
