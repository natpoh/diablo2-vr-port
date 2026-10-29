// Our own physics of the held weapon and a weight-physics library's, face to face on the same motions (the hands of hold.h, the library through vr/weight_lib.cpp).
//
//   weight_lib_compare dll=<path to d2r_vr_weight.dll> [w=100]
//
// w: the steel's mass in % of the real one. For each kind: how far the shaft sinks held out level, a chop of 60 degrees in 0.4 s down and up and in 1 s (how far the weapon
// trails the controller, degrees), a hard cut of 140 degrees in 0.3 s (how far behind the tip is, cm), how long it takes to settle after the chop and how far past its end it swings.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hold.h"
#include "weight_lib.h"

namespace hh = heft::hold;
using namespace heft;

namespace {

constexpr double kDt = 1.0 / 90.0;
constexpr double kPi = 3.14159265358979323846;
const V3 kAlong { 0, 0, -1 };

hh::HandIn Hand(V3 pos, Q rot, float grip) {
	hh::HandIn h;
	h.tracked = true;
	h.pose    = { pos, rot };
	h.grip    = grip;
	return h;
}

enum class Mode { OursStiff, OursSoft, Library, LibraryPalm };
const char* ModeName(Mode m) {
	return m == Mode::OursStiff ? "ours, stiff wrists" : m == Mode::OursSoft ? "ours, gravity feel" : m == Mode::Library ? "library" : "library, palm";
}
hh::Settings Make(Mode m, double weight) {
	hh::Settings s;
	s.weight = weight;
	if (m == Mode::OursSoft) hh::UseGravityFeel(s);
	if (m == Mode::Library || m == Mode::LibraryPalm) s.model = 1;
	if (m == Mode::LibraryPalm) s.modelFlags = 1;
	return s;
}

double SagDeg(const hh::Hold& w) {
	const V3 d = Rotate(w.BodyPose().rot, w.ShaftLine().dir);
	return std::asin(std::clamp(-d.y / std::max(1e-9, Len(d)), -1.0, 1.0)) * 180.0 / kPi;
}
double ElevDeg(const hh::Hold& w) { return -SagDeg(w); }

struct Rig {
	hh::Hold   w;
	hh::HandIn in[2];
	hh::HandOut out[2];
	double     second = -99;
	Pose Left(const Pose& right) const { return { Add(right.pos, Rotate(right.rot, Mul(kAlong, second))), right.rot }; }
	void Start(hh::Kind k, bool twoKind, const Pose& right, const hh::Settings& s) {
		in[0] = Hand(right.pos, right.rot, 0);
		in[1] = Hand({ -0.3, 1.0, 0.0 }, {}, 0);
		w.Place(0, k, twoKind, { {}, kAlong }, in, s);
		if (second > -10) {
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

double SecondAt(hh::Kind k, bool two) {
	switch (k) {
	case hh::Kind::Staff: case hh::Kind::Polearm: case hh::Kind::Spear: return -0.50;
	case hh::Kind::Javelin: return -0.35;
	default: return two ? -0.20 : -0.10;
	}
}

double HoldLevel(hh::Kind k, bool two, const hh::Settings& s, double second = -99) {
	Rig r;
	r.second = second;
	const Pose p { { 0.15, 1.3, -0.4 }, {} };
	r.Start(k, two, p, s);
	for (double t = 0; t < 3.0; t += kDt) r.Step(p, s);
	return SagDeg(r.w);
}

double CutLag(hh::Kind k, bool two, const hh::Settings& s, double second = -99) {
	const V3 shoulder { 0.20, 1.40, 0.0 };
	auto at = [&](double t) {
		const double u = std::clamp((t - 0.3) / 0.3, 0.0, 1.0), e = u * u * (3 - 2 * u);
		const Q yaw = QFromAxisAngle({ 0, 1, 0 }, (70 - 140 * e) * kPi / 180);
		return Pose { Add(shoulder, Rotate(yaw, { 0, -0.10, -0.55 })), yaw };
	};
	Rig r;
	r.second = second;
	r.Start(k, two, at(0), s);
	double worst = 0;
	const double tip = hh::ShapeOf(k, two).tip;
	for (double t = 0; t < 1.5; t += kDt) {
		const Pose p = at(t);
		r.Step(p, s);
		V3 tipNow;
		r.w.Ends(nullptr, &tipNow);
		worst = std::fmax(worst, Len(Sub(tipNow, Add(p.pos, Rotate(p.rot, Mul(kAlong, tip))))));
	}
	return worst;
}

struct Pitch {
	double lag = 0, settle = 0, past = 0;
};
Pitch PitchRun(hh::Kind k, bool two, const hh::Settings& s, double from, double to, double len, double second = -99) {
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
	r.second = second;
	r.Start(k, two, at(0), s);
	std::vector<double> err;
	const int n = (int)std::lround((lead + len + tail) / kDt);
	for (int i = 0; i < n; ++i) {
		r.Step(at(i * kDt), s);
		err.push_back(ElevDeg(r.w) - ctl(i * kDt));
	}
	Pitch o;
	const double dir = to >= from ? 1.0 : -1.0, end = err.back();
	for (int i = 0; i < n; ++i) {
		const double t = i * kDt;
		if (t >= lead && t <= lead + len) o.lag = std::fmax(o.lag, -dir * err[i]);
		if (t > lead + len) {
			o.past = std::fmax(o.past, dir * (err[i] - end));
			if (std::fabs(err[i] - end) > 1.0) o.settle = t - (lead + len);
		}
	}
	return o;
}

// A jerk of the hand: 0.4 m to the side in 50 ms; how far the hand on the weapon is behind its controller at the worst, metres.
double JerkPalm(hh::Kind k, bool two, const hh::Settings& s) {
	Rig r;
	auto at = [&](double t) {
		const double u = std::clamp((t - 0.5) / 0.05, 0.0, 1.0), e = u * u * (3 - 2 * u);
		return Pose { { 0.15 + 0.4 * e, 1.30, 0.40 }, QFromAxisAngle({ 1, 0, 0 }, kPi / 2) };
	};
	r.Start(k, two, at(0), s);
	double worst = 0;
	for (double t = 0; t < 1.5; t += kDt) {
		const Pose p = at(t);
		r.Step(p, s);
		worst = std::fmax(worst, Len(Sub(r.w.OnWeapon(0).pos, p.pos)));
	}
	return worst;
}

}  // namespace

int main(int argc, char** argv) {
	std::wstring dll;
	double weight = 1.0;
	for (int i = 1; i < argc; ++i) {
		if (std::strncmp(argv[i], "dll=", 4) == 0) {
			for (const char* c = argv[i] + 4; *c; ++c) dll += (wchar_t)(unsigned char)*c;
		} else if (std::strncmp(argv[i], "w=", 2) == 0) {
			weight = std::atof(argv[i] + 2) * 0.01;
		}
	}
	weightlib::SetLog([](const char* t) { std::printf("[%s]\n", t); });
	if (!dll.empty()) weightlib::SetPath(dll.c_str());
	if (!weightlib::Ensure()) {
		std::printf("no weight-physics library to compare with (dll=<path> to d2r_vr_weight.dll)\n");
		return 2;
	}
	std::printf("%s; the steel at %.0f %% of the real mass; one hand unless said\n", weightlib::Name(), weight * 100);
	std::printf("%-10s %-4s %5s | %-19s | sag | chop 0.4 s: down up | slow 1 s: down up | cut cm | settle s | past deg | jerk: palm cm behind\n", "kind", "hand", "kg", "model");
	struct Row { hh::Kind k; bool two; bool both; const char* name; };
	const Row rows[] = { { hh::Kind::Dagger, false, false, "dagger" },   { hh::Kind::Sword, false, false, "sword" },
	                     { hh::Kind::Mace, false, false, "mace" },       { hh::Kind::Sword, true, false, "2h sword" },
	                     { hh::Kind::Sword, true, true, "2h sword" },    { hh::Kind::Staff, false, false, "staff" },
	                     { hh::Kind::Staff, false, true, "staff" },      { hh::Kind::Polearm, false, false, "polearm" },
	                     { hh::Kind::Polearm, false, true, "polearm" } };
	for (const Row& r : rows)
		for (const Mode m : { Mode::OursStiff, Mode::OursSoft, Mode::Library, Mode::LibraryPalm }) {
			const hh::Settings s = Make(m, weight);
			const double sec = r.both ? SecondAt(r.k, r.two) : -99;
			const Pitch d = PitchRun(r.k, r.two, s, 30, -30, 0.4, sec), u = PitchRun(r.k, r.two, s, -30, 30, 0.4, sec);
			const Pitch ds = PitchRun(r.k, r.two, s, 30, -30, 1.0, sec), us = PitchRun(r.k, r.two, s, -30, 30, 1.0, sec);
			std::printf("%-10s %-4s %5.2f | %-19s | %4.1f |          %5.1f %5.1f |        %5.1f %5.1f | %6.0f | %8.2f | %7.1f | %5.1f\n", r.name, r.both ? "two" : "one",
			            hh::ShapeOf(r.k, r.two).Mass() * weight, ModeName(m), HoldLevel(r.k, r.two, s, sec), d.lag, u.lag, ds.lag, us.lag, CutLag(r.k, r.two, s, sec) * 100,
			            d.settle, d.past, r.both ? 0.0 : JerkPalm(r.k, r.two, s) * 100);
		}
	return 0;
}
