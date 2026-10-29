// [bow] quiver_plain_no_mana (vr/bow_mana.h) on synthetic numbers, without the game: what each bow skill costs at a level, when the hero cannot pay it, and the mana as it is at the release.
//
//   bow_mana_test

#include "../../vr/bow_mana.h"

#include <cmath>
#include <cstdio>

using namespace bowmana;

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

bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) < tol; }

void TestCost() {
    std::puts("what the skills cost");
    // level 1, in mana points - the figures the game shows for the bow's skills
    struct Lv { int32_t id; float cost; const char* name; };
    const Lv level1[] = {{6, 1.5f, "Magic Arrow"}, {7, 3.0f, "Fire Arrow"}, {11, 3.5f, "Cold Arrow"}, {12, 4.0f, "Multiple Shot"}, {16, 5.0f, "Exploding Arrow"},
                          {21, 4.0f, "Ice Arrow"}, {22, 8.0f, "Guided Arrow"}, {26, 11.0f, "Strafe"}, {27, 6.0f, "Immolation Arrow"}, {31, 9.0f, "Freezing Arrow"}};
    for (const Lv& r : level1) EXPECT(Near(CostPoints(r.id, 1), r.cost), "%s at level 1: %.3f (wanted %.3f)", r.name, CostPoints(r.id, 1), r.cost);
    // by the level: Cold Arrow +1 a point of the row's mana per level (1/32 of a mana), Magic Arrow -1, Fire Arrow none, Multiple Shot +1 (a whole mana at shift 8)
    EXPECT(Near(CostPoints(11, 20), (28 + 19) * 32 / 256.0f), "Cold Arrow at level 20: %.4f", CostPoints(11, 20));
    EXPECT(Near(CostPoints(7, 1), CostPoints(7, 20)), "Fire Arrow costs the same at every level");
    EXPECT(Near(CostPoints(12, 5), 8.0f) && Near(CostPoints(12, 20), 23.0f), "Multiple Shot: 4 + 1 a level (%.2f at 5, %.2f at 20)", CostPoints(12, 5), CostPoints(12, 20));
    EXPECT(Near(CostPoints(6, 5), 1.0f) && CostPoints(6, 5) < CostPoints(6, 1), "Magic Arrow gets cheaper with the level (%.3f at 5)", CostPoints(6, 5));
    EXPECT(Near(CostPoints(6, 13), 0.0f) && Near(CostPoints(6, 30), 0.0f), "... down to nothing (minmana 0), never negative (%.3f at 13, %.3f at 30)", CostPoints(6, 13), CostPoints(6, 30));
    // minmana is a whole point at least
    EXPECT(Near(CostPoints(22, 40), 1.0f), "Guided Arrow at level 40 (32 - 39 < 0): the least, 1 point (%.3f)", CostPoints(22, 40));
    EXPECT(CostRaw(7, 1) == 768 && CostRaw(6, 1) == 384, "the fixed point: Fire Arrow 768, Magic Arrow 384 (%lld, %lld)", (long long)CostRaw(7, 1), (long long)CostRaw(6, 1));
    // not known: not a bow skill here, a level below 1
    EXPECT(CostRaw(0, 1) < 0 && CostRaw(36, 1) < 0 && CostRaw(357, 1) < 0 && CostRaw(-1, 1) < 0, "the plain attack, Fire Bolt, interact, nothing: not judged");
    EXPECT(CostRaw(7, 0) < 0 && CostRaw(7, -3) < 0 && CostPoints(7, 0) < 0.0f, "a level below 1 is not a level");
    for (const Lv& r : level1) EXPECT(Find(r.id) != nullptr, "%s is in the table", r.name);
    int n = 0;
    for (const Row& r : kRows) { ++n; for (const Row& s : kRows) if (&r != &s) EXPECT(r.id != s.id, "ids differ"); }
    EXPECT(n == 10, "ten rows (%d)", n);
}

void TestShort() {
    std::puts("when the hero cannot pay");
    // Fire Arrow costs 3: 2.99 cannot pay it, 3.0 can (the game's >=), more can
    EXPECT(Short(7, 1, 2.99f) && Short(7, 1, 0.0f) && Short(7, 1, 2.0f), "below 3: short");
    EXPECT(!Short(7, 1, 3.0f) && !Short(7, 1, 3.01f) && !Short(7, 1, 100.0f), "3 or more: not short");
    // the game's mana is in 1/256 points: a read of 2.996 (767/256) is short by one step, 3.0 (768/256) is exact
    EXPECT(Short(7, 1, 767.0f / 256.0f) && !Short(7, 1, 768.0f / 256.0f), "one step of the fixed point decides");
    EXPECT(!Short(7, 1, 3.0f - 1e-6f) && Short(7, 1, 3.0f - 0.003f), "a mana that is a hair under what is meant is the whole step (rounded), a third of a step under is short");
    // a skill that costs nothing can always be paid; the cheapest costs a point
    EXPECT(!Short(6, 13, 0.0f), "Magic Arrow at level 13 is free: not short at 0 mana");
    EXPECT(Short(22, 40, 0.5f) && !Short(22, 40, 1.0f), "Guided Arrow at level 40: 1 point at least");
    // not known: not judged, whatever the mana
    EXPECT(!Short(0, 1, 0.0f) && !Short(36, 1, 0.0f), "the plain attack and a spell are not judged");
    EXPECT(!Short(7, 0, 0.0f) && !Short(7, -1, 0.0f), "a level not known: not judged");
    EXPECT(!Short(7, 1, -1.0f) && !Short(7, 1, NAN) && !Short(7, 1, INFINITY), "a mana not known (negative, NaN, infinite): not judged");
    // a Multiple Shot that grew with the level
    EXPECT(Short(12, 20, 22.9f) && !Short(12, 20, 23.0f), "Multiple Shot at level 20 costs 23");
}

void TestEstimate() {
    std::puts("the mana at the release");
    // nothing cast of ours: the read as it is
    EXPECT(Near(Estimate(10.0f, 100.0, -1.0, 3.0f), 10.0f), "no cast of ours: the read");
    EXPECT(Near(Estimate(10.0f, 100.0, 5000.0, 3.0f), 10.0f), "a cast long ago: the read has it already");
    // a cast pressed 80 ms ago and a read taken 200 ms ago: the read is older than the cast: its cost is off
    EXPECT(Near(Estimate(10.0f, 200.0, 80.0, 3.0f), 7.0f), "the read predates the cast: 10 - 3");
    // a cast pressed 600 ms ago and a read taken 100 ms ago: the read has it
    EXPECT(Near(Estimate(7.0f, 100.0, 600.0, 3.0f), 7.0f), "the read follows the cast");
    // a read taken just after the press (the game has not taken the mana yet): still off
    EXPECT(Near(Estimate(10.0f, 50.0, 100.0, 3.0f), 7.0f), "a read 50 ms after a press 100 ms ago is within the spend lag: off");
    EXPECT(Near(Estimate(10.0f, 20.0, 300.0, 3.0f), 10.0f), "a read 280 ms after the press has it");
    // never below nothing
    EXPECT(Near(Estimate(1.0f, 300.0, 50.0, 3.0f), 0.0f), "not below 0");
    // a cast that cost nothing leaves the read alone; not known stays not known
    EXPECT(Near(Estimate(10.0f, 300.0, 50.0, 0.0f), 10.0f), "a free cast changes nothing");
    EXPECT(Estimate(-1.0f, 10.0, 10.0, 3.0f) < 0.0f && Estimate(NAN, 10.0, 10.0, 3.0f) < 0.0f, "unknown stays unknown");
    // the decision on top: 4 mana, a Fire Arrow shot 80 ms ago on a read from before it (4 - 3 = 1 left): the next Fire Arrow cannot be paid
    EXPECT(Short(7, 1, Estimate(4.0f, 200.0, 80.0, 3.0f)), "after a cast the mana left cannot pay the next");
    EXPECT(!Short(7, 1, Estimate(4.0f, 20.0, 700.0, 3.0f)), "... but the read that has not been spent from can");
}

void TestGate() {
    std::puts("the arrow's effect while the hero cannot pay");
    const double dt = 1.0 / 90.0;
    // starts lit; short for less than the dwell: still lit; for the dwell: out
    {
        Gate g;
        double t = 10.0;
        EXPECT(!g.Step(false, t), "starts lit");
        bool out = false;
        for (int i = 0; i < 20; ++i) { t += dt; out = out || g.Step(true, t); }   // 0.22 s
        EXPECT(!out && !g.Out(), "short for 0.22 s: still lit (a read can show a shortness for a moment right after a cast)");
        for (int i = 0; i < 20; ++i) { t += dt; out = g.Step(true, t); }           // 0.44 s in all
        EXPECT(out && g.Out(), "short for 0.44 s: out");
        // back: able for less than the dwell: still out; for the dwell: lit
        bool lit = false;
        for (int i = 0; i < 20; ++i) { t += dt; lit = lit || !g.Step(false, t); }
        EXPECT(!lit && g.Out(), "able for 0.22 s: still out");
        for (int i = 0; i < 20; ++i) { t += dt; lit = !g.Step(false, t); }
        EXPECT(lit && !g.Out(), "able for 0.44 s: lit again");
    }
    // flicker at the edge: the shortness comes and goes every 0.1 s - never believed
    {
        Gate g;
        double t = 5.0;
        bool ever = false;
        for (int i = 0; i < 400; ++i) { t += 0.1; ever = ever || g.Step(i % 2 == 0, t); }
        EXPECT(!ever, "a mana that flips every 0.1 s never puts the effect out");
        // ... and once out, a flip back for 0.1 s does not light it
        Gate h;
        t = 5.0;
        for (int i = 0; i < 20; ++i) { t += 0.05; h.Step(true, t); }
        EXPECT(h.Out(), "out after a second of shortness");
        t += 0.1; h.Step(false, t);
        t += 0.1; h.Step(true, t);
        t += 0.1; h.Step(false, t);
        EXPECT(h.Out(), "a short able-again blip does not light it");
    }
    // the shortness that stops at 0.29 s (the dwell is 0.30) never counted
    {
        Gate g;
        const double t = 1.0;
        g.Step(false, t);
        for (int i = 1; i <= 29; ++i) g.Step(true, t + i * 0.01);
        EXPECT(!g.Out(), "short for 0.29 s: not out");
        g.Step(false, t + 0.30);
        for (int i = 1; i <= 29; ++i) g.Step(true, t + 0.30 + i * 0.01);
        EXPECT(!g.Out(), "... and the count starts again after an able frame");
    }
    // exactly the dwell counts
    {
        Gate g;
        g.Step(false, 1.0);
        g.Step(true, 2.0);
        EXPECT(!g.Out(), "the first short frame starts the count");
        g.Step(true, 2.0 + kDwellS - 1e-3);
        EXPECT(!g.Out(), "a millisecond short of the dwell: not yet");
        g.Step(true, 2.0 + kDwellS + 1e-6);
        EXPECT(g.Out(), "the dwell: out");
    }
    // a gap (frames stopped) or time going back forgets what was building up
    {
        Gate g;
        g.Step(false, 1.0);
        g.Step(true, 1.1);
        g.Step(true, 5.0);   // a gap of seconds: the count starts over from here
        EXPECT(!g.Out(), "a gap does not count as shortness");
        g.Step(true, 5.0 + kDwellS + 0.01);
        EXPECT(g.Out(), "... the count after it does");
        Gate h;
        h.Step(false, 10.0);
        h.Step(true, 10.2);
        h.Step(true, 9.0);   // back in time
        h.Step(true, 9.2);
        EXPECT(!h.Out(), "a time that went back starts the count over");
        h.Step(true, 9.35);
        EXPECT(h.Out(), "... and the new count is the one that counts (9.0 + the dwell)");
        // a time that is not one is ignored: the count that was building goes on
        Gate n;
        n.Step(false, 0.9);
        n.Step(true, 1.0);
        n.Step(true, NAN);
        EXPECT(!n.Out(), "a NaN time counts for nothing");
        n.Step(true, 1.31);
        EXPECT(n.Out(), "... and does not forget the count that was building (1.0 + the dwell)");
    }
    // Reset: lit again at once
    {
        Gate g;
        g.Step(false, 1.0);
        g.Step(true, 1.1);
        g.Step(true, 1.5);
        EXPECT(g.Out(), "out");
        g.Reset();
        EXPECT(!g.Out() && !g.Step(false, 2.0), "reset: lit");
    }
}

void TestEase() {
    std::puts("the fade");
    // a full swing takes kFadeS (a short fade, not a switch and not a slow one), in steps of dt / kFadeS, never past the target
    EXPECT(kFadeS >= 0.1 && kFadeS <= 0.4, "the fade is a fifth of a second or so (%.2f)", kFadeS);
    EXPECT(kDwellS >= 0.2 && kDwellS <= 0.5, "the dwell is a few tenths of a second (%.2f)", kDwellS);
    const double dt = 1.0 / 90.0;
    float v = 1.0f;
    double t = 0.0;
    int frames = 0;
    while (v > 0.0f && frames < 1000) { v = Ease(v, 0.0f, dt); t += dt; ++frames; }
    EXPECT(std::fabs(t - kFadeS) < 2.0 * dt && v == 0.0f, "1 to 0 in %.3f s (%d frames)", t, frames);
    float w = 0.0f;
    t = 0.0;
    frames = 0;
    while (w < 1.0f && frames < 1000) { w = Ease(w, 1.0f, dt); t += dt; ++frames; }
    EXPECT(std::fabs(t - kFadeS) < 2.0 * dt && w == 1.0f, "0 to 1 in %.3f s", t);
    // monotonic and no overshoot, from anywhere
    float u = 0.37f;
    float last = u;
    bool mono = true;
    for (int i = 0; i < 100; ++i) { u = Ease(u, 0.9f, dt); mono = mono && u >= last && u <= 0.9f; last = u; }
    EXPECT(mono && u == 0.9f, "toward 0.9: steady, never past it");
    // a big step is a full swing, not past the target; a time that is not one leaves it
    EXPECT(Ease(1.0f, 0.0f, 10.0) == 0.0f && Ease(0.2f, 1.0f, 10.0) == 1.0f, "a long gap: to the target");
    EXPECT(Ease(0.4f, 0.0f, 0.0) == 0.4f && Ease(0.4f, 0.0f, -1.0) == 0.4f && Ease(0.4f, 0.0f, NAN) == 0.4f, "no time, a time that went back, NaN: unchanged");
    EXPECT(Ease(0.5f, 0.5f, dt) == 0.5f, "at the target: stays");
}

}  // namespace

int main() {
    TestCost();
    TestShort();
    TestEstimate();
    TestGate();
    TestEase();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
