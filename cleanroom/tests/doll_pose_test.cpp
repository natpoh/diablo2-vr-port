// [weight] monster_ragdoll_pose: the doll's offsets as turns of the monster's own bones (vr/doll_pose.h), on a made-up skeleton.
//
// Checks: a pushed arm turns about its shoulder so its far end goes the way the doll has it, the hand with it, the rest of the body not; the whole body leans
// about the feet so its hips go where the doll has the body; a transform that turns, scales or mirrors the model gives the same motion in the world; no
// offset - no turn, the pose untouched bit for bit; a pose for another skeleton is not applied; a turn is held to kMaxAngle; the bones' axes stay a proper
// rotation (lengths and right angles kept).

#include "../../vr/doll_pose.h"

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

namespace dp = dollpose;

// A small body in its model's frame (y up, feet at 0): 0 pelvis, 1 spine, 2 head, 3 the right upper arm (out along +x), 4 forearm, 5 hand, 6 left thigh, 7 calf.
constexpr int kBones = 8;
const float kAt[kBones][3] = {{0, 1.0f, 0}, {0, 1.3f, 0}, {0, 1.7f, 0}, {0.2f, 1.5f, 0}, {0.5f, 1.5f, 0}, {0.8f, 1.5f, 0}, {-0.1f, 1.0f, 0}, {-0.1f, 0.5f, 0}};
const int16_t kParent[kBones] = {-1, 0, 1, 1, 3, 4, 0, 6};

void Mats(float (*m)[16]) {
	for (int i = 0; i < kBones; ++i) {
		std::memset(m[i], 0, sizeof m[i]);
		m[i][0] = m[i][5] = m[i][10] = m[i][15] = 1.0f;
		for (int k = 0; k < 3; ++k) m[i][12 + k] = kAt[i][k];
	}
}

// The model in the world: model x m3 + t.
struct World {
	float m3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	float t[3] = {};
	void Point(const float p[3], float o[3]) const {
		for (int j = 0; j < 3; ++j) o[j] = p[0] * m3[j] + p[1] * m3[3 + j] + p[2] * m3[6 + j] + t[j];
	}
};

struct Built {
	dp::Rig::Cap caps[3];
	dp::Rig rig;
};
// The rig as the mod hands it over: the capsules in the world (the upper arm, the trunk, the thigh), the model's transform.
void Make(const World& w, Built* b) {
	const int bones[3][3] = {{3, 3, 4}, {1, 0, 2}, {6, 6, 7}};   // bone, from, to
	for (int i = 0; i < 3; ++i) {
		w.Point(kAt[bones[i][1]], b->caps[i].a);
		w.Point(kAt[bones[i][2]], b->caps[i].b);
		b->caps[i].bone = bones[i][0];
	}
	std::memcpy(b->rig.m3, w.m3, sizeof w.m3);
	std::memcpy(b->rig.root, w.t, sizeof w.t);
	b->rig.bones = kBones;
	b->rig.parent = kParent;
	b->rig.caps = b->caps;
	b->rig.nCaps = 3;
}

void WorldOf(const World& w, const float (*m)[16], int bone, float o[3]) { w.Point(&m[bone][12], o); }

void TestArm(const World& w, float upm, const float push[3], const char* what) {   // push: across the arm in the world, unit
	Built b;
	Make(w, &b);
	dp::LimbOff limb{3, {0.1f * push[0], 0.1f * push[1], 0.1f * push[2]}};   // the doll has the arm's far end 10 cm that way (render metres)
	dp::Pose p;
	EXPECT(dp::Build(b.rig, upm, nullptr, &limb, 1, &p) && p.n == 1, "%s: one turn", what);
	float m[kBones][16], before[kBones][16];
	Mats(m);
	std::memcpy(before, m, sizeof m);
	EXPECT(dp::Apply(p, m, kBones), "%s: applied", what);
	float e0[3], e1[3], h0[3], h1[3], s0[3], s1[3];
	WorldOf(w, before, 4, e0);
	WorldOf(w, m, 4, e1);
	WorldOf(w, before, 5, h0);
	WorldOf(w, m, 5, h1);
	WorldOf(w, before, 3, s0);
	WorldOf(w, m, 3, s1);
	const float dz = -((e1[0] - e0[0]) * push[0] + (e1[1] - e0[1]) * push[1] + (e1[2] - e0[2]) * push[2]) / upm;   // (negative: along the push)
	EXPECT(dz < -0.08f && dz > -0.11f, "%s: the elbow goes back with the doll: %.3f m", what, dz);
	const float dh = -((h1[0] - h0[0]) * push[0] + (h1[1] - h0[1]) * push[1] + (h1[2] - h0[2]) * push[2]) / upm;
	EXPECT(dh < 1.5f * dz, "%s: the hand with it, further: %.3f m", what, dh);
	EXPECT(std::fabs(s1[0] - s0[0]) + std::fabs(s1[1] - s0[1]) + std::fabs(s1[2] - s0[2]) < 1e-4f, "%s: the shoulder stays", what);
	for (int i : {0, 1, 2, 6, 7})
		EXPECT(std::memcmp(m[i], before[i], sizeof m[i]) == 0, "%s: bone %d untouched", what, i);
	for (int i = 3; i < 6; ++i) {   // still a rotation: unit rows, at right angles
		const float* r = m[i];
		for (int a = 0; a < 3; ++a) {
			const float len = std::sqrt(r[a * 4] * r[a * 4] + r[a * 4 + 1] * r[a * 4 + 1] + r[a * 4 + 2] * r[a * 4 + 2]);
			EXPECT(std::fabs(len - 1.0f) < 1e-4f, "%s: bone %d row %d length %f", what, i, a, len);
		}
		const float d01 = r[0] * r[4] + r[1] * r[5] + r[2] * r[6];
		EXPECT(std::fabs(d01) < 1e-4f, "%s: bone %d rows at right angles", what, i);
	}
}

void TestLean(const World& w, float upm, const char* what) {
	Built b;
	Make(w, &b);
	const float body[3] = {0.1f, 0.0f, 0.0f};   // the doll has the body 10 cm to +x
	dp::Pose p;
	EXPECT(dp::Build(b.rig, upm, body, nullptr, 0, &p) && p.n == 1 && p.t[0].pivot == -1, "%s: a lean", what);
	float m[kBones][16], before[kBones][16];
	Mats(m);
	std::memcpy(before, m, sizeof m);
	dp::Apply(p, m, kBones);
	float p0[3], p1[3], hd0[3], hd1[3], c0[3], c1[3];
	WorldOf(w, before, 0, p0);
	WorldOf(w, m, 0, p1);
	WorldOf(w, before, 2, hd0);
	WorldOf(w, m, 2, hd1);
	WorldOf(w, before, 7, c0);
	WorldOf(w, m, 7, c1);
	const float hips = (p1[0] - p0[0]) / upm, head = (hd1[0] - hd0[0]) / upm, knee = (c1[0] - c0[0]) / upm;
	EXPECT(hips > 0.08f && hips < 0.12f, "%s: the hips go with the body: %.3f m", what, hips);
	EXPECT(head > hips && knee < hips && knee > 0.0f, "%s: a lean about the feet (head %.3f, knee %.3f)", what, head, knee);
}

void TestEdges() {
	World w;
	Built b;
	Make(w, &b);
	dp::Pose p;
	dp::LimbOff none{3, {0, 0, 0}};
	const float still[3] = {0, 0, 0};
	EXPECT(!dp::Build(b.rig, 1.0f, still, &none, 1, &p), "no offset, no turn");
	float m[kBones][16], before[kBones][16];
	Mats(m);
	std::memcpy(before, m, sizeof m);
	dp::Apply(p, m, kBones);
	EXPECT(std::memcmp(m, before, sizeof m) == 0, "the pose untouched");
	dp::LimbOff along{3, {0.5f, 0, 0}};   // along the arm: no turn
	EXPECT(!dp::Build(b.rig, 1.0f, nullptr, &along, 1, &p), "along the limb: no turn");
	dp::LimbOff huge{3, {0, 0, -50.0f}};
	EXPECT(dp::Build(b.rig, 1.0f, nullptr, &huge, 1, &p) && std::fabs(p.t[0].angle) <= dp::kMaxAngle + 1e-6f, "held to kMaxAngle: %f", p.t[0].angle);
	EXPECT(!dp::Apply(p, m, kBones - 1), "a pose for another skeleton is not applied");
	dp::LimbOff unknown{42, {0, 0, -0.1f}};
	EXPECT(!dp::Build(b.rig, 1.0f, nullptr, &unknown, 1, &p), "a bone with no capsule: nothing");
}

// [weight] grab_bodies' chain: the upper arm and the forearm each given its line's absolute turn (where its far end points now, from its joint). The forearm is
// carried by the upper arm's turn first and turned from there - both end where their lines say.
void TestChainLines() {
	World w;
	Built b;
	const int bones[3][3] = {{3, 3, 4}, {4, 4, 5}, {1, 0, 2}};   // the upper arm, the forearm, the trunk
	for (int i = 0; i < 3; ++i) {
		w.Point(kAt[bones[i][1]], b.caps[i].a);
		w.Point(kAt[bones[i][2]], b.caps[i].b);
		b.caps[i].bone = bones[i][0];
	}
	std::memcpy(b.rig.m3, w.m3, sizeof w.m3);
	std::memcpy(b.rig.root, w.t, sizeof w.t);
	b.rig.bones = kBones;
	b.rig.parent = kParent;
	b.rig.caps = b.caps;
	b.rig.nCaps = 3;
	// the upper arm (0.3 m along +x) turned 20 degrees back (-z), the forearm's line 45 degrees back
	const float a1 = 20.0f * 3.14159265f / 180.0f, a2 = 45.0f * 3.14159265f / 180.0f;
	const dp::LimbOff limbs[2] = {{3, {0.3f * std::cos(a1) - 0.3f, 0.0f, -0.3f * std::sin(a1)}}, {4, {0.3f * std::cos(a2) - 0.3f, 0.0f, -0.3f * std::sin(a2)}}};
	dp::Pose p;
	EXPECT(dp::Build(b.rig, 1.0f, nullptr, limbs, 2, &p) && p.n == 2, "two turns");
	float m[kBones][16];
	Mats(m);
	dp::Apply(p, m, kBones);
	const float* s = &m[3][12];
	const float* e = &m[4][12];
	const float* h = &m[5][12];
	const float up[3] = {e[0] - s[0], e[1] - s[1], e[2] - s[2]}, fo[3] = {h[0] - e[0], h[1] - e[1], h[2] - e[2]};
	const float upAng = std::atan2(-up[2], up[0]) * 180.0f / 3.14159265f, foAng = std::atan2(-fo[2], fo[0]) * 180.0f / 3.14159265f;
	EXPECT(std::fabs(upAng - 20.0f) < 0.5f, "the upper arm 20 degrees back: %.2f", upAng);
	EXPECT(std::fabs(foAng - 45.0f) < 0.5f, "the forearm's line 45 degrees back, not 20 + 45: %.2f", foAng);
	EXPECT(std::fabs(std::sqrt(fo[0] * fo[0] + fo[1] * fo[1] + fo[2] * fo[2]) - 0.3f) < 1e-4f, "the forearm keeps its length");
}

// A HANGING arm: the shoulder above the elbow above the wrist - the wrist nearer the body's middle than the elbow (the old "nearer the middle" rule took the wrist
// for the forearm's joint and turned it the other way: the headset 0.183). Its far end pushed forward goes forward.
void TestHangingArm() {
	// bones: 0 pelvis, 1 spine, 2 head, 3 upper arm (the shoulder at 1.5), 4 forearm (the elbow at 1.2), 5 the hand (the wrist at 1.0, in by the hip), 6 thigh, 7 calf
	const float at[kBones][3] = {{0, 1.0f, 0}, {0, 1.3f, 0}, {0, 1.7f, 0}, {0.2f, 1.5f, 0}, {0.22f, 1.2f, 0}, {0.1f, 1.0f, 0}, {-0.1f, 1.0f, 0}, {-0.1f, 0.5f, 0}};
	Built b;
	const int caps[4][3] = {{3, 3, 4}, {4, 4, 5}, {1, 0, 2}, {6, 6, 7}};
	dp::Rig::Cap cc[4];
	for (int i = 0; i < 4; ++i) {
		for (int k = 0; k < 3; ++k) { cc[i].a[k] = at[caps[i][1]][k]; cc[i].b[k] = at[caps[i][2]][k]; }
		cc[i].bone = caps[i][0];
	}
	cc[1].a[0] = at[5][0]; cc[1].a[1] = at[5][1];   // (the forearm's capsule written wrist first: the order the model's box gives must not matter)
	cc[1].b[0] = at[4][0]; cc[1].b[1] = at[4][1];
	b.rig.bones = kBones;
	b.rig.parent = kParent;
	b.rig.caps = cc;
	b.rig.nCaps = 4;
	const dp::LimbOff fore{4, {0.0f, 0.05f, 0.15f}};   // the wrist pushed forward (+z), a little up
	dp::Pose p;
	EXPECT(dp::Build(b.rig, 1.0f, nullptr, &fore, 1, &p) && p.n == 1 && p.t[0].pivot == 4, "a turn about the elbow");
	float m[kBones][16];
	for (int i = 0; i < kBones; ++i) {
		std::memset(m[i], 0, sizeof m[i]);
		m[i][0] = m[i][5] = m[i][10] = m[i][15] = 1.0f;
		for (int k = 0; k < 3; ++k) m[i][12 + k] = at[i][k];
	}
	dp::Apply(p, m, kBones);
	EXPECT(m[5][14] > 0.1f, "the wrist went forward with the push: z %.3f", m[5][14]);
	EXPECT(std::fabs(m[4][12] - 0.22f) + std::fabs(m[4][13] - 1.2f) + std::fabs(m[4][14]) < 1e-5f, "the elbow stays");
}

}  // namespace

int main() {
	World plain;
	const float back[3] = {0.0f, 0.0f, -1.0f}, side[3] = {1.0f, 0.0f, 0.0f};
	TestArm(plain, 1.0f, back, "plain");
	TestLean(plain, 1.0f, "plain");
	World turned;   // the model turned 90 degrees about y, 4 world units a metre, standing elsewhere
	const float s = 4.0f;
	const float r[9] = {0, 0, -s, 0, s, 0, s, 0, 0};
	std::memcpy(turned.m3, r, sizeof r);
	turned.t[0] = 40.0f;
	turned.t[2] = -12.0f;
	TestArm(turned, s, side, "turned and scaled");   // (the arm runs along the world's -z there)
	TestLean(turned, s, "turned and scaled");
	World mirrored;   // x mirrored (a model the game draws flipped)
	mirrored.m3[0] = -1.0f;
	TestArm(mirrored, 1.0f, back, "mirrored");
	TestLean(mirrored, 1.0f, "mirrored");
	TestEdges();
	TestChainLines();
	TestHangingArm();
	std::printf("doll_pose_test: %s (%d failed)\n", g_fails ? "FAIL" : "ok", g_fails);
	return g_fails ? 1 : 0;
}
