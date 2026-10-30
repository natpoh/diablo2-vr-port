// vr/render/stereo/prev_scan.cpp - where the game keeps the views of frames past ([debug] find_prev_matrix) (cut from vr/vrcam.cpp, docs/plan_modules.md M14).
// What it gives: vr/render/stereo/stereo.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// Where the game keeps the views of frames past (2026-10-08, [debug]
// find_prev_matrix=1): DLSS's motion vectors are made against the previous
// frame's camera, which with real stereo is the other eye's. To give each eye's
// DLSS instance its own eye's previous camera the game's copy has to be found.
// Every view vrcam hands the game is kept with its frame number (512 frames);
// a thread then reads the game's memory - the heap and the GPU upload buffers
// mapped into it - for exact copies, straight or transposed, and logs each copy
// with how many frames old it was when read. A place that is always 1 frame
// old is where the previous frame is kept. Turn the head while it runs, so every
// frame's view is its own.
namespace prevscan {
constexpr int kRing = 512;
struct Entry { uint32_t frame; float m[16]; };
SRWLOCK g_lock = SRWLOCK_INIT;
Entry g_ring[kRing];
uint32_t g_count = 0;
std::atomic<uint32_t> g_frame{0};
std::atomic<bool> g_busy{false};
// After the scans: the copies found in the game's own heap, watched in-frame -
// read each time vrcam hands the game a new view, before it is written, and
// logged with how many views old each holds. Write-combined GPU buffers are
// left out: transport, not where the game keeps anything.
constexpr int kWatch = 24;
uintptr_t g_watch[kWatch];
std::atomic<int> g_watchCount{0};
std::atomic<int> g_watchLeft{0};
int g_ages[kWatch][80];   // what the watch saw, per place and call
int g_seen = 0;
// The game's copy of the previous frame's view: the place whose age goes 2, 1, 2, 1
// call by call (vrcam is asked twice a frame, it is written once, at the frame's
// start). 0 = not found.
std::atomic<uintptr_t> g_prevAddr{0};
// The view of each game frame (the last one handed over before the next frame).
float g_frameView[4][16];
uint32_t g_frameViews = 0;
float g_lastView[16];
bool g_lastViewOk = false;
int g_lastEye = 0;
float g_lastProj[16];
bool g_lastProjOk = false;

void PickPrev() {
    for (int i = 0; i < g_watchCount.load(); ++i) {
        int good = 0;
        for (int c = 2; c < g_seen; ++c) {
            const int a = g_ages[i][c], b = g_ages[i][c - 1];
            if ((a == 2 && b == 1) || (a == 1 && b == 2)) ++good;
        }
        if (g_seen > 20 && good >= g_seen - 6) {
            g_prevAddr.store(g_watch[i]);
            LogF("vrcam: the game keeps the previous frame's view at %p (%d of %d calls fit)%s", (void*)g_watch[i], good, g_seen - 2,
                 g_set.dlssPrevFix.load() ? " - [render] dlss_prev_fix: it gets the same eye's, two frames back" : "");
            return;
        }
    }
    Log("vrcam: no place in the game's heap holds the previous frame's view exactly");
}

// Once a game frame (VrFrame): the view the frame was drawn with.
void FrameDone() {
    if (!g_lastViewOk) return;
    if (g_lastProjOk) dlssmv::RecordFrame(g_lastView, g_lastProj, g_lastEye);
    memcpy(g_frameView[g_frameViews % 4], g_lastView, sizeof g_lastView);
    ++g_frameViews;
}

// After the game copied its view into its previous-frame place (at the frame's
// start): the same eye's view two frames back instead, when it holds just what
// the game puts there - the last frame's view - and nothing else.
void FixPrev() {
    const uintptr_t a = g_prevAddr.load();
    if (!a || !g_set.dlssPrevFix.load() || !AfrOn() || (PairWanted() && !NativeByTurns()) || g_frameViews < 3) return;   // native by turns: eyes in turn, as AFR
    const float* last = g_frameView[(g_frameViews - 1) % 4];
    const float* same = g_frameView[(g_frameViews - 2) % 4];
    float now[16];
    if (!SafeRead(now, (const void*)a, sizeof now)) { g_prevAddr.store(0); return; }
    if (memcmp(now, same, sizeof now) == 0) return;   // already ours this frame
    if (memcmp(now, last, sizeof now) != 0) {         // not the game's previous view any more: let go
        static int off = 0;
        if (++off > 30) { g_prevAddr.store(0); off = 0; Log("vrcam: the previous-frame place holds something else now - let go"); }
        return;
    }
    __try { memcpy((void*)a, same, 64); } __except (EXCEPTION_EXECUTE_HANDLER) { g_prevAddr.store(0); }
}

// How many views old the 16 floats at a are, -1 none kept, -2 unreadable.
int AgeAt(uintptr_t a, bool* transposed) {
    float m[16];
    if (!SafeRead(m, (const void*)a, sizeof m)) return -2;
    const uint32_t now = g_frame.load();
    AcquireSRWLockShared(&g_lock);
    int age = -1;
    const uint32_t c = g_count;
    for (int k = 0; k < 64 && k < (int)c && age < 0; ++k) {
        const Entry& e = g_ring[(c - 1 - k) % kRing];
        if (memcmp(m, e.m, sizeof m) == 0) { age = (int)(now - e.frame); *transposed = false; break; }
        bool same = true;
        for (int r = 0; r < 4 && same; ++r)
            for (int q = 0; q < 4 && same; ++q) same = m[r * 4 + q] == e.m[q * 4 + r];
        if (same) { age = (int)(now - e.frame); *transposed = true; }
    }
    ReleaseSRWLockShared(&g_lock);
    return age;
}

// On the render thread, as a view is about to be handed over.
void Watch() {
    if (g_watchLeft.load() <= 0) return;
    const int n = g_watchCount.load();
    char line[512];
    int len = snprintf(line, sizeof line, "vrcam: prev watch (call %u):", g_frame.load() + 1);
    for (int i = 0; i < n && len < (int)sizeof line - 16; ++i) {
        bool tr = false;
        const int age = AgeAt(g_watch[i], &tr);
        len += snprintf(line + len, sizeof line - len, " %d%s", age, tr ? "t" : "");
        if (g_seen < 80) g_ages[i][g_seen] = age;
    }
    Log(line);
    if (g_seen < 80) ++g_seen;
    if (g_watchLeft.fetch_sub(1) == 1) PickPrev();
}

void Record(const float m[16]) {
    const uint32_t f = g_frame.fetch_add(1) + 1;
    AcquireSRWLockExclusive(&g_lock);
    Entry& e = g_ring[g_count % kRing];
    e.frame = f;
    memcpy(e.m, m, sizeof e.m);
    ++g_count;
    ReleaseSRWLockExclusive(&g_lock);
}

struct Hit { uintptr_t addr; int age; bool transposed; uint32_t prot; };

// One region: every 16-byte step whose 16 floats are a kept view (or its transpose).
int ScanRegion(const uint8_t* base, size_t size, const Entry* snap, int n, const uint32_t* first, const uint32_t* firstT,
               const uint8_t* bloom, Hit* out, int cap, uint32_t prot) {
    int got = 0;
    __try {
        for (size_t o = 0; o + 64 <= size && got < cap; o += 16) {
            const uint32_t w = *(const uint32_t*)(base + o);
            if (w == 0 || !bloom[(w * 2654435761u) >> 20]) continue;
            for (int k = 0; k < n && got < cap; ++k) {
                if (w == first[k] && memcmp(base + o, snap[k].m, 64) == 0) {
                    out[got++] = {(uintptr_t)(base + o), (int)(g_frame.load() - snap[k].frame), false, prot};
                    break;
                }
                if (w == firstT[k]) {
                    const float* f = (const float*)(base + o);
                    bool same = true;
                    for (int r = 0; r < 4 && same; ++r)
                        for (int c = 0; c < 4 && same; ++c) same = f[r * 4 + c] == snap[k].m[c * 4 + r];
                    if (same) { out[got++] = {(uintptr_t)(base + o), (int)(g_frame.load() - snap[k].frame), true, prot}; break; }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return got;
}

DWORD WINAPI Thread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    static Entry snap[kRing];
    static uint32_t first[kRing], firstT[kRing];
    static Hit hits[4096];
    static uint8_t bloom[4096];
    for (int pass = 1; pass <= 3; ++pass) {
        const ULONGLONG t0 = GetTickCount64();
        int nh = 0;
        size_t read = 0;
        MEMORY_BASIC_INFORMATION mi{};
        for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && nh < 4096; a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
            // a region going away under the walk is stepped over, not the end of it
            if (!VirtualQuery((void*)a, &mi, sizeof mi)) { mi.BaseAddress = (void*)a; mi.RegionSize = 0x10000; continue; }
            const uint32_t prot = mi.Protect & 0xFF;
            if (mi.State != MEM_COMMIT || (mi.Protect & PAGE_GUARD) || (prot != PAGE_READWRITE && prot != PAGE_WRITECOPY) ||
                mi.RegionSize > (1ull << 31) || (mi.Type != MEM_PRIVATE && mi.Type != MEM_MAPPED))
                continue;
            // the views kept now, the last 64 frames: what a region holds was written lately
            int n = 0;
            AcquireSRWLockShared(&g_lock);
            const uint32_t c = g_count;
            for (int k = 0; k < 64 && k < (int)c; ++k) snap[n++] = g_ring[(c - 1 - k) % kRing];
            ReleaseSRWLockShared(&g_lock);
            memset(bloom, 0, sizeof bloom);
            for (int k = 0; k < n; ++k) {
                memcpy(&first[k], &snap[k].m[0], 4);
                memcpy(&firstT[k], &snap[k].m[0], 4);   // m[0] is on the diagonal: the same either way
                bloom[(first[k] * 2654435761u) >> 20] = 1;
            }
            read += mi.RegionSize;
            nh += ScanRegion((const uint8_t*)mi.BaseAddress, mi.RegionSize, snap, n, first, firstT, bloom, hits + nh, 4096 - nh, mi.Protect);
        }
        LogF("vrcam: prev-matrix scan %d: %.0f MB read in %.1f s, %d copies of vrcam's views found", pass, read / 1048576.0,
             (GetTickCount64() - t0) / 1000.0, nh);
        for (int i = 0; i < nh && i < 200; ++i) {
            MEMORY_BASIC_INFORMATION r{};
            VirtualQuery((void*)hits[i].addr, &r, sizeof r);
            const uintptr_t mod = (uintptr_t)GetModuleHandleW(nullptr);
            LogF("vrcam:   copy at %p (region %p +0x%llX, %s, protect 0x%X%s) - %d frame(s) old%s", (void*)hits[i].addr, r.AllocationBase,
                 (unsigned long long)(hits[i].addr - (uintptr_t)r.AllocationBase), r.Type == MEM_MAPPED ? "mapped" : "private",
                 hits[i].prot, (uintptr_t)r.AllocationBase == mod ? ", the game's image" : "", hits[i].age,
                 hits[i].transposed ? ", transposed" : "");
        }
        // the heap copies of every pass, unique, for the in-frame watch
        static int n = 0;
        if (pass == 1) n = 0;
        for (int i = 0; i < nh && n < kWatch; ++i) {
            if ((hits[i].prot & 0x400) != 0) continue;   // write-combined: GPU transport
            bool dup = false;
            for (int k = 0; k < n && !dup; ++k) dup = g_watch[k] == hits[i].addr;
            if (!dup) g_watch[n++] = hits[i].addr;
        }
        if (pass == 3) {
            g_watchCount.store(n);
            char b[1024];
            int len = snprintf(b, sizeof b, "vrcam: prev watch - %d heap places, in this order:", n);
            for (int k = 0; k < n && len < (int)sizeof b - 24; ++k) len += snprintf(b + len, sizeof b - len, " %p", (void*)g_watch[k]);
            Log(b);
            g_seen = 0;
            g_watchLeft.store(80);
        }
        Sleep(500);
    }
    g_busy.store(false);
    return 0;
}

// Once per switch of [debug] find_prev_matrix to 1.
void Tick() {
    static bool was = false;
    const bool want = g_set.findPrevMatrix.load();
    if (want && !was && !g_busy.exchange(true)) {
        Log("vrcam: prev-matrix scan starting - turn the head slowly for the next few seconds");
        if (HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr)) CloseHandle(h);
        else g_busy.store(false);
    }
    was = want;
}
}  // namespace prevscan

}  // namespace vrc
