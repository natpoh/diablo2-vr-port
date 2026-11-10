#pragma once
// flat_map_zoom.h - [hud] flat_map_zoom: the flat game's map zoomed in and out with + and - (Serg, 2026-11-04: "для 2д режима надо сделать
// возможность +- увеличивать уменьшать карту"). Pure: no game, no Windows (cleanroom/tests/flat_map_zoom_test.cpp).
//
// The game has a zoom of its own for its map, only no key for it: the AutoMap panel's scale call (vr/map_zoom.h, game_sigs.h AutomapScale)
// takes a zoom per call - 0.5 its own - and draws whole 1/8 steps of it. The VR big map steers that call already (gamestate.cpp bigzoom);
// on the monitor the same call is given less than the game's own, so the game itself draws its map farther - its tiles, its markers -
// nothing is shrunk as a picture and nothing of the game's memory is written.
//
// Serg's steps (2026-11-04): "+ - in the circle as now; - - no longer held to the circle, shown as it is, and smaller with each press so the
// whole level can be seen, as on the VR big map". So the level (the ini's [hud] flat_map_zoom_level) runs from 0 down to kMinSteps:
//   0      the map as before - with [hud] map_rotate_flat the round map, turned (map_turn.h RoundOnScreen); without it the game's own;
//   -1     out of the circle: the map's whole box, turned all the same, at the game's own zoom;
//   -2..   smaller, an eighth of zoom each press, down to the game's floor 0.125 (a quarter of its own 0.5: the whole level in the box);
//   +1..+4 nearer (2026-11-10, a player's ask relayed by Serg: "zoom by keys, not only out but in - to see where to go"): the game draws its
//          map an eighth of zoom bigger a press, up to 1.0 (twice its own), in the same box - round and turned all the same.
// + walks back up the same steps and, past -1, into the circle, then nearer. The keys: + and - on the main row and the numpad, and the wheel with Ctrl
// held - only in a game, the game's map open, no chat line and no panel up; then the game does not see them.

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>

#include "map_zoom.h"   // mapzoom::GameRounded / kGameMin / kGameMax: the game's own rounding and our range

namespace flatzoom {

constexpr int kMinSteps = -4, kMaxSteps = 4;   // from the game's own 0.5: -1 0.5, -2 0.375, -3 0.25, -4 0.125 (its floor); +1 0.625 .. +4 1.0
constexpr float kStep = 0.125f;                // the game draws whole eighths

// The level after a press (dir +1 nearer, -1 farther), kept in the range.
inline int Stepped(int steps, int dir) { return std::clamp(steps + (dir > 0 ? 1 : dir < 0 ? -1 : 0), kMinSteps, kMaxSteps); }
// A level read from the ini, kept in the range.
inline int Clamped(int steps) { return std::clamp(steps, kMinSteps, kMaxSteps); }
// Held to the round frame (when the map turns): level 0 only.
inline bool InCircle(int steps) { return Clamped(steps) >= 0; }

// The zoom the game's scale call is given for the level: its own (rounded as it rounds it) at 0 and -1, an eighth less for each step past
// -1, never under the game's floor; an eighth more for each step over 0, never over kGameMax. A zoom of the game's that makes no sense
// (not 0.05 - 4): untouched.
inline float Arg(float game, int steps) {
    const int s = Clamped(steps);
    if ((s >= -1 && s <= 0) || !std::isfinite(game) || !(game >= 0.05f && game <= 4.0f)) return game;
    const float z = mapzoom::GameRounded(game) + (float)(s > 0 ? s : s + 1) * kStep;
    return std::clamp(z, mapzoom::kGameMin, mapzoom::kGameMax);
}
// Whether the game's scale call is to be steered at all for a level (0 and -1 draw at its own zoom).
inline bool Steers(int steps) { const int s = Clamped(steps); return s < -1 || s > 0; }
// How many times the game's own size the map is drawn at for a level (for the log: 0.5 = half as big).
inline float Times(float game, int steps) {
    const float own = mapzoom::GameRounded(game);
    return own > 0.0f ? Arg(game, steps) / own : 1.0f;
}

// The level between the keys (the window's thread: Press), the ini (vrcam's tick: Ini, with each settings load's number) and our own write of
// it (the save thread: Writing, just before its commit). An ini number is taken only when it is new AND not our own write coming back: Settings
// (or a hand edit) wins, while the ini still holding the old number before our write lands - or our write landing after a newer press - changes
// nothing. (0.202.2, Serg: "after zooming out it snaps back a few frames later" - the tick took the save as an excuse to forget the old number,
// saw it again on the next tick, put the level back to it, and the save then wrote that.)
class Keeper {
    std::atomic<int> live_{0};
    std::atomic<int> writing_{INT_MIN};   // the number our save is putting in the ini (INT_MIN none)
    int iniSeen_ = INT_MIN;               // the ini's number last seen (the tick's only)
public:
    int Live() const { return live_.load(); }
    // A press: the level stepped, and the new level (the same one when at an end).
    int Press(int dir) {
        int was = live_.load();
        while (!live_.compare_exchange_weak(was, Stepped(was, dir))) {}
        return Stepped(was, dir);
    }
    // Every tick: the ini's number as the settings last loaded it. True when the level was taken from it.
    bool Ini(int ini) {
        if (ini == iniSeen_) return false;
        iniSeen_ = ini;
        if (ini == writing_.load()) return false;   // our own write: the level is already that or newer
        live_.store(Clamped(ini));
        return true;
    }
    // The save thread, before it commits v.
    void Writing(int v) { writing_.store(v); }
};

// Windows' virtual keys (winuser.h), so the key's direction is tested here.
constexpr unsigned kVkAdd = 0x6B, kVkSubtract = 0x6D, kVkOemPlus = 0xBB, kVkOemMinus = 0xBD;
// [hud] flat_map_zoom_in_key / flat_map_zoom_out_key (2026-11-10: the map's keys set in the game's own D2R VR settings panel): the keys, as
// virtual-key codes; 0 none. The defaults are the main row's + and -; with a default the numpad's + / - work too, as before.
constexpr unsigned kDefaultInKey = kVkOemPlus, kDefaultOutKey = kVkOemMinus;
inline unsigned KeyOf(int v, unsigned fallback) { return v >= 1 && v <= 0xFE ? (unsigned)v : v == 0 ? 0u : fallback; }
// A key's direction: the zoom-in key nearer, the zoom-out key farther (and the numpad's + / - while the key is the default), 0 not ours.
inline int KeyDir(unsigned vk, unsigned inKey = kDefaultInKey, unsigned outKey = kDefaultOutKey) {
    if (vk == 0) return 0;
    if (vk == inKey || (inKey == kDefaultInKey && vk == kVkAdd)) return 1;
    if (vk == outKey || (outKey == kDefaultOutKey && vk == kVkSubtract)) return -1;
    return 0;
}
// The wheel's direction with Ctrl held (delta: WM_MOUSEWHEEL's, + away from the player): away nearer; without Ctrl it stays the game's.
inline int WheelDir(int delta, bool ctrl) { return !ctrl || delta == 0 ? 0 : delta > 0 ? 1 : -1; }

// Whether a press is the zoom's (and hidden from the game): the key on, the flat game (the monitor), in a game, its map open, no chat line,
// no panel or menu of the game's up.
inline bool Takes(bool key, bool flat, bool inGame, bool mapOpen, bool chat, bool menu) { return key && flat && inGame && mapOpen && !chat && !menu; }

}  // namespace flatzoom
