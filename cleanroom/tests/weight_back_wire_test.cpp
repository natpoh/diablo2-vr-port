// [weight] after_drop (the back, vanishes) end to end through vr/weight.cpp (the glue between the game's hands and the hold model), with real time for the steps and
// no skeleton, no game, no headset: a weapon let go falls, lies, goes to the back after the delay; who takes it from there (a hand behind a
// shoulder, a swap's claim on its grip, a hand with a potion, a hand in front), where it flies from, the swap gesture's word (BackStowed),
// to_back off, a new weapon, the box off.
//
//   weight_back_wire_test        (a few seconds: the steps are paced by the clock, as in the game)

#include "../../vr/weight.h"
#include "hold.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_fails = 0;
int g_pulses[2] = {};   // the haptic stand-in: pulses per hand (0 right, 1 left)
float g_lastAmp = 0.0f;
int g_lastMs = 0;
void Pulse(int side, float amplitude, int ms) {
	if (side >= 0 && side < 2) ++g_pulses[side];
	g_lastAmp = amplitude;
	g_lastMs = ms;
}

// the stand-ins of the throw gesture, the hero's animation mode and the log
bool g_throwOwned = true;
uint64_t g_lastThrowMs = 0;
int g_heroMode = -1;
std::vector<std::string> g_logLines;
bool ThrowOwned() { return g_throwOwned; }
uint64_t LastThrow() { return g_lastThrowMs; }
int HeroMode() { return g_heroMode; }
bool g_actionFrameOn = false;   // the game's animation has passed its action frame (its missile is up)
bool ActionFrame() { return g_actionFrameOn; }
void LogLine(const char* text) { g_logLines.emplace_back(text); }

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

struct Rig {
	skel::Input in, drawn;
	weight::Out out;
	uint32_t sample = 0;

	Rig() {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		Front();
		skel::Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)heft::hold::Kind::Staff;
		w.twoHanded = true;
		w.slot = 0;
		w.feel = 1.0f;
		w.strength = 1.0f;
		w.grabM = 0.12f;
		w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.tipAxis = 2;
		w.afterDrop = 1;
		w.stowAfterS = 0.05f;
		w.backLong = true;
	}
	void Front() {   // both hands in front of the chest (BodyWalk's frame: x right, y up, z back)
		const float r[3] = {0.2f, -0.4f, -0.3f}, l[3] = {-0.2f, -0.4f, -0.3f};
		for (int k = 0; k < 3; ++k) { in.hand[0][k] = r[k]; in.hand[1][k] = l[k]; }
	}
	void Behind(int side) {   // that hand behind its shoulder (the swap gesture's volume)
		const float p[3] = {side == 0 ? 0.25f : -0.25f, -0.1f, 0.15f};
		for (int k = 0; k < 3; ++k) in.hand[side][k] = p[k];
	}
	void Grip(int side, float v) { in.weight.rawGrip[side] = v; }
	void Step(int ms = 8) {
		Sleep(ms);
		in.sample = ++sample;
		skel::Input c = in;
		weight::Apply(&c, &out);
		drawn = c;   // the hands as the arms will be posed on them (moved onto the weapon where a hand holds it)
	}
	void Steps(int n) { while (n-- > 0) Step(); }
	// A step in which the right hand moves vz m/s along z for the real time since the last step (a swing at a known speed).
	void MoveStep(float vz, int ms = 8) {
		const auto now = std::chrono::steady_clock::now();
		if (!haveClock) { clock = now; haveClock = true; }
		Sleep(ms);
		const auto t1 = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(t1 - clock).count();
		clock = t1;
		in.hand[0][2] += vz * dt;
		Step(0);
	}
	std::chrono::steady_clock::time_point clock;
	bool haveClock = false;
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
		return !out.holds[0] && out.free;
	}
};

}   // namespace

int main() {
	// ---- a weapon alone in the game's LEFT slot is the weight model's too, in the left hand (the headset 2026-10-23: «оружие в левой руке - кукла прозрачная») ----
	{
		Rig r;
		r.in.weight.kind = (int)heft::hold::Kind::Sword;
		r.in.weight.twoHanded = false;
		r.in.weight.slot = 1;
		r.Off();
		r.Steps(3);
		std::printf("  a sword alone in the left slot: %s, slot %d, carried by the %s hand\n", r.out.on && weight::Placed() ? "held" : "NOT held", r.out.slot,
		            weight::CarryingHand() == 1 ? "left" : weight::CarryingHand() == 0 ? "right" : "no");
		EXPECT(r.out.on && weight::Placed() && r.out.slot == 1 && weight::HoldsWeapon(1) && weight::CarryingHand() == 1 && !weight::HoldsWeapon(0));
		r.in.weight.on = false;   // (forgotten before the staff below)
		r.Step();
	}
	// ---- falls, lies, goes to the back after the delay ----
	{
		Rig r;
		r.Off();
		r.Steps(3);
		EXPECT(r.out.on && weight::Placed() && weight::HoldsWeapon(0) && weight::CarryingHand() == 0 && !r.out.stowed);
		EXPECT(!weight::BackStowed());
		EXPECT(r.Drop());
		EXPECT(!r.out.stowed);   // it is falling, not yet on the back
		EXPECT(r.Until([&] { return r.out.stowed; }));
		EXPECT(r.out.stowed && r.out.free && !r.out.holds[0] && !r.out.holds[1] && std::isfinite(r.out.midM));
		EXPECT(weight::BackStowed() && weight::Placed() && weight::CarryingHand() == -1 && !weight::HoldsWeapon(0) && !weight::HoldsWeapon(1));

		// the skeleton tells where it drew it; it stays on the back (no hand takes it)
		const float back[3] = {0.05f, -0.30f, 0.20f}, backRot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		weight::SetBackBody(back, backRot);
		r.Steps(4);
		EXPECT(r.out.stowed && weight::BackStowed());

		// a hand in front of the chest with its grip squeezed takes nothing from the back (it is not the game's hand)
		r.Grip(1, 1.0f);
		r.Steps(4);
		EXPECT(r.out.stowed && !r.out.holds[1]);
		r.Grip(1, 0.0f);
		r.Steps(3);

		// a hand with a belt potion, behind the shoulder: nothing
		r.Behind(1);
		r.in.weight.busy[1] = true;
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(4);
		EXPECT(r.out.stowed);
		r.Grip(1, 0.0f);
		r.Steps(3);
		r.in.weight.busy[1] = false;

		// the left hand behind the left shoulder, its grip claimed by the swap gesture (that press is a take here): it flies into the left hand
		r.in.weight.claimed[1] = true;
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free && !r.out.holds[1]);
		EXPECT(weight::BackStowed());   // still the take's press for the swap gesture: not a swap, whichever of the two sees it first
		const float dx = r.out.body[0] - back[0], dy = r.out.body[1] - back[1], dz = r.out.body[2] - back[2];
		EXPECT(std::sqrt(dx * dx + dy * dy + dz * dz) < 0.08f);   // it left from where it was drawn
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(r.out.holds[1] && !r.out.free && r.out.main == 1 && weight::CarryingHand() == 1 && weight::HoldsWeapon(1) && !r.out.stowed);
		// a take on the back is a take for the left hand: the weapon is not teleported to the slot's hand ("grabs" bumped, not as placed)
		EXPECT(!r.out.asPlaced && r.out.grabs[1] >= 1);
		// the swap gesture's word runs out shortly after the take
		for (int i = 0; i < 60 && weight::BackStowed(); ++i) r.Step(10);
		EXPECT(!weight::BackStowed());

		// let it go again: it falls, lies, goes to the back again
		r.Grip(1, 0.0f);
		r.Step();
		EXPECT(!r.out.holds[1] && r.out.free);
		r.Front();
		EXPECT(r.Until([&] { return r.out.stowed; }));
		EXPECT(weight::BackStowed());

		// the game's (right) hand, anywhere but behind a shoulder, calls it back as from the floor: into the game's fist, as placed
		r.in.weight.claimed[1] = false;
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free);
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		EXPECT(r.out.asPlaced && r.out.main == 0 && weight::CarryingHand() == 0 && r.out.grabs[0] == 0);
	}

	// ---- the right hand behind the right shoulder takes it too (and a claim on the grip does not matter) ----
	{
		Rig r;
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }));
		r.Behind(0);
		r.in.weight.claimed[0] = true;
		r.Steps(3);
		r.Grip(0, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free);
		EXPECT(r.Until([&] { return r.out.holds[0] && !r.out.free; }, 200));
		EXPECT(r.out.asPlaced && weight::CarryingHand() == 0);
	}

	// ---- after_drop 2 (vanishes) is the same flow for the model; the tick at the take zone; recall_any_hand ----
	{
		Rig r;
		r.in.weight.afterDrop = 2;
		r.in.weight.recallAny = true;
		weight::SetHaptic(&Pulse);
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }));
		EXPECT(weight::BackStowed() && r.out.free && weight::CarryingHand() == -1);
		g_pulses[0] = g_pulses[1] = 0;
		r.Behind(1);   // takeHaptic is 0: no pulse
		r.Steps(4);
		EXPECT(g_pulses[0] == 0 && g_pulses[1] == 0);
		r.Front();
		r.Steps(3);
		r.in.weight.takeHaptic = 0.5f;
		r.Steps(60);   // (more than the tick's gap goes by)
		r.Behind(1);
		r.Steps(4);
		EXPECT(g_pulses[1] == 1 && g_pulses[0] == 0 && std::fabs(g_lastAmp - 0.5f) < 1e-4f && g_lastMs > 0);
		r.Steps(8);
		EXPECT(g_pulses[1] == 1);   // staying inside is silent
		r.Front();
		r.in.weight.busy[1] = true;   // a hand with a potion: silent
		r.Steps(60);
		r.Behind(1);
		r.Steps(4);
		EXPECT(g_pulses[1] == 1);
		r.in.weight.busy[1] = false;
		r.Front();
		r.Steps(60);
		r.Behind(0);   // the other hand, the right: its own tick
		r.Steps(4);
		EXPECT(g_pulses[0] == 1 && g_pulses[1] == 1);
		r.Front();
		r.Steps(3);
		// recall_any_hand: the left hand's grip in front of the chest calls it into the left hand
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free);
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 200));
		EXPECT(r.out.main == 1 && weight::CarryingHand() == 1);
		// ... and with it off the left hand there calls nothing (the game's hand and the shoulder take it)
		r.Grip(1, 0.0f);
		r.Step();
		EXPECT(r.Until([&] { return r.out.stowed; }));
		r.in.weight.recallAny = false;
		r.Grip(1, 1.0f);
		r.Steps(4);
		EXPECT(r.out.stowed);
		r.Grip(1, 0.0f);
		r.Steps(2);
		weight::SetHaptic(nullptr);
	}

	// ---- after_drop 0 (lies on the floor): nothing is away, nothing ticks, the shoulder takes nothing ----
	{
		Rig r;
		r.in.weight.afterDrop = 0;
		r.Off();
		EXPECT(r.Drop());
		bool ever = false;
		for (int i = 0; i < 160; ++i) { r.Step(); ever = ever || r.out.stowed || weight::BackStowed(); }
		EXPECT(!ever && r.out.free && !r.out.holds[0] && weight::CarryingHand() == -1);
		// ... and a hand behind a shoulder takes nothing off the floor from there (as before: the pick-up is at the shaft)
		r.Behind(1);
		r.Steps(3);
		r.Grip(1, 1.0f);
		r.Steps(4);
		EXPECT(!r.out.holds[1] && r.out.free);
	}

	// ---- weight::TakeZone with the weapon in the right hand: the left hand on its shaft is in the zone (a grip takes it as the second hand), a hand at the hip is not ----
	{
		Rig r;
		weight::SetLog(nullptr);
		r.Off();
		r.Steps(4);
		EXPECT(weight::Placed() && weight::HoldsWeapon(0) == true);
		float butt[3], tip[3];
		EXPECT(weight::ShaftEnds(butt, tip));
		const float mid[3] = {0.5f * (butt[0] + tip[0]), 0.5f * (butt[1] + tip[1]), 0.5f * (butt[2] + tip[2])};
		float cm = -1.0f;
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = mid[k];
		r.Steps(3);
		EXPECT(weight::TakeZone(1, &cm) == 1 && cm < 4.0f);            // the left fist on the shaft
		EXPECT(weight::TakeZone(0, &cm) == 0);                          // the hand that holds it: nothing to take
		r.in.hand[1][1] = mid[1] - 0.08f;
		r.Steps(3);
		EXPECT(weight::TakeZone(1, &cm) == 1 && cm > 5.0f && cm < 11.0f);   // 8 cm off the shaft: still inside the 12 cm
		r.in.hand[1][1] = mid[1] - 0.25f;
		r.Steps(3);
		EXPECT(weight::TakeZone(1, &cm) == 0 && cm > 18.0f);           // 25 cm off: no
		r.in.weight.on = false;                                          // the box off: the zones are cleared
		r.Step();
		EXPECT(weight::TakeZone(1, &cm) == 0);
		r.in.weight.on = true;
	}

	// ---- a throw: the release lets the model go and it leaves the hand like a drop (nothing is re-placed at once), is never stowed, and ends by itself:
	// the moment the game takes the throw (thrown_model 0), or when it lands / the game's animation is over / thrown_fly_ms ----
	{
		weight::SetThrowSource(&ThrowOwned, &LastThrow);
		weight::SetLog(&LogLine);
		weight::SetHeroModeSource(&HeroMode); weight::SetActionFrameSource(&ActionFrame);
		// modeSteps: from loop step `at` (counted from the release) the hero's mode is `mode`; lateThrowAt: the throw gesture's word comes at that step, not at the release
		struct ModeAt { int at; int mode; };
		struct Run {
			const char* what; int thrownModel; float flyMs; bool throwHappens; int lateThrowAt; ModeAt m[3]; const char* wantLog; bool wantStowed; ULONGLONG maxMs, minMs; float handoffMs = 0.0f; int actionFrameAt = -1;
			bool potion = false, potionFlies = true;   // a throwing potion in the hand, and [weight] potion_model_flies
		};
		const Run runs[] = {
			{"the game takes the throw: the model is gone at once", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the game took the throw", false, 450, 30},
			{"the game takes it BEFORE the gesture's word: gone at the word", 0, 3000.0f, true, 5, {{0, 11}, {-1, 0}, {-1, 0}}, "the game took the throw", false, 450, 30},
			{"the game does not take it (refused): the model flies until it lands", 0, 3000.0f, true, -1, {{0, 1}, {-1, 0}, {-1, 0}}, "it landed", false, 5000, 300},
			{"the game does not take it: thrown_fly_ms ends it", 0, 250.0f, true, -1, {{0, 1}, {-1, 0}, {-1, 0}}, "thrown_fly_ms ran out", false, 1000, 200},
			{"thrown_model 1: it flies on although the game took it, until it lands", 1, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "it landed", false, 5000, 300},
			{"thrown_model 1: the game's animation over ends it", 1, 3000.0f, true, -1, {{0, 1}, {4, 11}, {14, 1}}, "the game's throw animation is over", false, 900, 100},
			{"no throw: a plain drop - it goes to the back as any", 0, 3000.0f, false, -1, {{0, 1}, {-1, 0}, {-1, 0}}, nullptr, true, 100000, 0},
			{"handoff 350: the model flies out of the hand first, then hands over at the handoff time", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the handoff time", false, 800, 300, 350.0f, -1},
			{"handoff: the game's missile is up (the action frame) at ~120 ms: handed over there, before the handoff time", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "action frame", false, 340, 60, 350.0f, 8},
			{"handoff set but the game does not take the throw: no handoff, the flight is all there is", 0, 3000.0f, true, -1, {{0, 1}, {-1, 0}, {-1, 0}}, "it landed", false, 5000, 300, 350.0f, -1},
			// a throwing POTION with potion_model_flies: the game taking the throw (mode 11 at step 4), its action frame, its animation ending, the handoff time - none of them ends the model
			{"a POTION: the game takes the throw at once - the model flies on and breaks at its landing", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the thrown potion's model is done", false, 5000, 300, 0.0f, -1, true, true},
			{"a POTION, thrown_model 1 and the game's animation over at step 14: still flies to its landing", 1, 3000.0f, true, -1, {{0, 1}, {4, 11}, {14, 1}}, "the thrown potion's model is done", false, 5000, 300, 0.0f, -1, true, true},
			{"a POTION with a handoff time and the action frame: neither hands it over", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the thrown potion's model is done", false, 5000, 300, 350.0f, 8, true, true},
			{"a POTION the game does not take: it flies and breaks at the landing", 0, 3000.0f, true, -1, {{0, 1}, {-1, 0}, {-1, 0}}, "the thrown potion's model is done", false, 5000, 300, 0.0f, -1, true, true},
			{"a POTION, thrown_fly_ms 250: Timeout ends it", 0, 250.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "thrown_fly_ms ran out", false, 1000, 200, 0.0f, -1, true, true},
			{"a POTION with potion_model_flies OFF: as any thrown weapon - gone the moment the game takes the throw", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the game took the throw", false, 450, 30, 0.0f, -1, true, false},
			{"a javelin with potion_model_flies on is not a potion: gone at the take", 0, 3000.0f, true, -1, {{0, 1}, {4, 11}, {-1, 0}}, "the game took the throw", false, 450, 30, 0.0f, -1, false, true},
		};
		for (const Run& run : runs) {
			Rig r;
			r.in.weight.kind = (int)heft::hold::Kind::Javelin;
			r.in.weight.twoHanded = false;
			r.in.weight.afterDrop = 1;
			r.in.weight.thrownModel = run.thrownModel;
			r.in.weight.thrownFlyMs = run.flyMs;
			r.in.weight.thrownHandoffMs = run.handoffMs;
			r.in.weight.thrownPotion = run.potion;
			r.in.weight.potionFlies = run.potionFlies;
			g_actionFrameOn = false;
			g_logLines.clear();
			g_heroMode = 1;
			g_lastThrowMs = 0;
			r.Off();
			r.Steps(3);
			EXPECT(weight::HoldsWeapon(0));
			r.Grip(0, 1.0f);
			r.Steps(4);
			const ULONGLONG releaseAt = GetTickCount64();
			if (run.throwHappens && run.lateThrowAt < 0) g_lastThrowMs = releaseAt;
			g_heroMode = run.m[0].mode;
			r.Grip(0, 0.0f);
			r.Step();
			// let go AT the release, and not put back in the hand at once
			if (!(!r.out.holds[0] && r.out.free)) std::printf("  (%s: after the release holds %d free %d)\n", run.what, r.out.holds[0], r.out.free);
			EXPECT(!r.out.holds[0] && r.out.free);
			bool everStowed = false, fresh = false;
			for (int i = 1; i < 500 && !fresh; ++i) {
				if (run.lateThrowAt == i) g_lastThrowMs = GetTickCount64();
				if (run.actionFrameAt == i) g_actionFrameOn = true;
				for (const ModeAt& mm : run.m) if (mm.at == i) g_heroMode = mm.mode;
				r.Step();
				everStowed = everStowed || r.out.stowed;
				fresh = r.out.holds[0] && !r.out.free && r.out.asPlaced;   // a fresh one stands in the hand
				if (run.wantStowed && r.out.stowed) break;
			}
			const ULONGLONG tookMs = GetTickCount64() - releaseAt;
			if (run.wantStowed) {
				EXPECT(everStowed && !fresh);
			} else {
				if (!fresh || everStowed) std::printf("  (%s: fresh %d, ever stowed %d)\n", run.what, fresh, everStowed);
				EXPECT(fresh && !everStowed);
				if (!(tookMs >= run.minMs && tookMs <= run.maxMs)) std::printf("  (%s: the fresh one stood in the hand %llu ms after the release, wanted %llu..%llu)\n", run.what, tookMs, run.minMs, run.maxMs);
				EXPECT(tookMs >= run.minMs && tookMs <= run.maxMs);
				bool said = false;
				for (const std::string& l : g_logLines) said = said || (run.wantLog && l.find(run.wantLog) != std::string::npos);
				if (!said) { std::printf("  (%s: the log has no \"%s\":\n", run.what, run.wantLog); for (const std::string& l : g_logLines) std::printf("      %s\n", l.c_str()); }
				EXPECT(said);
			}
			// the throw never became a drop to the back: BackStowed (the swap's word) is not set by it
			if (!run.wantStowed) EXPECT(!weight::BackStowed());
		}
		weight::SetThrowSource(nullptr, nullptr);
		weight::SetHeroModeSource(nullptr); weight::SetActionFrameSource(nullptr);
		weight::SetLog(nullptr);
	}
	// ---- the hand stays EMPTY while the game throws (EmptyHand): the fresh weapon is held but NOT drawn (Out::hidden) until the game's animation is over ----
	{
		weight::SetThrowSource(&ThrowOwned, &LastThrow);
		weight::SetLog(&LogLine);
		weight::SetHeroModeSource(&HeroMode);
		struct Run { const char* what; float capMs; int modeAfter; int modeAfterAt; const char* wantEnd; ULONGLONG minMs, maxMs; bool wantHidden; };
		// modeAfter: from the step `modeAfterAt` after the game took the throw the hero's mode is this (-2: it stays in the throw mode)
		const Run runs[] = {
			{"the animation ends: drawn again a tail after it", 900.0f, 1, 20, "the game's throw animation is over", 250, 850, true},
			{"the mode signal is lost: the cap ends it", 300.0f, -1, 1, "thrown_hand_empty_ms ran out", 250, 700, true},
			{"the hero stays in the throw mode: the cap ends it", 400.0f, -2, 0, "thrown_hand_empty_ms ran out", 350, 800, true},
			{"cap 0: not hidden at all", 0.0f, 1, 20, nullptr, 0, 0, false},
		};
		for (const Run& run : runs) {
			Rig r;
			r.in.weight.kind = (int)heft::hold::Kind::Javelin;
			r.in.weight.twoHanded = false;
			r.in.weight.afterDrop = 1;
			r.in.weight.thrownModel = 0;
			r.in.weight.thrownFlyMs = 3000.0f;
			r.in.weight.thrownHandEmptyMs = run.capMs;
			r.in.weight.thrownHandoffMs = 0.0f;   // (these timelines start at the game taking the throw: no flight first)
			g_logLines.clear();
			g_heroMode = 1;
			g_lastThrowMs = 0;
			r.Off();
			r.Steps(3);
			r.Grip(0, 1.0f);
			r.Steps(4);
			g_lastThrowMs = GetTickCount64();
			r.Grip(0, 0.0f);
			r.Step();
			// the game takes the throw: the hero goes into the throw mode a few steps after the release
			bool fresh = false;
			for (int i = 1; i < 300 && !fresh; ++i) {
				if (i == 4) g_heroMode = 11;
				r.Step();
				fresh = r.out.holds[0] && !r.out.free && r.out.asPlaced;
			}
			EXPECT(fresh);
			EXPECT(r.out.hidden == run.wantHidden);   // a fresh one is held - and not drawn
			const ULONGLONG hiddenAt = GetTickCount64();
			ULONGLONG shownAt = 0;
			for (int i = 0; i < 400; ++i) {
				if (run.modeAfter != -2 && i == run.modeAfterAt) g_heroMode = run.modeAfter;
				r.Step();
				if (!r.out.hidden && !shownAt) { shownAt = GetTickCount64(); break; }
				EXPECT(r.out.holds[0] && !r.out.free);   // (held all along: only its drawing is off)
			}
			const ULONGLONG empty = shownAt ? shownAt - hiddenAt : 0;
			if (run.wantHidden) {
				if (!(shownAt && empty >= run.minMs && empty <= run.maxMs)) std::printf("  (%s: the hand was empty %llu ms, wanted %llu..%llu)\n", run.what, empty, run.minMs, run.maxMs);
				EXPECT(shownAt && empty >= run.minMs && empty <= run.maxMs);
				bool said = false, began = false;
				for (const std::string& l : g_logLines) {
					said = said || l.find(run.wantEnd) != std::string::npos;
					began = began || l.find("the hand stays empty until the game's throw animation is over") != std::string::npos;
				}
				if (!said || !began) { std::printf("  (%s: the log:\n", run.what); for (const std::string& l : g_logLines) std::printf("      %s\n", l.c_str()); }
				EXPECT(said && began);
			} else {
				EXPECT(!r.out.hidden);
				for (const std::string& l : g_logLines) EXPECT(l.find("the hand stays empty") == std::string::npos);
			}
		}
		weight::SetThrowSource(nullptr, nullptr);
		weight::SetHeroModeSource(nullptr);
		weight::SetLog(nullptr);
	}

	// ---- weight::LastThrow: the throw as the weight model works it out (preview in the hand, frozen at the release, scaled by the reach, held by the cap) ----
	{
		weight::SetThrowSource(&ThrowOwned, &LastThrow);
		weight::SetLog(&LogLine);
		weight::SetHeroModeSource(&HeroMode);
		struct Case { const char* what; float speed, reachPct, maxCells; float wantMin, wantMax; bool wantCapped; };
		const Case cases[] = {
			{"a comfortable 4 m/s at 800 %", 4.0f, 800.0f, 48.0f, 11.0f, 24.0f, false},
			{"the same swing, a real arm (100 %)", 4.0f, 100.0f, 48.0f, 1.2f, 3.6f, false},
			{"a hard swing, 1500 % and ten cells: held by the cap", 8.0f, 1500.0f, 10.0f, 5.3f, 5.6f, true},
			{"a gentle 2 m/s at 800 %", 2.0f, 800.0f, 48.0f, 4.5f, 12.0f, false},
		};
		for (const Case& cs : cases) {
			Rig r;
			r.in.weight.kind = (int)heft::hold::Kind::Javelin;
			r.in.weight.twoHanded = false;
			r.in.weight.afterDrop = 1;
			r.in.weight.thrownModel = 1;   // the model flies on, so its own landing can be looked at too
			r.in.weight.thrownFlyMs = 5000.0f;
			r.in.weight.throwReachPct = cs.reachPct;
			r.in.weight.throwMaxCells = cs.maxCells;
			r.in.weight.unitsPerMetre = 3.64f;
			g_logLines.clear();
			g_heroMode = -1;
			g_lastThrowMs = 0;
			r.Off();
			r.Steps(3);
			weight::ThrowOutcome o;
			EXPECT(weight::HoldsWeapon(0));
			r.Grip(0, 1.0f);
			r.Steps(4);
			// the swing straight ahead (-z): up to the speed, then a few steps at it; the outcome is the preview all the while
			bool previewSeen = false;
			float v = 0.0f;
			for (int i = 0; i < 16; ++i) {
				v = std::fmin(cs.speed, v + cs.speed / 10.0f);
				r.MoveStep(-v);
				if (weight::LastThrow(&o) && o.preview && o.releaseTickMs == 0) previewSeen = true;
			}
			EXPECT(previewSeen);
			const ULONGLONG releaseAt = GetTickCount64();
			g_lastThrowMs = releaseAt;
			r.Grip(0, 0.0f);
			r.Step();
			EXPECT(!r.out.holds[0] && r.out.free);
			const bool got = weight::LastThrow(&o);
			if (!got || o.preview || !o.valid) std::printf("  (%s: valid %d preview %d)\n", cs.what, got, o.preview);
			EXPECT(got && !o.preview && o.valid && o.releaseTickMs >= releaseAt - 5 && o.releaseTickMs <= releaseAt + 100);
			const float dist = std::sqrt(o.landing[0] * o.landing[0] + o.landing[2] * o.landing[2]);
			if (!(dist >= cs.wantMin && dist <= cs.wantMax)) std::printf("  (%s: lands %.2f m out, wanted %.1f..%.1f; the peak %.2f m/s, flight %.2f s, scale %.1f)\n", cs.what, dist, cs.wantMin, cs.wantMax, o.speed, o.flightS, o.reachScale);
			EXPECT(dist >= cs.wantMin && dist <= cs.wantMax);
			EXPECT(o.capped == cs.wantCapped);
			EXPECT(o.landing[2] < 0.0f && std::fabs(o.landing[0]) < 0.25f * dist + 0.1f);   // straight ahead (-z in the head frame, z back)
			EXPECT(o.velocity[2] < -1.0f && o.flightS > 0.3f && o.flightS < 1.0f && o.massKg > 0.3f && o.massKg < 1.0f);
			EXPECT(std::fabs(o.landing[1] - (-1.7f + 0.02f)) < 0.03f);   // the ground, a radius under a head 1.7 m up (the room's floor here)
			EXPECT(o.rawVelocity[2] < -0.5f * cs.speed && o.rawVelocity[2] > -1.5f * cs.speed);   // the peak the hand gave it, unscaled
			// the model's own flight lands where the outcome says (to within a weapon's length): its middle when the fresh one takes its place
			float lastMid[3] = {};
			bool fresh = false;
			for (int i = 0; i < 600 && !fresh; ++i) {
				r.Step();
				fresh = r.out.holds[0] && !r.out.free && r.out.asPlaced;
				float butt[3], tip[3];
				if (!fresh && weight::ShaftEnds(butt, tip)) for (int k = 0; k < 3; ++k) lastMid[k] = 0.5f * (butt[k] + tip[k]);   // (the flying model's, not the fresh one's)
			}
			EXPECT(fresh);
			const float flown = std::sqrt((lastMid[0] - o.landing[0]) * (lastMid[0] - o.landing[0]) + (lastMid[2] - o.landing[2]) * (lastMid[2] - o.landing[2]));
			if (flown > 2.5f) std::printf("  (%s: the model's flight ended %.2f m from the outcome's landing: the last middle (%.2f %.2f %.2f), the outcome's landing (%.2f %.2f %.2f), velocity (%.2f %.2f %.2f))\n", cs.what, flown,
			                              lastMid[0], lastMid[1], lastMid[2], o.landing[0], o.landing[1], o.landing[2], o.velocity[0], o.velocity[1], o.velocity[2]);
			EXPECT(flown < 2.5f);
			bool said = false;
			for (const std::string& l : g_logLines) said = said || l.find("the throw, worked out") != std::string::npos;
			EXPECT(said);
		}
		// no thrown weapon in the hand: nothing to work out (a sword is not thrown)
		{
			Rig r;
			r.in.weight.kind = (int)heft::hold::Kind::Sword;
			r.in.weight.twoHanded = false;
			r.Off();
			r.Steps(3);
			r.Grip(0, 1.0f);
			for (int i = 0; i < 10; ++i) r.MoveStep(-4.0f);
			weight::ThrowOutcome before, after;
			weight::LastThrow(&before);   // (the last javelin's, kept as it was)
			for (int i = 0; i < 10; ++i) r.MoveStep(-4.0f);
			weight::LastThrow(&after);
			EXPECT(after.tickMs == before.tickMs && after.landing[2] == before.landing[2]);   // nothing is worked out for a sword
		}
		weight::SetThrowSource(nullptr, nullptr);
		weight::SetHeroModeSource(nullptr);
		weight::SetLog(nullptr);
	}

	// ---- the shaft as drawn (weight::SetDrawnShaft): the ends the weight model has are the blade's, on the drawn bone's axis ----
	{
		Rig r;
		r.in.weight.afterDrop = 0;
		r.Off();
		r.Steps(4);
		// the bone's line measured against the body (the first fist's frame; here the controller, as placed): 1 m ahead of the fist along -z is the tip side
		// of a short staff (0.66 m each way of the bone, the bone 0.1 m ahead of where the line passes the fist)
		const float at[3] = {0.0f, 0.0f, 0.0f}, dir[3] = {0.0f, 0.0f, -1.0f};
		weight::SetDrawnShaft(at, dir, 0.10f - 0.66f, 0.10f + 0.66f, 0.10f);
		r.Steps(4);
		float butt[3], tip[3];
		EXPECT(weight::ShaftEnds(butt, tip));
		const float handX = r.in.hand[0][0], handY = r.in.hand[0][1], handZ = r.in.hand[0][2];
		auto closeTo = [](float a, float b) { return std::fabs(a - b) < 0.015f; };
		EXPECT(closeTo(butt[0], handX) && closeTo(butt[1], handY) && closeTo(butt[2], handZ + 0.56f));   // the butt 0.56 m behind the fist (z back)
		EXPECT(closeTo(tip[0], handX) && closeTo(tip[1], handY) && closeTo(tip[2], handZ - 0.76f));      // the tip 0.76 m ahead of it
		EXPECT(closeTo(std::sqrt((tip[0] - butt[0]) * (tip[0] - butt[0]) + (tip[1] - butt[1]) * (tip[1] - butt[1]) + (tip[2] - butt[2]) * (tip[2] - butt[2])), 1.32f));   // the wood: 1.32 m
		// the same line again with a different wood: refitted (the item changed its extents, the sliders moved)
		weight::SetDrawnShaft(at, dir, -0.5f, 1.0f, 0.10f);
		r.Steps(4);
		EXPECT(weight::ShaftEnds(butt, tip));
		EXPECT(closeTo(butt[2], handZ + 0.5f) && closeTo(tip[2], handZ - 1.0f));
		// a measurement that is none changes nothing
		const float nan = std::nanf("");
		const float bad[3] = {nan, 0.0f, 0.0f};
		weight::SetDrawnShaft(bad, dir, -0.5f, 1.0f, 0.10f);
		const float zero[3] = {0.0f, 0.0f, 0.0f};
		weight::SetDrawnShaft(at, zero, -0.5f, 1.0f, 0.10f);
		r.Steps(3);
		EXPECT(weight::ShaftEnds(butt, tip) && closeTo(butt[2], handZ + 0.5f) && closeTo(tip[2], handZ - 1.0f));
	}

	// ---- the take zone AT the weapon on the back (back_take_cm): a fist near it takes it, outside the shoulder zone too; the haptic tick fires there; the log says why not ----
	{
		weight::SetLog(&LogLine);
		weight::SetHaptic(&Pulse);
		Rig r;
		r.in.weight.afterDrop = 1;
		r.in.weight.takeHaptic = 0.5f;
		r.in.weight.backTakeM = 0.25f;
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }));
		// the staff hangs low across the back, below the shoulder zone (the drawn place: told by the skeleton). A wood of 1.32 m on the diagonal.
		const float at[3] = {0.0f, 0.0f, 0.0f}, dirv[3] = {0.0f, 1.0f, 0.0f};
		const float back[3] = {0.05f, -0.55f, 0.22f};
		float rotq[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // the body's frame as it hangs (identity: the staff's line runs along its own +Y: up the back)
		weight::SetDrawnShaft(at, dirv, -0.66f, 0.66f, 0.0f);
		weight::SetBackBody(back, rotq);
		g_pulses[0] = g_pulses[1] = 0;
		r.Steps(6);
		float butt[3], tip[3];
		const bool ends = weight::ShaftEnds(butt, tip);
		EXPECT(ends);
		const float mid[3] = {0.5f * (butt[0] + tip[0]), 0.5f * (butt[1] + tip[1]), 0.5f * (butt[2] + tip[2])};
		// a hand 12 cm from the middle of the wood, at the height of the lower back: NOT inside the shoulder volume (its y floor is -0.38 from the eyes)
		const float hand[3] = {mid[0] - 0.12f, mid[1], mid[2]};
		EXPECT(hand[1] < -0.38f);
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = hand[k];
		g_logLines.clear();
		r.Steps(6);
		EXPECT(g_pulses[1] == 1 && g_pulses[0] == 0);   // the tick fired as the hand came into the zone at the weapon
		{   // the zone a grip would take it in (weight::TakeZone: the belt potions ask it): the back's, ~12 cm from the shaft
			float cm = -1.0f;
			EXPECT(weight::TakeZone(1, &cm) == 2 && cm > 8.0f && cm < 16.0f);
			EXPECT(weight::TakeZone(0, &cm) == 3 && cm > 30.0f);   // the game's (right) hand, in front of the chest, far from it: its grip would call the weapon back from afar
		}
		r.Grip(1, 1.0f);
		r.Step();
		EXPECT(!r.out.stowed && r.out.free);
		EXPECT(r.Until([&] { return r.out.holds[1] && !r.out.free; }, 250));
		EXPECT(r.out.main == 1);
		// back on the back: again, now a hand 60 cm away (and the left is not the game's hand, recall_any_hand off): nothing, and the log says how far
		r.Grip(1, 0.0f);
		r.Step();
		r.Front();
		EXPECT(r.Until([&] { return r.out.stowed; }));
		weight::SetDrawnShaft(at, dirv, -0.66f, 0.66f, 0.0f);
		weight::SetBackBody(back, rotq);
		r.Steps(6);
		EXPECT(weight::ShaftEnds(butt, tip));
		const float mid2[3] = {0.5f * (butt[0] + tip[0]), 0.5f * (butt[1] + tip[1]), 0.5f * (butt[2] + tip[2])};
		r.in.hand[1][0] = mid2[0] - 0.60f; r.in.hand[1][1] = mid2[1]; r.in.hand[1][2] = mid2[2];
		r.in.weight.recallAny = false;
		r.Steps(4);
		{   // 60 cm from the shaft: no zone
			float cm = -1.0f;
			EXPECT(weight::TakeZone(1, &cm) == 0 && cm > 50.0f);
		}
		g_logLines.clear();
		r.Grip(1, 1.0f);
		r.Steps(3);
		EXPECT(r.out.stowed);
		bool said = false;
		for (const std::string& l : g_logLines)
			said = said || (l.find("stowed weapon: the left hand is") != std::string::npos && l.find("cm from it, zone 25 cm") != std::string::npos && l.find("not taken") != std::string::npos);
		if (!said) { std::printf("  (the log:\n"); for (const std::string& l : g_logLines) std::printf("      %s\n", l.c_str()); }
		EXPECT(said);
		r.Grip(1, 0.0f);
		r.Steps(2);
		weight::SetLog(nullptr);
		weight::SetHaptic(nullptr);
	}

	// ---- [weight] grip_fit: what the skeleton hook did to a hand on the weapon is logged once per take (the next test sees why a hand did not sit on the shaft) ----
	{
		weight::SetLog(&LogLine);
		Rig r;
		r.in.weight.afterDrop = 0;
		r.Off();
		r.Steps(4);
		g_logLines.clear();
		const auto count = [&](const char* what) {
			int c = 0;
			for (const std::string& l : g_logLines) c += l.find(what) != std::string::npos ? 1 : 0;
			return c;
		};
		const auto last = [&]() { return g_logLines.empty() ? std::string() : g_logLines.back(); };
		weight::GripFitNote n;
		n.side = 1;
		n.taker = n.snapped = n.aligned = true;
		n.grabs = 1;
		n.offAxisCm = 3.4f;
		n.alignDeg = 22.0f;
		n.settled = false;
		weight::NoteGripFit(n);   // still easing onto the weapon: nothing yet
		EXPECT(count("grip fit") == 0);
		n.settled = true;
		weight::NoteGripFit(n);
		weight::NoteGripFit(n);
		weight::NoteGripFit(n);   // every pose says it: the log, once per take
		EXPECT(count("grip fit, the left hand takes the shaft") == 1);
		EXPECT(last().find("3.4 cm off the shaft's axis") != std::string::npos && last().find("22 deg off the shaft") != std::string::npos);
		EXPECT(last().find("corrections") == std::string::npos && last().find("short of it") == std::string::npos);
		n.grabs = 2;   // the next take of that hand
		weight::NoteGripFit(n);
		EXPECT(count("grip fit, the left hand takes the shaft") == 2);
		n.grabs = 3;
		n.missCm = 4.0f;   // the arm cannot reach
		n.tune[0] = 1.5f; n.tune[1] = -2.0f; n.tune[2] = 0.5f; n.tune[3] = 12.0f; n.tune[4] = -7.0f; n.tune[5] = 30.0f;
		weight::NoteGripFit(n);
		EXPECT(last().find("corrections 1.5 -2.0 0.5 cm, 12 -7 30 deg") != std::string::npos && last().find("4.0 cm short of it") != std::string::npos);
		// the hand the game holds it in: its base grip kept, told once too (only when something is asked of it)
		n.side = 0; n.taker = n.snapped = n.aligned = false; n.grabs = 0; n.missCm = 0.0f;
		weight::NoteGripFit(n);
		weight::NoteGripFit(n);
		EXPECT(count("the right hand (the game's grip, kept)") == 1);
		// a bogus side is ignored
		const size_t before = g_logLines.size();
		n.side = 7;
		weight::NoteGripFit(n);
		EXPECT(g_logLines.size() == before);
		// the weapon is forgotten (the box off a moment) and placed again: the same take is told afresh
		r.Off();
		r.Steps(4);
		g_logLines.clear();
		n.side = 1; n.taker = n.snapped = n.aligned = true; n.grabs = 3;
		weight::NoteGripFit(n);
		EXPECT(count("grip fit, the left hand takes the shaft") == 1);
		// a pose the skeleton hook could not measure the drawn shaft in: told once for the reason, and again when it measures
		g_logLines.clear();
		weight::NoteShaftUnmeasured("the blade's extents are not known");
		weight::NoteShaftUnmeasured("the blade's extents are not known");
		EXPECT(count("the drawn shaft is NOT measured: the blade's extents are not known") == 1);
		const float at[3] = {0.0f, 0.0f, 0.0f}, dir[3] = {0.0f, 0.0f, -1.0f};
		weight::SetDrawnShaft(at, dir, -0.66f, 0.66f, 0.0f);
		weight::SetDrawnShaft(at, dir, -0.66f, 0.66f, 0.0f);
		EXPECT(count("the drawn shaft is measured again") == 1);
		weight::SetLog(nullptr);
	}

	// ---- the floor: the game's ground under the hero, not the room's (a staff lies on the visible ground, whatever the player's height) ----
	{
		struct Case { float headRoomY, aboveGround; float wantFloorRel; const char* what; };
		// floorRel = where the ground is from the head: -aboveGround (the lie height of a staff, 4 cm, is added); unknown: the room's floor, -headRoomY
		const Case cases[] = {
			{1.7f, 1.6f, -1.6f, "standing, the head 1.6 m over the ground"},
			{1.3f, 1.6f, -1.6f, "a player 30 cm shorter than the setting: the ground is 30 cm under the room's floor"},
			{1.1f, 1.6f, -1.6f, "seated"},
			{1.9f, 1.6f, -1.6f, "a tall player"},
			{1.7f, 0.0f, -1.7f, "no reading of the ground: the room's floor, as before"},
		};
		for (const Case& cs : cases) {
			Rig r;
			r.in.weight.afterDrop = 0;
			r.in.headRoom[1] = cs.headRoomY;
			r.in.weight.headAboveGroundM = cs.aboveGround;
			r.Off();
			EXPECT(r.Drop());
			r.Steps(220);
			float butt[3], tip[3];
			EXPECT(weight::ShaftEnds(butt, tip));
			const float lie = 0.04f;   // a staff's thickest piece, its knob: the axis lies a radius over the ground
			// the lower end rests a radius over the ground, the other at most a few degrees up (it came down "flat" within 3 degrees): 9 cm of a 1.75 m staff
			const float low = std::fmin(butt[1], tip[1]), high = std::fmax(butt[1], tip[1]), want = cs.wantFloorRel + lie;
			const bool ok = low >= want - 0.02f && low <= want + 0.02f && high <= want + 0.10f;
			if (!ok) std::printf("  (%s: the ends are %.3f and %.3f m from the head, wanted the lower at %.3f)\n", cs.what, butt[1], tip[1], want);
			EXPECT(ok);
		}
	}

	// ---- a new weapon while it hangs on the back: the back is forgotten ----
	{
		Rig r;
		r.Off();
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }));
		r.in.weight.kind = (int)heft::hold::Kind::Sword;   // the weapon swapped
		r.in.weight.twoHanded = false;
		r.Steps(2);
		EXPECT(!r.out.stowed && !r.out.free && r.out.holds[0] && !weight::BackStowed());
		// the box off: nothing is stowed, nothing is claimed
		EXPECT(r.Drop());
		EXPECT(r.Until([&] { return r.out.stowed; }));
		r.in.weight.on = false;
		r.Step();
		EXPECT(!r.out.on && !r.out.stowed && !weight::BackStowed() && !weight::Placed());
	}

	// ---- [weight] gravity_pct / damping_pct / gravity_feel reach the model: a sword held out level sinks by them ----
	{
		// the hand's turn about the horizontal, as drawn (the hand turns with the weapon): the weapon's sag, degrees
		auto sinks = [&](float gravity, float damping, bool feel, float strength = 1.0f) {
			Rig r;
			r.in.weight.kind = (int)heft::hold::Kind::Sword;
			r.in.weight.twoHanded = false;
			r.in.weight.gravity = gravity;
			r.in.weight.damping = damping;
			r.in.weight.gravityFeel = feel;
			r.in.weight.strength = strength;
			r.Off();
			r.Steps(150);   // a second and more of real time: it has settled
			const float* q = r.drawn.rot[0];
			return std::fabs(2.0 * std::asin(std::fmax(-1.0f, std::fmin(1.0f, q[0])))) * 180.0 / 3.14159265358979;
		};
		const double none = sinks(0.0f, 1.0f, true), real = sinks(1.0f, 1.0f, true), three = sinks(3.0f, 1.0f, true), old = sinks(1.0f, 1.0f, false);
		const double oldThree = sinks(3.0f, 1.0f, false), strong = sinks(1.0f, 1.0f, true, 2.0f);
		std::printf("  a sword held out level sinks: gravity 0 %.2f deg, 100 %% %.2f, 300 %% %.2f; the old wrists %.2f (300 %%: %.2f); strength 200 %% %.2f\n", none, real, three, old, oldThree, strong);
		EXPECT(none < 0.1);              // no gravity, no sag
		EXPECT(real > 1.5 && real < 8.0);   // the soft wrists let the weight sag it a few degrees
		EXPECT(three > 1.6 * real);      // and gravity_pct scales it
		EXPECT(old < 1.2 && old < 0.5 * real);   // gravity_feel off: the old stiff wrists
		EXPECT(oldThree > 1.6 * old);    // gravity_pct works with them too
		EXPECT(strong < 0.8 * real);     // stronger hands hold it better
	}

	// ---- the aim of a staff comes from the controllers (vr/aim_source.h): the shaft as the hand grips it does not move with the drawn weapon's sag or lag ----
	{
		struct Aimed {
			bool ok = false;
			weight::AimInfo a;
			double sag = 0;   // how far the drawn hand has turned off the controller about the horizontal, degrees
		};
		auto aimed = [&](float gravity, bool shake) {
			Rig r;
			r.in.weight.gravity = gravity;
			r.in.weight.gravityFeel = true;
			r.Off();
			r.Steps(4);
			// the staff as the grip has it: ahead of the controller, tilted a little down and to the side (the bone's line against the body = the controller, as placed)
			const float at[3] = {0.0f, 0.0f, 0.0f}, tilt[3] = {0.2f, -0.3f, -0.9327378f};
			weight::SetDrawnShaft(at, tilt, -0.5f, 1.0f, 0.10f);
			r.Steps(100);
			float q[4] = {0, 0, 0, 1};
			if (shake)   // the controller thrown about: up and down, side to side, and turned both ways, hard
				for (int i = 0; i < 70; ++i) {
					r.in.hand[0][0] = 0.2f + 0.25f * std::sin(i * 0.37f);
					r.in.hand[0][1] = -0.4f + 0.3f * std::sin(i * 0.5f);
					const float a = 0.7f * std::sin(i * 0.45f);
					q[0] = std::sin(a * 0.5f); q[3] = std::cos(a * 0.5f);
					for (int k = 0; k < 4; ++k) r.in.rot[0][k] = q[k];
					r.Step();
				}
			Aimed o;
			o.ok = weight::Aim(&o.a);
			if (!shake) o.sag = std::fabs(2.0 * std::asin(std::fmax(-1.0f, std::fmin(1.0f, r.drawn.rot[0][0])))) * 180.0 / 3.14159265358979;
			return o;
		};
		const Aimed none = aimed(0.0f, false), sag = aimed(3.0f, false), shaken = aimed(1.0f, true), shakenHeavy = aimed(3.0f, true);
		std::printf("  a staff in the right hand, the shaft as the hand grips it: %.3f %.3f %.3f; the drawn staff sags %.1f deg at 300 %% gravity, none at 0 %%\n", none.a.shaft[0],
		            none.a.shaft[1], none.a.shaft[2], sag.sag);
		EXPECT(none.ok && none.a.hand == 0 && none.a.holds[0] && !none.a.holds[1]);   // the right hand carries it, the left does not
		EXPECT(sag.sag > 4.0 && none.sag < 0.1);                                      // (the drawn weapon does sag with gravity)
		auto same = [](const weight::AimInfo& x, const weight::AimInfo& y) {
			return std::fabs(x.shaft[0] - y.shaft[0]) < 1e-3f && std::fabs(x.shaft[1] - y.shaft[1]) < 1e-3f && std::fabs(x.shaft[2] - y.shaft[2]) < 1e-3f;
		};
		EXPECT(sag.ok && same(none.a, sag.a));            // gravity sags the drawn staff, the aim axis stays the grip's
		EXPECT(shaken.ok && same(none.a, shaken.a));      // the hand thrown about, the staff lagging behind it: the same
		EXPECT(shakenHeavy.ok && same(none.a, shakenHeavy.a));
		const float len = std::sqrt(none.a.shaft[0] * none.a.shaft[0] + none.a.shaft[1] * none.a.shaft[1] + none.a.shaft[2] * none.a.shaft[2]);
		EXPECT(std::fabs(len - 1.0f) < 1e-3f);
		EXPECT(std::fabs(none.a.shaft[0] - 0.2f) < 2e-3f && std::fabs(none.a.shaft[1] + 0.3f) < 2e-3f && std::fabs(none.a.shaft[2] + 0.9327f) < 2e-3f);   // it IS the grip's line
		// let go (both grips): no hand carries it, nothing aims along it
		Rig r;
		r.Off();
		r.Steps(5);
		EXPECT(weight::Aim(nullptr));
		EXPECT(r.Drop());
		weight::AimInfo gone;
		EXPECT(!weight::Aim(&gone) && gone.hand == -1 && !gone.holds[0] && !gone.holds[1]);
		r.in.weight.on = false;   // the box off: nothing is aimed by it
		r.Step();
		EXPECT(!weight::Aim(nullptr));
	}

	if (g_fails) {
		std::printf("weight_back_wire_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("weight_back_wire_test: falls and lies, the delay, who takes it from the back, where it flies from, the swap's word, to_back off, a new weapon, the box off, gravity_pct / damping_pct / gravity_feel: all passed\n");
	return 0;
}
