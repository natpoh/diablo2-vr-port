// shared/rtss_detect.h - [support] rtss_warning: RivaTuner Statistics Server (RTSS, MSI Afterburner's on-screen display) found with the game.
// 2026-11-04, a player's crash report (0.201.0, RX 9070 XT, SteamVR): the game froze at its very first picture with the main thread inside
// RTSSHooks64.dll (RTSS's D3D hook on the game's present), every thread of ours idle - and his start before froze the same way. RTSS hooking
// the game's D3D12 present fights our VR rendering. The plugin (vr/diag/rtss_watch.cpp) looks at the start and every 10 s of the first minute
// and writes ONE log line (the hang / crash report files carry it too); D2R VR Settings shows a Warning row on Home while RTSS runs.
// Only reads: nothing is unhooked, closed or changed. Pure, no Windows calls (tools/rtss_detect_test.cpp).
#pragma once

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace rtss {

// What RTSS leaves behind: its hook DLLs in the game (64-bit; RTSSHooks.dll is the 32-bit one, named in case), its programs running.
constexpr const wchar_t* kHookDlls[] = {L"RTSSHooks64.dll", L"RTSSHooks.dll"};
constexpr const wchar_t* kPrograms[] = {L"RTSS.exe", L"MSIAfterburner.exe"};
enum : unsigned { kHooks64 = 1u, kHooks32 = 2u, kRtssExe = 4u, kAfterburnerExe = 8u };

// The advice, one sentence: the log line's, the reports' and (in its own words) Settings' Warning row.
constexpr const char* kAdvice = "close RivaTuner / MSI Afterburner, or in RivaTuner set Application detection level None (or turn its "
                                "On-Screen Display off) for D2R.exe and D2RLoader.exe";

inline bool SameName(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b) {
        wchar_t x = *a, y = *b;
        if (x >= L'A' && x <= L'Z') x = (wchar_t)(x - L'A' + L'a');
        if (y >= L'A' && y <= L'Z') y = (wchar_t)(y - L'A' + L'a');
        if (x != y) return false;
    }
    return *a == *b;
}
// A module's base name (or a full path's last part) -> its bit, 0 = not RTSS's.
inline unsigned HookBit(const wchar_t* module) {
    if (!module) return 0;
    const wchar_t* base = module;
    for (const wchar_t* p = module; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    if (SameName(base, kHookDlls[0])) return kHooks64;
    if (SameName(base, kHookDlls[1])) return kHooks32;
    return 0;
}
// The same for a narrow module name (the crash report's module list is char).
inline unsigned HookBitA(const char* module) {
    if (!module) return 0;
    wchar_t w[64];
    size_t i = 0;
    for (; module[i] && i < 63; ++i) w[i] = (wchar_t)(unsigned char)module[i];
    w[i] = 0;
    return module[i] ? 0 : HookBit(w);
}
// A process's exe name -> its bit, 0 = not RTSS's.
inline unsigned ProgramBit(const wchar_t* exe) {
    if (SameName(exe, kPrograms[0])) return kRtssExe;
    if (SameName(exe, kPrograms[1])) return kAfterburnerExe;
    return 0;
}
inline bool Running(unsigned found) { return (found & (kRtssExe | kAfterburnerExe)) != 0; }
inline bool Hooked(unsigned found) { return (found & (kHooks64 | kHooks32)) != 0; }

// The plugin's look-ups: at the start (0 s) and every 10 s up to a minute - 7 looks.
constexpr int kLooks = 7;
constexpr unsigned kEveryMs = 10000;
// Whether a look found something not told yet (a line is written then: at most once per new thing, normally once in all).
inline bool WorthALine(unsigned told, unsigned now) { return (now & ~told) != 0; }

// The line, into a fixed buffer (no heap: the crash report writes it too). `who`: "vrcam: " for the log, "" for the reports.
// Returns the length written (0: nothing found).
inline int Format(unsigned found, const char* who, char* out, size_t size) {
    if (!out || size == 0) return 0;
    out[0] = 0;
    if (!found) return 0;
    char what[160] = "";
    auto add = [&](const char* s) {
        const size_t n = strlen(what);
        snprintf(what + n, sizeof what - n, "%s%s", n ? ", " : "", s);
    };
    if (found & kHooks64) add("RTSSHooks64.dll loaded in the game");
    if (found & kHooks32) add("RTSSHooks.dll loaded in the game");
    if (found & kRtssExe) add("RTSS.exe running");
    if (found & kAfterburnerExe) add("MSIAfterburner.exe running");
    const int n = snprintf(out, size,
                           "%sRivaTuner warning - RivaTuner Statistics Server (MSI Afterburner's on-screen display) found: %s. Its hook on the "
                           "game's picture can freeze the game at its first picture in VR: %s ([support] rtss_warning; nothing was changed)",
                           who ? who : "", what, kAdvice);
    return n < 0 ? 0 : (n >= (int)size ? (int)size - 1 : n);
}

}  // namespace rtss
