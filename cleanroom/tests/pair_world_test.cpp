// Is the pair about to be drawn one of the game's world (vr/pair_world.h)? Timelines of pairs and of the world camera's follow, in
// milliseconds: no game, no headset.
//
//   pair_world_test        (exit code 0 = all passed)

#include <cstdint>
#include <cstdio>
#include <vector>

#include "pair_world.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace pairworld;

// A game drawn pair after pair: each pair begins `game` ms after the last one ended, its left pass takes `left`, its right `right`;
// the follow runs at the start of each pass while `inGameUntil` (ms) has not passed. d2rcam::InWorld is the follow within 1000 ms.
// Returns, for each pair, whether it was taken for the world.
struct Pass { uint64_t game, left, right; };
static std::vector<Why> Run(const std::vector<Pass>& pairs, bool slow, uint64_t inGameUntil = ~0ull) {
    Gate g;
    uint64_t t = 1000, lastSync = 0;
    std::vector<Why> out;
    for (const Pass& p : pairs) {
        t += p.game;
        const bool fresh = lastSync != 0 && t - lastSync < 1000;
        out.push_back(g.Begin(t, fresh, lastSync, slow));
        if (t < inGameUntil) lastSync = t;            // the left pass's follow
        t += p.left;
        if (t < inGameUntil) lastSync = t;            // the right pass's follow
        t += p.right;
    }
    return out;
}

static void TestFastPairs() {
    // 90 pairs a second: every pair fresh, the second rule never asked - as before, bit for bit
    std::vector<Pass> v(200, Pass{1, 5, 5});
    for (const bool slow : {true, false}) {
        const std::vector<Why> w = Run(v, slow);
        bool all = true;
        for (size_t i = 1; i < w.size(); ++i) all = all && w[i] == Why::Fresh;
        CHECK(all);
        CHECK(w[0] == Why::No);   // before the first follow: no world yet
    }
}

static void TestSlowCard() {
    // the player's log (0.174, a GTX 1650 SUPER 4 GB, Quest 2 over Link): pairs of 1.2 - 1.8 s - a right pass and the game's frame of a
    // second or more before the next pair
    const std::vector<Pass> v = {{44, 445, 81}, {120, 802, 950}, {120, 847, 844}, {122, 237, 883}, {134, 399, 979},
                                 {199, 206, 1009}, {74, 551, 944}, {7, 534, 489}, {1, 428, 415}, {1, 97, 67}, {1, 6, 6246}, {1, 50, 50}};
    const std::vector<Why> before = Run(v, false);
    int noBefore = 0;
    for (size_t i = 1; i < before.size(); ++i) noBefore += before[i] == Why::No;
    CHECK(noBefore >= 5);   // as 0.174 had it: every other pair or so "no world" - its right eye the flat picture
    CHECK(before[2] == Why::No && before[4] == Why::No && before[5] == Why::No && before[6] == Why::No);
    const std::vector<Why> now = Run(v, true);
    bool all = true;
    for (size_t i = 1; i < now.size(); ++i) all = all && World(now[i]);
    CHECK(all);   // with slow_pair_world: every one of them stereo
    CHECK(now[2] == Why::SincePrevious && now[11] == Why::SincePrevious);   // after the 6.2 s pass too
    CHECK(now[9] == Why::Fresh);   // a quick pair is fresh as ever
}

static void TestGameLeft() {
    // fast pairs, the game left at 3 s: still the world for the second the follow is fresh, then not - as before
    {
        std::vector<Pass> v(600, Pass{1, 5, 5});
        const std::vector<Why> w = Run(v, true, 3000);
        size_t firstNo = 0;
        for (size_t i = 1; i < w.size(); ++i)
            if (w[i] == Why::No) { firstNo = i; break; }
        CHECK(firstNo != 0);
        bool stays = true;
        for (size_t i = firstNo; i < w.size(); ++i) stays = stays && w[i] == Why::No;
        CHECK(stays);
        const std::vector<Why> old = Run(v, false, 3000);
        CHECK(old == w);   // fast: the second rule changes nothing
    }
    // slow pairs (1.5 s each: pair k begins at 1100 + 1500 k, its follows at its begin and 700 ms later), the game left at 9000 ms -
    // inside pair 5, after its left pass's follow (8600): pair 6 is still taken (the follow ran in pair 5), pair 7 and on are not
    {
        std::vector<Pass> v(12, Pass{100, 700, 700});
        const std::vector<Why> w = Run(v, true, 9000);
        CHECK(w[5] == Why::Fresh);           // 800 ms after pair 4's right follow
        CHECK(w[6] == Why::SincePrevious);   // 1500 ms after pair 5's left follow, which ran in pair 5
        CHECK(w[7] == Why::No && w[8] == Why::No && w[11] == Why::No);
        const std::vector<Why> old = Run(v, false, 9000);
        CHECK(old[6] == Why::No && old[7] == Why::No);   // before: the game left was seen a pair sooner
        CHECK(old[2] == Why::Fresh);   // (passes of 700 ms: 800 ms from the right pass's follow to the next pair - fresh either way)
    }
}

static void TestGateBasics() {
    Gate g;
    CHECK(g.Begin(100, false, 50, true) == Why::No);           // no previous pair: only the fresh rule
    CHECK(g.Begin(200, false, 150, true) == Why::SincePrevious);
    CHECK(g.Begin(300, false, 150, true) == Why::No);          // no follow since the pair at 200
    CHECK(g.Begin(400, false, 0, true) == Why::No);            // no camera (0)
    CHECK(g.Begin(500, false, 450, false) == Why::No);         // slow_pair_world off: as before
    CHECK(g.Begin(600, false, 700, true) == Why::No);          // a follow said to be later than now: not believed
    CHECK(g.Begin(700, true, 0, false) == Why::Fresh);         // fresh is fresh, either way
    CHECK(g.prevBegin == 700);
}

int main() {
    TestGateBasics();
    TestFastPairs();
    TestSlowCard();
    TestGameLeft();
    std::printf("pair_world_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
