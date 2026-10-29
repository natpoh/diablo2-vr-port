// dome_assets.h - D2R_VR_Settings makes [sky] game_sky's dome models from the player's own game: the Arreat Summit's sky card
// (skybox01_lod1.model, CascLib, read only) with our dome's mesh and our sky's texture name in it (shared/granny_remesh.h), one file for
// each LOD the game asks for, into the mod's own data folder. What comes out is Blizzard's file with our mesh in it: it stays on this
// machine, inside the mod - never in the repository or a build.
#pragma once

#include <string>

namespace domeassets {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked: ours taken out / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written
};

// game: the game's folder, with its trailing backslash. modRoot: <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not
// installed as the mod (then nothing is made). The models go into <modRoot>D2R-VR.mpq\data\hd\env\model\d2r_vr\sky\sky_dome_<act1|test>_lod<0-4>.model,
// the receipt beside the mod as <modRoot>sky_dome.json. A file of that name the player made or changed stays; ours and the first
// prototype's (written by tools/game_sky/make_sky_dome.py, one section) are replaced.
// on: [sky] game_sky. Off: our models are taken out again (by the receipt; a file the player made stays) and nothing is made.
// summitPath ([debug] game_sky_test 4): our magenta model also put at the game's own Summit card path in the mod (overriding the game's
// file only in our mod) - does the game take a model only from a path it knows? Off: ours taken out of that path again.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, bool summitPath = false);

// [ceiling] game_ceiling: the cave ceiling's slab (shared/game_ceiling.h) made the same way from act 1's cave rock model (lit,
// StandardMaterial), the floor's tiling rock as its maps, into <modRoot>D2R-VR.mpq\data\hd\env\model\Tests\test_detailnormal\
// cliffside01_lod<0-4>.model, the wall round its rim ([ceiling] game_ceiling_wall, always made with it: its tick live) as decal01_lod<0-4>
// in the same folder, and their units (ours, no file of the game's in them) as ...\d2r_vr\ceiling\cave_ceiling_wall_unit.json and
// cave_ceiling_unit.json; the receipt as <modRoot>cave_ceiling.json. Off: ours taken out again (a file the player changed stays).
Result PrepareCeiling(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace domeassets
