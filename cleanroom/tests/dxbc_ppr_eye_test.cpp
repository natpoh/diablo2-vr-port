// dxbc_ppr_eye_test - shared/dxbc_ppr_eye.h: the game's reflections (pixel projected reflections) put in the right place for each eye.
//   1. a compute shader of our own with the game's projection pass's math (cs_ppr_projection_c, read from its disassembly: the world
//      point from the linear depth by cb3's inverse view and the inverse projection's diagonal, mirrored over a water plane of cb13,
//      found along the ray from the eye cb10[1], put in the picture by cb3[21..24]) is found and patched; its checksum holds before and
//      after; D3D11 (WARP) runs both on one point of a scene:
//        - the left eye (its own camera): the game's and the patched shader write the reflection where the left eye sees it;
//        - the right eye as the replay gives it - FOLDED, as vr/render/depth/depth_fix.cpp does ([stereo] replay_proj 3: the view and
//          its inverse the left eye's, the eyes' distance in the projection, the eye cb10[1] moved): the game's shader writes the
//          reflection where an eye TWICE the eyes' distance away sees it (the floating reflection, zbiromir 2026-11-03), the patched
//          one where the right eye sees it;
//   2. a pixel shader with the resolve's shape (ps_ppr_reflection_p) is found and patched, D3D11 takes it;
//   3. shaders without the shape are left alone (cb3[12] read twice, no UAV, cb3[12] in an add);
//   dxbc_ppr_eye_test [<folder of the game's containers, *.dxbc>]: each one's checksum holds, the reflection passes are found and
//      patched (cs_ppr_projection_c, ps_ppr_reflection_p), the others not; D3D11 takes every patched one.
#include "dxbc_ppr_eye.h"
#include "ppr_standins.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// cb3[12] read twice (the shape is not the reflection's: left alone)
static const char* kTwiceCs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[91]; }
Texture2D<float> depth : register(t0);
RWStructuredBuffer<uint> outBuf : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const float d = depth.Load(int3(id.x, 0, 0)) * c10[53].x + c10[53].z;
    const float valid = d < c10[53].z + c10[53].x ? 1.0 : 0.0;
    const float3 world = c3[9].xyz * d + c3[11].xyz * valid + c3[12].xyz * valid;
    const float3 e = world - c10[1].xyz + c13[4].xyz;
    outBuf[id.x] = asuint(dot(e, c3[12].xyz));
}
)";
// no UAV
static const char* kNoUavPs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[91]; }
Texture2D<float> depth : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    const float d = depth.Load(int3(pos.xy, 0)) * c10[53].x + c10[53].z;
    const float valid = d < c10[53].z + c10[53].x ? 1.0 : 0.0;
    const float3 world = c3[9].xyz * d + c3[10].xyz * pos.x + c3[11].xyz * pos.y + c3[12].xyz * valid;
    return float4(world - c10[1].xyz + c13[4].xyz, 1);
}
)";

static std::vector<uint8_t> Compile(const char* src, const char* target) {
    ID3DBlob *b = nullptr, *e = nullptr;
    if (FAILED(D3DCompile(src, std::strlen(src), "test", nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
        std::printf("compile failed: %s\n", e ? (const char*)e->GetBufferPointer() : "?");
        std::exit(2);
    }
    std::vector<uint8_t> v((const uint8_t*)b->GetBufferPointer(), (const uint8_t*)b->GetBufferPointer() + b->GetBufferSize());
    b->Release();
    if (e) e->Release();
    return v;
}

// ---- the scene, in doubles (row-major, v * M, as the game's camera record) ---------------------------------------------------------
struct M4 { double m[16]; };
static M4 Mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { double s = 0; for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j]; r.m[i * 4 + j] = s; }
    return r;
}
static bool Inv(const M4& M, M4* out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { a[r][c] = M.m[r * 4 + c]; a[r][4 + c] = r == c; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-12) return false;
        for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[piv][k]);
        const double d = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= d;
        for (int r = 0; r < 4; ++r) if (r != c) { const double m = a[r][c]; for (int k = 0; k < 8; ++k) a[r][k] -= m * a[c][k]; }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out->m[r * 4 + c] = a[r][4 + c];
    return true;
}
struct V3 { double x, y, z; };
static V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 Sc(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }

constexpr int kW = 256, kH = 256;
// the eyes look ahead along -z, pitched down by `pitch` (y up); the camera's x axis along the world's x
struct Cam { M4 view, invView, proj, invProj; V3 eye; };
static Cam Camera(V3 eye, double pitch) {
    const double c = std::cos(pitch), s = std::sin(pitch);
    // camera axes in the world (rows of the inverse view): right, up, back
    const V3 right{1, 0, 0}, up{0, c, -s}, back{0, s, c};
    Cam k{};
    const V3 axes[3] = {right, up, back};
    for (int r = 0; r < 3; ++r) { k.invView.m[r * 4 + 0] = axes[r].x; k.invView.m[r * 4 + 1] = axes[r].y; k.invView.m[r * 4 + 2] = axes[r].z; }
    k.invView.m[12] = eye.x; k.invView.m[13] = eye.y; k.invView.m[14] = eye.z; k.invView.m[15] = 1;
    Inv(k.invView, &k.view);
    const double sx = 1.0, sy = 1.0, zn = 0.05, zf = 1000.0;   // 90 degrees, a square picture
    k.proj = M4{{sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0}};
    Inv(k.proj, &k.invProj);
    k.eye = eye;
    return k;
}
static bool Pixel(const M4& vp, V3 p, double* px, double* py) {
    const double x = p.x * vp.m[0] + p.y * vp.m[4] + p.z * vp.m[8] + vp.m[12];
    const double y = p.x * vp.m[1] + p.y * vp.m[5] + p.z * vp.m[9] + vp.m[13];
    const double w = p.x * vp.m[3] + p.y * vp.m[7] + p.z * vp.m[11] + vp.m[15];
    if (w <= 0) return false;
    *px = (x / w + 1) * 0.5 * kW;
    *py = (1 - y / w) * 0.5 * kH;
    return true;
}

struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
};
static Gpu Warp() {
    Gpu g;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g.dev, nullptr, &g.ctx))) {
        std::printf("no WARP device\n");
        std::exit(2);
    }
    return g;
}
static ID3D11Buffer* Cb(Gpu& g, const std::vector<float>& f) {
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = (UINT)(f.size() * 4);
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA s{f.data(), 0, 0};
    ID3D11Buffer* b = nullptr;
    g.dev->CreateBuffer(&d, &s, &b);
    return b;
}

// One eye's run of a projection shader: the pixel the reflection of the scene's one point is written at (-1: none)
struct Eye {
    M4 invView, invProj, vp;   // what cb3 holds (the replay's fold for the right eye)
    V3 eye;                    // what cb10[1] holds
};
static bool Run(Gpu& g, const std::vector<uint8_t>& cs, const Eye& e, int srcX, int srcY, float raw, double range, double start, int* outX, int* outY) {
    ID3D11ComputeShader* sh = nullptr;
    if (FAILED(g.dev->CreateComputeShader(cs.data(), cs.size(), nullptr, &sh))) return false;
    std::vector<float> c3(38 * 4, 0.0f), c10(54 * 4, 0.0f), c13(91 * 4, 0.0f);
    c3[4] = kW; c3[5] = kH;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) c3[(9 + r) * 4 + c] = (float)e.invView.m[r * 4 + c];
    c3[17 * 4 + 0] = (float)e.invProj.m[0];
    c3[18 * 4 + 1] = (float)e.invProj.m[5];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) c3[(21 + r) * 4 + c] = (float)e.vp.m[r * 4 + c];
    c10[4] = (float)e.eye.x; c10[5] = (float)e.eye.y; c10[6] = (float)e.eye.z;
    c10[53 * 4 + 0] = (float)range; c10[53 * 4 + 1] = (float)(1.0 / range); c10[53 * 4 + 2] = (float)start;
    // the water: the plane y = 0, its axes x, z (in the columns of rows 0..2: the normal in .z), centre 0, half size 1000
    c13[0 * 4 + 0] = 1; c13[1 * 4 + 1] = 0; c13[2 * 4 + 1] = 1;   // x axis (1,0,0) in .x; z axis (0,0,1) in .y
    c13[1 * 4 + 2] = 1;                                           // the normal (0,1,0) in .z
    c13[5 * 4 + 0] = 2000; c13[5 * 4 + 1] = 2000;
    c13[90 * 4 + 0] = 1;
    ID3D11Buffer *b3 = Cb(g, c3), *b10 = Cb(g, c10), *b13 = Cb(g, c13);
    // the depth: the one point at its pixel
    std::vector<float> dep(kW * kH, 0.0f);
    dep[srcY * kW + srcX] = raw;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA ts{dep.data(), kW * 4, 0};
    ID3D11Texture2D* tex = nullptr;
    g.dev->CreateTexture2D(&td, &ts, &tex);
    ID3D11ShaderResourceView* srv = nullptr;
    g.dev->CreateShaderResourceView(tex, nullptr, &srv);
    std::vector<uint32_t> init(kW * kH, 0xFFFFFFFFu);
    D3D11_BUFFER_DESC ud{};
    ud.ByteWidth = kW * kH * 4; ud.Usage = D3D11_USAGE_DEFAULT; ud.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ud.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; ud.StructureByteStride = 4;
    D3D11_SUBRESOURCE_DATA us{init.data(), 0, 0};
    ID3D11Buffer* ub = nullptr;
    g.dev->CreateBuffer(&ud, &us, &ub);
    ID3D11UnorderedAccessView* uav = nullptr;
    g.dev->CreateUnorderedAccessView(ub, nullptr, &uav);
    D3D11_BUFFER_DESC rd = ud;
    rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* rb = nullptr;
    g.dev->CreateBuffer(&rd, nullptr, &rb);
    ID3D11Buffer* cbs[14] = {};
    cbs[3] = b3; cbs[10] = b10; cbs[13] = b13;
    g.ctx->CSSetConstantBuffers(0, 14, cbs);
    g.ctx->CSSetShaderResources(0, 1, &srv);
    g.ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    g.ctx->CSSetShader(sh, nullptr, 0);
    g.ctx->Dispatch((kW * kH + 63) / 64, 1, 1);
    g.ctx->CopyResource(rb, ub);
    D3D11_MAPPED_SUBRESOURCE m{};
    *outX = *outY = -1;
    if (SUCCEEDED(g.ctx->Map(rb, 0, D3D11_MAP_READ, 0, &m))) {
        const uint32_t* u = (const uint32_t*)m.pData;
        for (int i = 0; i < kW * kH; ++i)
            if (u[i] == (uint32_t)(srcY * kW + srcX)) { *outX = i % kW; *outY = i / kW; break; }
        g.ctx->Unmap(rb, 0);
    }
    for (IUnknown* x : {(IUnknown*)sh, (IUnknown*)b3, (IUnknown*)b10, (IUnknown*)b13, (IUnknown*)tex, (IUnknown*)srv, (IUnknown*)ub, (IUnknown*)uav,
                        (IUnknown*)rb})
        if (x) x->Release();
    return true;
}

static void Folder(const char* dir) {
    Gpu g = Warp();
    WIN32_FIND_DATAA fd{};
    const std::string pat = std::string(dir) + "\\*.dxbc";
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { std::printf("no *.dxbc in %s\n", dir); ++g_fail; return; }
    int found = 0;
    do {
        const std::string path = std::string(dir) + "\\" + fd.cFileName;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> v;
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
        std::fclose(f);
        CHECK(dxbcfog::ChecksumHolds(v.data(), v.size()));
        std::string why;
        dxbcppr::Found fo;
        const std::vector<uint8_t> p = dxbcppr::Patch(v.data(), v.size(), &why, &fo);
        const bool ppr = std::strstr(fd.cFileName, "ppr_projection") || std::strstr(fd.cFileName, "ppr_reflection");
        std::printf("  %-34s %s%s\n", fd.cFileName, p.empty() ? "left alone: " : "PATCHED", p.empty() ? why.c_str() : "");
        CHECK(p.empty() != ppr);
        if (p.empty()) continue;
        ++found;
        CHECK(dxbcfog::ChecksumHolds(p.data(), p.size()));
        HRESULT hr;
        if (fo.compute) {
            ID3D11ComputeShader* s = nullptr;
            hr = g.dev->CreateComputeShader(p.data(), p.size(), nullptr, &s);
            if (s) s->Release();
        } else {
            ID3D11PixelShader* s = nullptr;
            hr = g.dev->CreatePixelShader(p.data(), p.size(), nullptr, &s);
            if (s) s->Release();
        }
        CHECK(SUCCEEDED(hr));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::printf("  %d patched\n", found);
}

int main(int argc, char** argv) {
    // ---- 1. the projection pass ----
    const std::vector<uint8_t> game = Compile(pprstandin::kProjCs, "cs_5_0");
    CHECK(dxbcfog::ChecksumHolds(game.data(), game.size()));
    std::string why;
    dxbcppr::Found fo;
    const std::vector<uint8_t> ours = dxbcppr::Patch(game.data(), game.size(), &why, &fo);
    if (ours.empty()) std::printf("  the stand-in was not found: %s\n", why.c_str());
    CHECK(!ours.empty() && fo.compute);
    CHECK(ours.size() == game.size() && dxbcfog::ChecksumHolds(ours.data(), ours.size()));
    {   // only the operand's two indices differ
        int diff = 0;
        for (size_t i = 20; i < game.size() && !ours.empty(); ++i) diff += game[i] != ours[i];
        CHECK(diff >= 1 && diff <= 3);
        dxbcppr::Found again;
        CHECK(!dxbcppr::Find(ours.data(), ours.size(), &again));   // patched: no cb3[12] left - never patched twice
    }

    Gpu g = Warp();
    // the eyes 1.6 m (in the game's units: 1.6) above the water, looking ahead and 35 degrees down; the eyes' distance as the replay's
    // (shift 0.2371: eye half 0.1185 x 2, a player's log 2026-11-04)
    const double shift = 0.2371, pitch = 35.0 * 3.14159265358979 / 180.0;
    const Cam left = Camera({0, 1.6, 0}, pitch);
    const Cam right = Camera({shift, 1.6, 0}, pitch);
    // a point 0.5 above the water, 3 ahead, a little to the right (a monster's body over a puddle): seen by the right eye at its pixel
    const V3 point{0.3, 0.5, -3.0};
    const double range = 1000.0, start = 0.0;
    const M4 vpL = Mul(left.view, left.proj), vpR = Mul(right.view, right.proj);
    auto viewOf = [](const Cam& k, V3 p) {   // view space (row-major, v * M)
        return V3{p.x * k.view.m[0] + p.y * k.view.m[4] + p.z * k.view.m[8] + k.view.m[12],
                  p.x * k.view.m[1] + p.y * k.view.m[5] + p.z * k.view.m[9] + k.view.m[13],
                  p.x * k.view.m[2] + p.y * k.view.m[6] + p.z * k.view.m[10] + k.view.m[14]};
    };
    auto runEye = [&](const Cam& k, const Eye& e, const std::vector<uint8_t>& cs, int* ox, int* oy, int* sx, int* sy, V3* drawnAt) {
        double fx, fy;
        Pixel(Mul(k.view, k.proj), point, &fx, &fy);
        *sx = (int)fx; *sy = (int)fy;
        // the point the shader sees at that pixel's centre and the eye's linear depth (parallel eyes: -z of the eye's view space)
        const double d = -viewOf(k, point).z;
        const double nx = (*sx + 0.5) / kW * 2 - 1, ny = (*sy + 0.5) / kH * 2 - 1;
        const V3 vv{nx * d / k.proj.m[0], -ny * d / k.proj.m[5], -d};
        *drawnAt = Add(Add(Add(Sc({k.invView.m[0], k.invView.m[1], k.invView.m[2]}, vv.x), Sc({k.invView.m[4], k.invView.m[5], k.invView.m[6]}, vv.y)),
                           Sc({k.invView.m[8], k.invView.m[9], k.invView.m[10]}, vv.z)), k.eye);
        Run(g, cs, e, *sx, *sy, (float)((d - start) / range), range, start, ox, oy);
    };
    auto mirror = [](V3 p) { return V3{p.x, -p.y, p.z}; };
    auto near1 = [](int x, int y, double fx, double fy) { return x >= 0 && std::fabs(x + 0.5 - fx) <= 1.01 && std::fabs(y + 0.5 - fy) <= 1.01; };

    // the left eye: its own camera, cb10[1] its eye - the game's and ours the same
    {
        const Eye e{left.invView, left.invProj, vpL, left.eye};
        int gx, gy, ox, oy, sx, sy;
        V3 at;
        runEye(left, e, game, &gx, &gy, &sx, &sy, &at);
        runEye(left, e, ours, &ox, &oy, &sx, &sy, &at);
        double ex, ey;
        Pixel(vpL, mirror(at), &ex, &ey);
        std::printf("  left eye: the reflection expected at %.1f %.1f; the game's shader %d %d, ours %d %d\n", ex, ey, gx, gy, ox, oy);
        CHECK(near1(gx, gy, ex, ey));
        CHECK(gx == ox && gy == oy);
    }
    // the right eye as the replay folds it (vr/render/depth/depth_fix.cpp PatchKnown, replay_proj 3): view and inverse view the left
    // eye's; Pr = T(-shift) * P (row 3: P[12 + c] - shift * P[c]) and its inverse; view * projection = V_left * Pr; the eye cb10[1] moved
    {
        M4 pr = left.proj, pri;
        for (int c = 0; c < 4; ++c) pr.m[12 + c] = left.proj.m[12 + c] - shift * left.proj.m[c];
        Inv(pr, &pri);
        const Eye e{left.invView, pri, Mul(left.view, pr), right.eye};
        // (the fold gives the right eye's own view * projection)
        double a, b, c2, d2;
        Pixel(e.vp, point, &a, &b);
        Pixel(vpR, point, &c2, &d2);
        CHECK(std::fabs(a - c2) < 1e-6 && std::fabs(b - d2) < 1e-6);
        int gx, gy, ox, oy, sx, sy;
        V3 at;
        runEye(right, e, game, &gx, &gy, &sx, &sy, &at);
        runEye(right, e, ours, &ox, &oy, &sx, &sy, &at);
        double ex, ey, bx, by, twoX, twoY;
        Pixel(vpR, mirror(at), &ex, &ey);                                            // where the right eye sees the reflection
        Pixel(vpR, mirror(Add(at, V3{-shift, 0, 0})), &bx, &by);                     // the point one eye distance to the side, mirrored
        const Cam far2 = Camera({2 * shift, 1.6, 0}, pitch);
        Pixel(Mul(far2.view, far2.proj), mirror(at), &twoX, &twoY);                  // = the reflection seen from twice the eyes' distance
        std::printf("  right eye (folded): expected at %.1f %.1f; the game's shader %d %d (one seen from 2 eye distances: %.1f %.1f); ours %d %d\n",
                    ex, ey, gx, gy, twoX, twoY, ox, oy);
        CHECK(std::fabs(bx - twoX) < 1e-6 && std::fabs(by - twoY) < 1e-6);
        CHECK(std::fabs(ex - twoX) > 3.0);                 // the scene tells them apart
        CHECK(near1(gx, gy, twoX, twoY));                  // the game's: the floating reflection
        CHECK(!near1(gx, gy, ex, ey));
        CHECK(near1(ox, oy, ex, ey));                      // ours: where the right eye sees it
        // the depth it stands at: the left eye's and the right eye's pixel of the reflection - their disparity, theirs and ours
        double lx, ly;
        Pixel(vpL, mirror(at), &lx, &ly);
        std::printf("  disparity of the reflection: right %.1f px, the game's %.1f px (x%.2f)\n", lx - ex, lx - (gx + 0.5), (lx - (gx + 0.5)) / (lx - ex));
    }

    // ---- 2. the resolve's shape ----
    {
        const std::vector<uint8_t> ps = Compile(pprstandin::kResolvePs, "ps_5_0");
        dxbcppr::Found f2;
        const std::vector<uint8_t> p2 = dxbcppr::Patch(ps.data(), ps.size(), &why, &f2);
        if (p2.empty()) std::printf("  the resolve stand-in was not found: %s\n", why.c_str());
        CHECK(!p2.empty() && !f2.compute);
        CHECK(!p2.empty() && dxbcfog::ChecksumHolds(p2.data(), p2.size()));
        ID3D11PixelShader* s = nullptr;
        CHECK(!p2.empty() && SUCCEEDED(g.dev->CreatePixelShader(p2.data(), p2.size(), nullptr, &s)));
        if (s) s->Release();
    }
    // ---- 3. not the shape: left alone ----
    {
        const std::vector<uint8_t> a = Compile(kTwiceCs, "cs_5_0"), b = Compile(kNoUavPs, "ps_5_0");
        dxbcppr::Found f3;
        std::string w1, w2;
        CHECK(!dxbcppr::Find(a.data(), a.size(), &f3, &w1));
        CHECK(!dxbcppr::Find(b.data(), b.size(), &f3, &w2));
        std::printf("  left alone: %s; %s\n", w1.c_str(), w2.c_str());
        CHECK(dxbcppr::Patch(a.data(), a.size()).empty());
    }
    if (argc > 1) Folder(argv[1]);

    g.ctx->Release();
    g.dev->Release();
    std::printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "OK", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
