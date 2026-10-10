// The heft model (cleanroom/heft) on the bench's weapons and paths, without the
// game and without Unity.
//
//   heft_test                 every weapon on every path: a table, and the checks below
//   heft_test compare <dir>   the Unity bench's runs (<weapon>_<path>.csv, written by
//                             tools/heft_bench_unity): the PhysX reference against ours,
//                             the same numbers for both
//
// Checks: nothing blows up; a dagger sits in the fist; the heavier and the longer
// the weapon, the further it lags and sinks; two hands far apart hold it better
// than one; the substep does not change the answer; a lost weapon goes back to
// the fist.

#include "bench.h"
#include "heft.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
	std::printf(g_fails ? "%d check(s) FAILED\n" : "all checks passed\n", g_fails);
	return g_fails ? 1 : 0;
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

}   // namespace

int main(int argc, char** argv) {
	if (argc >= 3 && std::strcmp(argv[1], "compare") == 0) return Compare(argv[2]);
	return Synthetic();
}
