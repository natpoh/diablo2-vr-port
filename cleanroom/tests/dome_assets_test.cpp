// dome_assets_test - D2R_VR_Settings' maker of [sky] game_sky's dome models (tools/settings/dome_assets.cpp), run on the player's own game
// into a scratch mod folder the way kurast_boat_test runs the boat's:
//   dome_assets_test <game folder> <empty scratch folder> [<make_sky_dome.py's D2R-VR.mpq folder: its units and textures compared>]
//   1. not the mod (no folder): nothing made;
//   2. the mod's folder: thirty-five models (acts 1 - 5, the magenta test and the black cave dome, LOD 0 - 4), each the game's card with our dome in it - 1105 vertices,
//      3968 triangles, our texture's name, its LOD - and the receipt; no name of the card's or of another dome's used, every dome's mesh
//      bytes its own (the same triangles), no ceiling model at a dome's path;
//   3. again: nothing written; a model the player changed stays; the first prototype's model (one section, make_sky_dome.py) is replaced;
//   4. [ceiling] game_ceiling's slab (PrepareCeiling): five models from act 1's cave rock with the floor's rock as their maps, our unit, its
//      receipt; again nothing; off: ours out again;
//   5. the barracks' beams (the game's frame in a grid, five LODs, a unit) and the rooms' measures (each area's walls' top, the frame's size);
//      the cathedral's dome (on its crossing's four pillars, over the canopy) and its vault (the cathedral's stone, five LODs, a unit).
#include "ceiling_levels.h"
#include "ceiling_measures.h"
#include "dome_assets.h"
#include "game_ceiling.h"
#include "granny_remesh.h"
#include "sky_atlas.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool ReadAll(const std::wstring& path, std::string* out) {
    out->clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}
static float Unhalf(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = e ? std::ldexp(1.0f + m / 1024.0f, e - 15) : std::ldexp(m / 1024.0f, -14);
    return h & 0x8000 ? -v : v;
}
static void WriteAll(const std::wstring& path, const std::string& data) {
    if (FILE* f = _wfopen(path.c_str(), L"wb")) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("dome_assets_test <game folder> <empty scratch folder>: not given - nothing to do\n"); return 0; }
    std::wstring game = argv[1], scratch = argv[2];
    if (game.back() != L'\\') game += L'\\';
    if (scratch.back() != L'\\') scratch += L'\\';
    using namespace domeassets;
    Result r = Prepare(game, L"", true);
    CHECK(r.state == kNotHere && !r.wrote);
    r = Prepare(game, scratch, true);
    CHECK(r.state == kNotHere);   // (no D2R-VR.mpq in it yet)
    CreateDirectoryW((scratch + L"D2R-VR.mpq").c_str(), nullptr);
    // the acts' painted skies, as the mod's data brings them (here five small BC6H pictures, each its own bytes): the domes' atlas made of them
    std::vector<std::string> actPictures;
    {
        const std::wstring dir = scratch + L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\";
        for (const wchar_t* d : {L"data", L"data\\hd", L"data\\hd\\env", L"data\\hd\\env\\texture", L"data\\hd\\env\\texture\\d2r_vr", L"data\\hd\\env\\texture\\d2r_vr\\sky"})
            CreateDirectoryW((scratch + L"D2R-VR.mpq\\" + d).c_str(), nullptr);
        for (int a = 1; a <= 5; ++a) {
            std::string s(skyatlas::kHeader, '\0');
            std::memcpy(&s[0], "DDS ", 4);
            skyatlas::Put32(&s, 4, 124);
            skyatlas::Put32(&s, 12, 16);
            skyatlas::Put32(&s, 16, 64);
            skyatlas::Put32(&s, 28, 5);
            std::memcpy(&s[84], "DX10", 4);
            skyatlas::Put32(&s, 128, skyatlas::kBC6HUF16);
            skyatlas::Put32(&s, 140, 1);
            for (uint32_t k = 0; k < 5; ++k)
                for (size_t i = 0; i < skyatlas::MipBytes(64, 16, k); ++i) s.push_back((char)(a * 16 + k + (int)(i % 7)));
            WriteAll(dir + L"sky_act" + std::to_wstring(a) + L"_day_HDR.dds", s);
            actPictures.push_back(s);
        }
    }
    r = Prepare(game, scratch, true);
    std::wprintf(L"  made: %ls\n", r.text.c_str());
    CHECK(r.state == kMade && r.wrote);
    {   // the atlas: the five acts' pictures and the cave's black, stacked (shared/sky_atlas.h)
        std::string atlas, want, why;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_domes_HDR.dds", &atlas) && skyatlas::Make(actPictures, &want, &why) &&
              atlas == want);
        CHECK(r.text.find(L"each dome its own band") != std::wstring::npos);
        for (const char* d : {"act1", "act2", "act3", "act4", "act5", "cave", "test"}) {   // the units: a live dome's names the atlas
            std::string unit;
            CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\sky\\sky_dome_" + std::wstring(d, d + std::strlen(d)) + L"_unit.json", &unit));
            CHECK((unit.find(skyatlas::kAtlas) != std::string::npos) == (skyatlas::BandOf(d) >= 0));
        }
    }
    // the game's own developers' test models' paths (it takes a model only from a path it knows)
    const std::wstring mpq = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\";
    auto at = [&](const wchar_t* name, int lod) {
        const std::wstring n = name, l = std::to_wstring(lod) + L".model";
        return n == L"act1"   ? mpq + L"Barbarian_ScaleRef_lod" + l
               : n == L"cave" ? mpq + L"test_cube_02_lod" + l
               : n == L"act2" ? mpq + L"pillar01_lod" + l
               : n == L"act3" ? mpq + L"Pillar_02_lod" + l
               : n == L"act4" ? mpq + L"Pillar_03_lod" + l
               : n == L"act5" ? mpq + L"test_cloth\\flag01_lod" + l
                              : mpq + L"test_cloth\\driver_mesh_test01\\driver_mesh_test01_lod" + l;
    };
    std::map<std::wstring, std::set<std::string>> referenced;   // each dome's (LOD 1) strings its pointers reach, outside the types
    std::map<std::wstring, grannyremesh::Mesh> meshes1;           // ... and its mesh
    for (const wchar_t* name : {L"act1", L"test", L"cave", L"act2", L"act3", L"act4", L"act5"})
        for (int lod = 0; lod < 5; ++lod) {
            std::string data;
            const std::wstring path = at(name, lod);
            CHECK(ReadAll(path, &data));
            grannyremesh::File f;
            std::string why;
            CHECK(grannyremesh::Read(data, &f, &why) && f.sec.size() == 8);
            grannyremesh::Walk w(f);
            const grannyremesh::Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
            grannyremesh::Addr t;
            const grannyremesh::Addr meshes = w.Member(rt, ro, "Meshes", &t);
            const grannyremesh::Addr meshType = w.Ref(t), mesh = w.Ptr(w.Ptr(meshes.at(4)));
            const grannyremesh::Addr pvd = w.Ptr(w.Member(meshType, mesh, "PrimaryVertexData", &t));
            const int32_t verts = w.Get<int32_t>(w.Member(w.Ref(t), pvd, "Vertices").at(8));
            CHECK(verts > 1105 && verts <= 1105 + 16);   // the dome's 1105 and its own few nobody points at (grannyremesh::Salt)
            if (lod == 1) {
                grannyremesh::Mesh m;
                CHECK(grannyremesh::MeshOf(f, &m, &why));
                meshes1[name] = m;
                for (const auto& fx : f.fix)
                    for (const grannyremesh::Fixup& x : fx) {
                        if (x.ptr.section == 6) continue;   // (the types: field names)
                        const std::string str = w.Str(grannyremesh::Addr{x.ptr.section, x.ptr.offset});
                        bool text = str.size() >= 2;
                        for (const char c : str) text = text && c >= 32 && c < 127;
                        if (text) referenced[name].insert(str);
                    }
            }
            std::string tex = "sky_";   // a live dome: the atlas (its own band); the debug dome its own texture: sky_test_day_HDR.dds
            std::string narrow;
            for (const wchar_t* c = name; *c; ++c) narrow += (char)*c;
            tex += narrow + "_day_HDR.dds";
            const int band = skyatlas::BandOf(narrow);
            if (band >= 0) tex = skyatlas::kAtlas;
            if (band >= 0 && lod == 1) {   // its uv inside its own band
                float from = 0.0f, to = 0.0f;
                skyatlas::BandRange(band, &from, &to);
                const grannyremesh::Mesh& m1 = meshes1[name];
                bool inBand = !m1.v.empty();
                for (size_t i = 0; i < 1105 && i < m1.v.size(); ++i) inBand = inBand && Unhalf(m1.v[i].uv[1]) >= from - 1e-3f && Unhalf(m1.v[i].uv[1]) <= to + 1e-3f;
                CHECK(inBand);
            }
            CHECK(data.find(tex) != std::string::npos);   // (BitKnit2 raw quanta: the bytes as they are)
            // its material named its own (the game took the first dome's material for the next one by its name)
            std::string own = "d2rvr_sky_";
            for (const wchar_t* c = name; *c; ++c) own += (char)*c;
            const std::string meshName = own + "_shape";
            own += "_material";
            CHECK(data.find(own) != std::string::npos);
            CHECK(data.find(meshName) != std::string::npos);   // (the mesh, the model, the art file: each dome's own - the game shared act 1's by them)
            grannyremesh::Walk g(f);
            bool cardName = false;   // no pointer of it to the card's own model name any more
            for (const auto& fx : f.fix)
                for (const grannyremesh::Fixup& x : fx) cardName = cardName || g.Str(grannyremesh::Addr{x.ptr.section, x.ptr.offset}) == "skybox01";
            CHECK(!cardName);
            bool named = false;
            for (const auto& fx : f.fix)
                for (const grannyremesh::Fixup& x : fx) named = named || w.Str(grannyremesh::Addr{x.ptr.section, x.ptr.offset}).find(tex) != std::string::npos;
            CHECK(named);
        }
    // Serg on 0.188: every act drew act 2's sky (the first dome loaded). No name that says which model, material, map, mesh, art file or
    // picture it is is shared by any two domes any more - only the Granny / Maya field names every model has - and the meshes' bytes
    // differ too (the same triangles): the game shares nothing between them by what they hold.
    {
        const std::vector<std::string> card = {"skybox01", "skybox0Shape1", "GLSLShader1", "file2", "expansion_mountaintop_skybox",
                                               "mountaintop_skybox01", "X:/assets/", "X:/branches/"};
        for (const auto& a : referenced)
            for (const std::string& str : a.second) {
                bool cardName = false;
                for (const std::string& c : card) cardName = cardName || str.find(c) != std::string::npos;
                if (cardName && str.find("UnlitMaterial.ogsfx") == std::string::npos) { CHECK(!cardName); std::printf("    the card's own name still used: %s\n", str.c_str()); }
                for (const auto& b : referenced) {
                    if (b.first <= a.first || !b.second.count(str)) continue;
                    const bool field = str == skyatlas::kAtlas ||   // (the one picture every live dome names - by design, 0.189)
                                       (str.find("d2rvr_") == std::string::npos && str.find(".dds") == std::string::npos &&
                                        str.find(".hdr") == std::string::npos && str.find(".ma") == std::string::npos);
                    if (!field) { CHECK(field); std::printf("    shared by two domes: %s\n", str.c_str()); }
                }
            }
        for (const auto& a : meshes1)
            for (const auto& b : meshes1) {
                if (b.first <= a.first) continue;
                const bool sameBytes = a.second.v.size() == b.second.v.size() &&
                                       std::memcmp(a.second.v.data(), b.second.v.data(), a.second.v.size() * sizeof(grannyremesh::Vertex)) == 0;
                CHECK(!sameBytes && a.second.idx != b.second.idx);   // its own bytes
                CHECK(a.second.idx.size() == b.second.idx.size() &&     // the same triangles, begun elsewhere
                      std::is_permutation(a.second.idx.begin(), a.second.idx.end(), b.second.idx.begin()));
            }
        CHECK(referenced.size() == 7 && meshes1.size() == 7);
        // no other maker writes where a dome is made (2026-10-29: the jungle's and the ice caves' ceilings were given act 2 - 4's paths)
        const std::vector<std::string> sky = SkyModelPaths();
        for (int k = 0; k < gameceiling::kKindCount; ++k)
            for (const char* file : {gameceiling::kKinds[k].slab, gameceiling::kKinds[k].wall})
                for (int lod = 0; file && lod < 5; ++lod) CHECK(std::find(sky.begin(), sky.end(), gameceiling::LodPath(file, lod)) == sky.end());
        for (const char* file : {gameceiling::kBeamModelFile, gameceiling::kVaultModelFile})
            for (int lod = 0; lod < 5; ++lod) CHECK(std::find(sky.begin(), sky.end(), gameceiling::LodPath(file, lod)) == sky.end());
        CHECK(std::find(sky.begin(), sky.end(), std::string("data/hd/env/model/Tests/pillar01_lod1.model")) != sky.end());   // (the paths as written)
    }
    // the units and the plain textures, made here too (a DLL + Settings install is whole without the staged data)
    for (const wchar_t* f : {L"model\\d2r_vr\\sky\\sky_dome_cave_unit.json", L"model\\d2r_vr\\sky\\sky_dome_act1_unit.json",
                             L"model\\d2r_vr\\sky\\sky_dome_summit_unit.json", L"texture\\d2r_vr\\sky\\sky_cave_day_HDR.dds",
                             L"model\\d2r_vr\\sky\\sky_dome_act2_unit.json", L"model\\d2r_vr\\sky\\sky_dome_act3_unit.json",
                             L"model\\d2r_vr\\sky\\sky_dome_act4_unit.json", L"model\\d2r_vr\\sky\\sky_dome_act5_unit.json",
                             L"texture\\d2r_vr\\sky\\sky_test_day_HDR.dds"}) {
        std::string data;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\" + f, &data) && data.size() > 100);
        if (argc > 3) {   // the same bytes as tools/game_sky/make_sky_dome.py's (its output's D2R-VR.mpq folder given)
            std::string py;
            CHECK(ReadAll(std::wstring(argv[3]) + L"\\data\\hd\\env\\" + f, &py) && py == data);
        }
    }
    std::string receipt;
    CHECK(ReadAll(scratch + L"sky_dome.json", &receipt) && receipt.find("driver_mesh_test01_lod4.model") != std::string::npos &&
          receipt.find("test_cube_02_lod4.model") != std::string::npos);
    r = Prepare(game, scratch, true);
    CHECK(r.state == kMade && !r.wrote);   // nothing to do
    // the player's own change stays; the first prototype's model is replaced
    std::string lod2;
    ReadAll(at(L"act1", 2), &lod2);
    WriteAll(at(L"act1", 2), lod2 + "x");
    std::string proto = "tools/game_sky/make_sky_dome.py";   // (not a Granny file: not taken for the prototype's - replaced only when it reads as one)
    r = Prepare(game, scratch, true);
    std::wprintf(L"  again: %ls\n", r.text.c_str());
    std::string after;
    ReadAll(at(L"act1", 2), &after);
    CHECK(after == lod2 + "x" && r.text.find(L"1 of them") != std::wstring::npos);
    (void)proto;
    // [debug] game_sky_test 4: the magenta dome also at the game's own card path, and out of it again
    const std::wstring summit = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\expansion\\mountaintop\\expansion_mountaintop_skybox\\skybox01_lod1.model";
    r = Prepare(game, scratch, true, true);
    std::wprintf(L"  summit path: %ls\n", r.text.c_str());
    CHECK(r.state == kMade && GetFileAttributesW(summit.c_str()) != INVALID_FILE_ATTRIBUTES);
    r = Prepare(game, scratch, true, false);
    CHECK(r.state == kMade && GetFileAttributesW(summit.c_str()) == INVALID_FILE_ATTRIBUTES);
    // [sky] game_sky_closed: the domes made again as whole spheres (bigger files, the game's card took the mesh), and back to the open dome
    {
        std::string open0, closed0, again0;
        ReadAll(at(L"act3", 0), &open0);
        r = Prepare(game, scratch, true, false, true);
        std::wprintf(L"  closed: %ls\n", r.text.c_str());
        ReadAll(at(L"act3", 0), &closed0);
        CHECK(r.state == kMade && r.wrote && closed0.size() > open0.size());
        r = Prepare(game, scratch, true, false, false);
        ReadAll(at(L"act3", 0), &again0);
        CHECK(r.state == kMade && r.wrote && again0 == open0);
    }
    // unticked: ours out, the player's changed one stays
    r = Prepare(game, scratch, false);
    std::wprintf(L"  off: %ls\n", r.text.c_str());
    CHECK(r.state == kOff && GetFileAttributesW(at(L"act1", 0).c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(at(L"act1", 2).c_str()) != INVALID_FILE_ATTRIBUTES);
    // 4. [ceiling] game_ceiling: the slab from act 1's cave rock - five models and our unit, the receipt; nothing again; off: out
    r = PrepareCeiling(game, L"", true);
    CHECK(r.state == kNotHere && !r.wrote);
    r = PrepareCeiling(game, scratch, true);
    std::wprintf(L"  ceiling: %ls\n", r.text.c_str());
    CHECK(r.state == kMade && r.wrote);
    const std::wstring cliff = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_detailnormal\\cliffside01_lod";
    for (int lod = 0; lod < 5; ++lod) {
        std::string data;
        CHECK(ReadAll(cliff + std::to_wstring(lod) + L".model", &data));
        grannyremesh::File f;
        std::string why;
        CHECK(grannyremesh::Read(data, &f, &why));
        CHECK(data.find("act1_caves_rock01_ALB.texture") != std::string::npos);   // (the old names' bytes stay, nothing points at them: game_ceiling_test)
    }
    std::string unit;
    CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\cave_ceiling_unit.json", &unit) && unit.find("cliffside01.model") != std::string::npos);
    CHECK(ReadAll(scratch + L"cave_ceiling.json", &receipt) && receipt.find("cave_ceiling_unit.json") != std::string::npos);
    // the wall at its rim ([ceiling] game_ceiling_wall), made with it: its tick is live in the game
    const std::wstring wallAt = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_detailnormal\\decal01_lod";
    for (int lod = 0; lod < 5; ++lod) {
        std::string data;
        grannyremesh::File f;
        std::string why;
        CHECK(ReadAll(wallAt + std::to_wstring(lod) + L".model", &data) && grannyremesh::Read(data, &f, &why));
    }
    std::string wallUnit;
    CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\cave_ceiling_wall_unit.json", &wallUnit) &&
          wallUnit.find("decal01.model") != std::string::npos);
    CHECK(receipt.find("cave_ceiling_wall_unit.json") != std::string::npos && receipt.find("decal01_lod4.model") != std::string::npos);
    // every room its own kind: the barracks' smooth dark stone, the palace's ornamented marble - their models and units made too
    {
        std::string barracks, palace, unitB;
        grannyremesh::File f;
        std::string why;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_terrainblending\\cliff01_lod2.model", &barracks) &&
              grannyremesh::Read(barracks, &f, &why) && barracks.find("act2_arcane_grunge01_ALB.texture") != std::string::npos);
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\TextureExportTesting\\TestCube_lod0.model", &palace) &&
              palace.find("act2_palace_cell_marble_tile01_ORM.texture") != std::string::npos);
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\smooth_dark_ceiling_unit.json", &unitB) &&
              unitB.find("test_terrainblending/cliff01.model") != std::string::npos);
        CHECK(receipt.find("ice_cave_wall_unit.json") != std::string::npos && r.text.find(L"15 kinds") != std::wstring::npos);
    }
    // [ceiling] game_ceiling_beams: the barracks' own frame in a grid, in the frame's own file at its test path, five LODs and our unit;
    // [ceiling] height_from_walls: the rooms' walls' top and the frame's size measured into measures.txt
    {
        const std::wstring beamAt = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_debug_emissive\\debug_emissive01_lod";
        unsigned verts[5] = {};
        for (int lod = 0; lod < 5; ++lod) {
            std::string data, why;
            grannyremesh::File f;
            grannyremesh::Mesh m;
            CHECK(ReadAll(beamAt + std::to_wstring(lod) + L".model", &data) && grannyremesh::Read(data, &f, &why) && grannyremesh::MeshOf(f, &m, &why));
            CHECK(data.find("act2_town_shops_wood01_ALB.texture") != std::string::npos);   // the frame's own wood
            verts[lod] = (unsigned)m.v.size();
            CHECK(m.mx[0] - m.mn[0] > 200.0f && m.mx[1] - m.mn[1] < 1.0f && m.mn[1] > 14.0f);   // whole frames side by side, at their own height
        }
        CHECK(verts[0] == 42u * 1430u && verts[4] < verts[0]);   // 7 x 6 of lod0's 1430; the same grid of a lower LOD's frame
        std::string beamUnit, text;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\barracks_beams_unit.json", &beamUnit) &&
              beamUnit.find("test_debug_emissive/debug_emissive01.model") != std::string::npos);
        ceilmeasure::Measures ms;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\measures.txt", &text) && ceilmeasure::Parse(text, &ms));
        CHECK(ms.haveBeam && ms.beam.cols == 7 && ms.beam.rows == 6 && ms.beam.indices == 42u * 2160u * 3u);
        CHECK(std::fabs(ms.beam.periodX - 30.76f) < 0.01f && std::fabs(ms.beam.periodZ - 40.47f) < 0.01f && std::fabs(ms.beam.bottom - 14.92f) < 0.01f);
        const float barracks = ceilmeasure::WallsTopOf(ms, "act1_barracks"), crypt = ceilmeasure::WallsTopOf(ms, "act1_crypt");
        std::printf("  measured: %zu areas - barracks %.2f, crypt %.2f, catacombs %.2f, caves %.2f, cathedral %.2f; the frame %.2f x %.2f, %.3f .. %.3f\n",
                    ms.walls.size(), barracks, crypt, ceilmeasure::WallsTopOf(ms, "act1_catacombs"), ceilmeasure::WallsTopOf(ms, "act1_caves"),
                    ceilmeasure::WallsTopOf(ms, "act1_cathedral"), ms.beam.periodX, ms.beam.periodZ, ms.beam.bottom, ms.beam.top);
        for (const auto& w : ms.walls) std::printf("    %s %.2f\n", w.first.c_str(), w.second);
        CHECK(std::fabs(barracks - 14.9f) < 0.3f && std::fabs(crypt - 14.1f) < 0.3f);   // the survey's (and near Serg's hand-set 15.1 / 14.5)
        CHECK(std::fabs(barracks - ms.beam.bottom) < 0.3f);   // the frame sits on the barracks walls
        CHECK(ReadAll(scratch + L"cave_ceiling.json", &receipt) && receipt.find("measures.txt") != std::string::npos &&
              receipt.find("debug_emissive01_lod4.model") != std::string::npos && receipt.find("barracks_beams_unit.json") != std::string::npos);
        CHECK(r.text.find(L"barracks' beams") != std::wstring::npos && r.text.find(L"42 whole frames") != std::wstring::npos);
        // [ceiling] game_ceiling_dome: the cathedral's dome measured from its preset (cathy3: the canopy baldachin01 at 146.5 123.5, the
        // crossing's pillars at 102.5 / 182.5) and its vault made from the cave rock with the cathedral's stone, five LODs and a unit
        CHECK(!ms.domes.empty() && ms.domes[0].slot == 0 && ms.domes[0].biome == "act1_cathedral");
        const ceilmeasure::Dome& d = ms.domes[0];
        std::printf("  the cathedral's dome: the canopy at %.1f %.1f %.1f (top %.2f), a pillar at %.1f %.1f; its middle %.1f %.1f, %.2f round, the "
                    "canopy reaching %.2f; made rising %.2f over the pillars' %.2f; the preset %.1f round; %u indices\n", d.anchor[0], d.anchor[1],
                    d.anchor[2], d.top, d.witness[0], d.witness[2], d.centreX, d.centreZ, d.radius, d.reach, d.rise, d.springs, d.half, d.indices);
        CHECK(d.anchor[0] == 146.5f && d.anchor[1] == 0.0f && d.anchor[2] == 123.5f && std::fabs(d.top - 36.5f) < 0.1f);   // (lod0's bounds)
        CHECK(d.centreX == 142.5f && d.centreZ == 142.5f && std::fabs(d.radius - 56.57f) < 0.01f && std::fabs(d.springs - 19.48f) < 0.03f);
        CHECK(d.reach > 30.0f && d.reach < d.radius && d.rise > d.top - d.springs && d.rise < 30.0f);
        const std::wstring vaultAt = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_cloth\\cloth01_lod";
        for (int lod = 0; lod < 5; ++lod) {
            std::string data, why;
            grannyremesh::File f;
            grannyremesh::Mesh m;
            CHECK(ReadAll(vaultAt + std::to_wstring(lod) + L".model", &data) && grannyremesh::Read(data, &f, &why) && grannyremesh::MeshOf(f, &m, &why));
            CHECK(data.find("act2_arcane_grunge02_ALB.texture") != std::string::npos);   // the cathedral's stone
            CHECK(m.idx.size() == d.indices && std::fabs(m.mx[1] - d.rise) < 0.01f && m.mx[0] >= d.half);   // the dome in it, over the preset
        }
        std::string vaultUnit;
        CHECK(ReadAll(scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\cathedral_vault_unit.json", &vaultUnit) &&
              vaultUnit.find("test_cloth/cloth01.model") != std::string::npos);
        CHECK(receipt.find("cloth01_lod4.model") != std::string::npos && receipt.find("cathedral_vault_unit.json") != std::string::npos);
        CHECK(r.text.find(L"cathedral's dome") != std::wstring::npos && r.text.find(L"57 units round") != std::wstring::npos);
        // [ceiling] game_ceiling_worldstone_dome: in the throne biome only, its piece standing the most over its ceiling, a ring of pillars
        // round it past its footprint - the Worldstone (wstone01: crystalflame_01 at 266.7 241.3, 45.6 over 9.9), its own slot, vault and unit
        bool worldstone = false;
        for (const ceilmeasure::Dome& t : ms.domes) {
            std::printf("  dome, slot %d: %s - the highest piece at %.1f %.1f (top %.2f), its middle %.1f %.1f, %.1f round, reaching %.1f; rising %.2f "
                        "over the pillars' %.2f; the preset %.1f round; %u indices\n", t.slot, t.biome.c_str(), t.anchor[0], t.anchor[2], t.top,
                        t.centreX, t.centreZ, t.radius, t.reach, t.rise, t.springs, t.half, t.indices);
            CHECK(t.slot >= 0 && t.slot < gameceiling::kVaultSlots && t.reach < t.radius && t.rise > 0.0f && t.indices > 0);
            CHECK(t.top >= ceilmeasure::kTallFactor * ceilmeasure::WallsTopOf(ms, t.biome) - 0.01f);
            if (t.slot == 0) continue;
            CHECK(std::hypot(t.centreX - t.anchor[0], t.centreZ - t.anchor[2]) < t.reach);   // over the piece itself, its footprint's middle
            worldstone = worldstone || (t.biome == "expansion_baallair_throneroom" && std::fabs(t.anchor[0] - 266.7f) < 0.2f && std::fabs(t.anchor[2] - 241.3f) < 0.2f);
            std::string unit;
            CHECK(ReadAll(scratch + L"D2R-VR.mpq\\" + std::wstring(gameceiling::kVaultUnits[t.slot], gameceiling::kVaultUnits[t.slot] + std::strlen(gameceiling::kVaultUnits[t.slot])), &unit) &&
                  unit.find(gameceiling::ModelPath(gameceiling::kVaultModelFiles[t.slot])) != std::string::npos);
        }
        // (Serg: "только на 132 уровне ... и только там где куст": the cathedral's and the Worldstone's, no other - not the throne room's arch)
        CHECK(worldstone && ms.domes.size() == 2 && ms.domes[1].slot == 1);
        CHECK(r.text.find(L"the Worldstone's dome") != std::wstring::npos && r.text.find(L"expansion_baallair_throneroom") != std::wstring::npos);
        {   // a vault's file there not ours (0.188.7: an earlier build's rock slab at its path, kept as the player's - the game drew it, flat
            // rock over the Worldstone): kept, and that dome taken out of the measures - never laid with someone else's model
            const std::wstring ws = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\expansion\\wildtemple\\expansion_wildtemple_temple\\temple01_d_lod1.model";
            const std::wstring mt = scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\measures.txt";
            std::string orig, text2;
            ceilmeasure::Measures m2;
            CHECK(ReadAll(ws, &orig) && orig.size() > 100000);   // (the vault: our mesh, not a slab)
            WriteAll(ws, "not ours");
            r = PrepareCeiling(game, scratch, true);
            std::wprintf(L"  a foreign file at the Worldstone vault's path: %ls\n", r.text.c_str());
            CHECK(ReadAll(mt, &text2) && ceilmeasure::Parse(text2, &m2) && m2.domes.size() == 1 && m2.domes[0].slot == 0 &&
                  r.text.find(L"not ours") != std::wstring::npos);
            WriteAll(ws, orig);   // ours again: the dome back
            r = PrepareCeiling(game, scratch, true);
            CHECK(ReadAll(mt, &text2) && ceilmeasure::Parse(text2, &m2) && m2.domes.size() == 2 && m2.domes[1].slot == 1);
        }
    }
    r = PrepareCeiling(game, scratch, true);
    CHECK(r.state == kMade && !r.wrote);   // nothing to do
    // the sky dome's files untouched by the ceiling's (their own paths, their own receipt)
    CHECK(GetFileAttributesW(at(L"act1", 2).c_str()) != INVALID_FILE_ATTRIBUTES);
    r = PrepareCeiling(game, scratch, false);
    std::wprintf(L"  ceiling off: %ls\n", r.text.c_str());
    CHECK(r.state == kOff && r.wrote && GetFileAttributesW((cliff + L"0.model").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((wallAt + L"0.model").c_str()) == INVALID_FILE_ATTRIBUTES && r.text.find(L"127 of ours") != std::wstring::npos);   // (15 kinds, 3 walls, the beams, 2 vaults: 6 files each; the measures)
    CHECK(GetFileAttributesW((scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\test_cloth\\cloth01_lod0.model").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((scratch + L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\ceiling\\measures.txt").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((scratch + L"cave_ceiling.json").c_str()) == INVALID_FILE_ATTRIBUTES);
    // the Ceiling tab's rooms (shared/ceiling_levels.h): each one's levels as the game's own files have them - every level of its biome,
    // by its number and name - in the order the game comes to them; every room of a ceiling kind among them
    {
        std::map<int, std::string> names;
        std::string why;
        const std::map<std::string, std::set<int>> byBiome = LevelsByBiome(game, &names, &why);
        CHECK(!byBiome.empty() && names.size() > 100);
        if (!why.empty()) std::printf("  levels: %s\n", why.c_str());
        int lastFirst = 0;
        for (const ceillevels::Area& a : ceillevels::kAreas) {
            std::set<int> ours;
            for (int i = 0; i < ceillevels::Count(a); ++i) {
                ours.insert(a.levels[i].id);
                if (names[a.levels[i].id] != a.levels[i].name) {
                    CHECK(names[a.levels[i].id] == a.levels[i].name);
                    std::printf("    level %d is '%s' in the game, '%s' here\n", a.levels[i].id, names[a.levels[i].id].c_str(), a.levels[i].name);
                }
            }
            const auto it = byBiome.find(a.biome);
            const std::set<int> game = it == byBiome.end() ? std::set<int>() : it->second;
            if (ours != game) {
                CHECK(ours == game);
                std::string g;
                for (const int id : game) g += " " + std::to_string(id);
                std::printf("    %s: the game's levels%s\n", a.biome, g.c_str());
            }
            CHECK(a.levels[0].id >= lastFirst);   // (the game's order; two biomes of one level side by side)
            lastFirst = a.levels[0].id;
            std::wprintf(L"  %ls\n", ceillevels::Title(a).c_str());
        }
        for (const gameceiling::BiomeKind& k : gameceiling::kBiomeKinds) CHECK(ceillevels::Find(k.biome) != nullptr);
        CHECK(ceillevels::Ranges(*ceillevels::Find("act1_crypt")) == L"18-25, 133" && ceillevels::Title(*ceillevels::Find("act1_cathedral")) ==
              L"Ceiling: act 1 cathedral (level 33)");
        // the rooms with no picture matched: their kind's maps their own floor's stone (gameceiling::FloorMaps over its biome's terrain
        // layers), the cave rock where it has none
        for (const gameceiling::FloorKind& f : gameceiling::kFloorKinds) {
            int kind = -1;
            for (int k = 0; k < gameceiling::kKindCount; ++k) if (std::string(gameceiling::kKinds[k].name) == f.kind) kind = k;
            CHECK(kind >= 0 && gameceiling::KindOf(f.biome) == kind);
            const std::vector<std::string> layers = FloorsOf(game, f.biome);
            CHECK(!layers.empty());
            std::string floor = gameceiling::FloorMaps(layers);
            if (floor.empty()) floor = gameceiling::kRockMaps;
            if (kind >= 0 && floor != gameceiling::kKinds[kind].maps) {
                CHECK(floor == gameceiling::kKinds[kind].maps);
                std::printf("    %s's floor in the game: %s\n", f.biome, floor.c_str());
            }
        }
        // every indoor area of the game (levels.txt's *IsInside) has a ceiling - but where an open sky belongs: on [sky] outdoor (the
        // shipped list), the Arcane Sanctuary, the developers' test biome
        {
            const std::string outdoor = ",act1_outdoors,act1_tristram,act2_outdoors,act2_town,act3_jungle,act3_docktown,act3_kurast,act3_travincal_outdoors,"
                                        "act4_mesa,act4_lava,act4_diab,expansion_town,expansion_siege,expansion_mountaintop,expansion_ruins,expansion_ruins_snow,"
                                        "expansion_wildtemple_tempenter,act1_court,act2_arcane,test,";
            int indoor = 0;
            for (const auto& b : InsideBiomes(game)) {
                if (outdoor.find("," + b + ",") != std::string::npos) continue;
                ++indoor;
                if (gameceiling::KindOf(b) < 0 || !ceillevels::Find(b)) {
                    CHECK(gameceiling::KindOf(b) >= 0 && ceillevels::Find(b));
                    std::printf("    an indoor area with no ceiling: %s\n", b.c_str());
                }
            }
            CHECK(indoor >= 20);
        }
    }
    if (g_fail) { std::printf("dome_assets_test: %d FAILED\n", g_fail); return 1; }
    std::printf("dome_assets_test: thirty-five dome models from the game's Summit card, the receipt, nothing again, the player's change kept; the cave "
                "ceiling's five from its cave rock and its unit, and out again: all passed\n");
    return 0;
}
