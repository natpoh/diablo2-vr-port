// drawn_hand.h - [hands] left_slot_drawn_left: the attach bone the game DRAWS a hand slot's weapon on, against the slot it sits in. Pure: no game calls, no globals
// (cleanroom/tests/drawn_hand_test.cpp feeds it).
//
// The game draws what the hands hold as Diablo II's composite does: a one-hand weapon is the RIGHT hand's part whatever slot it sits in - unless the right slot holds a
// weapon too (a weapon in each hand: the left slot's is then the left hand's part); a shield is the shield's part, on the left arm, whatever slot; a bow or crossbow hangs on
// its own wrist. So a sword ALONE in the left slot hangs on R_weapon_attach, in the right hand, while everything of ours - the weight model, Weapon Adjust, the touch's blade,
// the throw - poses the slot's own bone, L_weapon_attach, in the left hand (Serg on 0.206.5: «когда меч в левом слоте, игра рисует его в правой, а хитбокс в левой»).
//
// Fix: the bone the game draws on takes the slot bone's pose - its own handedness (the left attach bone is a mirror: a mirror-less matrix draws the item inside out) and the
// size the game draws it at (a bone the game shrank to a point - the javelin thrown, the item hidden - stays shrunk) kept. The weapon is then drawn where it is held and where
// it hits, in whichever hand holds it. A shield in the right slot, drawn by the game on the left arm, goes to the right forearm while the weapon is not in the right hand alone
// (weapon and shield on opposite arms, as vr/shield_arm.h has it for a left-slot shield). Only the drawing moves: which slot attacks stays the game's.
//
// Whether the game really hangs it on the right bone is read off its own pose each time (Shown): the right attach bone shown at its size beside its wrist. Shrunk (the
// right wrist holds nothing in the game's eyes), it is drawn on the left one already and nothing is carried.
#pragma once

#include <cmath>
#include <cstdint>

#include "d2r_vr_state.h"

namespace drawnhand {

constexpr int kRight = 0, kLeft = 1;

struct Slots {
    uint32_t kind[2] = {0, 0};   // D2RVR_TYPE_* of each slot's weapon ([0] right, [1] left; 0: nothing, a shield, a quiver) - gamestate::HandKind
    bool shield[2] = {false, false};
    uint32_t two = 0;            // gamestate::TwoHanded (D2RVR_TWO_*)
};

enum class Why : int {
    Carry,          // a weapon alone in the left slot: the game draws it on the right bone - carried to the slot's pose
    NoLeftWeapon,   // the left slot holds no weapon (empty, a shield, a quiver): the weapon (if any) is the right slot's and hangs on its own bone
    Dual,           // a weapon in each slot: each on its own bone
    Ranged,         // a bow or crossbow: on its own wrist
    BothHands,      // a two-handed weapon in both hands: [hands] both_attach lays it between the two bones
};

struct Plan {
    bool carry = false;           // the game's drawn bone (`drawn`) takes the slot bone's (`slot`) pose
    int slot = kLeft;             // the bone everything of ours poses: the slot's
    int drawn = kLeft;            // the bone the game hangs the weapon on
    bool shieldToRight = false;   // a shield in the right slot (the game draws it on the left arm): strapped to the right forearm instead
    Why why = Why::NoLeftWeapon;
};

inline const char* WhyText(Why w) {
    switch (w) {
    case Why::Carry: return "a weapon alone in the left slot - the game draws it on the right attach bone, carried to the left slot's pose";
    case Why::NoLeftWeapon: return "no weapon in the left slot";
    case Why::Dual: return "a weapon in each slot - each on its own attach bone";
    case Why::Ranged: return "a bow or crossbow - on its own wrist";
    case Why::BothHands: return "a two-handed weapon in both hands - laid between the two attach bones";
    }
    return "?";
}

inline bool IsRanged(uint32_t kind) { return kind == D2RVR_TYPE_BOW || kind == D2RVR_TYPE_CROSSBOW; }

// The plan from the two slots alone (the game's composite rule); Shown confirms it on the pose.
inline Plan Decide(const Slots& s) {
    Plan p;
    if (s.kind[kLeft] == 0) { p.why = Why::NoLeftWeapon; p.slot = p.drawn = kRight; return p; }
    if (s.kind[kRight] != 0) { p.why = Why::Dual; return p; }
    if (IsRanged(s.kind[kLeft])) { p.why = Why::Ranged; return p; }
    if (s.two & D2RVR_TWO_HANDS_ON) { p.why = Why::BothHands; return p; }
    p.carry = true;
    p.drawn = kRight;
    p.shieldToRight = s.shield[kRight];
    p.why = Why::Carry;
    return p;
}

// The game's attach bone against its own wrist in the game's pose: its size (the length of its first row). The game shrinks the bone of a wrist that holds nothing to a point.
constexpr float kShownAt = 0.7f, kHiddenBelow = 0.3f;
inline bool Shown(float sizeAgainstWrist) { return std::isfinite(sizeAgainstWrist) && sizeAgainstWrist > kShownAt; }
inline bool Hidden(float sizeAgainstWrist) { return !std::isfinite(sizeAgainstWrist) || sizeAgainstWrist < kHiddenBelow; }

// The arm a right-slot shield is strapped to while the weapon is carried: the right one unless the right hand alone holds the weapon (passed over to it), then the game's own
// (the left). holdsRight/holdsLeft: the hands on the weapon now (the weight model's; without it the slot's hand holds it).
inline int ShieldArm(bool holdsRight, bool holdsLeft) { return holdsRight && !holdsLeft ? kLeft : kRight; }

inline double Det3(const float m[16]) {
    return (double)m[0] * ((double)m[5] * m[10] - (double)m[6] * m[9]) - (double)m[1] * ((double)m[4] * m[10] - (double)m[6] * m[8]) +
           (double)m[2] * ((double)m[4] * m[9] - (double)m[5] * m[8]);
}
inline float RowLen(const float m[16], int r) { return std::sqrt(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]); }

// The drawn bone's matrix (row vectors, axes in rows 0..2, the place in 12..14 - skeletons.cpp's) from the slot bone's pose `slotPose`: the same place and axes' directions, each
// row the length the drawn bone has as placed now (`drawnNow`: the size the game draws the item at), row 0 turned over when the two bones' handedness differs (`drawnGame`: the
// game's own pose of the drawn bone says its handedness). False (out untouched) when the slot's pose has no axes to give (a point) or anything is not finite.
inline bool Carried(const float slotPose[16], const float drawnNow[16], const float drawnGame[16], float out[16]) {
    float r[16];
    for (int i = 0; i < 16; ++i) r[i] = slotPose[i];
    for (int row = 0; row < 3; ++row) {
        const float have = RowLen(slotPose, row), want = RowLen(drawnNow, row);
        if (!(have > 1e-6f) || !std::isfinite(have) || !std::isfinite(want)) return false;
        for (int c = 0; c < 3; ++c) r[row * 4 + c] = slotPose[row * 4 + c] / have * want;
    }
    const double dg = Det3(drawnGame), dr = Det3(r);
    if (dg != 0.0 && dr != 0.0 && ((dg < 0.0) != (dr < 0.0)))
        for (int c = 0; c < 3; ++c) r[c] = -r[c];
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(r[i])) return false;
    for (int i = 0; i < 16; ++i) out[i] = r[i];
    return true;
}

}  // namespace drawnhand
