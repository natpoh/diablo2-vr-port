// vr/views3d.h: native OpenXR's F1 (a window of the room onto the game seen from above) and F3 (the game's diorama on the
// floor) in real 3D - the maths on made-up poses: no game, no OpenXR, no headset.
//
//   views3d_test       exit code 0 = all passed

#include "views3d.h"

#include <cstdio>

using namespace views3d;

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                                  \
	do {                                                                          \
		++g_checks;                                                               \
		if (!(c)) { ++g_failed; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
	} while (0)
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps * std::max(1.0f, std::max(std::fabs(a), std::fabs(b))); }

// a pinhole camera: eye, the turn of its axes (x right, y up, -z ahead, as a Quat), the frustum as tangents
struct Cam { Vec eye; Quat q; float tanL, tanR, tanD, tanU; };
static bool Project(const Cam& c, Vec world, float* nx, float* ny) {
	const Quat inv{-c.q.x, -c.q.y, -c.q.z, c.q.w};
	return Ndc(Rotate(inv, world - c.eye), c.tanL, c.tanR, c.tanD, c.tanU, nx, ny);
}
static Quat Mul(const Quat& a, const Quat& b) {   // b first, then a
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static Quat AxisAngle(Vec a, float ang) {
	const float s = std::sin(0.5f * ang), n = Length(a);
	return {a.x / n * s, a.y / n * s, a.z / n * s, std::cos(0.5f * ang)};
}

int main() {
	// -- the basic turns -------------------------------------------------------------------------------------------
	{
		const Vec f = Rotate(YawQuat(0.7f), {0, 0, -1});
		CHECK(Near(f.x, -std::sin(0.7f)) && Near(f.z, -std::cos(0.7f)));   // a positive yaw turns left: forward (-sin, 0, -cos)
		CHECK(Near(YawOf(YawQuat(1.1f)), 1.1f));
		CHECK(Near(YawOf(YawQuat(-2.9f)), -2.9f));
		CHECK(Near(WrapPi(7.0f), 7.0f - 2.0f * kPi));
		// RoomToWorldDir is the F4 native branch's toWorld and TableCamera's turn: the room's forward at yaw t is the game's
		// heading phi + t
		const float phi = 0.9f, t = 0.4f;
		const Vec w = RoomToWorldDir(phi, {-std::sin(t), 0, -std::cos(t)});
		CHECK(Near(w.x, -std::sin(phi + t)) && Near(w.z, -std::cos(phi + t)));
	}

	// -- F1: the window ----------------------------------------------------------------------------------------------
	{
		const Window w = MakeWindow(2.4f, 2.0f, 16.0f / 9.0f);
		CHECK(Near(w.height, 1.35f) && Near(w.dist, 2.0f));
		CHECK(Near(MakeWindow(2.4f, 2.0f, 0.0f).height, 1.35f));   // aspect not known yet: 16:9
		CHECK(Near(MakeWindow(2.4f, 2.0f, 1.0f).height, 2.4f));    // a square window

		const Placed at = Place(w, 0.6f, {0.3f, 1.6f, -0.2f});
		CHECK(Near(Length(at.centre - Vec{0.3f, 1.6f, -0.2f}), 2.0f));   // dist ahead of where it was put
		CHECK(Near(at.centre.y, 1.6f));                                   // level with the head
		const Vec ahead = Rotate(YawQuat(0.6f), {0, 0, -1});
		CHECK(Near(Dot(at.centre - Vec{0.3f, 1.6f, -0.2f}, ahead), 2.0f));   // along the gaze it was put from
		// the frame round trip
		const Vec room{1.2f, 0.7f, -2.5f};
		const Vec back = FromWindow(at, ToWindow(at, room));
		CHECK(Near(back.x, room.x) && Near(back.y, room.y) && Near(back.z, room.z));
		CHECK(Near(ToWindow(at, at.centre).x, 0.0f) && Near(ToWindow(at, at.centre).z, 0.0f));
		// a head where it was put sees the window at its distance, in front
		const Vec head = ToWindow(at, {0.3f, 1.6f, -0.2f});
		CHECK(Near(head.x, 0.0f) && Near(head.y, 0.0f) && Near(head.z, 2.0f));

		// The invariant of a window: whatever the eyes do, a point on the window plane lands where the window itself puts it
		// (zero parallax on the plane): ndc x = 2x / width, ndc y = 2y / height - in BOTH eyes.
		const Vec heads[] = {{0.3f, 1.6f, -0.2f}, {0.9f, 1.8f, 0.4f}, {-1.0f, 1.2f, 0.8f}, {0.3f, 1.6f, 0.9f}, {2.0f, 1.0f, -1.0f}};
		const float turns[] = {0.0f, 0.5f, -0.7f, 1.2f};
		for (const Vec& h : heads)
			for (float turn : turns) {
				const Quat headQ = YawQuat(0.6f + turn);   // a head turned: its eyes are apart along ITS right
				const Vec right = Rotate(headQ, {1, 0, 0});
				const Vec eL = h - right * 0.032f, eR = h + right * 0.032f;
				const WindowEyes we = ThroughWindow(w, at, eL, eR, 0.15f);
				for (int e = 0; e < 2; ++e) {
					const Cam c{we.room[e], YawQuat(at.yaw), we.tanL[e], we.tanR[e], we.tanD[e], we.tanU[e]};
					const Vec pts[] = {{0, 0, 0}, {1.0f, 0.5f, 0}, {-1.2f, -0.675f, 0}, {0.4f, -0.3f, 0}};
					for (const Vec& pw : pts) {
						float nx = 0, ny = 0;
						const Vec room2 = FromWindow(at, pw);
						CHECK(Project(c, room2, &nx, &ny));
						CHECK(Near(nx, 2.0f * pw.x / w.width, 2e-3f) && Near(ny, 2.0f * pw.y / w.height, 2e-3f));
					}
				}
				// the half gap is the eyes' gap along the window (a head turned off the window has a smaller one), never more than 32 mm
				CHECK(we.half <= 0.032f + 1e-5f && we.half > 0.0f);
				// the eyes are level with each other and at the middle's depth: what is drawn is what is submitted
				CHECK(Near(ToWindow(at, we.room[0]).y, we.mid.y) && Near(ToWindow(at, we.room[1]).z, we.mid.z));
			}
		// the middle's depth never goes through the window
		const WindowEyes close = ThroughWindow(w, at, at.centre + Vec{-0.03f, 0, -0.5f}, at.centre + Vec{0.03f, 0, -0.5f}, 0.15f);
		CHECK(Near(close.mid.z, 0.15f));
		CHECK(close.tanR[0] - close.tanL[0] > 0.0f && std::isfinite(close.tanR[0]));

		// Depth: behind the window the left eye's image of a point is left of the right eye's (uncrossed disparity), in front of the
		// window the other way (crossed) - the stereo is the right way round.
		{
			const Vec h = {0.3f, 1.6f, -0.2f};   // where it was put: straight in front of the window
			const WindowEyes we = ThroughWindow(w, at, h - Rotate(YawQuat(at.yaw), {1, 0, 0}) * 0.032f, h + Rotate(YawQuat(at.yaw), {1, 0, 0}) * 0.032f, 0.15f);
			float xl, xr, y;
			const Cam cl{we.room[0], YawQuat(at.yaw), we.tanL[0], we.tanR[0], we.tanD[0], we.tanU[0]};
			const Cam cr{we.room[1], YawQuat(at.yaw), we.tanL[1], we.tanR[1], we.tanD[1], we.tanU[1]};
			const Vec behind = FromWindow(at, {0.0f, 0.0f, -1.5f}), before = FromWindow(at, {0.0f, 0.0f, 0.8f});
			CHECK(Project(cl, behind, &xl, &y) && Project(cr, behind, &xr, &y) && xl < xr);
			CHECK(Project(cl, before, &xl, &y) && Project(cr, before, &xr, &y) && xl > xr);
		}

		// The overhead camera in the game for a window eye: the hero stands on the window's middle, 7.5 units tall = ~1.8 m would be
		// 4.2 units a metre, but here the camera's own scale: half = the game's half height of view at the hero.
		{
			const float half = 7.5f;
			const float dist = OverheadDistance(half, w);
			CHECK(Near(dist, half * 2.0f * w.dist / w.height));
			const float upm = dist / w.dist;
			// the window's own height is the game's view height at the hero: tan(up) * dist = half
			CHECK(Near((0.5f * w.height / w.dist) * dist, half));
			const Vec hero{100.0f, 3.0f, -40.0f};
			// the game's overhead axes: looking down and ahead (heading 0.8 rad, pitched 35 degrees down), tilted by the window
			const Quat camQ = Mul(YawQuat(0.8f), AxisAngle({1, 0, 0}, -0.61f));
			const Vec right = Rotate(camQ, {1, 0, 0}), up = Rotate(camQ, {0, 1, 0}), fwd = Rotate(camQ, {0, 0, -1});
			const Vec h = {0.3f, 1.6f, -0.2f};
			for (float lean : {0.0f, 0.25f, -0.4f}) {
				const Vec hd = h + Rotate(YawQuat(at.yaw), {lean, 0.1f * lean, 0.3f * lean});
				const Vec r = Rotate(YawQuat(at.yaw), {1, 0, 0});
				const WindowEyes we = ThroughWindow(w, at, hd - r * 0.031f, hd + r * 0.031f, 0.15f);
				const Vec mid = WindowWorldEye(hero, right, up, fwd, upm, we.mid);
				for (int e = 0; e < 2; ++e) {
					const Vec eye = mid + right * ((e == 0 ? -we.half : we.half) * upm);
					const Cam c{eye, camQ, we.tanL[e], we.tanR[e], we.tanD[e], we.tanU[e]};
					// a world point on the plane through the hero perpendicular to the game's forward, at window offset (x, y) metres,
					// lands where the window puts it
					const Vec pts[] = {{0, 0}, {1.0f, 0.4f}, {-0.9f, -0.5f}};
					for (const Vec& p : pts) {
						const Vec wp = hero + (right * p.x + up * p.y) * upm;
						float nx = 0, ny = 0;
						CHECK(Project(c, wp, &nx, &ny));
						CHECK(Near(nx, 2.0f * p.x / w.width, 2e-3f) && Near(ny, 2.0f * p.y / w.height, 2e-3f));
					}
				}
			}
			// with the head where it was put the camera stands `dist` back of the hero, as TopCamera does
			const WindowEyes home = ThroughWindow(w, at, h - Rotate(YawQuat(at.yaw), {1, 0, 0}) * 0.03f, h + Rotate(YawQuat(at.yaw), {1, 0, 0}) * 0.03f, 0.15f);
			const Vec cam = WindowWorldEye(hero, right, up, fwd, upm, home.mid);
			const Vec want = hero - fwd * dist;
			CHECK(Near(cam.x, want.x, 1e-3f) && Near(cam.y, want.y, 1e-3f) && Near(cam.z, want.z, 1e-3f));
		}
	}

	// -- the window's anchor -----------------------------------------------------------------------------------------
	{
		Anchor a;
		double t = 0.0;
		CHECK(!a.Anchored());
		CHECK(a.Step(0.5f, {0, 1.6f, 0}, t));   // the first frame puts it
		CHECK(a.Anchored() && Near(a.Yaw(), 0.5f));
		// the first 45 frames follow the gaze (the runtime's first poses may not be the head's)
		for (int i = 1; i < Anchor::kHoldFrames; ++i) { t += 11000.0; CHECK(!a.Step(0.5f + 0.01f * (float)i, {0, 1.6f, 0}, t)); }
		CHECK(Near(a.Yaw(), 0.5f + 0.01f * (float)(Anchor::kHoldFrames - 1)));
		// then it stands
		const float stood = a.Yaw();
		for (int i = 0; i < 20; ++i) { t += 11000.0; a.Step(stood + 0.3f, {1, 1.6f, 1}, t); }
		CHECK(Near(a.Yaw(), stood));
		CHECK(Near(a.Pos().x, 0.0f));
		// 70 degrees off for less than a second: it stays
		for (int i = 0; i < 80; ++i) { t += 11000.0; CHECK(!a.Step(stood + 1.22f, {1, 1.6f, 1}, t)); }   // ~0.9 s
		CHECK(Near(a.Yaw(), stood));
		// back to the gaze: the timer starts over
		t += 11000.0; a.Step(stood, {0, 1.6f, 0}, t);
		for (int i = 0; i < 80; ++i) { t += 11000.0; CHECK(!a.Step(stood + 1.22f, {1, 1.6f, 1}, t)); }
		CHECK(Near(a.Yaw(), stood));
		// off for more than a second: brought round in front, to where the head is
		bool brought = false;
		for (int i = 0; i < 40; ++i) { t += 11000.0; brought = a.Step(stood + 1.22f, {1, 1.6f, 1}, t) || brought; }
		CHECK(brought && Near(a.Yaw(), stood + 1.22f) && Near(a.Pos().x, 1.0f));
		// Reset: the window comes up in front of the gaze again
		a.Reset();
		CHECK(!a.Anchored());
		CHECK(a.Step(-1.0f, {2, 1.5f, 2}, t) && Near(a.Yaw(), -1.0f) && Near(a.Pos().x, 2.0f));
	}
	{   // 50 degrees off (under the 60) never moves a window that is standing
		Anchor a;
		a.Step(0.0f, {0, 1.6f, 0}, 0.0);
		double t = 0.0;
		for (int i = 0; i < Anchor::kHoldFrames + 5; ++i) { t += 11000.0; a.Step(0.0f, {0, 1.6f, 0}, t); }
		for (int i = 0; i < 600; ++i) { t += 11000.0; a.Step(0.87f, {0, 1.6f, 0}, t); }
		CHECK(Near(a.Yaw(), 0.0f));
	}

	// -- F3: the diorama in the room ---------------------------------------------------------------------------------
	{
		const Table t{0.4f, -1.1f, -1.65f, 0.9f, 37.0f, 0.0f, 0.05f};
		const Vec hero{250.0f, 4.0f, -80.0f};
		// the diorama's map: a room point r to the game's world
		auto toWorld = [&](Vec r) {
			const Vec d = RoomToWorldDir(t.phi, {r.x - t.fx, 0.0f, r.z - t.fz}) * t.scale;
			return Vec{hero.x + d.x, hero.y + (r.y - t.floorY - t.lift) * t.scale, hero.z + d.z};
		};
		CHECK(Near(Length(toWorld({t.fx, t.floorY, t.fz}) - hero), 0.0f, 1e-3f));   // the floor point under the hero is the hero
		// the head's eye is that map of the head, when the head is over the board
		const Vec p{0.9f, -0.1f, 0.3f};
		const Vec e = TableEye(t, hero, p), m = toWorld(p);
		CHECK(Near(e.x, m.x, 1e-3f) && Near(e.y, m.y, 1e-3f) && Near(e.z, m.z, 1e-3f));
		// the head cannot go through the board: under the floor it stays minAbove over it
		CHECK(Near(TableEye(t, hero, {p.x, t.floorY - 0.5f, p.z}).y, hero.y + 0.05f * t.scale));
		// the scale: a step of 10 cm in the room is `scale / 10` units
		CHECK(Near(Length(TableEye(t, hero, {p.x + 0.1f, p.y, p.z}) - e), 0.1f * t.scale, 1e-3f));

		// THE point of the view: a camera in the room and its twin in the game (turned by phi, moved and scaled by the map,
		// the same frustum) see the same picture - the diorama stands in the room whatever the head does.
		const Vec pts[] = {{0.4f, -1.65f, -1.1f}, {0.7f, -1.5f, -1.4f}, {0.1f, -1.6f, -0.8f}, {0.5f, -1.2f, -1.0f}};
		const float tans[4] = {-1.1f, 1.1f, -0.9f, 0.9f};
		const Quat heads[] = {YawQuat(0.0f), Mul(YawQuat(0.7f), AxisAngle({1, 0, 0}, -0.9f)), Mul(YawQuat(-1.4f), AxisAngle({1, 0, 0}, -1.2f)),
		                      Mul(Mul(YawQuat(2.5f), AxisAngle({1, 0, 0}, -0.7f)), AxisAngle({0, 0, 1}, 0.2f))};
		const Vec places[] = {{0.9f, -0.1f, 0.3f}, {0.4f, -0.2f, 0.6f}, {-0.3f, 0.0f, -2.5f}, {1.2f, -0.3f, -0.6f}};
		for (const Quat& q : heads)
			for (const Vec& pos : places) {
				const Cam room{pos, q, tans[0], tans[1], tans[2], tans[3]};
				// the twin: position by the map; axes turned by phi about the vertical
				const Vec rx = RoomToWorldDir(t.phi, Rotate(q, {1, 0, 0})), ry = RoomToWorldDir(t.phi, Rotate(q, {0, 1, 0}));
				const Vec rz = RoomToWorldDir(t.phi, Rotate(q, {0, 0, 1}));
				for (const Vec& pt : pts) {
					float rnx, rny;
					if (!Project(room, pt, &rnx, &rny)) continue;
					// the world camera by hand: the axes rx, ry, rz
					const Vec ev = TableEye(t, hero, pos);
					const Vec d = toWorld(pt) - ev;
					const Vec cam{Dot(d, rx), Dot(d, ry), Dot(d, rz)};
					float wnx, wny;
					CHECK(Ndc(cam, tans[0], tans[1], tans[2], tans[3], &wnx, &wny));
					CHECK(Near(wnx, rnx, 2e-3f) && Near(wny, rny, 2e-3f));
				}
			}
		// the eyes' gap in the game's units: the room's gap times the scale (the parallax follows)
		const float gap = 0.063f;
		CHECK(Near(gap * t.scale, 2.331f, 1e-3f));
	}

	std::printf(g_failed ? "%d of %d checks FAILED\n" : "all %d checks passed\n", g_failed ? g_failed : g_checks, g_checks);
	return g_failed ? 1 : 0;
}
