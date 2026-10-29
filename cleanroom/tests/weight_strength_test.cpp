// The hero's strength in the weight model ([weight] hero_strength, vr/hero_strength.h) end to end through vr/weight.cpp, with real time for the steps and no skeleton, no game, no
// headset - in both models: ours, and the weight-physics library (a stand-in DLL that records what it is given: the steel's mass and the hands' strength).
//
//   weight_strength_test         (a few seconds: the steps are paced by the clock, as in the game)
//
// Checks: unticked, the weapon weighs what it did whatever the hero's Strength; ticked, the steel's mass and the hands' strength are scaled by the factors (a barbarian's lighter and
// stronger than a sorceress' with the same weapon), in our body and as the numbers the library is given; a Strength that cannot be read leaves everything; a requirement the game's
// tables do not give is a typical one for the kind; a level-up moves the weapon's mass live; one log line for each weapon and again for a real change; and on the hold's own, in
// fixed steps: the stronger hero's weapon sags less and trails the controller less - and factors of 1 are today's weight, to the last bit.

#include "../../vr/weight.h"
#include "../../vr/weight_lib.h"
#include "../../vr/hero_strength.h"
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
#include <string>
#include <vector>

#ifndef STANDIN_GOOD
#error "STANDIN_GOOD: the stand-in DLL's path (CMake)"
#endif

namespace hh = heft::hold;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                           \
	do {                                                         \
		if (!(c)) {                                              \
			++g_fails;                                           \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
			std::printf(__VA_ARGS__);                            \
			std::printf("\n");                                   \
		}                                                        \
	} while (0)

std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }
int CountLines(const char* part) {
	int n = 0;
	for (const std::string& l : g_log)
		if (l.find(part) != std::string::npos) ++n;
	return n;
}
std::wstring Wide(const char* s) {
	std::wstring w;
	while (*s) w += (wchar_t)(unsigned char)*s++;
	return w;
}
// what the stand-in library was given on its last step
bool Last(double* strength, double* steelKg) {
	HMODULE m = GetModuleHandleW(L"weight_lib_standin.dll");
	if (!m) return false;
	auto fn = (void (*)(double*, double*))GetProcAddress(m, "StandinLast");
	if (!fn) return false;
	fn(strength, steelKg);
	return true;
}

struct Rig {
	skel::Input in;
	weight::Out out;
	uint32_t sample = 0;
	Rig() {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		const float r[3] = {0.2f, -0.4f, -0.3f}, l[3] = {-0.2f, -0.4f, -0.3f};
		for (int k = 0; k < 3; ++k) { in.hand[0][k] = r[k]; in.hand[1][k] = l[k]; }
		skel::Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)hh::Kind::Sword;
		w.twoHanded = true;
		w.slot = 0;
		w.feel = 1.0f;
		w.strength = 1.0f;
		w.grabM = 0.12f;
		w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.tipAxis = 2;
		w.handsModel = 1;   // the library, when it is loaded
	}
	void Hero(int str, int req, float classPct, float effect = 100.0f) {
		in.weight.heroStrength = true;
		in.weight.heroStr = str;
		in.weight.reqStr = req;
		in.weight.strengthClassPct = classPct;
		in.weight.strengthEffect = effect;
	}
	void Step(int ms = 8) {
		Sleep(ms);
		in.sample = ++sample;
		skel::Input c = in;
		weight::Apply(&c, &out);
	}
	void Steps(int n) { while (n-- > 0) Step(); }
	void Off() {
		in.weight.on = false;
		Step();
		in.weight.on = true;
	}
};

// ---- the hold on its own, in fixed steps: how a weapon hangs and trails with the factors of two heroes ----
constexpr double kDt = 1.0 / 90.0;

hh::Settings SettingsFor(double steel, double hands) {
	hh::Settings s;
	s.weight   = 1.0 * steel;
	s.strength = std::max(0.05, 1.0 * hands);
	hh::UseGravityFeel(s);
	return s;
}

// A polearm in the right hand alone, held out level 3 s: how far the shaft sinks (degrees).
double Sag(const hh::Settings& s) {
	hh::Hold w;
	hh::HandIn in[2];
	in[0].tracked = true;
	in[0].pose    = { { 0.15, 1.3, -0.4 }, {} };
	in[1].pose    = { { -0.3, 1.0, 0.0 }, {} };
	in[0].grip = in[1].grip = 0.0f;
	w.Place(0, hh::Kind::Polearm, true, { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	for (double t = 0; t < 3.0; t += kDt) w.Step(in, kDt, s, out);
	const heft::V3 d = heft::Rotate(w.BodyPose().rot, w.ShaftLine().dir);
	return std::asin(std::clamp(-d.y / heft::Len(d), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
}

// The same polearm, the left hand closing on the shaft 0.3 m behind the right after 0.5 s, held out 3 s: the sag with both hands on it, and how many hands held it at the end.
double SagTwo(const hh::Settings& s, int* handsOn = nullptr) {
	hh::Hold w;
	hh::HandIn in[2];
	in[0].tracked = in[1].tracked = true;
	in[0].pose    = { { 0.15, 1.3, -0.4 }, {} };
	in[1].pose    = { { 0.15, 1.3, -0.1 }, {} };
	in[0].grip = in[1].grip = 0.0f;
	w.Place(0, hh::Kind::Polearm, true, { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	for (double t = 0; t < 3.0; t += kDt) {
		if (t >= 0.5) {   // the left hand onto the shaft where it is now, and its grip closed
			heft::V3 butt, tip;
			w.Ends(&butt, &tip);
			const heft::V3 d = heft::Sub(tip, butt);
			const double along = heft::Dot(heft::Sub(in[0].pose.pos, butt), d) / heft::Dot(d, d) - 0.3 / heft::Len(d);
			const double k = std::clamp(along, 0.0, 1.0);
			in[1].pose.pos = heft::Add(butt, heft::V3 { d.x * k, d.y * k, d.z * k });
			in[1].grip = 1.0f;
		}
		w.Step(in, kDt, s, out);
	}
	if (handsOn) *handsOn = (out[0].holds ? 1 : 0) + (out[1].holds ? 1 : 0);
	const heft::V3 d = heft::Rotate(w.BodyPose().rot, w.ShaftLine().dir);
	return std::asin(std::clamp(-d.y / heft::Len(d), -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
}

// A sword in one hand chopped 60 degrees in 0.4 s: how far the weapon trails the controller at the worst (degrees); the pose of the weapon at the end, for the bit-for-bit check.
double Lag(const hh::Settings& s, heft::V3* endTip = nullptr) {
	const double pi = 3.14159265358979323846;
	hh::Hold w;
	hh::HandIn in[2];
	in[0].tracked = true;
	in[1].pose = { { -0.3, 1.0, 0.0 }, {} };
	in[0].grip = in[1].grip = 0.0f;
	const heft::V3 shoulder { 0.20, 1.40, 0.0 };
	const auto at = [&](double t) {
		const double u = std::clamp((t - 1.0) / 0.4, 0.0, 1.0), e = u * u * (3 - 2 * u);
		const heft::Q pitch = heft::QFromAxisAngle({ 1, 0, 0 }, (30 - 60 * e) * pi / 180);
		return heft::Pose { heft::Add(shoulder, heft::Rotate(pitch, { 0, -0.10, -0.55 })), pitch };
	};
	in[0].pose = at(0);
	w.Place(0, hh::Kind::Sword, false, { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	double worst = 0;
	for (double t = 0; t < 2.6; t += kDt) {
		in[0].pose = at(t);
		w.Step(in, kDt, s, out);
		if (t >= 1.0 && t <= 1.4) {
			heft::V3 tip;
			w.Ends(nullptr, &tip);
			const heft::V3 d = heft::Rotate(w.BodyPose().rot, w.ShaftLine().dir);
			const heft::V3 want = heft::Rotate(in[0].pose.rot, { 0, 0, -1 });
			worst = std::fmax(worst, std::acos(std::clamp(heft::Dot(d, want) / (heft::Len(d) * heft::Len(want)), -1.0, 1.0)) * 180.0 / pi);
		}
	}
	if (endTip) w.Ends(nullptr, endTip);
	return worst;
}

}  // namespace

int main() {
	weight::SetLog(&LogLine);
	weight::SetLibrarySource(&weightlib::Ensure);
	weightlib::SetPath(Wide(STANDIN_GOOD).c_str());
	const double mass = hh::ShapeOf(hh::Kind::Sword, true).Mass();   // a two-handed sword, the steel's real kilograms; feel 1 = all of it

	// ---- unticked: the weapon weighs what it did, whatever the hero's Strength ----
	{
		g_log.clear();
		Rig r;
		r.in.weight.heroStr = 150;   // (the hero's numbers are in the input, the box is not ticked)
		r.in.weight.reqStr = 100;
		r.in.weight.strengthClassPct = 130.0f;
		r.Off();
		r.Steps(30);
		double str = 0, kg = 0;
		EXPECT(Last(&str, &kg), "the library was asked");
		std::printf("  unticked: the library is given %.3f kg and strength %.2f\n", kg, str);
		EXPECT(std::fabs(kg - mass) < 1e-9 && std::fabs(str - 1.0) < 1e-12, "the weapon weighs %.3f kg (%.3f)", mass, kg);
		weight::StrengthNote n;
		EXPECT(!weight::LastStrength(&n) && !n.on, "no strength was applied");
		EXPECT(CountLines("the hero's strength") == 0, "nothing said");
	}

	// ---- ticked: a barbarian and a sorceress with the same weapon ----
	double kgBarb = 0, kgSorc = 0, strBarb = 0, strSorc = 0;
	{
		g_log.clear();
		Rig r;
		r.Hero(150, 100, 130.0f);   // Str 150, a Great Sword's requirement 100, the barbarian's 130 %
		r.Off();
		r.Steps(30);
		herostr::In hi;
		hi.str = 150; hi.req = 100; hi.classPct = 130.0; hi.kind = (int)hh::Kind::Sword; hi.twoHanded = true;
		const herostr::Out want = herostr::Factor(hi);
		Last(&strBarb, &kgBarb);
		weight::StrengthNote n;
		EXPECT(weight::LastStrength(&n) && n.on && n.reqFromGame && n.str == 150 && n.req == 100, "the note: Str %d req %d", n.str, n.req);
		EXPECT(std::fabs(n.steel - want.steel) < 1e-5 && std::fabs(n.hands - want.hands) < 1e-5, "the factors are the maths' (%.3f %.3f)", n.steel, n.hands);
		std::printf("  barbarian, Str 150 x 130 %% against 100: steel x%.2f, hands x%.2f -> the library is given %.3f kg and strength %.2f\n", want.steel, want.hands, kgBarb, strBarb);
		EXPECT(std::fabs(kgBarb - mass * want.steel) < 1e-5 && std::fabs(strBarb - want.hands) < 1e-5, "the library is given the scaled numbers");
		EXPECT(kgBarb < mass && strBarb > 1.0, "lighter and stronger than today's");
		EXPECT(CountLines("the hero's strength: Str 150") == 1 && CountLines("(the game's table)") == 1, "one line for the weapon (%d)", CountLines("the hero's strength"));
		r.Steps(20);
		EXPECT(CountLines("the hero's strength") == 1, "and not again while nothing changes (%d)", CountLines("the hero's strength"));

		// the same hero levels up: a real change is told again, and the weapon's mass follows live
		r.Hero(200, 100, 130.0f);
		r.Steps(30);
		double s2 = 0, kg2 = 0;
		Last(&s2, &kg2);
		std::printf("  ... Str 200: %.3f kg, strength %.2f; lines %d\n", kg2, s2, CountLines("the hero's strength"));
		EXPECT(kg2 < kgBarb - 1e-6 && s2 > strBarb + 1e-6, "more Strength, lighter again, live");
		EXPECT(CountLines("the hero's strength") == 2, "a real change is told");
		// a point of Strength is not (3 %)
		r.Hero(201, 100, 130.0f);
		r.Steps(10);
		EXPECT(CountLines("the hero's strength") == 2, "a point is not worth a line");

		// "Strength matters" 0: nothing
		r.Hero(150, 100, 130.0f, 0.0f);
		r.Steps(30);
		double s3 = 0, kg3 = 0;
		Last(&s3, &kg3);
		EXPECT(std::fabs(kg3 - mass) < 1e-6 && std::fabs(s3 - 1.0) < 1e-9, "0 %%: today's weight (%.3f kg)", kg3);

		// the hands' strength slider multiplies on top
		r.Hero(150, 100, 130.0f);
		r.in.weight.strength = 2.0f;
		r.Steps(30);
		double s4 = 0, kg4 = 0;
		Last(&s4, &kg4);
		EXPECT(std::fabs(s4 - 2.0 * want.hands) < 1e-5, "Hands' strength 200 %% x the factor (%.3f)", s4);
	}
	{
		g_log.clear();
		Rig r;
		r.Hero(60, 100, 80.0f);   // a sorceress: Str 60 x 80 %
		r.Off();
		r.Steps(30);
		Last(&strSorc, &kgSorc);
		std::printf("  sorceress, Str 60 x 80 %% against 100: the library is given %.3f kg and strength %.2f\n", kgSorc, strSorc);
		EXPECT(kgSorc > kgBarb && strSorc < strBarb, "the same weapon is heavier and weaker in her hand");
		EXPECT(kgSorc > mass && strSorc < 1.0, "heavier than today's, in a weak hand");
	}

	// ---- a Strength that cannot be read: the weapon weighs as ever, said once ----
	{
		g_log.clear();
		Rig r;
		r.Hero(-1, 100, 130.0f);
		r.Off();
		r.Steps(30);
		double str = 0, kg = 0;
		Last(&str, &kg);
		weight::StrengthNote n;
		EXPECT(!weight::LastStrength(&n), "no strength applied");
		EXPECT(std::fabs(kg - mass) < 1e-9 && std::fabs(str - 1.0) < 1e-12, "as ever (%.3f kg)", kg);
		EXPECT(CountLines("was not read") == 1, "said once (%d)", CountLines("was not read"));
	}

	// ---- a requirement the game's tables do not give: a typical one for the kind ----
	{
		g_log.clear();
		Rig r;
		r.Hero(150, -1, 130.0f);
		r.Off();
		r.Steps(30);
		weight::StrengthNote n;
		EXPECT(weight::LastStrength(&n) && !n.reqFromGame && n.req == (int)herostr::NominalReq((int)hh::Kind::Sword, true), "a two-handed sword's typical %d", n.req);
		EXPECT(CountLines("typical for its kind") == 1, "said so");
	}

	// ---- another weapon: told again ----
	{
		g_log.clear();
		Rig r;
		r.Hero(150, 100, 130.0f);
		r.Off();
		r.Steps(20);
		r.in.weight.kind = (int)hh::Kind::Mace;
		r.in.weight.reqStr = 69;
		r.Steps(30);
		EXPECT(CountLines("the hero's strength") == 2, "a new weapon, a new line (%d)", CountLines("the hero's strength"));
	}

	// ---- ours, on the hold's own: the stronger hero's weapon sags less and trails less; factors of 1 are today's, to the last bit ----
	{
		herostr::In b, s;
		b.str = 150; b.req = 100; b.classPct = 130.0; b.kind = (int)hh::Kind::Polearm; b.twoHanded = true;
		s.str = 60;  s.req = 100; s.classPct = 80.0;  s.kind = (int)hh::Kind::Polearm; s.twoHanded = true;
		const herostr::Out fb = herostr::Factor(b), fs = herostr::Factor(s);
		const double sagToday = Sag(SettingsFor(1.0, 1.0)), sagBarb = Sag(SettingsFor(fb.steel, fb.hands)), sagSorc = Sag(SettingsFor(fs.steel, fs.hands));
		std::printf("  a polearm in one hand, held out level: sinks %.1f deg today, %.1f in a barbarian's hand (x%.2f / x%.2f), %.1f in a sorceress' (x%.2f / x%.2f)\n", sagToday, sagBarb, fb.steel, fb.hands,
		            sagSorc, fs.steel, fs.hands);
		EXPECT(sagBarb < sagToday && sagToday < sagSorc, "sag: barbarian < today < sorceress");
		heft::V3 tipA, tipB;
		const double lagToday = Lag(SettingsFor(1.0, 1.0), &tipA), lagBarb = Lag(SettingsFor(0.8, 1.25)), lagSorc = Lag(SettingsFor(1.3, 0.77));
		std::printf("  a sword chopped 60 deg in 0.4 s trails %.1f deg today, %.1f with the steel x0.8 and the hands x1.25, %.1f with x1.3 and x0.77\n", lagToday, lagBarb, lagSorc);
		EXPECT(lagBarb < lagToday && lagToday < lagSorc, "lag: stronger < today < weaker");
		Lag(SettingsFor(1.0, 1.0), &tipB);
		EXPECT(tipA.x == tipB.x && tipA.y == tipB.y && tipA.z == tipB.z, "factors of 1 are today's weight to the last bit");

		// [weight] two_hands_double: both hands on it, each holds twice as hard - it sags less than the same two hands without it
		hh::Settings shared = SettingsFor(1.0, 1.0), doubled = shared;
		doubled.twoHandsDouble = true;
		int onShared = 0, onDoubled = 0;
		const double sagShared = SagTwo(shared, &onShared), sagDoubled = SagTwo(doubled, &onDoubled);
		std::printf("  the polearm in both hands: sinks %.2f deg (%d hands), %.2f with two_hands_double (%d hands); one hand %.2f\n", sagShared, onShared, sagDoubled, onDoubled, sagToday);
		EXPECT(onShared == 2 && onDoubled == 2, "both hands hold it (%d %d)", onShared, onDoubled);
		EXPECT(sagDoubled < sagShared, "two_hands_double: both hands hold it harder (%.2f < %.2f)", sagDoubled, sagShared);
		const double oneDoubled = Sag(doubled);
		EXPECT(oneDoubled == sagToday, "two_hands_double changes nothing in one hand (%.4f %.4f)", oneDoubled, sagToday);
	}

	if (g_fails) {
		std::printf("weight_strength_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("weight_strength_test: unticked, the barbarian and the sorceress in both models, a level-up live, not read, a typical requirement, a new weapon, sag and lag: all passed\n");
	return 0;
}
