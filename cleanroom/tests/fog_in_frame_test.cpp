// fog_in_frame_test - [render] fog_in_frame (vr/fog_in_frame.h):
//   - the resolve's match (FNV-1a 64, the size first), and the game's own shader when it is given;
//   - our three steps against the game's resolve: the composite over the scene is the resolve, over the fogged scene it is
//     the resolve of the fogged scene;
//   - the fog's colour in the game's scene units (the inverse of its tone mapping below the knee), the stencil test;
//   - the fog of a pixel (vr/fog_in_frame_math.h FogPixel), by its own depth: the void, near, far, the cave's warm light;
//     the planks behind a torch (test-0176): no rim of the far fog round an object's edge;
//   - our shaders (vr/fog_in_frame_shader.h) compiled and run on WARP against the C++ mirror: the scene copy and the
//     composite with its blending, the compute step with its stencil and rectangle; with --game-shader the game's own
//     ps_transparent_resolve (from the player's own game files, never in this repository) on WARP against our steps;
//   - the post pass's side in reshade/D2R_DepthFog.fx (FogInFrame).
//   fog_in_frame_test [--game-shader <ps_transparent_resolve DXBC>] [--fx <D2R_DepthFog.fx>]

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fog_in_frame_math.h"
#include "fog_in_frame_shader.h"

using Microsoft::WRL::ComPtr;
using namespace fogframe;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol * (1.0f + std::fabs(b)); }
static bool Near3(V3 a, V3 b, float tol) { return Near(a.r, b.r, tol) && Near(a.g, b.g, tol) && Near(a.b, b.b, tol); }

static uint32_t g_seed = 12345;
static float Rnd(float lo, float hi) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)((g_seed >> 8) & 0xFFFF) / 65535.0f;
}

// ---------------------------------------------------------------- the match
static void Match() {
    // FNV-1a 64 with the basis vrcam's tools hashed the game's shaders with (1469598103934665603, not the standard's
    // 14695981039346656037): the values in shader_hashes and in light_bounds / fog_spare are made so
    Expect(Fnv("", 0) == 1469598103934665603ull, "the hash of nothing is the basis");
    Expect(Fnv("a", 1) == ((1469598103934665603ull ^ 'a') * 1099511628211ull), "a byte: xor, then the prime (FNV-1a, not FNV-1)");
    Expect(Fnv("ab", 2) != Fnv("ba", 2), "the order of the bytes counts");
    std::vector<uint8_t> other(kResolveSize);
    for (uint8_t& b : other) b = (uint8_t)Rnd(0, 255);
    Expect(!IsResolve(other.data(), other.size()), "a shader of the resolve's size but other bytes: not the resolve");
    Expect(!IsResolve(other.data(), other.size() - 1), "another size: not the resolve");
    Expect(!IsResolve(nullptr, kResolveSize), "no code: not the resolve");
}

// ---------------------------------------------------------------- the composite against the game's resolve
struct Px { V3 s; float accum[4], reveal[4]; V3 glow; };
static Px RandomPx(int i) {
    Px p{};
    p.s = {Rnd(0, 3), Rnd(0, 3), Rnd(0, 3)};
    const bool none = i % 5 == 0;
    for (int c = 0; c < 3; ++c) p.accum[c] = Rnd(0, 4);
    p.accum[3] = none ? 0.0f : Rnd(0.05f, 4);
    for (int c = 0; c < 3; ++c) p.reveal[c] = Rnd(0, 1);
    p.reveal[3] = i % 7 == 0 ? Rnd(-0.5f, 1.5f) : Rnd(0, 1);   // (saturated by both)
    p.glow = i % 3 ? V3{Rnd(0, 2), Rnd(0, 2), Rnd(0, 1)} : V3{0, 0, 0};
    return p;
}
static void CompositeCases() {
    for (int i = 0; i < 200; ++i) {
        const Px p = RandomPx(i);
        const Over o = Composite(p.accum, p.reveal, p.glow);
        Expect(Near3(Blend(o, p.s), GameResolve(p.s, p.accum, p.reveal, p.glow), 1e-5f), "the composite over the scene is the game's resolve");
        const V3 f = {Rnd(0, 1), Rnd(0, 1), Rnd(0, 1)};
        Expect(Near3(Blend(o, f), GameResolve(f, p.accum, p.reveal, p.glow), 1e-5f), "over the fogged scene: the resolve of the fogged scene");
        Expect(o.keep >= 0.0f && o.keep <= 1.0f && o.add.r >= 0.0f && o.add.g >= 0.0f && o.add.b >= 0.0f, "what the effects add is not negative");
    }
    // a flame (additive alone) in front of a far wall: the wall fogged, the flame added in full over it
    const float none[4] = {0, 0, 0, 0}, clear[4] = {1, 1, 1, 1};
    const V3 wall = {0.2f, 0.25f, 0.3f}, flame = {1.2f, 0.6f, 0.2f}, fogged = {0.1f, 0.1f, 0.11f};
    const V3 r = Blend(Composite(none, clear, flame), fogged);
    Expect(Near3(r, fogged + flame, 1e-6f), "a flame over a fogged wall keeps all its light");
    (void)wall;
}

// ---------------------------------------------------------------- the fog's colour, the stencil
static void ColourCases() {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) s += kM709to2020[i * 3 + k] * kM2020to709[k * 3 + j];
            Expect(std::fabs(s - (i == j ? 1.0f : 0.0f)) < 2e-4f, "Rec.709 to 2020 and back is the identity");
        }
    for (float r = 0.0f; r <= 0.6f; r += 0.1f)
        for (float g = 0.0f; g <= 0.6f; g += 0.15f)
            for (float b = 0.0f; b <= 0.6f; b += 0.2f) {
                const V3 c = {r, g, b}, sc = SceneFromDisplay(c);
                Expect(BelowKnee(sc), "a fog colour up to 0.6 lies under the game's knee");
                Expect(Near3(DisplayFromScene(sc), c, 2e-3f), "the fog's colour in scene units is shown as the colour asked for");
            }
    // the acts' own fog colours of the shipped d2r_vr.ini, and the snow's: under the knee, so exact
    const unsigned acts[] = {0x303334, 0x625233, 0x364549, 0x5A2712, 0x212934, 0x7E8D99};
    for (unsigned a : acts) {
        const V3 c = {((a >> 16) & 255) / 255.0f, ((a >> 8) & 255) / 255.0f, (a & 255) / 255.0f};
        Expect(BelowKnee(SceneFromDisplay(c)), "an act's fog colour is under the game's knee");
    }
    Expect(!BelowKnee(SceneFromDisplay({1, 1, 1})), "white is above the knee (the curve bends it)");
    // the knee in grey: the scene luminance where the intensity reaches it, about 0.42
    float lo = 0.0f, hi = 2.0f;
    for (int i = 0; i < 40; ++i) { const float m = 0.5f * (lo + hi); (Intensity({m, m, m}) < kKnee ? lo : hi) = m; }
    Expect(lo > 0.40f && lo < 0.44f, "the knee is at a grey of about 0.42 in scene units");
    Expect(Near(Pq(1.0f), 1.0f, 1e-5f) && Pq(0.0f) < 1e-6f, "PQ: 0 and 10000 nits");

    Expect(StencilPasses(7, 0, 0xFF, 0) && !StencilPasses(0, 0, 0xFF, 0), "always / never");
    Expect(StencilPasses(2, 1, 0xFF, 1) && !StencilPasses(2, 1, 0xFF, 2), "equal");
    Expect(StencilPasses(5, 0, 0xFF, 1) && !StencilPasses(5, 0, 0xFF, 0), "not equal");
    Expect(StencilPasses(1, 1, 0xFF, 2) && !StencilPasses(1, 2, 0xFF, 2), "less: ref < value");
    Expect(StencilPasses(4, 3, 0xFF, 2) && !StencilPasses(4, 2, 0xFF, 2), "greater: ref > value");
    Expect(StencilPasses(2, 0x11, 0x0F, 0x21), "the read mask on both sides");
}

// ---------------------------------------------------------------- the fog of a pixel
static Consts BaseConsts() {
    Consts k{};
    k.nearPlane = 1.5f; k.fogStart = 30.0f; k.fogEnd = 200.0f; k.fogCurve = 0.6f; k.fogStrength = 1.0f;
    const V3 f = SceneFromDisplay({0x30 / 255.0f, 0x33 / 255.0f, 0x34 / 255.0f});
    k.fogR = f.r; k.fogG = f.g; k.fogB = f.b;
    return k;
}
static float DepthOf(const Consts& k, float dist) { return dist > 0.0f ? k.nearPlane / dist : 0.0f; }   // reverse-Z infinite; 0 = the void

// What the fog in the frame did before test-0176's fix: the post pass's farthest of a pixel and its four neighbours (no blur
// here) - kept in the test only, to show the scene below is the one Serg saw.
static V3 FarthestOfFive(const Consts& k, const std::vector<float>& depth, int w, int h, int x, int y, V3 colour) {
    auto at = [&](int ax, int ay) { return depth[(size_t)std::clamp(ay, 0, h - 1) * w + std::clamp(ax, 0, w - 1)]; };
    const float d[5] = {at(x, y), at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y - 1)};
    float dNear = 0.0f, dFar = 1e30f;
    for (float v : d) { dNear = (std::max)(dNear, v); dFar = (std::min)(dFar, v); }
    if (!(dNear > 1e-6f)) return colour;
    return Lerp(colour, {k.fogR, k.fogG, k.fogB}, FogAmount(k, dFar));
}

// Runs of the blends the game's effects use, on a scene S0: S0 K + E (E the same run on black, K the coverage's copy on 1)
// is the scene after them, channel by channel - for every blend WhyNotApart lets through; what it refuses is not linear.
static void ApartCases() {
    const BlendRt add{true, false, bf::srcAlpha, bf::one, bf::add}, addOne{true, false, bf::one, bf::one, bf::add},
        over{true, false, bf::srcAlpha, bf::invSrcAlpha, bf::add}, premul{true, false, bf::one, bf::invSrcAlpha, bf::add},
        tint{true, false, bf::zero, bf::srcColor, bf::add}, sub{true, false, bf::srcAlpha, bf::one, bf::revSubtract},
        konst{true, false, bf::constColor, bf::invConstColor, bf::add}, src1{true, false, bf::one, bf::invSrc1Color, bf::add};
    const BlendRt ok[] = {add, addOne, over, premul, tint, sub, konst, src1};
    for (const BlendRt& b : ok) Expect(WhyNotApart(b, false, false) == nullptr, "a linear blend is taken apart");
    Expect(KeepVariant(over).src == bf::zero && KeepVariant(over).dst == over.dst && KeepVariant(over).op == over.op,
           "the coverage's copy: nothing of its own, the scene's factor and op as the game's");
    int bad = 0;
    for (int run = 0; run < 300; ++run) {
        float s0 = Rnd(0, 4), s = s0, e = 0.0f, k = 1.0f;
        const int n = 1 + run % 7;
        for (int i = 0; i < n; ++i) {
            const BlendRt& b = ok[(run * 3 + i) % 8];
            const float sc = Rnd(0, 3), sa = Rnd(0, 1), s1c = Rnd(0, 1), s1a = Rnd(0, 1), cc = Rnd(0, 1), ca = Rnd(0, 1);
            s = BlendChannel(b, s, sc, sa, s1c, s1a, cc, ca);
            e = BlendChannel(b, e, sc, sa, s1c, s1a, cc, ca);
            k = BlendChannel(KeepVariant(b), k, sc, sa, s1c, s1a, cc, ca);
        }
        if (std::fabs(s - (s0 * k + e)) > 1e-4f * (1.0f + std::fabs(s))) ++bad;
    }
    Expect(bad == 0, "any run of linear blends: the scene after them is S0 K + E");
    // refused: a factor of the scene's own colour (not linear), min / max, a logic op, depth written, the surface's other targets
    Expect(WhyNotApart({true, false, bf::destColor, bf::zero, bf::add}, false, false) != nullptr, "the scene's own colour as a factor: refused");
    Expect(WhyNotApart({true, false, bf::one, bf::one, bf::max}, false, false) != nullptr, "max blending: refused");
    Expect(WhyNotApart({true, true, bf::one, bf::one, bf::add}, false, false) != nullptr, "a logic op: refused");
    Expect(WhyNotApart(add, true, false) != nullptr, "depth written (a surface): refused");
    Expect(WhyNotApart(over, false, true) != nullptr, "the surface's other targets written (glass, decals): refused");
    Expect(WhyNotApart({false, false, bf::one, bf::zero, bf::add}, false, false) != nullptr, "no blending: refused");
    {   // and why: with the scene's own colour as a factor S0 K + E is not the scene
        const BlendRt mul{true, false, bf::destColor, bf::zero, bf::add};
        const float s0 = 0.5f, sc = 0.8f;
        const float s = BlendChannel(mul, s0, sc, 1), e = BlendChannel(mul, 0.0f, sc, 1), k = BlendChannel(KeepVariant(mul), 1.0f, sc, 1);
        Expect(std::fabs(s - (s0 * k + e)) > 0.1f, "(the scene's colour as a factor is not linear - rightly refused)");
    }
}

// Serg's torch (test-0176, screenshot 128): its flame is drawn additively into the scene itself, over the fence's planks (60
// away) and over the gaps between them (the void). Fogging the scene as a whole fogged the flame by the depth behind it:
// pale on the planks, at full light in the gaps - bright streaks, and less light for the game's bloom (no halo). Kept
// apart, the flame is the same over a plank and over a gap, and every plank is fogged as before.
static void FlameCases() {
    const Consts k = BaseConsts();
    const V3 fog = {k.fogR, k.fogG, k.fogB}, plank = {0.30f, 0.17f, 0.07f}, voidPx = {0, 0, 0};
    const BlendRt add{true, false, bf::srcAlpha, bf::one, bf::add};
    const V3 flameColour = {6.0f, 3.2f, 0.9f};
    const float flameAlpha = 0.8f;
    auto drawFlame = [&](V3 dst) {
        return V3{BlendChannel(add, dst.r, flameColour.r, flameAlpha), BlendChannel(add, dst.g, flameColour.g, flameAlpha),
                  BlendChannel(add, dst.b, flameColour.b, flameAlpha)};
    };
    auto keepOf = [&](V3 dst) {
        const BlendRt kv = KeepVariant(add);
        return V3{BlendChannel(kv, dst.r, flameColour.r, flameAlpha), BlendChannel(kv, dst.g, flameColour.g, flameAlpha),
                  BlendChannel(kv, dst.b, flameColour.b, flameAlpha)};
    };
    const float dPlank = DepthOf(k, 60.0f);
    // as before this fix: the scene with the flame in it, fogged as a whole
    const V3 oldPlank = FogPixel(k, drawFlame(plank), dPlank), oldGap = FogPixel(k, drawFlame(voidPx), 0.0f);
    // now: the flame into E (on black) and K (on 1), the opaque scene fogged, the flame laid back
    const V3 e = drawFlame(voidPx), kp = keepOf({1, 1, 1});
    const V3 newPlank = Apart(FogPixel(k, plank, dPlank), kp, e), newGap = Apart(FogPixel(k, voidPx, 0.0f), kp, e);
    const V3 flame = {flameColour.r * flameAlpha, flameColour.g * flameAlpha, flameColour.b * flameAlpha};
    Expect(oldGap.r - (oldPlank.r - FogPixel(k, plank, dPlank).r) > 1.0f, "before: the flame over a gap brighter than over a plank (the streaks)");
    Expect(Near3(newPlank, FogPixel(k, plank, dPlank) + flame, 1e-5f), "now: over a plank, the plank's own fog and the flame's full light");
    Expect(Near3(newGap, flame, 1e-5f), "now: over a gap, the same flame (the void left to the post pass)");
    Expect(Near3(newPlank + V3{-FogPixel(k, plank, dPlank).r, -FogPixel(k, plank, dPlank).g, -FogPixel(k, plank, dPlank).b}, newGap, 1e-5f),
           "the flame adds the same light over a plank and over a gap: no streaks");
    Expect(newPlank.r > oldPlank.r + 1.0f, "the flame over the planks as bright as the game draws it (light enough for its bloom)");
    (void)fog;
}

// Serg's 128, the thin lines: a row across the fence behind the torch - planks 60 away, a gap of the void between them
// (its two pixels next to the planks have no depth of their own but a plank beside: the post pass's rim; the ones between
// are the void proper) - the flame's light over all of it. The frame fogs the planks and keeps the flame apart; what has no
// depth of its own it leaves to the post pass. The post pass as it was fogged a rim pixel by the farthest of five - the
// void's full fog, the flame's light with it: a thin line on each side of every gap - and faded the flame over the sky by
// the fog of the planks below. Now on every pixel the flame adds the same light over what lies behind it.
struct PostOut { V3 out, behind; };
static PostOut PostPass(const Consts& k, const std::vector<float>& depth, int x, V3 colour, V3 sky, bool old) {
    const int w = (int)depth.size();
    auto at = [&](int i) { return depth[(size_t)std::clamp(i, 0, w - 1)]; };
    const float d = at(x);
    const float dNear = (std::max)(d, (std::max)(at(x - 1), at(x + 1))), dFar = (std::min)(d, (std::min)(at(x - 1), at(x + 1)));
    const V3 fog = {k.fogR, k.fogG, k.fogB};
    if (!(dNear > 1e-6f)) {   // the void proper: the sky, the effects' light laid over it - faded by the fog of the ground below
        float below = 0.0f;
        for (int i = 0; i < w; ++i) below = (std::max)(below, depth[(size_t)i]);   // (the planks below it)
        const float tf = FogAmount(k, below);
        return {sky + colour * (old ? 1.0f - tf : 1.0f), sky};
    }
    if (d > 1e-6f) return {colour, colour};   // its own depth: the frame's (the frame fogged it), left as it is
    const float t = FogAmount(k, dFar);       // a rim: no depth of its own, a plank beside
    if (old) return {Lerp(colour, fog, t), fog * t};
    return {FrameVoidFog(colour, fog, t), fog * t};
}
static void FenceRowCases() {
    const Consts k = BaseConsts();
    const float dPlank = DepthOf(k, 60.0f);
    const std::vector<float> depth = {dPlank, dPlank, dPlank, 0.0f, 0.0f, 0.0f, 0.0f, dPlank, dPlank, dPlank};
    const V3 plank = {0.30f, 0.17f, 0.07f}, sky = {0.20f, 0.22f, 0.26f}, flame = {4.8f, 2.6f, 0.7f};
    int linesBefore = 0, linesNow = 0, rims = 0, voids = 0;
    for (int x = 0; x < (int)depth.size(); ++x) {
        const bool hasDepth = depth[(size_t)x] > 0.0f;
        // the frame: the plank fogged, the flame kept apart and laid back (K 1, E the flame); the void left black under it
        const V3 framed = Apart(FogPixel(k, hasDepth ? plank : V3{0, 0, 0}, depth[(size_t)x]), {1, 1, 1}, flame);
        const V3 fogOfPlank = FogPixel(k, plank, depth[(size_t)x]);
        for (int old = 0; old < 2; ++old) {
            const PostOut o = PostPass(k, depth, x, framed, sky, old != 0);
            const V3 behind = hasDepth ? fogOfPlank : o.behind;   // what lies behind the flame there, as shown
            const V3 light = o.out + V3{-behind.r, -behind.g, -behind.b};
            const bool line = !Near3(light, flame, 1e-4f);
            if (old) linesBefore += line ? 1 : 0;
            else linesNow += line ? 1 : 0;
        }
        const bool rim = !hasDepth && ((x > 0 && depth[(size_t)x - 1] > 0.0f) || (x + 1 < (int)depth.size() && depth[(size_t)x + 1] > 0.0f));
        rims += rim ? 1 : 0;
        voids += !hasDepth && !rim ? 1 : 0;
    }
    Expect(rims == 2 && voids == 2, "the row has two rim pixels and two of the void proper");
    Expect(linesBefore == 4, "before: the flame's light lost on the two rims and faded on the void - the thin lines of 128");
    Expect(linesNow == 0, "now: the flame adds the same light on the planks, the rims and the void - no lines");
}

// Serg's 133 (the halo cut into vertical stripes) and 132 (dark lines along the planks) were taken while the frame's fog
// never ran (the log: "the game's own compute work earlier in the resolve's list ... left to the game" - its light
// culling is in that list): both are the post pass's own rules. The halo over the sky is faded by the fog of the first
// surface below each column (D2R_DepthFog.fx PS_VoidFog); above the torch's basket that is a bar (near, no fog) or the far
// ground between them (all fog) - column by column: stripes. With the frame's fog drawn (FogInFrame) the post pass leaves
// the effects' light over the void whole (4.3): the same halo in every column.
static void HaloColumnsCases() {
    const Consts k = BaseConsts();
    const int w = 24;
    const V3 sky = {0.20f, 0.22f, 0.26f}, halo = {0.9f, 0.45f, 0.12f};
    std::vector<float> below(w);
    for (int x = 0; x < w; ++x) below[(size_t)x] = (x / 3) % 2 ? DepthOf(k, 6.0f) : DepthOf(k, 400.0f);   // the basket's bars, the far ground between
    auto shown = [&](int x, bool frameDrawn) {   // the post pass's void branch for the halo over the sky at column x (VoidFog: the fog below)
        const float tf = FogAmount(k, below[(size_t)x]);
        const float fade = frameDrawn ? 1.0f : 1.0f - tf;
        return sky + halo * fade;
    };
    float lo = 1e9f, hi = -1e9f, loNow = 1e9f, hiNow = -1e9f;
    for (int x = 0; x < w; ++x) {
        lo = (std::min)(lo, shown(x, false).r); hi = (std::max)(hi, shown(x, false).r);
        loNow = (std::min)(loNow, shown(x, true).r); hiNow = (std::max)(hiNow, shown(x, true).r);
    }
    Expect(hi - lo > 0.5f, "the post pass alone: the halo striped by the columns' fog below (133)");
    Expect(hiNow - loNow < 1e-6f, "with the frame's fog drawn: the same halo in every column");
    // the game's compute constants given back run by run
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    ForEachRun(0b1110011ull | (1ull << 63), [&](uint32_t a, uint32_t n) { runs.push_back({a, n}); });
    Expect(runs.size() == 3 && runs[0] == std::make_pair(0u, 2u) && runs[1] == std::make_pair(4u, 3u) && runs[2] == std::make_pair(63u, 1u),
           "the compute constants given back as the runs the game set");
    runs.clear();
    ForEachRun(~0ull, [&](uint32_t a, uint32_t n) { runs.push_back({a, n}); });
    Expect(runs.size() == 1 && runs[0] == std::make_pair(0u, 64u), "all 64 in one run");
}

static void FogCases() {
    Consts k = BaseConsts();
    const V3 c = {0.3f, 0.2f, 0.1f}, fog = {k.fogR, k.fogG, k.fogB};
    bool fogged = true;
    Expect(Near3(FogPixel(k, c, 0.0f, &fogged), c, 0) && !fogged, "the void is left to the post pass");
    Expect(Near3(FogPixel(k, c, DepthOf(k, 10.0f), &fogged), c, 1e-6f) && fogged, "near: no fog");
    Expect(Near3(FogPixel(k, c, DepthOf(k, 1000.0f)), fog, 1e-5f), "far: all fog");
    const float t = std::pow((100.0f - 30.0f) / 170.0f, 0.6f);
    Expect(Near3(FogPixel(k, c, DepthOf(k, 100.0f)), Lerp(c, fog, t), 1e-5f), "the middle: the post pass's curve");
    // the cave's warm light: on, a warm far floor keeps some of its light; grey stone as before
    const V3 warmFloor = SceneFromDisplay({0.8f, 0.4f, 0.1f}), grey = SceneFromDisplay({0.4f, 0.4f, 0.4f});
    const V3 cold = FogPixel(k, warmFloor, DepthOf(k, 100.0f));
    k.warm = 1.0f;
    Expect(FogPixel(k, warmFloor, DepthOf(k, 100.0f)).r > cold.r + 1e-3f, "in a cave the warm light on the floor keeps its light");
    Consts noWarm = k;
    noWarm.warm = 0.0f;
    Expect(Near3(FogPixel(k, grey, DepthOf(k, 100.0f)), FogPixel(noWarm, grey, DepthOf(k, 100.0f)), 1e-6f), "grey stone is fogged as before");
}

// Serg's first picture (test-0176, 126): a fence of planks 60 units away, the void (or the far ground) in the gaps behind, a
// torch's light on the planks and the effects laid over them (here with none on the edge pixels, so the scene's own fog
// there shows). Every pixel of a plank, its edge too, must come out as the plank's own fog plus the effects; the farthest
// of five gave the edges the far fog: a rim of the fog's colour.
static void PlankCases() {
    const Consts k = BaseConsts();
    const int w = 32, h = 16;
    std::vector<float> depth((size_t)w * h);
    std::vector<V3> scene((size_t)w * h), glow((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool plank = (y % 5) >= 1 && (y % 5) <= 3 && x > 3 && x < w - 4;   // planks three rows tall, gaps of two
            const bool gapFar = x >= w / 2;                        // behind the right half the far ground, the left half the void
            depth[(size_t)y * w + x] = plank ? DepthOf(k, 60.0f) : gapFar ? DepthOf(k, 900.0f) : 0.0f;
            scene[(size_t)y * w + x] = plank ? V3{0.55f, 0.32f, 0.12f} : gapFar ? V3{0.05f, 0.05f, 0.05f} : V3{0, 0, 0};   // the plank lit by the torch
        }
    auto isPlank = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && depth[(size_t)y * w + x] == DepthOf(k, 60.0f); };
    int edges = 0, rimsBefore = 0, rimsNow = 0;
    const float none[4] = {0, 0, 0, 0}, clear[4] = {1, 1, 1, 1};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!isPlank(x, y)) continue;
            const bool edge = !isPlank(x + 1, y) || !isPlank(x - 1, y) || !isPlank(x, y + 1) || !isPlank(x, y - 1);
            const V3 flame = edge ? V3{0, 0, 0} : V3{1.5f, 0.9f, 0.3f};   // (none on the edge: the scene's fog there is what shows)
            const V3 s = scene[(size_t)y * w + x];
            const V3 own = Lerp(s, {k.fogR, k.fogG, k.fogB}, FogAmount(k, DepthOf(k, 60.0f)));
            const V3 now = Blend(Composite(none, clear, flame), FogPixel(k, s, depth[(size_t)y * w + x]));
            const V3 before = Blend(Composite(none, clear, flame), FarthestOfFive(k, depth, w, h, x, y, s));
            if (!edge) continue;
            ++edges;
            if (!Near3(before, own + flame, 1e-3f)) ++rimsBefore;
            if (!Near3(now, own + flame, 1e-5f)) ++rimsNow;
        }
    Expect(edges > 20, "the fence has edges against the void and the far ground");
    Expect(rimsBefore == edges, "the farthest of five (before the fix) rimmed every edge of the planks with the far fog");
    Expect(rimsNow == 0, "now every edge pixel of a plank has the plank's own fog: no rim");
}

// ---------------------------------------------------------------- WARP
struct Gpu { ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; };
static bool MakeGpu(Gpu& g) {
    return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g.dev, nullptr, &g.ctx));
}
static ComPtr<ID3DBlob> Build(const char* src, size_t n, const char* entry, const char* target) {
    ComPtr<ID3DBlob> b, e;
    if (FAILED(D3DCompile(src, n, "fog_in_frame.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
        printf("FAIL: compile %s: %s\n", entry, e ? (const char*)e->GetBufferPointer() : "?");
        ++g_fail;
        return nullptr;
    }
    return b;
}
struct Tex { ComPtr<ID3D11Texture2D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11UnorderedAccessView> uav; };
static Tex MakeTex(Gpu& g, DXGI_FORMAT f, UINT w, UINT h, const void* data, UINT pitch, UINT bind) {
    Tex x;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1; d.Format = f; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA init{data, pitch, 0};
    if (FAILED(g.dev->CreateTexture2D(&d, data ? &init : nullptr, &x.t))) return x;
    if (bind & D3D11_BIND_SHADER_RESOURCE) g.dev->CreateShaderResourceView(x.t.Get(), nullptr, &x.srv);
    if (bind & D3D11_BIND_RENDER_TARGET) g.dev->CreateRenderTargetView(x.t.Get(), nullptr, &x.rtv);
    if (bind & D3D11_BIND_UNORDERED_ACCESS) g.dev->CreateUnorderedAccessView(x.t.Get(), nullptr, &x.uav);
    return x;
}
static std::vector<float> ReadBack(Gpu& g, ID3D11Texture2D* t, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC d{};
    t->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> s;
    std::vector<float> out((size_t)w * h * 4, -1.0f);
    if (FAILED(g.dev->CreateTexture2D(&d, nullptr, &s))) return out;
    g.ctx->CopyResource(s.Get(), t);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(g.ctx->Map(s.Get(), 0, D3D11_MAP_READ, 0, &m))) return out;
    for (UINT y = 0; y < h; ++y) memcpy(&out[(size_t)y * w * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, w * 16);
    g.ctx->Unmap(s.Get(), 0);
    return out;
}

// The game's vertex shader's outputs, as its resolve reads them: SV_Position, TEXCOORD0 (one triangle over the target).
static const char kVs[] =
    "void VS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {\n"
    "    uv = float2((id << 1) & 2, id & 2);\n"
    "    pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

static void WarpPixel(const std::vector<uint8_t>& gameShader) {
    Gpu g;
    if (!MakeGpu(g)) { printf("FAIL: no WARP device\n"); ++g_fail; return; }
    const UINT w = 64, h = 1;
    ComPtr<ID3DBlob> vsb = Build(kVs, sizeof kVs - 1, "VS", "vs_5_0");
    ComPtr<ID3DBlob> copyb = Build(kPixel, sizeof kPixel - 1, "CopyScene", "ps_5_0");
    ComPtr<ID3DBlob> compb = Build(kPixel, sizeof kPixel - 1, "Composite", "ps_5_0");
    if (!vsb || !copyb || !compb) return;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> copyPs, compPs, gamePs;
    g.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    g.dev->CreatePixelShader(copyb->GetBufferPointer(), copyb->GetBufferSize(), nullptr, &copyPs);
    g.dev->CreatePixelShader(compb->GetBufferPointer(), compb->GetBufferSize(), nullptr, &compPs);
    if (!gameShader.empty() && FAILED(g.dev->CreatePixelShader(gameShader.data(), gameShader.size(), nullptr, &gamePs))) {
        printf("FAIL: the game's resolve shader could not be made on WARP\n");
        ++g_fail;
    }
    std::vector<Px> px(w);
    std::vector<float> s(w * 4), acc(w * 4), rev(w * 4), glow(w * 4), fog(w * 4);
    for (UINT i = 0; i < w; ++i) {
        px[i] = RandomPx((int)i);
        const float sv[4] = {px[i].s.r, px[i].s.g, px[i].s.b, 1}, gv[4] = {px[i].glow.r, px[i].glow.g, px[i].glow.b, 0};
        memcpy(&s[i * 4], sv, 16);
        memcpy(&acc[i * 4], px[i].accum, 16);
        memcpy(&rev[i * 4], px[i].reveal, 16);
        memcpy(&glow[i * 4], gv, 16);
        const float fv[4] = {Rnd(0, 1), Rnd(0, 1), Rnd(0, 1), 0};
        memcpy(&fog[i * 4], fv, 16);
    }
    const DXGI_FORMAT F = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Tex ts = MakeTex(g, F, w, h, s.data(), w * 16, D3D11_BIND_SHADER_RESOURCE), ta = MakeTex(g, F, w, h, acc.data(), w * 16, D3D11_BIND_SHADER_RESOURCE),
        tr = MakeTex(g, F, w, h, rev.data(), w * 16, D3D11_BIND_SHADER_RESOURCE), tg = MakeTex(g, F, w, h, glow.data(), w * 16, D3D11_BIND_SHADER_RESOURCE);
    Tex copyRt = MakeTex(g, F, w, h, nullptr, 0, D3D11_BIND_RENDER_TARGET), overRt = MakeTex(g, F, w, h, fog.data(), w * 16, D3D11_BIND_RENDER_TARGET),
        overScene = MakeTex(g, F, w, h, s.data(), w * 16, D3D11_BIND_RENDER_TARGET), gameRt = MakeTex(g, F, w, h, nullptr, 0, D3D11_BIND_RENDER_TARGET);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> smp;
    g.dev->CreateSamplerState(&sd, &smp);
    D3D11_BLEND_DESC bd{};   // ours: R = rgb + R x alpha
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> over;
    g.dev->CreateBlendState(&bd, &over);
    ID3D11ShaderResourceView* srvs[4] = {ts.srv.Get(), ta.srv.Get(), tr.srv.Get(), tg.srv.Get()};
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->VSSetShader(vs.Get(), nullptr, 0);
    g.ctx->PSSetShaderResources(10, 4, srvs);
    g.ctx->PSSetSamplers(0, 1, smp.GetAddressOf());
    g.ctx->RSSetViewports(1, &vp);
    auto drawInto = [&](ID3D11RenderTargetView* rtv, ID3D11PixelShader* ps, ID3D11BlendState* blend) {
        g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
        g.ctx->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
        g.ctx->PSSetShader(ps, nullptr, 0);
        g.ctx->Draw(3, 0);
    };
    drawInto(copyRt.rtv.Get(), copyPs.Get(), nullptr);
    drawInto(overRt.rtv.Get(), compPs.Get(), over.Get());
    drawInto(overScene.rtv.Get(), compPs.Get(), over.Get());
    if (gamePs) drawInto(gameRt.rtv.Get(), gamePs.Get(), nullptr);
    const std::vector<float> copied = ReadBack(g, copyRt.t.Get(), w, h), overF = ReadBack(g, overRt.t.Get(), w, h),
                             overS = ReadBack(g, overScene.t.Get(), w, h), game = ReadBack(g, gameRt.t.Get(), w, h);
    int bad = 0, badGame = 0;
    for (UINT i = 0; i < w; ++i) {
        const V3 c = {copied[i * 4], copied[i * 4 + 1], copied[i * 4 + 2]};
        if (!Near3(c, px[i].s, 1e-6f)) ++bad;
        const V3 f = {fog[i * 4], fog[i * 4 + 1], fog[i * 4 + 2]};
        const V3 o = {overF[i * 4], overF[i * 4 + 1], overF[i * 4 + 2]};
        if (!Near3(o, GameResolve(f, px[i].accum, px[i].reveal, px[i].glow), 1e-5f)) ++bad;
        const V3 os = {overS[i * 4], overS[i * 4 + 1], overS[i * 4 + 2]};
        if (!Near3(os, GameResolve(px[i].s, px[i].accum, px[i].reveal, px[i].glow), 1e-5f)) ++bad;
        if (gamePs) {
            const V3 gm = {game[i * 4], game[i * 4 + 1], game[i * 4 + 2]};
            if (!Near3(os, gm, 1e-5f) || !Near3(gm, GameResolve(px[i].s, px[i].accum, px[i].reveal, px[i].glow), 1e-5f)) {
                if (badGame < 3)
                    printf("  pixel %u: the game's %.5f %.5f %.5f, ours %.5f %.5f %.5f\n", i, gm.r, gm.g, gm.b, os.r, os.g, os.b);
                ++badGame;
            }
        }
    }
    Expect(bad == 0, "WARP: the scene copy, and the composite over the fogged scene and over the scene, as the C++ mirror");
    if (gamePs) {
        Expect(badGame == 0, "WARP: the game's own resolve and our steps over the unfogged scene write the same, pixel by pixel");
        if (!badGame) printf("ok: the game's own resolve on WARP = our copy + composite (%u pixels)\n", w);
    }
}

static D3D11_BLEND D11(uint32_t f) {
    switch (f) {
        case bf::zero: return D3D11_BLEND_ZERO;
        case bf::one: return D3D11_BLEND_ONE;
        case bf::srcColor: return D3D11_BLEND_SRC_COLOR;
        case bf::invSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
        case bf::srcAlpha: return D3D11_BLEND_SRC_ALPHA;
        case bf::invSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case bf::constColor: return D3D11_BLEND_BLEND_FACTOR;
        case bf::invConstColor: return D3D11_BLEND_INV_BLEND_FACTOR;
        default: return D3D11_BLEND_ONE;
    }
}
static void WarpApart() {
    Gpu g;
    if (!MakeGpu(g)) { printf("FAIL: no WARP device\n"); ++g_fail; return; }
    const UINT w = 16, h = 1;
    static const char kSrc[] =
        "cbuffer C : register(b0) { float4 col; };\n"
        "void VS(uint id : SV_VertexID, out float4 pos : SV_Position) { const float2 t = float2((id << 1) & 2, id & 2); pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
        "float4 PS(float4 p : SV_Position) : SV_Target { return float4(col.rgb * (1.0 + 0.1 * p.x), col.a); }\n";
    ComPtr<ID3DBlob> vsb = Build(kSrc, sizeof kSrc - 1, "VS", "vs_5_0"), psb = Build(kSrc, sizeof kSrc - 1, "PS", "ps_5_0");
    if (!vsb || !psb) return;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    g.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    g.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    std::vector<float> s0(w * 4), zero(w * 4, 0.0f), one(w * 4, 1.0f);
    for (UINT i = 0; i < w; ++i) { s0[i * 4] = Rnd(0, 2); s0[i * 4 + 1] = Rnd(0, 2); s0[i * 4 + 2] = Rnd(0, 2); s0[i * 4 + 3] = 1; }
    const DXGI_FORMAT F = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Tex scene = MakeTex(g, F, w, h, s0.data(), w * 16, D3D11_BIND_RENDER_TARGET), eff = MakeTex(g, F, w, h, zero.data(), w * 16, D3D11_BIND_RENDER_TARGET),
        keep = MakeTex(g, F, w, h, one.data(), w * 16, D3D11_BIND_RENDER_TARGET);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    g.dev->CreateBuffer(&bd, nullptr, &cb);
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->VSSetShader(vs.Get(), nullptr, 0);
    g.ctx->PSSetShader(ps.Get(), nullptr, 0);
    g.ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
    g.ctx->RSSetViewports(1, &vp);
    const BlendRt run[] = {{true, false, bf::srcAlpha, bf::one, bf::add}, {true, false, bf::srcAlpha, bf::invSrcAlpha, bf::add},
                           {true, false, bf::one, bf::invSrcAlpha, bf::add}, {true, false, bf::zero, bf::srcColor, bf::add},
                           {true, false, bf::constColor, bf::invConstColor, bf::add}, {true, false, bf::srcAlpha, bf::one, bf::revSubtract}};
    const float blendFactor[4] = {0.3f, 0.6f, 0.9f, 1.0f};
    for (const BlendRt& b : run) {
        const float col[4] = {Rnd(0, 2), Rnd(0, 2), Rnd(0, 2), Rnd(0, 1)};
        g.ctx->UpdateSubresource(cb.Get(), 0, nullptr, col, 0, 0);
        for (int target = 0; target < 3; ++target) {   // the scene, E (the game's own blend), K (the coverage's copy)
            const BlendRt use = target == 2 ? KeepVariant(b) : b;
            D3D11_BLEND_DESC d{};
            d.RenderTarget[0].BlendEnable = TRUE;
            d.RenderTarget[0].SrcBlend = D11(use.src);
            d.RenderTarget[0].DestBlend = D11(use.dst);
            d.RenderTarget[0].BlendOp = use.op == bf::revSubtract ? D3D11_BLEND_OP_REV_SUBTRACT : use.op == bf::subtract ? D3D11_BLEND_OP_SUBTRACT : D3D11_BLEND_OP_ADD;
            d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
            d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            ComPtr<ID3D11BlendState> st;
            g.dev->CreateBlendState(&d, &st);
            ID3D11RenderTargetView* rtv = target == 0 ? scene.rtv.Get() : target == 1 ? eff.rtv.Get() : keep.rtv.Get();
            g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
            g.ctx->OMSetBlendState(st.Get(), blendFactor, 0xFFFFFFFF);
            g.ctx->Draw(3, 0);
        }
    }
    const std::vector<float> sv = ReadBack(g, scene.t.Get(), w, h), ev = ReadBack(g, eff.t.Get(), w, h), kv = ReadBack(g, keep.t.Get(), w, h);
    int bad = 0;
    for (UINT i = 0; i < w; ++i)
        for (int c = 0; c < 3; ++c) {
            const float want = sv[i * 4 + c], got = s0[i * 4 + c] * kv[i * 4 + c] + ev[i * 4 + c];
            if (std::fabs(want - got) > 1e-4f * (1.0f + std::fabs(want))) {
                if (bad < 3) printf("  pixel %u channel %d: the scene %.5f, S0 K + E %.5f\n", i, c, want, got);
                ++bad;
            }
        }
    Expect(bad == 0, "WARP: the GPU's own blending - a run of the effects' blends into the scene is S0 K + E (E the game's blend on 0, K its coverage's copy on 1)");
}

static void WarpCompute() {
    Gpu g;
    if (!MakeGpu(g)) { printf("FAIL: no WARP device\n"); ++g_fail; return; }
    const UINT w = 48, h = 20;
    ComPtr<ID3DBlob> csb = Build(kFog, sizeof kFog - 1, "main", "cs_5_0");
    if (!csb) return;
    ComPtr<ID3D11ComputeShader> cs;
    g.dev->CreateComputeShader(csb->GetBufferPointer(), csb->GetBufferSize(), nullptr, &cs);
    std::vector<float> depth(w * h), scene(w * h * 4), frame(w * h * 4), effects(w * h * 4), keep(w * h * 4);
    std::vector<uint32_t> stencil(w * h * 2);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            const UINT i = y * w + x;
            const float dist = x < 6 ? 0.0f : 5.0f + 7.0f * (float)x + 3.0f * (float)y;   // the void at the left, then nearer to farther
            depth[i] = dist > 0.0f ? 1.5f / dist : 0.0f;
            if (x == 20 && y == 10) depth[i] = 0.0f;   // a hole
            if (x == 30 && y == 5) depth[i] = 1.5f / 8.0f;   // a near speck
            if (y % 6 < 2 && x > 2 && x < 40) depth[i] = 1.5f / 60.0f;   // planks 60 away, across the void and the farther ground: edges
            const float sc[4] = {Rnd(0, 1), Rnd(0, 0.8f), Rnd(0, 0.5f), 0}, fr[4] = {Rnd(0, 2), Rnd(0, 2), Rnd(0, 2), 0};
            memcpy(&scene[i * 4], sc, 16);
            memcpy(&frame[i * 4], fr, 16);
            const bool flame = x > 12 && x < 30;   // a flame drawn into the scene over the middle: what it added, what it left
            const float ef[4] = {flame ? Rnd(0, 3) : 0.0f, flame ? Rnd(0, 2) : 0.0f, flame ? Rnd(0, 1) : 0.0f, 0},
                        kp[4] = {flame ? Rnd(0.2f, 1) : 1.0f, flame ? Rnd(0.2f, 1) : 1.0f, flame ? Rnd(0.2f, 1) : 1.0f, 0};
            memcpy(&effects[i * 4], ef, 16);
            memcpy(&keep[i * 4], kp, 16);
            stencil[i * 2 + 1] = (x + y) % 4 == 0 ? 1u : 0u;
        }
    Tex td = MakeTex(g, DXGI_FORMAT_R32_FLOAT, w, h, depth.data(), w * 4, D3D11_BIND_SHADER_RESOURCE);
    Tex ts = MakeTex(g, DXGI_FORMAT_R32G32_UINT, w, h, stencil.data(), w * 8, D3D11_BIND_SHADER_RESOURCE);
    Tex tsc = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, scene.data(), w * 16, D3D11_BIND_SHADER_RESOURCE);
    Tex tf = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, frame.data(), w * 16, D3D11_BIND_SHADER_RESOURCE);
    Tex to = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
    Tex te = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, effects.data(), w * 16, D3D11_BIND_SHADER_RESOURCE);
    Tex tk = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, keep.data(), w * 16, D3D11_BIND_SHADER_RESOURCE);
    struct Run { const char* what; float warm, apart; uint32_t stencilOn, func, ref; uint32_t x0, y0, x1, y1; int paint = 0; };
    const Run runs[] = {
        {"WARP: the fog by each pixel's own depth, no stencil, the whole target", 0.0f, 0.0f, 0, 7, 0, 0, 0, w, h},
        {"WARP: the fog with a stencil (not equal 1) and a smaller rectangle", 0.0f, 0.0f, 1, 5, 1, 3, 2, w - 5, h - 3},
        {"WARP: the cave's warm rule, a stencil (equal 0)", 1.0f, 0.0f, 1, 2, 0, 0, 0, w, h},
        {"WARP: the effects drawn into the scene kept apart: fog(S) K + E", 0.0f, 1.0f, 0, 7, 0, 0, 0, w, h},
        {"WARP: kept apart, with a stencil and a smaller rectangle", 0.0f, 1.0f, 1, 5, 1, 3, 2, w - 5, h - 3},
        {"WARP: [debug] fog_in_frame_paint 2 - the effects kept apart alone", 0.0f, 1.0f, 0, 7, 0, 0, 0, w, h, 2},
        {"WARP: [debug] fog_in_frame_paint 3 - the fogged scene alone", 0.0f, 1.0f, 0, 7, 0, 0, 0, w, h, 3},
        {"WARP: [debug] fog_in_frame_paint 4 - the frame's own silhouettes green", 0.0f, 1.0f, 0, 7, 0, 0, 0, w, h, 4},
    };
    for (const Run& r : runs) {
        Consts k = BaseConsts();
        k.fogStart = 40.0f; k.fogEnd = 300.0f;
        k.warm = r.warm;
        k.effectsApart = r.apart;
        k.paint = (float)r.paint;
        k.stencilOn = r.stencilOn; k.stencilFunc = r.func; k.stencilRef = r.ref; k.stencilMask = 0xFF;
        k.x0 = r.x0; k.y0 = r.y0; k.x1 = r.x1; k.y1 = r.y1;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof k;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init{&k, 0, 0};
        ComPtr<ID3D11Buffer> cb;
        g.dev->CreateBuffer(&bd, &init, &cb);
        ID3D11ShaderResourceView* srvs[6] = {td.srv.Get(), ts.srv.Get(), tsc.srv.Get(), tf.srv.Get(), te.srv.Get(), tk.srv.Get()};
        g.ctx->CSSetShader(cs.Get(), nullptr, 0);
        g.ctx->CSSetConstantBuffers(0, 1, cb.GetAddressOf());
        g.ctx->CSSetShaderResources(0, 6, srvs);
        g.ctx->CSSetUnorderedAccessViews(0, 1, to.uav.GetAddressOf(), nullptr);
        g.ctx->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        ID3D11UnorderedAccessView* none = nullptr;
        g.ctx->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
        const std::vector<float> out = ReadBack(g, to.t.Get(), w, h);
        int bad = 0, fogged = 0, kept = 0;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                const size_t i = (size_t)y * w + x;
                const bool inside = x >= k.x0 && y >= k.y0 && x < k.x1 && y < k.y1 &&
                                    (!k.stencilOn || StencilPasses(k.stencilFunc, k.stencilRef, k.stencilMask, stencil[i * 2 + 1]));
                const V3 sc = {scene[i * 4], scene[i * 4 + 1], scene[i * 4 + 2]}, fr = {frame[i * 4], frame[i * 4 + 1], frame[i * 4 + 2]};
                const V3 e = {effects[i * 4], effects[i * 4 + 1], effects[i * 4 + 2]}, kp = {keep[i * 4], keep[i * 4 + 1], keep[i * 4 + 2]};
                auto dAt = [&](int ax, int ay) { return depth[(size_t)std::clamp(ay, 0, (int)h - 1) * w + std::clamp(ax, 0, (int)w - 1)]; };
                const float n4[4] = {dAt((int)x + 1, (int)y), dAt((int)x - 1, (int)y), dAt((int)x, (int)y + 1), dAt((int)x, (int)y - 1)};
                const V3 want = !inside ? fr : Painted(k, FogPixel(k, sc, depth[i]), kp, e, r.apart > 0.5f, Silhouette(k, depth[i], n4));
                const V3 got = {out[i * 4], out[i * 4 + 1], out[i * 4 + 2]};
                if (!Near3(got, want, 2e-4f)) {
                    if (bad < 3) printf("  %s: pixel %u,%u got %.5f %.5f %.5f, want %.5f %.5f %.5f\n", r.what, x, y, got.r, got.g, got.b, want.r, want.g, want.b);
                    ++bad;
                }
                if (inside && !Near3(want, sc, 1e-4f)) ++fogged;
                if (!inside) ++kept;
            }
        Expect(bad == 0, r.what);
        Expect(fogged > 0, "the run fogged something (not a test of nothing)");
        if (r.stencilOn || r.x1 < w) Expect(kept > 0, "the run left some pixels to the target (stencil, rectangle)");
    }
}

// The effect's own FOGFRAME block on WARP against the C++ mirror.
static void WarpPostBlock(const std::string& fx) {
    const size_t b = fx.find("// FOGFRAME-BEGIN"), e = fx.find("// FOGFRAME-END");
    if (b == std::string::npos || e == std::string::npos || e < b) { printf("FAIL: the FOGFRAME marks are not in the effect\n"); ++g_fail; return; }
    const size_t bodyAt = fx.find('\n', b) + 1;
    const std::string src = std::string("cbuffer C : register(b0) { float4 L[16]; float4 F[16]; };\n") + fx.substr(bodyAt, e - bodyAt) +
        "\nfloat4 PS(float4 p : SV_Position) : SV_Target { const int i = min((int)p.x, 15); return float4(FrameVoidFog(L[i].rgb, F[i].rgb, F[i].w), 1); }\n"
        "void VS(uint id : SV_VertexID, out float4 pos : SV_Position) { const float2 t = float2((id << 1) & 2, id & 2); pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); }\n";
    ComPtr<ID3DBlob> vsb = Build(src.c_str(), src.size(), "VS", "vs_5_0"), psb = Build(src.c_str(), src.size(), "PS", "ps_5_0");
    if (!vsb || !psb) return;
    Gpu g;
    if (!MakeGpu(g)) { printf("FAIL: no WARP device\n"); ++g_fail; return; }
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    g.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    g.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    struct { float l[16][4], f[16][4]; } cb{};
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 3; ++c) { cb.l[i][c] = Rnd(0, 5); cb.f[i][c] = Rnd(0, 0.5f); }
        cb.f[i][3] = Rnd(0, 1);
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof cb;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{&cb, 0, 0};
    ComPtr<ID3D11Buffer> buf;
    g.dev->CreateBuffer(&bd, &init, &buf);
    Tex rt = MakeTex(g, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 1, nullptr, 0, D3D11_BIND_RENDER_TARGET);
    D3D11_VIEWPORT vp{0, 0, 16, 1, 0, 1};
    g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g.ctx->VSSetShader(vs.Get(), nullptr, 0);
    g.ctx->PSSetShader(ps.Get(), nullptr, 0);
    g.ctx->PSSetConstantBuffers(0, 1, buf.GetAddressOf());
    g.ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtv = rt.rtv.Get();
    g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
    g.ctx->Draw(3, 0);
    const std::vector<float> out = ReadBack(g, rt.t.Get(), 16, 1);
    int bad = 0;
    for (int i = 0; i < 16; ++i) {
        const V3 want = FrameVoidFog({cb.l[i][0], cb.l[i][1], cb.l[i][2]}, {cb.f[i][0], cb.f[i][1], cb.f[i][2]}, cb.f[i][3]);
        if (!Near3({out[i * 4], out[i * 4 + 1], out[i * 4 + 2]}, want, 1e-5f)) ++bad;
    }
    Expect(bad == 0, "WARP: the effect's FrameVoidFog (the FOGFRAME block) as the C++ mirror");
}

static void Effect(const std::string& fx) {
    // where the frame left a pixel to the post pass: the effects' light over the fog of the far, and over the sky at full
    Expect(fx.find("if (FogInFrame) return FrameVoidFog(colour, fogColour, t);") != std::string::npos,
           "the post pass lays the effects' light over the fog where the frame left the pixel to it (no thin lines)");
    Expect(fx.find("colour * (seen * (1.0 - (FogInFrame ? 0.0 : tf)) * keep)") != std::string::npos,
           "over the sky the effects' light is not faded by the ground's fog while the frame draws the fog");
    Expect(fx.find("uniform bool FogInFrame") != std::string::npos, "the effect has FogInFrame");
    // the same pixels as the frame's: by its own depth (d), not the five's (dNear) - else the void's rim was neither's
    Expect(fx.find("if (FogInFrame && !ShowDistance && d > 1e-6) return colour;") != std::string::npos,
           "the post pass leaves a pixel with its own depth to the frame, the void and its rim it keeps");
}

static bool ReadFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

int main(int argc, char** argv) {
    const char* gamePath = nullptr;
    std::string fxPath = FOGFRAME_FX_PATH;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--game-shader")) gamePath = argv[++i];
        else if (!strcmp(argv[i], "--fx")) fxPath = argv[++i];
    }
    Match();
    std::vector<uint8_t> game;
    if (gamePath) {
        if (!ReadFile(gamePath, game)) { printf("FAIL: %s could not be read\n", gamePath); ++g_fail; }
        else {
            Expect(IsResolve(game.data(), game.size()), "the game's own ps_transparent_resolve is matched (its size and FNV-1a)");
            printf("the game's shader: %zu bytes, FNV-1a %016llX\n", game.size(), (unsigned long long)Fnv(game.data(), game.size()));
        }
    }
    CompositeCases();
    ColourCases();
    FogCases();
    PlankCases();
    ApartCases();
    FlameCases();
    FenceRowCases();
    HaloColumnsCases();
    WarpPixel(game);
    WarpCompute();
    WarpApart();
    {
        std::ifstream f(fxPath, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (!f) { printf("FAIL: %s could not be read\n", fxPath.c_str()); ++g_fail; }
        else { Effect(ss.str()); WarpPostBlock(ss.str()); }
    }
    if (g_fail) { printf("fog_in_frame_test: %d FAILED\n", g_fail); return 1; }
    printf("fog_in_frame_test: all passed%s\n", gamePath ? "" : " (without the game's own shader: --game-shader <DXBC> for that)");
    return 0;
}
