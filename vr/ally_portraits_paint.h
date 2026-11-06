#pragma once
// The allies' portraits' picture ([hud] ally_portraits, vr/ally_portraits.h): the grid of tiles, each the game's own portrait of that ally (copied out of
// the player's game by D2R_VR_Settings, shared/ally_portrait_files.h; a portrait not there: the name's initials), its life bar over it (green / yellow /
// red as the game's own bar, empty while not known, a dead ally greyed), a group's counter, a player's level, and the name under it - gold for a
// player, light grey for the rest, as the game writes them. Drawn by our own code with the paint kit (vr/paint_kit.h), the text through GDI.
// Premultiplied pixels, R in each one's low byte, PicW(cols) x PicH(n, cols) (the look's), as xr.cpp's quad layers take them.

#include <cstdint>
#include <string>
#include <vector>

#include "ally_portraits.h"

namespace allyportraitspaint {

// The picture for this look: some milliseconds, never on the headset's frame thread (vr/ally_portraits_wire.cpp paints it on a worker). The
// portraits are read from their folder the first time (all that are there; each one alone).
void Render(const allyportraits::Look& look, std::vector<uint32_t>& px);

// How many of the portraits were found (after the first Render; 0 before).
int PortraitsFound();

// Tests: the portraits' folder (default: the game look's, d2r_vr_game_ui beside this DLL); forgets what was read.
void SetDir(const std::wstring& dir);

}  // namespace allyportraitspaint
