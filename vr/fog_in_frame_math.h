#pragma once
// [render] fog_in_frame (vr/fog_in_frame.h): the pure parts - which shader is the game's transparent resolve, what that
// resolve writes and what our three steps in its place write, the fog's colour in the game's own scene units, the fog
// itself (the C++ mirror of the compute shader in vr/fog_in_frame_shader.h) and the game's stencil test.
// Tested in cleanroom/tests/fog_in_frame_test.cpp (the shaders on WARP against these).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fogframe {

// ---------------------------------------------------------------- the game's shader
// The game's ps_transparent_resolve (data/hd/shaders/ps_transparent_resolve.bin, its one DXBC container): its size and
// FNV-1a 64 (with the basis vrcam's tools use, 1469598103934665603) - what ReShade hands over when the game makes the
// pipeline (the same values as vr/fog_spare.cpp). Another
// build of the game: not found, nothing done.
inline constexpr uint32_t kResolveSize = 756;
inline constexpr uint64_t kResolveHash = 0x53183967E9589EDBull;

inline uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= static_cast<const uint8_t*>(p)[i]; h *= 1099511628211ull; }
    return h;
}
// The size first: the hash only for a shader of the right size (the game makes thousands of pipelines at its start).
inline bool IsResolve(const void* code, size_t size) { return code && size == kResolveSize && Fnv(code, size) == kResolveHash; }

// ---------------------------------------------------------------- the resolve
// The game's resolve, from its disassembly: with accum, reveal and glow its three targets (the weighted-blended
// accumulation, the revealage target, the additive one) and S the lit opaque scene,
//     where accum.a > 0:  R = a S + (1 - a) T + A,  a = saturate(reveal.w),  T = accum.rgb / accum.a * reveal.rgb
//     elsewhere:          R = S + A
// (the revealage target is sampled .wxyz: its alpha is the revealage, its colour a tint of what shows through).
struct V3 { float r, g, b; };
inline V3 operator+(V3 a, V3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline V3 operator*(V3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline V3 Lerp(V3 a, V3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

inline V3 GameResolve(V3 s, const float accum[4], const float reveal[4], V3 glow) {
    if (!(accum[3] > 0.0f)) return s + glow;
    const float a = std::clamp(reveal[3], 0.0f, 1.0f);
    const V3 t = {accum[0] / accum[3] * reveal[0], accum[1] / accum[3] * reveal[1], accum[2] / accum[3] * reveal[2]};
    return s * a + t * (1.0f - a) + glow;
}

// Our last step, the composite: what the transparents add (rgb) and how much of what lies under them shows (alpha),
// blended over the fogged scene F as R = rgb + F * alpha (blend: source one, destination source alpha).
struct Over { V3 add; float keep; };
inline Over Composite(const float accum[4], const float reveal[4], V3 glow) {
    if (!(accum[3] > 0.0f)) return {glow, 1.0f};
    const float a = std::clamp(reveal[3], 0.0f, 1.0f);
    const V3 t = {accum[0] / accum[3] * reveal[0], accum[1] / accum[3] * reveal[1], accum[2] / accum[3] * reveal[2]};
    return {t * (1.0f - a) + glow, a};
}
inline V3 Blend(Over o, V3 under) { return o.add + under * o.keep; }

// ---------------------------------------------------------------- the fog's colour in the scene's units
// The game tone-maps its scene by a 3D table (ps_tonemap_p: scene x 0.008 as PQ, into the table). The table it starts
// from (cs_generate_sdr_lut_c) reads the scene as linear Rec.2020, moves it to ICtCp, bends only the intensity above a
// knee (0.403838 in PQ: a scene luminance of about 0.42) and goes back to Rec.709 and sRGB. Below the knee the whole way is
//     display = sRGB(M2020to709 * scene)
// so a display colour C is the scene colour M709to2020 * sRGB^-1(C) - the fog's colour as the post pass lays it on the
// finished picture, put where the game will tone-map it to that colour. (The area's colour grading, combined into the
// same table, is not undone: the fog in the frame is graded with the scene, as the game's own fog is.)
inline float SrgbToLinear(float c) { c = std::clamp(c, 0.0f, 1.0f); return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float LinearToSrgb(float c) { c = (std::max)(c, 0.0f); return c < 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

inline constexpr float kM709to2020[9] = {0.627404f, 0.329283f, 0.043313f, 0.069097f, 0.919541f, 0.011362f, 0.016391f, 0.088013f, 0.895595f};
inline constexpr float kM2020to709[9] = {1.660532f, -0.587643f, -0.072840f, -0.124554f, 1.132903f, -0.008348f, -0.018151f, -0.100579f, 1.118581f};
inline V3 Mul(const float m[9], V3 v) {
    return {m[0] * v.r + m[1] * v.g + m[2] * v.b, m[3] * v.r + m[4] * v.g + m[5] * v.b, m[6] * v.r + m[7] * v.g + m[8] * v.b};
}
inline V3 SceneFromDisplay(V3 c) { return Mul(kM709to2020, {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)}); }
inline V3 DisplayFromScene(V3 s) {   // below the knee
    const V3 l = Mul(kM2020to709, s);
    return {LinearToSrgb(l.r), LinearToSrgb(l.g), LinearToSrgb(l.b)};
}

// The intensity the game's curve bends above, and whether a scene colour is under it (there the colour above is exact).
inline float Pq(float y) {   // SMPTE ST 2084, y = luminance / 10000 nits
    const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    const float p = std::pow((std::max)(y, 0.0f), m1);
    return std::pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
}
inline constexpr float kKnee = 0.403838f;
inline float Intensity(V3 s) {   // ICtCp's I of a scene colour, as the game's table computes it (scene x 0.008 into PQ)
    const float l = 0.412109f * s.r + 0.523926f * s.g + 0.063965f * s.b, m = 0.166748f * s.r + 0.720459f * s.g + 0.112793f * s.b;
    return 0.5f * (Pq(l * 0.008f) + Pq(m * 0.008f));
}
inline bool BelowKnee(V3 s) { return Intensity(s) <= kKnee; }

// ---------------------------------------------------------------- the fog
// The constants the compute shader gets (root constants, in this order; vr/fog_in_frame_shader.h FogK).
struct Consts {
    float nearPlane, fogStart, fogEnd, fogCurve;   // k0: as D2R_DepthFog.fx NearPlane, FogStart, FogEnd, FogCurve
    float fogStrength, effectsApart, paint, unused3;   // k1: FogStrength; 1 = the effects drawn into the scene kept apart (E, K: Apart);
                                                       //     [debug] fog_in_frame_paint (Painted)
    float fogR, fogG, fogB, warm;                  // k2: the fog's colour in scene units, the cave's warm rule (CeilHalo; 0 = none)
    uint32_t x0, y0, x1, y1;                       // k3: the pixels the game's draw reaches (its viewport and scissor)
    uint32_t stencilOn, stencilFunc, stencilRef, stencilMask;   // k4: the game's stencil test (compare_op as ReShade numbers it)
};
static_assert(sizeof(Consts) == 20 * 4, "five float4 of root constants");

// D2R_DepthFog.fx FogAt / FogAmount (reverse-Z infinite: distance = near / depth).
inline float FogAt(const Consts& k, float dist) {
    const float x = std::clamp((dist - k.fogStart) / (std::max)(k.fogEnd - k.fogStart, 1.0f), 0.0f, 1.0f);
    return std::pow(x, k.fogCurve) * k.fogStrength;
}
inline float FogAmount(const Consts& k, float depth) { return FogAt(k, k.nearPlane / (std::max)(depth, 1e-7f)); }

// One pixel of the opaque scene, `depth` its own depth (the depth buffer and the target are one size). Fogged by that
// depth alone: before the game's anti-aliasing and upscaling a pixel of the scene is one surface at one distance. The post
// pass's farthest of five and its 3x3 blur are for the finished picture (anti-aliased edges, the far ground's specks);
// here they gave every object's edge against the far ground or the void the far fog - a rim of the fog's colour round
// the planks behind a torch, where the game's effects layer has gaps (test-0176, 2026-10-21). The cave's warm light kept.
// The void (depth 0) is left as it is: the post pass's (the sky, the ceiling, its own fog). `fogged` = false there.
inline V3 FogPixel(const Consts& k, V3 colour, float depth, bool* fogged = nullptr) {
    if (fogged) *fogged = depth > 1e-6f;
    if (!(depth > 1e-6f)) return colour;
    const float t = FogAmount(k, depth);
    const V3 fog = {k.fogR, k.fogG, k.fogB};
    V3 out = Lerp(colour, fog, t);
    if (k.warm > 0.0f) {   // in a cave a torch's warm light on the far floor keeps its light (the post pass's rule, by the colour shown)
        const V3 shown = DisplayFromScene(colour);
        const float x = std::clamp((shown.r - shown.b - 0.05f) / 0.2f, 0.0f, 1.0f), warm = x * x * (3.0f - 2.0f * x);
        out = out + colour * (t * warm * (std::min)(k.warm, 1.0f));
    }
    return out;
}

// The game's stencil test as D3D12 makes it: (ref & mask) func (stencil & mask); func as ReShade's compare_op.
inline bool StencilPasses(uint32_t func, uint32_t ref, uint32_t mask, uint32_t value) {
    const uint32_t a = ref & mask & 0xFF, b = value & mask & 0xFF;
    switch (func) {
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

// ---------------------------------------------------------------- the effects the game draws into the scene itself
// Not all of the game's fire goes through its transparent targets: some is drawn straight into the lit scene S (blended,
// no depth written) - the flame of a torch among it (test-0176: fogged by the depth of the fence behind it, pale, its
// bloom halo gone, bright in the gaps between the planks). Every such draw blends linearly:
//     S' = c Fs (op) S Fd      with Fd not depending on S (no dest colour; the target has no alpha, so dest alpha is 1)
// so the scene after any run of them is S0 K + E, where E is the same draws on black and K the product of their Fd -
// drawn with the game's own pipeline into a target of ours starting at 0 (E), and with a copy of it whose source factor
// is zero into one starting at 1 (K). Then the opaque scene alone is fogged and the effects laid back over it:
//     fog(S0) K + E
// exactly - whatever the order of the draws, whatever lists they are in. What is not linear (dest colour, min / max, a
// logic op) or also writes the surface's other targets (glass, decals: they are the surface) stays in the scene, fogged.
// ReShade's blend_factor / blend_op numbers.
namespace bf {
enum : uint32_t { zero = 0, one = 1, srcColor = 2, invSrcColor = 3, destColor = 4, invDestColor = 5, srcAlpha = 6, invSrcAlpha = 7,
                  destAlpha = 8, invDestAlpha = 9, constColor = 10, invConstColor = 11, constAlpha = 12, invConstAlpha = 13,
                  srcAlphaSat = 14, src1Color = 15, invSrc1Color = 16, src1Alpha = 17, invSrc1Alpha = 18 };
enum : uint32_t { add = 0, subtract = 1, revSubtract = 2, min = 3, max = 4 };
}  // namespace bf
struct BlendRt {
    bool enable = false, logic = false;
    uint32_t src = bf::one, dst = bf::zero, op = bf::add;
};
// Why a draw into the scene can not be taken apart, or nullptr when it can.
inline const char* WhyNotApart(const BlendRt& b, bool depthWrite, bool writesOtherTargets) {
    if (!b.enable) return "it does not blend";
    if (b.logic) return "a logic op";
    if (depthWrite) return "it writes depth (a surface)";
    if (writesOtherTargets) return "it writes the surface's other targets too (glass, decals)";
    if (b.op != bf::add && b.op != bf::subtract && b.op != bf::revSubtract) return "min / max blending";
    auto onDest = [](uint32_t f) { return f == bf::destColor || f == bf::invDestColor || f == bf::srcAlphaSat; };
    if (onDest(b.src) || onDest(b.dst)) return "a factor of the scene's own colour";
    return nullptr;
}
// The coverage's copy: nothing of its own, the scene's factor as the game's.
inline BlendRt KeepVariant(BlendRt b) { b.src = bf::zero; return b; }

// One channel of a factor (the target has no alpha: dest alpha 1).
inline float Factor(uint32_t f, float sc, float sa, float s1c, float s1a, float cc, float ca, float dc) {
    switch (f) {
        case bf::zero: return 0.0f;
        case bf::one: return 1.0f;
        case bf::srcColor: return sc;
        case bf::invSrcColor: return 1.0f - sc;
        case bf::destColor: return dc;
        case bf::invDestColor: return 1.0f - dc;
        case bf::srcAlpha: return sa;
        case bf::invSrcAlpha: return 1.0f - sa;
        case bf::destAlpha: return 1.0f;
        case bf::invDestAlpha: return 0.0f;
        case bf::constColor: return cc;
        case bf::invConstColor: return 1.0f - cc;
        case bf::constAlpha: return ca;
        case bf::invConstAlpha: return 1.0f - ca;
        case bf::srcAlphaSat: return (std::min)(sa, 0.0f);
        case bf::src1Color: return s1c;
        case bf::invSrc1Color: return 1.0f - s1c;
        case bf::src1Alpha: return s1a;
        default: return 1.0f - s1a;
    }
}
// The output-merger's blend of one channel, as D3D12 does it (for the tests' runs of draws).
inline float BlendChannel(const BlendRt& b, float dst, float sc, float sa, float s1c = 0.0f, float s1a = 0.0f, float cc = 1.0f, float ca = 1.0f) {
    if (!b.enable) return sc;
    const float s = sc * Factor(b.src, sc, sa, s1c, s1a, cc, ca, dst), d = dst * Factor(b.dst, sc, sa, s1c, s1a, cc, ca, dst);
    switch (b.op) {
        case bf::subtract: return s - d;
        case bf::revSubtract: return d - s;
        case bf::min: return (std::min)(sc, dst);
        case bf::max: return (std::max)(sc, dst);
        default: return s + d;
    }
}
// The scene put back: the opaque part fogged, the effects kept apart laid over it.
inline V3 Apart(V3 foggedScene, V3 keep, V3 effects) {
    return {foggedScene.r * keep.r + effects.r, foggedScene.g * keep.g + effects.g, foggedScene.b * keep.b + effects.b};
}

// ---------------------------------------------------------------- the post pass, where the frame left a pixel to it
// reshade/D2R_DepthFog.fx FrameVoidFog (the FOGFRAME block): a pixel with no depth of its own holds only the effects'
// light over the black void; the fog of the far goes under that light, not over it.
inline V3 FrameVoidFog(V3 light, V3 fog, float t) { return fog * t + light; }

// The runs of set bits of a mask of root constants (the game's compute constants given back run by run).
template <class F>
void ForEachRun(uint64_t mask, F f) {
    for (uint32_t a = 0; a < 64;) {
        if (!(mask >> a & 1)) { ++a; continue; }
        uint32_t b = a;
        while (b < 64 && (mask >> b & 1)) ++b;
        f(a, b - a);
        a = b;
    }
}

// ---------------------------------------------------------------- [debug] fog_in_frame_paint, the frame's side
// What the compute step writes for a pixel of the draw's area in the paint modes (vr/fog_in_frame_shader.h main):
// 2 the effects kept apart alone (holes in the game's effects show black), 3 the fogged scene alone (no effects),
// 4 as drawn, with the silhouettes of the frame's own depth green (an own depth with a neighbour of none, or one whose
// fog differs by more than a quarter) - before the game's anti-aliasing and upscaling. Else (0, 1) as drawn.
inline bool Silhouette(const Consts& k, float d, const float n[4]) {
    if (!(d > 1e-6f)) return false;
    const float t = FogAmount(k, d);
    for (int i = 0; i < 4; ++i)
        if (!(n[i] > 1e-6f) || std::fabs(FogAmount(k, n[i]) - t) > 0.25f) return true;
    return false;
}
inline V3 Painted(const Consts& k, V3 foggedScene, V3 keep, V3 effects, bool apart, bool silhouette) {
    const int paint = (int)(k.paint + 0.5f);
    const V3 e = apart ? effects : V3{0, 0, 0}, kk = apart ? keep : V3{1, 1, 1};
    if (paint == 2) return e;
    const V3 scene = {foggedScene.r * kk.r, foggedScene.g * kk.g, foggedScene.b * kk.b};
    if (paint == 3) return scene;
    if (paint == 4 && silhouette) return {0, 1, 0};
    return scene + e;
}

}  // namespace fogframe
