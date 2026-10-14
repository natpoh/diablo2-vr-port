// The game's light culling at render sizes that are not 16:10: see vr/light_bounds.h and
// vr/light_bounds_shader.h. A replacement pipeline is made beside the game's own light-bin depth-bounds
// pipeline as the game creates it (ReShade's init_pipeline), and bound in its place (bind_pipeline) while
// [render] light_bounds_fix is on in VR. Nothing is written into the game: a failure at any step leaves the
// game's own pipeline in use, as it always was.
//
// The idea of a replacement pipeline matched by the shader's size and hash, and the values, are from
// bearstonem/d2r-pcvr (MIT), steamvr/light_bounds.cpp.

#include "light_bounds.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#include "game_device_only.h"
#include "light_bounds_shader.h"

namespace lightbounds {
namespace {

using namespace reshade::api;

// The game's shader (D2R 3.3.93787, data/hd/shaders/cs_light_bin_depth_bounds_c.bin: one DXBC container):
// the container's size and the FNV-1a 64 of its bytes, which is what ReShade hands over at creation.
constexpr uint32_t kGameShaderSize = 2164;
constexpr uint64_t kGameShaderHash = 0x7C22D74C89A5305Cull;

struct Copy {
    device* owner;
    uint64_t original;   // the game's pipeline
    pipeline replacement;
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Copy> g_copies;           // under g_lock
std::atomic<uint32_t> g_copyCount{0};   // g_copies.size(), for the bind's quick look
std::atomic<bool> g_active{false};
std::atomic<bool> g_logAll{false};
std::atomic<uint32_t> g_computeBinds{0};
std::atomic<bool> g_boundTold{false};
thread_local bool t_ours = false;       // our own calls into ReShade: their events are not for us
void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_registered{false};

void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}

uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}

// The shader compiled once, when the game's first matches it. Empty = it could not be (then no replacement).
std::once_flag g_compileOnce;
std::vector<uint8_t> g_code;
void Compile() {
    HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = lib ? reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile")) : nullptr;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    if (compile && SUCCEEDED(compile(kShader, sizeof kShader - 1, "vrcam-light-bounds", nullptr, nullptr, "main", "cs_5_0",
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
        const uint8_t* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
        g_code.assign(p, p + blob->GetBufferSize());
    }
    if (g_code.empty()) {
        Line("vrcam: light bounds - the replacement shader did not compile (%s): the game's own stays",
            !lib ? "no d3dcompiler_47.dll" : !compile ? "no D3DCompile" : errors ? (const char*)errors->GetBufferPointer() : "unknown");
    } else {
        Line("vrcam: light bounds - the replacement shader compiled (%zu bytes)", g_code.size());
    }
    if (blob) blob->Release();
    if (errors) errors->Release();
    if (lib) FreeLibrary(lib);
}

void OnInitPipeline(device* dev, pipeline_layout layout, uint32_t count, const pipeline_subobject* subs, pipeline original) {
    if (t_ours) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (subs[i].type != pipeline_subobject_type::compute_shader || !subs[i].data) continue;
        const shader_desc& native = *static_cast<const shader_desc*>(subs[i].data);
        if (!native.code || !native.code_size) return;
        const bool sized = native.code_size == kGameShaderSize;
        const uint64_t hash = sized || g_logAll.load(std::memory_order_relaxed) ? Fnv(native.code, native.code_size) : 0;
        if (g_logAll.load(std::memory_order_relaxed))
            Line("vrcam: compute shader %zu bytes, FNV-1a %016llX%s", native.code_size, (unsigned long long)hash,
                sized && hash == kGameShaderHash ? " - the light-bin depth-bounds shader" : "");
        if (!sized || hash != kGameShaderHash) return;   // another shader, or another build of the game: left alone
        if (!g_active.load(std::memory_order_relaxed)) return;   // off at the game's start: nothing is made (the old behaviour)
        std::call_once(g_compileOnce, Compile);
        if (g_code.empty()) return;
        shader_desc replaced = native;
        replaced.code = g_code.data();
        replaced.code_size = g_code.size();
        std::vector<pipeline_subobject> parts(subs, subs + count);
        parts[i].data = &replaced;
        parts[i].count = 1;
        pipeline made{};
        t_ours = true;
        const bool ok = dev->create_pipeline(layout, count, parts.data(), &made);
        t_ours = false;
        if (!ok) {
            Line("vrcam: light bounds - the game's shader matched but the replacement pipeline could not be made: the game's own stays");
            return;
        }
        AcquireSRWLockExclusive(&g_lock);
        g_copies.push_back({dev, original.handle, made});
        g_copyCount.store((uint32_t)g_copies.size());
        ReleaseSRWLockExclusive(&g_lock);
        Line("vrcam: light bounds - the game's light-bin depth-bounds shader found (%u bytes, FNV-1a %016llX); "
             "the replacement pipeline is ready",
             kGameShaderSize, (unsigned long long)kGameShaderHash);
        return;
    }
}

void OnDestroyPipeline(device* dev, pipeline original) {
    if (t_ours || !g_copyCount.load(std::memory_order_relaxed)) return;
    pipeline gone{};
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_copies.size(); ++i) {
        if (g_copies[i].owner == dev && g_copies[i].original == original.handle) {
            gone = g_copies[i].replacement;
            g_copies.erase(g_copies.begin() + (ptrdiff_t)i);
            g_copyCount.store((uint32_t)g_copies.size());
            break;
        }
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
        if (g_copies[i].owner == dev) {
            gone.push_back(g_copies[i].replacement);
            g_copies.erase(g_copies.begin() + (ptrdiff_t)i);
        } else {
            ++i;
        }
    }
    g_copyCount.store((uint32_t)g_copies.size());
    ReleaseSRWLockExclusive(&g_lock);
    t_ours = true;
    for (pipeline p : gone) dev->destroy_pipeline(p);
    t_ours = false;
}

// The game sets a pipeline thousands of times a frame: only a compute one is looked at, and only with the fix on.
void OnBindPipeline(command_list* cl, pipeline_stage stages, pipeline original) {
    if (t_ours || (stages & pipeline_stage::compute_shader) == 0) return;
    // A fix that is wanted and a game shader never seen: ReShade joined after the game made its pipelines.
    // The compute binds of a few seconds are plenty for the light bins to have come by (once, then silent).
    const uint32_t n = g_computeBinds.fetch_add(1, std::memory_order_relaxed);
    if (!g_active.load(std::memory_order_relaxed)) return;
    if (!g_copyCount.load(std::memory_order_relaxed)) {
        if (n == 20000) Line("vrcam: light bounds - the game's light-bin depth-bounds shader has not been seen (ReShade joined after "
                            "the game made its pipelines, or this is not build 3.3.93787): the game's own light culling stays");
        return;
    }
    device* dev = cl->get_device();
    pipeline replacement{};
    AcquireSRWLockShared(&g_lock);
    for (const Copy& c : g_copies)
        if (c.owner == dev && c.original == original.handle) { replacement = c.replacement; break; }
    ReleaseSRWLockShared(&g_lock);
    if (!replacement.handle) return;
    t_ours = true;
    cl->bind_pipeline(stages, replacement);
    t_ours = false;
    if (!g_boundTold.exchange(true)) Line("vrcam: light bounds - the replacement is bound in the game's light culling (rectangular tiles)");
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

void Configure(bool fix, bool logAll) {
    g_active.store(fix, std::memory_order_relaxed);
    g_logAll.store(logAll, std::memory_order_relaxed);
}

bool Wanted() { return g_active.load(std::memory_order_relaxed) || g_logAll.load(std::memory_order_relaxed); }

}  // namespace lightbounds
