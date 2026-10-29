// The optional weight-physics library, end to end with a stand-in DLL (cleanroom/tests/weight_lib_standin.cpp): vr/weight_lib.cpp finds it, checks its version, puts it behind
// heft::hold::Engine; the hands (hold.h) use it for the held body when Settings::model is 1, and our own physics when there is none or it cannot take a step.
// No game, no headset. Run: build/Release/weight_lib_test.exe   (exit code 0 = all passed)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "d2r_vr_weight.h"
#include "hold.h"
#include "weight_lib.h"

namespace hh = heft::hold;
using namespace heft;

#ifndef STANDIN_GOOD
#error "STANDIN_GOOD / STANDIN_BAD: the stand-in DLLs' paths (CMake)"
#endif

namespace {

constexpr double kDt = 1.0 / 90.0;
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

std::wstring Wide(const char* s) {
	std::wstring w;
	while (*s) w += (wchar_t)(unsigned char)*s++;
	return w;
}

std::vector<std::string> g_log;
void Log(const char* text) { g_log.emplace_back(text); }
bool Logged(const char* part) {
	for (const std::string& l : g_log)
		if (l.find(part) != std::string::npos) return true;
	return false;
}

// a sword along -Z in the grip frame
const Piece kSword[2] = { { { 0, 0, 0.14 }, { 0, 0, -0.06 }, 0.016, 0.30 }, { { 0, 0, -0.06 }, { 0, 0, -0.90 }, 0.020, 0.90 } };

hh::HandIn Hand(V3 pos, Q rot, float grip) {
	hh::HandIn h;
	h.tracked = true;
	h.pose    = { pos, rot };
	h.grip    = grip;
	return h;
}

// The sword in the right hand, swung and stopped; where its tip is at each step.
std::vector<V3> Swing(const hh::Settings& s, int steps = 160) {
	hh::Hold w;
	const V3 shoulder { 0.20, 1.40, 0.0 };
	auto at = [&](double t) {
		const double u = std::clamp((t - 0.3) / 0.35, 0.0, 1.0), e = u * u * (3 - 2 * u);
		const Q yaw = QFromAxisAngle({ 0, 1, 0 }, (50 - 100 * e) * kPi / 180);
		return Pose { Add(shoulder, Rotate(yaw, { 0, -0.10, -0.55 })), yaw };
	};
	hh::HandIn in[2] = { Hand(at(0).pos, at(0).rot, 0), Hand({ -0.3, 1.0, 0.0 }, {}, 0) };
	w.Place(0, hh::Kind::Sword, false, { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	std::vector<V3> tips;
	for (int i = 0; i < steps; ++i) {
		in[0].pose = at(i * kDt);
		w.Step(in, kDt, s, out);
		V3 tip;
		w.Ends(nullptr, &tip);
		tips.push_back(tip);
	}
	return tips;
}

bool SameTips(const std::vector<V3>& a, const std::vector<V3>& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) return false;
	return true;
}
double Apart(const std::vector<V3>& a, const std::vector<V3>& b) {
	double worst = 0;
	for (size_t i = 0; i < a.size() && i < b.size(); ++i) worst = std::fmax(worst, Len(Sub(a[i], b[i])));
	return worst;
}

// Drives the engine and a reference side by side and returns how far apart the grip frames are at the worst.
struct Compare {
	double worstPos = 0, worstRot = 0;
	int snaps = 0;
};
Compare Drive(const hh::Engine& engine, unsigned flags, double handMassOfReference, bool fallback, bool jump) {
	hh::Settings s;
	s.modelFlags = flags;   // (the stand-in's own bits ride in the flags)
	s.model = 1;
	const V3 palm {};
	Body ours = Build(kSword, 2, &palm, 1, 0.5);
	ours.maxAngularSpeed = 25;
	Body ref = Build(kSword, 2, &palm, 1, handMassOfReference);
	ref.maxAngularSpeed = 25;
	Grip g;
	g.muscle = { 3000, 300, 300, 500, 50, 75, 0 };   // (the stand-in's own; ours is what the adapter falls back to)
	const Pose p0 { { 0.2, 1.3, 0.3 }, QFromAxisAngle({ 0, 1, 0 }, 0.3) };
	State st, rs;
	Place(st, ours, g, p0);
	Place(rs, ref, g, p0);
	World w;
	w.snapDistance = s.snap;
	Compare c;
	Pose last = p0;
	for (int i = 0; i < 200; ++i) {
		const double t = i * kDt;
		Pose p { Add(p0.pos, { 0.35 * std::sin(6 * t), 0.15 * std::sin(9 * t), 0.2 * std::sin(4 * t) }), QMul(QFromAxisAngle({ 0, 1, 0 }, 1.3 * std::sin(7 * t)), p0.rot) };
		if (jump && i == 100) p.pos = Add(p.pos, { 3, 0, 0 });
		Controller from { last, {}, {} }, to { p, {}, {} };
		to.vel    = Mul(Sub(p.pos, last.pos), 1.0 / kDt);
		to.angVel = Mul(RotVec(QMul(p.rot, QConj(last.rot))), 1.0 / kDt);
		from.vel    = i ? to.vel : V3 {};
		const bool snapped = engine.Step(st, ours, kSword, 2, false, &g, &from, &to, 1, kDt, s);
		const bool refSnapped = Step(rs, fallback ? ours : ref, &g, &from, &to, 1, kDt, w);
		if (fallback) { /* the reference is ours, in our own terms */ }
		const Pose a = GripPose(st, ours), b = GripPose(rs, fallback ? ours : ref);
		c.worstPos = std::fmax(c.worstPos, Len(Sub(a.pos, b.pos)));
		const Q dq = QMul(a.rot, QConj(b.rot));
		c.worstRot = std::fmax(c.worstRot, 2 * std::atan2(std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z), std::fabs(dq.w)));   // (acos loses 1e-8 near 1)
		if (snapped) ++c.snaps;
		if (snapped != refSnapped && !fallback) c.snaps += 1000;
		last = p;
	}
	return c;
}

}  // namespace

int main() {
	weightlib::SetLog(&Log);

	// ---- no library: asking for model 1 is our own physics, step for step ----
	{
		weightlib::SetPath(L"C:\\no\\such\\folder\\d2r_vr_weight.dll");
		EXPECT(!weightlib::Probe() && !weightlib::Ensure() && weightlib::GetStatus() == weightlib::Status::Missing && !weightlib::Loaded() && hh::GetEngine() == nullptr,
		       "a library that is not there is not used");
		EXPECT(Logged("there is no d2r_vr_weight.dll in d2rloader"), "the log says so");
		hh::Settings ours, asked;
		asked.model = 1;
		EXPECT(SameTips(Swing(ours), Swing(asked)), "no library: model 1 is our own, bit for bit");
	}
	// ---- a DLL that is no library (no exports), and one that speaks another version ----
	{
		wchar_t sys[MAX_PATH];
		GetSystemDirectoryW(sys, MAX_PATH);
		weightlib::SetPath((std::wstring(sys) + L"\\version.dll").c_str());
		EXPECT(!weightlib::Ensure() && weightlib::GetStatus() == weightlib::Status::BrokenExports && hh::GetEngine() == nullptr, "a DLL without the exports is not used");
		weightlib::SetPath(Wide(STANDIN_BAD).c_str());
		EXPECT(!weightlib::Ensure() && weightlib::GetStatus() == weightlib::Status::BadAbi && hh::GetEngine() == nullptr, "another interface version is not used");
		EXPECT(Logged("speaks interface version 99, the mod needs 1"), "the log says which version");
	}
	// ---- the stand-in library ----
	weightlib::SetPath(Wide(STANDIN_GOOD).c_str());
	EXPECT(weightlib::Probe(), "the stand-in is there");
	EXPECT(weightlib::Ensure() && weightlib::Loaded() && weightlib::GetStatus() == weightlib::Status::Loaded && hh::GetEngine() != nullptr, "the stand-in is loaded");
	EXPECT(std::strcmp(weightlib::Name(), "standin 0.1") == 0, "its name: '%s'", weightlib::Name());
	EXPECT(weightlib::Ensure(), "asked again: still in use");
	const hh::Engine* engine = hh::GetEngine();
	if (!engine) return 1;

	// the engine against a reference, in the grip frame
	{
		const Compare same = Drive(*engine, 0, 0.5, false, false);
		std::printf("  the engine against the same body stepped directly: %.2e m, %.2e rad\n", same.worstPos, same.worstRot);
		EXPECT(same.worstPos < 1e-9 && same.worstRot < 1e-9, "the adapter changes nothing when the bodies agree (%.2e m, %.2e rad)", same.worstPos, same.worstRot);
		// the library's body has other hands (2 kg: another centre of mass): the state goes through the grip frame and the weapon's pose is the library's
		const Compare other = Drive(*engine, 1u << 31, 2.0, false, false);
		std::printf("  with the library's body heavier than ours: %.2e m, %.2e rad\n", other.worstPos, other.worstRot);
		EXPECT(other.worstPos < 1e-9 && other.worstRot < 1e-9, "the grip frame follows the library's body, not ours (%.2e m, %.2e rad)", other.worstPos, other.worstRot);
		// it refuses the step: ours, in its place
		const Compare refused = Drive(*engine, 1u << 30, 0.5, true, false);
		EXPECT(refused.worstPos < 1e-9 && refused.worstRot < 1e-9, "a refused step is taken by our own physics (%.2e m)", refused.worstPos);
		EXPECT(Logged("D2rwStep refused a step"), "the log says it once");
		// far from its hand: put back in it
		const Compare jumped = Drive(*engine, 0, 0.5, false, true);
		EXPECT(jumped.snaps >= 1 && jumped.snaps < 1000 && jumped.worstPos < 1e-9, "put back in the hand: %d (%.2e m)", jumped.snaps, jumped.worstPos);
	}
	// the throw
	{
		// (the stand-in takes the mean of what is within 100 ms of now; a sample older than that is not in it)
		uint64_t t[5] = { 500, 1000, 1030, 1060, 1090 };
		V3 v[5] = { { 0, 0, -100 }, { 0, 0, -2 }, { 0, 0, -4 }, { 0, 0, -6 }, { 0, 0, -8 } };
		V3 out;
		EXPECT(engine->ThrowVelocity(t, v, 5, 1090, &out) && std::fabs(out.z + 5.0) < 1e-9, "the library's throw velocity comes through (%.3f)", out.z);
		EXPECT(!engine->ThrowVelocity(t, v, 5, 5000, &out), "no samples in its window: ours");
		EXPECT(!engine->ThrowVelocity(t, v, 0, 1090, &out), "no samples: ours");
	}
	// the hands with the library
	{
		hh::Settings ours, lib;
		lib.model = 1;
		const std::vector<V3> a = Swing(ours), b = Swing(lib), b2 = Swing(lib);
		EXPECT(SameTips(b, b2), "with the library the same swing gives the same weapon");
		const double apart = Apart(a, b);
		std::printf("  a swing with our physics and with the stand-in: the tips %.3f m apart at most\n", apart);
		EXPECT(apart > 0.002 && apart < 2.0, "the library moves the weapon (and keeps it near the hand): %.3f m", apart);
		for (const V3& v : b) EXPECT(std::isfinite(v.x + v.y + v.z), "finite");
		hh::Settings off = lib;
		off.model = 0;
		EXPECT(SameTips(a, Swing(off)), "model 0 is our own with the library loaded");
	}

	if (g_fails) {
		std::printf("weight_lib_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("weight_lib_test: missing, not a library, another version, the stand-in loaded, the adapter against a reference, a heavier library body, a refused step, a snap, the throw, the hands: all passed\n");
	return 0;
}
