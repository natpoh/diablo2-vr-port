// [weight]: with TWO hands on a shaft (and in every other state) the weight model's shaft line - the red line, the line both grasp points lie on - is the DRAWN weapon
// bone's axis, the very line hit by touch's capsule is built from. No game, no headset: vr/weight.cpp (the real model, the real clock) fed by a stand-in of the skeleton hook's
// weapon placement (vr/skeletons.cpp, the [weight] block: the weapon bone carried by the hand weight::Apply says in the relation that hand took it, the relation of the other
// hand taken each pose, the drawn shaft measured against the body's frame and told back with weight::SetDrawnShaft).
//
//   weight_two_hands_test        (a few seconds: the steps are paced by the clock, as in the game)

#include "../../vr/grip_fit.h"
#include "../../vr/weapon_shaft.h"
#include "../../vr/weight.h"
#include "hold.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

using heft::Q;
using heft::V3;

int g_fails = 0;
std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

// ---- the skeleton hook's matrices: row vectors (v' = v * M), rows 0..2 the axes, row 3 the position, doubles ----
struct Mat {
	double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
Mat Mul(const Mat& a, const Mat& b) {   // a then b
	Mat r;
	for (int i = 0; i < 16; ++i) r.m[i] = 0;
	for (int row = 0; row < 4; ++row)
		for (int k = 0; k < 4; ++k)
			for (int col = 0; col < 4; ++col) r.m[row * 4 + col] += a.m[row * 4 + k] * b.m[k * 4 + col];
	return r;
}
Mat Inv(const Mat& a) {   // a rigid (rotation + translation) matrix's inverse
	Mat r;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = a.m[j * 4 + i];
	for (int j = 0; j < 3; ++j) r.m[12 + j] = -(a.m[12] * r.m[j] + a.m[13] * r.m[4 + j] + a.m[14] * r.m[8 + j]);
	return r;
}
Mat FromPose(const V3& p, const Q& q) {
	Mat r;
	const V3 ax[3] = {heft::Rotate(q, {1, 0, 0}), heft::Rotate(q, {0, 1, 0}), heft::Rotate(q, {0, 0, 1})};
	for (int i = 0; i < 3; ++i) { r.m[i * 4] = ax[i].x; r.m[i * 4 + 1] = ax[i].y; r.m[i * 4 + 2] = ax[i].z; }
	r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z;
	return r;
}
Mat FromFloats(const float p[3], const float q[4]) {
	const double l = std::sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);
	return FromPose({p[0], p[1], p[2]}, l > 0.5 ? Q{q[0] / l, q[1] / l, q[2] / l, q[3] / l} : Q{});
}
V3 Pos(const Mat& a) { return {a.m[12], a.m[13], a.m[14]}; }
V3 Row(const Mat& a, int i) { return {a.m[i * 4], a.m[i * 4 + 1], a.m[i * 4 + 2]}; }

V3 Unit(V3 v) { const double l = heft::Len(v); return l > 1e-12 ? heft::Mul(v, 1.0 / l) : V3{0, 1, 0}; }
double DistToLine(const V3& p, const V3& a, const V3& dir) {   // dir unit
	const V3 d = heft::Sub(p, a);
	return heft::Len(heft::Sub(d, heft::Mul(dir, heft::Dot(d, dir))));
}

// ---- the stand-in of the skeleton hook's [weight] block ----
struct Skeleton {
	// where the hand BONE hangs on the controller (the wrist's turn and the palm's offset: constants of the rig), and where the game's weapon bone hangs on the slot's hand bone
	Mat boneOnCtrl[2], home;
	double tipM = 0.66, buttM = 0.66;   // the wood's ends along the bone's Y from its origin (a short staff: the bone is the middle of the grip)
	// the hook's memory (WeightRel)
	Mat rel[2], body;
	bool ok[2] = {false, false}, bodyOk = false;
	uint32_t grabs[2] = {};
	int afterDrop = 1;
	// the pose just made
	Mat W, bone[2], bodyM;
	bool free = false;
	double reach = 0.0;   // > 0: the arm cannot reach farther than this from `shoulder[side]` (a hand bone beyond it is pulled to it, as the IK does)
	V3 shoulder[2] = {{0.18, 0.0, 0.0}, {-0.18, 0.0, 0.0}};

	Skeleton() {
		const Q wristR = heft::QFromAxisAngle(Unit({0.3, 1.0, -0.2}), 0.35), wristL = heft::QFromAxisAngle(Unit({-0.2, 1.0, 0.4}), -0.3);
		boneOnCtrl[0] = FromPose({0.012, -0.02, 0.035}, wristR);
		boneOnCtrl[1] = FromPose({-0.012, -0.02, 0.035}, wristL);
		// the weapon bone in the slot's hand bone, a grip like the game's: the shaft (the bone's Y) along the hand's knuckle row (its x) leaning some 18 degrees off it, the bone's
		// origin a couple of cm out of the palm and a little to the side; the hand bone is also turned from the controller's frame by the wrist offsets above
		home = FromPose({0.004, -0.02, 0.055}, heft::QMul(heft::QFromAxisAngle({0, 0, 1}, -1.5707963 + 0.31), heft::QFromAxisAngle({1, 0, 0}, 0.08)));
	}

	// ---- [weight] grip_fit as the hook's pass has it (skeletons.cpp): per HAND the user's corrections, the plan by how many times that hand took the weapon, the base hand
	//      first (its relation to the shaft is measured and cached), then the hands that took it ----
	bool gripFit = true, likeBase = true;
	gripfit::Tuning user[2];          // [0] right, [1] left: right_grip_* / left_grip_* (cm, degrees)
	gripfit::Hand geom[2];            // each hand as drawn after the pass (fully on the weapon only)
	gripfit::Hand pre[2];             // ... and as the controller drew it, before the pass
	bool fitted[2] = {false, false}, acted[2] = {false, false};
	gripfit::Tuning baseRel;          // cm, degrees
	bool baseKnown = false;
	static gripfit::Hand Geom(int side, const Mat& b) {   // wrist, palm's middle, the knuckles' lines of a hand bone (the rig's: pointing along its z, knuckle row along its x)
		gripfit::Hand g;
		const V3 w = Pos(b), pt = Row(b, 2), kn = Row(b, 0);
		const V3 palm = heft::Add(w, heft::Mul(pt, 0.045));
		const V3 out = heft::Mul(heft::Cross(pt, kn), side == 0 ? -1.0 : 1.0);
		g.wrist = {(float)w.x, (float)w.y, (float)w.z};
		g.palm = {(float)palm.x, (float)palm.y, (float)palm.z};
		g.point = {(float)pt.x, (float)pt.y, (float)pt.z};
		g.knuckles = {(float)kn.x, (float)kn.y, (float)kn.z};
		g.out = {(float)out.x, (float)out.y, (float)out.z};
		g.side = side;
		return g;
	}
	void FitPass(const weight::Out& out) {
		const V3 at = Pos(W), dir = Unit(Row(W, 1));
		const gripfit::Shaft gs{{(float)at.x, (float)at.y, (float)at.z}, {(float)dir.x, (float)dir.y, (float)dir.z}};
		fitted[0] = fitted[1] = acted[0] = acted[1] = false;
		if (!gripFit || out.free) return;
		for (int pass = 0; pass < 2; ++pass)
			for (int s = 0; s < 2; ++s) {
				if (!out.holds[s] || out.on01[s] < 1.0f) continue;
				const bool isBase = out.grabs[s] == 0;
				if ((pass == 0) != isBase) continue;
				gripfit::Hand g = Geom(s, bone[s]);
				pre[s] = g;
				const gripfit::Plan plan = gripfit::PlanFor(out.grabs[s], user[s], likeBase, baseKnown ? &baseRel : nullptr);
				if (plan.act) {
					const gripfit::Out fo = gripfit::Solve(g, gs, plan.tune, plan.mode, 0.01f);   // (metres: a cm is 0.01)
					if (!fo.ok) continue;
					g.point = gripfit::Apply(g.point, fo.r);
					g.knuckles = gripfit::Apply(g.knuckles, fo.r);
					g.out = gripfit::Apply(g.out, fo.r);
					g.wrist = gripfit::Add(fo.anchor, gripfit::Apply(gripfit::Sub(g.wrist, g.palm), fo.r));
					g.palm = fo.anchor;
					acted[s] = true;
				}
				geom[s] = g;
				fitted[s] = true;
				if (isBase) {
					bool measured = false;
					const gripfit::Tuning measure = gripfit::InCm(gripfit::Relation(g, gs, &measured), 0.01f);
					if (measured) { baseRel = measure; baseKnown = true; }
				}
			}
	}
	gripfit::Shaft DrawnShaft() const {
		const V3 at = Pos(W), dir = Unit(Row(W, 1));
		return {{(float)at.x, (float)at.y, (float)at.z}, {(float)dir.x, (float)dir.y, (float)dir.z}};
	}
	// The hook, once per pose: `drawn` the hands as weight::Apply moved them, `out` its answer.
	void Pose(const skel::Input& drawn, const weight::Out& out) {
		for (int s = 0; s < 2; ++s) {
			bone[s] = Mul(boneOnCtrl[s], FromFloats(drawn.hand[s], drawn.rot[s]));
			if (reach > 0.0) {   // the hand's wrist cannot go beyond the arm's length: pulled onto the sphere about the shoulder
				const V3 d = heft::Sub(Pos(bone[s]), shoulder[s]);
				const double l = heft::Len(d);
				if (l > reach) {
					const V3 p = heft::Add(shoulder[s], heft::Mul(d, reach / l));
					bone[s].m[12] = p.x; bone[s].m[13] = p.y; bone[s].m[14] = p.z;
				}
			}
		}
		const int slot = out.slot, m = out.main & 1, o = m ^ 1;
		bodyM = FromFloats(out.body, out.bodyRot);
		const Mat homeNow = home;   // the game's weapon relative to the slot's wrist (constant here)
		if (out.asPlaced) { rel[slot] = homeNow; grabs[slot] = out.grabs[slot]; ok[slot] = true; ok[slot ^ 1] = false; }
		const bool have = ok[m] && grabs[m] == out.grabs[m];
		const bool bodyDrawn = !out.free && !have && m != slot && afterDrop >= 1 && bodyOk;
		free = out.free;
		if (free || bodyDrawn) W = bodyOk ? Mul(body, bodyM) : Mul(homeNow, bone[slot]);
		else W = Mul(have ? rel[m] : homeNow, bone[have ? m : slot]);
		if (!free) { body = Mul(W, Inv(bodyM)); bodyOk = true; }
		if (bodyDrawn && out.holds[m] && out.on01[m] >= 1.0f) { rel[m] = Mul(W, Inv(bone[m])); grabs[m] = out.grabs[m]; ok[m] = true; }
		if (!free && out.holds[o] && out.on01[o] >= 1.0f) { rel[o] = Mul(W, Inv(bone[o])); grabs[o] = out.grabs[o]; ok[o] = true; }
		FitPass(out);   // (after the weapon: the sliders never move it; its relations were taken from the hands as the controllers have them)
		// the drawn shaft against the body's frame, told back (skeletons.cpp's end of the pose)
		if (!free) {
			float bm[16], wm[16];
			for (int i = 0; i < 16; ++i) { bm[i] = (float)bodyM.m[i]; wm[i] = (float)W.m[i]; }
			const weaponshaft::Line ln = weaponshaft::Relative(wm, bm, 1.0, 2, tipM, buttM);
			if (ln.ok) {
				const float a[3] = {(float)ln.at[0], (float)ln.at[1], (float)ln.at[2]}, d[3] = {(float)ln.dir[0], (float)ln.dir[1], (float)ln.dir[2]};
				weight::SetDrawnShaft(a, d, (float)ln.butt, (float)ln.tip, (float)ln.boneAlong);
			}
		}
	}
	// The drawn weapon's axis and its two ends in the head's frame (the line the capsule is built on: the bone's origin and Y, with the blade's ends).
	V3 DrawnDir() const { return Unit(Row(W, 1)); }
	V3 DrawnButt() const { return heft::Sub(Pos(W), heft::Mul(DrawnDir(), buttM)); }
	V3 DrawnTip() const { return heft::Add(Pos(W), heft::Mul(DrawnDir(), tipM)); }
};

struct Rig {
	skel::Input in, drawn;
	weight::Out out;
	uint32_t sample = 0;
	Skeleton sk;

	Rig() {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		Hand(0, {0.2, -0.40, -0.30});
		Hand(1, {-0.2, -0.40, -0.30});
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
		w.stowAfterS = 0.6f;
		w.backLong = true;
		w.slide = true;
		w.gravityFeel = true;
		w.gravity = 1.0f;
		w.damping = 1.0f;
	}
	void Hand(int side, V3 p, Q q = Q{}) {
		in.hand[side][0] = (float)p.x; in.hand[side][1] = (float)p.y; in.hand[side][2] = (float)p.z;
		in.rot[side][0] = (float)q.x; in.rot[side][1] = (float)q.y; in.rot[side][2] = (float)q.z; in.rot[side][3] = (float)q.w;
	}
	V3 HandPos(int side) const { return {in.hand[side][0], in.hand[side][1], in.hand[side][2]}; }
	void Grip(int side, float v) { in.weight.rawGrip[side] = v; }
	void Step(int ms = 8) {
		Sleep(ms);
		in.sample = ++sample;
		skel::Input c = in;
		weight::Apply(&c, &out);
		drawn = c;
		sk.Pose(drawn, out);
	}
	void Steps(int n) { while (n-- > 0) Step(); }
	template <class F> bool Until(F done, int maxSteps = 700) {
		for (int i = 0; i < maxSteps; ++i) {
			Step();
			if (done()) return true;
		}
		return false;
	}
	void Off() {
		in.weight.on = false;
		Step();
		in.weight.on = true;
		Grip(0, 0.0f);
		Grip(1, 0.0f);
		sk = Skeleton();
	}
	// The model's shaft against the drawn one: the largest distance between the two lines' ends (the red line's, from weight::ShaftEnds, against the bone's own axis with the blade's
	// ends) and how far a hand's controller (the grasp point: the fist offset is 0) is from the drawn axis.
	// pairDeg / pairSep: the line through the two grasp points (both hands closed their fists on the shaft) against the drawn axis, and how far apart the hands are (-1: not both)
	struct Gap { double ends, grasp[2], angleDeg; bool ok; double pairDeg = -1.0, pairSep = 0.0; };
	Gap Measure() {
		Gap g{0, {0, 0}, 0, false};
		float b[3], t[3];
		if (!weight::ShaftEnds(b, t)) return g;
		g.ok = true;
		const V3 rb{b[0], b[1], b[2]}, rt{t[0], t[1], t[2]};
		g.ends = std::max(heft::Len(heft::Sub(rb, sk.DrawnButt())), heft::Len(heft::Sub(rt, sk.DrawnTip())));
		g.angleDeg = std::acos(std::clamp(heft::Dot(Unit(heft::Sub(rt, rb)), sk.DrawnDir()), -1.0, 1.0)) * 57.29578;
		// (the hand the game holds it in, as placed, grips it where the GAME's grip has it - the axis passes a few cm from its controller, which is the base grip, not a
		// fault; a hand that took it closed its fist on the shaft: its controller is on the line)
		for (int s = 0; s < 2; ++s)
			g.grasp[s] = out.holds[s] && out.grabs[s] > 0 ? DistToLine({drawn.hand[s][0], drawn.hand[s][1], drawn.hand[s][2]}, sk.DrawnButt(), sk.DrawnDir()) : 0.0;
		if (out.holds[0] && out.holds[1] && out.grabs[0] > 0 && out.grabs[1] > 0) {
			const V3 pr{drawn.hand[0][0], drawn.hand[0][1], drawn.hand[0][2]}, pl{drawn.hand[1][0], drawn.hand[1][1], drawn.hand[1][2]};
			g.pairSep = heft::Len(heft::Sub(pl, pr));
			if (g.pairSep > 1e-3) g.pairDeg = std::acos(std::clamp(std::fabs(heft::Dot(Unit(heft::Sub(pl, pr)), sk.DrawnDir())), 0.0, 1.0)) * 57.29578;
		}
		return g;
	}
};

const double kMm = 0.001;

void Report(const char* what, const Rig::Gap& g) {
	std::printf("  %-52s ends %.2f mm, %.3f deg, grasp R %.2f L %.2f mm\n", what, g.ends / kMm, g.angleDeg, g.grasp[0] / kMm, g.grasp[1] / kMm);
}

// A quiet, settled gap: the controllers stand still for `steps` steps first.
Rig::Gap Settled(Rig& r, int steps = 140) {
	r.Steps(steps);
	return r.Measure();
}

}  // namespace

int main() {
	weight::SetLog(&LogLine);
	std::printf("weight_two_hands_test (the red line against the drawn axis)\n");
	std::mt19937 rng(20261014);
	std::uniform_real_distribution<double> u01(0.0, 1.0);
	auto rnd = [&](double a, double b) { return a + (b - a) * u01(rng); };

	// ---- one hand, as placed ----
	{
		Rig r;
		r.Off();
		r.Steps(4);
		const Rig::Gap g = Settled(r, 30);
		Report("one hand, as placed", g);
		EXPECT(g.ok && g.ends < kMm && g.angleDeg < 0.05);
	}

	// ---- the second hand takes the staff, anywhere along it, at any turn ----
	for (int trial = 0; trial < 6; ++trial) {
		Rig r;
		r.Off();
		r.Steps(6);
		const float shaftAt[3] = {r.drawn.hand[0][0], r.drawn.hand[0][1], r.drawn.hand[0][2]};
		(void)shaftAt;
		float butt[3], tip[3];
		EXPECT(weight::ShaftEnds(butt, tip));
		const double along = rnd(0.15, 0.95);   // where along the wood the left fist goes
		const V3 b{butt[0], butt[1], butt[2]}, t{tip[0], tip[1], tip[2]};
		const V3 onWood = heft::Add(b, heft::Mul(heft::Sub(t, b), along));
		const V3 off{rnd(-0.03, 0.03), rnd(-0.03, 0.03), rnd(-0.03, 0.03)};
		r.Hand(1, heft::Add(onWood, off), heft::QFromAxisAngle(Unit({rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)}), rnd(0.0, 1.2)));
		r.Steps(4);
		r.Grip(1, 1.0f);
		r.Steps(20);
		EXPECT(r.out.holds[1]);
		const Rig::Gap g = Settled(r);
		char what[96];
		std::snprintf(what, sizeof what, "the left hand takes it at %.0f %% along the wood (trial %d)", along * 100.0, trial);
		Report(what, g);
		EXPECT(g.ok && g.ends < kMm && g.angleDeg < 0.05 && g.grasp[1] < kMm);
	}
	// ---- passing it hand to hand, and the game's hand taking it again somewhere else along the wood ----
	for (int trial = 0; trial < 4; ++trial) {
		Rig r;
		r.Off();
		r.Steps(6);
		auto wood = [&](double along) {
			float butt[3], tip[3];
			weight::ShaftEnds(butt, tip);
			const V3 b{butt[0], butt[1], butt[2]}, t{tip[0], tip[1], tip[2]};
			return heft::Add(b, heft::Mul(heft::Sub(t, b), along));
		};
		const auto turn = [&]() { return heft::QFromAxisAngle(Unit({rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)}), rnd(0.0, 1.0)); };
		r.Hand(1, wood(rnd(0.5, 0.9)), turn());
		r.Steps(4);
		r.Grip(1, 1.0f);
		r.Steps(20);
		EXPECT(r.out.holds[1]);
		// the right hand lets go: it is in the left hand now
		r.Grip(0, 1.0f); r.Steps(3); r.Grip(0, 0.0f);   // (a press and a release: the as-placed fist is let go for good)
		r.Steps(30);
		char what[96];
		std::snprintf(what, sizeof what, "passed to the left hand (trial %d): main %d", trial, r.out.main);
		const Rig::Gap pass = Settled(r);
		Report(what, pass);
		EXPECT(pass.ok && pass.ends < kMm && pass.angleDeg < 0.05 && pass.grasp[1] < kMm);
		// the right hand takes it again, somewhere else along it
		r.Hand(0, wood(rnd(0.05, 0.35)), turn());
		r.Steps(4);
		r.Grip(0, 1.0f);
		r.Steps(30);
		const Rig::Gap back = Settled(r);
		std::snprintf(what, sizeof what, "the right hand took it again (trial %d): both %d%d main %d", trial, (int)r.out.holds[0], (int)r.out.holds[1], r.out.main);
		Report(what, back);
		EXPECT(back.ok && back.ends < kMm && back.angleDeg < 0.05 && back.grasp[0] < kMm && back.grasp[1] < kMm);
		// both fists closed on it: the line through the two grasp points is the drawn axis (hands this far apart, a millimetre of position is under 0.2 degrees)
		std::printf("      the line through the two grasp points: %.3f deg off the drawn axis, hands %.2f m apart\n", back.pairDeg, back.pairSep);
		EXPECT(back.pairDeg >= 0.0 && back.pairSep > 0.1 && back.pairDeg < 0.3);
		// the left lets go: it is in the right hand again
		r.Grip(1, 0.0f);
		r.Steps(30);
		const Rig::Gap one = Settled(r);
		std::snprintf(what, sizeof what, "the left let go (trial %d): main %d", trial, r.out.main);
		Report(what, one);
		EXPECT(one.ok && one.ends < kMm && one.angleDeg < 0.05 && one.grasp[0] < kMm);
	}

	// ---- dropped, lying, hung on the back, called back into the game's hand, then taken by the second hand too ----
	for (int onBack = 0; onBack < 2; ++onBack) {
		Rig r;
		r.in.weight.stowAfterS = 0.05f;
		r.Off();
		r.Steps(4);
		r.Grip(0, 1.0f); r.Steps(3); r.Grip(0, 0.0f);   // the as-placed fist squeezed and let go: both hands are off it, it falls
		r.Steps(3);
		EXPECT(r.out.free);
		if (onBack) {
			EXPECT(r.Until([&] { return r.out.stowed; }, 400));
			r.Steps(10);
		} else {
			r.in.weight.afterDrop = 0;   // lies on the floor
			r.Steps(220);
		}
		r.Grip(0, 1.0f);   // the game's hand squeezes its grip, away from it: the weapon flies back into that fist
		EXPECT(r.Until([&] { return !r.out.free && r.out.holds[0]; }, 400));
		r.Steps(30);
		Rig::Gap g = Settled(r);
		Report(onBack ? "called back from the back into the game's hand" : "called back from the floor into the game's hand", g);
		EXPECT(g.ok && g.ends < kMm && g.angleDeg < 0.05);
		float butt[3], tip[3];
		EXPECT(weight::ShaftEnds(butt, tip));
		const V3 b{butt[0], butt[1], butt[2]}, t{tip[0], tip[1], tip[2]};
		r.Hand(1, heft::Add(b, heft::Mul(heft::Sub(t, b), 0.8)), heft::QFromAxisAngle(Unit({0.3, -0.5, 0.2}), 0.6));
		r.Steps(4);
		r.Grip(1, 1.0f);
		r.Steps(30);
		g = Settled(r);
		Report("... and the left hand takes it too", g);
		EXPECT(r.out.holds[0] && r.out.holds[1] && g.ok && g.ends < kMm && g.angleDeg < 0.05 && g.grasp[1] < kMm);
	}

	// ---- [weight] grip_fit by HAND, in every state the staff can be in: the base hand (the game's), a hand that took it, two hands, a hand that took it over, the base hand taking it
	//      up again - the corrections of each hand act on THAT hand and no other, and every hand that took it sits on the shaft like the base grip ----
	{
		const auto relOf = [&](Rig& r, int side) {
			bool ok = false;
			const gripfit::Tuning t = gripfit::InCm(gripfit::Relation(r.sk.geom[side], r.sk.DrawnShaft(), &ok), 0.01f);
			EXPECT(ok);
			return t;
		};
		// two relations the same (offsets in cm, leans in degrees)
		const auto same = [&](const char* what, const gripfit::Tuning& got, const gripfit::Tuning& want, double cmTol = 0.06, double degTol = 0.4) {
			bool ok = true;
			for (int i = 1; i < 3; ++i) ok = ok && std::fabs(got.off[i] - want.off[i]) < cmTol;
			for (int i = 0; i < 3; ++i) ok = ok && std::fabs(got.rot[i] - want.rot[i]) < degTol;
			std::printf("  %-66s %s (across %.2f up %.2f cm, lean %.1f %.1f %.1f deg; wanted %.2f %.2f, %.1f %.1f %.1f)\n", what, ok ? "same" : "DIFFERENT", got.off[1], got.off[2], got.rot[0], got.rot[1], got.rot[2],
			            want.off[1], want.off[2], want.rot[0], want.rot[1], want.rot[2]);
			EXPECT(ok);
		};
		const auto wood = [&](double along) {
			float butt[3], tip[3];
			weight::ShaftEnds(butt, tip);
			const V3 b{butt[0], butt[1], butt[2]}, t{tip[0], tip[1], tip[2]};
			return heft::Add(b, heft::Mul(heft::Sub(t, b), along));
		};
		const auto turn = [&]() { return heft::QFromAxisAngle(Unit({rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)}), rnd(0.0, 1.2)); };
		// The corrections of ONE hand, offsets then a roll: that hand's palm moves by exactly them in the shaft's own frame (along the shaft, across, up - of the hand as it is
		// laid), and its palm's way out turns by the roll about the shaft; the other hand does not move at all.
		const auto corrections = [&](Rig& r, int side, const char* what) {
			r.Steps(25);
			const gripfit::Hand before[2] = {r.sk.geom[0], r.sk.geom[1]};
			const bool took = r.out.grabs[side] > 0;
			gripfit::Tuning t;
			t.off[0] = 1.2f; t.off[1] = -0.9f; t.off[2] = 1.6f;
			r.sk.user[side] = t;
			r.Steps(25);
			const gripfit::Hand& now = r.sk.geom[side];
			const gripfit::Frame fr = gripfit::FrameOf(r.sk.pre[side], r.sk.DrawnShaft(), took);   // (the hand as the controller drew it, before the pass)
			const V3 want = heft::Add(heft::Add(heft::Mul({fr.d.x, fr.d.y, fr.d.z}, 0.012), heft::Mul({fr.across.x, fr.across.y, fr.across.z}, -0.009)), heft::Mul({fr.up.x, fr.up.y, fr.up.z}, 0.016));
			const V3 moved{now.palm.x - before[side].palm.x, now.palm.y - before[side].palm.y, now.palm.z - before[side].palm.z};
			const double err = heft::Len(heft::Sub(moved, want));
			const int other = side ^ 1;
			const double otherMoved = heft::Len({r.sk.geom[other].palm.x - before[other].palm.x, r.sk.geom[other].palm.y - before[other].palm.y, r.sk.geom[other].palm.z - before[other].palm.z});
			std::printf("  %-66s palm moved %.2f cm for %.2f cm asked (error %.2f mm), the other hand %.2f mm\n", what, heft::Len(moved) * 100.0, heft::Len(want) * 100.0, err / kMm, otherMoved / kMm);
			EXPECT(err < 0.4 * kMm);
			if (!took && r.out.holds[other] && r.out.grabs[other] > 0) {
				// the base hand's corrections are part of how it is drawn, and a hand that took the weapon copies the base as drawn (grip_fit_like_base): it follows - and still sits on the shaft like it
				bool ok2 = false;
				const gripfit::Tuning rel2 = gripfit::InCm(gripfit::Relation(r.sk.geom[other], r.sk.DrawnShaft(), &ok2), 0.01f);
				EXPECT(ok2 && std::fabs(rel2.off[1] - r.sk.baseRel.off[1]) < 0.06 && std::fabs(rel2.off[2] - r.sk.baseRel.off[2]) < 0.06);
			} else {
				EXPECT(otherMoved < 0.2 * kMm);   // the other hand is not touched
			}
			// the roll alone: the palm's way out turns by it about the shaft
			gripfit::Tuning roll;
			roll.rot[0] = 20.0f;
			r.sk.user[side] = roll;
			r.Steps(25);
			const V3 d = Unit({fr.d.x, fr.d.y, fr.d.z});
			const auto flat = [&](const gripfit::V3& v) { const V3 w{v.x, v.y, v.z}; return Unit(heft::Sub(w, heft::Mul(d, heft::Dot(w, d)))); };
			const double turned = std::acos(std::clamp(heft::Dot(flat(r.sk.geom[side].out), flat(before[side].out)), -1.0, 1.0)) * 57.29578;
			std::printf("  %-66s the palm's way out turned %.1f deg for a roll of 20\n", "", turned);
			EXPECT(std::fabs(turned - 20.0) < 2.5);   // (the roll is made first and the lean after it: with a lean on the hand it is not the pure 20 about the shaft)
			r.sk.user[side] = gripfit::Tuning{};
			r.Steps(25);
		};
		Rig r;
		r.Off();
		r.Steps(40);
		EXPECT(r.out.holds[0] && !r.out.holds[1] && r.sk.baseKnown && r.sk.fitted[0]);
		const gripfit::Tuning base = r.sk.baseRel;
		std::printf("  the base grip (the game's hand, as placed) sits on the shaft: palm %.2f cm across, %.2f cm up from the axis, lean %.1f %.1f %.1f deg\n", base.off[1], base.off[2], base.rot[0], base.rot[1], base.rot[2]);
		EXPECT(!r.sk.acted[0]);   // nothing asked of the game's hand: its pose is the game's
		corrections(r, 0, "one hand: the right (base) hand's corrections act on it");
		r.Hand(1, wood(0.8), turn());
		r.Steps(4);
		r.Grip(1, 1.0f);
		r.Steps(40);
		EXPECT(r.out.holds[0] && r.out.holds[1] && r.sk.fitted[1] && r.sk.acted[1]);
		same("two hands: the left hand that took it sits like the base grip", relOf(r, 1), base);
		same("... and the base hand still does", relOf(r, 0), base);
		corrections(r, 1, "two hands: the left (taking) hand's corrections act on it");
		corrections(r, 0, "two hands: the right (base) hand's corrections act on it");
		same("(corrections off again) the taking hand is the base grip's again", relOf(r, 1), base);
		// like the base off: the palm exactly on the axis, the knuckles along it
		r.sk.likeBase = false;
		r.Steps(30);
		same("like-the-base off: the taking hand's palm is on the axis", relOf(r, 1), gripfit::Tuning{});
		r.sk.likeBase = true;
		r.Steps(30);
		// the passing: the right hand lets go, the left carries it
		r.Grip(0, 1.0f); r.Steps(3); r.Grip(0, 0.0f);
		r.Steps(40);
		EXPECT(!r.out.holds[0] && r.out.holds[1] && r.out.main == 1);
		same("passed to the left hand (it carries it): still the base grip's relation", relOf(r, 1), base);
		corrections(r, 1, "the left hand carries it: its corrections act on it");
		// the game's hand takes it up again, somewhere else, at another turn: now a hand that TOOK it - it sits like the base grip too
		r.Hand(0, wood(0.15), turn());
		r.Steps(4);
		r.Grip(0, 1.0f);
		r.Steps(50);
		EXPECT(r.out.holds[0] && r.out.holds[1] && r.out.grabs[0] > 0);
		same("the game's hand took it up again: it sits like the base grip too", relOf(r, 0), base);
		same("... the left one unchanged", relOf(r, 1), base);
		corrections(r, 0, "the game's hand took it up again: its corrections act on it");
		// a base hand with corrections of its own: they move that hand, and a hand that takes the weapon copies the base AS IT IS DRAWN (corrections in)
		Rig b;
		b.Off();
		b.sk.user[0].off[1] = 1.0f; b.sk.user[0].off[2] = 0.5f; b.sk.user[0].rot[1] = 4.0f;
		b.Steps(40);
		EXPECT(b.sk.acted[0] && b.sk.baseKnown);
		EXPECT(std::fabs(b.sk.baseRel.off[1] - base.off[1]) > 0.5 || std::fabs(b.sk.baseRel.off[2] - base.off[2]) > 0.3);   // the base relation moved with the base hand's corrections
		b.Hand(1, wood(0.8), turn());
		b.Steps(4);
		b.Grip(1, 1.0f);
		b.Steps(40);
		EXPECT(b.out.holds[1] && b.sk.fitted[1]);
		same("a base hand with corrections: the taking hand copies it as drawn", relOf(b, 1), b.sk.baseRel);
	}
	// ---- arms that cannot reach, and hands in motion ----
	for (const double reach : {0.0, 0.55, 0.45}) {
		Rig r;
		r.Off();
		r.sk.reach = reach;   // (Off() makes a fresh stand-in)
		r.Steps(6);
		float butt[3], tip[3];
		weight::ShaftEnds(butt, tip);
		const V3 b{butt[0], butt[1], butt[2]}, t{tip[0], tip[1], tip[2]};
		r.Hand(1, heft::Add(b, heft::Mul(heft::Sub(t, b), 0.85)));
		r.Steps(4);
		r.Grip(1, 1.0f);
		r.Steps(20);
		Rig::Gap g = Settled(r);
		char what[96];
		std::snprintf(what, sizeof what, "arm reach %.2f m, left on the wood, still", reach);
		Report(what, g);
		EXPECT(g.ok && g.ends < kMm && g.angleDeg < 0.05 && g.grasp[1] < kMm);
		// both hands swing the staff about: the worst gap seen over a second of motion
		double worst = 0, worstAngle = 0;
		int okCount = 0, held = 0;
		for (int i = 0; i < 120; ++i) {
			const double ph = i * 0.07;
			r.Hand(0, {0.2 + 0.08 * std::sin(ph), -0.40 + 0.05 * std::sin(ph * 1.3), -0.30 + 0.06 * std::cos(ph)});
			r.Hand(1, heft::Add(r.HandPos(1), {0.004 * std::cos(ph), 0.003 * std::sin(ph * 0.8), 0.0}));
			r.Step();
			const Rig::Gap m = r.Measure();
			okCount += m.ok ? 1 : 0;
			held += r.out.holds[0] && r.out.holds[1] ? 1 : 0;
			worst = std::max(worst, m.ends);
			worstAngle = std::max(worstAngle, m.angleDeg);
		}
		std::printf("  %-52s worst ends %.2f mm, %.3f deg (%d of 120 measured, %d with both hands on it)\n", "  ... in motion", worst / kMm, worstAngle, okCount, held);
		EXPECT(okCount == 120 && held == 120);
		EXPECT(worst < 0.03);   // an arm that cannot reach drags the drawn weapon off the model by a frame's worth of the hand's motion - centimetres at the most, never the several of a model line that stands still
		g = Settled(r);
		Report("  ... settled again", g);
		EXPECT(g.ok && g.ends < kMm && g.angleDeg < 0.05 && g.grasp[1] < kMm);
	}
	weight::SetLog(nullptr);
	std::printf(g_fails == 0 ? "weight_two_hands_test: all passed\n" : "weight_two_hands_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
