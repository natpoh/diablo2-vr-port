// [weight] shield_collide: the weapon in one hand meets the hero's own shield on the other arm (vr/shield_parts.h, vr/shield_shape.h, vr/shield_collide.h,
// vr/weight.cpp). The shape: every shield the game has, read off its own mesh, is a face of thin capsules on its front surface, inside the mesh's box, covering
// it; the game's per-class attachment fit and the drawn bone put it where it is drawn, and the hands' frame is the skeleton's. The contact, through the weight
// model as the game runs it: a sword driven into a tower shield stops on its face (its hand drawn held back, the controller gone on), a knock is felt in the
// sword's hand and the shield's, a hard one heard (the game's block sound), a gentle one only felt, resting silent; slid along the face it glances on; pressed
// on past 60 cm it goes through; off, it passes as before. Never a strike.
//
//   shield_collide_test        (a few seconds: the steps are paced by the clock, as in the game)

#include "../../vr/weight.h"
#include "../../vr/clash_loud.h"
#include "../../vr/shield_collide.h"
#include "../../vr/shield_shape.h"
#include "hold.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
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

uint32_t Code(const char* c) { return weaponextent::C(c[0], c[1], c[2], c[3]); }

// ---- the shape ---------------------------------------------------------------------------------------------------------------------------------------------

// Every shield of the table: its capsules' ends within the mesh's box, each capsule's front on the face side and within a radius of the box's depth (the
// generator lays it through the middle of the mesh's front surface, within a radius of it), the four edges of the face reached, and most of the face's box covered (seen along the depth: a round shield fills pi/4 of its box).
void TestShapes() {
	int shapes = 0;
	float worstCover = 1.0f;
	const char* worstName = "";
	for (const shieldparts::Shape& sh : shieldparts::kShapes) {
		++shapes;
		EXPECT(sh.n >= 4 && sh.n <= shieldparts::kParts, "%s: %d capsules", sh.asset, sh.n);
		const int d = sh.depth, u = (d + 1) % 3, v = (d + 2) % 3;
		float reach[4] = {1e9f, -1e9f, 1e9f, -1e9f};   // the capsules' extent along u and v
		for (int i = 0; i < sh.n; ++i) {
			const shieldparts::Part& p = sh.p[i];
			EXPECT(p.r >= 0.039f && p.r <= 0.2f, "%s part %d: r %.3f units (1 - 5.5 cm)", sh.asset, i, p.r);
			for (const float* e : {p.a, p.b}) {
				for (int k = 0; k < 3; ++k)
					EXPECT(e[k] >= sh.lo[k] - 1e-3f && e[k] <= sh.hi[k] + 1e-3f, "%s part %d: an end outside the mesh's box on axis %d (%.3f, %.3f .. %.3f)", sh.asset, i, k, e[k], sh.lo[k], sh.hi[k]);
				const float front = e[d] + sh.front * p.r;   // its front surface over that end
				EXPECT(front >= sh.lo[d] - p.r - 0.005f && front <= sh.hi[d] + p.r + 0.005f, "%s part %d: its front %.3f more than a radius outside the box's depth %.3f .. %.3f", sh.asset, i,
				       front, sh.lo[d], sh.hi[d]);
				EXPECT(sh.front * e[d] > -0.5f * (sh.hi[d] - sh.lo[d]), "%s part %d: on the face's side of the grip (%.3f, front %+d)", sh.asset, i, e[d], sh.front);
				reach[0] = std::min(reach[0], e[u] - p.r);
				reach[1] = std::max(reach[1], e[u] + p.r);
				reach[2] = std::min(reach[2], e[v] - p.r);
				reach[3] = std::max(reach[3], e[v] + p.r);
			}
		}
		const float su = sh.hi[u] - sh.lo[u], sv = sh.hi[v] - sh.lo[v];
		EXPECT(reach[0] <= sh.lo[u] + 0.08f * su && reach[1] >= sh.hi[u] - 0.08f * su && reach[2] <= sh.lo[v] + 0.08f * sv && reach[3] >= sh.hi[v] - 0.08f * sv,
		       "%s: the capsules reach the face's four edges (%.2f .. %.2f of %.2f .. %.2f, %.2f .. %.2f of %.2f .. %.2f)", sh.asset, reach[0], reach[1], sh.lo[u], sh.hi[u], reach[2],
		       reach[3], sh.lo[v], sh.hi[v]);
		// the face's box seen along the depth, on a grid: the share of it under a capsule
		int in = 0, all = 0;
		for (int a = 0; a < 40; ++a)
			for (int b = 0; b < 40; ++b) {
				const float pu = sh.lo[u] + (a + 0.5f) / 40.0f * su, pv = sh.lo[v] + (b + 0.5f) / 40.0f * sv;
				++all;
				for (int i = 0; i < sh.n; ++i) {
					const shieldparts::Part& p = sh.p[i];
					const float du = p.b[u] - p.a[u], dv = p.b[v] - p.a[v], l2 = du * du + dv * dv;
					const float t = l2 > 1e-9f ? std::clamp(((pu - p.a[u]) * du + (pv - p.a[v]) * dv) / l2, 0.0f, 1.0f) : 0.0f;
					const float x = p.a[u] + du * t - pu, y = p.a[v] + dv * t - pv;
					if (x * x + y * y <= p.r * p.r) { ++in; break; }
				}
			}
		const float cover = (float)in / (float)all;
		if (cover < worstCover) { worstCover = cover; worstName = sh.asset; }
		EXPECT(cover >= 0.6f, "%s: the capsules cover %.0f %% of the face's box", sh.asset, cover * 100.0f);
	}
	std::printf("  %d shields read off the game's meshes: each a face of 4 .. %d capsules inside its box, on its front, reaching its edges; the least covered %s, %.0f %% of its box\n",
	            shapes, shieldparts::kParts, worstName, worstCover * 100.0f);
	EXPECT(shapes >= 13, "the game's 13 shield models (%d)", shapes);
	const shieldparts::Shape* buc = shieldparts::Of(Code("buc ")), *xuc = shieldparts::Of(Code("xuc ")), *pa1 = shieldparts::Of(Code("pa1 "));
	EXPECT(buc && std::strcmp(buc->asset, "buckler") == 0 && xuc == buc, "a Buckler and its exceptional Defender share the buckler's model");
	EXPECT(pa1 && std::strcmp(pa1->asset, "targe") == 0, "a Paladin's Targe is the targe");
	EXPECT(!shieldparts::Of(Code("ne1 ")) && !shieldparts::Of(Code("hax ")), "a Necromancer's head and a weapon are no shield");
	EXPECT(buc && buc->fit[3].have && std::fabs(buc->fit[3].s[0] - 1.1f) < 1e-4f && std::fabs(buc->fit[3].rotDeg[1] + 7.0f) < 1e-4f,
	       "the buckler hangs on a Paladin by its definition's fit: scale 1.1, 7 degrees about y");
}

// The fit: the scale, then the turns about x y z, then the move; none for a class without one.
void TestFit() {
	float m[16];
	shieldparts::Fit none{};
	shieldshape::FitMatrix(none, m);
	bool ident = true;
	for (int i = 0; i < 16; ++i) ident = ident && std::fabs(m[i] - (i % 5 == 0 ? 1.0f : 0.0f)) < 1e-6f;
	EXPECT(ident, "no fit: as the mesh is");
	shieldparts::Fit f{1, {0.5f, -0.25f, 0.1f}, {0.0f, 90.0f, 0.0f}, {2.0f, 2.0f, 2.0f}};
	shieldshape::FitMatrix(f, m);
	const shieldshape::V x = shieldshape::Point(m, {1, 0, 0}), o = shieldshape::Point(m, {0, 0, 0});
	EXPECT(std::fabs(x.x - 0.5f) < 1e-4f && std::fabs(x.y + 0.25f) < 1e-4f && std::fabs(x.z - (0.1f - 2.0f)) < 1e-4f, "x scaled 2, turned 90 degrees about y to -z, moved (%.3f %.3f %.3f)",
	       x.x, x.y, x.z);
	EXPECT(std::fabs(o.x - 0.5f) + std::fabs(o.y + 0.25f) + std::fabs(o.z - 0.1f) < 1e-5f, "the grip moved by the translation");
	EXPECT(std::fabs(shieldshape::Scale(m) - 2.0f) < 1e-4f, "it scales by 2");
	// the drawn bone: a mirrored one (the left side's) scales the radius as much, positive; a shrunk one (put away) draws nothing
	const shieldparts::Shape& sh = *shieldparts::Of(Code("tow "));
	const float mirror[16] = {-2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 2, 3, 1};
	shieldshape::Capsule c[shieldparts::kParts];
	const int n = shieldshape::InModel(sh, -1, mirror, c, shieldparts::kParts);
	EXPECT(n == sh.n && std::fabs(c[0].r - 2.0f * sh.p[0].r) < 1e-5f && std::fabs(c[0].a.x - (1.0f - 2.0f * sh.p[0].a[0])) < 1e-4f, "a mirrored bone: %d capsules, r %.3f", n, n ? c[0].r : 0.0f);
	const float shrunk[16] = {0.001f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0, 0.001f, 0, 1, 2, 3, 1};
	EXPECT(shieldshape::InModel(sh, 3, shrunk, c, shieldparts::kParts) == 0, "a bone shrunk to a point (put away): no face");
	// the hands' frame and his model space: the skeleton's ToModel undone, at any yaw
	for (const float yaw : {0.0f, 0.7f, -2.4f}) {
		const float cy = std::cos(yaw), sy = std::sin(yaw);
		const shieldshape::V eye{0.3f, 6.1f, -0.2f}, off{0.02f, -0.05f, 0.1f}, h{0.25f, -0.4f, -0.6f};
		const shieldshape::V model = shieldshape::HandsToModel(h, eye, 3.65f, cy, sy, off);
		const shieldshape::V back = shieldshape::ModelToHands(model, eye, 3.65f, cy, sy, off);
		// skeletons.cpp ToModel: m = (-v.x, v.y, -v.z) turned by the yaw
		const shieldshape::V v = shieldshape::Add(h, off);
		const float mx = -v.x, mz = -v.z;
		const shieldshape::V want = shieldshape::Add(eye, shieldshape::Mul({mx * cy + mz * sy, v.y, -mx * sy + mz * cy}, 3.65f));
		EXPECT(shieldshape::Len(shieldshape::Sub(back, h)) < 1e-5f && shieldshape::Len(shieldshape::Sub(model, want)) < 1e-4f, "the hands' frame round the model and back at yaw %.1f", yaw);
	}
}

// The rules: through past 60 cm, back once clear; the knock's strength, felt and heard.
void TestRules() {
	shieldcollide::Pass p;
	EXPECT(p.Step(true, 0.30f, -0.01f) && p.Step(true, 0.59f, -0.01f), "resting on it, the hand up to 59 cm off: the shield is met");
	EXPECT(!p.Step(true, 0.61f, -0.01f) && p.Through() && p.Count() == 1, "61 cm: through");
	EXPECT(!p.Step(false, 0.62f, 0.10f), "clear of it but the hand still far off: still through");
	EXPECT(!p.Step(false, 0.10f, 0.01f), "the hand back, the weapon 1 cm off the shield: still through");
	EXPECT(p.Step(false, 0.10f, 0.05f) && !p.Through(), "the hand back and the weapon clear: the shield is met again");
	const shieldcollide::Knock slow = shieldcollide::KnockOf(0.2f), soft = shieldcollide::KnockOf(0.6f), hard = shieldcollide::KnockOf(3.0f);
	EXPECT(!slow.felt && !slow.heard, "0.2 m/s: resting, neither felt nor heard");
	EXPECT(soft.felt && !soft.heard && soft.amplitude >= 0.6f, "0.6 m/s: felt (%.2f), silent", soft.amplitude);
	EXPECT(hard.felt && hard.heard && hard.gameVolume == clashloud::Of(3.0f).gameVolume && hard.amplitude > soft.amplitude, "3 m/s: felt harder (%.2f) and heard at %u/255", hard.amplitude,
	       hard.gameVolume);
	shieldcollide::Touches t;
	EXPECT(t.Step(true, 1000) && !t.Step(true, 1011) && !t.Step(false, 1022) && !t.Step(true, 1033) && t.Step(false, 1200) == false && t.Step(true, 1211), "one knock a touch, none within 120 ms");
}

// ---- the contact, through the weight model -------------------------------------------------------------------------------------------------------------

int g_pulses[2] = {};
float g_pulseAmp = 0.0f;
void Pulse(int side, float amplitude, int) {
	if (side >= 0 && side < 2) ++g_pulses[side];
	g_pulseAmp = std::max(g_pulseAmp, amplitude);
}
struct ClashHeard { float speed; uint32_t volume; };
std::vector<ClashHeard> g_clashes;
void ClashSound(float speed, uint32_t volume, const char*) { g_clashes.push_back({speed, volume}); }
std::vector<std::string> g_log;
void LogLine(const char* t) { g_log.emplace_back(t); }
monsterbodies::Snapshot g_snap;   // (no monsters)
bool Bodies(monsterbodies::Snapshot* out) {
	*out = g_snap;
	out->tickMs = GetTickCount64();
	return true;
}

struct Rig {
	skel::Input in, drawn;
	weight::Out out;
	uint32_t sample = 0;
	Rig() {
		in.mode = 2;
		in.handsValid = 3;
		in.headRoom[1] = 1.7f;
		for (auto& r : in.rot) r[3] = 1.0f;
		const float r[3] = {0.25f, -0.4f, -0.3f}, l[3] = {-0.25f, -0.4f, -0.3f};
		for (int k = 0; k < 3; ++k) { in.hand[0][k] = r[k]; in.hand[1][k] = l[k]; }
		skel::Input::Weight& w = in.weight;
		w.on = true;
		w.kind = (int)heft::hold::Kind::Sword;
		in.weaponType = 100 + w.kind;
		w.slot = 0;   // the sword in the right hand, the shield on the left arm
		w.feel = 1.0f;
		w.strength = 1.0f;
		w.grabM = 0.12f;
		w.letGoM = 0.25f;
		w.haveRawGrip = true;
		w.tipAxis = 2;
		w.monsterBodies = true;
		w.worldOk = true;
		w.worldCamR[0] = 1.0f;
		w.worldCamF[2] = -1.0f;
		w.worldUpm = 1.0f;
		w.shieldCode = Code("tow ");
	}
	void Step(int ms = 11) {
		Sleep(ms);
		in.sample = ++sample;
		weight::SetDrawnShield(shield.empty() ? nullptr : shield.data(), (int)shield.size() / 7, 1, in.weight.shieldCode);   // (the skeleton tells it every pose)
		skel::Input c = in;
		weight::Apply(&c, &out);
		drawn = c;
	}
	void Steps(int n) { while (n-- > 0) Step(); }
	std::vector<float> shield;   // the shield's face in the hands' frame, 7 floats a capsule
};

// A tower shield's face (the game's mesh, through vr/shield_shape.h) across the sword's sweep, its front toward the right hand (+x) at x = frontX, its middle at
// (y, z): upright (its 1.05 m up, its 51 cm along the blade), or laid along the blade (lengthwise: its 1.05 m along the blade, the whole blade on it).
std::vector<float> TowerAt(float frontX, float y, float z, bool lengthwise = false) {
	// the bone: the mesh's z (the face's way out) toward +x in the hands' frame; upright, its y (the shield's height) up and its x along the blade - lengthwise
	// the other way round; his model a metre 3.65 units (the hero's), the eye at the origin, no yaw - the hands' frame is (-x, y, -z) of the model's
	const float upright[16] = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1};
	const float laid[16] = {0, 1, 0, 0, 0, 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 1};
	const float* bone = lengthwise ? laid : upright;
	shieldshape::Capsule c[shieldparts::kParts];
	const int n = shieldshape::InHands(*shieldparts::Of(Code("tow ")), -1, bone, {0, 0, 0}, 3.65f, 1.0f, 0.0f, {0, 0, 0}, c, shieldparts::kParts);
	const shieldshape::Size z0 = shieldshape::SizeOf(c, n);
	float front = -1e9f;
	for (int i = 0; i < n; ++i) front = std::max({front, c[i].a.x + c[i].r, c[i].b.x + c[i].r});
	const shieldshape::V shift{frontX - front, y - z0.centre.y, z - z0.centre.z};
	std::vector<float> out;
	for (int i = 0; i < n; ++i) {
		const shieldshape::V a = shieldshape::Add(c[i].a, shift), b = shieldshape::Add(c[i].b, shift);
		out.insert(out.end(), {a.x, a.y, a.z, b.x, b.y, b.z, c[i].r});
	}
	return out;
}

struct Drive {
	float held = 0.0f, most = 0.0f, slid = 0.0f;
	int pulses[2] = {};
	std::vector<ClashHeard> heard;
	bool through = false;
};
// The right hand driven across (toward -x) `metres` at `speed` m/s into the shield, held there, then (slide) along the face (-z) 33 cm.
Drive Run(bool on, float speed, float metres, bool slide, bool lengthwise = false) {
	Drive d;
	Rig r;
	r.in.weight.shieldCollide = on;
	r.Steps(5);   // placed: the sword in the right hand
	float butt[3], tip[3];
	EXPECT(weight::ShaftEnds(butt, tip), "the sword's ends");
	r.shield = TowerAt(r.in.hand[0][0] - 0.12f, r.in.hand[0][1], 0.5f * (butt[2] + tip[2]), lengthwise);   // 12 cm to the left of the hand, across the blade's middle
	r.Steps(3);
	g_pulses[0] = g_pulses[1] = 0;
	g_pulseAmp = 0.0f;
	g_clashes.clear();
	g_log.clear();
	const float x0 = r.in.hand[0][0];
	const int steps = (int)(metres / speed / 0.011f) + 1;
	for (int i = 0; i < steps; ++i) {
		r.in.hand[0][0] = x0 - std::min(metres, speed * 0.011f * (float)(i + 1));
		r.Step();
		d.most = std::max(d.most, r.drawn.hand[0][0] - r.in.hand[0][0]);
	}
	r.Steps(10);
	d.held = r.drawn.hand[0][0] - r.in.hand[0][0];   // how far the drawn right hand stayed behind its controller
	d.pulses[0] = g_pulses[0];
	d.pulses[1] = g_pulses[1];
	d.heard = g_clashes;
	if (slide) {
		const float z0 = r.drawn.hand[0][2];
		for (int i = 0; i < 30; ++i) {
			r.in.hand[0][2] -= 0.011f;
			r.Step();
		}
		r.Steps(10);
		d.slid = z0 - r.drawn.hand[0][2];
	}
	for (const std::string& l : g_log) d.through = d.through || l.find("pressed on through the shield") != std::string::npos;
	r.in.weight.on = false;
	r.Step();
	return d;
}

void TestContact() {
	weight::SetClashSound(&ClashSound);
	const Drive off = Run(false, 2.0f, 0.4f, false);
	const Drive hit = Run(true, 2.0f, 0.4f, true);
	const Drive soft = Run(true, 0.5f, 0.4f, false);
	const Drive rest = Run(true, 0.12f, 0.3f, false);
	const Drive push = Run(true, 1.0f, 0.95f, false, true);   // (laid along the blade: the whole blade on it, it cannot slide off its edge first)
	weight::SetClashSound(nullptr);
	std::printf("  a sword driven 40 cm into a tower shield at 2 m/s: off - its hand %.0f cm behind the controller (it passes); on - %.0f cm behind (it stops on the face), "
	            "pulses %d / %d, %zu block sound(s)%s; slid along the face it went on %.0f cm of 33\n", off.held * 100.0f, hit.held * 100.0f, hit.pulses[0], hit.pulses[1], hit.heard.size(),
	            hit.heard.empty() ? "" : (" at " + std::to_string((int)hit.heard[0].volume) + "/255").c_str(), hit.slid * 100.0f);
	std::printf("  at 0.5 m/s: pulses %d / %d, %zu sound(s); laid on at 0.12 m/s: pulses %d / %d, %zu sound(s), %.0f cm behind; pressed 95 cm at 1 m/s on one laid along the blade: %.0f cm behind at most, "
	            "%.0f cm at the end, %s\n", soft.pulses[0], soft.pulses[1], soft.heard.size(), rest.pulses[0], rest.pulses[1], rest.heard.size(), rest.held * 100.0f, push.most * 100.0f,
	            push.held * 100.0f, push.through ? "through (told)" : "NOT THROUGH");
	EXPECT(off.held < 0.05f && off.pulses[0] == 0 && off.heard.empty(), "off: the sword passes through the shield, nothing felt or heard (%.2f m)", off.held);
	EXPECT(hit.held > 0.2f, "on: the sword stops on the shield's face - its hand held back (%.2f m)", hit.held);
	EXPECT(hit.pulses[0] > 0 && hit.pulses[1] > 0, "on: the knock felt in the sword's hand and the shield's (%d / %d)", hit.pulses[0], hit.pulses[1]);
	EXPECT(!hit.heard.empty() && hit.heard[0].speed >= shieldcollide::kSoundMps && hit.heard[0].volume == clashloud::Of(hit.heard[0].speed).gameVolume,
	       "on: a hard knock plays the game's block sound at its loudness (%zu)", hit.heard.size());
	EXPECT(hit.slid > 0.15f, "slid along the face it glances on (%.2f m)", hit.slid);
	EXPECT(soft.held > 0.2f && soft.pulses[0] > 0 && soft.heard.empty(), "0.5 m/s: stopped, felt, silent (%.2f m, %d, %zu)", soft.held, soft.pulses[0], soft.heard.size());
	EXPECT(rest.held > 0.1f && rest.pulses[0] == 0 && rest.heard.empty(), "laid on gently: stopped, unfelt, silent (%.2f m, %d, %zu)", rest.held, rest.pulses[0], rest.heard.size());
	EXPECT(push.most <= shieldcollide::kGiveM + 0.05f && push.most > 0.5f && push.held < 0.1f && push.through,
	       "pressed on 95 cm: the hand gives way up to about 60 cm (%.2f), then the sword goes through (%.2f m at the end)", push.most, push.held);
	EXPECT(!weight::Reached(shieldcollide::kId, 2000) && weight::Blocked(shieldcollide::kId, 2000) <= 1, "the shield is never a body reached (no strike)");
	bool told = false;
	for (const std::string& l : g_log) told = told || l.find("shield_collide") != std::string::npos;
	EXPECT(told, "the log says what the shield did");
}

}  // namespace

int main() {
	std::printf("shield_collide_test: the weapon on the hero's own shield\n");
	weight::SetBodiesSource(&Bodies);
	weight::SetHaptic(&Pulse);
	weight::SetLog(&LogLine);
	TestShapes();
	TestFit();
	TestRules();
	TestContact();
	std::printf(g_fails ? "shield_collide_test: %d FAILED\n" : "shield_collide_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
