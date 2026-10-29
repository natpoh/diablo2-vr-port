// vr/ranged_hand.h, pure: a bow or crossbow put away behind a shoulder and drawn from it, into the hand that reached (the swap) or back into its own hand. No game, no headset.
//
//   ranged_hand_test

#include "../../vr/ranged_hand.h"

#include <cstdio>

using namespace rangedhand;

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

// both hands tracked and behind a shoulder; `press` presses that hand's grip
In Press(int press, bool swap, bool bow = true, int home = 1, bool busy = false, bool zone = true) {
	In in;
	in.weapon = true;
	in.swap = swap;
	in.bow = bow;
	in.home = home;
	for (int h = 0; h < 2; ++h) {
		in.hand[h].tracked = true;
		in.hand[h].inZone = zone;
		in.hand[h].busy = busy;
	}
	if (press >= 0) in.hand[press].press = true;
	return in;
}

}   // namespace

int main() {
	// starts in its own hand; a press away from the shoulders, an untracked hand, a busy hand: nothing
	{
		Holster h;
		Out o = h.Step(Press(-1, true));
		EXPECT(!o.stowed && o.side == 1);
		EXPECT(!h.Step(Press(0, true, true, 1, false, false)).stowed);
		In in = Press(1, true);
		in.hand[1].tracked = false;
		EXPECT(!h.Step(in).stowed);
		EXPECT(!h.Step(Press(1, true, true, 1, true)).stowed);
		in = Press(1, true);
		in.weapon = false;
		EXPECT(!h.Step(in).stowed);
	}
	// put away by either hand, drawn by either hand
	for (int by = 0; by < 2; ++by)
		for (int with = 0; with < 2; ++with) {
			Holster h;
			Out o = h.Step(Press(by, true));
			EXPECT(o.stowed && o.event == Event::PutAway && o.by == by);
			o = h.Step(Press(-1, true));
			EXPECT(o.stowed && o.event == Event::None);   // no press: stays away
			o = h.Step(Press(with, true));
			EXPECT(!o.stowed && o.event == Event::Drawn && o.by == with);
			EXPECT(o.side == with);   // the swap: into the hand that reached
		}
	// the swap off: always its own hand, whoever draws it
	for (int home = 0; home < 2; ++home)
		for (int with = 0; with < 2; ++with) {
			Holster h;
			h.Step(Press(home ^ 1, false, true, home));
			const Out o = h.Step(Press(with, false, true, home));
			EXPECT(!o.stowed && o.side == home);
		}
	// a crossbow is never swapped: drawn into its own hand even with the tick
	{
		Holster h;
		h.Step(Press(0, true, false, 1));
		const Out o = h.Step(Press(0, true, false, 1));
		EXPECT(!o.stowed && o.side == 1);
	}
	// the swap tick turned off while the bow is in the other hand: it is in its own hand again
	{
		Holster h;
		h.Step(Press(1, true, true, 1));
		h.Step(Press(0, true, true, 1));   // put away by the left, drawn by the right: the right hand holds it
		EXPECT(h.Side() == 0);
		EXPECT(h.Step(Press(-1, false, true, 1)).side == 1);
	}
	// one change in a step; both hands press at once: the right one's
	{
		Holster h;
		In in = Press(-1, true);
		in.hand[0].press = in.hand[1].press = true;
		const Out o = h.Step(in);
		EXPECT(o.stowed && o.by == 0);
	}
	// the weapon is gone (another one equipped): it starts fresh
	{
		Holster h;
		h.Step(Press(1, true));
		EXPECT(h.Stowed());
		In in = Press(-1, true);
		in.weapon = false;
		h.Step(in);
		EXPECT(!h.Stowed() && h.Side() == 1);
		EXPECT(!h.Step(Press(-1, true)).stowed);
	}
	// the invariant, over a random run: without the swap the hand is always home; stowed only changes by a press in the zone of a free tracked hand
	{
		unsigned seed = 4242u;
		for (int swap = 0; swap < 2; ++swap)
			for (int home = 0; home < 2; ++home) {
				Holster h;
				bool was = false;
				for (int i = 0; i < 5000; ++i) {
					seed = seed * 1664525u + 1013904223u;
					In in = Press(-1, swap != 0, (seed >> 4 & 7u) != 0, home);
					for (int k = 0; k < 2; ++k) {
						in.hand[k].tracked = (seed >> (8 + k) & 7u) != 0;
						in.hand[k].inZone = (seed >> (12 + k) & 3u) != 0;
						in.hand[k].busy = (seed >> (16 + k) & 7u) == 0;
						in.hand[k].press = (seed >> (20 + k) & 3u) == 0;
					}
					const Out o = h.Step(in);
					if (!(swap && in.bow)) EXPECT(o.side == home);
					if (o.stowed != was) {
						EXPECT(o.event != Event::None);
						bool ok = false;
						for (int k = 0; k < 2; ++k) ok = ok || (in.hand[k].tracked && in.hand[k].press && in.hand[k].inZone && !in.hand[k].busy);
						EXPECT(ok);
					} else {
						EXPECT(o.event == Event::None);
					}
					was = o.stowed;
				}
			}
	}
	if (g_fails) {
		std::printf("ranged_hand_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("ranged_hand_test: put away and drawn by either hand at either shoulder, the swap into the hand that reached (bows) or back into its own hand, busy and untracked hands, a fresh start: all passed\n");
	return 0;
}
