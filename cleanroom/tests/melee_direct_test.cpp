// melee_direct_test.cpp - the pure part of "Direct hit" (vr/melee_direct.h): which route a touch takes, what the units found on
// the game's thread allow, and which askers the two hooks answer. No game, no headset.
//   melee_direct_test.exe
#include "melee_direct.h"

#include <cstdio>
#include <initializer_list>

using namespace meleedirect;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Found Good() {
    Found f;
    f.player = true;
    f.playerMode = 1;
    f.monster = true;
    f.monsterMode = 1;
    f.hostile = true;
    f.wall = false;
    return f;
}

int main() {
    // ---- the route ----
    {
        Gate g;
        g.nowMs = 1000;
        CHECK(Decide(g) == Route::Key);            // off: the key, as today
        g.wanted = true;
        CHECK(Decide(g) == Route::Key);            // on but not ready (no server, a signature missing, a fault): the key
        g.ready = true;
        CHECK(Decide(g) == Route::Direct);
        g.ready = false;
        g.wanted = false;
        g.ready = true;
        CHECK(Decide(g) == Route::Key);            // ready alone changes nothing
    }
    // ---- F4 only: another view does nothing for a touch, whatever else is set ----
    {
        Gate g;
        g.nowMs = 1000;
        g.f4 = false;
        CHECK(Decide(g) == Route::Skip);
        g.wanted = true;
        CHECK(Decide(g) == Route::Skip);
        g.ready = true;
        CHECK(Decide(g) == Route::Skip);           // the option ticked and ready: still nothing outside F4
        g.f4 = true;
        CHECK(Decide(g) == Route::Direct);         // back in F4: as before
        g.wanted = false;
        CHECK(Decide(g) == Route::Key);
    }
    // ---- the minimum interval ----
    {
        Gate g;
        g.wanted = true;
        g.ready = true;
        g.minIntervalMs = 0;
        g.lastMs = 990;
        g.nowMs = 1000;
        CHECK(Decide(g) == Route::Direct);         // 0 = none beyond the detector's own rule
        g.minIntervalMs = 250;
        CHECK(Decide(g) == Route::Skip);           // 10 ms after the last
        g.nowMs = 1239;
        CHECK(Decide(g) == Route::Skip);
        g.nowMs = 1240;
        CHECK(Decide(g) == Route::Direct);         // exactly the interval later
        g.lastMs = 0;
        g.nowMs = 5;
        CHECK(Decide(g) == Route::Direct);         // no direct hit yet: nothing to wait for
        g.lastMs = 2000;
        g.nowMs = 1000;
        CHECK(Decide(g) == Route::Direct);         // a clock that stepped back is not a reason to skip for ever
    }
    // ---- what the units allow ----
    {
        Found f = Good();
        CHECK(Check(f) == Verdict::Go);
        f.player = false;
        CHECK(Check(f) == Verdict::NoPlayer);
        for (uint32_t mode : {0u, 17u, 5u, 6u}) {
            f = Good();
            f.playerMode = mode;
            CHECK(Check(f) == Verdict::PlayerMode);
        }
        for (uint32_t mode : {1u, 2u, 3u, 4u, 7u, 8u, 10u, 11u, 16u}) {   // walking, attacking, hit recovery... all allowed (the game's own gate is skipped on purpose)
            f = Good();
            f.playerMode = mode;
            CHECK(Check(f) == Verdict::Go);
        }
        f = Good();
        f.monster = false;
        CHECK(Check(f) == Verdict::NoMonster);
        for (uint32_t mode : {0u, 12u}) {
            f = Good();
            f.monsterMode = mode;
            CHECK(Check(f) == Verdict::MonsterDying);
        }
        f = Good();
        f.hostile = false;
        CHECK(Check(f) == Verdict::NotHostile);
        f = Good();
        f.wall = true;
        CHECK(Check(f) == Verdict::Wall);
        f = Good();
        f.hostile = false;
        f.wall = true;
        CHECK(Check(f) == Verdict::NotHostile);    // the first reason is the one told
    }
    // ---- the hooks' windows ----
    {
        Window none;
        CHECK(!none.active());
        CHECK(!AnswersTarget(none, 1, 2, 0x100, 0x100, 0x200));
        CHECK(!AnswersRange(none, 2, 3));
        Window w;
        w.game = 1;
        w.attacker = 2;
        w.defender = 3;
        CHECK(w.active());
        CHECK(AnswersTarget(w, 1, 2, 0x100, 0x100, 0x200));      // the first call site
        CHECK(AnswersTarget(w, 1, 2, 0x200, 0x100, 0x200));      // the second
        CHECK(!AnswersTarget(w, 1, 2, 0x300, 0x100, 0x200));     // any other asker: the game's own answer
        CHECK(!AnswersTarget(w, 9, 2, 0x100, 0x100, 0x200));     // another game
        CHECK(!AnswersTarget(w, 1, 5, 0x100, 0x100, 0x200));     // another unit
        CHECK(AnswersRange(w, 2, 3));
        CHECK(!AnswersRange(w, 3, 2));                            // the pair, in that order
        CHECK(!AnswersRange(w, 2, 4));
        Window half;
        half.game = 1;
        half.attacker = 2;                                        // no defender yet: nothing is answered
        CHECK(!AnswersTarget(half, 1, 2, 0x100, 0x100, 0x200));
        CHECK(!AnswersRange(half, 2, 3));
    }
    // ---- names (the log tells why a touch was not struck) ----
    CHECK(Name(Verdict::Go)[0] == 'g');
    CHECK(Name(Verdict::Wall)[0] == 'a');
    std::printf("melee_direct_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
