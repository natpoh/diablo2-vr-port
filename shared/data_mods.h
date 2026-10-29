#pragma once
// data_mods.h - other D2R data mods loaded together with D2R VR ([mods] merge_data_mods): their files merged into D2R VR's own mod.
//
// Why: D2R loads ONE -mod. Since 0.170 D2R VR starts as `D2RLoader.exe -mod D2R-VR`, so a mod the player built with D2RMM (olegbl's
// D2R Mod Manager: <game>\mods\D2RMM\D2RMM.mpq\data\..., started by it as `-mod D2RMM -txt`) is not loaded beside it (a player, 0.179).
// D2RLoader 1.3.1 has no layering of mods either: one active mod (`-mod`, `default_mod`) and, below it, only the loose -direct data root
// (its D2RCore.dll strings, read 2026-10-25). Pointing D2RMM's output at our mod is no way: D2RMM deletes <game>\mods\<name>\<name>.mpq
// whole before every build (its src/main/worker/BridgeAPI.ts) - our own files there would go with it.
//
// So D2R_VR_Settings (at its start, at START and at the launcher's Play = --start) copies the other mods' data files into
// mods\D2R-VR\D2R-VR.mpq\data, and keeps a record of what it put there (mods\D2R-VR\data_mods.txt). Every run compares that record with the
// mods as they are now: a file D2RMM built again is copied again, one it no longer has is taken out, everything is taken out when the tick
// is off. Only what the record names is ever removed, and only while it is still as we left it (size and time): D2R VR's own files there
// (the Arcane Sanctuary's visual files, the Kurast Docks' preset - written by this program from the player's game) are never deleted.
//
// Which mods: every folder mod in <game>\mods (<name>\<name>.mpq\modinfo.json) except D2R VR itself, a VR mod with its own launcher
// manifest (bearstonem's D2R-PCVR: its data is for its own plugin), a mod with D2RLoader plugins or patches of its own (they load only
// when that mod is started: its data alone would be half of it), a packed .mpq (no MPQ reader here), and the names in [mods] merge_skip.
// The order: D2RMM first, then by name; the first in that order keeps a file two of them have.
// The same file in D2R VR and in another mod: the caller's Merger decides - D2R VR's one change made on top of theirs where we know
// it (the canoe on the water, the Arcane Sanctuary without its flat stars), else D2R VR's own stays. D2R VR's own file is then set
// aside (mods\D2R-VR\data_mods_ours\...) and put back when the other mod's file goes. Every such choice is written to the log.
// The other mods' save folder (their modinfo.json "savepath") is not taken: D2R VR's characters stay where they are (only said).
//
// Windows file calls here; no game code. tools/data_mods_test.cpp tests it on a scratch folder.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "arcane_sky.h"
#include "kurast_boat.h"
#include "../tools/settings/lang.h"   // lang::TX: Settings' status line in the player's language (English in the tests)

namespace datamods {

constexpr const wchar_t* kOurMod = L"D2R-VR";
constexpr const wchar_t* kRecord = L"data_mods.txt";     // in the mod's folder (beside kurast_boat.json)
constexpr const wchar_t* kAside = L"data_mods_ours\\";   // D2R VR's own files while another mod's are in their place
constexpr const char* kRecordHead = "d2r_vr data mods 1";

// What the caller makes of a file both have: theirs with our change on top (out), theirs as it is, or ours.
enum class Pick { kMerged, kTheirs, kOurs };
// rel: the path under the .mpq folder, '/' between the parts, lower case ("data/hd/env/preset/act3/docktown/docktown3.json").
using Merger = std::function<Pick(const std::string& rel, const std::string& theirs, std::string* out, std::string* why)>;

struct Mod {
    std::wstring name;
    bool used = false;      // its files are merged (or would be)
    std::wstring why;       // why not, or a note (its save folder)
    int files = 0;          // files of it in D2R VR's mod now
};

enum State { kOff, kNone, kMerged, kUpToDate, kFailed, kNotHere };

struct Result {
    State state = kFailed;
    std::vector<Mod> mods;            // every mod found in <game>\mods but ours
    std::vector<std::wstring> log;    // each file's choice, each change
    std::wstring text;                // one line for the Status row
    int placed = 0, removed = 0, kept = 0, failed = 0;
    bool wrote = false;               // anything in D2R VR's mod changed
};

// D2R VR's own files in its mod are each one change of the game's file; on another mod's file of that name the same change is
// made on top of theirs (their file is what the player chose; our change is scenery only). A file we have no such change for: ours.
inline Pick OurChange(const std::string& rel, const std::string& theirs, std::string* out, std::string* why) {
    std::string w;
    if (rel == kurastboat::kPreset) {
        const kurastboat::State s = kurastboat::Lower(theirs, out, &w);
        if (s == kurastboat::kLowered) { *why = "the canoe on the water, [world] kurast_boat"; return Pick::kMerged; }
        *why = s == kurastboat::kNoNeed ? "theirs has no canoe in the sky" : "the canoe stays in the sky: their town is not as known (" + w + ")";
        return Pick::kTheirs;
    }
    for (int p = 0; p < 7; ++p) {
        if (rel != arcanesky::VisName(p)) continue;
        if (arcanesky::StripWeather(theirs, out, &w)) { *why = "the flat star cards taken out, [sky] arcane_sky"; return Pick::kMerged; }
        *why = "their file is not as known, so its flat stars stay (" + w + ")";
        return Pick::kTheirs;
    }
    return Pick::kOurs;
}

namespace detail {

inline std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}
inline std::string Utf8(const std::wstring& w) {
    std::string s(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr)), '\0');
    if (!s.empty()) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
    return s;
}
inline std::wstring Wide(const std::string& s) {
    std::wstring w(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0)), L'\0');
    if (!w.empty()) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
    return w;
}
inline std::wstring Slash(std::wstring d) {
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return d;
}
inline std::wstring Back(std::wstring rel) {   // '/' -> '\'
    for (wchar_t& c : rel) if (c == L'/') c = L'\\';
    return rel;
}
inline std::string Key(const std::wstring& rel) {   // lower case, '/'
    std::wstring k = Lower(rel);
    for (wchar_t& c : k) if (c == L'\\') c = L'/';
    return Utf8(k);
}
inline bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool IsFile(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
inline uint64_t Time(const FILETIME& t) { return (uint64_t)t.dwHighDateTime << 32 | t.dwLowDateTime; }
inline bool Stat(const std::wstring& p, uint64_t* size, uint64_t* time) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    *size = (uint64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow;
    *time = Time(a.ftLastWriteTime);
    return true;
}
inline bool Read(const std::wstring& path, std::string* out) {
    out->clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (256ll << 20);
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = out->empty() || (::ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size());
    }
    CloseHandle(f);
    return ok;
}
inline bool MakeDirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return IsDir(dir);
}
inline std::wstring Parent(const std::wstring& p) {
    const size_t at = p.find_last_of(L'\\');
    return at == std::wstring::npos ? std::wstring() : p.substr(0, at);
}
// Written beside itself first, then moved over: a game reading meanwhile never meets half a file.
inline bool Write(const std::wstring& path, const std::string& data) {
    if (!MakeDirs(Parent(path))) return false;
    const std::wstring tmp = path + L".d2rvr_tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = data.empty() || (::WriteFile(f, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size());
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
// A copy (its time with it, so the record can tell it from a later change); beside itself first, then moved over.
inline bool Copy(const std::wstring& from, const std::wstring& to) {
    if (!MakeDirs(Parent(to))) return false;
    const std::wstring tmp = to + L".d2rvr_tmp";
    if (!CopyFileW(from.c_str(), tmp.c_str(), FALSE)) return false;
    SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);   // a read-only source must not make our copy undeletable
    if (!MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
// Every file under dir (recursive), as paths relative to it with '\'.
inline void Files(const std::wstring& dir, const std::wstring& rel, std::vector<std::wstring>* out) {
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW((Slash(dir) + rel + L"*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring n = d.cFileName;
        if (n == L"." || n == L"..") continue;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Files(dir, rel + n + L"\\", out);   // no junctions: nothing outside the mod
        } else if (!(n.size() > 11 && Lower(n).compare(n.size() - 11, 11, L".d2rvr_tmp") == 0)) {
            out->push_back(rel + n);
        }
    } while (FindNextFileW(h, &d));
    FindClose(h);
}
inline bool AnyFile(const std::wstring& dir) {
    std::vector<std::wstring> f;
    Files(dir, L"", &f);
    return !f.empty();
}
// Empty folders left behind, from `dir` up to (not including) `stop`.
inline void PruneUp(std::wstring dir, const std::wstring& stop) {
    const std::wstring s = Lower(Slash(stop));
    while (dir.size() > s.size() && Lower(Slash(dir)).compare(0, s.size(), s) == 0 && RemoveDirectoryW(dir.c_str())) dir = Parent(dir);
}
inline std::vector<std::wstring> SplitList(const std::wstring& list) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : list + L",") {
        if (c == L',' || c == L';') {
            while (!cur.empty() && iswspace(cur.back())) cur.pop_back();
            size_t b = 0;
            while (b < cur.size() && iswspace(cur[b])) ++b;
            if (b < cur.size()) out.push_back(Lower(cur.substr(b)));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}
// modinfo.json's "savepath" (no general JSON reader: the one string)
inline std::string SavePath(const std::string& modinfo) {
    const size_t k = modinfo.find("\"savepath\"");
    if (k == std::string::npos) return std::string();
    const size_t q = modinfo.find('"', modinfo.find(':', k) + 1);
    if (q == std::string::npos) return std::string();
    const size_t e = modinfo.find('"', q + 1);
    return e == std::string::npos ? std::string() : modinfo.substr(q + 1, e - q - 1);
}

// The record: what we put into D2R VR's mod.
enum class How { kCopy, kMerged, kOver };   // copied (nothing of ours there) / theirs with our change (ours aside) / theirs over ours (ours aside)
struct Placed {
    std::wstring rel;    // as in the source mod, '\'
    std::wstring mod;
    How how = How::kCopy;
    uint64_t srcSize = 0, srcTime = 0;   // the other mod's file when it was taken
    uint64_t dstSize = 0, dstTime = 0;   // ours as we left it
};
inline const char* HowName(How h) { return h == How::kMerged ? "merged" : h == How::kOver ? "over" : "copy"; }
inline bool ParseHow(const std::string& s, How* h) {
    if (s == "copy") *h = How::kCopy;
    else if (s == "merged") *h = How::kMerged;
    else if (s == "over") *h = How::kOver;
    else return false;
    return true;
}
inline std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> f;
    size_t at = 0;
    for (;;) {
        const size_t t = line.find('\t', at);
        f.push_back(line.substr(at, t == std::string::npos ? std::string::npos : t - at));
        if (t == std::string::npos) return f;
        at = t + 1;
    }
}
inline std::map<std::string, Placed> LoadRecord(const std::wstring& path) {
    std::map<std::string, Placed> out;
    std::string text;
    if (!Read(path, &text)) return out;
    size_t at = 0;
    bool head = false;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!head) {
            if (line != kRecordHead) return out;   // not ours / another version: nothing is taken from it (nothing removed)
            head = true;
            continue;
        }
        const std::vector<std::string> f = Fields(line);
        Placed p;
        if (f.size() != 8 || f[0] != "placed" || !ParseHow(f[3], &p.how)) continue;
        p.rel = Back(Wide(f[1]));
        p.mod = Wide(f[2]);
        p.srcSize = strtoull(f[4].c_str(), nullptr, 10);
        p.srcTime = strtoull(f[5].c_str(), nullptr, 10);
        p.dstSize = strtoull(f[6].c_str(), nullptr, 10);
        p.dstTime = strtoull(f[7].c_str(), nullptr, 10);
        if (p.rel.empty() || p.rel.find(L"..") != std::wstring::npos || p.rel[0] == L'\\' || p.rel.find(L':') != std::wstring::npos) continue;
        out[Key(p.rel)] = p;
    }
    return out;
}
inline bool SaveRecord(const std::wstring& path, const std::map<std::string, Placed>& placed) {
    if (placed.empty()) return !IsFile(path) || DeleteFileW(path.c_str());
    std::string t = std::string(kRecordHead) + "\r\n";
    for (const auto& [k, p] : placed) {
        std::wstring rel = p.rel;
        for (wchar_t& c : rel) if (c == L'\\') c = L'/';
        t += "placed\t" + Utf8(rel) + "\t" + Utf8(p.mod) + "\t" + HowName(p.how) + "\t" + std::to_string(p.srcSize) + "\t" + std::to_string(p.srcTime) +
             "\t" + std::to_string(p.dstSize) + "\t" + std::to_string(p.dstTime) + "\r\n";
    }
    return Write(path, t);
}

}  // namespace detail

// The mods in <game>\mods that D2R VR could load with it, and why the others are not (no files touched).
// Order: D2RMM first, then by name; `used` ones only when `on`.
inline std::vector<Mod> Find(const std::wstring& game, const std::wstring& skipList) {
    using namespace detail;
    std::vector<Mod> mods;
    const std::wstring root = Slash(game) + L"mods\\";
    const std::vector<std::wstring> skip = SplitList(skipList);
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW((root + L"*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = d.cFileName;
            if (n == L"." || n == L".." || !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (_wcsicmp(n.c_str(), kOurMod) == 0) continue;
            Mod m;
            m.name = n;
            const std::wstring dir = root + n + L"\\";
            const std::wstring mpq = dir + n + L".mpq";
            if (IsFile(dir + L"vrmod-manifest.json")) m.why = L"a VR mod of its own (started from its launcher), not merged";
            else if (std::find(skip.begin(), skip.end(), Lower(n)) != skip.end()) m.why = L"left out ([mods] merge_skip)";
            else if (AnyFile(dir + L"d2rloader\\plugins") || AnyFile(dir + L"d2rloader\\patches"))
                m.why = L"has D2RLoader plugins or patches of its own - they load only when it is started on its own: not merged";
            else if (IsFile(mpq)) m.why = L"a packed .mpq - only folder mods can be merged";
            else if (!IsFile(mpq + L"\\modinfo.json")) m.why = L"no " + n + L".mpq\\modinfo.json: not a D2R mod";
            else if (!AnyFile(mpq + L"\\data")) m.why = L"no data files";
            else {
                m.used = true;
                std::string info;
                Read(mpq + L"\\modinfo.json", &info);
                const std::string save = SavePath(info);
                if (!save.empty() && save != "../" && save != "..\\" && save != "..")
                    m.why = L"its own save folder (" + Wide(save) + L") is not used: characters stay in D2R VR's";
            }
            mods.push_back(m);
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) {
        const bool am = _wcsicmp(a.name.c_str(), L"D2RMM") == 0, bm = _wcsicmp(b.name.c_str(), L"D2RMM") == 0;
        if (am != bm) return am;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return mods;
}

// Brings D2R VR's mod in line with the other mods as they are now (on), or takes every file of theirs out again (off).
// game: the game's folder; modRoot: <game>\mods\D2R-VR\ ("" = not installed as the mod: nothing done). The game must not be running.
inline Result Sync(const std::wstring& game, const std::wstring& modRoot, bool on, const std::wstring& skipList, const Merger& merge) {
    using namespace detail;
    Result r;
    const std::wstring mpq = modRoot.empty() ? std::wstring() : Slash(modRoot) + kOurMod + L".mpq\\";
    if (mpq.empty() || !IsDir(mpq)) {
        r.state = kNotHere;
        r.text = lang::TX(L"data_mods.no_mod", L"only in D2R VR's own mod (mods\\D2R-VR\\D2R-VR.mpq), which is not here");
        return r;
    }
    const std::wstring recordPath = Slash(modRoot) + kRecord;
    const std::wstring aside = Slash(modRoot) + kAside;
    std::map<std::string, Placed> record = LoadRecord(recordPath);
    r.mods = Find(game, skipList);

    // What is in place of what the record names: still as we left it, or changed since (then it is no longer ours to remove)
    for (auto it = record.begin(); it != record.end();) {
        uint64_t s = 0, t = 0;
        const std::wstring dst = mpq + it->second.rel;
        if (!Stat(dst, &s, &t)) {
            r.log.push_back(it->second.rel + L": gone from D2R VR's mod since it was merged");
            ++it;   // (still in the record: placed again below if wanted)
            continue;
        }
        if (s != it->second.dstSize || t != it->second.dstTime) {
            r.log.push_back(it->second.rel + L": changed since it was merged from " + it->second.mod + L" - not ours to remove: counted as D2R VR's own now");
            it = record.erase(it);
            continue;
        }
        ++it;
    }
    // D2R VR's own files: what is in its data folder and not ours from the record, and what is set aside
    std::set<std::string> ours;
    {
        std::vector<std::wstring> f;
        Files(mpq, L"data\\", &f);
        for (const std::wstring& rel : f)
            if (!record.count(Key(rel))) ours.insert(Key(rel));
        f.clear();
        Files(aside, L"data\\", &f);
        for (const std::wstring& rel : f) ours.insert(Key(rel));
    }

    // What should be there
    struct Want {
        std::wstring rel, mod, src;
        uint64_t size = 0, time = 0;
    };
    std::map<std::string, Want> want;
    if (on) {
        for (Mod& m : r.mods) {
            if (!m.used) continue;
            const std::wstring src = Slash(game) + L"mods\\" + m.name + L"\\" + m.name + L".mpq\\";
            std::vector<std::wstring> f;
            Files(src, L"data\\", &f);
            for (const std::wstring& rel : f) {
                const std::string k = Key(rel);
                if (k == "data/global/dataversionbuild.txt") continue;   // D2RMM's copy of the game's build stamp: not the mod's content
                auto w = want.find(k);
                if (w != want.end()) {
                    r.log.push_back(rel + L": in " + w->second.mod + L" and in " + m.name + L" - " + w->second.mod + L"'s taken (first in the order)");
                    continue;
                }
                Want x{rel, m.name, src + rel};
                if (!Stat(x.src, &x.size, &x.time)) continue;
                want[k] = x;
            }
        }
    }

    // Out: what the record names that is no longer wanted as it is
    auto restoreOurs = [&](const std::wstring& rel) {
        const std::wstring saved = aside + rel;
        if (!IsFile(saved)) return;
        if (MakeDirs(Parent(mpq + rel)) && MoveFileExW(saved.c_str(), (mpq + rel).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            r.log.push_back(rel + L": D2R VR's own file put back");
            PruneUp(Parent(saved), aside);
        } else {
            r.log.push_back(rel + L": D2R VR's own file could not be put back from data_mods_ours (error " + std::to_wstring(GetLastError()) + L")");
            ++r.failed;
        }
    };
    for (auto it = record.begin(); it != record.end();) {
        const Placed& p = it->second;
        const auto w = want.find(it->first);
        const bool hadOurs = p.how != How::kCopy;
        bool same = w != want.end() && _wcsicmp(w->second.mod.c_str(), p.mod.c_str()) == 0 && w->second.size == p.srcSize && w->second.time == p.srcTime;
        uint64_t s = 0, t = 0;
        const bool there = Stat(mpq + p.rel, &s, &t);
        if (same && there) {
            if (hadOurs) r.log.push_back(p.rel + L": " + p.mod + (p.how == How::kMerged ? L"'s with D2R VR's change on top" : L"'s over D2R VR's") + L" (as at the last start)");
            ++r.kept;
            ++it;
            continue;
        }
        if (there) {
            if (!DeleteFileW((mpq + p.rel).c_str())) {
                r.log.push_back(p.rel + L": could not be taken out (error " + std::to_wstring(GetLastError()) + L")");
                ++r.failed;
                ++it;
                continue;
            }
            r.log.push_back(p.rel + L": " + p.mod + L"'s taken out" + (w == want.end() ? L"" : L" (changed)"));
            ++r.removed;
            r.wrote = true;
        }
        if (hadOurs && w == want.end()) restoreOurs(p.rel);   // (still wanted: placed again below, ours stays aside)
        PruneUp(Parent(mpq + p.rel), mpq + L"data");
        it = record.erase(it);
    }

    // In: what is wanted and not there yet
    for (const auto& [k, w] : want) {
        if (record.count(k)) continue;
        const std::wstring dst = mpq + w.rel;
        Placed p;
        p.rel = w.rel;
        p.mod = w.mod;
        p.srcSize = w.size;
        p.srcTime = w.time;
        if (ours.count(k)) {
            // D2R VR has this file too: our change on top of theirs, theirs, or ours
            std::string theirs, made, why;
            if (!Read(w.src, &theirs)) {
                r.log.push_back(w.rel + L": " + w.mod + L"'s could not be read - D2R VR's own stays");
                ++r.failed;
                continue;
            }
            const Pick pick = merge ? merge(k, theirs, &made, &why) : Pick::kOurs;
            if (pick == Pick::kOurs) {
                r.log.push_back(w.rel + L": D2R VR has its own - D2R VR's kept, " + w.mod + L"'s left out" + (why.empty() ? L"" : L" (" + Wide(why) + L")"));
                continue;
            }
            const std::wstring saved = aside + w.rel;
            if (IsFile(saved) && IsFile(dst)) {   // ours aside AND a file of nobody's record in its place: neither is overwritten
                r.log.push_back(w.rel + L": D2R VR's own is in data_mods_ours and another file is in its place - both left, " + w.mod + L"'s left out");
                ++r.failed;
                continue;
            }
            if (!IsFile(saved)) {
                if (!MakeDirs(Parent(saved)) || !MoveFileExW(dst.c_str(), saved.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    r.log.push_back(w.rel + L": D2R VR's own could not be set aside (error " + std::to_wstring(GetLastError()) + L") - kept, " + w.mod +
                                    L"'s left out");
                    ++r.failed;
                    continue;
                }
            }
            const bool ok = pick == Pick::kMerged ? Write(dst, made) : Copy(w.src, dst);
            p.how = pick == Pick::kMerged ? How::kMerged : How::kOver;
            r.log.push_back(w.rel + L": " +
                            (pick == Pick::kMerged ? w.mod + L"'s with D2R VR's change on top" : w.mod + L"'s taken over D2R VR's") +
                            (why.empty() ? L"" : L" (" + Wide(why) + L")"));
            if (!ok) {
                r.log.push_back(w.rel + L": could not be written (error " + std::to_wstring(GetLastError()) + L")");
                ++r.failed;
                restoreOurs(w.rel);
                continue;
            }
        } else if (!Copy(w.src, dst)) {
            r.log.push_back(w.rel + L": " + w.mod + L"'s could not be copied (error " + std::to_wstring(GetLastError()) + L")");
            ++r.failed;
            continue;
        }
        if (!Stat(dst, &p.dstSize, &p.dstTime)) continue;
        record[k] = p;
        ++r.placed;
        r.wrote = true;
    }
    if (!SaveRecord(recordPath, record)) {
        r.log.push_back(L"mods\\D2R-VR\\data_mods.txt could not be written (error " + std::to_wstring(GetLastError()) + L")");
        ++r.failed;
    }

    for (const auto& [k, p] : record)
        for (Mod& m : r.mods)
            if (_wcsicmp(m.name.c_str(), p.mod.c_str()) == 0) ++m.files;
    // The Status row: short (which mods, which not); each mod's reason first in the log
    std::wstring used, notUsed;
    int usedCount = 0;
    bool otherSaves = false;
    std::vector<std::wstring> perMod;
    for (const Mod& m : r.mods) {
        if (m.used && on) {
            ++usedCount;
            used += (used.empty() ? L"" : L", ") + m.name + L" (" + std::to_wstring(m.files) + (m.files == 1 ? L" file)" : L" files)");
            otherSaves = otherSaves || !m.why.empty();
            perMod.push_back(m.name + L": merged, " + std::to_wstring(m.files) + L" file(s) in D2R VR's mod" + (m.why.empty() ? L"" : L"; " + m.why));
        } else if (m.used) {
            notUsed += (notUsed.empty() ? L"" : L", ") + m.name;
            perMod.push_back(m.name + L": not loaded (the tick is off)");
        } else {
            notUsed += (notUsed.empty() ? L"" : L", ") + m.name;
            perMod.push_back(m.name + L": " + m.why);
        }
    }
    r.log.insert(r.log.begin(), perMod.begin(), perMod.end());
    if (!on) {
        r.state = kOff;
        r.text = r.removed ? lang::TX(L"data_mods.off_removed", L"off - the other mods' files taken out of D2R VR's mod") : lang::TX(L"data_mods.off", L"off");
        if (!notUsed.empty()) r.text += L"; " + lang::TX(L"data_mods.not_loaded", L"not loaded: {mods}", {{L"mods", notUsed}});
    } else if (!usedCount) {
        r.state = kNone;
        r.text = r.mods.empty() ? lang::TX(L"data_mods.none_found", L"no other mods in the game's mods folder")
                                : lang::TX(L"data_mods.none_to_merge", L"none to merge ({mods}: why in d2r_vr_data_mods.txt)", {{L"mods", notUsed}});
    } else {
        r.state = r.failed ? kFailed : r.wrote ? kMerged : kUpToDate;
        r.text = r.wrote ? lang::TX(L"data_mods.merged", L"merged into D2R VR: {mods}", {{L"mods", used}})
                         : lang::TX(L"data_mods.loaded", L"loaded with D2R VR: {mods}", {{L"mods", used}});
        if (!notUsed.empty()) r.text += L"; " + lang::TX(L"data_mods.not_merged", L"not merged: {mods}", {{L"mods", notUsed}});
        if (otherSaves) r.text += L"; " + lang::TX(L"data_mods.saves", L"characters stay in D2R VR's save folder");
    }
    if (r.failed) r.text += L" - " + lang::TX(L"data_mods.failed", L"{n} file(s) failed, see d2r_vr_data_mods.txt", {{L"n", std::to_wstring(r.failed)}});
    return r;
}

}  // namespace datamods
