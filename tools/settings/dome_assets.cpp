// dome_assets.cpp - [sky] game_sky's dome models made from the player's own game (dome_assets.h). The game took its own Summit sky card
// through our unit and never took our own .model of the same type tree (PC1 2026-10-27): so the card itself, our mesh in it.

#include "dome_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"      // the receipt and its hash, the same as the Arcane sky's
#include "ceiling_measures.h"   // [ceiling] height_from_walls, game_ceiling_beams: the rooms measured from the game's models
#include "game_ceiling.h"    // [ceiling] game_ceiling: the slab's paths, its source model, its maps and its unit
#include "granny_remesh.h"
#include "sky_atlas.h"         // [sky] game_sky: every live dome's picture in one texture, each dome its own band

namespace domeassets {
namespace {

constexpr const char* kCard = "data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/skybox01_lod1.model";
constexpr const char* kCardTexture = "data/hd/env/texture/expansion/mountaintop/mountaintop_skybox/expansion_mountaintop_skybox01_HDR.dds";
constexpr const char* kModelDir = "data/hd/env/model/d2r_vr/sky/";
// The game takes a model only from a path it knows (its own files' names): our dome at our own path never got a ModelComponent, the same
// file at the Summit card's path did and was drawn (PC1 2026-10-27 13:17, [debug] game_sky_test 4). So each dome goes to a path of the
// game's own that nothing in play shows - its developers' test models, named by no unit or preset (the textures load from our own paths).
struct Dome { const char* name; const char* texture; const char* dir; const char* file; bool test4; };
constexpr Dome kDomes[] = {
    {"act1", "data/hd/env/texture/d2r_vr/sky/sky_act1_day_HDR.dds", "data/hd/env/model/Tests/", "Barbarian_ScaleRef", false},   // [sky] game_sky
    {"test", "data/hd/env/texture/d2r_vr/sky/sky_test_day_HDR.dds", "data/hd/env/model/Tests/test_cloth/driver_mesh_test01/", "driver_mesh_test01", false},   // test 1: magenta
    {"cave", "data/hd/env/texture/d2r_vr/sky/sky_cave_day_HDR.dds", "data/hd/env/model/Tests/", "test_cube_02", false},   // [sky] game_dome_caves: black
    // acts 2 - 5: each act's own sky (its texture from make_sky_dome.py --act, with the mod's data), at more of the game's unused test paths
    {"act2", "data/hd/env/texture/d2r_vr/sky/sky_act2_day_HDR.dds", "data/hd/env/model/Tests/", "pillar01", false},
    {"act3", "data/hd/env/texture/d2r_vr/sky/sky_act3_day_HDR.dds", "data/hd/env/model/Tests/", "Pillar_02", false},
    {"act4", "data/hd/env/texture/d2r_vr/sky/sky_act4_day_HDR.dds", "data/hd/env/model/Tests/", "Pillar_03", false},
    {"act5", "data/hd/env/texture/d2r_vr/sky/sky_act5_day_HDR.dds", "data/hd/env/model/Tests/test_cloth/", "flag01", false},
    // [debug] game_sky_test 4: the magenta dome at the game's own Summit card path (only while that test is set)
    {"summit", "data/hd/env/texture/d2r_vr/sky/sky_test_day_HDR.dds", "data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/", "skybox01", true},
};
std::string RelOf(const Dome& d, int lod) { return std::string(d.dir) + d.file + "_lod" + std::to_string(lod) + ".model"; }
// Each dome's mesh made its own by this (grannyremesh::Salt): its place in the table, 1 and up
unsigned SaltOf(const Dome& d) { return (unsigned)(&d - kDomes) + 1u; }
// where the domes were before they went to the game's own paths (never taken): ours there are taken out
std::string OldRelOf(const char* name, int lod) { return std::string(kModelDir) + "sky_dome_" + name + "_lod" + std::to_string(lod) + ".model"; }
constexpr float kRadius = 800.0f;   // the dome model's own radius (vr/sky_dome.h kModelRadius)

bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFile(const std::wstring& path, std::string* out) {
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (64ll << 20);
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = out->empty() || (::ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size());
    }
    CloseHandle(f);
    return ok;
}

// Written beside itself first, then moved over the old one: a game loading it meanwhile never meets half a file.
bool WriteFile(const std::wstring& path, const void* data, size_t n) {
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = ::WriteFile(f, data, (DWORD)n, &put, nullptr) && put == n;
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}

bool MakeDirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return IsDir(dir);
}

// A file by its full name in the archives ("data:data\\hd\\...")
bool CascReadFull(HANDLE storage, const std::string& full, std::string* out) {
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, full.c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    bool ok = CascGetFileSize64(f, &size) && size < (32ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}
bool CascRead(HANDLE storage, const char* name, std::string* out) { return CascReadFull(storage, std::string("data:") + name, out); }

// [ceiling] height_from_walls: each indoor area's walls' top (shared/ceiling_measures.h) - the wall pieces its presets place, each model's
// bounds read once from its own file (lod0). Areas with no wall piece are left out (their own height stays). [ceiling] game_ceiling_dome
// and game_ceiling_worldstone_dome: each preset's dome over its highest piece when that stands kTallFactor times the room's walls' top
// (ceilmeasure::DomeOf: every piece's bounds) - the cathedral's first (slot 0), the rest in the areas' order while slots last; `why` when
// the cathedral has none.
void Measure(HANDLE storage, ceilmeasure::Measures* out, std::string* domeWhy) {
    std::vector<std::string> names, keys;   // every preset's name as the archives have it, and lower case with '/'
    const std::string root = "data:data/hd/env/preset/";
    CASC_FIND_DATA fd{};
    if (HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr)) {
        do {
            std::string k = ceilmeasure::Lower(fd.szFileName);
            for (char& c : k) if (c == '\\') c = '/';
            if (k.compare(0, root.size(), root) == 0 && k.size() > 5 && k.compare(k.size() - 5, 5, ".json") == 0) {
                names.push_back(fd.szFileName);
                keys.push_back(k.substr(root.size()));
            }
        } while (CascFindNextFile(find, &fd));
        CascFindClose(find);
    }
    struct Box { bool ok; float mn[3], mx[3]; };
    std::map<std::string, Box> boxes;   // a model's bounds
    auto boundsOf = [&](const std::string& model, float mn[3], float mx[3]) {
        if (model.size() < 7 || model.compare(model.size() - 6, 6, ".model") != 0) return false;
        auto it = boxes.find(model);
        if (it == boxes.end()) {
            Box b{false, {}, {}};
            std::string data;
            grannyremesh::File f;
            if (CascRead(storage, (model.substr(0, model.size() - 6) + "_lod0.model").c_str(), &data) && grannyremesh::Read(data, &f, nullptr) &&
                grannyremesh::Bounds(f, b.mn, b.mx))
                b.ok = true;
            it = boxes.emplace(model, b).first;
        }
        if (!it->second.ok) return false;
        std::memcpy(mn, it->second.mn, sizeof it->second.mn);
        std::memcpy(mx, it->second.mx, sizeof it->second.mx);
        return true;
    };
    out->walls.clear();
    out->domes.clear();
    *domeWhy = "no preset of the cathedral in this game";
    std::vector<ceilmeasure::Dome> cathedral, tall;
    for (const ceilmeasure::Area& area : ceilmeasure::kAreas) {
        std::vector<float> tops;
        std::vector<std::vector<ceilmeasure::Placement>> presets;   // (each preset's pieces: its dome once the walls' top is known)
        const std::string want = area.presets;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].compare(0, want.size(), want) != 0) continue;
            std::string text;
            std::vector<ceilmeasure::Placement> placed;
            if (!CascReadFull(storage, names[i], &text)) continue;
            // (only its own biome's: a folder holds its entrances' and its neighbours' too - the caves' outdoors, the palace's three)
            const std::string biome = ceilmeasure::BiomeOf(text);
            if (!biome.empty() && biome != area.biome) continue;
            if (!ceilmeasure::Placements(text, &placed)) continue;
            for (const ceilmeasure::Placement& p : placed) {
                float mn[3], mx[3];
                if (!ceilmeasure::IsWallPiece(p.model) || !boundsOf(p.model, mn, mx)) continue;
                const float top = ceilmeasure::WallTop(p, mn[1], mx[1]);
                if (top >= 0.0f) tops.push_back(top);
            }
            presets.push_back(std::move(placed));
        }
        const float h = ceilmeasure::Median(tops);
        if (h > 0.0f) out->walls.emplace_back(area.biome, h);
        if (!(h > 0.0f)) continue;   // (no walls' top: no room's height to stand over)
        const bool isCathedral = std::string(area.biome) == ceilmeasure::kCathedral;
        for (const auto& placed : presets) {
            ceilmeasure::Dome d;
            std::string why;
            if (!ceilmeasure::DomeOf(placed, boundsOf, &d, &why, ceilmeasure::kTallFactor * h, !isCathedral)) continue;   // (over the piece itself; the cathedral's on its crossing, as made first)
            d.biome = area.biome;
            if (isCathedral && cathedral.empty()) cathedral.push_back(d);
            else if (std::string(area.biome) == ceilmeasure::kWorldstoneBiome) tall.push_back(d);   // (the Worldstone's room only)
        }
    }
    if (!cathedral.empty()) {
        domeWhy->clear();
        out->domes.push_back(cathedral[0]);
    }
    // slot 1: the Worldstone - of its room's, the piece standing the most over the ceiling (the 2026-10-29 game: the Worldstone 4.6 times the
    // throne biome's 9.9; the throne room's arch 3.5, left out)
    std::stable_sort(tall.begin(), tall.end(), [&](const ceilmeasure::Dome& a, const ceilmeasure::Dome& b) {
        return a.top / ceilmeasure::WallsTopOf(*out, a.biome) > b.top / ceilmeasure::WallsTopOf(*out, b.biome);
    });
    for (const ceilmeasure::Dome& d : tall) {
        if ((int)out->domes.size() - (cathedral.empty() ? 0 : 1) >= 1) break;   // (the one)
        out->domes.push_back(d);
        out->domes.back().slot = (int)out->domes.size() - (cathedral.empty() ? 0 : 1);
    }
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII: our own texts and the file names

// The game's archives opened: Settings starts five makers on threads of their own in the same moment (the Arcane sky, the props, the boat,
// the sky dome, the ceiling) - on PC1 (2026-10-27 14:30 and 15:30) the sky dome's open failed with error 2 in that crowd 25 ms after the
// start, every start, while the ceiling's a few hundred ms later went through: its cave dome never made. The two makers here take turns
// (g_casc, held for the whole make), and an open that fails is tried again, eight times over 4 s; the last error is kept for the log.
std::mutex g_casc;
bool OpenCasc(const std::wstring& game, HANDLE* storage, DWORD* error) {
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    for (int i = 0; i < 8; ++i) {
        if (i) Sleep(500);
        *storage = nullptr;
        if (CascOpenStorage(root.c_str(), 0, storage)) return true;
        *error = GetLastError();
    }
    return false;
}

// The domes' units and their one-colour textures, made here too (PC1 2026-10-27: a DLL + Settings install without the staged data -
// "cave dome - the dome's unit NOT given to the game: ... the file is not there"): the units word for word as tools/game_sky/make_sky_dome.py
// writes them (unit_json), the plain textures as its write_dds_rgba16f (256 x 64, A16B16G16R16F, every mip to 1 x 1). The act's painted sky
// (BC6H, from the panorama's pictures) comes with the mod's data.
std::string UnitJson(const std::string& act, const std::string& model, const std::string& texture) {
    return "{\"dependencies\":{\"particles\":[],\"models\":[{\"path\":\"" + model + "\"}],\"skeletons\":[],\"animations\":[],\"textures\":[{\"path\":\"" +
           texture + "\"}],\"physics\":[],\"json\":[],\"variantdata\":[],\"objecteffects\":[],\"other\":[]},\"type\":\"UnitDefinition\",\"name\":\"d2r_vr_sky_dome_" +
           act + "\",\"entities\":[{\"type\":\"Entity\",\"name\":\"entity_root\",\"id\":1000000003,\"components\":[{\"type\":\"UnitRootComponent\",\"name\":"
           "\"component_root\",\"state_machine_filename\":\"\",\"doNotInheritRotation\":false,\"rotationOverride\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0},"
           "\"doNotUseHDHeight\":true,\"hideAllMeshWhenInOpenedMode\":false,\"onCreateEventName\":\"\",\"animations\":[]},{\"type\":"
           "\"TransformDefinitionComponent\",\"name\":\"entity_root_TransformDefinition\",\"position\":{\"x\":0.0,\"y\":0.0,\"z\":0.0},\"orientation\":"
           "{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0},\"scale\":{\"x\":1.0,\"y\":1.0,\"z\":1.0},\"inheritOnlyPosition\":false}]},{\"type\":\"Entity\",\"name\":"
           "\"entity_model\",\"id\":1000000004,\"components\":[{\"type\":\"ModelDefinitionComponent\",\"name\":\"component_model_sky_dome_" + act +
           "\",\"filename\":\"" + model + "\",\"visibleLayers\":1,\"lightMask\":0,\"shadowMask\":0,\"ghostShadows\":false,\"floorModel\":false,"
           "\"terrainBlendEnableYUpBlend\":false,\"terrainBlendMode\":1}]}]}";
}
// a half float of a value that is one exactly (0, 1, 2 ...: our plain colours)
uint16_t Half(float f) {
    if (f == 0.0f) return 0;
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return (uint16_t)(((b >> 16) & 0x8000u) | ((((b >> 23) & 0xFFu) - 127 + 15) << 10) | ((b >> 13) & 0x3FFu));
}
std::string PlainDds(float r, float g, float b) {
    const int w = 256, h = 64;
    int levels = 1;
    for (int x = w, y = h; x > 1 || y > 1; x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1) ++levels;
    uint32_t head[32] = {};
    std::memcpy(head, "DDS ", 4);
    head[1] = 124;
    head[2] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000;   // caps height width pitch pixelformat mipmapcount
    head[3] = h; head[4] = w; head[5] = w * 8; head[7] = (uint32_t)levels;
    head[19] = 32; head[20] = 0x4; head[21] = 113;      // a fourcc: 113, A16B16G16R16F
    head[27] = 0x401008;                                 // texture, complex, mipmap
    std::string out((const char*)head, sizeof head);
    const uint16_t px[4] = {Half(r), Half(g), Half(b), Half(1.0f)};
    for (int x = w, y = h, l = 0; l < levels; ++l, x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1)
        for (int i = 0; i < x * y; ++i) out.append((const char*)px, sizeof px);
    return out;
}
struct Data { const char* rel; std::string (*make)(); };
constexpr const char* kSummitModel = "data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/skybox01.model";
const Data kData[] = {
    {"data/hd/env/model/d2r_vr/sky/sky_dome_act1_unit.json",
     [] { return UnitJson("act1", "data/hd/env/model/Tests/Barbarian_ScaleRef.model", "data/hd/env/texture/d2r_vr/sky/sky_act1_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_test_unit.json",
     [] { return UnitJson("test", "data/hd/env/model/Tests/test_cloth/driver_mesh_test01/driver_mesh_test01.model", "data/hd/env/texture/d2r_vr/sky/sky_test_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_cave_unit.json",
     [] { return UnitJson("cave", "data/hd/env/model/Tests/test_cube_02.model", "data/hd/env/texture/d2r_vr/sky/sky_cave_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_act2_unit.json",
     [] { return UnitJson("act2", "data/hd/env/model/Tests/pillar01.model", "data/hd/env/texture/d2r_vr/sky/sky_act2_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_act3_unit.json",
     [] { return UnitJson("act3", "data/hd/env/model/Tests/Pillar_02.model", "data/hd/env/texture/d2r_vr/sky/sky_act3_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_act4_unit.json",
     [] { return UnitJson("act4", "data/hd/env/model/Tests/Pillar_03.model", "data/hd/env/texture/d2r_vr/sky/sky_act4_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_act5_unit.json",
     [] { return UnitJson("act5", "data/hd/env/model/Tests/test_cloth/flag01.model", "data/hd/env/texture/d2r_vr/sky/sky_act5_day_HDR.dds"); }},
    {"data/hd/env/model/d2r_vr/sky/sky_dome_summit_unit.json", [] { return UnitJson("summit", kSummitModel, kCardTexture); }},   // [debug] game_sky_test 2, 4
    {"data/hd/env/model/d2r_vr/sky/sky_dome_summitours_unit.json",                                                              // test 3
     [] { return UnitJson("summit", "data/hd/env/model/Tests/test_cloth/driver_mesh_test01/driver_mesh_test01.model", kCardTexture); }},
    {"data/hd/env/texture/d2r_vr/sky/sky_test_day_HDR.dds", [] { return PlainDds(2.0f, 0.0f, 2.0f); }},   // magenta
    {"data/hd/env/texture/d2r_vr/sky/sky_cave_day_HDR.dds", [] { return PlainDds(0.0f, 0.0f, 0.0f); }},   // black
};

// The first prototype's model (tools/game_sky/make_sky_dome.py, never taken by the game): one uncompressed section, ours
bool FirstPrototype(const std::string& data) {
    grannyremesh::File f;
    return grannyremesh::Read(data, &f, nullptr) && f.sec.size() == 1 && data.find("tools/game_sky/make_sky_dome.py") != std::string::npos;
}

}  // namespace

namespace {
// A table of the game's (tab-separated, its first line the columns' names): each row by column name
std::vector<std::map<std::string, std::string>> Table(const std::string& text) {
    std::vector<std::map<std::string, std::string>> rows;
    std::vector<std::string> cols;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cells;
        size_t c = 0;
        for (;;) {
            const size_t t = line.find('\t', c);
            cells.push_back(line.substr(c, t == std::string::npos ? std::string::npos : t - c));
            if (t == std::string::npos) break;
            c = t + 1;
        }
        if (cols.empty()) { cols = cells; continue; }
        std::map<std::string, std::string> row;
        for (size_t i = 0; i < cols.size() && i < cells.size(); ++i) row[cols[i]] = cells[i];
        rows.push_back(row);
    }
    return rows;
}
std::string Path(std::string p) {   // a tile or preset path as the HD presets have it: lower case, '/'
    for (char& c : p) c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
    return p;
}
// A level's name without its number, letter or "Treasure" at the end ("Act 2 - Basement 1" -> "Act 2 - Basement")
std::string Stem(const std::string& name) {
    std::vector<std::string> words;
    size_t c = 0;
    while (c < name.size()) {
        const size_t sp = name.find(' ', c);
        const std::string w = name.substr(c, sp == std::string::npos ? std::string::npos : sp - c);
        if (!w.empty()) words.push_back(w);
        if (sp == std::string::npos) break;
        c = sp + 1;
    }
    while (words.size() > 1) {
        const std::string& w = words.back();
        bool tail = w == "Treasure" || (w.size() == 1 && std::isupper((unsigned char)w[0]));
        if (!tail && std::isdigit((unsigned char)w[0])) {
            tail = true;
            for (size_t i = 1; i < w.size(); ++i) tail = tail && (std::isdigit((unsigned char)w[i]) || (i + 1 == w.size() && std::isupper((unsigned char)w[i])));
        }
        if (!tail) break;
        words.pop_back();
    }
    std::string s;
    for (const std::string& w : words) s += (s.empty() ? "" : " ") + w;
    return s;
}
}  // namespace

std::map<std::string, std::set<int>> LevelsByBiome(const std::wstring& game, std::map<int, std::string>* names, std::string* why,
                                                   std::set<std::string>* inside) {
    std::map<std::string, std::set<int>> out;
    names->clear();
    std::lock_guard<std::mutex> one(g_casc);
    HANDLE storage = nullptr;
    DWORD error = 0;
    if (!OpenCasc(game, &storage, &error)) { *why = "the game's archives did not open (" + std::to_string(error) + ")"; return out; }
    std::string levelsText, typesText, prestText;
    const bool read = CascRead(storage, "data/global/excel/levels.txt", &levelsText) && CascRead(storage, "data/global/excel/lvltypes.txt", &typesText) &&
                      CascRead(storage, "data/global/excel/lvlprest.txt", &prestText);
    // every preset's biome, by its path under data/hd/env/preset/ (lower case, ".json")
    std::map<std::string, std::string> biomeOf;
    const std::string root = "data:data/hd/env/preset/";
    CASC_FIND_DATA fd{};
    if (HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr)) {
        do {
            const std::string k = Path(fd.szFileName);
            if (k.compare(0, root.size(), root) != 0 || k.size() < 5 || k.compare(k.size() - 5, 5, ".json") != 0) continue;
            std::string text;
            if (!CascReadFull(storage, fd.szFileName, &text)) continue;
            const size_t key = text.find("\"biomeFilename\"");
            if (key == std::string::npos) continue;
            const size_t q1 = text.find('"', text.find(':', key) + 1), q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            std::string b = text.substr(q1 + 1, q2 - q1 - 1);
            b = b.substr(b.find_last_of('/') + 1);
            if (b.size() > 5 && b.compare(b.size() - 5, 5, ".json") == 0) b.resize(b.size() - 5);
            if (!b.empty()) biomeOf[k.substr(root.size())] = b;
        } while (CascFindNextFile(find, &fd));
        CascFindClose(find);
    }
    CascCloseStorage(storage);
    if (!read || biomeOf.empty()) { *why = "the game's level tables or presets did not read"; return out; }
    const auto levels = Table(levelsText), types = Table(typesText), prests = Table(prestText);
    auto filesOf = [](const std::map<std::string, std::string>& row) {
        std::vector<std::string> f;
        for (int i = 1; i <= 6; ++i) {
            const auto it = row.find("File" + std::to_string(i));
            if (it != row.end() && !it->second.empty() && it->second != "0") {
                std::string p = Path(it->second);
                if (p.size() > 4 && p.compare(p.size() - 4, 4, ".ds1") == 0) p = p.substr(0, p.size() - 4) + ".json";
                f.push_back(p);
            }
        }
        return f;
    };
    auto dirOf = [](const std::string& p) { const size_t s = p.find_last_of('/'); return s == std::string::npos ? std::string() : p.substr(0, s); };
    for (const auto& lv : levels) {
        const auto idIt = lv.find("Id");
        if (idIt == lv.end() || idIt->second.empty() || !std::isdigit((unsigned char)idIt->second[0])) continue;
        const int id = std::atoi(idIt->second.c_str());
        if (id <= 0) continue;
        (*names)[id] = lv.count("LevelName") ? lv.at("LevelName") : std::string();
        std::set<std::string> biomes;
        // a level of presets of its own (lvlprest's LevelId): theirs
        for (const auto& p : prests)
            if (p.count("LevelId") && std::atoi(p.at("LevelId").c_str()) == id)
                for (const std::string& f : filesOf(p)) if (biomeOf.count(f)) biomes.insert(biomeOf[f]);
        if (biomes.empty()) {   // a maze: the presets of its tiles' folder (lvltypes' File 1), those named as the level if any are
            std::string folder;
            for (const auto& t : types)
                if (t.count("Id") && lv.count("LevelType") && t.at("Id") == lv.at("LevelType") && t.count("File 1")) folder = dirOf(Path(t.at("File 1")));
            const std::string stem = Stem(lv.count("Name") ? lv.at("Name") : std::string());
            for (const auto& p : prests) {
                if (!p.count("LevelId") || std::atoi(p.at("LevelId").c_str()) != 0 || !p.count("Name")) continue;
                if (p.at("Name").compare(0, stem.size() + 1, stem + " ") != 0) continue;
                for (const std::string& f : filesOf(p)) if (dirOf(f) == folder && biomeOf.count(f)) biomes.insert(biomeOf[f]);
            }
            if (biomes.empty() && !folder.empty())
                for (const auto& kv : biomeOf) if (dirOf(kv.first) == folder) biomes.insert(kv.second);
        }
        for (const std::string& b : biomes) out[b].insert(id);
        if (inside && lv.count("*IsInside") && lv.at("*IsInside") == "1") inside->insert(biomes.begin(), biomes.end());
    }
    return out;
}

std::set<std::string> InsideBiomes(const std::wstring& game) {
    std::map<int, std::string> names;
    std::string why;
    std::set<std::string> inside;
    LevelsByBiome(game, &names, &why, &inside);
    return inside;
}

std::vector<std::string> FloorsOf(const std::wstring& game, const std::string& biome) {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> one(g_casc);
    HANDLE storage = nullptr;
    DWORD error = 0;
    if (!OpenCasc(game, &storage, &error)) return out;
    std::string text;
    const bool read = CascRead(storage, ("data/hd/env/biome/" + biome + ".json").c_str(), &text);
    CascCloseStorage(storage);
    const size_t ultra = read ? text.find("\"terrainDataUltra\"") : std::string::npos;
    if (ultra == std::string::npos) return out;
    const size_t end = text.find("\"terrainDataHigh\"", ultra);
    for (size_t key = text.find("\"Albedo\"", ultra); key != std::string::npos && key < end; key = text.find("\"Albedo\"", key + 1)) {
        const size_t q1 = text.find('"', text.find(':', key) + 1), q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        out.push_back(text.substr(q1 + 1, q2 - q1 - 1));
    }
    return out;
}

std::vector<std::string> SkyModelPaths() {
    std::vector<std::string> out;
    for (const Dome& d : kDomes)
        for (int lod = 0; lod < 5; ++lod) out.push_back(RelOf(d, lod));
    return out;
}

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, bool summitPath, bool closed) {
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"dome.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: no game sky dome");
        return r;
    }
    const std::wstring receiptPath = modRoot + L"sky_dome.json";
    std::string last;
    ReadFile(receiptPath, &last);
    int oldOut = 0;   // the domes' first places (data/hd/env/model/d2r_vr/sky/sky_dome_*_lod*.model): ours there out, never taken by the game
    for (const char* name : {"act1", "test"})
        for (int lod = 0; lod < 5; ++lod) {
            const std::string rel = OldRelOf(name, lod);
            std::wstring path = mpq + Wide(rel);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
            if (ReadFile(path, &old) && ((!lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash) || FirstPrototype(old)) &&
                DeleteFileW(path.c_str()))
                ++oldOut;
        }
    if (oldOut) r.wrote = true;
    if (!on) {   // unticked: ours out again, nothing made (PC1 2026-10-27 12:40: "bad file" entering a game - kept out of the way while it is looked into)
        int out = 0;
        for (const Dome& d : kDomes)
            for (int lod = 0; lod < 5; ++lod) {
                const std::string rel = RelOf(d, lod);
                std::wstring path = mpq + Wide(rel);
                for (wchar_t& c : path) if (c == L'/') c = L'\\';
                std::string old;
                const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
                if (ReadFile(path, &old) && ((!lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash) || FirstPrototype(old)) &&
                    DeleteFileW(path.c_str()))
                    ++out;
            }
        std::vector<std::string> ours;   // the units, the plain textures and the domes' atlas
        for (const Data& x : kData) ours.push_back(x.rel);
        ours.push_back(skyatlas::kAtlas);
        for (const std::string& rel : ours) {
            std::wstring path = mpq + Wide(rel);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
            if (ReadFile(path, &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(path.c_str())) ++out;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.state = kOff;
        r.wrote = out > 0;
        r.text = lang::TX(L"dome.off", L"off ([sky] game_sky): no dome models made; {n} of ours taken out", {{L"n", std::to_wstring(out)}});
        return r;
    }
    std::lock_guard<std::mutex> one(g_casc);
    HANDLE storage = nullptr;
    DWORD error = 0;
    if (!OpenCasc(game, &storage, &error)) {
        r.text = lang::TX(L"dome.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start", {{L"error", std::to_wstring(error)}});
        return r;
    }
    std::string card;
    const bool read = CascRead(storage, kCard, &card);
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!read) {
        r.text = lang::TX(L"dome.no_card", L"the Arreat Summit's sky model is not in this game: no game sky dome");
        return r;
    }
    const grannyremesh::Mesh mesh = grannyremesh::DomeMesh(kRadius, 0.0f, 1.0f, closed);
    std::vector<arcanesky::Written> written;
    int kept = 0;
    int out = 0;
    // [sky] game_sky: the live domes' pictures in one atlas (shared/sky_atlas.h), each dome its own band - Serg on 0.189: loaded in act 1,
    // act 2's own dome drew act 1's sky. The acts' pictures come with the mod's data; without them each dome names its own picture as before.
    std::string atlas, atlasWhy;
    {
        std::vector<std::string> acts(skyatlas::kActBands);
        bool all = true;
        for (int i = 0; i < skyatlas::kActBands; ++i) {
            std::wstring path = mpq + Wide(std::string("data/hd/env/texture/d2r_vr/sky/sky_") + skyatlas::kBands[i] + "_day_HDR.dds");
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            all = ReadFile(path, &acts[i]) && all;
        }
        if (!all) atlasWhy = "the acts' sky pictures are not in the mod";
        else if (skyatlas::Make(acts, &atlas, &atlasWhy)) {
            const std::string rel = skyatlas::kAtlas, name = "mods/D2R-VR/D2R-VR.mpq/" + rel;
            std::wstring path = mpq + Wide(rel);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            std::string old;
            const bool exists = ReadFile(path, &old);
            if (!arcanesky::MayReplace(exists, old, atlas, arcanesky::ReceiptValue(last, name))) {
                ++kept;   // the player's own: left as it is - and used as it is
            } else {
                if (!exists || old != atlas) {
                    if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, atlas.data(), atlas.size())) {
                        atlasWhy = "could not write it (error " + std::to_string(GetLastError()) + ")";
                        atlas.clear();
                    } else {
                        r.wrote = true;
                    }
                }
                if (!atlas.empty()) written.push_back({name, arcanesky::Hash(atlas.data(), atlas.size())});
            }
        }
    }
    const bool atlasOk = !atlas.empty();
    for (const Dome& d : kDomes)
        for (int lod = 0; lod < 5; ++lod) {
            if (d.test4 && !summitPath) {   // the Summit's own path: ours taken out again unless the test asks (a file the player put there stays)
                const std::string rel = RelOf(d, lod);
                std::wstring path = mpq + Wide(rel);
                for (wchar_t& c : path) if (c == L'/') c = L'\\';
                std::string old;
                const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
                if (ReadFile(path, &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(path.c_str())) {
                    ++out;
                    r.wrote = true;
                }
                continue;
            }
            grannyremesh::File f;
            std::string why;
            // each dome's material and its texture map named its own: the card's "GLSLShader1" / "file2" in every dome, the game took the first
            // dome's material for the next - the black cave dome drawn with act 1's sky (PC1 2026-10-27 16:42, the act's dome loaded at 16:40)
            // The material's names alone did not do it (PC1 2026-10-28 14:29: the cave dome drawn - one draw of its mesh a frame, the act's dome
            // put away - still with act 1's sky): every name that says which model this is now its own - the model and its bone, the mesh,
            // the art file and the map's source picture.
            const std::string own = std::string("d2rvr_sky_") + d.name;
            const int band = atlasOk ? skyatlas::BandOf(d.name) : -1;   // (a live dome: its band of the atlas; the debug domes their own picture)
            const grannyremesh::Renames names{
                {kCardTexture, band >= 0 ? std::string(skyatlas::kAtlas) : std::string(d.texture)},
                {"GLSLShader1", own + "_material"},
                {"file2", own + "_map"},
                {"skybox01", own},
                {"skybox0Shape1", own + "_shape"},
                {"X:/assets/env/model/expansion/mountaintop/expansion_mountaintop_skybox/expansion_mountaintop_skybox.ma", "d2r_vr/" + own + ".ma"},
                {"X:/branches/master/hd/data/env/texture/expansion/mountaintop/mountaintop_skybox/expansion_mountaintop_skybox01_HDR.hdr",
                 "d2r_vr/" + own + "_HDR.hdr"}};
            // ... and its mesh's bytes its own (Serg on 0.188: every act drew act 2's sky - the first dome loaded - with every name above
            // its own already): the same dome in every one, the game shares it by what it holds (grannyremesh::Salt; the same triangles)
            grannyremesh::Mesh ownMesh = mesh;
            if (band >= 0) {   // its uv in its own band
                float from = 0.0f, to = 1.0f;
                skyatlas::BandRange(band, &from, &to);
                ownMesh = grannyremesh::DomeMesh(kRadius, from, to, closed);
            }
            grannyremesh::Salt(&ownMesh, SaltOf(d));
            if (!grannyremesh::Read(card, &f, &why) || !grannyremesh::Remesh(f, ownMesh, names, lod, nullptr, &why)) {
                r.text = lang::TX(L"dome.refused", L"the game's sky model is not as known ({why}): no game sky dome", {{L"why", Wide(why)}});
                return r;
            }
            const std::string made = grannyremesh::Write(f);
            const std::string rel = RelOf(d, lod);
            const std::string name = "mods/D2R-VR/D2R-VR.mpq/" + rel;   // (the receipt's, '/' between the parts)
            std::wstring path = mpq + Wide(rel);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            std::string old;
            const bool exists = ReadFile(path, &old);
            if (!arcanesky::MayReplace(exists, old, made, arcanesky::ReceiptValue(last, name)) && !FirstPrototype(old)) {
                ++kept;   // the player's own: left as it is
                continue;
            }
            if (!exists || old != made) {
                if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, made.data(), made.size())) {
                    r.text = lang::TX(L"dome.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\{file} (error {error})",
                                      {{L"file", Wide(rel.substr(rel.find_last_of('/') + 1))}, {L"error", std::to_wstring(GetLastError())}});
                    return r;
                }
                r.wrote = true;
            }
            written.push_back({name, arcanesky::Hash(made.data(), made.size())});
        }
    for (const Data& x : kData) {   // the units and the plain textures
        std::string made = x.make();
        for (int b = 0; atlasOk && b < skyatlas::kBandCount; ++b) {   // a live dome's unit names the atlas (its model does)
            if (std::string(x.rel) != std::string("data/hd/env/model/d2r_vr/sky/sky_dome_") + skyatlas::kBands[b] + "_unit.json") continue;
            const std::string own = std::string("data/hd/env/texture/d2r_vr/sky/sky_") + skyatlas::kBands[b] + "_day_HDR.dds";
            for (size_t at = made.find(own); at != std::string::npos; at = made.find(own)) made.replace(at, own.size(), skyatlas::kAtlas);
        }
        const std::string rel = x.rel;
        const std::string name = "mods/D2R-VR/D2R-VR.mpq/" + rel;
        std::wstring path = mpq + Wide(rel);
        for (wchar_t& c : path) if (c == L'/') c = L'\\';
        std::string old;
        const bool exists = ReadFile(path, &old);
        // (one the staging copied in, the same bytes, is ours; another one the player's)
        if (exists && old != made && !arcanesky::MayReplace(exists, old, made, arcanesky::ReceiptValue(last, name))) {
            ++kept;
            continue;
        }
        if (!exists || old != made) {
            if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, made.data(), made.size())) {
                r.text = lang::TX(L"dome.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\{file} (error {error})",
                                  {{L"file", Wide(rel.substr(rel.find_last_of('/') + 1))}, {L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        written.push_back({name, arcanesky::Hash(made.data(), made.size())});
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), written);
    if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = lang::TX(L"dome.no_receipt", L"could not write mods\\D2R-VR\\sky_dome.json");
        return r;
    }
    r.state = kMade;
    r.text = r.wrote ? lang::TX(L"dome.made", L"made from your game: the sky dome's models from the Arreat Summit's sky (mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game")
                     : lang::TX(L"dome.ready", L"ready: the sky dome's models (mods\\D2R-VR\\D2R-VR.mpq)");
    if (kept) r.text += L" - " + lang::TX(L"dome.kept", L"{n} of them changed by you: left as they are", {{L"n", std::to_wstring(kept)}});
    r.text += L" - " + (atlasOk ? lang::TX(L"dome.atlas", L"every act's sky and the cave's black in one picture, each dome its own band of it")
                                : lang::TX(L"dome.no_atlas", L"each dome its own picture ({why})", {{L"why", Wide(atlasWhy)}}));
    if (summitPath) r.text += L" - [debug] game_sky_test 4: the magenta dome also at the game's own Summit card path (in the mod only)";
    else if (out) r.text += L" - ours taken out of the game's Summit card path again";
    return r;
}

Result PrepareCeiling(const std::wstring& game, const std::wstring& modRoot, bool on) {
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"ceiling.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: no game cave ceiling");
        return r;
    }
    // ours: every kind's slab - five models and a unit - and a cave's wall at its rim ([ceiling] game_ceiling_wall: made with them, so its
    // tick is live in the game) - each by its path in the mod and the receipt's name ('/' between the parts). `lod` -1: the unit.
    // And the barracks' beams ([ceiling] game_ceiling_beams: kind -1, five models and a unit) and the rooms' measures (kind -2: the walls'
    // top, the beams' frame - [ceiling] height_from_walls), made with them so the ticks are live.
    struct Piece { std::string rel; int kind; bool wall; int lod; };
    std::vector<Piece> pieces;
    for (int k = 0; k < gameceiling::kKindCount; ++k)
        for (const bool wall : {false, true}) {
            const char* file = wall ? gameceiling::kKinds[k].wall : gameceiling::kKinds[k].slab;
            if (!file) continue;
            for (int lod = 0; lod < 5; ++lod) pieces.push_back({gameceiling::LodPath(file, lod), k, wall, lod});
            pieces.push_back({gameceiling::UnitPath(k, wall), k, wall, -1});
        }
    for (int lod = 0; lod < 5; ++lod) pieces.push_back({gameceiling::LodPath(gameceiling::kBeamModelFile, lod), -1, false, lod});
    pieces.push_back({gameceiling::kBeamUnit, -1, false, -1});
    // and the vaults ([ceiling] game_ceiling_dome: the cathedral's, slot 0; game_ceiling_worldstone_dome: the rest - kind -10 - slot, five
    // models and a unit each)
    for (int slot = 0; slot < gameceiling::kVaultSlots; ++slot) {
        for (int lod = 0; lod < 5; ++lod) pieces.push_back({gameceiling::LodPath(gameceiling::kVaultModelFiles[slot], lod), -10 - slot, false, lod});
        pieces.push_back({gameceiling::kVaultUnits[slot], -10 - slot, false, -1});
    }
    pieces.push_back({ceilmeasure::kFile, -2, false, -1});
    std::vector<std::string> rels;
    for (const Piece& p : pieces) rels.push_back(p.rel);
    auto pathOf = [&](const std::string& rel) {
        std::wstring p = mpq + Wide(rel);
        for (wchar_t& c : p) if (c == L'/') c = L'\\';
        return p;
    };
    const std::wstring receiptPath = modRoot + L"cave_ceiling.json";
    std::string last;
    ReadFile(receiptPath, &last);
    if (!on) {   // unticked: ours out again (by the receipt's hashes - a file the player made or changed stays), nothing made
        int out = 0;
        for (const std::string& rel : rels) {
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
            if (ReadFile(pathOf(rel), &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(pathOf(rel).c_str()))
                ++out;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.state = kOff;
        r.wrote = out > 0;
        r.text = lang::TX(L"ceiling.off", L"off ([ceiling] game_ceiling): no cave ceiling model made; {n} of ours taken out", {{L"n", std::to_wstring(out)}});
        return r;
    }
    std::lock_guard<std::mutex> one(g_casc);
    HANDLE storage = nullptr;
    DWORD error = 0;
    if (!OpenCasc(game, &storage, &error)) {
        r.text = lang::TX(L"ceiling.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start", {{L"error", std::to_wstring(error)}});
        return r;
    }
    std::string rock;
    const bool read = CascRead(storage, gameceiling::kSource, &rock);
    std::vector<bool> hasMaps(gameceiling::kKindCount, true);   // a kind whose maps this game lacks is not made (our vault stays there)
    for (int k = 0; k < gameceiling::kKindCount; ++k)
        for (const char* which : {"ALB", "NRM", "ORM"}) {
            HANDLE f = nullptr;
            if (CascOpenFile(storage, ("data:" + gameceiling::MapPath(gameceiling::kKinds[k], which)).c_str(), 0, CASC_OPEN_BY_NAME, &f)) CascCloseFile(f);
            else hasMaps[k] = false;
        }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    const std::string build = haveBuild ? std::to_string(product.BuildNumber) : std::string();
    // the barracks' frame, each of its LOD files (none: no beams in this game)
    std::string frames[5];
    bool haveFrames = true;
    for (int lod = 0; lod < 5; ++lod)
        haveFrames = CascRead(storage, (std::string(gameceiling::kBeamSource) + "_lod" + std::to_string(lod) + ".model").c_str(), &frames[lod]) && haveFrames;
    // the rooms' walls' top: measured once a game build (kept from the last make while the game is the same and our file unchanged)
    // (and the cathedral's dome with them)
    ceilmeasure::Measures measures;
    std::string domeWhy;
    {
        std::string old;
        ceilmeasure::Measures had;
        const std::string name = std::string("mods/D2R-VR/D2R-VR.mpq/") + ceilmeasure::kFile;
        if (!build.empty() && arcanesky::ReceiptValue(last, "game_build") == build && ReadFile(pathOf(ceilmeasure::kFile), &old) &&
            arcanesky::Hash(old.data(), old.size()) == arcanesky::ReceiptValue(last, name) && ceilmeasure::Parse(old, &had) && !had.walls.empty() &&
            had.dropped == 0) {   // (a make that dropped a dome: measured again, the dome back once its vault can be written)
            measures.walls = had.walls;
            measures.domes = had.domes;
            bool cathedral = false;
            for (const ceilmeasure::Dome& d : had.domes) cathedral = cathedral || d.slot == 0;
            if (!cathedral) domeWhy = "none in this game's cathedral (measured before)";
        } else {
            Measure(storage, &measures, &domeWhy);
            for (ceilmeasure::Dome& d : measures.domes) ceilmeasure::AsWritten(&d);   // (its vault the same next time, from the file)
        }
    }
    CascCloseStorage(storage);
    if (!read) {
        r.text = lang::TX(L"ceiling.no_rock", L"act 1's cave rock model is not in this game: no game cave ceiling");
        return r;
    }
    // the beams' grid: as many whole frames as one model holds, at the size the frame repeats at (lod0's bounds); every LOD the same grid
    grannyremesh::Mesh beamGrids[5];
    if (haveFrames) {
        grannyremesh::Mesh one;
        grannyremesh::File f;
        std::string why;
        ceilmeasure::Beam& b = measures.beam;
        haveFrames = grannyremesh::Read(frames[0], &f, &why) && grannyremesh::MeshOf(f, &one, &why);
        if (haveFrames) {
            b.periodX = one.mx[0] - one.mn[0];
            b.periodZ = one.mx[2] - one.mn[2];
            b.bottom = one.mn[1];
            b.top = one.mx[1];
            haveFrames = ceilmeasure::BeamGrid((unsigned)one.v.size(), b.periodX, b.periodZ, &b.cols, &b.rows);
        }
        for (int lod = 0; haveFrames && lod < 5; ++lod) {
            grannyremesh::File fl;
            haveFrames = grannyremesh::Read(frames[lod], &fl, &why) && grannyremesh::MeshOf(fl, &one, &why) &&
                         grannyremesh::GridMesh(one, b.cols, b.rows, b.periodX, b.periodZ, &beamGrids[lod]);
        }
        measures.haveBeam = haveFrames;
        if (haveFrames) b.indices = (unsigned)beamGrids[0].idx.size();
    }
    // the vaults: each the slab over its preset with its dome in it, made for the pillars' top, its stone its room's kind's (the cathedral's:
    // the cathedral's); a dome whose room's stone is not in this game, or whose vault is more than one model holds, is dropped
    grannyremesh::Mesh vaultMeshes[gameceiling::kVaultSlots];
    int vaultKinds[gameceiling::kVaultSlots];
    for (int& k : vaultKinds) k = -1;
    std::string tallWhy;
    for (auto it = measures.domes.begin(); it != measures.domes.end();) {
        ceilmeasure::Dome& d = *it;
        const int kind = gameceiling::KindOf(d.slot == 0 ? std::string(gameceiling::kVaultBiome) : d.biome);
        std::string why;
        if (d.slot < 0 || d.slot >= gameceiling::kVaultSlots || kind < 0) why = "no ceiling kind of its room";
        else if (!hasMaps[kind]) why = "its room's stone is not in this game";
        else {
            vaultMeshes[d.slot] = grannyremesh::VaultMesh(gameceiling::kTile, gameceiling::VaultHalf(d.half), gameceiling::kTile / gameceiling::kVaultCells,
                                                         d.radius, d.rise, 2.0f * gameceiling::kTile / gameceiling::kVaultCells);
            if (vaultMeshes[d.slot].v.empty()) why = "its vault is more than one model holds";
        }
        if (why.empty()) {
            vaultKinds[d.slot] = kind;
            d.indices = (unsigned)vaultMeshes[d.slot].idx.size();
            ++it;
            continue;
        }
        if (d.slot == 0) domeWhy = why == "its room's stone is not in this game" ? "the cathedral's stone is not in this game" : why;
        else tallWhy = d.biome + ": " + why;
        it = measures.domes.erase(it);
    }
    auto domeIn = [&](int slot) -> const ceilmeasure::Dome* {
        for (const ceilmeasure::Dome& d : measures.domes) if (d.slot == slot) return &d;
        return nullptr;
    };
    const grannyremesh::Mesh wallMesh = grannyremesh::WallMesh(gameceiling::kWallRadius, gameceiling::kWallHeight, gameceiling::kWallSegments,
                                                               gameceiling::kWallRows, gameceiling::kTile);
    std::vector<arcanesky::Written> written;
    int kept = 0, kinds = 0;
    std::wstring missing;
    for (int k = 0; k < gameceiling::kKindCount; ++k) {
        if (hasMaps[k]) ++kinds;
        else missing += (missing.empty() ? L"" : L", ") + Wide(gameceiling::kKinds[k].name);
    }
    int meshKind = -1;
    grannyremesh::Mesh mesh;
    std::string beamWhy, vaultWhy;
    int vaultFailed = -1;   // (a vault's model refused: its dome taken out of the measures, the last piece)
    bool vaultKept[gameceiling::kVaultSlots] = {};   // a vault's file there not ours (kept): its dome taken out too - the game would draw that file
    for (const Piece& piece : pieces) {
        std::string made;
        if (piece.kind == -2) {   // the measures (the last piece: the beams' and the vault's files are written by now)
            if (!beamWhy.empty()) measures.haveBeam = false;
            if (!vaultWhy.empty() && vaultFailed >= 0) {
                for (auto it = measures.domes.begin(); it != measures.domes.end(); ++it)
                    if (it->slot == vaultFailed) { measures.domes.erase(it); break; }
                if (vaultFailed == 0) domeWhy = vaultWhy;
            }
            for (auto it = measures.domes.begin(); it != measures.domes.end();) {
                if (!vaultKept[it->slot]) { ++it; continue; }
                ++measures.dropped;
                if (it->slot == 0) domeWhy = "a file of its vault there is not ours (left as it is)";
                else tallWhy = it->biome + ": a file of its vault there is not ours (left as it is)";
                it = measures.domes.erase(it);
            }
            made = ceilmeasure::Format(measures);
        } else if (piece.kind <= -10 && !domeIn(-10 - piece.kind)) {
            continue;
        } else if (piece.kind <= -10 && piece.lod >= 0) {   // a vault: the rock model with its mesh and its room's stone
            const int slot = -10 - piece.kind;
            const gameceiling::Kind& kind = gameceiling::kKinds[vaultKinds[slot]];
            const grannyremesh::Renames maps = {{gameceiling::kMaps[0].from, gameceiling::MapPath(kind, "ALB")},
                                                {gameceiling::kMaps[1].from, gameceiling::MapPath(kind, "NRM")},
                                                {gameceiling::kMaps[2].from, gameceiling::MapPath(kind, "ORM")}};
            grannyremesh::File f;
            std::string why;
            if (!grannyremesh::Read(rock, &f, &why) || !grannyremesh::Remesh(f, vaultMeshes[slot], maps, piece.lod, nullptr, &why)) {
                vaultWhy = why;
                vaultFailed = slot;
                continue;
            }
            made = grannyremesh::Write(f);
        } else if (piece.kind <= -10) {
            const int slot = -10 - piece.kind;
            made = gameceiling::VaultUnitJson(slot, domeIn(slot)->biome);
        } else if (piece.kind == -1 && !haveFrames) {
            continue;
        } else if (piece.kind == -1 && piece.lod >= 0) {   // the frame's own file (its material and maps), its mesh the grid of it
            grannyremesh::File f;
            std::string why;
            if (!grannyremesh::Read(frames[piece.lod], &f, &why) || !grannyremesh::Remesh(f, beamGrids[piece.lod], {}, piece.lod, nullptr, &why)) {
                beamWhy = why;
                continue;
            }
            made = grannyremesh::Write(f);
        } else if (piece.kind == -1) {
            made = gameceiling::BeamUnitJson();
        } else if (!hasMaps[piece.kind]) {
            continue;
        } else if (piece.lod >= 0) {
            const gameceiling::Kind& kind = gameceiling::kKinds[piece.kind];
            if (!piece.wall && meshKind != piece.kind) {   // (each kind its own relief, and its own bytes: the flat kinds' slabs are one mesh)
                mesh = grannyremesh::CeilingMesh(gameceiling::kTile, gameceiling::kTilesHalf, gameceiling::kCells, kind.relief);
                grannyremesh::Salt(&mesh, gameceiling::SaltOf(piece.kind, false));
                meshKind = piece.kind;
            }
            grannyremesh::Mesh ownWall;
            if (piece.wall) {
                ownWall = wallMesh;
                grannyremesh::Salt(&ownWall, gameceiling::SaltOf(piece.kind, true));
            }
            const grannyremesh::Renames maps = {{gameceiling::kMaps[0].from, gameceiling::MapPath(kind, "ALB")},   // the rock's maps: the kind's
                                                {gameceiling::kMaps[1].from, gameceiling::MapPath(kind, "NRM")},
                                                {gameceiling::kMaps[2].from, gameceiling::MapPath(kind, "ORM")}};
            grannyremesh::File f;
            std::string why;
            if (!grannyremesh::Read(rock, &f, &why) || !grannyremesh::Remesh(f, piece.wall ? ownWall : mesh, maps, piece.lod, nullptr, &why)) {
                r.text = lang::TX(L"ceiling.refused", L"the game's cave rock model is not as known ({why}): no game cave ceiling", {{L"why", Wide(why)}});
                return r;
            }
            made = grannyremesh::Write(f);
        } else {
            made = gameceiling::UnitJson(piece.kind, piece.wall);
        }
        const std::string& rel = piece.rel;
        const std::string name = "mods/D2R-VR/D2R-VR.mpq/" + rel;
        const std::wstring path = pathOf(rel);
        std::string old;
        const bool exists = ReadFile(path, &old);
        if (!arcanesky::MayReplace(exists, old, made, arcanesky::ReceiptValue(last, name))) {
            ++kept;   // the player's own: left as it is
            if (piece.kind <= -10) vaultKept[-10 - piece.kind] = true;   // (and its dome not laid: the game would draw that file)
            continue;
        }
        if (!exists || old != made) {
            if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, made.data(), made.size())) {
                r.text = lang::TX(L"ceiling.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\{file} (error {error})",
                                  {{L"file", Wide(rel.substr(rel.find_last_of('/') + 1))}, {L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        written.push_back({name, arcanesky::Hash(made.data(), made.size())});
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), written);
    if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = lang::TX(L"ceiling.no_receipt", L"could not write mods\\D2R-VR\\cave_ceiling.json");
        return r;
    }
    r.state = kMade;
    r.text = r.wrote ? lang::TX(L"ceiling.made_kinds", L"made from your game: {n} kinds of ceiling, each room's own stone or rock (mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game", {{L"n", std::to_wstring(kinds)}})
                     : lang::TX(L"ceiling.ready_kinds", L"ready: {n} kinds of ceiling (mods\\D2R-VR\\D2R-VR.mpq)", {{L"n", std::to_wstring(kinds)}});
    if (!missing.empty()) r.text += L" - " + lang::TX(L"ceiling.no_maps", L"not in this game, our vault there: {kinds}", {{L"kinds", missing}});
    if (haveFrames && beamWhy.empty())
        r.text += L" - " + lang::TX(L"ceiling.beams", L"the barracks' beams: the game's own frame, {n} whole frames a piece", {{L"n", std::to_wstring(measures.beam.cols * measures.beam.rows)}});
    else
        r.text += L" - " + lang::TX(L"ceiling.no_beams", L"no barracks beams: the game's frame is not in this game or not as known{why}",
                                    {{L"why", beamWhy.empty() ? std::wstring() : L" (" + Wide(beamWhy) + L")"}});
    r.text += L" - " + lang::TX(L"ceiling.walls", L"the walls' top measured in {n} areas", {{L"n", std::to_wstring(measures.walls.size())}});
    if (const ceilmeasure::Dome* cath = domeIn(0))
        r.text += L" - " + lang::TX(L"ceiling.dome", L"the cathedral's dome: on the chapel's four pillars, {r} units round",
                                    {{L"r", std::to_wstring((int)std::lround(cath->radius))}});
    else
        r.text += L" - " + lang::TX(L"ceiling.no_dome", L"no cathedral dome: {why}", {{L"why", Wide(domeWhy)}});
    {   // [ceiling] game_ceiling_worldstone_dome: the Worldstone's, its room and size
        std::wstring list;
        for (const ceilmeasure::Dome& d : measures.domes)
            if (d.slot > 0) list += (list.empty() ? L"" : L", ") + Wide(d.biome) + L" " + std::to_wstring((int)std::lround(d.radius));
        if (!list.empty())
            r.text += L" - " + lang::TX(L"ceiling.worldstone_dome", L"the Worldstone's dome (level 132, room and units round): {list}", {{L"list", list}});
        if (!tallWhy.empty()) r.text += L" (" + Wide(tallWhy) + L")";
    }
    if (kept) r.text += L" - " + lang::TX(L"ceiling.kept", L"{n} of its files changed by you: left as they are", {{L"n", std::to_wstring(kept)}});
    return r;
}

}  // namespace domeassets
