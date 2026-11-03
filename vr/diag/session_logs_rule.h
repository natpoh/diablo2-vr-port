// vr/diag/session_logs_rule.h - [support] session_logs' rules (vr/diag/session_logs.cpp): which log files a game's folder copies, a
// folder's name from its start time, and which old folders go (older than the days kept, then the oldest while all of them are over the
// size). Pure: no file system (tools/session_logs_test.cpp).
#pragma once

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace vrc {
namespace sessionlogsrule {

// A game's folder: logs\sessions\YYYY-MM-DD_HH-MM-SS (its start, local time) - names sort as the games came.
inline std::wstring FolderName(int y, int mo, int d, int h, int mi, int s) {
    wchar_t b[32];
    swprintf(b, 32, L"%04d-%02d-%02d_%02d-%02d-%02d", y, mo, d, h, mi, s);
    return b;
}

// A name of ours (FolderName's shape) and its day as yyyymmdd; anything else in sessions\ is never touched.
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

// The files a game's folder copies: the text logs (.log, .txt), not the headset screenshots' and videos' notes.
inline bool WantFile(const std::wstring& name) {
    auto ends = [&](const wchar_t* e) {
        const size_t n = wcslen(e);
        if (name.size() < n) return false;
        for (size_t i = 0; i < n; ++i)
            if (towlower(name[name.size() - n + i]) != e[i]) return false;
        return true;
    };
    if (!ends(L".log") && !ends(L".txt")) return false;
    return name.rfind(L"headset_shot", 0) != 0 && name.rfind(L"headset_clip", 0) != 0;
}

struct Folder {
    std::wstring name;
    uint64_t bytes = 0;
};

// The folders to delete: every one of ours from before oldestDay (yyyymmdd), then the oldest while the rest is over maxBytes. The
// current game's folder (current) never; folders sorted by name = by time.
inline std::vector<std::wstring> ToDelete(std::vector<Folder> all, int oldestDay, uint64_t maxBytes, const std::wstring& current) {
    std::vector<Folder> keep;
    std::vector<std::wstring> out;
    for (const Folder& f : all) {
        int day = 0;
        if (!ParseFolder(f.name, &day)) continue;
        if (f.name != current && day < oldestDay) out.push_back(f.name);
        else keep.push_back(f);
    }
    for (size_t i = 1; i < keep.size(); ++i)   // by name: a few dozen, insertion sort
        for (size_t j = i; j > 0 && keep[j].name < keep[j - 1].name; --j) std::swap(keep[j], keep[j - 1]);
    uint64_t total = 0;
    for (const Folder& f : keep) total += f.bytes;
    for (const Folder& f : keep) {
        if (total <= maxBytes) break;
        if (f.name == current) continue;
        out.push_back(f.name);
        total -= f.bytes;
    }
    return out;
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

}  // namespace sessionlogsrule
}  // namespace vrc
