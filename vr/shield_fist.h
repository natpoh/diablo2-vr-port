// shield_fist.h - [hands] shield_fist: the hand with a shield on it closes and opens with the controller's grip, as a free
// hand does ([hands] fist). The pure part: no game calls, no globals. The skeleton hook (skeletons.cpp) asks Grip() once per
// pose; a value >= 0 is the grip the left hand's fingers follow (the same Fist() a free hand uses), -1 leaves the game's own
// animation of the hand with a shield, exactly as before.
//
// What moves and what does not: only the fingers. Fist() rebuilds the finger chains below the wrist (the children of the
// wrist bone other than the weapon attach and the _END marker); the shield hangs on the weapon attach bone, which is not one
// of them, and the arm is placed before this runs - so the shield stays on the arm exactly as the game draws it, and a shield
// bash ([gameplay] shield_smite, which reads that attach bone) is the same with the hand open or closed.
//
// Which grip: the one the controller really has (direct input's frame, before any Take hid it), not the layout's value. The
// left grip has other duties in the plugin's own layout (a belt potion grabbed, a staff's second hand, the shoulder swap, the
// chords: the grip + stick click recentres, the grip + menu is the map) and a taken grip reads as let go after the layout; the
// fingers of a hand are the hand's, so they follow what the fingers would feel. Where there is no native frame (BodyWalk's
// tracking alone) the shared block's grip is the real one and is used.
//
// The weight model ([weight]) stays out of it: a shield is in the left hand's slot, and vrcam tells the weight model that the
// other hand "holds something of its own" (Input::Weight::otherBusy), so that hand takes no weapon and drops none; this module
// does not touch the weight model's inputs (Input::grip, Input::Weight::rawGrip) - it has a field of its own.
#pragma once

#include <algorithm>
#include <cmath>

namespace shieldfist {

struct Want {
    bool on = false;             // [hands] shield_fist
    bool f4 = false;             // viewgate::F4(): VR first person
    bool shield = false;         // a shield in the left hand of the active weapon set
    bool arms = false;           // the arms are on the controllers (the skeleton's mode 2)
    bool tracked = false;        // the left controller is tracked this pose
    float rawGrip = -1.0f;       // the left grip as the frame has it, 0..1; < 0 not known (no native frame)
    float layoutGrip = -1.0f;    // Input::grip[1], after the layout; < 0 not known (an older bridge)
    bool closedAlready = false;  // the free-hand rule already closed this hand in this pose: not twice
};

// The grip the shield hand's fingers follow, 0..1; -1 = the game's own animation of the hand.
inline float Grip(const Want& w) {
    if (!w.on || !w.f4 || !w.shield || !w.arms || !w.tracked || w.closedAlready) return -1.0f;
    const float g = std::isfinite(w.rawGrip) && w.rawGrip >= 0.0f ? w.rawGrip : w.layoutGrip;
    if (!std::isfinite(g) || g < 0.0f) return -1.0f;
    return std::min(g, 1.0f);
}

}  // namespace shieldfist
