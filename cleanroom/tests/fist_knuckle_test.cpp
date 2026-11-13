// [fist] from_knuckle (vr/finger_chain.h): a free hand's fingers curl from the KNUCKLE the rig's own bones tell - the skeleton hook's Fist ITSELF (vr/skeletons.cpp, included whole)
// on toy hands of the game's three makes of hero rig.
//
// Serg, 0.215.13: the Amazon's free hand, half closed, folded out of the middle of the palm - knuckles twisted, the fingers leaning into each other, the thumb across the index. Her
// rig's finger chains have FOUR bones and the first lies inside the palm; the fist bent it as the knuckle (so did the Paladin's and the Druid's).
//
//   - the rule (fingerchain::FirstBend) on the chains of the game's eight hero rigs, as read off data/hd/character/player/<class>/skeleton: the Amazon's, the Paladin's and the Druid's
//     four-bone fingers start at their second bone, the Assassin's named metacarpals as before, every three-bone finger and every thumb at its first; with the key off, the name alone;
//   - Fist on a hand of the Amazon's make: the palm bones stay as the bind pose has them (the palm does not fold), the knuckle stays in its place and bends by the table's first angle,
//     the middle joint by the second, the LAST joint by the third (before: it never bent) - either hand, every bone with axes of its own;
//   - with the key off the same hand is bent as it was up to 0.215 (the palm bone turned, the last joint not): the old behaviour is kept behind the key, bit for bit a fist that
//     starts one bone earlier;
//   - hands of the other two makes (three bones a finger; the Assassin's named metacarpals) are bent bit for bit the same with the key on and off: the other classes do not change;
//   - nothing comes out not a number, and a bone keeps its scale and handedness.
//
//   fist_knuckle_test

#include "../../vr/skeletons.cpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// ---- the rule on the game's rigs ----
struct ChainShape { const char* cls; const char* first; int bones; bool thumb; int knuckle; };
// (every finger chain of the right hand of each class's skeleton: its first bone, how many bones it runs to, and where its knuckle is)
const ChainShape kGame[] = {
	{"amazon", "R_ind_finger_01_bind_jnt", 4, false, 1}, {"amazon", "R_mid_finger_01_bind_jnt", 4, false, 1}, {"amazon", "R_rng_finger_01_bind_jnt", 4, false, 1},
	{"amazon", "R_pnk_finger_01_bind_jnt", 4, false, 1}, {"amazon", "R_tmb_finger_01_bind_jnt", 3, true, 0},
	{"paladin", "R_ind_finger_01_bind_jnt", 4, false, 1}, {"paladin", "R_mid_finger_01_bind_jnt", 4, false, 1}, {"paladin", "R_rng_finger_01_bind_jnt", 4, false, 1},
	{"paladin", "R_pnk_finger_01_bind_jnt", 4, false, 1}, {"paladin", "R_tmb_finger_01_bind_jnt", 3, true, 0},
	{"druid", "R_ind_finger_01_bind_jnt", 4, false, 1}, {"druid", "R_mid_finger_01_bind_jnt", 4, false, 1}, {"druid", "R_rng_finger_01_bind_jnt", 4, false, 1},
	{"druid", "R_pnk_finger_01_bind_jnt", 4, false, 1}, {"druid", "R_tmb_finger_01_bind_jnt", 3, true, 0},
	{"assassin", "hand_ring_metacarpal_R_JNT", 4, false, 1}, {"assassin", "hand_pinky_metacarpal_R_JNT", 4, false, 1}, {"assassin", "hand_middle_01_R_JNT", 3, false, 0},
	{"assassin", "hand_index_01_R_JNT", 3, false, 0}, {"assassin", "hand_thumb_01_R_JNT", 3, true, 0},
	{"barbarian", "R_finger_A_A", 3, false, 0}, {"barbarian", "R_finger_B_A", 3, false, 0}, {"barbarian", "R_finger_C_A", 3, false, 0}, {"barbarian", "R_finger_D_A", 3, false, 0},
	{"barbarian", "R_thumb_A_A", 3, true, 0},
	{"sorceress", "R_finger_A_A", 3, false, 0}, {"sorceress", "R_finger_D_A", 3, false, 0}, {"sorceress", "R_thumb_A_A", 3, true, 0},
	{"necromancer", "R_finger_A_A", 3, false, 0}, {"necromancer", "R_finger_D_A", 3, false, 0}, {"necromancer", "R_thumb_A_A", 3, true, 0},
	{"warlock", "R_finger_A_A", 3, false, 0}, {"warlock", "R_finger_D_A", 3, false, 0}, {"warlock", "R_thumb_A_A", 3, true, 0},
	{"druid's bear form", "R_finger1_bind_jnt", 2, false, 0},
};

void Rule() {
	std::printf("the rule on the game's hero rigs\n");
	int changed = 0;
	for (const ChainShape& c : kGame) {
		const bool named = std::string(c.first).find("metacarpal") != std::string::npos;
		const int on = fingerchain::FirstBend(c.bones, c.thumb, named, true), off = fingerchain::FirstBend(c.bones, c.thumb, named, false);
		CHECK(on == c.knuckle);
		CHECK(off == (named ? 1 : 0));   // the key off: the name alone, as it was
		CHECK(on + fingerchain::kFingerJoints <= c.bones || c.bones < fingerchain::kFingerJoints + 1);   // three joints left to bend where the chain has them
		if (on != off) {
			++changed;
			const std::string cls = c.cls;
			CHECK(cls == "amazon" || cls == "paladin" || cls == "druid");   // the three rigs of the one make, no other
		}
	}
	std::printf("  %d chains, %d of them start a bone later with the key on (the Amazon's, the Paladin's, the Druid's four fingers)\n", (int)(sizeof kGame / sizeof kGame[0]), changed);
	CHECK(changed == 12);
	// a chain of a length the rule does not know: the name's word
	CHECK(fingerchain::FirstBend(5, false, false, true) == 0 && fingerchain::FirstBend(5, false, true, true) == 1);
	CHECK(fingerchain::FirstBend(1, false, false, true) == 0 && fingerchain::FirstBend(0, false, false, true) == 0);
	CHECK(fingerchain::FirstBend(4, true, false, true) == 0);   // a thumb of four bones: not a finger's count
}

// ---- toy hands: model space, the wrist at the origin, the fingers ahead (+z), the palm facing up (+y) on either hand ----
enum Make { kAmazonMake, kBarbarianMake, kAssassinMake };

Mat Turned(int seed, float x, float y, float z, bool mirror, float scale) {   // a bone with axes of its own (any rotation), a left hand's a mirror, at a place
	const float a = 0.37f * (float)(seed + 1), b = 0.71f * (float)(seed + 2);
	const float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
	Mat m{};
	// Rz(a) * Rx(b), rows
	const float r[3][3] = {{ca, sa * cb, sa * sb}, {-sa, ca * cb, ca * sb}, {0.0f, -sb, cb}};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) m.m[i * 4 + j] = r[i][j] * scale * (mirror && i == 0 ? -1.0f : 1.0f);
	m.m[12] = x; m.m[13] = y; m.m[14] = z; m.m[15] = 1.0f;
	return m;
}

struct Hand {
	Hero h;
	std::vector<Mat> bind;
	int side = 0;
};

// The four fingers stand side by side; a finger of four bones has its first bone halfway to the knuckle (the Amazon's make), of three starts at the knuckle. Lengths in
// the Amazon's own proportions (model units: 16 to the palm bone, 18.5 on to the knuckle, 15, 10.4).
Hand MakeHand(Make make, int side) {
	Hand o;
	o.side = side;
	Hero& h = o.h;
	const float sx = side == 1 ? 1.0f : -1.0f;   // the index on +x on the left hand, on -x on the right: the palm faces +y on both (Fist's own rule)
	const bool mirror = side == 1;
	auto add = [&](int parent, float x, float y, float z) {
		const int i = (int)o.bind.size();
		o.bind.push_back(Turned(i, x, y, z, mirror, 1.0f + 0.01f * (float)(i % 5)));
		h.parent.push_back(parent);
		return i;
	};
	const int wrist = add(-1, 0.0f, 0.0f, 0.0f);
	h.hand[side] = wrist;
	static const char* const kNames[4] = {"ind", "mid", "rng", "pnk"};
	for (int c = 0; c < 4; ++c) {
		const float kx = (1.5f - (float)c) * 0.08f * sx;   // the knuckle's place across the hand
		const bool four = make == kAmazonMake || (make == kAssassinMake && c >= 2);
		Hero::Finger f;
		int b = wrist;
		if (four) { b = add(b, kx * 0.48f, 0.0f, 0.16f); f.bones.push_back(b); }
		const float z[3] = {0.345f, 0.495f, 0.599f};
		for (int j = 0; j < 3; ++j) { b = add(b, kx, 0.0f, z[j]); f.bones.push_back(b); }
		const bool named = make == kAssassinMake && four;
		f.firstBend = named ? 1 : 0;
		f.thumb = false;
		f.knuckle = fingerchain::FirstBend((int)f.bones.size(), false, named, true);   // (as Learn sets it)
		h.fingers[side].push_back(f);
		const int first = f.bones[0];
		if (kNames[c][0] == 'i') h.ind[side] = first;
		if (kNames[c][0] == 'm') h.mid[side] = first;
		if (kNames[c][0] == 'p') h.pnk[side] = first;
	}
	{   // the thumb: three bones on every make
		Hero::Finger f;
		int b = wrist;
		const float t[3][2] = {{0.10f, 0.08f}, {0.19f, 0.17f}, {0.25f, 0.27f}};
		for (int j = 0; j < 3; ++j) { b = add(b, t[j][0] * sx, 0.0f, t[j][1]); f.bones.push_back(b); }
		f.thumb = true;
		f.knuckle = fingerchain::FirstBend(3, true, false, true);
		h.fingers[side].push_back(f);
	}
	h.count = (int)o.bind.size();
	h.bind = o.bind;
	h.bindOk = true;
	return o;
}

V3 Dir(const std::vector<Mat>& w, int from, int to) { return Norm(Sub(Pos(w[to]), Pos(w[from]))); }
float Deg(V3 a, V3 b) { return acosf(std::clamp(Dot(Norm(a), Norm(b)), -1.0f, 1.0f)) * 57.29578f; }
// the angle a bone's axes turned by between two poses (its rows made unit; the handedness is the same in both)
float TurnDeg(const Mat& a, const Mat& b) {
	float tr = 0.0f;
	for (int r = 0; r < 3; ++r) tr += Dot(Norm(V3{a.m[r * 4], a.m[r * 4 + 1], a.m[r * 4 + 2]}), Norm(V3{b.m[r * 4], b.m[r * 4 + 1], b.m[r * 4 + 2]}));
	return acosf(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.29578f;
}
bool Same(const Mat& a, const Mat& b) { return memcmp(a.m, b.m, sizeof a.m) == 0; }
bool Near(const Mat& a, const Mat& b, float eps) {
	for (int i = 0; i < 16; ++i)
		if (!(std::fabs(a.m[i] - b.m[i]) <= eps)) return false;
	return true;
}
float Det3(const Mat& m) {
	const float* q = m.m;
	return q[0] * (q[5] * q[10] - q[6] * q[9]) - q[1] * (q[4] * q[10] - q[6] * q[8]) + q[2] * (q[4] * q[9] - q[5] * q[8]);
}
float RowLenOf(const Mat& m, int r) { return Len(V3{m.m[r * 4], m.m[r * 4 + 1], m.m[r * 4 + 2]}); }

std::vector<Mat> Closed(const Hand& hd, float amount, bool knuckle) {
	std::vector<Mat> w = hd.bind;
	Fist(hd.h, hd.side, 1.0f, amount, 0.2f, knuckle, hd.bind, w);
	return w;
}

void AmazonMake() {
	std::printf("Fist on a hand of the Amazon's make (four bones a finger, the first inside the palm)\n");
	for (int side = 0; side < 2; ++side) {
		const Hand hd = MakeHand(kAmazonMake, side);
		for (const float amount : {0.5f, 1.0f}) {
			const std::vector<Mat> on = Closed(hd, amount, true), off = Closed(hd, amount, false);
			float worst = 0.0f, lift = 0.0f;
			for (const Hero::Finger& f : hd.h.fingers[side]) {
				if (f.thumb) {
					for (const int b : f.bones) CHECK(Same(on[b], off[b]));   // the thumb is not the key's
					continue;
				}
				CHECK(f.knuckle == 1 && f.firstBend == 0 && f.bones.size() == 4);
				const int b0 = f.bones[0], b1 = f.bones[1], b2 = f.bones[2], b3 = f.bones[3];
				// the key on: the palm bone as the bind pose has it, the knuckle in its place - the palm does not fold
				CHECK(Near(on[b0], hd.bind[b0], 1e-5f));
				CHECK(Len(Sub(Pos(on[b1]), Pos(hd.bind[b1]))) < 1e-5f);
				// ... the three joints by the table: the knuckle, the middle joint, the last one
				const float knuckle = Deg(Dir(on, b1, b2), Dir(hd.bind, b1, b2)), middle = Deg(Dir(on, b2, b3), Dir(hd.bind, b2, b3));
				const float last = TurnDeg(on[b3], hd.bind[b3]);
				const float wantK = kFingerFistDeg[0] * amount, wantM = (kFingerFistDeg[0] + kFingerFistDeg[1]) * amount;
				const float wantL = (kFingerFistDeg[0] + kFingerFistDeg[1] + kFingerFistDeg[2]) * amount;
				worst = std::max({worst, std::fabs(knuckle - wantK), std::fabs(middle - wantM), std::fabs(last - (wantL > 180.0f ? 360.0f - wantL : wantL))});
				CHECK(TurnDeg(on[b1], hd.bind[b1]) > wantK - 0.5f && TurnDeg(on[b1], hd.bind[b1]) < wantK + 0.5f);
				CHECK(TurnDeg(on[b3], on[b2]) > 1.0f);   // the last joint bends against the one before it
				// ... toward the palm (+y on these hands)
				CHECK(Pos(on[b2]).y > 0.01f && Pos(on[b3]).y > Pos(on[b2]).y - 1e-4f);
				// the key off, as up to 0.215: the palm bone turned (the knuckle lifted out of the palm), the last joint never bent
				CHECK(TurnDeg(off[b0], hd.bind[b0]) > kFingerFistDeg[0] * amount - 0.5f);
				CHECK(Pos(off[b1]).y > 0.05f);
				CHECK(TurnDeg(off[b3], hd.bind[b3]) < TurnDeg(off[b2], hd.bind[b2]) + 0.01f && TurnDeg(off[b3], hd.bind[b3]) > TurnDeg(off[b2], hd.bind[b2]) - 0.01f);
				lift = std::max(lift, Pos(off[b1]).y);
				// every bone a proper one of its own scale and handedness
				for (const int b : f.bones) {
					for (float v : on[b].m) CHECK(std::isfinite(v));
					CHECK((Det3(on[b]) < 0.0f) == (Det3(hd.bind[b]) < 0.0f));
					for (int r = 0; r < 3; ++r) CHECK(std::fabs(RowLenOf(on[b], r) - RowLenOf(hd.bind[b], r)) < 1e-4f);
				}
			}
			std::printf("  the %s hand, %3.0f %% of a fist: the joints within %.2f deg of the table, the palm bones as the bind pose has them (key off: the knuckles lifted %.2f units out of the palm)\n",
			            side ? "left" : "right", amount * 100.0f, worst, lift);
			CHECK(worst < 0.5f);
		}
		// an open hand (nothing squeezed, open 0): the bind pose either way
		{
			std::vector<Mat> w = hd.bind;
			Fist(hd.h, side, 0.0f, 1.0f, 0.0f, true, hd.bind, w);
			for (const Hero::Finger& f : hd.h.fingers[side])
				for (const int b : f.bones) CHECK(Near(w[b], hd.bind[b], 1e-5f));
		}
	}
}

void OtherMakes() {
	std::printf("the other makes: the key changes nothing\n");
	for (const Make make : {kBarbarianMake, kAssassinMake})
		for (int side = 0; side < 2; ++side) {
			const Hand hd = MakeHand(make, side);
			int bones = 0, moved = 0;
			for (const float amount : {0.3f, 0.65f, 1.0f}) {
				const std::vector<Mat> on = Closed(hd, amount, true), off = Closed(hd, amount, false);
				for (const Hero::Finger& f : hd.h.fingers[side]) {
					CHECK(f.knuckle == f.firstBend);
					for (const int b : f.bones) {
						++bones;
						CHECK(Same(on[b], off[b]));
						if (!Near(on[b], hd.bind[b], 1e-5f)) ++moved;
					}
				}
			}
			std::printf("  %s make, the %s hand: %d bones bit for bit the same with the key on and off (%d of them moved by the fist)\n", make == kBarbarianMake ? "the Barbarian's" : "the Assassin's",
			            side ? "left" : "right", bones, moved);
			CHECK(moved > bones / 2);
		}
	// the Assassin's named metacarpals are not bent with the key on or off (as before)
	{
		const Hand hd = MakeHand(kAssassinMake, 1);
		const std::vector<Mat> on = Closed(hd, 1.0f, true);
		for (const Hero::Finger& f : hd.h.fingers[1])
			if (f.firstBend == 1) CHECK(Near(on[f.bones[0]], hd.bind[f.bones[0]], 1e-5f));
	}
}

}  // namespace tst
}  // namespace skel

int main() {
	using namespace skel::tst;
	std::printf("fist_knuckle_test ([fist] from_knuckle: the fingers curl from the knuckle the rig's own bones tell)\n");
	Rule();
	AmazonMake();
	OtherMakes();
	std::printf(g_fails == 0 ? "fist_knuckle_test: all passed\n" : "fist_knuckle_test: %d failed\n", g_fails);
	return g_fails == 0 ? 0 : 1;
}
