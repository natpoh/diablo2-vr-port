// The javelin throw gesture (vr/throw_gesture.h) on synthetic hand poses: no game, no headset.
//   throw_gesture_test            exit code 0 = every check passed
//
// A scripted hand: positions over time in the room's frame (x right, y up, z back), a trigger,
// frames at a given rate. The checks are the detector's rules - the edge, the travel, the speed
// band, the grace before the release, the hold time, a stationary release, a jab with the trigger
// up, the cancels - and that the direction it returns is the way the hand went.

#include "../../vr/throw_gesture.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <vector>

using namespace throwgesture;

namespace {

int g_fails = 0;
int g_checks = 0;

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

// One frame of a script: where the hand is and what the trigger does at time t (seconds from 0).
struct Frame {
	Vec3 hand;
	bool trigger = false;
	bool tracked = true;
	bool cancel = false;
};
using Script = std::function<Frame(double t)>;

struct Run {
	int throws = 0;
	int started = 0;
	Result last;     // the last throw
	double lastAt = 0.0;
};

// Feeds a script to a detector from t = 0 to `end`, one frame per 1/hz, and counts what it says.
Run Feed(Detector& d, const Script& script, double end, double hz = 90.0, double t0 = 0.0) {
	Run run;
	const double dt = 1.0 / hz;
	for (int i = 0; t0 + i * dt <= t0 + end + 1e-9; ++i) {
		const double t = t0 + i * dt;
		const Frame f = script(i * dt);
		Sample s;
		s.t = t;
		s.hand = f.hand;
		s.tracked = f.tracked;
		s.trigger = f.trigger;
		s.cancel = f.cancel;
		const Result r = d.Step(s);
		if (r.started) ++run.started;
		if (r.throwNow) {
			++run.throws;
			run.last = r;
			run.lastAt = t;
		}
	}
	return run;
}

Vec3 Lerp(const Vec3& a, const Vec3& b, double k) {
	return {(float)(a.x + (b.x - a.x) * k), (float)(a.y + (b.y - a.y) * k), (float)(a.z + (b.z - a.z) * k)};
}
Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }

// The angle in degrees between the ground part of a result and the wanted heading (x, z).
double HeadingError(const Result& r, float wx, float wz) {
	const double a = std::atan2((double)r.groundZ, (double)r.groundX), b = std::atan2((double)wz, (double)wx);
	double d = (a - b) * 180.0 / kPi;
	while (d > 180.0) d -= 360.0;
	while (d < -180.0) d += 360.0;
	return std::fabs(d);
}

// An overhand throw: trigger down at `t0` with the hand at `home`; the hand draws back along -dir
// for `windup` s (to `back` m), swings forward along dir at `speed` m/s for `swing` s, then the
// trigger goes up at `release` s after the swing began plus `coast` s of a hand that went on
// at rest (coast > 0: the hand stopped before the release).
struct Throw {
	Vec3 home{0.0f, 1.4f, 0.0f};
	Vec3 dir{0.0f, 0.0f, -1.0f};   // unit, where it goes
	double t0 = 0.2;
	double windup = 0.30;
	float back = 0.25f;
	double swing = 0.16;
	float speed = 5.0f;
	double coast = 0.0;            // at rest between the end of the swing and the release
	double tail = 0.4;             // frames after the release
	bool release = true;
	double idle = 0.0;             // the trigger (grip) down at t0, the hand at rest this long before the wind-up begins
	double end() const { return t0 + idle + windup + swing + coast + 0.01 + tail; }

	Frame At(double t) const {
		Frame f;
		f.hand = home;
		const double a = t0 + idle, b = a + windup, c = b + swing, rel = c + coast + 0.005;
		if (t >= a && t < b) {
			const double k = (t - a) / windup;
			f.hand = Add(home, Scale(dir, -back * (float)std::sin(k * kPi / 2)));
		} else if (t >= b && t < c) {
			f.hand = Add(Add(home, Scale(dir, -back)), Scale(dir, speed * (float)(t - b)));
		} else if (t >= c) {
			f.hand = Add(Add(home, Scale(dir, -back)), Scale(dir, speed * (float)swing));
		}
		f.trigger = t >= t0 && (!release || t < rel);
		return f;
	}
};

// Quaternions (x y z w) for the head: a turn about the vertical (+ to the left), a tilt about x (+ looks up), a roll.
struct Q { float x, y, z, w; };
Q Mul(const Q& a, const Q& b) {
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q AxisAngle(float ax, float ay, float az, double deg) {
	const float h = (float)(deg * kPi / 360.0), s = std::sin(h);
	return {ax * s, ay * s, az * s, std::cos(h)};
}
Q Head(double yawLeftDeg, double pitchUpDeg, double rollDeg) {
	return Mul(Mul(AxisAngle(0, 1, 0, yawLeftDeg), AxisAngle(1, 0, 0, pitchUpDeg)), AxisAngle(0, 0, 1, rollDeg));
}

void TestHeadFrame() {
	std::printf("the head's frame\n");
	// facing -z (straight ahead in the room): a throw along -z is forward, along +x to the right
	float fx, fz, r, f;
	{
		const Q q = Head(0, 0, 0);
		const float a[4] = {q.x, q.y, q.z, q.w};
		EXPECT(HeadGroundForward(a, &fx, &fz) && std::fabs(fx) < 1e-5f && std::fabs(fz + 1.0f) < 1e-5f, "facing -z: %.3f %.3f", fx, fz);
		ToHeadFrame(0.0f, -1.0f, fx, fz, &r, &f);
		EXPECT(std::fabs(r) < 1e-5f && std::fabs(f - 1.0f) < 1e-5f, "-z is forward: %.3f %.3f", r, f);
		ToHeadFrame(1.0f, 0.0f, fx, fz, &r, &f);
		EXPECT(std::fabs(r - 1.0f) < 1e-5f && std::fabs(f) < 1e-5f, "+x is right: %.3f %.3f", r, f);
	}
	// a head turned 90 degrees to the left faces -x: a throw along -x is forward, along -z to its right
	{
		const Q q = Head(90, 0, 0);
		const float a[4] = {q.x, q.y, q.z, q.w};
		EXPECT(HeadGroundForward(a, &fx, &fz) && std::fabs(fx + 1.0f) < 1e-5f && std::fabs(fz) < 1e-5f, "turned left: %.3f %.3f", fx, fz);
		ToHeadFrame(-1.0f, 0.0f, fx, fz, &r, &f);
		EXPECT(std::fabs(r) < 1e-5f && std::fabs(f - 1.0f) < 1e-5f, "-x is forward: %.3f %.3f", r, f);
		ToHeadFrame(0.0f, -1.0f, fx, fz, &r, &f);
		EXPECT(std::fabs(r - 1.0f) < 1e-5f && std::fabs(f) < 1e-5f, "-z is right: %.3f %.3f", r, f);
	}
	// the head's way holds whatever the tilt and the roll, even steeply up or down
	double worst = 0.0;
	for (int yaw = -180; yaw < 180; yaw += 20) {
		for (int pitch = -85; pitch <= 85; pitch += 17) {
			for (int roll = -40; roll <= 40; roll += 20) {
				// looking almost straight up or down, a roll about the view IS a turn: only an upright head has a way
				if (std::abs(pitch) > 70 && roll != 0) continue;
				const Q q = Head(yaw, pitch, roll);
				const float a[4] = {q.x, q.y, q.z, q.w};
				if (!HeadGroundForward(a, &fx, &fz)) { worst = 999; continue; }
				const double want = yaw * kPi / 180.0;   // turned left by yaw: forward = (-sin, -cos)
				const double e = std::fabs(std::atan2((double)fx, (double)fz) - std::atan2(-std::sin(want), -std::cos(want)));
				worst = std::fmax(worst, std::fmin(e, 2 * kPi - e) * 180.0 / kPi);
			}
		}
	}
	EXPECT(worst < 3.0, "head way off by %.1f deg", worst);
	std::printf("  worst head way error %.2f deg\n", worst);
	float bad[4] = {0, 0, 0, 0};
	EXPECT(!HeadGroundForward(bad, &fx, &fz), "an empty quaternion");
}

void TestOverhand() {
	std::printf("overhand throw\n");
	Throw th;
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	EXPECT(run.started == 1, "started %d", run.started);
	EXPECT(run.throws == 1, "throws %d", run.throws);
	EXPECT(HeadingError(run.last, 0.0f, -1.0f) < 3.0, "heading error %.2f deg", HeadingError(run.last, 0.0f, -1.0f));
	// the velocity is the change of place over ~30 ms: the end of the swing is in it, a little under 5
	EXPECT(run.last.speed > 3.5f && run.last.speed < 5.3f, "speed %.2f", run.last.speed);
	EXPECT(run.last.travel >= 0.18f, "travel %.3f", run.last.travel);
	EXPECT(run.last.dir.z < -0.9f && std::fabs(run.last.dir.y) < 0.1f, "dir %.2f %.2f %.2f", run.last.dir.x, run.last.dir.y, run.last.dir.z);
	EXPECT(d.Throwing() == false, "still throwing after the release");
}

void TestHeadings() {
	std::printf("a throw every 15 degrees round the hero, three frame rates\n");
	double worst = 0.0;
	int missed = 0;
	for (double hz : {72.0, 90.0, 120.0}) {
		for (int deg = 0; deg < 360; deg += 15) {
			const double a = deg * kPi / 180.0;
			Throw th;
			th.dir = {(float)std::cos(a), 0.0f, (float)std::sin(a)};
			Detector d;
			const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end(), hz);
			if (run.throws != 1) {
				++missed;
				std::printf("  missed: %.0f Hz, %d deg (throws %d)\n", hz, deg, run.throws);
				continue;
			}
			const double e = HeadingError(run.last, th.dir.x, th.dir.z);
			if (e > worst) worst = e;
		}
	}
	EXPECT(missed == 0, "%d throws missed", missed);
	EXPECT(worst < 3.0, "worst heading error %.2f deg", worst);
	std::printf("  worst heading error %.2f deg\n", worst);
}

void TestElevation() {
	std::printf("a throw with the hand going up or down\n");
	// 30 degrees up and down: still a heading on the ground, dir keeps the slope
	for (double el : {-30.0, 30.0}) {
		const double e = el * kPi / 180.0;
		Throw th;
		th.dir = {0.0f, (float)std::sin(e), (float)-std::cos(e)};
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 1, "elevation %.0f: throws %d", el, run.throws);
		EXPECT(HeadingError(run.last, 0.0f, -1.0f) < 3.0, "elevation %.0f: heading %.2f", el, HeadingError(run.last, 0.0f, -1.0f));
		EXPECT(std::fabs(run.last.dir.y - th.dir.y) < 0.05f, "elevation %.0f: dir.y %.2f", el, run.last.dir.y);
	}
	// 80 degrees up or down: no heading on the ground, nothing is thrown
	for (double el : {-80.0, 80.0}) {
		const double e = el * kPi / 180.0;
		Throw th;
		th.dir = {0.0f, (float)std::sin(e), (float)-std::cos(e)};
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 0, "elevation %.0f: throws %d", el, run.throws);
	}
}

void TestStationaryRelease() {
	std::printf("a release with the hand at rest\n");
	Detector d;
	const Run run = Feed(d, [](double t) {
		Frame f;
		f.hand = {0.1f, 1.3f, -0.2f};
		f.trigger = t >= 0.2 && t < 1.0;
		return f;
	}, 1.5);
	EXPECT(run.started == 1 && run.throws == 0, "started %d throws %d", run.started, run.throws);
}

void TestTooSlow() {
	std::printf("a slow hand\n");
	Throw th;
	th.speed = 1.0f;   // under the band, even though the swing is long enough
	th.swing = 0.30;
	th.back = 0.0f;
	th.windup = 0.10;
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	EXPECT(run.throws == 0, "slow: throws %d", run.throws);
	// right at the lower edge of the band: 1.5 m/s gets through, 1.3 does not
	for (float sp : {1.3f, 1.5f}) {
		Throw t2;
		t2.speed = sp;
		t2.swing = 0.30;
		t2.back = 0.0f;
		t2.windup = 0.10;
		Detector d2;
		const Run r2 = Feed(d2, [&](double t) { return t2.At(t); }, t2.end());
		EXPECT(r2.throws == (sp > 1.4f ? 1 : 0), "speed %.1f: throws %d", sp, r2.throws);
	}
}

void TestTooFast() {
	std::printf("a hand faster than the band (a tracking glitch)\n");
	Throw th;
	th.speed = 15.0f;
	th.swing = 0.05;   // 0.75 m
	th.back = 0.0f;
	th.windup = 0.10;
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	EXPECT(run.throws == 0, "fast: throws %d", run.throws);
}

void TestJumpInOneFrame() {
	std::printf("one frame's jump (a tracking glitch) is not a swing\n");
	// 0.3 m in a single frame (27 m/s) with the trigger down, the hand then still: smoothed over
	// the velocity window it would look like 9 m/s and 0.3 m of travel - a throw
	Detector d;
	const Run run = Feed(d, [](double t) {
		Frame f;
		f.hand = {0.0f, 1.4f, t >= 0.5 ? -0.3f : 0.0f};
		f.trigger = t >= 0.2 && t < 0.52;
		return f;
	}, 1.0);
	EXPECT(run.started == 1 && run.throws == 0, "started %d throws %d", run.started, run.throws);
}

void TestTravel() {
	std::printf("travel from the edge\n");
	// a quick flick: fast enough but only 0.1 m from where the trigger went down
	Throw th;
	th.back = 0.0f;
	th.windup = 0.10;
	th.speed = 3.0f;
	th.swing = 0.033;   // ~0.1 m
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	EXPECT(run.throws == 0, "short flick: throws %d", run.throws);
	// the same speed over 0.3 m
	Throw t2 = th;
	t2.swing = 0.10;
	Detector d2;
	const Run r2 = Feed(d2, [&](double t) { return t2.At(t); }, t2.end());
	EXPECT(r2.throws == 1, "long flick: throws %d", r2.throws);
}

void TestGrace() {
	std::printf("the hand stops before the release\n");
	for (double coast : {0.04, 0.07, 0.12, 0.30}) {
		Throw th;
		th.coast = coast;
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		// the last frame with the band's speed is the end of the swing; the window lags it by up to windowS
		const bool wantThrow = coast <= 0.07;
		EXPECT(run.throws == (wantThrow ? 1 : 0), "coast %.2f s: throws %d", coast, run.throws);
		if (run.throws) EXPECT(HeadingError(run.last, 0.0f, -1.0f) < 3.0, "coast %.2f: heading %.2f", coast, HeadingError(run.last, 0.0f, -1.0f));
	}
}

void TestHeld() {
	std::printf("how long the trigger is held\n");
	// edge, a fast swing, release within 0.06 s: too quick to be a throw
	{
		Throw th;
		th.windup = 0.0;
		th.back = 0.0f;
		th.swing = 0.055;
		th.speed = 6.0f;
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 0, "0.06 s: throws %d", run.throws);
	}
	// a hold of 3.5 s with a good swing at the end
	{
		Throw th;
		th.windup = 3.3;
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 0, "3.5 s: throws %d", run.throws);
	}
	// 2.5 s is fine
	{
		Throw th;
		th.windup = 2.3;
		Detector d;
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 1, "2.5 s: throws %d", run.throws);
	}
}

void TestJabTriggerUp() {
	std::printf("a jab with the trigger up\n");
	Throw th;
	th.release = false;
	Detector d;
	// the same swing, the trigger never pressed: nothing starts, nothing is thrown
	const Run run = Feed(d, [&](double t) { Frame f = th.At(t); f.trigger = false; return f; }, th.end());
	EXPECT(run.started == 0 && run.throws == 0, "started %d throws %d", run.started, run.throws);
	EXPECT(!d.Throwing(), "throwing with the trigger up");
	// a jab, then the trigger down at its end and straight up: no gesture worth a throw
	Detector d2;
	const Run r2 = Feed(d2, [&](double t) {
		Frame f = th.At(t);
		const double end = th.t0 + th.windup + th.swing;
		f.trigger = t >= end - 0.01 && t < end + 0.05;
		return f;
	}, th.end());
	EXPECT(r2.throws == 0, "late press: throws %d", r2.throws);
}

void TestTriggerAlreadyDown() {
	std::printf("a trigger already down when the detector starts\n");
	Throw th;
	th.t0 = 0.0;
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	EXPECT(run.started == 0 && run.throws == 0, "started %d throws %d", run.started, run.throws);
	// ...and a throw after it works
	Throw t2;
	t2.t0 = 0.2;
	const double shift = th.end() + 0.1;
	const Run r2 = Feed(d, [&](double t) { return t2.At(t); }, t2.end(), 90.0, shift);
	EXPECT(r2.throws == 1, "the next throw: %d", r2.throws);
}

void TestCancel() {
	std::printf("cancels\n");
	Throw th;
	// a cancel in the middle of the swing (a menu opened, the weapon changed, a recentre)
	const double mid = th.t0 + th.windup + th.swing * 0.5;
	{
		Detector d;
		const Run run = Feed(d, [&](double t) { Frame f = th.At(t); f.cancel = std::fabs(t - mid) < 0.006; return f; }, th.end());
		EXPECT(run.throws == 0, "cancel: throws %d", run.throws);
		EXPECT(!d.Throwing(), "cancel: still throwing");
	}
	// the cancel while the trigger is still held: letting go afterwards is not a throw
	{
		Detector d;
		const Run run = Feed(d, [&](double t) { Frame f = th.At(t); f.cancel = t >= th.t0 + 0.1 && t < th.t0 + 0.12; return f; }, th.end());
		EXPECT(run.throws == 0, "cancel, then release: throws %d", run.throws);
	}
	// tracking lost for a few frames mid-swing
	{
		Detector d;
		const Run run = Feed(d, [&](double t) { Frame f = th.At(t); f.tracked = !(t >= mid - 0.02 && t < mid + 0.02); return f; }, th.end());
		EXPECT(run.throws == 0, "tracking lost: throws %d", run.throws);
	}
	// after a cancel, a new press and throw works
	{
		Detector d;
		Run a = Feed(d, [&](double t) { Frame f = th.At(t); f.cancel = std::fabs(t - mid) < 0.006; return f; }, th.end());
		Throw t2;
		const Run b = Feed(d, [&](double t) { return t2.At(t); }, t2.end(), 90.0, th.end() + 0.1);
		EXPECT(a.throws == 0 && b.throws == 1, "after a cancel: %d then %d", a.throws, b.throws);
	}
	// Cancel() called by the owner
	{
		Detector d;
		Sample s;
		s.t = 0.0; s.hand = {0, 1, 0};
		d.Step(s);
		s.t = 0.011; s.trigger = true;
		EXPECT(d.Step(s).started, "no edge");
		EXPECT(d.Throwing(), "not throwing after the edge");
		d.Cancel();
		EXPECT(!d.Throwing(), "throwing after Cancel");
	}
}

void TestDiscontinuities() {
	std::printf("a recentre, a hitch, a glitch\n");
	Throw th;
	const double mid = th.t0 + th.windup + th.swing * 0.5;
	// the pose jumps a metre in one frame (a recentre) during the swing
	{
		Detector d;
		const Run run = Feed(d, [&](double t) {
			Frame f = th.At(t);
			if (t >= mid) f.hand = Add(f.hand, Vec3{0.0f, 0.0f, -1.0f});
			return f;
		}, th.end());
		EXPECT(run.throws == 0, "recentre: throws %d", run.throws);
	}
	// a 150 ms hitch (no frames) mid-swing
	{
		Detector d;
		Run run;
		const double dt = 1.0 / 90.0;
		for (int i = 0; i * dt <= th.end(); ++i) {
			const double t = i * dt;
			if (t > mid && t < mid + 0.15) continue;
			Frame f = th.At(t);
			Sample s; s.t = t; s.hand = f.hand; s.trigger = f.trigger;
			const Result r = d.Step(s);
			if (r.throwNow) ++run.throws;
		}
		EXPECT(run.throws == 0, "hitch: throws %d", run.throws);
	}
	// time going backwards
	{
		Detector d;
		Sample s;
		s.t = 1.0; s.hand = {0, 1, 0};
		d.Step(s);
		s.t = 1.011; s.trigger = true;
		d.Step(s);
		EXPECT(d.Throwing(), "no gesture");
		s.t = 0.5;
		d.Step(s);
		EXPECT(!d.Throwing(), "a gesture across a jump back in time");
	}
	// non-finite poses
	{
		Detector d;
		Sample s;
		s.t = 0.0; s.hand = {0, 1, 0};
		d.Step(s);
		s.t = 0.011; s.trigger = true;
		d.Step(s);
		s.t = 0.022; s.hand = {NAN, 1.0f, 0.0f};
		const Result r = d.Step(s);
		EXPECT(!r.throwNow && !d.Throwing(), "a NaN pose");
	}
}

void TestSameFrameTwice() {
	std::printf("the same frame again\n");
	Throw th;
	Detector d;
	Run run;
	const double dt = 1.0 / 90.0;
	for (int i = 0; i * dt <= th.end(); ++i) {
		const double t = i * dt;
		Frame f = th.At(t);
		Sample s; s.t = t; s.hand = f.hand; s.trigger = f.trigger;
		for (int again = 0; again < 3; ++again) {   // the plugin may read a frame's pose more than once
			const Result r = d.Step(s);
			if (r.throwNow) ++run.throws;
			if (r.started) ++run.started;
		}
	}
	EXPECT(run.started == 1 && run.throws == 1, "started %d throws %d", run.started, run.throws);
}

void TestNoise() {
	std::printf("a hand with a millimetre of tracking noise\n");
	// a deterministic pseudo-noise, +-1.5 mm per axis
	unsigned seed = 12345;
	auto noise = [&]() {
		seed = seed * 1664525u + 1013904223u;
		return ((float)((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 0.003f;
	};
	int throws = 0, trials = 0;
	double worst = 0.0;
	for (int deg = 0; deg < 360; deg += 20) {
		const double a = deg * kPi / 180.0;
		Throw th;
		th.dir = {(float)std::cos(a), 0.0f, (float)std::sin(a)};
		th.speed = 4.0f;
		Detector d;
		const Run run = Feed(d, [&](double t) {
			Frame f = th.At(t);
			f.hand = Add(f.hand, Vec3{noise(), noise(), noise()});
			return f;
		}, th.end());
		++trials;
		throws += run.throws;
		if (run.throws == 1) worst = std::fmax(worst, HeadingError(run.last, th.dir.x, th.dir.z));
	}
	EXPECT(throws == trials, "%d of %d throws", throws, trials);
	EXPECT(worst < 8.0, "worst heading error %.2f deg", worst);
	std::printf("  worst heading error %.2f deg\n", worst);
	// noise alone: a hand held still with a held trigger and a released one never throws
	Detector d;
	int stray = 0;
	for (int i = 0; i < 90 * 20; ++i) {
		Sample s;
		s.t = i / 90.0;
		s.hand = {noise(), 1.4f + noise(), noise()};
		s.trigger = (i / 45) % 2 == 1;   // pressed and released every half second
		if (d.Step(s).throwNow) ++stray;
	}
	EXPECT(stray == 0, "%d throws from tracking noise alone", stray);
}

void TestRepeatedThrows() {
	std::printf("three throws in a row, one detector\n");
	Detector d;
	int throws = 0;
	double t0 = 0.0;
	for (int n = 0; n < 3; ++n) {
		Throw th;
		th.dir = {(float)std::cos(n * 1.0), 0.0f, (float)std::sin(n * 1.0)};
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end(), 90.0, t0);
		throws += run.throws;
		EXPECT(run.throws == 1, "throw %d: %d", n, run.throws);
		EXPECT(run.throws != 1 || HeadingError(run.last, th.dir.x, th.dir.z) < 3.0, "throw %d: heading", n);
		t0 += th.end() + 0.05;
	}
	EXPECT(throws == 3, "%d throws", throws);
}

void TestBackswingRelease() {
	std::printf("the trigger let go at the end of the wind-up\n");
	// The hand drew back at a throw's speed and the trigger opened there, no swing forward. The
	// spec calls that a throw (backwards): the numbers hold, and the heading is the hand's way.
	// It is said here so a change of the rules is a visible one.
	Throw th;
	th.windup = 0.12;
	th.back = 0.30f;
	th.swing = 0.0;
	th.coast = 0.0;
	Detector d;
	const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
	std::printf("  throws %d%s\n", run.throws, run.throws ? (run.last.dir.z > 0.0f ? " (backwards)" : " (forwards)") : "");
}

void TestCustomParams() {
	std::printf("parameters\n");
	Params p;
	p.minTravelM = 0.5f;
	Throw th;   // 0.8 m swing, 0.25 m wind-up: passes 0.5
	Detector d(p);
	EXPECT(Feed(d, [&](double t) { return th.At(t); }, th.end()).throws == 1, "travel 0.5, long swing");
	th.swing = 0.06;   // 0.3 m
	th.back = 0.1f;
	Detector d2(p);
	EXPECT(Feed(d2, [&](double t) { return th.At(t); }, th.end()).throws == 0, "travel 0.5, short swing");
	// windowS = 0: one frame's step, as the fork has it
	Params q;
	q.windowS = 0.0;
	Throw t3;
	Detector d3(q);
	const Run r3 = Feed(d3, [&](double t) { return t3.At(t); }, t3.end());
	EXPECT(r3.throws == 1 && HeadingError(r3.last, 0.0f, -1.0f) < 3.0, "per-frame velocity: %d", r3.throws);
}

// -- hold mode: the control is a grip held as long as the weapon is in the hand ([hands] throw_by_grip) ---------------------

Params HoldParams() {
	Params p;
	p.hold = true;
	return p;
}

void TestHoldMode() {
	std::printf("hold mode: the grip held long, a swing, let go at its end\n");
	// held 10 s with the hand at rest, then an overhand swing: a throw - and the same in trigger mode is not (held > 3 s)
	{
		Throw th;
		th.idle = 10.0;
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.started == 1 && run.throws == 1, "held 10 s: started %d throws %d", run.started, run.throws);
		EXPECT(run.throws != 1 || HeadingError(run.last, 0.0f, -1.0f) < 3.0, "held 10 s: heading");
		EXPECT(run.throws != 1 || run.last.travel >= 0.18f, "held 10 s: travel %.3f", run.last.travel);
		Detector t(Params{});
		const Run rt = Feed(t, [&](double tt) { return th.At(tt); }, th.end());
		EXPECT(rt.throws == 0, "trigger mode after 10 s: throws %d", rt.throws);
	}
	// held and let go at rest: nothing, at any length of hold
	for (double hold : {0.5, 5.0, 60.0}) {
		Throw th;
		th.windup = 0.0; th.swing = 0.0; th.back = 0.0f; th.speed = 0.0f;
		th.idle = hold;
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.started == 1 && run.throws == 0, "let go at rest after %.1f s: throws %d", hold, run.throws);
	}
	// held while the hand wanders slowly (0.5 m over 3 s), let go while it is still moving at 0.17 m/s
	{
		Detector d(HoldParams());
		const Run run = Feed(d, [](double t) {
			Frame f;
			f.hand = {0.0f, 1.4f, (float)(-0.17 * std::fmin(t, 3.2))};
			f.trigger = t >= 0.2 && t < 3.0;
			return f;
		}, 3.5);
		EXPECT(run.throws == 0, "slow wander: throws %d", run.throws);
	}
	// let go while the arm swings as in walking (about 0.9 m/s): under the band, nothing
	{
		Detector d(HoldParams());
		const Run run = Feed(d, [](double t) {
			Frame f;
			f.hand = {0.0f, 1.0f, (float)(0.14 * std::sin(t * 2.0 * kPi * 1.0))};   // +-0.14 m at 1 Hz: peak 0.88 m/s
			f.trigger = t >= 0.2 && t < 3.0 + 0.0;
			return f;
		}, 3.5);
		EXPECT(run.throws == 0, "a walking arm: throws %d", run.throws);
	}
	// the swing too short: a fast flick of 0.1 m, held long before
	{
		Throw th;
		th.idle = 4.0; th.back = 0.0f; th.windup = 0.05; th.speed = 3.0f; th.swing = 0.033;
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 0, "a 0.1 m flick: throws %d", run.throws);
		Throw t2 = th;
		t2.swing = 0.10;   // 0.3 m at the same speed
		Detector d2(HoldParams());
		const Run r2 = Feed(d2, [&](double t) { return t2.At(t); }, t2.end());
		EXPECT(r2.throws == 1, "a 0.3 m swing: throws %d", r2.throws);
	}
	// the travel is the swing's own, not the whole drift since the grip: 0.3 m crept over 5 s, then a 0.12 m flick
	{
		const double flickAt = 5.2;
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) {
			Frame f;
			float z = (float)(-0.06 * std::fmin(t, flickAt));   // the creep: 0.06 m/s, under the band
			if (t > flickAt) z -= (float)(4.0 * std::fmin(t - flickAt, 0.03));   // the flick: 0.12 m at 4 m/s
			f.hand = {0.0f, 1.4f, z};
			f.trigger = t >= 0.2 && t < flickAt + 0.035;
			return f;
		}, flickAt + 0.5);
		EXPECT(run.throws == 0, "creep then a short flick: throws %d", run.throws);
	}
	// the grace: the swing ended 0.04 s / 0.30 s before the release
	for (double coast : {0.04, 0.30}) {
		Throw th;
		th.idle = 3.0; th.coast = coast;
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == (coast < 0.1 ? 1 : 0), "coast %.2f s: throws %d", coast, run.throws);
	}
	// every heading after a long hold, three frame rates
	{
		double worst = 0.0;
		int missed = 0;
		for (double hz : {72.0, 90.0, 120.0}) {
			for (int deg = 0; deg < 360; deg += 30) {
				const double a = deg * kPi / 180.0;
				Throw th;
				th.idle = 5.0;
				th.dir = {(float)std::cos(a), 0.0f, (float)std::sin(a)};
				Detector d(HoldParams());
				const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end(), hz);
				if (run.throws != 1) { ++missed; continue; }
				worst = std::fmax(worst, HeadingError(run.last, th.dir.x, th.dir.z));
			}
		}
		EXPECT(missed == 0 && worst < 3.0, "hold mode headings: %d missed, worst %.2f deg", missed, worst);
	}
	// a cancel while the grip is still held: letting go afterwards is no throw; the next hold and swing is
	{
		Throw th;
		th.idle = 2.0;
		Detector d(HoldParams());
		const double mid = th.t0 + th.idle + th.windup + th.swing * 0.5;
		const Run run = Feed(d, [&](double t) { Frame f = th.At(t); f.cancel = std::fabs(t - mid) < 0.006; return f; }, th.end());
		EXPECT(run.throws == 0, "cancel mid-swing: throws %d", run.throws);
		Throw t2;
		t2.idle = 1.5;
		const Run r2 = Feed(d, [&](double t) { return t2.At(t); }, t2.end(), 90.0, th.end() + 0.1);
		EXPECT(r2.throws == 1, "the next hold and swing: throws %d", r2.throws);
	}
	// tracking noise (1.5 mm) through a 20 s hold, let go at rest: nothing
	{
		unsigned seed = 777;
		auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return ((float)((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 0.003f; };
		Detector d(HoldParams());
		int stray = 0;
		for (int i = 0; i < 90 * 25; ++i) {
			Sample s;
			s.t = i / 90.0;
			s.hand = {noise(), 1.4f + noise(), noise()};
			s.trigger = s.t >= 0.5 && s.t < 20.0 + (i % 7) * 0.0;
			if (d.Step(s).throwNow) ++stray;
		}
		EXPECT(stray == 0, "%d throws from tracking noise alone", stray);
	}
	// two throws in a row, each after its own hold
	{
		Detector d(HoldParams());
		double t0 = 0.0;
		int throws = 0;
		for (int n = 0; n < 2; ++n) {
			Throw th;
			th.t0 = 0.2;
			th.idle = 1.0 + n;
			th.dir = {(float)std::cos(n * 2.0), 0.0f, (float)std::sin(n * 2.0)};
			const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end(), 90.0, t0);
			throws += run.throws;
			t0 += th.end() + 0.1;
		}
		EXPECT(throws == 2, "two holds: %d throws", throws);
	}
}

// -- the peak velocity before the release (what a thrown potion's flight is made from) ------------------------------------------

// A hand whose velocity is a piecewise-linear profile: {duration, from, to}. Position by 1 ms steps from `home`.
struct Seg { double dur; Vec3 from, to; };
struct Profile {
	Vec3 home{0.0f, 1.5f, 0.0f};
	double t0 = 0.2;               // the trigger goes down here, the hand at rest
	double rest = 0.1;             // then rests this long before the profile starts
	std::vector<Seg> segs;
	double Total() const { double d = 0; for (const Seg& g : segs) d += g.dur; return d; }
	double end() const { return t0 + rest + Total() + 0.5; }
	Frame At(double t) const {
		Frame f;
		f.hand = home;
		const double start = t0 + rest;
		double clock = start;
		Vec3 p = home;
		for (const Seg& g : segs) {
			const double upto = std::fmin(t, clock + g.dur);
			for (double u = clock; u < upto - 1e-12; u += 0.001) {
				const double step = std::fmin(0.001, upto - u);
				const double k = ((u + step * 0.5) - clock) / g.dur;
				const Vec3 v = Lerp(g.from, g.to, k);
				p = Add(p, Scale(v, (float)step));
			}
			clock += g.dur;
			if (t <= clock) break;
		}
		f.hand = p;
		const double rel = start + Total() + 0.003;
		f.trigger = t >= t0 && t < rel;
		return f;
	}
};

void TestPeak() {
	std::printf("the peak velocity before the release\n");
	const Vec3 z{};
	// a swing that is already slowing at the release: up to 6 m/s along -z in 0.10 s, down to 1.8 m/s in 0.12 s, let go
	{
		Profile p;
		p.segs = {{0.10, z, {0, 0, -6.0f}}, {0.12, {0, 0, -6.0f}, {0, 0, -1.8f}}};
		Detector d;
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		EXPECT(run.throws == 1, "throws %d", run.throws);
		EXPECT(run.last.speed < 3.6f, "the release's own speed %.2f", run.last.speed);
		EXPECT(run.last.peakSpeed > 4.8f && run.last.peakSpeed <= 6.3f, "the peak %.2f m/s", run.last.peakSpeed);
		EXPECT(run.last.peak.z < -4.0f && std::fabs(run.last.peak.y) < 0.2f, "peak %.2f %.2f %.2f", run.last.peak.x, run.last.peak.y, run.last.peak.z);
		EXPECT(run.last.peakSpeed >= run.last.speed, "never slower than the release");
	}
	// the end of a swing goes DOWN, the strong part of it went forward and up: the elevation is the strong part's
	{
		Profile p;
		p.segs = {{0.10, {0, 2.0f, -4.6f}, {0, 2.0f, -4.6f}}, {0.10, {0, -2.5f, -2.5f}, {0, -2.5f, -2.5f}}};
		Detector d;
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		EXPECT(run.throws == 1, "throws %d", run.throws);
		EXPECT(run.last.dir.y < -0.1f, "the release moment points down: %.2f", run.last.dir.y);
		EXPECT(run.last.peak.y > 0.5f && run.last.peak.z < -3.0f, "the peak points up and ahead: %.2f %.2f", run.last.peak.y, run.last.peak.z);
	}
	// an older burst is outside the window: 6 m/s ending 0.3 s before the release, then 2.5 m/s to the end
	{
		Profile p;
		p.segs = {{0.10, {0, 0, -6.0f}, {0, 0, -6.0f}}, {0.30, {0, 0, -2.5f}, {0, 0, -2.5f}}};
		Detector d;
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		EXPECT(run.throws == 1, "throws %d", run.throws);
		EXPECT(run.last.peakSpeed > 2.2f && run.last.peakSpeed < 3.2f, "the old burst counted: %.2f m/s", run.last.peakSpeed);
	}
	// a steady hand: the peak is the speed
	{
		Profile p;
		p.segs = {{0.30, {0, 0, -4.0f}, {0, 0, -4.0f}}};
		Detector d;
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		// (the release's own windowed speed is lower: the hand stopped a frame or two before the trigger went up)
		EXPECT(run.throws == 1 && std::fabs(run.last.peakSpeed - 4.0f) < 0.2f && run.last.peakSpeed >= run.last.speed,
		       "steady: peak %.2f speed %.2f", run.last.peakSpeed, run.last.speed);
	}
	// 1.5 mm of tracking noise: the peak is within 25% and 12 degrees of the truth
	{
		unsigned seed = 4242;
		auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return ((float)((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f) * 0.003f; };
		Profile p;
		p.segs = {{0.08, z, {0, 1.0f, -4.5f}}, {0.12, {0, 1.0f, -4.5f}, {0, -1.0f, -2.5f}}};
		double worstSpeed = 0.0, worstAngle = 0.0;
		int n = 0;
		for (int trial = 0; trial < 12; ++trial) {
			Detector d;
			const Run run = Feed(d, [&](double t) { Frame f = p.At(t); f.hand = Add(f.hand, Vec3{noise(), noise(), noise()}); return f; }, p.end());
			if (run.throws != 1) continue;
			++n;
			const double truth = std::sqrt(1.0 + 4.5 * 4.5);   // 4.6 m/s at its fastest, 12 deg up
			worstSpeed = std::fmax(worstSpeed, std::fabs(run.last.peakSpeed - truth) / truth);
			const double el = std::atan2((double)run.last.peak.y, std::sqrt((double)run.last.peak.x * run.last.peak.x + (double)run.last.peak.z * run.last.peak.z)) * 180.0 / kPi;
			worstAngle = std::fmax(worstAngle, std::fabs(el - 12.5));
		}
		EXPECT(n == 12, "%d of 12 throws", n);
		EXPECT(worstSpeed < 0.25 && worstAngle < 12.0, "worst speed error %.0f%%, elevation error %.1f deg", worstSpeed * 100.0, worstAngle);
	}
	// the thresholds that decide a throw are those of the release, not of the peak: a hand at rest with a fast flick 0.12 s ago is no throw
	{
		Profile p;
		p.segs = {{0.05, z, {0, 0, -6.0f}}, {0.05, {0, 0, -6.0f}, z}, {0.20, z, z}};
		Detector d;
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		EXPECT(run.throws == 0, "a flick long before the release: throws %d", run.throws);
	}
	// the window and the count are parameters
	{
		Profile p;
		p.segs = {{0.10, z, {0, 0, -6.0f}}, {0.12, {0, 0, -6.0f}, {0, 0, -1.8f}}};
		Params q;
		q.peakWindowS = 0.02;   // nearly the release's own moment
		Detector d(q);
		const Run run = Feed(d, [&](double t) { return p.At(t); }, p.end());
		EXPECT(run.throws == 1 && run.last.peakSpeed < 4.0f, "a 20 ms window: %.2f", run.last.peakSpeed);
	}
}

// The reason a release was not a throw (the log says it: a test in the headset should tell what it saw).
Why LastWhy(Detector& d, const Script& script, double end) {
	Why last = Why::None;
	const double dt = 1.0 / 90.0;
	for (int i = 0; i * dt <= end + 1e-9; ++i) {
		const Frame f = script(i * dt);
		Sample s;
		s.t = i * dt; s.hand = f.hand; s.trigger = f.trigger; s.tracked = f.tracked; s.cancel = f.cancel;
		const Result r = d.Step(s);
		if (r.released) last = r.throwNow ? Why::None : r.why;
	}
	return last;
}

void TestWhy() {
	std::printf("why a release was not a throw\n");
	{   // the hand at rest
		Throw th; th.windup = 0.0; th.swing = 0.0; th.back = 0.0f; th.speed = 0.0f; th.idle = 0.5;
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::NoFastSample, "at rest");
	}
	{   // the swing ended 0.3 s before the release
		Throw th; th.coast = 0.30;
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::Stale, "stale");
	}
	{   // fast enough but 0.1 m
		Throw th; th.back = 0.0f; th.windup = 0.10; th.speed = 3.0f; th.swing = 0.033;
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::ShortTravel, "short");
	}
	{   // let go 0.06 s after the edge
		Throw th; th.windup = 0.0; th.back = 0.0f; th.swing = 0.055; th.speed = 6.0f;
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::Quick, "quick");
	}
	{   // a throw straight up
		Throw th; th.dir = {0.0f, 1.0f, -0.1f};
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::Steep, "steep");
	}
	{   // a throw is None
		Throw th;
		Detector d;
		EXPECT(LastWhy(d, [&](double t) { return th.At(t); }, th.end()) == Why::None, "a throw");
	}
}

}  // namespace

// -- BeginHeld: a gesture under way with the grip already down ([hands] throw_left_hand: the weapon handed over to a squeezed grip) --------
void TestBeginHeld() {
	std::printf("begin held: the grip already squeezed when the weapon came into the hand\n");
	Throw th;
	th.t0 = 0.0;    // the grip down from the first frame
	th.idle = 1.0;
	const double dt = 1.0 / 90.0;
	{   // without it: a grip already down is an earlier grip's - nothing, as ever
		Detector d(HoldParams());
		const Run run = Feed(d, [&](double t) { return th.At(t); }, th.end());
		EXPECT(run.throws == 0, "no BeginHeld: throws %d", run.throws);
	}
	{   // with it: the swing and the release throw, along the swing
		Detector d(HoldParams());
		d.BeginHeld(0.0, th.At(0.0).hand);
		EXPECT(d.Throwing(), "under way at once");
		const Run run = Feed(d, [&](double t) { return th.At(t + dt); }, th.end() - dt, 90.0, dt);
		EXPECT(run.started == 0 && run.throws == 1, "BeginHeld: started %d throws %d", run.started, run.throws);
		EXPECT(run.throws != 1 || HeadingError(run.last, 0.0f, -1.0f) < 3.0, "BeginHeld: heading");
	}
	{   // ... and a release at rest after it is still nothing
		Throw rest = th;
		rest.windup = 0.0; rest.swing = 0.0; rest.back = 0.0f; rest.speed = 0.0f;
		Detector d(HoldParams());
		d.BeginHeld(0.0, rest.At(0.0).hand);
		const Run run = Feed(d, [&](double t) { return rest.At(t + dt); }, rest.end() - dt, 90.0, dt);
		EXPECT(run.throws == 0, "BeginHeld, let go at rest: throws %d", run.throws);
	}
	{   // a Step at the same moment is nothing new; a hand not finite begins nothing
		Detector d(HoldParams());
		d.BeginHeld(5.0, {0.0f, 1.4f, 0.0f});
		Sample s; s.t = 5.0; s.hand = {0.0f, 1.4f, 0.0f}; s.trigger = true;
		const Result r = d.Step(s);
		EXPECT(!r.started && !r.released && d.Throwing(), "the same moment again");
		Detector bad(HoldParams());
		bad.BeginHeld(1.0, {NAN, 0.0f, 0.0f});
		EXPECT(!bad.Throwing(), "not finite: nothing");
	}
}

int main() {
	TestOverhand();
	TestHeadFrame();
	TestHeadings();
	TestElevation();
	TestStationaryRelease();
	TestTooSlow();
	TestTooFast();
	TestJumpInOneFrame();
	TestTravel();
	TestGrace();
	TestHeld();
	TestJabTriggerUp();
	TestTriggerAlreadyDown();
	TestCancel();
	TestDiscontinuities();
	TestSameFrameTwice();
	TestNoise();
	TestRepeatedThrows();
	TestBackswingRelease();
	TestCustomParams();
	TestHoldMode();
	TestWhy();
	TestPeak();
	TestBeginHeld();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
