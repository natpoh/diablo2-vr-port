#pragma once
// flat_map_keys.h - [hud] flat_map_turn_key / flat_map_place_key / flat_map_corner: the flat game's map turned and put in the middle by keys of
// the player's own (2026-11-10, a player's ask relayed by Serg: "the map in 3D is not playable: to put it in the middle and look at the
// details you go into the menu - ideally a hotkey puts it there, a hotkey makes it turn, hotkeys zoom it in and out"). The zoom's keys are
// vr/flat_map_zoom.h's. Pure: no game, no Windows (cleanroom/tests/flat_map_keys_test.cpp).
//
// The turn key flips [hud] map_rotate_flat (vr/map_turn.h RoundOnScreen) and saves it; the place key puts the map in the middle ([hud]
// flat_map_place 2: the game's whole-screen map, which has no box and so does not turn) and, pressed again, back in the corner chosen in the
// options ([hud] flat_map_corner: 0 left, 1 right), and saves it. Both set in the game's own D2R VR settings panel (press a key) or in
// D2R VR Settings. Only in the flat game, in a game, with no chat line and no panel of the game's up; then the game does not see them.

#include <algorithm>

namespace flatmapkeys {

constexpr unsigned kDefaultTurnKey = 0x6F;    // Num /
constexpr unsigned kDefaultPlaceKey = 0x6A;   // Num *

// A key read from the ini: its code, 0 none, anything else the default.
inline unsigned KeyOf(int v, unsigned fallback) { return v >= 1 && v <= 0xFE ? (unsigned)v : v == 0 ? 0u : fallback; }

// [hud] flat_map_corner (0 left, else right) as [hud] flat_map_place's corner (1 left, 3 right).
inline int CornerPlace(int corner) { return corner == 0 ? 1 : 3; }
// The place after a press of the place key: in the middle (2) -> the corner; anywhere else (the game's own, a corner) -> the middle.
inline int NextPlace(int place, int corner) { return place == 2 ? CornerPlace(corner) : 2; }

// What a key press is: none of ours, the turn key, the place key. A key that is both (set so by hand) is the turn key's.
enum class Act { None, Turn, Place };
inline Act Of(unsigned vk, unsigned turnKey, unsigned placeKey) {
    if (vk == 0) return Act::None;
    if (vk == turnKey) return Act::Turn;
    if (vk == placeKey) return Act::Place;
    return Act::None;
}

// Whether a press is ours (and hidden from the game): the flat game (the monitor), in a game, no chat line, no panel or menu of the game's
// up. The map need not be open: the turn and the place hold for when it is.
inline bool Takes(bool flat, bool inGame, bool chat, bool menu) { return flat && inGame && !chat && !menu; }

}  // namespace flatmapkeys
