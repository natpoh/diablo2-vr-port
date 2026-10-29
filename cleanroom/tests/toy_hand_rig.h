// toy_hand_rig.h - a toy rig with real hands for the tests that run the skeleton hook (vr/skeletons.cpp, included whole): model space (y up, faces +z, the hero's left is +x),
// 3.5 model units to the metre; an index, a middle, a little finger and a thumb of three bones each, every bone with an axes of its own, the left hand the mirror image of the
// right with its axes RELABELLED (a rig's habit); the weight model (vr/weight.cpp) in front. Copied from grip_fingers_test.cpp, with the triggers added.
#pragma once

#include "../../vr/skeletons.cpp"
#include <mmsystem.h>

#include "d2r_vr_state.h"
#include "hold.h"

#include <array>
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

using grippose::M3;
using grippose::V;

// ---- the toy rig: model space (y up, faces +z, the hero's left is +x), 3.5 model units to the metre ----
constexpr float kUpm = 3.5f;
constexpr int kFirst = 4, kStride = 18;   // root, pelvis, spine, head; then 18 bones per side
enum Part { kUpr, kLwr, kHand, kInd, kMid, kPnk, kAtt, kItem, kItemB, kTmb, kInd1, kInd2, kMid1, kMid2, kPnk1, kPnk2, kTmb1, kTmb2 };
constexpr int B(int side, int part) { return kFirst + side * kStride + part; }
constexpr int kCount = kFirst + 2 * kStride;
constexpr int kChain[4][3] = {{kInd, kInd1, kInd2}, {kMid, kMid1, kMid2}, {kPnk, kPnk1, kPnk2}, {kTmb, kTmb1, kTmb2}};   // index, middle, little finger, thumb

int Parent(int i) {   // (parents first)
	static const int top[4] = {-1, 0, 1, 2};
	if (i < kFirst) return top[i];
	const int side = (i - kFirst) / kStride, part = (i - kFirst) % kStride;
	switch (part) {
	case kUpr: return 2;
	case kLwr: return B(side, kUpr);
	case kHand: return B(side, kLwr);
	case kAtt: case kInd: case kMid: case kPnk: case kTmb: return B(side, kHand);
	case kItem: return B(side, kAtt);
	case kItemB: return B(side, kItem);
	}
	for (int c = 0; c < 4; ++c)
		for (int j = 1; j < 3; ++j)
			if (part == kChain[c][j]) return B(side, kChain[c][j - 1]);
	return -1;
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
Mat MatOf(const M3& u, V p) {
	Mat m{};
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = u.m[r * 3 + c];
	m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z; m.m[15] = 1.0f;
	return m;
}

M3 RotX(float a) { M3 r; r.m[4] = std::cos(a); r.m[5] = std::sin(a); r.m[7] = -std::sin(a); r.m[8] = std::cos(a); return r; }
M3 RotY(float a) { M3 r; r.m[0] = std::cos(a); r.m[2] = -std::sin(a); r.m[6] = std::sin(a); r.m[8] = std::cos(a); return r; }
M3 RotZ(float a) { M3 r; r.m[0] = std::cos(a); r.m[1] = std::sin(a); r.m[3] = -std::sin(a); r.m[4] = std::cos(a); return r; }
M3 Mx() { M3 r; r.m[0] = -1.0f; return r; }   // the reflection across x = 0
M3 Q0() { M3 q; q.m[0] = 0; q.m[1] = 0; q.m[2] = 1; q.m[3] = 0; q.m[4] = -1; q.m[5] = 0; q.m[6] = 1; q.m[7] = 0; q.m[8] = 0; return q; }   // the left rig's own labels: x and z swapped, y turned over
M3 Times(const M3& a, const M3& b) { return grippose::Mul(a, b); }
V Turned(V v, const M3& r) { return {v.x * r.m[0] + v.y * r.m[3] + v.z * r.m[6], v.x * r.m[1] + v.y * r.m[4] + v.z * r.m[7], v.x * r.m[2] + v.y * r.m[5] + v.z * r.m[8]}; }
V Plus(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V Reflect(V v) { return {-v.x, v.y, v.z}; }

// The right hand at (-0.7, 0.1, 0) pointing down: where each chain's knuckle is, the link from a bone to the next, its axes in the bind pose, how far the game's grip pose turns it.
V Knuckle(int c) { static const V k[4] = {{-0.7f, -0.19f, 0.12f}, {-0.7f, -0.20f, 0.0f}, {-0.7f, -0.17f, -0.12f}, {-0.65f, 0.0f, 0.10f}}; return k[c]; }
V Link(int c) { return c == 3 ? V{0.04f, -0.08f, 0.05f} : V{0.0f, -0.12f, 0.0f}; }
M3 BindU(int side, int c, int j) {
	M3 r = Times(RotX(0.1f * (float)(c + 1)), RotZ(0.07f * (float)(j + 1)));
	if (c == 3) r = Times(RotY(0.9f + 0.1f * (float)j), RotX(0.5f));
	return side == 0 ? r : Times(Q0(), Times(r, Mx()));   // the left hand: the mirror image, its axes relabelled
}
M3 Cum(int side, int c, int j) {   // the grip pose: how far bone j of chain c is turned from its bind pose, about its own origin (world axes)
	static const float th[3] = {0.6f, 1.3f, 1.9f};
	const float k = c == 0 ? 0.9f : c == 1 ? 1.0f : 1.1f;
	const M3 r = c == 3 ? Times(RotZ(0.3f * (float)(j + 1)), RotY(0.45f * (float)(j + 1))) : RotZ(th[j] * k);
	return side == 0 ? r : Times(Mx(), Times(r, Mx()));
}
void Fingers(std::vector<Mat>& w, int side, bool curled) {
	for (int c = 0; c < 4; ++c) {
		V pos = side == 0 ? Knuckle(c) : Reflect(Knuckle(c));
		M3 prev;
		for (int j = 0; j < 3; ++j) {
			const M3 cum = curled ? Cum(side, c, j) : M3{};
			if (j > 0) pos = Plus(pos, Turned(side == 0 ? Link(c) : Reflect(Link(c)), prev));
			w[B(side, kChain[c][j])] = MatOf(Times(BindU(side, c, j), cum), pos);
			prev = cum;
		}
	}
}

// The animation's world poses. slotSide: the hand the game holds the weapon in (0 right, 1 left): its attach bone is the full-size one, the other's is shrunk to a point. animSide: the attach
// bone the GAME draws the item on (the other wrist's when it walks with a two-hander; 2 = both hidden). curlSide: the hand whose fingers are curled round the weapon (-1 none).
std::vector<Mat> Animation(int slotSide, int animSide, float shrink, int curlSide) {
	std::vector<Mat> w(kCount);
	w[0] = At({0, 0, 0});
	w[1] = At({0, 3.5f, 0});
	w[2] = At({0, 4.6f, 0});
	w[3] = At({0, 5.9f, 0});
	for (int side = 0; side < 2; ++side) {
		const float sx = side == 0 ? -1.0f : 1.0f, mir = side == 0 ? 1.0f : -1.0f;
		const int upr = B(side, kUpr), lwr = B(side, kLwr), hand = B(side, kHand), att = B(side, kAtt), item = B(side, kItem), itemB = B(side, kItemB);
		w[upr] = At({sx * 0.7f, 5.3f, 0});
		w[lwr] = At({sx * 0.7f, 2.7f, 0});
		const V3 hp{sx * 0.7f, 0.1f, 0};   // (long arms: nothing in the sweep is out of reach)
		w[hand] = Rows({mir, 0, 0}, {0, 1, 0}, {0, 0, 1}, hp);
		w[B(side, kMid)] = At({hp.x, hp.y - 0.3f, hp.z});
		w[B(side, kInd)] = At({hp.x, hp.y - 0.29f, hp.z + 0.12f});
		w[B(side, kPnk)] = At({hp.x, hp.y - 0.27f, hp.z - 0.12f});
		const float s = side == animSide ? 1.0f : shrink;
		const float lean = 0.31f, cs = std::cos(lean), sn = std::sin(lean);
		const V3 y{0, sn, cs}, x{mir, 0, 0}, z{0, -cs * mir, sn * mir};
		const V3 origin{hp.x - sx * 0.07f, hp.y - 0.15f, hp.z};
		w[att] = Rows({x.x * s, x.y * s, x.z * s}, {y.x * s, y.y * s, y.z * s}, {z.x * s, z.y * s, z.z * s}, origin);
		w[item] = Mul(Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1.0f, 0}), w[att]);
		w[itemB] = Mul(Rows({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1.0f, 0}), w[item]);
	}
	for (int side = 0; side < 2; ++side) Fingers(w, side, side == curlSide);
	(void)slotSide;
	return w;
}

struct HeroSpec {
	bool kinds = false;          // the fingers are named (index / middle / pinky / thumb): matched by name
	bool reversedLeft = false;   // ... and the left hand lists them the other way round
	bool noLeftThumb = false;    // the left hand lists no thumb: the two hands cannot be matched
};
Hero MakeHero(const std::vector<Mat>& bind, const HeroSpec& spec) {
	Hero h;
	h.skel = 1;
	h.count = kCount;
	for (int i = 0; i < kCount; ++i) h.parent.push_back(Parent(i));
	h.inHead.assign(kCount, 0);
	h.inNeck.assign(kCount, 0);
	h.upper.assign(kCount, 0);
	h.inHand.assign(kCount, 0);
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
	for (int i = 0; i < kCount; ++i) {
		h.upper[i] = i >= 2 ? 1 : 0;
		const int p = h.parent[i];
		h.inHand[i] = (p >= 0 && (p == h.hand[0] || p == h.hand[1] || h.inHand[p])) ? 1 : 0;
	}
	static const int kinds[4] = {bowhook::kIndex, bowhook::kMiddle, bowhook::kPinky, bowhook::kThumb};
	for (int side = 0; side < 2; ++side) {
		std::vector<int> order = {0, 1, 2, 3};
		if (side == 1 && spec.reversedLeft) order = {3, 2, 1, 0};
		for (const int c : order) {
			if (side == 1 && spec.noLeftThumb && c == 3) continue;
			Hero::Finger f;
			f.bones = {B(side, kChain[c][0]), B(side, kChain[c][1]), B(side, kChain[c][2])};
			f.firstBend = 0;
			f.thumb = c == 3;
			f.kind = spec.kinds ? kinds[c] : -1;
			h.fingers[side].push_back(f);
		}
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
bool Proper(const Mat& m, double scale, int detSign) {
	for (int i = 0; i < 16; ++i)
		if (!std::isfinite(m.m[i])) return false;
	for (int r = 0; r < 3; ++r)
		if (std::fabs(RowLen(m, r) - scale) > 0.01 * scale) return false;
	if (std::fabs(RowDot(m, 0, 1)) > 0.01 * scale * scale || std::fabs(RowDot(m, 0, 2)) > 0.01 * scale * scale || std::fabs(RowDot(m, 1, 2)) > 0.01 * scale * scale) return false;
	const double d = Det(m);
	return (d > 0.0 ? 1 : -1) == detSign && std::fabs(std::fabs(d) - scale * scale * scale) <= 0.03 * scale * scale * scale;
}

// Where the finger bones stand in the hand's own frame (from the wrist, along the fingers / across the knuckles / out of the palm), the four chains' three bones each: 36 numbers.
using Coords = std::vector<float>;
Coords HandCoords(const std::vector<Mat>& w, int side) {
	const auto P = [&](int b) { return V{w[b].m[12], w[b].m[13], w[b].m[14]}; };
	M3 e;
	if (!grippose::HandFrame(P(B(side, kHand)), P(B(side, kMid)), P(B(side, kInd)), P(B(side, kPnk)), side, &e)) return {};
	Coords out;
	const V o = P(B(side, kHand));
	for (int c = 0; c < 4; ++c)
		for (int j = 0; j < 3; ++j) {
			const V d = grippose::Sub(P(B(side, kChain[c][j])), o);
			for (int i = 0; i < 3; ++i) out.push_back(grippose::Dot(d, e.Row(i)));
		}
	return out;
}
double Dist(const Coords& a, const Coords& b) {
	if (a.empty() || a.size() != b.size()) return 1e9;
	double d = 0.0;
	for (size_t i = 0; i < a.size(); ++i) d = std::max(d, (double)std::fabs(a[i] - b[i]));
	return d;
}

struct Weapon { const char* name; int type; heft::hold::Kind kind; };

struct Rig {
	int slot;                        // the hand the game holds the weapon in
	std::vector<Mat> anim, animStand, animWalk, animAttack, animHidden, animOpen;
	Hero hero;
	FakeSelf self{};
	FakeWorld world{};
	std::vector<Mat> mats;
	Input in;
	weight::Out wo;
	uint32_t sample = 0;
	long poses = 0, bad = 0;
	bool hidden = false;
	std::string firstBad;

	Rig(int slotSide, bool fist, bool fingers, const Weapon& weapon, const HeroSpec& spec = HeroSpec{}, bool captureWalking = false)
	    : slot(slotSide),
	      anim(Animation(slotSide, captureWalking ? slotSide ^ 1 : slotSide, 0.001f, slotSide)),
	      animStand(Animation(slotSide, slotSide, 0.001f, slotSide)),
	      animWalk(Animation(slotSide, slotSide ^ 1, 0.001f, slotSide)),
	      animAttack(Animation(slotSide, slotSide, 0.001f, -1)),
	      animHidden(Animation(slotSide, 2, 0.001f, slotSide)),
	      animOpen(Animation(slotSide, slotSide, 0.001f, -1)),
	      hero(MakeHero(animOpen, spec)) {
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
		in.weaponType = weapon.type;
		in.fist = fist;
		in.handFree[0] = in.handFree[1] = true;   // (vrcam's word: a hand that holds nothing of its own is free)
		in.weaponSide = slotSide;
		in.headRoom[1] = 1.7f;
		in.grip[0] = in.grip[1] = 0.0f;
		for (auto& r : in.rot) r[3] = 1.0f;
		Hand(0, {0.2, -0.40, -0.30});
		Hand(1, {-0.2, -0.40, -0.30});
		Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)weapon.kind;
		w.twoHanded = true;
		w.slot = slotSide;
		w.feel = 1.0f; w.strength = 1.0f; w.gravity = 1.0f; w.damping = 1.0f; w.gravityFeel = true;
		w.grabM = 0.12f; w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.haveRawTrigger = true;
		w.tipAxis = 2;
		w.shaftTipM = 0.66f; w.shaftButtM = 0.66f;
		w.afterDrop = 1;
		w.stowAfterS = 0.6f;
		w.backLong = true;
		w.slide = true;
		w.gripFit = true;
		w.gripLikeBase = true;
		w.gripFingers = fingers;
		ResetGrip();
	}
	void Walk(bool on) { anim = on ? animWalk : animStand; }
	void Hide(bool on) { anim = on ? animHidden : animStand; hidden = on; }
	void Attack(bool on) { in.attacking = on; anim = on ? animAttack : animStand; }
	void Hand(int side, heft::V3 p, heft::Q q = heft::Q{}) {
		in.hand[side][0] = (float)p.x; in.hand[side][1] = (float)p.y; in.hand[side][2] = (float)p.z;
		in.rot[side][0] = (float)q.x; in.rot[side][1] = (float)q.y; in.rot[side][2] = (float)q.z; in.rot[side][3] = (float)q.w;
	}
	void Grip(int side, float v) { in.weight.rawGrip[side] = v; }
	void Trigger(int side, float v) { in.weight.rawTrigger[side] = v; }
	void Off() {
		in.weight.on = false;
		Step(1, false);
		in.weight.on = true;
		Grip(0, 0.0f); Grip(1, 0.0f);
		Trigger(0, 0.0f); Trigger(1, 0.0f);
		ResetGrip();
	}
	void Step(int ms = 1, bool check = true) {
		Sleep(ms);
		in.sample = ++sample;
		Input c = in;
		weight::Apply(&c, &wo);
		mats = anim;
		world.mats = (uintptr_t)mats.data();
		PoseBody((uintptr_t)&self, hero, c, wo);
		if (check && wo.on && !hidden) Check();   // (a pose with both attach bones hidden is not asked to be anything)
	}
	void Steps(int n, int ms = 1) { while (n-- > 0) Step(ms); }
	// until the hand is fully on the weapon / fully off it (the model eases it over 0.12 s)
	void Settle(int side, float on01) {
		for (int i = 0; i < 80 && std::fabs(wo.on01[side] - on01) > 1e-4f; ++i) Step(5);
		Steps(4, 5);
	}
	void Fail(const char* what) {
		++bad;
		if (firstBad.empty()) firstBad = what;
	}
	// every matrix the pose made for the weapon and the hands and every finger bone: the scale and the handedness the animation's has (the weapon's bones at scale 1)
	void Check() {
		++poses;
		for (int side = 0; side < 2; ++side) {
			if (!Proper(mats[B(side, kAtt)], 1.0, Det(anim[B(side, kAtt)]) > 0.0 ? 1 : -1)) Fail("an attach bone");
			if (!Proper(mats[B(side, kHand)], RowLen(anim[B(side, kHand)], 0), Det(anim[B(side, kHand)]) > 0.0 ? 1 : -1)) Fail("a hand");
			for (int c = 0; c < 4; ++c)
				for (int j = 0; j < 3; ++j) {
					const int b = B(side, kChain[c][j]);
					if (!Proper(mats[b], 1.0, Det(anim[b]) > 0.0 ? 1 : -1)) Fail("a finger bone");
				}
		}
	}
	bool Shaft(heft::V3* butt, heft::V3* tip) const {
		float b[3], t[3];
		if (!weight::ShaftEnds(b, t)) return false;
		*butt = {b[0], b[1], b[2]};
		*tip = {t[0], t[1], t[2]};
		return true;
	}
	// the second hand on the wood, with its grip
	void TakeSecond(float squeeze = 1.0f) {
		heft::V3 butt, tip;
		CHECK(Shaft(&butt, &tip));
		Hand(slot ^ 1, heft::Add(butt, heft::Mul(heft::Sub(tip, butt), 0.78)));
		Steps(4);
		Grip(slot ^ 1, squeeze);
		Steps(40);
		Settle(slot ^ 1, 1.0f);
	}
	Coords Mine(int side) const { return HandCoords(mats, side); }
};

const char* SideName(int s) { return s ? "left" : "right"; }

}  // namespace tst
}  // namespace skel
