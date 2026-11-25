// [hands] palm_from_knuckle (vr/palm_knuckle.h): the hand on the controller placed by its palm's middle half way to the middle finger's KNUCKLE - the skeleton hook's own
// PlanWrist / PlanPalm / ApplyWrist (vr/skeletons.cpp, included whole) on the hands of the game's eight hero rigs, both sides.
//
// Idea: Berk (bearstonem), D2R VR Unleashed commit 12bd172 (https://github.com/bearstonem/d2r-pcvr/commit/12bd172) - thank you.
//
// The rigs: each class's hand bone (its bind-pose world matrix as the game's rig has it - a left hand mirrored, the sorceress-make hands scaled) and the
// first bones of its index, middle and little finger, and the middle finger's knuckle, read off data/hd/character/player/<class>/skeleton (2026-11-25,
// model units; eyes: the eyeball bones' height). Amazon, Paladin, Druid: four bones a finger, the first inside the palm; the others: three, the first the knuckle.
//
//   - key off: the wrist goes exactly where it went (half a hand behind the grip along where the hand points, bit for bit), and the hand's real palm's middle
//     lands 2.22 / 2.14 / 2.04 cm from the grip on the Amazon's / Paladin's / Druid's hands (eyes 1.634 m: 2.19 / 2.13 / 2.03 of it along the hand, the
//     rest because the knuckle is a few degrees off the line the hand points along), 0 on the other five;
//   - key on: the real palm's middle lands on the grip on every class, both hands, any controller turn (within a tenth of a millimetre);
//   - key on, the five three-bone rigs: bit for bit as with it off;
//   - the knuckle rule (palmknuckle::KnuckleIndex) on the chain shapes; Turned keeps a vector's length and is the identity for an unturned hand.
//
//   palm_knuckle_test

#include "../../vr/skeletons.cpp"

#include <cstdio>
#include <cstring>

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

struct Rig {
	const char* cls;
	int side;        // 0 right, 1 left
	float eye;       // the eyes' height, model units
	int midBones;    // how many bones the middle finger's chain has
	float hand[12];  // the hand bone's bind world matrix: its three axes (rows), then its place
	float ind[3], mid[3], knuckle[3], pnk[3];   // the index's, the middle finger's and the little finger's first bones, the middle finger's knuckle
};
const Rig kRigs[] = {
	{"amazon", 0, 6.2578f, 4, {0.55144f, 0.76381f, -0.33542f, 0.80242f, -0.59561f, -0.03710f, -0.22812f, -0.24869f, -0.94134f, -2.02859f, 4.21461f, 0.34061f}, {-2.07694f, 4.13036f, 0.46798f}, {-2.11418f, 4.10831f, 0.41644f}, {-2.20498f, 3.97236f, 0.46304f}, {-2.13884f, 4.07578f, 0.31404f}},
	{"amazon", 1, 6.2578f, 4, {0.55144f, -0.76381f, 0.33542f, 0.80242f, 0.59561f, 0.03710f, -0.22812f, 0.24869f, 0.94134f, 2.02858f, 4.21459f, 0.34060f}, {2.07694f, 4.13036f, 0.46797f}, {2.11419f, 4.10829f, 0.41644f}, {2.20498f, 3.97235f, 0.46304f}, {2.13883f, 4.07578f, 0.31404f}},
	{"assassin", 0, 5.8869f, 3, {0.57818f, 0.72271f, -0.37868f, 0.74284f, -0.65825f, -0.12207f, -0.33749f, -0.21072f, -0.91744f, -1.92047f, 3.98963f, 0.31991f}, {-2.06209f, 3.77756f, 0.50191f}, {-2.07688f, 3.75920f, 0.43645f}, {-2.07688f, 3.75920f, 0.43645f}, {-2.07804f, 3.73954f, 0.29178f}},
	{"assassin", 1, 5.8869f, 3, {0.57818f, -0.72270f, 0.37869f, 0.74284f, 0.65825f, 0.12205f, -0.33747f, 0.21074f, 0.91744f, 1.92046f, 3.98964f, 0.31990f}, {2.06209f, 3.77756f, 0.50192f}, {2.07688f, 3.75922f, 0.43645f}, {2.07688f, 3.75922f, 0.43645f}, {2.07803f, 3.73955f, 0.29178f}},
	{"barbarian", 0, 7.0459f, 3, {1.14762f, 0.04100f, -0.06161f, -0.04101f, 1.14927f, 0.00096f, 0.06160f, 0.00124f, 1.14835f, -3.15793f, 6.03593f, -0.04296f}, {-3.53367f, 6.03297f, 0.05511f}, {-3.53649f, 6.04811f, -0.06214f}, {-3.53649f, 6.04811f, -0.06214f}, {-3.46275f, 6.00677f, -0.27796f}},
	{"barbarian", 1, 7.0459f, 3, {1.14762f, -0.04100f, 0.06161f, -0.04101f, -1.14927f, -0.00096f, 0.06160f, -0.00124f, -1.14835f, 3.15793f, 6.03592f, -0.04296f}, {3.53367f, 6.03296f, 0.05511f}, {3.53649f, 6.04810f, -0.06214f}, {3.53649f, 6.04810f, -0.06214f}, {3.46276f, 6.00676f, -0.27796f}},
	{"druid", 0, 7.0691f, 4, {0.41246f, 0.85639f, -0.31059f, 0.82895f, -0.49422f, -0.26189f, -0.37778f, -0.14945f, -0.91375f, -2.23809f, 4.40345f, 0.28356f}, {-2.30716f, 4.25204f, 0.42676f}, {-2.33401f, 4.23707f, 0.35845f}, {-2.42061f, 4.09419f, 0.41407f}, {-2.34358f, 4.22465f, 0.22628f}},
	{"druid", 1, 7.0691f, 4, {0.41246f, -0.85639f, 0.31059f, 0.82895f, 0.49422f, 0.26189f, -0.37778f, 0.14945f, 0.91375f, 2.23809f, 4.40344f, 0.28356f}, {2.30716f, 4.25202f, 0.42676f}, {2.33402f, 4.23707f, 0.35845f}, {2.42062f, 4.09419f, 0.41407f}, {2.34358f, 4.22465f, 0.22628f}},
	{"necromancer", 0, 6.8062f, 3, {1.14562f, 0.00373f, -0.10016f, 0.00000f, 1.14920f, 0.04281f, 0.10023f, -0.04265f, 1.14483f, -3.04053f, 5.96431f, 0.01597f}, {-3.38015f, 6.00562f, 0.11730f}, {-3.37651f, 6.00920f, 0.01545f}, {-3.37651f, 6.00920f, 0.01545f}, {-3.31709f, 5.96125f, -0.14022f}},
	{"necromancer", 1, 6.8062f, 3, {1.14562f, -0.00373f, 0.10016f, -0.00000f, -1.14920f, -0.04281f, 0.10023f, 0.04265f, -1.14483f, 3.04053f, 5.96430f, 0.01597f}, {3.38014f, 6.00561f, 0.11730f}, {3.37650f, 6.00921f, 0.01545f}, {3.37650f, 6.00921f, 0.01545f}, {3.31709f, 5.96124f, -0.14021f}},
	{"paladin", 0, 6.8915f, 4, {0.39089f, 0.87349f, -0.29020f, 0.86946f, -0.45388f, -0.19501f, -0.30206f, -0.17609f, -0.93688f, -2.19227f, 4.27254f, 0.28924f}, {-2.25113f, 4.15374f, 0.43661f}, {-2.28803f, 4.13212f, 0.35952f}, {-2.36927f, 3.97884f, 0.41008f}, {-2.29832f, 4.09426f, 0.23130f}},
	{"paladin", 1, 6.8915f, 4, {0.39089f, -0.87349f, 0.29020f, 0.86946f, 0.45388f, 0.19501f, -0.30206f, 0.17609f, 0.93688f, 2.19227f, 4.27254f, 0.28924f}, {2.25112f, 4.15375f, 0.43661f}, {2.28803f, 4.13213f, 0.35952f}, {2.36927f, 3.97883f, 0.41008f}, {2.29832f, 4.09428f, 0.23130f}},
	{"sorceress", 0, 5.9822f, 3, {1.14842f, 0.00224f, -0.06014f, 0.00000f, 1.14920f, 0.04281f, 0.06019f, -0.04276f, 1.14763f, -2.43256f, 5.19432f, 0.04989f}, {-2.73522f, 5.15001f, 0.14964f}, {-2.72914f, 5.15262f, 0.07799f}, {-2.72914f, 5.15262f, 0.07799f}, {-2.68611f, 5.11861f, -0.05322f}},
	{"sorceress", 1, 5.9822f, 3, {1.14842f, -0.00224f, 0.06014f, 0.00000f, -1.14920f, -0.04281f, 0.06019f, 0.04276f, -1.14763f, 2.43255f, 5.19432f, 0.04989f}, {2.73520f, 5.15002f, 0.14964f}, {2.72913f, 5.15261f, 0.07799f}, {2.72913f, 5.15261f, 0.07799f}, {2.68610f, 5.11860f, -0.05322f}},
	{"warlock", 0, 6.7918f, 3, {1.14562f, 0.00373f, -0.10016f, 0.00000f, 1.14920f, 0.04281f, 0.10023f, -0.04265f, 1.14483f, -3.04053f, 5.96431f, 0.01597f}, {-3.38015f, 6.00562f, 0.11730f}, {-3.37651f, 6.00920f, 0.01545f}, {-3.37651f, 6.00920f, 0.01545f}, {-3.31709f, 5.96125f, -0.14022f}},
	{"warlock", 1, 6.7918f, 3, {1.14562f, -0.00373f, 0.10016f, -0.00000f, -1.14920f, -0.04281f, 0.10023f, 0.04265f, -1.14483f, 3.04053f, 5.96430f, 0.01597f}, {3.38014f, 6.00561f, 0.11730f}, {3.37650f, 6.00921f, 0.01545f}, {3.37650f, 6.00921f, 0.01545f}, {3.31709f, 5.96124f, -0.14021f}},
};
constexpr float kUserEye = 1.634f;   // metres

struct Built { Hero h; std::vector<Mat> w; };
Built Build(const Rig& r) {
	Built b;
	Hero& h = b.h;
	Mat hand{};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) hand.m[i * 4 + j] = r.hand[i * 3 + j];
	hand.m[12] = r.hand[9]; hand.m[13] = r.hand[10]; hand.m[14] = r.hand[11]; hand.m[15] = 1.0f;
	auto at = [](const float* p) { Mat m{}; m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f; m.m[12] = p[0]; m.m[13] = p[1]; m.m[14] = p[2]; return m; };
	b.w = {hand, at(r.ind), at(r.mid), at(r.knuckle), at(r.pnk)};
	h.count = 5;
	h.parent = {-1, 0, 0, 2, 0};
	h.hand[r.side] = 0; h.ind[r.side] = 1; h.mid[r.side] = 2; h.pnk[r.side] = 4;
	h.midKnuckle[r.side] = palmknuckle::KnuckleIndex(r.midBones, false) == 1 ? 3 : 2;   // (as Learn sets it from the chain)
	return b;
}

// Where the hook puts the wrist for a controller grip at G turned by q (x y z w), and where the hand's real palm's middle (half way to the knuckle) then is.
struct Placed { V3 wrist, palm; };
Placed Place(const Built& b, int side, const float q[4], V3 G, bool knuckle, bool* ok) {
	Input in;
	in.wrist = true;
	for (int i = 0; i < 4; ++i) in.rot[side][i] = q[i];
	in.palmKnuckle = knuckle;
	const Wrist wr = PlanWrist(b.h, side, b.w, in, 1.0f, 0.0f);
	*ok = wr.ok;
	Placed o{};
	if (!wr.ok) return o;
	o.wrist = Sub(G, wr.palm);   // (PoseBody: T = the grip point - wr.palm)
	Mat hand = b.w[0];
	ApplyWrist(hand, wr);
	hand.m[12] = o.wrist.x; hand.m[13] = o.wrist.y; hand.m[14] = o.wrist.z;
	const V3 kn = Through(hand, Through(InvAffine(b.w[0]), Pos(b.w[3])));   // the knuckle carried with the hand
	o.palm = Scale(Add(o.wrist, kn), 0.5f);
	return o;
}

void Q(float ax, float ay, float az, float deg, float q[4]) {
	const float l = std::sqrt(ax * ax + ay * ay + az * az), h = 0.5f * deg * 0.0174532925f;
	q[0] = ax / l * std::sin(h); q[1] = ay / l * std::sin(h); q[2] = az / l * std::sin(h); q[3] = std::cos(h);
}

void Rule() {
	std::printf("the knuckle rule\n");
	CHECK(palmknuckle::KnuckleIndex(4, false) == 1);   // Amazon, Paladin, Druid: R_mid_finger_01 lies in the palm, 02 is the knuckle
	CHECK(palmknuckle::KnuckleIndex(3, false) == 0);   // the three-bone fingers start at the knuckle
	CHECK(palmknuckle::KnuckleIndex(4, true) == 1);    // a named metacarpal first (the Assassin's ring and little finger's make)
	CHECK(palmknuckle::KnuckleIndex(2, false) == 0 && palmknuckle::KnuckleIndex(1, false) == 0);   // the shapeshift forms' short fingers
	// Turned: an unturned hand keeps its palm vector; a turned one keeps its length
	const palmknuckle::V p{0.1f, -0.2f, 0.3f}, a{0.2f, 0.9f, 0.1f}, k{0.7f, -0.1f, 0.3f};
	const palmknuckle::V same = palmknuckle::Turned(p, a, k, a, k);
	CHECK(std::fabs(same.x - p.x) < 1e-5f && std::fabs(same.y - p.y) < 1e-5f && std::fabs(same.z - p.z) < 1e-5f);
	const palmknuckle::V t = palmknuckle::Turned(p, a, k, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f});
	CHECK(std::fabs(palmknuckle::Dot(t, t) - palmknuckle::Dot(p, p)) < 1e-5f);
}

void Classes() {
	std::printf("the game's hero rigs, both hands, 6 controller turns each (cm at eyes %.3f m)\n", kUserEye);
	float qs[6][4];
	Q(0, 0, 1, 0, qs[0]); Q(1, 0, 0, 90, qs[1]); Q(0, 1, 0, -70, qs[2]); Q(1, 1, 0, 135, qs[3]); Q(-0.3f, 0.8f, 0.5f, 200, qs[4]); Q(0.2f, -0.4f, 1, 33, qs[5]);
	const V3 G{0.4f, 3.9f, 1.2f};
	int classesFixed = 0;
	for (const Rig& r : kRigs) {
		const Built b = Build(r);
		const float upm = r.eye / kUserEye;
		const bool four = r.midBones == 4;
		float worstOff = 0.0f, bestOff = 1e9f, worstOn = 0.0f;
		for (const auto& q : qs) {
			bool ok0 = false, ok1 = false;
			const Placed off = Place(b, r.side, q, G, false, &ok0), on = Place(b, r.side, q, G, true, &ok1);
			CHECK(ok0 && ok1);
			if (!ok0 || !ok1) continue;
			// key off: the wrist as it was - half a hand (wrist -> middle finger's first bone) behind the grip, along where the hand points - bit for bit
			Input in;
			in.wrist = true;
			for (int i = 0; i < 4; ++i) in.rot[r.side][i] = q[i];
			const Wrist wr = PlanWrist(b.h, r.side, b.w, in, 1.0f, 0.0f);
			const V3 was = Sub(G, Scale(wr.point, 0.5f * wr.reach));
			CHECK(std::memcmp(&was, &off.wrist, sizeof was) == 0);
			const float offCm = Len(Sub(off.palm, G)) / upm * 100.0f, onCm = Len(Sub(on.palm, G)) / upm * 100.0f;
			worstOff = std::max(worstOff, offCm); bestOff = std::min(bestOff, offCm); worstOn = std::max(worstOn, onCm);
			CHECK(onCm < 0.01f);   // key on: the palm's middle on the grip
			if (four) {
				CHECK(Dot(Sub(off.palm, G), wr.point) > 0.0f);   // key off: past the grip, toward the fingertips
			} else {
				CHECK(std::memcmp(&on.wrist, &off.wrist, sizeof on.wrist) == 0);   // the five three-bone rigs: bit for bit the same with the key on
				CHECK(offCm < 0.01f);
			}
			CHECK(std::isfinite(on.wrist.x + on.wrist.y + on.wrist.z + on.palm.x + on.palm.y + on.palm.z));
		}
		std::printf("  %-12s %s  key off: the palm's middle %.2f cm past the grip; key on: %.3f cm\n", r.cls, r.side ? "left " : "right", worstOff, worstOn);
		CHECK(worstOff - bestOff < 0.01f);   // the same at any turn: a rigid hand
		// the numbers measured off the rigs (2026-11-25): Amazon 2.22, Paladin 2.14, Druid 2.04 (2.19 / 2.13 / 2.03 along the hand), the others 0
		const char* c = r.cls;
		const float want = !strcmp(c, "amazon") ? 2.22f : !strcmp(c, "paladin") ? 2.14f : !strcmp(c, "druid") ? 2.04f : 0.0f;
		CHECK(std::fabs(worstOff - want) < 0.01f);
		if (four) ++classesFixed;
	}
	CHECK(classesFixed == 6);   // the Amazon, the Paladin, the Druid - both hands
}

}  // namespace tst
}  // namespace skel

int main() {
	skel::tst::Rule();
	skel::tst::Classes();
	if (skel::tst::g_fails) {
		std::printf("palm_knuckle_test: %d FAILED\n", skel::tst::g_fails);
		return 1;
	}
	std::printf("palm_knuckle_test: all passed\n");
	return 0;
}
