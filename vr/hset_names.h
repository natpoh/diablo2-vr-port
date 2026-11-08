#pragma once
// Names for the values of the in-headset settings panel's stepper rows that are not numbers. Pure.
//
// The HUD page's "Toolbar type" row, whose value is the pair [hud] hide + [hud] bar_place (shared/toolbar_type.h):
// the box shows "Wrists", "Chest, low", "Low in front", "Left forearm", "Right forearm", "Not shown" or "In the picture".
// A row with no name is shown with its Def's own printf format, as every other.

#include <cmath>

#include "hset_defs.h"
#include "draw_by.h"   // the Video page's Sky / Fog drawn by
#include "render_type.h"   // the Video page's Render type (shared/render_type.h)
#include "toolbar_type.h"
#include "ui_text.h"

namespace hset {

// A toolbar type's short name in the player's language ("vr.toolbar.<name>": vr/ui_text_tables.h lists them for lang\en.txt).
inline std::string ToolbarKey(int type) { return "vr.toolbar." + uitext::Slug(toolbartype::kShort[type]); }
inline const char* ToolbarName(int type) { return uitext::Tr(ToolbarKey(type), toolbartype::kShort[type]); }

// The Melee > Attack page's Direct hit (shared/direct_hit_mode.h): its four choices, short for the value box (the line under the row says the rest).
inline const char* DirectHitName(int mode) {
    switch (mode) {
        case 1: return uitext::Tr("vr.directhit.cleave", "2H extras");
        case 2: return uitext::Tr("vr.directhit.rules", "By the rules");
        case 3: return uitext::Tr("vr.directhit.always", "Always");
        default: return uitext::Tr("vr.directhit.off", "Off");
    }
}

// The HUD page's "Allies shown" ([hud] allies_place, shared/allies_place.h): its three places, short for the value box.
inline const char* AlliesPlaceName(int place) {
    switch (place) {
        case 1: return uitext::Tr("vr.allies.map", "Under the map");
        case 2: return uitext::Tr("vr.allies.wrist", "On the wrist");
        default: return uitext::Tr("vr.allies.game", "At the top");
    }
}

// The Video page's "Sky drawn by" / "Fog drawn by" (vr/draw_by.h): D2R VR Settings' names for the two ways, and none.
inline const char* DrawByName(int way, bool sky) {
    switch (way) {
        case drawby::kOurs: return uitext::Tr("vr.drawby.ours", "Our effect");
        case drawby::kGame: return sky ? uitext::Tr("vr.drawby.game_sky", "The game's dome") : uitext::Tr("vr.drawby.game_fog", "The game's fog");
        default: return uitext::Tr("vr.drawby.none", "None");
    }
}

// The Video page's "Render type" (shared/render_type.h): the five types' names, in the player's language
inline rendertype::Keys RenderTypeKeys(const Values& v) {
    rendertype::Keys k;
    k.openxrOn = v[kKeyOpenXrOn] > 0.5f ? 1 : 0;
    k.afr = v[kKeyAfr] > 0.5f ? 1 : 0;
    k.pairPerTick = v[kKeyPairPerTick] > 0.5f ? 1 : 0;
    k.nativeReplay = v[kKeyNativeReplay] > 0.5f ? 1 : 0;
    k.source = (int)std::lround(v[kKeySource]);
    k.replayRight = (int)std::lround(v[kKeyReplayRight]);
    k.replayAhead = (int)std::lround(v[kKeyReplayAhead]);
    return k;
}
// The render type row's line under its name: when the type now in the draft takes effect (Serg's 0.207.7 report: a choice that seemed to do nothing).
// replayAtStart: the one-pass hooks went in at the game's start (video::Env).
inline const char* RenderTypeWhen(int type, bool replayAtStart) {
    switch (type) {
        case rendertype::kNativeOne:
            return replayAtStart ? uitext::Tr("vr.rendertype.when_one", "One pass: right away, once an area has settled (a paused frame is two passes)")
                                 : uitext::Tr("vr.rendertype.when_one_restart", "One pass: AFTER A RESTART of the game (it was off when the game started)");
        case rendertype::kNativeTwo: return uitext::Tr("vr.rendertype.when_two", "Two passes: right away");
        case rendertype::kNativeTurns: return uitext::Tr("vr.rendertype.when_turns", "Eyes by turns: right away");
        default: return uitext::Tr("vr.rendertype.when_flatvr", "FlatVR: the game leaves the headset at Apply - FlatVR (BodyWalk) must show it");
    }
}
inline const char* RenderTypeName(int type) {
    switch (type) {
        case rendertype::kNativeTwo: return uitext::Tr("vr.rendertype.native_two", "Native two passes");
        case rendertype::kNativeOne: return uitext::Tr("vr.rendertype.native_one", "Native one pass");
        case rendertype::kNativeTurns: return uitext::Tr("vr.rendertype.native_turns", "Native eyes by turns");
        case rendertype::kFlatStereo: return uitext::Tr("vr.rendertype.flatvr_stereo", "FlatVR stereo");
        default: return uitext::Tr("vr.rendertype.flatvr_depth", "FlatVR depth");
    }
}

// The Render page's three steppers (Serg 2026-11-08), as D2R VR Settings names their choices: foveated rendering's strength, its heavy passes,
// the grass lighting's rate
inline const char* FoveatedStrengthName(int s) {
    switch (s) {
        case 0: return uitext::Tr("vr.render.strength_low", "Low");
        case 2: return uitext::Tr("vr.render.strength_high", "High");
        case 3: return uitext::Tr("vr.render.strength_max", "Maximum");
        default: return uitext::Tr("vr.render.strength_medium", "Medium");
    }
}
inline const char* FoveatedPassesName(int p) {
    switch (p) {
        case 1: return uitext::Tr("vr.render.passes_grass", "Grass, reflections");
        case 2: return uitext::Tr("vr.render.passes_hair", "... and hair");
        default: return uitext::Tr("vr.render.passes_none", "No");
    }
}
inline const char* GrassRateName(int r) {
    switch (r) {
        case 1: return uitext::Tr("vr.render.grass_half", "Half (2x1)");
        case 2: return uitext::Tr("vr.render.grass_quarter", "Quarter (2x2)");
        default: return uitext::Tr("vr.render.grass_game", "As the game");
    }
}

// The Quality preset the values match (shared/quality_presets.h): 0 Potato, 1 Low, 2 Medium, qualitypreset::kCount Custom
inline int PresetOf(const Values& v) {
    return qualitypreset::Match([&](const qualitypreset::Key& k) -> float {
        if (qualitypreset::IsRenderType(k)) return (float)rendertype::FromKeys(RenderTypeKeys(v));
        const int i = IndexOf(k.section, k.key);
        return i >= 0 ? v[i] : -1.0e9f;
    });
}
inline const char* PresetName(int p) {
    switch (p) {
        case 0: return uitext::Tr("vr.preset.potato", "Potato");
        case 1: return uitext::Tr("vr.preset.low", "Low");
        case 2: return uitext::Tr("vr.preset.medium", "Medium");
        default: return uitext::Tr("vr.preset.custom", "Custom");
    }
}

// The name to show for a row's value, nullptr for a row that shows a number.
inline const char* ValueName(const Values& v, int key) {
    if (key == kKeyPreset) return PresetName(PresetOf(v));
    if (key == kKeyPairPerTick) return RenderTypeName(rendertype::FromKeys(RenderTypeKeys(v)));
    if (key == kKeyFoveatedStrength) return FoveatedStrengthName((int)std::lround(v[key]));
    if (key == kKeyFoveatedPasses) return FoveatedPassesName((int)std::lround(v[key]));
    if (key == kKeyGrassRate) return GrassRateName((int)std::lround(v[key]));
    if (key == kKeyGameSky) return DrawByName(drawby::FromKeys(v[kKeySkyOn] > 0.5f, v[kKeyGameSky] > 0.5f), true);
    if (key == kKeyGameFog) return DrawByName(drawby::FromKeys(v[kKeyFogOn] > 0.5f, v[kKeyGameFog] > 0.5f, true), false);
    if (key == kKeyAlliesPlace) return AlliesPlaceName((int)std::lround(v[kKeyAlliesPlace]));
    if (key == kKeyBarPlace) return ToolbarName(toolbartype::TypeOf((int)std::lround(v[kKeyHide]), (int)std::lround(v[kKeyBarPlace])));
    if (key == kKeyMeleeDirectClient) return DirectHitName((int)std::lround(v[kKeyMeleeDirectClient]));
    return nullptr;
}

}  // namespace hset
