#pragma once
// The settings pages as a description of a picture: what is drawn where, in the sheet's pixels (hset_model.h's layout, 1024 x 720) and
// in which role - a fill, a line of text, a button with its state. A model and the button under the beam in, a list of items out.
// Pure (no Windows, no OpenXR), so what a page shows is tested without drawing it. Two painters render it:
//   vr/hset_paint.cpp  the GDI picture of the panel of its own ([openxr] headset_settings without the skill panel's tiles),
//   vr/tiles_paint.cpp the same pages on the skill panel window's Settings tab, scaled and in that window's own style.
// The hit test (hset_model.h HitTest) reads the same rectangles, so what is drawn is what is clicked.

#include "direct_hit_mode.h"   // the Melee > Attack row's Direct hit modes
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
// Check: a tick box with its word beside it (the Co-op page's map row, Serg 2026-11-05 "галочку"): the game look draws it as Switch (the game's own
// check box), the panel's own painter a box with a tick when selected.
enum class Part : uint8_t { None, Row, Value, Track, TrackFill, Knob, Tab, Less, More, Switch, Choice, Flip, Action, Check };

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

// The Melee tab's pages with hit by touch on and Direct hit By the rules or Always (Melee > Attack's row: shared/direct_hit_mode.h): its line warns, in
// amber - Direct hit changes the game's own logic (D2R VR Settings > Strikes > Attention). Two-handed extras (only a two-handed swing's further monsters:
// the game has no one-swing-many-monsters) - a line of its own, not the warning.
inline int DirectHitMode(const Model& m) { return (int)std::lround(m.Get()[kKeyMeleeDirectClient]); }
inline bool DirectHitCleaveOnly(const Model& m) { return IsMeleePage(m.page) && m.Get()[kKeyMeleeTouch] > 0.5f && DirectHitMode(m) == directhit::kCleave; }
inline bool DirectHitWarns(const Model& m) {
    return IsMeleePage(m.page) && m.Get()[kKeyMeleeTouch] > 0.5f && (DirectHitMode(m) == directhit::kRules || DirectHitMode(m) == directhit::kFull);
}
// The line under the rows for a model's state. `window`: the pages are the skill panel's Settings tab (it is closed by B, its Close
// button or the long press of the right stick click, not by the grips and a click).
inline const char* StatusText(const Model& m, bool window = false) {
    if (m.page == kPageGuide) return firststart::Nav(std::clamp(m.guide, 0, firststart::kPageCount - 1));   // the guide's own foot, whatever else is going on
    if (m.page == kPageStats) return stats::StatusText(m.Stats());   // the strike statistics: nothing of d2r_vr.ini
    if (m.page == kPageParty) {   // the game's party: nothing of d2r_vr.ini
        if (!m.PartyOn()) return uitext::Tr("vr.party.status.off", "Party is off: D2R VR Authentic Settings, Party in the headset.");
        if (!m.Party().known) return uitext::Tr("vr.party.status.no_game", "Parties are in a multiplayer game (TCP/IP or online): here there is no one to invite.");
        if (m.PartyBusy()) return uitext::Tr("vr.party.status.sent", "Sent to the game: it answers in its own words.");
        return uitext::Tr("vr.party.status.idle", "Each button is the game's own Party panel button. Open Party panel: the game's panel (P).");
    }
    if (m.page == kPageCoop) {   // D2R Rooms' own file, written at once: d2r_vr.ini (there or not, edited or not) is not this page's
        if (!m.Coop()) return uitext::Tr("vr.coop.status.reading", "Reading D2R Rooms' files...");
        if (!m.Coop()->installed) return uitext::Tr("vr.coop.status.missing", "Nothing to choose: the D2R Rooms plugin is not installed.");
        switch (m.CoopResult()) {
            case Result::Saving: return uitext::Tr("vr.coop.status.saving", "Writing d2r-rooms.ini...");
            case Result::Saved: return uitext::Tr("vr.coop.status.saved", "Saved in d2r-rooms.ini. It counts from your next CREATE GAME.");
            case Result::Failed: return uitext::Tr("vr.coop.status.failed", "Could not write d2r-rooms.ini. Nothing changed.");
            default: break;
        }
        return uitext::Tr("vr.coop.status.idle", "Each change is written to d2r-rooms.ini at once (the plugin's own file).");
    }
    if (m.page == kPageVideo && m.AutoShown()) return m.Auto().status.c_str();   // the Auto run's own line (the plugin's words)
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
    if (IsMeleePage(m.page) && m.Get()[kKeyMeleeTouch] > 0.5f && DirectHitMode(m) == directhit::kFull)
        return uitext::Tr("vr.hset.status.melee_direct_always", "Direct hit Always (not recommended): every touch goes to the server, past the game's rules.");
    if (DirectHitCleaveOnly(m))   // Serg (2026-11-05): "директ хит должен по прежнему быть включен для двуручного меча и топоров"
        return uitext::Tr("vr.hset.status.melee_cleave", "Direct hit 2H extras: a two-handed weapon in both hands - a swing's further monsters get Direct hit.");
    if (DirectHitWarns(m))   // Serg (2026-11-02): the same warning as D2R VR Settings' Attention block
        return uitext::Tr("vr.hset.status.melee_direct_on", "Attention: Direct hit changes the game's own logic - hits go to the server directly; in co-op everyone is affected.");
    if (IsWorldPage(m.page))
        return uitext::Tr("vr.hset.status.world_idle", "Each change applies by itself (F4). They need the arms on the controllers; the rest is in D2R VR Authentic Settings.");
    if (IsMeleePage(m.page))
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

// The Options page's Players row: the line under its name - what it does, or why - / + do nothing now.
inline std::string PlayersDetail(const Model& m) {
    if (!m.PlayersOn()) return uitext::Tr("vr.hset.players.off", "Off: D2R VR Authentic Settings, Players from the headset");
    if (m.GamePlayers().count < kPlayersMin) return uitext::Tr("vr.hset.players.unknown", "The game's count is read once your hero is in a game");
    if (!m.GamePlayers().takes) return uitext::Tr("vr.hset.players.not_here", "Only in a game of your own: single player or a TCP/IP host");
    return DefDetail(kKeyPlayers);
}
// What the game answered the row's last press, for the line under the rows ("" none).
inline std::string PlayersAnswerText(const Model& m) {
    if (m.PlayersAnswer() > 0)
        return uitext::TrF("vr.hset.players.took", "The game set players to {n} (as /players {n}): monsters met from now on.",
                           {{"n", std::to_string(m.GamePlayers().count)}});
    if (m.PlayersAnswer() < 0)
        return uitext::Tr("vr.hset.players.refused", "The game did not take it: only in single player, or as the host of a TCP/IP game.");
    return "";
}

// The line under the rows as text (StatusText for the pages whose line is a fixed one; the Options page's Players row tells the game's answer).
inline std::string StatusLine(const Model& m, bool window = false) {
    if (m.page == kPageOptions && m.ready && m.result == Result::None && !m.Dirty() && m.PlayersAnswer() != 0) return PlayersAnswerText(m);
    return StatusText(m, window);
}

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
    for (int i = 0; i < kTabCount; ++i)   // (the view's tabs only: VR F1 - F3 leave out F4's body, hset_defs.h TabShown)
        if (TabShown(i, m.View())) b.Button(TabRect(i, m.View()), TabName(i), {Kind::Tab, TabPage(i), 0}, TabOf(m.page) == i, true, Role::Text, Part::Tab);
    if (HasSubRow(m.page)) {   // -- a tab of several pages (World's four, Controls' two): a button each, the one shown lit (a flick goes along them too) ------
        const int first = SubFirst(m.page), n = SubCount(m.page);
        for (int i = 0; i < n; ++i)
            b.Button(kSubBtn(i, n), PageName(first + i), {Kind::Tab, first + i, 0}, m.page == first + i, true, m.page == first + i ? Role::Accent : Role::Text, Part::Choice);
    }

    const bool canEdit = m.ready && !m.Busy();
    if (m.page == kPageVideo && m.AutoShown()) {
        // -- the Auto run: its lines, Keep / Undo (Stop while it measures) -----------------------------------------
        const AutoTune& at = m.Auto();
        b.Text(at.title, kAutoTitle, Face::Bold, at.state == 2 ? Role::Accent : Role::Warn);
        for (int i = 0; i < kAutoLines && i < (int)at.lines.size(); ++i) b.Text(at.lines[i], kAutoLine(i), Face::Small, Role::Text);
        if (at.state == 2) b.Button(kAutoKeep, uitext::Tr("vr.autotune.keep", "Keep"), {Kind::Auto, 1, 0}, true, true, Role::Accent);
        b.Button(kAutoUndo, at.state == 2 ? uitext::Tr("vr.autotune.undo", "Undo") : uitext::Tr("vr.autotune.stop", "Stop"), {Kind::Auto, 2, 0});
    } else if (m.page == kPageVideo) {
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
        if (m.Auto().on)   // [autotune]: measures and steps the settings down to the headset's rate (not with a size picked and not applied)
            b.Button(kAutoBtn, uitext::Tr("vr.autotune.button", "Auto"), {Kind::Auto, 0, 0}, false, canEdit && !edited, Role::Accent);
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
    if (m.page == kPageCoop) {
        // -- D2R Rooms' relay bridge (vr/hset_coop.h): its three choices as rows, then what the plugin knows ------------------------------------
        const roomsbridge::Config shown = m.CoopShown();
        if (m.CoopReady()) {
            const bool free = !m.CoopBusy();
            for (int r = 0; r < kCoopRows; ++r) {
                const int y = RowY(r, -1);
                const bool rowOn = free && (r == coop::kEnabled || shown.enabled);
                const bool pending = m.CoopBusy();   // amber while the line is on its way
                b.Fill({32, y - 5, 960, kRowStep - 2}, r % 2 ? Role::RowB : Role::RowA, Part::Row);
                b.Text(coop::Label(r), {48, y - 2, 520, 32}, Face::Bold, r == coop::kEnabled || shown.enabled ? Role::Text : Role::Quiet);
                b.Text(coop::Detail(shown, r), {48, y + 28, 530, 28}, Face::Small, Role::Quiet);
                if (r == coop::kEnabled) {
                    b.Button(kToggle(r), coop::ValueText(shown, r), {Kind::Coop, r, 1}, shown.enabled, rowOn, pending ? Role::Warn : Role::Text, Part::Switch);
                } else {
                    b.Button(kDec(r), "<", {Kind::Coop, r, -1}, false, rowOn, Role::Text, Part::Less);
                    b.Fill(kVal(r), Role::Back, Part::Value);
                    b.Text(coop::ValueText(shown, r), kVal(r), Face::Bold, pending ? Role::Warn : shown.enabled ? Role::Text : Role::Quiet, Align::Center);
                    b.Button(kInc(r), ">", {Kind::Coop, r, 1}, false, rowOn, Role::Text, Part::More);
                }
            }
        }
        // [openxr] headset_map_party: the game's own Show Party / Show Party Names (hset_party.h MapShow) - under the three, the plugin there or not
        const bool mapRow = m.MapShowOn();
        if (mapRow) {
            const party::MapShow& gm = m.GameMap();
            const int y = RowY(kMapRow, -1);
            b.Fill({32, y - 5, 960, kRowStep - 2}, kMapRow % 2 ? Role::RowB : Role::RowA, Part::Row);
            b.Text(party::MapLabel(), {48, y - 2, 520, 32}, Face::Bold, Role::Text);
            b.Text(party::MapDetail(gm), {48, y + 28, 530, 28}, Face::Small, gm.party == 0 ? Role::Warn : Role::Quiet);
            for (int w = 0; w < party::kMapCount; ++w) {
                const int v = m.MapShown(w);
                b.Button(kMapBtn(w), party::MapButton(w), {Kind::MapShow, w, 1}, v > 0, v >= 0 && !m.MapBusy(), m.MapAsked() == w ? Role::Warn : Role::Text,
                         Part::Check);   // ticked: the game shows it (amber: pressed, the game not there yet)
            }
        }
        const bool under = mapRow && m.CoopReady();   // the lines below the map row then, fewer
        const int most = under ? kCoopLinesUnderMap : coop::kLines;
        const std::vector<coop::Line> lines = coop::Lines(m.Coop(), shown, most);
        const int first = m.CoopReady() ? 0 : -6;   // nothing to choose: the lines stand where the rows would be
        for (int i = 0; i < (int)lines.size() && i < most; ++i) {
            const coop::Line& l = lines[(size_t)i];
            const Role ink = l.tone == coop::Tone::Good ? Role::Accent : l.tone == coop::Tone::Warn ? Role::Warn : l.tone == coop::Tone::Head ? Role::Text : Role::Quiet;
            b.Text(l.text, under ? kCoopLineUnderMap(i) : kCoopLine(first + i), l.tone == coop::Tone::Head ? Face::Bold : Face::Small, ink);
        }
    }
    if (m.page == kPageParty) {
        // -- the players in the game and the game's own party buttons (vr/hset_party.h) ----------------------------------------------------------
        const party::View& pv = m.Party();
        const bool on = m.PartyOn(), free = on && pv.known && !m.PartyBusy();
        b.Text(uitext::Tr("vr.party.title", "Players in this game"), kPartyTitle, Face::Bold, Role::Text);
        if (on && pv.known)
            b.Text(pv.inParty ? uitext::Tr("vr.party.you_in_party", "You are in a party") : uitext::Tr("vr.party.you_alone", "You are in no party"), kPartyTitle,
                   Face::Small, pv.inParty ? Role::Accent : Role::Quiet, Align::Right);
        if (on && pv.known && pv.others.empty()) b.Text(uitext::Tr("vr.party.nobody", "No one else is in this game yet."), kPartyName(0), Face::Small, Role::Quiet);
        for (int i = 0; on && pv.known && i < (int)pv.others.size() && i < party::kMaxOthers; ++i) {
            const party::Member& p = pv.others[(size_t)i];
            const party::Act act = party::ActFor(p.state);
            const bool asked = m.PartyAsked().what == party::Ask::Player && m.PartyAsked().id == p.id;
            b.Fill(kPartyRow(i), i % 2 ? Role::RowB : Role::RowA, Part::Row);
            b.Text(p.name, kPartyName(i), Face::Bold, Role::Text);
            b.Text(party::InfoText(p), kPartyInfo(i), Face::Small, Role::Quiet);
            b.Text(party::StateText(p), kPartyState(i), Face::Small, p.sameParty || p.state == party::kInParty ? Role::Accent : Role::Quiet);
            if (act != party::Act::None)
                b.Button(kPartyBtn(i), party::ActText(act), {Kind::Party, i, 0}, act == party::Act::Accept, free, asked ? Role::Warn : Role::Text, Part::Action);
        }
        b.Button(kPartyLeave, uitext::Tr("vr.party.leave", "Leave party"), {Kind::PartyLeave, -1, 0}, false, free && pv.inParty);
        b.Button(kPartyOpen, uitext::Tr("vr.party.open", "Open Party panel (P)"), {Kind::PartyOpen, -1, 0}, false, on && pv.known);
    }
    if (m.page == kPageStats) {
        // -- the strike statistics (vr/hset_stats.h): the hero and the period, four totals, a row a weapon --------------------------------------
        const stats::View& sv = m.Stats();
        const int hero = m.StatsHero();
        const bool all = m.StatsAllTime();
        const int heroes = (int)sv.heroes.size();
        b.Button(kStatsPrev, "<", {Kind::Stats, 0, -1}, false, heroes > 1, Role::Text, Part::Less);
        b.Button(kStatsNext, ">", {Kind::Stats, 0, 1}, false, heroes > 1, Role::Text, Part::More);
        b.Button(kStatsNow, uitext::Tr("vr.stats.this_game_btn", "This game"), {Kind::Stats, 1, 0}, !all, true, !all ? Role::Accent : Role::Text, Part::Choice);
        b.Button(kStatsAll, uitext::Tr("vr.stats.all_time_btn", "All time"), {Kind::Stats, 1, 1}, all, true, all ? Role::Accent : Role::Text, Part::Choice);
        if (hero < 0) {
            b.Text(uitext::Tr("vr.stats.no_hero", "No blows counted yet"), kStatsName, Face::Bold, Role::Quiet);
        } else {
            const strikestats::Hero& h = sv.heroes[(size_t)hero];
            const strikestats::Agg& g = stats::Of(h, all);
            b.Text(stats::HeroTitle(h), kStatsName, Face::Bold, Role::Text);
            b.Text(stats::HeroLine(sv, hero, all), kStatsLine, Face::Small, Role::Quiet);
            const std::string caps[4] = {uitext::Tr("vr.stats.blows", "Blows"), uitext::Tr("vr.stats.speed", "Speed"), uitext::Tr("vr.stats.force", "Force"),
                                         uitext::Tr("vr.stats.landed", "Landed")};
            const std::string vals[4] = {stats::Count(g.blows), g.speedN ? stats::Num1(g.SpeedAvg()) + " m/s" : std::string("-"),
                                         g.forceN ? stats::Num1(g.ForceAvg()) + " N s" : std::string("-"), stats::Landed(g)};
            const std::string subs[4] = {
                uitext::TrF("vr.stats.missed_sub", "and {n} swings that missed", {{"n", stats::Count(g.Misses())}}),
                g.speedN ? uitext::TrF("vr.stats.best_sub", "best {v}", {{"v", stats::Num1(g.speedBest)}}) : std::string(),
                g.forceN ? uitext::TrF("vr.stats.best_sub", "best {v}", {{"v", stats::Num1(g.forceBest)}}) : std::string(uitext::Tr("vr.stats.no_force", "a weapon with weight")),
                uitext::Tr("vr.stats.landed_sub", "of the swings that touched")};
            for (int i = 0; i < 4; ++i) {
                b.Fill(kStatsTile(i), Role::RowB, Part::Row);
                b.Text(caps[i], kStatsTileCap(i), Face::Small, Role::Quiet, Align::Center);
                b.Text(vals[i], kStatsTileVal(i), Face::Bold, i < 3 ? Role::Accent : Role::Text, Align::Center);
                if (!subs[i].empty()) b.Text(subs[i], kStatsTileSub(i), Face::Small, Role::Quiet, Align::Center);
            }
            const char* const heads[5] = {uitext::Tr("vr.stats.col.weapon", "Weapon"), uitext::Tr("vr.stats.col.blows", "Blows"), uitext::Tr("vr.stats.col.speed", "Speed m/s"),
                                          uitext::Tr("vr.stats.col.force", "Force N s"), uitext::Tr("vr.stats.col.missed", "Missed")};
            const int more = stats::RowsLeftOut(h, all);   // (the weapon's head says how many the rows leave out: "Weapon - 4 of 6")
            const std::string head0 = more > 0 ? uitext::TrF("vr.stats.col.weapon_of", "Weapon - {n} of {all}", {{"n", std::to_string(stats::kRows)}, {"all", std::to_string(stats::kRows + more)}})
                                               : std::string(heads[0]);
            for (int c = 0; c < 5; ++c) b.Text(c ? std::string(heads[c]) : head0, kStatsCol(c, kStatsHeadY, 26), Face::Small, Role::Accent, c ? Align::Right : Align::Left);
            const std::vector<const strikestats::Weapon*> rows = stats::Rows(h, all);
            if (rows.empty())
                b.Text(all ? uitext::Tr("vr.stats.none_ever", "No blows by touch yet.") : uitext::Tr("vr.stats.none_game", "No blows by touch in this game yet."),
                       kStatsCol(0, kStatsRowY0, 30), Face::Small, Role::Quiet);
            for (int i = 0; i < (int)rows.size(); ++i) {
                const strikestats::Weapon& w = *rows[(size_t)i];
                const strikestats::Agg& a = stats::Of(w, all);
                const Rect row = kStatsRow(i);
                b.Fill(row, i % 2 ? Role::RowB : Role::RowA, Part::Row);
                b.Text(stats::WeaponName(w), kStatsCol(0, row.y - 1, 32), Face::Bold, Role::Text);
                b.Text(stats::KindText(w.kind), kStatsCol(0, row.y + 28, 26), Face::Small, Role::Quiet);
                const int ny = row.y + 10, nh = 34;
                b.Text(stats::Count(a.blows), kStatsCol(1, ny, nh), Face::Bold, Role::Text, Align::Right);
                b.Text(stats::AvgBest(a.SpeedAvg(), a.speedBest, a.speedN), kStatsCol(2, ny, nh), Face::Normal, Role::Text, Align::Right);
                b.Text(stats::AvgBest(a.ForceAvg(), a.forceBest, a.forceN), kStatsCol(3, ny, nh), Face::Normal, Role::Text, Align::Right);
                b.Text(stats::Count(a.Misses()), kStatsCol(4, ny, nh), Face::Normal, Role::Quiet, Align::Right);
            }
        }
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
    const bool world = HasSubRow(pg);   // under a tab's page buttons (World, Controls): closer still, the name in the normal face (D2R VR Settings' longer labels fit it)
    for (int r = 0; r < kRowsMax; ++r) {
        const int key = KeyAt(pg, r, m.View());   // (the view's rows: VR F1 - F3 leave out F4's, hset_defs.h KeyShown)
        if (key < 0) break;
        const Def& d = kDefs[key];
        const int y = RowY(r, pg);
        const bool rowEdit = canEdit;
        b.Fill({32, y - 5, 960, step - 2}, r % 2 ? Role::RowB : Role::RowA, Part::Row);
        b.Text(DefLabel(key), world ? Rect{48, y - 5, 530, 27} : close ? Rect{48, y - 4, 520, 30} : Rect{48, y - 2, 520, 32}, world ? Face::Normal : Face::Bold, Role::Text);
        b.Text(d.type == DefType::Choice ? ChoiceDetail(m, key) : d.type == DefType::Game ? PlayersDetail(m) : std::string(DefDetail(key)),
               world ? Rect{48, y + 21, 530, 24} : close ? Rect{48, y + 24, 530, 26} : Rect{48, y + 28, 530, 28}, Face::Small, Role::Quiet);
        const float v = m.Get()[key];
        if (d.type == DefType::Game) {   // the game's players count: what the game says (amber: asked, not shown by the game yet), "-" not known
            const bool usable = m.PlayersUsable();
            const int shown = m.PlayersShown();
            b.Button(kDec(r, pg), "-", {Kind::Change, key, -1}, false, usable && shown > kPlayersMin, Role::Text, Part::Less);
            b.Fill(kVal(r, pg), Role::Back, Part::Value);
            b.Text(shown >= kPlayersMin ? std::to_string(shown) : std::string("-"), kVal(r, pg), Face::Bold,
                   m.PlayersAsked() ? Role::Warn : m.PlayersOn() && m.GamePlayers().takes ? Role::Text : Role::Dim, Align::Center);
            b.Button(kInc(r, pg), "+", {Kind::Change, key, 1}, false, usable && shown < kPlayersMax, Role::Text, Part::More);
        } else if (d.type == DefType::Choice) {
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
    // the Co-op tab's two pages: Party from the relay's, Relay back ([openxr] headset_party off: no Party button; the page, reached by a flick, says so)
    if (m.page == kPageCoop && m.PartyOn()) b.Button(kPreviewBtn, uitext::Tr("vr.party.go", "Party"), {Kind::Tab, kPageParty, 0}, false, true, Role::Accent);
    if (m.page == kPageParty) b.Button(kPreviewBtn, uitext::Tr("vr.party.back", "Relay"), {Kind::Tab, kPageCoop, 0});

    const std::string line = opt.status ? std::string(opt.status) : StatusLine(m, opt.window);
    const bool warn = m.page == kPageCoop ? !opt.status && m.Coop() && (!m.Coop()->installed || m.CoopResult() == Result::Failed)
                                          : !opt.status && ((m.Dirty() && !m.LiveOnly() && m.page != kPageGuide)
                                                            || (m.ready && m.result == Result::None && !m.Dirty() && DirectHitWarns(m)));   // (its warning line)
    b.Text(line, kStatus, Face::Normal, warn ? Role::Warn : Role::Quiet);
    b.Button(kRecentre, uitext::Tr("vr.hset.recentre", "Recentre view"), {Kind::Recentre, -1, 0});
    b.Button(kClose, uitext::Tr("vr.hset.close", "Close"), {Kind::Close, -1, 0});
    b.Button(kApply, uitext::Tr("vr.hset.apply", "Apply"), {Kind::Apply, -1, 0}, true, m.ready && m.Dirty() && !m.Busy());
    return scene;
}

}  // namespace hset
