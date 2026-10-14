#pragma once
// The in-headset settings panel: what it shows, where its buttons are, what a click does. Pure - no
// Windows, no OpenXR - so the whole of it is tested without a headset (cleanroom-style: tools/hset_test.cpp).
//
// After bearstonem/d2r-pcvr's steamvr/vr_settings.h (MIT): a table of keys, a draft of the values, a hit
// test on the painted atlas, Apply writes only what changed, a conflict with an outside edit reloads.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hset_body.h"
#include "hset_defs.h"
#include "hset_video.h"

namespace hset {

// -- values ----------------------------------------------------------------------------
inline float Normalize(int key, float v) {
    const Def& d = kDefs[key];
    if (!std::isfinite(v)) return d.def;
    v = std::clamp(v, d.min, d.max);
    if (IsToggle(d)) return v > 0.5f ? 1.0f : 0.0f;
    if (d.step > 0.0f) {   // on the grid that starts at min
        v = d.min + std::round((v - d.min) / d.step) * d.step;
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
inline uint32_t Changed(const Values& a, const Values& b) {
    uint32_t m = 0;
    for (int i = 0; i < kCount; ++i) if (Differ(a[i], b[i])) m |= 1u << i;
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
};
// An Apply: `base` is what the panel saw in the file when its draft began, `values` the draft; only `changed` is written.
struct Request {
    Values base{}, values{};
    uint32_t changed = 0;
    uint64_t sequence = 0;
};

// -- the atlas: the painted panel, in pixels -----------------------------------------------
constexpr int kW = 1024, kH = 720;
struct Rect {
    int x, y, w, h;
    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
constexpr Rect kTab(int i) { return {32 + i * 232, 84, 220, 52}; }
constexpr int kRowY0 = 156, kRowStep = 66, kRowH = 56;
constexpr Rect kDec(int row) { return {584, kRowY0 + row * kRowStep, 72, kRowH}; }
constexpr Rect kVal(int row) { return {664, kRowY0 + row * kRowStep, 248, kRowH}; }
constexpr Rect kInc(int row) { return {920, kRowY0 + row * kRowStep, 72, kRowH}; }
constexpr Rect kToggle(int row) { return {584, kRowY0 + row * kRowStep, 408, kRowH}; }
// The game view row (the first page's last): F1 (from above), F2 (third person), F3 (the game on your floor), F4 (first person).
constexpr int kViews = 4;
constexpr Rect kViewBtn(int i) { return {584 + i * 104, kRowY0 + kViewRow * kRowStep, 96, kRowH}; }
constexpr Rect kStatus = {32, 566, 960, 40};
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
// The Body page: what it is for, the two buttons, what is stored, and the steps.
constexpr Rect kBodyTitle = {32, 148, 960, 36};
constexpr Rect kBodyWhy = {32, 184, 960, 30};
constexpr Rect kCalibrate = {32, 228, 540, 76};
constexpr Rect kForget = {588, 228, 404, 76};
constexpr Rect kBodyStored = {32, 322, 960, 38};
constexpr Rect kBodyUse = {32, 360, 960, 30};
constexpr Rect kBodyStep(int i) { return {48, 408 + i * 32, 944, 30}; }
constexpr Rect kRecentre = {32, 620, 280, 68};
constexpr Rect kClose = {532, 620, 220, 68};
constexpr Rect kApply = {772, 620, 220, 68};
// The panel in the room: its width in metres (the height follows the atlas), and where it stands from the eyes.
constexpr float kWidthM = 1.2f, kHeightM = kWidthM * kH / kW;
constexpr float kAheadM = 1.0f, kBelowM = 0.05f;

enum class Kind { None, Tab, Change, Apply, Close, Recentre, Preset, Slider, Calibrate, Forget, View };
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
    if (page == kPageView)
        for (int i = 0; i < kViews; ++i) if (kViewBtn(i).Contains(x, y)) return {Kind::View, i + 1, 0};
    if (page == kPageVideo) {
        for (int i = 0; i < video::kPresetCount; ++i) if (kPreset(i).Contains(x, y)) return {Kind::Preset, i, 0};
        if (kSlDec.Contains(x, y)) return {Kind::Slider, -1, -1};
        if (kSlInc.Contains(x, y)) return {Kind::Slider, -1, 1};
        if (kSlTrack.Contains(x, y)) return SliderAt(x);
        return {};
    }
    if (page == kPageBody) {
        if (kCalibrate.Contains(x, y)) return {Kind::Calibrate, -1, 0};
        if (kForget.Contains(x, y)) return {Kind::Forget, -1, 0};
        return {};
    }
    for (int r = 0; r < kRowsPerPage; ++r) {
        const int key = KeyAt(page, r);
        if (key < 0) break;
        if (IsToggle(kDefs[key])) {
            if (kToggle(r).Contains(x, y)) return {Kind::Change, key, 1};
        } else {
            if (kDec(r).Contains(x, y)) return {Kind::Change, key, -1};
            if (kInc(r).Contains(x, y)) return {Kind::Change, key, 1};
        }
    }
    return {};
}

// -- the panel's state ---------------------------------------------------------------------------
enum class Do { Nothing, Save, Close, Recentre, Calibrate, View };
struct Action {
    Do what = Do::Nothing;
    Request request;   // with Save
    int view = 0;      // with View: 1 = F1 ... 4 = F4
};

class Model {
    Values base_ = Defaults(), baseFile_ = Defaults(), draft_ = Defaults();
    Values seen_ = Defaults(), seenFile_ = Defaults();   // the file as the store last read it (the Body page's measurement, a Forget's base)
    uint64_t serial_ = 0, pending_ = 0;
    uint32_t pendingMask_ = 0;   // the keys the Apply in flight writes
    video::Env env_;
    int view_ = 0;               // the game's view now, 1 = F1 ... 4 = F4, 0 = not known

    static constexpr uint32_t Bit(int key) { return 1u << key; }
    void Seen(const Snapshot& s) {
        const body::Stored was = Measured();
        seen_ = s.values;
        seenFile_ = s.file;
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
    Result result = Result::None;
    uint64_t revision = 0;   // moves with everything the picture shows

    const Values& Get() const { return draft_; }
    bool Dirty() const { return Changed(base_, draft_) != 0; }
    bool Busy() const { return pending_ != 0; }
    bool Edited(int key) const { return key >= 0 && key < kCount && Differ(base_[key], draft_[key]); }   // changed since the file was read
    // The render size the draft makes, and the one the file has.
    video::Pick DraftPick() const { return video::PickOf(draft_[kKeyMode], draft_[kKeyPx]); }
    video::Pick FilePick() const { return video::PickOf(base_[kKeyMode], base_[kKeyPx]); }
    bool VideoEdited() const { return Edited(kKeyMode) || Edited(kKeyPx); }
    // What the file says of the T-pose measurement, always the latest (not a draft: the plugin writes it).
    body::Stored Measured() const { return body::Of(seen_[kKeyUse], seen_[kKeyVersion], seen_[kKeyEye], seen_[kKeySpan]); }
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
    void SetEnv(const video::Env& e) {
        if (e == env_) return;
        env_ = e;
        ++revision;
    }

    void Open(const Snapshot& s) {
        open = true;
        page = 0;
        base_ = draft_ = s.values;
        baseFile_ = s.file;
        Seen(s);
        ready = s.ready;
        result = s.result == Result::Saving ? Result::None : s.result;
        serial_ = std::max(serial_, s.sequence);
        pending_ = 0;
        pendingMask_ = 0;
        ++revision;
    }
    // The panel comes back after the measurement it handed the hands to: on the Body page, with what the player had not
    // applied still in the draft (a fresh start when there was nothing).
    void Resume(const Snapshot& s) {
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
        page = kPageBody;
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
                const uint32_t mine = pendingMask_;
                pending_ = 0;
                pendingMask_ = 0;
                result = s.result;
                ready = s.ready;
                if (s.result == Result::Conflict) {
                    base_ = draft_ = s.values;
                    baseFile_ = s.file;
                } else if (s.result == Result::Saved) {
                    // what was written, and every key nobody was editing, comes from the file; an edit made meanwhile to a key this
                    // save did not carry (a Forget while a size is picked) stays in the draft
                    for (int i = 0; i < kCount; ++i)
                        if ((mine & Bit(i)) || !Differ(base_[i], draft_[i])) {
                            base_[i] = draft_[i] = s.values[i];
                            baseFile_[i] = s.file[i];
                        }
                }
                ++revision;
            }
        } else if (!Dirty() && (Changed(base_, s.values) || Changed(baseFile_, s.file) || ready != s.ready)) {
            base_ = draft_ = s.values;
            baseFile_ = s.file;
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
            case Kind::Tab:
                if (hit.index >= 0 && hit.index < PageCount() && hit.index != page) { page = hit.index; ++revision; }
                return a;
            case Kind::Change: {
                if (!ready || Busy() || hit.index < 0 || hit.index >= kCount) return a;
                const Def& d = kDefs[hit.index];
                float& v = draft_[hit.index];
                const float was = v;
                v = IsToggle(d) ? (v > 0.5f ? 0.0f : 1.0f) : Normalize(hit.index, v + (float)hit.delta * d.step);
                if (Differ(was, v)) { result = Result::None; ++revision; }
                return a;
            }
            case Kind::View:   // the view is the game's, not the ini's: no ini needed; the view it already is is not asked again
                if (hit.index < 1 || hit.index > kViews || hit.index == view_) return a;
                a.what = Do::View;
                a.view = hit.index;
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
            case Kind::Calibrate:   // the measurement writes the ini; without one there is nowhere to put it
                if (!ready || Busy()) return a;
                a.what = Do::Calibrate;
                return a;
            case Kind::Forget: {   // clears the stored measurement: [calibration] version, eye_height_m, arm_span_m (what the PC window's button does)
                if (!ready || Busy() || !Measured().any) return a;
                const uint32_t mask = Bit(kKeyVersion) | Bit(kKeyEye) | Bit(kKeySpan);
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
                return a;
            default: return a;
        }
    }
};

}  // namespace hset
