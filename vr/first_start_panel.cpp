// first_start_panel.cpp - the first-start guide's pages as pictures: a dark rounded panel, the page's title, its rows (what you press on the
// left, what it does on the right) and the foot that says how to page on. Painted once per page with the paint kit the skill panel and the
// wrist panels use (vr/paint_kit.h: the canvas, the rounded rectangle, GDI text) and put in a static swapchain by xr.cpp when it first shows,
// as the T-pose prompt's pictures are. The words are vr/first_start_pages.h. Premultiplied RGBA, R in the low byte.

#include "first_start_wire.h"

#include "first_start_pages.h"
#include "paint_kit.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace firststart {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Col;
using wheelpaint::kit::Fonts;
using wheelpaint::kit::RoundRect;
using wheelpaint::kit::Text;
using wheel::Rect;

const Col kEdge{90, 98, 120}, kBack{16, 18, 26}, kRowA{30, 34, 46}, kRowB{24, 27, 37};
const Col kTitle{255, 205, 100}, kControl{150, 205, 255}, kBody{236, 236, 242}, kNav{125, 232, 145}, kQuiet{150, 156, 172};

}  // namespace

bool PaintPanel(int page, uint32_t* pixels) {
    if (page < 0 || page >= kPageCount || !pixels) return false;
    const PageText& t = kPages[page];
    std::vector<uint32_t> px((size_t)kPanelW * kPanelH, 0u);
    Canvas cv{px, (int)kPanelW, (int)kPanelH};
    Fonts fonts;
    const float w = (float)kPanelW, h = (float)kPanelH;
    RoundRect(cv, Rect{0, 0, w, h}, 30, kBack, 0.94f, kEdge, 0.95f, 3);

    Text(cv, fonts, t.title, Rect{44, 20, w - 170, 76}, 34, true, kTitle, 1.0f, true);
    if (page != kOfferPage) {   // where in the guide: "2 / 5"
        char at[16];
        std::snprintf(at, sizeof at, "%d / %d", page, kGuidePages);
        Text(cv, fonts, at, Rect{w - 160, 20, w - 44, 76}, 24, false, kQuiet);
    }
    // the rows: a band each, the control's name in the left column (a row without one spans the panel)
    const float top = 92, rowH = 80, gap = 6, splitX = 330;   // a row holds three lines of its text
    for (int r = 0; r < kRowsMax; ++r) {
        const Row& row = t.rows[r];
        if (!row.does || !*row.does) break;
        const float y0 = top + (float)r * (rowH + gap), y1 = y0 + rowH;
        RoundRect(cv, Rect{30, y0, w - 30, y1}, 12, r % 2 ? kRowB : kRowA, 0.96f, kRowA, 0.0f, 0);
        const bool named = row.control && *row.control;
        if (named) Text(cv, fonts, row.control, Rect{46, y0 + 4, splitX - 10, y1 - 4}, 22, true, kControl, 1.0f, true);
        Text(cv, fonts, row.does, Rect{named ? splitX : 46.0f, y0 + 4, w - 46, y1 - 4}, 20, false, kBody, 1.0f, true);
    }
    Text(cv, fonts, t.nav, Rect{44, h - 92, w - 44, h - 50}, 25, true, kNav);
    Text(cv, fonts, "The game goes on behind this. Nothing here presses anything in the game.", Rect{44, h - 48, w - 44, h - 16}, 19, false,
         kQuiet);
    std::copy(px.begin(), px.end(), pixels);
    return true;
}

}  // namespace firststart
