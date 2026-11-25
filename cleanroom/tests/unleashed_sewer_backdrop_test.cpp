// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// unleashed_sewer_backdrop_test - [holes] sewer_backdrop: the Kurast sewer stairs' open end closed (shared/unleashed_sewer_backdrop.h, after
// D2R VR Unleashed steamvr/prepare_sewer_backdrop.py and its prepare_sewer_backdrop_test.py) and D2R_VR_Settings' maker (tools/settings/sewer_assets.cpp).
//   Always, on a made-up stairwell and made-up presets: the floor's lowest edge found, the wall half a unit behind it, 1 unit wider on the left
//   and 3 on the right, from half a unit under the floor to half a unit over the top; turned and moved by the stairs' own transform; both faces;
//   every byte of the preset kept but the two entities and the one dependency; refusals (no stairs, two, a stretched transform, not a preset,
//   no open edge, the quad not a quad); a preset that has ours already.
//   With the game (its archives read only, everything written under the scratch folder in the mod's layout <scratch>\mods\D2R-VR): the real
//   stairs and quad measured, the four real presets changed and read back; the maker: not the mod / no .mpq: nothing; made; again: nothing
//   written; a preset the player changed stays; off: ours out, the receipt too; nothing outside mods\D2R-VR.
//   unleashed_sewer_backdrop_test [<game folder> <empty scratch folder>]

#ifndef NOMINMAX
#define NOMINMAX   // (std::min / std::max in the shared headers)
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"
#include "props_presets.h"
#include "sewer_assets.h"
#include "unleashed_sewer_backdrop.h"

using namespace sewerbackdrop;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}
static bool Near(double a, double b, double e = 1e-4) { return std::fabs(a - b) < e; }

static std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static void WriteAll(const std::wstring& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << data;
}
static std::wstring Slash(std::wstring p) {
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p;
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// ---------------------------------------------------------------- made up

// A stairwell going down along -z: its floor ("terrain") from y 0 at z 20 to y -6 at z 10, 5 units wide (x 0 - 5); a wall to y 2.
static Model Stairs() {
    Model m;
    worldholes::MeshData d;
    d.name = "stairs";
    d.scale = 32;
    auto v = [&](float x, float y, float z) { worldholes::Vtx t; t.p[0] = x; t.p[1] = y; t.p[2] = z; d.v.push_back(t); };
    v(0, 0, 20); v(5, 0, 20); v(0, -6, 10); v(5, -6, 10);   // the floor
    v(0, 2, 20); v(0, 2, 10);                               // the wall's top
    d.idx = {0, 1, 2, 1, 3, 2, 0, 2, 4, 2, 5, 4};
    worldholes::Group floor, wall;
    floor.material = 0; floor.first = 0; floor.count = 2; floor.materialName = "terrain";
    wall.material = 1; wall.first = 2; wall.count = 2; wall.materialName = "elevation_walls_a";
    d.groups = {floor, wall};
    m.meshes.push_back(d);
    return m;
}
static Model Black() {
    Model m;
    worldholes::MeshData d;
    d.name = "black";
    auto v = [&](float x, float z) { worldholes::Vtx t; t.p[0] = x; t.p[2] = z; d.v.push_back(t); };
    v(-50, -50); v(50, -50); v(50, 50); v(-50, 50);
    d.idx = {0, 1, 2, 0, 2, 3};
    m.meshes.push_back(d);
    return m;
}

static std::string StairsEntity(const char* id, const char* pos, const char* rot, const char* scale) {
    return std::string("{\"type\":\"Entity\",\"name\":\"stairs\",\"id\":") + id +
           ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"name\":\"t\",\"position\":" + pos + ",\"orientation\":" + rot +
           ",\"scale\":" + scale + ",\"inheritOnlyPosition\":false},{\"type\":\"ModelDefinitionComponent\",\"name\":\"m\",\"filename\":\"" +
           "Data/HD/Env/Model/Act3/Kurast/Act3_Kurast_Stairs/Stairs_Sewers01.model\"}]}";
}
static std::string OtherEntity() {
    return "{\"type\":\"Entity\",\"name\":\"wall\",\"id\":7,\"components\":[{\"type\":\"ModelDefinitionComponent\",\"name\":\"m\",\"filename\":"
           "\"data/hd/env/model/act3/kurast/act3_kurast_architecture/sewer_entrance02.model\"}]}";
}
static std::string Preset(const std::vector<std::string>& ents, const char* models = "{\"path\":\"data/hd/env/model/x.model\"}") {
    std::string s = std::string("{\"dependencies\":{\"particles\":[],\"models\":[") + models + "],\"textures\":[]},\"type\":\"Preset\",\"name\":\"p\",\"entities\":[";
    for (size_t i = 0; i < ents.size(); ++i) s += (i ? "," : "") + ents[i];
    return s + "],\"terrain\":{\"type\":\"Entity\",\"name\":\"terrain\",\"id\":2,\"components\":[]}}";
}

static const char* kId = "{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0}";
static const char* kOne = "{\"x\":1.0,\"y\":1.0,\"z\":1.0}";

struct Ours { V3 pos; Q rot; V3 size; int n = 0; };
static std::vector<Ours> Read(const std::string& json, bool* depOnce) {
    std::vector<Ours> out;
    propspresets::Value doc;
    if (!propspresets::Parser(json.data(), json.size()).Parse(doc)) return out;
    int deps = 0;
    for (const auto& d : doc.Get("dependencies")->Get("models")->items) deps += Lower(d.Get("path")->s) == std::string(kBlack) + ".model";
    *depOnce = deps == 1;
    for (const auto& e : doc.Get("entities")->items) {
        if (e.Get("name")->s.rfind(kPrefix, 0) != 0) continue;
        Ours o;
        for (const auto& c : e.Get("components")->items) {
            if (c.Get("type")->s == "ModelDefinitionComponent") o.n += c.Get("filename")->s == std::string(kBlack) + ".model";
            if (c.Get("type")->s != "TransformDefinitionComponent") continue;
            const char* x[4] = {"x", "y", "z", "w"};
            for (int i = 0; i < 3; ++i) { o.pos[i] = c.Get("position")->Get(x[i])->n; o.size[i] = c.Get("scale")->Get(x[i])->n; }
            for (int i = 0; i < 4; ++i) o.rot[i] = c.Get("orientation")->Get(x[i])->n;
        }
        out.push_back(o);
    }
    return out;
}

// The world box of the quad as placed (its four corners, scaled, turned, moved), and the normal of its face
static void Box(const Ours& o, const Model& black, V3* lo, V3* hi, V3* normal) {
    *lo = {1e30, 1e30, 1e30};
    *hi = {-1e30, -1e30, -1e30};
    for (const auto& v : black.meshes[0].v) {
        const V3 p = Add(o.pos, Rotate({(v.p[0] + black.placement[0]) * o.size[0], (v.p[1] + black.placement[1]) * o.size[1], (v.p[2] + black.placement[2]) * o.size[2]}, o.rot));
        for (int c = 0; c < 3; ++c) { (*lo)[c] = std::min((*lo)[c], p[c]); (*hi)[c] = std::max((*hi)[c], p[c]); }
    }
    *normal = Rotate({0, 1, 0}, o.rot);
}

static void MadeUp() {
    Measured m;
    std::string why, out;
    Expect(Measure(Stairs(), Black(), &m, &why), "the made-up stairwell measured");
    printf("  made up: centre %.3f %.3f %.3f, scale %.4f %.4f %.4f, width %.2f height %.2f\n", m.center[0], m.center[1], m.center[2], m.scale[0], m.scale[1],
           m.scale[2], m.width, m.height);
    Expect(Near(m.width, 5) && Near(m.height, 9), "the open end 5 wide; the opening 9 high (y -6 to 2, plus 1)");
    Expect(Near(m.center[0], 3.5) && Near(m.center[1], -2) && Near(m.center[2], 9.5), "the wall's middle: 1 more to the right, half a unit behind the end, halfway up");
    Expect(Near(m.scale[0], 0.09) && Near(m.scale[1], 1) && Near(m.scale[2], 0.09), "the 100-unit quad scaled to 9 x 9");

    // laid by the preset's stairs (moved, unturned)
    const std::string other = OtherEntity();
    const std::string in = Preset({other, StairsEntity("5", "{\"x\":100.0,\"y\":1.0,\"z\":200.0}", kId, kOne)});
    Expect(AddBackdrop(in, m, &out, &why) == kAdded, "a preset with the stairs: added");
    bool dep = false;
    std::vector<Ours> ours = Read(out, &dep);
    Expect(ours.size() == 2 && dep && ours[0].n == 1 && ours[1].n == 1, "two faces, each the black quad; its dependency once");
    if (ours.size() == 2) {
        V3 lo, hi, n0, n1;
        Box(ours[0], Black(), &lo, &hi, &n0);
        printf("  front face spans x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        Expect(Near(lo[0], 99) && Near(hi[0], 108) && Near(lo[1], -5.5) && Near(hi[1], 3.5) && Near(lo[2], 209.5) && Near(hi[2], 209.5),
               "the front face: x from 1 left of the floor to 3 right of it, y from half a unit under the floor to half over the top, z half a unit behind");
        V3 l2, h2;
        Box(ours[1], Black(), &l2, &h2, &n1);
        Expect(Near(l2[0], lo[0]) && Near(h2[0], hi[0]) && Near(l2[1], lo[1]) && Near(h2[1], hi[1]) && Near(l2[2], lo[2]), "the back face in the same place");
        Expect(Near(n0[2], -n1[2]) && Near(std::fabs(n0[2]), 1), "... facing the other way (along the stairs)");
    }
    // every other byte as it was: take ours out again and the game's text is back
    {
        std::string back = out;
        const size_t a = back.find("{\"type\":\"Entity\",\"name\":\"D2R_VR_Sewer_Backdrop_front\"");
        const size_t b = back.find(other);
        if (a != std::string::npos && b != std::string::npos) back.erase(a, b - a);
        const std::string dep2 = std::string("{\"path\":\"") + kBlack + ".model\"},";
        const size_t d = back.find(dep2);
        if (d != std::string::npos) back.erase(d, dep2.size());
        Expect(back == in, "every other byte of the preset as the game has it");
    }
    {
        const std::string withOurs = out;
        Expect(AddBackdrop(withOurs, m, &out, &why) == kOurs && out.empty(), "a preset with ours already: nothing to change");
    }

    // turned a quarter round y and twice the size: the wall turned and scaled with the stairs
    const double h = std::sqrt(0.5);
    char rot[96];
    snprintf(rot, sizeof rot, "{\"x\":0.0,\"y\":%.17g,\"z\":0.0,\"w\":%.17g}", h, h);
    Expect(AddBackdrop(Preset({StairsEntity("5", "{\"x\":10.0,\"y\":0.0,\"z\":20.0}", rot, "{\"x\":2.0,\"y\":2.0,\"z\":2.0}")}), m, &out, &why) == kAdded,
           "turned stairs: added");
    ours = Read(out, &dep);
    if (ours.size() == 2) {
        V3 lo, hi, n;
        Box(ours[0], Black(), &lo, &hi, &n);
        const V3 c = Add({10, 0, 20}, Rotate(Mul(m.center, 2), {0, h, 0, h}));
        Expect(Near((lo[0] + hi[0]) / 2, c[0], 1e-3) && Near((lo[1] + hi[1]) / 2, c[1], 1e-3) && Near((lo[2] + hi[2]) / 2, c[2], 1e-3),
               "the wall's middle where the turned stairs put it");
        Expect(Near(hi[1] - lo[1], 18, 1e-3) && Near(std::max(hi[0] - lo[0], hi[2] - lo[2]), 18, 1e-3) && Near(std::min(hi[0] - lo[0], hi[2] - lo[2]), 0, 1e-3),
               "twice the size, still flat across the turned tunnel");
    } else {
        Expect(false, "turned stairs: two faces");
    }
    Expect(AddBackdrop(Preset({other}, ""), m, &out, &why) == kRefused, "no stairs: refused");
    Expect(AddBackdrop(Preset({StairsEntity("5", "{\"x\":0.0,\"y\":0.0,\"z\":0.0}", kId, kOne), StairsEntity("6", "{\"x\":9.0,\"y\":0.0,\"z\":0.0}", kId, kOne)}), m,
                       &out, &why) == kRefused,
           "two stairwells: refused");
    Expect(AddBackdrop(Preset({StairsEntity("5", "{\"x\":0.0,\"y\":0.0,\"z\":0.0}", kId, "{\"x\":1.0,\"y\":2.0,\"z\":1.0}")}), m, &out, &why) == kRefused,
           "stretched stairs: refused");
    printf("  %s\n", why.c_str());
    std::string notPreset = in;
    notPreset.replace(notPreset.find("\"Preset\""), 8, "\"Prefab\"");
    Expect(AddBackdrop(notPreset, m, &out, &why) == kRefused && out.empty(), "not a preset: refused");
    Expect(AddBackdrop("", m, &out, &why) == kRefused, "nothing: refused");
    Expect(AddBackdrop(Preset({StairsEntity("5", "{\"x\":0.0,\"y\":0.0,\"z\":0.0}", kId, kOne)}, ""), m, &out, &why) == kAdded &&
               out.find(std::string("\"models\":[{\"path\":\"") + kBlack + ".model\"}]") != std::string::npos,
           "an empty models list: the dependency alone in it");

    Model flat = Stairs();
    for (auto& v : flat.meshes[0].v) v.p[1] = 0;
    Expect(!Measure(flat, Black(), &m, &why), "a flat floor (no one lowest edge): refused");
    Model tri = Black();
    tri.meshes[0].v.pop_back();
    Expect(!Measure(Stairs(), tri, &m, &why), "the quad not four corners: refused");
    Model up = Stairs();
    for (auto& v : up.meshes[0].v) v.p[2] = 30 - v.p[2];   // the floor rising the other way: the edge's sides swapped
    Expect(!Measure(up, Black(), &m, &why) || m.center[2] > 10, "the stairs mirrored: never a wall on the stairs' own side");
}

// ---------------------------------------------------------------- the game's own

static bool Casc(HANDLE storage, const std::string& name, std::string* out) {
    HANDLE f = nullptr;
    out->clear();
    if (!CascOpenFile(storage, ("data:" + name).c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    CascGetFileSize64(f, &size);
    out->resize((size_t)size);
    DWORD got = 0;
    const bool ok = CascReadFile(f, out->data(), (DWORD)size, &got) && got == size;
    CascCloseFile(f);
    return ok;
}

static void Game(const std::wstring& game, const std::wstring& into) {
    std::wstring root = game;
    root.pop_back();
    HANDLE storage = nullptr;
    std::string sb, bb;
    std::vector<std::string> presets(kPresetCount);
    bool read = CascOpenStorage(root.c_str(), 0, &storage);
    Expect(read, "the game's archives opened");
    if (!read) return;
    read = Casc(storage, std::string(kStairs) + "_lod4.model", &sb) && Casc(storage, std::string(kBlack) + "_lod4.model", &bb);
    for (int i = 0; i < kPresetCount && read; ++i) read = Casc(storage, kPresets[i], &presets[(size_t)i]);
    // the other LODs measure the same (the outline is the same at every level of detail)
    std::vector<Measured> lods;
    for (int lod = 0; lod <= 4 && read; ++lod) {
        std::string a, b, why;
        Model s, k;
        Measured m;
        if (Casc(storage, std::string(kStairs) + "_lod" + std::to_string(lod) + ".model", &a) && Casc(storage, std::string(kBlack) + "_lod" + std::to_string(lod) + ".model", &b) &&
            ReadModel(a, &s, &why) && ReadModel(b, &k, &why) && Measure(s, k, &m, &why))
            lods.push_back(m);
        else
            printf("  LOD %d: %s\n", lod, why.c_str());
    }
    CascCloseStorage(storage);
    Expect(read, "the stairs, the quad and the four presets read from the game");
    if (!read) return;
    Model stairs, black;
    Measured m;
    std::string why;
    Expect(ReadModel(sb, &stairs, &why) && ReadModel(bb, &black, &why), "both models read");
    printf("  stairs: %zu mesh(es), %zu vertices, placement %.3f %.3f %.3f; quad: placement %.3f %.3f %.3f (%s)\n", stairs.meshes.size(),
           stairs.meshes.empty() ? 0 : stairs.meshes[0].v.size(), stairs.placement[0], stairs.placement[1], stairs.placement[2], black.placement[0], black.placement[1],
           black.placement[2], why.c_str());
    if (!stairs.meshes.empty()) {
        printf("  stairs' materials:");
        for (const auto& g : stairs.meshes[0].groups) printf(" %s(%u)", g.materialName.c_str(), g.count);
        printf("\n");
    }
    const bool measured = Measure(stairs, black, &m, &why);
    Expect(measured, "the game's stairs measured");
    if (!measured) { printf("  why: %s\n", why.c_str()); return; }
    printf("  the game's: open end %.3f wide, opening %.3f high; centre %.3f %.3f %.3f; facing %.4f %.4f %.4f %.4f; scale %.5f %.5f %.5f\n", m.width, m.height,
           m.center[0], m.center[1], m.center[2], m.facing[0], m.facing[1], m.facing[2], m.facing[3], m.scale[0], m.scale[1], m.scale[2]);
    int same = 0;
    for (const Measured& l : lods) same += Near(l.width, m.width, 0.05) && Near(l.height, m.height, 0.05) && Near(l.center[0], m.center[0], 0.05) && Near(l.center[2], m.center[2], 0.05);
    printf("  %d of %zu LODs measure the same\n", same, lods.size());
    Expect(same >= 1, "the measured LOD agrees with itself");
    for (int i = 0; i < kPresetCount; ++i) {
        std::string out;
        const State s = AddBackdrop(presets[(size_t)i], m, &out, &why);
        char what[200];
        snprintf(what, sizeof what, "%s: the two faces added", kPresets[i]);
        Expect(s == kAdded, what);
        if (s != kAdded) { printf("  why: %s\n", why.c_str()); continue; }
        bool dep = false;
        const std::vector<Ours> ours = Read(out, &dep);
        Expect(ours.size() == 2 && dep && out.size() > presets[(size_t)i].size(), "... read back: two faces, the quad's dependency once");
        if (ours.size() == 2) {
            V3 lo, hi, n;
            Box(ours[0], black, &lo, &hi, &n);
            printf("  %s (%zu bytes): front face x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n", kPresets[i], presets[(size_t)i].size(), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        }
    }

    // the maker
    const std::wstring modRoot = into + L"mods\\D2R-VR\\", mpq = modRoot + L"D2R-VR.mpq\\";
    CreateDirectoryW(into.c_str(), nullptr);
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    sewerassets::Result r = sewerassets::Prepare(game, L"", true);
    Expect(r.state == sewerassets::kNotHere && !r.wrote, "not the mod: nothing made");
    r = sewerassets::Prepare(game, modRoot, true);
    Expect(r.state == sewerassets::kNotHere && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing made");
    CreateDirectoryW(mpq.c_str(), nullptr);
    r = sewerassets::Prepare(game, modRoot, true);
    printf("  %ls\n", r.text.c_str());
    Expect(r.state == sewerassets::kMade && r.wrote, "with the mod's D2R-VR.mpq: made");
    std::vector<std::wstring> files;
    for (const char* rel : kPresets) {
        std::wstring p = mpq + std::wstring(rel, rel + strlen(rel));
        for (wchar_t& c : p) if (c == L'/') c = L'\\';
        files.push_back(p);
    }
    int ok = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        bool dep = false;
        ok += Read(ReadAll(files[i]), &dep).size() == 2 && dep;
    }
    Expect(ok == kPresetCount, "the four presets in mods\\D2R-VR\\D2R-VR.mpq\\data\\hd\\env\\preset\\act3\\kurast, each with ours");
    const std::wstring receipt = modRoot + L"sewer_backdrop.json";
    Expect(arcanesky::ReceiptValue(ReadAll(receipt), std::string("mods/D2R-VR/D2R-VR.mpq/") + kPresets[0]).size() == 16, "the receipt names them");
    r = sewerassets::Prepare(game, modRoot, true);
    Expect(r.state == sewerassets::kMade && !r.wrote, "again: up to date, nothing written");
    const std::string made0 = ReadAll(files[0]);
    WriteAll(files[0], made0 + " ");
    r = sewerassets::Prepare(game, modRoot, true);
    Expect(r.state == sewerassets::kMade && !r.wrote && ReadAll(files[0]) == made0 + " ", "a preset the player changed: left as it is");
    r = sewerassets::Prepare(game, modRoot, false);
    Expect(r.state == sewerassets::kOff && r.wrote && Exists(files[0]) && !Exists(files[1]) && !Exists(receipt), "off: ours out, the player's stays, the receipt gone");
    DeleteFileW(files[0].c_str());
    r = sewerassets::Prepare(game, modRoot, true);
    Expect(r.state == sewerassets::kMade && r.wrote && ReadAll(files[0]) == made0, "deleted: made again, the same bytes");
    r = sewerassets::Prepare(game, modRoot, false);
    Expect(r.state == sewerassets::kOff && r.wrote && !Exists(files[0]) && !Exists(receipt), "off: all ours out");
    r = sewerassets::Prepare(game, modRoot, false);
    Expect(r.state == sewerassets::kOff && !r.wrote, "off again: nothing to do");

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((into + L"*").c_str(), &fd);
    int others = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n != L"." && n != L".." && n != L"mods") ++others;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    Expect(others == 0, "nothing written outside mods\\D2R-VR");
}

int wmain(int argc, wchar_t** argv) {
    MadeUp();
    if (argc >= 3) {
        const std::wstring game = Slash(argv[1]), into = Slash(argv[2]);
        if (_wcsnicmp(game.c_str(), into.c_str(), game.size()) == 0) { printf("the scratch folder must not be in the game's\n"); return 2; }
        Game(game, into);
    } else {
        printf("  (no game given: the made-up stairs only)\n");
    }
    printf(g_fail ? "unleashed_sewer_backdrop_test: %d FAILED\n" : "unleashed_sewer_backdrop_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
