#pragma once
// game_ui.h - the skill panel's game look ([openxr] skill_panel_game_look): the game's own interface art and fonts, read from the
// cache D2R_VR_Settings makes out of the player's own game (shared/game_ui_files.h, tools/settings/menu_assets.cpp). The painter
// (vr/tiles_paint.cpp) asks for a sprite by what it is; nothing here is drawn, nothing of the game's art is in the plugin.
//
// Read once, on the painter's thread (the panel's worker, never the headset's frame), the first time the look is wanted: all the
// sprites or none - a cache with a piece missing is no game look, the window keeps ours and the log says why. The fonts are taken
// into this process only (AddFontResourceEx, FR_PRIVATE); a line the font has no letters for (Cyrillic, Chinese) is set in ours.

#include <string>

#include "game_ui_sprite.h"

namespace gameui {

// In shared/game_ui_files.h's order.
enum class Sprite { Slab, Border, Tabs, SmallTabs, Button, Checkbox, ArrowLeft, ArrowRight, Track, Knob, Field, Rule, Option, OptionChosen, Count };

// Where the cache's one line goes (the plugin's log; none in a test).
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
// [openxr] skill_panel_game_look, live (xr.cpp sets it from the ini).
void SetOn(bool on);
bool On();
// The look can be drawn: the key is on and the whole cache was read (reads it the first time; false while it cannot).
bool Ready();
// The key is on and every file of the cache is there (not read: a cheap look, for the window's layout).
bool Present();
const Sheet& Get(Sprite s);
// The game's fonts by family name when they were taken in, else nullptr; and whether `face` has every letter of `text`.
const wchar_t* TitleFace();   // Exocet (the game's titles and buttons)
const wchar_t* TextFace();    // Formal436 (its lines)
bool Covers(const wchar_t* face, const std::wstring& text);

// Tests: the cache's folder (default: d2r_vr_game_ui beside this DLL); forgets what was read.
void SetDir(const std::wstring& dir);

}  // namespace gameui
