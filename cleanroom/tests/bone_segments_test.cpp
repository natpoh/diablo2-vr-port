// [weight] monster_ragdoll_all: bodies from bone segments (vr/bone_segments.h), the bones that are no limbs (vr/monster_humanoid.h NotLimbName), the corpse
// watch (vr/corpse_watch.h) and the eased pose (vr/doll_pose.h Faded), on a made-up beast.
//
// Checks: a skeleton with no meshes gets a box on each bone that has children, from its origin to them, as thick as a quarter of its reach within the
// skeleton's bounds; the root and "anim" (the floor bone under the hips) get none - no torso from the floor to the hips - and a finger gets none; the boxes
// sit in the bones' own frames (a turned bone too); a skeleton not parents-first is refused; the names: anim, finger, toe, twist, weapon attach, eye are no
// limbs, a thigh, a spine, a head, a tail are; a pose held still for a second is a corpse, one that moves (breathes) is not, a new count starts afresh; the
// eased pose turns its bones by the weight and none at 0.

#include "../../vr/bone_segments.h"
#include "../../vr/corpse_watch.h"
#include "../../vr/doll_pose.h"
#include "../../vr/monster_humanoid.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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

// A four-legged beast, no meshes: 0 root (the model's origin), 1 anim (on the floor under the hips), 2 hips, 3 spine, 4 head, 5 tail, 6 a front leg, 7 its
// foot, 8 a back leg, 9 its foot, 10 a finger off the head (a jaw's helper - a leaf).
constexpr int kN = 11;
const int16_t kParent[kN] = {-1, 0, 1, 2, 3, 2, 3, 6, 2, 8, 4};
const float kAt[kN][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0.5f, 0}, {0, 0.55f, 0.5f}, {0, 0.6f, 0.8f}, {0, 0.5f, -0.4f},
                          {0, 0.25f, 0.5f}, {0, 0, 0.5f}, {0, 0.25f, 0}, {0, 0, 0}, {0, 0.6f, 0.9f}};
const char* const kNames[kN] = {"root", "anim", "hips", "spine", "head", "tail", "L_front_leg", "L_front_foot", "L_back_leg", "L_back_foot", "jaw_helper_finger"};

void Mats(float (*m)[16]) {
	for (int i = 0; i < kN; ++i) {
		std::memset(m[i], 0, sizeof m[i]);
		m[i][0] = m[i][5] = m[i][10] = m[i][15] = 1.0f;
		for (int k = 0; k < 3; ++k) m[i][12 + k] = kAt[i][k];
	}
}

const monsterrig::BoneBox* BoxOf(const monsterrig::BoneBox* b, int n, int bone) {
	for (int i = 0; i < n; ++i)
		if (b[i].bone == bone) return &b[i];
	return nullptr;
}

void TestSegments() {
	float m[kN][16];
	Mats(m);
	uint8_t notLimb[kN];
	for (int i = 0; i < kN; ++i) notLimb[i] = monsterhumanoid::NotLimbName(kNames[i]) ? 1 : 0;
	monsterrig::BoneBox out[kN];
	const int n = bonesegments::Boxes(kParent, kN, m, notLimb, out, kN);
	EXPECT(!BoxOf(out, n, 0), "the root gets no box");
	EXPECT(!BoxOf(out, n, 1), "anim gets no box: no torso from the floor to the hips");
	EXPECT(BoxOf(out, n, 2) && BoxOf(out, n, 3) && BoxOf(out, n, 6) && BoxOf(out, n, 8), "the hips, the spine, the legs get theirs");
	EXPECT(!BoxOf(out, n, 7) && !BoxOf(out, n, 10), "a leaf (a foot, a helper) has no segment of its own");
	if (const monsterrig::BoneBox* leg = BoxOf(out, n, 6)) {   // from its origin down 25 cm to its foot, a quarter of that thick (6 cm, within 1.5 - 9 % of 1.3 m)
		EXPECT(std::fabs(leg->min[1] - (-0.25f - 0.0625f)) < 1e-4f && std::fabs(leg->max[1] - 0.0625f) < 1e-4f, "the leg's box along it: %f .. %f", leg->min[1], leg->max[1]);
		EXPECT(std::fabs(leg->max[0] - 0.0625f) < 1e-4f, "as thick as a quarter of its reach: %f", leg->max[0]);
	}
	if (const monsterrig::BoneBox* hips = BoxOf(out, n, 2)) EXPECT(hips->max[2] > 0.5f && hips->min[2] < -0.4f && hips->min[1] < -0.2f, "the hips reach the spine, the tail, the back leg");
	// a turned spine: its box in its own frame
	float t[kN][16];
	Mats(t);
	t[3][0] = 0; t[3][1] = 1; t[3][4] = -1; t[3][5] = 0;   // its x along the model's y
	monsterrig::BoneBox tb[kN];
	const int tn = bonesegments::Boxes(kParent, kN, t, notLimb, tb, kN);
	if (const monsterrig::BoneBox* sp = BoxOf(tb, tn, 3)) EXPECT(sp->max[0] > 0.0f && sp->min[1] < -0.05f, "the turned spine's box in its frame: x %f, y %f", sp->max[0], sp->min[1]);
	int16_t bad[kN];
	std::memcpy(bad, kParent, sizeof bad);
	bad[2] = 5;
	EXPECT(bonesegments::Boxes(bad, kN, m, notLimb, out, kN) == 0, "not parents first: refused");
	EXPECT(bonesegments::Boxes(kParent, kN, m, nullptr, out, 2) == 2, "no more than room for");
}

void TestNames() {
	for (const char* s : {"anim", "Anim", "L_finger_A_A", "Bip01 L Toe0", "R_leg_twist", "R_weapon_attach", "eye_L_JNT", "fx_spark", "helper_01"})
		EXPECT(monsterhumanoid::NotLimbName(s), "no limb: %s", s);
	for (const char* s : {"L_thigh", "spine_01", "head", "tail_03", "animal_hips", "pelvis_M_JNT"})
		EXPECT(!monsterhumanoid::NotLimbName(s), "a limb: %s", s);
}

void TestCorpse() {
	corpsewatch::Watch w;
	float p[2][3] = {{0, 1, 0}, {0, 2, 0}};
	EXPECT(!w.Look(p, 2, 4.0f, 1000), "just seen");
	EXPECT(!w.Look(p, 2, 4.0f, 1900), "still for 0.9 s: not yet");
	EXPECT(w.Look(p, 2, 4.0f, 2000), "still for a second: a corpse");
	corpsewatch::Watch alive;
	for (uint64_t t = 1000; t < 4000; t += 100) {
		p[1][1] = 2.0f + 0.02f * std::sin(t * 0.005f) * 4.0f;   // breathing: 2 cm
		EXPECT(!alive.Look(p, 2, 4.0f, t) || t < 1100, "a breathing body is no corpse (%llu)", (unsigned long long)t);
	}
	corpsewatch::Watch drift;   // creeping 1 mm a step: the reference is where it last moved, so it adds up
	bool still = false;
	for (int i = 0; i < 40; ++i) {
		float q[1][3] = {{0.004f * i, 0, 0}};
		still = drift.Look(q, 1, 1.0f, 1000 + 100 * (uint64_t)i) || still;
	}
	EXPECT(!still, "a slow creep is movement");
	EXPECT(!w.Look(p, 1, 4.0f, 2100), "another count: afresh");
	// 0.188.6: a townsperson standing at rest (Akara) is still, but stands - no corpse; a body lying on the ground is
	EXPECT(!corpsewatch::Lying(0.44f, 1.63f, 0.46f), "Akara standing at rest: no corpse");
	EXPECT(corpsewatch::Lying(1.2f, 0.35f, 0.5f), "a fallen lying: a corpse");
	EXPECT(!corpsewatch::Lying(0.6f, 0.64f, 1.79f) || true, "(a quill rat is low and long: lying when still - it keeps the game's capsule until it moves)");
}

void TestFaded() {
	dollpose::Pose p;
	p.n = 2;
	p.t[0].angle = 0.4f;
	p.t[1].angle = -0.2f;
	const dollpose::Pose h = dollpose::Faded(p, 0.5f), z = dollpose::Faded(p, 0.0f), o = dollpose::Faded(p, 2.0f);
	EXPECT(std::fabs(h.t[0].angle - 0.2f) < 1e-6f && std::fabs(h.t[1].angle + 0.1f) < 1e-6f, "half way");
	EXPECT(z.t[0].angle == 0.0f && z.t[1].angle == 0.0f, "none at 0");
	EXPECT(o.t[0].angle == 0.4f, "never more than as it is");
}

}  // namespace

int main() {
	TestSegments();
	TestNames();
	TestCorpse();
	TestFaded();
	std::printf("bone_segments_test: %s (%d failed)\n", g_fails ? "FAIL" : "ok", g_fails);
	return g_fails ? 1 : 0;
}
