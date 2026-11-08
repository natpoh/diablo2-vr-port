// shadows_once.cpp - [render] shadows_once_a_pair: the lists' recordings followed through natived3d's so* sinks, judged at the capture
// (vr/shadows_once.h says what and why). Self-contained (natived3d + D3D12): vrcam and tools/native_replay_test.cpp link it alike.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

#include "native_d3d12.h"
#include "shadows_once.h"

namespace shadowsonce {
namespace {

LogFn g_log = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[640];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

std::atomic<bool> g_hooked{false};
std::atomic<bool> g_on{false};
// Each switch-on starts a new epoch: a recording begun in another (or while off) is not followed - judged "not followed", replayed.
std::atomic<uint32_t> g_epoch{1};
std::atomic<unsigned> g_eye[4] = {};

struct Rec {
    List list;
    uint32_t epoch = 0;   // the epoch its recording began in (0: never seen begin)
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<const void*, Rec*> g_recs;   // by the list's pointer; never freed (the game's lists are a fixed set; an address used again is a new Begin)
thread_local const void* t_key = nullptr;
thread_local Rec* t_rec = nullptr;

Rec* Find(const void* cl, bool make) {
    if (t_key == cl && t_rec) return t_rec;
    AcquireSRWLockShared(&g_lock);
    const auto it = g_recs.find(cl);
    Rec* r = it != g_recs.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_lock);
    if (!r && make) {
        AcquireSRWLockExclusive(&g_lock);
        Rec*& slot = g_recs[cl];
        if (!slot) slot = new Rec();
        r = slot;
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (r) { t_key = cl; t_rec = r; }
    return r;
}
// The recording being followed now, or nullptr (off, or begun outside this epoch)
Rec* Live(const void* cl) {
    if (!g_on.load(std::memory_order_relaxed)) return nullptr;
    Rec* r = Find(cl, true);
    return r->epoch == g_epoch.load(std::memory_order_relaxed) ? r : nullptr;
}

Eyes CurrentEyes() {
    Eyes e;
    for (int i = 0; i < 4; ++i) e.Add(g_eye[i].load(std::memory_order_relaxed));
    return e;
}
Kind KindOf(ID3D12Resource* r, const Eyes& eyes) {
    if (!r) return Kind::kOther;
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    Tex t;
    t.dimension = (int)d.Dimension;
    t.format = (unsigned)d.Format;
    t.width = d.Width;
    t.height = d.Height;
    t.samples = d.SampleDesc.Count;
    t.flags = (unsigned)d.Flags;
    return Classify(t, eyes);
}

// ---- the sinks (the game's recording threads; one thread records a list at a time)
void SinkReset(ID3D12GraphicsCommandList* cl) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    Rec* r = Find(cl, true);
    r->epoch = g_epoch.load(std::memory_order_relaxed);
    r->list.Begin(true);
}
void SinkTargets(ID3D12GraphicsCommandList* cl, unsigned n, ID3D12Resource* const* rts, ID3D12Resource* ds, bool unknown) {
    Rec* r = Live(cl);
    if (!r) return;
    const Eyes eyes = CurrentEyes();
    Kind k[9];
    int m = 0;
    for (unsigned i = 0; i < n && i < 8; ++i) k[m++] = KindOf(rts[i], eyes);
    if (ds) k[m++] = KindOf(ds, eyes);
    r->list.Bind(k, m, unknown);
}
void SinkWrite(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst) {
    Rec* r = Live(cl);
    if (!r) return;
    r->list.Write(KindOf(dst, CurrentEyes()));
}
void SinkWork(ID3D12GraphicsCommandList* cl, int kind) {
    Rec* r = Live(cl);
    if (!r) return;
    if (kind == 0) r->list.Draw();
    else if (kind == 1) r->list.Dispatch();
    else if (kind == 2) r->list.Indirect();
    else r->list.Other();
}
void SinkBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    Rec* r = Live(cl);
    if (!r) return;
    const Eyes eyes = CurrentEyes();
    for (unsigned i = 0; i < n; ++i) {
        const D3D12_RESOURCE_BARRIER& x = b[i];
        if (x.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            const bool split = (x.Flags & (D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY | D3D12_RESOURCE_BARRIER_FLAG_END_ONLY)) != 0;
            r->list.Transition(x.Transition.pResource, KindOf(x.Transition.pResource, eyes), x.Transition.Subresource, (unsigned)x.Transition.StateBefore,
                               (unsigned)x.Transition.StateAfter, split);
        } else if (x.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            r->list.Uav(KindOf(x.UAV.pResource, eyes), x.UAV.pResource == nullptr);
        } else {
            r->list.Aliasing();
        }
    }
}

// ---- the counts (the capture: the draw thread; the report: the draw thread)
std::atomic<uint32_t> g_pairs{0}, g_lists{0}, g_skipped{0}, g_capLists{0}, g_capDraws{0}, g_skipDraws{0};
std::atomic<uint32_t> g_why[(int)Why::kCount] = {};
std::atomic<uint32_t> g_whyDraws[(int)Why::kCount] = {};

}  // namespace

void FillSinks(natived3d::Sinks& k, LogFn log) {
    g_log = log;
    k.soReset = &SinkReset;
    k.soTargets = &SinkTargets;
    k.soWrite = &SinkWrite;
    k.soWork = &SinkWork;
    k.soBarrier = &SinkBarrier;
    g_hooked.store(true);
}
bool Hooked() { return g_hooked.load(); }

void SetOn(bool on) {
    if (on && !g_hooked.load()) {
        static bool told = false;
        if (!told) {
            told = true;
            LogF("vrcam: shadows once a pair - [render] shadows_once_a_pair=1 but its hooks did not go in at the start (the key off then, or \"Native one "
                 "pass\" off): every list is replayed - from the next start");
        }
    }
    const bool was = g_on.exchange(on && g_hooked.load());
    if (!was && g_on.load()) g_epoch.fetch_add(1);   // lists recorded before are not followed from their start
}
bool On() { return g_on.load(std::memory_order_relaxed); }

void SetEyes(unsigned a, unsigned b, unsigned c, unsigned d) {
    g_eye[0].store(a, std::memory_order_relaxed);
    g_eye[1].store(b, std::memory_order_relaxed);
    g_eye[2].store(c, std::memory_order_relaxed);
    g_eye[3].store(d, std::memory_order_relaxed);
}

bool MapsOnly(ID3D12CommandList* cl) {
    if (!g_on.load(std::memory_order_relaxed) || !cl) return false;
    Rec* r = Find(cl, false);
    const Why w = !r || r->epoch != g_epoch.load(std::memory_order_relaxed) ? Why::kNotFollowed : r->list.Verdict();
    const uint32_t draws = r ? r->list.draws : 0;
    g_capLists.fetch_add(1, std::memory_order_relaxed);
    g_capDraws.fetch_add(draws, std::memory_order_relaxed);
    g_why[(int)w].fetch_add(1, std::memory_order_relaxed);
    g_whyDraws[(int)w].fetch_add(draws, std::memory_order_relaxed);
    if (w == Why::kSkip) g_skipDraws.fetch_add(draws, std::memory_order_relaxed);
    return w == Why::kSkip;
}

void NotePair(uint32_t lists, uint32_t skipped) {
    g_pairs.fetch_add(1, std::memory_order_relaxed);
    g_lists.fetch_add(lists, std::memory_order_relaxed);
    g_skipped.fetch_add(skipped, std::memory_order_relaxed);
}

void Report(double seconds) {
    const uint32_t pairs = g_pairs.exchange(0), lists = g_lists.exchange(0), skipped = g_skipped.exchange(0);
    const uint32_t capLists = g_capLists.exchange(0), capDraws = g_capDraws.exchange(0), skipDraws = g_skipDraws.exchange(0);
    uint32_t why[(int)Why::kCount], whyDraws[(int)Why::kCount];
    for (int i = 0; i < (int)Why::kCount; ++i) { why[i] = g_why[i].exchange(0); whyDraws[i] = g_whyDraws[i].exchange(0); }
    if (!g_hooked.load()) return;
    if (!g_on.load() && !pairs && !capLists) return;
    const double p = pairs ? (double)pairs : 1.0;
    // the kept lists' reasons, the ones that came (lists and their draws a pair)
    char kept[400] = "";
    int at = 0;
    for (int i = 1; i < (int)Why::kCount && at < (int)sizeof kept - 80; ++i)
        if (why[i]) at += snprintf(kept + at, sizeof kept - at, "%s%s %.1f (%.0f draws)", at ? ", " : "", WhyText((Why)i), why[i] / p, whyDraws[i] / p);
    LogF("vrcam: shadows once a pair - %s: %.1f lists a pair left out of the right eye (%.0f draws: the shadow / grass maps), %.1f replayed; of %.1f "
         "lists / %.0f draws captured a pair (%u pairs in %.0f s). Kept: %s",
         g_on.load() ? "on" : "off", skipped / p, skipDraws / p, lists / p, capLists / p, capDraws / p, pairs, seconds, at ? kept : "-");
}

}  // namespace shadowsonce
