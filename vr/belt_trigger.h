// belt_trigger.h - [belt] hold_takes_trigger: the controller's triggers while a belt bottle is in a hand. Pure: no game, no windows.h
// (cleanroom/tests/belt_trigger_test.cpp feeds it, vr/belt_trigger_wire.cpp acts on it).
//
// The bug (Serg's test of test-54, 2026-10-17: "trigger + grip on the belt, the drink does not happen"; the grip alone drank): a potion drunk from the hand is the game's own
// belt key command (0x17..0x1A, gamecmd::Tap). The game does not drink it plainly - it asks for a "shift" first (read from the game image, RVAs of the loader layout):
//   0x11B230 (Belt1 down)  ->  0x228D10 (drink from the belt, column)  ->  0x160AC0 (is shift held?)  ->  the drink call, its third argument the answer (0x228D7E..0x228D8C).
//   0x160AC0: keyboard mode - the Shift key (0x120A100(0x10)); controller mode (0x77E10) - 0x14CF40, which asks the game's held-input set for the code 0x400.
//   The pad poll (0x1210740..0x1210791) makes that code out of the LEFT TRIGGER: bLeftTrigger past 30 is the press of 0x400 (the right trigger is 0x800, 0x1210829).
// So with the left trigger in the pad the game drinks "with shift" - the keyboard's Shift + belt key, which in Diablo II hands the potion to the hireling instead of drinking it. The
// layout passes the left trigger to the pad as LT (the skill rows' modifier), so a left hand that took the bottle with trigger + grip - or merely squeezed the trigger with its fist
// round the bottle - had LT in the pad at the moment of the drink, and nothing was drunk. (The right trigger is the pad's A, the game's attack: a right hand holding a bottle with its
// trigger squeezed attacked.)
//
// The fix: while a bottle is in a hand (taken, or being drunk) the left trigger is taken from the layout whichever hand holds it - it is the game's shift, and a drink must be a
// plain one - and a right hand's own trigger too (no attack out of a drink). Taken, a trigger stays masked afterwards until the player lets it go (the layout's release gate), so
// no LT or attack appears behind the drink. F4 only; [belt] hold_takes_trigger (1 by default; 0 = the triggers pass as before).
#pragma once

namespace belttrigger {

struct In {
    bool enabled = true;     // [belt] hold_takes_trigger
    bool holds[2] = {};      // the belt's hands (0 right, 1 left): a bottle is in that hand now - taken by the grip, or being drunk
};

struct Out {                 // direct_input's hands: 0 left, 1 right
    bool left = false;       // take the left trigger from the layout
    bool right = false;      // take the right trigger from the layout
};

inline Out Decide(const In& in) {
    Out o;
    if (!in.enabled) return o;
    const bool any = in.holds[0] || in.holds[1];
    o.left = any;            // the left trigger is the game's shift, whichever hand drinks
    o.right = in.holds[0];   // the right trigger is the pad's A: the right hand's own bottle is not an attack
    return o;
}

}  // namespace belttrigger
