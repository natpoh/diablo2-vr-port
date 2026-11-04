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

}  // namespace viewkeys
