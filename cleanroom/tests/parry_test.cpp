// [weight] parry_*: the pure part (vr/parry.h) - which unit modes are a monster's attack and the hero's block, the clash once per meeting, the pulse,
// the block shown's strength by how near the blow was, the knock along the blow and its ease, the weapon's velocity from its snapshots.
// Run: build/Release/parry_test.exe   (exit code 0 = all passed)

#include "../../vr/parry.h"

#include <cmath>
#include <cstdio>

using namespace parry;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

static void Modes() {
    for (int m : {4, 5, 8, 9, 10, 11, 14}) Expect(MonsterAttacking(m), "a monster's attack modes: A1 A2 S1..S4 SQ", m);
    for (int m : {-1, 0, 1, 2, 3, 6, 7, 12, 13, 15}) Expect(!MonsterAttacking(m), "not an attack: death, neutral, walk, get hit, block, cast, dead, knockback, run", m);
    Expect(HeroBlocking(9) && !HeroBlocking(7) && !HeroBlocking(1) && !HeroBlocking(-1), "the hero's block is mode 9 (BL)");
    BlockWatch w;
    Expect(!w.Step(1) && w.Step(9) && !w.Step(9) && !w.Step(9), "the block shown once as it begins, not while it lasts");
    Expect(!w.Step(1) && w.Step(9), "... and again at the next block");
}

static void Gaps() {
    const Capsule a{{0, 0, 0}, {0, 1, 0}, 0.05f}, b{{0.3f, 0, 0}, {0.3f, 1, 0}, 0.05f};
    Expect(std::fabs(Gap(a, b) - 0.2f) < 1e-5f, "two parallel capsules 30 cm apart, 5 cm thick each: a 20 cm gap", Gap(a, b));
    const Capsule c{{0.05f, 0.5f, -1}, {0.05f, 0.5f, 1}, 0.03f};
    Expect(Gap(a, c) < 0.0f, "crossing capsules overlap", Gap(a, c));
}

static void Clash() {
    Clashes cl;
    Expect(!cl.Step(7, true, 0.3f, 1000), "apart: no clash");
    Expect(cl.Step(7, true, 0.01f, 1010), "the weapons meet while it attacks: a clash");
    Expect(!cl.Step(7, true, 0.0f, 1020) && !cl.Step(7, true, 0.04f, 1100), "... once: not again while they stay together (within the parting gap)");
    Expect(!cl.Step(7, true, 0.2f, 1150), "they part");
    Expect(!cl.Step(7, true, 0.0f, 1200), "met again within the re-arm time: no new clash");
    Expect(!cl.Step(7, true, 0.2f, 1300) && cl.Step(7, true, 0.0f, 1400), "parted and met again later: a new clash");
    Clashes idle;
    Expect(!idle.Step(9, false, 0.0f, 1000), "the weapons touch while the monster does not attack: no clash");
    Expect(idle.Step(9, true, 0.0f, 1010), "... it begins its attack with the weapons together: its blow is on the weapon - a clash");
    Expect(!idle.Step(9, true, 0.0f, 1100), "... once");
    Clashes many;
    for (uint32_t id = 1; id <= 20; ++id) many.Step(id, true, 0.3f, 1000 + id);
    Expect(many.Step(20, true, 0.0f, 2000), "with more monsters than slots the newest is still followed");
}

static void Feels() {
    const Feel lo = ClashFeel(0.0f), hi = ClashFeel(1.0f), nan = ClashFeel(std::nanf(""));
    Expect(lo.amplitude >= 0.6f && hi.amplitude <= 1.0f && hi.amplitude > lo.amplitude && hi.ms > lo.ms, "a clash is always a hard pulse, harder for a harder one", lo.amplitude);
    Expect(nan.amplitude == lo.amplitude, "not a number: the softest");
    Expect(BlockStrength(-0.01f) == 1.0f && BlockStrength(0.0f) == 1.0f, "the block with the weapon in the blow's path: full");
    Expect(BlockStrength(kFarM) == kFloor && BlockStrength(2.0f) == kFloor && BlockStrength(std::nanf("")) == kFloor, "far, or no blow known: the floor");
    Expect(BlockStrength(0.1f) > BlockStrength(0.3f) && BlockStrength(0.3f) > kFloor, "nearer is stronger", BlockStrength(0.1f));
}

static void Knocks() {
    Expect(!KnockOf({0.2f, 0, 0}).ok, "a weapon hardly moving: no knock");
    const Knock slow = KnockOf({0, -1.0f, 0}), fast = KnockOf({0, 0, 6.0f});
    Expect(slow.ok && std::fabs(slow.dir.y + 1.0f) < 1e-5f && slow.metres >= kKnockMinM && slow.metres < kKnockMaxM, "a slow blow down: knocked down a little", slow.metres);
    Expect(fast.ok && std::fabs(fast.dir.z - 1.0f) < 1e-5f && std::fabs(fast.metres - kKnockMaxM) < 1e-5f, "a fast blow: knocked the most along it", fast.metres);
    Expect(KnockShare(0) == 1.0f && KnockShare(kKnockMs / 2) > 0.0f && KnockShare(kKnockMs / 2) < 0.5f && KnockShare(kKnockMs) == 0.0f && KnockShare(-5) == 0.0f, "the knock: at once, eased back to nothing");
    WeaponMotion m;
    Expect(stroke::Length(m.Feed(3, {0, 1, 0}, 1000)) == 0.0f, "the first snapshot: no velocity yet");
    stroke::Vec v = m.Feed(3, {0.02f, 1, 0}, 1010);   // 2 m/s along x
    Expect(v.x > 1.0f && v.x <= 2.0f + 1e-4f, "two snapshots 10 ms apart, 2 cm: toward 2 m/s", v.x);
    Expect(m.Feed(3, {0.5f, 1, 0}, 1010).x == v.x, "the same snapshot asked again: the same velocity");
    for (int i = 2; i < 8; ++i) v = m.Feed(3, {0.02f * i, 1, 0}, 1000 + 10 * i);
    Expect(std::fabs(v.x - 2.0f) < 0.05f, "a steady swing: its speed", v.x);
    Expect(stroke::Length(m.Feed(3, {1, 1, 0}, 5000)) == 0.0f, "a snapshot long after: no velocity (not a jump)");
}

int main() {
    Modes();
    Gaps();
    Clash();
    Feels();
    Knocks();
    if (g_failures) {
        std::printf("parry_test: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("parry_test: all passed\n");
    return 0;
}
