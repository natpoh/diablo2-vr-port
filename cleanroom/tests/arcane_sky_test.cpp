// arcane_sky_test - [sky] arcane_sky (shared/arcane_sky.h, after bearstonem/d2r-pcvr v0.3.18, MIT) without the game:
//   - the BC4 decoder on hand-made blocks (both of BC4's modes, every index) and its refusals (another layout, a truncated
//     file, a mip of the wrong size);
//   - the visual file's one change ("screenspaceVfx":"" in place of the stars, every other byte as it was) and its refusals;
//   - the PNG it writes, read back by Windows' own decoder (WIC), the receipt and when a file may be replaced;
//   - the effect's own HLSL (the ARCANE marks in reshade/D2R_DepthFog.fx) compiled and run on WARP in all six directions and
//     between them, against a C++ mirror of it: finite, as the mirror.
// With the game's own files at hand (releases/art/bear_arcane/raw, never in the repository): those decode and strip too, and
// the decoded star layers and clouds equal an independent decoder's pictures (releases/art/bear_arcane/1_*.png, 2_*.png).
//   arcane_sky_test [--fx <D2R_DepthFog.fx>] [--art <releases/art/bear_arcane>]

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "arcane_sky.h"

using Microsoft::WRL::ComPtr;
using namespace arcanesky;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}

static std::string ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = (uint8_t)(x >> (8 * i)); }

// A .texture as the game's: the header, the mip table (size, offset from the field after it), the blocks.
static std::vector<uint8_t> MakeTexture(int w, int h, int mips, const std::vector<std::vector<uint8_t>>& levels) {
    const size_t head = 36 + (size_t)mips * 8;
    size_t total = head;
    for (const auto& l : levels) total += l.size();
    std::vector<uint8_t> t(total, 0);
    Put32(t, 0, 0x2845443Cu);
    t[4] = 63;
    Put32(t, 8, (uint32_t)w);
    Put32(t, 12, (uint32_t)h);
    Put32(t, 28, (uint32_t)mips);
    size_t at = head;
    for (int l = 0; l < mips; ++l) {
        Put32(t, 36 + l * 8, (uint32_t)levels[l].size());
        Put32(t, 40 + l * 8, (uint32_t)(at - (40 + l * 8)));
        memcpy(t.data() + at, levels[l].data(), levels[l].size());
        at += levels[l].size();
    }
    return t;
}

static std::vector<uint8_t> Block(uint8_t a, uint8_t b, const int idx[16]) {
    std::vector<uint8_t> k(8, 0);
    k[0] = a;
    k[1] = b;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= (uint64_t)(idx[i] & 7) << (3 * i);
    for (int i = 0; i < 6; ++i) k[2 + i] = (uint8_t)(bits >> (8 * i));
    return k;
}

static void Bc4() {
    // two blocks across, one down: 8 x 4; the first in the 8-value mode, the second in the 6-value one
    int idx[16];
    for (int i = 0; i < 16; ++i) idx[i] = i % 8;
    std::vector<uint8_t> top = Block(200, 100, idx);
    const std::vector<uint8_t> second = Block(50, 150, idx);
    top.insert(top.end(), second.begin(), second.end());
    const std::vector<uint8_t> mip1 = Block(9, 9, idx);   // 4 x 2: one block
    const std::vector<uint8_t> mip2 = Block(9, 9, idx);   // 2 x 1: one block
    const std::vector<uint8_t> t = MakeTexture(8, 4, 3, {top, mip1, mip2});
    Gray g;
    std::string why;
    Expect(DecodeBC4(t.data(), t.size(), &g, &why) && g.w == 8 && g.h == 4 && g.px.size() == 32, "BC4: 8 x 4, three mips: decodes");
    // a > b: a, b, then six steps between: (6a+b)/7 ... (a+6b)/7, rounded down
    const int eight[8] = {200, 100, 185, 171, 157, 142, 128, 114};
    // a <= b: a, b, four steps (4a+b)/5 ... (a+4b)/5, then 0 and 255
    const int six[8] = {50, 150, 70, 90, 110, 130, 0, 255};
    bool okA = true, okB = true;
    for (int i = 0; i < 16; ++i) {
        okA = okA && g.px[(i / 4) * 8 + i % 4] == eight[i % 8];
        okB = okB && g.px[(i / 4) * 8 + 4 + i % 4] == six[i % 8];
    }
    Expect(okA, "BC4, a > b: the eight values, each texel at its place (row by row in the block)");
    Expect(okB, "BC4, a <= b: the six values, 0 and 255, in the second block to the right");

    std::vector<uint8_t> bad = t;
    bad[0] ^= 1;
    Expect(!DecodeBC4(bad.data(), bad.size(), &g, &why), "another magic: refused");
    bad = t;
    bad[4] = 71;
    Expect(!DecodeBC4(bad.data(), bad.size(), &g, &why), "another format (BC5): refused");
    Expect(!DecodeBC4(t.data(), t.size() - 1, &g, &why), "the last mip cut short: refused");
    Expect(!DecodeBC4(t.data(), 40, &g, &why), "shorter than a header: refused");
    bad = t;
    Put32(bad, 36 + 8, 16);   // mip 1 says 16 bytes, one block is 8
    Expect(!DecodeBC4(bad.data(), bad.size(), &g, &why), "a mip of the wrong size: refused");
    bad = t;
    Put32(bad, 40, 0);   // mip 0 starting inside the mip table
    Expect(!DecodeBC4(bad.data(), bad.size(), &g, &why), "a mip inside the header: refused");
    bad = t;
    Put32(bad, 8, 6);   // not a whole number of blocks
    Expect(!DecodeBC4(bad.data(), bad.size(), &g, &why), "a width not a multiple of 4: refused");
    Expect(!DecodeBC4(nullptr, 100, &g, &why), "no data: refused");
}

static void Strip() {
    const std::string pre = "{\"dependencies\":{\"particles\":[{\"path\":\"data/hd/vfx/particles/env/common/weather/Stars.particles\"}]},"
                            "\"type\":\"VisualDataDefinition\",\"name\":\"act2_arcane_day\",\"entities\":[{\"type\":\"Entity\",\"components\":"
                            "[{\"type\":\"VisualDataComponent\",\"name\":\"x\",";
    const std::string post = ",\"fog\":1}]}]}";
    const std::string json = pre + "\"screenspaceVfx\":\"data/hd/vfx/particles/env/common/weather/Stars.particles\"" + post;
    std::string out, why;
    Expect(StripWeather(json, &out, &why) && out == pre + "\"screenspaceVfx\":\"\"" + post,
           "the stars taken out: \"screenspaceVfx\":\"\", every other byte as it was (the dependency list too, as Bear's)");
    Expect(StripWeather(pre + "\"screenspaceVfx\" : \"DATA\\\\hd\\\\vfx\\\\particles\\\\env\\\\common\\\\weather\\\\STARS.particles\"" + post, &out, &why) &&
               out == pre + "\"screenspaceVfx\" : \"\"" + post,
           "spaces round the colon, back slashes, capitals: the same effect, taken out");
    Expect(!StripWeather(pre + "\"screenspaceVfx\":\"data/hd/vfx/particles/env/common/weather/rain.particles\"" + post, &out, &why),
           "another weather effect: refused (the game's own background kept)");
    Expect(!StripWeather(pre + "\"screenspaceVfx\":\"\"" + post, &out, &why), "already empty: refused (nothing of ours to take out)");
    Expect(!StripWeather(json + json, &out, &why), "two definitions: refused");
    Expect(!StripWeather(pre + post, &out, &why), "no screen-space effect: refused");
    std::string two = pre + "\"screenspaceVfx\":\"data/hd/vfx/particles/env/common/weather/Stars.particles\",\"screenspaceVfx\":\"\"" + post;
    Expect(!StripWeather(two, &out, &why), "two screen-space effects: refused");
    std::string comp2 = json;
    comp2.replace(comp2.find("\"Entity\""), 8, "\"VisualDataComponent\"");
    Expect(!StripWeather(comp2, &out, &why), "two visual components: refused");
    std::string def = json;
    def.replace(def.find("VisualDataDefinition"), 20, "ParticleDefinitionXY");
    Expect(!StripWeather(def, &out, &why), "not a visual definition: refused");
    Expect(!StripWeather(pre + "\"screenspaceVfx\":3" + post, &out, &why), "not a string: refused");
    Expect(!StripWeather(pre + "\"screenspaceVfx\":\"data\\u002fhd" + post, &out, &why), "an unknown escape: refused");
    Expect(!StripWeather(pre + "\"screenspaceVfx\":\"data/hd", &out, &why), "a string never closed: refused");
    Expect(!StripWeather(std::string(1024 * 1024 + 1, ' ') + json, &out, &why), "over 1 MiB: refused");
}

// Windows' own PNG decoder: RGBA rows.
static bool WicDecode(const std::vector<uint8_t>& png, int* w, int* h, std::vector<uint8_t>* rgba) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> s;
    if (FAILED(f->CreateStream(&s)) || FAILED(s->InitializeFromMemory((BYTE*)png.data(), (DWORD)png.size()))) return false;
    ComPtr<IWICBitmapDecoder> d;
    if (FAILED(f->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &d))) return false;
    ComPtr<IWICBitmapFrameDecode> fr;
    if (FAILED(d->GetFrame(0, &fr))) return false;
    ComPtr<IWICFormatConverter> c;
    if (FAILED(f->CreateFormatConverter(&c)) ||
        FAILED(c->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw = 0, uh = 0;
    c->GetSize(&uw, &uh);
    *w = (int)uw;
    *h = (int)uh;
    rgba->resize((size_t)uw * uh * 4);
    return SUCCEEDED(c->CopyPixels(nullptr, uw * 4, (UINT)rgba->size(), rgba->data()));
}

static void PngAndReceipt() {
    // small, and the stars' size (32 stored blocks of 64 KiB)
    for (int pass = 0; pass < 2; ++pass) {
        const int w = pass ? kStarsW : 3, h = pass ? kStarsH : 2;
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (size_t i = 0; i < px.size(); ++i) px[i] = (uint8_t)(i * 37 + i / 7);
        int rw = 0, rh = 0;
        std::vector<uint8_t> back;
        const bool ok = WicDecode(Png(w, h, px.data()), &rw, &rh, &back);
        char what[96];
        snprintf(what, sizeof what, "PNG %d x %d: Windows reads it back, every byte as written", w, h);
        Expect(ok && rw == w && rh == h && back == px, what);
    }
    Expect(Crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u, "CRC-32 of \"123456789\" = CBF43926");
    Expect(Hash("", 0) == "cbf29ce484222325" && Hash("a", 1) == "af63dc4c8601ec8c", "FNV-1a 64 of \"\" and \"a\"");

    const std::string r = Receipt("12345", {{"reshade-shaders/Textures/D2R_Arcane/stars.png", "00ff"}, {"mods/D2R-VR/D2R-VR.mpq/data/hd/env/vis/act2_arcane_day.json", "abcd"}});
    Expect(ReceiptValue(r, "game_build") == "12345" && ReceiptValue(r, "reshade-shaders/Textures/D2R_Arcane/stars.png") == "00ff" &&
               ReceiptValue(r, "mods/D2R-VR/D2R-VR.mpq/data/hd/env/vis/act2_arcane_day.json") == "abcd" && ReceiptValue(r, "nothing") == "",
           "the receipt: what was written, read back by name");
    const std::string ours = "ours", next = "next", theirs = "theirs";
    Expect(MayReplace(false, "", next, ""), "a visual file not there: written");
    Expect(MayReplace(true, next, next, ""), "already what we would write: fine");
    Expect(MayReplace(true, ours, next, Hash(ours.data(), ours.size())), "ours, unchanged since the receipt: replaced");
    Expect(!MayReplace(true, theirs, next, Hash(ours.data(), ours.size())), "changed by the player since: left as it is");
    Expect(!MayReplace(true, theirs, next, ""), "a file we never wrote: left as it is");
}

// ---------------------------------------------------------------- the effect on WARP

constexpr int kSW = 256, kSH = 128, kCW = 128, kCH = 128, kDirs = 32;

static const char* kPrelude =
    "Texture2D StarsT : register(t0);\n"
    "Texture2D CloudsT : register(t1);\n"
    "SamplerState Pt : register(s0);\n"
    "#define ArcaneStarsSmp 0\n"
    "#define ArcaneCloudsSmp 1\n"
    "float4 tex2Dlod(int which, float4 c) { return which == 0 ? StarsT.SampleLevel(Pt, c.xy, c.w) : CloudsT.SampleLevel(Pt, c.xy, c.w); }\n"
    "cbuffer C : register(b0) { float4 Dir[32]; };\n";
static const char* kMain =
    "\nvoid VS(uint id : SV_VertexID, out float4 pos : SV_Position) {\n"
    "    const float2 t = float2((id << 1) & 2, id & 2);\n"
    "    pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 PS(float4 p : SV_Position) : SV_Target { return float4(ArcaneSky(Dir[min((int)p.x, 31)].xyz), 1); }\n";

struct V3 { float x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static float Smooth(float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }

struct Tex { int w, h; std::vector<uint8_t> rgba; };
static float Texel(const Tex& t, float u, float v, int c) {   // point sampling, wrapped
    u -= std::floor(u);
    v -= std::floor(v);
    int x = (int)(u * t.w), y = (int)(v * t.h);
    x = x >= t.w ? t.w - 1 : x;
    y = y >= t.h ? t.h - 1 : y;
    return t.rgba[((size_t)y * t.w + x) * 4 + c] / 255.0f;
}
// ArcaneSky, line by line in C++
static V3 Mirror(const Tex& stars, const Tex& clouds, V3 d) {
    V3 wt = {std::pow(std::fabs(d.x), 8.0f), std::pow(std::fabs(d.y), 8.0f), std::pow(std::fabs(d.z), 8.0f)};
    const float sum = std::fmax(wt.x + wt.y + wt.z, 0.0001f);
    wt = wt * (1.0f / sum);
    auto tri = [&](const Tex& t, V3 p, int c) {
        return Texel(t, p.y, p.z, c) * wt.x + Texel(t, p.z, p.x, c) * wt.y + Texel(t, p.x, p.y, c) * wt.z;
    };
    const V3 sp = d * 1.8f + V3{0.13f, 0.37f, 0.61f};
    const float st = tri(stars, sp, 0) * 0.38f + tri(stars, sp, 1) * 0.55f + tri(stars, sp, 2) * 0.8f;
    const float broad = tri(clouds, d * 0.7f + V3{0.1f, 0.3f, 0.7f}, 0);
    const float detail = tri(clouds, d * 2.4f + V3{0.6f, 0.8f, 0.2f}, 0);
    const float density = Smooth(0.18f, 0.78f, broad) * (0.25f + 0.75f * detail);
    const float n = std::sqrt(0.6f * 0.6f + 0.2f * 0.2f + 0.7f * 0.7f);
    const float tint = Smooth(-0.8f, 0.8f, (d.x * 0.6f + d.y * 0.2f - d.z * 0.7f) / n);
    const V3 neb = V3{0.025f, 0.05f, 0.17f} * (1 - tint) + V3{0.015f, 0.14f, 0.11f} * tint;
    return V3{0.001f, 0.002f, 0.006f} + neb * density + V3{0.8f, 0.9f, 1.0f} * st;
}

static bool Warp(const std::string& fx) {
    const size_t b = fx.find("// ARCANE-BEGIN"), e = fx.find("// ARCANE-END");
    if (b == std::string::npos || e == std::string::npos || e < b) { printf("FAIL: the ARCANE marks are not in the effect\n"); return false; }
    const size_t bodyAt = fx.find('\n', b) + 1;
    const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
    ComPtr<ID3DBlob> vsb, psb, errors;
    if (FAILED(D3DCompile(src.data(), src.size(), "arcane.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors)) ||
        FAILED(D3DCompile(src.data(), src.size(), "arcane.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &errors))) {
        printf("FAIL: compile: %s\n", errors ? (const char*)errors->GetBufferPointer() : "?");
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

    // Smooth pictures (a neighbouring texel differs little: the mirror's and the GPU's choice at a texel's edge agree closely),
    // the stars' three layers each of their own.
    Tex stars{kSW, kSH, std::vector<uint8_t>((size_t)kSW * kSH * 4, 255)}, clouds{kCW, kCH, std::vector<uint8_t>((size_t)kCW * kCH * 4, 255)};
    const float tau = 6.2831853f;
    for (int y = 0; y < kSH; ++y)
        for (int x = 0; x < kSW; ++x)
            for (int c = 0; c < 3; ++c)
                stars.rgba[((size_t)y * kSW + x) * 4 + c] =
                    (uint8_t)std::lround(127.5f + 127.5f * std::sin(tau * (x * (c + 1)) / kSW) * std::cos(tau * (y * (3 - c)) / kSH));
    for (int y = 0; y < kCH; ++y)
        for (int x = 0; x < kCW; ++x) {
            const uint8_t v = (uint8_t)std::lround(127.5f + 127.5f * std::sin(tau * x / kCW + 1.0f) * std::sin(tau * y / kCH + 0.3f));
            for (int c = 0; c < 3; ++c) clouds.rgba[((size_t)y * kCW + x) * 4 + c] = v;
        }
    auto texture = [&](const Tex& t, ComPtr<ID3D11ShaderResourceView>* srv) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = t.w;
        td.Height = t.h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{t.rgba.data(), (UINT)t.w * 4, 0};
        ComPtr<ID3D11Texture2D> tex;
        return SUCCEEDED(dev->CreateTexture2D(&td, &init, &tex)) && SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, srv->GetAddressOf()));
    };
    ComPtr<ID3D11ShaderResourceView> starsV, cloudsV;
    if (!texture(stars, &starsV) || !texture(clouds, &cloudsV)) { printf("FAIL: no textures\n"); return false; }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> smp;
    dev->CreateSamplerState(&sd, &smp);

    // the six directions, then others between them (some on a plane's diagonal, where two planes share the weight)
    V3 dirs[kDirs] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0.7071068f, 0.7071068f, 0}, {0, -0.7071068f, 0.7071068f}};
    uint32_t seed = 12345;
    for (int i = 8; i < kDirs; ++i) {
        V3 v;
        float len;
        do {
            auto r = [&] { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) & 0xFFFF) / 32768.0f - 1.0f; };
            v = {r(), r(), r()};
            len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        } while (len < 0.2f || len > 1.0f);
        dirs[i] = v * (1.0f / len);
    }
    float cb[kDirs][4] = {};
    for (int i = 0; i < kDirs; ++i) { cb[i][0] = dirs[i].x; cb[i][1] = dirs[i].y; cb[i][2] = dirs[i].z; }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof cb;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{cb, 0, 0};
    ComPtr<ID3D11Buffer> buf;
    if (FAILED(dev->CreateBuffer(&bd, &init, &buf))) { printf("FAIL: no constant buffer\n"); return false; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kDirs;
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
    const D3D11_VIEWPORT vp{0, 0, (float)kDirs, 1, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {starsV.Get(), cloudsV.Get()};
    ctx->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* smps[1] = {smp.Get()};
    ctx->PSSetSamplers(0, 1, smps);
    ID3D11Buffer* cbs[1] = {buf.Get()};
    ctx->PSSetConstantBuffers(0, 1, cbs);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
    ctx->CopyResource(stage.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) { printf("FAIL: no read back\n"); return false; }
    std::vector<float> px(kDirs * 4);
    memcpy(px.data(), m.pData, px.size() * sizeof(float));
    ctx->Unmap(stage.Get(), 0);

    float worst = 0.0f, lo = 1e9f, hi = 0.0f;
    for (int i = 0; i < kDirs; ++i) {
        const V3 want = Mirror(stars, clouds, dirs[i]);
        const float got[3] = {px[i * 4], px[i * 4 + 1], px[i * 4 + 2]};
        const float w3[3] = {want.x, want.y, want.z};
        bool finite = true;
        float err = 0.0f;
        for (int c = 0; c < 3; ++c) {
            finite = finite && std::isfinite(got[c]);
            err = std::fmax(err, std::fabs(got[c] - w3[c]));
            lo = std::fmin(lo, got[c]);
            hi = std::fmax(hi, got[c]);
        }
        worst = std::fmax(worst, err);
        char what[128];
        snprintf(what, sizeof what, "WARP direction %d (%.2f %.2f %.2f): finite, as the mirror (off by %.4f)", i, dirs[i].x, dirs[i].y, dirs[i].z, err);
        Expect(finite && err < 0.03f, what);
    }
    Expect(lo >= 0.0009f && hi <= 2.0f, "the sky's colour: never below its own dark blue floor, never far above white");
    printf("the Arcane sky on WARP: %d directions, all six axes among them, as the mirror within %.4f (colours %.3f - %.3f)\n", kDirs, worst, lo, hi);
    return true;
}

// ---------------------------------------------------------------- the game's own files, when at hand

static void GameFiles(const std::string& art) {
    const std::string raw = art + "/raw/";
    if (ReadAll(raw + "t_fx_stars_far.texture").empty()) {
        printf("(no game files in %s: those checks skipped)\n", raw.c_str());
        return;
    }
    const char* layers[3] = {"far", "mid", "near"};
    Gray g[3], cloud;
    std::string why;
    for (int i = 0; i < 3; ++i) {
        const std::string t = ReadAll(raw + "t_fx_stars_" + layers[i] + ".texture");
        Expect(DecodeBC4((const uint8_t*)t.data(), t.size(), &g[i], &why) && g[i].w == kStarsW && g[i].h == kStarsH, "the game's star layer decodes, 1024 x 512");
        int w = 0, h = 0;
        std::vector<uint8_t> ref;
        const std::string png = ReadAll(art + "/1_game_t_fx_stars_" + layers[i] + ".png");
        if (!png.empty() && WicDecode(std::vector<uint8_t>(png.begin(), png.end()), &w, &h, &ref) && w == g[i].w && h == g[i].h) {
            bool same = true;
            for (size_t k = 0; k < g[i].px.size() && same; ++k) same = ref[k * 4] == g[i].px[k];
            char what[96];
            snprintf(what, sizeof what, "the %s layer = an independent decoder's picture, texel for texel", layers[i]);
            Expect(same, what);
        }
    }
    const std::string ct = ReadAll(raw + "t_fx_cloud_noise_03.texture");
    Expect(DecodeBC4((const uint8_t*)ct.data(), ct.size(), &cloud, &why) && cloud.w == kCloudsW && cloud.h == kCloudsH, "the game's cloud noise decodes, 512 x 512");
    {
        int w = 0, h = 0;
        std::vector<uint8_t> ref;
        const std::string png = ReadAll(art + "/2_game_t_fx_cloud_noise_03.png");
        if (!png.empty() && WicDecode(std::vector<uint8_t>(png.begin(), png.end()), &w, &h, &ref) && w == cloud.w && h == cloud.h) {
            bool same = true;
            for (size_t k = 0; k < cloud.px.size() && same; ++k) same = ref[k * 4] == cloud.px[k];
            Expect(same, "the cloud noise = an independent decoder's picture, texel for texel");
        }
    }
    std::vector<uint8_t> rgba;
    Expect(StarsRGBA(g[0], g[1], g[2], &rgba, &why) && rgba[0] == g[0].px[0] && rgba[1] == g[1].px[0] && rgba[2] == g[2].px[0] && rgba[3] == 255,
           "the three layers: far red, mid green, near blue, alpha 255");
    Expect(!StarsRGBA(g[0], g[1], cloud, &rgba, &why), "a layer of another size: refused");
    Expect(CloudsRGBA(cloud, &rgba, &why) && rgba[0] == cloud.px[0] && rgba[1] == cloud.px[0] && rgba[2] == cloud.px[0] && rgba[3] == 255, "the clouds grey, alpha 255");
    for (int p = 0; p < 7; ++p) {
        const std::string name = std::string("act2_arcane_") + kPeriods[p] + ".json";
        const std::string json = ReadAll(raw + name);
        std::string out;
        const std::string from = "\"screenspaceVfx\":\"data/hd/vfx/particles/env/common/weather/Stars.particles\"";
        std::string want = json;
        const size_t at = want.find(from);
        if (at != std::string::npos) want.replace(at, from.size(), "\"screenspaceVfx\":\"\"");
        char what[96];
        snprintf(what, sizeof what, "the game's %s: the stars taken out, nothing else", name.c_str());
        Expect(!json.empty() && StripWeather(json, &out, &why) && out == want && out.size() + from.size() - 19 == json.size(), what);
    }
    printf("the game's own files: three star layers, the cloud noise and seven visual files checked\n");
}

int main(int argc, char** argv) {
    std::string fxPath = ARCANE_FX_PATH, art = ARCANE_ART_DIR;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--fx") == 0) fxPath = argv[i + 1];
        if (strcmp(argv[i], "--art") == 0) art = argv[i + 1];
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Bc4();
    Strip();
    PngAndReceipt();
    const std::string fx = ReadAll(fxPath);
    Expect(!fx.empty(), "the effect reads");
    if (!fx.empty()) {
        Expect(fx.find("#ifndef D2R_ARCANE_SKY\r\n #define D2R_ARCANE_SKY 0") != std::string::npos ||
                   fx.find("#ifndef D2R_ARCANE_SKY\n #define D2R_ARCANE_SKY 0") != std::string::npos,
               "the effect: no Arcane textures unless vrcam says they are there (D2R_ARCANE_SKY 0 by default)");
        const size_t sky = fx.find("float3 Sky(float3 dir)");
        Expect(sky != std::string::npos && fx.find("if (ArcaneSkyOn) return ArcaneSky(dir);", sky) < fx.find("if (SkyTex > 0.5)", sky),
               "Sky(): the Arcane sky first, before any painted one");
        if (!Warp(fx)) ++g_fail;
    }
    GameFiles(art);
    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("arcane sky: all checks passed\n");
    return 0;
}
