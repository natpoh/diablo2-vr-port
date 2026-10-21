// effects_layer.cpp - eyes by turns: where the game's effects are - see vr/effects_layer.h.

#include "effects_layer.h"

#include <windows.h>

#include <reshade.hpp>

#include <atomic>
#include <cstdio>

#include "game_device_only.h"

namespace effectslayer {
namespace {

using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_registered{false}, g_wanted{false};
// The layer's two targets, as seen bound (Bind); their states, from the game's barriers on them (Barrier), read without a lock on
// every barrier. A new pair is put under g_found.
std::atomic<uint64_t> g_targets[2] = {};
std::atomic<uint32_t> g_states[2] = {};
std::atomic<bool> g_known[2] = {};
std::atomic<uint32_t> g_width{0}, g_height{0};
SRWLOCK g_found = SRWLOCK_INIT;

bool Layered(const resource_desc& d) {
    return d.type == resource_type::texture_2d && d.texture.samples == 1 && d.texture.depth_or_layers == 1 &&
           (d.texture.format == format::r16g16b16a16_float || d.texture.format == format::r16g16b16a16_typeless);
}

void Bind(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    if (count < 2 || !rtvs || !rtvs[0].handle || !rtvs[1].handle || !dsv.handle || !g_wanted.load(std::memory_order_relaxed)) return;
    device* dev = cl->get_device();
    const resource a = dev->get_resource_from_view(rtvs[0]), b = dev->get_resource_from_view(rtvs[1]), ds = dev->get_resource_from_view(dsv);
    if (!a.handle || !b.handle || !ds.handle) return;
    if (a.handle == g_targets[0].load(std::memory_order_relaxed) && b.handle == g_targets[1].load(std::memory_order_relaxed)) return;
    const resource_desc da = dev->get_resource_desc(a), db = dev->get_resource_desc(b), dd = dev->get_resource_desc(ds);
    if (!Layered(da) || !Layered(db) || db.texture.width != da.texture.width || db.texture.height != da.texture.height ||
        dd.texture.width != da.texture.width || dd.texture.height != da.texture.height)
        return;
    AcquireSRWLockExclusive(&g_found);
    for (int i = 0; i < 2; ++i) g_known[i].store(false);
    g_targets[0].store(a.handle);
    g_targets[1].store(b.handle);
    g_width.store(da.texture.width);
    g_height.store(da.texture.height);
    ReleaseSRWLockExclusive(&g_found);
    if (g_log) {
        char line[192];
        snprintf(line, sizeof line, "vrcam: eyes by turns - the game's effects layer found (%ux%u, two half-float targets): the eye made keeps its own picture there",
                 da.texture.width, da.texture.height);
        g_log(line);
    }
}
bool BeginPass(command_list* cl, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds, render_pass_flags) {
    if (count < 2 || !rts || !ds) return false;
    const resource_view rtvs[2] = {rts[0].view, rts[1].view};
    Bind(cl, 2, rtvs, ds->view);
    return false;
}
void Barrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* after) {
    const uint64_t a = g_targets[0].load(std::memory_order_relaxed);
    if (!a || !res || !after) return;
    const uint64_t b = g_targets[1].load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < count; ++i) {
        const int k = res[i].handle == a ? 0 : res[i].handle == b ? 1 : -1;
        if (k < 0) continue;
        g_states[k].store(uint32_t(after[i]), std::memory_order_relaxed);
        g_known[k].store(true, std::memory_order_release);
    }
}
void Destroyed(device*, resource r) {
    if (!r.handle || (r.handle != g_targets[0].load(std::memory_order_relaxed) && r.handle != g_targets[1].load(std::memory_order_relaxed))) return;
    AcquireSRWLockExclusive(&g_found);
    for (int i = 0; i < 2; ++i) { g_targets[i].store(0); g_known[i].store(false); }
    ReleaseSRWLockExclusive(&g_found);
}

}  // namespace

void Register(void (*log)(const char*)) {
    if (g_registered.exchange(true)) return;
    g_log = log;
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&Bind>::Call);
    reshade::register_event<reshade::addon_event::begin_render_pass>(&d2rvr::D3D12Only<&BeginPass>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&Barrier>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&Destroyed>::Call);
}

void SetWanted(bool wanted) { g_wanted.store(wanted, std::memory_order_relaxed); }

bool Latest(Layer& out) {
    AcquireSRWLockShared(&g_found);
    bool ok = true;
    for (int i = 0; i < 2 && ok; ++i) {
        const uint64_t t = g_targets[i].load();
        ok = t && g_known[i].load(std::memory_order_acquire);
        out.targets[i] = reinterpret_cast<ID3D12Resource*>(t);
        out.states[i] = State(g_states[i].load());
    }
    out.width = g_width.load();
    out.height = g_height.load();
    ReleaseSRWLockShared(&g_found);
    return ok;
}

}  // namespace effectslayer
