// [render] fog_in_frame: our fog drawn inside the game's frame, before its transparent resolve - see vr/fog_in_frame.h.

#include "fog_in_frame.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "fog_in_frame_math.h"
#include "fog_in_frame_shader.h"
#include "game_device_only.h"

namespace fogframe {
namespace {

using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[384];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}
// One line per reason while it lasts (the same reason again is not logged); 0 = drawing.
std::atomic<int> g_toldWhy{-1};
void Why(int code, const char* text) {
    if (g_toldWhy.exchange(code) != code) Line("vrcam: fog in the frame - %s", text);
}

std::atomic<bool> g_registered{false}, g_wanted{false}, g_on{false};
std::atomic<ULONGLONG> g_lastDraw{0};
std::atomic<uint32_t> g_draws{0};
std::atomic<uint32_t> g_apartDraws{0};   // the effects' draws taken apart (fog_in_frame_math.h BlendRt)
std::atomic<bool> g_apartTold{false};
std::atomic<bool> g_computeTold{false};
thread_local bool t_ours = false;   // our own calls into ReShade: their events are not for us

SRWLOCK g_paramLock = SRWLOCK_INIT;
Params g_params;   // under g_paramLock

// ---------------------------------------------------------------- our shaders
std::once_flag g_compileOnce;
std::vector<uint8_t> g_copyPs, g_compositePs, g_fogCs;
std::vector<uint8_t> CompileOne(pD3DCompile compile, const char* src, size_t n, const char* entry, const char* target) {
    std::vector<uint8_t> out;
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    if (SUCCEEDED(compile(src, n, "vrcam-fog-in-frame", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
        const uint8_t* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
        out.assign(p, p + blob->GetBufferSize());
    } else {
        Line("vrcam: fog in the frame - %s did not compile: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "unknown");
    }
    if (blob) blob->Release();
    if (errors) errors->Release();
    return out;
}
void Compile() {
    HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = lib ? reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile")) : nullptr;
    if (compile) {
        g_copyPs = CompileOne(compile, kPixel, sizeof kPixel - 1, "CopyScene", "ps_5_0");
        g_compositePs = CompileOne(compile, kPixel, sizeof kPixel - 1, "Composite", "ps_5_0");
        g_fogCs = CompileOne(compile, kFog, sizeof kFog - 1, "main", "cs_5_0");
    } else {
        Line("vrcam: fog in the frame - %s: the game's resolve stays", !lib ? "no d3dcompiler_47.dll" : "no D3DCompile");
    }
    if (lib) FreeLibrary(lib);
}
bool Compiled() { return !g_copyPs.empty() && !g_compositePs.empty() && !g_fogCs.empty(); }

// Does a pixel shader write a UAV (D3DReflect)? An effect's draw is drawn twice when it is kept apart (into E and into K):
// what it writes besides its colour would be written twice - such an effect stays in the scene. Unknown: as if it did.
bool WritesUav(const void* code, size_t size) {
    using ReflectFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
    static ReflectFn reflect = [] {
        HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // (kept loaded)
        return lib ? reinterpret_cast<ReflectFn>(GetProcAddress(lib, "D3DReflect")) : nullptr;
    }();
    ID3D11ShaderReflection* r = nullptr;
    if (!reflect || !code || FAILED(reflect(code, size, __uuidof(ID3D11ShaderReflection), (void**)&r)) || !r) return true;
    D3D11_SHADER_DESC d{};
    bool uav = FAILED(r->GetDesc(&d));
    for (UINT i = 0; !uav && i < d.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC b{};
        if (FAILED(r->GetResourceBindingDesc(i, &b))) { uav = true; break; }
        uav = b.Type == D3D_SIT_UAV_RWTYPED || b.Type == D3D_SIT_UAV_RWSTRUCTURED || b.Type == D3D_SIT_UAV_RWBYTEADDRESS ||
              b.Type == D3D_SIT_UAV_APPEND_STRUCTURED || b.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
              b.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER || b.Type == D3D_SIT_UAV_FEEDBACKTEXTURE;
    }
    r->Release();
    return uav;
}

// ---------------------------------------------------------------- the game's resolve pipelines, and ours beside each
struct Pipes {
    device* owner = nullptr;
    uint64_t game = 0;
    pipeline copy{0}, composite{0};
    depth_stencil_desc ds{};
};
SRWLOCK g_pipeLock = SRWLOCK_INIT;
std::vector<Pipes> g_pipes;   // under g_pipeLock
std::atomic<uint32_t> g_pipeCount{0};
// Our compute step: its root signature (b0: 20 root constants; table t0 - t5; table u0) and pipeline, once per device.
device* g_csOwner = nullptr;
pipeline_layout g_layout{0};
pipeline g_cs{0};

bool FindPipes(device* dev, uint64_t game, Pipes* out) {
    AcquireSRWLockShared(&g_pipeLock);
    bool found = false;
    for (const Pipes& p : g_pipes)
        if (p.owner == dev && p.game == game) { *out = p; found = true; break; }
    ReleaseSRWLockShared(&g_pipeLock);
    return found;
}
// The game's resolve pipelines, for the bind's quick look without a lock (the game binds pipelines thousands of times a frame).
constexpr int kQuick = 8;
std::atomic<uint64_t> g_quick[kQuick] = {};
void QuickAdd(uint64_t p) {   // under g_pipeLock
    for (std::atomic<uint64_t>& q : g_quick) {
        uint64_t none = 0;
        if (q.compare_exchange_strong(none, p)) return;
    }
}
void QuickRemove(uint64_t p) {   // under g_pipeLock
    for (std::atomic<uint64_t>& q : g_quick) {
        uint64_t was = p;
        q.compare_exchange_strong(was, 0);
    }
}
bool IsGamePipe(uint64_t p) {
    if (!p) return false;
    for (const std::atomic<uint64_t>& q : g_quick)
        if (q.load(std::memory_order_relaxed) == p) return true;
    return false;
}

bool MakeCompute(device* dev) {   // under g_pipeLock (exclusive)
    if (g_csOwner == dev && g_cs.handle) return true;
    const descriptor_range srv{0, 0, 0, 6, shader_stage::compute, 1, descriptor_type::texture_shader_resource_view};
    const descriptor_range uav{0, 0, 0, 1, shader_stage::compute, 1, descriptor_type::texture_unordered_access_view};
    const pipeline_layout_param params[3] = {
        pipeline_layout_param(constant_range{0, 0, 0, sizeof(Consts) / 4, shader_stage::compute}),
        pipeline_layout_param(1, &srv),
        pipeline_layout_param(1, &uav),
    };
    pipeline_layout layout{0};
    if (!dev->create_pipeline_layout(3, params, &layout)) return false;
    shader_desc cs{};
    cs.code = g_fogCs.data();
    cs.code_size = g_fogCs.size();
    const pipeline_subobject sub{pipeline_subobject_type::compute_shader, 1, &cs};
    pipeline made{0};
    if (!dev->create_pipeline(layout, 1, &sub, &made)) {
        dev->destroy_pipeline_layout(layout);
        return false;
    }
    g_csOwner = dev;
    g_layout = layout;
    g_cs = made;
    return true;
}

// ---------------------------------------------------------------- the effects the game draws into the scene itself
// (vr/fog_in_frame_math.h BlendRt): every pipeline that blends into an R11G11B10 target without writing depth, and the
// copy of it that draws the coverage (its source factor zero, nothing else written) - made as the game makes it.
struct EffectPipe {
    device* owner = nullptr;
    pipeline keep{0};            // the coverage's copy; 0 when it is not taken apart
    uint64_t ps = 0;             // its pixel shader's FNV-1a (for the log)
    const char* why = nullptr;   // why it stays in the scene (fogged), or nullptr
    BlendRt b;
};
SRWLOCK g_fxLock = SRWLOCK_INIT;
std::unordered_map<uint64_t, EffectPipe> g_fx;   // the game's pipeline -> ours, under g_fxLock
std::atomic<uint32_t> g_fxCount{0};
bool FindEffect(uint64_t game, EffectPipe* out) {
    AcquireSRWLockShared(&g_fxLock);
    const auto it = g_fx.find(game);
    const bool found = it != g_fx.end();
    if (found) *out = it->second;
    ReleaseSRWLockShared(&g_fxLock);
    return found;
}
bool IsEffect(uint64_t game) {
    AcquireSRWLockShared(&g_fxLock);
    const bool found = g_fx.find(game) != g_fx.end();
    ReleaseSRWLockShared(&g_fxLock);
    return found;
}
void NoteEffect(device* dev, pipeline_layout layout, uint32_t count, const pipeline_subobject* subs, pipeline made, int ps, int blend, int depth,
                int rts) {
    if (rts < 0 || !subs[rts].data || subs[rts].count < 1 || static_cast<const format*>(subs[rts].data)[0] != format::r11g11b10_float) return;
    if (blend < 0 || !subs[blend].data) return;
    const blend_desc& bd = *static_cast<const blend_desc*>(subs[blend].data);
    if (!bd.blend_enable[0]) return;
    const depth_stencil_desc ds = depth >= 0 && subs[depth].data ? *static_cast<const depth_stencil_desc*>(subs[depth].data) : depth_stencil_desc{};
    bool others = false;
    for (uint32_t i = 1; i < subs[rts].count && i < 8; ++i) others = others || bd.render_target_write_mask[i] != 0;
    EffectPipe e;
    e.owner = dev;
    e.b = BlendRt{bd.blend_enable[0], bd.logic_op_enable[0], (uint32_t)bd.source_color_blend_factor[0], (uint32_t)bd.dest_color_blend_factor[0],
                (uint32_t)bd.color_blend_op[0]};
    bool uav = true;
    if (ps >= 0 && subs[ps].data) {
        const shader_desc& sd = *static_cast<const shader_desc*>(subs[ps].data);
        if (sd.code && sd.code_size) {
            e.ps = Fnv(sd.code, sd.code_size);
            uav = WritesUav(sd.code, sd.code_size);
        }
    }
    e.why = WhyNotApart(e.b, ds.depth_enable && ds.depth_write_mask, others);
    if (!e.why && uav) e.why = "its pixel shader writes a UAV (it would be written twice), or could not be read";
    if (!e.why) {   // the coverage's copy: the scene's factor alone, no stencil written (the game's own draw writes it once)
        blend_desc kd = bd;
        const BlendRt kb = KeepVariant(e.b);
        kd.source_color_blend_factor[0] = (blend_factor)kb.src;
        kd.source_alpha_blend_factor[0] = blend_factor::zero;
        depth_stencil_desc kds = ds;
        kds.depth_write_mask = false;
        kds.front_stencil_write_mask = kds.back_stencil_write_mask = 0;
        std::vector<pipeline_subobject> parts(subs, subs + count);
        parts[blend].data = &kd;
        if (depth >= 0) parts[depth].data = &kds;
        t_ours = true;
        if (!dev->create_pipeline(layout, (uint32_t)parts.size(), parts.data(), &e.keep)) { e.keep = {0}; e.why = "its coverage's copy could not be made"; }
        t_ours = false;
    }
    AcquireSRWLockExclusive(&g_fxLock);
    g_fx[made.handle] = e;
    g_fxCount.store((uint32_t)g_fx.size());
    ReleaseSRWLockExclusive(&g_fxLock);
}

// The game made a pipeline: is its pixel shader the resolve? Then ours beside it, with its layout and state.
void OnInitPipeline(device* dev, pipeline_layout layout, uint32_t count, const pipeline_subobject* subs, pipeline made) {
    if (t_ours || !g_wanted.load(std::memory_order_relaxed)) return;
    int ps = -1, blend = -1, depth = -1, rts = -1;
    for (uint32_t i = 0; i < count; ++i) {
        switch (subs[i].type) {
            case pipeline_subobject_type::pixel_shader: ps = (int)i; break;
            case pipeline_subobject_type::blend_state: blend = (int)i; break;
            case pipeline_subobject_type::depth_stencil_state: depth = (int)i; break;
            case pipeline_subobject_type::render_target_formats: rts = (int)i; break;
            default: break;
        }
    }
    if (ps < 0 || !subs[ps].data) return;
    const shader_desc& native = *static_cast<const shader_desc*>(subs[ps].data);
    if (!IsResolve(native.code, native.code_size)) {
        NoteEffect(dev, layout, count, subs, made, ps, blend, depth, rts);
        return;
    }
    Line("vrcam: fog in the frame - the game's transparent resolve found (%u bytes, FNV-1a %016llX)", kResolveSize,
         (unsigned long long)kResolveHash);
    std::call_once(g_compileOnce, Compile);
    if (!Compiled()) return;
    if (depth < 0 || !subs[depth].data || rts < 0 || subs[rts].count != 1 || !subs[rts].data) {
        Line("vrcam: fog in the frame - the resolve's pipeline has no depth-stencil state or not one target: the game's resolve stays");
        return;
    }
    const depth_stencil_desc ds = *static_cast<const depth_stencil_desc*>(subs[depth].data);
    const format rt = *static_cast<const format*>(subs[rts].data);
    Line("vrcam: fog in the frame - the resolve's state: target format %u, depth %s, stencil %s (func %u, read %02X, write %02X, pass %u fail %u;"
         " back func %u, read %02X)",
         (unsigned)rt, ds.depth_enable ? "on" : "off", ds.stencil_enable ? "on" : "off", (unsigned)ds.front_stencil_func,
         ds.front_stencil_read_mask, ds.front_stencil_write_mask, (unsigned)ds.front_stencil_pass_op, (unsigned)ds.front_stencil_fail_op,
         (unsigned)ds.back_stencil_func, ds.back_stencil_read_mask);
    const bool blends = blend >= 0 && subs[blend].data && static_cast<const blend_desc*>(subs[blend].data)->blend_enable[0];
    if (rt != format::r11g11b10_float || ds.depth_enable || blends ||
        (ds.stencil_enable && (ds.front_stencil_func != ds.back_stencil_func || ds.front_stencil_read_mask != ds.back_stencil_read_mask))) {
        Line("vrcam: fog in the frame - the resolve is not drawn as this build knows it (target, depth test, blending, or stencil per face): "
             "the game's resolve stays");
        return;
    }
    // 1. the scene term: our pixel shader, the game's stencil test with nothing written
    shader_desc copyPs = native, compositePs = native;
    copyPs.code = g_copyPs.data();
    copyPs.code_size = g_copyPs.size();
    compositePs.code = g_compositePs.data();
    compositePs.code_size = g_compositePs.size();
    depth_stencil_desc readOnly = ds;
    readOnly.depth_write_mask = false;
    readOnly.front_stencil_write_mask = readOnly.back_stencil_write_mask = 0;
    std::vector<pipeline_subobject> one(subs, subs + count), three(subs, subs + count);
    one[ps].data = &copyPs;
    one[ps].count = 1;
    one[depth].data = &readOnly;
    // 3. the composite: our second pixel shader, blended over the fogged scene (R = rgb + R x alpha); the game's stencil state
    blend_desc over = blend >= 0 && subs[blend].data ? *static_cast<const blend_desc*>(subs[blend].data) : blend_desc{};
    over.blend_enable[0] = true;
    over.logic_op_enable[0] = false;
    over.source_color_blend_factor[0] = blend_factor::one;
    over.dest_color_blend_factor[0] = blend_factor::source_alpha;
    over.color_blend_op[0] = blend_op::add;
    over.source_alpha_blend_factor[0] = blend_factor::one;
    over.dest_alpha_blend_factor[0] = blend_factor::zero;
    over.alpha_blend_op[0] = blend_op::add;
    three[ps].data = &compositePs;
    three[ps].count = 1;
    const pipeline_subobject overSub{pipeline_subobject_type::blend_state, 1, &over};
    if (blend >= 0) three[blend] = overSub;
    else three.push_back(overSub);
    Pipes made3{dev, made.handle};
    made3.ds = ds;
    t_ours = true;
    bool ok = dev->create_pipeline(layout, (uint32_t)one.size(), one.data(), &made3.copy) &&
              dev->create_pipeline(layout, (uint32_t)three.size(), three.data(), &made3.composite);
    t_ours = false;
    AcquireSRWLockExclusive(&g_pipeLock);
    if (ok) {
        t_ours = true;
        ok = MakeCompute(dev);
        t_ours = false;
    }
    if (ok) {
        g_pipes.push_back(made3);
        g_pipeCount.store((uint32_t)g_pipes.size());
        QuickAdd(made.handle);
    }
    ReleaseSRWLockExclusive(&g_pipeLock);
    if (!ok) {
        t_ours = true;
        if (made3.copy.handle) dev->destroy_pipeline(made3.copy);
        if (made3.composite.handle) dev->destroy_pipeline(made3.composite);
        t_ours = false;
        Line("vrcam: fog in the frame - our pipelines could not be made: the game's resolve stays");
        return;
    }
    Line("vrcam: fog in the frame - our pipelines are ready beside the game's resolve");
}

void OnDestroyPipeline(device* dev, pipeline p) {
    if (t_ours) return;
    if (g_fxCount.load(std::memory_order_relaxed)) {
        pipeline keep{0};
        AcquireSRWLockExclusive(&g_fxLock);
        if (const auto it = g_fx.find(p.handle); it != g_fx.end()) {
            keep = it->second.keep;
            g_fx.erase(it);
            g_fxCount.store((uint32_t)g_fx.size());
        }
        ReleaseSRWLockExclusive(&g_fxLock);
        if (keep.handle) {
            t_ours = true;
            dev->destroy_pipeline(keep);
            t_ours = false;
        }
    }
    if (!g_pipeCount.load(std::memory_order_relaxed)) return;
    Pipes gone{};
    AcquireSRWLockExclusive(&g_pipeLock);
    for (size_t i = 0; i < g_pipes.size(); ++i)
        if (g_pipes[i].owner == dev && g_pipes[i].game == p.handle) {
            gone = g_pipes[i];
            g_pipes.erase(g_pipes.begin() + (ptrdiff_t)i);
            QuickRemove(p.handle);
            break;
        }
    g_pipeCount.store((uint32_t)g_pipes.size());
    ReleaseSRWLockExclusive(&g_pipeLock);
    t_ours = true;
    if (gone.copy.handle) dev->destroy_pipeline(gone.copy);
    if (gone.composite.handle) dev->destroy_pipeline(gone.composite);
    t_ours = false;
}

// ---------------------------------------------------------------- what each list being recorded has bound
// The game's compute bindings in a list (its light culling runs in the resolve's list): our compute step sets its own,
// and they are given back after it as the game had them - the root signature and tables through ReShade (which puts the
// game's descriptor heaps back with them), the root constants, and the root CBV / SRV / UAV addresses with the native
// calls (through the same hooks as the game's: the replay's camera redirection sees them as it saw the game's).
struct ComputeArgs {
    uint64_t layout = 0;
    std::vector<uint64_t> tables;                 // by root parameter, 0 = not set
    struct Root { uint8_t kind = 0; uint64_t va = 0; };   // kind: 0 none, 1 CBV, 2 SRV, 3 UAV
    std::vector<Root> roots;
    struct Consts32 { uint64_t mask = 0; uint32_t v[64] = {}; };
    std::vector<Consts32> consts;
    bool unknown = false;                         // something set the way ReShade does not tell us fully
    void Reset() { layout = 0; tables.clear(); roots.clear(); consts.clear(); unknown = false; }
};
// Kept per list (one list is recorded on one thread at a time), from its reset or creation on: a list begun before we
// joined is not trusted.
struct ListState {
    bool fresh = false;              // seen from its reset or creation
    bool compute = false;            // the game set compute state in it (given back after our compute step: cargs)
    ComputeArgs cargs;
    uint64_t resolve = 0;            // the game's resolve pipeline bound, else 0
    uint64_t effect = 0;             // an effect's pipeline bound (g_fx), else 0
    uint64_t gfxLayout = 0;          // the graphics root signature, and the descriptor tables bound with it
    std::vector<uint64_t> tables;
    resource_view rtv[8] = {};
    uint32_t rtvCount = 0;
    resource_view dsv{0};
    viewport vp{};
    rect sc{};
    bool vpSet = false, scSet = false;
    uint32_t stencilRef = 0;
    uint64_t depthRes = 0;           // the last barrier in this list on the depth buffer the resolve had
    uint32_t depthState = 0;
    void Clear() {   // (the vectors' room kept: lists are reset hundreds of times a second)
        std::vector<uint64_t> keep;
        keep.swap(tables);
        keep.clear();
        ComputeArgs c;
        std::swap(c, cargs);
        c.Reset();
        *this = ListState{};
        tables.swap(keep);
        std::swap(cargs, c);
    }
};
SRWLOCK g_listLock = SRWLOCK_INIT;
std::unordered_map<uint64_t, ListState*> g_lists;   // never freed: a list's address made again gets its state cleared
thread_local uint64_t t_cl = 0;
thread_local ListState* t_st = nullptr;
ListState* State(command_list* cl) {
    const uint64_t h = (uint64_t)cl;
    if (t_cl == h && t_st) return t_st;
    ListState* s = nullptr;
    AcquireSRWLockShared(&g_listLock);
    if (auto it = g_lists.find(h); it != g_lists.end()) s = it->second;
    ReleaseSRWLockShared(&g_listLock);
    if (!s) {
        AcquireSRWLockExclusive(&g_listLock);
        ListState*& slot = g_lists[h];
        if (!slot) slot = new ListState();
        s = slot;
        ReleaseSRWLockExclusive(&g_listLock);
    }
    t_cl = h;
    t_st = s;
    return s;
}

// The depth buffer the resolve had (its depth-stencil view; g_depthRes, above), and the last barrier on it in any list.
std::atomic<uint32_t> g_depthState{0};   // 0: none seen (the game keeps it as a depth target)

void OnInitCommandList(command_list* cl) {   // made open, its state empty
    ListState* s = State(cl);
    s->Clear();
    s->fresh = true;
}
void OnResetCommandList(command_list* cl) {
    ListState* s = State(cl);
    s->Clear();
    s->fresh = true;
}
void OnDestroyCommandList(command_list* cl) {
    ListState* s = State(cl);
    s->Clear();   // (fresh = false: an address made again is trusted only from its reset or creation)
    t_cl = 0;
    t_st = nullptr;
}

void OnBindPipeline(command_list* cl, pipeline_stage, pipeline p) {
    if (t_ours || !g_pipeCount.load(std::memory_order_relaxed)) return;
    ListState* s = State(cl);
    s->resolve = IsGamePipe(p.handle) ? p.handle : 0;
    s->effect = g_on.load(std::memory_order_relaxed) && g_fxCount.load(std::memory_order_relaxed) && IsEffect(p.handle) ? p.handle : 0;
}
void OnBindDescriptorTables(command_list* cl, shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count,
                            const descriptor_table* tables) {
    if (t_ours) return;
    ListState* s = State(cl);
    if ((stages & (shader_stage::all_compute | shader_stage::all_ray_tracing)) != 0) {
        s->compute = true;
        ComputeArgs& c = s->cargs;
        if (count == 0 || layout.handle != c.layout) {   // a compute root signature set: its bindings start empty
            c.Reset();
            c.layout = layout.handle;
        }
        if (tables) {
            if (c.tables.size() < first + count) c.tables.resize(first + count, 0);
            for (uint32_t i = 0; i < count; ++i) c.tables[first + i] = tables[i].handle;
        }
    }
    if ((stages & shader_stage::all_graphics) == 0) return;
    if (count == 0 || layout.handle != s->gfxLayout) {   // a root signature set: its bindings start empty
        s->gfxLayout = layout.handle;
        s->tables.clear();
    }
    if (!tables) return;
    if (s->tables.size() < first + count) s->tables.resize(first + count, 0);
    for (uint32_t i = 0; i < count; ++i) s->tables[first + i] = tables[i].handle;
}
void OnPushDescriptors(command_list* cl, shader_stage stages, pipeline_layout layout, uint32_t param, const descriptor_table_update& u) {
    if (t_ours || (stages & (shader_stage::all_compute | shader_stage::all_ray_tracing)) == 0) return;
    ListState* s = State(cl);
    s->compute = true;
    ComputeArgs& c = s->cargs;
    if (layout.handle != c.layout) { c.Reset(); c.layout = layout.handle; }
    // the root descriptors as ReShade's D3D12 wrapper reports them: a CBV as a buffer range, an SRV / UAV as its address
    uint8_t kind = 0;
    uint64_t va = 0;
    if (u.count == 1 && u.binding == 0 && u.descriptors) {
        if (u.type == descriptor_type::constant_buffer) {
            const buffer_range& r = *static_cast<const buffer_range*>(u.descriptors);
            if (r.buffer.handle) { kind = 1; va = reinterpret_cast<ID3D12Resource*>(r.buffer.handle)->GetGPUVirtualAddress() + r.offset; }
        } else if (u.type == descriptor_type::buffer_shader_resource_view || u.type == descriptor_type::acceleration_structure) {
            kind = 2; va = *static_cast<const uint64_t*>(u.descriptors);
        } else if (u.type == descriptor_type::buffer_unordered_access_view) {
            kind = 3; va = *static_cast<const uint64_t*>(u.descriptors);
        }
    }
    if (!kind || !va) { c.unknown = true; return; }
    if (c.roots.size() <= param) c.roots.resize(param + 1);
    c.roots[param] = {kind, va};
}
void OnPushConstants(command_list* cl, shader_stage stages, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) {
    if (t_ours || (stages & (shader_stage::all_compute | shader_stage::all_ray_tracing)) == 0) return;
    ListState* s = State(cl);
    s->compute = true;
    ComputeArgs& c = s->cargs;
    if (layout.handle != c.layout) { c.Reset(); c.layout = layout.handle; }
    if (!values || first + count > 64) { c.unknown = true; return; }
    if (c.consts.size() <= param) c.consts.resize(param + 1);
    for (uint32_t i = 0; i < count; ++i) {
        c.consts[param].v[first + i] = static_cast<const uint32_t*>(values)[i];
        c.consts[param].mask |= 1ull << (first + i);
    }
}
// Our compute step over: the game's compute bindings back (ComputeArgs). Before the graphics tables are given back (those
// put the game's heaps back last).
void GiveComputeBack(command_list* cl, const ComputeArgs& c) {
    if (!c.layout) return;
    const pipeline_layout layout{c.layout};
    cl->bind_descriptor_tables(shader_stage::all_compute, layout, 0, 0, nullptr);   // (count 0: ReShade sets the root signature again)
    for (uint32_t i = 0; i < (uint32_t)c.tables.size(); ++i)
        if (c.tables[i]) {
            const descriptor_table t{c.tables[i]};
            cl->bind_descriptor_tables(shader_stage::all_compute, layout, i, 1, &t);
        }
    auto* native = reinterpret_cast<ID3D12GraphicsCommandList*>(cl->get_native());
    for (uint32_t i = 0; i < (uint32_t)c.roots.size(); ++i) {
        const ComputeArgs::Root& r = c.roots[i];
        if (r.kind == 1) native->SetComputeRootConstantBufferView(i, r.va);
        else if (r.kind == 2) native->SetComputeRootShaderResourceView(i, r.va);
        else if (r.kind == 3) native->SetComputeRootUnorderedAccessView(i, r.va);
    }
    for (uint32_t i = 0; i < (uint32_t)c.consts.size(); ++i) {
        ForEachRun(c.consts[i].mask, [&](uint32_t first, uint32_t n) {   // each run of set values
            cl->push_constants(shader_stage::all_compute, layout, i, first, n, &c.consts[i].v[first]);
        });
    }
}
// The lit scene S: the forward pass's colour target - R11G11B10, the depth buffer's size, bound with a second target
// (R8G8B8A8, the surfaces' material) and the resolve's depth buffer. What draws blended into it is taken apart (AtEffect).
std::atomic<uint64_t> g_depthRes{0};
// (up to four: the game may draw its frames in flight into targets of their own)
constexpr int kScenes = 4;
std::atomic<uint64_t> g_scenes[kScenes] = {};
std::atomic<uint32_t> g_sceneNext{0};
bool IsScene(uint64_t r) {
    if (!r) return false;
    for (const std::atomic<uint64_t>& x : g_scenes)
        if (x.load(std::memory_order_relaxed) == r) return true;
    return false;
}
void NoteScene(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    const uint64_t depth = g_depthRes.load(std::memory_order_relaxed);
    if (count < 2 || !rtvs || !rtvs[0].handle || !rtvs[1].handle || !dsv.handle || !depth) return;
    device* dev = cl->get_device();
    if (dev->get_resource_from_view(dsv).handle != depth) return;
    const resource r0 = dev->get_resource_from_view(rtvs[0]), r1 = dev->get_resource_from_view(rtvs[1]);
    if (!r0.handle || !r1.handle || IsScene(r0.handle)) return;
    const resource_desc d0 = dev->get_resource_desc(r0), d1 = dev->get_resource_desc(r1), dd = dev->get_resource_desc(resource{depth});
    if (d0.texture.format != format::r11g11b10_float || d0.texture.width != dd.texture.width || d0.texture.height != dd.texture.height ||
        (d1.texture.format != format::r8g8b8a8_unorm && d1.texture.format != format::r8g8b8a8_typeless))
        return;
    const uint32_t n = g_sceneNext.fetch_add(1, std::memory_order_relaxed);
    g_scenes[n % kScenes].store(r0.handle, std::memory_order_relaxed);
    if (n < kScenes)
        Line("vrcam: fog in the frame - the lit scene's target %016llX (%ux%u): what is drawn blended into it is kept apart from the fog",
             (unsigned long long)r0.handle, d0.texture.width, d0.texture.height);
}
void OnBindTargets(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    if (t_ours) return;
    ListState* s = State(cl);
    s->rtvCount = count > 8 ? 8 : count;
    for (uint32_t i = 0; i < s->rtvCount; ++i) s->rtv[i] = rtvs ? rtvs[i] : resource_view{0};
    s->dsv = dsv;
    if (g_on.load(std::memory_order_relaxed)) NoteScene(cl, count, rtvs, dsv);
}
void OnBindViewports(command_list* cl, uint32_t first, uint32_t count, const viewport* v) {
    if (t_ours || first != 0 || !count || !v) return;
    ListState* s = State(cl);
    s->vp = v[0];
    s->vpSet = true;
}
void OnBindScissors(command_list* cl, uint32_t first, uint32_t count, const rect* r) {
    if (t_ours || first != 0 || !count || !r) return;
    ListState* s = State(cl);
    s->sc = r[0];
    s->scSet = true;
}
void OnBindStates(command_list* cl, uint32_t count, const dynamic_state* states, const uint32_t* values) {
    if (t_ours || !states || !values) return;
    for (uint32_t i = 0; i < count; ++i)
        if (states[i] == dynamic_state::front_stencil_reference_value) State(cl)->stencilRef = values[i];
}
void OnBarrier(command_list* cl, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to) {
    if (t_ours || !res || !to) return;
    const uint64_t d = g_depthRes.load(std::memory_order_relaxed);
    if (!d) return;
    for (uint32_t i = 0; i < count; ++i)
        if (res[i].handle == d) {
            ListState* s = State(cl);
            s->depthRes = d;
            s->depthState = (uint32_t)to[i];
            g_depthState.store((uint32_t)to[i], std::memory_order_relaxed);
        }
}

// ---------------------------------------------------------------- our textures, for a frame target and depth buffer
// ourS: the scene term (render target); ourF: the fogged scene (written by the compute step, copied into the target).
// Their views in a heap of ours (not shader visible); the compute step's two tables from ReShade's (static, not the
// transient ring: the replay sends the list again a while later).
struct Set {
    device* owner = nullptr;
    uint64_t target = 0, depth = 0;
    resource_desc td{}, dd{};
    ID3D12Resource* scene = nullptr;
    ID3D12Resource* fogged = nullptr;
    ID3D12Resource* effects = nullptr;       // E: the effects drawn into the scene, on black (cleared with the scene)
    ID3D12Resource* keep = nullptr;          // K: how much of the scene they left (cleared to 1 with the scene)
    ID3D12DescriptorHeap* views = nullptr;   // 0 depth, 1 stencil, 2 scene, 3 target, 4 E, 5 K (SRVs), 6 fogged (UAV)
    ID3D12DescriptorHeap* rtvs = nullptr;    // 0 scene, 1 E, 2 K
    resource_view sceneRtv{0}, effectsRtv{0}, keepRtv{0};
    descriptor_table tables[2] = {};         // t0 - t5, u0
    uint32_t gen = 0;                        // which set this is (E and K cleared for it: g_clearedGen)
    bool ready = false;
};
SRWLOCK g_setLock = SRWLOCK_INIT;
Set g_set;                         // under g_setLock
std::vector<Set> g_graveyard;      // old sets, destroyed two seconds later: a list recorded before may still use them
ULONGLONG g_graveAt = 0;
std::atomic<uint32_t> g_setGen{0};
// E and K cleared with the scene: for which set, and when (the effects are taken apart only while they are kept so).
std::atomic<uint32_t> g_clearedGen{0};
std::atomic<ULONGLONG> g_clearedAt{0};
bool KeptApart(const Set& s) {
    return s.ready && s.gen == g_clearedGen.load(std::memory_order_relaxed) &&
           GetTickCount64() - g_clearedAt.load(std::memory_order_relaxed) < 250;
}

void Destroy(Set& s) {
    if (s.owner && (s.tables[0].handle || s.tables[1].handle)) {
        for (descriptor_table t : s.tables)
            if (t.handle) s.owner->free_descriptor_table(t);
    }
    if (s.scene) s.scene->Release();
    if (s.fogged) s.fogged->Release();
    if (s.effects) s.effects->Release();
    if (s.keep) s.keep->Release();
    if (s.views) s.views->Release();
    if (s.rtvs) s.rtvs->Release();
    s = Set{};
}

// The depth buffer's views by its typeless format: the depth (plane 0) and the stencil (plane 1, or none).
bool DepthFormats(format f, DXGI_FORMAT* depth, DXGI_FORMAT* stencil) {
    switch (f) {
        case format::r24_g8_typeless: *depth = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; *stencil = DXGI_FORMAT_X24_TYPELESS_G8_UINT; return true;
        case format::r32_g8_typeless: *depth = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; *stencil = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT; return true;
        case format::r32_typeless: *depth = DXGI_FORMAT_R32_FLOAT; *stencil = DXGI_FORMAT_UNKNOWN; return true;
        case format::r16_typeless: *depth = DXGI_FORMAT_R16_UNORM; *stencil = DXGI_FORMAT_UNKNOWN; return true;
        default: return false;
    }
}

bool MakeSet(device* dev, resource target, resource depth, const resource_desc& td, const resource_desc& dd, Set* out) {
    Set s;
    s.owner = dev;
    s.target = target.handle;
    s.depth = depth.handle;
    s.td = td;
    s.dd = dd;
    auto* d3d = reinterpret_cast<ID3D12Device*>(dev->get_native());
    DXGI_FORMAT depthFmt, stencilFmt;
    if (!d3d || !DepthFormats(dd.texture.format, &depthFmt, &stencilFmt)) return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R11G11B10_FLOAT};
    if (FAILED(d3d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs)) ||
        (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
        Why(20, "the GPU cannot write R11G11B10 from a compute shader: the game's resolve stays");
        return false;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = td.texture.width;
    rd.Height = td.texture.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    bool ok = SUCCEEDED(d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     __uuidof(ID3D12Resource), (void**)&s.scene)) &&
              SUCCEEDED(d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     __uuidof(ID3D12Resource), (void**)&s.effects)) &&
              SUCCEEDED(d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     __uuidof(ID3D12Resource), (void**)&s.keep));
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ok = ok && SUCCEEDED(d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                      __uuidof(ID3D12Resource), (void**)&s.fogged));
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 7, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ok = ok && SUCCEEDED(d3d->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&s.views));
    D3D12_DESCRIPTOR_HEAP_DESC rh{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ok = ok && SUCCEEDED(d3d->CreateDescriptorHeap(&rh, __uuidof(ID3D12DescriptorHeap), (void**)&s.rtvs));
    if (ok) {
        const UINT step = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE base = s.views->GetCPUDescriptorHandleForHeapStart();
        auto at = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE h = base; h.ptr += (SIZE_T)i * step; return h; };
        auto* depthR = reinterpret_cast<ID3D12Resource*>(depth.handle);
        auto* targetR = reinterpret_cast<ID3D12Resource*>(target.handle);
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        sv.Format = depthFmt;
        d3d->CreateShaderResourceView(depthR, &sv, at(0));
        if (stencilFmt != DXGI_FORMAT_UNKNOWN) {
            sv.Format = stencilFmt;
            sv.Texture2D.PlaneSlice = 1;
            d3d->CreateShaderResourceView(depthR, &sv, at(1));
        } else {
            sv.Format = DXGI_FORMAT_R32G32_UINT;   // none: a null view reads 0 (the stencil test is off then)
            d3d->CreateShaderResourceView(nullptr, &sv, at(1));
        }
        sv.Texture2D.PlaneSlice = 0;
        sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        d3d->CreateShaderResourceView(s.scene, &sv, at(2));
        sv.Format = (DXGI_FORMAT)td.texture.format;
        d3d->CreateShaderResourceView(targetR, &sv, at(3));
        sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        d3d->CreateShaderResourceView(s.effects, &sv, at(4));
        d3d->CreateShaderResourceView(s.keep, &sv, at(5));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        d3d->CreateUnorderedAccessView(s.fogged, nullptr, &uv, at(6));
        const UINT rstep = d3d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = s.rtvs->GetCPUDescriptorHandleForHeapStart();
        auto rat = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE h = rtv; h.ptr += (SIZE_T)i * rstep; return h; };
        D3D12_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        d3d->CreateRenderTargetView(s.scene, &rv, rat(0));
        d3d->CreateRenderTargetView(s.effects, &rv, rat(1));
        d3d->CreateRenderTargetView(s.keep, &rv, rat(2));
        s.sceneRtv = resource_view{(uint64_t)rat(0).ptr};
        s.effectsRtv = resource_view{(uint64_t)rat(1).ptr};
        s.keepRtv = resource_view{(uint64_t)rat(2).ptr};
        ok = dev->allocate_descriptor_tables(1, g_layout, 1, &s.tables[0]) && dev->allocate_descriptor_tables(1, g_layout, 2, &s.tables[1]);
        if (ok) {
            const resource_view srvs[6] = {{(uint64_t)at(0).ptr}, {(uint64_t)at(1).ptr}, {(uint64_t)at(2).ptr},
                                           {(uint64_t)at(3).ptr}, {(uint64_t)at(4).ptr}, {(uint64_t)at(5).ptr}};
            const resource_view uav{(uint64_t)at(6).ptr};
            const descriptor_table_update up[2] = {
                {s.tables[0], 0, 0, 6, descriptor_type::texture_shader_resource_view, srvs},
                {s.tables[1], 0, 0, 1, descriptor_type::texture_unordered_access_view, &uav},
            };
            dev->update_descriptor_tables(2, up);
        }
    }
    if (!ok) {
        Destroy(s);
        return false;
    }
    s.ready = true;
    s.gen = g_setGen.fetch_add(1) + 1;
    *out = s;
    return true;
}

// The set for this target and depth buffer: the current one, or new ones made now (the old into the graveyard).
bool SetFor(device* dev, resource target, resource depth, const resource_desc& td, const resource_desc& dd, Set* out) {
    AcquireSRWLockShared(&g_setLock);
    const bool same = g_set.ready && g_set.owner == dev && g_set.target == target.handle && g_set.depth == depth.handle &&
                      g_set.td.texture.width == td.texture.width && g_set.td.texture.height == td.texture.height;
    if (same) *out = g_set;
    ReleaseSRWLockShared(&g_setLock);
    if (same) return true;
    AcquireSRWLockExclusive(&g_setLock);
    bool ok = g_set.ready && g_set.owner == dev && g_set.target == target.handle && g_set.depth == depth.handle;
    if (!ok) {
        Set made;
        t_ours = true;
        ok = g_cs.handle && MakeSet(dev, target, depth, td, dd, &made);
        t_ours = false;
        if (ok) {
            if (g_set.ready) { g_graveyard.push_back(g_set); g_graveAt = GetTickCount64(); }
            g_set = made;
            Line("vrcam: fog in the frame - our textures made, %ux%u (the depth buffer's format %u)", td.texture.width, td.texture.height,
                 (unsigned)dd.texture.format);
        }
    }
    if (ok) *out = g_set;
    ReleaseSRWLockExclusive(&g_setLock);
    return ok;
}

void OnDestroyResource(device*, resource r) {
    if (!r.handle) return;
    AcquireSRWLockShared(&g_setLock);
    const bool ours = g_set.ready && (g_set.target == r.handle || g_set.depth == r.handle);
    ReleaseSRWLockShared(&g_setLock);
    if (!ours) return;
    AcquireSRWLockExclusive(&g_setLock);
    if (g_set.ready && (g_set.target == r.handle || g_set.depth == r.handle)) {   // the game's target or depth goes: ours go with it later
        g_graveyard.push_back(g_set);
        g_graveAt = GetTickCount64();
        g_set = Set{};
    }
    ReleaseSRWLockExclusive(&g_setLock);
}

void OnDestroyDevice(device* dev) {
    AcquireSRWLockExclusive(&g_setLock);
    if (g_set.owner == dev) Destroy(g_set);
    for (Set& s : g_graveyard)
        if (s.owner == dev) Destroy(s);
    ReleaseSRWLockExclusive(&g_setLock);
    std::vector<pipeline> keeps;
    AcquireSRWLockExclusive(&g_fxLock);
    for (auto it = g_fx.begin(); it != g_fx.end();) {
        if (it->second.owner == dev) { if (it->second.keep.handle) keeps.push_back(it->second.keep); it = g_fx.erase(it); }
        else ++it;
    }
    g_fxCount.store((uint32_t)g_fx.size());
    ReleaseSRWLockExclusive(&g_fxLock);
    t_ours = true;
    for (pipeline k : keeps) dev->destroy_pipeline(k);
    t_ours = false;
    std::vector<Pipes> gone;
    AcquireSRWLockExclusive(&g_pipeLock);
    for (size_t i = 0; i < g_pipes.size();) {
        if (g_pipes[i].owner == dev) { gone.push_back(g_pipes[i]); QuickRemove(g_pipes[i].game); g_pipes.erase(g_pipes.begin() + (ptrdiff_t)i); }
        else ++i;
    }
    g_pipeCount.store((uint32_t)g_pipes.size());
    pipeline cs = g_csOwner == dev ? g_cs : pipeline{0};
    pipeline_layout layout = g_csOwner == dev ? g_layout : pipeline_layout{0};
    if (g_csOwner == dev) { g_csOwner = nullptr; g_cs = {0}; g_layout = {0}; }
    ReleaseSRWLockExclusive(&g_pipeLock);
    t_ours = true;
    for (const Pipes& p : gone) { dev->destroy_pipeline(p.copy); dev->destroy_pipeline(p.composite); }
    if (cs.handle) dev->destroy_pipeline(cs);
    if (layout.handle) dev->destroy_pipeline_layout(layout);
    t_ours = false;
}

// ---------------------------------------------------------------- at the resolve
D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// The game's resolve draw: our three steps in its place (true), or the game's draw alone (false).
template <typename DrawFn>
bool AtResolve(command_list* cl, DrawFn draw) {
    if (t_ours || !g_on.load(std::memory_order_relaxed) || !g_pipeCount.load(std::memory_order_relaxed)) return false;
    ListState* st = State(cl);
    if (!st->resolve) return false;
    device* dev = cl->get_device();
    Pipes pipes;
    if (!FindPipes(dev, st->resolve, &pipes)) return false;
    if (!st->fresh) return Why(1, "a list begun before the add-on joined: this frame left to the game"), false;
    if (st->compute && st->cargs.unknown)
        return Why(2, "the game's compute bindings in the resolve's list not all known (a root argument ReShade did not report): left to the game"), false;
    if (st->rtvCount != 1 || !st->rtv[0].handle || !st->dsv.handle) return Why(3, "the resolve's target or depth buffer was not seen: left to the game"), false;
    if (!st->vpSet || !st->scSet) return Why(4, "the resolve's viewport or scissor was not seen: left to the game"), false;
    bool anyTable = false;
    for (uint64_t t : st->tables) anyTable = anyTable || t != 0;
    if (!st->gfxLayout || !anyTable) return Why(5, "the resolve's descriptor tables were not seen: left to the game"), false;
    const resource target = dev->get_resource_from_view(st->rtv[0]), depth = dev->get_resource_from_view(st->dsv);
    if (!target.handle || !depth.handle) return Why(3, "the resolve's target or depth buffer was not seen: left to the game"), false;
    const resource_desc td = dev->get_resource_desc(target), dd = dev->get_resource_desc(depth);
    if (td.texture.format != format::r11g11b10_float || td.texture.levels != 1 || td.texture.samples != 1 || td.texture.depth_or_layers != 1 ||
        dd.texture.width != td.texture.width || dd.texture.height != td.texture.height || dd.texture.samples != 1)
        return Why(6, "the resolve's target is not the frame's colour target of the depth buffer's size: left to the game"), false;
    if (g_depthRes.exchange(depth.handle, std::memory_order_relaxed) != depth.handle) g_depthState.store(0, std::memory_order_relaxed);
    // the depth buffer's state here: the list's last barrier on it, else the last anywhere, else a depth target
    uint32_t s0 = st->depthRes == depth.handle ? st->depthState : g_depthState.load(std::memory_order_relaxed);
    if (!s0) s0 = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if ((s0 & (D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ)) == 0 ||
        ((s0 & D3D12_RESOURCE_STATE_DEPTH_WRITE) && s0 != D3D12_RESOURCE_STATE_DEPTH_WRITE))
        return Why(7, "the depth buffer's state at the resolve is not a depth target's: left to the game"), false;
    Set set;
    if (!SetFor(dev, target, depth, td, dd, &set)) return Why(8, "our textures could not be made: left to the game"), false;
    if (pipes.ds.stencil_enable && set.dd.texture.format != format::r24_g8_typeless && set.dd.texture.format != format::r32_g8_typeless)
        return Why(9, "the resolve tests a stencil the depth buffer has not: left to the game"), false;

    Consts k{};
    {
        AcquireSRWLockShared(&g_paramLock);
        const Params p = g_params;
        ReleaseSRWLockShared(&g_paramLock);
        const V3 fog = SceneFromDisplay({p.fogColour[0], p.fogColour[1], p.fogColour[2]});
        k = Consts{p.nearPlane, p.fogStart, p.fogEnd, p.fogCurve, p.fogStrength, KeptApart(set) ? 1.0f : 0.0f, (float)p.paint, 0.0f,
                   fog.r, fog.g, fog.b, p.caveHalo, 0, 0, 0, 0, 0, 0, 0, 0};
    }
    {   // where the game's draw reaches: its viewport and scissor
        const float vx0 = (std::max)(0.0f, st->vp.x), vy0 = (std::max)(0.0f, st->vp.y);
        const float vx1 = (std::min)((float)td.texture.width, st->vp.x + st->vp.width), vy1 = (std::min)((float)td.texture.height, st->vp.y + st->vp.height);
        k.x0 = (uint32_t)(std::max<int32_t>)((int32_t)vx0, st->sc.left);
        k.y0 = (uint32_t)(std::max<int32_t>)((int32_t)vy0, st->sc.top);
        k.x1 = (uint32_t)(std::max<int32_t>)(0, (std::min<int32_t>)((int32_t)(vx1 + 0.5f), st->sc.right));
        k.y1 = (uint32_t)(std::max<int32_t>)(0, (std::min<int32_t>)((int32_t)(vy1 + 0.5f), st->sc.bottom));
    }
    k.stencilOn = pipes.ds.stencil_enable ? 1u : 0u;
    k.stencilFunc = (uint32_t)pipes.ds.front_stencil_func;
    k.stencilRef = st->stencilRef;
    k.stencilMask = pipes.ds.front_stencil_read_mask;

    auto* native = reinterpret_cast<ID3D12GraphicsCommandList*>(cl->get_native());
    auto* targetR = reinterpret_cast<ID3D12Resource*>(target.handle);
    auto* depthR = reinterpret_cast<ID3D12Resource*>(depth.handle);
    const D3D12_RESOURCE_STATES dState = (D3D12_RESOURCE_STATES)s0;
    const D3D12_RESOURCE_STATES dRead = (dState & D3D12_RESOURCE_STATE_DEPTH_WRITE)
                                            ? (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                            : (dState | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    t_ours = true;
    // 1. the scene term, into our copy: the game's pipeline (its stencil test, nothing written) with our pixel shader
    cl->bind_render_targets_and_depth_stencil(1, &set.sceneRtv, st->dsv);
    cl->bind_pipeline(pipeline_stage::all, pipes.copy);
    draw();
    // 2. the fog: our compute step from our copy and the game's depth into ours, then into the frame's target
    {
        D3D12_RESOURCE_BARRIER b[5] = {Tr(set.scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                       Tr(targetR, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                       Tr(set.effects, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                       Tr(set.keep, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                       Tr(depthR, dState, dRead)};
        native->ResourceBarrier(dRead != dState ? 5 : 4, b);
    }
    cl->bind_pipeline(pipeline_stage::all_compute, g_cs);
    cl->bind_descriptor_tables(shader_stage::all_compute, g_layout, 1, 2, set.tables);
    cl->push_constants(shader_stage::all_compute, g_layout, 0, 0, sizeof(Consts) / 4, &k);
    cl->dispatch((td.texture.width + 7) / 8, (td.texture.height + 7) / 8, 1);
    {
        D3D12_RESOURCE_BARRIER b[2] = {Tr(set.fogged, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                       Tr(targetR, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
        native->ResourceBarrier(2, b);
    }
    native->CopyResource(targetR, set.fogged);
    {
        D3D12_RESOURCE_BARRIER b[6] = {Tr(targetR, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                       Tr(set.fogged, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                       Tr(set.scene, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                       Tr(set.effects, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                       Tr(set.keep, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                       Tr(depthR, dRead, dState)};
        native->ResourceBarrier(dRead != dState ? 6 : 5, b);
    }
    // the game's compute bindings as it had them (our compute step set ours), then its descriptor heaps and tables
    if (st->compute) GiveComputeBack(cl, st->cargs);
    // the game's descriptor heaps and tables as it had them (our table took the heap's place); its graphics root
    // signature and root arguments were never changed
    for (uint32_t i = 0; i < (uint32_t)st->tables.size(); ++i) {
        if (!st->tables[i]) continue;
        const descriptor_table t{st->tables[i]};
        cl->bind_descriptor_tables(shader_stage::all_graphics, pipeline_layout{st->gfxLayout}, i, 1, &t);
    }
    // 3. what the transparents add, over the fogged scene: the game's pipeline (its stencil state) with our second shader
    cl->bind_render_targets_and_depth_stencil(st->rtvCount, st->rtv, st->dsv);
    cl->bind_pipeline(pipeline_stage::all, pipes.composite);
    draw();
    cl->bind_pipeline(pipeline_stage::all, pipeline{st->resolve});
    t_ours = false;

    g_lastDraw.store(GetTickCount64(), std::memory_order_relaxed);
    if (st->compute && !g_computeTold.exchange(true))
        Line("vrcam: fog in the frame - the game's compute bindings in the resolve's list given back after ours (%zu tables, %zu root "
             "arguments, %zu constant ranges)", st->cargs.tables.size(), st->cargs.roots.size(), st->cargs.consts.size());
    if (g_draws.fetch_add(1, std::memory_order_relaxed) == 0)
        Line("vrcam: fog in the frame - first drawn (%ux%u, the depth buffer's state %08X, stencil test %s, the draw's rect %u,%u - %u,%u, "
             "the effects drawn into the scene %s)",
             td.texture.width, td.texture.height, s0, k.stencilOn ? "on" : "off", k.x0, k.y0, k.x1, k.y1,
             k.effectsApart > 0.5f ? "kept apart" : "not kept apart yet (the scene's clear not seen)");
    if (k.effectsApart > 0.5f && !g_apartTold.exchange(true))
        Line("vrcam: fog in the frame - the effects drawn into the scene are kept apart from the fog (%u draws so far)",
             g_apartDraws.load(std::memory_order_relaxed));
    if (g_toldWhy.exchange(0) > 0) Line("vrcam: fog in the frame - drawn again");
    return true;
}

// ---------------------------------------------------------------- the effects drawn into the scene, kept apart
// The game clears the scene at the start of its forward pass: E and K are cleared with it (0 and 1), in the same list.
bool OnClearRtv(command_list* cl, resource_view rtv, const float*, uint32_t, const rect*) {
    if (t_ours || !g_on.load(std::memory_order_relaxed) || !g_fxCount.load(std::memory_order_relaxed)) return false;
    if (!g_scenes[0].load(std::memory_order_relaxed) || !rtv.handle) return false;
    const uint64_t scene = cl->get_device()->get_resource_from_view(rtv).handle;
    if (!IsScene(scene)) return false;
    Set set;
    AcquireSRWLockShared(&g_setLock);
    set = g_set;
    ReleaseSRWLockShared(&g_setLock);
    if (!set.ready || set.owner != cl->get_device()) return false;
    const resource_desc sd = cl->get_device()->get_resource_desc(resource{scene});
    if (sd.texture.width != set.td.texture.width || sd.texture.height != set.td.texture.height) return false;
    static const float zero[4] = {0, 0, 0, 0}, one[4] = {1, 1, 1, 1};
    t_ours = true;
    cl->clear_render_target_view(set.effectsRtv, zero);
    cl->clear_render_target_view(set.keepRtv, one);
    t_ours = false;
    g_clearedGen.store(set.gen, std::memory_order_relaxed);
    g_clearedAt.store(GetTickCount64(), std::memory_order_relaxed);
    return false;   // the game's own clear goes on
}

// The ones that stay in the scene, told once each (by their pixel shader) - what is fogged with the scene and why.
std::atomic<uint32_t> g_toldEffects{0};
SRWLOCK g_toldLock = SRWLOCK_INIT;
std::vector<uint64_t> g_toldPs;   // under g_toldLock
void TellEffect(const EffectPipe& e) {
    if (g_toldEffects.load(std::memory_order_relaxed) >= 32) return;
    AcquireSRWLockExclusive(&g_toldLock);
    bool told = false;
    for (uint64_t ps : g_toldPs) told = told || ps == e.ps;
    if (!told && g_toldPs.size() < 32) g_toldPs.push_back(e.ps);
    ReleaseSRWLockExclusive(&g_toldLock);
    if (told) return;
    g_toldEffects.fetch_add(1, std::memory_order_relaxed);
    Line("vrcam: fog in the frame - an effect drawn into the scene (pixel shader %016llX, blend source %u, destination %u, op %u): %s%s",
         (unsigned long long)e.ps, e.b.src, e.b.dst, e.b.op, e.why ? "stays in the scene, fogged - " : "kept apart from the fog",
         e.why ? e.why : "");
}

// An effect's draw into the scene: drawn into E with the game's own pipeline, and into K with its coverage's copy
// (true); the game's draw goes on into the scene when it can not be (false).
template <typename DrawFn>
bool AtEffect(command_list* cl, DrawFn draw) {
    if (t_ours || !g_on.load(std::memory_order_relaxed) || !g_fxCount.load(std::memory_order_relaxed)) return false;
    ListState* st = State(cl);
    if (!st->effect || !st->fresh || !st->rtvCount || !st->rtv[0].handle || !st->dsv.handle) return false;
    device* dev = cl->get_device();
    if (!g_scenes[0].load(std::memory_order_relaxed) || !IsScene(dev->get_resource_from_view(st->rtv[0]).handle) ||
        dev->get_resource_from_view(st->dsv).handle != g_depthRes.load(std::memory_order_relaxed))
        return false;
    EffectPipe e;
    if (!FindEffect(st->effect, &e) || e.owner != dev) return false;
    TellEffect(e);
    if (e.why || !e.keep.handle) return false;   // stays in the scene: fogged with it, as before
    Set set;
    AcquireSRWLockShared(&g_setLock);
    set = g_set;
    ReleaseSRWLockShared(&g_setLock);
    if (!KeptApart(set) || set.owner != dev) return false;
    resource_view rtvs[8];
    for (uint32_t i = 0; i < st->rtvCount; ++i) rtvs[i] = st->rtv[i];
    t_ours = true;
    rtvs[0] = set.effectsRtv;   // the game's own pipeline, into E
    cl->bind_render_targets_and_depth_stencil(st->rtvCount, rtvs, st->dsv);
    draw();
    rtvs[0] = set.keepRtv;      // its coverage, into K
    cl->bind_render_targets_and_depth_stencil(st->rtvCount, rtvs, st->dsv);
    cl->bind_pipeline(pipeline_stage::all, e.keep);
    draw();
    cl->bind_render_targets_and_depth_stencil(st->rtvCount, st->rtv, st->dsv);
    cl->bind_pipeline(pipeline_stage::all, pipeline{st->effect});
    t_ours = false;
    g_apartDraws.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool OnDraw(command_list* cl, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    auto draw = [&] { cl->draw(vertices, instances, firstVertex, firstInstance); };
    return AtResolve(cl, draw) || AtEffect(cl, draw);
}
bool OnDrawIndexed(command_list* cl, uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    auto draw = [&] { cl->draw_indexed(indices, instances, firstIndex, vertexOffset, firstInstance); };
    return AtResolve(cl, draw) || AtEffect(cl, draw);
}
bool OnDrawIndirect(command_list* cl, indirect_command type, resource buffer, uint64_t offset, uint32_t count, uint32_t stride) {
    if (type != indirect_command::draw && type != indirect_command::draw_indexed) return false;
    return AtEffect(cl, [&] { cl->draw_or_dispatch_indirect(type, buffer, offset, count, stride); });
}

}  // namespace

void Register(void (*log)(const char*)) {
    if (g_registered.exchange(true)) return;
    g_log = log;
    using namespace reshade;
    register_event<addon_event::init_pipeline>(&d2rvr::D3D12Only<&OnInitPipeline>::Call);
    register_event<addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&OnDestroyPipeline>::Call);
    register_event<addon_event::init_command_list>(&d2rvr::D3D12Only<&OnInitCommandList>::Call);
    register_event<addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnResetCommandList>::Call);
    register_event<addon_event::destroy_command_list>(&d2rvr::D3D12Only<&OnDestroyCommandList>::Call);
    register_event<addon_event::bind_pipeline>(&d2rvr::D3D12Only<&OnBindPipeline>::Call);
    register_event<addon_event::bind_descriptor_tables>(&d2rvr::D3D12Only<&OnBindDescriptorTables>::Call);
    register_event<addon_event::push_descriptors>(&d2rvr::D3D12Only<&OnPushDescriptors>::Call);
    register_event<addon_event::push_constants>(&d2rvr::D3D12Only<&OnPushConstants>::Call);
    register_event<addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindTargets>::Call);
    register_event<addon_event::bind_viewports>(&d2rvr::D3D12Only<&OnBindViewports>::Call);
    register_event<addon_event::bind_scissor_rects>(&d2rvr::D3D12Only<&OnBindScissors>::Call);
    register_event<addon_event::bind_pipeline_states>(&d2rvr::D3D12Only<&OnBindStates>::Call);
    register_event<addon_event::barrier>(&d2rvr::D3D12Only<&OnBarrier>::Call);
    register_event<addon_event::destroy_resource>(&d2rvr::D3D12Only<&OnDestroyResource>::Call);
    register_event<addon_event::destroy_device>(&d2rvr::D3D12Only<&OnDestroyDevice>::Call);
    register_event<addon_event::draw>(&d2rvr::D3D12Only<&OnDraw>::Call);
    register_event<addon_event::draw_indexed>(&d2rvr::D3D12Only<&OnDrawIndexed>::Call);
    register_event<addon_event::draw_or_dispatch_indirect>(&d2rvr::D3D12Only<&OnDrawIndirect>::Call);
    register_event<addon_event::clear_render_target_view>(&d2rvr::D3D12Only<&OnClearRtv>::Call);
    Line("vrcam: fog in the frame - watching the game's transparent resolve ([render] fog_in_frame)");
}

bool Wanted() { return g_wanted.load(); }
void SetWanted(bool w) { g_wanted.store(w); }

void Configure(const Params& p) {
    AcquireSRWLockExclusive(&g_paramLock);
    g_params = p;
    ReleaseSRWLockExclusive(&g_paramLock);
    const bool on = p.on && g_registered.load();
    if (g_on.exchange(on) != on) {
        Line("vrcam: fog in the frame %s", on ? "on" : "off (not VR F4, our fog off, the fog's colour taken from the sky, or the key off)");
        if (!on) g_toldWhy.store(-1);
    }
    // old textures, two seconds on: no list recorded before still uses them
    AcquireSRWLockExclusive(&g_setLock);
    if (!g_graveyard.empty() && GetTickCount64() - g_graveAt > 2000) {
        for (Set& s : g_graveyard) Destroy(s);
        g_graveyard.clear();
    }
    ReleaseSRWLockExclusive(&g_setLock);
}

bool Armed() { return g_on.load(std::memory_order_relaxed); }
bool Drawn() { return Armed() && GetTickCount64() - g_lastDraw.load(std::memory_order_relaxed) < 250; }

}  // namespace fogframe
