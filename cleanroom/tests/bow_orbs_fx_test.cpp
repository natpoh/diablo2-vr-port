// The bow quiver's orbs in the effect (the QUIVER-ORBS block of reshade/D2R_DepthFog.fx) without the game or a headset: the text between the marks is compiled and run on a
// software D3D11 device (WARP) over a synthetic scene - a dark picture and a wall as the game's depth - and the pixels are read back:
//   - it compiles, and with QvN = 0 the picture is untouched bit for bit,
//   - an orb is a soft ball in its colour with a bright heart and a halo, and nothing far from it,
//   - the skill the slot holds is bigger and brighter than an idle one, the orb the hand is at bigger still,
//   - a nearer surface hides an orb (the whole picture behind a wall), a farther one does not, and an orb behind the eye draws nothing,
//   - only the first QvN orbs are drawn,
//   - the screen blend adds light and never goes over 1.
//
//   bow_orbs_fx_test

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    float QvN;
    float QvTime;
    float2 Pad;
    float4 QvPos[12];
    float4 QvCol[12];
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
    c = QuiverOrbs(c, ro, rd, back, sceneZ);
    return float4(c, 1.0);
}
)";

struct Cb {
    float qvN, qvTime, pad[2];
    float qvPos[12][4];
    float qvCol[12][4];
    float camP[4];
    float eyeT[4];
    float wallP[4];
};

struct Img {
    int w = 0, h = 0;
    std::vector<float> px;
    const float* At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
    std::string error;

    bool Init(const std::string& fx) {
        const size_t b = fx.find("// QUIVER-ORBS-BEGIN"), e = fx.find("// QUIVER-ORBS-END");
        if (b == std::string::npos || e == std::string::npos || e < b) { error = "the QUIVER-ORBS marks are not in the effect"; return false; }
        const size_t bodyAt = fx.find('\n', b) + 1;
        const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
        ComPtr<ID3DBlob> vsb, psb, errors;
        HRESULT hr = D3DCompile(src.data(), src.size(), "quiver_orbs.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "vs failed"; return false; }
        hr = D3DCompile(src.data(), src.size(), "quiver_orbs.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &psb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "ps failed"; return false; }
        if (errors && errors->GetBufferSize() > 0) std::printf("  [compiler warnings]\n%s\n", (const char*)errors->GetBufferPointer());
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) { error = "no WARP device"; return false; }
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
};

constexpr int kW = 160, kH = 120;
const float kBg[3] = {0.08f, 0.08f, 0.10f};

Cb Scene() {
    Cb c{};
    c.camP[0] = 0.3f;
    c.camP[1] = 0.3f * (float)kH / (float)kW;
    c.wallP[0] = 0.0f;
    return c;
}
void SetOrb(Cb& c, int i, float x, float y, float z, float r, const float rgb[3], float state) {
    c.qvPos[i][0] = x; c.qvPos[i][1] = y; c.qvPos[i][2] = z; c.qvPos[i][3] = r;
    c.qvCol[i][0] = rgb[0]; c.qvCol[i][1] = rgb[1]; c.qvCol[i][2] = rgb[2]; c.qvCol[i][3] = state;
}
bool IsBg(const float* p) { return std::fabs(p[0] - kBg[0]) < 1e-6f && std::fabs(p[1] - kBg[1]) < 1e-6f && std::fabs(p[2] - kBg[2]) < 1e-6f; }
int Lit(const Img& im, float over = 0.04f) {
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const float* p = im.At(x, y);
            if (p[0] > kBg[0] + over || p[1] > kBg[1] + over || p[2] > kBg[2] + over) ++n;
        }
    return n;
}
bool Same(const Img& a, const Img& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }
// How far from the middle pixel (along the row, to the right) the red channel has fallen to half of what it is at the middle over the background: the orb's own size, whatever its
// brightness.
int HalfRadius(const Img& im) {
    const int cy = im.h / 2, cx = im.w / 2;
    const float mid = im.At(cx, cy)[0] - kBg[0];
    for (int x = cx; x < im.w; ++x)
        if (im.At(x, cy)[0] - kBg[0] < 0.5f * mid) return x - cx;
    return im.w - cx;
}

}  // namespace

int main() {
    std::string fx;
#ifdef QUIVER_FX_PATH
    {
        FILE* f = nullptr;
        if (fopen_s(&f, QUIVER_FX_PATH, "rb") == 0 && f) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) fx.append(buf, n);
            fclose(f);
        }
    }
#endif
    Gpu gpu;
    if (!gpu.Init(fx)) {
        std::printf("FAIL: %s\n", gpu.error.c_str());
        return 1;
    }
    Expect(true, "compiled");
    const float red[3] = {1.0f, 0.15f, 0.05f}, blue[3] = {0.2f, 0.4f, 1.0f};
    const float R = 0.03f;

    Img off, one;
    {   // QvN = 0: the picture untouched, however many orbs the arrays hold
        Cb c = Scene();
        SetOrb(c, 0, 0.0f, 0.0f, -1.0f, R, red, 0.0f);
        c.qvN = 0.0f;
        Expect(gpu.Render(c, kW, kH, &off), "render");
        bool allBg = true;
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) allBg = allBg && IsBg(off.At(x, y));
        Expect(allBg, "QvN = 0: every pixel is the picture's own");
    }
    {   // one orb straight ahead, 1 m out
        Cb c = Scene();
        SetOrb(c, 0, 0.0f, 0.0f, -1.0f, R, red, 0.0f);
        c.qvN = 1.0f;
        gpu.Render(c, kW, kH, &one);
        const float* mid = one.At(kW / 2, kH / 2);
        Expect(mid[0] > 0.7f && mid[0] > mid[1] + 0.2f && mid[0] > mid[2] + 0.2f, "the middle is the orb's red and bright: %.2f %.2f %.2f", mid[0], mid[1], mid[2]);
        Expect(mid[1] > kBg[1] + 0.15f, "... with a hot heart (the green rises): %.2f", mid[1]);
        Expect(IsBg(one.At(5, 5)) && IsBg(one.At(kW - 5, kH - 5)), "the far corners are untouched");
        // the halo: a little outside the ball it is dimmer than the middle but lit
        // (the ball at 1 m with a 3 cm radius is ~ 3 / 0.3 * 80 = 8 px; the halo reaches ~ 3 radii)
        const float* edge = one.At(kW / 2 + 12, kH / 2);
        Expect(edge[0] > kBg[0] + 0.01f && edge[0] < mid[0], "just outside, the halo: %.3f (middle %.3f)", edge[0], mid[0]);
        const float* farPx = one.At(kW / 2 + 40, kH / 2);
        Expect(IsBg(farPx), "far from the ball nothing");
        for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
            const float* p = one.At(x, y);
            if (p[0] > 1.0f || p[1] > 1.0f || p[2] > 1.0f) { Expect(false, "over 1 at %d %d", x, y); y = kH; break; }
        }
    }
    {   // the states (QvCol.w = 2 * current + hover): 0 idle, 1 the hand is at it, 2 the slot's skill, 3 both - the slot's skill is bigger and brighter, the hand's more
        Img st[4];
        int lit[4];
        for (int s = 0; s < 4; ++s) {
            Cb c = Scene();
            SetOrb(c, 0, 0.001875f, -0.001875f, -1.0f, R, red, (float)s);   // (the middle of the middle pixel: the same spot in every state)
            c.qvN = 1.0f;
            gpu.Render(c, kW, kH, &st[s]);
            lit[s] = Lit(st[s]);
            for (int y = 0; y < kH; ++y) for (int x = 0; x < kW; ++x) {
                const float* p = st[s].At(x, y);
                if (p[0] > 1.0f || p[1] > 1.0f || p[2] > 1.0f) { Expect(false, "state %d: over 1 at %d %d (%.3f %.3f %.3f)", s, x, y, p[0], p[1], p[2]); y = kH; break; }
            }
        }
        {   // the ball itself is bigger: a big dim orb (nothing saturates), the radius where the light has fallen to half
            const float dim[3] = {0.30f, 0.10f, 0.05f};
            int half[4];
            for (int s = 0; s < 4; ++s) {
                Cb c = Scene();
                SetOrb(c, 0, 0.001875f, -0.001875f, -1.0f, 0.08f, dim, (float)s);
                c.qvN = 1.0f;
                Img im;
                gpu.Render(c, kW, kH, &im);
                half[s] = HalfRadius(im);
            }
            Expect(half[2] >= half[0] + 2 && half[1] >= half[2] + 2, "the ball is bigger: half-light radius idle %d px, current %d, hovered %d", half[0], half[2], half[1]);
            Expect(half[1] * 100 >= half[0] * 150, "the hovered ball is 1.5 times the idle one at least (%d against %d px)", half[1], half[0]);
        }
        Expect(lit[2] > lit[0] && lit[1] > lit[2], "bigger: idle %d px, current %d, hovered %d", lit[0], lit[2], lit[1]);
        Expect(lit[3] >= lit[1], "both is at least as big as hovered: %d", lit[3]);
        const float g0 = st[0].At(kW / 2, kH / 2)[1], g1 = st[2].At(kW / 2, kH / 2)[1], g2 = st[1].At(kW / 2, kH / 2)[1];
        Expect(g1 > g0 && g2 > g1, "brighter heart: idle %.3f, current %.3f, hovered %.3f", g0, g1, g2);
        // the hovered one goes toward white: its blue rises more than the idle one's, against the red
        const float* hv = st[1].At(kW / 2 + 6, kH / 2);
        const float* id = st[0].At(kW / 2 + 6, kH / 2);
        Expect(hv[2] - kBg[2] > id[2] - kBg[2] + 0.05f, "the hovered orb goes white (blue %.3f against %.3f)", hv[2], id[2]);
        {   // hue and brightness apart from size: a dim orb (nothing saturates), the same relative spot in each
            const float dim[3] = {0.30f, 0.10f, 0.05f};
            Img idle, hov, light;
            auto render = [&](float w, Img* im) {
                Cb c = Scene();
                SetOrb(c, 0, 0.001875f, -0.001875f, -1.0f, 0.04f, dim, w);   // (4 cm at 1 m: ~10.7 px; hovered 1.6 times that)
                c.qvN = 1.0f;
                gpu.Render(c, kW, kH, im);
            };
            render(0.0f, &idle);
            render(1.0f, &hov);
            render(0.2f, &light);
            const float* pi = idle.At(kW / 2 + 7, kH / 2);    // 7 of 10.7 px: 0.65 of the radius
            const float* ph = hov.At(kW / 2 + 11, kH / 2);    // 11 of 17.1 px: 0.64
            const float ri = (pi[2] - kBg[2]) / std::max(pi[0] - kBg[0], 1e-4f), rh = (ph[2] - kBg[2]) / std::max(ph[0] - kBg[0], 1e-4f);
            Expect(rh > ri + 0.2f, "the hovered orb goes toward white: blue over red %.2f against %.2f", rh, ri);
            const float gi = idle.At(kW / 2, kH / 2)[1] - kBg[1], gl = light.At(kW / 2, kH / 2)[1] - kBg[1];
            Expect(gl > 1.4f * gi, "a hand nearing (0.2) already brightens the heart: %.3f against %.3f", gl, gi);
        }
        // the ramp: a hand nearing (hover 0.5) shows part of it, between idle and at
        Img mid;
        {
            Cb c = Scene();
            SetOrb(c, 0, 0.001875f, -0.001875f, -1.0f, R, red, 0.5f);
            c.qvN = 1.0f;
            gpu.Render(c, kW, kH, &mid);
        }
        Expect(Lit(mid) > lit[0] && Lit(mid) < lit[1], "a hover of 0.5 is between idle (%d px) and at (%d px): %d", lit[0], lit[1], Lit(mid));
    }
    {   // depth: a wall nearer than the orb hides it, a farther one does not
        Cb c = Scene();
        SetOrb(c, 0, 0.0f, 0.0f, -1.0f, R, red, 0.0f);
        c.qvN = 1.0f;
        c.wallP[0] = 0.5f;   // 0.5 m: in front of the orb (1 m)
        c.wallP[1] = -1.0f;  // every column
        Img hidden, shown, half;
        gpu.Render(c, kW, kH, &hidden);
        Expect(Lit(hidden, 0.005f) == 0, "a wall in front hides the whole orb");
        {   // the orb the hand is at is hidden by what is in front of it too, as the medallions are (0.174: no show-through; fingers in front hide it, the ball round them shows)
            Cb h = c;
            SetOrb(h, 0, 0.0f, 0.0f, -1.0f, R, red, 1.0f);
            Img seen;
            gpu.Render(h, kW, kH, &seen);
            Expect(Lit(seen, 0.005f) == 0, "the hovered orb is hidden by a wall in front, like the others: %d px", Lit(seen, 0.005f));
            SetOrb(h, 0, 0.0f, 0.0f, -1.0f, R, red, 2.0f);
            gpu.Render(h, kW, kH, &seen);
            Expect(Lit(seen, 0.005f) == 0, "the current orb (not hovered) is hidden as ever");
        }
        c.wallP[0] = 2.0f;   // behind it
        gpu.Render(c, kW, kH, &shown);
        Expect(Same(shown, one), "a wall behind it changes nothing");
        c.wallP[0] = 0.5f;
        c.wallP[1] = (float)kW / 2;   // the right half only
        gpu.Render(c, kW, kH, &half);
        const float* left = half.At(kW / 2 - 6, kH / 2);
        const float* right = half.At(kW / 2 + 6, kH / 2);
        Expect(left[0] > kBg[0] + 0.1f && IsBg(right), "cut by the wall's edge: the left of it is lit (%.2f), the right hidden", left[0]);
    }
    {   // behind the eye
        Cb c = Scene();
        SetOrb(c, 0, 0.0f, 0.0f, 1.0f, R, red, 0.0f);
        c.qvN = 1.0f;
        Img behind;
        gpu.Render(c, kW, kH, &behind);
        Expect(Same(behind, off), "an orb behind the eye draws nothing");
    }
    {   // only the first QvN are drawn; two orbs of two colours
        Cb c = Scene();
        SetOrb(c, 0, -0.15f, 0.0f, -1.0f, R, red, 0.0f);
        SetOrb(c, 1, 0.15f, 0.0f, -1.0f, R, blue, 0.0f);
        c.qvN = 1.0f;
        Img a, b;
        gpu.Render(c, kW, kH, &a);
        c.qvN = 2.0f;
        gpu.Render(c, kW, kH, &b);
        const float* x0 = b.At(kW / 2 - (int)(0.15f / 0.3f * 0.5f * kW), kH / 2);
        const float* x1 = b.At(kW / 2 + (int)(0.15f / 0.3f * 0.5f * kW), kH / 2);
        Expect(x0[0] > x0[2] && x1[2] > x1[0], "two orbs, red on the left and blue on the right: %.2f %.2f / %.2f %.2f", x0[0], x0[2], x1[0], x1[2]);
        Expect(IsBg(a.At(kW / 2 + (int)(0.15f / 0.3f * 0.5f * kW), kH / 2)), "QvN = 1: the second is not drawn");
    }
    {   // the beat: the current orb's brightness moves with the time, an idle orb's does not
        Img t0, t1, i0, i1;
        for (int cur = 0; cur < 2; ++cur) {
            Cb c = Scene();
            SetOrb(c, 0, 0.0f, 0.0f, -1.0f, R, red, (float)(cur * 2));
            c.qvN = 1.0f;
            c.qvTime = 0.0f;
            gpu.Render(c, kW, kH, cur ? &t0 : &i0);
            c.qvTime = 0.314f;
            gpu.Render(c, kW, kH, cur ? &t1 : &i1);
        }
        Expect(Same(i0, i1), "an idle orb does not beat");
        Expect(!Same(t0, t1), "the current orb beats");
    }
    std::printf(g_failed ? "\n%d of %d FAILED\n" : "\nall %d checks passed\n", g_failed ? g_failed : g_checks, g_checks);
    return g_failed ? 1 : 0;
}
