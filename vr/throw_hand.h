#pragma once
// [hands] throw_left_hand: WHICH controllers throw a javelin, a throwing knife or axe (the throw gesture, vr/throw_gesture_wire.cpp), and whose
// weapon each one throws. Pure: no game calls, no globals; unit test cleanroom/tests/throw_hand_test.cpp.
//
// The game's throw (its attack, its Throw skill) is pressed as ever - nothing of that changes here. What changes is the controller whose trigger or
// grip and swing make the throw:
//   - unticked (as ever): the right controller, when the game's weapon kind (the right hand's weapon, else the left's) is a thrown one;
//   - ticked: EACH game slot (0 right, 1 left) that holds a thrown weapon gives a controller - the one of the hand that CARRIES that weapon in VR:
//       the weight model's word for its first weapon (weight::CarryingHand) and for the second of a weapon in each hand (weight::SecondHand),
//       a javelin handed over from the right hand to the left is the left controller's though the game still has it in its right slot;
//       nobody carries it (let go, flying, lying): the hand that last did - the release that let it go is that controller's, and so is its throw;
//       the weight model does not have it: the slot's own hand.
//     So a javelin in the right hand and an axe in the left: the right controller throws, the left grip is the axe's; two javelins: both
//     controllers throw, each its own (2026-10-28, a Barbarian: neither worked). Two thrown weapons in ONE hand's controller: the right slot's.
//   - the GAME's rule: its Throw takes the weapon of the right slot whenever that slot holds a weapon (the left one only with the right empty, a shield
//     there, or a thrown weapon there too). An axe in the right slot and a javelin in the left: the game refuses the throw ("Impossible" - the user's
//     headset, 2026-10-28: 5 presses of its own Throw, 5 refused). So that javelin throws nothing here (refused[left]): its controller's grip stays
//     the weight model's - the way to throw it is the game's: the javelin in the right slot (and in VR, handed over to the left hand if wanted).
// Everything that asks "is this control the throw's?" (the weapon swap behind the shoulder, a belt bottle's grip, the quiver's trigger, the weight
// model's release, hit by touch's "the weapon is in the hand") asks it of that controller.
//
// Two ways of counting hands meet here:
//   the controllers (vr/direct_input.h Frame::hand): 0 left, 1 right;
//   the game's hands and slots (gamestate, D2RVR_Shared, the weight model): 0 right, 1 left.

#include <cstdint>

namespace throwhand {

constexpr int kXrLeft = 0, kXrRight = 1;     // the controllers
constexpr int kSideRight = 0, kSideLeft = 1; // the game's hands

inline int XrOfSide(int side) { return (side & 1) == kSideLeft ? kXrLeft : kXrRight; }
inline int SideOfXr(int xr) { return (xr & 1) == kXrLeft ? kSideLeft : kSideRight; }

struct Slot {
    bool thrown = false;     // the game's slot holds a thrown weapon (gamestate::HandKind: javelin, throwing)
    bool weapon = false;     // ... a weapon of any kind (gamestate::HandKind not 0: no shield, no quiver, not empty)
    bool placed = false;     // the weight model has this slot's weapon (its first weapon, or the second body of a weapon in each hand)
    int carrier = -1;        // ... the hand that carries it now (0 right, 1 left), -1 none
};

struct In {
    bool follow = false;           // [hands] throw_left_hand
    bool thrownKind = false;       // unticked: the game's weapon kind (gamestate::WeaponType) is a thrown one
    uint32_t weaponSide = 0;       // unticked: gamestate::WeaponHand - the slot that kind came from
    Slot slot[2];                  // ticked: by the game's slot (0 right, 1 left)
    int lastHand[2] = {kSideRight, kSideLeft};   // the hand each slot's weapon had on the frame before (its own hand at first)
};

struct Out {
    bool throws[2] = {false, false};   // by controller (0 left, 1 right): this controller throws
    int slotOf[2] = {-1, -1};          // ... the game's slot of the weapon it throws
    int hand[2] = {kSideRight, kSideLeft};   // by slot: the hand its weapon is in now (the next frame's lastHand)
    bool refused[2] = {false, false};        // by slot: a thrown weapon the game's Throw will not take (the left one beside a non-thrown weapon in the right)
};

// The game's Throw takes the left slot's weapon only when the right slot holds no weapon, or a thrown one too.
inline bool GameThrowsFrom(const Slot slot[2], int k) {
    if (!slot[k].thrown) return false;
    if (k == kSideRight) return true;
    return !slot[kSideRight].weapon || slot[kSideRight].thrown;
}

inline Out Plan(const In& in) {
    Out o;
    if (!in.follow) {
        if (in.thrownKind) {
            o.throws[kXrRight] = true;
            o.slotOf[kXrRight] = in.weaponSide == (uint32_t)kSideLeft ? kSideLeft : kSideRight;
        }
        o.hand[0] = in.lastHand[0];
        o.hand[1] = in.lastHand[1];
        return o;
    }
    for (int k = 0; k < 2; ++k) {
        const Slot& s = in.slot[k];
        int hand = k;
        if (s.placed) {
            if (s.carrier == kSideRight || s.carrier == kSideLeft) hand = s.carrier;
            else if (in.lastHand[k] == kSideRight || in.lastHand[k] == kSideLeft) hand = in.lastHand[k];   // nobody carries it: the hand that last did
        }
        o.hand[k] = hand;
        if (!s.thrown) continue;
        if (!GameThrowsFrom(in.slot, k)) {   // the game would refuse it ("Impossible"): not a throw of ours either
            o.refused[k] = true;
            continue;
        }
        const int xr = XrOfSide(hand);
        if (o.throws[xr]) continue;   // (both in one hand: the right slot's)
        o.throws[xr] = true;
        o.slotOf[xr] = k;
    }
    return o;
}

// The control of controller `xr`, throwing the weapon of slot `slot`, was let go as a throw - but the OTHER hand holds that same weapon (holders: the hands on
// it, a bit each, 1 right 2 left - weight::HoldersOf): a hand-over (or one of two hands let go), not a throw - it stays in VR in the other hand, the game
// must not throw it. A weapon of its own in the other hand (an axe beside the javelin) is not that. Only with throw_left_hand.
inline bool HandOver(bool follow, unsigned holders, int xr) {
    const int other = SideOfXr(xr) ^ 1;
    return follow && (holders & (1u << other)) != 0;
}

// The controller began to throw (it did not on the frame before) with its grip already squeezed on the weapon (the hand-over: the left grip took it, then the
// right let go): in the grip mode that grip is the weapon "in the hand" from now, its gesture begins held - not waiting for a fresh squeeze.
inline bool BeginsHeld(bool began, bool byGrip, bool gripDown) { return began && byGrip && gripDown; }

// Does the throw own controller `xr`'s control now (ticked, the weight model holding the weapons)? Only while that hand HOLDS the weapon it throws
// (weight::HoldersOf has its bit) - and for kOwnGraceS after that, or while its gesture is under way, so the release that threw it (the weight model may
// see the hand open first) is still the throw's. An empty hand - the javelin put down, lying, thrown and not yet back - is a free hand, as with a sword:
// its grip takes a belt bottle (Serg, 2026-10-28: with a javelin held no bottle came; with two javelins neither hand was ever free). The caller keeps
// ownedUntil per controller. Unticked, or no weight model: owned whenever the controller throws, as ever.
constexpr double kOwnGraceS = 0.3;
inline bool OwnsGrip(bool follow, bool placed, bool holdsIt, bool underWay, double now, double* ownedUntil) {
    if (!follow || !placed) return true;
    if (holdsIt || underWay) *ownedUntil = now + kOwnGraceS;
    return now < *ownedUntil;
}

// A control of controller `xr` is the throw's: the gesture owns that controller's control now.
inline bool OwnsOn(const bool owned[2], int xr) { return owned[xr & 1]; }

inline const char* XrName(int xr) { return xr == kXrLeft ? "left" : "right"; }

}  // namespace throwhand
