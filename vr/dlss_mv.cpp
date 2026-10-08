// Motion vectors for DLSS with real stereo (2026-10-08).
//
// With the eyes drawn by turns, the game's motion vectors are made against the
// previous frame - the OTHER eye's camera - so they carry the parallax between
// the eyes as if it were motion (a capture: x shifts growing towards the
// ground in front, where the eyes' views differ most). vrcam gives each eye a
// DLSS instance of its own (vrcam.cpp, dlsseyes); for its history to line up,
// the vectors must be against the same eye's previous frame, two frames back.
//
// Here, just before the game's DLSS evaluation, a compute pass on the game's
// own command list reads the depth and the game's vectors and works out, per
// pixel, the camera's part of the motion two ways from vrcam's matrices: the
// one the game made (this frame against the last, the other eye) and the one
// wanted (against two frames back, this eye). Mode 1 only checks: it fits the
// game's vectors to the first (scale and how much is left over) and logs it,
// for two guesses of how many frames the renderer runs behind vrcam. Mode 2
// adds the difference to the game's vectors - the objects' own motion stays -
// and hands DLSS that texture instead.
//
// What the checks found: the "Depth" the game hands DLSS (R16_UNORM) is no
// depth but a mask - 1 over most of the scene, 0 near the bottom - and the
// game's vectors where it is 1 ("far", most of the scene) are exactly the
// step of the jitter from the render before, with nothing of the camera in
// them - not even the head turning 15-25 px a render. Where it is 0 ("near",
// the ground at the feet) they carry the camera's motion against the other
// eye, parallax and all.
// So the fix (mode 2), against the same eye two renders back:
//  far:  the game's less its jitter step, plus the same eye's jitter step and
//        the camera's turn (which needs no depth);
//  near: the depth is read off the parallax between the eyes in the game's
//        vector, and the camera's part redone against the same eye with it.

#include "dlss_mv.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace dlssmv {
namespace {

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// ---- the NGX parameter set, as the SDK declares it (nvsdk_ngx_params.h); the
// same declaration under the same compiler gives the same table of calls.
struct NgxParameter {
    virtual void Set(const char* name, unsigned long long v) = 0;
    virtual void Set(const char* name, float v) = 0;
    virtual void Set(const char* name, double v) = 0;
    virtual void Set(const char* name, unsigned int v) = 0;
    virtual void Set(const char* name, int v) = 0;
    virtual void Set(const char* name, struct ID3D11Resource* v) = 0;
    virtual void Set(const char* name, ID3D12Resource* v) = 0;
    virtual void Set(const char* name, void* v) = 0;
    virtual int Get(const char* name, unsigned long long* v) const = 0;
    virtual int Get(const char* name, float* v) const = 0;
    virtual int Get(const char* name, double* v) const = 0;
    virtual int Get(const char* name, unsigned int* v) const = 0;
    virtual int Get(const char* name, int* v) const = 0;
    virtual int Get(const char* name, struct ID3D11Resource** v) const = 0;
    virtual int Get(const char* name, ID3D12Resource** v) const = 0;
    virtual int Get(const char* name, void** v) const = 0;
    virtual void Reset() = 0;
};
bool Ok(int r) { return (r & 0xFFF00000) != 0xBAD00000; }

// ---- the views vrcam handed the game, a frame each
struct Frame { float view[16], proj[16]; int eye; uint32_t n; };
constexpr int kFrames = 8;
std::mutex g_framesLock;
Frame g_frames[kFrames];
uint32_t g_frameCount = 0;
Frame g_pending;          // the frame being built now (vrcam's latest view), not yet recorded
bool g_pendingOk = false;
// The view each DLSS evaluation came with (the latest vrcam handed over by then):
// with a pair of renders per game frame the game frames' list does not take
// turns between the eyes, the evaluations do.
Frame g_evals[kFrames];
uint32_t g_evalCount = 0;

// row vectors (p * M): A * B
void Mul(const float* a, const float* b, float* o) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    memcpy(o, r, sizeof r);
}
bool Inverse(const float* m, float* out) {
    double inv[16], det;
    const double* d = nullptr;
    double a[16];
    for (int i = 0; i < 16; ++i) a[i] = m[i];
    d = a;
    inv[0] = d[5] * d[10] * d[15] - d[5] * d[11] * d[14] - d[9] * d[6] * d[15] + d[9] * d[7] * d[14] + d[13] * d[6] * d[11] - d[13] * d[7] * d[10];
    inv[4] = -d[4] * d[10] * d[15] + d[4] * d[11] * d[14] + d[8] * d[6] * d[15] - d[8] * d[7] * d[14] - d[12] * d[6] * d[11] + d[12] * d[7] * d[10];
    inv[8] = d[4] * d[9] * d[15] - d[4] * d[11] * d[13] - d[8] * d[5] * d[15] + d[8] * d[7] * d[13] + d[12] * d[5] * d[11] - d[12] * d[7] * d[9];
    inv[12] = -d[4] * d[9] * d[14] + d[4] * d[10] * d[13] + d[8] * d[5] * d[14] - d[8] * d[6] * d[13] - d[12] * d[5] * d[10] + d[12] * d[6] * d[9];
    inv[1] = -d[1] * d[10] * d[15] + d[1] * d[11] * d[14] + d[9] * d[2] * d[15] - d[9] * d[3] * d[14] - d[13] * d[2] * d[11] + d[13] * d[3] * d[10];
    inv[5] = d[0] * d[10] * d[15] - d[0] * d[11] * d[14] - d[8] * d[2] * d[15] + d[8] * d[3] * d[14] + d[12] * d[2] * d[11] - d[12] * d[3] * d[10];
    inv[9] = -d[0] * d[9] * d[15] + d[0] * d[11] * d[13] + d[8] * d[1] * d[15] - d[8] * d[3] * d[13] - d[12] * d[1] * d[11] + d[12] * d[3] * d[9];
    inv[13] = d[0] * d[9] * d[14] - d[0] * d[10] * d[13] - d[8] * d[1] * d[14] + d[8] * d[2] * d[13] + d[12] * d[1] * d[10] - d[12] * d[2] * d[9];
    inv[2] = d[1] * d[6] * d[15] - d[1] * d[7] * d[14] - d[5] * d[2] * d[15] + d[5] * d[3] * d[14] + d[13] * d[2] * d[7] - d[13] * d[3] * d[6];
    inv[6] = -d[0] * d[6] * d[15] + d[0] * d[7] * d[14] + d[4] * d[2] * d[15] - d[4] * d[3] * d[14] - d[12] * d[2] * d[7] + d[12] * d[3] * d[6];
    inv[10] = d[0] * d[5] * d[15] - d[0] * d[7] * d[13] - d[4] * d[1] * d[15] + d[4] * d[3] * d[13] + d[12] * d[1] * d[7] - d[12] * d[3] * d[5];
    inv[14] = -d[0] * d[5] * d[14] + d[0] * d[6] * d[13] + d[4] * d[1] * d[14] - d[4] * d[2] * d[13] - d[12] * d[1] * d[6] + d[12] * d[2] * d[5];
    inv[3] = -d[1] * d[6] * d[11] + d[1] * d[7] * d[10] + d[5] * d[2] * d[11] - d[5] * d[3] * d[10] - d[9] * d[2] * d[7] + d[9] * d[3] * d[6];
    inv[7] = d[0] * d[6] * d[11] - d[0] * d[7] * d[10] - d[4] * d[2] * d[11] + d[4] * d[3] * d[10] + d[8] * d[2] * d[7] - d[8] * d[3] * d[6];
    inv[11] = -d[0] * d[5] * d[11] + d[0] * d[7] * d[9] + d[4] * d[1] * d[11] - d[4] * d[3] * d[9] - d[8] * d[1] * d[7] + d[8] * d[3] * d[5];
    inv[15] = d[0] * d[5] * d[10] - d[0] * d[6] * d[9] - d[4] * d[1] * d[10] + d[4] * d[2] * d[9] + d[8] * d[1] * d[6] - d[8] * d[2] * d[5];
    det = d[0] * inv[0] + d[1] * inv[4] + d[2] * inv[8] + d[3] * inv[12];
    if (std::fabs(det) < 1e-20) return false;
    for (int i = 0; i < 16; ++i) out[i] = (float)(inv[i] / det);
    return true;
}

// ---- the pass
const char* kShader = R"(
cbuffer C : register(b0) {
    float4x4 InvCur[3];      // per guess: clip -> world direction, this frame (the view's turn only)
    float4x4 PrevOther[3];   // world direction -> clip, the frame before (the other eye)
    float4x4 PrevSame[3];    // world direction -> clip, two frames before (this eye)
    uint2 Size;              // the scene's part of the textures, pixels
    uint Mode;               // 1 check, 2 fix
    uint Use;                // the guess the fix uses
    float Scale;             // the camera's part of a vector = Scale * (previous uv - uv): [render] dlss_mv_sign
    uint GroupsX;            // groups across, for each group's place in the stats
    float FarTurn;           // how much of the camera's turn the far part gets ([render] dlss_mv_far_turn)
    uint Fix;                // 1 the near part redone ([render] dlss_mv_near), 2 the jitter's step this eye's ([render] dlss_mv_jitter)
    float4 Jit;              // the jitter's step in uv: from the render before (the game's), from this eye's last (wanted)
};
Texture2D<float> Depth : register(t0);
Texture2D<float2> Game : register(t1);
RWTexture2D<float2> Out : register(u0);
RWByteAddressBuffer Stats : register(u1);
#define K 27
groupshared float s[K][256];

// where the point on this pixel's ray at clip depth z was in an earlier view
// (this camera at the origin, the earlier ones placed against it)
float2 Reproject(float2 uv, float z, float4x4 invCur, float4x4 prev) {
    float4 w = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0), invCur);
    float4 p = mul(float4(w.xyz / w.w, 1.0), prev);
    if (abs(p.w) < 1e-12) return 0.0;
    float2 n = p.xy / p.w;
    return float2(n.x * 0.5 + 0.5, 0.5 - n.y * 0.5) - uv;
}
// reversed depth: far away (the camera's turn alone), and a probe nearer -
// the camera's move shows in proportion to the depth value
static const float kFar = 1e-5;
static const float kProbe = 0.01;
float2 Turn(float2 uv, float4x4 invCur, float4x4 prev) { return Reproject(uv, kFar, invCur, prev); }
float2 Move(float2 uv, float2 turn, float4x4 invCur, float4x4 prev) { return (Reproject(uv, kProbe, invCur, prev) - turn) / (kProbe - kFar); }

[numthreads(16, 16, 1)]
void Main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
    const bool inside = id.x < Size.x && id.y < Size.y;
    const float d = inside ? Depth[id.xy] : 0.0;
    const float2 g = inside ? Game[id.xy] : 0.0;
    const float2 uv = (float2(id.xy) + 0.5) / float2(Size);
    const bool far = d >= 0.99998;   // where the game's vectors are the camera's turn (and the jitter) alone
    float2 m[3] = {float2(0.0, 0.0), float2(0.0, 0.0), float2(0.0, 0.0)};
    if (inside) {
        m[0] = Turn(uv, InvCur[0], PrevOther[0]);
        m[1] = Turn(uv, InvCur[1], PrevOther[1]);
        m[2] = Turn(uv, InvCur[2], PrevOther[2]);
    }
    const float k = (inside && far) ? 1.0 : 0.0;
    // the fit, per guess (an offset of its own each frame - the jitter's):
    // sums of g.m, m.m, m; then of g, g.g, how many; the mask's look
    [unroll] for (int q = 0; q < 3; ++q) {
        s[4 * q + 0][gi] = k * dot(g, m[q]);
        s[4 * q + 1][gi] = k * dot(m[q], m[q]);
        s[4 * q + 2][gi] = k * m[q].x;
        s[4 * q + 3][gi] = k * m[q].y;
    }
    s[12][gi] = k * g.x;
    s[13][gi] = k * g.y;
    s[14][gi] = k * dot(g, g);
    s[15][gi] = k;
    s[16][gi] = (inside && d <= 0.0) ? 1.0 : 0.0;
    s[17][gi] = inside ? 1.0 : 0.0;
    // near: how much of the game's vector (less the jitter's step) the camera against
    // the other eye explains - for each guess of the picture's view (q) and each sign
    const float2 lead = (inside && !far) ? g - Jit.xy : 0.0;
    s[18][gi] = dot(lead, lead);
    [unroll] for (int c = 0; c < 6; ++c) {
        const int q = c >> 1;
        const float a = (c & 1) ? 1.0 : -1.0;
        float2 left = 0.0;
        if (inside && !far) {
            const float2 tO = Turn(uv, InvCur[q], PrevOther[q]);
            const float2 dO = Move(uv, tO, InvCur[q], PrevOther[q]);
            const float2 r = lead - a * tO;   // a z dO + the objects' own
            const float dd = dot(dO, dO);
            const float z = dd > 1e-12 ? clamp(a * dot(r, dO) / dd, 0.0, 1.0) : 0.0;
            left = r - a * z * dO;
        }
        s[19 + c][gi] = dot(left, left);
    }
    // far: the camera's turn against the same eye two renders back (what mode 2 adds)
    const float2 tSame = inside ? Turn(uv, InvCur[Use], PrevSame[Use]) : 0.0;
    s[25][gi] = k * tSame.x;
    s[26][gi] = k * tSame.y;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step = 128; step > 0; step >>= 1) {
        if (gi < step)
            [unroll] for (int r = 0; r < K; ++r) s[r][gi] += s[r][gi + step];
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) {
        const uint at = (gid.y * GroupsX + gid.x) * (K * 4);
        [unroll] for (int r = 0; r < K; ++r) Stats.Store(at + r * 4, asuint(s[r][0]));
    }
    // eight places, everything about them: depth, the game's vector, each guess's
    const float2 kAt[8] = {float2(0.5, 0.5), float2(0.5, 0.75), float2(0.5, 0.9), float2(0.25, 0.9),
                           float2(0.75, 0.9), float2(0.5, 0.3), float2(0.1, 0.5), float2(0.9, 0.5)};
    [unroll] for (int q = 0; q < 8; ++q) {
        if (all(id.xy == uint2(kAt[q] * float2(Size)))) {
            const uint at = 900000 + q * 48;
            Stats.Store4(at, asuint(float4(d, g.x, g.y, m[0].x)));
            Stats.Store4(at + 16, asuint(float4(m[0].y, m[1].x, m[1].y, m[2].x)));
            Stats.Store4(at + 32, asuint(float4(m[2].y, uv.x, uv.y, far ? 0.0 : 1.0)));
        }
    }
    // and a column down the middle: depth and the game's vector, top to bottom
    if (id.x == Size.x / 2) {
        [unroll] for (int q = 0; q < 16; ++q) {
            if (id.y == uint((q + 0.5) / 16.0 * Size.y)) {
                const uint at = 900000 + 8 * 48 + q * 16;
                Stats.Store4(at, asuint(float4(d, g.x, g.y, m[1].x)));
            }
        }
    }
    // the game's vector = its jitter step + Scale (where the point was - uv) + the objects' own motion
    if (inside && Mode == 2) {
        const uint u = Use;
        const float2 tS = Turn(uv, InvCur[u], PrevSame[u]);
        const float2 jit = (Fix & 2) ? Jit.zw : Jit.xy;   // the jitter's step: this eye's last, or the game's (the render before)
        float2 o;
        if (far) {
            o = g - Jit.xy + jit + FarTurn * Scale * tS;
        } else if (!(Fix & 1)) {
            o = g - Jit.xy + jit;
        } else {
            const float2 tO = Turn(uv, InvCur[u], PrevOther[u]);
            const float2 dO = Move(uv, tO, InvCur[u], PrevOther[u]);
            const float2 dS = Move(uv, tS, InvCur[u], PrevSame[u]);
            const float2 r = g - Jit.xy - Scale * tO;   // Scale z dO + the objects' own
            const float dd = dot(dO, dO);
            const float z = dd > 1e-12 ? clamp(Scale * dot(r, dO) / dd, 0.0, 1.0) : 0.0;
            o = jit + Scale * (tS + z * dS) + (r - Scale * z * dO);
        }
        Out[id.xy] = o;
    }
}
)";

constexpr int kGuesses = 3;
struct Consts {
    float invCur[kGuesses][16];
    float prevOther[kGuesses][16];
    float prevSame[kGuesses][16];
    uint32_t size[2];
    uint32_t mode, use;
    float scale;
    uint32_t groupsX;
    float farTurn;
    uint32_t fix;
    float jit[4];
};
static_assert(sizeof(Consts) == 624, "the cbuffer's layout");
constexpr UINT64 kStatsSlot = 1u << 20;   // per slot: 108 bytes a group (below 900000), then the sample places
constexpr int kSums = 27;                  // floats a group

constexpr int kSlots = 8;   // in-flight constants and stats, round robin
struct Pass {
    bool tried = false, ok = false;
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;   // 4 per slot: depth, game, out, stats
    UINT inc = 0;
    ComPtr<ID3D12Resource> consts;       // upload, kSlots * 1024
    uint8_t* constsPtr = nullptr;
    ComPtr<ID3D12Resource> stats;        // default, kSlots * kStatsSlot, UAV
    ComPtr<ID3D12Resource> zero;         // (unused)
    ComPtr<ID3D12Resource> readback;     // readback, kSlots * kStatsSlot
    uint8_t* readPtr = nullptr;
    ComPtr<ID3D12Resource> out;          // R16G16_FLOAT, the game's vectors' size
    D3D12_RESOURCE_STATES outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    UINT64 outW = 0;
    UINT outH = 0;
    uint32_t slot = 0;
    uint32_t evals = 0;
    int pendingSlot[kSlots];
    uint32_t pendingAt[kSlots];
    uint32_t pendingGroups[kSlots];
    int pendingEye[kSlots];
    float pendingJitter[kSlots][4];      // this evaluation's, the one before's
    uint32_t pendingSize[kSlots][2];
    float pendingYaw[kSlots][3];         // the views' yaw: this, the last, two back (degrees)
    unsigned long long pendingUs[kSlots];
    float lastJitter[2] = {};
    float eyeJitter[2][2] = {};          // each eye's last
} g;
std::mutex g_passLock;

std::atomic<int> g_mode{0};
std::atomic<float> g_farTurn{1.0f};
std::atomic<uint32_t> g_fix{2};   // the jitter only: the near part redone from the eyes' parallax swam on grass in head turns
std::atomic<int> g_lag{0};   // the view the evaluation came with is the picture's: of 0/1/2 back only 0 explains the game's near vectors (99.5%+, head turning too)
std::atomic<float> g_sign{-1.0f};   // the game's vector = where the point is - where it was: -1 explains its near vectors, +1 leaves 15-180%
// what the checks found: the guess and the scale to use
std::atomic<int> g_use{-1};
std::atomic<float> g_scale{0.0f};

bool Build(ID3D12Device* dev) {
    g.tried = true;
    HMODULE comp = LoadLibraryW(L"d3dcompiler_47.dll");
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    CompileFn compile = comp ? (CompileFn)GetProcAddress(comp, "D3DCompile") : nullptr;
    if (!compile) { Log("vrcam: DLSS vectors - no d3dcompiler_47.dll, off"); return false; }
    ComPtr<ID3DBlob> cs, err;
    if (FAILED(compile(kShader, strlen(kShader), "dlss_mv", nullptr, nullptr, "Main", "cs_5_0", 0, 0, &cs, &err))) {
        LogF("vrcam: DLSS vectors - the shader did not compile: %s", err ? (const char*)err->GetBufferPointer() : "?");
        return false;
    }
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 2;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 2;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs = {};
    rs.NumParameters = 2;
    rs.pParameters = params;
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    ComPtr<ID3DBlob> rsBlob;
    if (!serialize || FAILED(serialize(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &err)) ||
        FAILED(dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&g.root)))) {
        Log("vrcam: DLSS vectors - no root signature, off");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = g.root.Get();
    pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g.pso)))) { Log("vrcam: DLSS vectors - no pipeline, off"); return false; }
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4 * kSlots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g.heap)))) { Log("vrcam: DLSS vectors - no descriptor heap, off"); return false; }
    g.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto buffer = [&](D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& r) {
        D3D12_HEAP_PROPERTIES hp = {type};
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = flags;
        return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)));
    };
    if (!buffer(D3D12_HEAP_TYPE_UPLOAD, 1024 * kSlots, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, g.consts) ||
        !buffer(D3D12_HEAP_TYPE_DEFAULT, kStatsSlot * kSlots, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, g.stats) ||
        !buffer(D3D12_HEAP_TYPE_UPLOAD, 64, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, g.zero) ||
        !buffer(D3D12_HEAP_TYPE_READBACK, kStatsSlot * kSlots, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, g.readback)) {
        Log("vrcam: DLSS vectors - no buffers, off");
        return false;
    }
    D3D12_RANGE none = {0, 0};
    g.consts->Map(0, &none, (void**)&g.constsPtr);
    void* z = nullptr;
    g.zero->Map(0, &none, &z);
    memset(z, 0, 64);
    g.zero->Unmap(0, nullptr);
    g.readback->Map(0, nullptr, (void**)&g.readPtr);
    for (int i = 0; i < kSlots; ++i) g.pendingSlot[i] = -1;
    g.dev = dev;
    g.ok = true;
    Log("vrcam: DLSS vectors - the pass is built");
    return true;
}

bool EnsureOut(const D3D12_RESOURCE_DESC& mvDesc) {
    if (g.out && g.outW == mvDesc.Width && g.outH == mvDesc.Height) return true;
    g.out.Reset();
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d = mvDesc;
    d.Format = DXGI_FORMAT_R16G16_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    d.MipLevels = 1;
    d.DepthOrArraySize = 1;
    d.SampleDesc = {1, 0};
    if (FAILED(g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&g.out))))
        return false;
    g.outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    g.outW = mvDesc.Width;
    g.outH = mvDesc.Height;
    return true;
}

DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
    default: return f;
    }
}

// The matrices for one guess: `lag` = how many of this eye's frames back the
// renderer's "this frame" is (0: the newest one vrcam handed over).
// guess 0: the frame being built now; 1: the newest recorded; 2: the one before
// - whichever of them is this eye's, with the two frames before it.
// Where a view's camera is: C R + t = 0 (rows 0-2 R, row 3 t).
bool CamPos(const float* v, double c[3]) {
    const double a = v[0], b = v[1], cc = v[2], d = v[4], e = v[5], f = v[6], g7 = v[8], h = v[9], i = v[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g7) + cc * (d * h - e * g7);
    if (std::fabs(det) < 1e-20) return false;
    const double inv[9] = {(e * i - f * h) / det, (cc * h - b * i) / det, (b * f - cc * e) / det,
                           (f * g7 - d * i) / det, (a * i - cc * g7) / det, (cc * d - a * f) / det,
                           (d * h - e * g7) / det, (b * g7 - a * h) / det, (a * e - b * d) / det};
    for (int j = 0; j < 3; ++j) c[j] = -(v[12] * inv[j] + v[13] * inv[3 + j] + v[14] * inv[6 + j]);
    return true;
}
// A view taking points given against the camera at c (world - c).
void Against(const float* view, const double c[3], float out[16]) {
    memcpy(out, view, 16 * sizeof(float));
    for (int j = 0; j < 3; ++j) out[12 + j] = (float)(c[0] * view[j] + c[1] * view[4 + j] + c[2] * view[8 + j] + view[12 + j]);
}

float ViewYaw(const float* v) { return std::atan2(v[2], v[10]) * 57.2957795f; }
unsigned long long NowUs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart / (f.QuadPart / 1000000));
}

// The matrices for one guess: the renderer's "this frame" is the view `guess`
// evaluations back (0: the one this evaluation came with); the last frame is
// the evaluation before it (the other eye), two back the one before that.
bool Matrices(int eye, int guess, float invCur[16], float prevOther[16], float prevSame[16]) {
    std::lock_guard<std::mutex> l(g_framesLock);
    if (g_evalCount < (uint32_t)(guess + 3)) return false;
    const Frame& cur = g_evals[(g_evalCount - 1 - guess) % kFrames];
    const Frame& other = g_evals[(g_evalCount - 2 - guess) % kFrames];
    const Frame& same = g_evals[(g_evalCount - 3 - guess) % kFrames];
    if (other.eye == cur.eye || same.eye != cur.eye) return false;   // the eyes are not by turns here
    (void)eye;
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: DLSS vectors - eye %d: this frame's eye position %.3f %.3f %.3f, the last frame's %.3f %.3f %.3f, two back %.3f %.3f %.3f; proj[8] %.4f / %.4f",
             eye, cur.view[12], cur.view[13], cur.view[14], other.view[12], other.view[13], other.view[14], same.view[12], same.view[13],
             same.view[14], cur.proj[8], other.proj[8]);
    }
    // this camera at the origin, the earlier ones placed against it (the world's
    // coordinates are too large for floats to keep the eyes' few cm apart)
    double at[3];
    if (!CamPos(cur.view, at)) return false;
    float vc[16], vo[16], vs[16];
    Against(cur.view, at, vc);
    Against(other.view, at, vo);
    Against(same.view, at, vs);
    float vp[16];
    Mul(vc, cur.proj, vp);
    if (!Inverse(vp, invCur)) return false;
    Mul(vo, other.proj, prevOther);
    Mul(vs, same.proj, prevSame);
    return true;
}

// The fit, gathered over frames: per frame centred (each frame's own offset -
// the jitter's - drops out), summed until it is told.
struct Fit {
    double gm[kGuesses] = {}, mm[kGuesses] = {}, gg = 0.0, n = 0.0;
    double rt[kGuesses] = {}, tt[kGuesses] = {}, rr = 0.0;   // per frame: the far part's mean less the jitter's step (r), each guess's mean turn (t)
    double nearLead[2] = {}, nearLeft[2][6] = {};   // near: the game's vectors, left per guess and sign - [0] all frames, [1] the head turning
    int turning = 0;
    int frames = 0;
};
Fit g_fit;

void ReadStats() {
    if (g_mode.load() != 1) {   // the check's numbers go to the log in mode 1 only
        for (int i = 0; i < kSlots; ++i) g.pendingSlot[i] = -1;
        return;
    }
    for (int i = 0; i < kSlots; ++i) {
        const int s = g.pendingSlot[i];
        if (s < 0 || g.evals - g.pendingAt[i] < 6) continue;   // the GPU is done with it by now
        g.pendingSlot[i] = -1;
        const float* v = (const float*)(g.readPtr + kStatsSlot * s);
        double sum[kSums] = {};
        for (uint32_t grp = 0; grp < g.pendingGroups[i]; ++grp)
            for (int q = 0; q < kSums; ++q) sum[q] += v[grp * kSums + q];
        const double n = sum[15], all = sum[17];
        if (n < 1000) continue;
        const double gx = sum[12], gy = sum[13];
        const double gg = sum[14] - (gx * gx + gy * gy) / n;
        for (int k = 0; k < kGuesses; ++k) {
            const double mx = sum[4 * k + 2], my = sum[4 * k + 3];
            g_fit.gm[k] += sum[4 * k] - (gx * mx + gy * my) / n;
            g_fit.mm[k] += sum[4 * k + 1] - (mx * mx + my * my) / n;
        }
        g_fit.gg += gg;
        g_fit.n += n;
        ++g_fit.frames;
        {
            const double tsx = sum[25] / n, tsy = sum[26] / n;
            const bool turn = tsx * tsx + tsy * tsy > 0.001 * 0.001;
            for (int w = 0; w < (turn ? 2 : 1); ++w) {
                g_fit.nearLead[w] += sum[18];
                for (int c = 0; c < 6; ++c) g_fit.nearLeft[w][c] += sum[19 + c];
            }
            if (turn) ++g_fit.turning;
        }
        // the game's far vectors: the jitter's step, and the camera's turn if it is in them
        const double jx = (g.pendingJitter[i][0] - g.pendingJitter[i][2]) / g.pendingSize[i][0];
        const double jy = (g.pendingJitter[i][1] - g.pendingJitter[i][3]) / g.pendingSize[i][1];
        const double rx = gx / n - jx, ry = gy / n - jy;
        g_fit.rr += rx * rx + ry * ry;
        for (int k = 0; k < kGuesses; ++k) {
            const double tx = sum[4 * k + 2] / n, ty = sum[4 * k + 3] / n;
            g_fit.rt[k] += rx * tx + ry * ty;
            g_fit.tt[k] += tx * tx + ty * ty;
        }

        // a run of frames now and then: the eye, its jitter, the far part's mean vector (the jitter's shift)
        static ULONGLONG burstAt = 0;
        static int burst = 0;
        if (GetTickCount64() - burstAt > 3000) { burstAt = GetTickCount64(); burst = 40; }
        if (burst > 0) {
            --burst;
            LogF("vrcam: DLSS vectors - frame at %llu us: eye %d, view yaw %.2f / last %.2f / two back %.2f, far: game less jitter step %.6f %.6f, turn vs the other eye %.6f %.6f, vs the same eye %.6f %.6f",
                 g.pendingUs[i], g.pendingEye[i], g.pendingYaw[i][0], g.pendingYaw[i][1], g.pendingYaw[i][2], rx, ry, sum[2] / n, sum[3] / n, sum[25] / n, sum[26] / n);
        }
        static ULONGLONG toldSamples = 0;
        if (GetTickCount64() - toldSamples > 6000) {
            toldSamples = GetTickCount64();
            const float* sp = (const float*)(g.readPtr + kStatsSlot * s + 900000);
            for (int q = 0; q < 8; ++q, sp += 12)
                LogF("vrcam: DLSS vectors - at %.2f,%.2f: depth %.5f%s, game %.5f %.5f | turn 0 %.5f %.5f, 1 %.5f %.5f, 2 %.5f %.5f",
                     sp[9], sp[10], sp[0], sp[11] > 0.5f ? " (near)" : "", sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], sp[8]);
            char line[3][1024];
            int len[3] = {};
            for (int q = 0; q < 16; ++q, sp += 4) {
                len[0] += snprintf(line[0] + len[0], sizeof line[0] - len[0], " %.4f", sp[0]);
                len[1] += snprintf(line[1] + len[1], sizeof line[1] - len[1], " %.5f", sp[1]);
                len[2] += snprintf(line[2] + len[2], sizeof line[2] - len[2], " %.5f", sp[3]);
            }
            LogF("vrcam: DLSS vectors - middle column, top to bottom, depth:%s", line[0]);
            LogF("vrcam: DLSS vectors - middle column, game x:%s", line[1]);
            LogF("vrcam: DLSS vectors - middle column, turn 1 x:%s", line[2]);
        }
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 2000) {
            told = GetTickCount64();
            const Fit f = g_fit;
            g_fit = Fit{};
            if (f.frames < 10) continue;
            for (int w = 0; w < 2; ++w) {
                if (f.nearLead[w] <= 0.0) continue;
                double pc[6];
                for (int c = 0; c < 6; ++c) pc[c] = 100.0 * f.nearLeft[w][c] / f.nearLead[w];
                LogF("vrcam: DLSS vectors - near, %s (%d frames): left of the game's vectors by view back 0/1/2 and sign -/+: "
                     "0: %.1f%% / %.1f%%, 1: %.1f%% / %.1f%%, 2: %.1f%% / %.1f%%",
                     w ? "the head turning" : "all", w ? f.turning : f.frames, pc[0], pc[1], pc[2], pc[3], pc[4], pc[5]);
            }
            {
                double sc[kGuesses], lf[kGuesses];
                for (int k = 0; k < kGuesses; ++k) {
                    sc[k] = f.tt[k] > 0 ? f.rt[k] / f.tt[k] : 0.0;
                    lf[k] = f.rr > 0 ? 1.0 - (f.tt[k] > 0 ? f.rt[k] * f.rt[k] / f.tt[k] : 0.0) / f.rr : 1.0;
                }
                LogF("vrcam: DLSS vectors - over %d frames: the game's far vectors less the jitter's step move %.6f; the head's turn %.6f / %.6f / %.6f; fit scale %.3f %.0f%% left / %.3f %.0f%% / %.3f %.0f%%",
                     f.frames, std::sqrt(f.rr / f.frames), std::sqrt(f.tt[0] / f.frames), std::sqrt(f.tt[1] / f.frames), std::sqrt(f.tt[2] / f.frames),
                     sc[0], 100.0 * lf[0], sc[1], 100.0 * lf[1], sc[2], 100.0 * lf[2]);
            }
            if (f.gg <= 0.0) continue;
            double scale[kGuesses], left[kGuesses];
            int best = 0;
            for (int k = 0; k < kGuesses; ++k) {
                scale[k] = f.mm[k] > 0 ? f.gm[k] / f.mm[k] : 0.0;
                left[k] = f.gg - (f.mm[k] > 0 ? f.gm[k] * f.gm[k] / f.mm[k] : 0.0);
                if (left[k] < left[best]) best = k;
            }
            LogF("vrcam: DLSS vectors - turn check over %d frames, far part: game moves %.5f, turn %.5f / %.5f / %.5f; guess 0 (this frame) scale %.3f, %.0f%% left; 1: %.3f, %.0f%% left; 2: %.3f, %.0f%% left",
                 f.frames, std::sqrt(f.gg / f.n), std::sqrt(f.mm[0] / f.n), std::sqrt(f.mm[1] / f.n), std::sqrt(f.mm[2] / f.n),
                 scale[0], 100.0 * left[0] / f.gg, scale[1], 100.0 * left[1] / f.gg, scale[2], 100.0 * left[2] / f.gg);
            // trusted only when the head turned enough and the fit explains most of the game's vectors
            if (std::sqrt(f.mm[best] / f.n) > 0.0005 && left[best] < 0.3 * f.gg && std::fabs(scale[best]) > 0.1) {
                g_use.store(best);
                g_scale.store((float)scale[best]);
            }
        }
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetMode(int mode) { g_mode.store(mode); }
void SetSign(float sign) { g_sign.store(sign < 0.0f ? -1.0f : 1.0f); }
void SetLag(int lag) { g_lag.store(std::clamp(lag, 0, 2)); }
void SetFarTurn(float k) { g_farTurn.store(std::clamp(k, -2.0f, 2.0f)); }
void SetFixes(bool nearPart, bool jitter) { g_fix.store((nearPart ? 1u : 0u) | (jitter ? 2u : 0u)); }

void SetPending(const float view[16], const float proj[16], int eye) {
    std::lock_guard<std::mutex> l(g_framesLock);
    memcpy(g_pending.view, view, sizeof g_pending.view);
    memcpy(g_pending.proj, proj, sizeof g_pending.proj);
    g_pending.eye = eye;
    g_pendingOk = true;
}

void RecordFrame(const float view[16], const float proj[16], int eye) {
    std::lock_guard<std::mutex> l(g_framesLock);
    Frame& f = g_frames[g_frameCount % kFrames];
    memcpy(f.view, view, sizeof f.view);
    memcpy(f.proj, proj, sizeof f.proj);
    f.eye = eye;
    f.n = g_frameCount;
    ++g_frameCount;
}

void* BeforeEvaluate(void* cmdListV, const void* paramsV, int eye) {
    const int mode = g_mode.load();
    if (mode <= 0 || !cmdListV || !paramsV) return nullptr;
    auto* cl = (ID3D12GraphicsCommandList*)cmdListV;
    auto* params = (NgxParameter*)const_cast<void*>(paramsV);
    std::lock_guard<std::mutex> l(g_passLock);
    if (!g.tried) {
        ComPtr<ID3D12Device> dev;
        if (FAILED(cl->GetDevice(IID_PPV_ARGS(&dev))) || !Build(dev.Get())) return nullptr;
    }
    if (!g.ok) return nullptr;
    ++g.evals;
    ReadStats();

    ID3D12Resource *depth = nullptr, *mv = nullptr;
    unsigned int w = 0, h = 0;
    if (!Ok(params->Get("Depth", &depth)) || !Ok(params->Get("MotionVectors", &mv)) || !depth || !mv) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: DLSS vectors - the evaluation carries no depth or vectors I can read, off"); }
        return nullptr;
    }
    if (!Ok(params->Get("DLSS.Render.Subrect.Dimensions.Width", &w)) || !Ok(params->Get("DLSS.Render.Subrect.Dimensions.Height", &h)) || !w || !h) {
        params->Get("Width", &w);
        params->Get("Height", &h);
    }
    const D3D12_RESOURCE_DESC dd = depth->GetDesc(), md = mv->GetDesc();
    if (!w || !h || w > md.Width || h > md.Height) return nullptr;
    {
        static bool told = false;
        if (!told) {
            told = true;
            float sx = 0, sy = 0;
            params->Get("MV.Scale.X", &sx);
            params->Get("MV.Scale.Y", &sy);
            LogF("vrcam: DLSS vectors - depth %llux%u fmt %d, vectors %llux%u fmt %d, scene %ux%u, MV scale %.1f %.1f",
                 (unsigned long long)dd.Width, dd.Height, (int)dd.Format, (unsigned long long)md.Width, md.Height, (int)md.Format, w, h, sx, sy);
        }
    }

    {   // the view this evaluation came with
        std::lock_guard<std::mutex> lf(g_framesLock);
        if (!g_pendingOk) return nullptr;
        g_evals[g_evalCount % kFrames] = g_pending;
        g_evals[g_evalCount % kFrames].n = g_evalCount;
        ++g_evalCount;
        static int mismatch = 0;
        if (g_pending.eye != eye && ++mismatch == 30)
            LogF("vrcam: DLSS vectors - the evaluations' eye and vrcam's latest view's differ (30 times so far: eye %d, view's %d)", eye, g_pending.eye);
    }
    Consts c = {};
    bool any = false;
    for (int k = 0; k < kGuesses; ++k) {
        if (Matrices(eye, k, c.invCur[k], c.prevOther[k], c.prevSame[k])) any = true;
        else { for (int i = 0; i < 16; ++i) c.invCur[k][i] = c.prevOther[k][i] = c.prevSame[k][i] = (i % 5 == 0) ? 1.0f : 0.0f; }
    }
    if (!any) return nullptr;
    // HLSL takes a float4x4 by columns: hand the transposes over so mul(row, M) is p * M
    auto transpose = [](float* m) { for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) std::swap(m[i * 4 + j], m[j * 4 + i]); };
    for (int k = 0; k < kGuesses; ++k) { transpose(c.invCur[k]); transpose(c.prevOther[k]); transpose(c.prevSame[k]); }
    c.groupsX = (w + 15) / 16;
    const uint32_t groups = c.groupsX * ((h + 15) / 16);
    if (groups * kSums * 4 > 900000) return nullptr;
    c.size[0] = w;
    c.size[1] = h;
    const int use = g_lag.load();   // the picture's view: this many evaluations back ([render] dlss_mv_lag)
    const bool fix = mode == 2 && EnsureOut(md);
    c.mode = fix ? 2 : 1;
    c.use = (uint32_t)use;
    c.scale = g_sign.load();
    c.farTurn = g_farTurn.load();
    c.fix = g_fix.load();
    float jx = 0.0f, jy = 0.0f;
    params->Get("Jitter.Offset.X", &jx);
    params->Get("Jitter.Offset.Y", &jy);
    c.jit[0] = (jx - g.lastJitter[0]) / w;
    c.jit[1] = (jy - g.lastJitter[1]) / h;
    c.jit[2] = (jx - g.eyeJitter[eye & 1][0]) / w;
    c.jit[3] = (jy - g.eyeJitter[eye & 1][1]) / h;

    const uint32_t s = g.slot;
    g.slot = (g.slot + 1) % kSlots;
    memcpy(g.constsPtr + 1024 * s, &c, sizeof c);

    // descriptors: depth, game's vectors, ours, the stats slot
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)g.inc * 4 * s;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = DepthSrvFormat(dd.Format);
    g.dev->CreateShaderResourceView(depth, &sv, cpu);
    cpu.ptr += g.inc;
    sv.Format = md.Format == DXGI_FORMAT_R16G16_TYPELESS ? DXGI_FORMAT_R16G16_FLOAT : md.Format;
    g.dev->CreateShaderResourceView(mv, &sv, cpu);
    cpu.ptr += g.inc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    if (fix) {
        uv.Format = DXGI_FORMAT_R16G16_FLOAT;
        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        g.dev->CreateUnorderedAccessView(g.out.Get(), nullptr, &uv, cpu);
    } else {   // a null view keeps the table whole
        uv.Format = DXGI_FORMAT_R16G16_FLOAT;
        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        g.dev->CreateUnorderedAccessView(nullptr, nullptr, &uv, cpu);
    }
    cpu.ptr += g.inc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC bv = {};
    bv.Format = DXGI_FORMAT_R32_TYPELESS;
    bv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    bv.Buffer.FirstElement = (UINT64)(kStatsSlot / 4) * s;
    bv.Buffer.NumElements = (UINT)(kStatsSlot / 4);
    bv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    g.dev->CreateUnorderedAccessView(g.stats.Get(), nullptr, &bv, cpu);

    // every group writes its own sums: the stats need no clearing (kept in UAV between uses)
    D3D12_RESOURCE_BARRIER b[2] = {};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = g.stats.Get();
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    int nb = 0;
    if (fix && g.outState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[1].Transition.pResource = g.out.Get();
        b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b[1].Transition.StateBefore = g.outState;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cl->ResourceBarrier(1, &b[1]);
        g.outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    (void)nb;

    ID3D12DescriptorHeap* heaps[] = {g.heap.Get()};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(g.root.Get());
    cl->SetPipelineState(g.pso.Get());
    cl->SetComputeRootConstantBufferView(0, g.consts->GetGPUVirtualAddress() + 1024 * s);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = g.heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)g.inc * 4 * s;
    cl->SetComputeRootDescriptorTable(1, gpu);
    cl->Dispatch((w + 15) / 16, (h + 15) / 16, 1);

    // the stats out to the readback slot; the vectors over to DLSS
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    nb = 1;
    if (fix) {
        b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[1].Transition.pResource = g.out.Get();
        b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g.outState = b[1].Transition.StateAfter;
        nb = 2;
    }
    cl->ResourceBarrier(nb, b);
    cl->CopyBufferRegion(g.readback.Get(), kStatsSlot * s, g.stats.Get(), kStatsSlot * s, (UINT64)groups * kSums * 4);
    cl->CopyBufferRegion(g.readback.Get(), kStatsSlot * s + 900000, g.stats.Get(), kStatsSlot * s + 900000, 8 * 48 + 16 * 16);
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cl->ResourceBarrier(1, b);
    for (int i = 0; i < kSlots; ++i)
        if (g.pendingSlot[i] < 0) {
            g.pendingSlot[i] = (int)s; g.pendingAt[i] = g.evals; g.pendingGroups[i] = groups;
            g.pendingEye[i] = eye; g.pendingJitter[i][0] = jx; g.pendingJitter[i][1] = jy;
            g.pendingJitter[i][2] = g.lastJitter[0]; g.pendingJitter[i][3] = g.lastJitter[1];
            g.pendingSize[i][0] = w; g.pendingSize[i][1] = h;
            g.pendingUs[i] = NowUs();
            {
                std::lock_guard<std::mutex> lf(g_framesLock);
                for (int k = 0; k < 3; ++k)
                    g.pendingYaw[i][k] = g_evalCount > (uint32_t)k ? ViewYaw(g_evals[(g_evalCount - 1 - k) % kFrames].view) : 0.0f;
            }
            break;
        }
    g.lastJitter[0] = jx;
    g.lastJitter[1] = jy;
    g.eyeJitter[eye & 1][0] = jx;
    g.eyeJitter[eye & 1][1] = jy;

    if (!fix) return nullptr;
    params->Set("MotionVectors", g.out.Get());
    {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: DLSS vectors - DLSS now gets the same eye's motion (the camera's turn, the near depth from the eyes' parallax, the jitter's step)"); }
    }
    return mv;   // the game's own, to put back after the evaluation
}

void AfterEvaluate(const void* paramsV, void* restore) {
    if (!restore || !paramsV) return;
    auto* params = (NgxParameter*)const_cast<void*>(paramsV);
    params->Set("MotionVectors", (ID3D12Resource*)restore);
}

}  // namespace dlssmv
