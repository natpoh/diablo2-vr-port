// weight_drive.h - which hand swings the weapon when it has weight ([weight], vr/weight.cpp), for hit by touch. Pure.
//
// Without [weight] the weapon is the slot hand's: its controller drives the stroke. With it, a hand takes the weapon where its grip
// closes on the shaft and the weapon goes from hand to hand; it is drawn where the hands that hold it have it, so the stroke is that
// hand's motion. Let go (both grips open: it falls, tips over, lies, or flies back into the game's hand) it is not swung by anyone and
// strikes nothing - the capsule of a weapon that lies on the floor must not touch a monster.
#pragma once

namespace meleetouch {

struct Drive {
    bool strikes = true;   // the weapon can strike now
    int hand = 0;          // the controller whose motion is the stroke (0 right, 1 left)
};

// Does the weight model hold this weapon: a weapon alone in EITHER hand's slot - not a weapon in each hand (then each blade pushes the doll itself: vr/blade_push.h).
// leftWeapon: a weapon in the game's left slot (D2RVR_LEFT_WEAPON); rightHeld: anything in its right slot (HandsHeld bit 0). The headset 2026-10-23: a weapon
// in the left hand alone was left out with the second weapon («оружие в левой руке - кукла прозрачная»).
inline bool HeldAlone(bool leftWeapon, bool rightHeld) { return !leftWeapon || !rightHeld; }

// What the weight model holds for the hands as the game has them (vrcam PushArms): `wanted` - [weight] on, F4, the body on the controllers; `kind` - the
// heft::hold::Kind of the slot the game's weapon kind comes from, -1 for none (nothing, a bow or crossbow - they have their own draw -, claws, orbs);
// `alone` - HeldAlone; `otherKind` - the other slot's kind when it is a weapon, -1 for none (empty, a shield, a QUIVER of arrows or bolts). A weapon in each
// hand: the first is the model's (on), the other a body of its own beside it (second). A bow with its quiver (the game's "hands right 'sbw ' left 'aqv '"):
// nothing - the bow hand and the string hand stay the draw's.
struct Holds {
    bool on = false;      // the weight model holds the first weapon
    bool second = false;  // ... and the other hand's weapon as a body of its own
    int kind = -1, secondKind = -1;
};
inline Holds WeightHolds(bool wanted, int kind, bool alone, int otherKind) {
    Holds h;
    const int second = alone ? -1 : otherKind;
    h.on = wanted && kind >= 0 && (alone || second >= 0);
    h.second = h.on && !alone && second >= 0;
    h.kind = h.on ? kind : -1;
    h.secondKind = h.second ? second : -1;
    return h;
}

// placed: weight::Placed() - the weight model holds the weapon in hand (in a hand or not). carrying: weight::CarryingHand() -
// the hand that carries it, -1 none (let go). slotHand: the hand slot the game has the weapon in.
inline Drive DrivingHand(bool placed, int carrying, int slotHand) {
    Drive d;
    d.hand = slotHand & 1;
    if (!placed) return d;                       // [weight] off, or no weapon held that way: as before
    if (carrying == 0 || carrying == 1) {
        d.hand = carrying;
        return d;
    }
    d.strikes = false;                           // let go: falling, lying, flying back
    return d;
}

}  // namespace meleetouch
