// [weight] knock_down: does a push of the held weapon knock a humanoid off its feet, stagger it, or nothing (vr/knock_trigger.h)?
//
// Checks: the masses on the author's two numbers (a fallen of 1.15 m 30 kg, a man of 1.75 m 63 kg). A blow needs BOTH a fast swing and a hard push (Serg,
// 2026-11-01: they fell from a light push): a fast hard blow knocks a fallen down; as hard but slow staggers; as fast but light (a man) staggers; a light touch
// nothing. Network: a body jumping onto a resting blade (a huge push, no swing) - nothing, not once, not pressed on. Running: the hero's weight adds to a real
// swing's push (a man down), never to a weapon only held out; running away adds nothing; only at a strike's first step. A sweep at the legs needs half the force,
// not less swing; a blow low but straight down is no sweep. A shove (pushed on and on) staggers, never knocks down; too few pushes, or spread wider than the
// window, nothing. After a verdict the body is left alone a second; another body is apart; bad input; the stagger off (0). The hero's run: eased, a jump
// (a teleport, a co-op correction) is no run, a gap starts it from rest.

#include "../../vr/knock_trigger.h"

#include <cmath>
#include <cstdio>

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

namespace kt = knocktrigger;

kt::Body Of(float kg) {
	kt::Body b;
	b.floorY = 2.0f;
	b.height = 1.75f;
	b.massKg = kg;
	return b;
}
kt::Body Man() { return Of(63.0f); }
kt::Body Fallen() { return Of(30.0f); }
const float kChest[3] = {0.0f, 3.3f, 0.0f}, kShin[3] = {0.0f, 2.25f, 0.0f};
const float kStill[3] = {0.0f, 0.0f, 0.0f};
const float kIn[3] = {0.0f, 0.0f, -1.0f};   // the way of the blows (only its way counts)
constexpr float kSword = 0.72f + 3.5f;     // a short sword's steel and the arm behind it

}  // namespace

int main() {
	EXPECT(std::fabs(kt::MassOf(1.15f, 1.0f) - 30.0f) < 0.3f && std::fabs(kt::MassOf(1.75f, 1.0f) - 63.0f) < 0.5f, "fallen %.1f, a man %.1f",
	       kt::MassOf(1.15f, 1.0f), kt::MassOf(1.75f, 1.0f));
	EXPECT(kt::MassOf(2.1f, 1.0f) > 80.0f && kt::MassOf(2.1f, 1.0f) < 95.0f, "a big one of 2.1 m: %.1f (bigger is heavier, faster than its size)", kt::MassOf(2.1f, 1.0f));
	EXPECT(std::fabs(kt::MassOf(1.75f, 2.0f) - 126.0f) < 1.0f, "the weight slider: %.1f", kt::MassOf(1.75f, 2.0f));
	const kt::Settings s;   // the defaults: a swing of 5 m/s, 0.8 m/s to the body, a stagger at 45 % of both
	EXPECT(s.minSwingMs == 5.0f && std::fabs(s.needMs - 0.8f) < 1e-6f && std::fabs(s.staggerShare - 0.45f) < 1e-6f, "the defaults %.1f %.2f %.2f", s.minSwingMs,
	       s.needMs, s.staggerShare);
	{   // a fast hard blow: a sword at 6 m/s on a fallen = 25.3 N s, 0.84 m/s - down; the impulse is the swing's, its way the push's
		kt::Tracker t;
		kt::Verdict v = t.Note(7, kIn, kChest, 6.0f, kSword, 1.0, Fallen(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_BLOW && v.strike && std::fabs(v.gaveMs - kSword * 6.0f / 30.0f) < 1e-3f && std::fabs(v.impulse[2] + kSword * 6.0f) < 1e-3f &&
		           v.swingMs == 6.0f && v.needSwingMs == 5.0f, "a fast hard blow: knock %d kind %d gave %.2f", v.knock, v.kind, v.gaveMs);
		v = t.Note(7, kIn, kChest, 6.0f, kSword, 1.5, Fallen(), kStill, s);
		EXPECT(!v.knock, "knocked down again within a second");
		v = t.Note(7, kIn, kChest, 6.0f, kSword, 2.2, Fallen(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_BLOW, "not knocked down again after the quiet second");
	}
	{   // as hard but slow: a heavy weapon (12 kg behind it) at 3 m/s = 36 N s, 1.2 m/s to the fallen - a slow push staggers (3 >= 2.25), never down
		kt::Tracker t;
		kt::Verdict v = t.Note(8, kIn, kChest, 3.0f, 12.0f, 1.0, Fallen(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_STAGGER && v.gaveMs > s.needMs && !v.shove, "a hard slow push: knock %d kind %d gave %.2f", v.knock, v.kind, v.gaveMs);
	}
	{   // as fast but light: the sword at 6 m/s on a man = 0.40 m/s (under 0.8, over 0.36) - a stagger; at 4 m/s 0.27 - nothing
		kt::Tracker t;
		kt::Verdict v = t.Note(9, kIn, kChest, 6.0f, kSword, 1.0, Man(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_STAGGER, "a fast light blow on a man: knock %d kind %d gave %.2f", v.knock, v.kind, v.gaveMs);
		v = t.Note(10, kIn, kChest, 4.0f, kSword, 1.0, Man(), kStill, s);
		EXPECT(!v.knock, "a lighter blow on a man staggered: gave %.2f", v.gaveMs);
	}
	{   // a light touch: 1 m/s (a poke) - nothing, even on a fallen
		kt::Tracker t;
		const kt::Verdict v = t.Note(11, kIn, kChest, 1.0f, kSword, 1.0, Fallen(), kStill, s);
		EXPECT(!v.knock && v.strike, "a touch did something: kind %d", v.kind);
	}
	{   // network: a body that jumps onto a resting blade - the contact's push is huge (200 N s), the weapon's own speed none; ninety steps of it - nothing
		kt::Tracker t;
		const float jump[3] = {0.0f, 0.0f, -200.0f};
		bool any = false;
		for (int i = 0; i < 90; ++i) any = t.Note(12, jump, kChest, i % 3 ? 0.0f : 0.2f, kSword, 1.0 + 0.011 * i + 0.2 * (i / 15), Fallen(), kStill, s).knock || any;
		EXPECT(!any, "a body jumping onto the blade knocked or staggered");
		const kt::Verdict v = t.Note(13, kIn, kChest, 40.0f, kSword, 5.0, Fallen(), kStill, s);   // a weapon that jumped (40 m/s in a step): no swing
		EXPECT(!v.knock && v.swingMs == 0.0f, "a jumping weapon knocked: kind %d swing %.1f", v.kind, v.swingMs);
	}
	{   // running: the hero at 3 m/s the way of the blow - the sword at 5.5 m/s on a man: 23.2 + 12 x 3 = 59.2 N s, 0.94 m/s - down; the weapon only held out
		// (0.5 m/s) - nothing; running away adds nothing (a stagger by the swing alone); the strike's second step adds nothing
		kt::Tracker t;
		const float ahead[3] = {0.0f, 0.0f, -3.0f}, away[3] = {0.0f, 0.0f, 3.0f};
		kt::Verdict v = t.Note(1, kIn, kChest, 5.5f, kSword, 1.0, Man(), ahead, s);
		EXPECT(v.knock && v.kind == D2RF_BLOW && std::fabs(v.runMs - 3.0f) < 1e-3f && std::fabs(v.gaveMs - (kSword * 5.5f + 36.0f) / 63.0f) < 1e-3f,
		       "a running blow: knock %d kind %d run %.2f gave %.2f", v.knock, v.kind, v.runMs, v.gaveMs);
		v = t.Note(2, kIn, kChest, 0.5f, kSword, 1.0, Man(), ahead, s);
		EXPECT(!v.knock && v.gaveMs > s.needMs * s.staggerShare, "a weapon held out running: knock %d kind %d gave %.2f", v.knock, v.kind, v.gaveMs);
		v = t.Note(3, kIn, kChest, 5.5f, kSword, 1.0, Man(), away, s);
		EXPECT(v.runMs == 0.0f && v.kind == D2RF_STAGGER, "running away: run %.2f kind %d", v.runMs, v.kind);
		v = t.Note(4, kIn, kChest, 2.0f, kSword, 1.0, Man(), kStill, s);
		v = t.Note(4, kIn, kChest, 6.0f, kSword, 1.01, Man(), ahead, s);
		EXPECT(!v.strike && !v.knock && v.runMs == 0.0f, "the strike's second step ran: strike %d knock %d", v.strike, v.knock);
	}
	{   // a sweep: low and across needs half the force (0.4 m/s: the sword at 6 m/s on a man gives 0.40) but the whole swing; low but down is a blow
		kt::Tracker t;
		const float across[3] = {1.0f, -0.25f, 0.0f}, down[3] = {0.0f, -1.0f, 0.1f};
		kt::Verdict v = t.Note(20, across, kShin, 6.1f, kSword, 1.0, Man(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_SWEEP && std::fabs(v.needMs - 0.4f) < 1e-4f, "a sweep: knock %d kind %d need %.2f gave %.2f", v.knock, v.kind, v.needMs, v.gaveMs);
		v = t.Note(21, across, kShin, 4.0f, 12.0f, 1.0, Man(), kStill, s);
		EXPECT(!(v.knock && v.kind == D2RF_SWEEP), "a slow sweep knocked down: kind %d", v.kind);
		v = t.Note(22, down, kShin, 6.1f, kSword, 1.0, Man(), kStill, s);
		EXPECT(v.kind != D2RF_SWEEP, "a blow down at the shin swept: kind %d", v.kind);
		v = t.Note(23, across, kChest, 6.1f, kSword, 1.0, Man(), kStill, s);
		EXPECT(v.kind != D2RF_SWEEP, "a chest blow taken for a sweep: kind %d", v.kind);
	}
	{   // a shove: pushes of 1 N s pressed on (the hands at 1 m/s) - 23 of them in 0.23 s add up to 0.37 m/s on a man (0.36 the stagger's): a stagger, never a
		// fall; pressed on hard and fast, still never a fall; 4 big ones are too few; pushes a tenth of a second apart never add up in the window
		kt::Tracker t;
		const float one[3] = {0.0f, 0.0f, -1.0f};
		kt::Verdict v;
		int at = -1;
		for (int i = 0; i < 60 && !v.knock; ++i) {
			v = t.Note(30, one, kChest, 1.0f, kSword, 1.0 + 0.01 * i, Man(), kStill, s);
			if (v.knock) at = i;
		}
		EXPECT(v.knock && v.kind == D2RF_STAGGER && v.shove && at == 22, "a shove: knock %d kind %d shove %d after %d pushes", v.knock, v.kind, v.shove, at + 1);
		kt::Tracker t2;
		const float big[3] = {0.0f, 0.0f, -10.0f};
		bool down = false;
		for (int i = 0; i < 60; ++i) {
			const kt::Verdict w = t2.Note(31, big, kChest, 3.0f, kSword, 1.0 + 0.01 * i, Fallen(), kStill, s);
			down = down || (w.knock && w.kind != D2RF_STAGGER);
		}
		EXPECT(!down, "pressed on and on knocked down");
		kt::Tracker t3;
		for (int i = 0; i < 4; ++i) v = t3.Note(32, big, kChest, 1.0f, kSword, 1.0 + 0.05 * i, Man(), kStill, s);
		EXPECT(!v.knock, "four pushes made a shove");
		kt::Tracker t4;
		bool any = false;
		for (int i = 0; i < 40; ++i) any = t4.Note(33, one, kChest, 1.0f, kSword, 1.0 + 0.1 * i, Man(), kStill, s).knock || any;
		EXPECT(!any, "pushes a tenth of a second apart made a shove");
	}
	{   // another body is apart; bad input
		kt::Tracker t;
		kt::Verdict a = t.Note(40, kIn, kChest, 4.0f, kSword, 1.0, Man(), kStill, s), b = t.Note(41, kIn, kChest, 4.0f, kSword, 1.0, Man(), kStill, s);
		EXPECT(!a.knock && !b.knock && b.strike, "two bodies added up");
		const float nan[3] = {NAN, 0.0f, 0.0f};
		EXPECT(!t.Note(42, nan, kChest, 9.0f, kSword, 1.0, Fallen(), kStill, s).knock && !t.Note(0, kIn, kChest, 9.0f, kSword, 1.0, Fallen(), kStill, s).knock,
		       "bad input knocked down");
		EXPECT(!t.Note(43, kIn, kChest, NAN, kSword, 1.0, Fallen(), kStill, s).knock && !t.Note(44, kIn, kChest, 9.0f, NAN, 1.0, Fallen(), kStill, s).knock,
		       "a swing or a heft not known knocked down");
		EXPECT(!t.Note(45, kIn, kChest, 9.0f, kSword, 1.0, Of(0.0f), kStill, s).knock, "a massless body");
		for (uint32_t id = 100; id < 140; ++id) t.Note(id, kIn, kChest, 4.0f, kSword, 2.0 + id * 0.001, Man(), kStill, s);   // more bodies than it keeps: the oldest go
		EXPECT(t.Note(139, kIn, kChest, 4.0f, kSword, 2.5, Man(), kStill, s).gaveMs > 0.0f, "a full tracker");
	}
	{   // the stagger off: the middling blow and the shove do nothing; the fast hard blow still knocks down
		kt::Settings st;
		st.staggerShare = 0.0f;
		kt::Tracker t;
		EXPECT(!t.Note(50, kIn, kChest, 6.0f, kSword, 1.0, Man(), kStill, st).knock, "staggered with the stagger off");
		const float one[3] = {0.0f, 0.0f, -1.0f};
		bool any = false;
		for (int i = 0; i < 60; ++i) any = t.Note(51, one, kChest, 1.0f, kSword, 1.0 + 0.01 * i, Man(), kStill, st).knock || any;
		EXPECT(!any, "a shove with the stagger off");
		EXPECT(t.Note(52, kIn, kChest, 6.0f, kSword, 1.0, Fallen(), kStill, st).knock, "the stagger off stopped a knock-down");
	}
	{   // the hero's run: 4 m/s steady (10 ms ticks) reads ~4; a 0.6 m jump in one tick (a co-op correction) is no run; a gap or no place: from rest
		kt::HeroRun r;
		float p[3] = {0.0f, 0.0f, 0.0f};
		const float* v = r.Feed(p, 0.0f);
		for (int i = 0; i < 50; ++i) {
			p[0] += 0.04f;
			v = r.Feed(p, 0.01f);
		}
		EXPECT(std::fabs(v[0] - 4.0f) < 0.05f && v[1] == 0.0f && std::fabs(v[2]) < 1e-4f, "a steady run read %.2f", v[0]);
		p[2] += 0.6f;
		v = r.Feed(p, 0.01f);
		EXPECT(std::fabs(v[0]) + std::fabs(v[2]) < 1e-4f, "a jump read as a run: %.2f %.2f", v[0], v[2]);
		for (int i = 0; i < 3; ++i) {
			p[0] += 0.04f;
			v = r.Feed(p, 0.01f);
		}
		EXPECT(v[0] > 0.5f && v[0] < 4.0f, "after the jump it eases back in: %.2f", v[0]);
		v = r.Feed(p, 0.8f);
		EXPECT(v[0] == 0.0f, "a gap kept the run: %.2f", v[0]);
		v = r.Feed(nullptr, 0.01f);
		EXPECT(v[0] == 0.0f && v[2] == 0.0f, "no place kept the run");
		const float bad[3] = {NAN, 0.0f, 0.0f};
		v = r.Feed(bad, 0.01f);
		EXPECT(v[0] == 0.0f, "a place not finite kept the run");
	}
	std::printf(g_fails ? "knock_trigger_test: %d FAILED\n" : "knock_trigger_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
