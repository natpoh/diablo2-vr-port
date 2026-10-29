// The nocked arrow's fire, frost and arcane glow in the effect (the ARROWFX block of reshade/D2R_DepthFog.fx) without the game or a headset: the text between the marks is compiled and run
// on a software D3D11 device (WARP) over a synthetic scene - a dark picture and a wall as the game's depth - and the pixels are read back:
//   - it compiles, and with AfOn = 0 (the guard the effect's pass has) the picture is untouched bit for bit,
//   - each kind is light in its own colours at the arrow and nothing far from it (fire: red-orange-yellow with a hot heart; frost: pale blue and white; arcane: violet-blue and white),
//   - fire rises above the arrow, frost's mist hangs below it, the heart of a flame is hotter than its rim,
//   - the strength and the size scale it, the time moves it,
//   - a nearer surface hides it (the whole picture behind a wall), a farther one does not, and an arrow behind the eye draws nothing,
//   - the screen blend adds light and never goes over 1.
//   --bench  also times the pass on the first real GPU (a full-screen pass per eye, the arrow drawn and not) and prints the milliseconds.
//   ARROWFX_DUMP=<dir>  writes the pictures of each kind as PNG files to look at.
//
//   bow_arrow_fx_test

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int g_failed = 0, g_checks = 0;
void Expect(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok) return;
    ++g_failed;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    std::printf("  FAIL: %s\n", b);
}

// What the effect declares above the block (the uniforms) and the scene around it.
const char* kPrelude = R"(
cbuffer Cb : register(b0)
{
    float AfOn;
    float AfTime;
    float2 Pad;
    float4 AfA;
    float4 AfB;
    float4 CamP;     // x tanX, y tanY
    float4 EyeT;     // xyz the eye
    float4 WallP;    // x the wall's view depth (0 = none), y its first pixel column, z width, w height
};
)";
const char* kMain = R"(
float4 VS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PS(float4 pos : SV_Position) : SV_Target
{
    const float2 ndc = float2(pos.x / WallP.z * 2.0 - 1.0, 1.0 - pos.y / WallP.w * 2.0);
    const float3 rd = normalize(float3(ndc.x * CamP.x, ndc.y * CamP.y, -1.0));
    const float3 ro = EyeT.xyz;
    const float3 back = float3(0.0, 0.0, 1.0);
    float3 c = float3(0.08, 0.08, 0.10);
    float sceneZ = 1e9;
    if (WallP.x > 0.0 && pos.x > WallP.y) sceneZ = WallP.x;
    [branch] if (AfOn > 0.5) c = QuiverArrowFx(c, ro, rd, back, sceneZ);
    return float4(c, 1.0);
}
)";

struct Cb {
    float afOn, afTime, pad[2];
    float afA[4];
    float afB[4];
    float camP[4];
    float eyeT[4];
    float wallP[4];
};

struct Img {
    int w = 0, h = 0;
    std::vector<float> px;
    const float* At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

// ---- a PNG, to look at (stored deflate blocks: no compression library) --------------------------------------------------------------------------------------------------
uint32_t Crc(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static uint32_t tab[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t v = i; for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; tab[i] = v; }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) c = tab[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int k = 3; k >= 0; --k) v.push_back((uint8_t)(x >> (8 * k))); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& body) {
    Put32(out, (uint32_t)body.size());
    std::vector<uint8_t> t(type, type + 4);
    t.insert(t.end(), body.begin(), body.end());
    out.insert(out.end(), t.begin(), t.end());
    Put32(out, Crc(t.data(), t.size()) ^ 0xFFFFFFFFu);
}
bool WritePng(const std::string& path, const Img& im) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < im.h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            for (int k = 0; k < 3; ++k) raw.push_back((uint8_t)std::lround(std::pow(std::clamp(p[k], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t at = 0; at < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 0xFF)); z.push_back((uint8_t)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
    }
    Put32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    Put32(ihdr, (uint32_t)im.w); Put32(ihdr, (uint32_t)im.h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
    std::string error;
    std::string src;
    UINT instructions = 0;

    bool Compile(const std::string& fx) {
        const size_t b = fx.find("// ARROWFX-BEGIN"), e = fx.find("// ARROWFX-END");
        if (b == std::string::npos || e == std::string::npos || e < b) { error = "the ARROWFX marks are not in the effect"; return false; }
        const size_t bodyAt = fx.find('\n', b) + 1;
        src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
        return true;
    }
    bool Init(bool hardware) {
        ComPtr<ID3DBlob> vsb, psb, errors;
        HRESULT hr = D3DCompile(src.data(), src.size(), "arrow_fx.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "vs failed"; return false; }
        hr = D3DCompile(src.data(), src.size(), "arrow_fx.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &psb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "ps failed"; return false; }
        if (errors && errors->GetBufferSize() > 0) std::printf("  [compiler warnings]\n%s\n", (const char*)errors->GetBufferPointer());
        ComPtr<ID3D11ShaderReflection> refl;
        if (SUCCEEDED(D3DReflect(psb->GetBufferPointer(), psb->GetBufferSize(), IID_PPV_ARGS(&refl)))) {
            D3D11_SHADER_DESC sd{};
            if (SUCCEEDED(refl->GetDesc(&sd))) instructions = sd.InstructionCount;
        }
        if (FAILED(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) { error = hardware ? "no hardware device" : "no WARP device"; return false; }
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (sizeof(Cb) + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) { error = "no constant buffer"; return false; }
        return vs && ps;
    }

    bool Render(const Cb& c, int w, int h, Img* out) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> rt, stage;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt))) return false;
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &stage))) return false;
        ComPtr<ID3D11RenderTargetView> rtv;
        if (FAILED(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) return false;
        Cb k = c;
        k.wallP[2] = (float)w;
        k.wallP[3] = (float)h;
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &k, 0, 0);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        const D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11Buffer* cbs[1] = {cb.Get()};
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3, 0);
        ctx->CopyResource(stage.Get(), rt.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out->w = w;
        out->h = h;
        out->px.resize((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) std::memcpy(&out->px[(size_t)y * w * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * 16);
        ctx->Unmap(stage.Get(), 0);
        return true;
    }

    // The pass as the headset would run it: `passes` full-screen draws of w x h, timed on the GPU. Milliseconds per draw, or -1.
    double Time(const Cb& c, int w, int h, int passes) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> rt;
        ComPtr<ID3D11RenderTargetView> rtv;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) return -1.0;
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        ComPtr<ID3D11Query> dis, t0, t1;
        dev->CreateQuery(&qd, &dis);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        dev->CreateQuery(&qd, &t0);
        dev->CreateQuery(&qd, &t1);
        Cb k = c;
        k.wallP[2] = (float)w;
        k.wallP[3] = (float)h;
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &k, 0, 0);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        const D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11Buffer* cbs[1] = {cb.Get()};
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (int warm = 0; warm < 8; ++warm) ctx->Draw(3, 0);
        ctx->Flush();
        ctx->Begin(dis.Get());
        ctx->End(t0.Get());
        for (int i = 0; i < passes; ++i) ctx->Draw(3, 0);
        ctx->End(t1.Get());
        ctx->End(dis.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 a = 0, b = 0;
        for (int spin = 0; spin < 20000000; ++spin) {
            if (ctx->GetData(dis.Get(), &dj, sizeof dj, 0) == S_OK && ctx->GetData(t0.Get(), &a, sizeof a, 0) == S_OK && ctx->GetData(t1.Get(), &b, sizeof b, 0) == S_OK) break;
            dj.Frequency = 0;
        }
        if (dj.Disjoint || dj.Frequency == 0) return -1.0;
        return (double)(b - a) / (double)dj.Frequency * 1000.0 / (double)passes;
    }
};

constexpr int kW = 160, kH = 120;
const float kBg[3] = {0.08f, 0.08f, 0.10f};

// An arrow drawn: the nock end by the string hand, the head a little below the line of sight and half a metre out; the effect 5 cm round the head.
Cb Scene(float kind) {
    Cb c{};
    c.afOn = kind;
    c.afA[0] = 0.10f; c.afA[1] = -0.16f; c.afA[2] = -0.35f; c.afA[3] = 0.05f;
    c.afB[0] = -0.04f; c.afB[1] = -0.10f; c.afB[2] = -0.85f; c.afB[3] = 1.0f;
    c.camP[0] = 0.55f;
    c.camP[1] = 0.55f * (float)kH / (float)kW;
    c.wallP[0] = 0.0f;
    return c;
}
bool IsBg(const float* p) { return std::fabs(p[0] - kBg[0]) < 1e-6f && std::fabs(p[1] - kBg[1]) < 1e-6f && std::fabs(p[2] - kBg[2]) < 1e-6f; }
int Lit(const Img& im, float over = 0.03f) {
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            if (p[0] > kBg[0] + over || p[1] > kBg[1] + over || p[2] > kBg[2] + over) ++n;
        }
    return n;
}
bool Same(const Img& a, const Img& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }
// The light added to the background: the sum of each channel over the picture.
void Sum(const Img& im, double s[3]) {
    s[0] = s[1] = s[2] = 0.0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            for (int k = 0; k < 3; ++k) s[k] += p[k] - kBg[k];
        }
}
// Where a point of the scene lands on the picture, in pixels.
void Project(const Cb& c, int w, int h, const float* P, float* sx, float* sy) {
    *sx = (P[0] / -P[2]) / c.camP[0] * 0.5f * (float)w + 0.5f * (float)w;
    *sy = -(P[1] / -P[2]) / c.camP[1] * 0.5f * (float)h + 0.5f * (float)h;
}
// Lit pixels farther than `fromHead` pixels from the arrow's head and farther than `fromShaft` from its line: what flies away from it.
int LitFar(const Cb& c, const Img& im, float fromHead, float fromShaft) {
    float hx, hy, ax, ay;
    Project(c, im.w, im.h, c.afB, &hx, &hy);
    Project(c, im.w, im.h, c.afA, &ax, &ay);
    const float dx = hx - ax, dy = hy - ay, len2 = dx * dx + dy * dy;
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            if (!(p[0] > kBg[0] + 0.03f || p[1] > kBg[1] + 0.03f || p[2] > kBg[2] + 0.03f)) continue;
            if (std::hypot((float)x - hx, (float)y - hy) <= fromHead) continue;
            const float f = std::clamp(((float)x - ax) * dx / len2 + ((float)y - ay) * dy / len2, 0.0f, 1.0f);
            if (std::hypot((float)x - (ax + dx * f), (float)y - (ay + dy * f)) <= fromShaft) continue;
            ++n;
        }
    return n;
}
// Where the arrow's own line (the segment's projection) is in rows: lit pixels above it and below it, the line taken at the column of the pixel.
void AboveBelow(const Cb& c, const Img& im, int* above, int* below) {
    *above = *below = 0;
    auto proj = [&](const float* P, float* sx, float* sy) {
        *sx = (P[0] / -P[2]) / c.camP[0] * 0.5f * (float)im.w + 0.5f * (float)im.w;
        *sy = -(P[1] / -P[2]) / c.camP[1] * 0.5f * (float)im.h + 0.5f * (float)im.h;
    };
    float ax, ay, bx, by;
    proj(c.afA, &ax, &ay);
    proj(c.afB, &bx, &by);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            if (!(p[0] > kBg[0] + 0.03f || p[1] > kBg[1] + 0.03f || p[2] > kBg[2] + 0.03f)) continue;
            const float lo = std::min(ax, bx), hi = std::max(ax, bx);
            if ((float)x < lo || (float)x > hi) continue;
            const float f = (bx - ax) != 0.0f ? ((float)x - ax) / (bx - ax) : 0.0f;
            const float lineY = ay + (by - ay) * f;
            if ((float)y < lineY) ++*above; else ++*below;
        }
}

}  // namespace

int main(int argc, char** argv) {
    bool bench = false;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--bench") == 0) bench = true;
    std::string fx;
#ifdef ARROWFX_FX_PATH
    {
        FILE* f = nullptr;
        if (fopen_s(&f, ARROWFX_FX_PATH, "rb") == 0 && f) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) fx.append(buf, n);
            fclose(f);
        }
    }
#endif
    Gpu gpu;
    if (!gpu.Compile(fx) || !gpu.Init(false)) {
        std::printf("FAIL: %s\n", gpu.error.c_str());
        return 1;
    }
    Expect(true, "compiled");
    std::printf("  the pixel shader: %u instructions (the whole pass of the test, all three kinds)\n", gpu.instructions);

    const char* names[4] = {"off", "fire", "frost", "arcane"};
    Img img[4];
    {   // AfOn = 0: the picture untouched, whatever the arrow's numbers are
        Cb c = Scene(0.0f);
        Expect(gpu.Render(c, kW, kH, &img[0]), "render");
        bool allBg = true;
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) allBg = allBg && IsBg(img[0].At(x, y));
        Expect(allBg, "AfOn = 0: every pixel is the picture's own");
    }
    for (int k = 1; k <= 3; ++k) {
        Cb c = Scene((float)k);
        c.afTime = 1.7f;
        gpu.Render(c, kW, kH, &img[k]);
        const int lit = Lit(img[k]);
        Expect(lit > 150, "%s: the arrow is lit (%d px)", names[k], lit);
        Expect(lit < kW * kH / 3, "%s: and only round the arrow (%d of %d px)", names[k], lit, kW * kH);
        Expect(IsBg(img[k].At(3, 3)) && IsBg(img[k].At(kW - 4, 3)) && IsBg(img[k].At(3, kH - 4)) && IsBg(img[k].At(kW - 4, kH - 4)), "%s: the corners are untouched", names[k]);
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
            const float* p = img[k].At(x, y);
            if (p[0] > 1.0f || p[1] > 1.0f || p[2] > 1.0f) { Expect(false, "%s: over 1 at %d %d", names[k], x, y); y = kH; break; }
        }
    }
    {   // the colours: fire is red-orange (red over green over blue), frost is blue-white (blue first, red last), arcane violet-blue (blue first, green over red)
        double f[3], i[3], a[3];
        Sum(img[1], f); Sum(img[2], i); Sum(img[3], a);
        Expect(f[0] > f[1] * 1.25 && f[1] > f[2] * 1.3, "fire is red, then orange: %.1f %.1f %.1f", f[0], f[1], f[2]);
        Expect(i[2] > i[1] && i[1] > i[0] * 1.15 && i[2] > i[0] * 1.4, "frost is blue, then cyan-white: %.1f %.1f %.1f", i[0], i[1], i[2]);
        Expect(a[2] > a[1] * 1.15 && a[2] > a[0] * 1.25, "arcane is blue-violet: %.1f %.1f %.1f", a[0], a[1], a[2]);
    }
    {   // fire rises, the frost's mist hangs
        int fa, fb, ia, ib;
        const Cb sf = Scene(1.0f);
        AboveBelow(sf, img[1], &fa, &fb);
        AboveBelow(sf, img[2], &ia, &ib);
        Expect(fa > fb, "fire: more light above the arrow than below it (%d against %d px)", fa, fb);
        Expect(ib > ia * 0.9, "frost: the mist hangs, not more above than below (%d above, %d below)", ia, ib);
    }
    {   // the heart of the flame is hotter than its rim: the hottest pixel is white-ish (blue up), the dimmest lit one is red
        float hotB = 0.0f, rimRatio = 1e9f;
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
            const float* p = img[1].At(x, y);
            hotB = std::max(hotB, p[2]);
            if (p[0] > kBg[0] + 0.06f && p[0] < kBg[0] + 0.15f) rimRatio = std::min(rimRatio, (p[2] - kBg[2]) / (p[0] - kBg[0]));
        }
        Expect(hotB > 0.35f, "fire: a white-hot heart (blue %.2f at the hottest pixel)", hotB);
        Expect(rimRatio < 0.35f, "fire: the rim is red, not white (blue over red %.2f)", rimRatio);
    }
    {   // the heart is at the head, the sparks and glints are away from it, the flames' weight is above it
        const Cb sc = Scene(1.0f);
        float hx, hy;
        Project(sc, kW, kH, sc.afB, &hx, &hy);
        const float* hp = img[1].At((int)hx, (int)hy);
        const float hotAtHead = hp[2] - kBg[2], redAtHead = hp[0] - kBg[0];
        std::printf("  [calib] fire head pixel %.3f %.3f %.3f\n", hp[0], hp[1], hp[2]);
        Expect(hotAtHead > 0.2f && hotAtHead > 0.45f * redAtHead, "fire: the head is the hot heart (blue +%.2f over red +%.2f)", hotAtHead, redAtHead);
        for (int k = 1; k <= 2; ++k) {
            const int n = LitFar(sc, img[k], 24.0f, 7.0f);
            std::printf("  [calib] %s lit far from the head: %d\n", names[k], n);
            Expect(n >= 2, "%s: %s fly away from the head (%d px lit beyond 24 px of it)", names[k], k == 1 ? "embers" : "glints", n);
        }
        // seen from the side the flames' weight is above the head, the mist's below
        for (int k = 1; k <= 2; ++k) {
            Cb c = Scene((float)k);
            c.afTime = 1.7f;
            c.afA[0] = 0.30f; c.afA[1] = -0.03f; c.afA[2] = -0.55f;
            c.afB[0] = -0.16f; c.afB[1] = 0.0f; c.afB[2] = -0.55f;
            Img side;
            gpu.Render(c, kW, kH, &side);
            float bx, by;
            Project(c, kW, kH, c.afB, &bx, &by);
            double wy = 0.0, wsum = 0.0;
            for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
                if (std::fabs((float)x - bx) > 22.0f || std::fabs((float)y - by) > 40.0f) continue;
                const float* p = side.At(x, y);
                const double lum = std::max(0.0, (double)(p[0] + p[1] + p[2]) - (kBg[0] + kBg[1] + kBg[2]));
                wy += lum * y; wsum += lum;
            }
            const double dy = wsum > 0.0 ? wy / wsum - by : 0.0;
            std::printf("  [calib] %s side-on centroid is %.2f px %s the head\n", names[k], std::fabs(dy), dy < 0 ? "above" : "below");
            if (k == 1) Expect(dy < -1.0, "fire seen from the side: its light's centre is above the head (%.2f px)", dy);
            else Expect(dy > -0.5, "frost seen from the side: its centre is not above the head (%.2f px)", dy);
        }
    }
    {   // the strength, the size, the time
        for (int k = 1; k <= 3; ++k) {
            Cb c = Scene((float)k);
            c.afTime = 1.7f;
            Img strong, big, later;
            c.afB[3] = 2.0f;
            gpu.Render(c, kW, kH, &strong);
            c.afB[3] = 1.0f;
            c.afA[3] = 0.09f;
            gpu.Render(c, kW, kH, &big);
            c.afA[3] = 0.05f;
            c.afTime = 2.4f;
            gpu.Render(c, kW, kH, &later);
            double s1[3], s2[3];
            Sum(img[k], s1); Sum(strong, s2);
            Expect(s2[0] + s2[1] + s2[2] > 1.3 * (s1[0] + s1[1] + s1[2]), "%s: a strength of 2 is brighter", names[k]);
            Expect(Lit(big) > Lit(img[k]) + 40, "%s: a bigger size lights more (%d against %d px)", names[k], Lit(big), Lit(img[k]));
            Expect(!Same(later, img[k]), "%s: the time moves it", names[k]);
        }
    }
    {   // depth: a wall nearer than the arrow hides it, a farther one does not
        for (int k = 1; k <= 3; ++k) {
            Cb c = Scene((float)k);
            c.afTime = 1.7f;
            c.wallP[0] = 0.25f;   // 0.25 m: in front of the nock end (0.35 m)
            c.wallP[1] = -1.0f;
            Img hidden, shown, half;
            gpu.Render(c, kW, kH, &hidden);
            Expect(Lit(hidden, 0.005f) == 0, "%s: a wall in front hides the whole arrow", names[k]);
            c.wallP[0] = 3.0f;
            gpu.Render(c, kW, kH, &shown);
            Expect(Same(shown, img[k]), "%s: a wall behind it changes nothing", names[k]);
            c.wallP[0] = 0.25f;
            c.wallP[1] = (float)kW / 2;
            gpu.Render(c, kW, kH, &half);
            int left = 0, right = 0;
            for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
                const float* p = half.At(x, y);
                if (p[0] > kBg[0] + 0.01f || p[1] > kBg[1] + 0.01f || p[2] > kBg[2] + 0.01f) (x < kW / 2 ? left : right)++;
            }
            Expect(left > 30 && right == 0, "%s: cut by the wall's edge (%d px left of it, %d right)", names[k], left, right);
        }
    }
    {   // behind the eye: nothing
        Cb c = Scene(1.0f);
        c.afA[2] = 0.35f; c.afB[2] = 0.85f;
        Img behind;
        gpu.Render(c, kW, kH, &behind);
        Expect(Same(behind, img[0]), "an arrow behind the eye draws nothing");
    }
    {   // a degenerate arrow (no length) or no size: nothing
        Cb c = Scene(1.0f);
        for (int k = 0; k < 3; ++k) c.afB[k] = c.afA[k];
        Img none;
        gpu.Render(c, kW, kH, &none);
        Expect(Same(none, img[0]), "an arrow of no length draws nothing");
        c = Scene(2.0f);
        c.afA[3] = 0.0f;
        gpu.Render(c, kW, kH, &none);
        Expect(Same(none, img[0]), "a size of 0 draws nothing");
    }
    {   // degenerate inputs (the 2026-10-18 hangs were blamed on this block first): each renders, ends, and gives the picture back
        // untouched or finite and never over 1 - whatever the numbers, the loops have fixed counts
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        struct Case { const char* what; Cb c; bool untouched; };
        std::vector<Case> cases;
        for (int k = 1; k <= 3; ++k) {
            Cb c = Scene((float)k);
            c.afA[0] = nan; cases.push_back({"a NaN in the nock end", c, true});
            c = Scene((float)k); c.afB[1] = nan; cases.push_back({"a NaN in the head", c, true});
            c = Scene((float)k); c.afA[3] = nan; cases.push_back({"a NaN size", c, true});
            c = Scene((float)k); c.afB[3] = nan; cases.push_back({"a NaN strength", c, true});
            c = Scene((float)k); c.afA[2] = inf; cases.push_back({"an infinite nock end", c, true});
            c = Scene((float)k); c.afA[3] = inf; cases.push_back({"an infinite size", c, false});
            c = Scene((float)k); c.afA[3] = 1e6f; cases.push_back({"a huge size (size_pct far too big)", c, false});
            c = Scene((float)k); c.afA[3] = -0.05f; cases.push_back({"a negative size", c, true});
            c = Scene((float)k); c.afB[3] = 0.0f; cases.push_back({"no strength", c, true});
            c = Scene((float)k); c.afTime = nan; cases.push_back({"a NaN time", c, false});
            c = Scene((float)k); c.afTime = 1e9f; cases.push_back({"a huge time", c, false});
            c = Scene((float)k);   // the eye inside the effect's sphere: at the head itself
            for (int i = 0; i < 3; ++i) c.eyeT[i] = c.afB[i];
            cases.push_back({"the eye inside the sphere, at the head", c, false});
            c = Scene((float)k);   // the eye on the shaft's line, looking along it
            for (int i = 0; i < 3; ++i) c.eyeT[i] = c.afA[i] - (c.afB[i] - c.afA[i]);
            cases.push_back({"the eye on the shaft's line", c, false});
            c = Scene((float)k); c.eyeT[0] = nan; cases.push_back({"a NaN eye", c, true});
            c = Scene((float)k); c.wallP[0] = nan; c.wallP[1] = -1.0f; cases.push_back({"a NaN depth everywhere", c, false});
        }
        Cb odd = Scene(7.0f); cases.push_back({"an AfOn of no kind (7)", odd, false});
        for (const Case& t : cases) {
            Img im;
            const ULONGLONG t0 = GetTickCount64();
            const bool ok = gpu.Render(t.c, kW, kH, &im);
            const ULONGLONG ms = GetTickCount64() - t0;
            bool finite = ok, notOver = ok;
            for (size_t i = 0; ok && i < im.px.size(); ++i) {
                if (!std::isfinite(im.px[i])) finite = false;
                else if (im.px[i] > 1.0001f) notOver = false;
            }
            Expect(ok && finite && notOver, "kind %.0f, %s: renders, finite, never over 1", t.c.afOn, t.what);
            Expect(ms < 5000, "kind %.0f, %s: ends (%llu ms on WARP)", t.c.afOn, t.what, (unsigned long long)ms);
            if (t.untouched) Expect(ok && Same(im, img[0]), "kind %.0f, %s: draws nothing", t.c.afOn, t.what);
        }
    }
    if (const char* dir = std::getenv("ARROWFX_DUMP")) {   // pictures to look at: a bigger frame, the three kinds, a few moments
        const int w = 640, h = 480;
        for (int k = 1; k <= 3; ++k)
            for (int m = 0; m < 3; ++m) {
                Cb c = Scene((float)k);
                c.camP[1] = 0.55f * (float)h / (float)w;
                c.afTime = 1.7f + 0.37f * (float)m;
                if (m == 2) {   // seen from the side: the arrow across the view, the head to the left
                    c.afA[0] = 0.30f; c.afA[1] = -0.03f; c.afA[2] = -0.55f;
                    c.afB[0] = -0.16f; c.afB[1] = 0.0f; c.afB[2] = -0.55f;
                }
                Img im;
                if (!gpu.Render(c, w, h, &im)) continue;
                char path[512];
                std::snprintf(path, sizeof path, "%s\\arrowfx_%s_%d.png", dir, names[k], m);
                WritePng(path, im);
            }
        std::printf("  pictures written to %s\n", dir);
    }
    if (bench) {
        Gpu hw;
        hw.src = gpu.src;
        if (!hw.Init(true)) {
            std::printf("  [bench] no hardware device: %s\n", hw.error.c_str());
        } else {
            DXGI_ADAPTER_DESC ad{};
            ComPtr<IDXGIDevice> dd;
            ComPtr<IDXGIAdapter> ada;
            if (SUCCEEDED(hw.dev.As(&dd)) && SUCCEEDED(dd->GetAdapter(&ada)) && SUCCEEDED(ada->GetDesc(&ad))) std::printf("  [bench] adapter: %ls\n", ad.Description);
            const int w = 2160, h = 2160;   // one eye of a headset's render target
            const char* what[4] = {"no arrow (the guard only)", "fire", "frost", "arcane"};
            for (float tanHalf : {0.55f, 1.19f}) {   // 61 degrees across (as the pictures) and 100 degrees (a headset's)
                for (int k = 0; k <= 3; ++k) {
                    Cb c = Scene((float)k);
                    c.camP[0] = c.camP[1] = tanHalf;
                    c.afTime = 1.7f;
                    double best = 1e9;
                    for (int rep = 0; rep < 5; ++rep) {
                        const double ms = hw.Time(c, w, h, 60);
                        if (ms >= 0.0) best = std::min(best, ms);
                    }
                    std::printf("  [bench] %-26s %.3f ms per eye (%dx%d, view %.0f degrees)\n", what[k], best, w, h, 2.0 * std::atan(tanHalf) * 57.29578);
                }
            }
        }
    }
    std::printf(g_failed ? "\n%d of %d FAILED\n" : "\nall %d checks passed\n", g_failed ? g_failed : g_checks, g_checks);
    return g_failed ? 1 : 0;
}
