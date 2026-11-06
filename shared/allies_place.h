#pragma once
// allies_place.h - [hud] allies_place: WHERE the allies' portraits (the mercenary, the summons, the party) are shown in VR first person (F4) - one place
// of three (Serg, 2026-11-06, test 0.205.6: "a selector, not ticks ... one of the 3"): the game's own row at the top of the view (as the game draws it),
// our grid under the map in the hand (vr/ally_portraits.h), or our tiles on the right wrist. Each choice shows the allies in that one place only. Pure;
// read by the plugin (vr/config/load_settings.cpp) and by D2R VR Settings (its Choice, and the one-time move of the three old ticks).

namespace alliesplace {

enum Place : int { kGame = 0, kMap = 1, kWrist = 2, kCount = 3 };
constexpr int kDefault = kGame;   // as before: the game's own row

constexpr int Sanitized(int place) { return place >= 0 && place < kCount ? place : kDefault; }

// The three ticks of 0.205.5 - 0.205.6 ([hud] ally_portraits, [hud] allies_game_hide, [hud] wrist_status_companions with [hud] wrist_status), read once
// when allies_place is not in the file yet (-1: that key is not there either): our grid ticked -> the grid; the game's row hidden while the wrist's
// counters were up -> the wrist; else the game's row. (The old "hidden everywhere" is no choice any more: the game's row.)
constexpr int FromOldTicks(int allyPortraits, int gameHide, int wristStatus, int wristCompanions) {
    if (allyPortraits == 1) return kMap;
    if (gameHide == 1 && wristStatus == 1 && wristCompanions != 0) return kWrist;
    return kGame;
}

// The columns of tiles across: [hud] allies_map_cols (the grid under the map, 4 by default) and [hud] allies_wrist_cols (on the wrist, 3). The rows
// grow as the allies need (Serg: "down it is as many as there are allies").
constexpr int kColsMin = 1, kColsMax = 8, kMapColsDefault = 4, kWristColsDefault = 3;
constexpr int SanitizedCols(int cols, int fallback) { return cols >= kColsMin && cols <= kColsMax ? cols : fallback; }

}  // namespace alliesplace
