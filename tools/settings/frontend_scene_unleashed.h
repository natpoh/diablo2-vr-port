// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/prepare_frontend_scene.py at 1f3b994 (the same file as at 9a276e5) and the scene editor's edits beside it
// (steamvr/frontend_edits_act2.json, _act3.json, _act5.json): the main menu's act scenes (character select, the game's preset
// data/hd/env/preset/ui/frontendact2.json) made into scenes round you from the player's own game - the clouds out, the painted cards hidden,
// each act's own pieces copied round its camera, Act 1's forest and grass, Act 2's road, wall and towers along the game's own strip
// (act2/town/transw2.json), Act 3 on Kurast Docks (act3/docktown/docktown3.json) over its water, Act 4's lava and guardians, Act 5's
// highlands, then the scene editor's moves, removals and additions. Authors: Kudlatytrue (PR #583: commits a645967, 863697f, 8f25454,
// 7bd5a0a, 35395da, 7b73d3f), merged by bearstonem (PR #584, 9b7abd6). Thanks to Kudlatytrue and bearstonem for the 3D main menu.
// Taken into D2R VR Authentic: D2R_VR_Settings builds it into the mod's own data folder instead of Unleashed's installer. The rest of
// D2R VR Authentic is MIT; this file, tools/settings/frontend_scene_unleashed.cpp and tools/settings/frontend_edits_act{2,3,5}.json stay
// MPL 2.0.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace frontendscene {

enum State { kMade, kOff, kFailed, kNotHere };   // made (or already so) / unticked / could not / not the mod

struct Result {
    State state = kFailed;
    std::wstring text;    // for the log
    bool wrote = false;   // a file was written or taken out
    std::vector<std::string> report;   // the script's report lines (pieces per act), when built
};

// The game's main menu preset (read under "data:").
constexpr const char* kPreset = "data/hd/env/preset/ui/frontendact2.json";

// source_presets(): the game's presets the scene is built from besides the menu's own (TILES, TOWN), sorted.
std::vector<std::string> SourcePresets();

// A model's extent in its own space (measure(): every mesh's vertices, as podglad_modeli reads them).
struct Extent {
    double mn[3] = {0, 0, 0};
    double mx[3] = {0, 0, 0};
};
// Reads one archive file by its name (e.g. "data/hd/env/model/.../x_lod0.model"): false when it is not there.
using FileReader = std::function<bool(const std::string& name, std::string* bytes)>;
// measure(assets, model): the model's _lod0 .. _lod5 files, then the model itself, the first whose meshes read; false = None.
bool MeasureModel(const FileReader& read, const std::string& model, Extent* out);

struct Input {
    std::string preset;                          // frontendact2.json as the game has it
    std::map<std::string, std::string> sources;  // SourcePresets()' files by their path (a missing one: the build fails, as the script)
    std::map<int, std::string> edits;            // the scene editor's file per act (2, 3, 5); none: no edits for that act
    FileReader assets;                           // empty: no assets (the script's guesses for the water and lava tiles)
};

// prepare(preset_bytes, sources=..., assets=...): the file exactly as the script writes it
// ((json.dumps(preset, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')) and its report lines (with the lava
// tile's measure line first when assets measured it). False with *why when it cannot be built (the script raised).
bool Build(const Input& in, std::string* out, std::vector<std::string>* report, std::string* why);

// The scene editor's edits embedded in D2R_VR_Settings (settings.rc, RCDATA): act -> text. Empty for an act whose resource is not in
// this program.
std::map<int, std::string> EmbeddedEdits();

// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// modRoot: the mod's folder <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not installed as the mod (then
//   nothing is made). The scene goes into <modRoot>D2R-VR.mpq\data\hd\env\preset\ui\frontendact2.json, the receipt (what we wrote,
//   by hash) beside the mod as <modRoot>frontend_scene.json.
// on: off = our file taken out again (the game's own menu then); a file of that name the player made or changed stays.
Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on);

// (for the test) Python's float arithmetic the port leans on, to compare with Python itself.
namespace detail {
double PyHypot(double x, double y);   // math.hypot(x, y)
// random.Random(seed): random(), getrandbits(32), choice index (_randbelow), uniform
struct PyRandomState;
class PyRandom {
  public:
    explicit PyRandom(unsigned seed);
    ~PyRandom();
    PyRandom(const PyRandom&) = delete;
    PyRandom& operator=(const PyRandom&) = delete;
    double Random();
    unsigned GetRandBits32();
    size_t RandBelow(size_t n);
    double Uniform(double a, double b);

  private:
    PyRandomState* s_;
};
}  // namespace detail

}  // namespace frontendscene
