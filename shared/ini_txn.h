#pragma once
// A transaction on an .ini file: the keys an editor changed, written only if nobody else changed them
// since the editor read them, to a temp copy that replaces the file in one move, with the original kept
// once beside it. Win32 only (kernel32), no game code - the in-headset settings panel (vr/hset_*) uses it;
// D2R VR Settings could take it for its own writes (it writes slider by slider now).
//
// The idea (and the names of the checks) after bearstonem/d2r-pcvr's vr/settings_menu.cpp (MIT):
// "a conflict with an outside edit reloads instead of overwriting", a backup before the first edit.

#include <cstdint>
#include <string>
#include <vector>

namespace ini {

// A key as text: Present false = not in the file.
struct Text {
    bool present = false;
    std::wstring text;
};
Text Read(const wchar_t* path, const wchar_t* section, const wchar_t* key);

// The file as the OS shows it (last write, size): the same pair twice = nobody wrote in between.
struct Stamp {
    bool exists = false;
    uint64_t written = 0;   // FILETIME as 64 bits
    uint64_t size = 0;
    bool operator==(const Stamp& o) const { return exists == o.exists && written == o.written && size == o.size; }
    bool operator!=(const Stamp& o) const { return !(*this == o); }
};
Stamp StampOf(const wchar_t* path);

// One key of a transaction. `base` is what the editor saw (the fallback's text when the key was missing),
// `value` what it wants written. A key missing in the file now means `fallback`. Texts are compared as
// numbers when both parse (2.5 = 2.50), else as text without case.
struct Change {
    std::wstring section, key;
    std::wstring fallback;       // what a missing key means to the editor
    std::wstring base;           // as the editor saw it (the fallback's text when it was missing)
    std::wstring value;
    bool erase = false;          // the key goes out of the file (value is not used); it reads back as missing
};

enum class Result { Saved, Failed, Conflict };

// Writes `changes` into the file at `path`:
//  - Conflict, nothing written: a key's text in the file is not what `base` says, or the file changed
//    between the check and the replace;
//  - the original is copied once to `path + backupSuffix` (never over an existing copy);
//  - Saved only if every key reads back as written.
// `why` (optional) gets a short English reason for Failed / Conflict, for a log.
Result Commit(const wchar_t* path, const std::vector<Change>& changes, const wchar_t* backupSuffix, std::string* why = nullptr);

// Both texts the same value: numerically (|a-b| < 1e-4) when both parse, else equal without case.
bool SameValue(const std::wstring& a, const std::wstring& b);

}  // namespace ini
