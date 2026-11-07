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
enum : unsigned {
    kHooks64 = 1u, kHooks32 = 2u, kRtssExe = 4u, kAfterburnerExe = 8u,
    kOsdAfterburner = 16u, kOsdOther = 32u, kGameInRtss = 64u,   // (RTSS's shared memory, below)
};
// From RTSS's shared memory (2026-11-07, MSI Afterburner's on-screen display named on its own): Afterburner draws its OSD THROUGH RivaTuner - it
// writes its text into an OSD slot of RTSS's shared memory "RTSSSharedMemoryV2" (RTSS SDK RTSSSharedMemory.h), and RTSS's hook paints it on the
// game. A slot owned by Afterburner with text in it = its OSD is on; another owner (HWiNFO, a frame counter...) is another program's OSD; an app
// entry for D2R.exe = RTSS has hooked the game. Only the mapping is read (FILE_MAP_READ): nothing is written into it.
constexpr wchar_t kSharedMemoryName[] = L"RTSSSharedMemoryV2";

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
inline bool Running(unsigned found) { return (found & (kRtssExe | kAfterburnerExe | kOsdAfterburner | kOsdOther)) != 0; }

// RTSS's shared memory, read: the k*Osd* / kGameInRtss bits; `otherOwner` (may be null) gets the first other OSD's owner name. The layout of the
// RTSS SDK (RTSSSharedMemory.h, version 2.0+): the header's DWORDs signature 'RTSS', version, app entry size / array offset / count, OSD entry size
// / array offset / count; an OSD entry starts szOSD[256], szOSDOwner[256], and from version 2.7 szOSDEx[4096]; an app entry dwProcessID then
// szName[260]. Entry 0 of the OSD array is RTSS's own (the SDK's sample starts at 1). Anything out of the mapping's size is not read.
inline unsigned ReadSharedMemory(const void* mem, size_t size, char* otherOwner = nullptr, size_t ownerSize = 0) {
    if (otherOwner && ownerSize) otherOwner[0] = 0;
    if (!mem || size < 36) return 0;
    const unsigned char* p = static_cast<const unsigned char*>(mem);
    auto dw = [&](size_t at) { unsigned v; memcpy(&v, p + at, 4); return v; };
    if (dw(0) != 0x52545353u || dw(4) < 0x00020000u) return 0;   // 'RTSS' (MSVC's multi-character constant), 2.0 or newer
    const unsigned version = dw(4);
    const size_t appSize = dw(8), appOff = dw(12), appCount = dw(16), osdSize = dw(20), osdOff = dw(24), osdCount = dw(28);
    // a C string inside [at, at + max) of the mapping: its length, or -1 if it would run past the mapping or the field
    auto strAt = [&](size_t at, size_t max) -> long {
        if (at >= size) return -1;
        const size_t lim = (size - at) < max ? (size - at) : max;
        for (size_t i = 0; i < lim; ++i)
            if (!p[at + i]) return (long)i;
        return -1;
    };
    auto has = [](const char* s, size_t n, const char* word) {   // `word` in s, any case (ASCII)
        const size_t w = strlen(word);
        for (size_t i = 0; i + w <= n; ++i) {
            size_t k = 0;
            for (; k < w; ++k) {
                char c = s[i + k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c != word[k]) break;
            }
            if (k == w) return true;
        }
        return false;
    };
    unsigned bits = 0;
    if (osdSize >= 512 && osdCount < 4096)
        for (size_t e = 1; e < osdCount; ++e) {
            const size_t at = osdOff + e * osdSize;
            if (at + 512 > size || at < osdOff) break;
            const long ownerLen = strAt(at + 256, 256);
            if (ownerLen <= 0) continue;   // a free slot
            const long textLen = strAt(at, 256);
            const long exLen = version >= 0x00020007u && osdSize >= 512 + 4096 ? strAt(at + 512, 4096) : 0;
            if (textLen <= 0 && exLen <= 0) continue;   // a slot taken but empty: nothing shown
            const char* owner = reinterpret_cast<const char*>(p + at + 256);
            if (has(owner, (size_t)ownerLen, "afterburner")) {
                bits |= kOsdAfterburner;
            } else {
                if (!(bits & kOsdOther) && otherOwner && ownerSize) {
                    const size_t n = (size_t)ownerLen < ownerSize - 1 ? (size_t)ownerLen : ownerSize - 1;
                    memcpy(otherOwner, owner, n);
                    otherOwner[n] = 0;
                }
                bits |= kOsdOther;
            }
        }
    if (appSize >= 4 + 260 && appCount < 4096)
        for (size_t e = 0; e < appCount; ++e) {
            const size_t at = appOff + e * appSize;
            if (at + 4 + 260 > size || at < appOff) break;
            if (!dw(at)) continue;   // no process
            const long n = strAt(at + 4, 260);
            if (n <= 0) continue;
            const char* name = reinterpret_cast<const char*>(p + at + 4);
            const char* base = name;
            for (long i = 0; i < n; ++i)
                if (name[i] == '\\' || name[i] == '/') base = name + i + 1;
            wchar_t w[64];
            size_t i = 0;
            for (; base[i] && i < 63; ++i) w[i] = (wchar_t)(unsigned char)base[i];
            w[i] = 0;
            if (!base[i] && (SameName(w, L"D2R.exe") || SameName(w, L"D2RLoader.exe"))) bits |= kGameInRtss;
        }
    return bits;
}
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
    char what[300] = "";
    auto add = [&](const char* s) {
        const size_t n = strlen(what);
        snprintf(what + n, sizeof what - n, "%s%s", n ? ", " : "", s);
    };
    if (found & kHooks64) add("RTSSHooks64.dll loaded in the game");
    if (found & kHooks32) add("RTSSHooks.dll loaded in the game");
    if (found & kRtssExe) add("RTSS.exe running");
    if (found & kAfterburnerExe) add("MSIAfterburner.exe running");
    if (found & kOsdAfterburner) add("MSI Afterburner's on-screen display on");
    if (found & kOsdOther) add("another program's on-screen display on");
    if (found & kGameInRtss) add("RTSS lists the game as hooked");
    const int n = snprintf(out, size,
                           "%sRivaTuner warning - RivaTuner Statistics Server (MSI Afterburner's on-screen display) found: %s. Its hook on the "
                           "game's picture can freeze the game at its first picture in VR: %s ([support] rtss_warning; nothing was changed)",
                           who ? who : "", what, kAdvice);
    return n < 0 ? 0 : (n >= (int)size ? (int)size - 1 : n);
}

}  // namespace rtss
