// menu_ready.h - the game's menus not touched before they exist: no window move, no mouse message, until the game's UI system is up (vr/hud/window.cpp
// LaserFitTick, GameWndProc; the latch from vr/gamestate.cpp OnUiMessage). Pure, tested in cleanroom/tests/menu_ready_test.cpp.
//
// Group J of the 2026-11-06 crash batch (rows 270, 271 - RTX 3070 Ti, SteamVR, 0.205.0): the start stopped at D2RLoader's step 15/24 "Initializing graphics
// context" (12 s long in 271, 2.7 s as a rule), and both deaths are at one instruction, D2RLoader+0x12157B2 - the game's allocator (tlsf_malloc) with a null pool:
// a mouse event's UI message (270: InputMessage LeftMouseButtonDown, 1.3 s into step 15; 271: MouseLeave) reached the game's menus before step 17
// "Initializing UI system" makes their memory pool. A bug of the game's, but we brought it out: in 271 our laser_fit_window moved the game's window during
// step 15 (12:24:11.858) - the MouseLeave. So: until the UI system is up, the window is not moved and the game's window gets no mouse message.
//
// Up: the game's panel manager spoke (its first PanelManager UI message - the logo panel opens as the start ends), or the hero is in a world, or the title
// screen's world settled. Never stuck: kFallbackMs after the game's window was first seen it counts as up whatever was heard (a D2RLoader without UI messages
// must not leave the mouse dead).
#pragma once

#include <cstdint>

namespace menuready {

constexpr uint64_t kFallbackMs = 30000;

// windowSeenMs: when the game's window was first seen (GetTickCount64; 0 not yet).
inline bool Up(bool panelManagerSpoke, bool inWorld, bool frontendSettled, uint64_t windowSeenMs, uint64_t nowMs) {
    if (panelManagerSpoke || inWorld || frontendSettled) return true;
    return windowSeenMs != 0 && nowMs - windowSeenMs >= kFallbackMs;
}

// The window messages a menu could make a UI message of: the mouse's (moves, buttons, wheels, leaving, hovering, non-client ones over the frame are Windows'
// own and pass). The values are winuser.h's, so this header needs no windows.h.
inline bool IsMouseMessage(unsigned msg) {
    if (msg >= 0x0200 && msg <= 0x020E) return true;   // WM_MOUSEMOVE .. WM_MOUSEHWHEEL
    return msg == 0x02A3 || msg == 0x02A1;              // WM_MOUSELEAVE, WM_MOUSEHOVER
}

// Dropped before the game sees it: a mouse message while the menus are not up.
inline bool DropBeforeMenu(unsigned msg, bool up) { return !up && IsMouseMessage(msg); }

// The laser keeps the window on its monitor (laser_fit_window) only once the menus are up.
inline bool MayMoveWindow(bool up) { return up; }

}  // namespace menuready
