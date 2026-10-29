// The skeleton hook ITSELF - vr/skeletons.cpp, included whole so its PoseBody runs as it is - on a toy rig in fake game memory, with the real weight model (vr/weight.cpp) in front:
// every matrix the pose makes for the weapon (both attach bones, what hangs on them) and for the hands must be a proper one - orthonormal, the scale it had, the handedness it had
// (a mirrored left bone stays mirrored, a right one is never turned inside out) - whichever way the staff points (the whole sphere: straight up and down, the horizontals, every
// roll of the hand), with one hand on it, with two, with a hand that took it over from the other, the weapon in the right hand's slot and in the left's. And the weight model's
// shaft line is the drawn bone's axis.
//
// The reported bug: "the staff got flattened into a plane" (test-21): a bone matrix that lost an axis - the mesh drawn on it squashed to a disc.
//
//   staff_flat_test        (some tens of seconds: the model's steps are paced by the clock, as in the game)

#include "../../vr/skeletons.cpp"
#include <mmsystem.h>

#include "d2r_vr_state.h"
#include "hold.h"

#include <cstdio>
#include <string>

#pragma comment(lib, "winmm.lib")

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

// ---- the toy rig: model space (y up, faces +z, the hero's left is +x), 3.5 model units to the metre ----
enum Bone { kRoot, kPelvis, kSpine, kHead, kUpr0, kLwr0, kHand0, kInd0, kMid0, kPnk0, kAtt0, kItem0, kItemB0, kUpr1, kLwr1, kHand1, kInd1, kMid1, kPnk1, kAtt1, kItem1, kItemB1, kFing0, kCount = kFing0 + 12 };
int Parent(int i) {   // (parents first)
	static const int base[22] = {-1, kRoot, kPelvis, kSpine, kSpine, kUpr0, kLwr0, kHand0, kHand0, kHand0, kHand0, kAtt0, kItem0, kSpine, kUpr1, kLwr1, kHand1, kHand1, kHand1, kHand1, kAtt1, kItem1};
	if (i < 22) return base[i];
	const int f = (i - kFing0) / 2, link = (i - kFing0) % 2, side = f / 3, finger = f % 3;   // index, middle, pinky chains: the first knuckle (kInd0 ...), then two more
	const int first = side == 0 ? kInd0 : kInd1;
	return link == 0 ? first + finger : i - 1;
}
constexpr float kUpm = 3.5f;   // model units per metre (the hook clamps eye height / user height into 2..8)

Mat Rows(V3 x, V3 y, V3 z, V3 p) {
	Mat m{};
	m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
	m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
	m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
	m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z; m.m[15] = 1.0f;
	return m;
}
Mat At(V3 p, float sx = 1.0f, float sy = 1.0f, float sz = 1.0f) { return Rows({sx, 0, 0}, {0, sy, 0}, {0, 0, sz}, p); }

// The animation's world poses. slotSide: the hand the game holds the weapon in (0 right, 1 left): its attach bone is the full-size one, the other's is shrunk to a point (what the game
// does to the bone of a hand that holds nothing). The left bones are mirrors (negative determinant), as the game's are.
std::vector<Mat> Animation(int slotSide, int animSide, float handScale, float shrink) {   // animSide: the attach bone the GAME draws the item on (it may be the other wrist's)
	std::vector<Mat> w(kCount);
	w[kRoot] = At({0, 0, 0});
	w[kPelvis] = At({0, 3.5f, 0});
	w[kSpine] = At({0, 4.6f, 0});
	w[kHead] = At({0, 5.9f, 0});
	for (int side = 0; side < 2; ++side) {
		const float sx = side == 0 ? -1.0f : 1.0f;   // the right hand is on the model's -x
		const float mir = side == 0 ? 1.0f : -1.0f;   // the left bones are mirrors
		const int upr = side == 0 ? kUpr0 : kUpr1, lwr = upr + 1, hand = upr + 2, ind = upr + 3, mid = upr + 4, pnk = upr + 5, att = upr + 6, item = upr + 7, itemB = upr + 8;
		w[upr] = At({sx * 0.7f, 5.3f, 0});
		w[lwr] = At({sx * 0.7f, 2.7f, 0});
		const V3 hp{sx * 0.7f, 0.1f, 0};   // (long arms - 2.6 units each, 1.5 m the two - so that nothing in the sweep is out of reach: a clamped arm drags the weapon off the model by design)
		w[hand] = Rows({mir * handScale, 0, 0}, {0, handScale, 0}, {0, 0, handScale}, hp);                // pointing down (-y): the knuckles' row along +z
		w[mid] = At({hp.x, hp.y - 0.3f, hp.z});
		w[ind] = At({hp.x, hp.y - 0.29f, hp.z + 0.12f});
		w[pnk] = At({hp.x, hp.y - 0.27f, hp.z - 0.12f});
		// the weapon's bone in the game's grip: its Y (toward the tip) along the knuckles' row, leaning 18 degrees up, its origin 2 cm out of the palm
		const bool held = side == animSide;
		const float s = held ? 1.0f : shrink;
		const float lean = 0.31f, cs = std::cos(lean), sn = std::sin(lean);
		const V3 y{0, sn, cs}, x{mir, 0, 0}, z{0, -cs * mir, sn * mir};
		const V3 origin{hp.x - sx * 0.07f, hp.y - 0.15f, hp.z};   // (out of the palm is toward the body)
		w[att] = Rows({x.x * s, x.y * s, x.z * s}, {y.x * s, y.y * s, y.z * s}, {z.x * s, z.y * s, z.z * s}, origin);
		// what hangs on it: the item and a piece of it, each a unit along the bone's Y
		w[item] = Mul(Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1.0f, 0}), w[att]);
		w[itemB] = Mul(Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1.0f, 0}), w[item]);
	}
	for (int i = 22; i < kCount; ++i) w[i] = Mul(Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, -0.12f, 0}), w[Parent(i)]);   // the fingers' links hang down from their knuckles
	return w;
}

Hero MakeHero(const std::vector<Mat>& anim) {
	Hero h;
	h.skel = 1;
	h.count = kCount;
	for (int i = 0; i < kCount; ++i) h.parent.push_back(Parent(i));
	h.inHead.assign(kCount, 0);
	h.inNeck.assign(kCount, 0);
	h.upper.assign(kCount, 0);
	h.inHand.assign(kCount, 0);
	h.weapon[0] = kAtt0; h.weapon[1] = kAtt1;
	h.upr[0] = kUpr0; h.upr[1] = kUpr1;
	h.lwr[0] = kLwr0; h.lwr[1] = kLwr1;
	h.hand[0] = kHand0; h.hand[1] = kHand1;
	h.ind[0] = kInd0; h.ind[1] = kInd1;
	h.mid[0] = kMid0; h.mid[1] = kMid1;
	h.pnk[0] = kPnk0; h.pnk[1] = kPnk1;
	h.head = kHead; h.spine = kSpine; h.pelvis = kPelvis;
	h.eyeY = 5.9f;
	h.pelvisY = 3.5f;
	h.bind = anim;
	h.bindOk = true;
	for (int i = 0; i < kCount; ++i) {
		h.upper[i] = i >= kSpine ? 1 : 0;
		h.inHand[i] = (i == kInd0 || i == kMid0 || i == kPnk0 || i == kAtt0 || i == kItem0 || i == kItemB0 || i == kInd1 || i == kMid1 || i == kPnk1 || i == kAtt1 || i == kItem1 || i == kItemB1 || i >= kFing0) ? 1 : 0;
	}
	for (int side = 0; side < 2; ++side)
		for (int finger = 0; finger < 3; ++finger) {   // index, middle, pinky: the first knuckle, then two links
			Hero::Finger f;
			f.bones = {(side == 0 ? kInd0 : kInd1) + finger, kFing0 + (side * 3 + finger) * 2, kFing0 + (side * 3 + finger) * 2 + 1};
			f.firstBend = 0;
			h.fingers[side].push_back(f);
		}
	return h;
}

// fake game memory for PoseBody(self): *(self + 0x20) -> world: int32 n, then (at +4) the pointer to the world matrices
#pragma pack(push, 1)
struct FakeWorld { int32_t n; uintptr_t mats; };
#pragma pack(pop)
struct FakeSelf { char pad[0x20]; uintptr_t world; };

// ---- matrix checks ----
double RowLen(const Mat& m, int r) { return std::sqrt((double)m.m[r * 4] * m.m[r * 4] + (double)m.m[r * 4 + 1] * m.m[r * 4 + 1] + (double)m.m[r * 4 + 2] * m.m[r * 4 + 2]); }
double RowDot(const Mat& m, int a, int b) { return (double)m.m[a * 4] * m.m[b * 4] + (double)m.m[a * 4 + 1] * m.m[b * 4 + 1] + (double)m.m[a * 4 + 2] * m.m[b * 4 + 2]; }
double Det(const Mat& m) {
	const float* q = m.m;
	return (double)q[0] * (q[5] * q[10] - q[6] * q[9]) - (double)q[1] * (q[4] * q[10] - q[6] * q[8]) + (double)q[2] * (q[4] * q[9] - q[5] * q[8]);
}
// A proper bone matrix: finite, its three rows square to each other and all of the scale it should have (`scale`), its handedness the one `det` says (+1 / -1).
bool Proper(const Mat& m, double scale, int detSign, std::string* why) {
	for (int i = 0; i < 16; ++i)
		if (!std::isfinite(m.m[i])) { if (why) *why = "not finite"; return false; }
	for (int r = 0; r < 3; ++r)
		if (std::fabs(RowLen(m, r) - scale) > 0.01 * scale) { if (why) { char b[96]; std::snprintf(b, sizeof b, "row %d is %.4f long, wanted %.4f", r, RowLen(m, r), scale); *why = b; } return false; }
	if (std::fabs(RowDot(m, 0, 1)) > 0.01 * scale * scale || std::fabs(RowDot(m, 0, 2)) > 0.01 * scale * scale || std::fabs(RowDot(m, 1, 2)) > 0.01 * scale * scale) {
		if (why) *why = "rows not square to each other";
		return false;
	}
	const double d = Det(m);
	if ((d > 0.0 ? 1 : -1) != detSign || std::fabs(std::fabs(d) - scale * scale * scale) > 0.03 * scale * scale * scale) {
		if (why) { char b[96]; std::snprintf(b, sizeof b, "determinant %.5f, wanted %c%.5f", d, detSign > 0 ? '+' : '-', scale * scale * scale); *why = b; }
		return false;
	}
	return true;
}

struct Rig {
	int slot;                       // the hand the game holds the weapon in
	std::vector<Mat> anim, animStand, animWalk, animHidden;   // + both attach bones hidden (a weapon swap in progress)
	bool hidden = false;   // the animation now / standing (the item on the slot's own attach bone) / walking (the game moves it to the other wrist's)
	int drawSide = 0;               // the attach bone the game draws the item on, in the animation now
	Hero hero;
	FakeSelf self{};
	FakeWorld world{};
	std::vector<Mat> mats;          // the "game's" world matrices (the pose writes its result back here)
	Input in;
	weight::Out wo;
	uint32_t sample = 0;
	long poses = 0, bad = 0;
	long flat = 0;                   // poses whose line between the two attach bones is under half the rest gap: the weapon the game derives from it is flattened
	double derivedRatioMin = 1e9, worstLineDeg = 0.0;   // the shortest that line was (of the rest gap), the worst angle it made with the weapon's own axis
	double worstEnds = 0.0, lastEnds = 0.0;   // the model's shaft against the drawn bone's axis, metres: the worst seen, and this pose's
	std::string firstBad;

	explicit Rig(int slotSide, bool rich = false, float handScale = 1.0f, float shrink = 0.001f, bool captureWalking = false) : slot(slotSide), anim(Animation(slotSide, captureWalking ? slotSide ^ 1 : slotSide, handScale, shrink)), animStand(anim), animWalk(Animation(slotSide, slotSide ^ 1, handScale, shrink)), animHidden(Animation(slotSide, 2, handScale, shrink)), drawSide(captureWalking ? slotSide ^ 1 : slotSide), hero(MakeHero(anim)) {
		mats = anim;
		world.n = kCount;
		world.mats = (uintptr_t)mats.data();
		self.world = (uintptr_t)&world;
		in.mode = 2;
		in.handsValid = 3;
		in.wrist = true;
		in.haveEye = true;
		in.eyeModel[0] = 0.0f; in.eyeModel[1] = 5.9f; in.eyeModel[2] = 0.0f;
		in.haveYaw = true;
		in.yawInModel = 0.0f;
		in.userHeadM = 5.9f / kUpm;
		in.holdGrip = true;
		in.bothAttach = true;
		in.weaponType = D2RVR_TYPE_STAFF;
		in.fist = rich;          // free hands close with the grip (Fist rebuilds the finger chains)
		in.lockUpper = rich;     // the body above the pelvis held in the bind pose
		in.weaponSide = slotSide;
		in.headRoom[1] = 1.7f;
		in.grip[0] = in.grip[1] = 0.0f;
		for (auto& r : in.rot) r[3] = 1.0f;
		Hand(0, {0.2, -0.40, -0.30});
		Hand(1, {-0.2, -0.40, -0.30});
		Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)heft::hold::Kind::Staff;
		w.twoHanded = true;
		w.slot = slotSide;
		w.feel = 1.0f; w.strength = 1.0f; w.gravity = 1.0f; w.damping = 1.0f; w.gravityFeel = true;
		w.grabM = 0.12f; w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.tipAxis = 2;
		w.shaftTipM = 0.66f; w.shaftButtM = 0.66f;
		w.afterDrop = 1;
		w.stowAfterS = 0.6f;
		w.backLong = true;
		w.slide = true;
		w.gripFit = true;
		w.gripLikeBase = true;
		ResetGrip();
	}
	// the game moves the item to the other wrist (walking with a two-hander) / back (standing)
	void Walk(bool on) {
		anim = on ? animWalk : animStand;
		drawSide = on ? slot ^ 1 : slot;
		hidden = false;
	}
	// the game hides both attach bones (a weapon swap in progress: nothing in a hand for a moment)
	void Hide(bool on) {
		if (on) { anim = animHidden; hidden = true; }
		else Walk(false);
	}
	void Hand(int side, heft::V3 p, heft::Q q = heft::Q{}) {
		in.hand[side][0] = (float)p.x; in.hand[side][1] = (float)p.y; in.hand[side][2] = (float)p.z;
		in.rot[side][0] = (float)q.x; in.rot[side][1] = (float)q.y; in.rot[side][2] = (float)q.z; in.rot[side][3] = (float)q.w;
	}
	heft::V3 HandPos(int side) const { return {in.hand[side][0], in.hand[side][1], in.hand[side][2]}; }
	void Grip(int side, float v) { in.weight.rawGrip[side] = v; }
	void Off() {
		in.weight.on = false;
		Step(1, false);
		in.weight.on = true;
		Grip(0, 0.0f); Grip(1, 0.0f);
		ResetGrip();
	}
	// the head frame (x right, y up, z back, metres) of a model-space point / direction, as the hook's ToModel undone (no yaw)
	static heft::V3 ToHead(heft::V3 model, bool point) {
		const double e = 5.9;
		return point ? heft::V3{-model.x / kUpm, (model.y - e) / kUpm, -model.z / kUpm} : heft::V3{-model.x, model.y, -model.z};
	}

	// One step: the weight model, then the hook, then the checks. check = false: just run.
	void Step(int ms = 1, bool check = true) {
		Sleep(ms);
		in.sample = ++sample;
		Input c = in;
		weight::Apply(&c, &wo);
		mats = anim;   // the game's animation of this frame
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, wo);
		if (check && wo.on && !hidden) Check();
	}
	void Steps(int n) { while (n-- > 0) Step(); }

	void Fail(const char* what, const std::string& why) {
		++bad;
		if (firstBad.empty()) firstBad = std::string(what) + ": " + why;
	}
	void Check() {
		++poses;
		// every matrix the pose made for the weapon and the hands: the scale and the handedness of the matrix the animation had for that bone, the weapon bones at scale 1 (both of them
		// are the drawn weapon's, whichever the game hangs the item on)
		struct Want { int bone; const char* name; bool weapon; };
		// (what hangs on the attach bone the game draws NO item on is not drawn: it is not asked to be anything)
		const bool d0 = drawSide == 0;
		const Want list[] = {{kAtt0, "R attach", true}, {kAtt1, "L attach", true}, {d0 ? kItem0 : kItem1, "the item", true}, {d0 ? kItemB0 : kItemB1, "the item's piece", true},
		                     {kHand0, "R hand", false}, {kHand1, "L hand", false}};
		for (const Want& w : list) {
			std::string why;
			const int sign = Det(anim[w.bone]) > 0.0 ? 1 : -1;
			const double scale = w.weapon ? 1.0 : RowLen(anim[w.bone], 0);
			if (!Proper(mats[w.bone], scale, sign, &why)) Fail(w.name, why);
		}
		for (int b : {kInd0, kMid0, kPnk0, kInd1, kMid1, kPnk1}) {
			std::string why;
			if (!Proper(mats[b], 1.0, Det(anim[b]) > 0.0 ? 1 : -1, &why)) Fail("a finger's first bone", why);
		}
		// The game lays a two-handed weapon along the LINE from one attach bone to the other (docs/HANDOVER.md, 10:25-10:30: both hands on the haft; "with the two in one point the line had no
		// length and the axe was squashed flat along its Y - its matrix had Y 0, both bones Y 1"). Every bone being proper is not enough: that line must have a length (the rest gap is
		// [hands] carry_cm) and lie along the weapon's own axis, or the weapon the game derives from it is flat though both bones are fine. The derived weapon, as the documented rule
		// has it: origin on the slot's bone, Y = the line over the rest gap.
		{
			const Mat& a = mats[slot == 0 ? kAtt0 : kAtt1];
			const Mat& b = mats[slot == 0 ? kAtt1 : kAtt0];
			const double rest = (double)in.carryM * kUpm;
			const double dx = (double)b.m[12] - a.m[12], dy = (double)b.m[13] - a.m[13], dz = (double)b.m[14] - a.m[14];
			const double len = std::sqrt(dx * dx + dy * dy + dz * dz), ratio = len / rest;   // the derived Y row's length: 1 = the weapon as long as it is
			derivedRatioMin = std::min(derivedRatioMin, ratio);
			if (ratio < 0.5) ++flat;
			if (len > 1e-9) {
				const double ay = RowLen(a, 1), cosA = (dx * a.m[4] + dy * a.m[5] + dz * a.m[6]) / (len * ay);
				worstLineDeg = std::max(worstLineDeg, std::acos(std::min(1.0, std::fabs(cosA))) * 57.29578);
			}
		}
		// the weight model's shaft is the drawn bone's axis: the slot's weapon bone's origin and Y, with the blade's ends, against the model's two ends (head frame, metres)
		float rb[3], rt[3];
		if (weight::ShaftEnds(rb, rt)) {
			const Mat& w = mats[slot == 0 ? kAtt0 : kAtt1];
			const heft::V3 o = ToHead({w.m[12], w.m[13], w.m[14]}, true);
			heft::V3 d = ToHead({w.m[4], w.m[5], w.m[6]}, false);
			const double dl = heft::Len(d);
			if (dl > 1e-9) {
				d = heft::Mul(d, 1.0 / dl);
				const heft::V3 b = heft::Sub(o, heft::Mul(d, 0.66)), t = heft::Add(o, heft::Mul(d, 0.66));
				const double e = std::max(heft::Len(heft::Sub({rb[0], rb[1], rb[2]}, b)), heft::Len(heft::Sub({rt[0], rt[1], rt[2]}, t)));
				lastEnds = e;
				worstEnds = std::max(worstEnds, e);
			}
		}
	}
	// the staff's shaft as the model has it now, butt -> tip, head frame
	bool Shaft(heft::V3* butt, heft::V3* tip) const {
		float b[3], t[3];
		if (!weight::ShaftEnds(b, t)) return false;
		*butt = {b[0], b[1], b[2]};
		*tip = {t[0], t[1], t[2]};
		return true;
	}
};

heft::V3 Unit(heft::V3 v) { const double l = heft::Len(v); return l > 1e-12 ? heft::Mul(v, 1.0 / l) : heft::V3{0, 1, 0}; }
heft::Q QBetween(heft::V3 a, heft::V3 b) {   // the smallest turn taking unit a onto unit b
	const heft::V3 ax = heft::Cross(a, b);
	const double s = heft::Len(ax), c = heft::Dot(a, b);
	if (s < 1e-9) {
		if (c > 0.0) return heft::Q{};
		const heft::V3 p = std::fabs(a.x) < 0.9 ? heft::Cross(a, {1, 0, 0}) : heft::Cross(a, {0, 1, 0});
		return heft::QFromAxisAngle(Unit(p), 3.14159265358979);
	}
	return heft::QFromAxisAngle(heft::Mul(ax, 1.0 / s), std::atan2(s, c));
}

// The directions: the six axes, the twelve edge middles, and a spiral over the sphere - exactly vertical up and down, the horizontals, and everything between
std::vector<heft::V3> Directions(int spiral) {
	std::vector<heft::V3> d = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
	for (int i = 0; i < 3; ++i)
		for (double s1 : {-1.0, 1.0})
			for (double s2 : {-1.0, 1.0}) {
				heft::V3 v{};
				double* c[3] = {&v.x, &v.y, &v.z};
				*c[i] = 0.0;
				*c[(i + 1) % 3] = s1;
				*c[(i + 2) % 3] = s2;
				d.push_back(Unit(v));
			}
	const double golden = 2.399963229728653;
	for (int i = 0; i < spiral; ++i) {
		const double y = 1.0 - 2.0 * (i + 0.5) / spiral, r = std::sqrt(1.0 - y * y), a = golden * i;
		d.push_back({r * std::cos(a), y, r * std::sin(a)});
	}
	return d;
}

}  // namespace tst
}  // namespace skel

using namespace skel;
using namespace skel::tst;

struct Config { bool walk, captureWalking; float handScale, shrink; bool rich; const char* name; bool hiddenFirst = false; };   // walk: after the weapon is placed the game moves the item to the other wrist; captureWalking: ... and the game's grip is taken in that state

// One rig through the four states and the sweep. full: also the held-still comparison of the model's shaft (slow)
void RunConfig(const Config& cfg, int slotSide, const std::vector<heft::V3>& dirs, const double* rolls, int nRolls, bool full) {
	const char* names[4] = {"one hand", "two hands", "taken over by the other hand", "both hands took it (the game's hand again)"};
	std::vector<heft::V3> strictDirs(dirs.begin(), dirs.begin() + 18);
	for (int scenario = 0; scenario < 4; ++scenario) {
		Rig r(slotSide, cfg.rich, cfg.handScale, cfg.shrink, cfg.captureWalking);
		r.Off();
		if (cfg.hiddenFirst) {   // a swap in progress: both attach bones hidden when the weapon is placed, shown a moment later
			r.Hide(true);
			for (int i = 0; i < 10; ++i) r.Step(1, false);
			r.Hide(false);
		}
		r.Steps(30);   // (the weapon is placed and the game's grip taken)
		if (cfg.walk) r.Walk(true);
		r.Steps(10);
		r.worstEnds = 0.0;   // (the very first poses: the model's first guess of the line, before the drawn bone is measured)
		CHECK(r.wo.on && r.wo.holds[slotSide]);
		heft::V3 butt, tip;
		CHECK(r.Shaft(&butt, &tip));
		const heft::V3 u0 = Unit(heft::Sub(tip, butt));
		const int o = slotSide ^ 1;
		if (scenario >= 1) {   // the other hand takes it with its grip, on the wood
			r.Hand(o, heft::Add(butt, heft::Mul(heft::Sub(tip, butt), 0.78)));
			r.Steps(4);
			r.Grip(o, 1.0f);
			r.Steps(40);
			CHECK(r.wo.holds[o]);
		}
		if (scenario >= 2) {   // the game's hand lets go: the other carries it
			r.Grip(slotSide, 1.0f); r.Steps(3); r.Grip(slotSide, 0.0f);
			r.Steps(40);
			CHECK(!r.wo.holds[slotSide] && r.wo.holds[o]);
		}
		if (scenario >= 3) {   // the game's hand takes it up again, a little way down the wood
			r.Hand(slotSide, heft::Add(butt, heft::Mul(heft::Sub(tip, butt), 0.25)));
			r.Steps(4);
			r.Grip(slotSide, 1.0f);
			r.Steps(50);
			CHECK(r.wo.holds[slotSide] && r.wo.holds[o]);
		}
		// the sweep: both controllers carried round the first hand's place, rigidly with the staff's direction and roll
		const heft::V3 p0 = r.HandPos(slotSide), q0 = r.HandPos(o);
		auto aim = [&](const heft::V3& d, double roll) {
			const heft::Q Q = heft::QNorm(heft::QMul(heft::QFromAxisAngle(d, roll), QBetween(u0, d)));
			r.Hand(slotSide, p0, Q);
			r.Hand(o, heft::Add(p0, heft::Rotate(Q, heft::Sub(q0, p0))), Q);
		};
		for (const heft::V3& d : dirs)
			for (int k = 0; k < nRolls; ++k) {
				aim(d, rolls[k]);
				r.Steps(3);
			}
		// ... and held still, to see the model's shaft against the drawn bone's axis once it has followed
		double settledWorst = 0.0;
		if (full)
			for (const heft::V3& d : strictDirs) {
				aim(d, 0.0);
				r.Steps(20);
				for (int i = 0; i < 4; ++i) { r.Step(2); settledWorst = std::max(settledWorst, r.lastEnds); }
			}
		std::printf("  %s%s, the weapon in the %s slot: %ld poses, %ld with a bad matrix%s\n", cfg.name, names[scenario], slotSide ? "left" : "right", r.poses, r.bad, "");
		if (full) std::printf("      the line between the two attach bones: at least %.2f of the rest gap, %.3f deg off the weapon's axis at the worst, %ld poses with it under half\n", r.derivedRatioMin, r.worstLineDeg, r.flat);
		CHECK(r.flat == 0);                  // (the weapon the game derives from that line is not flat)
		CHECK(r.derivedRatioMin > 0.99);
		CHECK(r.worstLineDeg < 0.05);
		if (full) std::printf("      the model's shaft against the drawn bone's axis: %.1f mm when held, %.1f mm at the worst of the sweep\n", settledWorst * 1000.0, r.worstEnds * 1000.0);
		if (r.bad) std::printf("      first: %s\n", r.firstBad.c_str());
		CHECK(r.poses > 50);
		CHECK(r.bad == 0);
		if (full) CHECK(settledWorst < 0.002);
	}
}

// The control: with [weight] attach_apart off both attach bones are in one point - the line between them has no length, the weapon the game derives from it is flat in EVERY pose (the
// staff of test-18 .. test-21). So the check above is one that fails when it should.
static void RunNoApart(int slotSide) {
	Rig r(slotSide, false);
	r.in.weight.attachApart = false;
	r.Off();
	r.Steps(30);
	heft::V3 butt, tip;
	CHECK(r.Shaft(&butt, &tip));
	r.Hand(slotSide ^ 1, heft::Add(butt, heft::Mul(heft::Sub(tip, butt), 0.78)));
	r.Steps(4);
	r.Grip(slotSide ^ 1, 1.0f);
	r.Steps(40);
	std::printf("  attach_apart off, the weapon in the %s slot: %ld poses, %ld of them with the line between the attach bones under half the rest gap (shortest %.3f)\n", slotSide ? "left" : "right", r.poses, r.flat, r.derivedRatioMin);
	CHECK(r.poses > 30);
	CHECK(r.flat == r.poses);
	CHECK(r.bad == 0);   // (every bone is proper all the same: it is the pair that makes the weapon flat)
}

// [weight] second_lets_go, the hand-over (the user, 2026-10-24: «оружие сбоку»): a sword in each hand; the right lets its own go; the right hand closes on the left's
// sword - it is laid in the right hand exactly as the game's own grip lays a weapon there: its attach bone against the right hand the same as the right's own sword's
// was (the same position, the same Y and Z), only its X turned for the left bone's handedness (a mirror-less matrix would draw the item inside out); every bone proper.
static void RunSecondLaid() {
	static bool toldLaid = false;
	weight::SetLog([](const char* t) { if (std::strstr(t, "the second weapon taken by the right hand")) toldLaid = true; });
	Rig r(0, false, 1.0f, 1.0f);   // (both attach bones full size: a weapon in each hand)
	Input::Weight& w = r.in.weight;
	w.kind = (int)heft::hold::Kind::Sword;
	w.twoHanded = false;
	r.in.weaponType = D2RVR_TYPE_SWORD;
	r.in.bothAttach = false;
	w.slide = false;
	w.second = true;
	w.secondKind = (int)heft::hold::Kind::Sword;
	w.secondLetsGo = true;
	w.otherBusy = true;
	w.shaftTipM = 0.9f;
	w.shaftButtM = 0.14f;
	w.secondTipAxis = 2;
	w.shaft2TipM = 0.9f;
	w.shaft2ButtM = 0.14f;
	r.Off();
	r.Steps(30);
	const Mat base = Mul(r.mats[kAtt0], InvAffine(r.mats[kHand0]));   // the right's own sword in the right hand, as the game's grip has it
	r.Grip(0, 1.0f);   // the right lets its own go
	r.Steps(10);
	r.Grip(0, 0.0f);
	r.Steps(20);
	CHECK(!r.wo.holds[0]);
	const Mat& la = r.mats[kAtt1];   // the left's sword as drawn: the right hand to it, 0.1 m up its blade
	const float yl = std::sqrt(la.m[4] * la.m[4] + la.m[5] * la.m[5] + la.m[6] * la.m[6]);
	const heft::V3 on = Rig::ToHead({la.m[12] + la.m[4] / yl * 0.1f * kUpm, la.m[13] + la.m[5] / yl * 0.1f * kUpm, la.m[14] + la.m[6] / yl * 0.1f * kUpm}, true);
	r.Hand(0, on);
	r.Steps(6);
	r.Grip(0, 1.0f);   // the right closes on it (a second hand on it while the left still has it) ...
	r.Steps(20);
	r.Grip(1, 1.0f);   // ... and the left lets it go: the right has it alone
	r.Steps(6);
	r.Grip(1, 0.0f);
	r.Steps(30);
	CHECK(r.wo.hand2 == 0);
	const Mat laid = Mul(r.mats[kAtt1], InvAffine(r.mats[kHand0]));   // the left's sword against the right hand now
	auto row = [](const Mat& m, int i) { return heft::V3{m.m[i * 4], m.m[i * 4 + 1], m.m[i * 4 + 2]}; };
	const double dPos = heft::Len(heft::Sub({laid.m[12], laid.m[13], laid.m[14]}, {base.m[12], base.m[13], base.m[14]})) / kUpm;
	const double dY = heft::Len(heft::Sub(row(laid, 1), row(base, 1))), dZ = heft::Len(heft::Sub(row(laid, 2), row(base, 2)));
	const double dX = std::min(heft::Len(heft::Sub(row(laid, 0), row(base, 0))), heft::Len(heft::Add(row(laid, 0), row(base, 0))));
	std::printf("  the hand-over: the left's sword in the right hand %s - %.1f cm and %.3f / %.3f / %.3f (X Y Z) off the right's own sword's lay; %ld poses, %ld bad%s%s\n",
	            r.wo.hand2 == 0 ? "laid" : "NOT TAKEN", dPos * 100.0, dX, dY, dZ, r.poses, r.bad, r.bad ? " - " : "", r.firstBad.c_str());
	CHECK(dPos < 0.005 && dX < 0.01 && dY < 0.01 && dZ < 0.01);
	CHECK(r.bad == 0);
	CHECK(toldLaid);   // (the hand-over told: "the second weapon taken by the right hand: laid ...")
	weight::SetLog(nullptr);
	r.Off();
}

int main() {
	using namespace skel;
	using namespace skel::tst;
	RunSecondLaid();
	timeBeginPeriod(1);   // (a 1 ms sleep is a millisecond, not a timer tick of 15)
	std::printf("staff_flat_test (the hook's own matrices on a toy rig)\n");
	for (int slotSide = 0; slotSide < 2; ++slotSide) RunNoApart(slotSide);
	const double rolls[3] = {0.0, 2.1, -1.3};   // radians about the shaft
	// animSide: -1 / 1 the game draws the item on the slot's own attach bone, 2 on the OTHER wrist's (the slot's is the shrunk one: what the game does walking with a two-hander)
	const std::vector<Config> configs = {
		{false, false, 1.0f, 0.001f, false, ""},
		{false, false, 1.0f, 0.001f, true, "[fists, body locked] "},
		{true, false, 1.0f, 0.001f, false, "[walking: the game draws it on the other wrist] "},
		{true, false, 1.3f, 0.00001f, true, "[walking, hands scaled 1.3, the empty bone shrunk to 1e-5, fists, body locked] "},
		{false, false, 1.3f, 0.0001f, false, "[hands scaled 1.3, the empty bone shrunk to 1e-4] "},
		{true, true, 1.0f, 0.001f, false, "[the grip taken while walking] "},
		{false, false, 1.0f, 0.001f, false, "[placed during a weapon swap: both attach bones hidden] ", true},
	};
	for (size_t c = 0; c < configs.size(); ++c) {
		const bool full = c == 0;   // the held-still comparison of the shaft on the first only (it is slow)
		const std::vector<heft::V3> dirs = Directions(full ? 30 : 14);
		for (int slotSide = 0; slotSide < 2; ++slotSide) RunConfig(configs[c], slotSide, dirs, rolls, full ? 3 : 2, full);
	}
	std::printf(g_fails == 0 ? "staff_flat_test: all passed\n" : "staff_flat_test: %d failed\n", skel::tst::g_fails);
	return skel::tst::g_fails == 0 ? 0 : 1;
}
