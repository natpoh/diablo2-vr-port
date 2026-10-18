// shoulder_draw.h - [hands] shoulder_draw: the zone behind each shoulder is a holster for the weapon, and a grip pressed in it draws the weapon into the hand that reached.
// Pure: no game calls, no globals (cleanroom/tests/shoulder_draw_test.cpp feeds it).
//
// The two zones are the weapon swap's volume (vr/shoulder_swap.h Near: |x| 0.10 - 0.48 m, y -0.38 .. +0.16 m, z +0.025 .. +0.38 m from the head, in the frame of the head's turn) cut by the
// hero's middle: x > 0 is the right shoulder's, x < 0 the left's. With shoulder_draw the zone no longer switches the weapon set (the game's own swap stays on its key or binding).
//
//   - EITHER zone draws the weapon, with EITHER hand: the left hand at the right shoulder or the right hand at the left one is a cross draw. The item goes into the hand that reached - the
//     game's slots do not change, only where it is drawn. (The shield has no zone: it follows the sword to the opposite forearm, vr/shield_arm.h.)
//   - A weapon that is away - it was let go and lies where it fell, or still falls - is drawn. One in a hand is not: the press is the zone's all the same, never anything else's (no potion, no
//     swap, no call-back).
//   - A hand that holds a belt potion draws nothing.
//   - A weapon let go is NOT called back by any grip from afar: it lies where it fell until it is drawn (or taken up off the floor by a hand that reaches it). Drawing it takes it off the floor in
//     the same instant: it flies from the shoulder into the hand, and the floor holds no copy.
//
// After bearstonem/d2r-pcvr's idea of the shoulder as a place the hand reaches (MIT, steamvr/shoulder_swap.h); the holster is ours.
#pragma once

#include "shoulder_swap.h"

namespace shoulderdraw {

enum class Side : signed char { None = -1, Right = 0, Left = 1 };

// The shoulder a hand (x right, y up, z back, metres from the head, in the head's turn frame) is behind, None outside both zones.
inline Side ZoneSide(const float hand[3]) {
    if (!shoulderswap::Near(hand)) return Side::None;
    return hand[0] > 0.0f ? Side::Right : Side::Left;
}

// Where the weapon comes out of a shoulder: the middle of that zone (metres from the head, the head's turn frame).
inline void DrawPoint(Side s, float out[3]) {
    out[0] = s == Side::Left ? -0.24f : 0.24f;
    out[1] = -0.08f;
    out[2] = 0.15f;
}

}  // namespace shoulderdraw
