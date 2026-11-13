// dxbc_ao_radius_test - shared/dxbc_ao_radius.h: the game's ASSAO generate pass with a radius for eyes near the ground ([render]
// ao_vr_radius_m).
//   1. A pixel shader of our own with Intel's shape (saturate(dist * cb4[6].y) * 0.8 + 0.2, times cb4[4].z) is found and patched. Its
//      checksum holds before and after. Drawn on D3D11 WARP, the patched one gives (saturate(dist / (1.2 r)) * 0.8 + 0.2) * r for points
//      inside and past the near limit, whatever the game's cb4 holds; the unpatched one the game's numbers.
//   2. A shader without that shape is left alone.
//   dxbc_ao_radius_test [<folder of the game's ps_assao_generate_p permutations, NNN.dxbc>]: each one found and patched (all six), its
//   checksum good, D3D11 takes every patched one.
#include "dxbc_ao_radius.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// Intel's radius, as the game's generate pass computes it: the distance from a read (as the game's from its depth), the result used on
static const char* kAoPs = R"(
cbuffer CB4 : register(b4) { float4 c4[15]; }
Texture2D<float> depth : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    const float dist = depth.Load(int3(0, 0, 0)) * pos.x;
    float r = saturate(dist * c4[6].y) * 0.8 + 0.2;
    r = r * c4[4].z;
    const float disk = 0.85 * r / (dist + 1.0);
    return float4(r, disk, -1.0 / (r * r), 1);
}
)";
static const char* kPlainPs = R"(
cbuffer CB4 : register(b4) { float4 c4[15]; }
float4 main(float4 pos : SV_Position) : SV_Target { return float4(pos.x * c4[4].z, c4[6].y, 0, 1); }
)";

static std::vector<uint8_t> Compile(const char* src) {
    ID3DBlob *b = nullptr, *e = nullptr;
    if (FAILED(D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
        if (e) { std::printf("%s\n", (const char*)e->GetBufferPointer()); e->Release(); }
        return {};
    }
    std::vector<uint8_t> v((const uint8_t*)b->GetBufferPointer(), (const uint8_t*)b->GetBufferPointer() + b->GetBufferSize());
    b->Release();
    return v;
}

// pixel x of an 8 x 1 target drawn with `ps`; the depth read 1 (dist = x + 0.5); cb4[4].z = gameR, cb4[6].y = gameRec
static bool Draw(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::vector<uint8_t>& ps, float gameR, float gameRec, float out[8]) {
    static const char* kVs = "float4 main(uint i : SV_VertexID) : SV_Position { float2 p = float2((i << 1) & 2, i & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }";
    ID3DBlob* vb = nullptr;
    if (FAILED(D3DCompile(kVs, std::strlen(kVs), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vb, nullptr))) return false;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* p = nullptr;
    dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vs);
    vb->Release();
    if (FAILED(dev->CreatePixelShader(ps.data(), ps.size(), nullptr, &p))) { if (vs) vs->Release(); return false; }
    D3D11_TEXTURE2D_DESC td{8, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *rt = nullptr, *st = nullptr, *dt = nullptr;
    dev->CreateTexture2D(&td, nullptr, &rt);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &st);
    const float one = 1.0f;
    D3D11_TEXTURE2D_DESC dd{1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA ds{&one, 4, 4};
    dev->CreateTexture2D(&dd, &ds, &dt);
    ID3D11ShaderResourceView* srv = nullptr;
    dev->CreateShaderResourceView(dt, nullptr, &srv);
    float cb[15 * 4] = {};
    cb[4 * 4 + 2] = gameR;
    cb[6 * 4 + 1] = gameRec;
    D3D11_BUFFER_DESC bd{sizeof cb, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA bs{cb, 0, 0};
    ID3D11Buffer* buf = nullptr;
    dev->CreateBuffer(&bd, &bs, &buf);
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(rt, nullptr, &rtv);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, 8, 1, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(p, nullptr, 0);
    ctx->PSSetConstantBuffers(4, 1, &buf);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->Draw(3, 0);
    ctx->CopyResource(st, rt);
    D3D11_MAPPED_SUBRESOURCE m{};
    const bool ok = SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m));
    if (ok) {
        for (int x = 0; x < 8; ++x) out[x] = ((const float*)m.pData)[x * 4];
        ctx->Unmap(st, 0);
    }
    for (IUnknown* u : {(IUnknown*)vs, (IUnknown*)p, (IUnknown*)rt, (IUnknown*)st, (IUnknown*)dt, (IUnknown*)srv, (IUnknown*)buf, (IUnknown*)rtv})
        if (u) u->Release();
    return ok;
}

int main(int argc, char** argv) {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)));
    const std::vector<uint8_t> ao = Compile(kAoPs), plain = Compile(kPlainPs);
    CHECK(!ao.empty() && !plain.empty());
    CHECK(dxbcfog::ChecksumHolds(ao.data(), ao.size()));
    dxbcao::Found f;
    std::string why;
    CHECK(dxbcao::Find(ao.data(), ao.size(), &f, &why));
    if (!why.empty()) std::printf("  ours not found: %s\n", why.c_str());
    // 1. the game's numbers (radius 40 units, its near limit 1 / 48) against ours (radius 2.5: near limit 3)
    const float gameR = 40.0f, gameRec = 1.0f / 48.0f, r = 2.5f;
    const std::vector<uint8_t> patched = dxbcao::Patch(ao.data(), ao.size(), r, &why);
    CHECK(!patched.empty() && dxbcfog::ChecksumHolds(patched.data(), patched.size()));
    float got0[8] = {}, got[8] = {};
    CHECK(Draw(dev, ctx, ao, gameR, gameRec, got0) && Draw(dev, ctx, patched, gameR, gameRec, got));
    for (int x = 0; x < 8; ++x) {
        const float dist = x + 0.5f;
        const float wantGame = (std::fmin(1.0f, dist * gameRec) * 0.8f + 0.2f) * gameR;
        const float wantOurs = (std::fmin(1.0f, dist / (1.2f * r)) * 0.8f + 0.2f) * r;
        std::printf("  distance %.1f: the game's radius %.4f (want %.4f), ours %.4f (want %.4f)\n", dist, got0[x], wantGame, got[x], wantOurs);
        CHECK(std::fabs(got0[x] - wantGame) < 1e-3f && std::fabs(got[x] - wantOurs) < 1e-3f);
    }
    // ... and nothing of the game's cb4 reaches ours (another game radius, the same result)
    float got2[8] = {};
    CHECK(Draw(dev, ctx, patched, 7.0f, 1.0f / 9.0f, got2));
    for (int x = 0; x < 8; ++x) CHECK(std::fabs(got2[x] - got[x]) < 1e-5f);
    // 2. a shader without Intel's shape: not found, not patched
    CHECK(!dxbcao::Find(plain.data(), plain.size(), &f) && dxbcao::Patch(plain.data(), plain.size(), r).empty());
    // the game's own six, when given
    if (argc > 1) {
        int found = 0, taken = 0;
        for (int i = 0; i < 16; ++i) {
            char path[512];
            std::snprintf(path, sizeof path, "%s\\%03d.dxbc", argv[1], i);
            FILE* fp = std::fopen(path, "rb");
            if (!fp) continue;
            std::vector<uint8_t> v;
            std::fseek(fp, 0, SEEK_END);
            v.resize((size_t)std::ftell(fp));
            std::fseek(fp, 0, SEEK_SET);
            v.resize(std::fread(v.data(), 1, v.size(), fp));
            std::fclose(fp);
            CHECK(dxbcfog::ChecksumHolds(v.data(), v.size()));
            std::string w;
            const std::vector<uint8_t> pv = dxbcao::Patch(v.data(), v.size(), r, &w);
            if (pv.empty()) { std::printf("  %03d: not patched (%s)\n", i, w.c_str()); continue; }
            ++found;
            CHECK(dxbcfog::ChecksumHolds(pv.data(), pv.size()));
            ID3D11PixelShader* ps = nullptr;
            if (SUCCEEDED(dev->CreatePixelShader(pv.data(), pv.size(), nullptr, &ps)) && ps) { ++taken; ps->Release(); }
        }
        std::printf("  the game's generate permutations: %d patched, %d taken by D3D11\n", found, taken);
        CHECK(found == 6 && taken == 6);
    }
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (g_fail) { std::printf("dxbc_ao_radius_test: %d FAILED\n", g_fail); return 1; }
    std::printf("dxbc_ao_radius_test: Intel's radius and near limit found and given ours, the rest the game's: all passed\n");
    return 0;
}
