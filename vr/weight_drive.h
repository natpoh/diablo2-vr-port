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
