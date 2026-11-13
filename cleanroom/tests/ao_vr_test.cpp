// ao_vr_test - vr/ao_vr.cpp on a D3D12 device of its own (WARP). A pipeline whose pixel shader has the game's AO generate shape (Intel's
// radius) is told as the game's. Off, it is bound as it is. On, a variant with our radius is made by the device and bound instead. The
// radius moving by less than 5 % makes none; a real move, a second on, makes a new one. Off again: the game's own. A pipeline without that
// shape is never touched.
#include "ao_vr.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace natived3d { void SetOurs(bool) {} }   // (ao_vr's own pipelines: the hooks are not in this test)

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
static void Log(const char* s) { std::printf("  %s\n", s); }

static std::vector<uint8_t> Compile(const char* src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
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
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    auto createFactory = (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory2");
    IDXGIAdapter* warp = nullptr;
    CHECK(createFactory && SUCCEEDED(createFactory(0, __uuidof(IDXGIFactory4), (void**)&fac)) && SUCCEEDED(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp)));
    CHECK(warp && SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)));
    if (!dev) return 1;
    aovr::SetLog(&Log);
    const char* vsSrc = "float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0, 1); }";
    const char* aoSrc = R"(
cbuffer CB4 : register(b4) { float4 c4[15]; }
Texture2D<float> depth : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    const float dist = depth.Load(int3(0, 0, 0)) * pos.x;
    float r = saturate(dist * c4[6].y) * 0.8 + 0.2;
    r = r * c4[4].z;
    return float4(r, 0.85 * r / (dist + 1.0), -1.0 / (r * r), 1);
})";
    const std::vector<uint8_t> vs = Compile(vsSrc, "vs_5_0"), ao = Compile(aoSrc, "ps_5_0"), plain = Compile("float4 main() : SV_Target { return 1; }", "ps_5_0");
    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 4;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable = {1, &range};
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
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
        d.RTVFormats[0] = DXGI_FORMAT_R8G8_UNORM;
        d.SampleDesc.Count = 1;
        *out = d;
        ID3D12PipelineState* p = nullptr;
        dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&p);
        return p;
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC dAo, dPlain;
    ID3D12PipelineState* gameAo = make(ao, &dAo);
    ID3D12PipelineState* gamePlain = make(plain, &dPlain);
    CHECK(gameAo && gamePlain);
    aovr::OnGraphicsPso(dev, &dAo, gameAo);
    aovr::OnGraphicsPso(dev, &dPlain, gamePlain);   // not the AO's: not kept
    CHECK(aovr::Kept() == 1);
    aovr::Frame(false, 2.0f, 0.5f);
    CHECK(aovr::Swap(gameAo) == gameAo);          // off: the game's own
    aovr::Frame(true, 2.0f, 0.5f);
    ID3D12PipelineState* ours = aovr::Swap(gameAo);
    CHECK(ours && ours != gameAo);                // on: ours, made by the device
    CHECK(aovr::Swap(gamePlain) == gamePlain);    // another pipeline: untouched
    const uint32_t b0 = aovr::Builds();
    for (int f = 0; f < 50; ++f) aovr::Frame(true, 2.0f + 0.05f * (f & 1), 0.5f);   // (a head bob of the world's scale: under 5 %)
    CHECK(aovr::Builds() == b0 && aovr::Swap(gameAo) == ours);
    Sleep(1100);
    aovr::Frame(true, 3.0f, 0.75f);               // a real move, a second on: a new one
    ID3D12PipelineState* ours2 = aovr::Swap(gameAo);
    CHECK(aovr::Builds() == b0 + 1 && ours2 && ours2 != ours && ours2 != gameAo);
    aovr::Frame(false, 3.0f, 0.75f);
    CHECK(aovr::Swap(gameAo) == gameAo);          // off again (F1 - F3, the key off)
    aovr::Frame(true, 0.0f, 0.0f);
    CHECK(aovr::Swap(gameAo) == gameAo);          // no radius: the game's
    if (g_fail) { std::printf("ao_vr_test: %d FAILED\n", g_fail); return 1; }
    std::printf("ao_vr_test: the AO's pipeline kept, ours with the radius bound while on, made again only on a real move: all passed\n");
    return 0;
}
