// merc_tint_test - [hud] merc_tint (vr/merc_tint.h) without the game or a headset:
//   - the decisions: the life from the game's stats, when the tint shows, the settings clamped, the capsule packed into the effect's frame;
//   - the effect's own HLSL (the text between the MERC marks in reshade/D2R_DepthFog.fx) compiled and run on a software D3D11 device
//     (WARP): for points in, at the edge of and outside a capsule, and colours, the effect's tint against the C++ mirror (Inside, Tint).
//   merc_tint_test [--fx <path to D2R_DepthFog.fx>]

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

#include "merc_tint.h"

using Microsoft::WRL::ComPtr;
using namespace merctint;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

static void Decisions() {
    // the life
    Expect(!LifeFromStats(100 * 256, 0).known, "no maximum: not known");
    Expect(!LifeFromStats(-1, 100 * 256).known, "a negative life: not known");
    Expect(!LifeFromStats(500 * 256, 100 * 256).known, "far above its maximum: not known");
    const Life half = LifeFromStats(40 * 256, 100 * 256);
    Expect(half.known && Near(half.frac, 0.4f, 1e-6f), "40 of 100 is 0.4");
    Expect(LifeFromStats(130 * 256, 100 * 256).frac == 1.0f, "above its maximum (a short boost) counts as full");
    // when it shows
    Config c;
    c.on = true;
    Expect(Show(c, half, true), "on, 40% under 50%, fresh: tinted");
    Expect(!Show(c, half, false), "a stale read: not tinted");
    Expect(!Show(c, LifeFromStats(60 * 256, 100 * 256), true), "60% is well");
    Expect(!Show(c, LifeFromStats(0, 100 * 256), true), "0 life (dead): not tinted");
    Expect(!Show(c, Life{}, true), "unknown life: not tinted");
    Config off;
    Expect(!Show(off, half, true), "off: never");
    c.threshold = 0.3f;
    Expect(!Show(c, half, true), "a 30% threshold: 40% is well");
    // settings
    Config wild;
    wild.threshold = 7.0f;
    wild.strength = -2.0f;
    const Config s = Sanitized(wild);
    Expect(s.threshold == 0.95f && s.strength == 0.0f, "the settings are clamped");
    Expect(Near(ThresholdFromPercent(50.0f), 0.5f, 1e-6f), "50 percent is 0.5");
    // freshness
    Expect(Fresh(1500, 1000) && !Fresh(2100, 1000) && !Fresh(1000, 0), "a read is fresh for a second");
    // the classes
    Expect(IsHireling(271) && IsHireling(338) && IsHireling(359) && IsHireling(560) && IsHireling(561) && !IsHireling(0) && !IsHireling(270),
           "the hireling classes");
    // the capsule into the effect's frame: render metres * upm - eyeWorld
    stroke::Body body;
    body.radius = 0.3f;
    body.bottom = {10.0f, 0.3f, 20.0f};
    body.top = {10.0f, 1.5f, 20.0f};
    const float eye[3] = {38.0f, 6.0f, 79.0f};
    Uniforms u;
    Expect(Pack(body, 4.0f, eye, 0.6f, &u), "a body packs");
    Expect(u.a[0] == 2.0f && u.a[1] == -4.8f && u.a[2] == 1.0f && u.a[3] == 1.2f, "the bottom and the radius in world units from the eye");
    Expect(u.b[0] == 2.0f && Near(u.b[1], 0.0f, 1e-5f) && u.b[2] == 1.0f && u.b[3] == 0.6f, "the top and the strength");
    stroke::Body bad = body;
    bad.radius = 0.0f;
    Expect(!Pack(bad, 4.0f, eye, 0.6f, &u), "a body without a radius does not pack");
    // BodyFor gives a merc a capsule standing on the floor: the floor itself is outside it
    meleetouch::MonsterRow row{2, 2, 0};
    Expect(meleetouch::BodyFor(row, 40.0f, 80.0f, 0.0f, 4.0f, body), "a merc's row makes a body");
    Expect(Pack(body, 4.0f, eye, 1.0f, &u), "and packs");
    const float floorUnder[3] = {u.a[0], u.a[1] - u.a[3], u.a[2]};
    Expect(Inside(floorUnder, u) < 1e-3f, "the floor right under the merc is not tinted (at the very edge: a radius from the bottom)");
    const float middle[3] = {(u.a[0] + u.b[0]) * 0.5f, (u.a[1] + u.b[1]) * 0.5f, (u.a[2] + u.b[2]) * 0.5f};
    Expect(Inside(middle, u) == 1.0f, "the middle of the body is tinted whole");
    // the colour
    float grey[3] = {0.5f, 0.5f, 0.5f};
    Tint(grey, 1.0f);
    Expect(grey[0] > 0.6f && Near(grey[1], 0.15f, 1e-6f) && Near(grey[2], 0.15f, 1e-6f), "grey goes red, keeping its brightness in the red");
    float none[3] = {0.2f, 0.7f, 0.1f};
    Tint(none, 0.0f);
    Expect(none[0] == 0.2f && none[1] == 0.7f && none[2] == 0.1f, "amount 0 is the colour as it was");
    printf("decisions checked\n");
}

// ---- the effect's MERC block on WARP --------------------------------------------------------------------------------------------
constexpr int kSamples = 16;
struct alignas(16) Cb {
    float mercA[4], mercB[4];
    float q[kSamples][4], col[kSamples][4];
};
static const char* kPrelude =
    "cbuffer C : register(b0) { float4 MercA; float4 MercB; float4 Q[16]; float4 Col[16]; };\n";
static const char* kMain =
    "\nvoid VS(uint id : SV_VertexID, out float4 pos : SV_Position) {\n"
    "    const float2 t = float2((id << 1) & 2, id & 2);\n"
    "    pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 PS(float4 p : SV_Position) : SV_Target {\n"
    "    const int i = min((int)p.x, 15);\n"
    "    return float4(MercTinted(Col[i].rgb, MercB.w * MercInside(Q[i].xyz)), MercInside(Q[i].xyz));\n"
    "}\n";

static bool Warp(const std::string& fx) {
    const size_t b = fx.find("// MERC-BEGIN"), e = fx.find("// MERC-END");
    if (b == std::string::npos || e == std::string::npos || e < b) { printf("FAIL: the MERC marks are not in the effect\n"); return false; }
    const size_t bodyAt = fx.find('\n', b) + 1;
    const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
    ComPtr<ID3DBlob> vsb, psb, errors;
    if (FAILED(D3DCompile(src.data(), src.size(), "merc.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors))) {
        printf("FAIL: vs: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        return false;
    }
    if (FAILED(D3DCompile(src.data(), src.size(), "merc.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &psb, &errors))) {
        printf("FAIL: ps: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
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

    // A merc's capsule (world units, as Pack makes them), points through it and around it, and colours.
    Uniforms u;
    u.a[0] = 2.0f; u.a[1] = -4.0f; u.a[2] = 1.0f; u.a[3] = 1.2f;
    u.b[0] = 2.0f; u.b[1] = 0.5f; u.b[2] = 1.0f; u.b[3] = 0.6f;
    Cb cb{};
    memcpy(cb.mercA, u.a, sizeof u.a);
    memcpy(cb.mercB, u.b, sizeof u.b);
    for (int i = 0; i < kSamples; ++i) {
        const float off = -1.6f + 0.2f * (float)i;   // sideways from the axis, across the soft edge and out
        cb.q[i][0] = 2.0f + off; cb.q[i][1] = -1.5f + 0.4f * (float)(i % 5); cb.q[i][2] = 1.0f + 0.1f * off; cb.q[i][3] = 0.0f;
        cb.col[i][0] = 0.1f + 0.05f * (float)i; cb.col[i][1] = 0.8f - 0.04f * (float)i; cb.col[i][2] = 0.3f; cb.col[i][3] = 1.0f;
    }
    cb.q[0][1] = u.a[1] - u.a[3];   // the floor right under the merc
    cb.q[0][0] = 2.0f; cb.q[0][2] = 1.0f;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (sizeof(Cb) + 15) & ~15u;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{&cb, 0, 0};
    ComPtr<ID3D11Buffer> buf;
    if (FAILED(dev->CreateBuffer(&bd, &init, &buf))) { printf("FAIL: no constant buffer\n"); return false; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSamples;
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
    const D3D11_VIEWPORT vp{0, 0, (float)kSamples, 1, 0, 1};
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
    std::vector<float> px(kSamples * 4);
    memcpy(px.data(), m.pData, px.size() * sizeof(float));
    ctx->Unmap(stage.Get(), 0);

    int inside = 0, outside = 0, edge = 0;
    for (int i = 0; i < kSamples; ++i) {
        const float q[3] = {cb.q[i][0], cb.q[i][1], cb.q[i][2]};
        const float k = Inside(q, u);
        float rgb[3] = {cb.col[i][0], cb.col[i][1], cb.col[i][2]};
        Tint(rgb, u.b[3] * k);
        char what[96];
        snprintf(what, sizeof what, "WARP sample %d: the effect's tint = the C++ mirror (inside %.3f)", i, k);
        Expect(Near(px[i * 4 + 3], k, 1e-4f) && Near(px[i * 4], rgb[0], 1e-4f) && Near(px[i * 4 + 1], rgb[1], 1e-4f) && Near(px[i * 4 + 2], rgb[2], 1e-4f), what);
        if (k >= 1.0f) ++inside; else if (k <= 0.0f) ++outside; else ++edge;
    }
    Expect(px[3] < 1e-3f, "WARP: the floor under the merc is not tinted");
    Expect(inside > 0 && outside > 0 && edge > 0, "the samples cover the inside, the soft edge and the outside");
    printf("the effect's tint on WARP: %d inside, %d on the edge, %d outside, all as the mirror\n", inside, edge, outside);
    return true;
}

int main(int argc, char** argv) {
    Decisions();
    std::string path = MERC_FX_PATH;
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "--fx") == 0) path = argv[i + 1];
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string fx = ss.str();
    Expect(!fx.empty(), "the effect reads");
    if (!fx.empty() && !Warp(fx)) ++g_fail;
    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("merc tint: all checks passed\n");
    return 0;
}
