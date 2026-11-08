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
// flat normal, not reflective. The depth pre-pass is the game's, untouched (the colour pass still draws with its depth-equal test).
// Water, glass, hair (blended, other layouts), lava, the blended models (vertex colour) and detail normals keep the game's shaders.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace litflat {

enum Family { kNotOurs = 0, kModels, kTerrain };

// The models' pixel shader; LIGHT (float3) is defined when it is compiled: the light colour times the level
inline constexpr char kModelsShader[] = R"(
Texture2D Albedo : register(t10);
SamplerState Linear : register(s0);
cbuffer Frame : register(b10) { float4 F[90]; };
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
    const float3 a = Albedo.SampleBias(Linear, i.uv, F[13].y).rgb;
    o0 = float4(Shoulder(a * LIGHT) * F[0].x, 1.0);
    o1 = float4(0.5, 0.5, 1.0, 1.0);
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
    o0 = float4(Shoulder(c.rgb * LIGHT) * F[0].x, 1.0);
    o1 = float4(0.5, 0.5, 1.0, c.a);
}
)";

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
void Configure(bool on, float level, uint32_t rgb, bool area = false, float levelCaves = -1.0f);
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
