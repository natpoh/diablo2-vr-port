// [weight] knock_down: does a push of the held weapon knock a humanoid off its feet (vr/knock_trigger.h)?
//
// Checks: the masses on the author's two numbers (a fallen of 1.15 m 30 kg, a zombie of 1.8 m 50 kg); a strike that gives the body the speed knocks it down,
// a weaker one does not; the same strike running knocks it down (the hero's weight behind it, along the blow only - running away adds nothing), and only at a
// strike's first step; a blow low at the legs and across is a sweep and needs half; a blow low but straight down is no sweep; small pushes pressed on add up
// to a shove, too few of them do not, spread wider than the window do not; a body knocked down is left alone a second; the strongest numbers a verdict
// carries; another body is apart; bad input. A stagger: a strike under the need but over its share staggers (no fall), a weaker one nothing, a shove's
// small pushes never stagger, 0 switches it off.

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

kt::Body Man() {
	kt::Body b;
	b.floorY = 2.0f;
	b.height = 1.75f;
	b.massKg = 50.0f;
	return b;
}
const float kChest[3] = {0.0f, 3.3f, 0.0f}, kShin[3] = {0.0f, 2.25f, 0.0f};
const float kStill[3] = {0.0f, 0.0f, 0.0f};

}  // namespace

int main() {
	EXPECT(std::fabs(kt::MassOf(1.15f, 1.0f) - 30.0f) < 0.3f && std::fabs(kt::MassOf(1.75f, 1.0f) - 63.0f) < 0.5f, "fallen %.1f, a man %.1f",
	       kt::MassOf(1.15f, 1.0f), kt::MassOf(1.75f, 1.0f));
	EXPECT(kt::MassOf(2.1f, 1.0f) > 80.0f && kt::MassOf(2.1f, 1.0f) < 95.0f, "a big one of 2.1 m: %.1f (bigger is heavier, faster than its size)", kt::MassOf(2.1f, 1.0f));
	EXPECT(std::fabs(kt::MassOf(1.75f, 2.0f) - 126.0f) < 1.0f, "the weight slider: %.1f", kt::MassOf(1.75f, 2.0f));
	kt::Settings s;   // 0.7 m/s
	s.staggerShare = 0.0f;   // (the knock-downs alone first; the stagger below)
	{   // a strike: 40 N s on 50 kg = 0.8 m/s knocks him down; 30 N s = 0.6 does not
		kt::Tracker t;
		const float hard[3] = {0.0f, 0.0f, -40.0f}, soft[3] = {0.0f, 0.0f, -30.0f};
		kt::Verdict v = t.Note(7, soft, kChest, 1.0, Man(), kStill, s);
		EXPECT(!v.knock && v.strike && std::fabs(v.gaveMs - 0.6f) < 1e-3f && v.kind == D2RF_BLOW, "a soft blow: knock %d gave %.2f", v.knock, v.gaveMs);
		v = t.Note(7, hard, kChest, 2.0, Man(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_BLOW && std::fabs(v.impulse[2] + 40.0f) < 1e-3f, "a hard blow: knock %d gave %.2f", v.knock, v.gaveMs);
		v = t.Note(7, hard, kChest, 2.5, Man(), kStill, s);
		EXPECT(!v.knock, "knocked down again within a second");
		v = t.Note(7, hard, kChest, 3.2, Man(), kStill, s);
		EXPECT(v.knock, "not knocked down again after the quiet second");
	}
	{   // running: the soft blow with the hero at 3 m/s the way of it - 30 + 12 x 3 = 66 N s; running the other way adds nothing; a second step of the strike adds nothing
		kt::Tracker t;
		const float soft[3] = {0.0f, 0.0f, -30.0f}, ahead[3] = {0.0f, 0.0f, -3.0f}, away[3] = {0.0f, 0.0f, 3.0f};
		kt::Verdict v = t.Note(1, soft, kChest, 1.0, Man(), away, s);
		EXPECT(!v.knock && v.runMs == 0.0f, "running away knocked down: %.2f", v.gaveMs);
		v = t.Note(1, soft, kChest, 1.01, Man(), ahead, s);
		EXPECT(!v.strike && !v.knock && v.runMs == 0.0f, "the strike's second step ran: strike %d gave %.2f", v.strike, v.gaveMs);
		v = t.Note(2, soft, kChest, 1.0, Man(), ahead, s);
		EXPECT(v.knock && std::fabs(v.runMs - 3.0f) < 1e-3f && std::fabs(v.gaveMs - 66.0f / 50.0f) < 1e-3f && std::fabs(v.impulse[2] + 66.0f) < 1e-2f,
		       "a running blow: knock %d run %.2f gave %.2f", v.knock, v.runMs, v.gaveMs);
	}
	{   // a sweep: low and across needs half (0.35 m/s: 20 N s); low but down is a blow
		kt::Tracker t;
		const float across[3] = {20.0f, -5.0f, 0.0f}, down[3] = {0.0f, -20.0f, 2.0f};
		kt::Verdict v = t.Note(3, across, kShin, 1.0, Man(), kStill, s);
		EXPECT(v.knock && v.kind == D2RF_SWEEP && std::fabs(v.needMs - 0.35f) < 1e-4f, "a sweep: knock %d kind %d need %.2f", v.knock, v.kind, v.needMs);
		v = t.Note(4, down, kShin, 1.0, Man(), kStill, s);
		EXPECT(!v.knock && v.kind == D2RF_BLOW, "a blow down at the shin swept: knock %d kind %d", v.knock, v.kind);
		v = t.Note(5, across, kChest, 1.0, Man(), kStill, s);
		EXPECT(!v.knock && v.kind == D2RF_BLOW, "a chest blow taken for a sweep");
	}
	{   // a shove: 40 pushes of 1 N s in 0.4 s add to 0.8 m/s; 4 big pushes are too few steps; pushes 0.1 s apart over 4 s never add up in the window
		kt::Tracker t;
		const float one[3] = {0.0f, 0.0f, -1.0f};
		kt::Verdict v;
		int at = -1;
		for (int i = 0; i < 40 && !v.knock; ++i) {
			v = t.Note(6, one, kChest, 1.0 + 0.01 * i, Man(), kStill, s);
			if (v.knock) at = i;
		}
		EXPECT(v.knock && v.kind == D2RF_SHOVE && at == 34, "a shove: knock %d kind %d after %d pushes", v.knock, v.kind, at + 1);
		kt::Tracker t2;
		const float big[3] = {0.0f, 0.0f, -10.0f};
		for (int i = 0; i < 4; ++i) v = t2.Note(8, big, kChest, 1.0 + 0.05 * i, Man(), kStill, s);
		EXPECT(!v.knock, "four pushes made a shove");
		kt::Tracker t3;
		bool any = false;
		for (int i = 0; i < 40; ++i) any = t3.Note(9, one, kChest, 1.0 + 0.1 * i, Man(), kStill, s).knock || any;
		EXPECT(!any, "pushes a tenth of a second apart made a shove");
	}
	{   // another body is apart; bad input
		kt::Tracker t;
		const float half[3] = {0.0f, 0.0f, -20.0f};
		kt::Verdict a = t.Note(10, half, kChest, 1.0, Man(), kStill, s), b = t.Note(11, half, kChest, 1.0, Man(), kStill, s);
		EXPECT(!a.knock && !b.knock && b.strike, "two bodies added up");
		const float nan[3] = {NAN, 0.0f, 0.0f};
		EXPECT(!t.Note(12, nan, kChest, 1.0, Man(), kStill, s).knock && !t.Note(0, half, kChest, 1.0, Man(), kStill, s).knock, "bad input knocked down");
		kt::Body light = Man();
		light.massKg = 0.0f;
		EXPECT(!t.Note(13, half, kChest, 1.0, light, kStill, s).knock, "a massless body");
		for (uint32_t id = 100; id < 140; ++id) t.Note(id, half, kChest, 2.0 + id * 0.001, Man(), kStill, s);   // more bodies than it keeps: the oldest go
		EXPECT(t.Note(139, half, kChest, 2.2, Man(), kStill, s).gaveMs > 0.0f, "a full tracker");
	}
	{   // a stagger: 30 N s on 50 kg = 0.6 m/s (over 0.45 x 0.7 = 0.315, under 0.7) staggers; 10 N s (0.2) nothing; 40 N s still knocks down
		kt::Settings st;
		kt::Tracker t;
		const float mid[3] = {0.0f, 0.0f, -30.0f}, weak[3] = {0.0f, 0.0f, -10.0f}, hard[3] = {0.0f, 0.0f, -40.0f}, one[3] = {0.0f, 0.0f, -1.0f};
		kt::Verdict v = t.Note(40, mid, kChest, 1.0, Man(), kStill, st);
		EXPECT(v.knock && v.kind == D2RF_STAGGER, "a middling blow: knock %d kind %d", v.knock, v.kind);
		v = t.Note(41, weak, kChest, 1.0, Man(), kStill, st);
		EXPECT(!v.knock, "a weak blow staggered");
		v = t.Note(42, hard, kChest, 1.0, Man(), kStill, st);
		EXPECT(v.knock && v.kind == D2RF_BLOW, "a hard blow: kind %d", v.kind);
		bool staggered = false;
		for (int i = 0; i < 20; ++i) staggered = t.Note(43, one, kChest, 1.0 + 0.01 * i, Man(), kStill, st).kind == D2RF_STAGGER || staggered;
		EXPECT(!staggered, "a shove's small pushes staggered");
		st.staggerShare = 0.0f;
		EXPECT(!t.Note(44, mid, kChest, 1.0, Man(), kStill, st).knock, "staggered with the stagger off");
	}
	std::printf(g_fails ? "knock_trigger_test: %d FAILED\n" : "knock_trigger_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
