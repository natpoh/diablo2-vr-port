// props_assets.cpp - see props_assets.h.
//
// After bearstonem/d2r-pcvr (MIT), steamvr/prepare_props.py of feat/knockable-props (2026-10-22): Bear's installer makes it in Python; ours is D2R_VR_Settings,
// never the game (the plugin has no way to read the game's archives). The rows are the presets' own entity ids, positions and kinds (shared/props_presets.h); what comes
// out is Blizzard's data and stays on this machine.

#include "props_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CascLib.h"
#include "props_presets.h"
#include "props_scene.h"

namespace propsassets {
namespace {

bool ReadAll(const std::wstring& path, std::string* out) {
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

bool CascRead(HANDLE storage, const char* name, std::string* out) {
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, name, 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    bool ok = CascGetFileSize64(f, &size) && size < (16ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& pluginDirIn, bool force) {
    Result r;
    std::wstring pluginDir = pluginDirIn;
    if (!pluginDir.empty() && pluginDir.back() != L'\\') pluginDir += L'\\';
    const DWORD attr = GetFileAttributesW(pluginDir.c_str());
    if (pluginDir.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        r.state = kNotHere;
        r.text = lang::TX(L"props.no_plugin_folder", L"the plugin's folder is not known");
        return r;
    }
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"props.no_archives", L"the game's archives could not be opened (error {error})", {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    const std::string build = haveBuild ? std::to_string(product.BuildNumber) : std::string("unknown");
    const std::wstring dir = pluginDir + L"d2r_vr_props\\";
    const std::wstring bin = dir + L"props.bin", made = dir + L"made.txt";
    // the stamp names the build and what was read: a catalogue made before the prefabs were read, or before the kinds of bearstonem's catalogue
    // version 4 (508c5d0), is made again on the same build
    const std::string stamp = "build " + build + "\ncatalogue 3 (presets and prefabs, 197 kinds)\n";
    std::string last, have;
    if (!force && ReadAll(made, &last) && last == stamp && ReadAll(bin, &have)) {
        propscene::Catalog c;
        if (c.Parse((const uint8_t*)have.data(), have.size())) {
            CascCloseStorage(storage);
            r.state = kMade;
            r.props = (int)c.Size();
            r.kinds = (int)c.KindCount();
            r.text = lang::TX(L"props.ready", L"ready: {props} knockable props of {kinds} kinds, made from this game build",
                              {{L"props", std::to_wstring(r.props)}, {L"kinds", std::to_wstring(r.kinds)}});
            return r;
        }
    }
    // Every HD level preset, and every prefab a preset places (data/hd/env/model/**.json: the Rogue Encampment's tents, wagons, campfire and smithy are prefabs, and the
    // props in them are the prefab's own entities, at their places in it): their names from the archive's own list
    std::vector<std::string> names;
    std::vector<bool> prefab;
    CASC_FIND_DATA fd{};
    HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr);
    if (find) {
        do {
            std::string n = Lower(fd.szFileName);   // ("data:data\hd\env\preset\act1\barracks\barE.json": the archive's names have backslashes)
            for (char& c : n) if (c == '\\') c = '/';
            if (n.size() <= 5 || n.compare(n.size() - 5, 5, ".json") != 0) continue;
            const bool preset = n.find("data/hd/env/preset/") != std::string::npos, model = n.find("data/hd/env/model/") != std::string::npos;
            if (preset || model) { names.push_back(fd.szFileName); prefab.push_back(model); }
        } while (CascFindNextFile(find, &fd));
        CascFindClose(find);
    }
    std::vector<propscene::Row> rows;
    std::string text;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!CascRead(storage, names[i].c_str(), &text)) continue;
        const size_t before = rows.size();
        if (!propspresets::PropsOf(text, rows)) continue;
        if (!prefab[i]) { ++r.presets; continue; }
        for (size_t j = before; j < rows.size(); ++j) rows[j].prefab = true;
        if (rows.size() > before) { ++r.prefabs; r.prefabProps += int(rows.size() - before); }
    }
    CascCloseStorage(storage);
    if (!r.presets || rows.empty()) {
        r.text = names.empty() ? lang::TX(L"props.no_presets", L"no level presets found in the game's archives")
                               : lang::TX(L"props.none", L"no knockable props in the game's level presets");
        return r;
    }
    const std::vector<uint8_t> data = propscene::Encode(rows);
    propscene::Catalog check;
    if (data.empty() || !check.Parse(data.data(), data.size())) {
        r.text = lang::TX(L"props.bad_catalogue", L"the catalogue did not come out right ({rows} rows)", {{L"rows", std::to_wstring(rows.size())}});
        return r;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!WriteAll(bin, data.data(), data.size()) || !WriteAll(made, stamp.data(), stamp.size())) {
        r.text = lang::TX(L"props.not_written", L"could not write d2r_vr_props\\props.bin (error {error})", {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    r.state = kMade;
    r.wrote = true;
    r.props = (int)check.Size();
    r.kinds = (int)check.KindCount();
    r.text = lang::TX(L"props.made", L"made from your game: {props} knockable props of {kinds} kinds from {presets} level presets and {prefabs} prefabs ({prefab_props} of the props in them)",
                      {{L"props", std::to_wstring(r.props)}, {L"kinds", std::to_wstring(r.kinds)}, {L"presets", std::to_wstring(r.presets)},
                       {L"prefabs", std::to_wstring(r.prefabs)}, {L"prefab_props", std::to_wstring(r.prefabProps)}});
    return r;
}

Result PrepareItemModels(const std::wstring& game, const std::wstring& pluginDirIn, bool force) {
    Result r;
    std::wstring pluginDir = pluginDirIn;
    if (!pluginDir.empty() && pluginDir.back() != L'\\') pluginDir += L'\\';
    const DWORD attr = GetFileAttributesW(pluginDir.c_str());
    if (pluginDir.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        r.state = kNotHere;
        r.text = lang::TX(L"props.no_plugin_folder", L"the plugin's folder is not known");
        return r;
    }
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"props.no_archives", L"the game's archives could not be opened (error {error})", {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    const std::string stamp = "# build " + (haveBuild ? std::to_string(product.BuildNumber) : std::string("unknown")) + "\n";
    const std::wstring dir = pluginDir + L"d2r_vr_props\\", file = dir + L"item_models.txt";
    std::string have;
    if (!force && ReadAll(file, &have) && have.compare(0, stamp.size(), stamp) == 0) {
        CascCloseStorage(storage);
        r.state = kMade;
        for (char c : have) r.props += c == '\n';
        --r.props;
        r.text = L"ready: " + std::to_wstring(r.props) + L" item models, made from this game build";
        return r;
    }
    // the item definitions there are (lower case, '/')
    std::vector<std::string> defs;
    CASC_FIND_DATA fd{};
    HANDLE find = CascFindFirstFile(storage, "*", &fd, nullptr);
    if (find) {
        do {
            std::string n = Lower(fd.szFileName);
            for (char& c : n) if (c == '\\') c = '/';
            const size_t at = n.find("data/hd/items/");
            if (at != std::string::npos && n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) defs.push_back(n.substr(at));
        } while (CascFindNextFile(find, &fd));
        CascFindClose(find);
    }
    std::sort(defs.begin(), defs.end());
    std::string text;
    const bool read = CascRead(storage, "data:data\\hd\\items\\items.json", &text);
    CascCloseStorage(storage);
    propspresets::Value list;
    if (!read || !propspresets::Parser(text.data(), text.size()).Parse(list) || list.type != propspresets::Value::Array) {
        r.text = L"data/hd/items/items.json did not read";
        return r;
    }
    std::string out = stamp;
    for (const propspresets::Value& e : list.items) {
        if (e.type != propspresets::Value::Object) continue;
        for (const auto& kv : e.keys) {
            const propspresets::Value* asset = kv.second.Get("asset");
            if (kv.first.empty() || kv.first.size() > 4 || !asset || asset->type != propspresets::Value::String || asset->s.empty()) continue;
            for (const char* cat : {"weapon/", "armor/", "misc/"}) {
                const std::string path = "data/hd/items/" + std::string(cat) + Lower(asset->s) + ".json";
                if (!std::binary_search(defs.begin(), defs.end(), path)) continue;
                out += kv.first + "\t" + path + "\n";
                ++r.props;
                break;
            }
        }
    }
    if (!r.props) { r.text = L"no item's model found"; return r; }
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!WriteAll(file, out.data(), out.size())) {
        r.text = L"could not write d2r_vr_props\\item_models.txt (error " + std::to_wstring(GetLastError()) + L")";
        return r;
    }
    r.state = kMade;
    r.wrote = true;
    r.text = L"made from your game: " + std::to_wstring(r.props) + L" item models";   // (the item models' lines go to the log only: English)
    return r;
}

}  // namespace propsassets
