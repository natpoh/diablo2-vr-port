// vr/diag/save_backups.cpp - [support] save_backups: the mod's own backups of the game's saves (Serg 2026-11-03: the 8th power cut
// that left Bar.d2s and the shared stash all zeros; D2RLoader's own backups skip our mod since its savepath is "../", the newest good
// copy was two weeks old). A thread of its own copies the save folder's characters, shared stashes and their .ctl / .key / .map / .ma*
// / .d2rl files into Saved Games\Diablo II Resurrected\D2R VR Backups\<date_time>\ - at the start (before the game reads them) and
// whenever one has changed and is still (looked at every 15 s), and once more at the end. Only a good file is copied
// (save_backups_rule.h: never all zeros, a character's and a stash's own header and lengths right): a zeroed one is named in the log
// with the folder of its last good copy. A copy is written to <name>.tmp, flushed to the disk, then renamed - a power cut never leaves a
// zeroed copy of ours. Old folders go at the start: older than the days, then the oldest while all are over save_backups_max_mb; the
// folder with the newest copy of each file never. The save folder itself is only read (no file of the game's is ever written, moved or
// deleted); nothing in play depends on it.
#include "save_backups.h"
#include "save_backups_rule.h"
#include "../vrcam_internal.h"

#include <shlobj.h>

#include <cstdio>
#include <string>
#include <vector>

namespace vrc {

namespace savebackups {
namespace {
namespace rule = savebackupsrule;

std::wstring g_saves, g_root;   // Saved Games\Diablo II Resurrected\, its D2R VR Backups\ (each with its slash)
std::wstring g_first;   // this game's first backup: never removed at this start
rule::States g_inBackup;   // the newest backup's files (their copies keep the saves' last write)
rule::States g_last, g_prev;   // the saves as the last backup saw them, and as the poll before this one
std::map<std::wstring, std::wstring> g_newestOf;   // a file's key: the folder of its newest good copy
HANDLE g_thread = nullptr, g_stop = nullptr;

constexpr uint64_t kSecond = 10000000ull;   // FILETIME ticks
constexpr uint64_t kMaxFile = 16ull << 20;   // a save is some KB; anything this big is not one

uint64_t U64(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }
uint64_t Now() { FILETIME f; GetSystemTimeAsFileTime(&f); return U64(f); }

std::vector<rule::FileState> List(const std::wstring& dir, uint64_t* bytes) {
    std::vector<rule::FileState> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const uint64_t size = (uint64_t)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        if (bytes) *bytes += size;
        if (rule::WantFile(fd.cFileName)) out.push_back({fd.cFileName, size, U64(fd.ftLastWriteTime)});
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

// The whole file (it is shared for the game's writes and deletes all along: the handle lives for one read).
bool ReadAll(const std::wstring& path, std::vector<uint8_t>& data) {
    HANDLE s = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER n{};
    bool ok = GetFileSizeEx(s, &n) && (uint64_t)n.QuadPart <= kMaxFile;
    if (ok) {
        data.resize((size_t)n.QuadPart);
        DWORD got = 0;
        ok = data.empty() || (ReadFile(s, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size());
    }
    CloseHandle(s);
    return ok;
}

// A copy that is on the disk when this returns: <name>.tmp written and flushed, its last write the save's, then renamed over the name.
bool WriteDurable(const std::wstring& path, const std::vector<uint8_t>& data, uint64_t wrote) {
    const std::wstring tmp = path + L".tmp";
    HANDLE d = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (d == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(d, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size() && FlushFileBuffers(d);
    FILETIME ft{(DWORD)wrote, (DWORD)(wrote >> 32)};
    SetFileTime(d, nullptr, nullptr, &ft);
    CloseHandle(d);
    if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());
    return ok;
}

std::wstring NewName() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return rule::FolderName(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

// Every good save into a new folder, when one of them is not in the newest backup as it is now. The saves as they are seen become
// g_last (a zeroed one is not read again until it changes). Returns the folder's name, "" when none was made.
std::wstring Backup(const std::vector<rule::FileState>& now, const char* why) {
    g_last = rule::ByKey(now);
    struct Good { rule::FileState f; std::vector<uint8_t> data; };
    std::vector<Good> good;
    std::string bad;
    bool fresh = false;
    for (const rule::FileState& f : now) {
        Good g{f, {}};
        if (!ReadAll(g_saves + f.name, g.data) || !rule::Valid(f.name, g.data.data(), g.data.size())) {
            auto at = g_newestOf.find(rule::Key(f.name));
            char line[400];
            snprintf(line, sizeof line, "%s%ls (%llu bytes%s; its last good copy: %ls)", bad.empty() ? "" : ", ", f.name.c_str(),
                     (unsigned long long)f.size, g.data.empty() && f.size ? ", not read" : "",
                     at == g_newestOf.end() ? L"none" : at->second.c_str());
            bad += line;
            continue;
        }
        g.f.size = g.data.size();
        auto it = g_inBackup.find(rule::Key(f.name));
        if (it == g_inBackup.end() || it->second.size != g.f.size || it->second.wrote != f.wrote) fresh = true;
        good.push_back(std::move(g));
    }
    if (!bad.empty())
        LogF("vrcam: save backups - NOT a good save, not backed up (all zeros or cut short: a power cut while the game wrote it?): %s",
             bad.c_str());
    if (!fresh) return L"";
    const std::wstring name = NewName();
    const std::wstring dir = g_root + name + L"\\";
    CreateDirectoryW(g_root.c_str(), nullptr);
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LogF("vrcam: save backups - could not make %ls (error %lu)", dir.c_str(), GetLastError());
        return L"";
    }
    int copied = 0;
    uint64_t bytes = 0;
    rule::States in;
    for (const Good& g : good) {
        if (!WriteDurable(dir + g.f.name, g.data, g.f.wrote)) {
            LogF("vrcam: save backups - could not write %ls%ls (error %lu)", dir.c_str(), g.f.name.c_str(), GetLastError());
            continue;
        }
        in[rule::Key(g.f.name)] = g.f;
        g_newestOf[rule::Key(g.f.name)] = name;
        ++copied;
        bytes += g.data.size();
    }
    g_inBackup = in;
    LogF("vrcam: save backups (%s) - %d save file(s), %llu KB, into Saved Games\\Diablo II Resurrected\\D2R VR Backups\\%ls", why, copied,
         (unsigned long long)((bytes + 1023) >> 10), name.c_str());
    return name;
}

void RemoveFolder(const std::wstring& folder) {   // ours only (rule::ParseFolder said so): its files, then it
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

// Our folders with their saves; a <name>.tmp a power cut left half written goes. The newest one's files become g_inBackup.
std::vector<rule::Folder> Folders() {
    std::vector<rule::Folder> all;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((g_root + L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return all;
    do
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && rule::ParseFolder(fd.cFileName, nullptr)) all.push_back({fd.cFileName, 0, {}});
    while (FindNextFileW(f, &fd));
    FindClose(f);
    std::wstring newest;
    std::vector<rule::FileState> newestFiles;
    for (rule::Folder& x : all) {
        const std::wstring dir = g_root + x.name + L"\\";
        HANDLE t = FindFirstFileW((dir + L"*.tmp").c_str(), &fd);
        if (t != INVALID_HANDLE_VALUE) {
            do DeleteFileW((dir + fd.cFileName).c_str());
            while (FindNextFileW(t, &fd));
            FindClose(t);
        }
        const std::vector<rule::FileState> files = List(dir, &x.bytes);
        for (const rule::FileState& s : files) x.files.push_back(s.name);
        if (x.name > newest) { newest = x.name; newestFiles = files; }
    }
    g_inBackup = rule::ByKey(newestFiles);
    g_newestOf = rule::Newest(all);
    return all;
}

void Tidy(std::vector<rule::Folder> all, int days, uint64_t maxBytes) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    const std::vector<std::wstring> gone =
        rule::ToDelete(all, rule::DayBefore(now.wYear, now.wMonth, now.wDay, days), maxBytes, g_first);
    for (const std::wstring& n : gone) RemoveFolder(g_root + n + L"\\");
    uint64_t bytes = 0;
    for (const rule::Folder& x : all) bytes += x.bytes;
    LogF("vrcam: save backups ([support] save_backups) - the saves of %ls are copied at the start and when one changes; kept %d days, at "
         "most %llu MB, the newest copy of each file always; %d backup folder(s) (%llu KB), %d old one(s) removed",
         g_saves.c_str(), days, (unsigned long long)(maxBytes >> 20), (int)all.size(), (unsigned long long)(bytes >> 10), (int)gone.size());
}

DWORD WINAPI Run(void* p) {
    const int days = (int)(intptr_t)p;
    const uint64_t maxMb = GetPrivateProfileIntW(L"support", L"save_backups_max_mb", 500, g_iniPath);
    std::vector<rule::Folder> all = Folders();
    std::vector<rule::FileState> now = List(g_saves, nullptr);
    g_first = Backup(now, "the start");   // the game has not opened its saves yet
    if (!g_first.empty()) {
        rule::Folder made{g_first, 0, {}};
        for (const auto& kv : g_inBackup) { made.bytes += kv.second.size; made.files.push_back(kv.second.name); }
        all.push_back(made);
    }
    Tidy(all, days, maxMb << 20);
    g_prev = rule::ByKey(now);
    while (WaitForSingleObject(g_stop, 15000) == WAIT_TIMEOUT) {
        now = List(g_saves, nullptr);
        if (rule::Changed(now, g_last, g_prev, Now(), 5 * kSecond) == rule::Decide::Go) Backup(now, "a save changed");
        g_prev = rule::ByKey(now);
    }
    now = List(g_saves, nullptr);   // the end: the game has written what it writes at its exit
    if (rule::Changed(now, g_last, g_prev, Now(), 0) != rule::Decide::Same) Backup(now, "the end");
    return 0;
}
}  // namespace

void Start() {
    const int days = (int)GetPrivateProfileIntW(L"support", L"save_backups_days", 30, g_iniPath);
    if (GetPrivateProfileIntW(L"support", L"save_backups", 1, g_iniPath) == 0 || days <= 0 || g_thread) return;
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) {
        LogF("vrcam: save backups - no Saved Games folder, none made");
        return;
    }
    g_saves = std::wstring(dir) + L"\\Diablo II Resurrected\\";   // the mod's modinfo.json savepath "../": the game's own save folder
    CoTaskMemFree(dir);
    g_root = g_saves + L"D2R VR Backups\\";
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
}  // namespace savebackups

}  // namespace vrc
