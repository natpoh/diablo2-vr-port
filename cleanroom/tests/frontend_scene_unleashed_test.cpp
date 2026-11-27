// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// frontend_scene_unleashed_test - D2R_VR_Settings' maker of the main menu's 3D act scenes (tools/settings/frontend_scene_unleashed.cpp,
// ported from D2R VR Unleashed steamvr/prepare_frontend_scene.py at 1f3b994 - Kudlatytrue, PR #583; bearstonem, PR #584). MPL 2.0.
//   Always: Python's random.Random and math.hypot against numbers Python 3.14 gave; on made-up presets: the clouds out and the painted
//   cards hidden, Python's compact json text (keys in order, floats as repr, ints, escapes, UTF-8), an Act 4 piece copied round its
//   camera (the turns, the ids, the names), not a preset refused, a source preset missing refused; the embedded edits (this test's .rc
//   holds the same files as settings.rc) are the files byte for byte.
//   frontend_scene_unleashed_test --build <frontendact2.json> <sources folder> <out file> [<models folder>]: the scene built as
//     Settings builds it (the edits embedded), the sources from the folder as the script's --tiles-from reads them (town__transw2.json,
//     docktown__docktown3.json); with <models folder>: the water and lava tiles measured from the models in it (<folder>\data\hd\...).
//   frontend_scene_unleashed_test <game folder> <empty scratch folder>: Settings' Prepare against the game (its archives read only,
//     everything written under the scratch folder in the mod's layout <scratch>\mods\D2R-VR): not the mod: nothing; made; again:
//     nothing written; a file the player changed: left; off: ours taken out, the receipt too.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "frontend_scene_unleashed.h"

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}

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
static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
static std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ---------------------------------------------------------------- Python's numbers

static void PythonNumbers() {
    using namespace frontendscene::detail;
    // Python 3.14: r = random.Random(1001); [r.random() for _ in range(3)], r.getrandbits(32), r.choice(range(8)), r.uniform(-1, 1)
    {
        PyRandom r(1001);
        const double a = r.Random(), b = r.Random(), c = r.Random();
        const unsigned bits = r.GetRandBits32();
        const size_t ch = r.RandBelow(8);
        const double u = r.Uniform(-1, 1);
        char line[256];
        snprintf(line, sizeof line, "%.17g %.17g %.17g %u %zu %.17g", a, b, c, bits, ch, u);
        Expect(std::string(line) == "0.79665096795997037 0.058626232748034579 0.7602397739740907 3705284347 1 0.11118949804443767",
               "random.Random(1001): random(), getrandbits(32), choice, uniform as Python's");
    }
    {
        PyRandom r(0);
        char line[64];
        snprintf(line, sizeof line, "%.17g", r.Random());
        Expect(std::string(line) == "0.84442185152504812", "random.Random(0).random() as Python's");
    }
    // math.hypot(3, 4) == 5.0; math.hypot(0.1, 0.2) == 0.223606797749979 (C's hypot may say ...7898)
    Expect(PyHypot(3.0, 4.0) == 5.0 && PyHypot(-0.0, 0.0) == 0.0 && PyHypot(5.0, 0.0) == 5.0, "math.hypot: exact cases");
    char h[64];
    snprintf(h, sizeof h, "%.17g %.17g", PyHypot(0.1, 0.2), PyHypot(1e-310, 3e-310));
    Expect(std::string(h) == "0.22360679774997899 3.1622776601683801e-310", "math.hypot as Python's (lossless sum, a correction)");
}

// ---------------------------------------------------------------- made-up presets

static std::string Num(double v) {
    char b[64];
    snprintf(b, sizeof b, "%.17g", v);
    return b;
}
static std::string Named(const char* name, int id) {
    return std::string("{\"type\":\"Entity\",\"name\":\"") + name + "\",\"id\":" + std::to_string(id) + ",\"components\":[]}";
}
static std::string Model(int id, const char* name, const char* model, double x, double y, double z) {
    return "{\"type\":\"Entity\",\"name\":\"" + std::string(name) + "\",\"id\":" + std::to_string(id) +
           ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"name\":\"t\",\"position\":{\"x\":" + Num(x) + ",\"y\":" + Num(y) + ",\"z\":" + Num(z) +
           "},\"orientation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0}},{\"type\":\"ModelDefinitionComponent\",\"name\":\"m\",\"filename\":\"" + model + "\"}]}";
}
static std::string Preset(const std::vector<std::string>& entities, const std::string& extra = "") {
    std::string s = "{\"type\":\"Preset\",\"name\":\"frontendact2\",\"entities\":[";
    for (size_t i = 0; i < entities.size(); ++i) s += (i ? "," : "") + entities[i];
    return s + "]" + extra + "}";
}

// The made-up preset's needs: Act 2's wall, road and ground pieces (walls(), road() and tiles() look them up by name).
static std::vector<std::string> Act2Pieces() {
    std::vector<std::string> e;
    int id = 500;
    const char* wall = "data/hd/env/model/ui/ui_act2_vignette/ui_wall01.model";
    e.push_back(Model(id++, "ui_wall01_01", wall, 60.0, 0.0, 120.0));
    e.push_back(Model(id++, "ui_wall01_02", wall, 60.0, 0.0, 126.0));
    e.push_back(Model(id++, "ui_wall01_03", wall, 60.0, 0.0, 132.0));
    e.push_back(Model(id++, "ui_wall01_cheap_05", wall, 60.0, 0.0, 100.0));
    const char* runs[] = {"trim_straight02_04", "trim_single02_03", "trim_straight01_11", "trim_straight01_05", "trim_straight03_07",
                          "trim_straight06_14", "trim_straight01_13", "trim_straight03_08"};
    double x = 40.0;
    for (const char* r : runs) { e.push_back(Model(id++, r, "data/hd/env/model/act2/stone_trims/trim_straight01.model", x, 0.0, 108.0)); x += 4.0; }
    e.push_back(Model(id++, "ui_act2_vignette_terrain_01", "data/hd/env/model/ui/ui_act2_vignette/ui_act2_vignette_terrain.model", 70.0, 0.0, 110.0));
    return e;
}

static frontendscene::Input SourcesFor(const std::string& preset) {
    frontendscene::Input in;
    in.preset = preset;
    in.sources["data/hd/env/preset/act2/town/transw2.json"] = "{\"entities\":[]}";
    in.sources["data/hd/env/preset/act3/docktown/docktown3.json"] = "{\"entities\":[]}";
    return in;
}

static void MadeUp() {
    using namespace frontendscene;
    std::string out, why;
    std::vector<std::string> report;

    // the clouds out, the cards hidden, Python's compact text (a key twice: the last value in the first place; floats as repr; big ints; escapes)
    std::vector<std::string> ents = Act2Pieces();
    ents.push_back(Named("FX_UIFrontEndAct1_clouds_01", 1));
    ents.push_back(Named("act1_sky_01", 2));
    ents.push_back(R"J({"type":"Entity","name":"keep é \"x\"\/\t\u0001 😀","id":18446744073709551617,"components":[],"v":[1.50,1e-5,1E16,-0.0,0.1,-0,100.0,2.5e-7,1234567890123456.7,true,null],"v":2})J");
    Input in = SourcesFor(Preset(ents, ",\"empty\":{},\"list\":[]"));
    Expect(Build(in, &out, &report, &why), ("a made-up preset builds: " + why).c_str());
    Expect(!Has(out, "clouds") && !Has(out, "\"act1_sky_01\"") && !report.empty() && report[0] == "clouds out: 1, hidden: 1", "the clouds out, the painted cards hidden");
    Expect(Has(out, R"J({"type":"Entity","name":"keep é \"x\"/\t\u0001 😀","id":18446744073709551617,"components":[],"v":2})J") &&
               Has(out, "\"empty\":{},\"list\":[]") && out.back() == '\n' && !Has(out, ": ") && !Has(out, ", "),
           "Python's compact json text: keys in order, a key twice, escapes, UTF-8, big ints");
    in.preset = Preset(Act2Pieces(), ",\"v\":[1.50,1e-5,1E16,-0.0,0.1,-0,100.0,2.5e-7,1234567890123456.7]");
    Expect(Build(in, &out, &report, &why) && Has(out, "\"v\":[1.5,1e-05,1e+16,-0.0,0.1,0,100.0,2.5e-07,1234567890123456.8]"), "floats as repr, ints as ints");

    // an Act 4 rock copied round its camera (30, 4, -789): 20 ahead of it, turned a half and three quarters (a quarter: within FRONT)
    ents = Act2Pieces();
    ents.push_back(Model(77, "rock_a", "data/hd/env/model/act4/lava/act4_lava_islands/rock01.model", 10.0, 4.0, -789.0));
    in.preset = Preset(ents);
    Expect(Build(in, &out, &report, &why), ("Act 4's rock: built " + why).c_str());
    // (what prepare_frontend_scene.prepare() wrote for the same preset, Python 3.14)
    Expect(Has(out, R"J({"type":"Entity","name":"rock_a_pcvr_r90","id":887559158,"components":[{"type":"TransformDefinitionComponent","name":"t_pcvr_r90","position":{"x":30.0,"y":4.0,"z":-769.0},"orientation":{"x":0.0,"y":0.7071067811865476,"z":0.0,"w":0.7071067811865476}},{"type":"ModelDefinitionComponent","name":"m_pcvr_r90","filename":"data/hd/env/model/act4/lava/act4_lava_islands/rock01.model"}]})J") &&
               Has(out, R"J({"type":"Entity","name":"rock_a_pcvr_r180","id":3541994927,"components":[{"type":"TransformDefinitionComponent","name":"t_pcvr_r180","position":{"x":50.0,"y":4.0,"z":-789.0},"orientation":{"x":0.0,"y":1.0,"z":0.0,"w":6.123233995736766e-17}})J") &&
               Has(out, R"J("name":"t_pcvr_r270","position":{"x":30.000000000000004,"y":4.0,"z":-809.0},"orientation":{"x":0.0,"y":0.7071067811865476,"z":0.0,"w":-0.7071067811865475}})J"),
           "Act 4: its rock turned round the camera a quarter, a half and three quarters, ids and names as the script's");
    bool act4 = false;
    for (const std::string& r : report) act4 = act4 || Has(r, "act 4: 1 pieces of its own, 3 copies round the camera");
    Expect(act4, "the report: Act 4's piece and its three copies");

    Expect(!Build(SourcesFor("[1,2]"), &out, &report, &why) && Has(why, "no entity list"), "not a preset: refused");
    Expect(!Build(SourcesFor("{\"entities\":["), &out, &report, &why), "not JSON: refused");
    Input missing = SourcesFor(Preset(Act2Pieces()));
    missing.sources.erase("data/hd/env/preset/act2/town/transw2.json");
    Expect(!Build(missing, &out, &report, &why) && Has(why, "transw2"), "a source preset missing: refused, as the script's KeyError");
    Expect(!Build(SourcesFor(Preset({Named("x", 1)})), &out, &report, &why) && Has(why, "ui_wall01_01"), "Act 2's wall not there: refused");
}

static void Edits(const std::wstring& here) {
    const std::map<int, std::string> e = frontendscene::EmbeddedEdits();
    Expect(e.size() == 3 && e.count(2) && e.count(3) && e.count(5), "the edits of Acts 2, 3 and 5 embedded");
    for (int act : {2, 3, 5}) {
        const std::string file = ReadAll(here + L"frontend_edits_act" + std::to_wstring(act) + L".json");
        if (!file.empty()) Expect(e.count(act) && e.at(act) == file, "an embedded edits file is the file byte for byte");
    }
}

// ---------------------------------------------------------------- the game's files

static int BuildFiles(const std::wstring& preset, const std::wstring& sourcesDir, const std::wstring& outFile, const std::wstring& modelsDir) {
    using namespace frontendscene;
    Input in;
    in.preset = ReadAll(preset);
    for (const std::string& w : SourcePresets()) {
        // the script's --tiles-from name: the part after the act folder, '/' as '__' (town/transw2.json -> town__transw2.json)
        std::string rest = w.substr(w.find("/preset/") + 8);
        rest = rest.substr(rest.find('/') + 1);
        for (size_t at = rest.find('/'); at != std::string::npos; at = rest.find('/')) rest.replace(at, 1, "__");
        const std::wstring f = sourcesDir + Wide(rest);
        if (Exists(f)) in.sources[w] = ReadAll(f);
    }
    in.edits = EmbeddedEdits();
    if (!modelsDir.empty())
        in.assets = [modelsDir](const std::string& name, std::string* bytes) {
            std::wstring p = modelsDir + Wide(name);
            for (wchar_t& c : p) if (c == L'/') c = L'\\';
            if (!Exists(p)) return false;
            *bytes = ReadAll(p);
            return true;
        };
    std::string out, why;
    std::vector<std::string> report;
    const ULONGLONG t0 = GetTickCount64();
    if (!Build(in, &out, &report, &why)) {
        printf("FAIL: not built: %s\n", why.c_str());
        ++g_fail;
        return 1;
    }
    for (const std::string& r : report) printf("  %s\n", r.c_str());
    WriteAll(outFile, out);
    printf("  %zu bytes (%llu ms)\n", out.size(), (unsigned long long)(GetTickCount64() - t0));
    return 0;
}

static int Game(const std::wstring& game, const std::wstring& into) {
    using namespace frontendscene;
    CreateDirectoryW(into.c_str(), nullptr);
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    const std::wstring modRoot = into + L"mods\\D2R-VR\\";
    CreateDirectoryW(modRoot.c_str(), nullptr);
    const std::wstring mpq = modRoot + L"D2R-VR.mpq\\";
    const std::wstring file = mpq + L"data\\hd\\env\\preset\\ui\\frontendact2.json";
    const std::wstring receipt = modRoot + L"frontend_scene.json";

    Result r = Prepare(game, L"", true);
    Expect(r.state == kNotHere && !r.wrote, "not the mod: nothing made");
    r = Prepare(game, modRoot, true);
    Expect(r.state == kNotHere && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing made");

    CreateDirectoryW(mpq.c_str(), nullptr);
    ULONGLONG t0 = GetTickCount64();
    r = Prepare(game, modRoot, true);
    printf("  %ls (%llu ms)\n", r.text.c_str(), (unsigned long long)(GetTickCount64() - t0));
    for (const std::string& line : r.report) printf("    %s\n", line.c_str());
    Expect(r.state == kMade && r.wrote && Exists(file) && Exists(receipt), "with the mod's D2R-VR.mpq: made, the receipt beside it");
    const std::string made = ReadAll(file);
    size_t clouds = 0, cloudFiles = 0;   // (the clouds' particle files stay in the dependencies, as the script leaves them)
    for (size_t at = made.find("_clouds"); at != std::string::npos; at = made.find("_clouds", at + 1)) ++clouds;
    for (size_t at = made.find("_clouds.particles"); at != std::string::npos; at = made.find("_clouds.particles", at + 1)) ++cloudFiles;
    Expect(Has(made, "_pcvr_r180") && Has(made, "pcvr_town_3_") && Has(made, "pcvr_lava_4_") && clouds == cloudFiles,
           "the scene: copies round, the town, the lava, no clouds");
    printf("  frontendact2.json: %zu bytes\n", made.size());
    WriteAll(into + L"made_frontendact2.json", made);   // (a copy in the scratch folder, to compare with the script's)
    Expect(Has(ReadAll(receipt), "mods/D2R-VR/D2R-VR.mpq/data/hd/env/preset/ui/frontendact2.json"), "the receipt names the file as campfire_scene.json does");

    r = Prepare(game, modRoot, true);
    Expect(r.state == kMade && !r.wrote && ReadAll(file) == made, "again: nothing written");
    WriteAll(file, made + " ");
    r = Prepare(game, modRoot, true);
    Expect(r.state == kMade && !r.wrote && ReadAll(file) == made + " ", "changed by the player: left as it is");
    r = Prepare(game, modRoot, false);
    Expect(r.state == kOff && Exists(file), "off with the player's own: it stays");
    DeleteFileW(file.c_str());
    r = Prepare(game, modRoot, true);
    Expect(r.state == kMade && r.wrote && ReadAll(file) == made, "made again");
    r = Prepare(game, modRoot, false);
    Expect(r.state == kOff && r.wrote && !Exists(file) && !Exists(receipt), "off: ours taken out, the receipt too");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    PythonNumbers();
    MadeUp();
    {   // the edits files beside the sources: this file's ..\..\tools\settings (CMake compiles it by its full path)
        std::wstring here = Wide(__FILE__);
        for (wchar_t& c : here) if (c == L'/') c = L'\\';
        const size_t at = here.rfind(L"\\cleanroom\\tests\\");
        Edits(at == std::wstring::npos ? std::wstring() : here.substr(0, at) + L"\\tools\\settings\\");
    }
    if (argc >= 5 && wcscmp(argv[1], L"--build") == 0) {
        BuildFiles(argv[2], Slash(argv[3]), argv[4], argc >= 6 ? Slash(argv[5]) : std::wstring());
    } else if (argc >= 3) {
        Game(Slash(argv[1]), Slash(argv[2]));
    }
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
