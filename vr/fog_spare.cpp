// [fog] spare_effects: the fog spares fire, spells and smoke - see vr/fog_spare.h.

#include "fog_spare.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "game_device_only.h"

namespace fogspare {
namespace {

using namespace reshade::api;

// The game's ps_transparent_resolve (data/hd/shaders/ps_transparent_resolve.bin, its one DXBC container): its size
// and FNV-1a 64 (the basis vrcam's tools use, 1469598103934665603) - what ReShade hands over when the game makes the
// pipeline. Another build of the game: not found, nothing done.
constexpr uint32_t kResolveSize = 756;
constexpr uint64_t kResolveHash = 0x53183967E9589EDBull;

uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}

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
// one line per reason while it lasts (the same reason again is not logged)
std::atomic<int> g_toldWhy{-1};
void Why(int code, const char* text) {
    if (g_toldWhy.exchange(code) != code) Line("vrcam: fog spares effects - %s", text);
}

std::atomic<bool> g_registered{false}, g_wanted{false}, g_on{false};

// The game's resolve pipelines (a handful at most).
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<uint64_t> g_resolve;   // under g_lock
std::atomic<uint32_t> g_resolveCount{0};
bool IsResolve(uint64_t p) {
    AcquireSRWLockShared(&g_lock);
    bool yes = false;
    for (uint64_t r : g_resolve) if (r == p) { yes = true; break; }
    ReleaseSRWLockShared(&g_lock);
    return yes;
}

// What a list being recorded has bound (one list is recorded on one thread at a time).
thread_local command_list* t_cl = nullptr;
thread_local bool t_resolveBound = false;
thread_local resource_view t_rtv0{0};
thread_local bool t_ours = false;   // our own calls into ReShade: their events are not for us

// The game's weighted-blended targets and its additive one, as its transparent upsample binds them (three render
// targets: two R16G16B16A16_FLOAT, one R11G11B10_FLOAT, one size): the resolve reads them next. Their states as
// the game's own barriers leave them.
std::atomic<uint64_t> g_trio[3] = {};
std::atomic<uint32_t> g_state[3] = {};

// Ours: the four copies - [0] the accumulation, [1] the revealage, [2] the additive, [3] the resolve's result.
constexpr const char* kSemantic[4] = {"D2RVR_SPARE_ACCUM", "D2RVR_SPARE_REVEAL", "D2RVR_SPARE_GLOW", "D2RVR_SPARE_RESOLVED"};
struct Set {
    resource res[4] = {};
    resource_view srv[4] = {};
    resource_desc desc[4] = {};
    bool ready = false;
};
Set g_set;                         // under g_lock (made and bound at the effects, read at the resolve's draw)
std::vector<Set> g_graveyard;      // old sets, destroyed a while later: a list recorded before may still copy into them
uint32_t g_graveAge = 0;
resource_desc g_seen[4] = {};      // the game's targets as last seen at its resolve (under g_lock)
bool g_seenAny = false;
std::atomic<ULONGLONG> g_lastCopy{0};
std::atomic<uint32_t> g_copies{0};

bool SameDesc(const resource_desc& a, const resource_desc& b) {
    return a.type == b.type && a.texture.width == b.texture.width && a.texture.height == b.texture.height && a.texture.format == b.texture.format;
}

void OnInitPipeline(device*, pipeline_layout, uint32_t count, const pipeline_subobject* subs, pipeline made) {
    if (t_ours || !g_wanted.load(std::memory_order_relaxed)) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (subs[i].type != pipeline_subobject_type::pixel_shader || !subs[i].data) continue;
        const shader_desc& ps = *static_cast<const shader_desc*>(subs[i].data);
        if (!ps.code || ps.code_size != kResolveSize || Fnv(ps.code, ps.code_size) != kResolveHash) return;
        AcquireSRWLockExclusive(&g_lock);
        g_resolve.push_back(made.handle);
        g_resolveCount.store((uint32_t)g_resolve.size());
        ReleaseSRWLockExclusive(&g_lock);
        Line("vrcam: fog spares effects - the game's transparent resolve found (%u bytes, FNV-1a %016llX)", kResolveSize,
             (unsigned long long)kResolveHash);
        return;
    }
}
void OnDestroyPipeline(device*, pipeline p) {
    if (!g_resolveCount.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_resolve.size(); ++i)
        if (g_resolve[i] == p.handle) { g_resolve.erase(g_resolve.begin() + (ptrdiff_t)i); break; }
    g_resolveCount.store((uint32_t)g_resolve.size());
    ReleaseSRWLockExclusive(&g_lock);
}

void OnBindPipeline(command_list* cl, pipeline_stage stages, pipeline p) {
    if (t_ours || (stages & pipeline_stage::pixel_shader) == 0 || !g_resolveCount.load(std::memory_order_relaxed)) return;
    t_cl = cl;
    t_resolveBound = IsResolve(p.handle);
}

void OnBindTargets(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view) {
    if (t_ours || !g_wanted.load(std::memory_order_relaxed)) return;
    t_cl = cl;
    t_rtv0 = count ? rtvs[0] : resource_view{0};
    if (count != 3 || !rtvs) return;
    device* dev = cl->get_device();
    resource r[3];
    for (int i = 0; i < 3; ++i) {
        if (!rtvs[i].handle) return;
        r[i] = dev->get_resource_from_view(rtvs[i]);
        if (!r[i].handle) return;
    }
    const resource_desc a = dev->get_resource_desc(r[0]), b = dev->get_resource_desc(r[1]), c = dev->get_resource_desc(r[2]);
    if (a.texture.format != format::r16g16b16a16_float || b.texture.format != format::r16g16b16a16_float ||
        c.texture.format != format::r11g11b10_float || !SameDesc(a, b) || a.texture.width != c.texture.width || a.texture.height != c.texture.height)
        return;
    for (int i = 0; i < 3; ++i) {   // bound as render targets: in that state now, for certain
        g_trio[i].store(r[i].handle, std::memory_order_relaxed);
        g_state[i].store((uint32_t)resource_usage::render_target, std::memory_order_relaxed);
    }
}

void OnBarrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to) {
    if (t_ours || !g_wanted.load(std::memory_order_relaxed) || !res || !to) return;
    const uint64_t a = g_trio[0].load(std::memory_order_relaxed);
    if (!a) return;
    const uint64_t b = g_trio[1].load(std::memory_order_relaxed), c = g_trio[2].load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t h = res[i].handle;
        const int k = h == a ? 0 : h == b ? 1 : h == c ? 2 : -1;
        if (k >= 0) g_state[k].store((uint32_t)to[i], std::memory_order_relaxed);
    }
}

void OnDestroyResource(device*, resource r) {
    for (int i = 0; i < 3; ++i)
        if (g_trio[i].load(std::memory_order_relaxed) == r.handle) { g_trio[i].store(0); g_state[i].store(0); }
}

// The resolve's draw: the game's own draw, by us, then the four copies. False: the game's own draw goes on alone.
template <typename DrawFn>
bool AtResolve(command_list* cl, DrawFn draw) {
    if (t_ours || t_cl != cl || !t_resolveBound || !g_on.load(std::memory_order_relaxed)) return false;
    device* dev = cl->get_device();
    if (!t_rtv0.handle) return Why(1, "the resolve's target was not seen: left to the game"), false;
    resource src[4];
    resource_usage state[4];
    for (int i = 0; i < 3; ++i) {
        src[i] = {g_trio[i].load(std::memory_order_relaxed)};
        state[i] = (resource_usage)g_state[i].load(std::memory_order_relaxed);
        if (!src[i].handle) return Why(2, "the game's transparent targets were not seen: left to the game"), false;
        // the resolve reads them: a shader resource state, as the game's own barrier left it
        if ((state[i] & resource_usage::shader_resource) == 0 || (state[i] & ~resource_usage::shader_resource) != resource_usage::undefined)
            return Why(3, "the transparent targets' state is not known as a shader resource: left to the game"), false;
    }
    src[3] = dev->get_resource_from_view(t_rtv0);
    state[3] = resource_usage::render_target;
    if (!src[3].handle) return Why(1, "the resolve's target was not seen: left to the game"), false;
    resource_desc d[4];
    for (int i = 0; i < 4; ++i) d[i] = dev->get_resource_desc(src[i]);
    if (d[3].texture.format != format::r11g11b10_float || d[3].texture.width != d[0].texture.width || d[3].texture.height != d[0].texture.height)
        return Why(4, "the resolve's target is not the frame's colour target of the transparents' size: left to the game"), false;
    resource dst[4];
    bool ready = true;
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < 4; ++i) {
        g_seen[i] = d[i];
        ready = ready && g_set.ready && SameDesc(g_set.desc[i], d[i]);
        dst[i] = g_set.res[i];
    }
    g_seenAny = true;
    ReleaseSRWLockExclusive(&g_lock);
    if (!ready) return Why(5, "our copies are made at the next effects pass: this frame left to the game"), false;

    t_ours = true;
    draw();   // the game's resolve, with all its own state
    resource_usage from[8], to[8];
    resource both[8];
    for (int i = 0; i < 4; ++i) {
        both[i] = src[i]; from[i] = state[i]; to[i] = resource_usage::copy_source;
        both[4 + i] = dst[i]; from[4 + i] = resource_usage::shader_resource; to[4 + i] = resource_usage::copy_dest;
    }
    cl->barrier(8, both, from, to);
    for (int i = 0; i < 4; ++i) cl->copy_resource(src[i], dst[i]);
    cl->barrier(8, both, to, from);
    t_ours = false;
    g_lastCopy.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_copies.fetch_add(1, std::memory_order_relaxed) == 0)
        Line("vrcam: fog spares effects - the first copies of the game's transparent targets made (%ux%u)", d[3].texture.width, d[3].texture.height);
    g_toldWhy.store(0);
    return true;
}

bool OnDraw(command_list* cl, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    return AtResolve(cl, [&] { cl->draw(vertices, instances, firstVertex, firstInstance); });
}
bool OnDrawIndexed(command_list* cl, uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    return AtResolve(cl, [&] { cl->draw_indexed(indices, instances, firstIndex, vertexOffset, firstInstance); });
}

void Destroy(device* dev, Set& s) {
    for (int i = 0; i < 4; ++i) {
        if (s.srv[i].handle) dev->destroy_resource_view(s.srv[i]);
        if (s.res[i].handle) dev->destroy_resource(s.res[i]);
    }
    s = Set{};
}

}  // namespace

void Register(void (*log)(const char*)) {
    if (g_registered.exchange(true)) return;
    g_log = log;
    using namespace reshade;
    register_event<addon_event::init_pipeline>(&d2rvr::D3D12Only<&OnInitPipeline>::Call);
    register_event<addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&OnDestroyPipeline>::Call);
    register_event<addon_event::bind_pipeline>(&d2rvr::D3D12Only<&OnBindPipeline>::Call);
    register_event<addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindTargets>::Call);
    register_event<addon_event::barrier>(&d2rvr::D3D12Only<&OnBarrier>::Call);
    register_event<addon_event::destroy_resource>(&d2rvr::D3D12Only<&OnDestroyResource>::Call);
    register_event<addon_event::draw>(&d2rvr::D3D12Only<&OnDraw>::Call);
    register_event<addon_event::draw_indexed>(&d2rvr::D3D12Only<&OnDrawIndexed>::Call);
    Line("vrcam: fog spares effects - watching the game's transparent resolve ([fog] spare_effects)");
}

bool Wanted() { return g_wanted.load(); }
void SetWanted(bool w) { g_wanted.store(w); }

void Configure(bool on) {
    if (g_on.exchange(on) != on) Line("vrcam: fog spares effects %s", on ? "on" : "off (not VR F4, our fog off, or the key off)");
}

bool Bind(effect_runtime* rt) {
    if (!g_on.load(std::memory_order_relaxed) || !rt) return false;
    device* dev = rt->get_device();
    resource_desc seen[4];
    bool any;
    AcquireSRWLockShared(&g_lock);
    any = g_seenAny;
    for (int i = 0; i < 4; ++i) seen[i] = g_seen[i];
    bool same = g_set.ready;
    for (int i = 0; i < 4 && same; ++i) same = SameDesc(g_set.desc[i], seen[i]);
    ReleaseSRWLockShared(&g_lock);
    if (g_graveAge && ++g_graveAge > 120) {   // two seconds on: no list recorded before still copies into the old ones
        for (Set& s : g_graveyard) Destroy(dev, s);
        g_graveyard.clear();
        g_graveAge = 0;
    }
    if (any && !same) {
        Set made;
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) {
            const resource_desc cd(resource_type::texture_2d, seen[i].texture.width, seen[i].texture.height, 1, 1, seen[i].texture.format, 1,
                                   memory_heap::default_, resource_usage::shader_resource | resource_usage::copy_dest);
            ok = dev->create_resource(cd, nullptr, resource_usage::shader_resource, &made.res[i]) &&
                 dev->create_resource_view(made.res[i], resource_usage::shader_resource, resource_view_desc(seen[i].texture.format), &made.srv[i]);
            made.desc[i] = seen[i];
        }
        if (!ok) {
            Destroy(dev, made);
            Why(6, "our copies could not be made: the fog as before");
            return false;
        }
        made.ready = true;
        for (int i = 0; i < 4; ++i) rt->update_texture_bindings(kSemantic[i], made.srv[i], made.srv[i]);   // (drains the GPU: once a size)
        AcquireSRWLockExclusive(&g_lock);
        if (g_set.ready) { g_graveyard.push_back(g_set); g_graveAge = 1; }
        g_set = made;
        ReleaseSRWLockExclusive(&g_lock);
        Line("vrcam: fog spares effects - our copies made, %ux%u, bound to the effect", seen[3].texture.width, seen[3].texture.height);
        return false;   // this frame's copies went into none of them
    }
    return same && GetTickCount64() - g_lastCopy.load(std::memory_order_relaxed) < 250;
}

}  // namespace fogspare
