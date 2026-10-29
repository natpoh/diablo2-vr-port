// [hands] shoulder_draw end to end through vr/weight.cpp (the glue between the game's hands and the hold model), with real time for the steps and no skeleton, no game,
// no headset: a weapon let go lies where it fell (never to the back, however long), no grip calls it back from afar, the shoulder of its side draws it into the hand that
// reached there - the game's hand as the game holds it, the other as a cross draw - and the tick in the hand that enters that shoulder's zone while it is away.
//
//   weight_draw_wire_test        (a few seconds: the steps are paced by the clock, as in the game)

#include "../../vr/shield_arm_wire.h"
#include "../../vr/weight.h"
#include "hold.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_fails = 0;
int g_pulses[2] = {};   // the haptic stand-in: pulses per hand (0 right, 1 left)
void Pulse(int side, float, int) {
	if (side >= 0 && side < 2) ++g_pulses[side];
}
std::vector<std::string> g_logLines;
void LogLine(const char* text) { g_logLines.emplace_back(text); }
bool Logged(const char* what) {
	for (const std::string& l : g_logLines)
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
	weight::Out out;
	uint32_t sample = 0;
	bool shield = false;   // a shield in the left slot, handled by [hands] shoulder_draw (vr/shield_arm_wire.cpp, stepped before the weight model as the skeleton hook does)
	int arm = -1;          // ... and the arm it is on after the last step

	explicit Rig(bool draw) {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		Front();
		skel::Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)heft::hold::Kind::Sword;
		w.twoHanded = false;
		w.slot = 0;
		w.feel = 1.0f;
		w.strength = 1.0f;
		w.grabM = 0.12f;
		w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.tipAxis = 2;
		w.afterDrop = 1;
		w.stowAfterS = 0.05f;   // (the legacy back's delay: not used with the draw)
		w.drawStowS = 1000.0f;  // (lies on the floor unless a block asks for the back)
		w.takeHaptic = 0.5f;
		w.drawMode = draw;
	}
	void Front() {   // both hands in front of the chest (BodyWalk's frame: x right, y up, z back)
		const float r[3] = {0.2f, -0.4f, -0.3f}, l[3] = {-0.2f, -0.4f, -0.3f};
		for (int k = 0; k < 3; ++k) { in.hand[0][k] = r[k]; in.hand[1][k] = l[k]; }
	}
	void At(int hand, float x) {   // that hand behind the shoulder at x (+ the right one, - the left one)
		const float p[3] = {x, -0.1f, 0.15f};
		for (int k = 0; k < 3; ++k) in.hand[hand][k] = p[k];
	}
	void Grip(int side, float v) { in.weight.rawGrip[side] = v; }
	void Step(int ms = 8) {
		Sleep(ms);
		in.sample = ++sample;
		skel::Input c = in;
		if (shield) {
			c.shieldHolster = true;
			shieldarmwire::Apply(&c);
			arm = c.shieldArm;
		}
		weight::Apply(&c, &out);
	}
	void Steps(int n) { while (n-- > 0) Step(); }
	template <class F> bool Until(F done, int maxSteps = 700) {
		for (int i = 0; i < maxSteps; ++i) {
			Step();
			if (done()) return true;
		}
		return false;
	}
	void Off() {   // the box off for a moment: the weight model forgets the weapon
		in.weight.on = false;
		Step();
		in.weight.on = true;
		Grip(0, 0.0f);
		Grip(1, 0.0f);
		Front();
		in.weight.busy[0] = in.weight.busy[1] = false;
		in.weight.claimed[0] = in.weight.claimed[1] = false;
	}
	// A new weapon sits in the right fist; its grip is squeezed and let go: both hands are off it, it falls, lands and lies.
	bool Drop() {
		Steps(3);
		Grip(0, 1.0f);
		Steps(3);
		Grip(0, 0.0f);
		Step();
		const bool let = !out.holds[0] && out.free;
		Steps(120);   // (a metre and a half of fall takes about half a second)
		return let;
	}
};

}   // namespace

int main() {
	weight::SetHaptic(&Pulse);
	weight::SetLog(&LogLine);

	// ---- it lies where it fell; a claimed grip calls nothing; either shoulder draws it ----
	{
		Rig r(true);
		r.Off();
		r.Steps(3);
		EXPECT(r.out.on && weight::Placed() && weight::HoldsWeapon(0) && weight::CarryingHand() == 0 && !r.out.stowed);
		EXPECT(r.Drop());
		r.Steps(120);   // far more than the 50 ms of the back's delay
		EXPECT(r.out.free && !r.out.stowed && !r.out.holds[0] && !r.out.holds[1] && weight::CarryingHand() == -1 && !weight::BackStowed());

		// a grip that is a zone's claim (the swap's old claim, a hand behind a shoulder) recalls nothing: the right hand's, and the left's
		r.in.weight.claimed[0] = true;
		r.Grip(0, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.in.weight.claimed[0] = false;
		r.Steps(3);
		r.in.weight.claimed[1] = true;
		r.Grip(1, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[1]);
		r.Grip(1, 0.0f);
		r.in.weight.claimed[1] = false;
		r.Steps(3);

		// the left hand enters the LEFT shoulder's zone: a tick (either shoulder holds the weapon); its grip pressed there draws it into the left hand
		g_pulses[0] = g_pulses[1] = 0;
		g_logLines.clear();
		r.At(1, -0.25f);
		r.Steps(4);
		EXPECT(g_pulses[1] == 1 && g_pulses[0] == 0);
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(weight::WieldingHands() == 2);   // (flying into the left hand: the shield follows at once)
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 1 && weight::WieldingHands() == 2);
		EXPECT(Logged("the left hand draws it from behind the left shoulder") && !Logged("cross draw"));
		r.Grip(1, 0.0f);
		r.Front();
		EXPECT(r.Until([&] { return r.out.free && !r.out.holds[1]; }, 100));
		EXPECT(weight::WieldingHands() == 0);   // let go: nobody wields it
		r.Steps(120);

		// the right hand enters the right shoulder's zone: one tick, silent while it stays; its grip pressed there draws the weapon (the zone's claim on the grip does not matter)
		g_logLines.clear();
		g_pulses[0] = g_pulses[1] = 0;
		r.At(0, 0.25f);
		r.Steps(4);
		EXPECT(g_pulses[0] == 1 && g_pulses[1] == 0);
		r.Steps(8);
		EXPECT(g_pulses[0] == 1);
		r.in.weight.claimed[0] = true;
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(!r.out.holds[0] && !r.out.stowed);
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		EXPECT(r.out.asPlaced && r.out.main == 0 && weight::CarryingHand() == 0 && r.out.grabs[0] == 0);
		EXPECT(Logged("the right hand draws it from behind the right shoulder") && weight::WieldingHands() == 1);
		EXPECT(!Logged("calls it back"));

		// let go again: it falls and lies; the cross draw - the LEFT hand behind the RIGHT shoulder - takes it into the left hand
		r.Grip(0, 0.0f);
		r.in.weight.claimed[0] = false;
		r.Front();
		r.Steps(3);
		EXPECT(r.Until([&] { return r.out.free && !r.out.holds[0]; }, 100));
		r.Steps(120);
		EXPECT(r.out.free && !r.out.stowed);
		g_logLines.clear();
		g_pulses[0] = g_pulses[1] = 0;
		r.At(1, 0.25f);
		r.Steps(4);
		EXPECT(g_pulses[1] == 1 && g_pulses[0] == 0);   // the tick is in the hand that entered
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(r.out.main == 1 && weight::CarryingHand() == 1 && weight::HoldsWeapon(1) && !weight::HoldsWeapon(0) && !r.out.asPlaced && r.out.grabs[1] >= 1);
		EXPECT(Logged("the left hand draws it from behind the right shoulder (a cross draw)") && weight::WieldingHands() == 2);
		// hit by touch follows the carrying hand: CarryingHand() is 1 (weight_drive_test covers the rest)

		// the weapon is in a hand: the other hand at a shoulder neither ticks nor draws it away
		g_pulses[0] = g_pulses[1] = 0;
		r.Front();
		r.Steps(60);
		r.At(0, 0.25f);
		r.Steps(4);
		r.Grip(0, 1.0f);
		r.Steps(8);
		EXPECT(g_pulses[0] == 0 && g_pulses[1] == 0);
		EXPECT(r.out.holds[1] && !r.out.holds[0] && weight::CarryingHand() == 1);
		r.Grip(0, 0.0f);
		r.Grip(1, 0.0f);
		r.Steps(3);
	}

	// ---- the grip of EITHER hand, pressed anywhere, calls a weapon that lies back into that hand (it does not go to the back for 1000 s here) ----
	{
		Rig r(true);
		r.Off();
		EXPECT(r.Drop());
		g_logLines.clear();
		r.Grip(0, 1.0f);   // the game's hand, in front of the chest, far from the weapon on the floor
		r.Step();
		EXPECT(weight::WieldingHands() == 1);   // flying into the right hand
		EXPECT(Logged("the right grip calls it back from where it lay") && !Logged("draws it"));
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 0 && r.out.asPlaced);
		// let go: it falls and lies again; now the LEFT hand's grip calls it into the left hand
		r.Grip(0, 0.0f);
		r.Steps(3);
		EXPECT(r.Until([&] { return r.out.free && !r.out.holds[0]; }, 100));
		r.Steps(100);
		g_logLines.clear();
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(weight::WieldingHands() == 2 && Logged("the left grip calls it back from where it lay"));
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 1 && r.out.main == 1 && !r.out.asPlaced);
		r.Grip(1, 0.0f);
		r.Steps(3);
		EXPECT(r.Until([&] { return r.out.free && !r.out.holds[1]; }, 100));
		r.Steps(100);
		// a hand with a potion, or a grip that is a zone's claim, calls nothing
		r.in.weight.busy[1] = true;
		r.Grip(1, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[1]);
		r.Grip(1, 0.0f);
		r.in.weight.busy[1] = false;
		r.Steps(3);
		r.in.weight.claimed[0] = true;
		r.Grip(0, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.in.weight.claimed[0] = false;
		r.Steps(3);
		// a hand AT the weapon where it lies takes it up there (no call): the weapon stays where it was and the hand closes on it
		float b[3], t[3];
		EXPECT(weight::ShaftEnds(b, t));
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = 0.5f * (b[k] + t[k]);
		r.Steps(3);
		g_logLines.clear();
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1]; }, 50) && !Logged("calls it back") && !r.out.free);
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);
	}

	// ---- it lies stowAfter seconds, then goes to the back; from then on only a hand behind a shoulder (or at it) takes it ----
	{
		Rig r(true);
		r.in.weight.drawStowS = 0.4f;
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.out.free);
		EXPECT(r.Until([&] { return r.out.stowed; }, 200));
		EXPECT(weight::BackStowed() && r.out.free && Logged("now on the back"));
		// a grip pressed far from it, away from the shoulders: nothing, whichever hand
		r.Grip(0, 1.0f);
		r.Steps(10);
		EXPECT(r.out.stowed && !r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(10);
		EXPECT(r.out.stowed && !r.out.holds[1]);
		r.Grip(1, 0.0f);
		r.Steps(3);
		// the tick in a hand that enters a shoulder while it is on the back
		g_pulses[0] = g_pulses[1] = 0;
		r.At(1, -0.25f);
		r.Steps(4);
		EXPECT(g_pulses[1] == 1 && g_pulses[0] == 0);
		// the left hand behind the LEFT shoulder takes it off the back into the left hand
		g_logLines.clear();
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 1 && Logged("the left hand takes it behind the shoulder from the back"));
		// let go again; it lies, goes to the back again; the RIGHT hand behind the LEFT shoulder (a cross draw) takes it
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);
		EXPECT(r.Until([&] { return r.out.free && !r.out.holds[1]; }, 100));
		EXPECT(r.Until([&] { return r.out.stowed; }, 300));
		r.At(0, -0.25f);
		r.Steps(4);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 0);
		r.Grip(0, 0.0f);
		r.Front();
		r.Steps(3);
	}

	// ---- the left shoulder is the shield's (shoulder_draw_shield_stow): the weapon is drawn from the right one only ----
	{
		Rig r(true);
		r.in.weight.shieldZone = true;
		r.Off();
		EXPECT(r.Drop());
		g_pulses[0] = g_pulses[1] = 0;
		r.At(1, -0.25f);
		r.Steps(4);
		EXPECT(g_pulses[1] == 0);   // no tick: nothing of the weapon's is there
		g_logLines.clear();
		r.in.weight.claimed[1] = true;   // (the shoulder zone claims the grip, shield or weapon: shoulderwire)
		r.Grip(1, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[1]);
		EXPECT(Logged("the left shoulder is the shield's"));
		r.Grip(1, 0.0f);
		r.in.weight.claimed[1] = false;
		r.Front();
		r.Steps(3);
		r.At(1, 0.25f);   // the right shoulder: either hand draws it
		r.Steps(4);
		EXPECT(g_pulses[1] == 1);
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);
	}

	// ---- the world moves under the room: a weapon that lies stays where it is in the game's WORLD; the hero walking or turning does not carry it ----
	{
		const auto worldOf = [](const float p[3], const skel::Input::Weight& w, double out[3]) {   // a head-frame point in the world: eye + upm * (x * camR - z * camF), height from the eye
			for (int k = 0; k < 3; ++k) out[k] = (double)w.worldEye[k] + (double)w.worldUpm * ((double)p[0] * w.worldCamR[k] - (double)p[2] * w.worldCamF[k]);
			out[1] = (double)w.worldEye[1] + (double)w.worldUpm * (double)p[1];
		};
		const auto same = [](const double a[3], const double b[3], double e) { return std::fabs(a[0] - b[0]) < e && std::fabs(a[2] - b[2]) < e; };
		Rig r(true);
		r.Off();
		skel::Input::Weight& w = r.in.weight;
		w.worldOk = true;
		w.worldUpm = 3.5f;
		w.worldEye[0] = 100.0f; w.worldEye[1] = 5.0f; w.worldEye[2] = 100.0f;
		w.worldCamR[0] = 1.0f; w.worldCamR[1] = 0.0f; w.worldCamR[2] = 0.0f;
		w.worldCamF[0] = 0.0f; w.worldCamF[1] = 0.0f; w.worldCamF[2] = -1.0f;
		EXPECT(r.Drop());
		r.Steps(4);
		float b0[3], t0[3], b1[3], t1[3];
		EXPECT(weight::ShaftEnds(b0, t0));
		double wb0[3], wt0[3], wb1[3], wt1[3];
		worldOf(b0, w, wb0);
		worldOf(t0, w, wt0);
		// the hero walks 2 m forward (the camera moves in the world along camF, the head in the room stays): the weapon is 2 m BEHIND him now (z is back)
		for (int i = 0; i < 20; ++i) {
			w.worldEye[2] -= 3.5f * 2.0f / 20.0f;
			r.Step();
		}
		EXPECT(weight::ShaftEnds(b1, t1));
		std::printf("  walked 2 m: the butt moved %.3f %.3f %.3f in the head frame\n", b1[0] - b0[0], b1[1] - b0[1], b1[2] - b0[2]);
		EXPECT(std::fabs(b1[2] - b0[2] - 2.0f) < 0.03f && std::fabs(b1[0] - b0[0]) < 0.03f && std::fabs(b1[1] - b0[1]) < 0.01f);
		worldOf(b1, w, wb1);
		worldOf(t1, w, wt1);
		EXPECT(same(wb0, wb1, 0.15) && same(wt0, wt1, 0.15));   // the same world points (units of the game's world: 0.15 is 4 cm)
		// he turns 90 degrees (the sticks): the head frame's axes turn in the world, the room does not; the weapon stays in the world
		const float R0[3] = {w.worldCamR[0], w.worldCamR[1], w.worldCamR[2]}, F0[3] = {w.worldCamF[0], w.worldCamF[1], w.worldCamF[2]};
		for (int i = 1; i <= 18; ++i) {
			const float th = 1.5707963f * (float)i / 18.0f, c = std::cos(th), s = std::sin(th);
			w.worldCamR[0] = c * R0[0] + s * R0[2]; w.worldCamR[2] = -s * R0[0] + c * R0[2];
			w.worldCamF[0] = c * F0[0] + s * F0[2]; w.worldCamF[2] = -s * F0[0] + c * F0[2];
			r.Step();
		}
		EXPECT(weight::ShaftEnds(b1, t1));
		worldOf(b1, w, wb1);
		worldOf(t1, w, wt1);
		EXPECT(same(wb0, wb1, 0.2) && same(wt0, wt1, 0.2));
		// the head steps 0.5 m in the room (with the camera after it): the weapon does not move in the room, so it moves 0.5 m the other way in the head frame
		float c0[3];
		EXPECT(weight::ShaftEnds(c0, t1));
		for (int i = 0; i < 10; ++i) {
			r.in.headRoom[0] += 0.05f;
			for (int k = 0; k < 3; ++k) w.worldEye[k] += 0.05f * 3.5f * w.worldCamR[k];
			r.Step();
		}
		float c1[3];
		EXPECT(weight::ShaftEnds(c1, t1));
		EXPECT(std::fabs(c1[0] - c0[0] + 0.5f) < 0.03f && std::fabs(c1[2] - c0[2]) < 0.03f);
		// a jump (a new area, a teleport: more than 1.5 m in a sample) is not followed: the weapon stays where it is in the room
		float d0[3];
		EXPECT(weight::ShaftEnds(d0, t1));
		w.worldEye[0] += 40.0f;
		r.Steps(3);
		float d1[3];
		EXPECT(weight::ShaftEnds(d1, t1));
		EXPECT(std::fabs(d1[0] - d0[0]) < 0.03f && std::fabs(d1[2] - d0[2]) < 0.03f);
		// the camera's frame not known: it moves with the hero (and the log says so)
		g_logLines.clear();
		w.worldOk = false;
		r.Steps(3);
		float e0[3];
		EXPECT(weight::ShaftEnds(e0, t1));
		w.worldEye[2] -= 7.0f;
		r.Steps(3);
		float e1[3];
		EXPECT(weight::ShaftEnds(e1, t1));
		EXPECT(std::fabs(e1[2] - e0[2]) < 0.03f && Logged("not known: a weapon on the floor moves with the hero"));
		// a weapon in the hand is not touched by the world's move
		w.worldOk = true;
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		float h0[3];
		EXPECT(weight::ShaftEnds(h0, t1));
		for (int i = 0; i < 10; ++i) {
			w.worldEye[2] -= 0.5f;
			r.Step();
		}
		float h1[3];
		EXPECT(weight::ShaftEnds(h1, t1));
		EXPECT(std::fabs(h1[2] - h0[2]) < 0.06f && r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.Steps(3);
	}

	// ---- the shield and the follow tick: off, the hand that wears the shield cannot take the weapon; on, it takes it and the shield goes to the other arm ----
	{
		shieldarmwire::SetWielderSource(&weight::WieldingHands);
		Rig r(true);
		r.shield = true;
		r.in.shieldFollow = false;
		r.in.weight.otherBusy = true;
		r.Off();
		r.Steps(8);
		EXPECT(weight::WieldingHands() == 1 && r.arm == 1);
		EXPECT(r.Drop());
		float b[3], t[3];
		EXPECT(weight::ShaftEnds(b, t));
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = 0.5f * (b[k] + t[k]);   // the left hand, which wears the shield, on the weapon where it lies
		r.Steps(3);
		g_logLines.clear();
		r.Grip(1, 1.0f);
		r.Steps(10);
		EXPECT(r.out.free && !r.out.holds[1] && r.arm == 1);
		EXPECT(Logged("the hand wears the shield and the shield does not follow the sword"));
		r.Grip(1, 0.0f);
		r.Steps(3);
		r.in.shieldFollow = true;   // the tick on
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1]; }, 100));
		r.Steps(6);
		EXPECT(r.arm == 0 && weight::WieldingHands() == 2);   // the shield is on the right arm now
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);
		shieldarmwire::SetWielderSource(nullptr);
	}

	// ---- a hand with a potion in it draws nothing; a grip already held when the hand comes into the zone draws nothing ----
	{
		Rig r(true);
		r.Off();
		EXPECT(r.Drop());
		r.in.weight.busy[0] = true;
		r.At(0, 0.25f);
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Steps(6);
		EXPECT(r.out.free && !r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.in.weight.busy[0] = false;
		r.Front();
		r.Steps(60);
		r.in.weight.claimed[0] = true;   // (a grip in front of the chest would call the weapon back; here it is some other gesture's)
		r.Grip(0, 1.0f);   // squeezed in front of the chest ...
		r.Steps(3);
		r.At(0, 0.25f);    // ... and brought to the shoulder: no new press
		r.Steps(8);
		EXPECT(r.out.free && !r.out.holds[0]);
		r.Grip(0, 0.0f);
		r.Steps(4);
		r.Grip(0, 1.0f);   // a fresh press there draws it
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		r.Grip(0, 0.0f);
		r.Steps(3);
	}

	// ---- the grip let go during the flight: it falls again; and a box ticked off for a moment forgets the weapon ----
	{
		Rig r(true);
		r.Off();
		EXPECT(r.Drop());
		r.At(0, 0.25f);
		r.Steps(4);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(r.out.free);
		r.Grip(0, 0.0f);
		r.Steps(3);
		EXPECT(r.out.free && !r.out.holds[0]);
		r.Steps(120);
		EXPECT(r.out.free && !r.out.stowed);
	}

	// ---- the shield (shieldArm >= 0: [hands] shoulder_draw handles it): no hand is busy with it - the hand that draws the weapon is the one the shield leaves, so a cross draw
	// works with a shield equipped; not handled (-1), the old rule: the other hand is busy with the shield ----
	{
		Rig r(true);
		r.in.weight.otherBusy = true;   // the game's left slot holds a shield
		r.in.weight.shieldArm = 1;      // ... on the left forearm, and handled
		r.Off();
		EXPECT(r.Drop());
		r.At(1, 0.25f);   // the left hand (it wears the shield) at the right shoulder: draws the sword - the shield leaves it
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(weight::CarryingHand() == 1);
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);

		Rig q(true);
		q.in.weight.otherBusy = true;
		q.in.weight.shieldArm = -1;     // not handled: the old rule
		q.Off();
		EXPECT(q.Drop());
		q.At(1, 0.25f);
		q.Steps(3);
		q.Grip(1, 1.0f);
		q.Steps(6);
		EXPECT(q.out.free && !q.out.holds[1]);
		q.Grip(1, 0.0f);
		q.Front();
		q.Steps(3);
		q.At(0, 0.25f);   // the right hand is the weapon's: it draws it
		q.Steps(3);
		q.Grip(0, 1.0f);
		q.Step();
		EXPECT(q.Until([&] { return q.out.holds[0] && !q.out.free; }, 200));
		q.Grip(0, 0.0f);
		q.Steps(3);
	}

	// ---- a javelin is drawn into the game's hand only (the throw gesture reads that controller) ----
	{
		Rig r(true);
		r.in.weight.kind = (int)heft::hold::Kind::Javelin;
		r.Off();
		EXPECT(r.Drop());
		g_logLines.clear();
		r.At(1, 0.25f);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(6);
		EXPECT(r.out.free && !r.out.holds[1]);   // the cross draw of a javelin is refused ...
		EXPECT(Logged("drawn into the game's hand only"));
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(3);
		r.At(0, 0.25f);
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));   // ... the game's hand draws it
		r.Grip(0, 0.0f);
		r.Steps(3);
	}

	// ---- WEAPON AND SHIELD ARE ALWAYS ON OPPOSITE ARMS, however the weapon changes hands - every way, through the real weight model and the real shield wire ----
	{
		shieldarmwire::SetWielderSource(&weight::WieldingHands);
		Rig r(true);
		r.shield = true;
		r.in.weight.otherBusy = true;   // the game's left slot holds a shield
		const auto invariant = [&] { const unsigned m = weight::WieldingHands(); return r.arm == 0 || r.arm == 1 ? m != (1u << (unsigned)r.arm) : false; };
		const auto shaftPoint = [&](int hand, double along) {   // that hand's controller put on the weapon's shaft (`along` 0 butt .. 1 tip)
			float b[3], t[3];
			EXPECT(weight::ShaftEnds(b, t));
			for (int k = 0; k < 3; ++k) r.in.hand[hand][k] = b[k] + (float)along * (t[k] - b[k]);
		};
		const auto letGo = [&] {   // both grips open: it falls and lies
			r.Grip(0, 0.0f);
			r.Grip(1, 0.0f);
			r.Front();
			EXPECT(r.Until([&] { return r.out.free && !r.out.holds[0] && !r.out.holds[1]; }, 100));
			r.Steps(120);
		};

		// the game puts it in the right hand: the shield is on the left arm, as the game has it
		r.Off();
		r.Steps(8);
		EXPECT(weight::WieldingHands() == 1 && r.arm == 1 && invariant());
		// let go: the shield stays
		EXPECT(r.Drop());
		EXPECT(weight::WieldingHands() == 0 && r.arm == 1);
		// 1. drawn by the right hand, from the left shoulder (a cross draw by the hand that does not wear the shield): stays
		r.At(0, -0.25f);
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		r.Steps(5);
		EXPECT(r.arm == 1 && invariant());
		letGo();
		EXPECT(r.arm == 1);
		// 2. drawn by the LEFT hand (the shield's) from the right shoulder: the shield jumps to the right arm at once - while the weapon flies
		r.At(1, 0.25f);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(2);
		EXPECT(weight::WieldingHands() == 2 && r.arm == 0);
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		r.Steps(5);
		EXPECT(r.arm == 0 && invariant());
		letGo();
		EXPECT(r.arm == 0);   // let go: stays
		// 3. taken up off the floor by the RIGHT hand (it wears the shield now): the shield jumps back to the left arm
		shaftPoint(0, 0.5);
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Steps(2);
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		r.Steps(5);
		EXPECT(r.arm == 1 && weight::WieldingHands() == 1 && invariant());
		// 4. passed to the left hand (the shield's) by its grip on the shaft: both hands on it - the shield stays; the right lets go - the weapon is in the left hand alone: it jumps
		shaftPoint(1, 0.8);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(8);
		EXPECT(r.out.holds[0] && r.out.holds[1] && weight::WieldingHands() == 3 && r.arm == 1);
		r.Grip(0, 0.0f);
		r.Steps(8);
		EXPECT(!r.out.holds[0] && r.out.holds[1] && weight::WieldingHands() == 2 && r.arm == 0 && invariant());
		// 5. passed back to the right hand (it wears the shield now): the same
		shaftPoint(0, 0.8);
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Steps(8);
		EXPECT(r.out.holds[0] && r.out.holds[1] && r.arm == 0);
		r.Grip(1, 0.0f);
		r.Steps(8);
		EXPECT(r.out.holds[0] && !r.out.holds[1] && weight::WieldingHands() == 1 && r.arm == 1 && invariant());
		// 6. the game puts a weapon in the right hand again while the shield is on the right arm (a weapon swap): gone and back - the shield, which stayed, jumps to the left
		letGo();
		r.At(1, 0.25f);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		r.Steps(5);
		EXPECT(r.arm == 0);
		r.in.weight.on = false;   // the weapon gone from the weight model for a moment (a weapon swap)
		r.Step();
		r.in.weight.on = true;
		r.Grip(0, 0.0f);
		r.Grip(1, 0.0f);
		r.Front();
		r.Steps(8);
		EXPECT(weight::WieldingHands() == 1 && r.arm == 1 && invariant());   // the new weapon is placed in the right fist: the shield is on the left
		r.Steps(3);
		shieldarmwire::SetWielderSource(nullptr);
	}

	// ---- the control: shoulder_draw off, the old behaviour - to the back after the delay, called back by the game's hand ----
	{
		Rig r(false);
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }, 400));
		EXPECT(weight::BackStowed());
		r.Front();
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free);
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		r.Grip(0, 0.0f);
		r.Steps(3);
	}

	weight::SetHaptic(nullptr);
	weight::SetLog(nullptr);
	if (g_fails) {
		std::printf("weight_draw_wire_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("weight_draw_wire_test: lies where it fell, no recall, either shoulder draws (the game's hand, the cross draw), the hand that wields it, the tick, a held weapon / a potion draw nothing, a shield makes no hand busy, off = the old back: all passed\n");
	return 0;
}
