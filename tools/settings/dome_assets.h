// dome_assets.h - D2R_VR_Settings makes [sky] game_sky's dome models from the player's own game: the Arreat Summit's sky card
// (skybox01_lod1.model, CascLib, read only) with our dome's mesh and our sky's texture name in it (shared/granny_remesh.h), one file for
// each LOD the game asks for, into the mod's own data folder. What comes out is Blizzard's file with our mesh in it: it stays on this
// machine, inside the mod - never in the repository or a build.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

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
// closed ([sky] game_sky_closed): the domes a whole sphere, the lower half the picture's bottom row (grannyremesh::DomeMesh); false: as before
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, bool summitPath = false, bool closed = false);
// Every model file the sky's domes are made at (each dome, each LOD; paths in the mod's D2R-VR.mpq): no other maker may write there
std::vector<std::string> SkyModelPaths();
// The game's levels each biome is drawn in, by the game's own files (levels.txt's Id and LevelName, its LevelType's tiles' folder in
// lvltypes.txt, lvlprest.txt's presets and the biome each HD preset names): shared/ceiling_levels.h's table is checked against it
// (dome_assets_test). Empty and `why` when they do not read.
std::map<std::string, std::set<int>> LevelsByBiome(const std::wstring& game, std::map<int, std::string>* names, std::string* why,
                                                   std::set<std::string>* inside = nullptr);
// The biomes of every level levels.txt says is inside (its *IsInside): the game's dungeons and buildings
std::set<std::string> InsideBiomes(const std::wstring& game);
// A biome's terrain layers' maps (data/hd/env/biome/<biome>.json, terrainDataUltra's, their ALB paths in its order); empty when not
// read - gameceiling::FloorMaps picks its floor's stone
std::vector<std::string> FloorsOf(const std::wstring& game, const std::string& biome);

// [ceiling] game_ceiling: the cave ceiling's slab (shared/game_ceiling.h) made the same way from act 1's cave rock model (lit,
// StandardMaterial), the floor's tiling rock as its maps, into <modRoot>D2R-VR.mpq\data\hd\env\model\Tests\test_detailnormal\
// cliffside01_lod<0-4>.model, the wall round its rim ([ceiling] game_ceiling_wall, always made with it: its tick live) as decal01_lod<0-4>
// in the same folder, and their units (ours, no file of the game's in them) as ...\d2r_vr\ceiling\cave_ceiling_wall_unit.json and
// cave_ceiling_unit.json; the receipt as <modRoot>cave_ceiling.json. Off: ours taken out again (a file the player changed stays).
// With them ([ceiling] game_ceiling_beams, height_from_walls - shared/ceiling_measures.h): the barracks' beams (the game's own ceiling frame
// in a grid, at Tests\test_debug_emissive\debug_emissive01_lod<0-4>.model, its unit barracks_beams_unit.json) and the rooms' measures
// (...\d2r_vr\ceiling\measures.txt: each indoor area's walls' top - measured once a game build - and the frame's size). And the
// cathedral's vault ([ceiling] game_ceiling_dome: the slab over its preset with the chapel's dome in it, at
// Tests\test_cloth\cloth01_lod<0-4>.model, its unit cathedral_vault_unit.json; the dome's place and size in the measures). `on`: any of the
// four ticks.
Result PrepareCeiling(const std::wstring& game, const std::wstring& modRoot, bool on);

}  // namespace domeassets
