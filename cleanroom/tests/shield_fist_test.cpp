// Synthetic checks of vr/shield_fist.h: which grip the fingers of a hand with a shield follow, and when the game's own animation
// stays. And the contract the weight model gives it: a hand with a shield in it (told to the model as a busy hand) takes no weapon
// and drops none, however its grip is worked. No game, no headset.
#include "../../vr/shield_fist.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "heft.h"
#include "hold.h"

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

shieldfist::Want Ready() {
	shieldfist::Want w;
	w.on = w.f4 = w.shield = w.arms = w.tracked = true;
	w.rawGrip = 0.0f;
	w.layoutGrip = 0.0f;
	return w;
}

// -- the weight model: the hand with a shield takes nothing ---------------------------------------------------------
namespace hh = heft::hold;
using heft::Q;
using heft::V3;

hh::HandIn Hand(V3 pos, float grip, bool busy) {
	hh::HandIn h;
	h.tracked = true;
	h.pose = {pos, Q{}};
	h.grip = grip;
	h.busy = busy;
	return h;
}

}  // namespace

int main() {
	// -- Grip(): the pure rule ---------------------------------------------------------------------------------------
	{
		shieldfist::Want w = Ready();
		EXPECT(shieldfist::Grip(w) == 0.0f, "an open hand with a shield follows its grip: open (%.2f)", shieldfist::Grip(w));
		w.rawGrip = 1.0f;
		EXPECT(shieldfist::Grip(w) == 1.0f, "a squeezed grip closes the hand (%.2f)", shieldfist::Grip(w));
		w.rawGrip = 0.37f;
		EXPECT(std::fabs(shieldfist::Grip(w) - 0.37f) < 1e-6f, "a half grip is a half fist (%.2f)", shieldfist::Grip(w));
		w.rawGrip = 1.7f;
		EXPECT(shieldfist::Grip(w) == 1.0f, "a grip past 1 is clamped (%.2f)", shieldfist::Grip(w));
	}
	{   // each switch off: the game's own animation (-1)
		for (int off = 0; off < 6; ++off) {
			shieldfist::Want w = Ready();
			w.rawGrip = 1.0f;
			switch (off) {
				case 0: w.on = false; break;           // [hands] shield_fist unticked
				case 1: w.f4 = false; break;           // F1 / F2 / F3 / flat
				case 2: w.shield = false; break;       // no shield on the arm
				case 3: w.arms = false; break;         // the arms are the game's
				case 4: w.tracked = false; break;      // the controller is not tracked
				default: w.closedAlready = true; break;   // the free-hand rule closed it already
			}
			EXPECT(shieldfist::Grip(w) == -1.0f, "case %d: not the shield fist's (%.2f)", off, shieldfist::Grip(w));
		}
	}
	{   // the RAW grip, not the layout's: a grip the layout took (a belt potion, a chord, the shoulder swap) reads as let go there
		shieldfist::Want w = Ready();
		w.rawGrip = 0.9f;
		w.layoutGrip = 0.0f;
		EXPECT(shieldfist::Grip(w) == 0.9f, "a grip taken from the layout still closes the fingers (%.2f)", shieldfist::Grip(w));
		w.rawGrip = 0.0f;
		w.layoutGrip = 1.0f;
		EXPECT(shieldfist::Grip(w) == 0.0f, "the raw grip let go opens the hand whatever the layout says (%.2f)", shieldfist::Grip(w));
	}
	{   // no native frame (BodyWalk's tracking alone): the shared block's grip is the real one
		shieldfist::Want w = Ready();
		w.rawGrip = -1.0f;
		w.layoutGrip = 0.6f;
		EXPECT(shieldfist::Grip(w) == 0.6f, "without a raw grip the layout's is used (%.2f)", shieldfist::Grip(w));
		w.layoutGrip = -1.0f;
		EXPECT(shieldfist::Grip(w) == -1.0f, "no grip known at all: the game's animation (%.2f)", shieldfist::Grip(w));
		w.rawGrip = NAN;
		w.layoutGrip = NAN;
		EXPECT(shieldfist::Grip(w) == -1.0f, "a NaN grip is no grip");
		w.rawGrip = NAN;
		w.layoutGrip = 0.5f;
		EXPECT(shieldfist::Grip(w) == 0.5f, "a NaN raw grip falls back to the layout's (%.2f)", shieldfist::Grip(w));
	}

	// -- the weight model and the shield hand -------------------------------------------------------------------------
	// A sword in the right hand, placed as the game holds it. The left hand has a shield (vrcam tells the model: busy). Its grip is
	// squeezed with the fist on the shaft, then let go: it takes nothing, and the right hand keeps the sword throughout.
	{
		hh::Settings s;
		hh::Hold w;
		const double dt = 1.0 / 90.0;
		hh::HandIn in[2] = {Hand({0.15, 1.3, -0.4}, 0, false), Hand({-0.3, 1.0, 0.0}, 0, true)};
		w.Place(0, hh::Kind::Sword, false, {{}, V3{0, 0, -1}}, in, s);
		hh::HandOut out[2];
		for (double t = 0; t < 1.0; t += dt) w.Step(in, dt, s, out);
		EXPECT(out[0].holds && w.Held(), "the sword was not held in the right hand as placed");
		V3 butt, tip;
		w.Ends(&butt, &tip);
		const V3 mid{(butt.x + tip.x) / 2, (butt.y + tip.y) / 2, (butt.z + tip.z) / 2};
		in[1] = Hand(mid, 1.0f, true);   // the left fist right on the shaft, grip squeezed, a shield on that hand
		for (double t = 0; t < 0.6; t += dt) w.Step(in, dt, s, out);
		EXPECT(!out[1].holds, "a hand with a shield took the sword by its grip");
		in[1].grip = 0.0f;
		for (double t = 0; t < 0.3; t += dt) w.Step(in, dt, s, out);
		EXPECT(out[0].holds && w.Held() && !out[1].holds, "the sword was dropped or taken as the shield hand's grip was let go");
		// the same hand and grip with nothing in it (not busy) does take it: the test above is not vacuous
		in[1] = Hand(mid, 1.0f, false);
		for (double t = 0; t < 0.6; t += dt) w.Step(in, dt, s, out);
		EXPECT(out[1].holds, "a free hand on the shaft with its grip squeezed did not take the sword (the test is vacuous)");
	}

	if (g_fails) {
		std::printf("shield_fist_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::puts("shield_fist_test: all checks passed");
	return 0;
}
