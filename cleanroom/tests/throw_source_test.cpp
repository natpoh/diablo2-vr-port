// vr/throw_source.h on its own: when the weapon-weight model's account of a throw is taken and when the hand's own estimate stays. No game, no headset.
//   throw_source_test           exit code 0 = every check passed

#include "../../vr/throw_source.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace throwsrc;

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

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// A throw straight ahead: the head frame is x right, y up, z BACK, so forward is -z.
Outcome Ahead(uint64_t tick, float metres = 18.0f) {
	Outcome o;
	o.valid = true;
	o.tickMs = tick;
	o.velocity[0] = 0.0f; o.velocity[1] = 1.0f; o.velocity[2] = -20.0f;
	o.speed = 20.02f;
	o.landing[0] = 0.0f; o.landing[1] = -1.5f; o.landing[2] = -metres;
	o.flightS = 0.9f;
	o.elevationDeg = 2.9f;
	return o;
}

void TestFresh() {
	std::printf("which outcome belongs to the throw\n");
	const uint64_t rel = 100000;
	EXPECT(Fresh(Ahead(rel), rel), "the same moment");
	EXPECT(Fresh(Ahead(rel - 11), rel), "a frame before (the preview, one step old)");
	EXPECT(Fresh(Ahead(rel + 20), rel), "a frame after");
	EXPECT(Fresh(Ahead(rel - 300), rel) && Fresh(Ahead(rel + 300), rel), "300 ms either way is still the throw");
	EXPECT(!Fresh(Ahead(rel - 301), rel) && !Fresh(Ahead(rel + 301), rel), "301 ms is another moment");
	EXPECT(!Fresh(Ahead(0), rel), "no step time");
	EXPECT(!Fresh(Ahead(rel), 0), "the throw gesture saw no release");
	// frozen at the grip release: that time counts too
	{
		Outcome o = Ahead(rel - 5000);
		o.releaseTickMs = rel + 40;
		EXPECT(Fresh(o, rel), "a stale step but the grip release is the throw's");
		o.releaseTickMs = rel - 9000;
		EXPECT(!Fresh(o, rel), "both stale");
	}
	EXPECT(DistanceMs(5, 9) == 4 && DistanceMs(9, 5) == 4 && DistanceMs(7, 7) == 0, "the distance is unsigned");
}

void TestTaken() {
	std::printf("when the weight model decides\n");
	const uint64_t rel = 100000;
	Choice c;
	// straight ahead: heading 0 right, 1 forward; the landing is final
	EXPECT(FromWeight(Ahead(rel), rel, &c), "a valid, fresh outcome");
	EXPECT(c.weight && Near(c.right, 0.0f, 1e-5f) && Near(c.forward, 1.0f, 1e-5f), "heading %.3f %.3f", c.right, c.forward);
	EXPECT(Near(c.landingM, 18.0f, 1e-4f) && Near(c.flightS, 0.9f, 1e-5f) && Near(c.elevationDeg, 2.9f, 1e-4f) && Near(c.speed, 20.02f, 1e-3f), "landing %.2f", c.landingM);
	EXPECT(!c.headingFromVelocity && !c.capped, "from the landing, not capped");
	// to the right (+x) and to the left (-x); behind (+z)
	{
		Outcome o = Ahead(rel);
		o.landing[0] = 10.0f; o.landing[2] = 0.0f;
		EXPECT(FromWeight(o, rel, &c) && Near(c.right, 1.0f, 1e-5f) && Near(c.forward, 0.0f, 1e-5f) && Near(c.landingM, 10.0f, 1e-4f), "right: %.3f %.3f", c.right, c.forward);
		o.landing[0] = -6.0f; o.landing[2] = -8.0f;
		EXPECT(FromWeight(o, rel, &c) && Near(c.right, -0.6f, 1e-5f) && Near(c.forward, 0.8f, 1e-5f) && Near(c.landingM, 10.0f, 1e-4f), "left and ahead: %.3f %.3f", c.right, c.forward);
		o.landing[0] = 0.0f; o.landing[2] = 7.0f;
		EXPECT(FromWeight(o, rel, &c) && Near(c.right, 0.0f, 1e-5f) && Near(c.forward, -1.0f, 1e-5f), "behind: forward %.3f", c.forward);
	}
	// the heading is a unit vector whatever the distance
	{
		Outcome o = Ahead(rel);
		o.landing[0] = 3.0f; o.landing[2] = -4.0f;
		EXPECT(FromWeight(o, rel, &c) && Near(c.right * c.right + c.forward * c.forward, 1.0f, 1e-5f), "unit");
	}
	// not valid, stale, no source of heading: the hand's own estimate
	{
		Outcome o = Ahead(rel);
		o.valid = false;
		EXPECT(!FromWeight(o, rel, &c) && !c.weight, "not valid");
		EXPECT(!FromWeight(Ahead(rel - 5000), rel, &c) && !c.weight, "stale");
		EXPECT(!FromWeight(Ahead(rel), 0, &c), "no release seen");
		EXPECT(!FromWeight(Ahead(rel), rel, nullptr), "no place for the answer");
	}
	// degenerate: the landing under the head, a NaN anywhere
	{
		Outcome o = Ahead(rel);
		o.landing[0] = 0.0f; o.landing[2] = 0.0f;
		EXPECT(!FromWeight(o, rel, &c), "the landing under the head, no velocity heading either would be taken: velocity is %.1f ahead", 20.0f);
	}
	{
		Outcome o = Ahead(rel);
		o.landing[2] = kNaN;
		EXPECT(!FromWeight(o, rel, &c), "NaN landing");
		o = Ahead(rel);
		o.velocity[0] = kNaN;
		EXPECT(!FromWeight(o, rel, &c), "NaN velocity");
		o = Ahead(rel);
		o.landing[1] = std::numeric_limits<float>::infinity();
		EXPECT(!FromWeight(o, rel, &c), "infinite landing height");
	}
	// a near landing (0.05 - 0.5 m) takes its heading from the velocity; no velocity either: the hand's estimate
	{
		Outcome o = Ahead(rel);
		o.landing[0] = 0.0f; o.landing[2] = -0.3f;
		o.velocity[0] = 5.0f; o.velocity[2] = 0.0f;
		EXPECT(FromWeight(o, rel, &c) && c.headingFromVelocity && Near(c.right, 1.0f, 1e-5f) && Near(c.forward, 0.0f, 1e-5f) && Near(c.landingM, 0.3f, 1e-5f), "near: %.3f %.3f %.2f", c.right, c.forward, c.landingM);
		o.velocity[0] = 0.1f;
		EXPECT(!FromWeight(o, rel, &c), "near landing and a velocity too slow to tell");
		o.landing[0] = 0.0f; o.landing[2] = -0.02f;
		o.velocity[0] = 5.0f;
		EXPECT(!FromWeight(o, rel, &c), "a landing under 5 cm says nothing");
	}
	// capped is carried; junk numbers beside a good landing do not fail it
	{
		Outcome o = Ahead(rel);
		o.capped = true;
		o.flightS = kNaN;
		o.elevationDeg = kNaN;
		o.speed = kNaN;
		EXPECT(FromWeight(o, rel, &c) && c.capped && c.flightS == 0.0f && c.elevationDeg == 0.0f && c.speed == 0.0f, "junk flight numbers are 0");
	}
	// a rejected answer leaves a clean Choice
	{
		Outcome o = Ahead(rel);
		o.valid = false;
		c.weight = true; c.landingM = 99.0f;
		EXPECT(!FromWeight(o, rel, &c) && !c.weight && c.landingM == 0.0f, "reset");
	}
}

}  // namespace

int main() {
	TestFresh();
	TestTaken();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
