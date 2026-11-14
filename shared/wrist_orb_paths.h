// wrist_orb_paths.h - [hud] wrist_game_models: where the wrist medallions' game models and units are (shared/wrist_orb_models.h makes them in D2R VR
// Settings; vrcam engitem::WristOrbsFrame loads the units). No dependencies: the plugin includes it without the model reader.
#pragma once

namespace wristorbs {

constexpr int kLife = 0, kMana = 1;
// The medallion's shape in units of the base's radius - the effect's own medallion's (vr/hand_orbs.h kDomeR ..., reshade/D2R_DepthFog.fx OrbMedal; the
// test checks they are the same numbers): the glass dome's radius (its flat face on the base's top plane), the plate's thickness under that plane, and a
// raised bezel ring round the dome's foot out to kMedBezelOuter, kMedBezelH high; the plate runs out to radius 1.
constexpr float kMedDomeR = 0.80f, kMedPlateT = 0.20f, kMedBezelOuter = 0.91f, kMedBezelH = 0.09f;
constexpr int kLevels = 64;   // the liquid's level meshes in its file (level k: the dome cut flat at k / kLevels of its height); a step 0.3 mm on a 2 cm dome

// The glass and the base: our dome and our disc in the lesser mana potion's own material (its glass at an empty-glass texel, drawn see-through with the
// game's per-entity opacity; its cap at a gold texel), at two more of the game's test models. (0.214.1 - 0.214.3 used the game's bvt glass_sphere and
// blend_sphere as they are: the glass, with no ORM map and none of the game's refraction settings, drew as glossy black and hid the liquid.)
constexpr const char* kGlassModel = "data/hd/bvt/models/detail_normal_sphere.model";
constexpr const char* kPlateModel = "data/hd/bvt/models/skin_sphere.model";   // (the gold base; kPlateMetal below has all three)
constexpr const char* kGlassFile = "data/hd/bvt/models/detail_normal_sphere_lod0.model";
constexpr const char* kPlateFile = "data/hd/bvt/models/skin_sphere_lod0.model";
constexpr const char* kGlassMaterial = "d2r_vr_wrist_orb_glass";
constexpr const char* kPlateMaterial = "d2r_vr_wrist_orb_base";
constexpr const char* kPlateSourceMesh = "solidShape";   // the potion's cork, cap and rope: its gold
// The glass: the lesser mana potion's glass mesh's material (its own painted glass, a StandardMaterial) at its top-tenth texel, see-through by the
// game's per-entity opacity ([hud] wrist_game_glass: the TransparentComponent of its drawn entity, the model's - the unit's root has none; the liquid
// under it stays solid - a see-through one is hidden whole by the dome's depth). (0.215.8 tried the healing potion's "alphaShape", GlassMaterial: in the headset an opaque milky-white egg
// over the liquid - a GlassMaterial is as see-through as 1 - its albedo picture's alpha, and every potion picture's alpha is 255.)
constexpr const char* kGlassSource = "data/hd/items/misc/potion/lesser_mana_potion/lesser_mana_potion_lod0.model";
constexpr const char* kGlassSourceMesh = "glassShape";
// Our liquid lens in the potions' files, at two more of the game's test models (only "_lod0": the game ships these with that one LOD)
constexpr const char* kLiquidModel[2] = {"data/hd/bvt/models/hair_sphere.model", "data/hd/bvt/models/foliage_sphere.model"};
constexpr const char* kLiquidFile[2] = {"data/hd/bvt/models/hair_sphere_lod0.model", "data/hd/bvt/models/foliage_sphere_lod0.model"};
// The potions whose glass material the liquid takes: lesser healing (red) and lesser mana (blue), the same uv layout
constexpr const char* kSource[2] = {"data/hd/items/misc/potion/lesser_healing_potion/lesser_healing_potion_lod0.model",
                                    "data/hd/items/misc/potion/lesser_mana_potion/lesser_mana_potion_lod0.model"};
constexpr const char* kSourceMesh = "glassShape";
constexpr const char* kMaterialName[2] = {"d2r_vr_wrist_orb_life", "d2r_vr_wrist_orb_mana"};
// The base for each metal ([hud] hand_vitals_orb_metal: 0 gold, 1 bronze, 2 silver) and its patina ([hud] hand_vitals_orb_patina): the effect's plate in a
// potion's own cap material, from a potion picture with that metal's embossed rosette (decoded 2026-11-13, uv = pixel / size): the full mana potion's gold
// sunburst, the elixir's big bronze sunburst, the antidote's pewter disc with its carved face. Each file holds two meshes for the game's mesh flipbook:
// [0] clean - every uv one smooth texel inside the rosette in the metal's own hue; [1] aged - the rosette laid across the plate seen from above (u = cu +
// x r, v = cv + z r): its embossing and the dark worn crevices. The plugin shows [1] from kPatinaAged on. Each at a free test model path (the two the glass
// and the plate of 0.214.1 - 0.214.3 used as they are, free again).
struct PlateMetal {
    const char* name;
    const char* source;       // the potion's model
    const char* sourceMesh;   // its mesh whose material holds the picture
    float cu, cv, r;          // the worn-metal disc the aged base is laid from: its middle and radius in the picture (uv)
    float cleanU, cleanV;     // the clean texel
    const char* file;         // ours, "_lod0" (a path the game knows)
    const char* model;
    const char* unit;
    const char* material;
    unsigned salt;
};
constexpr PlateMetal kPlateMetal[3] = {
    {"gold", "data/hd/items/misc/potion/full_mana_potion/full_mana_potion_lod0.model", "solidShape", 0.225f, 0.400f, 0.032f, 0.2383f, 0.4199f,
     "data/hd/bvt/models/skin_sphere_lod0.model", "data/hd/bvt/models/skin_sphere.model", "data/hd/env/model/d2r_vr/orbs/orb_plate_unit.json",
     "d2r_vr_wrist_orb_base", 4u},
    {"bronze", "data/hd/items/misc/potion/elixir/elixir_lod0.model", "elixir_potionShape", 0.470f, 0.190f, 0.034f, 0.5996f, 0.4375f,
     "data/hd/bvt/models/blend_sphere_lod0.model", "data/hd/bvt/models/blend_sphere.model", "data/hd/env/model/d2r_vr/orbs/orb_plate_bronze_unit.json",
     "d2r_vr_wrist_orb_base_bronze", 5u},
    {"silver", "data/hd/items/misc/potion/antidote_potion/antidote_potion_lod0.model", "solidShape", 0.790f, 0.117f, 0.10f, 0.7598f, 0.1367f,
     "data/hd/bvt/models/glass_sphere_lod0.model", "data/hd/bvt/models/glass_sphere.model", "data/hd/env/model/d2r_vr/orbs/orb_plate_silver_unit.json",
     "d2r_vr_wrist_orb_base_silver", 6u},
};
// The discs: one even worn-metal area of each picture, the way silver's was from the start (its coin-like plate, 0.10 round its middle). Gold and bronze
// took more than their metal: past the small embossed disc each picture holds the texture's stretched padding (radial streaks of the edge pixels), and
// gold's 0.09 also some of the grey unused area - 0.215.12, Serg: "a white piece on the patina" (gold), "bronze has problems too" (light and dark bands
// across the plate and bezel: bronze's 0.19 round the rosette's middle took its streaked hub and bezel). Now gold: its embossed disc alone (it ends at
// ~0.036, streaks past it); bronze: a patch of the worn copper ring left of the hub, (0.470, 0.190), no off-colour pixel within 0.036.
constexpr float kPatinaAged = 0.30f;   // [hud] hand_vitals_orb_patina (0 - 1) from which the aged mesh shows

// Our units
constexpr const char* kGlassUnit = "data/hd/env/model/d2r_vr/orbs/orb_glass_unit.json";
constexpr const char* kPlateUnit = "data/hd/env/model/d2r_vr/orbs/orb_plate_unit.json";
constexpr const char* kLiquidUnit[2] = {"data/hd/env/model/d2r_vr/orbs/orb_life_unit.json", "data/hd/env/model/d2r_vr/orbs/orb_mana_unit.json"};

}  // namespace wristorbs
