// ppr_standins.h - shaders of the tests' own with the shape of the game's reflection passes (cs_ppr_projection_c, ps_ppr_reflection_p:
// Blizzard's, never in the repository), for cleanroom/tests/dxbc_ppr_eye_test.cpp and reflections_eye_native_test.cpp.
#pragma once

namespace pprstandin {

// The game's projection pass, as its disassembly reads (one thread a pixel; the source pixel's index written into the pixel its
// mirror image is seen at). c3[1] = (w, h), c3[9..12] the inverse view (row 12 the eye), c3[17].x / c3[18].y the inverse projection's
// diagonal, c3[21..24] view * projection; c10[1] the eye, c10[53] = (range, 1 / range, start) of the linear depth; c13 one plane:
// its axes in the columns of rows 0..2 (z the normal), its centre in row 4, its half size in row 5.
inline const char* kProjCs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[91]; }
Texture2D<float> depth : register(t0);
RWStructuredBuffer<uint> outBuf : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint w = (uint)c3[1].x, h = (uint)c3[1].y;
    if (id.x >= w * h) return;
    const uint py = id.x / w, px = id.x - py * w;
    const float raw = depth.Load(int3(px, py, 0));
    if (raw <= 0.0) return;   // (the test's scene: one point)
    const float d = raw * c10[53].x + c10[53].z;
    const float valid = d < c10[53].z + c10[53].x ? 1.0 : 0.0;
    const float2 ndc = (float2(px, py) + 0.5) / float2(w, h) * 2.0 - 1.0;
    const float3 v = float3(ndc.x * c3[17].x * d, ndc.y * -c3[18].y * d, -d);
    const float3 world = c3[9].xyz * v.x + c3[10].xyz * v.y + c3[11].xyz * v.z + c3[12].xyz * valid;
    const float3 n = float3(c13[0].z, c13[1].z, c13[2].z);
    const float3 o = c13[4].xyz;
    const float3 refl = world - 2.0 * dot(world - o, n) * n;
    const float3 dir = normalize(refl - c10[1].xyz);
    const float dn = dot(n, dir);
    if (dn >= 0.0) return;
    const float t = dot(n, o - c10[1].xyz) / dn;
    if (t <= 0.0) return;
    const float3 x = c10[1].xyz + dir * t;
    const float4 clip = x.x * c3[21] + x.y * c3[22] + x.z * c3[23] + c3[24];
    const float2 s = float2(clip.x + clip.w, clip.w - clip.y) * 0.5 / clip.w;
    if (any(s < 0.0) || any(s > 1.0)) return;
    const uint2 to = uint2(s * float2(w, h));
    InterlockedMin(outBuf[to.y * w + to.x], id.x);
}
)";
// The resolve's shape: its own pixel's world point the same way (is it on a water plane?), the source pixel from the UAV
inline const char* kResolvePs = R"(
cbuffer CB3 : register(b3) { float4 c3[38]; }
cbuffer CB10 : register(b10) { float4 c10[54]; }
cbuffer CB13 : register(b13) { float4 c13[91]; }
Texture2D<float> depth : register(t0);
Texture2D<float4> scene : register(t1);
RWStructuredBuffer<uint> buf : register(u1);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float raw = depth.Load(int3(pos.xy, 0));
    const float d = raw * c10[53].x + c10[53].z;
    const float valid = d < c10[53].z + c10[53].x ? 1.0 : 0.0;
    const float2 ndc = uv * 2.0 - 1.0;
    const float3 v = float3(ndc.x * c3[17].x * d, ndc.y * -c3[18].y * d, -d);
    const float3 world = c3[9].xyz * v.x + c3[10].xyz * v.y + c3[11].xyz * v.z + c3[12].xyz * valid;
    const float3 eye = normalize(world - c10[1].xyz);
    if (abs(world.y - c13[4].y) > 0.6 || dot(eye, float3(c13[0].z, c13[1].z, c13[2].z)) > 0.0) return scene.Load(int3(pos.xy, 0));
    const uint src = buf[(uint)pos.y * (uint)c3[1].x + (uint)pos.x];
    const float4 base = scene.Load(int3(pos.xy, 0));
    if (src == 0xFFFFFFFF) return base;
    return base + scene.Load(int3(src % (uint)c3[1].x, src / (uint)c3[1].x, 0)) * 0.5;
}
)";
}  // namespace pprstandin
