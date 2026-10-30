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

bool OurFogWanted() {
    // under the Arcane Sanctuary's sky only with [sky] arcane_fog: the shader then takes the fog's colour from that sky. That sky's own key
    // decides there, not [fog] caves: the Sanctuary is off [sky] outdoor (so "underground") but floats in its open void - PC1 2026-10-28,
    // area 74 with caves=0 and arcane_fog=1: no fog at all
    const bool arcane = g_arcaneSky.load();
    return (!arcane || g_set.arcaneFog.load()) && g_set.fogOn.load() && g_enabled.load() && !Overhead() && g_inWorld.load() && !EffectsPaused() &&
           (g_set.fogCaves.load() || !g_underground.load() || arcane) &&   // [fog] caves=0: none underground
           !g_noFogArea.load();   // [fog] no_fog_river: none in the River of Flame (our effect's and the game's fog alike)
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
