// holes_assets.cpp - [holes] patch_world: the world's holes patched from the player's own game (holes_assets.h; shared/world_holes_list.h makes
// each place's files, this writes them). Read only on the game's side (CascLib); one make at a time (the archives are opened once a make).
#include "holes_assets.h"

#ifndef NOMINMAX
#define NOMINMAX   // (std::min / std::max in the shared headers)
#endif
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"          // the receipt and its hash, the same as the Arcane sky's
#include "world_holes_list.h"    // the places, and what is made for each

namespace holesassets {
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

// Every file a place may be made into (five LODs and the unit) and the measures file: ours to take out again
std::vector<std::string> OurPaths() {
    std::vector<std::string> rels;
    for (const holeslist::Place& p : holeslist::kPlaces) {
        for (int lod = 0; lod < holeslist::kLods; ++lod) rels.push_back(holeslist::LodPath(p, lod));
        rels.push_back(holeslist::UnitPath(p));
    }
    rels.push_back(holeslist::kFile);
    return rels;
}

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& modRoot, bool on) {
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : modRoot + L"D2R-VR.mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = L"only in the mod's own folder (mods\\D2R-VR\\D2R-VR.mpq), which is not here: no patches over the world's holes";
        return r;
    }
    auto pathOf = [&](const std::string& rel) {
        std::wstring p = mpq + Wide(rel);
        for (wchar_t& c : p) if (c == L'/') c = L'\\';
        return p;
    };
    const std::wstring receiptPath = modRoot + L"world_holes.json";
    std::string last;
    ReadFile(receiptPath, &last);
    if (!on) {   // unticked: ours out again (by the receipt's hashes - a file the player made or changed stays), nothing made
        int out = 0;
        for (const std::string& rel : OurPaths()) {
            std::string old;
            const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
            if (ReadFile(pathOf(rel), &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash && DeleteFileW(pathOf(rel).c_str())) ++out;
        }
        if (!last.empty()) DeleteFileW(receiptPath.c_str());
        r.state = kOff;
        r.wrote = out > 0;
        r.text = L"off ([holes] patch_world): no patches made; " + std::to_wstring(out) + L" of ours taken out";
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
    const worldscene::Reader read = [storage](const std::string& name, std::string* out) { return CascRead(storage, name, out); };
    std::vector<holeslist::Made> made;
    std::wstring notMade;
    for (const holeslist::Place& p : holeslist::kPlaces) {
        holeslist::Made m;
        if (holeslist::Make(read, p, &m)) made.push_back(std::move(m));
        else notMade += (notMade.empty() ? L"" : L"; ") + Wide(p.name) + L": " + Wide(m.why);
    }
    CascCloseStorage(storage);
    // the files: each place's, and the measures of those made
    std::vector<std::pair<std::string, std::string>> files;
    std::vector<holeslist::Line> lines;
    for (const holeslist::Made& m : made) {
        files.insert(files.end(), m.files.begin(), m.files.end());
        lines.push_back(m.line);
    }
    files.push_back({holeslist::kFile, holeslist::Write(lines)});
    std::vector<arcanesky::Written> written;
    int kept = 0;
    for (const auto& [rel, bytes] : files) {
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
    // a place no longer made: its old files out (ours, unchanged)
    for (const std::string& rel : OurPaths()) {
        bool now = false;
        for (const auto& f : files) now = now || f.first == rel;
        std::string old;
        const std::string lastHash = arcanesky::ReceiptValue(last, "mods/D2R-VR/D2R-VR.mpq/" + rel);
        if (!now && ReadFile(pathOf(rel), &old) && !lastHash.empty() && arcanesky::Hash(old.data(), old.size()) == lastHash) DeleteFileW(pathOf(rel).c_str());
    }
    const std::string receipt = arcanesky::Receipt(haveBuild ? std::to_string(product.BuildNumber) : std::string(), written);
    if (receipt != last && !WriteFile(receiptPath, receipt.data(), receipt.size())) {
        r.text = L"could not write mods\\D2R-VR\\world_holes.json";
        return r;
    }
    r.state = made.empty() ? kFailed : kMade;
    std::wstring list;
    for (const holeslist::Line& l : lines) {
        wchar_t b[256];
        swprintf_s(b, L"%hs (%d triangles over %.0f of %.0f sq units, %.0f left; %d ramp strips, %d side skirts; %.1f units of edge with an opening seen)",
                   l.name.c_str(), l.triangles, l.area, l.holeArea, l.left, l.ramps, l.sides, l.seen);
        list += (list.empty() ? L"" : L", ") + std::wstring(b);
    }
    r.text = (r.wrote ? L"made from your game: " : L"ready: ") + std::to_wstring(made.size()) + L" of " + std::to_wstring(holeslist::kPlaceCount) +
             L" places patched (mods\\D2R-VR\\D2R-VR.mpq)" + (list.empty() ? L"" : L" - " + list);
    if (!notMade.empty()) r.text += L" - not made: " + notMade;
    if (kept) r.text += L" - " + std::to_wstring(kept) + L" of its files changed by you: left as they are";
    return r;
}

}  // namespace holesassets
