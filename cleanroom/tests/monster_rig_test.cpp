// A monster's body from its own skeleton ([weight] monster_bodies; vr/monster_rig.h, vr/monster_bodies.h Assemble) on a made-up game memory:
// a Granny model instance, its skeleton's bones, two meshes with their bone bindings (the boxes), a pose - and the weight model's hold (hold.h)
// swinging a sword at the doll that comes out of them.
//
// Checks: the boxes are read by the skeleton's bone names and merged across meshes; bindings that name no bone, inside-out or endless boxes and a
// model that is not the skeleton's are refused; a box becomes a capsule along its longest side, carried by the bone's matrix (turned, scaled, moved);
// trifles (a finger) are left out and the largest kept; each hostile monster takes the skeleton standing where it stands, another monster keeps the
// game's capsule; and against the doll a thrust into the chest stops, a cut through the gap between its legs goes through - not one capsule round it all.
// A unit of parts: the body's boxes from its parts' models, the weapon's part left out.

#include "contact.h"
#include "hold.h"
#include "../../vr/monster_bodies.h"
#include "../../vr/monster_rig.h"
#include "../../vr/monster_humanoid.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace mr = monsterrig;
namespace mb = monsterbodies;

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

// A made-up memory: one block at kBase, written by Put, read by Read as the game's guarded reader does.
constexpr uintptr_t kBase = 0x10000000;
struct Memory {
	std::vector<uint8_t> bytes = std::vector<uint8_t>(1 << 16, 0);
	uintptr_t next = kBase + 0x100;
	uintptr_t Alloc(size_t n) {
		const uintptr_t at = next;
		next += (n + 15) & ~size_t(15);
		return at;
	}
	template <class T> void Put(uintptr_t at, const T& v) { std::memcpy(&bytes[at - kBase], &v, sizeof v); }
	void PutBytes(uintptr_t at, const void* p, size_t n) { std::memcpy(&bytes[at - kBase], p, n); }
	uintptr_t Str(const char* s) {
		const uintptr_t at = Alloc(std::strlen(s) + 1);
		PutBytes(at, s, std::strlen(s) + 1);
		return at;
	}
	bool Read(uintptr_t a, void* d, size_t n) const {
		if (a < kBase || a + n > kBase + bytes.size()) return false;
		std::memcpy(d, &bytes[a - kBase], n);
		return true;
	}
};

struct Box {
	const char* bone;
	float mn[3], mx[3];
};

// A humanoid of 1.8 m (model units = metres here): the bones and, in their own frames (each bone's y along it), the boxes of two meshes.
const char* const kBones[] = { "root", "pelvis", "spine", "head", "L_thigh", "R_thigh", "L_arm", "R_arm", "R_finger" };
constexpr int kBoneCount = 9;

struct Doll {
	Memory m;
	uintptr_t instance = 0, model = 0, skeleton = 0;
};

// The model: instance -> model -> skeleton; the model's meshes -> their bone bindings.
Doll MakeDoll(const std::vector<std::vector<Box>>& meshes, const char* const* boneNames = kBones, int boneCount = kBoneCount) {
	Doll d;
	Memory& m = d.m;
	d.skeleton = m.Alloc(0x20);
	const uintptr_t bones = m.Alloc(mr::kBoneStride * boneCount);
	m.Put<int32_t>(d.skeleton + mr::kSkelCount, boneCount);
	m.Put<uintptr_t>(d.skeleton + mr::kSkelBones, bones);
	for (int i = 0; i < boneCount; ++i) {
		m.Put<uintptr_t>(bones + i * mr::kBoneStride + mr::kBoneName, m.Str(boneNames[i]));
		m.Put<int32_t>(bones + i * mr::kBoneStride + mr::kBoneParent, i == 0 ? -1 : 0);
	}
	d.model = m.Alloc(0x60);
	d.instance = m.Alloc(0x20);
	m.Put<uintptr_t>(d.instance + grannymesh::kInstanceModel, d.model);
	m.Put<uintptr_t>(d.model + grannymesh::kModelSkeleton, d.skeleton);
	const uintptr_t list = m.Alloc(8 * meshes.size());
	m.Put<int32_t>(d.model + grannymesh::kModelMeshCount, (int32_t)meshes.size());
	m.Put<uintptr_t>(d.model + grannymesh::kModelMeshes, list);
	for (size_t k = 0; k < meshes.size(); ++k) {
		const uintptr_t mesh = m.Alloc(0x50);
		m.Put<uintptr_t>(list + 8 * k, mesh);
		m.Put<uintptr_t>(mesh + grannymesh::kMeshName, m.Str("body"));
		const uintptr_t binds = m.Alloc(mr::kBindingStride * meshes[k].size());
		m.Put<int32_t>(mesh + mr::kMeshBindingCount, (int32_t)meshes[k].size());
		m.Put<uintptr_t>(mesh + mr::kMeshBindings, binds);
		for (size_t j = 0; j < meshes[k].size(); ++j) {
			const uintptr_t at = binds + j * mr::kBindingStride;
			m.Put<uintptr_t>(at + mr::kBindingName, m.Str(meshes[k][j].bone));
			m.PutBytes(at + mr::kBindingMin, meshes[k][j].mn, 12);
			m.PutBytes(at + mr::kBindingMax, meshes[k][j].mx, 12);
		}
	}
	return d;
}

// The humanoid's two meshes: the body (pelvis, spine, head, the legs, the arms, a finger) and a cape over the spine.
std::vector<std::vector<Box>> Humanoid() {
	return {
		{ { "pelvis", { -0.16f, -0.10f, -0.12f }, { 0.16f, 0.10f, 0.12f } },
		  { "spine", { -0.17f, 0.0f, -0.12f }, { 0.17f, 0.55f, 0.12f } },
		  { "head", { -0.11f, 0.0f, -0.11f }, { 0.11f, 0.26f, 0.11f } },
		  { "L_thigh", { -0.08f, -0.85f, -0.08f }, { 0.08f, 0.0f, 0.08f } },
		  { "R_thigh", { -0.08f, -0.85f, -0.08f }, { 0.08f, 0.0f, 0.08f } },
		  { "L_arm", { -0.05f, -0.62f, -0.05f }, { 0.05f, 0.0f, 0.05f } },
		  { "R_arm", { -0.05f, -0.62f, -0.05f }, { 0.05f, 0.0f, 0.05f } },
		  { "R_finger", { -0.008f, -0.05f, -0.008f }, { 0.008f, 0.0f, 0.008f } } },
		{ { "spine", { -0.18f, 0.05f, -0.16f }, { 0.18f, 0.60f, 0.10f } } },   // the cape: the spine's box grows round both
	};
}

// The bones' matrices in the world (row vectors, translation in [12..14]): the doll standing at `at` (metres), turned `yaw` about +y, scaled by s.
void Pose(float at[3], float yaw, float s, float out[kBoneCount][16]) {
	const float bonePos[kBoneCount][3] = { { 0, 0, 0 },      { 0, 0.95f, 0 },  { 0, 1.05f, 0 },  { 0, 1.55f, 0 }, { -0.12f, 0.95f, 0 },
	                                       { 0.12f, 0.95f, 0 }, { -0.25f, 1.50f, 0 }, { 0.25f, 1.50f, 0 }, { 0.25f, 0.88f, 0 } };
	const float c = std::cos(yaw), sn = std::sin(yaw);
	for (int b = 0; b < kBoneCount; ++b) {
		float* m = out[b];
		std::memset(m, 0, 16 * sizeof(float));
		// rows: the bone's x, y, z axes in the world (turned about y, scaled), then its place
		m[0] = c * s; m[2] = -sn * s;
		m[5] = s;
		m[8] = sn * s; m[10] = c * s;
		const float p[3] = { bonePos[b][0] * s, bonePos[b][1] * s, bonePos[b][2] * s };
		m[12] = at[0] + p[0] * c + p[2] * sn;
		m[13] = at[1] + p[1];
		m[14] = at[2] - p[0] * sn + p[2] * c;
		m[15] = 1;
	}
}

struct Rig {   // as skel::MonsterRig has it
	float root[3] = {};
	int n = 0;
	mr::Capsule c[mr::kMaxPerRig];
	int doll = -1;   // [weight] monster_ragdoll: 1 a humanoid
	bool still = false;   // ... its pose held still: a corpse
	int nHeld = 0;   // [weight] monster_shields: what it holds (heldKind 1 a weapon, 2 a shield)
	mr::Capsule held[6];
	int heldKind[6] = {};
};

Rig MakeRig(const mr::Boxes& boxes, float at[3], float yaw, float s) {
	float mats[kBoneCount][16];
	Pose(at, yaw, s, mats);
	Rig r;
	std::memcpy(r.root, at, sizeof r.root);
	mr::Capsule caps[mr::kMaxBoxes];
	int n = 0;
	for (int i = 0; i < boxes.n; ++i)
		if (mr::CapsuleOf(mats[boxes.b[i].bone], boxes.b[i], &caps[n])) ++n;
	r.n = mr::Keep(caps, n);
	for (int i = 0; i < r.n; ++i) r.c[i] = caps[i];
	return r;
}

const mr::Capsule* ByBone(const Rig& r, int bone) {
	for (int i = 0; i < r.n; ++i)
		if (r.c[i].bone == bone) return &r.c[i];
	return nullptr;
}

void TestBoxes() {
	Doll d = MakeDoll(Humanoid());
	const auto read = [&](uintptr_t a, void* p, size_t n) { return d.m.Read(a, p, n); };
	mr::Boxes b;
	EXPECT(mr::BoxesOf(read, d.instance, d.skeleton, &b), "the boxes read: %s", b.why);
	EXPECT(b.n == 8 && b.bones == kBoneCount && b.meshes == 2, "8 bones with boxes, 9 bones, 2 meshes: %d %d %d", b.n, b.bones, b.meshes);
	for (int i = 0; i < b.n; ++i)
		if (b.b[i].bone == 2)   // the spine: the body's box and the cape's, one box round both
			EXPECT(b.b[i].min[0] == -0.18f && b.b[i].max[1] == 0.60f && b.b[i].min[2] == -0.16f && b.b[i].max[2] == 0.12f, "the spine's box merged");
	// refused: another skeleton's model, bindings that name no bone, inside-out and endless boxes
	mr::Boxes r;
	EXPECT(!mr::BoxesOf(read, d.instance, d.skeleton + 0x10, &r), "a model that is not this skeleton's is refused");
	Doll odd = MakeDoll({ { { "Bip01 Spine", { -0.1f, 0, -0.1f }, { 0.1f, 0.5f, 0.1f } }, { "Bip01 Head", { -0.1f, 0, -0.1f }, { 0.1f, 0.2f, 0.1f } } } });
	const auto readOdd = [&](uintptr_t a, void* p, size_t n) { return odd.m.Read(a, p, n); };
	EXPECT(!mr::BoxesOf(readOdd, odd.instance, odd.skeleton, &r), "bindings that name no bone of the skeleton are refused (%s)", r.why);
	Doll bad = MakeDoll({ { { "spine", { 0.1f, 0, 0 }, { -0.1f, 0.5f, 0.1f } }, { "head", { -0.1f, 0, -0.1f }, { 0.1f, 1e6f, 0.1f } } } });
	const auto readBad = [&](uintptr_t a, void* p, size_t n) { return bad.m.Read(a, p, n); };
	EXPECT(!mr::BoxesOf(readBad, bad.instance, bad.skeleton, &r), "inside-out and endless boxes are refused (%s)", r.why);
}

// Every limb with mesh weight gets its capsule: the author's screenshot 2026-10-24 (an NPC in town, npc_bodies) - no capsule on the LEFT leg below the knee,
// the right one had it; the log: Kashya "24 limbs (of 96 bones, 64 with a box)". The boxes stopped at 64 in the meshes' binding order, and then the largest
// 24 were kept with no regard for the other side. Now every boxed bone is read, and a left and right pair is kept or left out together.
void TestEveryLimb() {
	// the sides by name
	EXPECT(mr::Mirrored("Bip01 L Calf", "Bip01 R Calf") && mr::Mirrored("l_shin", "r_shin") && mr::Mirrored("LeftUpLeg", "RightUpLeg") && mr::Mirrored("arm.l", "arm.r") &&
	       mr::Mirrored("Bip01 R Foot", "Bip01 L Foot"),
	       "the left and right of a bone by its name");
	EXPECT(!mr::Mirrored("Bip01 L Calf", "Bip01 L Calf") && !mr::Mirrored("Bip01 L Calf", "Bip01 R Thigh") && !mr::Mirrored("Bip01 Spine", "Bip01 Spine1") &&
	       !mr::Mirrored("cloak", "cloak"),
	       "not the same side, not another bone, not a bone of no side");
	// a model of 84 bones: 60 cape and skirt bones bound first, then the body - the right calf the 61st binding, the left calf after the 64th
	static char names[84][24];
	const char* ptrs[84];
	std::vector<Box> mesh;
	int n = 0;
	const auto bone = [&](const char* nm) { std::snprintf(names[n], sizeof names[n], "%s", nm); ptrs[n] = names[n]; return n++; };
	bone("root");
	for (int i = 0; i < 60; ++i) {
		char nm[24];
		std::snprintf(nm, sizeof nm, "Cape%02d", i);
		bone(nm);
		mesh.push_back({ ptrs[n - 1], { -0.04f, -0.12f, -0.01f }, { 0.04f, 0.0f, 0.01f } });
	}
	const char* body[] = { "Bip01 R Calf", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Head", "Bip01 L Calf", "Bip01 L Thigh", "Bip01 R Thigh", "Bip01 L Foot", "Bip01 R Foot",
	                       "Bip01 L UpperArm", "Bip01 R UpperArm", "Bip01 L Forearm", "Bip01 R Forearm", "Bip01 L Hand", "Bip01 R Hand", "Bip01 Neck" };
	const float size[] = { 0.06f, 0.15f, 0.17f, 0.11f, 0.06f, 0.08f, 0.08f, 0.05f, 0.05f, 0.05f, 0.05f, 0.04f, 0.04f, 0.035f, 0.035f, 0.05f };
	for (int i = 0; i < 16; ++i) {
		bone(body[i]);
		mesh.push_back({ ptrs[n - 1], { -size[i], -0.45f, -size[i] }, { size[i], 0.0f, size[i] } });
	}
	while (n < 84) bone("Bip01 Spare");
	Doll d = MakeDoll({ mesh }, ptrs, n);
	const auto read = [&](uintptr_t a, void* p, size_t sz) { return d.m.Read(a, p, sz); };
	mr::Boxes b;
	EXPECT(mr::BoxesOf(read, d.instance, d.skeleton, &b), "the boxes read: %s", b.why);
	int lCalf = -1, rCalf = -1;
	for (int i = 0; i < b.n; ++i) {
		if (std::strcmp(ptrs[b.b[i].bone], "Bip01 L Calf") == 0) lCalf = i;
		if (std::strcmp(ptrs[b.b[i].bone], "Bip01 R Calf") == 0) rCalf = i;
	}
	std::printf("  a model of %d bones, %d with a box (60 cape bones bound first): the left calf %s, the right %s\n", b.bones, b.n, lCalf >= 0 ? "boxed" : "MISSING",
	            rCalf >= 0 ? "boxed" : "MISSING");
	EXPECT(b.n == 76 && lCalf >= 0 && rCalf >= 0, "every boxed bone read - not the first 64 (%d; left calf %d, right %d)", b.n, lCalf, rCalf);
	if (lCalf >= 0 && rCalf >= 0)
		EXPECT(b.b[lCalf].mirror == b.b[rCalf].bone && b.b[rCalf].mirror == b.b[lCalf].bone, "each calf knows the other");
	// Keep at the 24 limit: a pair straddling it is kept or left out together
	const auto cap = [](int bone, int mirror, float r) {
		mr::Capsule c;
		c.b[1] = 1.0f;
		c.radius = r;
		c.bone = bone;
		c.mirror = mirror;
		return c;
	};
	const auto kept = [](const mr::Capsule* c, int k, int bone) {
		for (int i = 0; i < k; ++i)
			if (c[i].bone == bone) return true;
		return false;
	};
	{   // 22 larger singles, then a calf pair at 23rd and 24th by size - both fit; the rest smaller
		std::vector<mr::Capsule> cs;
		for (int i = 0; i < 22; ++i) cs.push_back(cap(100 + i, -1, 0.20f - 0.002f * i));
		cs.push_back(cap(1, 2, 0.150f));   // the right calf
		cs.push_back(cap(2, 1, 0.149f));   // the left calf
		for (int i = 0; i < 8; ++i) cs.push_back(cap(200 + i, -1, 0.10f - 0.001f * i));
		const int k = mr::Keep(cs.data(), (int)cs.size(), 2.0f);
		EXPECT(k == mr::kMaxPerRig && kept(cs.data(), k, 1) && kept(cs.data(), k, 2), "the pair at the limit kept together (%d)", k);
	}
	{   // 23 larger singles, then the pair: no room for both - neither, a single takes the last place (before, the right calf stayed and the left went)
		std::vector<mr::Capsule> cs;
		for (int i = 0; i < 23; ++i) cs.push_back(cap(100 + i, -1, 0.20f - 0.002f * i));
		cs.push_back(cap(1, 2, 0.150f));
		cs.push_back(cap(2, 1, 0.149f));
		for (int i = 0; i < 8; ++i) cs.push_back(cap(200 + i, -1, 0.10f - 0.001f * i));
		const int k = mr::Keep(cs.data(), (int)cs.size(), 2.0f);
		EXPECT(k == mr::kMaxPerRig && kept(cs.data(), k, 1) == kept(cs.data(), k, 2) && kept(cs.data(), k, 200), "no pair split at the limit (right %d, left %d, %d kept)",
		       kept(cs.data(), k, 1) ? 1 : 0, kept(cs.data(), k, 2) ? 1 : 0, k);
	}
}

// No extra limbs: the author's screenshot 2026-10-24 - a zombie with two capsules floating beside it at the hips and a chain of small ones sticking out
// sideways from its right shoulder like a rope. A box not at its bone is left out; of a humanoid, only its body's bones (its limbs, the trunk, the head,
// the fingers and toes) - not a rope or a strip of cloth branching off it.
void TestNoExtraLimbs() {
	mr::BoneBox limb;   // a forearm's box: from its joint along -y
	limb.min[0] = -0.05f; limb.min[1] = -0.30f; limb.min[2] = -0.05f;
	limb.max[0] = 0.05f; limb.max[1] = 0.0f; limb.max[2] = 0.05f;
	mr::BoneBox far = limb;   // the same box 80 cm off its bone (made in another frame)
	far.min[0] += 0.8f; far.max[0] += 0.8f;
	EXPECT(mr::AtItsBone(limb) && !mr::AtItsBone(far), "a limb's box at its bone, one 80 cm off it is not");
	// a humanoid: 0 root, 1 pelvis, 2 spine, 3 neck, 4 head, 5 jaw; arms 6 L clav, 7 L upper, 8 L fore, 9 L hand, 10 L finger, 11 R clav, 12 R upper, 13 R fore,
	// 14 R hand; legs 15 L thigh, 16 L calf, 17 L foot, 18 L toe, 19 R thigh, 20 R calf, 21 R foot; a rope off the right clavicle 22-25; a belly strip off the spine 26
	const int16_t parent[] = { -1, 0, 1, 2, 3, 4, 2, 6, 7, 8, 9, 2, 11, 12, 13, 1, 15, 16, 17, 1, 19, 20, 11, 22, 23, 24, 2 };
	const int n = (int)(sizeof parent / sizeof parent[0]);
	int16_t limbOf[64] = {};
	for (int b : { 7, 8, 9 }) limbOf[b] = 1 + 7;
	for (int b : { 12, 13, 14 }) limbOf[b] = 1 + 12;
	for (int b : { 15, 16, 17 }) limbOf[b] = 1 + 15;
	for (int b : { 19, 20, 21 }) limbOf[b] = 1 + 19;
	bool body[mr::kMaxBones];
	mr::BodyBones(parent, limbOf, n, 4, body);
	char line[128] = "";
	int w = 0;
	for (int i = 0; i < n; ++i) w += std::snprintf(line + w, sizeof line - w, "%d", body[i] ? 1 : 0);
	std::printf("  the body's bones of a humanoid with a rope off its shoulder and a strip off its spine: %s\n", line);
	bool ok = true;
	for (int b : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 }) ok = ok && body[b];
	EXPECT(ok, "the trunk, the head and the jaw, both arms with the clavicles, both legs, a finger and a toe are the body");
	EXPECT(!body[22] && !body[23] && !body[24] && !body[25] && !body[26], "the rope off the shoulder and the strip off the spine are not");
	// The headset 2026-10-24 (f3021522): the zombie lost its head ("head_bind_jnt", "neck_bind_jnt" left out - the judge's head was another bone named "head"),
	// the quill rat its tail. By names, through the judge: a head-named helper off the root first, the trunk, the neck and the head, both arms and legs,
	// a rope off the right shoulder, a tail off the pelvis, two quills - the head, the neck and the tail stay; the rope and the quills go.
	const char* names[] = { "root", "head_look_target_jnt", "pelvis_bind_jnt", "spine_01_bind_jnt", "neck_bind_jnt", "head_bind_jnt", "jaw_bind_jnt",
	                        "L_shoulder_bind_jnt", "L_arm_01_bind_jnt", "L_arm_02_bind_jnt", "L_hand_bind_jnt", "R_shoulder_bind_jnt", "R_arm_01_bind_jnt",
	                        "R_arm_02_bind_jnt", "R_hand_bind_jnt", "L_leg_01_bind_jnt", "L_leg_02_bind_jnt", "L_foot_bind_jnt", "R_leg_01_bind_jnt",
	                        "R_leg_02_bind_jnt", "R_foot_bind_jnt", "R_rope_01_jnt", "R_rope_02_jnt", "R_rope_03_jnt", "tail_01_jnt", "tail_02_jnt", "tail_03_jnt",
	                        "spike_01_jnt", "spike_02_jnt" };
	const int par[] = { -1, 0, 0, 2, 3, 4, 5, 3, 7, 8, 9, 3, 11, 12, 13, 2, 15, 16, 2, 18, 19, 11, 21, 22, 2, 24, 25, 3, 3 };
	const int nn = (int)(sizeof par / sizeof par[0]);
	const monsterhumanoid::Verdict v = monsterhumanoid::Judge(names, par, nn);
	bool b2[mr::kMaxBones];
	mr::BodyBones(v.parent, v.limbOf, nn, v.headBone, b2, v.bodyWord);
	std::printf("  by names (the judge's head: '%s'): head %d, neck %d, jaw %d, tail %d %d %d, the rope %d %d %d, the quills %d %d\n", v.headBone >= 0 ? names[v.headBone] : "none",
	            b2[5], b2[4], b2[6], b2[24], b2[25], b2[26], b2[21], b2[22], b2[23], b2[27], b2[28]);
	EXPECT(v.humanoid, "judged a humanoid (%s)", v.why);
	EXPECT(b2[5] && b2[4] && b2[6] && b2[3] && b2[2], "the head, the neck, the jaw and the trunk stay whichever bone the judge took for the head");
	EXPECT(b2[24] && b2[25] && b2[26], "the tail stays");
	EXPECT(!b2[21] && !b2[22] && !b2[23] && !b2[27] && !b2[28], "the rope off the shoulder and the quills go");
}

// [weight] monster_shields: what a monster holds - a shield four capsules across its face, a weapon one along it; in the bodies after its limbs, of their
// own kind, never a doll; drawn violet and orange.
void TestHeld() {
	float bone[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // the bone at the origin, unturned (model units = metres)
	mr::BoneBox shield;   // a round shield 60 cm across, 4 cm thick, its face across x and y
	shield.bone = 9;
	shield.min[0] = -0.30f; shield.min[1] = -0.30f; shield.min[2] = -0.02f;
	shield.max[0] = 0.30f; shield.max[1] = 0.30f; shield.max[2] = 0.02f;
	mr::Capsule sc[4];
	const int ns = mr::HeldCapsules(bone, shield, true, sc, 4);
	float lo = 1e9f, hi = -1e9f, thick = 0;
	for (int i = 0; i < ns; ++i) {
		lo = std::min({ lo, sc[i].a[0] - sc[i].radius, sc[i].a[1] - sc[i].radius });
		hi = std::max({ hi, sc[i].b[0] + sc[i].radius, sc[i].b[1] + sc[i].radius });
		thick = std::max(thick, sc[i].radius);
	}
	std::printf("  a shield 60 cm across, 4 cm thick: %d capsules, %.0f cm thick each, covering %.0f..%.0f cm\n", ns, thick * 200, lo * 100, hi * 100);
	EXPECT(ns == 4 && lo <= -0.28f && hi >= 0.28f && thick < 0.08f, "four across the face, covering it, thinner than a body (%d, %.2f..%.2f, r %.2f)", ns, lo, hi, thick);
	mr::BoneBox blade;   // a sword's box along y
	blade.bone = 10;
	blade.min[0] = -0.02f; blade.min[1] = -0.05f; blade.min[2] = -0.01f;
	blade.max[0] = 0.02f; blade.max[1] = 0.95f; blade.max[2] = 0.01f;
	mr::Capsule wc[3];
	EXPECT(mr::HeldCapsules(bone, blade, false, wc, 3) == 1 && std::fabs(wc[0].b[1] - wc[0].a[1]) > 0.9f, "a weapon one capsule along its blade");
	// in the bodies, after its limbs, of their kind, no doll
	Doll d = MakeDoll(Humanoid());
	const auto read = [&](uintptr_t a, void* p, size_t n) { return d.m.Read(a, p, n); };
	mr::Boxes b;
	mr::BoxesOf(read, d.instance, d.skeleton, &b);
	float at[3] = { 8.0f, 0.0f, -12.0f };
	Rig rigs[1] = { MakeRig(b, at, 0.0f, 4.0f) };
	rigs[0].doll = 1;
	for (int i = 0; i < ns; ++i) { rigs[0].held[i] = sc[i]; rigs[0].heldKind[i] = 2; }
	rigs[0].held[ns] = wc[0];
	rigs[0].heldKind[ns] = 1;
	rigs[0].nHeld = ns + 1;
	mb::Body m;
	m.id = 11;
	m.bottom[0] = m.top[0] = 2.0f; m.bottom[2] = m.top[2] = -3.0f; m.top[1] = 1.5f; m.radius = 0.35f;
	mb::Snapshot snap;
	mb::Assemble(&m, 1, rigs, 1, 4.0f, &snap);
	int shields = 0, weapons = 0, dollHeld = 0;
	for (int i = 0; i < snap.n; ++i) {
		if (snap.b[i].held == 2) ++shields;
		if (snap.b[i].held == 1) ++weapons;
		if (snap.b[i].held && snap.b[i].doll) ++dollHeld;
	}
	EXPECT(shields == 4 && weapons == 1 && dollHeld == 0 && snap.n == rigs[0].n + 5, "its shield and its weapon among its bodies, of their kind, no doll (%d, %d, %d)", shields, weapons, dollHeld);
	snap.upm = 4.0f;
	const float eye[3] = { 0, 0, 0 };
	float pa[mb::kMax][4], pb[mb::kMax][4];
	const int k = mb::Pack(snap, eye, 0, -1, pa, pb, mb::kMax);
	int violet = 0, orange = 0;
	for (int i = 0; i < k; ++i) {
		if (pb[i][3] == (float)mb::kKindShield) ++violet;
		if (pb[i][3] == (float)mb::kKindWeapon) ++orange;
	}
	EXPECT(violet == 4 && orange == 1, "drawn violet and orange (%d, %d)", violet, orange);
}

// [weight] monster_shields as the game builds it (fallen1.json): the unit's root has a torso part with a model, an "rhand" part with NO model whose child
// is the item's own unit root whose child is the item's model (fallen_scimitar.json: a root and a model entity, no skeleton), and a shield part with its
// model under it at once. HeldModels finds the scimitar two levels down (RH) and the shield (SH), not the torso; ItemBox is the union of the item's boxes.
// Where a held item is: its world matrix, not its local one (the user's 0.179 run: 'navi.skeleton's bow "3556.0 m from the monster" - the local matrix,
// against its hand, read as the world's).
void TestHeldSpace() {
	const float pos[3] = { 3000.0f, 0.0f, 1800.0f };   // the monster, far from the world's origin
	float local[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.02f, 0.10f, 0.01f, 1 };   // against its hand: a few cm
	float world[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3000.3f, 1.1f, 1800.2f, 1 };
	float w = 0, l = 0;
	EXPECT(mr::HeldSpace(local, world, false, pos, 2.0f, &w, &l) == 0 && w < 2.0f && l > 3000.0f, "held: the world matrix (%.1f), not the local one (%.0f off)", w, l);
	EXPECT(mr::HeldSpace(local, nullptr, false, pos, 2.0f, &w, &l) == -1 && w < 0, "no world matrix and a parent: left out, never the local one");
	float rootedLocal[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3000.5f, 0.5f, 1800.0f, 1 };
	EXPECT(mr::HeldSpace(rootedLocal, nullptr, true, pos, 2.0f, &w, &l) == 1, "no parent: its local matrix is the world's");
	float far[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3050.0f, 0.0f, 1800.0f, 1 };
	EXPECT(mr::HeldSpace(local, far, false, pos, 2.0f, &w, &l) == -1, "a world matrix 50 away: left out");
	float nan[16] = { NAN, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, NAN, 0, 0, 1 };
	EXPECT(mr::HeldSpace(local, nan, false, pos, 2.0f, &w, &l) == -1 && w < 0, "a world matrix that does not read: left out");
	mr::HeldSpace(local, world, false, pos, 2.0f, &w, &l);
	std::printf("  a held item: by its world matrix (%.2f off), its local one against the hand would put it %.0f off\n", w, l);
}

void TestHeldModels() {
	Memory m;
	// entities: 1 root, 2 torso (part 1, model), 3 rhand (part RH, no model), 4 the scimitar's root, 5 the scimitar's model, 6 shield part (SH), 7 its model
	const auto childList = [&](std::vector<uint32_t> ids) {
		const uintptr_t comp = m.Alloc(mr::kChildrenCompSize), data = m.Alloc(4 * ids.size() + 4);
		for (size_t i = 0; i < ids.size(); ++i) m.Put<uint32_t>(data + 4 * i, ids[i]);
		m.Put<uintptr_t>(comp + mr::kChildrenData, data);
		m.Put<uint64_t>(comp + mr::kChildrenCount, (uint64_t)ids.size());
		return comp;
	};
	std::map<uint32_t, uintptr_t> children = { { 1, childList({ 2, 3, 6 }) }, { 3, childList({ 4 }) }, { 4, childList({ 5 }) }, { 6, childList({ 7 }) } };
	const uintptr_t torso = 0x1001, blade = 0x2002, shield = 0x3003;   // (model instances: just tokens here)
	std::map<uint32_t, uintptr_t> models = { { 2, kBase + torso }, { 5, kBase + blade }, { 7, kBase + shield } };
	std::map<uint32_t, int> slots = { { 2, mr::kTR }, { 3, mr::kRH }, { 6, mr::kSH } };
	const auto read = [&](uintptr_t a, void* p, size_t n) { return m.Read(a, p, n); };
	const auto modelOf = [&](uint32_t e) -> uintptr_t { const auto it = models.find(e); return it == models.end() ? 0 : it->second; };
	const auto slotOf = [&](uint32_t e) -> int { const auto it = slots.find(e); return it == slots.end() ? -1 : it->second; };
	const auto childrenOf = [&](uint32_t e) -> uintptr_t { const auto it = children.find(e); return it == children.end() ? 0 : it->second; };
	mr::HeldModel hm[6];
	int looked = 0;
	const int n = mr::HeldModels(read, children[1], modelOf, slotOf, childrenOf, hm, 6, &looked);
	std::printf("  a Fallen as the game builds it: %d held model(s) found (%d children could hold), ", n, looked);
	bool bladeRH = false, shieldSH = false, torsoHeld = false;
	for (int i = 0; i < n; ++i) {
		if (hm[i].inst == kBase + blade && hm[i].slot == mr::kRH && hm[i].entity == 5) bladeRH = true;
		if (hm[i].inst == kBase + shield && hm[i].slot == mr::kSH && hm[i].entity == 7) shieldSH = true;
		if (hm[i].inst == kBase + torso) torsoHeld = true;
	}
	std::printf("the scimitar %s, the shield %s\n", bladeRH ? "found (RH, two levels down)" : "MISSING", shieldSH ? "found (SH)" : "MISSING");
	EXPECT(n == 2 && bladeRH && shieldSH && !torsoHeld && looked == 2, "the item under the rhand part and the shield, not the torso (%d)", n);
	// ItemBox: an item instance whose two meshes' bindings give two boxes - their union
	const uintptr_t inst = m.Alloc(0x320), res = m.Alloc(0xD0), list = m.Alloc(16);
	m.Put<uintptr_t>(inst + mr::kInstanceResource, res);
	m.Put<uint8_t>(res + mr::kResourceState, mr::kLoaded);
	m.Put<uintptr_t>(inst + mr::kInstanceMeshes, list);
	m.Put<uint64_t>(inst + mr::kInstanceMeshes + 8, 2);
	const float boxes[2][6] = { { -0.02f, 0.0f, -0.01f, 0.02f, 0.60f, 0.01f }, { -0.06f, -0.12f, -0.03f, 0.06f, 0.02f, 0.03f } };   // the blade, the hilt
	for (int k = 0; k < 2; ++k) {
		const uintptr_t handle = m.Alloc(8), mesh = m.Alloc(0xC0), binds = m.Alloc(mr::kBindingStride);
		m.Put<uintptr_t>(list + 8 * k, handle);
		m.Put<uintptr_t>(handle, mesh);
		m.Put<int32_t>(mesh + mr::kMeshBoneCount, 1);
		m.Put<uintptr_t>(mesh + mr::kMeshBones, binds);
		m.Put<uintptr_t>(binds + mr::kBindingName, m.Str("root"));
		m.PutBytes(binds + mr::kBindingMin, boxes[k], 12);
		m.PutBytes(binds + mr::kBindingMax, boxes[k] + 3, 12);
	}
	mr::BoneBox box;
	const char* why = "";
	EXPECT(mr::ItemBox(read, inst, &box, &why) && box.min[1] == -0.12f && box.max[1] == 0.60f && box.min[0] == -0.06f && box.max[2] == 0.03f,
	       "the item's box is the union of its meshes' boxes (%s)", why);
	// a bow (Serg, 0.188.6: the rogue archer's bow "one huge orange capsule, ~0.5 m across and short"): one mesh skinned to its own skeleton's three bones - the grip
	// at the model's origin, the upper limb 0.45 up, the lower 0.45 down - each binding's box about ITS bone (a limb's box 0.4 long along it). Through the bind
	// poses the union is the bow's length, 1.3, not the 0.4 the boxes give as they are.
	const uintptr_t bow = m.Alloc(0x320), bres = m.Alloc(0xD0), blist = m.Alloc(8), bmodel = m.Alloc(0x60), bskel = m.Alloc(0x20);
	const char* names[3] = { "bow_grip", "bow_top", "bow_bottom" };
	const float ys[3] = { 0.0f, 0.45f, -0.45f };
	const uintptr_t bbones = m.Alloc(mr::kBoneStride * 3);
	for (int i = 0; i < 3; ++i) {
		const uintptr_t at = bbones + mr::kBoneStride * i;
		m.Put<uintptr_t>(at + mr::kBoneName, m.Str(names[i]));
		const float inv[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -ys[i], 0, 1 };   // (the bone at y in the model: its inverse moves it back)
		m.PutBytes(at + mr::kBoneInvWorld, inv, sizeof inv);
	}
	m.Put<uintptr_t>(bow + grannymesh::kInstanceModel, bmodel);
	m.Put<uintptr_t>(bmodel + grannymesh::kModelSkeleton, bskel);
	m.Put<int32_t>(bskel + mr::kSkelCount, 3);
	m.Put<uintptr_t>(bskel + mr::kSkelBones, bbones);
	m.Put<uintptr_t>(bow + mr::kInstanceResource, bres);
	m.Put<uint8_t>(bres + mr::kResourceState, mr::kLoaded);
	m.Put<uintptr_t>(bow + mr::kInstanceMeshes, blist);
	m.Put<uint64_t>(bow + mr::kInstanceMeshes + 8, 1);
	const uintptr_t bhandle = m.Alloc(8), bmesh = m.Alloc(0xC0), bbinds = m.Alloc(mr::kBindingStride * 3);
	m.Put<uintptr_t>(blist, bhandle);
	m.Put<uintptr_t>(bhandle, bmesh);
	m.Put<int32_t>(bmesh + mr::kMeshBoneCount, 3);
	m.Put<uintptr_t>(bmesh + mr::kMeshBones, bbinds);
	const float bb[3][6] = { { -0.03f, -0.06f, -0.03f, 0.03f, 0.06f, 0.03f }, { -0.02f, -0.05f, -0.02f, 0.06f, 0.20f, 0.02f }, { -0.02f, -0.20f, -0.02f, 0.06f, 0.05f, 0.02f } };
	for (int k = 0; k < 3; ++k) {
		const uintptr_t at = bbinds + mr::kBindingStride * k;
		m.Put<uintptr_t>(at + mr::kBindingName, m.Str(names[k]));
		m.PutBytes(at + mr::kBindingMin, bb[k], 12);
		m.PutBytes(at + mr::kBindingMax, bb[k] + 3, 12);
	}
	mr::BoneBox bowBox;
	const bool bowOk = mr::ItemBox(read, bow, &bowBox, &why);
	const float len = bowBox.max[1] - bowBox.min[1], across = bowBox.max[0] - bowBox.min[0];
	std::printf("  a bow skinned to its own three bones: its box %.2f long, %.2f across (as the boxes are, without their bones: 0.40 long)\n", len, across);
	EXPECT(bowOk && std::fabs(bowBox.min[1] + 0.65f) < 1e-4f && std::fabs(bowBox.max[1] - 0.65f) < 1e-4f && std::fabs(across - 0.09f) < 1e-4f,
	       "a bow's box through its bones' bind poses: 1.30 long (%.3f .. %.3f), 0.09 across (%.3f) (%s)", bowBox.min[1], bowBox.max[1], across, why);
	mr::Capsule bc;
	const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	EXPECT(mr::HeldCapsules(id, bowBox, false, &bc, 1) == 1 && mr::Extent(bc) > 1.2f && bc.radius < 0.06f, "its capsule along the bow (%.2f long, r %.3f)", mr::Extent(bc), bc.radius);
	float inv[16];
	const float t[16] = { 0, 1, 0, 0, -2, 0, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1 };
	float back[16];
	EXPECT(mr::InvertAffine(t, inv) && mr::InvertAffine(inv, back) && std::fabs(back[4] + 2.0f) < 1e-5f && std::fabs(back[13] - 4.0f) < 1e-5f, "the affine inverse inverts");
}

void TestCapsules() {
	Doll d = MakeDoll(Humanoid());
	const auto read = [&](uintptr_t a, void* p, size_t n) { return d.m.Read(a, p, n); };
	mr::Boxes b;
	mr::BoxesOf(read, d.instance, d.skeleton, &b);
	float at[3] = { 2.0f, 0.0f, -3.0f };
	const Rig r = MakeRig(b, at, 0.0f, 1.0f);
	EXPECT(r.n == 7, "7 limbs: the finger is a trifle (%d)", r.n);
	EXPECT(!ByBone(r, 8), "no finger");
	const mr::Capsule* thigh = ByBone(r, 4);
	EXPECT(thigh && std::fabs(thigh->radius - 0.08f) < 1e-4f, "the thigh 8 cm thick: %.3f", thigh ? thigh->radius : -1.0f);
	if (thigh) {   // along the bone's -y from the hip: 0.85 long, its ends one radius in
		const float top = std::max(thigh->a[1], thigh->b[1]), bottom = std::min(thigh->a[1], thigh->b[1]);
		EXPECT(std::fabs(top - (0.95f - 0.08f)) < 1e-3f && std::fabs(bottom - (0.95f - 0.85f + 0.08f)) < 1e-3f && std::fabs(thigh->a[0] - 1.88f) < 1e-3f,
		       "the thigh where the bone has it: %.3f..%.3f at x %.3f", bottom, top, thigh->a[0]);
	}
	EXPECT(r.c[0].bone == 2, "the largest first: the spine (%d)", r.c[0].bone);
	// turned and scaled: the same doll a quarter turn round and twice as big - the capsules turn and grow with the bones
	const Rig big = MakeRig(b, at, 1.5707963f, 2.0f);
	const mr::Capsule* t2 = ByBone(big, 4);
	EXPECT(t2 && std::fabs(t2->radius - 0.16f) < 1e-4f, "twice the size: %.3f", t2 ? t2->radius : -1.0f);
	if (t2) EXPECT(std::fabs(t2->a[2] - (at[2] + 0.24f)) < 1e-3f && std::fabs(t2->a[0] - at[0]) < 1e-3f, "turned: the left thigh now at z %.3f, x %.3f", t2->a[2], t2->a[0]);
}

// Akara's legs: a thigh's box hanging far off its bone (her robe's vertices bound to it) - a capsule along the bone, hip to knee, as thick as the box is
// across, never thicker than half the way to the other hip nor than half its length.
void TestAlongBone() {
	float hip[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.1f, 0.9f, 0.0f, 1};   // the left hip at (0.1, 0.9, 0), unturned
	mr::BoneBox box;
	box.bone = 5;
	box.mirror = 6;
	box.min[0] = -0.06f; box.min[1] = -0.9f; box.min[2] = -0.06f;   // 12 cm across, down by the shin (not at its bone)
	box.max[0] = 0.06f; box.max[1] = -0.5f; box.max[2] = 0.06f;
	EXPECT(!mr::AtItsBone(box), "a box hanging far below its bone is off it");
	const float knee[3] = {0.1f, 0.48f, 0.0f}, otherHip[3] = {-0.1f, 0.9f, 0.0f};
	mr::Capsule c;
	EXPECT(mr::AlongBone(hip, knee, otherHip, box, &c), "a capsule along the bone");
	EXPECT(std::fabs(c.a[1] - 0.9f) < 1e-5f && std::fabs(c.b[1] - 0.48f) < 1e-5f && c.a[0] == 0.1f && c.b[0] == 0.1f, "from the hip to the knee (%.2f..%.2f)", c.a[1], c.b[1]);
	EXPECT(std::fabs(c.radius - 0.06f) < 1e-5f && c.bone == 5 && c.mirror == 6, "as thick as its box is across (%.3f)", c.radius);
	const float near[3] = {0.0f, 0.9f, 0.0f};   // hips 10 cm apart: at most 5 cm
	EXPECT(mr::AlongBone(hip, knee, near, box, &c) && std::fabs(c.radius - 0.05f) < 1e-5f, "never past half the way to the other hip (%.3f)", c.radius);
	const float shortKnee[3] = {0.1f, 0.82f, 0.0f};
	EXPECT(mr::AlongBone(hip, shortKnee, nullptr, box, &c) && std::fabs(c.radius - 0.04f) < 1e-5f, "never thicker than half its length (%.3f)", c.radius);
	const float atHip[3] = {0.1f, 0.9f, 0.0f};
	EXPECT(!mr::AlongBone(hip, atHip, nullptr, box, &c), "a child at the bone itself: no length, none");
}

void TestAssemble() {
	Doll d = MakeDoll(Humanoid());
	const auto read = [&](uintptr_t a, void* p, size_t n) { return d.m.Read(a, p, n); };
	mr::Boxes b;
	mr::BoxesOf(read, d.instance, d.skeleton, &b);
	const float upm = 4.0f;   // world units per metre: the rig is made in world units, the monsters' capsules in metres
	float at[3] = { 8.0f, 0.0f, -12.0f };   // (2 m, -3 m)
	Rig rigs[2];
	rigs[0] = MakeRig(b, at, 0.0f, upm);   // a doll 1.8 m tall in world units
	float far[3] = { 40.0f, 0.0f, 40.0f };
	rigs[1] = MakeRig(b, far, 0.0f, upm);
	rigs[0].doll = 1;   // (a humanoid: its limbs may sway)
	mb::Body monsters[2];
	monsters[0].id = 11;   // stands where the first doll stands
	monsters[0].bottom[0] = 2.1f; monsters[0].bottom[1] = 0.3f; monsters[0].bottom[2] = -3.0f;
	monsters[0].top[0] = 2.1f; monsters[0].top[1] = 1.5f; monsters[0].top[2] = -3.0f;
	monsters[0].radius = 0.35f;
	monsters[1] = monsters[0];
	monsters[1].id = 12;   // stands where no skeleton is
	monsters[1].bottom[0] = monsters[1].top[0] = -4.0f;
	mb::Snapshot snap;
	int rigged = 0;
	mb::Assemble(monsters, 2, rigs, 2, upm, &snap, &rigged);
	int limbs = 0, own = 0, dolls = 0;
	for (int i = 0; i < snap.n; ++i) {
		if (snap.b[i].doll) ++dolls;
		if (snap.b[i].id == 11 && snap.b[i].rig) ++limbs;
		if (snap.b[i].id == 12 && !snap.b[i].rig) ++own;
	}
	EXPECT(rigged == 1 && limbs == rigs[0].n && own == 1 && snap.n == limbs + 1, "monster 11 has its skeleton's %d limbs (%d), monster 12 its own capsule (%d), %d rigged", rigs[0].n, limbs, own, rigged);
	EXPECT(dolls == limbs, "the humanoid's limbs are dolls, the own capsule not: %d of %d", dolls, limbs);
	rigs[0].still = true;   // [weight] monster_ragdoll: a corpse lies where monster 11 stands - its limbs are not 11's
	mb::Snapshot corpse;
	int rigged2 = 0;
	mb::Assemble(monsters, 2, rigs, 2, upm, &corpse, &rigged2);
	EXPECT(rigged2 == 0 && corpse.n == 2, "a still rig (a corpse) is nobody's body: %d rigged, %d bodies", rigged2, corpse.n);
	rigs[0].still = false;
	{   // [weight] npc_bodies: a unit the server did not call hostile (a town NPC) keeps that mark on every limb and on its own capsule
		mb::Body npcs[2] = { monsters[0], monsters[1] };
		npcs[0].hostile = false;
		mb::Snapshot s2;
		mb::Assemble(npcs, 2, rigs, 2, upm, &s2);
		int notHostile = 0, hostileOnes = 0;
		for (int i = 0; i < s2.n; ++i) (s2.b[i].hostile ? hostileOnes : notHostile) += 1;
		EXPECT(notHostile == rigs[0].n && hostileOnes == 1, "the NPC's %d limbs are marked not hostile (%d), the monster's own capsule hostile (%d)", rigs[0].n, notHostile, hostileOnes);
	}
	bool metres = true;
	for (int i = 0; i < snap.n; ++i)
		if (snap.b[i].rig) metres = metres && std::fabs(snap.b[i].bottom[0] - 2.0f) < 0.4f && snap.b[i].radius < 0.25f;
	EXPECT(metres, "the limbs in metres (world units over upm)");
	// [debug] melee_boxes: the snapshot drawn - the limbs pink, the monster on its own capsule blue (less the touch's margin), the limb the weapon is on white,
	// in world units from where the hands hang; never more than the room left
	snap.upm = upm;
	const float eye[3] = { 8.0f, 6.0f, -10.0f };
	float pa[mb::kMax][4], pbb[mb::kMax][4];
	const int thighBone = 4;
	const int packed = mb::Pack(snap, eye, 11, thighBone, pa, pbb, mb::kMax);
	int pink = 0, blue = 0, white = 0;
	bool placed = true;
	for (int i = 0; i < packed; ++i) {
		const int kind = (int)pbb[i][3];
		pink += kind == mb::kKindLimb;
		blue += kind == mb::kKindOwn;
		white += kind == mb::kKindTouched;
		placed = placed && std::fabs(pa[i][0] - (snap.b[i].bottom[0] * upm - eye[0])) < 1e-3f;
		if (kind == mb::kKindOwn) placed = placed && std::fabs(pa[i][3] - mb::CoreRadius(snap.b[i].radius) * upm) < 1e-4f;
	}
	EXPECT(packed == snap.n && pink == rigs[0].n - 1 && white == 1 && blue == 1 && placed, "drawn: %d pink, %d white (the thigh), %d blue of %d", pink, white, blue, packed);
	EXPECT(mb::Pack(snap, eye, 0, -1, pa, pbb, 3) == 3, "never more than the room left");
	snap.upm = 0;
	EXPECT(mb::Pack(snap, eye, 0, -1, pa, pbb, mb::kMax) == 0, "a snapshot with no scale draws nothing");
}

// The game's own meshes (BoxesOfMeshes): the headset test of 2026-10-21 found every .skeleton's Granny model without meshes; the boxes are in D2R's
// Mesh objects, reached through the model instance (+0x20 resource, its +0xC8 state 4 = loaded; +0x310 the meshes' handles, +0x318 their count;
// a handle's first qword the Mesh; Mesh +0xB4 the bindings' count, +0xB8 the array of 0x2C).
void TestGameMeshes() {
	Doll d = MakeDoll(Humanoid());
	Memory& m = d.m;
	const std::vector<std::vector<Box>> meshes = Humanoid();
	const uintptr_t inst = m.Alloc(0x400), res = m.Alloc(0x100), data = m.Alloc(8 * meshes.size());
	m.Put<uintptr_t>(inst + mr::kInstanceResource, res);
	m.Put<uint8_t>(res + mr::kResourceState, mr::kLoaded);
	m.Put<uintptr_t>(inst + mr::kInstanceMeshes, data);
	m.Put<uint64_t>(inst + mr::kInstanceMeshes + 8, meshes.size());
	for (size_t k = 0; k < meshes.size(); ++k) {
		const uintptr_t handle = m.Alloc(0x10), mesh = m.Alloc(0x300), binds = m.Alloc(mr::kBindingStride * meshes[k].size());
		m.Put<uintptr_t>(data + 8 * k, handle);
		m.Put<uintptr_t>(handle, mesh);
		m.Put<int32_t>(mesh + mr::kMeshBoneCount, (int32_t)meshes[k].size());
		m.Put<uintptr_t>(mesh + mr::kMeshBones, binds);
		for (size_t j = 0; j < meshes[k].size(); ++j) {
			const uintptr_t at = binds + j * mr::kBindingStride;
			m.Put<uintptr_t>(at + mr::kBindingName, m.Str(meshes[k][j].bone));
			m.PutBytes(at + mr::kBindingMin, meshes[k][j].mn, 12);
			m.PutBytes(at + mr::kBindingMax, meshes[k][j].mx, 12);
		}
	}
	// the .skeleton's model has no meshes - as in the game
	m.Put<int32_t>(d.model + grannymesh::kModelMeshCount, 0);
	const auto read = [&](uintptr_t a, void* p, size_t n) { return m.Read(a, p, n); };
	mr::Boxes g, b;
	EXPECT(!mr::BoxesOf(read, d.instance, d.skeleton, &g), "the .skeleton's Granny model has no meshes (%s)", g.why);
	int nm = 0;
	EXPECT(mr::BoxesOfMeshes(read, inst, d.skeleton, &b, &nm), "the game's meshes give the boxes: %s", b.why);
	EXPECT(b.n == 8 && nm == 2 && b.meshes == 2, "8 boxes of 2 meshes: %d %d %d", b.n, nm, b.meshes);
	// not loaded yet: nothing, and it says so (the caller asks again)
	m.Put<uint8_t>(res + mr::kResourceState, 3);
	EXPECT(!mr::BoxesOfMeshes(read, inst, d.skeleton, &b) && std::strcmp(b.why, "the model is not loaded yet") == 0, "a model still loading: %s", b.why);
	m.Put<uint8_t>(res + mr::kResourceState, mr::kLoaded);
	m.Put<uint64_t>(inst + mr::kInstanceMeshes + 8, 0);
	EXPECT(!mr::BoxesOfMeshes(read, inst, d.skeleton, &b), "no meshes: refused (%s)", b.why);
	// the paths that are no bodies
	EXPECT(!mr::BodyPath("data/hd/items/misc/dropped_items.skeleton") && !mr::BodyPath("x/gold_small.skeleton") && !mr::BodyPath("x/arrows.skeleton") &&
	           !mr::BodyPath("data/hd/character/player/barbarian/barbarian.skeleton"),
	       "items, gold, arrows, a hero are no bodies");
	EXPECT(mr::BodyPath("data/hd/character/monster/quillrat1/quillrat1.skeleton") && mr::BodyPath("data/hd/character/monster/fallen1/fallen1.skeleton"), "monsters are bodies");
}

// A model instance as the game keeps one (BoxesOfMeshes): +0x20 its resource (+0xC8 the state, 4 = loaded), +0x310 its meshes' handles, +0x318 their
// count; a handle's first qword the Mesh; Mesh +0xB4 the bindings' count, +0xB8 the array of 0x2C.
uintptr_t GameModel(Memory& m, const std::vector<std::vector<Box>>& meshes, uint8_t state = mr::kLoaded) {
	const uintptr_t inst = m.Alloc(0x400), res = m.Alloc(0x100), data = m.Alloc(8 * meshes.size());
	m.Put<uintptr_t>(inst + mr::kInstanceResource, res);
	m.Put<uint8_t>(res + mr::kResourceState, state);
	m.Put<uintptr_t>(inst + mr::kInstanceMeshes, data);
	m.Put<uint64_t>(inst + mr::kInstanceMeshes + 8, meshes.size());
	for (size_t k = 0; k < meshes.size(); ++k) {
		const uintptr_t handle = m.Alloc(0x10), mesh = m.Alloc(0x300), binds = m.Alloc(mr::kBindingStride * meshes[k].size());
		m.Put<uintptr_t>(data + 8 * k, handle);
		m.Put<uintptr_t>(handle, mesh);
		m.Put<int32_t>(mesh + mr::kMeshBoneCount, (int32_t)meshes[k].size());
		m.Put<uintptr_t>(mesh + mr::kMeshBones, binds);
		for (size_t j = 0; j < meshes[k].size(); ++j) {
			const uintptr_t at = binds + j * mr::kBindingStride;
			m.Put<uintptr_t>(at + mr::kBindingName, m.Str(meshes[k][j].bone));
			m.PutBytes(at + mr::kBindingMin, meshes[k][j].mn, 12);
			m.PutBytes(at + mr::kBindingMax, meshes[k][j].mx, 12);
		}
	}
	return inst;
}

// A unit's parts (PartModels, BoxesOfModels): the headset test of 2026-10-22 found no monster's root with a ModelComponent - a unit is a root (the
// skeleton) and its parts, each an entity with its own model, listed in the root's AttachedChildrenComponent (+0 the ids, +8 the count, its two top
// bits not part of it), each part's slot the first byte of its UnitPartComponent (Diablo II's HD TR LG RA LA RH LH SH S1..S8).
void TestUnitParts() {
	Doll d = MakeDoll(Humanoid());
	Memory& m = d.m;
	m.Put<int32_t>(d.model + grannymesh::kModelMeshCount, 0);   // the .skeleton's model: no meshes, as in the game
	const std::vector<Box> body = Humanoid()[0], cape = Humanoid()[1];
	std::vector<Box> torso, head;
	for (const Box& b : body) (std::strcmp(b.bone, "head") == 0 ? head : torso).push_back(b);
	const std::vector<Box> sword = { { "R_arm", { -0.02f, -1.60f, -0.02f }, { 0.02f, -0.50f, 0.02f } } };   // a long blade bound to the hand
	const uintptr_t tr = GameModel(m, { torso }), hd = GameModel(m, { head }), cp = GameModel(m, { cape }), rh = GameModel(m, { sword });
	// the children: the torso (TR), a light with no model, the head (HD), the cape (no UnitPartComponent), the sword (RH)
	const uint32_t ids[] = { 0x101, 0x102, 0x103, 0x104, 0x105 };
	const uintptr_t models[] = { tr, 0, hd, cp, rh };
	const int slots[] = { mr::kTR, -1, mr::kHD, -1, mr::kRH };
	const uintptr_t list = m.Alloc(sizeof ids), children = m.Alloc(mr::kChildrenCompSize);
	m.PutBytes(list, ids, sizeof ids);
	m.Put<uintptr_t>(children + mr::kChildrenData, list);
	m.Put<uint64_t>(children + mr::kChildrenCount, 5ull | (1ull << 62));   // a flag in the top bits
	const auto read = [&](uintptr_t a, void* p, size_t n) { return m.Read(a, p, n); };
	const auto modelOf = [&](uint32_t e) -> uintptr_t {
		for (int i = 0; i < 5; ++i)
			if (ids[i] == e) return models[i];
		return 0;
	};
	const auto slotOf = [&](uint32_t e) -> int {
		for (int i = 0; i < 5; ++i)
			if (ids[i] == e) return slots[i];
		return -1;
	};
	uintptr_t got[mr::kMaxModels];
	mr::Parts parts;
	const int n = mr::PartModels(read, children, modelOf, slotOf, got, mr::kMaxModels, &parts);
	EXPECT(n == 3 && got[0] == tr && got[1] == hd && got[2] == cp, "the body's parts: the torso, the head, the cape (%d)", n);
	EXPECT(parts.children == 5 && parts.models == 4 && parts.held == 1 && parts.slots[3] == mr::kRH, "5 children, 4 with a model, the sword held: %d %d %d", parts.children,
	       parts.models, parts.held);
	mr::Boxes b, alone;
	int nm = 0;
	EXPECT(mr::BoxesOfModels(read, got, n, d.skeleton, &b, &nm), "the parts give the boxes: %s", b.why);
	EXPECT(b.n == 8 && nm == 3 && b.meshes == 3, "8 boxes of 3 meshes in 3 parts: %d %d %d", b.n, nm, b.meshes);
	mr::BoxesOfMeshes(read, tr, d.skeleton, &alone);
	EXPECT(alone.n == 7, "the torso alone: 7 boxes (%d)", alone.n);
	for (int i = 0; i < b.n; ++i)
		if (std::strcmp(kBones[b.b[i].bone], "R_arm") == 0) EXPECT(b.b[i].min[1] > -0.63f, "the sword is not the arm: %.2f", b.b[i].min[1]);
		else if (std::strcmp(kBones[b.b[i].bone], "spine") == 0) EXPECT(b.b[i].max[1] > 0.59f && b.b[i].min[2] < -0.15f, "the spine's box takes the cape's in");
	// one part still loading: none yet, asked again
	uintptr_t hdRes = 0;
	m.Read(hd + mr::kInstanceResource, &hdRes, sizeof hdRes);
	m.Put<uint8_t>(hdRes + mr::kResourceState, 3);
	EXPECT(!mr::BoxesOfModels(read, got, n, d.skeleton, &b) && std::strcmp(b.why, "the model is not loaded yet") == 0, "a part still loading: %s", b.why);
	// a part that does not read is passed over; none that reads: refused
	const uintptr_t lost[] = { 0x7FFF0000, tr };
	EXPECT(mr::BoxesOfModels(read, lost, 2, d.skeleton, &b) && b.n == 7, "a part that does not read is passed over (%d)", b.n);
	EXPECT(!mr::BoxesOfModels(read, lost, 1, d.skeleton, &b) && std::strcmp(b.why, "the model instance does not read") == 0, "nothing reads: %s", b.why);
	// the count: the top bits are not part of it; endless or negative is no count
	EXPECT(mr::ChildCount(3) == 3 && mr::ChildCount(3ull | (3ull << 62)) == 3 && mr::ChildCount(1ull << 61) == -1 && mr::ChildCount(mr::kMaxChildren + 1) == -1,
	       "the children's count");
	// no children: nothing; an empty list: none, but it read
	EXPECT(mr::PartModels(read, 0, modelOf, slotOf, got, mr::kMaxModels, &parts) == 0 && parts.children == -1, "no AttachedChildrenComponent");
	m.Put<uint64_t>(children + mr::kChildrenCount, 0);
	EXPECT(mr::PartModels(read, children, modelOf, slotOf, got, mr::kMaxModels, &parts) == 0 && parts.children == 0, "no children");
	// the slots that are held
	EXPECT(!mr::BodySlot(mr::kRH) && !mr::BodySlot(mr::kLH) && !mr::BodySlot(mr::kSH) && mr::BodySlot(mr::kTR) && mr::BodySlot(-1) && mr::BodySlot(8), "RH, LH, SH are held");
}

// The doll against the hold's sword: a thrust into its chest stops; a cut low through the gap between its legs goes through.
void TestAgainstTheDoll() {
	namespace hh = heft::hold;
	namespace hc = heft::contact;
	Doll d = MakeDoll(Humanoid());
	const auto read = [&](uintptr_t a, void* p, size_t n) { return d.m.Read(a, p, n); };
	mr::Boxes b;
	mr::BoxesOf(read, d.instance, d.skeleton, &b);
	float at[3] = { 0.0f, 0.0f, -1.35f };
	const Rig r = MakeRig(b, at, 0.0f, 1.0f);
	std::vector<hc::Capsule> caps;
	for (int i = 0; i < r.n; ++i) {
		hc::Capsule c;
		c.a = { r.c[i].a[0], r.c[i].a[1], r.c[i].a[2] };
		c.b = { r.c[i].b[0], r.c[i].b[1], r.c[i].b[2] };
		c.radius = r.c[i].radius;
		c.id = 5;
		caps.push_back(c);
	}
	const auto swing = [&](heft::V3 from, heft::V3 to, double* deepest, int* steps) {
		hh::Settings s;
		s.bodies = true;
		hh::Hold h;
		hh::HandIn in[2];
		for (int k = 0; k < 2; ++k) { in[k].tracked = k == 0; in[k].grip = k == 0 ? 1.0f : 0.0f; in[k].fistLine = { 0, 0, -1 }; }
		in[0].pose.pos = from;
		in[1].pose.pos = { -0.3, 1.0, 0 };
		h.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
		hh::HandOut out[2];
		*deepest = -1;
		*steps = 0;
		for (int i = 1; i <= 60; ++i) {
			const double u = std::min(1.0, i / 27.0), e = u * u * (3 - 2 * u);
			in[0].pose.pos = { from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e, from.z + (to.z - from.z) * e };
			h.SetBodies(caps.data(), (int)caps.size());
			h.Step(in, 1.0 / 90.0, s, out);
			if (h.LastContact().bodies > 0) ++*steps;
			heft::V3 butt, tip;
			h.Ends(&butt, &tip);
			for (const hc::Capsule& c : caps) {
				double sa, ta;
				hc::ClosestOnSegments(butt, tip, c.a, c.b, &sa, &ta);
				const heft::V3 p = heft::Add(butt, heft::Mul(heft::Sub(tip, butt), sa)), q = heft::Add(c.a, heft::Mul(heft::Sub(c.b, c.a), ta));
				*deepest = std::max(*deepest, c.radius + 0.02 - heft::Len(heft::Sub(p, q)));
			}
		}
	};
	double deep = 0;
	int steps = 0;
	swing({ 0, 1.30, 0 }, { 0, 1.30, -0.6 }, &deep, &steps);   // the chest: its front at z -1.35 + 0.17
	std::printf("  the doll: a thrust into the chest - %.1f cm deep at most, %d steps on it\n", deep * 100, steps);
	EXPECT(deep < 0.012 && steps > 10, "the thrust stops on the chest: %.3f m, %d steps", deep, steps);
	swing({ 0, 0.45, 0 }, { 0, 0.45, -0.6 }, &deep, &steps);   // between the thighs (their axes at x -0.12 and 0.12, 8 cm thick: 8 cm between them): a 2 cm blade passes
	std::printf("  the doll: a thrust between the legs - %d steps on a body (the gap between the thighs lets it through)\n", steps);
	EXPECT(steps == 0, "between the legs nothing is met: %d steps", steps);
}

}   // namespace

int main() {
	std::printf("monster_rig_test: a monster's body from its own skeleton\n");
	TestBoxes();
	TestEveryLimb();
	TestNoExtraLimbs();
	TestHeld();
	TestHeldSpace();
	TestHeldModels();
	TestCapsules();
	TestAlongBone();
	TestAssemble();
	TestGameMeshes();
	TestUnitParts();
	TestAgainstTheDoll();
	if (g_fails) {
		std::printf("monster_rig_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("monster_rig_test: all passed\n");
	return 0;
}
