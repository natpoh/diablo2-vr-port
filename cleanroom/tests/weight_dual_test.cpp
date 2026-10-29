// A weapon in each hand through vr/weight.cpp (StepSecond), with a stand-in of the touch's monster bodies and of the doll: the headset 2026-10-23 - «когда два
// оружия в двух руках, они проходят сквозь куклу, а когда одно оружие - оно сопротивляется». Two swords, one per hand: each is a body of its own with one hand
// on it; each swung across a limb stops on it (its hand drawn held back on the near side, the controller gone on) and pushes that monster; each gives its own
// hand the pulse. And a sword alone in either hand still stops on a limb.
//
//   weight_dual_test        (a second or two: the steps are paced by the clock, as in the game)

#include "../../vr/weight.h"
#include "../../vr/weight_lib.h"
#include "../../vr/grip_lib.h"
#include "../../vr/clash_loud.h"
#include "../../vr/weapon_heads.h"
#include "../../vr/weapon_head_parts.h"
#include "hold.h"
#include "../../vr/shoulder_draw.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// The touch's bodies (render metres; the test's view makes them the head's frame) and what the doll was pushed with, per monster.
monsterbodies::Snapshot g_snap;
bool Bodies(monsterbodies::Snapshot* out) {
	*out = g_snap;
	out->tickMs = GetTickCount64();
	return true;
}
float g_pushed[16] = {};   // N s per monster id (under 16)
int g_pushes[16] = {};
void Push(const monsterbodies::Snapshot& snap, int i, const float imp[3], const float*) {
	const uint32_t id = snap.b[i].id;
	if (id < 16) {
		g_pushed[id] += std::sqrt(imp[0] * imp[0] + imp[1] * imp[1] + imp[2] * imp[2]);
		++g_pushes[id];
	}
}
int g_pulses[2] = {};
float g_pulseAmp = 0.0f;
void Pulse(int side, float amplitude, int) {
	if (side >= 0 && side < 2) ++g_pulses[side];
	g_pulseAmp = amplitude;
}
struct ClashHeard { float speed; uint32_t volume; };
std::vector<ClashHeard> g_clashes;   // [weight] clash_sound: what the game's block sound was asked to play
void ClashSound(float speed, uint32_t volume, const char*) { g_clashes.push_back({speed, volume}); }
std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }

bool g_lib = false, g_grip = false;   // the weight / grip library loaded (dll=, grip=): the rigs ask for them as the game does

struct Rig {
	skel::Input in, drawn;
	weight::Out out;
	uint32_t sample = 0;
	Rig(int slot, int secondKind, int kind = (int)heft::hold::Kind::Sword) {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		const float r[3] = {0.25f, -0.4f, -0.3f}, l[3] = {-0.25f, -0.4f, -0.3f};
		for (int k = 0; k < 3; ++k) { in.hand[0][k] = r[k]; in.hand[1][k] = l[k]; }
		skel::Input::Weight& w = in.weight;
		w.on = true;
		w.kind = kind;
		in.weaponType = 100 + kind;   // (as the game's: never 0 for a weapon - a key of 0 is weight.cpp's "no shaft measured yet", and a dagger is the hold's kind 0)
		w.handsModel = g_lib ? 1 : 0;
		w.gripModel = g_grip ? 1 : 0;
		w.slot = slot;
		w.second = secondKind >= 0;
		w.secondKind = secondKind;
		w.otherBusy = secondKind >= 0;
		w.feel = 1.0f;
		w.strength = 1.0f;
		w.grabM = 0.12f;
		w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.rawGrip[0] = w.rawGrip[1] = 0.0f;
		w.tipAxis = 2;
		// the world's frame: the render world IS the head's frame (eye at 0, a metre a unit, forward -z)
		w.monsterBodies = true;
		w.monsterRagdoll = true;
		w.worldOk = true;
		w.worldCamR[0] = 1.0f;
		w.worldCamF[2] = -1.0f;
		w.worldUpm = 1.0f;
	}
	void Step(int ms = 11) {
		Sleep(ms);
		in.sample = ++sample;
		skel::Input c = in;
		weight::Apply(&c, &out);
		drawn = c;   // the hands as drawn (on the weapons they hold)
	}
	void Steps(int n) { while (n-- > 0) Step(); }
};

// Where a hand's sword reaches: the fist's line, the main weapon's tip known from ShaftEnds (the second's mirrored across x).
void Limb(int i, uint32_t id, float x, float z) {
	monsterbodies::Body& b = g_snap.b[i];
	b = monsterbodies::Body{};
	b.id = id;
	b.bottom[0] = b.top[0] = x;
	b.bottom[2] = b.top[2] = z;
	b.bottom[1] = -1.2f;
	b.top[1] = 0.6f;
	b.radius = 0.07f;   // a fallen's leg
	b.rig = true;
	b.bone = 5;
	b.doll = true;
}

// Swing: the hands out sideways (the right +x, the left -x) at v m/s over `m` metres, the bodies one across each blade. Returns how far each drawn hand was held back.
void Swing(Rig& r, float v, float m, bool right, bool left, float back[2]) {
	const float x0[2] = {r.in.hand[0][0], r.in.hand[1][0]};
	const int steps = (int)(m / v / 0.011f) + 20;
	back[0] = back[1] = 0.0f;
	for (int i = 0; i < steps; ++i) {
		const float d = std::min(m, v * 0.011f * (float)(i + 1));
		if (right) r.in.hand[0][0] = x0[0] + d;
		if (left) r.in.hand[1][0] = x0[1] - d;
		r.Step();
		// the controller past the hand as drawn - held back by the body (the most of the swing: at its end the blade has slid off the limb's end)
		back[0] = std::max(back[0], r.in.hand[0][0] - r.drawn.hand[0][0]);
		back[1] = std::max(back[1], r.drawn.hand[1][0] - r.in.hand[1][0]);
	}
}

void Clear() {
	std::memset(g_pushed, 0, sizeof g_pushed);
	std::memset(g_pushes, 0, sizeof g_pushes);
	g_pulses[0] = g_pulses[1] = 0;
}

float g_dualBack[2] = {}, g_dualPush[2] = {}, g_aloneBack[2] = {}, g_alonePush[2] = {};   // what each hand's sword did in a pair and alone
void TestDual() {
	Rig r(0, (int)heft::hold::Kind::Sword);
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);   // placed: the first sword in the right hand, the second in the left
	float butt[3], tip[3];
	EXPECT(weight::ShaftEnds(butt, tip), "the first sword's ends");
	const float dx = tip[0] - butt[0], dz = tip[2] - butt[2];
	const float hx = r.in.hand[0][0], hz = r.in.hand[0][2];
	// a limb before each blade: 0.55 of the way to its tip, 18 cm outward of where the blade is now (the right's to the right, the left's mirrored)
	const float ax = butt[0] + 0.55f * dx - hx, az = butt[2] + 0.55f * dz - hz;
	g_snap.n = 2;
	Limb(0, 1, hx + ax + 0.18f, hz + az);
	Limb(1, 2, r.in.hand[1][0] - ax - 0.18f, r.in.hand[1][2] + az);
	Clear();
	r.Steps(3);
	float back[2];
	Swing(r, 3.0f, 0.6f, true, true, back);
	std::printf("  two swords, one per hand, each swung 60 cm across a limb at 3 m/s: the right pushed its monster %d time(s), %.1f N s, its hand held back %.0f cm; "
	            "the left %d, %.1f N s, held back %.0f cm; pulses %d / %d\n", g_pushes[1], g_pushed[1], back[0] * 100, g_pushes[2], g_pushed[2], back[1] * 100, g_pulses[0], g_pulses[1]);
	EXPECT(g_pushes[1] > 0 && g_pushes[2] > 0, "each sword pushes the monster it meets (%d, %d)", g_pushes[1], g_pushes[2]);
	EXPECT(back[0] > 0.04f && back[1] > 0.04f, "each sword stops on its limb: its hand drawn held back (%.2f, %.2f m)", back[0], back[1]);
	g_dualBack[0] = back[0]; g_dualBack[1] = back[1]; g_dualPush[0] = g_pushed[1]; g_dualPush[1] = g_pushed[2];
	EXPECT(g_pulses[0] > 0 && g_pulses[1] > 0, "each hand feels its own strike (%d, %d)", g_pulses[0], g_pulses[1]);
	bool told = false;
	for (const std::string& l : g_log) told = told || l.find("a weapon in each hand: the left hand's sword is a body of its own") != std::string::npos;
	EXPECT(told, "the log says the second weapon is a body of its own");
	r.in.weight.on = false;
	r.Step();
}

// The same swing with a sword alone in the right hand, then alone in the left: it stops too (and the other hand, empty, swings freely).
void TestAlone(int slot, int kind = (int)heft::hold::Kind::Sword) {
	Rig r(slot, -1, kind);
	const char* what = heft::hold::KindName((heft::hold::Kind)kind);
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);
	float butt[3], tip[3];
	weight::ShaftEnds(butt, tip);
	const float dx = tip[0] - butt[0], dz = tip[2] - butt[2];
	const float out = slot == 0 ? 0.18f : -0.18f;
	g_snap.n = 1;
	Limb(0, 1, butt[0] + 0.55f * dx + out, butt[2] + 0.55f * dz);   // (across the blade's path, 18 cm outward)
	Clear();
	r.Steps(3);
	float back[2];
	Swing(r, 3.0f, 0.6f, slot == 0, slot == 1, back);
	std::printf("  a %s alone in the %s hand: pushed %d time(s), %.1f N s, the hand held back %.0f cm\n", what, slot == 0 ? "right" : "left", g_pushes[1], g_pushed[1], back[slot] * 100);
	EXPECT(g_pushes[1] > 0 && back[slot] > 0.03f, "a %s alone in the %s hand stops on the limb and pushes it", what, slot == 0 ? "right" : "left");
	g_log.clear();
	if (kind == (int)heft::hold::Kind::Sword) {   // (the swords alone: what each of the pair is held to)
		g_aloneBack[slot] = back[slot];
		g_alonePush[slot] = g_pushed[1];
	}
	r.in.weight.on = false;
	r.Step();
}

// [weight] monster_shields: a sword swung into a monster's shield stops on it - the weight model says the monster was blocked (Blocked 2) and never reached
// (Reached false: hit by touch presses nothing), the shield pushes no doll; swung where the monster has no shield in the way, it reaches its body.
void TestShield() {
	Rig r(0, -1);
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);
	float butt[3], tip[3];
	weight::ShaftEnds(butt, tip);
	const float dx = tip[0] - butt[0], dz = tip[2] - butt[2];
	const float x = butt[0] + 0.55f * dx, z = butt[2] + 0.55f * dz;
	g_snap.n = 2;
	Limb(0, 7, x + 0.18f, z);        // its shield in the blade's way (a capsule of it)
	g_snap.b[0].held = 2;
	g_snap.b[0].doll = false;
	g_snap.b[0].radius = 0.06f;
	Limb(1, 7, x + 0.45f, z);        // its body behind the shield
	Clear();
	r.Steps(3);
	float back[2];
	Swing(r, 3.0f, 0.6f, true, false, back);
	const int blocked = weight::Blocked(7, 1000);
	const bool reached = weight::Reached(7, 1000);
	std::printf("  a sword swung into a monster's shield: %s, %s; the doll pushed %d time(s)\n", blocked == 2 ? "blocked by its shield" : blocked ? "blocked" : "NOT BLOCKED",
	            reached ? "REACHED ITS BODY" : "its body not reached", g_pushes[7]);
	EXPECT(blocked == 2 && !reached && g_pushes[7] == 0, "the shield stops it: blocked by the shield, the body not reached, no doll pushed (%d, %d, %d)", blocked, reached ? 1 : 0, g_pushes[7]);
	r.in.weight.on = false;
	r.Step();
	// the same monster, its body in the way and no shield there
	Rig r2(0, -1);
	g_snap = monsterbodies::Snapshot{};
	r2.Steps(5);
	weight::ShaftEnds(butt, tip);
	g_snap.n = 1;
	Limb(0, 8, butt[0] + 0.55f * (tip[0] - butt[0]) + 0.18f, butt[2] + 0.55f * (tip[2] - butt[2]));
	Clear();
	r2.Steps(3);
	Swing(r2, 3.0f, 0.6f, true, false, back);
	std::printf("  the same swing at a monster's body: %s\n", weight::Reached(8, 1000) ? "reached it" : "NOT REACHED");
	EXPECT(weight::Reached(8, 1000) && !weight::Blocked(8, 1000), "its body reached, nothing blocked");
	r2.in.weight.on = false;
	r2.Step();
}

// [weight] second_lets_go - the user, 2026-10-24: «когда у меня оружия в двух руках если я грипом выбросил первое оружие то со второго пропадает хитбокс и оно
// перестает быть оружием и его нельзя выбросить грипом». A grip pressed and let go drops that hand's weapon; the other stays in its hand, a body (it still stops on
// a limb), and its own grip lets it go too; pressed away from it, its grip calls it back. Off: the second is held for good.
void Grip(Rig& r, int side, float g, int steps) {
	r.in.weight.rawGrip[side] = g;
	r.Steps(steps);
}
void Drop(Rig& r, int side) {   // pressed, then let go
	Grip(r, side, 1.0f, 6);
	Grip(r, side, 0.0f, 6);
}
bool SecondStops(Rig& r) {   // the second (left) sword swung across a limb before it: stopped on it
	float butt[3], tip[3];
	const bool mainEnds = weight::ShaftEnds(butt, tip);
	g_snap.n = 1;
	Limb(0, 3, r.in.hand[1][0] - 0.40f, r.in.hand[1][2] - 0.45f);
	Clear();
	r.Steps(3);
	float back[2];
	Swing(r, 3.0f, 0.6f, false, true, back);
	g_snap.n = 0;
	(void)mainEnds;
	return g_pushes[3] > 0 || back[1] > 0.03f;
}
void TestDualDrop() {
	{   // the first (right) dropped: the second stays a weapon in its hand, and can be dropped and called back
		Rig r(0, (int)heft::hold::Kind::Sword);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		EXPECT(weight::CarryingHand() == 0 && !weight::SecondLetGo() && r.out.second && !r.out.free2, "placed: both in their hands (carrying %d)", weight::CarryingHand());
		Drop(r, 0);
		EXPECT(weight::CarryingHand() == -1, "the first let go: no hand carries it (%d)", weight::CarryingHand());
		EXPECT(!weight::SecondLetGo() && r.out.second && !r.out.free2 && weight::HoldsWeapon(1), "... the second still in its hand, a weapon (its grip the weapon's)");
		EXPECT(SecondStops(r), "... and still a body: swung across a limb, it stops on it / pushes it");
		Drop(r, 1);
		EXPECT(weight::SecondLetGo() && r.out.free2 && !weight::HoldsWeapon(1), "the second let go by its grip: it falls, drawn where it is");
		bool told = false;
		for (const std::string& l : g_log) told = told || l.find("(the second weapon): let go") != std::string::npos;
		EXPECT(told, "the log says the second was let go");
		r.in.hand[1][0] -= 0.6f;   // (its hand away from where it lies)
		Grip(r, 1, 1.0f, 60);
		EXPECT(!weight::SecondLetGo() && !r.out.free2, "its grip pressed away from it calls it back into its hand");
		std::printf("  a weapon in each hand, the first dropped: the second %s, %s; dropped by its grip, then called back: %s\n", weight::HoldsWeapon(1) ? "still in its hand" : "NOT IN ITS HAND",
		            "a body that stops on a limb", weight::SecondLetGo() ? "STILL AWAY" : "in its hand again");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	{   // the second (left) dropped first: the first stays in its hand
		Rig r(0, (int)heft::hold::Kind::Sword);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		Drop(r, 1);
		EXPECT(weight::SecondLetGo() && weight::CarryingHand() == 0 && weight::HoldsWeapon(0), "the second let go first: the first still carried by its hand (%d)", weight::CarryingHand());
		Drop(r, 0);
		EXPECT(weight::CarryingHand() == -1 && weight::SecondLetGo(), "... then the first let go too: both away");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	{   // second_lets_go off: the second is held for good, as before
		Rig r(0, (int)heft::hold::Kind::Sword);
		r.in.weight.secondLetsGo = false;
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		Drop(r, 1);
		EXPECT(!weight::SecondLetGo() && !r.out.free2, "second_lets_go off: its grip does not let it go");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
}

// ... and the user, 2026-10-24: «надо чтобы его второго тоже можно было выбросить грипом и достать потом из за спины как и первое», «первое из за правого плеча
// второе из за левого». With [hands] shoulder_draw and [weight] after_drop: both let go go to the back; the left hand behind the RIGHT shoulder takes neither,
// behind its own (left) shoulder it draws the second; the right hand behind the right shoulder draws the first.
void HandAt(Rig& r, int side, shoulderdraw::Side zone) {
	float p[3];
	shoulderdraw::DrawPoint(zone, p);
	for (int k = 0; k < 3; ++k) r.in.hand[side][k] = p[k];
}
// Both on the back, then each of the four: the right / the left hand behind the right / the left shoulder - the weapon that hangs there goes into that hand
// (the user, 2026-10-24: «правой рукой я не могу достать оружие из-за левого плеча левое ... надо исправить»); a hand with a weapon then takes no other.
void BothOnBack(Rig& r) {
	r.in.weight.drawMode = true;
	r.in.weight.afterDrop = 1;
	r.in.weight.drawStowS = 0.2f;
	r.in.weight.stowAfterS = 0.2f;
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);
	Drop(r, 0);
	Drop(r, 1);
	for (int i = 0; i < 200 && !(r.out.stowed && r.out.stowed2); ++i) r.Step();
}
void TestDualBack() {
	struct Case { int hand; shoulderdraw::Side zone; const char* what; };
	const Case cases[4] = {{0, shoulderdraw::Side::Right, "the right hand behind the right shoulder: the first (the right weapon) into it"},
	                       {1, shoulderdraw::Side::Left, "the left hand behind the left shoulder: the second (the left weapon) into it"},
	                       {0, shoulderdraw::Side::Left, "the right hand behind the left shoulder: the second into the right hand"},
	                       {1, shoulderdraw::Side::Right, "the left hand behind the right shoulder: the first into the left hand"}};
	for (const Case& c : cases) {
		Rig r(0, (int)heft::hold::Kind::Sword);
		BothOnBack(r);
		EXPECT(r.out.stowed && r.out.stowed2 && r.out.free2, "both let go go to the back (first %d, second %d)", r.out.stowed ? 1 : 0, r.out.stowed2 ? 1 : 0);
		HandAt(r, c.hand, c.zone);
		Grip(r, c.hand, 1.0f, 60);
		const bool first = c.zone == shoulderdraw::Side::Right;   // (the right weapon hangs behind the right shoulder, the left one behind the left)
		const bool ok = first ? (weight::CarryingHand() == c.hand && !r.out.stowed && r.out.stowed2) : (weight::SecondHand() == c.hand && !weight::SecondLetGo() && r.out.stowed);
		EXPECT(ok, "%s - only it (carrying %d, the second's hand %d, first on the back %d, second %d)", c.what, weight::CarryingHand(), weight::SecondHand(), r.out.stowed ? 1 : 0,
		       r.out.stowed2 ? 1 : 0);
		std::printf("  both on the back, %s: %s\n", c.what, ok ? "yes" : "NO");
		// ... and that hand, holding it, behind the other shoulder: it takes no other
		Grip(r, c.hand, 0.98f, 2);
		HandAt(r, c.hand, first ? shoulderdraw::Side::Left : shoulderdraw::Side::Right);
		Grip(r, c.hand, 1.0f, 40);
		EXPECT(first ? (r.out.stowed2 && weight::SecondLetGo()) : r.out.stowed, "... holding it, the same hand takes not the other from the other shoulder");
		if (c.hand == 0 && first) {   // the other hand, empty, draws the other: each hand one weapon (the first case: each into its own hand)
			const float front[3] = {0.25f, -0.4f, -0.3f};   // (the right hand, with the first, out from behind the left shoulder: the left reaches there alone)
			for (int i = 0; i < 20; ++i) {
				for (int k = 0; k < 3; ++k) r.in.hand[0][k] += (front[k] - r.in.hand[0][k]) * 0.2f;
				r.Step();
			}
			HandAt(r, 1, shoulderdraw::Side::Left);
			Grip(r, 1, 1.0f, 60);
			EXPECT(weight::CarryingHand() == 0 && weight::SecondHand() == 1 && !r.out.stowed && !r.out.stowed2, "... the other hand draws the other: each in its own hand");
			bool toldBack = false;
			for (const std::string& l : g_log) toldBack = toldBack || l.find("(the second weapon): on the back") != std::string::npos;
			EXPECT(toldBack, "the log says the second went to the back");
		}
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
}

// The user, 2026-10-24: «если я второе оружие выбрасываю рука не расслабляется я не могу взять первое оружие», «второй рукой»: the left hand drops the second
// weapon, then closes on the first (in the right hand) as its second hand - it takes it; the second's grip does not call its own weapon back meanwhile.
void TestDropThenTakeFirst() {
	Rig r(0, (int)heft::hold::Kind::Sword);
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);
	Drop(r, 1);
	EXPECT(weight::SecondLetGo() && !weight::HoldsWeapon(1), "the second let go: its hand holds nothing");
	float butt[3], tip[3];
	EXPECT(weight::ShaftEnds(butt, tip), "the first's ends");
	for (int k = 0; k < 3; ++k) r.in.hand[1][k] = butt[k] + 0.35f * (tip[k] - butt[k]);   // the left fist on the first's blade, a third of the way up
	Grip(r, 1, 1.0f, 30);
	EXPECT(r.out.holds[1] && weight::HoldsWeapon(1) && weight::CarryingHand() >= 0, "... the same hand closes on the first weapon: it takes it as its second hand");
	EXPECT(weight::SecondLetGo(), "... and the second stays where it is (its grip is on the first, not calling it back)");
	std::printf("  a weapon in each hand, the second dropped: its hand %s the first weapon as its second hand\n", r.out.holds[1] ? "takes" : "DOES NOT TAKE");
	r.in.weight.on = false;
	r.Step();
	g_log.clear();
	// The user, 2026-10-24: «если я выбросил левое оружие и взял левой рукой двуручный меч например у меня сейчас появляется оружие левой руки параллельно» - with the
	// shoulder draw on (a grip pressed anywhere calls a dropped weapon back): the left closes on the first - its own dropped weapon stays where it is, no second weapon
	// in the hand while it holds the first; a grip that just misses the first's shaft (18 cm off) calls nothing back either.
	for (int miss = 0; miss < 2; ++miss) {
		Rig q(0, (int)heft::hold::Kind::Sword);
		q.in.weight.drawMode = true;
		q.in.weight.afterDrop = 1;
		q.in.weight.drawStowS = 5.0f;   // (it lies on the floor meanwhile)
		g_snap = monsterbodies::Snapshot{};
		q.Steps(5);
		Drop(q, 1);
		float b2[3], t2[3];
		weight::ShaftEnds(b2, t2);
		for (int k = 0; k < 3; ++k) q.in.hand[1][k] = b2[k] + 0.35f * (t2[k] - b2[k]);
		if (miss) q.in.hand[1][1] += 0.18f;
		bool appeared = false;
		q.in.weight.rawGrip[1] = 1.0f;
		for (int i = 0; i < 60; ++i) {
			q.Step();
			appeared = appeared || !weight::SecondLetGo() || !q.out.free2 || weight::SecondHand() >= 0;
		}
		EXPECT(!appeared, "%s: the dropped second weapon stays away (no second weapon in the left hand)", miss ? "a grip 18 cm off the first's shaft" : "the left hand on the first");
		if (!miss) EXPECT(q.out.holds[1], "... the left holds the first as its second hand");
		std::printf("  shoulder draw on, the second dropped, the left %s: its own weapon %s\n", miss ? "grips 18 cm off the first's shaft" : "closes on the first",
		            appeared ? "CAME BACK INTO IT" : "stays where it lies");
		q.in.weight.on = false;
		q.Step();
		g_log.clear();
	}
}

// [weight] stab_in for the second weapon (the user: «меч когда во второй руке не хочет втыкаться»): its hand's sword driven point first into a body sticks in, as the
// first's does (through the weight library: dll=).
bool g_stabbing[2] = {};
bool StabNow(int side) { return side >= 0 && side < 2 && g_stabbing[side]; }
void TestSecondStab() {
	if (!g_lib) {
		std::printf("  the second weapon's stab: skipped (needs the weight library: dll=)\n");
		return;
	}
	for (int side = 0; side < 2; ++side) {   // the first weapon's (the right hand) as the measure of the test's thrust, then the second's (the left)
		Rig r(0, (int)heft::hold::Kind::Sword);
		r.in.weight.stabIn = true;
		weight::SetStabSource(&StabNow);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		// a body just past the point along the blade's line (the hand's -z; the first's tip as ShaftEnds has it, the second the same sword's): a trunk 25 cm on
		float b0[3], t0[3];
		const float reach = weight::ShaftEnds(b0, t0) ? std::fabs(t0[2] - r.in.hand[0][2]) : 1.0f;
		g_snap.n = 1;
		Limb(0, 11, r.in.hand[side][0], r.in.hand[side][2] - reach - 0.25f);
		g_snap.b[0].radius = 0.18f;
		g_stabbing[side] = true;
		r.Steps(3);
		for (int i = 0; i < 60; ++i) {   // the hand driven in point first at 2 m/s
			r.in.hand[side][2] -= 0.022f;
			r.Step();
		}
		bool in = false;
		for (const std::string& l : g_log)
			in = in || (l.find(side ? "stab (the left hand's, the second weapon): the point went into monster 11" : "stab: the point went into monster 11") != std::string::npos);
		std::printf("  the %s weapon driven point first into a body: %s\n", side ? "second (left)" : "first (right)", in ? "it sticks in" : "IT DOES NOT STICK IN");
		EXPECT(in, "the %s weapon sticks in", side ? "second" : "first");
		if (!in)
			for (const std::string& l : g_log)
				if (l.find("stab") != std::string::npos || l.find("struck") != std::string::npos) std::printf("    log: %s\n", l.c_str());
		if (!in) {
			float b[3], t[3];
			if (weight::ShaftEnds(b, t)) std::printf("    the first's shaft: butt %.2f %.2f %.2f tip %.2f %.2f %.2f; the hand %.2f %.2f %.2f\n", b[0], b[1], b[2], t[0], t[1], t[2], r.in.hand[side][0], r.in.hand[side][1], r.in.hand[side][2]);
		}
		g_stabbing[side] = false;
		weight::SetStabSource(nullptr);
		g_snap.n = 0;
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
}

// [weight] strike_by_force: a sword swung across a limb is a live strike for hit by touch (weight::LiveStrikes) while it is on - the monster, a hand on it, the
// impulse in its window, where along the weapon it met the body - and its slot is one the weight model holds (ForceSlots); none with strike_by_force off.
void TestLiveStrikes() {
	for (int on = 0; on < 2; ++on) {
		Rig r(0, -1);
		r.in.weight.strikeByForce = on != 0;
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		float butt[3], tip[3];
		weight::ShaftEnds(butt, tip);
		g_snap.n = 1;
		Limb(0, 9, butt[0] + 0.55f * (tip[0] - butt[0]) + 0.18f, butt[2] + 0.55f * (tip[2] - butt[2]));
		r.Steps(3);
		const float x0 = r.in.hand[0][0];
		double window = 0.0;
		float along = -1.0f;
		int hands = 0, seen = 0;
		for (int i = 0; i < 40; ++i) {
			r.in.hand[0][0] = x0 + std::min(0.6f, 3.0f * 0.011f * (float)(i + 1));
			r.Step();
			weight::LiveStrike ls[8];
			const int n = weight::LiveStrikes(ls, 8);
			for (int k = 0; k < n; ++k)
				if (ls[k].s.id == 9) { ++seen; window = std::max(window, ls[k].s.window); hands = std::max(hands, ls[k].s.hands); along = std::max(along, ls[k].s.along); }
		}
		if (on) {
			std::printf("  strike by force: a sword swung across a limb - a live strike %d step(s), %.2f N s in its window, %d hand(s), met at %.0f%% of the weapon; slots 0x%x\n", seen,
			            window, hands, along * 100.0f, weight::ForceSlots());
			EXPECT(seen > 0 && window > 0.0 && hands == 1 && along > 0.0f && along <= 1.0f, "a live strike on the limb, with a hand, an impulse, a place along it");
			EXPECT((weight::ForceSlots() & 1u) != 0, "the right slot is the weight model's (judged by force)");
		} else {
			EXPECT(seen == 0, "strike_by_force off: no live strikes (%d)", seen);
		}
		g_snap.n = 0;
		r.in.weight.on = false;
		r.Step();
	}
}

// The user, 2026-10-24: «если в левой руке есть оружие то я не могу взять второе» - the left hand with its own (the second) weapon closes on the first in the right hand:
// it takes nothing; «если я передал правое оружие в левую руку то я должен иметь возможность из-за спины достать левое оружие в правую руку» - the HAND-OVER: the left
// drops its weapon (to the back), takes the first, the right lets go of it; the right hand behind a shoulder draws the left one's weapon into itself. Each hand one weapon.
void TestOneAHand() {
	{
		Rig r(0, (int)heft::hold::Kind::Sword);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		float butt[3], tip[3];
		weight::ShaftEnds(butt, tip);
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = butt[k] + 0.35f * (tip[k] - butt[k]);   // the left fist (with its sword) on the first's blade
		Grip(r, 1, 1.0f, 30);
		EXPECT(!r.out.holds[1] && !weight::SecondLetGo() && weight::SecondHand() == 1, "the left hand with its own weapon takes not the first (holds the first: %d, the second's hand %d)",
		       r.out.holds[1] ? 1 : 0, weight::SecondHand());
		std::printf("  a weapon in each hand, the left closes on the right's: %s\n", r.out.holds[1] ? "IT TAKES IT TOO (two in one hand)" : "it takes nothing (one weapon a hand)");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	{
		Rig r(0, (int)heft::hold::Kind::Sword);
		r.in.weight.drawMode = true;
		r.in.weight.afterDrop = 1;
		r.in.weight.drawStowS = 0.2f;
		r.in.weight.stowAfterS = 0.2f;
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		Drop(r, 1);   // the left lets its sword go: it goes to the back
		float butt[3], tip[3];
		weight::ShaftEnds(butt, tip);
		for (int k = 0; k < 3; ++k) r.in.hand[1][k] = butt[k] + 0.35f * (tip[k] - butt[k]);
		Grip(r, 1, 1.0f, 20);   // ... takes the first (and keeps its grip)
		Grip(r, 0, 1.0f, 6);    // the right lets go of it
		Grip(r, 0, 0.0f, 10);
		for (int i = 0; i < 200 && !r.out.stowed2; ++i) r.Step();
		EXPECT(weight::CarryingHand() == 1 && r.out.holds[1] && !r.out.holds[0] && r.out.stowed2, "the hand-over: the first in the left hand, the right empty, the second on the back (carrying %d)",
		       weight::CarryingHand());
		HandAt(r, 0, shoulderdraw::Side::Left);   // the right hand behind the left shoulder, where the left one's weapon hangs
		Grip(r, 0, 1.0f, 60);
		EXPECT(weight::SecondHand() == 0 && !weight::SecondLetGo() && weight::CarryingHand() == 1 && r.out.free2,
		       "... the right hand draws the second into itself: each hand one weapon (the second's hand %d, the first's %d)", weight::SecondHand(), weight::CarryingHand());
		EXPECT(weight::HoldsWeapon(0) && weight::HoldsWeapon(1), "... both hands' grips are the weapons'");
		std::printf("  the hand-over: the left holds the first, the right draws the second from behind the left shoulder: %s\n",
		            weight::SecondHand() == 0 ? "each hand one weapon" : "NOT DRAWN");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
}

// The headset, 2026-10-24 18:51 («ничего не исправлено левая рука по прежнему держит два оружия»): after_drop to the back WITHOUT the shoulder draw - the first let
// go goes to the back, and the left hand, still holding its own sword, reaches behind a shoulder (the swap's zone) and grips: it must take nothing. Each way and
// each shoulder, through the real zones (shoulderswap::Near / shoulderdraw), and the draw mode too.
void TestFullHandTakesNone() {
	for (int draw = 0; draw < 2; ++draw)
		for (int z = 0; z < 2; ++z) {
			Rig r(0, (int)heft::hold::Kind::Sword);
			r.in.weight.drawMode = draw != 0;
			r.in.weight.afterDrop = 1;
			r.in.weight.stowAfterS = 0.3f;
			r.in.weight.drawStowS = 0.3f;
			g_snap = monsterbodies::Snapshot{};
			r.Steps(5);
			Drop(r, 0);   // the first (right) let go: to the back
			for (int i = 0; i < 200 && !r.out.stowed; ++i) r.Step();
			EXPECT(r.out.stowed && !weight::SecondLetGo() && weight::SecondHand() == 1, "the first on the back, the left still has its sword");
			const shoulderdraw::Side zone = z ? shoulderdraw::Side::Left : shoulderdraw::Side::Right;
			HandAt(r, 1, zone);   // the left hand (with its sword) behind a shoulder
			r.Steps(3);
			Grip(r, 1, 0.0f, 3);
			Grip(r, 1, 1.0f, 60);
			const bool two = weight::CarryingHand() == 1 || r.out.holds[1] || !r.out.stowed;
			EXPECT(!two, "%s: the left hand with its own weapon, behind the %s shoulder, takes not the first (carrying %d, first on the back %d)", draw ? "shoulder draw" : "after_drop's back",
			       z ? "left" : "right", weight::CarryingHand(), r.out.stowed ? 1 : 0);
			std::printf("  %s, the left hand with its sword behind the %s shoulder grips: %s\n", draw ? "shoulder draw" : "after_drop to the back", z ? "left" : "right",
			            two ? "IT TAKES THE FIRST TOO" : "it takes nothing");
			bool told = false;
			for (const std::string& l : g_log) told = told || l.find("not taken: the left hand holds its own weapon") != std::string::npos;
			EXPECT(told, "the refusal is told");
			r.in.weight.on = false;
			r.Step();
			g_log.clear();
		}
}

// His run of 18:51 step by step: the first let go goes to the back; the right hand, empty, closes on the left's sword (a second grip on it - its hand has nothing of
// its own); the left lets go of it (the sword in the right hand now); the left, empty, takes the first from behind a shoulder. Each hand ONE weapon: the first in the
// left, the second in the right - drawn where its body is (Out::free2: not at the left wrist, where the game hangs it).
void TestHisSequence() {
	Rig r(0, (int)heft::hold::Kind::Sword);
	r.in.weight.afterDrop = 1;
	r.in.weight.stowAfterS = 0.3f;
	g_snap = monsterbodies::Snapshot{};
	r.Steps(5);
	Drop(r, 0);
	for (int i = 0; i < 200 && !r.out.stowed; ++i) r.Step();
	for (int k = 0; k < 3; ++k) r.in.hand[0][k] = r.in.hand[1][k];
	r.in.hand[0][2] -= 0.25f;   // the right fist on the left's sword, 25 cm up its blade (along the left fist's line, -z)
	r.Steps(3);
	Grip(r, 0, 1.0f, 20);
	const bool both = r.out.holds[0] || weight::HoldsWeapon(0);
	Grip(r, 1, 1.0f, 3);   // the left grip pressed and let go: it lets go of the sword
	Grip(r, 1, 0.0f, 10);
	EXPECT(weight::SecondHand() == 0 && !weight::SecondLetGo(), "the sword passed to the right hand (it held it with the left: %d; its hand now %d)", both ? 1 : 0, weight::SecondHand());
	HandAt(r, 1, shoulderdraw::Side::Right);
	r.Steps(3);
	Grip(r, 1, 1.0f, 60);   // the left, empty now, takes the first from the back
	EXPECT(weight::CarryingHand() == 1 && weight::SecondHand() == 0 && r.out.free2, "each hand one weapon: the first in the left (carrying %d), the second in the right (%d), drawn where it is",
	       weight::CarryingHand(), weight::SecondHand());
	std::printf("  his 18:51 sequence (the first to the back, the sword to the right hand, the left draws the first): %s\n",
	            weight::CarryingHand() == 1 && weight::SecondHand() == 0 ? "each hand one weapon" : "NOT one a hand");
	r.in.weight.on = false;
	r.Step();
	g_log.clear();
}

// The user, 2026-10-24: «когда у меня в двух руках два оружия и я беру правой рукой левое оружие оно сдвинуто относительно кисти правой руки». The second weapon's
// shaft as the skeleton measures it off its drawn bone (weight::SetDrawnShaft2 - here 6 cm and 15 degrees off the left fist's line, as a game grip is): the right hand,
// empty, takes it on that drawn axis and is laid there - its fist ON the drawn weapon's axis, not beside it. Without the measurement (the old way) it lands on the
// fist's line instead, off the drawn weapon.
void TestSecondInPalm() {
	double off[2] = {};
	for (int measured = 0; measured < 2; ++measured) {
		Rig r(0, (int)heft::hold::Kind::Sword);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		const float at[3] = {0.06f, -0.03f, 0.0f};
		const float dir[3] = {0.2588f, 0.0f, -0.9659f};
		if (measured) {
			weight::SetDrawnShaft2(at, dir, -0.14f, 0.9f, 0.0f);
			r.Steps(3);
		}
		Drop(r, 0);   // the right hand empty (the first let go)
		const heft::Q q{r.out.body2Rot[0], r.out.body2Rot[1], r.out.body2Rot[2], r.out.body2Rot[3]};
		const heft::V3 b{r.out.body2[0], r.out.body2[1], r.out.body2[2]};
		const heft::V3 la = heft::Add(b, heft::Rotate(q, {at[0], at[1], at[2]})), ld = heft::Rotate(q, {dir[0], dir[1], dir[2]});
		const heft::V3 fistOff{r.in.weight.fist[0], r.in.weight.fist[1], r.in.weight.fist[2]};
		const heft::V3 target = heft::Add(la, heft::Mul(ld, 0.12));   // on the drawn blade, 12 cm up from its grip
		for (int k = 0; k < 3; ++k) r.in.hand[0][k] = (float)((&target.x)[k] - (&fistOff.x)[k]);
		r.Steps(3);
		Grip(r, 0, 1.0f, 40);
		const bool took = weight::SecondHand() >= 0 && r.out.second;
		const heft::Q hq{r.drawn.rot[0][0], r.drawn.rot[0][1], r.drawn.rot[0][2], r.drawn.rot[0][3]};
		const heft::V3 fist = heft::Add({r.drawn.hand[0][0], r.drawn.hand[0][1], r.drawn.hand[0][2]}, heft::Rotate(hq, fistOff));
		const heft::Q q2{r.out.body2Rot[0], r.out.body2Rot[1], r.out.body2Rot[2], r.out.body2Rot[3]};
		const heft::V3 b2{r.out.body2[0], r.out.body2[1], r.out.body2[2]};
		const heft::V3 la2 = heft::Add(b2, heft::Rotate(q2, {at[0], at[1], at[2]})), ld2 = heft::Rotate(q2, {dir[0], dir[1], dir[2]});   // the drawn axis where the body is now
		const heft::V3 rel = heft::Sub(fist, la2);
		off[measured] = heft::Len(heft::Sub(rel, heft::Mul(ld2, heft::Dot(rel, ld2))));
		std::printf("  the right hand takes the left's sword %s: %s, its fist %.1f cm from the drawn weapon's axis\n", measured ? "(its shaft measured off the drawn bone)" : "(the old way: the fist's line)",
		            took ? "taken" : "NOT TAKEN", off[measured] * 100.0);
		EXPECT(took, "the right hand takes the second weapon (%d)", weight::SecondHand());
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	EXPECT(off[1] < 0.015, "measured: the right fist on the drawn weapon's axis (%.1f cm)", off[1] * 100.0);
	EXPECT(off[0] > 0.03, "(the test's control: the old way the fist lands off it, %.1f cm)", off[0] * 100.0);
}

// [weight] dual_collide - the user, 2026-10-24: «для варвара оружия когда в двух руках не должны проходить друг сквозь друга должны стукаться сталью». Two swords side
// by side; the right driven across into the left at 2 m/s: it stops on it (its hand drawn held back, the controller gone on), a knock felt in BOTH hands and told;
// then slid along the other's blade it glances (moves on along it). Off: the right passes through, as before. Never a strike: nothing counted on a monster.
void TestDualCollide() {
	float held[3] = {}, slid = 0.0f, amp = 0.0f;
	int pulses[3][2] = {};
	std::vector<ClashHeard> heard[3];
	bool told = false;
	weight::SetClashSound(&ClashSound);
	for (int pass = 0; pass < 3; ++pass) {   // off at 2 m/s; on at 2 m/s; on, laid on gently (0.3 m/s)
		const bool on = pass != 0;
		const float speed = pass == 2 ? 0.3f : 2.0f;
		Rig r(0, (int)heft::hold::Kind::Sword);
		r.in.weight.dualCollide = on;
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		Clear();
		g_log.clear();
		g_clashes.clear();
		const float x0 = r.in.hand[0][0];
		for (int i = 0; i < 260; ++i) {   // the right hand toward the left one, 70 cm on: its blade across the left's
			r.in.hand[0][0] = x0 - std::min(0.7f, speed * 0.011f * (float)(i + 1));
			r.Step();
			if (speed * 0.011f * (float)i > 0.7f) break;
		}
		r.Steps(10);
		held[pass] = r.drawn.hand[0][0] - r.in.hand[0][0];   // how far the drawn right hand stayed behind its controller
		pulses[pass][0] = g_pulses[0];
		pulses[pass][1] = g_pulses[1];
		heard[pass] = g_clashes;
		if (pass == 1) amp = g_pulseAmp;
		for (const std::string& l : g_log) told = told || (pass == 1 && l.find("the two weapons struck each other") != std::string::npos && l.find("sound ") != std::string::npos);
		if (pass == 1) {   // ... and slid along the left's blade (along -z, still pressed across): it goes on along it
			const float z0 = r.drawn.hand[0][2];
			for (int i = 0; i < 30; ++i) {
				r.in.hand[0][2] -= 0.011f;
				r.Step();
			}
			r.Steps(10);
			slid = z0 - r.drawn.hand[0][2];
		}
		r.in.weight.on = false;
		r.Step();
	}
	weight::SetClashSound(nullptr);
	std::printf("  a weapon in each hand, the right driven across the left at 2 m/s: dual_collide off - its hand %.0f cm behind the controller (it passes); on - %.0f cm behind (it stops "
	            "on the other), pulses %d / %d; slid along the other's blade it went on %.0f cm of 33\n", held[0] * 100.0f, held[1] * 100.0f, pulses[1][0], pulses[1][1], slid * 100.0f);
	const ClashHeard first = heard[1].empty() ? ClashHeard{0.0f, 0} : heard[1][0];
	std::printf("  the knock heard: %zu clash sound(s), the first at %.1f m/s -> %u/255, pulse %.2f; laid on at 0.3 m/s: %zu sound(s), pulses %d / %d (%.0f cm behind)\n",
	            heard[1].size(), first.speed, first.volume, amp, heard[2].size(), pulses[2][0], pulses[2][1], held[2] * 100.0f);
	EXPECT(!heard[1].empty() && first.volume == clashloud::Of(first.speed).gameVolume && first.volume > 38 && first.volume < 255,
	       "on: the knock at 2 m/s plays the clash sound at its loudness (%zu, %.1f m/s, %u/255)", heard[1].size(), first.speed, first.volume);
	EXPECT(std::fabs(amp - clashloud::Of(first.speed).pulse) < 1e-4f, "on: the pulse as hard as the knock (%.2f vs %.2f)", amp, clashloud::Of(first.speed).pulse);
	EXPECT(heard[0].empty(), "off: no clash sound (%zu)", heard[0].size());
	EXPECT(held[2] > 0.15f && heard[2].empty() && pulses[2][0] == 0 && pulses[2][1] == 0, "laid on gently: it stops on the other, silent and unfelt (%.2f m, %zu, %d / %d)", held[2],
	       heard[2].size(), pulses[2][0], pulses[2][1]);
	EXPECT(held[0] < 0.05f, "off: the right sword passes through the left (%.2f m behind)", held[0]);
	EXPECT(held[1] > 0.15f, "on: the right sword stops on the left - its hand held back (%.2f m)", held[1]);
	EXPECT(pulses[1][0] > 0 && pulses[1][1] > 0 && told, "on: the knock felt in both hands (%d / %d) and told", pulses[1][0], pulses[1][1]);
	EXPECT(pulses[0][0] == 0 && pulses[0][1] == 0, "off: no knock");
	EXPECT(slid > 0.15f, "on: slid along the other's blade it glances on (%.2f m)", slid);
	EXPECT(!weight::Reached(0xFFFFFFF0u, 1000), "the other weapon is never a body reached (no strike)");
}

// [weight] dual_collide with [gameplay] melee_head_volume - the user, 2026-10-26: «и меч проходит сквозь лезвие топора но не проходит сквозь древко». A weapon in each
// hand, the right driven across the left at 2 m/s (as TestDualCollide), `dy` above it: what the right hand was held back by (m), how deep the other weapon's sword went
// into the head (m: past the surface; through it, it reaches the head's radius and more) and what was felt and heard. `headOf`: whose head it is (0 the first weapon's,
// 1 the second's), a to b radius `rad` in that weapon's body frame (its shaft along -z through the origin, the fist's line); told (weight::SetDrawnHead, SetDrawnHead2)
// or not (the kind's capsules alone, as before: the head only measured).
struct Cross { float held = 0.0f, into = 0.0f; int pulses[2] = {}; size_t sounds = 0; };
heft::V3 OnBody(const float p[3], const float q[4], heft::V3 v) { return heft::Add({p[0], p[1], p[2]}, heft::Rotate(heft::Q{q[0], q[1], q[2], q[3]}, v)); }
float HeadDepth(const weight::Out& o, int headOf, const float a[3], const float b[3], float rad) {   // the other weapon's sword blade (0.06 .. 0.90 along, r 2 cm) into the head
	const float* hp = headOf == 0 ? o.body : o.body2;
	const float* hq = headOf == 0 ? o.bodyRot : o.body2Rot;
	const float* sp = headOf == 0 ? o.body2 : o.body;
	const float* sq = headOf == 0 ? o.body2Rot : o.bodyRot;
	const heft::V3 ha = OnBody(hp, hq, {a[0], a[1], a[2]}), hb = OnBody(hp, hq, {b[0], b[1], b[2]});
	const heft::V3 sa = OnBody(sp, sq, {0.0, 0.0, -0.06}), sb = OnBody(sp, sq, {0.0, 0.0, -0.90});
	double s, t;
	heft::contact::ClosestOnSegments(sa, sb, ha, hb, &s, &t);
	const double d = heft::Len(heft::Sub(heft::Add(sa, heft::Mul(heft::Sub(sb, sa), s)), heft::Add(ha, heft::Mul(heft::Sub(hb, ha), t))));
	return (float)(rad + 0.02 - d);
}
Cross CrossInto(int slot, int firstKind, int secondKind, float dy, int headOf, bool told, const float a[3], const float b[3], float rad, bool dualCollide = true,
                const float* ma = nullptr, const float* mb = nullptr, float mr = 0.0f) {   // (ma, mb, mr: measured against that capsule of the weapon instead of the head)
	Cross c;
	if (!ma) { ma = a; mb = b; mr = rad; }
	Rig r(slot, secondKind, firstKind);
	r.in.weight.dualCollide = dualCollide;
	g_snap = monsterbodies::Snapshot{};
	r.in.hand[0][1] += dy;
	r.Steps(5);
	if (told && headOf == 0) weight::SetDrawnHead(a, b, rad);
	if (told && headOf == 1) weight::SetDrawnHead2(a, b, rad);
	r.Steps(3);
	Clear();
	g_clashes.clear();
	const float x0 = r.in.hand[0][0];
	c.into = -1.0f;
	for (int i = 0; i < 260; ++i) {
		r.in.hand[0][0] = x0 - std::min(0.7f, 2.0f * 0.011f * (float)(i + 1));
		r.Step();
		c.into = std::max(c.into, HeadDepth(r.out, headOf, ma, mb, mr));
		if (2.0f * 0.011f * (float)i > 0.7f) break;
	}
	r.Steps(10);
	c.held = r.drawn.hand[0][0] - r.in.hand[0][0];
	c.pulses[0] = g_pulses[0];
	c.pulses[1] = g_pulses[1];
	c.sounds = g_clashes.size();
	weight::SetDrawnHead(nullptr, nullptr, 0.0f);
	weight::SetDrawnHead2(nullptr, nullptr, 0.0f);
	r.in.weight.on = false;
	r.Step();
	return c;
}

// The headset 2026-10-28 (claude/test-0186): a javelin in the right hand, an axe in the left, CROSSED - «the axe's blade goes straight through the javelin's shaft».
// The javelin with its drawn extents (0.73 m behind the bone, 1.09 m ahead), along -z; the axe turned to point along +x, across in front of it. One hand driven
// through the other's weapon at `v` m/s (the right down through the axe, or the left up through the javelin): which side of the other it ends on (> 0: it stayed on
// its own side - stopped; < 0: through).
struct CrossedOut { float side = 0.0f, closest = 1e9f; size_t sounds = 0; };
CrossedOut Crossed(float v, bool leftMoves, bool dual = true) {
	CrossedOut o;
	Rig r(0, (int)heft::hold::Kind::Axe, (int)heft::hold::Kind::Javelin);
	r.in.weight.dualCollide = dual;
	g_snap = monsterbodies::Snapshot{};
	const float s = 0.70710678f;
	r.in.rot[1][0] = 0.0f; r.in.rot[1][1] = -s; r.in.rot[1][2] = 0.0f; r.in.rot[1][3] = s;   // the left fist's line -z -> +x
	r.in.hand[1][2] = -0.6f;
	r.in.hand[0][1] = leftMoves ? -0.4f : -0.15f;   // the javelin 25 cm over the axe (the right moving down), or level with it (the left moving up from below)
	if (leftMoves) r.in.hand[1][1] = -0.65f;
	r.Steps(5);
	const float at[3] = {0, 0, 0}, dir[3] = {0, 0, -1};
	weight::SetDrawnShaft(at, dir, -0.73f, 1.09f, 0.0f);
	r.Steps(5);
	g_clashes.clear();
	const float m = 0.5f;
	const int steps = (int)(m / v / 0.011f) + 1;
	const float y0 = leftMoves ? r.in.hand[1][1] : r.in.hand[0][1];
	for (int i = 0; i < steps + 20; ++i) {
		const float d = std::min(m, v * 0.011f * (float)(i + 1));
		if (leftMoves) r.in.hand[1][1] = y0 + d;
		else r.in.hand[0][1] = y0 - d;
		r.Step();
		float jb[3], jt[3];
		if (!weight::ShaftEnds(jb, jt)) continue;
		const heft::V3 ja{jb[0], jb[1], jb[2]}, jz{jt[0], jt[1], jt[2]};
		const heft::V3 aa = OnBody(r.out.body2, r.out.body2Rot, {0.0, 0.0, 0.10}), az = OnBody(r.out.body2, r.out.body2Rot, {0.0, 0.0, -0.58});
		double ps, pt;
		heft::contact::ClosestOnSegments(ja, jz, aa, az, &ps, &pt);
		const heft::V3 pj = heft::Add(ja, heft::Mul(heft::Sub(jz, ja), ps)), pa = heft::Add(aa, heft::Mul(heft::Sub(az, aa), pt));
		o.closest = std::min(o.closest, (float)heft::Len(heft::Sub(pj, pa)));
		o.side = (float)(leftMoves ? pj.y - pa.y : pj.y - pa.y);
	}
	o.sounds = g_clashes.size();
	r.in.weight.on = false;
	r.Step();
	return o;
}

// ... the javelin put INTO the axe at once (as a fresh javelin after a throw is placed in the hand wherever the axe is, or the model's line flipping 180 degrees to
// the drawn one): the hands then still and moved a little - how far apart the two shafts are (centre lines) after a third of a second.
// inBlade: through the middle of the axe's BLADE instead, clear of its shaft - its head volume as the model gives a hand axe (his log 2026-10-28: 3 cm long, r 9.8 cm,
// its middle 9 cm off the shaft's line), 25 cm out from its hand; then how far the javelin's line is from the head volume's.
const float kBladeA[3] = {0.0f, 0.075f, -0.25f}, kBladeB[3] = {0.0f, 0.105f, -0.25f};
constexpr float kBladeR = 0.098f;
float PlacedInside(bool dual = true, bool inBlade = false) {
	Rig r(0, (int)heft::hold::Kind::Axe, (int)heft::hold::Kind::Javelin);
	r.in.weight.dualCollide = dual;
	g_snap = monsterbodies::Snapshot{};
	const float s = 0.70710678f;
	r.in.rot[1][0] = 0.0f; r.in.rot[1][1] = -s; r.in.rot[1][2] = 0.0f; r.in.rot[1][3] = s;
	r.in.hand[1][2] = -0.6f;
	r.in.hand[0][1] = -0.40f;   // level with the axe and over its shaft (25 cm from its hand): the javelin through the axe's shaft
	r.in.hand[0][0] = 0.0f;
	if (inBlade) r.in.hand[0][1] = -0.40f + 0.09f;   // (the blade's middle: 9 cm over the axe's shaft - the shafts 9 cm apart, clear of each other)
	r.Steps(5);
	if (inBlade) weight::SetDrawnHead2(kBladeA, kBladeB, kBladeR);
	r.Steps(25);
	float apart = 0.0f;
	for (int i = 0; i < 30; ++i) {
		r.in.hand[0][0] = 0.02f * std::sin(0.3f * (float)i);   // a hand never still
		r.Step();
		float jb[3], jt[3];
		if (!weight::ShaftEnds(jb, jt)) continue;
		const heft::V3 ja{jb[0], jb[1], jb[2]}, jz{jt[0], jt[1], jt[2]};
		const heft::V3 aa = inBlade ? OnBody(r.out.body2, r.out.body2Rot, {kBladeA[0], kBladeA[1], kBladeA[2]}) : OnBody(r.out.body2, r.out.body2Rot, {0.0, 0.0, 0.10});
		const heft::V3 az = inBlade ? OnBody(r.out.body2, r.out.body2Rot, {kBladeB[0], kBladeB[1], kBladeB[2]}) : OnBody(r.out.body2, r.out.body2Rot, {0.0, 0.0, -0.58});
		double ps, pt;
		heft::contact::ClosestOnSegments(ja, jz, aa, az, &ps, &pt);
		apart = (float)heft::Len(heft::Sub(heft::Add(ja, heft::Mul(heft::Sub(jz, ja), ps)), heft::Add(aa, heft::Mul(heft::Sub(az, aa), pt))));
	}
	weight::SetDrawnHead2(nullptr, nullptr, 0.0f);
	r.in.weight.on = false;
	r.Step();
	return apart;
}

void TestCrossed() {
	const float inside = PlacedInside(), insideOff = PlacedInside(false);
	std::printf("  a javelin put through an axe's shaft (a fresh one after a throw, a line measured anew): after 0.3 s the shafts %.1f cm apart (dual_collide off: %.1f cm)\n",
	            inside * 100.0f, insideOff * 100.0f);
	EXPECT(inside > 0.028f, "two weapons put into each other come apart - they rest on each other, not inside (%.3f m between the shafts' lines)", inside);
	EXPECT(insideOff < 0.015f, "(off: they stay through each other, as before - %.3f m)", insideOff);
	// ... through the middle of the axe's blade (the user, 2026-10-28: «the axe collides only with its wooden handle; its blade is not solid»)
	const float blade = PlacedInside(true, true), bladeOff = PlacedInside(false, true);
	std::printf("  a javelin put through the middle of an axe's blade (its head volume r 9.8 cm, 9 cm off the shaft): after 0.3 s the javelin %.1f cm from the head's middle "
	            "(dual_collide off: %.1f cm)\n", blade * 100.0f, bladeOff * 100.0f);
	EXPECT(blade > kBladeR + 0.010f, "a javelin in the axe's blade comes out of it - the blade is steel, not air (%.3f m from its middle, the volume's radius %.3f)", blade, kBladeR);
	EXPECT(bladeOff < 0.03f, "(off: it stays in the blade - %.3f m)", bladeOff);
	weight::SetClashSound(&ClashSound);
	for (const bool left : {false, true})
		for (const float v : {1.0f, 2.0f, 4.0f, 6.0f}) {   // (his swings: 2 - 6 m/s)
			const CrossedOut on = Crossed(v, left), off = Crossed(v, left, false);
			std::printf("  crossed, %s at %.0f m/s: the javelin ends %.1f cm over the axe (closest %.1f cm, %zu knock(s)); dual_collide off %.1f cm\n",
			            left ? "the axe driven up into the javelin" : "the javelin driven down onto the axe", v, on.side * 100.0f, on.closest * 100.0f, on.sounds, off.side * 100.0f);
			EXPECT(on.side > 0.03f && on.sounds > 0, "crossed at %.0f m/s (%s): each stays on its own side and knocks (%.3f m, %zu)", v, left ? "the axe moving" : "the javelin moving", on.side, on.sounds);
			EXPECT(off.side < -0.1f, "(off at %.0f m/s: through, as before - %.3f m)", v, off.side);
		}
	weight::SetClashSound(nullptr);
}

// The user, 2026-10-28 (0e9d7fc8 in the headset): «the axe head's collision volume is much BIGGER than the drawn model, the other weapon stops well before touching
// the blade». His Hand Axe ('hax ', its model's unit 0.285 m as his log has it) in the left hand, its head as vr/weapon_head_parts.h has it (the slab of capsules
// where its mesh is) or as the one capsule round it (weapon_heads.h Row::cap); a sword driven onto the blade 11 cm over the shaft: how far the sword's line stays
// off the blade's own middle plane (the mesh's X = 0 - the blade's steel is 0.6 cm either side of it there). The model's X Y Z in the axe's body: X = x,
// Y (the shaft, to the head) = -z, Z (out of the shaft, the blade's way) = +y.
// What a sword driven onto the axe's blade does: where it first stops (its line off the blade's middle plane, beside the blade - the gap Serg sees), how close
// it ever got, and which side of the plane it ends on (through: it got past the blade - over it, round it or through it).
struct OnBlade { float stop = -1.0f; bool through = false; float y = 0.0f; };   // (y: how far out of the shaft the sword ends, level with the blade's middle)
OnBlade SwordOnBlade(bool parts, float past) {
	const float u = 0.285f;
	float p[weight::kHeadParts][7];
	int n = 0;
	const auto put = [&](const float* m) {   // model a, b, r -> the body's metres
		const float* a = m;
		const float* b = m + 3;
		p[n][0] = a[0] * u; p[n][1] = a[2] * u; p[n][2] = -a[1] * u;
		p[n][3] = b[0] * u; p[n][4] = b[2] * u; p[n][5] = -b[1] * u;
		p[n][6] = m[6] * u;
		++n;
	};
	const uint32_t hax = weaponextent::C('h', 'a', 'x', ' ');
	if (parts) {
		const weaponheadparts::Row* row = weaponheadparts::RowOf(hax);
		for (int i = 0; row && i < row->n; ++i) put(row->part[i]);
	} else if (const weaponhead::Row* row = weaponhead::RowOf(hax)) {
		put(row->cap);
	}
	Rig r(0, (int)heft::hold::Kind::Axe, (int)heft::hold::Kind::Sword);   // the sword in the right hand, the axe in the left
	g_snap = monsterbodies::Snapshot{};
	r.in.hand[0][1] += 0.11f;
	r.Steps(5);
	weight::SetDrawnHeadParts2(&p[0][0], n);
	r.Steps(3);
	OnBlade o;
	const float x0 = r.in.hand[0][0];
	const float drive = (x0 - r.in.hand[1][0]) + past;   // (the controller this far past the axe's plane)
	float side = 1.0f;
	for (int i = 0; i < 160; ++i) {   // at 1 m/s, then held there
		r.in.hand[0][0] = x0 - std::min(drive, 1.0f * 0.011f * (float)(i + 1));
		r.Step();
		float jb[3], jt[3];
		if (!weight::ShaftEnds(jb, jt)) continue;
		const heft::V3 org{r.out.body2[0], r.out.body2[1], r.out.body2[2]};
		const heft::Q q2{r.out.body2Rot[0], r.out.body2Rot[1], r.out.body2Rot[2], r.out.body2Rot[3]};
		const heft::V3 a{jb[0], jb[1], jb[2]}, b{jt[0], jt[1], jt[2]};
		double bestZ = 1e9;
		for (int k = 0; k <= 200; ++k) {
			const heft::V3 w = heft::Rotate(heft::QConj(q2), heft::Sub(heft::Add(a, heft::Mul(heft::Sub(b, a), k / 200.0)), org));   // (in the axe's body)
			if (std::fabs(-w.z - 1.45 * u) < bestZ) { bestZ = std::fabs(-w.z - 1.45 * u); side = (float)w.x; o.y = (float)w.y; }   // (level with the blade's middle along the shaft)
			if (-w.z < 1.24 * u || -w.z > 1.66 * u || w.y < 0.20 * u || w.y > 0.62 * u) continue;   // (beside the blade: its parts' own span)
			if (w.x > 0.0) o.stop = o.stop < 0.0f ? (float)w.x : std::min(o.stop, (float)w.x);
		}
	}
	o.through = side < 0.0f;
	weight::SetDrawnHeadParts2(nullptr, 0);
	r.in.weight.on = false;
	r.Step();
	return o;
}

void TestHandAxeTight() {
	const weaponheadparts::Row* row = weaponheadparts::RowOf(weaponextent::C('h', 'a', 'x', ' '));
	const float rPart = row ? row->part[row->n / 2][6] * 0.285f : 0.0f;
	// Where the sword first stops is what is seen: the drawn steel would stop its line 2.6 cm off the blade's middle (its own 2 cm, the steel's 0.6). Pressed on
	// further than a hand gives (10, 20 cm past), the sword pivots round the blade and slides off over its edge - the one capsule's way as well: the axe is not
	// shoved by it (the two weapons of a pair stop on each other, neither pushes the other); told, not judged.
	for (const float past : {0.05f, 0.10f, 0.20f}) {
		const OnBlade tight = SwordOnBlade(true, past), fat = SwordOnBlade(false, past);
		std::printf("  his Hand Axe (the mesh's head %.0f cm along the shaft, %.0f cm out, %.1f cm thick), a sword driven onto its blade, the controller %.0f cm past it: "
		            "its %d capsules stop it %.1f cm off the blade's middle%s; the one capsule round it %.1f cm%s (the drawn steel: 2.6 cm)\n",
		            row ? row->mesh[0] * 28.5f : 0.0f, row ? row->mesh[1] * 28.5f : 0.0f, row ? row->mesh[2] * 28.5f : 0.0f, past * 100.0f, row ? row->n : 0, tight.stop * 100.0f,
		            tight.through ? ", then it slid off over the blade's edge" : ", it stays on it", fat.stop * 100.0f, fat.through ? ", then over it" : "");
		EXPECT(tight.stop > 0.015f && tight.stop < 0.035f, "pressed %.0f cm past: the sword stops at the drawn blade - its line %.3f m off the blade's middle (the sword 0.02 + a part's %.3f)",
		       past * 100.0f, tight.stop, rPart);
		EXPECT(fat.stop > 2.0f * tight.stop, "(pressed %.0f cm past: the one capsule round it stopped the sword %.3f m off - the bug, over twice the parts' %.3f)", past * 100.0f,
		       fat.stop, tight.stop);
		if (past < 0.07f) EXPECT(!tight.through, "pressed %.0f cm past: the sword stays on the blade", past * 100.0f);
	}
	EXPECT(row && row->n > 1, "the Hand Axe's head is several capsules (%d)", row ? row->n : 0);
}

void TestDualHeads() {
	using K = heft::hold::Kind;
	weight::SetClashSound(&ClashSound);
	const float up[2][3] = {{0.0f, 0.03f, -0.50f}, {0.0f, 0.15f, -0.50f}};       // an axe's blade standing up off its shaft, 3 to 15 cm (r 4 cm), half a metre out
	const float down[2][3] = {{0.0f, -0.03f, -0.50f}, {0.0f, -0.15f, -0.50f}};   // ... hanging down off it
	const float round[2][3] = {{0.0f, 0.0f, -0.45f}, {0.0f, 0.0f, -0.55f}};      // a great mace's head round its shaft (r 13 cm)
	// the sword in the right hand, 11 cm above the left's axe (over its shaft, through its blade): it passes the shaft and the kind's head capsule, it stops on the blade
	const Cross axeNone = CrossInto(0, (int)K::Sword, (int)K::Axe, 0.11f, 1, false, up[0], up[1], 0.04f);
	const Cross axeHead = CrossInto(0, (int)K::Sword, (int)K::Axe, 0.11f, 1, true, up[0], up[1], 0.04f);
	const Cross axeOff = CrossInto(0, (int)K::Sword, (int)K::Axe, 0.11f, 1, true, up[0], up[1], 0.04f, false);
	// ... at the shaft's own height: it stops on the shaft, head or none (measured against the axe's shaft piece, -0.10 .. 0.45 along it, r 1.8 cm)
	const float shaftEnds[2][3] = {{0.0f, 0.0f, 0.10f}, {0.0f, 0.0f, -0.45f}};
	const Cross shaft = CrossInto(0, (int)K::Sword, (int)K::Axe, 0.0f, 1, true, up[0], up[1], 0.04f, true, shaftEnds[0], shaftEnds[1], 0.018f);
	const Cross shaftNone = CrossInto(0, (int)K::Sword, (int)K::Axe, 0.0f, 1, false, up[0], up[1], 0.04f, true, shaftEnds[0], shaftEnds[1], 0.018f);
	// ... 9 cm above a mace with a great round head
	const Cross maceNone = CrossInto(0, (int)K::Sword, (int)K::Mace, 0.09f, 1, false, round[0], round[1], 0.13f);
	const Cross maceHead = CrossInto(0, (int)K::Sword, (int)K::Mace, 0.09f, 1, true, round[0], round[1], 0.13f);
	// the other way: the axe in the right hand (the first weapon), 11 cm above the left's sword, its blade hanging down - the moving head stops on the sword
	const Cross moverNone = CrossInto(0, (int)K::Axe, (int)K::Sword, 0.11f, 0, false, down[0], down[1], 0.04f);
	const Cross moverHead = CrossInto(0, (int)K::Axe, (int)K::Sword, 0.11f, 0, true, down[0], down[1], 0.04f);
	// the first weapon in the LEFT hand (slot 1): the left's axe is the first, its head the first's - the right's sword (the second) stops on it
	const Cross leftNone = CrossInto(1, (int)K::Axe, (int)K::Sword, 0.11f, 0, false, up[0], up[1], 0.04f);
	const Cross leftFirst = CrossInto(1, (int)K::Axe, (int)K::Sword, 0.11f, 0, true, up[0], up[1], 0.04f);
	// (into: how deep the sword went into the head's volume at its deepest - through it, as deep as the head is thick and more; stopped on it, a centimetre or so)
	std::printf("  a sword 11 cm above an axe of the pair, driven across at 2 m/s - into its blade: no head volume %.1f cm (through), the blade its own volume %.1f cm "
	            "(%zu knock(s), pulses %d / %d), dual_collide off %.1f cm; at the shaft's height into the shaft %.1f cm, its hand held back %.0f cm (no head volume: %.1f cm, %.0f cm)\n", axeNone.into * 100.0f,
	            axeHead.into * 100.0f, axeHead.sounds, axeHead.pulses[0], axeHead.pulses[1], axeOff.into * 100.0f, shaft.into * 100.0f, shaft.held * 100.0f, shaftNone.into * 100.0f,
	            shaftNone.held * 100.0f);
	std::printf("  9 cm above a great mace - into its head: %.1f cm / its head as its volume %.1f cm (%zu knock(s)); the axe in the right hand driven over the left's sword: "
	            "%.1f / %.1f cm (%zu knock(s)); the axe the first weapon, in the left hand: %.1f / %.1f cm (%zu knock(s))\n", maceNone.into * 100.0f, maceHead.into * 100.0f,
	            maceHead.sounds, moverNone.into * 100.0f, moverHead.into * 100.0f, moverHead.sounds, leftNone.into * 100.0f, leftFirst.into * 100.0f, leftFirst.sounds);
	const float kThrough = 0.04f, kStopped = 0.02f;   // (the sword's own radius is 2 cm: a centimetre or two in is the contact's own give)
	EXPECT(axeNone.into > kThrough, "(the bug, without the head volume: the sword passes through the axe's blade - %.3f m in)", axeNone.into);
	EXPECT(axeHead.into < kStopped, "the sword stops on the axe's blade beside its shaft (%.3f m in)", axeHead.into);
	EXPECT(axeHead.sounds > 0 && axeHead.pulses[0] > 0 && axeHead.pulses[1] > 0, "... a knock: heard (%zu) and felt in both hands (%d / %d)", axeHead.sounds, axeHead.pulses[0], axeHead.pulses[1]);
	EXPECT(axeOff.into > kThrough && axeOff.sounds == 0, "dual_collide off: it passes as before (%.3f m, %zu)", axeOff.into, axeOff.sounds);
	EXPECT(shaft.into < kStopped && shaft.sounds > 0, "at the shaft's height it still stops on the shaft and knocks (%.3f m in, %zu)", shaft.into, shaft.sounds);
	EXPECT(maceNone.into > kThrough, "(without the head volume the sword passes through the mace's great head - %.3f m in)", maceNone.into);
	EXPECT(maceHead.into < kStopped && maceHead.sounds > 0, "the sword stops on the mace's head round its shaft and knocks (%.3f m, %zu)", maceHead.into, maceHead.sounds);
	EXPECT(moverNone.into > kThrough, "(without the head volume the axe's hanging blade passes through the sword - %.3f m in)", moverNone.into);
	EXPECT(moverHead.into < kStopped && moverHead.sounds > 0, "the axe's own blade, moving, stops on the other's sword and knocks (%.3f m, %zu)", moverHead.into, moverHead.sounds);
	EXPECT(leftNone.into > kThrough, "(without the head volume the right's sword passes through the left axe's blade - %.3f m in)", leftNone.into);
	EXPECT(leftFirst.into < kStopped && leftFirst.sounds > 0, "the first weapon's head in the left hand: the right's sword stops on it (%.3f m, %zu)", leftFirst.into, leftFirst.sounds);

	// two axes, head on head: the right's 22 cm above the left's, its blade hanging down (to 5 cm over the left's shaft), the left's standing up (to 5 cm under the
	// right's shaft) - only the two blades cross: without their volumes they pass each other, with them the right's blade stops on the left's and knocks
	{
		const float hangs[2][3] = {{0.0f, -0.03f, -0.50f}, {0.0f, -0.13f, -0.50f}}, stands[2][3] = {{0.0f, 0.03f, -0.50f}, {0.0f, 0.13f, -0.50f}};
		float into[2] = {-1.0f, -1.0f};
		size_t sounds[2] = {};
		for (int told = 0; told < 2; ++told) {
			Rig r(0, (int)K::Axe, (int)K::Axe);
			g_snap = monsterbodies::Snapshot{};
			r.in.hand[0][1] += 0.22f;
			r.Steps(5);
			if (told) {
				weight::SetDrawnHead(hangs[0], hangs[1], 0.04f);
				weight::SetDrawnHead2(stands[0], stands[1], 0.04f);
			}
			r.Steps(3);
			Clear();
			g_clashes.clear();
			const float x0 = r.in.hand[0][0];
			for (int i = 0; i < 260; ++i) {
				r.in.hand[0][0] = x0 - std::min(0.7f, 2.0f * 0.011f * (float)(i + 1));
				r.Step();
				const heft::V3 ra = OnBody(r.out.body, r.out.bodyRot, {hangs[0][0], hangs[0][1], hangs[0][2]}), rb = OnBody(r.out.body, r.out.bodyRot, {hangs[1][0], hangs[1][1], hangs[1][2]});
				const heft::V3 la = OnBody(r.out.body2, r.out.body2Rot, {stands[0][0], stands[0][1], stands[0][2]}), lb2 = OnBody(r.out.body2, r.out.body2Rot, {stands[1][0], stands[1][1], stands[1][2]});
				double s, t;
				heft::contact::ClosestOnSegments(ra, rb, la, lb2, &s, &t);
				into[told] = std::max(into[told], (float)(0.08 -heft::Len(heft::Sub(heft::Add(ra, heft::Mul(heft::Sub(rb, ra), s)), heft::Add(la, heft::Mul(heft::Sub(lb2, la), t))))));
				if (2.0f * 0.011f * (float)i > 0.7f) break;
			}
			r.Steps(10);
			sounds[told] = g_clashes.size();
			weight::SetDrawnHead(nullptr, nullptr, 0.0f);
			weight::SetDrawnHead2(nullptr, nullptr, 0.0f);
			r.in.weight.on = false;
			r.Step();
		}
		std::printf("  two axes, the right's blade driven across the left's at 2 m/s - one into the other: no head volumes %.1f cm (through, %zu knock(s)), their own volumes %.1f cm "
		            "(%zu knock(s))\n", into[0] * 100.0f, sounds[0], into[1] * 100.0f, sounds[1]);
		EXPECT(into[0] > kThrough && sounds[0] == 0, "(without the head volumes the two axes' blades pass through each other - %.3f m in, %zu)", into[0], sounds[0]);
		EXPECT(into[1] < kStopped && sounds[1] > 0, "head on head: the right axe's blade stops on the left's and knocks (%.3f m in, %zu)", into[1], sounds[1]);
	}
	weight::SetClashSound(nullptr);

	// the head's own volume is for the other weapon of the pair only: a monster's limb it alone would touch is neither met nor pushed by it (as before)
	{
		Rig r(0, (int)K::Sword, (int)K::Axe);
		g_snap = monsterbodies::Snapshot{};
		r.in.hand[0][1] += 0.11f;
		r.Steps(5);
		weight::SetDrawnHead(down[0], down[1], 0.04f);
		const float y = r.in.hand[0][1] - 0.17f;   // under the axe's shaft by more than the kind's head capsule reaches, within its hanging blade
		g_snap.n = 1;
		monsterbodies::Body& lb = g_snap.b[0];
		lb = monsterbodies::Body{};
		lb.id = 3;
		lb.bottom[0] = lb.top[0] = 0.10f;
		lb.bottom[1] = lb.top[1] = y;
		lb.bottom[2] = -0.4f;
		lb.top[2] = -1.2f;
		lb.radius = 0.07f;
		lb.rig = true;
		lb.bone = 5;
		lb.doll = true;
		r.Steps(3);
		Clear();
		const float x0 = r.in.hand[0][0];
		float held = 0.0f, through = -1.0f;
		for (int i = 0; i < 20; ++i) {   // 25 cm on: across the limb, short of the left's sword
			r.in.hand[0][0] = x0 - std::min(0.25f, 2.0f * 0.011f * (float)(i + 1));
			r.Step();
			held = std::max(held, r.drawn.hand[0][0] - r.in.hand[0][0]);
			const heft::V3 ha = OnBody(r.out.body, r.out.bodyRot, {down[0][0], down[0][1], down[0][2]}), hb = OnBody(r.out.body, r.out.bodyRot, {down[1][0], down[1][1], down[1][2]});
			const heft::V3 la{lb.bottom[0], lb.bottom[1], lb.bottom[2]}, lt{lb.top[0], lb.top[1], lb.top[2]};
			double s, t;
			heft::contact::ClosestOnSegments(ha, hb, la, lt, &s, &t);
			through = std::max(through, (float)(0.04 + lb.radius - heft::Len(heft::Sub(heft::Add(ha, heft::Mul(heft::Sub(hb, ha), s)), heft::Add(la, heft::Mul(heft::Sub(lt, la), t))))));
		}
		std::printf("  the axe's hanging blade swept %.0f cm deep through a monster's limb only it would reach: pushed %d time(s), held back %.0f cm\n", through * 100.0f, g_pushes[3], held * 100.0f);
		EXPECT(through > 0.05f, "(the test's own: the head went through the limb's place - %.2f m)", through);
		EXPECT(g_pushes[3] == 0 && held < 0.03f, "the head volume meets no monster (pushed %d, held %.2f m)", g_pushes[3], held);
		weight::SetDrawnHead(nullptr, nullptr, 0.0f);
		g_snap = monsterbodies::Snapshot{};
		r.in.weight.on = false;
		r.Step();
	}
	bool told = false;
	for (const std::string& l : g_log) told = told || l.find("head is a volume of its own against the other weapon") != std::string::npos;
	EXPECT(told, "the log says the head is a volume of its own");
	g_log.clear();
}

}   // namespace

// [hands] throw_left_hand, two javelins (Serg, 2026-10-28): the LEFT one thrown by the game must leave the hand as the right one does - its hand empty (the model
// not drawn) while the game's own missile flies, then a fresh one in it - not the held javelin falling to the floor beside the game's throw.
int g_heroModeNow = -1;
int HeroModeNow() { return g_heroModeNow; }
void TestSecondThrown() {
	const int jav = (int)heft::hold::Kind::Javelin;
	weight::SetHeroModeSource(&HeroModeNow);
	{   // the game took it: hidden from the throw, through the throw animation (the hero in mode 11), a fresh one in its hand as the animation is over
		Rig r(0, jav, jav);
		g_snap = monsterbodies::Snapshot{};
		g_heroModeNow = 1;
		r.Steps(5);
		EXPECT(r.out.second && !r.out.free2 && !r.out.hidden2 && weight::SecondPlaced() && weight::FirstSlot() == 0, "two javelins: the second a body in its hand");
		EXPECT(weight::HoldersOf(0) == 1u && weight::HoldersOf(1) == 2u, "by slot: the right on the first, the left on the second (%u %u)", weight::HoldersOf(0), weight::HoldersOf(1));
		Grip(r, 1, 1.0f, 6);
		weight::SecondThrown();   // (the throw's press can come before the weight model has seen the grip open)
		g_heroModeNow = 11;
		Grip(r, 1, 0.0f, 6);
		EXPECT(r.out.hidden2, "thrown: the left hand's javelin is not drawn (the hand empty)");
		EXPECT(!r.out.hidden && weight::CarryingHand() == 0, "the right one stays as it is (hidden %d, carrying %d)", r.out.hidden ? 1 : 0, weight::CarryingHand());
		r.Steps(20);
		EXPECT(r.out.hidden2, "still empty while the game throws");
		g_heroModeNow = 1;   // the throw animation over
		r.Steps(20);
		EXPECT(!r.out.hidden2 && !r.out.free2 && weight::SecondHand() == 1, "the animation over: a fresh one in the left hand (hidden %d free %d hand %d)",
		       r.out.hidden2 ? 1 : 0, r.out.free2 ? 1 : 0, weight::SecondHand());
		bool told = false;
		for (const std::string& l : g_log) told = told || l.find("the next javelin (the second weapon) is drawn in it") != std::string::npos;
		EXPECT(told, "the log says the next one is drawn");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	{   // the hero's mode not known: the cap ([weight] thrown_hand_empty_ms) ends it
		Rig r(0, jav, jav);
		r.in.weight.thrownHandEmptyMs = 200.0f;
		g_snap = monsterbodies::Snapshot{};
		g_heroModeNow = -1;
		r.Steps(5);
		Drop(r, 1);
		weight::SecondThrown();
		r.Steps(2);
		EXPECT(r.out.hidden2, "thrown (no mode known): empty");
		Sleep(220);
		r.Steps(3);
		EXPECT(!r.out.hidden2 && !r.out.free2, "after the cap: a fresh one in its hand");
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	{   // not thrown (let go at rest: a drop): it falls and is drawn where it lies, as ever
		Rig r(0, jav, jav);
		g_snap = monsterbodies::Snapshot{};
		r.Steps(5);
		Drop(r, 1);
		EXPECT(r.out.free2 && !r.out.hidden2, "dropped, not thrown: drawn where it falls (free %d hidden %d)", r.out.free2 ? 1 : 0, r.out.hidden2 ? 1 : 0);
		r.in.weight.on = false;
		r.Step();
		g_log.clear();
	}
	weight::SetHeroModeSource(nullptr);
	g_heroModeNow = -1;
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {   // dll=<d2r_vr_weight.dll>, grip=<d2r_vr_grip.dll>: through the libraries (hands_model 1, grip_model 1), as the game runs it
		wchar_t w[512] = {};
		const char* a = argv[i];
		const bool lib = std::strncmp(a, "dll=", 4) == 0, grip = std::strncmp(a, "grip=", 5) == 0;
		if (!lib && !grip) continue;
		a += lib ? 4 : 5;
		for (size_t k = 0; k < 511 && a[k]; ++k) w[k] = (wchar_t)(unsigned char)a[k];
		if (lib) { weightlib::SetPath(w); g_lib = weightlib::Ensure(); std::printf("  the weight library: %s\n", g_lib ? weightlib::Name() : "NOT LOADED"); }
		else { griplib::SetPath(w); g_grip = griplib::Ensure(); std::printf("  the grip library: %s\n", g_grip ? griplib::Name() : "NOT LOADED"); }
		if (!(lib ? g_lib : g_grip)) ++g_fails;
	}
	std::printf("weight_dual_test: a weapon in each hand, each a body of its own\n");
	weight::SetBodiesSource(&Bodies);
	weight::SetBodyPushSink(&Push);
	weight::SetHaptic(&Pulse);
	weight::SetLog(&LogLine);
	TestDual();
	TestAlone(0);
	TestAlone(1);
	TestAlone(0, (int)heft::hold::Kind::Dagger);
	TestShield();
	TestAlone(1, (int)heft::hold::Kind::Dagger);   // the right hand empty, a dagger in the left (the headset 2026-10-23: «если в правой руке пусто, а в левой кинжал ... проходит насквозь»)
	TestDualDrop();
	TestDualBack();
	TestDropThenTakeFirst();
	TestOneAHand();
	TestFullHandTakesNone();
	TestHisSequence();
	TestSecondStab();
	TestLiveStrikes();
	TestDualCollide();
	TestDualHeads();
	TestSecondThrown();
	TestHandAxeTight();
	TestCrossed();
	TestSecondInPalm();   // (last: it leaves the second sword's shaft measured off its line, as the game keeps it for the same weapon)
	// each sword of a pair does what the same sword does alone in that hand (the headset: one weapon resists - two must as well)
	for (int h = 0; h < 2; ++h)
		EXPECT(std::fabs(g_dualBack[h] - g_aloneBack[h]) < 0.02f && std::fabs(g_dualPush[h] - g_alonePush[h]) < 0.15f * g_alonePush[h],
		       "the %s sword of a pair as alone: held back %.2f vs %.2f m, pushed %.1f vs %.1f N s", h ? "left" : "right", g_dualBack[h], g_aloneBack[h], g_dualPush[h], g_alonePush[h]);
	weight::SetBodiesSource(nullptr);
	weight::SetBodyPushSink(nullptr);
	weight::SetHaptic(nullptr);
	weight::SetLog(nullptr);
	if (g_fails) {
		std::printf("weight_dual_test: %d FAILED\n", g_fails);
		for (const std::string& l : g_log) std::printf("    log: %s\n", l.c_str());
		return 1;
	}
	std::printf("weight_dual_test: all passed\n");
	return 0;
}
