// The held weapon and a bare hand against the level's walls ([weight] world_contact; cleanroom/heft/walls.h) on synthetic poses: a controller driven into
// boxes, the weight model's hold (hold.h) stepping the weapon at 90 Hz, without the game.
//
//   world_contact_test          every check below
//   world_contact_test table    the tracks of the scenes, for a look
//
// Checks: a thrust stops on a wall's face (without the wall it runs through); a fast cut across a thin wall never leaves the blade on the far side; the
// weapon bounces a little off and follows the controller back; a wall turned 45 degrees stops it on its own face; a wall that comes up round the weapon
// leaves it alone until it is clear (no throw); a controller far past the wall puts the weapon back in the hand through it, once; a bare hand stays on
// the face and slides along it, goes round a corner by the side face, never through a thin wall, and goes back to its controller past the return gap;
// the renderer's pieces judged as walls (vr/world_walls.h: a wall kept, a cup, a floor, a canopy over the hero left out; a turned piece's box); and the cost of a
// step against a room of walls.

#include "hold.h"
#include "walls.h"
#include "../../vr/world_walls.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hw = heft::walls;

namespace {

constexpr double kDt = 1.0 / 90.0;
int g_fails = 0;
bool g_table = false;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

// An axis-aligned wall from lo to hi.
hw::Wall Box(V3 lo, V3 hi, uint32_t id = 1) {
	hw::Wall w;
	w.c = Mul(Add(lo, hi), 0.5);
	w.half = Mul(Sub(hi, lo), 0.5);
	w.id = id;
	return w;
}

// How far a point is inside a wall (m; <= 0 outside).
double Inside(const hw::Wall& w, V3 p) {
	const V3 l = hw::Local(w, p);
	return std::min({ w.half.x - std::fabs(l.x), w.half.y - std::fabs(l.y), w.half.z - std::fabs(l.z) });
}

hh::HandIn HandAt(const Pose& p, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose = p;
	h.grip = tracked ? 1.0f : 0.0f;
	h.fistLine = { 0, 0, -1 };
	return h;
}

using PathFn = Pose (*)(double t, const void* ctx);

struct Run {
	double deepest = -1;      // the tip's deepest inside any wall (m)
	double farSide = -1e9;    // the tip's most past a reference plane (the far side of a thin wall), m
	int    contactSteps = 0;
	double maxApproach = 0;
	double maxJump = 0;       // the largest move of the tip in one step
	double lagEnd = 0;        // at the end: the tip from where the controller has it
	int    returns = 0;
	int    ignoredSteps = 0;
	V3     tipEnd;
	std::vector<V3> tips;
};

// The sword (or kind) in the right hand, its shaft along the controller's -Z (forward), stepped along a path with the walls (when on).
template <class F>
Run Swing(hh::Kind kind, F path, double secs, const std::vector<hw::Wall>& walls, bool on, const char* name = nullptr, double farZ = 1e9, int farAxis = 2,
          double farSign = -1) {
	Run r;
	hh::Settings s;
	s.walls = on;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt(path(0.0)), HandAt({ { -0.3, 1.0, 0 }, {} }, false) };
	in[1].grip = 0;
	h.Place(0, kind, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	const hh::Shape& shape = h.WeaponShape();
	V3 lastTip;
	h.Ends(nullptr, &lastTip);
	const int steps = (int)std::ceil(secs / kDt);
	for (int i = 1; i <= steps; ++i) {
		const double t = i * kDt;
		in[0] = HandAt(path(t));
		if (on) h.SetWalls(walls.data(), (int)walls.size());
		h.Step(in, kDt, s, out);
		V3 tip;
		h.Ends(nullptr, &tip);
		r.maxJump = std::max(r.maxJump, Len(Sub(tip, lastTip)));
		const hw::Report& c = h.LastWall();
		if (c.walls > 0) {
			++r.contactSteps;
			r.maxApproach = std::max(r.maxApproach, c.approach);
		}
		if (c.ignored > 0) ++r.ignoredSteps;
		for (const hw::Wall& w : walls) r.deepest = std::max(r.deepest, Inside(w, tip));
		const double past = farSign * (farAxis == 0 ? tip.x : farAxis == 1 ? tip.y : tip.z) - farSign * farZ;
		r.farSide = std::max(r.farSide, past);
		r.tips.push_back(tip);
		if (g_table && name)
			std::printf("  %-20s t %.3f  ctl (%.2f %.2f %.2f)  tip (%.3f %.3f %.3f)  walls %d depth %.1f cm approach %.2f face %d%s\n", name, t, in[0].pose.pos.x, in[0].pose.pos.y,
			            in[0].pose.pos.z, tip.x, tip.y, tip.z, c.walls, c.depth * 100, c.approach, c.face, c.returned ? " RETURNED" : "");
		lastTip = tip;
	}
	r.returns = h.WallReturns();
	h.Ends(nullptr, &r.tipEnd);
	const Pose last = path(secs);
	const V3 ctlTip = Add(last.pos, Rotate(last.rot, { 0, 0, -shape.tip }));
	r.lagEnd = Len(Sub(r.tipEnd, ctlTip));
	return r;
}

// The sword's tip with the hand at the origin, at rest (its z: the shaft along -Z).
double RestTipZ(hh::Kind kind) {
	hh::Hold probe;
	hh::Settings s;
	hh::HandIn in[2] = { HandAt({ { 0, 1.2, 0 }, {} }), HandAt({}, false) };
	probe.Place(0, kind, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	V3 tip;
	probe.Ends(nullptr, &tip);
	return tip.z;
}

double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}

// The thrust: the sword forward, the hand pushed 0.5 m forward in 0.25 s, held, back. A wall whose face is 0.25 m past the tip at rest.
void TestThrust() {
	auto path = [](double t) {
		const double out = Smooth(t / 0.25) - Smooth((t - 0.8) / 0.25);
		return Pose { { 0, 1.2, -0.5 * out }, {} };
	};
	hh::Hold probe;
	{
		hh::Settings s;
		hh::HandIn in[2] = { HandAt(path(0)), HandAt({}, false) };
		probe.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	}
	V3 tip0;
	probe.Ends(nullptr, &tip0);
	const double face = tip0.z - 0.25;
	const std::vector<hw::Wall> walls { Box({ -2, 0, face - 0.5 }, { 2, 3, face }) };
	const Run off = Swing(hh::Kind::Sword, path, 1.4, walls, false);
	const Run on = Swing(hh::Kind::Sword, path, 1.4, walls, true, "thrust");
	std::printf("thrust: without the wall the tip %.1f cm in, with it %.1f cm (%d steps on it, %.2f m/s in); back at the end %.1f cm off the controller's\n", off.deepest * 100,
	            on.deepest * 100, on.contactSteps, on.maxApproach, on.lagEnd * 100);
	EXPECT(off.deepest > 0.15, "without the wall the thrust goes in: %.1f cm", off.deepest * 100);
	EXPECT(on.deepest < 0.015, "the thrust stops on the face: %.1f cm in", on.deepest * 100);
	EXPECT(on.contactSteps > 10, "it rests on the face while the hand is held past it: %d steps", on.contactSteps);
	EXPECT(on.lagEnd < 0.03, "the hand back: the weapon in the fist again (%.1f cm off)", on.lagEnd * 100);
	EXPECT(on.returns == 0, "25 cm past is no return: %d", on.returns);
}

// A fast cut across a thin wall (a palisade, 8 cm): the hand sweeps 1.2 m right to left in 0.12 s (the tip at ~10 m/s), the wall across the blade's path.
void TestFastCut() {
	auto path = [](double t) {
		const double u = Smooth(t / 0.12);
		const double a = -0.9 + 1.8 * u;   // the swing's angle about the shoulder
		return Pose { { 0, 1.2, 0 }, QFromAxisAngle({ 0, 1, 0 }, -a) };
	};
	// the sword turns about the hand (y), its tip on a circle of its length; a wall at x in [-0.04, 0.04] from z -0.3 to -1.5
	const double tip = RestTipZ(hh::Kind::Sword);
	const std::vector<hw::Wall> walls { Box({ -0.04, 0.0, tip - 0.5 }, { 0.04, 3.0, 0.5 * tip }) };
	const Run off = Swing(hh::Kind::Sword, path, 0.4, walls, false, nullptr, 0.04, 0, -1);
	const Run on = Swing(hh::Kind::Sword, path, 0.4, walls, true, "fast cut", 0.04, 0, -1);
	// the far side: the one the cut never came from
	const bool fromRight = off.tips.front().x > 0;
	double worstFar = -1e9, worstIn = -1;
	for (const V3& p : on.tips) {
		const double past = fromRight ? -p.x - 0.04 : p.x - 0.04;   // the tip beyond the far face
		if (p.z < walls[0].c.z + walls[0].half.z - 0.05 && p.z > walls[0].c.z - walls[0].half.z + 0.05) worstFar = std::max(worstFar, past);
		worstIn = std::max(worstIn, Inside(walls[0], p));
	}
	double maxStep = 0;
	for (size_t i = 1; i < off.tips.size(); ++i) maxStep = std::max(maxStep, Len(Sub(off.tips[i], off.tips[i - 1])));
	std::printf("fast cut: the tip's fastest step %.1f cm (%.1f m/s); with the wall %d steps on it at up to %.2f m/s, the tip at most %.1f cm past the far face, %.1f cm in it\n",
	            maxStep * 100, maxStep / kDt, on.contactSteps, on.maxApproach, worstFar * 100, worstIn * 100);
	EXPECT(maxStep / kDt > 6.0, "the cut is fast: %.1f m/s", maxStep / kDt);
	EXPECT(worstFar < 0.0, "never on the far side: %.1f cm past it", worstFar * 100);
	EXPECT(worstIn < 0.02, "never in the wall: %.1f cm", worstIn * 100);
	EXPECT(on.contactSteps > 0 && on.maxApproach > 2.0, "it struck it hard: %d steps, %.2f m/s", on.contactSteps, on.maxApproach);
}

// A bounce: the tip struck into the face comes a little back off it (restitution), never on into it.
void TestBounce() {
	hw::Wall w = Box({ -2, 0, -2 }, { 2, 3, -0.9 });
	hw::Memory mem;
	hw::Params p;
	Body b;
	b.mass = 1.5;
	b.inertia.m[0][0] = b.inertia.m[1][1] = b.inertia.m[2][2] = 0.05;
	State before;
	before.com = { 0, 1.2, -0.5 };
	before.rot = {};
	const contact::Blade blade { { 0, 0, 0 }, { 0, 0, -0.38 }, 0.02 };
	State st = before;
	st.com = { 0, 1.2, -0.6 };   // the tip at -0.98: 8 cm past the face
	st.vel = { 0, 0, -3.0 };
	hw::Report rep;
	const bool hit = hw::Resolve(st, &before, b, &blade, 1, &w, 1, mem, p, &rep);
	const V3 tip = PointAt(st, b, blade.tip);
	std::printf("bounce: hit %d, the tip at z %.3f (face -0.9 + 2 cm), velocity %.2f m/s out of the wall (in at 3.0), face %d\n", hit ? 1 : 0, tip.z, st.vel.z, rep.face);
	EXPECT(hit && rep.face == 4 && std::fabs(tip.z - (-0.88)) < 0.005, "on the face: tip z %.3f, face %d", tip.z, rep.face);
	EXPECT(st.vel.z > 0.1 && st.vel.z < 1.0, "a little bounce: %.2f m/s", st.vel.z);
	EXPECT(std::fabs(rep.approach - 3.0) < 0.05, "its speed in: %.2f", rep.approach);
}

// A wall turned 45 degrees about the vertical stops the thrust on its own face.
void TestTurnedWall() {
	auto path = [](double t) { return Pose { { 0, 1.2, -0.6 * Smooth(t / 0.3) }, {} }; };
	hw::Wall w;
	const Q yaw = QFromAxisAngle({ 0, 1, 0 }, 0.785398);
	w.axis[0] = Rotate(yaw, { 1, 0, 0 });
	w.axis[2] = Rotate(yaw, { 0, 0, 1 });
	w.half = { 1.0, 1.5, 0.15 };
	w.c = { 0, 1.5, -1.25 };
	w.id = 3;
	const std::vector<hw::Wall> walls { w };
	const Run on = Swing(hh::Kind::Sword, path, 0.8, walls, true, "turned wall");
	std::printf("turned wall: the tip at most %.1f cm in it, %d steps on it\n", on.deepest * 100, on.contactSteps);
	EXPECT(on.deepest < 0.015 && on.contactSteps > 5, "stopped on the turned face: %.1f cm in, %d steps", on.deepest * 100, on.contactSteps);
}

// A wall that comes up round the weapon (a new scan, a teleport): left alone - no throw - until the weapon is clear, then it stops it again.
void TestWallAroundWeapon() {
	auto path = [](double t) {
		// the hand still for 0.3 s, then back 0.6 m (out of the wall), then forward again into it
		const double z = 0.6 * Smooth((t - 0.3) / 0.3) - 0.6 * Smooth((t - 0.9) / 0.3);
		return Pose { { 0, 1.2, z }, {} };
	};
	// the wall round the sword's outer part from the start
	const double tip = RestTipZ(hh::Kind::Sword);
	const std::vector<hw::Wall> walls { Box({ -1, 0, tip - 0.2 }, { 1, 3, tip + 0.25 }) };
	const Run on = Swing(hh::Kind::Sword, path, 1.4, walls, true, "wall round it", 1e9);
	std::printf("wall round the weapon: the largest move of the tip in a step %.1f cm, %d steps left alone, then %d steps on it\n", on.maxJump * 100, on.ignoredSteps, on.contactSteps);
	EXPECT(on.maxJump < 0.06, "no throw: %.1f cm in a step", on.maxJump * 100);
	EXPECT(on.ignoredSteps >= 20, "left alone while inside: %d steps", on.ignoredSteps);
	EXPECT(on.contactSteps > 3, "met again once it came out and went back: %d steps", on.contactSteps);
}

// The controller driven 1 m past the face: the weapon goes back into the hand through the wall - once - and the wall leaves it alone until it is clear.
void TestReturn() {
	auto path = [](double t) { return Pose { { 0, 1.2, -1.2 * Smooth(t / 0.4) + 1.2 * Smooth((t - 1.0) / 0.4) }, {} }; };
	const double tip = RestTipZ(hh::Kind::Sword);
	const std::vector<hw::Wall> walls { Box({ -1, 0, tip - 1.5 }, { 1, 3, tip - 0.1 }) };
	const Run on = Swing(hh::Kind::Sword, path, 1.8, walls, true, "return");
	std::printf("return: %d return(s) into the hand, %d steps left alone, back at the end %.1f cm off\n", on.returns, on.ignoredSteps, on.lagEnd * 100);
	EXPECT(on.returns == 1, "once: %d", on.returns);
	EXPECT(on.ignoredSteps > 10, "the wall leaves it alone after: %d", on.ignoredSteps);
	EXPECT(on.lagEnd < 0.03, "back in the fist at the end: %.1f cm", on.lagEnd * 100);
}

// A bare hand: on the face, sliding along it, round a corner by the side face, never through a thin wall, back to the controller past the return gap.
void TestHand() {
	hw::Params p;
	const double r = 0.05;
	auto step = [&](hw::Memory& mem, const std::vector<hw::Wall>& walls, V3 a, V3& drawnA, bool& have, hw::Report* rep) {
		const V3 b = Add(a, V3 { 0, 0, -0.1 });
		const V3 wasA = drawnA, wasB = Add(drawnA, V3 { 0, 0, -0.1 });
		const V3 off = hw::Block(a, b, r, have ? &wasA : nullptr, have ? &wasB : nullptr, walls.data(), (int)walls.size(), mem, p, rep);
		drawnA = Add(a, off);
		have = true;
		return off;
	};
	{   // pressed 20 cm into the face, then slid sideways along it
		const std::vector<hw::Wall> walls { Box({ -2, 0, -1.5 }, { 2, 3, -0.6 }) };
		hw::Memory mem;
		V3 drawn;
		bool have = false;
		double worst = 0, xEnd = 0;
		for (int i = 0; i <= 90; ++i) {
			const double t = i / 90.0;
			const V3 a { 0.6 * Smooth((t - 0.5) / 0.5), 1.2, -0.3 - 0.4 * Smooth(t / 0.4) };   // the palm's capsule from a to a - 10 cm: its front 20 cm past the face
			hw::Report rep;
			step(mem, walls, a, drawn, have, &rep);
			worst = std::max(worst, drawn.z - 0.1 - r - (-0.6) < 0 ? -(drawn.z - 0.1 - r + 0.6) : 0.0);
			xEnd = drawn.x;
		}
		std::printf("hand: pressed into the face it stays %.1f cm in at most; slid along to x %.2f (the controller 0.60)\n", worst * 100, xEnd);
		EXPECT(worst < 0.002, "on the face: %.1f cm in", worst * 100);
		EXPECT(std::fabs(xEnd - 0.6) < 0.01, "slides along it with the controller: %.2f", xEnd);
	}
	{   // a thin wall (6 cm) and the hand jumping 30 cm through it in one step: it stays on the near side
		const std::vector<hw::Wall> walls { Box({ -2, 0, -0.73 }, { 2, 3, -0.67 }) };
		hw::Memory mem;
		V3 drawn;
		bool have = false;
		step(mem, walls, { 0, 1.2, -0.4 }, drawn, have, nullptr);
		hw::Report rep;
		step(mem, walls, { 0, 1.2, -0.75 }, drawn, have, &rep);   // the front of the capsule at -0.90: through the wall and out of it
		std::printf("hand through a thin wall in one step: drawn at z %.3f (its front %.3f, the wall's near face -0.67)\n", drawn.z, drawn.z - 0.1 - r);
		EXPECT(drawn.z - 0.1 - r > -0.672, "on the near side: front at %.3f", drawn.z - 0.1 - r);
	}
	{   // round a corner: the hand slides along the front face past the wall's end, then back along the end face - it leaves by the end, not the front
		const std::vector<hw::Wall> walls { Box({ -2, 0, -1.5 }, { 0.5, 3, -0.6 }) };
		hw::Memory mem;
		V3 drawn;
		bool have = false;
		step(mem, walls, { 1.0, 1.2, -0.3 }, drawn, have, nullptr);
		// beside the end (x > 0.5 + r), the hand goes in along -z past the front's line, then left into the end face
		for (int i = 0; i <= 30; ++i) step(mem, walls, { 1.0, 1.2, -0.3 - 0.6 * i / 30.0 }, drawn, have, nullptr);
		hw::Report rep;
		for (int i = 0; i <= 30; ++i) step(mem, walls, { 1.0 - 0.6 * i / 30.0, 1.2, -0.9 }, drawn, have, &rep);
		std::printf("hand round a corner: drawn at x %.3f z %.3f (the end face at x 0.5, the hand's radius %.2f), face %d\n", drawn.x, drawn.z, r, rep.face);
		EXPECT(std::fabs(drawn.x - (0.5 + r)) < 0.005 && std::fabs(drawn.z + 0.9) < 0.005, "on the end face: x %.3f z %.3f", drawn.x, drawn.z);
	}
	{   // the controller far past the face: the hand back at the controller, the wall alone until the hand is out of it
		const std::vector<hw::Wall> walls { Box({ -2, 0, -2.5 }, { 2, 3, -0.6 }) };
		hw::Memory mem;
		V3 drawn;
		bool have = false;
		int returned = 0;
		double gapMax = 0;
		for (int i = 0; i <= 60; ++i) {
			hw::Report rep;
			step(mem, walls, { 0, 1.2, -0.3 - 0.8 * Smooth(i / 60.0) }, drawn, have, &rep);
			returned += rep.returned;
			gapMax = std::max(gapMax, rep.gap);
		}
		const bool atController = std::fabs(drawn.z - (-1.1)) < 1e-6;
		std::printf("hand far past: returned %d time(s), the gap up to %.2f m, drawn at the controller at the end: %d\n", returned, gapMax, atController ? 1 : 0);
		EXPECT(returned == 1 && atController, "back at the controller once: %d, z %.3f", returned, drawn.z);
	}
	{   // a wall that comes up round the hand: alone
		const std::vector<hw::Wall> walls { Box({ -2, 0, -1.5 }, { 2, 3, -0.2 }) };
		hw::Memory mem;
		V3 drawn;
		bool have = false;
		hw::Report rep;
		const V3 off = step(mem, walls, { 0, 1.2, -0.5 }, drawn, have, &rep);
		EXPECT(Len(off) < 1e-9 && rep.ignored == 1, "alone: off %.3f, ignored %d", Len(off), rep.ignored);
	}
}

// A dry-stone wall of many boxes, its face stepped 15 cm in and out (Serg, 0.192.15: "если меч сталкивается с кирпичной стеной его начинает колбасить", "чтобы он не
// считал их разными"): the sword pressed into it and slid along it for a second rides over the steps - the tip never goes back along the wall, never deep in it - and
// the whole wall is one body: one strike, not one for each box.
void TestSteppedWall() {
	const double tipZ = RestTipZ(hh::Kind::Sword);
	std::vector<hw::Wall> walls;
	for (int i = 0; i < 10; ++i) {
		const double front = tipZ - 0.15 + ((i & 1) ? 0.15 : 0.0);   // every other stone stands 15 cm out
		hw::Wall w = Box({ -1.5 + 0.3 * i, 0.0, tipZ - 0.9 }, { -1.2 + 0.3 * i, 2.5, front }, (uint32_t)(100 + i));
		w.body = 7;
		walls.push_back(w);
	}
	for (int bodies = 0; bodies < 2; ++bodies) {
		std::vector<hw::Wall> ws = walls;
		if (!bodies)
			for (auto& w : ws) w.body = 0;   // (each box its own: the old way)
		hh::Settings s;
		s.walls = true;
		hh::Hold h;
		auto path = [](double t) {   // pushed 25 cm in, then slid 1.2 m to the right along the wall
			return Pose { { -0.6 + 1.2 * Smooth((t - 0.3) / 1.0), 1.2, -0.25 * Smooth(t / 0.25) }, {} };
		};
		hh::HandIn in[2] = { HandAt(path(0)), HandAt({ { -0.3, 1.0, 0 }, {} }, false) };
		in[1].grip = 0;
		h.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
		hh::HandOut out[2];
		V3 last;
		h.Ends(nullptr, &last);
		double back = 0, deepest = 0;
		int strikes = 0, off = 100;
		uint32_t was = 0;
		for (int i = 1; i <= (int)std::ceil(1.5 / kDt); ++i) {
			in[0] = HandAt(path(i * kDt));
			h.SetWalls(ws.data(), (int)ws.size());
			h.Step(in, kDt, s, out);
			V3 tip;
			h.Ends(nullptr, &tip);
			const hw::Report& c = h.LastWall();
			if (c.walls > 0 && (c.id != was || off >= 10)) ++strikes;   // (a new body, or the same one again after a pause: a new strike)
			off = c.walls > 0 ? 0 : off + 1;
			if (g_table && bodies)
				std::printf("  stepped t %.3f ctl x %.3f  tip (%.3f %.3f %.3f) walls %d id %u face %d depth %.1f cm\n", i * kDt, in[0].pose.pos.x, tip.x, tip.y, tip.z, c.walls, c.id, c.face, c.depth * 100);
			if (c.walls > 0) was = c.id;
			if (i * kDt > 0.4 && i * kDt < 1.3) back = std::max(back, last.x - tip.x);   // (while the hand slides right)
			for (const hw::Wall& w : ws) deepest = std::max(deepest, Inside(w, tip));
			last = tip;
		}
		std::printf("stepped wall (%s): the tip went back along the wall at most %.1f cm, at most %.1f cm in a stone, %d strike(s)\n", bodies ? "one body" : "each box its own",
		            back * 100, deepest * 100, strikes);
		if (bodies) {
			EXPECT(back < 0.01, "rides over the steps, never back: %.1f cm", back * 100);
			EXPECT(deepest < 0.06, "never deep in a stone: %.1f cm", deepest * 100);
			EXPECT(strikes == 1, "one strike for the one wall: %d", strikes);
		}
	}
}

// The cost: a room of 48 walls round the hero, a swing through it.
void TestCost() {
	std::vector<hw::Wall> walls;
	for (int i = 0; i < 48; ++i) {
		const double a = i * 0.13;
		walls.push_back(Box({ std::cos(a) * (1.0 + 0.05 * i), 0, std::sin(a) * 1.5 - 0.3 }, { std::cos(a) * (1.0 + 0.05 * i) + 0.4, 2.5, std::sin(a) * 1.5 + 0.1 }, (uint32_t)i + 1));
	}
	auto path = [](double t) { return Pose { { 0, 1.2, 0 }, QFromAxisAngle({ 0, 1, 0 }, 2.5 * std::sin(t * 6.0)) }; };
	const auto t0 = std::chrono::steady_clock::now();
	const Run on = Swing(hh::Kind::Sword, path, 2.0, walls, true);
	const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / std::ceil(2.0 / kDt);
	const auto t1 = std::chrono::steady_clock::now();
	Swing(hh::Kind::Sword, path, 2.0, walls, false);
	const double base = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count() / std::ceil(2.0 / kDt);
	std::printf("cost: %.1f us a step with 48 walls (%.1f without), %d steps on a wall\n", us, base, on.contactSteps);
	EXPECT(us - base < 60.0, "cheap: %.1f us more a step", us - base);
}

// vr/world_walls.h: the renderer's pieces (render units, 4 a metre) matched to the solids catalogue (shared/world_solids.h), their parts as boxes, and which are walls
// for a hero standing at the origin.
void TestWorldWalls() {
	const float upm = 4.0f, feet[3] = { 0, 0, 0 };
	std::vector<worldsolids::Model> models;
	std::vector<worldsolids::Row> rows;
	auto piece = [&](uint32_t id, float x, float y, float z, float sx, float sy, float sz, float yawDeg = 0.0f, float scale = 1.0f) {
		worldwalls::Entity e;
		e.id = id;
		e.preset = 1000 + id;
		e.local[0] = (float)id;
		const float a = yawDeg * 0.0174532925f, c = std::cos(a) * scale, sn = std::sin(a) * scale;
		const float m[16] = { c, 0, -sn, 0, 0, scale, 0, 0, sn, 0, c, 0, x * upm, y * upm, z * upm, 1 };
		std::memcpy(e.world, m, sizeof m);
		const float lo[3] = { -sx * 0.5f * upm / scale, 0, -sz * 0.5f * upm / scale }, hi[3] = { sx * 0.5f * upm / scale, sy * upm / scale, sz * 0.5f * upm / scale };
		std::memcpy(e.lo, lo, sizeof lo);
		std::memcpy(e.hi, hi, sizeof hi);
		return e;
	};
	// its model: one part as big as its box (flags), or the parts given (model units, the box's own frame)
	auto model = [&](const worldwalls::Entity& e, uint16_t flags = 0, std::vector<worldsolids::Part> parts = {}) {
		worldsolids::Model m;
		m.flags = flags;
		std::memcpy(m.lo, e.lo, sizeof m.lo);
		std::memcpy(m.hi, e.hi, sizeof m.hi);
		if (parts.empty() && !flags) parts.push_back({ { e.lo[0], e.lo[1], e.lo[2] }, { e.hi[0], e.hi[1], e.hi[2] } });
		m.parts = parts;
		models.push_back(m);
		worldsolids::Row r;
		r.id = e.preset;
		r.x = e.local[0];
		r.models[0] = (uint32_t)(models.size() - 1);
		rows.push_back(r);
	};
	std::vector<worldwalls::Entity> es {
		piece(1, 0, 0, -1.5f, 3, 2.5f, 0.3f),            // a wall 1.5 m ahead with a doorway 1 m wide, 2 m tall in its middle
		piece(2, 0.5f, 0.8f, -0.5f, 0.1f, 0.15f, 0.1f),  // a cup on a table
		piece(3, 0, -0.1f, 0, 6, 0.1f, 6),               // the floor under him
		piece(4, 0, 0, 0, 5, 7, 5),                      // a box round him
		piece(5, 2, 3.0f, 0, 2, 0.5f, 2),                // a beam over his head
		piece(6, -1.2f, 0, -0.4f, 0.6f, 0.9f, 0.6f, 45.0f, 2.0f),   // a barrel, turned 45 degrees, its model scaled x2
		piece(7, 1.5f, 0, 0, 1, 1, 1),                   // not in the catalogue
		piece(8, -1.5f, 0, 0, 1, 1, 1),                  // a bush
		piece(9, 0, 0, 1.6f, 1, 1, 1),                   // its mesh half the renderer's box: stretched onto it
		piece(10, 1.0f, 0, 1.0f, 0.5f, 0.6f, 0.5f),      // a big jug the props catalogue knows (movable: the caller says so)
		piece(11, -1.0f, 0, 1.0f, 0.5f, 0.6f, 0.5f),     // a vase whose model the solids catalogue marks movable
		piece(12, 3.0f, 0, 3.0f, 5, 3, 5),               // a big piece not in the catalogue (a building's shell): left out
		piece(13, 0.75f, 0, -1.5f, 0.5f, 2.0f, 0.3f),    // a post touching the wall's right end: one body with it
	};
	const float u = upm;
	model(es[0], 0, { { { -1.5f * u, 0, -0.15f * u }, { -0.5f * u, 2.5f * u, 0.15f * u } }, { { 0.5f * u, 0, -0.15f * u }, { 1.5f * u, 2.5f * u, 0.15f * u } },
	                  { { -0.5f * u, 2.0f * u, -0.15f * u }, { 0.5f * u, 2.5f * u, 0.15f * u } } });
	for (int i = 1; i <= 5; ++i) model(es[(size_t)i]);
	model(es[7], worldsolids::kIgnore);
	model(es[10], worldsolids::kMovable);
	model(es[12]);
	{
		worldwalls::Entity half = es[8];
		for (int k = 0; k < 3; ++k) { half.lo[k] *= 0.5f; half.hi[k] *= 0.5f; }
		model(half);
	}
	const std::vector<uint8_t> data = worldsolids::Encode(models, rows);
	worldsolids::Catalog cat;
	EXPECT(cat.Parse(data.data(), data.size()), "the catalogue reads");
	worldwalls::Counts c;
	const std::vector<worldwalls::Box> kept = worldwalls::Choose(es, cat, upm, feet, &c, [](uint32_t preset, const float*) { return preset == 1010; });
	using worldwalls::Why;
	std::printf("world walls: %u kept of %u parts of %u pieces (%u in the catalogue, %u stretched; unknown %u, foliage %u, tiny %u, low %u, round %u, high %u)\n", c.kept, c.parts,
	            c.seen, c.matched, c.stretched, c.why[(int)Why::kUnknown], c.why[(int)Why::kFoliage], c.why[(int)Why::kTiny], c.why[(int)Why::kLow], c.why[(int)Why::kRound],
	            c.why[(int)Why::kHigh]);
	std::printf("world walls: movable %u, laid by their own box %u (not in the catalogue %u), big %u; %u bodies, %u of them pieces joined\n", c.why[(int)Why::kMovable], c.objects,
	            c.why[(int)Why::kUnknown], c.why[(int)Why::kBig], c.bodies, c.joined);
	EXPECT(c.kept == 7 && c.why[(int)Why::kTiny] == 1 && c.why[(int)Why::kLow] == 1 && c.why[(int)Why::kRound] == 1 && c.why[(int)Why::kHigh] == 1 &&
	       c.why[(int)Why::kUnknown] == 1 && c.objects == 1 && c.why[(int)Why::kFoliage] == 1 && c.stretched == 1 && c.why[(int)Why::kMovable] == 2 && c.why[(int)Why::kBig] == 1,
	       "which are walls");
	const float opening[3] = { 0, 1.0f, -1.5f }, jamb[3] = { -1.0f, 1.0f, -1.5f }, lintel[3] = { 0, 2.25f, -1.5f };
	bool open = true, jambSolid = false, lintelSolid = false;
	const worldwalls::Box* barrel = nullptr;
	const worldwalls::Box* grown = nullptr;
	for (const worldwalls::Box& b : kept) {
		open = open && worldwalls::Outside(b, opening, 0) > 0;
		jambSolid = jambSolid || worldwalls::Outside(b, jamb, 0) < 0;
		lintelSolid = lintelSolid || worldwalls::Outside(b, lintel, 0) < 0;
		if (b.entity == 6) barrel = &b;
		if (b.entity == 9) grown = &b;
	}
	bool oneWall = true, postJoined = false, barrelApart = true, objectThere = false;
	for (const worldwalls::Box& b : kept) {
		if (b.entity == 1) oneWall = oneWall && b.body == 1;
		if (b.entity == 13) postJoined = b.body == 1;
		if (b.entity == 6) barrelApart = b.body == 6;
		if (b.entity == 7) objectThere = true;
		if (b.entity == 10 || b.entity == 11 || b.entity == 12) EXPECT(false, "a movable prop or a big unknown piece is a wall: %u", b.entity);
	}
	EXPECT(oneWall && postJoined && barrelApart, "the wall's parts and the post touching it one body, the barrel its own: %d %d %d", oneWall, postJoined, barrelApart);
	EXPECT(objectThere, "a game object not in the catalogue laid by its own box (the camp's torch)");
	EXPECT(open && jambSolid && lintelSolid, "the doorway open, its jambs and lintel solid: %d %d %d", open, jambSolid, lintelSolid);
	EXPECT(barrel && std::fabs(barrel->half[0] - 0.3f) < 1e-4f && std::fabs(barrel->axis[0][0] - 0.7071f) < 1e-3f && std::fabs(barrel->axis[0][2] + 0.7071f) < 1e-3f,
	       "the barrel's box turned with it");
	EXPECT(grown && std::fabs(grown->half[0] - 0.5f) < 1e-4f && std::fabs(grown->half[1] - 0.5f) < 1e-4f, "the half-size mesh laid on the renderer's box: half %.3f %.3f",
	       grown ? grown->half[0] : 0.0f, grown ? grown->half[1] : 0.0f);
}

}   // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i)
		if (!std::strcmp(argv[i], "table")) g_table = true;
	TestThrust();
	TestFastCut();
	TestBounce();
	TestTurnedWall();
	TestWallAroundWeapon();
	TestReturn();
	TestHand();
	TestWorldWalls();
	TestSteppedWall();
	TestCost();
	if (g_fails) {
		std::printf("world_contact_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("world_contact_test: all passed\n");
	return 0;
}
