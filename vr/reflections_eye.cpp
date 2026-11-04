// [render] reflections_per_eye: see vr/reflections_eye.h and shared/dxbc_ppr_eye.h. A failure at any step leaves the game's own
// pipeline bound, as it always was; nothing is written into the game.

#include "reflections_eye.h"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dxbc_ppr_eye.h"
#include "game_device_only.h"
#include "native_d3d12.h"
#include "pso_stream.h"

namespace refleye {
namespace {

using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[320];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}

std::atomic<bool> g_make{false};   // [render] reflections_per_eye at the game's start: the copies are made
std::atomic<bool> g_bind{false};   // ... and bound now (on, VR)
std::atomic<bool> g_registered{false};
std::atomic<uint32_t> g_binds[2] = {};   // binds of our copy since the 10 s line: [0] the projection (compute), [1] the resolve (pixel)
std::atomic<uint32_t> g_boundTold{0};
thread_local bool t_ours = false;   // our own calls into ReShade: their events are not for us

const char* KindName(bool compute) { return compute ? "projection (compute)" : "resolve (pixel)"; }

// The game's shader patched, or empty (not a reflection pass's: most of them) - `compute` says which pass
std::vector<uint8_t> Patched(const void* code, size_t size, bool* compute) {
    if (!code || !size) return {};
    dxbcppr::Found f;
    std::vector<uint8_t> out = dxbcppr::Patch((const uint8_t*)code, size, nullptr, &f);
    if (!out.empty()) *compute = f.compute;
    return out;
}

// ---- through ReShade ----
struct Copy {
    device* owner;
    uint64_t original;
    pipeline replacement;
    bool compute;
};
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Copy> g_copies;   // under g_lock
std::atomic<int> g_copyCount{0};

void OnInitPipeline(device* dev, pipeline_layout layout, uint32_t count, const pipeline_subobject* subs, pipeline original) {
    if (t_ours || !g_make.load(std::memory_order_relaxed)) return;   // off at the game's start: nothing made (the old behaviour)
    for (uint32_t i = 0; i < count; ++i) {
        if ((subs[i].type != pipeline_subobject_type::compute_shader && subs[i].type != pipeline_subobject_type::pixel_shader) || !subs[i].data) continue;
        const shader_desc& native = *static_cast<const shader_desc*>(subs[i].data);
        bool compute = false;
        const std::vector<uint8_t> code = Patched(native.code, native.code_size, &compute);
        if (code.empty()) continue;
        shader_desc replaced = native;
        replaced.code = code.data();
        replaced.code_size = code.size();
        std::vector<pipeline_subobject> parts(subs, subs + count);
        parts[i].data = &replaced;
        parts[i].count = 1;
        pipeline made{};
        t_ours = true;
        const bool ok = dev->create_pipeline(layout, count, parts.data(), &made);
        t_ours = false;
        if (!ok) {
            Line("vrcam: reflections per eye - the game's reflection %s found but its copy could not be made: the game's own stays", KindName(compute));
            return;
        }
        AcquireSRWLockExclusive(&g_lock);
        g_copies.push_back({dev, original.handle, made, compute});
        g_copyCount.store((int)g_copies.size());
        ReleaseSRWLockExclusive(&g_lock);
        Line("vrcam: reflections per eye - the game's reflection %s found (%zu bytes); its copy with the eye's own position is ready",
             KindName(compute), (size_t)native.code_size);
        return;
    }
}
void OnDestroyPipeline(device* dev, pipeline original) {
    if (t_ours || !g_copyCount.load(std::memory_order_relaxed)) return;
    pipeline gone{};
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_copies.size(); ++i)
        if (g_copies[i].owner == dev && g_copies[i].original == original.handle) {
            gone = g_copies[i].replacement;
            g_copies.erase(g_copies.begin() + (ptrdiff_t)i);
            g_copyCount.store((int)g_copies.size());
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    if (gone.handle) {
        t_ours = true;
        dev->destroy_pipeline(gone);
        t_ours = false;
    }
}
void OnDestroyDevice(device* dev) {
    std::vector<pipeline> gone;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_copies.size();) {
        if (g_copies[i].owner == dev) { gone.push_back(g_copies[i].replacement); g_copies.erase(g_copies.begin() + (ptrdiff_t)i); }
        else ++i;
    }
    g_copyCount.store((int)g_copies.size());
    ReleaseSRWLockExclusive(&g_lock);
    t_ours = true;
    for (pipeline p : gone) dev->destroy_pipeline(p);
    t_ours = false;
}
// thousands of binds a frame: a look only while bound and copies exist
void OnBindPipeline(command_list* cl, pipeline_stage stages, pipeline original) {
    if (t_ours || !g_bind.load(std::memory_order_relaxed) || !g_copyCount.load(std::memory_order_relaxed)) return;
    if ((stages & (pipeline_stage::compute_shader | pipeline_stage::pixel_shader)) == 0) return;
    device* dev = cl->get_device();
    pipeline replacement{};
    bool compute = false;
    AcquireSRWLockShared(&g_lock);
    for (const Copy& c : g_copies)
        if (c.owner == dev && c.original == original.handle) { replacement = c.replacement; compute = c.compute; break; }
    ReleaseSRWLockShared(&g_lock);
    if (!replacement.handle) return;
    t_ours = true;
    cl->bind_pipeline(stages, replacement);
    t_ours = false;
    g_binds[compute ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
}

// ---- without ReShade (vr/native_d3d12.h's hooks) ----
// The game makes each reflection pipeline once (a few permutations at most): a few places. Added under g_nativeLock, read without it (an
// entry is whole before the count that shows it).
struct Native {
    ID3D12PipelineState* game = nullptr;   // one reference of ours: its address stays its own
    ID3D12PipelineState* ours = nullptr;
    bool compute = false;
};
constexpr int kNativeMax = 8;
Native g_native[kNativeMax];
std::atomic<int> g_nativeN{0};
SRWLOCK g_nativeLock = SRWLOCK_INIT;

template <class Make> void Keep(ID3D12PipelineState* game, const std::vector<uint8_t>& code, bool compute, const char* how, Make make) {
    const D3D12_SHADER_BYTECODE ours{code.data(), code.size()};
    ID3D12PipelineState* made = nullptr;
    const bool was = natived3d::Ours();
    natived3d::SetOurs(true);   // (our own pipeline: not told to ourselves)
    const HRESULT hr = make(ours, &made);
    natived3d::SetOurs(was);
    if (FAILED(hr) || !made) {
        Line("vrcam: reflections per eye - the game's reflection %s found (%s) but its copy was refused by the device (0x%08X): the game's own "
             "stays", KindName(compute), how, (unsigned)hr);
        return;
    }
    AcquireSRWLockExclusive(&g_nativeLock);
    const int n = g_nativeN.load();
    if (n >= kNativeMax) {
        ReleaseSRWLockExclusive(&g_nativeLock);
        made->Release();
        return;
    }
    game->AddRef();
    g_native[n] = {game, made, compute};
    g_nativeN.store(n + 1, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_nativeLock);
    Line("vrcam: reflections per eye - the game's reflection %s found without ReShade (%s); its copy with the eye's own position is ready",
         KindName(compute), how);
}

}  // namespace

void Register(void (*log)(const char*)) {
    if (g_registered.exchange(true)) return;
    g_log = log;
    reshade::register_event<reshade::addon_event::init_pipeline>(&d2rvr::D3D12Only<&OnInitPipeline>::Call);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&OnDestroyPipeline>::Call);
    reshade::register_event<reshade::addon_event::destroy_device>(&d2rvr::D3D12Only<&OnDestroyDevice>::Call);
    reshade::register_event<reshade::addon_event::bind_pipeline>(&d2rvr::D3D12Only<&OnBindPipeline>::Call);
}

void Configure(bool make, bool bind) {
    g_make.store(make, std::memory_order_relaxed);
    g_bind.store(make && bind, std::memory_order_relaxed);
}

bool Wanted() { return g_make.load(std::memory_order_relaxed); }

void SetLog(void (*log)(const char*)) { g_log = log; }

void OnComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || !g_make.load(std::memory_order_relaxed)) return;
    bool compute = false;
    const std::vector<uint8_t> code = Patched(d->CS.pShaderBytecode, d->CS.BytecodeLength, &compute);
    if (code.empty() || !compute) return;
    Keep(pso, code, true, "a compute description", [&](const D3D12_SHADER_BYTECODE& ours, ID3D12PipelineState** out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC c = *d;
        c.CS = ours;
        c.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};   // (the game's cache is of its own shader)
        return dev->CreateComputePipelineState(&c, __uuidof(ID3D12PipelineState), (void**)out);
    });
}

void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || !g_make.load(std::memory_order_relaxed)) return;
    bool compute = true;
    const std::vector<uint8_t> code = Patched(d->PS.pShaderBytecode, d->PS.BytecodeLength, &compute);
    if (code.empty() || compute) return;
    Keep(pso, code, false, "a graphics description", [&](const D3D12_SHADER_BYTECODE& ours, ID3D12PipelineState** out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC g = *d;
        g.PS = ours;
        g.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
        return dev->CreateGraphicsPipelineState(&g, __uuidof(ID3D12PipelineState), (void**)out);
    });
}

void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    if (!dev || !d || !pso || !g_make.load(std::memory_order_relaxed) || !d->pPipelineStateSubobjectStream || !d->SizeInBytes ||
        d->SizeInBytes > 65536)
        return;
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    size_t at = 0;
    bool isCs = psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, &at);
    if (!isCs && !psostream::Find(s, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, &at)) return;
    D3D12_SHADER_BYTECODE sh;
    std::memcpy(&sh, s + at, sizeof sh);
    bool compute = false;
    const std::vector<uint8_t> code = Patched(sh.pShaderBytecode, sh.BytecodeLength, &compute);
    if (code.empty() || compute != isCs) return;
    Keep(pso, code, compute, "a pipeline stream", [&](const D3D12_SHADER_BYTECODE& ours, ID3D12PipelineState** out) {
        std::vector<uint8_t> copy(s, s + d->SizeInBytes);
        std::memcpy(&copy[at], &ours, sizeof ours);
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
    const int n = g_nativeN.load(std::memory_order_acquire);
    if (!n || !g_bind.load(std::memory_order_relaxed)) return pso;
    for (int i = 0; i < n; ++i)
        if (g_native[i].game == pso) {
            g_binds[g_native[i].compute ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
            return g_native[i].ours;
        }
    return pso;
}

void Tell() {
    if (!g_make.load(std::memory_order_relaxed)) return;
    const int copies = Copies();
    const bool bind = g_bind.load(std::memory_order_relaxed);
    const uint32_t state = (uint32_t)copies | (bind ? 0x100u : 0u);
    static std::atomic<uint32_t> told{~0u};
    if (told.exchange(state) != state)
        Line("vrcam: reflections per eye ([render] reflections_per_eye) - %d of the game's reflection pipelines with a copy; %s", copies,
             !copies ? "none seen yet (none made yet, or the game's shaders are not the ones known: its own reflections stay)"
             : bind  ? "the copies are bound: each eye's reflections by its own position - no GPU work of ours (one operand of the game's own)"
                     : "not bound now (off, or flat mode): the game's own");
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (!bind || !copies || now - at < 10000) return;
    at = now;
    const uint32_t a = g_binds[0].exchange(0), b = g_binds[1].exchange(0);
    if (a || b) Line("vrcam: reflections per eye, 10 s: our copies bound %u times for the projection pass, %u for the resolve (once a game pass; "
                     "the replay runs the same lists again for the right eye)", a, b);
}

int Copies() { return g_copyCount.load(std::memory_order_relaxed) + g_nativeN.load(std::memory_order_relaxed); }

}  // namespace refleye
