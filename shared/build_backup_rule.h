#pragma once
// build_backup_rule.h - backups of the installed D2R VR build (D2R VR Settings > Home "Back up this build", D2R VR Setup before an update):
// what a "build" is on disk, the backup folders' names, their manifest, which old ones go, and the order a restore puts the files back in.
// Pure (no file system here); tools/settings/build_backup.cpp does the copying, cleanroom/tests/build_backup_test.cpp tests both.
//
// A build is what Setup puts in, plus the player's own settings - NOT the game's saves ([support] save_backups keeps those) and NOT the caches
// the plugin makes again by itself (d2r_vr_fx_cache, the skill icons, the game's interface art, the props list, logs, dumps):
//   <game>\D2R_VR_Settings.exe, D2R_VR_Headset_Report.exe
//   <game>\mods\D2R-VR\                     the made files' receipts (*.json, *.stamp), the launcher's manifest and icon, the licence
//   <game>\mods\D2R-VR\d2rloader\plugins\   d2rl-vrcam.dll and the settings: d2r_vr*.ini / .txt / .json (d2r_vr.ini, the headset's, the
//                                           window's, the depth's, auto tune's result) - not the ini's own .before_* copies
//   <game>\mods\D2R-VR\d2rloader\d2r_vr_libs\   the weight / grip / gait / gesture / walk libraries, the crash reporter, OFXR Bridge
//   <game>\mods\D2R-VR\lang\                D2R VR Settings' language files
//   <game>\mods\D2R-VR\D2R-VR.mpq\          the game data the mod made from the player's game (the sky dome, the ceilings, the boat ...)
//   <game>\mods\D2R-VR\shipped_data\        Setup's copy of its own data files beside the .mpq
//   <game>\mods\D2R-VR\reshade\reshade-shaders\   the mod's effect and its pictures (the sky, the Arcane Sanctuary's ...)
//   %LOCALAPPDATA%\BodyWalkVR\plugins\game_link\game_link.dll, ...\game_link\d2r.txt   BodyWalk Game Link (VR) - never BodyWalk's profiles
// About 260 MB on Serg's install of 2026-11-25 (340 files; the .mpq's made data 137 MB of it).
// "Save settings" (Scope kSettings) takes only the settings areas (Area::settings: the plugins folder's d2r_vr*.ini / .txt / .json, a few
// hundred KB), and restoring one puts back only those - the build's code and data stay as they are.
//
// In a backup folder the files keep their places under "game\" (the game's folder) and "bodywalk\" (%LOCALAPPDATA%\BodyWalkVR): copying the
// contents of its game folder into the game's folder by hand restores the build too. backup.txt, written LAST, lists every file with its size:
// a folder without it is a backup that did not finish (never restored; removed at the next backup).
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace buildbackup {

constexpr const wchar_t* kFolder = L"build_backups\\";      // under <game>\mods\D2R-VR\ (removed with the mod by the uninstaller)
constexpr const wchar_t* kManifest = L"backup.txt";
constexpr const wchar_t* kHowTo = L"HOW_TO_RESTORE.txt";
constexpr const wchar_t* kReplaced = L"_replaced\\";       // under kFolder: the running D2R_VR_Settings.exe moved aside by a restore
constexpr const wchar_t* kGameTop = L"game\\";
constexpr const wchar_t* kBodyWalkTop = L"bodywalk\\";
constexpr const wchar_t* kSettingsExe = L"D2R_VR_Settings.exe";
constexpr int kKeepDefault = 3;   // [support] build_backup_keep: the automatic backups kept (Setup's and a restore's); the player's own are never removed
constexpr int kKeepMin = 1, kKeepMax = 10;

constexpr const wchar_t* kPending = L".restore_new";      // the restored settings program beside its place, when the running one could not be moved aside

enum Root { kGame = 0, kBodyWalk = 1 };
enum Kind { kManual = 0, kUpdate = 1, kRestore = 2 };
// What a backup holds: the whole build, or only the player's settings ("Save settings": the settings areas below).
enum Scope { kBuild = 0, kSettings = 1 };

// One place of the build. patterns: ';'-separated, '*' and '?', case-insensitive, matched against the file's NAME (in every subfolder when
// recurse). mirror: a restore also removes the files there (of these patterns) that the backup does not have - the made game data, whose
// extra files the game would load; elsewhere a file the backup does not have stays (a library or a language file of a newer build is
// harmless to an older one, and a player's own language file stays). order: the restore's order (low first; the settings program last).
struct Area {
    int root;
    const wchar_t* dir;        // under the root, with a trailing backslash ("" = the root itself)
    const wchar_t* patterns;
    bool recurse;
    bool mirror;
    int order;
    bool settings = false;     // in a settings-only backup too (the player's own setting files)
};

inline const std::vector<Area>& Areas() {
    static const std::vector<Area> a = {
        {kGame, L"mods\\D2R-VR\\D2R-VR.mpq\\", L"*", true, true, 10},
        {kGame, L"mods\\D2R-VR\\shipped_data\\", L"*", true, true, 11},
        {kGame, L"mods\\D2R-VR\\reshade\\reshade-shaders\\", L"*.fx;*.fxh;*.png;*.dds;*.json;*.txt", true, true, 12},
        {kGame, L"mods\\D2R-VR\\", L"*.json;*.stamp", false, true, 13},   // the made data's receipts go with it
        {kGame, L"mods\\D2R-VR\\", L"*.png;*.txt", false, false, 14},
        {kGame, L"mods\\D2R-VR\\lang\\", L"*.txt", false, false, 20},
        {kGame, L"mods\\D2R-VR\\d2rloader\\d2r_vr_libs\\", L"*.dll;*.exe;*.json;*.ini;*.txt", true, false, 30},
        {kBodyWalk, L"plugins\\game_link\\", L"game_link.dll", false, false, 31},
        {kBodyWalk, L"game_link\\", L"d2r.txt", false, false, 32},
        {kGame, L"mods\\D2R-VR\\d2rloader\\plugins\\", L"d2rl-vrcam.dll", false, false, 40},
        {kGame, L"mods\\D2R-VR\\d2rloader\\plugins\\", L"d2r_vr*.ini;d2r_vr*.txt;d2r_vr*.json", false, false, 50, true},   // the settings after the code
        {kGame, L"", L"D2R_VR_Headset_Report.exe", false, false, 60},
        {kGame, L"", L"D2R_VR_Settings.exe", false, false, 99},   // last: it may be the program doing the restore (moved aside, then replaced)
    };
    return a;
}

inline wchar_t Low(wchar_t c) { return (wchar_t)std::towlower(c); }

// '*' any run, '?' one character; case-insensitive.
inline bool Match(const wchar_t* pat, const wchar_t* name) {
    const wchar_t* star = nullptr;
    const wchar_t* back = nullptr;
    while (*name) {
        if (*pat == L'*') { star = pat++; back = name; continue; }
        if (*pat && (*pat == L'?' || Low(*pat) == Low(*name))) { ++pat; ++name; continue; }
        if (!star) return false;
        pat = star + 1;
        name = ++back;
    }
    while (*pat == L'*') ++pat;
    return *pat == 0;
}

inline bool MatchAny(const wchar_t* patterns, const std::wstring& name) {
    std::wstring p;
    for (const wchar_t* c = patterns;; ++c) {
        if (*c == L';' || *c == 0) {
            if (!p.empty() && Match(p.c_str(), name.c_str())) return true;
            p.clear();
            if (*c == 0) return false;
        } else {
            p += *c;
        }
    }
}

inline std::wstring Top(int root) { return root == kBodyWalk ? kBodyWalkTop : kGameTop; }

// The area a backup-relative path ("game\mods\D2R-VR\lang\de.txt") belongs to, or -1. The FIRST area that takes it (the list's order).
inline int AreaOf(const std::wstring& rel) {
    const std::vector<Area>& a = Areas();
    for (size_t i = 0; i < a.size(); ++i) {
        const std::wstring head = Top(a[i].root) + a[i].dir;
        if (rel.size() <= head.size() || _wcsnicmp(rel.c_str(), head.c_str(), head.size()) != 0) continue;
        const std::wstring tail = rel.substr(head.size());
        const size_t slash = tail.find_last_of(L'\\');
        if (slash != std::wstring::npos && !a[i].recurse) continue;
        const std::wstring name = slash == std::wstring::npos ? tail : tail.substr(slash + 1);
        if (!name.empty() && MatchAny(a[i].patterns, name)) return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------- names
struct Stamp { int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0; };

inline const wchar_t* KindWord(int kind) { return kind == kUpdate ? L"update" : kind == kRestore ? L"restore" : L"manual"; }
inline int KindFromWord(const std::wstring& w) { return w == L"update" ? kUpdate : w == L"restore" ? kRestore : kManual; }

// The version as a folder name may hold it ("0.223.1"); anything odd: "unknown".
inline std::wstring SafeVersion(const std::wstring& v) {
    std::wstring out;
    for (wchar_t c : v) if (std::iswalnum(c) || c == L'.' || c == L'-') out += c;
    if (out.empty() || out.size() > 32) return L"unknown";
    return out;
}

inline std::wstring StampText(const Stamp& t, wchar_t dateTime = L' ', wchar_t timeSep = L':') {
    wchar_t b[40];
    swprintf(b, 40, L"%04d-%02d-%02d%lc%02d%lc%02d%lc%02d", t.y, t.mo, t.d, dateTime, t.h, timeSep, t.mi, timeSep, t.s);
    return b;
}

inline const wchar_t* ScopeWord(int scope) { return scope == kSettings ? L"settings" : L"build"; }
inline int ScopeFromWord(const std::wstring& w) { return w == L"settings" ? kSettings : kBuild; }

// "0.223.1_2026-11-25_14-03-22" (a backup by the button), "..._update" (Setup's, before an update), "..._before_restore" (a restore's of
// what was there); a settings-only one "..._settings" / "..._settings_before_restore". Sorting by the date part sorts by time.
inline std::wstring FolderName(const std::wstring& version, const Stamp& t, int kind, int scope = kBuild) {
    std::wstring n = SafeVersion(version) + L"_" + StampText(t, L'_', L'-');
    if (scope == kSettings) n += L"_settings";
    if (kind == kUpdate) n += L"_update";
    if (kind == kRestore) n += L"_before_restore";
    return n;
}

// A folder name of ours (FolderName's shape): what Delete may remove.
inline bool IsBackupName(const std::wstring& n) {
    for (wchar_t c : n) if (!std::iswalnum(c) && c != L'.' && c != L'-' && c != L'_') return false;   // (never a path)
    const size_t us = n.find(L'_');
    if (us == std::wstring::npos || us == 0 || n.size() < us + 20) return false;
    const std::wstring date = n.substr(us + 1, 19);   // 2026-11-25_14-03-22
    for (size_t i = 0; i < date.size(); ++i) {
        const wchar_t c = date[i];
        const bool sep = i == 4 || i == 7 || i == 13 || i == 16;
        if (i == 10 ? c != L'_' : sep ? c != L'-' : !std::iswdigit(c)) return false;
    }
    const std::wstring rest = n.substr(us + 20);
    return rest.empty() || rest == L"_update" || rest == L"_before_restore" || rest == L"_settings" || rest == L"_settings_before_restore";
}

// ---------------------------------------------------------------- the manifest
struct Entry {
    std::wstring rel;      // under the backup folder: "game\..." or "bodywalk\..."
    uint64_t size = 0;
};

struct Info {
    std::wstring folder;   // its name under build_backups
    std::wstring version;
    std::wstring made;     // "2026-11-25 14:03:22" (local time)
    int kind = kManual;
    int scope = kBuild;
    uint64_t files = 0, bytes = 0;
    bool complete = false; // backup.txt there and read
};

inline std::wstring Manifest(const Info& info, const std::vector<Entry>& entries) {
    std::wstring t = L"D2R VR build backup\r\n";
    t += L"version=" + info.version + L"\r\n";
    t += L"made=" + info.made + L"\r\n";
    t += std::wstring(L"kind=") + KindWord(info.kind) + L"\r\n";
    t += std::wstring(L"scope=") + ScopeWord(info.scope) + L"\r\n";
    t += L"files=" + std::to_wstring(entries.size()) + L"\r\n";
    uint64_t bytes = 0;
    for (const Entry& e : entries) bytes += e.size;
    t += L"bytes=" + std::to_wstring(bytes) + L"\r\n";
    for (const Entry& e : entries) t += L"file=" + std::to_wstring(e.size) + L"|" + e.rel + L"\r\n";
    return t;
}

// false: not a manifest of ours. A file line naming a path outside the backup (.., a drive, a leading slash) makes it not ours either.
inline bool ParseManifest(const std::wstring& text, Info* info, std::vector<Entry>* entries) {
    if (text.rfind(L"D2R VR build backup", 0) != 0) return false;
    Info in = *info;
    std::vector<Entry> list;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == L"version") in.version = v;
        else if (k == L"made") in.made = v;
        else if (k == L"kind") in.kind = KindFromWord(v);
        else if (k == L"scope") in.scope = ScopeFromWord(v);
        else if (k == L"file") {
            const size_t bar = v.find(L'|');
            if (bar == std::wstring::npos) return false;
            Entry e;
            e.size = wcstoull(v.substr(0, bar).c_str(), nullptr, 10);
            e.rel = v.substr(bar + 1);
            if (e.rel.empty() || e.rel.find(L"..") != std::wstring::npos || e.rel.find(L':') != std::wstring::npos || e.rel[0] == L'\\' ||
                e.rel[0] == L'/' || AreaOf(e.rel) < 0)
                return false;
            list.push_back(e);
        }
    }
    if (in.scope == kSettings)   // a settings backup puts back settings only
        for (const Entry& e : list) if (!Areas()[AreaOf(e.rel)].settings) return false;
    in.files = list.size();
    in.bytes = 0;
    for (const Entry& e : list) in.bytes += e.size;
    in.complete = true;
    *info = in;
    if (entries) *entries = list;
    return true;
}

// ---------------------------------------------------------------- which go
// The folders to remove: every backup that did not finish, and the automatic ones (Setup's, a restore's) past the newest `keepAuto`. The
// player's own backups (the button) are never removed here - only by Delete. `keep`: never removed, but counted (the one being made now,
// the one a restore is about to put back).
inline std::vector<std::wstring> Prune(std::vector<Info> list, int keepAuto, const std::vector<std::wstring>& keep = {}) {
    keepAuto = std::clamp(keepAuto, kKeepMin, kKeepMax);
    std::vector<std::wstring> out;
    std::sort(list.begin(), list.end(), [](const Info& a, const Info& b) { return a.made != b.made ? a.made > b.made : a.folder > b.folder; });
    int autoSeen = 0;
    for (const Info& i : list) {
        const bool kept = std::find(keep.begin(), keep.end(), i.folder) != keep.end();
        if (!i.complete) { if (!kept) out.push_back(i.folder); continue; }
        if (i.kind == kManual) continue;
        if (++autoSeen > keepAuto && !kept) out.push_back(i.folder);
    }
    return out;
}

// Newest first, for the list in Settings.
inline void SortNewestFirst(std::vector<Info>* list) {
    std::sort(list->begin(), list->end(), [](const Info& a, const Info& b) { return a.made != b.made ? a.made > b.made : a.folder > b.folder; });
}

// ---------------------------------------------------------------- the restore's order
// The files in the order a restore puts them back: by their area's order (the made data, the libraries, the plugin, its settings, the
// settings program last), then by path. An entry of no area is left out (a manifest of ours never has one).
inline std::vector<Entry> RestoreOrder(std::vector<Entry> entries) {
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const Entry& e) { return AreaOf(e.rel) < 0; }), entries.end());
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const int oa = Areas()[AreaOf(a.rel)].order, ob = Areas()[AreaOf(b.rel)].order;
        return oa != ob ? oa < ob : _wcsicmp(a.rel.c_str(), b.rel.c_str()) < 0;
    });
    return entries;
}

// The files a mirrored restore removes: those `now` has in a mirrored area that the backup does not.
inline std::vector<std::wstring> MirrorExtras(const std::vector<Entry>& backup, const std::vector<Entry>& now) {
    std::vector<std::wstring> out;
    for (const Entry& n : now) {
        const int a = AreaOf(n.rel);
        if (a < 0 || !Areas()[a].mirror) continue;
        bool there = false;
        for (const Entry& b : backup) if (_wcsicmp(b.rel.c_str(), n.rel.c_str()) == 0) { there = true; break; }
        if (!there) out.push_back(n.rel);
    }
    return out;
}

// "259 MB", "12.4 MB", "640 KB"
inline std::wstring SizeText(uint64_t bytes) {
    wchar_t b[32];
    const double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 100.0) swprintf(b, 32, L"%.0f MB", mb);
    else if (mb >= 1.0) swprintf(b, 32, L"%.1f MB", mb);
    else swprintf(b, 32, L"%.0f KB", (double)bytes / 1024.0);
    return b;
}

}  // namespace buildbackup
