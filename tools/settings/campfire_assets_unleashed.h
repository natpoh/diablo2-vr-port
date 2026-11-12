// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/prepare_campfire_sky.py at 79b921a (Kudlatytrue, bearstonem; fix/campfire-on-by-default) and the
// installer's call of it (steamvr/install.py: the tuned scene, failing that the plain one): character creation's scene made from the
// player's own game - the screen-fixed clouds taken out, Harrogath laid round the fire, the scene as tuned with its owner. Taken into
// D2R VR Authentic for [campfire] enabled: D2R_VR_Settings builds it (at its start, when the tick changes, and Setup's
// --prepare-assets) into the mod's own data folder instead of Unleashed's installer. The rest of D2R VR Authentic is MIT; this file and
// tools/settings/campfire_assets_unleashed.cpp stay MPL 2.0.
#pragma once

#include <string>
#include <vector>

namespace campfireassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written or taken out
};

// The game's files read (CascLib, under "data:").
constexpr const char* kPreset = "data/hd/env/preset/ui/charactercreate.json";
constexpr const char* kTown = "data/hd/env/preset/expansion/town/townwest.json";

// The scene made from the game's own presets, as prepare_campfire_sky.py's generate() writes it (json.dumps(indent=2,
// ensure_ascii=False) + '\n', byte for byte): tuned = TUNED_SCENE's options, else the plain defaults (parse([game])).
// town: townwest.json, nullptr when it could not be read (the scene needs it: then it fails, as Unleashed's read did).
// False with *why when it cannot be built (the preset not as known: no clouds, not a preset, bad JSON...).
bool Build(const std::string& preset, const std::string* town, bool tuned, std::string* out, std::string* why);
// The same with the script's command-line scene options (the game folder left out), e.g. {"--no-surround"} (the tests).
bool BuildWith(const std::string& preset, const std::string* town, const std::vector<std::string>& options, std::string* out, std::string* why);

// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// modRoot: the mod's folder <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not installed as the mod (then
//   nothing is made). The scene goes into <modRoot>D2R-VR.mpq\data\hd\env\preset\ui\charactercreate.json, the receipt (what we
//   wrote, by hash) beside the mod as <modRoot>campfire_scene.json.
// on: [campfire] enabled. Off: our file is taken out again (the game's own scene then); a file of that name the player made stays.
// The tuned scene, failing that the plain one (as Unleashed's installer); neither: whatever is there stays. Nothing is written that is
// already so; a scene the player changed stays.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace campfireassets
