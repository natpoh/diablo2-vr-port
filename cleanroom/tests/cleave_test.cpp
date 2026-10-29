// [weight] cleave (cleanroom/heft/hold.h Settings::cleave, vr/cleave.h): a strong strike goes on through the body it meets into the next, slowed by what each took;
// a weak one stops on the first, a thrust sticks into the first, shields and weapons stop it. And: a body stopping the weapon never makes a hand let go of it
// (a player, 0.179: slashing left to right "the other hand even lets go of the weapon entirely").

#include "contact.h"
#include "hold.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace heft;
namespace hh = heft::hold;
namespace hc = heft::contact;

namespace {

constexpr double kDt = 1.0 / 90.0;
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

hc::Capsule Monster(double x, double z, double r, uint32_t id) {
	hc::Capsule c;
	c.a = { x, 0.3, z };
	c.b = { x, 1.5, z };
	c.radius = r;
	c.id = id;
	return c;
}
hh::HandIn HandAt(V3 p, float grip = 1.0f) {
	hh::HandIn h;
	h.tracked = true;
	h.pose = { p, {} };
	h.grip = grip;
	h.fistLine = { 0, 0, -1 };
	return h;
}
double TipZ(hh::Kind kind, bool twoHanded) {
	hh::Settings s;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt({ 0, 1.2, 0 }), HandAt({ -0.3, 1.0, 0 }, 0.0f) };
	h.Place(0, kind, twoHanded, hh::Line { {}, { 0, 0, -1 } }, in, s);
	V3 tip;
	h.Ends(nullptr, &tip);
	return tip.z;
}

// A swing left to right with both hands on the weapon (the second 12 cm on along the shaft), the two controllers carried on together at v across bodies.
struct Swing {
	hh::Kind kind = hh::Kind::Sword;
	bool twoHanded = true, slide = false;
	float mainGrip = 0.0f;   // the game's hand: placed in it, its grip not pressed (0) or pressed (1)
	double v = 4.0;
	std::vector<hc::Capsule> bodies;
	// out
	bool secondLost = false, mainLost = false;
	double lostAt = -1;
	int stepsOn = 0;
	double maxGap = 0;   // the most the second hand's controller got from where the hand is drawn
};
void Play(Swing& w) {
	hh::Settings s;
	s.bodies = true;
	s.slide = w.slide;
	hh::Hold h;
	const double x0 = -0.6, x1 = 1.0;
	auto ctl = [&](double t, int hand) { return V3 { std::min(x1, x0 + w.v * t), 1.2, hand ? -0.12 : 0.0 }; };
	hh::HandIn in[2] = { HandAt(ctl(0, 0), w.mainGrip), HandAt(ctl(0, 1), 0.0f) };
	h.Place(0, w.kind, w.twoHanded, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	h.Step(in, kDt, s, out);
	in[1].grip = 1.0f;   // the second hand closes on the shaft
	for (int i = 0; i < 20; ++i) h.Step(in, kDt, s, out);   // (both hands settled on it)
	EXPECT(out[0].holds && out[1].holds, "both hands on it before the swing");
	for (double t = 0; t <= (x1 - x0) / w.v + 0.4; t += kDt) {
		in[0] = HandAt(ctl(t, 0), w.mainGrip);
		in[1] = HandAt(ctl(t, 1));
		h.SetBodies(w.bodies.data(), (int)w.bodies.size());
		h.Step(in, kDt, s, out);
		if (h.LastContact().bodies > 0) ++w.stepsOn;
		if (out[1].holds) w.maxGap = std::max(w.maxGap, Len(Sub(out[1].pose.pos, in[1].pose.pos)));
		if (!out[1].holds && !w.secondLost) { w.secondLost = true; w.lostAt = t; }
		if (!out[0].holds) w.mainLost = true;
	}
}

void TestSecondHandStays() {
	const double zs = TipZ(hh::Kind::Sword, true);
	int lost = 0, runs = 0;
	for (int slide = 0; slide < 2; ++slide)
		for (float g : { 0.0f, 1.0f })
			for (double v : { 2.0, 4.0, 7.0 })
				for (double r : { 0.12, 0.25 }) {
					Swing w;
					w.slide = slide != 0;
					w.mainGrip = g;
					w.v = v;
					w.bodies = { Monster(0.0, zs * 0.6, r, 7) };
					Play(w);
					++runs;
					if (w.secondLost || w.mainLost) {
						++lost;
						std::printf("  a two-handed sword swung at %.0f m/s across a body (r %.0f cm), slide %d, the game's grip %s: %s let go at %.2f s (on the body %d steps, the hand %.0f cm from its controller)\n",
						            v, r * 100, slide, g > 0 ? "pressed" : "not pressed", w.mainLost ? "the MAIN hand" : "the second hand", w.lostAt, w.stepsOn, w.maxGap * 100);
					}
				}
	std::printf("  a two-handed sword swung across a body with both hands: %d of %d swings lost a hand\n", lost, runs);
	EXPECT(lost == 0, "a body stopping the weapon never makes a hand let go (%d of %d)", lost, runs);
}

// Without a body the second hand pulled off the shaft's axis still lets go (as ever): the fix is for a body holding the steel back only.
void TestSecondHandStillLetsGoInTheAir() {
	hh::Settings s;
	s.bodies = true;
	s.slide = true;
	hh::Hold h;
	hh::HandIn in[2] = { HandAt({ 0, 1.2, 0 }, 0.0f), HandAt({ 0, 1.2, -0.12 }, 0.0f) };
	h.Place(0, hh::Kind::Sword, true, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	h.Step(in, kDt, s, out);
	in[1].grip = 1.0f;
	for (int i = 0; i < 20; ++i) h.Step(in, kDt, s, out);
	bool held = out[1].holds, lost = false;
	for (int i = 0; i < 30; ++i) {   // the second controller pulled 40 cm off the shaft, sideways
		in[1].pose.pos.x = 0.4 * std::min(1.0, (i + 1) / 10.0);
		h.Step(in, kDt, s, out);
		lost = lost || !out[1].holds;
	}
	EXPECT(held && lost, "no body: the second hand pulled 40 cm off the shaft lets go as before (held %d, let go %d)", held, lost);
}

// The strike's threshold of a weapon (vr/strike_force.h's defaults: edged 1.1, blunt 1.5 N s a kg^0.5, at least 1 N s).
double Threshold(bool edged, double kg) { return std::max(1.0, (edged ? 1.1 : 1.5) * std::sqrt(std::max(0.05, kg))); }

struct Row {
	hh::Kind kind = hh::Kind::Sword;
	bool twoHanded = true, bothHands = true, thrust = false, cleave = true;
	double v = 6.0, x1 = 1.6;        // the hands' speed, and where they stop
	double over = 2.0, cost = 2.0;   // the cleave's numbers, times the weapon's threshold
	double minSpeed = 0;             // the blade's speed into a body at least, m/s (0 = any)
	std::vector<hc::Capsule> bodies;
	// out
	std::vector<uint32_t> met;   // the monsters' bodies met, in order (each once)
	int cleaves = 0;
	double kg = 0, need = 0;
	double lag = 0;              // the most the steel's centre was held back behind the hands (along the swing)
	std::vector<hh::Hold::Cleave> told;
};
void Play(Row& w) {
	hh::Settings s;
	s.bodies = true;
	hh::Hold h;
	const double x0 = -0.6, x1 = w.x1;
	auto ctl = [&](double t, int hand) { return V3 { std::min(x1, x0 + w.v * t), 1.2, hand ? -0.12 : 0.0 }; };
	hh::HandIn in[2] = { HandAt(ctl(0, 0), 0.0f), HandAt(ctl(0, 1), 0.0f) };
	h.Place(0, w.kind, w.twoHanded, hh::Line { {}, { 0, 0, -1 } }, in, s);
	hh::HandOut out[2];
	h.Step(in, kDt, s, out);
	if (w.bothHands) in[1].grip = 1.0f;
	else in[1].tracked = false;
	for (int i = 0; i < 20; ++i) h.Step(in, kDt, s, out);
	w.kg = h.SteelKg();
	const bool edged = w.kind == hh::Kind::Dagger || w.kind == hh::Kind::Sword || w.kind == hh::Kind::Axe || w.kind == hh::Kind::Polearm;
	const double T = Threshold(edged, w.kg);
	s.cleave = w.cleave;
	s.cleaveImpulse = w.over * T;
	s.cleaveCost = w.cost * T;
	s.cleaveMinSpeed = w.minSpeed;
	w.need = s.cleaveImpulse;
	h.SetStabbing(w.thrust);
	int seen = 0;
	for (double t = 0; t <= (x1 - x0) / w.v + 0.4; t += kDt) {
		in[0] = HandAt(ctl(t, 0), 0.0f);
		in[1] = HandAt(ctl(t, 1), 1.0f);
		if (!w.bothHands) in[1].tracked = false;
		h.SetBodies(w.bodies.data(), (int)w.bodies.size());
		h.Step(in, kDt, s, out);
		const hc::Report& c = h.LastContact();
		for (int i = 0; i < c.nMet; ++i)
			if (c.met[i].onBody && std::find(w.met.begin(), w.met.end(), c.met[i].id) == w.met.end()) w.met.push_back(c.met[i].id);
		if (h.Cleaves() != seen) { seen = h.Cleaves(); w.told.push_back(h.LastCleave()); }
		w.lag = std::max(w.lag, in[0].pose.pos.x - h.Com().x);
	}
	w.cleaves = h.Cleaves();
}
std::vector<hc::Capsule> Fallen(int n, double z, double r = 0.12, double gap = 0.38) {
	std::vector<hc::Capsule> b;
	for (int i = 0; i < n; ++i) b.push_back(Monster(-0.1 + i * gap, z, r, (uint32_t)(i + 1)));
	return b;
}
void Print(const char* what, const Row& w) {
	std::printf("  %s: met %zu (", what, w.met.size());
	for (size_t i = 0; i < w.met.size(); ++i) std::printf("%s%u", i ? " " : "", w.met[i]);
	std::printf("), cut through %d (needs %.2f N s, %.2f kg), held back at most %.0f cm", w.cleaves, w.need, w.kg, w.lag * 100);
	for (const hh::Hold::Cleave& k : w.told) std::printf("; #%u %.2f N s at %.1f m/s, kept %.0f %%, %.1f left", k.id, k.impulse, k.approach, k.keep * 100, k.left);
	std::printf("\n");
}

void TestCleave() {
	const double zs = TipZ(hh::Kind::Sword, true);
	Row three;   // a two-handed sword swung hard with both hands across three Fallen
	three.bodies = Fallen(3, zs * 0.6);
	Play(three);
	Print("a two-handed sword at 6 m/s across three Fallen", three);
	Row off = three;
	off.cleave = false;
	off.met.clear(); off.told.clear();
	Play(off);
	Print("... cleave off", off);
	Row weak = three;
	weak.v = 1.5;
	weak.x1 = 0.0;   // (a light blow: the hands stop just past the first one, they do not drive on 2 m)
	weak.met.clear(); weak.told.clear();
	Play(weak);
	Print("... a weak swing at 1.5 m/s", weak);
	Row thrust = three;
	thrust.thrust = true;
	thrust.met.clear(); thrust.told.clear();
	Play(thrust);
	Print("... while it thrusts", thrust);
	Row shield = three;   // the first Fallen's shield across the blade's path, before its body
	hc::Capsule sh = Monster(-0.1 - 0.17, zs * 0.6, 0.05, 1);
	sh.held = 2;
	shield.bodies.insert(shield.bodies.begin(), sh);
	shield.met.clear(); shield.told.clear();
	Play(shield);
	Print("... the first one's shield in the way", shield);
	Row crowd = three;
	crowd.bodies = Fallen(10, zs * 0.6, 0.12, 0.30);
	crowd.v = 6.0;
	crowd.x1 = 3.2;
	crowd.met.clear(); crowd.told.clear();
	Play(crowd);
	Print("... a crowd of ten", crowd);
	Row mace = three;   // a one-handed mace (blunt, light): never
	mace.kind = hh::Kind::Mace;
	mace.twoHanded = false;
	mace.bothHands = false;
	mace.bodies = Fallen(3, TipZ(hh::Kind::Mace, false) - 0.05);
	mace.met.clear(); mace.told.clear();
	Play(mace);
	Print("a one-handed mace at 6 m/s", mace);
	// the author 2026-10-25: "a blunt weapon must not go through - staves, mauls", "and swords only at a high speed"
	Row staff = three;   // a staff in both hands, swung as hard as the sword: it stops
	staff.kind = hh::Kind::Staff;
	staff.bodies = Fallen(3, TipZ(hh::Kind::Staff, true) * 0.6);
	staff.met.clear(); staff.told.clear();
	Play(staff);
	Print("a two-handed staff at 6 m/s", staff);
	Row maul = staff;    // a two-handed maul (a heavy mace)
	maul.kind = hh::Kind::Mace;
	maul.bodies = Fallen(3, TipZ(hh::Kind::Mace, true) * 0.6);
	maul.met.clear(); maul.told.clear();
	Play(maul);
	Print("a two-handed maul at 6 m/s", maul);
	Row fast = three;    // the sword with a speed floor of 4 m/s: 6 m/s cleaves ...
	fast.minSpeed = 4.0;
	fast.met.clear(); fast.told.clear();
	Play(fast);
	Print("the sword at 6 m/s, floor 4 m/s", fast);
	Row slow = fast;     // ... hands at 3 m/s: a body met slower than 4 m/s stops it (the blade's tip may still outrun the hands on the next)
	slow.v = 3.0;
	slow.met.clear(); slow.told.clear();
	Play(slow);
	Print("the sword at 3 m/s, floor 4 m/s", slow);

	EXPECT(three.cleaves >= 1 && three.met.size() >= 2, "on: the hard swing goes through the first into the next (%d cut, %zu met)", three.cleaves, three.met.size());
	EXPECT(off.cleaves == 0, "off: nothing cut through (%d)", off.cleaves);
	EXPECT(three.lag < 0.7 * off.lag, "on: the stroke is held back far less than off (%.0f vs %.0f cm)", three.lag * 100, off.lag * 100);
	EXPECT(weak.cleaves == 0, "a weak swing stops on the first (%d cut)", weak.cleaves);
	EXPECT(thrust.cleaves == 0, "a thrust sticks into the first, it never cuts through (%d)", thrust.cleaves);
	EXPECT(shield.cleaves == 0 || (shield.told.size() && shield.told[0].id != 1), "a shield stops it (%d cut, the first #%u)", shield.cleaves, shield.told.empty() ? 0 : shield.told[0].id);
	int run = 0;
	bool fourth = false;
	for (const hh::Hold::Cleave& k : crowd.told) { run = std::max(run, k.inStroke); fourth = fourth || k.id == 4; }
	EXPECT(run == 3 && !fourth && crowd.told.size() >= 3 && crowd.told[2].id == 3, "a crowd: the stroke goes through three and the fourth stops it (%d in a stroke, the fourth cut %d)", run, fourth);
	EXPECT(mace.cleaves == 0, "a light one-handed blunt weapon never cleaves (%d)", mace.cleaves);
	EXPECT(staff.cleaves == 0, "a staff in both hands never cleaves (%d)", staff.cleaves);
	EXPECT(maul.cleaves == 0, "a two-handed maul never cleaves (%d)", maul.cleaves);
	bool fastOver = !fast.told.empty();
	for (const hh::Hold::Cleave& k : fast.told) fastOver = fastOver && k.approach >= 4.0;
	EXPECT(fast.cleaves >= 1 && fastOver, "a sword faster than the floor cleaves, each body at 4 m/s or more (%d cut)", fast.cleaves);
	bool slowOver = true;
	for (const hh::Hold::Cleave& k : slow.told) slowOver = slowOver && k.approach >= 4.0;
	bool firstStops = true;
	for (const hh::Hold::Cleave& k : slow.told) firstStops = firstStops && k.id != 1;
	EXPECT(slowOver && firstStops && slow.cleaves < fast.cleaves, "a slower sword: the first body (met under 4 m/s) stops it, no body cut under the floor (%d cut vs %d)",
	       slow.cleaves, fast.cleaves);
	bool slower = true;
	for (const hh::Hold::Cleave& k : three.told) slower = slower && k.keep < 1.0 && k.keep > 0.0 && k.impulse >= k.need;
	EXPECT(slower, "each body passed slows the stroke, and took at least what a cleave needs");
	for (size_t i = 1; i < 3 && i < crowd.told.size(); ++i)
		EXPECT(crowd.told[i].inStroke == crowd.told[i - 1].inStroke + 1 && crowd.told[i].left < crowd.told[i - 1].left, "the stroke's count goes on, its force spent (%d after %d)",
		       crowd.told[i].inStroke, crowd.told[i - 1].inStroke);
}

}   // namespace

int main() {
	std::printf("cleave_test: a strong strike through the bodies, the hands stay on it\n");
	TestSecondHandStays();
	TestSecondHandStillLetsGoInTheAir();
	TestCleave();
	if (g_fails) {
		std::printf("cleave_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("cleave_test: all passed\n");
	return 0;
}
