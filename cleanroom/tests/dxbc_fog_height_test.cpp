// dxbc_fog_height_test - shared/dxbc_fog_height.h: the game's distance fog thinned with height, by patching its shader's own bytecode.
//   1. a pixel shader of our own with the fog's shape (dp4, sqrt, div by cb13[9].x, exp, min with 1) is found and patched; its container's
//      checksum holds before and after; D3D11 (WARP) takes the patched one and, drawn, it gives t' = 1 - (1 - t) * saturate((top - h) / soft)
//      for points below, inside and above the fade - h the point's height above the eye from cb3[9..11];
//   2. a shader without the fog is left alone;
//   dxbc_fog_height_test [<folder of the game's ps_atmosphere_apply permutations, permNN.dxbc>]: each one's own checksum holds (the DXBC
//      hash is right), the fog's are found and patched (15 - 29), the rest not; D3D11 takes every patched one.
#include "dxbc_fog_height.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static const char* kFogPs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB13 : register(b13) { float4 c13[30]; }
Texture2D<float> depth : register(t0);   // (1 in the tests: the view-space position comes from a read, as the game's from its depth)
float4 main(float4 pos : SV_Position) : SV_Target {
    const float s = depth.Load(int3(0, 0, 0));
    float4 vp = float4(c3[0].x * s, c3[0].y * s, -c3[0].z * s, 1.0);   // the view-space position (x, y, -z), as the game's
    float t = min(1.0, exp(-c13[9].y * (sqrt(dot(vp, vp)) / c13[9].x - 1.0)));
    // t used on, as the game's pass lays the fog's colour with it (c13[10] = 1, 0 in the tests: t itself); the camera's rows to cb3[11] read
    return float4(t * c13[10].x + c13[10].y, vp.x * t + vp.y, vp.z * t, 1);   // (the view-space position used after t, as the game's height fog does)   // (vp kept on, as the game's)
}
)";
// the game's fog with its mist: the fog's colour * (1 - t) + the volumetric mist (from a read, as the game's sample of its mist texture)
static const char* kMistPs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB13 : register(b13) { float4 c13[30]; }
Texture2D<float> depth : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    const float s = depth.Load(int3(0, 0, 0));
    float4 vp = float4(c3[0].x * s, c3[0].y * s, -c3[0].z * s, 1.0);
    float t = min(1.0, exp(-c13[9].y * (sqrt(dot(vp, vp)) / c13[9].x - 1.0)));
    float3 mist = s * c13[12].xyz;
    float3 col = c13[10].xyz * (1.0 - t) + mist;
    return float4(col * c13[13].x, vp.x * t + vp.y + vp.z * t);   // (col used on: in a temp, as the game's)
}
)";
// the game's void: past the linear depth's far end (cb10[53]) the pass gives the mist as it is, fogged inside it (c13[14].x scales the read:
// 1 the void, 0.5 inside)
static const char* kVoidPs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[30]; }
Texture2D<float> depth : register(t0);
Texture2D<float4> mistTex : register(t1);   // (the game's mist: a texture of its own, sampled)
float4 main(float4 pos : SV_Position) : SV_Target {
    const float s = depth.Load(int3(0, 0, 0)) * c13[14].x;
    const float lin = s * c10[53].x + c10[53].z;
    const bool inside = lin < c10[53].z + c10[53].x;
    float4 vp = float4(c3[0].x * s, c3[0].y * s, -c3[0].z * s, 1.0);
    float t = min(1.0, exp(-c13[9].y * (sqrt(dot(vp, vp)) / c13[9].x - 1.0)));
    const float4 mist = mistTex.Load(int3(0, 0, 0));
    float4 fogged = float4(c13[10].xyz * (1.0 - t) + mist.xyz, mist.w * t);
    fogged.w += (vp.x * t + vp.y + vp.z * t) * c13[13].y;   // (the view-space position used after t, as the game's height fog does)
    return inside ? fogged : mist;
}
)";
static const char* kVs = R"(
float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0, 1); }
)";
static const char* kPlainPs = "float4 main() : SV_Target { return float4(1, 0, 0, 1); }";

static std::vector<uint8_t> Compile(const char* src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    const HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) { std::printf("compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?"); if (err) err->Release(); return {}; }
    std::vector<uint8_t> v((const uint8_t*)code->GetBufferPointer(), (const uint8_t*)code->GetBufferPointer() + code->GetBufferSize());
    code->Release();
    return v;
}

static float g_c10[4 * 54] = {};   // (the linear depth's range at [53]: the void's test - 0 but in 1c)
static ID3D11ShaderResourceView* g_mist = nullptr;   // (1c: the mist's texture at t1)
// draws the shader once into a 1x1 float target with the constants given; the red channel back
static bool Draw(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::vector<uint8_t>& ps, const float c3[4 * 38], const float c13[4 * 30], float* red) {
    const std::vector<uint8_t> vs = Compile(kVs, "vs_5_0");
    ID3D11VertexShader* v = nullptr;
    ID3D11PixelShader* p = nullptr;
    if (FAILED(dev->CreateVertexShader(vs.data(), vs.size(), nullptr, &v)) || FAILED(dev->CreatePixelShader(ps.data(), ps.size(), nullptr, &p))) return false;
    D3D11_TEXTURE2D_DESC td{1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *rt = nullptr, *st = nullptr;
    dev->CreateTexture2D(&td, nullptr, &rt);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &st);
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(rt, nullptr, &rtv);
    auto cb = [&](const float* data, UINT n) {
        D3D11_BUFFER_DESC bd{n, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
        ID3D11Buffer* b = nullptr;
        dev->CreateBuffer(&bd, &sd, &b);
        return b;
    };
    ID3D11Buffer* b3 = cb(c3, 4 * 38 * 4);
    ID3D11Buffer* b10 = cb(g_c10, 4 * 54 * 4);
    ID3D11Buffer* b13 = cb(c13, 4 * 30 * 4);
    // the "depth" read: a 1x1 texture of 1
    D3D11_TEXTURE2D_DESC dd{1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    const float one = 1.0f;
    D3D11_SUBRESOURCE_DATA ds{&one, 4, 4};
    ID3D11Texture2D* dt = nullptr;
    ID3D11ShaderResourceView* dsrv = nullptr;
    dev->CreateTexture2D(&dd, &ds, &dt);
    dev->CreateShaderResourceView(dt, nullptr, &dsrv);
    ctx->PSSetShaderResources(0, 1, &dsrv);
    ctx->PSSetShaderResources(1, 1, &g_mist);
    ctx->PSSetConstantBuffers(3, 1, &b3);
    ctx->PSSetConstantBuffers(13, 1, &b13);
    ctx->PSSetConstantBuffers(10, 1, &b10);
    ctx->VSSetShader(v, nullptr, 0);
    ctx->PSSetShader(p, nullptr, 0);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, 1, 1, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    dev->CreateRasterizerState(&rd, &rs);
    ctx->RSSetState(rs);
    ctx->Draw(3, 0);
    ctx->CopyResource(st, rt);
    D3D11_MAPPED_SUBRESOURCE m{};
    const bool ok = SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m));
    if (ok) { *red = ((const float*)m.pData)[0]; ctx->Unmap(st, 0); }
    if (rs) rs->Release();
    dsrv->Release(); dt->Release();
    rtv->Release(); rt->Release(); st->Release(); b3->Release(); b10->Release(); b13->Release(); v->Release(); p->Release();
    return ok;
}

int main(int argc, char** argv) {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)));
    if (!dev) return 1;
    // 1. ours
    const std::vector<uint8_t> fog = Compile(kFogPs, "ps_5_0");
    CHECK(!fog.empty() && dxbcfog::ChecksumHolds(fog.data(), fog.size()));
    std::string why;
    dxbcfog::Found f;
    CHECK(dxbcfog::Find(fog.data(), fog.size(), &f, &why));
    if (!why.empty()) {
        std::printf("  find: %s\n", why.c_str());
        ID3DBlob* dis = nullptr;
        if (SUCCEEDED(D3DDisassemble(fog.data(), fog.size(), 0, nullptr, &dis))) { std::printf("%s\n", (const char*)dis->GetBufferPointer()); dis->Release(); }
    }
    const float top = 60.0f, soft = 50.0f;
    const std::vector<uint8_t> patched = dxbcfog::Patch(fog.data(), fog.size(), top, soft, &why);
    CHECK(!patched.empty() && dxbcfog::ChecksumHolds(patched.data(), patched.size()));
    // the camera: the view-space position's y is the height above the eye (cb3[9..11] = identity's rows), the fog plane 30, density 0.5
    float c3[4 * 38] = {}, c13[4 * 30] = {};
    // the camera's rows (world from view): turned, so x, y and z all count in the height - h = 0.2 x + y + 0.1 z (view z: -100)
    c3[9 * 4 + 0] = 1; c3[9 * 4 + 1] = 0.2f; c3[10 * 4 + 1] = 1; c3[11 * 4 + 1] = 0.1f; c3[11 * 4 + 2] = 1;
    c13[9 * 4 + 0] = 30.0f; c13[9 * 4 + 1] = 0.5f;
    c13[10 * 4 + 0] = 1.0f;
    for (float h : {-20.0f, 0.0f, 20.0f, 35.0f, 50.0f, 80.0f, 150.0f}) {
        c3[0] = 150.0f; c3[1] = h; c3[2] = 100.0f;   // a point 150 across, h up, 100 ahead
        const float dist = std::sqrt(150.0f * 150.0f + h * h + 100.0f * 100.0f + 1.0f);
        const float t = std::fmin(1.0f, std::exp(-0.5f * (dist / 30.0f - 1.0f)));
        const float hw = 0.2f * 150.0f + h + 0.1f * -100.0f;   // the height the patch reads
        const float want = 1.0f - (1.0f - t) * dxbcfog::FogFactor(hw, top, soft);
        float got0 = -1, got = -1;
        CHECK(Draw(dev, ctx, fog, c3, c13, &got0) && Draw(dev, ctx, patched, c3, c13, &got));
        std::printf("  h %6.1f: the game's t %.4f, thinned %.4f (expected %.4f)\n", h, got0, got, want);
        CHECK(std::fabs(got0 - t) < 1e-3f && std::fabs(got - want) < 1e-3f);
    }
    // 1a. [fog] game_fog_our_curve: our effect's curve (D2R_DepthFog.fx FogAt: strength x saturate((d - start) / (end - start)) ^ curve) in
    // place of the game's exponential - alone (Serg's numbers: 50, 150, curve 1; a cave's 105, 210, curve 1.6, strength 0.8), and with the
    // height thinning on top of it
    {
        struct Case { dxbcfog::Curve c; float top, soft; };
        const Case cases[] = {{{true, 50.0f, 150.0f, 1.0f, 1.0f}, 1.0e9f, 1.0f},
                              {{true, 105.0f, 210.0f, 1.6f, 0.8f}, 1.0e9f, 1.0f},
                              {{true, 50.0f, 150.0f, 1.0f, 1.0f}, top, soft}};
        int checked = 0;
        for (const Case& k : cases) {
            const std::vector<uint8_t> pc = dxbcfog::Patch(fog.data(), fog.size(), k.top, k.soft, &why, false, false, false, &k.c);
            CHECK(!pc.empty() && dxbcfog::ChecksumHolds(pc.data(), pc.size()));
            for (float dz : {20.0f, 60.0f, 75.0f, 100.0f, 125.0f, 160.0f, 190.0f, 240.0f})
                for (float h : {0.0f, 40.0f}) {
                    c3[0] = 0.0f; c3[1] = h; c3[2] = dz;
                    const float dist = std::sqrt(h * h + dz * dz + 1.0f);
                    const float fogAt = dxbcfog::CurveFog(k.c, dist);
                    // FogAt itself, as the effect computes it (pow(saturate((d - start) / max(end - start, 1)), curve) * strength)
                    const float x = std::fmin(1.0f, std::fmax(0.0f, (dist - k.c.start) / std::fmax(k.c.end - k.c.start, 1.0f)));
                    CHECK(std::fabs(fogAt - std::pow(x, k.c.curve) * k.c.strength) < 1e-5f);
                    const float hw = h + 0.1f * -dz;   // (the height the patch reads: the camera's rows as above, x = 0)
                    const float want = 1.0f - fogAt * dxbcfog::FogFactor(hw, k.top, k.soft);
                    float got = -1;
                    CHECK(Draw(dev, ctx, pc, c3, c13, &got));
                    if (std::fabs(got - want) > 2e-3f)
                        std::printf("  our curve %.0f-%.0f ^%.1f x%.1f (top %.0f), distance %.1f: drawn t %.4f, FogAt says %.4f\n", k.c.start, k.c.end,
                                    k.c.curve, k.c.strength, k.top, dist, got, want);
                    CHECK(std::fabs(got - want) < 2e-3f);
                    ++checked;
                }
        }
        c3[0] = 150.0f; c3[1] = 0.0f; c3[2] = 100.0f;
        std::printf("  our curve: %d points drawn by the patched shader, each as FogAt (Serg's 50 - 150: 10 / 25 / 50 / 75 / 100 %% at 60 / 75 / 100 / "
                    "125 / 150, not the exponential's 26 / 53 / 78 / 89 / 95)\n", checked);
    }
    // 1b. the mist fogged by distance as well: in a cave our fog is black - the far walls go black, the mist alone stayed (a light band)
    {
        const std::vector<uint8_t> mistPs = Compile(kMistPs, "ps_5_0");
        dxbcfog::Found fm;
        CHECK(dxbcfog::Find(mistPs.data(), mistPs.size(), &fm, &why) && fm.mistEnd != 0);
        if (!fm.mistEnd) {
            ID3DBlob* dis = nullptr;
            if (SUCCEEDED(D3DDisassemble(mistPs.data(), mistPs.size(), 0, nullptr, &dis))) { std::printf("%s\n", (const char*)dis->GetBufferPointer()); dis->Release(); }
        }
        const std::vector<uint8_t> pm = dxbcfog::Patch(mistPs.data(), mistPs.size(), top, soft, &why, true);
        CHECK(!pm.empty() && dxbcfog::ChecksumHolds(pm.data(), pm.size()));
        float m3[4 * 38] = {}, m13[4 * 30] = {};
        std::memcpy(m3, c3, sizeof m3);
        m13[9 * 4 + 0] = 30.0f; m13[9 * 4 + 1] = 0.5f;   // the fog black (the cave's), the mist 0.5
        m13[12 * 4 + 0] = 0.5f;
        m13[13 * 4 + 0] = 1.0f;
        for (float h : {0.0f, 80.0f}) {
            m3[0] = 150.0f; m3[1] = h; m3[2] = 100.0f;
            const float dist = std::sqrt(150.0f * 150.0f + h * h + 100.0f * 100.0f + 1.0f);
            const float t = std::fmin(1.0f, std::exp(-0.5f * (dist / 30.0f - 1.0f)));
            const float hw = 0.2f * 150.0f + h + 0.1f * -100.0f;
            const float tp = 1.0f - (1.0f - t) * dxbcfog::FogFactor(hw, top, soft);
            float g0 = -1, g1 = -1;
            CHECK(Draw(dev, ctx, mistPs, m3, m13, &g0) && Draw(dev, ctx, pm, m3, m13, &g1));
            std::printf("  mist, h %5.1f: the game's %.4f (the mist whole), fogged %.4f (expected 0.5 x t' = %.4f)\n", h, g0, g1, 0.5f * tp);
            CHECK(std::fabs(g0 - 0.5f) < 1e-3f && std::fabs(g1 - 0.5f * tp) < 1e-3f);
        }
        const std::vector<uint8_t> noMist = dxbcfog::Patch(mistPs.data(), mistPs.size(), top, soft, &why, false);
        float g2 = -1;
        m3[1] = 0.0f;
        CHECK(Draw(dev, ctx, noMist, m3, m13, &g2) && std::fabs(g2 - 0.5f) < 1e-3f);   // without: the mist as the game's
        // [fog] game_fog_before_fx: the fog's colour left out (the scene fogged before the resolve), the mist still fogged by t'
        {
            m13[10 * 4 + 0] = 0.3f;   // a fog of some colour
            const std::vector<uint8_t> nf = dxbcfog::Patch(mistPs.data(), mistPs.size(), 1.0e9f, 1.0f, &why, true, false, true);
            const std::vector<uint8_t> wf = dxbcfog::Patch(mistPs.data(), mistPs.size(), 1.0e9f, 1.0f, &why, true, false, false);
            CHECK(!nf.empty() && dxbcfog::ChecksumHolds(nf.data(), nf.size()));
            const float dist = std::sqrt(150.0f * 150.0f + 100.0f * 100.0f + 1.0f);
            const float t = std::fmin(1.0f, std::exp(-0.5f * (dist / 30.0f - 1.0f)));
            float a = -1, b2 = -1;
            CHECK(Draw(dev, ctx, nf, m3, m13, &a) && Draw(dev, ctx, wf, m3, m13, &b2));
            std::printf("  no fog's colour: %.4f (expected the mist alone 0.5 x t = %.4f); with it %.4f (expected %.4f)\n", a, 0.5f * t, b2,
                        0.3f * (1.0f - t) + 0.5f * t);
            CHECK(std::fabs(a - 0.5f * t) < 1e-3f && std::fabs(b2 - (0.3f * (1.0f - t) + 0.5f * t)) < 1e-3f);
            m13[10 * 4 + 0] = 0.0f;
        }
    }
    // 1c. the void left alone: past the linear depth's far end the game's pass gives its mist whole (in a cave, past the ceiling's rim: a
    // light band in the black) - patched, (0, 0, 0, 1): nothing laid over it; inside, as the mist patch alone
    {
        const std::vector<uint8_t> voidPs = Compile(kVoidPs, "ps_5_0");
        dxbcfog::Found fv;
        CHECK(dxbcfog::Find(voidPs.data(), voidPs.size(), &fv, &why) && fv.voidAt != 0 && fv.mistEnd != 0);
        if (!fv.voidAt || !fv.mistEnd) {
            std::printf("  find: %s\n", why.c_str());
            ID3DBlob* dis = nullptr;
            if (SUCCEEDED(D3DDisassemble(voidPs.data(), voidPs.size(), 0, nullptr, &dis))) { std::printf("%s\n", (const char*)dis->GetBufferPointer()); dis->Release(); }
        }
        const std::vector<uint8_t> both = dxbcfog::Patch(voidPs.data(), voidPs.size(), 1.0e9f, 1.0f, &why, true, true);
        const std::vector<uint8_t> mistOnly = dxbcfog::Patch(voidPs.data(), voidPs.size(), 1.0e9f, 1.0f, &why, true, false);
        CHECK(!both.empty() && dxbcfog::ChecksumHolds(both.data(), both.size()) && !mistOnly.empty());
        float v3[4 * 38] = {}, v13[4 * 30] = {};
        std::memcpy(v3, c3, sizeof v3);
        v3[0] = 150.0f; v3[1] = 0.0f; v3[2] = 100.0f;
        v13[9 * 4 + 0] = 30.0f; v13[9 * 4 + 1] = 0.5f;   // the fog black, the mist 0.5
        {   // the mist: 0.5 of each colour, alpha 1
            D3D11_TEXTURE2D_DESC md{1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            const float m[4] = {0.5f, 0.5f, 0.5f, 1.0f};
            D3D11_SUBRESOURCE_DATA ms{m, 16, 16};
            ID3D11Texture2D* mt = nullptr;
            dev->CreateTexture2D(&md, &ms, &mt);
            if (mt) { dev->CreateShaderResourceView(mt, nullptr, &g_mist); mt->Release(); }
        }
        g_c10[53 * 4 + 0] = 1000.0f;                      // the linear depth: the read x 1000, its far end at 1000
        v13[14 * 4 + 0] = 1.0f;                           // the void
        float g0 = -1, g1 = -1, g2 = -1;
        CHECK(Draw(dev, ctx, voidPs, v3, v13, &g0) && Draw(dev, ctx, both, v3, v13, &g1) && Draw(dev, ctx, mistOnly, v3, v13, &g2));
        std::printf("  the void: the game's %.4f (its mist whole), left alone %.4f (expected 0), the mist patch alone %.4f (expected 0.5)\n", g0, g1, g2);
        CHECK(std::fabs(g0 - 0.5f) < 1e-3f && std::fabs(g1) < 1e-3f && std::fabs(g2 - 0.5f) < 1e-3f);
        v13[14 * 4 + 0] = 0.5f;                           // inside: the same as the mist patch alone
        CHECK(Draw(dev, ctx, both, v3, v13, &g1) && Draw(dev, ctx, mistOnly, v3, v13, &g2));
        std::printf("  inside: left alone %.4f, the mist patch alone %.4f (the same)\n", g1, g2);
        CHECK(std::fabs(g1 - g2) < 1e-4f && g1 > 0.0f && g1 < 0.5f);
        g_c10[53 * 4 + 0] = 0.0f;
        if (g_mist) { g_mist->Release(); g_mist = nullptr; }
    }
    // 2. a shader without the fog
    const std::vector<uint8_t> plain = Compile(kPlainPs, "ps_5_0");
    CHECK(!dxbcfog::Find(plain.data(), plain.size(), &f) && dxbcfog::Patch(plain.data(), plain.size(), top, soft).empty());
    // the game's own permutations, when given
    if (argc > 1) {
        int found = 0, taken = 0, holds = 0, mists = 0, voids = 0;
        const dxbcfog::Curve gameCurve{true, 50.0f, 150.0f, 1.0f, 1.0f};   // (every third with our curve too)
        for (int i = 0; i < 30; ++i) {
            char p[MAX_PATH];
            snprintf(p, sizeof p, "%s\\perm%02d.dxbc", argv[1], i);
            FILE* fi = fopen(p, "rb");
            if (!fi) continue;
            std::vector<uint8_t> b;
            uint8_t buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, fi)) > 0) b.insert(b.end(), buf, buf + n);
            fclose(fi);
            holds += dxbcfog::ChecksumHolds(b.data(), b.size());
            std::string w;
            if (!dxbcfog::Find(b.data(), b.size(), &f, &w)) {
                if (i >= 15) std::printf("  perm %02d not found: %s\n", i, w.c_str());
                CHECK(i < 15);
                continue;
            }
            CHECK(i >= 15);
            ++found;
            mists += f.mistEnd != 0;
            voids += f.voidAt != 0;
            CHECK((f.voidAt != 0) == (f.mistEnd != 0));   // (a permutation with a mist gives it over the void; one without, (0, 0, 0, 1) already)
            const std::vector<uint8_t> q = dxbcfog::Patch(b.data(), b.size(), top, soft, nullptr, true, true, i % 2 == 1, i % 3 == 0 ? &gameCurve : nullptr);   // (the mist fogged too, the void left alone; every other one without the fog's colour)
            ID3D11PixelShader* ps = nullptr;
            const bool ok = !q.empty() && dxbcfog::ChecksumHolds(q.data(), q.size()) && SUCCEEDED(dev->CreatePixelShader(q.data(), q.size(), nullptr, &ps));
            if (ps) ps->Release();
            taken += ok;
        }
        std::printf("  the game's permutations: %d checksums hold, %d with the distance fog found (%d with a mist after it, %d with the mist over "
                    "the void), %d patched and taken by D3D11\n", holds, found, mists, voids, taken);
        CHECK(holds == 30 && found == 15 && taken == 15);
    }
    ctx->Release();
    dev->Release();
    if (g_fail) { std::printf("dxbc_fog_height_test: %d FAILED\n", g_fail); return 1; }
    std::printf("dxbc_fog_height_test: the fog's shader found, patched, its checksum right; drawn, the fog thinned with height as asked: all passed\n");
    return 0;
}
