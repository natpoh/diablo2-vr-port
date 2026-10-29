// The ranged holster in the game's glue (vr/ranged_hand_wire.cpp) fed skel::Input like the skeleton hook does: a grip behind a shoulder puts a bow or crossbow away and draws it, into the hand
// that reached (the swap, bows) or back into its own hand; one step per head sample; a hand with the string or a potion does nothing; the box off forgets everything.
//
//   ranged_hand_wire_test

#include "../../vr/ranged_hand_wire.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_fails = 0;
std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }
bool Logged(const char* what) {
	for (const std::string& l : g_log)
		if (l.find(what) != std::string::npos) return true;
	return false;
}
int g_pulses = 0;
void Pulse(int, float, int) { ++g_pulses; }

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

struct Rig {
	skel::Input in;
	uint32_t sample = 0;
	Rig() {
		in.mode = 2;
		in.handsValid = 3;
		in.rangedHolster = false;   // (a new rig starts a new game: the wire forgets what the one before did)
		Step();
		in.rangedHolster = true;
		in.rangedHome = 1;
		in.rangedBone = 1;
		in.rangedBow = true;
		Front(0);
		Front(1);
		in.grip[0] = in.grip[1] = 0.0f;
		Step();
	}
	void Front(int side) { in.hand[side][0] = side ? -0.2f : 0.2f; in.hand[side][1] = -0.4f; in.hand[side][2] = -0.3f; }
	void Behind(int side, bool left) { in.hand[side][0] = left ? -0.25f : 0.25f; in.hand[side][1] = -0.1f; in.hand[side][2] = 0.15f; }
	skel::Input Step() {
		in.sample = ++sample;
		skel::Input c = in;
		rangedwire::Apply(&c);
		return c;
	}
	// the hand squeezes behind a shoulder: one step open, one step closed (a press)
	skel::Input Squeeze(int side, bool leftShoulder = false) {
		Behind(side, leftShoulder);
		in.grip[side] = 0.0f;
		Step();
		in.grip[side] = 1.0f;
		const skel::Input c = Step();
		return c;
	}
	void Release(int side) { in.grip[side] = 0.0f; Front(side); Step(); }
};

}   // namespace

int main() {
	rangedwire::SetLog(&LogLine);
	rangedwire::SetHaptic(&Pulse);

	// off: nothing handled
	{
		Rig r;
		r.in.rangedHolster = false;
		skel::Input c = r.Step();
		EXPECT(c.rangedSide == -1 && !c.rangedStowed && rangedwire::Side() == -1 && !rangedwire::Stowed());
	}
	// on: in its own hand; put away by the left hand behind the LEFT shoulder, drawn by the RIGHT hand behind the RIGHT one - without the swap it comes back into its own hand
	{
		Rig r;
		skel::Input c = r.Step();
		EXPECT(c.rangedSide == 1 && !c.rangedStowed && rangedwire::Side() == 1);
		g_log.clear();
		c = r.Squeeze(1, true);
		EXPECT(c.rangedStowed && rangedwire::Stowed() && c.rangedSide == 1);
		EXPECT(Logged("the left hand puts the bow away behind a shoulder"));
		r.Release(1);
		g_log.clear();
		c = r.Squeeze(0, false);
		EXPECT(!c.rangedStowed && c.rangedSide == 1);   // (no swap: its own hand, the left)
		EXPECT(Logged("the right hand draws the bow from behind a shoulder, into the left hand"));
	}
	// the swap: drawn into the hand that reached - and back again the same way
	{
		Rig r;
		r.in.rangedSwap = true;
		r.Squeeze(1, true);
		r.Release(1);
		g_log.clear();
		skel::Input c = r.Squeeze(0, false);
		EXPECT(!c.rangedStowed && c.rangedSide == 0 && rangedwire::Side() == 0);
		EXPECT(Logged("into the right hand (not its own: the swap)"));
		r.Release(0);
		r.Squeeze(0, true);
		r.Release(0);
		c = r.Squeeze(1, false);
		EXPECT(!c.rangedStowed && c.rangedSide == 1);
		// a crossbow is never swapped
		r.Release(1);
		r.in.rangedBow = false;
		r.Squeeze(1, true);
		r.Release(1);
		c = r.Squeeze(0, true);
		EXPECT(!c.rangedStowed && c.rangedSide == 1);
	}
	// a hand with the string (the draw's grip) or a potion does nothing; a grip held when the hand comes into the zone is no press; untracked: nothing
	{
		Rig r;
		r.in.rangedBusy[1] = true;
		skel::Input c = r.Squeeze(1, true);
		EXPECT(!c.rangedStowed);
		r.in.rangedBusy[1] = false;
		r.Release(1);
		r.in.shieldBusy[0] = true;
		c = r.Squeeze(0, false);
		EXPECT(!c.rangedStowed);
		r.in.shieldBusy[0] = false;
		r.Release(0);
		r.Front(1);
		r.in.grip[1] = 1.0f;
		r.Step();
		r.Behind(1, true);
		c = r.Step();
		EXPECT(!c.rangedStowed);
		r.Release(1);
		r.in.handsValid = 1;   // the left controller is not tracked
		c = r.Squeeze(1, true);
		EXPECT(!c.rangedStowed);
		r.in.handsValid = 3;
		r.Release(1);
	}
	// the same sample twice (the pair's second eye) does not step it again
	{
		Rig r;
		r.Behind(1, true);
		r.in.grip[1] = 0.0f;
		r.Step();
		r.in.grip[1] = 1.0f;
		r.in.sample = 500;
		skel::Input a = r.in, b = r.in;
		rangedwire::Apply(&a);
		r.in.grip[1] = 0.0f;
		b.grip[1] = 0.0f;
		rangedwire::Apply(&b);
		EXPECT(a.rangedStowed && b.rangedStowed);
	}
	// the pulse: a hand enters a shoulder while it is away
	{
		Rig r;
		r.in.shieldTick = 0.4f;
		r.Squeeze(1, true);
		r.Release(1);
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		g_pulses = 0;
		r.Behind(0, false);
		r.Step();
		EXPECT(g_pulses >= 1);
		r.Release(0);
	}
	// the holster off again: the weapon is in its own hand, everything forgotten
	{
		Rig r;
		r.Squeeze(1, true);
		EXPECT(rangedwire::Stowed());
		r.in.rangedHolster = false;
		g_log.clear();
		skel::Input c = r.Step();
		EXPECT(!c.rangedStowed && c.rangedSide == -1 && !rangedwire::Stowed() && Logged("ranged holster off"));
	}
	rangedwire::SetLog(nullptr);
	if (g_fails) {
		std::printf("ranged_hand_wire_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("ranged_hand_wire_test: put away and drawn through skel::Input (the swap, a crossbow, busy and untracked hands, one step a sample, the pulse, off): all passed\n");
	return 0;
}
