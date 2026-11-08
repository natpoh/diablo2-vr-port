// near_overlay.cpp - see near_overlay.h.

#include "near_overlay.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

namespace nearoverlay {
namespace {

// b0: ox oy | pictureW pictureH | depthScale.xy | nearPlane upm | maxM featherM | decode
const char kShader[] = R"(
cbuffer P : register(b0) {
    float2 origin;      // the eye's image in the picture
    float2 pictureSize;
    float2 depthScale;
    float nearPlane;
    float upm;
    float maxM;
    float featherM;
    float decodeSrgb;
    float pad;
};
Texture2D<float4> picture : register(t0);
Texture2D<float> depth : register(t1);

float4 VS(uint id : SV_VertexID) : SV_Position {
    const float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 Decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 PS(float4 pos : SV_Position) : SV_Target {
    const int2 p = int2(pos.xy) + int2(origin);
    float dw, dh;
    depth.GetDimensions(dw, dh);
    const float2 uv = (float2(p) + 0.5) / pictureSize;
    const float d = depth.Load(int3(int2(uv * depthScale * float2(dw, dh)), 0));
    if (!(d > 0) || !(nearPlane > 0) || !(upm > 0)) return 0;
    const float metres = nearPlane / d / upm;
    if (!(metres < maxM)) return 0;
    const float a = featherM > 0 ? saturate((maxM - metres) / featherM) : 1;
    float3 c = picture.Load(int3(p, 0)).rgb;
    if (decodeSrgb > 0.5) c = Decode(saturate(c));
    return float4(c * a, a);
}
)";

void Set(std::string* why, const char* text) {
    if (why) *why = text;
}

// d3dcompiler_47 and d3d12's serializer by name (the plugin links neither: as vr/turn_warp.cpp, vr/fog_before_fx.cpp)
pD3DCompile Compiler() {
    static const pD3DCompile fn = [] {
        const HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return dc ? reinterpret_cast<pD3DCompile>(reinterpret_cast<void*>(GetProcAddress(dc, "D3DCompile"))) : nullptr;
    }();
    return fn;
}
using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
SerializeFn Serializer() {
    static const SerializeFn fn = [] {
        const HMODULE d12 = GetModuleHandleW(L"d3d12.dll") ? GetModuleHandleW(L"d3d12.dll") : LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return d12 ? reinterpret_cast<SerializeFn>(reinterpret_cast<void*>(GetProcAddress(d12, "D3D12SerializeRootSignature"))) : nullptr;
    }();
    return fn;
}

template <class T> void Drop(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

}  // namespace

bool IsSrgb(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

DXGI_FORMAT PictureView(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

bool Pass::Init(ID3D12Device* dev, DXGI_FORMAT rtvFormat, std::string* why) {
    if (pso_ && dev == dev_ && rtvFormat == format_) return true;
    Release();
    if (!dev) { Set(why, "no device"); return false; }
    dev_ = dev;
    format_ = rtvFormat;
    const pD3DCompile compile = Compiler();
    const SerializeFn serialize = Serializer();
    if (!compile || !serialize) {
        Set(why, !compile ? "no d3dcompiler_47.dll" : "no D3D12SerializeRootSignature");
        Release();
        return false;
    }
    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(compile(kShader, sizeof kShader - 1, "near_overlay", nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)) ||
        FAILED(compile(kShader, sizeof kShader - 1, "near_overlay", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err))) {
        static char text[512];
        std::snprintf(text, sizeof text, "the shader did not compile: %s", err ? (const char*)err->GetBufferPointer() : "?");
        Set(why, text);
        Drop(err);
        Drop(vs);
        Drop(ps);
        Release();
        return false;
    }
    // the root: 12 constants (b0), a table of the two textures (t0, t1); one static point sampler is not needed (Load only)
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, 12};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {1, &range};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rd{2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* blob = nullptr;
    if (FAILED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs_))) {
        Set(why, "the root signature could not be made");
        Drop(blob);
        Drop(err);
        Drop(vs);
        Drop(ps);
        Release();
        return false;
    }
    Drop(blob);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs_;
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;   // (no blend: the target is the mask's picture alone)
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rtvFormat;
    d.SampleDesc.Count = 1;
    const HRESULT hr = dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&pso_);
    Drop(vs);
    Drop(ps);
    if (FAILED(hr)) {
        Set(why, "the pipeline could not be made");
        Release();
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, (UINT)(2 * kSlots), D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap_))) {
        Set(why, "the descriptor heap could not be made");
        Release();
        return false;
    }
    inc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool Pass::Record(ID3D12GraphicsCommandList* list, ID3D12Resource* picture, DXGI_FORMAT pictureView, ID3D12Resource* depth, DXGI_FORMAT depthView,
                  D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t w, uint32_t h, const Params& p) {
    if (!pso_ || !list || !picture || !depth || !w || !h) return false;
    const int slot = slot_;
    slot_ = (slot_ + 1) % kSlots;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)(2 * slot) * inc_;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    sv.Format = pictureView;
    dev_->CreateShaderResourceView(picture, &sv, cpu);
    cpu.ptr += inc_;
    sv.Format = depthView;
    dev_->CreateShaderResourceView(depth, &sv, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)(2 * slot) * inc_;
    const float c[12] = {(float)p.ox, (float)p.oy, p.pictureW, p.pictureH, p.depthScale[0], p.depthScale[1], p.nearPlane, p.upm, p.maxM, p.featherM,
                         p.decodeSrgb ? 1.0f : 0.0f, 0.0f};
    list->SetGraphicsRootSignature(rs_);
    list->SetPipelineState(pso_);
    ID3D12DescriptorHeap* heaps[1] = {heap_};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRoot32BitConstants(0, 12, c, 0);
    list->SetGraphicsRootDescriptorTable(1, gpu);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    const D3D12_RECT sr{0, 0, (LONG)w, (LONG)h};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    return true;
}

void Pass::Release() {
    Drop(pso_);
    Drop(rs_);
    Drop(heap_);
    dev_ = nullptr;
    format_ = DXGI_FORMAT_UNKNOWN;
    slot_ = 0;
}

}  // namespace nearoverlay
