// solids_assets.cpp - see solids_assets.h. What comes out is made from Blizzard's data and stays on this machine.

#include "solids_assets.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "CascLib.h"
#include "props_presets.h"
#include "world_solids_make.h"

namespace solidsassets {
namespace {

bool ReadAll(const std::wstring& path, std::string* out) {
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (256ll << 20);
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = out->empty() || (::ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size());
    }
    CloseHandle(f);
    return ok;
}

// Written beside itself first, then moved over the old one: the plugin (reading it at its start) never meets half a file.
bool WriteAll(const std::wstring& path, const void* data, size_t n) {
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = ::WriteFile(f, data, (DWORD)n, &put, nullptr) && put == n;
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}

bool CascRead(HANDLE storage, const std::string& name, std::string* out) {
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, name.c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    bool ok = CascGetFileSize64(f, &size) && size < (64ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

// A placed piece of a preset: its entity id, its place in the preset, the model(s) it is drawn with.
struct Piece {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    std::vector<std::string> models;
    bool prefab = false;
};

// The pieces of one preset's text: each entity with a transform and a model (or a model's variations) that is not the terrain. False when it is not a preset.
bool PiecesOf(const std::string& text, std::vector<Piece>& out) {
    using propspresets::Value;
    Value root;
    if (!propspresets::Parser(text.data(), text.size()).Parse(root) || root.type != Value::Object) return false;
    const Value* ents = root.Get("entities");
    if (!ents || ents->type != Value::Array) return true;
    for (const Value& e : ents->items) {
        if (e.type != Value::Object) continue;
        const Value* comps = e.Get("components");
        const Value* id = e.Get("id");
        if (!comps || comps->type != Value::Array || !id || id->type != Value::Number || !(id->n > 0 && id->n < 4294967296.0)) continue;
        Piece p;
        p.id = (uint32_t)id->n;
        const Value* transform = nullptr;
        bool terrain = false;
        for (const Value& c : comps->items) {
            if (c.type != Value::Object) continue;
            const Value* t = c.Get("type");
            const std::string type = t && t->type == Value::String ? t->s : std::string();
            if (type == "TransformDefinitionComponent") {
                transform = &c;
            } else if (type == "TerrainDefinitionComponent") {
                terrain = true;   // (the ground is not a wall)
            } else if (type == "ModelDefinitionComponent") {
                const Value* f = c.Get("filename");
                if (f && f->type == Value::String && !f->s.empty() && p.models.size() < (size_t)worldsolids::kMaxVariants) p.models.push_back(f->s);
            } else if (type == "ModelVariationDefinitionComponent") {
                const Value* vs = c.Get("variations");
                if (vs && vs->type == Value::Array)
                    for (const Value& v : vs->items) {
                        const Value* f = v.type == Value::Object ? v.Get("filename") : nullptr;
                        if (f && f->type == Value::String && !f->s.empty() && p.models.size() < (size_t)worldsolids::kMaxVariants) p.models.push_back(f->s);
                    }
            }
        }
        if (terrain || p.models.empty() || !transform) continue;
        const Value* pos = transform->Get("position");
        const Value* x = pos && pos->type == Value::Object ? pos->Get("x") : nullptr;
        const Value* y = pos && pos->type == Value::Object ? pos->Get("y") : nullptr;
        const Value* z = pos && pos->type == Value::Object ? pos->Get("z") : nullptr;
        if (!x || !y || !z || x->type != Value::Number || y->type != Value::Number || z->type != Value::Number) continue;
        p.x = (float)x->n;
        p.y = (float)y->n;
        p.z = (float)z->n;
        out.push_back(std::move(p));
    }
    return true;
}

// A model's meshes out of the archives: its _lod0 file (the presets name a model without its level of detail), else the name itself.
bool ModelMeshes(HANDLE storage, const std::string& path, std::vector<grannygeom::Mesh>* out) {
    std::string name = path;
    for (char& c : name) if (c == '/') c = '\\';
    std::string lod = name;
    if (lod.size() > 6 && worldsolids::Lower(lod.substr(lod.size() - 6)) == ".model") lod = lod.substr(0, lod.size() - 6) + "_lod0.model";
    std::string bytes;
    if (!CascRead(storage, "data:" + lod, &bytes) && !CascRead(storage, "data:" + name, &bytes)) return false;
    grannyremesh::File f;
    std::string why;
    return grannyremesh::Read(bytes, &f, &why) && grannygeom::MeshesOf(f, out, &why);
}

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& pluginDirIn, bool force) {
    Result r;
    const auto t0 = std::chrono::steady_clock::now();
    std::wstring pluginDir = pluginDirIn;
    if (!pluginDir.empty() && pluginDir.back() != L'\\') pluginDir += L'\\';
    const DWORD attr = GetFileAttributesW(pluginDir.c_str());
    if (pluginDir.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        r.state = kNotHere;
        r.text = L"the plugin's folder is not known";
        return r;
    }
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    const std::wstring dir = pluginDir + L"d2r_vr_props\\";
    const std::wstring bin = dir + L"solids.bin", made = dir + L"solids.txt";
    // the stamp names the build and the make: a catalogue of another make is made again on the same build
    const std::string make = "solids 4 (meshes in cells of 0.7 units, foliage but trees left out, tree crowns left out, movable props left out, solid pieces only, prefabs by id)\n";
    // The archives are opened only to read the build's number (made.txt names it) - every start. They can be busy for a moment while the game itself starts (Serg,
    // 0.192.19: Settings and the game started together, "could not be opened (error 2)" while the other makers opened them 0.3 s later): a few tries; and when they
    // still do not open, a catalogue of this make that is there and reads is kept (the game reads it) - a status, not a red line.
    HANDLE storage = nullptr;
    DWORD openError = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt) Sleep(750);
        if (CascOpenStorage(root.c_str(), 0, &storage)) break;
        storage = nullptr;
        openError = GetLastError();
    }
    if (!storage) {
        std::string lastStamp, kept;
        worldsolids::Catalog c;
        if (!force && ReadAll(made, &lastStamp) && lastStamp.size() >= make.size() && lastStamp.compare(lastStamp.size() - make.size(), make.size(), make) == 0 &&
            ReadAll(bin, &kept) && c.Parse((const uint8_t*)kept.data(), kept.size())) {
            r.state = kMade;
            r.pieces = (int)c.Rows();
            r.models = (int)c.Models();
            r.text = L"ready: " + std::to_wstring(r.pieces) + L" level pieces of " + std::to_wstring(r.models) + L" models, the catalogue made before kept (the game's archives did not open: "
                     L"error " + std::to_wstring(openError) + L" at " + root + L"; looked at again at the next start)";
            return r;
        }
        r.text = L"the game's archives could not be opened (error " + std::to_wstring(openError) + L", 4 tries, at " + root + L")";
        return r;
    }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    const std::string build = haveBuild ? std::to_string(product.BuildNumber) : std::string("unknown");
    const std::string stamp = "build " + build + "\n" + make;
    std::string last, have;
    if (!force && ReadAll(made, &last) && last == stamp && ReadAll(bin, &have)) {
        worldsolids::Catalog c;
        if (c.Parse((const uint8_t*)have.data(), have.size())) {
            CascCloseStorage(storage);
            r.state = kMade;
            r.pieces = (int)c.Rows();
            r.models = (int)c.Models();
            r.text = L"ready: " + std::to_wstring(r.pieces) + L" level pieces of " + std::to_wstring(r.models) + L" models, made from this game build";
            return r;
        }
    }
    // every HD level preset and every prefab one places, by the archive's own list
    std::vector<std::string> names;
    std::vector<bool> prefab;
    CASC_FIND_DATA fd{};
    HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr);
    if (find) {
        do {
            std::string n = worldsolids::Lower(fd.szFileName);
            if (n.size() <= 5 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
            const bool preset = n.find("data/hd/env/preset/") != std::string::npos, model = n.find("data/hd/env/model/") != std::string::npos;
            if (preset || model) { names.push_back(fd.szFileName); prefab.push_back(model); }
        } while (CascFindNextFile(find, &fd));
        CascFindClose(find);
    }
    std::vector<Piece> pieces;
    std::string text;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!CascRead(storage, names[i], &text)) continue;
        const size_t before = pieces.size();
        if (!PiecesOf(text, pieces)) continue;
        if (!prefab[i]) ++r.presets;
        else if (pieces.size() > before) ++r.prefabs;
        for (size_t j = before; j < pieces.size(); ++j) pieces[j].prefab = prefab[i];
    }
    // each model once
    std::vector<std::string> paths;
    for (const Piece& p : pieces)
        for (const std::string& m : p.models) paths.push_back(worldsolids::Lower(m));
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::vector<worldsolids::Model> models(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        worldsolids::Model& m = models[i];
        m.flags = worldsolids::Classify(paths[i]);
        if (m.flags & worldsolids::kIgnore) { ++r.foliage; continue; }
        if (propscene::KindOfModel(paths[i]) >= 0) { m.flags |= worldsolids::kMovable; ++r.movable; continue; }   // (knock props moves it: no wall)
        std::vector<grannygeom::Mesh> meshes;
        if (!ModelMeshes(storage, paths[i], &meshes) || !worldsolids::Make(meshes, &m)) {
            m.flags |= worldsolids::kNoMesh;
            ++r.unread;
            continue;
        }
        ++r.withParts;
    }
    CascCloseStorage(storage);
    std::vector<worldsolids::Row> rows;
    for (const Piece& p : pieces) {
        worldsolids::Row row;
        row.id = p.id;
        row.x = p.x;
        row.y = p.y;
        row.z = p.z;
        row.flags = p.prefab ? worldsolids::kRowPrefab : 0u;
        int k = 0;
        for (const std::string& m : p.models) {
            const std::string low = worldsolids::Lower(m);
            const auto it = std::lower_bound(paths.begin(), paths.end(), low);
            if (it != paths.end() && *it == low && k < worldsolids::kMaxVariants) row.models[k++] = (uint32_t)(it - paths.begin());
        }
        bool solid = false;   // (a piece drawn only with foliage or with models that did not read is no wall: not kept; a movable prop is kept, marked, so the
                              // plugin knows it is no wall even without the props catalogue - not laid by its box as an unknown piece)
        for (int v = 0; v < k; ++v) solid = solid || !models[row.models[v]].parts.empty() || (models[row.models[v]].flags & worldsolids::kMovable);
        if (solid) rows.push_back(row);
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rows.empty() || !r.withParts) {
        r.text = names.empty() ? L"no level presets found in the game's archives" : L"no level piece's model read";
        return r;
    }
    const std::vector<uint8_t> data = worldsolids::Encode(models, rows);
    worldsolids::Catalog check;
    if (!check.Parse(data.data(), data.size())) {
        r.text = L"the solids did not come out right";
        return r;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!WriteAll(bin, data.data(), data.size()) || !WriteAll(made, stamp.data(), stamp.size())) {
        r.text = L"could not write d2r_vr_props\\solids.bin (error " + std::to_wstring(GetLastError()) + L")";
        return r;
    }
    r.state = kMade;
    r.wrote = true;
    r.pieces = (int)rows.size();
    r.models = (int)models.size();
    wchar_t b[400];
    swprintf(b, 400, L"made from your game in %.0f s: %d level pieces from %d presets and %d prefabs; %d models with solid parts, %d foliage and %d movable props left out, "
             L"%d not read (%zu KB)", r.seconds, r.pieces, r.presets, r.prefabs, r.withParts, r.foliage, r.movable, r.unread, data.size() / 1024);
    r.text = b;
    return r;
}

}  // namespace solidsassets
