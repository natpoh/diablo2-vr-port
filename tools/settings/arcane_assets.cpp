// arcane_assets.cpp - the Arcane Sanctuary's sky pictures out of the player's own game ([sky] arcane_sky).
//
// After bearstonem/d2r-pcvr (MIT), steamvr/prepare_arcane_sky.py of v0.3.18: Bear's installer makes them;
// ours is D2R_VR_Settings (at its start, and Setup runs it once with --prepare-assets), never the game:
// the plugin has no way to read the game's archives, and nothing here can upset a running game.
// The game's archives are read through CascLib (MIT, Ladislav Zezula), read only. What comes out is
// Blizzard's and stays on this machine: never in the repository or a build.

#include "arcane_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"

namespace arcane {
namespace {

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

// Written beside itself first, then moved over the old one: the game (looking every 2 s) never meets half a picture.
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
    bool ok = CascGetFileSize64(f, &size) && size < (16ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII: our own texts and the file names

// A folder told by where it is in the game's folder (mods\D2R-VR\reshade), else in full; no trailing backslash.
std::wstring Shown(const std::wstring& game, std::wstring p) {
    if (!game.empty() && _wcsnicmp(p.c_str(), game.c_str(), game.size()) == 0) p = p.substr(game.size());
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    return p;
}

struct Out {
    std::string name;      // relative to the game's folder, '/' between the parts (the receipt's)
    std::wstring path;
    std::string data;
};

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& reshadeFolder, const std::wstring& modRoot) {
    Result r;
    // The pictures go where ReShade looks for textures: reshade-shaders\Textures of ReShade's own folder - the mod's reshade\ in the
    // mod layout (mods\D2R-VR\reshade, ReShade's base there), the game's folder in an install that is not the mod.
    const std::wstring textures = reshadeFolder + L"reshade-shaders\\Textures\\";
    if (reshadeFolder.empty() || !IsDir(textures)) {
        r.state = kNotHere;
        r.text = lang::TX(L"arcane.no_textures", L"ReShade's reshade-shaders\\Textures is not in {folder}: install ReShade (above): the pictures are made when it is in",
                          {{L"folder", reshadeFolder.empty() ? lang::TX(L"arcane.game_folder", L"the game's folder") : Shown(game, reshadeFolder)}});
        return r;
    }
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"arcane.no_archives", L"The game's archives could not be opened (error {error}); press \"Make the Arcane Sanctuary sky now\" later",
                          {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    std::vector<Out> outs;
    std::string why;
    arcanesky::Gray layer[3], cloud;
    std::string raw;
    bool ok = true;
    for (int i = 0; i < 3 && ok; ++i)
        ok = CascRead(storage, arcanesky::kStars[i], &raw) ? arcanesky::DecodeBC4((const uint8_t*)raw.data(), raw.size(), &layer[i], &why)
                                                            : (why = std::string("not in the game: ") + arcanesky::kStars[i], false);
    if (ok)
        ok = CascRead(storage, arcanesky::kClouds, &raw) ? arcanesky::DecodeBC4((const uint8_t*)raw.data(), raw.size(), &cloud, &why)
                                                          : (why = std::string("not in the game: ") + arcanesky::kClouds, false);
    std::vector<uint8_t> rgba;
    if (ok && (ok = arcanesky::StarsRGBA(layer[0], layer[1], layer[2], &rgba, &why))) {
        const std::vector<uint8_t> png = arcanesky::Png(arcanesky::kStarsW, arcanesky::kStarsH, rgba.data());
        outs.push_back({"reshade-shaders/Textures/D2R_Arcane/stars.png", textures + L"D2R_Arcane\\stars.png", std::string(png.begin(), png.end())});
    }
    if (ok && (ok = arcanesky::CloudsRGBA(cloud, &rgba, &why))) {
        const std::vector<uint8_t> png = arcanesky::Png(arcanesky::kCloudsW, arcanesky::kCloudsH, rgba.data());
        outs.push_back({"reshade-shaders/Textures/D2R_Arcane/clouds.png", textures + L"D2R_Arcane\\clouds.png", std::string(png.begin(), png.end())});
    }
    // The level's visual files: only into the mod's own data folder (D2RLoader's mods\D2R-VR\D2R-VR.mpq, which Setup's WriteModInfo
    // makes) - an install that is not the mod (modRoot empty) has no folder whose files the game takes for its own. All seven or none.
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    const bool modFolder = !mpq.empty() && IsDir(mpq);
    std::string visWhy;
    std::vector<Out> vis;
    if (ok && modFolder) {
        for (int p = 0; p < 7; ++p) {
            const std::string name = arcanesky::VisName(p);
            std::string json, stripped;
            if (!CascRead(storage, name.c_str(), &json)) { visWhy = "not in the game: " + name; vis.clear(); break; }
            if (!arcanesky::StripWeather(json, &stripped, &visWhy)) { visWhy = name + ": " + visWhy; vis.clear(); break; }
            std::wstring path = mpq + Wide(name);
            for (wchar_t& c : path) if (c == L'/') c = L'\\';
            vis.push_back({"mods/D2R-VR/D2R-VR.mpq/" + name, path, stripped});
        }
    }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!ok) {
        r.text = lang::TX(L"arcane.not_made", L"Could not be made from the game's files ({why}); the game's own background stays", {{L"why", Wide(why)}});
        return r;
    }

    const std::wstring dir = textures + L"D2R_Arcane\\";
    if (!MakeDirs(dir)) { r.text = lang::TX(L"arcane.no_folder", L"reshade-shaders\\Textures\\D2R_Arcane could not be made"); return r; }
    std::string last;
    ReadFile(dir + L"made.json", &last);
    std::vector<arcanesky::Written> receipt;
    int kept = 0;
    auto put = [&](const Out& o, bool guarded) {
        std::string old;
        const bool exists = ReadFile(o.path, &old);
        if (guarded && !arcanesky::MayReplace(exists, old, o.data, arcanesky::ReceiptValue(last, o.name))) {
            ++kept;   // the player's own file: left as it is, and not ours from now on
            return true;
        }
        if (!exists || old != o.data) {
            if (!MakeDirs(o.path.substr(0, o.path.find_last_of(L'\\'))) || !WriteFile(o.path, o.data.data(), o.data.size())) return false;
            r.wrote = true;
        }
        receipt.push_back({o.name, arcanesky::Hash(o.data.data(), o.data.size())});
        return true;
    };
    for (const Out& o : outs)
        if (!put(o, false)) { r.text = lang::TX(L"arcane.not_written", L"Could not write {file} (error {error})", {{L"file", Wide(o.name)}, {L"error", std::to_wstring(GetLastError())}}); return r; }
    for (const Out& o : vis)
        if (!put(o, true)) { r.text = lang::TX(L"arcane.not_written", L"Could not write {file} (error {error})", {{L"file", Wide(o.name)}, {L"error", std::to_wstring(GetLastError())}}); return r; }
    const std::string made = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), receipt);
    if (made != last && !WriteFile(dir + L"made.json", made.data(), made.size())) {
        r.text = lang::TX(L"arcane.no_receipt", L"Could not write reshade-shaders\\Textures\\D2R_Arcane\\made.json");
        return r;
    }
    r.state = kMade;
    // (told by the folder it really is in: the mod's, mods\D2R-VR\reshade\reshade-shaders\Textures\D2R_Arcane, or the game's)
    const std::wstring shown = Shown(game, dir);
    r.text = r.wrote ? lang::TX(L"arcane.made", L"Made from your game: the star layers and clouds ({folder})", {{L"folder", shown}})
                     : lang::TX(L"arcane.ready", L"Ready: made from your game ({folder})", {{L"folder", shown}});
    r.text += L"; ";
    if (!modFolder) r.text += lang::TX(L"arcane.cards_stay_no_mod", L"the game's flat star cards stay (they go only in the mod's folder mods\\D2R-VR)");
    else if (vis.empty()) r.text += lang::TX(L"arcane.cards_stay", L"the game's flat star cards stay ({why})", {{L"why", Wide(visWhy)}});
    else if (kept) r.text += lang::TX(L"arcane.cards_kept", L"{n} visual file(s) in mods\\D2R-VR changed by you were left as they are", {{L"n", std::to_wstring(kept)}});
    else r.text += lang::TX(L"arcane.cards_out", L"the flat star cards taken out in mods\\D2R-VR");
    return r;
}

}  // namespace arcane
