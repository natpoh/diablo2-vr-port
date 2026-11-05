// view_keys.h - which keyboard keys pick the view ([view] keys, Settings > Home > View keys). Pure: no window, no GetAsyncKeyState; the
// unit test is cleanroom/tests/view_keys_test.cpp, the wiring vr/hud/window.cpp (the game window's messages: what the game does not get)
// and vr/frame/frame_order.cpp (the update thread's poll: what switches the view).
//
// F1 - F4 picked the view since the views came, and the game never saw them - but the game has its skill hotkeys there (F1 - F8). A player
// (barti4033, 2026-11-04) plays with F1 / F2 / F4 as his main skills and F3 as a panic teleport. Serg's idea: Shift + F1 instead of F1.
//   0 (kPlain, the default)  F1 - F4 pick the view, the game never gets them - as before.
//   1 (kShift)               Shift + F1 - F4 pick the view and the game does not get that press; a plain F1 - F8 is the game's skill hotkey.
//   2 (kNone)                no F key picks a view: F12, Settings and the controller's view actions only; F1 - F8 are all the game's.
// F12 (next view), F11 (recentre), the controller's view actions (D2RVR_ACT_VIEW_*) and Settings' radios are the same in every mode.
// Shift + F is free in the plugin (Ctrl + F9 / Ctrl + F10 / Ctrl + Shift + F9 are the debug ones). Shift held in the game is "stand
// still": a short Shift + F1 is harmless.
//
// The game reads its hotkeys from WM_KEYDOWN. A press the plugin takes is taken whole: its repeats and its release go with it, whatever
// Shift does in the meantime (Shift let go before F1: the release is still ours; a plain F1 held and Shift pressed: the release is the game's).
#pragma once

#include <cstdint>

namespace viewkeys {

enum Mode : int { kPlain = 0, kShift = 1, kNone = 2 };

inline int ModeFromIni(float v) {
    const int m = (int)v;
    return m < kPlain || m > kNone ? kPlain : m;
}

inline bool IsPlain(int mode) { return mode != kShift && mode != kNone; }

inline const char* ModeName(int mode) {
    switch (mode) {
    case kShift: return "Shift + F1 - F4 pick the view, a plain F1 - F8 is the game's";
    case kNone:  return "no F key picks a view (F12, Settings, the controller) - F1 - F8 are the game's";
    default:     return "F1 - F4 pick the view, the game does not get them";
    }
}

// The update thread's poll: a press (its edge) of view key k switches the view? `k` 0-based (F1 = 0), `viewCount` the views there are.
inline bool Switches(int mode, int k, int viewCount, bool shiftHeld) {
    if (k < 0 || k >= viewCount) return false;
    switch (mode) {
    case kShift: return shiftHeld;
    case kNone:  return false;
    default:     return true;   // kPlain, and anything else (not read yet: -1) - as before
    }
}

// The game window's messages: true = the game does not get this one. One per window, on its thread.
struct Gate {
    uint32_t taken = 0;   // bit k: view key k's press was taken - its repeats and its release go with it

    // `vk` the message's key, `down` WM_KEYDOWN (false: WM_KEYUP), `repeat` the key-down's repeat bit (lParam bit 30).
    bool Take(int mode, int viewCount, unsigned vk, bool down, bool repeat, bool shiftHeld) {
        const int k = (int)vk - 0x70;   // VK_F1
        if (k < 0 || k >= viewCount || k >= 32) return false;
        const uint32_t bit = 1u << k;
        if (!down) {
            const bool ours = IsPlain(mode) || (taken & bit) != 0;   // kPlain: every release, as before
            taken &= ~bit;
            return ours;
        }
        if (repeat && !IsPlain(mode)) return (taken & bit) != 0;
        const bool ours = Switches(mode, k, viewCount, shiftHeld);
        taken = ours ? taken | bit : taken & ~bit;
        return ours;
    }
};

// [view] own_hotkeys (Settings > Home, under View keys): F9 - F12 are the plugin's own keys in every view and every mode - F9 mouse look
// (Ctrl + F9 memdiff), F10 the skeleton dump (Ctrl + F10 the frame trace), F11 recentre, F12 next view - read with GetAsyncKeyState, so the
// game got the same press too. Diablo II's default keys have its automap toggles there: F9 Center Automap, F10 Fade Automap, F11 "Show
// Party on Automap", F12 "Show Party Names" (the game's commands 8 - 11; Serg's characters from before 2026-10-18 still have them). Each
// F11 / F12 of ours flipped the game's "AutoMap Party" / "AutoMap Party Names" in its Settings.json, unseen: Serg's file has both at 0
// (his backups flip 0 / 1 between 2026-10-03 and -10-15), and at 0 the game draws no party member on any automap (D2R.exe 3.3.93787
// +0xD7707: a party member's marker skipped; +0xD2805: the far ones from the roster skipped) - the ally missing from the map in the
// hand, the big map and the flat map alike (2026-11-05, a co-op game with the manager PC).
//   0  as before: the game gets F9 - F12 as well.
//   1  the game does not get them (down, repeats, up; WM_SYSKEYDOWN too - F10 is a system key): a game key the player put there
//      deliberately is lost, which the plugin's own key on it made a double action anyway.
inline bool IsOwnHotkey(unsigned vk) { return vk >= 0x78 && vk <= 0x7B; }   // VK_F9 - VK_F12

// The game window's messages for F9 - F12: true = the game does not get this one. One per window, on its thread. A press taken keeps its
// repeats and its release even if the switch goes off meanwhile; a press let through keeps its release too (the game saw it go down).
struct OwnGate {
    uint32_t taken = 0;   // bit vk - VK_F9

    // `on` [view] own_hotkeys, `vk` the message's key, `down` a key-down (WM_KEYDOWN / WM_SYSKEYDOWN), false a key-up, `repeat` the
    // key-down's repeat bit (lParam bit 30).
    bool Take(bool on, unsigned vk, bool down, bool repeat) {
        if (!IsOwnHotkey(vk)) return false;
        const uint32_t bit = 1u << (vk - 0x78);
        if (!down) {
            const bool ours = (taken & bit) != 0;
            taken &= ~bit;
            return ours;
        }
        if (taken & bit) return true;   // a repeat of a press taken
        if (!on || repeat) return false;   // (a repeat of a press the game saw go down: the game's)
        taken |= bit;
        return true;
    }
};

}  // namespace viewkeys
