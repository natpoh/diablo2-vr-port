// [weight] grip_hilt_only (vr/grip_zone.h, cleanroom/heft/hold.h): where along the shaft the SECOND hand may take a weapon the other hand holds - pure, and on the hold itself.
//
//   grip_zone_test             all checks (exit code 0 = all passed)
//
// Checks: the kinds of the list are the weapons the weight model shapes as a hilt and a blade or head (a first piece shorter than the shaft, another piece after it), and every other kind
// keeps the whole shaft; unticked, the second hand takes a sword, an axe, a mace anywhere along it as ever; ticked, it takes it from the pommel to the guard and a little past (the margin)
// and not on the blade or the head - for every kind, one-handed and two-handed - while a staff, a spear, a polearm, a javelin, a wand, a throwing weapon keep the whole shaft; a
// length of its own moves the guard; the hilt follows the weapon's drawn extents (Hold::SetDrawnShaft); TakeZone says the same as the grip does (so the belt can have the press);
// the first hand's own grip and a weapon that lies or falls are untouched.

#include "../../vr/grip_zone.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "hold.h"

namespace hh = heft::hold;

namespace {
int g_fails = 0;
#define CHECK(c, ...)                                                    \
	do {                                                                 \
		if (!(c)) {                                                      \
			++g_fails;                                                   \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c);          \
			std::printf(__VA_ARGS__);                                    \
			std::printf("\n");                                           \
		}                                                                \
	} while (0)

constexpr double kDt = 1.0 / 90.0;

struct Rig {
	hh::Hold w;
	hh::Settings s;
	hh::HandIn in[2];
	hh::HandOut out[2];
	hh::Kind kind;
	bool two;
	int first = 0, second = 1;   // the hand that holds it (the game's slot's) and the one that takes it second
	double sx = 1.0;             // the first fist's side: +x the right hand, -x the left (the left-hand case is the right one's mirror image)
	Rig(hh::Kind k, bool twoHanded, bool hiltOnly, double hiltLen = 0.0, int firstHand = 0) : kind(k), two(twoHanded), first(firstHand & 1), second((firstHand & 1) ^ 1), sx(first == 0 ? 1.0 : -1.0) {
		s.hiltOnly = hiltOnly;
		s.hiltLen  = hiltLen;
		in[0].tracked = in[1].tracked = true;
		in[first].pose = { { 0.20 * sx, 1.30, -0.40 }, {} };   // the first fist holds the weapon here; the shaft runs from it along -z (the body's frame IS that fist)
		in[second].pose = { { -0.20 * sx, 1.30, -0.40 }, {} };
		in[0].grip = in[1].grip = 0.0f;
		w.Place(first, k, twoHanded, { {}, { 0, 0, -1 } }, in, s);
		Step(30);
	}
	void Step(int n = 1) {
		for (int i = 0; i < n; ++i) w.Step(in, kDt, s, out);
	}
	// the second fist on the shaft `ahead` metres along it from the first fist (negative: behind it, toward the pommel), 3 cm off the axis
	void Fist(double ahead) { in[second].pose.pos = { (0.20 - 0.03) * sx, 1.30, -0.40 - ahead }; }
	// does a grip pressed there take the weapon? (a press is a rise: let go first)
	bool Takes(double ahead) {
		in[second].grip = 0.0f;
		Fist(ahead);
		Step(3);
		in[second].grip = 1.0f;
		Step(10);
		const bool holds = out[second].holds;
		in[second].grip = 0.0f;
		Step(5);
		return holds;
	}
	hh::Hold::TakeAt Zone(double ahead, double* metres = nullptr, double* along = nullptr) {
		in[second].grip = 0.0f;
		Fist(ahead);
		Step(3);
		double m = 0.0;
		const hh::Hold::TakeAt at = w.TakeZone(second, in[second], s, &m);
		if (metres) *metres = m;
		if (along) { double d = 0.0; w.Probe(second, in[second], s, along, &d); }
		return at;
	}
};

// the model's hilt for a kind: where the first piece of its shape ends (metres, the line's measure), and the shaft's ends
struct Hilt { double end, butt, tip; int pieces; };
Hilt ModelHilt(hh::Kind k, bool two) {
	const hh::Shape sh = hh::ShapeOf(k, two);
	return { sh.nPieces > 0 ? std::max(sh.pieces[0].a.y, sh.pieces[0].b.y) : sh.butt, sh.butt, sh.tip, sh.nPieces };
}
}  // namespace

int main() {
	using hh::Kind;

	// ---- the list against the weight model's shapes ----
	{
		for (int i = 0; i < gripzone::kCount; ++i) {
			const uint32_t t = gripzone::kKinds[i].type;
			CHECK(gripzone::IndexOf(t) == i, "kind %d finds itself", i);
			Kind k = Kind::Other;
			switch (t) {
			case D2RVR_TYPE_SWORD: k = Kind::Sword; break;
			case D2RVR_TYPE_AXE: k = Kind::Axe; break;
			case D2RVR_TYPE_MACE: k = Kind::Mace; break;
			case D2RVR_TYPE_SCEPTER: k = Kind::Scepter; break;
			case D2RVR_TYPE_DAGGER: k = Kind::Dagger; break;
			default: CHECK(false, "kind %d is a type with no shape in this test", i);
			}
			for (const bool two : { false, true }) {
				const Hilt h = ModelHilt(k, two);
				CHECK(h.pieces >= 2 && h.end > h.butt + 0.05 && h.end < h.tip - 0.05, "%s%s: a hilt (%.2f m) and a blade or head after it (shaft %.2f .. %.2f)", two ? "two-handed " : "", gripzone::kKinds[i].name, h.end - h.butt, h.butt, h.tip);
			}
		}
		for (const uint32_t t : { (uint32_t)D2RVR_TYPE_STAFF, (uint32_t)D2RVR_TYPE_POLEARM, (uint32_t)D2RVR_TYPE_SPEAR, (uint32_t)D2RVR_TYPE_JAVELIN, (uint32_t)D2RVR_TYPE_WAND, (uint32_t)D2RVR_TYPE_THROWING,
		                          (uint32_t)D2RVR_TYPE_OTHER, (uint32_t)D2RVR_TYPE_BOW, (uint32_t)D2RVR_TYPE_UNARMED })
			CHECK(gripzone::IndexOf(t) == -1, "type %u has no hilt of its own: the whole shaft", t);
		CHECK(gripzone::kCount == 5 && gripzone::kMaxCm >= 120.0f, "five kinds, a slider long enough for a two-handed axe's haft");
		std::printf("  the list: ");
		for (int i = 0; i < gripzone::kCount; ++i) std::printf("%s%s", gripzone::kKinds[i].name, i + 1 < gripzone::kCount ? ", " : "\n");
	}

	// ---- unticked: the whole weapon, as ever; ticked: the hilt, with a margin ----
	{
		struct K { const char* name; Kind kind; bool two; };
		const K ks[] = { { "sword", Kind::Sword, false },     { "two-handed sword", Kind::Sword, true }, { "axe", Kind::Axe, false },   { "two-handed axe", Kind::Axe, true },
		                 { "mace", Kind::Mace, false },       { "maul", Kind::Mace, true },              { "scepter", Kind::Scepter, false }, { "dagger", Kind::Dagger, false } };
		const double margin = hh::Settings{}.hiltMargin;
		for (const K& k : ks) {
			const Hilt h = ModelHilt(k.kind, k.two);
			const double inside = h.end - 0.02, past = h.end + margin + 0.03, tipEnd = h.tip - 0.04, pommel = h.butt + 0.02;
			// unticked
			{
				Rig r(k.kind, k.two, false);
				CHECK(r.Takes(inside) && r.Takes(past) && r.Takes(tipEnd) && r.Takes(pommel), "%s: unticked, the second hand takes it anywhere along it", k.name);
			}
			// ticked
			Rig r(k.kind, k.two, true);
			const bool a = r.Takes(inside), b = r.Takes(pommel), c = r.Takes(past), d = r.Takes(tipEnd), e = r.Takes(h.end + margin - 0.01);
			std::printf("  %-17s hilt %.0f cm (the guard at %.2f m): the second hand takes it at the hilt %s, at the pommel %s, past the guard and the margin %s, just inside the margin %s, at its tip %s\n", k.name,
			            (h.end - h.butt) * 100.0, h.end, a ? "yes" : "no", b ? "yes" : "no", c ? "yes" : "no", e ? "yes" : "no", d ? "yes" : "no");
			CHECK(a && b, "%s: ticked, the hilt and the pommel are taken", k.name);
			CHECK(e, "%s: ... and just short of the margin past the guard", k.name);
			CHECK(!c && !d, "%s: ... the blade or the head is not", k.name);
			// TakeZone says what the grip does
			CHECK(r.Zone(inside) == hh::Hold::TakeAt::Shaft && r.Zone(past) == hh::Hold::TakeAt::Nowhere && r.Zone(tipEnd) == hh::Hold::TakeAt::Nowhere, "%s: TakeZone: the hilt is the shaft's, the blade is nowhere", k.name);
			Rig u(k.kind, k.two, false);
			CHECK(u.Zone(tipEnd) == hh::Hold::TakeAt::Shaft, "%s: unticked, TakeZone has the blade as the shaft's", k.name);
		}
	}

	// ---- the other kinds keep the whole shaft, ticked ----
	{
		struct K { const char* name; Kind kind; bool two; };
		const K ks[] = { { "staff", Kind::Staff, true }, { "polearm", Kind::Polearm, true }, { "spear", Kind::Spear, true }, { "javelin", Kind::Javelin, false }, { "wand", Kind::Wand, false },
		                 { "throwing weapon", Kind::Throwing, false }, { "other", Kind::Other, false } };
		for (const K& k : ks) {
			const hh::Shape sh = hh::ShapeOf(k.kind, k.two);
			Rig r(k.kind, k.two, true);
			CHECK(std::fabs(r.w.HiltEnd(r.s) - sh.tip) < 1e-12, "%s: the zone reaches the tip (%.2f of %.2f)", k.name, r.w.HiltEnd(r.s), sh.tip);
			CHECK(r.Takes(sh.butt + 0.05) && r.Takes(0.5 * (sh.butt + sh.tip)) && r.Takes(sh.tip - 0.05), "%s: ticked, it is taken along the whole shaft", k.name);
		}
		std::printf("  a staff, a spear, a polearm, a javelin, a wand, a throwing weapon: the whole shaft, ticked\n");
	}

	// ---- a length of its own, the margin, the drawn weapon ----
	{
		// a two-handed sword with a 55 cm hilt of its own (butt at -0.30): the guard at 0.25
		Rig r(Kind::Sword, true, true, 0.55);
		CHECK(std::fabs(r.w.HiltEnd(r.s) - (-0.30 + 0.55)) < 1e-9, "the length of its own: %.3f", r.w.HiltEnd(r.s));
		CHECK(r.Takes(0.20) && !r.Takes(0.25 + r.s.hiltMargin + 0.03), "the guard moves with it");
		// no margin: the zone ends at the guard
		Rig m(Kind::Sword, true, true);
		m.s.hiltMargin = 0.0;
		const double end = m.w.HiltEnd(m.s);
		CHECK(m.Takes(end - 0.01) && !m.Takes(end + 0.02), "with no margin the zone ends at the guard");
		// a hilt of its own longer than the weapon is the weapon
		Rig big(Kind::Dagger, false, true, 5.0);
		CHECK(big.w.HiltEnd(big.s) <= hh::ShapeOf(Kind::Dagger, false).tip + 1e-12, "never past the tip");
		// the weapon as it is drawn: its extents measured (butt 0.15, tip 1.10 from the bone: the log of a two-handed sword), the hilt scaled onto them
		Rig d(Kind::Sword, true, true);
		const hh::Shape sh0 = hh::ShapeOf(Kind::Sword, true);
		const double nb = -0.15, nt = 1.10, k = (nt - nb) / (sh0.tip - sh0.butt);
		d.w.SetDrawnShaft({ {}, { 0, 0, -1 } }, nb, nt, d.s);
		const double want = nb + (std::max(sh0.pieces[0].a.y, sh0.pieces[0].b.y) - sh0.butt) * k;
		std::printf("  the drawn weapon (butt %.2f, tip %.2f): the hilt ends at %.2f m (the model's %.2f scaled by %.2f), %.0f cm long\n", nb, nt, d.w.HiltEnd(d.s), want, k, (d.w.HiltEnd(d.s) - nb) * 100.0);
		CHECK(std::fabs(d.w.HiltEnd(d.s) - want) < 1e-9, "the hilt follows the drawn extents (%.4f against %.4f)", d.w.HiltEnd(d.s), want);
		CHECK(d.Takes(want - 0.02) && !d.Takes(want + d.s.hiltMargin + 0.03), "and so does the zone");
	}

	// ---- what the zone does not touch: the first hand's own grip; a weapon that lies ----
	{
		// the hand the weapon was placed in holds it with its own grip wherever it is (the game's hand: the weapon is in its fist)
		Rig r(Kind::Sword, true, true);
		r.in[0].grip = 1.0f;
		r.Step(10);
		CHECK(r.out[0].holds, "the first hand holds");
		// a weapon nobody holds is taken by the whole of it, as ever: let both go, let it fall and lie, then a hand at its tip
		r.in[0].grip = 0.0f;
		r.Step(5);
		CHECK(!r.out[0].holds && !r.out[1].holds, "nobody holds it");
		for (int i = 0; i < 1500 && !r.w.Landed(); ++i) r.Step();
		CHECK(r.w.Landed(), "it lies on the floor");
		heft::V3 butt, tip;
		r.w.Ends(&butt, &tip);
		r.in[1].pose.pos = heft::Add(tip, { 0.0, 0.03, 0.0 });
		r.in[1].grip = 0.0f;
		r.Step(3);
		double m = 0.0;
		CHECK(r.w.TakeZone(1, r.in[1], r.s, &m) == hh::Hold::TakeAt::Shaft, "a lying weapon: the zone is the shaft, its tip too (%.3f m)", m);
		r.in[1].grip = 1.0f;
		r.Step(10);
		CHECK(r.out[1].holds, "a lying weapon is taken by its tip");
		std::printf("  a lying weapon is taken anywhere along it, its tip too\n");
	}

	// ---- both directions (Serg, 0.206.11: «правая рука иногда не может взять за рукоятку сверху левой»): the RIGHT hand taking a weapon the LEFT one holds is the left hand
	//      taking one the right holds, mirrored - the zone, the distance, the place along the shaft, the take, for every kind, ticked and not, at every place along it ----
	{
		struct K { const char* name; Kind kind; bool two; };
		const K ks[] = { { "sword", Kind::Sword, false }, { "two-handed sword", Kind::Sword, true }, { "axe", Kind::Axe, false }, { "mace", Kind::Mace, false }, { "dagger", Kind::Dagger, false },
		                 { "staff", Kind::Staff, true },  { "spear", Kind::Spear, true } };
		int compared = 0;
		for (const K& k : ks)
			for (const bool hilt : { false, true }) {
				Rig r(k.kind, k.two, hilt, 0.0, 0), l(k.kind, k.two, hilt, 0.0, 1);
				CHECK(r.out[0].holds && !r.out[1].holds && l.out[1].holds && !l.out[0].holds, "%s: placed in the first hand (right %d, left %d)", k.name, (int)r.out[0].holds, (int)l.out[1].holds);
				for (double ahead = -0.30; ahead <= 0.90; ahead += 0.05) {
					double mr = 0, ml = 0, ar = 0, al = 0;
					const hh::Hold::TakeAt zr = r.Zone(ahead, &mr, &ar), zl = l.Zone(ahead, &ml, &al);
					CHECK(zr == zl && std::fabs(mr - ml) < 1e-6 && std::fabs(ar - al) < 1e-6, "%s%s, %.2f m ahead: the left hand taking from the right (zone %d, %.3f m off, %.3f along) = the right hand taking from the left (zone %d, %.3f, %.3f)",
					      k.name, hilt ? " (hilt only)" : "", ahead, (int)zr, mr, ar, (int)zl, ml, al);
					++compared;
				}
				for (double ahead : { -0.15, -0.05, 0.05, 0.15, 0.40 }) {
					Rig r2(k.kind, k.two, hilt, 0.0, 0), l2(k.kind, k.two, hilt, 0.0, 1);
					const bool tr = r2.Takes(ahead), tl = l2.Takes(ahead);
					CHECK(tr == tl, "%s%s, %.2f m ahead: the second hand takes it the same either way (left from right %d, right from left %d)", k.name, hilt ? " (hilt only)" : "", ahead, (int)tr, (int)tl);
				}
			}
		std::printf("  both directions: %d places along %d kinds compared - the right hand on a weapon the left holds is the left hand on one the right holds\n", compared, (int)(sizeof ks / sizeof ks[0]));
	}

	std::printf(g_fails == 0 ? "grip_zone_test: all passed\n" : "grip_zone_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
