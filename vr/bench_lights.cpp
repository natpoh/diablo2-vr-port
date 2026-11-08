// bench_lights.cpp - see bench_lights.h. The pipeline copies are made as vr/light_bounds.cpp makes its own (the game's description or stream
// with our shader in it, its cache left out), on the plugin's own D3D12 hooks (vr/native_d3d12.h).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include "bench_lights.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "native_d3d12.h"   // natived3d::SetOurs: our own pipeline creation is not told to the hooks
#include "pso_stream.h"
#include "view_gate.h"      // F4 only

namespace benchlights {
namespace {

void (*g_log)(const char*) = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}

enum Kind { kBin = 0, kGrid = 1, kKinds };
const char* const kName[kKinds] = {"light bins", "light grid"};
// Which of the two a compute shader is (the size first: cheap), -1 neither
int KindOf(const D3D12_SHADER_BYTECODE& cs) {
    if (!cs.pShaderBytecode) return -1;
    if (cs.BytecodeLength == kBinSize && Fnv(cs.pShaderBytecode, cs.BytecodeLength) == kBinHash) return kBin;
    if (cs.BytecodeLength == kGridSize && Fnv(cs.pShaderBytecode, cs.BytecodeLength) == kGridHash) return kGrid;
    return -1;
}

// Our shaders, compiled once at the first match. Empty = could not be (the game's own stays).
std::once_flag g_compileOnce;
std::vector<uint8_t> g_code[kKinds];
void Compile() {
    HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = lib ? reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile")) : nullptr;
    const char* const src[kKinds] = {kBinShader, kGridShader};
    const size_t len[kKinds] = {sizeof kBinShader - 1, sizeof kGridShader - 1};
    for (int k = 0; k < kKinds; ++k) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errors = nullptr;
        if (compile && SUCCEEDED(compile(src[k], len[k], "vrcam-bench-lights", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                         &blob, &errors))) {
            const uint8_t* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
            g_code[k].assign(p, p + blob->GetBufferSize());
        }
        if (g_code[k].empty())
            Say("vrcam: render bench - point lights: our %s shader did not compile (%s): bench_skip_point_lights does nothing for it", kName[k],
                 !lib ? "no d3dcompiler_47.dll" : !compile ? "no D3DCompile" : errors ? (const char*)errors->GetBufferPointer() : "unknown");
        if (blob) blob->Release();
        if (errors) errors->Release();
    }
    if (lib) FreeLibrary(lib);
}

// The game makes each once per device: a few places are plenty. Added under the lock, read without it (an entry whole before the count).
struct Copy { ID3D12PipelineState* game = nullptr; ID3D12PipelineState* ours = nullptr; int kind = 0; };
constexpr int kMax = 8;
Copy g_copies[kMax];
std::atomic<int> g_n{0};
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_swapped[kKinds] = {}, g_left[kKinds] = {};   // binds swapped / left as they were (not F4) since the last line
std::atomic<ULONGLONG> g_since{0};
bool g_told = false;

template <class Make> void Keep(ID3D12PipelineState* game, int kind, const char* how, Make make) {
    std::call_once(g_compileOnce, Compile);
    if (g_code[kind].empty()) return;
    const D3D12_SHADER_BYTECODE ours{g_code[kind].data(), g_code[kind].size()};
    ID3D12PipelineState* made = nullptr;
    const bool was = natived3d::Ours();
    natived3d::SetOurs(true);
    const HRESULT hr = make(ours, &made);
    natived3d::SetOurs(was);
    if (FAILED(hr) || !made) {
        Say("vrcam: render bench - point lights: the game's %s pipeline found (%s) but our copy was refused by the device (0x%08X): it stays the "
             "game's", kName[kind], how, (unsigned)hr);
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    const int n = g_n.load();
    if (n >= kMax) {
        ReleaseSRWLockExclusive(&g_lock);
        made->Release();
        return;
    }
    game->AddRef();   // (its address stays its own while we compare against it)
    g_copies[n] = {game, made, kind};
    g_n.store(n + 1, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    Say("vrcam: render bench - point lights: the game's %s pipeline found (%s); our copy with no local light is ready "
         "([render] bench_skip_point_lights)", kName[kind], how);
}

}  // namespace

void SetLog(void (*log)(const char*)) { g_log = log; }

void OnComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso) return;
    const int kind = KindOf(d->CS);
    if (kind < 0) return;
    Keep(pso, kind, "a compute description", [&](const D3D12_SHADER_BYTECODE& ours, ID3D12PipelineState** out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC c = *d;
        c.CS = ours;
        c.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};   // (the game's cache is of its own shader)
        return dev->CreateComputePipelineState(&c, __uuidof(ID3D12PipelineState), (void**)out);
    });
}

void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    size_t csAt = 0;
    if (!psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, &csAt)) return;
    D3D12_SHADER_BYTECODE cs;
    std::memcpy(&cs, s + csAt, sizeof cs);
    const int kind = KindOf(cs);
    if (kind < 0) return;
    Keep(pso, kind, "a pipeline stream", [&](const D3D12_SHADER_BYTECODE& ours, ID3D12PipelineState** out) {
        std::vector<uint8_t> copy(s, s + d->SizeInBytes);
        std::memcpy(&copy[csAt], &ours, sizeof ours);
        size_t cacheAt = 0;
        if (psostream::Find(copy.data(), copy.size(), D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, &cacheAt)) {
            const D3D12_CACHED_PIPELINE_STATE none{};
            std::memcpy(&copy[cacheAt], &none, sizeof none);
        }
        ID3D12Device2* d2 = nullptr;
        HRESULT hr = dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2);
        if (FAILED(hr) || !d2) return FAILED(hr) ? hr : E_NOINTERFACE;
        const D3D12_PIPELINE_STATE_STREAM_DESC sd{copy.size(), copy.data()};
        hr = d2->CreatePipelineState(&sd, __uuidof(ID3D12PipelineState), (void**)out);
        d2->Release();
        return hr;
    });
}

ID3D12PipelineState* Swap(ID3D12PipelineState* pso) {
    if (!g_on.load(std::memory_order_relaxed)) return pso;
    const int n = g_n.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (g_copies[i].game == pso) {
            if (!viewgate::F4()) { g_left[g_copies[i].kind].fetch_add(1, std::memory_order_relaxed); return pso; }
            g_swapped[g_copies[i].kind].fetch_add(1, std::memory_order_relaxed);
            return g_copies[i].ours;
        }
    return pso;
}

void SetOn(bool on) {
    const bool was = g_on.exchange(on);
    if (on == was) return;
    if (on) g_since.store(GetTickCount64());
    int ready[kKinds] = {};
    const int n = g_n.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) ++ready[g_copies[i].kind];
    Say("vrcam: render bench - point lights %s ([render] bench_skip_point_lights): %s; copies ready: light bins %d, light grid %d%s", on ? "OFF" : "back",
         on ? "the torches, fires, glowing things and the hero's light circle are left out of the game's light bins and light volumes - the sun "
              "and the area's ambient light alone (VR first person only)"
            : "the game's own light bins and volumes again",
         ready[kBin], ready[kGrid], (on && !n) ? " - NONE: this game build's light shaders are not the ones known (bench_lights.h)" : "");
}
bool On() { return g_on.load(); }

std::string Line() {
    const bool on = g_on.load();
    if (!on) {
        if (!g_told) return std::string();
        g_told = false;
        for (int k = 0; k < kKinds; ++k) { g_swapped[k].store(0); g_left[k].store(0); }
        return "vrcam: render bench, 10 s: the game's local lights back (bench_skip_point_lights 0)";
    }
    g_told = true;
    uint32_t sw[kKinds], left[kKinds];
    for (int k = 0; k < kKinds; ++k) { sw[k] = g_swapped[k].exchange(0); left[k] = g_left[k].exchange(0); }
    char b[320];
    snprintf(b, sizeof b, "vrcam: render bench, 10 s: LOCAL LIGHTS OFF (bench_skip_point_lights) - binds given our copy: light bins %u, light grid %u "
             "(not F4, left the game's: %u, %u)%s", sw[kBin], sw[kGrid], left[kBin], left[kGrid],
             (sw[kBin] + sw[kGrid] + left[kBin] + left[kGrid]) ? "" : " - none bound: no copy made, or the game did not bind them");
    return b;
}

}  // namespace benchlights
