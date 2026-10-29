// vr/throw_aim.h on its own: the auto-aim cone around a throw's line, the ballistics of a thrown potion and the +-25%
// window of its auto-aim. No game, no headset.
//   throw_aim_test              exit code 0 = every check passed

#include "../../vr/throw_aim.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace throwaim;

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

constexpr double kPi = 3.14159265358979323846;

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// A monster `cells` from the hero at `deg` off the heading (positive turns left of it: counter-clockwise).
Monster At(uint32_t id, float hx, float hy, double headingDeg, double offDeg, float cells) {
	const double a = (headingDeg + offDeg) * kPi / 180.0;
	return {id, hx + cells * (float)std::cos(a), hy + cells * (float)std::sin(a)};
}

void TestCone() {
	std::printf("the cone\n");
	const float hx = 100.0f, hy = 100.0f;
	// inside and outside, along +x with a 15 degree half-angle
	{
		const Monster in[] = {{1, 110.0f, 100.0f}};
		EXPECT(PickInCone(hx, hy, 1, 0, 15, 1, 40, in, 1).found, "straight ahead");
		const Monster a[] = {{1, 110.0f, 102.0f}};   // 11.3 deg
		EXPECT(PickInCone(hx, hy, 1, 0, 15, 1, 40, a, 1).found, "11.3 deg off");
		const Monster b[] = {{1, 110.0f, 104.0f}};   // 21.8 deg
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, 1, 40, b, 1).found, "21.8 deg off");
		const Monster c[] = {{1, 110.0f, 97.0f}};    // the other side, 16.7 deg
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, 1, 40, c, 1).found, "16.7 deg off the other way");
		const Monster d[] = {{1, 110.0f, 97.5f}};    // 14.0 deg
		EXPECT(PickInCone(hx, hy, 1, 0, 15, 1, 40, d, 1).found, "14 deg off the other way");
	}
	// the smallest angle wins, not the nearest
	{
		const Monster m[] = {{1, 110.0f, 102.0f}, {2, 130.0f, 100.0f}, {3, 105.0f, 101.5f}};   // 11.3, 0, 16.7 deg
		const Pick p = PickInCone(hx, hy, 1, 0, 20, 1, 40, m, 3);
		EXPECT(p.found && p.id == 2 && p.angleDeg < 0.1f && Near(p.cells, 30.0f, 0.01f), "id %u angle %.2f cells %.2f", p.id, p.angleDeg, p.cells);
		EXPECT(Near(p.x, 130.0f, 1e-4f) && Near(p.y, 100.0f, 1e-4f), "its place");
	}
	// angles within half a degree are one: then the nearest
	{
		const Monster m[] = {{1, 130.0f, 100.2f}, {2, 110.0f, 100.0f}, {3, 120.0f, 100.1f}};   // 0.4, 0, 0.3 deg
		const Pick p = PickInCone(hx, hy, 1, 0, 15, 1, 40, m, 3);
		EXPECT(p.found && p.id == 2, "a tie of angle goes to the nearest: id %u", p.id);
		const Monster far[] = {{1, 130.0f, 101.0f}, {2, 110.0f, 101.5f}};   // 1.9, 8.5 deg: a clear difference
		const Pick q = PickInCone(hx, hy, 1, 0, 15, 1, 40, far, 2);
		EXPECT(q.found && q.id == 1, "a clear difference of angle goes to the smaller: id %u", q.id);
	}
	// behind the hero, at the side, none at all
	{
		const Monster behind[] = {{1, 90.0f, 100.0f}, {2, 100.0f, 110.0f}, {3, 90.0f, 101.0f}};
		EXPECT(!PickInCone(hx, hy, 1, 0, 45, 1, 40, behind, 3).found, "behind and abeam");
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, 1, 40, nullptr, 0).found, "no monsters");
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, 1, 40, behind, 0).found, "an empty list");
	}
	// the cone is clamped to 45; 0 and less is off
	{
		const Monster m[] = {{1, 110.0f, 109.0f}};   // 42 deg
		EXPECT(PickInCone(hx, hy, 1, 0, 100, 1, 40, m, 1).found, "a cone of 100 is 45: 42 deg is in");
		const Monster n[] = {{1, 110.0f, 111.0f}};   // 48 deg
		EXPECT(!PickInCone(hx, hy, 1, 0, 100, 1, 40, n, 1).found, "a cone of 100 is 45: 48 deg is out");
		const Monster z[] = {{1, 110.0f, 100.0f}};
		EXPECT(!PickInCone(hx, hy, 1, 0, 0, 1, 40, z, 1).found, "0 degrees is off");
		EXPECT(!PickInCone(hx, hy, 1, 0, -5, 1, 40, z, 1).found, "negative is off");
	}
	// the distance window
	{
		const Monster m[] = {{1, 100.5f, 100.0f}, {2, 141.0f, 100.0f}};
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, 1, 40, m, 2).found, "0.5 cells is too near, 41 too far");
		const Monster k[] = {{1, 139.9f, 100.0f}};
		EXPECT(PickInCone(hx, hy, 1, 0, 15, 1, 40, k, 1).found, "39.9 cells is in");
	}
	// a heading that is not unit length, a zero heading, non-finite numbers
	{
		const Monster m[] = {{1, 110.0f, 100.0f}};
		EXPECT(PickInCone(hx, hy, 7.0f, 0.0f, 15, 1, 40, m, 1).found, "a long heading");
		EXPECT(!PickInCone(hx, hy, 0.0f, 0.0f, 15, 1, 40, m, 1).found, "no heading");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const Monster bad[] = {{1, nan, 100.0f}, {2, 110.0f, nan}, {3, 110.0f, 100.0f}};
		const Pick p = PickInCone(hx, hy, 1, 0, 15, 1, 40, bad, 3);
		EXPECT(p.found && p.id == 3, "non-finite monsters are skipped: id %u", p.id);
		EXPECT(!PickInCone(nan, hy, 1, 0, 15, 1, 40, m, 1).found, "a NaN hero");
	}
	// every heading: the pick and its angle are those of the arrangement, whichever way the throw goes
	{
		int wrong = 0;
		double worst = 0.0;
		for (int deg = 0; deg < 360; deg += 7) {
			const Monster m[] = {At(1, hx, hy, deg, 9.0, 20.0f), At(2, hx, hy, deg, -4.0, 14.0f), At(3, hx, hy, deg, 30.0, 8.0f), At(4, hx, hy, deg + 180, 0.0, 6.0f)};
			const float dx = (float)std::cos(deg * kPi / 180.0), dy = (float)std::sin(deg * kPi / 180.0);
			const Pick p = PickInCone(hx, hy, dx, dy, 15, 1, 40, m, 4);
			if (!p.found || p.id != 2) { ++wrong; continue; }
			worst = std::fmax(worst, std::fabs(p.angleDeg - 4.0));
		}
		EXPECT(wrong == 0 && worst < 0.05, "%d wrong picks, worst angle error %.3f", wrong, worst);
	}
}

void TestPotionWindow() {
	std::printf("a potion's window\n");
	float lo, hi;
	PotionWindow(12.0f, &lo, &hi);
	EXPECT(Near(lo, 9.0f, 1e-4f) && Near(hi, 15.0f, 1e-4f), "window %.2f .. %.2f", lo, hi);
	const float hx = 50.0f, hy = 50.0f;
	// monsters on the line at 8.9, 9.1, 14.9 and 15.1 cells; the window decides, the cone only the angle
	const float at[] = {8.9f, 9.1f, 14.9f, 15.1f};
	bool in[4];
	for (int i = 0; i < 4; ++i) {
		const Monster m[] = {{1, hx + at[i], hy}};
		in[i] = PickInCone(hx, hy, 1, 0, 15, lo, hi, m, 1).found;
	}
	EXPECT(!in[0] && in[1] && in[2] && !in[3], "%d %d %d %d", in[0], in[1], in[2], in[3]);
	// a short lob does not snap to a far monster; the nearer one in the window does
	{
		float l, h;
		PotionWindow(5.0f, &l, &h);
		const Monster m[] = {{1, hx + 30.0f, hy}, {2, hx + 5.5f, hy + 0.8f}};
		const Pick p = PickInCone(hx, hy, 1, 0, 15, l, h, m, 2);
		EXPECT(p.found && p.id == 2, "id %u", p.id);
		const Monster far[] = {{1, hx + 30.0f, hy}};
		EXPECT(!PickInCone(hx, hy, 1, 0, 15, l, h, far, 1).found, "only a far monster: none");
	}
}

// What a usual swing lands at (the default 800%), printed: peak 3 / 4 / 5 m/s, the hand at 1.4 / 1.5 / 1.6 m, level and 20 degrees down / up.
void TestNormalSwings() {
	std::printf("what a usual swing lands at (cells; 800%% of the real throw, cap 48)\n");
	for (float upm : {3.7f, 4.0f}) {
		std::printf("  %.1f game units per metre:\n", upm);
		for (float elev : {-20.0f, 0.0f, 20.0f}) {
			for (float h : {1.4f, 1.5f, 1.6f}) {
				std::printf("    %+3.0f deg, hand %.1f m:", elev, h);
				for (float speed : {3.0f, 4.0f, 5.0f}) {
					const float e = elev * 0.017453292f;
					const Reach r = ReachFor(speed * std::cos(e), speed * std::sin(e), h, kDefaultReachPct, upm);
					std::printf("  %.0f m/s = %4.1f cells%s", speed, r.cells, r.clamped ? "*" : " ");
				}
				std::printf("\n");
			}
		}
	}
	// level, a hand at 1.5 m, 3 to 5 m/s at 4 units per metre: 13 - 22 m of the game's ground (27 - 44 cells), none clamped
	for (float speed : {3.0f, 4.0f, 5.0f}) {
		const Reach r = ReachFor(speed, 0.0f, 1.5f, kDefaultReachPct, 4.0f);
		const float metres = r.cells * 2.0f / 4.0f;
		EXPECT(r.cells > 26.0f && r.cells < 45.0f && !r.clamped && metres > 13.0f && metres < 22.5f, "%.0f m/s lands at %.1f cells = %.1f m", speed, r.cells, metres);
	}
	// what a 5 and a 10 m/s swing give, in metres of the game's ground, at 3.2 (48 cells = 30 m) and 4 units per metre; the cap holds the hard one
	std::printf("  level, hand 1.5 m, 800%%:\n");
	for (float upm : {3.2f, 4.0f}) {
		for (float speed : {5.0f, 10.0f}) {
			const Reach r = ReachFor(speed, 0.0f, 1.5f, kDefaultReachPct, upm);
			std::printf("    %.0f m/s at %.1f units/m: %.1f m asked (real %.2f m) -> %.1f cells = %.1f m%s\n", speed, upm, r.metres * 8.0f, r.metres, r.cells, r.cells * 2.0f / upm, r.clamped ? "  (capped)" : "");
		}
	}
	{
		const Reach five = ReachFor(5.0f, 0.0f, 1.5f, kDefaultReachPct, 3.2f), ten = ReachFor(10.0f, 0.0f, 1.5f, kDefaultReachPct, 3.2f);
		EXPECT(Near(five.cells * 2.0f / 3.2f, 22.1f, 0.2f) && !five.clamped, "5 m/s: %.1f m", five.cells * 2.0f / 3.2f);
		EXPECT(Near(ten.cells, kMaxCells, 1e-4f) && ten.clamped && Near(ten.cells * 2.0f / 3.2f, 30.0f, 0.01f), "10 m/s: capped at %.1f cells = %.1f m", ten.cells, ten.cells * 2.0f / 3.2f);
	}
}

// The point itself: 48 cells out for a throw, 40 for everything else; the map's edge and the limits still hold.
void TestLongPoint() {
	std::printf("the long point\n");
	float px, py;
	// the default is 40 whatever is asked
	EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 0.0f, 48.0f, &px, &py) && Near(px, 140.0f, 1e-3f), "default cap: x %.2f", px);
	// a throw asks for 48 and gets it
	EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 0.0f, 48.0f, &px, &py, kMaxCells) && Near(px, 148.0f, 1e-3f) && Near(py, 100.0f, 1e-3f), "48 for a throw: x %.2f", px);
	EXPECT(aimpoint::Point(100.0f, 100.0f, 0.0f, -1.0f, 48.0f, &px, &py, kMaxCells) && Near(py, 52.0f, 1e-3f), "48 along the other axis: y %.2f", py);
	// never more than 48, whatever a caller says; junk is the default 40
	EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 0.0f, 90.0f, &px, &py, 90.0f) && Near(px, 148.0f, 1e-3f), "asked 90: %.2f", px);
	EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 0.0f, 90.0f, &px, &py, std::numeric_limits<float>::quiet_NaN()) && Near(px, 140.0f, 1e-3f), "NaN cap: %.2f", px);
	// along a diagonal 48 cells is 34 on an axis: far inside the game's 50
	EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 1.0f, 48.0f, &px, &py, kMaxCells) && Near(std::hypot(px - 100.0f, py - 100.0f), 48.0f, 1e-3f) && px - 100.0f < 50.0f, "diagonal");
	// the map's edge still takes it nearer, and a point nearer than 3 cells is still refused
	EXPECT(aimpoint::Point(65500.0f, 100.0f, 1.0f, 0.0f, 48.0f, &px, &py, kMaxCells) && Near(px, 65534.0f, 1e-2f), "the edge: x %.2f", px);
	EXPECT(!aimpoint::Point(65533.0f, 100.0f, 1.0f, 0.0f, 48.0f, &px, &py, kMaxCells), "no room");
	// the hero may step 2 cells before the game checks: 48 + 2 is the game's 50 on an axis
	EXPECT(48.0f + 2.0f <= (float)aimpoint::kRefuseBeyondCells, "a margin of 2 cells");
	// CellsFor: metres final, no percentage, the same clamps
	EXPECT(Near(CellsFor(10.0f, 4.0f).cells, 20.0f, 1e-4f) && !CellsFor(10.0f, 4.0f).clamped, "10 m = 20 cells at 4 units/m");
	EXPECT(Near(CellsFor(10.0f, 3.2f).cells, 16.0f, 1e-4f), "10 m = 16 cells at 3.2 units/m");
	EXPECT(Near(CellsFor(40.0f, 4.0f).cells, kMaxCells, 1e-4f) && CellsFor(40.0f, 4.0f).clamped, "40 m: capped");
	EXPECT(Near(CellsFor(30.0f, 3.2f).cells, 48.0f, 1e-3f), "30 m at 3.2 units/m is the cap: %.2f", CellsFor(30.0f, 3.2f).cells);
	EXPECT(Near(CellsFor(0.5f, 4.0f).cells, kMinCells, 1e-4f) && CellsFor(0.5f, 4.0f).clamped, "0.5 m: the floor");
	EXPECT(Near(CellsFor(0.0f, 4.0f).cells, kMinCells, 1e-4f) && CellsFor(0.0f, 4.0f).metres == 0.0f, "nothing: the floor");
	EXPECT(Near(CellsFor(std::numeric_limits<float>::quiet_NaN(), 4.0f).cells, kMinCells, 1e-4f), "NaN metres");
	// the weight model's landing is final: the percentage is NOT applied again; no landing = the hand's own estimate
	{
		const Reach w800 = ThrowReach(12.0f, 5.0f, 0.0f, 1.5f, 800.0f, 4.0f), w100 = ThrowReach(12.0f, 5.0f, 0.0f, 1.5f, 100.0f, 4.0f), w1500 = ThrowReach(12.0f, 5.0f, 0.0f, 1.5f, 1500.0f, 4.0f);
		EXPECT(Near(w800.cells, 24.0f, 1e-3f) && Near(w100.cells, 24.0f, 1e-3f) && Near(w1500.cells, 24.0f, 1e-3f), "the weight landing ignores the percentage: %.2f %.2f %.2f", w800.cells, w100.cells, w1500.cells);
		EXPECT(Near(w800.metres, 12.0f, 1e-4f), "its metres are the weight model's");
		const Reach own = ThrowReach(0.0f, 5.0f, 0.0f, 1.5f, 800.0f, 4.0f);
		EXPECT(Near(own.cells, ReachFor(5.0f, 0.0f, 1.5f, 800.0f, 4.0f).cells, 1e-4f), "no weight landing: the hand's estimate (%.2f)", own.cells);
		EXPECT(Near(ThrowReach(-3.0f, 5.0f, 0.0f, 1.5f, 800.0f, 4.0f).cells, own.cells, 1e-4f), "a negative landing is none");
		EXPECT(Near(ThrowReach(0.4f, 5.0f, 0.0f, 1.5f, 800.0f, 4.0f).cells, kMinCells, 1e-4f), "a weight landing of 0.4 m: the 3-cell floor");
		EXPECT(Near(ThrowReach(80.0f, 5.0f, 0.0f, 1.5f, 800.0f, 4.0f).cells, kMaxCells, 1e-4f), "a weight landing of 80 m: the 48-cell cap");
	}
}

void TestBallistics() {
	std::printf("ballistics\n");
	// level throw from 1.5 m: t = sqrt(2 h / g)
	EXPECT(Near(LandingDistanceM(5.0f, 0.0f, 1.5f), 5.0f * std::sqrt(3.0f / 9.81f), 1e-3f), "level %.3f", LandingDistanceM(5.0f, 0.0f, 1.5f));
	EXPECT(Near(LandingDistanceM(5.0f, 0.0f, 1.5f), 2.765f, 0.01f), "level, the number");
	// at the ground and level: nowhere
	EXPECT(LandingDistanceM(5.0f, 0.0f, 0.0f) == 0.0f, "from the ground, level");
	// from the ground, thrown up: t = 2 vy / g
	EXPECT(Near(LandingDistanceM(4.0f, 4.0f, 0.0f), 4.0f * 8.0f / 9.81f, 1e-3f), "from the ground, up: %.3f", LandingDistanceM(4.0f, 4.0f, 0.0f));
	// thrown down from 1.5 m: a short flight
	EXPECT(Near(LandingDistanceM(4.0f, -4.0f, 1.5f), 1.117f, 0.01f), "down: %.3f", LandingDistanceM(4.0f, -4.0f, 1.5f));
	EXPECT(LandingDistanceM(4.0f, -4.0f, 1.5f) < LandingDistanceM(4.0f, 0.0f, 1.5f), "down is shorter than level");
	EXPECT(LandingDistanceM(4.0f, 4.0f, 1.5f) > LandingDistanceM(4.0f, 0.0f, 1.5f), "up is longer than level");
	// steeply up: long in the air, little forward
	EXPECT(Near(LandingDistanceM(0.5f, 10.0f, 1.0f), 0.5f * (10.0f + std::sqrt(100.0f + 19.62f)) / 9.81f, 1e-3f), "steep up");
	// straight down: nothing forward
	EXPECT(LandingDistanceM(0.0f, -6.0f, 1.5f) == 0.0f, "straight down");
	// no speed, no height, junk
	EXPECT(LandingDistanceM(0.0f, 0.0f, 1.5f) == 0.0f, "zero speed");
	EXPECT(Near(LandingDistanceM(5.0f, 0.0f, -2.0f), 0.0f, 1e-6f), "a hand below the ground is the ground");
	EXPECT(LandingDistanceM(std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f) == 0.0f, "NaN");
	EXPECT(LandingDistanceM(-3.0f, 0.0f, 1.0f) == 0.0f, "a negative horizontal speed");
	// higher or faster is farther
	EXPECT(LandingDistanceM(6.0f, 1.0f, 1.5f) > LandingDistanceM(5.0f, 1.0f, 1.5f), "faster is farther");
	EXPECT(LandingDistanceM(5.0f, 1.0f, 1.8f) > LandingDistanceM(5.0f, 1.0f, 1.2f), "higher is farther");

	// metres to cells: upm 4 game units per metre, a cell is two units: 2 cells a metre
	{
		const Reach r = ReachFor(5.0f, 0.0f, 1.5f, 100.0f, 4.0f);
		EXPECT(Near(r.cells, 5.53f, 0.02f) && !r.clamped && Near(r.metres, 2.765f, 0.01f), "cells %.2f clamped %d", r.cells, r.clamped);
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 200.0f, 4.0f).cells, 11.06f, 0.04f), "200%%");
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 50.0f, 4.0f).cells, kMinCells, 1e-4f) && ReachFor(5.0f, 0.0f, 1.5f, 50.0f, 4.0f).clamped, "50%%: 2.77 cells, held to the minimum");
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 400.0f, 4.0f).cells, 22.12f, 0.08f), "400%%");
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 100.0f, 6.0f).cells, 8.30f, 0.03f), "a bigger world: upm 6");
		// the slider is held to 50..1500, junk is the default
		EXPECT(kMinReachPct == 50.0f && kMaxReachPct == 1500.0f, "the slider's ends");
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 10.0f, 4.0f).cells, ReachFor(5.0f, 0.0f, 1.5f, 50.0f, 4.0f).cells, 1e-4f), "pct below 50");
		EXPECT(Near(ReachFor(1.5f, 0.0f, 1.5f, 3000.0f, 4.0f).cells, ReachFor(1.5f, 0.0f, 1.5f, 1500.0f, 4.0f).cells, 1e-4f) && !ReachFor(1.5f, 0.0f, 1.5f, 1500.0f, 4.0f).clamped, "pct above 1500: %.2f", ReachFor(1.5f, 0.0f, 1.5f, 1500.0f, 4.0f).cells);
		EXPECT(Near(ReachFor(1.5f, 0.0f, 1.5f, 1500.0f, 4.0f).cells, 24.9f, 0.1f), "1500%% of a 0.83 m throw at 2 cells a metre: %.2f", ReachFor(1.5f, 0.0f, 1.5f, 1500.0f, 4.0f).cells);
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, std::numeric_limits<float>::quiet_NaN(), 4.0f).cells, ReachFor(5.0f, 0.0f, 1.5f, kDefaultReachPct, 4.0f).cells, 1e-4f), "pct NaN is the default");
		EXPECT(kDefaultReachPct == 800.0f && Near(ReachFor(5.0f, 0.0f, 1.5f, kDefaultReachPct, 4.0f).cells, 44.24f, 0.15f), "the default 800%%: %.2f", ReachFor(5.0f, 0.0f, 1.5f, kDefaultReachPct, 4.0f).cells);
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, kDefaultReachPct, 4.0f).metres, 2.765f, 0.01f), "metres stay the real throw's");
		// the floor is aim_point.h's, the cap its long one (48: the game refuses 50 cells off, 2 left for the hero's step)
		EXPECT(kMinCells == aimpoint::kMinCells && kMaxCells == aimpoint::kLongMaxCells && kMaxCells == 48.0f && kMaxCells <= (float)aimpoint::kRefuseBeyondCells - 2.0f, "the clamps are aim_point.h's");
		EXPECT(aimpoint::kMaxCells == 40.0f, "every other cast keeps 40");
		{
			float px, py;
			const Reach lo = ReachFor(0.0f, 0.0f, 1.5f, 100.0f, 4.0f);
			EXPECT(aimpoint::Point(100.0f, 100.0f, 1.0f, 0.0f, lo.cells, &px, &py), "the shortest lob (%.2f cells) is a point aimpoint accepts", lo.cells);
		}
	}
	// the clamps: at least 3 cells, at most 48
	{
		const Reach lo = ReachFor(1.0f, 0.0f, 0.2f, 100.0f, 4.0f);
		EXPECT(Near(lo.cells, kMinCells, 1e-6f) && lo.clamped, "short: %.2f clamped %d", lo.cells, lo.clamped);
		const Reach none = ReachFor(0.0f, 0.0f, 1.5f, 100.0f, 4.0f);
		EXPECT(Near(none.cells, kMinCells, 1e-6f) && none.clamped && none.metres == 0.0f, "no speed: the minimum");
		const Reach hi = ReachFor(12.0f, 5.0f, 1.5f, 200.0f, 4.0f);   // ~15 m x 2 x 2 cells
		EXPECT(Near(hi.cells, kMaxCells, 1e-6f) && hi.clamped, "far: %.2f clamped %d", hi.cells, hi.clamped);
		const Reach mid = ReachFor(12.0f, 5.0f, 1.5f, 100.0f, 4.0f);   // ~15 m x 2 = 30 cells
		EXPECT(Near(mid.cells, 30.1f, 0.3f) && !mid.clamped, "a hard throw: %.2f", mid.cells);
		EXPECT(Near(ReachFor(5.0f, 0.0f, 1.5f, 100.0f, 400.0f).cells, ReachFor(5.0f, 0.0f, 1.5f, 100.0f, 12.0f).cells, 1e-4f), "upm is held to 12");
	}
}

}  // namespace

int main() {
	TestCone();
	TestPotionWindow();
	TestLongPoint();
	TestBallistics();
	TestNormalSwings();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
