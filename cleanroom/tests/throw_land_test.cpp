// vr/throw_land.h on its own: when a thrown potion's game throw is pressed (potion_throw_mode), what the game's own potion costs, the pending press.
// No game, no headset.
//   throw_land_test             exit code 0 = every check passed

#include "../../vr/throw_land.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace throwland;

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

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void TestDecide() {
	std::printf("when the press goes\n");
	// mode 0, whatever else
	for (bool potion : {false, true})
		for (bool weight : {false, true}) EXPECT(!Decide(kNow, potion, weight, 1.0).deferred, "mode 0 never waits");
	// mode 1, a potion, the weight model's flight time
	{
		const Plan p = Decide(kAtLanding, true, true, 0.67);
		EXPECT(p.deferred && Near(p.delayS, 0.67, 1e-9), "waits the flight: %d %.2f", p.deferred, p.delayS);
	}
	// not a potion, no weight account, a flight too short, junk: at the release
	EXPECT(!Decide(kAtLanding, false, true, 1.0).deferred, "a javelin is pressed at the release");
	EXPECT(!Decide(kAtLanding, true, false, 1.0).deferred, "no weight account: at the release");
	EXPECT(!Decide(kAtLanding, true, true, 0.10).deferred, "a 0.1 s flight is not waited for");
	EXPECT(Decide(kAtLanding, true, true, 0.15).deferred, "0.15 s is");
	EXPECT(!Decide(kAtLanding, true, true, std::numeric_limits<double>::quiet_NaN()).deferred, "NaN");
	EXPECT(!Decide(kAtLanding, true, true, -1.0).deferred, "negative");
	// a very long flight is held to 4 s
	EXPECT(Near(Decide(kAtLanding, true, true, 9.0).delayS, kMaxFlightS, 1e-9), "capped at %.1f s", Decide(kAtLanding, true, true, 9.0).delayS);
	// any mode that is not 1 is 0
	EXPECT(!Decide(2, true, true, 1.0).deferred && !Decide(-1, true, true, 1.0).deferred, "unknown modes are 0");
	// the reason is always told
	EXPECT(Decide(kNow, true, true, 1.0).why[0] && Decide(kAtLanding, true, false, 1.0).why[0] && Decide(kAtLanding, true, true, 1.0).why[0], "a reason");
}

void TestGameFlight() {
	std::printf("what the game's potion costs\n");
	// the octagonal distance: (2 * longer + shorter) / 2 of the two axes, in whole cells
	EXPECT(OctFrames(20, 0) == 20 && OctFrames(0, -20) == 20, "straight: %d", OctFrames(20, 0));
	EXPECT(OctFrames(10, 10) == 15, "diagonal 10,10 = %d (14.1 cells for real)", OctFrames(10, 10));
	EXPECT(OctFrames(12.95, -21.05) == 27, "the logged throw 12.95, -21.05 = %d", OctFrames(12.95, -21.05));   // 21 and 12 whole cells: (42 + 12) / 2
	EXPECT(OctFrames(0, 0) == 0, "zero");
	EXPECT(OctFrames(3, 4) == 5, "3,4 = %d", OctFrames(3, 4));
	// 25 frames a second at a cell a frame
	EXPECT(Near(GameFlightS(25, 0), 1.0, 1e-9), "25 cells = 1 s");
	EXPECT(Near(GameFlightS(48, 0), 1.92, 1e-9), "48 cells = 1.92 s: %.3f", GameFlightS(48, 0));
	EXPECT(Near(GameFlightStraightS(10), 0.4, 1e-9), "10 cells = 0.4 s");
	// the octagon is between 1.0 and 1.12 of the straight line
	for (double a = 0.0; a < 6.3; a += 0.05) {
		const double dx = 30.0 * std::cos(a), dy = 30.0 * std::sin(a);
		const double f = GameFlightS(dx, dy), s = GameFlightStraightS(30.0);
		EXPECT(f >= s - 0.05 && f <= s * 1.13 + 0.05, "angle %.2f: %.3f s against %.3f", a, f, s);
	}
}

void TestPending() {
	std::printf("the pending press\n");
	Pending p;
	EXPECT(!p.Active() && p.Step(1.0, true) == Pending::Act::None, "idle");
	p.Arm(10.0, 0.67, 7);
	EXPECT(p.Active() && p.Seq() == 7 && Near(p.FireAt(), 10.67, 1e-9), "armed");
	EXPECT(p.Step(10.2, true) == Pending::Act::None && p.Active(), "waiting");
	EXPECT(p.Step(10.66, true) == Pending::Act::None && p.Active(), "not yet");
	EXPECT(p.Step(10.67, true) == Pending::Act::Fire && !p.Active(), "fires on time, once");
	EXPECT(p.Step(10.7, true) == Pending::Act::None, "and not again");
	// a change of play drops it
	p.Arm(20.0, 1.0, 8);
	EXPECT(p.Step(20.3, false) == Pending::Act::Dropped && !p.Active(), "dropped");
	// a frame that comes late fires (a hitch), but one that is far too late is dropped
	p.Arm(30.0, 0.5, 9);
	EXPECT(p.Step(31.0, true) == Pending::Act::Fire, "late but not stale");
	p.Arm(40.0, 0.5, 10);
	EXPECT(p.Step(46.0, true) == Pending::Act::Dropped, "6 s later: stale, never fires");
	p.Arm(50.0, 1.0, 11);
	p.Cancel();
	EXPECT(!p.Active() && p.Step(52.0, true) == Pending::Act::None, "cancelled");
}

// What it means in seconds for a few throws, printed.
void TestNumbers() {
	std::printf("a potion to N cells: the game's flight, and the whole delay after the release (press at once / at the landing)\n");
	std::printf("  cells   game flight   +0.3 s animation   model lands (weight, example)  press at landing -> explosion after\n");
	for (double n : {10.0, 15.0, 20.0, 30.0, 48.0}) {
		const double g = GameFlightStraightS(n);
		const double modelFlight = 0.67;   // the log's 26 m throw
		std::printf("  %5.0f   %5.2f s       %5.2f s            %5.2f s                          %5.2f s\n", n, g, g + 0.3, modelFlight, modelFlight + 0.3 + g);
	}
	EXPECT(true, "(printed)");
}

}  // namespace

int main() {
	TestDecide();
	TestGameFlight();
	TestPending();
	TestNumbers();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
