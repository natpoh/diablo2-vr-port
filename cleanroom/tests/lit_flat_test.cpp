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

#include <algorithm>
#include <cmath>
#include <cstring>
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

static std::vector<uint8_t> Compile(const char* src, size_t len, const char* target, const char* light, const char* game = "0", int variant = 0) {
    const D3D_SHADER_MACRO m[] = {{"LIGHT", light}, {"GAME_LIGHT", game}, {"CLIP", litflat::ClipMacro(variant)}, {"BLEND", litflat::BlendMacro(variant)},
                                  {nullptr, nullptr}};
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
// the same with TEXCOORD1.z 10 (the blended permutations' depth weight: 10^6 / (10^5 - 10^3))
static const char kModelsVsZ[] = R"(
struct V { float4 pos : SV_POSITION; float4 p : POSITION; float3 n : NORMAL; float4 t : TANGENT; float2 uv : TEXCOORD0; float4 w : TEXCOORD1; };
V main(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0.5, 1); o.p = o.pos; o.n = float3(0, 0, 1); o.t = float4(1, 0, 0, 1);
    o.uv = c; o.w = float4(0, 0, 10, 1); return o;
})";
// the same with the normal given (a view-space NORMAL, scaled: the shader normalises it)
static const char kModelsVsN[] = R"(
struct V { float4 pos : SV_POSITION; float4 p : POSITION; float3 n : NORMAL; float4 t : TANGENT; float2 uv : TEXCOORD0; float4 w : TEXCOORD1; };
V main(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0.5, 1); o.p = o.pos; o.n = float3(0.6, 1.6, 0.0); o.t = float4(1, 0, 0, 1);
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
// the game's ambient (b0), key light (b1) and camera (b3) for GAME_LIGHT
struct GameCbs { const float* b0; const float* b1; const float* b3; };
// the cuts' and blending's inputs: the material's b4[0], the draw's b2 (29 rows), the opacity map t14's texel
struct CutIns { const float* b4; const float* b2; const float* t14; };
static bool Draw(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, UINT texReg, const float colour[4], float exposure,
                 float out0[4], float out1[4], const GameCbs* game = nullptr, const CutIns* cut = nullptr) {
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
    ID3D11Buffer* gcb[3] = {};
    ID3D11Buffer* ccb[2] = {};
    ID3D11Texture2D* otex = nullptr;
    ID3D11ShaderResourceView* osrv = nullptr;
    if (cut) {
        const float* src[2] = {cut->b4, cut->b2};
        const UINT n[2] = {1, 29}, slot[2] = {4, 2};
        for (int k = 0; k < 2; ++k) {
            if (!src[k]) continue;
            D3D11_BUFFER_DESC gd{n[k] * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
            D3D11_SUBRESOURCE_DATA gs{src[k], 0, 0};
            g_dev->CreateBuffer(&gd, &gs, &ccb[k]);
            g_ctx->PSSetConstantBuffers(slot[k], 1, &ccb[k]);
        }
        if (cut->t14) {
            D3D11_SUBRESOURCE_DATA os{cut->t14, 16, 16};
            g_dev->CreateTexture2D(&td, &os, &otex);
            g_dev->CreateShaderResourceView(otex, nullptr, &osrv);
            g_ctx->PSSetShaderResources(14, 1, &osrv);
        }
    }
    if (game) {
        const float* src[3] = {game->b0, game->b1, game->b3};
        const UINT n[3] = {10, 10, 13}, slot[3] = {0, 1, 3};
        for (int k = 0; k < 3; ++k) {
            D3D11_BUFFER_DESC gd{n[k] * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
            D3D11_SUBRESOURCE_DATA gs{src[k], 0, 0};
            g_dev->CreateBuffer(&gd, &gs, &gcb[k]);
            g_ctx->PSSetConstantBuffers(slot[k], 1, &gcb[k]);
        }
    }
    g_ctx->Draw(3, 0);
    {
        ID3D11Buffer* none[4] = {};
        g_ctx->PSSetConstantBuffers(0, 4, none);
    }
    for (ID3D11Buffer* b : gcb)
        if (b) b->Release();
    for (ID3D11Buffer* b : ccb)
        if (b) b->Release();
    if (osrv) { ID3D11ShaderResourceView* none = nullptr; g_ctx->PSSetShaderResources(14, 1, &none); osrv->Release(); }
    if (otex) otex->Release();
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

// A decal over the ground: our terrain drawn first (no blending), then a quad of the models' family with `decalPs` through a blended pipeline -
// the first target premultiplied over (ONE, INV_SRC_ALPHA), the second (the revealage) ZERO, INV_SRC_ALPHA - the decal's texel `decal`, its
// b4 / b2. Returns both targets' pixel.
static bool DrawDecalOverGround(const std::vector<uint8_t>& groundVs, const std::vector<uint8_t>& groundPs, const float ground[4],
                                const std::vector<uint8_t>& decalVs, const std::vector<uint8_t>& decalPs, const float decal[4], const float* b4,
                                const float* b2, float out0[4], float out1[4]) {
    ID3D11VertexShader* vs[2] = {};
    ID3D11PixelShader* ps[2] = {};
    if (FAILED(g_dev->CreateVertexShader(groundVs.data(), groundVs.size(), nullptr, &vs[0])) ||
        FAILED(g_dev->CreatePixelShader(groundPs.data(), groundPs.size(), nullptr, &ps[0])) ||
        FAILED(g_dev->CreateVertexShader(decalVs.data(), decalVs.size(), nullptr, &vs[1])) ||
        FAILED(g_dev->CreatePixelShader(decalPs.data(), decalPs.size(), nullptr, &ps[1])))
        return false;
    const UINT W = 8;
    ID3D11Texture2D* rt[2] = {};
    ID3D11RenderTargetView* rtv[2] = {};
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC d{W, W, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
        g_dev->CreateTexture2D(&d, nullptr, &rt[i]);
        g_dev->CreateRenderTargetView(rt[i], nullptr, &rtv[i]);
        const float z[4] = {0, 0, 0, 0};
        g_ctx->ClearRenderTargetView(rtv[i], z);
    }
    ID3D11Texture2D* tex[2] = {};
    ID3D11ShaderResourceView* srv[2] = {};
    const float* texels[2] = {ground, decal};
    for (int i = 0; i < 2; ++i) {
        D3D11_TEXTURE2D_DESC td{1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA ts{texels[i], 16, 16};
        g_dev->CreateTexture2D(&td, &ts, &tex[i]);
        g_dev->CreateShaderResourceView(tex[i], nullptr, &srv[i]);
    }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* smp = nullptr;
    g_dev->CreateSamplerState(&sd, &smp);
    std::vector<float> f(90 * 4, 0.0f);
    f[0] = 1.0f;
    ID3D11Buffer* cb[3] = {};
    const float* src[3] = {f.data(), b4, b2};
    const UINT n[3] = {90, 1, 29}, slot[3] = {10, 4, 2};
    for (int k = 0; k < 3; ++k) {
        D3D11_BUFFER_DESC bd{n[k] * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA bs{src[k], 0, 0};
        g_dev->CreateBuffer(&bd, &bs, &cb[k]);
        g_ctx->PSSetConstantBuffers(slot[k], 1, &cb[k]);
    }
    D3D11_BLEND_DESC bl{};
    bl.IndependentBlendEnable = TRUE;
    bl.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA,
                          D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    bl.RenderTarget[1] = {TRUE, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA,
                          D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    ID3D11BlendState* blend = nullptr;
    g_dev->CreateBlendState(&bl, &blend);
    D3D11_VIEWPORT vp{0, 0, (float)W, (float)W, 0, 1};
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetRenderTargets(2, rtv, nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->PSSetSamplers(0, 1, &smp);
    // the ground: our terrain, no blending
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_ctx->VSSetShader(vs[0], nullptr, 0);
    g_ctx->PSSetShader(ps[0], nullptr, 0);
    g_ctx->PSSetShaderResources(35, 1, &srv[0]);
    g_ctx->Draw(3, 0);
    // the decal over it, blended
    g_ctx->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
    g_ctx->VSSetShader(vs[1], nullptr, 0);
    g_ctx->PSSetShader(ps[1], nullptr, 0);
    g_ctx->PSSetShaderResources(10, 1, &srv[1]);
    g_ctx->Draw(3, 0);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    {
        ID3D11Buffer* none[11] = {};
        g_ctx->PSSetConstantBuffers(0, 11, none);
        ID3D11ShaderResourceView* nsrv[36] = {};
        g_ctx->PSSetShaderResources(0, 36, nsrv);
    }
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
    blend->Release(); smp->Release();
    for (ID3D11Buffer* b : cb) b->Release();
    for (int i = 0; i < 2; ++i) { srv[i]->Release(); tex[i]->Release(); rtv[i]->Release(); rt[i]->Release(); vs[i]->Release(); ps[i]->Release(); }
    return true;
}

// A permutation's cut read from its disassembly (independent of the plugin's token hash): its instructions up to its last discard among the
// first 20, by what they do
static int VariantFromText(const uint8_t* p, size_t n) {
    ID3DBlob* b = nullptr;
    if (FAILED(D3DDisassemble(p, n, 0, nullptr, &b)) || !b) return litflat::kVarUnknown;
    std::string all((const char*)b->GetBufferPointer(), b->GetBufferSize());
    b->Release();
    std::vector<std::string> ins;
    size_t at = 0;
    while (at < all.size()) {
        size_t e = all.find('\n', at);
        if (e == std::string::npos) e = all.size();
        std::string l = all.substr(at, e - at);
        at = e + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\0')) l.pop_back();
        const size_t s = l.find_first_not_of(' ');
        if (s == std::string::npos) continue;
        l = l.substr(s);
        if (l.rfind("//", 0) == 0 || l.rfind("dcl_", 0) == 0 || l.rfind("ps_", 0) == 0 || l[0] == '{') continue;
        ins.push_back(l);
    }
    int last = -1, discards = 0;
    for (int i = 0; i < (int)ins.size(); ++i)
        if (ins[i].rfind("discard", 0) == 0) { ++discards; if (i < 20) last = i; }
    if (!discards) return litflat::kVarPlain;
    if (last < 0) return litflat::kVarUnknown;
    std::string pro;
    for (int i = 0; i <= last; ++i) pro += ins[i] + "\n";
    auto has = [&](const char* x) { return pro.find(x) != std::string::npos; };
    auto hasAll = [&](const char* x) { return all.find(x) != std::string::npos; };
    const bool albedo = has("t10.xyzw, s0, cb10[13].y") && has("add r0.w, r0.w, -cb4[0].x");
    const bool opacity = has("t14.") && has("-cb4[0].y");
    const bool grunge = has("mul r0.w, r0.w, cb4[0].x") && has("lt r1.x, r0.w, l(0.000978)");
    const bool foliage = has("lt r1.x, r0.w, l(0.000978)") && !has("cb4");
    const bool blendTail = hasAll("mov o1.xyz, l(1.000000,1.000000,1.000000,0)") && hasAll("mul r0.w, r0.w, cb2[19].z") &&
                           hasAll("mad r1.x, r1.x, -v5.z, l(100000.000000)") && hasAll("div r1.x, l(1000000.000000), r1.x") &&
                           hasAll("mul o0.xyzw, r0.xyzw, r1.xxxx");
    if (foliage) return discards == 1 && blendTail ? litflat::kVarFoliage : litflat::kVarUnknown;
    if (grunge) return discards == 1 && blendTail ? litflat::kVarGrunge : litflat::kVarUnknown;
    if (blendTail) return litflat::kVarUnknown;
    if (albedo && opacity) return discards == 2 ? litflat::kVarBothRef : litflat::kVarUnknown;
    if (albedo) return discards == 1 ? litflat::kVarAlbedoRef : litflat::kVarUnknown;
    if (opacity) return discards == 1 ? litflat::kVarOpacityRef : litflat::kVarUnknown;
    return litflat::kVarUnknown;
}
// every distinct cut hash seen, with the variant its disassembly says (printed for vr/lit_flat.h's kKnownCuts)
static std::vector<std::pair<uint64_t, int>> g_cuts;
static void NoteCut(uint64_t h, int v) {
    for (auto& c : g_cuts)
        if (c.first == h) { CHECK(c.second == v); return; }   // one hash, one variant
    g_cuts.push_back({h, v});
}

static void GameFolder(const std::string& dir) {
    struct F { const char* file; litflat::Family want; };
    const F files[] = {{"ps_models_p.bin", litflat::kModels}, {"ps_foliage_p.bin", litflat::kModels}, {"ps_grunge_p.bin", litflat::kModels},
                       {"ps_alpha_threshold_p.bin", litflat::kModels}, {"ps_terrain.bin", litflat::kTerrain},
                       {"ps_detail_normal_p.bin", litflat::kNotOurs}, {"ps_models_blend_p.bin", litflat::kNotOurs}, {"ps_lava_p.bin", litflat::kNotOurs},
                       {"ps_water_p.bin", litflat::kNotOurs}, {"ps_glass_p.bin", litflat::kNotOurs}};
    for (const F& f : files) {
        std::ifstream in(dir + "\\" + f.file, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int n = 0, right = 0, varRight = 0, varN = 0;
        for (size_t i = 0; i + 32 <= d.size();) {
            if (std::memcmp(&d[i], "DXBC", 4) != 0) { ++i; continue; }
            uint32_t t = 0;
            std::memcpy(&t, &d[i + 24], 4);
            if (t < 32 || i + t > d.size()) { ++i; continue; }
            ++n;
            right += litflat::FamilyOf(&d[i], t) == f.want ? 1 : 0;
            if (f.want != litflat::kNotOurs) {   // its cut: from the tokens (the plugin's) against its disassembly read here
                ++varN;
                const int want = VariantFromText(&d[i], t);
                litflat::Cut c;
                const bool read = litflat::CutOf(&d[i], t, &c);
                const int got = litflat::VariantOf(&d[i], t);
                if (read && c.any && !c.late) NoteCut(c.hash, want);
                if (got == want && want != litflat::kVarUnknown) ++varRight;
                else std::printf("    %s #%d: variant %s by its tokens (cut %016llX), %s by its disassembly\n", f.file, n - 1, litflat::VariantName(got),
                                 (unsigned long long)c.hash, litflat::VariantName(want));
            }
            i += t;
        }
        std::printf("  the game's %s: %d of %d permutations taken as expected; their cut read right: %d of %d\n", f.file, right, n, varRight, varN);
        CHECK(n > 0 && right == n);
        CHECK(varRight == varN);
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
    const std::string modelsSrc = litflat::Source(litflat::kModels), terrainSrc = litflat::Source(litflat::kTerrain);
    CHECK(litflat::Source(litflat::kNotOurs).empty());
    const std::vector<uint8_t> models = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", light.c_str());
    const std::vector<uint8_t> terrain = Compile(terrainSrc.data(), terrainSrc.size(), "ps_5_0", light.c_str());
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
    {   // [render] potato_light_game: the game's own ambient (b0) and key light (b1) without shadows, the normal turned to the world by b3[9..11]
        const std::vector<uint8_t> mg = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", "float3(1.5, 1.5, 1.5)", "1");
        const std::vector<uint8_t> tg = Compile(terrainSrc.data(), terrainSrc.size(), "ps_5_0", "float3(1.0, 1.0, 1.0)", "1");
        CHECK(!mg.empty() && !tg.empty());
        CHECK(litflat::FamilyOf(mg.data(), mg.size()) == litflat::kModels);    // the layouts still the game's
        CHECK(litflat::FamilyOf(tg.data(), tg.size()) == litflat::kTerrain);
        float b0[40] = {}, b1[40] = {}, b3[52] = {};
        b0[0] = 0.4f; b0[1] = 0.0f; b0[2] = 1.0f; b0[3] = 1.0f;                       // iblIntensity 0.4, no reach mask, diffuse / spec 1
        const float sh[9][3] = {{0.9f, 1.0f, 1.2f}, {0.1f, 0.2f, 0.3f}, {0.05f, 0.0f, -0.05f}, {0.2f, 0.1f, 0.0f}, {0.01f, 0.02f, 0.03f},
                                {0.03f, 0.02f, 0.01f}, {-0.02f, 0.0f, 0.02f}, {0.04f, 0.04f, 0.0f}, {0.0f, -0.03f, 0.05f}};
        for (int k = 0; k < 9; ++k)
            for (int c = 0; c < 3; ++c) b0[4 * (k + 1) + c] = sh[k][c];
        uint32_t kind = 1;
        std::memcpy(&b1[0], &kind, 4);                                               // a directional key light
        b1[4] = 1.0f; b1[5] = 0.94f; b1[6] = 0.87f;                                   // the day's colour (act 1)
        b1[8] = 1.0f; b1[9] = 1.0f; b1[10] = 13.5f;                                   // diffuse, specular contributions, power
        b1[12] = 0.0f; b1[13] = -0.8f; b1[14] = -0.6f;                                // its direction (view space)
        // the camera's view -> world rows 9..11: x -> x, y -> z, z -> -y (a turned head)
        b3[36] = 1; b3[40 + 2] = 1; b3[44 + 1] = -1;
        auto expect = [&](const float nv[3], const float alb[3], float level, float exposure, float out[3]) {
            const float l = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
            const float v[3] = {nv[0] / l, nv[1] / l, nv[2] / l};
            const float n[3] = {v[0], -v[2], v[1]};                                   // row9 * x + row10 * y + row11 * z
            const float x = n[0], y = n[1], z = n[2];
            const float basis[9] = {1, y, z, x, x * y, z * y, 3 * z * z - 1, x * z, x * x - y * y};
            uint32_t count = 0;
            std::memcpy(&count, &b1[0], 4);
            count = std::min(count, 3u);
            for (int c = 0; c < 3; ++c) {
                float s = 0;
                for (int k = 0; k < 9; ++k) s += sh[k][c] * basis[k];
                const float amb = std::max(s, 0.0f) * b0[2] * b0[0];
                float direct = 0.0f;
                for (uint32_t i = 0; i < count; ++i) {   // light i: colour [1 + 3i], (diffuse, spec, power, shadow) [2 + 3i], direction [3 + 3i]
                    const float* L = b1 + 4 * (1 + 3 * i);
                    const float nl = std::max(0.0f, std::min(1.0f, -(v[0] * L[8] + v[1] * L[9] + v[2] * L[10])));
                    direct += L[c] * L[6] * L[4] * nl;
                }
                out[c] = alb[c] * (amb + direct) * 0.31830989f * level * exposure;
            }
        };
        auto same = [](const float a[4], const float b[3]) {
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok &= std::fabs(a[c] - b[c]) < 1e-4f * std::max(1.0f, b[c]);
            return ok;
        };
        const GameCbs g{b0, b1, b3};
        const std::vector<uint8_t> mvsn = Compile(kModelsVsN, sizeof kModelsVsN - 1, "vs_5_0", nullptr);
        float want[3];
        CHECK(Draw(mvsn, mg, 10, colour, 0.5f, o0, o1, &g));
        const float nm[3] = {0.6f, 1.6f, 0.0f};
        expect(nm, colour, 1.5f, 0.5f, want);
        std::printf("  game light, models: %.5f %.5f %.5f (expected %.5f %.5f %.5f)\n", o0[0], o0[1], o0[2], want[0], want[1], want[2]);
        CHECK(same(o0, want) && o0[3] == 1.0f && o1[2] == 1.0f);
        const float nt[3] = {0.0f, 1.0f, 0.0f};                                        // the terrain VS's NORMAL_VS
        CHECK(Draw(tvs, tg, 35, colour, 0.25f, o0, o1, &g));
        expect(nt, colour, 1.0f, 0.25f, want);
        std::printf("  game light, terrain (day): %.5f %.5f %.5f (expected %.5f %.5f %.5f)\n", o0[0], o0[1], o0[2], want[0], want[1], want[2]);
        CHECK(same(o0, want) && Near(o1[3], 0.6f));
        // night from the game's numbers alone (act 1 night: the moon (0.42, 0.45, 1) x 4.5, ibl 0.1): darker and bluer than the day
        const float dayG = o0[1], dayB = o0[2];
        b0[0] = 0.1f; b1[4] = 0.42f; b1[5] = 0.45f; b1[6] = 1.0f; b1[10] = 4.5f;
        CHECK(Draw(tvs, tg, 35, colour, 0.25f, o0, o1, &g));
        expect(nt, colour, 1.0f, 0.25f, want);
        std::printf("  game light, terrain (night): %.5f %.5f %.5f\n", o0[0], o0[1], o0[2]);
        CHECK(same(o0, want) && o0[1] < dayG && o0[2] / o0[1] > dayB / dayG);
        // b1[0].x is the game's COUNT of lights (ieq 1, ieq 2, else a loop): two lights, the second (a fill from the side) added
        const float oneLight = o0[0];
        kind = 2;
        std::memcpy(&b1[0], &kind, 4);
        b1[16] = 0.9f; b1[17] = 0.92f; b1[18] = 1.0f;                                 // light 1's colour
        b1[20] = 1.0f; b1[21] = 1.0f; b1[22] = 4.0f;                                  // diffuse, specular, power 4
        b1[24] = 0.0f; b1[25] = -1.0f; b1[26] = 0.0f;                                 // its direction (view space)
        CHECK(Draw(tvs, tg, 35, colour, 0.25f, o0, o1, &g));
        expect(nt, colour, 1.0f, 0.25f, want);
        std::printf("  game light, two lights: %.5f %.5f %.5f (expected %.5f %.5f %.5f; one light %.5f)\n", o0[0], o0[1], o0[2], want[0], want[1],
                    want[2], oneLight);
        CHECK(same(o0, want) && o0[0] > oneLight + 1e-3f);
        b1[22] = 0.0f;                                                                // (light 1 off again for the cases below)
        // the light facing away: the ambient alone too
        kind = 1;
        std::memcpy(&b1[0], &kind, 4);
        b1[13] = 0.8f;
        CHECK(Draw(tvs, tg, 35, colour, 0.25f, o0, o1, &g));
        expect(nt, colour, 1.0f, 0.25f, want);
        CHECK(same(o0, want));
        // a character at night (Serg 0.209.21: "персонажи очень темные"): the vis file's player lights - key 30, fill 4, back 0 - three of them,
        // the ambient at night (ibl 0.1). Lit by all three: far above the ambient alone (what ours gave before: a count of 3 lit nothing)
        {
            float c1[40] = {};
            const uint32_t three = 3;
            std::memcpy(&c1[0], &three, 4);
            const float lights[3][3][4] = {{{1.0f, 0.94f, 0.88f, 0}, {1, 1, 30.0f, 0}, {-0.6f, -0.8f, 0.0f, 0}},
                                           {{0.9f, 0.92f, 1.0f, 0}, {1, 1, 4.0f, 0}, {0.82f, 0.0f, -0.57f, 0}},
                                           {{1.0f, 1.0f, 1.0f, 0}, {1, 1, 2.0f, 0}, {0.0f, -0.6f, -0.8f, 0}}};   // (the back light given 2 here: all three summed)
            for (int i = 0; i < 3; ++i)
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) c1[4 * (1 + 3 * i + r) + c] = lights[i][r][c];
            const float savedIbl = b0[0];
            b0[0] = 0.1f;
            const GameCbs gc{b0, c1, b3};
            CHECK(Draw(mvsn, mg, 10, colour, 0.5f, o0, o1, &gc));
            float b1save[40];
            std::memcpy(b1save, b1, sizeof b1);
            std::memcpy(b1, c1, sizeof c1);
            expect(nm, colour, 1.5f, 0.5f, want);
            float ambientOnly[3];
            const uint32_t none = 0;
            std::memcpy(&b1[0], &none, 4);
            expect(nm, colour, 1.5f, 0.5f, ambientOnly);
            std::memcpy(b1, b1save, sizeof b1);
            b0[0] = savedIbl;
            std::printf("  game light, a character at night (3 lights): %.5f %.5f %.5f (expected %.5f %.5f %.5f; the ambient alone %.5f)\n", o0[0], o0[1],
                        o0[2], want[0], want[1], want[2], ambientOnly[0]);
            CHECK(same(o0, want) && o0[0] > 5.0f * ambientOnly[0]);
        }
        // the characters' curve (b2[19].w set): the game's HSV lift, against its instruction sequence done here on the CPU
        {
            auto curve = [](const float c[3], const float* d2, float out[3]) {   // ps_models_p's tail, line by line
                const float mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2])), delta = mx - mn;
                float h = 0, sat = 0;
                if (delta != 0) {
                    const float dG = ((mx - c[1]) / 6.0f + delta * 0.5f) / delta, dB = ((mx - c[2]) / 6.0f + delta * 0.5f) / delta,
                                dR = ((mx - c[0]) / 6.0f + delta * 0.5f) / delta;
                    float hb = mx == c[2] ? dG + 2.0f / 3.0f - dR : 0.0f;
                    float hh = mx == c[1] ? dR + 1.0f / 3.0f - dB : hb;
                    h = mx == c[0] ? dB - dG : hh;
                    if (h < 0) h += 1;
                    if (h > 1) h -= 1;
                    sat = delta / mx;
                }
                const float v = std::min(std::max(mx, 0.0f), 1.0f);
                const float poly = d2[18 * 4] + d2[18 * 4 + 1] * v + d2[18 * 4 + 2] * v * v + d2[18 * 4 + 3] * v * v * v;
                const float l = poly * d2[19 * 4 + 1] + mx, V = l * d2[19 * 4] + l;
                const float sector = std::floor(h * 6), f = h * 6 - sector;
                const float pp = (1 - sat) * V, q = (1 - sat * f) * V, t = (1 - sat * (1 - f)) * V;
                float r[3];
                if (sector == 0) { r[0] = V; r[1] = t; r[2] = pp; }
                else if (sector == 1) { r[0] = q; r[1] = V; r[2] = pp; }
                else if (sector == 2) { r[0] = pp; r[1] = V; r[2] = t; }
                else if (sector == 3) { r[0] = pp; r[1] = q; r[2] = V; }
                else if (sector == 4) { r[0] = t; r[1] = pp; r[2] = V; }
                else { r[0] = V; r[1] = pp; r[2] = q; }
                for (int k = 0; k < 3; ++k) out[k] = sat != 0 ? r[k] : V;
            };
            float d2[29 * 4] = {};
            d2[18 * 4 + 0] = 0.05f; d2[18 * 4 + 1] = 0.9f; d2[18 * 4 + 2] = -0.6f; d2[18 * 4 + 3] = 0.2f;   // a contrast curve
            d2[19 * 4 + 0] = 0.3f; d2[19 * 4 + 1] = 0.8f;                                                 // brightness 0.3, curve weight 0.8
            const uint32_t on = 1;
            std::memcpy(&d2[19 * 4 + 3], &on, 4);
            const std::vector<uint8_t> mc = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", "float3(1.0, 1.0, 1.0)", "1");
            const GameCbs gd{b0, b1, b3};
            const CutIns ci{nullptr, d2, nullptr};
            float plain0[4], plain1[4];
            float ref[3];
            // a colour in each of the six hue sectors (red, yellow, green, cyan, blue, magenta max / mid), under an even white light
            float e0[40] = {}, e1[40] = {};
            e0[0] = 1.0f; e0[2] = 1.0f; e0[4] = 3.14159265f; e0[5] = 3.14159265f; e0[6] = 3.14159265f;   // ambient 1 after / pi, no lights
            const GameCbs ge{e0, e1, b3};
            const float hues[6][4] = {{0.6f, 0.3f, 0.1f, 1}, {0.4f, 0.6f, 0.1f, 1}, {0.1f, 0.6f, 0.3f, 1},
                                      {0.1f, 0.4f, 0.6f, 1}, {0.3f, 0.1f, 0.6f, 1}, {0.6f, 0.1f, 0.4f, 1}};
            int curveOk = 0;
            for (const float* hc : hues) {
                const uint32_t off0 = 0;
                std::memcpy(&d2[19 * 4 + 3], &off0, 4);
                CHECK(Draw(mvsn, mc, 10, hc, 1.0f, plain0, plain1, &ge, &ci));               // b2[19].w 0: the curve off
                std::memcpy(&d2[19 * 4 + 3], &on, 4);
                CHECK(Draw(mvsn, mc, 10, hc, 1.0f, o0, o1, &ge, &ci));
                curve(plain0, d2, ref);
                const bool hit = std::fabs(o0[0] - ref[0]) < 2e-4f && std::fabs(o0[1] - ref[1]) < 2e-4f && std::fabs(o0[2] - ref[2]) < 2e-4f &&
                                std::max(o0[0], std::max(o0[1], o0[2])) > std::max(plain0[0], std::max(plain0[1], plain0[2]));   // lifted
                curveOk += hit ? 1 : 0;
                if (!hit)
                    std::printf("    curve: %.5f %.5f %.5f -> %.5f %.5f %.5f, the game's sequence %.5f %.5f %.5f\n", plain0[0], plain0[1], plain0[2], o0[0],
                                o0[1], o0[2], ref[0], ref[1], ref[2]);
            }
            std::printf("  the characters' curve against the game's sequence: %d of 6 hue sectors\n", curveOk);
            CHECK(curveOk == 6);
            CHECK(Draw(mvsn, mc, 10, colour, 0.5f, plain0, plain1, &gd));                   // b2 not bound: zeros, the curve off
            const uint32_t off = 0;
            std::memcpy(&d2[19 * 4 + 3], &off, 4);
            CHECK(Draw(mvsn, mc, 10, colour, 0.5f, o0, o1, &gd, &ci));
            CHECK(Near(o0[0], plain0[0]) && Near(o0[1], plain0[1]) && Near(o0[2], plain0[2]));   // b2[19].w 0: not a character, untouched
            // a grey (no hue): V alone
            const float grey[3] = {0.4f, 0.4f, 0.4f};
            std::memcpy(&d2[19 * 4 + 3], &on, 4);
            curve(grey, d2, ref);
            CHECK(Near(ref[0], ref[1]) && Near(ref[1], ref[2]));
        }
    }
    {   // the game's cuts and blending (Variant): drawn with the cut's inputs; a cut texel leaves the cleared 9 in both targets
        const float pass[4] = {0.5f, 0.25f, 1.0f, 0.6f};
        const float b4lo[4] = {0.5f, 0.2f, 0, 0}, b4hi[4] = {0.7f, 0.9f, 0, 0};   // the material's alpha / opacity references
        float b2[29 * 4] = {};
        b2[19 * 4 + 2] = 0.5f;                                                    // b2[19].z: the object's fade
        const float opLo[4] = {0.1f, 0, 0, 1}, opHi[4] = {0.95f, 0, 0, 1};
        const std::string lightW = litflat::LightDefine(0xFFFFFF, 1.0f);
        auto draw = [&](int v, const float* b4, const float* t14, const char* vsSrc, size_t vsLen, const float* tex, float a0[4], float a1[4]) {
            const std::vector<uint8_t> ps = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", lightW.c_str(), "0", v);
            const std::vector<uint8_t> vs = Compile(vsSrc, vsLen, "vs_5_0", nullptr);
            CHECK(!ps.empty() && litflat::FamilyOf(ps.data(), ps.size()) == litflat::kModels);   // the layout still the game's
            const CutIns ci{b4, b2, t14};
            return !ps.empty() && Draw(vs, ps, 10, tex, 1.0f, a0, a1, nullptr, &ci);
        };
        auto cut = [](const float a[4], const float b[4]) { return a[0] == 9.0f && a[3] == 9.0f && b[0] == 9.0f && b[3] == 9.0f; };
        auto lit = [&](const float a[4], const float b[4]) {
            return Near(a[0], 0.5f) && Near(a[1], 0.25f) && Near(a[2], 1.0f) && a[3] == 1.0f && b[0] == 0.5f && b[3] == 1.0f;
        };
        const size_t vl = sizeof kModelsVs - 1;
        // the albedo's cut: alpha 0.6 against 0.7 cut, against 0.5 drawn
        CHECK(draw(litflat::kVarAlbedoRef, b4hi, nullptr, kModelsVs, vl, pass, o0, o1) && cut(o0, o1));
        CHECK(draw(litflat::kVarAlbedoRef, b4lo, nullptr, kModelsVs, vl, pass, o0, o1) && lit(o0, o1));
        // the opacity map's: t14.r 0.1 against b4[0].y 0.2 cut, 0.95 against 0.9 drawn (the albedo's alpha not asked: 0.6 < 0.7 would cut)
        CHECK(draw(litflat::kVarOpacityRef, b4lo, opLo, kModelsVs, vl, pass, o0, o1) && cut(o0, o1));
        CHECK(draw(litflat::kVarOpacityRef, b4hi, opHi, kModelsVs, vl, pass, o0, o1) && lit(o0, o1));
        // both: either cuts
        CHECK(draw(litflat::kVarBothRef, b4lo, opLo, kModelsVs, vl, pass, o0, o1) && cut(o0, o1));
        CHECK(draw(litflat::kVarBothRef, b4hi, opHi, kModelsVs, vl, pass, o0, o1) && cut(o0, o1));
        const float b4mid[4] = {0.5f, 0.9f, 0, 0};
        CHECK(draw(litflat::kVarBothRef, b4mid, opHi, kModelsVs, vl, pass, o0, o1) && lit(o0, o1));
        // each against its own reference: t14.r 0.6 under b4[0].y 0.9 cut although over b4[0].x 0.5; the albedo's 0.6 over .x 0.5 drawn
        const float opMid[4] = {0.6f, 0, 0, 1};
        CHECK(draw(litflat::kVarOpacityRef, b4mid, opMid, kModelsVs, vl, pass, o0, o1) && cut(o0, o1));
        CHECK(draw(litflat::kVarAlbedoRef, b4mid, nullptr, kModelsVs, vl, pass, o0, o1) && lit(o0, o1));
        // the plain one cuts nothing (the alpha 0.6 under b4's 0.7 drawn)
        CHECK(draw(litflat::kVarPlain, b4hi, nullptr, kModelsVs, vl, pass, o0, o1) && lit(o0, o1));
        // the foliage's weighted transparency: alpha 0.6 x the fade 0.5 = 0.3, weight 10^6 / (10^5 - 10^3) at TEXCOORD1.z 10
        const float w = 1000000.0f / (100000.0f - 1000.0f);
        auto blended = [&](const float a[4], const float b[4], float alpha) {
            return Near(a[0] / w, 0.5f * alpha) && Near(a[1] / w, 0.25f * alpha) && Near(a[2] / w, 1.0f * alpha) && Near(a[3] / w, alpha) &&
                   b[0] == 1.0f && b[1] == 1.0f && b[2] == 1.0f && Near(b[3], alpha);
        };
        const size_t vz = sizeof kModelsVsZ - 1;
        CHECK(draw(litflat::kVarFoliage, b4hi, nullptr, kModelsVsZ, vz, pass, o0, o1) && blended(o0, o1, 0.3f));
        std::printf("  foliage (blended): %.4f %.4f %.4f %.4f / %.4f (weight %.4f)\n", o0[0], o0[1], o0[2], o0[3], o1[3], w);
        // the grunge's: alpha 0.6 x b4[0].x 0.5 x the fade 0.5 = 0.15
        CHECK(draw(litflat::kVarGrunge, b4lo, nullptr, kModelsVsZ, vz, pass, o0, o1) && blended(o0, o1, 0.15f));
        // a fully transparent texel (the atlas's empty blue, alpha 0) cut in both blended ones
        const float empty[4] = {0.2f, 0.3f, 1.0f, 0.0f};
        CHECK(draw(litflat::kVarFoliage, b4hi, nullptr, kModelsVsZ, vz, empty, o0, o1) && cut(o0, o1));
        CHECK(draw(litflat::kVarGrunge, b4hi, nullptr, kModelsVsZ, vz, empty, o0, o1) && cut(o0, o1));
        // the same with the game's own light (GAME_LIGHT 1): the cut and the blending unchanged
        {
            const std::vector<uint8_t> ps = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", lightW.c_str(), "1", litflat::kVarFoliage);
            const std::vector<uint8_t> vs = Compile(kModelsVsZ, vz, "vs_5_0", nullptr);
            CHECK(!ps.empty());
            float b0[40] = {}, b1[40] = {}, b3[52] = {};
            b0[0] = 1.0f; b0[2] = 1.0f; b0[4] = 3.14159265f; b0[5] = 3.14159265f; b0[6] = 3.14159265f;   // an even ambient of 1 after / pi
            b3[36] = 1; b3[41] = 1; b3[46] = 1;
            const GameCbs g{b0, b1, b3};
            const CutIns ci{b4hi, b2, nullptr};
            CHECK(Draw(vs, ps, 10, empty, 1.0f, o0, o1, &g, &ci) && cut(o0, o1));
            CHECK(Draw(vs, ps, 10, pass, 1.0f, o0, o1, &g, &ci) && blended(o0, o1, 0.3f));
        }
    }
    {   // which lit pipelines take ours (Accept): an opaque copy only where nothing blends; the weighted transparency where the first target blends
        using namespace litflat;
        CHECK(Accept(kTerrain, kVarPlain, false, false));
        CHECK(!Accept(kTerrain, kVarPlain, true, false));          // a ground layer laid over by its alpha: the game's
        CHECK(!Accept(kTerrain, kVarPlain, false, true));
        CHECK(Accept(kModels, kVarPlain, false, false));
        CHECK(!Accept(kModels, kVarPlain, true, false));           // a decal of the models' family, blended: the game's (the road's lilac blocks)
        CHECK(!Accept(kModels, kVarAlbedoRef, true, false));
        CHECK(!Accept(kModels, kVarOpacityRef, false, true));      // another target blends / a logic op: the game's
        CHECK(Accept(kModels, kVarBothRef, false, false));
        CHECK(Accept(kModels, kVarFoliage, true, true));           // the weighted transparency: accumulation + revealage both blend
        CHECK(Accept(kModels, kVarGrunge, true, false));
        CHECK(!Accept(kModels, kVarFoliage, false, false));        // a blended permutation in an opaque pipeline: the game's
        CHECK(!Accept(kModels, kVarUnknown, false, false));
    }
    {   // a decal over the ground on WARP: our terrain, then our grunge copy blended over it (premultiplied over, revealage ZERO / INV_SRC_ALPHA).
        // TEXCOORD1.z = cbrt(-900000): the depth weight 10^6 / (10^5 + 9 x 10^5) = 1, so the over-blend reads straight.
        static const char kDecalVs[] = R"(
struct V { float4 pos : SV_POSITION; float4 p : POSITION; float3 n : NORMAL; float4 t : TANGENT; float2 uv : TEXCOORD0; float4 w : TEXCOORD1; };
V main(uint id : SV_VertexID) {
    V o; float2 c = float2((id << 1) & 2, id & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0.5, 1); o.p = o.pos; o.n = float3(0, 0, 1); o.t = float4(1, 0, 0, 1);
    o.uv = c; o.w = float4(0, 0, -96.548938, 1); return o;
})";
        const std::string lw = litflat::LightDefine(0xFFFFFF, 1.0f);
        const std::vector<uint8_t> gvs = Compile(kTerrainVs, sizeof kTerrainVs - 1, "vs_5_0", nullptr);
        const std::vector<uint8_t> gps = Compile(terrainSrc.data(), terrainSrc.size(), "ps_5_0", lw.c_str());
        const std::vector<uint8_t> dvs = Compile(kDecalVs, sizeof kDecalVs - 1, "vs_5_0", nullptr);
        const std::vector<uint8_t> dps = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", lw.c_str(), "0", litflat::kVarGrunge);
        const float ground[4] = {0.2f, 0.5f, 0.1f, 1.0f};                     // green ground, the composite's alpha 1
        const float b4[4] = {1.0f, 0, 0, 0};                                  // the grunge's b4[0].x (alpha scale) 1
        float b2[29 * 4] = {};
        b2[19 * 4 + 2] = 1.0f;                                                // fade 1
        float a0[4], a1[4];
        // an empty texel of the decal's atlas (lilac, alpha 0): the ground stays exactly as it was, the revealage untouched
        const float lilac[4] = {0.45f, 0.35f, 0.8f, 0.0f};
        CHECK(DrawDecalOverGround(gvs, gps, ground, dvs, dps, lilac, b4, b2, a0, a1));
        std::printf("  decal over the ground, empty texel: %.4f %.4f %.4f %.4f / revealage %.4f\n", a0[0], a0[1], a0[2], a0[3], a1[3]);
        CHECK(Near(a0[0], 0.2f) && Near(a0[1], 0.5f) && Near(a0[2], 0.1f) && Near(a0[3], 1.0f) && Near(a1[3], 1.0f));
        // a half-covered texel (dark road, alpha 0.5): blended by its alpha, half road, half ground
        const float road[4] = {0.3f, 0.2f, 0.1f, 0.5f};
        CHECK(DrawDecalOverGround(gvs, gps, ground, dvs, dps, road, b4, b2, a0, a1));
        std::printf("  decal over the ground, half texel: %.4f %.4f %.4f %.4f / revealage %.4f\n", a0[0], a0[1], a0[2], a0[3], a1[3]);
        CHECK(Near(a0[0], 0.5f * 0.3f + 0.5f * 0.2f) && Near(a0[1], 0.5f * 0.2f + 0.5f * 0.5f) && Near(a0[2], 0.5f * 0.1f + 0.5f * 0.1f) &&
              Near(a1[3], 0.5f));
        // the old shader's way (no cut, alpha 1, our plain copy in the blended pipeline - what Accept now refuses): the lilac block
        const std::vector<uint8_t> plain = Compile(modelsSrc.data(), modelsSrc.size(), "ps_5_0", lw.c_str(), "0", litflat::kVarPlain);
        CHECK(DrawDecalOverGround(gvs, gps, ground, dvs, plain, lilac, b4, b2, a0, a1));
        std::printf("  the old way (plain copy in the blended pipeline), empty texel: %.4f %.4f %.4f - the lilac block\n", a0[0], a0[1], a0[2]);
        CHECK(Near(a0[0], 0.45f) && Near(a0[2], 0.8f));
    }
    if (argc > 1) {
        GameFolder(argv[1]);
        std::printf("  the game's cuts (vr/lit_flat.h kKnownCuts):\n");
        for (auto& c : g_cuts) std::printf("    {0x%016llXull, %d},   // %s\n", (unsigned long long)c.first, c.second, litflat::VariantName(c.second));
    }

    g_ctx->Release();
    g_dev->Release();
    std::printf("lit_flat_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
