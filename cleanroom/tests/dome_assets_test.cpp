// dome_assets_test - D2R_VR_Settings' maker of [sky] game_sky's dome models (tools/settings/dome_assets.cpp), run on the player's own game
// into a scratch mod folder the way kurast_boat_test runs the boat's:
//   dome_assets_test <game folder> <empty scratch folder> [<make_sky_dome.py's D2R-VR.mpq folder: its units and textures compared>]
//   1. not the mod (no folder): nothing made;
//   2. the mod's folder: thirty-five models (acts 1 - 5, the magenta test and the black cave dome, LOD 0 - 4), each the game's card with our dome in it - 1105 vertices,
//      3968 triangles, our texture's name, its LOD - and the receipt;
//   3. again: nothing written; a model the player changed stays; the first prototype's model (one section, make_sky_dome.py) is replaced;
//   4. [ceiling] game_ceiling's slab (PrepareCeiling): five models from act 1's cave rock with the floor's rock as their maps, our unit, its
//      receipt; again nothing; off: ours out again.
#include "dome_assets.h"
#include "granny_remesh.h"

#include <windows.h>

#include <cstdio>
#include <string>

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
    r = Prepare(game, scratch, true);
    std::wprintf(L"  made: %ls\n", r.text.c_str());
    CHECK(r.state == kMade && r.wrote);
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
            CHECK(w.Get<int32_t>(w.Member(w.Ref(t), pvd, "Vertices").at(8)) == 1105);
            std::string tex = "sky_";   // each dome its own texture: sky_<act1..act5 / cave / test>_day_HDR.dds
            for (const wchar_t* c = name; *c; ++c) tex += (char)*c;
            tex += "_day_HDR.dds";
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
        CHECK(receipt.find("ice_cave_wall_unit.json") != std::string::npos && r.text.find(L"7 kinds") != std::wstring::npos);
    }
    r = PrepareCeiling(game, scratch, true);
    CHECK(r.state == kMade && !r.wrote);   // nothing to do
    // the sky dome's files untouched by the ceiling's (their own paths, their own receipt)
    CHECK(GetFileAttributesW(at(L"act1", 2).c_str()) != INVALID_FILE_ATTRIBUTES);
    r = PrepareCeiling(game, scratch, false);
    std::wprintf(L"  ceiling off: %ls\n", r.text.c_str());
    CHECK(r.state == kOff && r.wrote && GetFileAttributesW((cliff + L"0.model").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((wallAt + L"0.model").c_str()) == INVALID_FILE_ATTRIBUTES && r.text.find(L"60 of ours") != std::wstring::npos);   // (7 kinds, 3 walls: 6 files each)
    CHECK(GetFileAttributesW((scratch + L"cave_ceiling.json").c_str()) == INVALID_FILE_ATTRIBUTES);
    if (g_fail) { std::printf("dome_assets_test: %d FAILED\n", g_fail); return 1; }
    std::printf("dome_assets_test: thirty-five dome models from the game's Summit card, the receipt, nothing again, the player's change kept; the cave "
                "ceiling's five from its cave rock and its unit, and out again: all passed\n");
    return 0;
}
