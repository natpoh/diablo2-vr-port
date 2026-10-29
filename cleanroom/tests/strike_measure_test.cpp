// [debug] strike_measure (vr/strike_measure.h; cleanroom/heft/contact.h Report::step*; docs/notes/strike_force.md): what a strike delivers into a body, on synthetic
// poses, without the game. The contact's own step totals (a weapon's momentum into a body is its mass times its speed); the tracker's strikes (arrival, drive,
// window, cut, stab, the gap, another monster, a long rest); and end to end in the weight model's hold: a light tap delivers less than a hard blow, two hands drive
// harder than one, a slide along the body is a cut, a weapon let go delivers nothing.

#include "contact.h"
#include "hold.h"
#include "../../vr/stick_strength.h"
#include "../../vr/strike_measure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace heft;
namespace hh = heft::hold;
namespace hc = heft::contact;
namespace hs = heft::stab;
namespace sm = strikemeasure;

namespace {

constexpr double kDt = 1.0 / 90.0;
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

hc::Capsule Monster(double x, double z, double r, uint32_t id = 7) {
	hc::Capsule c;
	c.a = { x, 0.3, z };
	c.b = { x, 1.5, z };
	c.radius = r;
	c.id = id;
	return c;
}

// The contact's step totals: a 2 kg ball of a weapon (its centre on the blade) at 3 m/s into a body - the velocity taken out is its momentum.
void TestStepTotals() {
	Body b;
	b.mass = 2.0;
	b.inertia.m[0][0] = b.inertia.m[1][1] = b.inertia.m[2][2] = 0.05;
	State st;
	st.com = { 0, 1.0, -0.62 };   // (the blade's point 3 cm into the body's front)
	st.vel = { 0, 0, -3.0 };
	State before = st;
	before.com = { 0, 1.0, -0.59 };
	hc::Blade blade { { 0, 0, 0.2 }, { 0, 0, 0 }, 0.02 };   // a short blade ending at the centre
	const hc::Capsule c = Monster(0, -0.8, 0.17);
	hc::Report rep;
	hc::Resolve(st, &before, b, &blade, 1, &c, 1, hc::Params {}, 0, &rep);
	std::printf("  the contact: a 2 kg weapon at 3 m/s - %.2f N s taken into the body (its momentum %.2f), %.3f kg m put back\n", rep.stepImpulse, 2.0 * 3.0, rep.stepPush);
	EXPECT(rep.bodies == 1 && std::fabs(rep.stepImpulse - 6.0) < 0.05, "the momentum into the body: %.3f N s", rep.stepImpulse);
	EXPECT(rep.stepPush > 0 && rep.stepFriction == 0 && rep.stepSlid < 1e-9, "put back out, no slide straight in");
}

void TestTracker() {
	sm::Tracker tr;
	sm::Step s;
	s.dt = kDt;
	s.hands = 1;
	s.id = 7;
	s.approach = 2.5;
	s.impulse = 3.0;            // the arrival
	s.t = 1.0;
	EXPECT(!tr.Feed(s) && tr.On(), "a strike begins");
	for (int i = 1; i <= 10; ++i) {   // the hands drive it on: 0.2 N s a step, sliding 1 cm a step
		s.t = 1.0 + i * kDt;
		s.impulse = 0.2;
		s.slid = 0.01;
		s.approach = 0;
		s.hands = 2;
		EXPECT(!tr.Feed(s), "still on");
	}
	s.id = 0;
	s.impulse = 0;
	s.slid = 0;
	s.t += kDt;
	EXPECT(!tr.Feed(s), "a step without contact: not over yet");
	s.t += sm::kGapS + 0.01;
	EXPECT(tr.Feed(s) && !tr.On(), "a gap: over");
	const sm::Strike& k = tr.Last();
	std::printf("  a strike: arrival %.2f N s at %.1f m/s, drive %.2f N s, total %.2f (%.2f in the window), cut %.1f J over %.0f cm, peak %.0f N, %d hands, %d steps\n",
	            k.arrival, k.approach, k.drive, k.Total(), k.window, k.cut, k.slid * 100, k.peakForce, k.hands, k.steps);
	EXPECT(k.id == 7 && std::fabs(k.arrival - 3.0) < 1e-9 && std::fabs(k.approach - 2.5) < 1e-9 && std::fabs(k.drive - 2.0) < 1e-9 && k.hands == 2 && k.steps == 11,
	       "the arrival, then the drive");
	EXPECT(std::fabs(k.cut - 10 * (0.2 / kDt) * 0.01) < 1e-6 && std::fabs(k.slid - 0.10) < 1e-9, "the cut: the normal force times the slide (%.3f J)", k.cut);
	EXPECT(std::fabs(k.peakForce - 3.0 / kDt) < 1e-6, "the peak force: the arrival's step");
	EXPECT(std::fabs(k.window - (3.0 + 0.2 * std::min(10.0, std::floor(sm::kWindowS / kDt + 1e-6)))) < 1e-6, "the window: the first %.0f ms (%.3f)", sm::kWindowS * 1000, k.window);
	// another monster: the strike on the first ends
	sm::Tracker t2;
	sm::Step a = s;
	a.id = 7;
	a.impulse = 1.0;
	a.t = 5.0;
	t2.Feed(a);
	a.id = 8;
	a.t += kDt;
	EXPECT(t2.Feed(a) && t2.Last().id == 7 && t2.On(), "another monster: the first strike ends, the next begins");
	// a weapon in a wound: the stab's joint force
	sm::Tracker t3;
	sm::Step w;
	w.id = 9;
	w.dt = kDt;
	w.stabForce = 90.0;
	for (int i = 0; i < 9; ++i) { w.t = 10 + i * kDt; t3.Feed(w); }
	w.id = 0;
	w.stabForce = 0;
	w.t += 1.0;
	EXPECT(t3.Feed(w) && std::fabs(t3.Last().stab - 9 * 90.0 * kDt) < 1e-9, "the stab's push over its steps (%.2f N s)", t3.Last().stab);
	// resting on a body: told after a second, and begun again
	sm::Tracker t4;
	sm::Step rst;
	rst.id = 4;
	rst.dt = kDt;
	rst.impulse = 0.05;
	bool told = false;
	for (int i = 0; i < 120 && !told; ++i) { rst.t = 20 + i * kDt; told = t4.Feed(rst); }
	EXPECT(told && t4.On(), "a weapon resting on a body: told after %.0f s, and on again", sm::kLongestS);
}

// End to end in the hold: the controller's path at a monster, the strikes measured.
struct Blow {
	hh::Kind kind = hh::Kind::Mace;
	double second = 0;   // a second hand this far on along the shaft (0: one hand)
	double letGoAt = 1e9;
	double wedge = 1;   // the friction times this while caught between several monsters (hold Settings::wedgeFriction)
	V3 tip0, tip1;      // the weapon's tip at the start and at the end
	std::function<V3(double)> ctl;
	double secs = 1.2;
	std::vector<sm::Strike> strikes;
};
sm::Strike Hardest(const Blow& b) {
	sm::Strike k;
	for (const sm::Strike& s : b.strikes)
		if (s.Total() > k.Total()) k = s;
	return k;
}
hh::HandIn HandAt(V3 p, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose = { p, {} };
	h.grip = tracked ? 1.0f : 0.0f;
	h.fistLine = { 0, 0, -1 };
	return h;
}
void Play(Blow& b, const std::vector<hc::Capsule>& bodies) {
	hh::Settings s;
	s.bodies = true;
	s.wedgeFriction = b.wedge;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt(b.ctl(0)), HandAt({ -0.3, 1.0, 0 }, false) };
	in[1].grip = 0;
	h.Place(0, b.kind, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	if (b.second > 0) {
		in[1] = HandAt(Add(b.ctl(0), { 0, 0, -b.second }));
		for (int i = 0; i < 3; ++i) h.Step(in, kDt, s, out);
	}
	h.Ends(nullptr, &b.tip0);
	sm::Strikes tr;
	sm::Strike ended[sm::Strikes::kMax];
	for (double t = kDt; t <= b.secs + 1e-9; t += kDt) {
		in[0] = HandAt(b.ctl(t));
		if (b.second > 0) in[1] = HandAt(Add(b.ctl(t), { 0, 0, -b.second }));
		if (t >= b.letGoAt) in[0].grip = in[1].grip = 0;
		h.SetBodies(bodies.data(), (int)bodies.size());
		h.Step(in, kDt, s, out);
		const hc::Report& c = h.LastContact();
		sm::Step st[sm::Strikes::kMax];
		int n = 0;
		for (int i = 0; c.bodies > 0 && i < c.nMet && n < sm::Strikes::kMax; ++i, ++n) {   // (as vr/weight.cpp: every monster met, each its own)
			st[n].t = t;
			st[n].dt = kDt;
			st[n].handMask = (out[0].holds ? 1u : 0u) | (out[1].holds ? 2u : 0u);
			st[n].hands = (out[0].holds ? 1 : 0) + (out[1].holds ? 1 : 0);
			st[n].id = c.met[i].id;
			st[n].impulse = c.met[i].impulse;
			st[n].friction = c.met[i].friction;
			st[n].slid = c.met[i].slid;
			st[n].push = c.met[i].push;
			st[n].approach = c.met[i].approach;
			st[n].crowd = c.nMet;
		}
		const int ne = tr.Feed(st, n, t, kDt, ended);
		for (int i = 0; i < ne; ++i) b.strikes.push_back(ended[i]);
	}
	h.Ends(nullptr, &b.tip1);
	const int ne = tr.Feed(nullptr, 0, b.secs + 1.0, kDt, ended);
	for (int i = 0; i < ne; ++i) b.strikes.push_back(ended[i]);
}
double TipZ(hh::Kind kind) {
	hh::Settings s;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt({ 0, 1.2, 0 }), HandAt({ -0.3, 1.0, 0 }, false) };
	in[1].grip = 0;
	h.Place(0, kind, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	V3 tip;
	h.Ends(nullptr, &tip);
	return tip.z;
}
double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}

void TestBlows() {
	const double r = 0.17, zb = TipZ(hh::Kind::Mace) - 0.15 - r;   // the body's front 15 cm before the mace's tip (its head's ball short of it)
	const std::vector<hc::Capsule> body { Monster(0, zb, r) };
	Blow tap;   // a light tap: the hand 20 cm on, slowly, and back
	tap.ctl = [](double t) { return V3 { 0, 1.2, -0.20 * Smooth(t / 0.4) * (t < 0.6 ? 1.0 : 1.0 - Smooth((t - 0.6) / 0.3)) }; };
	Play(tap, body);
	Blow hard = tap;   // a hard blow: the hand 50 cm on into the body in 0.2 s, held a moment
	hard.strikes.clear();
	hard.ctl = [](double t) { return V3 { 0, 1.2, -0.50 * Smooth(t / 0.2) * (t < 0.5 ? 1.0 : 1.0 - Smooth((t - 0.5) / 0.3)) }; };
	Play(hard, body);
	Blow two = hard;   // the same with both hands on it
	two.strikes.clear();
	two.second = 0.12;
	Play(two, body);
	const sm::Strike kt = Hardest(tap), kh = Hardest(hard), k2 = Hardest(two);
	std::printf("  a mace: a light tap %.2f N s (arrival %.2f, drive %.2f); a hard blow %.2f (arrival %.2f at %.1f m/s, drive %.2f); with both hands %.2f (drive %.2f)\n",
	            kt.window, kt.arrival, kt.drive, kh.window, kh.arrival, kh.approach, kh.drive, k2.window, k2.drive);
	EXPECT(!tap.strikes.empty() && !hard.strikes.empty() && kh.window > 2.0 * kt.window, "a hard blow delivers far more than a light tap (%.2f vs %.2f N s)", kh.window, kt.window);
	EXPECT(k2.hands == 2 && k2.drive > kh.drive * 1.2, "two hands drive harder than one (%.2f vs %.2f N s)", k2.drive, kh.drive);
	// a slide along the body: a sword drawn across its front, pressed on - a cut
	const double zs = TipZ(hh::Kind::Sword);
	Blow cut;
	cut.kind = hh::Kind::Sword;
	cut.ctl = [](double t) { return V3 { -0.5 + 1.0 * Smooth(t / 0.6), 1.2, 0 }; };
	Play(cut, { Monster(0, zs * 0.5, 0.17) });
	const sm::Strike kc = Hardest(cut);
	std::printf("  a sword drawn across a body: %.2f N s, a cut of %.1f J over %.0f cm\n", kc.Total(), kc.cut, kc.slid * 100);
	EXPECT(!cut.strikes.empty() && kc.cut > 0 && kc.slid > 0.05, "a slide along the body is a cut (%.2f J over %.3f m)", kc.cut, kc.slid);
	// a weapon let go over the body: it falls through - nothing delivered
	Blow drop = tap;
	drop.strikes.clear();
	drop.ctl = [](double) { return V3 { 0, 1.2, 0 }; };
	drop.letGoAt = 0.1;
	Play(drop, { Monster(0, -0.5, 0.17) });
	std::printf("  a weapon let go over a body: %zu strikes\n", drop.strikes.size());
	EXPECT(drop.strikes.empty(), "a dropped weapon delivers nothing (%zu strikes)", drop.strikes.size());
	// a crowd: the hard blow into the gap between two monsters side by side - the mace's head caught between them, each takes its own share and is its own strike
	Blow gap = hard;
	gap.strikes.clear();
	const double zg = TipZ(hh::Kind::Mace) - 0.15;
	Play(gap, { Monster(-(r + 0.02), zg, r, 7), Monster(r + 0.02, zg, r, 8) });
	sm::Strike g7, g8;
	for (const sm::Strike& k : gap.strikes) {
		if (k.id == 7 && k.Total() > g7.Total()) g7 = k;
		if (k.id == 8 && k.Total() > g8.Total()) g8 = k;
	}
	std::printf("  a hard blow caught between two monsters: monster 7 %.2f N s in the window (among %d), monster 8 %.2f (among %d)\n", g7.window, g7.crowd, g8.window, g8.crowd);
	EXPECT(g7.id == 7 && g8.id == 8 && g7.window > 0.15 * kh.window && g8.window > 0.15 * kh.window, "each monster of the two its own strike, a share each (less than alone: the wedge) (%.2f, %.2f of %.2f)",
	       g7.window, g8.window, kh.window);
	EXPECT(g7.crowd == 2 && g8.crowd == 2, "told among two (%d, %d)", g7.crowd, g8.crowd);
}

// The strikes on several monsters at once: each its own, ended by its own gap.
void TestStrikes() {
	sm::Strikes ss;
	sm::Strike ended[sm::Strikes::kMax];
	std::vector<sm::Strike> got;
	for (int i = 0; i < 20; ++i) {
		sm::Step st[2];
		int n = 0;
		const double t = 30 + i * kDt;
		if (i < 15) { st[n].id = 7; st[n].impulse = 1.0; st[n].t = t; st[n].dt = kDt; ++n; }   // monster 7: 15 steps
		if (i < 5) { st[n].id = 8; st[n].impulse = 0.5; st[n].t = t; st[n].dt = kDt; ++n; }    // monster 8: the first 5, then a gap
		const int ne = ss.Feed(st, n, t, kDt, ended);
		got.insert(got.end(), ended, ended + ne);
	}
	const int ne = ss.Feed(nullptr, 0, 40, kDt, ended);
	got.insert(got.end(), ended, ended + ne);
	EXPECT(got.size() == 2 && got[0].id == 8 && got[0].steps == 5 && got[1].id == 7 && got[1].steps == 15 && std::fabs(got[1].Total() - 15.0) < 1e-9,
	       "two monsters, each its own strike (%zu strikes)", got.size());
}

// [weight] stick_by_strength, stick_pct (vr/stick_strength.h): how hard a weapon sticks by the class' multiplier - the curve for each class, what it does to the stab and
// to a blade drawn along a body.
void TestStick() {
	struct Cls { const char* name; double pct; };
	const Cls classes[] = { { "Barbarian", 200 }, { "Barbarian (code default)", 130 }, { "Paladin", 110 }, { "Amazon / Assassin", 100 }, { "Druid / Warlock", 95 },
	                        { "Necromancer", 90 }, { "Sorceress", 80 } };
	hs::Params base = hs::FromSliders(hs::Sliders { 0.5, 60, 30, 0, 45, 110 });   // (the author's stab sliders)
	std::printf("  how hard a weapon sticks, by the class (strength matters 100 %%, stick_pct 100):\n");
	double barb = 0, sorc = 0;
	for (const Cls& c : classes) {
		const double f = stickstr::Factor(true, c.pct, 100, 100);
		const hs::Params p = stickstr::Apply(base, f);
		std::printf("    %-26s %3.0f %%: x%.2f - resistance %3.0f %%, frees at %3.0f N in %.2f s, tears free %2.0f cm aside; caught between two monsters the friction x%.2f\n", c.name,
		            c.pct, f, 100.0 * p.fleshDamper / hs::Params {}.fleshDamper, p.unlockForce, p.lockTime, p.breakM * 100, stickstr::Friction(1.0, f));
		if (c.pct == 200) barb = f;
		if (c.pct == 80) sorc = f;
	}
	EXPECT(barb < 0.15 && sorc > 1.8 && std::fabs(stickstr::Factor(true, 100, 100, 100) - 1.0) < 1e-9, "a Barbarian about a tenth, a Sorceress about twice, 100 %% as before");
	EXPECT(std::fabs(stickstr::Factor(false, 200, 100, 100) - 1.0) < 1e-9 && std::fabs(stickstr::Factor(true, 200, 0, 100) - 1.0) < 1e-9, "unticked, or strength not mattering: as before");
	EXPECT(std::fabs(stickstr::Factor(true, 200, 50, 100) - std::pow(1.5, -stickstr::kExponent)) < 1e-9, "strength half mattering: half the margin (x%.2f)", stickstr::Factor(true, 200, 50, 100));
	EXPECT(stickstr::Factor(true, 100, 100, 0) == 0.0 && stickstr::Apply(base, 0).unlockForce == 0.0, "stick_pct 0: nothing locks");
	EXPECT(stickstr::Apply(base, 1.0).unlockForce == base.unlockForce && stickstr::Apply(base, 1.0).breakM == base.breakM, "x1: the stab's sliders as they are");
	EXPECT(stickstr::Apply(base, 4.0).unlockForce < 300.0, "a stab held hardest still frees under the hands' 300 N (%.0f N)", stickstr::Apply(base, 4.0).unlockForce);
	// on one body the class changes nothing: a sword drawn across a body, pressed on, slides as far for a Barbarian as for a Sorceress (the surface stops and holds it
	// the same - the hit and the weight are felt: "the weapon passes through the monsters", the headset 2026-10-23, when it did not)
	const double zs = TipZ(hh::Kind::Sword);
	double slid[2] = {}, out[2] = {};
	for (int k = 0; k < 2; ++k) {
		Blow cut;
		cut.kind = hh::Kind::Sword;
		cut.wedge = stickstr::Friction(1.0, k == 0 ? barb : sorc);
		cut.ctl = [](double t) { return V3 { -0.5 + 1.0 * Smooth(t / 0.6), 1.2, 0 }; };
		Play(cut, { Monster(0, zs * 0.5, 0.17) });
		for (const sm::Strike& s : cut.strikes) slid[k] += s.slid;
		// caught between two monsters side by side (the blade in the gap, touching both), drawn back out along itself
		Blow pull;
		pull.kind = hh::Kind::Sword;
		pull.wedge = cut.wedge;
		pull.secs = 0.6;
		pull.ctl = [](double t) { return V3 { 0, 1.2, 0.30 * Smooth(t / 0.4) }; };
		Play(pull, { Monster(-0.185, zs * 0.6, 0.17, 7), Monster(0.185, zs * 0.6, 0.17, 8) });
		out[k] = pull.tip1.z - pull.tip0.z;   // (how far the blade came back out of the gap)
	}
	std::printf("  a sword drawn 1 m across one body: a Barbarian's slides %.0f cm along it, a Sorceress' %.0f cm (the same)\n", slid[0] * 100, slid[1] * 100);
	std::printf("  a sword caught between two monsters, the hand drawn back 30 cm: the blade comes %.0f cm out for a Barbarian, %.0f cm for a Sorceress\n", out[0] * 100, out[1] * 100);
	EXPECT(std::fabs(slid[0] - slid[1]) < 1e-9, "one body holds the weapon the same for every class (%.3f vs %.3f m)", slid[0], slid[1]);
	EXPECT(out[0] > out[1] + 0.02, "caught between two, a strong class' blade slides out more freely (%.3f vs %.3f m)", out[0], out[1]);
}

// A one-handed sword swung across a body at 3-12 m/s must stop on it (or glance along it), not pass through: the headset 2026-10-23, «спира спотыкается об
// монстров, а одноручное оружие проходит насквозь, даже не замедляясь». The blade moves 3-9 cm a step at 90 Hz - more than a thin limb lets in before it counts
// the blade as "deep inside already" (contact.h Params::deepShare), so the contact sweeps the step (contact.cpp).
void TestSwingThrough() {
	const double zs = TipZ(hh::Kind::Sword);
	struct Body { const char* name; double r; };
	const Body bodies[] = { { "a fallen's forearm", 0.04 }, { "a fallen's arm", 0.05 }, { "a fallen's leg", 0.07 }, { "a zombie's body", 0.12 } };
	for (const Body& bd : bodies)
		for (double v : { 3.0, 5.0, 8.0, 12.0 }) {   // (the headset's strikes: 1-13 m/s)
			const double zb = zs * 0.6;   // the body across the blade's outer part
			const hc::Capsule body = Monster(0, zb, bd.r);
			hh::Settings s;
			s.bodies = true;
			hh::Hold h;
			const double x0 = -0.45, x1 = 1.10;   // the controller from 45 cm before the body to 1.1 m past it (past the hold's snap of 0.8 m), at v
			hh::HandIn in[2] = { HandAt({ x0, 1.2, 0 }), HandAt({ -0.3, 1.0, 0 }, false) };
			in[1].grip = 0;
			h.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
			hh::HandOut out[2];
			for (int i = 0; i < 20; ++i) h.Step(in, kDt, s, out);   // (settled in the hand)
			double worst = -1e9;   // the farthest the blade got past the body's centre, at the body's depth along it
			int touched = 0;
			double jump = 0, offAtZ = 0;   // the most the tip moved in a step; where the tip was along z the step it left the body
			V3 lastTip;
			h.Ends(nullptr, &lastTip);
			bool was = false;
			for (double t = 0; t <= (x1 - x0) / v + 0.3; t += kDt) {
				in[0] = HandAt({ std::min(x1, x0 + v * t), 1.2, 0 });
				h.SetBodies(&body, 1);
				h.Step(in, kDt, s, out);
				const bool on = h.LastContact().bodies > 0;
				if (on) ++touched;
				V3 butt, tip;
				h.Ends(&butt, &tip);
				jump = std::max(jump, Len(Sub(tip, lastTip)));
				if (was && !on) offAtZ = tip.z;
				was = on;
				lastTip = tip;
				if (std::fabs(tip.z - butt.z) > 1e-6) {
					const double u = (zb - butt.z) / (tip.z - butt.z);
					if (u >= 0 && u <= 1) worst = std::max(worst, butt.x + (tip.x - butt.x) * u);
				}
			}
			// through: it left the body still spanning it (the tip past the body's far side along the blade) or hardly met it; else it held on and slid off its own tip -
			// the way a blade does get past a body, dragged along itself (the controller swung on 1.1 m)
			const bool through = touched < 5 || offAtZ < zb - bd.r;
			std::printf("  a sword swung at %2.0f m/s across %s (r %.0f cm): held %3.0f ms on it, then %s (the tip %+.0f cm along the blade from the body's axis; %.0f cm a step at most)\n",
			            v, bd.name, bd.r * 100, touched * kDt * 1000, through ? "THROUGH it" : "slid off its tip", (offAtZ - zb) * 100, jump * 100);
			EXPECT(!through, "a sword at %.0f m/s across %s must not pass through (%d steps on it, the tip at %.2f past %.2f)", v, bd.name, touched, offAtZ, zb);
			(void)worst;
		}
}

}   // namespace

int main() {
	std::printf("strike_measure_test: what a strike delivers into a body\n");
	TestStepTotals();
	TestTracker();
	TestStrikes();
	TestStick();
	TestSwingThrough();
	TestBlows();
	if (g_fails) {
		std::printf("strike_measure_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("strike_measure_test: all passed\n");
	return 0;
}
