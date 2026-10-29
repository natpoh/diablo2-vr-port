// native_rsapi.cpp - see native_rsapi.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <reshade_api.hpp>
#include "native_d3d12.h"
#include "native_rsapi.h"

namespace hud::native {   // vr/hud.cpp: its event handlers, for the plugin's own hooks
void Clear(reshade::api::command_list* cl, reshade::api::resource_view rtv, const float c[4], uint32_t n, const reshade::api::rect* r);
void Bind(reshade::api::command_list* cl, uint32_t count, const reshade::api::resource_view* rtvs);
void Scissors(reshade::api::command_list* cl, uint32_t count, const reshade::api::rect* rects);
void Viewports(reshade::api::command_list* cl, uint32_t count, const reshade::api::viewport* vps);
void Reset(reshade::api::command_list* cl);
}

namespace nrs {
namespace {

using namespace reshade::api;

// What ReShade's D3D12 back end makes of a usage as a state (only those hud.cpp moves between, and their kin).
D3D12_RESOURCE_STATES StateOf(resource_usage u) {
    const uint32_t v = (uint32_t)u;
    if (u == resource_usage::undefined || u == resource_usage::general) return D3D12_RESOURCE_STATE_COMMON;
    if (u == resource_usage::present) return D3D12_RESOURCE_STATE_PRESENT;
    D3D12_RESOURCE_STATES s = D3D12_RESOURCE_STATE_COMMON;
    if (v & (uint32_t)resource_usage::render_target) s |= D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (v & (uint32_t)resource_usage::shader_resource_pixel) s |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (v & (uint32_t)resource_usage::shader_resource_non_pixel) s |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (v & (uint32_t)resource_usage::unordered_access) s |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (v & (uint32_t)resource_usage::depth_stencil_write) s |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
    else if (v & (uint32_t)resource_usage::depth_stencil_read) s |= D3D12_RESOURCE_STATE_DEPTH_READ;
    if (v & (uint32_t)resource_usage::copy_dest) s |= D3D12_RESOURCE_STATE_COPY_DEST;
    if (v & (uint32_t)resource_usage::copy_source) s |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (v & (uint32_t)resource_usage::resolve_dest) s |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
    if (v & (uint32_t)resource_usage::resolve_source) s |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    return s;
}

// Our recording on the game's list: its calls are not the game's (the hooks' sinks stay quiet), set back as it was after.
struct Ours {
    bool was;
    Ours() : was(natived3d::Ours()) { natived3d::SetOurs(true); }
    ~Ours() { natived3d::SetOurs(was); }
};

// The views made through the stand-in: CPU descriptor heaps of our own, a slot each.
struct View { ID3D12Resource* res = nullptr; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; };
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<uint64_t, View> g_views;

class Device final : public device {
public:
    explicit Device(ID3D12Device* d) : d_(d) {}

    uint64_t get_native() const override { return (uint64_t)(uintptr_t)d_; }
    void get_private_data(const uint8_t[16], uint64_t* data) const override { if (data) *data = 0; }
    void set_private_data(const uint8_t[16], const uint64_t) override {}
    device_api get_api() const override { return device_api::d3d12; }

    bool create_resource(const resource_desc& desc, const subresource_data*, resource_usage initial_state, resource* out_resource, void** shared_handle) override {
        if (out_resource) *out_resource = {0};
        if (desc.type != resource_type::texture_2d || !out_resource) return false;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = desc.texture.width;
        rd.Height = desc.texture.height;
        rd.DepthOrArraySize = std::max<uint16_t>(1, desc.texture.depth_or_layers);
        rd.MipLevels = std::max<uint16_t>(1, desc.texture.levels);
        rd.Format = (DXGI_FORMAT)desc.texture.format;   // (ReShade's formats are DXGI's numbers)
        rd.SampleDesc.Count = std::max<uint16_t>(1, desc.texture.samples);
        const uint32_t u = (uint32_t)desc.usage;
        if (u & (uint32_t)resource_usage::render_target) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (u & (uint32_t)resource_usage::unordered_access) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const bool shared = ((uint32_t)desc.flags & (uint32_t)resource_flags::shared) != 0;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        ID3D12Resource* r = nullptr;
        if (FAILED(d_->CreateCommittedResource(&hp, shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE, &rd, StateOf(initial_state), nullptr,
                                               __uuidof(ID3D12Resource), (void**)&r)) || !r)
            return false;
        r->SetName(L"d2rvr hud texture");
        if (shared && shared_handle) {
            HANDLE h = nullptr;
            if (FAILED(d_->CreateSharedHandle(r, nullptr, GENERIC_ALL, nullptr, &h))) h = nullptr;
            *shared_handle = h;
        }
        *out_resource = {(uint64_t)(uintptr_t)r};
        return true;
    }
    void destroy_resource(resource res) override {
        if (res.handle) ((ID3D12Resource*)(uintptr_t)res.handle)->Release();
    }
    resource_desc get_resource_desc(resource res) const override {
        resource_desc out;
        if (!res.handle) return out;
        ID3D12Resource* r = (ID3D12Resource*)(uintptr_t)res.handle;
        const D3D12_RESOURCE_DESC d = r->GetDesc();
        uint32_t u = 0;
        if (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) u |= (uint32_t)resource_usage::render_target;
        if (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) u |= (uint32_t)resource_usage::depth_stencil;
        if (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) u |= (uint32_t)resource_usage::unordered_access;
        if (!(d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) u |= (uint32_t)resource_usage::shader_resource;
        u |= (uint32_t)resource_usage::copy_dest | (uint32_t)resource_usage::copy_source;
        out.usage = (resource_usage)u;
        out.heap = memory_heap::default_;
        if (d.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            out.type = resource_type::buffer;
            out.buffer.size = d.Width;
            return out;
        }
        out.type = d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? resource_type::texture_3d
                   : d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D ? resource_type::texture_1d : resource_type::texture_2d;
        out.texture.width = (uint32_t)d.Width;
        out.texture.height = d.Height;
        out.texture.depth_or_layers = d.DepthOrArraySize;
        out.texture.levels = d.MipLevels;
        out.texture.format = (format)d.Format;
        out.texture.samples = (uint16_t)d.SampleDesc.Count;
        return out;
    }
    bool create_resource_view(resource res, resource_usage usage_type, const resource_view_desc& desc, resource_view* out_view) override {
        if (out_view) *out_view = {0};
        if (!res.handle || !out_view) return false;
        ID3D12Resource* r = (ID3D12Resource*)(uintptr_t)res.handle;
        const bool rtv = usage_type == resource_usage::render_target;
        if (!rtv && usage_type != resource_usage::shader_resource) return false;
        const DXGI_FORMAT f = desc.format != format::unknown ? (DXGI_FORMAT)desc.format : r->GetDesc().Format;
        D3D12_CPU_DESCRIPTOR_HANDLE h{};
        if (!Slot(rtv, &h)) return false;
        if (rtv) {
            D3D12_RENDER_TARGET_VIEW_DESC v{};
            v.Format = f;
            v.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            Ours o;   // (not one of the game's views for the interface's own map either way: ours is looked up first)
            d_->CreateRenderTargetView(r, &v, h);
        } else {
            D3D12_SHADER_RESOURCE_VIEW_DESC v{};
            v.Format = f;
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            v.Texture2D.MipLevels = (UINT)-1;
            d_->CreateShaderResourceView(r, &v, h);
        }
        AcquireSRWLockExclusive(&g_lock);
        g_views[h.ptr] = View{r, f};
        ReleaseSRWLockExclusive(&g_lock);
        *out_view = {(uint64_t)h.ptr};
        return true;
    }
    void destroy_resource_view(resource_view view) override {
        if (!view.handle) return;
        AcquireSRWLockExclusive(&g_lock);
        g_views.erase(view.handle);
        for (Heap& hp : heaps_) {
            const SIZE_T base = hp.cpu.ptr;
            if (hp.heap && view.handle >= base && view.handle < base + (SIZE_T)kSlots * hp.inc) hp.free.push_back((UINT)((view.handle - base) / hp.inc));
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    resource get_resource_from_view(resource_view view) const override {
        uint32_t f = 0;
        return {(uint64_t)(uintptr_t)ResourceOfView(view.handle, &f)};
    }
    void set_resource_name(resource res, const char* name) override {
        if (!res.handle || !name) return;
        wchar_t w[128];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 128);
        ((ID3D12Resource*)(uintptr_t)res.handle)->SetName(w);
    }

    // ---- device: the rest, unused by hud.cpp (nothing done)
    bool check_capability(device_caps capability) const override { return false; }
    bool check_format_support(format format, resource_usage usage) const override { return false; }
    bool create_sampler(const sampler_desc &desc, sampler *out_sampler) override { return false; }
    void destroy_sampler(sampler sampler) override {}
    resource_view_desc get_resource_view_desc(resource_view view) const override { return {}; }
    bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void **out_data) override { return false; }
    void unmap_buffer_region(resource resource) override {}
    bool map_texture_region(resource resource, uint32_t subresource, const subresource_box *box, map_access access, subresource_data *out_data) override { return false; }
    void unmap_texture_region(resource resource, uint32_t subresource) override {}
    void update_buffer_region(const void *data, resource dest, uint64_t dest_offset, uint64_t size) override {}
    void update_texture_region(const subresource_data &data, resource dest, uint32_t dest_subresource, const subresource_box *dest_box) override {}
    bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject *subobjects, pipeline *out_pipeline) override { return false; }
    void destroy_pipeline(pipeline pipeline) override {}
    bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param *params, pipeline_layout *out_layout) override { return false; }
    void destroy_pipeline_layout(pipeline_layout layout) override {}
    bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table *out_tables) override { return false; }
    void free_descriptor_tables(uint32_t count, const descriptor_table *tables) override {}
    void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap *out_heap, uint32_t *out_offset) const override {}
    void copy_descriptor_tables(uint32_t count, const descriptor_table_copy *copies) override {}
    void update_descriptor_tables(uint32_t count, const descriptor_table_update *updates) override {}
    bool create_query_heap(query_type type, uint32_t count, query_heap *out_heap) override { return false; }
    void destroy_query_heap(query_heap heap) override {}
    bool get_query_heap_results(query_heap heap, query_type type, uint32_t first, uint32_t count, void *results, uint32_t stride) override { return false; }
    void set_resource_view_name(resource_view view, const char *name) override {}
    bool create_fence(uint64_t initial_value, fence_flags flags, fence *out_fence, void **shared_handle) override { return false; }
    void destroy_fence(fence fence) override {}
    uint64_t get_completed_fence_value(fence fence) const override { return {}; }
    bool wait(fence fence, uint64_t value, uint64_t timeout) override { return false; }
    bool signal(fence fence, uint64_t value) override { return false; }
    bool get_property(device_properties property, void *data) const override { return false; }
    uint64_t get_resource_view_gpu_address(resource_view view) const override { return {}; }
    void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input *inputs, uint64_t *out_size, uint64_t *out_build_scratch_size, uint64_t *out_update_scratch_size) const override {}
    bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void *out_handles) override { return false; }

private:
    static constexpr UINT kSlots = 256;
    struct Heap { ID3D12DescriptorHeap* heap = nullptr; D3D12_CPU_DESCRIPTOR_HANDLE cpu{}; UINT inc = 0, next = 0; std::vector<UINT> free; };
    Heap heaps_[2];   // render-target views, shader-resource views
    bool Slot(bool rtv, D3D12_CPU_DESCRIPTOR_HANDLE* out) {
        AcquireSRWLockExclusive(&g_lock);
        Heap& hp = heaps_[rtv ? 0 : 1];
        bool ok = true;
        if (!hp.heap) {
            D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.Type = rtv ? D3D12_DESCRIPTOR_HEAP_TYPE_RTV : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.NumDescriptors = kSlots;
            ok = SUCCEEDED(d_->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&hp.heap)) && hp.heap;
            if (ok) {
                hp.cpu = hp.heap->GetCPUDescriptorHandleForHeapStart();
                hp.inc = d_->GetDescriptorHandleIncrementSize(hd.Type);
            }
        }
        UINT slot = 0;
        if (ok && !hp.free.empty()) { slot = hp.free.back(); hp.free.pop_back(); }
        else if (ok && hp.next < kSlots) slot = hp.next++;
        else ok = false;
        if (ok) out->ptr = hp.cpu.ptr + (SIZE_T)slot * hp.inc;
        ReleaseSRWLockExclusive(&g_lock);
        return ok;
    }
    ID3D12Device* d_;
};

Device* DeviceOf(ID3D12Device* d) {
    static std::unordered_map<ID3D12Device*, Device*> devs;   // (one: the game's; never let go)
    AcquireSRWLockExclusive(&g_lock);
    Device*& p = devs[d];
    if (!p) p = new Device(d);
    Device* out = p;
    ReleaseSRWLockExclusive(&g_lock);
    return out;
}

class List final : public command_list {
public:
    explicit List(ID3D12GraphicsCommandList* l) : l_(l) {
        ID3D12Device* d = nullptr;
        if (SUCCEEDED(l->GetDevice(__uuidof(ID3D12Device), (void**)&d)) && d) {
            dev_ = DeviceOf(d);
            d->Release();
        }
    }

    uint64_t get_native() const override { return (uint64_t)(uintptr_t)l_; }
    void get_private_data(const uint8_t[16], uint64_t* data) const override { if (data) *data = 0; }
    void set_private_data(const uint8_t[16], const uint64_t) override {}
    device* get_device() override { return dev_; }

    void barrier(uint32_t count, const resource* resources, const resource_usage* old_states, const resource_usage* new_states) override {
        std::vector<D3D12_RESOURCE_BARRIER> b;
        for (uint32_t i = 0; i < count; ++i) {
            if (!resources[i].handle) continue;
            D3D12_RESOURCE_BARRIER x{};
            x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            x.Transition.pResource = (ID3D12Resource*)(uintptr_t)resources[i].handle;
            x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            x.Transition.StateBefore = StateOf(old_states[i]);
            x.Transition.StateAfter = StateOf(new_states[i]);
            if (x.Transition.StateBefore != x.Transition.StateAfter) b.push_back(x);
        }
        if (b.empty()) return;
        Ours o;
        l_->ResourceBarrier((UINT)b.size(), b.data());
    }
    void bind_render_targets_and_depth_stencil(uint32_t count, const resource_view* rtvs, resource_view dsv) override {
        D3D12_CPU_DESCRIPTOR_HANDLE h[8] = {};
        count = std::min<uint32_t>(count, 8);
        for (uint32_t i = 0; i < count; ++i) h[i].ptr = (SIZE_T)rtvs[i].handle;
        D3D12_CPU_DESCRIPTOR_HANDLE d{(SIZE_T)dsv.handle};
        Ours o;
        l_->OMSetRenderTargets(count, count ? h : nullptr, FALSE, dsv.handle ? &d : nullptr);
    }
    void copy_resource(resource source, resource dest) override {
        if (!source.handle || !dest.handle) return;
        Ours o;
        l_->CopyResource((ID3D12Resource*)(uintptr_t)dest.handle, (ID3D12Resource*)(uintptr_t)source.handle);
    }
    void copy_texture_region(resource source, uint32_t source_subresource, const subresource_box* source_box, resource dest, uint32_t dest_subresource,
                             const subresource_box* dest_box, filter_mode) override {
        if (!source.handle || !dest.handle) return;
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = (ID3D12Resource*)(uintptr_t)dest.handle;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.SubresourceIndex = dest_subresource;
        from.pResource = (ID3D12Resource*)(uintptr_t)source.handle;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.SubresourceIndex = source_subresource;
        D3D12_BOX box{};
        if (source_box) box = {source_box->left, source_box->top, source_box->front, source_box->right, source_box->bottom, source_box->back};
        Ours o;
        l_->CopyTextureRegion(&to, dest_box ? dest_box->left : 0, dest_box ? dest_box->top : 0, dest_box ? dest_box->front : 0, &from,
                              source_box ? &box : nullptr);
    }
    void clear_render_target_view(resource_view rtv, const float color[4], uint32_t rect_count, const rect* rects) override {
        if (!rtv.handle) return;
        Ours o;
        l_->ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE{(SIZE_T)rtv.handle}, color, rect_count, (const D3D12_RECT*)rects);
    }

    // ---- command_list: the rest, unused by hud.cpp (nothing done)
    void end_render_pass() override {}
    void bind_pipeline(pipeline_stage stages, pipeline pipeline) override {}
    void bind_pipeline_states(uint32_t count, const dynamic_state *states, const uint32_t *values) override {}
    void bind_viewports(uint32_t first, uint32_t count, const viewport *viewports) override {}
    void bind_scissor_rects(uint32_t first, uint32_t count, const rect *rects) override {}
    void push_constants(shader_stage stages, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void *values) override {}
    void push_descriptors(shader_stage stages, pipeline_layout layout, uint32_t param, const descriptor_table_update &update) override {}
    void bind_descriptor_tables(shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table *tables) override {}
    void bind_index_buffer(resource buffer, uint64_t offset, uint32_t index_size) override {}
    void bind_vertex_buffers(uint32_t first, uint32_t count, const resource *buffers, const uint64_t *offsets, const uint32_t *strides) override {}
    void bind_stream_output_buffers(uint32_t first, uint32_t count, const resource *buffers, const uint64_t *offsets, const uint64_t *max_sizes, const resource *counter_buffers, const uint64_t *counter_offsets) override {}
    void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) override {}
    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) override {}
    void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override {}
    void draw_or_dispatch_indirect(indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride) override {}
    void copy_buffer_region(resource source, uint64_t source_offset, resource dest, uint64_t dest_offset, uint64_t size) override {}
    void copy_buffer_to_texture(resource source, uint64_t source_offset, uint32_t row_length, uint32_t slice_height, resource dest, uint32_t dest_subresource, const subresource_box *dest_box) override {}
    void copy_texture_to_buffer(resource source, uint32_t source_subresource, const subresource_box *source_box, resource dest, uint64_t dest_offset, uint32_t row_length, uint32_t slice_height) override {}
    void resolve_texture_region(resource source, uint32_t source_subresource, const subresource_box *source_box, resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, format format) override {}
    void clear_depth_stencil_view(resource_view dsv, const float *depth, const uint8_t *stencil, uint32_t rect_count, const rect *rects) override {}
    void clear_unordered_access_view_uint(resource_view uav, const uint32_t values[4], uint32_t rect_count, const rect *rects) override {}
    void clear_unordered_access_view_float(resource_view uav, const float values[4], uint32_t rect_count, const rect *rects) override {}
    void generate_mipmaps(resource_view srv) override {}
    void begin_query(query_heap heap, query_type type, uint32_t index) override {}
    void end_query(query_heap heap, query_type type, uint32_t index) override {}
    void copy_query_heap_results(query_heap heap, query_type type, uint32_t first, uint32_t count, resource dest, uint64_t dest_offset, uint32_t stride) override {}
    void begin_debug_event(const char *label, const float color[4]) override {}
    void end_debug_event() override {}
    void insert_debug_marker(const char *label, const float color[4]) override {}
    void dispatch_mesh(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) override {}
    void dispatch_rays(resource raygen, uint64_t raygen_offset, uint64_t raygen_size, resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth) override {}
    void copy_acceleration_structure(resource_view source, resource_view dest, acceleration_structure_copy_mode mode) override {}
    void build_acceleration_structure(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input *inputs, resource scratch, uint64_t scratch_offset, resource_view source, resource_view dest, acceleration_structure_build_mode mode) override {}
    void query_acceleration_structures(uint32_t count, const resource_view *acceleration_structures, query_heap heap, query_type type, uint32_t first) override {}
    void update_buffer_region(const void *data, resource dest, uint64_t dest_offset, uint64_t size) override {}
    void update_texture_region(const subresource_data &data, resource dest, uint32_t dest_subresource, const subresource_box *dest_box) override {}
    void begin_render_pass2(uint32_t count, const render_pass_render_target_desc *rts, const render_pass_depth_stencil_desc *ds, render_pass_flags flags) override {}

private:
    ID3D12GraphicsCommandList* l_;
    Device* dev_ = nullptr;
};

SRWLOCK g_listLock = SRWLOCK_INIT;
std::unordered_map<ID3D12GraphicsCommandList*, List*> g_lists;   // (made once a list, kept: the game's lists live as long as it does)
command_list* ListOf(ID3D12GraphicsCommandList* l) {
    AcquireSRWLockShared(&g_listLock);
    const auto it = g_lists.find(l);
    List* p = it != g_lists.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_listLock);
    if (p) return p;
    AcquireSRWLockExclusive(&g_listLock);
    List*& q = g_lists[l];
    if (!q) q = new List(l);
    p = q;
    ReleaseSRWLockExclusive(&g_listLock);
    return p;
}

static_assert(sizeof(rect) == sizeof(D3D12_RECT) && sizeof(viewport) == sizeof(D3D12_VIEWPORT) && sizeof(subresource_box) == sizeof(D3D12_BOX),
              "ReShade's rect / viewport / box are D3D12's");

void SinkClear(ID3D12GraphicsCommandList* cl, const D3D12_CPU_DESCRIPTOR_HANDLE* h, const float* c, unsigned n, const tagRECT* r) {
    hud::native::Clear(ListOf(cl), resource_view{(uint64_t)h->ptr}, c, n, (const rect*)r);
}
void SinkTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single) {
    resource_view v[8] = {};
    n = rts ? std::min(n, 8u) : 0u;
    const SIZE_T inc = natived3d::RtvIncrement();
    for (unsigned i = 0; i < n; ++i) v[i] = {(uint64_t)(single ? rts[0].ptr + i * inc : rts[i].ptr)};
    hud::native::Bind(ListOf(cl), n, v);
}
void SinkScissors(ID3D12GraphicsCommandList* cl, unsigned n, const tagRECT* r) { hud::native::Scissors(ListOf(cl), n, (const rect*)r); }
void SinkViewports(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_VIEWPORT* v) { hud::native::Viewports(ListOf(cl), n, (const viewport*)v); }
void SinkReset(ID3D12GraphicsCommandList* cl) { hud::native::Reset(ListOf(cl)); }

}  // namespace

void HudSinks(natived3d::Sinks& k) {
    k.clearRtv = &SinkClear;
    k.targets = &SinkTargets;
    k.scissors = &SinkScissors;
    k.viewports = &SinkViewports;
    k.listReset = &SinkReset;
}

ID3D12Resource* ResourceOfView(uint64_t view, uint32_t* dxgiFormat) {
    if (dxgiFormat) *dxgiFormat = 0;
    if (!view) return nullptr;
    AcquireSRWLockShared(&g_lock);
    const auto it = g_views.find(view);
    const View v = it != g_views.end() ? it->second : View{};
    ReleaseSRWLockShared(&g_lock);
    if (v.res) {
        if (dxgiFormat) *dxgiFormat = (uint32_t)v.fmt;
        return v.res;
    }
    return natived3d::RtvResource(view);
}

}  // namespace nrs
