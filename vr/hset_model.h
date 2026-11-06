#pragma once
// The in-headset settings panel: what it shows, where its buttons are, what a click does. Pure - no
// Windows, no OpenXR - so the whole of it is tested without a headset (cleanroom-style: tools/hset_test.cpp).
//
// After bearstonem/d2r-pcvr's steamvr/vr_settings.h (MIT): a table of keys, a draft of the values, a hit
// test on the painted atlas, Apply writes only what changed, a conflict with an outside edit reloads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hset_body.h"
#include "hset_coop.h"      // the Co-op page: D2R Rooms' relay bridge, its own d2r-rooms.ini
#include "hset_defs.h"
#include "hset_keys.h"
#include "hset_party.h"     // the Party page: the players in the game, the game's own party buttons
#include "hset_stats.h"     // the Stats page: the strike statistics ([stats] strike_stats)
#include "hset_video.h"
#include "first_start.h"    // the Guide page: how many pages the first-start guide has
#include "toolbar_type.h"   // the HUD page's toolbar type: a step over bar_place and hide (shared/)

namespace hset {

// -- values ----------------------------------------------------------------------------
// How many decimals a step has (0.01 -> 2, 0.5 -> 1, 5 -> 0): a value on the grid is rounded to them, so -0.3 + 31 * 0.01 is 0.01, not 0.00999999.
inline int StepDecimals(float step) {
    int n = 0;
    float s = std::fabs(step);
    while (n < 6 && std::fabs(s - std::round(s)) > 1e-5f) { s *= 10.0f; ++n; }
    return n;
}
inline float Normalize(int key, float v) {
    const Def& d = kDefs[key];
    if (!std::isfinite(v)) return d.def;
    if (d.type == DefType::Choice) return std::clamp(std::round(v), 0.0f, (float)keys::CustomPick());   // a pick of the list, or Custom one past it
    v = std::clamp(v, d.min, d.max);
    if (IsToggle(d)) return v > 0.5f ? 1.0f : 0.0f;
    if (d.step > 0.0f) {   // on the grid that starts at min
        v = d.min + std::round((v - d.min) / d.step) * d.step;
        if (const int n = StepDecimals(d.step)) {
            float k = 1.0f;
            for (int i = 0; i < n; ++i) k *= 10.0f;
            v = std::round(v * k) / k;
        }
        v = std::clamp(v, d.min, d.max);
    }
    return v;
}
inline Values Defaults() {
    Values v{};
    for (int i = 0; i < kCount; ++i) v[i] = kDefs[i].def;
    return v;
}
// Two values apart by more than what a click moves (a tenth of the smallest step).
inline bool Differ(float a, float b) { return std::fabs(a - b) > 1e-3f; }
inline Mask Changed(const Values& a, const Values& b) {
    Mask m = 0;
    for (int i = 0; i < kCount; ++i) if (Differ(a[i], b[i])) m |= Bit(i);
    return m;
}

enum class Result { None, Saving, Saved, Failed, Conflict };

// What the ini says now (the store's thread makes it): `values` normalised for the panel, `file` as parsed
// (the default for a missing key), what a conflict check compares against.
struct Snapshot {
    Values values = Defaults();
    Values file = Defaults();
    bool ready = false;          // the ini is there
    uint64_t sequence = 0;       // of the last Apply this answers
    Result result = Result::None;
    // The binding keys (kKeyBindFirst ...) as the file has their lines, "" when a line is not there: values[] holds the choice each is
    // (kCustom: none of the list's), this is what a conflict is judged by and what a custom one shows.
    std::array<std::string, kBindCount> bind;
    // The Co-op page's: what the store's thread read of the D2R Rooms plugin's files (a new one only when something in it changed; null: not read yet).
    std::shared_ptr<const coop::State> coop;
};
// An Apply: `base` is what the panel saw in the file when its draft began, `values` the draft; only `changed` is written.
struct Request {
    Values base{}, values{};
    Mask changed = 0;
    uint64_t sequence = 0;
    std::array<std::string, kBindCount> bind;   // the binding lines the draft began from (the base of a binding key's conflict check)
};

// -- the atlas: the painted panel, in pixels -----------------------------------------------
constexpr int kW = 1024, kH = 720;
struct Rect {
    int x, y, w, h;
    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
// The tabs share the 960 pixels under the title in proportion to their names' widths (hset_defs.h kPageNameW, + 16 for the tab's edges and the text's
// margin): "Options" gets a wide one, "Life" a narrow one, so a page more does not squeeze the long names out of their tab (eleven pages at an even 87
// pixels would not hold "Options" in either painter; fourteen by their letters held neither "Options" nor "HUD").
// A view shows only its tabs (hset_defs.h TabShown: VR F1 - F3 not F4's body): they share the strip among themselves, a tab not shown has no place.
constexpr int TabWeightTo(int i, int view = 0) {   // the weights of the first i tabs shown (a tab is not always a page: hset_defs.h TabOf)
    int w = 0;
    for (int k = 0; k < i; ++k) if (TabShown(k, view)) w += kTabNameW[k] + 16;
    return w;
}
constexpr int kTabX(int i, int view = 0) { return 32 + 960 * TabWeightTo(i, view) / TabWeightTo(kTabCount, view); }
constexpr Rect TabRect(int i, int view) {
    if (!TabShown(i, view)) return {0, 0, 0, 0};
    const int x = kTabX(i, view), next = 32 + 960 * (TabWeightTo(i, view) + kTabNameW[i] + 16) / TabWeightTo(kTabCount, view);
    return {x, 84, next - x - 8, 52};
}
constexpr Rect kTab(int i) { return TabRect(i, 0); }
constexpr int kRowY0 = 156, kRowStep = 66, kRowH = 56;
// A page with more rows than kRowsPerPage (the Melee page's seven, up to kRowsMax) stands them closer, its last one still above the status line.
// The rows' rectangles take the page (-1, as the other pages: the usual step).
constexpr int kRowStepClose = 58, kRowHClose = 50;
// A tab of several pages (hset_defs.h HasSubRow: World's four - Contact, Monsters react, Grab, More - and Controls' two, Left and Right): their buttons
// on top (a Tab hit of that page), sharing the rows' width; the rows under them closer still (seven rows and the buttons above the status line: the
// row's name in the normal face, hset_scene.h).
constexpr Rect kSubBtn(int i, int n) { return {32 + i * (960 + 16) / n, 144, (960 + 16) / n - 16, 42}; }
constexpr Rect kWorldSub(int i) { return kSubBtn(i, kWorldPages); }
constexpr int kWorldRowY0 = 200, kWorldRowStep = 52, kWorldRowH = 46;
constexpr bool CloseRows(int page) { return page >= 0 && page < kPageCount && (RowsOn(page) > kRowsPerPage || HasSubRow(page)); }
constexpr int RowStep(int page) { return HasSubRow(page) ? kWorldRowStep : CloseRows(page) ? kRowStepClose : kRowStep; }
constexpr int RowH(int page) { return HasSubRow(page) ? kWorldRowH : CloseRows(page) ? kRowHClose : kRowH; }
constexpr int RowY(int row, int page) { return (HasSubRow(page) ? kWorldRowY0 : kRowY0) + row * RowStep(page); }
constexpr Rect kDec(int row, int page = -1) { return {584, RowY(row, page), 72, RowH(page)}; }
constexpr Rect kVal(int row, int page = -1) { return {664, RowY(row, page), 248, RowH(page)}; }
constexpr Rect kInc(int row, int page = -1) { return {920, RowY(row, page), 72, RowH(page)}; }
constexpr Rect kToggle(int row, int page = -1) { return {584, RowY(row, page), 408, RowH(page)}; }
// The game view row (the first page's last): F1 (from above), F2 (third person), F3 (the game on your floor), F4 (first person).
constexpr int kViews = 4;
constexpr Rect kViewBtn(int i) { return {584 + i * 104, kRowY0 + kViewRow * kRowStep, 96, kRowH}; }
constexpr Rect kStatus = {32, 566, 960, 40};
constexpr bool RowsFit() {
    for (int p = 0; p < kPageCount; ++p)
        if (RowsOn(p) > kRowsMax || (RowsOn(p) > 0 && RowY(RowsOn(p) - 1, p) + RowStep(p) - 5 > kStatus.y)) return false;
    return true;
}
static_assert(RowsFit(), "every page's rows end above the status line");
// The Guide page (the first-start guide, vr/first_start.h): its title and "2 / 5", its rows from kGuideTop to kGuideBottom (vr/hset_scene.h lays them out by
// how many lines each takes), and Back / Next under them (on the measurement's offer: Not now / Measure now; on the last page: Done).
constexpr Rect kGuideTitle = {32, 146, 720, 38};
constexpr Rect kGuideAt = {752, 146, 240, 38};
constexpr int kGuideTop = 190, kGuideBottom = 510;
constexpr Rect kGuideBack = {32, 516, 230, 46};
constexpr Rect kGuideNext = {762, 516, 230, 46};
// The Video page: a title row, a line about the sizes, eight presets in two rows of four, the slider (a stepper at each end),
// the cost, and two notes. The slider's track is one hit zone; its stops run from kSlX0 to kSlX1 (the thumb's centre).
constexpr Rect kVidTitle = {32, 148, 960, 36};
constexpr Rect kVidNow = {32, 184, 960, 30};
constexpr Rect kPreset(int i) { return {32 + (i % 4) * 244, 224 + (i / 4) * 64, 228, 54}; }
constexpr Rect kSlDec = {32, 358, 72, 56};
constexpr Rect kSlInc = {920, 358, 72, 56};
constexpr Rect kSlTrack = {112, 358, 800, 56};
constexpr int kSlX0 = 142, kSlX1 = 882;
constexpr Rect kSlEnds = {112, 416, 800, 26};
constexpr Rect kVidCost = {32, 450, 960, 30};
constexpr Rect kVidLimit = {32, 482, 960, 30};
constexpr Rect kVidNote = {32, 516, 960, 30};
// The Body page: its one table row on top (how you play: standing / seated, row 0 as on every page), what Calibrate is for, the three
// buttons (the measurement, its Forget, the first-start guide again), what is stored, and the steps.
constexpr Rect kBodyWhy = {32, 222, 960, 30};
constexpr Rect kCalibrate = {32, 258, 300, 64};
constexpr Rect kForget = {344, 258, 320, 64};
constexpr Rect kGuideBtn = {676, 258, 316, 64};   // the first-start guide (vr/first_start.h) again: the Guide page, from its first page
// right under Calibrate: the arm span by hand (hset::body::StepSpan), a stepper each side of its value, what it does beside it
constexpr Rect kSpanDec = {32, 330, 64, 50};
constexpr Rect kSpanVal = {104, 330, 228, 50};
constexpr Rect kSpanInc = {340, 330, 64, 50};
constexpr Rect kSpanNote = {416, 330, 576, 50};
constexpr Rect kBodyStored = {32, 384, 960, 32};
constexpr Rect kBodyUse = {32, 416, 960, 26};
constexpr Rect kBodyStep(int i) { return {48, 446 + i * 29, 944, 27}; }
// The Co-op page: three rows where a table page has its rows (the bridge's switch, the relay's mode, the relay to host on: hset_coop.h), then
// up to coop::kLines lines of what the plugin knows, above the status line.
constexpr int kCoopRows = coop::kWhatCount;
constexpr Rect kCoopLine(int i) { return {48, 358 + i * 26, 944, 26}; }
// [openxr] headset_map_party: the row "The game's map shows" under the three (hset_party.h MapShow) - its two switches, Party and Names - and the
// lines below it then, fewer (kCoopLinesUnderMap).
constexpr int kMapRow = kCoopRows;
constexpr Rect kMapBtn(int what) { return what == 0 ? Rect{584, RowY(kMapRow, -1), 222, kRowH} : Rect{814, RowY(kMapRow, -1), 178, kRowH}; }   // Show Party, Names
constexpr Rect kCoopLineUnderMap(int i) { return {48, 358 + kRowStep + i * 26, 944, 26}; }
constexpr int kCoopLinesUnderMap = 5;
// The Party page (hset_party.h): a title line, a row a player (name, level and class, state, the game's button for him), Leave party and Open Party
// panel under them. The Co-op page's Party button and the Party page's Relay button stand where the low-life pages' Preview does (kPreviewBtn).
constexpr Rect kPartyTitle = {32, 148, 960, 34};
constexpr Rect kPartyRow(int i) { return {32, 188 + i * 44, 960, 40}; }
constexpr Rect kPartyName(int i) { return {48, 188 + i * 44, 330, 40}; }
constexpr Rect kPartyInfo(int i) { return {384, 188 + i * 44, 250, 40}; }
constexpr Rect kPartyState(int i) { return {640, 188 + i * 44, 160, 40}; }
constexpr Rect kPartyBtn(int i) { return {808, 190 + i * 44, 184, 36}; }
constexpr Rect kPartyLeave = {32, 504, 300, 52};
constexpr Rect kPartyOpen = {344, 504, 400, 52};
// The Stats page (hset_stats.h): < the hero > with the line under his name, This game / All time; four totals; a row a weapon (two lines: its name and
// kind; the numbers beside them) under the columns' heads.
constexpr Rect kStatsPrev = {32, 146, 56, 56};
constexpr Rect kStatsName = {100, 144, 440, 32};
constexpr Rect kStatsLine = {100, 176, 440, 26};
constexpr Rect kStatsNext = {548, 146, 56, 56};
constexpr Rect kStatsNow = {616, 146, 182, 56};
constexpr Rect kStatsAll = {810, 146, 182, 56};
constexpr Rect kStatsTile(int i) { return {32 + i * 244, 210, 228, 78}; }
constexpr Rect kStatsTileCap(int i) { return {32 + i * 244, 211, 228, 24}; }
constexpr Rect kStatsTileVal(int i) { return {32 + i * 244, 233, 228, 32}; }
constexpr Rect kStatsTileSub(int i) { return {32 + i * 244, 263, 228, 24}; }
constexpr int kStatsHeadY = 294, kStatsRowY0 = 322, kStatsRowStep = 60;
constexpr Rect kStatsRow(int i) { return {32, kStatsRowY0 + i * kStatsRowStep, 960, kStatsRowStep - 4}; }
// the columns: the weapon (name over kind), blows, speed, force, missed (the numbers right-aligned in theirs)
constexpr Rect kStatsCol(int c, int y, int h) {
    return c == 0 ? Rect{48, y, 380, h} : c == 1 ? Rect{430, y, 110, h} : c == 2 ? Rect{550, y, 160, h} : c == 3 ? Rect{720, y, 160, h} : Rect{884, y, 100, h};
}
constexpr Rect kRecentre = {32, 620, 280, 68};
// The two low-life pages' own button, between Recentre and Close: the vignette and the beat in the controllers for five seconds, with the rows as they stand.
constexpr Rect kPreviewBtn = {324, 620, 200, 68};
constexpr Rect kClose = {532, 620, 220, 68};
constexpr Rect kApply = {772, 620, 220, 68};
// The Video page's Auto ([autotune] enabled; vr/auto_tune_run.h): its button where the low-life pages' Preview stands. While a run measures or
// its result waits, the page shows the run's lines instead of the render size, and Keep / Undo (Stop while it runs) under them.
constexpr Rect kAutoBtn = {324, 620, 200, 68};
constexpr int kAutoLines = 10;
constexpr Rect kAutoTitle = {32, 148, 960, 36};
constexpr Rect kAutoLine(int i) { return {48, 188 + i * 32, 944, 30}; }
constexpr Rect kAutoKeep = {32, 516, 300, 46};
constexpr Rect kAutoUndo = {344, 516, 300, 46};
// What the plugin says of the run, every frame the panel is up on the Video page; the words are the plugin's (in the player's language).
struct AutoTune {
    bool on = false;          // [autotune] enabled: the Auto button is there
    int state = 0;            // 0 nothing runs, 1 a run measures, 2 its result waits for Keep or Undo
    uint64_t revision = 0;    // the plugin's: anything below changed
    std::string title, status;
    std::vector<std::string> lines;
    bool operator==(const AutoTune& o) const { return on == o.on && state == o.state && revision == o.revision; }
    bool operator!=(const AutoTune& o) const { return !(*this == o); }
};
// The panel in the room: its width in metres (the height follows the atlas), and where it stands from the eyes.
constexpr float kWidthM = 1.2f, kHeightM = kWidthM * kH / kW;
constexpr float kAheadM = 1.0f, kBelowM = 0.05f;

enum class Kind { None, Tab, Change, Apply, Close, Recentre, Preset, Slider, Calibrate, Forget, View, Preview, Guide, GuideBack, GuideNext, Span, Coop,
                  Party, PartyLeave, PartyOpen, Auto, MapShow, Stats };
struct Hit {
    Kind kind = Kind::None;
    int index = -1;   // a tab's page, a Change's key, a Preset's number, a Slider's side along the track (-1: a stepper at its end), a View's number (1 = F1 ... 4 = F4),
                      // a Coop row's choice (coop::What), a Party row's player (0 .. party::kMaxOthers - 1), a MapShow's switch (party::MapWhat),
                      // a Stats button (0: the hero, delta -1 / +1; 1: the period, delta 0 this game / 1 all time)
    int delta = 0;    // -1 / +1 (a toggle: 1; a Slider's stepper)
                      // (an Auto's index: 0 the Auto button, 1 Keep, 2 Undo / Stop)
    bool operator==(const Hit& o) const { return kind == o.kind && index == o.index && delta == o.delta; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

// The slider's side at a place along the atlas (x in pixels; also off the track: the thumb follows the beam while it is held).
inline Hit SliderAt(float x) {
    return {Kind::Slider, video::SideAtFraction((x - (float)kSlX0) / (float)(kSlX1 - kSlX0)), 0};
}
// A held trigger on these goes on pressing them (the steppers: a Change with a step, the slider's ends). Not the Players row: each press is one
// command to the game, as typed.
inline bool Repeats(const Hit& h) {
    if (h.kind == Kind::Slider) return h.index < 0 && h.delta != 0;
    if (h.kind == Kind::Span) return h.delta != 0;
    return h.kind == Kind::Change && h.delta != 0 && h.index >= 0 && h.index < kCount && !IsToggle(kDefs[h.index]) && !IsGame(h.index);
}

// The Options page's Players row (DefType::Game): what the game says of its players count, handed in by the plugin (vr/players_cmd.h).
constexpr int kPlayersMin = 1, kPlayersMax = 8;   // what /players takes (the game clamps to it too)
struct Players {
    int count = 0;        // the game's count now, 1 - 8 (its Offline Difficulty Scaling); 0 not known (the game's code not found, or not read yet)
    bool takes = false;   // the game takes /players now: single player, or the host of a TCP/IP game (the game's own check, read back)
    bool operator==(const Players& o) const { return count == o.count && takes == o.takes; }
    bool operator!=(const Players& o) const { return !(*this == o); }
};
// How long the row waits for the game to show the count it was asked for before it takes the answer as a no.
constexpr double kPlayersWaitMs = 1500.0;

// The atlas' u, v (0..1 from its top left) to the button there, in a view (hset_defs.h PageShown: 0 / 4 everything, VR F1 - F3 not F4's body).
inline Hit HitTest(int page, float u, float v, int view = 0) {
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return {};
    const float x = u * kW, y = v * kH;
    for (int i = 0; i < kTabCount; ++i) if (TabRect(i, view).Contains(x, y)) return {Kind::Tab, TabPage(i), 0};   // (a tab opens its first page)
    if (HasSubRow(page))
        for (int i = 0; i < SubCount(page); ++i) if (kSubBtn(i, SubCount(page)).Contains(x, y)) return {Kind::Tab, SubFirst(page) + i, 0};
    if (kApply.Contains(x, y)) return {Kind::Apply, -1, 0};
    if (kClose.Contains(x, y)) return {Kind::Close, -1, 0};
    if (kRecentre.Contains(x, y)) return {Kind::Recentre, -1, 0};
    if (IsLifePage(page) && kPreviewBtn.Contains(x, y)) return {Kind::Preview, -1, 0};
    if (page == kPageView)
        for (int i = 0; i < kViews; ++i) if (kViewBtn(i).Contains(x, y)) return {Kind::View, i + 1, 0};
    if (page == kPageVideo) {
        if (kAutoBtn.Contains(x, y)) return {Kind::Auto, 0, 0};   // (the model takes these only as the run's state allows)
        if (kAutoKeep.Contains(x, y)) return {Kind::Auto, 1, 0};
        if (kAutoUndo.Contains(x, y)) return {Kind::Auto, 2, 0};
        for (int i = 0; i < video::kPresetCount; ++i) if (kPreset(i).Contains(x, y)) return {Kind::Preset, i, 0};
        if (kSlDec.Contains(x, y)) return {Kind::Slider, -1, -1};
        if (kSlInc.Contains(x, y)) return {Kind::Slider, -1, 1};
        if (kSlTrack.Contains(x, y)) return SliderAt(x);
        return {};
    }
    if ((page == kPageCoop || page == kPageParty) && kPreviewBtn.Contains(x, y)) return {Kind::Tab, page == kPageCoop ? kPageParty : kPageCoop, 0};   // Party / Relay
    if (page == kPageParty) {   // a player's button, Leave party, Open Party panel (the model takes none while the page is off or the game says nothing)
        for (int i = 0; i < party::kMaxOthers; ++i) if (kPartyBtn(i).Contains(x, y)) return {Kind::Party, i, 0};
        if (kPartyLeave.Contains(x, y)) return {Kind::PartyLeave, -1, 0};
        if (kPartyOpen.Contains(x, y)) return {Kind::PartyOpen, -1, 0};
        return {};
    }
    if (page == kPageCoop) {   // the bridge's switch, then the two choices' < and > (the model takes none while the plugin is not there)
        for (int w = 0; w < party::kMapCount; ++w) if (kMapBtn(w).Contains(x, y)) return {Kind::MapShow, w, 1};   // (only while its row is there: the model's)
        if (kToggle(coop::kEnabled).Contains(x, y)) return {Kind::Coop, coop::kEnabled, 1};
        for (int r = coop::kMode; r < kCoopRows; ++r) {
            if (kDec(r).Contains(x, y)) return {Kind::Coop, r, -1};
            if (kInc(r).Contains(x, y)) return {Kind::Coop, r, 1};
        }
        return {};
    }
    if (page == kPageStats) {   // the hero round the list, the period (the page only shows: nothing else to press)
        if (kStatsPrev.Contains(x, y)) return {Kind::Stats, 0, -1};
        if (kStatsNext.Contains(x, y)) return {Kind::Stats, 0, 1};
        if (kStatsNow.Contains(x, y)) return {Kind::Stats, 1, 0};
        if (kStatsAll.Contains(x, y)) return {Kind::Stats, 1, 1};
        return {};
    }
    if (page == kPageGuide) {   // what Back and Next do depends on the guide's page (the model's): the offer's Not now / Measure now, the last page's Done
        if (kGuideBack.Contains(x, y)) return {Kind::GuideBack, -1, -1};
        if (kGuideNext.Contains(x, y)) return {Kind::GuideNext, -1, 1};
        return {};
    }
    if (page == kPageBody) {   // (and its table row, below)
        if (kCalibrate.Contains(x, y)) return {Kind::Calibrate, -1, 0};
        if (kForget.Contains(x, y)) return {Kind::Forget, -1, 0};
        if (kGuideBtn.Contains(x, y)) return {Kind::Guide, -1, 0};
        if (kSpanDec.Contains(x, y)) return {Kind::Span, -1, -1};
        if (kSpanInc.Contains(x, y)) return {Kind::Span, -1, 1};
    }
    for (int r = 0; r < kRowsMax; ++r) {
        const int key = KeyAt(page, r, view);
        if (key < 0) break;
        if (IsToggle(kDefs[key])) {
            if (kToggle(r, page).Contains(x, y)) return {Kind::Change, key, 1};
        } else {
            if (kDec(r, page).Contains(x, y)) return {Kind::Change, key, -1};
            if (kInc(r, page).Contains(x, y)) return {Kind::Change, key, 1};
        }
    }
    return {};
}

// -- the panel's state ---------------------------------------------------------------------------
// GuideDone: the last page's Done - the window closes, the guide is over. Players: the game's own "/players N" (vr/players_cmd.h), nothing of the ini.
// Coop: a [relay] line of d2r-rooms.ini to write (the store's SubmitCoop), at once. Party: a press of the game's party buttons (vr/party_cmd.h).
// AutoTune: the Video page's Auto - autoTune 0 start a run, 1 Keep its result, 2 Undo it (or stop the run).
// MapShow: the game's own key command 10 / 11 (Show Party / Show Party Names on its automap, hset_party.h), nothing of the ini.
enum class Do { Nothing, Save, Close, Recentre, Calibrate, View, Preview, GuideDone, Players, Coop, Party, AutoTune, MapShow };
struct Action {
    Do what = Do::Nothing;
    Request request;   // with Save
    int view = 0;      // with View: 1 = F1 ... 4 = F4
    int players = 0;   // with Players: the count to ask the game for, 1 - 8
    coop::Edit coop;   // with Coop
    party::Request party;   // with Party
    int autoTune = 0;       // with AutoTune
    int mapShow = -1;       // with MapShow: party::kMapParty / kMapNames
};

class Model {
    Values base_ = Defaults(), baseFile_ = Defaults(), draft_ = Defaults();
    Values seen_ = Defaults(), seenFile_ = Defaults();   // the file as the store last read it (the Body page's measurement, a Forget's base)
    uint64_t serial_ = 0, pending_ = 0;
    Mask pendingMask_ = 0;       // the keys the Apply in flight writes
    video::Env env_;
    int view_ = 0;               // the game's view now, 1 = F1 ... 4 = F4, 0 = not known
    std::array<std::string, kBindCount> bindBase_;   // the binding keys' lines as the file had them when the draft began
    int resumePage_ = kPageBody;   // where the panel comes back from the measurement it handed the hands to: the Body page, or the Guide's
    bool guideShown_ = false, guideDone_ = false;   // the Guide page has been on show since the panel opened / Done was pressed
    Values liveSeen_ = Defaults();   // the draft as the last Live call saw it
    double editAt_ = 0, firstAt_ = 0;   // the last / the first change of a live key that is not written yet (ms; 0: none)
    float spanInUse_ = 0.0f;            // the span the hero's arms are fitted to now (the plugin's; 0 none)
    Players players_;                   // the game's players count (the plugin's)
    int asked_ = 0;                     // the count the row asked the game for and the game does not show yet (0: none)
    double askedAt_ = 0, clock_ = 0;    // when it was asked; the last clock SetPlayers was given (ms)
    int answer_ = 0;                    // the last ask's answer: 1 the game took it, -1 it did not (in time), 0 none since the panel opened
    party::View party_;                 // the Party page: the game's roster as the plugin read it
    stats::View stats_;                 // the Stats page: the strike statistics as the plugin copied them
    int statsHero_ = -1;                // ... the hero shown (an index into them; -1: the one playing)
    bool statsAll_ = false;             // ... all time (else this game)
    AutoTune auto_;                     // the Video page's Auto: the plugin's run
    party::Request partyAsked_;         // a press on its way to the game (what None: none)
    party::MapShow map_;                // the Co-op page's map row: the game's Show Party / Show Party Names as the plugin read them
    int mapAsked_ = -1;                 // the switch pressed and not shown flipped by the game yet (-1: none) ...
    int mapFrom_ = -1;                  // ... and what it was then
    double mapAskedAt_ = 0, mapClock_ = 0;
    double partyAskedAt_ = 0, partyClock_ = 0;
    std::shared_ptr<const coop::State> coop_;   // the Co-op page: D2R Rooms' files as last read
    std::optional<coop::Edit> coopPending_;      // a [relay] line on its way to d2r-rooms.ini (shown at once)
    uint64_t coopSerial_ = 0;
    Result coopResult_ = Result::None;
    void SeenCoop(const std::shared_ptr<const coop::State>& c) {
        if (c == coop_) return;
        coop_ = c;
        ++revision;
        if (!coop_) return;
        if (coopPending_ && coop_->written >= coopPending_->sequence) {   // the thread carried it out (or a later one)
            coopResult_ = coop_->written == coopPending_->sequence && coop_->writeFailed ? Result::Failed : Result::Saved;
            coopPending_.reset();
        }
        coopSerial_ = std::max(coopSerial_, coop_->written);   // (the thread outlives a panel: a new one's first edit is past the last it did)
    }
    void Seen(const Snapshot& s) {
        SeenCoop(s.coop);
        const body::Stored was = Measured();
        seen_ = s.values;
        seenFile_ = s.file;
        // the measurement's keys are the plugin's too (a new measurement writes them): one nobody is editing here follows the file at once, other
        // edits waiting or not - the Arm span stepper moves to the new measurement (the author, 2026-10-25: "the slider moves to the new measurement")
        for (const int k : {kKeyUse, kKeyVersion, kKeyEye, kKeySpan})
            if (!Differ(base_[k], draft_[k]) && (Differ(base_[k], s.values[k]) || Differ(baseFile_[k], s.file[k]))) {
                base_[k] = draft_[k] = liveSeen_[k] = s.values[k];
                baseFile_[k] = s.file[k];
                ++revision;
            }
        const body::Stored now = Measured();
        if (was.any != now.any || was.valid != now.valid || was.use != now.use || Differ(was.eye, now.eye) || Differ(was.span, now.span)) ++revision;
    }
    // The Video page's choice into the two keys; the draft's own choice again is no change.
    void SetPick(const video::Pick& want) {
        if (want == DraftPick()) return;
        const video::Keys k = video::KeysFor(want, draft_[kKeyPx]);
        draft_[kKeyMode] = Normalize(kKeyMode, k.mode);
        draft_[kKeyPx] = Normalize(kKeyPx, k.px);
        result = Result::None;
        ++revision;
    }
public:
    bool open = false, ready = false;
    int page = 0;
    int guide = 1;           // the Guide page's page: firststart::kOfferPage (0) or 1 .. kGuidePages
    Result result = Result::None;
    uint64_t revision = 0;   // moves with everything the picture shows

    const Values& Get() const { return draft_; }
    bool Dirty() const { return Changed(base_, draft_) != 0; }
    bool Busy() const { return pending_ != 0; }
    bool Edited(int key) const {   // changed since the file was read
        if (key == kKeyBarPlace && Differ(base_[kKeyHide], draft_[kKeyHide])) return true;   // the toolbar type is both keys
        return key >= 0 && key < kCount && Differ(base_[key], draft_[key]);
    }
    // What the file says of a binding key's line when the draft began ("" if it is not there), for a Custom one to show.
    const std::string& BindRaw(int key) const { return bindBase_[BindSlot(key)]; }
    // Only live keys (Def::live) are changed: they are written by themselves, nothing waits for Apply.
    bool LiveOnly() const { return (Changed(base_, draft_) & ~kLiveMask) == 0; }
    // The render size the draft makes, and the one the file has.
    video::Pick DraftPick() const { return video::PickOf(draft_[kKeyMode], draft_[kKeyPx]); }
    video::Pick FilePick() const { return video::PickOf(base_[kKeyMode], base_[kKeyPx]); }
    bool VideoEdited() const { return Edited(kKeyMode) || Edited(kKeyPx); }
    // What the file says of the T-pose measurement, always the latest (not a draft: the plugin writes it) - read as the draft plays it
    // (standing / seated: the row above it, at once).
    body::Stored Measured() const { return body::Of(seen_[kKeyUse], seen_[kKeyVersion], seen_[kKeyEye], seen_[kKeySpan], draft_[kKeySeated]); }
    // Standing / seated is still on its way to the file (a live key, written a moment after the click): a measurement now would be
    // taken the other way, so Calibrate waits for it.
    bool PlayPending() const { return Edited(kKeySeated); }
    // What the plugin knows of the picture (the game's size now, the headset's numbers); the picture follows it.
    const video::Env& Env() const { return env_; }
    // The game's view now (the plugin's, 1 = F1 ... 4 = F4; 0 = not known): the button of it is lit, and the pages and rows shown are the view's
    // (hset_defs.h PageShown, KeyShown: VR F1 - F3 not F4's body). A page the view does not show gives way to the first one.
    int View() const { return view_; }
    void SetView(int v) {
        v = v >= 1 && v <= kViews ? v : 0;
        if (v == view_) return;
        view_ = v;
        if (!PageShown(page, view_)) page = kPageView;
        ++revision;
    }
    // The Body page's Arm span: the span the hero's arms are fitted to now (from the plugin), shown while none is stored.
    void SetSpanInUse(float m) {
        m = std::isfinite(m) && m > 0.0f ? m : 0.0f;
        if (!Differ(m, spanInUse_)) return;
        spanInUse_ = m;
        ++revision;
    }
    // What the Arm span shows: a span set here and not written yet, else the stored one, else (none stored) the one in use; and whether it is stored.
    bool SpanStored() const { return Edited(kKeySpan) || Measured().valid; }
    float SpanShown() const { return Edited(kKeySpan) ? draft_[kKeySpan] : Measured().valid ? Measured().span : spanInUse_; }
    void SetEnv(const video::Env& e) {
        if (e == env_) return;
        env_ = e;
        ++revision;
    }
    // The Options page's Players row: what the game says now (the plugin's, every frame the panel is up) and a clock (ms). The count asked for
    // stays on show until the game shows it (the game took it) or kPlayersWaitMs passes (it did not: single player or the host only).
    void SetPlayers(const Players& p, double ms) {
        clock_ = ms;
        if (p != players_) { players_ = p; ++revision; }
        if (!asked_) return;
        if (p.count == asked_) { asked_ = 0; answer_ = 1; ++revision; }
        else if (ms - askedAt_ > kPlayersWaitMs) { asked_ = 0; answer_ = -1; ++revision; }
    }
    const Players& GamePlayers() const { return players_; }
    // The Video page's Auto: the plugin's run, every frame the page is up.
    void SetAuto(const AutoTune& a) {
        if (a == auto_) return;
        auto_ = a;
        ++revision;
    }
    const AutoTune& Auto() const { return auto_; }
    bool AutoShown() const { return auto_.on && auto_.state != 0; }   // the page shows the run, not the render size
    bool PlayersOn() const { return draft_[kKeyPlayersOn] > 0.5f; }   // [openxr] headset_players
    // - / + do something: the switch on, the game's count known and the game taking the command now, no ask on its way.
    bool PlayersUsable() const { return ready && PlayersOn() && players_.count >= kPlayersMin && players_.takes && !asked_; }
    int PlayersShown() const { return asked_ ? asked_ : players_.count; }   // 0: not known
    bool PlayersAsked() const { return asked_ != 0; }
    int PlayersAnswer() const { return answer_; }

    // The Party page: the game's roster as the plugin read it (every frame the page is up) and a clock (ms). A press waits for the game's answer -
    // the player's state moves, our party changes - or party::kWaitMs, before the buttons take another.
    void SetParty(const party::View& v, double ms) {
        partyClock_ = ms;
        if (v != party_) {
            const bool answered = partyAsked_.what != party::Ask::None && (v.inParty != party_.inParty || v.others != party_.others);
            party_ = v;
            if (answered) partyAsked_ = {};
            ++revision;
        }
        if (partyAsked_.what != party::Ask::None && ms - partyAskedAt_ > party::kWaitMs) { partyAsked_ = {}; ++revision; }
    }
    const party::View& Party() const { return party_; }
    // The Stats page: the strike statistics as the plugin copied them (while the page is up, when they change).
    void SetStats(const stats::View& v) {
        if (v == stats_) return;
        stats_ = v;
        if (statsHero_ >= (int)stats_.heroes.size()) statsHero_ = -1;
        ++revision;
    }
    const stats::View& Stats() const { return stats_; }
    // the hero the page shows: the one picked with < >, else the one playing, else the first; -1 none
    int StatsHero() const {
        const int n = (int)stats_.heroes.size();
        if (!n) return -1;
        if (statsHero_ >= 0 && statsHero_ < n) return statsHero_;
        return stats_.current >= 0 && stats_.current < n ? stats_.current : 0;
    }
    bool StatsAllTime() const { return statsAll_; }
    // The Co-op page's map row ([openxr] headset_map_party): the game's two settings as read now (the plugin's, every frame the page is up) and a
    // clock (ms). A press waits for the game to show it flipped, or party::kWaitMs, before the switches take another.
    void SetMapShow(const party::MapShow& v, double ms) {
        mapClock_ = ms;
        if (v != map_) { map_ = v; ++revision; }
        if (mapAsked_ < 0) return;
        if (map_.Of(mapAsked_) != mapFrom_ || ms - mapAskedAt_ > party::kWaitMs) { mapAsked_ = -1; ++revision; }
    }
    const party::MapShow& GameMap() const { return map_; }
    bool MapShowOn() const { return draft_[kKeyMapPartyOn] > 0.5f; }   // [openxr] headset_map_party: the row is there
    bool MapBusy() const { return mapAsked_ >= 0; }
    int MapAsked() const { return mapAsked_; }
    // What a switch shows: the game's value, flipped while a press of it is on its way (amber); -1 not known.
    int MapShown(int what) const {
        const int v = map_.Of(what);
        return what == mapAsked_ && v >= 0 ? 1 - v : v;
    }
    bool PartyOn() const { return draft_[kKeyPartyOn] > 0.5f; }   // [openxr] headset_party
    bool PartyBusy() const { return partyAsked_.what != party::Ask::None; }
    const party::Request& PartyAsked() const { return partyAsked_; }

    // The Co-op page: what was read of D2R Rooms' files (null: nothing yet), its choices as shown (an edit on its way in place), and that edit's fate.
    const coop::State* Coop() const { return coop_.get(); }
    roomsbridge::Config CoopShown() const { return coop_ ? coop::Shown(coop_->cfg, coopPending_ ? &*coopPending_ : nullptr) : roomsbridge::Config{}; }
    bool CoopBusy() const { return coopPending_.has_value(); }
    bool CoopReady() const { return coop_ && coop_->installed; }
    Result CoopResult() const { return coopPending_ ? Result::Saving : coopResult_; }
    // The edit could not be handed to the store's thread.
    void CoopSubmitFailed(uint64_t sequence) {
        if (!coopPending_ || coopPending_->sequence != sequence) return;
        coopPending_.reset();
        coopResult_ = Result::Failed;
        ++revision;
    }

    // The first-start guide's page (vr/first_start.h): the window comes up on it (the guide asks for it on the first start, or the Body page's button).
    void OpenGuide(int sub) {
        page = kPageGuide;
        guide = std::clamp(sub, firststart::kOfferPage, firststart::kGuidePages);
        guideShown_ = true;
        ++revision;
    }
    // The Guide page was on show since the panel opened (so that closing the panel ends the guide), and Done was pressed.
    bool GuideShown() const { return guideShown_; }
    bool GuideFinished() const { return guideDone_; }

    void Open(const Snapshot& s) {
        open = true;
        page = 0;
        guide = 1;
        guideShown_ = guideDone_ = false;
        resumePage_ = kPageBody;
        base_ = draft_ = s.values;
        baseFile_ = s.file;
        bindBase_ = s.bind;
        liveSeen_ = draft_;
        editAt_ = firstAt_ = 0;
        asked_ = answer_ = 0;
        partyAsked_ = {};
        coopPending_.reset();   // (one still on its way is the thread's: the file shows it when it is done)
        coopResult_ = Result::None;
        Seen(s);
        ready = s.ready;
        result = s.result == Result::Saving ? Result::None : s.result;
        serial_ = std::max(serial_, s.sequence);
        pending_ = 0;
        pendingMask_ = 0;
        ++revision;
    }
    // The panel comes back after the measurement it handed the hands to: on the page it asked from (the Body page; the Guide's offer: the guide's
    // first page), with what the player had not applied still in the draft (a fresh start when there was nothing).
    void Resume(const Snapshot& s) {
        const int back = resumePage_, sub = guide;
        if (!Dirty()) {
            Open(s);
        } else {
            open = true;
            ready = s.ready;
            Seen(s);
            pending_ = 0;
            pendingMask_ = 0;
            serial_ = std::max(serial_, s.sequence);
        }
        resumePage_ = kPageBody;
        page = back;
        if (back == kPageGuide) OpenGuide(sub);
        result = Result::None;
        ++revision;
    }
    void Close() { open = false; ++revision; }

    // The store's latest. After an Apply it brings the answer; otherwise, while nothing is being edited, an
    // outside change of the file is taken over.
    void Observe(const Snapshot& s) {
        if (!open) return;
        Seen(s);
        if (pending_) {
            if (s.sequence == pending_ && s.result != Result::Saving) {
                const Mask mine = pendingMask_;
                pending_ = 0;
                pendingMask_ = 0;
                result = s.result;
                ready = s.ready;
                if (s.result == Result::Conflict) {
                    base_ = draft_ = s.values;
                    baseFile_ = s.file;
                    bindBase_ = s.bind;
                } else if (s.result == Result::Saved) {
                    // what was written, and every key nobody was editing, comes from the file; an edit made meanwhile to a key this
                    // save did not carry (a Forget while a size is picked) stays in the draft
                    for (int i = 0; i < kCount; ++i)
                        if ((mine & Bit(i)) || !Differ(base_[i], draft_[i])) {
                            base_[i] = draft_[i] = s.values[i];
                            baseFile_[i] = s.file[i];
                            if (IsBindKey(i)) bindBase_[BindSlot(i)] = s.bind[BindSlot(i)];
                        }
                }
                ++revision;
            }
        } else if (!Dirty() && (Changed(base_, s.values) || Changed(baseFile_, s.file) || s.bind != bindBase_ || ready != s.ready)) {
            base_ = draft_ = s.values;
            baseFile_ = s.file;
            bindBase_ = s.bind;
            ready = s.ready;
            ++revision;
        }
    }
    // The Apply could not even be handed over.
    void SubmitFailed(uint64_t sequence) {
        if (pending_ != sequence) return;
        pending_ = 0;
        pendingMask_ = 0;
        result = Result::Failed;
        ++revision;
    }

    Action Click(const Hit& hit) {
        Action a;
        if (!open) return a;
        switch (hit.kind) {
            case Kind::Close: a.what = Do::Close; return a;
            case Kind::Recentre: a.what = Do::Recentre; return a;
            case Kind::Guide: OpenGuide(1); return a;   // the first-start guide: its page, here, from the first (nothing of the ini)
            case Kind::GuideBack:   // the offer's Not now goes on to the guide; the first page has nothing before it
                if (page != kPageGuide) return a;
                if (guide == firststart::kOfferPage) guide = 1;
                else if (guide > 1) --guide;
                else return a;
                ++revision;
                return a;
            case Kind::GuideNext:
                if (page != kPageGuide) return a;
                if (guide == firststart::kOfferPage) {   // Measure now: the measurement writes the ini; the panel comes back on the guide's first page
                    if (!ready || Busy() || PlayPending()) return a;
                    resumePage_ = kPageGuide;
                    guide = 1;
                    a.what = Do::Calibrate;
                    return a;
                }
                if (guide >= firststart::kGuidePages) {   // Done
                    guideDone_ = true;
                    a.what = Do::GuideDone;
                    return a;
                }
                ++guide;
                ++revision;
                return a;
            case Kind::Tab:
                if (hit.index == kPageParty && !PartyOn()) return a;   // the Co-op page's Party button is not there while the page is off
                if (!PageShown(hit.index, view_)) return a;           // (a page of F4's body, in VR F1 - F3)
                if (hit.index >= 0 && hit.index < PageCount() && hit.index != page) {
                    page = hit.index;
                    if (page == kPageGuide) guideShown_ = true;
                    ++revision;
                }
                return a;
            case Kind::Stats: {   // the Stats page: the next / the last hero (round the list), this game / all time - nothing of the ini
                if (page != kPageStats) return a;
                if (hit.index == 1) {
                    if (statsAll_ != (hit.delta > 0)) { statsAll_ = hit.delta > 0; ++revision; }
                    return a;
                }
                const int n = (int)stats_.heroes.size();
                if (n < 2 || hit.delta == 0) return a;
                statsHero_ = ((StatsHero() + (hit.delta > 0 ? 1 : -1)) % n + n) % n;
                ++revision;
                return a;
            }
            case Kind::Party: {   // a player's button: what the game's Party panel's button does by the state the game keeps for him
                if (page != kPageParty || !PartyOn() || !party_.known || PartyBusy() || hit.index < 0 || hit.index >= (int)party_.others.size()) return a;
                const party::Member& p = party_.others[(size_t)hit.index];
                const party::Act act = party::ActFor(p.state);
                if (act == party::Act::None) return a;
                partyAsked_ = {party::Ask::Player, p.id, p.state, act};
                partyAskedAt_ = partyClock_;
                ++revision;
                a.what = Do::Party;
                a.party = partyAsked_;
                return a;
            }
            case Kind::PartyLeave:   // the game's Party panel's Leave button: only while we are in a party
                if (page != kPageParty || !PartyOn() || !party_.known || !party_.inParty || PartyBusy()) return a;
                partyAsked_ = {party::Ask::Leave, 0, 0, party::Act::None};
                partyAskedAt_ = partyClock_;
                ++revision;
                a.what = Do::Party;
                a.party = partyAsked_;
                return a;
            case Kind::MapShow:   // the game's Show Party / Show Party Names: its own key command flips it (the game keeps it in its Settings.json)
                if (page != kPageCoop || !MapShowOn() || MapBusy() || hit.index < 0 || hit.index >= party::kMapCount || map_.Of(hit.index) < 0) return a;
                mapAsked_ = hit.index;
                mapFrom_ = map_.Of(hit.index);
                mapAskedAt_ = mapClock_;
                ++revision;
                a.what = Do::MapShow;
                a.mapShow = hit.index;
                return a;
            case Kind::PartyOpen:   // the game's own Party key command (P): its panel, for the laser (the game opens it in a multiplayer game only)
                if (page != kPageParty || !PartyOn() || !party_.known) return a;
                a.what = Do::Party;
                a.party = {party::Ask::OpenPanel, 0, 0, party::Act::None};
                return a;
            case Kind::Change: {
                if (hit.index == kKeyPlayers) {   // the game's players count: one "/players N" a press, nothing of the draft (the game keeps it)
                    if (!PlayersUsable() || hit.delta == 0) return a;
                    const int to = std::clamp(players_.count + (hit.delta > 0 ? 1 : -1), kPlayersMin, kPlayersMax);
                    if (to == players_.count) return a;   // (at an end)
                    asked_ = to;
                    askedAt_ = clock_;
                    answer_ = 0;
                    ++revision;
                    a.what = Do::Players;
                    a.players = to;
                    return a;
                }
                if (!ready || Busy() || hit.index < 0 || hit.index >= kCount || !KeyShown(hit.index, view_)) return a;
                if (hit.index == kKeyBarPlace) {   // the HUD page's toolbar type: a step over bar_place and hide together (shared/toolbar_type.h)
                    int h = (int)std::lround(draft_[kKeyHide]), p = (int)std::lround(draft_[kKeyBarPlace]);
                    const float wasHide = draft_[kKeyHide], wasPlace = draft_[kKeyBarPlace];
                    toolbartype::Step(hit.delta, &h, &p);
                    draft_[kKeyHide] = Normalize(kKeyHide, (float)h);
                    draft_[kKeyBarPlace] = Normalize(kKeyBarPlace, (float)p);
                    if (Differ(wasHide, draft_[kKeyHide]) || Differ(wasPlace, draft_[kKeyBarPlace])) { result = Result::None; ++revision; }
                    return a;
                }
                const Def& d = kDefs[hit.index];
                float& v = draft_[hit.index];
                const float was = v;
                if (d.type == DefType::Choice) {   // round the list: past the last is the first, and a Custom line goes to either end
                    const int n = keys::PickCount(), at = (int)std::lround(v);
                    v = (float)(at >= n ? (hit.delta > 0 ? 0 : n - 1) : ((at + (hit.delta > 0 ? 1 : n - 1)) % n));
                } else {
                    v = IsToggle(d) ? (v > 0.5f ? 0.0f : 1.0f) : Normalize(hit.index, v + (float)hit.delta * d.step);
                }
                if (Differ(was, v)) { result = Result::None; ++revision; }
                return a;
            }
            case Kind::View:   // the view is the game's, not the ini's: no ini needed; the view it already is is not asked again
                if (hit.index < 1 || hit.index > kViews || hit.index == view_) return a;
                a.what = Do::View;
                a.view = hit.index;
                return a;
            case Kind::Coop: {   // a [relay] line of d2r-rooms.ini, written at once (no Apply: the plugin's file, not d2r_vr.ini); one at a time
                if (page != kPageCoop || !CoopReady() || CoopBusy() || hit.index < 0 || hit.index >= coop::kWhatCount) return a;
                const roomsbridge::Config shown = CoopShown();
                if (hit.index != coop::kEnabled && !shown.enabled) return a;   // (the bridge off: its two choices do nothing, as on Settings' tab)
                std::optional<coop::Edit> e = coop::Step(shown, hit.index, hit.delta);
                if (!e) return a;
                e->sequence = ++coopSerial_;
                coopPending_ = *e;
                coopResult_ = Result::None;
                ++revision;
                a.what = Do::Coop;
                a.coop = *e;
                return a;
            }
            case Kind::Preview:   // the plugin shows the vignette and plays the beat for five seconds: nothing of the ini, nothing of the draft is touched
                if (!IsLifePage(page)) return a;
                a.what = Do::Preview;
                return a;
            case Kind::Auto:   // start a run (nothing edited on the page: a size not applied would be measured as not there), Keep, Undo / Stop
                if (page != kPageVideo || !auto_.on) return a;
                if (hit.index == 0 && auto_.state == 0 && ready && !Busy() && !VideoEdited()) a.autoTune = 0;
                else if (hit.index == 1 && auto_.state == 2) a.autoTune = 1;
                else if (hit.index == 2 && auto_.state != 0) a.autoTune = 2;
                else return a;
                a.what = Do::AutoTune;
                return a;
            case Kind::Preset:
                if (AutoShown()) return a;   // (hidden under the run's lines)
                if (!ready || Busy() || hit.index < 0 || hit.index >= video::kPresetCount) return a;
                SetPick(video::PickOfPreset(hit.index));
                return a;
            case Kind::Slider: {
                if (!ready || Busy() || AutoShown()) return a;
                if (hit.index >= rendersize::kSideMin) {   // a place on the track
                    SetPick(video::Square(hit.index));
                } else if (hit.delta != 0) {   // a stepper at its end: the first press takes the side the slider rests at, the next ones step from it
                    const video::Pick now = DraftPick();
                    const int rest = draft_[kKeyPx] >= (float)rendersize::kSideMin ? (int)draft_[kKeyPx] : 2048;
                    SetPick(video::Square(now.kind == rendersize::Kind::Square ? video::StepSide(now.side, hit.delta > 0 ? 1 : -1) : rest));
                }
                return a;
            }
            case Kind::Span: {   // the arm span by hand: [calibration] arm_span_m a centimetre a press, written by itself (a live key), in use at once
                if (!ready || Busy() || hit.delta == 0) return a;
                const float was = SpanShown(), to = body::StepSpan(was, hit.delta);
                if (SpanStored() && !Differ(was, to)) return a;   // (at an end)
                draft_[kKeySpan] = to;
                draft_[kKeyVersion] = 1.0f;   // a span stored ...
                draft_[kKeyUse] = 1.0f;       // ... and in use: the stepper shows the span in use, so moving it is using it
                result = Result::None;
                ++revision;
                return a;
            }
            case Kind::Calibrate:   // the measurement writes the ini; without one there is nowhere to put it (and standing / seated is in it first)
                if (!ready || Busy() || PlayPending()) return a;
                a.what = Do::Calibrate;
                return a;
            case Kind::Forget: {   // clears the stored measurement: [calibration] version, eye_height_m, arm_span_m (what the PC window's button does)
                if (!ready || Busy() || !Measured().any) return a;
                const Mask mask = Bit(kKeyVersion) | Bit(kKeyEye) | Bit(kKeySpan);
                pending_ = ++serial_;
                pendingMask_ = mask;
                result = Result::Saving;
                ++revision;
                a.what = Do::Save;
                a.request.base = seenFile_;
                a.request.values = seen_;
                for (int k : {kKeyVersion, kKeyEye, kKeySpan}) a.request.values[k] = 0.0f;
                a.request.changed = mask;
                a.request.sequence = pending_;
                return a;
            }
            case Kind::Apply:
                if (!ready || Busy() || !Dirty()) return a;
                pending_ = ++serial_;
                pendingMask_ = Changed(base_, draft_);
                // The render size is one value in two keys: written together, so a change of either made elsewhere meanwhile is a conflict
                // (choosing the headset's own size while the file holds a side must not turn into another side if the PC window moved it)
                if (pendingMask_ & (Bit(kKeyMode) | Bit(kKeyPx))) pendingMask_ |= Bit(kKeyMode) | Bit(kKeyPx);
                result = Result::Saving;
                ++revision;
                a.what = Do::Save;
                a.request.base = baseFile_;
                a.request.values = draft_;
                a.request.changed = pendingMask_;
                a.request.sequence = pending_;
                a.request.bind = bindBase_;
                return a;
            default: return a;
        }
    }

    // The live keys (Def::live: the belt) are written by themselves, as D2R VR Settings' sliders are: a moment
    // (kLiveSettleMs) after the last change, or at the latest kLiveMaxMs after the first one waiting, so a held button moves the thing
    // while it is held. Only the live keys go; what else is changed waits for Apply. Called every frame the panel is up; `ms` a clock.
    static constexpr double kLiveSettleMs = 300.0, kLiveMaxMs = 700.0;
    Action Live(double ms) {
        Action a;
        if (!open || !ready) return a;
        const Mask live = Changed(base_, draft_) & kLiveMask;
        if (!live) { firstAt_ = editAt_ = 0; liveSeen_ = draft_; return a; }
        if (firstAt_ == 0) firstAt_ = editAt_ = ms;
        for (int i = 0; i < kCount; ++i)
            if ((live & Bit(i)) && Differ(liveSeen_[i], draft_[i])) { editAt_ = ms; break; }
        liveSeen_ = draft_;
        if (Busy()) return a;
        if (ms - editAt_ < kLiveSettleMs && ms - firstAt_ < kLiveMaxMs) return a;
        firstAt_ = editAt_ = 0;
        pending_ = ++serial_;
        pendingMask_ = live;
        result = Result::Saving;
        ++revision;
        a.what = Do::Save;
        a.request.base = baseFile_;
        a.request.values = draft_;
        a.request.changed = live;
        a.request.sequence = pending_;
        a.request.bind = bindBase_;
        return a;
    }
};

}  // namespace hset
