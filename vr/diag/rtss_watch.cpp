// vr/diag/rtss_watch.cpp - [support] rtss_warning (shared/rtss_detect.h): RivaTuner Statistics Server found with the game, said once in the log.
// A thread of its own looks at the start and every 10 s of the first minute: RTSSHooks64.dll / RTSSHooks.dll loaded in this process
// (GetModuleHandle), RTSS.exe / MSIAfterburner.exe running (a process snapshot), and in RTSS's shared memory MSI Afterburner's on-screen display
// (or another program's) on and the game listed as hooked (read only, 2026-11-07). Something new found: one log line with the advice; the
// hang and crash report files (vr/diag/crash.cpp) carry the same line, so the crash reporter's report text shows it. Only reads: nothing
// is unhooked, closed or changed. rtss_warning=0: no thread, no line, as before.
#include "diag.h"
#include "../vrcam_internal.h"
#include "../../shared/rtss_detect.h"

#include <tlhelp32.h>

#include <atomic>

namespace vrc {
namespace rtsswatch {
namespace {
std::atomic<bool> g_on{false};
std::atomic<unsigned> g_found{0};   // what the looks found so far (rtss::k* bits)
HANDLE g_stop = nullptr, g_thread = nullptr;

unsigned Look() {
    unsigned found = 0;
    for (const wchar_t* dll : rtss::kHookDlls)
        if (GetModuleHandleW(dll)) found |= rtss::HookBit(dll);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{sizeof pe};
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) found |= rtss::ProgramBit(pe.szExeFile);
        CloseHandle(snap);
    }
    // RTSS's shared memory, read only: MSI Afterburner's on-screen display on, another program's, the game hooked (rtss::ReadSharedMemory)
    if (HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, rtss::kSharedMemoryName)) {
        if (const void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0)) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(view, &mbi, sizeof mbi)) found |= rtss::ReadSharedMemory(view, mbi.RegionSize);
            UnmapViewOfFile(view);
        }
        CloseHandle(map);
    }
    return found;
}

DWORD WINAPI Run(void*) {
    unsigned told = 0;
    for (int i = 0; i < rtss::kLooks; ++i) {
        if (i && WaitForSingleObject(g_stop, rtss::kEveryMs) != WAIT_TIMEOUT) break;
        const unsigned now = Look() | told;   // (a program that closed since stays named: it may have hooked the game already)
        g_found.store(now);
        if (!rtss::WorthALine(told, now)) continue;
        told = now;
        char line[640];
        if (rtss::Format(now, "vrcam: ", line, sizeof line)) Log(line);
    }
    return 0;
}
}  // namespace

void Start() {
    if (g_thread || GetPrivateProfileIntW(L"support", L"rtss_warning", 1, g_iniPath) == 0) return;
    g_on.store(true);
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr);
    if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = g_stop = nullptr;
}

bool On() { return g_on.load(); }
unsigned Found() { return g_found.load(); }
}  // namespace rtsswatch
}  // namespace vrc
