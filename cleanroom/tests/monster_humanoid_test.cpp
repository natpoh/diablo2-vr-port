// [weight] monster_ragdoll: the doll goes on humanoids only (vr/monster_humanoid.h) - skeletons made up after the rigs the game's heroes have (F10 dumps)
// and the usual ones of other monsters.
//
// Checks: a name's words (case turns, digits, underscores); a biped rig, the heroes' three rigs, a rig whose hips are bones of a side, a rig with lone
// "leg" / "arm" helper bones off its pelvis and spine (the first headset test refused 'zombie1' for them) are humanoids; a spider (three legs a side), a
// beast with fore and hind legs, a bird (wings, no arms), a worm, a blob, names that tell nothing, arms on no trunk are not, each with its reason; the log
// line names the limbs; a beast on a biped rig passes the names but not Upright (its trunk flat, or its forepaws on the ground - 'quillrat1' in the
// first test), a man and a crouching fallen do; the verdict read off a skeleton in a made-up game memory (vr/monster_rig.h's layout).

#include "../../vr/monster_humanoid.h"

#include <array>
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

struct Rig {
	std::vector<std::string> names;
	std::vector<int> parent;
	int Add(const char* name, int up) {
		names.push_back(name);
		parent.push_back(up);
		return static_cast<int>(names.size()) - 1;
	}
	mh::Verdict Judge() const {
		std::vector<const char*> p;
		for (const std::string& s : names) p.push_back(s.c_str());
		return mh::Judge(p.data(), parent.data(), static_cast<int>(names.size()));
	}
};

// A 3ds Max biped: Bip01 - Pelvis - Spine - Spine1 - Neck - Head, the clavicles off the neck, the thighs off the pelvis.
Rig Biped() {
	Rig r;
	const int root = r.Add("Bip01", -1), pelvis = r.Add("Bip01 Pelvis", root), spine = r.Add("Bip01 Spine", pelvis), spine1 = r.Add("Bip01 Spine1", spine);
	const int neck = r.Add("Bip01 Neck", spine1);
	r.Add("Bip01 Head", neck);
	for (const char* s : {"L", "R"}) {
		const std::string b = std::string("Bip01 ") + s + " ";
		const int cl = r.Add((b + "Clavicle").c_str(), neck), up = r.Add((b + "UpperArm").c_str(), cl), fo = r.Add((b + "Forearm").c_str(), up);
		const int ha = r.Add((b + "Hand").c_str(), fo);
		r.Add((b + "Finger0").c_str(), ha);
		const int th = r.Add((b + "Thigh").c_str(), pelvis), ca = r.Add((b + "Calf").c_str(), th), ft = r.Add((b + "Foot").c_str(), ca);
		r.Add((b + "Toe0").c_str(), ft);
	}
	return r;
}

void TestWords() {
	char w[mh::kMaxWords][mh::kWordLen];
	int n = mh::Words("LUpperArm", w, mh::kMaxWords);
	EXPECT(n == 3 && !std::strcmp(w[0], "l") && !std::strcmp(w[1], "upper") && !std::strcmp(w[2], "arm"), "LUpperArm: %d words", n);
	n = mh::Words("Bip01 R Forearm", w, mh::kMaxWords);
	EXPECT(n == 3 && !std::strcmp(w[0], "bip") && !std::strcmp(w[1], "r") && !std::strcmp(w[2], "forearm"), "Bip01 R Forearm: %d words", n);
	n = mh::Words("hand_index_01_R_JNT", w, mh::kMaxWords);
	EXPECT(n == 4 && !std::strcmp(w[0], "hand") && !std::strcmp(w[2], "r"), "hand_index_01_R_JNT: %d words", n);
	EXPECT(mh::Read("shoulder_R_JNT").part == mh::kArm && mh::Read("shoulder_R_JNT").side == 1, "the assassin's shoulder");
	EXPECT(mh::Read("LeftUpLeg").part == mh::kLeg && mh::Read("LeftUpLeg").side == -1, "a mixamo thigh");
	EXPECT(mh::Read("arm").part == mh::kNone, "an arm of no side is no evidence");
	EXPECT(mh::Read("head_M_JNT").part == mh::kHead, "the assassin's head");
	EXPECT(mh::Read("R_finger_A_A").part == mh::kNone, "a finger is nothing");
}

void TestHumanoids() {
	mh::Verdict v = Biped().Judge();
	EXPECT(v.humanoid && v.settled, "a biped: %s", v.why);
	EXPECT(v.armsL == 1 && v.armsR == 1 && v.legsL == 1 && v.legsR == 1, "one limb a side: %d %d %d %d", v.armsL, v.armsR, v.legsL, v.legsR);
	EXPECT(std::strstr(v.said, "Bip01 L Clavicle(4)") && std::strstr(v.said, "Bip01 R Thigh(3)"), "the log names the limbs: %s", v.said);

	Rig helpers = Biped();   // twist and strap bones hung off the pelvis and the spine: lone bones, no limbs of their own
	helpers.Add("L_leg_twist", 1);
	helpers.Add("R_leg_twist", 1);
	helpers.Add("L_thigh_armor_strap", 1);
	helpers.Add("L_arm_roll", 3);
	v = helpers.Judge();
	EXPECT(v.humanoid && v.legsL == 1 && v.armsL == 1, "helper bones are no limbs: %s (%d legs left)", v.why, v.legsL);

	Rig pal;   // paladin, amazon
	{
		const int root = pal.Add("root", -1), sp = pal.Add("spine_01_bind_jnt", root), sp2 = pal.Add("spine_02_bind_jnt", sp);
		pal.Add("head_bind_jnt", sp2);
		for (const char* s : {"L", "R"}) {
			const std::string b = s;
			const int up = pal.Add((b + "_upr_arm_bind_jnt").c_str(), sp2), lo = pal.Add((b + "_lwr_arm_bind_jnt").c_str(), up);
			pal.Add((b + "_hand_bind_jnt").c_str(), lo);
			pal.Add((b + "_weapon_attach").c_str(), lo);
			const int ul = pal.Add((b + "_upr_leg_bind_jnt").c_str(), root), ll = pal.Add((b + "_lwr_leg_bind_jnt").c_str(), ul);
			pal.Add((b + "_foot_bind_jnt").c_str(), ll);
		}
	}
	v = pal.Judge();
	EXPECT(v.humanoid, "the paladin's rig: %s", v.why);

	Rig asn;   // assassin: hips of a side between the pelvis and the knees
	{
		const int pe = asn.Add("pelvis_M_JNT", -1), sp = asn.Add("spine_01_M_JNT", pe), he = asn.Add("head_M_JNT", sp);
		asn.Add("head", he);
		for (const char* s : {"L", "R"}) {
			const std::string b = s;
			const int sh = asn.Add(("shoulder_" + b + "_JNT").c_str(), sp), el = asn.Add(("elbow_" + b + "_JNT").c_str(), sh), wr = asn.Add(("wrist_" + b + "_JNT").c_str(), el);
			asn.Add(("hand_index_01_" + b + "_JNT").c_str(), wr);
			const int hip = asn.Add(("hip_" + b + "_JNT").c_str(), pe), kn = asn.Add(("knee_" + b + "_JNT").c_str(), hip);
			asn.Add(("ankle_" + b + "_JNT").c_str(), kn);
		}
	}
	v = asn.Judge();
	EXPECT(v.humanoid, "the assassin's rig: %s", v.why);
}

void TestOthers() {
	Rig spider;
	{
		const int body = spider.Add("spine", -1);
		spider.Add("head", body);
		for (const char* s : {"L", "R"})
			for (int k = 1; k <= 3; ++k) {
				char n[32];
				std::snprintf(n, sizeof n, "%s_leg_0%d_a", s, k);
				const int a = spider.Add(n, body);
				std::snprintf(n, sizeof n, "%s_leg_0%d_b", s, k);
				spider.Add(n, a);
			}
	}
	mh::Verdict v = spider.Judge();
	EXPECT(!v.humanoid && v.settled && v.legsL == 3 && std::strstr(v.why, "more than two legs a side"), "a spider: %s (%d legs left)", v.why, v.legsL);

	Rig beast;
	{
		const int sp = beast.Add("spine", -1);
		beast.Add("head", sp);
		for (const char* s : {"L", "R"})
			for (const char* e : {"front", "back"}) {
				const std::string b = std::string(s) + "_" + e + "_leg";
				const int a = beast.Add((b + "_upper").c_str(), sp);
				beast.Add((b + "_lower").c_str(), a);
			}
	}
	v = beast.Judge();
	EXPECT(!v.humanoid && v.legsL == 2, "fore and hind legs: %s", v.why);

	Rig bird;
	{
		const int sp = bird.Add("spine", -1);
		bird.Add("head", sp);
		for (const char* s : {"L", "R"}) {
			bird.Add((std::string(s) + "_wing").c_str(), sp);
			const int leg = bird.Add((std::string(s) + "_leg").c_str(), sp);
			bird.Add((std::string(s) + "_foot").c_str(), leg);
		}
	}
	v = bird.Judge();
	EXPECT(!v.humanoid && std::strstr(v.why, "no arms"), "a bird: %s", v.why);

	Rig worm;
	{
		int up = worm.Add("head", -1);
		for (int k = 0; k < 8; ++k) up = worm.Add(("spine_0" + std::to_string(k)).c_str(), up);
	}
	v = worm.Judge();
	EXPECT(!v.humanoid && std::strstr(v.why, "no legs"), "a worm: %s", v.why);

	Rig blob;
	{
		const int r = blob.Add("root", -1);
		for (int k = 0; k < 6; ++k) blob.Add(("bone" + std::to_string(k)).c_str(), r);
	}
	v = blob.Judge();
	EXPECT(!v.humanoid && !v.settled, "a blob of plain bones: %s", v.why);

	Rig loose = Biped();   // its arms hang from the root, not from the trunk
	for (size_t i = 0; i < loose.names.size(); ++i)
		if (loose.names[i].find("Clavicle") != std::string::npos) loose.parent[i] = 0;
	v = loose.Judge();
	EXPECT(!v.humanoid && std::strstr(v.why, "trunk"), "arms off the trunk: %s", v.why);

	v = mh::Judge(nullptr, nullptr, 0);
	EXPECT(!v.humanoid && !v.settled, "nothing");
}

// A pose of the biped: the hips at `hips` over the feet at 0, the head at `head` and `ahead` in front of the hips (+z), the hands at `hands`.
struct Pose {
	std::vector<std::array<float, 3>> p;
	const float (*Data() const)[3] { return reinterpret_cast<const float (*)[3]>(p.data()); }
};
Pose Posed(const Rig& r, float hips, float head, float ahead, float hands) {
	Pose o;
	o.p.assign(r.names.size(), {0.0f, hips, 0.0f});
	for (size_t i = 0; i < r.names.size(); ++i) {
		const std::string& n = r.names[i];
		if (n.find("Head") != std::string::npos || n.find("Neck") != std::string::npos) o.p[i] = {0.0f, head, ahead};
		if (n.find("Spine1") != std::string::npos || n.find("Clavicle") != std::string::npos || n.find("UpperArm") != std::string::npos) o.p[i] = {0.0f, 0.5f * (hips + head), 0.5f * ahead};
		if (n.find("Forearm") != std::string::npos) o.p[i] = {0.0f, 0.5f * (hands + head), ahead};
		if (n.find("Hand") != std::string::npos || n.find("Finger") != std::string::npos) o.p[i] = {0.0f, hands, ahead};
		if (n.find("Calf") != std::string::npos) o.p[i][1] = hips * 0.5f;
		if (n.find("Foot") != std::string::npos || n.find("Toe") != std::string::npos) o.p[i][1] = 0.0f;
	}
	return o;
}

void TestUpright() {
	const Rig r = Biped();
	const mh::Verdict v = r.Judge();
	const int n = static_cast<int>(r.names.size());
	const char* why = "";
	EXPECT(mh::Upright(v, Posed(r, 1.0f, 1.7f, 0.1f, 0.9f).Data(), n, &why), "a man stands up: %s", why);
	EXPECT(mh::Upright(v, Posed(r, 0.45f, 0.85f, 0.25f, 0.3f).Data(), n, &why), "a crouching fallen, hunched, its hands low: %s", why);
	EXPECT(!mh::Upright(v, Posed(r, 0.5f, 0.6f, 0.5f, 0.3f).Data(), n, &why), "a dog on a biped rig: its head barely over its hips");
	EXPECT(!mh::Upright(v, Posed(r, 0.3f, 0.5f, 0.45f, 0.15f).Data(), n, &why) && std::strstr(why, "flatter"), "a quill rat: its head well up but far ahead - %s", why);
	EXPECT(!mh::Upright(v, Posed(r, 0.4f, 0.75f, 0.2f, 0.02f).Data(), n, &why) && std::strstr(why, "hands"), "a beast sitting up, forepaws on the ground: %s", why);
	EXPECT(!mh::Upright(v, Posed(r, 0.15f, 0.2f, 0.9f, 0.1f).Data(), n), "a body lying down is not seen standing (a later standing frame decides)");
	mh::Verdict no = v;
	no.humanoid = false;
	EXPECT(!mh::Upright(no, Posed(r, 1.0f, 1.7f, 0.1f, 0.9f).Data(), n), "only for a verdict the names passed");
	EXPECT(!mh::Upright(v, Posed(r, 1.0f, 1.7f, 0.1f, 0.9f).Data(), n - 1), "a pose of another skeleton");
}

// The skeleton in a made-up game memory: a granny_skeleton at base, its bones (0xA4 each) after it, the names after them.
struct Memory {
	uintptr_t base = 0x200000;
	std::vector<uint8_t> bytes;
	template <class T> void Put(uintptr_t at, const T& v) { std::memcpy(&bytes[at - base], &v, sizeof v); }
	bool Read(uintptr_t at, void* out, size_t n) const {
		if (at < base || at + n > base + bytes.size()) return false;
		std::memcpy(out, &bytes[at - base], n);
		return true;
	}
};

void TestMemory() {
	const Rig r = Biped();
	Memory m;
	const int n = static_cast<int>(r.names.size());
	const uintptr_t skel = m.base, bones = skel + 0x40, strings = bones + (uintptr_t)n * monsterrig::kBoneStride;
	m.bytes.assign(0x40 + (size_t)n * monsterrig::kBoneStride + (size_t)n * 32, 0);
	m.Put(skel + monsterrig::kSkelCount, (int32_t)n);
	m.Put(skel + monsterrig::kSkelBones, (uint64_t)bones);
	for (int i = 0; i < n; ++i) {
		const uintptr_t s = strings + (uintptr_t)i * 32;
		std::memcpy(&m.bytes[s - m.base], r.names[i].c_str(), r.names[i].size() + 1);
		m.Put(bones + (uintptr_t)i * monsterrig::kBoneStride + monsterrig::kBoneName, (uint64_t)s);
		m.Put(bones + (uintptr_t)i * monsterrig::kBoneStride + monsterrig::kBoneParent, (int32_t)r.parent[i]);
	}
	const auto read = [&](uintptr_t at, void* out, size_t bytes) { return m.Read(at, out, bytes); };
	mh::Verdict v = mh::JudgeSkeleton(read, skel);
	EXPECT(v.humanoid, "read off the memory: %s", v.why);
	v = mh::JudgeSkeleton(read, skel + 0x100000);
	EXPECT(!v.humanoid && !v.settled && std::strstr(v.why, "does not read"), "a skeleton that does not read: %s", v.why);
}

// A long skeleton (the crash on leaving the Arcane Sanctuary, 2026-10-23): 161 - 256 bones are judged in whole, past monster_rig's 160, and more than
// 256 are not judged - never written past the arrays.
void TestLongSkeleton() {
	const Rig r = Biped();
	for (int bones : {200, 256, 300}) {
		Memory m;
		const int n = bones;
		const uintptr_t skel = m.base, table = skel + 0x40, strings = table + (uintptr_t)n * monsterrig::kBoneStride;
		m.bytes.assign(0x40 + (size_t)n * monsterrig::kBoneStride + (size_t)n * 32, 0);
		m.Put(skel + monsterrig::kSkelCount, (int32_t)n);
		m.Put(skel + monsterrig::kSkelBones, (uint64_t)table);
		for (int i = 0; i < n; ++i) {
			const bool body = i < (int)r.names.size();
			const std::string name = body ? r.names[i] : "fx_cloth_" + std::to_string(i);   // the biped, then cloth / effect bones under its root
			const uintptr_t s = strings + (uintptr_t)i * 32;
			std::memcpy(&m.bytes[s - m.base], name.c_str(), name.size() + 1);
			m.Put(table + (uintptr_t)i * monsterrig::kBoneStride + monsterrig::kBoneName, (uint64_t)s);
			m.Put(table + (uintptr_t)i * monsterrig::kBoneStride + monsterrig::kBoneParent, (int32_t)(body ? r.parent[i] : 0));
		}
		const auto read = [&](uintptr_t at, void* out, size_t bytes) { return m.Read(at, out, bytes); };
		const mh::Verdict v = mh::JudgeSkeleton(read, skel);
		if (bones <= mh::kMaxBones)
			EXPECT(v.humanoid && v.bones == bones, "%d bones: judged in whole, a humanoid (%s)", bones, v.why);
		else
			EXPECT(!v.humanoid && std::strstr(v.why, "too many"), "%d bones: not judged (%s)", bones, v.why);
	}
}

}  // namespace

int main() {
	TestWords();
	TestHumanoids();
	TestOthers();
	TestUpright();
	TestMemory();
	TestLongSkeleton();
	std::printf("monster_humanoid_test: %s (%d failed)\n", g_fails ? "FAIL" : "ok", g_fails);
	return g_fails ? 1 : 0;
}
