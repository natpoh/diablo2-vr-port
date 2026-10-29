// boat_assets.h - D2R_VR_Settings puts the Kurast Docks' flying canoe down on the water ([world] kurast_boat): the town's HD preset
// out of the player's own game (CascLib, read only) with the one height changed (shared/kurast_boat.h), into the mod's own data folder.
// After bearstonem/d2r-pcvr (MIT, bed674b), which takes the boat out.
#pragma once

#include <string>

namespace boatassets {

enum State { kMade, kOff, kFailed, kNotHere };   // on the water (or nothing to do) / unticked / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the Status row and the log
    bool wrote = false;   // a file was written or taken out
};

// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// modRoot: the mod's folder <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not installed as the mod (then
//   nothing is made: no folder whose files the game takes for its own). The preset goes into <modRoot>D2R-VR.mpq\data\..., the
//   receipt (what we wrote, by hash) beside it as <modRoot>kurast_boat.json.
// on: [world] kurast_boat. Off: our preset is taken out again (the game's own then); a file of that name the player made stays.
// Nothing is written that is already so; a preset the player changed stays; our copy goes when the game no longer needs it.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace boatassets
