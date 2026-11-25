// sewer_assets.cpp - [holes] sewer_backdrop: the Kurast sewer stairs' open end closed, from the player's own game (sewer_assets.h;
// shared/unleashed_sewer_backdrop.h measures and edits, this reads and writes). The game's archives are read through CascLib (MIT, Ladislav
// Zezula), read only; the receipt is the Arcane sky's (arcane_sky.h). Same shape as boat_assets.cpp.
#include "sewer_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#ifndef NOMINMAX
#define NOMINMAX   // (std::min / std::max in the shared headers)
#endif
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"                 // the receipt and its hash
#include "unleashed_sewer_backdrop.h"   // what is measured and changed

namespace sewerassets {
namespace {

std::mutex g_once;   // one make at a time (Settings' start and a tick change may overlap)

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

// Written beside itself first, then moved over the old one: a game loading the level meanwhile never meets half a file.
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

bool CascRead(HANDLE storage, const std::string& name, std::string* out) {
    out->clear();
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, ("data:" + name).c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
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

std::wstring PathOf(const std::wstring& mpq, const char* rel) {
    std::wstring p = mpq + Wide(rel);
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    return p;
}

std::string ReceiptName(const char* rel) { return std::string("mods/D2R-VR/D2R-VR.mpq/") + rel; }

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on) {
    std::lock_guard<std::mutex> lock(g_once);
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"sewer.no_mod", L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: the game's own stairs");
        return r;
    }
    const std::wstring receiptPath = modRoot + L"sewer_backdrop.json";
    std::string last;
    ReadFile(receiptPath, &last);

    // Ours out again (unticked, or the game's own presets no longer as known): only a file whose hash is the receipt's.
    auto takeOut = [&](const std::wstring& because) {
        int out = 0, kept = 0;
        for (const char* rel : sewerbackdrop::kPresets) {
            const std::wstring path = PathOf(mpq, rel);
            std::string old;
            if (!ReadFile(path, &old)) continue;
            const std::string lastHash = arcanesky::ReceiptValue(last, ReceiptName(rel));
            if (!lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(path.c_str())) ++out;
            else ++kept;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.wrote = out > 0;
        r.text = because;
        if (out) r.text += L"; " + lang::TX(L"sewer.ours_out", L"our {n} Kurast presets taken out of mods\\D2R-VR", {{L"n", std::to_wstring(out)}});
        if (kept) r.text += L"; " + lang::TX(L"sewer.not_ours", L"{n} Kurast sewer presets in mods\\D2R-VR that are not ours stay as they are", {{L"n", std::to_wstring(kept)}});
        return r;
    };
    if (!on) {
        r.state = kOff;
        return takeOut(lang::TX(L"sewer.off", L"off ([holes] sewer_backdrop, the World tab): the stairs as the game has them"));
    }

    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"sewer.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start",
                          {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    std::string stairsBytes, blackBytes;
    std::vector<std::string> presets(sewerbackdrop::kPresetCount);
    bool read = CascRead(storage, std::string(sewerbackdrop::kStairs) + "_lod" + std::to_string(sewerbackdrop::kMeasureLod) + ".model", &stairsBytes) &&
                CascRead(storage, std::string(sewerbackdrop::kBlack) + "_lod" + std::to_string(sewerbackdrop::kMeasureLod) + ".model", &blackBytes);
    for (int i = 0; i < sewerbackdrop::kPresetCount && read; ++i) read = CascRead(storage, sewerbackdrop::kPresets[i], &presets[(size_t)i]);
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    CascCloseStorage(storage);
    if (!read) {
        r.state = kMade;
        return takeOut(lang::TX(L"sewer.no_files", L"the Kurast sewer stairs are not in this game as known: nothing to do"));
    }
    std::string why;
    sewerbackdrop::Model stairs, black;
    sewerbackdrop::Measured m;
    if (!sewerbackdrop::ReadModel(stairsBytes, &stairs, &why) || !sewerbackdrop::ReadModel(blackBytes, &black, &why) ||
        !sewerbackdrop::Measure(stairs, black, &m, &why)) {
        takeOut(L"");
        r.state = kFailed;
        r.text = lang::TX(L"sewer.refused", L"the game's sewer stairs are not as known ({why}); the game's own stay", {{L"why", Wide(why)}});
        return r;
    }
    std::vector<std::string> made((size_t)sewerbackdrop::kPresetCount);
    for (int i = 0; i < sewerbackdrop::kPresetCount; ++i) {
        const sewerbackdrop::State s = sewerbackdrop::AddBackdrop(presets[(size_t)i], m, &made[(size_t)i], &why);
        if (s != sewerbackdrop::kAdded) {
            takeOut(L"");
            r.state = kFailed;
            r.text = lang::TX(L"sewer.preset_refused", L"{preset} is not as known ({why}); the game's own stairs stay",
                              {{L"preset", Wide(sewerbackdrop::kPresets[i])}, {L"why", Wide(s == sewerbackdrop::kOurs ? "it has ours already" : why)}});
            return r;
        }
    }
    std::vector<arcanesky::Written> written;
    int changedByYou = 0;
    for (int i = 0; i < sewerbackdrop::kPresetCount; ++i) {
        const char* rel = sewerbackdrop::kPresets[i];
        const std::wstring path = PathOf(mpq, rel);
        const std::string& data = made[(size_t)i];
        std::string old;
        const bool exists = ReadFile(path, &old);
        if (!arcanesky::MayReplace(exists, old, data, arcanesky::ReceiptValue(last, ReceiptName(rel)))) { ++changedByYou; continue; }
        if (!exists || old != data) {
            if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, data.data(), data.size())) {
                r.text = lang::TX(L"sewer.not_written", L"could not write mods\\D2R-VR\\D2R-VR.mpq\\{preset} (error {error})",
                                  {{L"preset", Wide(rel)}, {L"error", std::to_wstring(GetLastError())}});
                return r;
            }
            r.wrote = true;
        }
        written.push_back({ReceiptName(rel), arcanesky::Hash(data.data(), data.size())});
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), written);
    if (written.empty()) DeleteFileW(receiptPath.c_str());
    else if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = lang::TX(L"sewer.no_receipt", L"could not write mods\\D2R-VR\\sewer_backdrop.json");
        return r;
    }
    r.state = kMade;
    wchar_t size[96];
    swprintf(size, 96, L"%.1f x %.1f", m.width + 4.0, m.height);
    r.text = r.wrote ? lang::TX(L"sewer.made", L"made from your game: a dark wall at the end of the Kurast sewer stairs ({size} units, {n} presets in mods\\D2R-VR\\D2R-VR.mpq), from the next start of the game",
                                {{L"size", size}, {L"n", std::to_wstring(written.size())}})
                     : lang::TX(L"sewer.ready", L"ready: a dark wall at the end of the Kurast sewer stairs ({n} presets in mods\\D2R-VR\\D2R-VR.mpq)",
                                {{L"n", std::to_wstring(written.size())}});
    if (changedByYou) r.text += L"; " + lang::TX(L"sewer.changed_by_you", L"{n} changed by you: left as they are", {{L"n", std::to_wstring(changedByYou)}});
    return r;
}

}  // namespace sewerassets
