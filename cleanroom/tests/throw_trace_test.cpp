// vr/throw_trace.h on its own: the throw tracer's lines for synthetic hero and missile snapshots. No game, no headset.
//   throw_trace_test            exit code 0 = every check passed

#include "../../vr/throw_trace.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace throwtrace;

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

struct Run {
	Tracker t;
	std::vector<std::string> lines;
	double base = 100.0;
	void Feed(double at, bool modeKnown, uint32_t mode, const std::vector<Missile>& ms) {
		Snapshot s;
		s.t = base + at;
		s.heroModeKnown = modeKnown;
		s.heroMode = mode;
		s.heroKnown = true;
		s.hx = 2000.0f; s.hz = 3000.0f;
		s.m = ms.data();
		s.n = ms.size();
		Line out[24];
		const size_t k = t.Step(s, out, 24);
		for (size_t i = 0; i < k; ++i) lines.push_back(out[i].text);
	}
	bool Has(const char* what) const {
		for (const std::string& l : lines) if (l.find(what) != std::string::npos) return true;
		return false;
	}
	int Count(const char* what) const {
		int n = 0;
		for (const std::string& l : lines) if (l.find(what) != std::string::npos) ++n;
		return n;
	}
};

// A missile at (dx, dy) cells from the hero (the hero is at 2000, 3000 world units: a cell is 2 units).
Missile At(uint32_t id, uint32_t cls, double dx, double dy, float life = 20.0f) {
	Missile m;
	m.id = id; m.cls = cls;
	m.x = 2000.0f + (float)dx * 2.0f; m.z = 3000.0f + (float)dy * 2.0f;
	m.life = life;
	return m;
}

void TestGoodThrow() {
	std::printf("a throw that works: the potion flies, an effect appears where it ends\n");
	Run r;
	r.t.Arm(r.base);
	EXPECT(r.t.Active(), "armed");
	r.Feed(0.00, true, 1, {});
	EXPECT(r.Has("the hero's mode is 1"), "first mode told");
	r.Feed(0.05, true, 11, {});
	EXPECT(r.Has("mode 1 -> 11 (throw)"), "into throw");
	r.Feed(0.10, true, 11, {});
	EXPECT(r.lines.size() == 2, "nothing new, nothing told: %zu", r.lines.size());
	r.Feed(0.35, true, 11, {At(5, 44, 0.8, -0.5, 24.0f)});
	EXPECT(r.Has("POTION missile #5 class 44 (oilpotion (Fulminating)) appeared 0.9 cells"), "appeared");
	r.Feed(0.55, true, 1, {At(5, 44, 6.0, -10.0, 17.0f)});
	EXPECT(r.Has("mode 11 -> 1 (neutral)") && r.Has("his throw animation took 500 ms"), "back to neutral after 500 ms");
	r.Feed(0.80, true, 1, {At(5, 44, 9.0, -15.0, 11.0f)});
	EXPECT(r.Has("potion #5 at 17.5 cells"), "flight told");
	const size_t before = r.lines.size();
	r.Feed(0.84, true, 1, {At(5, 44, 9.5, -15.8, 10.0f)});
	EXPECT(r.lines.size() == before, "not told again within 0.25 s");
	r.Feed(1.30, true, 1, {});
	EXPECT(r.Has("potion #5 is GONE: it lived 0.95 s, last seen"), "gone");
	r.Feed(1.34, true, 1, {At(9, 29, 10.0, -16.0, 12.0f)});
	EXPECT(r.Has("an effect missile #9 class 29 (fireexplode) appeared"), "effect near where it vanished");
	r.Feed(5.2, true, 1, {});
	EXPECT(r.Has("over: 1 potion missile(s) seen, 1 vanished, 1 effect missile(s)") && !r.Has("NO explosion"), "summary: fine");
	EXPECT(!r.t.Active(), "done");
	const size_t n = r.lines.size();
	r.Feed(5.4, true, 1, {At(6, 44, 1, 1)});
	EXPECT(r.lines.size() == n, "quiet after the summary");
}

void TestNoPotion() {
	std::printf("a throw the game made no missile for\n");
	Run r;
	r.t.Arm(r.base);
	r.Feed(0.0, true, 1, {});
	r.Feed(0.05, true, 11, {});
	r.Feed(0.6, true, 1, {});
	r.Feed(5.1, true, 1, {At(77, 7, 5, 5)});   // somebody else's missile
	EXPECT(r.Has("over: 0 potion missile(s) seen") && r.Has("made NO potion missile"), "no potion told");
}

void TestNoExplosion() {
	std::printf("a potion that ends without an effect\n");
	Run r;
	r.t.Arm(r.base);
	r.Feed(0.0, true, 1, {});
	r.Feed(0.4, true, 1, {At(3, 46, 1, 1, 24.0f)});
	r.Feed(1.2, true, 1, {At(3, 46, 20, 3, 2.0f)});
	r.Feed(1.5, true, 1, {});
	EXPECT(r.Has("is GONE"), "gone");
	// a missile far from the spot is not its effect
	r.Feed(1.6, true, 1, {At(12, 29, 40, 30)});
	EXPECT(!r.Has("an effect missile"), "far away: not its effect");
	r.Feed(5.1, true, 1, {});
	EXPECT(r.Has("over: 1 potion missile(s) seen, 1 vanished, 0 effect") && r.Has("NO explosion effect appeared"), "summary: no explosion");
}

void TestEffectBeforeRemoval() {
	std::printf("the effect appears while the potion is still listed\n");
	Run r;
	r.t.Arm(r.base);
	r.Feed(0.0, true, 1, {});
	r.Feed(0.4, true, 1, {At(3, 45, 1, 1)});
	r.Feed(1.0, true, 1, {At(3, 45, 15, 10, 0.5f), At(8, 51, 15.5, 10, 12.0f)});
	EXPECT(r.Has("an effect missile #8 class 51 (explosivepotionexp) appeared") && r.Has("from the potion still flying"), "effect told");
	r.Feed(1.04, true, 1, {At(8, 51, 15.5, 10, 11.0f)});
	r.Feed(5.1, true, 1, {});
	EXPECT(r.Has("1 effect missile"), "counted");
}

void TestLimitsAndEdges() {
	std::printf("limits and edges\n");
	// not armed: nothing
	{
		Run r;
		r.Feed(0.0, true, 11, {At(1, 44, 1, 1)});
		EXPECT(r.lines.empty(), "idle tracker is silent");
	}
	// the mode unknown: no mode lines, the missiles still told
	{
		Run r;
		r.t.Arm(r.base);
		r.Feed(0.0, false, 0, {At(1, 47, 2, 2)});
		EXPECT(!r.Has("mode") && r.Has("POTION missile #1 class 47"), "unknown mode");
	}
	// the hero not known: distances are 0 but nothing breaks
	{
		Run r;
		r.t.Arm(r.base);
		Snapshot s;
		s.t = r.base + 0.1;
		const Missile m = At(2, 48, 3, 3);
		s.m = &m; s.n = 1;
		Line out[24];
		EXPECT(r.t.Step(s, out, 24) == 1 && std::strstr(out[0].text, "appeared 0.0 cells"), "hero unknown: %s", out[0].text);
	}
	// at most 80 lines for one throw
	{
		Run r;
		r.t.Arm(r.base);
		std::vector<Missile> many;
		for (uint32_t i = 0; i < 6; ++i) many.push_back(At(100 + i, 44, i, i));
		for (int k = 0; k < 400; ++k) {
			many[0].id = 1000 + (uint32_t)k;   // a new potion every frame: appears, vanishes ...
			r.Feed(0.01 * k, true, (uint32_t)(k % 2 ? 11 : 1), many);
		}
		EXPECT((int)r.lines.size() <= kMaxLines, "capped: %zu", r.lines.size());
	}
	// Arm resets everything
	{
		Run r;
		r.t.Arm(r.base);
		r.Feed(0.0, true, 11, {At(1, 44, 1, 1)});
		r.t.Arm(r.base + 10.0);
		r.lines.clear();
		r.base += 10.0;
		r.Feed(0.0, true, 1, {});
		EXPECT(r.Has("the hero's mode is 1") && !r.Has("is GONE"), "fresh after Arm");
	}
	// class names
	EXPECT(IsPotion(44) && IsPotion(49) && !IsPotion(43) && !IsPotion(50), "potion classes");
	EXPECT(std::strcmp(ClassName(29), "fireexplode") == 0 && ClassName(7)[0] == 0, "names");
}

}  // namespace

int main() {
	TestGoodThrow();
	TestNoPotion();
	TestNoExplosion();
	TestEffectBeforeRemoval();
	TestLimitsAndEdges();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
