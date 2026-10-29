// fog_spare_test - [fog] spare_effects (vr/fog_spare_math.h): how much of a pixel is the game's opaque scene, from its transparent
// resolve's own sums - the cases (no effects, a flame over a far wall, smoke, glow alone, a dark pixel, an empty accumulation), and
// the effect's own HLSL (the FOGSPARE marks in reshade/D2R_DepthFog.fx) compiled and run on WARP against the C++ mirror.
//   fog_spare_test [--fx <D2R_DepthFog.fx>]

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

#include "fog_spare_math.h"

using Microsoft::WRL::ComPtr;
using namespace fogspare;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}

struct Case { V3 r; float accum[4], reveal[4]; V3 glow; };

// The game's own resolve, written out: R = a S + (1 - a) T + A.
static V3 Resolve(V3 s, const float accum[4], const float reveal[4], V3 glow) {
    if (!(accum[3] > 0.0f)) return {s.r + glow.r, s.g + glow.g, s.b + glow.b};
    const float a = std::clamp(reveal[0], 0.0f, 1.0f);
    const float t[3] = {accum[0] / accum[3] * reveal[1], accum[1] / accum[3] * reveal[2], accum[2] / accum[3] * reveal[3]};
    return {a * s.r + (1 - a) * t[0] + glow.r, a * s.g + (1 - a) * t[1] + glow.g, a * s.b + (1 - a) * t[2] + glow.b};
}

static void Cases() {
    const float none[4] = {0, 0, 0, 0}, clear[4] = {1, 1, 1, 1};
    const V3 wall = {0.2f, 0.25f, 0.3f}, zero = {0, 0, 0};
    // no effects: all opaque
    Expect(std::fabs(OpaqueShare(Resolve(wall, none, clear, zero), none, clear, zero) - 1.0f) < 1e-6f, "no effects: the whole pixel is the scene (fogged as before)");
    // a flame (additive) over a far wall, four times its light: a fifth is the wall
    const V3 flame = {1.2f, 0.6f, 0.2f};
    const V3 r1 = Resolve(wall, none, clear, flame);
    const float s1 = OpaqueShare(r1, none, clear, flame);
    Expect(s1 > 0.05f && s1 < 0.35f, "a flame over a far wall: only the wall's share is fogged");
    Expect(std::fabs(s1 - Luma(wall) / Luma(r1)) < 1e-5f, "... exactly the wall's share of the light");
    // smoke (weighted-blended) half over the wall
    const float smoke[4] = {0.3f, 0.3f, 0.3f, 1.0f}, half[4] = {0.5f, 1, 1, 1};
    const V3 r2 = Resolve(wall, smoke, half, zero);
    const float s2 = OpaqueShare(r2, smoke, half, zero);
    Expect(std::fabs(s2 - 0.5f * Luma(wall) / Luma(r2)) < 1e-5f, "smoke half over the wall: the wall's half share");
    // all effect: nothing fogged
    const float thick[4] = {0.8f, 0.4f, 0.1f, 2.0f}, covered[4] = {0, 1, 1, 1};
    Expect(OpaqueShare(Resolve(wall, thick, covered, zero), thick, covered, zero) < 1e-5f, "a pixel all effect: no fog");
    // dark pixel: no division by zero, the scene's
    Expect(OpaqueShare(zero, none, clear, zero) == 1.0f, "a black pixel: as before");
    // negative or odd values never leave 0..1
    const float odd[4] = {-1, -1, -1, 0.5f}, oddR[4] = {2, -1, 3, 4};
    const float s3 = OpaqueShare({-1, 5, 0}, odd, oddR, {-2, 0, 0});
    Expect(s3 >= 0.0f && s3 <= 1.0f && std::isfinite(s3), "odd values: still 0..1");
}

static const char* kPrelude = "cbuffer C : register(b0) { float4 R[32]; float4 Acc[32]; float4 Rev[32]; float4 Glow[32]; };\n";
static const char* kMain =
    "\nvoid VS(uint id : SV_VertexID, out float4 pos : SV_Position) {\n"
    "    const float2 t = float2((id << 1) & 2, id & 2);\n"
    "    pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 PS(float4 p : SV_Position) : SV_Target { const int i = min((int)p.x, 31); return OpaqueShare(R[i].rgb, Acc[i], Rev[i], Glow[i].rgb).xxxx; }\n";

static bool Warp(const std::string& fx) {
    const size_t b = fx.find("// FOGSPARE-BEGIN"), e = fx.find("// FOGSPARE-END");
    if (b == std::string::npos || e == std::string::npos || e < b) { printf("FAIL: the FOGSPARE marks are not in the effect\n"); return false; }
    const size_t bodyAt = fx.find('\n', b) + 1;
    const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
    ComPtr<ID3DBlob> vsb, psb, errors;
    if (FAILED(D3DCompile(src.data(), src.size(), "spare.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors)) ||
        FAILED(D3DCompile(src.data(), src.size(), "spare.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &errors))) {
        printf("FAIL: compile: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        return false;
    }
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        printf("FAIL: no WARP device\n");
        return false;
    }
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    struct { float r[32][4], acc[32][4], rev[32][4], glow[32][4]; } cb{};
    uint32_t seed = 7;
    auto rnd = [&](float lo, float hi) { seed = seed * 1664525u + 1013904223u; return lo + (hi - lo) * (float)((seed >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < 32; ++i) {
        const V3 s = {rnd(0, 2), rnd(0, 2), rnd(0, 2)}, g = {i % 3 ? rnd(0, 3) : 0.0f, i % 3 ? rnd(0, 2) : 0.0f, 0.0f};
        float acc[4] = {rnd(0, 4), rnd(0, 4), rnd(0, 4), i % 4 ? rnd(0.1f, 4) : 0.0f}, rev[4] = {rnd(0, 1), rnd(0, 1), rnd(0, 1), rnd(0, 1)};
        const V3 r = i == 5 ? V3{0, 0, 0} : Resolve(s, acc, rev, g);
        memcpy(cb.r[i], &r, sizeof r);
        memcpy(cb.acc[i], acc, sizeof acc);
        memcpy(cb.rev[i], rev, sizeof rev);
        memcpy(cb.glow[i], &g, sizeof g);
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof cb;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{&cb, 0, 0};
    ComPtr<ID3D11Buffer> buf;
    if (FAILED(dev->CreateBuffer(&bd, &init, &buf))) { printf("FAIL: no constant buffer\n"); return false; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 32;
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
    const D3D11_VIEWPORT vp{0, 0, 32, 1, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[1] = {buf.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
    ctx->CopyResource(stage.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) { printf("FAIL: no read back\n"); return false; }
    std::vector<float> px(32 * 4);
    memcpy(px.data(), m.pData, px.size() * sizeof(float));
    ctx->Unmap(stage.Get(), 0);
    float worst = 0.0f;
    for (int i = 0; i < 32; ++i) {
        V3 r, g;
        memcpy(&r, cb.r[i], sizeof r);
        memcpy(&g, cb.glow[i], sizeof g);
        const float want = OpaqueShare(r, cb.acc[i], cb.rev[i], g), got = px[i * 4];
        worst = std::fmax(worst, std::fabs(want - got));
        char what[96];
        snprintf(what, sizeof what, "WARP sample %d: %.4f, the mirror %.4f", i, got, want);
        Expect(std::isfinite(got) && std::fabs(want - got) < 1e-4f, what);
    }
    printf("the opaque share on WARP: 32 samples as the mirror within %.6f\n", worst);
    return true;
}

int main(int argc, char** argv) {
    std::string path = FOGSPARE_FX_PATH;
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "--fx") == 0) path = argv[i + 1];
    Cases();
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string fx = ss.str();
    Expect(!fx.empty(), "the effect reads");
    if (!fx.empty()) {
        const size_t at = fx.find("if (SpareOn) {");
        Expect(at != std::string::npos && fx.find("t *= OpaqueShare(", at) != std::string::npos && fx.find("t *= OpaqueShare(", at) < fx.find("return lerp(colour, fogColour, t);", at),
               "the effect: the fog amount times the opaque share, before the fog is laid on");
        if (!Warp(fx)) ++g_fail;
    }
    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("fog spare: all checks passed\n");
    return 0;
}
