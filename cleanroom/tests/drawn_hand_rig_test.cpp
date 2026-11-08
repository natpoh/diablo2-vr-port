// The skeleton hook ITSELF (vr/skeletons.cpp, included whole) on a toy rig in fake game memory: [hands] left_slot_drawn_left (vr/drawn_hand.h). A weapon alone in the LEFT slot,
// which the game hangs on the RIGHT attach bone: the right bone ends where the left slot's bone is posed (the hand holding it), in its own handedness and drawn size; a shield in
// the right slot goes to the right forearm while the weapon is not in the right hand alone, to the left forearm when it is; the game hiding the right bone, or the box off, carries
// nothing; the touch's weapon bone (the cast/gun bone off the left slot) stays on the drawn weapon, sized even where the game shrank the left bone. The rig scaffolding is
// shield_arm_rig_test's (a symmetric toy body, the left attach bone a mirror).
//
//   drawn_hand_rig_test

#include "../../vr/skeletons.cpp"

#include <cstdio>
#include <vector>

namespace skel {
namespace tst {

int g_fails = 0;
#define CHECK(c)                                                \
	do {                                                        \
		if (!(c)) {                                             \
			++g_fails;                                          \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c);  \
		}                                                       \
	} while (0)

constexpr int kCount = 4 + 2 * 6;   // root, pelvis, spine, head; then per side: upper arm, forearm, hand, three finger roots, attach
enum Part { kUpr, kLwr, kHand, kInd, kMid, kPnk, kAtt, kPartCount = 7 };
constexpr int kFirst = 4;
constexpr int B(int side, int part) { return kFirst + side * kPartCount + part; }
constexpr int kBones = kFirst + 2 * kPartCount;
constexpr int kShd = kBones;       // "shield_attach": one more bone, under the left forearm (the rig has it; the hero knows it only where a test says so: Rig::UseShieldAttach)
constexpr int kAll = kBones + 1;

int Parent(int i) {
	if (i == kShd) return B(1, kLwr);
	static const int top[4] = {-1, 0, 1, 2};
	if (i < kFirst) return top[i];
	const int side = (i - kFirst) / kPartCount, part = (i - kFirst) % kPartCount;
	switch (part) {
	case kUpr: return 2;
	case kLwr: return B(side, kUpr);
	case kHand: return B(side, kLwr);
	default: return B(side, kHand);
	}
}

Mat Rows(V3 x, V3 y, V3 z, V3 p) {
	Mat m{};
	m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
	m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
	m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
	m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z; m.m[15] = 1.0f;
	return m;
}
Mat At(V3 p) { return Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, p); }

// The animation's world poses; the attach bones' scale per side (1 shown, 0.001 hidden).
std::vector<Mat> Animation(float scaleRight, float scaleLeft, float rollRight = 0.0f) {
	std::vector<Mat> w(kAll);
	w[kShd] = Rows({-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.78f, 1.3f, 0.05f});   // (on the left forearm, a mirror like the left attach bone: det -1)
	w[0] = At({0, 0, 0});
	w[1] = At({0, 3.5f, 0});
	w[2] = At({0, 4.6f, 0});
	w[3] = At({0, 5.9f, 0});
	for (int side = 0; side < 2; ++side) {
		const float sx = side == 0 ? -1.0f : 1.0f, mir = side == 0 ? 1.0f : -1.0f;
		w[B(side, kUpr)] = At({sx * 0.7f, 5.3f, 0});
		// the forearm's own axes: the right bone proper, the left one the mirror image with its axes relabelled (x and z swapped), as a rig does
		w[B(side, kLwr)] = side == 0 ? Rows({1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {sx * 0.7f, 2.7f, 0}) : Rows({0, 0, 1}, {0, -1, 0}, {1, 0, 0}, {sx * 0.7f, 2.7f, 0});
		const V3 hp{sx * 0.7f, 0.1f, 0};
		w[B(side, kHand)] = Rows({mir, 0, 0}, {0, 1, 0}, {0, 0, 1}, hp);
		w[B(side, kMid)] = At({hp.x, hp.y - 0.3f, hp.z});
		w[B(side, kInd)] = At({hp.x, hp.y - 0.29f, hp.z + 0.12f});
		w[B(side, kPnk)] = At({hp.x, hp.y - 0.27f, hp.z - 0.12f});
		// the attach bone: 7 cm toward the body from the hand, the left one a mirror (det < 0), the right one proper
		const float s = side == 0 ? scaleRight : scaleLeft;
		const float lean = 0.31f, cs = std::cos(lean), sn = std::sin(lean);
		const V3 y{0, sn, cs}, x{mir, 0, 0}, z{0, -cs, sn};   // (x mirrored, z not: the left bone's det is -1, the right one's +1)
		w[B(side, kAtt)] = Rows({x.x * s, x.y * s, x.z * s}, {y.x * s, y.y * s, y.z * s}, {z.x * s, z.y * s, z.z * s}, {hp.x - sx * 0.07f, hp.y - 0.15f, hp.z});
		if (side == 0 && rollRight != 0.0f) {   // the game's grip of the weapon in the RIGHT hand: rolled about the shaft (its y) - the left fist's attach bone keeps the bind's
			const float t = rollRight * 0.01745329f, c = std::cos(t), sn = std::sin(t);
			Mat r{};
			r.m[0] = c; r.m[2] = -sn; r.m[5] = 1; r.m[8] = sn; r.m[10] = c; r.m[15] = 1;
			const Mat a = w[B(0, kAtt)];
			w[B(0, kAtt)] = Mul(r, a);
		}
	}
	return w;
}

Hero MakeHero(const std::vector<Mat>& bind) {
	Hero h;
	h.skel = 1;
	h.count = kAll;
	for (int i = 0; i < kAll; ++i) h.parent.push_back(Parent(i));
	h.inHead.assign(kAll, 0);
	h.inNeck.assign(kAll, 0);
	h.upper.assign(kAll, 0);
	h.inHand.assign(kAll, 0);
	for (int side = 0; side < 2; ++side) {
		h.weapon[side] = B(side, kAtt);
		h.upr[side] = B(side, kUpr); h.lwr[side] = B(side, kLwr); h.hand[side] = B(side, kHand);
		h.ind[side] = B(side, kInd); h.mid[side] = B(side, kMid); h.pnk[side] = B(side, kPnk);
	}
	h.head = 3; h.spine = 2; h.pelvis = 1;
	h.eyeY = 5.9f;
	h.pelvisY = 3.5f;
	h.bind = bind;
	h.bindOk = true;
	for (int i = 0; i < kAll; ++i) {
		h.upper[i] = i >= 2 ? 1 : 0;
		const int p = h.parent[i];
		h.inHand[i] = (p >= 0 && (p == h.hand[0] || p == h.hand[1] || h.inHand[p])) ? 1 : 0;
	}
	return h;
}

#pragma pack(push, 1)
struct FakeWorld { int32_t n; uintptr_t mats; };
#pragma pack(pop)
struct FakeSelf { char pad[0x20]; uintptr_t world; };

double RowLen(const Mat& m, int r) { return std::sqrt((double)m.m[r * 4] * m.m[r * 4] + (double)m.m[r * 4 + 1] * m.m[r * 4 + 1] + (double)m.m[r * 4 + 2] * m.m[r * 4 + 2]); }
double Det(const Mat& m) {
	const float* q = m.m;
	return (double)q[0] * (q[5] * q[10] - q[6] * q[9]) - (double)q[1] * (q[4] * q[10] - q[6] * q[8]) + (double)q[2] * (q[4] * q[9] - q[5] * q[8]);
}
bool Finite(const Mat& m) {
	for (int i = 0; i < 16; ++i)
		if (!std::isfinite(m.m[i])) return false;
	return true;
}
bool Near(V3 a, V3 b, float e) { return Len(Sub(a, b)) < e; }
V3 Row(const Mat& m, int r) { return {m.m[r * 4], m.m[r * 4 + 1], m.m[r * 4 + 2]}; }

struct Rig {
	std::vector<Mat> anim, bind, mats;
	Hero hero;
	FakeSelf self{};
	FakeWorld world{};
	Input in;
	uint32_t sample = 0;
	Rig(float scaleRight = 1.0f, float scaleLeft = 1.0f, float rollRight = 0.0f)
	    : anim(Animation(scaleRight, scaleLeft, rollRight)), bind(Animation(1.0f, 1.0f)), hero(MakeHero(bind)) {
		mats = anim;
		world.n = kAll;
		world.mats = (uintptr_t)mats.data();
		self.world = (uintptr_t)&world;
		in.mode = 2;
		in.handsValid = 3;
		in.wrist = true;
		in.haveEye = true;
		in.eyeModel[1] = 5.9f;
		in.haveYaw = true;
		in.userHeadM = 5.9f / 3.5f;
		in.holdGrip = true;
		in.headRoom[1] = 1.7f;
		in.grip[0] = in.grip[1] = 0.0f;
		for (auto& r : in.rot) r[3] = 1.0f;
		in.weaponSide = 1;          // gamestate::WeaponHand: the kind came from the left slot
		in.leftSlotDrawn = true;    // vrcam: F4, the box ticked, drawnhand::Decide says carry
		hero.shield = kShd;         // the rig's shield_attach (the game hangs a shield there)
		Hand(0, {0.2f, -0.40f, -0.30f});
		Hand(1, {-0.2f, -0.40f, -0.30f});
	}
	void Hand(int side, V3 p) { in.hand[side][0] = p.x; in.hand[side][1] = p.y; in.hand[side][2] = p.z; }
	void Pose() {
		++sample;
		in.sample = sample;
		Input c = in;
		weight::Out none;
		mats = anim;
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, none);
	}
	// any hold of the weight model's
	void PoseOut(const weight::Out& wo) {
		++sample;
		in.sample = sample;
		Input c = in;
		mats = anim;
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, wo);
	}
	// the weight model holding the left slot's weapon: in the left hand (as the game's slot), or passed into the right hand alone
	void PoseWeight(bool rightHandAlone) {
		++sample;
		in.sample = sample;
		Input c = in;
		weight::Out wo;
		wo.on = true;
		wo.slot = 1;
		wo.main = rightHandAlone ? 0 : 1;
		wo.asPlaced = !rightHandAlone;
		wo.holds[rightHandAlone ? 0 : 1] = true;
		wo.on01[rightHandAlone ? 0 : 1] = 1.0f;
		mats = anim;
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, wo);
	}
};

}  // namespace tst
}  // namespace skel

using namespace skel;
using namespace skel::tst;

int main() {
	const int L = B(1, kAtt), R = B(0, kAtt), LH = B(1, kHand), RH = B(0, kHand);
	const auto nearer = [](const Mat& bone, const Mat& a, const Mat& b) { return Len(Sub(Pos(bone), Pos(a))) < Len(Sub(Pos(bone), Pos(b))); };

	// ---- a sword alone in the left slot, nothing in the right (no weight model): the right bone carried onto the left one's pose ----
	{
		Rig r;
		r.Pose();
		CHECK(Finite(r.mats[R]) && Finite(r.mats[L]));
		CHECK(Near(Pos(r.mats[R]), Pos(r.mats[L]), 1e-4f));            // where the left slot's bone is
		CHECK(nearer(r.mats[R], r.mats[LH], r.mats[RH]));              // in the left hand
		CHECK(Det(r.mats[R]) > 0.0 && Det(r.mats[L]) < 0.0);           // each its own handedness: the item on the right bone never inside out
		for (int k = 0; k < 3; ++k) CHECK(std::fabs(RowLen(r.mats[R], k) - 1.0) < 0.01);   // the size the game draws at
		CHECK(Near(Row(r.mats[R], 1), Row(r.mats[L], 1), 1e-3f));       // the blade's line: the left bone's
		CHECK(Near(Row(r.mats[R], 2), Row(r.mats[L], 2), 1e-3f));
		CHECK(Near(Row(r.mats[R], 0), Scale(Row(r.mats[L], 0), -1.0f), 1e-3f));   // only x turned over
		CastPose cp;
		CHECK(CastPoseNow(&cp, 1000) && cp.weapon && cp.weaponSide == 1);   // the touch's / the aim's weapon bone: the left slot's
	}

	// ---- the box off: the right bone stays the game's, in the right hand ----
	{
		Rig r;
		r.in.leftSlotDrawn = false;
		r.Pose();
		CHECK(nearer(r.mats[R], r.mats[RH], r.mats[LH]));
		CHECK(!Near(Pos(r.mats[R]), Pos(r.mats[L]), 0.05f));
	}

	// ---- the game does not show its right bone (it hangs the weapon on the left one, or hid it): nothing carried, it stays a point ----
	{
		Rig r(0.001f, 1.0f);
		r.Pose();
		CHECK(RowLen(r.mats[R], 0) < 0.01);
		CHECK(!Near(Pos(r.mats[R]), Pos(r.mats[L]), 0.05f));
	}

	// ---- the game shrank the LEFT bone (it hangs nothing there): the right bone carried at its own size, the weapon bone sized for the touch ----
	{
		Rig r(1.0f, 0.001f);
		r.Pose();
		CHECK(Near(Pos(r.mats[R]), Pos(r.mats[L]), 1e-4f));
		for (int k = 0; k < 3; ++k) CHECK(std::fabs(RowLen(r.mats[R], k) - 1.0) < 0.01);
		CastPose cp;
		CHECK(CastPoseNow(&cp, 1000) && cp.weapon && cp.weaponSide == 1);   // (off the left bone at 0.001 it was no weapon at all)
		float o[3], ax[3][3];
		CHECK(GunBone(o, ax));
		for (int k = 0; k < 3; ++k) CHECK(std::fabs(std::sqrt(ax[k][0] * ax[k][0] + ax[k][1] * ax[k][1] + ax[k][2] * ax[k][2]) - 1.0f) < 1e-3f);
	}

	// ---- the weight model holds it in the left hand: carried there too ----
	{
		Rig r;
		r.PoseWeight(false);
		CHECK(Near(Pos(r.mats[R]), Pos(r.mats[L]), 1e-4f));
		CHECK(nearer(r.mats[R], r.mats[LH], r.mats[RH]));
		CHECK(Det(r.mats[R]) > 0.0);
	}

	// ---- a shield in the right slot: strapped to the RIGHT forearm, mirrored, while the weapon is in the left hand ----
	{
		Rig r;
		r.in.leftSlotShieldRight = true;
		r.PoseWeight(false);
		CHECK(g_shieldStrapped.load() == 3);
		CHECK(Pos(r.mats[kShd]).x < 0.0f);                              // on the right side of the body (the toy's right is -x)
		CHECK(Det(r.mats[kShd]) < 0.0);                                 // the shield bone's own handedness
		CHECK(nearer(r.mats[R], r.mats[LH], r.mats[RH]));              // the weapon still in the left hand (the strap moved the left side's shield bones, not it)
		// without the weight model the slot's hand holds it: the same
		Rig q;
		q.in.leftSlotShieldRight = true;
		q.Pose();
		CHECK(g_shieldStrapped.load() == 3 && Pos(q.mats[kShd]).x < 0.0f);
		CHECK(nearer(q.mats[R], q.mats[LH], q.mats[RH]));
	}

	// ---- ... passed into the right hand alone: the shield on the left forearm (the game's arm), strapped ----
	{
		Rig r;
		r.in.leftSlotShieldRight = true;
		r.PoseWeight(true);
		CHECK(g_shieldStrapped.load() == 1);
		CHECK(Pos(r.mats[kShd]).x > 0.0f);
		CHECK(Finite(r.mats[R]) && Det(r.mats[R]) > 0.0 && std::fabs(RowLen(r.mats[R], 1) - 1.0) < 0.01);   // the right bone keeps the weapon's pose ...
		CHECK(!Near(Pos(r.mats[R]), Pos(r.mats[L]), 0.01f));          // ... while the left side's bones wear the shield on the left forearm
	}

	// ---- the game's grip is the right hand's (rolled 30 deg about the shaft); the left fist's attach bone in that animation holds nothing (the bind's): the left slot's
	//      bone is the right grip mirrored onto the left hand - not the idle attach bone 30 deg off it - and the drawn bone follows it ----
	{
		for (int weight = 0; weight < 2; ++weight) {
			Rig r(1.0f, 1.0f, 30.0f);
			r.in.weaponType = 4100 + weight;   // (a weapon type of its own: the grip is taken afresh from this animation)
			if (weight) r.PoseWeight(false); else r.Pose();
			const Mat leftLocal = Mul(r.mats[L], InvAffine(r.mats[LH]));
			const Mat idle = Mul(r.anim[L], InvAffine(r.anim[LH]));
			weaponmirror::K kh, kw;
			CHECK(weaponmirror::FromMirrorPair(r.bind[RH].m, r.bind[LH].m, &kh) && weaponmirror::FromMirrorPair(r.bind[R].m, r.bind[L].m, &kw));
			Mat want{};
			CHECK(drawnhand::MirrorGrip(Mul(r.anim[R], InvAffine(r.anim[RH])).m, kw, kh, want.m));
			CHECK(drawnhand::GripTurnDeg(leftLocal.m, want.m) < 0.5);                       // the right grip mirrored
			CHECK(std::fabs(drawnhand::GripTurnDeg(leftLocal.m, idle.m) - 30.0) < 0.5);    // (the idle attach bone was 30 deg off it)
			CHECK(drawnhand::GripApart(leftLocal.m, want.m) < 1e-3);
			CHECK(Det(r.mats[L]) < 0.0 && Det(r.mats[R]) > 0.0);                            // the handedness of each
			CHECK(Near(Pos(r.mats[R]), Pos(r.mats[L]), 1e-4f) && Near(Row(r.mats[R], 1), Row(r.mats[L], 1), 1e-3f));   // drawn where held, along the same shaft
			// the mirror image: the left weapon's shaft against the left hand is the right weapon's against the right hand, reflected in x
			const Mat rightLocal = Mul(r.anim[R], InvAffine(r.anim[RH]));
			CHECK(std::fabs(leftLocal.m[4] + rightLocal.m[4]) < 1e-3f && std::fabs(leftLocal.m[5] - rightLocal.m[5]) < 1e-3f && std::fabs(leftLocal.m[6] - rightLocal.m[6]) < 1e-3f);
		}
		// the box off: the left slot's grip is the animation's own, as before
		Rig q(1.0f, 1.0f, 30.0f);
		q.in.weaponType = 4102;
		q.in.leftSlotDrawn = false;
		q.Pose();
		CHECK(drawnhand::GripTurnDeg(Mul(q.mats[L], InvAffine(q.mats[LH])).m, Mul(q.anim[L], InvAffine(q.anim[LH])).m) < 0.5);
	}

	// ---- the grip table ([debug] grip_trace's state, skel::GripStateNow): a sword, an axe, a staff, a spear in either slot, the first hand the slot's, then the other hand takes it too,
	//      the weapon passed into the other hand alone, let go, called back into the slot's hand - at every step the bone the game draws it on, the touch's blade bone, the hand the
	//      fingers' grip pose is read off and the attach bone agree with the hand that holds it ----
	{
		struct Kind { const char* name; int type; uint32_t kind; bool two; };
		const Kind kinds[] = { { "sword", 5001, D2RVR_TYPE_SWORD, false }, { "axe", 5002, D2RVR_TYPE_AXE, false }, { "staff", 5003, D2RVR_TYPE_STAFF, true }, { "spear", 5004, D2RVR_TYPE_SPEAR, true } };
		int rows = 0;
		for (const Kind& k : kinds)
			for (int slot = 0; slot < 2; ++slot) {
				// the game's slots: a two-handed weapon in either slot is in both hands (left_slot_drawn_left leaves it to [hands] both_attach), a one-handed one is carried from the left
				drawnhand::Slots sl;
				sl.kind[slot] = k.kind;
				if (k.two) sl.two = D2RVR_TWO_HANDED | D2RVR_TWO_HANDED_KNOWN | D2RVR_TWO_HANDS_ON;   // (gamestate: in both hands whenever the other slot is empty, whichever slot)
				const drawnhand::Plan plan = drawnhand::Decide(sl);
				if (k.two) CHECK(!plan.carry);   // (a two-handed weapon in both hands: nothing carried, whichever slot)
				if (k.two && slot == 1) continue;   // (its two-handed drawing is both_attach's, not this table's)
				Rig r;
				r.in.weaponType = k.type + slot * 10;
				r.in.weaponSide = slot;
				r.in.leftSlotDrawn = plan.carry;
				const int other = slot ^ 1;
				const V3 home[2] = { { 0.2f, -0.40f, -0.30f }, { -0.2f, -0.40f, -0.30f } };
				weight::Out wo;
				wo.on = true;
				wo.slot = slot;
				struct Step { const char* name; bool holds[2]; int main; bool asPlaced, free; V3 at[2]; };
				const V3 beside = { home[slot].x * 0.6f, home[slot].y + 0.04f, home[slot].z };   // the other hand closed beside the first one on the weapon
				const V3 away = { home[other].x * 1.4f, home[other].y - 0.10f, home[other].z + 0.10f };
				Step steps[5] = {};
				steps[0] = { "placed in the slot's hand", {}, slot, true, false, { home[0], home[1] } };
				steps[0].holds[slot] = true;
				steps[1] = { "the other hand takes it too", {}, slot, false, false, { home[0], home[1] } };
				steps[1].holds[0] = steps[1].holds[1] = true;
				steps[1].at[other] = beside;
				steps[2] = { "passed into the other hand alone", {}, other, false, false, { home[0], home[1] } };
				steps[2].holds[other] = true;
				steps[2].at[other] = away;
				steps[3] = { "let go", {}, other, false, true, { home[0], home[1] } };
				steps[4] = { "called back into the slot's hand", {}, slot, true, false, { home[0], home[1] } };
				steps[4].holds[slot] = true;
				for (int st = 0; st < 5; ++st) {
					const Step& sp = steps[st];
					wo.main = sp.main;
					wo.asPlaced = sp.asPlaced;
					wo.free = sp.free;
					for (int sd = 0; sd < 2; ++sd) {
						wo.holds[sd] = sp.holds[sd];
						wo.on01[sd] = sp.holds[sd] ? 1.0f : 0.0f;
						r.Hand(sd, sp.at[sd]);
					}
					if (st >= 1) wo.grabs[other] = 1;   // (the other hand's take: its relation is the one taken then)
					if (st == 4) wo.grabs[slot] = 1;
					r.PoseOut(wo);
					r.PoseOut(wo);   // (a second pose: the relations taken in the first are used)
					const GripState g = GripStateNow();
					const char* where = sp.name;
					CHECK(g.on && g.slot == slot);
					CHECK(g.carried == plan.carry);
					CHECK(g.drawnBone == (plan.carry ? 0 : slot));               // the bone the game draws on: the right one for a lone weapon, whatever its slot
					CHECK(g.bladeBone == slot);                                  // the touch's blade: the slot's bone (the drawn weapon is on its pose)
					CHECK(g.poseHand == (plan.carry ? 0 : slot));                // the fingers' grip pose: the hand the game grips it in
					CHECK(g.gripFrom == (plan.carry ? 2 : 1));                   // the slot's bone's grip: the game's own, or its right-hand grip mirrored
					CHECK(Finite(r.mats[R]) && Finite(r.mats[L]));
					if (plan.carry) CHECK(Near(Pos(r.mats[R]), Pos(r.mats[L]), 1e-4f) && Det(r.mats[R]) > 0.0);   // drawn where the slot's bone is, never inside out
					if (!sp.free) {
						if (!(g.drawnNear && g.bladeNear)) std::printf("  grip table: %s, slot %s, %s - drawn %s, blade %s the holding (%s) hand\n", k.name, slot ? "left" : "right", where,
						                                                g.drawnNear ? "at" : "AWAY FROM", g.bladeNear ? "at" : "AWAY FROM", g.holder ? "left" : "right");
						CHECK(g.drawnNear && g.bladeNear);                       // the drawing and the hitbox in the hand that holds it
						CHECK(g.holder == (sp.holds[0] && sp.holds[1] ? sp.main : sp.holds[0] ? 0 : 1));
					}
					++rows;
				}
			}
		CHECK(rows == 6 * 5);
		std::printf("  grip table: %d rows (sword, axe in either slot; staff, spear in the right) - drawn bone, blade bone, grip pose hand, attach bone and holding hand agree\n", rows);
	}

	// ---- no shield to move: the shield bones are the game's ----
	{
		Rig r;
		r.Pose();
		CHECK(g_shieldStrapped.load() == 0);
	}

	std::printf(g_fails == 0 ? "drawn_hand_rig_test: the right bone carried onto the left slot's pose (place, axes, handedness, drawn size), the box off / the bone hidden left alone, the weapon bone sized, the right slot's shield on the arm opposite the weapon, the left slot's grip the game's right grip mirrored, the grip table: all passed\n"
	                         : "drawn_hand_rig_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
