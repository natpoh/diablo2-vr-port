// [weight] death_ragdoll: which deaths fall as our ragdoll, by what push, when the fallen body is kept as it lies, and the corpses kept (vr/death_ragdoll.h)?
//
// Checks: a boss, a prime evil, one that bursts, a town NPC, a row that did not read - the game's; a death with no read of it alive a moment before (it died out
// of sight, a corpse already, not a humanoid) - the game's; not hostile - the game's; else ours. The killing push: the hand's last strike a moment ago gives its
// way and heft x swing over the mass, held between the least and the most (a light poke still fells it, a huge blow never throws it across the room); an older
// strike, another body's, none - away from the hero across the ground at the chest; the hero under it - no way (the library falls it back). The strikes: a
// strike's first step carries its swing, a slower press after it does not replace it; the slots go round. The phases: falling waits, lying keeps, a get-up
// begun keeps, on its feet falls anew. The ease back into reach. The corpses: kept, replaced, full - the one seen longest ago makes room; forgotten a second
// after the scan stops listing them. The deaths decided once, forgotten when gone. The same skeleton: the instance, its bones, where it stood.

#include "../../vr/death_ragdoll.h"

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

namespace dr = deathragdoll;

constexpr uint32_t kEnabled = 1u << 25, kKillable = 1u << 15;   // an ordinary monster's row
float Len(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

}  // namespace

int main() {
	{   // whose death is ours
		const char* why = nullptr;
		EXPECT(dr::Decide(true, kEnabled | kKillable, true, 20, true, &why) == dr::Start::Ours && !why, "an ordinary monster read alive 20 ms ago is ours");
		EXPECT(dr::Decide(true, kEnabled | kKillable | dr::kRowBoss, true, 20, true, &why) == dr::Start::LeftToGame && why, "a boss is the game's");
		EXPECT(dr::Decide(true, dr::kRowPrimeEvil | dr::kRowBoss, true, 20, true, &why) == dr::Start::LeftToGame, "a prime evil is the game's");
		EXPECT(dr::Decide(true, kEnabled | dr::kRowDeathDmg, true, 20, true, &why) == dr::Start::LeftToGame, "one that bursts is the game's");
		EXPECT(dr::Decide(true, kEnabled | dr::kRowNpc, true, 20, true, &why) == dr::Start::LeftToGame, "a town NPC is the game's");
		EXPECT(dr::Decide(true, kEnabled | dr::kRowInTown, true, 20, true, &why) == dr::Start::LeftToGame, "one in town is the game's");
		EXPECT(dr::Decide(false, kEnabled, true, 20, true, &why) == dr::Start::LeftToGame, "a row that did not read is the game's");
		EXPECT(dr::Decide(true, kEnabled, false, 0, true, &why) == dr::Start::NotSeenAlive && why, "never read alive: the game's");
		EXPECT(dr::Decide(true, kEnabled, true, dr::kAliveFreshMs + 1, true, &why) == dr::Start::NotSeenAlive, "read alive too long ago: the game's");
		EXPECT(dr::Decide(true, kEnabled, true, dr::kAliveFreshMs, true, &why) == dr::Start::Ours, "read alive just in time: ours");
		EXPECT(dr::Decide(true, kEnabled, true, 20, false, &why) == dr::Start::NotHostile, "not hostile: the game's");
		EXPECT(dr::Decide(true, kEnabled | dr::kRowBoss, false, 0, false, nullptr) == dr::Start::LeftToGame, "no why asked: still decided (the row first)");
		EXPECT(dr::Dead(0) && dr::Dead(12) && !dr::Dead(1) && !dr::Dead(17), "modes 0 and 12 are death");
	}
	const float chest[3] = {10.0f, 1.3f, 20.0f};
	{   // the push of the strike that killed it
		dr::Blows blows;
		const float j[3] = {0.0f, 0.0f, 30.0f}, at[3] = {10.0f, 1.2f, 19.8f};
		blows.Note(7, j, at, 6.0f, 4.5f, 100.0);
		const dr::Blow* b = blows.Last(7, 100.3);
		EXPECT(b && b->swingMs == 6.0f, "the strike 0.3 s ago is the killing one");
		const dr::Push p = dr::KillingPush(b, chest, 30.0f, nullptr);
		EXPECT(p.byBlow && std::fabs(p.gaveMs - 0.9f) < 1e-4f, "4.5 kg x 6 m/s over 30 kg: 0.9 m/s (got %.3f)", p.gaveMs);
		EXPECT(std::fabs(p.impulse[2] - 27.0f) < 1e-3f && std::fabs(p.impulse[0]) < 1e-6f, "its way the strike's, 0.9 m/s x 30 kg");
		EXPECT(p.point[1] == 1.2f, "where the strike met it");
		const dr::Blow poke{7, 100.0, {0, 0, 1}, {0, 1, 0}, 1.0f, 1.0f}, smash{7, 100.0, {1, 0, 0}, {0, 1, 0}, 20.0f, 15.0f};
		const dr::Push light = dr::KillingPush(&poke, chest, 60.0f, nullptr);
		EXPECT(std::fabs(light.gaveMs - dr::kMinMs) < 1e-4f, "a light poke still fells it at the least (got %.2f)", light.gaveMs);
		const dr::Push huge = dr::KillingPush(&smash, chest, 30.0f, nullptr);
		EXPECT(std::fabs(huge.gaveMs - dr::kMaxMs) < 1e-4f && std::fabs(Len(huge.impulse) - dr::kMaxMs * 30.0f) < 1e-3f, "a huge blow is held to the most");
		EXPECT(!blows.Last(7, 100.0 + dr::kBlowS + 0.01), "an older strike killed nothing");
		EXPECT(!blows.Last(8, 100.1), "another body's strike is not this one's");
		EXPECT(!blows.Last(7, 99.0), "a strike after the death is not it");
		// a strike's later steps press on slower: the first carries the swing
		const float j2[3] = {5.0f, 0.0f, 0.0f};
		blows.Note(7, j2, at, 0.5f, 4.5f, 100.05);
		b = blows.Last(7, 100.1);
		EXPECT(b && b->swingMs == 6.0f && b->impulse[2] == 30.0f, "a slower press after the strike keeps the strike's swing and way");
		blows.Note(7, j2, at, 3.0f, 4.5f, 100.5);   // a new strike (0.45 s later), slower
		b = blows.Last(7, 100.6);
		EXPECT(b && b->swingMs == 3.0f && b->impulse[0] == 5.0f, "a new strike replaces the old one, slower or not");
		dr::Blows ring;
		for (uint32_t id = 1; id <= dr::Blows::kMax + 1; ++id) ring.Note(id, j, at, 5.0f, 4.0f, 10.0 + id * 0.01);
		EXPECT(!ring.Last(1, 10.3) && ring.Last(2, 10.3) && ring.Last(dr::Blows::kMax + 1, 10.3), "full: the body struck longest ago makes room");
		const float bad[3] = {NAN, 0.0f, 0.0f};
		ring.Note(3, bad, at, 5.0f, 4.0f, 10.2);
		EXPECT(ring.Last(3, 10.2) && ring.Last(3, 10.2)->t < 10.1 && ring.Last(3, 10.2)->impulse[2] == 30.0f, "a push not finite is not noted");
	}
	{   // no strike: away from the hero
		const float hero[3] = {10.0f, 0.0f, 17.0f};
		const dr::Push p = dr::KillingPush(nullptr, chest, 40.0f, hero);
		EXPECT(!p.byBlow && std::fabs(p.gaveMs - dr::kAwayMs) < 1e-4f, "no blow: pushed kAwayMs");
		EXPECT(std::fabs(p.impulse[2] - dr::kAwayMs * 40.0f) < 1e-3f && std::fabs(p.impulse[0]) < 1e-5f && p.impulse[1] == 0.0f, "away from the hero, across the ground");
		EXPECT(p.point[0] == chest[0] && p.point[1] == chest[1], "at its chest");
		const float under[3] = {10.0f, 0.0f, 20.0f};
		const dr::Push q = dr::KillingPush(nullptr, chest, 40.0f, under);
		EXPECT(Len(q.impulse) == 0.0f, "the hero right under it: no way (the library falls it back)");
		const dr::Push r = dr::KillingPush(nullptr, chest, 40.0f, nullptr);
		EXPECT(Len(r.impulse) == 0.0f && !r.byBlow, "no hero known: no way");
		const dr::Blow still{7, 1.0, {0, 0, 0}, {0, 0, 0}, 5.0f, 5.0f};
		const dr::Push s = dr::KillingPush(&still, chest, 40.0f, hero);
		EXPECT(!s.byBlow && s.impulse[2] > 0.0f, "a strike of no way: away from the hero instead");
		const dr::Push m = dr::KillingPush(nullptr, chest, NAN, hero);
		EXPECT(std::isfinite(m.impulse[2]) && m.impulse[2] > 0.0f, "a mass not known: a man's");
	}
	{   // the phases
		EXPECT(dr::OnPhase(D2RF_FALLING) == dr::Next::Wait, "falling: wait");
		EXPECT(dr::OnPhase(D2RF_HELD) == dr::Next::Wait, "held by a hand: wait");
		EXPECT(dr::OnPhase(D2RF_LYING) == dr::Next::Keep, "lying still: kept");
		EXPECT(dr::OnPhase(D2RF_GETTING_UP) == dr::Next::Keep, "a get-up begun within a step: kept as it is");
		EXPECT(dr::OnPhase(D2RF_STAGGERING) == dr::Next::Fell && dr::OnPhase(D2RF_WALKING) == dr::Next::Fell && dr::OnPhase(D2RF_BACK) == dr::Next::Fell,
		       "died on its feet: falls anew");
		EXPECT(dr::OnPhase(D2RF_NONE) == dr::Next::Wait, "none: wait");
	}
	{   // the ease back into reach
		EXPECT(dr::Weight(5000, 0) == 1.0f, "never away: all ours");
		EXPECT(dr::Weight(5000, 5000) == 0.0f, "back this moment: none yet");
		EXPECT(std::fabs(dr::Weight(5150, 5000) - 0.5f) < 1e-4f, "half the ease: half");
		EXPECT(dr::Weight(5000 + dr::kEaseMs + 1, 5000) == 1.0f, "after the ease: all");
		dr::Corpse c;
		dr::Drawn(c, 1000);
		EXPECT(c.backMs == 0 && c.drawnMs == 1000, "the first draw is no coming back");
		dr::Drawn(c, 1010);
		EXPECT(c.backMs == 0, "drawn tick after tick: never away");
		dr::Drawn(c, 1010 + dr::kBackGapMs + 1);
		EXPECT(c.backMs == 1010 + dr::kBackGapMs + 1, "not drawn for a while: it came back, eased in from now");
	}
	{   // the corpses kept
		dr::Corpses k;
		dr::Corpse c;
		c.id = 1;
		c.seenMs = 100;
		EXPECT(k.Keep(c) == 0 && k.Count() == 1 && k.Of(1), "kept");
		c.offM = 0.4f;
		EXPECT(k.Keep(c) == 0 && k.Count() == 1 && k.Of(1)->offM == 0.4f, "kept again: replaced, not twice");
		for (uint32_t id = 2; id <= dr::kMaxCorpses; ++id) {
			c.id = id;
			c.seenMs = 100 + id;
			k.Keep(c);
		}
		EXPECT(k.Count() == dr::kMaxCorpses, "full");
		c.id = 999;
		c.seenMs = 500;
		EXPECT(k.Keep(c) == 1 && !k.Of(1) && k.Of(999) && k.Count() == dr::kMaxCorpses, "full: the one seen longest ago made room");
		uint32_t listed[2] = {999, 5};
		EXPECT(k.Forget(listed, 2, 100 + dr::kGoneMs) == 0, "a second not yet: none forgotten");
		EXPECT(k.Forget(listed, 2, 200 + dr::kGoneMs + 30) == dr::kMaxCorpses - 2 && k.Count() == 2 && k.Of(999) && k.Of(5), "the unlisted forgotten after a second, the listed kept");
		EXPECT(k.Of(5)->seenMs == 200 + dr::kGoneMs + 30, "the listed are seen");
		EXPECT(!k.Of(0), "id 0 is none");
		k.Reset();
		EXPECT(k.Count() == 0 && !k.Of(999), "reset");
	}
	{   // the deaths decided once
		dr::Told t;
		EXPECT(!t.Has(4), "not yet");
		t.Add(4, 1000);
		EXPECT(t.Has(4), "decided");
		const uint32_t listed[1] = {4};
		t.Seen(listed, 1, 1500);
		t.Seen(nullptr, 0, 1500 + dr::kGoneMs);
		EXPECT(t.Has(4), "still within a second of the last listing");
		t.Seen(nullptr, 0, 1500 + dr::kGoneMs + 1);
		EXPECT(!t.Has(4), "gone a second: forgotten (the game may give its id anew)");
		for (uint32_t id = 1; id <= dr::Told::kMax + 1; ++id) t.Add(id, 2000 + id);
		EXPECT(!t.Has(1) && t.Has(dr::Told::kMax + 1), "full: the oldest makes room");
	}
	{   // the same skeleton
		const float root[3] = {100.0f, 0.0f, 200.0f}, near[3] = {101.0f, 0.0f, 200.0f}, far[3] = {103.0f, 0.0f, 200.0f};
		const float upm = 4.0f;   // half a metre: 2 units
		EXPECT(dr::SameSkeleton(0x1000, root, 60, 0x1000, near, 60, upm), "the instance, its bones, a unit from where it stood");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x1000, far, 60, upm), "the instance standing elsewhere: another unit's now");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x2000, root, 60, upm), "another instance");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x1000, root, 61, upm), "another bone count");
		EXPECT(!dr::SameSkeleton(0, root, 60, 0, root, 60, upm), "no instance");
	}
	std::printf(g_fails ? "death_ragdoll_test: %d FAILED\n" : "death_ragdoll_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
