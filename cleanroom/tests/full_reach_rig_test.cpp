// The skeleton hook's arm IK ITSELF (vr/skeletons.cpp Reach, included whole) on a toy rig: [arms] full_reach (vr/full_reach.h) puts the
// wrist on a target past the arm's reach - the collarbone turned toward it (at most 20 degrees, its own length kept), then the arm
// lengthened for the rest (at most 35 %) with the hand, its fingers and what it holds at their own size. Off: the wrist stops at the
// arm's length, as before. A second Reach of the same arm in the same pose (the grip fit's, the bow's) does not pile turns and
// stretches on the first; a target in reach changes nothing; a rig whose upper arm hangs off the chest (no collarbone of its own)
// only lengthens; the left arm is the right one mirrored.
//
//   full_reach_rig_test

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

Mat At(V3 p) {
	Mat m{};
	m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
	m.m[12] = p.x; m.m[13] = p.y; m.m[14] = p.z;
	return m;
}
float RowLen(const Mat& m, int r) { return Len({m.m[r * 4], m.m[r * 4 + 1], m.m[r * 4 + 2]}); }
float Dist(V3 a, V3 b) { return Len(Sub(a, b)); }
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// Model space: y up, faces +z, its left is +x. pelvis, spine, chest, neck, head; each side a collarbone (or none: the upper arm on the
// chest), upper arm, forearm, hand, a finger. The collarbone 0.8 long, the upper arm 1.2, the forearm 1.0 (the arm 2.2), the finger 0.4.
enum { kPelvis, kSpine, kChest, kNeck, kHead, kRClav, kRUpr, kRLwr, kRHand, kRFing, kLClav, kLUpr, kLLwr, kLHand, kLFing, kCount };
struct Rig {
	Hero h;
	std::vector<Mat> base;
	explicit Rig(bool clavicles = true) {
		h.count = kCount;
		const int parent[kCount] = {-1, kPelvis, kSpine, kChest, kNeck, kChest, clavicles ? kRClav : kChest, kRUpr, kRLwr, kRHand,
		                            kChest, clavicles ? kLClav : kChest, kLUpr, kLLwr, kLHand};
		h.parent.assign(parent, parent + kCount);
		const V3 at[kCount] = {{0, 9, 0}, {0, 10, 0}, {0, 12, 0}, {0, 13, 0}, {0, 14, 0},
		                       {-0.2f, 12.6f, 0}, {-1.0f, 12.6f, 0}, {-2.2f, 12.6f, 0}, {-3.2f, 12.6f, 0}, {-3.6f, 12.6f, 0},
		                       {0.2f, 12.6f, 0}, {1.0f, 12.6f, 0}, {2.2f, 12.6f, 0}, {3.2f, 12.6f, 0}, {3.6f, 12.6f, 0}};
		for (int i = 0; i < kCount; ++i) base.push_back(At(at[i]));
		h.upper.assign(kCount, 0);
		h.inHead.assign(kCount, 0);
		h.inNeck.assign(kCount, 0);
		for (int i = kSpine; i < kCount; ++i) h.upper[i] = 1;
		h.inHead[kHead] = 1;
		h.spine = kSpine; h.neck = kNeck; h.head = kHead; h.pelvis = kPelvis;
		h.upr[0] = kRUpr; h.lwr[0] = kRLwr; h.hand[0] = kRHand;
		h.upr[1] = kLUpr; h.lwr[1] = kLLwr; h.hand[1] = kLHand;
		h.bind = base;
		h.bindOk = true;
	}
};

float ArmLen(const std::vector<Mat>& w, int u, int l, int k) { return Dist(Pos(w[l]), Pos(w[u])) + Dist(Pos(w[k]), Pos(w[l])); }

void Run() {
	const float kArm = 2.2f;
	// a slash across the body: the right hand ahead of the left side of the chest, 2.4 from the right shoulder (the arm reaches 2.2)
	const V3 across{0.6f, 11.8f, 1.6f};
	{
		Rig r;
		CHECK(Dist(across, Pos(r.base[kRUpr])) > kArm);
		CHECK(Clavicle(r.h, 0) == kRClav && Clavicle(r.h, 1) == kLClav);
		// off: the wrist stops at the arm's length, short of the target - as before
		g_fullReachPose = false;
		std::vector<Mat> w = r.base;
		Reach(r.h, 0, across, false, 1.0f, 0.0f, r.base, w);
		CHECK(Near(Dist(Pos(w[kRHand]), Pos(r.base[kRUpr])), kArm * 0.999f, 2e-3f));
		CHECK(Dist(Pos(w[kRHand]), across) > 0.15f);
		CHECK(memcmp(w[kRClav].m, r.base[kRClav].m, sizeof(Mat)) == 0);   // the collarbone is not touched
		// on: the wrist on the target
		g_fullReachPose = true;
		g_fullReachSeen[0] = FullReachSeen{};
		std::vector<Mat> f = r.base;
		Reach(r.h, 0, across, false, 1.0f, 0.0f, r.base, f);
		CHECK(Dist(Pos(f[kRHand]), across) < 2e-3f);
		// the collarbone turned, its own length kept, by no more than 20 degrees
		const V3 P = Pos(r.base[kRClav]), s0 = Sub(Pos(r.base[kRUpr]), P), s1 = Sub(Pos(f[kRUpr]), P);
		CHECK(Near(Len(s1), 0.8f, 1e-4f));
		const float turned = acosf(std::clamp(Dot(Norm(s0), Norm(s1)), -1.0f, 1.0f)) * 57.29578f;
		CHECK(turned > 1.0f && turned <= 20.01f);
		CHECK(Pos(f[kRUpr]).z > Pos(r.base[kRUpr]).z && Pos(f[kRUpr]).x > Pos(r.base[kRUpr]).x);   // the shoulder came forward and in
		// the arm lengthened by what the turn left, within 35 %; the hand and its finger at their own size
		const float len = ArmLen(f, kRUpr, kRLwr, kRHand);
		CHECK(len >= kArm * 0.999f && len <= kArm * 1.35f + 1e-3f);
		CHECK(Near(RowLen(f[kRHand], 0), 1.0f, 1e-4f) && Near(RowLen(f[kRHand], 1), 1.0f, 1e-4f) && Near(RowLen(f[kRHand], 2), 1.0f, 1e-4f));
		CHECK(Near(Dist(Pos(f[kRFing]), Pos(f[kRHand])), 0.4f, 1e-4f));
		// the left arm and the trunk untouched
		for (int i : {kPelvis, kSpine, kChest, kNeck, kHead, kLClav, kLUpr, kLLwr, kLHand, kLFing}) CHECK(memcmp(f[i].m, r.base[i].m, sizeof(Mat)) == 0);
		CHECK(g_fullReachSeen[0].acted == 1 && g_fullReachSeen[0].shortPct > 5.0f && g_fullReachSeen[0].turnDeg > 1.0f);
		// a second Reach of the same arm in this pose (the grip fit's): the same arm, not turned and lengthened again on top
		std::vector<Mat> twice = f;
		Reach(r.h, 0, across, false, 1.0f, 0.0f, r.base, twice);
		CHECK(Dist(Pos(twice[kRHand]), across) < 2e-3f);
		CHECK(Dist(Pos(twice[kRUpr]), Pos(f[kRUpr])) < 1e-4f);
		CHECK(Near(ArmLen(twice, kRUpr, kRLwr, kRHand), len, 1e-3f));
		// ... and a second one to a target in reach puts it all back: the collarbone as it was, the arm its own length
		const V3 inReach{-2.0f, 11.0f, 1.0f};
		Reach(r.h, 0, inReach, false, 1.0f, 0.0f, r.base, twice);
		CHECK(memcmp(twice[kRClav].m, r.base[kRClav].m, sizeof(Mat)) == 0);
		CHECK(Near(ArmLen(twice, kRUpr, kRLwr, kRHand), kArm, 1e-3f));
		CHECK(Dist(Pos(twice[kRHand]), inReach) < 2e-3f);
	}
	// in reach from the start: nothing of full_reach happens (the collarbone, the arm's length), the wrist on the target as ever
	{
		Rig r;
		g_fullReachPose = true;
		g_fullReachSeen[0] = FullReachSeen{};
		const V3 t{-2.0f, 11.0f, 1.0f};
		std::vector<Mat> w = r.base;
		Reach(r.h, 0, t, false, 1.0f, 0.0f, r.base, w);
		CHECK(Dist(Pos(w[kRHand]), t) < 2e-3f);
		CHECK(memcmp(w[kRClav].m, r.base[kRClav].m, sizeof(Mat)) == 0);
		CHECK(Near(ArmLen(w, kRUpr, kRLwr, kRHand), kArm, 1e-4f));
		CHECK(g_fullReachSeen[0].acted == 0);
		// ... the same as with it off
		g_fullReachPose = false;
		std::vector<Mat> o = r.base;
		Reach(r.h, 0, t, false, 1.0f, 0.0f, r.base, o);
		for (int i = 0; i < kCount; ++i) CHECK(Dist(Pos(o[i]), Pos(w[i])) < 1e-5f);
	}
	// a controller that jumped far away: the turn at its bound, the stretch at its bound, the hand left short
	{
		Rig r;
		g_fullReachPose = true;
		const V3 away{8.0f, 12.0f, 6.0f};
		std::vector<Mat> w = r.base;
		Reach(r.h, 0, away, false, 1.0f, 0.0f, r.base, w);
		CHECK(Near(ArmLen(w, kRUpr, kRLwr, kRHand), kArm * 0.999f * 1.35f, 5e-3f));
		CHECK(Dist(Pos(w[kRHand]), away) > 1.0f);
		const V3 P = Pos(r.base[kRClav]);
		const float turned = acosf(std::clamp(Dot(Norm(Sub(Pos(r.base[kRUpr]), P)), Norm(Sub(Pos(w[kRUpr]), P))), -1.0f, 1.0f)) * 57.29578f;
		CHECK(Near(turned, 20.0f, 0.05f));
	}
	// no collarbone of its own (the upper arm on the chest, the other arm under it too): no turn - the arm lengthens alone
	{
		Rig r(false);
		CHECK(Clavicle(r.h, 0) == -1 && Clavicle(r.h, 1) == -1);
		g_fullReachPose = true;
		std::vector<Mat> w = r.base;
		Reach(r.h, 0, across, false, 1.0f, 0.0f, r.base, w);
		CHECK(Dist(Pos(w[kRHand]), across) < 2e-3f);
		CHECK(memcmp(w[kChest].m, r.base[kChest].m, sizeof(Mat)) == 0);
		CHECK(Dist(Pos(w[kRUpr]), Pos(r.base[kRUpr])) < 1e-6f);
		CHECK(Near(Dist(Pos(w[kRFing]), Pos(w[kRHand])), 0.4f, 1e-4f));
	}
	// the left arm reaching across to the right: the right arm's case mirrored
	{
		Rig r;
		g_fullReachPose = true;
		std::vector<Mat> wr = r.base, wl = r.base;
		const V3 acrossL{-across.x, across.y, across.z};
		Reach(r.h, 0, across, false, 1.0f, 0.0f, r.base, wr);
		Reach(r.h, 1, acrossL, false, 1.0f, 0.0f, r.base, wl);
		CHECK(Dist(Pos(wl[kLHand]), acrossL) < 2e-3f);
		CHECK(Near(Pos(wl[kLUpr]).x, -Pos(wr[kRUpr]).x, 1e-4f) && Near(Pos(wl[kLUpr]).z, Pos(wr[kRUpr]).z, 1e-4f));
		CHECK(Near(ArmLen(wl, kLUpr, kLLwr, kLHand), ArmLen(wr, kRUpr, kRLwr, kRHand), 1e-3f));
	}
	g_fullReachPose = false;
}

}  // namespace tst
}  // namespace skel

int main() {
	skel::tst::Run();
	if (skel::tst::g_fails) {
		std::printf("full_reach_rig_test: %d failed\n", skel::tst::g_fails);
		return 1;
	}
	std::printf("full_reach_rig_test: all passed\n");
	return 0;
}
