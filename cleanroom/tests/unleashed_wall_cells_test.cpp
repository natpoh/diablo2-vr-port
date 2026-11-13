// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// Tests for vr/unleashed_wall_cells.h (from D2R VR Unleashed commit b0f8eb1, "Weapons hit walls"; its held-world-test checked the same cells, a
// blade into a wall and along it across seams, an inner corner, the floor, a fast swing, a barrel's footprint) and for how [weight] wall_contact_mode 2 (Bear's)
// puts those cells into our weight model (vr/wall_collide.h, cleanroom/heft/walls.h with Wall::open). The rest of D2R VR Authentic is MIT; this
// file stays MPL 2.0.
//
//   unleashed_wall_cells_test          every check below
//   unleashed_wall_cells_test table    the tracks of the swings, for a look
//
// The wall map lookup: a cell's bits marked (a wall, a closed door, an object's footprint, no room, off the map), the cell under a render point
// with the ground's shear, the boxes of a patch (open only where the neighbour is no wall; inside a wall left out; objects and unloaded cells left
// out and not closing their neighbours' sides; the window; the heights), the floor box, when to sample again, the boxes into the hands' frame.
// The push-back maths (the weight model's hold at 90 Hz, a controller driven into the cells): a thrust stops on a wall's face; a blade pressed on a
// wall slides along it across the cells' seams; an inner corner holds it out of both walls; a thrust down stops on the floor; a closed door stops
// it, a barrel's footprint does not; a fast cut across a wall one cell thick never ends on the far side; the knock's strength; the cost.

#include "hold.h"
#include "walls.h"
#include "../../vr/unleashed_wall_cells.h"
#include "../../vr/wall_collide.h"
#include "../../shared/wall_contact_mode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hw = heft::walls;
namespace uw = unleashedwalls;

namespace {

constexpr double kDt = 1.0 / 90.0;
constexpr float kUpm = 4.0f;   // render units per metre in these scenes: a cell (2 units) is 0.5 m
int g_fails = 0;
bool g_table = false;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

// ---- the wall map lookup ------------------------------------------------------------------------------------------------------------------------

uw::Patch EmptyPatch() {
	uw::Patch p;
	p.session = 7;
	p.x0 = -16;
	p.z0 = -16;
	return p;
}
// A cell (absolute) of the patch marked with these bits.
void Set(uw::Patch& p, int x, int z, uint16_t bits, bool roomKnown = true) { p.Mark(x - p.x0, z - p.z0, false, roomKnown, bits); }

void TestMark() {
	uw::Patch p = EmptyPatch();
	Set(p, 0, 0, 0x4);              // a wall
	Set(p, 1, 0, 0x804);            // a closed door
	Set(p, 2, 0, 0x404);            // an object's footprint (a barrel)
	Set(p, 3, 0, 0x400);            // the footprint bit alone: no barrier
	Set(p, 4, 0, 0, false);         // no room loaded
	Set(p, 5, 0, 0x1);              // walkable, other bits
	p.Mark(6 - p.x0, 0 - p.z0, true, true, 0);   // off the map
	p.Mark(-1, 0, false, true, 0x4);             // outside the patch: ignored
	EXPECT(p.Blocked(0, 0) && p.Wall(0, 0), "a wall cell");
	EXPECT(p.Blocked(1, 0) && p.Wall(1, 0) && p.Bit(p.doors, 1, 0), "a closed door is a wall and a door");
	EXPECT(p.Blocked(2, 0) && !p.Wall(2, 0) && p.Bit(p.objects, 2, 0), "a barrel's footprint is blocked but no wall for the weapon");
	EXPECT(!p.Blocked(3, 0), "0x400 alone is no barrier");
	EXPECT(p.Blocked(4, 0) && !p.Wall(4, 0) && p.Bit(p.unknown, 4, 0), "no room: blocked, unknown, no wall for the weapon");
	EXPECT(!p.Blocked(5, 0), "walkable");
	EXPECT(p.Blocked(6, 0) && p.Bit(p.unknown, 6, 0), "off the map: unknown");
	EXPECT(!p.Covers(16, 0) && !p.Blocked(16, 0) && !p.Wall(-17, 0), "outside the patch: nothing");
	std::printf("mark: wall, door, object, unknown, off the map, outside - ok\n");
}

void TestCellAt() {
	const uw::Cell a = uw::CellAt(5.0f, -0.5f, 0.0f);
	EXPECT(a.x == 2 && a.z == -1, "flat ground: %d %d", a.x, a.z);
	// raised ground is drawn shifted by height * sqrt(3/2) on both axes: the same cell under it
	const float h = 10.0f, s = h * uw::kShear;
	const uw::Cell b = uw::CellAt(5.0f + s, -0.5f + s, h);
	EXPECT(b.x == 2 && b.z == -1, "raised ground, the same cell: %d %d", b.x, b.z);
	const uw::Cell c = uw::CellAt(5.0f, -0.5f, h);
	EXPECT(c.x == (int)std::floor((5.0f - s) * 0.5f), "raised ground without the shift is another cell: %d", c.x);
	std::printf("cell at: shear %.5f, raised ground shifted %.2f units - ok\n", uw::kShear, s);
}

int FindBox(const uw::Box* b, int n, int x, int z) {
	for (int i = 0; i < n; ++i)
		if (b[i].x == x && b[i].z == z) return i;
	return -1;
}

void TestBoxes() {
	uw::Box b[256];
	uw::Counts c;
	{   // a straight wall along x at z = -3, x -4..4
		uw::Patch p = EmptyPatch();
		for (int x = -4; x <= 4; ++x) Set(p, x, -3, 0x4);
		const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 2.5f, b, 256, &c);
		EXPECT(n == 9 && c.walls == 9 && c.inside == 0, "nine cells: %d (%u walls)", n, c.walls);
		const int mid = FindBox(b, n, 0, -3), end = FindBox(b, n, 4, -3);
		EXPECT(mid >= 0 && b[mid].open == (uw::FaceBit(2, 1) | uw::FaceBit(2, -1)), "a middle cell opens only its two long sides: %02x", mid >= 0 ? b[mid].open : 0);
		EXPECT(end >= 0 && (b[end].open & uw::FaceBit(0, 1)) && !(b[end].open & uw::FaceBit(0, -1)), "the end cell opens its end: %02x", end >= 0 ? b[end].open : 0);
		EXPECT(mid >= 0 && std::fabs(b[mid].lo[2] - (-1.5f)) < 1e-5f && std::fabs(b[mid].hi[2] - (-1.0f)) < 1e-5f && std::fabs(b[mid].lo[0]) < 1e-5f &&
		           std::fabs(b[mid].hi[0] - 0.5f) < 1e-5f,
		       "its place: x %.2f..%.2f z %.2f..%.2f", b[mid].lo[0], b[mid].hi[0], b[mid].lo[2], b[mid].hi[2]);
		EXPECT(mid >= 0 && std::fabs(b[mid].lo[1] + uw::kWallBelowM) < 1e-5f && std::fabs(b[mid].hi[1] - uw::kWallHeightM) < 1e-5f, "from under the floor to 6 m");
		EXPECT(mid >= 0 && b[mid].kind == uw::Kind::Wall, "a wall");
	}
	{   // a wall three cells thick: its middle row is inside, left out
		uw::Patch p = EmptyPatch();
		for (int z = -5; z <= -3; ++z)
			for (int x = -2; x <= 2; ++x) Set(p, x, z, 0x4);
		const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 2.5f, b, 256, &c);
		EXPECT(c.inside == 3 && FindBox(b, n, 0, -4) < 0 && n == 12, "the middle row inside: %u inside, %d boxes", c.inside, n);
	}
	{   // an inner corner: a wall along x at z = -3 and along z at x = -3; the corner cell opens only outward
		uw::Patch p = EmptyPatch();
		for (int i = -3; i <= 3; ++i) { Set(p, i, -3, 0x4); Set(p, -3, i, 0x4); }
		const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 2.5f, b, 256, &c);
		const int corner = FindBox(b, n, -3, -3), a = FindBox(b, n, -2, -3), d = FindBox(b, n, -3, -2);
		EXPECT(corner >= 0 && !(b[corner].open & uw::FaceBit(0, 1)) && !(b[corner].open & uw::FaceBit(2, 1)), "the corner closes toward both walls: %02x",
		       corner >= 0 ? b[corner].open : 0);
		EXPECT(a >= 0 && (b[a].open & uw::FaceBit(2, 1)) && d >= 0 && (b[d].open & uw::FaceBit(0, 1)), "each wall opens to the room");
	}
	{   // a door in a wall, a barrel next to a wall, an unloaded doorway
		uw::Patch p = EmptyPatch();
		for (int x = -4; x <= 4; ++x) Set(p, x, -3, x == 0 ? 0x804 : 0x4);
		Set(p, 2, -2, 0x404);         // a barrel against the wall
		Set(p, -2, -2, 0, false);     // no room loaded in front of the wall
		const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 2.5f, b, 256, &c);
		const int door = FindBox(b, n, 0, -3), behindBarrel = FindBox(b, n, 2, -3), behindUnknown = FindBox(b, n, -2, -3);
		EXPECT(door >= 0 && b[door].kind == uw::Kind::Door && c.doors == 1, "the closed door stays, a door");
		EXPECT(FindBox(b, n, 2, -2) < 0 && c.objects == 1, "the barrel's footprint is no box");
		EXPECT(behindBarrel >= 0 && (b[behindBarrel].open & uw::FaceBit(2, 1)), "the wall behind the barrel still opens toward it (the barrel is no wall)");
		EXPECT(FindBox(b, n, -2, -2) < 0 && c.unknown == 1 && behindUnknown >= 0 && (b[behindUnknown].open & uw::FaceBit(2, 1)), "the unloaded cell is no box and closes nothing");
	}
	{   // the window: at most 16 cells across; reach; the floor's height and shear move the boxes
		uw::Patch p = EmptyPatch();
		for (int z = -16; z < 16; ++z)
			for (int x = -16; x < 16; ++x)
				if (((x + z) & 1) == 0) Set(p, x, z, 0x4);   // a checkerboard: every wall cell open
		const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 20.0f, b, 256, &c);
		EXPECT(n == 128 && c.walls == 128, "16 x 16 cells at most: %d", n);
		const int capped = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 20.0f, b, 100, &c);
		EXPECT(capped == 100 && c.dropped == 28, "over the limit: %d written, %u dropped", capped, c.dropped);
		uw::Patch q = EmptyPatch();
		Set(q, 0, -3, 0x4);
		const float h = 8.0f, s = h * uw::kShear;   // ground 2 m up (8 units): the cell drawn shifted by s units
		const int m = uw::Walls(q, h, kUpm, s / kUpm, s / kUpm, 2.5f, b, 256, &c);
		EXPECT(m == 1 && std::fabs(b[0].lo[0] - s / kUpm) < 1e-4f && std::fabs(b[0].lo[1] - (2.0f - uw::kWallBelowM)) < 1e-4f, "raised: x %.3f (want %.3f), bottom %.3f",
		       b[0].lo[0], s / kUpm, b[0].lo[1]);
		const int none = uw::Walls(q, 0.0f, kUpm, 10.0f, 10.0f, 2.5f, b, 256, &c);
		EXPECT(none == 0 && c.uncovered > 0, "far from the cell: none (%d), %u outside the sample", none, c.uncovered);
		EXPECT(uw::Walls(q, 0.0f, 0.5f, 0, 0, 2.5f, b, 256) == 0 && uw::Walls(q, NAN, kUpm, 0, 0, 2.5f, b, 256) == 0, "bad units or floor: none");
	}
	uw::Box f;
	EXPECT(uw::Floor(4.0f, kUpm, 1.0f, 2.0f, &f) && f.hi[1] == 1.0f && f.open == uw::FaceBit(1, 1) && f.kind == uw::Kind::Floor && f.lo[0] < 1.0f - 3.9f,
	       "the floor's top at the ground (%.2f), open upward only", f.hi[1]);
	std::printf("boxes: a row, a thick wall, an inner corner, a door, a barrel, an unloaded doorway, the window, raised ground, the floor - ok\n");
}

void TestSampleDue() {
	uw::Patch last = EmptyPatch();   // centred on cell 0, 0
	EXPECT(uw::SampleDue(last, false, 0, 7, {0, 0}, 1000), "none yet: due");
	EXPECT(!uw::SampleDue(last, true, 1000, 7, {3, -2}, 1200), "near and fresh: not due");
	EXPECT(uw::SampleDue(last, true, 1000, 7, {3, -2}, 1600), "near but 600 ms old: due (doors open and close)");
	EXPECT(uw::SampleDue(last, true, 1000, 7, {9, 0}, 1100), "moved 9 cells: due");
	EXPECT(!uw::SampleDue(last, true, 1000, 7, {9, 0}, 1020), "but not within one game tick");
	EXPECT(uw::SampleDue(last, true, 1000, 8, {0, 0}, 1100), "another session: due");
	std::printf("sample due: fresh, aged, moved, one tick apart, another session - ok\n");
}

void TestToHead() {
	const float lo[3] = {0.0f, -0.5f, -1.5f}, hi[3] = {0.5f, 6.0f, -1.0f};
	monsterbodies::View v;   // the eyes at the origin, looking down -z: the hands' frame is the render one
	v.eye[0] = v.eye[1] = v.eye[2] = 0.0f;
	v.camR[0] = 1; v.camR[1] = 0; v.camR[2] = 0;
	v.camF[0] = 0; v.camF[1] = 0; v.camF[2] = -1;
	v.upm = kUpm;
	hw::Wall w;
	EXPECT(wallcollide::ToHead(v, lo, hi, 0x14, 99, &w), "identity view");
	EXPECT(std::fabs(w.c.x - 0.25) < 1e-5 && std::fabs(w.c.y - 2.75) < 1e-5 && std::fabs(w.c.z + 1.25) < 1e-5 && std::fabs(w.half.z - 0.25) < 1e-5 && w.open == 0x14 &&
	           w.id == 99 && w.body == 0,
	       "centre %.2f %.2f %.2f, open %02x", w.c.x, w.c.y, w.c.z, w.open);
	// the head turned 90 degrees (looking down -x) and standing 2 m along +x: the render +x axis is the hands' frame -z (ahead)
	v.eye[0] = 2.0f * kUpm;
	v.camR[0] = 0; v.camR[2] = -1;
	v.camF[0] = -1; v.camF[2] = 0;
	EXPECT(wallcollide::ToHead(v, lo, hi, 0x14, 99, &w), "turned view");
	float hc[3];
	const float c[3] = {0.25f, 2.75f, -1.25f};
	monsterbodies::RenderToHead(v, c, hc);
	const float px[3] = {1.25f, 2.75f, -1.25f};
	float hx[3];
	monsterbodies::RenderToHead(v, px, hx);
	EXPECT(std::fabs(w.c.x - hc[0]) < 1e-5 && std::fabs(w.c.z - hc[2]) < 1e-5, "the centre through the view");
	EXPECT(std::fabs(w.axis[0].x - (hx[0] - hc[0])) < 1e-5 && std::fabs(w.axis[0].z - (hx[2] - hc[2])) < 1e-5 && std::fabs(Len(w.axis[0]) - 1) < 1e-6,
	       "render +x as the view turns it: %.2f %.2f %.2f", w.axis[0].x, w.axis[0].y, w.axis[0].z);
	EXPECT(std::fabs(w.axis[1].y - 1) < 1e-6, "up stays up");
	EXPECT(wallcollide::CellId(3, -2) != wallcollide::CellId(-2, 3) && wallcollide::CellId(0, 0) != 0 && (wallcollide::CellId(5, 5) & wallcollide::kDoorIdBit) == 0 &&
	           wallcollide::CellId(1, 1) != wallcollide::kFloorId,
	       "cell ids distinct, nonzero, not a door's, not the floor's");
	std::printf("to head: identity and turned views, faces kept, ids - ok\n");
}

// ---- the push-back maths: the weight model's hold against the cells ------------------------------------------------------------------------

// The patch's boxes (and the floor) as the weight model's walls through the identity view (the room is the render world here, metres).
std::vector<hw::Wall> WallsOf(const uw::Patch& p, bool floor = false, bool allFaces = false) {
	monsterbodies::View v;
	v.eye[0] = v.eye[1] = v.eye[2] = 0.0f;
	v.camR[0] = 1; v.camR[1] = 0; v.camR[2] = 0;
	v.camF[0] = 0; v.camF[1] = 0; v.camF[2] = -1;
	v.upm = kUpm;
	static uw::Box b[hw::kMaxWalls];
	const int n = uw::Walls(p, 0.0f, kUpm, 0.0f, 0.0f, 2.5f, b, hw::kMaxWalls - 1);
	std::vector<hw::Wall> out;
	for (int i = 0; i < n; ++i) {
		hw::Wall w;
		const uint32_t id = wallcollide::CellId(b[i].x, b[i].z) | (b[i].kind == uw::Kind::Door ? wallcollide::kDoorIdBit : 0u);
		if (wallcollide::ToHead(v, b[i].lo, b[i].hi, allFaces ? 0x3F : b[i].open, id, &w)) out.push_back(w);
	}
	uw::Box f;
	hw::Wall w;
	if (floor && uw::Floor(0.0f, kUpm, 0.0f, 0.0f, &f) && wallcollide::ToHead(v, f.lo, f.hi, f.open, wallcollide::kFloorId, &w)) out.push_back(w);
	return out;
}

double Inside(const hw::Wall& w, V3 p) {
	const V3 l = hw::Local(w, p);
	return std::min({w.half.x - std::fabs(l.x), w.half.y - std::fabs(l.y), w.half.z - std::fabs(l.z)});
}

hh::HandIn HandAt(const Pose& p, bool tracked = true) {
	hh::HandIn h;
	h.tracked = tracked;
	h.pose = p;
	h.grip = tracked ? 1.0f : 0.0f;
	h.fistLine = {0, 0, -1};
	return h;
}

struct Run {
	double deepest = -1;   // the tip's deepest inside any wall (m)
	int contactSteps = 0;
	double maxApproach = 0;
	uint32_t metId = 0;
	int ignoredSteps = 0, returns = 0;
	V3 tipEnd;
	std::vector<V3> tips;
};

double Smooth(double u) {
	u = std::clamp(u, 0.0, 1.0);
	return u * u * (3 - 2 * u);
}

template <class F>
Run Swing(F path, double secs, const std::vector<hw::Wall>& walls, bool on, const char* name = nullptr) {
	Run r;
	hh::Settings s;
	s.walls = on;
	hh::Hold h;
	hh::HandIn in[2] = {HandAt(path(0.0)), HandAt({{-0.3, 1.0, 0}, {}}, false)};
	in[1].grip = 0;
	h.Place(0, hh::Kind::Sword, false, hh::Line {{}, {0, 0, -1}}, in, s);
	hh::HandOut out[2];
	const int steps = (int)std::ceil(secs / kDt);
	for (int i = 1; i <= steps; ++i) {
		const double t = i * kDt;
		in[0] = HandAt(path(t));
		if (on) h.SetWalls(walls.data(), (int)walls.size());
		h.Step(in, kDt, s, out);
		V3 tip;
		h.Ends(nullptr, &tip);
		const hw::Report& c = h.LastWall();
		if (c.walls > 0) {
			++r.contactSteps;
			if (c.approach > r.maxApproach) { r.maxApproach = c.approach; r.metId = c.id; }
		}
		if (c.ignored > 0) ++r.ignoredSteps;
		for (const hw::Wall& w : walls) r.deepest = std::max(r.deepest, Inside(w, tip));
		r.tips.push_back(tip);
		if (g_table && name)
			std::printf("  %-14s t %.3f  ctl (%.2f %.2f %.2f)  tip (%.3f %.3f %.3f)  walls %d depth %.1f cm approach %.2f face %d%s\n", name, t, in[0].pose.pos.x,
			            in[0].pose.pos.y, in[0].pose.pos.z, tip.x, tip.y, tip.z, c.walls, c.depth * 100, c.approach, c.face, c.ignored ? " left alone" : "");
	}
	r.returns = h.WallReturns();
	h.Ends(nullptr, &r.tipEnd);
	return r;
}

// The sword's tip with the hand at the origin, at rest (its z: the shaft along -Z).
double RestTipZ() {
	hh::Hold probe;
	hh::Settings s;
	hh::HandIn in[2] = {HandAt({{0, 1.2, 0}, {}}), HandAt({}, false)};
	probe.Place(0, hh::Kind::Sword, false, hh::Line {{}, {0, 0, -1}}, in, s);
	V3 tip;
	probe.Ends(nullptr, &tip);
	return tip.z;
}

// A wall along x at cell row z = -3 (its face at z = -1.0), x -4..4.
uw::Patch RowWall(uint16_t middleBits = 0x4) {
	uw::Patch p = EmptyPatch();
	for (int x = -4; x <= 4; ++x) Set(p, x, -3, x == 0 ? middleBits : 0x4);
	return p;
}

// The thrust: the hand pushed 0.5 m forward in 0.25 s, held, back; the face 0.25 m past the tip at rest.
void TestThrust() {
	const double tip0 = RestTipZ();
	const double start = -0.75 - tip0;   // the hand's z: the tip at rest 25 cm short of the face (z -1.0)
	auto path = [start](double t) {
		const double out = Smooth(t / 0.25) - Smooth((t - 0.8) / 0.25);
		return Pose {{0.25, 1.2, start - 0.5 * out}, {}};
	};
	const std::vector<hw::Wall> walls = WallsOf(RowWall());
	const Run off = Swing(path, 1.4, walls, false);
	const Run on = Swing(path, 1.4, walls, true, "thrust");
	std::printf("thrust into the cells: without them the tip %.1f cm in, with them %.1f cm (%d steps on the face, %.2f m/s in)\n", off.deepest * 100, on.deepest * 100,
	            on.contactSteps, on.maxApproach);
	EXPECT(off.deepest > 0.15, "without the walls it goes in: %.1f cm", off.deepest * 100);
	EXPECT(on.deepest < 0.015, "it stops on the face: %.1f cm in", on.deepest * 100);
	EXPECT(on.contactSteps > 10, "it rests on the face while the hand is past it: %d", on.contactSteps);
	EXPECT(on.metId == wallcollide::CellId(0, -3) && (on.metId & wallcollide::kDoorIdBit) == 0, "the cell it met: %08x", on.metId);
	EXPECT(wallcollide::KnockAmplitude((float)on.maxApproach) >= 0.6f, "a knock felt: %.2f m/s", on.maxApproach);
}

// Along the wall: the blade pressed 6 cm into it, the hand moved 2 m along x - it slides across the cells' seams and is not caught on them.
double Slide(bool allFaces, double* deepest) {
	const double tip0 = RestTipZ();
	const double start = -0.94 - tip0;   // the tip 6 cm into the face (z -1.0)
	auto path = [start](double t) {
		const double in = Smooth(t / 0.15);
		return Pose {{-1.0 + 2.0 * Smooth((t - 0.2) / 1.2), 1.2, start - 0.4 + 0.4 * in}, {}};
	};
	const std::vector<hw::Wall> walls = WallsOf(RowWall(), false, allFaces);
	const Run on = Swing(path, 1.6, walls, true, allFaces ? "slide (all)" : "slide");
	double minX = 1e9, maxX = -1e9;
	for (size_t i = 30; i < on.tips.size(); ++i) { minX = std::min(minX, on.tips[i].x); maxX = std::max(maxX, on.tips[i].x); }
	*deepest = on.deepest;
	return maxX - minX;
}
void TestSlide() {
	double deep = 0, deepAll = 0;
	const double along = Slide(false, &deep), alongAll = Slide(true, &deepAll);
	std::printf("slide along the wall: the tip went %.2f m of the hand's 2 m, at most %.1f cm in (every face of every cell open: %.2f m, %.1f cm)\n", along, deep * 100,
	            alongAll, deepAll * 100);
	EXPECT(along > 1.7, "it slides across the seams: %.2f m", along);
	EXPECT(deep < 0.02, "it stays on the face: %.1f cm in", deep * 100);
}

// An inner corner: the hand pushed diagonally into it - held out of both walls.
void TestCorner() {
	uw::Patch p = EmptyPatch();
	for (int i = -3; i <= 3; ++i) { Set(p, i, -3, 0x4); Set(p, -3, i, 0x4); }   // faces at z = -1.0 and x = -1.0
	const std::vector<hw::Wall> walls = WallsOf(p);
	const double tip0 = RestTipZ();
	// the sword turned 45 degrees left, its tip short of the corner (-1, -1), then the hand pushed 0.9 m along its point
	const Q yaw = QFromAxisAngle({0, 1, 0}, 0.785398);
	const V3 dir = Rotate(yaw, {0, 0, -1});
	const V3 tipAtRest = {-0.6, 1.2, -0.6};
	const V3 hand0 = Sub(tipAtRest, Mul(dir, -tip0));
	auto path = [&](double t) { return Pose {Add(hand0, Mul(dir, 0.9 * Smooth(t / 0.3))), yaw}; };
	const Run off = Swing(path, 0.9, walls, false);
	const Run on = Swing(path, 0.9, walls, true, "corner");
	std::printf("inner corner: without the walls the tip %.1f cm in, with them %.1f cm (end tip %.2f %.2f)\n", off.deepest * 100, on.deepest * 100, on.tipEnd.x, on.tipEnd.z);
	EXPECT(off.deepest > 0.1, "without them it goes in: %.1f cm", off.deepest * 100);
	EXPECT(on.deepest < 0.02 && on.tipEnd.x > -1.02 && on.tipEnd.z > -1.02, "held out of both walls: %.1f cm in", on.deepest * 100);
}

// The floor: the sword pointing down, the hand pushed down 0.6 m - it stops on the floor (y 0) and pushes only up.
void TestFloor() {
	const double tip0 = RestTipZ();   // negative: the blade's length ahead of the hand
	const Q down = QFromAxisAngle({1, 0, 0}, -1.5707963);   // -Z (the blade) turned to -Y
	const double y0 = 0.25 - tip0;    // the tip at rest 25 cm over the floor
	auto path = [=](double t) { return Pose {{0.2, y0 - 0.6 * Smooth(t / 0.3), 0.3}, down}; };
	const std::vector<hw::Wall> walls = WallsOf(EmptyPatch(), true);
	const Run off = Swing(path, 0.8, walls, false);
	const Run on = Swing(path, 0.8, walls, true, "floor");
	std::printf("floor: without it the tip goes %.1f cm under, with it %.1f cm (end tip y %.3f), met %08x\n", off.deepest * 100, on.deepest * 100, on.tipEnd.y, on.metId);
	EXPECT(walls.size() == 1, "only the floor: %zu", walls.size());
	EXPECT(off.deepest > 0.15, "without it the tip goes under: %.1f cm", off.deepest * 100);
	EXPECT(on.deepest < 0.015 && on.tipEnd.y > -0.015, "it stops on the floor: %.1f cm under", on.deepest * 100);
	EXPECT(on.metId == wallcollide::kFloorId, "the floor met: %08x", on.metId);
}

// A closed door stops the thrust as a wall does; a barrel's footprint in its place does not.
void TestDoorAndBarrel() {
	const double tip0 = RestTipZ();
	const double start = -0.75 - tip0;
	auto path = [start](double t) { return Pose {{0.25, 1.2, start - 0.5 * Smooth(t / 0.25)}, {}}; };
	uw::Patch door = EmptyPatch(), barrel = EmptyPatch();
	Set(door, 0, -3, 0x804);
	Set(barrel, 0, -3, 0x404);
	const Run d = Swing(path, 0.6, WallsOf(door), true, "door");
	const std::vector<hw::Wall> bw = WallsOf(barrel);
	const Run b = Swing(path, 0.6, bw, true, "barrel");
	std::printf("closed door: the tip %.1f cm in, met %08x; a barrel's footprint: %zu boxes, %d steps on it\n", d.deepest * 100, d.metId, bw.size(), b.contactSteps);
	EXPECT(d.deepest < 0.015 && d.contactSteps > 5 && (d.metId & wallcollide::kDoorIdBit), "the door stops it: %.1f cm in", d.deepest * 100);
	EXPECT(bw.empty() && b.contactSteps == 0, "the barrel's footprint stops nothing");
}

// A fast cut across a wall one cell thick (0.5 m): the hand swept right to left in 0.12 s - the tip never ends on the far side.
void TestFastCut() {
	uw::Patch p = EmptyPatch();
	for (int z = -4; z <= -1; ++z) Set(p, 0, z, 0x4);   // x 0..0.5, z -2.0..0
	const std::vector<hw::Wall> walls = WallsOf(p);
	auto path = [](double t) {
		const double u = Smooth(t / 0.12);
		const double a = -0.9 + 1.8 * u;   // the swing's angle about the hand
		return Pose {{-0.5, 1.2, 0.2}, QFromAxisAngle({0, 1, 0}, -a)};
	};
	const Run off = Swing(path, 0.4, walls, false);
	const Run on = Swing(path, 0.4, walls, true, "fast cut");
	const bool fromRight = off.tips.front().x > 0.25;
	double far = -1e9, maxStep = 0;
	for (const V3& t : on.tips)
		if (t.z < -0.05 && t.z > -1.95) far = std::max(far, fromRight ? -t.x : t.x - 0.5);
	for (size_t i = 1; i < off.tips.size(); ++i) maxStep = std::max(maxStep, Len(Sub(off.tips[i], off.tips[i - 1])));
	std::printf("fast cut: the tip at %.1f m/s; with the wall %d steps on it (up to %.2f m/s), at most %.1f cm past the far face, %.1f cm in it\n", maxStep / kDt,
	            on.contactSteps, on.maxApproach, far * 100, on.deepest * 100);
	EXPECT(maxStep / kDt > 6.0, "a fast cut: %.1f m/s", maxStep / kDt);
	EXPECT(on.contactSteps > 0, "it struck the wall");
	EXPECT(far < 0.0, "never on the far side: %.1f cm past it", far * 100);
	EXPECT(on.deepest < 0.03, "never in the wall: %.1f cm", on.deepest * 100);
}

void TestKnock() {
	EXPECT(wallcollide::KnockAmplitude(0.3f) == 0.0f, "slow: no knock");
	EXPECT(wallcollide::KnockAmplitude(0.5f) == 0.6f, "from 0.5 m/s: 0.6");
	EXPECT(std::fabs(wallcollide::KnockAmplitude(2.5f) - 0.8f) < 1e-6f, "2.5 m/s: 0.8");
	EXPECT(wallcollide::KnockAmplitude(20.0f) == 1.0f && wallcollide::KnockAmplitude(NAN) == 0.0f, "at most 1; NaN none");
	std::printf("knock: 0 under 0.5 m/s, 0.6 .. 1.0 - ok\n");
}

// [weight] wall_contact_mode: the one choice, and an old ini's world_contact read as Ours
void TestMode() {
	namespace wc = wallcontact;
	EXPECT(wc::FromKeys(-1.0f, false) == wc::kOff && wc::FromKeys(-1.0f, true) == wc::kOurs, "missing: the old tick decides");
	EXPECT(wc::FromKeys(0.0f, true) == wc::kOff, "chosen Off wins over an old world_contact=1");
	EXPECT(wc::FromKeys(1.0f, false) == wc::kOurs && wc::FromKeys(2.0f, true) == wc::kBears, "Ours, Bear's");
	EXPECT(wc::FromKeys(7.0f, false) == wc::kBears && wc::FromKeys(NAN, true) == wc::kOurs, "clamped; NaN as missing");
	std::printf("mode: Off / Ours / Bear's, an old world_contact=1 as Ours - ok\n");
}

void TestCost() {
	uw::Patch p = EmptyPatch();
	for (int z = -16; z < 16; ++z)
		for (int x = -16; x < 16; ++x)
			if ((x * 7 + z * 3) % 5 == 0) Set(p, x, z, 0x4);
	const auto t0 = std::chrono::steady_clock::now();
	size_t total = 0;
	for (int i = 0; i < 2000; ++i) total += WallsOf(p, true).size();
	const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000;
	std::printf("cost: %.1f us to lay a crowded patch's %zu walls in the hands' frame\n", us, total / 2000);
	EXPECT(us < 200.0, "cheap enough for each weight step: %.1f us", us);
}

}  // namespace

int main(int argc, char** argv) {
	g_table = argc > 1 && std::strcmp(argv[1], "table") == 0;
	TestMark();
	TestCellAt();
	TestBoxes();
	TestSampleDue();
	TestToHead();
	TestThrust();
	TestSlide();
	TestCorner();
	TestFloor();
	TestDoorAndBarrel();
	TestFastCut();
	TestKnock();
	TestMode();
	TestCost();
	if (g_fails) {
		std::printf("unleashed_wall_cells_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("unleashed_wall_cells_test: all passed\n");
	return 0;
}
