// fog_height_test - vr/fog_height.cpp on a D3D12 device of its own (WARP): a pipeline with a fog shader of the game's shape is told as
// the game's (OnGraphicsPso), off it is bound as it is (Swap gives it back), on a variant with the patched shader is made by the device
// and bound instead, a new setting makes a new one, off again the game's own; a pipeline without the fog is never touched.
#include "fog_height.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace natived3d { void SetOurs(bool) {} }   // (fog_height's own pipelines: the hooks are not in this test)

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
static void Log(const char* s) { std::printf("  %s\n", s); }

static std::vector<uint8_t> Compile(const char* src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        std::printf("compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return {};
    }
    std::vector<uint8_t> v((const uint8_t*)code->GetBufferPointer(), (const uint8_t*)code->GetBufferPointer() + code->GetBufferSize());
    code->Release();
    return v;
}

int main() {
    ID3D12Device* dev = nullptr;
    IDXGIFactory4* fac = nullptr;
    // WARP through DXGI's adapter
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    auto createFactory = (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory2");
    IDXGIAdapter* warp = nullptr;
    CHECK(createFactory && SUCCEEDED(createFactory(0, __uuidof(IDXGIFactory4), (void**)&fac)) && SUCCEEDED(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp)));
    CHECK(warp && SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)));
    if (!dev) return 1;
    fogheight::SetLog(&Log);
    const char* vsSrc = "float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0, 1); }";
    const char* fogSrc = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB13 : register(b13) { float4 c13[30]; }
Texture2D<float> depth : register(t0);   // (1 in the tests: the view-space position comes from a read, as the game's from its depth)
float4 main(float4 pos : SV_Position) : SV_Target {
    const float s = depth.Load(int3(0, 0, 0));
    float4 vp = float4(c3[0].x * s, c3[0].y * s, -c3[0].z * s, 1.0);
    float t = min(1.0, exp(-c13[9].y * (sqrt(dot(vp, vp)) / c13[9].x - 1.0)));
    return float4(t * c13[10].x + c13[10].y, vp.x * t + vp.y, vp.z * t, 1);   // (the view-space position used after t, as the game's height fog does)
})";
    const std::vector<uint8_t> vs = Compile(vsSrc, "vs_5_0"), fog = Compile(fogSrc, "ps_5_0"), plain = Compile("float4 main() : SV_Target { return 1; }", "ps_5_0");
    // a root signature with the two constant buffers
    D3D12_ROOT_PARAMETER rp[3] = {};
    for (int i = 0; i < 2; ++i) {
        rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[i].Descriptor.ShaderRegister = i == 0 ? 3 : 13;
        rp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable = {1, &range};
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd{3, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* rsb = nullptr;
    ID3D12RootSignature* rs = nullptr;
    CHECK(SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, nullptr)) &&
          SUCCEEDED(dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs)));
    auto make = [&](const std::vector<uint8_t>& ps, D3D12_GRAPHICS_PIPELINE_STATE_DESC* out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = rs;
        d.VS = {vs.data(), vs.size()};
        d.PS = {ps.data(), ps.size()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = 0xFFFFFFFF;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        d.SampleDesc.Count = 1;
        *out = d;
        ID3D12PipelineState* p = nullptr;
        dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&p);
        return p;
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC dFog, dPlain;
    ID3D12PipelineState* gameFog = make(fog, &dFog);
    ID3D12PipelineState* gamePlain = make(plain, &dPlain);
    CHECK(gameFog && gamePlain);
    fogheight::OnGraphicsPso(dev, &dFog, gameFog);
    fogheight::OnGraphicsPso(dev, &dPlain, gamePlain);   // not the fog's: not kept
    fogheight::Frame(false, 60, 50, true);
    CHECK(fogheight::Swap(gameFog) == gameFog);         // off: the game's own
    fogheight::Frame(true, 60, 50, true);
    ID3D12PipelineState* ours = fogheight::Swap(gameFog);
    CHECK(ours && ours != gameFog);                      // on: ours, made by the device
    CHECK(fogheight::Swap(gamePlain) == gamePlain);     // another pipeline: untouched
    fogheight::Frame(true, 120, 30, true);
    ID3D12PipelineState* ours2 = fogheight::Swap(gameFog);
    CHECK(ours2 && ours2 != gameFog && ours2 != ours);   // a new setting: a new one
    fogheight::Frame(false, 120, 30, true);
    CHECK(fogheight::Swap(gameFog) == gameFog);         // off again
    // as the game makes them (PC1 2026-10-27): from a pipeline state stream - a root signature, the shaders, the target's format, the
    // topology, the rasterizer, a cached blob (taken out of ours)
    struct alignas(void*) RootSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE; ID3D12RootSignature* v; };
    struct alignas(void*) CodeSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t; D3D12_SHADER_BYTECODE v; };
    struct alignas(void*) RtSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS; D3D12_RT_FORMAT_ARRAY v; };
    struct alignas(void*) TopoSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY; D3D12_PRIMITIVE_TOPOLOGY_TYPE v; };
    struct alignas(void*) RastSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER; D3D12_RASTERIZER_DESC v; };
    struct alignas(void*) CacheSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO; D3D12_CACHED_PIPELINE_STATE v; };
    struct Stream { RootSub root; CodeSub vs, ps; RtSub rt; TopoSub topo; RastSub rast; CacheSub cache; } st{};
    st.root.v = rs;
    st.vs = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, {vs.data(), vs.size()}};
    st.ps = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, {fog.data(), fog.size()}};
    st.rt.v.NumRenderTargets = 1;
    st.rt.v.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    st.topo.v = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    st.rast.v = dFog.RasterizerState;
    st.cache.v = {};
    ID3D12Device2* d2 = nullptr;
    CHECK(SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2)));
    const D3D12_PIPELINE_STATE_STREAM_DESC sd{sizeof st, &st};
    ID3D12PipelineState* gameStream = nullptr;
    CHECK(d2 && SUCCEEDED(d2->CreatePipelineState(&sd, __uuidof(ID3D12PipelineState), (void**)&gameStream)));
    fogheight::OnStreamPso(dev, &sd, gameStream);
    std::memset(&st, 0xCD, sizeof st);                   // (the game's stream gone: ours a copy)
    fogheight::Frame(true, 60, 50, true);
    ID3D12PipelineState* oursStream = fogheight::Swap(gameStream);
    CHECK(oursStream && oursStream != gameStream);       // made again from the stream, our shader in it
    CHECK(fogheight::Swap(gameFog) != gameFog);          // (and the first one's too)
    fogheight::Frame(false, 60, 50, true);
    CHECK(fogheight::Swap(gameStream) == gameStream);
    if (d2) d2->Release();
    if (g_fail) { std::printf("fog_height_test: %d FAILED\n", g_fail); return 1; }
    std::printf("fog_height_test: the fog's pipeline kept, ours made by the device and bound in its place while on, again on a new setting: all passed\n");
    return 0;
}
