// [weight] leap_landing_scatter (vr/leap_scatter.h): the landing seen from the leap's arc (once, only past a real leap's height, not a step or a stair) and
// the leap's way (take-off to landing); who is thrown (within the radius; one the game knocked back from farther, within its own radius); ONE cell outward
// (Serg: «вокруг разлетались на 1 клетку») - the neighbour cell rounded from the way, the drawn throw that cell's length over the fall, the same for near and
// far, nearness only in the arc; landed on (Serg: «если он прыгнул в монстра монстр сдвигается на 1 клетку»): moved whatever it weighs, dead centre the leap's
// way; a big one only staggers; bad input.

#include "../../vr/leap_scatter.h"

#include <cmath>
#include <cstdio>

namespace {

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

namespace ls = leapscatter;
constexpr float kUpm = 3.2f;   // render units a metre: a cell 0.625 m

float Across(const ls::Throw& t) { return std::hypot(t.impulse[0], t.impulse[2]); }

}  // namespace

int main() {
	{   // the landing: a leap's arc (up to 13 units) lands once and knows its way; a step up of 2 units, a stair, never
		ls::Landing l;
		const float arc[] = {0.0f, 2.0f, 7.0f, 12.0f, 13.5f, 9.0f, 3.0f, 0.8f, 0.2f, 0.1f, 0.0f};
		int landed = 0, at = -1;
		for (int i = 0; i < (int)(sizeof arc / sizeof arc[0]); ++i) {
			const float pos[3] = {100.0f + 2.0f * i, 0.0f, 50.0f - 1.0f * i};   // flying +x, -z
			if (l.Feed(arc[i], 1000 + 50 * i, pos)) { ++landed; at = i; }
		}
		EXPECT(landed == 1 && at == 8 && std::fabs(l.LastPeak() - 13.5f) < 1e-4f && l.LastMs() == 300, "a leap: landed %d at %d peak %.1f %llu ms", landed, at,
		       l.LastPeak(), (unsigned long long)l.LastMs());
		EXPECT(std::fabs(l.Way()[0] - 16.0f) < 1e-3f && std::fabs(l.Way()[1] + 8.0f) < 1e-3f, "the leap's way from its take-off (pose 0, last on the ground): %.1f %.1f",
		       l.Way()[0], l.Way()[1]);
		ls::Landing s;
		bool any = false;
		for (float h : {0.0f, 2.0f, 4.0f, 5.9f, 1.0f, 0.0f}) any = s.Feed(h, 1000) || any;
		EXPECT(!any, "a step or a stair taken for a leap");
		EXPECT(!s.Feed(NAN, 1000), "not a number");
		ls::Landing n;
		for (float h : {0.0f, 8.0f, 0.1f}) n.Feed(h, 1000);   // (no places given: the way not known)
		EXPECT(n.Way()[0] == 0.0f && n.Way()[1] == 0.0f, "a way made up without places");
	}
	const float land[3] = {10.0f, 2.0f, 5.0f};
	{   // one cell outward: the neighbour the way points to; the throw that cell's length over the fall - the same near and far; nearer only higher
		const float near[3] = {10.5f, 2.9f, 5.0f}, edge[3] = {10.0f, 3.0f, 5.0f + 2.9f}, diag[3] = {11.0f, 2.0f, 6.1f};
		const ls::Throw a = ls::Scatter(land, near, 30.0f, 7, false, kUpm), b = ls::Scatter(land, edge, 30.0f, 8, false, kUpm), c = ls::Scatter(land, diag, 30.0f, 9, false, kUpm);
		EXPECT(a.ok && a.fall && a.move && a.cell[0] == 1 && a.cell[1] == 0 && a.impulse[0] > 0.0f && std::fabs(a.impulse[2]) < 1e-4f, "a near one: cell %d,%d imp %.1f %.1f",
		       a.cell[0], a.cell[1], a.impulse[0], a.impulse[2]);
		EXPECT(b.ok && b.cell[0] == 0 && b.cell[1] == 1 && b.impulse[2] > 0.0f, "the edge one: cell %d,%d", b.cell[0], b.cell[1]);
		EXPECT(c.cell[0] == 1 && c.cell[1] == 1, "a diagonal one: cell %d,%d", c.cell[0], c.cell[1]);
		const float cellM = ls::kUnitsPerCell / kUpm;
		EXPECT(std::fabs(a.speedMs * ls::kFallS - cellM) < 1e-4f && std::fabs(b.speedMs - a.speedMs) < 1e-5f, "one cell's throw near and far: %.3f m, %.3f vs %.3f m/s",
		       a.speedMs * ls::kFallS, a.speedMs, b.speedMs);
		EXPECT(std::fabs(c.speedMs * ls::kFallS - cellM * std::sqrt(2.0f)) < 1e-4f, "a diagonal cell's throw: %.3f m", c.speedMs * ls::kFallS);
		EXPECT(std::fabs(Across(a) - 30.0f * a.speedMs) < 1e-3f, "across: the mass times the speed");
		EXPECT(a.upMs > b.upMs && a.impulse[1] > b.impulse[1] && b.upMs >= ls::kUpEdgeMs - 1e-4f && a.upMs <= ls::kUpNearMs, "nearer, higher: %.2f vs %.2f", a.upMs, b.upMs);
	}
	{   // who: past the radius nothing; one the game knocked back is thrown to its own radius; past that nothing; height is no distance
		const float far[3] = {14.0f, 2.0f, 5.0f}, farther[3] = {16.5f, 2.0f, 5.0f}, high[3] = {11.0f, 30.0f, 5.0f};
		EXPECT(!ls::Scatter(land, far, 40.0f, 1, false, kUpm).ok, "4 m off thrown");
		const ls::Throw g = ls::Scatter(land, far, 40.0f, 1, true, kUpm);
		EXPECT(g.ok && g.cell[0] == 1 && std::fabs(g.upMs - ls::kUpEdgeMs) < 1e-4f, "the game's own knockback at 4 m: ok %d cell %d up %.2f", g.ok, g.cell[0], g.upMs);
		EXPECT(!ls::Scatter(land, farther, 40.0f, 1, true, kUpm).ok, "6.5 m off thrown though the game knocked it back");
		EXPECT(ls::Scatter(land, high, 40.0f, 1, false, kUpm).ok, "across the ground only");
	}
	{   // landed on it: dead centre the leap's way; no way: its own, neighbours apart; never zero
		const float on[3] = {10.0f, 2.5f, 5.0f}, way[2] = {-3.0f, 0.1f};
		const ls::Throw w = ls::Scatter(land, on, 30.0f, 100, false, kUpm, way);
		EXPECT(w.ok && w.move && w.cell[0] == -1 && w.cell[1] == 0 && w.impulse[0] < 0.0f, "dead centre, the leap's way: cell %d,%d", w.cell[0], w.cell[1]);
		const ls::Throw a = ls::Scatter(land, on, 30.0f, 100, false, kUpm), b = ls::Scatter(land, on, 30.0f, 101, false, kUpm);
		EXPECT(a.ok && Across(a) > 1.0f && (a.cell[0] != b.cell[0] || a.cell[1] != b.cell[1]), "two on the spot, no way known: cells %d,%d and %d,%d", a.cell[0],
		       a.cell[1], b.cell[0], b.cell[1]);
	}
	{   // a big one keeps its feet and its cell - unless the hero landed on it: then its cell is asked whatever it weighs
		const float near[3] = {11.0f, 2.0f, 5.0f}, under[3] = {10.3f, 2.0f, 5.0f};
		const ls::Throw big = ls::Scatter(land, near, 150.0f, 1, false, kUpm), onBig = ls::Scatter(land, under, 150.0f, 1, false, kUpm);
		EXPECT(big.ok && !big.fall && !big.move, "a 150 kg one 1 m off: fall %d move %d", big.fall, big.move);
		EXPECT(onBig.ok && !onBig.fall && onBig.move && onBig.cell[0] == 1, "a 150 kg one landed on: fall %d move %d cell %d", onBig.fall, onBig.move, onBig.cell[0]);
		const float nan[3] = {NAN, 0.0f, 0.0f};
		EXPECT(!ls::Scatter(land, nan, 30.0f, 1, false, kUpm).ok && !ls::Scatter(land, near, 0.0f, 1, false, kUpm).ok &&
		           !ls::Scatter(nullptr, near, 30.0f, 1, false, kUpm).ok && !ls::Scatter(land, near, 30.0f, 1, false, 0.0f).ok,
		       "bad input thrown");
	}
	std::printf(g_fails ? "leap_scatter_test: %d FAILED\n" : "leap_scatter_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
