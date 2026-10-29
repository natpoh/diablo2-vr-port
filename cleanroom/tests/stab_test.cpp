// [weight] stab_in on synthetic poses, without the game: the seam (cleanroom/heft/stab.h) and the hold's wiring of it (hold.cpp), the sliders, the debug's readout and marks
// (vr/stab_debug.h), hit by touch's verdict (thrust::ThrustingNow). The stab itself is the weight-physics library's (D2rwStab, not in this repository):
//
//   stab_test                 the seam, the wiring (a stand-in engine that records what the hold hands it), the pure parts
//   stab_test dll=<path>      ... and the library itself through the mod's own loader (vr/weight_lib.cpp) and hold: a spear thrust in, held on its line, drawn out,
//                             torn free, carried by a walking body, following its limb (the library's own tests are its repository's)
//
// The wiring checks: no library - every weapon stops at the surface, bit for bit as with the tick off, and a library that lets nothing in changes nothing either; a strike
// is handed over only with the tick on, with the weapon as it was before the contact, the mod's gate and where it met; while in, the contact leaves that monster's bodies
// alone, the library is called before the contact with the hands' gaps, a step without bodies is handed over as none; the weapon let go, the tick off: out, with the
// monster's id.

#include "contact.h"
#include "hold.h"
#include "stab.h"
#include "../../vr/body_motion.h"
#include "../../vr/monster_bodies.h"
#include "../../vr/stab_debug.h"
#include "../../vr/thrust_stroke.h"
#include "../../vr/weight_lib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hc = heft::contact;
namespace hs = heft::stab;

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

hc::Capsule Monster(double x, double z, double r, uint32_t id = 7, int part = -1) {
	hc::Capsule c;
	c.a = { x, 0.3, z };
	c.b = { x, 1.5, z };
	c.radius = r;
	c.id = id;
	c.part = part;
	return c;
}

hh::HandIn HandAt(V3 p, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose = { p, {} };
	h.grip = tracked ? 1.0f : 0.0f;
	h.fistLine = { 0, 0, -1 };
	return h;
}

double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}

double Deg(V3 a, V3 b) {
	const double la = Len(a), lb = Len(b);
	if (la < 1e-9 || lb < 1e-9) return 0;
	return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0, 1.0)) * 57.29577951308232;
}

// ---- a stand-in for the library: it lets a thrusting strike in (the weapon put back as it came, as the library does), then holds nothing - it records what it is handed ----
struct StandIn final : public hs::Engine {
	mutable int calls = 0, strikes = 0, noBodies = 0, gapsSeen = 0;
	mutable hs::Strike lastStrike;
	mutable bool lastStrikeHad = false;
	mutable hs::Params lastParams;
	mutable double leaveAt = 1e9, t = 0;
	int Step(State& st, const Body&, V3, V3, const hc::Capsule* bodies, int n, const hs::Strike* strike, const V3*, int nGaps, double, const hs::Params& p, hs::Blob* blob,
	         hs::Report* out) const override {
		++calls;
		lastParams = p;
		if (n == 0) ++noBodies;
		gapsSeen = std::max(gapsSeen, nGaps);
		*out = {};
		const bool in = blob->bytes[0] != 0;
		if (!in) {
			if (!strike) return 0;
			++strikes;
			lastStrike = *strike;
			lastStrikeHad = true;
			out->judged = true;
			out->thrusting = strike->thrusting;
			if (!strike->thrusting) {
				std::snprintf(out->why, sizeof out->why, "not a thrust");
				return 0;
			}
			st = strike->before;
			blob->bytes[0] = 1;
			out->entered = out->in = true;
			out->id = bodies[strike->body].id;
			std::snprintf(out->why, sizeof out->why, "in");
			return 1;
		}
		if (t >= leaveAt) {
			out->left = true;
			std::snprintf(out->why, sizeof out->why, "drawn out");
			return 0;
		}
		out->in = true;
		out->id = 7;
		return 1;
	}
};

struct Scene {
	hh::Kind kind = hh::Kind::Spear;
	bool stab = true;            // [weight] stab_in
	bool stabbing = true;        // the mod's word: thrusting now ...
	double stabbingUntil = 1e9;  // ... until then
	double secs = 1.6;
	double letGoAt = 1e9;        // the grip opened then
	hs::Params params;
	std::function<V3(double)> ctl;
	std::function<std::vector<hc::Capsule>(double)> bodies;
	std::function<void(double)> tick;   // (the stand-in's clock)
	std::function<Q(double)> rot;       // the controllers' turn (none: the shaft along -Z)
	double second = 0;                  // a second hand this far behind the first along the shaft (0: one hand)
};

struct Run {
	bool entered = false, locked = false, unlocked = false, inAtEnd = false;
	double deepest = 0, pastSurface = -1, offLine = 0, turned = 0, maxJump = 0;
	std::string why;
	uint32_t leftId = 0;
	V3 tipEnd, axisEnd;
	std::vector<V3> tips;
};

Run Play(const Scene& sc) {
	Run r;
	hh::Settings s;
	s.bodies = true;
	s.stab = sc.stab;
	s.stabParams = sc.params;
	hh::Hold h;
	const auto rotAt = [&](double t) { return sc.rot ? sc.rot(t) : Q {}; };
	const auto secondHand = [&](double t) {   // the second hand on the shaft behind the first, turned with it
		hh::HandIn h2 = HandAt(Add(sc.ctl(t), Rotate(rotAt(t), { 0, 0, sc.second })), true);
		h2.pose.rot = rotAt(t);
		return h2;
	};
	hh::HandIn in[2] = { HandAt(sc.ctl(0)), HandAt({ -0.3, 1.0, 0 }, false) };
	in[0].pose.rot = rotAt(0);
	in[1].grip = 0;
	h.Place(0, sc.kind, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	if (sc.second > 0) {   // the second hand closes on the shaft
		in[1] = secondHand(0);
		for (int i = 0; i < 3; ++i) h.Step(in, kDt, s, out);
	}
	V3 lastTip, axisIn;
	h.Ends(nullptr, &lastTip);
	const int steps = (int)std::ceil(sc.secs / kDt);
	for (int i = 1; i <= steps; ++i) {
		const double t = i * kDt;
		if (sc.tick) sc.tick(t);
		in[0] = HandAt(sc.ctl(t), true);
		in[0].pose.rot = rotAt(t);
		if (sc.second > 0) in[1] = secondHand(t);
		if (t >= sc.letGoAt) in[0].grip = in[1].grip = 0;
		const std::vector<hc::Capsule> bodies = sc.bodies(t);
		h.SetBodies(bodies.data(), (int)bodies.size());
		h.SetStabbing(sc.stabbing && t < sc.stabbingUntil);
		h.Step(in, kDt, s, out);
		V3 butt, tip;
		h.Ends(&butt, &tip);
		r.tips.push_back(tip);
		r.maxJump = std::max(r.maxJump, Len(Sub(tip, lastTip)));
		lastTip = tip;
		const hs::Report& st = h.LastStab();
		if (st.entered) {
			r.entered = true;
			axisIn = Sub(tip, butt);
		}
		if (h.Stuck()) {
			r.deepest = std::max(r.deepest, st.depth);
			if (st.locked) r.locked = true;
			else if (r.locked) r.unlocked = true;
			if (!st.entered && Len(st.axis) > 0.5) {
				const V3 u = Mul(Sub(tip, butt), 1.0 / Len(Sub(tip, butt)));
				r.offLine = std::max(r.offLine, Len(Sub(st.entry, Add(butt, Mul(u, Dot(Sub(st.entry, butt), u))))));
				r.turned = std::max(r.turned, Deg(Sub(tip, butt), axisIn));
			}
		}
		if (st.left) {
			r.why = st.why;
			r.leftId = st.id;
		} else if (!r.entered && st.judged) {
			r.why = st.why;
		}
		if (!bodies.empty()) r.pastSurface = std::max(r.pastSurface, (bodies[0].a.z + bodies[0].radius) - tip.z);
		r.axisEnd = Sub(tip, butt);
	}
	r.inAtEnd = h.Stuck();
	r.tipEnd = lastTip;
	return r;
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

std::function<V3(double)> Thrust(double reach, double hold = 0.4, double x = 0) {
	return [=](double t) {
		double z = 0;
		if (t < 0.3) z = -reach * Smooth(t / 0.3);
		else if (t < 0.3 + hold) z = -reach;
		else z = -reach * (1 - Smooth((t - 0.3 - hold) / 0.3));
		return V3 { x, 1.2, z };
	};
}

constexpr double kSpearHead = 1.32 - 1.10;   // the spear's point piece (hold.cpp ShapeOf): the depth's limit

// [gameplay] melee_thrust_with_run: the bodies' velocities in the room (vr/body_motion.h).
void TestBodyMotion() {
	bodymotion::Tracker tr;
	const uint64_t key = bodymotion::Key(7, 3);
	V3 v;
	for (int i = 0; i < 30; ++i) v = tr.Feed(key, { 0, 1, -2.0 + 3.0 * i * kDt }, kDt, (uint64_t)i + 1);
	EXPECT(std::fabs(v.z - 3.0) < 0.05 && std::fabs(v.x) < 1e-9, "a body coming at 3 m/s: %.3f m/s", v.z);
	v = tr.Feed(key, { 0, 1, 5.0 }, kDt, 31);
	EXPECT(Len(v) == 0, "a jump (a new scan, a teleport): from rest");
	tr.Feed(key, { 0, 1, 5.0 }, kDt, 32);
	v = tr.Feed(key, { 0, 1, 5.1 }, kDt, 40);
	EXPECT(Len(v) == 0, "not seen for a few steps: from rest");
	EXPECT(bodymotion::Key(7, -1) != bodymotion::Key(7, 0) && bodymotion::Key(7, 2) != bodymotion::Key(8, 2), "a body's key: its monster and its limb");
	const V3 other = tr.Feed(bodymotion::Key(9, -1), { 1, 1, 1 }, kDt, 41);
	EXPECT(Len(other) == 0, "a body first seen: at rest");
}

// The hold's wiring of the seam, against the stand-in.
void TestWiring() {
	const double tip0 = TipZ(hh::Kind::Spear);
	const double r = 0.17, zb = tip0 - 0.12 - r;
	Scene sc;
	sc.secs = 1.2;
	sc.ctl = Thrust(0.40, 5.0);
	sc.bodies = [=](double) { return std::vector<hc::Capsule> { Monster(0, zb, r) }; };
	// no library: no stab - stopped at the surface, bit for bit as with the tick off; a library that lets nothing in: the same
	hs::SetEngine(nullptr);
	const Run none = Play(sc);
	Scene off = sc;
	off.stab = false;
	const Run o = Play(off);
	StandIn standIn;
	hs::SetEngine(&standIn);
	Scene refuse = sc;
	refuse.stabbing = false;
	const Run rf = Play(refuse);
	std::printf("  no stab library: %.1f cm past the surface; the tick off %.1f; a library that lets nothing in %.1f\n", none.pastSurface * 100, o.pastSurface * 100,
	            rf.pastSurface * 100);
	EXPECT(!none.entered && none.pastSurface < 0.015, "no library: it stops at the surface (%.3f m)", none.pastSurface);
	bool same = none.tips.size() == o.tips.size() && rf.tips.size() == o.tips.size();
	for (size_t i = 0; same && i < o.tips.size(); ++i)
		same = none.tips[i].x == o.tips[i].x && none.tips[i].y == o.tips[i].y && none.tips[i].z == o.tips[i].z && rf.tips[i].x == o.tips[i].x && rf.tips[i].z == o.tips[i].z;
	EXPECT(same, "no library, the tick off, a refusing library: bit for bit the same");
	EXPECT(standIn.strikes > 0 && standIn.lastStrikeHad && !standIn.lastStrike.thrusting && standIn.lastStrike.body == 0 && rf.why == "not a thrust",
	       "a strike is handed over with the mod's gate (%d strikes, why '%s')", standIn.strikes, rf.why.c_str());
	const int strikesOff = standIn.strikes;
	Play(off);
	EXPECT(standIn.strikes == strikesOff, "the tick off: the library is not asked");
	// let in: the weapon goes on as it came (the stand-in holds nothing), the contact leaves the monster alone - the point goes well past the surface
	standIn = StandIn {};
	hs::SetEngine(&standIn);
	sc.params.minSpeed = 0.77;
	const Run in = Play(sc);
	std::printf("  a stand-in that lets it in: %.1f cm past the surface, %d calls\n", in.pastSurface * 100, standIn.calls);
	EXPECT(in.entered && in.inAtEnd && in.pastSurface > 0.10, "let in: the contact leaves that monster alone (%.3f m)", in.pastSurface);
	EXPECT(standIn.gapsSeen == 0 && standIn.lastParams.minSpeed == 0.77, "while in: called with the settings, no gaps (the tear is the mod's own)");
	EXPECT(Len(standIn.lastStrike.before.vel) > 0.1, "the strike carries the weapon as it was before the contact (%.2f m/s)", Len(standIn.lastStrike.before.vel));
	// a step without bodies: handed over as none
	standIn = StandIn {};
	hs::SetEngine(&standIn);
	Scene gap = sc;
	gap.bodies = [=](double t) { return t > 0.7 && t < 0.8 ? std::vector<hc::Capsule> {} : std::vector<hc::Capsule> { Monster(0, zb, r) }; };
	const Run g = Play(gap);
	EXPECT(g.inAtEnd && standIn.noBodies >= 8, "steps without bodies: handed over as none (%d), the library decides (still in)", standIn.noBodies);
	// the library says out; the mod's own exits name the monster
	standIn = StandIn {};
	hs::SetEngine(&standIn);
	Scene leave = sc;
	leave.tick = [&](double t) { standIn.t = t; };
	standIn.leaveAt = 0.8;
	const Run lv = Play(leave);
	EXPECT(lv.entered && !lv.inAtEnd && lv.why == "drawn out", "the library's out: out (%s)", lv.why.c_str());
	standIn = StandIn {};
	hs::SetEngine(&standIn);
	Scene drop = sc;
	drop.letGoAt = 0.8;
	const Run dr = Play(drop);
	EXPECT(dr.entered && !dr.inAtEnd && dr.why == "the weapon left the hands" && dr.leftId == 7, "let go: out, with the monster (%s, %u)", dr.why.c_str(), dr.leftId);
	hs::SetEngine(nullptr);
}

// The sliders ([weight] stab_*) as the numbers handed to the library.
void TestSliders() {
	hs::Sliders sl;
	hs::Params p = hs::FromSliders(sl);
	const hs::Params d;
	EXPECT(std::fabs(p.minSpeed - 0.5) < 1e-9 && std::fabs(p.angleCos - 0.5) < 1e-6 && p.shellDamper == d.shellDamper && p.fleshDamper == d.fleshDamper && p.depthLimit == 0 &&
	           std::fabs(p.breakM - 0.45) < 1e-9 && p.unlockForce == 150,
	       "the defaults are the stab's own");
	sl.minSpeed = 1.2;
	sl.maxAngleDeg = 75;
	sl.resistPct = 30;
	sl.depthCm = 15;
	sl.tearCm = 80;
	sl.unlockN = 0;
	p = hs::FromSliders(sl);
	EXPECT(std::fabs(p.minSpeed - 1.2) < 1e-9 && std::fabs(p.angleCos - std::cos(75 * 0.017453292519943295)) < 1e-9 && std::fabs(p.shellDamper - 1500) < 1e-9 &&
	           std::fabs(p.fleshDamper - 300) < 1e-9 && std::fabs(p.depthLimit - 0.15) < 1e-9 && std::fabs(p.breakM - 0.8) < 1e-9 && p.unlockForce == 0,
	       "each slider sets its number");
	sl = {};
	sl.minSpeed = NAN;
	sl.maxAngleDeg = 200;
	sl.resistPct = -5;
	p = hs::FromSliders(sl);
	EXPECT(std::fabs(p.minSpeed - 0.5) < 1e-9 && std::fabs(p.angleCos - std::cos(89 * 0.017453292519943295)) < 1e-9 && p.shellDamper == 0, "out of range: clamped; not a number: the default");
}

// [debug] stab_debug (vr/stab_debug.h): the readout's lines, the board keeping the judged numbers, the marks; HeadToRender the inverse of RenderToHead.
void TestDebug() {
	stabdbg::Note n;
	n.state = stabdbg::State::Refused;
	std::snprintf(n.why, sizeof n.why, "not straight in (at an angle)");
	n.judged = true;
	n.thrusting = true;
	n.speed = 1.1f; n.minSpeed = 0.5f; n.angleDeg = 72; n.maxAngleDeg = 60; n.fromTip = 0.02f; n.tipM = 0.10f;
	n.atMs = 1000;
	n.havePoint = true;
	n.point[0] = 1; n.point[1] = 1; n.point[2] = 1;
	thrustdbg::Line lines[12];
	size_t k = stabdbg::Lines(n, lines, 12);
	EXPECT(k == 5 && std::strstr(lines[0].text, "refused: not straight") && lines[0].colour == thrustdbg::kFail && std::strstr(lines[3].text, "angle 72 / 60") &&
	           lines[3].colour == thrustdbg::kFail && lines[2].colour == thrustdbg::kPass,
	       "a refused strike: why, and the angle failing (%zu lines: '%s' '%s')", k, k ? lines[0].text : "", k > 3 ? lines[3].text : "");
	const float eye[3] = { 0, 0, 0 };
	float a[8][4], b[8][4];
	EXPECT(stabdbg::Marks(n, 1500, 4.0f, eye, a, b, 8) == 1 && a[0][3] > 0 && b[0][3] == (float)meleeboxes::TouchKind(6, 0), "a refused strike: an orange ball");
	EXPECT(stabdbg::Marks(n, 1000 + stabdbg::kMarkMs + 10, 4.0f, eye, a, b, 8) == 0, "... gone after a moment");
	stabdbg::Board board;
	board.Take(n);
	stabdbg::Note in;
	in.state = stabdbg::State::In;
	in.depth = 0.08f; in.limit = 0.22f; in.across = 0.05f; in.tearM = 0.45f;
	in.atMs = 1100;
	in.have3d = true;
	in.axis[2] = -1;
	board.Take(in);
	k = stabdbg::Lines(board.Last(), lines, 12);
	EXPECT(board.Has(1200) && k == 6 && std::strstr(lines[0].text, "in, 8.0 / 22 cm") && std::strstr(lines[2].text, "speed 1.10"),
	       "in: the depth, with the strike's numbers kept (%zu lines: '%s')", k, k ? lines[0].text : "");
	EXPECT(stabdbg::Marks(board.Last(), 1200, 4.0f, eye, a, b, 8) == 3 && b[1][3] == (float)meleeboxes::TouchKind(3, 0) && std::fabs(b[1][2] - (-0.08f * 4.0f)) < 1e-5,
	       "in: the line, the depth bar (green, 8 cm along the line), the entry");
	stabdbg::Note out;
	out.state = stabdbg::State::Out;
	std::snprintf(out.why, sizeof out.why, "drawn out");
	out.deepest = 0.12f;
	out.atMs = 1300;
	board.Take(out);
	EXPECT(board.Last().have3d && stabdbg::Marks(board.Last(), 1400, 4.0f, eye, a, b, 8) == 3 && b[1][3] == (float)meleeboxes::TouchKind(4, 0), "out: drawn where it was, red");
	EXPECT(!board.Has(1300 + stabdbg::kShowMs + 1), "the last word goes after a while");
	monsterbodies::View v;
	v.eye[0] = 12; v.eye[1] = 6; v.eye[2] = -4;
	v.camR[0] = 0.8f; v.camR[1] = 0; v.camR[2] = 0.6f;
	v.camF[0] = -0.6f; v.camF[1] = -0.1f; v.camF[2] = 0.8f;
	v.upm = 4;
	const float hp[3] = { 0.3f, -0.4f, -0.9f };
	float rp[3], back[3];
	EXPECT(monsterbodies::HeadToRender(v, hp, rp) && monsterbodies::RenderToHead(v, rp, back) && std::fabs(back[0] - hp[0]) < 1e-5f && std::fabs(back[1] - hp[1]) < 1e-5f &&
	           std::fabs(back[2] - hp[2]) < 1e-5f,
	       "HeadToRender is RenderToHead's inverse: %.4f %.4f %.4f", back[0], back[1], back[2]);
}

// Hit by touch's verdict the mod hands over: a poke along the blade is a thrust now, a swing across it is not.
void TestThrustingNow() {
	thrust::Rule rule;
	thrust::Path poke, swing;
	for (int i = 0; i <= 20; ++i) {
		const double t = i / 90.0;
		const float z = -1.2f * (float)t;
		poke.Feed(t, { 0, 1.2f, z }, { 0, 1.2f, z - 1.0f }, 3);
		const float x = 2.0f * (float)t;
		swing.Feed(t, { x, 1.2f, 0 }, { x, 1.2f, -1.0f }, 4);
	}
	EXPECT(thrust::ThrustingNow(rule, poke), "a poke is a thrust now (share %.2f, step %.2f)", poke.Share(), poke.StepCos());
	EXPECT(!thrust::ThrustingNow(rule, swing), "a swing is not (share %.2f)", swing.Share());
	thrust::Path none;
	EXPECT(!thrust::ThrustingNow(rule, none), "no stroke, no thrust");
	rule.on = false;
	EXPECT(!thrust::ThrustingNow(rule, poke), "thrusts off: none");
	// the stab's own judge, every pointed blade (a sword too): point first now - a poke yes; a swing, a slow push, a turn no; which kinds have a point
	const auto judge = [](auto grip, auto axis, int steps = 14) {
		thrust::PointFirst pf;
		for (int i = 0; i <= steps; ++i) {
			const double t = i / 90.0;
			const stroke::Vec g = grip(t), ax = axis(t);
			pf.Feed(t, g, g + ax * 0.5f);
		}
		float speed = 0;
		const bool now = pf.Now(70.0f, &speed);
		return std::make_pair(now, speed);
	};
	const auto fwd = [](double) { return stroke::Vec { 0, 0, -1 }; };
	const auto pokeP = judge([](double t) { return stroke::Vec { 0, 1.2f, -1.5f * (float)t }; }, fwd);
	const auto swingP = judge([](double t) { return stroke::Vec { 2.0f * (float)t, 1.2f, 0 }; }, fwd);
	const auto slowP = judge([](double t) { return stroke::Vec { 0, 1.2f, -0.1f * (float)t }; }, fwd);
	const auto turnP = judge([](double t) { return stroke::Vec { 0, 1.2f, -1.5f * (float)t }; },
	                         [](double t) { const float a = 6.0f * (float)t; return stroke::Vec { std::sin(a), 0, -std::cos(a) }; });
	std::printf("  the stab's own judge: a poke %s (%.2f m/s), a swing %s, a slow push %s (%.2f m/s), a poke turning 40 deg %s\n", pokeP.first ? "yes" : "no", pokeP.second,
	            swingP.first ? "yes" : "no", slowP.first ? "yes" : "no", slowP.second, turnP.first ? "yes" : "no");
	EXPECT(pokeP.first && !swingP.first && !slowP.first && !turnP.first, "point first: a poke yes; a swing, a slow push, a turning poke no");
	EXPECT(thrust::PointedKind(strikedir::kSword) && thrust::PointedKind(strikedir::kSpear) && thrust::PointedKind(strikedir::kDagger) &&
	           thrust::PointedKind(strikedir::kJavelin) && thrust::PointedKind(strikedir::kThrowing) && thrust::PointedKind(strikedir::kPolearm) &&
	           thrust::PointedKind(strikedir::kClaw) && !thrust::PointedKind(strikedir::kMace) && !thrust::PointedKind(strikedir::kAxe) &&
	           !thrust::PointedKind(strikedir::kStaff) && !thrust::PointedKind(strikedir::kUnarmed) && !thrust::PointedKind(strikedir::kScepter) &&
	           !thrust::PointedKind(strikedir::kWand) && !thrust::PointedKind(-1),
	       "swords, spears, daggers, javelins, throwing, polearms and claws have a point; maces, axes, staffs, fists, scepters, wands do not");
}

// The library itself (stab_test dll=<path>), through the mod's loader and hold, at the ini's settings (30% of the toolkit's dampers, unlock 150 N).
void TestLibrary(const char* path) {
	wchar_t wpath[512] = {};
	for (size_t i = 0; i < 511 && path[i]; ++i) wpath[i] = (wchar_t)(unsigned char)path[i];   // (an ASCII path)
	weightlib::SetPath(wpath);
	if (!weightlib::Ensure() || !hs::GetEngine()) {
		++g_fails;
		std::printf("  FAIL: the library at %s did not load with a stab\n", path);
		return;
	}
	std::printf("  the library: %s\n", weightlib::Name());
	hs::Sliders sl;
	sl.resistPct = 30;
	const hs::Params ini = hs::FromSliders(sl);
	const double tip0 = TipZ(hh::Kind::Spear);
	const double r = 0.17, zb = tip0 - 0.12 - r;
	Scene sc;
	sc.params = ini;
	sc.secs = 3.0;
	sc.ctl = Thrust(0.40, 0.8);
	sc.bodies = [=](double) { return std::vector<hc::Capsule> { Monster(0, zb, r) }; };
	const Run in = Play(sc);
	std::printf("  a spear thrust: in %s, %.1f cm deep at most (its head %.0f), locked %s, unlocked %s, out: %s\n", in.entered ? "yes" : "no", in.deepest * 100,
	            kSpearHead * 100, in.locked ? "yes" : "no", in.unlocked ? "yes" : "no", in.why.c_str());
	EXPECT(in.entered && in.pastSurface > 0.06 && in.deepest <= kSpearHead + 1e-6, "in, past the skin, no deeper than the blade (%.3f / %.3f m)", in.pastSurface, in.deepest);
	EXPECT(in.locked && in.unlocked && !in.inAtEnd && in.why == "drawn out", "held still it locks, pulled back it unlocks and comes out (%s)", in.why.c_str());
	EXPECT(in.offLine < 1e-6 && in.turned < 0.01, "on its line (%.2e m off), not turned (%.3f deg)", in.offLine, in.turned);
	EXPECT(in.maxJump < 0.12, "no jump (%.3f m)", in.maxJump);
	Scene hard = sc;
	hard.params = hs::FromSliders(hs::Sliders {});
	const Run h = Play(hard);
	std::printf("  the same at 100%% (the toolkit's own dampers): %.1f cm deep at most\n", h.deepest * 100);
	EXPECT(h.deepest < in.deepest, "100%% holds it back more than 30%% (%.3f vs %.3f m)", h.deepest, in.deepest);
	Scene off = sc;
	off.stabbing = false;
	EXPECT(!Play(off).entered, "not a thrust for hit by touch: stopped at the skin");
	Scene aside = sc;
	aside.secs = 2.0;
	aside.stabbingUntil = 0.5;
	aside.ctl = [](double t) { return V3 { t < 0.6 ? 0.0 : 0.25 * Smooth((t - 0.6) / 0.4), 1.2, -0.30 * Smooth(t / 0.3) }; };
	const Run as = Play(aside);
	std::printf("  pulled 25 cm aside while in: %s, %.2e m off its line, %.3f deg turned\n", as.inAtEnd ? "still in" : as.why.c_str(), as.offLine, as.turned);
	EXPECT(as.inAtEnd && as.offLine < 1e-6 && as.turned < 0.01, "held on its line (%s)", as.why.c_str());
	Scene far = aside;
	far.ctl = [](double t) { return V3 { t < 0.6 ? 0.0 : 0.9 * Smooth((t - 0.6) / 0.4), 1.2, -0.30 * Smooth(t / 0.3) }; };
	const Run fr = Play(far);
	EXPECT(fr.entered && !fr.inAtEnd && fr.why.find("torn") != std::string::npos && fr.leftId == 7, "pulled 90 cm aside: torn free (%s, monster %u)", fr.why.c_str(), fr.leftId);
	Scene walk = sc;
	walk.secs = 1.8;
	walk.ctl = Thrust(0.30, 5.0);
	walk.bodies = [=](double t) { return std::vector<hc::Capsule> { Monster(t < 0.7 ? 0.0 : 0.15 * Smooth((t - 0.7) / 0.6), zb, r) }; };
	const Run wk = Play(walk);
	EXPECT(wk.inAtEnd && wk.tipEnd.x > 0.10, "a walking body carries it (%.3f m)", wk.tipEnd.x);
	Scene limbs = walk;
	limbs.bodies = [=](double t) { return std::vector<hc::Capsule> { Monster(t < 0.7 ? 0.0 : 0.12, zb, 0.08, 7, 2), Monster(-0.05, zb - 0.02, 0.08, 7, 1) }; };
	const Run lm = Play(limbs);
	EXPECT(lm.inAtEnd && lm.tipEnd.x > 0.08, "in the arm: it follows the arm, not the nearer torso (%.3f m)", lm.tipEnd.x);
	Scene gap = walk;
	gap.bodies = [=](double t) { return t > 0.7 && t < 0.85 ? std::vector<hc::Capsule> {} : std::vector<hc::Capsule> { Monster(0, zb, r) }; };
	EXPECT(Play(gap).inAtEnd, "150 ms without bodies: still in");
	// The field (the headset run of 2026-10-22 20:40, the user's ini: stab_max_angle 31, stab_resist_pct 5, stab_unlock_n 110): a still zombie, the controller driven
	// on 60 cm past the surface (in VR the hand goes on into the monster while the weapon is held at the wound), the wrist wobbling 8 degrees as a hand does, one hand
	// and both on the spear - it must stay in while held, and come out drawn back along its length
	hs::Sliders user;
	user.maxAngleDeg = 31;
	user.resistPct = 5;
	user.unlockN = 110;
	Scene field;
	field.params = hs::FromSliders(user);
	field.secs = 2.0;
	field.ctl = [](double t) { return V3 { 0, 1.2, -0.72 * Smooth(t / 0.35) }; };   // (the surface 12 cm before the point: 60 cm past it)
	field.rot = [](double t) { return QFromAxisAngle({ 0, 1, 0 }, t < 0.4 ? 0.0 : 0.14 * std::sin(9.4 * (t - 0.4))); };   // (once it is in: the hand holding)
	field.bodies = [=](double) { return std::vector<hc::Capsule> { Monster(0, zb, r) }; };
	const Run f1 = Play(field);
	std::printf("  the field, one hand, the hand 60 cm past the surface, wobbling: in %s, %.1f cm deep at most, at the end %s\n", f1.entered ? "yes" : "no", f1.deepest * 100,
	            f1.inAtEnd ? "still in" : f1.why.c_str());
	EXPECT(f1.entered && f1.inAtEnd, "one hand: held in, it stays in (%s)", f1.why.c_str());
	Scene both = field;
	both.second = 0.45;
	const Run f2 = Play(both);
	std::printf("  the field, both hands on the spear: in %s, %.1f cm deep at most, at the end %s\n", f2.entered ? "yes" : "no", f2.deepest * 100, f2.inAtEnd ? "still in" : f2.why.c_str());
	EXPECT(f2.entered && f2.inAtEnd, "both hands: held in, it stays in (%s)", f2.why.c_str());
	Scene back = both;
	back.secs = 3.0;
	back.ctl = [](double t) { return V3 { 0, 1.2, t < 1.5 ? -0.72 * Smooth(t / 0.35) : -0.72 + 1.0 * Smooth((t - 1.5) / 0.5) }; };
	const Run f3 = Play(back);
	EXPECT(f3.entered && !f3.inAtEnd && f3.why == "drawn out", "both hands, pulled back 1 m along it: drawn out (%s)", f3.why.c_str());
	Scene yank = both;
	yank.secs = 3.0;
	yank.ctl = [](double t) { return V3 { t < 1.5 ? 0.0 : 1.2 * Smooth((t - 1.5) / 0.4), 1.2, -0.72 * Smooth(t / 0.35) }; };
	yank.stabbingUntil = 1.0;
	const Run f4 = Play(yank);
	EXPECT(f4.entered && !f4.inAtEnd, "both hands yanked 1.2 m aside: it comes free (%s)", f4.why.c_str());
	// harder: the hand 1 m past the surface, the wrists 15 degrees about, both hands - still in (the snap of a hand far from the weapon must not throw it out)
	Scene deep = both;
	deep.secs = 2.5;
	deep.ctl = [](double t) { return V3 { 0, 1.2, -1.12 * Smooth(t / 0.4) }; };
	deep.rot = [](double t) { return QFromAxisAngle({ 0.3, 1, 0 }, t < 0.45 ? 0.0 : 0.26 * std::sin(7.0 * (t - 0.45))); };
	const Run f5 = Play(deep);
	std::printf("  the field, both hands, 1 m past the surface, wrists 15 deg about: in %s, %.1f cm deep at most, at the end %s\n", f5.entered ? "yes" : "no", f5.deepest * 100,
	            f5.inAtEnd ? "still in" : f5.why.c_str());
	EXPECT(f5.entered && f5.inAtEnd, "1 m past, 15 degrees about: still in (%s)", f5.why.c_str());
	Scene past = both;   // the hand 1.5 m past the surface: farther from its hand on the weapon than the model's snap (0.8 m) - the weapon is not thrown out of the wound
	past.secs = 2.0;
	past.ctl = [](double t) { return V3 { 0, 1.2, -1.62 * Smooth(t / 0.5) }; };
	const Run f6 = Play(past);
	std::printf("  the field, both hands, the hand 1.5 m past the surface: in %s, at the end %s, the biggest jump of the point %.2f m\n", f6.entered ? "yes" : "no",
	            f6.inAtEnd ? "still in" : f6.why.c_str(), f6.maxJump);
	EXPECT(f6.entered && f6.inAtEnd && f6.maxJump < 0.15, "1.5 m past: still in, no jump (%s, %.2f m)", f6.why.c_str(), f6.maxJump);
	Scene turn = deep;   // the wrists turning 25 degrees about while in: on a 1.3 m spear that is 50+ cm across at the grips - a turn, not a tear
	turn.rot = [](double t) { return QFromAxisAngle({ 0.3, 1, 0 }, t < 0.45 ? 0.0 : 0.44 * std::sin(7.0 * (t - 0.45))); };
	const Run f7 = Play(turn);
	std::printf("  the field, both hands, wrists 25 deg about while in: at the end %s\n", f7.inAtEnd ? "still in" : f7.why.c_str());
	EXPECT(f7.entered && f7.inAtEnd, "25 degrees about: still in (%s)", f7.why.c_str());
	Scene slant = both;   // in, then the hands drive on 1.1 m in a direction 25 degrees off the stab line: far across at the end, but along the way in - no tear
	slant.secs = 2.0;
	slant.ctl = [](double t) {
		const double u = Smooth(t / 0.6), in = 1.1 * u;
		return V3 { t < 0.15 ? 0.0 : std::sin(0.44) * (in - 0.25) * (t < 0.15 ? 0.0 : 1.0), 1.2, -0.25 * Smooth(t / 0.15) - std::cos(0.44) * std::max(0.0, in - 0.25) };
	};
	const Run f8 = Play(slant);
	std::printf("  the field, both hands, driven on 1.1 m at 25 deg off the line: at the end %s\n", f8.inAtEnd ? "still in" : f8.why.c_str());
	EXPECT(f8.entered && f8.inAtEnd, "driven on at 25 degrees off: still in (%s)", f8.why.c_str());
	Scene late = both;   // stopped at the skin first (no thrust yet for hit by touch) while the hands drive on 0.9 m at 30 degrees off; then the thrust is seen and it goes in
	late.secs = 2.0;
	late.stabbing = true;
	late.ctl = [](double t) {
		const double in = 0.9 * Smooth(t / 0.5);
		return V3 { std::sin(0.52) * in, 1.2, -std::cos(0.52) * in };
	};
	late.rot = [](double) { return QFromAxisAngle({ 0, 1, 0 }, -0.52 * 0.0); };
	const Run f9 = Play(late);
	std::printf("  the field, both hands, the hands 0.9 m on at 30 deg off: in %s, at the end %s\n", f9.entered ? "yes" : "no", f9.inAtEnd ? "still in" : f9.why.c_str());
	EXPECT(!f9.entered || f9.inAtEnd, "in with the hands already far on at a slant: still in (%s)", f9.why.c_str());
	// [gameplay] melee_thrust_with_run: a run with the spear held forward, the hands still. In the room the hero's run is the body coming at the weapon - with
	// its velocity there (vr/body_motion.h, the tick on): 3 m/s into it, the run stops (in, held), then the hero backs off (out, drawn out). The tick off:
	// the bodies have no velocity - the point's speed against the body is the hands' alone, and still hands do not go in.
	const auto runBody = [=](bool withRun) {
		return [=](double t) {
			const double z0 = zb - 0.45;   // (the body's front 45 cm before the point at the start)
			double z = z0, v = 0;
			if (t < 0.45) { z = z0 + 3.0 * t; v = 3.0; }                                       // the run: 3 m/s at it
			else if (t < 1.2) { z = z0 + 3.0 * 0.45; }                                        // stopped
			else { z = z0 + 3.0 * 0.45 - 2.0 * std::min(t - 1.2, 0.6); v = t < 1.8 ? -2.0 : 0; }   // backing off
			hc::Capsule c = Monster(0, z, r);
			if (withRun) c.vel = { 0, 0, v };
			return std::vector<hc::Capsule> { c };
		};
	};
	Scene run = field;
	run.secs = 2.2;
	run.rot = nullptr;
	run.ctl = [](double) { return V3 { 0, 1.2, 0 }; };   // the hands still
	run.bodies = runBody(true);
	Scene runHold = run;
	runHold.secs = 1.1;
	const Run rh = Play(runHold);
	std::printf("  a 3 m/s run with still hands, the run counted: in %s, %.1f cm deep, after the stop %s\n", rh.entered ? "yes" : "no", rh.deepest * 100,
	            rh.inAtEnd ? "still in" : rh.why.c_str());
	EXPECT(rh.entered && rh.inAtEnd && rh.deepest > 0.05, "running in with the weapon forward sticks it in and it holds (%s)", rh.why.c_str());
	const Run rb = Play(run);
	EXPECT(rb.entered && !rb.inAtEnd && rb.why == "drawn out", "backing off draws it out (%s)", rb.why.c_str());
	Scene runOff = run;
	runOff.bodies = runBody(false);
	runOff.secs = 1.1;
	const Run ro = Play(runOff);
	// (the tick off: no velocity for the body, as before - the body shoves the weapon back and the hands' pull drives it forward again, so it may still go in
	// a moment later, by the arms' push as it always could)
	std::printf("  the same run, not counted (the tick off): in %s, %.1f cm deep - by the arms' push back into it, as before\n", ro.entered ? "yes" : "no", ro.deepest * 100);
	Scene sword = field;   // a sword driven point first (it does not thrust for hit by touch; the stab's own judge says it is point first): in, held
	sword.kind = hh::Kind::Sword;
	sword.bodies = [=](double) { return std::vector<hc::Capsule> { Monster(0, TipZ(hh::Kind::Sword) - 0.12 - r, r) }; };
	const Run sw = Play(sword);
	std::printf("  a sword thrust point first: in %s, %.1f cm deep at most, at the end %s\n", sw.entered ? "yes" : "no", sw.deepest * 100, sw.inAtEnd ? "still in" : sw.why.c_str());
	EXPECT(sw.entered && sw.inAtEnd && sw.deepest > 0.05, "a sword sticks in too (%s)", sw.why.c_str());
	weightlib::SetPath(nullptr);
}

}   // namespace

int main(int argc, char** argv) {
	std::printf("stab_test: a thrusting weapon sticks into a body (the seam; the library with dll=<path>)\n");
	TestBodyMotion();
	TestWiring();
	TestSliders();
	TestDebug();
	TestThrustingNow();
	for (int i = 1; i < argc; ++i)
		if (std::strncmp(argv[i], "dll=", 4) == 0) TestLibrary(argv[i] + 4);
	if (g_fails) {
		std::printf("stab_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("stab_test: all passed\n");
	return 0;
}
