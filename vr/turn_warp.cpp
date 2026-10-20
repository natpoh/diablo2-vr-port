// turn_warp.cpp - see turn_warp.h. The shaders follow bearstonem/d2r-pcvr's steamvr/turn_warp.cpp (MIT): a full-screen
// triangle for the turn, and a grid of two triangles a cell for each picture moved with its depth.

#include "turn_warp.h"

#include <d3dcompiler.h>

#include <climits>
#include <cstdio>
#include <cstring>

namespace turnwarp {
namespace {

// Pass 1: this frame's image coordinates to the drawn image's (Homography's rows), the picture sampled there.
const char kTurnShader[] = R"(
Texture2D<float4> drawn : register(t0);
SamplerState edge : register(s0);
cbuffer Turn : register(b0) { float4 row0; float4 row1; float4 row2; };
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Pixel VS(uint id : SV_VertexID) {
    Pixel p;
    p.uv = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    p.position = float4(p.uv.x * 2 - 1, 1 - p.uv.y * 2, 0, 1);
    return p;
}
#if MADE_DEPTH
struct Out { float4 colour : SV_Target0; float depth : SV_Target1; };
#else
struct Out { float4 colour : SV_Target0; };
#endif
Out PS(Pixel p) {
    const float3 at = float3(p.uv, 1);
    const float3 h = float3(dot(row0.xyz, at), dot(row1.xyz, at), dot(row2.xyz, at));
    Out o;
#if MADE_DEPTH
    o.depth = 0;                                                                   // the turn alone knows no depth: far
#endif
    o.colour = h.z <= 1e-6 ? float4(0, 0, 0, 1)                                    // behind the drawn eye: nothing it saw
                           : drawn.SampleLevel(edge, h.xy / h.z, 0);               // past its edge: the edge drawn out
    return o;
}
)";

// Passes 2 and 3: a grid over the drawn picture, each point put back at its distance (reverse-Z, infinite: near / depth),
// moved (what the hero carries by `carry`) and projected into this frame's eye, nearer over farther.
const char kDepthShader[] = R"(
Texture2D<float4> drawn : register(t0);
Texture2D<float> drawnDepth : register(t1);
SamplerState edge : register(s0);
cbuffer Move : register(b0) {
    float4 move0; float4 move1; float4 move2;      // Move's rows
    float4 drawnTan;                               // the drawn eye's tangents: left, right, down, up
    float4 nowTan;                                 // this frame's
    float4 depthInfo;                              // near; the depth's drawn part x, y
    uint4 grid;                                    // cells across, down
    float4 carry0; float4 carry1; float4 carry2;   // Carry's rows
    float4 reach;                                  // carried nearer than x, the world's farther than y; z: the edge scale (0: no cut)
};
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; noperspective float depth : TEXCOORD1; };
static const float2 Corner[6] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1) };
static const float Ahead = 1e-3;        // nearer than this to this frame's eye: cut off
static const float EdgePixels = 2;      // turnwarp::kEdgePixels
// The nearest of the texels round a point (reverse-Z: the largest), so an edge stays with what is in front; the sky (0) far.
float Nearest(float2 uv) {
    const float4 d4 = drawnDepth.GatherRed(edge, uv * depthInfo.yz);
    return max(max(max(d4.x, d4.y), max(d4.z, d4.w)), 1e-6);
}
Pixel VS(uint id : SV_VertexID) {
    const uint cell = id / 6;
    const float2 at = float2(cell % grid.x, cell / grid.x), size = float2(grid.xy);
    const float2 uv = (at + Corner[id % 6]) / size;
    const float d = Nearest(uv);
    const float away = depthInfo.x / d;
    const float3 p = float3(lerp(drawnTan.x, drawnTan.y, uv.x), lerp(drawnTan.w, drawnTan.z, uv.y), -1) * away;
    const float3 world = float3(dot(move0.xyz, p) + move0.w, dot(move1.xyz, p) + move1.w, dot(move2.xyz, p) + move2.w);
    const float3 carried = float3(dot(carry0.xyz, p) + carry0.w, dot(carry1.xyz, p) + carry1.w, dot(carry2.xyz, p) + carry2.w);
    const float3 q = reach.y > reach.x ? lerp(carried, world, saturate((away - reach.x) / (reach.y - reach.x))) : world;
    const float w = -q.z;   // this frame's projection, its depth reversed: nearer is larger
    Pixel o;
    o.uv = uv;
    o.depth = depthInfo.x / max(w, 1e-6);   // this frame's eye's own reverse-Z depth (near / distance): linear on the screen
    o.position = float4(2 * (q.x - nowTan.x * w) / (nowTan.y - nowTan.x) - w, w - 2 * (nowTan.w * w - q.y) / (nowTan.w - nowTan.z), Ahead, w);
    if (reach.z > 0) {   // the drawn eye's picture: a cell across an edge in its depth is cut (out of the clip volume)
        const float a = Nearest(at / size), b = Nearest((at + float2(1, 0)) / size), c = Nearest((at + float2(0, 1)) / size), f = Nearest((at + 1) / size);
        if ((max(max(a, b), max(c, f)) - min(min(a, b), min(c, f))) * reach.z > EdgePixels) o.position = float4(0, 0, 0, -1);
    }
    return o;
}
#if MADE_DEPTH
struct Out { float4 colour : SV_Target0; float depth : SV_Target1; };
Out PS(Pixel p) { Out o; o.colour = drawn.SampleLevel(edge, p.uv, 0); o.depth = p.depth; return o; }
#else
float4 PS(Pixel p) : SV_Target { return drawn.SampleLevel(edge, p.uv, 0); }
#endif
)";
constexpr UINT kDepthConstants = 44;
constexpr UINT kPerSlot = 5;   // base; own image, own depth; drawn image, drawn depth

template <class T> void Free(T*& p) { if (p) { p->Release(); p = nullptr; } }

}  // namespace

void Gpu::Release() {
    Free(depthTarget_);
    Free(madeDepth_);
    Free(dsv_);
    Free(rtv_);
    Free(srv_);
    for (int k = 0; k < 2; ++k) { Free(depthPso_[k]); Free(pso_[k]); }
    Free(depthRoot_);
    Free(root_);
    depthW_ = depthH_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
    dev_ = nullptr;
    tried_ = false;
}

bool Gpu::Init(ID3D12Device* dev, DXGI_FORMAT target, void (*log)(const char*)) {
    if (pso_[0] && dev == dev_ && target == format_) return true;
    if (tried_ && dev == dev_ && target == format_) return false;   // failed for this one already
    Release();
    dev_ = dev;
    format_ = target;
    tried_ = true;
    auto say = [&](const char* fmt, const char* a) {
        if (!log) return;
        char b[384];
        snprintf(b, sizeof b, fmt, a);
        log(b);
    };
    if (!dev) return false;
    HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) {
        say("vrcam: eyes by turns - no shader compiler (%s): the eye not drawn goes as it was last drawn", "d3dcompiler_47.dll");
        return false;
    }
    // made: the variant that also writes the made eye's depth (MadeDepth, [stereo] turn_overlay_depth) as a second target
    auto build = [&](const char* src, const char* entry, const char* target, bool made, ID3DBlob** out) {
        ID3DBlob* err = nullptr;
        const D3D_SHADER_MACRO defs[] = {{"MADE_DEPTH", made ? "1" : "0"}, {nullptr, nullptr}};
        const bool ok = SUCCEEDED(compile(src, strlen(src), "d2rvr_turn", defs, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err));
        if (!ok) say("vrcam: eyes by turns - a shader did not compile: %s", err ? (const char*)err->GetBufferPointer() : "?");
        Free(err);
        return ok;
    };
    auto rootOf = [&](UINT srvs, UINT constants, D3D12_SHADER_VISIBILITY vis, ID3D12RootSignature** out) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvs, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[0].DescriptorTable = {1, &range};
        rp[0].ShaderVisibility = vis;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[1].Constants = {0, 0, constants};
        rp[1].ShaderVisibility = vis;
        D3D12_STATIC_SAMPLER_DESC smp{};
        smp.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp.MaxLOD = D3D12_FLOAT32_MAX;
        smp.ShaderVisibility = vis;
        const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob* blob = nullptr, *err = nullptr;
        const bool ok = SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) &&
                        SUCCEEDED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)out));
        Free(blob);
        Free(err);
        return ok;
    };
    auto psoOf = [&](ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps, bool depth, bool made, ID3D12PipelineState** out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = rs;
        d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = made ? 2 : 1;   // the picture, and its depth (MadeDepth)
        d.RTVFormats[0] = format_;
        if (made) d.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
        d.SampleDesc.Count = 1;
        if (depth) {
            d.DepthStencilState.DepthEnable = TRUE;
            d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;   // reverse-Z: nearer is larger
            d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        }
        return SUCCEEDED(dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)out));
    };
    ID3DBlob *vs = nullptr, *ps = nullptr, *psM = nullptr, *dvs = nullptr, *dps = nullptr, *dpsM = nullptr;
    bool ok = build(kTurnShader, "VS", "vs_5_0", false, &vs) && build(kTurnShader, "PS", "ps_5_0", false, &ps) &&
              rootOf(1, 12, D3D12_SHADER_VISIBILITY_PIXEL, &root_) && psoOf(root_, vs, ps, false, false, &pso_[0]);
    // the made depth's variant: without it the overlays of the eye made are hidden by nothing (as with turn_overlay_depth=0)
    if (ok && !(build(kTurnShader, "PS", "ps_5_0", true, &psM) && psoOf(root_, vs, psM, false, true, &pso_[1]))) {
        Free(pso_[1]);
        say("vrcam: eyes by turns - the made eye's depth unavailable (%s): its overlays are hidden by nothing", "its pass could not be made");
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kPerSlot, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ok = ok && SUCCEEDED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&srv_));
    D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ok = ok && SUCCEEDED(dev->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void**)&rtv_));
    if (!ok) {
        Free(vs); Free(ps); Free(psM);
        for (int k = 0; k < 2; ++k) Free(pso_[k]);
        Free(root_); Free(srv_); Free(rtv_);
        say("vrcam: eyes by turns - the GPU pass could not be made (%s): the eye not drawn goes as it was last drawn", "pipeline or heaps");
        return false;
    }
    srvInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv_->SetName(L"d2rvr eyes by turns srv");
    // the depth passes: without them the eye not drawn is turned only
    const bool depthOk = build(kDepthShader, "VS", "vs_5_0", false, &dvs) && build(kDepthShader, "PS", "ps_5_0", false, &dps) &&
                         rootOf(2, kDepthConstants, D3D12_SHADER_VISIBILITY_ALL, &depthRoot_) && psoOf(depthRoot_, dvs, dps, true, false, &depthPso_[0]);
    D3D12_DESCRIPTOR_HEAP_DESC dd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (!depthOk || FAILED(dev->CreateDescriptorHeap(&dd, __uuidof(ID3D12DescriptorHeap), (void**)&dsv_))) {
        for (int k = 0; k < 2; ++k) Free(depthPso_[k]);
        Free(depthRoot_); Free(dsv_);
        say("vrcam: eyes by turns - moving with the depth unavailable (%s): the eye not drawn is turned only", "its pass could not be made");
    } else if (pso_[1] && !(build(kDepthShader, "PS", "ps_5_0", true, &dpsM) && psoOf(depthRoot_, dvs, dpsM, true, true, &depthPso_[1]))) {
        Free(depthPso_[1]);
        Free(pso_[1]);   // the made depth whole or not at all: from the turn alone it would be far everywhere
        say("vrcam: eyes by turns - the made eye's depth unavailable (%s): its overlays are hidden by nothing", "its grid pass could not be made");
    }
    Free(vs); Free(ps); Free(psM); Free(dvs); Free(dps); Free(dpsM);
    return true;
}

bool Gpu::Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, uint32_t w, uint32_t h, ID3D12Resource* base,
                 const std::array<float, 9>& homography, const Grid* own, const Grid* drawn, int slot, bool madeDepth) {
    const int k = madeDepth && pso_[1] ? 1 : 0;   // 1: the made depth written too (MadeDepth)
    if (!pso_[0] || !list || !target || !base || !w || !h || slot < 0 || slot >= kSlots) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = format_;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = srv_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = srv_->GetGPUDescriptorHandleForHeapStart();
    cpu0.ptr += SIZE_T(srvInc_) * kPerSlot * slot;
    gpu0.ptr += UINT64(srvInc_) * kPerSlot * slot;
    auto at = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE c = cpu0; c.ptr += SIZE_T(srvInc_) * i; return c; };
    auto gpuAt = [&](UINT i) { D3D12_GPU_DESCRIPTOR_HANDLE g = gpu0; g.ptr += UINT64(srvInc_) * i; return g; };
    dev_->CreateShaderResourceView(base, &sv, at(0));
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = format_;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_->GetCPUDescriptorHandleForHeapStart();   // [0] the picture, [1] its depth: one range
    dev_->CreateRenderTargetView(target, &rv, rtv);
    const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (k && madeDepth_) {
        const D3D12_RESOURCE_DESC m = madeDepth_->GetDesc();
        if (m.Width != w || m.Height != h) Free(madeDepth_);   // (one size a session: the caller waited for its lists at a change)
    }
    if (k && !madeDepth_) {   // the made eye's depth, as a picture the effect reads (MadeDepth)
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, readable, nullptr, __uuidof(ID3D12Resource), (void**)&madeDepth_))) {
            madeDepth_ = nullptr;
            return false;
        }
        madeDepth_->SetName(L"d2rvr eyes by turns made depth");
        ++madeGen_;   // another texture, perhaps at the old one's address: the effect's binding made again (MadeDepthGen)
    }
    D3D12_RESOURCE_BARRIER toTarget{};
    if (k) {
        D3D12_RENDER_TARGET_VIEW_DESC dr{};
        dr.Format = DXGI_FORMAT_R32_FLOAT;
        dr.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvDepth = rtv;
        rtvDepth.ptr += dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dev_->CreateRenderTargetView(madeDepth_, &dr, rtvDepth);
        toTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toTarget.Transition = {madeDepth_, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, readable, D3D12_RESOURCE_STATE_RENDER_TARGET};
        list->ResourceBarrier(1, &toTarget);
    }
    const UINT targets = k ? 2 : 1;
    auto done = [&] {   // the made depth readable again for the effect
        if (!k) return true;
        D3D12_RESOURCE_BARRIER back = toTarget;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        back.Transition.StateAfter = readable;
        list->ResourceBarrier(1, &back);
        return true;
    };
    const D3D12_VIEWPORT vp{0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f};
    const D3D12_RECT rect{0, 0, LONG(w), LONG(h)};

    // 1. the turn
    ID3D12DescriptorHeap* heaps[] = {srv_};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(root_);
    list->SetGraphicsRootDescriptorTable(0, gpuAt(0));
    float rows[12] = {};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) rows[r * 4 + c] = homography[r * 3 + c];
    list->SetGraphicsRoot32BitConstants(1, 12, rows, 0);
    list->OMSetRenderTargets(targets, &rtv, TRUE, nullptr);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &rect);
    list->SetPipelineState(pso_[k]);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    auto usable = [](const Grid* g) { return g && g->image && g->depth && g->depthView != DXGI_FORMAT_UNKNOWN && g->nearPlane > 0.0f; };
    if (!depthPso_[k] || (!usable(own) && !usable(drawn))) return done();
    if (!depthTarget_ || depthW_ != w || depthH_ != h) {   // the grids' depth test, the eyes' size
        Free(depthTarget_);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = DXGI_FORMAT_D32_FLOAT;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 0.0f;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, __uuidof(ID3D12Resource),
                                                 (void**)&depthTarget_))) {
            depthTarget_ = nullptr;
            return done();   // turned only this frame
        }
        depthTarget_->SetName(L"d2rvr eyes by turns depth test");
        depthW_ = w;
        depthH_ = h;
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dev_->CreateDepthStencilView(depthTarget_, &dv, dsv_->GetCPUDescriptorHandleForHeapStart());
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(targets, &rtv, TRUE, &dsv);
    list->SetGraphicsRootSignature(depthRoot_);
    list->SetPipelineState(depthPso_[k]);
    const uint32_t across = (w + kGridStep - 1) / kGridStep, down = (h + kGridStep - 1) / kGridStep;
    auto grid = [&](const Grid& g, UINT first) {
        dev_->CreateShaderResourceView(g.image, &sv, at(first));
        D3D12_SHADER_RESOURCE_VIEW_DESC dvw = sv;
        dvw.Format = g.depthView;
        dev_->CreateShaderResourceView(g.depth, &dvw, at(first + 1));
        list->SetGraphicsRootDescriptorTable(0, gpuAt(first));
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);   // each grid over the last whole
        uint32_t c[kDepthConstants] = {};
        float f[44] = {};
        memcpy(f, g.move.data(), sizeof(float) * 12);
        f[12] = g.drawnTan.left; f[13] = g.drawnTan.right; f[14] = g.drawnTan.down; f[15] = g.drawnTan.up;
        f[16] = g.nowTan.left; f[17] = g.nowTan.right; f[18] = g.nowTan.down; f[19] = g.nowTan.up;
        f[20] = g.nearPlane; f[21] = g.scale[0]; f[22] = g.scale[1];
        memcpy(f + 28, g.carry.data(), sizeof(float) * 12);
        f[40] = g.nearW; f[41] = g.farW; f[42] = g.edgeScale;
        memcpy(c, f, sizeof f);
        c[24] = across;
        c[25] = down;
        list->SetGraphicsRoot32BitConstants(1, kDepthConstants, c, 0);
        list->DrawInstanced(6 * across * down, 1, 0, 0);
    };
    if (usable(own)) grid(*own, 1);
    if (usable(drawn)) grid(*drawn, 3);
    return done();
}

void Gpu::ClearMadeDepth(ID3D12GraphicsCommandList* list) {
    if (!madeDepth_ || !list || !rtv_) return;
    const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RENDER_TARGET_VIEW_DESC dr{};
    dr.Format = DXGI_FORMAT_R32_FLOAT;
    dr.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvDepth = rtv_->GetCPUDescriptorHandleForHeapStart();
    rtvDepth.ptr += dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dev_->CreateRenderTargetView(madeDepth_, &dr, rtvDepth);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {madeDepth_, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, readable, D3D12_RESOURCE_STATE_RENDER_TARGET};
    list->ResourceBarrier(1, &b);
    const float farZ[4] = {0, 0, 0, 0};
    list->ClearRenderTargetView(rtvDepth, farZ, 0, nullptr);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = readable;
    list->ResourceBarrier(1, &b);
}

}  // namespace turnwarp
