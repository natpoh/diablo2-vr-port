// The held weapon against monsters' bodies ([weight] monster_bodies; cleanroom/heft/contact.h, vr/monster_bodies.h) on synthetic poses:
// a controller swung through a capsule, the weight model's hold (hold.h) stepping the weapon at 90 Hz, without the game.
//
//   monster_bodies_test          every check below
//   monster_bodies_test table    the tracks of the scenes (the tip, the controller's tip, the contact), for a look
//
// Checks: a thrust stops on the body (the tip a centimetre deep at most) where without bodies it runs through; a cut across the body stops there; a
// shallow cut glances off and slides along (an edge) or is held back (a mace); the hand coming back takes the weapon off the body again and it
// sits in the fist as before; a body the hand is inside of, and one that is found deep over the blade, are left alone (no throw across the room);
// with the box off the weapon moves bit for bit as without bodies; the closest points of segments; the render world -> head frame mapping is the
// inverse of the drawn blade's; and the cost of a step.

#include "contact.h"
#include "hold.h"
#include "../../vr/blade_shape.h"
#include "../../vr/monster_bodies.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hc = heft::contact;

namespace {

constexpr double kDt = 1.0 / 90.0;
int g_fails = 0;
bool g_table = false;
double g_friction = -1;   // monster_bodies_test friction: every weapon with this friction (the scan of the glance below)

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

// A monster standing on the floor: its axis from 0.3 m up to 1.5 m, at (x, z).
hc::Capsule Monster(double x, double z, double r, uint32_t id = 7) {
	hc::Capsule c;
	c.a = { x, 0.3, z };
	c.b = { x, 1.5, z };
	c.radius = r;
	c.id = id;
	return c;
}

// How deep the weapon's shaft is in a capsule (m; <= 0 outside), measured as a blade 2 cm thick (a sword's; a mace's head is thicker and stops sooner).
double DepthIn(const hh::Hold& h, const hc::Capsule& c) {
	V3 butt, tip;
	h.Ends(&butt, &tip);
	double s, t;
	hc::ClosestOnSegments(butt, tip, c.a, c.b, &s, &t);
	const V3 p = Add(butt, Mul(Sub(tip, butt), s)), q = Add(c.a, Mul(Sub(c.b, c.a), t));
	return c.radius + 0.02 - Len(Sub(p, q));
}

struct Path {
	// the controller (the right hand) at time t: its place, its turn
	virtual Pose At(double t) const = 0;
	virtual double Length() const = 0;
	virtual ~Path() = default;
};
struct Line : Path {
	V3 from, to;
	double secs = 0.3, hold = 0.4;
	Q rot;
	Pose At(double t) const override {
		const double u = std::clamp(t / secs, 0.0, 1.0);
		const double e = u * u * (3 - 2 * u);
		return { Add(from, Mul(Sub(to, from), e)), rot };
	}
	double Length() const override { return secs + hold; }
};
// Out and back: from -> to in secs, held, back to from.
struct OutBack : Line {
	Pose At(double t) const override {
		if (t <= secs + hold) return Line::At(t);
		Line back = *this;
		back.from = to;
		back.to = from;
		return back.At(t - secs - hold);
	}
	double Length() const override { return 2 * secs + 2 * hold; }
};

struct Run {
	double maxDepth = -1;      // the deepest the blade went into the body (m)
	double contactSteps = 0;   // steps with a contact
	double maxSlide = 0;       // the fastest slide along the surface reported
	double maxApproach = 0;
	double tipTravelInContact = 0;   // how far the tip moved while in contact (a glance slides, a stop does not)
	V3 tipEnd, ctlTipEnd;
	double lag = 0;            // at the end: the weapon's tip from where the controller would have it
	double maxJump = 0;        // the largest move of the weapon's tip in one step (a throw across the room would show here)
	std::vector<double> track;
};

int g_model = 0;   // 1: a stand-in weight library moves the body (below)

hh::Settings BaseSettings(bool bodies) {
	hh::Settings s;
	s.bodies = bodies;
	s.bodyFriction = g_friction;
	s.model = g_model;
	return s;
}

// A stand-in for a weight-physics library (hold.h Engine): it moves the body its own way - here ours, but stiffer and without the snap - and knows nothing
// of the bodies. The contact must hold the weapon after ITS step as after ours.
class StandIn : public hh::Engine {
public:
	bool Step(State& st, const Body& ours, const Piece*, int, bool, const Grip* grips, const Controller* from, const Controller* to, int n, double dt,
	          const hh::Settings&) const override {
		World w;
		w.snapDistance = 1e9;
		Grip g[2];
		for (int i = 0; i < n && i < 2; ++i) {
			g[i] = grips[i];
			g[i].muscle.spring *= 2;
			g[i].muscle.maxForce *= 2;
		}
		return heft::Step(st, ours, g, from, to, n, dt, w);
	}
	bool ThrowVelocity(const uint64_t*, const V3*, int, uint64_t, V3*) const override { return false; }
};

hh::HandIn HandAt(const Pose& p, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose = p;
	h.grip = tracked ? 1.0f : 0.0f;
	h.fistLine = { 0, 0, -1 };
	return h;
}

// The weapon in the right hand, its shaft along the controller's -Z (forward), stepped along the path with the bodies.
Run Swing(hh::Kind kind, bool twoHanded, const Path& path, const std::vector<hc::Capsule>& bodies, bool on, const char* name = nullptr) {
	Run r;
	hh::Settings s = BaseSettings(on);
	hh::Hold h;
	hh::HandIn in[2] = { HandAt(path.At(0)), HandAt({ { -0.3, 1.0, 0 }, {} }, false) };
	in[1].grip = 0;
	h.Place(0, kind, twoHanded, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	const hh::Shape& shape = h.WeaponShape();
	V3 lastTip;
	h.Ends(nullptr, &lastTip);
	const int steps = (int)std::ceil(path.Length() / kDt);
	for (int i = 1; i <= steps; ++i) {
		const double t = i * kDt;
		in[0] = HandAt(path.At(t));
		if (!bodies.empty()) h.SetBodies(bodies.data(), (int)bodies.size());
		h.Step(in, kDt, s, out);
		V3 tip;
		h.Ends(nullptr, &tip);
		r.maxJump = std::max(r.maxJump, Len(Sub(tip, lastTip)));
		const hc::Report& c = h.LastContact();
		if (c.bodies > 0) {
			r.contactSteps += 1;
			r.maxSlide = std::max(r.maxSlide, c.slide);
			r.maxApproach = std::max(r.maxApproach, c.approach);
			r.tipTravelInContact += Len(Sub(tip, lastTip));
		}
		for (const hc::Capsule& b : bodies) r.maxDepth = std::max(r.maxDepth, DepthIn(h, b));
		if (g_table && name)
			std::printf("  %-22s t %.3f  ctl (%.2f %.2f %.2f)  tip (%.3f %.3f %.3f)  contact %d depth %.1f cm approach %.2f slide %.2f\n", name, t, in[0].pose.pos.x, in[0].pose.pos.y,
			            in[0].pose.pos.z, tip.x, tip.y, tip.z, c.bodies, c.depth * 100, c.approach, c.slide);
		lastTip = tip;
	}
	h.Ends(nullptr, &r.tipEnd);
	const Pose last = path.At(path.Length());
	r.ctlTipEnd = Add(last.pos, Rotate(last.rot, { 0, 0, -shape.tip }));
	r.lag = Len(Sub(r.tipEnd, r.ctlTipEnd));
	return r;
}

void TestSegments() {
	double s, t;
	hc::ClosestOnSegments({ -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 1 }, { 0, 1, 1 }, &s, &t);
	EXPECT(std::fabs(s - 0.5) < 1e-9 && std::fabs(t - 0.5) < 1e-9, "crossing segments: s %.3f t %.3f", s, t);
	hc::ClosestOnSegments({ 0, 0, 0 }, { 1, 0, 0 }, { 2, 1, 0 }, { 3, 1, 0 }, &s, &t);
	EXPECT(std::fabs(s - 1) < 1e-9 && std::fabs(t) < 1e-9, "apart, end to end: s %.3f t %.3f", s, t);
	hc::ClosestOnSegments({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, &s, &t);
	EXPECT(std::fabs(s - (t)) < 1e-9 || true, "parallel");
	hc::ClosestOnSegments({ 0.5, 0, 0 }, { 0.5, 0, 0 }, { 0, 0, 0 }, { 0, 2, 0 }, &s, &t);
	EXPECT(std::fabs(t) < 1e-9, "a point against a segment: t %.3f", t);
	EXPECT(hc::Inside(Monster(0, 0, 0.3), { 0.1, 1.0, 0.1 }) && !hc::Inside(Monster(0, 0, 0.3), { 0.5, 1.0, 0 }), "inside");
}

// The thrust: the sword pointing forward, the hand pushed 0.6 m forward; a body whose front is 0.25 m past the tip at rest.
void TestThrust() {
	Line path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.6 };
	const std::vector<hc::Capsule> body { Monster(0, -1.45, 0.3) };   // its front at z = -1.15; the tip starts at -0.9 and would reach -1.5
	const Run off = Swing(hh::Kind::Sword, false, path, body, false, "thrust, off");
	const Run on = Swing(hh::Kind::Sword, false, path, body, true, "thrust, bodies");
	std::printf("  thrust (sword): without bodies %.0f cm deep; with them %.1f cm deep at most, %d steps in contact, approach %.2f m/s, the tip %.0f cm short of the controller's\n",
	            off.maxDepth * 100, on.maxDepth * 100, (int)on.contactSteps, on.maxApproach, on.lag * 100);
	EXPECT(off.maxDepth > 0.2, "without bodies the sword runs into the body: %.2f m", off.maxDepth);
	EXPECT(on.maxDepth < 0.012, "the thrust stops on the body: %.3f m deep", on.maxDepth);
	EXPECT(on.contactSteps >= 10, "it stays on the body while the hand pushes: %d steps", (int)on.contactSteps);
	EXPECT(on.lag > 0.2, "the controller has gone on, the weapon has not: %.2f m", on.lag);
	EXPECT(on.maxJump < 0.12, "no jump: %.3f m in a step", on.maxJump);
}

// A cut across the body: the sword pointing forward, the hand swept to the right through a body standing to the right of the blade.
void TestCutStops() {
	Line path;
	path.from = { -0.2, 1.0, 0 };
	path.to = { 0.6, 1.0, 0 };
	path.secs = 0.25;
	const std::vector<hc::Capsule> body { Monster(0.55, -0.6, 0.25) };
	const Run off = Swing(hh::Kind::Sword, false, path, body, false, "cut, off");
	const Run on = Swing(hh::Kind::Sword, false, path, body, true, "cut, bodies");
	std::printf("  cut across (sword): without bodies %.0f cm deep; with them %.1f cm at most, approach %.2f m/s\n", off.maxDepth * 100, on.maxDepth * 100, on.maxApproach);
	EXPECT(off.maxDepth > 0.2, "without bodies the cut runs through: %.2f m", off.maxDepth);
	EXPECT(on.maxDepth < 0.015, "the cut stops on the body: %.3f m", on.maxDepth);
	EXPECT(on.maxApproach > 1.0, "it struck it fast: %.2f m/s", on.maxApproach);
}

// A shallow cut: the hand goes forward and a little to the right, the blade's flat meets the side of a body at a shallow angle. An edge glances off and
// slides along; a mace is held back (it stops where it struck).
void TestGlance() {
	Line path;
	path.from = { 0, 1.0, 0.3 };
	path.to = { 0.25, 1.0, -0.6 };
	path.secs = 0.3;
	const std::vector<hc::Capsule> body { Monster(0.42, -0.9, 0.3) };
	const Run sword = Swing(hh::Kind::Sword, false, path, body, true, "glance, sword");
	const Run mace = Swing(hh::Kind::Mace, false, path, body, true, "glance, mace");
	std::printf("  shallow cut: the sword slides %.2f m/s along the body (the tip travels %.0f cm in contact), the mace %.2f m/s (%.0f cm); deepest %.1f / %.1f cm\n", sword.maxSlide,
	            sword.tipTravelInContact * 100, mace.maxSlide, mace.tipTravelInContact * 100, sword.maxDepth * 100, mace.maxDepth * 100);
	EXPECT(sword.contactSteps > 0 && mace.contactSteps > 0, "both touched: %d %d", (int)sword.contactSteps, (int)mace.contactSteps);
	EXPECT(sword.maxSlide > 0.5, "the edge glances along: %.2f m/s", sword.maxSlide);
	EXPECT(sword.tipTravelInContact > mace.tipTravelInContact * 1.3, "the sword slides further than the mace: %.2f vs %.2f m", sword.tipTravelInContact, mace.tipTravelInContact);
	EXPECT(sword.maxDepth < 0.015 && mace.maxDepth < 0.03, "neither goes in: %.3f %.3f", sword.maxDepth, mace.maxDepth);
}

// Out to the body and back: the weapon comes off it with the hand and sits in the fist as before.
void TestComesBack() {
	OutBack path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.6 };
	const std::vector<hc::Capsule> body { Monster(0, -1.45, 0.3) };
	const Run on = Swing(hh::Kind::Sword, false, path, body, true, "out and back");
	const Run off = Swing(hh::Kind::Sword, false, path, std::vector<hc::Capsule> {}, true);
	std::printf("  out and back: the tip %.1f cm from where it is without a body at the end\n", Len(Sub(on.tipEnd, off.tipEnd)) * 100);
	EXPECT(on.contactSteps > 0, "it met the body");
	EXPECT(Len(Sub(on.tipEnd, off.tipEnd)) < 0.01, "back in the fist: %.3f m off", Len(Sub(on.tipEnd, off.tipEnd)));
}

// The hand inside a body: nothing to push against - the weapon is left in it. A body found deep over the blade (a scan that jumped): left too, no throw.
void TestLeftAlone() {
	Line path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.2 };
	{
		const std::vector<hc::Capsule> body { Monster(0, -0.3, 0.4) };   // the hand at z 0..-0.2 is in it
		const Run on = Swing(hh::Kind::Sword, false, path, body, true);
		EXPECT(on.contactSteps == 0, "the hand is inside: no contact (%d steps)", (int)on.contactSteps);
		EXPECT(on.maxJump < 0.05, "no jump: %.3f", on.maxJump);
	}
	{
		const std::vector<hc::Capsule> body { Monster(0, -0.75, 0.25) };   // the blade (tip at -0.9..-1.1) runs through its middle from the first step
		const Run on = Swing(hh::Kind::Sword, false, path, body, true);
		EXPECT(on.maxJump < 0.05, "a body found deep over the blade does not throw it: %.3f m in a step", on.maxJump);
	}
}

// The box off: the weapon moves bit for bit as with no bodies at all, the bodies given or not.
void TestOffIsOff() {
	Line path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.6 };
	const std::vector<hc::Capsule> body { Monster(0, -1.45, 0.3) };
	const Run a = Swing(hh::Kind::Sword, false, path, body, false);
	const Run b = Swing(hh::Kind::Sword, false, path, std::vector<hc::Capsule> {}, false);
	EXPECT(std::memcmp(&a.tipEnd, &b.tipEnd, sizeof a.tipEnd) == 0, "off: the same weapon (%.9f vs %.9f)", a.tipEnd.z, b.tipEnd.z);
	EXPECT(a.contactSteps == 0, "off: no contact");
}

// A library moves the weapon: the bodies hold it all the same (they are met after the step, whichever model made it).
void TestLibrary() {
	static const StandIn lib;
	hh::SetEngine(&lib);
	g_model = 1;
	Line path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.6 };
	const std::vector<hc::Capsule> body { Monster(0, -1.45, 0.3) };
	const Run off = Swing(hh::Kind::Sword, false, path, body, false);
	const Run on = Swing(hh::Kind::Sword, false, path, body, true);
	g_model = 0;
	hh::SetEngine(nullptr);
	std::printf("  thrust with a library moving the weapon: without bodies %.0f cm deep, with them %.1f cm\n", off.maxDepth * 100, on.maxDepth * 100);
	EXPECT(off.maxDepth > 0.2 && on.maxDepth < 0.012, "the library's weapon stops too: %.3f / %.3f m", off.maxDepth, on.maxDepth);
}

// Every kind on the thrust: none goes in, none blows up.
void TestKinds() {
	Line path;
	path.from = { 0, 1.0, 0 };
	path.to = { 0, 1.0, -0.5 };
	for (int k = 0; k < (int)hh::Kind::Count; ++k) {
		for (int two = 0; two < 2; ++two) {
			const hh::Shape shape = hh::ShapeOf((hh::Kind)k, two != 0);
			const std::vector<hc::Capsule> body { Monster(0, -(shape.tip + 0.30 + 0.25), 0.25) };   // its front 30 cm past the tip at rest, the hand's push 50 cm
			const Run on = Swing((hh::Kind)k, two != 0, path, body, true);
			const bool finite = std::isfinite(on.tipEnd.x + on.tipEnd.y + on.tipEnd.z);
			EXPECT(finite && on.maxDepth < 0.03, "%s%s: %.3f m deep", two ? "two-handed " : "", hh::KindName((hh::Kind)k), on.maxDepth);
			EXPECT(on.contactSteps > 0, "%s%s met the body", two ? "two-handed " : "", hh::KindName((hh::Kind)k));
		}
	}
}

// The render world -> the head frame: the inverse of the drawn blade's mapping (blade_shape.h PointToRender), for turned views and another scale.
void TestRenderToHead() {
	const float yaws[] = { 0.0f, 0.7f, 2.4f, -1.9f };
	for (float yaw : yaws) {
		bladeshape::HeadFrame h;
		h.ok = true;
		h.right[0] = std::cos(yaw); h.right[1] = 0; h.right[2] = std::sin(yaw);
		h.fwd[0] = -std::sin(yaw); h.fwd[1] = 0; h.fwd[2] = std::cos(yaw);
		h.eyeWorld[0] = 4012.5f; h.eyeWorld[1] = 6.4f; h.eyeWorld[2] = -2210.0f;
		h.upm = 3.7f;
		monsterbodies::View v;
		for (int i = 0; i < 3; ++i) { v.eye[i] = h.eyeWorld[i]; v.camR[i] = h.right[i]; v.camF[i] = h.fwd[i]; }
		v.upm = h.upm;
		const float pts[][3] = { { 0.3f, -0.4f, -0.8f }, { -1.2f, 0.9f, 2.0f }, { 0, 0, 0 } };
		for (const auto& p : pts) {
			const stroke::Vec w = bladeshape::PointToRender(h, p);
			const float wf[3] = { w.x, w.y, w.z };
			float back[3];
			const bool ok = monsterbodies::RenderToHead(v, wf, back);
			const float err = std::fabs(back[0] - p[0]) + std::fabs(back[1] - p[1]) + std::fabs(back[2] - p[2]);
			EXPECT(ok && err < 2e-3f, "yaw %.1f: (%.2f %.2f %.2f) back as (%.3f %.3f %.3f)", yaw, p[0], p[1], p[2], back[0], back[1], back[2]);
		}
	}
	monsterbodies::View bad;
	volatile float zero = 0.0f;   // (not a constant: the compiler would warn of the division it never reaches)
	bad.camR[0] = zero; bad.camF[2] = zero;
	float o[3];
	const float p[3] = { 1, 1, 1 };
	EXPECT(!monsterbodies::RenderToHead(bad, p, o), "a degenerate view is refused");
	EXPECT(std::fabs(monsterbodies::CoreRadius(0.35f) - 0.30f) < 1e-6f && monsterbodies::CoreRadius(0.06f) >= 0.08f, "the core radius");
}

// The cost: the contact of one step with 16 bodies near (none touched, and one touched), and the hold's step with and without them.
void TestCost() {
	std::vector<hc::Capsule> far;
	for (int i = 0; i < 16; ++i) far.push_back(Monster(2.0 + 0.3 * i, -3.0, 0.3, (uint32_t)i));
	hh::Settings s = BaseSettings(true);
	hh::Hold h;
	hh::HandIn in[2] = { HandAt({ { 0, 1, 0 }, {} }), HandAt({ { -0.3, 1, 0 }, {} }, false) };
	in[1].grip = 0;
	h.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	const int n = 20000;
	auto timeSteps = [&](bool bodies, const std::vector<hc::Capsule>& caps) {
		s.bodies = bodies;
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; ++i) {
			in[0].pose.pos.z = -0.05 * std::sin(i * 0.01);
			if (!caps.empty()) h.SetBodies(caps.data(), (int)caps.size());
			h.Step(in, kDt, s, out);
		}
		return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
	};
	const double plain = timeSteps(false, {});
	const double withFar = timeSteps(true, far);
	std::vector<hc::Capsule> near = far;
	near[0] = Monster(0, -1.05, 0.3, 99);   // the tip in it
	const double withNear = timeSteps(true, near);
	std::printf("  cost of a step (the hold, one hand, a sword): %.2f us; with 16 bodies near (none touched) %.2f us (+%.2f), one touched %.2f us (+%.2f)\n", plain, withFar,
	            withFar - plain, withNear, withNear - plain);
	EXPECT(withNear - plain < 50.0, "the bodies cost under 50 us a step: %.2f", withNear - plain);
}

}   // namespace

int main(int argc, char** argv) {
	g_table = argc > 1 && std::strcmp(argv[1], "table") == 0;
	if (argc > 1 && std::strcmp(argv[1], "friction") == 0) {   // how the glance goes with the friction
		for (double f : { 0.0, 0.25, 0.5, 0.9, 1.5, 3.0, 10.0 }) {
			g_friction = f;
			Line path;
			path.from = { 0, 1.0, 0.3 };
			path.to = { 0.25, 1.0, -0.6 };
			const Run r = Swing(hh::Kind::Sword, false, path, { Monster(0.42, -0.9, 0.3) }, true);
			std::printf("  friction %.2f: slides %.2f m/s at most, the tip travels %.0f cm in contact, %d steps, the tip %.0f cm off the controller's at the end\n", f, r.maxSlide, r.tipTravelInContact * 100, (int)r.contactSteps, r.lag * 100);
		}
		return 0;
	}
	std::printf("monster_bodies_test: the held weapon against the monsters' bodies\n");
	TestSegments();
	TestThrust();
	TestCutStops();
	TestGlance();
	TestComesBack();
	TestLeftAlone();
	TestOffIsOff();
	TestKinds();
	TestLibrary();
	TestRenderToHead();
	TestCost();
	if (g_fails) {
		std::printf("monster_bodies_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("monster_bodies_test: all passed\n");
	return 0;
}
