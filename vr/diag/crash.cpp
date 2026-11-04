// vr/diag/crash.cpp - crash reports: the start's steps and the log's first minutes to a file of our own, an exception the game dies of
// to d2r_vr_crash.txt (cut from vr/vrcam.cpp, docs/plan_modules.md M1)
#include "diag.h"
#include "hang_watch.h"
#include "../vrcam_internal.h"
#include "../log_async.h"
#include "../../shared/rtss_detect.h"

#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace vrc {


// Crash reports (2026-10-10, 0.157): a player's game died at its start with nothing in the log to
// say where. Nothing of this costs a frame anything:
// - d2rloader\logs\d2r_vr_start.txt: the log's first two minutes, line by line straight to the
//   file (WriteFile - no buffer left to lose when the game dies), each start step before and
//   after: its last line is the step that died. The run before is kept as d2r_vr_start_prev.txt.
// - d2r_vr_crash.txt: an exception the game dies of - the top-level filter, put first again every
//   2 s (the game's own crash handler is called after it) - with the module and offset it hit in,
//   the code, the registers, the stack and the module list, and a minidump beside it
//   (d2r_vr_crash.dmp). The few exceptions nothing catches (stack overflow, heap corruption, an
//   illegal instruction) are written from a vectored handler at once; an access violation is only
//   noted there (many are caught on purpose - SafeRead) and named in a report that follows.
// Collect logs (D2R VR Settings > Home) takes them all.
namespace crash {
static wchar_t g_dir[MAX_PATH] = L"";   // d2rloader\logs\ with its slash, "" until Install
static const char* g_version = "?";
static HANDLE g_start = INVALID_HANDLE_VALUE;
static std::atomic<ULONGLONG> g_mirrorUntil{0};
static char g_step[96] = "loading the plugin";   // the last start step begun, or the last milestone
static ULONGLONG g_stepAt = 0, g_installedAt = 0;
static std::atomic<int> g_busy{0}, g_firstChance{0}, g_dumps{0};
static std::atomic<DWORD> g_filterThread{0};
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;
static void* g_veh = nullptr;
using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
static MiniDumpFn g_miniDump = nullptr;
struct Seen { DWORD tid; DWORD64 pc, target; ULONG_PTR rw; ULONGLONG at; };
static Seen g_lastAv{};   // the last access violation, first chance (it may well have been caught)
static std::atomic<uint32_t> g_avTotal{0};   // how many there were, first chance (caught ones too: each one costs its thread an unwind; [debug] frame_trace tells the ones of a pair)
uint32_t AvTotal() { return g_avTotal.load(std::memory_order_relaxed); }

// One line to d2r_vr_start.txt, with the time.
void Line(const char* text) {
    if (g_start == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[640];
    int n = snprintf(b, sizeof b, "[%02d:%02d:%02d.%03d] %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
    if (n <= 0) return;
    if (n >= (int)sizeof b) { n = (int)sizeof b - 1; b[n - 2] = '\r'; b[n - 1] = '\n'; }
    DWORD w = 0;
    WriteFile(g_start, b, (DWORD)n, &w, nullptr);
}
void Mirror(const char* text) {
    if (GetTickCount64() < g_mirrorUntil.load(std::memory_order_relaxed)) Line(text);
}

// The start's steps: the one before done (with its time), this one begun. nullptr: the start is over.
void Step(const char* name) {
    const ULONGLONG now = GetTickCount64();
    char b[200];
    if (g_stepAt) { snprintf(b, sizeof b, "vrcam start: %s - done (%llu ms)", g_step, now - g_stepAt); Log(b); }
    if (name) {
        strncpy_s(g_step, name, _TRUNCATE);
        g_stepAt = now;
        snprintf(b, sizeof b, "vrcam start: %s ...", name);
        Log(b);
    } else {
        strncpy_s(g_step, "the plugin loaded, the game going on", _TRUNCATE);
        g_stepAt = 0;
    }
}
// A milestone after the start (the first present, ReShade joined...): the report names the last one.
void Mark(const char* what) {
    strncpy_s(g_step, what, _TRUNCATE);
    char b[160];
    snprintf(b, sizeof b, "vrcam milestone: %s", what);
    Line(b);
}

// The report's own memory: the heap may be what broke, and a stack overflow leaves little stack.
static char g_buf[32768];
size_t g_len = 0;
void Add(const char* fmt, ...) {
    if (g_len >= sizeof g_buf - 1) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_buf + g_len, sizeof g_buf - g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len = std::min(g_len + (size_t)n, sizeof g_buf - 1);
}
struct ModInfo { uint64_t base, end; char name[56]; };
static HMODULE g_mods[1024];
static ModInfo g_modInfo[1024];
static int g_modCount = 0;
void ListMods() {   // psapi reads the module list in place: no loader lock (DumpAllStacks)
    g_modCount = 0;
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), g_mods, sizeof g_mods, &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i) {
        MODULEINFO mi{};
        if (!K32GetModuleInformation(GetCurrentProcess(), g_mods[i], &mi, sizeof mi)) continue;
        ModInfo& m = g_modInfo[g_modCount++];
        m.base = (uint64_t)mi.lpBaseOfDll;
        m.end = m.base + mi.SizeOfImage;
        if (!K32GetModuleBaseNameA(GetCurrentProcess(), g_mods[i], m.name, sizeof m.name)) strcpy_s(m.name, "?");
    }
}
void AddWhere(DWORD64 pc) {
    for (int i = 0; i < g_modCount; ++i)
        if (pc >= g_modInfo[i].base && pc < g_modInfo[i].end) { Add("%s+0x%llX", g_modInfo[i].name, pc - g_modInfo[i].base); return; }
    Add("0x%llX (in no module)", pc);
}
const char* CodeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case 0xC0000374: return "heap corruption";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error (a file the code was read from went away)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case 0xE06D7363: return "C++ exception not caught";
    case 0xC0000409: return "stack buffer overrun / fail fast";
    case 0x80000003: return "breakpoint";
    default: return "";
    }
}
void Write(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_dir);
    wcscat_s(path, name);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, g_buf, (DWORD)g_len, &w, nullptr);
    CloseHandle(f);
}

// [support] rtss_warning (shared/rtss_detect.h): RivaTuner found - what rtss_watch.cpp saw and the module list just read (ListMods:
// no loader lock) - one line in the report, so the crash reporter's text says it too. Nothing found or the key off: nothing.
static void AddRtss(void (*add)(const char*, ...)) {
    if (!rtsswatch::On()) return;
    unsigned found = rtsswatch::Found();
    for (int i = 0; i < g_modCount; ++i) found |= rtss::HookBitA(g_modInfo[i].name);
    char line[640];
    if (rtss::Format(found, "", line, sizeof line)) add("%s\r\n", line);
}

// To d2r_vr_crash.txt: what, where, the registers, the stack, the last access violation before it;
// `fatal` (the one the game dies of) the whole module list too.
void Report(EXCEPTION_POINTERS* ep, const char* kind, bool walk, bool fatal) {
    if (!g_dir[0] || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return;
    if (!fatal) {   // first chance: the first few, never two at once
        if (g_firstChance.load() >= 4 || g_busy.exchange(1)) return;
        ++g_firstChance;
    } else {        // the one the game dies of waits for one being written (a writer stuck that long: anyway)
        for (int i = 0; i < 200 && g_busy.exchange(1); ++i) Sleep(1);
    }
    const EXCEPTION_RECORD& r = *ep->ExceptionRecord;
    const CONTEXT& c = *ep->ContextRecord;
    ListMods();
    g_len = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    Add("==== %04d-%02d-%02d %02d:%02d:%02d - D2R VR vrcam %s - %s ====\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, g_version, kind);
    Add("exception 0x%08lX %s at ", r.ExceptionCode, CodeName(r.ExceptionCode));
    AddWhere((DWORD64)r.ExceptionAddress);
    if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2)
        Add(" - %s 0x%llX", r.ExceptionInformation[0] == 1 ? "writing" : r.ExceptionInformation[0] == 8 ? "running code at" : "reading",
            (unsigned long long)r.ExceptionInformation[1]);
    const DWORD tid = GetCurrentThreadId();
    Add("\r\nthread %lu%s; %.1f s after the plugin loaded; the start's last step: %s\r\n", tid,
        tid == pairtime::g_drawThread.load() ? " (the game's draw thread)" : "", (GetTickCount64() - g_installedAt) / 1000.0, g_step);
    Add("rip %016llX rsp %016llX rbp %016llX\r\nrax %016llX rbx %016llX rcx %016llX rdx %016llX\r\nrsi %016llX rdi %016llX r8  %016llX r9  %016llX\r\n"
        "r10 %016llX r11 %016llX r12 %016llX r13 %016llX\r\nr14 %016llX r15 %016llX\r\n",
        c.Rip, c.Rsp, c.Rbp, c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15);
    AddRtss(&Add);
    if (walk && c.Rsp) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
            DWORD64 pcs[drawprof::kDepth];
            const size_t have = (size_t)((DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
            const int n = drawprof::UnwindCopy(c, (uint8_t*)c.Rsp, have, pcs);   // the stack itself: the copy at no offset
            Add("stack:\r\n");
            for (int i = 0; i < n; ++i) { Add("    "); AddWhere(pcs[i]); Add("\r\n"); }
        }
    }
    const Seen av = g_lastAv;
    if (av.at && r.ExceptionCode != EXCEPTION_ACCESS_VIOLATION && GetTickCount64() - av.at < 10000) {
        Add("an access violation %.1f s before (thread %lu, maybe caught): at ", (GetTickCount64() - av.at) / 1000.0, av.tid);
        AddWhere(av.pc);
        Add(" %s 0x%llX\r\n", av.rw == 1 ? "writing" : av.rw == 8 ? "running code at" : "reading", (unsigned long long)av.target);
    }
    if (fatal) {
        Add("modules:\r\n");
        for (int i = 0; i < g_modCount; ++i)
            Add("    %016llX %8llX %s\r\n", g_modInfo[i].base, g_modInfo[i].end - g_modInfo[i].base, g_modInfo[i].name);
    }
    Add("\r\n");
    Write(L"d2r_vr_crash.txt");
    Line(kind);
    Line("vrcam: the crash went to d2rloader\\logs\\d2r_vr_crash.txt");
    g_busy.store(0);
}

// A minidump beside the report, once a run: the threads, their stacks, the modules.
void Dump(EXCEPTION_POINTERS* ep) {
    if (!g_miniDump || !g_dir[0] || g_dumps.exchange(1)) return;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_dir);
    wcscat_s(path, L"d2r_vr_crash.dmp");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    const BOOL ok = g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                               (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                               &mei, nullptr, nullptr);
    CloseHandle(f);
    Line(ok ? "vrcam: minidump d2rloader\\logs\\d2r_vr_crash.dmp written" : "vrcam: the minidump could not be written");
}

// The hang watchdog (vr/diag/hang_watch.h), every mode: presents standing still for 5 s - every thread's stack and the draw
// thread's registers to d2r_vr_hang.txt, then the draw thread looked at 12 times more (a spin shows the loop's extent, a wait
// the same place); 15 s - a minidump beside it (d2r_vr_hang.dmp). All of it straight to the files from our own memory, before
// any log line: the stuck thread may hold the log's lock or the heap's.
static char g_hangBuf[196608];
static size_t g_hangLen = 0;
static uint8_t g_hangStack[65536];
void HAdd(const char* fmt, ...) {
    if (g_hangLen >= sizeof g_hangBuf - 1) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_hangBuf + g_hangLen, sizeof g_hangBuf - g_hangLen, fmt, ap);
    va_end(ap);
    if (n > 0) g_hangLen = std::min(g_hangLen + (size_t)n, sizeof g_hangBuf - 1);
}
void HAddWhere(DWORD64 pc) {
    for (int i = 0; i < g_modCount; ++i)
        if (pc >= g_modInfo[i].base && pc < g_modInfo[i].end) { HAdd("%s+0x%llX", g_modInfo[i].name, pc - g_modInfo[i].base); return; }
    HAdd("0x%llX", pc);
}
#pragma optimize("", off)
static size_t CopyStack(const CONTEXT& c) noexcept {   // the stack from rsp up, as much as is committed and fits
    MEMORY_BASIC_INFORMATION mbi{};
    if (!c.Rsp || !VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    const size_t n = (size_t)std::min<DWORD64>(sizeof g_hangStack, (DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
    __try { memcpy(g_hangStack, (const void*)c.Rsp, n); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return n;
}
#pragma optimize("", on)
// One thread stopped for its context and a copy of its stack, then let go; false: it could not be looked at.
static bool LookAt(DWORD tid, CONTEXT* c, size_t* copied) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) return false;
    *copied = 0;
    bool ok = false;
    if (SuspendThread(th) != (DWORD)-1) {
        memset(c, 0, sizeof *c);
        c->ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, c)) { ok = true; *copied = CopyStack(*c); }
        ResumeThread(th);
    }
    CloseHandle(th);
    return ok;
}
static void HAddFrames(const CONTEXT& c, size_t copied, int most) {
    DWORD64 pcs[drawprof::kDepth];
    const int n = copied ? drawprof::UnwindCopy(c, g_hangStack, copied, pcs) : 0;
    if (!n) { HAdd("    "); HAddWhere(c.Rip); HAdd(" (no stack read)\r\n"); return; }
    for (int i = 0; i < n && i < most; ++i) { HAdd("    "); HAddWhere(pcs[i]); HAdd("\r\n"); }
}
static void HangFile(double stoodMs, uint32_t presents) {
    ListMods();
    g_hangLen = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    const DWORD draw = pairtime::g_drawThread.load(), me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HAdd("==== %04d-%02d-%02d %02d:%02d:%02d.%03d - D2R VR vrcam %s - no present for %.1f s (%u presents before), %.0f s after the plugin loaded ====\r\n",
         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, g_version, stoodMs / 1000.0, presents,
         (GetTickCount64() - g_installedAt) / 1000.0);
    HAdd("the game's draw thread (presents): %lu; the start's last step: %s\r\n", draw, g_step);
    AddRtss(&HAdd);
    if (draw) {   // its registers, for the PDB (releases\pdb\<version>) and the game's image
        CONTEXT c;
        size_t copied = 0;
        if (LookAt(draw, &c, &copied))
            HAdd("draw thread registers:\r\nrip %016llX rsp %016llX rbp %016llX\r\nrax %016llX rbx %016llX rcx %016llX rdx %016llX\r\n"
                 "rsi %016llX rdi %016llX r8  %016llX r9  %016llX\r\nr10 %016llX r11 %016llX r12 %016llX r13 %016llX\r\nr14 %016llX r15 %016llX\r\n"
                 "xmm0 %08X %08X xmm1 %08X %08X xmm2 %08X %08X xmm3 %08X %08X\r\n",
                 c.Rip, c.Rsp, c.Rbp, c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15,
                 (unsigned)c.Xmm0.Low, (unsigned)(c.Xmm0.Low >> 32), (unsigned)c.Xmm1.Low, (unsigned)(c.Xmm1.Low >> 32),
                 (unsigned)c.Xmm2.Low, (unsigned)(c.Xmm2.Low >> 32), (unsigned)c.Xmm3.Low, (unsigned)(c.Xmm3.Low >> 32));
    }
    HAdd("every thread:\r\n");
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{sizeof te};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
            CONTEXT c;
            size_t copied = 0;
            if (!LookAt(te.th32ThreadID, &c, &copied)) continue;
            HAdd("thread %lu%s\r\n", te.th32ThreadID, te.th32ThreadID == draw ? " (draw)" : "");
            HAddFrames(c, copied, drawprof::kDepth);
        }
        CloseHandle(snap);
    }
    if (draw) {   // where it is over 1.2 s: one place each time = a wait or a tight loop there; several = the loop's extent
        HAdd("the draw thread again, every 100 ms (the top 8 frames):\r\n");
        for (int i = 0; i < 12; ++i) {
            Sleep(100);
            CONTEXT c;
            size_t copied = 0;
            if (!LookAt(draw, &c, &copied)) break;
            HAdd("  look %d:\r\n", i + 1);
            HAddFrames(c, copied, 8);
        }
    }
    HAdd("modules:\r\n");
    for (int i = 0; i < g_modCount; ++i) HAdd("    %016llX %8llX %s\r\n", g_modInfo[i].base, g_modInfo[i].end - g_modInfo[i].base, g_modInfo[i].name);
    wchar_t a[MAX_PATH], b[MAX_PATH];
    wcscpy_s(a, g_dir); wcscat_s(a, L"d2r_vr_hang.txt");
    wcscpy_s(b, g_dir); wcscat_s(b, L"d2r_vr_hang_prev.txt");
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);   // the one before kept (a long load's file does not take a hang's place)
    HANDLE f = CreateFileW(a, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, g_hangBuf, (DWORD)g_hangLen, &w, nullptr);
    CloseHandle(f);
}
static bool HangDump() {
    if (!g_miniDump || !g_dir[0]) return false;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_dir);
    wcscat_s(path, L"d2r_vr_hang.dmp");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const BOOL ok = g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                               (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                               nullptr, nullptr, nullptr);
    CloseHandle(f);
    return ok != FALSE;
}
// The presents came back: what d2r_vr_hang.txt holds was a long frame, not a hang. Said at its end, and the files renamed d2r_vr_stall.*,
// so d2r_vr_hang.txt only ever stands for a stall that did not end (2026-11-04: the crash reporter gave a 5 s load at the start, 27 minutes
// before, as the place of a fast fail it had no place for).
static void HangRecovered(double stoodMs) {
    wchar_t a[MAX_PATH], b[MAX_PATH];
    wcscpy_s(a, g_dir); wcscat_s(a, L"d2r_vr_hang.txt");
    wcscpy_s(b, g_dir); wcscat_s(b, L"d2r_vr_stall.txt");
    HANDLE f = CreateFileW(a, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[200];
    const int n = snprintf(line, sizeof line, "==== %02d:%02d:%02d.%03d - presents again after %.1f s: the game went on - a long frame, not a hang ====\r\n",
                           t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, stoodMs / 1000.0);
    DWORD w = 0;
    if (n > 0) WriteFile(f, line, (DWORD)std::min<int>(n, (int)sizeof line - 1), &w, nullptr);
    CloseHandle(f);
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    wcscpy_s(a, g_dir); wcscat_s(a, L"d2r_vr_hang.dmp");
    wcscpy_s(b, g_dir); wcscat_s(b, L"d2r_vr_stall.dmp");
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);   // (none when it stood under 15 s)
}
static std::atomic<bool> g_hangStop{false};
static HANDLE g_hangThread = nullptr;
static DWORD WINAPI HangWatch(void*) {
    hangwatch::Watch w;
    bool told = false;
    while (!g_hangStop.load()) {
        Sleep(500);
        const HWND wnd = w.started ? GameWindow() : nullptr;
        const hangwatch::Act a = hangwatch::Step(w, pairtime::g_presents.load(std::memory_order_relaxed), (double)GetTickCount64(), wnd && IsIconic(wnd));
        if (w.started && !told) {
            told = true;
            Log("vrcam: hang watchdog - watching the presents (any mode): none for 5 s - every thread's stack to d2rloader\\logs\\d2r_vr_hang.txt, for 15 s - a minidump d2r_vr_hang.dmp");
        }
        switch (a) {
        case hangwatch::Act::Stacks:
            HangFile((double)GetTickCount64() - w.movedAtMs, w.seen);
            Line("vrcam: hang watchdog - no present for 5 s: every thread's stack went to d2rloader\\logs\\d2r_vr_hang.txt");   // (not the log yet: its lock may be what stands)
            break;
        case hangwatch::Act::Dump: {
            const bool ok = HangDump();
            Line(ok ? "vrcam: hang watchdog - still no present after 15 s: minidump d2rloader\\logs\\d2r_vr_hang.dmp written" : "vrcam: hang watchdog - the minidump could not be written");
            Log(ok ? "vrcam: hang watchdog - no present for 15 s: every thread's stack in d2rloader\\logs\\d2r_vr_hang.txt (at 5 s), minidump d2r_vr_hang.dmp"
                   : "vrcam: hang watchdog - no present for 15 s: every thread's stack in d2rloader\\logs\\d2r_vr_hang.txt (at 5 s); the minidump could not be written");
            break;
        }
        case hangwatch::Act::Back:
            HangRecovered(w.stoodMs);
            LogF("vrcam: hang watchdog - presents again after %.1f s: a long frame (a load?), not a hang - where it stood is in d2rloader\\logs\\d2r_vr_stall.txt", w.stoodMs / 1000.0);
            break;
        default: break;
        }
    }
    return 0;
}

// The report of the exception the game dies of is written once: by our thread's guard (OwnThreadFilter) or the top-level filter.
static std::atomic<bool> g_fatalDone{false};

// Our own threads (the update thread, vr/frame/frame_order.cpp): their bodies run inside __try with this as the filter, which Windows
// asks only for an exception nothing inside them catches. The top-level filter is fought over (2026-11-04, Serg's PC: "another crash
// handler was set" again and again - D2RLoader's own; a C++ exception escaping our update thread went to its "Fatal error" box and
// left no report of ours, and the watcher saw no crash). Here it is reported wherever the top-level filter is, and the search goes on:
// the game's or D2RLoader's handler runs after it as before.
long OwnThreadFilter(struct _EXCEPTION_POINTERS* ep, const char* thread) {
    if (!g_fatalDone.exchange(true)) {
        logasync::Drain(150);
        crashwatch::Crashed();   // [support] crash_reports: no "clean end" from a detach after it
        char kind[160];
        snprintf(kind, sizeof kind, "UNHANDLED in our %s - the game closes on this", thread);
        Report(ep, kind, true, true);
        Dump(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    const DWORD me = GetCurrentThreadId();
    if (g_filterThread.load() == me) return EXCEPTION_CONTINUE_SEARCH;   // a filter chained back to ours
    logasync::Drain(150);   // [debug] log_async: the lines before the crash are in the log (a writer stuck on the host's file is not waited for long)
    DWORD none = 0;
    const bool first = g_filterThread.compare_exchange_strong(none, me);
    if (first && !g_fatalDone.exchange(true)) {   // (our thread's guard may have written it already)
        crashwatch::Crashed();   // [support] crash_reports: no "clean end" from the detach that may follow (the game's handler may ExitProcess)
        Report(ep, "UNHANDLED - the game closes on this", true, true);
        Dump(ep);
    }
    LONG r = EXCEPTION_CONTINUE_SEARCH;
    if (g_prev && g_prev != &Unhandled) r = g_prev(ep);   // the game's own crash handler, as before
    if (first) g_filterThread.store(0);
    return r;
}

LONG WINAPI Vectored(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    if (!r) return EXCEPTION_CONTINUE_SEARCH;
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:   // noted only: caught ones are many (SafeRead)
        g_avTotal.fetch_add(1, std::memory_order_relaxed);
        g_lastAv = {GetCurrentThreadId(), (DWORD64)r->ExceptionAddress, r->NumberParameters >= 2 ? (DWORD64)r->ExceptionInformation[1] : 0,
                    r->NumberParameters >= 1 ? r->ExceptionInformation[0] : 0, GetTickCount64()};
        break;
    case EXCEPTION_STACK_OVERFLOW:     // little stack left: no walk
        Report(ep, "stack overflow (first chance)", false, false);
        break;
    case 0xC0000374:                   // heap corruption: the process ends without the filter
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
        Report(ep, "first chance - an exception seldom caught", true, false);
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// First thing at the plugin's load: the files, the filter and the vectored handler.
void Install(const char* version) {
    g_version = version;
    g_installedAt = GetTickCount64();
    wcscpy_s(g_dir, g_iniPath);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\')) slash[1] = 0;
    wcscat_s(g_dir, L"..\\logs\\");
    CreateDirectoryW(g_dir, nullptr);
    wchar_t a[MAX_PATH], b[MAX_PATH];
    auto at = [&](wchar_t* out, const wchar_t* name) { wcscpy_s(out, MAX_PATH, g_dir); wcscat_s(out, MAX_PATH, name); };
    at(a, L"d2r_vr_start.txt"); at(b, L"d2r_vr_start_prev.txt");
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    g_start = CreateFileW(a, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    g_mirrorUntil.store(g_installedAt + 120000);
    at(a, L"d2r_vr_crash.txt"); at(b, L"d2r_vr_crash_old.txt");
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(a, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 1024 * 1024)) MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    at(a, L"d2r_vr_hang.txt"); at(b, L"d2r_vr_hang_prev.txt");   // a run before's hang is not this run's: d2r_vr_hang.txt is only ever this run's
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    at(a, L"d2r_vr_hang.dmp"); at(b, L"d2r_vr_hang_prev.dmp");
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    wchar_t sys[MAX_PATH];
    if (UINT n = GetSystemDirectoryW(sys, MAX_PATH); n && n < MAX_PATH - 16) {
        wcscat_s(sys, L"\\dbghelp.dll");
        if (HMODULE d = LoadLibraryW(sys)) g_miniDump = (MiniDumpFn)GetProcAddress(d, "MiniDumpWriteDump");
    }
    g_prev = SetUnhandledExceptionFilter(&Unhandled);
    g_veh = AddVectoredExceptionHandler(0, &Vectored);
    g_hangThread = CreateThread(nullptr, 0, &HangWatch, nullptr, 0, nullptr);   // idle until the game's first present
    char line[160];
    snprintf(line, sizeof line, "D2R VR vrcam %s starting - this file is the log's first two minutes, written as it goes", version);
    Line(line);
}

// Every 2 s (the update thread): our filter first again if something set its own since; theirs after.
void Keep() {
    const LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(&Unhandled);
    if (cur == &Unhandled) return;
    g_prev = cur;
    static int told = 0;
    if (told++ < 3) LogF("vrcam: crash reports - another crash handler was set (%p): ours goes first again, then it", (void*)cur);
}

void Uninstall() {
    g_hangStop.store(true);
    if (g_hangThread) { WaitForSingleObject(g_hangThread, 2000); CloseHandle(g_hangThread); g_hangThread = nullptr; }
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
    const LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(g_prev);
    if (cur != &Unhandled) SetUnhandledExceptionFilter(cur);   // not ours any more: left as it was
    g_mirrorUntil.store(0);
    const HANDLE h = g_start;
    g_start = INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}
}  // namespace crash

}  // namespace vrc
