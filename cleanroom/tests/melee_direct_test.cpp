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
    // ---- [gameplay] melee_direct_by_hand: which blow is the game's "Left Hand Swing" ----
    CHECK(LeftSwing(true, true, 1, false));      // the left-hand weapon's blade, a weapon in each hand: the left swing
    CHECK(!LeftSwing(true, true, 0, false));     // the right-hand weapon's blade: the plain attack (the right weapon)
    CHECK(!LeftSwing(true, false, 1, false));    // one weapon (or a two-handed one): as before
    CHECK(!LeftSwing(false, true, 1, false));    // the tick off: always the right weapon, as before
    CHECK(!LeftSwing(true, true, 1, true));      // a joined player's extra hit: its hand is not known here
    CHECK(!LeftSwing(true, true, -1, false));    // the slot not known
    CHECK(kSkillLeftHandSwing == 5 && kWeapselLeft == 1);
    // the node found must be the game's "Left Hand Swing"
    CHECK(IsLeftHandSwing(5, 1));
    CHECK(!IsLeftHandSwing(0, 0) && !IsLeftHandSwing(5, 0) && !IsLeftHandSwing(4, 1));   // Attack; a wrong weapsel; Left Hand Throw
    // the layout, proven from the game's weapon getter (3.3.93787's bytes)
    {
        const uint8_t mov[7] = {0x48, 0x8B, 0x8F, 0x00, 0x01, 0x00, 0x00};
        const uint8_t getter[13] = {0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x41, 0x18, 0xC3};
        const uint8_t weapsel[7] = {0x0F, 0xB6, 0x90, 0x08, 0x02, 0x00, 0x00};
        CHECK(LayoutProven(mov, 0xE8, getter, weapsel));
        CHECK(!LayoutProven(mov, 0xE9, getter, weapsel));   // not a call
        uint8_t m2[7]; for (int i = 0; i < 7; ++i) m2[i] = mov[i]; m2[3] = 0x08;   // another offset of the skill list
        CHECK(!LayoutProven(m2, 0xE8, getter, weapsel));
        uint8_t g2[13]; for (int i = 0; i < 13; ++i) g2[i] = getter[i]; g2[11] = 0x10;   // the current skill elsewhere
        CHECK(!LayoutProven(mov, 0xE8, g2, weapsel));
        uint8_t w2[7]; for (int i = 0; i < 7; ++i) w2[i] = weapsel[i]; w2[3] = 0x10;   // the weapsel column elsewhere
        CHECK(!LayoutProven(mov, 0xE8, getter, w2));
        CHECK(kUnitSkillList == 0x100 && kListCurrentSkill == 0x18 && kRowWeapsel == 0x208);
    }
    // ---- the press is a skill: the key path (B1: an armed skill in Full; B2: a skill on the attack slot), in both modes ----
    {
        Gate g;
        g.wanted = g.ready = true;
        g.nowMs = 1000;
        CHECK(Decide(g) == Route::Direct);           // nothing armed, the plain attack on A: the direct hit
        g.armed = true;
        CHECK(Decide(g) == Route::Key);              // B1: an armed melee skill - the game's cast (its mana, its damage)
        g.armed = false;
        g.slotPlain = false;
        CHECK(Decide(g) == Route::Key);              // B2: Zeal / Bash / a spell on A - the game casts it
        g.armed = true;
        CHECK(Decide(g) == Route::Key);
        g.f4 = false;
        CHECK(Decide(g) == Route::Skip);             // another view: still nothing for a touch
        g.f4 = true;
        g.armed = false;
        g.slotPlain = true;
        g.minIntervalMs = 100;
        g.lastMs = 950;
        CHECK(Decide(g) == Route::Skip);             // the interval still holds for a plain touch
        g.armed = true;
        CHECK(Decide(g) == Route::Key);              // an armed touch is never swallowed by the interval: the game's cast
        g.wanted = false;
        CHECK(Decide(g) == Route::Key);              // off: the key, as ever
    }
    // ---- the attack slot's read: plain only when read, the basic attack, and fresh ----
    {
        CHECK(SlotIsPlain(true, kSkillAttack, 1000, 900));
        CHECK(SlotIsPlain(true, 0, 1900, 900));                    // exactly kSlotFreshMs old
        CHECK(!SlotIsPlain(true, 0, 1901, 900));                   // too old: not trusted - the key path
        CHECK(!SlotIsPlain(false, 0, 1000, 900));                  // the read failed
        CHECK(!SlotIsPlain(true, 0, 1000, 0));                     // never read
        CHECK(!SlotIsPlain(true, 106, 1000, 900));                 // Zeal (106) on A
        CHECK(!SlotIsPlain(true, 126, 1000, 900));                 // Bash (126) on A
        CHECK(!SlotIsPlain(true, 1, 1000, 900));                   // Kick
        CHECK(!SlotIsPlain(true, -1, 1000, 900));                  // nothing read into it
        CHECK(!SlotIsPlain(true, 0, 800, 900));                    // a clock that stepped back: not fresh
        CHECK(kSlotLookMs < kSlotFreshMs);                         // the look comes often enough to stay fresh
    }
    std::printf("melee_direct_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
