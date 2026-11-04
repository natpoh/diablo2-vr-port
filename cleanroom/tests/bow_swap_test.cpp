// vr/bow_swap.h + vr/ranged_hand.h's grab, pure: [bow] swap_by_grab - the free hand's squeeze at the bow's grip passes the bow into that hand, and back; at the string it
// is the string's; the remembered side read from the ini. No game, no headset.
//
//   bow_swap_test

#include "../../vr/bow_swap.h"
#include "../../vr/ranged_hand.h"

#include <cmath>
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

// both hands tracked, in front (not behind a shoulder), the grab on; `press` presses that hand's grip, `at` puts it at the bow's grip
In Grab(int press, bool at = true, bool busy = false, bool shoulder = true) {
	In in;
	in.weapon = true;
	in.bow = true;
	in.home = 1;
	in.grab = true;
	in.shoulder = shoulder;
	for (int h = 0; h < 2; ++h) {
		in.hand[h].tracked = true;
		in.hand[h].busy = busy;
	}
	if (press >= 0) {
		in.hand[press].press = true;
		in.hand[press].atGrip = at;
	}
	return in;
}

}   // namespace

int main() {
	// ---- where a squeeze takes the bow: the bow hand's grip pointing ahead (identity: knuckles down -> the controller's forward, -Z), the string 16 cm behind it ----
	{
		const bowdraw::Vec bow{-0.2f, -0.4f, -0.3f};
		const bowdraw::Quat ahead{};
		const bowdraw::Vec s = bowswap::StringPoint(bow, ahead);
		EXPECT(std::fabs(s.x - bow.x) < 1e-5f && std::fabs(s.y - bow.y) < 1e-5f && std::fabs(s.z - (bow.z + 0.16f)) < 1e-5f);
		EXPECT(bowswap::AtGrip({-0.2f, -0.25f, -0.3f}, bow, ahead));    // on the upper limb, 15 cm above the grip (nearer it than the string)
		EXPECT(bowswap::AtGrip({-0.2f, -0.55f, -0.32f}, bow, ahead));   // on the lower limb
		EXPECT(bowswap::AtGrip({-0.08f, -0.4f, -0.3f}, bow, ahead));    // beside the riser, 12 cm (two controllers come no nearer)
		EXPECT(!bowswap::AtGrip({-0.2f, -0.4f, -0.14f}, bow, ahead));   // at the string: the draw's
		EXPECT(!bowswap::AtGrip({-0.2f, -0.3f, -0.17f}, bow, ahead));   // nearer the string than the grip
		EXPECT(!bowswap::AtGrip({0.2f, -0.4f, -0.3f}, bow, ahead));     // the other side of the body, 40 cm
		EXPECT(!bowswap::AtGrip({-0.2f, -0.12f, -0.3f}, bow, ahead));   // 28 cm up: out of reach
		EXPECT(!bowswap::AtGrip({-0.2f, -0.25f, -0.3f}, bow, bowdraw::Quat{0, 0, 0, 0}));   // no turn known: the string at the grip, nothing taken
		EXPECT(!bowswap::AtGrip({NAN, 0, 0}, bow, ahead));
		// the bow held with the knuckles ahead (an upright grip turned flat): its forward is the knuckles', the string behind them
		const float h = std::sqrt(0.5f);
		const bowdraw::Quat flat{h, 0, 0, h};   // 90 deg about X: -Y (knuckles) goes to -Z
		const bowdraw::Vec sf = bowswap::StringPoint(bow, flat);
		EXPECT(std::fabs(sf.z - (bow.z + 0.16f)) < 1e-4f);
		EXPECT(bowswap::AtGrip({-0.2f, -0.25f, -0.3f}, bow, flat) && !bowswap::AtGrip({-0.2f, -0.4f, -0.14f}, bow, flat));
	}
	// ---- [bow] bow_hand as the ini has it ----
	{
		EXPECT(bowswap::ParseSide(L"1", 0) == 1 && bowswap::ParseSide(L"0", 1) == 0);
		EXPECT(bowswap::ParseSide(L"left", 0) == 1 && bowswap::ParseSide(L" Right ", 1) == 0 && bowswap::ParseSide(L"LEFT ; the bow", 0) == 1);
		EXPECT(bowswap::ParseSide(L"", 1) == 1 && bowswap::ParseSide(L"both", 0) == 0 && bowswap::ParseSide(nullptr, 1) == 1);
		EXPECT(bowswap::ParseSide(bowswap::SideValue(0), 1) == 0 && bowswap::ParseSide(bowswap::SideValue(1), 0) == 1);
	}
	// ---- the pass: the right (free) hand takes the bow from the left, then the left takes it back ----
	{
		Holster h;
		Out o = h.Step(Grab(-1));
		EXPECT(o.side == 1 && o.event == Event::None);
		o = h.Step(Grab(0));
		EXPECT(o.side == 0 && o.event == Event::Passed && o.by == 0 && !o.stowed);
		o = h.Step(Grab(-1));
		EXPECT(o.side == 0 && o.event == Event::None);   // it stays in the right hand
		o = h.Step(Grab(0));
		EXPECT(o.side == 0 && o.event == Event::None);   // the hand that holds it squeezing again: nothing (it is not the free hand)
		o = h.Step(Grab(1));
		EXPECT(o.side == 1 && o.event == Event::Passed && o.by == 1);
	}
	// ---- no pass: away from the grip (at the string), a busy hand (the string, a potion), an untracked hand, the holding hand untracked, the grab off, a crossbow ----
	{
		Holster h;
		EXPECT(h.Step(Grab(0, false)).side == 1);
		EXPECT(h.Step(Grab(0, true, true)).side == 1);
		In in = Grab(0);
		in.hand[0].tracked = false;
		EXPECT(h.Step(in).side == 1);
		in = Grab(0);
		in.hand[1].tracked = false;
		EXPECT(h.Step(in).side == 1);
		in = Grab(0);
		in.grab = false;
		EXPECT(h.Step(in).side == 1);
		in = Grab(0);
		in.bow = false;
		EXPECT(h.Step(in).side == 1 && h.Step(Grab(-1)).side == 1);
	}
	// ---- the grab keeps the side the bow was passed to (no home snap-back), and the home is where it starts - the remembered side ----
	{
		Holster h;
		h.Reset(0);
		In in = Grab(-1);
		in.home = 0;
		EXPECT(h.Step(in).side == 0);
		in = Grab(1);
		in.home = 0;
		EXPECT(h.Step(in).side == 1);
		in = Grab(-1);
		in.home = 0;
		EXPECT(h.Step(in).side == 1);
		in.weapon = false;   // the bow taken off: it starts fresh, in its home, next time
		EXPECT(h.Step(in).side == 0);
	}
	// ---- with the shoulder too: put away, nothing is passed; drawn with no shoulder swap, it comes back into the hand it was in (not the settings' home) ----
	{
		Holster h;
		h.Step(Grab(0));   // into the right hand
		In in = Grab(1);
		in.hand[1].atGrip = false;
		in.hand[1].inZone = true;
		Out o = h.Step(in);
		EXPECT(o.stowed && o.event == Event::PutAway && o.side == 0);
		EXPECT(h.Step(Grab(1)).event == Event::None);   // put away: no grip to take it at
		in = Grab(1);
		in.hand[1].atGrip = false;
		in.hand[1].inZone = true;
		o = h.Step(in);
		EXPECT(!o.stowed && o.event == Event::Drawn && o.side == 0);
	}
	// ---- without the shoulder (grab only): a squeeze behind a shoulder puts nothing away ----
	{
		Holster h;
		In in = Grab(1, false, false, false);
		in.hand[1].inZone = true;
		EXPECT(!h.Step(in).stowed);
		in = Grab(0, true, false, false);
		EXPECT(h.Step(in).event == Event::Passed);
	}
	if (g_fails) {
		std::printf("bow_swap_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("bow_swap_test: the grip zone against the string, the remembered side, the pass and back, busy/untracked/crossbow, with and without the shoulder: all passed\n");
	return 0;
}
