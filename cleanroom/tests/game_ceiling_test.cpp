// game_ceiling_test - shared/game_ceiling.h and granny_remesh.h's CeilingMesh: [ceiling] game_ceiling's slab of the game's own lit rock.
//   1. shown only ticked, in VR F4, where a ceiling is wanted, the hero known - each missing one named;
//   2. its height (the area's own height_<biome>, or its walls' top), a tile's span (4 of [ceiling] scale's), the area's floor (the lowest stood since
//      the area changed); its matrix: its top over the floor, its middle on the whole tile nearest the hero, put away small under him;
//   3. the slab: the index count vrcam expects, 16-bit indices in range, its normals down, its rock 0 .. kRelief down, and every tile the
//      same (a vertex and the one a tile on: the same height, normal, tangent, uv one more) - moved by whole tiles it shows no step;
//   4. its unit: one root and a model as the game wants (vr/sky_dome.h's check), naming our model's path and the floor's three maps;
//   5. [ceiling] height_from_walls (ceiling_measures.h): a preset's placements read (its own JSON reader), a wall piece's top, the median,
//      the areas' table, the measures file written and read back (another version taken as nothing), the height on the walls' top;
//   6. [ceiling] game_ceiling_beams: the frame's grid (as many whole frames as 16-bit indices hold), laid 2 x 2 at the game's size on whole
//      frames (walking within one: they stay; a frame on: one frame on), its bottom at the height, the slab over it; its unit and path;
//   7. [ceiling] game_ceiling_dome: the dome of a preset like the cathedral's (on the four pillars round its highest piece, over that
//      piece's footprint; none without four, or past them), its line in the file, the game's pieces matched (the canopy and a pillar as far
//      apart drawn as placed), the vault's mesh (its rim on a ring, flat outside, the dome's height inside, normals down), its rise and
//      matrix (world-fixed, put away with no rise), its unit and path;
//   game_ceiling_test [<the game's rocks03_lod1.model> [<out .model> [<the game's wood_ceiling_frame01_lod0.model>]]]: the rock made our
//   slab, written, read again - our vertices and indices, its three maps renamed, the old names gone, the types' section the game's byte for
//   byte, still its StandardMaterial; the frame's mesh read (where its bounds say), laid in its grid in its own file, read back.
#include "ceiling_measures.h"
#include "game_ceiling.h"
#include "granny_remesh.h"
#include "sky_dome.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace gameceiling;

static bool ReadAll(const char* path, std::string* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out->clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

static float Unhalf(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = e ? std::ldexp(1.0f + m / 1024.0f, e - 15) : std::ldexp(m / 1024.0f, -14);
    return h & 0x8000 ? -v : v;
}

int main(int argc, char** argv) {
    // 1. when
    In ok;
    ok.ticked = ok.f4 = ok.wanted = ok.heroOk = true;
    CHECK(std::strcmp(WhyNot(ok), "") == 0);
    {
        In in = ok; in.ticked = false; CHECK(std::strstr(WhyNot(in), "game_ceiling=0") != nullptr);
        in = ok; in.f4 = false; CHECK(std::strstr(WhyNot(in), "F4") != nullptr);
        in = ok; in.wanted = false; CHECK(std::strstr(WhyNot(in), "no game ceiling wanted") != nullptr);
        in = ok; in.heroOk = false; CHECK(std::strstr(WhyNot(in), "hero") != nullptr);
    }
    // 1b. who draws it (test-0186: entering a cave showed our old vault until the slab was laid). The game's while ticked in VR F4 and
    //     not given up on - laid or not; our effect only when it is not the game's at all; while the slab loads, nobody.
    //     Only in a natural cave ([ceiling] game_ceiling_biomes): every other room on [ceiling] biomes keeps our vault, game_ceiling or not
    //     (Serg 2026-10-28: the barracks got the cave rock).
    CHECK(ByGame(true, true, true, false, false));
    CHECK(!ByGame(false, true, true, false, false) && !ByGame(true, false, true, false, false) && !ByGame(true, true, false, false, false) &&
          !ByGame(true, true, true, true, false) && !ByGame(true, true, true, false, true));
    CHECK(!GivenUp(false, false, 0));   // not tried yet (outside a cave): not given up - the first cave frame is the game's already
    CHECK(GivenUp(true, false, 0) && !GivenUp(true, true, 4) && GivenUp(true, true, 5));
    CHECK(Drawer(false, true, true, false) == 2);   // the first frames in a cave, slab not laid: nothing drawn, NOT our vault
    CHECK(Drawer(false, true, true, true) == 3);
    CHECK(Drawer(false, false, true, true) == 3);   // a cave on game_ceiling_biomes only (not on biomes): the game's still
    CHECK(Drawer(false, true, false, false) == 1);  // game_ceiling off (or given up, or not F4): our vault as before
    CHECK(Drawer(false, false, false, false) == 0 && Drawer(true, true, false, false) == 0);
    for (int laid = 0; laid < 2; ++laid)            // a cave, the game's: our vault never, whatever the slab's state
        CHECK(Drawer(false, true, ByGame(true, true, true, GivenUp(false, false, 0), false), laid != 0) != 1);
    // the barracks with game_ceiling=1 (on [ceiling] biomes, not a natural cave): our vault, as before
    CHECK(Drawer(false, true, ByGame(true, true, false, false, false), false) == 1);
    // 2. where
    CHECK(TileWorld(10.0f) == 40.0f && TileWorld(0.0f) == 40.0f && TileWorld(2.5f) == 10.0f);
    {
        float floor = 0.0f;
        unsigned seen = ~0u;
        CHECK(Floor(&floor, &seen, 7, 100.0f) == 100.0f && seen == 7);
        CHECK(Floor(&floor, &seen, 7, 96.0f) == 96.0f);    // down a step: the floor with him
        CHECK(Floor(&floor, &seen, 7, 104.0f) == 96.0f);   // up on a ledge: the floor stays
        CHECK(Floor(&floor, &seen, 8, 104.0f) == 104.0f);  // another area: its own
    }
    {
        const float hero[3] = {57.0f, 96.0f, -21.0f};
        float m[16];
        Matrix(hero, 90.0f, 23.0f, 40.0f, true, m);
        CHECK(m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f && m[15] == 1.0f && m[1] == 0.0f && m[4] == 0.0f);
        CHECK(m[12] == 40.0f && m[14] == -40.0f && m[13] == 113.0f);   // the nearest whole tiles (57 -> 40, -21 -> -40), the floor + 23
        const float on[3] = {61.0f, 96.0f, -19.0f};
        Matrix(on, 90.0f, 23.0f, 40.0f, true, m);
        CHECK(m[12] == 80.0f && m[14] == 0.0f);
        Matrix(hero, 90.0f, 23.0f, 20.0f, true, m);                    // a tile of 20 units: half the model's
        CHECK(m[0] == 0.5f && m[12] == 60.0f && m[14] == -20.0f);
        const float far[3] = {123456.0f, 0.0f, -654321.0f};             // far from the origin: still a whole number of tiles
        Matrix(far, 0.0f, 23.0f, 40.0f, true, m);
        CHECK(std::fmod((double)m[12], 40.0) == 0.0 && std::fabs(m[12] - far[0]) <= 20.0f && std::fabs(m[14] - far[2]) <= 20.0f);
        Matrix(hero, 90.0f, 23.0f, 40.0f, false, m);
        CHECK(m[0] == 0.001f && m[12] == 57.0f && m[13] == 96.0f - 5000.0f && m[14] == -21.0f);
    }
    // 3. the slab
    const grannyremesh::Mesh mesh = grannyremesh::CeilingMesh(kTile, kTilesHalf, kCells, kRelief);
    const int n = 2 * kTilesHalf * kCells, row = n + 1;
    CHECK(mesh.v.size() == (size_t)row * row && mesh.v.size() <= 65536);
    CHECK(mesh.idx.size() == kIndexCount);
    bool inRange = true, down = true, relief = true, same = true;
    for (uint16_t i : mesh.idx) inRange = inRange && i < mesh.v.size();
    const float half = kTile * kTilesHalf;
    CHECK(mesh.scale == half);
    for (const grannyremesh::Vertex& v : mesh.v) {
        down = down && v.normal[1] < -90;   // (a gentle rock: within ~45 degrees of straight down)
        const float y = v.pos[1] / 32767.0f * mesh.scale;
        relief = relief && y <= 0.01f && y >= -kRelief - 0.01f;
    }
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j + kCells <= n; ++j) {
            const grannyremesh::Vertex& a = mesh.v[(size_t)i * row + j];
            const grannyremesh::Vertex& b = mesh.v[(size_t)i * row + j + kCells];
            const grannyremesh::Vertex& c = mesh.v[(size_t)j * row + i];             // (and along z)
            const grannyremesh::Vertex& d = mesh.v[(size_t)(j + kCells) * row + i];
            same = same && std::abs(a.pos[1] - b.pos[1]) <= 1 && std::memcmp(a.normal, b.normal, 4) == 0 && std::memcmp(a.tangent, b.tangent, 4) == 0 &&
                   std::fabs(Unhalf(b.uv[0]) - Unhalf(a.uv[0]) - 1.0f) < 0.01f && a.uv[1] == b.uv[1];
            same = same && std::abs(c.pos[1] - d.pos[1]) <= 1 && std::memcmp(c.normal, d.normal, 4) == 0 &&
                   std::fabs(Unhalf(c.uv[1]) - Unhalf(d.uv[1]) - 1.0f) < 0.01f;   // (v along -z)
        }
    CHECK(inRange);
    CHECK(down);
    CHECK(relief);
    CHECK(same);
    CHECK(mesh.mn[0] == -half && mesh.mx[0] == half && mesh.mn[2] == -half && mesh.mx[2] == half && mesh.mx[1] <= 0.0f && mesh.mn[1] >= -kRelief);
    CHECK(mesh.v[0].pos[3] == (int16_t)std::lround(std::log2(half) / 13.0 * 32767.0));
    // 4. its unit
    const std::string unit = UnitJson();
    CHECK(skydome::UnitFileProblem(unit) == nullptr);
    CHECK(unit.find(std::string("\"filename\":\"") + kModel + "\"") != std::string::npos);
    for (const Map& m : kMaps) CHECK(unit.find(m.to) != std::string::npos);
    CHECK(unit.find("\"lightMask\":19") != std::string::npos && unit.find("\"shadowMask\":0") != std::string::npos);
    CHECK(std::string(kModel) == std::string(kModelDir) + kModelFile + ".model");
    CHECK(std::string(kModel).find("Barbarian_ScaleRef") == std::string::npos && std::string(kModel).find("driver_mesh_test01") == std::string::npos);
    // 5. [ceiling] game_ceiling_wall: the rim bent down
    CHECK(WallReach(23.0f, 20.0f) == 43.0f && WallReach(23.0f, 0.0f) == 23.0f && WallReach(23.0f, -5.0f) == 23.0f);
    {
        const float hero[3] = {57.0f, 96.0f, -21.0f};
        float w[16];
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, 43.0f, true, w);
        CHECK(w[12] == 57.0f && w[14] == -21.0f && w[13] == 113.0f);                     // at the hero (not snapped), its top the slab's
        CHECK(w[0] == 1.0f && w[10] == 1.0f && std::fabs(w[5] - 43.0f / kWallHeight) < 1e-6f && w[1] == 0.0f && w[4] == 0.0f);
        WallMatrix(hero, 90.0f, 23.0f, 20.0f, 43.0f, true, w);
        CHECK(w[0] == 0.5f && std::fabs(w[5] - 43.0f / kWallHeight) < 1e-6f);           // round with the tile, down to the reach
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, 43.0f, false, w);
        CHECK(w[0] == 0.001f && w[5] == 0.001f && w[13] == 96.0f - 5000.0f);
        // its bottom: the reach under the top - floor_depth under the floor
        WallMatrix(hero, 90.0f, 23.0f, 40.0f, WallReach(23.0f, 20.0f), true, w);
        CHECK(std::fabs(w[13] - kWallHeight * w[5] - (90.0f - 20.0f)) < 1e-3f);
    }
    // under the slab wherever the slab snaps (its middle up to half a tile from the hero), its top inside the slab's rock
    CHECK(kWallRadius + kTile * 0.5f <= kTile * kTilesHalf && kWallRadius > kTile * (kTilesHalf - 1));
    CHECK(mesh.mx[1] <= 0.0f);   // (the wall's top at 0: at or above the slab's underside everywhere)
    const grannyremesh::Mesh wall = grannyremesh::WallMesh(kWallRadius, kWallHeight, kWallSegments, kWallRows, kTile);
    CHECK(wall.v.size() == (size_t)(kWallSegments + 1) * (kWallRows + 1) && wall.idx.size() == kWallIndexCount);
    {
        bool in = true, range = true, onRing = true;
        for (uint16_t i : wall.idx) range = range && i < wall.v.size();
        for (const grannyremesh::Vertex& v : wall.v) {
            const float x = v.pos[0] / 32767.0f * wall.scale, z = v.pos[2] / 32767.0f * wall.scale;
            in = in && v.normal[1] == 0 && x * v.normal[0] + z * v.normal[2] < 0.0f;   // level, facing the middle
            onRing = onRing && std::fabs(std::sqrt(x * x + z * z) - kWallRadius) < 0.05f;
        }
        CHECK(range);
        CHECK(in);
        CHECK(onRing);
        CHECK(wall.mx[1] == 0.0f && std::fabs(wall.mn[1] + kWallHeight) < 1e-3f);
        const float u0 = Unhalf(wall.v[0].uv[0]), u1 = Unhalf(wall.v[kWallSegments].uv[0]);
        CHECK(u0 == 0.0f && u1 >= 1.0f && std::fabs(u1 - std::floor(u1 + 0.5f)) < 0.01f);   // whole pictures round: no seam where it closes
        CHECK(std::fabs(Unhalf(wall.v.back().uv[1]) + 1.0f) < 0.01f);                     // one picture down
    }
    const std::string wallUnit = UnitJson(true);
    CHECK(skydome::UnitFileProblem(wallUnit) == nullptr);
    CHECK(wallUnit.find(std::string("\"filename\":\"") + kWallModel + "\"") != std::string::npos && wallUnit.find(kModel) == std::string::npos);
    CHECK(std::string(kWallModel) == std::string(kModelDir) + kWallModelFile + ".model" && std::string(kWallModel) != kModel);
    CHECK(std::string(kWallUnit) != kUnit);
    // 6. every room its own kind: kind 0 the first cave's at its first paths and units (the files already made stay right), every kind's
    //    pieces at paths of their own - never the sky dome's -, each unit as the game wants naming its own model and its own three maps
    CHECK(UnitPath(0, false) == kUnit && UnitPath(0, true) == kWallUnit && UnitJson(0, false) == UnitJson() && UnitJson(0, true) == UnitJson(true));
    CHECK(ModelPath(kKinds[0].slab) == kModel && ModelPath(kKinds[0].wall) == kWallModel && MapPath(kKinds[0], "ALB") == kMaps[0].to);
    {
        std::vector<std::string> paths, units;
        for (int k = 0; k < kKindCount; ++k)
            for (const bool wall : {false, true}) {
                const char* file = wall ? kKinds[k].wall : kKinds[k].slab;
                if (!file) continue;
                paths.push_back(ModelPath(file));
                units.push_back(UnitPath(k, wall));
                const std::string u = UnitJson(k, wall);
                CHECK(skydome::UnitFileProblem(u) == nullptr && u.find(ModelPath(file)) != std::string::npos);
                for (const char* m : {"ALB", "NRM", "ORM"}) CHECK(u.find(MapPath(kKinds[k], m)) != std::string::npos);
                CHECK(LodPath(file, 3) == ModelPath(file).substr(0, ModelPath(file).size() - 6) + "_lod3.model");
            }
        for (size_t i = 0; i < paths.size(); ++i)
            for (size_t j = i + 1; j < paths.size(); ++j) CHECK(paths[i] != paths[j] && units[i] != units[j]);
        for (const std::string& p : paths)
            CHECK(p.find("Barbarian_ScaleRef") == std::string::npos && p.find("driver_mesh_test01") == std::string::npos &&
                  p.find("test_resolution") == std::string::npos && p.find("flag01") == std::string::npos &&
                  p.find("pillar01") == std::string::npos && p.find("Pillar_0") == std::string::npos);   // (the sky domes'; a prop palette's)
        // (test_cloth's others are free: the game draws its driver_mesh_test01 as a plain model - the magenta test dome - and the
        // sky's act 2 - 4 domes took Tests/pillar01, Pillar_02, Pillar_03: the jungle's and ice caves' kinds moved off them, 2026-10-29)
        for (int k = 0; k < kKindCount; ++k) CHECK((kKinds[k].wall != nullptr) == (kKinds[k].relief == kRelief));   // walls: the caves'
    }
    // matched to the pictures chosen for our vault: the barracks and catacombs share theirs (smooth dark stone), so they share a kind
    CHECK(KindOf("act1_caves") == 0 && KindOf("act1_barracks") == KindOf("act1_catacombs") &&
          std::string(kKinds[KindOf("act1_barracks")].maps).find("grunge01") != std::string::npos);
    CHECK(KindOf("act4_diab") == -1 && KindOf("act1_outdoors") == -1 && KindOf("") == -1);   // (the Chaos Sanctuary keeps its void)
    for (const BiomeKind& b : kBiomeKinds) CHECK(b.kind >= 0 && b.kind < kKindCount && AllBiomes().find(b.biome) != std::string::npos);
    for (const char* room : {"act1_crypt", "act1_barracks", "act1_cathedral", "act1_catacombs", "act2_palace_clean", "act2_sewer", "act2_palace",
                             "act2_palace_cells"})   // every room of [ceiling] biomes has a kind now
        CHECK(KindOf(room) >= 0);
    // the rooms with no picture matched (Serg on 0.189: "надо взять просто с пола текстуру"): each its own kind, its own floor's maps (their
    // paths checked against the game's biomes by dome_assets_test), flat, no wall; every kind its own mesh bytes
    for (const FloorKind& f : kFloorKinds) {
        const int k = KindOf(f.biome);
        CHECK(k >= 0 && std::string(kKinds[k].name) == f.kind && kKinds[k].relief == 0.0f && kKinds[k].wall == nullptr);
        for (const FloorKind& g : kFloorKinds) CHECK(&f == &g || KindOf(g.biome) != k || std::string(f.kind) == g.kind);   // (one floor: one kind)
    }
    CHECK(std::string(kKinds[KindOf("act2_sewer")].maps).find("act2_sewer_cobblestone01") != std::string::npos);
    // the palace cellar (levels 52 - 54): smooth stone, not its floor's tiles (Serg on 0.190: "как 34") - the catacombs' smooth dark stone
    CHECK(KindOf("act2_palace_cells") == KindOf("act1_catacombs") && std::string(kKinds[KindOf("act2_palace_cells")].name) == "smooth_dark");
    CHECK(KindOf("act2_tomb") == KindOf("act1_catacombs"));   // and every tomb, levels 55 - 61, 66 - 73 ("в 55 тоже")
    for (const FloorKind& f : kFloorKinds) CHECK(std::string(f.biome) != "act2_palace_cells");
    for (int a = 0; a < kKindCount; ++a)
        for (const bool wa : {false, true})
            for (int b = 0; b < kKindCount; ++b)
                for (const bool wb : {false, true}) CHECK((a == b && wa == wb) || SaltOf(a, wa) != SaltOf(b, wb));

    // ---- [ceiling] height_from_walls: the walls' top from the presets (ceiling_measures.h) ----
    {
        const char* preset = R"({"name":"barE1","entities":[{"type":"Entity","name":"wall","components":[
            {"type":"TransformDefinitionComponent","name":"t","position":{"x":3.0,"y":0.5,"z":-2.0},"scale":{"x":1.0,"y":1.0,"z":1.0}},
            {"type":"ModelDefinitionComponent","name":"m","filename":"data/hd/env/model/act1/barracks/Wall01.model","visibleLayers":1}]},
          {"type":"Entity","name":"pillar","components":[{"type":"ModelDefinitionComponent","filename":"data/hd/env/model/act1/barracks/pillar02.model"},
            {"type":"TransformDefinitionComponent","position":{"y":-1e0},"scale":{"y":2}}]},
          {"type":"Entity","name":"no model","components":[{"type":"TransformDefinitionComponent","position":{"y":9}}]},
          {"type":"Entity","name":"nested","children":[{"components":[{"type":"ModelDefinitionComponent","filename":"x/crate.model"}]}],
           "note":"a \"quoted\" \\ string é","flag":true,"none":null,"list":[1,-2.5e-1,[]]}]})";
        std::vector<ceilmeasure::Placement> placed;
        CHECK(ceilmeasure::Placements(preset, &placed) && placed.size() == 3);
        if (placed.size() == 3) {
            CHECK(placed[0].model == "data/hd/env/model/act1/barracks/wall01.model" && placed[0].y == 0.5f && placed[0].scaleY == 1.0f);
            CHECK(placed[1].model.find("pillar02") != std::string::npos && placed[1].y == -1.0f && placed[1].scaleY == 2.0f);   // (its transform after it)
            CHECK(placed[2].model == "x/crate.model" && placed[2].y == 0.0f && placed[2].scaleY == 1.0f);   // (no transform: at 0, scale 1)
            CHECK(std::fabs(ceilmeasure::WallTop(placed[0], 0.0f, 14.4f) - 14.9f) < 1e-4f);   // its height + its top
            CHECK(std::fabs(ceilmeasure::WallTop(placed[1], 0.0f, 8.0f) - 15.0f) < 1e-4f);    // scaled: 8 x 2 - 1
            CHECK(ceilmeasure::WallTop(placed[2], 0.0f, 20.0f) < 0.0f);                      // a crate is not a wall
            CHECK(ceilmeasure::WallTop(placed[0], 10.0f, 15.0f) < 0.0f);                     // a cap 5 tall is not a wall
        }
        std::vector<ceilmeasure::Placement> none;
        CHECK(!ceilmeasure::Placements("{\"a\":[1,2", &none) && !ceilmeasure::Placements("{} x", &none));   // cut short; trailing bytes
        CHECK(ceilmeasure::IsWallPiece("data/hd/env/model/act1/caves/act1_caves_cliffs/CLIFF03.model") && ceilmeasure::IsWallPiece("column_a") &&
              !ceilmeasure::IsWallPiece("wood_ceiling_frame01"));
        CHECK(ceilmeasure::Median({}) == 0.0f && ceilmeasure::Median({3.0f, 1.0f, 2.0f}) == 2.0f && ceilmeasure::Median({4.0f, 1.0f, 2.0f, 3.0f}) == 2.5f);
        // every area named once, indoors only, each its own presets' folder
        for (size_t i = 0; i < sizeof ceilmeasure::kAreas / sizeof ceilmeasure::kAreas[0]; ++i) {
            const std::string b = ceilmeasure::kAreas[i].biome, p = ceilmeasure::kAreas[i].presets;
            CHECK(p.compare(0, 3, b.substr(0, 3)) == 0 || b.rfind("expansion", 0) == 0);
            CHECK(b != "act4_diab" && b != "act4_fort" && b.find("outdoors") == std::string::npos);
            for (size_t j = i + 1; j < sizeof ceilmeasure::kAreas / sizeof ceilmeasure::kAreas[0]; ++j) CHECK(b != ceilmeasure::kAreas[j].biome);
        }
        for (const BiomeKind& k : kBiomeKinds) {   // every room with a kind of the game's ceiling can be measured
            bool in = false;
            for (const ceilmeasure::Area& a : ceilmeasure::kAreas) in = in || k.biome == std::string(a.biome);
            CHECK(in);
        }
        // the file: written, read back; another version or none taken as nothing
        ceilmeasure::Measures m, back;
        m.haveBeam = true;
        m.beam = ceilmeasure::Beam{30.7608f, 40.471f, 14.9234f, 15.4297f, 7, 6, 272160u};
        m.walls = {{"act1_barracks", 14.92f}, {"act1_crypt", 14.1f}};
        CHECK(ceilmeasure::Parse(ceilmeasure::Format(m), &back) && back.haveBeam && back.beam.cols == 7 && back.beam.rows == 6 &&
              back.beam.indices == 272160u && std::fabs(back.beam.periodZ - 40.471f) < 1e-3f && back.walls.size() == 2);
        CHECK(std::fabs(ceilmeasure::WallsTopOf(back, "act1_crypt") - 14.1f) < 1e-3f && ceilmeasure::WallsTopOf(back, "act1_caves") == 0.0f);
        std::string other = ceilmeasure::Format(m);
        const std::string cur = "version " + std::to_string(ceilmeasure::kVersion);
        other.replace(other.find(cur), cur.size(), "version 1");   // (the last one's: measured again)
        CHECK(!ceilmeasure::Parse(other, &back) && !back.haveBeam && back.walls.empty());
        CHECK(!ceilmeasure::Parse("", &back) && !ceilmeasure::Parse("walls act1_crypt 14\n", &back) && back.walls.empty());
        CHECK(ceilmeasure::Parse("version 4\r\nbeam 0 1 2 3 4 5 6\r\nwalls a 0\r\nwalls b 7.5\r\n", &back) && !back.haveBeam &&   // (a frame of no size; a top of 0)
              back.walls.size() == 1 && ceilmeasure::WallsTopOf(back, "b") == 7.5f);
        // the height (Serg 2026-10-29: the walls only where the room's own is not set): its own once set; else on the walls' top when
        // ticked and measured (a hanging rock resting its lowest on them); else 18
        CHECK(HeightOf(15.1f, true, 14.9f, 0.0f, 1.0f) == 15.1f);                 // its own, set, wins over the walls
        CHECK(HeightOf(0.0f, true, 14.9f, 0.0f, 1.0f) == 14.9f);                  // not set: the walls
        CHECK(HeightOf(0.0f, true, 10.4f, kRelief, 0.5f) == 10.4f + 3.0f);        // the cave rock: 6 model units at half a tile's scale
        CHECK(HeightOf(0.0f, true, 0.0f, 0.0f, 1.0f) == kHeightAlone && HeightOf(0.0f, false, 14.9f, 0.0f, 1.0f) == kHeightAlone);   // not measured / not ticked
        CHECK(kHeightAlone == 18.0f && HeightOf(-3.0f, false, 0.0f, 0.0f, 1.0f) == kHeightAlone);
        // a room's floor: the first layer of solid stone among its terrain's, nothing loose or wet; none - "" (the cave rock then)
        CHECK(FloorMaps({"t/act3_docktown_base_dirt_ALB.texture", "t/act3_temple_stone01_ALB.texture", "t/act3_temple_stone02_ALB.texture"}) ==
              "t/act3_temple_stone01");
        CHECK(FloorMaps({"t/expansion_wildtemple_puddle01_ALB.texture", "t/expansion_wildtemple_caverock_ALB.texture"}) == "t/expansion_wildtemple_caverock");
        CHECK(FloorMaps({"t/act1_barracks_pebbles01_ALB.texture", "t/act1_barracks_cobblestone03_ALB.texture"}) == "t/act1_barracks_cobblestone03");
        CHECK(FloorMaps({"t/act2_tomb_sand02_ALB.texture", "t/act2_tomb_mud01_ALB.texture", "t/act2_tomb_sand_hard01_ALB.texture"}).empty());
        CHECK(FloorMaps({"t/expansion_baallair_floor01_damaged04b_ALB.texture", "t/expansion_baallair_floor01_ALB.texture"}) == "t/expansion_baallair_floor01");
        CHECK(FloorMaps({}).empty() && std::string(kRockMaps).find("act1_caves_rock01") != std::string::npos);
        // an upgrading player's lists (0.190: the installer keeps them as they were): one from before a batch gets it, once; one with any
        // of its areas is taken as it is (what the player took out stays out); an empty one stays empty
        {
            const std::vector<std::string> oldVault = {"act1_caves", "act1_crypt", "act1_barracks", "act1_cathedral", "act1_catacombs", "act2_sewer",
                                                       "act2_palace_clean"};
            const std::vector<std::string> oldGame = {"act1_caves", "act2_maggot", "act3_spider", "act3_jungle_dungeon", "expansion_icecave",
                                                      "act1_crypt", "act1_barracks", "act1_catacombs", "act1_cathedral", "act2_palace_clean"};
            std::string added;
            const std::vector<std::string> v = Upgraded(oldVault, false, &added), g = Upgraded(oldGame, true);
            auto has = [](const std::vector<std::string>& l, const char* b) { return std::find(l.begin(), l.end(), std::string(b)) != l.end(); };
            CHECK(v.size() == oldVault.size() + 10 && has(v, "act2_tomb") && has(v, "act2_palace_cells") && !has(v, "act2_maggot"));
            CHECK(added.find("expansion_baallair_throneroom") != std::string::npos && added.find("act2_sewer") == std::string::npos);
            CHECK(g.size() == oldGame.size() + 11 && has(g, "act2_sewer") && has(g, "act3_temple"));
            for (const BiomeKind& k : kBiomeKinds) CHECK(has(g, k.biome));   // every area with a kind on an upgraded game list
            CHECK(Upgraded(g, true) == g && Upgraded(v, false, &added) == v && added.empty());   // (once)
            std::vector<std::string> mine = g;
            mine.erase(std::find(mine.begin(), mine.end(), std::string("act2_tomb")));   // the player took the tombs out
            CHECK(Upgraded(mine, true) == mine);
            CHECK(Upgraded({}, true).empty());
        }
        // a model at the game's own (not Tests/): "@"
        CHECK(ModelPath("@a/b/test_asset") == "data/hd/env/model/a/b/test_asset.model" && LodPath("@a/x", 2) == "data/hd/env/model/a/x_lod2.model" &&
              ModelPath("testy") == "data/hd/env/model/Tests/testy.model");
    }

    // ---- [ceiling] game_ceiling_beams: the barracks' frame laid in a grid (game_ceiling.h, ceiling_measures.h) ----
    {
        int cols = 0, rows = 0;   // the game's frame: 1430 vertices, 30.76 x 40.47 -> 7 x 6 (215 x 243): 42 x 1430 = 60060 vertices
        CHECK(ceilmeasure::BeamGrid(1430, 30.7608f, 40.471f, &cols, &rows) && cols == 7 && rows == 6);
        CHECK(ceilmeasure::BeamGrid(352, 30.7608f, 40.471f, &cols, &rows) && (unsigned)(cols * rows) * 352u <= 65536u && cols * rows > 150);
        CHECK(!ceilmeasure::BeamGrid(70000, 1.0f, 1.0f, &cols, &rows) && !ceilmeasure::BeamGrid(0, 1.0f, 1.0f, &cols, &rows));
        CHECK(IsBeamBiome("act1_barracks") && !IsBeamBiome("act1_catacombs") && !IsBeamBiome(""));
        CHECK(std::fabs(BeamSlabHeight(15.1f, 14.9234f, 15.4297f) - (15.1f + 0.5063f)) < 1e-4f);
        const float px = 30.7608f, pz = 40.471f, bottom = 14.9234f;
        float a[4][16], b[16];
        const float hero[3] = {1003.0f, 2.0f, -517.0f};
        for (int i = 0; i < kBeamPieces; ++i) BeamMatrix(hero, 2.0f, 15.0f, px, pz, 7, 6, bottom, i, true, a[i]);
        for (int i = 0; i < kBeamPieces; ++i) {
            CHECK(a[i][0] == 1.0f && a[i][5] == 1.0f && a[i][10] == 1.0f && a[i][1] == 0.0f);   // the game's own size
            CHECK(std::fabs(a[i][13] - (2.0f + 15.0f - bottom)) < 1e-4f);                      // its bottom at the height over the floor
            // the four quarters side by side: their middles 7 frames apart across, 6 along, round the whole frame nearest the hero
            CHECK(std::fabs(std::fabs(a[i][12] - a[i ^ 1][12]) - 7 * px) < 1e-2f && std::fabs(std::fabs(a[i][14] - a[i ^ 2][14]) - 6 * pz) < 1e-2f);
            const float cx = 0.5f * (a[i][12] + a[i ^ 1][12]), cz = 0.5f * (a[i][14] + a[i ^ 2][14]);
            CHECK(std::fabs(hero[0] - cx) <= 0.5f * px + 1e-3f && std::fabs(hero[2] - cz) <= 0.5f * pz + 1e-3f);
            CHECK(std::fabs(cx / px - std::floor(cx / px + 0.5f)) < 1e-3f && std::fabs(cz / pz - std::floor(cz / pz + 0.5f)) < 1e-3f);   // on whole frames
            // walking inside a frame: the beams stay; a frame on: they move by exactly one
            const float h2[3] = {hero[0] + 0.3f * px, hero[1], hero[2] + 0.1f * pz};
            BeamMatrix(h2, 2.0f, 15.0f, px, pz, 7, 6, bottom, i, true, b);
            CHECK(std::floor(hero[0] / px + 0.5f) == std::floor(h2[0] / px + 0.5f) && std::floor(hero[2] / pz + 0.5f) == std::floor(h2[2] / pz + 0.5f));
            CHECK(b[12] == a[i][12] && b[14] == a[i][14]);
            const float h3[3] = {hero[0] + px, hero[1], hero[2] - pz};
            BeamMatrix(h3, 2.0f, 15.0f, px, pz, 7, 6, bottom, i, true, b);
            CHECK(std::fabs(b[12] - a[i][12] - px) < 1e-2f && std::fabs(b[14] - a[i][14] + pz) < 1e-2f);
        }
        BeamMatrix(hero, 2.0f, 15.0f, px, pz, 7, 6, bottom, 0, false, b);
        CHECK(b[0] < 0.01f && b[13] < hero[1] - 1000.0f);   // put away: small, under the ground
        // its unit: one root and a model as the game wants, our grid's path, the frame's own maps; a path of its own
        const std::string unit = BeamUnitJson();
        CHECK(skydome::UnitFileProblem(unit) == nullptr);
        CHECK(unit.find(ModelPath(kBeamModelFile)) != std::string::npos && unit.find("act2_town_shops_wood01_NRM.texture") != std::string::npos);
        CHECK(std::string(kBeamUnit) != kUnit && std::string(kBeamUnit) != kWallUnit);
        for (int k = 0; k < kKindCount; ++k) {
            CHECK(std::string(kKinds[k].slab) != kBeamModelFile && (!kKinds[k].wall || std::string(kKinds[k].wall) != kBeamModelFile));
            CHECK(UnitPath(k, false) != kBeamUnit && UnitPath(k, true) != kBeamUnit);
        }
        CHECK(std::string(kBeamModelFile).find("Barbarian_ScaleRef") == std::string::npos && std::string(kBeamModelFile).find("test_cube_02") == std::string::npos);
    }

    // ---- [ceiling] game_ceiling_dome: the cathedral's dome over its chapel (ceiling_measures.h Dome, game_ceiling.h, VaultMesh) ----
    // A preset as the cathedral's (cathy3: the canopy baldachin01 at 146.5 123.5, its bounds -23.36 .. 0 across, 1.03 .. 36.49 up, -10.26 .. 0
    // along; the pillars of the chapel's crossing at 102.5 / 182.5 - one of them a cap - more pillars farther out, a column nearer, the
    // vis-box blocker 60 high) - the dome stands on the crossing's four pillars and passes over the canopy.
    struct Model { const char* path; float mn[3], mx[3]; };
    const Model models[] = {
        {"data/hd/env/model/act1/cathedrl/props/baldachin01.model", {-23.3592f, 1.03048f, -10.2644f}, {0.0f, 36.4938f, 0.0f}},
        {"data/hd/env/model/act1/cathedrl/walls/pillar01.model", {-2.5f, 0.1f, -2.5f}, {2.5f, 19.48f, 2.5f}},
        {"data/hd/env/model/act1/cathedrl/walls/pillar_cap01.model", {-2.5f, 7.41f, -2.5f}, {2.5f, 19.5f, 2.5f}},
        {"data/hd/env/model/act1/cathedrl/props/column_large01.model", {-4.0f, 0.0f, -4.0f}, {4.0f, 19.69f, 4.0f}},
        {"data/hd/env/model/act1/cathedrl/walls/vault01.model", {-5.0f, 0.0f, -2.5f}, {5.0f, 11.32f, 2.5f}},
        {"data/hd/env/light/vis_box/blocker/act1_cathedral_blocker01.model", {0.0f, 0.0f, 0.0f}, {1.0f, 60.0f, 1.0f}},
    };
    auto boundsOf = [&](const std::string& model, float mn[3], float mx[3]) {
        for (const Model& m : models)
            if (model == m.path) { std::memcpy(mn, m.mn, 12); std::memcpy(mx, m.mx, 12); return true; }
        return false;
    };
    auto place = [](const char* model, float x, float z) { ceilmeasure::Placement p; p.model = model; p.x = x; p.z = z; return p; };
    std::vector<ceilmeasure::Placement> cath = {
        place(models[5].path, 142.0f, 340.8f), place(models[1].path, 42.5f, 42.5f), place(models[1].path, 282.5f, 182.5f),
        place(models[1].path, 102.5f, 102.5f), place(models[1].path, 182.5f, 102.5f), place(models[1].path, 102.5f, 182.5f),
        place(models[2].path, 182.5f, 182.5f), place(models[3].path, 195.0f, 115.0f), place(models[3].path, 85.0f, 165.0f),
        place(models[0].path, 146.5f, 123.5f), place(models[4].path, 0.0f, 0.0f), place(models[4].path, 284.0f, 344.0f),
    };
    ceilmeasure::Dome dome;
    std::string why;
    CHECK(ceilmeasure::DomeOf(cath, boundsOf, &dome, &why));
    const float reach = std::hypot(146.5f - 23.3592f - 142.5f, 123.5f - 10.2644f - 142.5f);   // the canopy's corner farthest from the middle
    CHECK(dome.anchor[0] == 146.5f && dome.anchor[1] == 0.0f && dome.anchor[2] == 123.5f);     // the highest piece drawn (not the blocker)
    CHECK(dome.witness[0] == 102.5f && dome.witness[2] == 102.5f);                              // a pillar of the crossing
    CHECK(dome.centreX == 142.5f && dome.centreZ == 142.5f && std::fabs(dome.radius - 40.0f * std::sqrt(2.0f)) < 1e-3f);   // through its four pillars
    CHECK(std::fabs(dome.top - 36.4938f) < 1e-4f && std::fabs(dome.reach - reach) < 1e-3f && dome.springs == 19.48f);
    CHECK(std::fabs(dome.rise - (36.4938f - 19.48f) / std::sqrt(1.0f - (reach / dome.radius) * (reach / dome.radius))) < 1e-3f);
    CHECK(std::fabs(dome.half - 201.5f) < 1e-3f);   // the preset's reach round the middle (to 344 along)
    {
        // the dome over the canopy: wherever its footprint reaches, the dome over the pillars' top is at least as high as the canopy
        const double R = dome.radius, k = dome.reach / R;
        CHECK(dome.rise * std::sqrt(1.0 - k * k) >= dome.top - dome.springs - 1e-3);
        // no dome: a quarter with no pillar; the canopy lower than the pillars; or reaching out past them
        std::vector<ceilmeasure::Placement> p = cath;
        p.erase(p.begin() + 6);   // the cap at 182.5 182.5: the nearest in that quarter is now 282.5 182.5 - still four
        CHECK(ceilmeasure::DomeOf(p, boundsOf, &dome, &why) && std::fabs(dome.centreX - 167.5f) < 1e-3f);
        p = cath;
        p.erase(p.begin() + 5);   // 102.5 182.5: no pillar in that quarter
        CHECK(!ceilmeasure::DomeOf(p, boundsOf, &dome, &why) && why.find("quarter") != std::string::npos);
        p = cath;
        p[9].scaleY = 0.5f;   // the canopy 18.25 high: the columns (19.69) the highest now - under twice the room (2 x 11.32)
        CHECK(!ceilmeasure::DomeOf(p, boundsOf, &dome, &why, ceilmeasure::kTallFactor * 11.32f));
        p.erase(p.begin() + 7, p.begin() + 9);   // and without them: a pillar's cap the highest - the same
        CHECK(!ceilmeasure::DomeOf(p, boundsOf, &dome, &why, ceilmeasure::kTallFactor * 11.32f));
        p = cath;
        p[9].scaleX = 3.0f;   // 70 wide: out past the crossing's pillars - the next ring past its footprint, wider, its rim out past it
        CHECK(ceilmeasure::DomeOf(p, boundsOf, &dome, &why) && dome.reach < dome.radius && dome.radius > 40.0f * std::sqrt(2.0f) + 1.0f);
        // [ceiling] game_ceiling_domes_over_tall: the highest piece at least twice the room (the cathedral: 2 x 11.32 = 22.64 < 36.49)
        CHECK(ceilmeasure::DomeOf(cath, boundsOf, &dome, &why, ceilmeasure::kTallFactor * 11.32f));
        CHECK(!ceilmeasure::DomeOf(cath, boundsOf, &dome, &why, 40.0f) && why.find("twice") != std::string::npos);
        {   // over the piece itself (Serg on 132: "над кристаллом"): its middle the canopy's footprint's (123.14 .. 146.5, 113.24 .. 123.5),
            // the rim through the farthest of the same four pillars, still out past the footprint
            ceilmeasure::Dome on;
            CHECK(ceilmeasure::DomeOf(cath, boundsOf, &on, &why, 0.0f, true));
            const float mx = 0.5f * (146.5f - 23.3592f + 146.5f), mz = 0.5f * (123.5f - 10.2644f + 123.5f);
            CHECK(std::fabs(on.centreX - mx) < 1e-3f && std::fabs(on.centreZ - mz) < 1e-3f && on.reach < on.radius);
            CHECK(std::fabs(on.radius - std::hypot(182.5f - mx, 182.5f - mz)) < 1e-3f);   // (the crossing's far corner)
            CHECK(on.rise > 0.0f && on.rise * std::sqrt(1.0 - (double)(on.reach / on.radius) * (on.reach / on.radius)) >= on.top - on.springs - 1e-3);
        }
        CHECK(ceilmeasure::DomeOf(cath, boundsOf, &dome, &why));   // (the cathedral's, on the crossing, again)
        // a pillar inside the footprint does not count, nor a footing under the floor (the Worldstone's, 84 x 64, has both near it)
        p = cath;
        p.push_back(place(models[1].path, 140.0f, 120.0f));   // in the canopy's footprint: the quarter's nearest past it stays the crossing's
        CHECK(ceilmeasure::DomeOf(p, boundsOf, &dome, &why) && dome.centreX == 142.5f && dome.centreZ == 142.5f);
        p = cath;
        p.push_back(place(models[1].path, 130.0f, 135.0f));
        p.back().y = -20.0f;   // a footing: its top under kWallMinHeight
        CHECK(ceilmeasure::DomeOf(p, boundsOf, &dome, &why) && dome.centreX == 142.5f && dome.centreZ == 142.5f);
        p = cath;
        p[9].q[1] = std::sqrt(0.5f); p[9].q[3] = std::sqrt(0.5f);   // turned a quarter round y (x' = z, z' = -x): -10.26 .. 0 across, 0 .. 23.36 along
        CHECK(ceilmeasure::DomeOf(p, boundsOf, &dome, &why));
        float turned = 0.0f;
        for (const float x : {146.5f - 10.2644f, 146.5f})
            for (const float z : {123.5f, 123.5f + 23.3592f}) turned = std::fmax(turned, std::hypot(x - 142.5f, z - 142.5f));
        CHECK(std::fabs(dome.reach - turned) < 1e-3f && dome.centreX == 142.5f);
        CHECK(ceilmeasure::RiseOver(0.0f, 1.0f, 2.0f) == 0.0f && ceilmeasure::RiseOver(1.0f, 2.0f, 2.0f) == 0.0f && ceilmeasure::RiseOver(3.0f, 0.0f, 2.0f) == 3.0f);
        CHECK(ceilmeasure::IsPillar("data/x/PILLAR_cap01.model") && !ceilmeasure::IsPillar("data/pillars/column01.model"));
        CHECK(ceilmeasure::DomeOf(cath, boundsOf, &dome, &why));   // (the cathedral's again)
    }
    // the file: the dome written and read back; one that could not stand taken as none
    {
        ceilmeasure::Measures m, back;
        m.walls = {{"act1_cathedral", 11.32f}};
        m.domes = {dome};
        m.domes[0].biome = "act1_cathedral";
        m.domes[0].indices = 255792u;
        ceilmeasure::Dome tall = dome;   // a dome over a tall piece in another room, its own slot
        tall.biome = "expansion_baallair_throneroom";
        tall.slot = 2;
        tall.indices = 1000u;
        m.domes.push_back(tall);
        CHECK(ceilmeasure::Parse(ceilmeasure::Format(m), &back) && back.domes.size() == 2 && back.domes[0].biome == "act1_cathedral" &&
              back.domes[0].indices == 255792u && back.domes[0].slot == 0 && back.domes[1].slot == 2 && back.domes[1].biome == tall.biome);
        const ceilmeasure::Dome& b0 = back.domes[0];
        CHECK(b0.anchor[0] == 146.5f && b0.anchor[2] == 123.5f && b0.witness[0] == 102.5f && b0.centreZ == 142.5f);
        CHECK(std::fabs(b0.radius - dome.radius) < 1e-3f && std::fabs(b0.rise - dome.rise) < 1e-3f && std::fabs(b0.reach - dome.reach) < 1e-3f);
        CHECK(std::fabs(b0.half - dome.half) < 1e-3f && b0.top == dome.top && b0.springs == dome.springs);
        // an area's domes by the keys: the cathedral's (slot 0) by game_ceiling_dome, the others by game_ceiling_domes_over_tall
        CHECK(ceilmeasure::DomesOf(back, "act1_cathedral", true, false).size() == 1 && ceilmeasure::DomesOf(back, "act1_cathedral", false, true).empty());
        CHECK(ceilmeasure::DomesOf(back, tall.biome, true, true).size() == 1 && ceilmeasure::DomesOf(back, tall.biome, true, false).empty());
        ceilmeasure::AsWritten(&m.domes[0]);   // as written: read back the very same numbers (the vault made from either is the same file)
        CHECK(ceilmeasure::Parse(ceilmeasure::Format(m), &back) && back.domes[0].radius == m.domes[0].radius && back.domes[0].rise == m.domes[0].rise &&
              back.domes[0].reach == m.domes[0].reach && back.domes[0].top == m.domes[0].top && back.domes[0].centreX == m.domes[0].centreX &&
              m.domes[0].radius != dome.radius);
        m.domes[0].reach = m.domes[0].radius + 1.0f;
        CHECK(ceilmeasure::Parse(ceilmeasure::Format(m), &back) && back.domes.size() == 1 && back.domes[0].slot == 2 && back.walls.size() == 1);
        CHECK(std::string(ceilmeasure::kCathedral) == kVaultBiome && KindOf(kVaultBiome) >= 0);
        // the vaults: four slots, the first the cathedral's as it was, each its own model and unit; a unit names its room's stone
        CHECK(kVaultSlots == 4 && std::string(kVaultModelFiles[0]) == "test_cloth/cloth01" && std::string(kVaultUnit) == kVaultUnits[0]);
        for (int a = 0; a < kVaultSlots; ++a)
            for (int b = a + 1; b < kVaultSlots; ++b) CHECK(ModelPath(kVaultModelFiles[a]) != ModelPath(kVaultModelFiles[b]) && std::string(kVaultUnits[a]) != kVaultUnits[b]);
        CHECK(VaultUnitJson(0, kVaultBiome) == VaultUnitJson());
        const std::string thr = VaultUnitJson(2, "expansion_baallair_throneroom");
        CHECK(thr.find("expansion_baallair_floor01_ALB.texture") != std::string::npos && thr.find("d2r_vr_tall_vault2") != std::string::npos &&
              thr.find("expansion_wildtemple_cages/cage05.model") != std::string::npos);
    }
    // the game's own pieces: the canopy at its place in a preset that also has the pillar where it should be, the two as far apart drawn
    {
        const float A[3] = {146.5f, 0.0f, 123.5f}, P[3] = {102.5f, 0.0f, 102.5f};
        auto piece = [](uint32_t parent, const float l[3], float wx, float wy, float wz) {
            ceilmeasure::Piece p; p.parent = parent; std::memcpy(p.local, l, 12); p.world[0] = wx; p.world[1] = wy; p.world[2] = wz; return p;
        };
        const float o[3] = {1000.25f, 3.5f, -2000.75f};   // the preset's root as drawn
        std::vector<ceilmeasure::Piece> pieces = {
            piece(9, A, 5.0f, 0.0f, 5.0f),                                                          // another preset's piece at that place, no pillar
            piece(11, A, 0.0f, 0.0f, 0.0f), piece(11, P, 1.0f, 0.0f, 1.0f),                          // both, but not as far apart (turned)
            piece(7, P, o[0] + P[0], o[1] + P[1], o[2] + P[2]), piece(7, A, o[0] + A[0], o[1] + A[1], o[2] + A[2]),   // the one
        };
        float world[3] = {};
        CHECK(ceilmeasure::MatchPreset(pieces, A, P, world) == 1 && world[0] == o[0] + A[0] && world[1] == o[1] && world[2] == o[2] + A[2]);
        pieces.push_back(piece(8, A, 10.0f, 0.0f, 10.0f));
        pieces.push_back(piece(8, P, 10.0f + P[0] - A[0], 0.0f, 10.0f + P[2] - A[2]));
        CHECK(ceilmeasure::MatchPreset(pieces, A, P, world) == 2);   // two such presets: which one is not known
        CHECK(ceilmeasure::MatchPreset({}, A, P, world) == 0);
    }
    // the vault: the slab over the preset with the dome in its middle - its rim on a ring of vertices, flat outside, the dome's height
    // inside, normals down (in, under the dome); laid world-fixed, its flat at the ceiling, the dome raised to pass over the canopy
    const grannyremesh::Mesh vault = grannyremesh::VaultMesh(kTile, VaultHalf(dome.half), kTile / kVaultCells, dome.radius, dome.rise, 2.0f * kTile / kVaultCells);
    {
        CHECK(!vault.v.empty() && vault.v.size() <= 65536u && vault.idx.size() % 12 == 0);
        const size_t row = (size_t)std::lround(std::sqrt((double)vault.v.size()));
        CHECK(row * row == vault.v.size() && vault.idx.size() == 12u * (row - 1) * (row - 1));
        bool inRange = true;
        for (const uint16_t i : vault.idx) inRange = inRange && i < vault.v.size();
        CHECK(inRange);
        CHECK(vault.mx[0] >= VaultHalf(dome.half) && -vault.mn[0] >= VaultHalf(dome.half) && vault.mx[2] >= VaultHalf(dome.half));   // over the whole preset
        CHECK(std::fabs(vault.mx[1] - dome.rise) < 1e-3f && std::fabs(vault.mn[1]) < 1e-6f);
        const double R = dome.radius, H = dome.rise, q = vault.scale / 32767.0;
        int rim = 0, inside = 0, badHeight = 0, badNormal = 0, flatBad = 0;
        for (const grannyremesh::Vertex& v : vault.v) {
            const double x = v.pos[0] * q, y = v.pos[1] * q, z = v.pos[2] * q, r = std::sqrt(x * x + z * z);
            const double want = r < R ? H * std::sqrt(1.0 - (r / R) * (r / R)) : 0.0;
            if (std::fabs(y - want) > 0.05 + (r > R - 4.0 && r < R ? H * 0.02 : 0.0)) ++badHeight;   // (steep at the rim: its quantised place)
            if (v.normal[1] > 0) ++badNormal;                                                          // never up
            if (std::fabs(r - R) < 0.02) ++rim;
            if (r < R - 0.02) ++inside;
            if (r > R + 0.02 && (v.normal[0] != 0 || v.normal[1] != -127 || v.normal[2] != 0 || v.pos[1] != 0)) ++flatBad;
            CHECK(std::fabs(Unhalf(v.uv[0]) * kTile - x) < kTile / 128.0 && std::fabs(-Unhalf(v.uv[1]) * kTile - z) < kTile / 128.0);   // (a half's step at 8 .. 16 tiles)
        }
        CHECK(badHeight == 0 && badNormal == 0 && flatBad == 0);
        const long ringsIn = std::lround(dome.radius / (kTile / kVaultCells));
        CHECK(rim == 8 * ringsIn && inside == (2 * ringsIn - 1) * (2 * ringsIn - 1));   // a whole ring on the rim; the dome's rings inside it
        std::printf("  the vault: %zu vertices, %zu triangles, %.0f units round its middle; the dome %.1f round rising %.1f (%ld rings to the rim)\n",
                    vault.v.size(), vault.idx.size() / 6, vault.mx[0], dome.radius, dome.rise, ringsIn);
        CHECK(grannyremesh::VaultMesh(kTile, 2000.0f, kTile / kVaultCells, dome.radius, dome.rise).v.empty());   // more than one model holds
        CHECK(grannyremesh::VaultMesh(kTile, 240.0f, 3.3f, dome.radius, 0.0f).v.empty());
        CHECK(VaultHalf(10.0f) == kTile * kTilesHalf / kDomeSizeMin && std::fabs(VaultHalf(201.5f) - 241.5f / kDomeSizeMin) < 1e-3f);
        // made wide enough for the smallest size, its far part in cells twice as wide: fewer vertices than all fine
        const long fine = (long)std::ceil(VaultHalf(dome.half) / (dome.radius / ringsIn));
        CHECK((long)vault.v.size() < (2 * fine + 1) * (2 * fine + 1));
        // [ceiling] game_ceiling_dome_size / _rise (Serg on 0.189): in range, never with its rim on the canopy's footprint; laid at the
        // smallest it still covers the cathedral and passes over the canopy
        CHECK(DomeSize(1.0f, dome.reach, dome.radius) == 1.0f && DomeSize(9.0f, dome.reach, dome.radius) == kDomeSizeMax);
        CHECK(DomeSize(0.1f, dome.reach, dome.radius) == kDomeSizeMin && DomeSize(0.7f, 50.0f, 56.57f) > 0.9f);   // (a canopy reaching 50: its rim kept out past it)
        CHECK(DomeRiseScale(0.5f) == 1.0f && DomeRiseScale(1.5f) == 1.5f && DomeRiseScale(5.0f) == kDomeRiseMax);
        CHECK(VaultHalf(dome.half) * kDomeSizeMin >= dome.half + kTile - 1e-3f);
        for (const float size : {kDomeSizeMin, 1.0f, kDomeSizeMax}) {
            const float rs = VaultRise(3.5f, dome.top, 3.5f + 23.0f, dome.reach, dome.radius * size);
            const double k = dome.reach / (dome.radius * size);
            CHECK(rs > 0.0f && rs * std::sqrt(1.0 - k * k) >= dome.top - 23.0f - 1e-3);   // over the canopy's whole footprint
        }
        // its rise now: over a ceiling at 23 on a floor drawn 3.5 up, the canopy's top as drawn
        const float r23 = VaultRise(3.5f, dome.top, 3.5f + 23.0f, dome.reach, dome.radius);
        CHECK(std::fabs(r23 - ceilmeasure::RiseOver(dome.top - 23.0f, dome.reach, dome.radius)) < 1e-4f && r23 > 0.0f && r23 < dome.rise);
        CHECK(std::fabs(VaultRise(0.0f, dome.top, dome.springs, dome.reach, dome.radius) - dome.rise) < 1e-3f);   // as made over the pillars' top
        CHECK(VaultRise(0.0f, dome.top, 40.0f, dome.reach, dome.radius) == 0.0f);   // a ceiling over the canopy: no dome
        float m[16];
        const float hero[3] = {1100.0f, 3.5f, -1900.0f};
        VaultMatrix(hero, 1142.5f, -1881.5f, 26.5f, r23, dome.rise, 1.0f, true, m);
        CHECK(m[12] == 1142.5f && m[13] == 26.5f && m[14] == -1881.5f && m[0] == 1.0f && m[10] == 1.0f && std::fabs(m[5] - r23 / dome.rise) < 1e-6f);
        VaultMatrix(hero, 1142.5f, -1881.5f, 26.5f, r23, dome.rise, 1.25f, true, m);   // a size: round the same middle, the rise as given
        CHECK(m[12] == 1142.5f && m[14] == -1881.5f && m[0] == 1.25f && m[10] == 1.25f && std::fabs(m[5] - r23 / dome.rise) < 1e-6f);
        VaultMatrix(hero, 1142.5f, -1881.5f, 26.5f, 0.0f, dome.rise, 1.0f, true, m);
        CHECK(m[0] < 0.01f && m[13] < hero[1] - 1000.0f);   // no rise: put away
        VaultMatrix(hero, 1142.5f, -1881.5f, 26.5f, r23, dome.rise, 1.0f, false, m);
        CHECK(m[0] < 0.01f && m[13] < hero[1] - 1000.0f);
        // its unit: one root and a model as the game wants, its own test path, the cathedral's stone
        const std::string unit = VaultUnitJson();
        CHECK(skydome::UnitFileProblem(unit) == nullptr);
        CHECK(unit.find(ModelPath(kVaultModelFile)) != std::string::npos && unit.find("act2_arcane_grunge02_ALB.texture") != std::string::npos);
        CHECK(std::string(kVaultUnit) != kUnit && std::string(kVaultUnit) != kWallUnit && std::string(kVaultUnit) != kBeamUnit);
        for (int k = 0; k < kKindCount; ++k) {
            CHECK(std::string(kKinds[k].slab) != kVaultModelFile && (!kKinds[k].wall || std::string(kKinds[k].wall) != kVaultModelFile));
            CHECK(UnitPath(k, false) != kVaultUnit && UnitPath(k, true) != kVaultUnit);
        }
        CHECK(std::string(kVaultModelFile) != kBeamModelFile && std::string(kVaultModelFile).find("driver_mesh_test01") == std::string::npos);
    }
    if (argc > 3) {   // the game's own frame (wood_ceiling_frame01_lod0.model): its mesh read, laid in the grid, written into its own file
        std::string frame, why;
        CHECK(ReadAll(argv[3], &frame));
        grannyremesh::File f;
        grannyremesh::Mesh one, grid, back;
        float mn[3], mx[3];
        CHECK(grannyremesh::Read(frame, &f, &why) && grannyremesh::MeshOf(f, &one, &why) && grannyremesh::Bounds(f, mn, mx));
        std::printf("  the frame: %zu vertices, %zu triangles, %.2f x %.2f, y %.3f .. %.3f%s\n", one.v.size(), one.idx.size() / 3,
                    one.mx[0] - one.mn[0], one.mx[2] - one.mn[2], one.mn[1], one.mx[1], why.c_str());
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};   // its vertices where its bounds say
        for (const grannyremesh::Vertex& v : one.v)
            for (int k = 0; k < 3; ++k) {
                const float p = v.pos[k] / 32767.0f * one.scale;
                lo[k] = p < lo[k] ? p : lo[k];
                hi[k] = p > hi[k] ? p : hi[k];
            }
        for (int k = 0; k < 3; ++k) CHECK(std::fabs(lo[k] - one.mn[k]) < 0.01f && std::fabs(hi[k] - one.mx[k]) < 0.01f && mn[k] == one.mn[k]);
        int cols = 0, rows = 0;
        const float px = one.mx[0] - one.mn[0], pz = one.mx[2] - one.mn[2];
        CHECK(ceilmeasure::BeamGrid((unsigned)one.v.size(), px, pz, &cols, &rows));
        CHECK(grannyremesh::GridMesh(one, cols, rows, px, pz, &grid) && grid.v.size() == one.v.size() * cols * rows && grid.idx.size() == one.idx.size() * cols * rows);
        CHECK(std::fabs(grid.mx[0] - grid.mn[0] - cols * px) < 0.02f && std::fabs(grid.mx[2] - grid.mn[2] - rows * pz) < 0.02f);   // whole frames side by side
        CHECK(std::fabs(grid.mn[1] - one.mn[1]) < 0.01f && std::fabs(grid.mx[1] - one.mx[1]) < 0.01f);   // at the frame's own height
        CHECK(!grannyremesh::GridMesh(one, cols + 1, rows + 1, px, pz, &back));   // more than 16-bit indices hold
        grannyremesh::Done done;
        CHECK(grannyremesh::Remesh(f, grid, {}, 0, &done, &why) && done.vertices == grid.v.size() && done.textureNames == 0);
        const std::string out = grannyremesh::Write(f);
        grannyremesh::File g;
        CHECK(grannyremesh::Read(out, &g, &why) && grannyremesh::MeshOf(g, &back, &why) && back.v.size() == grid.v.size() && back.idx == grid.idx);
        CHECK(out.find("act2_town_shops_wood01_ALB.texture") != std::string::npos && out.find("StandardMaterial") != std::string::npos);   // its own wood
        std::printf("  the frame laid %d x %d in its own file: %zu vertices, %zu triangles, %.1f x %.1f, %zu bytes\n", cols, rows, back.v.size(),
                    back.idx.size() / 3, back.mx[0] - back.mn[0], back.mx[2] - back.mn[2], out.size());
    }
    if (argc > 1) {
        std::string rock;
        CHECK(ReadAll(argv[1], &rock));
        grannyremesh::File f;
        std::string why;
        CHECK(grannyremesh::Read(rock, &f, &why));
        if (!why.empty()) std::printf("  read: %s\n", why.c_str());
        const std::vector<uint8_t> types = f.sec.size() > 6 ? f.sec[6] : std::vector<uint8_t>();
        grannyremesh::Renames maps;
        for (const Map& m : kMaps) maps.push_back({m.from, m.to});
        grannyremesh::Done done;
        CHECK(grannyremesh::Remesh(f, mesh, maps, 3, &done, &why));
        if (!why.empty()) std::printf("  remesh: %s\n", why.c_str());
        std::printf("  the rock made our slab: %u vertices, %u triangles, %u texture name(s), %u bone(s)\n", done.vertices, done.triangles, done.textureNames, done.bones);
        CHECK(done.vertices == mesh.v.size() && done.triangles * 3 == kIndexCount && done.textureNames >= 3);
        const std::string out = grannyremesh::Write(f);
        if (argc > 2)
            if (FILE* o = std::fopen(argv[2], "wb")) { std::fwrite(out.data(), 1, out.size(), o); std::fclose(o); }
        grannyremesh::File g;
        CHECK(grannyremesh::Read(out, &g, &why));
        CHECK(g.sec.size() == f.sec.size() && g.sec.size() > 6 && g.sec[6] == types);   // the types: the game's, byte for byte
        grannyremesh::Walk w(g);
        const grannyremesh::Addr rt{g.h.rootType.section, g.h.rootType.offset}, ro{g.h.rootObj.section, g.h.rootObj.offset};
        grannyremesh::Addr t;
        const grannyremesh::Addr meshes = w.Member(rt, ro, "Meshes", &t);
        const grannyremesh::Addr meshType = w.Ref(t), m0 = w.Ptr(w.Ptr(meshes.at(4)));
        const grannyremesh::Addr pvd = w.Ptr(w.Member(meshType, m0, "PrimaryVertexData", &t));
        const grannyremesh::Addr verts = w.Member(w.Ref(t), pvd, "Vertices", &t);
        CHECK(w.Get<int32_t>(verts.at(8)) == (int32_t)mesh.v.size());
        const grannyremesh::Addr vd = w.Ptr(verts.at(12));
        CHECK(w.In(vd, (uint32_t)(mesh.v.size() * 20)) && std::memcmp(&g.sec[vd.s][vd.o], mesh.v.data(), mesh.v.size() * 20) == 0);
        const grannyremesh::Addr topo = w.Ptr(w.Member(meshType, m0, "PrimaryTopology", &t));
        grannyremesh::Addr e;
        CHECK(w.Get<int32_t>(w.Member(w.Ref(t), topo, "Indices16", &e)) == (int32_t)kIndexCount);
        int ours = 0, theirs = 0;
        for (size_t s = 0; s < g.fix.size(); ++s)
            for (const grannyremesh::Fixup& x : g.fix[s]) {
                const std::string str = w.Str(grannyremesh::Addr{x.ptr.section, x.ptr.offset});
                for (const Map& m : kMaps) { ours += str == m.to; theirs += str == m.from; }
            }
        CHECK(ours == (int)done.textureNames && theirs == 0);
        CHECK(out.find("StandardMaterial") != std::string::npos);   // lit, as the rock was
        std::printf("  written: %zu bytes, %zu sections, read back the same\n", out.size(), g.sec.size());
        grannyremesh::File fw;   // the wall from the same rock
        CHECK(grannyremesh::Read(rock, &fw, &why) && grannyremesh::Remesh(fw, wall, maps, 0, &done, &why));
        CHECK(done.vertices == wall.v.size() && done.triangles * 3 == kWallIndexCount);
        grannyremesh::File gw;
        CHECK(grannyremesh::Read(grannyremesh::Write(fw), &gw, &why) && gw.sec[6] == types);
        std::printf("  the rock made our wall: %u vertices, %u triangles\n", done.vertices, done.triangles);
        grannyremesh::File fv;   // [ceiling] game_ceiling_dome: the cathedral's vault from the same rock, the cathedral's stone
        const Kind& stone = kKinds[KindOf(kVaultBiome)];
        const grannyremesh::Renames stoneMaps = {{kMaps[0].from, MapPath(stone, "ALB")}, {kMaps[1].from, MapPath(stone, "NRM")}, {kMaps[2].from, MapPath(stone, "ORM")}};
        CHECK(grannyremesh::Read(rock, &fv, &why) && grannyremesh::Remesh(fv, vault, stoneMaps, 0, &done, &why));
        CHECK(done.vertices == vault.v.size() && done.triangles * 3 == vault.idx.size() && done.textureNames >= 3);
        const std::string vout = grannyremesh::Write(fv);
        grannyremesh::File gv;
        grannyremesh::Mesh vback;
        CHECK(grannyremesh::Read(vout, &gv, &why) && gv.sec[6] == types && grannyremesh::MeshOf(gv, &vback, &why) && vback.idx == vault.idx);
        CHECK(vout.find("act2_arcane_grunge02_NRM.texture") != std::string::npos && vout.find("StandardMaterial") != std::string::npos);
        std::printf("  the rock made the cathedral's vault: %u vertices, %u triangles, %zu bytes\n", done.vertices, done.triangles, vout.size());
    }
    if (g_fail) { std::printf("game_ceiling_test: %d FAILED\n", g_fail); return 1; }
    std::printf("game_ceiling_test: shown only where wanted, laid on whole tiles at its height, the slab the same in every tile, its unit as the "
                "game wants, the walls' top, the barracks' beams on whole frames, the cathedral's dome on its pillars%s: all passed\n",
                argc > 1 ? ", the game's rock made our slab and vault" : " (no rock model given)");
    return 0;
}
