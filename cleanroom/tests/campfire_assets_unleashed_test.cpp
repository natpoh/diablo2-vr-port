// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// campfire_assets_unleashed_test - [campfire] enabled: D2R_VR_Settings' maker of character creation's scene
// (tools/settings/campfire_assets_unleashed.cpp, ported from D2R VR Unleashed steamvr/prepare_campfire_sky.py at 79b921a and its test
// prepare_campfire_sky_test.py). MPL 2.0.
//   Always, on made-up presets: Python's json text (keys in order, floats as repr, ints, escapes, UTF-8), the clouds taken out and nothing
//   else, no clouds refused, Harrogath's gate on the scene's, the town kept off the heroes with its ground, the scene's half mirrored.
//   campfire_assets_unleashed_test --build <charactercreate.json> <townwest.json> <out folder>: the tuned and the plain scene written as
//     tuned.json / plain.json (to compare with the script's output on the same files).
//   campfire_assets_unleashed_test <game folder> <empty scratch folder>: Settings' Prepare against the game (its archives read only,
//     everything written under the scratch folder in the mod's layout <scratch>\mods\D2R-VR): not the mod: nothing; made; again: nothing
//     written; a file the player changed: left; off: ours taken out, the receipt too.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "campfire_assets_unleashed.h"

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

// ---------------------------------------------------------------- made-up presets

// fmt_case.json and what prepare_campfire_sky.generate(surround=False) wrote for it (Python 3.14)
static const char* kFormatIn =
    R"J({"type":"Preset","name":"charactercreate","entities":[{"type":"Entity","name":"FX_UICharacterCreate_clouds_01","id":0,"components":[]},{"type":"Entity","name":"campéfire \"x\"\/\t\u0001 😀","id":18446744073709551617,"components":[{"type":"PointLightDefinitionComponent","v":[1.50,1e-5,1E16,123456789012345678,-0.0,0.1,1e400,-0,100.0,2.5e-7,1234567890123456.7,0.0001,true,null,0.30000000000000004,5e-324]}],"u":"ж"}],"empty":{},"list":[],"name":"dup"})J";
static const char* kFormatOut = R"J({
  "type": "Preset",
  "name": "dup",
  "entities": [
    {
      "type": "Entity",
      "name": "campéfire \"x\"/\t\u0001 😀",
      "id": 18446744073709551617,
      "components": [
        {
          "type": "PointLightDefinitionComponent",
          "v": [
            1.5,
            1e-05,
            1e+16,
            123456789012345678,
            -0.0,
            0.1,
            Infinity,
            0,
            100.0,
            2.5e-07,
            1234567890123456.8,
            0.0001,
            true,
            null,
            0.30000000000000004,
            5e-324
          ],
          "shadowCaster": false
        }
      ],
      "u": "ж"
    }
  ],
  "empty": {},
  "list": []
}
)J";

static std::string Preset(const std::vector<std::string>& entities, const std::string& extra = "") {
    std::string s = "{\"type\":\"Preset\",\"name\":\"charactercreate\",\"entities\":[";
    for (size_t i = 0; i < entities.size(); ++i) s += (i ? "," : "") + entities[i];
    return s + "]" + extra + "}";
}
static std::string Named(const char* name, int id) {
    return std::string("{\"type\":\"Entity\",\"name\":\"") + name + "\",\"id\":" + std::to_string(id) + ",\"components\":[]}";
}
static std::string Num(double v) {
    char b[64];
    snprintf(b, sizeof b, "%.17g", v);
    return b;
}
static std::string Model(int id, const char* name, double x, double z) {
    return "{\"type\":\"Entity\",\"name\":\"" + std::string(name) + "\",\"id\":" + std::to_string(id) +
           ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"position\":{\"x\":" + Num(x) + ",\"y\":0.0,\"z\":" + Num(z) +
           "},\"orientation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0}},{\"type\":\"ModelDefinitionComponent\",\"filename\":\"data/hd/env/model/x.model\"}]}";
}

static void MadeUp() {
    using namespace campfireassets;
    std::string out, why;
    Expect(BuildWith(kFormatIn, nullptr, {"--no-surround"}, &out, &why) && out == kFormatOut,
           "Python's json text: keys in order (a key twice: the last value, the first place), floats as repr, big ints, escapes, UTF-8");

    // test_takes_out_only_the_clouds
    const std::string three = Preset({Named("ui_skybox_characterSelect_sky", 0), Named("FX_UICharacterCreate_clouds_01", 1), Named("campfire", 2)});
    Expect(BuildWith(three, nullptr, {"--no-surround"}, &out, &why) && !Has(out, "clouds") && Has(out, "\"name\": \"ui_skybox_characterSelect_sky\"") &&
               Has(out, "\"name\": \"campfire\""),
           "only the clouds taken out");
    // test_no_clouds_keeps_the_native_preset
    Expect(!BuildWith(Preset({Named("ui_skybox_characterSelect_sky", 0)}), nullptr, {}, &out, &why) && out.empty() && Has(why, "no cloud effect"),
           "no clouds: refused");
    Expect(!Build("[1,2]", nullptr, true, &out, &why) && Has(why, "no entity list"), "not a preset: refused");
    Expect(!Build("{\"entities\":[", nullptr, true, &out, &why), "not JSON: refused");
    Expect(!Build(three, nullptr, true, &out, &why) && Has(why, "townwest"), "the town not readable: the tuned scene not made");

    const double atx = 49.2 + 1060.67 - 1002.2, atz = 190.0 + 123.94 - 107.6;   // TOWN_AT_GATE
    // test_harrogath_gate_on_the_scene_gate: the town's gate on the scene's gate (its built half: left out), a house 40 east of the fire
    const std::string clouds = Preset({Named("FX_UICharacterCreate_clouds_01", 0)});
    std::string town = "{\"entities\":[" + Model(1, "wall_gate", 49.2, 190.0) + "," + Model(2, "house", atx + 40.0, atz) + "]}";
    Expect(BuildWith(clouds, &town, {}, &out, &why) && Has(out, "\"name\": \"house_pcvr_behind\"") && !Has(out, "wall_gate"),
           "Harrogath's gate on the scene's: the gate out, the house in");
    // test_harrogath_keeps_off_the_heroes_and_brings_its_ground
    town = "{\"entities\":[" + Model(1, "near_house", atx - 30.0, atz + 18.0) + "," + Model(2, "far_house", atx - 60.0, atz + 36.0) + "],\"terrain\":" +
           Model(3, "terrain", 0.0, 0.0) + "}";
    Expect(BuildWith(clouds, &town, {}, &out, &why) && !Has(out, "near_house") && Has(out, "far_house_pcvr_behind") && Has(out, "town_ground_pcvr_behind") &&
               Has(out, "\"dependencies\": {}"),
           "the town off the heroes, its ground with it");
    Expect(BuildWith(clouds, &town, {"--no-ground"}, &out, &why) && !Has(out, "town_ground"), "--no-ground: no ground");
    // test_town_turned_behind_the_player (--mirror): the wall turned half round the fire, the light and the log by the fire stay single
    const std::string wall = "{\"type\":\"Entity\",\"name\":\"ui_wall01_01\",\"id\":7,\"components\":[{\"type\":\"TransformDefinitionComponent\",\"position\":"
                             "{\"x\":1020.67,\"y\":1.0,\"z\":123.94},\"orientation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0}},"
                             "{\"type\":\"ModelDefinitionComponent\",\"filename\":\"data/hd/env/model/ui/wall.model\"}]}";
    const std::string light = "{\"type\":\"Entity\",\"name\":\"spot\",\"id\":8,\"components\":[{\"type\":\"TransformDefinitionComponent\",\"position\":"
                              "{\"x\":1020.0,\"y\":5.0,\"z\":123.0}},{\"type\":\"SpotLightDefinitionComponent\"}]}";
    const std::string log = "{\"type\":\"Entity\",\"name\":\"log\",\"id\":9,\"components\":[{\"type\":\"TransformDefinitionComponent\",\"position\":"
                            "{\"x\":1062.0,\"y\":0.0,\"z\":124.0}},{\"type\":\"ModelDefinitionComponent\"}]}";
    Expect(BuildWith(Preset({Named("FX_UICharacterCreate_clouds_01", 0), wall, light, log}), nullptr, {"--mirror", "--sky"}, &out, &why) &&
               Has(out, "\"name\": \"ui_wall01_01_pcvr_behind\"") && !Has(out, "spot_pcvr_behind") && !Has(out, "log_pcvr_behind") &&
               Has(out, "\"x\": 1100.67") && Has(out, "\"y\": 1.0,\n            \"z\": -0.0,\n            \"w\": -0.0"),
           "--mirror: the scene's half turned behind the player");
}

// ---------------------------------------------------------------- the script's files

static int BuildFiles(const std::wstring& preset, const std::wstring& townPath, const std::wstring& outDir) {
    const std::string in = ReadAll(preset), town = ReadAll(townPath);
    std::string out, why;
    for (int tuned = 1; tuned >= 0; --tuned) {
        const ULONGLONG t0 = GetTickCount64();
        if (!campfireassets::Build(in, &town, tuned != 0, &out, &why)) {
            printf("FAIL: %s scene: %s\n", tuned ? "tuned" : "plain", why.c_str());
            return 1;
        }
        WriteAll(outDir + (tuned ? L"tuned.json" : L"plain.json"), out);
        printf("  %s: %zu bytes (%llu ms)\n", tuned ? "tuned" : "plain", out.size(), (unsigned long long)(GetTickCount64() - t0));
    }
    return 0;
}

// ---------------------------------------------------------------- the game's own

static int Game(const std::wstring& game, const std::wstring& into) {
    using namespace campfireassets;
    CreateDirectoryW(into.c_str(), nullptr);
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    const std::wstring modRoot = into + L"mods\\D2R-VR\\";
    CreateDirectoryW(modRoot.c_str(), nullptr);
    const std::wstring mpq = modRoot + L"D2R-VR.mpq\\";
    const std::wstring file = mpq + L"data\\hd\\env\\preset\\ui\\charactercreate.json";
    const std::wstring receipt = modRoot + L"campfire_scene.json";

    Result r = Prepare(game, L"", true);
    Expect(r.state == kNotHere && !r.wrote, "not the mod: nothing made");
    r = Prepare(game, modRoot, true);
    Expect(r.state == kNotHere && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing made");

    CreateDirectoryW(mpq.c_str(), nullptr);
    ULONGLONG t0 = GetTickCount64();
    r = Prepare(game, modRoot, true);
    printf("  %ls (%llu ms)\n", r.text.c_str(), (unsigned long long)(GetTickCount64() - t0));
    Expect(r.state == kMade && r.wrote && Exists(file) && Exists(receipt), "with the mod's D2R-VR.mpq: made, the receipt beside it");
    const std::string made = ReadAll(file);
    Expect(Has(made, "_pcvr_behind") && !Has(made, "\"name\": \"FX_UICharacterCreate_clouds") && Has(made, "_pave000"),
           "the tuned scene: the town round the fire, its road, no clouds");
    printf("  charactercreate.json: %zu bytes\n", made.size());

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
    MadeUp();
    if (argc >= 5 && wcscmp(argv[1], L"--build") == 0) {
        BuildFiles(argv[2], argv[3], Slash(argv[4]));
    } else if (argc >= 5 && wcscmp(argv[1], L"--build-with") == 0) {   // <preset> <town> <out file> <the script's options...>
        std::vector<std::string> options;
        for (int i = 5; i < argc; ++i) {
            std::string a;
            for (const wchar_t* c = argv[i]; *c; ++c) a += (char)*c;   // (ASCII options)
            options.push_back(a);
        }
        const std::string in = ReadAll(argv[2]), town = ReadAll(argv[3]);
        std::string out, why;
        if (campfireassets::BuildWith(in, &town, options, &out, &why)) WriteAll(argv[4], out);
        else { printf("not built: %s\n", why.c_str()); WriteAll(argv[4], "ERROR " + why); }
    }
    else if (argc >= 3) Game(Slash(argv[1]), Slash(argv[2]));
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
