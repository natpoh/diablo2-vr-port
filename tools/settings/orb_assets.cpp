// orb_assets.cpp - [hud] wrist_game_models: the wrist medallions' game models made from the player's own game (orb_assets.h; shared/wrist_orb_models.h
// makes the files, this writes them). Read only on the game's side (CascLib); one make at a time.
#include "orb_assets.h"

#ifndef NOMINMAX
#define NOMINMAX   // (std::min / std::max in the shared headers)
#endif
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"          // the receipt and its hash, the same as the Arcane sky's
#include "wrist_orb_models.h"    // what is made

namespace orbassets {
namespace {

std::mutex g_casc;

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // ASCII: our own texts and the file names

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

// A file of the game's by its name ("data/hd/...")
bool CascRead(HANDLE storage, const std::string& name, std::string* out) {
    out->clear();
    HANDLE f = nullptr;
    if (!CascOpenFile(storage, ("data:" + name).c_str(), 0, CASC_OPEN_BY_NAME, &f)) return false;
    ULONGLONG size = 0;
    CascGetFileSize64(f, &size);
    bool ok = size < (256ull << 20);
    if (ok) {
        out->resize((size_t)size);
        DWORD got = 0;
        ok = size == 0 || (CascReadFile(f, out->data(), (DWORD)size, &got) && got == size);
    }
    CascCloseFile(f);
    return ok;
}

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on, float glow) {
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: the wrist medallions stay the effect's";
        return r;
    }
    auto pathOf = [&](const std::string& rel) {
        std::wstring p = mpq + Wide(rel);
        for (wchar_t& c : p) if (c == L'/') c = L'\\';
        return p;
    };
    const std::wstring receiptPath = modRoot + L"wrist_orbs.json";
    std::string last;
    ReadFile(receiptPath, &last);
    if (!on) {   // unticked: ours out again (by the receipt's hashes - a file the player made or changed stays), nothing made
        int out = 0;
        for (const std::string& rel : wristorbs::OurPaths()) {
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
            if (ReadFile(pathOf(rel), &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(pathOf(rel).c_str())) ++out;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.state = kOff;
        r.wrote = out > 0;
        r.text = L"off ([hud] wrist_game_models): nothing made; " + std::to_wstring(out) + L" of ours taken out";
        return r;
    }
    std::lock_guard<std::mutex> one(g_casc);
    HANDLE storage = nullptr;
    DWORD error = 0;
    if (!OpenCasc(game, &storage, &error)) {
        r.text = L"the game's archives could not be opened (error " + std::to_wstring(error) + L"); tried again at the next start";
        return r;
    }
    CASC_STORAGE_PRODUCT product{};
    const bool haveBuild = CascGetStorageInfo(storage, CascStorageProduct, &product, sizeof product, nullptr) && product.BuildNumber;
    const wristorbs::Reader read = [storage](const std::string& name, std::string* out) { return CascRead(storage, name, out); };
    wristorbs::Made made;
    const bool ok = wristorbs::Make(read, glow, &made);
    CascCloseStorage(storage);
    if (!ok) {
        r.text = L"could not be made from your game: " + Wide(made.why) + L" - the medallions stay the effect's";
        return r;
    }
    std::vector<arcanesky::Written> written;
    int kept = 0;
    for (const auto& [rel, bytes] : made.files) {
        const std::string name = "mods/D2R-VR/D2R-VR.mpq/" + rel;
        const std::wstring path = pathOf(rel);
        std::string old;
        const bool exists = ReadFile(path, &old);
        if (!arcanesky::MayReplace(exists, old, bytes, arcanesky::ReceiptValue(last, name))) { ++kept; continue; }   // the player's own: left as it is
        if (!exists || old != bytes) {
            if (!MakeDirs(path.substr(0, path.find_last_of(L'\\'))) || !WriteFile(path, bytes.data(), bytes.size())) {
                r.text = L"could not write mods\\D2R-VR\\D2R-VR.mpq\\...\\" + Wide(rel.substr(rel.find_last_of('/') + 1)) + L" (error " + std::to_wstring(GetLastError()) + L")";
                return r;
            }
            r.wrote = true;
        }
        written.push_back({name, arcanesky::Hash(bytes.data(), bytes.size())});
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), written);
    if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = L"could not write mods\\D2R-VR\\wrist_orbs.json";
        return r;
    }
    r.state = kMade;
    wchar_t b[900];
    swprintf_s(b, L"the life liquid in %hs's material (was %hs, glow %.1f -> %.1f; its texel at uv %.3f %.3f, %.0f%% up the glass), the mana liquid in %hs's "
                  L"(was %hs, glow %.1f -> %.1f; uv %.3f %.3f, %.0f%% up); the glass dome in the lesser mana potion's glass (texel uv %.3f %.3f, %.0f%% up: "
                  L"empty glass), the bases from the full mana potion's gold rosette, the elixir's bronze one and the antidote's pewter one (clean and aged; gold clean uv %.3f %.3f)",
               wristorbs::kSource[0], made.liquid[0].oldMaterial.c_str(), made.liquid[0].oldEmissive, wristorbs::GlowClamp(glow), made.liquid[0].u, made.liquid[0].v,
               made.liquid[0].height * 100.0f, wristorbs::kSource[1], made.liquid[1].oldMaterial.c_str(), made.liquid[1].oldEmissive, wristorbs::GlowClamp(glow),
               made.liquid[1].u, made.liquid[1].v, made.liquid[1].height * 100.0f, made.glass.u, made.glass.v, made.glass.height * 100.0f, made.plate.u,
               made.plate.v);
    r.text = std::wstring(r.wrote ? L"made from your game (takes effect at the next start of the game): " : L"ready: ") + b + L" (mods\\D2R-VR\\D2R-VR.mpq)";
    if (kept) r.text += L" - " + std::to_wstring(kept) + L" of its files changed by you: left as they are";
    return r;
}

}  // namespace orbassets
