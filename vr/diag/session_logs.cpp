// vr/diag/session_logs.cpp - [support] session_logs: each game's logs kept in a folder of its own, logs\sessions\<its start>, for
// session_logs_days days (Serg 2026-11-03: the next start wrote over a two-hour game's log). A thread of its own copies what the logs
// grew by every 30 s (only the new bytes: a file that got shorter was started again, copied again from its start) and once more at
// the end; a game that dies loses at most its last 30 s, and the next start puts what was written after them (a crash report, a hang
// file) into that game's folder. Copied: the mod's d2rloader\logs, the game's d2rloader\logs and d2rloader\crashes, their .log and
// .txt files written during the game (vr/diag/session_logs_rule.h). Old folders go at the start: older than the days, then the
// oldest while all are over session_logs_max_mb. Reads and writes files only; nothing in play depends on it.
#include "diag.h"
#include "session_logs_rule.h"
#include "../vrcam_internal.h"

#include <map>
#include <string>
#include <vector>

namespace vrc {

namespace sessionlogs {
namespace {
struct Source {
    std::wstring dir;      // with its slash
    std::wstring prefix;   // before the name in the game's folder: two folders have a d2r-vr-vrcam.log
};
std::vector<Source> g_sources;
std::wstring g_root, g_folder, g_name;   // logs\sessions\, logs\sessions\<name>\, <name>
FILETIME g_startedAt{};   // 15 s before the plugin's load: what was written after it is this game's
std::map<std::wstring, uint64_t> g_copied;   // a copy's name: the source bytes it has
HANDLE g_thread = nullptr, g_stop = nullptr;
bool g_made = false;
uint64_t g_bytes = 0;

uint64_t U64(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

// The new part of a file (from `from` on) appended to `to`; false when it could not be read. *size: the source's size now.
bool Append(const std::wstring& from, const std::wstring& to, uint64_t have, uint64_t* size) {
    HANDLE s = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER n{};
    GetFileSizeEx(s, &n);
    *size = (uint64_t)n.QuadPart;
    const bool again = *size < have;   // shorter: written again from its start
    if (again) have = 0;
    bool ok = true;
    if (*size > have) {
        HANDLE d = CreateFileW(to.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, again || have == 0 ? CREATE_ALWAYS : OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (d == INVALID_HANDLE_VALUE) { CloseHandle(s); return false; }
        LARGE_INTEGER at{};
        at.QuadPart = (LONGLONG)have;
        SetFilePointerEx(s, at, nullptr, FILE_BEGIN);
        static char buf[1 << 16];
        for (uint64_t left = *size - have; left > 0 && ok;) {
            DWORD got = 0, put = 0;
            if (!ReadFile(s, buf, (DWORD)(left < sizeof buf ? left : sizeof buf), &got, nullptr) || got == 0) break;
            ok = WriteFile(d, buf, got, &put, nullptr) && put == got;
            left -= got;
            g_bytes += got;
        }
        CloseHandle(d);
    }
    CloseHandle(s);
    return ok;
}

// Each source's .log / .txt written since `since` (and before `until`, 0 = no end) into `folder`.
void Copy(const std::wstring& folder, uint64_t since, uint64_t until, std::map<std::wstring, uint64_t>& copied, bool* made) {
    for (const Source& src : g_sources) {
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((src.dir + L"*").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = fd.cFileName;
            const uint64_t wrote = U64(fd.ftLastWriteTime);
            if (!sessionlogsrule::WantFile(name) || wrote < since || (until && wrote >= until)) continue;
            const std::wstring as = src.prefix + name;
            auto it = copied.find(as);
            const uint64_t have = it == copied.end() ? 0 : it->second;
            const uint64_t now = (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
            if (it != copied.end() && now == have) continue;   // nothing new
            WIN32_FILE_ATTRIBUTE_DATA d{};
            if (!made && GetFileAttributesExW((folder + as).c_str(), GetFileExInfoStandard, &d) &&
                ((uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow) >= now)
                continue;   // the last game's folder: a copy as long already there stays
            if (made && !*made) {   // the game's folder at its first file
                CreateDirectoryW(g_root.c_str(), nullptr);
                *made = CreateDirectoryW(folder.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
            }
            uint64_t size = 0;
            if (Append(src.dir + name, folder + as, have, &size)) copied[as] = size;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
}

uint64_t FolderBytes(const std::wstring& folder, uint64_t* newest) {
    uint64_t sum = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((folder + L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        sum += (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        if (newest && U64(fd.ftLastWriteTime) > *newest) *newest = U64(fd.ftLastWriteTime);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return sum;
}

void RemoveFolder(const std::wstring& folder) {   // ours only (sessionlogsrule::ParseFolder said so): its files, then it
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((folder + L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((folder + fd.cFileName).c_str());
        while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(folder.c_str());
}

// At the start: the last game's folder gets what was written after its last copy (a crash report, a hang file), then the old ones go.
void Tidy(int days, uint64_t maxBytes) {
    std::vector<sessionlogsrule::Folder> all;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((g_root + L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && sessionlogsrule::ParseFolder(fd.cFileName, nullptr))
                all.push_back({fd.cFileName, 0});
        while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    std::wstring last;
    for (const auto& x : all)
        if (x.name > last && x.name != g_name) last = x.name;
    int topped = 0;
    if (!last.empty()) {
        uint64_t newest = 0;
        FolderBytes(g_root + last + L"\\", &newest);
        std::map<std::wstring, uint64_t> none;
        Copy(g_root + last + L"\\", newest + 1, U64(g_startedAt), none, nullptr);
        topped = (int)none.size();
    }
    for (auto& x : all) x.bytes = FolderBytes(g_root + x.name + L"\\", nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);
    const std::vector<std::wstring> gone =
        sessionlogsrule::ToDelete(all, sessionlogsrule::DayBefore(now.wYear, now.wMonth, now.wDay, days), maxBytes, g_name);
    for (const std::wstring& n : gone) RemoveFolder(g_root + n + L"\\");
    LogF("vrcam: session logs ([support] session_logs) - this game's logs go to d2rloader\\logs\\sessions\\%ls as they grow (every 30 s); "
         "kept %d days, at most %llu MB; %d older game folder(s) now, %d removed; the last game's folder got %d file(s) written after it",
         g_name.c_str(), days, (unsigned long long)(maxBytes >> 20), (int)all.size(), (int)gone.size(), topped);
}

DWORD WINAPI Run(void* p) {
    const int days = (int)(intptr_t)p;
    const uint64_t maxMb = GetPrivateProfileIntW(L"support", L"session_logs_max_mb", 2000, g_iniPath);
    Tidy(days, maxMb << 20);
    const uint64_t since = U64(g_startedAt);
    while (WaitForSingleObject(g_stop, 30000) == WAIT_TIMEOUT) Copy(g_folder, since, 0, g_copied, &g_made);
    Copy(g_folder, since, 0, g_copied, &g_made);
    return 0;
}
}  // namespace

void Start() {
    const int days = (int)GetPrivateProfileIntW(L"support", L"session_logs_days", 30, g_iniPath);
    if (GetPrivateProfileIntW(L"support", L"session_logs", 1, g_iniPath) == 0 || days <= 0 || g_thread) return;
    std::wstring ini = g_iniPath;
    ini.resize(ini.find_last_of(L'\\') + 1);   // the plugins folder, its slash kept
    const std::wstring mod = ini + L"..\\logs\\";   // the mod's d2rloader logs folder, as crash.cpp's
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    while (n > 0 && exe[n - 1] != L'\\') --n;
    exe[n] = 0;   // the game's folder (D2R.exe's)
    g_sources = {{mod, L""}, {std::wstring(exe) + L"d2rloader\\logs\\", L"game_"}, {std::wstring(exe) + L"d2rloader\\crashes\\", L"crash_"}};
    SYSTEMTIME st;
    GetLocalTime(&st);
    GetSystemTimeAsFileTime(&g_startedAt);
    const uint64_t back = U64(g_startedAt) - 15ull * 10000000ull;   // D2RLoader's own logs begin a second or two before the plugin
    g_startedAt.dwHighDateTime = (DWORD)(back >> 32);
    g_startedAt.dwLowDateTime = (DWORD)back;
    g_name = sessionlogsrule::FolderName(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_root = mod + L"sessions\\";
    g_folder = g_root + g_name + L"\\";
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, &Run, (void*)(intptr_t)days, 0, nullptr);
    if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 5000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = g_stop = nullptr;
}
}  // namespace sessionlogs

}  // namespace vrc
