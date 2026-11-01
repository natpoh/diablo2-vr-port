// walk_home.h - [weight] knock_down / grab_throw: when a body the knock-down library draws walking home goes back to the game's own animation. Pure
// (cleanroom/tests/walk_home_test.cpp); vr/knock_down.cpp's Tick asks it.
//
// Serg's 0.196.1 video (Rec 0104, 23:50:33 - 45 and 23:50:55 - 51:07): a fallen thrown and up again was drawn "walking home" by the library for 11 - 12 s,
// its pelvis 0.03 - 0.09 m from home the last 6 of them - standing in front of the hero with its arms out, nearly still, while the game had it fight; when
// the library let it go at last it jumped. Serg's rule: «там где он упал, после того как встал игра записывает что он должен оказаться в этой клетке и
// анимация его отпускает» - up again, the game has the unit on the cell where it lies ([weight] grab_moves_unit asks its knockback there through the walk
// too), and the moment the game has it there the drawing lets it go, wholly, from that cell. Not before (released while the game has it elsewhere, it would
// walk over); not after (held once the game has it there, it stood still while the game fought). A unit the game will not move walks home as before.
#pragma once

#include <cmath>

namespace walkhome {

constexpr int kWalking = 7;          // D2RF_WALKING (shared/d2r_vr_fall.h)
constexpr float kHomeM = 0.3f;       // the drawn pelvis this near the game's unit across the ground: the game has it where it is drawn

// offM: D2rfPose::off (the drawn pelvis from home - where the game has the unit - across the ground, m). Up again (walking home) and the game has it there.
inline bool BackNow(int phase, float offM) { return phase == kWalking && std::isfinite(offM) && offM < kHomeM; }

}  // namespace walkhome
