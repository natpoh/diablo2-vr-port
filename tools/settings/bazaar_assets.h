// bazaar_assets.h - D2R_VR_Settings takes the stray terrain-mask plane out of Kurast Bazaar's scenery ([holes] bazaar_mask_hidden): the
// tile's HD preset out of the player's own game (CascLib, read only) with that one entity cut out (shared/bazaar_mask.h), into the mod's own
// data folder. Same shape as boat_assets.h.
#pragma once

#include <string>

namespace bazaarassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or nothing to do) / unticked / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written or taken out
};

// game: the game's folder with its trailing backslash; modRoot: <game>\mods\D2R-VR\ with its trailing backslash (empty: not the mod -
// nothing made). The preset goes into <modRoot>D2R-VR.mpq\data\..., the receipt beside the mod as <modRoot>bazaar_mask.json.
// on: [holes] bazaar_mask_hidden. Off: ours taken out again (the game's own then); a file of that name the player made or changed stays.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace bazaarassets
