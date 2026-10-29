// dome_assets.cpp - [sky] game_sky's dome models made from the player's own game (dome_assets.h). The game took its own Summit sky card
// through our unit and never took our own .model of the same type tree (PC1 2026-10-27): so the card itself, our mesh in it.

#include "dome_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"      // the receipt and its hash, the same as the Arcane sky's
#include "game_ceiling.h"    // [ceiling] game_ceiling: the slab's paths, its source model, its maps and its unit
#include "granny_remesh.h"

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

bool CascRead(HANDLE storage, const char* name, std::string* out) {
    const std::string full = std::string("data:") + name;
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

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, bool summitPath) {
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
        for (const Data& x : kData) {
            std::wstring path = mpq + Wide(x.rel);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, std::string("mods/D2R-VR/D2R-VR.mpq/") + x.rel);
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
    const grannyremesh::Mesh mesh = grannyremesh::DomeMesh(kRadius);
    std::vector<arcanesky::Written> written;
    int kept = 0;
    int out = 0;
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
            const grannyremesh::Renames names{
                {kCardTexture, d.texture},
                {"GLSLShader1", own + "_material"},
                {"file2", own + "_map"},
                {"skybox01", own},
                {"skybox0Shape1", own + "_shape"},
                {"X:/assets/env/model/expansion/mountaintop/expansion_mountaintop_skybox/expansion_mountaintop_skybox.ma", "d2r_vr/" + own + ".ma"},
                {"X:/branches/master/hd/data/env/texture/expansion/mountaintop/mountaintop_skybox/expansion_mountaintop_skybox01_HDR.hdr",
                 "d2r_vr/" + own + "_HDR.hdr"}};
            if (!grannyremesh::Read(card, &f, &why) || !grannyremesh::Remesh(f, mesh, names, lod, nullptr, &why)) {
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
        const std::string made = x.make();
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
    struct Piece { std::string rel; int kind; bool wall; int lod; };
    std::vector<Piece> pieces;
    for (int k = 0; k < gameceiling::kKindCount; ++k)
        for (const bool wall : {false, true}) {
            const char* file = wall ? gameceiling::kKinds[k].wall : gameceiling::kKinds[k].slab;
            if (!file) continue;
            for (int lod = 0; lod < 5; ++lod) pieces.push_back({gameceiling::LodPath(file, lod), k, wall, lod});
            pieces.push_back({gameceiling::UnitPath(k, wall), k, wall, -1});
        }
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
    CascCloseStorage(storage);
    if (!read) {
        r.text = lang::TX(L"ceiling.no_rock", L"act 1's cave rock model is not in this game: no game cave ceiling");
        return r;
    }
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
    for (const Piece& piece : pieces) {
        if (!hasMaps[piece.kind]) continue;
        const gameceiling::Kind& kind = gameceiling::kKinds[piece.kind];
        std::string made;
        if (piece.lod >= 0) {
            if (!piece.wall && meshKind != piece.kind) {   // (each kind its own relief)
                mesh = grannyremesh::CeilingMesh(gameceiling::kTile, gameceiling::kTilesHalf, gameceiling::kCells, kind.relief);
                meshKind = piece.kind;
            }
            const grannyremesh::Renames maps = {{gameceiling::kMaps[0].from, gameceiling::MapPath(kind, "ALB")},   // the rock's maps: the kind's
                                                {gameceiling::kMaps[1].from, gameceiling::MapPath(kind, "NRM")},
                                                {gameceiling::kMaps[2].from, gameceiling::MapPath(kind, "ORM")}};
            grannyremesh::File f;
            std::string why;
            if (!grannyremesh::Read(rock, &f, &why) || !grannyremesh::Remesh(f, piece.wall ? wallMesh : mesh, maps, piece.lod, nullptr, &why)) {
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
    if (kept) r.text += L" - " + lang::TX(L"ceiling.kept", L"{n} of its files changed by you: left as they are", {{L"n", std::to_wstring(kept)}});
    return r;
}

}  // namespace domeassets
