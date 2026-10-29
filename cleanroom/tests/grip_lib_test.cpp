// The grip library end to end with a stand-in DLL (cleanroom/tests/grip_lib_standin.cpp): the loader (vr/grip_lib.cpp) finds it and checks its version, the hold (cleanroom/heft/hold.h)
// asks it which buttons hold a hand and how the second hand of a long shaft sits (tight, loose), and the skeleton hook ITSELF (vr/skeletons.cpp, included whole) on a toy rig with real
// hands - every bone with an axes of its own and the left hand the mirror image of the right with its axes RELABELLED - lays the hand and the fingers it answers with.
//
//   - the loader: no file, a DLL without the exports, another interface version, the stand-in;
//   - the hold: without a library [weight] grip_model 1 is our own rule step for step; with one the second hand is held by the grip or the trigger, tight where it is, loose where the
//     controller is, let go when both are; the first hand's trigger is the game's; the pulses come out;
//   - the hook: the library's fingers are laid on the bones whatever the rig's axes - echoed, they come back exactly as the hand had them (both hands, the mirror one too); half way
//     between open and closed they are half way; the palm is where the library put it; a library that refuses a step leaves the hand ours; no library: the game's pose as ever.
//
//   grip_lib_test        (about half a minute: the model's steps are paced by the clock, as in the game)

#include "toy_hand_rig.h"

#include "grip_lib.h"
#include "slide_kinds.h"

using namespace skel;
using namespace skel::tst;
namespace hh = heft::hold;

#ifndef GRIP_STANDIN_GOOD
#error "GRIP_STANDIN_GOOD / GRIP_STANDIN_BAD: the stand-in DLLs' paths (CMake)"
#endif

namespace {

std::vector<std::string> g_log;
void Log(const char* text) { g_log.emplace_back(text); }
bool Logged(const char* part) {
	for (const std::string& l : g_log)
		if (l.find(part) != std::string::npos) return true;
	return false;
}
std::wstring Wide(const char* s) {
	std::wstring w;
	while (*s) w += (wchar_t)(unsigned char)*s++;
	return w;
}
// the stand-in's own switch (not part of the interface): refuse every step / echo the fingers it is given
HMODULE StandinModule() {
	for (const wchar_t* name : {L"grip_lib_standin.dll", L"grip_lib_standin_v1.dll"})
		if (HMODULE m = GetModuleHandleW(name)) return m;
	return nullptr;
}
void StandinSet(int refuse, int echo) {
	if (HMODULE m = StandinModule())
		if (auto fn = (void (*)(int, int))GetProcAddress(m, "StandinSet")) fn(refuse, echo);
}
// what the stand-in's last HandStep was given (see grip_lib_standin.cpp): [0] struct size, [1] has-axis, [2..4] the vector from the palm to the axis, [5..7] the line, [8] palm depth,
// [9] the palm's distance from the axis it answered with
struct Last {
	double v[12] = {};
	bool ok = false;
};
Last StandinLast() {
	Last l;
	if (HMODULE m = StandinModule())
		if (auto fn = (void (*)(double*))GetProcAddress(m, "StandinLast")) { fn(l.v); l.ok = true; }
	return l;
}

// The hand `side` as the drawn pose has it (its wrist, the palm's middle - halfway to the middle knuckle - and the knuckle line, the way the hook reads them), and the drawn shaft: the
// weapon bone of the slot's hand, its second axis the tip's.
gripfit::Hand GripDrawnHand(const Rig& r, int side) {
	const auto P = [&](int b) { return gripfit::V3{r.mats[b].m[12], r.mats[b].m[13], r.mats[b].m[14]}; };
	gripfit::Hand h;
	const gripfit::V3 wrist = P(B(side, kHand)), mid = P(B(side, kMid)), ind = P(B(side, kInd)), pnk = P(B(side, kPnk));
	h.wrist = wrist;
	h.palm = gripfit::Mul(gripfit::Add(wrist, mid), 0.5f);
	h.point = gripfit::Sub(mid, wrist);
	h.knuckles = gripfit::Sub(ind, pnk);
	h.out = gripfit::Mul(gripfit::Cross(h.point, h.knuckles), side == 0 ? -1.0f : 1.0f);
	h.side = side;
	return h;
}
gripfit::Shaft DrawnShaft(const Rig& r, int slotSide) {
	const Mat& a = r.mats[B(slotSide, kAtt)];
	return {{a.m[12], a.m[13], a.m[14]}, {a.m[4], a.m[5], a.m[6]}};
}
// how far the palm's middle of `side` stands from the drawn shaft's axis, metres
double PalmOffAxis(const Rig& r, int side, int slotSide) {
	const gripfit::Hand h = GripDrawnHand(r, side);
	const gripfit::Shaft s = DrawnShaft(r, slotSide);
	gripfit::V3 d;
	gripfit::Unit(s.dir, &d);
	const gripfit::V3 rel = gripfit::Sub(h.palm, s.at);
	return gripfit::Len(gripfit::Sub(rel, gripfit::Mul(d, gripfit::Dot(rel, d)))) / kUpm;
}
// where the palm's middle of `side` stands along the drawn shaft (metres from the weapon bone's origin, toward the tip)
double PalmAlong(const Rig& r, int side, int slotSide) {
	const gripfit::Hand h = GripDrawnHand(r, side);
	const gripfit::Shaft s = DrawnShaft(r, slotSide);
	gripfit::V3 d;
	gripfit::Unit(s.dir, &d);
	return gripfit::Dot(gripfit::Sub(h.palm, s.at), d) / kUpm;
}
// The second hand's slide through the whole chain (weight::Apply with [weight] slide_second_hand_<kind>'s mask, the hook): the left controller 25 cm up the shaft, how far the left palm
// goes along the drawn shaft. library: the grip library's way, where the grip holds tight and only the trigger slides (so the hand is let go of its grip and held by the trigger).
struct SlideRun {
	double moved = 0.0;
	bool holds = false;
	int mode = 0;
};
SlideRun SlideThrough(const Weapon& weapon, uint32_t mask, bool library) {
	Rig r(0, false, true, weapon);
	r.in.weight.slideKinds = mask;
	r.in.weight.gripModel = library ? 1 : 0;
	r.Off();
	r.Steps(30);
	r.TakeSecond(1.0f);
	SlideRun out;
	if (!r.wo.holds[1]) return out;
	heft::V3 butt, tip;
	if (!r.Shaft(&butt, &tip)) return out;
	const heft::V3 dir = heft::Mul(heft::Sub(tip, butt), 1.0 / heft::Len(heft::Sub(tip, butt)));
	const double was = PalmAlong(r, 1, 0);
	if (library) {
		r.Grip(1, 0.0f);
		r.Trigger(1, 1.0f);
	}
	r.Hand(1, {r.in.hand[1][0] + dir.x * 0.25, r.in.hand[1][1] + dir.y * 0.25, r.in.hand[1][2] + dir.z * 0.25});
	r.Steps(40, 5);   // (the slide's smoothing is 50 ms of real time, as in the game)
	out.holds = r.wo.holds[1];
	out.mode = r.wo.gripMode[1];
	out.moved = PalmAlong(r, 1, 0) - was;
	return out;
}

// [weight] grip_hilt_only through the whole chain (weight::Apply with the tick, the hold, the hook): the left hand's grip pressed `frac` of the way from the weapon's butt to its tip, the right
// hand holding the weapon - does the left hand take it? hiltCm: the hilt's length of its own for this kind (0: the model's).
bool SecondTakes(const Weapon& weapon, bool hiltOnly, double frac, bool library, float hiltCm = 0.0f, int zoneKind = 0) {
	Rig r(0, false, true, weapon);
	r.in.weight.hiltOnly = hiltOnly;
	r.in.weight.hiltCm[zoneKind] = hiltCm;
	r.in.weight.gripModel = library ? 1 : 0;
	r.Off();
	r.Steps(30);
	heft::V3 butt, tip;
	if (!r.Shaft(&butt, &tip)) return false;
	r.Hand(1, heft::Add(butt, heft::Mul(heft::Sub(tip, butt), frac)));
	r.Steps(4);
	r.Grip(1, 1.0f);
	r.Steps(40);
	return r.wo.holds[1];
}

// the angle (degrees) between the knuckle line of `side` and the drawn shaft
double KnuckleLean(const Rig& r, int side, int slotSide) {
	const gripfit::Hand h = GripDrawnHand(r, side);
	const gripfit::Shaft s = DrawnShaft(r, slotSide);
	gripfit::V3 d, k;
	gripfit::Unit(s.dir, &d);
	gripfit::Unit(h.knuckles, &k);
	return std::acos(std::clamp(std::fabs(gripfit::Dot(d, k)), 0.0f, 1.0f)) * 57.2957795;
}

// ---- the hold on its own: a staff, the right hand first, the left taking it ----
struct HoldRig {
	hh::Hold w;
	hh::Settings s;
	hh::HandIn in[2];
	hh::HandOut out[2];
	HoldRig(int gripModel, bool slide = true) {
		s.slide     = slide;
		s.gripModel = gripModel;
		in[0].tracked = in[1].tracked = true;
		in[0].pose  = { { 0.20, 1.30, -0.40 }, {} };
		in[1].pose  = { { -0.20, 1.30, -0.90 }, {} };
		in[0].grip = in[1].grip = 0.0f;
		w.Place(0, hh::Kind::Staff, true, { {}, { 0, 0, -1 } }, in, s);
	}
	void Step(int n = 1) {
		for (int i = 0; i < n; ++i) w.Step(in, 1.0 / 90.0, s, out);
	}
	void TakeSecond() {   // the left fist 50 cm up the shaft, the grip squeezed (a press is a rise: let go first)
		in[1].pose.pos = { 0.20, 1.30, -0.90 };
		in[1].grip = 0.0f;
		Step(3);
		in[1].grip = 1.0f;
		Step(30);
	}
	// where the left hand holds the shaft, metres from the butt
	double Along(int hand) const {
		heft::V3 butt, tip;
		w.Ends(&butt, &tip);
		const heft::V3 span = heft::Sub(tip, butt);
		const heft::V3 d = heft::Mul(span, 1.0 / heft::Len(span));
		return heft::Dot(heft::Sub(w.OnWeapon(hand).pos, butt), d);
	}
};

void HoldChecks(bool library) {
	// ---- the second hand sits tight where it closed: the controller moving along the shaft moves nothing ----
	{
		HoldRig r(1);
		r.Step(30);
		r.TakeSecond();
		CHECK(r.out[0].holds && r.out[1].holds);
		if (library) CHECK(r.out[1].mode == 1);
		const double was = r.Along(1);
		r.in[1].pose.pos.z -= 0.20;
		r.Step(40);
		const double moved = r.Along(1) - was;
		std::printf("  %s: the left controller moves 20 cm up the shaft: the grasp point moves %.1f cm\n", library ? "library tight" : "no library (ours)", moved * 100.0);
		if (library) CHECK(std::fabs(moved) < 0.001);
		else CHECK(moved > 0.15);   // ours: it slides with the controller while it is squeezed
		CHECK(r.out[1].holds);
	}
	// ---- no slide for this kind of weapon ([weight] slide_second_hand_<kind> unticked: a sword's second hand): it holds where it closed, whichever way the hold is made ----
	{
		HoldRig r(library ? 1 : 0, false);
		r.Step(30);
		r.TakeSecond();
		CHECK(r.out[0].holds && r.out[1].holds);
		const double was = r.Along(1);
		r.in[1].pose.pos.z -= 0.20;
		r.Step(40);
		const double moved = r.Along(1) - was;
		std::printf("  %s, no slide for this kind: the left controller moves 20 cm up the shaft: the grasp point moves %.2f cm\n", library ? "library" : "no library (ours)", moved * 100.0);
		CHECK(std::fabs(moved) < 0.001);
		CHECK(r.out[1].holds && (!library || r.out[1].mode == 1));
		// the grip let go, the trigger squeezed: the trigger alone does not hold a hand that does not slide - it lets go
		r.in[1].pose.pos.z += 0.20;
		r.Step(5);
		r.in[1].grip = 0.0f;
		r.in[1].trigger = 1.0f;
		r.Step(10);
		CHECK(!r.out[1].holds && r.out[0].holds);
	}
	if (!library) return;
	// ---- trigger alone: loose, held, the point goes to the controller ----
	{
		HoldRig r(1);
		r.Step(30);
		r.TakeSecond();
		const double was = r.Along(1);
		r.in[1].grip = 0.0f;
		r.in[1].trigger = 1.0f;
		r.in[1].pose.pos.z -= 0.20;
		r.Step(40);
		const double moved = r.Along(1) - was;
		std::printf("  library loose (grip let go, trigger squeezed): still holds, the grasp point moves %.1f cm with the controller\n", moved * 100.0);
		CHECK(r.out[1].holds && r.out[1].mode == 2);
		CHECK(moved > 0.15 && moved < 0.25);
		CHECK(std::fabs(r.out[1].roll) < 0.2);   // (the controller did not turn)
		// the grip again: tight where it is
		r.in[1].grip = 1.0f;
		r.Step(5);
		const double at = r.Along(1);
		r.in[1].pose.pos.z -= 0.10;
		r.Step(30);
		CHECK(r.out[1].mode == 1 && std::fabs(r.Along(1) - at) < 0.001);
		// both let go
		r.in[1].grip = 0.0f;
		r.in[1].trigger = 0.0f;
		r.Step(5);
		CHECK(!r.out[1].holds);
	}
	// ---- the first hand is held by its grip alone: its trigger is the game's ----
	{
		HoldRig r(1);
		r.Step(30);
		r.TakeSecond();
		CHECK(r.out[0].holds);
		r.in[0].grip = 1.0f;
		r.Step(3);
		r.in[0].grip = 0.0f;
		r.in[0].trigger = 1.0f;
		r.Step(10);
		CHECK(!r.out[0].holds && r.out[1].holds);
	}
	// ---- the pulses ----
	{
		HoldRig r(1);
		uint32_t placed = 0;   // the hand the weapon was placed in: no pulse for the first closing
		for (int i = 0; i < 5; ++i) { r.Step(); placed |= r.out[0].events; }
		CHECK((placed & 2u) == 0);
		r.Step(25);
		bool grab = false, release = false, hover = false;
		r.in[1].pose.pos = { 0.20, 1.30, -0.90 };
		r.in[1].grip = 0.0f;
		for (int i = 0; i < 5; ++i) { r.Step(); hover = hover || (r.out[1].events & 1u); }
		r.in[1].grip = 1.0f;
		for (int i = 0; i < 30; ++i) { r.Step(); grab = grab || (r.out[1].events & 2u); }
		r.in[1].grip = 0.0f;
		for (int i = 0; i < 5; ++i) { r.Step(); release = release || (r.out[1].events & 4u); }
		std::printf("  pulses: a hand coming within reach %s, closing %s, letting go %s\n", hover ? "yes" : "no", grab ? "yes" : "no", release ? "yes" : "no");
		CHECK(hover && grab && release);
	}
}

// ---- no library: model 1 is ours, step for step ----
void HoldNoLibrary() {
	hh::SetGripEngine(nullptr);
	HoldRig a(0), b(1);
	double worst = 0.0;
	for (int phase = 0; phase < 4; ++phase) {
		a.Step(30); b.Step(30);
		if (phase == 0) { a.TakeSecond(); b.TakeSecond(); }
		if (phase == 1) { a.in[1].pose.pos.z -= 0.2; b.in[1].pose.pos.z -= 0.2; a.in[1].trigger = b.in[1].trigger = 1.0f; }
		if (phase == 2) { a.in[1].grip = b.in[1].grip = 0.0f; }
		for (int h = 0; h < 2; ++h) {
			worst = std::max(worst, heft::Len(heft::Sub(a.out[h].pose.pos, b.out[h].pose.pos)));
			CHECK(a.out[h].holds == b.out[h].holds && b.out[h].mode == 0 && b.out[h].events == 0);
		}
	}
	std::printf("  no grip library: [weight] grip_model 1 differs from 0 by %.2e m\n", worst);
	CHECK(worst == 0.0);
}

}  // namespace

// A library of interface version 1 (an older d2r_vr_grip.dll): the loader takes it, and the hook gives it what version 1 had - no version-2 fields, no lean of the line - so the hand is laid
// the version-1 way (the stand-in puts the palm 3.5 cm from the axis). Run as `grip_lib_test v1` (a library, once loaded, stays loaded in a process).
int RunV1(const Weapon& staff) {
#ifdef GRIP_STANDIN_V1
	griplib::SetPath(Wide(GRIP_STANDIN_V1).c_str());
	CHECK(griplib::Probe() && griplib::Ensure() && griplib::Loaded());
	CHECK(griplib::Abi() == 1);
	CHECK(Logged("speaks interface version 1: the hand is laid on the wood by its palm's surface"));
	std::printf("  a library of interface version 1: loaded, speaks %d\n", griplib::Abi());
	for (int slotSide = 0; slotSide < 2; ++slotSide) {
		const int o = slotSide ^ 1;
		StandinSet(0, 0);
		Rig r(slotSide, false, true, staff);
		r.in.weight.gripModel = 1;
		r.Off();
		r.Steps(30);
		r.TakeSecond(1.0f);
		const Last last = StandinLast();
		CHECK(last.ok && r.wo.gripLib && r.wo.holds[o]);
		std::printf("  version 1, the %s hand takes it: given hasAxis %.0f, the vector (%.3f %.3f %.3f), the line (%.2f %.2f %.2f), palm depth %.1f cm; the palm's middle %.1f cm from the axis\n", SideName(o), last.v[1], last.v[2],
		            last.v[3], last.v[4], last.v[5], last.v[6], last.v[7], last.v[8] * 100.0, PalmOffAxis(r, o, slotSide) * 100.0);
		CHECK(last.v[1] == 0.0 && last.v[2] == 0.0 && last.v[3] == 0.0 && last.v[4] == 0.0);
		CHECK(last.v[5] == 0.0 && last.v[6] == 0.0 && last.v[7] == 0.0);
		CHECK(std::fabs(PalmOffAxis(r, o, slotSide) - 0.035) < 0.006);
		CHECK(r.bad == 0);
	}
#else
	(void)staff;
	CHECK(false);
#endif
	std::printf(g_fails == 0 ? "grip_lib_test v1: all passed\n" : "grip_lib_test v1: %d failed\n", skel::tst::g_fails);
	return skel::tst::g_fails == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
	timeBeginPeriod(1);
	std::printf("grip_lib_test (the grip library with a stand-in DLL: the loader, the hold, the hook on a toy rig)\n");
	griplib::SetLog(&Log);
	weight::SetGripLibrarySource(&griplib::Ensure);
	skel::SetGripHandStep(&griplib::HandStep, &griplib::Abi);

	const Weapon staff{"a two-handed staff", D2RVR_TYPE_STAFF, heft::hold::Kind::Staff};
	if (argc > 1 && std::strcmp(argv[1], "v1") == 0) return RunV1(staff);

	// ---- the loader ----
	{
		griplib::SetPath(L"C:\\no\\such\\folder\\d2r_vr_grip.dll");
		CHECK(!griplib::Probe() && !griplib::Ensure() && griplib::GetStatus() == griplib::Status::Missing && !griplib::Loaded() && hh::GetGripEngine() == nullptr);
		CHECK(Logged("there is no d2r_vr_grip.dll in d2rloader"));
		wchar_t sys[MAX_PATH];
		GetSystemDirectoryW(sys, MAX_PATH);
		griplib::SetPath((std::wstring(sys) + L"\\version.dll").c_str());
		CHECK(!griplib::Ensure() && griplib::GetStatus() == griplib::Status::BrokenExports && hh::GetGripEngine() == nullptr);
		griplib::SetPath(Wide(GRIP_STANDIN_BAD).c_str());
		CHECK(!griplib::Ensure() && griplib::GetStatus() == griplib::Status::BadAbi && hh::GetGripEngine() == nullptr);
		CHECK(Logged("speaks interface version 99, the mod needs 1 to 2"));
		std::printf("  the loader: missing, a DLL without the exports, another version: none is used\n");
	}

	// ---- no library: grip_model 1 is our own way ----
	HoldNoLibrary();
	HoldChecks(false);
	{
		Rig r(0, false, true, staff);
		r.in.weight.gripModel = 1;
		r.Off();
		r.Steps(30);
		const Coords want = HandCoords(r.animStand, 0);
		r.TakeSecond(1.0f);
		CHECK(!r.wo.gripLib);
		std::printf("  no library: the taking hand has the game's pose, %.2f mm; %ld bad\n", Dist(r.Mine(1), want) / kUpm * 1000.0, r.bad);
		CHECK(Dist(r.Mine(1), want) < 0.004);
		CHECK(r.bad == 0);
	}

	// ---- the stand-in ----
	griplib::SetPath(Wide(GRIP_STANDIN_GOOD).c_str());
	CHECK(griplib::Probe());
	CHECK(griplib::Ensure() && griplib::Loaded() && griplib::GetStatus() == griplib::Status::Loaded && hh::GetGripEngine() != nullptr);
	CHECK(std::strcmp(griplib::Name(), "grip standin 0.1") == 0);
	HoldChecks(true);

	for (int slotSide = 0; slotSide < 2; ++slotSide) {
		const int o = slotSide ^ 1;
		// echo: the library gives back the fingers it was given - they come back as the hand had them, whichever hand and whatever the rig's axes
		{
			StandinSet(0, 1);
			Rig r(slotSide, false, true, staff);
			r.in.weight.gripModel = 1;
			r.Off();
			r.Steps(30);
			const Coords want = HandCoords(r.animStand, slotSide);
			const Coords openTaker = HandCoords(r.animOpen, o);
			r.TakeSecond(1.0f);
			CHECK(r.wo.gripLib && r.wo.holds[o] && r.wo.gripMode[o] == 1);
			const double dTaker = Dist(r.Mine(o), openTaker), dSlot = Dist(r.Mine(slotSide), want);
			std::printf("  echo, the %s hand takes it from the %s slot: its fingers %.3f mm from where the hand had them, the slot hand's %.3f mm\n", SideName(o), SideName(slotSide),
			            dTaker / kUpm * 1000.0, dSlot / kUpm * 1000.0);
			CHECK(dTaker < 0.004);
			CHECK(dSlot < 0.004);
			CHECK(r.bad == 0);
			if (r.bad) std::printf("      first bad: %s\n", r.firstBad.c_str());
		}
		// half way: the fingers are between the open hand and the game's pose
		{
			StandinSet(0, 0);
			Rig r(slotSide, false, true, staff);
			r.in.weight.gripModel = 1;
			r.Off();
			r.Steps(30);
			const Coords want = HandCoords(r.animStand, slotSide);
			const Coords openTaker = HandCoords(r.animOpen, o);
			r.TakeSecond(1.0f);
			const Coords mine = r.Mine(o);
			const double toOpen = Dist(mine, openTaker), toWant = Dist(mine, want), whole = Dist(want, openTaker);
			std::printf("  half way, the %s hand: %.2f of the way from the open hand to the game's pose (%.1f mm from one, %.1f from the other, of %.1f)\n", SideName(o),
			            toOpen / (toOpen + toWant), toOpen / kUpm * 1000.0, toWant / kUpm * 1000.0, whole / kUpm * 1000.0);
			CHECK(toOpen / (toOpen + toWant) > 0.25 && toOpen / (toOpen + toWant) < 0.75);
			CHECK(toOpen > 0.05 && toWant > 0.05);
			CHECK(r.bad == 0);
			// the palm where the library put it: the vector the hook gave it (the base hand's, as the game's grip has the shaft) reaches the drawn shaft's axis, so the palm's middle stands as far
			// from the axis as that vector is long
			const gripfit::AxisInHand base = gripfit::AxisOf(GripDrawnHand(r, slotSide), DrawnShaft(r, slotSide), (float)kUpm);
			const double off = PalmOffAxis(r, o, slotSide), want3 = gripfit::Len(base.toAxis);
			std::printf("      the palm's middle %.1f cm from the drawn shaft's axis (the base hand's grip has the axis %.1f cm from its palm)\n", off * 100.0, want3 * 100.0);
			CHECK(base.ok && want3 > 0.005);
			CHECK(std::fabs(off - want3) < 0.006);
		}
	}

	// ---- the shaft against the hand (interface version 2): the hook gives the library how the game's own grip lies on the shaft, and the library lays the hand that way ----
	CHECK(griplib::Abi() == 2);
	for (int slotSide = 0; slotSide < 2; ++slotSide) {
		const int o = slotSide ^ 1;
		StandinSet(0, 0);
		// ours, the grip fit with the base's relation: where the taking hand's palm stands
		Rig ours(slotSide, false, true, staff);
		ours.in.weight.gripModel = 0;
		ours.Off();
		ours.Steps(30);
		ours.TakeSecond(1.0f);
		// the library's
		Rig lib(slotSide, false, true, staff);
		lib.in.weight.gripModel = 1;
		lib.Off();
		lib.Steps(30);
		const gripfit::AxisInHand base = gripfit::AxisOf(GripDrawnHand(lib, slotSide), DrawnShaft(lib, slotSide), (float)kUpm);
		lib.TakeSecond(1.0f);
		const Last last = StandinLast();
		CHECK(last.ok && lib.wo.gripLib && lib.wo.holds[o]);
		// what the hook gave: in the library's proper frame, the base hand's numbers (the second axis turned over on a right hand)
		const double ys = o == 0 ? -1.0 : 1.0;
		const double dAxis = std::fabs(last.v[2] - base.toAxis.x) + std::fabs(last.v[3] - ys * base.toAxis.y) + std::fabs(last.v[4] - base.toAxis.z);
		const double dLine = std::fabs(last.v[5] - base.line.x) + std::fabs(last.v[6] - ys * base.line.y) + std::fabs(last.v[7] - base.line.z);
		std::printf("  version 2, the %s hand takes it: given hasAxis %.0f, the vector (%.3f %.3f %.3f) m and the line (%.2f %.2f %.2f); the base hand's own: (%.3f %.3f %.3f) (%.2f %.2f %.2f), %.1f mm and %.2f apart\n", SideName(o), last.v[1],
		            last.v[2], last.v[3], last.v[4], last.v[5], last.v[6], last.v[7], base.toAxis.x, ys * base.toAxis.y, base.toAxis.z, base.line.x, ys * base.line.y, base.line.z, dAxis * 1000.0, dLine);
		CHECK(last.v[1] == 1.0 && last.v[0] >= (double)sizeof(D2rgHandIn));
		CHECK(base.ok && dAxis < 0.004 && dLine < 0.02);
		CHECK(gripfit::Len(base.toAxis) > 0.005);   // (the toy hand's grip is not a palm on the axis: the vector is worth carrying)
		// the palm where ours puts it: the grip fit and the library lay the hand on the shaft the same way (palm and axis, the lean)
		const double offLib = PalmOffAxis(lib, o, slotSide), offOurs = PalmOffAxis(ours, o, slotSide);
		const double apart = gripfit::Len(gripfit::Sub(GripDrawnHand(lib, o).palm, GripDrawnHand(ours, o).palm)) / kUpm;
		const double leanLib = KnuckleLean(lib, o, slotSide), leanOurs = KnuckleLean(ours, o, slotSide);
		std::printf("      the palm's middle %.1f cm from the axis (ours %.1f cm), %.1f mm from where ours puts it; the knuckles %.0f deg off the shaft (ours %.0f)\n", offLib * 100.0, offOurs * 100.0,
		            apart * 1000.0, leanLib, leanOurs);
		CHECK(std::fabs(offLib - offOurs) < 0.008);
		CHECK(apart < 0.015);
		CHECK(std::fabs(leanLib - leanOurs) < 6.0);
		CHECK(lib.bad == 0 && ours.bad == 0);

		// The user's corrections (right_grip_* / left_grip_*) are the same keys with the library, in the same frame with the same signs: along the shaft, across, up, the roll and the two
		// tilts, on either hand - the base one and the one that took the weapon. Moves (X, Y, Z) put the hand exactly where ours puts it; turns orbit the hand about the shaft's axis (as ours
		// does for the base hand), where ours turns a hand that took the weapon in place about the palm's middle - so the palms differ by at most the palm's lever (its distance from the
		// axis) times the angles.
		const auto pair = [&](const float (&fix)[2][6], double* apartMm, double* movedCm) {
			Rig a2(slotSide, false, true, staff), b2(slotSide, false, true, staff);
			for (int side = 0; side < 2; ++side)
				for (int i = 0; i < 6; ++i) { a2.in.weight.gripHand[side][i] = fix[side][i]; b2.in.weight.gripHand[side][i] = fix[side][i]; }
			a2.in.weight.gripModel = 0;
			b2.in.weight.gripModel = 1;
			for (Rig* r : {&a2, &b2}) { r->Off(); r->Steps(30); r->TakeSecond(1.0f); }
			double worst2 = 0.0, mv = 0.0;
			for (int side = 0; side < 2; ++side) {
				worst2 = std::max(worst2, (double)gripfit::Len(gripfit::Sub(GripDrawnHand(b2, side).palm, GripDrawnHand(a2, side).palm)) / kUpm);
				mv = std::max(mv, (double)gripfit::Len(gripfit::Sub(GripDrawnHand(b2, side).palm, GripDrawnHand(lib, side).palm)) / kUpm);
			}
			CHECK(a2.bad == 0 && b2.bad == 0);
			*apartMm = worst2 * 1000.0;
			*movedCm = mv * 100.0;
		};
		const float moves[2][6] = {{3.0f, -2.0f, 1.5f, 0, 0, 0}, {-4.0f, 2.5f, -1.0f, 0, 0, 0}};                           // (right, left)
		const float turns[2][6] = {{0, 0, 0, 20.0f, -10.0f, 12.0f}, {0, 0, 0, -15.0f, 8.0f, -14.0f}};
		double apartMove = 0, movedMove = 0, apartTurn = 0, movedTurn = 0;
		pair(moves, &apartMove, &movedMove);
		pair(turns, &apartTurn, &movedTurn);
		const double lever = offLib;   // the palm's distance from the axis (m)
		const double bound = (lever * (20.0 + 10.0 + 12.0) * 3.14159265 / 180.0) * 1000.0 + 1.0;
		std::printf("      with corrections, the %s hand takes it: moves (right 3 -2 2 cm, left -4 2.5 -1 cm) put the palms %.1f mm from where ours puts them (they moved the hand %.1f cm); turns (right 20 -10 12 deg, left -15 8 -14)\n"
		            "      put them %.1f mm from ours (at most %.1f mm: the palm's lever %.1f cm times the angles); they moved the library's hand %.1f cm\n",
		            SideName(o), apartMove, movedMove, apartTurn, bound, lever * 100.0, movedTurn);
		CHECK(movedMove > 3.0);                // the moves are real
		CHECK(apartMove < 1.0);                // ... and the same as ours, to the millimetre
		CHECK(movedTurn > 0.3);                // the turns are real
		CHECK(apartTurn < bound);
		CHECK(lib.bad == 0 && ours.bad == 0);
	}

	// ---- [weight] slide_second_hand_<kind> through the whole chain: a staff's second hand slides with the controller, a sword's holds where it closed - ours and the library's ----
	{
		StandinSet(0, 0);
		const Weapon sword{"a two-handed sword", D2RVR_TYPE_SWORD, heft::hold::Kind::Sword};
		const uint32_t def = slidekinds::DefaultMask();
		const SlideRun staffOurs = SlideThrough(staff, def, false), swordOurs = SlideThrough(sword, def, false), swordAll = SlideThrough(sword, slidekinds::kAll, false);
		std::printf("  ours, the left controller 25 cm up the shaft: the staff's second hand moves %.1f cm, the sword's %.1f cm (all kinds ticked: the sword's %.1f cm)\n", staffOurs.moved * 100.0, swordOurs.moved * 100.0,
		            swordAll.moved * 100.0);
		CHECK(staffOurs.holds && staffOurs.moved > 0.17 && staffOurs.moved < 0.30);
		CHECK(swordOurs.holds && std::fabs(swordOurs.moved) < 0.02);
		CHECK(swordAll.holds && swordAll.moved > 0.17 && swordAll.moved < 0.30);   // (every kind ticked: as before the list)
		const SlideRun staffLib = SlideThrough(staff, def, true), swordLib = SlideThrough(sword, def, true), swordLibAll = SlideThrough(sword, slidekinds::kAll, true);
		std::printf("  library, grip let go and the trigger squeezed, controller 25 cm up: the staff's second hand %s (mode %d) and moves %.1f cm; the sword's %s; all kinds ticked, the sword's %s (mode %d) and moves %.1f cm\n",
		            staffLib.holds ? "holds" : "lets go", staffLib.mode, staffLib.moved * 100.0, swordLib.holds ? "holds" : "lets go", swordLibAll.holds ? "holds" : "lets go", swordLibAll.mode, swordLibAll.moved * 100.0);
		CHECK(staffLib.holds && staffLib.mode == 2 && staffLib.moved > 0.17 && staffLib.moved < 0.30);
		CHECK(!swordLib.holds);   // no slide for this kind: the trigger alone does not hold a hand
		CHECK(swordLibAll.holds && swordLibAll.mode == 2 && swordLibAll.moved > 0.17 && swordLibAll.moved < 0.30);
	}

	// ---- [weight] grip_hilt_only through the whole chain: a sword's second hand takes only the hilt - ours and the library's ----
	{
		StandinSet(0, 0);
		const Weapon sword{"a two-handed sword", D2RVR_TYPE_SWORD, heft::hold::Kind::Sword};
		for (const bool library : { false, true }) {
			const bool offHilt = SecondTakes(sword, false, 0.78, library), offBlade = SecondTakes(sword, false, 0.50, library);
			const bool hilt = SecondTakes(sword, true, 0.10, library), blade = SecondTakes(sword, true, 0.78, library), mid = SecondTakes(sword, true, 0.50, library);
			const bool custom = SecondTakes(sword, true, 0.50, library, 80.0f), customPast = SecondTakes(sword, true, 0.78, library, 80.0f);
			const bool staffAll = SecondTakes(staff, true, 0.90, library);
			std::printf("  %s, the second hand's grip on a two-handed sword: unticked on the blade %s / middle %s; ticked at the hilt %s, in the middle %s, on the blade %s; with a hilt of 80 cm: middle %s, near the tip %s; a staff ticked, near its tip %s\n",
			            library ? "library" : "ours", offHilt ? "takes" : "no", offBlade ? "takes" : "no", hilt ? "takes" : "no", mid ? "takes" : "no", blade ? "takes" : "no", custom ? "takes" : "no", customPast ? "takes" : "no",
			            staffAll ? "takes" : "no");
			CHECK(offHilt && offBlade);           // as ever
			CHECK(hilt && !mid && !blade);        // the hilt only
			CHECK(custom && !customPast);         // a hilt of its own
			CHECK(staffAll);                      // a staff keeps its whole shaft
		}
	}

	// ---- loose by the trigger through the hook ----
	{
		StandinSet(0, 0);
		Rig r(0, false, true, staff);
		r.in.weight.gripModel = 1;
		r.Off();
		r.Steps(30);
		r.TakeSecond(1.0f);
		CHECK(r.wo.holds[1] && r.wo.gripMode[1] == 1);
		r.Grip(1, 0.0f);
		r.Trigger(1, 1.0f);
		r.Steps(20);
		std::printf("  grip let go, trigger squeezed: holds %s, mode %d\n", r.wo.holds[1] ? "yes" : "no", r.wo.gripMode[1]);
		CHECK(r.wo.holds[1] && r.wo.gripMode[1] == 2);
		r.Trigger(1, 0.0f);
		r.Steps(20);
		CHECK(!r.wo.holds[1]);
		CHECK(r.bad == 0);
	}

	// ---- a library that refuses: the hand is ours ----
	{
		StandinSet(1, 0);
		Rig r(0, false, true, staff);
		r.in.weight.gripModel = 1;
		r.Off();
		r.Steps(30);
		const Coords want = HandCoords(r.animStand, 0);
		r.TakeSecond(1.0f);
		std::printf("  every step refused: the taking hand has the game's pose, %.2f mm; %ld bad\n", Dist(r.Mine(1), want) / kUpm * 1000.0, r.bad);
		CHECK(r.wo.holds[1]);
		CHECK(Dist(r.Mine(1), want) < 0.004);
		CHECK(Logged("refused a step"));
		CHECK(r.bad == 0);
		StandinSet(0, 0);
	}

	// ---- the key off with the library loaded: ours, as ever ----
	{
		Rig r(0, false, true, staff);
		r.in.weight.gripModel = 0;
		r.Off();
		r.Steps(30);
		const Coords want = HandCoords(r.animStand, 0);
		r.TakeSecond(1.0f);
		CHECK(!r.wo.gripLib && r.wo.gripMode[1] == 0);
		CHECK(Dist(r.Mine(1), want) < 0.004);
		CHECK(r.bad == 0);
	}

	std::printf(g_fails == 0 ? "grip_lib_test: all passed\n" : "grip_lib_test: %d failed\n", skel::tst::g_fails);
	return skel::tst::g_fails == 0 ? 0 : 1;
}
