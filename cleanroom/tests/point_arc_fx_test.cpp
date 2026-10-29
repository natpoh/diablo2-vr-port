// [gameplay] teleport_circle: the effect's own beam and ring (reshade/D2R_DepthFog.fx, the PARC marks: PointArcLight) compiled with the system's HLSL
// compiler and run on WARP, without the game and without a headset - the text between the marks is the very shader the game draws. A few rays: one
// through the beam's middle, one past it, one onto the ring's line, one into the circle's middle, one away from it all; then the same at strength 0.
//
//   point_arc_fx_test        every check below

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int g_fails = 0;
void Expect(bool ok, const char* what) {
    if (!ok) ++g_fails;
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
}

constexpr int kRays = 6;
struct alignas(16) Cb {
    float pts[17][4];
    float ring[4], look[4], size[4];
    float ro[kRays][4], rd[kRays][4];
};
const char* kPrelude =
    "cbuffer C : register(b0) { float4 PaPts[17]; float4 PaRing; float4 PaLook; float4 PaSize; float4 Ro[6]; float4 Rd[6]; };\n";
const char* kMain =
    "\nvoid VS(uint id : SV_VertexID, out float4 pos : SV_Position) {\n"
    "    const float2 t = float2((id << 1) & 2, id & 2);\n"
    "    pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 PS(float4 p : SV_Position) : SV_Target {\n"
    "    const int i = min((int)p.x, 5);\n"
    "    return float4(PointArcLight(Ro[i].xyz, normalize(Rd[i].xyz)), 1.0);\n"
    "}\n";

void Unit(float* v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= l;
}

bool Render(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11VertexShader* vs, ID3D11PixelShader* ps, const Cb& cb, std::vector<float>* px) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (sizeof(Cb) + 15) & ~15u;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{&cb, 0, 0};
    ComPtr<ID3D11Buffer> buf;
    if (FAILED(dev->CreateBuffer(&bd, &init, &buf))) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kRays;
    td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> rt, stage;
    dev->CreateTexture2D(&td, nullptr, &rt);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &stage);
    ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
    ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
    ctx->OMSetRenderTargets(1, rtvs, nullptr);
    const D3D11_VIEWPORT vp{0, 0, (float)kRays, 1, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ID3D11Buffer* cbs[1] = {buf.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
    ctx->CopyResource(stage.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    px->resize(kRays * 4);
    memcpy(px->data(), m.pData, px->size() * sizeof(float));
    ctx->Unmap(stage.Get(), 0);
    return true;
}

}  // namespace

int main() {
    std::ifstream in(POINTARC_FX_PATH, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string fx = ss.str();
    const size_t b = fx.find("// PARC-BEGIN"), e = fx.find("// PARC-END");
    if (b == std::string::npos || e == std::string::npos || e < b) { printf("FAIL: the PARC marks are not in the effect\n"); return 1; }
    const size_t bodyAt = fx.find('\n', b) + 1;
    const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
    ComPtr<ID3DBlob> vsb, psb, errors;
    if (FAILED(D3DCompile(src.data(), src.size(), "parc.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors))) {
        printf("FAIL: vs: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        return 1;
    }
    if (FAILED(D3DCompile(src.data(), src.size(), "parc.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &psb, &errors))) {
        printf("FAIL: ps: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        return 1;
    }
    printf("the PARC block compiles (ps_5_0)\n");
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        printf("FAIL: no WARP device\n");
        return 1;
    }
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);

    // A straight beam across the view 5 ahead (x -2 .. 2, y 0, z -5), red at full strength, its core 0.04; the circle 2 below and 10 ahead, radius 1.
    Cb cb{};
    for (int i = 0; i < 17; ++i) {
        const float t = float(i) / 16.0f;
        cb.pts[i][0] = -2.0f + 4.0f * t; cb.pts[i][1] = 0.0f; cb.pts[i][2] = -5.0f; cb.pts[i][3] = t;
    }
    const float ring[4] = {0.0f, -2.0f, -10.0f, 1.0f}, look[4] = {1.0f, 0.15f, 0.1f, 1.0f}, size[4] = {0.04f, 0.1f, 0.5f, 0.0012f};
    memcpy(cb.ring, ring, sizeof ring);
    memcpy(cb.look, look, sizeof look);
    memcpy(cb.size, size, sizeof size);
    const float dirs[kRays][3] = {
        {0.0f, 0.0f, -5.0f},    // 0: the beam's middle
        {0.0f, 3.0f, -5.0f},    // 1: well above it, nothing below
        {1.0f, -2.0f, -10.0f},  // 2: onto the ring's line
        {0.0f, -2.0f, -10.0f},  // 3: into the circle's middle
        {0.0f, 0.0f, 5.0f},     // 4: away, behind
        {0.0f, 0.12f, -5.0f},   // 5: 0.12 above the beam: three core radii - in its glow, not its core
    };
    for (int i = 0; i < kRays; ++i) {
        float d[3] = {dirs[i][0], dirs[i][1], dirs[i][2]};
        Unit(d);
        cb.rd[i][0] = d[0]; cb.rd[i][1] = d[1]; cb.rd[i][2] = d[2];
    }
    std::vector<float> px;
    if (!Render(dev.Get(), ctx.Get(), vs.Get(), ps.Get(), cb, &px)) { printf("FAIL: no render\n"); return 1; }
    auto red = [&](int i) { return px[i * 4 + 0]; };
    auto green = [&](int i) { return px[i * 4 + 1]; };
    for (int i = 0; i < kRays; ++i) printf("  ray %d: %.3f %.3f %.3f\n", i, px[i * 4], px[i * 4 + 1], px[i * 4 + 2]);
    Expect(red(0) > 0.4f && green(0) > 0.2f, "the beam's middle: bright, its core white-hot (green in it too)");
    Expect(red(1) < 0.01f, "above the beam, no ground below: nothing");
    Expect(red(2) > 0.5f, "onto the ring's line: bright");
    Expect(red(3) > 0.02f && red(3) < 0.3f, "the circle's middle: a faint fill");
    Expect(red(4) < 0.001f, "away from it all: nothing");
    Expect(red(5) > 0.05f && red(5) < red(0) && green(5) < 0.5f * green(0), "in the glow beside the core: dimmer, redder");
    Expect(green(2) < red(2), "the ring is the colour (red), not white");
    cb.look[3] = 0.0f;
    if (!Render(dev.Get(), ctx.Get(), vs.Get(), ps.Get(), cb, &px)) { printf("FAIL: no render\n"); return 1; }
    bool dark = true;
    for (float v : px) dark = dark && (v == 0.0f || v == 1.0f);   // (alpha is 1)
    Expect(dark, "strength 0: no light at all");
    cb.look[0] = 0.1f; cb.look[1] = 0.3f; cb.look[2] = 1.0f; cb.look[3] = 1.0f;
    if (!Render(dev.Get(), ctx.Get(), vs.Get(), ps.Get(), cb, &px)) { printf("FAIL: no render\n"); return 1; }
    Expect(px[2 * 4 + 2] > px[2 * 4 + 0], "a blue colour: the ring blue");
    if (g_fails) {
        printf("point_arc_fx_test: %d FAILED\n", g_fails);
        return 1;
    }
    printf("point_arc_fx_test: all passed\n");
    return 0;
}
