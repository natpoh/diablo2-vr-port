// vr/quiver_ctl.h on its own: the hands behind the shoulder, the taps, the steps and the haptic ticks, the pool following the weapon. No game, no headset.
//   quiver_ctl_test             exit code 0 = every check passed

#include "../../vr/quiver_ctl.h"

#include <cstdio>

using namespace quiver;

namespace {

int g_fails = 0, g_checks = 0;

#define EXPECT(c, ...)                                           \
	do {                                                         \
		++g_checks;                                              \
		if (!(c)) {                                              \
			++g_fails;                                           \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
			std::printf(__VA_ARGS__);                            \
			std::printf("\n");                                   \
		}                                                        \
	} while (0)

const Learned kLearned[] = {{15, 5, nullptr}, {20, 1, nullptr}, {25, 3, nullptr}};

// behind the right shoulder: x +0.2 (right), y -0.1, z +0.2 (back); and a hand at rest at the hip
void Behind(HandSample* h, bool right) {
	h->tracked = true;
	h->pos[0] = right ? 0.20f : -0.20f; h->pos[1] = -0.10f; h->pos[2] = 0.20f;
}
void Rest(HandSample* h, bool right) {
	h->tracked = true;
	h->pos[0] = right ? 0.25f : -0.25f; h->pos[1] = -0.55f; h->pos[2] = -0.30f;
}

struct Rig {
	Controller c;
	FrameIn in;
	double t = 1.0;
	Rig() {
		in.play = true;
		in.weapon = Kind::Javelin;
		in.learned = kLearned;
		in.nLearned = 3;
		in.learnedKnown = true;
		in.session = 1;
		in.epoch = 1;
		Rest(&in.hand[0], false);
		Rest(&in.hand[1], true);
	}
	FrameOut Frame() { in.t = t; t += 1.0 / 90.0; return c.Step(in); }
	// frames for `s` seconds, the sum of what happened
	FrameOut Run(double s) {
		FrameOut sum;
		for (double u = 0; u < s; u += 1.0 / 90.0) {
			const FrameOut o = Frame();
			sum.changed |= o.changed; sum.stepped |= o.stepped;
			for (int h = 0; h < 2; ++h) { sum.take[h] |= o.take[h]; sum.tick[h] += o.tick[h]; sum.inReach[h] |= o.inReach[h]; }
		}
		return sum;
	}
	// the right hand goes behind the shoulder, waits, taps the trigger for `down` s, lets go
	FrameOut RightTap(double down = 0.12, double wait = 0.20) {
		Behind(&in.hand[1], true);
		in.hand[1].trigger = 0.0f;
		FrameOut sum = Run(wait);
		in.hand[1].trigger = 1.0f;
		FrameOut a = Run(down);
		in.hand[1].trigger = 0.0f;
		FrameOut b = Run(0.1);
		sum.changed |= a.changed | b.changed; sum.stepped |= a.stepped | b.stepped;
		for (int h = 0; h < 2; ++h) { sum.take[h] |= a.take[h] | b.take[h]; sum.tick[h] += a.tick[h] + b.tick[h]; }
		return sum;
	}
};

void TestZoneTap() {
	std::printf("one hand's reach and trigger\n");
	ZoneTap z;
	HandSample h;
	Behind(&h, true);
	double t = 1.0;
	auto step = [&](float trig, bool allowed = true) { h.trigger = trig; t += 1.0 / 90.0; return z.Step(t, allowed, h); };
	// in the reach but not dwelled yet: nothing claimed, a press is no tap
	ZoneTap::Out o = step(0.0f);
	EXPECT(o.inReach && !o.claim && !o.tap, "just in");
	// after the dwell the trigger is the quiver's
	for (int i = 0; i < 12; ++i) o = step(0.0f);
	EXPECT(o.inReach && o.claim && !o.tap, "dwelled: claimed");
	// a press and a quick release is a tap, once
	int taps = 0;
	for (int i = 0; i < 8; ++i) taps += step(1.0f).tap;
	EXPECT(taps == 0, "no tap while down");
	for (int i = 0; i < 3; ++i) taps += step(0.0f).tap;
	EXPECT(taps == 1, "one tap on the release: %d", taps);
	// a long press is not a tap
	for (int i = 0; i < 60; ++i) step(1.0f);
	taps = 0;
	for (int i = 0; i < 3; ++i) taps += step(0.0f).tap;
	EXPECT(taps == 0, "a press of 0.67 s is not a tap");
	// the claim stays through the press, and ends with the release
	for (int i = 0; i < 5; ++i) o = step(0.0f);
	EXPECT(o.claim, "still claimed in the reach");
	// out of the reach: not claimed
	Rest(&h, true);
	o = step(0.0f);
	EXPECT(!o.inReach && !o.claim, "out of the reach");
	// a press that began outside and comes in with the hand is not a tap (it must be let go first)
	h.trigger = 1.0f;
	Behind(&h, true);
	for (int i = 0; i < 20; ++i) o = step(1.0f);
	EXPECT(!o.tap, "held on the way in: no press registered");
	taps = 0;
	for (int i = 0; i < 3; ++i) taps += step(0.0f).tap;
	EXPECT(taps == 0, "... and its release is no tap");
	// the hand leaves the reach while the trigger is down: no tap
	for (int i = 0; i < 12; ++i) step(0.0f);
	for (int i = 0; i < 4; ++i) step(1.0f);
	Rest(&h, true);
	taps = 0;
	for (int i = 0; i < 3; ++i) taps += step(0.0f).tap;
	EXPECT(taps == 0, "left the reach with the trigger down");
	// not allowed / not tracked: nothing
	Behind(&h, true);
	o = step(1.0f, false);
	EXPECT(!o.inReach && !o.claim && !o.tap, "not allowed");
	h.tracked = false;
	o = step(1.0f);
	EXPECT(!o.inReach && !o.claim, "not tracked");
	h.tracked = true;
	h.trigger = std::nanf("");
	o = z.Step(t, true, h);
	EXPECT(!o.claim && !o.tap, "NaN trigger");
}

void TestSteps() {
	std::printf("the controller: taps step the choice\n");
	Rig r;
	// no weapon change yet: the pool is built on the first frame, the default is chosen
	r.Frame();
	EXPECT(r.c.Sel().Count() == 4 && r.c.Sel().Id() == kDefault, "built: %d", r.c.Sel().Count());
	// the right hand's tap: next
	FrameOut o = r.RightTap();
	EXPECT(o.stepped && o.changed && r.c.Sel().Id() == 15, "right tap: Poison Javelin (%d)", r.c.Sel().Id());
	EXPECT(o.tick[1] == 1 && o.tick[0] == 0, "one pulse in the right hand: %d %d", o.tick[0], o.tick[1]);
	EXPECT(o.take[1] && !o.take[0], "the right trigger was the quiver's");
	// again
	Rest(&r.in.hand[1], true);
	r.Run(0.3);
	o = r.RightTap();
	EXPECT(r.c.Sel().Id() == 20, "next: Lightning Bolt (%d)", r.c.Sel().Id());
	// the left hand: previous
	Rest(&r.in.hand[1], true);
	r.Run(0.3);
	Behind(&r.in.hand[0], false);
	r.in.hand[0].trigger = 0.0f;
	r.Run(0.2);
	r.in.hand[0].trigger = 1.0f;
	r.Run(0.1);
	r.in.hand[0].trigger = 0.0f;
	o = r.Run(0.1);
	EXPECT(r.c.Sel().Id() == 15, "left tap: previous (%d)", r.c.Sel().Id());
	EXPECT(o.tick[0] == 1, "the pulse is in the left hand: %d", o.tick[0]);
	// back to the plain throw: two pulses
	r.in.hand[0].trigger = 0.0f;
	r.Run(0.2);
	r.in.hand[0].trigger = 1.0f;
	r.Run(0.1);
	r.in.hand[0].trigger = 0.0f;
	o = r.Run(0.1);
	EXPECT(r.c.Sel().Id() == kDefault && o.tick[0] == 2, "default: two pulses (%d)", o.tick[0]);
}

void TestGates() {
	std::printf("when it does nothing\n");
	// a menu / another view: no step, no claim
	{
		Rig r;
		r.Frame();
		r.in.play = false;
		const FrameOut o = r.RightTap();
		EXPECT(!o.stepped && !o.take[1] && r.c.Sel().Id() == kDefault, "not in play");
	}
	// the reach switched off
	{
		Rig r;
		r.Frame();
		r.in.zoneOn = false;
		const FrameOut o = r.RightTap();
		EXPECT(!o.stepped && !o.take[1], "the reach is off");
	}
	// the right trigger is a throw's control (throw_gesture): only the left hand's reach works
	{
		Rig r;
		r.Frame();
		r.in.rightTriggerIsThrows = true;
		FrameOut o = r.RightTap();
		EXPECT(!o.stepped && !o.take[1], "the right trigger is the throw's");
		Behind(&r.in.hand[0], false);
		r.in.hand[0].trigger = 0.0f;
		r.Run(0.2);
		r.in.hand[0].trigger = 1.0f;
		r.Run(0.1);
		r.in.hand[0].trigger = 0.0f;
		o = r.Run(0.1);
		EXPECT(r.c.Sel().Id() == 25, "the left hand still steps (previous wraps to the last: %d)", r.c.Sel().Id());
	}
	// the LEFT trigger is a throw's control (throw_gesture with throw_left_hand, the weapon in the left hand): the left hand's reach does nothing,
	// the right one's still steps
	{
		Rig r;
		r.Frame();
		r.in.leftTriggerIsThrows = true;
		Behind(&r.in.hand[0], false);
		r.in.hand[0].trigger = 0.0f;
		r.Run(0.2);
		r.in.hand[0].trigger = 1.0f;
		FrameOut o = r.Run(0.1);
		r.in.hand[0].trigger = 0.0f;
		FrameOut o2 = r.Run(0.1);
		EXPECT(!o.stepped && !o2.stepped && !o.take[0] && !o2.take[0], "the left trigger is the throw's");
		Rest(&r.in.hand[0], false);
		o = r.RightTap();
		EXPECT(o.stepped && o.take[1], "the right hand still steps");
	}
	// a weapon that is not thrown: nothing offered, nothing claimed
	{
		Rig r;
		r.in.weapon = Kind::None;
		r.Frame();
		const FrameOut o = r.RightTap();
		EXPECT(r.c.Sel().Count() == 0 && !o.stepped && !o.take[1], "no weapon");
	}
	// a potion: nothing offered
	{
		Rig r;
		r.in.weapon = Kind::Potion;
		r.Frame();
		const FrameOut o = r.RightTap();
		EXPECT(r.c.Sel().Count() == 0 && !o.stepped && !o.take[1], "a potion has no quiver");
	}
	// nothing learned beyond the plain throw: nothing to cycle, the trigger stays the layout's
	{
		Rig r;
		r.in.nLearned = 0;
		r.Frame();
		const FrameOut o = r.RightTap();
		EXPECT(r.c.Sel().Count() == 1 && !o.stepped && !o.take[1], "nothing to choose from");
	}
}

void TestLayout() {
	std::printf("a remapped button\n");
	Rig r;
	r.Frame();
	r.in.layoutStep = +1;
	FrameOut o = r.Frame();
	r.in.layoutStep = 0;
	EXPECT(o.stepped && r.c.Sel().Id() == 15 && o.tick[1] == 1, "next: %d", r.c.Sel().Id());
	r.in.layoutStep = -1;
	o = r.Frame();
	r.in.layoutStep = 0;
	EXPECT(r.c.Sel().Id() == kDefault && o.tick[1] == 2, "previous: back at the default, two pulses");
	r.in.layoutStep = -1;
	o = r.Frame();
	r.in.layoutStep = 0;
	EXPECT(r.c.Sel().Id() == 25, "previous wraps");
	// not in play / not a thrown weapon: ignored
	r.in.play = false;
	r.in.layoutStep = +1;
	o = r.Frame();
	r.in.layoutStep = 0;
	EXPECT(!o.stepped && r.c.Sel().Id() == 25, "not in play");
}

void TestPoolFollowsTheHand() {
	std::printf("the pool follows the weapon and the learned list\n");
	Rig r;
	r.Frame();
	r.in.layoutStep = +1;
	r.Frame();
	r.in.layoutStep = 0;
	EXPECT(r.c.Sel().Id() == 15, "on Poison Javelin");
	// a knife in hand: Poison Javelin is not allowed; back to the plain throw, reported changed
	r.in.weapon = Kind::Knife;
	FrameOut o = r.Frame();
	EXPECT(o.changed && r.c.Sel().Id() == kDefault && r.c.Sel().Count() == 1, "a knife: %d entries", r.c.Sel().Count());
	// the javelin again: the choice was lost, the pool is back
	r.in.weapon = Kind::Javelin;
	o = r.Frame();
	EXPECT(r.c.Sel().Count() == 4 && r.c.Sel().Id() == kDefault, "javelin again");
	// the learned list grows: kept choice, bigger pool
	r.in.layoutStep = +1;
	r.Frame();
	r.in.layoutStep = 0;
	const Learned more[] = {{15, 5, nullptr}, {20, 1, nullptr}, {25, 3, nullptr}, {35, 1, nullptr}};
	r.in.learned = more;
	r.in.nLearned = 4;
	o = r.Frame();
	EXPECT(r.c.Sel().Count() == 5 && r.c.Sel().Id() == 15 && !o.changed, "learned a new one: kept (%d entries, on %d)", r.c.Sel().Count(), r.c.Sel().Id());
	// the list not read (stale): the pool stays as it was
	r.in.learnedKnown = false;
	r.in.learned = nullptr;
	r.in.nLearned = 0;
	o = r.Frame();
	EXPECT(r.c.Sel().Count() == 5 && r.c.Sel().Id() == 15, "an unread list changes nothing");
	// a weapon put on while the list is unread: the default alone, the pool again when the list comes
	r.in.weapon = Kind::Axe;
	r.Frame();
	EXPECT(r.c.Sel().Count() == 1, "an axe with the list unread");
	r.in.learnedKnown = true;
	r.in.learned = more;
	r.in.nLearned = 4;
	r.in.otherHandThrown = true;
	r.Frame();
	EXPECT(r.c.Sel().Count() == 1, "an axe: no javelin skill, no Double Throw learned");
	// a new session forgets everything
	r.in.weapon = Kind::Javelin;
	r.in.otherHandThrown = false;
	r.Frame();
	r.in.layoutStep = +1;
	r.Frame();
	r.in.layoutStep = 0;
	EXPECT(r.c.Sel().Id() == 15, "on Poison Javelin again");
	r.in.session = 2;
	r.Frame();
	EXPECT(r.c.Sel().Id() == kDefault, "a new session starts at the plain throw");
}

}  // namespace

int main() {
	TestZoneTap();
	TestSteps();
	TestGates();
	TestLayout();
	TestPoolFollowsTheHand();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
