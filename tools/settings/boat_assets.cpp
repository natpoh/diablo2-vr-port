// boat_assets.cpp - the Kurast Docks' flying canoe put down on the water ([world] kurast_boat), from the player's own game.
//
// After bearstonem/d2r-pcvr (MIT), bed674b: its installer takes the canoe out of the town's HD preset; ours keeps it and lowers it
// (shared/kurast_boat.h says where it belongs and why). D2R_VR_Settings does it (at its start, and Setup runs it once with
// --prepare-assets), never the game. The game's archives are read through CascLib (MIT, Ladislav Zezula), read only. What comes out
// is Blizzard's file with one number changed and stays on this machine, inside the mod: never in the repository or a build.

#include "boat_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"    // the receipt and its hash, the same as the Arcane sky's
#include "kurast_boat.h"

namespace boatassets {
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

// Written beside itself first, then moved over the old one: a game loading the town meanwhile never meets half a file.
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

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on) {
    Result r;
    // Only into the mod's own data folder (D2RLoader's mods\D2R-VR\D2R-VR.mpq, which Setup's WriteModInfo makes): an install that is not
    // the mod has no folder whose files the game takes for its own.
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"boat.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: the game's own town");
        return r;
    }
    const std::string name = std::string("mods/D2R-VR/D2R-VR.mpq/") + kurastboat::kPreset;   // (the receipt's, '/' between the parts)
    std::wstring path = mpq + Wide(kurastboat::kPreset);
    for (wchar_t& c : path) if (c == L'/') c = L'\\';
    const std::wstring receiptPath = modRoot + L"kurast_boat.json";
    std::string last, old;
    ReadFile(receiptPath, &last);
    const bool exists = ReadFile(path, &old);
    const std::string lastHash = arcanesky::ReceiptValue(last, name);
    const bool ours = exists && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash;

    // Our copy out again: unticked, or the game's own preset no longer wants it (an old town over a newer game would be worse than the
    // boat in the sky). A file of that name the player made or changed stays.
    auto takeOut = [&](const std::wstring& because) {
        if (ours) {
            if (!DeleteFileW(path.c_str())) {
                r.state = kFailed;
                r.text = lang::TX(L"boat.not_taken_out", L"could not take out mods\\D2R-VR\\D2R-VR.mpq\\...\\docktown3.json (error {error})", {{L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.text = because;
        if (ours) r.text += L"; " + lang::TX(L"boat.ours_out", L"our town preset taken out of mods\\D2R-VR");
        else if (exists) r.text += L"; " + lang::TX(L"boat.not_ours", L"a docktown3.json in mods\\D2R-VR that is not ours stays as it is");
        return r;
    };
    if (!on) {
        r.state = kOff;
        return takeOut(lang::TX(L"boat.off", L"off ([world] kurast_boat, the World tab): the town as the game has it"));
    }

    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"boat.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start", {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    std::string json, lowered, why;
    const bool read = CascRead(storage, kurastboat::kPreset, &json);
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!read) {
        r.state = kMade;
        return takeOut(lang::TX(L"boat.no_preset", L"the town's preset is not in this game: nothing to do"));
    }
    const kurastboat::State s = kurastboat::Lower(json, &lowered, &why);
    if (s == kurastboat::kNoNeed) {
        r.state = kMade;
        return takeOut(lang::TX(L"boat.no_need", L"the game's own town has no boat in the sky: nothing to do"));
    }
    if (s == kurastboat::kRefused) {
        takeOut(L"");
        r.state = kFailed;
        r.text = lang::TX(L"boat.refused", L"the game's town preset is not as known ({why}); the game's own stays, boat and all", {{L"why", Wide(why)}});
        return r;
    }
    if (!arcanesky::MayReplace(exists, old, lowered, lastHash)) {
        r.state = kMade;
        r.text = lang::TX(L"boat.changed_by_you", L"mods\\D2R-VR\\D2R-VR.mpq\\...\\docktown3.json was changed by you: left as it is");
        return r;
    }
    if (!exists || old != lowered) {
        if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, lowered.data(), lowered.size())) {
            r.text = lang::TX(L"boat.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\docktown3.json (error {error})", {{L"error", std::to_wstring(GetLastError())}});
            return r;
        }
        r.wrote = true;
    }
    const std::string made = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(),
                                                {{name, arcanesky::Hash(lowered.data(), lowered.size())}});
    if (made != last && !WriteFile(receiptPath, made.data(), made.size())) {
        r.text = lang::TX(L"boat.no_receipt", L"could not write mods\\D2R-VR\\kurast_boat.json");
        return r;
    }
    r.state = kMade;
    r.text = r.wrote ? lang::TX(L"boat.made", L"made from your game: the canoe on the water (mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game")
                     : lang::TX(L"boat.ready", L"ready: the canoe on the water (mods\\D2R-VR\\D2R-VR.mpq)");
    return r;
}

}  // namespace boatassets
