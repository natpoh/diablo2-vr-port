#pragma once
// game_ceiling.h - [ceiling] game_ceiling: the cave ceiling drawn by the game itself (docs/game_ceiling.md). A slab of the game's own lit
// cave rock - act 1's rocks03 model (StandardMaterial) with our slab's mesh in it and the floor's own tiling rock as its maps
// (shared/granny_remesh.h CeilingMesh; D2R_VR_Settings makes it from the player's game, tools/settings/dome_assets.cpp) - loaded by the
// game's own unit loader and laid over the hero every frame by vrcam (engitem::CeilingFrame), as [sky] game_sky's dome is. The game
// draws it in its own frame: both eyes, its torches' and the hero's light on it, its fog over it, fire and spells in their own order.
// The pure parts, for vrcam and Settings both; tested in cleanroom/tests/game_ceiling_test.cpp.
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "ceiling_measures.h"   // [ceiling] game_ceiling_dome: RiseOver

namespace gameceiling {

// The slab as made (CeilingMesh): kTilesHalf tiles of kTile units each way from its middle, kCells cells a tile, its rock hanging down
// kRelief from its top. One tile = one picture of the floor's rock.
constexpr float kTile = 40.0f;
constexpr int kTilesHalf = 6;
constexpr int kCells = 6;
constexpr float kRelief = 6.0f;
// Its mesh's index count, every LOD: two triangles a cell, both windings ([sky] game_sky's 10 s line counts the draws of that many)
constexpr unsigned kIndexCount = 12u * (2u * kTilesHalf * kCells) * (2u * kTilesHalf * kCells);

// Where the game takes it from. The game makes a model only from a path it knows (its own files' names - our dome at our own path never
// got a ModelComponent): one of its developers' test models no unit or preset names and nothing in play shows, not the sky dome's
// (Tests/Barbarian_ScaleRef, Tests/test_cloth/driver_mesh_test01). Of all the game's JSON (the installed game, 2026-10-27) only its LOD table
// (data/hd/model_lod_desc.json) names this one - test_resolution_plane_nonterrain is in act 2 town's prop palette. The unit (ours) and the
// textures (the game's) load from their own paths.
constexpr const char* kModelDir = "data/hd/env/model/Tests/test_detailnormal/";
constexpr const char* kModelFile = "cliffside01";   // + "_lod<0-4>.model"; the unit names it without the LOD
constexpr const char* kModel = "data/hd/env/model/Tests/test_detailnormal/cliffside01.model";
constexpr const char* kUnit = "data/hd/env/model/d2r_vr/ceiling/cave_ceiling_unit.json";
// The game's model it is made from (its file read for every LOD), and its maps renamed to the floor's tiling rock (act 1's caves' terrain)
constexpr const char* kSource = "data/hd/env/model/act1/caves/act1_caves_rocks/rocks03_lod1.model";
struct Map { const char* from; const char* to; };
constexpr Map kMaps[] = {
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_alb.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ALB.texture"},
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_nrm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_NRM.texture"},
    {"data/hd/env/texture/act1/caves/act1_caves_rocks_rocks03_orm.texture", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01_ORM.texture"},
};

// [ceiling] game_ceiling_wall: the slab's rim bent down - a ring of the same rock (WallMesh), its own model and unit, laid with the slab:
// the black void past the slab's edge (a band between its rim and the far floor, lit by the fog behind fires) closed by a wall, as the post
// ceiling's wall_distance closed it. Its middle at the hero (not on whole tiles: it never pokes out from under the slab, which snaps), its
// top at the slab's top, its reach down to [ceiling] floor_depth under the area's floor. The same source model and maps as the slab; its
// path the other test model only the LOD table names in that folder.
constexpr float kWallRadius = kTile * kTilesHalf - kTile * 0.5f - kRelief;   // under the slab wherever its middle is (it snaps up to half a tile)
constexpr float kWallHeight = kTile;      // the model's own height (one picture), scaled to the reach
constexpr int kWallSegments = 96;         // round it
constexpr int kWallRows = 4;              // down it
constexpr unsigned kWallIndexCount = 12u * kWallSegments * kWallRows;
constexpr const char* kWallModelFile = "decal01";   // in kModelDir
constexpr const char* kWallModel = "data/hd/env/model/Tests/test_detailnormal/decal01.model";
constexpr const char* kWallUnit = "data/hd/env/model/d2r_vr/ceiling/cave_ceiling_wall_unit.json";

// ---- every room its own: the kinds of ceiling (Serg 2026-10-28: "a fitting model in each room") ----
// The same slab, made the same way from the same lit rock model (kSource), each kind with a tiling terrain set of the game's own (ALB / NRM /
// ORM) matched by eye to the picture already chosen for those rooms for our vault ([ceiling] texture_<biome>, the D2R_Ceiling_*.png we ship):
// the caves' rock, the crypt's tall stone slabs, the cathedral's smooth light stone, the barracks' and catacombs' smooth dark stone, the
// palace's ornamented squares. Caves keep their rock relief and the wall at the rim; built rooms are flat and have walls of their own.
// The rollout rule (Serg): a room gets the game's ceiling only once its kind is made here; every other room keeps our vault with its own
// settings, game_ceiling or not - never nothing.
// A room with no picture of its own matched (Serg 2026-10-29, 0.189, the act 2 sewers had our old vault: "надо взять просто с пола
// текстуру"): its own floor's terrain set - the first terrain layer of its biome (data/hd/env/biome/<biome>.json; dome_assets_test checks
// it against the game's files) - laid flat: the sewers, the corrupt harem, the palace cellar.
// Each kind is its own model at its own free path (its maps are baked in): the game's developers' test models no unit or preset of play
// names (2026-10-27 scan: only the LOD table). Kind 0 is the first one, at its first paths and units.
struct Kind {
    const char* name;      // for the log and its units' names
    const char* maps;      // the terrain set's path without "_ALB.texture" / "_NRM.texture" / "_ORM.texture"
    float relief;          // its rock's relief (CeilingMesh), model units; 0 flat
    const char* slab;      // its model, under data/hd/env/model/Tests/, without "_lod<n>.model"
    const char* wall;      // the wall at its rim's model, or nullptr (rooms: their own walls)
};
constexpr Kind kKinds[] = {
    // reference D2R_Ceiling_act1_caves_walls (Serg saw this one in the headset: "всё отлично")
    {"cave", "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01", kRelief, "test_detailnormal/cliffside01", "test_detailnormal/decal01"},
    // the other caves (caves are the game's by default): their own act's rock
    // (2026-10-29: off Tests/pillar01, Pillar_02, Pillar_03 - the sky's act 2 - 4 domes took the same three the same day, d6068c32)
    {"jungle_cave", "data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01", kRelief, "test_cloth/cloth02", "test_cloth/cloth04"},
    {"ice_cave", "data/hd/env/texture/terrain/expansion/icecave/Act5_Rocky_Floor", kRelief, "test_cloth/rope_test01/rope_test01", "testy"},
    // reference D2R_Ceiling_act1_crypt (tall stone slabs): act 4's tall stone tiles in columns
    {"crypt", "data/hd/env/texture/terrain/act4/mesa/act4_mesa_stonetile01", 0.0f, "vertblend_house", nullptr},
    // reference D2R_Ceiling_act1_barracks_smooth (smooth dark stone; the barracks' and the catacombs'): the Arcane Sanctuary's dark grunge
    {"smooth_dark", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge01", 0.0f, "test_terrainblending/cliff01", nullptr},
    // reference D2R_Ceiling_act1_crypt_smooth (smooth light stone; the cathedral's): its lighter grunge
    {"smooth_light", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge02", 0.0f, "test_terrainblending/ramp01", nullptr},
    // reference D2R_Ceiling_act2_palace (beige ornamented squares): the palace cells' ornamented marble
    {"palace", "data/hd/env/texture/terrain/act2/palace/act2_palace_cell_marble_tile01", 0.0f, "TextureExportTesting/TestCube", nullptr},
    // their own floor's (2026-10-29): at the test models only the developers' test preset test/chad.json names (no level loads it - lvlprest)
    {"sewer", "data/hd/env/texture/terrain/act2/sewer/act2_sewer_cobblestone01", 0.0f, "test_cube_tex", nullptr},
    {"harem", "data/hd/env/texture/terrain/act2/palace/act2_palace_marble01", 0.0f, "test_cube_foliage", nullptr},
    // every dungeon (Serg 2026-10-29: "мы должны добавить потолки на всех подземельях"; "где нет, там поставить скалу"): its floor's stone
    // by FloorMaps, the cave rock laid flat where it has none. The game's test models are all ours now: three models of the game's own
    // nothing names but its LOD table (a test asset, an _OLD one, an _ one - "@": under data/hd/env/model/), and four test models only the
    // developers' test presets (test/chad, graphics, spotLightTest) and the editor's warehouses name - no level loads them (lvlprest)
    {"barracks_court", "data/hd/env/texture/terrain/act1/barracks/act1_barracks_cobblestone03", 0.0f, "@expansion/siege/expansion_siege_craters/test_asset", nullptr},
    {"act3_sewer", "data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01", 0.0f, "@act1/catacomb/act1_catacomb_props/_pit02", nullptr},
    {"temple", "data/hd/env/texture/terrain/act3/temple/act3_temple_stone01", 0.0f, "test_cube", nullptr},
    {"durance", "data/hd/env/texture/terrain/act3/travincal/act3_travincal_stone01", 0.0f, "test_cloth/hangingRug", nullptr},
    {"wildtemple", "data/hd/env/texture/terrain/expansion/wildtemple/expansion_wildtemple_caverock", 0.0f, "test_decal/steppe01", nullptr},
    {"baallair", "data/hd/env/texture/terrain/expansion/baallair/expansion_baallair_floor01", 0.0f, "test_light/test_material_spheres01", nullptr},
};
// The kinds whose maps are their room's own floor (the biome they were taken from): dome_assets_test reads its first terrain layer
struct FloorKind { const char* biome; const char* kind; };
constexpr FloorKind kFloorKinds[] = {{"act2_sewer", "sewer"}, {"act2_palace", "harem"},
                                     {"act1_court_to_barracks", "barracks_court"}, {"act3_sewer", "act3_sewer"},
                                     {"act3_temple", "temple"}, {"act3_travincal", "durance"}, {"expansion_wildtemple", "wildtemple"},
                                     {"expansion_baallair", "baallair"}, {"expansion_baallair_throneroom", "baallair"}};
// The floor's stone among a biome's terrain layers (their ALB paths, in its order): the first whose name says a solid floor - stone,
// cobblestone, marble, tile, floor, brick, rock - and nothing loose or wet (dirt, mud, sand, water, puddle, sewage, blood, moss, pebbles,
// grass, damaged, bridge); its maps without "_ALB.texture". None: "" - the cave rock then (kRockMaps), laid flat.
constexpr const char* kRockMaps = "data/hd/env/texture/terrain/act1/caves/act1_caves_rock01";
inline std::string FloorMaps(const std::vector<std::string>& albedos) {
    for (const std::string& a : albedos) {
        std::string n = a.substr(a.find_last_of('/') + 1);
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        bool solid = false, loose = false;
        for (const char* w : {"stone", "cobble", "marble", "tile", "floor", "brick", "rock"}) solid = solid || n.find(w) != std::string::npos;
        for (const char* w : {"dirt", "mud", "sand", "water", "puddle", "sewage", "blood", "moss", "pebble", "grass", "damaged", "bridge"})
            loose = loose || n.find(w) != std::string::npos;
        const std::string tail = "_alb.texture";
        if (solid && !loose && n.size() > tail.size() && n.compare(n.size() - tail.size(), tail.size(), tail) == 0)
            return a.substr(0, a.size() - tail.size());
    }
    return std::string();
}
// Each piece's mesh its own bytes (grannyremesh::Salt: the game shares a model it has by what it holds - every flat kind's slab is the same
// mesh): kind k's slab 1 + 2k, its wall 2 + 2k
inline unsigned SaltOf(int kind, bool wall) { return 1u + 2u * (unsigned)kind + (wall ? 1u : 0u); }
constexpr int kKindCount = (int)(sizeof kKinds / sizeof kKinds[0]);
// Which kind each area gets (the game's biome names); an area not here keeps our vault (or none, as before)
struct BiomeKind { const char* biome; int kind; };
constexpr BiomeKind kBiomeKinds[] = {
    {"act1_caves", 0}, {"act2_maggot", 1}, {"act3_spider", 1}, {"act3_jungle_dungeon", 1}, {"expansion_icecave", 2},
    {"act1_crypt", 3}, {"act1_barracks", 4}, {"act1_catacombs", 4}, {"act1_cathedral", 5}, {"act2_palace_clean", 6},
    {"act2_sewer", 7}, {"act2_palace", 8},
    // smooth stone, not the floor's tiles or the cave rock (Serg on 0.190: "52-54 надо сделать гладкую текстуру, а не плитку", "точнее как
    // 34", "в 55 тоже"): the catacombs' smooth dark stone in the palace cellar (52 - 54) and every tomb (55 - 61, 66 - 73)
    {"act2_palace_cells", 4}, {"act2_tomb", 4},
    {"act1_court_to_barracks", 9}, {"act3_sewer", 10}, {"act3_temple", 11}, {"act3_travincal", 12},
    {"expansion_wildtemple", 13}, {"expansion_baallair", 14}, {"expansion_baallair_throneroom", 14},
};
// Areas added to the shipped lists later, a batch at a time. The installer only adds missing keys (0.190 on PC1: an upgrading player kept
// the old [ceiling] biomes / game_ceiling_biomes and no new dungeon got a ceiling), so a list is brought up to date where it is read: a
// list with none of a batch's areas is from before that batch - the batch is added; one with any of them is taken as it is (what the
// player took out stays out). An empty list (none anywhere) stays empty.
struct AreaBatch { const char* vault; const char* game; };   // [ceiling] biomes' and game_ceiling_biomes' new areas, comma separated
constexpr AreaBatch kAreaBatches[] = {
    // 0.190 (2026-10-29): the palace's cellar and corrupt harem, then every dungeon; the game's ceiling in the act 2 sewers too
    {"act2_palace,act2_palace_cells,act1_court_to_barracks,act2_tomb,act3_sewer,act3_temple,act3_travincal,expansion_wildtemple,"
     "expansion_baallair,expansion_baallair_throneroom",
     "act2_sewer,act2_palace,act2_palace_cells,act1_court_to_barracks,act2_tomb,act3_sewer,act3_temple,act3_travincal,expansion_wildtemple,"
     "expansion_baallair,expansion_baallair_throneroom"},
};
// `list` (lower case, as read) brought up to date: `game` its game_ceiling_biomes, else [ceiling] biomes; `added` the areas it gained
inline std::vector<std::string> Upgraded(std::vector<std::string> list, bool game, std::string* added = nullptr) {
    if (added) added->clear();
    if (list.empty()) return list;
    for (const AreaBatch& b : kAreaBatches) {
        std::vector<std::string> batch;
        const std::string text = game ? b.game : b.vault;
        for (size_t at = 0; at <= text.size();) {
            const size_t comma = text.find(',', at);
            const std::string one = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            if (!one.empty()) batch.push_back(one);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        bool any = false;
        for (const std::string& a : batch)
            for (const std::string& l : list) any = any || a == l;
        if (any) continue;
        for (const std::string& a : batch) {
            list.push_back(a);
            if (added) *added += (added->empty() ? "" : ",") + a;
        }
    }
    return list;
}
// The default of [ceiling] game_ceiling_biomes: every area of the table
inline std::string AllBiomes() {
    std::string s;
    for (const BiomeKind& b : kBiomeKinds) s += (s.empty() ? "" : ",") + std::string(b.biome);
    return s;
}
// The area's kind, or -1 (no fitting model: our vault there, if it has one)
inline int KindOf(const std::string& biome) {
    for (const BiomeKind& b : kBiomeKinds) if (biome == b.biome) return b.kind;
    return -1;
}
inline std::string MapPath(const Kind& k, const char* which) { return std::string(k.maps) + "_" + which + ".texture"; }   // which: ALB, NRM, ORM
// A piece's model path the unit names (no LOD), one LOD's file, and its unit's path (kind 0 at its first ones)
inline std::string ModelBase(const char* file) {   // (under Tests/; "@...": under data/hd/env/model/ itself)
    return file[0] == '@' ? std::string("data/hd/env/model/") + (file + 1) : std::string("data/hd/env/model/Tests/") + file;
}
inline std::string ModelPath(const char* file) { return ModelBase(file) + ".model"; }
inline std::string LodPath(const char* file, int lod) { return ModelBase(file) + "_lod" + std::to_string(lod) + ".model"; }
inline std::string UnitPath(int kind, bool wall) {
    if (kind == 0) return wall ? kWallUnit : kUnit;
    return std::string("data/hd/env/model/d2r_vr/ceiling/") + kKinds[kind].name + (wall ? "_wall_unit.json" : "_ceiling_unit.json");
}

struct In {
    bool ticked = false;   // [ceiling] game_ceiling
    bool f4 = false;       // VR first person
    bool wanted = false;   // a ceiling wanted here: [ceiling] enabled, first person, in a world, an area on [ceiling] biomes, no menu
    bool heroOk = false;   // the hero's place is known
};

// Why the slab is not shown now ("" = shown)
inline const char* WhyNot(const In& in) {
    if (!in.ticked) return "[ceiling] game_ceiling=0";
    if (!in.f4) return "not VR F4";
    if (!in.wanted) return "no game ceiling wanted here ([ceiling] enabled, first person, an area on [ceiling] game_ceiling_biomes with a kind of its own, no menu)";
    if (!in.heroOk) return "no hero yet";
    return "";
}

// The slab given up on: its unit refused or missing (tried, no definition), or its placings run out (PlaceInstance giving nothing, or 5
// that made no model)
inline bool GivenUp(bool loadTried, bool haveDef, int tries) { return (loadTried && !haveDef) || tries > 4; }
// The ceiling is the game's: ticked, VR F4, a cave ceiling of the game's rock wanted here (a natural cave, [ceiling] game_ceiling_biomes),
// the slab not given up on, our hook on the game not failed - whether or not it is laid yet
inline bool ByGame(bool ticked, bool f4, bool caveWanted, bool givenUp, bool hookFailed) {
    return ticked && f4 && caveWanted && !givenUp && !hookFailed;
}
// Who draws it this frame: 0 nobody (no ceiling wanted here, or the sky's area), 1 our effect's vault (`wanted`: an area on [ceiling] biomes,
// with its own settings), 2 the game's - its slab not laid yet, nothing drawn meanwhile (never our vault for a moment, test-0186), 3 the
// game's, laid. Our effect draws only at 1: in a natural cave the game's rock, in every other room our vault as before (Serg 2026-10-28).
inline int Drawer(bool skyHere, bool wanted, bool byGame, bool laid) { return skyHere ? 0 : byGame ? (laid ? 3 : 2) : wanted ? 1 : 0; }

// Its height over the area's floor: the area's own (height_<biome>, the post ceiling's). (Serg 2026-10-29: the common [ceiling]
// game_ceiling_height overrode every room's own - "надо убрать общий ползунок"; gone, an old ini's key is not read.)

// World units one tile spans: the post ceiling's picture, 4 of its tiles ([ceiling] scale)
inline float TileWorld(float scale) { return 4.0f * (scale > 0.0f ? scale : 10.0f); }

// The area's floor: the lowest the hero has stood since the area changed (the post ceiling's rule: on a ledge the ceiling rose with him).
// `gen` the area's generation now; `seen` the one `floor` was taken in.
inline float Floor(float* floor, unsigned* seen, unsigned gen, float heroY) {
    if (*seen != gen) { *seen = gen; *floor = heroY; }
    if (heroY < *floor) *floor = heroY;
    return *floor;
}

// Its local matrix (row vectors, the place in [12..14]): shown - its top `height` over `floorY`, scaled to `tileWorld` units a tile, its
// middle on the whole tile nearest the hero (every tile is the same: walking, the rock stays where it is in the world); put away - a
// thousandth of its size, 5000 units under the hero.
inline void Matrix(const float hero[3], float floorY, float height, float tileWorld, bool shown, float m[16]) {
    const float k = shown ? tileWorld / kTile : 0.001f;
    float x = hero[0], y = hero[1] - 5000.0f, z = hero[2];
    if (shown) {
        x = (float)(std::floor((double)hero[0] / tileWorld + 0.5) * tileWorld);
        z = (float)(std::floor((double)hero[2] / tileWorld + 0.5) * tileWorld);
        y = floorY + height;
    }
    const float out[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, x, y, z, 1};
    std::memcpy(m, out, sizeof out);
}

// The wall's reach: from the slab's top down to `floorDepth` ([ceiling] floor_depth, the post ceiling's stone under the floor) below the
// area's floor - so it closes the void down to wherever the drawn floor ends
inline float WallReach(float height, float floorDepth) { return height + (floorDepth > 0.0f ? floorDepth : 0.0f); }

// The wall's local matrix: shown - its middle at the hero, its top `height` over `floorY` (the slab's), scaled round to `tileWorld` units a
// tile as the slab and down to `reach`; put away as the slab.
inline void WallMatrix(const float hero[3], float floorY, float height, float tileWorld, float reach, bool shown, float m[16]) {
    const float k = shown ? tileWorld / kTile : 0.001f, ky = shown ? reach / kWallHeight : 0.001f;
    const float out[16] = {k, 0, 0, 0, 0, ky, 0, 0, 0, 0, k, 0, hero[0], shown ? floorY + height : hero[1] - 5000.0f, hero[2], 1};
    std::memcpy(m, out, sizeof out);
}

// ---- [ceiling] game_ceiling_beams: the barracks' own wooden ceiling frame (shared/ceiling_measures.h) ----
// The game ships a flat grid of wooden beams for the barracks (wood_ceiling_frame01: 30.8 x 40.5, its bottom at 14.9 - the barracks walls'
// top - its own act 2 shop wood) and no preset places it. Laid here as the game made it: D2R_VR_Settings copies its mesh, from each of its
// own LOD files, into a grid of whole frames at the size it repeats at (its bounds), as many as one model holds, into the frame's own file
// (its material and maps kept) at one more free test path. vrcam lays that grid kBeamPieces times round the hero on whole frames (world-
// fixed, as the slab's tiles), at the game's own size, its bottom at the ceiling's height; the slab goes up by the frame's depth over it
// and closes the gaps between the beams.
constexpr const char* kBeamSource = "data/hd/env/model/global/prop/act1/barracks/act1_barracks_wood_props/wood_ceiling_frame01";   // + "_lod<n>.model"
constexpr const char* kBeamMaps = "data/hd/env/texture/act2/town/act2_town_shops_wood01";   // its own maps (the unit loads them with it)
constexpr const char* kBeamModelFile = "test_debug_emissive/debug_emissive01";   // under Tests/: named by the LOD table only (2026-10-29 scan)
constexpr const char* kBeamUnit = "data/hd/env/model/d2r_vr/ceiling/barracks_beams_unit.json";
constexpr int kBeamPieces = 4;   // the grid laid 2 x 2 round the hero: about the slab's span
constexpr const char* kBeamBiomes[] = {"act1_barracks"};   // (its jail too: the same biome)
inline bool IsBeamBiome(const std::string& biome) {
    for (const char* b : kBeamBiomes) if (biome == b) return true;
    return false;
}

// The slab's top with the beams under it: the frame's depth (its top less its bottom, the model's own) over the ceiling's height
inline float BeamSlabHeight(float height, float beamBottom, float beamTop) { return height + (beamTop - beamBottom); }

// One of the beams' pieces' local matrix: the game's own size (scale 1); the grids' middle on the whole frame nearest the hero (every frame
// the same: walking, the beams stay where they are in the world), piece `i` the quarter of the 2 x 2 (bit 0: +x, bit 1: +z); the frame's
// bottom `height` over `floorY`. Put away as the slab.
inline void BeamMatrix(const float hero[3], float floorY, float height, float periodX, float periodZ, int cols, int rows, float bottom, int i,
                       bool shown, float m[16]) {
    if (!shown) { Matrix(hero, 0.0f, 0.0f, 1.0f, false, m); return; }
    const double cx = std::floor((double)hero[0] / periodX + 0.5) * periodX, cz = std::floor((double)hero[2] / periodZ + 0.5) * periodZ;
    const double x = cx + ((i & 1) ? 0.5 : -0.5) * cols * periodX, z = cz + ((i & 2) ? 0.5 : -0.5) * rows * periodZ;
    const float out[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)x, floorY + height - bottom, (float)z, 1};
    std::memcpy(m, out, sizeof out);
}

// A room's height (Serg 2026-10-29: "эта галочка должна работать если индивидуальный ползунок высоты ещё не установлен"): its own
// height_<biome> once set (> 0) wins; not set (0, or no key), [ceiling] height_from_walls puts it on the room's walls' top where they are
// measured (> 0) - a rock that hangs `relief` model units (scaled by `k`, the slab's) rests its lowest on the walls; else kHeightAlone.
constexpr float kHeightAlone = 18.0f;
inline float HeightOf(float own, bool fromWalls, float wallsTop, float relief, float k) {
    return own > 0.0f ? own : fromWalls && wallsTop > 0.0f ? wallsTop + relief * k : kHeightAlone;
}

// ---- [ceiling] game_ceiling_dome: the cathedral's dome over its chapel (shared/ceiling_measures.h Dome) ----
// Serg (0.188): the dome our post vault made over the cathedral's chapel, in the game's own ceiling. A slab can't have one (it is laid round
// the hero, and opaque - a dome over it would never be seen), so in the cathedral the slab gives way to a vault of its own: the same smooth
// light stone, made the same way from the same lit rock model (VaultMesh), a flat ceiling over the whole preset with the dome in it - laid
// world-fixed, its middle where the game's own pillars stand round the altar's canopy (found in the renderer), its flat at the ceiling's
// height, its dome raised to pass over the canopy's top. [ceiling] scale does not size its stone: its picture stays one tile of kTile.
constexpr const char* kVaultBiome = "act1_cathedral";   // (ceilmeasure::kCathedral)
// [ceiling] game_ceiling_domes_over_tall: a vault each dome over a tall piece, as the cathedral's (ceilmeasure::DomeOf, kTallFactor) - its
// model at a path of the game's that only its LOD table names (the 2026-10-29 scan: 1198 such; these four named by nothing of ours either),
// its unit ours. The first is the cathedral's, as it was.
constexpr int kVaultSlots = 4;
constexpr const char* kVaultModelFiles[kVaultSlots] = {
    "test_cloth/cloth01",                                               // under Tests/
    "@global/prop/act2/sewer/act2_sewer_sewage/poop_skirt02_old",       // "@": under data/hd/env/model/
    "@expansion/wildtemple/expansion_wildtemple_cages/cage05",
    "@expansion/wildtemple/expansion_wildtemple_monuments/monument05",
};
constexpr const char* kVaultUnits[kVaultSlots] = {
    "data/hd/env/model/d2r_vr/ceiling/cathedral_vault_unit.json", "data/hd/env/model/d2r_vr/ceiling/tall_vault1_unit.json",
    "data/hd/env/model/d2r_vr/ceiling/tall_vault2_unit.json", "data/hd/env/model/d2r_vr/ceiling/tall_vault3_unit.json"};
constexpr const char* kVaultModelFile = kVaultModelFiles[0];
constexpr const char* kVaultUnit = kVaultUnits[0];
constexpr int kVaultCells = 12;   // cells a tile (about 3.3 units: some 34 across the dome); twice as wide past twice its radius
// [ceiling] game_ceiling_dome_size (Serg on 0.189: a size slider over the measured one): the vault laid that many times its size round
// the dome's measured middle - live, so the whole vault scales (its stone's picture with it). From kDomeSizeMin - the smallest the mesh is
// made to cover the cathedral at, and still wider than the canopy's footprint - to kDomeSizeMax. [ceiling] game_ceiling_dome_rise: its
// rise that many times the least that passes over the canopy - never lower.
constexpr float kDomeSizeMin = 0.7f, kDomeSizeMax = 1.6f, kDomeRiseMax = 2.0f;
// The size asked, as laid: in range, and its rim kept a twentieth out past the canopy's footprint (`reach` from the middle)
inline float DomeSize(float asked, float reach, float radius) {
    float s = asked < kDomeSizeMin ? kDomeSizeMin : asked > kDomeSizeMax ? kDomeSizeMax : asked;
    if (!(s == s)) s = 1.0f;
    const float least = radius > 0.0f ? 1.05f * reach / radius : 0.0f;
    return s < least ? least : s;
}
inline float DomeRiseScale(float asked) { return !(asked >= 1.0f) ? 1.0f : asked > kDomeRiseMax ? kDomeRiseMax : asked; }
// Its mesh's half side: the slab's, or the preset's reach round its middle and a tile more - at the smallest size
inline float VaultHalf(float presetReach) {
    const float slab = kTile * kTilesHalf, preset = presetReach + kTile;
    return (preset > slab ? preset : slab) / kDomeSizeMin;
}
// Its rise now: the canopy's top drawn at `baseY` (the preset's floor, the game's piece's own height) + `top`, over the ceiling at `ceilingY`
inline float VaultRise(float baseY, float top, float ceilingY, float reach, float radius) {
    return ceilmeasure::RiseOver(baseY + top - ceilingY, reach, radius);
}
// Its local matrix: shown - its middle at (x, z), its flat at `ceilingY`, `size` times the game's own size across (DomeSize), its dome
// raised to `rise` (the mesh made rising `madeRise`); put away as the slab.
inline void VaultMatrix(const float hero[3], float x, float z, float ceilingY, float rise, float madeRise, float size, bool shown, float m[16]) {
    if (!shown || !(rise > 0.0f) || !(madeRise > 0.0f) || !(size > 0.0f)) { Matrix(hero, 0.0f, 0.0f, 1.0f, false, m); return; }
    const float out[16] = {size, 0, 0, 0, 0, rise / madeRise, 0, 0, 0, 0, size, 0, x, ceilingY, z, 1};
    std::memcpy(m, out, sizeof out);
}

// A unit's file (ours: written by D2R_VR_Settings beside the models): `name` (d2r_vr_<name>), its model and its three maps (`maps` without
// "_ALB.texture" ...). lightMask 19 and shadowMask 0 as the game's own cave pieces that cast none (act1/caves presets): its lights reach it,
// it throws no shadow.
inline std::string UnitJsonOf(const std::string& name, const std::string& model, const std::string& maps) {
    return R"({"dependencies":{"particles":[],"models":[{"path":")" + model + R"("}],)"
           R"("skeletons":[],"animations":[],"textures":[{"path":")" + maps + "_ALB.texture" + R"("},)"
           R"({"path":")" + maps + "_NRM.texture" + R"("},)"
           R"({"path":")" + maps + "_ORM.texture" + R"("}],"physics":[],"json":[],"variantdata":[],)"
           R"("objecteffects":[],"other":[]},"type":"UnitDefinition","name":"d2r_vr_)" + name + R"(","entities":[{"type":"Entity",)"
           R"("name":"entity_root","id":1000000003,"components":[{"type":"UnitRootComponent","name":"component_root",)"
           R"("state_machine_filename":"","doNotInheritRotation":false,"rotationOverride":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
           R"("doNotUseHDHeight":true,"hideAllMeshWhenInOpenedMode":false,"onCreateEventName":"","animations":[]},)"
           R"({"type":"TransformDefinitionComponent","name":"entity_root_TransformDefinition","position":{"x":0.0,"y":0.0,"z":0.0},)"
           R"("orientation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},"scale":{"x":1.0,"y":1.0,"z":1.0},"inheritOnlyPosition":false}]},)"
           R"({"type":"Entity","name":"entity_model","id":1000000004,"components":[{"type":"ModelDefinitionComponent",)"
           R"("name":"component_model_)" + name + R"(","filename":")" + model + R"(",)"
           R"("visibleLayers":1,"lightMask":19,"shadowMask":0,"ghostShadows":false,"floorModel":false,"terrainBlendEnableYUpBlend":false,)"
           R"("terrainBlendMode":1}]}]})";
}
// A kind's slab, or its wall, naming its own model and its maps
inline std::string UnitJson(int kind, bool wall) {
    const Kind& k = kKinds[kind];
    const std::string name = kind == 0 ? (wall ? "cave_ceiling_wall" : "cave_ceiling") : std::string(k.name) + (wall ? "_wall" : "_ceiling");
    return UnitJsonOf(name, ModelPath(wall ? k.wall : k.slab), k.maps);
}
inline std::string UnitJson(bool wall = false) { return UnitJson(0, wall); }   // (the act 1 cave's)
// The barracks' beams' unit: the grid's model at its test path, the frame's own maps
inline std::string BeamUnitJson() { return UnitJsonOf("barracks_beams", ModelPath(kBeamModelFile), kBeamMaps); }
// A vault's unit: its slot's model, its room's kind's maps (the cathedral's: slot 0, the cathedral's stone, as it was)
inline std::string VaultUnitJson(int slot, const std::string& biome) {
    const int kind = KindOf(biome);
    return UnitJsonOf(slot == 0 ? std::string("cathedral_vault") : "tall_vault" + std::to_string(slot), ModelPath(kVaultModelFiles[slot]),
                      kKinds[kind >= 0 ? kind : KindOf(kVaultBiome)].maps);
}
inline std::string VaultUnitJson() { return VaultUnitJson(0, kVaultBiome); }

}  // namespace gameceiling
