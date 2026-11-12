// replay_hang_fallback.h - [stereo] replay_hang_fallback: after a GPU hang in Native one pass, the next start draws two passes.
//
// Crash batch 2026-11-12: six players' games ended in DEVICE_HUNG inside the replayed right eye, several of them more than once in a row - each
// start went straight back into one pass. With the key on, the hang (the device lost, or the direct queue standing still for
// replay_hang_fallback_stall_ms with the presents stopped) while one pass ran in the last 15 s leaves a note beside the logs; the next start reads
// it, deletes it, says why in the log and draws two passes that session. The start after that tries one pass again (a single hang is not a verdict).
// The note's text is one line: what happened. vr/render/stereo/replay.cpp writes and reads it; tools/native_replay_test.cpp proves the round trip.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace hangfallback {

constexpr const wchar_t* kNoteName = L"d2r_vr_one_pass_hung.txt";

// <logDir><kNoteName>; false without a log folder (logDir ends in a backslash, as gpufault::LogDir() gives it).
inline bool NotePath(const wchar_t* logDir, wchar_t* out, size_t n) {
    if (!logDir || !logDir[0] || !out || !n) return false;
    return swprintf(out, n, L"%s%s", logDir, kNoteName) > 0;
}

// The note, written and flushed at once (the game ends ~40 ms after a device loss). Any thread.
inline bool WriteNote(const wchar_t* path, const char* why) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[400];
    const int len = snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, why ? why : "hung");
    DWORD w = 0;
    const bool ok = len > 0 && WriteFile(f, b, (DWORD)strnlen(b, sizeof b), &w, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}

// The note read (its line, without the line end, into why) and deleted. False: there was none.
inline bool TakeNote(const wchar_t* path, char* why, size_t n) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char b[400] = {};
    DWORD r = 0;
    ReadFile(f, b, sizeof b - 1, &r, nullptr);
    CloseHandle(f);
    DeleteFileW(path);
    b[r < sizeof b ? r : sizeof b - 1] = 0;
    for (char* c = b; *c; ++c)
        if (*c == '\r' || *c == '\n') { *c = 0; break; }
    if (why && n) {
        // "YYYY-MM-DD hh:mm:ss <what>" -> "at hh:mm:ss: <what>" reads in the start's log line ("the last session ...")
        const bool dated = strlen(b) > 20 && b[4] == '-' && b[10] == ' ';
        if (dated) snprintf(why, n, "hung at %.8s on %.10s: %s", b + 11, b, b + 20);
        else snprintf(why, n, "hung: %s", b[0] ? b : "(no reason written)");
    }
    return true;
}

}  // namespace hangfallback
