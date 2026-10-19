// stowed_block.h - [hands] shield_stow_no_block: a shield put away behind the left shoulder ([hands] shoulder_draw_shield_stow) does not block.
//
// The game still decides every block: its chance, its roll, the block animation, Faster Block Rate, the shield's wear. With the tick on and the shield put away, the
// hero's block check is told the hero cannot block - the same path the game takes for a hero with no shield - and goes on to its own dodge/avoid checks. Nothing
// else changes: not the monsters' to-hit roll, not the damage, not the Defense of the equipped shield, not the chance the character screen shows.
// Refuse is pure (cleanroom/tests/stowed_block_test.cpp); the hook is vr/stowed_block.cpp.
#pragma once

namespace stowedblock {

// Whether this block check is refused: the tick is on, the shield is put away, the defender is the hero (a player unit; offline it is the only one), and the game
// was going to roll a block at all (`block` != 0 - with 0 it does not roll one anyway).
inline bool Refuse(bool on, bool stowed, bool defenderIsPlayer, int block) { return on && stowed && defenderIsPlayer && block != 0; }

// vrcam: the tick, read with the rest of the ini (live).
void Configure(bool on);
// vrcam's tick, twice a second: puts the hook in once the tick asks for it and the game's code is there; quiet until then, says once if it never will be.
void Install();
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

}  // namespace stowedblock
