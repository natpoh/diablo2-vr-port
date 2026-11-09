// lit_flat.h - [render] bench_lit_flat: the game's lit colour pass drawn flat - each surface its own material colour times one light colour,
// evenly, everywhere: no sun, no shadows, no torches, no light loops, no reflections of the sky. A render bench key (vr/render_bench.h):
// visual only, live, VR first person (F4) only. Serg 2026-11-08: "мне нужно отключить все эффекты и осветить всё равномерно одним цветом" -
// with it the shadow maps and the local lights can be left out (bench_skip_shadows_*, bench_skip_point_lights) and the world still shows.
//
// What the game's lit shaders do first (read from its own shaders, D2R 3.3.x; docs/notes/render_bench.md):
//   - the models' family - ps_models_p (all 192 permutations), ps_foliage_p, ps_grunge_p, ps_alpha_threshold_p - one input layout
//     (SV_POSITION v0, POSITION v1, NORMAL v2, TANGENT v3, TEXCOORD0 v4, TEXCOORD1 v5, SV_IsFrontFace v6): the material's colour is t10
//     sampled at TEXCOORD0 with s0 and the frame's mip bias (b10[13].y);
//   - the terrain (ps_terrain: SV_POSITION, POSITION, POS_VS, POS_WS, NORMAL_VS, TANGENT_VS): its colour is the game's composite of the
//     ground's layers, t35, at the world position laid flat the game's way ((xz - y sqrt(1.5) - b10[3].zw) / 180) with s0;
//   - both end with the light they gathered times the frame's exposure (b10[0].x) into SV_Target0 (the HDR scene) and a packed normal /
//     material word into SV_Target1.
// Ours, in the game's own root signature and with the same input layout (the pipeline links exactly as the game's): the colour as the game
// samples it, times the light colour, times the exposure, into SV_Target0; SV_Target1 the target's own cleared value (0.5, 0.5, 1, 1) - a
// flat normal, not reflective. The depth pre-pass is the game's, untouched (the colour pass still draws with its depth-equal test). Each
// permutation's own cut of its transparent texels and the foliage's / grunge's blended output are copied (Variant, since 0.209.21).
// Water, glass, hair (blended, other layouts), lava, the blended models (vertex colour) and detail normals keep the game's shaders.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace litflat {

enum Family { kNotOurs = 0, kModels, kTerrain };

// How the game's own permutation of the models' family cuts its transparent texels and whether it is blended - our copy does the same
// (Serg 2026-11-09, 0.209.18 Potato: "вокруг прозрачных текстур синяя подкраска" - every bush and grass card a solid quad of the atlas's
// empty blue: ours had no cut and wrote alpha 1). Read from the game's shaders (D2R 3.3.x, all 209 permutations; lit_flat_test checks it):
//   - kVarAlbedoRef: `discard` where albedo.a - b4[0].x < 0 (ps_alpha_threshold_p 2, 3, 6, 7 and 48 of ps_models_p);
//   - kVarOpacityRef: where the opacity map t14.r - b4[0].y < 0 (48 of ps_models_p); kVarBothRef: both (48);
//   - kVarFoliage (ps_foliage_p 1, 3, 5, 7) and kVarGrunge (ps_grunge_p): BLENDED, the game's weighted transparency - a texel with alpha under
//     1/1023 cut, then alpha x b2[19].z (the object's fade), SV_Target0 = (colour x alpha, alpha) x the depth weight
//     max(10^6 / (10^5 - z^3), 0) of TEXCOORD1.z, SV_Target1 = (1, 1, 1, alpha); the grunge's alpha is albedo.a x b4[0].x.
// kVarUnknown: a cut we do not know (another game version) - that pipeline stays the game's.
enum Variant : int { kVarUnknown = -1, kVarPlain = 0, kVarAlbedoRef, kVarOpacityRef, kVarBothRef, kVarFoliage, kVarGrunge, kVariants };
inline bool Blended(int v) { return v == kVarFoliage || v == kVarGrunge; }
// The shader's macros for a variant: CLIP (bit 1 the albedo's cut against b4[0].x, bit 2 the opacity map's against b4[0].y), BLEND (1 the
// foliage's, 2 the grunge's)
inline const char* ClipMacro(int v) { return v == kVarAlbedoRef ? "1" : v == kVarOpacityRef ? "2" : v == kVarBothRef ? "3" : "0"; }
inline const char* BlendMacro(int v) { return v == kVarFoliage ? "1" : v == kVarGrunge ? "2" : "0"; }
inline const char* VariantName(int v) {
    static const char* const n[kVariants] = {"plain", "albedo cut", "opacity cut", "both cuts", "foliage (blended)", "grunge (blended)"};
    return v >= 0 && v < kVariants ? n[v] : "unknown";
}

// [render] potato_light_game (docs/notes/lighting_study.md, off by default): GAME_LIGHT 1 - the light is not one colour but the game's own
// two lights of the lit pass, without their shadows, torches, light loops or reflections, so the ground and the grass keep the colours the
// area's vis file gives them at this time of day:
//   - the ambient (b0, CB0[10], built each frame at 0xE12719): A[0].x the vis file's iblIntensity, A[0].z its diffuse scale (1.0), A[1..9]
//     the area's IBL probe's 9 spherical-harmonic colours (0xF69110), read at the WORLD normal - the game turns the view-space normal by
//     the camera's b3[9..11] first (ps_terrain / ps_models_p / ps_grass_lighting_p, all three alike);
//   - the lights (b1, CB1[10]): K[0].x HOW MANY (uint, up to 3 - the game's own branches: ieq 1, ieq 2, else a loop to the count), each
//     directional, light i at K[1 + 3i] its colour, K[2 + 3i] (diffuse contribution, specular, power, shadow flag), K[3 + 3i].xyz its
//     direction in view space; lit by N.L (the game's diffuse = albedo x colour x power x diffuse x N.L / pi). The world gets one (the vis
//     file's global_key_light: the sun or the moon); characters get the player's / monsters' three (key 30, fill 4, back - lightMask 68 / 74
//     / 10): until 0.209.22 ours lit only a count of 1, and the characters drew in the ambient alone, black at night (Serg on 0.209.21).
//   - the characters' own curve (ps_models_p / ps_alpha_threshold_p, all permutations, when the draw's b2[19].w is set): the lit colour (after
//     the exposure) to HSV, its value lifted - V = (b2[18] . (1, v, v^2, v^3) x b2[19].y + max) x (1 + b2[19].x), v = saturate(max) - and back
//     (the vis file's characterContrastPoints / characterBrightnessFactor): CharacterCurve, on our opaque models' copy with GAME_LIGHT.
// b0 / b1 / b3 are registers the game's own lit shaders declare (CB0[10], CB1[10], CB3[13]): the same root signature. Left out of the
// game's: the shadow (K[3].w / the sun map), the ambient's reach mask (A[0].y / t44 - with the torches out it would be black indoors), the
// specular, the (1 - F) of a dielectric (~4 % brighter) and the material's AO. LIGHT is then the level alone (the brightness sliders).
inline constexpr char kGameLight[] = R"(
#if GAME_LIGHT
cbuffer Ambient : register(b0) { float4 A[10]; };
cbuffer KeyLight : register(b1) { float4 K[10]; };
cbuffer Camera : register(b3) { float4 C[13]; };
float3 GameLight(float3 nvs)
{
    const float3 n = normalize(C[9].xyz * nvs.x + C[10].xyz * nvs.y + C[11].xyz * nvs.z);
    const float3 sh = A[1].xyz + A[2].xyz * n.y + A[3].xyz * n.z + A[4].xyz * n.x + A[5].xyz * (n.x * n.y) + A[6].xyz * (n.z * n.y) +
                      A[7].xyz * (3.0 * n.z * n.z - 1.0) + A[8].xyz * (n.x * n.z) + A[9].xyz * (n.x * n.x - n.y * n.y);
    const float3 ambient = max(sh, 0.0) * (A[0].z * A[0].x);
    const uint count = min(asuint(K[0].x), 3u);
    float3 direct = float3(0.0, 0.0, 0.0);
    [unroll] for (uint k = 0; k < 3; ++k)
        if (k < count) direct += K[1 + 3 * k].xyz * (K[2 + 3 * k].z * K[2 + 3 * k].x * saturate(dot(nvs, -K[3 + 3 * k].xyz)));
    return (ambient + direct) * 0.31830989;
}
#endif
)";

// The models' pixel shader; LIGHT (float3) is defined when it is compiled: the light colour times the level; GAME_LIGHT 0 / 1 (kGameLight,
// put in front of it); CLIP / BLEND the game's cut and blending (Variant, ClipMacro / BlendMacro)
inline constexpr char kModelsShader[] = R"(
#ifndef CLIP
#define CLIP 0
#endif
#ifndef BLEND
#define BLEND 0
#endif
Texture2D Albedo : register(t10);
#if CLIP >= 2
Texture2D Opacity : register(t14);
#endif
SamplerState Linear : register(s0);
cbuffer Frame : register(b10) { float4 F[90]; };
#if CLIP || BLEND == 2
cbuffer Material : register(b4) { float4 M[1]; };
#endif
#if BLEND || GAME_LIGHT
cbuffer Draw : register(b2) { float4 D[29]; };
#endif
#if GAME_LIGHT && !BLEND
// the game's characters' curve (ps_models_p's tail, when b2[19].w is set): c after the exposure, its HSV value lifted
float3 CharacterCurve(float3 c)
{
    if (asuint(D[19].w) == 0u) return c;
    const float mx = max(c.r, max(c.g, c.b)), delta = mx - min(c.r, min(c.g, c.b));
    float h = 0.0, s = 0.0;
    if (delta != 0.0) {
        const float3 d = ((mx - c.gbr) / 6.0 + delta * 0.5) / delta;   // (G, B, R)
        h = mx == c.r ? d.y - d.x : mx == c.g ? d.z + 1.0 / 3.0 - d.y : mx == c.b ? d.x + 2.0 / 3.0 - d.z : 0.0;
        h = h < 0.0 ? h + 1.0 : h;
        h = h > 1.0 ? h - 1.0 : h;
        s = delta / mx;
    }
    const float v = saturate(mx);
    const float lifted = dot(D[18], float4(1.0, v, v * v, v * v * v)) * D[19].y + mx;
    const float V = lifted * D[19].x + lifted;
    if (s == 0.0) return float3(V, V, V);
    const float sector = floor(h * 6.0), f = h * 6.0 - sector;
    const float p = (1.0 - s) * V, q = (1.0 - s * f) * V, t = (1.0 - s * (1.0 - f)) * V;
    return sector == 0.0 ? float3(V, t, p) : sector == 1.0 ? float3(q, V, p) : sector == 2.0 ? float3(p, V, t) :
           sector == 3.0 ? float3(p, q, V) : sector == 4.0 ? float3(t, p, V) : float3(V, p, q);
}
#endif
struct In {
    float4 pos : SV_POSITION;
    float4 p : POSITION;
    float3 n : NORMAL;
    float4 t : TANGENT;
    float2 uv : TEXCOORD0;
    float4 w : TEXCOORD1;
    bool face : SV_IsFrontFace;
};
// (the light over 1 - [render] potato_light_brightness up to 5 - eased: up to 1 as it is, past it toward twice the colour and never beyond, so a
// bright setting brightens the dark surfaces more than it burns the light ones out in the game's tone map)
float3 Shoulder(float3 c) { const float3 x = max(c - 1.0, 0.0); return min(c, 1.0) + x / (1.0 + x); }
void main(In i, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1)
{
    const float4 a4 = Albedo.SampleBias(Linear, i.uv, F[13].y);
    const float3 a = a4.rgb;
    // the game's cuts, as its permutation makes them (discard where the difference is below 0)
#if CLIP == 1 || CLIP == 3
    if (a4.a - M[0].x < 0.0) discard;
#endif
#if CLIP >= 2
    if (Opacity.SampleBias(Linear, i.uv, F[13].y).x - M[0].y < 0.0) discard;
#endif
#if BLEND
#if BLEND == 2
    float alpha = a4.a * M[0].x;
#else
    float alpha = a4.a;
#endif
    if (alpha < 0.00097752) discard;   // (1 / 1023, the game's l(0.000978))
#endif
#if GAME_LIGHT
    // the game's normal: flipped on a back face by TEXCOORD1.w (ps_models_p: movc r2.w, v6.x, 1, -v5.w), view space
    const float3 nvs = normalize((i.face ? 1.0 : -i.w.w) * i.n);
#if BLEND
    const float3 lit = a * GameLight(nvs) * LIGHT * F[0].x;
#else
    const float3 lit = CharacterCurve(a * GameLight(nvs) * F[0].x) * LIGHT;   // (the game's curve on the game's value; the level after it)
#endif
#else
    const float3 lit = Shoulder(a * LIGHT) * F[0].x;
#endif
#if BLEND
    // the game's weighted transparency: premultiplied by the alpha times the object's fade, weighted by depth; the second target the alpha
    alpha *= D[19].z;
    const float w = max(1000000.0 / (100000.0 - i.w.z * i.w.z * i.w.z), 0.0);
    o0 = float4(lit * alpha, alpha) * w;
    o1 = float4(1.0, 1.0, 1.0, alpha);
#else
    o0 = float4(lit, 1.0);
    o1 = float4(0.5, 0.5, 1.0, 1.0);
#endif
}
)";

// The terrain's
inline constexpr char kTerrainShader[] = R"(
Texture2D Ground : register(t35);
SamplerState Linear : register(s0);
cbuffer Frame : register(b10) { float4 F[90]; };
struct In {
    float4 pos : SV_POSITION;
    float4 p : POSITION;
    float3 pvs : POS_VS;
    float3 pws : POS_WS;
    float3 nvs : NORMAL_VS;
    float4 tvs : TANGENT_VS;
};
// (the light over 1 - [render] potato_light_brightness up to 5 - eased: up to 1 as it is, past it toward twice the colour and never beyond, so a
// bright setting brightens the dark surfaces more than it burns the light ones out in the game's tone map)
float3 Shoulder(float3 c) { const float3 x = max(c - 1.0, 0.0); return min(c, 1.0) + x / (1.0 + x); }
void main(In i, out float4 o0 : SV_Target0, out float4 o1 : SV_Target1)
{
    const float2 uv = (i.pws.xz - i.pws.y * 1.2247449 - F[3].zw) * (1.0 / 180.0);
    const float4 c = Ground.Sample(Linear, uv);
#if GAME_LIGHT
    o0 = float4(c.rgb * GameLight(normalize(i.nvs)) * LIGHT * F[0].x, 1.0);   // (ps_terrain's base normal: NORMAL_VS, before its normal map)
#else
    o0 = float4(Shoulder(c.rgb * LIGHT) * F[0].x, 1.0);
#endif
    o1 = float4(0.5, 0.5, 1.0, c.a);
}
)";

// A family's whole source: the game's-light part (compiled out with GAME_LIGHT 0) in front of the shader
inline std::string Source(Family f) {
    return f == kModels ? std::string(kGameLight) + kModelsShader : f == kTerrain ? std::string(kGameLight) + kTerrainShader : std::string();
}

// A shader's input signature (its ISGN chunk): semantic, index, register, component mask
struct Input { std::string name; uint32_t index = 0, reg = 0; uint8_t mask = 0; };
inline bool InputsOf(const uint8_t* dxbc, size_t size, std::vector<Input>* out) {
    out->clear();
    if (!dxbc || size < 32 || std::memcmp(dxbc, "DXBC", 4) != 0) return false;
    uint32_t n = 0;
    std::memcpy(&n, dxbc + 28, 4);
    if (n > 64 || 32 + 4ull * n > size) return false;
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t off = 0;
        std::memcpy(&off, dxbc + 32 + 4 * k, 4);
        if (off + 8ull > size) return false;
        if (std::memcmp(dxbc + off, "ISGN", 4) != 0) continue;
        uint32_t len = 0;
        std::memcpy(&len, dxbc + off + 4, 4);
        const uint8_t* c = dxbc + off + 8;
        if (off + 8ull + len > size || len < 8) return false;
        uint32_t count = 0;
        std::memcpy(&count, c, 4);
        if (count > 32 || 8ull + 24ull * count > len) return false;
        for (uint32_t e = 0; e < count; ++e) {
            const uint8_t* el = c + 8 + 24 * e;
            uint32_t nameAt = 0;
            std::memcpy(&nameAt, el, 4);
            if (nameAt >= len) return false;
            Input in;
            const char* nm = (const char*)c + nameAt;
            in.name.assign(nm, strnlen(nm, len - nameAt));
            std::memcpy(&in.index, el + 4, 4);
            std::memcpy(&in.reg, el + 16, 4);
            in.mask = el[20];
            out->push_back(in);
        }
        return true;
    }
    return false;
}
inline const std::vector<Input>& Expected(Family f) {
    static const std::vector<Input> models = {{"SV_POSITION", 0, 0, 0xF}, {"POSITION", 0, 1, 0xF}, {"NORMAL", 0, 2, 0x7}, {"TANGENT", 0, 3, 0xF},
                                              {"TEXCOORD", 0, 4, 0x3},    {"TEXCOORD", 1, 5, 0xF}, {"SV_IsFrontFace", 0, 6, 0x1}};
    static const std::vector<Input> terrain = {{"SV_POSITION", 0, 0, 0xF}, {"POSITION", 0, 1, 0xF}, {"POS_VS", 0, 2, 0x7},
                                               {"POS_WS", 0, 3, 0x7},      {"NORMAL_VS", 0, 4, 0x7}, {"TANGENT_VS", 0, 5, 0xF}};
    static const std::vector<Input> none;
    return f == kModels ? models : f == kTerrain ? terrain : none;
}
inline bool Same(const std::vector<Input>& a, const std::vector<Input>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (_stricmp(a[i].name.c_str(), b[i].name.c_str()) != 0 || a[i].index != b[i].index || a[i].reg != b[i].reg || a[i].mask != b[i].mask)
            return false;
    return true;
}
// Which of ours a lit pixel shader takes (by its input layout: the exact one ours declares), kNotOurs = left the game's
inline Family FamilyOf(const uint8_t* dxbc, size_t size) {
    std::vector<Input> in;
    if (!InputsOf(dxbc, size, &in)) return kNotOurs;
    if (Same(in, Expected(kModels))) return kModels;
    if (Same(in, Expected(kTerrain))) return kTerrain;
    return kNotOurs;
}
// A pixel shader's cut, from its tokens (SHEX / SHDR): the instructions from the first after the declarations up to its last `discard`, hashed
// (FNV-1a 64 of their tokens). The game's compiler puts every cut first (the albedo or opacity sample, the compare, the discard); a shader with
// no discard is plain, one with a discard past its 12th instruction is not ours to copy. false: not readable.
struct Cut { bool any = false; bool late = false; uint64_t hash = 0; };
inline bool CutOf(const uint8_t* dxbc, size_t size, Cut* out) {
    *out = Cut{};
    if (!dxbc || size < 32 || std::memcmp(dxbc, "DXBC", 4) != 0) return false;
    uint32_t n = 0;
    std::memcpy(&n, dxbc + 28, 4);
    if (n > 64 || 32 + 4ull * n > size) return false;
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t off = 0, len = 0;
        std::memcpy(&off, dxbc + 32 + 4 * k, 4);
        if (off + 8ull > size) return false;
        if (std::memcmp(dxbc + off, "SHEX", 4) != 0 && std::memcmp(dxbc + off, "SHDR", 4) != 0) continue;
        std::memcpy(&len, dxbc + off + 4, 4);
        if (off + 8ull + len > size || len < 8) return false;
        const uint8_t* t = dxbc + off + 8;
        const size_t words = len / 4;
        auto word = [&](size_t i) { uint32_t w = 0; std::memcpy(&w, t + 4 * i, 4); return w; };
        auto decl = [](uint32_t op) { return op == 53 || (op >= 88 && op <= 106) || (op >= 143 && op <= 162) || op == 206; };
        size_t first = 0, cutEnd = 0;
        int ordinal = 0;
        for (size_t i = 2; i < words;) {
            const uint32_t w0 = word(i), op = w0 & 0x7FF;
            const size_t l = op == 53 ? (i + 1 < words ? word(i + 1) : 0) : (w0 >> 24) & 0x7F;
            if (!l || i + l > words) return false;
            if (!decl(op)) {
                if (!first) first = i;
                if (op == 13) {   // discard
                    out->any = true;
                    if (ordinal >= 12) out->late = true;
                    else cutEnd = i + l;
                }
                ++ordinal;
            }
            i += l;
        }
        if (out->any && !out->late && first) {
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 4 * first; i < 4 * cutEnd; ++i) { h ^= t[i]; h *= 1099511628211ull; }
            out->hash = h;
        }
        return true;
    }
    return false;
}
// The game's cuts as hashed by CutOf (D2R 3.3.x; lit_flat_test prints them from the game's shaders and checks each against its disassembly).
// Two register allocations of the same cut hash apart: both listed.
struct KnownCut { uint64_t hash; int variant; };
inline constexpr KnownCut kKnownCuts[] = {
    {0x187D7DC93DCB9E80ull, kVarAlbedoRef},    // ps_alpha_threshold_p 2, 3, 6, 7; ps_models_p (48)
    {0x97A150654302A073ull, kVarOpacityRef},   // ps_models_p: t14 into r0.w
    {0xA61CD46801B019CCull, kVarOpacityRef},   // ps_models_p: t14 into r1.xy
    {0xA6D140A8C69B1778ull, kVarBothRef},      // ps_models_p: t14 into r0.w
    {0xC995CC40EAC61627ull, kVarBothRef},      // ps_models_p: t14 into r1.xy
    {0x69506FB4FD2FEBEAull, kVarFoliage},      // ps_foliage_p 1, 3, 5, 7
    {0x3285417ED56B7A7Cull, kVarGrunge},       // ps_grunge_p
};
// A lit pipeline's blending as it bears on our copy: rt0 = its first target blends, other = any other target blends (with independent blending)
// or a logic op is on. Ours goes in only where it gives back what the game's shader gives the blend: an opaque copy (plain, cut) only in a
// pipeline that blends nothing, the foliage's / grunge's weighted transparency only where the first target blends (its second, the
// revealage, may blend too). Every other blended lit pipeline - a decal laid over the ground by its alpha (Serg 0.209.20, the road before
// the camp's bridge: lilac blocks) - stays the game's.
inline bool Accept(Family f, int variant, bool rt0, bool other) {
    if (variant == kVarUnknown) return false;
    if (f == kTerrain) return variant == kVarPlain && !rt0 && !other;
    if (Blended(variant)) return rt0;
    return !rt0 && !other;
}
// Our variant for a models' family shader (kVarUnknown: leave the pipeline the game's)
inline int VariantOf(const uint8_t* dxbc, size_t size) {
    Cut c;
    if (!CutOf(dxbc, size, &c) || c.late) return kVarUnknown;
    if (!c.any) return kVarPlain;
    for (const KnownCut& k : kKnownCuts)
        if (k.hash == c.hash && k.hash) return k.variant;
    return kVarUnknown;
}

// The Potato light's colours ([render] potato_light_colour 0 - 2; 3 = the area's own, AreaTint)
inline uint32_t PresetColour(int choice) { return choice == 1 ? 0xFFE0B5u : choice == 2 ? 0xD2E1FFu : 0xFFFFFFu; }
// The area's colour as the light's: its hue at full strength, half-way to white (a tint, never a strong colour; the brightness is the slider's),
// in 1/32 steps so a slow drift does not make the copies again every few seconds. 0xFFFFFF for a colour with no light in it.
inline uint32_t AreaTint(const float c[3]) {
    const float m = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
    if (!(m > 1e-4f)) return 0xFFFFFFu;
    uint32_t out = 0;
    for (int i = 0; i < 3; ++i) {
        float t = 0.5f + 0.5f * (c[i] > 0.0f ? c[i] / m : 0.0f);
        int q = (int)(t * 32.0f + 0.5f);
        q = q > 32 ? 32 : q;
        int v = q * 255 / 32;
        out = out << 8 | (uint32_t)v;
    }
    return out;
}
// "float3(1.0000, 0.9000, 0.8000)" - the light colour (0..1 each, from #RRGGBB) times the level
inline std::string LightDefine(uint32_t rgb, float level) {
    char b[96];
    snprintf(b, sizeof b, "float3(%.5f, %.5f, %.5f)", ((rgb >> 16) & 255) / 255.0 * level, ((rgb >> 8) & 255) / 255.0 * level, (rgb & 255) / 255.0 * level);
    return b;
}

}  // namespace litflat

#ifdef _WIN32
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
struct ID3D12Device;
struct ID3D12PipelineState;
namespace litflat {
void SetLog(void (*log)(const char*));
// The game's graphics pipelines as made: a lit one of a known layout is kept (its description, shaders and layout copied) - nothing made yet
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso);
// [render] potato_light (bench_lit_flat), live: a new light makes the copies again (a few a frame). `area`: the light's colour follows the
// area's own (SetAreaColour's), `rgb` then unused. `levelCaves`: the level underground ([render] potato_light_brightness_caves; < 0: `level`) -
// each lit pipeline keeps a copy for each light, so an area change only picks the other one
// `gameLight`: [render] potato_light_game - the game's own ambient and key light without shadows (kGameLight); `rgb` / `area` then unused
void Configure(bool on, float level, uint32_t rgb, bool area = false, float levelCaves = -1.0f, bool gameLight = false);
// Underground now (the fog's split: caves and dungeons - g_underground), for which light is in force
void SetUnderground(bool (*fn)());
// The area's colour now (the sky's horizon / the act's fog colour, as our fog takes it: fx::FogColourNow), for the Potato light's "the area's own"
void SetAreaColour(bool (*fn)(float rgb[3]));
// Each present (the present's thread): our shaders compiled for the light, copies made (up to a few a frame), old ones let go later
void Frame();
// A pipeline the game binds: ours in its place while on, in F4, once made for the light now
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);
std::string Line();   // the 10 s line (empty while off and already told so)
}  // namespace litflat
#endif
