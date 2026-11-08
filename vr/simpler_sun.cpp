// simpler_sun.cpp - [render] sun_shadow_radius_m / sun_shadow_every, the hooks' side. Why and how: vr/simpler_sun.h and docs/notes/simpler_sun.md.
//
// Three ways in, none of them new hooks:
//   - Filter: vr/head_shadow.cpp's hook on ModelDrawCollect asks it for every model of a shadow pass (the render workers) - true = the model is left
//     out of that pass the game's own way. Only the sun's (an orthographic light) models are ever left out;
//   - OnBarrier: the game's barriers (vr/native_d3d12.h barrier sink, before the real call): at the one that ends the sun map's drawing, a kept
//     frame copies the last drawn map back in, a drawn frame copies it out into our keep - recorded on the game's own list, in its order, as ours
//     (natived3d::SetOurs: the other sinks never see it);
//   - OnPresent: a game frame ends (the sun map the frame drew taken from vr/pipe_hunt's tally, the F4 gate for the next).
// Every read of the game's memory is behind an SEH guard.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "simpler_sun.h"

#include "head_shadow.h"   // SetShadowFilter: the model draw collector's say
#include "native_d3d12.h"  // SetOurs: our copies are not the game's to the other sinks
#include "pipe_hunt.h"     // SunMap: the busiest shadow target of the last frame
#include "view_gate.h"     // F4 only

namespace simplersun {
namespace {

void (*g_log)(const char*) = nullptr;
template <typename... A> void LogF(const char* fmt, A... a) {
    if (!g_log) return;
    char b[700];
    snprintf(b, sizeof b, fmt, a...);
    g_log(b);
}

std::atomic<int> g_mode{kModeGame};
std::atomic<float> g_radiusM{0.0f};
std::atomic<int> g_every{1};
// [render] sun_shadows 0: the sun's map still to be found (its draws let through for a frame: vr/pipe_hunt's tally names it); the frame the sun's
// models were last seen (the sun is in this frame: an outdoor area); the frame its map was last cleared by the game; what the game cleared it to
std::atomic<bool> g_learning{true};
std::atomic<uint64_t> g_sunSeenFrame{0}, g_clearSeenFrame{0};
std::atomic<float> g_gameClear{-1.0f};
bool On() {
    return g_mode.load(std::memory_order_relaxed) != kModeGame || g_every.load(std::memory_order_relaxed) > 1;
}
int EveryNow() {   // (the sun without shadows draws no map: nothing to keep)
    return g_mode.load(std::memory_order_relaxed) == kModeNone ? 1 : g_every.load(std::memory_order_relaxed);
}
std::atomic<bool> g_f4{false};   // the frame now is VR first person (taken at each present)

// the hero (SetHero: vrcam's frame) - read by the render workers for each model
SRWLOCK g_heroLock = SRWLOCK_INIT;
Hero g_hero;
ULONGLONG g_heroAt = 0;
constexpr ULONGLONG kHeroFreshMs = 500;

// the frame's decision, the sun's lights as last drawn, the keep (g_lock)
SRWLOCK g_lock = SRWLOCK_INIT;
Frames g_frames;
std::atomic<uint64_t> g_frameNo{0}, g_decidedNo{0};
std::atomic<int> g_plan{0};   // the decided frame's Plan (fast path for the workers)
struct Light { uintptr_t key = 0; float cull[16] = {}, draw[16] = {}; uint64_t frame = 0; };
constexpr int kLights = 6;
Light g_lights[kLights];      // the sun's views of the last drawn frame (key: the culling matrix's address, the view's own)
thread_local uintptr_t t_notedKey = 0;
thread_local uint64_t t_notedFrame = 0;

// the sun map and our copy of it
std::atomic<ID3D12Resource*> g_sunMap{nullptr};
D3D12_RESOURCE_DESC g_sunDesc{};
unsigned g_sunSubs = 1;
float g_sunSide = 4096.0f;
ID3D12Resource* g_keep = nullptr;      // (g_lock) same description as the sun map; between our copies in COMMON
ID3D12Resource* g_keepOf = nullptr;    // the sun map it was made for (a key, never called)
struct Grave { ID3D12Resource* r; ULONGLONG at; };
std::vector<Grave> g_graves;           // keeps let go, released once the graphics card is surely past them
std::atomic<uint64_t> g_barrierFrame{0};
std::atomic<bool> g_parts{false};      // the game drew into the map again after its end in a frame: nothing kept any more (until the keys change)
uint64_t g_keepBytes = 0;

// the 10 s line's numbers
struct AtomicCounts {
    std::atomic<uint64_t> presents{0}, f4Presents{0}, sunKept{0}, sunCulled{0}, heroKept{0}, otherLights{0}, notRead{0}, heldModels{0};
    std::atomic<uint64_t> framesDrawn{0}, framesHeld{0}, copiesOut{0}, copiesIn{0}, notWhole{0}, keepMade{0}, keepFailed{0}, sunCalls{0}, sunFrames{0};
    std::atomic<uint64_t> heldNoCopy{0}, reopened{0}, noShadow{0}, learning{0}, litClears{0}, gameClears{0}, relearned{0};   // a kept frame whose copy could not go in (its map empty that frame)
    std::atomic<uint64_t> why[(int)Why::kCount] = {};
} g_c;
ULONGLONG g_lineAt = 0;
bool g_toldOff = true;

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

Hero HeroNow() {
    AcquireSRWLockShared(&g_heroLock);
    Hero h = g_hero;
    const ULONGLONG at = g_heroAt;
    ReleaseSRWLockShared(&g_heroLock);
    if (!at || GetTickCount64() - at > kHeroFreshMs) h.ok = false;
    return h;
}

// Two descriptions the same (field by field: the struct has padding after Dimension, a memcmp reads it - the WARP test caught a kept frame left
// without its copy that way)
bool SameDesc(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) {
    return a.Dimension == b.Dimension && a.Alignment == b.Alignment && a.Width == b.Width && a.Height == b.Height &&
           a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels && a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality && a.Layout == b.Layout && a.Flags == b.Flags;
}

unsigned Planes(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return 2;
        default: return 1;
    }
}

// (g_lock held) the frame's decision, made now if not yet: `light` (this view's culling and drawing matrices, the view's key) known or not
Decision DecideLocked(bool lightKnownCall, uintptr_t key, const float* cull, const float* draw) {
    if (g_frames.Has()) return g_frames.now;
    bool known = false, still = false;
    if (lightKnownCall) {
        for (const Light& l : g_lights) {
            if (!l.key || l.key != key) continue;
            known = true;
            const Hero h = HeroNow();
            const float reach = h.ok && h.upm > 0.0f ? 10.0f * h.upm : 60.0f;   // the ground the map covers round the hero, to measure its shift by
            const float at[3] = {h.ok ? h.at[0] : 0.0f, h.ok ? h.at[1] : 0.0f, h.ok ? h.at[2] : 0.0f};
            still = h.ok && Still(l.cull, cull, at, reach, g_sunSide) && Still(l.draw, draw, at, reach, g_sunSide);
            break;
        }
    }
    const bool keep = g_frames.keepValid && g_keep && g_keepOf == g_sunMap.load(std::memory_order_relaxed);
    const Decision d = Decide(g_f4.load(std::memory_order_relaxed), EveryNow(), g_frames.DrawnAgo(), keep, known, still,
                              g_parts.load(std::memory_order_relaxed));
    g_frames.Take(d);
    if (d.plan == Plan::kDraw) g_frames.keepValid = false;   // (the keep is of the map drawn before: valid again once this frame's copy is recorded)
    g_c.why[(int)d.why].fetch_add(1, std::memory_order_relaxed);
    (d.plan == Plan::kDraw ? g_c.framesDrawn : g_c.framesHeld).fetch_add(1, std::memory_order_relaxed);
    g_plan.store((int)d.plan, std::memory_order_relaxed);
    g_decidedNo.store(g_frames.frame, std::memory_order_release);
    return d;
}

// (a drawn frame) the sun's view as it is drawn now: kept to measure the next frames' light by
void NoteLight(uintptr_t key, const float* cull, const float* draw) {
    const uint64_t f = g_frameNo.load(std::memory_order_relaxed);
    if (t_notedKey == key && t_notedFrame == f) return;
    t_notedKey = key;
    t_notedFrame = f;
    AcquireSRWLockExclusive(&g_lock);
    Light* slot = nullptr;
    for (Light& l : g_lights)
        if (l.key == key) { slot = &l; break; }
    if (!slot)
        for (Light& l : g_lights)
            if (!slot || l.frame < slot->frame) slot = &l;   // (a free one has frame 0: the oldest)
    slot->key = key;
    std::memcpy(slot->cull, cull, sizeof slot->cull);
    std::memcpy(slot->draw, draw, sizeof slot->draw);
    slot->frame = f;
    ReleaseSRWLockExclusive(&g_lock);
}

// The model draw collector's say for a shadow pass's model (the render workers): true = left out.
bool Filter(uint32_t entity, uintptr_t transform, uintptr_t cullMatrix, uintptr_t drawMatrix) {
    if (!g_f4.load(std::memory_order_relaxed)) return false;
    float cull[16], draw[16];
    if (!ReadAt(cullMatrix, cull, sizeof cull) || !ReadAt(drawMatrix, draw, sizeof draw)) { g_c.notRead.fetch_add(1, std::memory_order_relaxed); return false; }
    if (!Orthographic(cull) || !Orthographic(draw)) { g_c.otherLights.fetch_add(1, std::memory_order_relaxed); return false; }   // a spot / point light: never
    const int mode = g_mode.load(std::memory_order_relaxed);
    g_sunSeenFrame.store(g_frameNo.load(std::memory_order_relaxed), std::memory_order_relaxed);
    // 0. the sun without shadows: none of its models (but while its map is still to be found)
    if (mode == kModeNone) {
        if (g_learning.load(std::memory_order_relaxed)) { g_c.learning.fetch_add(1, std::memory_order_relaxed); return false; }
        g_c.noShadow.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // 2. a kept frame: none of the sun's models
    if (EveryNow() > 1) {
        const uint64_t f = g_frameNo.load(std::memory_order_relaxed);
        Plan plan;
        if (g_decidedNo.load(std::memory_order_acquire) == f && f) {
            plan = (Plan)g_plan.load(std::memory_order_relaxed);
        } else {
            AcquireSRWLockExclusive(&g_lock);
            plan = DecideLocked(true, cullMatrix, cull, draw).plan;
            ReleaseSRWLockExclusive(&g_lock);
        }
        if (plan == Plan::kHold) { g_c.heldModels.fetch_add(1, std::memory_order_relaxed); return true; }
        NoteLight(cullMatrix, cull, draw);
    }
    // 1. the distance (near casters only)
    if (mode == kModeNear) {
        const float radius = g_radiusM.load(std::memory_order_relaxed);
        float pos[3];
        uint32_t parent = kNoEntity;
        if (!ReadAt(transform + 0x70, pos, sizeof pos) || !ReadAt(transform + 0xC0, &parent, sizeof parent)) {
            g_c.notRead.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const Cast c = SelectMode(mode, false, radius, HeroNow(), entity, parent, pos);
        if (c == Cast::kCull) { g_c.sunCulled.fetch_add(1, std::memory_order_relaxed); return true; }
        (c == Cast::kHero ? g_c.heroKept : g_c.sunKept).fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_c.sunKept.fetch_add(1, std::memory_order_relaxed);
    return false;
}

// (g_lock held) our copy of the sun map, made for its description now (nullptr: could not be made)
ID3D12Resource* KeepLocked(ID3D12GraphicsCommandList* cl, ID3D12Resource* map) {
    const D3D12_RESOURCE_DESC d = map->GetDesc();
    if (g_keep && g_keepOf == map && SameDesc(d, g_sunDesc)) return g_keep;
    if (g_keep) { g_graves.push_back({g_keep, GetTickCount64()}); g_keep = nullptr; g_keepOf = nullptr; }
    ID3D12Device* dev = nullptr;
    if (FAILED(cl->GetDevice(IID_PPV_ARGS(&dev))) || !dev) { g_c.keepFailed.fetch_add(1); return nullptr; }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* r = nullptr;
    const HRESULT hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
    const D3D12_RESOURCE_ALLOCATION_INFO ai = dev->GetResourceAllocationInfo(0, 1, &d);
    dev->Release();
    if (FAILED(hr) || !r) {
        g_c.keepFailed.fetch_add(1);
        LogF("vrcam: simpler sun - our copy of the sun's shadow map (%llux%u, format %d) could NOT be made (0x%08lX): every frame draws it",
             (unsigned long long)d.Width, d.Height, (int)d.Format, (unsigned long)hr);
        return nullptr;
    }
    r->SetName(L"D2R VR sun shadow keep");
    g_keep = r;
    g_keepOf = map;
    g_sunDesc = d;
    g_sunSubs = (unsigned)d.MipLevels * d.DepthOrArraySize * Planes(d.Format);
    g_sunSide = (float)d.Width;
    g_keepBytes = ai.SizeInBytes;
    g_frames.keepValid = false;
    g_c.keepMade.fetch_add(1);
    LogF("vrcam: simpler sun - our copy of the sun's shadow map made: %llux%u, %u slice(s), format %d, %.0f MB of graphics memory",
         (unsigned long long)d.Width, d.Height, (unsigned)d.DepthOrArraySize, (int)d.Format, ai.SizeInBytes / 1048576.0);
    return r;
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, unsigned before, unsigned after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = (D3D12_RESOURCE_STATES)before;
    b.Transition.StateAfter = (D3D12_RESOURCE_STATES)after;
    return b;
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void Configure(int mode, float radiusM, int every) {
    mode = std::clamp(mode, 0, 2);
    radiusM = radiusM > 0.0f && std::isfinite(radiusM) ? std::min(radiusM, 500.0f) : 0.0f;
    every = std::clamp(every, 1, 8);
    const int wasM = g_mode.exchange(mode);
    const float wasR = g_radiusM.exchange(radiusM);
    const int wasE = g_every.exchange(every);
    const bool on = On();
    if (wasM != mode && mode == kModeNone && !g_sunMap.load()) g_learning.store(true);
    pipehunt::SetSunWatch(on);
    headshadow::SetShadowFilter(on ? &Filter : nullptr);
    if (wasE != every || wasM != mode) {
        AcquireSRWLockExclusive(&g_lock);
        g_frames.Reset();
        g_parts.store(false);
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (wasM != mode || wasR != radiusM || wasE != every)
        LogF("vrcam: simpler sun - [render] sun_shadows %d (%s), sun_shadow_radius_m %.1f, sun_shadow_every %d (%s); VR first person only, the "
             "game's own sun map and lighting, visual only", mode,
             mode == kModeNone ? "the sun without shadows: no caster drawn, its map cleared to lit" : mode == kModeNear ? "near casters only" : "as the game",
             mode == kModeNear ? (radiusM > 0.0f ? radiusM : kNearDefaultM) : radiusM, every,
             mode == kModeNone ? "not used: no map drawn" : every > 1 ? "the map kept between, while the sun's camera stands still" : "drawn every frame");
}

bool WantsHero() { return g_mode.load(std::memory_order_relaxed) == kModeNear || EveryNow() > 1; }

void SetHero(const float at[3], float upm, uint32_t entity) {
    if (!at || !std::isfinite(at[0] + at[1] + at[2])) return;
    AcquireSRWLockExclusive(&g_heroLock);
    g_hero.ok = true;
    for (int k = 0; k < 3; ++k) g_hero.at[k] = at[k];
    g_hero.upm = upm > 0.0f && std::isfinite(upm) ? upm : 0.0f;
    g_hero.entity = entity;
    g_heroAt = GetTickCount64();
    ReleaseSRWLockExclusive(&g_heroLock);
}

void OnPresent() {
    if (!On()) {
        g_f4.store(false, std::memory_order_relaxed);
        return;
    }
    const bool f4 = viewgate::F4();
    g_c.presents.fetch_add(1, std::memory_order_relaxed);
    if (f4) g_c.f4Presents.fetch_add(1, std::memory_order_relaxed);
    uint32_t calls = 0, shadow = 0;
    ID3D12Resource* picked = pipehunt::SunMap(&calls, &shadow);
    AcquireSRWLockExclusive(&g_lock);
    // the sun map this frame drew (the pick of a kept frame may be another light's target: not taken)
    const bool learning = g_learning.load(std::memory_order_relaxed);
    const bool drew = (!g_frames.Has() || g_frames.now.plan == Plan::kDraw) && (g_mode.load(std::memory_order_relaxed) != kModeNone || learning);
    const uint64_t ending = g_frames.frame;
    const bool sunThisFrame = ending && g_sunSeenFrame.load(std::memory_order_relaxed) == ending;
    if (g_mode.load(std::memory_order_relaxed) == kModeNone && g_f4.load(std::memory_order_relaxed)) {   // the sun without shadows: its map
        if (learning) {
            if (sunThisFrame && picked && calls) {   // (the sun's models went in this frame: the busiest shadow target is its map)
                g_sunMap.store(picked, std::memory_order_relaxed);
                g_learning.store(false, std::memory_order_relaxed);
                static bool told = false;   // (once: each later find is counted in the 10 s line, "looked for again")
                if (!told && (told = true))
                    LogF("vrcam: simpler sun - the sun's shadow map found (%u draws in it that frame): from now on no caster is drawn into it, and it "
                     "is cleared to lit right after the game's clear", calls);
            }
        } else if (sunThisFrame && g_clearSeenFrame.load(std::memory_order_relaxed) != ending) {
            g_learning.store(true, std::memory_order_relaxed);   // the sun is here but its map was not cleared: another map now (an area, a resize)
            g_c.relearned.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (drew && picked && calls) {
        if (picked != g_sunMap.load(std::memory_order_relaxed)) {
            g_sunMap.store(picked, std::memory_order_relaxed);
            g_frames.keepValid = false;
        }
        g_c.sunCalls.fetch_add(calls, std::memory_order_relaxed);
        g_c.sunFrames.fetch_add(1, std::memory_order_relaxed);
    }
    const ULONGLONG now = GetTickCount64();
    for (size_t i = 0; i < g_graves.size();) {   // keeps let go 3 s ago: the graphics card is long past them
        if (now - g_graves[i].at > 3000) { g_graves[i].r->Release(); g_graves[i] = g_graves.back(); g_graves.pop_back(); }
        else ++i;
    }
    g_frames.Next();
    g_frameNo.store(g_frames.frame, std::memory_order_relaxed);
    g_f4.store(f4, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_lock);
}

void OnClearDsv(ID3D12GraphicsCommandList* cl, uint64_t dsv, ID3D12Resource* ds, unsigned flags, float depth, uint8_t stencil, unsigned n,
                const tagRECT* rects) {
    (void)stencil;
    if (!ds || !cl || !On()) return;
    if (ds != g_sunMap.load(std::memory_order_relaxed) || !(flags & D3D12_CLEAR_FLAG_DEPTH)) return;
    g_clearSeenFrame.store(g_frameNo.load(std::memory_order_relaxed), std::memory_order_relaxed);
    g_gameClear.store(depth, std::memory_order_relaxed);
    g_c.gameClears.fetch_add(1, std::memory_order_relaxed);
    if (!g_f4.load(std::memory_order_relaxed) ||
        !LitClearNeeded(g_mode.load(std::memory_order_relaxed), g_learning.load(std::memory_order_relaxed), true, flags, depth))
        return;
    natived3d::SetOurs(true);   // right behind the game's clear, the same view and rectangles: the depth only, to the lit value
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    h.ptr = (SIZE_T)dsv;
    cl->ClearDepthStencilView(h, D3D12_CLEAR_FLAG_DEPTH, kLitDepth, 0, n, reinterpret_cast<const D3D12_RECT*>(rects));
    natived3d::SetOurs(false);
    g_c.litClears.fetch_add(1, std::memory_order_relaxed);
}

void OnBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (EveryNow() <= 1 || !g_f4.load(std::memory_order_relaxed) || !cl || !b || !n) return;
    ID3D12Resource* map = g_sunMap.load(std::memory_order_relaxed);
    if (!map) return;
    int idx = -1;
    unsigned before = 0;
    bool whole = false;
    for (unsigned i = 0; i < n && idx < 0; ++i) {   // (the pure rule on the game's own barrier structs, one at a time)
        if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || b[i].Transition.pResource != map) continue;
        const Transition t{0, (unsigned)b[i].Flags, b[i].Transition.pResource, b[i].Transition.Subresource, (unsigned)b[i].Transition.StateBefore,
                           (unsigned)b[i].Transition.StateAfter};
        if (MapDone(&t, 1, map, g_sunSubs, &whole) == 0) { idx = (int)i; before = t.before; }
    }
    const uint64_t f = g_frameNo.load(std::memory_order_relaxed);
    if (idx < 0) {
        // after our copy this frame: the map taken back into depth-write = drawn in parts (another slice, a second clear) - stop keeping
        if (g_barrierFrame.load(std::memory_order_relaxed) == f && !g_parts.load(std::memory_order_relaxed)) {
            for (unsigned i = 0; i < n; ++i) {
                if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || b[i].Transition.pResource != map) continue;
                const Transition t{0, (unsigned)b[i].Flags, b[i].Transition.pResource, b[i].Transition.Subresource, (unsigned)b[i].Transition.StateBefore,
                                   (unsigned)b[i].Transition.StateAfter};
                if (!Reopens(&t, 1, map)) continue;
                if (!g_parts.exchange(true)) {
                    AcquireSRWLockExclusive(&g_lock);
                    g_frames.keepValid = false;
                    ReleaseSRWLockExclusive(&g_lock);
                    g_c.reopened.fetch_add(1, std::memory_order_relaxed);
                    LogF("vrcam: simpler sun - the game drew into its sun map again after the barrier that ended it, in the same frame (drawn in "
                         "parts): nothing is kept any more - every frame draws it (sun_shadow_every acts as 1 until a key changes)");
                }
                break;
            }
        }
        return;
    }
    uint64_t was = g_barrierFrame.load(std::memory_order_relaxed);
    if (was == f || !g_barrierFrame.compare_exchange_strong(was, f)) return;   // once a frame: the first barrier that ends the map
    AcquireSRWLockExclusive(&g_lock);
    const Decision d = DecideLocked(false, 0, nullptr, nullptr);   // (no sun model collected yet this frame: it draws)
    if (!whole) {
        g_c.notWhole.fetch_add(1, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    ID3D12Resource* keep = nullptr;
    if (d.plan == Plan::kHold) {   // a kept frame copies in only the keep it decided on - never one made now (it would hold nothing)
        const D3D12_RESOURCE_DESC now = map->GetDesc();
        if (g_keep && g_keepOf == map && SameDesc(now, g_sunDesc)) keep = g_keep;
    } else {
        keep = KeepLocked(cl, map);
    }
    if (!keep) {
        if (d.plan == Plan::kHold) {   // (should not happen: a kept frame is decided only with its keep) - this frame's map stays empty; the next draw
            g_frames.keepValid = false;
            g_c.heldNoCopy.fetch_add(1, std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    natived3d::SetOurs(true);
    if (d.plan == Plan::kDraw) {   // drawn: our copy of it, for the frames that keep it
        const D3D12_RESOURCE_BARRIER in[2] = {Tr(map, before, D3D12_RESOURCE_STATE_COPY_SOURCE), Tr(keep, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)};
        cl->ResourceBarrier(2, in);
        cl->CopyResource(keep, map);
        const D3D12_RESOURCE_BARRIER out[2] = {Tr(map, D3D12_RESOURCE_STATE_COPY_SOURCE, before), Tr(keep, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON)};
        cl->ResourceBarrier(2, out);
        g_frames.keepValid = true;
        g_c.copiesOut.fetch_add(1, std::memory_order_relaxed);
    } else {   // kept: the last drawn map back in place of the empty one (cleared, no model drawn into it)
        const D3D12_RESOURCE_BARRIER in[2] = {Tr(map, before, D3D12_RESOURCE_STATE_COPY_DEST), Tr(keep, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)};
        cl->ResourceBarrier(2, in);
        cl->CopyResource(map, keep);
        const D3D12_RESOURCE_BARRIER out[2] = {Tr(map, D3D12_RESOURCE_STATE_COPY_DEST, before), Tr(keep, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)};
        cl->ResourceBarrier(2, out);
        g_c.copiesIn.fetch_add(1, std::memory_order_relaxed);
    }
    natived3d::SetOurs(false);
    ReleaseSRWLockExclusive(&g_lock);
}

std::string Line() {
    const ULONGLONG now = GetTickCount64();
    const double secs = g_lineAt && now > g_lineAt ? (now - g_lineAt) / 1000.0 : 10.0;
    g_lineAt = now;
    Counts c;
    c.presents = g_c.presents.exchange(0);
    c.f4Presents = g_c.f4Presents.exchange(0);
    c.sunKept = g_c.sunKept.exchange(0);
    c.sunCulled = g_c.sunCulled.exchange(0);
    c.heroKept = g_c.heroKept.exchange(0);
    c.otherLights = g_c.otherLights.exchange(0);
    c.notRead = g_c.notRead.exchange(0);
    c.heldModels = g_c.heldModels.exchange(0);
    c.framesDrawn = g_c.framesDrawn.exchange(0);
    c.framesHeld = g_c.framesHeld.exchange(0);
    c.copiesOut = g_c.copiesOut.exchange(0);
    c.copiesIn = g_c.copiesIn.exchange(0);
    c.notWhole = g_c.notWhole.exchange(0);
    for (int i = 0; i < (int)Why::kCount; ++i) c.why[i] = g_c.why[i].exchange(0);
    c.noShadow = g_c.noShadow.exchange(0);
    c.learning = g_c.learning.exchange(0);
    c.litClears = g_c.litClears.exchange(0);
    c.gameClears = g_c.gameClears.exchange(0);
    const uint64_t relearned = g_c.relearned.exchange(0);
    const uint64_t sunCalls = g_c.sunCalls.exchange(0), sunFrames = g_c.sunFrames.exchange(0), heldNoCopy = g_c.heldNoCopy.exchange(0),
                   reopened = g_c.reopened.exchange(0);
    const int mode = g_mode.load();
    const float radius = mode == kModeNear ? (g_radiusM.load() > 0.0f ? g_radiusM.load() : kNearDefaultM) : 0.0f;
    const int every = EveryNow();
    if (!On()) {
        if (g_toldOff) return std::string();
        g_toldOff = true;
        return "vrcam: simpler sun, 10 s: off (sun_shadows 2, sun_shadow_every 1) - the game's sun map as it draws it";
    }
    g_toldOff = false;
    const double per = c.presents ? 1.0 / (double)c.presents : 0.0;
    char b[900];
    snprintf(b, sizeof b,
             "vrcam: simpler sun, 10 s (sun_shadows %d %s, radius %.1f m, every %d; %llu presents in %.0f s, %llu F4): models a present into the sun's "
             "map - kept %.0f, LEFT OUT by the radius %.0f, LEFT OUT (no sun shadows) %.0f, let through to find the map %.0f, the hero's %.0f, not "
             "collected on kept frames %.0f; other lights' %.0f (never touched), not read %llu | the sun map's draw calls a drawn frame %.0f | frames: "
             "drawn %llu, KEPT %llu",
             mode, mode == kModeNone ? "(none)" : mode == kModeNear ? "(near)" : "(as the game)", radius, every, (unsigned long long)c.presents, secs,
             (unsigned long long)c.f4Presents, c.sunKept * per, c.sunCulled * per, c.noShadow * per, c.learning * per, c.heroKept * per,
             c.heldModels * per, c.otherLights * per, (unsigned long long)c.notRead, sunFrames ? (double)sunCalls / (double)sunFrames : 0.0,
             (unsigned long long)c.framesDrawn, (unsigned long long)c.framesHeld);
    std::string out = b;
    {   // what the game clears its sun map to (its reversed depth: 0 = nothing in the way), and our lit clears behind it
        const float gc = g_gameClear.load();
        if (gc >= 0.0f) snprintf(b, sizeof b, " | the game clears its sun map to %.3f (%llu clears)", gc, (unsigned long long)c.gameClears);
        else snprintf(b, sizeof b, " | the game's clear of its sun map not seen yet");
        out += b;
        if (mode == kModeNone) {
            snprintf(b, sizeof b, "; cleared to lit (%.1f) by us %llu%s; its map looked for again %llu", kLitDepth, (unsigned long long)c.litClears,
                     g_learning.load() ? " - its map being found" : gc == kLitDepth ? " (not needed: the game's own clear is lit)" : "",
                     (unsigned long long)relearned);
            out += b;
        }
    }
    if (every > 1) {
        out += " - drawn because:";
        bool any = false;
        for (int i = 1; i < (int)Why::kCount; ++i) {
            if (!c.why[i]) continue;
            snprintf(b, sizeof b, "%s %s %llu", any ? "," : "", WhyText((Why)i), (unsigned long long)c.why[i]);
            out += b;
            any = true;
        }
        if (!any) out += " -";
        AcquireSRWLockShared(&g_lock);
        const uint64_t bytes = g_keep ? g_keepBytes : 0;
        ReleaseSRWLockShared(&g_lock);
        snprintf(b, sizeof b, " | copies: out %llu, in %llu; a barrier on one slice only (no copy) %llu; kept WITHOUT a copy %llu; drawn in parts %s; "
                 "our copy %.0f MB", (unsigned long long)c.copiesOut, (unsigned long long)c.copiesIn, (unsigned long long)c.notWhole,
                 (unsigned long long)heldNoCopy, reopened ? "SEEN NOW" : g_parts.load() ? "yes (nothing kept)" : "no", bytes / 1048576.0);
        out += b;
    }
    return out;
}

}  // namespace simplersun
