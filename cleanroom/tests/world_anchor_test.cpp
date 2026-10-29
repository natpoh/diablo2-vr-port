// vr/world_anchor.h, pure: how a thing that stays in the game's WORLD is carried between two head samples, in the room's axes. No game, no headset.
//
//   world_anchor_test

#include "../../vr/world_anchor.h"

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

unsigned g_seed = 12345u;
double Rand(double lo, double hi) {
	g_seed = g_seed * 1664525u + 1013904223u;
	return lo + (hi - lo) * ((g_seed >> 8) / 16777216.0);
}

// a snapshot from a head in the room and a camera in the world; `mirror` flips the world's handedness (camR the other way)
struct Setup {
	float head[3], yawDeg, eye[3], camR[3], camF[3], upm;
};
Setup RandomSetup(bool mirror) {
	Setup s;
	s.head[0] = (float)Rand(-1.5, 1.5); s.head[1] = (float)Rand(1.2, 1.9); s.head[2] = (float)Rand(-1.5, 1.5);
	s.yawDeg = (float)Rand(-180, 180);
	s.eye[0] = (float)Rand(-300, 300); s.eye[1] = 5.0f; s.eye[2] = (float)Rand(-300, 300);
	const double th = Rand(-3.14159, 3.14159);
	s.camF[0] = (float)std::sin(th); s.camF[1] = 0.0f; s.camF[2] = (float)-std::cos(th);
	// right = forward turned 90 degrees: (cos, 0, sin) for forward (sin, 0, -cos)
	s.camR[0] = (float)std::cos(th) * (mirror ? -1.0f : 1.0f); s.camR[1] = 0.0f; s.camR[2] = (float)std::sin(th) * (mirror ? -1.0f : 1.0f);
	s.upm = 3.5f;
	return s;
}
worldanchor::Snap SnapOf(const Setup& s) { return worldanchor::Make(s.head, s.yawDeg, s.eye, s.camR, s.camF, s.upm); }

// where a room point (x, z) stands in the world for a setup: eye + upm * C * R(-psi) * (p - H)
void WorldOf(const Setup& s, double px, double pz, double out[2]) {
	const double psi = (double)s.yawDeg * 0.017453292519943295, c = std::cos(-psi), sn = std::sin(-psi);
	const double dx = px - s.head[0], dz = pz - s.head[2];
	const double vx = dx * c + dz * sn, vz = -dx * sn + dz * c;
	out[0] = s.eye[0] + s.upm * (s.camR[0] * vx - s.camF[0] * vz);
	out[1] = s.eye[2] + s.upm * (s.camR[2] * vx - s.camF[2] * vz);
}

}   // namespace

int main() {
	// a thing that stays in the world: its room point after the move stands at the same world point - for random heads, cameras and both handednesses of the world
	for (int mirror = 0; mirror < 2; ++mirror) {
		double worst = 0.0;
		for (int i = 0; i < 2000; ++i) {
			const Setup a = RandomSetup(mirror != 0), b = RandomSetup(mirror != 0);
			const worldanchor::Move m = worldanchor::Between(SnapOf(a), SnapOf(b));
			EXPECT(m.ok, "the move between two random samples is not ok");
			if (!m.ok) continue;
			const double px = Rand(-3, 3), pz = Rand(-3, 3);
			double w0[2], w1[2];
			WorldOf(a, px, pz, w0);
			const double c = std::cos(m.delta), s = std::sin(m.delta);
			const double qx = px * c + pz * s + m.sx, qz = -px * s + pz * c + m.sz;
			WorldOf(b, qx, qz, w1);
			worst = std::fmax(worst, std::fmax(std::fabs(w0[0] - w1[0]), std::fabs(w0[1] - w1[1])));
		}
		EXPECT(worst < 0.01, "world %s: the carried point is %.5f world units off the fixed one", mirror ? "mirrored" : "proper", worst);
	}

	// the simple cases, in numbers: the camera walks 2 m forward - the thing is 2 m further back in the room; nothing moves - nothing is carried; the head's step with the camera after it - nothing
	{
		Setup a;
		a.head[0] = 0.0f; a.head[1] = 1.7f; a.head[2] = 0.0f; a.yawDeg = 0.0f;
		a.eye[0] = 100.0f; a.eye[1] = 5.0f; a.eye[2] = 100.0f;
		a.camR[0] = 1; a.camR[1] = 0; a.camR[2] = 0;
		a.camF[0] = 0; a.camF[1] = 0; a.camF[2] = -1;
		a.upm = 3.5f;
		worldanchor::Move m = worldanchor::Between(SnapOf(a), SnapOf(a));
		EXPECT(m.ok && std::fabs(m.delta) < 1e-12 && std::fabs(m.sx) < 1e-9 && std::fabs(m.sz) < 1e-9, "no move: delta %.9f shift %.9f %.9f", m.delta, m.sx, m.sz);
		Setup b = a;
		b.eye[2] -= 7.0f;   // 2 m forward (forward is -z in the world here)
		m = worldanchor::Between(SnapOf(a), SnapOf(b));
		EXPECT(m.ok && std::fabs(m.delta) < 1e-9 && std::fabs(m.sx) < 1e-9 && std::fabs(m.sz - 2.0) < 1e-6, "2 m forward: delta %.9f shift %.6f %.6f", m.delta, m.sx, m.sz);
		b = a;
		b.head[0] += 0.5f;
		b.eye[0] += 0.5f * 3.5f;   // the head steps 0.5 m to the right in the room, the camera follows
		m = worldanchor::Between(SnapOf(a), SnapOf(b));
		EXPECT(m.ok && std::fabs(m.delta) < 1e-9 && std::fabs(m.sx) < 1e-6 && std::fabs(m.sz) < 1e-6, "a step in the room with the camera after it carries nothing: shift %.6f %.6f", m.sx, m.sz);
		b = a;
		b.camR[0] = 0; b.camR[2] = -1; b.camF[0] = -1; b.camF[2] = 0;   // the world turned 90 degrees under the room
		m = worldanchor::Between(SnapOf(a), SnapOf(b));
		EXPECT(m.ok && std::fabs(std::fabs(m.delta) - 1.5707963) < 1e-6, "a quarter turn: delta %.6f", m.delta);
	}

	// data that cannot be: not a number, a degenerate frame, a scale that changed, a shear
	{
		Setup a = RandomSetup(false);
		worldanchor::Snap sa = SnapOf(a);
		EXPECT(sa.ok, "a normal snapshot is ok");
		Setup bad = a;
		bad.eye[0] = std::nanf("");
		EXPECT(!SnapOf(bad).ok, "a NaN eye is ok");
		bad = a;
		bad.camR[0] = bad.camF[0]; bad.camR[2] = bad.camF[2];
		EXPECT(!SnapOf(bad).ok, "a degenerate frame (right = forward) is ok");
		bad = a;
		bad.upm = 0.0f;
		EXPECT(!SnapOf(bad).ok, "no scale is ok");
		Setup far = RandomSetup(false);
		far.upm = a.upm * 3.0f;
		EXPECT(!worldanchor::Between(sa, SnapOf(far)).ok, "a scale that tripled is a move");
		Setup shear = RandomSetup(false);
		shear.camR[0] *= 2.0f;   // a frame whose axes are not square
		EXPECT(!worldanchor::Between(sa, SnapOf(shear)).ok, "a sheared frame is a move");
		worldanchor::Snap none;
		EXPECT(!worldanchor::Between(none, sa).ok && !worldanchor::Between(sa, none).ok, "a missing snapshot is a move");
	}

	if (g_fails) {
		std::printf("world_anchor_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("world_anchor_test: a thing fixed in the world stays fixed through any walk, turn and head step (both handednesses of the world), nothing moved carries nothing, impossible data is refused: all passed\n");
	return 0;
}
