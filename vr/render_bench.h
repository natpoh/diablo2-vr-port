// render_bench.h - [render] bench_skip_*: the game's own render stages left out one by one, live, to measure what each costs in the headset
// (docs/notes/frame_cost_study.md section 2 is the map; Serg 2026-11-08: "сперва попробуем отключить все тяжёлые проходы и посмотреть
// производительность"). A diagnostic bench: visual only, never the game's state or mechanics.
//
// How: every pipeline the game makes is hashed as it is made (vr/pipe_hunt.cpp: FNV-1a 64 of its pixel shader, of its compute shader, and of
// its vertex shader); the hash is looked up in vr/render_bench_hashes.h (generated from the game's own shader containers by
// tools/gen_render_bench_hashes.py) for its stage. While a stage's key is on, the game's draws, dispatches and indirect calls with that
// stage's pipeline bound are not made - nothing else: its clears, barriers, copies, target binds and the lists themselves go on as the game
// recorded them, so every resource stays in the state the game expects and the GPU has nothing to wait for that never comes. A skipped
// stage's targets keep what they were cleared to (or last frame's): the picture is wrong in that stage's way, the frame rate is the point.
// The game records each list once in the one-pass replay (the right eye resubmits the same lists) and twice in two passes: a skip is in the
// recording, so it holds for both eyes either way. Only in VR first person (F4); everywhere else nothing is skipped.
//
// Pure (no D3D12, no Windows): tested by cleanroom/tests/render_bench_test.cpp.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "render_bench_hashes.h"

namespace renderbench {

// The game's stages (the generator's order; vr/render_bench_hashes.h stores these numbers).
enum Stage : uint8_t {
    kNone = 0,
    kShadows,          // the sun's and spot lights' shadow maps (ps_*_shadows*, ps_*_shadows_dither*; depth-only pipelines by their vs_*_shadows_v)
    kGrassForce,       // the 512 x 512 map of where models press the grass down
    kLit,              // the lit colour pass, forward: ps_models_p, ps_terrain, foliage, detail normals, blends, alpha-tested, lava
    kGrass,            // the grass: its placement (compute), visibility and lighting (the full-screen 2,000-instruction pass)
    kWaterGlassHair,   // ps_water_p, ps_glass_p, ps_hair_p
    kAssao,            // the ambient occlusion: depth preparation, 4 generate + 4 blur passes, apply
    kBloom,            // cutoff, 6 downsamples, 6 applies
    kPyramid,          // the colour copy and its 7 downsamples (what glass, water and reflections read)
    kPpr,              // the reflections' search only (cs_ppr_projection_c): with it out the resolve finds no hit (-1, the init's) and writes the
                       // scene back without reflections. NOT the resolve ps_ppr_reflection_p - it returns the whole lit scene (the game copies it
                       // out and clears it first): skipping it took the sunlight away (2026-11-08)
    kAtmosphere,       // the game's fog: the volume (compute), sample / blur / reduce, the full-screen apply
    kTonemap,          // the tone map with its look-up tables, FXAA, SMAA
    kOutline,          // the outline round the hovered monster or item: its mask and two full-screen blurs
    kShadowsPoint,     // the point lights' shadow maps (torches, the hero's light): their own ps_*_shadows_point_p
    kStages
};

// The keys, one bit each ([render] bench_skip_<name>). The shadow maps are split by their shaders: shadows_sun = the directional and spot
// shadow passes' (the sun's maps, all of them), shadows_other = the point lights' (their own _point shaders). Until 2026-11-08 evening the
// split went by the target (the one with the most shadow draws last frame was "the sun's"): a second, small map of the sun's shaders fell
// into "other", and skipping "other" took the sunlight away (Serg's bench run 3). bench_skip_all = every bit.
enum Key : int {
    kKeyShadowsSun = 0, kKeyShadowsOther, kKeyGrassForce, kKeyLit, kKeyGrass, kKeyWaterGlassHair, kKeyAssao, kKeyBloom, kKeyPyramid, kKeyPpr,
    kKeyAtmosphere, kKeyTonemap, kKeyOutline, kKeys
};
inline constexpr uint32_t kAll = (1u << kKeys) - 1u;
// The players' [render] game_stages (D2R VR Settings > Performance > Render cost, the Low and Potato presets; Serg's bench runs 2026-11-08):
// 1 Low = the lit world, the sun's shadow map, the scene's copy-back and the tone map kept; everything else out. 2 Potato = Low without the
// sun's shadow maps (with the Potato light and the torch lights out: vr/config/load_settings.cpp). The outline has its own key
// ([render] game_outline) and is in neither. The atmosphere stage is the game's fog pass (ps_atmosphere_apply and its helpers): kept
// whenever the game's fog is wanted ([fog] game_fog=1, "Fog drawn by: the game's" - Serg 2026-11-08 on 0.207.9 Potato: game_fog=1 and no
// fog at all; the game's own fog is the fog, one cheap full-screen pass), left out only when the game does not draw the fog.
inline constexpr uint32_t kLowStages = (1u << kKeyShadowsOther) | (1u << kKeyGrassForce) | (1u << kKeyGrass) | (1u << kKeyWaterGlassHair) |
                                       (1u << kKeyAssao) | (1u << kKeyBloom) | (1u << kKeyPyramid) | (1u << kKeyPpr) | (1u << kKeyAtmosphere);
inline constexpr uint32_t kPotatoStages = kLowStages | (1u << kKeyShadowsSun);
inline uint32_t StagesMask(int gameStages, bool gameOutline, bool gameFog = false) {
    uint32_t m = gameStages >= 2 ? kPotatoStages : gameStages == 1 ? kLowStages : 0u;
    if (gameFog) m &= ~(1u << kKeyAtmosphere);
    return gameOutline ? m : m | (1u << kKeyOutline);
}
inline const char* KeyName(int k) {
    static const char* const n[kKeys] = {"shadows_sun", "shadows_other", "grass_force", "lit", "grass", "water_glass_hair", "assao", "bloom",
                                         "pyramid", "ppr", "atmosphere", "tonemap", "outline"};
    return k >= 0 && k < kKeys ? n[k] : "?";
}
inline const char* StageName(int s) {
    static const char* const n[kStages] = {"-", "shadows", "grass_force", "lit", "grass", "water_glass_hair", "assao", "bloom", "pyramid", "ppr",
                                           "atmosphere", "tonemap", "outline", "shadows_point"};
    return s >= 0 && s < kStages ? n[s] : "?";
}
// The key of a stage
inline int KeyOfStage(int s) {
    switch (s) {
    case kShadows: return kKeyShadowsSun;
    case kShadowsPoint: return kKeyShadowsOther;
    case kGrassForce: return kKeyGrassForce;
    case kLit: return kKeyLit;
    case kGrass: return kKeyGrass;
    case kWaterGlassHair: return kKeyWaterGlassHair;
    case kAssao: return kKeyAssao;
    case kBloom: return kKeyBloom;
    case kPyramid: return kKeyPyramid;
    case kPpr: return kKeyPpr;
    case kAtmosphere: return kKeyAtmosphere;
    case kTonemap: return kKeyTonemap;
    case kOutline: return kKeyOutline;
    default: return -1;
    }
}

// A shader hash's stage (kNone: not one of the bench's)
inline uint8_t StageOf(uint64_t hash) {
    if (!hash) return kNone;
    const StageHash* b = std::begin(kStageHashes);
    const StageHash* e = std::end(kStageHashes);
    const StageHash* it = std::lower_bound(b, e, hash, [](const StageHash& x, uint64_t h) { return x.hash < h; });
    return it != e && it->hash == hash ? it->stage : (uint8_t)kNone;
}
// A pipeline's stage: by its pixel shader (a compute pipeline: `ps` is its compute shader); by its vertex shader only when it has no pixel
// shader (the depth-only shadow pipelines) - a vertex shader shared with other passes (vs_models_shadows_v draws a velocity quad too) never
// decides for a pipeline that has a pixel shader of its own.
inline uint8_t Classify(uint64_t ps, uint64_t vs, bool compute) {
    if (ps) return StageOf(ps);
    if (compute || !vs) return kNone;
    const uint8_t s = StageOf(vs);
    return s == kShadows ? s : (uint8_t)kNone;
}

// Whether a call of a pipeline of stage `s` is left out under `mask` (bits of Key)
inline bool Skips(uint32_t mask, uint8_t s) {
    if (!mask || s == kNone || s >= kStages) return false;
    const int k = KeyOfStage(s);
    return k >= 0 && (mask >> k & 1u);
}

// "shadows_sun, lit" - the keys on, "none" with none
inline std::string MaskText(uint32_t mask) {
    if (!(mask & kAll)) return "none";
    if ((mask & kAll) == kAll) return "ALL";
    std::string s;
    for (int k = 0; k < kKeys; ++k)
        if (mask >> k & 1u) { if (!s.empty()) s += ", "; s += KeyName(k); }
    return s;
}

}  // namespace renderbench
