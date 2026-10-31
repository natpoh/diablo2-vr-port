// solids_assets.h - D2R_VR_Settings makes the level pieces' solid parts for [weight] world_contact: d2r_vr_props\solids.bin beside the plugin (shared/world_solids.h),
// from the HD level presets of the player's own game (data/hd/env/preset/**.json), the prefabs they place (data/hd/env/model/**.json) and each placed model's own
// mesh (its _lod0 .model file), through CascLib, read only. Made once per game build (solids.txt says which); nothing of it is shipped.
#pragma once

#include <string>

namespace solidsassets {

enum State { kNotMade = 0, kMade = 1, kNotHere = 2 };

struct Result {
    State state = kNotMade;
    bool wrote = false;
    int pieces = 0, models = 0, withParts = 0, foliage = 0, movable = 0, unread = 0, presets = 0, prefabs = 0;
    double seconds = 0;
    std::wstring text;   // for the status line and the log (English: the log's)
};

// game: the game's folder; pluginDir: the plugin's folder, with or without a trailing backslash. force: make it even when this build's is there.
Result Prepare(const std::wstring& game, const std::wstring& pluginDir, bool force = false);

}  // namespace solidsassets
