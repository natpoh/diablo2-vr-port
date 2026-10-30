// sky_choice_test - vr/sky/sky_choice.cpp, the plugin's own code (not a copy), in a made-up world: who draws the sky, the fog and the
// cave ceiling for every view, area and choice (docs/plan_modules.md, seam test 1 - the decision side).
//   1. never both skies: our post pass's sky (OurSkyWanted) is never wanted where the game's dome is chosen and able (sky_paths.h GameHolds);
//   2. never both fogs: our post pass's fog (FogWanted) is never wanted while the game's fog is drawn, nor where the game's is chosen and
//      able - the Arcane Sanctuary's own sky and fog excepted (ours whatever the choice);
//   3. the game's chosen and unable for good (its view, its fog not found, its dome's files missing): ours instead - no area without one;
//   4. no fog, no sky, no ceiling with the camera off, outside a game area, under a flat menu in VR (EffectsPaused);
//   5. a ceiling only from inside (never ThirdPerson, never from above) and only on a ceiling biome; the game's rock only on its list;
//   6. [fog] caves=0: no fog underground; [fog] no_fog_river: none in the River of Flame.
// The world vrcam.cpp keeps (the area's state SkyTick sets, the view, the game's fog and dome) is faked here, below.
#include "sky/sky_choice.h"
#include "config/settings.h"
#include "vrcam_internal.h"

#include <cstdio>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s  [%s]\n", __LINE__, #c, g_case); ++g_fail; } } while (0)
static char g_case[256] = "";

// ---- the made-up world (vrcam.cpp's names, as sky_choice.cpp reads them) ----
static bool w_overhead = false, w_third = false, w_menuFlat = false, w_effectsPaused = false, w_gameView = true, w_gameFogBad = false,
            w_gameFogActive = false;
static const char* w_skyCannot = nullptr;
namespace vrc {
std::atomic<bool> g_enabled{true}, g_inWorld{true};
std::atomic<int> g_skyPalette{0};
std::atomic<bool> g_arcaneSky{false}, g_underground{false}, g_noFogArea{false}, g_ceilBiome{false}, g_gameCeilBiome{false};
bool GameDrawnView() { return w_gameView; }
namespace gamefogvr {
std::atomic<bool> g_bad{false};
bool Active() { return w_gameFogActive; }
}
namespace engitem { const char* SkyCannot() { return w_skyCannot; } }
bool Overhead() { return w_overhead; }
bool ThirdPerson() { return w_third; }
bool MenuFlat() { return w_menuFlat; }
bool EffectsPaused() { return w_effectsPaused; }
// (vrcam.cpp's log: not called by the choice)
void Log(const char*) {}
void LogF(const char*, ...) {}
}  // namespace vrc

int main() {
    using namespace vrc;
    int cases = 0;
    // every combination of what the answers read
    for (int bits = 0; bits < (1 << 19); ++bits) {
        auto b = [&](int i) { return ((bits >> i) & 1) != 0; };
        g_set.gameSky.store(b(0));
        g_set.gameFog.store(b(1));
        g_set.fogOn.store(b(2));
        g_set.fogCaves.store(b(3));
        g_set.arcaneFog.store(b(4));
        g_set.ceilOn.store(b(5));
        g_arcaneSky.store(b(6));
        g_underground.store(b(7));
        g_noFogArea.store(b(8));
        g_skyPalette.store(b(9) ? 2 : -1);
        g_ceilBiome.store(b(10));
        g_gameCeilBiome.store(b(11));
        w_skyCannot = b(12) ? "its files missing" : nullptr;
        w_gameView = !b(13);
        w_gameFogBad = b(14);
        gamefogvr::g_bad.store(w_gameFogBad);
        w_gameFogActive = b(15) && !w_gameFogBad && g_set.gameFog.load();   // the game's fog is drawn only when chosen and found
        w_overhead = b(16);
        w_third = b(17);
        w_effectsPaused = w_menuFlat = b(18);
        g_enabled.store(true);
        g_inWorld.store(true);
        std::snprintf(g_case, sizeof g_case, "bits %05X", bits);
        ++cases;

        const bool gameSkyHolds = g_set.gameSky.load() && !w_skyCannot;
        const char* fogCannot = fx::FogCannot();
        const bool gameFogHolds = g_set.gameFog.load() && !fogCannot;
        // 1. never both skies
        if (gameSkyHolds) CHECK(!fx::OurSkyWanted());
        // 2. never both fogs
        if (w_gameFogActive) CHECK(!fx::FogWanted());
        if (gameFogHolds && !g_arcaneSky.load()) CHECK(!fx::FogWanted());
        // 3. unable for good: ours instead
        if (g_set.gameSky.load() && w_skyCannot && g_skyPalette.load() >= 0) CHECK(fx::OurSkyWanted());
        if (!g_set.gameSky.load()) CHECK(fx::OurSkyWanted() == (g_skyPalette.load() >= 0));
        CHECK((fogCannot != nullptr) == (!w_gameView || w_gameFogBad));
        if (fx::OurFogWanted() && !w_gameFogActive && (!g_set.gameFog.load() || fogCannot || g_arcaneSky.load())) CHECK(fx::FogWanted());
        // 4. ... none under a flat menu in VR
        if (w_effectsPaused) { CHECK(!fx::OurFogWanted()); CHECK(!fx::FogWanted()); CHECK(!fx::CeilWanted()); }
        if (w_overhead) { CHECK(!fx::OurFogWanted()); }
        // 5. the ceiling: from inside, on its biomes
        if (w_third) { CHECK(!fx::CeilWanted()); CHECK(!fx::GameCeilWanted()); }
        if (!g_ceilBiome.load()) CHECK(!fx::CeilWanted());
        if (!g_gameCeilBiome.load()) CHECK(!fx::GameCeilWanted());
        if (!g_set.ceilOn.load()) { CHECK(!fx::CeilWanted()); CHECK(!fx::GameCeilWanted()); }
        if (w_menuFlat) CHECK(!fx::GameCeilWanted());
        // 6. caves and the river
        if (g_underground.load() && !g_set.fogCaves.load() && !g_arcaneSky.load()) CHECK(!fx::OurFogWanted());
        if (g_noFogArea.load()) { CHECK(!fx::OurFogWanted()); CHECK(!fx::FogWanted()); }
        // the Arcane Sanctuary: its fog ours whatever the game's choice, with [sky] arcane_fog
        if (g_arcaneSky.load() && !g_set.arcaneFog.load()) CHECK(!fx::OurFogWanted());
    }
    // 4. the camera off, outside a game area
    g_set.fogOn.store(true); g_set.ceilOn.store(true); g_ceilBiome.store(true); g_gameCeilBiome.store(true);
    w_overhead = w_third = w_menuFlat = w_effectsPaused = false;
    std::snprintf(g_case, sizeof g_case, "off / no world");
    g_enabled.store(false);
    CHECK(!fx::OurFogWanted()); CHECK(!fx::FogWanted()); CHECK(!fx::CeilWanted()); CHECK(!fx::GameCeilWanted());
    g_enabled.store(true); g_inWorld.store(false);
    CHECK(!fx::OurFogWanted()); CHECK(!fx::FogWanted()); CHECK(!fx::CeilWanted()); CHECK(!fx::GameCeilWanted());
    g_inWorld.store(true);
    CHECK(fx::CeilWanted()); CHECK(fx::GameCeilWanted());

    std::printf("sky_choice_test: %d cases, %d checks, %d failed\n", cases, g_checks, g_fail);
    return g_fail ? 1 : 0;
}
