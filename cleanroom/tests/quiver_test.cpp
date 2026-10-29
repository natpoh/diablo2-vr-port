// vr/quiver.h on its own: what a thrown weapon in the hand allows, the list that is offered, the cursor, and what the game's cast is told. No game, no headset.
//   quiver_test                 exit code 0 = every check passed

#include "../../vr/quiver.h"

#include <cstdio>
#include <cstring>

using namespace quiver;

namespace {

int g_fails = 0, g_checks = 0;

#define EXPECT(c, ...)                                           \
	do {                                                         \
		++g_checks;                                              \
		if (!(c)) {                                              \
			++g_fails;                                           \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
			std::printf(__VA_ARGS__);                            \
			std::printf("\n");                                   \
		}                                                        \
	} while (0)

// The amazon with every javelin skill, a spear skill and Throw's kin.
const Learned kAmazon[] = {{15, 5, nullptr}, {20, 1, nullptr}, {25, 3, nullptr}, {35, 20, nullptr}, {10, 1, nullptr}, {14, 8, nullptr}, {6, 2, nullptr}};

bool Has(const Pool& p, int32_t id) {
	for (int i = 0; i < p.n; ++i) if (p.e[i].id == id) return true;
	return false;
}

void TestKinds() {
	std::printf("what is in the hand\n");
	// the codes the way gamestate::HandCode packs them
	EXPECT(KindOfCode(Code("jav")) == Kind::Javelin && KindOfCode(Code("9ja")) == Kind::Javelin && KindOfCode(Code("7ts")) == Kind::Javelin, "javelins");
	EXPECT(KindOfCode(Code("ama")) == Kind::Javelin && KindOfCode(Code("am5")) == Kind::Javelin && KindOfCode(Code("amf")) == Kind::Javelin, "the Amazon's javelins");
	EXPECT(KindOfCode(Code("glv")) == Kind::Javelin && KindOfCode(Code("ssp")) == Kind::Javelin, "glaives and short spears are javelins for the game");
	EXPECT(KindOfCode(Code("tkf")) == Kind::Knife && KindOfCode(Code("7bk")) == Kind::Knife, "knives");
	EXPECT(KindOfCode(Code("tax")) == Kind::Axe && KindOfCode(Code("7b8")) == Kind::Axe, "axes");
	EXPECT(KindOfCode(Code("opl")) == Kind::Potion && KindOfCode(Code("gps")) == Kind::Potion, "potions");
	EXPECT(KindOfCode(Code("spr")) == Kind::None && KindOfCode(Code("hax")) == Kind::None && KindOfCode(0) == Kind::None, "a spear, an axe, nothing");
	// the real packing: a space in the fourth byte
	const uint32_t jav = (uint32_t)'j' | ((uint32_t)'a' << 8) | ((uint32_t)'v' << 16) | ((uint32_t)' ' << 24);
	EXPECT(KindOfCode(jav) == Kind::Javelin, "jav + space");
	EXPECT(IsThrownKind(Kind::Javelin) && !IsThrownKind(Kind::None), "thrown kinds");
	EXPECT(std::strcmp(KindName(Kind::Knife), "throwing knife") == 0, "names");
}

void TestCounts() {
	std::printf("the item types a weapon counts as (itemtypes.txt)\n");
	EXPECT(Counts(Kind::Javelin, Type::Thro) && Counts(Kind::Javelin, Type::Jave) && Counts(Kind::Javelin, Type::Spea) && !Counts(Kind::Javelin, Type::Knif), "javelin");
	EXPECT(Counts(Kind::Knife, Type::Thro) && Counts(Kind::Knife, Type::Knif) && !Counts(Kind::Knife, Type::Jave) && !Counts(Kind::Knife, Type::Spea), "knife");
	EXPECT(Counts(Kind::Axe, Type::Thro) && !Counts(Kind::Axe, Type::Jave) && !Counts(Kind::Axe, Type::Knif) && !Counts(Kind::Axe, Type::Spea), "axe");
	EXPECT(Counts(Kind::Potion, Type::Thro) && !Counts(Kind::Potion, Type::Jave), "potion");
	EXPECT(!Counts(Kind::None, Type::Thro), "nothing");
	// the rules table: ids, ranged / melee as skills.txt has them
	EXPECT(RuleOf(15) && RuleOf(15)->ranged && RuleOf(15)->need == Type::Jave, "Poison Javelin");
	EXPECT(RuleOf(35) && RuleOf(35)->ranged, "Lightning Fury");
	EXPECT(RuleOf(140) && RuleOf(140)->dual && RuleOf(140)->need == Type::Thro, "Double Throw needs two");
	EXPECT(RuleOf(14) && !RuleOf(14)->ranged && RuleOf(14)->need == Type::Spea, "Power Strike is a blow in reach");
	EXPECT(RuleOf(73) && RuleOf(73)->need == Type::Knif, "Poison Dagger");
	EXPECT(!RuleOf(2) && !RuleOf(0) && !RuleOf(9), "Throw, Attack, Critical Strike are not offered");
}

void TestPool() {
	std::printf("the list that is offered\n");
	Needs jav;
	jav.weapon = Kind::Javelin;
	const Pool a = Build(jav, kAmazon, sizeof kAmazon / sizeof kAmazon[0]);
	EXPECT(a.n == 1 + 4 + 2, "default + four javelin skills + two spear skills: %d", a.n);
	EXPECT(a.e[0].id == kDefault && a.e[0].ranged && std::strcmp(a.e[0].name, "Throw") == 0, "the default first");
	EXPECT(Has(a, 15) && Has(a, 20) && Has(a, 25) && Has(a, 35) && Has(a, 10) && Has(a, 14), "every learned one");
	EXPECT(!Has(a, 6) && !Has(a, 140) && !Has(a, 19), "Magic Arrow, Double Throw, Impale (not learned) are not");
	// order: ranged ones before the blows
	int lastRanged = -1, firstMelee = 99;
	for (int i = 0; i < a.n; ++i) { if (a.e[i].id >= 0 && a.e[i].ranged) lastRanged = i; if (a.e[i].id >= 0 && !a.e[i].ranged && firstMelee == 99) firstMelee = i; }
	EXPECT(lastRanged < firstMelee, "ranged before melee: %d %d", lastRanged, firstMelee);
	// level 0 = not learned
	const Learned zero[] = {{15, 0, nullptr}, {20, 1, nullptr}};
	const Pool z = Build(jav, zero, 2);
	EXPECT(!Has(z, 15) && Has(z, 20) && z.n == 2, "level 0");
	// the game's own text is used
	const Learned named[] = {{15, 3, "Poison Javelin (game)"}};
	EXPECT(std::strcmp(Build(jav, named, 1).e[1].name, "Poison Javelin (game)") == 0, "game name");
	EXPECT(std::strcmp(Build(jav, zero, 2).e[1].name, "Lightning Bolt") == 0, "fallback name");
	// a knife: no javelin skill; Poison Dagger yes
	Needs knife;
	knife.weapon = Kind::Knife;
	const Learned nec[] = {{73, 4, nullptr}, {15, 1, nullptr}, {10, 1, nullptr}};
	const Pool k = Build(knife, nec, 3);
	EXPECT(k.n == 2 && Has(k, 73) && !Has(k, 15) && !Has(k, 10), "knife: %d", k.n);
	// Double Throw needs a thrown weapon in the other hand
	const Learned bar[] = {{140, 6, nullptr}};
	Needs axe;
	axe.weapon = Kind::Axe;
	EXPECT(Build(axe, bar, 1).n == 1, "one axe: no Double Throw");
	axe.otherHandThrown = true;
	const Pool d = Build(axe, bar, 1);
	EXPECT(d.n == 2 && Has(d, 140), "two thrown weapons: Double Throw");
	EXPECT(d.e[1].ranged, "Double Throw is thrown");
	// a potion or nothing offers nothing
	Needs potion;
	potion.weapon = Kind::Potion;
	EXPECT(Build(potion, bar, 1).n == 0 && Build(Needs(), bar, 1).n == 0, "potion / none: empty");
	// no learned list at all: the default alone
	EXPECT(Build(jav, nullptr, 0).n == 1, "no list: the default");
	// the limit
	Learned many[kMaxEntries * 2];
	for (int i = 0; i < kMaxEntries * 2; ++i) many[i] = {15, 1, nullptr};
	EXPECT(Build(jav, many, kMaxEntries * 2).n <= kMaxEntries, "bounded");
}

void TestSelector() {
	std::printf("the cursor\n");
	Needs jav;
	jav.weapon = Kind::Javelin;
	Selector s;
	EXPECT(s.Count() == 0 && !s.CanCycle() && !s.Step(1) && s.Id() == kDefault && s.Position() == 0, "empty");
	s.Rebuild(Build(jav, kAmazon, 7));
	EXPECT(s.Count() == 7 && s.CanCycle() && s.Id() == kDefault && s.Position() == 1, "built on the default");
	EXPECT(s.Step(1) && s.Id() == 15 && s.Position() == 2, "next");
	EXPECT(s.Step(1) && s.Id() == 20, "next");
	EXPECT(s.Step(-1) && s.Id() == 15, "previous");
	EXPECT(s.Step(-1) && s.Id() == kDefault, "back at the default");
	EXPECT(s.Step(-1) && s.Id() == 14, "previous wraps to the last (%d)", s.Id());
	EXPECT(s.Step(1) && s.Id() == kDefault, "next wraps to the first");
	EXPECT(!s.Step(0), "no direction");
	// the choice stays where it still can
	s.Step(1); s.Step(1);   // Lightning Bolt
	EXPECT(s.Id() == 20, "on Lightning Bolt");
	EXPECT(!s.Rebuild(Build(jav, kAmazon, 7)), "same pool: unchanged");
	EXPECT(s.Id() == 20, "kept");
	const Learned fewer[] = {{15, 1, nullptr}, {20, 1, nullptr}};
	EXPECT(!s.Rebuild(Build(jav, fewer, 2)) && s.Id() == 20, "a smaller pool that still has it: kept");
	const Learned without[] = {{15, 1, nullptr}};
	EXPECT(s.Rebuild(Build(jav, without, 1)) && s.Id() == kDefault, "the skill is gone: back at the default, reported changed");
	// a pool with the default alone cannot cycle
	s.Rebuild(Build(jav, nullptr, 0));
	EXPECT(!s.CanCycle() && !s.Step(1) && s.Id() == kDefault, "nothing to cycle");
	// the weapon goes: empty
	s.Rebuild(Build(jav, kAmazon, 7));
	s.Step(1);
	EXPECT(s.Rebuild(Pool()) && s.Count() == 0 && s.Id() == kDefault, "no weapon");
	s.Rebuild(Build(jav, kAmazon, 7));
	s.Reset();
	EXPECT(s.Id() == kDefault, "reset");
}

void TestAnswer() {
	std::printf("what the cast is told\n");
	const int32_t T = kSkillThrow;
	// no choice: what the throw's swap answered (Throw for a throw, the binding otherwise)
	EXPECT(Answer(T, 0, true, true, true, kDefault, true, true) == T, "default, a throw: Throw");
	EXPECT(Answer(0, 0, true, true, true, kDefault, true, false) == 0, "default, a plain attack: Attack");
	// a ranged skill: both a throw and a plain attack
	EXPECT(Answer(T, 0, true, true, true, 15, true, true) == 15, "Poison Javelin by the throw gesture");
	EXPECT(Answer(0, 0, true, true, true, 15, true, false) == 15, "Poison Javelin by a plain attack");
	// a blow in reach: a plain attack, but a throw stays the Throw
	EXPECT(Answer(0, 0, true, true, true, 14, false, false) == 14, "Power Strike by a plain attack");
	EXPECT(Answer(T, 0, true, true, true, 14, false, true) == T, "a throw is still the Throw when Power Strike is chosen");
	// every other condition leaves it
	EXPECT(Answer(0, 0, false, true, true, 15, true, false) == 0, "not a cast site");
	EXPECT(Answer(0, 0, true, false, true, 15, true, false) == 0, "switched off");
	EXPECT(Answer(0, 0, true, true, false, 15, true, false) == 0, "nothing thrown in hand");
	EXPECT(Answer(5, 5, true, true, true, 15, true, false) == 5, "a slot the player gave another skill keeps it");
	EXPECT(Answer(0, 36, true, true, true, 15, true, false) == 0, "(the swapped answer is the game's when the binding is not Attack)");
}

}  // namespace

int main() {
	TestKinds();
	TestCounts();
	TestPool();
	TestSelector();
	TestAnswer();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
