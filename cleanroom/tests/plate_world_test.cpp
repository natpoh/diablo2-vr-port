// The monster's health bar in the world (vr/plate_world.h): the maths and the shared state, on synthetic monsters and views.
//
//   plate_world_test        exit code 0 = every case passed

#include "../../vr/plate_world.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                               \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool Near3(const float a[3], const float b[3], float eps = 1e-3f) { return Near(a[0], b[0], eps) && Near(a[1], b[1], eps) && Near(a[2], b[2], eps); }

// A pinhole camera for the lift's tests (the game's PickRay stands in): eye in metres, a pitch, a vertical field of view, a square viewport; upm game units a metre.
struct Cam {
	float eye[3] = {0.0f, 1.7f, 0.0f};
	float fwd[3] = {0.0f, 0.0f, 1.0f}, right[3] = {1.0f, 0.0f, 0.0f}, up[3] = {0.0f, 1.0f, 0.0f};
	float f = 1.0f, size = 1536.0f, upm = 3.0f;
	void Set(float pitchDeg, float fovDeg, float eyeY) {   // pitch + looks up
		const float p = pitchDeg * 3.14159265f / 180.0f;
		eye[1] = eyeY;
		fwd[0] = 0.0f; fwd[1] = std::sin(p); fwd[2] = std::cos(p);
		up[0] = 0.0f; up[1] = std::cos(p); up[2] = -std::sin(p);
		f = 0.5f * size / std::tan(0.5f * fovDeg * 3.14159265f / 180.0f);
	}
	bool Project(const float P[3], float* sx, float* sy) const {   // a point in metres -> pixels
		const float v[3] = {P[0] - eye[0], P[1] - eye[1], P[2] - eye[2]};
		const float z = v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2];
		if (z < 0.05f) return false;
		*sx = 0.5f * size + f * (v[0] * right[0] + v[1] * right[1] + v[2] * right[2]) / z;
		*sy = 0.5f * size - f * (v[0] * up[0] + v[1] * up[1] + v[2] * up[2]) / z;
		return true;
	}
} g_cam;
bool CamRay(float sx, float sy, float o[3], float d[3]) {   // a pixel -> a ray in game units
	for (int i = 0; i < 3; ++i) {
		o[i] = g_cam.eye[i] * g_cam.upm;
		d[i] = g_cam.fwd[i] + (sx - 0.5f * g_cam.size) / g_cam.f * g_cam.right[i] - (sy - 0.5f * g_cam.size) / g_cam.f * g_cam.up[i];
	}
	return true;
}
bool NoRay(float, float, float[3], float[3]) { return false; }

// A made-up view: the head `yaw` radians round, eyes at (ex, ey, ez) in game units, upm units a metre.
bladeshape::HeadFrame View(float yaw, float ex, float ey, float ez, float upm) {
	bladeshape::HeadFrame h;
	h.ok = true;
	h.right[0] = std::cos(yaw); h.right[1] = 0.0f; h.right[2] = -std::sin(yaw);
	h.fwd[0] = std::sin(yaw); h.fwd[1] = 0.0f; h.fwd[2] = std::cos(yaw);
	h.eyeWorld[0] = ex; h.eyeWorld[1] = ey; h.eyeWorld[2] = ez;
	h.upm = upm;
	return h;
}

// Fake game memory for FootOf: a unit at 0x100000 whose path (at +0x38) is at 0x200000.
struct Memory {
	std::array<unsigned char, 0x100> unit{}, path{};
	uintptr_t unitAt = 0x100000, pathAt = 0x200000;
	uintptr_t fail = 0;
	template <class T> void Put(std::array<unsigned char, 0x100>& m, size_t at, T v) { std::memcpy(m.data() + at, &v, sizeof v); }
	bool Read(uintptr_t address, void* out, size_t n) const {
		if (address == fail) return false;
		if (address >= unitAt && address + n <= unitAt + unit.size()) { std::memcpy(out, unit.data() + (address - unitAt), n); return true; }
		if (address >= pathAt && address + n <= pathAt + path.size()) { std::memcpy(out, path.data() + (address - pathAt), n); return true; }
		return false;
	}
};
}  // namespace

int main() {
	using namespace plateworld;

	// ---- the fixed point ----
	{
		float p[2];
		CHECK(FixedPoint(2048.0f, 2048.0f, p) && Near(p[0], 1024.0f) && Near(p[1], 614.4f));
		CHECK(!FixedPoint(0.0f, 1000.0f, p) && !FixedPoint(1000.0f, 50.0f, p) && !FixedPoint(std::nanf(""), 1000.0f, p) && !FixedPoint(1e9f, 1e9f, p));
	}

	// ---- FootOf: the path's 16.16 coordinates, twice the cell ----
	{
		Memory m;
		m.Put(m.unit, 0x38, uint64_t(m.pathAt));
		m.Put(m.path, 0, uint16_t(32768));   // x fraction .5
		m.Put(m.path, 2, uint16_t(100));     // x
		m.Put(m.path, 4, uint16_t(16384));   // z fraction .25
		m.Put(m.path, 6, uint16_t(60));      // z
		auto read = [&](uintptr_t at, auto& v) { return m.Read(at, &v, sizeof v); };
		float x = 0, z = 0;
		CHECK(FootOf(m.unitAt, read, &x, &z) && Near(x, 201.0f) && Near(z, 120.5f));
		m.fail = m.unitAt + 0x38;
		CHECK(!FootOf(m.unitAt, read, &x, &z));          // the path pointer unreadable
		m.fail = m.pathAt + 4;
		CHECK(!FootOf(m.unitAt, read, &x, &z));          // one coordinate unreadable
		m.fail = 0;
		CHECK(!FootOf(0, read, &x, &z) && !FootOf(0x8000, read, &x, &z));   // not a unit
		m.Put(m.unit, 0x38, uint64_t(0x500));
		CHECK(!FootOf(m.unitAt, read, &x, &z));          // a path that is not a pointer
	}

	// ---- HeadOnAxis: the ray through the head meets the monster's axis there ----
	{
		const float k = meleetouch::kGroundShear;
		for (float floorM : {0.0f, 0.7f, -0.4f}) {
			for (float heightM : {0.5f, 1.8f, 3.2f}) {
				const float fx = 14.0f, fz = 9.0f;   // metres
				const float head[3] = {fx + (floorM + heightM) * k, floorM + heightM, fz + (floorM + heightM) * k};
				const float eye[3] = {2.0f, floorM + 1.7f, 1.0f};
				float d[3] = {head[0] - eye[0], head[1] - eye[1], head[2] - eye[2]};
				const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				for (float& v : d) v /= l;
				float h = 0, got[3];
				CHECK(HeadOnAxis(fx, fz, floorM, eye, d, &h, got) && Near(h, heightM, 1e-2f) && Near3(got, head, 1e-2f));
				// a ray that is not unit length says the same
				float d2[3] = {d[0] * 5.0f, d[1] * 5.0f, d[2] * 5.0f};
				CHECK(HeadOnAxis(fx, fz, floorM, eye, d2, &h, got) && Near(h, heightM, 1e-2f));
			}
		}
		// along the axis: no answer; pointing away from it: none; nothing: none
		const float eye[3] = {0.0f, 1.7f, 0.0f};
		float h = 0, got[3];
		const float up[3] = {k, 1.0f, k};
		CHECK(!HeadOnAxis(5.0f, 5.0f, 0.0f, eye, up, &h, got));
		const float away[3] = {-1.0f, 0.0f, -1.0f};
		CHECK(!HeadOnAxis(5.0f, 5.0f, 0.0f, eye, away, &h, got));
		const float zero[3] = {0.0f, 0.0f, 0.0f};
		CHECK(!HeadOnAxis(5.0f, 5.0f, 0.0f, eye, zero, &h, got));
		const float nan[3] = {std::nanf(""), 0.0f, 1.0f};
		CHECK(!HeadOnAxis(5.0f, 5.0f, 0.0f, eye, nan, &h, got));
	}

	// ---- RenderToHead is the inverse of PointToRender ----
	{
		for (float yaw : {0.0f, 0.7f, -2.1f, 3.0f}) {
			const bladeshape::HeadFrame v = View(yaw, 123.0f, 8.0f, -45.0f, 4.0f);
			for (const std::array<float, 3> in : {std::array<float, 3>{0.0f, 0.0f, 0.0f}, {1.5f, 0.3f, -2.0f}, {-0.4f, -1.6f, 3.5f}, {10.0f, 2.0f, 20.0f}}) {
				const stroke::Vec w = bladeshape::PointToRender(v, in.data());
				const float p[3] = {w.x, w.y, w.z};
				float back[3];
				CHECK(RenderToHead(v, p, back) && Near3(back, in.data(), 1e-3f));
			}
		}
		bladeshape::HeadFrame flat = View(0.0f, 0, 0, 0, 4.0f);
		flat.right[0] = 1.0f; flat.right[2] = 0.0f; flat.fwd[0] = 1.0f; flat.fwd[2] = 0.0f;   // right and forward the same line: not a frame
		const float p[3] = {1, 1, 1};
		float out[3];
		CHECK(!RenderToHead(flat, p, out));
	}

	// ---- MakeAnchor: a monster in front, the head in the head's frame ----
	{
		const float upm = 4.0f;
		const bladeshape::HeadFrame view = View(0.4f, 80.0f, 7.0f, 60.0f, upm);   // the hero's eyes at (20, 1.75, 15) m
		// a monster 6 m ahead of the hero's eyes (along the view's forward), 1.9 m tall, on a floor 0.2 m up
		const float floorM = 0.2f;
		const float k = meleetouch::kGroundShear;
		const float eyeM[3] = {view.eyeWorld[0] / upm, view.eyeWorld[1] / upm, view.eyeWorld[2] / upm};
		const float fx = eyeM[0] + 6.0f * view.fwd[0], fz = eyeM[2] + 6.0f * view.fwd[2];
		const float headM[3] = {fx + (floorM + 1.9f) * k, floorM + 1.9f, fz + (floorM + 1.9f) * k};
		Capture c;
		c.valid = true;
		c.foot[0] = fx * upm; c.foot[1] = fz * upm;
		float d[3] = {headM[0] - eyeM[0], headM[1] - eyeM[1], headM[2] - eyeM[2]};
		const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		for (int i = 0; i < 3; ++i) { c.rayO[i] = eyeM[i] * upm; c.rayD[i] = d[i] / l; }
		c.atMs = 1000;
		Anchor a;
		CHECK(MakeAnchor(c, view, floorM, 20.0f, 100.0f, 1000, &a) && a.ok);
		const stroke::Vec back = bladeshape::PointToRender(view, a.head);
		CHECK(Near(back.x, headM[0], 2e-2f) && Near(back.y, headM[1], 2e-2f) && Near(back.z, headM[2], 2e-2f));
		CHECK(Near(a.distM, std::sqrt(a.head[0] * a.head[0] + a.head[1] * a.head[1] + a.head[2] * a.head[2]), 1e-3f) && Near(a.distM, l, 2e-2f));   // (l: the eyes to the head, the ground shear included)
		CHECK(Near(a.gapM, 0.20f) && Near(a.sizePct, 100.0f) && a.atMs == 1000);
		// ahead is -z in the head's frame (z runs back), and the monster is in front
		CHECK(a.head[2] < -5.0f);

		// a ray along the axis cannot place the head: the fallback height is used, still an anchor
		Capture up = c;
		up.rayD[0] = k; up.rayD[1] = 1.0f; up.rayD[2] = k;
		Anchor b;
		CHECK(MakeAnchor(up, view, floorM, 0.0f, 100.0f, 1000, &b) && b.ok);
		const stroke::Vec fb = bladeshape::PointToRender(view, b.head);
		CHECK(Near(fb.y, floorM + kFallbackHeadM, 2e-2f));

		// refusals: no record, no view, a monster at the eyes, one far away, a floor that is not a number
		Capture none;
		CHECK(!MakeAnchor(none, view, floorM, 0.0f, 100.0f, 1000, &b));
		bladeshape::HeadFrame noView = view;
		noView.ok = false;
		CHECK(!MakeAnchor(c, noView, floorM, 0.0f, 100.0f, 1000, &b));
		CHECK(!MakeAnchor(c, view, std::nanf(""), 0.0f, 100.0f, 1000, &b));
		Capture far = c;
		far.foot[0] += 400.0f * upm; far.foot[1] += 400.0f * upm;
		float dfar[3] = {far.foot[0] / upm + 1.9f * k - eyeM[0], 1.9f - eyeM[1], far.foot[1] / upm + 1.9f * k - eyeM[2]};
		const float lf = std::sqrt(dfar[0] * dfar[0] + dfar[1] * dfar[1] + dfar[2] * dfar[2]);
		for (int i = 0; i < 3; ++i) far.rayD[i] = dfar[i] / lf;
		CHECK(!MakeAnchor(far, view, floorM, 0.0f, 100.0f, 1000, &b));
		bladeshape::HeadFrame wrongUpm = view;
		wrongUpm.upm = 0.0f;
		CHECK(!MakeAnchor(c, wrongUpm, floorM, 0.0f, 100.0f, 1000, &b));
	}

	// ---- the size ----
	{
		CHECK(Near(WidthM(2.0f, 100.0f), 0.40f) && Near(WidthM(5.0f, 100.0f), 1.0f) && Near(WidthM(30.0f, 100.0f), 1.60f));   // clamped near and far
		CHECK(Near(WidthM(5.0f, 200.0f), 2.0f) && Near(WidthM(5.0f, 30.0f), 0.30f));
		CHECK(Near(WidthM(5.0f, 5000.0f), 2.0f) && Near(WidthM(5.0f, -3.0f), 0.10f) && Near(WidthM(5.0f, std::nanf("")), 1.0f));
		CHECK(WidthM(std::nanf(""), 100.0f) > 0.0f);
		float prev = 0.0f;
		for (float dist = 0.5f; dist < 40.0f; dist += 0.5f) {
			const float w = WidthM(dist, 100.0f);
			CHECK(w >= prev);   // never smaller with distance
			prev = w;
		}
	}

	// ---- the shared state ----
	{
		Clear();
		Capture c;
		c.valid = true;
		c.atMs = 5000;
		SetCapture(c);
		Capture got;
		CHECK(GetCapture(&got, 5000) && GetCapture(&got, 5000 + kStaleMs));
		CHECK(!GetCapture(&got, 5000 + kStaleMs + 1));   // stale
		Anchor a;
		a.ok = true;
		a.atMs = 9000;
		SetAnchor(a);
		Anchor ag;
		CHECK(GetAnchor(&ag, 9100) && !GetAnchor(&ag, 9000 + kStaleMs + 1));
		SetActive(true);
		CHECK(Active());
		SetCapture(c);
		SetActive(false);   // the view is over: what was recorded goes
		CHECK(!Active() && !GetCapture(&got, 5000) && !GetAnchor(&ag, 9100));
		Configure(true, 15.0f, 100.0f);
		CHECK(Wanted() && Near(S().gapCm.load(), 15.0f));
		Configure(false, 1e9f, -5.0f);
		CHECK(!Wanted() && Near(S().gapCm.load(), 150.0f) && Near(S().sizePct.load(), 30.0f));
		Configure(true, -1e9f, 1e9f);
		CHECK(Near(S().gapCm.load(), -50.0f) && Near(S().sizePct.load(), 200.0f));
		Configure(true, std::nanf(""), std::nanf(""));
		CHECK(Near(S().gapCm.load(), 15.0f) && Near(S().sizePct.load(), 100.0f));
		CHECK(RaySlot().load() == nullptr);
	}

	// ---- is the plate where the fixed point put it (a test of test-54, 2026-10-17) ----
	{
		// The log of that run: a 1536 x 1536 picture, interface scale 0.569 (= 1536 / 2160 x 0.80), the panel at 0,80. The game was given -30000,-30000 (the
		// ordinary_mob_bars answer) and its TargetAttached read -54075,-53075 off the panel: the anchor (panel + widget) was -54075,-52995.
		const float viewport[2] = {1536.0f, 1536.0f};
		const float far[2] = {-30000.0f, -30000.0f};
		float want[2];
		CHECK(ExpectedAnchor(far, viewport, 0.569f, want));
		CHECK(Near(want[0], -54075.0f, 3.0f) && Near(want[1], -52995.0f, 10.0f));   // (5 units are the helper's own offset)
		const int32_t culled[2] = {-54075, -52995};
		CHECK(AnchorAtFixed(culled, far, viewport, 0.569f));
		// the fixed point: the middle of the width, 30 % down - what the widget reads when the game has hung the plate there
		float fixed[2];
		CHECK(FixedPoint(1536.0f, 1536.0f, fixed) && Near(fixed[0], 768.0f) && Near(fixed[1], 460.8f));
		CHECK(ExpectedAnchor(fixed, viewport, 0.569f, want) && Near(want[0], 0.0f, 0.01f) && Near(want[1], 540.0f, 1.0f));
		const int32_t hung[2] = {0, 535};
		CHECK(AnchorAtFixed(hung, fixed, viewport, 0.569f));
		const int32_t hungHigh[2] = {20, 565}, hungLow[2] = {-20, 515};
		CHECK(AnchorAtFixed(hungHigh, fixed, viewport, 0.569f) && AnchorAtFixed(hungLow, fixed, viewport, 0.569f));
		// what the plate was cut from in that run: Health at 0,-86 1000x58 under a TargetAttached at 0,0 0x0 - the anchor is the panel's own place, 0,80:
		// the game's default before a target, and its mouse layout. Not a plate to cut.
		const int32_t panelOwn[2] = {0, 80};
		CHECK(!AnchorAtFixed(panelOwn, fixed, viewport, 0.569f));
		const int32_t moved[2] = {400, 535}, high[2] = {0, 470};
		CHECK(!AnchorAtFixed(moved, fixed, viewport, 0.569f) && !AnchorAtFixed(high, fixed, viewport, 0.569f));
		CHECK(!AnchorAtFixed(culled, fixed, viewport, 0.569f));   // taken off the screen (ordinary_mob_bars) is not hung at the fixed point
		// at the interface sizes a player uses the check still tells the default from the point
		for (float pct : {100.0f, 80.0f, 60.0f, 50.0f, 45.0f}) {
			const float sc = 1536.0f * pct / 100.0f / 2160.0f;
			float w2[2];
			CHECK(ExpectedAnchor(fixed, viewport, sc, w2));
			const int32_t at[2] = {static_cast<int32_t>(w2[0]), static_cast<int32_t>(w2[1])};
			CHECK(AnchorAtFixed(at, fixed, viewport, sc));
			CHECK(!AnchorAtFixed(panelOwn, fixed, viewport, sc));
		}
		// not a scale, not a viewport: nothing is anchored
		CHECK(!ExpectedAnchor(fixed, viewport, 0.0f, want) && !ExpectedAnchor(fixed, viewport, 50.0f, want));
		const float tiny[2] = {10.0f, 10.0f};
		CHECK(!ExpectedAnchor(fixed, tiny, 0.569f, want) && !AnchorAtFixed(hung, fixed, tiny, 0.569f));
		// a wide picture: the area stands in the middle, its top is half of what the percent leaves free
		const float wide[2] = {2560.0f, 1440.0f};
		float fw[2];
		FixedPoint(2560.0f, 1440.0f, fw);
		CHECK(ExpectedAnchor(fw, wide, 1440.0f / 2160.0f, want) && Near(want[0], 0.0f, 0.01f) && Near(want[1], 648.0f, 1.0f));
	}

	// ---- is it a plate at all ----
	{
		const int32_t unsized[4] = {0, -6, 1000, 58}, ordinary[4] = {-77, 458, 154, 58}, unique[4] = {-101, 458, 203, 58}, empty[4] = {0, 0, 0, 0}, tall[4] = {0, 0, 200, 900};
		CHECK(!SanePlate(unsized) && SanePlate(ordinary) && SanePlate(unique) && !SanePlate(empty) && !SanePlate(tall));
		const int32_t edge[4] = {0, 0, kMaxPlateW, kMaxPlateH}, over[4] = {0, 0, kMaxPlateW + 1, 58};
		CHECK(SanePlate(edge) && !SanePlate(over));
	}

	// ---- where a target's bar went: the words, and the one line ----
	{
		for (int i = 0; i < static_cast<int>(Why::kCount); ++i) {
			const char* t = WhyText(static_cast<Why>(i));
			CHECK(t && t[0] && std::strcmp(t, "?") != 0);
			for (int j = 0; j < i; ++j) CHECK(std::strcmp(t, WhyText(static_cast<Why>(j))) != 0);   // each says something of its own
		}
		CHECK(std::strcmp(WhyText(Why::kCount), "?") == 0);
		// what the tick makes of what the hook did: the hook's reason first, then the plate's widgets, the layout, the anchor
		for (Why h : {Why::Hidden, Why::NoViewport, Why::NoFoot, Why::NoRay, Why::NotShown}) CHECK(Resolve(h, true, true, 1, true) == h && Resolve(h, false, false, 0, false) == h);
		CHECK(Resolve(Why::Hung, false, false, 0, false) == Why::NoRect);
		CHECK(Resolve(Why::Hung, true, false, 0, true) == Why::MouseLayout);
		CHECK(Resolve(Why::Hung, true, false, 1, true) == Why::NotAnchored && Resolve(Why::Hung, true, false, -1, true) == Why::NotAnchored);
		CHECK(Resolve(Why::Hung, true, true, 1, false) == Why::NoAnchor);
		CHECK(Resolve(Why::Hung, true, true, 1, true) == Why::Hung && Resolve(Why::Hung, true, true, 0, true) == Why::Hung);   // (anchored beats a layout read)

		// Tracker: told once for a target, after it has stood a moment; again for the next target, for a change, and for the same monster after a gap
		Tracker t;
		const uintptr_t A = 0x1000, B = 0x2000;
		const int hungN = static_cast<int>(Why::Hung), noRect = static_cast<int>(Why::NoRect), notAnchored = static_cast<int>(Why::NotAnchored);
		CHECK(!t.Should(A, noRect, 1000));                       // the first frame: the widgets catching up
		CHECK(!t.Should(A, noRect, 1000 + Tracker::kSettleMs - 1));
		CHECK(!t.Should(A, hungN, 1300));                        // it became something else before it settled: that one is waited for
		CHECK(t.Should(A, hungN, 1300 + Tracker::kSettleMs));    // told
		CHECK(!t.Should(A, hungN, 1300 + Tracker::kSettleMs + 10) && !t.Should(A, hungN, 60000));   // never twice for a standing outcome
		CHECK(!t.Should(A, notAnchored, 61000) && t.Should(A, notAnchored, 61000 + Tracker::kSettleMs));   // the outcome changed and stayed: told
		CHECK(!t.Should(B, notAnchored, 62000) && t.Should(B, notAnchored, 62000 + Tracker::kSettleMs));   // another monster, the same outcome: told
		CHECK(!t.Should(B, notAnchored, 63000));
		t.Gone();                                                // the plate went (nothing targeted)
		CHECK(!t.Should(B, notAnchored, 64000) && t.Should(B, notAnchored, 64000 + Tracker::kSettleMs));   // the same monster again: a target of its own
		CHECK(t.Told() == 4);
		// a flapping outcome does not fill the log: each change is told only after it stood
		Tracker f;
		uint64_t now = 0;
		int told = 0;
		for (int i = 0; i < 1000; ++i) {
			now += 100;
			told += f.Should(A, i % 2, now) ? 1 : 0;   // changes every 100 ms, never stands for 300
		}
		CHECK(told == 0);
		// and a long game: a hard cap
		Tracker cap;
		now = 0;
		for (int i = 0; i < 1000; ++i) {
			now += 1000;
			cap.Should(0x10 + static_cast<uintptr_t>(i), hungN, now);
			now += Tracker::kSettleMs;
			cap.Should(0x10 + static_cast<uintptr_t>(i), hungN, now);
		}
		CHECK(cap.Told() == Tracker::kMaxTold);
	}

	// ---- the game in its mouse layout: when the pad's is asked for ----
	{
		uint64_t since = 0, last = 0;
		// not in force, no game, a panel open, the laser the mouse, the pad's layout, or not known: never, and the wait starts over
		CHECK(!ShouldAskPad(false, true, false, false, 0, 1000, &since, &last) && since == 0);
		CHECK(!ShouldAskPad(true, false, false, false, 0, 1000, &since, &last) && !ShouldAskPad(true, true, true, false, 0, 1000, &since, &last));
		CHECK(!ShouldAskPad(true, true, false, true, 0, 1000, &since, &last) && !ShouldAskPad(true, true, false, false, 1, 1000, &since, &last));
		CHECK(!ShouldAskPad(true, true, false, false, -1, 1000, &since, &last) && since == 0 && last == 0);
		// the mouse layout: asked for after half a second, then at most every 3 s while it stays
		CHECK(!ShouldAskPad(true, true, false, false, 0, 2000, &since, &last) && since == 2000);
		CHECK(!ShouldAskPad(true, true, false, false, 0, 2000 + kMouseMs - 1, &since, &last));
		CHECK(ShouldAskPad(true, true, false, false, 0, 2000 + kMouseMs, &since, &last) && last == 2000 + kMouseMs);
		CHECK(!ShouldAskPad(true, true, false, false, 0, 2000 + kMouseMs + 100, &since, &last));
		CHECK(!ShouldAskPad(true, true, false, false, 0, 2000 + kMouseMs + kAskEveryMs - 1, &since, &last));
		CHECK(ShouldAskPad(true, true, false, false, 0, 2000 + kMouseMs + kAskEveryMs, &since, &last));
		// the pad's layout came (or the laser took the mouse): the half second starts over the next time
		CHECK(!ShouldAskPad(true, true, false, false, 1, 9000, &since, &last) && since == 0);
		CHECK(!ShouldAskPad(true, true, false, false, 0, 9100, &since, &last) && since == 9100);
		CHECK(ShouldAskPad(true, true, false, false, 0, 9100 + kMouseMs, &since, &last));   // (the last ask was long ago)
	}

	// ---- what the hook saw ----
	{
		Clear();
		Seen sn;
		CHECK(!GetSeen(&sn, 1000));
		const float px[2] = {111.0f, 222.0f};
		NoteSeen(0x1234, 19, Why::NoRay, px, 5000);
		CHECK(GetSeen(&sn, 5100) && sn.unit == 0x1234 && sn.cls == 19 && sn.why == Why::NoRay && Near(sn.px[0], 111.0f) && Near(sn.px[1], 222.0f));
		CHECK(!GetSeen(&sn, 5000 + kStaleMs + 1));
		NoteSeen(0x5678, 5, Why::Hidden, nullptr, 6000);
		CHECK(GetSeen(&sn, 6000) && sn.unit == 0x5678 && sn.why == Why::Hidden && sn.px[0] == 0.0f);
		SetActive(true);
		SetActive(false);   // the view is over: what was seen goes
		CHECK(!GetSeen(&sn, 6000));
	}

	// ---- the bar's swapchain is one size, whatever the cut-out is (a test of test-56: it was made again for 128x64, 256x64, 128x64... and the game was gone at the seventh) ----
	{
		Fit f;
		CHECK(!FitInto(0, 64, &f) && !FitInto(64, 0, &f) && !FitInto(0xFFFFFFFFu, 64, &f));
		// the sizes of that log, and every step of hud.cpp's cut (128 x 32 steps) up to the whole of a big picture
		const uint32_t sizes[][2] = {{128, 64}, {256, 64}, {384, 64}, {512, 96}, {640, 64}, {1024, 128}, {2048, 256}, {2176, 160}, {4096, 128}, {640, 2048}, {5000, 5000}};
		for (const auto& sz : sizes) {
			CHECK(FitInto(sz[0], sz[1], &f));
			CHECK(f.w >= 1 && f.w <= kChainW && f.h >= 1 && f.h <= kChainH);
			if (sz[0] <= kChainW && sz[1] <= kChainH) {
				CHECK(f.w == sz[0] && f.h == sz[1]);   // fits: drawn as it is, pixel for pixel
			} else {
				const double was = static_cast<double>(sz[0]) / sz[1], is = static_cast<double>(f.w) / f.h;
				CHECK(std::fabs(is - was) <= 0.05 * was + 1.0 / f.h);   // too big: the same shape, smaller
			}
		}
		// flipped back and forth many times: the corner always lies in the chain (whose size is a constant, not a function of the bar), and a bar of a size
		// is always given the same corner - nothing is carried over from the last
		uint32_t seed = 7u;
		auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
		bool bad = false;
		for (int i = 0; i < 100000; ++i) {
			const uint32_t w = 128u * (1u + rnd() % 20u), h = 32u * (1u + rnd() % 8u);
			Fit a, b;
			if (!FitInto(w, h, &a) || !FitInto(w, h, &b) || a.w != b.w || a.h != b.h) bad = true;
			if (a.w > kChainW || a.h > kChainH || !a.w || !a.h) bad = true;
			if (w <= kChainW && h <= kChainH && (a.w != w || a.h != h)) bad = true;
		}
		CHECK(!bad);
		CHECK(kChainW == 2048 && kChainH == 256);
	}

	// ---- [hud] monster_alpha reaches the bar in the room (a test of test-56: "the opacity slider does not work" with the bar ticked - it was drawn whole) ----
	{
		Configure(true, 15.0f, 100.0f);
		CHECK(Near(Opacity(), 1.0f) && OpacityVisible(Opacity()) && Near(ShaderOpacity(Opacity()), 1.0f));   // as the game draws it by default
		Configure(true, 60.0f, 80.0f, 0.10f);   // Serg's: opacity 0.10, size 80 %, gap 60 cm
		CHECK(Near(Opacity(), 0.10f) && OpacityVisible(Opacity()) && Near(ShaderOpacity(Opacity()), 0.10f));
		CHECK(Near(S().gapCm.load(), 60.0f) && Near(S().sizePct.load(), 80.0f));   // the size and the gap reach it too
		CHECK(Near(WidthM(5.0f, S().sizePct.load()), WidthM(5.0f, 100.0f) * 0.8f));   // ... the size as the bar's width
		Configure(true, 15.0f, 100.0f, 0.0f);   // 0 = gone: nothing is drawn, and the shader is never told "0" (which it reads as whole)
		CHECK(Near(Opacity(), 0.0f) && !OpacityVisible(Opacity()) && ShaderOpacity(Opacity()) >= kMinOpacity);
		Configure(true, 15.0f, 100.0f, kMinOpacity);
		CHECK(OpacityVisible(Opacity()) && !OpacityVisible(kMinOpacity * 0.99f));
		Configure(true, 15.0f, 100.0f, 7.0f);     // out of range or not a number: clamped, whole
		CHECK(Near(Opacity(), 1.0f));
		Configure(true, 15.0f, 100.0f, -3.0f);
		CHECK(Near(Opacity(), 0.0f));
		Configure(true, 15.0f, 100.0f, std::nanf(""));
		CHECK(Near(Opacity(), 1.0f) && Near(ShaderOpacity(std::nanf("")), 1.0f) && Near(ShaderOpacity(2.0f), 1.0f) && ShaderOpacity(-1.0f) >= kMinOpacity);
		// live: the next Configure (the ini reloaded) is the new value at once
		Configure(true, 15.0f, 100.0f, 0.5f);
		CHECK(Near(Opacity(), 0.5f));
		Configure(true, 15.0f, 100.0f, 0.75f);
		CHECK(Near(Opacity(), 0.75f));
		Configure(false, 15.0f, 100.0f);
	}

	// ---- the targeted monster's distance (monster_far, monster_depth_body) and the hook's note of it ----
	{
		const float foot[2] = {130.0f, 140.0f}, hero[3] = {100.0f, 9.0f, 100.0f};
		float units = 0.0f, metres = 0.0f;
		CHECK(GroundDistance(foot, hero, 3.0f, &units, &metres) && Near(units, 50.0f) && Near(metres, 50.0f / 3.0f));
		CHECK(!GroundDistance(foot, hero, 0.0f, &units, &metres) && !GroundDistance(foot, hero, 100.0f, &units, &metres));   // not a scale
		const float nanFoot[2] = {std::nanf(""), 0.0f};
		CHECK(!GroundDistance(nanFoot, hero, 3.0f, &units, &metres));
		// the tick's result is fresh for a while, none before one
		float m = 0.0f, u = 0.0f;
		SetTarget(-1.0f, 0.0f, 100);
		CHECK(!GetTarget(&m, &u, 100));
		SetTarget(12.5f, 37.5f, 1000);
		CHECK(GetTarget(&m, &u, 1000 + kStaleMs) && Near(m, 12.5f) && Near(u, 37.5f) && !GetTarget(&m, &u, 1000 + kStaleMs + 1));
		// the hook's note carries the monster's place; a note without one (not readable) says so
		Clear();
		Seen sn;
		const float at[2] = {140.0f, 60.0f};
		NoteSeen(0x10, 19, Why::Hung, nullptr, 2000, at);
		CHECK(GetSeen(&sn, 2000) && sn.footOk && Near(sn.foot[0], 140.0f) && Near(sn.foot[1], 60.0f));
		NoteSeen(0x10, 19, Why::NotShown, nullptr, 2100);
		CHECK(GetSeen(&sn, 2100) && !sn.footOk);
		// tracking is its own switch, apart from the bar in the room
		SetTrack(false);
		CHECK(!Tracking());
		SetTrack(true);
		CHECK(Tracking() && !Wanted());
		SetTrack(false);
		// a far monster's bar is smaller: the tick passes the size times the far rule, the width follows
		const float wNear = WidthM(12.0f, 100.0f), wFar = WidthM(12.0f, 100.0f * 0.4f);
		CHECK(Near(wFar, wNear * 0.4f));
		CHECK(Near(WidthM(12.0f, 80.0f * 0.2f), WidthM(12.0f, 100.0f) * 0.16f));   // the smallest the two sliders can ask for (80 % of the size, 20 % far) is honoured
	}

	// ---- [hud] monster_over_head: the game's own plate, its anchor given another y so its lower edge hangs gap cm over the head ----
	{
		const float k = meleetouch::kGroundShear;
		g_cam.Set(0.0f, 60.0f, 1.7f);
		// one monster: its feet at (fx, fz) metres, its head t metres up the upright axis (the render world's ground shear), floor 0
		struct Run { LiftWhy why; LiftOut out; float headY, topY; };
		auto run = [&](float fx, float fz, float t, float gapCm, float gameGap) {
			Run r{};
			const float P[3] = {fx + t * k, t, fz + t * k};
			const float Q[3] = {P[0], P[1] + gapCm * 0.01f, P[2]};   // straight over the head
			float sx = 0.0f, sxQ = 0.0f;
			g_cam.Project(P, &sx, &r.headY);
			g_cam.Project(Q, &sxQ, &r.topY);   // (the head's column is the head's own)
			LiftIn in;
			in.px[0] = sx; in.px[1] = r.headY;
			in.viewportH = g_cam.size;
			in.foot[0] = fx * g_cam.upm; in.foot[1] = fz * g_cam.upm; in.footOk = true;
			in.floorM = 0.0f; in.upm = g_cam.upm;
			in.gapCm = gapCm; in.gameGapPx = gameGap;
			in.ray = &CamRay;
			r.why = LiftAnchor(in, &r.out);
			return r;
		};
		// the lower edge at the head (gap 0): the anchor stands the game's own gap (19 px) under the head's point
		Run r = run(1.0f, 10.0f, 1.8f, 0.0f, 19.0f);
		CHECK(r.why == LiftWhy::Lifted && Near(r.out.anchorY, r.headY + 19.0f, 0.5f) && Near(r.out.movedPx, 19.0f, 0.5f));
		// 15 cm over a head 12 m away: where the point 15 cm higher is seen (within a pixel or so; the lift is worked out from rays)
		r = run(1.0f, 10.0f, 1.8f, 15.0f, 19.0f);
		CHECK(r.why == LiftWhy::Lifted && Near(r.out.anchorY, r.topY + 19.0f, 1.0f));
		CHECK(r.topY < r.headY && Near(r.out.distM, 12.3f, 0.5f));   // (higher up the screen; the head's distance)
		// the same gap in pixels falls with the distance: 50 cm over a head 4 m away against one 16 m away
		const Run nearer = run(0.5f, 3.0f, 1.8f, 50.0f, 0.0f), farther = run(0.5f, 15.0f, 1.8f, 50.0f, 0.0f);
		CHECK(nearer.why == LiftWhy::Lifted && farther.why == LiftWhy::Lifted);
		const float pxNear = -nearer.out.movedPx, pxFar = -farther.out.movedPx;
		CHECK(pxNear > 0.0f && pxFar > 0.0f && pxNear > 2.5f * pxFar && pxNear < 4.5f * pxFar);   // about the ratio of the depths (5.8 m against 17.8 m: a little over 3)
		CHECK(Near(nearer.out.anchorY, nearer.topY, 0.02f * (nearer.headY - nearer.topY) + 1.0f) && Near(farther.out.anchorY, farther.topY, 1.0f));
		// the plate lowered: a gap under 0 puts the lower edge into the head
		r = run(1.0f, 10.0f, 1.8f, -30.0f, 0.0f);
		CHECK(r.why == LiftWhy::Lifted && r.out.anchorY > r.headY && Near(r.out.anchorY, r.topY, 1.0f));
		// the game's own gap makes the anchor go lower by exactly that much, and a silly one is held
		const Run g0 = run(1.0f, 10.0f, 1.8f, 15.0f, 0.0f), g40 = run(1.0f, 10.0f, 1.8f, 15.0f, 40.0f);
		CHECK(Near(g40.out.anchorY - g0.out.anchorY, 40.0f, 0.01f));
		const Run huge = run(1.0f, 10.0f, 1.8f, 15.0f, 1e9f);
		CHECK(huge.why == LiftWhy::Lifted && huge.out.anchorY <= huge.headY + 0.3f * g_cam.size + 1.0f);
		// a head near the top of the screen (the probe ray is taken below it), and one near the bottom
		g_cam.Set(-29.0f, 60.0f, 1.7f);   // looking down: the monster stands at the very top of the screen (the probe ray is taken below it)
		r = run(-1.0f, 6.0f, 1.8f, 20.0f, 0.0f);
		CHECK(r.why == LiftWhy::Lifted && r.headY < kProbePx && Near(r.out.anchorY, r.topY, 2.0f));
		g_cam.Set(25.0f, 60.0f, 1.7f);    // looking up: the monster stands low
		r = run(-1.0f, 6.0f, 1.8f, 20.0f, 0.0f);
		CHECK(r.why == LiftWhy::Lifted && Near(r.out.anchorY, r.topY, 2.0f));
		g_cam.Set(0.0f, 60.0f, 1.7f);
		// the x belongs to the game: LiftAnchor only gives a y (there is no x in LiftOut)
		// what makes it keep the game's own place
		LiftIn bad;
		bad.px[0] = 700.0f; bad.px[1] = 700.0f; bad.viewportH = 1536.0f; bad.foot[0] = 3.0f; bad.foot[1] = 30.0f; bad.footOk = true;
		bad.floorM = 0.0f; bad.upm = 3.0f; bad.ray = &CamRay;
		LiftOut o;
		CHECK(LiftAnchor(bad, &o) == LiftWhy::Lifted);
		LiftIn t = bad; t.viewportH = 0.0f;
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoViewport);
		t = bad; t.upm = 0.0f;
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoWorld);
		t = bad; t.floorM = std::nanf("");
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoWorld);
		t = bad; t.footOk = false;
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoFoot);
		t = bad; t.ray = nullptr;
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoRay);
		t = bad; t.ray = &NoRay;
		CHECK(LiftAnchor(t, &o) == LiftWhy::NoRay);
		t = bad; t.foot[0] = -2.2045f * 3.0f; t.foot[1] = -2.2045f * 3.0f; t.px[0] = 768.0f; t.px[1] = 768.0f;   // the head within 20 cm of the eye
		CHECK(LiftAnchor(t, &o) != LiftWhy::Lifted);
		for (int i = 0; i < static_cast<int>(LiftWhy::kCount); ++i) {
			const char* text = LiftWhyText(static_cast<LiftWhy>(i));
			CHECK(text && text[0] && std::strcmp(text, "?") != 0);
			for (int j = 0; j < i; ++j) CHECK(std::strcmp(text, LiftWhyText(static_cast<LiftWhy>(j))) != 0);
		}
		// the switch, the world and the game's own gap
		SetLift(false);
		CHECK(!LiftWanted());
		SetLift(true);
		CHECK(LiftWanted() && !Wanted());   // apart from the plate cut out and hung in the room
		SetLift(false);
		SetWorld(0.25f, 3.0f);
		CHECK(Near(S().floorM.load(), 0.25f) && Near(S().upm.load(), 3.0f));
		SetGameGap(22.0f);
		CHECK(Near(S().gameGapPx.load(), 22.0f));
		SetGameGap(-5.0f);
		SetGameGap(std::nanf(""));
		SetGameGap(1e6f);
		CHECK(Near(S().gameGapPx.load(), 22.0f));   // a reading that is not a gap changes nothing
	}

	// ---- the plate's box: its margins, and it follows the plate (Serg, 2026-10-24: "old frames stay round the plates, above and sometimes at the side") ----
	{
		// the rect of his log (a pad's plate: -279,623 203x58 at scale 0.758 on 2048x2048): half the height round it, above and below too
		const int32_t r[4] = {-279, 623, 203, 58};
		const float s = 0.758f;
		float b[4];
		CHECK(BoxOf(r, s, 2048, 2048, b));
		const float top = 0.5f * (2048.0f - 2160.0f * s), m = 0.5f * 58.0f * s;
		CHECK(Near(b[0], 1024.0f - 279.0f * s - m, 0.01f) && Near(b[2], 1024.0f + (203.0f - 279.0f) * s + m, 0.01f));
		CHECK(Near(b[1], top + 623.0f * s - m, 0.01f) && Near(b[3], top + (623.0f + 58.0f) * s + m, 0.01f));
		CHECK(Near(b[1], top + 623.0f * s - 0.5f * 58.0f * s, 0.01f));   // the frame's top line, drawn above the rect, inside (15 % left it out)
		CHECK(!BoxOf(r, s, 0, 2048, b));
		const int32_t flat[4] = {0, 0, 203, 0};
		CHECK(!BoxOf(flat, s, 2048, 2048, b));

		// the hook's answers, newest first, three kept; another unit starts again
		Follow f;
		const float p1[2] = {1000.0f, 500.0f}, p2[2] = {1010.0f, 480.0f}, p3[2] = {1020.0f, 460.0f}, p4[2] = {1030.0f, 440.0f};
		FollowAdd(&f, 0x111, p1, 2048.0f, 2048.0f, 100);
		CHECK(f.n == 1 && f.unit == 0x111 && Near(f.px[0][0], 1000.0f) && f.atMs == 100);
		FollowAdd(&f, 0x111, p2, 2048.0f, 2048.0f, 111);
		FollowAdd(&f, 0x111, p3, 2048.0f, 2048.0f, 122);
		FollowAdd(&f, 0x111, p4, 2048.0f, 2048.0f, 133);
		CHECK(f.n == kFollowN && Near(f.px[0][1], 440.0f) && Near(f.px[1][1], 460.0f) && Near(f.px[2][1], 480.0f));
		FollowAdd(&f, 0x222, p1, 2048.0f, 2048.0f, 144);
		CHECK(f.n == 1 && f.unit == 0x222 && Near(f.px[0][0], 1000.0f));

		const float read[4] = {0.40f, 0.30f, 0.50f, 0.34f};   // the box as read, the plate's anchor then at 1000,500 px
		const float at[2] = {1000.0f, 500.0f};
		float out[4];
		// no answer, or none at the read: the box as read
		Follow none;
		FollowBox(read, 0x111, at, true, none, 0.0f, out);
		CHECK(Near(out[0], 0.40f) && Near(out[1], 0.30f) && Near(out[2], 0.50f) && Near(out[3], 0.34f));
		Follow g;
		FollowAdd(&g, 0x111, p1, 2048.0f, 2048.0f, 100);
		FollowAdd(&g, 0x111, p4, 2048.0f, 2048.0f, 111);   // the head turned: the plate 30 px right, 60 up since the read
		FollowBox(read, 0x111, at, false, g, 0.0f, out);
		CHECK(Near(out[0], 0.40f) && Near(out[3], 0.34f));
		// one answer, the read's own: unmoved
		Follow one;
		FollowAdd(&one, 0x111, p1, 2048.0f, 2048.0f, 100);
		FollowBox(read, 0x111, at, true, one, 0.0f, out);
		CHECK(Near(out[0], 0.40f) && Near(out[1], 0.30f) && Near(out[2], 0.50f) && Near(out[3], 0.34f));
		// moved: the union of the box at the read's place and at the newest (the frame on the screen is one of them)
		FollowBox(read, 0x111, at, true, g, 0.0f, out);
		const float dx = 30.0f / 2048.0f, dy = -60.0f / 2048.0f;
		CHECK(Near(out[0], 0.40f) && Near(out[2], 0.50f + dx) && Near(out[1], 0.30f + dy) && Near(out[3], 0.34f));
		// the plate moved on and only the newest answers are kept: the box goes with it, nothing of the read's place
		Follow h;
		FollowAdd(&h, 0x111, p2, 2048.0f, 2048.0f, 100);
		FollowAdd(&h, 0x111, p3, 2048.0f, 2048.0f, 111);
		FollowAdd(&h, 0x111, p4, 2048.0f, 2048.0f, 122);
		FollowBox(read, 0x111, at, true, h, 0.0f, out);
		CHECK(Near(out[0], 0.40f + 10.0f / 2048.0f) && Near(out[2], 0.50f + dx) && Near(out[1], 0.30f + dy) && Near(out[3], 0.34f - 20.0f / 2048.0f));
		// every place the plate was drawn in those frames is inside the box: no edge of it left out
		for (int i = 0; i < h.n; ++i) {
			const float ex = (h.px[i][0] - at[0]) / 2048.0f, ey = (h.px[i][1] - at[1]) / 2048.0f;
			CHECK(out[0] <= read[0] + ex + 1e-6f && out[2] >= read[2] + ex - 1e-6f && out[1] <= read[1] + ey + 1e-6f && out[3] >= read[3] + ey - 1e-6f);
		}
		// a new target, its name not read yet: as wide as the widest plate read, about the same middle, at the new plate's place
		Follow n;
		FollowAdd(&n, 0x222, p2, 2048.0f, 2048.0f, 100);
		FollowBox(read, 0x111, at, true, n, 0.30f, out);
		CHECK(Near(out[2] - out[0], 0.30f) && Near(0.5f * (out[0] + out[2]), 0.45f + 10.0f / 2048.0f) && Near(out[1], 0.30f - 20.0f / 2048.0f));
		FollowBox(read, 0x111, at, true, n, 0.05f, out);   // the read one is wider: kept
		CHECK(Near(out[2] - out[0], 0.10f));
		FollowBox(read, 0x222, at, true, n, 0.30f, out);   // its own read: its own width
		CHECK(Near(out[2] - out[0], 0.10f));
		// a viewport that is not one, or an empty box: as read
		Follow bad;
		FollowAdd(&bad, 0x111, p4, 0.0f, 2048.0f, 100);
		FollowBox(read, 0x111, at, true, bad, 0.0f, out);
		CHECK(Near(out[0], 0.40f) && Near(out[1], 0.30f));
		const float empty[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		FollowBox(empty, 0x111, at, true, g, 0.0f, out);
		CHECK(out[0] == 0.0f && out[2] == 0.0f);

		// the shared note: fresh for kStaleMs, the switch apart
		SetFollow(true);
		CHECK(Following());
		SetFollow(false);
		CHECK(!Following());
		Follow got;
		NoteFollow(0x333, p1, 2048.0f, 2048.0f, 7000);
		NoteFollow(0x333, p2, 2048.0f, 2048.0f, 7011);
		CHECK(GetFollow(&got, 7011 + kStaleMs) && got.unit == 0x333 && got.n == 2 && Near(got.px[0][0], 1010.0f));
		CHECK(!GetFollow(&got, 7011 + kStaleMs + 1));
		SetActive(true);
		SetActive(false);   // the room bar's view over: the follow is not its record
		CHECK(GetFollow(&got, 7020));
	}

	std::printf("plate_world_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
