// [weight] monster_ragdoll with a weapon in each hand, or one in the left (vr/blade_push.h, vr/weight_drive.h HeldAlone; melee_touch.cpp PushWithBlades): the headset
// 2026-10-23 - «оружие в двух руках - кукла прозрачная; оружие в левой руке - кукла прозрачная; оружие в правой руке - кукла работает». Which weapon the weight model
// holds (left only, right only, both), and each blade it does not hold pushing the body it enters: once as it enters, along the way in, the steel and the arm times
// the speed; nothing for a blade resting on it, a slow touch, a blade let go.

#include "../../vr/blade_push.h"
#include "../../vr/weight_drive.h"

#include <cmath>
#include <cstdio>

namespace bp = bladepush;

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

constexpr double kDt = 1.0 / 90.0;

// A blade along -z from its hand at x, swung across +x at v m/s; the bodies stand ahead.
bp::Blade BladeAt(float x, float y = 1.2f) { return { { x, y, 0 }, { x, y, -0.8f }, 0.02f }; }

void TestWhoHolds() {
	// (leftWeapon, rightHeld): right only, left only, both, nothing
	EXPECT(meleetouch::HeldAlone(false, true), "a weapon in the right hand alone: the weight model's");
	EXPECT(meleetouch::HeldAlone(true, false), "a weapon in the left hand alone: the weight model's too");
	EXPECT(!meleetouch::HeldAlone(true, true), "a weapon in each hand: not the weight model's - each blade pushes itself");
	std::printf("  the weight model holds: right alone %s, left alone %s, both %s\n", meleetouch::HeldAlone(false, true) ? "yes" : "no", meleetouch::HeldAlone(true, false) ? "yes" : "no",
	            meleetouch::HeldAlone(true, true) ? "yes" : "no");
}

// Two hands, a body before each (a fallen's arm at x 0 before the right blade's path, another monster's at x -1 before the left's): each blade swung through its own.
struct Run {
	int pushes[2] = {};      // per body
	float impulse[2] = {};   // the sum of each body's pushes along +x (the swing's way)
	int by[2] = { -1, -1 };  // the hand that pushed it
};
Run Swing(bool right, bool left, float v, float radius = 0.05f, float start = -0.4f) {
	bp::Body bodies[2];
	bodies[0] = { { 0.0f, 0.3f, -0.5f }, { 0.0f, 1.5f, -0.5f }, radius, 11, 7 };
	bodies[1] = { { -1.0f, 0.3f, -0.5f }, { -1.0f, 1.5f, -0.5f }, radius, 12, 7 };
	bp::Tracker hand[2];
	Run r;
	for (int i = 0; i <= 60; ++i) {
		const double t = i * kDt;
		const float dx = std::fmin(0.4f, start + v * (float)t);   // each hand from `start` before its body to 40 cm past it
		const bool have[2] = { right, left };
		const bp::Blade blade[2] = { BladeAt(0.0f + dx), BladeAt(-1.0f + dx) };
		for (int h = 0; h < 2; ++h) {
			bp::Push p[4];
			const int n = hand[h].Step(have[h], blade[h], t, bp::SteelKg(0), bodies, 2, p, 4);
			for (int k = 0; k < n; ++k) {
				++r.pushes[p[k].body];
				r.impulse[p[k].body] += p[k].impulse.x;
				r.by[p[k].body] = h;
			}
		}
	}
	return r;
}

void TestBlades() {
	const float v = 4.0f, kg = bp::SteelKg(0) + bp::kArmKg;
	const Run both = Swing(true, true, v), rightOnly = Swing(true, false, v), leftOnly = Swing(false, true, v);
	std::printf("  a sword in each hand swung at %.0f m/s: the right blade pushed its monster %d time(s), %.1f N s; the left its own %d time(s), %.1f N s (%.1f kg x %.0f m/s each)\n", v,
	            both.pushes[0], both.impulse[0], both.pushes[1], both.impulse[1], kg, v);
	EXPECT(both.pushes[0] == 1 && both.by[0] == 0 && both.pushes[1] == 1 && both.by[1] == 1, "each blade pushes the body it enters, once (%d by %d, %d by %d)", both.pushes[0], both.by[0],
	       both.pushes[1], both.by[1]);
	EXPECT(std::fabs(both.impulse[0] - kg * v) < 0.15f * kg * v && std::fabs(both.impulse[1] - kg * v) < 0.15f * kg * v, "along the way in, the steel and the arm times the speed (%.1f, %.1f of %.1f)",
	       both.impulse[0], both.impulse[1], kg * v);
	EXPECT(rightOnly.pushes[0] == 1 && rightOnly.pushes[1] == 0, "the right blade alone: its body only");
	EXPECT(leftOnly.pushes[1] == 1 && leftOnly.pushes[0] == 0, "the left blade alone: its body only");
	const Run fast = Swing(true, true, 12.0f, 0.03f, -0.47f);   // 13 cm a frame across a 3 cm wrist, no frame on it (-7 cm, +6 cm): stepped over without the sweep
	std::printf("  at 12 m/s: the right blade pushed its monster %d time(s), %.1f N s; the left %d, %.1f N s\n", fast.pushes[0], fast.impulse[0], fast.pushes[1], fast.impulse[1]);
	EXPECT(fast.pushes[0] == 1 && fast.pushes[1] == 1 && fast.impulse[0] > 0.8f * kg * 12.0f && fast.impulse[1] > 0.8f * kg * 12.0f, "a fast blade crossing a thin limb within a frame pushes it");
	const Run slow = Swing(true, true, 0.2f);
	EXPECT(slow.pushes[0] == 0 && slow.pushes[1] == 0, "a slow touch (0.2 m/s) is resting on it: no push");
	const Run none = Swing(false, false, v);
	EXPECT(none.pushes[0] == 0 && none.pushes[1] == 0, "no blade (let go, or the weight model's): nothing");
	// a blade that stays in the body after entering: one push, not one a frame
	bp::Body body = { { 0.0f, 0.3f, -0.5f }, { 0.0f, 1.5f, -0.5f }, 0.12f, 21, -1 };
	bp::Tracker tr;
	int n = 0;
	for (int i = 0; i <= 40; ++i) {
		const float x = i < 10 ? -0.4f + 0.04f * i : 0.0f;   // in, then held there
		bp::Push p[4];
		n += tr.Step(true, BladeAt(x), i * kDt, 1.5f, &body, 1, p, 4);
	}
	EXPECT(n == 1, "a blade left in the body pushed once (%d)", n);
}

}   // namespace

int main() {
	std::printf("blade_push_test: each blade a weapon in each hand, and a weapon in the left hand alone, pushes the doll\n");
	TestWhoHolds();
	TestBlades();
	if (g_fails) {
		std::printf("blade_push_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("blade_push_test: all passed\n");
	return 0;
}
