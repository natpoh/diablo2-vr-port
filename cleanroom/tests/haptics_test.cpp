// The haptics logic (vr/haptics_pattern.h) without a headset, the game or OpenXR: the amplitude
// scale, the mailbox the events post into from any thread, the limiter that decides what the frame
// loop plays, the named events' patterns and the bow's ratchet.
//
//   haptics_test

#include "haptics_pattern.h"

#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace haptics;

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

bool Near(float a, float b, float e = 0.001f) { return std::fabs(a - b) <= e; }

void TestScale() {
	std::printf("scale and clamps\n");
	EXPECT(Near(Scaled(0.5f, 1.0f), 0.5f), "plain");
	EXPECT(Near(Scaled(0.5f, 0.5f), 0.25f), "halved");
	EXPECT(Near(Scaled(0.72f, 2.0f), 1.0f), "doubled and clamped to 1");
	EXPECT(Scaled(0.5f, 0.0f) == 0.0f, "scale 0 = nothing");
	EXPECT(Scaled(0.03f, 1.0f) == 0.0f, "below the felt threshold = nothing");
	EXPECT(Scaled(NAN, 1.0f) == 0.0f, "NaN amplitude = nothing");
	EXPECT(Scaled(0.5f, NAN) == 0.0f, "NaN scale = nothing");
	EXPECT(Scaled(INFINITY, 1.0f) == 0.0f, "infinite amplitude = nothing");
	EXPECT(Scaled(-0.5f, 1.0f) == 0.0f, "negative amplitude = nothing");
	EXPECT(ClampDuration(-5) == kMinDurationMs, "negative duration");
	EXPECT(ClampDuration(0) == kMinDurationMs, "zero duration");
	EXPECT(ClampDuration(45) == 45, "usual duration");
	EXPECT(ClampDuration(100000) == kMaxDurationMs, "huge duration");
	EXPECT(FromSkel(0) == kRight && FromSkel(1) == kLeft, "skel's slot 0 is the right hand, haptics' 1");
	EXPECT(Other(kLeft) == kRight && Other(kRight) == kLeft, "the other hand");
}

void TestMailbox() {
	std::printf("mailbox\n");
	Mailbox m;
	Request r;
	EXPECT(!m.Take(1000, 150, &r), "empty");
	m.Post(0.3f, 40, 1000);
	EXPECT(m.Take(1010, 150, &r) && Near(r.amplitude, 0.3f) && r.durationMs == 40, "one pulse out as it went in (%.3f, %u)", r.amplitude, r.durationMs);
	EXPECT(!m.Take(1010, 150, &r), "taken once");
	// two pulses before a take are one: strongest, longest
	m.Post(0.3f, 70, 2000);
	m.Post(0.7f, 30, 2005);
	m.Post(0.1f, 20, 2008);
	EXPECT(m.Take(2010, 150, &r) && Near(r.amplitude, 0.7f) && r.durationMs == 70, "merged: strongest 0.7 and longest 70 (%.3f, %u)", r.amplitude, r.durationMs);
	// a pulse nobody took in time is a ghost
	m.Post(0.5f, 40, 3000);
	EXPECT(!m.Take(3200, 150, &r), "stale after 200 ms with a 150 ms limit");
	// a stale waiting pulse is replaced, not merged into
	m.Post(0.9f, 100, 4000);
	m.Post(0.2f, 20, 4400);
	EXPECT(m.Take(4410, 150, &r) && Near(r.amplitude, 0.2f) && r.durationMs == 20, "old waiting pulse replaced (%.3f, %u)", r.amplitude, r.durationMs);
	// junk posts
	m.Post(NAN, 40, 5000);
	m.Post(0.0f, 40, 5000);
	m.Post(0.01f, 40, 5000);
	EXPECT(!m.Take(5001, 150, &r), "NaN, zero and too weak are not kept");
	m.Post(5.0f, 40, 6000);
	EXPECT(m.Take(6001, 150, &r) && Near(r.amplitude, 1.0f), "amplitude over 1 held to 1");
	m.Post(0.5f, 1, 7000);
	EXPECT(m.Take(7001, 150, &r) && r.durationMs == kMinDurationMs, "duration held to the minimum");
	// the 32-bit millisecond clock wraps (49 days): a stamp just before the wrap is not stale just after it
	m.Post(0.5f, 40, 0xFFFFFFF0u);
	EXPECT(m.Take(0x00000010u, 150, &r), "across the clock's wrap");
	m.Post(0.5f, 40, 8000);
	m.Clear();
	EXPECT(!m.Take(8001, 150, &r), "cleared");
}

void TestMailboxThreads() {
	std::printf("mailbox from many threads\n");
	Mailbox m;
	std::vector<std::thread> t;
	for (int i = 0; i < 8; ++i)
		t.emplace_back([&m, i] {
			for (int k = 0; k < 20000; ++k) m.Post(0.1f + 0.01f * (float)i, 20 + (uint32_t)i, 9000);
		});
	for (auto& x : t) x.join();
	Request r;
	EXPECT(m.Take(9001, 150, &r), "something waits");
	EXPECT(Near(r.amplitude, 0.17f, 0.002f) && r.durationMs == 27, "the strongest and the longest of all eight survive (%.3f, %u)", r.amplitude, r.durationMs);
}

void TestLimiter() {
	std::printf("limiter\n");
	Limiter L;
	EXPECT(L.Allow(kRight, 0.5f, 40, 1000), "first pulse plays");
	EXPECT(!L.Allow(kRight, 0.5f, 40, 1010), "10 ms later: too close");
	EXPECT(L.Allow(kLeft, 0.5f, 40, 1010), "the other hand is its own");
	EXPECT(!L.Allow(kRight, 0.5f, 40, 1031 - 2), "inside the gap");
	// 1031: gap passed (31 ms) but the 40 ms pulse still plays: only a clearly stronger one cuts in
	EXPECT(!L.Allow(kRight, 0.55f, 40, 1031), "still playing: a barely stronger pulse waits");
	EXPECT(L.Allow(kRight, 0.7f, 40, 1032), "still playing: a clearly stronger one cuts in");
	// a weak tick right after a strong hit does not cut it short
	Limiter M;
	EXPECT(M.Allow(kRight, 0.72f, 45, 2000), "hit");
	EXPECT(!M.Allow(kRight, 0.25f, 25, 2035), "ratchet tick inside the hit");
	EXPECT(M.Allow(kRight, 0.25f, 25, 2050), "...but plays once the hit is over");
	// refused ones do not count as starts
	Limiter N;
	int played = 0;
	for (int i = 0; i < 1000; ++i) played += N.Allow(kRight, 0.5f, 20, 3000 + (uint64_t)i * 5) ? 1 : 0;   // a pulse every 5 ms for 5 s
	EXPECT(played <= (int)(5 * Limiter::kMaxInWindow) && played >= 4 * Limiter::kMaxInWindow, "a flood is held to ~%d a second: %d in 5 s", Limiter::kMaxInWindow, played);
	EXPECT(N.Allow(kRight, 0.5f, 20, 3000 + 1000 * 5 + 2000), "and plays again after a pause");
	// invalid
	Limiter V;
	EXPECT(!V.Allow(-1, 0.5f, 20, 100) && !V.Allow(2, 0.5f, 20, 100), "no such hand");
	EXPECT(!V.Allow(kLeft, 0.0f, 20, 100) && !V.Allow(kLeft, NAN, 20, 100), "no amplitude");
	V.Allow(kLeft, 0.5f, 20, 100);
	V.Reset();
	EXPECT(V.Allow(kLeft, 0.5f, 20, 101), "reset forgets the last start");
}

void TestPatterns() {
	std::printf("patterns\n");
	for (int e = 0; e < kEventCount; ++e) {
		const Pattern& p = PatternOf((Event)e);
		EXPECT(p.name && p.n >= 1 && p.n <= 2, "event %d has a name and 1-2 parts", e);
		Resolved r[2];
		const int n = Expand((Event)e, -1, r);
		EXPECT(n == p.n, "event %d: %d parts", e, n);
		for (int i = 0; i < n; ++i) {
			EXPECT(r[i].amplitude >= 0.12f && r[i].amplitude <= 0.72f, "%s: amplitude %.2f in 0.12 - 0.72", p.name, r[i].amplitude);
			EXPECT(r[i].durationMs >= 20 && r[i].durationMs <= 100, "%s: %u ms in 20 - 100", p.name, r[i].durationMs);
			EXPECT(r[i].hand == kLeft || r[i].hand == kRight, "%s: a real hand", p.name);
		}
	}
	Resolved r[2];
	EXPECT(Expand(Event::Hit, -1, r) == 1 && r[0].hand == kRight, "a hit: the right hand");
	EXPECT(Expand(Event::Hit, kLeft, r) == 1 && r[0].hand == kLeft, "a hit in a given hand");
	EXPECT(Expand(Event::ShieldHit, -1, r) == 1 && r[0].hand == kLeft, "a shield hit: the left hand");
	EXPECT(Expand(Event::Drink, 7, r) == 1 && r[0].hand == kRight, "a hand that does not exist = the event's own");
	EXPECT(Expand(Event::BowNock, kRight, r) == 2 && r[0].hand == kRight && r[1].hand == kLeft && r[0].amplitude > r[1].amplitude, "a nock: the string hand strong, the bow hand light");
	EXPECT(Expand(Event::BowNock, kLeft, r) == 2 && r[0].hand == kLeft && r[1].hand == kRight, "a left-handed archer");
	EXPECT(Expand(Event::BowRelease, kRight, r) == 2 && r[0].hand != r[1].hand, "a release is in both hands");
	EXPECT(Expand((Event)99, -1, r) == 1, "an unknown event is the first one, not a crash");
}

void TestHitHand() {
	std::printf("the hand a landed blow buzzes\n");
	Resolved r[2];
	const auto played = [&](int hand) { return Expand(Event::Hit, hand, r) == 1 ? r[0].hand : -1; };
	// a blow somebody asked for belongs to that hand's controller, whatever the weight model holds and wherever the weapon is
	for (int carry : {-1, 0, 1})
		for (bool placed : {false, true}) {
			EXPECT(played(HitHand(0, carry, placed)) == kRight, "claimed by the right hand, weapon in hand %d (placed %d): the RIGHT controller", carry, (int)placed);
			EXPECT(played(HitHand(1, carry, placed)) == kLeft, "claimed by the left hand, weapon in hand %d (placed %d): the LEFT controller", carry, (int)placed);
		}
	// a blow nobody of ours asked for (the game's own attack button, a thrown weapon in the air): the hand that holds the weapon, the right one when the weight model has none
	EXPECT(played(HitHand(-1, 0, true)) == kRight, "a sword in the right hand: the right controller");
	EXPECT(played(HitHand(-1, 1, true)) == kLeft, "a sword drawn into the left hand: the left controller");
	EXPECT(HitHand(-1, 1, false) == -1 && played(HitHand(-1, 1, false)) == kRight, "no weapon placed in the weight model: the event's own (right)");
	EXPECT(HitHand(-1, -1, true) == -1, "a carrying hand that is not known: the event's own");
	EXPECT(HitHand(-1, 5, true) == -1 && HitHand(7, -1, false) == -1, "nonsense is the event's own");
}

void TestRatchet() {
	std::printf("bow ratchet\n");
	Ratchet R;
	float s = 0, b = 0;
	EXPECT(!R.Step(0.0f, 1000, &s, &b), "no string");
	EXPECT(!R.Step(0.01f, 1000, &s, &b), "a hair of draw is not a step");
	EXPECT(R.Step(0.05f, 1000, &s, &b) && s > b && s >= 0.22f && b >= 0.10f, "the first step ticks (%.2f, %.2f)", s, b);
	EXPECT(!R.Step(0.10f, 1030, &s, &b), "30 ms later: too soon");
	EXPECT(R.Step(0.10f, 1070, &s, &b), "70 ms later a bigger step ticks");
	EXPECT(!R.Step(0.10f, 1200, &s, &b), "held still: nothing");
	EXPECT(!R.Step(0.105f, 1300, &s, &b), "a tremble under 0.018: nothing");
	const float low = s;
	EXPECT(R.Step(0.50f, 1400, &s, &b) && s > low, "the farther the draw, the stronger the string hand (%.2f > %.2f)", s, low);
	EXPECT(R.Step(0.40f, 1500, &s, &b), "easing back ticks as well");
	EXPECT(!R.Step(0.0f, 1600, &s, &b), "string let go");
	EXPECT(R.Step(0.05f, 1700, &s, &b), "drawn again: counts from zero");
	R.Rearm(0.6f, 2000);
	EXPECT(!R.Step(0.61f, 2100, &s, &b), "after a nock the count starts from where the string is");
	EXPECT(R.Step(0.95f, 2200, &s, &b) && s <= 0.5f && b <= 0.25f, "full draw stays inside the amplitudes (%.2f, %.2f)", s, b);
	EXPECT(R.Step(5.0f, 2400, &s, &b) && Near(s, 0.5f) && Near(b, 0.25f), "a draw over 1 is held to 1 (%.2f, %.2f)", s, b);
	// 32-bit clock wrap
	Ratchet W;
	EXPECT(W.Step(0.3f, 0xFFFFFFF0u, &s, &b), "tick before the wrap");
	EXPECT(!W.Step(0.5f, 0x00000010u, &s, &b), "32 ms across the wrap: too soon");
	EXPECT(W.Step(0.5f, 0x00000040u, &s, &b), "80 ms across the wrap: ticks");
}

}   // namespace

// The motors: which headset has the weak one, and what a gentle pulse becomes there.
void TestMotors() {
	std::printf("the motors\n");
	EXPECT(MotorOfHeadset("SteamVR/OpenXR : playstation_vr2") == Motor::Weak, "the PlayStation VR2 under SteamVR (the name the log of the player's machine has)");
	EXPECT(MotorOfHeadset("PLAYSTATION VR2") == Motor::Weak && MotorOfHeadset("PlayStation") == Motor::Weak && MotorOfHeadset("x playstation") == Motor::Weak, "any case, anywhere in the name");
	EXPECT(MotorOfHeadset("Oculus Quest2") == Motor::Plain && MotorOfHeadset("SteamVR/OpenXR : oculus_quest3") == Motor::Plain && MotorOfHeadset("SteamVR/OpenXR : valve_index") == Motor::Plain &&
	       MotorOfHeadset("SteamVR/OpenXR : steam_frame") == Motor::Plain, "a Quest, an Index, a Steam Frame: plain");
	EXPECT(MotorOfHeadset(nullptr) == Motor::Plain && MotorOfHeadset("") == Motor::Plain && MotorOfHeadset("playstatio") == Motor::Plain && MotorOfHeadset("play station") == Motor::Plain,
	       "no name, an empty one, a part of the word: plain");
	EXPECT(MotorChosen(0, Motor::Weak) == Motor::Weak && MotorChosen(0, Motor::Plain) == Motor::Plain, "0: by the headset");
	EXPECT(MotorChosen(1, Motor::Weak) == Motor::Plain && MotorChosen(2, Motor::Plain) == Motor::Weak && MotorChosen(7, Motor::Weak) == Motor::Weak, "1 plain, 2 weak, anything else by the headset");

	// plain: bit for bit
	bool same = true;
	for (int i = 4; i <= 100; ++i) {
		const float a = i * 0.01f;
		for (uint32_t ms : {10u, 28u, 40u, 45u, 70u}) {
			const Request r = ForMotor(Motor::Plain, a, ms);
			same = same && r.amplitude == a && r.durationMs == ms;
		}
	}
	EXPECT(same, "a plain motor: every amplitude and length as made");
	EXPECT(ForMotor(Motor::Plain, 0.03f, 40).amplitude == 0.0f && ForMotor(Motor::Plain, NAN, 40).amplitude == 0.0f && ForMotor(Motor::Weak, 0.03f, 40).amplitude == 0.0f &&
	       ForMotor(Motor::Weak, NAN, 40).amplitude == 0.0f && ForMotor(Motor::Weak, -1.0f, 40).amplitude == 0.0f, "nothing to play stays nothing, on either motor");
	EXPECT(ForMotor(Motor::Plain, 3.0f, 40).amplitude == 1.0f && ForMotor(Motor::Weak, 3.0f, 40).amplitude == 1.0f, "over 1 is 1");

	// weak: lifted, in the same order
	EXPECT(Near(ForMotor(Motor::Weak, kMinAmplitude, 40).amplitude, kWeakFloorAmp, 1e-5f) && Near(ForMotor(Motor::Weak, 1.0f, 40).amplitude, 1.0f, 1e-5f),
	       "the weakest felt pulse becomes the floor (%.2f), the strongest stays the strongest", ForMotor(Motor::Weak, kMinAmplitude, 40).amplitude);
	bool ordered = true, floored = true;
	float prev = 0.0f;
	for (int i = 4; i <= 100; ++i) {
		const float a = i * 0.01f, w = ForMotor(Motor::Weak, a, 28).amplitude;
		ordered = ordered && w >= prev && w >= a - 1e-6f;
		floored = floored && w >= kWeakFloorAmp - 1e-5f && w <= 1.0f && ForMotor(Motor::Weak, a, 28).durationMs == kWeakFloorMs;
		prev = w;
	}
	EXPECT(ordered, "a stronger pulse is never weaker after the lift, and none is lower than it was");
	EXPECT(floored, "every lifted pulse is in %.2f .. 1 and at least %u ms", kWeakFloorAmp, kWeakFloorMs);
	EXPECT(ForMotor(Motor::Weak, 0.5f, 90).durationMs == 90 && ForMotor(Motor::Weak, 0.5f, 45).durationMs == 45 && ForMotor(Motor::Weak, 0.5f, 28).durationMs == 45,
	       "a longer pulse keeps its length, a shorter one gets the floor's");
	// the hit that the player's PlayStation VR2 does feel (0.72 for 45 ms) is not below the lifted floor: the heartbeat now sits at that strength
	const Request quiet = ForMotor(Motor::Weak, 0.14f, 28);
	EXPECT(quiet.amplitude >= 0.55f && quiet.amplitude < 0.72f && quiet.durationMs == 45, "the quietest heartbeat of the defaults (%.2f for %u ms) is just under the hit's 0.72", quiet.amplitude, quiet.durationMs);
	// a mailbox says when a pulse was too old to play
	Mailbox m;
	Request r;
	bool stale = false;
	m.Post(0.5f, 40, 1000);
	EXPECT(!m.Take(1500, 150, &r, &stale) && stale, "an old pulse is reported as stale");
	stale = true;
	EXPECT(!m.Take(1500, 150, &r, &stale) && !stale, "nothing waiting is not stale");
	m.Post(0.5f, 40, 2000);
	EXPECT(m.Take(2010, 150, &r, &stale) && !stale, "a fresh one plays");
}

int main() {
	TestMotors();
	TestScale();
	TestMailbox();
	TestMailboxThreads();
	TestLimiter();
	TestPatterns();
	TestHitHand();
	TestRatchet();
	if (g_fails) {
		std::printf("\n%d FAILED\n", g_fails);
		return 1;
	}
	std::printf("\nall haptics checks passed\n");
	return 0;
}
