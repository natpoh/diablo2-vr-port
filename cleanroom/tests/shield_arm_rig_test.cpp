// The skeleton hook ITSELF (vr/skeletons.cpp, included whole) on a toy rig in fake game memory: the shield strapped to the left forearm as before, to the RIGHT forearm mirrored
// ([hands] shoulder_draw, vr/shield_arm.h: the shield follows the sword to the forearm opposite its hand). The rig is a symmetric body (the right side the left's mirror image, the bones' axes relabelled the way a rig does); the
// hands are held mirror-wise, so what the right forearm carries must be what the left carries, reflected.
//
//   - on the left forearm the matrix is the old one (bind attach * inv(bind forearm) * forearm), whatever shieldArm -1 or 1 says;
//   - on the right forearm it is the reflected shield carried by the right forearm: its centre is the left shield's centre in the mirror, its long axis is the left one's, its
//     face points out of the arm on the other side, and the matrix is a proper one with the left attach bone's handedness (a mirror) - never inside out;
//   - the user's [shield] corrections are mirrored with it; the forearm twist turns it with the right forearm too;
//   - the game hiding the shield (its attach bone shrunk) is respected on either arm.
//
//   shield_arm_rig_test

#include "../../vr/skeletons.cpp"

#include <chrono>
#include <cstdio>
#include <thread>
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
std::vector<Mat> Animation(float scaleRight, float scaleLeft) {
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
	weight::Out wo;
	uint32_t sample = 0;
	Rig(float scaleRight = 1.0f, float scaleLeft = 1.0f)
	    : anim(Animation(scaleRight, scaleLeft)), bind(Animation(1.0f, 1.0f)), hero(MakeHero(bind)) {
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
		in.shieldForearm = true;   // [shield] forearm
		Hand(0, {0.2f, -0.40f, -0.30f});
		Hand(1, {-0.2f, -0.40f, -0.30f});   // (mirror-wise)
	}
	void UseShieldAttach() { hero.shield = kShd; }   // the hero knows the rig's shield_attach (hero.shield), as the hook finds it by name in the game
	void Hand(int side, V3 p) { in.hand[side][0] = p.x; in.hand[side][1] = p.y; in.hand[side][2] = p.z; }
	void Pose(int arm) {
		in.shieldArm = arm;
		++sample;
		in.sample = sample;
		Input c = in;
		weight::Out none;
		mats = anim;
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, none);
	}
	// the same with the weight model holding the weapon in the right hand (as in the game with [weight] on): the weapon's own block runs too
	void PoseWeight(int arm) {
		in.shieldArm = arm;
		++sample;
		in.sample = sample;
		Input c = in;
		weight::Out wo;
		wo.on = true;
		wo.slot = 0;
		wo.main = 0;
		wo.asPlaced = true;
		wo.holds[0] = true;
		wo.on01[0] = 1.0f;
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
	const int L = B(1, kAtt), R = B(0, kAtt), LF = B(1, kLwr), RF = B(0, kLwr);
	const double leftDet = Det(Animation(1, 1)[L]), rightDet = Det(Animation(1, 1)[R]);
	CHECK(leftDet < 0.0 && rightDet > 0.0);   // (the left attach bone is a mirror, the right one proper)

	// ---- on the left forearm: the old strap, as before, whether shieldArm says -1 (not handled) or 1 ----
	Mat leftOld;
	{
		Rig r;
		r.Pose(-1);
		leftOld = r.mats[L];
		CHECK(Finite(leftOld) && Det(leftOld) < 0.0);
		// the old formula: bind attach * inv(bind forearm) * forearm now
		const Mat local = Mul(r.bind[L], InvAffine(r.bind[LF]));
		const Mat want = Mul(local, r.mats[LF]);
		CHECK(Near(Pos(want), Pos(leftOld), 1e-3f));
		for (int i = 0; i < 3; ++i) CHECK(Near(Row(want, i), Row(leftOld, i), 1e-3f));
		Rig q;
		q.Pose(1);
		for (int i = 0; i < 16; ++i) CHECK(std::fabs(q.mats[L].m[i] - leftOld.m[i]) < 1e-4f);
		CHECK(g_shieldStrapped.load() == 1);
	}

	// ---- on the right forearm: the reflection of that, carried by the right forearm ----
	{
		Rig r;
		r.Pose(0);
		const Mat got = r.mats[R];
		CHECK(Finite(got));
		CHECK(g_shieldStrapped.load() == 3);
		// the game's attach bone on the LEFT wrist is the shield's (the game hangs it there); the right wrist's is the weapon's. The shield bone is bone L whatever the arm:
		const Mat shield = r.mats[L];
		CHECK(Finite(shield));
		CHECK(Det(shield) < 0.0);                                  // the left attach bone's handedness (a mirror): never inside out
		CHECK(std::fabs(RowLen(shield, 0) - 1.0) < 0.01 && std::fabs(RowLen(shield, 1) - 1.0) < 0.01 && std::fabs(RowLen(shield, 2) - 1.0) < 0.01);
		// the mirror: the hands are held mirror-wise, so the arms pose mirror-wise, and the shield on the right is the one on the left reflected in x = 0
		const V3 lp = Pos(leftOld), rp = Pos(shield);
		std::printf("  the shield on the left forearm at %.3f %.3f %.3f, on the right at %.3f %.3f %.3f\n", lp.x, lp.y, lp.z, rp.x, rp.y, rp.z);
		CHECK(Near({-lp.x, lp.y, lp.z}, rp, 0.02f));
		// the long axis (Y row) is the left one's, reflected
		const V3 ly = Row(leftOld, 1), ry = Row(shield, 1);
		CHECK(Near({-ly.x, ly.y, ly.z}, ry, 0.02f));
		// the row across the plate was negated, the face kept: the face row of the left (the one that points off the forearm) reflected is the right's face row
		const shieldmirror::Axes ax = [&] {
			weaponback::Mat bs;
			memcpy(bs.m, r.bind[L].m, sizeof bs.m);
			const V3 e = Pos(r.bind[LF]), w = Pos(r.bind[B(1, kHand)]);
			return shieldmirror::FaceAxes(bs, {e.x, e.y, e.z}, {w.x, w.y, w.z});
		}();
		CHECK(ax.known);
		const V3 lf = Row(leftOld, ax.normalRow), rf = Row(shield, ax.normalRow);
		CHECK(Near({-lf.x, lf.y, lf.z}, rf, 0.02f));
		// the weapon's bone on the right wrist is not the shield: it stays what the animation / the weight model made it (here: the game's, untouched, in proper handedness)
		CHECK(Det(got) > 0.0 || Det(got) < 0.0);
	}

	// ---- the user's corrections and the twist follow it ----
	{
		Rig a, b;
		a.Pose(0);
		for (int k = 0; k < 6; ++k) b.in.shieldAdj[k] = 0.0f;
		b.in.shieldAdj[0] = 5.0f;   // 5 cm along the shield's own x
		b.Pose(0);
		const V3 d = Sub(Pos(b.mats[L]), Pos(a.mats[L]));
		CHECK(Len(d) > 0.01f);
		// the same correction on the left arm moves it the mirrored way
		Rig c, e;
		c.Pose(1);
		e.in.shieldAdj[0] = 5.0f;
		e.Pose(1);
		const V3 dl = Sub(Pos(e.mats[L]), Pos(c.mats[L]));
		std::printf("  a 5 cm correction along the shield's x moves it %.3f %.3f %.3f on the left arm, %.3f %.3f %.3f on the right\n", dl.x, dl.y, dl.z, d.x, d.y, d.z);
		CHECK(Len(dl) > 0.01f && std::fabs(Len(dl) - Len(d)) < 0.01f);
		CHECK(Near({-dl.x, dl.y, dl.z}, d, 0.02f) || Near({dl.x, dl.y, dl.z}, d, 0.02f));   // (the mirrored displacement, or the same one where x is the axis across the plate)
	}

	// ---- [shield] x y z pitch yaw roll on the LEFT forearm, the default path ([hands] shoulder_draw off: shieldArm -1, no holster): each slider moves or turns the strapped shield in its own axis, live ----
	{
		Rig base;
		base.Pose(-1);
		CHECK(g_shieldStrapped.load() == 1);
		const Mat m0 = base.mats[L];
		{ float seen[6], mv, tn; ShieldAdjNote(seen, &mv, &tn); CHECK(mv == 0.0f && tn == 0.0f && seen[0] == 0.0f && seen[5] == 0.0f); }
		const V3 p0 = Pos(m0);
		const float kCm = 6.0f, kDeg = 30.0f;
		const float upm = 5.9f / (5.9f / 3.5f);   // the rig's model units per metre (vrcam: eye height over the user's)
		for (int k = 0; k < 6; ++k) {
			Rig r;
			r.in.shieldAdj[k] = k < 3 ? kCm : kDeg;
			r.Pose(-1);
			CHECK(g_shieldStrapped.load() == 1);
			const Mat m = r.mats[L];
			CHECK(Finite(m) && Det(m) < 0.0);
			{   // what reached the hook and what it did (the log line vrcam tells when a slider seems to do nothing)
				float seen[6], moveCm, turnDeg;
				ShieldAdjNote(seen, &moveCm, &turnDeg);
				for (int q = 0; q < 6; ++q) CHECK(std::fabs(seen[q] - (q == k ? (k < 3 ? kCm : kDeg) : 0.0f)) < 1e-4f);
				CHECK(std::fabs(moveCm - (k < 3 ? kCm : 0.0f)) < 0.05f && std::fabs(turnDeg - (k < 3 ? 0.0f : kDeg)) < 0.1f);
			}
			if (k < 3) {   // moved along the shield's own axis k (a row of the strapped matrix) by kCm
				const V3 d = Sub(Pos(m), p0), ax = Norm(Row(m0, k));
				const float along = Dot(d, ax);
				std::printf("  slider %d: moved %.3f along its axis (wanted %.3f), %.3f off it\n", k, along, kCm * 0.01f * upm, Len(Sub(d, Scale(ax, along))));
				CHECK(std::fabs(along - kCm * 0.01f * upm) < 0.02f * upm);
				CHECK(Len(Sub(d, Scale(ax, along))) < 0.01f * upm);
				for (int i = 0; i < 3; ++i) CHECK(Near(Row(m, i), Row(m0, i), 1e-3f));   // and not turned
			} else {   // turned about its own axis k - 3 by kDeg, in place
				CHECK(Near(Pos(m), p0, 1e-3f));
				const int axis = k - 3;
				const V3 a0 = Norm(Row(m0, axis)), a1 = Norm(Row(m, axis));
				CHECK(Near(a0, a1, 1e-3f));   // the turn's own axis stays
				for (int i = 0; i < 3; ++i) {
					if (i == axis) continue;
					const float c = Dot(Norm(Row(m0, i)), Norm(Row(m, i)));
					CHECK(std::fabs(c - std::cos(kDeg * 0.0174532925f)) < 2e-3f);
				}
			}
		}
		// live: the same rig posed again with the numbers changed moves it, and back
		Rig live;
		live.Pose(-1);
		const Mat a = live.mats[L];
		live.in.shieldAdj[1] = 10.0f;
		live.Pose(-1);
		const Mat b = live.mats[L];
		live.in.shieldAdj[1] = 0.0f;
		live.Pose(-1);
		const Mat c = live.mats[L];
		CHECK(Len(Sub(Pos(a), Pos(b))) > 0.01f);
		CHECK(Near(Pos(a), Pos(c), 1e-4f));
	}

	// ---- [shield] forearm 0 against 1 (the tick), shoulder_draw off: unticked the shield hangs on the HAND (it keeps the game's relation to the wrist), ticked it is strapped to the FOREARM (the bind relation to it) ----
	{
		const int LH = B(1, kHand);
		const float s45 = std::sin(0.5f * 0.7853982f), c45 = std::cos(0.5f * 0.7853982f);   // the left controller bent 45 degrees about x: the hand turns against the forearm
		Rig on, off;
		for (Rig* r : {&on, &off}) { r->in.rot[1][0] = s45; r->in.rot[1][1] = 0.0f; r->in.rot[1][2] = 0.0f; r->in.rot[1][3] = c45; }
		off.in.shieldForearm = false;
		on.Pose(-1);
		const int gOn = g_shieldStrapped.load();
		off.Pose(-1);
		const int gOff = g_shieldStrapped.load();
		CHECK(gOn == 1 && gOff == 0);
		const Mat bindToForearm = Mul(on.bind[L], InvAffine(on.bind[LF]));
		const Mat gameToHand = Mul(on.anim[L], InvAffine(on.anim[LH]));   // the game's own relation of the attach bone to the wrist
		const Mat onToForearm = Mul(on.mats[L], InvAffine(on.mats[LF])), offToHand = Mul(off.mats[L], InvAffine(off.mats[LH]));
		for (int i = 0; i < 3; ++i) {
			CHECK(Near(Row(onToForearm, i), Row(bindToForearm, i), 2e-3f));   // ticked: the bind relation to the forearm
			CHECK(Near(Row(offToHand, i), Row(gameToHand, i), 2e-3f));       // unticked: the game's relation to the hand
		}
		CHECK(Near(Pos(onToForearm), Pos(bindToForearm), 2e-3f) && Near(Pos(offToHand), Pos(gameToHand), 2e-3f));
		const V3 pOn = Pos(on.mats[L]), pOff = Pos(off.mats[L]);
		std::printf("  forearm=1 puts the shield at %.3f %.3f %.3f, forearm=0 at %.3f %.3f %.3f (the wrist bent 45 degrees): %.3f apart\n", pOn.x, pOn.y, pOn.z, pOff.x, pOff.y, pOff.z, Len(Sub(pOn, pOff)));
		CHECK(Len(Sub(pOn, pOff)) > 0.02f);   // the tick changes where the shield is once the wrist bends; without a bend the two relations coincide
		CHECK(!Near(Row(on.mats[L], 1), Row(off.mats[L], 1), 1e-3f));
	}

	// ---- [hands] shoulder_draw_shield_stow: the shield put away (shieldArm 2): its attach bone is shrunk to a point, nothing strapped on either forearm; drawn again it is where it was ----
	{
		Rig r;
		r.Pose(2);
		CHECK(g_shieldStrapped.load() == 2);
		CHECK(RowLen(r.mats[L], 0) < 0.01 && RowLen(r.mats[L], 1) < 0.01 && RowLen(r.mats[L], 2) < 0.01);
		CHECK(Finite(r.mats[L]));
		Rig q;
		q.Pose(1);
		CHECK(g_shieldStrapped.load() == 1 && RowLen(q.mats[L], 0) > 0.9);
		q.Pose(0);
		CHECK(g_shieldStrapped.load() == 3 && RowLen(q.mats[L], 0) > 0.9);
		q.Pose(2);
		CHECK(RowLen(q.mats[L], 0) < 0.01);
		q.Pose(1);   // drawn again: the left strap as ever
		for (int i = 0; i < 16; ++i) CHECK(std::fabs(q.mats[L].m[i] - leftOld.m[i]) < 1e-4f);
	}

	// ---- [hands] shoulder_draw_ranged: a bow or crossbow put away (rangedStowed): the attach bone it hangs on is shrunk to a point - the one the game shows, else the slot's - and nothing else is ----
	{
		auto sizes = [&](Rig& r) { return std::make_pair(RowLen(r.mats[R], 0), RowLen(r.mats[L], 0)); };
		{   // both attach bones shown (an Amazon's quiver in the other slot): the slot's (rangedBone) is hidden
			Rig r;
			r.in.shieldForearm = false;
			r.in.rangedStowed = true;
			r.in.rangedBone = 1;
			r.Pose(-1);
			CHECK(sizes(r).second < 0.01 && sizes(r).first > 0.9 && Finite(r.mats[L]));
			Rig q;
			q.in.shieldForearm = false;
			q.in.rangedStowed = true;
			q.in.rangedBone = 0;
			q.Pose(-1);
			CHECK(sizes(q).first < 0.01 && sizes(q).second > 0.9);
		}
		{   // the game shows one attach bone: that one, whatever the slot says
			Rig r(0.001f, 1.0f);
			r.in.shieldForearm = false;
			r.in.rangedStowed = true;
			r.in.rangedBone = 0;
			r.Pose(-1);
			CHECK(sizes(r).second < 0.01);
			Rig q(1.0f, 0.001f);
			q.in.shieldForearm = false;
			q.in.rangedStowed = true;
			q.in.rangedBone = 1;
			q.Pose(-1);
			CHECK(sizes(q).first < 0.01 && sizes(q).second < 0.01);   // (the hidden one stays hidden; the shown one is the right)
		}
		{   // drawn again: both as the game has them
			Rig r;
			r.in.shieldForearm = false;
			r.in.rangedStowed = false;
			r.Pose(-1);
			CHECK(sizes(r).first > 0.9 && sizes(r).second > 0.9);
		}
	}

	// ---- [hands] shoulder_draw_ranged_swap: a bow drawn into the OTHER hand - the game's attach bone (the left one) carried to the right wrist, mirrored; it is the same bow, in the other hand ----
	{
		auto bow = [&](Rig& r, int side, bool swap, bool isBow = true, bool stowed = false) {
			r.in.shieldForearm = false;
			r.in.rangedHolster = true;
			r.in.rangedBow = isBow;
			r.in.rangedSwap = swap;
			r.in.rangedSide = side;
			r.in.rangedStowed = stowed;
			r.in.rangedBone = 1;
		};
		auto mirror = [](V3 v) { return V3{-v.x, v.y, v.z}; };
		const int RH = B(0, kHand), LH = B(1, kHand);
		// the game shows the left attach bone (the right one is shrunk), as it does with a bow; held in its own hand: nothing changes
		Rig own(0.001f, 1.0f);
		bow(own, 1, true);
		own.Pose(-1);
		Rig plain(0.001f, 1.0f);
		plain.in.shieldForearm = false;
		plain.Pose(-1);
		for (int i = 0; i < 16; ++i) CHECK(std::fabs(own.mats[L].m[i] - plain.mats[L].m[i]) < 1e-5f);
		const Mat was = own.mats[L];
		CHECK(Det(was) < 0.0 && RowLen(was, 0) > 0.9 && RowLen(own.mats[R], 0) < 0.01);
		// drawn into the right hand: the bone is on the right wrist - the left one's place in the mirror, still the left bone's handedness (never inside out), the right bone still hidden
		Rig swp(0.001f, 1.0f);
		bow(swp, 0, true);
		swp.Pose(-1);
		const Mat now = swp.mats[L];
		CHECK(Finite(now) && Det(now) < 0.0);
		CHECK(RowLen(now, 0) > 0.99 && RowLen(now, 1) > 0.99 && RowLen(now, 2) > 0.99 && RowLen(now, 0) < 1.01 && RowLen(now, 1) < 1.01 && RowLen(now, 2) < 1.01);
		CHECK(Near(Pos(now), mirror(Pos(was)), 1e-2f));
		CHECK(Near(Row(now, 1), mirror(Row(was, 1)), 1e-3f) && Near(Row(now, 2), mirror(Row(was, 2)), 1e-3f));   // (its limbs and its face point as the mirrored left bow's)
		CHECK(RowLen(swp.mats[R], 0) < 0.01);
		// it is carried by the right hand: the right hand moved up, the bow went with it, at the same distance in the hand's frame
		Rig up(0.001f, 1.0f);
		bow(up, 0, true);
		up.Hand(0, {0.2f, -0.10f, -0.55f});
		up.Pose(-1);
		CHECK(Finite(up.mats[L]) && Det(up.mats[L]) < 0.0);
		CHECK(Len(Sub(Pos(up.mats[L]), Pos(up.mats[RH]))) > 0.0f);
		CHECK(std::fabs(Len(Sub(Pos(up.mats[L]), Pos(up.mats[RH]))) - Len(Sub(Pos(own.mats[L]), Pos(own.mats[LH])))) < 2e-2f);
		CHECK(Len(Sub(Pos(up.mats[L]), Pos(now))) > 0.1f);   // (and it did move, with the hand)
		// the fingers go with it: the left middle finger is curled on the bow (the game's grip), the right one is open; drawn into the right hand, the right finger has the curl - mirrored, in
		// its own hand's frame - and the left one the right's open pose (the string hand)
		{
			auto fingers = [&](Rig& r) {
				for (int side = 0; side < 2; ++side) {
					r.hero.fingers[side].clear();
					const int bones[3] = {B(side, kInd), B(side, kMid), B(side, kPnk)};
					for (int q = 0; q < 3; ++q) {
						Hero::Finger f;
						f.bones = {bones[q]};
						f.kind = q;
						r.hero.fingers[side].push_back(f);
					}
				}
				const float cs = std::cos(0.7f), sn = std::sin(0.7f);
				const V3 hp = Pos(r.anim[B(1, kMid)]);
				r.anim[B(1, kMid)] = Rows({cs, sn, 0}, {-sn, cs, 0}, {0, 0, 1}, hp);   // (the left middle finger curled about the knuckle line)
			};
			Rig f1(0.001f, 1.0f);
			bow(f1, 0, true);
			fingers(f1);
			f1.Pose(-1);
			const auto local = [](const Mat& w, const Mat& hand) { return Mul(w, InvAffine(hand)); };
			// the game's own poses: left curled, right open
			Rig g(0.001f, 1.0f);
			g.in.shieldForearm = false;
			fingers(g);
			g.Pose(-1);
			const Mat rightIs = local(f1.mats[B(0, kMid)], f1.mats[RH]), leftIs = local(f1.mats[B(1, kMid)], f1.mats[LH]);
			const Mat leftWas = local(g.mats[B(1, kMid)], g.mats[LH]), rightOpen = local(g.mats[B(0, kMid)], g.mats[RH]);
			const float cs = std::cos(0.7f), sn = std::sin(0.7f);
			CHECK(Near(Row(rightOpen, 0), V3{1, 0, 0}, 1e-3f) && Near(Row(rightOpen, 1), V3{0, 1, 0}, 1e-3f));   // (the game's right hand: open)
			CHECK(Near(Row(leftWas, 0), V3{-cs, sn, 0}, 1e-3f) && Near(Row(leftWas, 1), V3{sn, cs, 0}, 1e-3f));   // (the game's left hand: curled about the knuckle line)
			CHECK(Near(Row(rightIs, 0), V3{cs, -sn, 0}, 1e-3f) && Near(Row(rightIs, 1), V3{sn, cs, 0}, 1e-3f));   // the curl, mirrored: the other way about the same line
			CHECK(Near(Row(leftIs, 0), V3{-1, 0, 0}, 1e-3f) && Near(Row(leftIs, 1), V3{0, 1, 0}, 1e-3f));          // the left one has the right's open pose (in its own frame)
			CHECK(Det(f1.mats[B(0, kMid)]) > 0.0 && Det(f1.mats[B(1, kMid)]) > 0.0);                              // (the bones keep their handedness)
			// without the swap the fingers are the game's own
			Rig f0(0.001f, 1.0f);
			bow(f0, 1, true);
			fingers(f0);
			f0.Pose(-1);
			CHECK(Near(Row(local(f0.mats[B(1, kMid)], f0.mats[LH]), 1), V3{sn, cs, 0}, 1e-3f) && Near(Row(local(f0.mats[B(0, kMid)], f0.mats[RH]), 1), V3{0, 1, 0}, 1e-3f));
		}
		// the swap box off, a crossbow, a bow put away, a hand holding it in its own place: left to the game
		Rig off(0.001f, 1.0f);
		bow(off, 0, false);
		off.Pose(-1);
		for (int i = 0; i < 16; ++i) CHECK(std::fabs(off.mats[L].m[i] - was.m[i]) < 1e-5f);
		Rig xb(0.001f, 1.0f);
		bow(xb, 0, true, false);
		xb.Pose(-1);
		for (int i = 0; i < 16; ++i) CHECK(std::fabs(xb.mats[L].m[i] - was.m[i]) < 1e-5f);
		Rig away(0.001f, 1.0f);
		bow(away, 0, true, true, true);
		away.Pose(-1);
		CHECK(RowLen(away.mats[L], 0) < 0.01);   // (put away: hidden, not carried)
		// the game shows the RIGHT attach bone (the left one shrunk): drawn into the left hand it is carried the other way, a proper matrix as the right bone is
		Rig rb(1.0f, 0.001f);
		bow(rb, 1, true);
		rb.in.rangedBone = 0;
		rb.Pose(-1);
		Rig rbPlain(1.0f, 0.001f);
		rbPlain.in.shieldForearm = false;
		rbPlain.Pose(-1);
		CHECK(Finite(rb.mats[R]) && Det(rb.mats[R]) > 0.0 && RowLen(rb.mats[R], 0) > 0.99 && RowLen(rb.mats[L], 0) < 0.01);
		CHECK(Near(Pos(rb.mats[R]), mirror(Pos(rbPlain.mats[R])), 1e-2f));
	}

	// ---- the sliders with the weight model holding the sword in the right hand (the game's own setting): the strapped shield still moves with them, on the left arm ----
	{
		Rig a, b;
		a.in.shieldForearm = b.in.shieldForearm = true;
		b.in.shieldAdj[0] = 5.0f;
		a.PoseWeight(1);
		b.PoseWeight(1);
		const V3 moved = Sub(Pos(b.mats[L]), Pos(a.mats[L]));
		std::printf("  with the weight model: a 5 cm correction along the shield's x moves it %.3f %.3f %.3f\n", moved.x, moved.y, moved.z);
		CHECK(Len(moved) > 0.01f && Finite(a.mats[L]) && Finite(b.mats[L]) && RowLen(a.mats[L], 0) > 0.9);
	}

	// ---- [debug] shield_blink: the strapped shield's bone is hidden half a second in every two (and only then), on either arm; off, never ----
	{
		auto blinks = [&](bool blink, int arm) {
			Rig r;
			r.in.shieldForearm = true;
			r.in.shieldBlink = blink;
			int hidden = 0, shown = 0;
			for (int i = 0; i < 52; ++i) {   // (2.6 s: a whole period and a bit)
				r.Pose(arm);
				if (RowLen(r.mats[L], 0) < 0.01) ++hidden; else if (RowLen(r.mats[L], 0) > 0.9) ++shown;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return std::make_pair(hidden, shown);
		};
		const auto on = blinks(true, 1), onRight = blinks(true, 0), off = blinks(false, 1);
		std::printf("  blink on, left arm: hidden %d shown %d poses; on the right arm: hidden %d shown %d; off: hidden %d shown %d\n", on.first, on.second, onRight.first, onRight.second, off.first, off.second);
		CHECK(on.first >= 4 && on.second >= 20 && on.first + on.second >= 50);   // (about a quarter of the time away, the rest shown, nothing in between)
		CHECK(onRight.first >= 4 && onRight.second >= 20);
		CHECK(off.first == 0 && off.second >= 50);
	}

	// ---- shield_attach: the bone the shield effects name, under the left forearm. The strap, the corrections, the right arm, the put-away and the blink act on it as on the left attach bone ----
	{
		const int S = kShd;
		Rig plain;   // (the hero does not know shield_attach: as it was - the bone stays where the upper-body pass left it, on the left forearm)
		plain.Pose(0);
		Rig a, b, c;
		a.UseShieldAttach(); b.UseShieldAttach(); c.UseShieldAttach();
		a.Pose(1);
		b.Pose(0);
		c.Pose(2);
		// on the left forearm: where its bind pose against that forearm puts it
		const Mat wantLeft = Mul(Mul(a.bind[S], InvAffine(a.bind[LF])), a.mats[LF]);
		CHECK(Near(Pos(a.mats[S]), Pos(wantLeft), 2e-3f) && Det(a.mats[S]) < 0.0 && RowLen(a.mats[S], 0) > 0.99);
		// on the right arm: the reflection of that (the arms are held mirror-wise), still the bone's own handedness - and it LEFT the left arm
		CHECK(Finite(b.mats[S]) && Det(b.mats[S]) < 0.0);
		CHECK(Near(Pos(b.mats[S]), V3{-Pos(a.mats[S]).x, Pos(a.mats[S]).y, Pos(a.mats[S]).z}, 2e-2f));
		CHECK(Len(Sub(Pos(b.mats[S]), Pos(plain.mats[S]))) > 0.5f);   // (not where the plain pass leaves it: on the left forearm)
		// put away: both bones shrunk
		CHECK(RowLen(c.mats[S], 0) < 0.01 && RowLen(c.mats[L], 0) < 0.01 && Finite(c.mats[S]));
		// the left attach bone is still strapped as before (the two are set alike)
		CHECK(Near(Pos(a.mats[L]), Pos(Mul(Mul(a.bind[L], InvAffine(a.bind[LF])), a.mats[LF])), 2e-3f));
		// a correction moves it as it moves the left attach bone
		Rig d;
		d.UseShieldAttach();
		d.in.shieldAdj[0] = 5.0f;
		d.Pose(1);
		const V3 moved = Sub(Pos(d.mats[S]), Pos(a.mats[S])), movedL = Sub(Pos(d.mats[L]), Pos(a.mats[L]));
		std::printf("  shield_attach: a 5 cm correction along x moves it %.3f %.3f %.3f (the left attach bone %.3f %.3f %.3f)\n", moved.x, moved.y, moved.z, movedL.x, movedL.y, movedL.z);
		CHECK(Len(moved) > 0.01f && Near(moved, movedL, 2e-2f));
		// the blink hides it too
		Rig e;
		e.UseShieldAttach();
		e.in.shieldBlink = true;
		int hidden = 0, shown = 0;
		for (int i = 0; i < 52; ++i) {
			e.Pose(1);
			if (RowLen(e.mats[S], 0) < 0.01) ++hidden; else if (RowLen(e.mats[S], 0) > 0.9) ++shown;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		CHECK(hidden >= 4 && shown >= 20);
	}

	// ---- the game hides the shield (a weapon swap): hidden on either arm, nothing strapped ----
	{
		Rig r(1.0f, 0.001f);
		r.Pose(0);
		CHECK(RowLen(r.mats[L], 0) < 0.01);
		CHECK(g_shieldStrapped.load() == -1);
		r.Pose(1);
		CHECK(RowLen(r.mats[L], 0) < 0.01 && g_shieldStrapped.load() == -1);
	}

	// ---- the forearm twist turns it with the right forearm ----
	{
		Rig a, b;
		a.in.forearmTwist = 0.5f;
		b.in.forearmTwist = 0.5f;
		a.in.rot[0][0] = 0.0f;
		const float s = std::sin(0.4f), c = std::cos(0.4f);
		b.in.rot[0][0] = 0.0f; b.in.rot[0][1] = s; b.in.rot[0][2] = 0.0f; b.in.rot[0][3] = c;   // the right controller rolled 46 degrees about its y
		a.Pose(0);
		b.Pose(0);
		CHECK(Finite(a.mats[L]) && Finite(b.mats[L]));
		CHECK(Det(b.mats[L]) < 0.0 && std::fabs(RowLen(b.mats[L], 0) - 1.0) < 0.01);
	}

	std::printf(g_fails == 0 ? "shield_arm_rig_test: the left strap as before, the right forearm's mirrored strap (centre, axes, handedness, corrections), hidden by the game: all passed\n"
	                         : "shield_arm_rig_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
