#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

#include "ini_txn.h"

namespace ini {
namespace {

// GetPrivateProfileString cannot tell "missing" from "empty" by itself: ask twice with two defaults.
// A key whose text is empty in the file reads back empty both times.
Text ReadRaw(const wchar_t* path, const wchar_t* section, const wchar_t* key) {
    wchar_t a[1024], b[1024];
    GetPrivateProfileStringW(section, key, L"\x01", a, 1024, path);
    GetPrivateProfileStringW(section, key, L"\x02", b, 1024, path);
    Text t;
    t.present = !(a[0] == 1 && a[1] == 0 && b[0] == 2 && b[1] == 0);
    if (t.present) t.text = a;
    return t;
}

bool Number(const std::wstring& s, double* out) {
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    const double v = wcstod(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v)) return false;
    while (*end && iswspace(*end)) ++end;
    if (*end) return false;
    *out = v;
    return true;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

uint64_t Pack(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

}  // namespace

Text Read(const wchar_t* path, const wchar_t* section, const wchar_t* key) { return ReadRaw(path, section, key); }

Stamp StampOf(const wchar_t* path) {
    Stamp s;
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return s;
    s.exists = true;
    s.written = Pack(d.ftLastWriteTime);
    s.size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return s;
}

bool SameValue(const std::wstring& a, const std::wstring& b) {
    double x = 0.0, y = 0.0;
    if (Number(Trim(a), &x) && Number(Trim(b), &y)) return std::fabs(x - y) < 1e-4;
    return _wcsicmp(Trim(a).c_str(), Trim(b).c_str()) == 0;
}

Result Commit(const wchar_t* path, const std::vector<Change>& changes, const wchar_t* backupSuffix, std::string* why) {
    auto say = [&](const char* s) { if (why) *why = s; };
    const Stamp before = StampOf(path);
    if (!before.exists) { say("the ini is not there"); return Result::Failed; }
    for (const Change& c : changes) {
        const Text now = ReadRaw(path, c.section.c_str(), c.key.c_str());
        if (!SameValue(now.present ? now.text : c.fallback, c.base)) { say("a key was changed outside since it was read"); return Result::Conflict; }
    }
    const std::wstring temp = std::wstring(path) + L".hset." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    if (!CopyFileW(path, temp.c_str(), FALSE)) { say("the temp copy could not be made"); return Result::Failed; }
    bool ok = true;
    for (const Change& c : changes)
        if (!WritePrivateProfileStringW(c.section.c_str(), c.key.c_str(), c.value.c_str(), temp.c_str())) { ok = false; break; }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temp.c_str());   // flush the profile cache
    if (!ok) { DeleteFileW(temp.c_str()); say("the temp copy could not be written"); return Result::Failed; }
    if (StampOf(path) != before) { DeleteFileW(temp.c_str()); say("the ini changed while the copy was made"); return Result::Conflict; }
    if (backupSuffix && *backupSuffix) {   // the original, once: never over a copy that is there
        const std::wstring backup = std::wstring(path) + backupSuffix;
        if (!CopyFileW(path, backup.c_str(), TRUE)) {
            const DWORD e = GetLastError();
            if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS) { DeleteFileW(temp.c_str()); say("the backup could not be made"); return Result::Failed; }
        }
    }
    if (!MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        say("the temp copy could not replace the ini");
        return Result::Failed;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path);
    for (const Change& c : changes) {
        const Text now = ReadRaw(path, c.section.c_str(), c.key.c_str());
        if (!now.present || !SameValue(now.text, c.value)) { say("a key did not read back as written"); return Result::Failed; }
    }
    return Result::Saved;
}

}  // namespace ini
