// props_assets.h - D2R_VR_Settings makes the catalogue of the level's knockable props ([weight] knock_props): d2r_vr_props\props.bin beside the plugin, from
// the HD level presets of the player's own game (data/hd/env/preset/**.json) and the prefabs they place (data/hd/env/model/**.json), through CascLib, read only.
// Made once per game build, and again when the catalogue's make changes (made.txt says both); nothing of it is shipped.
#pragma once

#include <string>

namespace propsassets {

enum State { kNotMade = 0, kMade = 1, kNotHere = 2 };

struct Result {
    State state = kNotMade;
    bool wrote = false;     // the file was (re)made this time
    int props = 0, kinds = 0, presets = 0, prefabs = 0, prefabProps = 0;   // prefabs: those with props in them, and how many
    std::wstring text;      // for the status line and the log
};

// game: the game's folder; pluginDir: the plugin's folder (the ini's), with or without a trailing backslash. force: make it even when the build's is there.
Result Prepare(const std::wstring& game, const std::wstring& pluginDir, bool force = false);

// [weight] grab_items: d2r_vr_props\item_models.txt beside the plugin - each item code's own 3D look, the game's UnitDefinition (data/hd/items/<weapon|armor|misc>/<asset>.json,
// the asset named by data/hd/items/items.json): the hand that took an item off the ground shows our copy of it. One line a code, "code<TAB>path"; the first line "# build N".
// Made once per game build; nothing of it is shipped. Result::props = the codes written.
Result PrepareItemModels(const std::wstring& game, const std::wstring& pluginDir, bool force = false);

}  // namespace propsassets
