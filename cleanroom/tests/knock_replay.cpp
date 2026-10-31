// [weight] knock_down replayed offline: the knock-down library (the real d2r_vr_weight.dll through vr/fall_lib.cpp) on a made-up humanoid skeleton,
// its drawn pose laid on the library's joints (vr/knock_pose.h) as the skinning's copy would be - every bone written for a film from the player's camera
// (tools/knock_down/knock_video.py). Also a check of the chain end to end: the body falls to the ground, lies, gets up, stands where the game's pose
// takes it back, its bones' lengths kept.
//
//   knock_replay <d2r_vr_weight.dll> <out.txt> [blow|behind|sweep|shove|stagger|throw|regrip|twohands|spear|impale] [mirrored]
//
// mirrored (Serg: «колени не в ту сторону» - the act 1 monsters): the man's mirror image (his left on the world's right), handed over as knock_down.cpp hands
// such a body - knockpose::Mirrored (its transform a mirror) says so, its sides swapped to the library and back. toesback: his toe bones behind his ankles
// (the rogues' rig, 0.192.16: by its toes it was swapped and drawn back to front - «одевает рагдол задом наперёд»): never swapped. Every replay checks his
// knees never bend backward while he stands, steps or walks (the knee forward of the line from the hip to the ankle, his own front), and that once he stands
// again he faces as the game has him (his hips' left-to-right as the game's: a body drawn back to front turns it round).
//
// stagger: a lighter blow - a step or two back, never down, home again. throw: the right hand takes him at the right wrist and the left at the left
// shoulder, lifts him 0.6 m, swings him toward the player's left and lets go - he flies, lands, lies, gets up and walks home.
// regrip (Serg: «я должен перехватиться или взять двумя руками»): the right hand takes his chest and lifts him off his feet; the left takes the left knee; the right
// lets go - he passes from hand to hand without falling or a jump; the left lets go and he falls. twohands: the right hand on the chest, the left on the
// left knee, lifted 0.6 m together, swung and let go together - thrown. spear (Serg: «я не могу схватить монстра за оружие»): the right hand takes his
// spear 0.8 m out in front of his right hand (held by that hand's limb - D2rfGrabPart) and pulls him toward the player: he comes with it, the hold a lever.
// impale ([weight] impale_drag, Serg: «с разгону врезаюсь с копьём в монстра ... насадить на копьё и протащить»): the player charges at him from the front,
// the spear's tip goes into his chest and carries him back 3 m/s for 1.2 s (as knock_down.cpp hands the library the drawn tip each step); the player stops,
// he slides off, falls, gets up and walks home.
//
// Without the library (no path, a file that is not it): says so and passes (the library is private; the public tree has no copy of it).

#include "../../vr/fall_lib.h"
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

struct Bone { const char* name; int parent; float at[3]; };
// A Mixamo-named man of 1.75 m standing at the world's origin facing +z (toward the player), his left at -x; the game's "anim" bone under him.
const Bone kMan[] = {
	{"anim", -1, {0, 0, 0}}, {"Hips", 0, {0, 0.95f, 0}}, {"Spine", 1, {0, 1.10f, 0}}, {"Spine2", 2, {0, 1.30f, -0.02f}}, {"Neck", 3, {0, 1.50f, -0.03f}},
	{"Head", 4, {0, 1.58f, 0}}, {"HeadTop_End", 5, {0, 1.78f, 0.02f}},
	{"LeftShoulder", 3, {-0.06f, 1.44f, -0.03f}}, {"LeftArm", 7, {-0.19f, 1.42f, -0.03f}}, {"LeftForeArm", 8, {-0.22f, 1.14f, -0.05f}}, {"LeftHand", 9, {-0.24f, 0.89f, 0}},
	{"LeftHandIndex1", 10, {-0.25f, 0.80f, 0.02f}},
	{"RightShoulder", 3, {0.06f, 1.44f, -0.03f}}, {"RightArm", 12, {0.19f, 1.42f, -0.03f}}, {"RightForeArm", 13, {0.22f, 1.14f, -0.05f}}, {"RightHand", 14, {0.24f, 0.89f, 0}},
	{"RightHandIndex1", 15, {0.25f, 0.80f, 0.02f}},
	{"LeftUpLeg", 1, {-0.09f, 0.92f, 0}}, {"LeftLeg", 17, {-0.10f, 0.50f, 0.02f}}, {"LeftFoot", 18, {-0.10f, 0.08f, -0.02f}}, {"LeftToeBase", 19, {-0.10f, 0.02f, 0.13f}},
	{"RightUpLeg", 1, {0.09f, 0.92f, 0}}, {"RightLeg", 21, {0.10f, 0.50f, 0.02f}}, {"RightFoot", 22, {0.10f, 0.08f, -0.02f}}, {"RightToeBase", 23, {0.10f, 0.02f, 0.13f}},
	{"weapon_attach_R", 15, {0.30f, 0.85f, 0.10f}},
};
constexpr int kN = static_cast<int>(sizeof kMan / sizeof kMan[0]);

bool g_mirror = false;     // (the mirrored replay: every bone's place mirrored in x)
bool g_toesBack = false;   // (toesback: the toe bones behind the ankles)

// The game's own pose at t: standing, breathing (the chest and the arms a little), each bone a turned frame of its own.
void GamePose(float t, float (*m)[16]) {
	for (int i = 0; i < kN; ++i) {
		std::memset(m[i], 0, sizeof m[i]);
		const float a = 0.37f * static_cast<float>(i), c = std::cos(a), s = std::sin(a);
		const float x = 0.70710678f, y = 0.70710678f, k = 1.0f - c;
		const float r[9] = {c + x * x * k, x * y * k, y * s, y * x * k, c + y * y * k, -x * s, -y * s, x * s, c};
		for (int row = 0; row < 3; ++row)
			for (int col = 0; col < 3; ++col) m[i][row * 4 + col] = r[row * 3 + col];
		for (int q = 0; q < 3; ++q) m[i][12 + q] = kMan[i].at[q];
		if (g_toesBack && (i == 20 || i == 24)) m[i][14] = -0.17f;
		if (g_mirror) m[i][12] = -m[i][12];
		if (i >= 7 && i != 17 && i < 25 && kMan[i].at[1] > 0.8f && i != 21) m[i][13] += 0.01f * std::sin(t * 2.0f);   // (the arms breathe)
		m[i][15] = 1.0f;
	}
}

}  // namespace

int main(int argc, char** argv) {
	const char* kind = argc > 3 ? argv[3] : "blow";
	g_mirror = argc > 4 && !std::strcmp(argv[4], "mirrored");
	g_toesBack = argc > 4 && !std::strcmp(argv[4], "toesback");
	if (argc < 3) {
		std::printf("knock_replay <d2r_vr_weight.dll> <out.txt> [blow|behind|sweep|shove]: no library given - nothing replayed\n");
		return 0;
	}
	std::wstring path;
	for (const char* p = argv[1]; *p; ++p) path += static_cast<wchar_t>(*p);
	falllib::SetPath(path.c_str());
	falllib::SetLog([](const char* s) { std::printf("  %s\n", s); });
	if (!falllib::Ensure()) {
		std::printf("knock_replay: no knock-down library at %s - nothing replayed\n", argv[1]);
		return 0;
	}
	std::vector<const char*> names;
	std::vector<int> parent;
	for (const Bone& b : kMan) { names.push_back(b.name); parent.push_back(b.parent); }
	const monsterhumanoid::Verdict v = monsterhumanoid::Judge(names.data(), parent.data(), kN);
	EXPECT(v.humanoid && v.joints, "not a humanoid with joints: %s / %s", v.why, v.jointsSaid);
	static float game[kN][16], drawn[kN][16];
	GamePose(0.0f, game);
	D2rfKnock k{};
	k.size = sizeof k;
	k.id = 77;
	uint32_t have = 0;
	EXPECT(knockpose::JointsOf(v.joint, game, kN, k.joint, &have, v.parent), "no joints");   // (the model's frame is the world's here: m3 = I, root 0, upm 1)
	k.have = have;
	// as knock_down.cpp decides it: the game walks him toward his front (+z, toward the player - the mirror image too); going against the library's front
	// his sides are swapped to the library and back (the replay's world is the library's: nothing mirrored in x on the way)
	const float way[3] = {0.0f, 0.0f, 1.0f};
	const int goes = knockpose::FacesGoing(k.joint, way);
	const bool swap = goes < 0;
	EXPECT(goes != 0 && swap == g_mirror, "the game's walking says %d - taken for %s", goes, swap ? "mirrored" : "not mirrored");
	const int ownToes = g_toesBack ? 1 : knockpose::ToesForward(k.joint, have);   // his own front against the library's frame by his labels (1: cross(R - L, up), 0: its opposite; toesback: his front as he is made)
	if (g_toesBack) EXPECT(knockpose::ToesForward(k.joint, have) == 0 && !swap, "toes behind: toes %d, swapped %d", knockpose::ToesForward(k.joint, have), swap);
	float worstFacing = 1.0f;   // once he stands again: his hips' left-to-right against the game's (1 the same way, -1 turned round)
	k.floorY = 0.0f;
	k.massKg = 50.0f;
	k.lieS = 1.5f;
	const int at = !std::strcmp(kind, "sweep") ? D2RF_L_ANKLE : D2RF_CHEST;
	const float push[3] = {!std::strcmp(kind, "sweep") ? 25.0f : 0.0f, 0.0f, !std::strcmp(kind, "sweep") ? 0.0f : !std::strcmp(kind, "behind") ? 40.0f : -40.0f};
	std::memcpy(k.impulse, push, sizeof push);
	std::memcpy(k.point, k.joint[at], sizeof k.point);
	k.kind = !std::strcmp(kind, "sweep") ? D2RF_SWEEP : !std::strcmp(kind, "shove") ? D2RF_SHOVE : D2RF_BLOW;
	const bool stagger = !std::strcmp(kind, "stagger"), regrip = !std::strcmp(kind, "regrip"), twohands = !std::strcmp(kind, "twohands"), spear = !std::strcmp(kind, "spear");
	const bool impale = !std::strcmp(kind, "impale");
	const bool thrown = !std::strcmp(kind, "throw") || twohands, held = thrown || regrip || spear || impale;
	if (stagger) {
		k.kind = D2RF_STAGGER;
		k.impulse[2] = -25.0f;
	}
	float handR[3], handL[3];
	std::memcpy(handR, k.joint[twohands || regrip ? D2RF_CHEST : D2RF_R_WRIST], sizeof handR);
	std::memcpy(handL, k.joint[regrip || twohands ? D2RF_L_KNEE : D2RF_L_SHOULDER], sizeof handL);
	if (spear) handR[2] += 0.8f;   // (his spear's shaft, 0.8 m out in front of his right hand)
	if (impale) { std::memcpy(handR, k.joint[D2RF_CHEST], sizeof handR); handR[2] += 0.05f; }   // (the tip 5 cm into his chest from the front)
	if (swap) knockpose::SwapSides(k.joint, &k.have);
	if (thrown) EXPECT(falllib::Grab(k, 0, handR) == 1 && falllib::Grab(k, 1, handL) == 1, "not taken");
	else if (regrip) EXPECT(falllib::Grab(k, 0, handR) == 1, "not taken");
	else if (spear) EXPECT(falllib::Grab(k, 0, handR, knockpose::SwapJoint(D2RF_R_WRIST, swap)) == 1, "not taken by the spear");
	else if (impale) EXPECT(falllib::Grab(k, 0, handR) == 1, "not impaled");
	else EXPECT(falllib::KnockDown(k) == 1, "not knocked down");
	float carriedOff = 0.0f, carriedHips = 0.0f;   // impale: the chest's farthest from the tip while carried, how far the hips went with it
	float lastHips[3] = {}, lastMove[3] = {}, worstJump = 0.0f, lever = -1.0f, leverWorst = 0.0f, spearPull = 0.0f;
	bool haveHips = false, droppedInPass = false;
	float worstKnee = 1.0f, worstKneeAt = 0.0f;   // his knees' bend toward his own front while he stands, steps or walks (m; < 0 backward)
	FILE* out = std::fopen(argv[2], "w");
	if (!out) { std::printf("cannot write %s\n", argv[2]); return 1; }
	std::fprintf(out, "# knock replay '%s': %d bones; the camera (eye, forward) the player's: 1.6 m in front, eyes at 1.65\n# parents", kind, kN);
	for (int i = 0; i < kN; ++i) std::fprintf(out, " %d", kMan[i].parent);
	std::fprintf(out, "\n# camera 0 1.65 1.6 0 -0.35 -1\n");
	const float dt = 1.0f / 30.0f;
	float lowestHips = 10.0f, standHead = 0.0f, worstStretch = 0.0f;
	bool lay = false, stood = false, back = false;
	float offBy[8] = {}, offAt[8] = {};   // by phase: the most a drawn joint was off the library's (the retarget keeps the skeleton's own proportions)
	int offJoint[8] = {};
	int phase = D2RF_FALLING;
	float farthest = 0.0f;
	for (int f = 0; f < 30 * (held ? 14 : 12); ++f) {   // (a walk home turns round first when it lies ahead of its place)
		const float t = f * dt;
		GamePose(t, game);
		std::memcpy(drawn, game, sizeof drawn);
		if (thrown && t < 1.6f) {   // the hands: up 0.6 m over 0.8 s, held, then swung to -x at 4 m/s for 0.25 s and let go
			const float up = 0.6f * std::fmin(1.0f, t / 0.8f), sw = t <= 1.35f ? 0.0f : twohands ? -12.0f * (t - 1.35f) * (t - 1.35f) : -4.0f * (t - 1.35f);   // (two hands: a swing that speeds up to 6 m/s)
			const float r[3] = {handR[0] + sw, handR[1] + up, handR[2]}, l[3] = {handL[0] + sw, handL[1] + up, handL[2]};
			if (t + dt >= 1.6f) { falllib::Release(77, 0); falllib::Release(77, 1); }
			else { falllib::Hand(77, 0, r); falllib::Hand(77, 1, l); }
		}
		if (regrip && t < 2.4f) {   // the right lifts 0.6 m over 0.8 s; at 1.0 s the left takes the left knee where it is drawn; at 1.4 s the right lets go; at 2.4 s the left
			const float up = 0.6f * std::fmin(1.0f, t / 0.8f);
			const float r[3] = {handR[0], handR[1] + up, handR[2]};
			static float knee[3];
			if (t < 1.4f) falllib::Hand(77, 0, r);
			if (std::fabs(t - 1.0f) < 0.5f * dt) {
				D2rfPose q[D2RF_MAX_DOWN];
				if (falllib::Poses(q, D2RF_MAX_DOWN) > 0) {
					if (swap) knockpose::SwapSides(q[0].joint, nullptr);
					std::memcpy(knee, q[0].joint[D2RF_L_KNEE], sizeof knee);
				}
				EXPECT(falllib::Grab(k, 1, knee) == 1, "the left hand did not take him while the right held him");
			}
			if (t > 1.0f) falllib::Hand(77, 1, knee);
			if (std::fabs(t - 1.4f) < 0.5f * dt) falllib::Release(77, 0);
			if (t + dt >= 2.4f) falllib::Release(77, 1);
		}
		if (impale && t < 1.4f) {   // charging: the tip goes back (-z) at 3 m/s from 0.1 s to 1.3 s; stopped - it slides off
			const float go = 3.0f * std::fmin(std::fmax(t - 0.1f, 0.0f), 1.2f);
			const float r[3] = {handR[0], handR[1], handR[2] - go};
			if (t + dt >= 1.4f) falllib::Release(77, 0);
			else falllib::Hand(77, 0, r);
		}
		if (spear && t < 1.6f) {   // the hand lifts the shaft 0.3 m over 0.5 s, then pulls toward the player at 2 m/s for 0.5 s, holds, lets go
			spearPull = t > 0.6f ? 2.0f * std::fmin(t - 0.6f, 0.5f) : 0.0f;
			const float r[3] = {handR[0], handR[1] + 0.3f * std::fmin(1.0f, t / 0.5f), handR[2] + spearPull};
			if (t + dt >= 1.6f) falllib::Release(77, 0);
			else falllib::Hand(77, 0, r);
		}
		for (int s = 0; s < 3; ++s) falllib::Step(dt / 3.0f);
		D2rfPose p[D2RF_MAX_DOWN];
		const int np = falllib::Poses(p, D2RF_MAX_DOWN);
		if (swap)
			for (int i = 0; i < np; ++i) knockpose::SwapSides(p[i].joint, nullptr);
		float weight = 0.0f;
		phase = 0;
		if (np > 0) {
			weight = p[0].weight;
			phase = p[0].phase;
			EXPECT(knockpose::Apply(drawn, kN, v.parent, v.joint, p[0].joint, p[0].weight), "not laid at %.2f s", t);
			if (p[0].weight > 0.999f) {   // the drawn joints where the library has them
				float got[D2RF_JOINTS][3];
				knockpose::JointsOf(v.joint, drawn, kN, got, nullptr, v.parent);
				float worst = 0.0f;
				int wj = -1;
				for (int j = 0; j < D2RF_JOINTS; ++j) {
					const float e = std::sqrt(std::pow(got[j][0] - p[0].joint[j][0], 2.0f) + std::pow(got[j][1] - p[0].joint[j][1], 2.0f) + std::pow(got[j][2] - p[0].joint[j][2], 2.0f));
					if (e > worst) { worst = e; wj = j; }
				}
				if (worst > offBy[p[0].phase]) { offBy[p[0].phase] = worst; offJoint[p[0].phase] = wj; offAt[p[0].phase] = t; }
			}
		}
		float hips = drawn[1][13], head = drawn[5][13];
		if (haveHips && (regrip || twohands) && phase == D2RF_HELD) {   // passed from hand to hand, lifted and swung together: no jump of the body
			// a jump: the frame's move unlike the last frame's (a swing, a fall move on smoothly; a snap from one hold to another does not)
			float move[3], jump = 0.0f;
			for (int q = 0; q < 3; ++q) { move[q] = drawn[1][12 + q] - lastHips[q]; jump += (move[q] - lastMove[q]) * (move[q] - lastMove[q]); }
			worstJump = std::fmax(worstJump, std::sqrt(jump));
			std::memcpy(lastMove, move, sizeof move);
		}
		if (regrip && t > 1.4f && t < 2.35f && phase != D2RF_HELD) droppedInPass = true;
		if (np > 0 && weight > 0.95f && phase == D2RF_BACK) {
			float got[D2RF_JOINTS][3], gm[D2RF_JOINTS][3];
			knockpose::JointsOf(v.joint, drawn, kN, got, nullptr, v.parent);
			knockpose::JointsOf(v.joint, game, kN, gm, nullptr, v.parent);
			float a[3], b[3], la = 0.0f, lb = 0.0f, d = 0.0f;
			for (int q : {0, 2}) { a[q] = got[D2RF_R_HIP][q] - got[D2RF_L_HIP][q]; b[q] = gm[D2RF_R_HIP][q] - gm[D2RF_L_HIP][q]; la += a[q] * a[q]; lb += b[q] * b[q]; d += a[q] * b[q]; }
			worstFacing = std::fmin(worstFacing, d / std::sqrt(std::fmax(la * lb, 1e-12f)));
		}
		if (np > 0 && weight > 0.5f && (phase == D2RF_BACK || phase == D2RF_WALKING || phase == D2RF_STAGGERING) && ownToes >= 0) {
			float got[D2RF_JOINTS][3], lat[3], up[3], fwd[3];
			knockpose::JointsOf(v.joint, drawn, kN, got, nullptr, v.parent);
			for (int q = 0; q < 3; ++q) { lat[q] = got[D2RF_R_HIP][q] - got[D2RF_L_HIP][q]; up[q] = got[D2RF_NECK][q] - got[D2RF_PELVIS][q]; }
			fwd[0] = lat[1] * up[2] - lat[2] * up[1];
			fwd[1] = lat[2] * up[0] - lat[0] * up[2];
			fwd[2] = lat[0] * up[1] - lat[1] * up[0];
			const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]) * (ownToes ? 1.0f : -1.0f);
			for (const int hip : {D2RF_L_HIP, D2RF_R_HIP}) {
				float ax[3], l2 = 0.0f, s = 0.0f, bend = 0.0f;
				for (int q = 0; q < 3; ++q) { ax[q] = got[hip + 2][q] - got[hip][q]; l2 += ax[q] * ax[q]; s += (got[hip + 1][q] - got[hip][q]) * ax[q]; }
				s /= std::fmax(l2, 1e-8f);
				for (int q = 0; q < 3; ++q) bend += (got[hip + 1][q] - (got[hip][q] + ax[q] * s)) * fwd[q] / fl;
				if (bend < worstKnee) { worstKnee = bend; worstKneeAt = t; }
			}
		}
		if (spear && t > 0.1f && t < 1.55f && np > 0) {   // the hold a lever: the shaft's point as far from the drawn wrist as when it was taken
			const float hand[3] = {handR[0], handR[1] + 0.3f * std::fmin(1.0f, t / 0.5f), handR[2] + spearPull};
			const float d = std::sqrt(std::pow(p[0].joint[D2RF_R_WRIST][0] - hand[0], 2.0f) + std::pow(p[0].joint[D2RF_R_WRIST][1] - hand[1], 2.0f) +
			                          std::pow(p[0].joint[D2RF_R_WRIST][2] - hand[2], 2.0f));
			if (lever < 0.0f) lever = d;
			leverWorst = std::fmax(leverWorst, std::fabs(d - lever));
		}
		if (impale && t > 0.3f && t < 1.35f && np > 0) {   // the chest on the tip while carried
			const float go = 3.0f * std::fmin(std::fmax(t - 0.1f, 0.0f), 1.2f);
			const float d = std::sqrt(std::pow(p[0].joint[D2RF_CHEST][0] - handR[0], 2.0f) + std::pow(p[0].joint[D2RF_CHEST][1] - handR[1], 2.0f) +
			                          std::pow(p[0].joint[D2RF_CHEST][2] - (handR[2] - go), 2.0f));
			carriedOff = std::fmax(carriedOff, d);
			carriedHips = std::fmax(carriedHips, -drawn[1][14]);
		}
		std::memcpy(lastHips, &drawn[1][12], sizeof lastHips);
		haveHips = true;
		farthest = std::fmax(farthest, std::sqrt(drawn[1][12] * drawn[1][12] + drawn[1][14] * drawn[1][14]));
		if (phase == D2RF_LYING) { lay = true; lowestHips = std::fmin(lowestHips, hips); }
		if (phase == D2RF_BACK && weight > 0.95f) { stood = true; standHead = head; }
		if (np == 0 && stood) back = true;
		for (int i = 1; i < kN; ++i) {   // every bone as long as the game has it
			const int pa = kMan[i].parent;
			const float dg = std::sqrt(std::pow(game[i][12] - game[pa][12], 2.0f) + std::pow(game[i][13] - game[pa][13], 2.0f) + std::pow(game[i][14] - game[pa][14], 2.0f));
			const float dd = std::sqrt(std::pow(drawn[i][12] - drawn[pa][12], 2.0f) + std::pow(drawn[i][13] - drawn[pa][13], 2.0f) + std::pow(drawn[i][14] - drawn[pa][14], 2.0f));
			worstStretch = std::fmax(worstStretch, std::fabs(dd - dg));
		}
		std::fprintf(out, "%.3f %d %.2f |", t, phase, weight);
		for (int i = 0; i < kN; ++i) std::fprintf(out, " %.4f %.4f %.4f", drawn[i][12], drawn[i][13], drawn[i][14]);
		std::fprintf(out, "\n");
	}
	std::fclose(out);
	std::printf("  lying: the hips %.2f m over the ground at the lowest; stood up: the head at %.2f m; the bones' lengths kept to %.4f m\n", lowestHips, standHead, worstStretch);
	std::printf("  the hips went %.2f m from where he stood at the farthest\n", farthest);
	std::printf("  his knees bent at least %+.3f m toward his front while he stood, stepped or walked (%.2f s)%s\n", worstKnee, worstKneeAt, g_mirror ? " - his mirror image" : "");
	EXPECT(worstKnee > -0.02f, "a knee bent %.3f m backward at %.2f s", worstKnee, worstKneeAt);
	std::printf("  back on his feet: his hips turned at most %.0f deg from the game's (%s)\n", std::acos(std::fmax(-1.0f, std::fmin(1.0f, worstFacing))) * 57.3f,
	            worstFacing > 0.7f ? "facing as the game has him" : "TURNED ROUND");
	EXPECT(worstFacing > 0.7f, "back on his feet he faces %.2f of the game's way - drawn back to front", worstFacing);
	if (stagger) EXPECT(!lay && farthest > 0.2f && farthest < 1.0f, "staggered: lay %d, %.2f m", lay, farthest);
	else if (!spear) EXPECT(lay && lowestHips < 0.35f, "never lay (hips %.2f)", lowestHips);
	if (thrown) EXPECT(farthest > 1.0f, "thrown only %.2f m", farthest);
	if (regrip || twohands) {
		std::printf("  held: the hips' move changed at most %.3f m from one frame (1/30 s) to the next while the hands had him%s\n", worstJump, regrip ? (droppedInPass ? ", DROPPED in the pass" : ", passed from hand to hand") : "");
		EXPECT(worstJump < 0.08f, "a jump of %.3f m in a frame", worstJump);   // (a swing of the hands at 6 m/s: 0.05; a snap from one hold to another: a frame's whole move)
		EXPECT(!droppedInPass, "dropped when the right hand let go");
	}
	if (impale) {
		std::printf("  impaled: carried %.2f m back on the tip, the chest at most %.2f m from it\n", carriedHips, carriedOff);
		EXPECT(carriedHips > 2.5f && carriedOff < 0.25f, "not carried on the tip (%.2f m, %.2f m off)", carriedHips, carriedOff);
	}
	if (spear) {
		std::printf("  spear: the shaft's point %.2f m from his wrist when taken, kept to %.3f m while pulled; the hips went %.2f m\n", lever, leverWorst, farthest);
		EXPECT(lever > 0.6f && leverWorst < 0.12f, "the hold not a lever (%.2f m, %.3f)", lever, leverWorst);
		EXPECT(farthest > 0.4f, "pulled by the spear only %.2f m", farthest);
	}
	EXPECT(stood && standHead > 1.4f, "never stood up (head %.2f)", standHead);
	EXPECT(back, "never went back to the game's pose");
	EXPECT(worstStretch < 1e-3f, "a bone stretched %.4f m", worstStretch);
	static const char* const kNames[8] = {"-", "falling", "lying", "getting up", "back", "held", "staggering", "walking home"};
	for (int ph = D2RF_FALLING; ph <= D2RF_WALKING; ++ph) {
		std::printf("  %s: the drawn joints at most %.3f m off the library's (joint %d at %.2f s)\n", kNames[ph], offBy[ph], offJoint[ph], offAt[ph]);
		EXPECT(offBy[ph] < 0.08f, "phase %d: %.3f m off", ph, offBy[ph]);
	}
	std::printf(g_fails ? "knock_replay '%s': %d FAILED\n" : "knock_replay '%s': all passed\n", kind, g_fails);
	return g_fails ? 1 : 0;
}
