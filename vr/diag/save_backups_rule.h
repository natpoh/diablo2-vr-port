// vr/diag/save_backups_rule.h - [support] save_backups' rules (vr/diag/save_backups.cpp): which of the game's save files a backup
// copies, whether a file's bytes are a good save (a power cut leaves a file of its size in zeros: never backed up), when the folder has
// changed enough for a new backup, a backup folder's name, and which old folders go (older than the days, then the oldest while all are
// over the size - never the one that holds the newest good copy of a file). Pure: no file system (tools/save_backups_test.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vrc {
namespace savebackupsrule {

inline std::wstring Key(const std::wstring& name) {   // Windows' names: the same file whatever the case
    std::wstring k = name;
    for (wchar_t& c : k) c = (wchar_t)towlower(c);
    return k;
}

inline bool EndsWith(const std::wstring& key, const wchar_t* e) {
    const size_t n = wcslen(e);
    return key.size() >= n && key.compare(key.size() - n, n, e) == 0;
}

// The save folder's files a backup copies: the characters (.d2s), the shared stashes (.d2i), and each character's controls (.ctl), keys
// (.key), maps (.map, .ma0 - .ma99) and .d2rl. Not the game's .ctlo / .keyo / .json, not a ".broken_..." copy of someone's.
inline bool WantFile(const std::wstring& name) {
    const std::wstring k = Key(name);
    for (const wchar_t* e : {L".d2s", L".d2i", L".ctl", L".key", L".map", L".d2rl"})
        if (EndsWith(k, e)) return k.size() > wcslen(e);
    const size_t dot = k.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0 || k.compare(dot, 3, L".ma") != 0) return false;
    const size_t digits = k.size() - dot - 3;
    if (digits < 1 || digits > 2) return false;
    for (size_t i = dot + 3; i < k.size(); ++i)
        if (k[i] < L'0' || k[i] > L'9') return false;
    return true;
}

inline uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline bool Magic(const uint8_t* p) { return p[0] == 0x55 && p[1] == 0xAA && p[2] == 0x55 && p[3] == 0xAA; }

// A character's checksum (its bytes 12-15 counted as zeros): each byte added to the sum turned left by one.
inline uint32_t D2sChecksum(const uint8_t* p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; ++i) s = ((s << 1) | (s >> 31)) + (i >= 12 && i < 16 ? 0u : p[i]);
    return s;
}

// A good save: never empty or all zeros (what a power cut leaves). A .d2s starts 55 AA 55 AA, its bytes 8-11 are its length and 12-15
// its checksum (a file the game was still writing fails it); a .d2i is sections, each 55 AA 55 AA with its length at 16, end to end
// to the file's end. The others have no header: any byte not zero.
inline bool Valid(const std::wstring& name, const uint8_t* p, size_t n) {
    if (n == 0) return false;
    bool any = false;
    for (size_t i = 0; i < n && !any; ++i) any = p[i] != 0;
    if (!any) return false;
    const std::wstring k = Key(name);
    if (EndsWith(k, L".d2s")) return n >= 16 && Magic(p) && U32(p + 8) == n && U32(p + 12) == D2sChecksum(p, n);
    if (EndsWith(k, L".d2i")) {
        size_t at = 0;
        while (at < n) {
            if (n - at < 20 || !Magic(p + at)) return false;
            const uint32_t len = U32(p + at + 16);
            if (len < 20 || len > n - at) return false;
            at += len;
        }
        return true;
    }
    return true;
}

// A file as the folder lists it: its size and last write (FILETIME ticks).
struct FileState {
    std::wstring name;
    uint64_t size = 0;
    uint64_t wrote = 0;
};
using States = std::map<std::wstring, FileState>;   // by Key(name)

inline States ByKey(const std::vector<FileState>& files) {
    States s;
    for (const FileState& f : files) s[Key(f.name)] = f;
    return s;
}

enum class Decide { Same, Wait, Go };

// A backup now? `last`: the files as the last backup (or the last look that found nothing good to keep) saw them; `prev`: the poll before
// this one. Same: nothing new or changed (a file gone is no reason). Go: something changed and it is still - every changed file written
// at least `settle` ticks ago, or the folder as the poll before saw it (the game writes a character's files together, in a few ms).
// Wait: changed, still being written.
inline Decide Changed(const std::vector<FileState>& now, const States& last, const States& prev, uint64_t nowTicks, uint64_t settle) {
    bool changed = false, still = true;
    for (const FileState& f : now) {
        auto it = last.find(Key(f.name));
        if (it != last.end() && it->second.size == f.size && it->second.wrote == f.wrote) continue;
        changed = true;
        if (f.wrote + settle > nowTicks) still = false;
    }
    if (!changed) return Decide::Same;
    if (still) return Decide::Go;
    const States n = ByKey(now);
    if (n.size() != prev.size()) return Decide::Wait;
    for (const auto& [k, f] : n) {
        auto it = prev.find(k);
        if (it == prev.end() || it->second.size != f.size || it->second.wrote != f.wrote) return Decide::Wait;
    }
    return Decide::Go;
}

// A backup's folder: D2R VR Backups\YYYY-MM-DD_HH-MM-SS (local time) - names sort as they came (as session_logs_rule.h's).
inline std::wstring FolderName(int y, int mo, int d, int h, int mi, int s) {
    wchar_t b[32];
    swprintf(b, 32, L"%04d-%02d-%02d_%02d-%02d-%02d", y, mo, d, h, mi, s);
    return b;
}

// A name of ours (FolderName's shape) and its day as yyyymmdd; anything else in the backups folder is never touched.
inline bool ParseFolder(const std::wstring& name, int* yyyymmdd) {
    if (name.size() != 19) return false;
    for (size_t i = 0; i < 19; ++i) {
        const wchar_t c = name[i];
        const bool sep = i == 4 || i == 7 || i == 13 || i == 16;
        if (i == 10 ? c != L'_' : sep ? c != L'-' : (c < L'0' || c > L'9')) return false;
    }
    if (yyyymmdd) *yyyymmdd = std::stoi(name.substr(0, 4)) * 10000 + std::stoi(name.substr(5, 2)) * 100 + std::stoi(name.substr(8, 2));
    return true;
}

// The day `days` before y-mo-d as yyyymmdd (the civil calendar, days from 1970).
inline int DayBefore(int y, int mo, int d, int days) {
    auto toDays = [](int yy, int mm, int dd) {   // H. Hinnant's days_from_civil
        yy -= mm <= 2;
        const int era = (yy >= 0 ? yy : yy - 399) / 400;
        const int yoe = yy - era * 400;
        const int doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
        return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    };
    int z = toDays(y, mo, d) - days + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int dd = doy - (153 * mp + 2) / 5 + 1;
    const int mm = mp + (mp < 10 ? 3 : -9);
    const int yy = yoe + era * 400 + (mm <= 2);
    return yy * 10000 + mm * 100 + dd;
}

struct Folder {
    std::wstring name;
    uint64_t bytes = 0;
    std::vector<std::wstring> files;   // the saves in it (WantFile's)
};

// The newest folder holding each file (by Key): where its last good copy is.
inline std::map<std::wstring, std::wstring> Newest(const std::vector<Folder>& all) {
    std::map<std::wstring, std::wstring> at;
    for (const Folder& f : all) {
        if (!ParseFolder(f.name, nullptr)) continue;
        for (const std::wstring& file : f.files) {
            std::wstring& w = at[Key(file)];
            if (f.name > w) w = f.name;
        }
    }
    return at;
}

// The folders to delete: every one of ours from before oldestDay (yyyymmdd), then the oldest while the rest is over maxBytes. Never the
// current game's (current), and never one that holds the newest copy of a file - a character not played for months keeps its last good
// copy, and so does one the power cut zeroed (its newer backups do not have it).
inline std::vector<std::wstring> ToDelete(std::vector<Folder> all, int oldestDay, uint64_t maxBytes, const std::wstring& current) {
    std::set<std::wstring> keepAlways = {current};
    for (const auto& kv : Newest(all)) keepAlways.insert(kv.second);
    std::vector<Folder> keep;
    std::vector<std::wstring> out;
    for (const Folder& f : all) {
        int day = 0;
        if (!ParseFolder(f.name, &day)) continue;
        if (!keepAlways.count(f.name) && day < oldestDay) out.push_back(f.name);
        else keep.push_back(f);
    }
    for (size_t i = 1; i < keep.size(); ++i)   // by name: some hundreds, insertion sort
        for (size_t j = i; j > 0 && keep[j].name < keep[j - 1].name; --j) std::swap(keep[j], keep[j - 1]);
    uint64_t total = 0;
    for (const Folder& f : keep) total += f.bytes;
    for (const Folder& f : keep) {
        if (total <= maxBytes) break;
        if (keepAlways.count(f.name)) continue;
        out.push_back(f.name);
        total -= f.bytes;
    }
    return out;
}

}  // namespace savebackupsrule
}  // namespace vrc
