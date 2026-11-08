// lit_flat_test - [render] bench_lit_flat (vr/lit_flat.h): our two flat pixel shaders compiled, their input layouts exactly the game's (so
// they link in the game's pipelines), and drawn on WARP (D3D11's software device) with the game's bindings: the material's colour times the
// light times the frame's exposure into the first target, the cleared value into the second.
//   lit_flat_test.exe [<folder with the game's ps_models_p.bin, ps_terrain.bin, ... from tools/casc_extract>]
// Given the folder, every permutation of the game's lit shaders is checked as the plugin checks it (models' family / terrain / not ours).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "lit_flat.h"

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;

static std::vector<uint8_t> Compile(const char* src, size_t len, const char* target, const char* light) {
    const D3D_SHADER_MACRO m[] = {{"LIGHT", light}, {nullptr, nullptr}};
    ID3DBlob* b = nullptr;
    ID3DBlob* e = nullptr;
    if (FAILED(D3DCompile(src, len, "t", light ? m : nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
        std::printf("compile: %s\n", e ? (const char*)e->GetBufferPointer() : "?");
        return {};
    }
    std::vector<uint8_t> v((uint8_t*)b->GetBufferPointer(), (uint8_t*)b->GetBufferPointer() + b->GetBufferSize());
    b->Release();
    return v;
}

static const char kModelsVs[] = R"(
struct V { float4 pos : SV_POSITION; float4 p : POSITION; float3 n : NORMAL; float4 t : TANGENT; float2 uv : TEXCOORD0; float4 w : TEXCOORD1; };
V main(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0.5, 1); o.p = o.pos; o.n = float3(0, 0, 1); o.t = float4(1, 0, 0, 1);
    o.uv = c; o.w = float4(0, 0, 0, 1); return o;
})";
static const char kTerrainVs[] = R"(
struct V { float4 pos : SV_POSITION; float4 p : POSITION; float3 pvs : POS_VS; float3 pws : POS_WS; float3 nvs : NORMAL_VS; float4 tvs : TANGENT_VS; };
V main(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0.5, 1); o.p = o.pos; o.pvs = 0; o.pws = float3(c.x * 90, 7, c.y * 90);
    o.nvs = float3(0, 1, 0); o.tvs = float4(1, 0, 0, 1); return o;
})";

// Draws a full-screen triangle with ps; the material texture at register `texReg`; returns the two targets' pixel (4, 4)
static bool Draw(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, UINT texReg, const float colour[4], float exposure,
                 float out0[4], float out1[4]) {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    if (FAILED(g_dev->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &vs)) ||
        FAILED(g_dev->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &ps)))
        return false;
    const UINT W = 8;
    ID3D11Texture2D* rt[2] = {};
    ID3D11RenderTargetView* rtv[2] = {};
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC d{W, W, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        g_dev->CreateTexture2D(&d, nullptr, &rt[i]);
        g_dev->CreateRenderTargetView(rt[i], nullptr, &rtv[i]);
        const float z[4] = {9, 9, 9, 9};
        g_ctx->ClearRenderTargetView(rtv[i], z);
    }
    D3D11_TEXTURE2D_DESC td{1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA ts{colour, 16, 16};
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    g_dev->CreateTexture2D(&td, &ts, &tex);
    g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* smp = nullptr;
    g_dev->CreateSamplerState(&sd, &smp);
    std::vector<float> f(90 * 4, 0.0f);
    f[0] = exposure;   // b10[0].x: the frame's exposure; [3].zw, [13].y 0
    D3D11_BUFFER_DESC bd{(UINT)(f.size() * 4), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA bs{f.data(), 0, 0};
    ID3D11Buffer* cb = nullptr;
    g_dev->CreateBuffer(&bd, &bs, &cb);
    D3D11_VIEWPORT vp{0, 0, (float)W, (float)W, 0, 1};
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetRenderTargets(2, rtv, nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->VSSetShader(vs, nullptr, 0);
    g_ctx->PSSetShader(ps, nullptr, 0);
    g_ctx->PSSetShaderResources(texReg, 1, &srv);
    g_ctx->PSSetSamplers(0, 1, &smp);
    g_ctx->PSSetConstantBuffers(10, 1, &cb);
    g_ctx->Draw(3, 0);
    float* outs[2] = {out0, out1};
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC d{W, W, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
        ID3D11Texture2D* st = nullptr;
        g_dev->CreateTexture2D(&d, nullptr, &st);
        g_ctx->CopyResource(st, rt[i]);
        D3D11_MAPPED_SUBRESOURCE m{};
        g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        const float* px = (const float*)((const uint8_t*)m.pData + 4 * m.RowPitch) + 4 * 4;
        for (int c = 0; c < 4; ++c) outs[i][c] = px[c];
        g_ctx->Unmap(st, 0);
        st->Release();
    }
    cb->Release(); smp->Release(); srv->Release(); tex->Release();
    for (int i = 0; i < 2; ++i) { rtv[i]->Release(); rt[i]->Release(); }
    ps->Release(); vs->Release();
    return true;
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static void GameFolder(const std::string& dir) {
    struct F { const char* file; litflat::Family want; };
    const F files[] = {{"ps_models_p.bin", litflat::kModels}, {"ps_foliage_p.bin", litflat::kModels}, {"ps_grunge_p.bin", litflat::kModels},
                       {"ps_alpha_threshold_p.bin", litflat::kModels}, {"ps_terrain.bin", litflat::kTerrain},
                       {"ps_detail_normal_p.bin", litflat::kNotOurs}, {"ps_models_blend_p.bin", litflat::kNotOurs}, {"ps_lava_p.bin", litflat::kNotOurs},
                       {"ps_water_p.bin", litflat::kNotOurs}, {"ps_glass_p.bin", litflat::kNotOurs}};
    for (const F& f : files) {
        std::ifstream in(dir + "\\" + f.file, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int n = 0, right = 0;
        for (size_t i = 0; i + 32 <= d.size();) {
            if (std::memcmp(&d[i], "DXBC", 4) != 0) { ++i; continue; }
            uint32_t t = 0;
            std::memcpy(&t, &d[i + 24], 4);
            if (t < 32 || i + t > d.size()) { ++i; continue; }
            ++n;
            right += litflat::FamilyOf(&d[i], t) == f.want ? 1 : 0;
            i += t;
        }
        std::printf("  the game's %s: %d of %d permutations taken as expected\n", f.file, right, n);
        CHECK(n > 0 && right == n);
    }
}

int main(int argc, char** argv) {
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx))) {
        std::printf("no WARP device\n");
        return 1;
    }
    const std::string light = litflat::LightDefine(0xFF8040, 2.0f);
    CHECK(light == "float3(2.00000, 1.00392, 0.50196)");
    const std::vector<uint8_t> models = Compile(litflat::kModelsShader, sizeof litflat::kModelsShader - 1, "ps_5_0", light.c_str());
    const std::vector<uint8_t> terrain = Compile(litflat::kTerrainShader, sizeof litflat::kTerrainShader - 1, "ps_5_0", light.c_str());
    CHECK(!models.empty() && !terrain.empty());
    // the input layouts: exactly the game's (as read from its shaders), so the copies link in its pipelines
    CHECK(litflat::FamilyOf(models.data(), models.size()) == litflat::kModels);
    CHECK(litflat::FamilyOf(terrain.data(), terrain.size()) == litflat::kTerrain);
    std::vector<litflat::Input> in;
    CHECK(litflat::InputsOf(models.data(), models.size(), &in) && in.size() == 7 && in[6].name == "SV_IsFrontFace" && in[4].reg == 4);
    CHECK(litflat::FamilyOf(nullptr, 0) == litflat::kNotOurs);

    const float colour[4] = {0.5f, 0.25f, 1.0f, 0.6f};
    const float light3[3] = {2.0f, 1.00392f, 0.50196f};
    float o0[4], o1[4];
    const std::vector<uint8_t> mvs = Compile(kModelsVs, sizeof kModelsVs - 1, "vs_5_0", nullptr);
    CHECK(Draw(mvs, models, 10, colour, 0.5f, o0, o1));
    bool ok = true;
    for (int c = 0; c < 3; ++c) ok &= Near(o0[c], colour[c] * light3[c] * 0.5f);
    CHECK(ok && o0[3] == 1.0f);   // the material's colour x the light x the exposure
    CHECK(o1[0] == 0.5f && o1[1] == 0.5f && o1[2] == 1.0f && o1[3] == 1.0f);
    const std::vector<uint8_t> tvs = Compile(kTerrainVs, sizeof kTerrainVs - 1, "vs_5_0", nullptr);
    CHECK(Draw(tvs, terrain, 35, colour, 0.25f, o0, o1));
    ok = true;
    for (int c = 0; c < 3; ++c) ok &= Near(o0[c], colour[c] * light3[c] * 0.25f);
    CHECK(ok && o0[3] == 1.0f);
    CHECK(o1[0] == 0.5f && o1[1] == 0.5f && o1[2] == 1.0f && Near(o1[3], 0.6f));   // the ground's alpha kept, as the game's
    // the Potato light's colours ([render] potato_light_colour): the presets, and the area's colour as a soft tint in 1/32 steps
    CHECK(litflat::PresetColour(0) == 0xFFFFFFu && litflat::PresetColour(1) == 0xFFE0B5u && litflat::PresetColour(2) == 0xD2E1FFu);
    {
        const float grey[3] = {0.2f, 0.2f, 0.2f}, red[3] = {0.4f, 0.1f, 0.0f}, none[3] = {0.0f, 0.0f, 0.0f}, nearRed[3] = {0.4f, 0.1001f, 0.0f};
        CHECK(litflat::AreaTint(grey) == 0xFFFFFFu);        // no hue: white
        CHECK(litflat::AreaTint(red) == 0xFF9F7Fu);         // full red, green 0.5 + 0.125 = 20/32, blue half-way
        CHECK(litflat::AreaTint(none) == 0xFFFFFFu);        // no light at all: white
        CHECK(litflat::AreaTint(nearRed) == litflat::AreaTint(red));   // a drift under a step: the same light (no copies made again)
    }
    {   // a bright light (the slider's top, 5): past 1 eased toward twice the colour - 0.5 x 5 = 2.5 -> 1 + 1.5 / 2.5 = 1.6, 1.25 -> 1.2, 5 -> 1.8
        const std::string bright = litflat::LightDefine(0xFFFFFF, 5.0f);
        const std::vector<uint8_t> mb = Compile(litflat::kModelsShader, sizeof litflat::kModelsShader - 1, "ps_5_0", bright.c_str());
        CHECK(Draw(mvs, mb, 10, colour, 0.5f, o0, o1));
        CHECK(Near(o0[0], 1.6f * 0.5f) && Near(o0[1], 1.2f * 0.5f) && Near(o0[2], 1.8f * 0.5f));
        const std::vector<uint8_t> tb = Compile(litflat::kTerrainShader, sizeof litflat::kTerrainShader - 1, "ps_5_0", bright.c_str());
        CHECK(Draw(tvs, tb, 35, colour, 1.0f, o0, o1));
        CHECK(o0[0] < 2.0f && o0[1] < 2.0f && o0[2] < 2.0f && Near(o0[2], 1.8f));   // never past twice the colour (x the exposure)
    }
    if (argc > 1) GameFolder(argv[1]);

    g_ctx->Release();
    g_dev->Release();
    std::printf("lit_flat_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
