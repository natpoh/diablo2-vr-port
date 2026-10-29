// Offline checks of the camera math against the game's conventions as read from
// the snapshot (RECON.md section 1). Run: build/Release/cleanroom_math_test.exe
//   1. view -> (eye, quaternion) -> the game's own view rebuild gives the view back
//   2. a world point projected to a pixel lies on the ray RayThrough returns
//      (perspective with an off-axis shift, and orthographic)
//   3. with the game's own matrices RayThrough equals the game's ScreenToRay
//      formula (0xED62B0: dir = Z - sx*tan*aspect*X + sy*tan*Y, origin = pos)

#include "../camera/mat4.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace d2rcam::m4;

static int g_failures = 0;

static void Expect(bool ok, const char* what, double value) {
	if (!ok) {
		++g_failures;
		std::printf("FAIL %s (%g)\n", what, value);
	}
}

static void RandomQuat(std::mt19937& rng, float q[4]) {
	std::normal_distribution<float> n(0.0f, 1.0f);
	float l = 0;
	for (int i = 0; i < 4; ++i) {
		q[i] = n(rng);
		l += q[i] * q[i];
	}
	l = std::sqrt(l);
	for (int i = 0; i < 4; ++i) {
		q[i] /= l;
	}
}

static void GameView(V3 pos, const float q[4], float out[16]) {
	LookTo(pos, Rotate(q, { 0, 0, 1 }), Rotate(q, { 0, 1, 0 }), out);
}

static void Transform(const float p[4], const float m[16], float out[4]) {
	for (int j = 0; j < 4; ++j) {
		out[j] = p[0] * m[0 * 4 + j] + p[1] * m[1 * 4 + j] + p[2] * m[2 * 4 + j] + p[3] * m[3 * 4 + j];
	}
}

static float DistanceToRay(V3 p, V3 o, V3 d) {
	const V3 v = p - o;
	const V3 c = Cross(v, d);
	return Length(c);
}

int main() {
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> u(-1.0f, 1.0f);

	// 1. Pose round trip, the same steps as d2rcam.cpp SyncFrame.
	double worst = 0;
	for (int i = 0; i < 1000; ++i) {
		float q[4];
		RandomQuat(rng, q);
		const V3 pos { u(rng) * 3000.0f, u(rng) * 200.0f, u(rng) * 3000.0f };
		float view[16], inv[16];
		GameView(pos, q, view);
		Inverse(view, inv);
		const V3 eye { inv[12], inv[13], inv[14] };
		const V3 fwd = Normalize({ -inv[8], -inv[9], -inv[10] });
		const V3 lx  = Normalize(Cross({ inv[4], inv[5], inv[6] }, fwd));
		const V3 up  = Cross(fwd, lx);
		float q2[4];
		QuatFromAxes(lx, up, fwd, q2);
		float view2[16];
		GameView(eye, q2, view2);
		for (int k = 0; k < 16; ++k) {
			const double scale = k >= 12 ? 3000.0 : 1.0;
			worst = std::fmax(worst, std::fabs(view[k] - view2[k]) / scale);
		}
	}
	Expect(worst < 1e-4, "pose round trip", worst);
	std::printf("pose round trip: worst relative error %.2e\n", worst);

	// 2. Pixel -> ray passes through the projected point.
	worst = 0;
	for (int i = 0; i < 1000; ++i) {
		float q[4];
		RandomQuat(rng, q);
		const V3 pos { u(rng) * 300.0f, u(rng) * 50.0f, u(rng) * 300.0f };
		float view[16], inv[16], proj[16];
		GameView(pos, q, view);
		Inverse(view, inv);
		const bool ortho = (i % 4) == 3;
		if (ortho) {
			OrthoRevZ(-40, 40, -22, 22, 10000, 1, proj);
		} else {
			PerspectiveRevZ(1.2f, 16.0f / 9.0f, 0.25f, proj);
			proj[8] = 0.1f * u(rng);   // off-axis shift as a VR eye has
			proj[9] = 0.05f * u(rng);
		}
		// a point in front of the camera
		const V3 local { u(rng) * 5.0f, u(rng) * 3.0f, -(2.0f + 30.0f * (u(rng) + 1.0f)) };
		const float lp[4] { local.x, local.y, local.z, 1 };
		float world[4];
		Transform(lp, inv, world);
		float vp[16], clip[4];
		Mul(view, proj, vp);
		Transform(world, vp, clip);
		const float W = 1920, H = 1080;
		const float sx = (clip[0] / clip[3] + 1.0f) * 0.5f * W;
		const float sy = (0.5f - 0.5f * clip[1] / clip[3]) * H;   // the game's WorldToScreen (0xED5EB0)
		float o[3], d[3];
		const bool ok = RayThrough(inv, proj, W, H, sx, sy, o, d);
		Expect(ok, "RayThrough ok", 0);
		const float dist = DistanceToRay({ world[0], world[1], world[2] }, { o[0], o[1], o[2] }, { d[0], d[1], d[2] });
		worst = std::fmax(worst, dist);
		if (!ortho) {
			Expect(Length(V3 { o[0], o[1], o[2] } - pos) < 1e-2f, "perspective ray starts at the eye", Length(V3 { o[0], o[1], o[2] } - pos));
		}
		// the ray points toward the point, not away from it
		const V3 to = V3 { world[0], world[1], world[2] } - V3 { o[0], o[1], o[2] };
		Expect(Dot(to, { d[0], d[1], d[2] }) > 0, "ray direction sign", Dot(to, { d[0], d[1], d[2] }));
	}
	Expect(worst < 5e-2, "projected point on ray", worst);
	std::printf("pixel -> ray: worst miss %.2e world units\n", worst);

	// 3. Same as the game's ScreenToRay formula for the game's own camera.
	worst = 0;
	for (int i = 0; i < 1000; ++i) {
		float q[4];
		RandomQuat(rng, q);
		const V3 pos { u(rng) * 300.0f, u(rng) * 50.0f, u(rng) * 300.0f };
		const float fovDeg = 3.44f + 80.0f * (u(rng) + 1.0f) * 0.5f;
		const float W = 1600, H = 900, aspect = W / H;
		float view[16], inv[16], proj[16];
		GameView(pos, q, view);
		Inverse(view, inv);
		PerspectiveRevZ(fovDeg * 3.14159265f / 180.0f, aspect, 10.0f, proj);
		const float px = (u(rng) + 1.0f) * 0.5f * W, py = (u(rng) + 1.0f) * 0.5f * H;
		const float sx = 2.0f * px / W - 1.0f, sy = 1.0f - 2.0f * py / H;
		const float t  = std::tan(fovDeg * 3.14159265f / 180.0f * 0.5f);
		const V3 X = Rotate(q, { 1, 0, 0 }), Y = Rotate(q, { 0, 1, 0 }), Z = Rotate(q, { 0, 0, 1 });
		const V3 game = Normalize(Z - X * (sx * t * aspect) + Y * (sy * t));
		float o[3], d[3];
		RayThrough(inv, proj, W, H, px, py, o, d);
		worst = std::fmax(worst, Length(game - V3 { d[0], d[1], d[2] }));
	}
	Expect(worst < 1e-3, "matches the game's ScreenToRay", worst);
	std::printf("game ScreenToRay formula vs matrices: worst %.2e\n", worst);

	std::printf(g_failures == 0 ? "all checks passed\n" : "%d checks failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
