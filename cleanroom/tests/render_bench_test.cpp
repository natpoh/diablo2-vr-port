// render_bench_test - [render] bench_skip_* (vr/render_bench.h): the game's shaders sorted into its render stages, and what each key leaves out.
// The hashes checked are the ones the 2026-10-13 frame trace showed the game binding (docs/notes/frame_cost_study.md section 2).
#include <cstdio>
#include <cstdint>
#include <iterator>

#include "render_bench.h"

using namespace renderbench;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    // the table: sorted, no hash twice, every stage a real one
    bool sorted = true, stagesOk = true;
    for (size_t i = 0; i < std::size(kStageHashes); ++i) {
        if (i && !(kStageHashes[i - 1].hash < kStageHashes[i].hash)) sorted = false;
        if (kStageHashes[i].stage == kNone || kStageHashes[i].stage >= kStages) stagesOk = false;
    }
    CHECK(sorted);
    CHECK(stagesOk);
    CHECK(std::size(kStageHashes) > 300);

    // the frame trace's shaders, each in its stage
    CHECK(StageOf(0xa11d8d1c6efd3f33ull) == kLit);              // ps_terrain
    CHECK(StageOf(0xae12b1e437155234ull) == kLit);              // ps_models_p
    CHECK(StageOf(0x33e9ff96047fa42eull) == kShadows);          // ps_models_shadows_p
    CHECK(StageOf(0x9f5a9f87e34c6a5full) == kShadows);          // ps_hair_shadows_dither_p
    CHECK(StageOf(0xa4e0fde465cf1cf0ull) == kGrassForce);       // ps_models_grass_force_p
    CHECK(StageOf(0x576532f6ec6521b7ull) == kGrassForce);       // ps_grass_force_update_p
    CHECK(StageOf(0x5fafbd06ef7f97d7ull) == kGrass);            // ps_grass_lighting_p
    CHECK(StageOf(0xf666e268a199a2deull) == kWaterGlassHair);   // ps_water_p
    CHECK(StageOf(0xc9d98c2e990c7ba1ull) == kWaterGlassHair);   // ps_glass_p
    CHECK(StageOf(0xace4faac06289d00ull) == kWaterGlassHair);   // ps_hair_p
    CHECK(StageOf(0x8b57d95606d6d7b2ull) == kAssao);            // ps_assao_generate_p
    CHECK(StageOf(0xa914164017855558ull) == kAssao);            // ps_assao_apply_p
    CHECK(StageOf(0xffda83db6037f452ull) == kBloom);            // ps_bloom_apply_p
    CHECK(StageOf(0x0f833afce5ec6098ull) == kBloom);            // ps_bloom_cutoff_p
    CHECK(StageOf(0x908caf83443c3d6cull) == kPyramid);          // ps_color_downsample_p
    CHECK(StageOf(0xb0eb2e8c9fe3aa57ull) == kPyramid);          // ps_color_copy_p
    CHECK(StageOf(0x1a8677d1fd43cc99ull) == kPpr);              // cs_ppr_projection_c: the reflections' search
    CHECK(StageOf(0x403f6d66029d1d1full) == kNone);             // ps_ppr_reflection_p: writes the whole lit scene back - never skipped
    CHECK(StageOf(0x6211714a46043944ull) == kNone);             // cs_ppr_init_buffer_c: "no hit" for the resolve - kept
    CHECK(StageOf(0xf56bdb8a11695740ull) == kAtmosphere);       // ps_atmosphere_apply #5
    CHECK(StageOf(0xf0102f21a9639cc9ull) == kTonemap);          // ps_tonemap_p
    CHECK(StageOf(0x35348fafecb43ae3ull) == kTonemap);          // ps_fxaa_p
    CHECK(StageOf(0xeb057a4a82fe2a57ull) == kOutline);          // ps_outline_blur_p
    // ... and what the bench must never touch: the depth pre-pass, the transparents' resolve, the interface, the linear depth
    CHECK(StageOf(0xdba7aacc1295b83bull) == kNone);             // ps_models_velocity_p (the pre-pass)
    CHECK(StageOf(0x53183967e9589edbull) == kNone);             // ps_transparent_resolve
    CHECK(StageOf(0xeafadee47877eb58ull) == kNone);             // ps_ui_p
    CHECK(StageOf(0x4c551117939402eaull) == kNone);             // ps_linearize_depth
    CHECK(StageOf(0x29fb91e2dbea7a87ull) == kNone);             // vs_fullscreen_v
    CHECK(StageOf(0) == kNone);

    // a pipeline by its shaders: the vertex shader decides only without a pixel shader, and only for the shadows
    const uint64_t vsShadows = 0x0ff5f144953c838cull;   // vs_models_shadows_v
    CHECK(StageOf(vsShadows) == kShadows);
    CHECK(Classify(0, vsShadows, false) == kShadows);                       // a depth-only shadow pipeline
    CHECK(Classify(0xdba7aacc1295b83bull, vsShadows, false) == kNone);      // the velocity quad drawn with the shadows' vertex shader
    CHECK(Classify(0, vsShadows, true) == kNone);                           // (a compute pipeline has no vertex shader)
    CHECK(Classify(0, 0xc6d83ee9f62612d2ull, false) == kNone);              // vs_models_v, no pixel shader: the pre-pass, kept
    CHECK(Classify(0x5fafbd06ef7f97d7ull, 0x29fb91e2dbea7a87ull, false) == kGrass);

    // the keys
    CHECK(!Skips(0, kLit));
    CHECK(Skips(1u << kKeyLit, kLit));
    CHECK(!Skips(1u << kKeyLit, kTonemap));
    CHECK(!Skips(kAll, kNone));
    // the shadows split by their shaders: the sun's and spot lights' passes / the point lights' own
    CHECK(Skips(1u << kKeyShadowsSun, kShadows));
    CHECK(!Skips(1u << kKeyShadowsSun, kShadowsPoint));
    CHECK(Skips(1u << kKeyShadowsOther, kShadowsPoint));
    CHECK(!Skips(1u << kKeyShadowsOther, kShadows));
    CHECK(StageOf(0xbeb81c74b9ea66f4ull) == kShadowsPoint);   // ps_models_shadows_point_p
    for (int s = 1; s < kStages; ++s) CHECK(Skips(kAll, (uint8_t)s));
    for (int s = 1; s < kStages; ++s) CHECK(KeyOfStage(s) >= 0 && KeyOfStage(s) < kKeys);
    // the players' game_stages (the Low / Potato presets) and game_outline
    CHECK(StagesMask(0, true) == 0u);
    CHECK(StagesMask(0, false) == (1u << kKeyOutline));
    CHECK(!Skips(StagesMask(1, true), kLit) && !Skips(StagesMask(1, true), kShadows) && !Skips(StagesMask(1, true), kTonemap));   // Low keeps these
    CHECK(Skips(StagesMask(1, true), kShadowsPoint) && Skips(StagesMask(1, true), kGrass) && Skips(StagesMask(1, true), kGrassForce) &&
          Skips(StagesMask(1, true), kWaterGlassHair) && Skips(StagesMask(1, true), kAssao) && Skips(StagesMask(1, true), kBloom) &&
          Skips(StagesMask(1, true), kPyramid) && Skips(StagesMask(1, true), kPpr) && Skips(StagesMask(1, true), kAtmosphere));
    CHECK(!Skips(StagesMask(1, true), kOutline) && Skips(StagesMask(1, false), kOutline));   // the outline: its own key
    CHECK(Skips(StagesMask(2, true), kShadows) && !Skips(StagesMask(2, true), kLit) && !Skips(StagesMask(2, true), kTonemap));   // Potato: the sun's maps too
    CHECK(StagesMask(5, true) == StagesMask(2, true));
    // the game's fog wanted ([fog] game_fog=1): its pass kept on Low and Potato, the rest as before; nothing added where nothing was left out
    for (int st = 1; st <= 2; ++st) {
        CHECK(!Skips(StagesMask(st, true, true), kAtmosphere));
        CHECK(StagesMask(st, true, true) == (StagesMask(st, true) & ~(1u << kKeyAtmosphere)));
        CHECK(Skips(StagesMask(st, true, true), kAssao) && Skips(StagesMask(st, false, true), kOutline));
    }
    CHECK(StagesMask(0, true, true) == 0u);
    CHECK(StagesMask(2, true, false) == StagesMask(2, true));
    CHECK(MaskText(0) == "none");
    CHECK(MaskText(kAll) == "ALL");
    CHECK(MaskText((1u << kKeyBloom) | (1u << kKeyPpr)) == "bloom, ppr");

    // the lit calls by kind and the depth pre-pass (view only; Serg's pipe trace of 2026-11-09 16:51 - each heavy pipeline's hashes)
    CHECK(LitKindOf(0xae12b1e437155234ull) == kSubModels);         // ps_models_p (944 draws in that frame)
    CHECK(LitKindOf(0xdf7d377bda243447ull) == kSubFoliage);        // ps_foliage_p into the scene's colour (731)
    CHECK(LitKindOf(0xd606a4f97d05aa30ull) == kSubFoliage);        // ps_foliage_p into the two RGBA16F targets, blended (731)
    CHECK(LitKindOf(0xa11d8d1c6efd3f33ull) == kSubTerrain);        // ps_terrain
    CHECK(LitKindOf(0x390f43817f73b104ull) == kSubAlphaTested);    // ps_alpha_threshold_p
    CHECK(LitKindOf(0x170ed0704b9e5650ull) == kSubGrunge);         // ps_grunge_p
    CHECK(LitKindOf(0x33e9ff96047fa42eull) == kSubNone);           // a shadow shader is no lit kind
    CHECK(LitKindOf(0) == kSubNone);
    CHECK(SubOf(kLit, 0xae12b1e437155234ull, 0xc6d83ee9f62612d2ull, false, false) == kSubModels);
    // the pre-pass: ps_models_velocity_p (in no bench container) with vs_models_shadows_v / vs_clutter_shadows_v, the depth written
    CHECK(SubOf(Classify(0xdba7aacc1295b83bull, 0x0ff5f144953c838cull, false), 0xdba7aacc1295b83bull, 0x0ff5f144953c838cull, false, true) == kSubPrepass);
    CHECK(SubOf(Classify(0x69bc30aa76416354ull, 0x240c2ea9fec85a71ull, false), 0x69bc30aa76416354ull, 0x240c2ea9fec85a71ull, false, true) == kSubPrepass);
    CHECK(SubOf(kNone, 0x69bc30aa76416354ull, 0x240c2ea9fec85a71ull, false, false) == kSubNone);   // no depth written: not the pre-pass
    CHECK(SubOf(kShadows, 0, 0x0ff5f144953c838cull, false, true) == kSubNone);                     // the shadow map's own depth-only draws
    CHECK(SubOf(kNone, 0xeafadee47877eb58ull, 0xba29203c61667ee4ull, false, true) == kSubNone);   // ps_ui_p: not a shadow container's vs
    CHECK(SubOf(kNone, 0x45573483ffc471f0ull, 0, true, false) == kSubNone);                        // a compute pipeline
    for (const LitKindHash& h : kLitKindHashes) CHECK(StageOf(h.hash) == kLit && h.kind >= kSubModels && h.kind < kSubPrepass);
    CHECK(std::is_sorted(std::begin(kLitKindHashes), std::end(kLitKindHashes), [](const LitKindHash& a, const LitKindHash& b) { return a.hash < b.hash; }));
    std::string names;
    for (int s = 0; s < kSubs; ++s) names += SubName(s);
    CHECK(names.find('?') == std::string::npos);

    std::printf("render_bench_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
