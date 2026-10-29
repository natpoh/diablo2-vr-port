// The skeleton hook's discovery - vr/skeletons.cpp, included whole - on a toy hero rig in fake game memory: what it finds must not change with how cheaply it is found.
//
//  - Str (a name read a page at a time, vr/guarded_text.h, bearstonem/d2r-pcvr's) gives what the letter-at-a-time loop it replaced gave, at every place near a page that does
//    not read: strings that end on the page's last byte, run into the unreadable page, cross from one page to the next, are longer than 95 letters, hold a letter that is not
//    printable; and the low 64 KB.
//  - Learn finds the same bones of a hero rig whose names lie across pages (one name unreadable, one not printable): every slot it fills.
//  - Arms keeps the learned hero (one rig, held by pointer: posed again, it is not learned again or copied), a rig that is not a hero as nothing, and a full cache drops the
//    rigs that are not heroes, not the hero.
//  - The bow's, the flail's and the crossbow's caches tell a rig that is none of theirs once, and after that without learning it again.
//
//   skeleton_discovery_test

#include "../../vr/skeletons.cpp"

#include <cstdio>
#include <string>

namespace skel {
namespace tst {

int g_fails = 0;
#define CHECK(c)                                                       \
	do {                                                               \
		if (!(c)) {                                                    \
			++g_fails;                                                 \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c);         \
		}                                                              \
	} while (0)

// The loop Str was before (one guarded copy per letter): the reference.
std::string StrLetters(uintptr_t p) {
	char buf[96];
	int n = 0;
	for (; n < 95; ++n) {
		if (!Get(p + n, &buf[n])) return {};
		if (!buf[n]) break;
		if (buf[n] < 32 || buf[n] > 126) return {};
	}
	return std::string(buf, n);
}

// Three pages: two that read, then one that does not.
struct Guarded {
	char* base = nullptr;
	static constexpr size_t kPage = 0x1000;
	Guarded() {
		base = (char*)VirtualAlloc(nullptr, 3 * kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		DWORD old = 0;
		if (base) VirtualProtect(base + 2 * kPage, kPage, PAGE_NOACCESS, &old);
	}
	~Guarded() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
	char* End() const { return base + 2 * kPage; }   // the first byte that does not read
};

void Names() {
	std::printf("Str against the letter-at-a-time loop\n");
	Guarded g;
	CHECK(g.base != nullptr);
	if (!g.base) return;
	long compared = 0, empty = 0;
	auto same = [&](uintptr_t at) {
		const std::string a = Str(at), b = StrLetters(at);
		if (a != b) std::printf("    at end-%lld: '%s' against '%s'\n", (long long)((uintptr_t)g.End() - at), a.c_str(), b.c_str());
		CHECK(a == b);
		++compared;
		if (a.empty()) ++empty;
	};
	for (const size_t edge : {Guarded::kPage, 2 * Guarded::kPage}) {   // across from page to page; into the page that does not read
		for (int start = 1; start <= 140; ++start)
			for (int len = 0; len <= 110; ++len) {
				char* s = g.base + edge - start;
				memset(g.base + edge - 200, 'a', 200);
				if (edge == Guarded::kPage) memset(g.base + edge, 'b', 200);
				if (start - len > 0 || edge == Guarded::kPage) s[len] = 0;   // the NUL where it fits (across pages it always does)
				same((uintptr_t)s);
				if (len > 2 && s + len / 2 < g.End()) {   // a letter that is not printable inside it
					const char keep = s[len / 2];
					s[len / 2] = 0x01;
					same((uintptr_t)s);
					s[len / 2] = (char)0xC3;
					same((uintptr_t)s);
					s[len / 2] = keep;
				}
			}
	}
	for (const uintptr_t low : {(uintptr_t)0, (uintptr_t)0x100, (uintptr_t)0xFFFF, (uintptr_t)0x10000, (uintptr_t)0x10001}) same(low);
	{   // exactly 95 letters and longer: the first 95
		memset(g.base, 'c', 300);
		g.base[95] = 0;
		CHECK(Str((uintptr_t)g.base).size() == 95);
		g.base[95] = 'c';
		g.base[200] = 0;
		CHECK(Str((uintptr_t)g.base) == std::string(95, 'c'));
	}
	std::printf("  %ld places compared, %ld of them read as nothing\n", compared, empty);
}

// ---- a toy hero rig in fake game memory (as skeletons.cpp's header describes the game's) ----
struct BoneDef { const char* name; int parent; float x, y, z; };
const BoneDef kBones[] = {
	{"root", -1, 0, 0, 0},                                  // 0
	{"pelvis_bind_jnt", 0, 0, 3.5f, 0},                     // 1
	{"spine_01_bind_jnt", 1, 0, 4.6f, 0},                   // 2
	{"neck_bind_jnt", 2, 0, 5.4f, 0},                       // 3
	{"head_bind_jnt", 3, 0, 5.9f, 0},                       // 4
	{"R_eye_bind_jnt", 4, -0.1f, 6.1f, 0.2f},               // 5
	{"L_eye_bind_jnt", 4, 0.1f, 6.1f, 0.2f},                // 6
	{"R_upr_arm_bind_jnt", 2, -0.7f, 5.3f, 0},              // 7
	{"R_lwr_arm_bind_jnt", 7, -0.7f, 4.3f, 0},              // 8
	{"R_lwr_arm_twist_01_bind_jnt", 8, -0.7f, 3.9f, 0},     // 9
	{"R_hand_bind_jnt", 8, -0.7f, 3.3f, 0},                 // 10
	{"R_ind_finger_01_bind_jnt", 10, -0.7f, 3.0f, 0.1f},    // 11
	{"R_ind_finger_02_bind_jnt", 11, -0.7f, 2.9f, 0.1f},    // 12
	{"R_weapon_attach", 10, -0.7f, 3.1f, 0},                // 13
	{"L_upr_arm_bind_jnt", 2, 0.7f, 5.3f, 0},               // 14
	{"L_lwr_arm_bind_jnt", 14, 0.7f, 4.3f, 0},              // 15
	{"L_hand_bind_jnt", 15, 0.7f, 3.3f, 0},                 // 16
	{"L_ind_finger_01_bind_jnt", 16, 0.7f, 3.0f, 0.1f},     // 17
	{"L_ind_finger_02_bind_jnt", 17, 0.7f, 2.9f, 0.1f},     // 18
	{"L_weapon_attach", 16, 0.7f, 3.1f, 0},                 // 19
	{"shield_attach", 15, 0.8f, 3.8f, 0},                   // 20
	{"R_thigh_bind_jnt", 1, -0.3f, 3.3f, 0},                // 21
	{"R_knee_bind_jnt", 21, -0.3f, 1.8f, 0},                // 22
	{"R_foot_bind_jnt", 22, -0.3f, 0.3f, 0},                // 23
	{"L_thigh_bind_jnt", 1, 0.3f, 3.3f, 0},                 // 24
	{"L_knee_bind_jnt", 24, 0.3f, 1.8f, 0},                 // 25
	{"L_foot_bind_jnt", 25, 0.3f, 0.3f, 0},                 // 26
	{"two_hand_weapon_attach", 2, 0, 4.0f, 0.3f},           // 27
	{nullptr, 0, 0, 1, 0},                                  // 28: its name runs into the page that does not read
	{"R_hand_bind_jnt\x01", 0, 0, 1, 0},                    // 29: a letter that is not printable (read as no name: not a second right hand)
};
constexpr int kCount = (int)(sizeof kBones / sizeof kBones[0]);

#pragma pack(push, 1)
struct FakeWorld { int32_t n; uintptr_t mats; };
struct FakeSkeleton { uintptr_t name; int32_t count; uintptr_t bones; };   // +0 name, +8 count, +0xC bones
#pragma pack(pop)
static_assert(offsetof(FakeSkeleton, bones) == 0xC, "granny_skeleton: Bones at +0xC");
struct FakeNameObject { char pad[8]; uintptr_t path; };
struct FakeModel { char pad[8]; uintptr_t skeleton; };
struct FakeSelf { uintptr_t nameObject; uintptr_t model; char pad[0x10]; uintptr_t world; };
static_assert(offsetof(FakeSelf, world) == 0x20, "SkeletonInstance: world pose at +0x20");

struct Rig {
	Guarded g;   // the names (and the model paths) at the end of the readable pages, one across the page line
	std::vector<unsigned char> bones = std::vector<unsigned char>((size_t)kCount * 0xA4);
	std::vector<Mat> world = std::vector<Mat>(kCount);
	FakeWorld fw{};
	FakeSkeleton sk{};
	FakeModel model{};
	FakeNameObject heroName{}, monsterName{}, itemName{};
	FakeSelf hero{}, hero2{}, monster{}, item{}, menu{};   // menu: a player's model no entity of the world holds
	Rig() {
		char* at = g.End();
		auto put = [&](const char* s) { const size_t n = strlen(s) + 1; at -= n; memcpy(at, s, n); return (uintptr_t)at; };
		// the readable page's last bytes: bone 28's name, which runs on into the page that does not read; the other names just before it
		const uintptr_t endless = (uintptr_t)(g.End() - 6);
		memset(g.End() - 6, 'z', 6);
		at -= 6;
		uintptr_t names[kCount] = {};
		for (int i = kCount - 1; i >= 0; --i) names[i] = kBones[i].name ? put(kBones[i].name) : endless;
		const uintptr_t heroPath = put("data/hd/global/models/character/player/amazon/amazon_lit.model");
		const uintptr_t monsterPath = put("data/hd/global/models/monsters/zombie/zombie.model");
		const uintptr_t itemPath = put("data/hd/global/models/items/weapon/axe/axe.model");
		// one name across the line between the two pages
		char* across = g.base + Guarded::kPage - 9;
		memcpy(across, kBones[10].name, strlen(kBones[10].name) + 1);
		names[10] = (uintptr_t)across;
		for (int i = 0; i < kCount; ++i) {
			unsigned char* b = bones.data() + (size_t)i * 0xA4;
			memcpy(b, &names[i], 8);
			memcpy(b + 8, &kBones[i].parent, 4);
			const Mat w = Rows(kBones[i].x, kBones[i].y, kBones[i].z);
			const Mat inverse = InvAffine(w);   // the bind pose as granny keeps it: InverseWorld4x4 at +0x50
			memcpy(b + 0x50, inverse.m, 64);
			world[i] = w;   // standing in its bind pose
		}
		fw.n = kCount; fw.mats = (uintptr_t)world.data();
		sk.name = 0; sk.count = kCount; sk.bones = (uintptr_t)bones.data();
		model.skeleton = (uintptr_t)&sk;
		heroName.path = heroPath; monsterName.path = monsterPath; itemName.path = itemPath;
		for (FakeSelf* s : {&hero, &hero2, &menu}) *s = FakeSelf{(uintptr_t)&heroName, (uintptr_t)&model, {}, (uintptr_t)&fw};
		monster = FakeSelf{(uintptr_t)&monsterName, (uintptr_t)&model, {}, (uintptr_t)&fw};
		item = FakeSelf{(uintptr_t)&itemName, (uintptr_t)&model, {}, (uintptr_t)&fw};
	}
	static Mat Rows(float x, float y, float z) {
		Mat m{};
		m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
		m.m[12] = x; m.m[13] = y; m.m[14] = z;
		return m;
	}
};

void Learned() {
	std::printf("Learn on a hero rig whose names lie across pages\n");
	Rig r;
	Hero h;
	CHECK(Learn((uintptr_t)&r.hero, &h));
	CHECK(h.count == kCount && h.skel == (uintptr_t)&r.sk);
	CHECK(h.cls == 0);   // amazon
	CHECK(h.upr[0] == 7 && h.upr[1] == 14);
	CHECK(h.lwr[0] == 8 && h.lwr[1] == 15);
	CHECK(h.hand[0] == 10 && h.hand[1] == 16);   // (10: the name across the page line; 29's not printable)
	CHECK(h.ind[0] == 11 && h.ind[1] == 17);
	CHECK(h.mid[0] == -1 && h.pnk[0] == -1);
	CHECK(h.head == 4 && h.spine == 2 && h.pelvis == 1 && h.neck == 3);
	CHECK(h.eyes[0] == 5 && h.eyes[1] == 6);
	CHECK(h.weapon[0] == 13 && h.weapon[1] == 19);
	CHECK(h.shield == 20 && h.twoHand == 27);
	CHECK(h.leg[0][0] == 21 && h.leg[0][1] == 22 && h.leg[0][2] == 23);
	CHECK(h.leg[1][0] == 24 && h.leg[1][1] == 25 && h.leg[1][2] == 26);
	CHECK(h.twist[0].size() == 1 && h.twist[0][0].bone == 9 && h.twist[1].empty());
	CHECK(h.fingers[0].size() == 1 && h.fingers[0][0].bones == std::vector<int>({11, 12}));
	CHECK(h.fingers[1].size() == 1 && h.fingers[1][0].bones == std::vector<int>({17, 18}));
	CHECK(h.bindOk);
	CHECK(std::fabs(h.eyeY - 6.1f) < 1e-4f);
	// every bone name as the letter-at-a-time loop reads it
	CHECK((int)h.boneNames.size() == kCount);
	for (int i = 0; i < kCount && i < (int)h.boneNames.size(); ++i) {
		uintptr_t nm = 0;
		memcpy(&nm, r.bones.data() + (size_t)i * 0xA4, 8);
		CHECK(h.boneNames[i] == StrLetters(nm));
	}
	CHECK(h.boneNames[28].empty() && h.boneNames[29].empty());
	Hero not_;
	CHECK(!Learn((uintptr_t)&r.monster, &not_));   // not a player's model
}

std::shared_ptr<const Hero> Cached(const FakeSelf& s, const Rig& r, bool* found) {
	AcquireSRWLockShared(&g_heroLock);
	const auto it = g_heroes.find({(uintptr_t)&s, (uintptr_t)&r.sk});
	*found = it != g_heroes.end();
	std::shared_ptr<const Hero> h = *found ? it->second : nullptr;
	ReleaseSRWLockShared(&g_heroLock);
	return h;
}

void Cache() {
	std::printf("Arms' cache of the rigs\n");
	Rig r;
	TrackHero(true);   // (the hero is looked for with nothing done to him: facing)
	bool found = false;
	Arms((uintptr_t)&r.hero);
	const std::shared_ptr<const Hero> first = Cached(r.hero, r, &found);
	CHECK(found && first && first->count == kCount && first->hand[0] == 10);
	CHECK(g_heroSelf.load() == (uintptr_t)&r.hero);
	Arms((uintptr_t)&r.hero);
	CHECK(Cached(r.hero, r, &found).get() == first.get());   // posed again: the same rig, not learned again
	Arms((uintptr_t)&r.monster);
	CHECK(Cached(r.monster, r, &found) == nullptr && found);   // not a hero: known as nothing
	// full: the rigs that are not heroes go, the hero stays
	AcquireSRWLockExclusive(&g_heroLock);
	for (uintptr_t i = 0; i < 4100; ++i) g_heroes[{0x100000 + i * 16, 0x200000}] = nullptr;
	ReleaseSRWLockExclusive(&g_heroLock);
	Arms((uintptr_t)&r.hero2);   // a rig not met yet, with the cache full
	CHECK(Cached(r.hero, r, &found).get() == first.get());
	CHECK(Cached(r.hero2, r, &found) != nullptr);
	AcquireSRWLockShared(&g_heroLock);
	const size_t left = g_heroes.size();
	ReleaseSRWLockShared(&g_heroLock);
	CHECK(left == 2);
	TrackHero(false);
	AcquireSRWLockExclusive(&g_heroLock); g_heroes.clear(); ReleaseSRWLockExclusive(&g_heroLock);
}

// [debug] doll_trace: the notes when the hero's rig is let go (its head down) and taken again; [hands] doll_rebuild: a rig no entity of the renderer's
// world holds is posed along with the hero's, not taken (BodyPlace's "no entity", from fake storages that read and an instance known to have none).
void DollNotes() {
	std::printf("the doll let go and taken again; a menu's model\n");
	Rig r;
	TrackHero(true);
	Input in;
	in.dollTrace = true;
	Set(in);
	DollTakeNote();
	Arms((uintptr_t)&r.hero);
	CHECK(g_heroSelf.load() == (uintptr_t)&r.hero);
	CHECK(DollTakeNote().empty());   // bound from the start: nothing to say
	r.world[4].m[13] = 1.0f;   // the head down (under a third of 5.9): a corpse
	Arms((uintptr_t)&r.hero);
	Arms((uintptr_t)&r.hero);
	std::string note = DollTakeNote();
	std::printf("    %s\n", note.c_str());
	CHECK(note.find("doll: let go") != std::string::npos && note.find("its head is down") != std::string::npos && note.find("posed as ours last") != std::string::npos);
	CHECK(note.find("doll: let go", 5) == std::string::npos);   // once, not every pose
	r.world[4].m[13] = 5.9f;
	Arms((uintptr_t)&r.hero);
	note = DollTakeNote();
	std::printf("    %s\n", note.c_str());
	CHECK(note.find("doll: taken again after") != std::string::npos && note.find("the same rig") != std::string::npos);
	in.dollTrace = false;
	Set(in);
	r.world[4].m[13] = 1.0f;
	Arms((uintptr_t)&r.hero);
	CHECK(DollTakeNote().empty());   // off: nothing noted
	r.world[4].m[13] = 5.9f;
	// the menu's model: the storages read (fake ones, their vtables as found), the instance known to have no entity; the hero's rig is ours
	static uintptr_t fakeX = 0x1111, fakeS = 0x2222;
	const ULONGLONG now = GetTickCount64();
	AcquireSRWLockExclusive(&ecs::g_lock);
	ecs::g_xs = (uintptr_t)&fakeX; ecs::g_vtX = 0x1111; ecs::g_ss = (uintptr_t)&fakeS; ecs::g_vtS = 0x2222;
	ecs::g_bodyEntity[(uintptr_t)&r.menu] = {ecs::kNoEntity, now};
	ReleaseSRWLockExclusive(&ecs::g_lock);
	AcquireSRWLockExclusive(&g_bodiesLock);
	g_ours = (uintptr_t)&r.hero;
	g_bodies[(uintptr_t)&r.hero] = Body{0, 0, 0, now, 10};
	ReleaseSRWLockExclusive(&g_bodiesLock);
	Hero h;
	CHECK(Learn((uintptr_t)&r.menu, &h));
	char why[320] = "";
	CHECK(OursNow((uintptr_t)&r.menu, h, why, sizeof why) == Doll::Along);   // posed along (drawn, maybe: a rig being made), not taken
	std::printf("    the menu's model: %s\n", why);
	CHECK(std::string(why).find("no entity of the renderer's world") != std::string::npos);
	g_dollRebuild.store(false);   // [hands] doll_rebuild=0: as before - the standing test alone (no look-at here)
	CHECK(OursNow((uintptr_t)&r.menu, h) == Doll::Ours);
	g_dollRebuild.store(true);
	AcquireSRWLockExclusive(&g_bodiesLock); g_ours = 0; g_bodies.clear(); ReleaseSRWLockExclusive(&g_bodiesLock);
	AcquireSRWLockExclusive(&ecs::g_lock);
	g_ours = 0; ecs::g_xs = ecs::g_ss = ecs::g_vtX = ecs::g_vtS = 0; ecs::g_bodyEntity.clear();
	ReleaseSRWLockExclusive(&ecs::g_lock);
	TrackHero(false);
	AcquireSRWLockExclusive(&g_heroLock); g_heroes.clear(); ReleaseSRWLockExclusive(&g_heroLock);
}

// A renderer's ECS of one entity, the hero's (5): its TransformComponent (local matrix, no parent) and its SkeletonComponent (the SkeletonInstance) - read as
// skeletons.cpp's ecs:: reads the game's (storage +0 vtable, +0x08 sparse pages, +0x20 packed entities, +0x38 components).
struct FakeEcs {
	struct Storage { uintptr_t vt; uintptr_t sparseB, sparseE, pad18; uintptr_t packedB, packedE, pad30; uintptr_t comps; };
	static constexpr uint32_t kEntity = 5;
	uint32_t page[256];
	uintptr_t pages[1];
	uint32_t packed[1] = {kEntity};
	alignas(16) unsigned char xform[kTransformSize] = {};
	uintptr_t skelComp[kSkeletonCompSize / 8] = {};
	Storage xs{}, ss{};
	FakeEcs() {
		for (uint32_t& v : page) v = 0xFFFFF;
		page[kEntity] = 0;
		pages[0] = (uintptr_t)page;
		for (Storage* st : {&xs, &ss}) {
			st->sparseB = (uintptr_t)pages; st->sparseE = (uintptr_t)(pages + 1);
			st->packedB = (uintptr_t)packed; st->packedE = (uintptr_t)(packed + 1);
		}
		xs.vt = 0x1111; ss.vt = 0x2222;
		xs.comps = (uintptr_t)xform; ss.comps = (uintptr_t)skelComp;
		float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		memcpy(xform, id, sizeof id);
		const uint32_t noParent = 0xFFFFFFFFu;
		memcpy(xform + 0xC0, &noParent, 4);
		AcquireSRWLockExclusive(&ecs::g_lock);
		ecs::g_xs = (uintptr_t)&xs; ecs::g_vtX = 0x1111; ecs::g_ss = (uintptr_t)&ss; ecs::g_vtS = 0x2222;
		ecs::g_bodyEntity.clear(); ecs::g_cachedSelf = 0;
		ReleaseSRWLockExclusive(&ecs::g_lock);
	}
	~FakeEcs() {
		AcquireSRWLockExclusive(&ecs::g_lock);
		ecs::g_xs = ecs::g_ss = ecs::g_vtX = ecs::g_vtS = 0; ecs::g_bodyEntity.clear(); ecs::g_cachedSelf = 0;
		ReleaseSRWLockExclusive(&ecs::g_lock);
	}
	void Holds(const void* self) { skelComp[0] = (uintptr_t)self; }   // the instance the hero's SkeletonComponent holds
	void At(float x, float z) {                                         // the hero stands here, the camera's look-at with him
		memcpy(xform + 48, &x, 4); memcpy(xform + 56, &z, 4);
		SetHeroPos(x, 0.0f, z);
	}
};

void FreshDoll(bool rebuild) {
	AcquireSRWLockExclusive(&g_bodiesLock); g_ours = 0; g_oursBefore = 0; g_bodies.clear(); ReleaseSRWLockExclusive(&g_bodiesLock);
	AcquireSRWLockExclusive(&g_heroLock); g_heroes.clear(); g_heroRetry.clear(); ReleaseSRWLockExclusive(&g_heroLock);
	Input in;
	in.dollTrace = true;
	in.dollRebuild = rebuild;
	Set(in);
	g_dollLetGo.store(false);
	g_dollAlongTold.store(0);
	DollTakeNote();
	TrackHero(true);
}

Doll Verdict(const FakeSelf& s) {
	Hero h;
	if (!Learn((uintptr_t)&s, &h)) return Doll::Not;
	return OursNow((uintptr_t)&s, h);
}

// A weapon swap: the hero's model rebuilt - a new SkeletonInstance (B) of the same entity, while the game still poses the old one (A) for a moment.
void Swap() {
	std::printf("a weapon swap: the hero's model rebuilt\n");
	{   // (1) the hero's SkeletonComponent holds B from its first pose; A is posed on: B taken, A posed along, the facing's rig stays B (no flip back)
		Rig r;
		FakeEcs e;
		FreshDoll(true);
		e.Holds(&r.hero); e.At(10.0f, 20.0f);
		Arms((uintptr_t)&r.hero); Arms((uintptr_t)&r.hero);   // (a rig is taken at its second pose: every rig of a frame known)
		CHECK(g_heroSelf.load() == (uintptr_t)&r.hero);
		DollTakeNote();
		e.Holds(&r.hero2);
		Arms((uintptr_t)&r.hero2);
		CHECK(g_heroSelf.load() == (uintptr_t)&r.hero2);
		const uint32_t gen = HeroGen();
		for (int i = 0; i < 4; ++i) { Arms((uintptr_t)&r.hero); Arms((uintptr_t)&r.hero2); }   // the game poses both
		CHECK(g_heroSelf.load() == (uintptr_t)&r.hero2 && HeroGen() == gen);   // the facing's rig did not go back and forth
		CHECK(Verdict(r.hero) == Doll::Along && Verdict(r.hero2) == Doll::Ours);
		Sleep(130);   // A no longer posed
		Arms((uintptr_t)&r.hero2);
		const std::string note = DollTakeNote();
		std::printf("%s\n", note.c_str());
		CHECK(note.find("model rebuilt") != std::string::npos && note.find("posed along") != std::string::npos && note.find("posed together for") != std::string::npos);
		CHECK(note.find("let go") == std::string::npos);
	}
	{   // (2) B posed before the SkeletonComponent holds it (no entity yet), the hero running; then it holds B
		for (int pass = 0; pass < 2; ++pass) {
			const bool rebuild = pass == 0;
			Rig r;
			FakeEcs e;
			FreshDoll(rebuild);
			e.Holds(&r.hero); e.At(10.0f, 20.0f);
			Arms((uintptr_t)&r.hero); Arms((uintptr_t)&r.hero);
			DollTakeNote();
			Arms((uintptr_t)&r.hero2);   // no entity holds it yet
			if (rebuild) CHECK(g_heroSelf.load() == (uintptr_t)&r.hero && Verdict(r.hero2) == Doll::Along);   // along; the facing stays on A
			Arms((uintptr_t)&r.hero);
			Sleep(60);
			e.Holds(&r.hero2); e.At(11.5f, 20.0f);   // 1.5 units on in those 60 ms (a quick run)
			Arms((uintptr_t)&r.hero2);
			Arms((uintptr_t)&r.hero2);
			if (!rebuild) { Sleep(460); Arms((uintptr_t)&r.hero2); }   // as before: its entity looked for again only after 500 ms
			const std::string note = DollTakeNote();
			std::printf("  [hands] doll_rebuild=%d:\n%s\n", rebuild ? 1 : 0, note.c_str());
			if (rebuild) {
				CHECK(g_heroSelf.load() == (uintptr_t)&r.hero2);   // taken: its entity looked for again after 50 ms, the rebuild within the run's reach
				CHECK(Verdict(r.hero) == Doll::Along);
				CHECK(note.find("let go") == std::string::npos);
			} else {
				CHECK(note.find("let go") != std::string::npos);   // as before: B's entity cached as none for 500 ms, then 1.5 units off A - not its rebuild
			}
		}
	}
	TrackHero(false);
	FreshDoll(true);
	TrackHero(false);
}

// A player's rig met before its bones read (a model being made): learned again after 100 ms.
void Relearn() {
	std::printf("a player's rig met before its bones read\n");
	Rig r;
	FreshDoll(true);
	std::vector<unsigned char> keep = r.bones;
	for (int i = 0; i < kCount; ++i) memset(r.bones.data() + (size_t)i * 0xA4, 0, 8);   // no names yet
	Arms((uintptr_t)&r.hero);
	bool found = false;
	CHECK(Cached(r.hero, r, &found) == nullptr && found);
	memcpy(r.bones.data(), keep.data(), keep.size());
	Arms((uintptr_t)&r.hero);
	CHECK(Cached(r.hero, r, &found) == nullptr);   // not before 100 ms
	Sleep(110);
	Arms((uintptr_t)&r.hero);
	CHECK(Cached(r.hero, r, &found) != nullptr);
	const std::string note = DollTakeNote();
	std::printf("%s\n", note.c_str());
	CHECK(note.find("not learned") != std::string::npos && note.find("learned on its try 2") != std::string::npos);
	FreshDoll(true);
	TrackHero(false);
}

void OtherCaches() {
	std::printf("the bow's, the flail's and the crossbow's caches\n");
	Rig r;
#if D2RVR_FIRST_PERSON
	g_bowStringOn.store(true);
	g_flailOn.store(true);
	g_xbowBarrelOn.store(true);
	g_heroSelf.store(0);
	for (int pass = 0; pass < 3; ++pass)
		for (const FakeSelf* s : {&r.hero, &r.monster, &r.item}) {
			PoseBowRig((uintptr_t)s);
			PoseFlail((uintptr_t)s);
			NoteCrossbowBarrel((uintptr_t)s);
		}
	const auto key = [&](const FakeSelf& s) { return std::make_pair((uintptr_t)&s, (uintptr_t)&r.sk); };
	CHECK(g_bowRigs.size() == 3 && g_flails.size() == 3 && g_xbowRigs.size() == 3);
	for (const FakeSelf* s : {&r.hero, &r.monster, &r.item}) {
		CHECK(g_bowRigs.count(key(*s)) && !g_bowRigs[key(*s)].isBow);
		CHECK(g_flails.count(key(*s)) && !g_flails[key(*s)].flail);
		CHECK(g_xbowRigs.count(key(*s)) && !g_xbowRigs[key(*s)].ok);
	}
	CHECK(KnownNot(g_bowRigLock, g_bowRigs, key(r.item), (uintptr_t)r.bones.data(), [](const BowRigEntry& e) { return !e.isBow; }));
	CHECK(!KnownNot(g_bowRigLock, g_bowRigs, key(r.item), (uintptr_t)r.bones.data() + 1, [](const BowRigEntry& e) { return !e.isBow; }));   // other bones: learned again
	{   // full: the rigs that are none of theirs go; a bow, a flail, a crossbow stays
		BowRigEntry bow; bow.isBow = true; bow.bones = 7;
		g_bowRigs[{1, 1}] = bow;
		FlailEntry flail; flail.bones = 7; flail.flail = std::make_unique<FlailState>();
		g_flails[{1, 1}] = std::move(flail);
		XbowRigEntry xbow; xbow.ok = true; xbow.bones = 7;
		g_xbowRigs[{1, 1}] = xbow;
		for (uintptr_t i = 0; i < 4100; ++i) { g_bowRigs[{0x100000 + i * 16, 2}] = BowRigEntry{}; g_flails[{0x100000 + i * 16, 2}] = FlailEntry{}; g_xbowRigs[{0x100000 + i * 16, 2}] = XbowRigEntry{}; }
		PoseBowRig((uintptr_t)&r.hero2); PoseFlail((uintptr_t)&r.hero2); NoteCrossbowBarrel((uintptr_t)&r.hero2);
		CHECK(g_bowRigs.size() == 2 && g_bowRigs.count({1, 1}) && g_bowRigs.count(key(r.hero2)));
		CHECK(g_flails.size() == 2 && g_flails.count({1, 1}) && g_flails.count(key(r.hero2)));
		CHECK(g_xbowRigs.size() == 2 && g_xbowRigs.count({1, 1}) && g_xbowRigs.count(key(r.hero2)));
	}
	g_bowRigs.clear(); g_flails.clear(); g_xbowRigs.clear();
	g_bowStringOn.store(false); g_flailOn.store(false); g_xbowBarrelOn.store(false);
#else
	std::printf("  (not a first-person build: skipped)\n");
#endif
}

}  // namespace tst
}  // namespace skel

int main() {
	using namespace skel::tst;
	std::printf("skeleton_discovery_test (what the skeleton hook finds, against how it was found before)\n");
	Names();
	Learned();
	Cache();
	DollNotes();
	Swap();
	Relearn();
	OtherCaches();
	std::printf(g_fails == 0 ? "skeleton_discovery_test: all passed\n" : "skeleton_discovery_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
