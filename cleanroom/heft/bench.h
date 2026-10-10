#pragma once
// The bench's weapons and controller paths, one copy for the synthetic tests
// (cleanroom/tests/heft_test.cpp) and the Unity bench (tools/heft_bench_unity,
// through heft_capi.cpp), so both measure the same thing.
//
// Frames are Unity's: X right, Y up, Z forward. A weapon's own frame (the grip
// frame): the main palm at the origin, the weapon's length along +Y (as D2R
// hangs a weapon on its attach bone). Held level and forward the controller's
// rotation turns +Y onto +Z.

#include "heft.h"

namespace heft::bench {

constexpr double kHandMass = 2.0;   // kg, Hurricane's physics hand (no gravity on it)

struct Weapon {
	const char* name;
	Piece       pieces[4];
	int         nPieces;
	V3          tip;          // the point / the head's end, grip frame
	V3          second;       // where the other hand holds when two hands are on
	bool        twoHanded;    // the bench also runs it with both hands
};

int           WeaponCount();
const Weapon& WeaponAt(int i);
Body          WeaponOnly(const Weapon& w);                // the steel alone (the reference's weapon rigidbody)
Body          Held(const Weapon& w, bool twoHands);       // steel and hands: our one body

// Hurricane's hand strengths, the reference's numbers (PDStrength assets):
// the default hand, and each hand on a large two-handed weapon.
Muscle OneHand();
Muscle TwoHandEach();

struct Path {
	const char* name;
	double      duration;   // s
	bool        twoHands;   // the second controller holds weapon.second
};
int         PathCount();
const Path& PathAt(int i);
// A controller's pose at time t (hand 0 the main one). The second controller is
// where the second grip would be on a weapon that followed the main one exactly.
Pose ControllerAt(int path, const Weapon& w, int hand, double t);

}   // namespace heft::bench
