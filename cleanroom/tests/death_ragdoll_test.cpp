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
#include "../../vr/doll_pose.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

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

// A made-up game memory, byte by byte: a monster unit (type, class, its monster data), the data's row pointer, the MonStats row (its id, its flags at 0x3C).
struct Mem {
	std::map<uintptr_t, uint8_t> b;
	template <class T> void Put(uintptr_t a, T v) {
		uint8_t raw[sizeof v];
		std::memcpy(raw, &v, sizeof v);
		for (size_t i = 0; i < sizeof v; ++i) b[a + i] = raw[i];
	}
	template <class T> bool operator()(uintptr_t a, T& v) const {
		uint8_t raw[sizeof(T)];
		for (size_t i = 0; i < sizeof(T); ++i) {
			const auto it = b.find(a + i);
			if (it == b.end()) return false;
			raw[i] = it->second;
		}
		std::memcpy(&v, raw, sizeof v);
		return true;
	}
	// A monster unit at `unit` of class cls, its data at unit + 0x1000, its row at row (id rowId, flags).
	void Monster(uintptr_t unit, uint32_t cls, uintptr_t row, uint16_t rowId, uint32_t flags) {
		Put<uint32_t>(unit + 0x00, 1u);
		Put<uint32_t>(unit + 0x04, cls);
		Put<uintptr_t>(unit + 0x10, unit + 0x1000);
		Put<uintptr_t>(unit + 0x1000, row);
		Put<uint16_t>(row + 0x00, rowId);
		Put<uint32_t>(row + 0x3C, flags);
	}
};

}  // namespace

int main() {
	{   // 0.199.5 «трупы дёргаются» (weapon in hand): why a kept corpse could not go through the moving bodies' list - the kept table (vr/knock_down.cpp) is its one source now
		dollpose::Pose kept;
		kept.self = 0x10;
		kept.bones = 30;
		kept.knock.weight = 1.0f;
		kept.knock.frozen = true;
		dollpose::Pose made[16], old[1] = {kept};
		const int n = dollpose::Carry(made, 0, 16, old, 1, 1000, 300);   // one tick it was not made (crowded out of the 16, its skeleton not read)
		EXPECT(n == 1 && made[0].fadeFromMs == 1000, "a corpse missed one tick is carried as fading");
		const dollpose::Pose drawn = dollpose::Faded(made[0], 1.0f - 16.0f / 300.0f);   // (one frame later, as MonsterPosesForSkin eases it)
		EXPECT(drawn.knock.weight < 0.999f, "... and one frame of that fade takes it off its frozen bones (weight %.3f): a twitch", drawn.knock.weight);
		dr::Corpse c;
		dr::Drawn(c, 1000);
		dr::Drawn(c, 1000 + dr::kBackGapMs + 1);   // a 151 ms gap between two ticks that drew it
		EXPECT(dr::Weight(1000 + dr::kBackGapMs + 1, c.backMs) == 0.0f, "a 151 ms gap eased a kept corpse in again from the game's pose");
	}
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
	{   // 0.195.1, Serg's log: zombie1, fallen1, quillrat1 - plain monsters - were all "a prime evil" (the flags read from the touch getter's row, a
		// MonStats2 row, whose 0x3C had bit 7 for every monster) and nothing fell. The flags come from the unit's own MonStats row now.
		constexpr uint32_t kSpawn = 1u << 0, kMelee = 1u << 1, kDoors = 1u << 3, kLUndead = 1u << 11;
		Mem m;
		m.Monster(0x100000, 2, 0x900000, 2, kEnabled | kKillable | kSpawn | kMelee | kLUndead);   // zombie1
		m.Monster(0x200000, 19, 0x901000, 19, kEnabled | kKillable | kSpawn | kMelee | kDoors);    // fallen1
		m.Monster(0x300000, 156, 0x902000, 156, kEnabled | kKillable | dr::kRowBoss | dr::kRowPrimeEvil);   // andariel
		m.Monster(0x400000, 2, 0x903000, 7, kEnabled | kKillable);        // a row that is not the unit's own (another class's id)
		m.Monster(0x500000, 5, 0x904000, 5, kEnabled | dr::kRowDeathDmg);
		uint32_t f = 0;
		const char* why = nullptr;
		EXPECT(dr::UnitRowFlags(0x100000, m, &f) && !(f & (dr::kRowBoss | dr::kRowPrimeEvil)), "zombie1's own row: read, no boss bits (0x%08X)", f);
		EXPECT(dr::Decide(true, f, true, 20, true, &why) == dr::Start::Ours, "zombie1 falls as ours (%s)", why ? why : "-");
		EXPECT(dr::UnitRowFlags(0x200000, m, &f) && dr::Decide(true, f, true, 20, true, &why) == dr::Start::Ours, "fallen1 falls as ours (%s)", why ? why : "-");
		EXPECT(dr::UnitRowFlags(0x300000, m, &f) && dr::Decide(true, f, true, 20, true, &why) == dr::Start::LeftToGame, "Andariel stays the game's");
		EXPECT(dr::UnitRowFlags(0x500000, m, &f) && dr::Decide(true, f, true, 20, true, nullptr) == dr::Start::LeftToGame, "one that bursts stays the game's");
		EXPECT(!dr::UnitRowFlags(0x400000, m, &f), "a row whose id is another class's is not read as this unit's");
		EXPECT(!dr::UnitRowFlags(0x600000, m, &f), "no unit there: not read");
		Mem p;
		p.Monster(0x100000, 2, 0x900000, 2, kEnabled);
		p.Put<uint32_t>(0x100000, 0u);   // a player's type
		EXPECT(!dr::UnitRowFlags(0x100000, p, &f), "not a monster: not read");
		Mem z;
		z.Monster(0x100000, 2, 0x900000, 2, kEnabled);
		z.Put<uintptr_t>(0x101000, 0);   // no row behind the data
		EXPECT(!dr::UnitRowFlags(0x100000, z, &f), "a null row pointer: not read");
		EXPECT(!dr::UnitRowFlags(0x100000, m, nullptr), "no flags to fill: not read");
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
	{   // [weight] death_ragdoll_flat (Serg: «режим 2д падение монстров при смерти по дуге оружия»): the push from the game's own swing of the hero's weapon
		// a right-hand swing: the weapon bone round the hero over 0.3 s at 90 Hz, slow at its ends, at its fastest crossing in front of him going +x; he stands still
		dr::ArcSample s[40];
		int n = 0;
		const double t0 = 10.0;
		for (int i = 0; i < 30; ++i, ++n) {
			const double t = t0 + i / 90.0;
			const float u = static_cast<float>(i) / 29.0f;
			const float ang = -1.2f + 2.4f * (0.5f - 0.5f * std::cos(3.14159265f * u));   // -69 .. +69 deg about the hero, radius 0.9 m, fastest in the middle
			const float r = 0.9f;
			s[n].t = t;
			s[n].ok[0] = true;
			s[n].p[0][0] = r * std::sin(ang);
			s[n].p[0][1] = 1.2f;
			s[n].p[0][2] = r * std::cos(ang);
		}
		const double tKill = t0 + 29 / 90.0 + 0.03;   // seen a scan after the swing's last frame
		const dr::Arc a = dr::ArcAt(s, n, tKill);
		const float want = 0.9f * 2.4f * 0.5f * 3.14159265f / (29.0f / 90.0f);   // the peak angular speed times the radius: ~10.5 m/s (a step's chord a little under)
		EXPECT(a.ok && a.hand == 0 && std::fabs(a.speed - want) < 0.3f, "the swing read: %.2f m/s (want %.2f), hand %d", a.speed, want, a.hand);
		EXPECT(a.vel[0] > 0.0f && std::fabs(a.vel[1]) < 1e-3f && std::fabs(a.vel[2]) < 0.1f * a.speed, "its way: across in front of the hero, level (%.2f %.2f %.2f)", a.vel[0],
		       a.vel[1], a.vel[2]);
		// the same swing while he runs +z at 5 m/s: his model's step taken off - the same arc
		dr::ArcSample run[40];
		for (int i = 0; i < n; ++i) {
			run[i] = s[i];
			const float dz = 5.0f * static_cast<float>(s[i].t - t0);
			run[i].root[2] = dz;
			run[i].p[0][2] += dz;
		}
		const dr::Arc ar = dr::ArcAt(run, n, tKill);
		EXPECT(ar.ok && std::fabs(ar.speed - a.speed) < 0.02f && std::fabs(ar.vel[2] - a.vel[2]) < 0.05f, "running: the arc the same (%.2f vs %.2f m/s)", ar.speed, a.speed);
		// the push: the arc's way, heft x speed over the mass, held; at the chest
		const float arcChest[3] = {0.5f, 1.3f, 1.0f}, hero[3] = {0.0f, 0.0f, 0.0f};
		const dr::Push p = dr::ArcPush(a, arcChest, 33.0f, hero);
		EXPECT(p.byBlow && std::fabs(p.gaveMs - std::clamp(dr::kFlatHeftKg * a.speed / 33.0f, dr::kMinMs, dr::kMaxMs)) < 1e-4f, "a fallen of 33 kg: %.2f m/s", p.gaveMs);
		const float j = std::sqrt(p.impulse[0] * p.impulse[0] + p.impulse[1] * p.impulse[1] + p.impulse[2] * p.impulse[2]);
		EXPECT(std::fabs(j - p.gaveMs * 33.0f) < 1e-2f && p.impulse[0] > 0.0f && p.point[1] == 1.3f, "the impulse along the arc, at the chest");
		const dr::Push heavy = dr::ArcPush(a, arcChest, 400.0f, hero);
		EXPECT(std::fabs(heavy.gaveMs - dr::kMinMs) < 1e-4f, "a heavy one still falls (the least)");
		// a swing long over, too slow, a jump, a gap, the left hand's
		EXPECT(!dr::ArcAt(s, n, tKill + 1.0).ok, "a swing a second before the kill: no arc");
		dr::ArcSample slow[40];
		for (int i = 0; i < n; ++i) {
			slow[i] = s[i];
			for (int k = 0; k < 3; ++k) slow[i].p[0][k] *= 0.1f;
		}
		EXPECT(!dr::ArcAt(slow, n, tKill).ok, "the weapon hardly moving (a cast, a stand): no arc");
		const dr::Push away = dr::ArcPush(dr::ArcAt(slow, n, tKill), arcChest, 33.0f, hero);
		EXPECT(!away.byBlow && away.impulse[2] > 0.0f && std::fabs(away.gaveMs - dr::kAwayMs) < 1e-4f, "no arc: away from the hero");
		dr::ArcSample jump[3] = {};
		jump[0].t = 1.0; jump[1].t = 1.011; jump[2].t = 1.022;
		for (int i = 0; i < 3; ++i) jump[i].ok[0] = true;
		jump[1].p[0][0] = 3.0f;   // 270 m/s: a model made again
		jump[2].p[0][0] = 3.0f;
		EXPECT(!dr::ArcAt(jump, 3, 1.03).ok, "a jump is no swing");
		dr::ArcSample gap[2] = {};
		gap[0].t = 1.0; gap[1].t = 1.3; gap[0].ok[0] = gap[1].ok[0] = true; gap[1].p[0][0] = 1.0f;
		EXPECT(!dr::ArcAt(gap, 2, 1.3).ok, "a gap between samples: no speed from it");
		dr::ArcSample left[40];
		for (int i = 0; i < n; ++i) {
			left[i] = slow[i];
			left[i].ok[1] = true;
			std::memcpy(left[i].p[1], s[i].p[0], sizeof left[i].p[1]);
		}
		const dr::Arc al = dr::ArcAt(left, n, tKill);
		EXPECT(al.ok && al.hand == 1, "the left hand's swing (two weapons): its arc");
		EXPECT(!dr::ArcAt(nullptr, 5, tKill).ok && !dr::ArcAt(s, 1, tKill).ok, "no samples: none");
	}
	{   // 0.197.4, Serg: «через 2 секунды труп прыгает туда, где игра его положила» - a kept corpse was let go of when its unit fell out of the dead scan
		// (24 units round the hero, our ragdoll a few metres from the game's corpse); bodies thrown 6 - 11 m by a pelvis read off two rigs; arc speeds from
		// two poses 3 ms apart
		EXPECT(dr::kDeadReachUnits >= 100.0f, "the dead are looked for far beyond the living's 24 units (%.0f)", dr::kDeadReachUnits);
		float v[3];
		const float fast[3] = {6.0f, 3.0f, -8.0f};
		dr::DeathVel(fast, v);
		EXPECT(std::fabs(std::sqrt(v[0] * v[0] + v[2] * v[2]) - dr::kDeathVelMs) < 1e-4f && v[1] == 0.0f && v[0] > 0.0f && v[2] < 0.0f,
		       "a body's motion of 10 m/s handed to its fall as its walk at most (%.2f %.2f %.2f)", v[0], v[1], v[2]);
		const float walk[3] = {0.6f, 0.0f, 0.3f};
		dr::DeathVel(walk, v);
		EXPECT(v[0] == 0.6f && v[2] == 0.3f, "a walk handed as it is");
		const float bad[3] = {NAN, 0.0f, 0.0f};
		dr::DeathVel(bad, v);
		EXPECT(v[0] == 0.0f && v[2] == 0.0f, "not a number: none");
		dr::DeathVel(nullptr, v);
		EXPECT(v[0] == 0.0f, "none: none");
		// released: the ids dropped, for the log
		dr::Corpses k;
		dr::Corpse c;
		for (uint32_t id = 1; id <= 3; ++id) {
			c.id = id;
			c.seenMs = 1000;
			k.Keep(c);
		}
		const uint32_t listed[1] = {2};
		uint32_t ids[4] = {};
		const int n = k.Forget(listed, 1, 1000 + dr::kGoneMs + 1, ids, 4);
		EXPECT(n == 2 && ((ids[0] == 1 && ids[1] == 3) || (ids[0] == 3 && ids[1] == 1)) && k.Count() == 1 && k.Of(2), "released: the unlisted two, their ids told");
		// two poses 3 ms apart are no step of a swing
		dr::ArcSample twin[3] = {};
		twin[0].t = 1.0; twin[1].t = 1.003; twin[2].t = 1.014;
		for (int i = 0; i < 3; ++i) twin[i].ok[0] = true;
		twin[1].p[0][0] = 0.09f;   // 30 m/s between the two close poses
		twin[2].p[0][0] = 0.12f;   // 2.7 m/s over the frame
		const dr::Arc a = dr::ArcAt(twin, 3, 1.02);
		EXPECT(a.ok && a.speed < 3.0f, "the frame's step read, not the two close poses' (%.1f m/s)", a.speed);
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
		const float root[3] = {100.0f, 0.0f, 200.0f}, near[3] = {101.0f, 0.0f, 200.0f}, cell[3] = {105.0f, 0.0f, 202.0f}, far[3] = {110.0f, 0.0f, 200.0f};
		const float upm = 4.0f;   // two metres: 8 units
		EXPECT(dr::SameSkeleton(0x1000, root, 60, 0x1000, near, 60, upm), "the instance, its bones, a unit from where it stood");
		EXPECT(dr::SameSkeleton(0x1000, root, 60, 0x1000, cell, 60, upm),
		       "the game eased its model 1.35 m to its cell: still its skeleton (0.196.1: at 0.5 m the corpse was lost there, drawn as the game's)");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x1000, far, 60, upm), "the instance standing 2.5 m elsewhere: another unit's now");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x2000, root, 60, upm), "another instance");
		EXPECT(!dr::SameSkeleton(0x1000, root, 60, 0x1000, root, 61, upm), "another bone count");
		EXPECT(!dr::SameSkeleton(0, root, 60, 0, root, 60, upm), "no instance");
	}
	{   // 0.195.3, Serg: a zombie's torso lay on the ground, an arm and a hand floated where they had stood - parts hung on its bones, which the game stops
		// making once its death animation is over. 0.196.1: a zombie killed while held - its pieces still in the air: the part was forgotten the first tick
		// its corpse was not drawn, and the storage refused its write. Kept while the body is a dead one we have (drawn or not), written through its own
		// component while that still holds it, a part on a part goes with it.
		const auto act = [](bool known, bool poseNow, bool ours) { return dr::HangNext(known, poseNow, ours).act; };
		EXPECT(act(true, true, false) == dr::HangDo::Carry && act(true, true, true) == dr::HangDo::Carry, "a dead body drawn: carried with it");
		EXPECT(act(true, false, true) == dr::HangDo::PutBack && !dr::HangNext(true, false, true).drop,
		       "a dead body not drawn this moment (out of reach): the game's matrix back - and KEPT (0.196.1: it was forgotten here)");
		EXPECT(act(true, false, false) == dr::HangDo::Nothing && !dr::HangNext(true, false, false).drop, "not drawn, the game's in it: nothing, kept");
		EXPECT(act(false, false, true) == dr::HangDo::PutBack && dr::HangNext(false, false, true).drop, "the body gone: the game's matrix back, forgotten");
		EXPECT(act(false, true, false) == dr::HangDo::Nothing && dr::HangNext(false, true, false).drop, "not a dead body of ours: nothing, forgotten");

		const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
		const float game[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1}, bone[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1};
		const float local[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.1f, 0.2f, 0.3f, 1};
		dr::Hangs h;
		dr::Hang* a = h.Note(0x300064, 0x5000, 40, 0, game, bone, local, 0x00000999u, 0xABC000);
		EXPECT(a && h.Count() == 1 && a->entity == 0x300064 && a->bone == 40 && a->onEntity == 0 && a->game[12] == 5.0f && !a->ours && a->component == 0xABC000,
		       "a part on a bone noted as the game made it, with its component");
		a->ours = true;
		const float game2[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 8, 6, 7, 1};
		a = h.Note(0x300064, 0x5000, 40, 0, game2, bone, local, 0x00000999u, 0xABC000);
		EXPECT(h.Count() == 1 && a->game[12] == 8.0f && !a->ours, "the game made it again: its matrix replaces ours, nothing owed");
		EXPECT(!h.Note(0, 0x5000, 40, 0, game, bone, local, 0, 0) && !h.Note(101, 0, 40, 0, game, bone, local, 0, 0) &&
		           !h.Note(102, 0x5000, -1, 0, game, bone, local, 0, 0) && !h.Note(103, 0x5000, 40, 0, game, bone, nullptr, 0, 0) && h.Count() == 1,
		       "no entity, no body, neither a bone nor a part to hang on, no local matrix: not noted");
		// a forearm piece on the upper arm's part: its parent field names the part's slot (the low 20 bits)
		EXPECT(h.OfSlot(0x10000064u) == h.Of(0x300064) && !h.OfSlot(0xFFFFFu) && !h.OfSlot(0x65u), "the part hung on is found by its slot");
		dr::Hang* c = h.Note(0x300070, 0x5000, -1, 0x300064, game, I, local, 0x10000064u, 0xABD000);
		EXPECT(c && c->bone == -1 && c->onEntity == 0x300064 && h.Count() == 2, "a part on a part noted, no bone of its own");
		float w[16];
		const float parentWorld[16] = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 10, 0, 20, 1};   // turned a quarter about y, at (10, 0, 20)
		dr::ChildWorld(local, parentWorld, w);
		EXPECT(std::fabs(w[12] - (10.0f + 0.3f)) < 1e-5f && std::fabs(w[13] - 0.2f) < 1e-5f && std::fabs(w[14] - (20.0f - 0.1f)) < 1e-5f && w[2] == -1.0f,
		       "a part on a part: its local matrix times the parent's world (%.2f %.2f %.2f)", w[12], w[13], w[14]);
		// its own component still its own: the same parent field and local matrix
		EXPECT(dr::SameComponent(*a, local, 0x00000999u), "the component still holds the part");
		float moved[16];
		std::memcpy(moved, local, sizeof moved);
		moved[12] += 0.5f;
		EXPECT(!dr::SameComponent(*a, moved, 0x00000999u) && !dr::SameComponent(*a, local, 0x00000998u) && !dr::SameComponent(*a, nullptr, 0x00000999u),
		       "another local matrix or parent there: another part's component now - not written");
		// full
		for (uint32_t e = 200; h.Count() < dr::Hangs::kMax; ++e)
			if (dr::Hang* x = h.Note(e, 0x6000, 3, 0, game, bone, local, 0, 0)) x->ours = true;
		dr::Hang* free1 = h.Of(0x300070);
		const uint32_t gives = free1->entity;
		free1->ours = false;   // (the game's matrix in it)
		h.Of(0x300064)->ours = true;
		EXPECT(h.Note(999, 0x6000, 3, 0, game, bone, local, 0, 0) && !h.Of(gives) && h.Of(999) && h.Count() == dr::Hangs::kMax,
		       "full: the part with the game's matrix in it makes room, never one owed its matrix back");
		h.Of(999)->ours = true;
		EXPECT(!h.Note(1000, 0x6000, 3, 0, game, bone, local, 0, 0) && !h.Of(1000), "full of parts owed back: the new one waits");
		h.Drop(*h.Of(999));
		EXPECT(h.Count() == dr::Hangs::kMax - 1 && !h.Of(999), "dropped");
	}
	std::printf(g_fails ? "death_ragdoll_test: %d FAILED\n" : "death_ragdoll_test: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
