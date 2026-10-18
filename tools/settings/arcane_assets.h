// arcane_assets.h - D2R_VR_Settings makes the Arcane Sanctuary's sky pictures ([sky] arcane_sky)
// out of the player's own game, read through CascLib (read only). After bearstonem/d2r-pcvr (MIT).
#pragma once

#include <string>

namespace arcane {

enum State { kMade, kFailed, kNotHere };   // made (or up to date) / could not be made / nothing to make it into

struct Result {
    State state = kFailed;
    std::wstring text;    // for the Status row and the log
    bool wrote = false;   // a file was written (not up to date before)
};

// Reads the four textures and the seven visual files out of the game's archives, makes stars.png and clouds.png in
// <reshadeFolder>reshade-shaders\Textures\D2R_Arcane and - only when <modRoot>D2R-VR.mpq is there - the visual files without
// their flat star cards in it. Nothing is written that is already so; a visual file the player changed stays.
// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// reshadeFolder: ReShade's own folder, trailing backslash: the mod's reshade\ (<game>\mods\D2R-VR\reshade\) when D2R VR is the D2RLoader
//   mod (the way Settings' ReShadeFolder() and the plugin's ReShadeDir() resolve it), else the game's folder.
// modRoot: the mod's folder <game>\mods\D2R-VR\ with its trailing backslash; empty when D2R VR is not installed as the mod (then the game's
//   flat star cards stay: nothing the game takes for its own is changed outside the mod).
Result Prepare(const std::wstring& game, const std::wstring& reshadeFolder, const std::wstring& modRoot);

}  // namespace arcane
