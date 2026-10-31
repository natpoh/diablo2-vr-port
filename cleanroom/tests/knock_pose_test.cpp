// [weight] knock_down: a humanoid's drawn pose laid on the knock-down's joints (vr/knock_pose.h), its joints found by name (vr/monster_humanoid.h
// MapJoints), on a made-up skeleton.
//
// Checks: a Mixamo-named skeleton's joints are its bones by name (the clavicle no shoulder, the toe base a toe), a Biped's, the paladin's and the assassin's
// rigs map too, a rig with no knees does not; the joints read off the pose; laid on the same body lying on its back (turned and moved) every joint is where
// the target has it, within a millimetre - a finger and the weapon in the hand go with the hand, every bone's axes stay a proper rotation; an arm raised and
// a knee bent: those joints there, the rest unmoved; weight 0 leaves the pose bit for bit, 0.5 goes half the way; a NaN target is refused and the pose left;
// a leg of two bones has its ankle put at the farthest bone below the knee; the model's frame and the world's, turned, scaled and mirrored, round trip.

#include "../../vr/knock_pose.h"
#include "../../vr/monster_humanoid.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

namespace mh = monsterhumanoid;
namespace kp = knockpose;

struct Bone { const char* name; int parent; float at[3]; };
// A Mixamo-named man of 1.75 m in his model's frame (y up, facing +z, his left at -x), with the game's "anim" bone on the floor under him, a finger and a
// sword in the right hand.
const Bone kMan[] = {
	{"anim", -1, {0, 0, 0}},                      // 0
	{"Hips", 0, {0, 0.95f, 0}},                   // 1
	{"Spine", 1, {0, 1.10f, 0}},                  // 2
	{"Spine2", 2, {0, 1.30f, -0.02f}},            // 3
	{"Neck", 3, {0, 1.50f, -0.03f}},              // 4
	{"Head", 4, {0, 1.58f, 0}},                   // 5
	{"LeftShoulder", 3, {-0.06f, 1.44f, -0.03f}}, // 6
	{"LeftArm", 6, {-0.19f, 1.42f, -0.03f}},      // 7
	{"LeftForeArm", 7, {-0.22f, 1.14f, -0.05f}},  // 8
	{"LeftHand", 8, {-0.24f, 0.89f, 0}},          // 9
	{"LeftHandIndex1", 9, {-0.25f, 0.80f, 0.02f}},// 10
	{"RightShoulder", 3, {0.06f, 1.44f, -0.03f}}, // 11
	{"RightArm", 11, {0.19f, 1.42f, -0.03f}},     // 12
	{"RightForeArm", 12, {0.22f, 1.14f, -0.05f}}, // 13
	{"RightHand", 13, {0.24f, 0.89f, 0}},         // 14
	{"RightHandIndex1", 14, {0.25f, 0.80f, 0.02f}},// 15
	{"LeftUpLeg", 1, {-0.09f, 0.92f, 0}},         // 16
	{"LeftLeg", 16, {-0.10f, 0.50f, 0.02f}},      // 17
	{"LeftFoot", 17, {-0.10f, 0.08f, -0.02f}},    // 18
	{"LeftToeBase", 18, {-0.10f, 0.02f, 0.13f}},  // 19
	{"RightUpLeg", 1, {0.09f, 0.92f, 0}},         // 20
	{"RightLeg", 20, {0.10f, 0.50f, 0.02f}},      // 21
	{"RightFoot", 21, {0.10f, 0.08f, -0.02f}},    // 22
	{"RightToeBase", 22, {0.10f, 0.02f, 0.13f}},  // 23
	{"weapon_attach_R", 14, {0.30f, 0.85f, 0.10f}},// 24
};
constexpr int kN = static_cast<int>(sizeof kMan / sizeof kMan[0]);

mh::Verdict JudgeOf(const Bone* b, int n) {
	std::vector<const char*> names;
	std::vector<int> parent;
	for (int i = 0; i < n; ++i) { names.push_back(b[i].name); parent.push_back(b[i].parent); }
	return mh::Judge(names.data(), parent.data(), n);
}

// Each bone a turned frame (a different rotation each, as a real rig's), its origin where the table has it.
void Mats(const Bone* b, int n, float (*m)[16]) {
	for (int i = 0; i < n; ++i) {
		const float a = 0.37f * static_cast<float>(i), c = std::cos(a), s = std::sin(a);
		std::memset(m[i], 0, sizeof m[i]);
		// a rotation about the axis (1, 1, 0)/sqrt(2) by a (rows)
		const float x = 0.70710678f, y = 0.70710678f, z = 0.0f, k = 1.0f - c;
		const float r[9] = {c + x * x * k, x * y * k - z * s, x * z * k + y * s, y * x * k + z * s, c + y * y * k, y * z * k - x * s,
		                    z * x * k - y * s, z * y * k + x * s, c + z * z * k};
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 3; ++col) m[i][row * 4 + col] = r[row * 3 + col];
		for (int k2 = 0; k2 < 3; ++k2) m[i][12 + k2] = b[i].at[k2];
		m[i][15] = 1.0f;
	}
}

float Dist(const float* a, const float* b) {
	const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return std::sqrt(x * x + y * y + z * z);
}
// Worst |rows . rows - I| over the bones' axes.
float Unproper(const float (*m)[16], int n) {
	float worst = 0.0f;
	for (int b = 0; b < n; ++b)
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j) {
				const float d = m[b][i * 4] * m[b][j * 4] + m[b][i * 4 + 1] * m[b][j * 4 + 1] + m[b][i * 4 + 2] * m[b][j * 4 + 2];
				worst = std::max(worst, std::fabs(d - (i == j ? 1.0f : 0.0f)));
			}
	return worst;
}
// p in the frame of bone b (its axes as rows, its origin)
void InFrame(const float (*m)[16], int b, const float p[3], float out[3]) {
	const float d[3] = {p[0] - m[b][12], p[1] - m[b][13], p[2] - m[b][14]};
	for (int i = 0; i < 3; ++i) out[i] = d[0] * m[b][i * 4] + d[1] * m[b][i * 4 + 1] + d[2] * m[b][i * 4 + 2];
}

void TestMap() {
	const mh::Verdict v = JudgeOf(kMan, kN);
	EXPECT(v.humanoid && v.joints, "the man: %s / %s", v.why, v.jointsSaid);
	const int want[D2RF_JOINTS] = {1, 3, 4, 5, 7, 8, 9, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23};
	for (int j = 0; j < D2RF_JOINTS; ++j) EXPECT(v.joint[j] == want[j], "joint %d is bone %d, not %d (%s)", j, v.joint[j], want[j], v.jointsSaid);

	std::vector<std::string> keep;   // a Biped: the clavicles hang off the neck - the chest is the neck's bone, the head its own neck joint
	std::vector<const char*> names;
	std::vector<int> parent;
	const auto add = [&](const std::string& n, int p) { keep.push_back(n); parent.push_back(p); return static_cast<int>(keep.size()) - 1; };
	const int root = add("Bip01", -1), pelvis = add("Bip01 Pelvis", root), spine = add("Bip01 Spine", pelvis), spine1 = add("Bip01 Spine1", spine);
	const int neck = add("Bip01 Neck", spine1), head = add("Bip01 Head", neck);
	int up[2], th[2], ft[2];
	for (int s = 0; s < 2; ++s) {
		const std::string b = std::string("Bip01 ") + (s ? "R" : "L") + " ";
		const int cl = add(b + "Clavicle", neck);
		up[s] = add(b + "UpperArm", cl);
		const int fo = add(b + "Forearm", up[s]);
		add(b + "Hand", fo);
		th[s] = add(b + "Thigh", pelvis);
		const int ca = add(b + "Calf", th[s]);
		ft[s] = add(b + "Foot", ca);
		add(b + "Toe0", ft[s]);
	}
	for (const std::string& s : keep) names.push_back(s.c_str());
	mh::Verdict b = mh::Judge(names.data(), parent.data(), static_cast<int>(names.size()));
	EXPECT(b.joints && b.joint[D2RF_PELVIS] == pelvis && b.joint[D2RF_CHEST] == neck && b.joint[D2RF_NECK] == head && b.joint[D2RF_HEAD] == -1,
	       "a biped: %s", b.jointsSaid);
	EXPECT(b.joint[D2RF_L_SHOULDER] == up[0] && b.joint[D2RF_R_HIP] == th[1] && b.joint[D2RF_R_ANKLE] == ft[1], "a biped's limbs: %s", b.jointsSaid);

	const Bone asn[] = {   // the assassin's: hip_L_JNT is her thigh, shoulder / elbow / wrist
		{"pelvis_M_JNT", -1, {}}, {"spine_01_M_JNT", 0, {}}, {"head_M_JNT", 1, {}},
		{"shoulder_L_JNT", 1, {}}, {"elbow_L_JNT", 3, {}}, {"wrist_L_JNT", 4, {}},
		{"shoulder_R_JNT", 1, {}}, {"elbow_R_JNT", 6, {}}, {"wrist_R_JNT", 7, {}},
		{"hip_L_JNT", 0, {}}, {"knee_L_JNT", 9, {}}, {"ankle_L_JNT", 10, {}},
		{"hip_R_JNT", 0, {}}, {"knee_R_JNT", 12, {}}, {"ankle_R_JNT", 13, {}},
	};
	const mh::Verdict a = JudgeOf(asn, 15);
	EXPECT(a.joints && a.joint[D2RF_L_HIP] == 9 && a.joint[D2RF_L_KNEE] == 10 && a.joint[D2RF_L_ANKLE] == 11 && a.joint[D2RF_R_SHOULDER] == 6 &&
	       a.joint[D2RF_R_WRIST] == 8 && a.joint[D2RF_PELVIS] == 0 && a.joint[D2RF_CHEST] == 1 && a.joint[D2RF_NECK] == 2, "the assassin: %s", a.jointsSaid);

	const Bone pal[] = {   // the paladin's: R_upr_arm / R_lwr_arm / R_hand, R_upr_leg / R_lwr_leg / R_foot, the legs off the root
		{"root", -1, {}}, {"spine_01_bind_jnt", 0, {}}, {"spine_02_bind_jnt", 1, {}}, {"neck_bind_jnt", 2, {}}, {"head_bind_jnt", 3, {}},
		{"L_upr_arm_bind_jnt", 2, {}}, {"L_lwr_arm_bind_jnt", 5, {}}, {"L_hand_bind_jnt", 6, {}}, {"L_weapon_attach", 6, {}},
		{"R_upr_arm_bind_jnt", 2, {}}, {"R_lwr_arm_bind_jnt", 9, {}}, {"R_hand_bind_jnt", 10, {}}, {"R_weapon_attach", 10, {}},
		{"L_upr_leg_bind_jnt", 0, {}}, {"L_lwr_leg_bind_jnt", 13, {}}, {"L_foot_bind_jnt", 14, {}},
		{"R_upr_leg_bind_jnt", 0, {}}, {"R_lwr_leg_bind_jnt", 16, {}}, {"R_foot_bind_jnt", 17, {}},
	};
	const mh::Verdict p = JudgeOf(pal, 19);
	EXPECT(p.joints && p.joint[D2RF_PELVIS] == 0 && p.joint[D2RF_CHEST] == 2 && p.joint[D2RF_NECK] == 3 && p.joint[D2RF_HEAD] == 4 &&
	       p.joint[D2RF_R_WRIST] == 11 && p.joint[D2RF_L_ANKLE] == 15, "the paladin's: %s", p.jointsSaid);

	const Bone noKnee[] = {{"Hips", -1, {}}, {"Spine", 0, {}}, {"Head", 1, {}}, {"LeftArm", 1, {}}, {"LeftForeArm", 3, {}}, {"RightArm", 1, {}},
	                       {"RightForeArm", 5, {}}, {"LeftUpLeg", 0, {}}, {"LeftFoot_x", 0, {}}, {"RightUpLeg", 0, {}}};
	const mh::Verdict k = JudgeOf(noKnee, 10);
	EXPECT(!k.joints, "a rig with no knees has joints: %s", k.jointsSaid);
}

void TestApply() {
	const mh::Verdict v = JudgeOf(kMan, kN);
	float m[kN][16], orig[kN][16];
	Mats(kMan, kN, m);
	std::memcpy(orig, m, sizeof m);
	float j[D2RF_JOINTS][3];
	uint32_t have = 0;
	EXPECT(kp::JointsOf(v.joint, m, kN, j, &have, v.parent), "the joints do not read");
	EXPECT(have == (1u << D2RF_JOINTS) - 1, "have %08X", have);
	EXPECT(Dist(j[D2RF_L_KNEE], kMan[17].at) < 1e-6f && Dist(j[D2RF_HEAD], kMan[5].at) < 1e-6f, "joints off their bones");

	{   // lying on his back: the pose turned -90 deg about x (his head toward -z) and moved
		float t[D2RF_JOINTS][3];
		for (int i = 0; i < D2RF_JOINTS; ++i) {
			t[i][0] = j[i][0] + 0.3f;
			t[i][1] = j[i][2] + 0.12f;          // y' = z
			t[i][2] = -j[i][1] + 0.2f;          // z' = -y
		}
		float fingerInHand[3], swordInHand[3];
		InFrame(m, 14, kMan[15].at, fingerInHand);
		InFrame(m, 14, kMan[24].at, swordInHand);
		EXPECT(kp::Apply(m, kN, v.parent, v.joint, t, 1.0f), "not applied");
		float now[D2RF_JOINTS][3];
		kp::JointsOf(v.joint, m, kN, now, nullptr, v.parent);
		float worst = 0.0f;
		int at = -1;
		for (int i = 0; i < D2RF_JOINTS; ++i)
			if (Dist(now[i], t[i]) > worst) { worst = Dist(now[i], t[i]); at = i; }
		EXPECT(worst < 1e-3f, "lying: joint %d %.4f m off", at, worst);
		const float finger[3] = {m[15][12], m[15][13], m[15][14]}, sword[3] = {m[24][12], m[24][13], m[24][14]};
		float f2[3], s2[3];
		InFrame(m, 14, finger, f2);
		InFrame(m, 14, sword, s2);
		EXPECT(Dist(f2, fingerInHand) < 1e-4f && Dist(s2, swordInHand) < 1e-4f, "the finger %.5f, the sword %.5f off the hand", Dist(f2, fingerInHand), Dist(s2, swordInHand));
		EXPECT(Unproper(m, kN) < 1e-4f, "axes no rotation: %.6f", Unproper(m, kN));
		// the anim bone (0.95 under the hips) went with the body: lying, 0.95 past the hips toward his feet (+z)
		EXPECT(std::fabs(m[0][12] - 0.3f) < 1e-3f && std::fabs(m[0][13] - 0.12f) < 1e-3f && std::fabs(m[0][14] - 0.2f) < 1e-3f, "the anim bone at (%.3f %.3f %.3f)",
		       m[0][12], m[0][13], m[0][14]);
	}
	{   // an arm raised straight up and a knee bent: those joints there, the others where they were
		std::memcpy(m, orig, sizeof m);
		float t[D2RF_JOINTS][3];
		std::memcpy(t, j, sizeof t);
		const float upper = Dist(j[D2RF_L_SHOULDER], j[D2RF_L_ELBOW]), fore = Dist(j[D2RF_L_ELBOW], j[D2RF_L_WRIST]);
		t[D2RF_L_ELBOW][0] = j[D2RF_L_SHOULDER][0]; t[D2RF_L_ELBOW][1] = j[D2RF_L_SHOULDER][1] + upper; t[D2RF_L_ELBOW][2] = j[D2RF_L_SHOULDER][2];
		t[D2RF_L_WRIST][0] = t[D2RF_L_ELBOW][0]; t[D2RF_L_WRIST][1] = t[D2RF_L_ELBOW][1] + fore; t[D2RF_L_WRIST][2] = t[D2RF_L_ELBOW][2];
		const float shin = Dist(j[D2RF_R_KNEE], j[D2RF_R_ANKLE]), foot[3] = {j[D2RF_R_TOE][0] - j[D2RF_R_ANKLE][0], j[D2RF_R_TOE][1] - j[D2RF_R_ANKLE][1], j[D2RF_R_TOE][2] - j[D2RF_R_ANKLE][2]};
		t[D2RF_R_ANKLE][0] = j[D2RF_R_KNEE][0]; t[D2RF_R_ANKLE][1] = j[D2RF_R_KNEE][1]; t[D2RF_R_ANKLE][2] = j[D2RF_R_KNEE][2] - shin;   // the shin back, level
		for (int k = 0; k < 3; ++k) t[D2RF_R_TOE][k] = t[D2RF_R_ANKLE][k] + foot[k];
		EXPECT(kp::Apply(m, kN, v.parent, v.joint, t, 1.0f), "not applied");
		float now[D2RF_JOINTS][3];
		kp::JointsOf(v.joint, m, kN, now, nullptr, v.parent);
		float worst = 0.0f;
		int at = -1;
		for (int i = 0; i < D2RF_JOINTS; ++i)
			if (Dist(now[i], t[i]) > worst) { worst = Dist(now[i], t[i]); at = i; }
		EXPECT(worst < 1e-3f, "arm and knee: joint %d %.4f m off", at, worst);
	}
	{   // weight 0: bit for bit; 0.5: half the way; a NaN: refused, the pose left
		std::memcpy(m, orig, sizeof m);
		float t[D2RF_JOINTS][3];
		for (int i = 0; i < D2RF_JOINTS; ++i) { t[i][0] = j[i][0] + 1.0f; t[i][1] = j[i][1]; t[i][2] = j[i][2]; }
		EXPECT(kp::Apply(m, kN, v.parent, v.joint, t, 0.0f) && std::memcmp(m, orig, sizeof m) == 0, "weight 0 changed the pose");
		EXPECT(kp::Apply(m, kN, v.parent, v.joint, t, 0.5f) && std::fabs(m[1][12] - 0.5f) < 1e-4f, "half the way: hips at x %.4f", m[1][12]);
		std::memcpy(m, orig, sizeof m);
		t[3][1] = NAN;
		EXPECT(!kp::Apply(m, kN, v.parent, v.joint, t, 1.0f) && std::memcmp(m, orig, sizeof m) == 0, "a NaN target applied");
	}
	{   // the legs hang from a "root" on the floor (the paladin's rig): the pelvis's bone is the root, the pelvis joint the middle of the hips, not the floor
		Bone rooted[kN];
		std::memcpy(rooted, kMan, sizeof rooted);
		rooted[0].name = "root";
		rooted[16].parent = 0;
		rooted[20].parent = 0;
		const mh::Verdict r = JudgeOf(rooted, kN);
		EXPECT(r.joints && r.joint[D2RF_PELVIS] == 0, "the root rig: pelvis bone %d (%s)", r.joint[D2RF_PELVIS], r.jointsSaid);
		float mr[kN][16], jr[D2RF_JOINTS][3];
		Mats(rooted, kN, mr);
		EXPECT(kp::JointsOf(r.joint, mr, kN, jr, nullptr, r.parent) && std::fabs(jr[D2RF_PELVIS][1] - 0.92f) < 1e-4f && std::fabs(jr[D2RF_PELVIS][0]) < 1e-4f,
		       "the pelvis joint at (%.2f %.2f %.2f), not between the hips", jr[D2RF_PELVIS][0], jr[D2RF_PELVIS][1], jr[D2RF_PELVIS][2]);
	}
	{   // a leg of two bones ("leg_01", "leg_02") with a foot helper under the second: the ankle there
		Bone two[kN];
		std::memcpy(two, kMan, sizeof two);
		two[16].name = "L_leg_01"; two[17].name = "L_leg_02"; two[18].name = "aux_a"; two[19].name = "aux_b";   // (no limb's words: below the knee, no limb's)
		const mh::Verdict t2 = JudgeOf(two, kN);
		EXPECT(t2.joints && t2.joint[D2RF_L_HIP] == 16 && t2.joint[D2RF_L_KNEE] == 17 && t2.joint[D2RF_L_ANKLE] == -1, "two-bone leg: %s", t2.jointsSaid);
		float m2[kN][16], j2[D2RF_JOINTS][3];
		Mats(two, kN, m2);
		uint32_t h2 = 0;
		EXPECT(kp::JointsOf(t2.joint, m2, kN, j2, &h2, t2.parent) && !(h2 >> D2RF_L_ANKLE & 1u), "no joints");
		EXPECT(Dist(j2[D2RF_L_ANKLE], kMan[19].at) < 1e-5f, "the ankle put at (%.2f %.2f %.2f), not at the farthest bone below the knee", j2[D2RF_L_ANKLE][0], j2[D2RF_L_ANKLE][1], j2[D2RF_L_ANKLE][2]);
	}
}

// What hangs on a bone (the game's own entity: a monster's sword in its hand) carried with the bone as drawn: made by the game from the hand as the game has it
// (grip x hand x X), after Carry it is grip x the drawn hand x X - its grip in the drawn hand, under a turned, scaled, moved world transform.
void TestCarry() {
	const mh::Verdict v = JudgeOf(kMan, kN);
	float m[kN][16], game[kN][16];
	Mats(kMan, kN, m);
	std::memcpy(game, m, sizeof m);
	float j[D2RF_JOINTS][3], t[D2RF_JOINTS][3];
	kp::JointsOf(v.joint, m, kN, j, nullptr, v.parent);
	for (int i = 0; i < D2RF_JOINTS; ++i) { t[i][0] = j[i][0] + 0.3f; t[i][1] = j[i][2] + 0.12f; t[i][2] = -j[i][1] + 0.2f; }   // lying, as above
	EXPECT(kp::Apply(m, kN, v.parent, v.joint, t, 1.0f), "not applied");
	const float m3[9] = {0, 0, -2.0f, 0, 2.0f, 0, 2.0f, 0, 0}, root[3] = {10.0f, 2.0f, -4.0f};
	float x[16] = {}, grip[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0.02f, -0.05f, 0.1f, 1};   // the sword turned in the hand, 11 cm off its origin
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c) x[r * 4 + c] = m3[r * 3 + c];
	for (int c = 0; c < 3; ++c) x[12 + c] = root[c];
	x[15] = 1.0f;
	const auto boneM = [](const float* b, float* o) { std::memcpy(o, b, sizeof(float) * 16); o[3] = o[7] = o[11] = 0.0f; o[15] = 1.0f; };
	float bg[16], bd[16], t1[16], world[16], want[16], got[16];
	boneM(game[14], bg);
	boneM(m[14], bd);
	kp::detail::MulM(grip, bg, t1);
	kp::detail::MulM(t1, x, world);   // as the game made it
	kp::detail::MulM(grip, bd, t1);
	kp::detail::MulM(t1, x, want);    // in the drawn hand
	EXPECT(kp::Carry(world, game[14], m[14], m3, root, got), "not carried");
	float worst = 0.0f;
	for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(got[i] - want[i]));
	EXPECT(worst < 1e-3f, "the sword %.5f off the drawn hand's grip", worst);
	const float flat[9] = {};
	EXPECT(!kp::Carry(world, game[14], m[14], flat, root, got), "carried through a flat transform");
	// a capsule's end on the hand (the bodies the grab and the weapon meet, LayBodies): where the game has it -> on the drawn hand
	const float local[4] = {0.02f, -0.05f, 0.1f, 1.0f};
	const auto through = [](const float* p, const float* a, const float* b, float* o) {   // p x a x b
		float q[4];
		for (int c = 0; c < 4; ++c) q[c] = p[0] * a[c] + p[1] * a[4 + c] + p[2] * a[8 + c] + p[3] * a[12 + c];
		for (int c = 0; c < 3; ++c) o[c] = q[0] * b[c] + q[1] * b[4 + c] + q[2] * b[8 + c] + q[3] * b[12 + c];
	};
	float atGame[3], atDrawn[3], laid[3];
	through(local, bg, x, atGame);
	through(local, bd, x, atDrawn);
	EXPECT(kp::CarryPoint(atGame, game[14], m[14], m3, root, laid), "the point not carried");
	EXPECT(Dist(laid, atDrawn) < 1e-3f && Dist(atGame, atDrawn) > 0.1f, "the capsule's end %.5f off the drawn hand (it moved %.2f)", Dist(laid, atDrawn), Dist(atGame, atDrawn));
}

// Which way a body faces against the library's frame (Serg, 0.192.15: «колени не в ту сторону» - the act 1 monsters' skeletons are mirrored in the world):
// the man as he stands is not; his mirror image (his left on the world's right) is - by his toes, and by his knees alone without toe bones; swapping his
// sides makes him right again, twice is nothing.
void TestFacing() {
	const mh::Verdict v = JudgeOf(kMan, kN);
	float m[kN][16], j[D2RF_JOINTS][3];
	Mats(kMan, kN, m);
	uint32_t have = 0;
	EXPECT(kp::JointsOf(v.joint, m, kN, j, &have, v.parent), "no joints");
	// (the man is made in the library's own frame: his left at -x, facing +z - the replay hands him over as he is)
	EXPECT(kp::ToesForward(j, have) == 1, "the man's toes not forward");
	float mir[D2RF_JOINTS][3];
	std::memcpy(mir, j, sizeof mir);
	for (int i = 0; i < D2RF_JOINTS; ++i) mir[i][0] = -mir[i][0];   // his mirror image: his left on the other side
	EXPECT(kp::ToesForward(mir, have) == 0, "his mirror image's toes not backward");
	const uint32_t noToes = have & ~(1u << D2RF_L_TOE | 1u << D2RF_R_TOE);
	EXPECT(kp::KneesForward(j, noToes) == 1 && kp::KneesForward(mir, noToes) == 0, "by his knees alone: %d", kp::KneesForward(j, noToes));
	uint32_t h2 = have;
	kp::SwapSides(mir, &h2);
	EXPECT(kp::ToesForward(mir, h2) == 1 && h2 == have, "swapped sides still backward");
	// a body is swapped only when its model is drawn mirrored (0.192.16: by its toes the rogues' rig went back to front)
	const float turned[9] = {0, 0, -2.0f, 0, 2.0f, 0, 2.0f, 0, 0}, mirrored[9] = {-1.5f, 0, 0, 0, 1.5f, 0, 0, 0, 1.5f}, upright[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	EXPECT(!kp::Mirrored(turned) && !kp::Mirrored(upright) && kp::Mirrored(mirrored), "mirrored by the transform: turned %d, as is %d, mirrored %d",
	       kp::Mirrored(turned), kp::Mirrored(upright), kp::Mirrored(mirrored));
	// the game's own word: walked toward his front (+z) he goes with the library's front; his mirror image against it; sideways or standing: nothing
	const float ahead[3] = {0, 0, 1}, aside[3] = {1, 0, 0}, none[3] = {0, 0, 0};
	EXPECT(kp::FacesGoing(j, ahead) == 1 && kp::FacesGoing(mir, ahead) == 1 && kp::FacesGoing(j, aside) == 0 && kp::FacesGoing(j, none) == 0,
	       "facing by the going: %d %d %d %d", kp::FacesGoing(j, ahead), kp::FacesGoing(mir, ahead), kp::FacesGoing(j, aside), kp::FacesGoing(j, none));
	EXPECT(kp::SwapJoint(D2RF_L_WRIST, true) == D2RF_R_WRIST && kp::SwapJoint(D2RF_R_TOE, true) == D2RF_L_TOE && kp::SwapJoint(D2RF_CHEST, true) == D2RF_CHEST &&
	       kp::SwapJoint(D2RF_L_WRIST, false) == D2RF_L_WRIST, "a joint's other side");
}

void TestFrames() {
	const float turned[9] = {0, 0, -2.0f, 0, 2.0f, 0, 2.0f, 0, 0};   // a quarter turn about y, scale 2
	const float mirrored[9] = {-1.5f, 0, 0, 0, 1.5f, 0, 0, 0, 1.5f};
	const float root[3] = {10.0f, 2.0f, -4.0f};
	const float p[3] = {1.0f, 0.5f, -2.0f};
	for (const float* m3 : {turned, mirrored}) {
		float model[3], back[3];
		EXPECT(kp::ToModel(m3, root, 4.0f, p, model), "no model point");
		kp::ToWorld(m3, root, 4.0f, model, back);
		EXPECT(Dist(back, p) < 1e-5f, "round trip %.6f", Dist(back, p));
	}
	const float flat[9] = {};
	float o[3];
	EXPECT(!kp::ToModel(flat, root, 4.0f, p, o), "a flat transform inverted");
}

}  // namespace

int main() {
	TestMap();
	TestApply();
	TestCarry();
	TestFacing();
	TestFrames();
	std::printf(g_fails ? "knock_pose_test: %d FAILED\n" : "knock_pose_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
