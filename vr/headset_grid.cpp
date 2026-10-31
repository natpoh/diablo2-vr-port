// [headset_shot] grid: a calibration grid in each eye's picture - see vr/headset_grid.h, the sums in vr/headset_grid_math.h. The shaders
// are compiled on the update thread when the tick is set (no hitch on the draw thread); the pipeline for a format is made at its first draw.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

#include "headset_grid.h"
#include "headset_grid_math.h"

namespace headgrid {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

std::atomic<bool> g_on{false};
std::mutex g_lock;                      // the compiled shaders (the update thread makes them, the draw thread reads)
std::vector<uint8_t> g_vs, g_ps;
bool g_failed = false;                  // said once
ID3D12Device* g_dev = nullptr;
ID3D12RootSignature* g_root = nullptr;
struct Pso { DXGI_FORMAT fmt; ID3D12PipelineState* pso; };
std::vector<Pso> g_psos;
ID3D12DescriptorHeap* g_rtvHeap = nullptr;

const char kShader[] = R"(
cbuffer G : register(b0) { float4 Tan; float2 Size; float Eye; float Cell; };
static const uint Glyphs[12] = { 0x7B6F, 0x749A, 0x73E7, 0x79E7, 0x49ED, 0x79CF, 0x7BCF, 0x4927, 0x7BEF, 0x79EF, 0x7249, 0x5AEB };
float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float Lit(uint g, float2 q) {
    if (q.x < 0 || q.y < 0 || q.x >= 3 || q.y >= 5) return 0;
    return (Glyphs[g] >> ((uint)q.y * 3 + (uint)q.x)) & 1;
}
float Number(uint n, float2 q) { return n >= 10 ? max(Lit(n / 10, q), Lit(n % 10, q - float2(4, 0))) : Lit(n, q); }
float ColumnOf(float t) { return (t - Tan.x) / (Tan.y - Tan.x) * Size.x; }
float RowOf(float t) { return (Tan.z - t) / (Tan.z - Tan.w) * Size.y; }
float4 Label(float4 c, float2 p, float2 at, uint n, float cell) {
    float2 q = (p - at) / cell;
    if (q.x < -0.5 || q.y < -0.5 || q.x >= 7.5 || q.y >= 5.5) return c;
    return Number(n, q) > 0 ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0.6);
}
float4 PSMain(float4 pos : SV_Position) : SV_Target {
    float2 p = pos.xy;
    float2 uv = p / Size;
    float tx = lerp(Tan.x, Tan.y, uv.x), ty = lerp(Tan.z, Tan.w, uv.y);
    float ax = degrees(atan(tx)), ay = degrees(atan(ty));
    float px = (Tan.y - Tan.x) / Size.x * 57.29578 / (1 + tx * tx);   // degrees a pixel, here
    float py = (Tan.z - Tan.w) / Size.y * 57.29578 / (1 + ty * ty);
    float dx = abs(frac(ax / 10 + 0.5) - 0.5) * 10 / px;              // pixels to the nearest line
    float dy = abs(frac(ay / 10 + 0.5) - 0.5) * 10 / py;
    float w = max(1.0, Cell * 0.5);
    float4 c = float4(0, 0, 0, 0);
    if (dx < w || dy < w) c = float4(1, 0.85, 0.1, 0.75);
    if (abs(ax) / px < w * 1.5 || abs(ay) / py < w * 1.5) c = float4(1, 0.1, 0.1, 0.9);   // the axis
    if ((abs(ax) / px < w * 3 && abs(ay) < 2) || (abs(ay) / py < w * 3 && abs(ax) < 2)) c = float4(1, 0.1, 0.1, 1);   // its cross
    float r = length(p - Size * 0.5);
    if (r > Cell * 3 && r < Cell * 4.5) c = float4(1, 0.2, 1, 0.95);   // the image's own middle
    float x0 = ColumnOf(0), y0 = RowOf(0);
    int kx = (int)round(ax / 10), ky = (int)round(ay / 10);
    if (kx != 0) c = Label(c, p, float2(ColumnOf(tan(radians(kx * 10.0))) + Cell, y0 + Cell), abs(kx) * 10, Cell);
    if (ky != 0) c = Label(c, p, float2(x0 + Cell, RowOf(tan(radians(ky * 10.0))) + Cell), abs(ky) * 10, Cell);
    float big = Cell * 4;
    float2 qe = (p - float2(x0 - 16 * Cell, y0 - 28 * Cell)) / big;   // the eye's letter, up and left of the axis
    if (qe.x >= -0.5 && qe.y >= -0.5 && qe.x < 3.5 && qe.y < 5.5) c = Lit(Eye > 0.5 ? 11 : 10, qe) > 0 ? float4(0.2, 1, 1, 1) : float4(0, 0, 0, 0.6);
    return c;
}
)";
struct Consts { float tan[4]; float size[2]; float eye, cell; };

void Compile() {
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_vs.empty() || g_failed) return;
    HMODULE comp = LoadLibraryW(L"d3dcompiler_47.dll");
    const pD3DCompile compile = comp ? (pD3DCompile)GetProcAddress(comp, "D3DCompile") : nullptr;
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
    const bool ok = compile && SUCCEEDED(compile(kShader, sizeof kShader - 1, "headset_grid", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &err)) &&
                    SUCCEEDED(compile(kShader, sizeof kShader - 1, "headset_grid", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &err));
    if (ok) {
        g_vs.assign((const uint8_t*)vs->GetBufferPointer(), (const uint8_t*)vs->GetBufferPointer() + vs->GetBufferSize());
        g_ps.assign((const uint8_t*)ps->GetBufferPointer(), (const uint8_t*)ps->GetBufferPointer() + ps->GetBufferSize());
    } else {
        g_failed = true;
        LogF("vrcam: headset grid - its shader could not be compiled%s%s", err ? ": " : "", err ? (const char*)err->GetBufferPointer() : "");
    }
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (err) err->Release();
}

// The root signature and the heap for `dev`, the pipeline for `fmt` (the draw thread, under g_lock).
ID3D12PipelineState* PsoFor(ID3D12Device* dev, DXGI_FORMAT fmt) {
    if (g_vs.empty() || g_failed) return nullptr;
    if (dev != g_dev) {   // (another device: everything made again)
        for (Pso& p : g_psos) p.pso->Release();
        g_psos.clear();
        if (g_root) { g_root->Release(); g_root = nullptr; }
        if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
        g_dev = dev;
    }
    for (const Pso& p : g_psos) if (p.fmt == fmt) return p.pso;
    if (!g_root) {
        HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
        const PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize = d12 ? (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants = {0, 0, sizeof(Consts) / 4};
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        const D3D12_ROOT_SIGNATURE_DESC rd{1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob *sig = nullptr, *err = nullptr;
        if (!serialize || FAILED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) ||
            FAILED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&g_root)))
            g_root = nullptr;
        if (sig) sig->Release();
        if (err) err->Release();
        const D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g_rtvHeap))) g_rtvHeap = nullptr;
        if (!g_root || !g_rtvHeap) { g_failed = true; LogF("vrcam: headset grid - its root signature could not be made"); return nullptr; }
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = g_root;
    pd.VS = {g_vs.data(), g_vs.size()};
    pd.PS = {g_ps.data(), g_ps.size()};
    D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ZERO;
    b.DestBlendAlpha = D3D12_BLEND_ONE;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = fmt;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState* pso = nullptr;
    if (FAILED(dev->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&pso))) {
        g_failed = true;
        LogF("vrcam: headset grid - no pipeline for the eyes' format %d: no grid", (int)fmt);
        return nullptr;
    }
    g_psos.push_back(Pso{fmt, pso});
    LogF("vrcam: headset grid - drawn into the eyes' pictures (format %d)", (int)fmt);
    return pso;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
bool On() { return g_on.load(std::memory_order_relaxed); }
void SetOnForTest(bool on) {
    if (on) Compile();
    g_on.store(on);
}

void LoadSettings(const wchar_t* iniPath) {
    const bool on = GetPrivateProfileIntW(L"headset_shot", L"grid", 0, iniPath) == 1;
    if (on) Compile();   // (here, on the update thread: the draw thread only makes the pipeline)
    if (on != g_on.load()) LogF("vrcam: headset grid %s ([headset_shot] grid)", on ? "on - the calibration grid in each eye's picture (native OpenXR)" : "off");
    g_on.store(on);
}

bool Draw(ID3D12GraphicsCommandList* list, ID3D12Resource* target, DXGI_FORMAT fmt, uint32_t w, uint32_t h, float tanL, float tanR, float tanU,
          float tanD, int eye) {
    if (!On() || !list || !target || !w || !h || !(tanR > tanL) || !(tanU > tanD)) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    ID3D12Device* dev = nullptr;
    if (FAILED(target->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return false;
    dev->Release();   // (the target holds it)
    ID3D12PipelineState* pso = PsoFor(dev, fmt);
    if (!pso) return false;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = fmt;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(target, &rv, rtv);   // (read when the list records it: one view is enough)
    const Consts k{{tanL, tanR, tanU, tanD}, {(float)w, (float)h}, eye ? 1.0f : 0.0f, CellPx(h)};
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    const D3D12_RECT sc{0, 0, (LONG)w, (LONG)h};
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    list->SetGraphicsRootSignature(g_root);
    list->SetPipelineState(pso);
    list->SetGraphicsRoot32BitConstants(0, sizeof k / 4, &k, 0);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    return true;
}

}  // namespace headgrid
