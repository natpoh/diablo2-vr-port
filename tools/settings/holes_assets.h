// holes_assets.h - [holes] patch_world: D2R_VR_Settings makes the patches over the world's holes (shared/world_holes_list.h: the places) from the
// player's own game (CascLib, read only): each place's preset laid out, its hole found, the patch made from the game's own edges, written as the
// rim floor's own model file (its material kept) with the patch's triangles in it - five LODs at the place's free model path - and a unit, plus
// the measures file the plugin reads (where each patch rides: its anchor and witness pieces). Into the mod's own data folder
// (<modRoot>D2R-VR.mpq\data\hd\env\model\...); the receipt beside the mod as <modRoot>world_holes.json. What comes out holds Blizzard's files:
// it stays on this machine, inside the mod - never in the repository or a build.
#pragma once

#include <string>

namespace holesassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked: ours taken out / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written
};

// game: the game's folder with its trailing backslash; modRoot: <game>\mods\D2R-VR\ with its trailing backslash (empty: not the mod - nothing
// made). on: [holes] patch_world. Off: ours taken out again (by the receipt; a file the player made or changed stays), nothing made.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace holesassets
