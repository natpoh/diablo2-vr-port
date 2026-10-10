// uitrace - one frame of the game's GPU work, through ReShade, to find where the
// world ends and the interface (HUD, panels, text) begins: which render
// targets a frame draws into, in what order, what is cleared to what, what is
// copied where, and which texture is presented. Ctrl + F10 in the game writes
// d2r_vr_uitrace.txt beside d2r_vr.ini.
//
// The order is the order the game RECORDS its commands in (D3D12 records on
// several threads); each line carries its command list, so the lists can be
// read apart. Draws are counted per target, not listed.
//
// Every 8-bit colour target (and the back buffer) is also copied out right
// after the game drew into it - on the game's own command list, where its
// state is known (render target) - and saved as uitex_<id>_<w>x<h>_<fmt>.raw
// beside the trace: 16-byte header (w, h, format, row pitch), then the rows.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include <reshade.hpp>
#include "game_device_only.h"

namespace uitrace {
namespace {

using namespace reshade::api;

std::atomic<int> g_state{0};   // 0 idle, 1 armed (waits for a present), 2 recording until the next one
std::mutex g_lock;
std::string g_out;
std::wstring g_path;
uint32_t g_seq = 0;

struct Pending {
    std::string target;
    uint32_t draws = 0, dispatches = 0, indirect = 0, pipelines = 0, constants = 0, bundles = 0;
    uint64_t pipe = 0;    // the graphics pipeline of the last draw counted here
    resource rts[8] = {};
    uint32_t rtCount = 0;
    bool watch = false;   // one full-size 8-bit colour target: every draw into it is listed with its counts
    bool layer = false;   // a full-size R16G16B16A16_FLOAT target alone (the interface): draws counted per scissor rect
    bool inPass = false;
};
// Colour targets copied out this frame: game texture -> our readback copy.
struct Grab { resource src{}, copy{}; resource_desc desc{}; device* dev = nullptr; };
std::unordered_map<uint64_t, Grab> g_grabs;

bool Grabbable(const resource_desc& d) {
    const uint32_t f = (uint32_t)d.texture.format;
    return d.type == resource_type::texture_2d && (d.usage & resource_usage::render_target) != 0 && d.texture.samples <= 1 &&
           (f == 28 || f == 29 || f == 87 || f == 91 || f == 27 || f == 86 || f == 90 || f == 10 ||   // 10: R16G16B16A16_FLOAT
            // the lighting and its masks too (the act 2 town's straight line across the picture, 2026-10-07):
            // 26 R11G11B10_FLOAT, 61 R8_UNORM, 54 R16_FLOAT, 56 R16_UNORM, 34 R16G16_FLOAT
            f == 26 || f == 61 || f == 54 || f == 56 || f == 34);
}

// Copies a colour target, now in the render target state, on the list that drew it.
void GrabNow(command_list* cl, resource r, resource_usage state) {
    device* dev = cl->get_device();
    const resource_desc d = dev->get_resource_desc(r);
    if (!Grabbable(d)) return;
    Grab& g = g_grabs[r.handle];
    if (!g.copy.handle) {
        resource_desc rd(d.texture.width, d.texture.height, 1, 1, d.texture.format, 1, memory_heap::readback, resource_usage::copy_dest);
        if (!dev->create_resource(rd, nullptr, resource_usage::copy_dest, &g.copy)) { g_grabs.erase(r.handle); return; }
        g.src = r; g.desc = d; g.dev = dev;
    }
    cl->barrier(r, state, resource_usage::copy_source);
    cl->copy_texture_region(r, 0, nullptr, g.copy, 0, nullptr, filter_mode::min_mag_mip_point);
    cl->barrier(r, resource_usage::copy_source, state);
}
std::unordered_map<command_list*, Pending> g_pending;
std::unordered_set<uint64_t> g_described;

const char* FormatName(format f) {
    switch ((uint32_t)f) {
    case 2: return "R32G32B32A32_FLOAT";
    case 10: return "R16G16B16A16_FLOAT";
    case 11: return "R16G16B16A16_UNORM";
    case 24: return "R10G10B10A2_UNORM";
    case 26: return "R11G11B10_FLOAT";
    case 28: return "R8G8B8A8_UNORM";
    case 29: return "R8G8B8A8_UNORM_SRGB";
    case 34: return "R16G16_FLOAT";
    case 40: return "D32_FLOAT";
    case 41: return "R32_FLOAT";
    case 45: return "D24_UNORM_S8_UINT";
    case 54: return "R16_FLOAT";
    case 61: return "R8_UNORM";
    case 87: return "B8G8R8A8_UNORM";
    case 91: return "B8G8R8A8_UNORM_SRGB";
    default: return nullptr;
    }
}

// "#<resource> WxH FORMAT" once per resource, then "#<resource>".
std::string Describe(device* dev, resource r) {
    char b[160];
    if (r.handle == 0) return "(none)";
    if (!g_described.insert(r.handle).second) { snprintf(b, sizeof b, "#%llx", (unsigned long long)r.handle); return b; }
    const resource_desc d = dev->get_resource_desc(r);
    const char* fn = FormatName(d.texture.format);
    char fb[24]; if (!fn) { snprintf(fb, sizeof fb, "fmt%u", (uint32_t)d.texture.format); fn = fb; }
    snprintf(b, sizeof b, "#%llx %ux%u %s%s%s%s", (unsigned long long)r.handle, d.texture.width, d.texture.height, fn,
             (d.usage & resource_usage::render_target) != 0 ? " RT" : "",
             (d.usage & resource_usage::shader_resource) != 0 ? " SRV" : "",
             (d.usage & resource_usage::unordered_access) != 0 ? " UAV" : "");
    return b;
}
std::string View(device* dev, resource_view v) { return v.handle ? Describe(dev, dev->get_resource_from_view(v)) : "(none)"; }

void Line(command_list* cl, const std::string& text) {
    char b[48]; snprintf(b, sizeof b, "%5u cl%04llx ", g_seq++, (unsigned long long)(((uintptr_t)cl >> 4) & 0xFFFF));
    g_out += b; g_out += text; g_out += '\n';
}

uint64_t PsHash(uint64_t pl);   // below

// The draws counted into the target bound before: one line when it changes.
void Flush(command_list* cl) {
    auto it = g_pending.find(cl);
    if (it == g_pending.end()) return;
    const Pending& p = it->second;
    if (p.draws || p.dispatches || p.indirect || p.pipelines || p.bundles) {
        char b[240]; snprintf(b, sizeof b, "    %u draws, %u dispatches, %u indirect, %u pipelines, %u constants, %u bundles, last pipe %llx ps %016llx -> ",
                              p.draws, p.dispatches, p.indirect, p.pipelines, p.constants, p.bundles, (unsigned long long)p.pipe,
                              (unsigned long long)PsHash(p.pipe));
        Line(cl, b + p.target);
    }
    // what was drawn is copied out now, while it is still a render target
    if ((p.draws || p.indirect || p.bundles) && !p.inPass)
        for (uint32_t i = 0; i < p.rtCount; ++i) if (p.rts[i].handle) GrabNow(cl, p.rts[i], resource_usage::render_target);
    g_pending.erase(it);
}

bool Recording() { return g_state.load(std::memory_order_relaxed) == 2; }

bool OnBeginRenderPass(command_list* cl, uint32_t count, const render_pass_render_target_desc* rts, const render_pass_depth_stencil_desc* ds, render_pass_flags) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    device* dev = cl->get_device();
    std::string t = "begin pass:";
    for (uint32_t i = 0; i < count; ++i) {
        t += " [" + View(dev, rts[i].view);
        if (rts[i].load_op == render_pass_load_op::clear) {
            char b[64]; snprintf(b, sizeof b, " clear %.2f %.2f %.2f %.2f", rts[i].clear_color[0], rts[i].clear_color[1], rts[i].clear_color[2], rts[i].clear_color[3]);
            t += b;
        }
        t += "]";
    }
    if (ds) t += " depth " + View(dev, ds->view);
    Line(cl, t);
    Pending& p = g_pending[cl];
    p.target = t.substr(12);
    p.inPass = true;
    return false;
}

bool OnEndRenderPass(command_list* cl) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    return false;
}

void OnBindRenderTargets(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view dsv) {
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    Flush(cl);
    device* dev = cl->get_device();
    std::string t;
    for (uint32_t i = 0; i < count; ++i) t += " [" + View(dev, rtvs[i]) + "]";
    if (dsv.handle) t += " depth " + View(dev, dsv);
    if (t.empty()) t = " (nothing)";
    Line(cl, "bind:" + t);
    Pending& p = g_pending[cl];
    p.target = t;
    p.rtCount = std::min<uint32_t>(count, 8);
    for (uint32_t i = 0; i < p.rtCount; ++i) p.rts[i] = rtvs[i].handle ? dev->get_resource_from_view(rtvs[i]) : resource{};
    p.watch = false;
    p.layer = false;
    if (count == 1 && p.rts[0].handle) {
        const resource_desc d = dev->get_resource_desc(p.rts[0]);
        p.watch = Grabbable(d) && d.texture.width >= 1000 && d.texture.format != format::r16g16b16a16_float;
        p.layer = d.texture.format == format::r16g16b16a16_float && d.texture.width >= 640;
    }
}

// On the interface's layer: the draws so far under the old scissor rect, then the new one.
void OnScissor(command_list* cl, uint32_t, uint32_t count, const rect* rects) {
    if (!Recording() || count == 0) return;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_pending.find(cl);
    if (it == g_pending.end() || !it->second.layer) return;
    Pending& p = it->second;
    if (p.draws) {
        char b[64]; snprintf(b, sizeof b, "      %u draws", p.draws);
        Line(cl, b);
        p.draws = 0;
    }
    char b[96]; snprintf(b, sizeof b, "      scissor %d %d %d %d", rects[0].left, rects[0].top, rects[0].right, rects[0].bottom);
    Line(cl, b);
}

// Each graphics pipeline's pixel shader, hashed (FNV-1a 64 of its bytecode) as the game creates it:
// a pipeline's handle changes with every run, its shader does not. [render] skip_shaders (vrcam) lists
// shaders whose draws are skipped for good - the act 2 town's straight veil across the picture
// (2026-10-07). The handles to skip are known as they are created; a bind of one marks its list
// (thread-local: a list records on one thread), and the draws on it are skipped until another bind.
std::mutex g_hashLock;
std::unordered_map<uint64_t, uint64_t> g_psHash;   // pipeline -> its pixel shader's hash
constexpr int kSkipMax = 8;
std::atomic<uint64_t> g_skipHash[kSkipMax];        // the shaders to skip (0 = empty)
std::atomic<uint64_t> g_skipHandle[64];            // pipelines made of them (0 = empty)
std::atomic<int> g_skipHandles{0};
uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}
bool SkipHash(uint64_t h) {
    if (!h) return false;
    for (int i = 0; i < kSkipMax; ++i) if (g_skipHash[i].load(std::memory_order_relaxed) == h) return true;
    return false;
}
void AddSkipHandle(uint64_t pl) {
    const int n = g_skipHandles.load();
    for (int i = 0; i < n; ++i) if (g_skipHandle[i].load() == pl) return;
    if (n < 64) { g_skipHandle[n].store(pl); g_skipHandles.store(n + 1); }
}
bool SkipHandle(uint64_t pl) {
    const int n = g_skipHandles.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) if (g_skipHandle[i].load(std::memory_order_relaxed) == pl) return true;
    return false;
}
void OnInitPipeline(device*, pipeline_layout, uint32_t count, const pipeline_subobject* subs, pipeline pl) {
    for (uint32_t i = 0; i < count; ++i) {
        if (subs[i].type != pipeline_subobject_type::pixel_shader || !subs[i].data) continue;
        const shader_desc* sd = (const shader_desc*)subs[i].data;
        if (!sd->code || !sd->code_size) continue;
        const uint64_t h = Fnv(sd->code, sd->code_size);
        { std::lock_guard<std::mutex> g(g_hashLock); g_psHash[pl.handle] = h; }
        if (SkipHash(h)) AddSkipHandle(pl.handle);
        break;
    }
}
void OnDestroyPipeline(device*, pipeline pl) {
    std::lock_guard<std::mutex> g(g_hashLock);
    g_psHash.erase(pl.handle);
}
uint64_t PsHash(uint64_t pl) {
    std::lock_guard<std::mutex> g(g_hashLock);
    auto it = g_psHash.find(pl);
    return it == g_psHash.end() ? 0 : it->second;
}
struct ListSkip { command_list* cl = nullptr; bool skip = false; };
thread_local ListSkip t_skip;

// [debug] skip_pipeline (vrcam): every draw with this graphics pipeline is skipped - to find by
// elimination which of the game's passes draws something (the act 2 town's line, 2026-10-07).
// Nothing in the game's memory is changed; 0 = off.
std::atomic<uint64_t> g_skipPipe{0};
std::mutex g_pipeLock;
std::unordered_map<command_list*, uint64_t> g_lastPipe;   // the last graphics pipeline bound on each list
uint64_t LastPipe(command_list* cl) {
    std::lock_guard<std::mutex> g(g_pipeLock);
    auto it = g_lastPipe.find(cl);
    return it == g_lastPipe.end() ? 0 : it->second;
}

void OnBindPipeline(command_list* cl, pipeline_stage stages, pipeline pl) {
    if (g_skipHandles.load(std::memory_order_relaxed) &&
        ((stages & pipeline_stage::pixel_shader) != 0 || stages == pipeline_stage::all || stages == pipeline_stage::all_graphics))
        t_skip = {cl, SkipHandle(pl.handle)};
    // only while looked for: a lock on every bind of every frame otherwise costs the game for nothing
    if ((g_skipPipe.load(std::memory_order_relaxed) || Recording()) &&
        ((stages & pipeline_stage::pixel_shader) != 0 || stages == pipeline_stage::all || stages == pipeline_stage::all_graphics)) {
        std::lock_guard<std::mutex> g(g_pipeLock);
        g_lastPipe[cl] = pl.handle;
    }
    if (Recording()) { std::lock_guard<std::mutex> g(g_lock); ++g_pending[cl].pipelines; }
}
void OnPushConstants(command_list* cl, shader_stage, pipeline_layout, uint32_t, uint32_t, uint32_t, const void*) {
    if (Recording()) { std::lock_guard<std::mutex> g(g_lock); ++g_pending[cl].constants; }
}
void OnExecuteSecondary(command_list* cl, command_list* secondary) {
    if (!Recording()) return;
    std::lock_guard<std::mutex> g(g_lock);
    ++g_pending[cl].bundles;
    char b[64]; snprintf(b, sizeof b, "execute bundle / secondary list cl%04llx", (unsigned long long)(((uintptr_t)secondary >> 4) & 0xFFFF));
    Line(cl, b);
}

bool OnClear(command_list* cl, resource_view rtv, const float c[4], uint32_t, const rect*) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    char b[80]; snprintf(b, sizeof b, " to %.2f %.2f %.2f %.2f", c[0], c[1], c[2], c[3]);
    Line(cl, "clear " + View(cl->get_device(), rtv) + b);
    return false;
}

bool OnDraw(command_list* cl, uint32_t vertices, uint32_t instances, uint32_t, uint32_t) {
    if (t_skip.skip && t_skip.cl == cl) return true;   // [render] skip_shaders
    const uint64_t skip = g_skipPipe.load(std::memory_order_relaxed);
    const uint64_t pipe = (skip || Recording()) ? LastPipe(cl) : 0;
    if (skip && pipe == skip) return true;
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Pending& p = g_pending[cl];
    ++p.draws;
    p.pipe = pipe;
    if (p.watch) { char b[96]; snprintf(b, sizeof b, "      draw %u vertices x %u instances", vertices, instances); Line(cl, b); }
    return false;
}
bool OnDrawIndexed(command_list* cl, uint32_t indices, uint32_t instances, uint32_t, int32_t, uint32_t) {
    if (t_skip.skip && t_skip.cl == cl) return true;   // [render] skip_shaders
    const uint64_t skip = g_skipPipe.load(std::memory_order_relaxed);
    const uint64_t pipe = (skip || Recording()) ? LastPipe(cl) : 0;
    if (skip && pipe == skip) return true;
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    Pending& p = g_pending[cl];
    ++p.draws;
    p.pipe = pipe;
    if (p.watch) { char b[96]; snprintf(b, sizeof b, "      draw indexed %u indices x %u instances", indices, instances); Line(cl, b); }
    return false;
}
bool OnDispatch(command_list* cl, uint32_t, uint32_t, uint32_t) {
    if (Recording()) { std::lock_guard<std::mutex> g(g_lock); ++g_pending[cl].dispatches; }
    return false;
}
bool OnIndirect(command_list* cl, indirect_command, resource, uint64_t, uint32_t, uint32_t) {
    if (Recording()) { std::lock_guard<std::mutex> g(g_lock); ++g_pending[cl].indirect; }
    return false;
}

bool OnCopy(command_list* cl, resource src, resource dst) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = cl->get_device();
    Line(cl, "copy " + Describe(dev, src) + " -> " + Describe(dev, dst));
    return false;
}
bool OnCopyRegion(command_list* cl, resource src, uint32_t, const subresource_box*, resource dst, uint32_t, const subresource_box*, filter_mode) {
    return OnCopy(cl, src, dst);
}
bool OnResolve(command_list* cl, resource src, uint32_t, const subresource_box*, resource dst, uint32_t, uint32_t, uint32_t, uint32_t, format) {
    if (!Recording()) return false;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = cl->get_device();
    Line(cl, "resolve " + Describe(dev, src) + " -> " + Describe(dev, dst));
    return false;
}

// The copies, now that the GPU is done with the frame: one .raw file each.
void SaveGrabs(command_queue* queue) {
    queue->wait_idle();
    std::wstring dir = g_path;
    dir.resize(dir.find_last_of(L'\\') + 1);
    for (auto& [handle, g] : g_grabs) {
        subresource_data data{};
        if (g.dev->map_texture_region(g.copy, 0, nullptr, map_access::read_only, &data) && data.data) {
            wchar_t name[96];
            swprintf_s(name, L"uitex_%llx_%ux%u_%u.raw", (unsigned long long)handle, g.desc.texture.width, g.desc.texture.height, (uint32_t)g.desc.texture.format);
            if (FILE* f = _wfopen((dir + name).c_str(), L"wb")) {
                const uint32_t head[4] = {g.desc.texture.width, g.desc.texture.height, (uint32_t)g.desc.texture.format, data.row_pitch};
                fwrite(head, 4, 4, f);
                for (uint32_t y = 0; y < g.desc.texture.height; ++y) fwrite((const uint8_t*)data.data + (size_t)y * data.row_pitch, 1, data.row_pitch, f);
                fclose(f);
            }
            g.dev->unmap_texture_region(g.copy, 0);
            char b[96]; snprintf(b, sizeof b, "# saved #%llx\n", (unsigned long long)handle);
            g_out += b;
        }
        g.dev->destroy_resource(g.copy);
    }
    g_grabs.clear();
}

void OnPresent(command_queue* queue, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    const int s = g_state.load();
    if (s == 0) return;
    std::lock_guard<std::mutex> g(g_lock);
    device* dev = sc->get_device();
    const std::string bb = Describe(dev, sc->get_current_back_buffer());
    if (s == 1) {
        g_out = "# one frame of the game's GPU work, as recorded (see uitrace.cpp)\n# present before: back buffer " + bb + "\n";
        g_seq = 0; g_pending.clear(); g_described.clear();
        g_state.store(2);
        return;
    }
    for (auto it = g_pending.begin(); it != g_pending.end();) {   // lists closed already: counts only, no copies
        it->second.inPass = true;
        command_list* cl = it->first; ++it; Flush(cl);
    }
    g_out += "# present: back buffer " + bb + "\n";
    {   // the back buffer as presented
        command_list* imm = queue->get_immediate_command_list();
        GrabNow(imm, sc->get_current_back_buffer(), resource_usage::present);
        queue->flush_immediate_command_list();
    }
    SaveGrabs(queue);
    if (FILE* f = _wfopen(g_path.c_str(), L"wb")) { fwrite(g_out.data(), 1, g_out.size(), f); fclose(f); }
    g_out.clear();
    g_state.store(0);
}

}  // namespace

void Register() {
    reshade::register_event<reshade::addon_event::begin_render_pass>(&d2rvr::D3D12Only<&OnBeginRenderPass>::Call);
    reshade::register_event<reshade::addon_event::end_render_pass>(&d2rvr::D3D12Only<&OnEndRenderPass>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindRenderTargets>::Call);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(&d2rvr::D3D12Only<&OnClear>::Call);
    reshade::register_event<reshade::addon_event::draw>(&d2rvr::D3D12Only<&OnDraw>::Call);
    reshade::register_event<reshade::addon_event::draw_indexed>(&d2rvr::D3D12Only<&OnDrawIndexed>::Call);
    reshade::register_event<reshade::addon_event::dispatch>(&d2rvr::D3D12Only<&OnDispatch>::Call);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(&d2rvr::D3D12Only<&OnIndirect>::Call);
    reshade::register_event<reshade::addon_event::copy_resource>(&d2rvr::D3D12Only<&OnCopy>::Call);
    reshade::register_event<reshade::addon_event::copy_texture_region>(&d2rvr::D3D12Only<&OnCopyRegion>::Call);
    reshade::register_event<reshade::addon_event::resolve_texture_region>(&d2rvr::D3D12Only<&OnResolve>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    reshade::register_event<reshade::addon_event::bind_pipeline>(&d2rvr::D3D12Only<&OnBindPipeline>::Call);
    reshade::register_event<reshade::addon_event::init_pipeline>(&d2rvr::D3D12Only<&OnInitPipeline>::Call);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&OnDestroyPipeline>::Call);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(&d2rvr::D3D12Only<&OnScissor>::Call);
    reshade::register_event<reshade::addon_event::push_constants>(&d2rvr::D3D12Only<&OnPushConstants>::Call);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(&d2rvr::D3D12Only<&OnExecuteSecondary>::Call);
}

// Records the next whole frame into path.
bool Arm(const std::wstring& path) {
    int idle = 0;
    if (!g_state.compare_exchange_strong(idle, 1)) return false;
    std::lock_guard<std::mutex> g(g_lock);
    g_path = path;
    return true;
}

bool Busy() { return g_state.load() != 0; }

// [debug] skip_pipeline: 0 = off
void SetSkip(uint64_t pipe) { g_skipPipe.store(pipe); }
// [render] skip_shaders: up to 8 pixel shader hashes; pipelines already made of them are found again
void SetSkipShaders(const uint64_t* hashes, int n) {
    for (int i = 0; i < kSkipMax; ++i) g_skipHash[i].store(i < n ? hashes[i] : 0);
    g_skipHandles.store(0);
    std::lock_guard<std::mutex> g(g_hashLock);
    for (const auto& kv : g_psHash) if (SkipHash(kv.second)) AddSkipHandle(kv.first);
}

}  // namespace uitrace
