// The light-bounds replacement shader (vr/light_bounds_shader.h) on a software D3D11 device (WARP; the machine's
// GPU with D2RVR_TEST_HARDWARE=1), against a CPU scan of every pixel the shader is meant to read.
//
//   light_bounds_test [<game's shader, one DXBC container>]
//
// With the game's own compute shader as the argument (cs_light_bin_depth_bounds_c.bin's container: take
// it out of the game's archives with tools/casc_extract on your own machine - Blizzard's, never in the
// repository) the test also runs it on the same inputs and checks that
//   - at sizes whose tiles are square (16:10) it gives the very same numbers as ours, and
//   - at a square window it does not (the bug), while ours still matches the scan.
// Without it only ours is tested.

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../vr/light_bounds_shader.h"
#include "light_bounds_scene.h"

using Microsoft::WRL::ComPtr;

namespace {

using namespace lbscene;

int g_failed = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_failed; printf("  FAIL: %s\n", what); }
}
void Must(HRESULT hr, const char* what) {
    if (FAILED(hr)) { printf("%s failed: 0x%08lX\n", what, (unsigned long)hr); exit(2); }
}

std::vector<Pair> Run(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ComputeShader* cs, const Case& c,
                      const std::vector<float>& depth, uint32_t tw, uint32_t th, uint32_t ox, uint32_t oy) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = tw; td.Height = th; td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{depth.data(), tw * sizeof(float), 0};
    ComPtr<ID3D11Texture2D> tex;
    Must(dev->CreateTexture2D(&td, &sd, &tex), "depth texture");
    ComPtr<ID3D11ShaderResourceView> srv;
    Must(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv), "depth view");

    // b3: the lit area's size [1].xy, and the area uv -> texture uv: scale [3].xy, offset [2].zw
    float pass[16] = {};
    pass[4] = (float)c.w; pass[5] = (float)c.h;
    pass[10] = (float)ox / tw; pass[11] = (float)oy / th;
    pass[12] = (float)c.w / tw; pass[13] = (float)c.h / th;
    uint32_t grid[8] = {kCols, kRows, 0, 0, 0, kPitch, 0, 0};
    auto cbuffer = [&](const void* data, UINT bytes) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA s{data, 0, 0};
        ComPtr<ID3D11Buffer> b;
        Must(dev->CreateBuffer(&bd, &s, &b), "constant buffer");
        return b;
    };
    ComPtr<ID3D11Buffer> cb3 = cbuffer(pass, sizeof pass), cb12 = cbuffer(grid, sizeof grid);

    const UINT n = kPitch * kRows;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = n * sizeof(Pair); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = sizeof(Pair);
    ComPtr<ID3D11Buffer> out;
    Must(dev->CreateBuffer(&bd, nullptr, &out), "bounds buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = n;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Must(dev->CreateUnorderedAccessView(out.Get(), &ud, &uav), "bounds view");
    const UINT zeros[4] = {};
    ctx->ClearUnorderedAccessViewUint(uav.Get(), zeros);

    D3D11_SAMPLER_DESC sm{};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> smp;
    Must(dev->CreateSamplerState(&sm, &smp), "sampler");

    ctx->CSSetShader(cs, nullptr, 0);
    ID3D11Buffer* cbs3[] = {cb3.Get()};
    ID3D11Buffer* cbs12[] = {cb12.Get()};
    ctx->CSSetConstantBuffers(3, 1, cbs3);
    ctx->CSSetConstantBuffers(12, 1, cbs12);
    ID3D11ShaderResourceView* srvs[] = {srv.Get()};
    ctx->CSSetShaderResources(10, 1, srvs);
    ID3D11SamplerState* smps[] = {smp.Get()};
    ctx->CSSetSamplers(1, 1, smps);
    ID3D11UnorderedAccessView* uavs[] = {uav.Get()};
    ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ctx->Dispatch(kCols, kRows, 1);

    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.StructureByteStride = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> stage;
    Must(dev->CreateBuffer(&bd, nullptr, &stage), "readback buffer");
    ctx->CopyResource(stage.Get(), out.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    Must(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m), "map");
    std::vector<Pair> r((const Pair*)m.pData, (const Pair*)m.pData + n);
    ctx->Unmap(stage.Get(), 0);
    ID3D11ShaderResourceView* none[] = {nullptr};
    ctx->CSSetShaderResources(10, 1, none);
    ID3D11UnorderedAccessView* noneU[] = {nullptr};
    ctx->CSSetUnorderedAccessViews(0, 1, noneU, nullptr);
    return r;
}

size_t Differences(const std::vector<Pair>& a, const std::vector<Pair>& b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (memcmp(&a[i], &b[i], sizeof(Pair)) != 0) ++n;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    ComPtr<ID3DBlob> blob, errors;
    const HRESULT hr = D3DCompile(lightbounds::kShader, sizeof lightbounds::kShader - 1, "light_bounds", nullptr, nullptr, "main",
                                  "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    if (FAILED(hr)) {
        printf("the shader did not compile: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
        return 2;
    }
    printf("compiled: %zu bytes\n", (size_t)blob->GetBufferSize());

    // what it binds: b3, b12, t10, s1, u0 - the game's layout, nothing else
    {
        ComPtr<ID3D11ShaderReflection> refl;
        Must(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&refl)), "reflection");
        D3D11_SHADER_DESC sdesc{};
        refl->GetDesc(&sdesc);
        struct Want { D3D_SHADER_INPUT_TYPE type; UINT point; };
        std::vector<Want> got;
        for (UINT i = 0; i < sdesc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC b{};
            refl->GetResourceBindingDesc(i, &b);
            Check(b.BindCount == 1, "every binding has count 1");
            got.push_back({b.Type, b.BindPoint});
        }
        auto has = [&](D3D_SHADER_INPUT_TYPE t, UINT p) { for (const Want& w : got) if (w.type == t && w.point == p) return true; return false; };
        Check(got.size() == 5, "five bindings");
        Check(has(D3D_SIT_CBUFFER, 3) && has(D3D_SIT_CBUFFER, 12), "constant buffers b3 and b12");
        Check(has(D3D_SIT_TEXTURE, 10), "texture t10");
        Check(has(D3D_SIT_SAMPLER, 1), "sampler s1");
        Check(has(D3D_SIT_UAV_RWSTRUCTURED, 0), "structured UAV u0");
    }

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    // WARP unless D2RVR_TEST_HARDWARE is set: then the machine's own GPU (its division and gather rounding at the tile edges)
    const bool hardware = GetEnvironmentVariableA("D2RVR_TEST_HARDWARE", nullptr, 0) != 0;
    Must(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                           D3D11_SDK_VERSION, &dev, nullptr, &ctx), hardware ? "hardware device" : "WARP device");
    printf("device: %s\n", hardware ? "hardware" : "WARP");
    ComPtr<ID3D11ComputeShader> ours, native;
    Must(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ours), "our shader");

    if (argc > 1) {
        FILE* f = nullptr;
        std::vector<uint8_t> bytes;
        if (!fopen_s(&f, argv[1], "rb") && f) {
            fseek(f, 0, SEEK_END);
            bytes.resize((size_t)ftell(f));
            fseek(f, 0, SEEK_SET);
            fread(bytes.data(), 1, bytes.size(), f);
            fclose(f);
        }
        if (bytes.empty() || FAILED(dev->CreateComputeShader(bytes.data(), bytes.size(), nullptr, &native))) {
            printf("the game's shader %s could not be read or made\n", argv[1]);
            return 2;
        }
        printf("the game's shader: %zu bytes\n", bytes.size());
    } else {
        printf("(no game shader given: ours against the scan only)\n");
    }

    static const Case cases[] = {
        {"1920x1200 (16:10)", 1920, 1200, false, true},
        {"2560x1600 (16:10)", 2560, 1600, false, true},
        {"1920x1200 in a texture twice as big (16:10, atlas)", 1920, 1200, true, true},
        {"2048x2048 (square)", 2048, 2048, false, false},
        {"4096x4096 (square)", 4096, 4096, false, false},
        {"2000x2000 (square, tiles of odd size)", 2000, 2000, false, false},
        {"2048x2048 in a texture twice as big (square, atlas)", 2048, 2048, true, false},
        {"2560x1080 (wide)", 2560, 1080, false, false},
        {"3440x1440 (wide)", 3440, 1440, false, false},
        {"1600x1200 (4:3)", 1600, 1200, false, false},
        // flat mode: a monitor or a window (16:9 is what most have), and the user's squared flat window
        {"1920x1080 (16:9)", 1920, 1080, false, false},
        {"2560x1440 (16:9)", 2560, 1440, false, false},
        {"3840x2160 (16:9)", 3840, 2160, false, false},
        {"1366x768 (16:9, tiles of odd size)", 1366, 768, false, false},
        {"1280x720 (16:9)", 1280, 720, false, false},
        {"1920x1920 (the user's squared window)", 1920, 1920, false, false},
    };
    for (const Case& c : cases) {
        printf("%s\n", c.name);
        const uint32_t tw = c.atlas ? 2 * c.w : c.w, th = c.atlas ? 2 * c.h : c.h;
        const uint32_t ox = c.atlas ? c.w / 2 : 0, oy = c.atlas ? c.h / 2 : 0;
        const std::vector<float> depth = MakeDepth(c, tw, th, ox, oy);
        const std::vector<Pair> want = Scan(c, depth, tw, th, ox, oy);
        const std::vector<Pair> got = Run(dev.Get(), ctx.Get(), ours.Get(), c, depth, tw, th, ox, oy);
        const size_t bad = Differences(got, want);
        printf("  ours against the scan: %zu of %zu entries differ\n", bad, want.size());
        Check(bad == 0, "ours equals the scan");
        if (native) {
            const std::vector<Pair> theirs = Run(dev.Get(), ctx.Get(), native.Get(), c, depth, tw, th, ox, oy);
            const size_t nativeBad = Differences(theirs, want), apart = Differences(theirs, got);
            printf("  the game's against the scan: %zu differ; the game's against ours: %zu differ\n", nativeBad, apart);
            if (c.squareTiles) Check(apart == 0 && nativeBad == 0, "square tiles: the game's shader and ours give the same numbers");
            else Check(nativeBad > 0, "non-16:10: the game's shader gets some tile wrong (the bug)");
        }
    }
    printf(g_failed ? "FAILED (%d)\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
