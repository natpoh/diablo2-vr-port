// The bench's weapons and controller paths: see bench.h.

#include "bench.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace heft::bench {

namespace {

constexpr double kPi = 3.14159265358979323846;
double Deg(double d) { return d * kPi / 180.0; }

// Masses as Hurricane's tech demo has them where it has the weapon (broadsword 1.5,
// spear 1, sledgehammer 3.5); the rest as D2R's heavier kinds.
const Weapon kWeapons[] = {
	{ "dagger",
	  { { { 0, -0.09, 0 }, { 0, 0.04, 0 }, 0.015, 0.12 }, { { 0, 0.04, 0 }, { 0, 0.30, 0 }, 0.012, 0.28 } },
	  2, { 0, 0.30, 0 }, {}, false },
	{ "broadsword",
	  { { { 0, -0.14, 0 }, { 0, 0.05, 0 }, 0.016, 0.30 }, { { -0.09, 0.06, 0 }, { 0.09, 0.06, 0 }, 0.012, 0.12 },
	    { { 0, 0.07, 0 }, { 0, 0.92, 0 }, 0.02, 1.08 } },
	  3, { 0, 0.92, 0 }, {}, false },
	{ "twohand_sword",
	  { { { 0, -0.30, 0 }, { 0, 0.05, 0 }, 0.018, 0.55 }, { { -0.12, 0.06, 0 }, { 0.12, 0.06, 0 }, 0.014, 0.20 },
	    { { 0, 0.08, 0 }, { 0, 1.25, 0 }, 0.025, 2.25 } },
	  3, { 0, 1.25, 0 }, { 0, -0.22, 0 }, true },
	{ "spear",
	  { { { 0, -0.60, 0 }, { 0, 1.15, 0 }, 0.02, 0.82 }, { { 0, 1.10, 0 }, { 0, 1.32, 0 }, 0.03, 0.18 } },
	  2, { 0, 1.32, 0 }, { 0, 0.55, 0 }, true },
	{ "polearm",
	  { { { 0, -0.50, 0 }, { 0, 1.40, 0 }, 0.022, 1.90 }, { { 0, 1.25, 0 }, { 0, 1.60, 0 }, 0.06, 1.60 } },
	  2, { 0, 1.60, 0 }, { 0, 0.60, 0 }, true },
	{ "sledgehammer",
	  { { { 0, -0.10, 0 }, { 0, 0.62, 0 }, 0.018, 0.90 }, { { -0.12, 0.66, 0 }, { 0.12, 0.66, 0 }, 0.06, 2.60 } },
	  2, { 0, 0.72, 0 }, { 0, 0.40, 0 }, true },
};

const Path kPaths[] = {
	{ "hold", 3.0, false },   // held level and forward, still: how far the point sinks
	{ "step", 2.0, false },   // 0.4 m to the right in 50 ms: lag, overshoot, settling
	{ "swing", 2.0, false },  // a flat cut, 140 degrees about the shoulder in 0.3 s
	{ "chop", 2.0, false },   // from up high down to level in 0.3 s
	{ "flick", 1.5, false },  // the wrist alone, 90 degrees in 80 ms
	{ "hold2", 3.0, true },   // the same, two hands on
	{ "swing2", 2.0, true },
	{ "chop2", 2.0, true },
};

double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}
// From a to b across [t0, t0 + len].
double Ramp(double t, double t0, double len, double a, double b) { return a + (b - a) * Smooth((t - t0) / len); }

const V3 kShoulder { 0.20, 1.40, 0.0 };
Q Level() { return QFromAxisAngle({ 1, 0, 0 }, Deg(90)); }   // +Y onto +Z

Pose Main(int path, double t) {
	const std::string_view name = kPaths[path].name;
	if (name == "hold" || name == "hold2") return { { 0.15, 1.30, 0.40 }, Level() };
	if (name == "step") return { { 0.15 + Ramp(t, 0.5, 0.05, 0, 0.4), 1.30, 0.40 }, Level() };
	if (name == "flick") return { { 0.15, 1.30, 0.40 }, QMul(QFromAxisAngle({ 0, 1, 0 }, Deg(Ramp(t, 0.5, 0.08, 0, 90))), Level()) };
	if (name == "swing" || name == "swing2") {
		const Q yaw = QFromAxisAngle({ 0, 1, 0 }, Deg(Ramp(t, 0.5, 0.30, -70, 70)));
		return { Add(kShoulder, Rotate(yaw, { 0, -0.10, 0.55 })), QMul(yaw, Level()) };
	}
	// chop: pitch about the shoulder, from 80 degrees up to 15 down
	const Q pitch = QFromAxisAngle({ 1, 0, 0 }, Deg(Ramp(t, 0.5, 0.30, -80, 15)));
	return { Add(kShoulder, Rotate(pitch, { 0, 0, 0.55 })), QMul(pitch, Level()) };
}

}   // namespace

int           WeaponCount() { return (int)(sizeof(kWeapons) / sizeof(kWeapons[0])); }
const Weapon& WeaponAt(int i) { return kWeapons[std::clamp(i, 0, WeaponCount() - 1)]; }

Body WeaponOnly(const Weapon& w) { return Build(w.pieces, w.nPieces, nullptr, 0, 0); }
Body Held(const Weapon& w, bool twoHands) {
	const V3 hands[2] = { {}, w.second };
	return Build(w.pieces, w.nPieces, hands, twoHands ? 2 : 1, kHandMass);
}

Muscle OneHand() { return { 3000, 300, 300, 500, 50, 75 }; }       // HVR_DefaultHandStrength
Muscle TwoHandEach() { return { 3000, 300, 300, 200, 10, 50 }; }   // HVR_LargeWeapon_TwoHanded_Strength

int         PathCount() { return (int)(sizeof(kPaths) / sizeof(kPaths[0])); }
const Path& PathAt(int i) { return kPaths[std::clamp(i, 0, PathCount() - 1)]; }

Pose ControllerAt(int path, const Weapon& w, int hand, double t) {
	const Pose m = Main(std::clamp(path, 0, PathCount() - 1), t);
	if (hand == 0) return m;
	return { Add(m.pos, Rotate(m.rot, w.second)), m.rot };
}

}   // namespace heft::bench
