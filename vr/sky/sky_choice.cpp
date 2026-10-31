// vr/sky/sky_choice.cpp - who draws the sky, the fog and the cave ceiling here: our effect's post pass or the game's own (the rule is
// vr/sky_paths.h; these are its answers for this area and view) - cut from vr/vrcam.cpp (docs/plan_modules.md M6). Every drawer asks
// these (the effect's uniforms, the game's dome and slab, the fog pass); one answer per question, in one file.
#include "sky_choice.h"
#include "../vrcam_internal.h"
#include "../config/settings.h"
#include "../view/view_state.h"
#include "../sky_paths.h"

namespace vrc {
// [fog] game_fog (vrcam.cpp's gamefogvr, not vr/game_fog_vr.h's): not found for long; the game's fog drawn now
namespace gamefogvr { extern std::atomic<bool> g_bad; bool Active(); }
namespace fx {

// [fog] caves=0: no fog underground, the game's and ours alike (Serg 2026-10-31: "туман в пещерах я бы пока отключил полностью") - but
// the Arcane Sanctuary keeps it with [sky] arcane_fog, its sky shown or not ("а в аркан включил"): it is off [sky] outdoor (so
// "underground") yet floats in its open void - PC1 2026-10-28, area 74 with caves=0 and arcane_fog=1: no fog at all
bool NoFogUnderground() {
    return g_inWorld.load() && g_underground.load() && !g_set.fogCaves.load() && !(g_arcaneArea.load() && g_set.arcaneFog.load());
}
bool OurFogWanted() {
    // under the Arcane Sanctuary's sky only with [sky] arcane_fog: the shader then takes the fog's colour from that sky
    const bool arcane = g_arcaneSky.load();
    return (!arcane || g_set.arcaneFog.load()) && g_set.fogOn.load() && g_enabled.load() && !Overhead() && g_inWorld.load() && !EffectsPaused() &&
           !NoFogUnderground() &&   // [fog] caves=0: none underground (the Arcane Sanctuary with arcane_fog apart)
           !g_noFogArea.load();     // [fog] no_fog_river: none in the River of Flame (our effect's and the game's fog alike)
}
// The post pass's fog: ours, unless the game's own pass draws it ([fog] game_fog)
// ... and only when the game's is not chosen ([fog] game_fog: its pass draws ours, also before it took our numbers - never both; the Arcane
// Sanctuary's ours whatever the choice - vr/sky_paths.h)
// (the game's unable here for good - a view it does not draw in, its frame fog not found: ours instead; vr/sky_paths.h GameFogCannot)
const char* FogCannot() { return skypaths::GameFogCannot(GameDrawnView(), gamefogvr::g_bad.load()); }
bool FogWanted() {
    return skypaths::OurFog(OurFogWanted(), skypaths::GameHolds(g_set.gameFog.load(), FogCannot()), g_arcaneSky.load()) && !gamefogvr::Active();
}
// Our post pass's act sky: wanted here and the game's dome not chosen ([sky] game_sky), or chosen and unable here for good (its files
// missing - a fresh install - refused, no model, a view it is not drawn in: engitem::SkyCannot) - vr/sky_paths.h. g_skyPalette itself stays
// the act's sky wanted here: the game's dome is laid by it.
bool OurSkyWanted() { return g_skyPalette.load() >= 0 && !skypaths::GameHolds(g_set.gameSky.load(), engitem::SkyCannot()); }
// [render] distance_blur (D2R_DepthFog.fx DistBlur*): where our fog could be - first person, in the world, not from above (the ground is
// all one distance there), not while the effects are paused - and whoever draws the fog: the blur rides on the picture as it is
bool DistanceBlurWanted() {
    return g_set.distanceBlur.load() && g_set.distanceBlurStrength.load() > 0.001f && g_enabled.load() && !Overhead() && g_inWorld.load() &&
           !EffectsPaused();
}
// Our effect's fixes on what the game draws find the dome and the fogged world by the scene's depth (D2R_DepthFog.fx: d <= SkyDomeDepth):
// the dome's colours and night ([sky] dome_true_colour, day_night: DomeLook), its low part into the fog ([sky] dome_fog_horizon), the game's
// fog in the act's colour ([fog] game_fog_match) - as effects.cpp sets them. The flat game copies the depth only for what is placed by it
// (fx::DepthEffectsWanted): with the game's sky and no fog of ours none was copied, the effect read "no depth" and gave the picture as it
// was - Serg on 0.192.6, flat F2, fog off: "цвет купола не меняется", day or night.
bool GameSkyFixWanted() {
    const bool dome = engitem::DomeShown() && g_skyPalette.load() >= 0 && !g_arcaneSky.load() &&
                      ((g_set.domeTint.load() && (g_set.domeTrueColour.load() || g_set.skyDayNight.load())) ||   // (the tinting: parked)
                       (g_set.domeFogHorizon.load() && (FogWanted() || gamefogvr::Active())));
    return dome || (g_set.fogMatch.load() && gamefogvr::Active());   // (its make-up of the share with the colour measured or not)
}

bool ArcaneOwnFog() { return g_set.arcaneOwnFog.load() && g_arcaneArea.load(); }
FogNumbers FogNumbersNow() {
    if (ArcaneOwnFog())
        return {g_set.arcaneFogStart.load(), g_set.arcaneFogEnd.load(), g_set.arcaneFogCurve.load(), g_set.arcaneFogStrength.load()};
    if (g_underground.load()) return {g_set.fogCaveStart.load(), g_set.fogCaveEnd.load(), g_set.fogCurve.load(), g_set.fogCaveStrength.load()};
    return {g_set.fogStart.load(), g_set.fogEnd.load(), g_set.fogCurve.load(), 1.0f};
}
FogNumbers GameFogNumbersNow() {
    FogNumbers n = FogNumbersNow();
    if (ArcaneOwnFog()) { n.start = g_set.gameArcaneFogStart.load(); n.end = g_set.gameArcaneFogEnd.load(); }
    else if (g_underground.load()) { n.start = g_set.gameFogCaveStart.load(); n.end = g_set.gameFogCaveEnd.load(); }
    else { n.start = g_set.gameFogStart.load(); n.end = g_set.gameFogEnd.load(); }
    return n;
}
bool ArcaneHazeFog() { return g_arcaneSky.load() && (!ArcaneOwnFog() || g_set.arcaneFogIntoSky.load()); }

// The cave ceiling (D2R_DepthFog.fx Ceiling): from inside only - VR F4 and flat F3,
// never from above, from behind or on the floor (all three run as ThirdPerson).
bool CeilWanted() {
    return g_set.ceilOn.load() && g_enabled.load() && !ThirdPerson() && g_inWorld.load() && g_ceilBiome.load() &&
           !EffectsPaused();
}
// A cave ceiling of the game's rock wanted here ([ceiling] game_ceiling's areas): [ceiling] enabled, first person, in a world, an area on
// [ceiling] game_ceiling_biomes (a natural cave - on [ceiling] biomes or not), no menu
bool GameCeilWanted() {
    return g_set.ceilOn.load() && g_enabled.load() && !ThirdPerson() && g_inWorld.load() && g_gameCeilBiome.load() && !MenuFlat();
}

}  // namespace fx
}  // namespace vrc
