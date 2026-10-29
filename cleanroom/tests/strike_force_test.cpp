// [weight] strike_by_force: the force judge (vr/strike_force.h) - the threshold from the steel, the score (the window's impulse, an edge's cut), no hand no hit,
// a blow shared between monsters, the stuck repeat - and the author's recorded blows (2026-10-24, a Barbarian, 225 strikes: strikes_144913.csv): seven
// quantiles of each weapon's strikes (the score's order) checked in below. A touch (under 1 N s) is never counted; the p25 of every weapon's real blows is.
// Run: build/Release/strike_force_test.exe   (exit code 0 = all passed)

#include "../../vr/strike_force.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace strikeforce;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// item, heft::hold::Kind (1 sword, 2 axe, 3 mace, 8 spear, 9 javelin), hands, steel kg, the first 150 ms (N s), cut (J), the quantile among that weapon's strikes
struct Row { const char* item; int kind, hands; float steel; double first, cut; float q; };
static const Row kRows[] = {
{"2hs", 1, 2, 2.40f, 0.19, 0.3, 0.00f},
    {"2hs", 1, 2, 2.11f, 3.46, 4.9, 0.10f},
    {"2hs", 1, 2, 2.11f, 8.87, 9.7, 0.25f},
    {"2hs", 1, 2, 2.11f, 11.56, 43.5, 0.50f},
    {"2hs", 1, 2, 2.40f, 12.20, 85.1, 0.75f},
    {"2hs", 1, 2, 2.40f, 27.79, 118.3, 0.90f},
    {"2hs", 1, 2, 2.40f, 65.67, 1317.4, 1.00f},
    {"clb", 3, 1, 1.21f, 3.96, 7.9, 0.00f},
    {"clb", 3, 1, 1.21f, 5.82, 56.4, 0.10f},
    {"clb", 3, 1, 1.21f, 7.88, 0.0, 0.25f},
    {"clb", 3, 1, 1.21f, 11.05, 2.8, 0.50f},
    {"clb", 3, 1, 1.21f, 13.09, 6.6, 0.75f},
    {"clb", 3, 1, 1.21f, 18.00, 14.2, 0.90f},
    {"clb", 3, 1, 1.21f, 18.25, 35.7, 1.00f},
    {"hax", 2, 1, 0.99f, 0.00, 0.0, 0.00f},
    {"hax", 2, 1, 0.99f, 2.10, 17.5, 0.10f},
    {"hax", 2, 1, 0.99f, 7.89, 21.6, 0.25f},
    {"hax", 2, 1, 0.99f, 8.67, 86.2, 0.50f},
    {"hax", 2, 1, 0.99f, 4.64, 285.1, 0.75f},
    {"hax", 2, 1, 0.99f, 13.08, 341.1, 0.90f},
    {"hax", 2, 1, 0.99f, 17.34, 1326.2, 1.00f},
    {"jav", 9, 2, 0.46f, 0.44, 1.2, 0.00f},
    {"jav", 9, 2, 0.46f, 0.70, 5.0, 0.10f},
    {"jav", 9, 2, 0.46f, 2.15, 6.6, 0.25f},
    {"jav", 9, 2, 0.46f, 5.50, 12.2, 0.50f},
    {"jav", 9, 2, 0.46f, 10.36, 64.2, 0.75f},
    {"jav", 9, 2, 0.46f, 15.07, 287.2, 0.90f},
    {"jav", 9, 2, 0.46f, 26.65, 99.5, 1.00f},
    {"lax", 2, 1, 2.97f, 1.34, 7.2, 0.00f},
    {"lax", 2, 1, 2.97f, 2.09, 1.1, 0.10f},
    {"lax", 2, 1, 2.97f, 2.21, 5.8, 0.25f},
    {"lax", 2, 1, 2.97f, 7.69, 4.3, 0.50f},
    {"lax", 2, 1, 2.97f, 8.66, 150.1, 0.75f},
    {"lax", 2, 1, 2.97f, 23.83, 21.3, 0.90f},
    {"lax", 2, 1, 2.97f, 24.19, 125.5, 1.00f},
    {"lax", 2, 2, 2.97f, 0.00, 0.0, 0.00f},
    {"lax", 2, 2, 2.97f, 2.65, 12.8, 0.10f},
    {"lax", 2, 2, 2.97f, 5.66, 26.2, 0.25f},
    {"lax", 2, 2, 2.97f, 9.98, 68.6, 0.50f},
    {"lax", 2, 2, 2.97f, 24.49, 63.5, 0.75f},
    {"lax", 2, 2, 2.97f, 21.82, 185.5, 0.90f},
    {"lax", 2, 2, 2.97f, 59.72, 314.7, 1.00f},
    {"spr", 8, 2, 0.74f, 1.80, 0.7, 0.00f},
    {"spr", 8, 2, 0.74f, 3.39, 31.3, 0.10f},
    {"spr", 8, 2, 0.74f, 5.07, 0.5, 0.25f},
    {"spr", 8, 2, 0.74f, 8.06, 96.6, 0.50f},
    {"spr", 8, 2, 0.74f, 9.83, 1.3, 0.75f},
    {"spr", 8, 2, 0.74f, 10.90, 98.1, 0.90f},
    {"spr", 8, 2, 0.74f, 13.45, 50.2, 1.00f},
    {"ssd", 1, 1, 0.88f, 0.00, 0.0, 0.00f},
    {"ssd", 1, 1, 0.88f, 0.01, 0.0, 0.10f},
    {"ssd", 1, 1, 0.88f, 1.04, 0.9, 0.25f},
    {"ssd", 1, 1, 0.88f, 2.16, 25.8, 0.50f},
    {"ssd", 1, 1, 0.88f, 5.50, 42.5, 0.75f},
    {"ssd", 1, 1, 0.88f, 4.93, 101.6, 0.90f},
    {"ssd", 1, 1, 0.88f, 9.18, 376.6, 1.00f},
};

static Strike Of(const Row& r) {
    Strike k;
    k.id = 1;
    k.serial = 1;
    k.hands = r.hands;
    k.window = r.first;
    k.cut = r.cut;
    return k;
}

static void Recorded() {
    const Settings s;   // the defaults
    int counted = 0, n = 0;
    for (const Row& r : kRows) {
        const Family f = FamilyOf(r.kind);
        const Verdict v = Judge(s, f, r.steel, Of(r));
        ++n;
        counted += v.Counts() ? 1 : 0;
        if (v.score < 1.0f) Expect(!v.Counts(), "a touch (under 1 N s) is never counted", v.score);
        if (r.q >= 0.25f) Expect(v.Counts(), "the p25 and up of a weapon's recorded blows counts", v.score - v.need);
        if (v.score < 1.0f || r.q >= 0.25f) continue;
        std::printf("  %s %s, %d hand(s), p%.0f: %.2f N s against %.2f - %s\n", r.item, FamilyName(f), r.hands, r.q * 100.0f, v.score, v.need, v.Counts() ? "counts" : "refused");
    }
    std::printf("  the recorded sample: %d of %d strikes count with the defaults\n", counted, n);
}

static void Thresholds() {
    Settings s;
    Expect(std::fabs(Threshold(s, Family::Edged, 1.0f) - 1.1f) < 1e-5f && std::fabs(Threshold(s, Family::Blunt, 4.0f) - 3.0f) < 1e-5f, "general x family x sqrt(kg)");
    Expect(Threshold(s, Family::Edged, 0.3f) == s.floorNs, "never under the floor (a dagger)");
    s.massExp = 1.0f;
    Expect(std::fabs(Threshold(s, Family::Piercing, 2.0f) - 3.0f) < 1e-5f, "mass exponent 1: in proportion to the steel");
    s.massExp = 0.0f;
    Expect(Threshold(s, Family::Blunt, 0.5f) == Threshold(s, Family::Blunt, 3.0f), "mass exponent 0: one threshold for every weight");
    Settings twice;
    twice.general = 2.0f;
    Expect(std::fabs(Threshold(Settings{}, Family::Blunt, 2.0f) * 2.0f - Threshold(twice, Family::Blunt, 2.0f)) < 1e-5f, "the general factor");
    Settings bad;
    bad.general = std::nanf("");
    bad.blunt = -1.0f;
    bad.edged = 100.0f;
    bad.massExp = 9.0f;
    bad.cutWeight = -2.0f;
    bad.floorNs = 50.0f;
    bad.stuck = 7;
    const Settings c = Clamp(bad);
    Expect(c.general == 1.0f && c.blunt == 0.1f && c.edged == 20.0f && c.massExp == 1.5f && c.cutWeight == 0.0f && c.floorNs == 20.0f && c.stuck == 0, "clamped");
    Expect(FamilyOf(1) == Family::Edged && FamilyOf(2) == Family::Edged && FamilyOf(7) == Family::Edged && FamilyOf(0) == Family::Edged, "swords, axes, polearms, daggers: edged");
    Expect(FamilyOf(3) == Family::Blunt && FamilyOf(6) == Family::Blunt && FamilyOf(11) == Family::Blunt, "maces, staves, the rest: blunt");
    Expect(FamilyOf(8) == Family::Piercing && FamilyOf(9) == Family::Piercing && FamilyOf(10) == Family::Piercing, "spears, javelins, throwing: piercing");
}

static void Rules() {
    const Settings s;
    Strike k;
    k.hands = 0;
    k.window = 50.0;
    Expect(Judge(s, Family::Blunt, 1.0f, k).why == Why::NoHand, "no hand on the weapon: never, however hard (a weapon falling on a monster)");
    k.hands = 1;
    k.window = 0.5;
    k.cut = 30.0;
    Expect(Judge(s, Family::Edged, 1.0f, k).Counts(), "an edge's cut counts: 0.5 N s + 30 J");
    Expect(!Judge(s, Family::Blunt, 1.0f, k).Counts(), "... a mace's slide is no cut");
    Settings noCut = s;
    noCut.cutWeight = 0.0f;
    Expect(!Judge(noCut, Family::Edged, 1.0f, k).Counts(), "cut weight 0: the impulse alone");
    // a blow shared between two monsters: 1.0 and 0.8 N s with a 1.5 N s threshold (a 1 kg mace): each counts; alone neither would
    Strike two[3];
    two[0].hands = two[1].hands = two[2].hands = 1;
    two[0].window = 1.0;
    two[0].startS = 10.00;
    two[1].window = 0.8;
    two[1].startS = 10.05;
    two[2].window = 0.1;   // a graze on a third: too small a share
    two[2].startS = 10.05;
    Verdict v[3];
    JudgeAll(s, Family::Blunt, 1.0f, two, 3, v);
    Expect(v[0].why == Why::Shared && v[1].why == Why::Shared && v[2].why == Why::TooWeak, "shared: each with a fair share counts, a graze does not");
    Settings alone = s;
    alone.shared = false;
    JudgeAll(alone, Family::Blunt, 1.0f, two, 3, v);
    Expect(!v[0].Counts() && !v[1].Counts(), "shared off: each alone");
    two[1].startS = 10.5;
    JudgeAll(s, Family::Blunt, 1.0f, two, 2, v);
    Expect(!v[0].Counts() && !v[1].Counts(), "strikes far apart in time are not one blow");
    // stuck: mode 0 never repeats; mode 1 repeats while pushed in, the game's attack over, not sooner than kRepeatMinS
    Strike in;
    in.hands = 1;
    in.recent = 5.0;
    Expect(!RepeatDue(s, Family::Edged, 1.0f, in, false, 1.0), "stuck 0: one hit a strike");
    Settings st = s;
    st.stuck = 1;
    Expect(RepeatDue(st, Family::Edged, 1.0f, in, false, 0.5), "stuck 1: pushed in, the attack over - again");
    Expect(!RepeatDue(st, Family::Edged, 1.0f, in, true, 0.5), "... not while the game's attack runs");
    Expect(!RepeatDue(st, Family::Edged, 1.0f, in, false, 0.05), "... not sooner than the least gap");
    in.recent = 0.2;
    Expect(!RepeatDue(st, Family::Edged, 1.0f, in, false, 0.5), "... not when it only rests there (no push)");
    in.recent = 5.0;
    in.hands = 0;
    Expect(!RepeatDue(st, Family::Edged, 1.0f, in, false, 0.5), "... not with no hand on it");
    Expect(HeroAttacking(7) && HeroAttacking(8) && HeroAttacking(14) && !HeroAttacking(1) && !HeroAttacking(3) && !HeroAttacking(-1), "the hero's attack modes");
    Expect(std::strcmp(WhyText(Why::NoHand), WhyText(Why::TooWeak)) != 0, "the texts");
}

int main() {
    Thresholds();
    Rules();
    Recorded();
    if (g_failures) {
        std::printf("strike_force_test: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("strike_force_test: all passed\n");
    return 0;
}
