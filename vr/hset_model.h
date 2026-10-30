#pragma once
// The in-headset settings panel: what it shows, where its buttons are, what a click does. Pure - no
// Windows, no OpenXR - so the whole of it is tested without a headset (cleanroom-style: tools/hset_test.cpp).
//
// After bearstonem/d2r-pcvr's steamvr/vr_settings.h (MIT): a table of keys, a draft of the values, a hit
// test on the painted atlas, Apply writes only what changed, a conflict with an outside edit reloads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "hset_body.h"
#include "hset_defs.h"
#include "hset_keys.h"
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
// The tabs share the 960 pixels under the title in proportion to their names (the letters + 3): "Options" gets a wide one, "HUD" a narrow one, so a
// page more does not squeeze the long names out of their tab (eleven pages at an even 87 pixels would not hold "Options" in either painter).
constexpr int NameLen(const char* s) {
    int n = 0;
    while (s[n]) ++n;
    return n;
}
constexpr int TabWeightTo(int i) {   // the weights of the first i tabs
    int w = 0;
    for (int k = 0; k < i; ++k) w += NameLen(kPageNames[k]) + 3;
    return w;
}
constexpr int kTabX(int i) { return 32 + 960 * TabWeightTo(i) / TabWeightTo(kPageCount); }
constexpr Rect kTab(int i) { return {kTabX(i), 84, kTabX(i + 1) - kTabX(i) - 8, 52}; }
constexpr int kRowY0 = 156, kRowStep = 66, kRowH = 56;
// A page with more rows than kRowsPerPage (the Melee page's seven, up to kRowsMax) stands them closer, its last one still above the status line.
// The rows' rectangles take the page (-1, as the other pages: the usual step).
constexpr int kRowStepClose = 58, kRowHClose = 50;
constexpr bool CloseRows(int page) { return page >= 0 && page < kPageCount && RowsOn(page) > kRowsPerPage; }
constexpr int RowStep(int page) { return CloseRows(page) ? kRowStepClose : kRowStep; }
constexpr int RowH(int page) { return CloseRows(page) ? kRowHClose : kRowH; }
constexpr int RowY(int row, int page) { return kRowY0 + row * RowStep(page); }
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
constexpr Rect kRecentre = {32, 620, 280, 68};
// The two low-life pages' own button, between Recentre and Close: the vignette and the beat in the controllers for five seconds, with the rows as they stand.
constexpr Rect kPreviewBtn = {324, 620, 200, 68};
constexpr Rect kClose = {532, 620, 220, 68};
constexpr Rect kApply = {772, 620, 220, 68};
// The panel in the room: its width in metres (the height follows the atlas), and where it stands from the eyes.
constexpr float kWidthM = 1.2f, kHeightM = kWidthM * kH / kW;
constexpr float kAheadM = 1.0f, kBelowM = 0.05f;

enum class Kind { None, Tab, Change, Apply, Close, Recentre, Preset, Slider, Calibrate, Forget, View, Preview, Guide, GuideBack, GuideNext, Span };
struct Hit {
    Kind kind = Kind::None;
    int index = -1;   // a tab's page, a Change's key, a Preset's number, a Slider's side along the track (-1: a stepper at its end), a View's number (1 = F1 ... 4 = F4)
    int delta = 0;    // -1 / +1 (a toggle: 1; a Slider's stepper)
    bool operator==(const Hit& o) const { return kind == o.kind && index == o.index && delta == o.delta; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

// The slider's side at a place along the atlas (x in pixels; also off the track: the thumb follows the beam while it is held).
inline Hit SliderAt(float x) {
    return {Kind::Slider, video::SideAtFraction((x - (float)kSlX0) / (float)(kSlX1 - kSlX0)), 0};
}
// A held trigger on these goes on pressing them (the steppers: a Change with a step, the slider's ends).
inline bool Repeats(const Hit& h) {
    if (h.kind == Kind::Slider) return h.index < 0 && h.delta != 0;
    if (h.kind == Kind::Span) return h.delta != 0;
    return h.kind == Kind::Change && h.delta != 0 && h.index >= 0 && h.index < kCount && !IsToggle(kDefs[h.index]);
}

// The atlas' u, v (0..1 from its top left) to the button there.
inline Hit HitTest(int page, float u, float v) {
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return {};
    const float x = u * kW, y = v * kH;
    if (PageCount() > 1)
        for (int i = 0; i < PageCount(); ++i) if (kTab(i).Contains(x, y)) return {Kind::Tab, i, 0};
    if (kApply.Contains(x, y)) return {Kind::Apply, -1, 0};
    if (kClose.Contains(x, y)) return {Kind::Close, -1, 0};
    if (kRecentre.Contains(x, y)) return {Kind::Recentre, -1, 0};
    if (IsLifePage(page) && kPreviewBtn.Contains(x, y)) return {Kind::Preview, -1, 0};
    if (page == kPageView)
        for (int i = 0; i < kViews; ++i) if (kViewBtn(i).Contains(x, y)) return {Kind::View, i + 1, 0};
    if (page == kPageVideo) {
        for (int i = 0; i < video::kPresetCount; ++i) if (kPreset(i).Contains(x, y)) return {Kind::Preset, i, 0};
        if (kSlDec.Contains(x, y)) return {Kind::Slider, -1, -1};
        if (kSlInc.Contains(x, y)) return {Kind::Slider, -1, 1};
        if (kSlTrack.Contains(x, y)) return SliderAt(x);
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
        const int key = KeyAt(page, r);
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
enum class Do { Nothing, Save, Close, Recentre, Calibrate, View, Preview, GuideDone };   // GuideDone: the last page's Done - the window closes, the guide is over
struct Action {
    Do what = Do::Nothing;
    Request request;   // with Save
    int view = 0;      // with View: 1 = F1 ... 4 = F4
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
    void Seen(const Snapshot& s) {
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
    // The game's view now (the plugin's, 1 = F1 ... 4 = F4; 0 = not known): the button of it is lit.
    int View() const { return view_; }
    void SetView(int v) {
        v = v >= 1 && v <= kViews ? v : 0;
        if (v == view_) return;
        view_ = v;
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
                if (hit.index >= 0 && hit.index < PageCount() && hit.index != page) {
                    page = hit.index;
                    if (page == kPageGuide) guideShown_ = true;
                    ++revision;
                }
                return a;
            case Kind::Change: {
                if (!ready || Busy() || hit.index < 0 || hit.index >= kCount) return a;
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
            case Kind::Preview:   // the plugin shows the vignette and plays the beat for five seconds: nothing of the ini, nothing of the draft is touched
                if (!IsLifePage(page)) return a;
                a.what = Do::Preview;
                return a;
            case Kind::Preset:
                if (!ready || Busy() || hit.index < 0 || hit.index >= video::kPresetCount) return a;
                SetPick(video::PickOfPreset(hit.index));
                return a;
            case Kind::Slider: {
                if (!ready || Busy()) return a;
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
