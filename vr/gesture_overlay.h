#pragma once
// [openxr] gesture_overlay: the skill panel's GESTURES tab - the player's BodyWalk gestures managed from inside the game, in the tiles window
// (vr/tiles_geom.h; the same window, swapchain, laser and trigger as the Skills and Settings tabs). This is its brain and its layout: which
// screen is up, what the beam is on, what a click asks BodyWalk for (Game Link's GestureCmds, vr/gesture_cmds.h), and what the painter shows
// (View). Pure: no Windows, no OpenXR, no game - a frame's pointer and BodyWalk's Gestures block in, a command out
// (cleanroom/tests/gesture_overlay_test.cpp). No gesture logic: BodyWalk records, recognises and keeps them; this only shows and asks.
//
// Screens:
//   List      - a row per gesture of the class: its picture | its name and the skill it casts | its type, hand and threshold (< 80 % >) |
//               Re-record, Clear (asks once more), Bind (a skill picker); NEW GESTURE at the foot; Previous / Next past six. A click on a
//               glyph's or a motion's picture gives it the next colour.
//   NewType   - Glyph / Hand pose / Body motion;  NewHand - Left / Right (a pose also Both);  NewSkill - the class's skills, a grid of tiles:
//               the pick asks BodyWalk to RECORD a new gesture of that type and hand for that skill.
//   BindSkill - the same grid for a gesture already there (and No skill).
//   Recording - what to do for the type, how BodyWalk's recording stands (READY -> DRAWING). While it records the trigger is BodyWalk's
//               pen: the window takes no click and the beam is hidden; the window stays where it is, the trail drawn over it (xr.cpp);
//               B / Y (the window's close keys) STOP it. A take that failed offers Try again / Back.
//   Review    - the new take's picture big: Keep / Re-record / Discard.
// The back key steps back through the screens before the list (WantsBack); on the list it closes the window as ever.
//
// When there is nothing to manage the list says which piece is missing, with the fix right there where there is one (Serg, 2026-11-07: "there
// should be a button to switch it on"): the skill link off in d2r_vr.ini -> ENABLE (the host writes [gestures] skill_link=1, the plugin takes the
// ini live); no BodyWalk running -> START BODYWALK (the host starts the copy D2R VR Setup put in, never a second one); a BodyWalk that runs but
// does not answer, takes no skills or writes no gestures -> what to update (it has no switch of its own for this: the game's skill link is it).

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gesture_cmds.h"
#include "skill_link_abi.h"
#include "wheel_types.h"   // wheel::Skill: the hero's skills, as the skill panel's snapshot has them

namespace gov {

// -- the layout (the tiles window's pixels; tiles::kW x kH - checked in the test) --------------------------------------------------------------

constexpr int kW = 1600, kH = 1100;

struct R {
    float l = 0, t = 0, r = 0, b = 0;
    bool Has(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
};
inline R Inflate(R a, float d) { return {a.l - d, a.t - d, a.r + d, a.b + d}; }

// The cells: what a point of the window hits on a screen.
enum : int {
    kNone = -1,
    kNew = 1,        // List: NEW GESTURE
    kPrev = 2,       // List, the grids: the page before
    kNext = 3,       // ... the page after
    kBack = 4,       // the screens before the list, a failed take
    kKeep = 5,       // Review
    kRerecord = 6,   // Review
    kDiscard = 7,    // Review
    kStop = 8,       // Recording (shown; the trigger is BodyWalk's pen then - B / Y stop it)
    kRetry = 9,      // Recording, a failed take: Try again
    kNoSkill = 10,   // BindSkill: the gesture casts nothing
    kFix = 11,       // List, nothing to manage: ENABLE / START BODYWALK (Avail's fix)
    kChoice0 = 20,   // NewType / NewHand: the big choices, 20..22
    kRow0 = 100,     // List: kRow0 + row * 8 + part
    kGrid0 = 200,    // the grids: kGrid0 + 0..23
};
enum Part : int { kPic = 0, kRowRerecord = 1, kRowClear = 2, kRowBind = 3, kThrLess = 4, kThrMore = 5, kParts = 6 };
constexpr int RowCell(int row, int part) { return kRow0 + row * 8 + part; }
constexpr bool IsRowCell(int c) { return c >= kRow0 && c < kRow0 + 8 * 6 && (c - kRow0) % 8 < kParts; }
constexpr int RowOf(int c) { return (c - kRow0) / 8; }
constexpr int PartOf(int c) { return (c - kRow0) % 8; }

// The list's rows: six to a page, between the tab strip and the status lines.
constexpr int kRowsPerPage = 6;
constexpr float kRowTop = 138.0f, kRowH = 108.0f, kRowGap = 8.0f, kRowL = 60.0f, kRowR = 1540.0f;
inline R RowRect(int i) { const float t = kRowTop + i * (kRowH + kRowGap); return {kRowL, t, kRowR, t + kRowH}; }
inline R PicRect(int i) { const R r = RowRect(i); return {r.l + 10.0f, r.t + 4.0f, r.l + 110.0f, r.b - 4.0f}; }
inline R NameRect(int i) { const R r = RowRect(i); return {r.l + 128.0f, r.t + 8.0f, r.l + 676.0f, r.t + 58.0f}; }
inline R SkillLineRect(int i) { const R r = RowRect(i); return {r.l + 128.0f, r.t + 58.0f, r.l + 676.0f, r.b - 6.0f}; }
inline R TypeRect(int i) { const R r = RowRect(i); return {r.l + 686.0f, r.t + 8.0f, r.l + 896.0f, r.t + 54.0f}; }
inline R ThrTextRect(int i) { const R r = RowRect(i); return {r.l + 730.0f, r.t + 58.0f, r.l + 852.0f, r.b - 8.0f}; }
inline R PartRect(int i, int part) {
    const R r = RowRect(i);
    switch (part) {
    case kPic: return PicRect(i);
    case kThrLess: return {r.l + 686.0f, r.t + 58.0f, r.l + 730.0f, r.b - 8.0f};
    case kThrMore: return {r.l + 852.0f, r.t + 58.0f, r.l + 896.0f, r.b - 8.0f};
    case kRowRerecord: return {r.l + 908.0f, r.t + 20.0f, r.l + 1114.0f, r.b - 20.0f};   // (wide enough for the game's gem buttons' ends and the word)
    case kRowClear: return {r.l + 1124.0f, r.t + 20.0f, r.l + 1296.0f, r.b - 20.0f};
    case kRowBind: return {r.l + 1306.0f, r.t + 20.0f, r.r - 10.0f, r.b - 20.0f};
    default: return {};
    }
}

// The status lines (the tiles window's: tiles::kRedLine, kHintLine) and the footer's buttons (tiles::kFooterTop ..).
constexpr R kNoteLine{160.0f, 836.0f, 1440.0f, 886.0f};
constexpr R kHintLine{160.0f, 888.0f, 1440.0f, 926.0f};
constexpr float kFootT = 940.0f, kFootB = 1020.0f, kButtonW = 340.0f, kButtonGap = 24.0f;
inline R FooterRect(int index, int count) {
    const float total = count * kButtonW + (count - 1) * kButtonGap, l = 0.5f * (kW - total) + index * (kButtonW + kButtonGap);
    return {l, kFootT, l + kButtonW, kFootB};
}

// NewType / NewHand: big choices, one under another, in the middle.
inline R ChoiceRect(int i, int n) {
    const float h = 150.0f, gap = 32.0f, total = n * h + (n - 1) * gap, top = 150.0f + 0.5f * (680.0f - total);
    return {320.0f, top + i * (h + gap), 1280.0f, top + i * (h + gap) + h};
}

// The skill grids: 8 x 3 tiles with the name under each, the tiles window's editor's measures (tiles::GridRect) without its category strip.
constexpr int kGridCols = 8, kGridRows = 3, kGridCount = kGridCols * kGridRows;
constexpr float kCellTile = 150.0f, kCellGap = 16.0f, kCellH = 200.0f, kCellRowGap = 12.0f;
constexpr float kGridLeft = 0.5f * (kW - (kGridCols * kCellTile + (kGridCols - 1) * kCellGap)), kGridTop = 168.0f;
inline R GridRect(int i) {
    const float l = kGridLeft + (i % kGridCols) * (kCellTile + kCellGap), t = kGridTop + (i / kGridCols) * (kCellH + kCellRowGap);
    return {l, t, l + kCellTile, t + kCellH};
}
inline R GridTileRect(int i) { const R r = GridRect(i); return {r.l, r.t, r.r, r.t + kCellTile}; }
inline R GridNameRect(int i) { const R r = GridRect(i); return {r.l - 4.0f, r.t + kCellTile + 4.0f, r.r + 4.0f, r.b}; }

// Recording and Review.
constexpr R kRecTitle{140.0f, 150.0f, 1460.0f, 220.0f};     // "RECORDING: <type>, <hand>" / the gesture's name
constexpr R kRecText{160.0f, 236.0f, 1440.0f, 520.0f};      // what to do, three lines
constexpr R kRecState{420.0f, 560.0f, 1180.0f, 700.0f};     // READY / DRAWING... on a plate
constexpr R kReviewPic{540.0f, 140.0f, 1060.0f, 640.0f};
constexpr R kReviewName{160.0f, 660.0f, 1440.0f, 724.0f};
constexpr R kReviewLine{160.0f, 728.0f, 1440.0f, 780.0f};
// The list with nothing to manage: what is missing (a headline and a line of detail) and the fix under it.
constexpr R kMissTitle{140.0f, 300.0f, 1460.0f, 370.0f};
constexpr R kMissText{160.0f, 380.0f, 1440.0f, 500.0f};
constexpr R kFixRect{560.0f, 540.0f, 1040.0f, 630.0f};

// -- what the painter is given ----------------------------------------------------------------------------------------------------------------

enum class Screen : uint8_t { List, NewType, NewHand, NewSkill, BindSkill, Recording, Review };

// Whether there is anything to manage: the skill link's tick on, a BodyWalk writing its Gestures, a hero (BodyWalk's set = the class) - and
// when not, which piece is missing.
enum class Avail : uint8_t {
    Ok,
    LinkOff,      // d2r_vr.ini [gestures] skill_link=0: ENABLE
    NotRunning,   // no BodyWalkVR.exe runs: START BODYWALK
    Starting,     // we started it a moment ago and its Game Link is not up yet
    NoGameLink,   // BodyWalkVR.exe runs but its Game Link is still (starting, or a BodyWalk without it)
    NoSkills,     // its Game Link runs but it does not take the game's skills (a BodyWalk older than Game skills)
    NoGestures,   // it takes the skills but writes no Gestures (a BodyWalk older than the in-game gestures)
    NoEngine,     // [gestures] engine_in_game: the gesture engine (d2r_vr_gestures.dll) is not there or would not open
    NoHero,       // no hero in its list yet (in the game's menu)
};
// What the host knows when the Gestures list is not there (In::missing): one of Avail's NotRunning .. NoEngine.
using Missing = Avail;
// The fix the list offers for a missing piece.
enum class Fix : uint8_t { None, EnableLink, StartBodyWalk };
inline Fix FixFor(Avail a, bool canStart) { return a == Avail::LinkOff ? Fix::EnableLink : a == Avail::NotRunning && canStart ? Fix::StartBodyWalk : Fix::None; }

// The line under the screen (the painter words it): what happened last, or what is in the way.
enum class Note : uint8_t {
    None, NoAnswer, Busy, NoGesture, Bad, NoSet,      // BodyWalk's answers (BW_LINK_GR_*) and none at all
    Asked, Kept, Discarded, Cleared, Bound, Coloured, Threshold,
    ClearAgain, ChooseReview, NoSkills, Failed, Cancelled, Stopped,
};

struct Row {
    BW_LinkGesture g;
    char skill[wheel::kNameLen];   // the skill it casts, in the game's words (empty: none / not known)
};

struct View {
    uint32_t revision;
    Screen screen;
    int hover;
    Avail avail;
    bool canStart;                 // a BodyWalkVR.exe was found to start (NotRunning: START BODYWALK; else where to get one)
    char setKey[32];
    int count, page, pages, rows;   // the class's gestures, the list's page and pages, the rows on this page
    Row row[kRowsPerPage];
    uint32_t clearArmed;           // the gesture whose Clear asks once more (its id), 0 none
    uint8_t newType, newHand, choices;   // NewHand / NewSkill: the type and hand chosen; how many big choices are shown
    int options, pickPage, pickPages;
    wheel::Skill option[kGridCount];
    int32_t bindSkill;             // BindSkill: what the gesture casts now (its tile framed)
    bool waiting;                  // a command not yet acted on by BodyWalk
    bool live;                     // Recording: BodyWalk records (or is about to): the trigger is its pen, no click here
    bool queued;                   // Recording: the RECORD waits for the triggers to be let go
    bool failed;                   // Recording: the take failed (or BodyWalk refused / did not answer) - Try again / Back
    uint32_t recordState;          // BW_LINK_REC_*
    uint8_t recType, recHand;
    char recSkill[wheel::kNameLen];
    char recName[48];              // a re-record: the gesture's name
    bool haveReview;
    Row review;
    Note note;
    bool lastShown;                // a stroke was read since BodyWalk started: the hint line says how it went
    float lastPercent;
    char lastName[48];             // the gesture it matched (empty: none over its threshold)
};

inline void Zero(View& v) { std::memset(&v, 0, sizeof v); v.hover = kNone; v.bindSkill = -1; v.lastPercent = -1.0f; }

// The footer's buttons on a screen.
inline int FooterCells(const View& v, int* out) {
    int n = 0;
    switch (v.screen) {
    case Screen::List: out[n++] = kPrev; out[n++] = kNew; out[n++] = kNext; break;
    case Screen::NewType:
    case Screen::NewHand: out[n++] = kBack; break;
    case Screen::NewSkill: out[n++] = kPrev; out[n++] = kNext; out[n++] = kBack; break;
    case Screen::BindSkill: out[n++] = kPrev; out[n++] = kNext; out[n++] = kNoSkill; out[n++] = kBack; break;
    case Screen::Recording:
        if (v.failed) { out[n++] = kRetry; out[n++] = kBack; }
        else out[n++] = kStop;
        break;
    case Screen::Review: out[n++] = kKeep; out[n++] = kRerecord; out[n++] = kDiscard; break;
    }
    return n;
}
// A footer button that does something now (the painter dims the rest; a click on one is refused).
inline bool FooterEnabled(const View& v, int cell) {
    if (v.waiting && cell != kBack) return false;
    switch (cell) {
    case kPrev: return v.screen == Screen::List ? v.page > 0 : v.pickPage > 0;
    case kNext: return v.screen == Screen::List ? v.page + 1 < v.pages : v.pickPage + 1 < v.pickPages;
    case kNew: return v.avail == Avail::Ok && v.count < (int)BW_LINK_GESTURES_MAX;
    case kFix: return FixFor(v.avail, v.canStart) != Fix::None;
    case kStop: return false;   // (B / Y: the trigger is the pen while recording)
    default: return true;
    }
}
// A row's part that can be clicked: the picture of a glyph / motion (its colour), Re-record (not a button: BodyWalk chooses those), Clear,
// Bind, the threshold's arrows (not a button).
inline bool PartUsable(const BW_LinkGesture& g, int part) {
    switch (part) {
    case kPic: return g.type == BW_LINK_GT_GLYPH || g.type == BW_LINK_GT_MOTION;
    case kRowRerecord: case kThrLess: case kThrMore: return g.type != BW_LINK_GT_BUTTON;
    case kRowClear: case kRowBind: return true;
    default: return false;
    }
}
inline const char* ScreenName(Screen s) {
    switch (s) {
    case Screen::List: return "list";
    case Screen::NewType: return "new: type";
    case Screen::NewHand: return "new: hand";
    case Screen::NewSkill: return "new: skill";
    case Screen::BindSkill: return "bind: skill";
    case Screen::Recording: return "recording";
    case Screen::Review: return "review";
    }
    return "?";
}
inline const char* StateName(uint32_t st) {
    static const char* const k[] = {"none", "ready", "drawing", "saved", "failed", "cancelled"};
    return st < 6 ? k[st] : "?";
}

// NewHand's choices, top down: Right (most players' pen hand), Left, and Both for a pose.
inline uint8_t HandOfChoice(int i) { return uint8_t(i == 0 ? BW_LINK_GH_RIGHT : i == 1 ? BW_LINK_GH_LEFT : BW_LINK_GH_BOTH); }
inline int ChoiceCount(const View& v) {
    if (v.screen == Screen::NewType) return 3;
    if (v.screen == Screen::NewHand) return v.newType == BW_LINK_GT_POSE ? 3 : 2;
    return 0;
}

inline bool CellRect(const View& v, int cell, R* out) {
    int cells[4];
    const int n = FooterCells(v, cells);
    for (int i = 0; i < n; ++i)
        if (cells[i] == cell) { *out = FooterRect(i, n); return true; }
    if (v.screen == Screen::List && IsRowCell(cell) && RowOf(cell) < v.rows) { *out = PartRect(RowOf(cell), PartOf(cell)); return true; }
    if (v.screen == Screen::List && cell == kFix && FixFor(v.avail, v.canStart) != Fix::None) { *out = kFixRect; return true; }
    if (cell >= kChoice0 && cell < kChoice0 + ChoiceCount(v)) { *out = ChoiceRect(cell - kChoice0, ChoiceCount(v)); return true; }
    if ((v.screen == Screen::NewSkill || v.screen == Screen::BindSkill) && cell >= kGrid0 && cell < kGrid0 + v.options) {
        *out = GridRect(cell - kGrid0);
        return true;
    }
    return false;
}

// What a point of the window (u, v in 0..1) hits on the screen shown; `previous` (the frame before's) is kept just past its edge.
inline int HitTest(const View& v, float u, float vv, int previous) {
    if (!std::isfinite(u) || !std::isfinite(vv) || u < 0.0f || u >= 1.0f || vv < 0.0f || vv >= 1.0f) return kNone;
    const float x = u * kW, y = vv * kH;
    int cells[4];
    const int n = FooterCells(v, cells);
    for (int i = 0; i < n; ++i)
        if (FooterRect(i, n).Has(x, y)) return cells[i];
    switch (v.screen) {
    case Screen::List:
        if (FixFor(v.avail, v.canStart) != Fix::None && kFixRect.Has(x, y)) return kFix;
        for (int i = 0; i < v.rows; ++i)
            for (int p = 0; p < kParts; ++p)
                if (PartUsable(v.row[i].g, p) && PartRect(i, p).Has(x, y)) return RowCell(i, p);
        break;
    case Screen::NewType:
    case Screen::NewHand:
        for (int i = 0; i < ChoiceCount(v); ++i)
            if (ChoiceRect(i, ChoiceCount(v)).Has(x, y)) return kChoice0 + i;
        break;
    case Screen::NewSkill:
    case Screen::BindSkill:
        for (int i = 0; i < v.options; ++i)
            if (GridRect(i).Has(x, y)) return kGrid0 + i;
        break;
    default: break;
    }
    R r;
    if (previous != kNone && CellRect(v, previous, &r) && Inflate(r, 8.0f).Has(x, y)) return previous;
    return kNone;
}

// -- the colours a click on a picture steps through (R in the low byte, A 255) ------------------------------------------------------------------

constexpr uint32_t kPalette[8] = {0xFF3C3CE6u, 0xFF2896F0u, 0xFF3CD2F0u, 0xFF50D25Au, 0xFFC8C83Cu, 0xFFF08C3Cu, 0xFFE65AB4u, 0xFFF0F0F0u};
inline uint32_t NextColour(uint32_t rgba) {
    int best = 0;
    long bestD = 0x7FFFFFFF;
    for (int i = 0; i < 8; ++i) {
        long d = 0;
        for (int k = 0; k < 3; ++k) {
            const long a = (long)((rgba >> (8 * k)) & 0xFF), b = (long)((kPalette[i] >> (8 * k)) & 0xFF);
            d += (a - b) * (a - b);
        }
        if (d < bestD) { bestD = d; best = i; }
    }
    return kPalette[(best + 1) % 8];
}
// The threshold a step of the arrows gives: on the 5 % grid, 50 .. 95.
inline float StepThreshold(float now, int dir) {
    const float base = std::isfinite(now) ? std::round(now / 5.0f) * 5.0f : 80.0f;
    return std::clamp(base + 5.0f * (dir < 0 ? -1.0f : 1.0f), 50.0f, 95.0f);
}

// The skills a gesture can cast, from the hero's (the skill panel's snapshot catalog): learned or given, not the plain actions (attack,
// pick-up), one entry per id; the class's trees first, in their order, then the rest. Returns how many.
inline int Bindable(const wheel::Skill* catalog, int count, wheel::Skill* out, int max) {
    int n = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < count && n < max; ++i) {
            const wheel::Skill& k = catalog[i];
            if (!k.known || !k.available || wheel::IsPlainAction(k.id)) continue;
            const bool tree = k.tree >= 1 && k.tree <= 3;
            if ((pass == 0) != tree) continue;
            bool dup = false;
            for (int j = 0; j < n && !dup; ++j) dup = out[j].id == k.id;
            if (!dup) std::memcpy(&out[n++], &k, sizeof k);
        }
    return n;
}

// -- the model ----------------------------------------------------------------------------------------------------------------------------------

struct In {
    double time = 0;              // seconds, monotonic
    bool shown = false;           // the window is up on the Gestures tab, its top level (and F4: the controller's gate)
    bool pointer = false;         // the beam is on the window
    float u = 0, v = 0;
    bool click = false;           // the trigger's edge
    bool back = false;            // a close key's edge (B / Y / menu): taken here only where WantsBack() said so
    bool link = false;            // [gestures] skill_link is on (the skills go to BodyWalk; it writes the Gestures block only then)
    const BW_LinkGestures* g = nullptr;   // BodyWalk's block, a consistent copy; null = no BodyWalk writes it
    Missing missing = Avail::NotRunning;  // ... and then why (the host's look at the process and Game Link): NotRunning .. NoGestures
    bool canStart = false;        // a BodyWalkVR.exe was found (START BODYWALK can do something)
    bool triggers = false;        // either hand's trigger is down: a RECORD waits until both are let go (the click's pull is never BodyWalk's pen)
    const wheel::Skill* catalog = nullptr;   // the hero's skills, for the names and the grids
    int catalogCount = 0;
};

enum class Feedback : uint8_t { None, Hover, Chosen, Refused, Applied };

// How long BodyWalk is given to act on a command (a minimised or busy BodyWalk ticks slowly), how long it may stop writing its Gestures before the
// screens give up and go back to the list, and how long after its answer a take must show READY / DRAWING (else BodyWalk did not start it).
constexpr double kAnswerS = 6.0, kLostS = 5.0, kStartS = 2.0;

// [debug] gesture_trace: what the model did this frame and why, a line each (the host writes them to the log with its own lines: the commands, the
// block, the pen). Kept even with the key off (a few bytes a frame); only the host decides whether they are written.
constexpr int kTraceMax = 8, kTraceLen = 120;

struct Out {
    int traces = 0;
    char trace[kTraceMax][kTraceLen];
    void Trace(const char* fmt, ...) {
        if (traces >= kTraceMax) return;
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(trace[traces++], kTraceLen, fmt, a);
        va_end(a);
    }
    bool send = false;            // `cmd` goes to BodyWalk (vr/link.cpp PushGestureCmd) - Sent() is told its sequence
    gesturecmd::Cmd cmd;
    Fix fix = Fix::None;          // the list's fix was clicked: the host enables the skill link / starts BodyWalk
    Feedback feedback = Feedback::None;
};

class Model {
    View v_;
    Screen screen_ = Screen::List;
    char setKey_[32] = {};
    int page_ = 0, pickPage_ = 0;
    uint8_t newType_ = BW_LINK_GT_GLYPH, newHand_ = BW_LINK_GH_RIGHT;
    uint32_t bindGesture_ = 0;
    uint32_t reviewId_ = 0;
    uint32_t recGesture_ = 0;      // the gesture being recorded as BodyWalk names it while it records (0: a new one with no take yet)
    uint32_t recTarget_ = 0;       // what our RECORD asked for: the gesture re-taken, 0 a new one (Try again asks the same - never BodyWalk's recordGesture
                                   // after the take: a failed new one's row is gone and that number then names another gesture)
    int32_t recSkill_ = -1;
    uint8_t recType_ = 0, recHand_ = 0;
    bool recAcked_ = false;        // BodyWalk took our RECORD: its record state is about ours ...
    bool recSeenLive_ = false;     // ... once it has shown READY / DRAWING for it (a SAVED / FAILED before that is the take before's)
    double recAckAt_ = 0;
    bool recQueued_ = false;       // the RECORD waits for the triggers to be let go
    bool failed_ = false;
    double lostAt_ = 0;            // BodyWalk's Gestures went away while a screen past the list was up (0: there)
    gesturecmd::Cmd recCmd_;       // the RECORD queued
    uint32_t traceState_ = ~0u, traceGesture_ = ~0u;   // (gesture_trace: the record state last told)
    uint32_t pending_ = 0;         // our last command's sequence, until BodyWalk acted on it (cmdDone)
    uint32_t pendingOp_ = 0;
    double pendingAt_ = 0;
    bool sentWait_ = false;        // Step asked for a command and Sent has not told its sequence yet
    uint32_t clearArmed_ = 0;
    double clearUntil_ = 0;
    Note note_ = Note::None;
    double noteUntil_ = 0;
    int hover_ = kNone;
    bool canStart_ = false;
    wheel::Skill picks_[wheel::kCatalogMax];
    int pickCount_ = 0;

    void Say(Note n, double now, double seconds = 3.0) { note_ = n; noteUntil_ = now + seconds; }
    static const BW_LinkGesture* Find(const BW_LinkGestures* g, uint32_t id) {
        if (!g || !id) return nullptr;
        const uint32_t n = std::min<uint32_t>(g->count, BW_LINK_GESTURES_MAX);
        for (uint32_t i = 0; i < n; ++i)
            if (g->g[i].id == id) return &g->g[i];
        return nullptr;
    }
    static void SkillName(const In& in, int32_t id, char* out, size_t size) {
        out[0] = 0;
        if (id < 0) return;
        for (int i = 0; i < in.catalogCount; ++i)
            if (in.catalog[i].id == id && in.catalog[i].name[0]) { std::snprintf(out, size, "%s", in.catalog[i].name); return; }
        std::snprintf(out, size, "#%d", (int)id);   // (a skill the hero has not now: its number)
    }
    bool Live(const BW_LinkGestures* g) const {
        if (screen_ != Screen::Recording || failed_) return false;
        if (recQueued_) return true;   // waiting for the triggers to be let go
        if (pendingOp_ == BW_LINK_GC_RECORD && (pending_ || sentWait_)) return true;   // asked, BodyWalk about to start
        if (recAcked_ && !recSeenLive_) return true;   // taken, its READY not seen yet
        return recAcked_ && g && (g->recordState == BW_LINK_REC_READY || g->recordState == BW_LINK_REC_DRAWING);
    }
    void Fail(Note n, double now, Out* out) {
        out->Trace("the take is not on: Try again / Back (note %d)", (int)n);
        failed_ = true;
        recQueued_ = false;
        Say(n, now, 6.0);
        out->feedback = Feedback::Refused;
    }
    void Ask(Out* out, double now, gesturecmd::Cmd c) {
        out->send = true;
        out->cmd = c;
        pendingOp_ = c.op;
        pendingAt_ = now;
        sentWait_ = true;
        pending_ = 0;
        if (c.op == BW_LINK_GC_RECORD) { recAcked_ = false; recSeenLive_ = false; failed_ = false; }
    }
    void Record(Out* out, double now, uint32_t gesture, uint8_t type, uint8_t hand, int32_t skill) {
        gesturecmd::Cmd c;
        c.op = BW_LINK_GC_RECORD;
        c.gesture = gesture;
        c.type = type;
        c.hand = hand;
        c.skill = skill;
        recGesture_ = gesture;
        recTarget_ = gesture;
        recType_ = type;
        recHand_ = hand;
        recSkill_ = skill;
        recCmd_ = c;
        recQueued_ = true;   // sent once both triggers are up (Step): the pull that clicked must not be the take's start
        recAcked_ = false;
        recSeenLive_ = false;
        failed_ = false;
        (void)out;
        (void)now;
        screen_ = Screen::Recording;
    }
    void OpenPicker(const In& in, Screen s) {
        pickCount_ = Bindable(in.catalog, in.catalogCount, picks_, wheel::kCatalogMax);
        pickPage_ = 0;
        screen_ = s;
    }

public:
    const View& Current() const { return v_; }
    Screen Shown() const { return screen_; }
    // The back key is this tab's on the screens before the list (it steps back; recording: stops), not the window's.
    bool WantsBack() const { return screen_ != Screen::List; }
    // BodyWalk records (or is about to) for this tab: the trigger is its pen - no click on the window, the beam hidden, the window out of the way.
    bool Recording() const { return v_.live; }
    // The sequence Out::cmd was given in the ring (the wiring's push); 0 = it could not be written (no block): the command is dropped.
    void Sent(uint32_t sequence, double now) {
        sentWait_ = false;
        if (sequence) {
            pending_ = sequence;
            pendingAt_ = now;
        } else {
            pendingOp_ = 0;
            if (screen_ == Screen::Recording) {   // (no block to write: the take did not start - Try again / Back)
                failed_ = true;
                recQueued_ = false;
            }
            Say(Note::NoAnswer, now);
        }
    }
    void Reset() { *this = Model{}; }

    Out Step(const In& in) {
        Out out;
        const Screen screenBefore = screen_;
        const BW_LinkGestures* g = in.link ? in.g : nullptr;
        const Avail avail = !in.link ? Avail::LinkOff
                          : !in.g ? (in.missing >= Avail::NotRunning && in.missing <= Avail::NoEngine ? in.missing : Avail::NotRunning)
                          : !in.g->setKey[0] ? Avail::NoHero : Avail::Ok;
        canStart_ = in.canStart;
        if (avail == Avail::Ok && std::strncmp(setKey_, g->setKey, sizeof setKey_) != 0) {   // another class (or the first look): the list, from its start
            std::memcpy(setKey_, g->setKey, sizeof setKey_);
            setKey_[sizeof setKey_ - 1] = 0;
            screen_ = Screen::List;
            page_ = 0;
            pending_ = 0;
            pendingOp_ = 0;
            sentWait_ = false;
        }
        // BodyWalk's Gestures gone while a screen past the list is up: a short hiccup (a busy, minimised BodyWalk) keeps the screen; kLostS of it, the list
        if (avail != Avail::Ok && screen_ != Screen::List) {
            if (!lostAt_) {
                lostAt_ = in.time;
                out.Trace("BodyWalk's Gestures gone (avail %d) on '%s': the screen kept for %.0f s", (int)avail, ScreenName(screen_), kLostS);
            }
            if (in.time - lostAt_ > kLostS) {   // nothing to ask: the list (it says why)
                out.Trace("BodyWalk's Gestures gone for %.0f s: back to the list", kLostS);
                screen_ = Screen::List;
                pending_ = 0;
                pendingOp_ = 0;
                sentWait_ = false;
                recQueued_ = false;
                lostAt_ = 0;
            }
        } else {
            if (lostAt_) out.Trace("BodyWalk's Gestures back after %.1f s", in.time - lostAt_);
            lostAt_ = 0;
        }

        // the RECORD queued: out once both triggers are up (and BodyWalk is there)
        if (recQueued_ && screen_ == Screen::Recording && !in.triggers && avail == Avail::Ok && !pending_ && !sentWait_ && in.shown) {
            recQueued_ = false;
            out.Trace("the triggers are up: the RECORD goes out (gesture %u, type %u, hand %u, skill %d)", recCmd_.gesture, (unsigned)recCmd_.type,
                      (unsigned)recCmd_.hand, (int)recCmd_.skill);
            Ask(&out, in.time, recCmd_);
        }

        // our last command: acted on, refused, or no answer
        if (pending_ && g && gesturecmd::Done(*g, pending_)) {
            const uint32_t r = g->cmdResult, op = pendingOp_;
            out.Trace("answer to #%u (%s): result %u after %.2f s (cmdDone %u)", pending_, gesturecmd::OpName(op), r, in.time - pendingAt_, g->cmdDone);
            pending_ = 0;
            pendingOp_ = 0;
            if (r != BW_LINK_GR_OK) {
                const Note why = r == BW_LINK_GR_BUSY ? Note::Busy : r == BW_LINK_GR_NO_GESTURE ? Note::NoGesture : r == BW_LINK_GR_NO_SET ? Note::NoSet : Note::Bad;
                Say(why, in.time);
                out.feedback = Feedback::Refused;
                if (op == BW_LINK_GC_RECORD && screen_ == Screen::Recording) Fail(why, in.time, &out);   // (Try again / Back: the flow is not thrown back)
                else if (screen_ == Screen::Review) screen_ = Screen::List;
            } else {
                out.feedback = Feedback::Applied;
                switch (op) {
                case BW_LINK_GC_RECORD: recAcked_ = true; recAckAt_ = in.time; break;
                case BW_LINK_GC_KEEP: Say(Note::Kept, in.time); break;
                case BW_LINK_GC_DISCARD: Say(Note::Discarded, in.time); break;
                case BW_LINK_GC_CLEAR: Say(Note::Cleared, in.time); break;
                case BW_LINK_GC_BIND: Say(Note::Bound, in.time); break;
                case BW_LINK_GC_COLOR: Say(Note::Coloured, in.time, 1.5); break;
                case BW_LINK_GC_THRESHOLD: Say(Note::Threshold, in.time, 1.5); break;
                case BW_LINK_GC_STOP: Say(Note::Stopped, in.time); break;
                default: break;
                }
            }
        } else if ((pending_ || sentWait_) && in.time - pendingAt_ > kAnswerS) {
            const uint32_t op = pendingOp_;
            out.Trace("TIMEOUT: no answer to #%u (%s) in %.0f s (BodyWalk's cmdDone %u)", pending_, gesturecmd::OpName(op), kAnswerS, g ? g->cmdDone : 0u);
            pending_ = 0;
            pendingOp_ = 0;
            sentWait_ = false;
            Say(Note::NoAnswer, in.time);
            out.feedback = Feedback::Refused;
            if (op == BW_LINK_GC_RECORD && screen_ == Screen::Recording) Fail(Note::NoAnswer, in.time, &out);   // (Try again / Back)
            else if (screen_ == Screen::Review) screen_ = Screen::List;
        }

        // the recording: BodyWalk's state, once it took our RECORD - and only after it showed READY / DRAWING for it (a SAVED, FAILED or CANCELLED
        // before that is the take before's, still in the block: never read as this one's)
        if (screen_ == Screen::Recording && recAcked_ && g && !failed_) {
            const uint32_t st = g->recordState;
            if (st != traceState_ || g->recordGesture != traceGesture_) {
                out.Trace("BodyWalk's recording: %s (gesture %u)%s", StateName(st), g->recordGesture,
                          !recSeenLive_ && st != BW_LINK_REC_READY && st != BW_LINK_REC_DRAWING ? " - before READY: the take before's, not read" : "");
                traceState_ = st;
                traceGesture_ = g->recordGesture;
            }
            if (st == BW_LINK_REC_READY || st == BW_LINK_REC_DRAWING) {
                recSeenLive_ = true;
                if (g->recordGesture) recGesture_ = g->recordGesture;
            } else if (recSeenLive_) {
                const BW_LinkGesture* rg = Find(g, g->recordGesture);
                const bool ours = rg && (recTarget_ == 0 || rg->id == recTarget_);
                if (st == BW_LINK_REC_SAVED && ours && (rg->flags & BW_LINK_GF_REVIEW)) {
                    reviewId_ = rg->id;
                    screen_ = Screen::Review;
                    out.feedback = Feedback::Applied;
                } else if (st == BW_LINK_REC_CANCELLED || st == BW_LINK_REC_NONE) {
                    screen_ = Screen::List;
                    Say(Note::Cancelled, in.time);
                } else if (st == BW_LINK_REC_FAILED) {
                    Fail(Note::Failed, in.time, &out);
                }
            } else if (in.time - recAckAt_ > kStartS) {   // taken, but no READY ever came: BodyWalk did not start the take
                out.Trace("TIMEOUT: BodyWalk took the RECORD but showed no READY in %.0f s", kStartS);
                Fail(Note::Failed, in.time, &out);
            }
        }
        if (screen_ == Screen::Review && !pending_ && !sentWait_) {   // the take is gone from review (kept or dropped in BodyWalk itself)
            const BW_LinkGesture* rg = Find(g, reviewId_);
            if (!rg || !(rg->flags & BW_LINK_GF_REVIEW)) screen_ = Screen::List;
        }

        // the window went away (closed, another tab): a recording of ours is stopped; the rest stays where it was
        if (!in.shown) {
            if (screen_ == Screen::Recording) {
                recQueued_ = false;   // (a RECORD not out yet is simply dropped)
                if (Live(g) && avail == Avail::Ok && (pending_ || sentWait_ || recAcked_)) {
                    gesturecmd::Cmd c;
                    c.op = BW_LINK_GC_STOP;
                    c.gesture = recGesture_;
                    Ask(&out, in.time, c);
                }
                screen_ = Screen::List;
            }
            hover_ = kNone;
            Build(in, g, avail);
            if (screen_ != screenBefore) out.Trace("screen '%s' -> '%s' (the window away)", ScreenName(screenBefore), ScreenName(screen_));
            return out;
        }

        // the back key
        if (in.back) {
            out.Trace("the back key on '%s'", ScreenName(screen_));
            switch (screen_) {
            case Screen::NewType: screen_ = Screen::List; break;
            case Screen::NewHand: screen_ = Screen::NewType; break;
            case Screen::NewSkill: screen_ = Screen::NewHand; break;
            case Screen::BindSkill: screen_ = Screen::List; break;
            case Screen::Recording:
                recQueued_ = false;
                if (Live(g) && avail == Avail::Ok && (pending_ || sentWait_ || recAcked_)) {
                    gesturecmd::Cmd c;
                    c.op = BW_LINK_GC_STOP;
                    c.gesture = recGesture_;
                    Ask(&out, in.time, c);
                }
                screen_ = Screen::List;
                break;
            case Screen::Review: Say(Note::ChooseReview, in.time); out.feedback = Feedback::Refused; break;
            default: break;
            }
        }
        if (clearArmed_ && in.time > clearUntil_) clearArmed_ = 0;

        Build(in, g, avail);   // (the hit test reads the screen as it is now)
        const bool blind = v_.live;
        const int hover = !in.pointer || blind ? int(kNone) : HitTest(v_, in.u, in.v, hover_);
        if (hover != hover_ && hover != kNone && out.feedback == Feedback::None) out.feedback = Feedback::Hover;
        hover_ = hover;
        if (in.click && !blind && hover != kNone && !out.send) {
            out.Trace("click on cell %d ('%s')", hover, ScreenName(screen_));
            Click(in, g, avail, hover, &out);
        }
        Build(in, g, avail);
        if (screen_ != screenBefore) out.Trace("screen '%s' -> '%s'", ScreenName(screenBefore), ScreenName(screen_));
        return out;
    }

private:
    void Click(const In& in, const BW_LinkGestures* g, Avail avail, int h, Out* out) {
        const bool busy = pending_ || sentWait_;
        int cells[4];
        const int n = FooterCells(v_, cells);
        for (int i = 0; i < n; ++i)
            if (cells[i] == h && !FooterEnabled(v_, h)) { out->feedback = Feedback::Refused; return; }
        if (busy && h != kBack) { out->feedback = Feedback::Refused; return; }
        if (avail != Avail::Ok && h != kBack && h != kFix) { out->feedback = Feedback::Refused; return; }   // (BodyWalk away a moment: nothing asked meanwhile)
        out->feedback = Feedback::Chosen;
        switch (screen_) {
        case Screen::List:
            if (h == kFix) out->fix = FixFor(avail, canStart_);
            else if (h == kPrev) page_ = std::max(0, page_ - 1);
            else if (h == kNext) page_ = std::min(v_.pages - 1, page_ + 1);
            else if (h == kNew) screen_ = Screen::NewType;
            else if (IsRowCell(h) && RowOf(h) < v_.rows && avail == Avail::Ok) {
                const BW_LinkGesture& r = v_.row[RowOf(h)].g;
                gesturecmd::Cmd c;
                c.gesture = r.id;
                switch (PartOf(h)) {
                case kPic: c.op = BW_LINK_GC_COLOR; c.rgba = NextColour(r.rgba); Ask(out, in.time, c); break;
                case kThrLess:
                case kThrMore: c.op = BW_LINK_GC_THRESHOLD; c.value = StepThreshold(r.threshold, PartOf(h) == kThrLess ? -1 : 1); Ask(out, in.time, c); break;
                case kRowRerecord: Record(out, in.time, r.id, r.type, r.hand, r.skill); break;
                case kRowClear:
                    if (clearArmed_ == r.id) {
                        clearArmed_ = 0;
                        c.op = BW_LINK_GC_CLEAR;
                        Ask(out, in.time, c);
                    } else {
                        clearArmed_ = r.id;
                        clearUntil_ = in.time + 3.0;
                        Say(Note::ClearAgain, in.time);
                    }
                    break;
                case kRowBind:
                    bindGesture_ = r.id;
                    OpenPicker(in, Screen::BindSkill);
                    break;
                default: break;
                }
            }
            break;
        case Screen::NewType:
            if (h == kBack) screen_ = Screen::List;
            else if (h >= kChoice0 && h < kChoice0 + 3) {
                newType_ = uint8_t(BW_LINK_GT_GLYPH + (h - kChoice0));
                screen_ = Screen::NewHand;
            }
            break;
        case Screen::NewHand:
            if (h == kBack) screen_ = Screen::NewType;
            else if (h >= kChoice0 && h < kChoice0 + ChoiceCount(v_)) {
                const int i = h - kChoice0;
                newHand_ = HandOfChoice(i);
                OpenPicker(in, Screen::NewSkill);
                if (!pickCount_) Say(Note::NoSkills, in.time);
            }
            break;
        case Screen::NewSkill:
        case Screen::BindSkill:
            if (h == kBack) screen_ = screen_ == Screen::NewSkill ? Screen::NewHand : Screen::List;
            else if (h == kPrev) pickPage_ = std::max(0, pickPage_ - 1);
            else if (h == kNext) pickPage_ = std::min(v_.pickPages - 1, pickPage_ + 1);
            else if (h == kNoSkill || (h >= kGrid0 && h < kGrid0 + v_.options)) {
                const int32_t skill = h == kNoSkill ? -1 : v_.option[h - kGrid0].id;
                if (avail != Avail::Ok) { out->feedback = Feedback::Refused; break; }
                if (screen_ == Screen::NewSkill) {
                    Record(out, in.time, 0, newType_, newHand_, skill);
                } else {
                    gesturecmd::Cmd c;
                    c.op = BW_LINK_GC_BIND;
                    c.gesture = bindGesture_;
                    c.skill = skill;
                    Ask(out, in.time, c);
                    screen_ = Screen::List;
                }
            }
            break;
        case Screen::Recording:
            if (h == kBack) screen_ = Screen::List;
            else if (h == kRetry && avail == Avail::Ok) Record(out, in.time, recTarget_, recType_, recHand_, recSkill_);   // (the same as asked: a new one again is a new one)
            break;
        case Screen::Review: {
            gesturecmd::Cmd c;
            c.gesture = reviewId_;
            if (h == kKeep) { c.op = BW_LINK_GC_KEEP; Ask(out, in.time, c); screen_ = Screen::List; }
            else if (h == kDiscard) { c.op = BW_LINK_GC_DISCARD; Ask(out, in.time, c); screen_ = Screen::List; }
            else if (h == kRerecord) {
                const BW_LinkGesture* rg = Find(g, reviewId_);
                Record(out, in.time, reviewId_, rg ? rg->type : recType_, rg ? rg->hand : recHand_, rg ? rg->skill : recSkill_);
            }
            break;
        }
        }
        if (screen_ == Screen::List && clearArmed_ && !(IsRowCell(h) && PartOf(h) == kRowClear)) clearArmed_ = 0;   // (any other click: Clear asks anew)
    }

    // The painter's copy, made fresh (zeroed first, so the revision's compare sees only what changed).
    void Build(const In& in, const BW_LinkGestures* g, Avail avail) {
        static View n;   // (one model per process; ~9 KB, not on the stack)
        Zero(n);
        n.screen = screen_;
        n.avail = avail;
        n.canStart = canStart_;
        n.hover = hover_;
        if (g) std::memcpy(n.setKey, g->setKey, sizeof n.setKey);
        n.setKey[sizeof n.setKey - 1] = 0;
        // the list's rows
        const BW_LinkGesture* list[BW_LINK_GESTURES_MAX];
        int count = 0;
        if (g && avail == Avail::Ok)
            for (uint32_t i = 0; i < std::min<uint32_t>(g->count, BW_LINK_GESTURES_MAX); ++i)
                if (g->g[i].id != 0) list[count++] = &g->g[i];
        n.count = count;
        n.pages = std::max(1, (count + kRowsPerPage - 1) / kRowsPerPage);
        page_ = std::clamp(page_, 0, n.pages - 1);
        n.page = page_;
        n.rows = std::max(0, std::min(kRowsPerPage, count - page_ * kRowsPerPage));
        for (int i = 0; i < n.rows; ++i) {
            std::memcpy(&n.row[i].g, list[page_ * kRowsPerPage + i], sizeof(BW_LinkGesture));
            n.row[i].g.name[sizeof n.row[i].g.name - 1] = 0;
            n.row[i].g.button[sizeof n.row[i].g.button - 1] = 0;
            SkillName(in, n.row[i].g.skill, n.row[i].skill, sizeof n.row[i].skill);
        }
        n.clearArmed = clearArmed_;
        n.newType = newType_;
        n.newHand = newHand_;
        // the grids
        if (screen_ == Screen::NewSkill || screen_ == Screen::BindSkill) {
            n.pickPages = std::max(1, (pickCount_ + kGridCount - 1) / kGridCount);
            pickPage_ = std::clamp(pickPage_, 0, n.pickPages - 1);
            n.pickPage = pickPage_;
            n.options = std::max(0, std::min(kGridCount, pickCount_ - pickPage_ * kGridCount));
            for (int i = 0; i < n.options; ++i) std::memcpy(&n.option[i], &picks_[pickPage_ * kGridCount + i], sizeof(wheel::Skill));
            const BW_LinkGesture* bg = screen_ == Screen::BindSkill ? Find(g, bindGesture_) : nullptr;
            n.bindSkill = bg ? bg->skill : -1;
        } else {
            n.pickPages = 1;
        }
        n.choices = (uint8_t)ChoiceCount(n);
        // recording / review
        n.waiting = pending_ != 0 || sentWait_;
        n.failed = screen_ == Screen::Recording && failed_;
        n.live = Live(g);
        n.queued = screen_ == Screen::Recording && recQueued_;
        n.recordState = recAcked_ && recSeenLive_ && g ? g->recordState : uint32_t(BW_LINK_REC_NONE);
        n.recType = recType_;
        n.recHand = recHand_;
        SkillName(in, recSkill_, n.recSkill, sizeof n.recSkill);
        if (const BW_LinkGesture* rg = recTarget_ ? Find(g, recTarget_) : nullptr) std::snprintf(n.recName, sizeof n.recName, "%.47s", rg->name);
        if (screen_ == Screen::Review)
            if (const BW_LinkGesture* rg = Find(g, reviewId_)) {
                n.haveReview = true;
                std::memcpy(&n.review.g, rg, sizeof *rg);
                n.review.g.name[sizeof n.review.g.name - 1] = 0;
                n.review.g.button[sizeof n.review.g.button - 1] = 0;
                SkillName(in, rg->skill, n.review.skill, sizeof n.review.skill);
            }
        if (note_ != Note::None && in.time > noteUntil_) note_ = Note::None;
        n.note = note_;
        // the last stroke read
        if (g && g->lastStamp) {
            n.lastShown = true;
            n.lastPercent = std::isfinite(g->lastPercent) ? g->lastPercent : -1.0f;
            if (const BW_LinkGesture* lg = Find(g, g->lastGesture)) std::snprintf(n.lastName, sizeof n.lastName, "%.47s", lg->name);
        }
        n.revision = v_.revision;
        if (std::memcmp(&n, &v_, sizeof n) != 0) {
            const uint32_t rev = v_.revision + 1;
            std::memcpy(&v_, &n, sizeof n);
            v_.revision = rev;
        }
    }
};

}  // namespace gov
