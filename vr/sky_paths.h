// sky_paths.h - who draws the sky and the fog: the game (its dome, its own fog pass with our numbers) or our effect's post pass - one of
// them, never both (Serg, 2026-10-29: "never draw a sky, fog or ceiling twice by both paths"; "they must be switched"). The pure part.
//
// Settings > Sky "Sky and fog drawn by" writes [sky] game_sky and [fog] game_fog together. Our post pass drew its sky wherever the game's
// dome was not shown at that moment - put away, not loaded yet, or a view it does not cover - and its fog wherever the game's fog pass had
// not taken our numbers yet: with "the game" chosen, both skies in flat (PC1 2026-10-29 17:18, 0.188.3: "sky on (picture 5)" with
// game_sky=1, game_drawn_flat=1). Now the choice is the switch: the game's chosen, ours not drawn at all. The Arcane Sanctuary is the one
// place the game has no sky of its own and its fog stands aside ([sky] arcane_sky / arcane_fog): ours there, whatever the choice.
//
// And the interface "the same in both eyes" (D2R_DepthFog.fx HudMask: a pixel below HudTop equal in this present and the one before is
// the interface - the sky only in its dark parts) holds only where consecutive presents ARE the two eyes seeing the world apart (VR F4). In
// flat they are one view: the
// still world below HudTop (0.70 of the height) was taken for the interface - our sky stopped at a hard row, the bright bits of the mist
// left raw in the void below it (PC1 2026-10-29, 43-45.webp / 47.webp: the row at y 1340 of 1913; "without the sky all is fine").
#pragma once

namespace skypaths {

// Our post pass's sky: the act's sky wanted here and the game's not chosen - or the Arcane Sanctuary's own; never over the game's dome.
inline bool OurSky(bool actSkyWanted, bool gameSkyChosen, bool arcaneSky, bool domeShown) {
    return ((actSkyWanted && !gameSkyChosen) || arcaneSky) && !domeShown;
}
// Our post pass's fog: wanted here and the game's not chosen - or under the Arcane Sanctuary's sky (its fog is that sky behind each point).
inline bool OurFog(bool fogWanted, bool gameFogChosen, bool arcaneSky) { return fogWanted && (!gameFogChosen || arcaneSky); }
// The interface told by "the same in both eyes": only while presents alternate the eyes AND the eyes see the world apart - VR first person
// (F4). In VR's other views the world is far below the camera or one picture for both eyes (F1's window): its still pixels the same in both
// eyes, the row came back there (PC1 2026-10-29 18:20, 0.188.5, [mode] platform=1 with the high camera, 57.webp: y 1235 of 1813 = HudTop).
inline bool HudByEyes(bool alternateEyes, bool vrFirstPerson) { return alternateEyes && vrFirstPerson; }

// The game's chosen but it cannot draw here FOR GOOD: then ours instead (Serg, 2026-10-29: a player who installs and presses Play before
// the Settings window ever made the dome's files must not get a black sky, nor no fog). Never for a passing state - the dome's files
// loading, its placings still being tried, no eye yet, the game's fog still being found: nothing over it then, the two never together.
// What one act's dome came to (vrcam's engitem::Dome)
struct DomeTry {
    bool tried = false;         // its definition asked (once a run, when first wanted)
    bool defLoaded = false;     // ... and the game took it
    bool gaveUp = false;        // no model after its placings (5 over 15 s)
    bool placeFailed = false;   // PlaceInstance gave nothing
};
// Why the game's sky cannot be drawn here for good (nullptr: it can, or it is on its way). gameSkyboxHere: the Summits ([sky] game_skybox,
// skydome::IsGameSkyboxArea - the Arreat Summit, the uber one): the game's own sea of clouds there, the dome stands aside ON PURPOSE and our
// painted sky stays behind the clouds (586fa025, "the old look") - in every view our sky draws in, first or third person. With the game's
// sky chosen 7544b652 turned ours off there too: neither the dome nor ours, level 120 black (PC1 2026-10-29 18:07, 0.188.4, third person).
inline const char* GameSkyCannot(bool gameDrawnView, bool actHasDome, const DomeTry& d, bool gameSkyboxHere = false) {
    if (gameSkyboxHere) return "the game's sea of clouds here (the Arreat Summit): our painted sky behind it, no dome over it ([sky] game_skybox)";
    if (!gameDrawnView) return "the game draws its dome only in VR F4 and in the flat game with [render] game_drawn_flat";
    if (!actHasDome) return "no dome for this area's act";
    if (d.tried && !d.defLoaded) return "the dome's files are missing or the game refused them (the Settings window makes them)";
    if (d.gaveUp) return "the game took no model for the dome after its placings";
    if (d.placeFailed) return "the game placed no dome";
    return nullptr;
}
// Why the game's fog cannot draw ours here for good (nullptr: it can, or it is being found)
inline const char* GameFogCannot(bool gameDrawnView, bool gaveUp) {
    if (!gameDrawnView) return "the game's fog takes our numbers only in VR F4 and in the flat game with [render] game_drawn_flat";
    if (gaveUp) return "the game's frame fog was not found (given up)";
    return nullptr;
}
// The choice as it holds now: the game's chosen and able (or on its way)
inline bool GameHolds(bool chosen, const char* cannot) { return chosen && !cannot; }

// The game's dome laid: chosen - or our effect, whose sky is the path, not drawing yet (its build for this size still running: a minute at a
// new build's first start, PC1 2026-10-29 17:50 - a black sky meanwhile). Our sky never draws over a shown dome (OurSky), so the two are
// still never together: the dome until our effect draws, then put away.
inline bool GameDomeWanted(bool gameSkyChosen, bool ourEffectWaiting) { return gameSkyChosen || ourEffectWaiting; }

}  // namespace skypaths
