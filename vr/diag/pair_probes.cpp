// vr/diag/pair_probes.cpp - where a stereo pair's time goes (pairtime), the draw thread's sampling profile ([debug] profile_draw), the
// constant buffers of one pair compared ([debug] cb_compare) and one pair's submissions traced ([debug] replay_trace) - cut from
// vr/vrcam.cpp (docs/plan_modules.md M1)
#include "diag.h"
#include "../vrcam_internal.h"
#include "../game_device_only.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <reshade.hpp>
#include <tlhelp32.h>

#include "MinHook.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace vrc {


// Where a pair's time goes (2026-10-08, the frame budget: ~80 pairs/s whatever
// DLSS or the window size, the GPU at 85-88%). Microseconds on the QPC clock,
// summed over the log's 10 s and printed with the pairs/s line: outside the
// pair (the game's own frame), each pass, and in each pass the stretch from
// ReShade's effects to the Present event (effects) and from Present to the pass's
// end (the Present call itself: waiting for the GPU or the swap chain).
namespace pairtime {
std::atomic<double> g_effBegin{0.0}, g_effSum{0.0}, g_presentAt{0.0};
std::atomic<uint32_t> g_presents{0};   // real presents (ReShade's), never cleared: the replay watchdog's
std::atomic<DWORD> g_drawThread{0};
double UsNow() { return flog::UsNow(); }
// This thread's own CPU time, us: a pass's wall time minus it is waiting. From its
// cycle count (GetThreadTimes moves only at the scheduler's tick: it read 0) at the
// clock's rate, measured once against QPC over 50 ms - a turbo clock reads it a little off.
double CpuUs() {
    static const double cyclesPerUs = [] {
        ULONG64 c0 = 0, c1 = 0;
        const double t0 = UsNow();
        QueryThreadCycleTime(GetCurrentThread(), &c0);
        while (UsNow() - t0 < 50000.0) {}
        QueryThreadCycleTime(GetCurrentThread(), &c1);
        return (double)(c1 - c0) / (UsNow() - t0);
    }();
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return cyclesPerUs > 0.0 ? (double)c / cyclesPerUs : 0.0;
}
// The game's busiest threads over the last call's span, % of one core, by their
// cycle counts (Toolhelp + QueryThreadCycleTime): "game thread 38%, 1234 92% (the
// render thread?)". Called with the pairs/s line; the pass's own thread is marked.
std::string BusyThreads(double spanUs) {
    static std::unordered_map<DWORD, ULONG64> last;
    std::unordered_map<DWORD, ULONG64> now;
    std::vector<std::pair<double, DWORD>> busy;
    const double cpu0 = CpuUs(); (void)cpu0;   // calibrates cyclesPerUs on first use
    ULONG64 one = 0; QueryThreadCycleTime(GetCurrentThread(), &one);
    const double perUs = one && CpuUs() > 0.0 ? (double)one / CpuUs() : 0.0;
    if (perUs <= 0.0) return "?";   // before the snapshot: it was left open here (2026-10-13)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return "?";
    THREADENTRY32 te{sizeof te};
    const DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        ULONG64 c = 0;
        if (QueryThreadCycleTime(h, &c)) {
            now[te.th32ThreadID] = c;
            if (auto it = last.find(te.th32ThreadID); it != last.end() && spanUs > 0.0)
                busy.push_back({100.0 * (double)(c - it->second) / perUs / spanUs, te.th32ThreadID});
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    last.swap(now);
    std::sort(busy.begin(), busy.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string out;
    char b[64];
    for (size_t i = 0; i < busy.size() && i < 5; ++i) {
        snprintf(b, sizeof b, "%s%lu%s %.0f%%", i ? ", " : "", busy[i].second,
                 busy[i].second == g_drawThread.load() ? " (draw)" : "", busy[i].first);
        out += b;
    }
    return out;
}
}  // namespace pairtime

// A sampling profile of the draw thread ([debug] profile_draw=1, live; it turns
// itself off after one run): 5 s at ~1 kHz, the thread suspended for each sample
// just long enough to read its context and unwind its stack into a fixed array
// (no allocation, no locks of ours while it is stopped). Then, by module and by
// function (the unwind table's start): self = the sample's leaf is there,
// incl = it is anywhere on the stack. Where the draw thread's ~6 ms an eye go.
namespace drawprof {
std::atomic<bool> g_want{false};
static std::atomic<bool> g_running{false};
struct Sample { int n; DWORD64 pc[kDepth]; };

std::string ModuleOf(DWORD64 pc, DWORD64* base) {
    HMODULE m = nullptr;
    *base = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)pc, &m) || !m)
        return "?";
    *base = (DWORD64)m;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* b = strrchr(path, '\\');
    return b ? b + 1 : path;
}

// A stopped thread's stack, copied, walked by the unwind tables. Under SEH: a
// frame kept in a register other than RSP/RBP still points into the live stack,
// whatever is there now - a bad read ends the walk, not the game.
// The game's own unwind table: D2RLoader rebuilds the image after Windows registered
// it, so RtlLookupFunctionEntry finds nothing in it - its .pdata is read in place
// (the header's exception directory, as in dump_loader's snapshot: 0x3BFF000).
PRUNTIME_FUNCTION GameFunction(DWORD64 pc, DWORD64* imageBase) {
    static const DWORD64 base = (DWORD64)GetModuleHandleW(nullptr);
    static const RUNTIME_FUNCTION* table = nullptr;
    static size_t count = 0;
    static DWORD imageSize = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        __try {
            const auto* dos = (const IMAGE_DOS_HEADER*)base;
            const auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
            const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            imageSize = nt->OptionalHeader.SizeOfImage;
            if (d.VirtualAddress && d.Size) { table = (const RUNTIME_FUNCTION*)(base + d.VirtualAddress); count = d.Size / sizeof(RUNTIME_FUNCTION); }
        } __except (EXCEPTION_EXECUTE_HANDLER) { table = nullptr; }
    }
    if (!table || pc < base || pc >= base + imageSize) return nullptr;
    const DWORD rva = (DWORD)(pc - base);
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (table[mid].EndAddress <= rva) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= count || table[lo].BeginAddress > rva) return nullptr;
    PRUNTIME_FUNCTION f = (PRUNTIME_FUNCTION)&table[lo];
    // chained entries (UNW_FLAG_CHAININFO) are followed by RtlVirtualUnwind itself
    *imageBase = base;
    return f;
}

PRUNTIME_FUNCTION FindFunction(DWORD64 pc, DWORD64* imageBase) {
    if (PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(pc, imageBase, nullptr)) return f;
    return GameFunction(pc, imageBase);
}

int UnwindCopy(CONTEXT c, uint8_t* copy, size_t copied, DWORD64* out) {
    const DWORD64 orig = c.Rsp, origEnd = c.Rsp + copied, delta = (DWORD64)copy - orig;
    const DWORD64 lo = (DWORD64)copy, hi = (DWORD64)copy + copied;
    c.Rsp += delta;
    if (c.Rbp >= orig && c.Rbp < origEnd) c.Rbp += delta;
    int n = 0;
    __try {
        for (int d = 0; d < kDepth && c.Rip; ++d) {
            out[n++] = c.Rip;
            if (c.Rsp < lo || c.Rsp + 8 > hi) break;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION f = FindFunction(c.Rip, &imageBase);
            if (!f) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; continue; }   // a leaf: the return address on top
            void* handlerData = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, f, &c, &handlerData, &establisher, nullptr);
            if (c.Rbp >= orig && c.Rbp < origEnd) c.Rbp += delta;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

DWORD WINAPI Run(void* arg) {
    const DWORD tid = (DWORD)(uintptr_t)arg;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) { Log("vrcam: profile_draw - cannot open the draw thread"); g_running.store(false); return 0; }
    static Sample samples[6000];
    static uint8_t stackCopy[32768];
    int count = 0;
    const ULONGLONG until = GetTickCount64() + 5000;
    while (GetTickCount64() < until && count < 6000) {
        // Stopped only for the context and a memcpy of its stack: nothing that
        // takes a lock (the first version unwound while it was stopped and hung
        // the game - the unwinder waited on a lock the stopped thread held).
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        size_t copied = 0;
        if (SuspendThread(th) == (DWORD)-1) { Sleep(1); continue; }
        const bool got = GetThreadContext(th, &c) != 0;
        if (got && c.Rsp) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                const DWORD64 regionEnd = (DWORD64)mbi.BaseAddress + mbi.RegionSize;
                copied = (size_t)std::min<DWORD64>(sizeof stackCopy, regionEnd - c.Rsp);
                memcpy(stackCopy, (void*)c.Rsp, copied);
            }
        }
        ResumeThread(th);
        if (!got || !copied) { Sleep(1); continue; }
        // Unwind the copy: the stack and frame registers moved into it.
        Sample& sm = samples[count];
        sm.n = UnwindCopy(c, stackCopy, copied, sm.pc);
        if (sm.n) ++count;
        Sleep(1);
    }
    CloseHandle(th);

    // by module and by function: self and inclusive
    struct Acc { int self = 0, incl = 0; };
    std::unordered_map<std::string, Acc> mods, funcs, callers, callers2, under1, under2, under3;
    // the tree under sDrawGameScreen (D2R 0x93B40): what each pass calls, two and three levels down
    const DWORD64 drawRoot = (DWORD64)GetModuleHandleW(nullptr) + 0x93B40;
    int underN = 0;
    std::unordered_map<DWORD64, std::pair<std::string, DWORD64>> modCache;
    auto modOf = [&](DWORD64 pc) -> std::pair<std::string, DWORD64> {
        const DWORD64 page = pc & ~0xFFFull;
        auto it = modCache.find(page);
        if (it != modCache.end()) return it->second;
        DWORD64 base = 0; std::string m = ModuleOf(pc, &base);
        return modCache[page] = {m, base};
    };
    auto funcOf = [&](DWORD64 pc) -> std::string {
        auto [m, base] = modOf(pc);
        DWORD64 ib = 0;
        PRUNTIME_FUNCTION f = FindFunction(pc, &ib);
        char b[128];
        if (f && ib) snprintf(b, sizeof b, "%s+0x%llX", m.c_str(), (unsigned long long)f->BeginAddress);
        else snprintf(b, sizeof b, "%s+0x%llX?", m.c_str(), (unsigned long long)(base ? pc - base : pc));
        return b;
    };
    for (int i = 0; i < count; ++i) {
        const Sample& sm = samples[i];
        {
            int r = -1;
            for (int d = 0; d < sm.n; ++d) {
                DWORD64 ib = 0;
                PRUNTIME_FUNCTION f = FindFunction(sm.pc[d], &ib);
                if (f && ib + f->BeginAddress == drawRoot) { r = d; break; }
            }
            if (r >= 1) {
                ++underN;
                const std::string a = funcOf(sm.pc[r - 1]);
                under1[a].incl++;
                if (r >= 2) {
                    const std::string b = a + " > " + funcOf(sm.pc[r - 2]);
                    under2[b].incl++;
                    if (r >= 3) under3[b + " > " + funcOf(sm.pc[r - 3])].incl++;
                }
            }
        }
        std::vector<std::string> seenM, seenF;
        for (int d = 0; d < sm.n; ++d) {
            const std::string m = modOf(sm.pc[d]).first, fn = funcOf(sm.pc[d]);
            if (d == 0) { mods[m].self++; funcs[fn].self++; }
            // who calls the leaf, and the first caller outside the system DLLs (ntdll, kernel, win32u, D3D12Core, the driver)
            if (d == 1) callers[funcOf(sm.pc[0]) + " <- " + fn].self++;
            if (d >= 1 && m != "ntdll.dll" && m != "KERNELBASE.dll" && m != "kernel32.dll" && m != "win32u.dll" &&
                m != "D3D12Core.dll" && m != "nvwgf2umx.dll" && m != "?" && seenM.empty() == false) {
                static thread_local int lastSample = -1;
                if (lastSample != i) { lastSample = i; callers2[modOf(sm.pc[0]).first + " <- " + fn].self++; }
            }
            if (std::find(seenM.begin(), seenM.end(), m) == seenM.end()) { seenM.push_back(m); mods[m].incl++; }
            if (std::find(seenF.begin(), seenF.end(), fn) == seenF.end()) { seenF.push_back(fn); funcs[fn].incl++; }
        }
    }
    auto dump = [&](const char* what, std::unordered_map<std::string, Acc>& map, bool bySelf, size_t top) {
        std::vector<std::pair<std::string, Acc>> v(map.begin(), map.end());
        std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return bySelf ? a.second.self > b.second.self : a.second.incl > b.second.incl; });
        LogF("vrcam: profile_draw %s (%d samples):", what, count);
        for (size_t i = 0; i < v.size() && i < top; ++i)
            LogF("vrcam:   %5.1f%% self %5.1f%% incl  %s", 100.0 * v[i].second.self / count, 100.0 * v[i].second.incl / count, v[i].first.c_str());
    };
    {   // every sample's stack as module+rva, leaf first, for an offline call tree (tools/prof_tree.py)
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_iniPath);
        if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
        wcscat_s(path, L"\\..\\logs\\d2r_vr_profile.txt");
        if (FILE* fp = _wfopen(path, L"w")) {
            for (int i = 0; i < count; ++i) {
                for (int d = 0; d < samples[i].n; ++d) {
                    auto [m, base] = modOf(samples[i].pc[d]);
                    fprintf(fp, "%s%s+%llX", d ? " " : "", m.c_str(), (unsigned long long)(base ? samples[i].pc[d] - base : samples[i].pc[d]));
                }
                fputc('\n', fp);
            }
            fclose(fp);
            Log("vrcam: profile_draw - the stacks went to d2rloader\\logs\\d2r_vr_profile.txt");
        }
    }
    if (count) {
        dump("modules", mods, false, 14);
        dump("functions by self", funcs, true, 25);
        dump("functions by inclusive", funcs, false, 20);
        dump("leaf <- its caller", callers, true, 30);
        LogF("vrcam: profile_draw - %d of %d samples inside sDrawGameScreen (0x93B40)", underN, count);
        dump("under sDrawGameScreen, level 1", under1, false, 15);
        dump("under sDrawGameScreen, level 2", under2, false, 25);
        dump("under sDrawGameScreen, level 3", under3, false, 30);
        dump("leaf module <- first caller in an app module", callers2, true, 30);
    }
    g_running.store(false);
    return 0;
}


// From the present callback (the draw thread): start a run when asked.
void Tick() {
    if (!g_want.load() || g_running.exchange(true)) return;
    g_want.store(false);
    Log("vrcam: profile_draw - sampling the draw thread for 5 s");
    if (HANDLE h = CreateThread(nullptr, 0, Run, (void*)(uintptr_t)GetCurrentThreadId(), 0, nullptr)) CloseHandle(h);
    else g_running.store(false);
}
}  // namespace drawprof

// One CPU pass for two GPU frames - first, can it be done at all? (2026-10-09)
// [debug] cb_compare=1 (read when vrcam joins ReShade: the game restarted with it)
// listens to every root constant buffer and root constant the game binds; each time
// [debug] cb_compare_go turns 1, one stereo pair is recorded - the left pass's
// bindings, then the right's - and compared: how many of the right eye's constants
// the left eye had byte for byte, and, by root parameter, which ones differ. If only
// a few (the camera's) differ, the left eye's command lists can be replayed for the
// right eye with those patched; if every object's own constants differ, they hold
// the view and replay is out.
namespace cbcmp {
using namespace reshade::api;
std::atomic<bool> g_go{false};
static std::atomic<bool> g_on{false};
static std::atomic<int> g_state{0};   // 0 idle, 1 armed for the next pair, 2 recording
static std::atomic<int> g_eyeRec{0};
constexpr int kMax = 40000, kHead = 4096;   // the whole 4 KB: camera buffers keep four views, the last at word 596
struct Rec { uint64_t hash; uint32_t param, kind, len, tid; uint64_t layout; uint8_t head[kHead]; };
static Rec* g_rec[2] = {nullptr, nullptr};
static float g_view[2][16] = {};   // vrcam's view (row-major, v*M) for each eye, the last one built while recording
void NoteView(int eye, const float v[16]) { if (g_state.load(std::memory_order_relaxed) == 2) memcpy(g_view[eye & 1], v, sizeof g_view[0]); }
static std::atomic<int> g_n[2];
static SRWLOCK g_lock = SRWLOCK_INIT;

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

void Add(uint32_t kind, uint64_t layout, uint32_t param, const void* data, uint32_t len) {
    const int e = g_eyeRec.load() & 1;
    const int i = g_n[e].fetch_add(1);
    if (i >= kMax) return;
    Rec& r = g_rec[e][i];
    r.kind = kind; r.layout = layout; r.param = param; r.tid = GetCurrentThreadId();
    r.len = std::min<uint32_t>(len, kHead);
    memcpy(r.head, data, r.len);
    r.hash = Fnv(r.head, r.len) ^ ((uint64_t)r.len << 56);
}

void OnPushDescriptors(command_list* cl, shader_stage, pipeline_layout layout, uint32_t param, const descriptor_table_update& u) {
    if (g_state.load(std::memory_order_relaxed) != 2 || u.type != descriptor_type::constant_buffer || !u.count || !u.descriptors) return;
    const buffer_range& br = static_cast<const buffer_range*>(u.descriptors)[0];
    device* dev = cl->get_device();
    const resource_desc d = dev->get_resource_desc(br.buffer);
    if (d.type != resource_type::buffer || br.offset >= d.buffer.size) return;
    const uint64_t len = std::min<uint64_t>(kHead, d.buffer.size - br.offset);
    void* p = nullptr;
    if (!dev->map_buffer_region(br.buffer, br.offset, len, map_access::read_only, &p) || !p) return;
    uint8_t tmp[kHead];
    __try { memcpy(tmp, p, (size_t)len); } __except (EXCEPTION_EXECUTE_HANDLER) { dev->unmap_buffer_region(br.buffer); return; }
    dev->unmap_buffer_region(br.buffer);
    Add(1, layout.handle, param, tmp, (uint32_t)len);
}

void OnPushConstants(command_list*, shader_stage, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) {
    if (g_state.load(std::memory_order_relaxed) != 2 || !values) return;
    Add(2, layout.handle, param | (first << 16), values, count * 4);
}

void Register() {
    if (!IniB(L"debug", L"cb_compare", false)) return;
    g_rec[0] = (Rec*)VirtualAlloc(nullptr, sizeof(Rec) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_rec[1] = (Rec*)VirtualAlloc(nullptr, sizeof(Rec) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_rec[0] || !g_rec[1]) return;
    reshade::register_event<reshade::addon_event::push_descriptors>(&d2rvr::D3D12Only<&OnPushDescriptors>::Call);
    reshade::register_event<reshade::addon_event::push_constants>(&d2rvr::D3D12Only<&OnPushConstants>::Call);
    g_on.store(true);
    Log("vrcam: cb_compare on - [debug] cb_compare_go=1 records one stereo pair's constant buffers");
}

// The pair's hook, before the left pass / between / after the right.
void PairBegin() {
    if (!g_on.load()) return;
    if (g_go.exchange(false)) g_state.store(1);
    if (g_state.load() == 1) { g_n[0] = 0; g_n[1] = 0; g_eyeRec = 0; g_state.store(2); }
}
void PairMiddle() { if (g_state.load() == 2) g_eyeRec = 1; }

void Report() {
    const int n0 = std::min<int>(g_n[0].load(), kMax), n1 = std::min<int>(g_n[1].load(), kMax);
    std::unordered_map<uint64_t, int> left;
    for (int i = 0; i < n0; ++i) left[g_rec[0][i].hash]++;
    struct By { int n = 0, same = 0, firstDiff = -1; uint32_t len = 0; uint64_t layout = 0; };
    std::map<std::pair<uint32_t, uint32_t>, By> by;   // (kind, param)
    int same = 0;
    for (int i = 0; i < n1; ++i) {
        const Rec& r = g_rec[1][i];
        By& b = by[{r.kind, r.param}];
        b.n++; b.len = r.len; b.layout = r.layout;
        auto it = left.find(r.hash);
        if (it != left.end() && it->second > 0) { it->second--; same++; b.same++; }
        else if (b.firstDiff < 0) b.firstDiff = i;
    }
    LogF("vrcam: cb_compare - left %d bindings, right %d; %d of the right eye's (%.1f%%) the left eye had byte for byte",
         n0, n1, same, n1 ? 100.0 * same / n1 : 0.0);
    for (auto& [k, b] : by) {
        if (b.n == b.same) continue;
        LogF("vrcam:   differ: %s param %u (first %u) layout %llX - %d of %d differ, %u bytes read",
             k.first == 1 ? "root CBV" : "root constants", k.second & 0xFFFF, k.second >> 16,
             (unsigned long long)b.layout, b.n - b.same, b.n, b.len);
    }
    // the first differing binding of the three most common differing params: its floats, right eye and
    // the left eye's binding of the same param at the same position in the draw thread's order
    int shown = 0;
    for (auto& [k, b] : by) {
        if (b.n == b.same || b.firstDiff < 0 || shown >= 4) continue;
        ++shown;
        const Rec& r = g_rec[1][b.firstDiff];
        int nth = 0;
        for (int i = 0; i < b.firstDiff; ++i) if (g_rec[1][i].kind == r.kind && g_rec[1][i].param == r.param && g_rec[1][i].tid == r.tid) ++nth;
        const Rec* l = nullptr;
        for (int i = 0, m = 0; i < n0; ++i)
            if (g_rec[0][i].kind == r.kind && g_rec[0][i].param == r.param) { if (m++ == nth) { l = &g_rec[0][i]; break; } }
        for (int part = 0; part < 2; ++part) {
            const Rec* x = part ? l : &r;
            if (!x) continue;
            std::string line;
            char b2[32];
            const float* f = (const float*)x->head;
            for (uint32_t j = 0; j < std::min<uint32_t>(x->len / 4, 40); ++j) { snprintf(b2, sizeof b2, "%s%.4g", j ? " " : "", f[j]); line += b2; }
            LogF("vrcam:   %s param %u %s: %.200s", k.first == 1 ? "CBV" : "consts", k.second & 0xFFFF, part ? "left " : "right", line.c_str());
            if (line.size() > 200) LogF("vrcam:      ...%.200s", line.c_str() + 200);
        }
    }
}
// Both eyes' records to d2rloader\logs\d2r_vr_cbdump.bin for tools/cb_diff.py:
// "CBD1", then per eye an int count and the records as they are (Rec, packed by the compiler).
void Dump() {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_cbdump.bin");
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite("CBD1", 1, 4, f);
    const uint32_t recSize = sizeof(Rec);
    fwrite(&recSize, 4, 1, f);
    for (int e = 0; e < 2; ++e) {
        const int n = std::min<int>(g_n[e].load(), kMax);
        fwrite(&n, 4, 1, f);
        fwrite(g_rec[e], sizeof(Rec), n, f);
    }
    fwrite("VIEW", 1, 4, f);
    fwrite(g_view, sizeof g_view, 1, f);
    fclose(f);
    Log("vrcam: cb_compare - both eyes' records went to d2rloader\\logs\\d2r_vr_cbdump.bin");
}
void PairEnd() {
    if (g_state.load() != 2) return;
    g_state.store(0);
    Report();
    Dump();
}
}  // namespace cbcmp

// The submissions of one stereo pair, for replaying the left eye's command lists as
// the right eye's ([debug] replay_trace=1 when vrcam joins ReShade; replay_trace_go
// 0 -> 1 records one pair): command lists reset, closed and executed (queue, list),
// the back buffer bound as a render target (which list draws the final picture),
// the queue's Signal and Wait (the game's frame fences, D3D12Core hooked through the
// queue's vtable) and Present - in order, with the time, thread and eye, to
// d2rloader\logs\d2r_vr_replaytrace.txt.
namespace rtrace {
using namespace reshade::api;
std::atomic<bool> g_go{false};
static std::atomic<bool> g_on{false};
static std::atomic<int> g_state{0};   // 0 idle, 1 armed, 2 recording
struct Ev { double t; DWORD tid; int eye; char kind; uint64_t a, b, c; };
constexpr int kMax = 60000;
static Ev* g_ev = nullptr;
static std::atomic<int> g_n{0};
static double g_t0 = 0.0;
static std::atomic<uint64_t> g_bb[8];   // the swap chain's back buffers (resource handles), from Present

void Add(char kind, uint64_t a, uint64_t b = 0, uint64_t c = 0) {
    if (g_state.load(std::memory_order_relaxed) != 2) return;
    const int i = g_n.fetch_add(1);
    if (i >= kMax) return;
    g_ev[i] = {pairtime::UsNow() - g_t0, GetCurrentThreadId(), g_eye.load(), kind, a, b, c};
}

void OnReset(command_list* cl) { Add('R', (uint64_t)cl->get_native()); }
void OnClose(command_list* cl) { Add('C', (uint64_t)cl->get_native()); }
void OnExecute(command_queue* q, command_list* cl) { Add('X', (uint64_t)q->get_native(), (uint64_t)cl->get_native(), (uint64_t)q->get_type()); }
void OnPresent(command_queue* q, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    const uint32_t n = std::min<uint32_t>(sc->get_back_buffer_count(), 8);
    for (uint32_t i = 0; i < n; ++i) g_bb[i] = sc->get_back_buffer(i).handle;
    Add('P', (uint64_t)q->get_native(), (uint64_t)sc->get_native(), sc->get_current_back_buffer_index());
}
void OnBindRT(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view) {
    if (g_state.load(std::memory_order_relaxed) != 2) return;
    device* dev = cl->get_device();
    for (uint32_t i = 0; i < count; ++i) {
        if (!rtvs[i].handle) continue;
        const uint64_t r = dev->get_resource_from_view(rtvs[i]).handle;
        for (int k = 0; k < 8; ++k)
            if (r && g_bb[k].load() == r) { Add('B', (uint64_t)cl->get_native(), r, k); break; }
    }
}

using SignalFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
using WaitFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
static SignalFn OrigSignal = nullptr;
static WaitFn OrigWait = nullptr;
HRESULT STDMETHODCALLTYPE HookSignal(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { Add('S', (uint64_t)q, (uint64_t)f, v); return OrigSignal(q, f, v); }
HRESULT STDMETHODCALLTYPE HookWait(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { Add('W', (uint64_t)q, (uint64_t)f, v); return OrigWait(q, f, v); }
static std::atomic<bool> g_hooked{false};
void HookQueue(command_queue* q) {
    if (g_hooked.exchange(true)) return;
    void** vt = *(void***)q->get_native();
    const bool ok = MH_CreateHook(vt[14], (void*)&HookSignal, (void**)&OrigSignal) == MH_OK && MH_EnableHook(vt[14]) == MH_OK &&
                    MH_CreateHook(vt[15], (void*)&HookWait, (void**)&OrigWait) == MH_OK && MH_EnableHook(vt[15]) == MH_OK;
    LogF("vrcam: replay_trace - queue Signal/Wait hooks %s", ok ? "in" : "FAILED");
}
void OnExecuteHook(command_queue* q, command_list* cl) { HookQueue(q); OnExecute(q, cl); }

void Register() {
    if (!IniB(L"debug", L"replay_trace", false)) return;
    g_ev = (Ev*)VirtualAlloc(nullptr, sizeof(Ev) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_ev) return;
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::close_command_list>(&d2rvr::D3D12Only<&OnClose>::Call);
    reshade::register_event<reshade::addon_event::execute_command_list>(&d2rvr::D3D12Only<&OnExecuteHook>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindRT>::Call);
    g_on.store(true);
    Log("vrcam: replay_trace on - [debug] replay_trace_go=1 records one stereo pair's submissions");
}

void PairBegin() {
    if (!g_on.load()) return;
    if (g_go.exchange(false)) { g_n = 0; g_t0 = pairtime::UsNow(); g_state.store(2); Add('L', 0); }
}
void PairMiddle() { Add('M', 0); }
// After the right pass: its Present (at the end of PrismBlit) is in.
void PairEnd() {
    if (g_state.load() != 2) return;
    Add('E', 0);
    g_state.store(0);
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_replaytrace.txt");
    FILE* f = _wfopen(path, L"w");
    if (!f) return;
    const int n = std::min<int>(g_n.load(), kMax);
    for (int i = 0; i < n; ++i) {
        const Ev& e = g_ev[i];
        fprintf(f, "%9.1f %6lu e%d %c %llX %llX %llX\n", e.t, e.tid, e.eye, e.kind,
                (unsigned long long)e.a, (unsigned long long)e.b, (unsigned long long)e.c);
    }
    fclose(f);
    LogF("vrcam: replay_trace - %d events of one pair to d2rloader\\logs\\d2r_vr_replaytrace.txt", n);
}
}  // namespace rtrace

}  // namespace vrc
