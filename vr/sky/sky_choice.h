// vr/sky/sky_choice.h - who draws the sky, the fog and the cave ceiling here (vr/sky/sky_choice.cpp, docs/plan_modules.md M6):
// our effect's post pass or the game's own pass - never both (vr/sky_paths.h). Read live by every drawer.
#pragma once

#include <atomic>

namespace vrc {

// The area's state SkyTick sets (vrcam.cpp, on the update thread at each new biome) that the answers read.
extern std::atomic<int> g_skyPalette;      // the act's sky wanted here (kPalettes index), else -1
extern std::atomic<bool> g_arcaneSky;      // the Arcane Sanctuary's own sky shows now
extern std::atomic<bool> g_underground;    // in the world, off the outdoor list
extern std::atomic<bool> g_arcaneArea;     // in the Arcane Sanctuary (act2_arcane), its sky shown or not
extern std::atomic<bool> g_noFogArea;      // [fog] no_fog_river: the River of Flame
extern std::atomic<bool> g_ceilBiome;      // the area's biome is on [ceiling] biomes
extern std::atomic<bool> g_gameCeilBiome;  // ... on [ceiling] game_ceiling_biomes
bool GameDrawnView();                      // a view the game's own dome, slab and fog are drawn in (vrcam.cpp)

namespace fx {
bool FogHere();               // a fog here at all, whoever draws it ([fog] caves, the River of Flame, first person...)
bool OurFogWanted();          // our effect's fog: [fog] enabled and FogHere
bool GameFogWanted();         // the game's fog with our numbers: [fog] game_fog and FogHere ([fog] enabled not asked)
const char* FogCannot();      // the game's fog unable here for good ("" / nullptr: able)
bool FogWanted();             // our post pass's fog
// The fog's numbers here: the open air's ([fog] start, end), the caves' (every area off the outdoor list: caves_start, caves_end,
// caves_strength), or the Arcane Sanctuary's own ([fog] arcane_own: arcane_start ... arcane_curve) - every fog takes them from here
struct FogNumbers { float start, end, curve, strength; };
FogNumbers FogNumbersNow();
FogNumbers GameFogNumbersNow();   // the same for the game's fog ([fog] game_fog=1): a full fog below its start taken the other way round
bool NoFogUnderground();      // [fog] caves=0 and the area off the outdoor list (not the Arcane Sanctuary with [sky] arcane_fog): no fog at all
bool ArcaneOwnFog();          // [fog] arcane_own, in the Arcane Sanctuary: its own numbers and colour, not the caves'
bool ArcaneHazeFog();         // the fog's colour is the Arcane Sanctuary's sky behind each point (its sky shown; arcane_into_sky with its own fog)
bool OurSkyWanted();          // our post pass's act sky
bool DistanceBlurWanted();    // [render] distance_blur: the far picture blurred behind a pane (our post pass, whoever draws the fog)
bool GameSkyFixWanted();      // a fix of ours on the game's dome or fog that reads the scene's depth (its colour, its night, its horizon, the fog's match)
bool CeilWanted();            // a cave ceiling wanted here ([ceiling] enabled, first person, a ceiling biome, no menu)
bool GameCeilWanted();        // ... of the game's rock ([ceiling] game_ceiling's areas)
}  // namespace fx

}  // namespace vrc
