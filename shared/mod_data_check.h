#pragma once
// mod_data_check.h - [mods] restore_mod_data: D2R VR's own files in its mod's data folder (mods\D2R-VR\D2R-VR.mpq) found gone, and put back.
//
// Why (barti4033, 0.218.3, 2026-11-17/18): the game sky's dome was not shown - the bare void behind the trees, the fog unable to tint it -
// because every file D2R VR Settings had made into mods\D2R-VR\D2R-VR.mpq from the player's game (the sky domes, the cave ceilings, the
// canoe, the world's patches) AND the act skies Setup puts there were gone at each start, until Settings was opened again. His D2RLoader
// log gave it away: the mod's modinfo.json savepath "D2R-VR/" (ours is "../": every other player's log) and 19 of the game's tables
// from the mod's own TXT (ours has none). That is D2RMM (olegbl's D2R Mod Manager) with its output mod named D2R-VR: before every build it
// deletes mods\<name>\<name>.mpq whole (its src/main/worker/BridgeAPI.ts), then writes its own files and a modinfo.json of its own
// (savepath "<name>/") - see shared/data_mods.h, the supported way to load D2RMM's mods with ours ([mods] merge_data_mods).
// What survives such a wipe is everything in mods\D2R-VR beside the .mpq folder: the plugin, its ini, and the makers' receipts
// (sky_dome.json, cave_ceiling.json, ...: which files each maker wrote into the .mpq).
//
// So: Look() reads those receipts and names the files they list that are not there any more, and Setup's own files in the .mpq that are
// gone - Setup keeps a copy of them beside the .mpq (kShipped, installer\mod_files.inc), and RestoreShipped() copies a missing one back.
// The plugin looks at the game's start and, with [mods] restore_mod_data on, runs D2R_VR_Settings.exe --restore-mod-data (the makers,
// no window) when something is gone; off, the same finding is one log line. modinfo.json is never touched (its save folder is the
// player's characters' place now); only said.
//
// Windows file calls and string work only; no game code. tools/mod_data_check_test.cpp tests it on a scratch folder.
#include <windows.h>

#include <string>
#include <vector>

namespace moddata {

// The makers' receipts beside the mod's .mpq folder (relative to mods\D2R-VR\): each lists the files it wrote, by the game folder's path
constexpr const wchar_t* kReceipts[] = {
    L"sky_dome.json",        // [sky] game_sky: the domes' models, the act skies' atlas, the units (tools/settings/dome_assets.cpp)
    L"cave_ceiling.json",    // [ceiling] game_ceiling & co. (dome_assets.cpp PrepareCeiling)
    L"kurast_boat.json",     // [world] kurast_boat: the Kurast Docks' preset (boat_assets.cpp)
    L"world_holes.json",     // [holes] patch_world (holes_assets.cpp)
    L"wrist_orbs.json",      // [hud] wrist_game_models (orb_assets.cpp)
    L"campfire_scene.json",  // [campfire] enabled (campfire_assets_unleashed.cpp)
    L"reshade\\reshade-shaders\\Textures\\D2R_Arcane\\made.json",   // [sky] arcane_sky: its visual files in the .mpq (arcane_assets.cpp)
};
constexpr const char* kMpqPrefix = "mods/D2R-VR/D2R-VR.mpq/";   // a receipt's name of a file in the .mpq
constexpr const wchar_t* kMpq = L"D2R-VR.mpq\\";
constexpr const wchar_t* kShipped = L"shipped_data\\";   // Setup's copy of its own files in the .mpq, the same tree under it (mod_files.inc)

// --- pure

// The .mpq-relative paths ("data/hd/...", '/') a receipt names, in its order; other names (outside the .mpq) left out.
inline std::vector<std::string> ReceiptFiles(const std::string& receipt) {
    std::vector<std::string> out;
    const std::string prefix = std::string("\"") + kMpqPrefix;
    for (size_t at = receipt.find(prefix); at != std::string::npos; at = receipt.find(prefix, at + 1)) {
        const size_t from = at + prefix.size(), to = receipt.find('"', from);
        if (to == std::string::npos) break;
        if (receipt.compare(to, 3, "\":\"") != 0) continue;   // a name is followed by its hash
        if (to > from) out.push_back(receipt.substr(from, to - from));
    }
    return out;
}

// modinfo.json's "savepath" ("" when it has none); `found` false when the text has no such key
inline std::string SavePathOf(const std::string& modinfo, bool* found = nullptr) {
    if (found) *found = false;
    const size_t k = modinfo.find("\"savepath\"");
    if (k == std::string::npos) return "";
    const size_t colon = modinfo.find(':', k + 10);
    if (colon == std::string::npos) return "";
    const size_t q1 = modinfo.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    const size_t q2 = modinfo.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    if (found) *found = true;
    return modinfo.substr(q1 + 1, q2 - q1 - 1);
}

// Setup's modinfo.json says savepath "../" (installer\mod_layout.inc WriteModInfo); D2RMM's says "<its output name>/"
inline bool ModInfoOurs(const std::string& modinfo) {
    bool found = false;
    const std::string s = SavePathOf(modinfo, &found);
    return found && (s == "../" || s == "..\\" || s == "..");
}

struct Check {
    bool mod = false;                    // mods\D2R-VR\D2R-VR.mpq is a folder (the D2RLoader mod's layout)
    int named = 0;                       // files the receipts name in the .mpq
    std::vector<std::string> missing;    // ... of them not there (or empty)
    int shipped = 0;                     // Setup's files kept beside the .mpq
    std::vector<std::string> shippedMissing;   // ... of them not in the .mpq
    bool modinfo = false;                // the .mpq's modinfo.json is there
    bool modinfoOurs = false;            // ... with Setup's savepath "../"
    std::string savepath;                // its savepath as it is
    bool Gone() const { return !missing.empty() || !shippedMissing.empty(); }
};

// One line for the log (no prefix): what is gone, and a word on the likely cause
inline std::string Line(const Check& c) {
    if (!c.mod) return "not the mod's layout (no mods\\D2R-VR\\D2R-VR.mpq folder): nothing looked at";
    std::string s;
    if (!c.Gone()) {
        s = "D2R VR's files in mods\\D2R-VR\\D2R-VR.mpq all there (" + std::to_string(c.named) + " made from the game by Settings, " +
            std::to_string(c.shipped) + " of Setup's)";
    } else {
        s = "D2R VR's files in mods\\D2R-VR\\D2R-VR.mpq GONE: " + std::to_string(c.missing.size()) + " of the " + std::to_string(c.named) +
            " Settings made from the game, " + std::to_string(c.shippedMissing.size()) + " of Setup's " + std::to_string(c.shipped);
        const std::string& first = !c.missing.empty() ? c.missing.front() : c.shippedMissing.front();
        s += " (" + first + (c.missing.size() + c.shippedMissing.size() > 1 ? " ...)" : ")");
    }
    if (!c.modinfo) s += "; its modinfo.json is not there";
    else if (!c.modinfoOurs)
        s += "; its modinfo.json is not Setup's (savepath \"" + c.savepath + "\", ours \"../\") - another program rebuilt the folder: D2RMM "
             "with its output mod named D2R-VR deletes it whole at every build (tick Settings' other mods instead and name D2RMM's output D2RMM)";
    return s;
}

// --- files

namespace detail {
inline bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool NonEmptyFile(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    return GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) && !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           (a.nFileSizeHigh || a.nFileSizeLow);
}
inline bool Read(const std::wstring& path, std::string* out, size_t limit = 4u << 20) {
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && (size_t)size.QuadPart <= limit;
    if (ok && size.QuadPart) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ::ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size();
    }
    CloseHandle(f);
    return ok;
}
inline std::wstring Wide(const std::string& s) {
    std::wstring w(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0)), L'\0');
    if (!w.empty()) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
    return w;
}
inline std::string Utf8(const std::wstring& w) {
    std::string s(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr)), '\0');
    if (!s.empty()) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
    return s;
}
inline std::wstring Back(std::string rel) {
    for (char& c : rel) if (c == '/') c = '\\';
    return Wide(rel);
}
// every file under dir (with its slash), relative, '/' between the parts
inline void Files(const std::wstring& dir, const std::string& rel, std::vector<std::string>* out, int depth = 0) {
    if (depth > 16) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        const std::string r = rel + Utf8(n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Files(dir + n + L"\\", r + "/", out, depth + 1);
        else out->push_back(r);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
inline bool MakeDirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return IsDir(dir);
}
}  // namespace detail

// modRoot: <game>\mods\D2R-VR\ with its trailing backslash
inline Check Look(const std::wstring& modRoot) {
    Check c;
    const std::wstring mpq = modRoot + kMpq;
    c.mod = detail::IsDir(mpq);
    if (!c.mod) return c;
    for (const wchar_t* r : kReceipts) {
        std::string text;
        if (!detail::Read(modRoot + r, &text)) continue;
        for (const std::string& rel : ReceiptFiles(text)) {
            ++c.named;
            if (!detail::NonEmptyFile(mpq + detail::Back(rel))) c.missing.push_back(rel);
        }
    }
    std::vector<std::string> shipped;
    detail::Files(modRoot + kShipped, "", &shipped);
    for (const std::string& rel : shipped) {
        ++c.shipped;
        if (!detail::NonEmptyFile(mpq + detail::Back(rel))) c.shippedMissing.push_back(rel);
    }
    std::string info;
    c.modinfo = detail::Read(mpq + L"modinfo.json", &info, 64u << 10);
    if (c.modinfo) {
        c.savepath = SavePathOf(info);
        c.modinfoOurs = ModInfoOurs(info);
    }
    return c;
}

// Setup's files kept beside the .mpq (kShipped) copied into it where they are not (or empty); a file there is never replaced.
// Returns how many were put back; `failed` those that could not be.
inline int RestoreShipped(const std::wstring& modRoot, std::vector<std::string>* restored = nullptr, int* failed = nullptr) {
    if (failed) *failed = 0;
    const std::wstring mpq = modRoot + kMpq;
    if (!detail::IsDir(mpq)) return 0;
    std::vector<std::string> shipped;
    detail::Files(modRoot + kShipped, "", &shipped);
    int n = 0;
    for (const std::string& rel : shipped) {
        const std::wstring to = mpq + detail::Back(rel);
        if (detail::NonEmptyFile(to)) continue;
        const std::wstring from = modRoot + kShipped + detail::Back(rel);
        const std::wstring tmp = to + L".tmp";
        const bool ok = detail::MakeDirs(to.substr(0, to.find_last_of(L'\\'))) && CopyFileW(from.c_str(), tmp.c_str(), FALSE) &&
                        MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (!ok) {
            DeleteFileW(tmp.c_str());
            if (failed) ++*failed;
            continue;
        }
        ++n;
        if (restored) restored->push_back(rel);
    }
    return n;
}

}  // namespace moddata
