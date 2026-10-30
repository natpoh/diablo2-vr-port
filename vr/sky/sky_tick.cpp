// vr/sky/sky_tick.cpp - the area judged on the update thread: in world, underground, the act, the ceiling biome, the sky and its reason (SkyTick), and the state it sets (cut from vr/vrcam.cpp, docs/plan_modules.md - the sky group).
// It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// (vr/sky/sky_config.cpp; declared here only: its defaults are its definition's)
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr, float* own = nullptr);


// Sky on or off, judged on the timer: the vrcam camera on, [sky] enabled, an
// outdoor biome, no menu and no side panel. Each change of mind goes to the log
// with its reason - the biome names of caves and dungeons are learnt from it.
std::atomic<int> g_skyPalette{-1};   // kPalettes index while the sky shows, else -1
std::atomic<bool> g_gameSkyboxHere{false};   // [sky] game_skybox: the area has a skybox of the game's own (skydome::IsGameSkyboxArea)
// [sky] arcane_sky (after bearstonem/d2r-pcvr v0.3.18, MIT): in the Arcane Sanctuary the game's own star
// layers and cloud noise all round the void, from pictures D2R_VR_Settings makes out of the player's game files.
std::atomic<bool> g_arcaneSky{false};     // it shows now
std::atomic<bool> g_arcaneReady{false};   // reshade-shaders\Textures\D2R_Arcane\stars.png and clouds.png are there
std::atomic<int> g_fogAct{0};        // the area's act for the fog colour, caves too; 0 = unknown
std::atomic<bool> g_underground{false};   // in the world, off the outdoor list: the fog takes [fog] color_caves
std::atomic<bool> g_noFogArea{false};     // [fog] no_fog_river and the area is the River of Flame: no added fog (unleashed::fog::NoFogArea)
std::atomic<bool> g_inWorld{false};  // a game area is loaded: not the main menu (its own "frontend" biome), not before any
std::atomic<bool> g_frontendBiome{false};   // the game's menus' own scene: the main menu, character creation ([render] menu_wide)
std::atomic<bool> g_ceilBiome{false};   // the area's biome is on [ceiling] biomes
std::atomic<bool> g_gameCeilBiome{false};   // ... on [ceiling] game_ceiling_biomes: a natural cave, the game's rock there with game_ceiling

namespace fx { bool FogWanted(); }   // (below, with the effect)
void SkyTick() {
    env::Find();
    solidwalls::Tick();
    roofhook::Tick();
    engitem::weather::Tick();   // [sky] weather_first_person: the log lines
    prophook::Tick();
    worldobj::Gather();
    gamefog::Tick();
    depthsrc::Tick();   // [render] depth_fix_mode 2: what became of the frames, every 20 s
    static uint32_t seenGen = ~0u;
    static std::string biome;
    static int palette = -1;
    if (const uint32_t gen = g_biomeGen.load(); gen != seenGen) {
        seenGen = gen;
        AcquireSRWLockShared(&g_biomeLock); biome = g_biome; ReleaseSRWLockShared(&g_biomeLock);
        palette = SkyPaletteFor(biome, g_set.skyAlways.load());
        g_gameSkyboxHere.store(skydome::IsGameSkyboxArea(biome));
        // Not a world: the main menu (act2_frontend_biome) and the menu scenes of
        // their own - character creation is 'ui_characterselectscreen' (2026-10-08:
        // taken for a world it got the first person's stereo, HUD shift and hidden heads).
        const bool world = !biome.empty() && biome.find("frontend") == std::string::npos &&
                           biome.rfind("ui_", 0) != 0;
        g_inWorld.store(world);
        g_frontendBiome.store(!biome.empty() && !world);   // (a load into a game sets no name first: not the menus' any more)
        if (!world) g_roomHeightsClear.store(true);   // [render] floors_stay: the next game's rooms are other rooms
        g_fogAct.store(world ? ActOfBiome(biome) : 0);
        g_underground.store(world && !IsOutdoorBiome(biome));
        g_noFogArea.store(world && unleashed::fog::NoFogArea(biome, g_set.fogNoRiver.load()));
        g_ceilBiome.store(world && IsCeilingBiome(biome));
        // [ceiling] game_ceiling: the area's kind of the game's ceiling (shared/game_ceiling.h) when on game_ceiling_biomes, else none
        const bool onGameList = world && IsGameCeilingBiome(biome);
        const int kind = onGameList ? gameceiling::KindOf(biome) : -1;
        engitem::g_gameCeilKind.store(kind);
        g_gameCeilBiome.store(kind >= 0);
        {   // [ceiling] height_from_walls, game_ceiling_beams: the rooms measured from the game's models (D2R_VR_Settings writes them)
            std::wstring file;
            ceilmeasure::Measures ms;
            const bool have = world && ceilmeasure::Parse(engitem::ModDataText(ceilmeasure::kFile, &file), &ms);
            g_wallsTopHere.store(have ? ceilmeasure::WallsTopOf(ms, biome) : 0.0f);
            AcquireSRWLockExclusive(&g_biomeLock); g_beam = ms.beam; ReleaseSRWLockExclusive(&g_biomeLock);
            g_beamsHere.store(have && ms.haveBeam && gameceiling::IsBeamBiome(biome));
            std::vector<ceilmeasure::Dome> domes = have ? ceilmeasure::DomesOf(ms, biome, true, true) : std::vector<ceilmeasure::Dome>();
            g_chapelHere.store(!domes.empty());
            AcquireSRWLockExclusive(&g_biomeLock); g_chapelDomes = std::move(domes); ReleaseSRWLockExclusive(&g_biomeLock);
            g_chapelGen.fetch_add(1);
        }
        static std::string told = "?";   // no biome name has a question mark
        if (biome != told) {
            told = biome;
            // the area's ceiling, which and why: the game's of its kind (game_ceiling), our vault with the area's own settings ([ceiling]
            // biomes), or none
            std::string which;
            if (!world) which = "none - not a world";
            else if (kind >= 0 && g_set.gameCeiling.load())
                which = std::string("the game's '") + gameceiling::kKinds[kind].name + "' (" + gameceiling::kKinds[kind].maps + ") in VR F4" +
                        (g_ceilBiome.load() ? "; our vault with its own settings elsewhere (flat F3)" : "");
            else if (g_ceilBiome.load())
                which = std::string("our vault with this area's own settings ([ceiling] biomes)") +
                        (!g_set.gameCeiling.load() ? "" : onGameList ? " - no fitting game model for it" : " - not on [ceiling] game_ceiling_biomes");
            else which = kind >= 0 ? "none - [ceiling] game_ceiling=0 and not on [ceiling] biomes" : "none - on neither [ceiling] biomes nor a kind of the game's";
            if (world && g_set.ceilFromWalls.load()) {   // [ceiling] height_from_walls: where its height comes from
                char h[160];
                const float walls = g_wallsTopHere.load();
                if (walls > 0.0f) std::snprintf(h, sizeof h, "; with no height of its own set, on its walls' top, %.1f (the game's own pieces, [ceiling] height_from_walls)", walls);
                else std::snprintf(h, sizeof h, "; its walls not measured (D2R_VR_Settings, or a room built without them) - its own height");
                which += h;
            }
            if (world && g_set.gameCeilingBeams.load() && gameceiling::IsBeamBiome(biome))
                which += g_beamsHere.load() ? "; the barracks' own beams under it ([ceiling] game_ceiling_beams)"
                                            : "; no beams - their frame not measured yet (D2R_VR_Settings makes it from the game's files)";
            if (world && g_set.gameCeilingDome.load() && biome == gameceiling::kVaultBiome)
                which += g_chapelHere.load() ? "; the chapel's dome in it - the canopy looked for among the game's pieces ([ceiling] game_ceiling_dome)"
                                           : "; no dome - not measured yet (D2R_VR_Settings makes it from the game's files)";
            else if (world && g_set.gameCeilingDomesTall.load() && g_chapelHere.load())
                which += "; the Worldstone's dome - its preset looked for among the game's pieces ([ceiling] game_ceiling_worldstone_dome; level 132)";
            LogF("vrcam: biome '%s' - %s; ceiling: %s%s", biome.empty() ? "(none yet)" : biome.c_str(),
                 palette >= 0 ? kPalettes[palette].part : "no sky here (not in [sky] outdoor)", which.c_str(),
                 g_noFogArea.load() ? "; no added fog ([fog] no_fog_river)" : "");
        }
    }
    cavelight::Tick(biome, g_underground.load());
    {   // the cave ceiling: each change of mind to the log, like the sky's
        const char* cw = nullptr;
        if (!g_set.ceilOn.load()) cw = "[ceiling] enabled=0";
        else if (!g_ceilBiome.load()) cw = "not on [ceiling] biomes";
        else if (!g_enabled.load() || ThirdPerson()) cw = "not first person";
        else if (EffectsPaused()) cw = "a menu is open";
        static std::string toldCeil = "?";
        if (std::string(cw ? cw : "") != toldCeil) {
            toldCeil = cw ? cw : "";
            if (cw) LogF("vrcam: cave ceiling off - %s", cw);
            else {
                float height, bright, light;
                CeilingNow(&height, &bright, &light);
                LogF("vrcam: cave ceiling on - %.0f units over the hero's floor, brightness %.2f, light reach %.0f%s", height, bright, light,
                     g_set.gameCeiling.load() && g_gameCeilBiome.load() ? " (a natural cave, [ceiling] game_ceiling: in VR F4 the game's rock instead - its own lines say when)" : "");
            }
        }
    }
    const char* why = nullptr;
    if (!g_set.skyOn.load()) why = "[sky] enabled=0";
    else if (!g_enabled.load()) why = "the VR camera is off (F12)";
    else if (TableView()) why = "the table view (the room round the game)";
    else if (TopPersp()) why = "the view from above";
    else if (palette < 0) why = "not outdoors";
    else if (EffectsPaused()) why = "a menu is open";   // (a menu as a panel, menu_3d: the world as without one)
    g_skyPalette.store(why ? -1 : palette);
    {   // [sky] arcane_sky: the pictures looked for every 2 s (D2R_VR_Settings may make them while the game runs)
        static ULONGLONG lookedAt = 0;
        static bool toldReady = false;
        const ULONGLONG t = GetTickCount64();
        if (!lookedAt || t - lookedAt >= 2000) {
            lookedAt = t;
            const bool ready = SkyFileExists(L"D2R_Arcane\\stars.png") && SkyFileExists(L"D2R_Arcane\\clouds.png");
            if (g_arcaneReady.exchange(ready) != ready || !toldReady) {
                toldReady = true;
                g_skyCfgGen.fetch_add(1);   // D2R_ARCANE_SKY follows (ApplySkyPictures)
                LogF("vrcam: Arcane sky pictures %s", ready ? "found (reshade-shaders\\Textures\\D2R_Arcane)"
                                                          : "not made yet - D2R_VR_Settings makes them from the game's files");
            }
        }
    }
    // The Arcane Sanctuary's own sky: a part of the level, not an added outdoor sky - [sky] enabled and the
    // outdoor list do not count, the views from above and menus do, as for the sky.
    const char* arcaneWhy = nullptr;
    if (biome != "act2_arcane") arcaneWhy = "not in the Arcane Sanctuary";
    else if (!g_set.arcaneSky.load()) arcaneWhy = "[sky] arcane_sky=0";
    else if (!g_arcaneReady.load()) arcaneWhy = "its pictures are not made yet (D2R_VR_Settings makes them from the game's files)";
    else if (!g_enabled.load()) arcaneWhy = "the VR camera is off (F12)";
    else if (TableView()) arcaneWhy = "the table view (the room round the game)";
    else if (TopPersp()) arcaneWhy = "the view from above";
    else if (EffectsPaused()) arcaneWhy = "a menu is open";
    g_arcaneSky.store(arcaneWhy == nullptr);
    // [fog] spare_effects: off as soon as the fog or F4 is (the effects' pass, which also sets it, may not run then);
    // [render] fog_in_frame the same, and the spare is off while the fog is drawn in the frame
    if (fogframe::Armed() && !(fx::FogWanted() && viewgate::F4())) fogframe::Configure(fogframe::Params{});
    fogspare::Configure(fogspare::Wanted() && fx::FogWanted() && viewgate::F4() && !fogframe::Armed());
    {
        static std::string toldArcane = "?";
        if (std::string(arcaneWhy ? arcaneWhy : "") != toldArcane) {
            const bool wasThere = toldArcane != "?" && toldArcane != "not in the Arcane Sanctuary";
            toldArcane = arcaneWhy ? arcaneWhy : "";
            if (!arcaneWhy) LogF("vrcam: Arcane sky on - the game's own star layers and clouds all round ([sky] arcane_sky); %s",
                                 g_set.arcaneFog.load() ? "the fog fades into it ([sky] arcane_fog=1)" : "no fog under it ([sky] arcane_fog=0)");
            else if (biome == "act2_arcane" || wasThere) LogF("vrcam: Arcane sky off - %s", arcaneWhy);
        }
    }
    static std::string toldWhy = "?";
    if (std::string(why ? why : "") != toldWhy) {
        toldWhy = why ? why : "";
        if (why) LogF("vrcam: sky off - %s", why);
        else LogF("vrcam: sky on - %s palette", kPalettes[palette].part);
    }
}

}  // namespace vrc
