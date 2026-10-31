// Our monsters' bodies never hold the hero ([weight] monster_bodies, strike_by_force, strike_force_stuck; vr/monster_bodies.h GiveWay, vr/strike_measure.h,
// vr/strike_force.h): zbiromir, 2026-10-30 - "a leap sometimes lands inside an enemy's model, only way out is to exit the game"; the hero could not walk, could
// still fight (Griswold, Goatmen, a zombie - never a Fallen). The weapon held in the monster he landed in made a strike that never ended: every second a new
// one (the tracker's restart), and with strike_force_stuck 1 a repeat each time the game's attack ended - hit by touch pressed the game's attack again and
// again, the hero stood in his attacks. Here the same on synthetic poses, the weight model's hold stepping a sword at 90 Hz, the presses decided as
// vr/melee_touch.cpp decides them, the game's hero a small model: an attack runs kAttackS after a press, a walk asked for is walked only when no attack runs.
//
//   hero_free_test          every check below
//   hero_free_test table    each scene's steps (contact, presses, the hero's mode)
//
// Checks: GiveWay - a hero beside a monster keeps its body, one landed inside it (a leap) has it give way, out again past the margin (no flicker at the edge),
// a body appearing on the hero gives way at once, walking through a crowd only the ones he stands in give way, his own body and a shield never do; the strike
// tracker - a contact resting past a second goes on as the same strike (its serial, no new arrival); the force judge - a resumed strike is no new blow, a repeat
// waits while the hero walks or his mode is not read; end to end - the leap into a big monster: before the fix the hero is held (presses all along, no walk),
// after it he walks at once; a weapon resting on a monster beside him: one blow, then the hero walks away.

#include "contact.h"
#include "hold.h"
#include "../../vr/monster_bodies.h"
#include "../../vr/strike_force.h"
#include "../../vr/strike_measure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hc = heft::contact;
namespace mb = monsterbodies;
namespace sf = strikeforce;
namespace sm = strikemeasure;

namespace {

constexpr double kDt = 1.0 / 90.0;
int g_fails = 0;
bool g_table = false;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

// The touch's capsule of a monster standing at (x, z) on the floor (render metres, the hero's feet at y 0): as melee_bodies.h BodyFor makes it - radius
// its footprint plus the touch's margin, the axis from a radius up to its height less a radius.
mb::Body Touch(uint32_t id, float x, float z, float footprint, float height = 1.8f) {
	mb::Body b;
	b.id = id;
	b.radius = footprint + meleetouch::kTouchSlackM;
	b.bottom[0] = b.top[0] = x;
	b.bottom[2] = b.top[2] = z;
	b.bottom[1] = b.radius;
	b.top[1] = std::max(b.radius, height - b.radius);
	return b;
}
bool Has(const mb::Body* b, int n, uint32_t id) {
	for (int i = 0; i < n; ++i)
		if (b[i].id == id) return true;
	return false;
}

void TestGiveWay() {
	const float hero[3] = { 0, 0, 0 };
	mb::GiveWay gw;
	// beside: the game keeps the two apart (centres a body and the hero's half apart)
	mb::Body b[8];
	b[0] = Touch(1, 0.0f, -0.95f, 0.52f);   // Griswold-sized (3 cells: 0.52 m), its surface 0.38 m in front
	int n = gw.Apply(b, 1, hero);
	EXPECT(n == 1 && gw.Count() == 0, "a big monster beside the hero keeps its body: %d kept", n);
	// the leap landed in it
	b[0] = Touch(1, 0.0f, -0.30f, 0.52f);
	int entered = 0;
	n = gw.Apply(b, 1, hero, &entered);
	EXPECT(n == 0 && gw.Count() == 1 && entered == 1, "landed inside: its body gives way (%d kept, %d entered)", n, entered);
	// a step out, still within the margin: stays given way; past it: back
	const float core = mb::CoreRadius(b[0].radius);
	b[0] = Touch(1, 0.0f, -(core + mb::kHeroInsideM + 0.10f), 0.52f);
	n = gw.Apply(b, 1, hero, &entered);
	EXPECT(n == 0 && entered == 0, "just out: still giving way (no flicker at the edge)");
	b[0] = Touch(1, 0.0f, -(core + mb::kHeroInsideM + mb::kHeroOutM + 0.02f), 0.52f);
	n = gw.Apply(b, 1, hero);
	EXPECT(n == 1 && gw.Count() == 0, "out past the margin: its body is back");
	// the same distance never entered: a body
	b[0] = Touch(1, 0.0f, -(core + mb::kHeroInsideM + 0.10f), 0.52f);
	n = gw.Apply(b, 1, hero);
	EXPECT(n == 1, "that far, never inside: a body");
	// a body appearing on the hero (spawned, a monster walking into him): gives way at once; the others stay, in order
	b[0] = Touch(2, 0.6f, -0.9f, 0.35f);
	b[1] = Touch(3, 0.05f, 0.02f, 0.35f);
	b[2] = Touch(4, -0.7f, -0.8f, 0.17f);
	n = gw.Apply(b, 3, hero, &entered);
	EXPECT(n == 2 && b[0].id == 2 && b[1].id == 4 && entered == 1, "one spawned in the hero gives way, the rest kept in order (%d kept)", n);
	// the hero's own body and what a monster holds never give way
	b[0] = Touch(mb::kOwnBodyId, 0.0f, 0.0f, 0.2f);
	b[0].held = mb::kHeldOwnBody;
	b[1] = Touch(5, 0.0f, -0.1f, 0.2f);
	b[1].held = 2;
	n = gw.Apply(b, 2, hero);
	EXPECT(n == 2, "his own body and a shield stay (%d kept)", n);
	// no hero known: nothing gives way
	b[0] = Touch(6, 0.0f, 0.0f, 0.35f);
	EXPECT(gw.Apply(b, 1, nullptr) == 1, "no hero: no change");

	// walking through a crowd: a line of zombies 0.9 m apart, the hero walking along it 0.1 m to the side - only the one he is in gives way, the others never
	mb::GiveWay walk;
	int wrongKept = 0, wrongGone = 0, gone = 0;
	for (int step = 0; step <= 400; ++step) {
		const float hx = -1.0f + step * 0.01f;
		const float h[3] = { hx, 0, 0 };
		mb::Body c[5];
		for (int i = 0; i < 5; ++i) c[i] = Touch(10 + i, -0.9f + i * 0.9f, 0.1f, 0.35f);
		mb::Body in[5];
		std::memcpy(in, c, sizeof c);
		const int k = walk.Apply(c, 5, h);
		gone += 5 - k;
		for (int i = 0; i < 5; ++i) {
			const float d = std::sqrt((hx - in[i].bottom[0]) * (hx - in[i].bottom[0]) + 0.01f);
			const float r = mb::CoreRadius(in[i].radius);
			const bool kept = Has(c, k, in[i].id);
			if (d < r + mb::kHeroInsideM && kept) ++wrongKept;
			if (d > r + mb::kHeroInsideM + mb::kHeroOutM && !kept) ++wrongGone;
		}
	}
	std::printf("  walking through a crowd of zombies: %d body-steps gave way, %d kept that he stood in, %d gone that he was clear of\n", gone, wrongKept, wrongGone);
	EXPECT(gone > 0 && wrongKept == 0 && wrongGone == 0, "only the ones he stands in give way");
}

void TestTracker() {
	// a weapon resting on a body for 3.5 s: told each second, the same strike going on (no new serial, no new arrival)
	sm::Tracker tr;
	sm::Step s;
	s.id = 4;
	s.dt = kDt;
	s.hands = 1;
	s.impulse = 0.3;
	s.approach = 2.0;
	uint32_t first = 0;
	int told = 0, newSerials = 0;
	bool resumedSeen = false, arrivalAgain = false;
	for (int i = 0; i < 315; ++i) {
		s.t = i * kDt;
		if (tr.Feed(s)) ++told;
		if (!first) first = tr.Current().serial;
		if (tr.Current().serial != first) ++newSerials;
		if (tr.Current().resumed) {
			resumedSeen = true;
			if (tr.Current().arrival != 0) arrivalAgain = true;
		}
	}
	EXPECT(told == 3 && newSerials == 0 && resumedSeen && !arrivalAgain, "resting 3.5 s: told %d times, %d new serials, resumed %d, an arrival again %d", told, newSerials,
	       (int)resumedSeen, (int)arrivalAgain);
	// a real second blow (off the body, back on) is a new strike
	s.id = 0;
	s.impulse = 0;
	s.t += 0.2;
	tr.Feed(s);
	s.id = 4;
	s.impulse = 2.0;
	s.t += kDt;
	tr.Feed(s);
	EXPECT(tr.On() && tr.Current().serial != first && !tr.Current().resumed && tr.Current().arrival == 2.0, "off and on again: a new strike");
}

void TestJudge() {
	sf::Settings st;
	st.on = true;
	st.stuck = 1;
	sf::Strike k;
	k.hands = 1;
	k.window = 5.0;
	k.recent = 5.0;
	sf::Verdict v = sf::Judge(st, sf::Family::Edged, 1.0f, k);
	EXPECT(sf::NewBlowDue(v, k), "a fresh strong strike is a blow");
	k.resumed = true;
	EXPECT(!sf::NewBlowDue(v, k), "the same contact begun again is no new blow");
	EXPECT(sf::HeroBusy(-1) && sf::HeroBusy(2) && sf::HeroBusy(3) && sf::HeroBusy(6) && sf::HeroBusy(7) && !sf::HeroBusy(1) && !sf::HeroBusy(4),
	       "busy: not known, walking, running, attacking; free: standing, hit");
	EXPECT(!sf::RepeatDue(st, sf::Family::Edged, 1.0f, k, sf::HeroBusy(2), 1.0), "no repeat while he walks");
	EXPECT(!sf::RepeatDue(st, sf::Family::Edged, 1.0f, k, sf::HeroBusy(-1), 1.0), "no repeat when his mode is not read");
	EXPECT(sf::RepeatDue(st, sf::Family::Edged, 1.0f, k, sf::HeroBusy(1), 1.0), "standing, pushed in: the repeat as before");
}

// ---- end to end -------------------------------------------------------------------------------------------------------------------------------------

hh::HandIn HandAt(const Pose& p) {
	hh::HandIn h;
	h.tracked = true;
	h.pose = p;
	h.grip = 1.0f;
	h.fistLine = { 0, 0, -1 };
	return h;
}

struct Scene {
	const char* name = "";
	std::vector<mb::Body> monsters;   // the touch's capsules (render metres; the room is the render world here)
	V3 hand { 0.15, 1.1, -0.2 };      // the controller, still but for a small push to and fro (a player standing, the hands never quite still)
	V3 push { 0.0, 0.0, -0.03 };      // ... its amplitude, at kPushHz
	double secs = 6.0;
	double walkFrom = 1.0;            // the player asks to walk from this time on (the stick held)
	bool modeKnown = true;            // the hero's unit mode is read (vrcam's HeroAnimMode; -1 when not)
	int stuck = 1;                    // [weight] strike_force_stuck
	bool fixed = true;                // GiveWay on the bodies and the judge's new rules; false: as before the fix
};
struct Outcome {
	int presses = 0;
	int contactSteps = 0;
	double walkedS = 0;     // how long the hero walked after it was asked for
	double heldS = 0;       // ... and was held in his attacks instead
};

constexpr double kAttackS = 0.55;   // a press: the game's attack runs this long (a Barbarian's swing)
constexpr double kPushHz = 1.0;

Outcome Play(const Scene& sc) {
	Outcome o;
	hh::Settings s;
	s.bodies = true;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt({ sc.hand, {} }), HandAt({ { -0.3, 1.0, 0 }, {} }) };
	in[1].tracked = false;
	in[1].grip = 0;
	h.Place(0, hh::Kind::Sword, false, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	sm::Strikes tracker;
	sf::Settings force;
	force.on = true;
	force.stuck = sc.stuck;
	struct Pressed { uint32_t serial = 0, id = 0; double at = 0; };
	std::vector<Pressed> pressed;
	mb::GiveWay giveWay;
	const float hero[3] = { 0, 0, 0 };
	double lastPress = -1e9;
	const int steps = (int)std::ceil(sc.secs / kDt);
	for (int i = 1; i <= steps; ++i) {
		const double t = i * kDt;
		const double w = std::sin(t * 2 * 3.14159265358979 * kPushHz);
		in[0] = HandAt({ Add(sc.hand, Mul(sc.push, w)), {} });
		// the bodies as the touch publishes them (melee_touch.cpp PublishBodies), into the weight model's room
		mb::Body b[mb::kMax];
		int n = 0;
		for (const mb::Body& m : sc.monsters) b[n++] = m;
		if (sc.fixed) n = giveWay.Apply(b, n, hero);
		hc::Capsule caps[mb::kMax];
		for (int k = 0; k < n; ++k) {
			caps[k].a = { b[k].bottom[0], b[k].bottom[1], b[k].bottom[2] };
			caps[k].b = { b[k].top[0], b[k].top[1], b[k].top[2] };
			caps[k].radius = mb::CoreRadius(b[k].radius);
			caps[k].id = b[k].id;
		}
		h.SetBodies(caps, n);
		h.Step(in, kDt, s, out);
		// the strikes measured (weight.cpp MeasureStrikes)
		const hc::Report& c = h.LastContact();
		sm::Step ms[sm::Strikes::kMax];
		int nm = 0;
		for (int k = 0; c.bodies > 0 && k < c.nMet && nm < sm::Strikes::kMax; ++k) {
			const hc::Report::Met& e = c.met[k];
			if (!e.onBody) continue;
			sm::Step& m = ms[nm++];
			m.t = t;
			m.dt = kDt;
			m.id = e.id;
			m.hands = out[0].holds ? 1 : 0;
			m.handMask = out[0].holds ? 1u : 0u;
			m.impulse = e.impulse;
			m.friction = e.friction;
			m.slid = e.slid;
			m.push = e.push;
			m.approach = e.approach;
		}
		if (nm) ++o.contactSteps;
		sm::Strike ended[sm::Strikes::kMax];
		tracker.Feed(ms, nm, t, kDt, ended);
		// the game's hero: in his attack for kAttackS after a press; else walking when asked, else standing
		const bool attacking = t - lastPress < kAttackS;
		const bool walkAsked = t >= sc.walkFrom;
		const int mode = attacking ? 7 : walkAsked ? 2 : 1;
		const int modeSeen = sc.modeKnown ? mode : -1;
		// hit by touch by force (melee_touch.cpp): a new strike that counts is pressed; one pressed already repeats when RepeatDue says
		sm::Strike live[sm::Strikes::kMax];
		const int nl = tracker.Live(live, sm::Strikes::kMax);
		bool press = false;
		for (int k = 0; k < nl && !press; ++k) {
			const sm::Strike& m = live[k];
			const sf::Strike ks { m.id, m.serial, m.hands, m.window, m.cut, m.recent, m.startS, m.along, sc.fixed && m.resumed };
			const sf::Verdict v = sf::Judge(force, sf::Family::Edged, (float)h.SteelKg(), ks);
			// (before the fix a strike begun again after a second had a serial of its own: told apart here by when it began)
			const uint32_t key = sc.fixed ? m.serial : m.serial * 131u + (uint32_t)std::lround(m.startS * 1000.0);
			Pressed* was = nullptr;
			for (Pressed& p : pressed)
				if (p.serial == key && p.id == m.id) was = &p;
			const bool attackingSeen = sc.fixed ? sf::HeroBusy(modeSeen) : sf::HeroAttacking(modeSeen);
			const bool due = !was ? (sc.fixed ? sf::NewBlowDue(v, ks) : v.Counts()) : sf::RepeatDue(force, sf::Family::Edged, (float)h.SteelKg(), ks, attackingSeen, t - was->at);
			if (!due) continue;
			if (was) was->at = t;
			else pressed.push_back({ key, m.id, t });
			press = true;
		}
		if (press) {
			++o.presses;
			lastPress = t;
		}
		const bool nowAttacking = t - lastPress < kAttackS;
		if (walkAsked) (nowAttacking ? o.heldS : o.walkedS) += kDt;
		if (g_table && (i % 9 == 0 || press))
			std::printf("  %-34s t %.2f  contact %d (%.2f N s)  live %d  mode %d%s\n", sc.name, t, c.bodies, c.stepImpulse, nl, modeSeen, press ? "  PRESS" : "");
	}
	return o;
}

void TestLeapIntoBigMonster() {
	// Leap Attack onto Griswold: the hero lands with his centre 0.45 m from its axis (inside its 0.52 m body); his sword hand is just outside it, the blade in it
	Scene sc;
	sc.name = "leap into Griswold";
	sc.monsters = { Touch(77, -0.32f, -0.32f, 0.52f, 2.2f) };
	sc.hand = { 0.18, 1.1, -0.15 };
	sc.push = { -0.03, 0.0, 0.0 };
	sc.fixed = false;
	const Outcome before = Play(sc);
	sc.modeKnown = false;
	const Outcome beforeUnread = Play(sc);
	sc.modeKnown = true;
	sc.stuck = 0;
	const Outcome beforeStuck0 = Play(sc);
	sc.stuck = 1;
	sc.fixed = true;
	const Outcome after = Play(sc);
	sc.modeKnown = false;
	const Outcome afterUnread = Play(sc);
	std::printf("  leap into Griswold (asked to walk from 1 s on, 5 s): before the fix %d presses, walked %.1f s, held %.1f s (mode not read: %d presses, walked %.1f s; stuck 0: %d presses, "
	            "walked %.1f s); after it %d presses, walked %.1f s (mode not read: %d, %.1f s), %d steps in contact\n",
	            before.presses, before.walkedS, before.heldS, beforeUnread.presses, beforeUnread.walkedS, beforeStuck0.presses, beforeStuck0.walkedS, after.presses, after.walkedS,
	            afterUnread.presses, afterUnread.walkedS, after.contactSteps);
	EXPECT(before.contactSteps > 100, "the scene reproduces: the blade is in the body he landed in (%d steps)", before.contactSteps);
	EXPECT(before.presses >= 4 && before.heldS > 0.5 * (before.heldS + before.walkedS), "before the fix the hero is held: %d presses, held %.1f s of %.1f", before.presses, before.heldS,
	       before.heldS + before.walkedS);
	EXPECT(beforeUnread.walkedS < 1.0, "... and with his mode not read hardly walks at all: %.1f s", beforeUnread.walkedS);
	EXPECT(beforeStuck0.presses >= 4, "... strike_force_stuck 0 too (the restart each second): %d presses", beforeStuck0.presses);
	EXPECT(after.presses == 0 && after.contactSteps == 0, "after it the body he stands in gives way: %d presses, %d steps in contact", after.presses, after.contactSteps);
	EXPECT(after.heldS == 0 && afterUnread.heldS == 0, "... and he walks the whole time asked: held %.2f / %.2f s", after.heldS, afterUnread.heldS);
}

void TestRestingBeside() {
	// a zombie beside the hero (not inside), the sword pressed into it and held there: one blow, then the hero walks away when asked
	Scene sc;
	sc.name = "resting on a zombie beside him";
	sc.monsters = { Touch(55, 0.15f, -1.35f, 0.35f) };   // its front 1 m out: the sword's tip (1.1 m out at rest) in it
	sc.hand = { 0.15, 1.1, -0.2 };
	sc.push = { 0.0, 0.0, -0.03 };
	sc.walkFrom = 1.5;
	sc.fixed = false;
	const Outcome before = Play(sc);
	sc.fixed = true;
	const Outcome after = Play(sc);
	sc.stuck = 0;
	const Outcome after0 = Play(sc);
	std::printf("  resting on a zombie beside him (asked to walk from 1.5 s on): before the fix %d presses, walked %.1f s, held %.1f s; after it %d presses, walked %.1f s, held %.1f s "
	            "(stuck 0: %d presses); %d steps in contact\n",
	            before.presses, before.walkedS, before.heldS, after.presses, after.walkedS, after.heldS, after0.presses, after.contactSteps);
	EXPECT(after.contactSteps > 100, "the weapon rests on its body: %d steps", after.contactSteps);
	EXPECT(after.presses >= 1, "the first blow still counts: %d", after.presses);
	EXPECT(after.heldS <= kAttackS + kDt, "once asked he walks - held at most by the game's attack already on: %.2f s", after.heldS);
	EXPECT(after0.presses == 1, "stuck 0: one hit a strike, however long it rests: %d", after0.presses);
	EXPECT(before.presses > after.presses, "before the fix it pressed more: %d vs %d", before.presses, after.presses);
}

}  // namespace

int main(int argc, char** argv) {
	g_table = argc > 1 && std::strcmp(argv[1], "table") == 0;
	std::printf("hero_free_test: our monsters' bodies never hold the hero\n");
	TestGiveWay();
	TestTracker();
	TestJudge();
	TestLeapIntoBigMonster();
	TestRestingBeside();
	if (g_fails) {
		std::printf("hero_free_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("hero_free_test: all passed\n");
	return 0;
}
