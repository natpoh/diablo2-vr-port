// The shoulder draw's zones (vr/shoulder_draw.h), pure: which shoulder a hand is behind and where the weapon comes out of it.
//
//   shoulder_draw_test

#include "../../vr/shoulder_draw.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace shoulderdraw;

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

struct Pt { float v[3]; };
Pt P(float x, float y, float z) { return Pt{{x, y, z}}; }

}   // namespace

int main() {
	// the shoulder a hand is behind: x right, y up, z back - the right shoulder is x > 0
	EXPECT(ZoneSide(P(0.25f, -0.1f, 0.15f).v) == Side::Right);
	EXPECT(ZoneSide(P(-0.25f, -0.1f, 0.15f).v) == Side::Left);
	EXPECT(ZoneSide(P(0.0f, -0.1f, 0.15f).v) == Side::None);     // the middle of the back of the head: neither
	EXPECT(ZoneSide(P(0.25f, -0.1f, -0.15f).v) == Side::None);   // in front
	EXPECT(ZoneSide(P(0.25f, -0.9f, 0.15f).v) == Side::None);    // the hip
	EXPECT(ZoneSide(P(0.9f, -0.1f, 0.15f).v) == Side::None);     // far out
	EXPECT(ZoneSide(P(0.10f - 0.004f, 0.0f, 0.1f).v) == Side::None && ZoneSide(P(0.10f + 0.004f, 0.0f, 0.1f).v) == Side::Right);
	EXPECT(ZoneSide(P(-(0.48f - 0.004f), 0.0f, 0.1f).v) == Side::Left && ZoneSide(P(-(0.48f + 0.004f), 0.0f, 0.1f).v) == Side::None);
	// [hands] shoulder_zone_body: the reach fits the body and begins at the spine. The hands of Serg's log (head frame, x right, y up, z back): behind his neck, a hand reaching back over it
	{
		const float serg1[3] = {-0.07f, 0.02f, 0.26f};    // the left hand behind the neck, 7 cm left of the middle: outside the fixed zone (its edge is 10 cm out)
		const float serg2[3] = {0.11f, -0.08f, 0.26f};    // the left hand across, behind the RIGHT shoulder
		const float front[3] = {-0.15f, -0.34f, -0.27f};  // a squeeze in front of the body: no zone, ever
		EXPECT(ZoneSide(serg1) == Side::None);            // (the fixed zone, as it was)
		shoulderswap::SetZone(shoulderswap::BodyZone(1.65f));
		EXPECT(ZoneSide(serg1) == Side::Left);
		EXPECT(ZoneSide(serg2) == Side::Right);
		EXPECT(ZoneSide(front) == Side::None);
		EXPECT(ZoneSide(P(0.0f, 0.0f, 0.2f).v) == Side::Left && ZoneSide(P(0.004f, 0.0f, 0.2f).v) == Side::Right);   // (the line between the shoulders is the middle of the back)
		EXPECT(ZoneSide(P(-0.30f, -0.1f, 0.15f).v) == Side::Left && ZoneSide(P(0.30f, -0.1f, 0.15f).v) == Side::Right);   // (the shoulders themselves as before)
		// it goes with the body: a tall one's reach is longer, a short one's shorter, the extremes clamped
		const shoulderswap::Zone tall = shoulderswap::BodyZone(1.90f), mid = shoulderswap::BodyZone(1.65f), shortz = shoulderswap::BodyZone(1.40f), giant = shoulderswap::BodyZone(2.60f), none = shoulderswap::BodyZone(0.0f);
		EXPECT(tall.absXMax > mid.absXMax && tall.zMax > mid.zMax && tall.yMin < mid.yMin);
		EXPECT(shortz.absXMax < mid.absXMax && shortz.zMax < mid.zMax && shortz.yMin > mid.yMin);
		EXPECT(std::fabs(mid.absXMax - 0.48f) < 1e-5f && std::fabs(mid.yMin + 0.38f) < 1e-5f && mid.absXMin == 0.0f);   // (the reference height gives the fixed numbers, from the spine)
		EXPECT(std::fabs(giant.absXMax - 0.48f * 1.25f) < 1e-5f && std::fabs(shortz.absXMax - 0.48f * 0.85f) < 1e-5f);   // (clamped)
		EXPECT(std::fabs(none.absXMax - 0.48f) < 1e-5f);   // (no height known: the reference one)
		shoulderswap::SetZone(tall);
		EXPECT(ZoneSide(P(-0.52f, -0.1f, 0.15f).v) == Side::Left);   // (beyond the fixed zone's 0.48, inside a tall body's)
		shoulderswap::ResetZone();
		EXPECT(ZoneSide(P(-0.52f, -0.1f, 0.15f).v) == Side::None && ZoneSide(serg1) == Side::None);   // (and back to the fixed one)
	}

	const float nan = std::numeric_limits<float>::quiet_NaN();
	EXPECT(ZoneSide(P(nan, 0.0f, 0.1f).v) == Side::None);

	// the point the weapon comes out of is inside the zone it is drawn from, whichever the hand
	for (Side s : {Side::Right, Side::Left}) {
		float p[3];
		DrawPoint(s, p);
		EXPECT(ZoneSide(p) == s);
	}
	float r[3], l[3];
	DrawPoint(Side::Right, r);
	DrawPoint(Side::Left, l);
	EXPECT(r[0] == -l[0] && r[1] == l[1] && r[2] == l[2]);   // mirror images

	if (g_fails) {
		std::printf("shoulder_draw_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("shoulder_draw_test: the two zones and the point a weapon comes out of: all passed\n");
	return 0;
}
