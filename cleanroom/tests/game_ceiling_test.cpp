// game_ceiling_test - shared/game_ceiling.h and granny_remesh.h's CeilingMesh: [ceiling] game_ceiling's slab of the game's own lit rock.
//   1. shown only ticked, in VR F4, where a ceiling is wanted, the hero known - each missing one named;
//   2. its height (game_ceiling_height, else the area's), a tile's span (4 of [ceiling] scale's), the area's floor (the lowest stood since
//      the area changed); its matrix: its top over the floor, its middle on the whole tile nearest the hero, put away small under him;
//   3. the slab: the index count vrcam expects, 16-bit indices in range, its normals down, its rock 0 .. kRelief down, and every tile the
//      same (a vertex and the one a tile on: the same height, normal, tangent, uv one more) - moved by whole tiles it shows no step;
//   4. its unit: one root and a model as the game wants (vr/sky_dome.h's check), naming our model's path and the floor's three maps;
//   game_ceiling_test [<the game's rocks03_lod1.model> [<out .model>]]: the rock made our slab, written, read again - our vertices and
//   indices, its three maps renamed, the old names gone, the types' section the game's byte for byte, still its StandardMaterial.
#include "game_ceiling.h"
#include "granny_remesh.h"
#include "sky_dome.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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
    CHECK(Height(0.0f, 23.0f) == 23.0f && Height(30.5f, 23.0f) == 30.5f);
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
                  p.find("test_resolution") == std::string::npos && p.find("test_cloth") == std::string::npos);   // (the dome's; a prop palette's; cloth)
        for (int k = 0; k < kKindCount; ++k) CHECK((kKinds[k].wall != nullptr) == (kKinds[k].relief == kRelief));   // walls: the caves'
    }
    // matched to the pictures chosen for our vault: the barracks and catacombs share theirs (smooth dark stone), so they share a kind
    CHECK(KindOf("act1_caves") == 0 && KindOf("act1_barracks") == KindOf("act1_catacombs") &&
          std::string(kKinds[KindOf("act1_barracks")].maps).find("grunge01") != std::string::npos);
    CHECK(KindOf("act4_diab") == -1 && KindOf("act1_outdoors") == -1 && KindOf("") == -1);   // (the Chaos Sanctuary keeps its void)
    CHECK(KindOf("act2_sewer") == -1);   // no matching game brick yet: our vault there (the rollout rule - never nothing)
    for (const BiomeKind& b : kBiomeKinds) CHECK(b.kind >= 0 && b.kind < kKindCount && AllBiomes().find(b.biome) != std::string::npos);
    for (const char* room : {"act1_crypt", "act1_barracks", "act1_cathedral", "act1_catacombs", "act2_palace_clean"})   // [ceiling] biomes' rooms done
        CHECK(KindOf(room) >= 0);
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
    }
    if (g_fail) { std::printf("game_ceiling_test: %d FAILED\n", g_fail); return 1; }
    std::printf("game_ceiling_test: shown only where wanted, laid on whole tiles at its height, the slab the same in every tile, its unit as the "
                "game wants%s: all passed\n", argc > 1 ? ", the game's rock made our slab" : " (no rock model given)");
    return 0;
}
