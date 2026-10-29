// The shoulder swap's detector (vr/shoulder_swap.h) on synthetic hand poses, without the game, a headset
// or BodyWalk:
//
//   shoulder_swap_test
//
// Checks: the hand in the head's turn frame, whatever the head's pitch and roll; either hand behind either
// shoulder swaps once per press; the reach's edges; the grip's two thresholds; the dwell; a press that is not
// the gesture's (held on entering, held on tracking's return, a menu, a jump, a gap, another hand) swaps
// nothing; reaching back without the grip claims nothing; the press is claimed while the hand is in the
// reach (no belt potion), and until it is let go; a request waits for the game's set and is never repeated.

#include "../../vr/shoulder_swap.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace shoulderswap;

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

constexpr double kDt = 1.0 / 90.0;   // a headset's rate

// A point to ask Near() about.
struct Pt { float v[3]; };
Pt MakePt(float x, float y, float z) { return Pt{{x, y, z}}; }
#define P(x, y, z) MakePt(x, y, z).v

// One hand, stepped at 90 Hz. Starts behind the right shoulder, 14 cm back, a little below the eyes.
struct Sim {
	Gesture g;
	Input in;
	Output out;
	int swaps = 0;
	Sim() {
		in.time = 1.0;
		in.hand = 0;
		in.tracked = true;
		in.pos[0] = 0.23f; in.pos[1] = -0.20f; in.pos[2] = 0.14f;
		in.grip = 0.0f;
	}
	void Place(float x, float y, float z) { in.pos[0] = x; in.pos[1] = y; in.pos[2] = z; }
	Output Step(int n = 1) {
		while (n-- > 0) {
			in.time += kDt;
			out = g.Step(in);
			swaps += out.swap ? 1 : 0;
		}
		return out;
	}
	// Time long enough for the dwell, hand still, grip up.
	void Settle() { Step(14); }   // 14 samples = 0.155 s
};


// Quaternions, x y z w, for the head's pose in the room.
struct Q { float v[4]; };
Q AxisAngle(float ax, float ay, float az, float a) {
	const float s = std::sin(a * 0.5f);
	return Q{{ax * s, ay * s, az * s, std::cos(a * 0.5f)}};
}
Q Mul(const Q& a, const Q& b) {   // a, then b is applied first
	const float *p = a.v, *q = b.v;
	return Q{{p[3] * q[0] + p[0] * q[3] + p[1] * q[2] - p[2] * q[1], p[3] * q[1] - p[0] * q[2] + p[1] * q[3] + p[2] * q[0],
	          p[3] * q[2] + p[0] * q[1] - p[1] * q[0] + p[2] * q[3], p[3] * q[3] - p[0] * q[0] - p[1] * q[1] - p[2] * q[2]}};
}
void Turn(const Q& q, const float v[3], float o[3]) {   // v turned by q
	const float x = q.v[0], y = q.v[1], z = q.v[2], w = q.v[3];
	o[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - w * z) * v[1] + 2 * (x * z + w * y) * v[2];
	o[1] = 2 * (x * y + w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z - w * x) * v[2];
	o[2] = 2 * (x * z - w * y) * v[0] + 2 * (y * z + w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}
bool Near3(const float a[3], float x, float y, float z) {
	return std::fabs(a[0] - x) < 1e-4f && std::fabs(a[1] - y) < 1e-4f && std::fabs(a[2] - z) < 1e-4f;
}

void HeadFrame() {
	// A hand at (0.23, -0.2, 0.14) in the head's turn frame comes back as that, whatever the head's yaw
	// (turned left +), pitch and roll, and wherever the head stands in the room.
	const float headAt[3] = {2.0f, 1.2f, -4.0f}, mine[3] = {0.23f, -0.2f, 0.14f};
	for (float yaw : {-3.0f, -1.5708f, 0.0f, 0.7f, 1.5708f, 3.1f})
		for (float pitch : {-1.2f, 0.0f, 1.2f})
			for (float roll : {-0.5f, 0.0f, 0.5f}) {
				const Q head = Mul(AxisAngle(0, 1, 0, yaw), Mul(AxisAngle(1, 0, 0, pitch), AxisAngle(0, 0, 1, roll)));
				Heading h;
				EXPECT(h.Update(head.v) && std::fabs(std::remainder(h.Yaw() - yaw, 6.2831853f)) < 1e-3f);
				float w[3], hand[3], rel[3];
				Turn(AxisAngle(0, 1, 0, yaw), mine, w);
				for (int i = 0; i < 3; ++i) hand[i] = headAt[i] + w[i];
				EXPECT(h.Relative(headAt, hand, rel) && Near3(rel, 0.23f, -0.2f, 0.14f));
			}
	// Behind a head turned 90 degrees left is room +x.
	{
		Heading h;
		const Q head = AxisAngle(0, 1, 0, 1.5708f);
		const float at[3] = {0, 1.7f, 0}, hand[3] = {0.3f, 1.5f, 0.0f};
		float rel[3];
		EXPECT(h.Update(head.v) && h.Relative(at, hand, rel) && Near3(rel, 0.0f, -0.2f, 0.3f));
	}
	// A gaze straight up or down has no heading of its own: the last one stays; none before is no hand.
	{
		Heading h;
		const float at[3] = {0, 1.7f, 0}, hand[3] = {0.23f, 1.5f, 0.14f};
		float rel[3];
		const Q down = Mul(AxisAngle(0, 1, 0, 2.0f), AxisAngle(1, 0, 0, -1.5608f));   // 89.4 degrees down
		EXPECT(!h.Update(down.v) && !h.Relative(at, hand, rel));
		const Q level = AxisAngle(0, 1, 0, 0.5f);
		EXPECT(h.Update(level.v));
		EXPECT(h.Update(down.v) && std::fabs(h.Yaw() - 0.5f) < 1e-4f);
	}
	// Not a rotation: nothing taken.
	{
		Heading h;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float bad[4] = {nan, 0, 0, 1}, zero[4] = {0, 0, 0, 0};
		EXPECT(!h.Update(bad) && !h.Update(zero) && !h.Have());
	}
}

void Reach() {
	// Both hands, both shoulders: a swap from the press, and only one, with the grip held.
	for (int hand = 0; hand < 2; ++hand)
		for (float side : {-0.23f, 0.23f}) {
			Sim s;
			s.in.hand = hand;
			s.Place(side, -0.2f, 0.14f);
			s.Settle();
			EXPECT(s.out.inReach);
			EXPECT(!s.out.claim);       // reaching back without the grip leaves combat and throws free
			EXPECT(s.swaps == 0);
			s.in.grip = 1.0f;
			s.Step();
			EXPECT(s.out.swap && s.out.claim && s.swaps == 1);
			s.Step(30);
			EXPECT(s.swaps == 1 && s.out.claim);   // held: no repeat, still the gesture's
			for (int i = 0; i < 14; ++i) {         // the hand moves away with the grip still held
				s.in.pos[2] -= 0.04f;
				s.Step();
				EXPECT(s.out.claim && !s.out.swap && !s.out.jump);   // the press is used up until it is let go
			}
			EXPECT(!s.out.inReach);
			s.in.grip = 0.0f;
			s.Step();
			EXPECT(!s.out.claim);
			// a second press is a second swap
			s.Place(side, -0.2f, 0.14f);
			s.Settle();
			s.in.grip = 1.0f;
			s.Step();
			EXPECT(s.swaps == 2);
		}
}

void Volume() {
	// Outside: the face, the hip, the head's own line, far out, above the head, too far back, the chest.
	const float outside[][3] = {{0.23f, -0.2f, -0.14f}, {0.23f, -0.9f, 0.0f}, {0.05f, -0.2f, 0.14f}, {0.9f, -0.2f, 0.14f},
	                            {0.23f, 0.40f, 0.14f},  {0.23f, -0.2f, 0.60f}, {0.23f, -0.2f, 0.0f},  {0.23f, -0.60f, 0.14f}};
	for (const auto& p : outside) {
		Sim s;
		s.Place(p[0], p[1], p[2]);
		s.Settle();
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(!s.out.swap && !s.out.claim && !s.out.inReach && s.swaps == 0);
	}
	// The edges, inside and out (a hair either way).
	const float e = 0.004f;
	EXPECT(Near(P(kAbsXMin + e, 0.0f, 0.1f)));
	EXPECT(!Near(P(kAbsXMin - e, 0.0f, 0.1f)));
	EXPECT(Near(P(-(kAbsXMax - e), 0.0f, 0.1f)));
	EXPECT(!Near(P(-(kAbsXMax + e), 0.0f, 0.1f)));
	EXPECT(Near(P(0.2f, kYMin + e, 0.1f)));
	EXPECT(!Near(P(0.2f, kYMin - e, 0.1f)));
	EXPECT(Near(P(0.2f, kYMax - e, 0.1f)));
	EXPECT(!Near(P(0.2f, kYMax + e, 0.1f)));
	EXPECT(Near(P(0.2f, 0.0f, kZMin + e)));
	EXPECT(!Near(P(0.2f, 0.0f, kZMin - e)));
	EXPECT(Near(P(0.2f, 0.0f, kZMax - e)));
	EXPECT(!Near(P(0.2f, 0.0f, kZMax + e)));
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	EXPECT(!Near(P(nan, 0.0f, 0.1f)));
	EXPECT(!Near(P(0.2f, inf, 0.1f)));
}

void Grip() {
	// Hysteresis: down above 0.55, up below 0.20; between the two it stays what it was.
	Sim s;
	s.Settle();
	s.in.grip = 0.54f;
	s.Step();
	EXPECT(!s.out.swap && !s.out.claim);   // not down yet
	s.in.grip = 0.56f;
	s.Step();
	EXPECT(s.out.swap);
	s.in.grip = 0.30f;   // squeezed less, but still down
	s.Step(5);
	EXPECT(s.swaps == 1 && s.out.claim);
	s.in.grip = 0.60f;   // back up from 0.30: the same press, not a new one
	s.Step(5);
	EXPECT(s.swaps == 1);
	s.in.grip = 0.19f;
	s.Step();
	EXPECT(!s.out.claim);
	s.in.grip = 0.50f;   // 0.5 is not a press from up
	s.Step(5);
	EXPECT(s.swaps == 1 && !s.out.claim);
	s.in.grip = 0.56f;
	s.Step();
	EXPECT(s.swaps == 2);
	// A grip that is not known (< 0, NaN) is nothing: no claim, no swap.
	Sim u;
	u.Settle();
	u.in.grip = -1.0f;
	u.Step(5);
	EXPECT(u.swaps == 0 && !u.out.claim);
	u.in.grip = std::numeric_limits<float>::quiet_NaN();
	u.Step(5);
	EXPECT(u.swaps == 0 && !u.out.claim);
}

void Dwell() {
	// In the reach 0.12 s before the press: a hand that comes in and presses at once swaps nothing.
	{
		Sim s;
		s.Place(0.0f, -0.2f, -0.3f);   // far from it
		s.Step(10);
		s.Place(0.23f, -0.2f, 0.14f);  // arrives
		s.Step();
		s.in.grip = 1.0f;
		s.Step(3);
		EXPECT(s.swaps == 0 && s.out.claim);   // the press is the gesture's, even though it swaps nothing
		s.in.grip = 0.0f;
		s.Step(12);
		s.in.grip = 1.0f;                      // pressed again - now long enough in the reach
		s.Step();
		EXPECT(s.swaps == 1);
	}
	// The dwell's edge: 9 samples (0.1 s) too soon, 11 (0.122 s) enough.
	{
		Sim s;
		s.Place(0.0f, -0.2f, -0.3f);
		s.Step(10);
		s.Place(0.23f, -0.2f, 0.14f);
		s.Step(9);       // in the reach for 8 steps = 0.089 s when the press comes
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 0);
	}
	{
		Sim s;
		s.Place(0.0f, -0.2f, -0.3f);
		s.Step(10);
		s.Place(0.23f, -0.2f, 0.14f);
		s.Step(11);      // 10 steps = 0.111 s ...
		s.in.grip = 1.0f;
		s.Step();        // ... 11 steps = 0.122 s at the press
		EXPECT(s.swaps == 1);
	}
	// Leaving the reach for one pose starts the dwell again.
	{
		Sim s;
		s.Settle();
		s.Place(0.23f, -0.2f, -0.1f);
		s.Step();
		s.Place(0.23f, -0.2f, 0.14f);
		s.Step(3);
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 0);
	}
}

void NotTheGestures() {
	// A grip held when the hand comes in: no swap, but the grip is claimed there (no belt potion).
	{
		Sim s;
		s.Place(0.23f, -0.2f, -0.14f);
		s.Step(10);
		s.in.grip = 1.0f;
		s.Step(2);
		EXPECT(!s.out.claim);   // in front of the face: it is not ours
		s.Place(0.23f, -0.2f, 0.14f);
		s.Step(30);
		EXPECT(s.swaps == 0 && s.out.claim);
	}
	// A grip held when tracking starts.
	{
		Sim s;
		s.in.grip = 1.0f;
		s.Step(10);
		EXPECT(s.swaps == 0 && s.out.claim);
		s.in.grip = 0.0f;
		s.Step();
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 1);   // a fresh press after the let go
	}
	// Tracking lost with the grip held, then back.
	{
		Sim s;
		s.Settle();
		s.in.tracked = false;
		s.in.grip = 1.0f;
		s.Step(3);
		s.in.tracked = true;
		s.Step(30);
		EXPECT(s.swaps == 0 && s.out.claim);
		s.in.grip = 0.0f;
		s.Step(14);
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 1);
	}
	// Another hand: what the first held is not the second's press.
	{
		Sim s;
		s.Settle();
		s.in.grip = 1.0f;
		s.in.hand = 1;
		s.Step(20);
		EXPECT(s.swaps == 0);
	}
	// A frame hitch of a second cannot replay the gesture.
	{
		Sim s;
		s.Settle();
		s.in.time += 1.0;
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 0);
		s.Step(20);
		EXPECT(s.swaps == 0);
	}
	// Time that stands still or goes back.
	{
		Sim s;
		s.Settle();
		s.in.grip = 1.0f;
		s.in.time -= 0.5;
		s.out = s.g.Step(s.in);
		EXPECT(!s.out.swap);
		s.out = s.g.Step(s.in);   // the same time twice
		EXPECT(!s.out.swap);
	}
	// Not a pose at all: NaN, no time.
	{
		Sim s;
		s.Settle();
		s.in.grip = 1.0f;
		s.in.pos[0] = std::numeric_limits<float>::quiet_NaN();
		s.Step(3);
		EXPECT(s.swaps == 0);
		s.in.pos[0] = 0.23f;
		s.in.time = 0.0;
		s.out = s.g.Step(s.in);
		EXPECT(!s.out.swap && !s.out.claim);
	}
}

void Jumps() {
	// Tracking that relocks: the hand is somewhere else in one sample. A press on the jump's sample, or
	// before the hand has been in the reach 0.12 s again, is nothing.
	{
		Sim s;
		s.Place(0.0f, -0.6f, -0.4f);
		s.Step(20);
		s.Place(0.23f, -0.2f, 0.14f);   // 0.8 m in one step
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.out.jump && s.swaps == 0);
		s.in.grip = 0.0f;
		s.Step(2);
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 0);   // the dwell began at the jump
		s.in.grip = 0.0f;
		s.Step(12);
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 1);
	}
	// A jump with the grip held uses the press up.
	{
		Sim s;
		s.Settle();
		s.in.grip = 1.0f;
		s.Step(1);
		EXPECT(s.swaps == 1);
		s.in.grip = 0.0f;
		s.Step(14);
		s.in.grip = 1.0f;
		s.Place(0.45f, 0.0f, 0.34f);    // 0.35 m in one step: a jump
		s.Step();
		EXPECT(s.out.jump && !s.out.swap && s.out.claim && s.swaps == 1);
	}
	// A quick hand is not a jump: 8 m/s (a hard swing) at 90 Hz is 9 cm a sample.
	{
		Sim s;
		s.Place(0.23f, -0.2f, 0.14f);
		s.Step(14);
		for (int i = 0; i < 6; ++i) {
			s.in.pos[2] += 0.09f * ((i & 1) ? -1.0f : 1.0f);
			s.Step();
			EXPECT(!s.out.jump);
		}
	}
}

void Context() {
	// allowed = false (a menu, a bottle in the hand): nothing is claimed, nothing swapped; letting the
	// context back cannot replay a grip held through it.
	{
		Sim s;
		s.Settle();
		s.in.allowed = false;
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(!s.out.swap && !s.out.claim);
		s.in.allowed = true;
		s.Step(20);
		EXPECT(s.swaps == 0);
		EXPECT(s.out.claim);   // in the reach with the grip held: the gesture's again
	}
	// canSwap = false (a request out, the set not known): the press is still the gesture's - claimed,
	// used up - and swaps nothing; the next press does.
	{
		Sim s;
		s.Settle();
		s.in.canSwap = false;
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(!s.out.swap && s.out.claim);
		s.in.canSwap = true;
		s.Step(30);
		EXPECT(s.swaps == 0 && s.out.claim);
		s.in.grip = 0.0f;
		s.Step();
		EXPECT(!s.out.claim);
		s.in.grip = 1.0f;
		s.Step();
		EXPECT(s.swaps == 1);
	}
	// Reaching back without the grip, whatever else is in the other hand: nothing claimed (the javelin's
	// wind-up and the trigger are not this hand's grip).
	{
		Sim s;
		for (float z = -0.2f; z < 0.5f; z += 0.05f) {
			s.in.pos[2] = z;
			s.Step(3);
			EXPECT(!s.out.claim && !s.out.swap);
		}
	}
}

void Request() {
	// A swap asked for waits for the set to show; once it has, it is done.
	{
		Pending p;
		EXPECT(!p.Busy());
		EXPECT(p.Update(1.0, 1) == Pending::Result::None);
		EXPECT(p.Begin(1.0, 1) && p.Busy() && p.Expected() == 2);
		EXPECT(p.Update(1.2, 1) == Pending::Result::None);
		EXPECT(p.Update(1.4, 2) == Pending::Result::Applied);
		EXPECT(!p.Busy());
		EXPECT(p.Update(1.6, 2) == Pending::Result::None);   // said once
	}
	// Set II to set I.
	{
		Pending p;
		EXPECT(p.Begin(5.0, 2) && p.Expected() == 1);
		EXPECT(p.Update(5.3, 1) == Pending::Result::Applied);
	}
	// One at a time; not while the set is not known.
	{
		Pending p;
		EXPECT(!p.Begin(1.0, 0));
		EXPECT(!p.Begin(1.0, 3));
		EXPECT(p.Begin(1.0, 1));
		EXPECT(!p.Begin(1.1, 1));
		EXPECT(p.Expected() == 2);
	}
	// Not seen to take effect: let go once after the time-out, and not asked again by itself.
	{
		Pending p;
		EXPECT(p.Begin(10.0, 1));
		EXPECT(p.Update(10.0 + Pending::kTimeoutS - 0.1, 1) == Pending::Result::None);
		EXPECT(p.Update(10.0 + Pending::kTimeoutS + 0.1, 1) == Pending::Result::NotTaken);
		EXPECT(!p.Busy());
		EXPECT(p.Update(20.0, 1) == Pending::Result::None);
	}
	// The set lost (no hero): let go as well.
	{
		Pending p;
		EXPECT(p.Begin(1.0, 1));
		EXPECT(p.Update(1.1, 0) == Pending::Result::NotTaken);
		EXPECT(!p.Busy());
	}
	// The two together, as the wiring does: one press, one request, the held grip asks for nothing more.
	{
		Sim s;
		Pending p;
		int sent = 0;
		uint32_t shown = 1;
		s.Settle();
		s.in.grip = 1.0f;
		for (int i = 0; i < 400; ++i) {
			s.in.canSwap = !p.Busy() && shown != 0;
			s.in.time += kDt;
			s.out = s.g.Step(s.in);
			if (s.out.swap && p.Begin(s.in.time, shown)) ++sent;
			if (i == 100) shown = 2;   // the game shows it a while later
			p.Update(s.in.time, shown);
		}
		EXPECT(sent == 1 && shown == 2 && !p.Busy());
	}
}

}   // namespace

int main() {
	HeadFrame();
	Reach();
	Volume();
	Grip();
	Dwell();
	NotTheGestures();
	Jumps();
	Context();
	Request();
	if (g_fails) {
		std::printf("shoulder_swap_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("shoulder_swap_test: head frame, reach, edges, grip thresholds, dwell, press ownership, jumps, gaps, request: all passed\n");
	return 0;
}
