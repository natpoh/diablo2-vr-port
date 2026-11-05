// flat_menu.h - [hud] game_menu_settings (off by default): the flat game's (the monitor, platform 0) settings inside the game. With the game's
// pause menu open (Esc: its panel PauseLayout*), a "D2R VR settings" button stands under the game's own buttons; a click opens a panel of the
// keys that matter in 2D and, in a section of its own, the host's rules (vr/host_rules.h): as the host / in single player they are ours to
// change, joined they are only shown - the host's values, read over D2RLoader's plugin network. [hud] game_menu_key (F10 by default) opens
// and closes the same panel directly, in a menu or not (PanelKey, PanelKeyGate, PanelStays below).
//
// The panel writes the player's d2r_vr.ini one key at a time with WritePrivateProfileString (never the whole file); vrcam's watch of the
// ini's time reads it again (LoadSettings), so a live key changes at once. Drawn with D2RLoader's own overlay (rectangles and text after
// the game's interface); the mouse comes through the game window's procedure (vr/hud/window.cpp): while the panel is up its clicks and
// wheel are ours, the pointer's moves go on to the game (its own cursor), Esc closes the panel only.
//
// This header is the pure part (the rows, the layout in screen pixels, the hit test, a value's step and its ini text); the test is
// cleanroom/tests/host_rules_test.cpp, the game's side vr/flat_menu.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

#include "host_rules.h"
#include "ui_text.h"

namespace flatmenu {

enum class Kind { Toggle, Steps, Choice };

struct RowDef {
    const wchar_t* section;
    const wchar_t* key;
    Kind kind;
    float lo, hi, step, fallback;   // Toggle: 0 / 1; Choice: 0 .. hi
};

// The player's own 2D keys (D2R VR Settings has each, the same default): Camera's field of view and F2's distance back, UI: F3 eyes'
// weapon in hand and the body under the camera, Controls' flat aiming, picking, gamepad, the portal's oval and the pad's look up and down.
inline constexpr RowDef kOwn[] = {
    {L"camera", L"fov", Kind::Steps, 20.0f, 120.0f, 5.0f, 60.0f},
    {L"third", L"distance", Kind::Steps, 0.0f, 30.0f, 0.5f, 7.0f},
    {L"body", L"flat_weapon_adjust", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"body", L"flat_hold_trunk", Kind::Toggle, 0.0f, 1.0f, 1.0f, 0.0f},
    {L"input", L"flat_aim_ground", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"input", L"flat_body_pick", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"input", L"flat_no_pad", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"input", L"flat_portal_no_oval", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"camera", L"stick_pitch_flat", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
    {L"hud", L"flat_map_zoom", Kind::Toggle, 0.0f, 1.0f, 1.0f, 1.0f},
};
constexpr int kOwnCount = static_cast<int>(sizeof kOwn / sizeof kOwn[0]);
constexpr int kRows = kOwnCount + hostrules::kCount;   // the own rows, then the host's rules in kRules' order

inline bool IsRule(int row) { return row >= kOwnCount && row < kRows; }
inline int RuleOf(int row) { return IsRule(row) ? row - kOwnCount : -1; }
// A row as a definition: a rule is a Toggle (two values) or a Choice.
inline RowDef Def(int row) {
    if (row >= 0 && row < kOwnCount) return kOwn[row];
    const int i = RuleOf(row);
    if (i < 0) return {L"", L"", Kind::Toggle, 0, 1, 1, 0};
    const hostrules::Rule& r = hostrules::kRules[i];
    return {r.section, r.key, r.count == 2 ? Kind::Toggle : Kind::Choice, 0.0f, static_cast<float>(r.count - 1), 1.0f, static_cast<float>(r.fallback)};
}

inline const char* Label(int row) {
    if (IsRule(row)) return hostrules::Label(RuleOf(row));
    switch (row) {
        case 0: return uitext::Tr("vr.flatmenu.fov", "Field of view, degrees");
        case 1: return uitext::Tr("vr.flatmenu.third_distance", "Third person (F2): camera back");
        case 2: return uitext::Tr("vr.flatmenu.flat_weapon_adjust", "Weapon in hand: arms and weapon out of the middle (F3)");
        case 3: return uitext::Tr("vr.flatmenu.flat_hold_trunk", "Keep the body under the camera when attacking (F3)");
        case 4: return uitext::Tr("vr.flatmenu.flat_aim_ground", "Aim on raised ground where you look");
        case 5: return uitext::Tr("vr.flatmenu.flat_body_pick", "Click a monster, an NPC or a chest by its body");
        case 6: return uitext::Tr("vr.flatmenu.flat_no_pad", "Hide the gamepad from the game (mouse and keyboard)");
        case 7: return uitext::Tr("vr.flatmenu.flat_portal_no_oval", "No black oval in the town portal");
        case 8: return uitext::Tr("vr.flatmenu.stick_pitch_flat", "The gamepad's right stick looks up and down");
        case 9: return uitext::Tr("vr.flatmenu.flat_map_zoom", "- and + zoom the map out of its circle and back");
        default: return "?";
    }
}

// The ini's text as the row's value (a missing or unreadable key: the default), within the row's range.
inline float Clamp(const RowDef& d, float v) { return std::clamp(v, d.lo, d.hi); }
inline float ParseValue(const RowDef& d, const wchar_t* text) {
    if (!text || !*text) return d.fallback;
    wchar_t* end = nullptr;
    const double v = wcstod(text, &end);
    if (end == text || !std::isfinite(v)) return d.fallback;
    float f = static_cast<float>(v);
    if (d.kind != Kind::Steps) f = std::round(f);
    return Clamp(d, f);
}
// One click: a toggle flips (dir is not looked at), a choice goes one value on (no wrap), a slider one step on the step's grid.
inline float Step(const RowDef& d, float v, int dir) {
    if (d.kind == Kind::Toggle) return v >= 0.5f ? 0.0f : 1.0f;
    const float s = d.step > 0 ? d.step : 1.0f;
    const float on = std::round((v - d.lo) / s) * s + d.lo;   // a value off the grid (typed by hand) comes onto it first
    const float next = std::fabs(on - v) > 1e-4f && ((dir > 0) == (on > v)) ? on : on + (dir > 0 ? s : -s);
    return Clamp(d, std::round(next * 1000.0f) / 1000.0f);
}
// The value as the ini gets it: a whole number without a point, else at most two decimals ("7.5", "0.25").
inline std::wstring IniText(float v) {
    wchar_t b[32];
    if (std::fabs(v - std::round(v)) < 1e-4f) swprintf(b, 32, L"%d", static_cast<int>(std::round(v)));
    else {
        swprintf(b, 32, L"%.2f", v);
        size_t n = wcslen(b);
        while (n > 0 && b[n - 1] == L'0') b[--n] = 0;
    }
    return b;
}
// The value as the panel shows it.
inline std::string Shown(int row, float v) {
    const RowDef d = Def(row);
    if (IsRule(row)) return hostrules::ValueName(RuleOf(row), static_cast<int>(std::round(v)));
    if (d.kind == Kind::Toggle) return v >= 0.5f ? uitext::Tr("vr.rules.on", "On") : uitext::Tr("vr.rules.off", "Off");
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-4f) snprintf(b, sizeof b, "%d", static_cast<int>(std::round(v)));
    else snprintf(b, sizeof b, "%.1f", v);
    return b;
}

// ---- the layout, in the overlay's pixels (its frame's screen size) ----
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool In(float px, float py) const { return w > 0 && h > 0 && px >= x && px < x + w && py >= y && py < y + h; }
};
struct Layout {
    float scale = 1, text = 20, small = 16;
    Rect panel, title, close, ownHead, rulesHead, role;
    Rect row[kRows], minus[kRows], value[kRows], plus[kRows];
};
// The pause menu's button: under the game's own buttons, in the middle.
inline Rect MenuButton(float w, float h) {
    const float s = h > 0 ? h / 1080.0f : 1.0f;
    const float bw = 340.0f * s, bh = 52.0f * s;
    return {(w - bw) * 0.5f, h * 0.84f, bw, bh};
}
inline Layout Lay(float w, float h) {
    Layout L;
    const float s = h > 0 ? h / 1080.0f : 1.0f;
    L.scale = s;
    L.text = 22.0f * s;
    L.small = 18.0f * s;
    const float pad = 16.0f * s, rowH = 38.0f * s, headH = 40.0f * s, titleH = 56.0f * s, bottomH = 64.0f * s;
    const float pw = std::min(w - 2 * pad, 1040.0f * s);
    const float ph = titleH + headH + kOwnCount * rowH + headH + rowH + hostrules::kCount * rowH + bottomH + pad;
    L.panel = {(w - pw) * 0.5f, std::max(pad, (h - ph) * 0.5f), pw, ph};
    float y = L.panel.y;
    L.title = {L.panel.x + pad, y, pw - 2 * pad, titleH};
    y += titleH;
    L.ownHead = {L.panel.x + pad, y, pw - 2 * pad, headH};
    y += headH;
    const float btn = 40.0f * s, valW = 210.0f * s, gap = 6.0f * s;
    const float right = L.panel.x + pw - pad;
    auto put = [&](int r) {
        L.row[r] = {L.panel.x + pad, y, pw - 2 * pad, rowH};
        const float by = y + 3.0f * s, bh = rowH - 6.0f * s;
        L.plus[r] = {right - btn, by, btn, bh};
        L.value[r] = {right - btn - gap - valW, by, valW, bh};
        L.minus[r] = {L.value[r].x - gap - btn, by, btn, bh};
        y += rowH;
    };
    for (int r = 0; r < kOwnCount; ++r) put(r);
    L.rulesHead = {L.panel.x + pad, y, pw - 2 * pad, headH};
    y += headH;
    L.role = {L.panel.x + pad, y, pw - 2 * pad, rowH};
    y += rowH;
    for (int r = kOwnCount; r < kRows; ++r) put(r);
    const float cw = 200.0f * s, ch = 46.0f * s;
    L.close = {L.panel.x + (pw - cw) * 0.5f, y + (bottomH - ch) * 0.5f, cw, ch};
    return L;
}

// What a row shows as buttons: a toggle its value as one button; a slider or a choice "-" value "+"; a rule only where we decide.
inline bool HasSteps(int row) { return Def(row).kind != Kind::Toggle; }
inline bool Editable(int row, hostrules::Role role) { return row >= 0 && row < kRows && (!IsRule(row) || role != hostrules::Role::Joined); }

enum class Part { None, Panel, Close, Minus, Value, Plus };
struct Hit { Part part = Part::None; int row = -1; };
inline Hit HitTest(const Layout& L, float x, float y, hostrules::Role role) {
    if (L.close.In(x, y)) return {Part::Close, -1};
    for (int r = 0; r < kRows; ++r) {
        if (!Editable(r, role)) continue;
        if (HasSteps(r)) {
            if (L.minus[r].In(x, y)) return {Part::Minus, r};
            if (L.plus[r].In(x, y)) return {Part::Plus, r};
        } else if (L.value[r].In(x, y)) {
            return {Part::Value, r};
        }
    }
    return {L.panel.In(x, y) ? Part::Panel : Part::None, -1};
}
// A click as the value's change: the new value, or false (nothing to change: off the buttons, at the end of the range, a rule not ours).
inline bool Click(const Hit& hit, float now, float* next) {
    if (hit.row < 0 || hit.row >= kRows) return false;
    if (hit.part != Part::Minus && hit.part != Part::Plus && hit.part != Part::Value) return false;
    const float v = Step(Def(hit.row), now, hit.part == Part::Minus ? -1 : 1);
    if (std::fabs(v - now) < 1e-5f) return false;
    *next = v;
    return true;
}

// The client's pixel -> the overlay's (the game may draw at another size than its window: [render] vr_resolution).
inline void ToOverlay(float cx, float cy, float clientW, float clientH, float overlayW, float overlayH, float* ox, float* oy) {
    *ox = clientW > 0 ? cx * overlayW / clientW : cx;
    *oy = clientH > 0 ? cy * overlayH / clientH : cy;
}

// [hud] host_rules_key: 0 none, 1 Home, 2 End, 3 Insert (the list shows while it is held) - as a virtual key, 0 none.
inline unsigned ListKey(int choice) {
    switch (choice) {
        case 1: return 0x24;   // VK_HOME
        case 2: return 0x23;   // VK_END
        case 3: return 0x2D;   // VK_INSERT
        default: return 0;
    }
}

// [hud] game_menu_key: the key that opens and closes the panel itself in the flat game, menus or not (Serg 2026-11-05: Esc showed no button in
// 0.202.2 - D2RLoader's overlay had refused the late registration, and a key is quicker anyway). 0 none, 1 F10 (default), 2 Insert,
// 3 Delete, 4 Page Up, 5 Page Down - as a virtual key, 0 none. F10 is the game's Fade Automap by default and ours anyway with [view]
// own_hotkeys; F11 / F12 are recentre / next view. One key for both the panel and host_rules_key's list: the panel's.
inline unsigned PanelKey(int choice) {
    switch (choice) {
        case 1: return 0x79;   // VK_F10
        case 2: return 0x2D;   // VK_INSERT
        case 3: return 0x2E;   // VK_DELETE
        case 4: return 0x21;   // VK_PRIOR
        case 5: return 0x22;   // VK_NEXT
        default: return 0;
    }
}

// The panel key's messages. F10 comes as WM_SYSKEYDOWN / WM_SYSKEYUP (a system key), the others as WM_KEYDOWN / WM_KEYUP: the window
// procedure passes both as `down` true / false. A first press (no repeat, no Ctrl: Ctrl + F10 stays the frame trace) toggles the panel;
// its repeats and its release are taken too, so the game sees nothing of it (Fade Automap stays put); a press let through keeps its release.
struct PanelKeyGate {
    bool taken = false;
    enum class Act { Pass, Toggle, Swallow };
    Act Feed(unsigned vk, unsigned panelVk, bool down, bool repeat, bool ctrl) {
        if (!panelVk || vk != panelVk) return Act::Pass;
        if (!down) {
            const bool ours = taken;
            taken = false;
            return ours ? Act::Swallow : Act::Pass;
        }
        if (taken) return Act::Swallow;   // a repeat of our press
        if (repeat || ctrl) return Act::Pass;
        taken = true;
        return Act::Toggle;
    }
};

// Whether the open panel stays open. Not flat (a VR view): closed. Opened from the pause menu's button: it closes with the pause menu.
// Opened by the key: it stays until the key, Esc or its Close button (menus opening and closing under it change nothing).
inline bool PanelStays(bool flat, bool fromPause, bool pauseOpen) {
    if (!flat) return false;
    return !fromPause || pauseOpen;
}

// When the rules' list is on the screen: the key held (flat), the pause menu open (flat and VR - its picture is the flat one there) in a game
// someone else hosts, and the first seconds after a host's rules first came in a game we joined (Counter-Strike's look at a server's rules).
constexpr uint64_t kJoinShowMs = 10000;
inline bool ListShown(bool on, bool flat, bool keyHeld, bool pauseOpen, bool panelOpen, hostrules::Role role, uint64_t now, uint64_t joinedShowFrom) {
    if (!on || panelOpen) return false;
    if (flat && keyHeld) return true;
    if (role != hostrules::Role::Joined) return false;
    if (pauseOpen) return true;
    return joinedShowFrom && now >= joinedShowFrom && now - joinedShowFrom < kJoinShowMs;
}

}  // namespace flatmenu
