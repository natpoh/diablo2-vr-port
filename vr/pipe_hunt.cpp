// pipe_hunt.cpp - see pipe_hunt.h.

#include "pipe_hunt.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "native_d3d12.h"   // natived3d::RtvResource: a render target's resource by its view
#include "pso_stream.h"
#include "fog_in_frame_math.h"   // fogframe::IsResolve: the game's ps_transparent_resolve (the trace's pass-order line)
#include "render_bench.h"   // [render] bench_skip_*: the game's stages by their shaders' hashes
#include "view_gate.h"     // ... left out only in VR first person (F4)

namespace pipehunt {
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

uint64_t Fnv(const void* p, size_t n) {   // (vr/uitrace.cpp's: the same "ps" numbers)
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}
uint64_t HashOf(const D3D12_SHADER_BYTECODE& b) { return b.pShaderBytecode && b.BytecodeLength ? Fnv(b.pShaderBytecode, b.BytecodeLength) : 0; }

// stage: [render] bench_skip_* (vr/render_bench.h). The trace's pass order (Serg 2026-11-09, "огонь туман перекрывает": does the game's fog
// apply come after the flames, and what depth do they leave it?): resolve = the game's transparent resolve; zw = the depth written (1), not
// (0), - a compute pipeline (-1); blend = a render target blended (the effects: additive / alpha, no depth written)
struct Info { uint64_t hash = 0, vs = 0; char kind = 'g'; uint8_t stage = 0; bool resolve = false; int8_t zw = -1; bool blend = false; };
int8_t DepthWrites(const D3D12_DEPTH_STENCIL_DESC& d) { return d.DepthEnable && d.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL ? 1 : 0; }
bool Blends(const D3D12_BLEND_DESC& b) {
    for (int i = 0; i < (b.IndependentBlendEnable ? 8 : 1); ++i)
        if (b.RenderTarget[i].BlendEnable) return true;
    return false;
}
SRWLOCK g_infoLock = SRWLOCK_INIT;
std::unordered_map<ID3D12PipelineState*, Info> g_info;
// [render] bench_skip_* (vr/render_bench.h): the pipelines of each stage the game made (recognised by their hashes)
std::atomic<uint32_t> g_benchMade[renderbench::kStages] = {};
void Keep(ID3D12PipelineState* pso, Info i) {
    i.stage = renderbench::Classify(i.hash, i.vs, i.kind == 'c');
    if (i.stage) g_benchMade[i.stage].fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_infoLock);
    g_info[pso] = i;
    ReleaseSRWLockExclusive(&g_infoLock);
}
bool InfoOf(ID3D12PipelineState* pso, Info* out) {
    AcquireSRWLockShared(&g_infoLock);
    const auto it = g_info.find(pso);
    const bool ok = it != g_info.end();
    if (ok) *out = it->second;
    ReleaseSRWLockShared(&g_infoLock);
    return ok;
}

constexpr int kShaders = 8;
std::atomic<uint64_t> g_skipLegacy{0}, g_skipHunt{0};
std::atomic<uint64_t> g_skipShader[kShaders] = {};
std::atomic<int> g_shaders{0};
std::atomic<bool> g_any{false};       // something is skipped or recorded: binds and draws are looked at
std::atomic<int> g_huntK{0};
std::atomic<bool> g_hunting{false};
std::atomic<uint32_t> g_skipped{0}, g_skippedShown{0};
std::atomic<ULONGLONG> g_skippedAt{0};

// The trace: armed -> recording (from a present) -> written (at the next).
enum : int { kIdle = 0, kArmed, kRecording };
std::atomic<int> g_trace{kIdle};
std::mutex g_recLock;
std::wstring g_recPath;
struct Rec {
    uint64_t hash = 0, vs = 0; char kind = 'g'; uint32_t order = 0, binds = 0, draws = 0, dispatches = 0, indirect = 0;
    std::string targets;   // what its first draw drew into (render targets' formats and sizes, depth, viewport)
    uint8_t stage = 0; bool resolve = false; int8_t zw = -1; bool blend = false;   // (the first pipeline bound with this hash)
    bool mixed = false;           // pipelines of this hash with another depth / blend state bound too
    uint32_t firstCall = 0, lastCall = 0;   // the frame's call count at its first and last call (1-based; 0 none)
};
std::wstring g_lastPath;
bool g_lastOk = false;
std::unordered_map<uint64_t, Rec> g_rec;   // by hash (0: a pipeline not seen made - kept by its pointer, | 1)
uint32_t g_recOrder = 0, g_recCalls = 0;
struct Entry { uint64_t hash; char kind; uint32_t calls; };
std::vector<Entry> g_list;                 // the last trace's pipelines that drew, in order (g_recLock)

thread_local ID3D12GraphicsCommandList* t_cl = nullptr;
thread_local bool t_skip = false;
thread_local uint64_t t_recKey = 0;

// [render] bench_skip_* (vr/render_bench.h): the keys on (bits of renderbench::Key), whether this frame is VR first person (taken at each
// present), what the pipeline bound on this thread's list belongs to, and the depth-stencil that list binds (the shadows: the sun's or not)
std::atomic<uint32_t> g_benchMask{0};
std::atomic<bool> g_benchGate{false};
thread_local uint8_t t_stage = 0;
thread_local ID3D12GraphicsCommandList* t_dsCl = nullptr;
thread_local ID3D12Resource* t_ds = nullptr;
// calls of each stage made / left out since the last bench line, and the presents meanwhile
std::atomic<uint32_t> g_benchSeen[renderbench::kStages] = {};
std::atomic<uint32_t> g_benchSkipped[renderbench::kKeys] = {};
std::atomic<uint32_t> g_benchPresents{0}, g_benchUngated{0};
// The shadow targets drawn into last frame and how many calls each took, sun / spot or point: ONE tally for the bench's line (what the maps
// are) and for [render] sun_shadows / sun_shadow_every (vr/simpler_sun.h: the sun's map = the sun / spot target with the most calls; SunMap).
// Lock-free: a slot is taken by a compare-exchange on its key (the target, the point flag in its low bits), its count by fetch_add - ~1,900
// shadow calls a frame, each was an exclusive lock. A call counted across the present's reset lands in the next frame's tally (a heuristic's
// count, never a decision of the game's).
struct Tally { std::atomic<uintptr_t> key{0}; std::atomic<uint32_t> calls{0}; };
Tally g_tally[12];
std::string g_shadowTargets;   // last frame's, written at the present (the bench line)
std::atomic<ID3D12Resource*> g_sunMap{nullptr};
std::atomic<uint32_t> g_sunCalls{0}, g_lastShadowCalls{0};
std::atomic<bool> g_sunWatch{false};   // [render] sun_shadows / sun_shadow_every: the tally kept without the bench (SetSunWatch)
uintptr_t TallyKey(ID3D12Resource* ds, bool point) { return (uintptr_t)ds | (point ? 1u : 0u) | 4u; }   // (bit 2: taken; resources are 8-aligned)
void NoteShadowTarget(ID3D12Resource* ds, bool point) {
    const uintptr_t k = TallyKey(ds, point);
    for (Tally& t : g_tally) {
        uintptr_t cur = t.key.load(std::memory_order_relaxed);
        if (!cur && t.key.compare_exchange_strong(cur, k, std::memory_order_relaxed)) cur = k;
        if (cur == k) { t.calls.fetch_add(1, std::memory_order_relaxed); return; }
    }
}
// (the present) the frame's sun map picked (the busiest sun / spot target); describe: the bench's text too, "4096x4096 sun/spot 1816, 512x512 point 12"
void TakeShadowTargets(bool describe) {
    std::string s;
    ID3D12Resource* best = nullptr;
    uint32_t bestCalls = 0, all = 0;
    for (Tally& t : g_tally) {
        const uintptr_t k = t.key.exchange(0, std::memory_order_relaxed);
        const uint32_t c = t.calls.exchange(0, std::memory_order_relaxed);
        if (!k || !c) continue;
        all += c;
        ID3D12Resource* ds = reinterpret_cast<ID3D12Resource*>(k & ~(uintptr_t)7);
        const bool point = (k & 1u) != 0;
        if (!point && ds && c > bestCalls) { best = ds; bestCalls = c; }
        if (!describe) continue;
        char b[80];
        if (ds) {
            const D3D12_RESOURCE_DESC d = ds->GetDesc();
            snprintf(b, sizeof b, "%s%llux%u%s %s %u", s.empty() ? "" : ", ", (unsigned long long)d.Width, d.Height,
                     d.DepthOrArraySize > 1 ? "(array)" : "", point ? "point" : "sun/spot", c);
        } else {
            snprintf(b, sizeof b, "%s(target not known) %s %u", s.empty() ? "" : ", ", point ? "point" : "sun/spot", c);
        }
        s += b;
    }
    g_lastShadowCalls.store(all, std::memory_order_relaxed);
    if (best) { g_sunMap.store(best, std::memory_order_relaxed); g_sunCalls.store(bestCalls, std::memory_order_relaxed); }
    if (describe && !s.empty()) g_shadowTargets = s;
}
ULONGLONG g_benchLineAt = 0;
uint32_t g_benchToldMask = 0;
// (while recording) what the list on this thread has bound
thread_local ID3D12GraphicsCommandList* t_tcl = nullptr;
thread_local std::string t_targets;
thread_local float t_vpW = 0.0f, t_vpH = 0.0f;

const char* FormatName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8_SRGB";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "RGBA8_TL";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "BGRA8_SRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10F";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
    case DXGI_FORMAT_R16G16_FLOAT: return "RG16F";
    case DXGI_FORMAT_R16_FLOAT: return "R16F";
    case DXGI_FORMAT_R32_FLOAT: return "R32F";
    case DXGI_FORMAT_R32G32_FLOAT: return "RG32F";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
    case DXGI_FORMAT_R8_UNORM: return "R8";
    case DXGI_FORMAT_R8G8_UNORM: return "RG8";
    case DXGI_FORMAT_R16G16_UNORM: return "RG16";
    case DXGI_FORMAT_R16G16B16A16_UNORM: return "RGBA16";
    case DXGI_FORMAT_R32_UINT: return "R32U";
    case DXGI_FORMAT_R32_TYPELESS: return "R32_TL";
    case DXGI_FORMAT_D32_FLOAT: return "D32F";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32FS8";
    case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TL";
    case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TL";
    default: return nullptr;
    }
}
void Describe(std::string& s, ID3D12Resource* r) {
    char b[96];
    if (!r) { s += "?"; return; }
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    const char* n = FormatName(d.Format);
    // (with the resource: the rows can be matched with the fog step's lines - which target the lit pass draws into, Serg 2026-11-09)
    if (n) snprintf(b, sizeof b, "%s %llux%u @%p", n, (unsigned long long)d.Width, d.Height, (void*)r);
    else snprintf(b, sizeof b, "fmt%d %llux%u @%p", (int)d.Format, (unsigned long long)d.Width, d.Height, (void*)r);
    s += b;
}

void Recount() {
    bool any = g_skipLegacy.load() || g_skipHunt.load() || g_shaders.load() || g_trace.load() != kIdle || g_benchMask.load() || g_sunWatch.load();
    g_any.store(any);
}
bool Skipped(uint64_t h) {
    if (!h) return false;
    if (h == g_skipLegacy.load(std::memory_order_relaxed) || h == g_skipHunt.load(std::memory_order_relaxed)) return true;
    const int n = g_shaders.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) if (g_skipShader[i].load(std::memory_order_relaxed) == h) return true;
    return false;
}
void ResolveHunt() {   // (g_recLock held) K of the list -> the hash skipped
    const int k = g_huntK.load();
    uint64_t h = 0;
    if (g_hunting.load() && k >= 1 && k <= (int)g_list.size()) h = g_list[(size_t)k - 1].hash;
    g_skipHunt.store(h);
    Recount();
}

// The pass-order line: where the game's transparent resolve and its fog apply (the atmosphere stage's full-screen graphics pass) drew in the
// frame, and the effects (blended, no depth written, not the bench's lit / water / atmosphere / tonemap ...) before and after the fog apply.
// The order is the order the calls were recorded in (one list a thread: the frame's lists may be recorded side by side - the game submits them
// in its pass order; a count, never a decision).
struct PassOrder { char line[400]; };
PassOrder OrderOf(const std::vector<Rec>& v) {
    PassOrder o{};
    uint32_t resolveFirst = 0, resolveLast = 0, fogFirst = 0, fogLast = 0, fogPipes = 0;
    for (const Rec& r : v) {
        if (!r.firstCall) continue;
        if (r.resolve) { resolveFirst = resolveFirst ? std::min(resolveFirst, r.firstCall) : r.firstCall; resolveLast = std::max(resolveLast, r.lastCall); }
        if (r.stage == renderbench::kAtmosphere && r.kind == 'g' && r.draws) {
            ++fogPipes;
            fogFirst = fogFirst ? std::min(fogFirst, r.firstCall) : r.firstCall;
            fogLast = std::max(fogLast, r.lastCall);
        }
    }
    uint32_t fxBefore = 0, fxBeforeCalls = 0, fxAfter = 0, fxAfterCalls = 0, fxBetween = 0, fxDepth = 0;
    for (const Rec& r : v) {
        if (!r.firstCall || r.kind != 'g' || !r.blend || r.zw != 0 || r.resolve || r.stage != renderbench::kNone) continue;
        const uint32_t calls = r.draws + r.indirect;
        if (fogFirst && r.lastCall < fogFirst) { ++fxBefore; fxBeforeCalls += calls; }
        else if (fogLast && r.firstCall > fogLast) { ++fxAfter; fxAfterCalls += calls; }
        else ++fxBetween;
        if (r.targets.find("| -") == std::string::npos && r.targets.find("| ?") == std::string::npos) ++fxDepth;   // (a depth bound to test against)
    }
    snprintf(o.line, sizeof o.line,
             "pass order: transparent resolve calls %u-%u, the game's fog apply (%u pipelines) calls %u-%u -> %s; effects without depth written: %u "
             "drew before the fog apply (%u calls: fogged by the depth behind them), %u after it (%u calls), %u across it; %u of them test a depth",
             resolveFirst, resolveLast, fogPipes, fogFirst, fogLast,
             !resolveFirst || !fogFirst ? "one of them not seen this frame"
             : fogFirst > resolveLast ? "the fog after the resolve (the flames fogged by the opaque depth behind them)"
             : fogLast < resolveFirst ? "the fog before the resolve" : "interleaved",
             fxBefore, fxBeforeCalls, fxAfter, fxAfterCalls, fxBetween, fxDepth);
    return o;
}

void Write() {   // (g_recLock held) the frame recorded to the file, its list kept
    std::vector<Rec> v;
    v.reserve(g_rec.size());
    for (const auto& kv : g_rec) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const Rec& a, const Rec& b) { return a.order < b.order; });
    g_list.clear();
    FILE* f = nullptr;
    _wfopen_s(&f, g_recPath.c_str(), L"w");
    if (f) {
        fprintf(f, "# D2R VR - one frame of the game's pipelines, in the order of their first bind (Ctrl+F10 without ReShade, vr/pipe_hunt.h)\n");
        fprintf(f, "# K = [debug] patch_hunt_pipe (patch_hunt = 8); hash = [debug] skip_pipeline / [render] skip_shaders (g: its pixel shader, c: its compute shader)\n");
        fprintf(f, "# stage: the bench's (vr/render_bench.h), resolve = the game's transparent resolve; zw: the depth written (1) or not (0), * = this "
                   "hash bound with another state too; bl: blended; calls: the frame's call numbers of its first and last call\n");
        fprintf(f, "#   K  kind  hash              vs                binds  draws  dispatches  indirect  stage             zw  bl  calls          "
                   "targets of its first call | depth | viewport\n");
    }
    for (const Rec& r : v) {
        const uint32_t calls = r.draws + r.dispatches + r.indirect;
        int k = 0;
        if (calls && r.kind != '?') {   // (a pipeline not seen made cannot be skipped by its hash)
            g_list.push_back({r.hash, r.kind, calls});
            k = (int)g_list.size();
        }
        if (f) {
            char ks[8] = "  -";
            if (k) snprintf(ks, sizeof ks, "%3d", k);
            char zw[4] = " -";
            if (r.zw >= 0) snprintf(zw, sizeof zw, "%d%s", (int)r.zw, r.mixed ? "*" : " ");
            fprintf(f, "  %s  %c     %016llx  %016llx  %5u  %5u  %10u  %8u  %-16s  %-2s  %-2s  %6u-%-6u  %s\n", ks, r.kind, (unsigned long long)r.hash,
                    (unsigned long long)r.vs, r.binds, r.draws, r.dispatches, r.indirect, r.resolve ? "resolve" : renderbench::StageName(r.stage), zw,
                    r.kind == 'g' && r.blend ? "1" : "-", r.firstCall, r.lastCall, r.targets.c_str());
        }
    }
    const PassOrder po = OrderOf(v);
    if (f) {
        fprintf(f, "# %s\n", po.line);
        fclose(f);
    }
    LogF("vrcam: pipe trace - %s", po.line);
    LogF("vrcam: pipe trace - %zu pipelines bound in one frame, %zu of them drew (the list patch_hunt_pipe counts in)%s", v.size(), g_list.size(),
         f ? " - written" : " - the file could NOT be written");
    g_recCalls = 0;
    g_lastPath = g_recPath;
    g_lastOk = f != nullptr;
    g_rec.clear();
    ResolveHunt();
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void OnGraphicsPso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso) return;
    Info i{HashOf(d->PS), HashOf(d->VS), 'g'};
    i.resolve = fogframe::IsResolve(d->PS.pShaderBytecode, d->PS.BytecodeLength);
    i.zw = DepthWrites(d->DepthStencilState);
    i.blend = Blends(d->BlendState);
    Keep(pso, i);
}
void OnComputePso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso) return;
    Keep(pso, {HashOf(d->CS), 0, 'c'});
}
void OnStreamPso(const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso || !d->pPipelineStateSubobjectStream) return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    Info i;
    size_t at = 0;
    D3D12_SHADER_BYTECODE b{};
    if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, &at)) {
        std::memcpy(&b, s + at, sizeof b);
        i.hash = HashOf(b);
        i.kind = 'c';
    } else {
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, &at)) {
            std::memcpy(&b, s + at, sizeof b);
            i.hash = HashOf(b);
            i.resolve = fogframe::IsResolve(b.pShaderBytecode, b.BytecodeLength);
        }
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, &at)) { std::memcpy(&b, s + at, sizeof b); i.vs = HashOf(b); }
        // the depth (a stream without one: enabled and written, D3D12's default) and the blend (none: not blended)
        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, &at)) std::memcpy(&ds, s + at, sizeof ds);
        else if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, &at)) {
            D3D12_DEPTH_STENCIL_DESC1 d1{};
            std::memcpy(&d1, s + at, sizeof d1);
            ds.DepthEnable = d1.DepthEnable;
            ds.DepthWriteMask = d1.DepthWriteMask;
        }
        i.zw = DepthWrites(ds);
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, &at)) {
            D3D12_BLEND_DESC bd{};
            std::memcpy(&bd, s + at, sizeof bd);
            i.blend = Blends(bd);
        }
    }
    Keep(pso, i);
}

void OnBound(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    t_cl = cl;
    t_skip = false;
    t_recKey = 0;
    t_stage = 0;
    if (!g_any.load(std::memory_order_relaxed) || !pso) return;
    Info i;
    const bool known = InfoOf(pso, &i);
    t_skip = known && Skipped(i.hash);
    if (known) t_stage = i.stage;
    if (g_trace.load(std::memory_order_relaxed) == kRecording) {
        std::lock_guard<std::mutex> l(g_recLock);
        const uint64_t key = known && i.hash ? i.hash : ((uint64_t)(uintptr_t)pso | 1ull << 63);
        Rec& r = g_rec[key];
        if (!r.binds) {
            r.order = ++g_recOrder; r.hash = key; r.vs = i.vs; r.kind = known ? i.kind : '?';
            r.stage = i.stage; r.resolve = i.resolve; r.zw = known ? i.zw : (int8_t)-1; r.blend = i.blend;
        } else if (known && (r.zw != i.zw || r.blend != i.blend)) r.mixed = true;
        ++r.binds;
        t_recKey = key;
    }
}

bool Skip(ID3D12GraphicsCommandList* cl, int kind) {
    if (!g_any.load(std::memory_order_relaxed) || cl != t_cl) return false;
    if (t_recKey && g_trace.load(std::memory_order_relaxed) == kRecording) {
        std::lock_guard<std::mutex> l(g_recLock);
        const auto it = g_rec.find(t_recKey);
        if (it != g_rec.end()) {
            Rec& r = it->second;
            if (!r.draws && !r.dispatches && !r.indirect) {   // its first call: what it drew into
                if (kind == 1) r.targets = "(compute)";
                else if (t_tcl == cl) {
                    char vp[48];
                    snprintf(vp, sizeof vp, " | vp %.0fx%.0f", t_vpW, t_vpH);
                    r.targets = t_targets + vp;
                }
            }
            (kind == 0 ? r.draws : kind == 1 ? r.dispatches : r.indirect)++;
            r.lastCall = ++g_recCalls;
            if (!r.firstCall) r.firstCall = r.lastCall;
        }
    }
    if (t_skip) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // [render] bench_skip_*: a call of a stage whose key is on, in VR first person - not made (its clears, barriers and binds are)
    const uint8_t st = t_stage;
    const uint32_t mask = g_benchMask.load(std::memory_order_relaxed);
    if (!st || (!mask && !g_sunWatch.load(std::memory_order_relaxed))) return false;
    if (mask) g_benchSeen[st].fetch_add(1, std::memory_order_relaxed);
    if (st == renderbench::kShadows || st == renderbench::kShadowsPoint) NoteShadowTarget(t_dsCl == cl ? t_ds : nullptr, st == renderbench::kShadowsPoint);
    if (!mask || !renderbench::Skips(mask, st)) return false;
    if (!g_benchGate.load(std::memory_order_relaxed)) { g_benchUngated.fetch_add(1, std::memory_order_relaxed); return false; }
    g_benchSkipped[renderbench::KeyOfStage(st)].fetch_add(1, std::memory_order_relaxed);
    return true;
}

void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds) {
    t_dsCl = cl;   // ([render] bench_skip_shadows_*: which shadow target this list draws into)
    t_ds = ds;
    if (g_trace.load(std::memory_order_relaxed) != kRecording) return;
    if (t_tcl != cl) { t_vpW = t_vpH = 0.0f; }
    t_tcl = cl;
    t_targets.clear();
    const unsigned inc = natived3d::RtvIncrement();
    for (unsigned i = 0; i < n && i < 8 && rts; ++i) {
        if (i) t_targets += " + ";
        const uint64_t h = single ? rts[0].ptr + (uint64_t)i * inc : rts[i].ptr;
        Describe(t_targets, natived3d::RtvResource(h));
    }
    if (!n) t_targets = "-";
    t_targets += " | ";
    if (ds) Describe(t_targets, ds);
    else t_targets += "-";
}
void OnViewport(ID3D12GraphicsCommandList* cl, float w, float h) {
    if (g_trace.load(std::memory_order_relaxed) != kRecording) return;
    if (t_tcl != cl) { t_tcl = cl; t_targets = "?"; }
    t_vpW = w;
    t_vpH = h;
}

void OnPresent() {
    const ULONGLONG now = GetTickCount64();
    // the shadow tally: the frame's sun map ([render] sun_shadows / sun_shadow_every), the bench line's targets
    if (g_benchMask.load(std::memory_order_relaxed) || g_sunWatch.load(std::memory_order_relaxed)) TakeShadowTargets(g_benchMask.load(std::memory_order_relaxed) != 0);
    if (g_benchMask.load(std::memory_order_relaxed)) {   // [render] bench_skip_*: F4 or not for the next frame
        g_benchGate.store(viewgate::F4(), std::memory_order_relaxed);
        g_benchPresents.fetch_add(1, std::memory_order_relaxed);
        if (g_benchLineAt && now - g_benchLineAt >= 12000) {   // no pair line to sit beside (eyes by turns, flat): its own 10 s line
            const std::string s = BenchLine();
            if (!s.empty()) LogF("%s", s.c_str());
        }
    }
    if (now - g_skippedAt.load(std::memory_order_relaxed) >= 1000) {
        g_skippedAt.store(now, std::memory_order_relaxed);
        g_skippedShown.store(g_skipped.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    const int t = g_trace.load();
    if (t == kIdle) return;
    std::lock_guard<std::mutex> l(g_recLock);
    if (t == kArmed) {
        g_rec.clear();
        g_recOrder = 0;
        g_trace.store(kRecording);
    } else if (t == kRecording) {
        g_trace.store(kIdle);
        Write();
    }
}

void SetSkip(uint64_t hash) {
    g_skipLegacy.store(hash);
    Recount();
}
void SetSkipShaders(const uint64_t* hashes, int n) {
    n = std::clamp(n, 0, kShaders);
    for (int i = 0; i < kShaders; ++i) g_skipShader[i].store(i < n && hashes ? hashes[i] : 0);
    g_shaders.store(n);
    Recount();
}
void SetHunt(bool on, int k) {
    std::lock_guard<std::mutex> l(g_recLock);
    g_hunting.store(on);
    g_huntK.store(k);
    ResolveHunt();
}
bool Arm(const std::wstring& path) {
    std::lock_guard<std::mutex> l(g_recLock);
    if (g_trace.load() != kIdle) return false;
    g_recPath = path;
    g_trace.store(kArmed);
    Recount();
    return true;
}

bool LastWritten(std::wstring* path) {
    std::lock_guard<std::mutex> l(g_recLock);
    if (path) *path = g_lastPath;
    return g_lastOk;
}

Now State() {
    Now n;
    std::lock_guard<std::mutex> l(g_recLock);
    n.hunting = g_hunting.load();
    n.index = g_huntK.load();
    n.count = (int)g_list.size();
    n.hash = g_skipHunt.load();
    if (n.index >= 1 && n.index <= n.count) {
        n.kind = g_list[(size_t)n.index - 1].kind;
        n.draws = g_list[(size_t)n.index - 1].calls;
    }
    n.skipLegacy = g_skipLegacy.load();
    n.shaders = g_shaders.load();
    n.skippedLastSecond = g_skippedShown.load();
    n.recording = g_trace.load() != kIdle;
    return n;
}

void SetBench(uint32_t mask) {
    mask &= renderbench::kAll;
    const uint32_t was = g_benchMask.exchange(mask);
    if (mask && !was) {
        g_benchLineAt = GetTickCount64();
        g_benchGate.store(viewgate::F4());
    }
    Recount();
    if (mask != was)
        LogF("vrcam: render bench - [render] bench_skip_*: the game's stages left out now: %s (VR first person only; draws, dispatches and indirect "
             "calls only - clears, barriers and binds go on)", renderbench::MaskText(mask).c_str());
}
uint32_t BenchMask() { return g_benchMask.load(); }
void SetSunWatch(bool on) {
    if (g_sunWatch.exchange(on) != on) Recount();
}
ID3D12Resource* SunMap(uint32_t* calls, uint32_t* shadowCalls) {
    if (calls) *calls = g_sunCalls.load(std::memory_order_relaxed);
    if (shadowCalls) *shadowCalls = g_lastShadowCalls.load(std::memory_order_relaxed);
    return g_sunMap.load(std::memory_order_relaxed);
}
std::string BenchLine() {
    const uint32_t mask = g_benchMask.load();
    const ULONGLONG now = GetTickCount64();
    const double secs = g_benchLineAt && now > g_benchLineAt ? (now - g_benchLineAt) / 1000.0 : 10.0;
    g_benchLineAt = now;
    uint32_t seen[renderbench::kStages], skipped[renderbench::kKeys];
    for (int i = 0; i < renderbench::kStages; ++i) seen[i] = g_benchSeen[i].exchange(0);
    for (int k = 0; k < renderbench::kKeys; ++k) skipped[k] = g_benchSkipped[k].exchange(0);
    const uint32_t presents = g_benchPresents.exchange(0), ungated = g_benchUngated.exchange(0);
    if (!mask) {
        if (!g_benchToldMask) return std::string();
        g_benchToldMask = 0;
        return "vrcam: render bench, 10 s: nothing left out (every [render] bench_skip_* key is 0)";
    }
    g_benchToldMask = mask;
    std::string out = "vrcam: render bench, 10 s: LEFT OUT " + renderbench::MaskText(mask) + " - calls not made a present:";
    char b[160];
    const double per = presents ? 1.0 / presents : 0.0;
    bool any = false;
    for (int k = 0; k < renderbench::kKeys; ++k) {
        if (!(mask >> k & 1u)) continue;
        snprintf(b, sizeof b, "%s %s %.0f", any ? "," : "", renderbench::KeyName(k), skipped[k] * per);
        out += b;
        any = true;
    }
    snprintf(b, sizeof b, " (%u presents in %.0f s; not F4: %u calls made anyway) | calls of each stage a present:", presents, secs, ungated);
    out += b;
    for (int st = 1; st < renderbench::kStages; ++st) {
        snprintf(b, sizeof b, " %s %.0f", renderbench::StageName(st), seen[st] * per);
        out += b;
    }
    out += " | pipelines recognised:";
    bool none = true;
    for (int st = 1; st < renderbench::kStages; ++st) {
        const uint32_t m = g_benchMade[st].load();
        if (m) none = false;
        snprintf(b, sizeof b, " %s %u", renderbench::StageName(st), m);
        out += b;
    }
    if (none) out += " - NONE: this game build's shaders are not in vr/render_bench_hashes.h (tools/gen_render_bench_hashes.py)";
    out += " | shadow targets last frame: " + (g_shadowTargets.empty() ? std::string("none seen") : g_shadowTargets);
    return out;
}

}  // namespace pipehunt
