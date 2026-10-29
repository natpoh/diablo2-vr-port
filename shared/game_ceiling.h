#pragma once
// game_ceiling.h - [ceiling] game_ceiling: the cave ceiling drawn by the game itself (docs/game_ceiling.md). A slab of the game's own lit
// cave rock - act 1's rocks03 model (StandardMaterial) with our slab's mesh in it and the floor's own tiling rock as its maps
// (shared/granny_remesh.h CeilingMesh; D2R_VR_Settings makes it from the player's game, tools/settings/dome_assets.cpp) - loaded by the
// game's own unit loader and laid over the hero every frame by vrcam (engitem::CeilingFrame), as [sky] game_sky's dome is. The game
// draws it in its own frame: both eyes, its torches' and the hero's light on it, its fog over it, fire and spells in their own order.
// The pure parts, for vrcam and Settings both; tested in cleanroom/tests/game_ceiling_test.cpp.
#include <cmath>
#include <cstring>
#include <string>

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
// settings, game_ceiling or not - never nothing. The act 2 sewers wait (no brick of the game's matches their picture yet).
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
    {"jungle_cave", "data/hd/env/texture/terrain/act3/jungle/act3_jungle_dungeon_rocky01", kRelief, "pillar01", "Pillar_02"},
    {"ice_cave", "data/hd/env/texture/terrain/expansion/icecave/Act5_Rocky_Floor", kRelief, "Pillar_03", "testy"},
    // reference D2R_Ceiling_act1_crypt (tall stone slabs): act 4's tall stone tiles in columns
    {"crypt", "data/hd/env/texture/terrain/act4/mesa/act4_mesa_stonetile01", 0.0f, "vertblend_house", nullptr},
    // reference D2R_Ceiling_act1_barracks_smooth (smooth dark stone; the barracks' and the catacombs'): the Arcane Sanctuary's dark grunge
    {"smooth_dark", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge01", 0.0f, "test_terrainblending/cliff01", nullptr},
    // reference D2R_Ceiling_act1_crypt_smooth (smooth light stone; the cathedral's): its lighter grunge
    {"smooth_light", "data/hd/env/texture/terrain/act2/arcane/act2_arcane_grunge02", 0.0f, "test_terrainblending/ramp01", nullptr},
    // reference D2R_Ceiling_act2_palace (beige ornamented squares): the palace cells' ornamented marble
    {"palace", "data/hd/env/texture/terrain/act2/palace/act2_palace_cell_marble_tile01", 0.0f, "TextureExportTesting/TestCube", nullptr},
};
constexpr int kKindCount = (int)(sizeof kKinds / sizeof kKinds[0]);
// Which kind each area gets (the game's biome names); an area not here keeps our vault (or none, as before)
struct BiomeKind { const char* biome; int kind; };
constexpr BiomeKind kBiomeKinds[] = {
    {"act1_caves", 0}, {"act2_maggot", 1}, {"act3_spider", 1}, {"act3_jungle_dungeon", 1}, {"expansion_icecave", 2},
    {"act1_crypt", 3}, {"act1_barracks", 4}, {"act1_catacombs", 4}, {"act1_cathedral", 5}, {"act2_palace_clean", 6},
};
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
inline std::string ModelPath(const char* file) { return std::string("data/hd/env/model/Tests/") + file + ".model"; }
inline std::string LodPath(const char* file, int lod) { return std::string("data/hd/env/model/Tests/") + file + "_lod" + std::to_string(lod) + ".model"; }
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

// Its height over the area's floor: [ceiling] game_ceiling_height when set (> 0), else the area's own (height_<biome>, the post ceiling's)
inline float Height(float asked, float areas) { return asked > 0.0f ? asked : areas; }

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

// A unit's file (ours: written by D2R_VR_Settings beside the models) - a kind's slab, or its wall, naming its own model and its maps.
// lightMask 19 and shadowMask 0 as the game's own cave pieces that cast none (act1/caves presets): its lights reach it, it throws no shadow.
inline std::string UnitJson(int kind, bool wall) {
    const Kind& k = kKinds[kind];
    const std::string model = ModelPath(wall ? k.wall : k.slab);
    const std::string name = kind == 0 ? (wall ? "cave_ceiling_wall" : "cave_ceiling") : std::string(k.name) + (wall ? "_wall" : "_ceiling");
    return R"({"dependencies":{"particles":[],"models":[{"path":")" + model + R"("}],)"
           R"("skeletons":[],"animations":[],"textures":[{"path":")" + MapPath(k, "ALB") + R"("},)"
           R"({"path":")" + MapPath(k, "NRM") + R"("},)"
           R"({"path":")" + MapPath(k, "ORM") + R"("}],"physics":[],"json":[],"variantdata":[],)"
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
inline std::string UnitJson(bool wall = false) { return UnitJson(0, wall); }   // (the act 1 cave's)

}  // namespace gameceiling
