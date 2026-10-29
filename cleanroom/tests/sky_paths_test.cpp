// sky_paths_test - vr/sky_paths.h: the sky and the fog drawn by one path only, and the interface told by the eyes only in stereo.
//   1. "the game" chosen: our sky drawn in no view, whether the dome is shown or not (put away, not loaded, not covering the view) - the
//      old rule drew ours wherever the dome was not shown (PC1 2026-10-29 17:18: both skies in flat);
//   2. "our effect" chosen: ours wherever the act's sky is wanted, never over a dome;
//   3. the Arcane Sanctuary: its own sky and fog ours, whatever the choice;
//   4. the fog the same way;
//   5. the interface by "the same in both eyes" only in VR first person - flat's still world below HudTop was taken for it (47.webp), and
//      VR's other views' too (57.webp, with the fallback sky);
//   6. the game's chosen but unable for good (its dome's files missing, refused, no model, not placed; a view it does not draw in; its fog
//      not found): ours instead - not while it is on its way (no black sky, never both);
//   7. our effect still being built: the game's dome stands in;
//   8. the Summits ([sky] game_skybox): our painted sky behind the game's sea of clouds whatever the choice, first and third person.
#include "sky_paths.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace skypaths;
    // the views: VR F4, VR F2 / F3 / F1 (no game dome there), flat F3 / F2 with game_drawn_flat, flat without it - as the dome's state
    // and whether the act's sky is wanted there (the views from above and the table never want it: palette -1)
    struct View { const char* name; bool actSky, domeShownIfGame; };
    const View views[] = {{"VR F4", true, true},           {"VR F4, the dome not loaded yet", true, false}, {"VR F2", true, false},
                          {"flat F3 (game_drawn_flat)", true, true}, {"flat F3, the dome put away", true, false},
                          {"flat F2 without game_drawn_flat", true, false}, {"from above", false, false}};
    int oldBoth = 0;
    for (const View& v : views) {
        // 1. the game's chosen (and able: views it does not draw in are case 6)
        const bool dome = v.domeShownIfGame;
        CHECK(!OurSky(v.actSky, true, false, dome));
        const bool oldRule = (v.actSky) && !dome;   // the rule before: ours wherever the dome was not shown
        if (oldRule) ++oldBoth;
        // 2. ours chosen: no dome then (game_sky=0 puts it away)
        CHECK(OurSky(v.actSky, false, false, false) == v.actSky);
        std::printf("  %-34s the game's chosen (and able there): ours %s (before: %s); ours chosen: %s\n", v.name, OurSky(v.actSky, true, false, dome) ? "DRAWN" : "off",
                    oldRule ? "drawn too" : "off", OurSky(v.actSky, false, false, false) ? "drawn" : "off (no sky here)");
    }
    CHECK(oldBoth == 4);   // (the four views where the old rule drew ours with the game's chosen)
    CHECK(!OurSky(true, false, false, true));   // never over a dome
    // 3. the Arcane Sanctuary
    CHECK(OurSky(false, true, true, false) && OurSky(false, false, true, false));
    CHECK(OurFog(true, true, true) && OurFog(true, false, true));
    // 4. the fog
    CHECK(!OurFog(true, true, false));   // the game's chosen: ours never, also before its pass took our numbers
    CHECK(OurFog(true, false, false));
    CHECK(!OurFog(false, false, false) && !OurFog(false, true, true));
    // 5. the interface by the eyes: VR first person only - flat (one view), and VR's other views (the world far below, F1's window one
    // picture for both eyes: 57.webp, the row back with the fallback sky there) are never told by it
    {
        struct V { const char* name; bool afr, vrF4; bool want; };
        const V views[] = {{"VR F4", true, true, true},
                           {"flat (afr on, one view)", true, false, false},
                           {"VR from above / F1 window / F3 floor / F2 (platform 1, not F4)", true, false, false},
                           {"VR F4 without afr", false, true, false}};
        for (const V& v : views) {
            std::printf("  the interface by the eyes, %-62s %s\n", v.name, HudByEyes(v.afr, v.vrF4) ? "on" : "off");
            CHECK(HudByEyes(v.afr, v.vrF4) == v.want);
        }
        // the fallback sky in a view the game does not draw its dome in (VR not F4, or flat): drawn, and no row - the two never together
        const char* cannot = GameSkyCannot(false, true, DomeTry{});
        CHECK(OurSky(true, GameHolds(true, cannot), false, false) && !HudByEyes(true, false));
    }
    // 6. the game's chosen but unable for good: ours instead (a fresh install before the Settings window made the dome's files - no black sky);
    // while it is on its way, nothing of ours over it
    {
        DomeTry loading;   // its definition asked, the files loading / its placings being tried: on its way
        loading.tried = true;
        loading.defLoaded = true;
        DomeTry missing;   // the unit file missing, or refused
        missing.tried = true;
        DomeTry noModel = loading;
        noModel.gaveUp = true;
        DomeTry notYet;    // not asked yet (no eye yet, just in the world)
        struct Case { const char* name; bool view, act; DomeTry d; bool ours; };
        const Case cases[] = {{"the dome loading / its placings tried", true, true, loading, false},
                              {"not asked yet (no eye)", true, true, notYet, false},
                              {"the dome's files missing (fresh install)", true, true, missing, true},
                              {"no model after its placings", true, true, noModel, true},
                              {"a view the game does not draw it in", false, true, loading, true},
                              {"no dome for the act", true, false, notYet, true}};
        for (const Case& c : cases) {
            const char* cannot = GameSkyCannot(c.view, c.act, c.d);
            const bool ours = OurSky(true, GameHolds(true, cannot), false, false);
            std::printf("  the game's sky chosen, %-42s ours %s%s%s\n", c.name, ours ? "DRAWN" : "off", cannot ? " - " : "", cannot ? cannot : "");
            CHECK(ours == c.ours);
        }
        DomeTry placeFail = loading;
        placeFail.placeFailed = true;
        CHECK(GameSkyCannot(true, true, placeFail) != nullptr);
        CHECK(!OurSky(true, GameHolds(true, GameSkyCannot(true, true, missing)), false, true));   // (never over a dome that IS shown)
        // the fog: found or being found - nothing of ours; given up, or a view the game does not draw - ours
        CHECK(!OurFog(true, GameHolds(true, GameFogCannot(true, false)), false));
        CHECK(OurFog(true, GameHolds(true, GameFogCannot(true, true)), false));
        CHECK(OurFog(true, GameHolds(true, GameFogCannot(false, false)), false));
        CHECK(!GameHolds(false, nullptr));   // ours chosen: the game's never holds
    }
    // 8. the Summits ([sky] game_skybox): the game's sea of clouds, the dome away on purpose - our painted sky behind it whatever the
    // choice, in every view our sky draws in: VR F4, flat first person, flat third person (PC1 2026-10-29 18:07: level 120 black in third
    // person - neither dome nor ours); with game_skybox=0 the Summit is as any area
    {
        DomeTry shownOk;
        shownOk.tried = true;
        shownOk.defLoaded = true;
        struct V { const char* name; bool gameView; };
        const V views[] = {{"VR F4", true}, {"flat first person (game_drawn_flat)", true}, {"flat third person (game_drawn_flat)", true},
                           {"flat third person without game_drawn_flat", false}};
        for (const V& v : views) {
            const char* cannot = GameSkyCannot(v.gameView, true, shownOk, true);
            const bool ours = OurSky(true, GameHolds(true, cannot), false, false);
            std::printf("  the Arreat Summit, the game's sky chosen, %-44s ours %s\n", v.name, ours ? "DRAWN (behind the cloud sea)" : "off");
            CHECK(ours && cannot != nullptr);
            CHECK(OurSky(true, GameHolds(false, nullptr), false, false));   // ours chosen: ours there too
        }
        CHECK(GameSkyCannot(true, true, shownOk, false) == nullptr);   // game_skybox=0 (or not a Summit): the dome as anywhere
        // the fog at the Summit: the choice as anywhere (586fa025 - our post sky and fog as before the dome; the game's own fog with our numbers
        // when it is chosen) - nothing of the Summit in GameFogCannot
        CHECK(!OurFog(true, GameHolds(true, GameFogCannot(true, false)), false));
    }
    // 7. our effect still being built (a new build's first start): the game's dome stands in for our sky; once ours draws, the dome goes
    {
        CHECK(GameDomeWanted(false, true));    // ours chosen, ours not drawing yet: the dome meanwhile
        CHECK(!GameDomeWanted(false, false));  // ours drawing: no dome
        CHECK(GameDomeWanted(true, false) && GameDomeWanted(true, true));
        CHECK(!OurSky(true, false, false, true));   // the dome still shown in the first frame ours draws again: ours waits a frame, never both
    }
    if (g_fail) { std::printf("sky_paths_test: %d FAILED\n", g_fail); return 1; }
    std::printf("sky_paths_test: one path draws the sky and the fog in every view, the Arcane Sanctuary ours, the interface by the eyes only in "
                "VR stereo: all passed\n");
    return 0;
}
