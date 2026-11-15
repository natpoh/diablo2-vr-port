// Offline checks of the skill panel's TILES look (vr/tiles_geom.h, vr/tiles_labels.h, vr/tiles_paint.cpp) and of how the shared wheel
// code (vr/wheel_model.h, vr/wheel_controller.h, vr/wheel_bank.h) behaves with the game's TWELVE controller slots and in that look:
// the laser-ray-to-tile hit test on synthetic rays, the layout at several window widths, the button labels for each controller profile,
// the twelve slots read and set against a fake game, the editor's 24-cell pages, a change of look while the panel is up, the panel
// standing down outside VR first person (F4), the long press that opens and closes it, and the window's picture. No game, no OpenXR,
// no headset.
// Run: build/Release/skill_tiles_test.exe            (exit code 0 = all passed)
//      build/Release/skill_tiles_test.exe --dump <folder>   also writes the window's pictures as BMP files to look at
//      (SKILL_WHEEL_ICONS=<folder of a cache made by tools/skill_icons from your own game> puts the real icons in the dumped pictures)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../vr/tiles_geom.h"
#include "../../vr/tiles_labels.h"
#include "../../vr/tiles_paint.h"
#include "../../vr/wheel_bank.h"
#include "../../vr/wheel_cast.h"
#include "../../vr/wheel_controller.h"
#include "../../vr/wheel_icons.h"
#include "../../vr/wheel_model.h"
#include "../../vr/wheel_paint.h"

using namespace wheel;
namespace di = directinput;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static float CentreX(const tiles::PRect& r) { return 0.5f * (r.l + r.r); }
static float CentreY(const tiles::PRect& r) { return 0.5f * (r.t + r.b); }
static int HitPx(Page page, float x, float y, int previous = kNone, int options = 0) {
    return tiles::HitTest(page, x / tiles::kW, y / tiles::kH, previous, options);
}

// -- geometry --------------------------------------------------------------------------------------------------------------

static bool Overlap(const tiles::PRect& a, const tiles::PRect& b) { return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b; }
static bool Inside(const tiles::PRect& r, float l, float t, float rr, float b) { return r.l >= l && r.t >= t && r.r <= rr && r.b <= b; }
static bool InsideR(const tiles::PRect& r, const tiles::PRect& o) { return Inside(r, o.l, o.t, o.r, o.b); }

static void TestGeometry() {
    // the twelve slots are each in the rows once, as the game's dialog orders them (A X B Y RB RT / the same with LT)
    int seen = 0;
    for (int r = 0; r < tiles::kRows; ++r)
        for (int c = 0; c < tiles::kCols; ++c) {
            const int s = tiles::kSlotAt[r][c];
            CHECK(s >= 0 && s < kSlotCount && !(seen & (1 << s)));
            seen |= 1 << s;
            const tiles::Place p = tiles::PlaceOf(s);
            CHECK(p.row == r && p.col == c);
        }
    CHECK(seen == (1 << kSlotCount) - 1);
    const char* const order[2][6] = {{"A", "X", "B", "Y", "RB", "RT"}, {"LT + A", "LT + X", "LT + B", "LT + Y", "LT + RB", "LT + RT"}};
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 6; ++c) CHECK(std::strcmp(kSlotDefs[tiles::kSlotAt[r][c]].label, order[r][c]) == 0);
    // tiles: on the panel, square, in order from the left with an even gap, centred, rows apart, none overlaps another
    for (int s = 0; s < kSlotCount; ++s) {
        const tiles::PRect r = tiles::SlotRect(s);
        CHECK(InsideR(r, tiles::kPanel) && Near(r.r - r.l, tiles::kTile) && Near(r.b - r.t, tiles::kTile));
        for (int u = s + 1; u < kSlotCount; ++u) CHECK(!Overlap(r, tiles::SlotRect(u)));
        // what is under it: the mark, the name, the hint - below the tile, in order, above the next row (or the status), on the panel
        const tiles::PRect g = tiles::GlyphRect(s), n = tiles::NameRect(s), h = tiles::HintRect(s);
        CHECK(g.t >= r.b && n.t >= g.b && h.t >= n.b - 1.0f && InsideR(g, tiles::kPanel) && InsideR(n, tiles::kPanel) && InsideR(h, tiles::kPanel));
        CHECK(Near(CentreX(g), CentreX(r)));
        const tiles::Place p = tiles::PlaceOf(s);
        const float limit = p.row + 1 < tiles::kRows ? tiles::kRowTop[p.row + 1] : tiles::kRedLine.t;
        CHECK(h.b <= limit);
    }
    for (int r = 0; r < tiles::kRows; ++r) {
        for (int c = 1; c < tiles::kCols; ++c) {
            const tiles::PRect a = tiles::SlotRect(tiles::kSlotAt[r][c - 1]), b = tiles::SlotRect(tiles::kSlotAt[r][c]);
            CHECK(Near(b.l - a.r, tiles::kGap) && Near(a.t, b.t));
        }
        CHECK(Near(tiles::SlotRect(tiles::kSlotAt[r][0]).l, tiles::kW - tiles::SlotRect(tiles::kSlotAt[r][5]).r));
    }
    CHECK(tiles::SlotRect(tiles::kSlotAt[1][0]).t > tiles::kRowTop[0] + tiles::kTile);
    // the LT chip: on the second row's left end, beside its first tile, on the panel; the arrows under the chip, clear of the tile
    const tiles::PRect chip = tiles::LtChipRect(), arrows = tiles::LtArrowsRect(), first = tiles::SlotRect(tiles::kSlotAt[1][0]);
    CHECK(InsideR(chip, tiles::kPanel) && chip.r < first.l && InsideR(arrows, tiles::kPanel) && arrows.r < first.l && arrows.t >= chip.b);
    CHECK(chip.t > first.t && chip.b < first.b && arrows.b < first.b + 1.0f);
    // the window's bands
    CHECK(InsideR(tiles::kTitlePlate, tiles::PRect{0, 0, float(tiles::kW), tiles::kPanel.t}) && InsideR(tiles::kPanel, tiles::PRect{0, 0, float(tiles::kW), float(tiles::kH)}));
    CHECK(tiles::kRedLine.b <= tiles::kHintLine.t && tiles::kHintLine.b <= tiles::kFooterTop && tiles::kFooterBottom <= tiles::kPanel.b);
    // the grid: 8 x 3 = 24 (the most a page of the model holds), on the panel above the red line, cells apart; the tile is the cell's top
    CHECK(tiles::kGridCount == 24 && tiles::kGridCount <= kOptionsMax);
    for (int i = 0; i < tiles::kGridCount; ++i) {
        const tiles::PRect r = tiles::GridRect(i), t = tiles::GridTileRect(i), n = tiles::GridNameRect(i);
        CHECK(InsideR(r, tiles::kPanel) && r.b <= tiles::kRedLine.t && InsideR(t, r) && Near(t.r - t.l, t.b - t.t) && n.t >= t.b && n.b <= r.b + 0.5f);
        for (int j = i + 1; j < tiles::kGridCount; ++j) CHECK(!Overlap(r, tiles::GridRect(j)));
    }
    CHECK(tiles::GridRect(0).l == tiles::GridRect(8).l && tiles::GridRect(7).l > tiles::GridRect(6).r);
    CHECK(Near(tiles::GridRect(0).l, tiles::kW - tiles::GridRect(7).r));
    // the footer: per page, in the panel, apart, centred
    for (Page page : {Page::Wheel, Page::PickSlot, Page::PickSkill}) {
        const int n = tiles::ButtonCount(page);
        CHECK(n >= 1 && n <= 4);
        for (int i = 0; i < n; ++i) {
            const tiles::PRect r = tiles::ButtonRect(page, i);
            CHECK(InsideR(r, tiles::kPanel));
            for (int j = i + 1; j < n; ++j) CHECK(!Overlap(r, tiles::ButtonRect(page, j)));
        }
        CHECK(Near(tiles::ButtonRect(page, 0).l, tiles::kW - tiles::ButtonRect(page, n - 1).r));
    }
    CHECK(tiles::ButtonCell(Page::Wheel, 0) == kEdit && tiles::ButtonCell(Page::Wheel, 1) == kClose);
    CHECK(tiles::ButtonCell(Page::PickSlot, 0) == kBack);
    CHECK(tiles::ButtonCell(Page::PickSkill, 0) == kPrev && tiles::ButtonCell(Page::PickSkill, 1) == kNext &&
          tiles::ButtonCell(Page::PickSkill, 2) == kCentre && tiles::ButtonCell(Page::PickSkill, 3) == kBack);
    CHECK(Near(tiles::kAspect, 1600.0f / 1100.0f) && Near(tiles::HeightFor(1.6f), 1.1f));
}

// the category strip and the RUN / WALK tile
static void TestStripAndRunTile() {
    // five tabs side by side across the panel's top, over the grid and under the title; none touches the grid, the footer or each other
    for (int c = 0; c < wheel::kCategories; ++c) {
        const tiles::PRect r = tiles::StripRect(c);
        CHECK(InsideR(r, tiles::kPanel) && r.b <= tiles::GridRect(0).t && r.r - r.l > 200.0f && r.b - r.t >= 44.0f);
        for (int d = c + 1; d < wheel::kCategories; ++d) CHECK(!Overlap(r, tiles::StripRect(d)) && tiles::StripRect(d).l - r.r >= 8.0f);
        for (int i = 0; i < tiles::kGridCount; ++i) CHECK(!Overlap(r, tiles::GridRect(i)));
        for (int b = 0; b < tiles::ButtonCount(Page::PickSkill); ++b) CHECK(!Overlap(r, tiles::ButtonRect(Page::PickSkill, b)));
    }
    CHECK(Near(tiles::StripRect(0).l, tiles::kW - tiles::StripRect(wheel::kCategories - 1).r));   // (centred)
    // the grid still fits above the state line with the strip over it
    CHECK(tiles::GridRect(tiles::kGridCount - 1).b <= tiles::kRedLine.t);
    // the RUN / WALK tile: on the panel, beside the first row (not over any slot, its mark, name or hint, the LT chip), a name under it
    const tiles::PRect run = tiles::kRunRect, name = tiles::kRunNameRect;
    CHECK(InsideR(run, tiles::kPanel) && InsideR(name, tiles::kPanel) && Near(run.r - run.l, run.b - run.t) && name.t >= run.b);
    for (int s = 0; s < kSlotCount; ++s) {
        CHECK(!Overlap(run, tiles::SlotRect(s)) && !Overlap(name, tiles::SlotRect(s)));
        CHECK(!Overlap(run, tiles::GlyphRect(s)) && !Overlap(run, tiles::NameRect(s)) && !Overlap(run, tiles::HintRect(s)));
        CHECK(!Overlap(name, tiles::NameRect(s)) && !Overlap(name, tiles::HintRect(s)));
    }
    CHECK(!Overlap(run, tiles::LtChipRect()) && !Overlap(run, tiles::LtArrowsRect()) && !Overlap(name, tiles::LtChipRect()));
    CHECK(run.l > tiles::SlotRect(tiles::kSlotAt[0][5]).r && name.b <= tiles::kRedLine.t);
    for (int b = 0; b < tiles::ButtonCount(Page::Wheel); ++b) CHECK(!Overlap(run, tiles::ButtonRect(Page::Wheel, b)) && !Overlap(name, tiles::ButtonRect(Page::Wheel, b)));
    CHECK(!Overlap(run, tiles::kTitlePlate) && !Overlap(run, tiles::kRedLine));
}

static int HitFlags(Page page, float x, float y, int options, bool strip, bool runWalk, int previous = kNone) {
    return tiles::HitTest(page, x / tiles::kW, y / tiles::kH, previous, options, false, false, strip, runWalk);
}

static void TestHitStripAndRunTile() {
    // each tab answers as its category when the strip is shown, and nothing when it is not; the grid under it is unchanged
    for (int c = 0; c < wheel::kCategories; ++c) {
        const tiles::PRect r = tiles::StripRect(c);
        CHECK(HitFlags(Page::PickSkill, CentreX(r), CentreY(r), 24, true, false) == wheel::CategoryCell(c));
        CHECK(HitFlags(Page::PickSkill, CentreX(r), CentreY(r), 24, false, false) == kNone);
        CHECK(HitFlags(Page::PickSkill, CentreX(r), CentreY(r), 24, true, true) == wheel::CategoryCell(c));
        CHECK(HitFlags(Page::Wheel, CentreX(r), CentreY(r), 24, true, true) != wheel::CategoryCell(c));   // (the strip is the editor's grid page only)
        CHECK(HitFlags(Page::PickSlot, CentreX(r), CentreY(r), 24, true, true) != wheel::CategoryCell(c));
    }
    for (int i = 0; i < tiles::kGridCount; ++i) {
        const tiles::PRect r = tiles::GridRect(i);
        CHECK(HitFlags(Page::PickSkill, CentreX(r), CentreY(r), 24, true, false) == i);
    }
    // the tile answers on the main page when it is there, and is nothing when it is off; on the other pages it is nothing at all
    const tiles::PRect run = tiles::kRunRect;
    CHECK(HitFlags(Page::Wheel, CentreX(run), CentreY(run), 0, false, true) == wheel::kRunWalk);
    CHECK(HitFlags(Page::Wheel, run.l + 0.5f, run.t + 0.5f, 0, false, true) == wheel::kRunWalk && HitFlags(Page::Wheel, run.r - 0.5f, run.b - 0.5f, 0, false, true) == wheel::kRunWalk);
    CHECK(HitFlags(Page::Wheel, CentreX(run), CentreY(run), 0, false, false) == kNone);
    CHECK(HitFlags(Page::PickSlot, CentreX(run), CentreY(run), 0, false, true) == kNone);
    CHECK(HitFlags(Page::PickSkill, CentreX(run), CentreY(run), 24, true, true) == kNone);
    // the slots still answer with it there
    for (int s = 0; s < kSlotCount; ++s) CHECK(HitFlags(Page::Wheel, CentreX(tiles::SlotRect(s)), CentreY(tiles::SlotRect(s)), 0, false, true) == s);
    // the hold: just outside the tile it keeps the hover, farther out it does not
    CHECK(HitFlags(Page::Wheel, run.r + 4.0f, CentreY(run), 0, false, true, wheel::kRunWalk) == wheel::kRunWalk);
    CHECK(HitFlags(Page::Wheel, run.r + 14.0f, CentreY(run), 0, false, true, wheel::kRunWalk) == kNone);
    // no point is in two cells with both on: sample the window and count the rectangles it is in
    for (Page page : {Page::Wheel, Page::PickSkill}) {
        for (float y = 1.37f; y < tiles::kH; y += 4.0f)
            for (float x = 1.37f; x < tiles::kW; x += 4.0f) {
                int in = 0, which = kNone;
                for (int cell = -1; cell <= wheel::kRunWalk; ++cell) {
                    tiles::PRect r;
                    if (tiles::RectOf(page, cell, 24, &r) && tiles::Has(r, x, y)) { ++in; which = cell; }
                }
                CHECK(in <= 1);
                CHECK(HitFlags(page, x, y, 24, true, true) == (in ? which : kNone));
            }
    }
}

static void TestHitTest() {
    // the centre of each tile is its slot - on the page that shows the row (the main page and the slot chooser alike)
    for (int s = 0; s < kSlotCount; ++s) {
        const tiles::PRect r = tiles::SlotRect(s);
        CHECK(HitPx(Page::Wheel, CentreX(r), CentreY(r)) == s);
        CHECK(HitPx(Page::PickSlot, CentreX(r), CentreY(r)) == s);
        CHECK(HitPx(Page::Wheel, r.l + 0.5f, r.t + 0.5f) == s && HitPx(Page::Wheel, r.r - 0.5f, r.b - 0.5f) == s);   // its corners
        CHECK(HitPx(Page::PickSkill, CentreX(r), CentreY(r), kNone, 0) == kNone);                                   // (no grid cells: nothing)
    }
    // the grid: exactly `options` cells answer, the name under a tile counts as the cell
    for (int options : {0, 5, 8, 24}) {
        for (int i = 0; i < tiles::kGridCount; ++i) {
            const tiles::PRect r = tiles::GridRect(i);
            CHECK(HitPx(Page::PickSkill, CentreX(r), CentreY(r), kNone, options) == (i < options ? i : kNone));
            const tiles::PRect n = tiles::GridNameRect(i);
            CHECK(HitPx(Page::PickSkill, CentreX(n), CentreY(n), kNone, options) == (i < options ? i : kNone));
        }
    }
    CHECK(HitPx(Page::Wheel, CentreX(tiles::GridRect(23)), tiles::GridRect(23).b - 4.0f, kNone, 24) == kNone);   // (the main page has no grid)
    // the footer buttons
    for (Page page : {Page::Wheel, Page::PickSlot, Page::PickSkill})
        for (int i = 0; i < tiles::ButtonCount(page); ++i) {
            const tiles::PRect r = tiles::ButtonRect(page, i);
            CHECK(HitPx(page, CentreX(r), CentreY(r), kNone, 24) == tiles::ButtonCell(page, i));
        }
    // gaps, bands, the outside and the nonsense are nothing; the name under a tile is not the tile
    const tiles::PRect a = tiles::SlotRect(8), b = tiles::SlotRect(1);
    CHECK(HitPx(Page::Wheel, 0.5f * (a.r + b.l), CentreY(a)) == kNone);                      // between two tiles of a row
    CHECK(HitPx(Page::Wheel, CentreX(a), tiles::kRowTop[0] + tiles::kTile + 5.0f) == kNone);   // under a tile
    CHECK(HitPx(Page::Wheel, CentreX(tiles::LtChipRect()), CentreY(tiles::LtChipRect())) == kNone);   // the LT mark is a mark, not a button
    CHECK(HitPx(Page::Wheel, 800.0f, 60.0f) == kNone);                                       // the title
    CHECK(HitPx(Page::Wheel, 800.0f, tiles::kRedLine.t + 10.0f) == kNone);                   // the state line
    CHECK(tiles::HitTest(Page::Wheel, -0.01f, 0.5f, kNone, 0) == kNone && tiles::HitTest(Page::Wheel, 1.0f, 0.5f, kNone, 0) == kNone);
    CHECK(tiles::HitTest(Page::Wheel, 0.5f, 1.0f, kNone, 0) == kNone && tiles::HitTest(Page::Wheel, 0.5f, -1.0f, kNone, 0) == kNone);
    CHECK(tiles::HitTest(Page::Wheel, NAN, 0.5f, kNone, 0) == kNone && tiles::HitTest(Page::Wheel, 0.5f, INFINITY, kNone, 0) == kNone);
    // no point is in two cells: sample the window (every 4 px) and count the rectangles it is in
    for (Page page : {Page::Wheel, Page::PickSlot, Page::PickSkill}) {
        int sampled = 0;
        for (float y = 1.37f; y < tiles::kH; y += 4.0f)   // (not on a whole pixel: the edges are, and u * kW is not exact there)
            for (float x = 1.37f; x < tiles::kW; x += 4.0f) {
                int in = 0, which = kNone;
                for (int cell = -1; cell <= kClose; ++cell) {
                    tiles::PRect r;
                    if (tiles::RectOf(page, cell, 24, &r) && tiles::Has(r, x, y)) { ++in; which = cell; }
                }
                CHECK(in <= 1);
                if (HitPx(page, x, y, kNone, 24) != (in ? which : kNone) && g_failures < 4) std::printf("  page %d x %.1f y %.1f: got %d, rects say %d (in %d)\n", (int)page, x, y, HitPx(page, x, y, kNone, 24), which, in);
                CHECK(HitPx(page, x, y, kNone, 24) == (in ? which : kNone));
                ++sampled;
            }
        CHECK(sampled > 100000);
    }
    // the hold: a pointer just outside the tile it was on keeps it; farther out, or on another tile, it moves
    const tiles::PRect t1 = tiles::SlotRect(1);
    CHECK(HitPx(Page::Wheel, t1.r + 5.0f, CentreY(t1), kNone) == kNone);
    CHECK(HitPx(Page::Wheel, t1.r + 5.0f, CentreY(t1), 1) == 1);
    CHECK(HitPx(Page::Wheel, t1.r + 9.0f, CentreY(t1), 1) == kNone);
    CHECK(HitPx(Page::Wheel, CentreX(tiles::SlotRect(0)), CentreY(t1), 1) == 0);
    CHECK(HitPx(Page::Wheel, t1.l - 5.0f, t1.t - 5.0f, 1) == 1);
    CHECK(HitPx(Page::PickSkill, tiles::GridRect(4).r + 4.0f, CentreY(tiles::GridRect(4)), 4, 24) == 4);
    CHECK(HitPx(Page::PickSkill, tiles::GridRect(4).r + 4.0f, CentreY(tiles::GridRect(4)), 30, 24) == kNone);   // (a previous cell that is not there)
}

// -- labels ------------------------------------------------------------------------------------------------------------------

static void TestLabels() {
    using tiles::Profile;
    CHECK(tiles::ProfileOf("/interaction_profiles/oculus/touch_controller") == Profile::Touch);
    CHECK(tiles::ProfileOf("/interaction_profiles/meta/touch_controller_plus") == Profile::Touch);
    CHECK(tiles::ProfileOf("/interaction_profiles/valve/index_controller") == Profile::Index);
    CHECK(tiles::ProfileOf("/interaction_profiles/htc/vive_controller") == Profile::Vive);
    CHECK(tiles::ProfileOf("/interaction_profiles/valve/frame_controller_valve") == Profile::Frame);
    CHECK(tiles::ProfileOf("/interaction_profiles/microsoft/motion_controller") == Profile::Wmr);
    CHECK(tiles::ProfileOf("/interaction_profiles/khr/simple_controller") == Profile::Simple);
    CHECK(tiles::ProfileOf("/interaction_profiles/some/other_thing") == Profile::Other);
    CHECK(tiles::ProfileOf("") == Profile::None && tiles::ProfileOf(nullptr) == Profile::None);
    // the face buttons as the controllers name them
    CHECK(std::strcmp(tiles::FaceButton(Profile::Touch, 0, false), "X") == 0 && std::strcmp(tiles::FaceButton(Profile::Touch, 0, true), "Y") == 0);
    CHECK(std::strcmp(tiles::FaceButton(Profile::Touch, 1, false), "A") == 0 && std::strcmp(tiles::FaceButton(Profile::Touch, 1, true), "B") == 0);
    CHECK(std::strcmp(tiles::FaceButton(Profile::Index, 0, false), "A") == 0 && std::strcmp(tiles::FaceButton(Profile::Index, 0, true), "B") == 0);
    CHECK(std::strcmp(tiles::FaceButton(Profile::Index, 1, false), "A") == 0 && std::strcmp(tiles::FaceButton(Profile::Index, 1, true), "B") == 0);
    // Steam Frame: A B on the right, the D-pad's down / up on the left
    CHECK(std::strcmp(tiles::FaceButton(Profile::Frame, 1, false), "A") == 0 && std::strcmp(tiles::FaceButton(Profile::Frame, 1, true), "B") == 0);
    CHECK(std::strcmp(tiles::FaceButton(Profile::Frame, 0, false), "Dpad Dn") == 0 && std::strcmp(tiles::FaceButton(Profile::Frame, 0, true), "Dpad Up") == 0);
    {   // the longest hint fits whole
        char fh[kHintLen];
        CHECK(tiles::HandHint(5, Profile::Frame, Profile::Frame, fh, sizeof fh) && std::string(fh) == "LT + Left Dpad Dn");
        CHECK(tiles::HandHint(6, Profile::Frame, Profile::Frame, fh, sizeof fh) && std::string(fh) == "LT + Left Dpad Up");
        CHECK(tiles::HandHint(0, Profile::Frame, Profile::Frame, fh, sizeof fh) && std::string(fh) == "Right B");
    }
    for (Profile p : {Profile::None, Profile::Vive, Profile::Wmr, Profile::Simple, Profile::Other})
        CHECK(!tiles::FaceButton(p, 0, false) && !tiles::FaceButton(p, 1, true));

    // the glyph: the game's own pad button of the slot, whatever the controllers are; the left trigger's row has the same marks
    struct Want { const char* text; tiles::GlyphKind kind; tiles::GlyphColour colour; };
    const Want want[kSlotCount] = {   // by slot number: B X Y RB, with LT: B X Y RB, A, RT, with LT: A RT
        {"B", tiles::GlyphKind::Round, tiles::GlyphColour::Red},   {"X", tiles::GlyphKind::Round, tiles::GlyphColour::Blue},
        {"Y", tiles::GlyphKind::Round, tiles::GlyphColour::Yellow}, {"RB", tiles::GlyphKind::Chip, tiles::GlyphColour::Grey},
        {"B", tiles::GlyphKind::Round, tiles::GlyphColour::Red},   {"X", tiles::GlyphKind::Round, tiles::GlyphColour::Blue},
        {"Y", tiles::GlyphKind::Round, tiles::GlyphColour::Yellow}, {"RB", tiles::GlyphKind::Chip, tiles::GlyphColour::Grey},
        {"A", tiles::GlyphKind::Round, tiles::GlyphColour::Green}, {"RT", tiles::GlyphKind::Chip, tiles::GlyphColour::Grey},
        {"A", tiles::GlyphKind::Round, tiles::GlyphColour::Green}, {"RT", tiles::GlyphKind::Chip, tiles::GlyphColour::Grey}};
    for (int s = 0; s < kSlotCount; ++s) {
        const tiles::Glyph g = tiles::GlyphOf(s);
        CHECK(std::strcmp(g.text, want[s].text) == 0 && g.kind == want[s].kind && g.colour == want[s].colour);
        CHECK(g.lt == kSlotDefs[s].alt);
    }
    CHECK(!tiles::GlyphOf(-1).text[0] && !tiles::GlyphOf(12).text[0]);

    // the hint: Touch - B is the right B, X the left X, Y the left Y, RB the right grip with B, A the layout's right trigger, RT nothing
    char h[kHintLen];
    auto hint = [&](int slot, Profile l, Profile r) { return tiles::HandHint(slot, l, r, h, sizeof h) ? std::string(h) : std::string("-"); };
    CHECK(hint(0, Profile::Touch, Profile::Touch) == "Right B" && hint(1, Profile::Touch, Profile::Touch) == "Left X");
    CHECK(hint(2, Profile::Touch, Profile::Touch) == "Left Y" && hint(3, Profile::Touch, Profile::Touch) == "R grip + B");
    CHECK(hint(4, Profile::Touch, Profile::Touch) == "LT + Right B" && hint(5, Profile::Touch, Profile::Touch) == "LT + Left X");
    CHECK(hint(6, Profile::Touch, Profile::Touch) == "LT + Left Y" && hint(7, Profile::Touch, Profile::Touch) == "LT + R grip + B");
    CHECK(hint(8, Profile::Touch, Profile::Touch) == "R trigger" && hint(10, Profile::Touch, Profile::Touch) == "LT + R trigger");
    CHECK(hint(9, Profile::Touch, Profile::Touch) == "-" && hint(11, Profile::Touch, Profile::Touch) == "-");
    // Index has A / B on both hands: the left hand's two buttons are A and B, not X and Y
    CHECK(hint(0, Profile::Index, Profile::Index) == "Right B" && hint(1, Profile::Index, Profile::Index) == "Left A");
    CHECK(hint(2, Profile::Index, Profile::Index) == "Left B" && hint(3, Profile::Index, Profile::Index) == "R grip + B");
    CHECK(hint(5, Profile::Index, Profile::Index) == "LT + Left A" && hint(6, Profile::Index, Profile::Index) == "LT + Left B");
    CHECK(hint(8, Profile::Index, Profile::Index) == "R trigger");
    // two different controllers: each hand by its own profile
    CHECK(hint(1, Profile::Touch, Profile::Index) == "Left X" && hint(1, Profile::Index, Profile::Touch) == "Left A");
    CHECK(hint(0, Profile::Touch, Profile::Index) == "Right B");
    // no face buttons, no profile: no hint on the buttons (the trigger is every controller's, but only where a profile is known)
    for (Profile p : {Profile::Vive, Profile::Wmr, Profile::Simple, Profile::Other})
        for (int s = 0; s < kSlotCount; ++s) CHECK(hint(s, p, p) == (s == 8 ? "R trigger" : s == 10 ? "LT + R trigger" : "-"));
    for (int s = 0; s < kSlotCount; ++s) CHECK(hint(s, Profile::None, Profile::None) == "-");
    CHECK(hint(0, Profile::Touch, Profile::None) == "-" && hint(3, Profile::Touch, Profile::None) == "-" && hint(8, Profile::Touch, Profile::None) == "-");
    CHECK(hint(1, Profile::Touch, Profile::None) == "Left X" && hint(2, Profile::Touch, Profile::None) == "Left Y");
    CHECK(hint(0, Profile::None, Profile::Touch) == "Right B" && hint(1, Profile::None, Profile::Touch) == "-");
    CHECK(!tiles::HandHint(-1, Profile::Touch, Profile::Touch, h, sizeof h) && h[0] == 0);
    CHECK(!tiles::HandHint(12, Profile::Touch, Profile::Touch, h, sizeof h) && !tiles::HandHint(0, Profile::Touch, Profile::Touch, nullptr, 0));
    // the longest hint fits the buffer the view carries
    for (int s = 0; s < kSlotCount; ++s) { tiles::HandHint(s, Profile::Touch, Profile::Touch, h, sizeof h); CHECK(std::strlen(h) < (size_t)kHintLen); }
    // all of them at once, and `show` false empties them (direct input off, or its layout rebound)
    const Hints on = tiles::BuildHints(Profile::Touch, Profile::Touch, true), off = tiles::BuildHints(Profile::Touch, Profile::Touch, false);
    CHECK(std::strcmp(on.text[0], "Right B") == 0 && std::strcmp(on.text[7], "LT + R grip + B") == 0 && std::strcmp(on.text[10], "LT + R trigger") == 0);
    bool allEmpty = true;
    for (int s = 0; s < kSlotCount; ++s) allEmpty = allEmpty && !off.text[s][0];
    CHECK(allEmpty && !(on == off) && off == Hints{});
}

// -- the twelve slots against a fake game (vr/wheel_bank.h) ---------------------------------------------------------------------------

// A game with six bases: the lookup finds B X Y RB and A by their bits (A at `aBase`), adds 6 for the left trigger; RT has no bit.
struct Game12 : Game {
    int aBase = 0;                  // A's base slot (-1: the lookup does not know A)
    int bases[4] = {2, 3, 4, 5};    // B X Y RB
    bool controller = true;
    int bank = 0;
    bool ignoreWrites = false;
    Binding table[32];
    int assigns = 0, clears = 0, lastIndex = -1;
    int ButtonSlot(uint16_t button, bool alt) override {
        int base = -1;
        if (button == kPadB) base = bases[0];
        else if (button == kPadX) base = bases[1];
        else if (button == kPadY) base = bases[2];
        else if (button == kPadRb) base = bases[3];
        else if (button == kPadA) base = aBase;
        return base < 0 ? -1 : alt ? base + 6 : base;
    }
    bool ControllerLayout() override { return controller; }
    int Bank() override { return bank; }
    bool ReadBinding(int index, Binding* out) override {
        if (index < 0 || index >= 32) return false;
        *out = table[index];
        return true;
    }
    bool HasSkill(int32_t, int32_t, int32_t* level) override { *level = 3; return true; }
    bool LeftFlag(int32_t, int32_t, bool* left) override { *left = false; return true; }
    bool LearnedSkills(std::vector<Learned>* out) override {
        *out = {{36, -1, 5}, {47, -1, 3}, {54, -1, 1}, {70, -1, 2}};
        return true;
    }
    bool Name(int32_t id, char* out, int n) override { std::snprintf(out, n, "Skill %d", (int)id); return true; }
    void DoAssign(int slotIndex, bool, int32_t skill, int32_t owner) override {
        ++assigns;
        lastIndex = slotIndex;
        if (ignoreWrites) return;
        for (int i = 0; i < 16; ++i) if (table[bank + i].skill == skill && i != slotIndex) table[bank + i] = Binding{};
        table[bank + slotIndex] = Binding{skill, owner};
    }
    void DoClear(int slotIndex) override {
        ++clears;
        lastIndex = slotIndex;
        if (!ignoreWrites) table[bank + slotIndex] = Binding{};
    }
    Game12() { table[0] = {70, -1}; table[1] = {54, -1}; table[2] = {36, -1}; table[3] = {47, -1}; table[6] = {70, -1}; }
};

static void TestTwelveSlots() {
    {   // A comes from the game's own lookup, RT is the one base left: slot indices for all twelve
        Game12 g;
        int idx[kSlotCount];
        CHECK(SlotIndices(g, idx));
        const int expect[kSlotCount] = {2, 3, 4, 5, 8, 9, 10, 11, 0, 1, 6, 7};   // B X Y RB | with LT | A RT | LT + A, LT + RT
        for (int i = 0; i < kSlotCount; ++i) CHECK(idx[i] == expect[i]);
        Snapshot s;
        CHECK(Capture(g, &s) && s.ready && s.bank == 0);
        CHECK(s.slot[8].known && s.slot[8].id == 70 && s.slot[8].available);                 // A holds skill 70 (table[0])
        CHECK(s.slot[9].known && s.slot[9].id == 54);                                         // RT: table[1]
        CHECK(s.slot[10].known && s.slot[10].id == 70 && s.slot[11].known && s.slot[11].id == -1);   // LT + A (table[6]); LT + RT (table[7]) empty
        CHECK(s.slot[0].id == 36 && s.slot[1].id == 47);
        // the same with another weapon set's bank
        g.bank = 16;
        g.table[16] = {47, -1};
        CHECK(Capture(g, &s) && s.bank == 16 && s.slot[8].id == 47);
    }
    {   // A somewhere else: RT is still the one left over
        Game12 g;
        g.aBase = 1;
        int idx[kSlotCount];
        CHECK(SlotIndices(g, idx) && idx[8] == 1 && idx[9] == 0 && idx[10] == 7 && idx[11] == 6);
        g.aBase = 5;
        g.bases[3] = 0;   // RB takes 0
        CHECK(SlotIndices(g, idx) && idx[8] == 5 && idx[9] == 1 && idx[3] == 0);
    }
    {   // what cannot be told is not guessed: the first eight work, the other four are "not there"
        auto unknown = [](Game12& g, const char* what) {
            int idx[kSlotCount];
            CHECK(SlotIndices(g, idx));
            for (int i = 0; i < kWheelSlots; ++i) CHECK(idx[i] >= 0);
            for (int i = kWheelSlots; i < kSlotCount; ++i) {
                if (idx[i] != -1) std::printf("  (%s: slot %d got %d)\n", what, i, idx[i]);
                CHECK(idx[i] == -1);
            }
            Snapshot s;
            CHECK(Capture(g, &s) && s.ready);
            for (int i = kWheelSlots; i < kSlotCount; ++i) CHECK(!s.slot[i].known && s.slot[i].id == -1);
            for (int i = 0; i < kWheelSlots; ++i) CHECK(s.slot[i].known);
        };
        Game12 none;
        none.aBase = -1;                       // the lookup does not know A
        unknown(none, "no A");
        Game12 clash;
        clash.aBase = 2;                       // A on B's base
        unknown(clash, "A on B");
        Game12 outside;
        outside.aBase = 7;                     // not one of the six bases
        unknown(outside, "A out of range");
        Game12 wide;
        wide.bases[3] = 6;                     // RB beyond the six: not the layout this knows
        unknown(wide, "base beyond six");
    }
    {   // a different alternate offset than six: A's pair does not add up, nothing is guessed
        struct Odd : Game12 {
            int ButtonSlot(uint16_t button, bool alt) override {
                const int s = Game12::ButtonSlot(button, alt);
                return button == kPadA && alt && s >= 0 ? s + 1 : s;
            }
        } odd;
        int idx[kSlotCount];
        CHECK(SlotIndices(odd, idx));
        for (int i = kWheelSlots; i < kSlotCount; ++i) CHECK(idx[i] == -1);
    }
    {   // the setter on the new slots: A and RT through the game's own setter with the read-back; a clear; a refusal writes nothing
        Game12 g;
        Snapshot s;
        CHECK(Capture(g, &s));
        s.session = 5;
        s.at = 1000;
        auto request = [&](int slot, Op op, int32_t skill) {
            Request r;
            r.op = op;
            r.session = 5;
            r.slot = slot;
            r.skill = op == Op::Assign ? skill : -1;
            r.bank = 0;
            r.prevSkill = s.slot[slot].id;
            r.prevOwner = s.slot[slot].owner;
            return r;
        };
        const char* why = nullptr;
        CHECK(Apply(g, request(9, Op::Assign, 47), s, 1100, &why) == Result::Applied && g.lastIndex == 1 && g.table[1].skill == 47);   // RT
        CHECK(g.table[3].skill == -1);                                                            // (the game's setter took 47 off the slot that had it)
        Snapshot s2;
        CHECK(Capture(g, &s2));
        s2.session = 5;
        s2.at = 1000;
        s = s2;
        CHECK(Apply(g, request(8, Op::Assign, 36), s, 1100, &why) == Result::Applied && g.lastIndex == 0 && g.table[0].skill == 36);   // A
        CHECK(Apply(g, request(10, Op::Clear, -1), s, 1100, &why) == Result::Applied && g.lastIndex == 6 && g.table[6].skill == -1);   // LT + A
        const int writes = g.assigns + g.clears;
        Game12 lost;
        lost.aBase = -1;
        Snapshot s3;
        CHECK(Capture(lost, &s3));
        s3.session = 5;
        s3.at = 1000;
        Request bad;
        bad.op = Op::Assign;
        bad.session = 5;
        bad.slot = 8;
        bad.skill = 36;
        bad.bank = 0;
        CHECK(Apply(lost, bad, s3, 1100, &why) == Result::Refused && lost.assigns == 0);          // a slot this cannot tell is not written
        bad.slot = 12;
        CHECK(Apply(g, bad, s, 1100, &why) == Result::Refused && g.assigns + g.clears == writes);
        g.ignoreWrites = true;
        CHECK(Apply(g, request(9, Op::Assign, 54), s, 1100, &why) == Result::Refused);            // the game did not take it
    }
}

// -- the shared model in the tiles look -----------------------------------------------------------------------------------------------

static Skill Sk(int id, const char* name, bool available = true, int level = 5, int owner = -1) {
    Skill s;
    s.id = id; s.owner = owner; s.known = true; s.available = available; s.level = level;
    std::snprintf(s.name, kNameLen, "%s", name);
    return s;
}

// A snapshot with all twelve slots read: B Fire Bolt, X Fire Ball, Y empty, RB Teleport (gone), LT+ Frozen Armor, Ice Blast, Town Portal,
// Meteor, A Attack, RT Nova, LT + A Charged Bolt, LT + RT empty.
static Snapshot MakeSnap(uint64_t session, uint64_t at, int catalogCount) {
    Snapshot s;
    s.session = session; s.at = at; s.ready = true; s.editable = true; s.bank = 0;
    s.slot[0] = Sk(36, "Fire Bolt");
    s.slot[1] = Sk(47, "Fire Ball");
    s.slot[2] = Skill{}; s.slot[2].known = true;                    // Y: empty
    s.slot[3] = Sk(54, "Teleport", false);                         // RB: a skill the player no longer has
    s.slot[4] = Sk(40, "Frozen Armor");
    s.slot[5] = Sk(45, "Ice Blast");
    s.slot[6] = Sk(kSkillTownPortal, "Town Portal", true, 0);
    s.slot[7] = Sk(56, "Meteor");
    s.slot[8] = Sk(kSkillInteract + 1, "Attack");                  // A
    s.slot[9] = Sk(48, "Nova");                                    // RT
    s.slot[10] = Sk(38, "Charged Bolt");                           // LT + A
    s.slot[11] = Skill{}; s.slot[11].known = true;                 // LT + RT: empty
    s.count = (uint32_t)catalogCount;
    for (int i = 0; i < catalogCount; ++i) {
        char name[kNameLen];
        std::snprintf(name, sizeof name, "Skill %d", i);
        s.catalog[i] = Sk(100 + i, name, true, 1 + i % 20);
    }
    return s;
}

struct Rig {
    Model m;
    Snapshot snap;
    double t = 10.0;
    uint64_t now = 5000;
    Style style = Style::Tiles;
    Hints hints;
    Reply reply;
    bool categories = false, runWalk = false;   // [openxr] skill_categories / skill_run_walk
    bool slotControls = false;                   // [openxr] skill_rt_a
    explicit Rig(Style st = Style::Tiles, int catalog = 30) : style(st) { snap = MakeSnap(7, now, catalog); }
    Out Step(bool toggle = false, bool click = false, float u = -1, float v = -1) {
        In in;
        in.time = t += 0.01;
        in.nowMs = now += 10;
        snap.at = now;
        in.allowed = true;
        in.session = 7;
        in.epoch = 1;
        in.toggle = toggle;
        in.pointer = u >= 0;
        in.u = u;
        in.v = v;
        in.click = click;
        in.snap = &snap;
        in.reply = reply;
        in.style = style;
        in.hints = &hints;
        in.categories = categories;
        in.runWalk = runWalk;
        in.slotControls = slotControls;
        return m.Step(in);
    }
    Out PointPx(float x, float y, bool click) { return Step(false, click, x / tiles::kW, y / tiles::kH); }
    Out Point(Page page, int cell, bool click) {
        tiles::PRect r;
        CHECK(tiles::RectOf(page, cell, m.Current().options, &r));
        return PointPx(CentreX(r), CentreY(r), click);
    }
};

static void TestModelTiles() {
    {   // open, hover each of the twelve tiles, the picks: Pad slots are cast by their button, A is the layout's trigger, RT cannot be cast
        Rig r;
        Out o = r.Step(true);
        CHECK(r.m.Open() && o.feedback == Feedback::Opened && r.m.Current().style == Style::Tiles);
        for (int k = 0; k < kSlotCount; ++k) {
            r.Point(Page::Wheel, k, false);
            CHECK(r.m.Current().hover == k);
        }
        r.Point(Page::Wheel, 4, false);
        o = r.Point(Page::Wheel, 4, true);                                  // LT + B
        CHECK(!r.m.Open() && r.m.Selected() == 4 && o.feedback == Feedback::Selected);
        r.Step(true);
        o = r.Point(Page::Wheel, 2, true);                                  // Y is empty: refused, stays open
        CHECK(r.m.Open() && r.m.Selected() == 4 && o.feedback == Feedback::Refused);
        o = r.Point(Page::Wheel, 3, true);                                  // RB holds a skill the player no longer has: refused
        CHECK(r.m.Open() && r.m.Selected() == 4 && o.feedback == Feedback::Refused);
        o = r.Point(Page::Wheel, 9, true);                                  // RT: shown, not castable from here
        CHECK(r.m.Open() && r.m.Selected() == 4 && o.feedback == Feedback::Refused && std::strstr(r.m.Current().status, "cannot be cast") != nullptr);
        o = r.Point(Page::Wheel, 11, true);                                 // LT + RT, empty and not castable
        CHECK(r.m.Open() && r.m.Selected() == 4 && o.feedback == Feedback::Refused);
        o = r.Point(Page::Wheel, 10, true);                                 // LT + A: cast as the pad's A with LT
        CHECK(!r.m.Open() && r.m.Selected() == 10 && o.feedback == Feedback::Selected);
        r.Step(true);
        o = r.Point(Page::Wheel, 8, true);                                  // A: the layout's own trigger again (the basic attack)
        CHECK(!r.m.Open() && r.m.Selected() == -1 && o.feedback == Feedback::Selected);
        r.Step(true);
        r.Point(Page::Wheel, kClose, false);
        o = r.Point(Page::Wheel, kClose, true);
        CHECK(!r.m.Open() && o.feedback == Feedback::Closed);
    }
    {   // A is chosen even when its slot is empty or the game state is old: it is the layout's trigger, not a read of the game
        Rig r;
        r.snap.slot[8] = Skill{};
        r.snap.slot[8].known = true;
        r.Step(true);
        r.Point(Page::Wheel, 8, false);
        const Out o = r.Point(Page::Wheel, 8, true);
        CHECK(!r.m.Open() && r.m.Selected() == -1 && o.feedback == Feedback::Selected);
    }
    {   // the editor: 30 skills = pages of 24 and 6; any of the twelve slots can be edited, RT and A included; a pick on page 2 assigns that skill
        Rig r;
        r.Step(true);
        r.Point(Page::Wheel, kEdit, true);
        CHECK(r.m.Current().page == Page::PickSlot);
        r.Point(Page::PickSlot, 9, false);
        r.Point(Page::PickSlot, 9, true);                                   // RT
        const View& v = r.m.Current();
        CHECK(v.page == Page::PickSkill && v.editSlot == 9 && v.options == 24 && v.editPages == 2 && v.editPage == 0);
        CHECK(v.option[0].id == 100 && v.option[23].id == 123);
        r.Point(Page::PickSkill, kNext, true);
        CHECK(r.m.Current().editPage == 1 && r.m.Current().options == 6 && r.m.Current().option[0].id == 124 && r.m.Current().option[5].id == 129);
        r.Point(Page::PickSkill, kNext, true);
        CHECK(r.m.Current().editPage == 1);                                 // (no page after the last)
        r.PointPx(CentreX(tiles::GridRect(6)), CentreY(tiles::GridRect(6)), false);   // (only 6 cells here: the seventh place is nothing)
        CHECK(r.m.Current().hover == kNone);
        Out o = r.Point(Page::PickSkill, 5, true);
        const Request first = o.req;
        CHECK(o.request && first.op == Op::Assign && first.slot == 9 && first.skill == 129 && first.bank == 0 && first.prevSkill == 48);
        o = r.Point(Page::PickSkill, 5, true);                              // a second click while the request is out: not a second request
        CHECK(!o.request);
        r.reply = Reply{first.sequence, Result::Applied};
        o = r.Step();
        CHECK(o.feedback == Feedback::Applied && r.m.Current().page == Page::PickSlot);
        // CLEAR SLOT is a footer button; A is editable too
        r.Point(Page::PickSlot, 8, false);
        r.Point(Page::PickSlot, 8, true);
        CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().editSlot == 8);
        r.Point(Page::PickSkill, kCentre, false);
        o = r.Point(Page::PickSkill, kCentre, true);
        CHECK(o.request && o.req.op == Op::Clear && o.req.slot == 8);
        r.reply = Reply{o.req.sequence, Result::Refused};
        r.Step();
        CHECK(r.m.Current().page == Page::PickSlot);
        r.Point(Page::PickSlot, kBack, true);
        CHECK(r.m.Current().page == Page::Wheel);
    }
    {   // a slot the game side could not place (not known) is shown as not there and cannot be edited
        Rig r;
        r.snap.slot[9] = Skill{};   // known = false
        r.Step(true);
        r.Point(Page::Wheel, kEdit, true);
        r.Point(Page::PickSlot, 9, false);
        const Out o = r.Point(Page::PickSlot, 9, true);
        CHECK(r.m.Current().page == Page::PickSlot && o.feedback == Feedback::Refused);
        r.Point(Page::PickSlot, 11, true);
        CHECK(r.m.Current().page == Page::PickSkill);   // (LT + RT is known: that one edits)
    }
    {   // the wheel's list still pages by 8 and its ring has its eight wedges (the same model, the other look)
        Rig r(Style::Wheel);
        r.Step(true);
        CHECK(r.m.Open() && r.m.Current().style == Style::Wheel);
        float u, v;
        WedgePoint(2, 0.27f, &u, &v);
        r.Step(false, false, u, v);
        CHECK(r.m.Current().hover == 2);
        const Rect e = FooterRect(Page::Wheel, 0);
        r.Step(false, false, 0.5f * (e.l + e.r), 0.5f * (e.t + e.b));
        CHECK(r.m.Current().hover == kEdit);
        r.Step(false, true, 0.5f * (e.l + e.r), 0.5f * (e.t + e.b));
        CHECK(r.m.Current().page == Page::PickSlot);
        WedgePoint(2, 0.27f, &u, &v);
        r.Step(false, false, u, v);
        r.Step(false, true, u, v);
        CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().options == 8 && r.m.Current().editPages == 4);
        // the wheel's centre is still the basic attack: the selection cleared
        Rig w(Style::Wheel);
        w.Step(true);
        w.Step(false, false, kCx, kCy);
        CHECK(w.m.Current().hover == kCentre);
        w.Step(false, true, kCx, kCy);
        CHECK(!w.m.Open() && w.m.Selected() == -1);
    }
}

// -- the controller on synthetic headset frames ---------------------------------------------------------------------------------

struct World {
    Controller c;
    Snapshot snap;
    uint64_t now = 20000;
    uint32_t counter = 0;
    double time = 100.0;
    di::Frame f{};
    Reply reply;
    bool f4 = true;                // the view is VR first person
    bool request = false;          // the long press's request, for the next frame only
    const SettingsIn* host = nullptr;   // the Settings tab's side (null: no tab strip, as the window was)
    explicit World(Style st = Style::Tiles, float tilesW = 0.9f, int catalog = 30, int openMode = 0) {
        Settings cfg;
        cfg.style = st;
        cfg.tilesWidthM = tilesW;
        cfg.openMode = openMode;
        c.Configure(cfg);
        snap = MakeSnap(3, now, catalog);
        f.version = di::kVersion;
        f.session = 3;
        f.epoch = 1;
        f.flags = di::kFocused | di::kDirect;
        for (int h = 0; h < 2; ++h) {
            f.hand[h].active = 1;
            f.hand[h].grip.valid = f.hand[h].aim.valid = 1;
            f.hand[h].grip.rot[3] = f.hand[h].aim.rot[3] = 1.0f;
            f.hand[h].aim.pos[0] = h ? 0.25f : -0.25f;
            f.hand[h].aim.pos[1] = 1.2f;
            f.hand[h].aim.pos[2] = -0.3f;
        }
        f.head.valid = 1;
        f.head.pos[1] = 1.6f;
        f.head.rot[3] = 1.0f;
    }
    Step Next(double dt = 0.011) {
        f.counter = ++counter;
        f.time = time += dt;
        f.displayTime = (int64_t)(time * 1e9);
        now += (uint64_t)(dt * 1000.0);
        snap.at = now;
        const bool req = request;
        request = false;
        return c.Frame(f, snap, now, reply, f4, req, host);
    }
    void Hand(int h, float trigger, float squeeze, uint32_t buttons) {
        f.hand[h].trigger = trigger;
        f.hand[h].squeeze = squeeze;
        f.hand[h].buttons = buttons;
    }
    // The pointing hand (right) aims at a point of the panel, given as u, v in 0..1 (it may be outside).
    void AimUV(float u, float v) {
        const Pose p = c.PanelPose();
        const float w = PanelWidthM(c.Config()), h = PanelHeightM(c.Config());
        const V3 target = Add(p.p, Rot(p.q, V3{(u - 0.5f) * w, (0.5f - v) * h, 0.0f}));
        const V3 from{f.hand[1].aim.pos[0], f.hand[1].aim.pos[1], f.hand[1].aim.pos[2]};
        const V3 d = Unit(Sub(target, from));
        const V3 z{0, 0, -1};
        V3 axis{z.y * d.z - z.z * d.y, z.z * d.x - z.x * d.z, z.x * d.y - z.y * d.x};
        const float s = Len(axis), cs = Dot(z, d);
        const float ang = std::atan2(s, cs);
        axis = s > 1e-6f ? Mul(axis, 1.0f / s) : V3{0, 1, 0};
        f.hand[1].aim.rot[0] = axis.x * std::sin(ang / 2);
        f.hand[1].aim.rot[1] = axis.y * std::sin(ang / 2);
        f.hand[1].aim.rot[2] = axis.z * std::sin(ang / 2);
        f.hand[1].aim.rot[3] = std::cos(ang / 2);
    }
    void AimPx(float x, float y) { AimUV(x / tiles::kW, y / tiles::kH); }
    void AimCell(Page page, int cell) {
        tiles::PRect r;
        const bool there = tiles::RectOf(page, cell, c.ViewNow().options, &r);
        CHECK(there);
        if (there) AimPx(CentreX(r), CentreY(r));
    }
    // aim at a cell, pull the trigger (the frame returned is the one with the click), let go
    Step ClickCell(Page page, int cell) {
        AimCell(page, cell);
        Hand(1, 0, 0, 0);
        Next();
        Hand(1, 0.9f, 0, 0);
        const Step s = Next();
        Hand(1, 0, 0, 0);
        Next();
        return s;
    }
};

// The chord: the other hand's grip, then the pointing hand's stick click; then both let go
static Step OpenPanel(World& w) {
    w.Hand(0, 0, 0.9f, 0);   // the left grip
    w.Next();
    w.Hand(1, 0, 0, di::kStick);   // and the right stick click
    const Step s = w.Next();
    w.Hand(0, 0, 0, 0);
    w.Hand(1, 0, 0, 0);
    w.Next();
    return s;
}

// the shared model in the tiles look: categories and RUN / WALK
static Snapshot TreeSnap(int catalogCount) {
    Snapshot s = MakeSnap(7, 5000, catalogCount);
    s.treesKnown = true;
    s.playerClass = 1;
    std::snprintf(s.treeName[0], kNameLen, "%s", "Fire Spells");
    std::snprintf(s.treeName[1], kNameLen, "%s", "Lightning Spells");
    std::snprintf(s.treeName[2], kNameLen, "%s", "Cold Spells");
    for (int i = 0; i < catalogCount; ++i) s.catalog[i].tree = i < 10 ? 1 : i < 12 ? 2 : i < 30 ? 0 : 3;   // 10 / 2 / 0 and the rest General
    return s;
}

static void TestModelStripAndRunTile() {
    {   // the strip in the grid, tiles look: 30 skills = 10 on tree 1, 2 on tree 2, none on tree 3, 18 General
        Rig r;
        r.categories = true;
        r.snap = TreeSnap(30);
        r.Step(true);
        r.Point(Page::Wheel, kEdit, true);
        r.Point(Page::PickSlot, 3, true);
        const View& v = r.m.Current();
        CHECK(v.page == Page::PickSkill && v.catStrip && v.category == wheel::kCatAll && v.options == 24 && v.editPages == 2);
        CHECK(v.catCount[wheel::kCatAll] == 30 && v.catCount[wheel::kCatTree1] == 10 && v.catCount[wheel::kCatTree2] == 2 && v.catCount[wheel::kCatTree3] == 0 && v.catCount[wheel::kCatGeneral] == 18);
        r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatTree1), false);
        CHECK(v.hover == wheel::CategoryCell(wheel::kCatTree1));
        Out o = r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatTree1), true);
        CHECK(o.feedback == Feedback::Hover && v.category == wheel::kCatTree1 && v.options == 10 && v.editPages == 1 && v.option[0].id == 100 && v.option[9].id == 109);
        o = r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatTree3), true);   // empty: refused, the one shown stays
        CHECK(o.feedback == Feedback::Refused && v.category == wheel::kCatTree1);
        r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatGeneral), true);
        CHECK(v.category == wheel::kCatGeneral && v.options == 18 && v.option[0].id == 112);
        // a pick in a category: that skill, for that slot
        o = r.Point(Page::PickSkill, 4, true);
        CHECK(o.request && o.req.op == Op::Assign && o.req.slot == 3 && o.req.skill == 116);
        r.reply = Reply{o.req.sequence, Result::Applied};
        r.Step();
        CHECK(v.page == Page::PickSlot && !v.catStrip);
        // the strip's place is nothing when the editor is not on the grid page; the next editor opens on All
        r.Point(Page::PickSlot, 0, true);
        CHECK(v.category == wheel::kCatAll && v.options == 24);
    }
    {   // no strip: the key is off, or the game side does not know the trees
        for (int variant = 0; variant < 2; ++variant) {
            Rig r;
            r.categories = variant == 1;
            r.snap = TreeSnap(30);
            if (variant == 1) r.snap.treesKnown = false;
            r.Step(true);
            r.Point(Page::Wheel, kEdit, true);
            r.Point(Page::PickSlot, 3, true);
            const View& v = r.m.Current();
            CHECK(v.page == Page::PickSkill && !v.catStrip && v.options == 24 && v.editPages == 2);
            const tiles::PRect t = tiles::StripRect(wheel::kCatTree1);
            r.PointPx(CentreX(t), CentreY(t), true);
            CHECK(v.category == wheel::kCatAll && v.options == 24 && v.hover == kNone);
        }
    }
    {   // the strip is the model's: the wheel look has it above its list of 8 too
        Rig r(Style::Wheel);
        r.categories = true;
        r.snap = TreeSnap(30);
        r.Step(true);
        const Rect e = FooterRect(Page::Wheel, 0);
        r.Step(false, true, 0.5f * (e.l + e.r), 0.5f * (e.t + e.b));
        float u, v;
        WedgePoint(2, 0.27f, &u, &v);
        r.Step(false, false, u, v);
        r.Step(false, true, u, v);
        CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().catStrip && r.m.Current().options == 8);
    }
    {   // RUN / WALK on the main page: clicked once, the host is told; the panel stays up; the state is the game's
        Rig r;
        r.runWalk = true;
        r.Step(true);
        CHECK(r.m.Current().runWalk);
        r.Point(Page::Wheel, wheel::kRunWalk, false);
        CHECK(r.m.Current().hover == wheel::kRunWalk);
        Out o = r.Point(Page::Wheel, wheel::kRunWalk, true);
        CHECK(o.runWalk && o.feedback == Feedback::Selected && r.m.Open() && r.m.Current().page == Page::Wheel && !o.request);
        o = r.Point(Page::Wheel, wheel::kRunWalk, false);
        CHECK(!o.runWalk);
        CHECK(r.m.Selected() == -1);   // (picks no slot)
        r.snap.run = wheel::kRunning;
        r.Step();
        CHECK(r.m.Current().run == wheel::kRunning);
        r.snap.run = wheel::kWalking;
        r.Step();
        CHECK(r.m.Current().run == wheel::kWalking);
        // the slot chooser has no such tile: the same point is nothing
        r.Point(Page::Wheel, kEdit, true);
        CHECK(r.m.Current().page == Page::PickSlot);
        const tiles::PRect run = tiles::kRunRect;
        o = r.PointPx(CentreX(run), CentreY(run), true);
        CHECK(!o.runWalk && r.m.Current().page == Page::PickSlot);
    }
    {   // the key off: no tile, the same point is nothing
        Rig r;
        r.Step(true);
        CHECK(!r.m.Current().runWalk);
        const tiles::PRect run = tiles::kRunRect;
        r.PointPx(CentreX(run), CentreY(run), false);
        CHECK(r.m.Current().hover == kNone);
        const Out o = r.PointPx(CentreX(run), CentreY(run), true);
        CHECK(!o.runWalk && r.m.Open());
    }
}

static void TestControllerTiles() {
    {   // opening: the window hangs ahead of the head, lower than the wheel
        World w;
        OpenPanel(w);
        CHECK(w.c.Open() && w.c.ViewNow().style == Style::Tiles && w.c.ViewNow().page == Page::Wheel);
        const Step t = w.Next();
        CHECK(t.layer.show && t.layer.style == Style::Tiles && Near(t.layer.widthM, 0.9f) && Near(t.layer.heightM, 0.9f / tiles::kAspect));
        const Pose p = w.c.PanelPose();
        CHECK(Near(p.p.x, 0.0f) && Near(p.p.y, 1.6f - 0.15f) && Near(p.p.z, -0.8f));
    }
    {   // the wheel's default look is exactly what it was: square, 8 cm below the eyes
        World w(Style::Wheel);
        OpenPanel(w);
        const Step t = w.Next();
        CHECK(t.layer.show && t.layer.style == Style::Wheel && Near(t.layer.widthM, 0.8f) && Near(t.layer.heightM, 0.8f));
        CHECK(Near(w.c.PanelPose().p.y, 1.6f - 0.08f));
    }
    {   // a laser ray on each of the twelve tiles hovers it, the beam ends on the window
        World w;
        OpenPanel(w);
        for (int k = 0; k < kSlotCount; ++k) {
            w.AimCell(Page::Wheel, k);
            const Step s = w.Next();
            CHECK(w.c.ViewNow().hover == k);
            CHECK(s.layer.beam && s.layer.hit && s.layer.len > 0.3f && s.layer.len < 1.3f);
        }
        w.AimCell(Page::Wheel, kEdit);
        w.Next();
        CHECK(w.c.ViewNow().hover == kEdit);
        // beside the window and above it: no hover, the beam goes on to its full length
        w.AimUV(-0.2f, 0.5f);
        Step s = w.Next();
        CHECK(w.c.ViewNow().hover == kNone && s.layer.beam && !s.layer.hit && Near(s.layer.len, kBeamM));
        w.AimUV(0.5f, -0.5f);
        s = w.Next();
        CHECK(w.c.ViewNow().hover == kNone && s.layer.beam && !s.layer.hit);
        // the state line and the title are nothing
        w.AimPx(800.0f, tiles::kRedLine.t + 10.0f);
        w.Next();
        CHECK(w.c.ViewNow().hover == kNone);
        // a ray that points away from the window (behind the hand) is no hit
        w.f.hand[1].aim.rot[0] = 0; w.f.hand[1].aim.rot[1] = 1; w.f.hand[1].aim.rot[2] = 0; w.f.hand[1].aim.rot[3] = 0;
        s = w.Next();
        CHECK(w.c.ViewNow().hover == kNone && !s.layer.hit);
    }
    {   // the layout at other widths: the same tiles under the same relative points (the window just scales)
        for (float width : {0.5f, 0.9f, 1.6f}) {
            World w(Style::Tiles, width);
            OpenPanel(w);
            const Step t = w.Next();
            CHECK(Near(t.layer.widthM, width) && Near(t.layer.heightM, width / tiles::kAspect));
            for (int k = 0; k < kSlotCount; ++k) {
                w.AimCell(Page::Wheel, k);
                w.Next();
                CHECK(w.c.ViewNow().hover == k);
            }
            // 3 px inside a tile's edge still hits it; the gap between two tiles of a row does not (from a clear start)
            const tiles::PRect r = tiles::SlotRect(1);
            w.AimUV(0.5f, -0.5f);
            w.Next();
            w.AimPx(r.l + 3.0f, CentreY(r));
            w.Next();
            CHECK(w.c.ViewNow().hover == 1);
            w.AimUV(0.5f, -0.5f);
            w.Next();
            w.AimPx(r.r + 0.5f * tiles::kGap, CentreY(r));
            w.Next();
            CHECK(w.c.ViewNow().hover == kNone);
        }
    }
    {   // a pick: the trigger on a tile casts its slot as the game's own pad button (LT first, alone, for the second row)
        World w;
        OpenPanel(w);
        const Step pick = w.ClickCell(Page::Wheel, 4);
        CHECK(pick.feedback == Feedback::Selected);
        CHECK(!w.c.Open() && w.c.Selected() == 4);
        for (int i = 0; i < 4; ++i) w.Next();
        bool ltAlone = false, both = false;
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 20; ++i) {
            const Step s = w.Next();
            if (s.casting && s.lt == 255 && s.buttons == 0) ltAlone = true;
            if (s.casting && s.lt == 255 && s.buttons == kPadB) both = true;
        }
        CHECK(ltAlone && both);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) w.Next();
        // the first row: the pad button alone
        OpenPanel(w);
        w.ClickCell(Page::Wheel, 1);
        CHECK(w.c.Selected() == 1);
        w.Hand(1, 0.9f, 0, 0);
        Step s;
        for (int i = 0; i < 4; ++i) s = w.Next();
        CHECK(s.casting && s.buttons == kPadX && s.lt == 0);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) w.Next();
        // LT + A: the pad's A with the left trigger first
        OpenPanel(w);
        w.ClickCell(Page::Wheel, 10);
        CHECK(w.c.Selected() == 10);
        w.Hand(1, 0.9f, 0, 0);
        ltAlone = both = false;
        for (int i = 0; i < 20; ++i) {
            s = w.Next();
            if (s.casting && s.lt == 255 && s.buttons == 0) ltAlone = true;
            if (s.casting && s.lt == 255 && s.buttons == kPadA) both = true;
        }
        CHECK(ltAlone && both);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) w.Next();
        // A: the layout's own trigger - nothing is pressed here, nothing is taken from the layout
        OpenPanel(w);
        const Step basic = w.ClickCell(Page::Wheel, 8);
        CHECK(basic.feedback == Feedback::Selected && !w.c.Open() && w.c.Selected() == -1);
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 6; ++i) {
            s = w.Next();
            CHECK(!s.casting && s.buttons == 0 && s.lt == 0);
        }
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) w.Next();
        // RT is not castable: the pick is refused, the panel stays up, nothing is selected
        OpenPanel(w);
        const Step rt = w.ClickCell(Page::Wheel, 9);
        CHECK(rt.feedback == Feedback::Refused && w.c.Open() && w.c.Selected() == -1);
    }
    {   // the editor through the controller: EDIT SKILLS, a slot, page 2 of the grid, a skill -> a request for the game's UI thread
        World w;
        OpenPanel(w);
        w.ClickCell(Page::Wheel, kEdit);
        CHECK(w.c.ViewNow().page == Page::PickSlot);
        w.ClickCell(Page::PickSlot, 9);
        CHECK(w.c.ViewNow().page == Page::PickSkill && w.c.ViewNow().options == 24 && w.c.ViewNow().editPages == 2);
        w.ClickCell(Page::PickSkill, kNext);
        CHECK(w.c.ViewNow().editPage == 1 && w.c.ViewNow().options == 6);
        const Step s = w.ClickCell(Page::PickSkill, 3);
        CHECK(s.request && s.req.op == Op::Assign && s.req.skill == 127 && s.req.slot == 9 && s.req.bank == 0);
        w.reply = Reply{s.req.sequence, Result::Applied};
        w.Next();
        CHECK(w.c.ViewNow().page == Page::PickSlot && w.c.Open());
        // CLEAR SLOT (a footer button now)
        w.ClickCell(Page::PickSlot, 0);
        const Step clear = w.ClickCell(Page::PickSkill, kCentre);
        CHECK(clear.request && clear.req.op == Op::Clear && clear.req.slot == 0);
        w.reply = Reply{clear.req.sequence, Result::Applied};
        w.Next();
        // BACK, CLOSE
        w.ClickCell(Page::PickSlot, kBack);
        CHECK(w.c.ViewNow().page == Page::Wheel);
        w.ClickCell(Page::Wheel, kClose);
        CHECK(!w.c.Open());
    }
    {   // the hint captions reach the view, and a change of them makes the picture be painted again
        World w;
        Settings cfg = w.c.Config();
        cfg.hints = tiles::BuildHints(tiles::Profile::Touch, tiles::Profile::Touch, true);
        w.c.Configure(cfg);
        OpenPanel(w);
        w.Next();
        CHECK(std::strcmp(w.c.ViewNow().hints.text[0], "Right B") == 0 && std::strcmp(w.c.ViewNow().hints.text[6], "LT + Left Y") == 0);
        const uint32_t rev = w.c.ViewNow().revision;
        w.Next();
        CHECK(w.c.ViewNow().revision == rev);   // (nothing changed: not painted again)
        cfg.hints = tiles::BuildHints(tiles::Profile::Index, tiles::Profile::Index, true);
        w.c.Configure(cfg);
        w.Next();
        CHECK(w.c.ViewNow().revision != rev && std::strcmp(w.c.ViewNow().hints.text[1], "Left A") == 0);
        cfg.hints = Hints{};
        w.c.Configure(cfg);
        w.Next();
        CHECK(!w.c.ViewNow().hints.text[0][0]);
    }
}

// -- VR first person only (F4) -------------------------------------------------------------------------------------------------------------

static void TestF4Gate() {
    {   // not in F4 from the start: the chord does nothing, and its click is not taken from the layout (the layout behaves as it did)
        World w;
        w.f4 = false;
        w.Hand(0, 0, 0.9f, 0);
        Step s = w.Next();
        CHECK(!w.c.Open() && s.take[0] == 0 && s.take[1] == 0 && !s.casting && !s.layer.show);
        w.Hand(1, 0, 0, di::kStick);
        s = w.Next();
        CHECK(!w.c.Open() && s.take[0] == 0 && s.take[1] == 0 && !s.layer.show && !s.request);
        CHECK(s.feedback == Feedback::None && s.buttons == 0 && s.lt == 0);
        // and the same click with the grip held in F4 is the chord's
        World g;
        g.Hand(0, 0, 0.9f, 0);
        s = g.Next();
        CHECK((s.take[1] & di::kStick) != 0);
    }
    {   // F4 -> another view with the panel up: it closes, nothing is taken, no press, no layer, the pick is forgotten; F4 again starts fresh
        World w;
        OpenPanel(w);
        CHECK(w.c.Open());
        w.ClickCell(Page::Wheel, 1);   // (slot X picked, the panel closed)
        CHECK(w.c.Selected() == 1);
        OpenPanel(w);
        CHECK(w.c.Open());
        w.AimPx(800.0f, 60.0f);                              // (at the title: a click there picks nothing)
        w.Hand(1, 0.9f, 0.9f, di::kPrimary | di::kStick);   // a trigger, a grip and buttons held on the panel
        w.Next();
        CHECK(w.c.Open());
        w.f4 = false;
        const Step s = w.Next();
        CHECK(!w.c.Open() && !s.layer.show && !s.layer.beam && s.take[0] == 0 && s.take[1] == 0 && !s.casting && s.buttons == 0 && s.lt == 0);
        CHECK(w.c.Selected() == -1 && s.feedback == Feedback::None);
        for (int i = 0; i < 10; ++i) {
            const Step t = w.Next();
            CHECK(!w.c.Open() && t.take[0] == 0 && t.take[1] == 0 && !t.casting && !t.layer.show);
        }
        w.f4 = true;
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        const Step back = w.Next();
        CHECK(!w.c.Open() && !back.layer.show && back.take[0] == 0 && back.take[1] == 0 && !back.casting);
        OpenPanel(w);
        CHECK(w.c.Open());
    }
    {   // a picked slot is cast in F4 and never in another view
        World w;
        OpenPanel(w);
        w.ClickCell(Page::Wheel, 0);
        CHECK(w.c.Selected() == 0);
        w.Hand(1, 0.9f, 0, 0);
        Step s;
        for (int i = 0; i < 4; ++i) s = w.Next();
        CHECK(s.casting && s.buttons == kPadB && (s.take[1] & di::kTakeTrigger));
        w.f4 = false;   // the trigger is still held: the press ends at once, the trigger is the layout's again
        s = w.Next();
        CHECK(!s.casting && s.buttons == 0 && s.lt == 0 && s.take[0] == 0 && s.take[1] == 0);
        for (int i = 0; i < 20; ++i) {
            s = w.Next();
            CHECK(!s.casting && s.buttons == 0 && s.take[1] == 0);
        }
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 20; ++i) {
            s = w.Next();
            CHECK(!s.casting && s.take[1] == 0);
        }
        // back in F4: the pick was forgotten with the view, so the trigger is the layout's attack (nothing of ours)
        w.f4 = true;
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 6; ++i) {
            s = w.Next();
            CHECK(!s.casting && s.buttons == 0 && w.c.Selected() == -1);
        }
    }
    {   // the long press's request is ignored outside F4 (and does not wait for the return)
        World w(Style::Tiles, 0.9f, 30, 1);
        w.f4 = false;
        w.request = true;
        Step s = w.Next();
        CHECK(!w.c.Open() && !s.layer.show && s.take[1] == 0);
        w.f4 = true;
        s = w.Next();
        CHECK(!w.c.Open());
    }
    {   // the pure controller's default (no view given) is F4: the tests above and the wheel's own run as they did
        World w;
        w.Hand(0, 0, 0.9f, 0);
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        const Step s = w.c.Frame(w.f, w.snap, w.now, w.reply);   // (the four-argument call)
        CHECK(w.c.Open() && s.layer.show);
    }
}

// -- the long press that opens and closes the panel --------------------------------------------------------------------------------------

static void TestLongPress() {
    {   // mode 1: the request opens it, ahead of the head; a second request closes it; the chord does nothing while direct input feeds the game
        World w(Style::Tiles, 0.9f, 30, 1);
        w.Hand(0, 0, 0.9f, 0);                       // the left grip
        w.Next();
        w.Hand(1, 0, 0, di::kStick);                 // + the click: the chord is not the way in here
        Step s = w.Next();
        CHECK(!w.c.Open() && (s.take[1] & di::kStick) == 0);   // and its click is not taken: it stays the layout's
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.request = true;
        s = w.Next();
        CHECK(w.c.Open() && s.feedback == Feedback::Opened && s.layer.show);
        const Pose p = w.c.PanelPose();
        CHECK(Near(p.p.y, 1.6f - 0.15f) && Near(p.p.z, -0.8f));
        w.request = true;
        s = w.Next();
        CHECK(!w.c.Open() && s.feedback == Feedback::Closed);
        // a request where it cannot open: a menu, no head, no focus; and the request is spent either way
        World m(Style::Tiles, 0.9f, 30, 1);
        m.f.flags |= di::kInMenu;
        m.request = true;
        m.Next();
        CHECK(!m.c.Open());
        m.f.flags &= ~di::kInMenu;
        m.Next();
        CHECK(!m.c.Open());
        World n(Style::Tiles, 0.9f, 30, 1);
        n.f.head.valid = 0;
        n.request = true;
        n.Next();
        CHECK(!n.c.Open());
        World held(Style::Tiles, 0.9f, 30, 1);
        held.f.head.valid = 2;   // a held place is not a place to hang a panel
        held.request = true;
        held.Next();
        CHECK(!held.c.Open());
        World nf(Style::Tiles, 0.9f, 30, 1);
        nf.f.flags = di::kDirect;   // no focus
        nf.request = true;
        nf.Next();
        CHECK(!nf.c.Open());
    }
    {   // mode 0: the request is ignored, the chord is the way in (the original)
        World w(Style::Tiles, 0.9f, 30, 0);
        w.request = true;
        w.Next();
        CHECK(!w.c.Open());
        OpenPanel(w);
        CHECK(w.c.Open());
    }
    {   // mode 1 without direct mode (BodyWalk's input): the long press cannot come, so the chord still opens it
        World w(Style::Tiles, 0.9f, 30, 1);
        w.f.flags = di::kFocused;
        OpenPanel(w);
        CHECK(w.c.Open());
    }
    {   // closing by long press: the press that opened it does not close it; a fresh long press does; a short click does not
        World w(Style::Tiles, 0.9f, 30, 1);
        w.Hand(1, 0, 0, di::kStick);      // the press that is still down when the request arrives
        w.request = true;
        Step s = w.Next();
        CHECK(w.c.Open() && (s.take[1] & di::kStick));
        for (int i = 0; i < 100; ++i) {   // held 1.1 s more: still open (it is the opening press)
            s = w.Next();
            CHECK(w.c.Open());
        }
        w.Hand(1, 0, 0, 0);
        w.Next();
        // a short click (0.2 s) does not close it
        w.Hand(1, 0, 0, di::kStick);
        for (int i = 0; i < 18; ++i) w.Next();
        w.Hand(1, 0, 0, 0);
        w.Next();
        CHECK(w.c.Open());
        // a long one (the default 0.5 s) does
        w.Hand(1, 0, 0, di::kStick);
        int closedAt = -1;
        for (int i = 0; i < 80 && closedAt < 0; ++i) {
            s = w.Next();
            if (!w.c.Open()) closedAt = i;
        }
        CHECK(closedAt > 35 && closedAt < 55 && s.feedback == Feedback::Closed);   // (about 0.5 s of 11 ms frames)
        // the click that closed it is still down: still the panel's, not the layout's, until it is let go; the next press is the layout's
        CHECK((s.take[1] & di::kStick) != 0);
        s = w.Next();
        CHECK((s.take[1] & di::kStick) != 0 && !w.c.Open());
        w.Hand(1, 0, 0, 0);
        s = w.Next();
        CHECK(s.take[1] == 0);
        // a long press does not open it again by itself in the same hold
        w.Hand(1, 0, 0, di::kStick);
        for (int i = 0; i < 100; ++i) {
            s = w.Next();
            CHECK(!w.c.Open());
        }
    }
    {   // a different hold time ([direct] hold_ms): the same time closes as opens
        World w(Style::Tiles, 0.9f, 30, 1);
        Settings cfg = w.c.Config();
        cfg.holdS = 1.0f;
        w.c.Configure(cfg);
        w.request = true;
        w.Next();
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        for (int i = 0; i < 70; ++i) w.Next();   // 0.77 s: not yet
        CHECK(w.c.Open());
        for (int i = 0; i < 40; ++i) w.Next();
        CHECK(!w.c.Open());
    }
    {   // with the left hand pointing (the right stick is the other hand's): the long press of the right stick still closes it
        World w(Style::Tiles, 0.9f, 30, 1);
        Settings cfg = w.c.Config();
        cfg.pointerHand = 0;
        w.c.Configure(cfg);
        w.request = true;
        w.Next();
        CHECK(w.c.Open());
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        for (int i = 0; i < 60; ++i) w.Next();
        CHECK(!w.c.Open());
    }
    {   // in mode 0 a long press of the right stick is nothing to the panel
        World w(Style::Tiles, 0.9f, 30, 0);
        OpenPanel(w);
        CHECK(w.c.Open());
        w.Hand(1, 0, 0, di::kStick);
        for (int i = 0; i < 100; ++i) w.Next();
        CHECK(w.c.Open());   // (a long press of the right click is nothing to the panel in this mode: only the chord toggles it)
    }
}

// -- which slots the trigger can ever press ----------------------------------------------------------------------------------------------

static void TestCastGateSlots() {
    for (int slot = 0; slot < kSlotCount; ++slot) {
        CastGate g;
        CastGate::In in;
        in.slot = slot;
        in.allowed = true;
        in.time = 1.0;
        in.trigger = 0.0f;
        g.Step(in);   // (armed: the trigger was up)
        bool pressed = false, ltSeen = false;
        uint16_t button = 0;
        in.trigger = 0.9f;
        for (int i = 0; i < 40; ++i) {
            in.time += 0.011;
            const CastGate::Out o = g.Step(in);
            if (o.casting) pressed = true;
            if (o.lt) ltSeen = true;
            if (o.buttons) button = o.buttons;
        }
        const bool pad = kSlotDefs[slot].cast == Cast::Pad;
        CHECK(pressed == pad);                                                   // only a pad slot is ever pressed (A, RT, LT + RT are not)
        CHECK(!pad || (button == kSlotDefs[slot].button && ltSeen == kSlotDefs[slot].alt));
        CHECK(pad || (button == 0 && !ltSeen));
    }
    CHECK(kSlotDefs[8].cast == Cast::Layout && kSlotDefs[9].cast == Cast::None && kSlotDefs[10].cast == Cast::Pad && kSlotDefs[11].cast == Cast::None);
    for (int s = 0; s < 8; ++s) CHECK(kSlotDefs[s].cast == Cast::Pad);           // the wheel's eight, as ever
    CHECK(BaseSlot(0) == 0 && BaseSlot(3) == 3 && BaseSlot(4) == 0 && BaseSlot(7) == 3 && BaseSlot(8) == 8 && BaseSlot(9) == 9 && BaseSlot(10) == 8 && BaseSlot(11) == 9);
}

// -- the strip when the trees are not known at the editor's opening: a visible note, and the strip when they are known a moment later -------------

static void TestStripLateTrees() {
    Rig r;
    r.categories = true;
    r.snap = TreeSnap(30);
    r.snap.treesKnown = false;                                      // (the snapshot does not know the trees yet)
    for (int i = 0; i < 30; ++i) r.snap.catalog[i].tree = wheel::kTreeUnknown;
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.Point(Page::PickSlot, 3, true);
    const View& v = r.m.Current();
    CHECK(v.page == Page::PickSkill && !v.catStrip && v.catWanted && v.options == 24);   // no strip - and the painter is told to say why
    std::vector<uint32_t> note, bare;
    {
        View without = v;
        without.catWanted = false;
        tilespaint::Render(v, note);
        tilespaint::Render(without, bare);
        CHECK(note != bare);
        int lit = 0;   // the note is letters in the strip's band
        for (int y = int(tiles::kStripTop); y < int(tiles::kStripBottom); ++y)
            for (int x = int(tiles::kStripLeft); x < tiles::kW - int(tiles::kStripLeft); ++x) {
                const uint32_t px = note[(size_t)y * tiles::kW + x];
                if (((px >> 0) & 255) > 110 && ((px >> 8) & 255) > 110) ++lit;
            }
        CHECK(lit > 150);
    }
    // the trees become known while the grid is up: the strip comes up there, All shown, the counts in
    r.snap.treesKnown = true;
    r.snap.playerClass = 1;
    for (int i = 0; i < 30; ++i) r.snap.catalog[i].tree = i < 10 ? 1 : i < 12 ? 2 : 0;
    r.Step();
    CHECK(v.page == Page::PickSkill && v.catStrip && !v.catWanted && v.category == wheel::kCatAll && v.options == 24);
    CHECK(v.catCount[wheel::kCatAll] == 30 && v.catCount[wheel::kCatTree1] == 10 && v.catCount[wheel::kCatTree2] == 2 && v.catCount[wheel::kCatGeneral] == 18);
    r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatTree1), false);
    const Out o = r.Point(Page::PickSkill, wheel::CategoryCell(wheel::kCatTree1), true);
    CHECK(o.feedback == Feedback::Hover && v.category == wheel::kCatTree1 && v.options == 10);
    // the key off: no strip and no note, whatever is known
    Rig off;
    off.categories = false;
    off.snap = TreeSnap(30);
    off.Step(true);
    off.Point(Page::Wheel, kEdit, true);
    off.Point(Page::PickSlot, 3, true);
    CHECK(off.m.Current().page == Page::PickSkill && !off.m.Current().catStrip && !off.m.Current().catWanted);
    off.snap.treesKnown = false;
    off.Step();
    CHECK(!off.m.Current().catStrip && !off.m.Current().catWanted);
    // a strip that is there stays there when the trees are lost for a moment (the editor does not drop it under the player's hand)
    Rig keep;
    keep.categories = true;
    keep.snap = TreeSnap(30);
    keep.Step(true);
    keep.Point(Page::Wheel, kEdit, true);
    keep.Point(Page::PickSlot, 3, true);
    CHECK(keep.m.Current().catStrip);
    keep.snap.treesKnown = false;
    keep.Step();
    CHECK(keep.m.Current().catStrip && !keep.m.Current().catWanted);
}

// -- the category strip through the whole controller (not only the model): what the live game does -----------------------------------------------

static void WriteBmp(const char* dir, const char* name, const std::vector<uint32_t>& px, int w, int h);

static void TestControllerStrip() {
    World w;
    w.snap.treesKnown = true;
    for (int i = 0; i < 30; ++i) w.snap.catalog[i].tree = int8_t(i % 4);
    OpenPanel(w);
    CHECK(w.c.Open());
    w.ClickCell(Page::Wheel, kEdit);
    CHECK(w.c.ViewNow().page == Page::PickSlot);
    w.ClickCell(Page::PickSlot, 2);
    CHECK(w.c.ViewNow().page == Page::PickSkill);
    std::printf("  the strip through the controller: catStrip=%d category=%d counts=%d/%d/%d/%d/%d\n", (int)w.c.ViewNow().catStrip, w.c.ViewNow().category,
                w.c.ViewNow().catCount[0], w.c.ViewNow().catCount[1], w.c.ViewNow().catCount[2], w.c.ViewNow().catCount[3], w.c.ViewNow().catCount[4]);
    CHECK(w.c.ViewNow().catStrip);
    {   // the same with the Settings tab on (the user's ini: headset_settings=1 with the tiles' look): the tab strip must not take the category strip
        World t;
        Settings cfg = t.c.Config();
        cfg.settingsTab = true;
        t.c.Configure(cfg);
        SettingsIn hostIn;
        t.host = &hostIn;
        t.snap.treesKnown = true;
        for (int i = 0; i < 30; ++i) t.snap.catalog[i].tree = int8_t(i % 4);
        OpenPanel(t);
        CHECK(t.c.Open() && t.c.ViewNow().tabs);
        t.ClickCell(Page::Wheel, kEdit);
        CHECK(t.c.ViewNow().page == Page::PickSlot);
        t.ClickCell(Page::PickSlot, 2);
        std::printf("  the strip with the Settings tab: page=%d catStrip=%d tabs=%d\n", (int)t.c.ViewNow().page, (int)t.c.ViewNow().catStrip, (int)t.c.ViewNow().tabs);
        CHECK(t.c.ViewNow().page == Page::PickSkill && t.c.ViewNow().catStrip);
    }
    if (const char* dir = std::getenv("SKILL_STRIP_DUMP")) {   // (a look at the picture the live window would be painted from)
        std::vector<uint32_t> px;
        wheelpaint::Render(w.c.ViewNow(), px);
        WriteBmp(dir, "strip_through_controller", px, tiles::kW, tiles::kH);
    }
    // and the picture of that very view, through the painter's worker as the live game's draw thread asks for it, has the strip in it
    View shown = w.c.ViewNow();
    View without = shown;
    without.catStrip = false;
    std::vector<uint32_t> a, b;
    wheelpaint::Render(shown, a);
    wheelpaint::Render(without, b);
    CHECK(a != b);
    wheelpaint::Async worker;
    shown.revision += 100;
    worker.Submit(shown);
    std::vector<uint32_t> got;
    uint32_t rev = 0;
    int gw = 0, gh = 0;
    bool have = false;
    for (int i = 0; i < 400 && !have; ++i) { have = worker.Fetch(got, &rev, &gw, &gh); if (!have) Sleep(10); }
    CHECK(have && rev == shown.revision && got == a);
    worker.Stop();
}

// -- the player's own icon cache, if SKILL_STRIP_CACHE names one: does it give the trees the strip needs? ---------------------------------------

static void TestUserCache() {
    char dir[400] = {};
    if (!GetEnvironmentVariableA("SKILL_STRIP_CACHE", dir, sizeof dir)) return;
    wchar_t wide[400] = {};
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wide, 399);
    wheelicons::Shutdown();
    wheelicons::SetDirectory(wide);
    std::printf("  cache %s: icons %d, trees %d\n", dir, (int)wheelicons::Available(), (int)wheelicons::HasTrees());
    for (int cls = 0; cls < 8; ++cls)
        std::printf("    class %d: %s | %s | %s\n", cls, wheelicons::TreeKey(cls, 1).c_str(), wheelicons::TreeKey(cls, 2).c_str(), wheelicons::TreeKey(cls, 3).c_str());
    for (int id : {126, 133, 151, 357, 359}) {
        int cls = -9, page = -9;
        const bool have = wheelicons::SkillTree(id, &cls, &page);
        std::printf("    skill %d: row %d class %d page %d\n", id, (int)have, cls, page);
    }
    wheelicons::Shutdown();
}

// -- the look changing while the panel is up -----------------------------------------------------------------------------------

static void SetStyle(World& w, Style st) {
    Settings cfg = w.c.Config();
    cfg.style = st;
    w.c.Configure(cfg);
}

static void TestStyleChange() {
    {   // wheel -> tiles with the wheel up: it stays up, on its first page, in the same place, as the window
        World w(Style::Wheel);
        OpenPanel(w);
        w.Next();
        const Pose before = w.c.PanelPose();
        const uint32_t rev = w.c.ViewNow().revision;
        SetStyle(w, Style::Tiles);
        const Step s = w.Next();
        CHECK(w.c.Open() && w.c.ViewNow().style == Style::Tiles && w.c.ViewNow().page == Page::Wheel);
        CHECK(s.layer.show && s.layer.style == Style::Tiles && s.layer.heightM < s.layer.widthM && Near(s.layer.widthM, 0.9f));
        const Pose after = w.c.PanelPose();
        CHECK(Near(before.p.x, after.p.x) && Near(before.p.y, after.p.y) && Near(before.p.z, after.p.z));   // (not hung again)
        CHECK(w.c.ViewNow().revision != rev);                                                                // (painted in the new look)
        w.AimCell(Page::Wheel, 5);                                                                           // the tiles answer to rays now
        w.Next();
        CHECK(w.c.ViewNow().hover == 5);
        SetStyle(w, Style::Wheel);                                                                           // and back
        w.Next();
        CHECK(w.c.Open() && w.c.ViewNow().style == Style::Wheel && Near(w.c.PanelPose().p.y, after.p.y));
        float u, v;
        WedgePoint(6, 0.27f, &u, &v);
        w.AimUV(u, v);
        w.Next();
        CHECK(w.c.ViewNow().hover == 6);
    }
    {   // a trigger pulled in the very frame the look changes is not a click on the new look; the next one is
        World w(Style::Wheel);
        OpenPanel(w);
        SetStyle(w, Style::Tiles);
        w.AimCell(Page::Wheel, 1);   // (the ray is on a tile of the new look)
        w.Hand(1, 0.9f, 0, 0);
        const Step s = w.Next();
        CHECK(w.c.Open() && w.c.Selected() == -1 && s.feedback != Feedback::Selected);
        w.Hand(1, 0, 0, 0);
        w.Next();
        const Step pick = w.ClickCell(Page::Wheel, 1);
        CHECK(pick.feedback == Feedback::Selected && w.c.Selected() == 1);
    }
    {   // tiles -> wheel while the grid is on its second page: the wheel's first page, nothing half-done; the editor works in the new look
        World w(Style::Tiles);
        OpenPanel(w);
        w.ClickCell(Page::Wheel, kEdit);
        w.ClickCell(Page::PickSlot, 1);
        w.ClickCell(Page::PickSkill, kNext);
        CHECK(w.c.ViewNow().page == Page::PickSkill && w.c.ViewNow().editPage == 1);
        SetStyle(w, Style::Wheel);
        w.Next();
        const View& v = w.c.ViewNow();
        CHECK(w.c.Open() && v.page == Page::Wheel && v.editSlot == -1 && v.editPage == 0 && v.style == Style::Wheel);
        const Rect e = FooterRect(Page::Wheel, 0);
        w.AimUV(0.5f * (e.l + e.r), 0.5f * (e.t + e.b));
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        w.Next();
        w.Hand(1, 0, 0, 0);
        w.Next();
        CHECK(w.c.ViewNow().page == Page::PickSlot);
        float u, v2;
        WedgePoint(1, 0.27f, &u, &v2);
        w.AimUV(u, v2);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        w.Next();
        w.Hand(1, 0, 0, 0);
        w.Next();
        CHECK(w.c.ViewNow().page == Page::PickSkill && w.c.ViewNow().options == 8 && w.c.ViewNow().editPages == 4);   // (30 skills, 8 a page)
    }
    {   // a request in flight when the look changes: its answer is still taken
        World w(Style::Tiles);
        OpenPanel(w);
        w.ClickCell(Page::Wheel, kEdit);
        w.ClickCell(Page::PickSlot, 0);
        const Step s = w.ClickCell(Page::PickSkill, 2);
        CHECK(s.request && s.req.op == Op::Assign && s.req.slot == 0 && s.req.skill == 102);
        SetStyle(w, Style::Wheel);
        w.Next();
        w.reply = Reply{s.req.sequence, Result::Applied};
        const Step done = w.Next();
        CHECK(done.feedback == Feedback::Applied && w.c.ViewNow().page == Page::PickSlot && w.c.Open() && w.c.ViewNow().style == Style::Wheel);
    }
    {   // closed: the look changes quietly, the pick stays, the next opening is in the new look and hung for it
        World w(Style::Tiles);
        OpenPanel(w);
        w.ClickCell(Page::Wheel, 0);
        CHECK(!w.c.Open() && w.c.Selected() == 0);
        SetStyle(w, Style::Wheel);
        w.Next();
        CHECK(!w.c.Open() && w.c.Selected() == 0 && w.c.ViewNow().style == Style::Wheel);
        OpenPanel(w);
        CHECK(w.c.Open() && Near(w.c.PanelPose().p.y, 1.6f - 0.08f) && w.c.Selected() == 0);
        w.Hand(0, 0, 0.9f, 0);   // the chord closes it
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        w.Next();
        CHECK(!w.c.Open());
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Next();
        SetStyle(w, Style::Tiles);
        OpenPanel(w);
        CHECK(w.c.Open() && w.c.ViewNow().style == Style::Tiles && Near(w.c.PanelPose().p.y, 1.6f - 0.15f) && w.c.Selected() == 0);
    }
}

// -- a fuzz: random frames, the look flipping, the view leaving F4, the requests, invariants only ------------------------------------

static void TestFuzz() {
    uint32_t seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int run = 0; run < 4; ++run) {
        World w(run == 1 ? Style::Wheel : Style::Tiles, 0.6f + 0.3f * run, 5 + 20 * run, run & 1);
        int opened = 0, requests = 0, hovered = 0, casts = 0;
        for (int i = 0; i < 6000; ++i) {
            if (rnd() % 400 == 0) SetStyle(w, w.c.Config().style == Style::Tiles ? Style::Wheel : Style::Tiles);
            if (rnd() % 300 == 0) { Settings cfg = w.c.Config(); cfg.tilesWidthM = 0.5f + (rnd() % 20) * 0.1f; w.c.Configure(cfg); }
            if (rnd() % 500 == 0) w.f4 = !w.f4;
            if (rnd() % 90 == 0) w.request = true;
            if (rnd() % 60 == 0) w.Hand(0, 0, (rnd() & 1) ? 0.9f : 0.0f, 0);
            w.Hand(1, (rnd() % 5) == 0 ? 0.9f : 0.0f, 0, (rnd() % 40 == 0) ? di::kStick : 0u);
            if (rnd() % 7 == 0) w.AimUV((rnd() % 1400) / 1000.0f - 0.2f, (rnd() % 1400) / 1000.0f - 0.2f);
            if (rnd() % 300 == 0) { w.f.flags = di::kDirect | ((rnd() & 1) ? di::kFocused : 0u) | ((rnd() & 3) == 0 ? di::kInMenu : 0u); ++w.f.epoch; }
            const Step s = w.Next();
            if (w.c.Open()) ++opened;
            if (s.casting) ++casts;
            if (s.request) {
                ++requests;
                CHECK(s.req.slot >= 0 && s.req.slot < kSlotCount && (s.req.op == Op::Assign || s.req.op == Op::Clear));
                w.reply = Reply{s.req.sequence, (rnd() & 1) ? Result::Applied : Result::Refused};
            }
            const View& v = w.c.ViewNow();
            CHECK(std::isfinite(s.layer.len) && std::isfinite(s.layer.panel.p.x) && std::isfinite(s.layer.heightM) && s.layer.heightM > 0.0f);
            CHECK(w.c.Selected() >= -1 && w.c.Selected() < kSlotCount);
            CHECK(v.hover >= -1 && v.hover <= kRunWalk);
            CHECK(v.options >= 0 && v.options <= (v.style == Style::Tiles ? tiles::kGridCount : kOptionsPerPage));
            CHECK(v.editPage >= 0 && v.editPage < v.editPages);
            if (v.hover != kNone) ++hovered;
            if (s.layer.show) CHECK(s.layer.style == v.style);
            if (!w.f4) CHECK(!w.c.Open() && !s.layer.show && !s.casting && s.take[0] == 0 && s.take[1] == 0 && !s.request);   // (nothing of ours outside F4)
            if (s.casting) CHECK(w.c.Selected() >= 0 && kSlotDefs[w.c.Selected()].cast == Cast::Pad);   // (only a pad slot is ever pressed)
        }
        std::printf("  fuzz run %d: %d frames with the panel up, %d hovers, %d requests, %d casting frames\n", run, opened, hovered, requests, casts);
        CHECK(opened > 0);
    }
}

// -- the picture ---------------------------------------------------------------------------------------------------------------------

static uint8_t Chan(uint32_t p, int c) { return (uint8_t)(p >> (8 * c)); }

// A view with the realistic skills of a sorceress (the ids are the game's own, so a local icon cache shows the right icons): A has
// Fire Bolt's neighbour, X Fire Ball, B Fire Bolt, Y empty, RB a skill that is gone, RT Nova; LT: Frozen Armor, Ice Blast, ...
static View SampleView(Page page, int hover, int selected, int catalog = 26) {
    static const struct { int id; const char* name; } kCatalog[] = {
        {kSkillInteract, "Interact / pick up"}, {36, "Fire Bolt"}, {38, "Charged Bolt"}, {39, "Ice Bolt"}, {40, "Frozen Armor"}, {41, "Inferno"},
        {42, "Static Field"}, {43, "Telekinesis"}, {44, "Frost Nova"}, {45, "Ice Blast"}, {46, "Blaze"}, {47, "Fire Ball"}, {48, "Nova"},
        {49, "Lightning"}, {50, "Shiver Armor"}, {51, "Fire Wall"}, {52, "Enchant"}, {53, "Chain Lightning"}, {54, "Teleport"},
        {55, "Glacial Spike"}, {56, "Meteor"}, {57, "Thunder Storm"}, {58, "Energy Shield"}, {59, "Blizzard"}, {62, "Hydra"},
        {kSkillTownPortal, "Town Portal"}};
    View v;
    v.style = Style::Tiles;
    v.open = true; v.page = page; v.hover = hover; v.selected = selected;
    v.ready = v.editable = true; v.bank = 0;
    v.slot[0] = Sk(36, "Fire Bolt");        // B
    v.slot[1] = Sk(47, "Fire Ball");        // X
    v.slot[2] = Skill{}; v.slot[2].known = true;   // Y: empty
    v.slot[3] = Sk(54, "Teleport", false);  // RB: a skill the player does not have now
    v.slot[4] = Sk(40, "Frozen Armor");
    v.slot[5] = Sk(45, "Ice Blast");
    v.slot[6] = Sk(kSkillTownPortal, "Town Portal", true, 0);
    v.slot[7] = Sk(56, "Meteor");
    v.slot[8] = Sk(kSkillInteract, "Interact");   // A
    v.slot[9] = Sk(48, "Nova");                   // RT
    v.slot[10] = Sk(38, "Charged Bolt");
    v.slot[11] = Skill{}; v.slot[11].known = true;
    v.editSlot = page == Page::PickSkill ? 5 : -1;
    v.editPage = 0; v.editPages = 2;
    v.options = std::min(catalog, tiles::kGridCount);
    for (int i = 0; i < v.options; ++i) v.option[i] = Sk(kCatalog[i % 26].id, kCatalog[i % 26].name, true, 1 + (i * 7) % 20);
    v.revision = 1;
    return v;
}

static void WriteBmp(const char* dir, const char* name, const std::vector<uint32_t>& px, int w, int h) {
    // over a dark dungeon-ish stone backdrop with a faint grain (so a transparent corner is visible); premultiplied: out = px + bg * (1 - a)
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = px[(size_t)y * w + x];
            const float a = Chan(p, 3) / 255.0f, g = 52.0f + float(((x * 7 + y * 13) ^ (x / 3 * 5 + y / 3)) & 15);
            const float bg[3] = {g * 0.92f, g * 0.95f, g * 1.05f};
            for (int c = 0; c < 3; ++c) rgb[((size_t)y * w + x) * 3 + c] = (uint8_t)std::min(255.0f, Chan(p, c) + bg[c] * (1.0f - a) + 0.5f);
        }
    char path[400];
    std::snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") || !f) return;
    const uint32_t rowBytes = (uint32_t)(w * 3 + 3) & ~3u, size = 54 + rowBytes * h;
    uint8_t head[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { std::memcpy(head + at, &v, 4); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)h);
    head[26] = 1; head[28] = 24;
    put32(34, rowBytes * h);
    std::fwrite(head, 1, 54, f);
    std::vector<uint8_t> row(rowBytes, 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* s = &rgb[((size_t)y * w + x) * 3];
            row[x * 3 + 0] = s[2]; row[x * 3 + 1] = s[1]; row[x * 3 + 2] = s[0];
        }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
}

static bool Differ(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) { return a.size() != b.size() || a != b; }

static void TestPaint(const char* dumpDir) {
    const int W = tiles::kW, H = tiles::kH;
    int sw = 0, sh = 0;
    wheelpaint::SizeOf(Style::Tiles, &sw, &sh);
    CHECK(sw == W && sh == H);
    wheelpaint::SizeOf(Style::Wheel, &sw, &sh);
    CHECK(sw == wheelpaint::kSize && sh == wheelpaint::kSize);
    tilespaint::Size(&sw, &sh);
    CHECK(sw == W && sh == H);

    std::vector<uint32_t> main, again;
    wheelpaint::Render(SampleView(Page::Wheel, 1, 4), main);   // (the wheel's entry point, the tiles' look by the view's style)
    CHECK(main.size() == (size_t)W * H);
    tilespaint::Render(SampleView(Page::Wheel, 1, 4), again);
    CHECK(main == again);   // (deterministic, and one painter behind both doors)
    auto at = [&](const std::vector<uint32_t>& px, float x, float y) { return px[(size_t)y * W + (size_t)x]; };
    // the window: clear outside its cut corners, opaque inside, premultiplied
    CHECK(Chan(main[0], 3) == 0 && Chan(main[W - 1], 3) == 0);
    CHECK(Chan(main[(size_t)(H - 1) * W], 3) == 0 && Chan(main[(size_t)H * W - 1], 3) == 0);
    CHECK(Chan(at(main, 800, 560), 3) >= 240 && Chan(at(main, 800, 1050), 3) >= 240 && Chan(at(main, 60, 500), 3) >= 240);
    bool ok = true;
    for (size_t i = 0; i < main.size() && ok; i += 5)
        ok = Chan(main[i], 0) <= Chan(main[i], 3) + 1 && Chan(main[i], 1) <= Chan(main[i], 3) + 1 && Chan(main[i], 2) <= Chan(main[i], 3) + 1;
    CHECK(ok);
    CHECK(Chan(at(main, 8, 8), 3) == 0 && Chan(at(main, 60, 60), 3) > 200);                     // the chamfered corner
    // the panel is near black, the frame is a lit grey metal
    const uint32_t panel = at(main, 800, 130 + 20), frame = at(main, 800, 15);
    CHECK(Chan(panel, 0) < 30 && Chan(panel, 1) < 30 && Chan(panel, 2) < 30);
    CHECK(Chan(frame, 0) > 25 && Chan(frame, 0) < 200 && std::abs(int(Chan(frame, 0)) - int(Chan(frame, 2))) < 24);   // (grey: no colour cast)

    // the plates: a slot that holds a skill is RED, an empty one stays COPPER, one that is not known is a dull plate
    auto faceOf = [&](const std::vector<uint32_t>& px, int slot) {   // a pixel of the plate's face (between the rivets, above the picture's middle)
        const tiles::PRect r = tiles::SlotRect(slot);
        return at(px, r.l + 0.5f * tiles::kTile, r.t + 26.0f);
    };
    const uint32_t redFace = faceOf(main, 0), copperFace = faceOf(main, 2), goneFace = faceOf(main, 3);
    CHECK(Chan(redFace, 0) > 2 * Chan(redFace, 1) && Chan(redFace, 0) > 90);                         // red
    CHECK(Chan(copperFace, 0) < 2 * Chan(copperFace, 1) && Chan(copperFace, 0) > Chan(copperFace, 1) && Chan(copperFace, 1) > Chan(copperFace, 2) && Chan(copperFace, 0) > 70);   // copper-brown
    CHECK(Chan(goneFace, 0) < Chan(redFace, 0) - 30);                                                // a skill the player does not have: the red dimmed
    // a rivet in each corner: a lit point against the dark inner bevel
    const tiles::PRect t0 = tiles::SlotRect(2);
    for (float fx : {t0.l + 17.0f, t0.r - 17.0f})
        for (float fy : {t0.t + 17.0f, t0.b - 17.0f}) {
            const uint32_t dome = at(main, fx - 2.0f, fy - 2.0f), ring = at(main, fx + 9.5f, fy);
            CHECK(Chan(dome, 0) > Chan(ring, 0) + 15);
        }
    // under the beam: a bright rim; the picked slot: a gold frame; both absent from a tile that is neither
    std::vector<uint32_t> hotX, hotB, pickRb, plain;
    wheelpaint::Render(SampleView(Page::Wheel, 1, -1), hotX);
    wheelpaint::Render(SampleView(Page::Wheel, 0, -1), hotB);
    wheelpaint::Render(SampleView(Page::Wheel, -1, 3), pickRb);
    wheelpaint::Render(SampleView(Page::Wheel, -1, 5), plain);
    const tiles::PRect tx = tiles::SlotRect(1), tr = tiles::SlotRect(3);
    CHECK(Chan(at(hotX, tx.l + 0.5f, CentreY(tx)), 0) > Chan(at(hotB, tx.l + 0.5f, CentreY(tx)), 0) + 120);
    CHECK(Chan(at(hotB, tiles::SlotRect(0).l + 0.5f, CentreY(tiles::SlotRect(0))), 0) > Chan(at(hotX, tiles::SlotRect(0).l + 0.5f, CentreY(tiles::SlotRect(0))), 0) + 120);
    CHECK(Chan(at(pickRb, tr.l - 1.5f, CentreY(tr)), 0) > 200 && Chan(at(pickRb, tr.l - 1.5f, CentreY(tr)), 1) > 150 && Chan(at(pickRb, tr.l - 1.5f, CentreY(tr)), 2) < 140);   // gold
    CHECK(Chan(at(plain, tr.l - 1.5f, CentreY(tr)), 0) < 60);
    // A is the picked slot while nothing else is (the trigger is the layout's): the same gold frame on its tile; not once another is picked
    const tiles::PRect ta = tiles::SlotRect(8);
    std::vector<uint32_t> aActive;
    wheelpaint::Render(SampleView(Page::Wheel, -1, -1), aActive);
    CHECK(Chan(at(aActive, ta.l - 1.5f, CentreY(ta)), 0) > 200 && Chan(at(plain, ta.l - 1.5f, CentreY(ta)), 0) < 60);
    {   // [openxr] skill_rt_a: with the key on and a skill on RT (the sample's Nova) and nothing picked, the gold frame is on the RT tile, not on A; a picked slot keeps it
        View on = SampleView(Page::Wheel, -1, -1);
        on.slotControls = true;
        std::vector<uint32_t> keysOn, keysPicked, keysPlainRt;
        wheelpaint::Render(on, keysOn);
        const tiles::PRect trt = tiles::SlotRect(9);
        CHECK(Chan(at(keysOn, trt.l - 1.5f, CentreY(trt)), 0) > 200 && Chan(at(keysOn, ta.l - 1.5f, CentreY(ta)), 0) < 60);
        CHECK(Differ(keysOn, aActive));
        View picked = on;
        picked.selected = 3;
        wheelpaint::Render(picked, keysPicked);
        CHECK(Chan(at(keysPicked, tr.l - 1.5f, CentreY(tr)), 0) > 200 && Chan(at(keysPicked, trt.l - 1.5f, CentreY(trt)), 0) < 60);
        View attackRt = on;
        attackRt.slot[9] = Sk(kSkillAttack, "Attack");               // the plain attack on RT: the layout's trigger, so A's frame again
        wheelpaint::Render(attackRt, keysPlainRt);
        CHECK(Chan(at(keysPlainRt, ta.l - 1.5f, CentreY(ta)), 0) > 200 && Chan(at(keysPlainRt, trt.l - 1.5f, CentreY(trt)), 0) < 60);
        // the red line names what the trigger casts (letters in its band): different from the basic attack's line
        auto lineLit = [&](const std::vector<uint32_t>& px) {
            int lit = 0;
            const tiles::PRect rl = tiles::kRedLine;
            for (int y = int(rl.t); y < int(rl.b); ++y)
                for (int x = int(rl.l); x < int(rl.r); ++x) lit += Chan(px[(size_t)y * W + x], 0) > 150 && Chan(px[(size_t)y * W + x], 1) < 100;
            return lit;
        };
        CHECK(lineLit(keysOn) > 200 && lineLit(keysPlainRt) > 200 && lineLit(keysOn) != lineLit(keysPlainRt));
    }
    // the pad marks under the tiles: A green, X blue, B red discs; RB and RT light chips
    auto ringLeft = [&](int slot) {
        const tiles::PRect g = tiles::GlyphRect(slot);
        return at(main, CentreX(g) - 21.0f + 1.6f, CentreY(g));
    };
    const uint32_t gA = ringLeft(8), gX = ringLeft(1), gB = ringLeft(0), gY = ringLeft(2);
    CHECK(Chan(gA, 1) > Chan(gA, 0) + 60 && Chan(gA, 1) > Chan(gA, 2) + 60);                         // green
    CHECK(Chan(gX, 2) > Chan(gX, 0) + 60 && Chan(gX, 2) > Chan(gX, 1) + 20);                         // blue
    CHECK(Chan(gB, 0) > Chan(gB, 1) + 100 && Chan(gB, 0) > Chan(gB, 2) + 100);                       // red
    CHECK(Chan(gY, 0) > 150 && Chan(gY, 1) > 120 && Chan(gY, 2) < 100);                              // yellow
    const tiles::PRect chipRb = tiles::GlyphRect(3);
    const uint32_t chip = at(main, CentreX(chipRb) - 28.0f, CentreY(chipRb));
    CHECK(Chan(chip, 0) > 170 && Chan(chip, 1) > 170 && Chan(chip, 2) > 170);                        // a light chip
    const uint32_t bite = at(main, CentreX(chipRb) + 33.0f, CentreY(chipRb) + 16.0f);                // ... with a bite out of its lower right corner
    CHECK(Chan(bite, 0) < 40);
    // the LT chip at the left end of the second row, with its arrows (gold) under it
    const tiles::PRect lt = tiles::LtChipRect(), arrows = tiles::LtArrowsRect();
    const uint32_t ltPx = at(main, lt.l + 8.0f, lt.b - 8.0f);
    CHECK(Chan(ltPx, 0) > 170 && Chan(ltPx, 2) > 170);
    int goldPx = 0;
    for (int y = int(arrows.t); y < int(arrows.b); ++y)
        for (int x = int(arrows.l); x < int(arrows.r); ++x) {
            const uint32_t p = at(main, float(x), float(y));
            if (Chan(p, 0) > 130 && Chan(p, 1) > 90 && Chan(p, 2) < 100 && Chan(p, 0) > Chan(p, 2) + 60) ++goldPx;
        }
    CHECK(goldPx > 60);
    // the title is silver letters on the plate; the state line is red
    int silver = 0, redPx = 0;
    for (int y = int(tiles::kTitlePlate.t) + 6; y < int(tiles::kTitlePlate.b) - 6; ++y)
        for (int x = 400; x < 1200; ++x) {
            const uint32_t p = at(main, float(x), float(y));
            if (Chan(p, 0) > 190 && Chan(p, 1) > 190 && Chan(p, 2) > 190) ++silver;
        }
    for (int y = int(tiles::kRedLine.t); y < int(tiles::kRedLine.b); ++y)
        for (int x = int(tiles::kRedLine.l); x < int(tiles::kRedLine.r); ++x) {
            const uint32_t p = at(main, float(x), float(y));
            if (Chan(p, 0) > 150 && Chan(p, 1) < 100 && Chan(p, 2) < 100) ++redPx;
        }
    CHECK(silver > 400 && redPx > 200);
    // the pages differ from each other; the state without a game differs; the grid follows what is in it
    std::vector<uint32_t> pickSlot, pickSkill, few, off;
    wheelpaint::Render(SampleView(Page::PickSlot, 3, -1), pickSlot);
    wheelpaint::Render(SampleView(Page::PickSkill, 7, -1), pickSkill);
    wheelpaint::Render(SampleView(Page::PickSkill, 7, -1, 5), few);
    View notReady = SampleView(Page::Wheel, -1, -1);
    notReady.ready = false;
    wheelpaint::Render(notReady, off);
    CHECK(pickSlot.size() == (size_t)W * H && pickSkill.size() == (size_t)W * H && few.size() == (size_t)W * H && off.size() == (size_t)W * H);
    CHECK(Differ(pickSlot, main) && Differ(pickSkill, main) && Differ(pickSkill, pickSlot) && Differ(few, pickSkill) && Differ(off, aActive));
    // without a game every plate is copper (nothing is known to be assigned)
    for (int s = 0; s < kSlotCount; ++s) {
        const uint32_t p = faceOf(off, s);
        CHECK(Chan(p, 0) < 2 * Chan(p, 1));
    }
    // a cell under the beam in the grid is lit; the grid tile of a skill that is already on a button is red, the others copper
    std::vector<uint32_t> cell7, cell8;
    wheelpaint::Render(SampleView(Page::PickSkill, 7, -1), cell7);
    wheelpaint::Render(SampleView(Page::PickSkill, 8, -1), cell8);
    const tiles::PRect g7 = tiles::GridTileRect(7);
    CHECK(Chan(at(cell7, g7.l + 0.5f, CentreY(g7)), 0) > Chan(at(cell8, g7.l + 0.5f, CentreY(g7)), 0) + 120);
    auto cellFace = [&](const std::vector<uint32_t>& px, int i) { const tiles::PRect r = tiles::GridTileRect(i); return at(px, CentreX(r), r.t + 24.0f); };
    const uint32_t boundCell = cellFace(pickSkill, 1) /* Fire Bolt is on B */, freeCell = cellFace(pickSkill, 3) /* Ice Bolt is on no button */;
    CHECK(Chan(boundCell, 0) > 2 * Chan(boundCell, 1) && Chan(freeCell, 0) < 2 * Chan(freeCell, 1));
    // the footer buttons light when pointed at, the disabled one does not
    std::vector<uint32_t> hotEdit, hotClose, hotNext, hotPrev;
    wheelpaint::Render(SampleView(Page::Wheel, kEdit, -1), hotEdit);
    wheelpaint::Render(SampleView(Page::Wheel, kClose, -1), hotClose);
    const tiles::PRect edit = tiles::ButtonRect(Page::Wheel, 0);
    CHECK(Chan(at(hotEdit, edit.l + 1.5f, CentreY(edit)), 0) > Chan(at(hotClose, edit.l + 1.5f, CentreY(edit)), 0) + 40);
    wheelpaint::Render(SampleView(Page::PickSkill, kNext, -1), hotNext);
    wheelpaint::Render(SampleView(Page::PickSkill, kPrev, -1), hotPrev);   // (page 0: PREVIOUS is greyed, pointing at it lights nothing)
    const tiles::PRect next = tiles::ButtonRect(Page::PickSkill, 1);
    CHECK(Chan(at(hotNext, next.l + 1.5f, CentreY(next)), 0) > Chan(at(hotPrev, next.l + 1.5f, CentreY(next)), 0) + 40);
    // the category strip: five tabs over the grid; the one shown differs from the others, a tab under the beam is lit, a tab with nothing in it is dim,
    // and the grid is drawn without the strip as before
    View stripView = SampleView(Page::PickSkill, -1, -1);
    stripView.catStrip = true;
    stripView.category = wheel::kCatTree1;
    stripView.catCount[wheel::kCatAll] = 26; stripView.catCount[wheel::kCatTree1] = 10; stripView.catCount[wheel::kCatTree2] = 3; stripView.catCount[wheel::kCatTree3] = 0;
    stripView.catCount[wheel::kCatGeneral] = 13;
    std::snprintf(stripView.treeName[0], kNameLen, "%s", "Fire Spells");
    std::snprintf(stripView.treeName[1], kNameLen, "%s", "Lightning Spells");
    std::snprintf(stripView.treeName[2], kNameLen, "%s", "Cold Spells");
    std::vector<uint32_t> strip, stripHot, stripAll;
    wheelpaint::Render(stripView, strip);
    View hotTab = stripView;
    hotTab.hover = wheel::CategoryCell(wheel::kCatGeneral);
    wheelpaint::Render(hotTab, stripHot);
    View allTab = stripView;
    allTab.category = wheel::kCatAll;
    wheelpaint::Render(allTab, stripAll);
    CHECK(strip.size() == (size_t)W * H && Differ(strip, pickSkill) && Differ(strip, stripHot) && Differ(strip, stripAll));
    // the shown tab is a warm plate, an idle one a dark grey plate, in the tab's own body (left of its text)
    auto tabBody = [&](const std::vector<uint32_t>& px, int c) {
        const tiles::PRect r = tiles::StripRect(c);
        return at(px, r.l + 14.0f, CentreY(r));
    };
    const uint32_t shown = tabBody(strip, wheel::kCatTree1), idle = tabBody(strip, wheel::kCatTree2);
    CHECK(Chan(shown, 0) > Chan(shown, 2) + 14 && Chan(shown, 0) > Chan(idle, 0) + 20);
    CHECK(std::abs(int(Chan(idle, 0)) - int(Chan(idle, 2))) < 12);
    // the strip's pixels are only in its own band: the same view without the strip leaves it as the panel
    const tiles::PRect band = tiles::StripRect(2);
    int bandDiff = 0;
    for (int y = int(band.t); y < int(band.b); ++y)
        for (int x = int(band.l); x < int(band.r); x += 3) bandDiff += at(strip, float(x), float(y)) != at(pickSkill, float(x), float(y));
    CHECK(bandDiff > 100);
    int gridDiff = 0;   // (and the grid under it is the same whichever tab is shown: the view's options are what it draws)
    for (int y = int(tiles::GridRect(0).t); y < int(tiles::kRedLine.t); y += 3)
        for (int x = int(tiles::kPanel.l); x < int(tiles::kPanel.r); x += 3) gridDiff += at(strip, float(x), float(y)) != at(stripAll, float(x), float(y));
    CHECK(gridDiff == 0);
    // RUN / WALK: the tile is there only with the flag, on the main page; red while running, copper while walking, a different face while unknown
    View runView = SampleView(Page::Wheel, -1, -1);
    runView.runWalk = true;
    std::vector<uint32_t> runOff, runUnknown, runWalking, runRunning, runHot;
    wheelpaint::Render(SampleView(Page::Wheel, -1, -1), runOff);
    runView.run = wheel::kRunUnknown;
    wheelpaint::Render(runView, runUnknown);
    runView.run = wheel::kWalking;
    wheelpaint::Render(runView, runWalking);
    runView.run = wheel::kRunning;
    wheelpaint::Render(runView, runRunning);
    runView.hover = wheel::kRunWalk;
    wheelpaint::Render(runView, runHot);
    CHECK(Differ(runOff, runUnknown) && Differ(runUnknown, runWalking) && Differ(runWalking, runRunning) && Differ(runRunning, runHot));
    const tiles::PRect rt = tiles::kRunRect;
    const uint32_t walkFace = at(runWalking, rt.l + 0.5f * tiles::kRunTile, rt.t + 26.0f), runFace = at(runRunning, rt.l + 0.5f * tiles::kRunTile, rt.t + 26.0f);
    CHECK(Chan(runFace, 0) > 2 * Chan(runFace, 1) && Chan(runFace, 0) > 90);                                   // red: on RUN
    CHECK(Chan(walkFace, 0) < 2 * Chan(walkFace, 1) && Chan(walkFace, 0) > Chan(walkFace, 1));                 // copper: on WALK
    CHECK(Chan(at(runHot, rt.l + 0.5f, CentreY(rt)), 0) > Chan(at(runRunning, rt.l + 0.5f, CentreY(rt)), 0) + 100);   // the rim lights under the beam
    // nothing outside its own area changes with the state
    int outside = 0;
    for (int y = 130; y < int(tiles::kPanel.b); y += 3)
        for (int x = 40; x < int(tiles::kW) - 40; x += 3) {
            if (x > int(rt.l) - 20 && x < int(rt.r) + 20 && y > int(rt.t) - 20 && y < int(tiles::kRunNameRect.b) + 20) continue;
            outside += at(runWalking, float(x), float(y)) != at(runRunning, float(x), float(y));
        }
    CHECK(outside == 0);
    // not on the other pages
    View runPick = SampleView(Page::PickSlot, -1, -1);
    std::vector<uint32_t> pickOff, pickOn;
    wheelpaint::Render(runPick, pickOff);
    runPick.runWalk = true;
    wheelpaint::Render(runPick, pickOn);
    CHECK(!Differ(pickOff, pickOn));
    // the hint captions are painted
    View hinted = SampleView(Page::Wheel, -1, -1);
    hinted.hints = tiles::BuildHints(tiles::Profile::Touch, tiles::Profile::Touch, true);
    std::vector<uint32_t> withHints;
    wheelpaint::Render(hinted, withHints);
    CHECK(Differ(withHints, aActive));
    // names of every kind: long, UTF-8, empty, no terminator within the buffer's end - a picture, no crash
    View odd = SampleView(Page::Wheel, 0, 1);
    std::snprintf(odd.slot[0].name, kNameLen, "%s", "A Skill With A Terribly Long Name That Does Not Fit");
    std::snprintf(odd.slot[1].name, kNameLen, "%s", "\xD0\x9E\xD1\x87\xD0\xB5\xD0\xBD\xD1\x8C \xD0\xB4\xD0\xBB\xD0\xB8\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xB5 \xD0\xB8\xD0\xBC\xD1\x8F");
    odd.slot[4].name[0] = 0;
    std::memset(odd.slot[5].name, 'W', kNameLen);   // (not even terminated)
    odd.slot[5].name[kNameLen - 1] = 0;
    std::snprintf(odd.slot[7].name, kNameLen, "%s", "\xD0");   // (a cut UTF-8 sequence)
    std::vector<uint32_t> oddPx;
    wheelpaint::Render(odd, oddPx);
    CHECK(oddPx.size() == (size_t)W * H);
    odd.page = Page::PickSkill;
    wheelpaint::Render(odd, oddPx);
    CHECK(oddPx.size() == (size_t)W * H);
    View unknownSlots = SampleView(Page::Wheel, 9, -1);   // the four slots the game side could not place
    for (int s = 8; s < kSlotCount; ++s) unknownSlots.slot[s] = Skill{};
    wheelpaint::Render(unknownSlots, oddPx);
    CHECK(oddPx.size() == (size_t)W * H && Differ(oddPx, aActive));

    // the worker: the tiles' picture comes back with its own size, and the wheel's with its
    wheelpaint::Async a;
    a.Submit(SampleView(Page::Wheel, 0, -1));
    std::vector<uint32_t> got;
    uint32_t rev = 0;
    int gw = 0, gh = 0;
    bool have = false;
    for (int i = 0; i < 400 && !have; ++i) { have = a.Fetch(got, &rev, &gw, &gh); if (!have) Sleep(10); }
    CHECK(have && rev == 1 && gw == W && gh == H && got.size() == (size_t)W * H);
    View wheelView = SampleView(Page::Wheel, 0, -1);
    wheelView.style = Style::Wheel;
    wheelView.revision = 2;
    a.Submit(wheelView);
    have = false;
    for (int i = 0; i < 400 && !have; ++i) { have = a.Fetch(got, &rev, &gw, &gh); if (!have) Sleep(10); }
    CHECK(have && rev == 2 && gw == wheelpaint::kSize && gh == wheelpaint::kSize && got.size() == (size_t)gw * gh);
    View again2 = SampleView(Page::PickSlot, 1, 2);
    again2.revision = 3;
    a.Submit(again2);
    have = false;
    for (int i = 0; i < 400 && !have; ++i) { have = a.Fetch(got, &rev); if (!have) Sleep(10); }   // (the old two-argument form still works)
    CHECK(have && rev == 3);
    a.Stop();

    // The window as the player sees it: the main row (a tile under the beam, a picked slot, an empty one, a skill gone), with the hints for
    // Touch controllers, with Index controllers, without a game, the slot chooser, the skill grid on page 1 and 2.
    View touch = SampleView(Page::Wheel, 1, 4);
    touch.hints = tiles::BuildHints(tiles::Profile::Touch, tiles::Profile::Touch, true);
    View index = SampleView(Page::Wheel, 5, -1);
    index.hints = tiles::BuildHints(tiles::Profile::Index, tiles::Profile::Index, true);
    View page2 = SampleView(Page::PickSkill, 3, -1, 6);
    page2.editPage = 1;
    View saving = SampleView(Page::PickSkill, 2, -1);
    std::snprintf(saving.status, sizeof saving.status, "Saving...");
    View refused = SampleView(Page::Wheel, 9, -1);
    std::snprintf(refused.status, sizeof refused.status, "That slot cannot be cast from here");
    refused.result = Result::Refused;
    View runTile = SampleView(Page::Wheel, wheel::kRunWalk, -1);
    runTile.runWalk = true;
    runTile.run = wheel::kRunning;
    runTile.hints = touch.hints;
    View trees = stripView;
    trees.hover = 7;
    View noTrees = SampleView(Page::PickSkill, 7, -1);   // the key is on, the game side does not know the trees: the note in the strip's place
    noTrees.catWanted = true;
    View slotKeys = SampleView(Page::Wheel, 9, -1);   // the key on: the right trigger casts RT (Nova), A and the second row by the right A and the left trigger
    slotKeys.slotControls = true;
    slotKeys.hints = tiles::BuildHints(tiles::Profile::Touch, tiles::Profile::Touch, true, true);
    slotKeys.hover = -1;
    View rtEdit = SampleView(Page::PickSkill, 1, -1);   // and the editor on the RT plate: the list has Attack beside Interact, the way back
    rtEdit.slotControls = true;
    rtEdit.editSlot = 9;
    rtEdit.option[1] = Sk(kSkillAttack, "Attack");
    const char* const names[] = {"tiles_main_touch", "tiles_main_index", "tiles_main_nogame", "tiles_main_plain", "tiles_main_refused", "tiles_pickslot",
                                 "tiles_pickskill_page1", "tiles_pickskill_page2", "tiles_pickskill_saving", "tiles_main_runwalk", "tiles_pickskill_trees", "tiles_pickskill_notrees",
                                 "tiles_main_slotkeys", "tiles_pickskill_rt"};
    const View views[] = {touch, index, notReady, SampleView(Page::Wheel, kEdit, -1), refused, SampleView(Page::PickSlot, 9, -1),
                          SampleView(Page::PickSkill, 7, -1), page2, saving, runTile, trees, noTrees, slotKeys, rtEdit};
    const size_t viewCount = sizeof views / sizeof views[0];
    std::vector<uint32_t> px;
    {   // the left trigger bound to its own action (View::rowOff): the slots with LT are greyed, the first row's are not touched
        View on = SampleView(Page::Wheel, kNone, -1), off = on;
        off.rowOff = true;
        std::vector<uint32_t> a, b;
        wheelpaint::Render(on, a);
        wheelpaint::Render(off, b);
        auto differs = [&](const auto& r) {
            for (int y = int(r.t); y < int(r.b); ++y)
                for (int x = int(r.l); x < int(r.r); ++x)
                    if (x >= 0 && y >= 0 && x < W && y < H && a[size_t(y) * W + x] != b[size_t(y) * W + x]) return true;
            return false;
        };
        for (int s = 0; s < wheel::kSlotCount; ++s) CHECK(differs(tiles::SlotRect(s)) == wheel::kSlotDefs[s].alt);
        for (int s = 0; s < wheel::kSlotCount; ++s) if (wheel::kSlotDefs[s].alt) CHECK(differs(tiles::HintRect(s)));   // "LT row off"
        if (dumpDir) {
            WriteBmp(dumpDir, "tiles_main_rowoff", b, W, H);
            View wv = off;
            wv.style = Style::Wheel;
            wheelpaint::Render(wv, px);
            WriteBmp(dumpDir, "wheel_rowoff", px, wheelpaint::kSize, wheelpaint::kSize);
        }
    }
    if (dumpDir) {
        LARGE_INTEGER freq, q0, q1;
        QueryPerformanceFrequency(&freq);
        wheelpaint::Render(SampleView(Page::Wheel, 0, 0), px);
        QueryPerformanceCounter(&q0);
        for (int i = 0; i < 10; ++i) wheelpaint::Render(SampleView(Page::Wheel, i % 8, 1), px);
        QueryPerformanceCounter(&q1);
        std::printf("  a tiles picture (%dx%d): %.1f ms\n", W, H, 1000.0 * double(q1.QuadPart - q0.QuadPart) / double(freq.QuadPart) / 10.0);
        for (size_t i = 0; i < viewCount; ++i) {   // (no icon cache here: names and the stand-in initials)
            wheelpaint::Render(views[i], px);
            WriteBmp(dumpDir, names[i], px, W, H);
        }
        View wv = SampleView(Page::Wheel, 2, 1);   // and the wheel, for the side-by-side
        wv.style = Style::Wheel;
        wheelpaint::Render(wv, px);
        WriteBmp(dumpDir, "wheel_for_comparison", px, wheelpaint::kSize, wheelpaint::kSize);
        std::printf("  pictures written to %s\n", dumpDir);
    }
    // with a real cache (SKILL_WHEEL_ICONS = the folder made by tools/skill_icons from the tester's own game): the icons are drawn
    char icons[400] = {};
    if (dumpDir && GetEnvironmentVariableA("SKILL_WHEEL_ICONS", icons, sizeof icons)) {
        wchar_t wide[400] = {};
        MultiByteToWideChar(CP_UTF8, 0, icons, -1, wide, 399);
        wheelicons::Shutdown();
        wheelicons::SetDirectory(wide);
        CHECK(wheelicons::Available());
        {   // the skills' trees in a cache made by a tool of this version (the tester's own game: only what any class has is asked)
            int cls = -1, page = 0;
            CHECK(wheelicons::HasTrees());
            CHECK(wheelicons::SkillTree(36, &cls, &page) && cls >= 0 && page >= 1 && page <= 3);   // Fire Bolt: a sorceress's, on one of her trees
            CHECK(!wheelicons::TreeKey(cls, page).empty() && wheelicons::TreeKey(cls, 0).empty() && wheelicons::TreeKey(cls, 4).empty());
            CHECK(!wheelicons::SkillTree(kSkillInteract, &cls, &page) || cls < 0 || page == 0);       // the basic attack is on no tree
        }
        std::vector<uint32_t> iconMain;
        wheelpaint::Render(views[0], iconMain);   // (asks for the icons)
        wheelpaint::Render(views[6], iconMain);
        for (int i = 0; i < 150; ++i) Sleep(20);
        wheelpaint::Render(views[0], iconMain);
        CHECK(Differ(iconMain, main));   // the icons arrived
        for (size_t i = 0; i < viewCount; ++i) {
            wheelpaint::Render(views[i], px);
            char n[100];
            std::snprintf(n, sizeof n, "%s_icons", names[i]);
            WriteBmp(dumpDir, n, px, W, H);
        }
        std::printf("  with the local icon cache too (_icons)\n");
        wheelicons::Shutdown();
    }
}

// -- [openxr] skill_rt_a: the right trigger and the right A as the game's RT and A slots ---------------------------------------------------------------

static void SlotsOn(World& w, bool on = true) {
    Settings cfg = w.c.Config();
    cfg.slotControls = on;
    w.c.Configure(cfg);
}
static Step Quiet(World& w, int n = 6) {   // every control up for a few frames (armed)
    w.Hand(0, 0, 0, 0);
    w.Hand(1, 0, 0, 0);
    Step s;
    for (int i = 0; i < n; ++i) s = w.Next();
    return s;
}
constexpr uint32_t kTrig = di::kTrigger | di::kTakeTrigger;

static void TestSlotControlsController() {
    {   // the key off (the default): the trigger and A are the layout's, whatever the slots hold
        World w;
        CHECK(!w.c.Config().slotControls);
        Quiet(w);
        w.Hand(1, 0.9f, 0, di::kPrimary);
        Step s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.buttons == 0 && s.lt == 0 && s.take[0] == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
        CHECK(s.slotStarted[0] == -1 && s.slotStarted[1] == -1);
    }
    {   // on, a skill on RT (Nova): the trigger is the game's RT - held by the pad's trigger value, the layout's trigger taken, the left trigger taken too
        World w;
        SlotsOn(w);
        Quiet(w);
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(s.casting && s.rt == 255 && s.buttons == 0 && s.lt == 0);
        CHECK((s.take[1] & kTrig) == kTrig && (s.take[0] & kTrig) == kTrig && (s.take[1] & di::kPrimary) == 0);
        CHECK(s.slotStarted[1] == 9 && s.slotStarted[0] == -1 && w.c.TriggerCastsSkill());
        s = w.Next();
        CHECK(s.casting && s.rt == 255 && s.slotStarted[1] == -1 && (s.take[1] & kTrig) == kTrig);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0 && s.take[0] == 0);
        CHECK(w.c.TriggerCastsSkill());   // (the trigger is the RT slot's again, waiting for the next pull)
    }
    {   // A: the plain action (the game's Attack / Interact) on it leaves the button to the layout; a skill on it is cast by the button
        World w;
        SlotsOn(w);
        Quiet(w);
        w.Hand(1, 0, 0, di::kPrimary);
        Step s = w.Next();
        CHECK(!s.casting && s.buttons == 0 && (s.take[1] & di::kPrimary) == 0);
        Quiet(w);
        w.snap.slot[8] = Sk(36, "Fire Bolt");
        w.Hand(1, 0, 0, di::kPrimary);
        s = w.Next();
        CHECK(s.casting && s.buttons == kPadA && s.rt == 0 && (s.take[1] & di::kPrimary) && s.slotStarted[0] == 8 && !(s.take[1] & di::kTakeTrigger));
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        CHECK(!s.casting && s.buttons == 0 && s.take[1] == 0);
    }
    {   // the slot is put back to the plain action (Attack on RT): the trigger is the layout's again, TriggerCastsSkill follows
        World w;
        SlotsOn(w);
        Quiet(w);
        CHECK(w.c.TriggerCastsSkill());
        w.snap.slot[9] = Sk(kSkillAttack, "Attack");
        Step s = Quiet(w);
        CHECK(!w.c.TriggerCastsSkill());
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0);
        w.snap.slot[9] = Skill{};   // (not read: the same)
        Quiet(w);
        CHECK(!w.c.TriggerCastsSkill());
    }
    {   // the left trigger is the second layer: LT + RT (11), LT + A (10); the first layer's slots stay quiet
        World w;
        SlotsOn(w);
        w.snap.slot[11] = Sk(59, "Blizzard");
        Quiet(w);
        w.Hand(0, 0.9f, 0, 0);
        for (int i = 0; i < 12; ++i) w.Next();                   // (the left trigger has been down a while: no lead)
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(s.casting && s.lt == 255 && s.rt == 255 && s.slotStarted[1] == 11 && (s.take[0] & kTrig) == kTrig);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        CHECK(!s.casting);
        w.Hand(1, 0, 0, di::kPrimary);                           // LT + A holds Charged Bolt in the sample
        s = w.Next();
        CHECK(s.casting && s.lt == 255 && s.buttons == kPadA && s.slotStarted[0] == 10);
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        // the second layer's slot empty (LT + RT): the layout's own trigger, nothing of ours
        w.snap.slot[11] = Skill{};
        w.snap.slot[11].known = true;
        Quiet(w);
        w.Hand(0, 0.9f, 0, 0);
        for (int i = 0; i < 12; ++i) w.Next();
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
    }
    {   // the left trigger pulled just before the trigger: the modifier first, alone, then the button (the game reads it that way)
        World w;
        SlotsOn(w);
        w.snap.slot[11] = Sk(59, "Blizzard");
        Quiet(w);
        w.Hand(0, 0.9f, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(s.casting && s.lt == 255 && s.rt == 0 && s.slotStarted[1] == 11);
        int frames = 0;
        for (; frames < 40 && s.rt == 0; ++frames) s = w.Next();
        CHECK(s.rt == 255 && s.lt == 255 && frames >= 4 && frames <= 12);
    }
    {   // the panel up: the trigger is a click, A a button of the panel - nothing is cast
        World w;
        SlotsOn(w);
        OpenPanel(w);
        CHECK(w.c.Open());
        w.Hand(1, 0.9f, 0, di::kPrimary);
        const Step s = w.Next();
        CHECK(s.rt == 0 && s.buttons == 0 && s.slotStarted[0] == -1 && s.slotStarted[1] == -1 && !w.c.TriggerCastsSkill());
    }
    {   // a slot picked on the panel keeps the trigger (the pick is explicit): the picked slot's pad B, not RT; dropping the pick gives it back to RT
        World p;
        SlotsOn(p);
        OpenPanel(p);
        p.ClickCell(Page::Wheel, 0);                             // B picked, the panel closed
        CHECK(p.c.Selected() == 0 && !p.c.Open());
        Quiet(p);
        CHECK(!p.c.TriggerCastsSkill());
        p.Hand(1, 0.9f, 0, 0);
        Step s = p.Next();
        CHECK(s.casting && s.buttons == kPadB && s.rt == 0 && s.slotStarted[1] == -1);
        Quiet(p, 30);
        OpenPanel(p);
        p.ClickCell(Page::Wheel, 8);                             // the A tile: the pick is dropped
        CHECK(p.c.Selected() == -1 && !p.c.Open());
        Quiet(p);
        CHECK(p.c.TriggerCastsSkill());
        p.Hand(1, 0.9f, 0, 0);
        s = p.Next();
        CHECK(s.casting && s.rt == 255 && s.buttons == 0 && s.slotStarted[1] == 9);
    }
    {   // another view, no direct input (BodyWalk's input), a panel of the plugin's own up (kUiUp), the grip held, a menu: nothing
        World w;
        SlotsOn(w);
        Quiet(w);
        w.f4 = false;
        w.Hand(1, 0.9f, 0, di::kPrimary);
        Step s = w.Next();
        CHECK(!s.casting && s.take[0] == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
        w.f4 = true;
        Quiet(w);
        w.f.flags &= ~di::kDirect;
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
        w.f.flags |= di::kDirect;
        Quiet(w);
        w.f.flags |= di::kUiUp;
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
        w.f.flags &= ~di::kUiUp;
        Quiet(w);
        w.Hand(1, 0.9f, 0.9f, 0);                                // the right grip held: the layout's chord
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0 && !w.c.TriggerCastsSkill());
        w.f.flags |= di::kInMenu;                                // a menu of the game's
        Quiet(w);
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && s.take[1] == 0);
    }
    {   // somebody else's: a gesture that holds the trigger (a bottle in the hand, a javelin) or the bow's string - the trigger; a gesture that holds A - the button
        World w;
        SlotsOn(w);
        Quiet(w);
        w.c.SetControlsTaken(true, false);
        w.Hand(1, 0.9f, 0, di::kPrimary);
        Step s = w.Next();
        CHECK(s.rt == 0 && s.slotStarted[1] == -1 && (s.take[1] & di::kTakeTrigger) == 0 && !w.c.TriggerCastsSkill());
        w.snap.slot[8] = Sk(36, "Fire Bolt");
        w.c.SetControlsTaken(false, true);
        Quiet(w);   // (a control that was not free is let go once before it starts anything)
        w.Hand(1, 0.9f, 0, di::kPrimary);
        s = w.Next();
        CHECK(s.slotStarted[1] == 9 && s.slotStarted[0] == -1 && s.buttons == 0);   // the trigger is ours, A is the gesture's
        World b;
        SlotsOn(b);
        Quiet(b);
        b.c.SetCastBlocked(true);                                // the bow's draw owns the trigger
        b.Hand(1, 0.9f, 0, 0);
        s = b.Next();
        CHECK(s.rt == 0 && !s.casting && !b.c.TriggerCastsSkill());
    }
    {   // a snapshot that is not this session's, not read or older than the controls will trust: the layout's controls
        World w;
        SlotsOn(w);
        Quiet(w);
        w.snap.session = 99;
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(!s.casting && s.rt == 0 && !w.c.TriggerCastsSkill());
        w.snap.session = 3;
        w.snap.ready = false;
        Quiet(w);
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && !w.c.TriggerCastsSkill());
    }
    {   // switched off while a press is on: it ends at once, and the state is the ini's
        World w;
        SlotsOn(w);
        Quiet(w);
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(s.casting && s.rt == 255);
        SlotsOn(w, false);
        s = w.Next();
        CHECK(!s.casting && s.rt == 0 && !w.c.TriggerCastsSkill());
    }
}

static void TestSlotControlsModelAndHints() {
    {   // the trigger's slot on the first page: the picked one; with the key on and a skill on RT, RT; else A, the layout's attack
        View v = SampleView(Page::Wheel, -1, -1);
        CHECK(wheel::TriggerSlot(v) == 8);
        v.slotControls = true;
        CHECK(wheel::TriggerSlot(v) == 9);                       // RT holds Nova
        v.slot[9] = Sk(kSkillAttack, "Attack");
        CHECK(wheel::TriggerSlot(v) == 8);                       // the plain attack on RT: the layout's
        v.slot[9] = Sk(48, "Nova");
        v.selected = 5;
        CHECK(wheel::TriggerSlot(v) == 5);                       // a picked slot is the trigger's whatever RT holds
        v.selected = -1;
        v.ready = false;
        CHECK(wheel::TriggerSlot(v) == 8);
        v.ready = true;
        v.slotControls = false;
        CHECK(wheel::TriggerSlot(v) == 8);
    }
    {   // the model hands the key to the view, and a click on the RT tile says what the control is (the tile is not castable by a pick)
        Rig r;
        r.slotControls = true;
        r.Step(true);
        CHECK(r.m.Current().slotControls);
        Out o = r.Point(Page::Wheel, 9, true);
        CHECK(r.m.Open() && o.feedback == Feedback::Refused && std::strstr(r.m.Current().status, "right trigger") != nullptr);
        o = r.Point(Page::Wheel, 11, true);
        CHECK(r.m.Open() && o.feedback == Feedback::Refused && std::strstr(r.m.Current().status, "left trigger") != nullptr);
        Rig q;
        q.Step(true);
        CHECK(!q.m.Current().slotControls);
        q.Point(Page::Wheel, 9, true);
        CHECK(std::strstr(q.m.Current().status, "cannot be cast") != nullptr);     // (the key off: the old words)
    }
    {   // the editor on RT and A: the same request as for any slot (the game's setter); the Attack entry is a pick like the others
        Rig r;
        r.slotControls = true;
        r.snap.catalog[1] = Sk(kSkillAttack, "Attack");
        r.Step(true);
        r.Point(Page::Wheel, kEdit, true);
        r.Point(Page::PickSlot, 9, true);                         // the RT plate
        CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().editSlot == 9);
        const Out o = r.Point(Page::PickSkill, 1, true);          // the Attack entry
        CHECK(o.request && o.req.op == Op::Assign && o.req.slot == 9 && o.req.skill == kSkillAttack && o.req.prevSkill == 48 && o.req.bank == 0);
        Rig a;
        a.slotControls = true;
        a.Step(true);
        a.Point(Page::Wheel, kEdit, true);
        a.Point(Page::PickSlot, 8, true);                         // the A plate
        const Out oa = a.Point(Page::PickSkill, 0, true);         // the first entry of the sample catalog
        CHECK(oa.request && oa.req.slot == 8 && oa.req.skill == 100 && oa.req.prevSkill == kSkillInteract + 1);
    }
    {   // the captions under the tiles: with the key on, A is "Right A" and RT "R trigger" (each with the left trigger for the second row)
        using tiles::Profile;
        char h[32];
        auto hint = [&](int slot, Profile l, Profile r, bool keys) { return tiles::HandHint(slot, l, r, h, sizeof h, keys) ? std::string(h) : std::string("-"); };
        CHECK(hint(8, Profile::Touch, Profile::Touch, true) == "Right A" && hint(10, Profile::Touch, Profile::Touch, true) == "LT + Right A");
        CHECK(hint(9, Profile::Touch, Profile::Touch, true) == "R trigger" && hint(11, Profile::Touch, Profile::Touch, true) == "LT + R trigger");
        CHECK(hint(8, Profile::Index, Profile::Index, true) == "Right A" && hint(8, Profile::Frame, Profile::Frame, true) == "Right A");
        CHECK(hint(8, Profile::Vive, Profile::Vive, true) == "-" && hint(9, Profile::Vive, Profile::Vive, true) == "R trigger");   // (no face button to name; a trigger every controller has)
        CHECK(hint(8, Profile::None, Profile::None, true) == "-" && hint(9, Profile::None, Profile::None, true) == "-");
        // the key off: as it was (A is the layout's trigger, RT has no control)
        CHECK(hint(8, Profile::Touch, Profile::Touch, false) == "R trigger" && hint(9, Profile::Touch, Profile::Touch, false) == "-");
        CHECK(hint(10, Profile::Touch, Profile::Touch, false) == "LT + R trigger" && hint(11, Profile::Touch, Profile::Touch, false) == "-");
        for (int s = 0; s < 8; ++s) CHECK(hint(s, Profile::Touch, Profile::Touch, true) == hint(s, Profile::Touch, Profile::Touch, false));   // the first eight: the same
        for (int s = 0; s < kSlotCount; ++s) { tiles::HandHint(s, Profile::Touch, Profile::Touch, h, sizeof h, true); CHECK(std::strlen(h) < (size_t)kHintLen); }
        // the whole table
        const Hints on = tiles::BuildHints(Profile::Touch, Profile::Touch, true, true);
        const Hints plain = tiles::BuildHints(Profile::Touch, Profile::Touch, true, false);
        CHECK(std::string(on.text[8]) == "Right A" && std::string(on.text[9]) == "R trigger" && std::string(plain.text[8]) == "R trigger" && plain.text[9][0] == 0);
        CHECK(!(on == plain));
        // the layout rebound (hints off): the controls the panel takes itself are still true, the rest stays empty
        const Hints reb = tiles::BuildHints(Profile::Touch, Profile::Touch, false, true);
        CHECK(std::string(reb.text[8]) == "Right A" && std::string(reb.text[9]) == "R trigger" && std::string(reb.text[10]) == "LT + Right A" && std::string(reb.text[11]) == "LT + R trigger");
        for (int s = 0; s < 8; ++s) CHECK(reb.text[s][0] == 0);
        const Hints none = tiles::BuildHints(Profile::Touch, Profile::Touch, false, false);
        CHECK(none == Hints{});
        CHECK(tiles::BuildHints(Profile::Touch, Profile::Touch, true) == plain);    // (the three-argument call is the key off)
    }
}

int main(int argc, char** argv) {
    TestPaint(argc > 2 && std::strcmp(argv[1], "--dump") == 0 ? argv[2] : nullptr);
    TestGeometry();
    TestStripAndRunTile();
    TestHitStripAndRunTile();
    TestHitTest();
    TestLabels();
    TestTwelveSlots();
    TestModelTiles();
    TestModelStripAndRunTile();
    TestControllerTiles();
    TestStripLateTrees();
    TestControllerStrip();
    TestUserCache();
    TestCastGateSlots();
    TestF4Gate();
    TestLongPress();
    TestStyleChange();
    TestFuzz();
    TestSlotControlsController();
    TestSlotControlsModelAndHints();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
