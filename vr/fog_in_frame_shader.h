#pragma once
// [render] fog_in_frame (vr/fog_in_frame.h): our three shaders in place of the game's transparent resolve. Written here
// from what the game's resolve does (vr/fog_in_frame_math.h), not copied from it.
//
// kPixel - two pixel shaders for the game's own resolve pipeline (its vertex shader, root signature, stencil test):
//   CopyScene  the opaque scene term alone (the game's t10) into our copy of it;
//   Composite  what the transparent effects add and how much of the scene shows through them (alpha), blended over
//              the fogged scene in the frame's target as R = rgb + R * alpha.
//   The registers are the game's (t10 - t13, s0), its vertex shader's outputs SV_Position and TEXCOORD0.
// kFog - the fog, a compute shader with a root signature of our own (the game's graphics bindings stay as they are):
//   reads the depth and stencil, our copy of the scene and the frame's target; writes the fogged scene where the game's
//   draw reaches (its viewport, scissor and stencil test), the target as it is elsewhere. The fog is the post pass's
//   curve (reshade/D2R_DepthFog.fx FogAmount) by each pixel's OWN depth - not the post pass's farthest of five and its
//   blur: those are for the finished, anti-aliased picture; here a pixel of the scene is one surface at its depth, and
//   the farthest of five ringed every object against the far ground or the void with full fog (test-0176, the planks
//   behind a torch). The C++ mirror: fogframe::FogPixel. With the effects drawn into the scene kept apart (k1.y), the
//   result is fog(S0) K + E (E, K: what they added and how much of the scene they left; fogframe::Apart).
// tests: cleanroom/tests/fog_in_frame_test.cpp

namespace fogframe {

inline constexpr char kPixel[] = R"hlsl(
Texture2D<float4> sceneTex : register(t10);
Texture2D<float4> accumTex : register(t11);
Texture2D<float4> revealTex : register(t12);
Texture2D<float4> glowTex : register(t13);
SamplerState frameSmp : register(s0);

float4 CopyScene(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    return float4(sceneTex.Sample(frameSmp, uv).rgb, 0.0);
}

float4 Composite(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    const float4 accum = accumTex.Sample(frameSmp, uv);
    const float4 reveal = revealTex.Sample(frameSmp, uv);
    const float3 glow = glowTex.Sample(frameSmp, uv).rgb;
    if (!(accum.a > 0.0)) return float4(glow, 1.0);
    const float a = saturate(reveal.a);
    return float4((1.0 - a) * (accum.rgb / accum.a * reveal.rgb) + glow, a);
}
)hlsl";

inline constexpr char kFog[] = R"hlsl(
cbuffer FogK : register(b0)
{
    float4 k0;   // NearPlane, FogStart, FogEnd, FogCurve
    float4 k1;   // FogStrength, the effects drawn into the scene kept apart (1: E and K below), [debug] fog_in_frame_paint, (w: not used)
    float4 k2;   // the fog's colour in scene units, the cave's warm rule (0 = none)
    uint4 k3;    // the pixels the game's draw reaches: x0 y0 x1 y1
    uint4 k4;    // the game's stencil test: on, func (ReShade's compare_op), ref, read mask
};
Texture2D<float> depthTex : register(t0);
Texture2D<uint2> stencilTex : register(t1);
Texture2D<float4> sceneTex : register(t2);
Texture2D<float4> frameTex : register(t3);
Texture2D<float4> effectsTex : register(t4);   // E: the effects the game drew into the scene, on black
Texture2D<float4> keepTex : register(t5);      // K: how much of the scene they left (the product of their factors)
RWTexture2D<float4> outTex : register(u0);

float FogAt(float dist) { return pow(saturate((dist - k0.y) / max(k0.z - k0.y, 1.0)), k0.w) * k1.x; }
float FogAmount(float depth) { return FogAt(k0.x / max(depth, 1e-7)); }

float LinearToSrgb(float c) { c = max(c, 0.0); return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }

float3 FogPixel(int2 p)
{
    const float3 colour = sceneTex.Load(int3(p, 0)).rgb;
    const float d = depthTex.Load(int3(p, 0));   // this pixel's own surface (the depth buffer is the target's size)
    if (!(d > 1e-6)) return colour;              // the void: the post pass's, as before
    const float t = FogAmount(d);
    float3 o = lerp(colour, k2.rgb, t);
    if (k2.w > 0.0) {
        const float3 l = float3(dot(float3(1.660532, -0.587643, -0.072840), colour), dot(float3(-0.124554, 1.132903, -0.008348), colour),
                                dot(float3(-0.018151, -0.100579, 1.118581), colour));
        const float warm = smoothstep(0.05, 0.25, LinearToSrgb(l.r) - LinearToSrgb(l.b));
        o += colour * (t * warm * min(k2.w, 1.0));
    }
    return o;
}

// [debug] fog_in_frame_paint 4: an own depth with a neighbour of none, or one whose fog differs by more than a quarter.
bool Silhouette(int2 p)
{
    const float d = depthTex.Load(int3(p, 0));
    if (!(d > 1e-6)) return false;
    uint w, h;
    depthTex.GetDimensions(w, h);
    const float t = FogAmount(d);
    const int2 o[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    [unroll] for (int i = 0; i < 4; ++i) {
        const float n = depthTex.Load(int3(clamp(p + o[i], int2(0, 0), int2(w, h) - 1), 0));
        if (!(n > 1e-6) || abs(FogAmount(n) - t) > 0.25) return true;
    }
    return false;
}

bool StencilPasses(uint value)
{
    if (k4.x == 0) return true;
    const uint a = k4.z & k4.w & 0xFF, b = value & k4.w & 0xFF;
    switch (k4.y) {
        case 0: return false;
        case 1: return a < b;
        case 2: return a == b;
        case 3: return a <= b;
        case 4: return a > b;
        case 5: return a != b;
        case 6: return a >= b;
        default: return true;
    }
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    frameTex.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    const int2 p = int2(id.xy);
    float4 o = frameTex.Load(int3(p, 0));
    if (id.x >= k3.x && id.y >= k3.y && id.x < k3.z && id.y < k3.w && StencilPasses(stencilTex.Load(int3(p, 0)).y))
    {
        float3 f = FogPixel(p);   // the opaque scene, fogged (the effects drawn into it were kept apart)
        float3 e = 0.0, kk = 1.0;
        if (k1.y > 0.5) { kk = keepTex.Load(int3(p, 0)).rgb; e = effectsTex.Load(int3(p, 0)).rgb; }
        const int paint = (int)(k1.z + 0.5);   // [debug] fog_in_frame_paint (fogframe::Painted)
        if (paint == 2) f = e;
        else if (paint == 3) f = f * kk;
        else if (paint == 4 && Silhouette(p)) f = float3(0.0, 1.0, 0.0);
        else f = f * kk + e;
        o = float4(f, 0.0);
    }
    outTex[p] = o;
}
)hlsl";

}  // namespace fogframe
