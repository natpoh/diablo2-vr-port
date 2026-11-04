// [render] reflections_per_eye without ReShade (vr/reflections_eye.h), on a software D3D12 device (WARP). Shaders of the test's own with
// the shape of the game's two reflection passes (cleanroom/tests/ppr_standins.h) are made into pipelines as the game makes them - the
// projection from a compute description and from a pipeline stream, the resolve from a graphics description and from a stream - and
// handed to refleye::OnComputePso / OnGraphicsPso / OnStreamPso as the plugin's D3D12 hooks hand them over; then
//   - off at the game's start: nothing made, Swap gives the game's own (as before);
//   - on: a copy is made for each, and Swap gives it for exactly those pipelines while bound (on, VR), the game's own when not bound
//     (off now, or flat mode), and any other pipeline is left alone;
//   - each copy's shader is the patched one (shared/dxbc_ppr_eye.h): no cb3[12] left in it.
//
//   reflections_eye_native_test

#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../vr/reflections_eye.h"
#include "dxbc_ppr_eye.h"
#include "ppr_standins.h"

// The plugin's D3D12 hooks are not in this test: our own calls are not told to them anyway.
namespace natived3d {
void SetOurs(bool) {}
bool Ours() { return false; }
}  // namespace natived3d

using Microsoft::WRL::ComPtr;

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; printf("  FAIL: %s\n", what); }
}
void Must(HRESULT hr, const char* what) {
    if (FAILED(hr)) { printf("%s failed: 0x%08lX\n", what, (unsigned long)hr); exit(2); }
}
std::vector<std::string> g_lines;
void Log(const char* s) { printf("  log: %s\n", s); g_lines.push_back(s); }

ComPtr<ID3DBlob> Compile(const char* src, const char* target) {
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3DCompile(src, strlen(src), "test", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
        printf("did not compile: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        exit(2);
    }
    return blob;
}
const char* kVs = R"(
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2); o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
)";
// another compute shader with the same bindings, not the reflection's shape (cb3[12] read twice)
const char* kOtherCs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[91]; }
RWStructuredBuffer<uint> outBuf : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { outBuf[id.x] = asuint(dot(c3[12].xyz, c10[1].xyz) + c3[12].w + c13[4].x + c3[9].x + c3[11].x + c10[53].x); }
)";

// One root signature for all: a table of b0 - b13, t0 - t1, u0 - u1
ComPtr<ID3D12RootSignature> Root(ID3D12Device* dev) {
    D3D12_DESCRIPTOR_RANGE r[3] = {{D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 14, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 14},
                                   {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 16}};
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable = {3, r};
    D3D12_ROOT_SIGNATURE_DESC rd{1, &p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errors;
    Must(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature serialized");
    ComPtr<ID3D12RootSignature> root;
    Must(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "root signature");
    return root;
}

// a pipeline stream: root signature + one shader (+ for graphics: VS, RT formats)
template <class T> void Put(std::vector<uint8_t>& s, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const T& value) {
    const size_t align = alignof(void*);
    size_t at = (s.size() + align - 1) / align * align;
    s.resize(at);
    const size_t valueAt = (4 + alignof(T) - 1) / alignof(T) * alignof(T);
    s.resize(at + (valueAt + sizeof(T) + align - 1) / align * align, 0);
    const uint32_t t = (uint32_t)type;
    memcpy(&s[at], &t, 4);
    memcpy(&s[at + valueAt], &value, sizeof(T));
}

}  // namespace

int main() {
    ComPtr<IDXGIFactory4> factory;
    Must(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<IDXGIAdapter> warp;
    Must(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
    ComPtr<ID3D12Device> dev;
    Must(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)), "WARP device");
    ComPtr<ID3D12Device2> dev2;
    Must(dev.As(&dev2), "Device2");
    refleye::SetLog(&Log);
    const ComPtr<ID3D12RootSignature> root = Root(dev.Get());
    const ComPtr<ID3DBlob> cs = Compile(pprstandin::kProjCs, "cs_5_0"), ps = Compile(pprstandin::kResolvePs, "ps_5_0"),
                           vs = Compile(kVs, "vs_5_0"), other = Compile(kOtherCs, "cs_5_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
    cd.pRootSignature = root.Get();
    cd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    D3D12_COMPUTE_PIPELINE_STATE_DESC od = cd;
    od.CS = {other->GetBufferPointer(), other->GetBufferSize()};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};
    gd.pRootSignature = root.Get();
    gd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    gd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.SampleMask = UINT_MAX;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    gd.SampleDesc.Count = 1;

    auto makeCompute = [&](const D3D12_COMPUTE_PIPELINE_STATE_DESC& d) {
        ComPtr<ID3D12PipelineState> p;
        Must(dev->CreateComputePipelineState(&d, IID_PPV_ARGS(&p)), "compute pipeline");
        return p;
    };
    auto makeGraphics = [&]() {
        ComPtr<ID3D12PipelineState> p;
        Must(dev->CreateGraphicsPipelineState(&gd, IID_PPV_ARGS(&p)), "graphics pipeline");
        return p;
    };
    std::vector<uint8_t> csStream, psStream;
    Put(csStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, root.Get());
    Put(csStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, cd.CS);
    Put(psStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, root.Get());
    Put(psStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, gd.VS);
    Put(psStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, gd.PS);
    D3D12_RT_FORMAT_ARRAY rts{};
    rts.NumRenderTargets = 1;
    rts.RTFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
    Put(psStream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, rts);
    auto makeStream = [&](std::vector<uint8_t>& s, D3D12_PIPELINE_STATE_STREAM_DESC* sd) {
        *sd = {s.size(), s.data()};
        ComPtr<ID3D12PipelineState> p;
        Must(dev2->CreatePipelineState(sd, IID_PPV_ARGS(&p)), "pipeline from a stream");
        return p;
    };

    // 1. off at the game's start: nothing made, the game's own bound - as before
    refleye::Configure(false, true);
    {
        const ComPtr<ID3D12PipelineState> early = makeCompute(cd);
        refleye::OnComputePso(dev.Get(), &cd, early.Get());
        Check(refleye::Copies() == 0, "off at the start: no copy made");
        Check(refleye::Swap(early.Get()) == early.Get(), "off at the start: the game's own pipeline bound");
        refleye::Tell();
        Check(g_lines.empty(), "off: not a line in the log");
    }
    // 2. on at the start, VR: a copy for each of the four ways the game could make its two passes
    refleye::Configure(true, true);
    const ComPtr<ID3D12PipelineState> proj = makeCompute(cd), res = makeGraphics(), otherP = makeCompute(od);
    refleye::OnComputePso(dev.Get(), &cd, proj.Get());
    refleye::OnGraphicsPso(dev.Get(), &gd, res.Get());
    refleye::OnComputePso(dev.Get(), &od, otherP.Get());
    D3D12_PIPELINE_STATE_STREAM_DESC csd{}, psd{};
    const ComPtr<ID3D12PipelineState> projS = makeStream(csStream, &csd), resS = makeStream(psStream, &psd);
    refleye::OnStreamPso(dev.Get(), &csd, projS.Get());
    refleye::OnStreamPso(dev.Get(), &psd, resS.Get());
    Check(refleye::Copies() == 4, "on: a copy for the projection and the resolve, from descriptions and from streams");
    for (ID3D12PipelineState* p : {proj.Get(), res.Get(), projS.Get(), resS.Get()}) {
        ID3D12PipelineState* s = refleye::Swap(p);
        Check(s != nullptr && s != p, "bound: our copy in the game's reflection pipeline's place");
    }
    Check(refleye::Swap(otherP.Get()) == otherP.Get(), "another compute pipeline of the same bindings: left alone");
    Check(refleye::Swap(nullptr) == nullptr, "no pipeline: nothing");
    // the copy's shader is the patched one: its cached blob holds the bytes we gave it - check through the pure part instead
    {
        std::string why;
        dxbcppr::Found f;
        const std::vector<uint8_t> patched = dxbcppr::Patch((const uint8_t*)cs->GetBufferPointer(), cs->GetBufferSize(), &why, &f);
        Check(!patched.empty() && f.compute, "the projection stand-in is the reflection's shape");
        Check(!dxbcppr::Find(patched.data(), patched.size(), &f), "patched: no cb3[12] left");
    }
    refleye::Tell();
    bool told = false;
    for (const std::string& l : g_lines) told |= l.find("4 of the game's reflection pipelines with a copy; the copies are bound") != std::string::npos;
    Check(told, "the state line says the copies are bound");
    // 3. not bound: off now, or flat mode - the game's own
    refleye::Configure(false, true);
    Check(refleye::Swap(proj.Get()) == proj.Get(), "off now: the game's own bound");
    refleye::Configure(true, false);
    Check(refleye::Swap(res.Get()) == res.Get(), "flat mode: the game's own bound");
    refleye::Configure(true, true);
    Check(refleye::Swap(resS.Get()) != resS.Get(), "on again: our copy (live)");

    printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "OK", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
