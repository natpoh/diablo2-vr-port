// vr/sky/sky_choice.h - who draws the sky, the fog and the cave ceiling here (vr/sky/sky_choice.cpp, docs/plan_modules.md M6):
// our effect's post pass or the game's own pass - never both (vr/sky_paths.h). Read live by every drawer.
#pragma once

#include <atomic>

namespace vrc {

// The area's state SkyTick sets (vrcam.cpp, on the update thread at each new biome) that the answers read.
extern std::atomic<int> g_skyPalette;      // the act's sky wanted here (kPalettes index), else -1
extern std::atomic<bool> g_arcaneSky;      // the Arcane Sanctuary's own sky shows now
extern std::atomic<bool> g_underground;    // in the world, off the outdoor list
extern std::atomic<bool> g_noFogArea;      // [fog] no_fog_river: the River of Flame
extern std::atomic<bool> g_ceilBiome;      // the area's biome is on [ceiling] biomes
extern std::atomic<bool> g_gameCeilBiome;  // ... on [ceiling] game_ceiling_biomes
bool GameDrawnView();                      // a view the game's own dome, slab and fog are drawn in (vrcam.cpp)

namespace fx {
bool OurFogWanted();          // a fog wanted here at all (either path)
const char* FogCannot();      // the game's fog unable here for good ("" / nullptr: able)
bool FogWanted();             // our post pass's fog
bool OurSkyWanted();          // our post pass's act sky
bool CeilWanted();            // a cave ceiling wanted here ([ceiling] enabled, first person, a ceiling biome, no menu)
bool GameCeilWanted();        // ... of the game's rock ([ceiling] game_ceiling's areas)
}  // namespace fx

}  // namespace vrc
