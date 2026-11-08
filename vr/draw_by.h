#pragma once
// draw_by.h - the headset Video page's "Sky drawn by" and "Fog drawn by" (Serg 2026-11-08: "селекторы отрисовки тумана и неба: игрой, или
// движком, или не рисовать"): one three-way choice over the two keys D2R VR Settings has for each - [sky] enabled + [sky] game_sky,
// [fog] enabled + [fog] game_fog ("Sky drawn by" / "Fog drawn by": our effect or the game's; "Distance fog" / the sky's tick: at all).
// Pure; tested in tools/hset_test.cpp.

namespace drawby {

enum Way : int { kNone = 0, kOurs = 1, kGame = 2, kWays = 3 };

// What the two keys say: not on at all - none; on - the game's or our effect's. The fog (gameAlone): the game's fog draws with [fog]
// game_fog alone - [fog] enabled is our effect's fog only (Serg 2026-11-08: enabled=0 must never take the game's fog away) - so game_fog=1
// is the game's whatever enabled says, and none is both off.
inline int FromKeys(bool enabled, bool game, bool gameAlone = false) {
    if (gameAlone) return game ? kGame : enabled ? kOurs : kNone;
    return !enabled ? kNone : game ? kGame : kOurs;
}

// The keys a way writes. The sky: none leaves the drawn-by key as it was (the player's choice comes back with the sky). The fog
// (gameAlone): none both off, ours [fog] enabled alone, the game's game_fog alone (our effect's fog off - the game's draws it).
inline void ToKeys(int way, int gameWas, int* enabled, int* game, bool gameAlone = false) {
    if (gameAlone) {
        *enabled = way == kOurs ? 1 : 0;
        *game = way == kGame ? 1 : 0;
        return;
    }
    *enabled = way == kNone ? 0 : 1;
    *game = way == kGame ? 1 : way == kOurs ? 0 : gameWas;
}

// A press of < (-1) or > (+1): none, our effect, the game's - stopping at the ends.
inline int Step(int way, int delta) {
    const int to = way + (delta > 0 ? 1 : -1);
    return to < kNone ? kNone : to > kGame ? kGame : to;
}

}  // namespace drawby
