// bench_lights_test - [render] bench_skip_point_lights (vr/bench_lights.h): our two replacement compute shaders, compiled and run on WARP
// (D3D11's software device) with the game's bindings: the light bins' copy clears both counts of every tile and touches nothing else; the
// light volumes' copy writes the game's own "no light" values inside the volume and leaves the cells past it alone.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "bench_lights.h"

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;

static ID3D11ComputeShader* Compile(const char* src, size_t len) {
    ID3DBlob* b = nullptr;
    ID3DBlob* e = nullptr;
    if (FAILED(D3DCompile(src, len, "bench", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
        std::printf("compile: %s\n", e ? (const char*)e->GetBufferPointer() : "?");
        return nullptr;
    }
    ID3D11ComputeShader* cs = nullptr;
    g_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs);
    b->Release();
    return cs;
}
static ID3D11Buffer* Constants(const void* data, UINT bytes) {
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = (bytes + 15) / 16 * 16;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    std::vector<uint8_t> init(d.ByteWidth, 0);
    std::memcpy(init.data(), data, bytes);
    D3D11_SUBRESOURCE_DATA s{init.data(), 0, 0};
    ID3D11Buffer* b = nullptr;
    g_dev->CreateBuffer(&d, &s, &b);
    return b;
}
template <class T> static std::vector<T> ReadBack(ID3D11Resource* r, UINT bytes, bool tex3d, UINT w = 0, UINT h = 0, UINT dd = 0) {
    ID3D11Resource* st = nullptr;
    if (tex3d) {
        D3D11_TEXTURE3D_DESC d{w, h, dd, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
        g_dev->CreateTexture3D(&d, nullptr, (ID3D11Texture3D**)&st);
    } else {
        D3D11_BUFFER_DESC d{bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
        g_dev->CreateBuffer(&d, nullptr, (ID3D11Buffer**)&st);
    }
    g_ctx->CopyResource(st, r);
    D3D11_MAPPED_SUBRESOURCE m{};
    g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
    std::vector<T> out;
    if (tex3d) {
        for (UINT z = 0; z < dd; ++z)
            for (UINT y = 0; y < h; ++y) {
                const T* row = (const T*)((const uint8_t*)m.pData + z * m.DepthPitch + y * m.RowPitch);
                out.insert(out.end(), row, row + w * 4);
            }
    } else {
        out.assign((const T*)m.pData, (const T*)m.pData + bytes / sizeof(T));
    }
    g_ctx->Unmap(st, 0);
    st->Release();
    return out;
}

static void TestBins() {
    ID3D11ComputeShader* cs = Compile(benchlights::kBinShader, sizeof benchlights::kBinShader - 1);
    CHECK(cs != nullptr);
    if (!cs) return;
    // 4 x 2 tiles, a row of 4: one 8 x 4 group writes tiles 0..19 and the second counts at 8 + tile, 8..27; 28..31 are not the shader's
    const int b12[16] = {4, 2, 0, 0, 0, 4, 0, 0, 5, 6, 7, 0, 9, 0, 0, 0};
    ID3D11Buffer* cb = Constants(b12, sizeof b12);
    std::vector<uint32_t> init(32, 7u);
    D3D11_BUFFER_DESC d{32 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4};
    D3D11_SUBRESOURCE_DATA s{init.data(), 0, 0};
    ID3D11Buffer* buf = nullptr;
    g_dev->CreateBuffer(&d, &s, &buf);
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = DXGI_FORMAT_UNKNOWN;
    u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = 32;
    ID3D11UnorderedAccessView* uav = nullptr;
    g_dev->CreateUnorderedAccessView(buf, &u, &uav);
    g_ctx->CSSetShader(cs, nullptr, 0);
    g_ctx->CSSetConstantBuffers(12, 1, &cb);
    g_ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    g_ctx->Dispatch(1, 1, 1);
    const std::vector<uint32_t> r = ReadBack<uint32_t>(buf, 32 * 4, false);
    bool cleared = true, kept = true;
    for (int i = 0; i < 28; ++i) cleared &= r[i] == 0u;
    for (int i = 28; i < 32; ++i) kept &= r[i] == 7u;
    CHECK(cleared);   // every tile's two counts 0: no local light in any tile
    CHECK(kept);      // nothing past them
    uav->Release(); buf->Release(); cb->Release(); cs->Release();
}

static void TestGrid(uint32_t attenSwitch) {
    ID3D11ComputeShader* cs = Compile(benchlights::kGridShader, sizeof benchlights::kGridShader - 1);
    CHECK(cs != nullptr);
    if (!cs) return;
    std::vector<float> b10(89 * 4, 0.0f);
    b10[88 * 4 + 1] = 0.75f;   // the attenuation's base
    b10[88 * 4 + 2] = 2.0f;
    ID3D11Buffer* c10 = Constants(b10.data(), (UINT)(b10.size() * 4));
    uint32_t b2[16] = {};
    b2[4] = 6; b2[5] = 2; b2[6] = 8;   // [1].xyz the volume: 6 x 2 x 8 (the textures are 8 wide: x 6, 7 are past it)
    b2[7] = attenSwitch;               // [1].w
    ID3D11Buffer* c2 = Constants(b2, sizeof b2);
    const UINT W = 8, H = 2, D = 8;
    ID3D11Texture3D* tex[4] = {};
    ID3D11UnorderedAccessView* uav[4] = {};
    std::vector<float> fill(W * H * D * 4, 5.0f);
    for (int i = 0; i < 4; ++i) {
        D3D11_TEXTURE3D_DESC d{W, H, D, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0};
        D3D11_SUBRESOURCE_DATA s{fill.data(), W * 16, W * H * 16};
        g_dev->CreateTexture3D(&d, &s, &tex[i]);
        g_dev->CreateUnorderedAccessView(tex[i], nullptr, &uav[i]);
    }
    g_ctx->CSSetShader(cs, nullptr, 0);
    ID3D11Buffer* cbs[1] = {c2};
    g_ctx->CSSetConstantBuffers(2, 1, cbs);
    g_ctx->CSSetConstantBuffers(10, 1, &c10);
    g_ctx->CSSetUnorderedAccessViews(4, 4, uav, nullptr);
    g_ctx->Dispatch(1, 2, 1);
    ID3D11UnorderedAccessView* none[4] = {};
    g_ctx->CSSetUnorderedAccessViews(4, 4, none, nullptr);
    std::vector<float> v[4];
    for (int i = 0; i < 4; ++i) v[i] = ReadBack<float>(tex[i], 0, true, W, H, D);
    bool inside = true, past = true;
    const float w4 = attenSwitch ? 0.0f : 0.75f;
    for (UINT z = 0; z < D; ++z)
        for (UINT y = 0; y < H; ++y)
            for (UINT x = 0; x < W; ++x) {
                const size_t at = ((z * H + y) * W + x) * 4;
                if (x < 6) {
                    // u4, u6: the game's default direction with its attenuation term; u5, u7: no light's colour
                    inside &= std::fabs(v[0][at + 1] + 0.01f) < 1e-6f && std::fabs(v[0][at + 2] + 0.006f) < 1e-6f && v[0][at + 3] == w4;
                    inside &= std::fabs(v[2][at + 1] + 0.01f) < 1e-6f && std::fabs(v[2][at + 2] + 0.006f) < 1e-6f && v[2][at + 3] == 0.75f;
                    for (int c = 0; c < 4; ++c) inside &= v[1][at + c] == 0.0f && v[3][at + c] == 0.0f && v[0][at] == 0.0f && v[2][at] == 0.0f;
                } else {
                    for (int i = 0; i < 4; ++i)
                        for (int c = 0; c < 4; ++c) past &= v[i][at + c] == 5.0f;
                }
            }
    CHECK(inside);
    CHECK(past);
    for (int i = 0; i < 4; ++i) { uav[i]->Release(); tex[i]->Release(); }
    c2->Release(); c10->Release(); cs->Release();
}

int main() {
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &g_dev, nullptr, &g_ctx))) {
        std::printf("no WARP device\n");
        return 1;
    }
    TestBins();
    TestGrid(0);
    TestGrid(1);
    CHECK(benchlights::kBinSize == 18388 && benchlights::kGridSize == 5120);
    g_ctx->Release();
    g_dev->Release();
    std::printf("bench_lights_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
