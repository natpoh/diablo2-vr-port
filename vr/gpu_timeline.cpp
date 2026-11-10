// gpu_timeline.cpp - [debug] gpu_timeline (vr/gpu_timeline.h): the graphics card's idle gaps within a pair, by the part of the pair that ended them.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "ecl_hook.h"
#include "gpu_timeline.h"

namespace gputimeline {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

constexpr int kSlots = 2048;    // stamps (each its own list, recorded once)
constexpr int kRecs = 1024;     // batches in flight
constexpr int kQueues = 4;
const char* const kPhaseName[kPhases] = {"the game's frame", "XR begin", "the left pass", "the right eye (replay)", "the pair's end"};

std::atomic<bool> g_on{false};
std::atomic<int> g_phase{kGame};
std::mutex g_mu;   // everything below

ID3D12Device* g_dev = nullptr;
ID3D12QueryHeap* g_heap = nullptr;
ID3D12Resource* g_rb = nullptr;
volatile uint64_t* g_rbMap = nullptr;
ID3D12CommandAllocator* g_alloc = nullptr;
ID3D12GraphicsCommandList* g_list[kSlots] = {};
bool g_slotBusy[kSlots] = {};
int g_nextSlot = 0;
bool g_made = false, g_failed = false;

struct Rec { int pre = -1, post = -1; ID3D12CommandQueue* q = nullptr; uint8_t phase = 0; ULONGLONG at = 0; };
Rec g_rec[kRecs];
uint32_t g_head = 0, g_tail = 0;   // (written / read; head - tail in flight)

struct QStat {
    ID3D12CommandQueue* q = nullptr;
    uint64_t hz = 0, lastPost = 0;
    double busyMs = 0, idleMs[kPhases] = {}, longestMs = 0;
    int longestPhase = 0;
    uint32_t batches = 0, gaps[kPhases] = {};
};
QStat g_q[kQueues];
uint32_t g_pairs = 0, g_drops = 0, g_lost = 0;

bool Make(ID3D12CommandQueue* q) {
    if (g_made) return true;
    if (g_failed) return false;
    g_failed = true;
    if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&g_dev))) return false;
    D3D12_QUERY_HEAP_DESC hd{};
    hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    hd.Count = kSlots;
    if (FAILED(g_dev->CreateQueryHeap(&hd, __uuidof(ID3D12QueryHeap), (void**)&g_heap))) return false;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = kSlots * sizeof(uint64_t);
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                              (void**)&g_rb)))
        return false;
    void* p = nullptr;
    if (FAILED(g_rb->Map(0, nullptr, &p)) || !p) return false;
    g_rbMap = (volatile uint64_t*)p;
    for (int i = 0; i < kSlots; ++i) g_rbMap[i] = 0;
    if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_alloc))) return false;
    const ULONGLONG t0 = GetTickCount64();
    for (int i = 0; i < kSlots; ++i) {   // each list: its own timestamp, copied out - recorded once, submitted again and again
        if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_list[i])))
            return false;
        g_list[i]->EndQuery(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)i);
        g_list[i]->ResolveQueryData(g_heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)i, 1, g_rb, (UINT64)i * sizeof(uint64_t));
        if (FAILED(g_list[i]->Close())) return false;
    }
    g_failed = false;
    g_made = true;
    LogF("vrcam: gpu timeline - %d stamp lists made in %llu ms", kSlots, (unsigned long long)(GetTickCount64() - t0));
    return true;
}

int TakeSlot() {
    for (int k = 0; k < 8; ++k) {
        const int s = g_nextSlot;
        g_nextSlot = (g_nextSlot + 1) % kSlots;
        if (!g_slotBusy[s]) { g_slotBusy[s] = true; return s; }
    }
    return -1;
}
void FreeSlot(int s) {
    if (s < 0) return;
    g_rbMap[s] = 0;
    g_slotBusy[s] = false;
}

QStat* StatOf(ID3D12CommandQueue* q) {
    for (QStat& s : g_q)
        if (s.q == q) return &s;
    for (QStat& s : g_q)
        if (!s.q) {
            s = QStat{};
            s.q = q;
            if (FAILED(q->GetTimestampFrequency(&s.hz))) s.hz = 0;
            return &s;
        }
    return nullptr;
}

// The stamps that came back, in submission order (a queue runs its batches in order: a later one never comes back first)
void Drain() {
    const ULONGLONG now = GetTickCount64();
    while (g_tail != g_head) {
        Rec& r = g_rec[g_tail % kRecs];
        const uint64_t pre = g_rbMap[r.pre], post = g_rbMap[r.post];
        if (!pre || !post) {
            if (now - r.at < 2000) break;   // not yet
            ++g_lost;                       // never came back (2 s): let go
        } else if (QStat* s = StatOf(r.q); s && s->hz) {
            const double ms = 1000.0 / (double)s->hz;
            if (s->lastPost && pre > s->lastPost) {
                const double gap = (double)(pre - s->lastPost) * ms;
                s->idleMs[r.phase] += gap;
                ++s->gaps[r.phase];
                if (gap > s->longestMs) { s->longestMs = gap; s->longestPhase = r.phase; }
            }
            const uint64_t from = std::max(pre, s->lastPost);
            if (post > from) s->busyMs += (double)(post - from) * ms;
            if (post > s->lastPost) s->lastPost = post;
            ++s->batches;
        }
        FreeSlot(r.pre);
        FreeSlot(r.post);
        ++g_tail;
    }
}

void Around(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists, eclhook::ExecFn orig) {
    if (!g_on.load(std::memory_order_relaxed) || !q || q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        orig(q, n, lists);
        return;
    }
    std::lock_guard<std::mutex> l(g_mu);   // (held across the call: the records stay in the queue's order)
    if (!Make(q) || g_head - g_tail >= (uint32_t)kRecs) {
        ++g_drops;
        orig(q, n, lists);
        return;
    }
    const int pre = TakeSlot(), post = pre >= 0 ? TakeSlot() : -1;
    if (pre < 0 || post < 0) {
        FreeSlot(pre);
        ++g_drops;
        orig(q, n, lists);
        return;
    }
    ID3D12CommandList* a = g_list[pre];
    orig(q, 1, &a);
    orig(q, n, lists);
    ID3D12CommandList* b = g_list[post];
    orig(q, 1, &b);
    Rec& r = g_rec[g_head % kRecs];
    r.pre = pre;
    r.post = post;
    r.q = q;
    r.phase = (uint8_t)std::clamp(g_phase.load(std::memory_order_relaxed), 0, kPhases - 1);
    r.at = GetTickCount64();
    ++g_head;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

std::atomic<bool> g_want{false};
bool g_hooked = false, g_hookTried = false;   // (the draw thread's)

void Configure(bool on) {
    if (g_want.exchange(on) != on && !on && g_on.exchange(false)) LogF("vrcam: gpu timeline off");
}

void SetPhase(int phase) { g_phase.store(phase, std::memory_order_relaxed); }

void OnPair(ID3D12CommandQueue* gameQueue) {
    if (g_want.load(std::memory_order_relaxed) && !g_on.load(std::memory_order_relaxed) && gameQueue) {
        if (!g_hooked && !g_hookTried) {
            g_hookTried = true;
            g_hooked = eclhook::InstallAround(gameQueue, &Around);
            LogF(g_hooked ? "vrcam: gpu timeline - the direct queues' submissions stamped ([debug] gpu_timeline)"
                          : "vrcam: gpu timeline - the queue's hook could NOT go in: no timeline");
        }
        if (g_hooked) g_on.store(true);
    }
    if (!g_made) return;
    std::lock_guard<std::mutex> l(g_mu);
    ++g_pairs;
    Drain();
}

std::string Line() {
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_made || !g_pairs) return std::string();
    Drain();
    std::string out;
    char b[400];
    const double per = 1.0 / g_pairs;
    for (QStat& s : g_q) {
        if (!s.q || !s.batches) continue;
        double idle = 0;
        for (double v : s.idleMs) idle += v;
        snprintf(b, sizeof b, "%svrcam: gpu timeline, 10 s ([debug] gpu_timeline): queue %p - the card busy %.2f ms a pair, idle %.2f ms a pair (%.1f "
                 "batches a pair) - idle before batches submitted in:", out.empty() ? "" : "\n", (void*)s.q, s.busyMs * per, idle * per, s.batches * per);
        out += b;
        for (int p = 0; p < kPhases; ++p) {
            snprintf(b, sizeof b, " %s %.2f (%.1f gaps)%s", kPhaseName[p], s.idleMs[p] * per, s.gaps[p] * per, p + 1 < kPhases ? "," : "");
            out += b;
        }
        snprintf(b, sizeof b, " | longest gap %.2f ms, in %s", s.longestMs, kPhaseName[s.longestPhase]);
        out += b;
        const ID3D12CommandQueue* keep = s.q;
        const uint64_t hz = s.hz, last = s.lastPost;
        s = QStat{};
        s.q = (ID3D12CommandQueue*)keep;
        s.hz = hz;
        s.lastPost = last;
    }
    if (g_drops || g_lost) {
        snprintf(b, sizeof b, " | batches not stamped %u, stamps never back %u", g_drops, g_lost);
        out += b;
    }
    g_pairs = 0;
    g_drops = g_lost = 0;
    return out;
}

}  // namespace gputimeline
