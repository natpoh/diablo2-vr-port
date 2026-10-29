// [weight] slide_second_hand_<kind> (vr/slide_kinds.h): on which weapons the second hand slides along the shaft. Pure, and cross-checked against the weight model's own shapes.
//
//   slide_kinds_test           all checks (exit code 0 = all passed)
//
// Checks: the defaults (a staff, a spear and a polearm slide; a sword, an axe, a mace do not); a mask holds one bit for each kind and nothing else; a type with no tick never slides; the
// list is complete (every kind the weight model gives a shaft long enough to slide on has a tick) and has no idle tick (every tick is for a kind that has such a shaft in some
// configuration); the ini keys are the Settings program's (the names are distinct and plain); and the hold itself: the second hand slides on a ticked kind and holds where it closed
// on an unticked one, with the same gate the weight model applies (Settings::slide), for every long kind, in the mod's own way of holding.

#include "../../vr/slide_kinds.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "hold.h"
#include "slide.h"

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

// The weight model's kind for a weapon type (vr/vrcam.cpp does this switch for the model; -1: no shape of its own)
int HeftKindOf(uint32_t type) {
	switch (type) {
	case D2RVR_TYPE_SWORD: return (int)hh::Kind::Sword;
	case D2RVR_TYPE_AXE: return (int)hh::Kind::Axe;
	case D2RVR_TYPE_MACE: return (int)hh::Kind::Mace;
	case D2RVR_TYPE_SCEPTER: return (int)hh::Kind::Scepter;
	case D2RVR_TYPE_WAND: return (int)hh::Kind::Wand;
	case D2RVR_TYPE_STAFF: return (int)hh::Kind::Staff;
	case D2RVR_TYPE_POLEARM: return (int)hh::Kind::Polearm;
	case D2RVR_TYPE_SPEAR: return (int)hh::Kind::Spear;
	case D2RVR_TYPE_DAGGER: return (int)hh::Kind::Dagger;
	case D2RVR_TYPE_THROWING: return (int)hh::Kind::Throwing;
	case D2RVR_TYPE_JAVELIN: return (int)hh::Kind::Javelin;
	case D2RVR_TYPE_OTHER: return (int)hh::Kind::Other;
	default: return -1;
	}
}

bool Long(int heftKind, bool two) {
	const hh::Shape s = hh::ShapeOf((hh::Kind)heftKind, two);
	return s.tip - s.butt >= heft::slide::kMinShaft;
}

// two hands on a weapon of this kind, the second one squeezed on 50 cm up the shaft, then its controller 25 cm further along: how far its grasp moved along the shaft
double SlideOf(hh::Kind kind, bool slide) {
	hh::Hold w;
	hh::Settings s;
	s.slide = slide;
	hh::HandIn in[2];
	hh::HandOut out[2];
	in[0].tracked = in[1].tracked = true;
	in[0].pose = { { 0.20, 1.30, -0.40 }, {} };
	in[1].pose = { { -0.20, 1.30, -0.90 }, {} };
	in[0].grip = in[1].grip = 0.0f;
	w.Place(0, kind, true, { {}, { 0, 0, -1 } }, in, s);
	const auto step = [&](int n) { for (int i = 0; i < n; ++i) w.Step(in, 1.0 / 90.0, s, out); };
	const auto along = [&](int hand) {
		heft::V3 butt, tip;
		w.Ends(&butt, &tip);
		const heft::V3 span = heft::Sub(tip, butt);
		return heft::Dot(heft::Sub(w.OnWeapon(hand).pos, butt), heft::Mul(span, 1.0 / heft::Len(span)));
	};
	step(30);
	in[1].pose.pos = { 0.20, 1.30, -0.90 };
	step(3);
	in[1].grip = 1.0f;
	step(30);
	if (!out[1].holds) return -1.0;
	const double was = along(1);
	in[1].pose.pos.z -= 0.25;
	step(40);
	return out[1].holds ? along(1) - was : -2.0;
}
}  // namespace

int main() {
	using namespace slidekinds;

	// ---- the defaults and the mask ----
	{
		const uint32_t def = DefaultMask();
		CHECK(Allows(def, D2RVR_TYPE_STAFF) && Allows(def, D2RVR_TYPE_SPEAR) && Allows(def, D2RVR_TYPE_POLEARM), "a staff's, a spear's and a polearm's second hand slide by default");
		CHECK(!Allows(def, D2RVR_TYPE_SWORD) && !Allows(def, D2RVR_TYPE_AXE) && !Allows(def, D2RVR_TYPE_MACE), "a sword's, an axe's and a mace's do not");
		CHECK(!Allows(def, D2RVR_TYPE_JAVELIN) && !Allows(def, D2RVR_TYPE_OTHER), "a javelin's and the unnamed kind's do not");
		int ticks = 0;
		for (int i = 0; i < kCount; ++i) ticks += (def >> i) & 1u;
		CHECK(ticks == 3, "three kinds slide by default (%d)", ticks);
		CHECK(Allows(kAll, D2RVR_TYPE_SWORD) && Allows(kAll, D2RVR_TYPE_STAFF) && Allows(kAll, D2RVR_TYPE_OTHER), "every kind ticked: every long shaft's second hand slides, as before the list");
		CHECK(!Allows(0u, D2RVR_TYPE_STAFF) && !Allows(0u, D2RVR_TYPE_SWORD), "no kind ticked: none slides");
		// one bit for each kind, nothing else
		for (int i = 0; i < kCount; ++i) {
			const uint32_t only = 1u << i;
			for (int j = 0; j < kCount; ++j) CHECK(Allows(only, kKinds[j].type) == (i == j), "mask bit %d allows kind %d only when they are the same (%d)", i, j, (int)Allows(only, kKinds[j].type));
		}
		// the types with no tick never slide, whatever the mask
		for (const uint32_t t : { (uint32_t)D2RVR_TYPE_UNKNOWN, (uint32_t)D2RVR_TYPE_UNARMED, (uint32_t)D2RVR_TYPE_BOW, (uint32_t)D2RVR_TYPE_CROSSBOW, (uint32_t)D2RVR_TYPE_SCEPTER, (uint32_t)D2RVR_TYPE_WAND,
		                          (uint32_t)D2RVR_TYPE_DAGGER, (uint32_t)D2RVR_TYPE_THROWING, (uint32_t)D2RVR_TYPE_CLAW, (uint32_t)D2RVR_TYPE_ORB, (uint32_t)D2RVR_TYPE_COUNT, 999u })
			CHECK(IndexOf(t) == -1 && !Allows(kAll, t), "type %u has no tick and never slides", t);
		std::printf("  the defaults and the mask: %d kinds, mask %#x\n", kCount, def);
	}

	// ---- the names ----
	{
		std::set<std::string> names;
		std::set<uint32_t> types;
		bool plain = true;
		for (int i = 0; i < kCount; ++i) {
			names.insert(kKinds[i].name);
			types.insert(kKinds[i].type);
			for (const char* p = kKinds[i].name; *p; ++p) plain = plain && ((*p >= 'a' && *p <= 'z'));
		}
		CHECK((int)names.size() == kCount && (int)types.size() == kCount, "each kind has a name and a type of its own");
		CHECK(plain, "the names are lower-case words (they end an ini key)");
		CHECK(kCount <= 32, "a mask is 32 bits");
	}

	// ---- the list against the weight model's shapes ----
	{
		for (uint32_t t = 0; t < (uint32_t)D2RVR_TYPE_COUNT; ++t) {
			const int hk = HeftKindOf(t);
			if (hk < 0) continue;
			const bool longAlone = Long(hk, false), longTwo = Long(hk, true);
			if (longAlone || longTwo) CHECK(IndexOf(t) >= 0, "%s has a shaft long enough to slide on (one hand %d, two %d) and a tick", hh::KindName((hh::Kind)hk), (int)longAlone, (int)longTwo);
			else CHECK(IndexOf(t) < 0, "%s has no shaft long enough to slide on, so no tick", hh::KindName((hh::Kind)hk));
		}
		for (int i = 0; i < kCount; ++i) {
			const int hk = HeftKindOf(kKinds[i].type);
			CHECK(hk >= 0 && (Long(hk, false) || Long(hk, true)), "the tick for %s is for a kind that can have a long shaft", kKinds[i].name);
		}
		std::printf("  the list is the kinds with a long shaft: ");
		for (int i = 0; i < kCount; ++i) std::printf("%s%s", kKinds[i].name, i + 1 < kCount ? ", " : "\n");
	}

	// ---- the hold: ticked, the second hand slides with its controller; unticked, it holds where it closed ----
	{
		struct K { const char* name; hh::Kind kind; uint32_t type; };
		const K ks[] = { { "sword (two-handed)", hh::Kind::Sword, D2RVR_TYPE_SWORD }, { "axe (two-handed)", hh::Kind::Axe, D2RVR_TYPE_AXE }, { "mace (two-handed)", hh::Kind::Mace, D2RVR_TYPE_MACE },
		                 { "staff", hh::Kind::Staff, D2RVR_TYPE_STAFF },            { "polearm", hh::Kind::Polearm, D2RVR_TYPE_POLEARM }, { "spear", hh::Kind::Spear, D2RVR_TYPE_SPEAR } };
		for (const K& k : ks) {
			const bool ticked = Allows(DefaultMask(), k.type);
			const double moved = SlideOf(k.kind, ticked);      // (the gate the weight model applies: Settings::slide = master && Allows(mask, type))
			const double heldFast = SlideOf(k.kind, false);
			std::printf("  %-18s %s by default: the second hand moves %5.1f cm along the shaft for a 25 cm slide of its controller (unticked: %.1f cm)\n", k.name, ticked ? "slides" : "holds   ", moved * 100.0, heldFast * 100.0);
			if (ticked) CHECK(std::fabs(moved - 0.25) < 0.04, "%s: its second hand slides with the controller (%.3f m)", k.name, moved);
			else CHECK(std::fabs(moved) < 0.01, "%s: its second hand holds where it closed (%.3f m)", k.name, moved);
			CHECK(std::fabs(heldFast) < 0.01, "%s: unticked, it holds where it closed (%.3f m)", k.name, heldFast);
		}
	}

	std::printf(g_fails == 0 ? "slide_kinds_test: all passed\n" : "slide_kinds_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
