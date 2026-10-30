// health_test - vr/health.h: [debug] health's counters, the rule for the mark and the line.
//   1. off: nothing is counted, no line;
//   2. the body chain: pushes given back as poses - a plain line; pushes and no pose given - the mark (0.188.6: NPCs pushed, never drawn turned);
//   3. the sky: one drawer each - a plain line; our sky and the game's dome in one present - the mark, and the same for the fog and the ceiling;
//   4. a window is taken once: the next one starts from 0, and a feature that did nothing writes no line.
#include "health.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

int main() {
    // 1. off
    health::SetOn(false);
    {
        health::Body b;
        b.pushes.Add(5);
        b.posesAsked.Add(10);
        CHECK(b.Take().empty());
        health::Sky s;
        s.Present(true, true, false, false, false, false);
        CHECK(s.Take().empty());
    }
    health::SetOn(true);
    // 2. the body chain
    {
        health::Body b;
        b.pushes.Add(5);
        b.posesAsked.Add(1180);
        b.posesGiven.Add(40);
        const std::string ok = b.Take();
        CHECK(Has(ok, "vrcam: health body: pushes 5 | poses asked 1180 | poses given 40"));
        CHECK(!Has(ok, "(!)"));
        b.pushes.Add(5);
        b.posesAsked.Add(1180);
        b.skinAsked.Add(300);
        const std::string bad = b.Take();
        CHECK(Has(bad, "pushes 5"));
        CHECK(Has(bad, "poses given 0"));
        CHECK(Has(bad, "(!) pushed, never given"));
        // given by the skinning alone is given
        b.pushes.Add(2);
        b.skinGiven.Add(3);
        CHECK(!Has(b.Take(), "(!)"));
        // 4. taken: nothing counted since, no line
        CHECK(b.Take().empty());
        // no push: asks alone are no broken chain
        b.posesAsked.Add(100);
        CHECK(!Has(b.Take(), "(!)"));
    }
    // 3. the sky
    {
        health::Sky s;
        for (int i = 0; i < 10; ++i) s.Present(false, true, true, false, false, true);   // the game's dome, our fog, the game's ceiling
        const std::string ok = s.Take();
        CHECK(Has(ok, "presents 10 | post sky 0 | game dome 10 | post fog 10 | game fog 0"));
        CHECK(Has(ok, "both skies 0 | both fogs 0 | both ceilings 0"));
        CHECK(!Has(ok, "(!)"));
        s.Present(true, true, false, false, false, false);
        CHECK(Has(s.Take(), "(!) two skies"));
        s.Present(false, false, true, true, false, false);
        CHECK(Has(s.Take(), "(!) two fogs"));
        s.Present(false, false, false, false, true, true);
        CHECK(Has(s.Take(), "(!) two ceilings"));
        CHECK(s.Take().empty());
    }
    // the line itself
    {
        const health::Item items[] = {{"a", 1}, {"b", 0}};
        CHECK(health::Line("x", items) == "vrcam: health x: a 1 | b 0");
        CHECK(health::Line("x", items, "why") == "vrcam: health x: a 1 | b 0 (!) why");
        const health::Item none[] = {{"a", 0}};
        CHECK(health::Line("x", none).empty());
        CHECK(!health::Line("x", none, "why").empty());   // a mark is told even with nothing counted
    }
    std::printf(g_fail ? "health_test: %d FAILED\n" : "health_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
