#pragma once
// The settings pages as a description of a picture: what is drawn where, in the sheet's pixels (hset_model.h's layout, 1024 x 720) and
// in which role - a fill, a line of text, a button with its state. A model and the button under the beam in, a list of items out.
// Pure (no Windows, no OpenXR), so what a page shows is tested without drawing it. Two painters render it:
//   vr/hset_paint.cpp  the GDI picture of the panel of its own ([openxr] headset_settings without the skill panel's tiles),
//   vr/tiles_paint.cpp the same pages on the skill panel window's Settings tab, scaled and in that window's own style.
// The hit test (hset_model.h HitTest) reads the same rectangles, so what is drawn is what is clicked.

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "first_start_pages.h"   // the Guide page's words
#include "hset_model.h"
#include "hset_names.h"

namespace hset {

// Colours by what they mean; each painter has its own for them.
enum class Role : uint8_t { Back, RowA, RowB, Button, Hot, Selected, Off, Text, Quiet, Accent, Warn, Dim };
enum class Face : uint8_t { Normal, Bold, Small };
enum class Align : uint8_t { Left, Center, Right };
// What an item is a piece of, for a painter that draws each kind with a picture of its own (the skill panel's game look, vr/tiles_paint.cpp):
// a row's band, a value's field, the slider's track / its filled part / its knob; a page tab, a less / more arrow, an on / off switch, one of
// a row of choices, a toggle with two words of its own (Left / Right: a click flips it), any other button. The other painters go by the role alone.
enum class Part : uint8_t { None, Row, Value, Track, TrackFill, Knob, Tab, Less, More, Switch, Choice, Flip, Action };

struct Item {
    enum class Type : uint8_t { Fill, Text, Button };
    Type type = Type::Fill;
    Rect r{};
    Role colour = Role::Back;   // a Fill's colour; a Text's or a Button label's ink
    Face face = Face::Normal;
    Align align = Align::Left;
    Part part = Part::None;
    bool hot = false, selected = false, enabled = true;   // a Button: under the beam, the chosen one, usable
    std::string text;
};
using Scene = std::vector<Item>;

// The line under the rows for a model's state. `window`: the pages are the skill panel's Settings tab (it is closed by B, its Close
// button or the long press of the right stick click, not by the grips and a click).
inline const char* StatusText(const Model& m, bool window = false) {
    if (m.page == kPageGuide) return firststart::Nav(std::clamp(m.guide, 0, firststart::kPageCount - 1));   // the guide's own foot, whatever else is going on
    if (!m.ready) return uitext::Tr("vr.hset.status.no_ini", "d2r_vr.ini was not found. Nothing can be changed.");
    switch (m.result) {
        case Result::Saving: return uitext::Tr("vr.hset.status.saving", "Saving...");
        case Result::Saved: return m.page == kPageVideo ? uitext::Tr("vr.hset.status.saved_video", "Saved. The game rebuilds its picture in a moment: a few seconds frozen.") : uitext::Tr("vr.hset.status.saved", "Saved. The change is live.");
        case Result::Failed: return uitext::Tr("vr.hset.status.save_failed", "Saving failed. Your changes are still here.");
        case Result::Conflict: return uitext::Tr("vr.hset.status.conflict", "The ini was changed elsewhere. Its current values are shown.");
        default: break;
    }
    if (m.Dirty()) {
        if (m.LiveOnly()) return uitext::Tr("vr.hset.status.live_soon", "A change applies by itself in a moment.");   // the belt: written as D2R VR Settings' sliders are
        return m.page == kPageVideo && m.VideoEdited() ? uitext::Tr("vr.hset.status.video_unapplied", "Not applied yet. Apply asks the game for this size; Close drops it.")
                                                       : uitext::Tr("vr.hset.status.unsaved", "Unsaved changes. Apply saves them, Close drops them.");
    }
    if (m.page == kPageVideo) return uitext::Tr("vr.hset.status.video_idle", "Pick a size, then Apply. It takes effect once your hero is in the world.");
    if (m.page == kPageBody) return uitext::Tr("vr.hset.status.body_idle", "Calibrate body hides this panel; it comes back when the measurement is done.");
    if (m.page == kPageKeysL || m.page == kPageKeysR)
        return m.Get()[kKeyDirectOn] > 0.5f ? uitext::Tr("vr.hset.status.keys_idle", "Pick what a button does, then Apply. Chords and the other long presses: D2R VR Authentic Settings.")
                                            : uitext::Tr("vr.hset.status.keys_off", "Controls without BodyWalk is off: these buttons do nothing (BodyWalk's mapping is the controls).");
    if (m.page == kPageMelee && m.Get()[kKeyMeleeTouch] > 0.5f && m.Get()[kKeyMeleeDirect] < 0.5f && m.Get()[kKeyMeleeDirectClient] > 1.5f)   // the file says Fully on
        return uitext::Tr("vr.hset.status.melee_direct_full", "Direct hit is Fully on (D2R VR Authentic Settings > Strikes); its tick here is By the rules.");
    if (m.page == kPageMelee)
        return m.Get()[kKeyMeleeTouch] > 0.5f ? uitext::Tr("vr.hset.status.melee_idle", "Each change applies by itself. First person (F4) only.")
                                              : uitext::Tr("vr.hset.status.melee_off", "Hit by touch is off: tick it, and the rows below apply.");
    if (m.page == kPageBelt && m.Get()[kKeyBeltOn] > 0.5f && m.Get()[kKeyGrabTrigger] > 0.5f && m.Get()[kKeyDirectOn] < 0.5f)
        return uitext::Tr("vr.hset.status.belt_needs_direct", "Needs Controls without BodyWalk (off now): until then the grip alone takes a potion.");   // the plugin reads the trigger itself, or not at all
    if (m.page == kPageBelt) return m.Get()[kKeyBeltOn] > 0.5f ? uitext::Tr("vr.hset.status.belt_idle", "Each change applies by itself: look at your belt, then move it again.")
                                                                 : uitext::Tr("vr.hset.status.belt_off", "The potions on the belt are off (D2R VR Authentic Settings > UI: F4 body): these do nothing.");
    return window ? uitext::Tr("vr.hset.status.idle_window", "Point and pull the trigger. B, Close or a long press of the right stick closes this window.")
                  : uitext::Tr("vr.hset.status.idle", "Point and pull the trigger. B, or a long press of the right stick, closes it.");
}

// What a Choice row says and shows: the pick's name ("Custom (ini)" for a line the list does not name); and under the control's name, the ini
// line itself (what the pick writes, or the custom text as the file has it).
inline std::string ChoiceLabel(int index) { return keys::PickName(index); }
inline std::string ChoiceDetail(const Model& m, int key) {
    const int index = (int)std::lround(m.Get()[key]);
    std::string text = index < keys::CustomPick() ? keys::PickIni(index) : m.BindRaw(key);
    if (index >= keys::CustomPick() && text.empty()) text = uitext::Tr("vr.hset.custom_empty", "(a line the list does not name)");
    std::string lower = text;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    const bool rowOff = std::wcscmp(kDefs[key].key, L"left_trigger") == 0 && lower.find("pad:lt") == std::string::npos;   // (vr/direct_layout.h SecondRowOn)
    return std::string(kDefs[key].detail) + " = " + text + (rowOff ? uitext::Tr("vr.hset.second_row_off", " - the second row (LT) is off") : "");
}

// The line under the rows as text (StatusText for the pages whose line is a fixed one).
inline std::string StatusLine(const Model& m, bool window = false) { return StatusText(m, window); }

struct ComposeOptions {
    bool title = true;               // "HEADSET SETTINGS" and "D2R VR" over the tabs (the skill panel's window has its own title)
    bool window = false;             // the skill panel's Settings tab: its status line says how that window is closed
    const char* status = nullptr;    // replaces the line under the rows
};

// The first-start guide's page: the text of a row is set in lines as long as their place takes (a line is one item of the scene, so both painters set it
// as they set any line), the rows follow each other by how many lines each has. Counted in letters: the Small face is about 7.5 pixels a letter in the
// panel's own painter, 8 with capitals and digits among them (a test measures every line of every page against its place); the tiles' window sets
// what is too long smaller.
constexpr int kGuideLineH = 24, kGuideRowPad = 10;
constexpr int kGuideControlChars = 26, kGuideTextChars = 84, kGuideWideChars = 110;   // a control's name / its text beside it / a text across the page
constexpr Rect kGuideControlBox = {44, 0, 216, kGuideLineH}, kGuideTextBox = {270, 0, 722, kGuideLineH}, kGuideWideBox = {44, 0, 948, kGuideLineH};
inline std::vector<std::string> WrapWords(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    std::string cur;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(at, end - at);
        at = end + 1;
        if (word.empty()) continue;
        if (cur.empty()) cur = word;
        else if (uitext::Letters(cur) + 1 + uitext::Letters(word) <= maxChars) cur += " " + word;
        else { lines.push_back(cur); cur = word; }
        while (uitext::Letters(cur) > maxChars) {   // a word longer than a line (a language without spaces): cut at a letter
            size_t cut = 0;
            for (int n = 0; cut < cur.size() && n < maxChars; ++n)
                do ++cut; while (cut < cur.size() && ((unsigned char)cur[cut] & 0xC0) == 0x80);
            lines.push_back(cur.substr(0, cut));
            cur = cur.substr(cut);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.push_back("");
    return lines;
}
struct GuideRowLayout {
    std::vector<std::string> control, text;   // the lines of each column
    bool named = false;                        // a control's name stands in the left column (the text is beside it, not across)
    int lines = 1, y = 0, h = 0;               // how many lines the row takes, where it starts and how tall it is (the sheet's pixels)
};
// The rows of page `sub` of the guide (firststart::kPages), top to bottom from kGuideTop.
inline std::vector<GuideRowLayout> GuideRows(int sub) {
    std::vector<GuideRowLayout> out;
    sub = std::clamp(sub, 0, firststart::kPageCount - 1);
    int y = kGuideTop;
    for (int r = 0; r < firststart::kRowsMax; ++r) {
        const firststart::Row& row = firststart::kPages[sub].rows[r];
        if (!row.does || !*row.does) break;
        GuideRowLayout l;
        l.named = row.control && *row.control;
        if (l.named) l.control = WrapWords(firststart::Control(sub, r), kGuideControlChars);
        l.text = WrapWords(firststart::Does(sub, r), l.named ? kGuideTextChars : kGuideWideChars);
        l.lines = std::max({1, (int)l.control.size(), (int)l.text.size()});
        l.y = y;
        l.h = l.lines * kGuideLineH + kGuideRowPad;
        y += l.h;
        out.push_back(std::move(l));
    }
    return out;
}
// Where the rows of page `sub` end (the sheet's y): they must end above kGuideBottom.
inline int GuideEnd(int sub) {
    const std::vector<GuideRowLayout> rows = GuideRows(sub);
    return rows.empty() ? kGuideTop : rows.back().y + rows.back().h;
}

namespace scene_detail {

struct Builder {
    Scene& s;
    const Hit& hover;
    void Fill(Rect r, Role c, Part p = Part::None) {
        Item i;
        i.type = Item::Type::Fill;
        i.r = r;
        i.colour = c;
        i.part = p;
        s.push_back(std::move(i));
    }
    void Text(const std::string& t, Rect r, Face f, Role c, Align a = Align::Left) {
        Item i;
        i.type = Item::Type::Text;
        i.r = r;
        i.colour = c;
        i.face = f;
        i.align = a;
        i.text = t;
        s.push_back(std::move(i));
    }
    void Button(Rect r, const std::string& label, const Hit& h, bool selected = false, bool enabled = true, Role ink = Role::Text, Part p = Part::Action) {
        Item i;
        i.type = Item::Type::Button;
        i.r = r;
        i.colour = ink;
        i.face = Face::Bold;
        i.align = Align::Center;
        i.part = p;
        i.hot = enabled && hover == h && h.kind != Kind::None;
        i.selected = selected;
        i.enabled = enabled;
        i.text = label;
        s.push_back(std::move(i));
    }
};

}  // namespace scene_detail

inline Scene Compose(const Model& m, const Hit& hover, const ComposeOptions& opt = {}) {
    Scene scene;
    scene_detail::Builder b{scene, hover};
    b.Fill({0, 0, kW, kH}, Role::Back);
    if (opt.title) {
        b.Text(uitext::Tr("vr.hset.title", "HEADSET SETTINGS"), {32, 22, 640, 50}, Face::Bold, Role::Accent);
        b.Text("D2R VR", {760, 22, 232, 50}, Face::Small, Role::Quiet, Align::Center);
    }
    if (PageCount() > 1)
        for (int i = 0; i < PageCount(); ++i) b.Button(kTab(i), PageName(i), {Kind::Tab, i, 0}, m.page == i, true, Role::Text, Part::Tab);

    const bool canEdit = m.ready && !m.Busy();
    if (m.page == kPageVideo) {
        // -- the render size ----------------------------------------------------------------------------------
        const video::Pick pick = m.DraftPick();
        const video::Env& env = m.Env();
        const bool edited = m.VideoEdited();
        b.Text(uitext::Tr("vr.video.title", "Render size"), kVidTitle, Face::Bold, Role::Text);
        b.Text(video::PickText(pick, env), kVidTitle, Face::Bold, edited ? Role::Warn : Role::Accent, Align::Right);
        b.Text(video::NowText(env), kVidNow, Face::Small, Role::Quiet);
        for (int i = 0; i < video::kPresetCount; ++i) {
            const bool on = pick == video::PickOfPreset(i);
            b.Button(kPreset(i), video::PresetLabel(i), {Kind::Preset, i, 0}, on, canEdit, on ? (edited ? Role::Warn : Role::Accent) : Role::Text, Part::Choice);
        }
        b.Button(kSlDec, "-", {Kind::Slider, -1, -1}, false, canEdit, Role::Text, Part::Less);
        b.Button(kSlInc, "+", {Kind::Slider, -1, 1}, false, canEdit, Role::Text, Part::More);
        const int midY = kSlTrack.y + kSlTrack.h / 2;
        const bool square = pick.kind == rendersize::Kind::Square;
        const int rest = m.Get()[kKeyPx] >= (float)rendersize::kSideMin ? (int)m.Get()[kKeyPx] : 0;   // where the slider rests while a preset is chosen
        const int thumbSide = square ? pick.side : rest;
        const int thumbX = kSlX0 + (int)std::lround(video::FractionOfSide(thumbSide ? thumbSide : rendersize::kSideMin) * (kSlX1 - kSlX0));
        b.Fill({kSlX0, midY - 4, kSlX1 - kSlX0, 8}, Role::Off, Part::Track);
        if (square) b.Fill({kSlX0, midY - 4, thumbX - kSlX0, 8}, Role::Selected, Part::TrackFill);
        if (thumbSide) {
            const bool hot = canEdit && hover.kind == Kind::Slider && hover.index >= 0;
            b.Fill({thumbX - 14, midY - 24, 28, 48}, !canEdit ? Role::Off : square ? (hot ? Role::Hot : edited ? Role::Warn : Role::Accent) : Role::Button, Part::Knob);
            if (!square) b.Fill({thumbX - 10, midY - 20, 20, 40}, Role::Back, Part::Knob);   // a hollow thumb: the choice is not a square
        }
        b.Text("1024", {kSlEnds.x, kSlEnds.y, 200, kSlEnds.h}, Face::Small, Role::Quiet);
        b.Text("4096", {kSlEnds.x + kSlEnds.w - 200, kSlEnds.y, 200, kSlEnds.h}, Face::Small, Role::Quiet, Align::Right);
        b.Text(video::CostText(pick, env), kVidCost, Face::Small, Role::Quiet);
        const std::string limit = video::LimitText(pick, env);
        if (!limit.empty()) b.Text(limit, kVidLimit, Face::Small, Role::Warn);
        b.Text(uitext::Tr("vr.video.note", "Apply rebuilds the picture: the game freezes a few seconds (about 7)."), kVidNote, Face::Normal, Role::Warn);
    } else if (m.page == kPageBody) {
        // -- the T-pose measurement -----------------------------------------------------------------------------
        // (how you play, standing / seated, is the page's table row above: drawn with the other pages' rows below)
        const body::Stored st = m.Measured();
        b.Text(body::WhyText(st.seated), kBodyWhy, Face::Small, Role::Quiet);
        b.Button(kCalibrate, uitext::Tr("vr.body.calibrate", "Calibrate body"), {Kind::Calibrate, -1, 0}, true, canEdit && !m.PlayPending(), Role::Accent);
        b.Button(kForget, uitext::Tr("vr.body.forget", "Forget measurement"), {Kind::Forget, -1, 0}, false, canEdit && st.any);
        b.Button(kGuideBtn, uitext::Tr("vr.body.guide", "First-start guide"), {Kind::Guide, -1, 0});
        // the arm span by hand, right under Calibrate: the span in use, a centimetre a press (a new measurement moves it)
        {
            const bool stored = m.SpanStored(), edited = m.Edited(kKeySpan);
            b.Button(kSpanDec, "-", {Kind::Span, -1, -1}, false, canEdit, Role::Text, Part::Less);
            b.Button(kSpanInc, "+", {Kind::Span, -1, 1}, false, canEdit, Role::Text, Part::More);
            b.Text(body::SpanText(m.SpanShown(), stored), kSpanVal, Face::Bold, edited ? Role::Warn : stored ? Role::Accent : Role::Quiet, Align::Center);
            b.Text(uitext::Tr("vr.body.span_note", "Arm span by hand: - shorter, + longer arms (a new Calibrate sets it again)"), kSpanNote, Face::Small, Role::Quiet);
        }
        b.Text(body::MeasuredText(st), kBodyStored, Face::Bold, st.valid ? Role::Accent : Role::Quiet);
        b.Text(body::UseText(st), kBodyUse, Face::Small, st.valid && !st.use ? Role::Warn : Role::Quiet);
        static const char* const steps[] = {
            uitext::Tr("vr.body.step1", "1. Stand upright where you play, eyes ahead. The headset's floor must be set up."),
            uitext::Tr("vr.body.step2", "2. Press Calibrate body: this panel hides and a prompt appears in front of you."),
            uitext::Tr("vr.body.step3", "3. Arms out sideways, level. Let go of both triggers, then squeeze both and hold still."),
            uitext::Tr("vr.body.step4", "4. Right B cancels. This panel comes back when it is done, with the numbers."),
        };
        static const char* const seatedSteps[] = {
            uitext::Tr("vr.body.seated_step1", "1. Sit upright where you play, eyes ahead, room for your arms. No floor is needed."),
            uitext::Tr("vr.body.step2", "2. Press Calibrate body: this panel hides and a prompt appears in front of you."),
            uitext::Tr("vr.body.step3", "3. Arms out sideways, level. Let go of both triggers, then squeeze both and hold still."),
            uitext::Tr("vr.body.seated_step4", "4. Then Recentre view sitting as you play: the hero's eyes are where your head is."),
        };
        for (int i = 0; i < 4; ++i) b.Text((st.seated ? seatedSteps : steps)[i], kBodyStep(i), Face::Small, Role::Quiet);
    }
    if (m.page == kPageGuide) {
        // -- the first-start guide (vr/first_start.h): a page of its words, Back and Next under it -----------------------------------------
        const int sub = std::clamp(m.guide, 0, firststart::kPageCount - 1);
        const bool offer = sub == firststart::kOfferPage, last = sub == firststart::kGuidePages;
        b.Text(firststart::Title(sub), kGuideTitle, Face::Bold, Role::Accent);
        if (!offer) {
            char at[24];
            std::snprintf(at, sizeof at, "%d / %d", sub, firststart::kGuidePages);
            b.Text(at, kGuideAt, Face::Small, Role::Quiet, Align::Right);
        }
        int r = 0;
        for (const GuideRowLayout& l : GuideRows(sub)) {
            b.Fill({32, l.y, 960, l.h - 2}, r++ % 2 ? Role::RowB : Role::RowA, Part::Row);
            for (size_t k = 0; k < l.control.size(); ++k)
                b.Text(l.control[k], {kGuideControlBox.x, l.y + kGuideRowPad / 2 + (int)k * kGuideLineH, kGuideControlBox.w, kGuideLineH}, Face::Small, Role::Accent);
            const Rect box = l.named ? kGuideTextBox : kGuideWideBox;
            for (size_t k = 0; k < l.text.size(); ++k) b.Text(l.text[k], {box.x, l.y + kGuideRowPad / 2 + (int)k * kGuideLineH, box.w, kGuideLineH}, Face::Small, Role::Text);
        }
        b.Button(kGuideBack, offer ? uitext::Tr("vr.guide.not_now", "Not now") : uitext::Tr("vr.guide.back", "Back"), {Kind::GuideBack, -1, -1}, false, offer || sub > 1);
        b.Button(kGuideNext, offer ? uitext::Tr("vr.guide.measure_now", "Measure now") : last ? uitext::Tr("vr.guide.done", "Done") : uitext::Tr("vr.guide.next", "Next"), {Kind::GuideNext, -1, 1}, true, !offer || canEdit, Role::Accent);
    }
    if (m.page == kPageView) {
        // -- the game's view: F1 from above, F2 third person, F3 the game on your floor, F4 first person -------------------------
        const int y = kRowY0 + kViewRow * kRowStep;
        b.Fill({32, y - 5, 960, kRowStep - 2}, kViewRow % 2 ? Role::RowB : Role::RowA, Part::Row);
        b.Text(uitext::Tr("vr.hset.game_view", "Game view"), {48, y - 2, 520, 32}, Face::Bold, Role::Text);
        b.Text(uitext::Tr("vr.hset.game_view.detail", "F1 above, F2 third person, F3 on your floor, F4 first"), {48, y + 28, 530, 28}, Face::Small, Role::Quiet);
        static const char* const names[kViews] = {"F1", "F2", "F3", "F4"};
        for (int i = 0; i < kViews; ++i) b.Button(kViewBtn(i), names[i], {Kind::View, i + 1, 0}, m.View() == i + 1, true, m.View() == i + 1 ? Role::Accent : Role::Text, Part::Choice);
    }
    const int pg = m.page, step = RowStep(pg);
    const bool close = CloseRows(pg);   // seven rows: the two lines of a row's name a little smaller and closer
    for (int r = 0; r < kRowsMax; ++r) {
        const int key = KeyAt(pg, r);
        if (key < 0) break;
        const Def& d = kDefs[key];
        const int y = RowY(r, pg);
        const bool rowEdit = canEdit;
        b.Fill({32, y - 5, 960, step - 2}, r % 2 ? Role::RowB : Role::RowA, Part::Row);
        b.Text(DefLabel(key), close ? Rect{48, y - 4, 520, 30} : Rect{48, y - 2, 520, 32}, Face::Bold, Role::Text);
        b.Text(d.type == DefType::Choice ? ChoiceDetail(m, key) : std::string(DefDetail(key)), close ? Rect{48, y + 24, 530, 26} : Rect{48, y + 28, 530, 28},
               Face::Small, Role::Quiet);
        const float v = m.Get()[key];
        if (d.type == DefType::Choice) {
            b.Button(kDec(r, pg), "<", {Kind::Change, key, -1}, false, rowEdit, Role::Text, Part::Less);
            b.Fill(kVal(r, pg), Role::Back, Part::Value);
            b.Text(ChoiceLabel((int)std::lround(v)), kVal(r, pg), Face::Bold, m.Edited(key) ? Role::Warn : Role::Text, Align::Center);
            b.Button(kInc(r, pg), ">", {Kind::Change, key, 1}, false, rowEdit, Role::Text, Part::More);
        } else if (IsToggle(d)) {
            b.Button(kToggle(r, pg), ValueWord(v > 0.5f ? d.on : d.off), {Kind::Change, key, 1}, v > 0.5f, rowEdit, m.Edited(key) ? Role::Warn : Role::Text,
                     std::strcmp(d.off, "Off") == 0 && std::strcmp(d.on, "On") == 0 ? Part::Switch : Part::Flip);
        } else {
            char n[48];
            std::snprintf(n, sizeof n, d.fmt, (double)v);
            const char* name = ValueName(m.Get(), key);   // a row that is a name (the toolbar type), not a number
            const std::string s = name ? name : n;
            b.Button(kDec(r, pg), "-", {Kind::Change, key, -1}, false, rowEdit, Role::Text, Part::Less);
            b.Fill(kVal(r, pg), Role::Back, Part::Value);
            b.Text(s, kVal(r, pg), Face::Bold, m.Edited(key) ? Role::Warn : Role::Text, Align::Center);   // amber: changed, not applied yet
            b.Button(kInc(r, pg), "+", {Kind::Change, key, 1}, false, rowEdit, Role::Text, Part::More);
        }
    }

    // -- the low-life pages' own button: the vignette and the beat for five seconds with the rows as they stand (opening a page shows and plays nothing) ----------
    if (IsLifePage(m.page)) b.Button(kPreviewBtn, uitext::Tr("vr.hset.preview", "Preview 5 s"), {Kind::Preview, -1, 0}, false, true, Role::Accent);

    const std::string line = opt.status ? std::string(opt.status) : StatusLine(m, opt.window);
    b.Text(line, kStatus, Face::Normal, m.Dirty() && !opt.status && !m.LiveOnly() && m.page != kPageGuide ? Role::Warn : Role::Quiet);
    b.Button(kRecentre, uitext::Tr("vr.hset.recentre", "Recentre view"), {Kind::Recentre, -1, 0});
    b.Button(kClose, uitext::Tr("vr.hset.close", "Close"), {Kind::Close, -1, 0});
    b.Button(kApply, uitext::Tr("vr.hset.apply", "Apply"), {Kind::Apply, -1, 0}, true, m.ready && m.Dirty() && !m.Busy());
    return scene;
}

}  // namespace hset
