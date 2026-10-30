// menu_assets.h - D2R_VR_Settings copies the game's own interface art for the skill panel's game look ([openxr] skill_panel_game_look)
// out of the player's own game (CascLib, read only) into the folder beside the plugin (shared/game_ui_files.h). The plugin only reads it.
#pragma once

#include <string>

namespace menuassets {

struct Result {
    bool ok = false;      // the folder holds every piece (or the key is off: nothing to do)
    bool wrote = false;   // a file was written
    std::wstring text;    // for the log
};

// game: the game's folder (its archives are read from there, read only), with its trailing backslash.
// pluginDir: the folder of d2r_vr.ini and d2rl-vrcam.dll, with its trailing backslash; the art goes into <pluginDir>d2r_vr_game_ui\.
// on: [openxr] skill_panel_game_look. Off: nothing is read or written (a folder made before stays; unticked the panel does not use it).
// A piece already there as the game has it is not written again.
Result Prepare(const std::wstring& game, const std::wstring& pluginDir, bool on);

}  // namespace menuassets
