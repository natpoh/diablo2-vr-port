// build_backup.cpp - the file side of D2R VR Settings' build backups (build_backup.h; the rules in shared/build_backup_rule.h).
#include "build_backup.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstdio>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace buildbackup {
namespace {

std::wstring Slash(std::wstring d) {
    for (wchar_t& c : d) if (c == L'/') c = L'\\';
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return d;
}

// A path Windows takes past 260 characters (the made data's paths under a long game folder in a backup folder are long).
std::wstring Long(const std::wstring& p) {
    if (p.size() >= 3 && p[1] == L':' && p[2] == L'\\') return L"\\\\?\\" + p;
    if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\' && (p.size() < 3 || p[2] != L'?')) return L"\\\\?\\UNC\\" + p.substr(2);
    return p;
}

std::wstring Err(DWORD e) {
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, (LPWSTR)&text, 0, nullptr);
    std::wstring s = text ? text : L"";
    if (text) LocalFree(text);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
    return L"error " + std::to_wstring(e) + (s.empty() ? L"" : L": " + s);
}

bool IsFile(const std::wstring& p, uint64_t* size = nullptr) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(Long(p).c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (size) *size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    return true;
}
bool IsDir(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(Long(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Every folder of the path made (a full path with backslashes).
bool MakeDirs(const std::wstring& dir) {
    const std::wstring d = Slash(dir);
    size_t from = d.size() >= 2 && d[0] == L'\\' && d[1] == L'\\' ? d.find(L'\\', d.find(L'\\', 2) + 1) + 1 : 3;
    for (size_t at = d.find(L'\\', from); at != std::wstring::npos; at = d.find(L'\\', at + 1)) {
        const std::wstring part = d.substr(0, at);
        if (IsDir(part)) continue;
        if (!CreateDirectoryW(Long(part).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return true;
}

std::wstring Parent(const std::wstring& p) {
    const size_t s = p.find_last_of(L'\\');
    return s == std::wstring::npos ? std::wstring() : p.substr(0, s + 1);
}

// The files of `dir` (and its subfolders when recurse) whose names match, as paths under `dir`.
void Walk(const std::wstring& base, const std::wstring& sub, const wchar_t* patterns, bool recurse, std::vector<std::pair<std::wstring, uint64_t>>* out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Long(base + sub + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> dirs;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recurse && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) dirs.push_back(n);
        } else if (MatchAny(patterns, n)) {
            out->push_back({sub + n, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const std::wstring& d : dirs) Walk(base, sub + d + L"\\", patterns, recurse, out);
}

std::wstring RootOf(const Places& p, int root) { return root == kBodyWalk ? p.bodywalk : p.game; }

// "game\mods\..." -> the real path ("" when its root is not there, BodyWalk's left out)
std::wstring RealPath(const Places& p, const std::wstring& rel) {
    if (_wcsnicmp(rel.c_str(), kGameTop, wcslen(kGameTop)) == 0) return p.game + rel.substr(wcslen(kGameTop));
    if (_wcsnicmp(rel.c_str(), kBodyWalkTop, wcslen(kBodyWalkTop)) == 0) return p.bodywalk.empty() ? std::wstring() : p.bodywalk + rel.substr(wcslen(kBodyWalkTop));
    return std::wstring();
}

bool ReadText(const std::wstring& path, std::wstring* out) {
    HANDLE f = CreateFileW(Long(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::string bytes((size_t)std::min<LONGLONG>(sz.QuadPart, 64ll << 20), '\0');
    DWORD got = 0;
    const BOOL ok = bytes.empty() || ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    bytes.resize(got);
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) bytes.erase(0, 3);
    const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    out->assign(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out->data(), n);
    return true;
}

// UTF-8, through a .tmp moved over it (a reader never sees half of it).
bool WriteText(const std::wstring& path, const std::wstring& text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), bytes.data(), n, nullptr, nullptr);
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(Long(tmp).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const BOOL ok = bytes.empty() || WriteFile(f, bytes.data(), (DWORD)bytes.size(), &put, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || put != bytes.size()) { DeleteFileW(Long(tmp).c_str()); return false; }
    return MoveFileExW(Long(tmp).c_str(), Long(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool RemoveTree(const std::wstring& dir) {
    const std::wstring d = Slash(dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Long(d + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    bool ok = true;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                ok = RemoveTree(d + n) && ok;
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(Long(d + n).c_str(), FILE_ATTRIBUTE_NORMAL);
                ok = ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(Long(d + n).c_str()) : DeleteFileW(Long(d + n).c_str())) && ok;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(Long(d).c_str()) && ok;
}

const wchar_t kHowToText[] =
    L"D2R VR build backup\r\n"
    L"\r\n"
    L"To put this back, use D2R VR Settings > Home > Backups: click its row, then Restore (with the game and BodyWalk closed).\r\n"
    L"\r\n"
    L"By hand, if D2R VR Settings does not open: close the game and BodyWalk, then copy everything inside the folder \"game\" here into\r\n"
    L"your Diablo II Resurrected folder (the one with D2R.exe) and let it replace the files; and everything inside \"bodywalk\" (if it is\r\n"
    L"here) into %LOCALAPPDATA%\\BodyWalkVR. backup.txt lists every file of the build.\r\n";

}  // namespace

Places PlacesFor(const std::wstring& gameDir) {
    Places p;
    wchar_t full[4096];
    const DWORD n = GetFullPathNameW(gameDir.c_str(), 4096, full, nullptr);
    p.game = Slash(n && n < 4096 ? std::wstring(full, n) : gameDir);
    PWSTR la = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la)) && la) p.bodywalk = Slash(std::wstring(la) + L"\\BodyWalkVR");
    if (la) CoTaskMemFree(la);
    return p;
}

std::wstring BackupsDir(const std::wstring& gameDir) { return Slash(gameDir) + L"mods\\D2R-VR\\" + kFolder; }

bool BuildInstalled(const std::wstring& gameDir) { return IsFile(Slash(gameDir) + L"mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll"); }

std::wstring InstalledVersion(const std::wstring& gameDir) {
    for (const wchar_t* f : {L"mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll", L"D2R_VR_Settings.exe"}) {
        const std::wstring path = Slash(gameDir) + f;
        DWORD dummy = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
        if (!size) continue;
        std::vector<unsigned char> block(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) continue;
        VS_FIXEDFILEINFO* fi = nullptr;
        UINT len = 0;
        if (!VerQueryValueW(block.data(), L"\\", (void**)&fi, &len) || !fi || len < sizeof(VS_FIXEDFILEINFO)) continue;
        const unsigned a = HIWORD(fi->dwProductVersionMS), b = LOWORD(fi->dwProductVersionMS), c = HIWORD(fi->dwProductVersionLS),
                       d = LOWORD(fi->dwProductVersionLS);
        if (!a && !b && !c && !d) continue;
        wchar_t v[48];
        if (d) swprintf(v, 48, L"%u.%u.%u.%u", a, b, c, d); else swprintf(v, 48, L"%u.%u.%u", a, b, c);
        return v;
    }
    return L"unknown";
}

Stamp Now() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    Stamp s;
    s.y = t.wYear; s.mo = t.wMonth; s.d = t.wDay; s.h = t.wHour; s.mi = t.wMinute; s.s = t.wSecond;
    return s;
}

std::vector<Entry> Scan(const Places& p, int scope) {
    std::vector<Entry> out;
    for (const Area& a : Areas()) {
        if (scope == kSettings && !a.settings) continue;
        const std::wstring base = RootOf(p, a.root);
        if (base.empty()) continue;
        std::vector<std::pair<std::wstring, uint64_t>> found;
        Walk(base + a.dir, L"", a.patterns, a.recurse, &found);
        for (const auto& f : found) {
            Entry e;
            e.rel = Top(a.root) + a.dir + f.first;
            e.size = f.second;
            // (an earlier area that takes the same file has it already: the .mpq's modinfo.json is the .mpq's, not the receipts')
            bool dup = false;
            for (const Entry& o : out) if (_wcsicmp(o.rel.c_str(), e.rel.c_str()) == 0) { dup = true; break; }
            if (!dup && AreaOf(e.rel) >= 0) out.push_back(e);
        }
    }
    return out;
}

bool ReadBackup(const std::wstring& gameDir, const std::wstring& folder, Info* info, std::vector<Entry>* entries) {
    std::wstring text;
    if (!ReadText(BackupsDir(gameDir) + folder + L"\\" + kManifest, &text)) return false;
    info->folder = folder;
    return ParseManifest(text, info, entries);
}

std::vector<Info> List(const std::wstring& gameDir) {
    std::vector<Info> out;
    const std::wstring dir = BackupsDir(gameDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Long(dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::wstring n = fd.cFileName;
        if (!IsBackupName(n)) continue;
        Info i;
        i.folder = n;
        // from the name, in case backup.txt is not there (a backup that did not finish)
        const size_t us = n.find(L'_');
        i.version = n.substr(0, us);
        std::wstring d = n.substr(us + 1, 19);
        d[10] = L' '; d[13] = L':'; d[16] = L':';
        i.made = d;
        i.kind = n.find(L"_update") != std::wstring::npos ? kUpdate : n.find(L"_before_restore") != std::wstring::npos ? kRestore : kManual;
        i.scope = n.find(L"_settings") != std::wstring::npos ? kSettings : kBuild;
        Info m = i;
        if (ReadBackup(gameDir, n, &m, nullptr)) i = m;
        out.push_back(i);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    SortNewestFirst(&out);
    return out;
}

bool Remove(const std::wstring& gameDir, const std::wstring& folder) {
    if (!IsBackupName(folder) || folder.find_first_of(L"\\/") != std::wstring::npos) return false;
    const std::wstring dir = BackupsDir(gameDir) + folder;
    return !IsDir(dir) || RemoveTree(dir);
}

bool FinishPending(unsigned long pid, const std::wstring& pending, const std::wstring& target, unsigned long timeoutMs) {
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(h, timeoutMs);
        CloseHandle(h);
    }   // (not there any more: it has exited)
    for (int i = 0; i < 40; ++i) {
        if (MoveFileExW(Long(pending).c_str(), Long(target).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        Sleep(250);
    }
    return false;
}

void CleanReplaced(const std::wstring& gameDir) {
    const std::wstring dir = BackupsDir(gameDir) + kReplaced;
    if (!IsDir(dir)) return;
    std::vector<std::pair<std::wstring, uint64_t>> files;
    Walk(dir, L"", L"*", false, &files);
    for (const auto& f : files) DeleteFileW(Long(dir + f.first).c_str());   // (the one still running stays until the next time)
    RemoveDirectoryW(Long(dir).c_str());
}

Outcome Make(const Places& p, int kind, const std::wstring& version, int keepAuto, const Stamp& now, const Progress& progress,
             const std::vector<std::wstring>& keep, int scope) {
    Outcome out;
    if (!BuildInstalled(p.game)) { out.code = kNothing; out.error = L"no D2R VR build here (no mods\\D2R-VR\\d2rloader\\plugins\\d2rl-vrcam.dll)"; return out; }
    const std::vector<Entry> entries = Scan(p, scope);
    if (entries.empty()) { out.code = kNothing; out.error = L"no settings files here (mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr*.ini)"; return out; }
    for (const Entry& e : entries) out.bytes += e.size;
    out.files = entries.size();
    const std::wstring dir = BackupsDir(p.game);
    if (!MakeDirs(dir)) { out.error = L"could not make " + dir + L" (" + Err(GetLastError()) + L")"; return out; }
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(dir.c_str(), &freeBytes, nullptr, nullptr) && freeBytes.QuadPart < out.bytes + (64ull << 20)) {
        out.code = kNoSpace;
        out.error = L"not enough free space on the game's drive: " + SizeText(out.bytes) + L" needed, " + SizeText(freeBytes.QuadPart) + L" free";
        return out;
    }
    Info info;
    info.version = SafeVersion(version);
    info.made = StampText(now);
    info.kind = kind;
    info.scope = scope;
    info.folder = FolderName(version, now, kind, scope);
    out.folder = info.folder;
    const std::wstring to = dir + info.folder + L"\\";
    if (IsDir(to)) { out.error = L"a backup of this very second is there already: " + info.folder; return out; }
    uint64_t done = 0;
    auto fail = [&](const std::wstring& what) {
        out.error = what;
        RemoveTree(to);
        return out;
    };
    for (const Entry& e : entries) {
        const std::wstring src = RealPath(p, e.rel), dst = to + e.rel;
        if (!MakeDirs(Parent(dst))) return fail(L"could not make the folder for " + e.rel + L" (" + Err(GetLastError()) + L")");
        if (!CopyFileW(Long(src).c_str(), Long(dst).c_str(), TRUE)) return fail(L"could not copy " + src + L" (" + Err(GetLastError()) + L")");
        done += e.size;
        if (progress) progress(done, out.bytes);
    }
    // the sizes as copied (a file the game or Settings wrote meanwhile: the copy's own)
    std::vector<Entry> copied = entries;
    for (Entry& e : copied) IsFile(to + e.rel, &e.size);
    if (!WriteText(to + kHowTo, kHowToText)) return fail(L"could not write " + std::wstring(kHowTo) + L" (" + Err(GetLastError()) + L")");
    if (!WriteText(to + kManifest, Manifest(info, copied))) return fail(L"could not write " + std::wstring(kManifest) + L" (" + Err(GetLastError()) + L")");
    out.bytes = 0;
    for (const Entry& e : copied) out.bytes += e.size;
    out.code = kDone;
    std::vector<std::wstring> protect = keep;
    protect.push_back(info.folder);
    for (const std::wstring& old : Prune(List(p.game), keepAuto, protect)) Remove(p.game, old);
    return out;
}

Outcome Restore(const Places& p, const std::wstring& folder, int keepAuto, const Stamp& now, const Progress& progress) {
    Outcome out;
    Info info;
    std::vector<Entry> entries;
    if (!IsBackupName(folder) || !ReadBackup(p.game, folder, &info, &entries)) {
        out.code = kBadBackup;
        out.error = L"no finished backup " + folder + L" (its backup.txt is missing)";
        return out;
    }
    const std::wstring from = BackupsDir(p.game) + folder + L"\\";
    for (const Entry& e : entries) {
        uint64_t size = 0;
        if (!IsFile(from + e.rel, &size) || size != e.size) {
            out.code = kBadBackup;
            out.error = L"the backup " + folder + L" is not whole: " + e.rel + (IsFile(from + e.rel) ? L" has another size" : L" is missing");
            return out;
        }
    }
    // what is there now, first (never removed by this backup's pruning: the one being put back is kept too)
    if (BuildInstalled(p.game)) {
        const Outcome before = Make(p, kRestore, InstalledVersion(p.game), keepAuto, now, nullptr, {folder}, info.scope);
        if (before.code != kDone) {
            out.code = before.code == kNoSpace ? kNoSpace : kFailed;
            out.error = L"the backup of the build there now failed, nothing was restored: " + before.error;
            return out;
        }
        out.folder = before.folder;
    }
    const std::vector<Entry> order = RestoreOrder(entries);
    uint64_t total = 0, done = 0;
    for (const Entry& e : order) total += e.size;
    const std::wstring aside = BackupsDir(p.game) + kReplaced;
    for (const Entry& e : order) {
        const std::wstring dst = RealPath(p, e.rel);
        if (dst.empty()) continue;   // (BodyWalk's files with no BodyWalk folder here)
        if (!MakeDirs(Parent(dst))) { out.error = L"could not make the folder for " + dst + L" (" + Err(GetLastError()) + L")"; return out; }
        const std::wstring tmp = dst + L".restore_tmp";
        if (!CopyFileW(Long(from + e.rel).c_str(), Long(tmp).c_str(), FALSE)) {
            out.error = L"could not copy " + e.rel + L" (" + Err(GetLastError()) + L")";
            return out;
        }
        if (!MoveFileExW(Long(tmp).c_str(), Long(dst).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            // a program in use (this settings program, running) cannot be written over, but it can be moved aside
            const DWORD e1 = GetLastError();
            const size_t slash = dst.find_last_of(L'\\');
            const std::wstring moved = aside + dst.substr(slash + 1) + L"." + std::to_wstring(GetTickCount64()) + L".old";
            const bool isSettings = _wcsicmp(dst.substr(slash + 1).c_str(), kSettingsExe) == 0;
            const bool movedAside = MakeDirs(aside) && MoveFileExW(Long(dst).c_str(), Long(moved).c_str(), 0);
            if (!movedAside && isSettings) {
                // not even a rename (FAT / exFAT, or held without delete sharing): the restored program waits beside it until this one has exited
                const std::wstring pending = dst + kPending;
                if (MoveFileExW(Long(tmp).c_str(), Long(pending).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    out.settingsPending = pending;
                    done += e.size;
                    if (progress) progress(done, total);
                    continue;
                }
            }
            if (!movedAside || !MoveFileExW(Long(tmp).c_str(), Long(dst).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(Long(tmp).c_str());
                out.error = L"could not put back " + dst + L" (" + Err(e1) + L")";
                return out;
            }
            if (isSettings) out.settingsReplaced = true;
        }
        done += e.size;
        if (progress) progress(done, total);
    }
    // the made data's files the backup did not have (a newer build's), and their receipts - a whole build's restore only (a settings
    // backup has none of them: they are not its to remove)
    if (info.scope == kBuild)
        for (const std::wstring& extra : MirrorExtras(entries, Scan(p))) {
            const std::wstring path = RealPath(p, extra);
            if (!path.empty()) DeleteFileW(Long(path).c_str());
        }
    out.files = entries.size();
    out.bytes = total;
    out.code = kDone;
    return out;
}

}  // namespace buildbackup
