// [weight] grab_bodies: the mod's glue to the weight library's grab (vr/grab_bodies.h, vr/grab_lib.cpp).
//
//   grab_bodies_test                   every check below that needs no library
//   grab_bodies_test dll=<path>        ... and through the loader against the weight library (d2r_vr_weight.dll 1.4+): a hand takes an arm, pulls it, lets go
//
// Checks: the bodies handed over as the weapon meets them (a limb as thick as its mesh, the game's capsule less the touch's margin), the monsters' shields and weapons
// left out, a limb marked to give only with the doll on, the hostile flag looked up; the hand's turn taken into the render world (a hand-local vector turned into the head frame and
// mapped equals it turned by the render turn); the loader: no file, a DLL without the grab, then the library's grab end to end.

#include "../../vr/grab_bodies.h"
#include "../../vr/doll_view.h"
#include "../../vr/grab_lib.h"
#include "../../vr/doll_pose.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_checks = 0, g_fails = 0;
std::string g_log;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

void Log(const char* s) { g_log += s; g_log += "\n"; }

float Dist(const float a[3], const float b[3]) {
	return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

using monsterbodies::Body;
using monsterbodies::Snapshot;
namespace gb = grabbodies;

Body& Add(Snapshot& s, uint32_t id, int bone, float ax, float ay, float az, float bx, float by, float bz, float r) {
	Body& b = s.b[s.n++];
	b = Body{};
	b.id = id;
	b.bone = bone;
	b.rig = bone >= 0;
	b.doll = bone >= 0;
	b.bottom[0] = ax; b.bottom[1] = ay; b.bottom[2] = az;
	b.top[0] = bx; b.top[1] = by; b.top[2] = bz;
	b.radius = r;
	return b;
}

// A humanoid at x (render metres): a trunk, an arm held out sideways (+x), two legs - the shape of the library's own tests.
void Humanoid(Snapshot& s, uint32_t id, float x) {
	Add(s, id, 0, x, 0.9f, 0.0f, x, 1.5f, 0.0f, 0.15f);
	Add(s, id, 1, x + 0.2f, 1.4f, 0.0f, x + 0.8f, 1.4f, 0.0f, 0.05f);
	Add(s, id, 2, x - 0.1f, 0.0f, 0.0f, x - 0.1f, 0.85f, 0.0f, 0.07f);
	Add(s, id, 3, x + 0.1f, 0.0f, 0.0f, x + 0.1f, 0.85f, 0.0f, 0.07f);
}

void TestToGrab() {
	Snapshot s;
	Humanoid(s, 7, 0.0f);
	Body& shield = Add(s, 7, 20, 0.0f, 1.0f, -0.3f, 0.0f, 1.4f, -0.3f, 0.2f);
	shield.held = 2;
	Body& npc = Add(s, 11, -1, 2.0f, 0.0f, 0.0f, 2.0f, 1.7f, 0.0f, 0.4f);
	npc.hostile = false;
	D2rgbBody out[D2RGB_MAX_BODIES];
	int of[D2RGB_MAX_BODIES];
	const int n = gb::ToGrab(s, true, out, of);
	EXPECT(n == 5, "the shield left out: %d bodies", n);
	EXPECT(out[1].id == 7 && out[1].bone == 1 && out[1].radius == 0.05f && out[1].doll == 1 && of[1] == 1, "a limb as thick as its mesh, giving way");
	EXPECT(out[4].bone == -1 && std::fabs(out[4].radius - monsterbodies::CoreRadius(0.4f)) < 1e-6f && out[4].doll == 0 && of[4] == 5, "the game's capsule less the touch's margin, standing");
	EXPECT(out[1].a[0] == 0.2f && out[1].b[0] == 0.8f, "the ends as the game poses them");
	gb::ToGrab(s, false, out);
	EXPECT(out[1].doll == 0, "without the doll nothing gives");
	EXPECT(gb::Hostile(s, 7) && !gb::Hostile(s, 11) && !gb::Hostile(s, 99), "hostile: the monster yes, the NPC no, an unknown no");
}

void TestBusy() {
	gb::HandCase c;
	EXPECT(gb::BusyWhy(c) == 0, "an empty hand is free");
	c.slotFree = false;
	EXPECT(gb::BusyWhy(c) == gb::kSlot, "a weapon or shield in its slot: busy");
	// a bow (the town test 2026-10-26: an Amazon with a bow, no hand was free)
	gb::HandCase bow;
	bow.ranged = true;
	bow.slotFree = false;   // (vrcam marks both hands not free with a bow)
	bow.bowHand = true;
	EXPECT(gb::BusyWhy(bow) == gb::kBowHand, "the bow hand: busy with the bow");
	bow.bowHand = false;
	EXPECT(gb::BusyWhy(bow) == 0, "the string hand: free");
	bow.bowString = true;
	EXPECT(gb::BusyWhy(bow) == gb::kBowString && std::strstr(gb::BusyText(gb::kBowString), "string"), "... unless it draws the string");
	// the weight model
	gb::HandCase w;
	w.weightOn = true;
	w.slotFree = false;
	w.weightSlot = true;
	EXPECT(gb::BusyWhy(w) == 0, "the weapon's own hand, the weapon dropped: free");
	w.weightHolds = true;
	EXPECT(gb::BusyWhy(w) == gb::kWeapon, "on the weapon: busy");
	w.weightHolds = false;
	w.takeZone = true;
	EXPECT(gb::BusyWhy(w) == gb::kTakeZone, "the weapon in reach: its grip takes the weapon first");
	gb::HandCase b;
	b.bottle = true;
	b.shoulder = true;
	EXPECT(gb::BusyWhy(b) == (gb::kBottle | gb::kShoulder) && std::strstr(gb::BusyText(gb::BusyWhy(b)), "bottle"), "a bottle and the shoulder: both said, the first in words");
	EXPECT(std::strcmp(gb::BusyText(0), "free") == 0, "free in words");
}

void TestNearest() {
	Snapshot s;
	Humanoid(s, 7, 0.0f);
	D2rgbBody out[D2RGB_MAX_BODIES];
	const int n = gb::ToGrab(s, true, out);
	const float palm[3] = {0.6f, 1.4f, -0.2f};
	int which = -1;
	const float d = gb::Nearest(out, n, palm, &which);
	EXPECT(which == 1 && std::fabs(d - 0.15f) < 1e-4f, "the arm, 15 cm off its surface (%d, %.4f)", which, d);
	EXPECT(gb::Nearest(out, 0, palm, &which) > 1e29f && which == -1, "no bodies: none");
}

// [weight] grab_arm_reach: the held point past the drawn arm lets go - unless the controller is out of reach on its own (full_reach draws it there without a grab).
void TestPastArm() {
	const float shoulder[3] = {0.2f, -0.25f, 0.05f};   // the hands' frame, metres from the eye
	const float arm = 0.70f;
	const float at60[3] = {0.2f, -0.25f, -0.55f};     // 60 cm ahead of the shoulder
	const float at75[3] = {0.2f, -0.25f, -0.70f};      // 75 cm
	const float at90[3] = {0.2f, -0.25f, -0.85f};  // 90 cm
	EXPECT(!gb::PastArm(shoulder, arm, at60, at60), "held in reach, the controller on it: holds");
	EXPECT(gb::PastArm(shoulder, arm, at75, at60), "held 75 cm out, the arm 70, the controller pulled back to 60: lets go (the arm would lengthen)");
	EXPECT(!gb::PastArm(shoulder, arm, at75, at90), "the controller 90 cm out on its own, the held point 75: holds (no longer than without the grab)");
	EXPECT(gb::PastArm(shoulder, arm, at90, at75), "the controller 75 out, the held point 90: lets go");
	// Serg's props, 2026-10-27: the drawn arm 63 cm, his controller 70 cm out, the vase held at the controller give or take the hand on its surface (71 cm): it held 0.0 s
	const float arm63 = 0.63f, ctrl70[3] = {0.2f, -0.25f, -0.65f}, held71[3] = {0.2f, -0.25f, -0.66f}, held80[3] = {0.2f, -0.25f, -0.75f};
	EXPECT(!gb::PastArm(shoulder, arm63, held71, ctrl70, 0.08f), "the controller past the arm on its own, the prop held 1 cm past it (the hand on its surface): holds");
	EXPECT(gb::PastArm(shoulder, arm63, held80, ctrl70, 0.08f), "the prop 10 cm past that controller, the reach 8: lets go");
	EXPECT(gb::PastArm(shoulder, arm, at75, at60, 0.08f) == false && gb::PastArm(shoulder, arm, at90, at60, 0.08f), "the slack counts past the arm too: 5 cm holds, 20 lets go");
	EXPECT(!gb::PastArm(shoulder, 0.0f, at90, at60) && !gb::PastArm(shoulder, NAN, at90, at60), "no arm length: no verdict (holds)");
	const float bad[3] = {NAN, 0.0f, 0.0f};
	EXPECT(!gb::PastArm(shoulder, arm, bad, at60), "a held point not finite: holds");
}

// [weight] grab_bodies' active ragdoll: each limb linked to the limb above it up its skeleton (monsterbodies::LinkParents), handed over with the library's indices.
void TestParents() {
	Snapshot s;
	// bones: 0 pelvis (no capsule), 1 trunk, 2 upper arm, 3 forearm, 4 hand, 5 a finger (no capsule), 6 the finger tip (a capsule: its parent is the hand)
	const int16_t parent[7] = {-1, 0, 1, 2, 3, 4, 5};
	Add(s, 7, 1, 0.0f, 0.9f, 0.0f, 0.0f, 1.5f, 0.0f, 0.15f);
	Body& shield = Add(s, 7, 20, 0.0f, 1.0f, -0.3f, 0.0f, 1.4f, -0.3f, 0.2f);   // (what it holds: not a body of its own)
	shield.held = 2;
	Add(s, 7, 2, 0.2f, 1.4f, 0.0f, 0.5f, 1.4f, 0.0f, 0.05f);
	Add(s, 7, 3, 0.5f, 1.4f, 0.0f, 0.75f, 1.4f, 0.0f, 0.04f);
	Add(s, 7, 4, 0.75f, 1.4f, 0.0f, 0.9f, 1.4f, 0.0f, 0.04f);
	Add(s, 7, 6, 0.9f, 1.4f, 0.0f, 0.95f, 1.4f, 0.0f, 0.01f);
	Add(s, 8, 2, 3.0f, 1.4f, 0.0f, 3.5f, 1.4f, 0.0f, 0.05f);   // another monster's limb of the same bone: never a parent across monsters
	monsterbodies::LinkParents(&s, 7, parent, 7);
	EXPECT(s.b[0].parent == -1, "the trunk: a root (its pelvis has no capsule)");
	EXPECT(s.b[2].parent == 0 && s.b[3].parent == 2 && s.b[4].parent == 3, "the arm: upper on the trunk, forearm on it, hand on that");
	EXPECT(s.b[5].parent == 4, "a bone without a capsule is skipped: the tip hangs on the hand");
	EXPECT(s.b[1].parent == -1 && s.b[6].parent == -1, "what it holds and another monster stay unlinked");
	D2rgbBody out[D2RGB_MAX_BODIES];
	const int n = gb::ToGrab(s, true, out);
	EXPECT(n == 6, "the shield left out (%d)", n);
	EXPECT(out[0].parent == -1 && out[1].parent == 0 && out[2].parent == 1 && out[3].parent == 2 && out[4].parent == 3 && out[5].parent == -1,
	       "the parents in the library's indices, past the shield left out (%d %d %d %d %d %d)", out[0].parent, out[1].parent, out[2].parent, out[3].parent, out[4].parent, out[5].parent);
}

// The held limbs put into the bodies where the library holds them: each end where its link's end is, whichever way the capsule was written.
void TestPutLinks() {
	Snapshot s;
	Humanoid(s, 7, 0.0f);
	Body& shield = Add(s, 7, 1, 0.0f, 1.0f, -0.3f, 0.0f, 1.4f, -0.3f, 0.2f);   // (what it holds on the same bone: left alone)
	shield.held = 2;
	D2rgbLink l{};
	l.id = 7;
	l.bone = 1;
	const float joint[3] = {0.21f, 1.45f, -0.02f}, tip[3] = {0.7f, 1.6f, -0.3f};
	for (int k = 0; k < 3; ++k) { l.joint[k] = joint[k]; l.tip[k] = tip[k]; }
	EXPECT(gb::PutLinks(&s, &l, 1) == 1, "one limb put");
	EXPECT(Dist(s.b[1].bottom, joint) < 1e-6f && Dist(s.b[1].top, tip) < 1e-6f, "the arm's shoulder end at the link's joint, its hand end at its tip");
	EXPECT(s.b[4].bottom[2] == -0.3f && s.b[4].held == 2, "what it holds is not moved");
	EXPECT(s.b[0].top[1] == 1.5f, "the other limbs stay");
	D2rgbLink none = l;
	none.id = 99;
	EXPECT(gb::PutLinks(&s, &none, 1) == 0, "another monster's link: nothing");
}

// A held trunk carries what hangs below it - the arm on it, the hand on the arm - the way the drawn model turns them (headset 0.183: the model leaned whole, the
// bodies only in part); what does not hang on it (the legs) stays.
void TestPutLinksCarriesBelow() {
	Snapshot s;
	Humanoid(s, 7, 0.0f);   // 0 the trunk (0.9 -> 1.5 up), 1 the arm (out along +x at 1.4), 2 / 3 the legs
	s.b[1].parent = 0;
	Body& hand = Add(s, 7, 4, 0.8f, 1.4f, 0.0f, 0.95f, 1.4f, 0.0f, 0.04f);
	hand.parent = 1;
	D2rgbLink trunk{};
	trunk.id = 7;
	trunk.bone = 0;
	const float tj[3] = {0.1f, 0.9f, 0.0f}, tt[3] = {0.7f, 0.9f, 0.0f};   // leaned over onto +x (a quarter turn about -z), its foot 10 cm along
	for (int k = 0; k < 3; ++k) { trunk.joint[k] = tj[k]; trunk.tip[k] = tt[k]; }
	EXPECT(gb::PutLinks(&s, &trunk, 1) == 1, "the trunk put");
	EXPECT(Dist(s.b[0].bottom, tj) < 1e-6f && Dist(s.b[0].top, tt) < 1e-6f, "the trunk on its link");
	const float armA[3] = {0.6f, 0.7f, 0.0f}, armB[3] = {0.6f, 0.1f, 0.0f}, handB[3] = {0.6f, -0.05f, 0.0f};
	EXPECT(Dist(s.b[1].bottom, armA) < 1e-5f && Dist(s.b[1].top, armB) < 1e-5f, "the arm on it turned with it and carried (%.2f %.2f -> %.2f %.2f)", s.b[1].bottom[0], s.b[1].bottom[1],
	       s.b[1].top[0], s.b[1].top[1]);
	EXPECT(Dist(s.b[4].top, handB) < 1e-5f, "the hand on the arm too (%.2f %.2f)", s.b[4].top[0], s.b[4].top[1]);
	EXPECT(s.b[2].top[1] == 0.85f && s.b[3].bottom[0] == 0.1f, "the legs (not on it) stay");
	// the arm held as well, after the trunk: its own line, the hand turned from where the trunk left it
	Snapshot t;
	Humanoid(t, 7, 0.0f);
	t.b[1].parent = 0;
	Body& h2 = Add(t, 7, 4, 0.8f, 1.4f, 0.0f, 0.95f, 1.4f, 0.0f, 0.04f);
	h2.parent = 1;
	D2rgbLink both[2] = {trunk, D2rgbLink{}};
	both[1].id = 7;
	both[1].bone = 1;
	const float aj[3] = {0.6f, 0.7f, 0.0f}, at[3] = {1.2f, 0.7f, 0.0f};   // the arm put back out along +x from where the trunk took it
	for (int k = 0; k < 3; ++k) { both[1].joint[k] = aj[k]; both[1].tip[k] = at[k]; }
	EXPECT(gb::PutLinks(&t, both, 2) == 2, "both put");
	EXPECT(Dist(t.b[1].bottom, aj) < 1e-5f && Dist(t.b[1].top, at) < 1e-5f, "the arm on its own link");
	const float h2a[3] = {1.2f, 0.7f, 0.0f}, h2b[3] = {1.35f, 0.7f, 0.0f};
	EXPECT(Dist(t.b[4].bottom, h2a) < 1e-5f && Dist(t.b[4].top, h2b) < 1e-5f, "the hand along the arm again (%.2f %.2f)", t.b[4].top[0], t.b[4].top[1]);
	EXPECT(gb::HangsBelow(t, 4, 0) && gb::HangsBelow(t, 4, 1) && !gb::HangsBelow(t, 2, 0) && !gb::HangsBelow(t, 0, 4), "what hangs below what");
}

// [weight] grab_wrap without the grip library: the palm laid on the capsule, facing it.
void TestLayPalm() {
	const float a[3] = {0.0f, 0.0f, 0.0f}, b[3] = {0.0f, 1.0f, 0.0f};   // an upright limb, 10 cm thick
	const float palm[3] = {0.0f, 0.5f, -0.3f};                           // the palm 30 cm in front of it
	const float fingers[3] = {0.0f, 0.2f, 1.0f};                         // the fingers pointing at it, a little up
	float to[3], rows[9];
	EXPECT(gb::LayPalm(palm, fingers, a, b, 0.05f, 0.015f, to, rows), "laid");
	EXPECT(std::fabs(to[0]) < 1e-5f && std::fabs(to[1] - 0.5f) < 1e-5f && std::fabs(to[2] + 0.065f) < 1e-5f, "the palm's middle a palm's depth off the surface, over the nearest axis point (%.3f %.3f %.3f)", to[0], to[1], to[2]);
	EXPECT(std::fabs(rows[6]) < 1e-5f && std::fabs(rows[7]) < 1e-5f && std::fabs(rows[8] - 1.0f) < 1e-5f, "the palm faces into the body (z %.2f %.2f %.2f)", rows[6], rows[7], rows[8]);
	EXPECT(std::fabs(rows[0]) < 1e-5f && std::fabs(rows[1] - 1.0f) < 1e-5f && std::fabs(rows[2]) < 1e-5f, "the fingers along the surface, as near as they were (up the limb)");
	const float det = rows[0] * (rows[4] * rows[8] - rows[5] * rows[7]) - rows[1] * (rows[3] * rows[8] - rows[5] * rows[6]) + rows[2] * (rows[3] * rows[7] - rows[4] * rows[6]);
	EXPECT(std::fabs(det - 1.0f) < 1e-4f, "a proper frame (%.4f)", det);
	const float inside[3] = {0.02f, 0.5f, 0.0f}, past[3] = {0.0f, 1.4f, -0.2f};
	EXPECT(gb::LayPalm(inside, fingers, a, b, 0.05f, 0.015f, to, rows) && std::fabs(to[0] - 0.065f) < 1e-5f, "inside it: out to the surface the way it is off the axis");
	EXPECT(gb::LayPalm(past, fingers, a, b, 0.05f, 0.015f, to, rows) && std::fabs(std::sqrt(to[0] * to[0] + (to[1] - 1.0f) * (to[1] - 1.0f) + to[2] * to[2]) - 0.065f) < 1e-5f && to[1] > 1.0f, "past its end: on the end cap, a palm's depth off it");
	const float straight[3] = {0.0f, 0.0f, 1.0f};
	EXPECT(!gb::LayPalm(palm, straight, a, b, 0.05f, 0.015f, to, rows), "fingers pointing straight into it: no frame (the hand stays as it is)");
	EXPECT(!gb::LayPalm(palm, fingers, a, a, 0.05f, 0.015f, to, rows), "a capsule of no length: none");
}

void Rotate(const float q[4], const float v[3], float out[3]) {
	const float u[3] = {q[0], q[1], q[2]}, w = q[3];
	const float t[3] = {2.0f * (u[1] * v[2] - u[2] * v[1]), 2.0f * (u[2] * v[0] - u[0] * v[2]), 2.0f * (u[0] * v[1] - u[1] * v[0])};
	out[0] = v[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]);
	out[1] = v[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]);
	out[2] = v[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0]);
}

void TestRotation() {
	monsterbodies::View v;
	const float yaw = 0.7f;
	v.camR[0] = std::cos(yaw); v.camR[1] = 0.0f; v.camR[2] = -std::sin(yaw);
	v.camF[0] = -std::sin(yaw); v.camF[1] = 0.0f; v.camF[2] = -std::cos(yaw);   // (right, up and back a right-handed basis)
	const float s = std::sin(0.4f), c = std::cos(0.4f);
	const float qHead[4] = {s * 0.6f, 0.0f, s * 0.8f, c};   // some turn about (0.6, 0, 0.8)
	float qR[4];
	gb::HeadRotToRender(v, qHead, qR);
	const float vecs[3][3] = {{1, 0, 0}, {0, 1, 0}, {0.3f, -0.2f, 0.9f}};
	for (const auto& d : vecs) {
		float turned[3], a[3], b[3];
		Rotate(qHead, d, turned);
		dollview::HeadDirToRender(v, turned, a);   // turned in the head frame, then mapped
		Rotate(qR, d, b);                          // the same hand-local vector by the turn in the render world (an orientation: local -> world)
		const float e = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
		EXPECT(e < 1e-4f, "the turn maps with the frame (%.6f off)", e);
	}
	monsterbodies::View m = v;   // the other handedness: still a unit turn
	for (int k = 0; k < 3; ++k) m.camF[k] = -v.camF[k];
	gb::HeadRotToRender(m, qHead, qR);
	const float len = std::sqrt(qR[0] * qR[0] + qR[1] * qR[1] + qR[2] * qR[2] + qR[3] * qR[3]);
	EXPECT(std::fabs(len - 1.0f) < 1e-4f, "a mirrored world: a unit turn (%.5f)", len);
}

void TestLoaderWithout() {
	grablib::SetLog(&Log);
	grablib::SetPath(L"C:\\no\\such\\folder\\d2r_vr_weight.dll");
	EXPECT(!grablib::Ensure() && grablib::GetStatus() == grablib::Status::Missing, "no file: missing");
	EXPECT(g_log.find("grab_bodies does nothing") != std::string::npos, "the log says so");
	D2rgbHand h[2] = {};
	D2rgbHandOut o[2];
	EXPECT(grablib::Step(nullptr, nullptr, 0, h, 0.01f, o) < 0 && grablib::Sways(nullptr, 0) == 0, "no library: nothing done");
	wchar_t sys[MAX_PATH];
	GetSystemDirectoryW(sys, MAX_PATH);
	grablib::SetPath((std::wstring(sys) + L"\\version.dll").c_str());
	EXPECT(!grablib::Ensure() && grablib::GetStatus() == grablib::Status::BrokenExports, "a DLL without the grab: not used");
	EXPECT(g_log.find("has no grab") != std::string::npos, "the log says why");
}

// The library's grab through the loader: the right hand on the arm, the grip, a pull, the grip let go.
void TestLibrary(const wchar_t* path) {
	grablib::SetPath(path);
	EXPECT(grablib::Ensure() && grablib::Loaded(), "loaded: %s", grablib::Name());
	if (!grablib::Loaded()) return;
	Snapshot s;
	Humanoid(s, 7, 0.0f);
	D2rgbBody bodies[D2RGB_MAX_BODIES];
	const int n = gb::ToGrab(s, true, bodies);
	D2rgbParams p{};
	p.size = sizeof p;
	p.limbHz = 2.5f; p.bodyHz = 0.8f; p.damping = 1.0f; p.maxLimbM = 0.25f; p.maxBodyM = 0.20f; p.massScale = 1.0f;   // (the mod's doll: melee_touch.cpp g_dollParams)
	p.strength = 1.0f;
	p.grabRadius = gb::kReachM;
	p.breakDistance = gb::kBreakM;
	D2rgbHand h[2] = {};
	h[0].tracked = 1;
	h[0].mayTake = 1;
	h[0].rot[3] = 1.0f;
	h[0].pos[0] = 0.75f; h[0].pos[1] = 1.4f; h[0].pos[2] = -0.08f;
	D2rgbHandOut o[2];
	int events = 0;
	auto steps = [&](int k) { for (int i = 0; i < k; ++i) { grablib::Step(&p, bodies, n, h, 1.0f / 90.0f, o); events |= o[0].events; } };
	steps(2);
	h[0].grip = 1.0f;
	steps(10);
	EXPECT((events & D2RGB_EV_GRAB) && o[0].state == D2RGB_HOLDING && o[0].id == 7 && o[0].bone == 1 && o[0].gives == 1, "the arm taken and held (state %d)", o[0].state);
	h[0].pos[2] = -0.38f;
	steps(45);
	D2rgbSway sw[D2RGB_MAX_SWAYS];
	const int k = grablib::Sways(sw, D2RGB_MAX_SWAYS);
	float arm = 0.0f;
	for (int i = 0; i < k; ++i)
		if (sw[i].id == 7 && sw[i].bone == 1) arm = sw[i].off[2];
	EXPECT(k >= 2 && arm < -0.03f, "pulled: the arm's sway told toward the hand (%.3f m, %d sways)", arm, k);
	EXPECT(o[0].pos[2] < -0.08f && o[0].gap > 0.0f, "the hand drawn on the arm, held back %.3f m", o[0].gap);
	h[0].grip = 0.0f;
	events = 0;
	steps(1);
	EXPECT((events & D2RGB_EV_RELEASE) && grablib::Sways(sw, D2RGB_MAX_SWAYS) == 0, "let go: released, no sway told");
}

// End to end: a held hand pulled toward the player - the library's sways turned into the doll pose's bone turns (dollpose) - the drawn arm goes WITH the pull,
// where the library holds it (the headset 0.183: «тянется в обратную сторону»). An arm hanging down and in, as a standing NPC's does.
void TestPullDrawn() {
	if (!grablib::Loaded()) return;
	// bones: 0 pelvis, 1 trunk, 2 upper arm (shoulder), 3 forearm (elbow), 4 hand (wrist), 5 the hand's tip, 6 thigh
	const float at[7][3] = {{0, 0.9f, 0}, {0, 0.9f, 0}, {0.2f, 1.5f, 0}, {0.22f, 1.2f, 0}, {0.12f, 0.98f, 0}, {0.08f, 0.85f, 0}, {-0.1f, 0.9f, 0}};
	const int16_t parent[7] = {-1, 0, 1, 2, 3, 4, 0};
	Snapshot s;
	Add(s, 9, 1, 0.0f, 0.9f, 0.0f, 0.0f, 1.55f, 0.0f, 0.08f);
	Add(s, 9, 2, at[2][0], at[2][1], 0.0f, at[3][0], at[3][1], 0.0f, 0.05f);
	Add(s, 9, 3, at[4][0], at[4][1], 0.0f, at[3][0], at[3][1], 0.0f, 0.045f);   // (written wrist first)
	Add(s, 9, 4, at[4][0], at[4][1], 0.0f, at[5][0], at[5][1], 0.0f, 0.04f);
	Add(s, 9, 6, at[6][0], at[6][1], 0.0f, -0.1f, 0.0f, 0.0f, 0.07f);
	monsterbodies::LinkParents(&s, 9, parent, 7);
	D2rgbBody bodies[D2RGB_MAX_BODIES];
	const int n = gb::ToGrab(s, true, bodies);
	D2rgbParams p{};
	p.size = sizeof p;
	p.limbHz = 2.5f; p.bodyHz = 0.8f; p.damping = 1.0f; p.maxLimbM = 0.25f; p.maxBodyM = 0.20f; p.massScale = 1.0f; p.muscle = 1.0f;
	p.strength = 1.0f;
	p.grabRadius = gb::kReachM;
	p.breakDistance = gb::kBreakM;
	D2rgbHand h[2] = {};
	h[0].tracked = 1;
	h[0].mayTake = 1;
	h[0].rot[3] = 1.0f;
	h[0].pos[0] = 0.10f; h[0].pos[1] = 0.92f; h[0].pos[2] = -0.06f;   // in front of the hand
	D2rgbHandOut o[2];
	for (int i = 0; i < 2; ++i) grablib::Step(&p, bodies, n, h, 1.0f / 90.0f, o);
	h[0].grip = 1.0f;
	for (int i = 0; i < 12; ++i) grablib::Step(&p, bodies, n, h, 1.0f / 90.0f, o);
	EXPECT(o[0].state == D2RGB_HOLDING && o[0].bone == 4, "the hand held (state %d, bone %d)", o[0].state, o[0].bone);
	h[0].pos[2] -= 0.20f;   // pulled 20 cm toward the player (-z)
	for (int i = 0; i < 60; ++i) grablib::Step(&p, bodies, n, h, 1.0f / 90.0f, o);
	D2rgbSway sw[D2RGB_MAX_SWAYS];
	const int k = grablib::Sways(sw, D2RGB_MAX_SWAYS);
	dollpose::LimbOff limbs[dollpose::kMaxTurns];
	int nl = 0;
	float body[3] = {};
	for (int i = 0; i < k; ++i) {
		if (sw[i].bone < 0) { for (int c = 0; c < 3; ++c) body[c] = sw[i].off[c]; continue; }
		if (nl < dollpose::kMaxTurns - 1) { limbs[nl].bone = sw[i].bone; for (int c = 0; c < 3; ++c) limbs[nl].off[c] = sw[i].off[c]; ++nl; }
	}
	EXPECT(nl == 4, "the held chain: trunk, upper arm, forearm, hand (%d)", nl);
	dollpose::Rig::Cap caps[5];
	for (int i = 0; i < s.n && i < 5; ++i) {
		for (int c = 0; c < 3; ++c) { caps[i].a[c] = s.b[i].bottom[c]; caps[i].b[c] = s.b[i].top[c]; }
		caps[i].bone = s.b[i].bone;
	}
	dollpose::Rig rig;
	rig.bones = 7;
	rig.parent = parent;
	rig.caps = caps;
	rig.nCaps = 5;
	dollpose::Pose pose;
	EXPECT(dollpose::Build(rig, 1.0f, body, limbs, nl, &pose), "turns made");
	float m[7][16];
	for (int i = 0; i < 7; ++i) {
		std::memset(m[i], 0, sizeof m[i]);
		m[i][0] = m[i][5] = m[i][10] = m[i][15] = 1.0f;
		for (int c = 0; c < 3; ++c) m[i][12 + c] = at[i][c];
	}
	dollpose::Apply(pose, m, 7);
	const float dz = m[5][14] - at[5][2], dzw = m[4][14] - at[4][2];
	EXPECT(dz < -0.05f && dzw < -0.03f, "the drawn hand went WITH the pull, toward the player (tip %.3f, wrist %.3f m in z)", dz, dzw);
	D2rgbLink links[D2RGB_MAX_SWAYS];
	const int nk = grablib::Links(links, D2RGB_MAX_SWAYS);
	for (int i = 0; i < nk; ++i)
		if (links[i].bone == 4) {
			const float* tip = &m[5][12];
			EXPECT(Dist(tip, links[i].tip) < 0.10f, "the drawn hand's tip where the library holds the hand (%.3f m apart)", Dist(tip, links[i].tip));
		}
	h[0].grip = 0.0f;
	grablib::Step(&p, bodies, n, h, 1.0f / 90.0f, o);
}

}  // namespace

int main(int argc, char** argv) {
	TestToGrab();
	TestBusy();
	TestNearest();
	TestPastArm();
	TestLayPalm();
	TestParents();
	TestPutLinks();
	TestPutLinksCarriesBelow();
	TestRotation();
	std::wstring dll;
	for (int i = 1; i < argc; ++i)
		if (std::strncmp(argv[i], "dll=", 4) == 0) {
			const std::string a = argv[i] + 4;
			dll.assign(a.begin(), a.end());
		}
	TestLoaderWithout();
	if (!dll.empty()) {
		TestLibrary(dll.c_str());
		TestPullDrawn();
	}
	std::printf("grab_bodies_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
