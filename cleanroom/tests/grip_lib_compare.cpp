// Our own way of holding a weapon and a grip library's, face to face: the skeleton hook ITSELF (vr/skeletons.cpp, included whole) on a toy rig with real hands - the game's grip pose on
// the slot's hand - and the weight model in front, once with [weight] grip_model 0 and once with 1 and the library in hand; and the hold on its own for the grip's modes.
//
//   grip_lib_compare dll=<path to d2r_vr_grip.dll>
//
// For each weapon, each hand, each way: how far the palm's middle stands from the shaft's axis (cm) and what is between the palm's surface and the wood (mm, nominal palm depth
// 1.5 cm), how far the line of the knuckles is off the shaft (degrees), how far the fingers are closed (the share of the way from the open hand to the game's grip pose), and
// where the fingertips stand against the wood's surface (mm; negative: in it). Then the second hand's slide: how long the grasp point takes to follow a controller that moves 20 cm,
// in our way (grip squeezed), the library's tight and loose.

#include "toy_hand_rig.h"

#include "grip_lib.h"

using namespace skel;
using namespace skel::tst;
namespace hh = heft::hold;

namespace {

struct Metrics {
	double palmCm = 0, gapMm = 0, knuckleDeg = 0, closed = 0, tipMinMm = 0, tipMaxMm = 0;
};

// The radius of the wood at t metres along the tip axis from the weapon bone's origin (the pieces the weight model told).
double RadiusAt(const weight::Out& wo, double t) {
	double best = -1.0, nearest = 0.02, nd = 1e9;
	for (int i = 0; i < wo.nPieces; ++i) {
		if (t >= wo.pieceT0[i] && t <= wo.pieceT1[i]) best = std::max(best, (double)wo.pieceR[i]);
		const double d = t < wo.pieceT0[i] ? wo.pieceT0[i] - t : t > wo.pieceT1[i] ? t - wo.pieceT1[i] : 0.0;
		if (d < nd) {
			nd      = d;
			nearest = wo.pieceR[i];
		}
	}
	return best >= 0.0 ? best : nearest;
}

Metrics Measure(const Rig& r, int side, int slot, const Coords& openTaker, const Coords& want) {
	Metrics m;
	const auto Pm = [&](int b) { return V3{r.mats[b].m[12], r.mats[b].m[13], r.mats[b].m[14]}; };
	const Mat& att = r.mats[B(slot, kAtt)];
	const V3 o3{att.m[12], att.m[13], att.m[14]}, d3 = Norm(V3{att.m[4], att.m[5], att.m[6]});
	const V3 palm = Scale(Add(Pm(B(side, kHand)), Pm(B(side, kMid))), 0.5f);
	const V3 rel = Sub(palm, o3);
	const double along = Dot(rel, d3) / kUpm;
	const double off = Len(Sub(rel, Scale(d3, Dot(rel, d3)))) / kUpm;
	const double rad = RadiusAt(r.wo, along);
	m.palmCm = off * 100.0;
	m.gapMm = (off - rad - 0.015) * 1000.0;
	const V3 knuckles = Norm(Sub(Pm(B(side, kInd)), Pm(B(side, kPnk))));
	m.knuckleDeg = std::acos(std::min(1.0, std::fabs((double)Dot(knuckles, d3)))) * 57.2957795;
	const Coords mine = r.Mine(side);
	const double toOpen = Dist(mine, openTaker), toWant = Dist(mine, want);
	m.closed = toOpen / std::max(1e-9, toOpen + toWant);
	m.tipMinMm = 1e9;
	m.tipMaxMm = -1e9;
	for (int c = 0; c < 4; ++c) {   // the fingertip: the last bone's origin and one more link
		const V3 a = Pm(B(side, kChain[c][1])), b = Pm(B(side, kChain[c][2]));
		const V3 tip = Add(b, Sub(b, a));
		const V3 q = Sub(tip, o3);
		const double dist = Len(Sub(q, Scale(d3, Dot(q, d3)))) / kUpm;
		const double gap = (dist - RadiusAt(r.wo, Dot(q, d3) / kUpm)) * 1000.0;
		m.tipMinMm = std::min(m.tipMinMm, gap);
		m.tipMaxMm = std::max(m.tipMaxMm, gap);
	}
	return m;
}

// ---- the second hand's slide on its own: how long (s) the grasp point takes to cover 63 % of a 20 cm move of the controller ----
struct HoldRig {
	hh::Hold w;
	hh::Settings s;
	hh::HandIn in[2];
	hh::HandOut out[2];
	explicit HoldRig(int gripModel) {
		s.slide     = true;
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
	double Along(int hand) const {
		heft::V3 butt, tip;
		w.Ends(&butt, &tip);
		const heft::V3 span = heft::Sub(tip, butt);
		return heft::Dot(heft::Sub(w.OnWeapon(hand).pos, butt), heft::Mul(span, 1.0 / heft::Len(span)));
	}
};

double SlideTime(int gripModel, bool trigger) {
	HoldRig r(gripModel);
	r.Step(30);
	r.in[1].pose.pos = { 0.20, 1.30, -0.90 };
	r.in[1].grip     = 0.0f;
	r.Step(3);
	r.in[1].grip = 1.0f;
	r.Step(30);
	const double was = r.Along(1);
	if (trigger) {
		r.in[1].grip    = 0.0f;
		r.in[1].trigger = 1.0f;
		r.Step(2);
	}
	r.in[1].pose.pos.z -= 0.20;
	for (int i = 1; i <= 90; ++i) {
		r.Step();
		if (r.Along(1) - was >= 0.2 * 0.632) return i / 90.0;
	}
	return -1.0;
}

}  // namespace

int main(int argc, char** argv) {
	timeBeginPeriod(1);
	std::wstring dll;
	for (int i = 1; i < argc; ++i)
		if (std::strncmp(argv[i], "dll=", 4) == 0)
			for (const char* c = argv[i] + 4; *c; ++c) dll += (wchar_t)(unsigned char)*c;
	griplib::SetLog([](const char* t) { std::printf("[%s]\n", t); });
	if (!dll.empty()) griplib::SetPath(dll.c_str());
	weight::SetGripLibrarySource(&griplib::Ensure);
	skel::SetGripHandStep(&griplib::HandStep, &griplib::Abi);
	if (!griplib::Ensure()) {
		std::printf("no grip library to compare with (dll=<path> to d2r_vr_grip.dll)\n");
		return 2;
	}
	std::printf("%s\n", griplib::Name());
	std::printf("%-20s %-5s %-8s | palm cm | gap mm | knuckles deg | closed | tips mm (nearest .. farthest)\n", "weapon", "hand", "way");
	struct W { const char* name; int type; heft::hold::Kind kind; };
	const W weapons[] = { { "staff, two hands", D2RVR_TYPE_STAFF, heft::hold::Kind::Staff }, { "spear, two hands", D2RVR_TYPE_SPEAR, heft::hold::Kind::Spear },
	                      { "sword in two hands", D2RVR_TYPE_SWORD, heft::hold::Kind::Sword } };
	for (const W& wp : weapons)
		for (int slotSide = 0; slotSide < 2; ++slotSide)
			for (int way = 0; way < 2; ++way) {
				const Weapon weapon{wp.name, wp.type, wp.kind};
				Rig r(slotSide, false, true, weapon);
				r.in.weight.gripModel = way;
				r.Off();
				r.Steps(30);
				const int o = slotSide ^ 1;
				const Coords want = HandCoords(r.animStand, slotSide), openTaker = HandCoords(r.animOpen, o);
				r.TakeSecond(1.0f);
				r.Steps(60, 4);   // (the library's fingers ease in)
				for (const int side : {slotSide, o}) {
					const Metrics m = Measure(r, side, slotSide, openTaker, want);
					std::printf("%-20s %-5s %-8s | %7.1f | %6.1f | %12.1f | %5.0f%% | %6.1f .. %6.1f\n", wp.name, SideName(side), way ? "library" : "ours", m.palmCm, m.gapMm, m.knuckleDeg,
					            m.closed * 100.0, m.tipMinMm, m.tipMaxMm);
				}
				if (r.bad) std::printf("   (%ld bad matrices; first: %s)\n", r.bad, r.firstBad.c_str());
			}
	std::printf("\nthe second hand on a staff follows a controller that moves 20 cm (63 %% of the way), s:\n");
	std::printf("  ours, grip squeezed (it slides)      %.3f\n", SlideTime(0, false));
	std::printf("  library, grip squeezed (tight)       %s\n", SlideTime(1, false) < 0 ? "does not move" : "moves");
	std::printf("  library, trigger squeezed (loose)    %.3f\n", SlideTime(1, true));
	return 0;
}
