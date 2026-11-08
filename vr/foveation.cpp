// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed: 7c642e33d2c8c04043c6f2ac47e7169b54e5693c "Apply foveated rendering to the visible shading pass and add strength"
//   (steamvr/foveation.cpp, foveation.h, vrs_compat.h) - rewritten for D2R VR Authentic's own D3D12 hooks; published before Unleashed moved
//   to the MPL (2026-10-28), so also available under its MIT notice. Kept MPL here, as the other ports. The rest of D2R VR Authentic is MIT.
// foveation.cpp - [render] foveated: see foveation.h. Ported from D2R VR Unleashed's steamvr/foveation.cpp (bearstonem): which draws take
// the rate image and how a list's binding is followed are his (there through ReShade's add-on events, here through the plugin's own D3D12
// hooks); the live image copied per eye before each pass is ours. The tile rule and its numbers: shared/unleashed_foveation.h (MPL 2.0).
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
#include <cstring>
#include <unordered_map>
#include <vector>

#include "foveation.h"
#include "native_d3d12.h"
#include "pso_stream.h"
#include "fullscreen_passes.h"   // [render] foveated_fullscreen, grass_lighting_coarse: the game's heavy full-screen passes by their shader
#include "view_gate.h"
#include "xr.h"
#include "../shared/unleashed_foveation.h"

namespace foveation {
namespace {

namespace uf = unleashed::foveation;
namespace fsp = fullscreenpasses;

LogFn g_log = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[640];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

std::atomic<bool> g_hooked{false};
// the settings (Configure, live)
std::atomic<bool> g_cfgOn{false}, g_cfgUnseen{true}, g_cfgGaze{true};
std::atomic<int> g_cfgStrength{uf::kDefaultStrength}, g_cfgAb{0};
std::atomic<int> g_cfgFull{0};    // [render] foveated_fullscreen: 0 Bear's rule alone, 1 + the grass lighting and the reflections, 2 + the hair
std::atomic<int> g_cfgGrass{0};   // [render] grass_lighting_coarse: 0 as the game, 1 the grass lighting shaded 2x1 everywhere, 2 2x2
std::atomic<float> g_cfgRadius{0.0f};
// the device: VRS tier 2 and its tile (NoteScreen, once)
ID3D12Device* g_dev = nullptr;
std::atomic<bool> g_ready{false};
std::atomic<int> g_vrs{-1};   // VRS tier 2 here: 1 / 0, -1 not asked yet (VrsTier2)
bool g_checked = false;
uint32_t g_tile = 0;
bool g_rates4 = false;   // D3D12_SHADING_RATE_4X4 allowed (AdditionalShadingRatesSupported)
std::atomic<uint32_t> g_screenW{0}, g_screenH{0};

// While an eye's pass of a first-person pair is recorded (BeginEye .. EndWorld): the lists bind the live image where Bear's rule holds.
std::atomic<bool> g_world{false};
// ... and this pass takes the live image (foveated on, a lens or gaze mask copied in; false: only grass_lighting_coarse's rate, or whole
// after a gaze jump), the share of that mask's tiles that are coarse (the full-screen passes' line)
std::atomic<bool> g_fovNow{false};
std::atomic<float> g_slotCoarse{0.0f};
std::atomic<uint32_t> g_gen{1};   // moved: every list looks at its binding again at its next draw

// ---- the game's pipelines: why each one's draws stay whole (0: they may take the image)
SRWLOCK g_psoLock = SRWLOCK_INIT;
// ... and which of the heavy full-screen passes it draws (vr/fullscreen_passes.h; kNone for nearly all)
struct PsoInfo {
    uint8_t reject = uf::kUnknown;
    uint8_t kind = fsp::kNone;
};
std::unordered_map<ID3D12PipelineState*, PsoInfo> g_psos;
// Each recording thread's last pipelines looked up (the game binds ~1,800 a pass): no lock on the way (docs/notes/vr_cpu_overhead.md), thrown
// away whenever a pipeline is kept (the generation moves after the map's write).
std::atomic<uint32_t> g_psoGen{1};
struct PsoCache {
    uint32_t gen = 0;
    ID3D12PipelineState* key[64] = {};
    PsoInfo info[64] = {};
};
thread_local PsoCache t_psoCache;
PsoInfo InfoOf(ID3D12PipelineState* pso) {
    if (!pso) return PsoInfo{};
    PsoCache& c = t_psoCache;
    const uint32_t gen = g_psoGen.load(std::memory_order_acquire);
    if (c.gen != gen) {
        c.gen = gen;
        memset(c.key, 0, sizeof c.key);
    }
    const int i = (int)(((uint64_t)(uintptr_t)pso * 0x9E3779B97F4A7C15ull) >> 58);   // (64 slots)
    if (c.key[i] == pso) return c.info[i];
    AcquireSRWLockShared(&g_psoLock);
    const auto it = g_psos.find(pso);
    const PsoInfo r = it != g_psos.end() ? it->second : PsoInfo{};
    ReleaseSRWLockShared(&g_psoLock);
    c.key[i] = pso;
    c.info[i] = r;
    return r;
}
void Keep(ID3D12PipelineState* pso, uint8_t reject, uint8_t kind) {
    AcquireSRWLockExclusive(&g_psoLock);
    g_psos[pso] = {reject, kind};   // (a pipeline made at the address of one let go replaces it)
    ReleaseSRWLockExclusive(&g_psoLock);
    g_psoGen.fetch_add(1, std::memory_order_release);
}
// tools/foveation_native_test: a pixel shader of its own taken as one of the game's heavy passes (TestKind)
SRWLOCK g_testLock = SRWLOCK_INIT;
std::vector<std::pair<uint64_t, uint8_t>> g_testKinds;
uint8_t KindFor(const void* code, size_t bytes) {
    const uint8_t k = fsp::KindOf(code, bytes);
    if (k || !code || !bytes) return k;
    AcquireSRWLockShared(&g_testLock);
    const bool any = !g_testKinds.empty();
    ReleaseSRWLockShared(&g_testLock);
    if (!any) return fsp::kNone;
    const uint64_t h = fsp::Fnv(code, bytes);
    uint8_t r = fsp::kNone;
    AcquireSRWLockShared(&g_testLock);
    for (const auto& t : g_testKinds)
        if (t.first == h) r = t.second;
    ReleaseSRWLockShared(&g_testLock);
    return r;
}

// ---- the image bound in the game's lists, and both eyes' masks to copy into it
struct Live {
    ID3D12Resource* tex = nullptr;      // R8_UINT, a texel a tile, in SHADING_RATE_SOURCE between our copies
    ID3D12Resource* upload = nullptr;   // the masks, placed footprints: slot 0 / 1 each eye's lens centre; with the bank, 2 + eye * 81 + cell
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp;   //   each eye's 9 x 9 gaze centres (Bear's bank: never rewritten as the eyes move)
    bool bank = false;
    float gazeRadius = 0.0f;
    uint32_t w = 0, h = 0;              // the scene target it is for
    D3D12_VIEWPORT vp{};
    uf::Region region[2];
    uint8_t coarse = 0, unseen = 0;
    uf::MaskCount count[2];             // the lens-centre masks'
    std::vector<float> share;           // each slot's share of coarse tiles
    uint32_t tiles = 0;
    bool fresh = true;                  // still in COPY_DEST (made, never copied into)
};
std::atomic<Live*> g_live{nullptr};
struct Dead {
    Live* l;
    uint64_t fence;
    uint32_t passes;
};
std::vector<Dead> g_dead;   // the draw thread's

bool SameVp(const D3D12_VIEWPORT& a, const D3D12_VIEWPORT& b) {
    return a.TopLeftX == b.TopLeftX && a.TopLeftY == b.TopLeftY && a.Width == b.Width && a.Height == b.Height;
}

// A full-size scene target the lists found the live image not made for (none yet, or another size / viewport): the next eye's image is made
// for it when its draws outnumber the ones that matched.
SRWLOCK g_wantLock = SRWLOCK_INIT;
struct Want {
    uint32_t w = 0, h = 0;
    D3D12_VIEWPORT vp{};
    uint32_t n = 0;
} g_want;
std::atomic<uint32_t> g_matched{0};
void NoteWant(uint32_t w, uint32_t h, const D3D12_VIEWPORT& vp) {
    AcquireSRWLockExclusive(&g_wantLock);
    if (!g_want.n) { g_want.w = w; g_want.h = h; g_want.vp = vp; }
    if (g_want.w == w && g_want.h == h && SameVp(g_want.vp, vp)) ++g_want.n;   // (the first one seen this pass; another waits for the next)
    ReleaseSRWLockExclusive(&g_wantLock);
}

// ---- the statistics (the 10 s line): draws of the game's during eyes, batched per thread as Bear's
enum Why : uint8_t { kNotScene = 0, kCovered, kBlended, kNoDepth, kNoPixel, kUnknownPso, kSamples, kNotMatching, kWhyCount };
std::atomic<uint32_t> g_shaded{0}, g_whole{0}, g_why[kWhyCount] = {};
// the heavy full-screen passes (per fsp::Kind): draws coarse (the rate image or the grass rate), their viewports' pixels, and of those how
// many were shaded coarse (the mask's coarse share of each draw, all of it under grass_lighting_coarse); and their draws left whole
std::atomic<uint32_t> g_fsDraws[4] = {}, g_fsWhole[4] = {};
std::atomic<uint64_t> g_fsPixels[4] = {}, g_fsCoarse[4] = {};
struct Pending {
    uint32_t shaded = 0, whole = 0, why[kWhyCount] = {};
    uint32_t fsDraws[4] = {}, fsWhole[4] = {}, fsAll = 0;
    uint64_t fsPixels[4] = {}, fsCoarse[4] = {};
};
thread_local Pending t_pend;
void Flush(Pending& p) {
    g_shaded.fetch_add(p.shaded, std::memory_order_relaxed);
    g_whole.fetch_add(p.whole, std::memory_order_relaxed);
    for (int i = 0; i < kWhyCount; ++i)
        if (p.why[i]) g_why[i].fetch_add(p.why[i], std::memory_order_relaxed);
    for (int k = 0; k < 4; ++k) {
        if (p.fsDraws[k]) g_fsDraws[k].fetch_add(p.fsDraws[k], std::memory_order_relaxed);
        if (p.fsWhole[k]) g_fsWhole[k].fetch_add(p.fsWhole[k], std::memory_order_relaxed);
        if (p.fsPixels[k]) g_fsPixels[k].fetch_add(p.fsPixels[k], std::memory_order_relaxed);
        if (p.fsCoarse[k]) g_fsCoarse[k].fetch_add(p.fsCoarse[k], std::memory_order_relaxed);
    }
    p = Pending{};
}

// ---- a list of the game's: what it binds (followed between its draws), and the image it has
struct List {
    ID3D12GraphicsCommandList5* l5 = nullptr;   // the same object (QueryInterface once; no reference kept: the game's list holds it)
    bool queried = false;
    uint8_t reject = uf::kUnknown;
    uint8_t kind = fsp::kNone;   // the bound pipeline's heavy pass, if it is one
    ID3D12PipelineState* pso = nullptr;
    uf::Bound b;            // its targets (vp filled at the evaluation)
    D3D12_VIEWPORT vp{};
    ID3D12Resource* applied = nullptr;
    D3D12_SHADING_RATE rate = D3D12_SHADING_RATE_1X1, appliedRate = D3D12_SHADING_RATE_1X1;   // the base rate (grass_lighting_coarse)
    uint8_t why = kNotScene;
    bool dirty = true;
    uint32_t gen = 0;
};
SRWLOCK g_listLock = SRWLOCK_INIT;
std::unordered_map<ID3D12GraphicsCommandList*, List*> g_lists;   // kept: the game's lists live as long as it does (a list at a freed one's
                                                                 // address starts with a Reset or its creation, both told)
thread_local ID3D12GraphicsCommandList* t_cl = nullptr;
thread_local List* t_list = nullptr;
List& Of(ID3D12GraphicsCommandList* cl) {
    if (t_cl == cl && t_list) return *t_list;
    AcquireSRWLockShared(&g_listLock);
    const auto it = g_lists.find(cl);
    List* p = it != g_lists.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_listLock);
    if (!p) {
        AcquireSRWLockExclusive(&g_listLock);
        List*& q = g_lists[cl];
        if (!q) q = new List;
        p = q;
        ReleaseSRWLockExclusive(&g_listLock);
    }
    t_cl = cl;
    t_list = p;
    return *p;
}

// The image (nullptr: none) and the base rate; with both, the coarser of the two per axis (MAX).
void Bind(ID3D12GraphicsCommandList* cl, List& s, ID3D12Resource* image, D3D12_SHADING_RATE rate = D3D12_SHADING_RATE_1X1) {
    if (!s.queried) {
        s.queried = true;
        ID3D12GraphicsCommandList5* l5 = nullptr;
        if (SUCCEEDED(cl->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), (void**)&l5)) && l5) {
            s.l5 = l5;
            l5->Release();
        } else {
            static std::atomic<bool> told{false};
            if (!told.exchange(true)) Say("vrcam: foveated - a list of the game's has no ID3D12GraphicsCommandList5: its draws stay whole");
        }
    }
    if (!s.l5) return;
    const D3D12_SHADING_RATE_COMBINER c[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                                              image ? D3D12_SHADING_RATE_COMBINER_MAX : D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
    s.l5->RSSetShadingRate(rate, c);
    s.l5->RSSetShadingRateImage(image);
    s.applied = image;
    s.appliedRate = rate;
}

uint8_t WhyOf(uint8_t reject) {
    if (reject & uf::kUnknown) return kUnknownPso;
    if (reject & uf::kNoPixel) return kNoPixel;
    if (reject & uf::kNoDepth) return kNoDepth;
    if (reject & uf::kBlended) return kBlended;
    return kSamples;
}

// What the list's next draws get (Bear's Apply): the live image when the pass is an eye's, the pipeline may, the targets are the scene's and
// the image was made for them; else none.
void Evaluate(ID3D12GraphicsCommandList* cl, List& s) {
    s.dirty = false;
    s.gen = g_gen.load(std::memory_order_relaxed);
    ID3D12Resource* image = nullptr;
    D3D12_SHADING_RATE rate = D3D12_SHADING_RATE_1X1;
    uint8_t why = kNotScene;
    if (g_world.load(std::memory_order_relaxed)) {
        uf::Bound b = s.b;
        b.vx = s.vp.TopLeftX; b.vy = s.vp.TopLeftY; b.vw = s.vp.Width; b.vh = s.vp.Height;
        if (uf::Eligible(b, g_screenW.load(std::memory_order_relaxed), g_screenH.load(std::memory_order_relaxed))) {
            // [render] foveated_fullscreen: the heavy passes Bear's rule leaves whole (no depth test, blended) take the image too
            const uint8_t reject = s.kind && fsp::Widened((fsp::Kind)s.kind, g_cfgFull.load(std::memory_order_relaxed)) ? 0 : s.reject;
            // [render] grass_lighting_coarse: the grass lighting shaded 2x1 / 2x2 everywhere (its only permutation: no lighter one exists)
            const int grass = g_cfgGrass.load(std::memory_order_relaxed);
            if (s.kind == fsp::kGrassLighting && grass > 0) rate = grass >= 2 ? D3D12_SHADING_RATE_2X2 : D3D12_SHADING_RATE_2X1;
            if (!g_fovNow.load(std::memory_order_relaxed)) why = kNotScene;
            else if (reject) why = WhyOf(reject);
            else {
                const Live* L = g_live.load(std::memory_order_acquire);
                if (L && L->w == b.w && L->h == b.h && SameVp(L->vp, s.vp)) {
                    image = L->tex;
                    why = kCovered;
                    g_matched.fetch_add(1, std::memory_order_relaxed);
                } else {
                    why = kNotMatching;
                    NoteWant(b.w, b.h, s.vp);
                }
            }
        }
    }
    s.why = why;
    s.rate = rate;
    if (image != s.applied || rate != s.appliedRate) Bind(cl, s, image, rate);
}

// ---- the hooks' sinks (the recording thread of each list)
void SinkGraphicsPso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    if (!d || !pso) return;
    bool opaque = !d->BlendState.AlphaToCoverageEnable;
    const UINT n = d->BlendState.IndependentBlendEnable ? std::min<UINT>(std::max<UINT>(d->NumRenderTargets, 1), 8) : 1;
    for (UINT i = 0; i < n; ++i) opaque = opaque && !d->BlendState.RenderTarget[i].BlendEnable && !d->BlendState.RenderTarget[i].LogicOpEnable;
    Keep(pso, uf::Classify(true, d->PS.pShaderBytecode && d->PS.BytecodeLength, d->DepthStencilState.DepthEnable && d->DSVFormat != DXGI_FORMAT_UNKNOWN,
                           opaque, d->SampleDesc.Count == 1),
         KindFor(d->PS.pShaderBytecode, d->PS.BytecodeLength));
}
void SinkStreamPso(const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    if (!d || !pso) return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    const size_t n = d->SizeInBytes;
    bool known = s && n && n <= 65536, hasPixel = false, depthSeen = false, depth = true, opaque = true, single = true;
    uint8_t kind = fsp::kNone;
    DXGI_FORMAT dsv = DXGI_FORMAT_UNKNOWN;
    for (size_t at = 0; known && at + 4 <= n;) {
        uint32_t t = 0;
        std::memcpy(&t, s + at, 4);
        size_t vo = 0, size = 0;
        if (!psostream::Subobject(t, &vo, &size) || at + size > n) { known = false; break; }
        const uint8_t* v = s + at + vo;
        switch (t) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: {
            D3D12_SHADER_BYTECODE b;
            std::memcpy(&b, v, sizeof b);
            hasPixel = b.pShaderBytecode && b.BytecodeLength;
            kind = KindFor(b.pShaderBytecode, b.BytecodeLength);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: known = false; break;   // (mesh pipelines: not classified)
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: { D3D12_DEPTH_STENCIL_DESC x; std::memcpy(&x, v, sizeof x); depth = x.DepthEnable; depthSeen = true; break; }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: { D3D12_DEPTH_STENCIL_DESC1 x; std::memcpy(&x, v, sizeof x); depth = x.DepthEnable; depthSeen = true; break; }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: std::memcpy(&dsv, v, sizeof dsv); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: { DXGI_SAMPLE_DESC x; std::memcpy(&x, v, sizeof x); single = x.Count == 1; break; }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: {
            D3D12_BLEND_DESC x;
            std::memcpy(&x, v, sizeof x);
            opaque = !x.AlphaToCoverageEnable;
            for (int i = 0; i < (x.IndependentBlendEnable ? 8 : 1); ++i) opaque = opaque && !x.RenderTarget[i].BlendEnable && !x.RenderTarget[i].LogicOpEnable;
            break;
        }
        default: break;
        }
        at += size;
    }
    (void)depthSeen;   // (no depth-stencil subobject: D3D12's default, depth on - but a depth format is needed either way)
    Keep(pso, uf::Classify(known, hasPixel, depth && dsv != DXGI_FORMAT_UNKNOWN, opaque, single), kind);
}
void SinkBound(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    if (s.pso == pso && pso) return;
    s.pso = pso;
    const PsoInfo r = InfoOf(pso);
    if (r.reject != s.reject || r.kind != s.kind) { s.reject = r.reject; s.kind = r.kind; s.dirty = true; }
}
void SinkTargets(ID3D12GraphicsCommandList* cl, ID3D12Resource* colour, ID3D12Resource* depth, unsigned n) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    s.b = uf::Bound{};
    if (g_world.load(std::memory_order_relaxed) && colour && depth && n >= 1 && n <= 8) {   // (described only during an eye's pass)
        const D3D12_RESOURCE_DESC c = colour->GetDesc(), d = depth->GetDesc();
        s.b.w = (uint32_t)c.Width; s.b.h = c.Height;
        s.b.dw = (uint32_t)d.Width; s.b.dh = d.Height;
        s.b.texture2d = c.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        s.b.single = c.SampleDesc.Count == 1 && d.SampleDesc.Count == 1;
    }
    s.dirty = true;
    if (s.applied || s.appliedRate != D3D12_SHADING_RATE_1X1) Evaluate(cl, s);   // taken off at once where no longer wanted, before anything else records on the list
}
void SinkViewport(ID3D12GraphicsCommandList* cl, const D3D12_VIEWPORT* vp) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    if (!SameVp(s.vp, *vp)) { s.vp = *vp; s.dirty = true; }
}
void SinkReset(ID3D12GraphicsCommandList* cl) {   // a new recording: no image, nothing bound (D3D12's state after a Reset)
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    s.reject = uf::kUnknown;
    s.kind = fsp::kNone;
    s.pso = nullptr;
    s.b = uf::Bound{};
    s.vp = D3D12_VIEWPORT{};
    s.applied = nullptr;
    s.rate = s.appliedRate = D3D12_SHADING_RATE_1X1;
    s.why = kNotScene;
    s.dirty = true;
}
void SinkDraw(ID3D12GraphicsCommandList* cl) {
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    if (s.dirty || s.gen != g_gen.load(std::memory_order_relaxed)) Evaluate(cl, s);
    Pending& p = t_pend;
    if (s.kind && g_world.load(std::memory_order_relaxed)) {   // a heavy full-screen pass during an eye
        const bool coarse = s.applied || s.appliedRate != D3D12_SHADING_RATE_1X1;
        if (coarse) {
            const uint64_t px = (uint64_t)std::max(0.0f, s.vp.Width) * (uint64_t)std::max(0.0f, s.vp.Height);
            const float share = s.appliedRate != D3D12_SHADING_RATE_1X1 ? 1.0f : g_slotCoarse.load(std::memory_order_relaxed);
            ++p.fsDraws[s.kind];
            p.fsPixels[s.kind] += px;
            p.fsCoarse[s.kind] += (uint64_t)(px * share);
        } else ++p.fsWhole[s.kind];
        if (++p.fsAll >= 16) Flush(p);
    }
    if (s.why == kNotScene && !s.applied) return;
    if (s.applied) ++p.shaded; else ++p.whole;
    ++p.why[s.why];
    if (p.shaded + p.whole >= 256) Flush(p);
}
void SinkIndirect(ID3D12GraphicsCommandList* cl) {   // (an indirect call's draws are not known: whole, as Bear's)
    if (g_vrs.load(std::memory_order_relaxed) == 0) return;   // (no VRS tier 2 on the game's own device: nothing followed)
    List& s = Of(cl);
    if (s.applied || s.appliedRate != D3D12_SHADING_RATE_1X1) Bind(cl, s, nullptr);
    s.dirty = true;
}

// ---- our copies: the eye's mask into the live image on the game's queue, before its pass
ID3D12CommandAllocator* g_alloc[4] = {};
uint64_t g_allocAt[4] = {};
int g_allocI = 0;
ID3D12GraphicsCommandList* g_copy = nullptr;
ID3D12Fence* g_fence = nullptr;
uint64_t g_fenceV = 0;
HANDLE g_event = nullptr;
bool g_copyFailed = false;

bool MakeCopyList() {
    if (g_copy) return true;
    if (g_copyFailed || !g_dev) return false;
    natived3d::SetOurs(true);
    bool ok = SUCCEEDED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence));
    for (int i = 0; ok && i < 4; ++i)
        ok = SUCCEEDED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_alloc[i]));
    ok = ok && SUCCEEDED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_copy)) &&
         SUCCEEDED(g_copy->Close());
    natived3d::SetOurs(false);
    if (ok) g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ok || !g_event) {
        g_copyFailed = true;
        Say("vrcam: foveated - our copy list could not be made: the picture stays whole");
        return false;
    }
    return true;
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    return b;
}

// Mask `slot` into the live image. False: not copied (no queue, an allocator still busy after 50 ms) - the image keeps what it had.
bool CopyMask(Live& L, size_t slot) {
    ID3D12CommandQueue* q = natived3d::GameQueue();
    if (!q || !MakeCopyList()) return false;
    const int i = g_allocI;
    if (g_fence->GetCompletedValue() < g_allocAt[i]) {
        g_fence->SetEventOnCompletion(g_allocAt[i], g_event);
        if (WaitForSingleObject(g_event, 50) != WAIT_OBJECT_0) return false;
    }
    if (FAILED(g_alloc[i]->Reset()) || FAILED(g_copy->Reset(g_alloc[i], nullptr))) return false;
    natived3d::SetOurs(true);
    const D3D12_RESOURCE_BARRIER in = Tr(L.tex, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!L.fresh) g_copy->ResourceBarrier(1, &in);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = L.tex;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = L.upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = L.fp[slot < L.fp.size() ? slot : 0];
    g_copy->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    const D3D12_RESOURCE_BARRIER out = Tr(L.tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
    g_copy->ResourceBarrier(1, &out);
    const bool closed = SUCCEEDED(g_copy->Close());
    if (!closed) { natived3d::SetOurs(false); return false; }
    ID3D12CommandList* lists[1] = {g_copy};
    q->ExecuteCommandLists(1, lists);   // (still ours: [stereo] native_replay's look at the queue leaves it out of the left pass's lists)
    q->Signal(g_fence, ++g_fenceV);
    natived3d::SetOurs(false);
    g_allocAt[i] = g_fenceV;
    g_allocI = (i + 1) & 3;
    L.fresh = false;
    return true;
}

void Free(Live* l) {
    if (!l) return;
    if (l->tex) l->tex->Release();
    if (l->upload) l->upload->Release();
    delete l;
}
void Retire(Live* l) { if (l) g_dead.push_back({l, g_fenceV, 0}); }
void Sweep(bool all) {
    const uint64_t done = g_fence ? g_fence->GetCompletedValue() : ~0ull;
    for (size_t i = 0; i < g_dead.size();) {
        Dead& d = g_dead[i];
        ++d.passes;
        if (all || (d.passes > 16 && done >= d.fence)) { Free(d.l); g_dead[i] = g_dead.back(); g_dead.pop_back(); }
        else ++i;
    }
}

// The region of mask `slot` (see Live::fp)
uf::Region SlotRegion(const Live& L, size_t slot) {
    if (slot < 2) return L.region[slot];
    const size_t k = slot - 2;
    return uf::GazeRegion(L.region[k / uf::kGazeMasks], (int)(k % uf::kGazeMasks), L.gazeRadius);
}
// The live image for a scene target of w x h and viewport vp, both eyes' regions: made with its upload of the masks - both lens centres, and
// with `bank` each eye's 9 x 9 gaze centres (nullptr: failed).
Live* Make(uint32_t w, uint32_t h, const D3D12_VIEWPORT& vp, const uf::Region r[2], uint8_t coarse, uint8_t unseen, bool bank, float gazeRadius) {
    Live* L = new Live;
    L->w = w; L->h = h; L->vp = vp; L->coarse = coarse; L->unseen = unseen;
    L->region[0] = r[0]; L->region[1] = r[1];
    L->bank = bank;
    L->gazeRadius = gazeRadius;
    const size_t slots = bank ? 2 + 2 * uf::kGazeMasks : 2;
    L->fp.resize(slots);
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = uf::Tiles(w, g_tile);
    d.Height = uf::Tiles(h, g_tile);
    d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8_UINT;
    L->tiles = (uint32_t)d.Width * d.Height;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    natived3d::SetOurs(true);
    bool ok = SUCCEEDED(g_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                       (void**)&L->tex));
    UINT64 bytes = 0;
    g_dev->GetCopyableFootprints(&d, 0, 1, 0, &L->fp[0], nullptr, nullptr, &bytes);
    const UINT64 stride = (bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    for (size_t i = 1; i < slots; ++i) {
        L->fp[i] = L->fp[0];
        L->fp[i].Offset += stride * i;
    }
    D3D12_RESOURCE_DESC u{};
    u.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    u.Width = stride * slots;
    u.Height = u.DepthOrArraySize = u.MipLevels = 1;
    u.SampleDesc.Count = 1;
    u.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ok = ok && SUCCEEDED(g_dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &u, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
                                                        (void**)&L->upload));
    natived3d::SetOurs(false);
    uint8_t* data = nullptr;
    const D3D12_RANGE none{0, 0};
    if (ok && SUCCEEDED(L->upload->Map(0, &none, (void**)&data)) && data) {
        std::memset(data, 0, (size_t)u.Width);
        std::vector<uint8_t> m;
        for (size_t e = 0; e < slots; ++e) {
            const uf::MaskCount n = uf::BuildMask(w, h, g_tile, vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, SlotRegion(*L, e), coarse, unseen, m);
            if (e < 2) L->count[e] = n;
            L->share.push_back(L->tiles ? (float)n.coarse / (float)L->tiles : 0.0f);
            const uint32_t tw = (uint32_t)d.Width;
            for (uint32_t y = 0; y < d.Height; ++y) std::memcpy(data + L->fp[e].Offset + (size_t)y * L->fp[e].Footprint.RowPitch, m.data() + (size_t)y * tw, tw);
        }
        L->upload->Unmap(0, nullptr);
    } else ok = false;
    if (!ok) {
        Free(L);
        Say("vrcam: foveated - the rate image could not be made (%ux%u): the picture stays whole", w, h);
        return nullptr;
    }
    return L;
}

bool Near(const uf::Region& a, const uf::Region& b) {
    auto n = [](float x, float y) { return std::abs(x - y) < 2e-3f; };
    return a.ok == b.ok && n(a.cx, b.cx) && n(a.cy, b.cy) && n(a.ax, b.ax) && n(a.ay, b.ay) && n(a.x0, b.x0) && n(a.x1, b.x1) && n(a.y0, b.y0) && n(a.y1, b.y1);
}

// The settings in force: the periphery's rate, the radius, the unseen part's rate.
struct Shape {
    uint8_t coarse, unseen;
    float radius, gazeRadius;
};
Shape ShapeNow() {
    const uf::Preset p = uf::StrengthPreset(g_cfgStrength.load());
    const float r = g_cfgRadius.load();
    Shape s;
    s.radius = r > 0.0f ? std::clamp(r, 0.30f, 0.85f) : p.radius;
    s.gazeRadius = p.gazeRadius;
    s.coarse = p.rate >= 4 && g_rates4 ? uf::kRate4x4 : uf::kRate2x2;
    s.unseen = g_cfgUnseen.load() ? (g_rates4 ? uf::kRate4x4 : uf::kRate2x2) : 0;
    return s;
}
const char* RateName(uint8_t r) { return r == uf::kRate4x4 ? "4x4" : r == uf::kRate2x2 ? "2x2" : "1x1"; }
const char* kLevel[uf::kStrengthCount] = {"Low", "Medium", "High", "Maximum"};

// ---- the 10 s line and [render] foveated_ab
ULONGLONG g_lineAt = 0, g_abAt = 0;
uint32_t g_passes = 0, g_abPasses = 0;
int g_abPhase = -1;
uint32_t g_nGaze = 0, g_nLens = 0, g_nJump = 0;   // eye passes: on the gaze's cell, on the lens centre, whole after a gaze jump
uf::GazeJump g_jump[2];
double g_abRate[2] = {};   // the last window's eye passes a second: [0] on, [1] off
bool g_toldOn = false;

void Line(const char* state) {
    const ULONGLONG now = GetTickCount64();
    if (!g_lineAt) { g_lineAt = now; return; }
    if (now - g_lineAt < 10000) return;
    const double secs = (now - g_lineAt) / 1000.0;
    g_lineAt = now;
    Flush(t_pend);
    const uint32_t shaded = g_shaded.exchange(0), whole = g_whole.exchange(0);
    uint32_t why[kWhyCount];
    for (int i = 0; i < kWhyCount; ++i) why[i] = g_why[i].exchange(0);
    const uint32_t passes = g_passes;
    g_passes = 0;
    const Shape sh = ShapeNow();
    const Live* L = g_live.load();
    Say("vrcam: foveated - %s; %s: radius %.2f, periphery %s, unseen %s | %.1f eye passes/s | the scene's draws (full-size colour+depth, during eyes): "
        "%u with the rate image, whole: blended %u, no depth test %u, no pixel shader %u, pipeline not known %u, multisample %u, image not made for "
        "that target yet %u (all draws during eyes: %u reduced, %u whole)",
        state, kLevel[std::clamp(g_cfgStrength.load(), 0, uf::kStrengthCount - 1)],
        sh.radius, RateName(sh.coarse), sh.unseen ? RateName(sh.unseen) : "as the periphery", passes / secs, why[kCovered], why[kBlended], why[kNoDepth],
        why[kNoPixel], why[kUnknownPso], why[kSamples], why[kNotMatching], shaded, whole);
    {   // the heavy full-screen passes (foveated_fullscreen, grass_lighting_coarse)
        uint32_t d[4], wh[4];
        uint64_t px[4], co[4];
        for (int k = 0; k < 4; ++k) {
            d[k] = g_fsDraws[k].exchange(0); wh[k] = g_fsWhole[k].exchange(0);
            px[k] = g_fsPixels[k].exchange(0); co[k] = g_fsCoarse[k].exchange(0);
        }
        const int full = g_cfgFull.load(), grass = g_cfgGrass.load();
        if (full || grass || d[1] + d[2] + d[3])
            Say("vrcam: foveated - full-screen passes, 10 s (foveated_fullscreen %d, grass_lighting_coarse %d): grass lighting %u coarse (%.1f of %.1f Mpx "
                "shaded coarse, %.0f%%) %u whole | reflections %u coarse (%.1f of %.1f Mpx, %.0f%%) %u whole | hair %u coarse (%.1f of %.1f Mpx, %.0f%%) %u whole",
                full, grass, d[1], co[1] / 1e6, px[1] / 1e6, px[1] ? 100.0 * co[1] / px[1] : 0.0, wh[1], d[2], co[2] / 1e6, px[2] / 1e6,
                px[2] ? 100.0 * co[2] / px[2] : 0.0, wh[2], d[3], co[3] / 1e6, px[3] / 1e6, px[3] ? 100.0 * co[3] / px[3] : 0.0, wh[3]);
    }
    if (L && L->bank)
        Say("vrcam: foveated - eye-tracked: %u eye passes centred on the gaze, %u on the lens centre (no tracked gaze, or it looked beyond the picture), "
            "%u whole (the gaze came back or jumped)", g_nGaze, g_nLens, g_nJump);
    g_nGaze = g_nLens = g_nJump = 0;
    if (L)
        Say("vrcam: foveated - image %ux%u tiles of %u for the scene %ux%u (viewport %.0fx%.0f at %.0f,%.0f): left eye centre %.3f,%.3f, %.0f%% coarse "
            "(%.0f%% never shown), right eye centre %.3f,%.3f, %.0f%% coarse (%.0f%% never shown)",
            uf::Tiles(L->w, g_tile), uf::Tiles(L->h, g_tile), g_tile, L->w, L->h, L->vp.Width, L->vp.Height, L->vp.TopLeftX, L->vp.TopLeftY,
            L->region[0].cx, L->region[0].cy, 100.0 * L->count[0].coarse / std::max(1u, L->tiles), 100.0 * L->count[0].unseen / std::max(1u, L->tiles),
            L->region[1].cx, L->region[1].cy, 100.0 * L->count[1].coarse / std::max(1u, L->tiles), 100.0 * L->count[1].unseen / std::max(1u, L->tiles));
}

// [render] foveated_ab: on for ab seconds, off for ab seconds, by itself - each window's eye passes a second in the log (frames/s is the
// proof: the GPU's own timestamps under-read). False: this window is off.
bool AbOn() {
    const int ab = g_cfgAb.load();
    if (ab <= 0) { g_abPhase = -1; return true; }
    const ULONGLONG now = GetTickCount64();
    const int phase = (int)((now / 1000 / (ULONGLONG)ab) & 1);
    if (phase != g_abPhase) {
        if (g_abPhase >= 0 && g_abAt && now > g_abAt) {
            const double rate = g_abPasses * 1000.0 / (now - g_abAt);
            g_abRate[g_abPhase] = rate;
            Say("vrcam: foveated A/B - %s for %.1f s: %.1f eye passes/s (last %s window: %.1f)%s", g_abPhase == 0 ? "ON " : "OFF", (now - g_abAt) / 1000.0, rate,
                g_abPhase == 0 ? "OFF" : "ON", g_abRate[g_abPhase ^ 1],
                g_abRate[0] > 0 && g_abRate[1] > 0 ? (g_abRate[0] > g_abRate[1] ? " - ON faster" : " - ON not faster") : "");
        }
        g_abPhase = phase;
        g_abAt = now;
        g_abPasses = 0;
    }
    ++g_abPasses;
    return phase == 0;
}

void Off() {
    if (g_world.exchange(false)) g_gen.fetch_add(1, std::memory_order_relaxed);
    g_fovNow.store(false, std::memory_order_relaxed);
}

}  // namespace

void FillSinks(natived3d::Sinks& k, LogFn log) {
    g_log = log;
    k.vrsGraphicsPso = &SinkGraphicsPso;
    k.vrsStreamPso = &SinkStreamPso;
    k.vrsBound = &SinkBound;
    k.vrsTargets = &SinkTargets;
    k.vrsViewport = &SinkViewport;
    k.vrsReset = &SinkReset;
    k.vrsDraw = &SinkDraw;
    k.vrsIndirect = &SinkIndirect;
    g_hooked.store(true);
}
bool Hooked() { return g_hooked.load(); }

void Configure(const Config& c) {
    const bool was = g_cfgOn.exchange(c.on);
    g_cfgStrength.store(std::clamp(c.strength, 0, uf::kStrengthCount - 1));
    g_cfgRadius.store(std::isfinite(c.radius) ? c.radius : 0.0f);
    g_cfgUnseen.store(c.unseen);
    g_cfgAb.store(std::clamp(c.abSeconds, 0, 600));
    g_cfgGaze.store(c.gaze);
    g_cfgFull.store(std::clamp(c.fullscreen, 0, 2));
    g_cfgGrass.store(std::clamp(c.grassCoarse, 0, 2));
    (void)was;
}
bool Usable() { return g_hooked.load() && g_ready.load() && g_vrs.load() != 0; }
int VrsTier2() { return g_vrs.load(); }
void NoteNoVrs() { g_vrs.store(0); }
void SetLog(LogFn log) { g_log = log; }
void TestKind(uint64_t psHash, int kind) {
    AcquireSRWLockExclusive(&g_testLock);
    g_testKinds.push_back({psHash, (uint8_t)kind});
    ReleaseSRWLockExclusive(&g_testLock);
}

void NoteScreen(ID3D12Device* dev, uint32_t w, uint32_t h) {
    if (dev && g_vrs.load(std::memory_order_relaxed) < 0) {   // asked once, hooked or not ([autotune] says why foveation is no rung on a card without it)
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6{};
        const bool got6 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof o6));
        g_vrs.store(got6 && o6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 ? 1 : 0);
    }
    if (!g_hooked.load()) {
        static bool told = false;
        if (!told && g_cfgOn.load()) { told = true; Say("vrcam: foveated - [render] foveated is on, but it was off at the game's start: from the next start"); }
        return;
    }
    g_screenW.store(w, std::memory_order_relaxed);
    g_screenH.store(h, std::memory_order_relaxed);
    if (g_checked || !dev) return;
    g_checked = true;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o{};
    const bool got = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o, sizeof o));
    const bool tile = o.ShadingRateImageTileSize == 8 || o.ShadingRateImageTileSize == 16 || o.ShadingRateImageTileSize == 32;
    const bool ok = got && o.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 && tile;
    g_vrs.store(ok ? 1 : 0);
    if (ok) {
        dev->AddRef();
        g_dev = dev;
        g_tile = o.ShadingRateImageTileSize;
        g_rates4 = o.AdditionalShadingRatesSupported != FALSE;
        g_ready.store(true);
    }
    Say("vrcam: foveated - variable rate shading %s (tier %d, tile %u, 4x4 %s): %s", ok ? "available" : "NOT available", got ? (int)o.VariableShadingRateTier : -1,
        o.ShadingRateImageTileSize, o.AdditionalShadingRatesSupported ? "yes" : "no (2x2 only)",
        ok ? "the game's opaque world shaded coarser outside each eye's centre in F4" : "the picture stays whole");
}

void BeginEye(int eye, bool pair) {
    if (!g_hooked.load()) return;
    Sweep(false);
    // anything of ours this pass: foveated (the image) or grass_lighting_coarse (a base rate on that one pass), in F4's native pair
    const bool grass = g_cfgGrass.load() > 0;
    const bool want = (g_cfgOn.load() || grass) && g_ready.load() && g_vrs.load(std::memory_order_relaxed) != 0 && pair && viewgate::F4();
    if (pair) ++g_passes;
    const bool ab = want ? AbOn() : true;
    Line(!g_cfgOn.load() && !grass ? "OFF ([render] foveated=0)" : !g_ready.load() || g_vrs.load() == 0 ? "not available (no variable rate shading tier 2)"
         : !pair || !viewgate::F4() ? "on, not drawn now (only in F4 with native OpenXR's pair)" : !ab ? "on, A/B window OFF"
         : !g_cfgOn.load() ? "OFF ([render] foveated=0) - grass_lighting_coarse alone" : "ON");
    if (!want || !ab) { Off(); return; }
    auto worldOn = [](bool fov) {   // the lists look again whenever what this pass takes changes
        const bool w = !g_world.exchange(true), f = g_fovNow.exchange(fov) != fov;
        if (w || f) g_gen.fetch_add(1, std::memory_order_relaxed);
    };
    xr::EyeLens lens[2];
    if (!g_cfgOn.load() || !xr::PairLens(0, &lens[0]) || !xr::PairLens(1, &lens[1])) { worldOn(false); return; }
    if (!g_toldOn) { g_toldOn = true; Say("vrcam: foveated - on in this pair's eyes (F4, native OpenXR)"); }
    // both eyes' regions now (the image holds both masks: the right one copied before the right pass or the replay)
    const Shape sh = ShapeNow();
    uf::Region r[2];
    for (int e = 0; e < 2; ++e) {
        const xr::EyeLens& l = lens[e];
        r[e] = uf::RegionFor({l.drawn[0], l.drawn[1], l.drawn[2], l.drawn[3]}, l.fwd[0], l.fwd[1], {l.seen[0], l.seen[1], l.seen[2], l.seen[3]}, l.seenOk,
                             sh.radius);
    }
    Live* L = g_live.load();
    // a new image only before a left eye (a replay runs the left pass's lists again as the right eye: they name the image they were recorded with)
    if (eye == 0) {
        AcquireSRWLockExclusive(&g_wantLock);
        const Want w = g_want;
        g_want = Want{};
        ReleaseSRWLockExclusive(&g_wantLock);
        const uint32_t matched = g_matched.exchange(0);
        uint32_t tw = L ? L->w : 0, th = L ? L->h : 0;
        D3D12_VIEWPORT vp = L ? L->vp : D3D12_VIEWPORT{};
        if (w.n && (!L || w.n > matched)) { tw = w.w; th = w.h; vp = w.vp; }
            const bool bank = lens[0].gazeAvail && g_cfgGaze.load();   // an eye tracker bound: each eye's 9 x 9 gaze centres too
        const bool changed = tw && (!L || L->w != tw || L->h != th || !SameVp(L->vp, vp) || L->coarse != sh.coarse || L->unseen != sh.unseen ||
                                    L->bank != bank || (bank && L->gazeRadius != sh.gazeRadius) || !Near(L->region[0], r[0]) || !Near(L->region[1], r[1]));
        if (changed) {
            const ULONGLONG t0 = GetTickCount64();
            if (Live* made = Make(tw, th, vp, r, sh.coarse, sh.unseen, bank, sh.gazeRadius)) {
                Retire(L);
                L = made;
                g_live.store(L, std::memory_order_release);
                Say("vrcam: foveated - rate image made for the scene %ux%u (viewport %.0fx%.0f): left centre %.3f,%.3f, right %.3f,%.3f, ellipse %.2f x %.2f "
                    "of the picture, shown part x %.2f-%.2f y %.2f-%.2f (left eye), %s periphery, never-shown part %s; %s (%llu ms)",
                    tw, th, vp.Width, vp.Height, r[0].cx, r[0].cy, r[1].cx, r[1].cy, 2 * r[0].ax, 2 * r[0].ay, r[0].x0, r[0].x1, r[0].y0, r[0].y1,
                    RateName(sh.coarse), sh.unseen ? RateName(sh.unseen) : "as the periphery",
                    bank ? "EYE-TRACKED: each eye's 9 x 9 gaze centres made too (gaze radius + Bear's grid margin)" : "fixed (no eye tracker bound)",
                    (unsigned long long)(GetTickCount64() - t0));
            }
        }
    }
    if (!L) {   // (no scene seen yet: the lists learn it this pass)
        worldOn(true);
        return;
    }
    // which mask this eye gets: its gaze's cell when the eyes are tracked; whole on the pass after the gaze came back or jumped (Bear's
    // rule: a new fixation is never shaded coarsely); the lens centre when no tracked gaze comes or it looks beyond the picture
    size_t slot = (size_t)(eye & 1);
    const xr::EyeLens& me = lens[eye & 1];
    float u = 0, v = 0;
    if (L->bank && me.gazeOk && uf::GazeUV({me.drawn[0], me.drawn[1], me.drawn[2], me.drawn[3]}, me.gaze[0], me.gaze[1], &u, &v)) {
        if (!g_jump[eye & 1].Steady(u, v)) { ++g_nJump; worldOn(false); return; }   // (the grass rate, if asked, still)
        slot = 2 + (size_t)(eye & 1) * uf::kGazeMasks + (size_t)uf::GazeCell(u, v);
        ++g_nGaze;
    } else {
        g_jump[eye & 1].Reset();
        ++g_nLens;
    }
    CopyMask(*L, slot);
    g_slotCoarse.store(slot < L->share.size() ? L->share[slot] : 0.0f, std::memory_order_relaxed);
    worldOn(true);
}

void EndWorld() {
    if (!g_hooked.load()) return;
    Off();
}

}  // namespace foveation
