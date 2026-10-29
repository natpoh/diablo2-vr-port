// vr/pointer3d.h: the game's mouse pointer in F1 and F3 in 3D - the ground point under a pixel of the game's window (the point a click would
// walk to) and where it stands in the room, against the game's own ray maths (d2rcam m4::RayThrough). No game, no OpenXR, no headset.
//
//   pointer3d_test       exit code 0 = all passed

#include "pointer3d.h"

#include "mat4.h"

#include <cstdio>

using namespace pointer3d;
using views3d::Quat;
using views3d::Rotate;
using views3d::Window;
using views3d::YawQuat;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                                  \
	do {                                                                          \
		++g_checks;                                                               \
		if (!(c)) { ++g_failed; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
	} while (0)
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps * std::max(1.0f, std::max(std::fabs(a), std::fabs(b))); }
static bool NearV(Vec a, Vec b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

static Quat Mul(const Quat& a, const Quat& b) {   // b first, then a
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static Quat AxisAngle(Vec a, float ang) {
	const float s = std::sin(0.5f * ang), n = views3d::Length(a);
	return {a.x / n * s, a.y / n * s, a.z / n * s, std::cos(0.5f * ang)};
}

// the game's own answer: the world ray through a pixel (u, v of the window) of the camera - what d2rcam hands the mouse code
static bool GameRay(const float view[16], const float proj[16], float u, float v, Vec* o, Vec* d) {
	float inv[16], org[3], dir[3];
	if (!d2rcam::m4::Inverse(view, inv)) return false;
	if (!d2rcam::m4::RayThrough(inv, proj, 1920.0f, 1080.0f, u * 1920.0f, v * 1080.0f, org, dir)) return false;
	*o = {org[0], org[1], org[2]};
	*d = {dir[0], dir[1], dir[2]};
	return true;
}
static float DistToRay(Vec p, Vec o, Vec d) {
	const Vec w = p - o;
	const Vec c = {w.y * d.z - w.z * d.y, w.z * d.x - w.x * d.z, w.x * d.y - w.y * d.x};
	return views3d::Length(c) / views3d::Length(d);
}
// a world point through the row-vector view and projection to a pixel (u, v from the top left); false behind the camera
static bool Project(const float view[16], const float proj[16], Vec p, float* u, float* v) {
	float pv[4], c[4];
	for (int j = 0; j < 4; ++j) pv[j] = p.x * view[0 * 4 + j] + p.y * view[1 * 4 + j] + p.z * view[2 * 4 + j] + view[3 * 4 + j];
	for (int j = 0; j < 4; ++j) c[j] = pv[0] * proj[0 * 4 + j] + pv[1] * proj[1 * 4 + j] + pv[2] * proj[2 * 4 + j] + pv[3] * proj[3 * 4 + j];
	if (!(c[3] > 1e-6f)) return false;
	*u = 0.5f * (c[0] / c[3] + 1.0f);
	*v = 0.5f * (1.0f - c[1] / c[3]);
	return true;
}

// the projection a native eye gets (vrcam VrProjInner): its frustum's tangents
static void EyeProj(float tanL, float tanR, float tanD, float tanU, float nearZ, float M[16]) {
	d2rcam::m4::PerspectiveRevZ(1.0f, 1.0f, nearZ, M);
	M[0] = 2.0f / (tanR - tanL);
	M[5] = 2.0f / (tanU - tanD);
	M[8] = (tanR + tanL) / (tanR - tanL);
	M[9] = (tanU + tanD) / (tanU - tanD);
}

int main() {
	// -- the maps ----------------------------------------------------------------------------------------------------
	{
		const views3d::Table t{0.4f, -1.1f, -1.65f, 0.9f, 37.0f, 0.1f, 0.05f};
		const Vec hero{250.0f, 4.0f, -80.0f};
		const Similarity m = ForTable(t, hero);
		const Vec p{0.9f, -0.1f, 0.3f};
		CHECK(NearV(views3d::TableEye(t, hero, p), ToWorld(m, p)));                      // the same map TableEye makes
		CHECK(NearV(ToWorld(m, {t.fx, t.floorY + t.lift, t.fz}), hero));                 // the floor point under the hero is the hero
		CHECK(Near(ToWorld(m, {1.0f, t.floorY + t.lift, 0.2f}).y, hero.y));              // the diorama's ground is the hero's ground
		CHECK(NearV(FromWorld(m, ToWorld(m, p)), p));                                    // and back
		const Quat camQ = Mul(YawQuat(0.8f), AxisAngle({1, 0, 0}, -0.61f));
		const Vec right = Rotate(camQ, {1, 0, 0}), up = Rotate(camQ, {0, 1, 0}), fwd = Rotate(camQ, {0, 0, -1});
		const Similarity f = ForWindow(hero, right, up, fwd, 22.0f);
		CHECK(NearV(ToWorld(f, {0, 0, 0}), hero));
		CHECK(NearV(ToWorld(f, {1, 0, 0}), hero + right * 22.0f));
		CHECK(NearV(ToWorld(f, {0, 1, 0}), hero + up * 22.0f));
		CHECK(NearV(ToWorld(f, {0, 0, 1}), hero - fwd * 22.0f));                         // towards the viewer is back along the camera's forward
		CHECK(NearV(FromWorld(f, ToWorld(f, {0.3f, -0.4f, 0.7f})), {0.3f, -0.4f, 0.7f}));
	}

	// -- F3: the diorama on the floor ---------------------------------------------------------------------------------
	{
		const views3d::Table t{0.4f, -1.1f, -1.65f, 0.9f, 37.0f, 0.0f, 0.05f};
		const Vec hero{250.0f, 4.0f, -80.0f};
		const Similarity m = ForTable(t, hero);
		const float groundY = hero.y;
		// two heads over the board: looking steeply down, and looking out (the top of the picture is above the horizon)
		for (float pitch : {-0.8f, -0.25f}) {
			const Vec head{0.9f, -0.15f, 0.5f};
			const Quat q = Mul(YawQuat(0.35f), AxisAngle({1, 0, 0}, pitch));
			const Vec rightR = Rotate(q, {1, 0, 0}), upR = Rotate(q, {0, 1, 0}), fwdR = Rotate(q, {0, 0, -1});
			const Vec eyeL = head - rightR * (0.5f * 0.063f);
			const Vec eyeW = views3d::TableEye(t, hero, eyeL);
			const Vec uW = Rotate(YawQuat(t.phi), upR), fW = Rotate(YawQuat(t.phi), fwdR);
			float V[16], P[16];
			d2rcam::m4::LookTo({eyeW.x, eyeW.y, eyeW.z}, {fW.x, fW.y, fW.z}, {uW.x, uW.y, uW.z}, V);
			EyeProj(-1.0f, 1.0f, -0.9f, 0.9f, 0.05f * t.scale, P);
			int hits = 0, misses = 0;
			for (float u : {0.1f, 0.5f, 0.9f})
				for (float v : {0.05f, 0.3f, 0.5f, 0.75f, 0.95f}) {
					const Under h = GroundUnderPixel(m, groundY, V, P, u, v);
					Vec o, d;
					CHECK(GameRay(V, P, u, v, &o, &d));
					if (!h.hit) { ++misses; CHECK(!(d.y < -1e-4f)); continue; }   // only a ray that does not go down misses
					++hits;
					CHECK(Near(h.world.y, groundY));                                  // on the game's ground
					CHECK(DistToRay(h.world, o, d) < 1e-3f * t.scale);                  // THE point: on the game's own ray through that pixel
					CHECK(Near(h.at.y, t.floorY + t.lift));                             // in the room, on the diorama's floor
					CHECK(NearV(ToWorld(m, h.at), h.world, 1e-3f));                     // the room point and the world point are one
					float pu, pv;
					CHECK(Project(V, P, h.world, &pu, &pv) && Near(pu, u, 2e-3f) && Near(pv, v, 2e-3f));   // the point is drawn under the pointer
					CHECK(Near(h.dist, views3d::Length(h.at - eyeL), 2e-3f));           // how far it is from the left eye, in metres
				}
			CHECK(hits > 0);
			if (pitch > -0.5f) CHECK(misses > 0);                                       // looking out: the picture's top is sky, no ground under it
			else CHECK(misses == 0);                                                    // looking down: all of it is ground
		}
	}

	// -- F1: the window ----------------------------------------------------------------------------------------------
	{
		const Window w = views3d::MakeWindow(2.4f, 2.0f, 16.0f / 9.0f);
		const views3d::Placed at = views3d::Place(w, 0.6f, {0.3f, 1.6f, -0.2f});
		const Vec hero{100.0f, 3.0f, -40.0f};
		const Quat camQ = Mul(YawQuat(0.8f), AxisAngle({1, 0, 0}, -0.61f));   // the game's overhead axes: down and ahead
		const Vec right = Rotate(camQ, {1, 0, 0}), up = Rotate(camQ, {0, 1, 0}), fwd = Rotate(camQ, {0, 0, -1});
		const float half = 7.5f, dist = views3d::OverheadDistance(half, w), upm = dist / w.dist;
		const Similarity m = ForWindow(hero, right, up, fwd, upm);
		const float groundY = hero.y;
		for (Vec lean : {Vec{0.0f, 0.0f, 0.0f}, Vec{0.25f, 0.05f, 0.4f}, Vec{-0.4f, -0.1f, -0.2f}}) {
			const Vec headRoom = Vec{0.3f, 1.6f, -0.2f} + Rotate(YawQuat(at.yaw), lean);
			const Vec rr = Rotate(YawQuat(at.yaw), {1, 0, 0});
			const views3d::WindowEyes we = views3d::ThroughWindow(w, at, headRoom - rr * 0.031f, headRoom + rr * 0.031f, 0.25f);
			const Vec midWorld = views3d::WindowWorldEye(hero, right, up, fwd, upm, we.mid);
			const Vec eyeL = midWorld - right * (we.half * upm);
			float V[16], P[16];
			d2rcam::m4::LookTo({eyeL.x, eyeL.y, eyeL.z}, {fwd.x, fwd.y, fwd.z}, {up.x, up.y, up.z}, V);
			EyeProj(we.tanL[0], we.tanR[0], we.tanD[0], we.tanU[0], 0.5f, P);
			int hits = 0;
			for (float u : {0.1f, 0.4f, 0.5f, 0.9f})
				for (float v : {0.1f, 0.35f, 0.5f, 0.8f, 0.95f}) {
					const Under h = GroundUnderPixel(m, groundY, V, P, u, v);
					if (!h.hit) continue;
					++hits;
					Vec o, d;
					CHECK(GameRay(V, P, u, v, &o, &d));
					CHECK(Near(h.world.y, groundY));
					CHECK(DistToRay(h.world, o, d) < 1e-2f);
					CHECK(NearV(ToWorld(m, h.at), h.world, 1e-3f));                     // the window's frame and the world are one
					float pu, pv;
					CHECK(Project(V, P, h.world, &pu, &pv) && Near(pu, u, 2e-3f) && Near(pv, v, 2e-3f));
					// what xr.cpp does with it: the point from the window's frame into the room, where the pointer is hung
					const Vec room = views3d::FromWindow(at, h.at);
					CHECK(NearV(views3d::ToWindow(at, room), h.at));
				}
			CHECK(hits > 10);                                                          // looking down at the ground through the window: most pixels are ground
		}
		// the middle of the window is the ground behind the hero's point on the window's plane: further than the plane
		const Vec h0{0.3f, 1.6f, -0.2f};
		const Vec rr0 = Rotate(YawQuat(at.yaw), {1, 0, 0});
		const views3d::WindowEyes home = views3d::ThroughWindow(w, at, h0 - rr0 * 0.031f, h0 + rr0 * 0.031f, 0.25f);
		float V[16], P[16];
		const Vec eyeL = views3d::WindowWorldEye(hero, right, up, fwd, upm, home.mid) - right * (home.half * upm);
		d2rcam::m4::LookTo({eyeL.x, eyeL.y, eyeL.z}, {fwd.x, fwd.y, fwd.z}, {up.x, up.y, up.z}, V);
		EyeProj(home.tanL[0], home.tanR[0], home.tanD[0], home.tanU[0], 0.5f, P);
		const Under mid = GroundUnderPixel(m, groundY, V, P, 0.5f, 0.5f);
		CHECK(mid.hit && mid.at.z < 0.0f);                                              // behind the window's plane, where the hero's ground is
	}

	std::printf(g_failed ? "%d of %d checks FAILED\n" : "all %d checks passed\n", g_failed ? g_failed : g_checks, g_checks);
	return g_failed ? 1 : 0;
}
