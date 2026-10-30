// menu_assets.cpp - the skill panel's game look: the game's own interface art beside the plugin ([openxr] skill_panel_game_look).
//
// D2R_VR_Settings does it (at its start, when the tick is set, and Setup runs it once with --prepare-assets), never the game. The game's
// archives are read through CascLib (MIT, Ladislav Zezula), read only. What comes out is Blizzard's files as they are, on this machine,
// beside the plugin: never in the repository or a build.

#include "menu_assets.h"
#include "lang.h"   // lang::TX: the result line in the player's language (English in the tests)

#include <windows.h>

#include <string>

#include "CascLib.h"
#include "game_ui_files.h"

namespace menuassets {
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

// Written beside itself first, then moved over the old one: a panel reading the folder meanwhile never meets half a file.
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

}  // namespace

Result Prepare(const std::wstring& game, const std::wstring& pluginDir, bool on) {
    Result r;
    if (!on) {
        r.ok = true;
        r.text = lang::TX(L"menu_art.off", L"off ([openxr] skill_panel_game_look): the skill panel in its own look");
        return r;
    }
    if (pluginDir.empty() || !IsDir(pluginDir)) {
        r.text = lang::TX(L"menu_art.no_plugin_dir", L"the plugin's folder was not found: the skill panel keeps its own look");
        return r;
    }
    const std::wstring dir = pluginDir + gameuifiles::kFolder + L"\\";
    // Nothing to do when every piece is there (the game is not opened: its archives take a moment).
    bool all = IsDir(dir);
    for (int i = 0; all && i < gameuifiles::kFileCount; ++i) all = GetFileAttributesW((dir + gameuifiles::kFiles[i].leaf).c_str()) != INVALID_FILE_ATTRIBUTES;
    if (all) {
        r.ok = true;
        r.text = lang::TX(L"menu_art.ready", L"ready: the game's own frames, buttons and sliders beside the plugin (d2r_vr_game_ui)");
        return r;
    }
    std::wstring root = game;
    if (!root.empty() && root.back() == L'\\') root.pop_back();
    HANDLE storage = nullptr;
    if (!CascOpenStorage(root.c_str(), 0, &storage)) {
        r.text = lang::TX(L"menu_art.no_archives", L"the game's archives could not be opened (error {error}); it is tried again at the next start", {{L"error", std::to_wstring(GetLastError())}});
        return r;
    }
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring missing;
    for (int i = 0; i < gameuifiles::kFileCount; ++i) {
        const std::wstring path = dir + gameuifiles::kFiles[i].leaf;
        std::string bytes, old;
        if (!CascRead(storage, gameuifiles::kFiles[i].archive, &bytes) || bytes.empty()) {
            missing += (missing.empty() ? L"" : L", ") + std::wstring(gameuifiles::kFiles[i].leaf);
            continue;
        }
        if (ReadFile(path, &old) && old == bytes) continue;
        if (!WriteFile(path, bytes.data(), bytes.size())) {
            CascCloseStorage(storage);
            r.text = lang::TX(L"menu_art.not_written", L"could not write {file} (error {error})", {{L"file", std::wstring(gameuifiles::kFiles[i].leaf)}, {L"error", std::to_wstring(GetLastError())}});
            return r;
        }
        r.wrote = true;
    }
    CascCloseStorage(storage);
    if (!missing.empty()) {
        r.text = lang::TX(L"menu_art.missing", L"not in this game: {files} - the skill panel keeps its own look", {{L"files", missing}});
        return r;
    }
    r.ok = true;
    r.text = lang::TX(L"menu_art.made", L"made from your game: its own frames, buttons and sliders for the skill panel (d2r_vr_game_ui)");
    return r;
}

}  // namespace menuassets
