// The heft model (cleanroom/heft) on the bench's weapons and paths, without the
// game and without Unity.
//
//   heft_test                 every weapon on every path: a table, and the checks below
//   heft_test compare <dir>   the Unity bench's runs (<weapon>_<path>.csv, written by
//                             tools/heft_bench_unity): the PhysX reference against ours,
//                             the same numbers for both
//   heft_test hands           only the hands that take the weapon (hold.h): D2R's kinds
//                             and their checks
//
// Checks: nothing blows up; a dagger sits in the fist; the heavier and the longer
// the weapon, the further it lags and sinks; two hands far apart hold it better
// than one; the substep does not change the answer; a lost weapon goes back to
// the fist. Hands (hold.h): a spear held at its end sinks more than held where
// the game holds it; the weapon goes from the right hand to the left; a grip far
// from the shaft or in a busy hand takes nothing; the last hand never drops it.

#include "bench.h"
#include "heft.h"
#include "hold.h"
#include "slide.h"
#include "../../vr/weight_flight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace heft;
using namespace heft::bench;

namespace {

constexpr double kDt = 1.0 / 90.0;   // the reference's physics rate
constexpr double kPi = 3.14159265358979323846;
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

struct Sample {
	double t;
	Pose   ctl, grip;
};

// When the path's motion ends (s): what comes after is settling.
double MotionEnd(const Path& p) {
	const std::string n = p.name;
	if (n.rfind("hold", 0) == 0) return 0;
	if (n == "step") return 0.55;
	if (n == "flick") return 0.58;
	return 0.80;
}

struct Metrics {
	double palmLag = 0;   // m, the most the palm was off its controller
	double tipLag  = 0;   // m, the most the tip was off where a weightless weapon would have it
	double endSag  = 0;   // deg, the weapon's axis off the controller's at the end
	double after   = 0;   // m, the most the tip was off after the motion ended
	double settle  = 0;   // s after the motion ended until the tip stays within 3 cm of where it ends up (-1: never)
	bool   finite  = true;
};

double AngleDeg(V3 a, V3 b) {
	const double c = Dot(a, b) / std::max(1e-12, Len(a) * Len(b));
	return std::acos(std::fmax(-1.0, std::fmin(1.0, c))) * 180.0 / kPi;
}

V3 TipOf(const Pose& p, const Weapon& w) { return Add(p.pos, Rotate(p.rot, w.tip)); }

Metrics Measure(const std::vector<Sample>& run, const Weapon& w, const Path& p) {
	Metrics m;
	const double end = MotionEnd(p);
	// settled = within 3 cm of where it ends up (a held weapon may rest a little low)
	const Sample& fin = run.back();
	const double finalErr = Len(Sub(TipOf(fin.grip, w), TipOf(fin.ctl, w)));
	double lastBad = -1;
	for (const Sample& s : run) {
		const double palm = Len(Sub(s.grip.pos, s.ctl.pos)), tipErr = Len(Sub(TipOf(s.grip, w), TipOf(s.ctl, w)));
		if (!std::isfinite(palm) || !std::isfinite(tipErr)) m.finite = false;
		m.palmLag = std::fmax(m.palmLag, palm);
		m.tipLag  = std::fmax(m.tipLag, tipErr);
		if (s.t >= end) {
			m.after = std::fmax(m.after, tipErr);
			if (tipErr > finalErr + 0.03) lastBad = s.t;
		}
	}
	m.endSag = AngleDeg(Rotate(fin.grip.rot, { 0, 1, 0 }), Rotate(fin.ctl.rot, { 0, 1, 0 }));
	m.settle = lastBad < 0 ? 0 : lastBad >= fin.t - 1e-6 ? -1 : lastBad - end;
	return m;
}

// Velocity of a pose sampled at kDt, as the reference does it (difference over one step).
void Velocities(const Pose& prev, const Pose& now, double dt, V3* vel, V3* ang) {
	*vel = Mul(Sub(now.pos, prev.pos), 1.0 / dt);
	*ang = Mul(RotVec(QMul(now.rot, QConj(prev.rot))), 1.0 / dt);
}

std::vector<Sample> Run(int wi, int pi, double substep) {
	const Weapon& w = WeaponAt(wi);
	const Path&   p = PathAt(pi);
	const bool two = p.twoHands;
	const int  n   = two ? 2 : 1;
	const Body body = Held(w, two);
	Grip grips[2];
	for (Grip& g : grips) g.muscle = two ? TwoHandEach() : OneHand();
	grips[1].at = w.second;
	World world;
	world.substep = substep;
	State st;
	Controller last[2], now[2];
	std::vector<Sample> out;
	const int steps = (int)std::lround(p.duration / kDt);
	for (int k = 0; k <= steps; ++k) {
		const double t = k * kDt;
		for (int i = 0; i < n; ++i) {
			now[i].pose = ControllerAt(pi, w, i, t);
			const Pose prev = k ? last[i].pose : now[i].pose;
			Velocities(prev, now[i].pose, kDt, &now[i].vel, &now[i].angVel);
		}
		if (k == 0) {
			for (int i = 0; i < n; ++i) last[i] = now[i];
		}
		Step(st, body, grips, last, now, n, k ? kDt : 0, world);
		for (int i = 0; i < n; ++i) last[i] = now[i];
		out.push_back({ t, now[0].pose, GripPose(st, body) });
	}
	return out;
}

int FindWeapon(const char* name) {
	for (int i = 0; i < WeaponCount(); ++i)
		if (std::strcmp(WeaponAt(i).name, name) == 0) return i;
	return -1;
}
int FindPath(const char* name) {
	for (int i = 0; i < PathCount(); ++i)
		if (std::strcmp(PathAt(i).name, name) == 0) return i;
	return -1;
}
bool Applies(const Weapon& w, const Path& p) { return !p.twoHands || w.twoHanded; }

void PrintHead() {
	std::printf("%-14s %-7s %8s %8s %8s %8s %8s\n", "weapon", "path", "palm cm", "tip cm", "sag deg", "after cm", "settle s");
}
void Print(const char* w, const char* p, const Metrics& m, const char* tag = "") {
	std::printf("%-14s %-7s %8.1f %8.1f %8.1f %8.1f %8.2f %s\n", w, p, m.palmLag * 100, m.tipLag * 100, m.endSag, m.after * 100, m.settle,
	            tag);
}

// The most two runs' tips are apart.
double Apart(const std::vector<Sample>& a, const std::vector<Sample>& b, const Weapon& w) {
	double worst = 0;
	for (size_t k = 0; k < a.size() && k < b.size(); ++k) worst = std::fmax(worst, Len(Sub(TipOf(a[k].grip, w), TipOf(b[k].grip, w))));
	return worst;
}

int Hands(int argc, char** argv);   // below: the hands that take the weapon
int Feel(int argc, char** argv);    // below: gravity - a swing down against a swing up
int Gravity();                      // below: the gravity feel's checks

int Synthetic() {
	PrintHead();
	Metrics table[16][16];
	for (int wi = 0; wi < WeaponCount(); ++wi)
		for (int pi = 0; pi < PathCount(); ++pi) {
			if (!Applies(WeaponAt(wi), PathAt(pi))) continue;
			table[wi][pi] = Measure(Run(wi, pi, 1.0 / 360.0), WeaponAt(wi), PathAt(pi));
			Print(WeaponAt(wi).name, PathAt(pi).name, table[wi][pi]);
			EXPECT(table[wi][pi].finite, "%s %s", WeaponAt(wi).name, PathAt(pi).name);
			EXPECT(table[wi][pi].palmLag < 1.0, "%s %s palm off by %.2f m", WeaponAt(wi).name, PathAt(pi).name, table[wi][pi].palmLag);
		}
	const int dagger = FindWeapon("dagger"), sword = FindWeapon("broadsword"), pole = FindWeapon("polearm"), spear = FindWeapon("spear"),
	          hammer = FindWeapon("sledgehammer");
	const int hold = FindPath("hold"), hold2 = FindPath("hold2"), step = FindPath("step"), swing = FindPath("swing");
	// a dagger sits in the fist; a polearm held out in one hand sinks
	EXPECT(table[dagger][hold].endSag < 2.0, "dagger sinks %.1f deg", table[dagger][hold].endSag);
	EXPECT(table[pole][hold].endSag > table[sword][hold].endSag && table[sword][hold].endSag > table[dagger][hold].endSag,
	       "sag dagger %.1f sword %.1f polearm %.1f", table[dagger][hold].endSag, table[sword][hold].endSag, table[pole][hold].endSag);
	// two hands hold it better - where the hands are far enough apart to make a lever (the
	// two-handed sword's are 22 cm apart, and Hurricane's two-hand profile weakens each wrist)
	for (int w : { spear, pole, hammer })
		EXPECT(table[w][hold2].endSag < table[w][hold].endSag, "%s sinks %.1f with two hands, %.1f with one", WeaponAt(w).name,
		       table[w][hold2].endSag, table[w][hold].endSag);
	// the heavier, the further behind
	EXPECT(table[dagger][step].tipLag < table[sword][step].tipLag && table[sword][step].tipLag < table[pole][step].tipLag,
	       "step tip lag dagger %.3f sword %.3f polearm %.3f", table[dagger][step].tipLag, table[sword][step].tipLag, table[pole][step].tipLag);
	EXPECT(table[dagger][swing].tipLag < table[pole][swing].tipLag, "swing tip lag dagger %.3f polearm %.3f", table[dagger][swing].tipLag,
	       table[pole][swing].tipLag);
	// the substep does not change the answer where the motion is not chaotic: a dagger's cut,
	// a sword and a polearm carried across, a spear and a polearm in two hands. (A long weapon
	// whipped round in one hand is a flail - the reference itself moves by tens of cm with
	// its solver's iterations: printed, not checked.)
	const int swing2 = FindPath("swing2"), chop2 = FindPath("chop2");
	for (const auto& [wi, pi] : { std::pair { dagger, swing }, std::pair { sword, step }, std::pair { pole, step },
	                              std::pair { spear, swing2 }, std::pair { pole, chop2 } }) {
		const auto coarse = Run(wi, pi, 1.0 / 360.0), fine = Run(wi, pi, 1.0 / 1440.0);
		const double d = Apart(coarse, fine, WeaponAt(wi));
		// allowed: 2 cm, or one and a half coarse substeps of the tip's fastest travel
		double fastest = 0;
		for (size_t k = 1; k < fine.size(); ++k)
			fastest = std::fmax(fastest, Len(Sub(TipOf(fine[k].grip, WeaponAt(wi)), TipOf(fine[k - 1].grip, WeaponAt(wi)))) / kDt);
		const double allowed = std::fmax(0.02, 1.5 * fastest / 360.0);
		EXPECT(d < allowed, "%s %s: substep 1/360 against 1/1440 differs by %.3f m at the tip (allowed %.3f)", WeaponAt(wi).name,
		       PathAt(pi).name, d, allowed);
	}
	{
		const auto fine = Run(pole, swing, 1.0 / 1440.0);
		std::printf("polearm swing (a flail): against substep 1/1440 the tip moves %.1f cm at 1/360, %.1f cm at 1/720\n",
		            Apart(Run(pole, swing, 1.0 / 360.0), fine, WeaponAt(pole)) * 100,
		            Apart(Run(pole, swing, 1.0 / 720.0), fine, WeaponAt(pole)) * 100);
	}
	// lost: put back in the fist
	{
		const Weapon& w = WeaponAt(sword);
		const Body b = Held(w, false);
		Grip g;
		g.muscle = OneHand();
		State st;
		World world;
		Controller c;
		c.pose = { { 0, 1.3, 0.4 }, {} };
		Step(st, b, &g, &c, &c, 1, 0, world);
		Controller far = c;
		far.pose.pos = { 3, 1.3, 0.4 };
		const bool snapped = Step(st, b, &g, &c, &far, 1, kDt, world);
		EXPECT(snapped && Len(Sub(HandPose(st, b, g).pos, far.pose.pos)) < 1e-6, "a sword 3 m away was not put back in the hand");
	}
	Gravity();
	return Hands(0, nullptr);
}

// <dir>/<weapon>_<path>.csv: t, controller pos+rot, reference grip pos+rot, our grip pos+rot.
bool ReadRun(const std::string& file, std::vector<Sample>* ref, std::vector<Sample>* ours) {
	std::ifstream f(file);
	if (!f) return false;
	std::string line;
	std::getline(f, line);   // header
	while (std::getline(f, line)) {
		std::stringstream ss(line);
		std::vector<double> v;
		for (std::string c; std::getline(ss, c, ',');) v.push_back(std::atof(c.c_str()));
		if (v.size() < 22) continue;
		const Pose ctl { { v[1], v[2], v[3] }, { v[4], v[5], v[6], v[7] } };
		ref->push_back({ v[0], ctl, { { v[8], v[9], v[10] }, { v[11], v[12], v[13], v[14] } } });
		ours->push_back({ v[0], ctl, { { v[15], v[16], v[17] }, { v[18], v[19], v[20], v[21] } } });
	}
	return !ref->empty();
}

int Compare(const char* dir) {
	std::printf("PhysX reference (ref) against our model (ours), Hurricane's hand on both\n");
	PrintHead();
	int runs = 0;
	double worstTip = 0;
	for (int wi = 0; wi < WeaponCount(); ++wi)
		for (int pi = 0; pi < PathCount(); ++pi) {
			const Weapon& w = WeaponAt(wi);
			const Path&   p = PathAt(pi);
			if (!Applies(w, p)) continue;
			std::vector<Sample> ref, ours;
			if (!ReadRun(std::string(dir) + "/" + w.name + "_" + p.name + ".csv", &ref, &ours)) continue;
			++runs;
			Print(w.name, p.name, Measure(ref, w, p), "ref");
			Print(w.name, p.name, Measure(ours, w, p), "ours");
			const double tip = Apart(ref, ours, w);
			worstTip = std::fmax(worstTip, tip);
			std::printf("%-14s %-7s   tips apart at most %.1f cm\n", "", "", tip * 100);
		}
	if (!runs) {
		std::printf("no runs in %s\n", dir);
		return 2;
	}
	std::printf("%d runs, the tips at most %.1f cm apart\n", runs, worstTip * 100);
	return 0;
}


// ---- The hands that take the weapon (hold.h) ----

namespace hh = heft::hold;

// A controller as a hand holds a shaft: the line through its fist (-Z) along it. Unturned, -Z is
// ahead and level; the shaft runs from the fist along the controller's -Z, as the game's grip has it.
const V3 kAlong { 0, 0, -1 };

hh::HandIn Hand(V3 pos, Q rot, float grip, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose    = { pos, rot };
	h.grip    = grip;
	return h;
}

// How far below the horizontal the shaft points, degrees.
double SagDeg(const hh::Hold& w) {
	const V3 d = Rotate(w.BodyPose().rot, w.ShaftLine().dir);
	return std::asin(std::clamp(-d.y / std::max(1e-9, Len(d)), -1.0, 1.0)) * 180.0 / kPi;
}

// The weapon as the right fist holds it `heldAt` metres along its shaft (0: where the game holds it,
// its butt end is negative); with `second` >= -10, the left fist on it too, that far along.
struct Rig {
	hh::Hold   w;
	hh::HandIn in[2];
	hh::HandOut out[2];
	double     heldAt = 0, second = -99;
	Pose Left(const Pose& right) const { return { Add(right.pos, Rotate(right.rot, Mul(kAlong, second - heldAt))), right.rot }; }
	void Start(hh::Kind k, bool two, const Pose& right, const hh::Settings& s) {
		in[0] = Hand(right.pos, right.rot, 0);
		in[1] = Hand({ -0.3, 1.0, 0.0 }, {}, 0);
		w.Place(0, k, two, { Mul(kAlong, -heldAt), kAlong }, in, s);
		if (second > -10) {   // the left takes it, on the first step
			const Pose l = Left(right);
			in[1]        = Hand(l.pos, l.rot, 1);
			w.Step(in, kDt, s, out);
		}
	}
	void Step(const Pose& right, const hh::Settings& s) {
		in[0].pose = right;
		if (second > -10) in[1].pose = Left(right);
		w.Step(in, kDt, s, out);
	}
};

// Held out level and still, `seconds` long: the sag at the end.
double HoldLevel(hh::Kind k, bool two, const hh::Settings& s, double heldAt = 0, double second = -99) {
	Rig r;
	r.heldAt = heldAt;
	r.second = second;
	const Pose p { { 0.15, 1.3, -0.4 }, {} };
	r.Start(k, two, p, s);
	for (double t = 0; t < 3.0; t += kDt) r.Step(p, s);
	return SagDeg(r.w);
}

// The tip's worst lag behind a weightless weapon on a flat cut: 140 degrees about the shoulder in 0.3 s
// (a hard cut; `deg` in `len` s otherwise).
double SwingLag(hh::Kind k, bool two, const hh::Settings& s, double heldAt = 0, double second = -99, double deg = 140, double len = 0.3) {
	const V3 shoulder { 0.20, 1.40, 0.0 };
	auto at = [&](double t) {
		const double u = std::clamp((t - 0.3) / len, 0.0, 1.0), e = u * u * (3 - 2 * u);
		const Q yaw = QFromAxisAngle({ 0, 1, 0 }, (deg / 2 - deg * e) * kPi / 180);
		return Pose { Add(shoulder, Rotate(yaw, { 0, -0.10, -0.55 })), yaw };
	};
	Rig r;
	r.heldAt = heldAt;
	r.second = second;
	r.Start(k, two, at(0), s);
	double worst = 0;
	const double tip = hh::ShapeOf(k, two).tip - heldAt;
	for (double t = 0; t < 1.5; t += kDt) {
		const Pose p = at(t);
		r.Step(p, s);
		V3 tipNow;
		r.w.Ends(nullptr, &tipNow);
		worst = std::fmax(worst, Len(Sub(tipNow, Add(p.pos, Rotate(p.rot, Mul(kAlong, tip))))));
	}
	return worst;
}

// ---- Gravity: a swing down against a swing up (docs/WEAPON_WEIGHT.md, "Gravity") ----

// How high the shaft points above the horizontal, degrees.
double ElevDeg(const hh::Hold& w) { return -SagDeg(w); }

// The hands of the gravity feel (hold.h UseGravityFeel) with the sliders' gravity and damping, as fractions of the real ones.
hh::Settings Soft(double gravity = 1.0, double damping = 1.0) {
	hh::Settings s;
	hh::UseGravityFeel(s);
	s.gravity = gravity;
	s.damping = damping;
	return s;
}

// The hand swings the weapon in a vertical plane about the shoulder: the controller's pitch goes from `from` to `to` degrees (up positive) in
// `len` s, a smooth start and stop, after a second of holding still (the weapon sags in the hand meanwhile). Pitch is the weapon's shaft against
// the controller's, as the eye sees it: sag counts.
struct Pitch {
	double lag = 0;        // deg: the most the weapon trails the controller, against the direction of the motion
	double over = 0;       // deg: the most it is ahead of the controller, in the direction of the motion
	double rest = 0;       // deg: weapon minus controller before the motion (minus its sag)
	double end = 0;        // deg: the same a second after it
	double settle = 0;     // s after the motion until the weapon stays within 1 degree of where it ends up
	double past = 0;       // deg: the most it swings past where it ends up, in the direction of the motion
};
Pitch PitchSwing(hh::Kind k, bool two, const hh::Settings& s, double from, double to, double len, double second = -99, double heldAt = 0) {
	const V3 shoulder { 0.20, 1.40, 0.0 };
	const double lead = 1.0, tail = 1.2;
	auto ctl = [&](double t) {
		const double u = std::clamp((t - lead) / len, 0.0, 1.0);
		return from + (to - from) * u * u * (3 - 2 * u);
	};
	auto at = [&](double t) {
		const Q pitch = QFromAxisAngle({ 1, 0, 0 }, ctl(t) * kPi / 180);
		return Pose { Add(shoulder, Rotate(pitch, { 0, -0.10, -0.55 })), pitch };
	};
	Rig r;
	r.heldAt = heldAt;
	r.second = second;
	r.Start(k, two, at(0), s);
	std::vector<double> err;   // weapon minus controller, deg, at each step
	const int n = (int)std::lround((lead + len + tail) / kDt);
	for (int i = 0; i < n; ++i) {
		const double t = i * kDt;
		r.Step(at(t), s);
		err.push_back(ElevDeg(r.w) - ctl(t));
	}
	Pitch o;
	const double dir = to >= from ? 1.0 : -1.0;
	o.rest = err[(int)(lead / kDt) - 1];
	o.end  = err.back();
	for (int i = 0; i < n; ++i) {
		const double t = i * kDt;
		if (t >= lead && t <= lead + len) {
			o.lag  = std::fmax(o.lag, -dir * err[i]);
			o.over = std::fmax(o.over, dir * err[i]);
		}
		if (t > lead + len) {
			o.past = std::fmax(o.past, dir * (err[i] - o.end));
			if (std::fabs(err[i] - o.end) > 1.0) o.settle = t - (lead + len);
		}
	}
	return o;
}

// Where a second hand goes on a two-handed hold: a fist apart on a hilt, wide on a shaft.
double SecondAt(hh::Kind k, bool two) {
	switch (k) {
	case hh::Kind::Staff: case hh::Kind::Polearm: case hh::Kind::Spear: return -0.50;
	case hh::Kind::Javelin: return -0.35;
	default: return two ? -0.20 : -0.10;
	}
}

// argv: key=value ... to tune by, or `legacy` (the old stiff wrists): g gravity %, d damping %, s strength %, ts / mt / tz the one hand's wrist
// spring, ceiling and damping ratio, soft the wrist's soft angle (deg), lz / sp / mf the arm's damping ratio, spring and ceiling; the same
// keys with a 2 on the end for each of two hands.
int Feel(int argc, char** argv) {
	hh::Settings s;
	hh::UseGravityFeel(s);
	for (int i = 0; i < argc; ++i) {
		if (std::strcmp(argv[i], "legacy") == 0) {
			s.one = hh::OneHand();
			s.two = hh::EachOfTwo();
			continue;
		}
		const char* eq = std::strchr(argv[i], '=');
		if (!eq) continue;
		const std::string k(argv[i], eq - argv[i]);
		const double v = std::atof(eq + 1);
		if (k == "g") s.gravity = v * 0.01;
		else if (k == "d") s.damping = v * 0.01;
		else if (k == "s") s.strength = v * 0.01;
		else if (k == "ts") s.one.torqueSpring = v;
		else if (k == "mt") s.one.maxTorque = v;
		else if (k == "tz") s.one.torqueDamping = v;
		else if (k == "soft") s.one.torqueSoft = v * kPi / 180;
		else if (k == "lz") s.one.damping = v;
		else if (k == "mf") s.one.maxForce = v;
		else if (k == "sp") s.one.spring = v;
		else if (k == "ts2") s.two.torqueSpring = v;
		else if (k == "mt2") s.two.maxTorque = v;
		else if (k == "tz2") s.two.torqueDamping = v;
		else if (k == "soft2") s.two.torqueSoft = v * kPi / 180;
		else if (k == "lz2") s.two.damping = v;
		else if (k == "mf2") s.two.maxForce = v;
		else if (k == "sp2") s.two.spring = v;
	}
	std::printf("gravity %.0f %%  damping %.0f %%  strength %.0f %% | one hand: wrist %.0f N m/rad (soft %.0f deg), up to %.0f N m, zeta %.2f; arm %.0f N/m, up to %.0f N, zeta %.2f\n",
	            s.gravity * 100, s.damping * 100, s.strength * 100, s.one.torqueSpring, s.one.torqueSoft * 180 / kPi, s.one.maxTorque, s.one.torqueDamping,
	            s.one.spring, s.one.maxForce, s.one.damping);
	std::printf("deg the weapon trails the controller (negative: ahead of it); cut: cm the tip trails a flat 140 degree cut in 0.3 s\n");
	std::printf("%-10s %-4s %5s | sag | chop 60 deg in 0.4 s: down up | slow 60 deg in 1 s: down up | cut | settle s: down up\n", "kind", "hand", "kg");
	struct Row { hh::Kind k; bool two; bool both; const char* name; };
	const Row rows[] = { { hh::Kind::Dagger, false, false, "dagger" },   { hh::Kind::Sword, false, false, "sword" },
	                     { hh::Kind::Mace, false, false, "mace" },       { hh::Kind::Sword, true, false, "2h sword" },
	                     { hh::Kind::Sword, true, true, "2h sword" },    { hh::Kind::Staff, false, false, "staff" },
	                     { hh::Kind::Staff, false, true, "staff" },      { hh::Kind::Spear, false, false, "spear" },
	                     { hh::Kind::Spear, false, true, "spear" },      { hh::Kind::Polearm, false, false, "polearm" },
	                     { hh::Kind::Polearm, false, true, "polearm" } };
	for (const Row& r : rows) {
		const double sec = r.both ? SecondAt(r.k, r.two) : -99;
		const Pitch d = PitchSwing(r.k, r.two, s, 30, -30, 0.4, sec), u = PitchSwing(r.k, r.two, s, -30, 30, 0.4, sec);
		const Pitch ds = PitchSwing(r.k, r.two, s, 30, -30, 1.0, sec), us = PitchSwing(r.k, r.two, s, -30, 30, 1.0, sec);
		const double sag = HoldLevel(r.k, r.two, s, 0, sec), cut = SwingLag(r.k, r.two, s, 0, sec);
		std::printf("%-10s %-4s %5.2f | %4.1f |                    %5.1f %5.1f |                       %5.1f %5.1f | %4.0f |            %5.2f %5.2f\n", r.name,
		            r.both ? "two" : "one", hh::ShapeOf(r.k, r.two).Mass(), sag, d.lag, u.lag, ds.lag, us.lag, cut * 100, d.settle, u.settle);
	}
	return 0;
}

// ---- Gravity: the checks ----

// A small deterministic random in [0, 1).
struct Lcg {
	uint32_t s = 12345;
	double Next() {
		s = s * 1664525u + 1013904223u;
		return (s >> 8) / 16777216.0;
	}
};

// One step of `dt` s of a rig with the right controller at `p` (Rig::Step steps a whole 1/90 s).
void StepDt(Rig& r, const Pose& p, double dt, const hh::Settings& s) {
	r.in[0].pose = p;
	if (r.second > -10) r.in[1].pose = r.Left(p);
	r.w.Step(r.in, dt, s, r.out);
}

// A frame time as a headset gives it: 72 to 120 Hz.
double FrameDt(Lcg& rnd) { return 1.0 / 120.0 + (1.0 / 72.0 - 1.0 / 120.0) * rnd.Next(); }

// The most the tip moves off its mean in `seconds`, after `settleFor` s of holding still (m), frames of uneven length.
double TipJitter(hh::Kind k, bool two, const hh::Settings& s, double second, double settleFor = 3.0, double seconds = 3.0) {
	Rig r;
	r.second = second;
	const Pose p { { 0.15, 1.3, -0.4 }, {} };
	r.Start(k, two, p, s);
	Lcg rnd;
	std::vector<V3> tips;
	for (double t = 0; t < settleFor + seconds;) {
		const double dt = FrameDt(rnd);
		StepDt(r, p, dt, s);
		t += dt;
		if (t >= settleFor) {
			V3 tip;
			r.w.Ends(nullptr, &tip);
			tips.push_back(tip);
		}
	}
	V3 mean;
	for (const V3& v : tips) mean = Add(mean, Mul(v, 1.0 / tips.size()));
	double worst = 0;
	for (const V3& v : tips) worst = std::fmax(worst, Len(Sub(v, mean)));
	return worst;
}

// The controller turns 30 degrees about the vertical in one frame and stays: how the weapon's heading comes round, as the most it is off the
// controller's heading in each 0.2 s window (deg). The windows may not grow, and the last is nearly nothing.
std::vector<double> JumpEnvelope(hh::Kind k, bool two, const hh::Settings& s, double second) {
	Rig r;
	r.second = second;
	const Pose p0 { { 0.15, 1.3, -0.4 }, {} };
	r.Start(k, two, p0, s);
	Lcg rnd;
	for (double t = 0; t < 1.0;) {
		const double dt = FrameDt(rnd);
		StepDt(r, p0, dt, s);
		t += dt;
	}
	const Pose p1 { p0.pos, QFromAxisAngle({ 0, 1, 0 }, 30 * kPi / 180) };
	auto heading = [](const Q& q, const V3& along) {
		const V3 d = Rotate(q, along);
		return std::atan2(d.x, -d.z) * 180 / kPi;
	};
	const double c = heading(p1.rot, kAlong);
	std::vector<double> win;
	double worst = 0;
	int n = 0;
	for (double t = 0; t < 3.0;) {
		const double dt = FrameDt(rnd);
		StepDt(r, p1, dt, s);
		t += dt;
		worst = std::fmax(worst, std::fabs(heading(r.w.BodyPose().rot, r.w.ShaftLine().dir) - c));
		if (t >= 0.2 * (n + 1)) {
			win.push_back(worst);
			worst = 0;
			++n;
		}
	}
	return win;
}

// The energy of the weapon and its hand's springs (kinetic + the steel's height + what the springs hold) after a jump of the controller that stays
// put, step by step: with the muscles' ceilings out of the way it may only go down, whatever the gravity, the frame length or the damper.
struct EnergyOut {
	double start = 0, end = 0, worstRise = 0;
};
EnergyOut EnergyRun(double gravity, double damping, bool jitter) {
	const Weapon& wp = WeaponAt(FindWeapon("broadsword"));
	const Body body = Held(wp, false);
	const hh::Strength f = hh::OneHandSoft();
	Grip g;
	Muscle& m = g.muscle;
	m.spring       = f.spring;
	m.damper       = 2.0 * f.damping * damping * std::sqrt(f.spring * body.mass);
	m.maxForce     = 1e12;
	m.torqueSpring = f.torqueSpring;
	m.torqueDamper = 4.0 * f.torqueDamping * damping * std::sqrt(f.torqueSpring * 0.3);
	m.maxTorque    = 1e12;
	m.torqueSoft   = f.torqueSoft;
	World world;
	world.gravity = { 0, -9.81 * gravity, 0 };
	auto energy = [&](const State& st, const Pose& c) {
		const V3 wb = Rotate(QConj(st.rot), st.angVel);
		V3 iw;
		iw.x = body.inertia.m[0][0] * wb.x + body.inertia.m[0][1] * wb.y + body.inertia.m[0][2] * wb.z;
		iw.y = body.inertia.m[1][0] * wb.x + body.inertia.m[1][1] * wb.y + body.inertia.m[1][2] * wb.z;
		iw.z = body.inertia.m[2][0] * wb.x + body.inertia.m[2][1] * wb.y + body.inertia.m[2][2] * wb.z;
		const double ke = 0.5 * body.mass * Dot(st.vel, st.vel) + 0.5 * Dot(wb, iw);
		const double ug = body.weight * 9.81 * gravity * PointAt(st, body, body.weightAt).y;
		const Pose hand = HandPose(st, body, g);
		const V3 x = Sub(c.pos, hand.pos);
		const double ulin = 0.5 * m.spring * Dot(x, x);
		Q rel = QMul(QConj(c.rot), hand.rot);
		if (rel.w < 0) rel = { -rel.x, -rel.y, -rel.z, -rel.w };
		const double sv = Len(V3 { rel.x, rel.y, rel.z });
		const double uang = m.torqueSpring * (0.5 * sv * sv + sv * sv * sv * sv / (m.torqueSoft * m.torqueSoft));
		return ke + ug + ulin + uang;
	};
	Controller a, b;
	a.pose = { { 0.15, 1.3, 0.4 }, QFromAxisAngle({ 1, 0, 0 }, kPi / 2) };
	b.pose = { Add(a.pose.pos, { 0.05, 0.03, 0.0 }), QMul(QFromAxisAngle({ 1, 0, 0 }, 35 * kPi / 180), a.pose.rot) };
	State st;
	Step(st, body, &g, &a, &a, 1, 0, world);
	Lcg rnd;
	for (double t = 0; t < 2.0;) {
		const double dt = jitter ? FrameDt(rnd) : kDt;
		Step(st, body, &g, &a, &a, 1, dt, world);
		t += dt;
	}
	EnergyOut o;
	double prev = energy(st, b.pose);
	o.start = prev;
	for (double t = 0; t < 3.0;) {
		const double dt = jitter ? FrameDt(rnd) : kDt;
		Step(st, body, &g, &b, &b, 1, dt, world);
		t += dt;
		const double e = energy(st, b.pose);
		o.worstRise = std::fmax(o.worstRise, e - prev);
		prev = e;
	}
	o.end = prev;
	return o;
}

// A hand that is shaken as hard as a person can: the weapon stays finite and near the hand.
double Shaken(hh::Kind k, bool two, const hh::Settings& s, double second, bool* finite) {
	Rig r;
	r.second = second;
	const V3 shoulder { 0.20, 1.40, 0.0 };
	auto at = [&](double t) {
		const double yaw = 1.2 * std::sin(9 * t) + 0.8 * std::sin(17 * t + 1), pitch = 0.9 * std::sin(11 * t + 2) + 0.5 * std::sin(23 * t);
		const Q q = QMul(QFromAxisAngle({ 0, 1, 0 }, yaw), QFromAxisAngle({ 1, 0, 0 }, pitch));
		const double jump = std::floor(t / 2.5) * 0.35;   // a step of 35 cm every 2.5 s (a recentre of the hand, below the weapon's own snap)
		return Pose { Add(Add(shoulder, Rotate(q, { 0, -0.10, -0.55 })), { std::fmod(jump, 0.7) - 0.35, 0, 0 }), q };
	};
	r.Start(k, two, at(0), s);
	Lcg rnd;
	double worst = 0;
	*finite = true;
	for (double t = 0; t < 20.0;) {
		const double dt = FrameDt(rnd);
		StepDt(r, at(t), dt, s);
		t += dt;
		V3 tip;
		r.w.Ends(nullptr, &tip);
		if (!std::isfinite(tip.x + tip.y + tip.z)) *finite = false;
		worst = std::fmax(worst, Len(Sub(tip, at(t).pos)));
	}
	return worst;
}

int Gravity() {
	std::printf("gravity feel (the soft wrists, hold.h UseGravityFeel):\n");
	const hh::Settings soft = Soft();
	using K = hh::Kind;
	struct W { K k; bool two; const char* name; };
	const W weights[] = { { K::Sword, false, "sword" }, { K::Mace, false, "mace" }, { K::Sword, true, "two-handed sword in one hand" },
	                      { K::Polearm, false, "polearm in one hand" } };

	// a swing down against a swing up: the weight helps the one and slows the other
	for (const W& w : weights) {
		const Pitch d = PitchSwing(w.k, w.two, soft, 30, -30, 0.4), u = PitchSwing(w.k, w.two, soft, -30, 30, 0.4);
		const Pitch ds = PitchSwing(w.k, w.two, soft, 30, -30, 1.0), us = PitchSwing(w.k, w.two, soft, -30, 30, 1.0);
		std::printf("  %-28s chop down: trails %5.1f deg, up: %5.1f deg | slow down: %5.1f, up: %5.1f\n", w.name, d.lag, u.lag, ds.lag, us.lag);
		EXPECT(d.lag < 0.75 * u.lag, "%s: a swing down trails %.1f deg, a swing up %.1f deg - not asymmetric enough", w.name, d.lag, u.lag);
		EXPECT(ds.lag < 1.0, "%s: a slow swing down still trails %.1f deg - gravity does not help it", w.name, ds.lag);
		EXPECT(us.lag > 3.0, "%s: a slow lift trails only %.1f deg - gravity does not slow it", w.name, us.lag);
		EXPECT(ds.over > us.over + 2.0, "%s: a slow swing down is ahead of the controller by %.1f deg, a slow lift by %.1f - it does not lead", w.name, ds.over, us.over);
	}
	{   // a dagger hardly feels it
		const Pitch d = PitchSwing(K::Dagger, false, soft, 30, -30, 0.4), u = PitchSwing(K::Dagger, false, soft, -30, 30, 0.4);
		const Pitch us = PitchSwing(K::Dagger, false, soft, -30, 30, 1.0);
		std::printf("  %-28s chop down: trails %5.1f deg, up: %5.1f deg | slow up: %5.1f\n", "dagger", d.lag, u.lag, us.lag);
		EXPECT(std::fabs(u.lag - d.lag) < 1.5 && us.lag < 1.0, "dagger: down %.1f up %.1f slow up %.1f deg", d.lag, u.lag, us.lag);
	}

	// held out level: heavier sinks more, a dagger not at all, two hands hold better
	{
		const double dagger = HoldLevel(K::Dagger, false, soft), sword = HoldLevel(K::Sword, false, soft), mace = HoldLevel(K::Mace, false, soft),
		             big = HoldLevel(K::Sword, true, soft), pole = HoldLevel(K::Polearm, false, soft);
		std::printf("  held out level in one hand: dagger %.1f deg, sword %.1f, mace %.1f, two-handed sword %.1f, polearm %.1f\n", dagger, sword, mace, big, pole);
		EXPECT(dagger < 1.0, "a dagger sinks %.1f deg", dagger);
		EXPECT(sword > 2.0 && sword < 8.0, "a sword sinks %.1f deg", sword);
		EXPECT(dagger < sword && sword < mace && mace < big && big < pole, "the heavier the more it sinks: %.1f %.1f %.1f %.1f %.1f", dagger, sword, mace, big, pole);
		EXPECT(pole > 8.0, "a polearm in one hand sinks only %.1f deg", pole);
		struct T { K k; bool two; double second; const char* name; };
		for (const T& t : { T { K::Sword, true, SecondAt(K::Sword, true), "two-handed sword" }, T { K::Polearm, false, SecondAt(K::Polearm, false), "polearm" },
		                    T { K::Staff, false, SecondAt(K::Staff, false), "staff" }, T { K::Spear, false, SecondAt(K::Spear, false), "spear" } }) {
			const double one = HoldLevel(t.k, t.two, soft), both = HoldLevel(t.k, t.two, soft, 0, t.second);
			std::printf("  %-28s one hand %.1f deg, two hands %.1f deg\n", t.name, one, both);
			EXPECT(both < 0.6 * one, "%s: two hands sink %.1f deg, one hand %.1f", t.name, both, one);
		}
		const Pitch one = PitchSwing(K::Polearm, false, soft, -30, 30, 0.4), two = PitchSwing(K::Polearm, false, soft, -30, 30, 0.4, SecondAt(K::Polearm, false));
		EXPECT(two.lag < 0.7 * one.lag, "a polearm lifted in two hands trails %.1f deg, in one %.1f", two.lag, one.lag);
	}

	// gravity 0: nothing sinks and a swing down is a swing up turned over - the lag is inertia and the force ceilings alone
	{
		const hh::Settings none = Soft(0.0);
		for (const W& w : weights) {
			const Pitch d = PitchSwing(w.k, w.two, none, 30, -30, 0.4), u = PitchSwing(w.k, w.two, none, -30, 30, 0.4);
			const double sag = HoldLevel(w.k, w.two, none);
			std::printf("  gravity 0, %-28s sag %.2f deg, chop down trails %5.1f deg, up %5.1f\n", w.name, sag, d.lag, u.lag);
			EXPECT(std::fabs(sag) < 0.05, "%s: gravity 0 and it sinks %.2f deg", w.name, sag);
			EXPECT(std::fabs(d.lag - u.lag) < 0.15 * std::fmax(d.lag, u.lag) + 0.3, "%s: gravity 0, down %.1f deg and up %.1f deg are not alike", w.name, d.lag, u.lag);
		}
		const double dagger = PitchSwing(K::Dagger, false, none, -30, 30, 0.4).lag, sword = PitchSwing(K::Sword, false, none, -30, 30, 0.4).lag,
		             big = PitchSwing(K::Sword, true, none, -30, 30, 0.4).lag;
		EXPECT(dagger < sword && sword < big, "the lag is inertia: dagger %.1f sword %.1f two-handed sword %.1f deg", dagger, sword, big);
	}

	// the gravity slider: more of it, more sag, and a wider gap between down and up
	{
		double sag[4], gap[4];
		const double pct[4] = { 0.5, 1.0, 2.0, 3.0 };
		for (int i = 0; i < 4; ++i) {
			const hh::Settings s = Soft(pct[i]);
			sag[i] = HoldLevel(K::Sword, false, s);
			gap[i] = PitchSwing(K::Sword, false, s, -30, 30, 0.4).lag - PitchSwing(K::Sword, false, s, 30, -30, 0.4).lag;
		}
		std::printf("  gravity 50/100/200/300 %%, a sword: sag %.1f %.1f %.1f %.1f deg, up minus down %.1f %.1f %.1f %.1f deg\n", sag[0], sag[1], sag[2], sag[3], gap[0],
		            gap[1], gap[2], gap[3]);
		EXPECT(sag[0] < sag[1] && sag[1] < sag[2] && sag[2] < sag[3], "the sag does not grow with gravity");
		EXPECT(gap[0] < gap[1] && gap[1] < gap[2] && gap[2] < gap[3], "the gap between a swing down and up does not grow with gravity");
		EXPECT(sag[1] > 1.6 * sag[0] * 0.9 && sag[2] > 1.5 * sag[1], "the sag is not about proportional to gravity: %.2f %.2f %.2f", sag[0], sag[1], sag[2]);
	}

	// the strength slider: stronger hands hold the weight better (soft wrists); with the old wrists it moves only the ceilings
	{
		const double weak = HoldLevel(K::Sword, true, Soft()), strongHands = [&] {
			hh::Settings s = Soft();
			s.strength     = 2.0;
			return HoldLevel(K::Sword, true, s);
		}();
		hh::Settings old50, old200;
		old50.strength  = 0.5;
		old200.strength = 2.0;
		const double a = HoldLevel(K::Sword, false, old50), b = HoldLevel(K::Sword, false, old200);
		std::printf("  strength: a two-handed sword in one hand sinks %.1f deg, at 200 %% %.1f; the old wrists, a sword: %.2f at 50 %%, %.2f at 200 %%\n", weak, strongHands, a, b);
		EXPECT(strongHands < 0.7 * weak, "stronger hands sink %.1f deg against %.1f", strongHands, weak);
		EXPECT(std::fabs(a - b) < 0.05, "the old wrists' sag moved with strength: %.2f %.2f", a, b);
	}

	// the damper is no drag: it follows the controller's speed, so a smaller one lets the weapon fall behind a hard cut and ring after it
	{
		const hh::Settings low = Soft(1.0, 0.3), high = Soft(1.0, 2.0);
		const double cutLow = SwingLag(K::Sword, false, low), cut = SwingLag(K::Sword, false, soft), cutHigh = SwingLag(K::Sword, false, high);
		const Pitch pLow = PitchSwing(K::Sword, false, low, 30, -30, 0.4), p = PitchSwing(K::Sword, false, soft, 30, -30, 0.4);
		std::printf("  damping 30/100/200 %%, a sword's hard cut: %.0f %.0f %.0f cm behind; settling after a chop %.2f s at 30 %%, %.2f s at 100 %%\n", cutLow * 100, cut * 100,
		            cutHigh * 100, pLow.settle, p.settle);
		EXPECT(cutLow > cut, "a weaker damper did not let the cut fall further behind (%.0f %.0f cm)", cutLow * 100, cut * 100);
		EXPECT(pLow.settle > p.settle, "a weaker damper settled in %.2f s, the normal one in %.2f", pLow.settle, p.settle);
		EXPECT(cutHigh <= cut * 1.05, "a stronger damper made the cut worse (%.0f cm against %.0f)", cutHigh * 100, cut * 100);
	}

	// stable: no jitter, no growing swing, no energy out of nothing, nothing blows up
	{
		const struct { K k; bool two; double second; const char* name; } cases[] = { { K::Dagger, false, -99, "dagger" }, { K::Sword, false, -99, "sword" },
		    { K::Polearm, false, -99, "polearm, one hand" }, { K::Sword, true, SecondAt(K::Sword, true), "two-handed sword, two hands" } };
		for (const auto& c : cases) {
			const double j = TipJitter(c.k, c.two, soft, c.second);
			EXPECT(j < 0.0005, "%s held still, frames of 8-14 ms: the tip moves %.2f mm", c.name, j * 1000);
			const std::vector<double> win = JumpEnvelope(c.k, c.two, soft, c.second);
			bool grows = false;
			for (size_t i = 1; i < win.size(); ++i)
				if (win[i] > win[i - 1] + 0.05) grows = true;
			std::printf("  %-28s held still: tip moves %.3f mm; after a 30 deg jump the heading is off by %.1f deg in the first 0.2 s, %.2f in the last\n", c.name, j * 1000,
			            win.front(), win.back());
			EXPECT(!grows, "%s: the swing after a jump grows", c.name);
			EXPECT(win.back() < 0.3, "%s: still %.2f deg off 3 s after a jump", c.name, win.back());
		}
		for (const double g : { 0.0, 1.0, 3.0 })
			for (const double d : { 0.0, 1.0, 2.0 })
				for (const bool jitter : { false, true }) {
					const EnergyOut e = EnergyRun(g, d, jitter);
					EXPECT(e.worstRise < 1e-3 * std::fmax(1.0, e.start - e.end) + 1e-4, "gravity %.0f%% damping %.0f%% %s: the energy rose by %.5f J in a step (%.2f J in all)", g * 100,
					       d * 100, jitter ? "jittery frames" : "90 Hz", e.worstRise, e.start - e.end);
					if (g == 1.0 && d == 1.0)
						std::printf("  energy after a jump (gravity 100%%, %s): %.2f J -> %.2f J, the worst step %+.5f J\n", jitter ? "jittery frames" : "90 Hz", e.start, e.end, e.worstRise);
				}
		for (const auto& c : cases) {
			bool finite = true;
			const double worst = Shaken(c.k, c.two, soft, c.second, &finite);
			EXPECT(finite && worst < 4.0, "%s shaken for 20 s: finite %d, %.2f m from the hand at worst", c.name, finite, worst);
		}
	}

	// every kind with the soft wrists: nothing blows up, a dagger and a sword still follow a hard cut, a two-hander in one hand still wants the other
	{
		std::printf("  %-10s %-4s %6s | one hand: %7s %8s | two hands: %7s %8s\n", "kind", "2h", "kg", "sag deg", "swing cm", "sag deg", "swing cm");
		for (int i = 0; i < (int)hh::Kind::Count; ++i)
			for (int two = 0; two < 2; ++two) {
				const hh::Kind k = (hh::Kind)i;
				if (two && k != K::Sword && k != K::Axe && k != K::Mace) continue;
				const double b = SecondAt(k, two);
				const double sag = HoldLevel(k, two, soft), lag = SwingLag(k, two, soft), sag2 = HoldLevel(k, two, soft, 0, b), lag2 = SwingLag(k, two, soft, 0, b);
				std::printf("  %-10s %-4s %6.2f |           %7.1f %8.1f |            %7.1f %8.1f\n", hh::KindName(k), two ? "yes" : "", hh::ShapeOf(k, two).Mass(), sag, lag * 100,
				            sag2, lag2 * 100);
				EXPECT(std::isfinite(sag) && std::isfinite(lag) && std::isfinite(sag2) && std::isfinite(lag2), "%s blew up", hh::KindName(k));
			}
		const double d = SwingLag(K::Dagger, false, soft), w = SwingLag(K::Sword, false, soft), one = SwingLag(K::Sword, true, soft);
		const double sweep1 = SwingLag(K::Sword, true, soft, 0, -99, 90, 0.6), sweep2 = SwingLag(K::Sword, true, soft, 0, -0.2, 90, 0.6);
		EXPECT(d < 0.06, "a dagger's tip is %.0f cm behind on a cut", d * 100);
		EXPECT(w < 0.45, "a sword's tip is %.0f cm behind on a cut", w * 100);
		EXPECT(one > 2 * w, "a two-handed sword in one hand lags %.0f cm, a sword %.0f", one * 100, w * 100);
		EXPECT(sweep2 < sweep1, "a two-handed sword lags %.0f cm in two hands, %.0f in one", sweep2 * 100, sweep1 * 100);
	}

	// the old stiff wrists are as they were (gravity feel unticked): pinned to what they gave before the soft ones came
	{
		const hh::Settings old;
		const double sag = HoldLevel(K::Sword, false, old), pole = HoldLevel(K::Polearm, false, old), cut = SwingLag(K::Sword, false, old);
		std::printf("  the old wrists: a sword sinks %.2f deg (0.6), a polearm %.2f (4.7), a hard cut trails %.0f cm (29)\n", sag, pole, cut * 100);
		EXPECT(std::fabs(sag - 0.6) < 0.1 && std::fabs(pole - 4.7) < 0.3 && std::fabs(cut * 100 - 28.7) < 1.5, "the old wrists moved: %.2f %.2f %.1f", sag, pole, cut * 100);
	}

	// a weapon let go falls at the real g, whatever the slider says (or it would never land)
	{
		hh::Hold w;
		const hh::Settings none = Soft(0.0);
		const V3 hand { 0.15, 1.3, -0.4 };
		hh::HandIn in[2] = { Hand(hand, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
		hh::HandOut out[2];
		w.Place(0, K::Sword, false, { {}, kAlong }, in, none);
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, none, out);
		in[0].grip = 1;
		for (double t = 0; t < 0.2; t += kDt) w.Step(in, kDt, none, out);
		in[0].grip = 0;
		double landedAt = -1;
		for (double t = 0; t < 3.0; t += kDt) {
			w.Step(in, kDt, none, out);
			if (landedAt < 0 && w.Landed()) landedAt = t;
		}
		EXPECT(landedAt >= 0 && landedAt < 1.0, "gravity 0: a weapon let go landed after %.2f s", landedAt);
	}
	return g_fails;
}

// argv: [wrist spring, wrist ceiling, arm ceiling] for one hand, to tune by (hold.h OneHand).
int Hands(int argc = 0, char** argv = nullptr) {
	hh::Settings s;
	if (argc >= 1) s.one.torqueSpring = std::atof(argv[0]);
	if (argc >= 2) s.one.maxTorque = std::atof(argv[1]);
	if (argc >= 3) s.one.maxForce = s.two.maxForce = std::atof(argv[2]);
	std::printf("%-10s %-4s %6s | one hand: %7s %8s | two hands: %7s %8s\n", "kind", "2h", "kg", "sag deg", "swing cm", "sag deg", "swing cm");
	for (int i = 0; i < (int)hh::Kind::Count; ++i)
		for (int two = 0; two < 2; ++two) {
			const hh::Kind k = (hh::Kind)i;
			if (two && k != hh::Kind::Sword && k != hh::Kind::Axe && k != hh::Kind::Mace) continue;
			const double b = SecondAt(k, two);
			const double sag = HoldLevel(k, two, s), lag = SwingLag(k, two, s), sag2 = HoldLevel(k, two, s, 0, b), lag2 = SwingLag(k, two, s, 0, b);
			std::printf("%-10s %-4s %6.2f |           %7.1f %8.1f |            %7.1f %8.1f\n", hh::KindName(k), two ? "yes" : "",
			            hh::ShapeOf(k, two).Mass(), sag, lag * 100, sag2, lag2 * 100);
			EXPECT(std::isfinite(sag) && std::isfinite(lag) && std::isfinite(sag2) && std::isfinite(lag2), "%s blew up", hh::KindName(k));
		}
	const double dagger = HoldLevel(hh::Kind::Dagger, false, s), sword = HoldLevel(hh::Kind::Sword, false, s),
	             big = HoldLevel(hh::Kind::Sword, true, s);
	EXPECT(dagger < 1.0, "a dagger sinks %.1f deg", dagger);
	EXPECT(dagger < sword && sword < big, "sag dagger %.1f sword %.1f two-handed sword %.1f", dagger, sword, big);
	{   // a dagger and a sword follow a hard cut; a two-handed sword in one hand cannot (on a hard cut it is
		// a flail in two as well); on a sweep (90 degrees in 0.6 s) two hands hold it better than one
		const double d = SwingLag(hh::Kind::Dagger, false, s), w = SwingLag(hh::Kind::Sword, false, s), one = SwingLag(hh::Kind::Sword, true, s);
		const double sweep1 = SwingLag(hh::Kind::Sword, true, s, 0, -99, 90, 0.6), sweep2 = SwingLag(hh::Kind::Sword, true, s, 0, -0.2, 90, 0.6);
		std::printf("two-handed sword on a sweep: %.0f cm behind in one hand, %.0f in two\n", sweep1 * 100, sweep2 * 100);
		EXPECT(d < 0.06, "a dagger's tip is %.0f cm behind on a cut", d * 100);
		EXPECT(w < 0.30, "a sword's tip is %.0f cm behind on a cut", w * 100);
		EXPECT(one > 2 * w, "a two-handed sword in one hand lags %.0f cm, a sword %.0f", one * 100, w * 100);
		EXPECT(sweep2 < sweep1, "a two-handed sword lags %.0f cm in two hands, %.0f in one", sweep2 * 100, sweep1 * 100);
	}
	{   // no weight: even a two-handed sword stays level
		hh::Settings none = s;
		none.weight = 0;
		const double sag = HoldLevel(hh::Kind::Sword, true, none);
		EXPECT(sag < 0.5, "a weightless two-handed sword sinks %.1f deg", sag);
	}
	{   // held by its butt, a spear sinks more and lags far more on a sweep (90 degrees in 0.6 s) than held
		// where the game holds it (on a hard cut it is a flail either way)
		const double sagGrip = HoldLevel(hh::Kind::Spear, false, s), sagButt = HoldLevel(hh::Kind::Spear, false, s, -0.55);
		const double lagGrip = SwingLag(hh::Kind::Spear, false, s, 0, -99, 90, 0.6),
		             lagButt = SwingLag(hh::Kind::Spear, false, s, -0.55, -99, 90, 0.6);
		std::printf("spear in one hand: where the game holds it %.1f deg, a sweep %.0f cm behind; by its butt %.1f deg, %.0f cm\n", sagGrip,
		            lagGrip * 100, sagButt, lagButt * 100);
		EXPECT(sagButt > sagGrip * 1.5, "by its butt the spear sinks %.1f deg, %.1f where the game holds it", sagButt, sagGrip);
		EXPECT(lagButt > lagGrip * 1.5, "by its butt the spear lags %.0f cm, %.0f where the game holds it", lagButt * 100, lagGrip * 100);
	}

	// A spear in the right hand where the game holds it (its grip never pressed). The left takes it at its
	// butt; the right grip pressed and let go: the right lets go, the left has it. The left grip let go:
	// it falls and lies on the floor. Taken up off the floor; let go again; the right grip pressed away
	// from it calls it back into the right fist.
	{
		hh::Hold w;
		const V3 r { 0.15, 1.3, -0.4 };
		hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
		w.Place(0, hh::Kind::Spear, false, { {}, kAlong }, in, s);
		hh::HandOut out[2];
		for (double t = 0; t < 1.0; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(out[0].holds && w.Held(), "placed in the right hand, its grip not pressed, the spear was not held");
		V3 butt;
		w.Ends(&butt, nullptr);
		// far from the shaft: nothing
		in[1] = Hand(Add(butt, { 0, 0.3, 0 }), {}, 1);
		for (double t = 0; t < 0.2; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(!out[1].holds, "a grip 30 cm from the spear took it");
		// busy (a belt potion in it): nothing
		in[1].grip = 0;
		w.Step(in, kDt, s, out);
		in[1]      = Hand(butt, {}, 1);
		in[1].busy = true;
		for (double t = 0; t < 0.2; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(!out[1].holds, "a hand with a potion took the spear");
		in[1].busy = false;
		in[1].grip = 0;
		w.Step(in, kDt, s, out);
		// near the butt: taken there
		w.Ends(&butt, nullptr);
		in[1] = Hand(Add(butt, { 0.03, 0.04, 0 }), {}, 1);
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(out[1].holds && out[1].grabs == 1 && out[0].holds, "the left hand did not take the spear by its butt");
		const double along = Dot(Sub(w.OnWeapon(1).pos, w.BodyPose().pos), Rotate(w.BodyPose().rot, w.ShaftLine().dir));
		EXPECT(along < -0.4, "the left hand holds %.2f m along the shaft, not at its butt", along);
		// the right grip pressed and let go: the right hand comes off at once, the left has it
		in[0].grip = 1;
		w.Step(in, kDt, s, out);
		in[0].grip = 0;
		w.Step(in, kDt, s, out);
		EXPECT(!out[0].holds && out[1].holds && w.Main() == 1 && w.Held(), "a grip let go did not pass the spear to the left (right %d left %d main %d)",
		       out[0].holds, out[1].holds, w.Main());
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(out[0].on == 0, "the right hand is still drawn %.2f on the spear", out[0].on);
		// the left grip let go too: it falls, and lies on the floor
		in[1].grip = 0;
		w.Step(in, kDt, s, out);
		EXPECT(!out[0].holds && !out[1].holds && !w.Held(), "both grips let go, the spear was still held");
		for (double t = 0; t < 3.0; t += kDt) w.Step(in, kDt, s, out);
		V3 tip;
		w.Ends(&butt, &tip);
		std::printf("spear let go from 1.3 m: lies with its butt at %.2f m and its tip at %.2f m over the floor\n", butt.y, tip.y);
		EXPECT(butt.y < 0.1 && tip.y < 0.1 && butt.y > -0.01 && tip.y > -0.01, "the spear does not lie on the floor (butt %.2f tip %.2f m)",
		       butt.y, tip.y);
		// taken up off the floor by the right hand, where it closes on it
		const V3 mid = Mul(Add(butt, tip), 0.5);
		in[0] = Hand(Add(mid, { 0, 0.05, 0 }), {}, 0);
		w.Step(in, kDt, s, out);
		in[0].grip = 1;
		w.Step(in, kDt, s, out);
		EXPECT(out[0].holds && out[0].grabs == 1 && w.Held(), "the right hand did not take the spear up off the floor");
		in[0].pose.pos = Add(in[0].pose.pos, { 0, 1.0, 0 });   // lifted
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		w.Ends(&butt, &tip);
		EXPECT(butt.y > 0.3 || tip.y > 0.3, "the spear taken up stayed on the floor");
		// let go, it falls; the right grip pressed far from it: it flies back into the right fist
		in[0].grip = 0;
		w.Step(in, kDt, s, out);
		for (double t = 0; t < 2.0; t += kDt) w.Step(in, kDt, s, out);
		in[0] = Hand(r, {}, 0);
		w.Step(in, kDt, s, out);
		in[0].grip = 1;
		w.Step(in, kDt, s, out);
		EXPECT(w.Flying(), "the right grip pressed away from the spear did not call it back");
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(out[0].holds && out[0].grabs == 0 && w.AsPlaced() && Len(Sub(w.BodyPose().pos, r)) < 0.05,
		       "the spear called back is not in the right fist where the game holds it (%.2f m off)", Len(Sub(w.BodyPose().pos, r)));
	}
	// The game's hand (its grip never pressed) pulled away once the other has it: it lets go too.
	{
		hh::Hold w;
		const V3 r { 0.15, 1.3, -0.4 };
		hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
		w.Place(0, hh::Kind::Spear, false, { {}, kAlong }, in, s);
		hh::HandOut out[2];
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		V3 butt;
		w.Ends(&butt, nullptr);
		in[1] = Hand(butt, {}, 1);
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		in[0].pose.pos = Add(r, { 0.6, -0.3, 0 });
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(!out[0].holds && out[1].holds && w.Main() == 1, "the right hand pulled away did not let go (right %d left %d main %d)",
		       out[0].holds, out[1].holds, w.Main());
	}
	// The grip shared with the weapon swap (busy = claimed: the grip behind a shoulder is the swap's, vr/vrcam.cpp sets it for the weight
	// model): a hand that holds the weapon keeps it while its grip is claimed; let go, the weapon falls; the grip pressed again (the
	// swap's press) neither takes it up nor calls it back; once the claim is over a new press calls it back into the game's hand.
	{
		hh::Hold w;
		const V3 r { 0.15, 1.3, -0.4 };
		hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
		w.Place(0, hh::Kind::Sword, false, { {}, kAlong }, in, s);
		hh::HandOut out[2];
		for (double t = 0; t < 0.5; t += kDt) w.Step(in, kDt, s, out);
		in[0].grip = 1;   // the grip squeezed once: the hand holds it by its grip now, no longer as placed
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, s, out);
		in[0].busy = true;   // the hand goes behind the shoulder, its grip down: claimed
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(out[0].holds && w.Held(), "a claimed grip let go of the weapon it held");
		in[0].grip = 0;   // the grip let go behind the shoulder
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(!out[0].holds && !w.Held(), "the weapon was still held with the grip let go");
		in[0].grip = 1;   // pressed again, claimed: the swap's press
		for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, s, out);
		EXPECT(!out[0].holds && !w.Flying() && !w.Held(), "the swap's press took the weapon (holds %d flying %d held %d)", out[0].holds, w.Flying(), w.Held());
		in[0].grip = 0;   // the swap done, the grip let go and the claim over
		in[0].busy = false;
		for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, s, out);
		in[0].grip = 1;   // a new press, away from the weapon on the floor: it flies back into the game's hand
		w.Step(in, kDt, s, out);
		EXPECT(w.Flying() || w.Held(), "a press with the claim over did not call the weapon back");
	}
	// A weapon let go falls, lands, lies, and goes to the hero's back (Settings::stowAfter): the timing, who takes it from there, the call-back.
	{
		hh::Settings st = s;
		st.stowAfter    = 0.6;
		const V3 r { 0.15, 1.3, -0.4 };
		// a sword placed in the right fist; the grip squeezed once and let go: it falls and lands; time counts from the landing
		auto dropped = [&](hh::Hold& w, const hh::Settings& set) {
			hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			w.Place(0, hh::Kind::Sword, false, { {}, kAlong }, in, set);
			hh::HandOut out[2];
			for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, set, out);
			in[0].grip = 1;
			for (double t = 0; t < 0.2; t += kDt) w.Step(in, kDt, set, out);
			in[0].grip = 0;   // let go: falls
			w.Step(in, kDt, set, out);
		};
		auto lie = [&](hh::Hold& w, const hh::Settings& set, double seconds) {
			hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			hh::HandOut out[2];
			for (double t = 0; t < seconds; t += kDt) w.Step(in, kDt, set, out);
		};
		{   // timing: not before it has landed, not before the delay after, then Stowed
			hh::Hold w;
			dropped(w, st);
			EXPECT(!w.Held() && !w.Stowed(), "the weapon was held or stowed at once after it was let go");
			double landedAt = -1, stowedAt = -1;
			hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			hh::HandOut out[2];
			for (double t = 0; t < 4.0; t += kDt) {
				w.Step(in, kDt, st, out);
				V3 butt, tip;
				w.Ends(&butt, &tip);
				if (landedAt < 0 && !w.Stowed() && butt.y < 0.05 && tip.y < 0.05) landedAt = t;
				if (stowedAt < 0 && w.Stowed()) stowedAt = t;
			}
			EXPECT(stowedAt >= 0, "the weapon never went to the back");
			EXPECT(landedAt >= 0 && stowedAt - landedAt >= 0.55 && stowedAt - landedAt <= 0.75, "it went to the back %.2f s after landing, not 0.6", stowedAt - landedAt);
			EXPECT(!w.Held() && !w.Flying(), "a stowed weapon is held or flies");
		}
		{   // stowAfter < 0 (to_back off): it lies on the floor for ever; 0: the moment it lies
			hh::Hold w;
			hh::Settings never = s;
			never.stowAfter    = -1;
			dropped(w, never);
			lie(w, never, 5.0);
			EXPECT(!w.Stowed() && !w.Held(), "to_back off: the weapon left the floor");
			hh::Hold z;
			hh::Settings zero = s;
			zero.stowAfter    = 0;
			dropped(z, zero);
			lie(z, zero, 3.0);
			EXPECT(z.Stowed(), "stowAfter 0: the weapon never went to the back");
		}
		{   // picked up off the floor before the delay is over: not stowed (the old rule: the floor pick-up while it lies)
			hh::Hold w;
			hh::Settings slow = s;
			slow.stowAfter    = 1.5;
			dropped(w, slow);
			lie(w, slow, 0.9);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 mid = Mul(Add(butt, tip), 0.5);
			hh::HandIn in[2] = { Hand(Add(mid, { 0, 0.05, 0 }), {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, slow, out);
			in[0].grip = 1;
			w.Step(in, kDt, slow, out);
			EXPECT(w.Held() && !w.Stowed() && out[0].holds, "the weapon was not picked up off the floor before it went to the back");
		}
		{   // a THROWN weapon (MarkThrown): let go like any, it flies and falls with the motion it had - and is never taken, called back or stowed; it only ends
			hh::Hold w;
			hh::Settings set = st;
			set.stowAfter    = 0;   // (the quickest stow there is: a thrown one must not be stowed even so)
			hh::HandIn in[2] = { Hand(r, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			hh::HandOut out[2];
			w.Place(0, hh::Kind::Javelin, false, { {}, kAlong }, in, set);
			for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, set, out);
			in[0].grip = 1;
			for (double t = 0; t < 0.2; t += kDt) w.Step(in, kDt, set, out);
			w.MarkThrown();   // the throw may be known a frame before the grip is seen open: armed, the hand still holds it
			EXPECT(!w.Thrown() && w.Held(), "(thrown) armed while a hand holds it: not yet a thrown weapon in the air");
			in[0].grip = 0;
			w.Step(in, kDt, set, out);
			EXPECT(w.Thrown() && !w.Held() && !w.Flying() && !w.Stowed(), "(thrown) released: a thrown weapon in the air");
			for (double t = 0; t < 0.15; t += kDt) w.Step(in, kDt, set, out);
			EXPECT(w.BodyPose().pos.y < r.y - 0.05, "(thrown) it did not fall out of the hand (y %.2f from %.2f)", w.BodyPose().pos.y, r.y);
			in[0].grip = 1;   // a grip squeezed on it meanwhile (the game's hand): it is not for taking or calling back
			w.Step(in, kDt, set, out);
			EXPECT(!w.Held() && !w.Flying(), "(thrown) a grip took or called back a thrown weapon");
			in[0].grip = 0;
			double landedAt = -1;
			for (double t = 0; t < 3.0; t += kDt) {
				w.Step(in, kDt, set, out);
				if (landedAt < 0 && w.Landed()) landedAt = t;
			}
			EXPECT(landedAt >= 0 && w.Landed() && !w.Stowed(), "(thrown) it landed %d and is stowed %d (stowAfter 0)", landedAt >= 0, w.Stowed());
			EXPECT(w.ThrownFor() > 0.3, "(thrown) the time out of the hands is not counted (%.2f s)", w.ThrownFor());
			w.ClearThrown();   // a throw that was not one: a plain drop again - it goes to the back as any
			w.Step(in, kDt, set, out);
			w.Step(in, kDt, set, out);
			EXPECT(w.Stowed(), "(thrown) cleared, the landed weapon did not go to the back");
		}
		{   // a release whose verdict is not in (SetStowHold): the landed weapon waits for it; no throw, it goes to the back
			hh::Hold w;
			hh::Settings set = st;
			set.stowAfter    = 0;
			dropped(w, set);
			w.SetStowHold(true);
			lie(w, set, 3.0);
			EXPECT(w.Landed() && !w.Stowed(), "(verdict) a landed weapon was stowed before the verdict");
			w.SetStowHold(false);
			lie(w, set, 0.1);
			EXPECT(w.Stowed(), "(verdict) it did not go to the back after the verdict");
		}
		{   // a thrown mark on a weapon flying into a hand, or stowed: ignored
			hh::Hold w;
			dropped(w, st);
			lie(w, st, 3.0);
			EXPECT(w.Stowed(), "(setup) the weapon did not go to the back");
			w.MarkThrown();
			EXPECT(!w.Thrown(), "(thrown) a stowed weapon was marked thrown");
		}
		// stowed: taken by a hand behind a shoulder
		auto stowedHold = [&](hh::Hold& w) {
			dropped(w, st);
			lie(w, st, 3.0);
			EXPECT(w.Stowed(), "(setup) the weapon did not go to the back");
			w.SetStowPose({ { 0.0, 1.2, 0.30 }, QFromAxisAngle({ 0, 0, 1 }, 0.5) });   // on the back, as the mod draws it
			hh::HandIn z[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand({ -0.3, 1.0, -0.4 }, {}, 0) };   // (one step, the hands away: the body is then AT the back's pose)
			hh::HandOut zo[2];
			w.Step(z, kDt, st, zo);
		};
		{   // the game's (right) hand behind a shoulder
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.4, 0.25 }, {}, 0), Hand({ -0.3, 0.9, -0.2 }, {}, 0) };
			hh::HandOut out[2];
			in[0].behind = true;
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "a hand behind a shoulder with its grip open took the weapon");
			in[0].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying() && !w.Stowed(), "the grip pressed behind the shoulder did not take the stowed weapon");
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			EXPECT(w.Held() && out[0].holds && out[0].grabs == 0 && w.AsPlaced() && w.Main() == 0, "the right hand did not get the weapon as the game holds it");
			EXPECT(Len(Sub(w.BodyPose().pos, in[0].pose.pos)) < 0.05, "the weapon is %.2f m from the fist it flew into", Len(Sub(w.BodyPose().pos, in[0].pose.pos)));
		}
		{   // the OTHER hand behind its shoulder: the weapon flies into that hand, the fist closing on the shaft at the game's grip point
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.4, 0.25 }, {}, 0), Hand({ -0.28, 1.35, 0.22 }, {}, 0) };
			hh::HandOut out[2];
			in[1].behind = true;
			w.Step(in, kDt, st, out);
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "the left grip pressed behind the left shoulder did not take the stowed weapon");
				{   // it leaves from where it hung (no jump by the grip's turn): the body's frame is still at the back's pose, turn and place
					const Pose back { { 0.0, 1.2, 0.30 }, QFromAxisAngle({ 0, 0, 1 }, 0.5) };
					const Pose now = w.BodyPose();
					const Q    dq  = QMul(now.rot, QConj(back.rot));
					const double turned = 2.0 * std::acos(std::min(1.0, std::fabs(dq.w)));
					EXPECT(Len(Sub(now.pos, back.pos)) < 0.05 && turned < 0.12, "it left the back %.1f cm and %.1f deg off where it hung",
					       Len(Sub(now.pos, back.pos)) * 100, turned * 57.3);
				}
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			EXPECT(w.Held() && out[1].holds && out[1].grabs == 1 && !w.AsPlaced() && w.Main() == 1 && !out[0].holds,
			       "the left hand did not get the weapon (held %d holds %d grabs %u main %d)", w.Held(), out[1].holds, out[1].grabs, w.Main());
			// the fist's middle lies on the shaft, and the shaft along the fist
			const V3 fist = Add(in[1].pose.pos, Rotate(in[1].pose.rot, V3 { -st.fist.x, st.fist.y, st.fist.z }));
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 d = Mul(Sub(tip, butt), 1.0 / Len(Sub(tip, butt)));
			const V3 off = Sub(fist, butt);
			const double across = Len(Sub(off, Mul(d, Dot(off, d))));
			EXPECT(across < 0.01, "the left fist is %.1f cm off the shaft", across * 100);
			EXPECT(std::fabs(Dot(Rotate(in[1].pose.rot, kAlong), d)) > 0.99, "the shaft does not lie along the left fist");
		}
		{   // ... and a fist whose line runs ACROSS (the grip turns the weapon 90 degrees into it): it still leaves from where it hung
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.4, 0.25 }, {}, 0), Hand({ -0.28, 1.35, 0.22 }, {}, 0) };
			hh::HandOut out[2];
			in[1].behind   = true;
			in[1].fistLine = { 1, 0, 0 };
			w.Step(in, kDt, st, out);
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "(across) the left grip pressed behind the left shoulder did not take the stowed weapon");
			const Pose back { { 0.0, 1.2, 0.30 }, QFromAxisAngle({ 0, 0, 1 }, 0.5) };
			const Pose now = w.BodyPose();
			const Q    dq  = QMul(now.rot, QConj(back.rot));
			const double turned = 2.0 * std::acos(std::min(1.0, std::fabs(dq.w)));
			EXPECT(Len(Sub(now.pos, back.pos)) < 0.05 && turned < 0.12, "(across) it left the back %.1f cm and %.1f deg off where it hung",
			       Len(Sub(now.pos, back.pos)) * 100, turned * 57.3);
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 d = Mul(Sub(tip, butt), 1.0 / Len(Sub(tip, butt)));
			EXPECT(w.Held() && std::fabs(Dot(Rotate(in[1].pose.rot, V3 { 1, 0, 0 }), d)) > 0.99, "(across) the shaft does not lie along the left fist");
		}
		{   // a busy grip (a potion) takes nothing; a claimed one (the swap's) still takes the stowed weapon
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.4, 0.25 }, {}, 0), Hand({ -0.3, 0.9, -0.2 }, {}, 0) };
			hh::HandOut out[2];
			in[0].behind = true;
			in[0].busy   = true;
			w.Step(in, kDt, st, out);
			in[0].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "a hand with a potion took the stowed weapon");
			in[0].grip = 0;
			in[0].busy = false;
			w.Step(in, kDt, st, out);
			in[0].claimed = true;
			in[0].grip    = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "the swap's claim of the grip stopped it taking the stowed weapon");
		}
		{   // the game's hand pressed anywhere but behind a shoulder calls it back from the back; the other hand, there, takes nothing
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand({ -0.3, 1.0, -0.4 }, {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, st, out);
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "the other hand, away from the shoulder, called the weapon back");
			in[1].grip = 0;
			w.Step(in, kDt, st, out);
			in[0].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "the game's hand pressed away from the back did not call the weapon back");
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			EXPECT(w.Held() && out[0].holds && w.AsPlaced(), "the weapon called back from the back is not in the game's fist");
		}
		{   // recallAny: the grip of the OTHER free hand, anywhere, calls it into that hand; a busy hand or a claimed grip still does not
			hh::Settings any = st;
			any.recallAny    = true;
			hh::Hold w;
			stowedHold(w);
			hh::HandIn in[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand({ -0.3, 1.0, -0.4 }, {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, any, out);
			in[1].busy = true;
			in[1].grip = 1;
			w.Step(in, kDt, any, out);
			EXPECT(w.Stowed(), "(recallAny) a hand with a potion called the weapon back");
			in[1].grip = 0;
			in[1].busy = false;
			w.Step(in, kDt, any, out);
			in[1].claimed = true;
			in[1].grip    = 1;
			w.Step(in, kDt, any, out);
			EXPECT(w.Stowed(), "(recallAny) a claimed grip called the weapon back");
			in[1].grip    = 0;
			in[1].claimed = false;
			w.Step(in, kDt, any, out);
			in[1].grip = 1;
			w.Step(in, kDt, any, out);
			EXPECT(w.Flying(), "(recallAny) the free left hand's grip did not call the weapon back");
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, any, out);
			EXPECT(w.Held() && out[1].holds && !out[0].holds && w.Main() == 1, "(recallAny) the weapon did not come into the left hand");
		}
		{   // the take zone AT the weapon where it hangs (Settings::backTake): a fist near its drawn shaft takes it - anywhere along it, outside the shoulder zone too
			hh::Hold w;
			stowedHold(w);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 mid = Mul(Add(butt, tip), 0.5), axis = Mul(Sub(tip, butt), 1.0 / Len(Sub(tip, butt)));
			const V3 off = Cross(axis, V3 { 0, 1, 0 });   // sideways of the shaft
			const V3 side = Mul(off, 1.0 / std::max(1e-9, Len(off)));
			hh::HandIn in[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand({ -0.3, 1.0, -0.4 }, {}, 0) };
			hh::HandOut out[2];
			// StowDistance: 0 on the shaft, the distance past an end, to the segment's nearest point
			EXPECT(w.StowDistance(0, Hand(mid, {}, 0), st) < 1e-6, "(back) a fist on the shaft is %.3f m from it", w.StowDistance(0, Hand(mid, {}, 0), st));
			EXPECT(std::fabs(w.StowDistance(0, Hand(Add(tip, Mul(axis, 1.0)), {}, 0), st) - 1.0) < 1e-6, "(back) a fist a metre past the tip is not a metre from it");
			EXPECT(std::fabs(w.StowDistance(0, Hand(Add(mid, Mul(side, 0.30)), {}, 0), st) - 0.30) < 1e-6, "(back) a fist 30 cm beside the middle is not 30 cm from it");
			// the LEFT hand (not the game's) at the weapon, away from any shoulder: takes it into the left hand
			in[1].pose.pos = Add(mid, Mul(side, 0.10));
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "(back) a hand at the weapon with its grip open took it");
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "(back) the left grip at the weapon, outside the shoulder zone, did not take it");
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			EXPECT(w.Held() && out[1].holds && !out[0].holds && w.Main() == 1, "(back) it did not come into the left hand");
		}
		{   // 30 cm from the shaft is outside the 25 cm zone: nothing (and the left is not the game's hand); a claimed grip at the weapon still takes it; a busy hand does not
			hh::Hold w;
			stowedHold(w);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 mid = Mul(Add(butt, tip), 0.5), axis = Mul(Sub(tip, butt), 1.0 / Len(Sub(tip, butt)));
			const V3 off = Cross(axis, V3 { 0, 1, 0 });
			const V3 side = Mul(off, 1.0 / std::max(1e-9, Len(off)));
			hh::HandIn in[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand(Add(mid, Mul(side, 0.30)), {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, st, out);
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "(back) a fist 30 cm from the shaft took it (the zone is 25 cm)");
			in[1].grip = 0;
			w.Step(in, kDt, st, out);
			in[1].pose.pos = Add(mid, Mul(side, 0.10));
			in[1].busy = true;
			w.Step(in, kDt, st, out);
			in[1].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Stowed(), "(back) a hand with a potion took it at the weapon");
			in[1].grip = 0;
			in[1].busy = false;
			w.Step(in, kDt, st, out);
			in[1].claimed = true;   // the swap's claim on the grip: still a take at the weapon
			in[1].grip    = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "(back) the swap's claim stopped a take at the weapon");
		}
		{   // backTake 0: the shoulder alone (the old rule)
			hh::Hold w;
			hh::Settings shoulderOnly = st;
			shoulderOnly.backTake     = 0.0;
			stowedHold(w);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 mid = Mul(Add(butt, tip), 0.5);
			hh::HandIn in[2] = { Hand({ 0.25, 1.1, -0.4 }, {}, 0), Hand(mid, {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, shoulderOnly, out);
			in[1].grip = 1;
			w.Step(in, kDt, shoulderOnly, out);
			EXPECT(w.Stowed(), "(back) with backTake 0 a hand at the weapon took it outside the shoulder zone");
		}
		{   // the game's (right) hand at the weapon takes it AS PLACED (the game's grip), and either hand can
			hh::Hold w;
			stowedHold(w);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			const V3 mid = Mul(Add(butt, tip), 0.5);
			hh::HandIn in[2] = { Hand(mid, {}, 0), Hand({ -0.3, 1.0, -0.4 }, {}, 0) };
			hh::HandOut out[2];
			w.Step(in, kDt, st, out);
			in[0].grip = 1;
			w.Step(in, kDt, st, out);
			EXPECT(w.Flying(), "(back) the right grip at the weapon did not take it");
			for (double t = 0; t < 0.4; t += kDt) w.Step(in, kDt, st, out);
			EXPECT(w.Held() && out[0].holds && w.AsPlaced(), "(back) the right hand did not get it as the game holds it");
		}
		{   // a claimed grip takes nothing off the floor either (the swap's press behind a shoulder is not a pick-up)
			hh::Hold w;
			hh::Settings never = s;
			never.stowAfter    = -1;
			dropped(w, never);
			lie(w, never, 2.0);
			V3 butt, tip;
			w.Ends(&butt, &tip);
			hh::HandIn in[2] = { Hand(Add(Mul(Add(butt, tip), 0.5), { 0, 0.05, 0 }), {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			hh::HandOut out[2];
			in[0].claimed = true;
			w.Step(in, kDt, never, out);
			in[0].grip = 1;
			w.Step(in, kDt, never, out);
			EXPECT(!w.Held(), "a claimed grip took the weapon off the floor");
		}
	}
	// ---- The second hand slides along a long shaft (heft/slide.h) ----
	{   // the pure part: projection, clamp, smoothing, hand-off at the ends
		using heft::slide::In;
		using heft::slide::Out;
		using heft::slide::Step;
		In a;
		a.butt = -0.4; a.tip = 0.9; a.t = 0.2; a.raw = 0.5; a.dt = 0.02; a.tau = 0;
		Out o = Step(a);
		EXPECT(!o.release && std::fabs(o.t - 0.5) < 1e-12, "(slide) no smoothing: the grasp is the controller's projection (%.3f)", o.t);
		a.tau = 0.05;
		o     = Step(a);
		EXPECT(!o.release && std::fabs(o.t - (0.2 + 0.3 * (1.0 - std::exp(-0.4)))) < 1e-12, "(slide) smoothed: a share of the way (%.4f)", o.t);
		double t = 0.2;
		for (int i = 0; i < 100; ++i) { a.t = t; t = Step(a).t; }
		EXPECT(std::fabs(t - 0.5) < 1e-3, "(slide) it converges on the projection (%.4f)", t);
		a.t = 0.2; a.raw = 0.95; a.tau = 0;   // a little past the tip, within the margin: held at the tip
		o = Step(a);
		EXPECT(!o.release && std::fabs(o.t - 0.9) < 1e-12, "(slide) just past the tip: clamped to it (%.3f)", o.t);
		a.raw = -0.45;
		o = Step(a);
		EXPECT(!o.release && std::fabs(o.t + 0.4) < 1e-12, "(slide) just past the butt: clamped to it (%.3f)", o.t);
		a.raw = 0.9 + 0.13;   // off the tip by more than the margin: lets go
		o = Step(a);
		EXPECT(o.release, "(slide) off the tip: did not let go");
		a.raw = -0.4 - 0.13;
		EXPECT(Step(a).release, "(slide) off the butt: did not let go");
		a.raw = 0.4; a.perp = 0.26;   // pulled away from the axis by more than let_go
		EXPECT(Step(a).release, "(slide) pulled away: did not let go");
		a.perp = 0.24;
		EXPECT(!Step(a).release, "(slide) 24 cm from the axis: let go");
		a.raw = std::nan("");
		o = Step(a);
		EXPECT(!o.release && o.t == a.t, "(slide) no number: it stays");
		In bad;
		bad.butt = 1; bad.tip = 0.5; bad.raw = 0.7;
		EXPECT(!Step(bad).release && Step(bad).t == bad.t, "(slide) a shaft with no length: it stays");
	}
	{   // each long kind: the left hand slides with the controller while its grip is squeezed; the right one stays; off an end it lets go; with slide off, nothing moves
		struct K { hh::Kind kind; bool two; const char* name; };
		const K kinds[] = { { hh::Kind::Staff, false, "staff" },          { hh::Kind::Spear, false, "spear" },          { hh::Kind::Polearm, false, "polearm" },
		                    { hh::Kind::Sword, true, "two-handed sword" }, { hh::Kind::Axe, true, "two-handed axe" }, { hh::Kind::Mace, true, "two-handed mace" },
		                    { hh::Kind::Javelin, false, "javelin" } };
		for (const K& kd : kinds) {
			for (int on = 0; on < 2; ++on) {
				hh::Settings sl = s;
				sl.slide        = on != 0;
				Rig r;
				r.heldAt = 0;
				r.second = 0.30;
				const Pose p { { 0.15, 1.3, -0.4 }, {} };
				r.Start(kd.kind, kd.two, p, sl);
				for (double t = 0; t < 0.6; t += kDt) r.Step(p, sl);
				EXPECT(r.out[0].holds && r.out[1].holds, "(slide) %s: both hands did not hold it (%d %d)", kd.name, r.out[0].holds, r.out[1].holds);
				auto along = [&](int h) { return Dot(Sub(r.w.OnWeapon(h).pos, r.w.BodyPose().pos), Rotate(r.w.BodyPose().rot, r.w.ShaftLine().dir)); };
				const double l0 = along(1), r0 = along(0);
				const double to = r.second + 0.25;
				while (r.second < to) { r.second += 0.25 * kDt / 0.5; r.Step(p, sl); }   // the left controller moves 25 cm along the shaft in half a second
				for (double t = 0; t < 0.5; t += kDt) r.Step(p, sl);
				const double l1 = along(1), r1 = along(0);
				if (on) EXPECT(std::fabs((l1 - l0) - 0.25) < 0.04, "(slide) %s: the left grasp moved %.3f m for a 0.25 m slide of the controller", kd.name, l1 - l0);
				else EXPECT(std::fabs(l1 - l0) < 0.01, "(slide off) %s: the left grasp moved %.3f m", kd.name, l1 - l0);
				EXPECT(std::fabs(r1 - r0) < 0.005, "(slide) %s: the first hand's grasp moved %.3f m", kd.name, r1 - r0);
				if (!on) continue;   // (slide off: the old rule stays - the left pulled 25 cm off its place lets the placed right hand go; only the grasp is checked, above)
				EXPECT(r.out[0].holds && r.out[1].holds, "(slide on=%d) %s: a hand let go while sliding (%d %d)", on, kd.name, r.out[0].holds, r.out[1].holds);
				EXPECT(r.out[1].on >= 1.0, "(slide) %s: the left hand is not fully on the shaft", kd.name);
				// slides back: the grasp follows
				const double back = r.second - 0.25;
				while (r.second > back) { r.second -= 0.25 * kDt / 0.5; r.Step(p, sl); }
				for (double t = 0; t < 0.5; t += kDt) r.Step(p, sl);
				EXPECT(std::fabs(along(1) - l0) < 0.04, "(slide) %s: it did not slide back (%.3f from %.3f)", kd.name, along(1), l0);
				// off the tip: the left lets go, the right keeps it
				const double tipEnd = r.w.WeaponShape().tip;
				const double off    = tipEnd + 0.3;
				while (r.second < off) { r.second += 1.5 * kDt; r.Step(p, sl); }
				for (double t = 0; t < 0.3; t += kDt) r.Step(p, sl);
				EXPECT(!r.out[1].holds && r.out[0].holds && r.w.Held(), "(slide) %s: off the tip, the left did not let go and leave the right on it (%d %d)", kd.name,
				       r.out[1].holds, r.out[0].holds);
			}
		}
	}
	{   // a short weapon in two hands does not slide (the dagger): the grasp stays where it closed
		hh::Settings sl = s;
		sl.slide        = true;
		Rig r;
		r.heldAt = 0;
		r.second = 0.20;
		const Pose p { { 0.15, 1.3, -0.4 }, {} };
		r.Start(hh::Kind::Dagger, false, p, sl);
		for (double t = 0; t < 0.6; t += kDt) r.Step(p, sl);
		auto along = [&]() { return Dot(Sub(r.w.OnWeapon(1).pos, r.w.BodyPose().pos), Rotate(r.w.BodyPose().rot, r.w.ShaftLine().dir)); };
		const double l0 = along();
		for (int i = 0; i < 20; ++i) { r.second += 0.01; r.Step(p, sl); }
		for (double t = 0; t < 0.3; t += kDt) r.Step(p, sl);
		EXPECT(std::fabs(along() - l0) < 0.01, "(slide) a dagger's second hand slid %.3f m", along() - l0);
	}
// ---- A throw worked out by the weight model: the same swing, a light weapon and a heavy one (vr/weight_flight.h) ----
	{
		struct W { hh::Kind kind; bool two; const char* name; };
		const W weapons[] = { { hh::Kind::Throwing, false, "throwing knife" }, { hh::Kind::Javelin, false, "javelin" }, { hh::Kind::Sword, false, "sword" },
		                      { hh::Kind::Axe, true, "two-handed axe" }, { hh::Kind::Polearm, false, "polearm" } };
		double dist[5] = {}, peak[5] = {};
		for (int i = 0; i < 5; ++i) {
			hh::Hold w;
			hh::Settings set = s;
			hh::HandIn in[2] = { Hand({ 0.15, 1.4, -0.4 }, {}, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
			w.Place(0, weapons[i].kind, weapons[i].two, { {}, kAlong }, in, set);
			hh::HandOut out[2];
			in[0].grip = 1;
			for (double t = 0; t < 0.3; t += kDt) w.Step(in, kDt, set, out);
			// a throwing swing straight ahead: 6 m/s at the top of a half-sine of 0.35 s, let go near its end
			const double T = 0.35, vmax = 6.0;
			double best = 0;
			V3 bestVel;
			for (double t = 0; t < 0.85 * T; t += kDt) {
				in[0].pose.pos.z -= vmax * std::sin(kPi * t / T) * kDt;
				w.Step(in, kDt, set, out);
				if (Len(w.ComVel()) > best) { best = Len(w.ComVel()); bestVel = w.ComVel(); }
			}
			const V3 com = w.Com();
			peak[i] = best;
			weightflight::In fi;
			fi.pos[0] = com.x; fi.pos[1] = com.y; fi.pos[2] = com.z;
			fi.vel[0] = bestVel.x; fi.vel[1] = bestVel.y; fi.vel[2] = bestVel.z;
			const double kg = w.SteelKg();
			const weightflight::Out o = weightflight::SimulateThrow(fi, 100.0, 0.0, kg);
			dist[i] = o.valid ? std::sqrt((o.landing[0] - com.x) * (o.landing[0] - com.x) + (o.landing[2] - com.z) * (o.landing[2] - com.z)) : -1.0;   // the flight, from the release
			std::printf("throw, a 6 m/s swing: %-16s %.2f kg, the model's peak %.2f m/s, pushed x%.2f, lands %.2f m out (a real arm, reach 100 %%)\n", weapons[i].name, kg,
			            peak[i], o.massFactor, dist[i]);
		}
		EXPECT(peak[0] > 4.0 && dist[0] > 2.5, "(throw) a light knife on a 6 m/s swing did not leave fast (%.2f m/s, %.2f m)", peak[0], dist[0]);
		// (the model's own muscles make a heavy weapon lag and then overshoot - its peak is no lower; the weight is applied by the arm's push, weight_flight.h)
		EXPECT(dist[0] > dist[1] && dist[1] > dist[2] && dist[2] > dist[3] && dist[3] >= dist[4] - 0.3, "(throw) the reach does not fall with the weight: knife %.2f, javelin %.2f, sword %.2f, axe %.2f, polearm %.2f",
		       dist[0], dist[1], dist[2], dist[3], dist[4]);
		EXPECT(dist[4] < 0.75 * dist[1], "(throw) a polearm on the same swing as a javelin is not clearly shorter (%.2f vs %.2f m)", dist[4], dist[1]);
	}
	std::printf(g_fails ? "%d check(s) FAILED\n" : "all checks passed\n", g_fails);
	return g_fails ? 1 : 0;
}

}   // namespace

int main(int argc, char** argv) {
	if (argc >= 3 && std::strcmp(argv[1], "compare") == 0) return Compare(argv[2]);
	if (argc >= 2 && std::strcmp(argv[1], "hands") == 0) return Hands(argc - 2, argv + 2);
	if (argc >= 2 && std::strcmp(argv[1], "feel") == 0) return Feel(argc - 2, argv + 2);
	if (argc >= 2 && std::strcmp(argv[1], "gravity") == 0) {
		Gravity();
		std::printf(g_fails ? "%d check(s) FAILED\n" : "all checks passed\n", g_fails);
		return g_fails ? 1 : 0;
	}
	return Synthetic();
}
