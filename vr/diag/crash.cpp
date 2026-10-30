// vr/diag/crash.cpp - crash reports: the start's steps and the log's first minutes to a file of our own, an exception the game dies of
// to d2r_vr_crash.txt (cut from vr/vrcam.cpp, docs/plan_modules.md M1)
#include "diag.h"
#include "../vrcam_internal.h"
#include "../log_async.h"

#include <dbghelp.h>
#include <psapi.h>

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

LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    const DWORD me = GetCurrentThreadId();
    if (g_filterThread.load() == me) return EXCEPTION_CONTINUE_SEARCH;   // a filter chained back to ours
    logasync::Drain(150);   // [debug] log_async: the lines before the crash are in the log (a writer stuck on the host's file is not waited for long)
    DWORD none = 0;
    const bool first = g_filterThread.compare_exchange_strong(none, me);
    if (first) {
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
    wchar_t sys[MAX_PATH];
    if (UINT n = GetSystemDirectoryW(sys, MAX_PATH); n && n < MAX_PATH - 16) {
        wcscat_s(sys, L"\\dbghelp.dll");
        if (HMODULE d = LoadLibraryW(sys)) g_miniDump = (MiniDumpFn)GetProcAddress(d, "MiniDumpWriteDump");
    }
    g_prev = SetUnhandledExceptionFilter(&Unhandled);
    g_veh = AddVectoredExceptionHandler(0, &Vectored);
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
