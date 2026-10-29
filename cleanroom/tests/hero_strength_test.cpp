// The hero's strength against the weapon's (vr/hero_strength.h): how heavy a weapon feels. Pure maths.
//
//   hero_strength_test         all checks (exit code 0 = all passed);  `table` prints the factors for the classes and the weapons (docs/WEAPON_WEIGHT.md)
//
// Checks: a Strength that is not read changes nothing; at the reference margin both factors are 1; the steel gets lighter and the hands stronger as Strength grows (monotone,
// smooth, clamped both ways), heavier and weaker as the requirement grows; a class with a larger multiplier is lighter in hand than another with the same stat (the barbarian
// against the sorceress); "Strength matters" 0 changes nothing and 200 doubles the exponent; a requirement the game's tables do not give is a typical one for the kind.

#include "../../vr/hero_strength.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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

herostr::In Hero(double str, double req, double classPct = 100.0, double effect = 100.0, int kind = 1, bool two = false) {
	herostr::In in;
	in.str       = str;
	in.req       = req;
	in.classPct  = classPct;
	in.effectPct = effect;
	in.kind      = kind;
	in.twoHanded = two;
	return in;
}
}  // namespace

int main(int argc, char** argv) {
	using herostr::Factor;
	if (argc > 1 && std::strcmp(argv[1], "table") == 0) {
		struct W { const char* name; int kind; bool two; double req; };
		const W ws[] = { { "Dagger (0)", 0, false, 0 },        { "Long Sword (55)", 1, false, 55 }, { "Two-Handed Sword (35)", 1, true, 35 }, { "Great Sword (100)", 1, true, 100 },
		                 { "Maul (69)", 3, true, 69 },         { "Great Maul (99)", 3, true, 99 },  { "Colossus Blade (189)", 1, true, 189 },  { "Thunder Maul (253)", 3, true, 253 } };
		const char* cn[8] = { "Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock" };
		const double str[8] = { 90, 40, 50, 110, 160, 60, 90, 60 };   // a hero of about level 45, points to taste
		std::printf("%-14s %5s %5s |", "class", "Str", "x%");
		for (const W& w : ws) std::printf(" %-22s|", w.name);
		std::printf("\n");
		for (int c = 0; c < 8; ++c) {
			std::printf("%-14s %5.0f %5.0f |", cn[c], str[c], herostr::DefaultClassPct(c));
			for (const W& w : ws) {
				const herostr::Out o = Factor(Hero(str[c], w.req, herostr::DefaultClassPct(c), 100.0, w.kind, w.two));
				std::printf(" steel %.2f hands %.2f   |", o.steel, o.hands);
			}
			std::printf("\n");
		}
		return 0;
	}

	// ---- a Strength that was not read changes nothing ----
	{
		const herostr::Out o = Factor(Hero(-1, 100));
		CHECK(!o.known && o.steel == 1.0 && o.hands == 1.0, "not read: nothing moves");
		const herostr::Out bad = Factor(Hero(std::nan(""), 100));
		CHECK(!bad.known && bad.steel == 1.0, "a NaN is not a Strength");
	}

	// ---- the reference margin is today's weight ----
	{
		// (str x 1 + 50) / (req + 50) = 1.5  ->  str = 1.5 * (req + 50) - 50
		for (const double req : { 0.0, 35.0, 100.0, 189.0 }) {
			const double str = herostr::kRef * (req + herostr::kK) - herostr::kK;
			const herostr::Out o = Factor(Hero(str, req));
			CHECK(o.known && std::fabs(o.ratio - herostr::kRef) < 1e-9 && std::fabs(o.steel - 1.0) < 1e-9 && std::fabs(o.hands - 1.0) < 1e-9, "req %.0f: str %.1f is the reference (steel %.4f hands %.4f)", req, str, o.steel, o.hands);
		}
	}

	// ---- monotone, smooth, clamped ----
	{
		double lastSteel = 9.0, lastHands = 0.0, worstStep = 0.0;
		for (double str = 0; str <= 600; str += 1.0) {
			const herostr::Out o = Factor(Hero(str, 100));
			CHECK(o.steel <= lastSteel + 1e-12 && o.hands >= lastHands - 1e-12, "monotone in Strength at %.0f", str);
			if (str > 0) worstStep = std::fmax(worstStep, std::fmax(std::fabs(o.steel - lastSteel), std::fabs(o.hands - lastHands)));
			lastSteel = o.steel;
			lastHands = o.hands;
			CHECK(o.steel >= herostr::kSteelMin && o.steel <= herostr::kSteelMax && o.hands >= herostr::kHandsMin && o.hands <= herostr::kHandsMax, "clamped at %.0f", str);
		}
		std::printf("  a point of Strength moves the factors by at most %.4f\n", worstStep);
		CHECK(worstStep < 0.01, "smooth: no jump (%.4f)", worstStep);
		lastSteel = 0.0;
		lastHands = 9.0;
		for (double req = 0; req <= 260; req += 1.0) {
			const herostr::Out o = Factor(Hero(100, req));
			CHECK(o.steel >= lastSteel - 1e-12 && o.hands <= lastHands + 1e-12, "monotone in the requirement at %.0f", req);
			lastSteel = o.steel;
			lastHands = o.hands;
		}
		const herostr::Out weak = Factor(Hero(1, 250)), strong = Factor(Hero(900, 0));
		CHECK(weak.steel == herostr::kSteelMax && weak.hands == herostr::kHandsMin, "the weakest hero with the heaviest weapon: the clamps (%.2f %.2f)", weak.steel, weak.hands);
		CHECK(strong.steel == herostr::kSteelMin && strong.hands == herostr::kHandsMax, "the strongest with nothing: the clamps (%.2f %.2f)", strong.steel, strong.hands);
	}

	// ---- the class multiplier: the barbarian against the sorceress ----
	{
		const herostr::Out barb = Factor(Hero(80, 100, herostr::DefaultClassPct(4))), sorc = Factor(Hero(80, 100, herostr::DefaultClassPct(1)));
		std::printf("  Str 80 against a requirement of 100: the barbarian's steel x%.2f hands x%.2f, the sorceress' steel x%.2f hands x%.2f\n", barb.steel, barb.hands, sorc.steel, sorc.hands);
		CHECK(barb.steel < sorc.steel && barb.hands > sorc.hands, "the barbarian is the lighter in hand");
		for (int c = 0; c < herostr::kClasses; ++c) CHECK(herostr::DefaultClassPct(c) <= herostr::DefaultClassPct(4), "the barbarian's multiplier is the highest (class %d)", c);
		const herostr::Out same = Factor(Hero(80, 100, 100.0)), twice = Factor(Hero(160, 100, 50.0));
		CHECK(std::fabs(same.ratio - twice.ratio) < 1e-12, "capacity is Str x the class %%");
		CHECK(herostr::DefaultClassPct(-1) == 100.0 && herostr::DefaultClassPct(9) == 100.0, "an unknown class counts 100");
	}

	// ---- "Strength matters" ----
	{
		const herostr::Out off = Factor(Hero(160, 100, 130.0, 0.0)), base = Factor(Hero(160, 100, 130.0, 100.0)), twice = Factor(Hero(160, 100, 130.0, 200.0));
		CHECK(off.steel == 1.0 && off.hands == 1.0, "0 %%: nothing moves");
		const double x = base.ratio / herostr::kRef;
		CHECK(base.steel < 1.0 && std::fabs(base.steel - std::pow(x, -0.5)) < 1e-12 && std::fabs(base.hands - std::pow(x, 0.5)) < 1e-12, "100 %%: x^-1/2 and x^+1/2 (x %.3f)", x);
		CHECK(std::fabs(twice.steel - std::pow(x, -1.0)) < 1e-12 || twice.steel == herostr::kSteelMin, "200 %%: the exponent doubled (%.3f)", twice.steel);
		CHECK(twice.steel <= base.steel && twice.hands >= base.hands, "more of it, more effect");
		CHECK(std::fabs(base.steel * base.hands - 1.0) < 1e-12, "mass x wrist cancel at the default exponents (the lag moves by x^-1)");
	}

	// ---- the requirement from the game, else a typical one ----
	{
		const herostr::Out g = Factor(Hero(100, 69, 100, 100, 3, true));
		CHECK(g.reqFromGame && g.req == 69.0, "the game's requirement is used");
		const herostr::Out t = Factor(Hero(100, -1, 100, 100, 3, true));
		CHECK(!t.reqFromGame && t.req == herostr::NominalReq(3, true) && t.req == 146.0, "a maul without a requirement: the typical %.0f", t.req);
		CHECK(herostr::NominalReq(1, true) > herostr::NominalReq(1, false) && herostr::NominalReq(0, false) < herostr::NominalReq(7, true), "two-handed asks more than one-handed, a dagger less than a polearm");
		for (int k = 0; k < 12; ++k) CHECK(herostr::NominalReq(k, false) > 0 && herostr::NominalReq(k, true) > 0, "every kind has one (%d)", k);
		const herostr::Out zero = Factor(Hero(30, 0, 130));
		CHECK(zero.known && zero.reqFromGame && zero.req == 0.0 && std::isfinite(zero.ratio), "a requirement of 0 is a requirement (a dagger): finite");
	}

	// ---- [weight] strength_honest: the ratio as it is (the author's Barbarian: 35 Str x 300 % against a Two-Handed Sword's 35) ----
	{
		herostr::In in = Hero(35, 35, 300, 100, 1, true);
		const herostr::Out old = Factor(in);
		in.honest = true;
		const herostr::Out now = Factor(in);
		CHECK(std::fabs(old.ratio - 155.0 / 85.0) < 1e-9, "as before: +50 each side (%.3f)", old.ratio);
		CHECK(std::fabs(now.ratio - 3.0) < 1e-9, "honest: 105 / 35 = 3 (%.3f)", now.ratio);
		CHECK(now.steel < old.steel && now.hands > old.hands, "honest: lighter and stronger than before (steel %.2f < %.2f)", now.steel, old.steel);
		herostr::In light = Hero(35, 0, 100, 100, 0);
		light.honest = true;
		const herostr::Out l = Factor(light);
		CHECK(std::isfinite(l.ratio) && std::fabs(l.ratio - 35.0 / herostr::kMinReq) < 1e-9, "honest: a weapon asking none counts as %.0f (ratio %.2f)", herostr::kMinReq, l.ratio);
		CHECK(l.steel >= herostr::kSteelMin && l.steel < 1.0, "honest: a light weapon is lighter but keeps the steel's floor, never nothing (%.2f)", l.steel);
		herostr::In weak = Hero(10, 100, 80, 100, 1, true);
		weak.honest = true;
		const herostr::Out w = Factor(weak);
		CHECK(w.steel == herostr::kSteelMax && w.hands == herostr::kHandsMin, "honest: a weak Sorceress with a Great Sword at the clamps (%.2f %.2f)", w.steel, w.hands);
		herostr::In none = Hero(0, 35);
		none.honest = true;
		const herostr::Out z = Factor(none);
		CHECK(z.known && z.steel == herostr::kSteelMax && z.hands == herostr::kHandsMin, "honest: no strength at all: the clamps, no NaN (%.2f %.2f)", z.steel, z.hands);
		double lastSteel = 2.0;
		for (double str = 0; str <= 400; str += 1) {
			herostr::In m = Hero(str, 60);
			m.honest = true;
			const herostr::Out o = Factor(m);
			CHECK(std::isfinite(o.steel) && o.steel <= lastSteel + 1e-12, "honest: monotone at %.0f", str);
			lastSteel = o.steel;
		}
	}

	if (g_fails) {
		std::printf("hero_strength_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("hero_strength_test: not read, the reference, monotone and smooth and clamped, the classes, \"Strength matters\", the requirement from the game and a typical one: all passed\n");
	return 0;
}
