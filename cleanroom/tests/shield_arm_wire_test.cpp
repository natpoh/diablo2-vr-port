// The shield's arm in the game's glue (vr/shield_arm_wire.cpp) fed skel::Input like the skeleton hook does, with a stand-in for the weight model's WieldingHand: the shield is on the
// arm opposite the hand that holds the sword or that it is drawn into, stays where it is while the sword is away, one step a head sample, the box off forgets everything.
//
//   shield_arm_wire_test

#include "../../vr/shield_arm_wire.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace weight {
bool g_holds[2] = {false, false};   // (the weight model's HoldsWeapon, which the wire asks of the hand that would put the shield away)
bool HoldsWeapon(int side) { return side >= 0 && side < 2 && g_holds[side]; }
bool g_placed = true;   // (and whether it holds the weapon at all)
bool Placed() { return g_placed; }
}  // namespace weight

namespace {

int g_fails = 0;
unsigned g_wielding = 0;
unsigned Wielder() { return g_wielding; }
std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }
int g_pulses = 0;
void Pulse(int, float, int) { ++g_pulses; }
bool Logged(const char* what) {
	for (const std::string& l : g_log)
		if (l.find(what) != std::string::npos) return true;
	return false;
}

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
		g_wielding = 0;
		in.shieldHolster = false;   // (a new rig starts a new game: the follower forgets what the one before did)
		Step();
		in.shieldHolster = true;
	}
	int Step() {   // one pose of a new head sample; the answer
		in.sample = ++sample;
		skel::Input c = in;
		shieldarmwire::Apply(&c);
		EXPECT(c.shieldArm == c.weight.shieldArm);
		return c.shieldArm;
	}
};

}   // namespace

int main() {
	shieldarmwire::SetLog(&LogLine);
	shieldarmwire::SetWielderSource(&Wielder);
	shieldarmwire::SetHaptic(&Pulse);

	// on: it starts on the left arm; off: -1, nothing handled
	{
		Rig r;
		EXPECT(r.Step() == 1 && shieldarmwire::Arm() == 1 && shieldarmwire::WearingHand() == 1);
		r.in.shieldHolster = false;
		EXPECT(r.Step() == -1 && shieldarmwire::Arm() == -1 && shieldarmwire::WearingHand() == -1);
		r.in.shieldHolster = true;
		EXPECT(r.Step() == 1);
		r.in.mode = 0;   // the arms are not on the controllers
		EXPECT(r.Step() == -1);
	}

	// the sword drawn into the left hand: the shield on the right arm; let go: stays; into the right hand: the left arm again
	{
		Rig r;
		g_wielding = 1;   // the weapon in the right hand
		EXPECT(r.Step() == 1);
		g_wielding = 2;   // ... in the left, alone (drawn into it, taken up by it, passed to it)
		EXPECT(r.Step() == 0 && shieldarmwire::WearingHand() == 0);
		EXPECT(Logged("the weapon is in the left hand, the shield jumps to the right arm (mirrored)"));
		EXPECT(r.Step() == 0);
		g_wielding = 0;   // let go: falling, lying
		for (int i = 0; i < 4; ++i) EXPECT(r.Step() == 0);
		g_wielding = 1;   // into the right hand, which wears the shield now
		EXPECT(r.Step() == 1);
		EXPECT(Logged("the weapon is in the right hand, the shield jumps to the left arm"));
		g_wielding = 3;   // both hands on it (a pass in progress): stays
		EXPECT(r.Step() == 1);
		g_wielding = 2;   // the right hand lets go: alone in the left hand, the shield's: it jumps
		EXPECT(r.Step() == 0);
		g_wielding = 0;
	}

	// the same sample twice (the pair's second eye) does not step it again
	{
		Rig r;
		g_wielding = 1;
		r.Step();
		g_wielding = 2;
		r.in.sample = 100;
		skel::Input a = r.in, b = r.in;
		shieldarmwire::Apply(&a);
		g_wielding = 1;   // (it changed between the eyes: the pair keeps the first answer)
		shieldarmwire::Apply(&b);
		EXPECT(a.shieldArm == 0 && b.shieldArm == 0);
		g_wielding = 0;
	}


	// the put-away gesture ([hands] shoulder_draw_shield_stow): a grip behind the LEFT shoulder, by a hand that holds nothing
	{
		const auto zone = [](skel::Input& in, int side, bool inZone) {   // that hand behind the left shoulder or in front of the body
			if (inZone) { in.hand[side][0] = -0.24f; in.hand[side][1] = -0.08f; in.hand[side][2] = 0.15f; }
			else { in.hand[side][0] = side ? -0.2f : 0.2f; in.hand[side][1] = -0.4f; in.hand[side][2] = -0.3f; }
		};
		{   // the tick off: nothing happens
			Rig r;
			zone(r.in, 1, true);
			r.in.grip[1] = 0.0f;
			r.Step();
			r.in.grip[1] = 1.0f;
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed());
		}
		Rig r;
		r.in.shieldStow = true;
		r.in.shieldFollow = false;   // (independent of the follow tick)
		r.in.handsValid = 3;
		zone(r.in, 0, false);
		zone(r.in, 1, true);
		r.in.grip[0] = r.in.grip[1] = 0.0f;
		EXPECT(r.Step() == 1);
		g_log.clear();
		r.in.grip[1] = 1.0f;   // the left hand squeezes behind the left shoulder
		EXPECT(r.Step() == 2 && shieldarmwire::Stowed() && shieldarmwire::WearingHand() == -1);
		EXPECT(Logged("the left hand puts the shield away at the left shoulder"));
		{   // the answer in the input too: shieldArm 2, and the weight model told nobody wears it
			skel::Input c = r.in;
			c.sample = ++r.sample;
			shieldarmwire::Apply(&c);
			EXPECT(c.shieldArm == 2 && c.weight.shieldArm == 2 && !c.weight.shieldFollow);
		}
		r.in.grip[1] = 0.0f;
		EXPECT(r.Step() == 2);   // a let go is not a press
		// the weapon comes into the left hand meanwhile: the shield is away, nothing to move (and it is still away)
		g_wielding = 2;
		EXPECT(r.Step() == 2);
		g_wielding = 0;
		// the RIGHT hand behind the left shoulder draws it onto the right arm
		zone(r.in, 0, true);
		zone(r.in, 1, false);
		r.in.grip[0] = 0.0f;
		r.Step();
		g_log.clear();
		r.in.grip[0] = 1.0f;
		EXPECT(r.Step() == 0 && !shieldarmwire::Stowed() && shieldarmwire::WearingHand() == 0);
		EXPECT(Logged("the right hand draws the shield from the left shoulder onto the right arm (mirrored)"));
		// a hand that holds the weapon does nothing; a hand with a potion does nothing
		r.in.grip[0] = 0.0f;
		r.Step();
		weight::g_holds[0] = true;
		r.in.grip[0] = 1.0f;
		EXPECT(r.Step() == 0);   // the right hand holds the weapon: the grip behind the shoulder does not put the shield away
		weight::g_holds[0] = false;
		r.in.grip[0] = 0.0f;
		r.Step();
		r.in.shieldBusy[0] = true;
		r.in.grip[0] = 1.0f;
		EXPECT(r.Step() == 0);
		r.in.shieldBusy[0] = false;
		r.in.grip[0] = 0.0f;
		r.Step();
		// a grip already held when the hand comes into the zone is not a press
		zone(r.in, 0, false);
		r.in.grip[0] = 1.0f;
		r.Step();
		zone(r.in, 0, true);
		EXPECT(r.Step() == 0);   // (arrived with the grip held: nothing yet)
		r.in.grip[0] = 0.0f;
		EXPECT(r.Step() == 2);   // ... but the hand wears the shield and carried it there: let go behind the shoulder puts it away
		r.in.grip[0] = 1.0f;
		EXPECT(r.Step() == 0);   // a squeeze in the zone draws it again onto this arm
		// the tick turned off with the shield away: it is back on the arm it was on (the right)
		r.in.grip[0] = 0.0f;
		r.Step();                // (the release of the grip that has just drawn it is spent: nothing)
		r.in.grip[0] = 1.0f;
		EXPECT(r.Step() == 2);   // put away again, by the squeeze
		r.in.shieldStow = false;
		r.in.grip[0] = 0.0f;
		EXPECT(r.Step() == 0 && !shieldarmwire::Stowed());
		// the pulse: a hand that enters the left shoulder while the shield is away
		r.in.shieldStow = true;
		r.in.shieldTick = 0.4f;
		zone(r.in, 0, false);
		r.in.grip[0] = 0.0f;
		r.Step();
		r.in.grip[0] = 1.0f;
		zone(r.in, 0, true);
		r.Step();   // put away (the press)
		g_pulses = 0;
		zone(r.in, 0, false);
		r.in.grip[0] = 0.0f;
		r.Step();
		std::this_thread::sleep_for(std::chrono::milliseconds(400));   // (a tick is at least 300 ms after the last)
		zone(r.in, 1, true);   // the left hand enters
		r.Step();
		EXPECT(g_pulses >= 1);
	}

	// the shield carried behind the left shoulder with the grip held and let go there: put away - the way a weapon goes to the back; the squeeze in the zone still works beside it
	{
		const auto place = [](skel::Input& in, int side, float x, float y, float z) { in.hand[side][0] = x; in.hand[side][1] = y; in.hand[side][2] = z; };
		const auto elsewhere = [&](Rig& r, int side) { place(r.in, side, side ? -0.2f : 0.2f, -0.4f, -0.3f); };
		const auto zone = [&](Rig& r, int side) { place(r.in, side, -0.24f, -0.08f, 0.15f); };
		weight::g_holds[0] = weight::g_holds[1] = false;
		{   // squeezed out in front, carried into the zone, let go there
			Rig r;
			r.in.shieldStow = true;
			r.in.shieldFollow = false;
			elsewhere(r, 0);
			elsewhere(r, 1);
			r.in.grip[0] = r.in.grip[1] = 0.0f;
			r.Step();
			g_log.clear();
			r.in.grip[1] = 1.0f;   // the left hand, which wears the shield, takes hold of it
			r.Step();
			EXPECT(Logged("the left hand holds the shield") && !shieldarmwire::Stowed());
			g_log.clear();
			zone(r, 1);   // carried behind the left shoulder, the grip still down
			r.Step();
			EXPECT(!shieldarmwire::Stowed() && Logged("it holds the shield: let go of the grip here to put it away"));
			g_log.clear();
			r.in.grip[1] = 0.0f;   // let go there
			EXPECT(r.Step() == 2 && shieldarmwire::Stowed());
			EXPECT(Logged("the left hand puts the shield away at the left shoulder") && Logged("carried there with the grip held, let go"));
			// the grip is let go again, still there: nothing more (a release is never a draw)
			r.in.grip[1] = 1.0f;   // (a squeeze in the zone: that is the draw, onto the arm of the hand that squeezed)
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed());
			r.Step();   // (the grip stays down a few samples after the draw: it is the draw's, not a hold on the shield)
			r.Step();
			g_log.clear();
			r.in.grip[1] = 0.0f;   // ... and the release that follows the draw does not put it away again
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed() && !Logged("puts the shield away"));
		}
		{   // the grip already held when the hand comes into the zone, and when the holster starts
			Rig r;
			r.in.shieldStow = true;
			elsewhere(r, 0);
			elsewhere(r, 1);
			r.in.grip[0] = 0.0f;
			r.in.grip[1] = 1.0f;   // held from the start
			r.Step();
			r.Step();
			zone(r, 1);
			r.Step();
			r.in.grip[1] = 0.0f;
			EXPECT(r.Step() == 2 && shieldarmwire::Stowed());
		}
		{   // let go outside the zone: the shield stays, and the log says where the hand was
			Rig r;
			r.in.shieldStow = true;
			elsewhere(r, 0);
			elsewhere(r, 1);
			r.in.grip[0] = r.in.grip[1] = 0.0f;
			r.Step();
			r.in.grip[1] = 1.0f;
			r.Step();
			std::this_thread::sleep_for(std::chrono::milliseconds(300));   // (a carry, not a tap)
			r.Step();
			g_log.clear();
			r.in.grip[1] = 0.0f;
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed() && Logged("let go of the shield's grip at x -0.20") && Logged("not behind the left shoulder"));
		}
		{   // a hand that holds the weapon (or a potion) carries no shield: its release does nothing
			Rig r;
			r.in.shieldStow = true;
			elsewhere(r, 0);
			elsewhere(r, 1);
			r.in.grip[0] = r.in.grip[1] = 0.0f;
			r.Step();
			weight::g_holds[1] = true;
			r.in.grip[1] = 1.0f;
			r.Step();
			zone(r, 1);
			r.Step();
			r.in.grip[1] = 0.0f;
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed());
			weight::g_holds[1] = false;
			r.in.shieldBusy[1] = true;   // a belt potion
			r.in.grip[1] = 1.0f;
			elsewhere(r, 1);
			r.Step();
			zone(r, 1);
			r.Step();
			r.in.grip[1] = 0.0f;
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed());
		}
		{   // the tick off: carrying and letting go change nothing; the old way (a squeeze in the zone) still puts it away
			Rig r;
			r.in.shieldStow = false;
			elsewhere(r, 1);
			r.in.grip[1] = 1.0f;
			r.Step();
			zone(r, 1);
			r.Step();
			r.in.grip[1] = 0.0f;
			EXPECT(r.Step() == 1 && !shieldarmwire::Stowed());
			Rig q;
			q.in.shieldStow = true;
			zone(q, 1);
			elsewhere(q, 0);
			q.in.grip[0] = q.in.grip[1] = 0.0f;
			q.Step();
			q.in.grip[1] = 1.0f;   // the old way: a squeeze with the hand already in the zone
			g_log.clear();
			EXPECT(q.Step() == 2 && shieldarmwire::Stowed() && Logged("squeezed there"));
		}
		{   // the weapon in the shield's hand: the shield is on the other arm, and that hand is the weapon's
			Rig r;
			r.in.shieldStow = true;
			elsewhere(r, 0);
			elsewhere(r, 1);
			r.in.grip[0] = r.in.grip[1] = 0.0f;
			g_wielding = 2;   // the weapon alone in the left hand: the shield is on the right arm now
			r.Step();
			r.Step();
			r.in.grip[1] = 1.0f;
			zone(r, 1);
			weight::g_holds[1] = true;
			r.Step();
			r.in.grip[1] = 0.0f;
			r.Step();
			EXPECT(!shieldarmwire::Stowed());   // (the left hand holds the sword and does not wear the shield: its release is nothing)
			weight::g_holds[1] = false;
			g_wielding = 0;
		}
	}

	// the follow rule told each time the hands that hold the weapon change: a jump has its line; when the shield stays, the line says why
	{
		Rig r;
		g_wielding = 0;
		r.Step();   // (the first sample after the holster comes on tells nothing of the hands)
		g_log.clear();
		g_wielding = 1;   // the sword in the right hand: the shield is on the left arm already
		EXPECT(r.Step() == 1);
		EXPECT(Logged("the weapon is now in the right hand; the shield stays on the left arm: that is the hand opposite the shield"));
		g_log.clear();
		g_wielding = 3;   // the left hand takes it too (a second hand, a hand-over): both hands on it
		EXPECT(r.Step() == 1);
		EXPECT(Logged("the weapon is now in both hands; the shield stays on the left arm: both hands are on it"));
		g_log.clear();
		g_wielding = 0;
		weight::g_placed = false;
		EXPECT(r.Step() == 1);
		EXPECT(Logged("now in no hand") && Logged("the weight model does not hold this weapon"));
		g_log.clear();
		weight::g_placed = true;
		g_wielding = 1;
		r.Step();
		g_log.clear();
		g_wielding = 0;
		r.Step();
		EXPECT(Logged("no hand holds it (it is falling, lying, on the back, or just let go)"));
		// the left hand has it alone: it jumps (its own line), and there is no "stays" line for that change
		g_log.clear();
		g_wielding = 2;
		EXPECT(r.Step() == 0);
		EXPECT(Logged("the shield jumps to the right arm") && !Logged("the shield stays"));
		// the same hands in the next sample: nothing more is told
		g_log.clear();
		r.Step();
		EXPECT(g_log.empty());
		// the follow tick off: the reason is the tick
		r.in.shieldFollow = false;
		g_log.clear();
		g_wielding = 1;
		r.Step();
		EXPECT(Logged("shoulder_draw_shield is off"));
		r.in.shieldFollow = true;
		g_wielding = 0;
		// the holster off: the line says it, and why it is off
		g_log.clear();
		r.in.shieldHolster = false;
		r.Step();
		EXPECT(Logged("shield follows the sword off") && Logged("the holster is off"));
		g_log.clear();
		r.in.shieldHolster = true;
		r.Step();
		r.in.mode = 0;
		r.Step();
		EXPECT(Logged("the arms are not on the controllers"));
	}

	// the gesture told step by step (a gesture that "never fires" says where it stops): a hand coming behind the left shoulder, a squeeze there that did nothing, a squeeze elsewhere - and
	// nothing of it with the tick off
	{
		const auto place = [](skel::Input& in, int side, float x, float y, float z) { in.hand[side][0] = x; in.hand[side][1] = y; in.hand[side][2] = z; };
		{   // the tick off: no step lines
			Rig r;
			g_log.clear();
			place(r.in, 1, -0.24f, -0.08f, 0.15f);
			r.in.grip[1] = 0.0f;
			r.Step();
			r.in.grip[1] = 1.0f;
			r.Step();
			EXPECT(!Logged("is behind the left shoulder") && !Logged("squeezed"));
		}
		Rig r;
		r.in.shieldStow = true;
		place(r.in, 0, 0.2f, -0.4f, -0.3f);
		place(r.in, 1, -0.2f, -0.4f, -0.3f);
		r.in.grip[0] = r.in.grip[1] = 0.0f;
		r.Step();
		// a hand that holds the weapon enters the left shoulder: told once, with the reason; its squeeze does nothing and says so
		weight::g_holds[1] = true;
		g_log.clear();
		place(r.in, 1, -0.24f, -0.08f, 0.15f);
		r.Step();
		std::this_thread::sleep_for(std::chrono::milliseconds(520));   // (past the throttle: only the hand's coming in tells, not every sample it stays)
		r.Step();
		EXPECT(Logged("the left hand is behind the left shoulder") && Logged("it holds the weapon"));
		int told = 0;
		for (const std::string& l : g_log) told += l.find("is behind the left shoulder") != std::string::npos;
		EXPECT(told == 1);   // (once for the entry, not every sample)
		r.in.grip[1] = 1.0f;
		g_log.clear();
		r.Step();
		EXPECT(Logged("squeezed behind the left shoulder and nothing happened") && Logged("it holds the weapon") && !shieldarmwire::Stowed());
		// a free hand: told what its squeeze will do - and then it does it
		weight::g_holds[1] = false;
		r.in.grip[1] = 0.0f;
		place(r.in, 1, -0.2f, -0.4f, -0.3f);
		r.Step();
		std::this_thread::sleep_for(std::chrono::milliseconds(520));
		g_log.clear();
		place(r.in, 1, -0.24f, -0.08f, 0.15f);
		r.Step();
		EXPECT(Logged("squeeze its grip, or carry the shield here with the grip held and let go, to put the shield away"));
		r.in.grip[1] = 1.0f;
		r.Step();
		EXPECT(shieldarmwire::Stowed());
		// a squeeze elsewhere: told with where the hand was, once in 1.5 s; behind the right shoulder it says that one is the weapon's
		r.in.grip[1] = 0.0f;
		r.in.grip[0] = 0.0f;
		place(r.in, 1, -0.2f, -0.4f, -0.3f);
		r.Step();
		g_log.clear();
		place(r.in, 0, 0.24f, -0.08f, 0.15f);
		r.Step();
		weight::g_holds[0] = true;   // (its squeeze is not the shield's: the right shoulder is the weapon's)
		r.in.grip[0] = 1.0f;
		r.Step();
		EXPECT(Logged("the right hand squeezed its grip at x 0.24") && Logged("behind the RIGHT shoulder"));
		g_log.clear();
		r.in.grip[0] = 0.0f;
		r.Step();
		r.in.grip[0] = 1.0f;
		r.Step();
		EXPECT(!Logged("squeezed its grip"));   // (the same hand again within 1.5 s: not told again)
		weight::g_holds[0] = false;
	}

	// no wielder source registered: the shield stays on the left arm
	{
		shieldarmwire::SetWielderSource(nullptr);
		Rig r;
		g_wielding = 2;
		EXPECT(r.Step() == 1);
		shieldarmwire::SetWielderSource(&Wielder);
	}

	shieldarmwire::SetLog(nullptr);
	if (g_fails) {
		std::printf("shield_arm_wire_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("shield_arm_wire_test: the shield jumps when the weapon is alone in its hand, stays while it is away or in both hands, one step a head sample, the box off: all passed\n");
	return 0;
}
