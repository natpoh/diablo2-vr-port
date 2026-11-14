// orb_assets.h - [hud] wrist_game_models: D2R_VR_Settings makes the wrist medallions' game models from the player's own game (CascLib, read only):
// the liquid lens in the lesser healing / lesser mana potion's own material at two of the game's test model paths, and the four units naming them,
// the game's glass ball and its aluminium ball (shared/wrist_orb_models.h). Into the mod's own data folder (<modRoot>D2R-VR.mpq\data\hd\...); the
// receipt beside the mod as <modRoot>wrist_orbs.json. What comes out holds Blizzard's files: it stays on this machine, inside the mod - never in the
// repository or a build.
#pragma once

#include <string>

namespace orbassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked: ours taken out / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written
};

// game: the game's folder with its trailing backslash; modRoot: <game>\mods\D2R-VR\ with its trailing backslash (empty: not the mod - nothing made).
// on: [hud] wrist_game_models; glow: [hud] wrist_game_glow (the liquid's EmissiveIntensity, baked into its file). Off: ours taken out again (by the
// receipt; a file the player made or changed stays), nothing made.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, float glow);

}  // namespace orbassets
