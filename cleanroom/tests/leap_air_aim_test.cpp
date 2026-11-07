// [gameplay] leap_air_aim (vr/leap_air_aim.h): the air seen from the model's lift (a leap's arc is air, a walk or a step is not, landing ends it once);
// which releases are held through the landing (a hold's, of a leap skill, in the air, with the tick); the kept world point seen from the landing spot,
// held to the range and the minimum along its heading; bad input.

#include "../../vr/leap_air_aim.h"

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

namespace la = leapair;

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
	{   // the author's leap as logged (up to 13.7 units over the look-at): air from past 1 unit to back under 0.5, once
		la::Air air;
		const float arc[] = {0.0f, 0.1f, 0.6f, 1.2f, 6.0f, 13.7f, 9.0f, 3.0f, 0.8f, 0.4f, 0.0f};
		const bool want[] = {false, false, false, true, true, true, true, true, true, false, false};
		for (int i = 0; i < (int)(sizeof arc / sizeof arc[0]); ++i) EXPECT(air.Feed(arc[i]) == want[i], "pose %d lift %.1f", i, arc[i]);
	}
	{   // walking: the matrix within undercam's 0.3 of the look-at - never air; a step of 0.9 units neither
		la::Air air;
		for (float l : {0.0f, 0.2f, 0.3f, -0.4f, 0.9f, 0.1f}) EXPECT(!air.Feed(l), "lift %.1f", l);
	}
	{   // not a number changes nothing
		la::Air air;
		air.Feed(5.0f);
		EXPECT(air.Feed(NAN) && air.Up(), "NaN in the air keeps the air");
		la::Air ground;
		EXPECT(!ground.Feed(INFINITY), "infinity on the ground keeps the ground");
	}
	{   // only a hold's release of a leap skill, in the air, with the tick, is held through the landing
		EXPECT(la::HoldThrough(true, true, true, true), "all four");
		EXPECT(!la::HoldThrough(false, true, true, true), "unticked: today's release");
		EXPECT(!la::HoldThrough(true, false, true, true), "Teleport or another point skill: today's");
		EXPECT(!la::HoldThrough(true, true, false, true), "on the ground or in the take-off's crouch: today's");
		EXPECT(!la::HoldThrough(true, true, true, false), "a tap: the gaze at its own cast");
	}
	{   // the kept point seen from the landing: the circle was 20 units ahead of the hero mid-flight at (100, 0); he lands 8 units on at (108, 0)
		float ox = 0, oz = 0;
		EXPECT(la::FromHero(120.0f, 0.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz) && Near(ox, 12.0f) && Near(oz, 0.0f), "12 units on from the landing: %.2f %.2f", ox, oz);
		// looked back over the shoulder in the air: the point behind the landing spot
		EXPECT(la::FromHero(90.0f, 0.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz) && Near(ox, -18.0f), "behind: %.2f", ox);
		// beyond the range from the landing: held to it along the same heading
		EXPECT(la::FromHero(108.0f + 60.0f, 80.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz) && Near(std::hypot(ox, oz), 50.0f) && Near(ox / oz, 60.0f / 80.0f),
		       "held to the cap: %.2f %.2f", ox, oz);
		// nearer than the minimum: pushed out to it
		EXPECT(la::FromHero(108.5f, 0.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz) && Near(ox, 2.0f), "pushed to the minimum: %.2f", ox);
		// landed right on it, or not numbers: none (the gaze's point instead)
		EXPECT(!la::FromHero(108.0f, 0.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz), "on the hero");
		EXPECT(!la::FromHero(NAN, 0.0f, 108.0f, 0.0f, 2.0f, 50.0f, &ox, &oz), "NaN");
		// a cap under the minimum: the minimum
		EXPECT(la::FromHero(130.0f, 0.0f, 108.0f, 0.0f, 2.0f, 1.0f, &ox, &oz) && Near(ox, 2.0f), "cap under the minimum: %.2f", ox);
	}
	if (g_fails) {
		std::printf("leap_air_aim_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("leap_air_aim_test: all passed\n");
	return 0;
}
