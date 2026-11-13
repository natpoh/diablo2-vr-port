// wrist_look.h - [hud] wrist_look: the look of the life and mana medallions on the wrists (VR first person, F4; the toolbar type "Wrists") - one of
// three. Pure; D2R VR Settings' "Look" choice and the in-headset settings panel's "Medallions: look" row (vr/hset_*.h) both write this one key, the
// plugin reads it (vr/config/load_settings.cpp).
//   0  the mod's 3D glass balls (the effect draws them) - the default
//   1  the game's own globes, cut out of its toolbar
//   2  the same medallions drawn by the game's own models (their files are made by D2R VR Settings from the player's game, and taken out again when
//      another look is chosen there - so a pick of 2 in the headset has nothing to draw until Settings has run once and the game has started again)
#pragma once

namespace wristlook {

constexpr int kMod = 0, kGlobes = 1, kGameModels = 2;
constexpr int kCount = 3;
// The order both programs list them in: the mod's, the game's models, the game's globes.
constexpr int kOrder[kCount] = {kMod, kGameModels, kGlobes};

constexpr int Clamp(int look) { return look < 0 || look >= kCount ? kMod : look; }
// The next look round the list (delta > 0) or the one before it.
constexpr int Step(int look, int delta) {
    look = Clamp(look);
    int at = 0;
    for (int i = 0; i < kCount; ++i) if (kOrder[i] == look) at = i;
    return kOrder[(at + (delta > 0 ? 1 : delta < 0 ? kCount - 1 : 0)) % kCount];
}
static_assert(Step(kMod, 1) == kGameModels && Step(kGameModels, 1) == kGlobes && Step(kGlobes, 1) == kMod && Step(kMod, -1) == kGlobes && Step(7, 1) == kGameModels,
              "round the list, in Settings' order; a value out of range is the mod's");

// The game models' units as the plugin found them (vrcam's engitem): not asked yet this game, all there, or some missing (not made).
constexpr int kUnitsUnknown = -1, kUnitsMissing = 0, kUnitsLoaded = 1;
// The pick has nothing to draw: the game's models are chosen and their files are not there.
constexpr bool NotMade(int look, int units) { return Clamp(look) == kGameModels && units == kUnitsMissing; }

}  // namespace wristlook
