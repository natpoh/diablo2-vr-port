// The low-mana vignette ([hud] low_mp_vignette, vr/low_mp.h) without the game or a headset:
//
//   - the settings put in range, the defaults (off; its equalizer off, its points the height's default at the default threshold),
//   - the band: nothing above its top, the full blue at the bottom edge, the sides as the middle (the whole width), the field of view;
//   - the beat: the life's Heartbeat fed the mana - nothing above the threshold, the plain rate (counted over a minute), the equalizer's
//     cover as the band's height and its strength,
//     no mana at all the most urgent (not "no read"), nothing when the hero is dead or in a menu, a point at 0 steady;
//   - the effect's own HLSL (the text between the LOWMP marks in reshade/D2R_DepthFog.fx) compiled and run on WARP against the C++
//     mirror, the picture bit for bit at opacity 0; and the passes lay the blue first and the life's red over it, so where both are the
//     red reads at its full strength (the life's Apply after the mana's, as the effect does).
//
//   low_mp_test [--fx <path to D2R_DepthFog.fx>]
//
// What it cannot say: how the blue looks in the lenses.

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "low_mp.h"

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
    printf("FAIL: %s\n", b);
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void TestConfig() {
    const lowmp::Config d;
    Expect(!d.on && !d.coverEq.on, "off by default, its equalizer too");
    for (int k = 0; k < lowhp::eq::kBands; ++k) {
        Expect(d.coverEq.cover[k] == lowmp::Config::kCoverDefault[k], "point %d starts at its default", k);
        char want[32], got[32] = {};
        snprintf(want, sizeof want, "low_mp_cover_eq_%d", lowhp::eq::kLifePct[k]);
        for (int i = 0; lowmp::kCoverKeys[k][i] && i < 31; ++i) got[i] = (char)lowmp::kCoverKeys[k][i];
        Expect(strcmp(want, got) == 0, "point %d's key is %s (got %s)", k, want, got);
    }
    Expect(Near(lowhp::eq::CoverAt(d.coverEq, d.threshold), d.height, 1e-6f), "at the default threshold the default cover is the default height");
    lowmp::Config c;
    c.threshold = 5.0f;
    c.height = -1.0f;
    c.intensityStart = NAN;
    c.softness = 3.0f;
    c.coverEq.cover[2] = 900.0f;
    const lowmp::Config s = lowmp::Sanitized(c);
    Expect(s.threshold == lowmp::kThresholdMax && s.height == lowmp::kHeightMin && s.intensityStart == d.intensityStart && s.softness == 1.0f,
           "out of range: the end; not a number: the default");
    Expect(s.coverEq.cover[2] == lowhp::eq::kCoverMax, "its points in range");
    // the beat's config: the mana's threshold, intensities and points; never gentle, never in the hands
    c = lowmp::Config();
    c.on = true;
    c.threshold = 0.4f;
    c.coverEq.on = true;
    c.coverEq.cover[0] = 33.0f;
    const lowhp::Config b = lowmp::BeatConfig(c);
    Expect(b.on && Near(b.threshold, 0.4f) && b.coverEq.on && b.coverEq.cover[0] == 33.0f && !b.gentle && !b.haptics,
           "the beat's config is the mana's, its points too, with no pulse in the hands");
}

void TestShape() {
    lowmp::Config c;
    c.height = 0.25f;
    c.softness = 0.8f;
    const lowmp::Shape s = lowmp::ShapeOf(c);
    Expect(Near(s.inner, 0.75f), "it starts a quarter of the half-height up from the bottom edge");
    for (float u = 0.0f; u <= 1.0f; u += 0.125f) {
        Expect(lowmp::Mask(u, 0.5f, s) == 0.0f && lowmp::Mask(u, 0.0f, s) == 0.0f && lowmp::Mask(u, 0.80f, s) == 0.0f, "the middle, the top and above its start: clear");
        Expect(Near(lowmp::Mask(u, 1.0f, s), lowmp::Mask(0.5f, 1.0f, s), 1e-6f), "the bottom edge alike across the whole width");
    }
    Expect(lowmp::Mask(0.5f, 1.0f, s) > 0.9f, "the bottom edge nearly the full blue (%.2f)", lowmp::Mask(0.5f, 1.0f, s));
    float last = -1.0f;
    bool rising = true;
    for (int i = 0; i <= 100; ++i) {
        const float m = lowmp::Mask(0.5f, 0.5f + 0.005f * (float)i, s);
        rising = rising && m >= last - 1e-6f;
        last = m;
    }
    Expect(rising, "the blue only grows toward the bottom");
    // the field of view: the same angle on any headset - in a wide view's picture that angle is more of the rows (a row near the bottom looks further down)
    const lowmp::Shape wide = lowmp::ShapeOf(c, 2.14f);
    Expect(lowmp::Mask(0.5f, 0.92f, wide) > lowmp::Mask(0.5f, 0.92f, s), "a 130 degree view: the same angle reaches further up the picture");
    // the equalizer's cover: the height instead of the slider's, up to the middle row
    Expect(Near(lowmp::ShapeOf(c, 0.0f, 0.5f).inner, 0.5f) && Near(lowmp::ShapeOf(c, 0.0f, 1.0f).inner, 0.0f) && Near(lowmp::ShapeOf(c, 0.0f, -1.0f).inner, 0.75f),
           "a cover of 50%% starts half way down, 100%% at the middle row, none: the height");
    Expect(Near(lowmp::MakeUniforms(0.5f, c, 0.0f, 0.5f).shape[0], 0.5f), "the effect is handed the cover's height");
    // thinner by default than the life's red all round
    Expect(lowmp::Config().height < lowhp::Config().radius, "by default lower than the red reaches");
    // the life's red over it: where both are full, the red is all that shows; the blue never lowers the red's own strength
    const float px[3] = {0.5f, 0.6f, 0.4f};
    float blue[3], both[3], redOnly[3];
    const lowhp::Uniforms hu = lowhp::MakeUniforms(1.0f, lowhp::Config());
    const lowhp::Shape hs = lowhp::ShapeOf(lowhp::Config());
    const lowmp::Uniforms mu = lowmp::MakeUniforms(1.0f, c);
    lowmp::Apply(px, 0.5f, 1.0f, 0.6f, s, mu.color, blue);
    lowhp::Apply(blue, 0.5f, 1.0f, 0.7f, hs, hu.color, both);
    lowhp::Apply(px, 0.5f, 1.0f, 0.7f, hs, hu.color, redOnly);
    const float k = 0.7f * lowhp::Mask(0.5f, 1.0f, hs);
    for (int i = 0; i < 3; ++i)
        Expect(Near(both[i] - hu.color[i], (1.0f - k) * (blue[i] - hu.color[i]), 1e-5f), "the red over the blue at its own strength (channel %d)", i);
    lowmp::Apply(px, 0.5f, 1.0f, 0.0f, s, mu.color, blue);
    Expect(memcmp(blue, px, sizeof px) == 0, "opacity 0: the picture bit for bit");
}

struct Run {
    int lubs = 0;
    float lo = 1e9f, hi = -1e9f, cover = -2.0f;
};
Run Drive(const lowmp::Config& c, float mana, float seconds, bool menu = false, bool dead = false, bool known = true) {
    lowhp::Heartbeat hb;
    const float dt = 1.0f / 90.0f;
    Run r;
    for (float t = 0.0f; t < 3.0f; t += dt) hb.Step(lowmp::BeatConfig(c), lowmp::BeatIn(true, menu, known, mana, dead, dt));
    for (float t = 0.0f; t < seconds; t += dt) {
        const lowhp::Heartbeat::Out o = hb.Step(lowmp::BeatConfig(c), lowmp::BeatIn(true, menu, known, mana, dead, dt));
        r.lubs += o.beat.lub;
        r.lo = std::fmin(r.lo, o.alpha);
        r.hi = std::fmax(r.hi, o.alpha);
        r.cover = o.cover;
    }
    return r;
}

void TestBeat() {
    lowmp::Config c;
    c.on = true;
    c.threshold = 0.40f;
    Run r = Drive(c, 0.50f, 5.0f);
    Expect(r.hi == 0.0f, "above the threshold: nothing");
    r = Drive(c, 0.20f, 60.0f);
    const float bpm = lowhp::Bpm(lowhp::Severity(0.20f, 0.40f), false);
    Expect(std::abs((float)r.lubs - bpm) <= 1.0f && r.lo < 0.5f * r.hi, "at 20%% mana the plain rate, a real beat (%d a minute, %.0f; %.3f - %.3f)", r.lubs, bpm, r.lo, r.hi);
    Expect(r.cover == -1.0f, "the equalizer off: no cover, the height");
    r = Drive(c, 0.0f, 10.0f);
    Expect(r.hi > 0.0f && Near(r.hi, c.intensityEnd, 0.03f), "no mana at all: shown, the most urgent, at the intensity for no mana (%.3f)", r.hi);
    Expect(Drive(c, 0.10f, 3.0f, true).hi == 0.0f, "a menu open: nothing");
    Expect(Drive(c, 0.10f, 3.0f, false, true).hi == 0.0f, "the hero dead: nothing");
    Expect(Drive(c, 0.10f, 3.0f, false, false, false).hi == 0.0f, "no read of the mana: nothing");
    lowmp::Config off = c;
    off.on = false;
    Expect(Drive(off, 0.10f, 3.0f).hi == 0.0f, "off: nothing");
    // the equalizer: the cover at the mana, its strength on the opacity, the beat as without it
    lowmp::Config e = c;
    e.coverEq.on = true;
    for (float& v : e.coverEq.cover) v = 40.0f;
    e.coverEq.cover[8] = 15.0f;   // 20%: half strength
    e.coverEq.cover[11] = 0.0f;   // 5%: none
    const Run plain = Drive(c, 0.20f, 30.0f), eq = Drive(e, 0.20f, 30.0f);
    Expect(Near(eq.cover, 0.15f, 1e-5f) && eq.lubs == plain.lubs && Near(eq.hi, 0.5f * plain.hi, 1e-4f), "at 20%%: cover 15, half as strong, the same beat (%.3f of %.3f)", eq.hi, plain.hi);
    const Run none = Drive(e, 0.03f, 5.0f);
    Expect(none.hi == 0.0f && Near(none.cover, 0.0f), "under 5%% with that point at 0: no blue");
    Expect(Near(Drive(e, 0.39f, 2.0f).cover, 0.40f, 1e-5f), "at 39%%: between two points of 40, 40");
}

std::string ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void TestHlsl(const std::string& fxPath) {
    printf("the effect's HLSL (%s)\n", fxPath.c_str());
    const std::string fx = ReadAll(fxPath);
    if (fx.empty()) { Expect(false, "could not read %s", fxPath.c_str()); return; }
    const size_t b = fx.find("// LOWMP-BEGIN"), e = fx.find("// LOWMP-END");
    Expect(b != std::string::npos && e != std::string::npos && b < e, "the LOWMP marks are in the effect");
    if (b == std::string::npos || e == std::string::npos || b > e) return;
    const std::string body = fx.substr(fx.find('\n', b) + 1, e - fx.find('\n', b) - 1);
    Expect(fx.find("uniform float LowMpAlpha < hidden = true; > = 0.0;") != std::string::npos, "the effect's opacity defaults to 0 (off)");
    Expect(fx.find("float4(0.75, 1.10, 0.0, 0.0)") != std::string::npos && fx.find("float3(0.01, 0.07, 0.36)") != std::string::npos, "the effect's defaults are lowmp::Uniforms'");
    int passes = 0;
    for (const char* p : {"LowHp(LowMp(Phantom(pos, uv, 0, false), uv), uv)", "LowHp(LowMp(Phantom(pos, uv, 1, false), uv), uv)", "LowHp(LowMp(Phantom(pos, uv, 0, true), uv), uv)",
                          "LowHp(LowMp(Phantom(pos, uv, 1, true), uv), uv)"})
        passes += fx.find(p) != std::string::npos;
    Expect(passes == 4, "every Phantom pass lays the blue first and the red over it (%d of 4)", passes);

    const std::string src =
        "cbuffer Uniforms : register(b0) { float LowMpAlpha; float3 pad0; float4 LowMpShape; float3 LowMpColor; float pad1; };\n"
        "StructuredBuffer<float4> In : register(t0);\n"
        "RWStructuredBuffer<float4> Out : register(u0);\n" +
        body +
        "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "    const float4 a = In[2 * id.x], c = In[2 * id.x + 1];\n"
        "    Out[id.x] = float4(LowMp(c.rgb, a.xy), 1.0);\n"
        "}\n";
    ComPtr<ID3DBlob> blob, errors;
    const HRESULT hr = D3DCompile(src.data(), src.size(), "LowMp", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &errors);
    Expect(SUCCEEDED(hr), "the effect's LowMp compiles%s%s", errors ? ": " : "", errors ? (const char*)errors->GetBufferPointer() : "");
    if (FAILED(hr)) return;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) { Expect(false, "no WARP device"); return; }
    ComPtr<ID3D11ComputeShader> cs;
    if (FAILED(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs))) { Expect(false, "the shader could not be made"); return; }

    struct Item { float u, v, r, g, b; };
    std::vector<Item> items;
    uint32_t seed = 4242;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    for (int j = 0; j <= 48; ++j)
        for (int i = 0; i <= 48; ++i) items.push_back({i / 48.0f, j / 48.0f, rnd(), rnd(), rnd()});
    const UINT n = (UINT)items.size();
    std::vector<float> input;
    for (const Item& it : items) { const float x[8] = {it.u, it.v, 0, 0, it.r, it.g, it.b, 0}; input.insert(input.end(), x, x + 8); }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(input.size() * sizeof(float)); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
    D3D11_SUBRESOURCE_DATA sd{input.data(), 0, 0};
    ComPtr<ID3D11Buffer> inBuf, outBuf, stage;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    vd.Format = DXGI_FORMAT_UNKNOWN; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.NumElements = n * 2;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = n;
    if (FAILED(dev->CreateBuffer(&bd, &sd, &inBuf)) || FAILED(dev->CreateShaderResourceView(inBuf.Get(), &vd, &srv))) { Expect(false, "input buffer"); return; }
    bd = {};
    bd.ByteWidth = n * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &outBuf)) || FAILED(dev->CreateUnorderedAccessView(outBuf.Get(), &ud, &uav))) { Expect(false, "output buffer"); return; }
    bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.StructureByteStride = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &stage))) { Expect(false, "readback buffer"); return; }

    struct Case { float alpha, height, softness, tanY; };
    const Case cases[] = {{0.0f, 0.25f, 0.8f, 0.0f}, {0.0f, 0.25f, 0.8f, 1.19f}, {0.3f, 0.25f, 0.8f, 0.0f}, {0.55f, 0.25f, 0.8f, 1.19f},
                          {1.0f, 0.60f, 1.0f, 2.14f}, {0.7f, 0.10f, 0.0f, 0.84f}, {1.6f, 0.30f, 0.5f, 1.19f}};
    float worst = 0.0f;
    bool exactOff = true;
    for (const Case& k : cases) {
        lowmp::Config cfg;
        cfg.height = k.height;
        cfg.softness = k.softness;
        const lowmp::Uniforms uni = lowmp::MakeUniforms(1.0f, cfg, k.tanY);
        struct Cb { float alpha, pad0[3], shape[4], color[3], pad1; } cb = {k.alpha, {}, {uni.shape[0], uni.shape[1], uni.shape[2], 0.0f}, {uni.color[0], uni.color[1], uni.color[2]}, 0.0f};
        D3D11_BUFFER_DESC cd{};
        cd.ByteWidth = sizeof cb; cd.Usage = D3D11_USAGE_IMMUTABLE; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA csd{&cb, 0, 0};
        ComPtr<ID3D11Buffer> cbuf;
        if (FAILED(dev->CreateBuffer(&cd, &csd, &cbuf))) { Expect(false, "constant buffer"); return; }
        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ID3D11Buffer* cbs[] = {cbuf.Get()};
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ID3D11ShaderResourceView* srvs[] = {srv.Get()};
        ctx->CSSetShaderResources(0, 1, srvs);
        ID3D11UnorderedAccessView* uavs[] = {uav.Get()};
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ctx->Dispatch((n + 63) / 64, 1, 1);
        ctx->CopyResource(stage.Get(), outBuf.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) { Expect(false, "map"); return; }
        const float* got = (const float*)m.pData;
        const lowmp::Shape shape = lowmp::ShapeOf(cfg, k.tanY);
        float caseWorst = 0.0f;
        for (UINT i = 0; i < n; ++i) {
            const float px[3] = {items[i].r, items[i].g, items[i].b};
            float cpu[3];
            lowmp::Apply(px, items[i].u, items[i].v, k.alpha, shape, uni.color, cpu);
            for (int c = 0; c < 3; ++c) {
                caseWorst = std::max(caseWorst, std::fabs(cpu[c] - got[i * 4 + c]));
                if (k.alpha == 0.0f && memcmp(&px[c], &got[i * 4 + c], 4) != 0) exactOff = false;
            }
        }
        ctx->Unmap(stage.Get(), 0);
        worst = std::max(worst, caseWorst);
        printf("  opacity %.2f, height %.0f%%, softness %.1f, tan %.2f: the HLSL differs from the C++ mirror by at most %.2e over %u points\n", k.alpha, k.height * 100.0f,
               k.softness, k.tanY, caseWorst, n);
    }
    Expect(worst < 3e-4f, "the HLSL and the C++ mirror agree (worst %.2e)", worst);
    Expect(exactOff, "opacity 0 in the HLSL: every colour comes out bit for bit");
}

}  // namespace

int main(int argc, char** argv) {
    std::string fx = LOWMP_FX_PATH;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--fx") && i + 1 < argc) fx = argv[++i];
    TestConfig();
    TestShape();
    TestBeat();
    TestHlsl(fx);
    printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "ok", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
