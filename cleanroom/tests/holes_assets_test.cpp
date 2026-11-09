// holes_assets_test - [holes] patch_world: D2R_VR_Settings' maker (tools/settings/holes_assets.cpp) on the player's own game, read through CascLib,
// written into a scratch mod folder (never the game's): every place made (its five models, its unit), the measures file, the receipt; made again:
// nothing written; a file of ours changed by the player: left as it is; off: ours taken out, the player's stays.
//   holes_assets_test <game folder> <empty scratch folder>
#include <windows.h>

#include <cstdio>
#include <string>

#include "holes_assets.h"
#include "world_holes_file.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::wstring In(const std::wstring& mpq, const std::string& rel) {
    std::wstring p = mpq + Wide(rel);
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    return p;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("holes_assets_test <game folder> <empty scratch folder>\n"); return 2; }
    std::wstring game = argv[1], scratch = argv[2];
    if (game.back() != L'\\') game += L'\\';
    if (scratch.back() != L'\\') scratch += L'\\';
    const std::wstring modRoot = scratch + L"mods\\D2R-VR\\", mpq = modRoot + L"D2R-VR.mpq\\";
    CreateDirectoryW((scratch + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    CreateDirectoryW(mpq.c_str(), nullptr);
    // not the mod: nothing
    {
        const holesassets::Result r = holesassets::Prepare(game, L"", true);
        CHECK(r.state == holesassets::kNotHere && !r.wrote);
    }
    // made
    holesassets::Result r = holesassets::Prepare(game, modRoot, true);
    std::printf("made: %ls\n", r.text.c_str());
    CHECK(r.state == holesassets::kMade && r.wrote);
    for (const holeslist::Place& p : holeslist::kPlaces) {
        for (int lod = 0; lod < holeslist::kLods; ++lod) CHECK(Exists(In(mpq, holeslist::LodPath(p, lod))));
        CHECK(Exists(In(mpq, holeslist::UnitPath(p))));
    }
    CHECK(Exists(In(mpq, holeslist::kFile)));
    CHECK(Exists(modRoot + L"world_holes.json"));
    {   // the measures file reads, a line a place
        HANDLE f = CreateFileW(In(mpq, holeslist::kFile).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        std::string text(1 << 16, 0);
        DWORD got = 0;
        if (f != INVALID_HANDLE_VALUE) { ReadFile(f, text.data(), (DWORD)text.size(), &got, nullptr); CloseHandle(f); }
        text.resize(got);
        std::vector<holeslist::Line> lines;
        CHECK(holeslist::Parse(text, &lines) && (int)lines.size() == holeslist::kPlaceCount);
    }
    // again: nothing written
    r = holesassets::Prepare(game, modRoot, true);
    std::printf("again: %ls\n", r.text.c_str());
    CHECK(r.state == holesassets::kMade && !r.wrote);
    // a file of ours the player changed stays, made or not
    const std::wstring unit0 = In(mpq, holeslist::UnitPath(holeslist::kPlaces[0]));
    {
        HANDLE f = CreateFileW(unit0.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD put = 0;
        WriteFile(f, "mine", 4, &put, nullptr);
        CloseHandle(f);
    }
    r = holesassets::Prepare(game, modRoot, true);
    std::printf("changed by the player: %ls\n", r.text.c_str());
    CHECK(r.text.find(L"changed by you") != std::wstring::npos);
    // off: ours out, the player's stays
    r = holesassets::Prepare(game, modRoot, false);
    std::printf("off: %ls\n", r.text.c_str());
    CHECK(r.state == holesassets::kOff);
    CHECK(Exists(unit0));
    CHECK(!Exists(In(mpq, holeslist::LodPath(holeslist::kPlaces[0], 0))));
    CHECK(!Exists(In(mpq, holeslist::kFile)));
    CHECK(!Exists(modRoot + L"world_holes.json"));
    std::printf(g_fail ? "holes_assets_test: %d FAILED\n" : "holes_assets_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
