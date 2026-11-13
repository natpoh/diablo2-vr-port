// wrist_models.h - [hud] wrist_game_models: where the game's own models of a wrist medallion are laid (VR F4). Pure: no game, no Windows (the test,
// cleanroom/tests/wrist_orb_models_test.cpp). The medallion's place, frame, eased level and low-life heartbeat are today's (vr/hand_orbs.h Step, worked
// out in the effect's frame block, vr/render/effects/effects.cpp); this turns them into the local matrices of three of the game's models
// (shared/wrist_orb_models.h): the glass ball, the plate, the liquid lens. engitem::WristOrbsFrame lays them on the registry's own thread.
//
// The matrices are the engine units' (vr/engine/engitem.cpp): rows 0 - 2 the model's x, y, z axes in the world, each times its scale, row 3 the place.
// Every model stands up along its own +y, so +y goes out of the back of the hand (`back`), +x along the hand toward the knuckles (`along`), +z = x cross y.
// The models are in the world's units (feet: the files' UnitsPerMeter 3.28); `perM` world units a metre.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hand_orbs.h"   // the medallion's shape: kDomeR, kPlateT (the effect's own numbers)
#include "wrist_orb_paths.h"   // kLevels: the liquid's level meshes

namespace wristmodels {

constexpr int kGlass = 0, kPlate = 1, kLiquid = 2, kParts = 3;

// The liquid lies just inside the glass (no two surfaces on one place), and an emptied ball shows no sliver
constexpr float kLiquidInside = 0.985f;   // of the dome's radius, across and up: the liquid fills the dome as the effect's does (just inside its glass)
constexpr float kLiquidMinFill = 0.01f;   // under it: the liquid put away
constexpr float kGlassOpacityMin = 0.01f;                 // the glass's opacity ([hud] wrist_game_glass) under it: no glass

// One medallion as the frame block has it, in the world: its middle on the base's top plane (`c`, world units), out of the back of the hand (`back`)
// and along the hand (`along`, square to back or nearly), the base's radius (world units), the eased level 0 - 1, the heartbeat 0 - 1, shown or not.
struct Orb {
    bool on = false;
    float c[3] = {};
    float back[3] = {0.0f, 1.0f, 0.0f};
    float along[3] = {1.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float fill = 0.0f;
    float glow = 0.0f;
    float glass = 0.25f;   // the glass's opacity ([hud] wrist_game_glass; under kGlassOpacityMin: no glass)
    int metal = 0;         // [hud] hand_vitals_orb_metal: the base's metal (0 gold, 1 bronze, 2 silver)
    float patina = 0.55f;  // [hud] hand_vitals_orb_patina (0 - 1): from wristorbs::kPatinaAged the aged base
    int kind = 0;   // 0 life, 1 mana
};
struct Want {
    Orb orb[2];
    uint64_t stamp = 0;   // the frame block's clock (ms); a stale want is put away
    // Each medallion's place in its wrist's frame (ArmFrame of the drawn arm the frame block used; absent when the wrist's frame came from elsewhere -
    // the controller carrying the last arm): re-laid on the arm as the skeleton hook poses it, the same frame the game skins the hero (no lag behind the
    // wrist while the hero moves). Positions in metres (the world's units over perM), directions unit, in the frame's own axes.
    bool rel[2] = {};
    int side[2] = {};          // the skeleton's: 0 right, 1 left
    float relC[2][3] = {}, relBack[2][3] = {}, relAlong[2][3] = {};
    float radiusM[2] = {};
    float perM = 1.0f;
};

// A wrist's frame, made the same way from the frame block's arm (eye-relative metres) and from the posed arm (world units): its origin the wrist, x along
// the hand (wrist -> middle knuckle when the knuckles are known, else the forearm elbow -> wrist), y the palm's normal made square to x, z = x cross y.
struct ArmFrame { float o[3], x[3], y[3], z[3]; };
inline bool MakeFrame(const float wrist[3], const float elbow[3], const float* point, const float across[3], ArmFrame* f) {
    float x[3];
    if (point) for (int i = 0; i < 3; ++i) x[i] = point[i];
    else for (int i = 0; i < 3; ++i) x[i] = wrist[i] - elbow[i];
    const float lx = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (!(lx > 1e-6f) || !std::isfinite(lx)) return false;
    for (float& v : x) v /= lx;
    float y[3] = {across[0], across[1], across[2]};
    const float d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    for (int i = 0; i < 3; ++i) y[i] -= d * x[i];
    const float ly = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
    if (!(ly > 1e-6f) || !std::isfinite(ly)) return false;
    for (float& v : y) v /= ly;
    for (int i = 0; i < 3; ++i) { f->o[i] = wrist[i]; f->x[i] = x[i]; f->y[i] = y[i]; }
    f->z[0] = x[1] * y[2] - x[2] * y[1]; f->z[1] = x[2] * y[0] - x[0] * y[2]; f->z[2] = x[0] * y[1] - x[1] * y[0];
    return true;
}
// A point (`point` true) or a direction into the frame's axes, and back
inline void ToFrame(const ArmFrame& f, const float v[3], bool point, float out[3]) {
    float d[3] = {v[0], v[1], v[2]};
    if (point) for (int i = 0; i < 3; ++i) d[i] -= f.o[i];
    out[0] = d[0] * f.x[0] + d[1] * f.x[1] + d[2] * f.x[2];
    out[1] = d[0] * f.y[0] + d[1] * f.y[1] + d[2] * f.y[2];
    out[2] = d[0] * f.z[0] + d[1] * f.z[1] + d[2] * f.z[2];
}
inline void FromFrame(const ArmFrame& f, const float v[3], bool point, float scale, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = (f.x[i] * v[0] + f.y[i] * v[1] + f.z[i] * v[2]) * scale + (point ? f.o[i] : 0.0f);
}

// The three matrices of a medallion; `show[p]` false: that part is put away (no frame, a level under the minimum, not shown). False: none shown.
// `level`: the liquid's level mesh to show (1 - kLevels; 0 none) - the liquid keeps the dome's shape, cut flat at the level (the file's meshes).
inline bool Matrices(const Orb& o, float m[kParts][16], bool show[kParts], int* level = nullptr) {
    for (int p = 0; p < kParts; ++p) show[p] = false;
    if (level) *level = 0;
    if (!o.on || !(o.radius > 0.0f) || !std::isfinite(o.radius)) return false;
    float y[3] = {o.back[0], o.back[1], o.back[2]}, x[3] = {o.along[0], o.along[1], o.along[2]};
    float ly = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
    if (!(ly > 1e-4f) || !std::isfinite(ly)) return false;
    for (float& v : y) v /= ly;
    const float d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    for (int i = 0; i < 3; ++i) x[i] -= d * y[i];
    const float lx = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (!(lx > 1e-4f) || !std::isfinite(lx)) return false;
    for (float& v : x) v /= lx;
    const float z[3] = {x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
    for (float v : o.c) if (!std::isfinite(v)) return false;
    auto put = [&](float* out, float sx, float sy, float sz, float lift) {
        for (int i = 0; i < 3; ++i) {
            out[i] = x[i] * sx;
            out[4 + i] = y[i] * sy;
            out[8 + i] = z[i] * sz;
            out[12 + i] = o.c[i] + y[i] * lift;
        }
        out[3] = out[7] = out[11] = 0.0f;
        out[15] = 1.0f;
    };
    const float dome = handorbs::DomeRadius(o.radius);
    // the plate: its file is the effect's plate in units of the base's radius (radius 1, kPlateT thick under the top plane, the bezel over it): the base's radius
    // every way, its top plane on the medallion's middle - its underside where the effect's is, on the skin (the place puts the middle a plate's
    // thickness out of the skin: handorbs::OnBone / the spot's lift)
    put(m[kPlate], o.radius, o.radius, o.radius, 0.0f);
    show[kPlate] = true;
    // the glass: the open dome (radius 1) at the dome's radius, its rim on the plate's top plane inside the bezel
    put(m[kGlass], dome, dome, dome, 0.0f);
    show[kGlass] = o.glass >= kGlassOpacityMin;
    // the liquid: its file's half ball (radius 1) at the dome's size, on the top plane; its level picks the mesh cut flat there (not a squash: 0.214.5
    // "it does not get cut, it just sinks")
    const float f = std::isfinite(o.fill) ? (o.fill < 0.0f ? 0.0f : o.fill > 1.0f ? 1.0f : o.fill) : 0.0f;
    const int k = f >= kLiquidMinFill ? std::max(1, std::min(wristorbs::kLevels, (int)std::lround(f * wristorbs::kLevels))) : 0;
    if (k > 0) {
        const float across = dome * kLiquidInside;
        put(m[kLiquid], across, across, across, 0.0f);
        show[kLiquid] = true;
        if (level) *level = k;
    }
    return true;
}

// The highlight of the liquid (the game's own hover value: its colour x (1 + h)): `pulse` at the heartbeat's top ([hud] wrist_game_pulse), 0 at rest.
inline float Highlight(float glow, float pulse) {
    if (!std::isfinite(glow) || !std::isfinite(pulse) || glow <= 0.0f || pulse <= 0.0f) return 0.0f;
    return (glow > 1.0f ? 1.0f : glow) * pulse;
}
// [hud] wrist_game_glow, live (0.215.8, Serg: "the liquid's brightness still doesn't work" - the material's EmissiveIntensity baked into its file did not
// change what is seen, reloaded or not): the same highlight, at rest - the liquid's colour x (1 + glow x kGlowToHighlight): 13 (the default) x 1.65,
// 40 x 3, 0 as the game draws it. The heartbeat's pulse adds on top.
constexpr float kGlowToHighlight = 0.05f;
inline float GlowHighlight(float glow) {
    if (!std::isfinite(glow) || glow <= 0.0f) return 0.0f;
    return (glow > 40.0f ? 40.0f : glow) * kGlowToHighlight;
}

}  // namespace wristmodels
