// sewer_assets.h - D2R_VR_Settings closes the open end of the Kurast sewer stairs ([holes] sewer_backdrop): the four HD presets that place
// the stairwell (Kurast Bazaar, Upper Kurast) out of the player's own game (CascLib, read only), each with two faces of the game's own black
// rectangle stood behind the stairs' open floor end (shared/unleashed_sewer_backdrop.h, after bearstonem/d2r-pcvr, MPL 2.0), into the mod's
// own data folder. What comes out is Blizzard's files changed and stays on this machine, inside the mod: never in the repository or a build.
#pragma once

#include <string>

namespace sewerassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked: ours taken out / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the Status row and the log
    bool wrote = false;   // a file was written or taken out
};

// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// modRoot: <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not installed as the mod (then nothing is made). The presets
//   go into <modRoot>D2R-VR.mpq\data\hd\env\preset\act3\kurast\, the receipt (what we wrote, by hash) beside the mod as <modRoot>sewer_backdrop.json.
// on: [holes] sewer_backdrop. Off: ours taken out again (by the receipt; a file of that name the player made or changed stays).
// Nothing is written that is already so; a preset the player changed stays.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace sewerassets
