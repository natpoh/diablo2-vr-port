// stats_leak.cpp - [debug] stats_leak_trace on the game: see stats_leak.h. Read only: two of the game's functions followed, their originals
// always run with the arguments they were given; nothing of the game's is written. Addresses in D2RLoader 1.3.1's layout (3.3.93787), each
// hook behind the loader's check of the function's first bytes.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "log_async.h"
#include "sigscan.h"
#include "stats_leak.h"
#include "stats_leak_wire.h"

using namespace D2RL;

namespace statsleak {
namespace {

constexpr uint64_t RVA_ALLOC_PLAIN = 0x2F7300;   // (flags, float bits, id, id) -> a plain stat list, from pool 0x2A9A430
constexpr uint64_t RVA_FREE = 0x2F9EB0;          // (byte, list): a plain list, or an Ex list with what it carries; asserts at the last Ex
constexpr uint64_t RVA_POOL_PLAIN = 0x2A9A430, RVA_POOL_EX = 0x2A9A4D0;
constexpr uint32_t kPoolLive = 0x88;             // a pool's live count (dec in its free 0x1230490)
constexpr uint32_t kListFlags = 0x1C;            // < 0: an Ex list (a unit's stat holder)
constexpr uint32_t kChainA = 0x90, kChainB = 0x98, kNext = 0x68;   // an Ex list's carried plain lists and their link

// mov [rsp+10h],rbx; mov [rsp+18h],rsi; mov [rsp+20h],rdi; mov [rsp+8],ecx; push rbp
const uint8_t kSigAlloc[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x89, 0x4C, 0x24, 0x08, 0x55};
// mov [rsp+8],rbx; mov [rsp+18h],rbp; mov [rsp+20h],rsi; push rdi; sub rsp,20h
const uint8_t kSigFree[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20};

using AllocFn = uintptr_t (*)(uint32_t flags, uint32_t floatBits, uint32_t a, uint32_t b);
using FreeFn = void (*)(uint8_t which, uintptr_t list);

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_wanted{false};
std::atomic<int> g_state{0};      // 0 not in yet, 1 in, 2 could not go in
std::atomic<bool> g_told{false};  // the leak written once
AllocFn g_origAlloc = nullptr;
FreeFn g_origFree = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
Tracker* g_tracker = nullptr;     // never freed: the game's threads may be in a hook at the process's end
Range g_ours, g_game;
wchar_t g_file[MAX_PATH] = {};    // d2rloader\logs\d2r_vr_stats_leak.txt

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// The verdict's lines: on this thread, now - the game asserts and ends a moment after. Into the plugin's log and the report file.
void Say(FILE* f, const char* text) {
    if (g_ctx) g_ctx->LogInfo(text);
    if (f) { std::fputs(text, f); std::fputc('\n', f); std::fflush(f); }
}

bool Read(void* out, uintptr_t at, size_t n) {
    __try { std::memcpy(out, (const void*)at, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

int32_t PoolLive(uint64_t poolRva) {
    int32_t v = -1;
    const uintptr_t p = d2rsig::Addr(poolRva);
    return p && Read(&v, p + kPoolLive, sizeof v) ? v : -1;
}

Range ModuleRange(HMODULE m) {
    Range r;
    if (!m) return r;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(m) + dos->e_lfanew);
    r.base = reinterpret_cast<uintptr_t>(m);
    r.size = nt->OptionalHeader.SizeOfImage;
    return r;
}

uintptr_t HookAlloc(uint32_t flags, uint32_t floatBits, uint32_t a, uint32_t b) {
    Birth birth;
    birth.args[0] = flags; birth.args[1] = floatBits; birth.args[2] = a; birth.args[3] = b;
    void* frames[kFrames] = {};
    birth.frameCount = (int)RtlCaptureStackBackTrace(1, kFrames, frames, nullptr);   // (1: this hook left out - the game's caller first)
    for (int i = 0; i < birth.frameCount; ++i) birth.frames[i] = (uint64_t)frames[i];
    const uintptr_t list = g_origAlloc(flags, floatBits, a, b);
    if (list) {
        AcquireSRWLockExclusive(&g_lock);
        g_tracker->Born(list, birth);
        ReleaseSRWLockExclusive(&g_lock);
    }
    return list;
}

// The plain lists an Ex list carries (both chains), as its free walks them.
void Carried(uintptr_t ex, std::unordered_set<uintptr_t>* out) {
    for (uint32_t head : {kChainA, kChainB}) {
        uintptr_t p = 0;
        if (!Read(&p, ex + head, sizeof p)) continue;
        for (int guard = 0; p && guard < 100000; ++guard) {
            int32_t flags = 0;
            if (Read(&flags, p + kListFlags, sizeof flags) && flags >= 0) out->insert(p);
            if (!Read(&p, p + kNext, sizeof p)) break;
        }
    }
}

void TellLeak(uintptr_t ex, int32_t plainLive) {
    std::unordered_set<uintptr_t> carried;
    Carried(ex, &carried);
    AcquireSRWLockShared(&g_lock);
    const std::vector<Leak> leaks = g_tracker->Leaked(carried);
    const uint64_t born = g_tracker->BornCount(), freed = g_tracker->FreedCount(), unknown = g_tracker->FreedUnknown();
    ReleaseSRWLockShared(&g_lock);
    const Verdict v = Judge(plainLive, (int64_t)carried.size(), (int64_t)leaks.size());
    if (!v.leak) return;
    if (g_told.exchange(true)) return;
    logasync::Drain(500);   // the lines before it first
    FILE* f = nullptr;
    if (g_file[0]) _wfopen_s(&f, g_file, L"w");
    char line[1400];
    std::snprintf(line, sizeof line,
                  "vrcam: stats leak ([debug] stats_leak_trace) - the last unit's stat holder %p is freed with %d plain stat list(s) alive that it does not "
                  "carry (%zu it carries): the game's assert \"We have leaked Stats\" follows. %zu of them were born under the trace (below, oldest first), "
                  "%lld before it began (not known); the trace saw %llu born, %llu freed, %llu freed that were born before it",
                  (void*)ex, (int)v.notCarried, carried.size(), leaks.size(), (long long)v.untracked, (unsigned long long)born, (unsigned long long)freed,
                  (unsigned long long)unknown);
    Say(f, line);
    int ours = 0, n = 0;
    for (const Leak& l : leaks) {
        if (FirstIn(l.birth, g_ours) >= 0) ++ours;
        if (n++ >= 40) continue;   // (the first 40 written, all counted)
        char d[1100];
        Describe(l, g_game.base, g_game.size, g_ours, d, sizeof d);
        std::snprintf(line, sizeof line, "vrcam: stats leak - %s", d);
        Say(f, line);
    }
    std::snprintf(line, sizeof line, "vrcam: stats leak - %d of the %zu known leaked list(s) had D2R VR's code (d2rl-vrcam.dll at %p) on the stack that made them%s", ours,
                  leaks.size(), (void*)g_ours.base, ours ? "" : " - none: made by the game's own calls");
    Say(f, line);
    if (f) std::fclose(f);
}

void HookFree(uint8_t which, uintptr_t list) {
    int32_t flags = 0;
    if (list && Read(&flags, list + kListFlags, sizeof flags)) {
        if (flags >= 0) {   // a plain list: forgotten before the pool can hand its memory to another thread's alloc
            AcquireSRWLockExclusive(&g_lock);
            g_tracker->Freed(list);
            ReleaseSRWLockExclusive(&g_lock);
        } else if (PoolLive(RVA_POOL_EX) == 1) {   // the last unit's stat holder: the game's check follows its free
            const int32_t plain = PoolLive(RVA_POOL_PLAIN);
            if (plain > 0) TellLeak(list, plain);
        }
    }
    g_origFree(which, list);
}

}   // namespace

void Init(const PluginContext* ctx, bool wanted, const wchar_t* iniPath) {
    g_ctx = ctx;
    g_wanted.store(wanted);
    if (iniPath) {
        wcscpy_s(g_file, iniPath);
        if (wchar_t* slash = wcsrchr(g_file, L'\\')) slash[1] = 0;
        wcscat_s(g_file, L"..\\logs\\d2r_vr_stats_leak.txt");
    }
}

void Tick() {
    if (!g_wanted.load(std::memory_order_relaxed) || g_state.load(std::memory_order_relaxed) != 0 || !g_ctx) return;
    static ULONGLONG tryAt = 0, since = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < tryAt) return;
    tryAt = now + 500;
    if (!since) since = now;
    // the code's page is decrypted the first time the game runs it (the character screen's heroes): until then the bytes do not check out
    if (!d2rsig::Check(RVA_ALLOC_PLAIN, kSigAlloc, sizeof kSigAlloc) || !d2rsig::Check(RVA_FREE, kSigFree, sizeof kSigFree)) {
        if (now - since > 120000) {
            g_state.store(2);
            Log("vrcam: stats leak trace ([debug] stats_leak_trace) NOT in - the game's stat list functions (0x2F7300, 0x2F9EB0) are not as this build has them");
        }
        return;
    }
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Tick), &self);
    g_ours = ModuleRange(self);
    g_game = ModuleRange(GetModuleHandleW(nullptr));
    g_tracker = new Tracker();
    // the free first: a list born under the trace is always forgotten at its free
    const bool in = d2rsig::Hook(RVA_FREE, kSigFree, sizeof kSigFree, reinterpret_cast<void*>(&HookFree), reinterpret_cast<void**>(&g_origFree)) &&
                    d2rsig::Hook(RVA_ALLOC_PLAIN, kSigAlloc, sizeof kSigAlloc, reinterpret_cast<void*>(&HookAlloc), reinterpret_cast<void**>(&g_origAlloc));
    g_state.store(in ? 1 : 2);
    char b[400];
    std::snprintf(b, sizeof b,
                  in ? "vrcam: stats leak trace in ([debug] stats_leak_trace) - each plain stat list's birth is noted (the stack that made it) and forgotten at its "
                       "free; if the last unit's stat holder goes with lists alive, the log and d2rloader\\logs\\d2r_vr_stats_leak.txt name them (now: %d plain, %d holders alive)"
                     : "vrcam: stats leak trace ([debug] stats_leak_trace) - the hooks did NOT go in (%d plain, %d holders alive)",
                  PoolLive(RVA_POOL_PLAIN), PoolLive(RVA_POOL_EX));
    Log(b);
}

}   // namespace statsleak
