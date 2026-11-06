#pragma once
// ally_portrait_files.h - [hud] ally_portraits: the game's own portraits of the party's players, the mercenary and the summons (data\hd\global\ui\
// HIREABLES\*.sprite, 120 x 120, the pictures its HireablesPanel draws; which one for whom: vr/ally_portraits.h, after the game's pettype.txt).
// D2R_VR_Settings copies them out of the player's own game (tools/settings/menu_assets.cpp PreparePortraits, CascLib, read only) into the game look's
// folder beside the plugin (shared/game_ui_files.h kFolder); the plugin only reads the copies (vr/ally_portraits_paint.cpp), each on its own - a
// portrait that is not there is drawn as initials. Blizzard's files on the player's own machine: never in the repository, an installer or a release.

namespace allyportraitfiles {

// In vr/ally_portraits.h Icon's order from Amazon on (Icon::None has no file). The archive's name is data\hd\global\ui\HIREABLES\<leaf>.sprite.
constexpr const char* kLeaves[] = {
    "amazonicon", "sorceressicon", "necromancericon", "paladinicon", "barbarianicon", "druidicon", "assassinicon", "warlockicon",
    "rogueicon", "act2hireableicon", "act3hireableicon", "barbhirable_icon", "valkarieicon",
    "earthgolumicon", "bloodgolumicon", "metalgolumicon", "firegolumicon", "skeletonicon", "skeletonmageicon", "revivedicon",
    "raven", "wolf", "bear", "oaksage", "heartofwolverine", "spiritofbarbs", "plaguepoppy", "cycleoflife", "vines", "shadowassassin",
    "binddemon", "tainted", "defiler", "khazra",
};
constexpr int kCount = (int)(sizeof kLeaves / sizeof kLeaves[0]);

// The copy's name in the folder: "portrait_<leaf>.sprite" (apart from the game look's own files).
constexpr const char* kPrefix = "portrait_";

}  // namespace allyportraitfiles
