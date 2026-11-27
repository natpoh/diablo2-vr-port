#pragma once
// keyboard_game_look.h - [openxr] keyboard_game_look: the VR keyboard (vr/vr_keyboard_unleashed.h - its keys, board and typist stay as
// they are) painted in the game's own look instead of the stone plaques with gold rims it came with: every key one of the game's own
// option buttons (the Create Game window's difficulty button; the waiting Shift its gold-framed "chosen" one, the key under the laser
// its lit frame), on the dark stone of the game's modal windows, the labels in the game's own title font (Exocet) in its buttons' gold.
// Serg 2026-11-27: "Давай сделаем их в цветах игры, чтобы это было как бы в общем стиле игры."
//
// The art is the player's own game's, read from the cache D2R VR Settings makes beside the plugin (d2r_vr_game_ui, vr/game_ui.h) -
// nothing of the game's is in the plugin. No cache: nothing painted here, and the caller keeps the keyboard's own look.

#include <cstdint>
#include <vector>

namespace kblook {

// The keyboard's four pictures stacked as framevr::keyboard::PaintAtlas stacks them (TexelWidth x 4*TexelHeight, premultiplied RGBA:
// picture p at rows p*TexelHeight.., lit = p & 1, capitals = p >= 2). Empty: the game's art is not there, or GDI failed.
std::vector<uint8_t> PaintAtlas();

}  // namespace kblook
