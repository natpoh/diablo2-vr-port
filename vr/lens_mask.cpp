// lens_mask.cpp - see lens_mask.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "lens_mask.h"
#include "lens_mask_shape.h"
#include "native_d3d12.h"
#include "view_gate.h"
#include "xr.h"

namespace lensmask {
namespace {

// Every rectangle at least this many pixels from a shown one (the picture's pixels; an upscaler's share keeps it at >= 1.5 of its own) - 3 by
// default, [render] lens_mask_margin_px (lens_mask.h Config)
std::atomic<int> g_margin{3};
std::atomic<bool> g_strip{true};   // [render] lens_mask_strip: the never-submitted strip masked too
// Rows a band of rectangles spans (fewer rectangles; the curved edge loses at most a band's slant). A rectangle clear is not free: on an RTX
// 4070 Ti SUPER at 1664 x 1664 (tools/lens_mask_test.cpp, LENS_TIME) 605 rectangles (band 4) took 186 us a clear, 332 (band 8) 55 us, 176 (16) 34 us.
constexpr int kBand = 8;
// Rectangles a ClearDepthStencilView is given at most: one call with 100 of them killed the process on that NVIDIA driver (a fast-fail inside it;
// 64 went through) - 16 a call. (WARP honours only a call's first rectangle: its test sets 1, SetRectsPerCall.)
std::atomic<unsigned> g_perCall{16};

LogFn g_log = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[768];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

std::atomic<bool> g_hooked{false};
std::atomic<bool> g_cfgOn{false}, g_cfgTurnWarp{true};
std::atomic<uint32_t> g_screenW{0}, g_screenH{0};
std::atomic<float> g_shareX{1.0f}, g_shareY{1.0f};
std::atomic<ReplayFn> g_replay{nullptr};

// What the clears use: both eyes' rectangles in the scene target's pixels (its upscaler share applied)
struct Plan {
    uint32_t w = 0, h = 0;                  // the scene target (the eyes' picture)
    float sx = 1.0f, sy = 1.0f;             // the share it was scaled to
    std::vector<D3D12_RECT> rects[2];
};
std::atomic<Plan*> g_plan{nullptr};
struct Dead { Plan* p; uint32_t passes; };
std::vector<Dead> g_dead;   // the draw thread's: freed once no list can still be recording with it

// The inputs the plan was built from (the draw thread's)
struct Inputs {
    uint32_t gen[2] = {};
    float drawn[2][4] = {}, seen[2][4] = {};
    bool seenOk[2] = {};
    uint32_t w = 0, h = 0;
    int margin = 3;
    bool strip = true;
    bool valid = false;
};
Inputs g_in;
std::vector<lensshape::Rect> g_full[2];   // the picture's rectangles before the share (kept: a new share is only scaled)
ULONGLONG g_builtAt = 0;
bool g_toldPlan = false;

// The pass being recorded: the eye (-1: none, or not drawn now)
std::atomic<int> g_eyeNow{-1};

// The 10 s line
std::atomic<uint32_t> g_drawn[2] = {}, g_both{0}, g_noPlan{0}, g_skipReplay{0}, g_otherClears{0}, g_outside{0}, g_noStencil{0};
uint32_t g_passes = 0;
ULONGLONG g_lineAt = 0;
const char* g_state = "OFF";

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void Sweep(bool all) {
    for (size_t i = 0; i < g_dead.size();) {
        if (all || ++g_dead[i].passes > 16) { delete g_dead[i].p; g_dead[i] = g_dead.back(); g_dead.pop_back(); }
        else ++i;
    }
}
void Swap(Plan* p) {
    Plan* old = g_plan.exchange(p, std::memory_order_acq_rel);
    if (old) g_dead.push_back({old, 0});
}

std::vector<D3D12_RECT> ToD3D(const std::vector<lensshape::Rect>& r) {
    std::vector<D3D12_RECT> o(r.size());
    for (size_t i = 0; i < r.size(); ++i) o[i] = {r[i].x0, r[i].y0, r[i].x1, r[i].y1};
    return o;
}
uint64_t Area(const std::vector<D3D12_RECT>& r) {
    uint64_t a = 0;
    for (const D3D12_RECT& q : r) a += (uint64_t)(q.right - q.left) * (uint64_t)(q.bottom - q.top);
    return a;
}

// Both eyes' plan for the picture w x h, from the lenses (drawn / seen) and the meshes; nullptr when neither eye has anything hidden
Plan* Build(const xr::EyeLens lens[2], const xr::VisMask mesh[2], uint32_t w, uint32_t h, float sx, float sy, bool tell) {
    const ULONGLONG t0 = GetTickCount64();
    lensshape::Stats st[2];
    bool any = false;
    for (int e = 0; e < 2; ++e) {
        const xr::EyeLens& l = lens[e];
        const lensshape::Tan drawn{l.drawn[0], l.drawn[1], l.drawn[2], l.drawn[3]};
        const lensshape::Tan seen{l.seen[0], l.seen[1], l.seen[2], l.seen[3]};
        const bool seenCuts = g_strip.load() && l.seenOk && (seen.l > drawn.l + 1e-4f || seen.r < drawn.r - 1e-4f || seen.u < drawn.u - 1e-4f || seen.d > drawn.d + 1e-4f);
        const xr::VisMask& m = mesh[e];
        g_full[e] = lensshape::Plan(m.xy.data(), (uint32_t)(m.xy.size() / 2), m.idx.data(), (uint32_t)m.idx.size(), drawn, seenCuts ? &seen : nullptr, w, h,
                                    g_margin.load(), kBand, &st[e]);
        any = any || !g_full[e].empty();
    }
    if (!any) {
        if (tell) Say("vrcam: lens mask - nothing hidden in either eye (runtime %s: no mesh, and the whole picture is shown): nothing drawn",
                      xr::RuntimeName()[0] ? xr::RuntimeName() : "?");
        return nullptr;
    }
    Plan* p = new Plan;
    p->w = w; p->h = h; p->sx = sx; p->sy = sy;
    for (int e = 0; e < 2; ++e) p->rects[e] = ToD3D(lensshape::Scaled(g_full[e], sx, sy));
    if (tell) {
        const double sp = std::max(1.0, (double)w * sx * (double)h * sy);   // (the target's pixels the scene is drawn into)
        Say("vrcam: lens mask - runtime %s, scene %ux%u (drawn into %.3f x %.3f of it): left eye %u mesh triangles, %.1f%% of its picture hidden%s, "
            "%zu rectangles cover %.1f%%; right eye %u triangles, %.1f%% hidden%s, %zu rectangles cover %.1f%% (a %d-pixel margin kept from every shown "
            "pixel, [render] lens_mask_margin_px; the never-submitted strip %s, lens_mask_strip; built in %llu ms)", xr::RuntimeName()[0] ? xr::RuntimeName() : "?", w, h, sx, sy, st[0].tris, 100.0 * st[0].meshShare,
            mesh[0].idx.empty() ? " (no mesh: only what is not submitted)" : "", p->rects[0].size(), 100.0 * Area(p->rects[0]) / sp, st[1].tris,
            100.0 * st[1].meshShare, mesh[1].idx.empty() ? " (no mesh: only what is not submitted)" : "", p->rects[1].size(),
            100.0 * Area(p->rects[1]) / sp, g_margin.load(), g_strip.load() ? "masked too" : "NOT masked", (unsigned long long)(GetTickCount64() - t0));
    }
    return p;
}

// The plan for this pair (the draw thread): built again when an input moved; false = nothing to draw (said why in `why`)
bool Ready(const char** why) {
    xr::EyeLens lens[2];
    if (!xr::PairLens(0, &lens[0]) || !xr::PairLens(1, &lens[1])) { *why = "on, not drawn now (no first-person pair's eyes)"; return false; }
    if (!lens[0].seenOk || !lens[1].seenOk || lens[0].fwd[0] != 0.0f || lens[0].fwd[1] != 0.0f || lens[1].fwd[0] != 0.0f || lens[1].fwd[1] != 0.0f) {
        *why = "on, not drawn (the displays are canted: the eyes are drawn turned from the runtime's views)";
        return false;
    }
    const uint32_t w = g_screenW.load(), h = g_screenH.load();
    if (!w || !h) { *why = "on, waiting for the picture's size"; return false; }
    xr::VisMask mesh[2];
    xr::VisibilityMask(0, &mesh[0]);
    xr::VisibilityMask(1, &mesh[1]);
    bool same = g_in.valid && g_in.w == w && g_in.h == h && g_in.margin == g_margin.load() && g_in.strip == g_strip.load();
    for (int e = 0; e < 2 && same; ++e) {
        same = g_in.gen[e] == mesh[e].gen && g_in.seenOk[e] == lens[e].seenOk;
        for (int k = 0; k < 4 && same; ++k) same = Near(g_in.drawn[e][k], lens[e].drawn[k]) && Near(g_in.seen[e][k], lens[e].seen[k]);
    }
    const float sx = std::clamp(g_shareX.load(), 0.25f, 1.0f), sy = std::clamp(g_shareY.load(), 0.25f, 1.0f);
    if (!same) {
        // a new plan; never more than one a second (a runtime whose fov moves all the time: no mask meanwhile rather than an old one)
        const ULONGLONG now = GetTickCount64();
        if (g_in.valid && now - g_builtAt < 1000) {
            Swap(nullptr);
            g_in.valid = false;
            *why = "on, the eyes' fov moved: built again in a moment";
            return false;
        }
        Swap(Build(lens, mesh, w, h, sx, sy, !g_toldPlan || g_in.valid));
        g_toldPlan = true;
        g_builtAt = now;
        g_in.valid = true;
        g_in.w = w; g_in.h = h;
        g_in.margin = g_margin.load();
        g_in.strip = g_strip.load();
        for (int e = 0; e < 2; ++e) {
            g_in.gen[e] = mesh[e].gen;
            g_in.seenOk[e] = lens[e].seenOk;
            for (int k = 0; k < 4; ++k) { g_in.drawn[e][k] = lens[e].drawn[k]; g_in.seen[e][k] = lens[e].seen[k]; }
        }
    } else if (Plan* p = g_plan.load()) {
        if (std::fabs(p->sx - sx) > 1e-3f || std::fabs(p->sy - sy) > 1e-3f) {   // the upscaler's share moved: the same rectangles scaled anew
            Plan* q = new Plan;
            q->w = w; q->h = h; q->sx = sx; q->sy = sy;
            for (int e = 0; e < 2; ++e) q->rects[e] = ToD3D(lensshape::Scaled(g_full[e], sx, sy));
            Swap(q);
            Say("vrcam: lens mask - the game draws its scene into %.3f x %.3f of the picture now: the rectangles scaled to it", sx, sy);
        }
    }
    if (!g_plan.load()) { *why = "on, nothing hidden in either eye"; return false; }
    *why = "ON";
    return true;
}

void Line() {
    const ULONGLONG now = GetTickCount64();
    if (!g_lineAt) g_lineAt = now;
    if (now - g_lineAt < 10000) return;
    const double s = (now - g_lineAt) / 1000.0;
    g_lineAt = now;
    const uint32_t l = g_drawn[0].exchange(0), r = g_drawn[1].exchange(0), b = g_both.exchange(0), np = g_noPlan.exchange(0), sr = g_skipReplay.exchange(0),
                   oc = g_otherClears.exchange(0), out = g_outside.exchange(0), ns = g_noStencil.exchange(0);
    const uint32_t passes = g_passes;
    g_passes = 0;
    if (!g_cfgOn.load() && !l && !r && !b) return;   // (off: once said at the switch)
    char extra[200] = "";
    size_t n = 0;
    if (sr) n += snprintf(extra + n, sizeof extra - n, " | NOT masked, a replay without its per-eye predicate: %u", sr);
    if (np || oc || out || ns)
        snprintf(extra + n, sizeof extra - n, " | not masked - no plan %u, a partial or stencil-only clear %u, outside an eye's pass %u, a depth of the "
                 "picture's size without stencil (a shadow map) %u", np, oc, out, ns);
    Say("vrcam: lens mask - %s | %.1f eye passes/s | the scene depth's clears masked: left eye %u, right eye %u, both eyes for the replay (predicated) %u%s",
        g_state, passes / s, l, r, b, extra);
}

// A depth format with a stencil part (the scene's depth-stencil; the game's shadow maps are depth only)
bool HasStencil(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
            return true;
        default:
            return false;
    }
}

// After the game's ClearDepthStencilView, on its list (any of its threads)
void SinkClear(ID3D12GraphicsCommandList* cl, uint64_t dsv, ID3D12Resource* ds, unsigned flags, float depth, uint8_t, unsigned nRects, const tagRECT*) {
    if (!ds || !g_cfgOn.load(std::memory_order_relaxed)) return;
    const uint32_t w = g_screenW.load(std::memory_order_relaxed), h = g_screenH.load(std::memory_order_relaxed);
    const D3D12_RESOURCE_DESC d = ds->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != w || d.Height != h || d.DepthOrArraySize != 1) return;   // not the scene's
    // 2026-11-12, Serg: grass drawn less in one eye ONLY at a 2048 x 2048 render size - the size of the game's 2048 shadow maps (shadows_once.h:
    // "hair, 2048"): their clears matched the size test and got each eye's rectangles at the nearest depth, so each eye's shadows (and the grass
    // lit by them) differed. The scene's depth carries stencil (the grass lighting is a stencil pass; R24G8 here); a shadow map does not.
    if (!HasStencil(d.Format)) { g_noStencil.fetch_add(1, std::memory_order_relaxed); return; }
    const int eye = g_eyeNow.load(std::memory_order_acquire);
    if (eye < 0) { g_outside.fetch_add(1, std::memory_order_relaxed); return; }
    if (!(flags & D3D12_CLEAR_FLAG_DEPTH) || nRects) { g_otherClears.fetch_add(1, std::memory_order_relaxed); return; }
    const Plan* p = g_plan.load(std::memory_order_acquire);
    if (!p || p->w != w || p->h != h) { g_noPlan.fetch_add(1, std::memory_order_relaxed); return; }
    ID3D12Resource* pred = nullptr;
    const ReplayFn rf = g_replay.load(std::memory_order_relaxed);
    const int replay = rf && eye == 0 ? rf(&pred) : 0;
    if (replay == 2 || (replay == 1 && !pred)) { g_skipReplay.fetch_add(1, std::memory_order_relaxed); return; }
    const float nearest = depth < 0.5f ? 1.0f : 0.0f;   // (the game's clear is the far value: reverse-Z clears 0)
    const D3D12_CPU_DESCRIPTOR_HANDLE view{(SIZE_T)dsv};
    const size_t per = std::max(1u, g_perCall.load(std::memory_order_relaxed));
    auto clear = [&](const std::vector<D3D12_RECT>& r) {
        for (size_t at = 0; at < r.size(); at += per)
            cl->ClearDepthStencilView(view, D3D12_CLEAR_FLAG_DEPTH, nearest, 0, (UINT)std::min(per, r.size() - at), r.data() + at);
    };
    natived3d::SetOurs(true);
    if (replay == 1) {   // both eyes, each under the replay's per-eye predicate (skipped while its value is zero)
        if (!p->rects[0].empty()) {
            cl->SetPredication(pred, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
            clear(p->rects[0]);
        }
        if (!p->rects[1].empty()) {
            cl->SetPredication(pred, 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
            clear(p->rects[1]);
        }
        cl->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        g_both.fetch_add(1, std::memory_order_relaxed);
    } else {
        clear(p->rects[eye & 1]);
        g_drawn[eye & 1].fetch_add(1, std::memory_order_relaxed);
    }
    natived3d::SetOurs(false);
}

}  // namespace

void FillSinks(natived3d::Sinks& k) {
    k.clearDsv = &SinkClear;
    g_hooked.store(true);
}
void SetLog(LogFn log) { g_log = log; }
bool Hooked() { return g_hooked.load(); }

void Configure(const Config& c) {
    const bool was = g_cfgOn.exchange(c.on);
    g_cfgTurnWarp.store(c.turnWarp);
    g_margin.store(c.margin < 1 ? 1 : c.margin > 256 ? 256 : c.margin);   // (a change builds the plan again at the next pair: Ready)
    g_strip.store(c.strip);
    if (was != c.on)
        Say(c.on ? "vrcam: lens mask - [render] lens_mask ON: what the lenses never show is left out of the game's depth-tested passes (F4, native OpenXR)"
                 : "vrcam: lens mask - [render] lens_mask OFF: the game's depth as it clears it");
}

void NoteScreen(uint32_t w, uint32_t h, const float share[2]) {
    g_screenW.store(w, std::memory_order_relaxed);
    g_screenH.store(h, std::memory_order_relaxed);
    if (share) {
        g_shareX.store(share[0], std::memory_order_relaxed);
        g_shareY.store(share[1], std::memory_order_relaxed);
    }
}

void SetReplay(ReplayFn fn) { g_replay.store(fn); }
void SetRectsPerCall(unsigned n) { g_perCall.store(std::max(1u, n)); }

void BeginEye(int eye, bool pair, bool byTurns) {
    if (!g_hooked.load()) return;
    Sweep(false);
    if (pair) ++g_passes;
    const char* why = "OFF ([render] lens_mask=0)";
    bool want = false;
    if (!g_cfgOn.load()) {
        if (g_plan.load()) { Swap(nullptr); g_in.valid = false; }
    } else if (!pair || !viewgate::F4()) {
        why = "on, not drawn now (only in F4 with native OpenXR's pair)";
    } else if (byTurns && g_cfgTurnWarp.load()) {
        why = "on, not drawn: eyes by turns with [stereo] turn_warp (the eye made is moved by this depth - the nearest depth would come into view)";
    } else {
        want = Ready(&why);
    }
    g_state = why;
    g_eyeNow.store(want ? (eye & 1) : -1, std::memory_order_release);
    Line();
}

void EndWorld() {
    if (!g_hooked.load()) return;
    g_eyeNow.store(-1, std::memory_order_release);
}

}  // namespace lensmask
