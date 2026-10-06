// writewatch.cpp - who writes a given address: hardware data breakpoints for a few seconds.
//
// Made for the left eye's shake (docs/plan_left_eye_shake.md): the hero's model matrix is written
// by the renderer after the pose is worked out on the first pass of a pair, and nothing in the
// recon says where. Dr0/Dr1 on two words of it, in every thread, catch the writing code and its
// callers; the vectored handler only records, the report is written after the breakpoints are off.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <share.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "writewatch.h"

namespace ww {
namespace {

constexpr int kFrames = 10;
constexpr int kMaxHits = 2048;

struct Hit {
    LONGLONG tUs;
    DWORD tid;
    int which;   // 0 addrA, 1 addrB
    uint64_t tag;
    DWORD64 frames[kFrames];
    int n;
};

Hit g_hits[kMaxHits];
std::atomic<int> g_count{0};
std::atomic<bool> g_armed{false};
std::atomic<bool> g_running{false};
TagFn g_tag = nullptr;
LONGLONG g_freq = 0;

uintptr_t g_a = 0, g_b = 0;
unsigned g_ms = 0;
wchar_t g_out[MAX_PATH];
LogFn g_log = nullptr;

// The writing code and its callers, from the trap's context (no C++ objects: __try).
int Unwind(const CONTEXT* from, DWORD64* out) {
    int n = 0;
    __try {
        CONTEXT c = *from;
        out[n++] = c.Rip;
        while (n < kFrames) {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
            if (fe) {
                PVOID handlerData = nullptr; DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fe, &c, &handlerData, &establisher, nullptr);
            } else {   // a leaf: the return address is on top of the stack
                c.Rip = *(DWORD64*)c.Rsp;
                c.Rsp += 8;
            }
            if (!c.Rip) break;
            out[n++] = c.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !g_armed.load()) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    const DWORD64 dr6 = c->Dr6;
    if (!(dr6 & 3)) return EXCEPTION_CONTINUE_SEARCH;
    const int i = g_count.fetch_add(1);
    if (i < kMaxHits) {
        Hit& h = g_hits[i];
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        h.tUs = g_freq ? (LONGLONG)(q.QuadPart * 1000000.0 / g_freq) : 0;
        h.tid = GetCurrentThreadId();
        h.which = (dr6 & 1) ? 0 : 1;
        h.tag = g_tag ? g_tag() : 0;
        h.n = Unwind(c, h.frames);
    }
    c->Dr6 &= ~3ull;
    return EXCEPTION_CONTINUE_EXECUTION;   // a data breakpoint traps after the write: go on
}

// Write breakpoint (4 bytes) in debug slot `slot` at addr, or that slot off, in every other
// thread of the process; the other slots are left as they are.
int SetSlot(int slot, uintptr_t addr, bool on) {
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    int done = 0;
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (SuspendThread(h) != (DWORD)-1) {
            CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &c)) {
                c.Dr7 &= ~((0x3ull << (slot * 2)) | (0xFull << (16 + slot * 4)));   // Ln Gn, RWn LENn
                const DWORD64 a = on ? addr : 0;
                if (slot == 0) c.Dr0 = a; else if (slot == 1) c.Dr1 = a; else if (slot == 2) c.Dr2 = a; else c.Dr3 = a;
                if (a) c.Dr7 |= (0x1ull << (slot * 2)) | (0x1ull << (16 + slot * 4)) | (0x3ull << (18 + slot * 4));
                if (SetThreadContext(h, &c)) ++done;
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return done;
}

std::string Where(DWORD64 a) {
    HMODULE m = nullptr;
    char buf[MAX_PATH + 32];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &m) && m) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        snprintf(buf, sizeof buf, "%ls+0x%llX", base, (unsigned long long)(a - (DWORD64)m));
    } else {
        snprintf(buf, sizeof buf, "0x%llX", (unsigned long long)a);
    }
    return buf;
}

DWORD WINAPI Run(void*) {
    PVOID veh = AddVectoredExceptionHandler(1, Handler);
    g_count.store(0);
    g_armed.store(true);
    int threads = 0;
    if (g_a) threads = SetSlot(0, g_a, true);
    if (g_b) threads = SetSlot(1, g_b, true);
    Sleep(g_ms);
    SetSlot(0, 0, false);
    SetSlot(1, 0, false);
    Sleep(100);   // a trap already raised still finds the handler
    g_armed.store(false);
    if (veh) RemoveVectoredExceptionHandler(veh);

    const int n = std::min(g_count.load(), kMaxHits);
    char line[512];
    snprintf(line, sizeof line, "writewatch: %d hits in %u ms (breakpoints in %d threads) - %ls", g_count.load(), g_ms, threads, g_out);
    if (g_log) g_log(line);

    // per writing place: how often, by which threads, with which tags
    std::map<std::pair<int, DWORD64>, std::tuple<int, std::set<DWORD>, std::set<uint64_t>, int>> places;
    for (int i = 0; i < n; ++i) {
        auto& p = places[{g_hits[i].which, g_hits[i].frames[0]}];
        ++std::get<0>(p); std::get<1>(p).insert(g_hits[i].tid);
        if (std::get<2>(p).size() < 8) std::get<2>(p).insert(g_hits[i].tag & 0xFFFF);
        std::get<3>(p) = i;
    }
    FILE* f = _wfsopen(g_out, L"w", _SH_DENYWR);
    if (f) {
        fprintf(f, "# writewatch: A 0x%llX, B 0x%llX, %u ms, %d hits\n", (unsigned long long)g_a, (unsigned long long)g_b, g_ms, g_count.load());
        fprintf(f, "# t_us,tid,word,tag(hex: pass<<32 | flags),writer,callers...\n");
        for (int i = 0; i < n; ++i) {
            const Hit& h = g_hits[i];
            fprintf(f, "%lld,%lu,%c,%llX", h.tUs, h.tid, h.which ? 'B' : 'A', (unsigned long long)h.tag);
            for (int k = 0; k < h.n; ++k) fprintf(f, ",%s", Where(h.frames[k]).c_str());
            fprintf(f, "\n");
        }
        fclose(f);
    }
    int told = 0;
    for (const auto& [key, v] : places) {
        if (++told > 8) break;
        const Hit& h = g_hits[std::get<3>(v)];
        std::string chain;
        for (int k = 1; k < h.n && k < 6; ++k) { chain += " < "; chain += Where(h.frames[k]); }
        std::string tags;
        for (uint64_t t : std::get<2>(v)) { char b[16]; snprintf(b, sizeof b, " %llX", (unsigned long long)t); tags += b; }
        snprintf(line, sizeof line, "writewatch: word %c written %d times by %s (%zu threads, flags%s)%s",
                 key.first ? 'B' : 'A', std::get<0>(v), Where(key.second).c_str(), std::get<1>(v).size(), tags.c_str(), chain.c_str());
        if (g_log) g_log(line);
    }
    g_running.store(false);
    return 0;
}

}  // namespace

bool Start(uintptr_t addrA, uintptr_t addrB, unsigned ms, const wchar_t* outPath, TagFn tag, LogFn log) {
    if (g_running.exchange(true)) return false;
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
    g_a = addrA & ~(uintptr_t)3; g_b = addrB & ~(uintptr_t)3; g_ms = ms; g_tag = tag; g_log = log;
    wcsncpy_s(g_out, outPath, _TRUNCATE);
    HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (!t) { g_running.store(false); return false; }
    CloseHandle(t);
    return true;
}

}  // namespace ww
