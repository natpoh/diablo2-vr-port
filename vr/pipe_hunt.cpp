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

struct Info { uint64_t hash = 0, vs = 0; char kind = 'g'; };
SRWLOCK g_infoLock = SRWLOCK_INIT;
std::unordered_map<ID3D12PipelineState*, Info> g_info;
void Keep(ID3D12PipelineState* pso, const Info& i) {
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
};
std::wstring g_lastPath;
bool g_lastOk = false;
std::unordered_map<uint64_t, Rec> g_rec;   // by hash (0: a pipeline not seen made - kept by its pointer, | 1)
uint32_t g_recOrder = 0;
struct Entry { uint64_t hash; char kind; uint32_t calls; };
std::vector<Entry> g_list;                 // the last trace's pipelines that drew, in order (g_recLock)

thread_local ID3D12GraphicsCommandList* t_cl = nullptr;
thread_local bool t_skip = false;
thread_local uint64_t t_recKey = 0;
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
    char b[64];
    if (!r) { s += "?"; return; }
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    const char* n = FormatName(d.Format);
    if (n) snprintf(b, sizeof b, "%s %llux%u", n, (unsigned long long)d.Width, d.Height);
    else snprintf(b, sizeof b, "fmt%d %llux%u", (int)d.Format, (unsigned long long)d.Width, d.Height);
    s += b;
}

void Recount() {
    bool any = g_skipLegacy.load() || g_skipHunt.load() || g_shaders.load() || g_trace.load() != kIdle;
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
        fprintf(f, "#   K  kind  hash              vs                binds  draws  dispatches  indirect  targets of its first call | depth | viewport\n");
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
            fprintf(f, "  %s  %c     %016llx  %016llx  %5u  %5u  %10u  %8u  %s\n", ks, r.kind, (unsigned long long)r.hash, (unsigned long long)r.vs,
                    r.binds, r.draws, r.dispatches, r.indirect, r.targets.c_str());
        }
    }
    if (f) fclose(f);
    LogF("vrcam: pipe trace - %zu pipelines bound in one frame, %zu of them drew (the list patch_hunt_pipe counts in)%s", v.size(), g_list.size(),
         f ? " - written" : " - the file could NOT be written");
    g_lastPath = g_recPath;
    g_lastOk = f != nullptr;
    g_rec.clear();
    ResolveHunt();
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void OnGraphicsPso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!d || !pso) return;
    Keep(pso, {HashOf(d->PS), HashOf(d->VS), 'g'});
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
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, &at)) { std::memcpy(&b, s + at, sizeof b); i.hash = HashOf(b); }
        if (psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, &at)) { std::memcpy(&b, s + at, sizeof b); i.vs = HashOf(b); }
    }
    Keep(pso, i);
}

void OnBound(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    t_cl = cl;
    t_skip = false;
    t_recKey = 0;
    if (!g_any.load(std::memory_order_relaxed) || !pso) return;
    Info i;
    const bool known = InfoOf(pso, &i);
    t_skip = known && Skipped(i.hash);
    if (g_trace.load(std::memory_order_relaxed) == kRecording) {
        std::lock_guard<std::mutex> l(g_recLock);
        const uint64_t key = known && i.hash ? i.hash : ((uint64_t)(uintptr_t)pso | 1ull << 63);
        Rec& r = g_rec[key];
        if (!r.binds) { r.order = ++g_recOrder; r.hash = key; r.vs = i.vs; r.kind = known ? i.kind : '?'; }
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
        }
    }
    if (!t_skip) return false;
    g_skipped.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds) {
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

}  // namespace pipehunt
