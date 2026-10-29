// Offline checks of vr/stand_still.h: which presses want the game's Stand Still, and the wait for the command before an
// attack goes out. No game, no clock: the time is handed in. Run: build/Release/stand_still_test.exe (exit code 0 = passed)

#include <cstdio>
#include <initializer_list>

#include "../../vr/stand_still.h"

using namespace standstill;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestWhich() {
    CHECK(SkillWhy(-1) == Why::UnknownSkill);
    CHECK(SkillWhy(0) == Why::Held);              // the basic attack
    CHECK(SkillWhy(6) == Why::Held);              // Magic Arrow
    CHECK(SkillWhy(97) == Why::Held);             // Smite: stands and hits what is in reach
    CHECK(SkillWhy(357) == Why::Interact);
    CHECK(SkillWhy(54) == Why::Mover);            // Teleport
    CHECK(SkillWhy(107) == Why::Mover);           // Charge
    CHECK(SkillWhy(132) == Why::Mover);           // Leap
    CHECK(SkillWhy(143) == Why::Mover);           // Leap Attack
    CHECK(SkillWhy(151) == Why::Mover);           // Whirlwind
    CHECK(SkillWhy(275) == Why::Mover);           // Dragon Flight
    CHECK(SkillWhy(126) == Why::Held);            // Bash
}

// One poll every 8 ms from `t0`; `wanted` per poll; the game's command is down from `downAt` on (or never: -1).
struct Run {
    Machine m;
    uint64_t t = 1000;
    int64_t downAt = -1;
    bool delivered = false, timedOut = false;
    uint32_t waited = 0;
    int deliverPolls = 0, holdPolls = 0, waitedPolls = 0;
    Out last;
    Out Poll(bool wanted) {
        const bool confirmed = downAt >= 0 && (int64_t)t >= downAt;
        last = m.Step(wanted, confirmed, t);
        if (last.delivered) { delivered = true; waited = last.waitedMs; timedOut = last.timedOut; }
        if (last.deliver) ++deliverPolls;
        if (last.holdCommand) ++holdPolls;
        if (wanted && !last.deliver) ++waitedPolls;
        t += 8;
        return last;
    }
};

static void TestWaitsForTheCommand() {
    Run r;
    r.downAt = 1020;                                   // the game has the command down 20 ms after the press began
    Out o = r.Poll(true);
    CHECK(o.started && o.holdCommand && !o.deliver);   // 1000: asked for, held back
    o = r.Poll(true);
    CHECK(o.holdCommand && !o.deliver);                // 1008
    o = r.Poll(true);
    CHECK(!o.deliver);                                 // 1016
    o = r.Poll(true);                                  // 1024: down
    CHECK(o.deliver && o.delivered && !o.timedOut && o.waitedMs == 24);
    for (int i = 0; i < 10; ++i) o = r.Poll(true);
    CHECK(o.deliver && o.holdCommand && !o.started);   // held while pressed
}

static void TestNeverComes() {
    Run r;                                             // the command never comes down (the executor is not found)
    int firstDeliver = -1;
    for (int i = 0; i < 20 && firstDeliver < 0; ++i) { if (r.Poll(true).deliver) firstDeliver = i; }
    CHECK(firstDeliver > 0 && r.timedOut && r.waited >= kWaitMs && r.waited < kWaitMs + 8);   // the wait ran out: the press goes
    CHECK(!r.m.Degraded());
    // let go, a second press, again nothing: now it stops waiting
    for (int i = 0; i < 20; ++i) r.Poll(false);
    CHECK(!r.m.Busy());
    r.delivered = false;
    for (int i = 0; i < 20 && !r.delivered; ++i) r.Poll(true);
    CHECK(r.delivered && r.timedOut && r.m.Degraded());
    for (int i = 0; i < 20; ++i) r.Poll(false);
    r.delivered = false;
    const Out o = r.Poll(true);
    CHECK(o.deliver && o.delivered && o.waitedMs == 0 && o.holdCommand);   // no wait any more, the command is still asked for
    // when it does come, it is waited for again
    for (int i = 0; i < 20; ++i) r.Poll(false);
    r.downAt = (int64_t)r.t;
    r.Poll(true);
    CHECK(!r.m.Degraded());
}

static void TestShortPressIsStretched() {
    Run r;
    r.downAt = 1030;
    r.Poll(true);                                      // 1000
    r.Poll(false);                                     // 1008: the source let go before the command was down
    r.Poll(false);                                     // 1016
    r.Poll(false);                                     // 1024
    Out o = r.Poll(false);                             // 1032: down - the press is let through though the source is gone
    CHECK(o.deliver && o.delivered);
    int stretched = 1;
    for (int i = 0; i < 20; ++i) { o = r.Poll(false); if (o.deliver) ++stretched; else break; }
    CHECK(stretched * 8 >= (int)kMinPressMs && stretched * 8 <= (int)kMinPressMs + 16);   // at least its least length, then over
    CHECK(o.holdCommand);                              // the tail
}

static void TestTailAndAgain() {
    Run r;
    r.downAt = 1000;
    for (int i = 0; i < 12; ++i) r.Poll(true);         // a press, long
    Out o;
    int tail = 0;
    for (int i = 0; i < 20; ++i) { o = r.Poll(false); if (o.holdCommand) ++tail; else break; }
    CHECK(tail >= 2 && tail * 8 <= (int)kTailMs + 16 + 8);   // a short tail only
    CHECK(!o.holdCommand && !r.m.Busy());
    // a second press inside a tail goes straight through (the command is still down)
    Run q;
    q.downAt = 1000;
    for (int i = 0; i < 12; ++i) q.Poll(true);
    q.Poll(false);                                     // the tail begins
    o = q.Poll(true);
    CHECK(o.deliver && o.started && o.holdCommand);
}

static void TestResetLetsGo() {
    Run r;
    r.downAt = 1000;
    for (int i = 0; i < 5; ++i) r.Poll(true);
    CHECK(r.m.Busy());
    r.m.Reset();                                       // the view left F4, the switch went off
    CHECK(!r.m.Busy());
    const Out o = r.Poll(false);
    CHECK(!o.holdCommand && !o.deliver);
}

static void TestNothingWithoutAPress() {
    Run r;
    for (int i = 0; i < 50; ++i) { const Out o = r.Poll(false); CHECK(!o.holdCommand && !o.deliver && !o.started); }
}

static void TestClicks() {
    // the click types: 0 / 1 the left button pressed / held, 3 / 4 the right
    for (int type : {0, 1, 3, 4}) CHECK(ClickCandidate(true, true, false, true, type, 0));
    // the controller's own types (6 stop, 7 .. 10 casts), and anything else, are not the mouse's
    for (int type : {2, 5, 6, 7, 8, 9, 10, 11, -1}) CHECK(!ClickCandidate(true, true, false, true, type, 0));
    // F4 only, the switch, no menu, a hero
    CHECK(!ClickCandidate(false, true, false, true, 0, 0));    // [gameplay] attack_in_place=0: as it was
    CHECK(!ClickCandidate(true, false, false, true, 0, 0));    // F1, F2, F3, flat: a click on the floor walks (F3: it must)
    CHECK(!ClickCandidate(true, true, true, true, 0, 0));      // a panel is open
    CHECK(!ClickCandidate(true, true, false, false, 0, 0));    // no hero unit
    // a click that already has a flag (Shift held by the player, a shooter's flag) is left as it is
    CHECK(!ClickCandidate(true, true, false, true, 0, 1));
    CHECK(!ClickCandidate(true, true, false, true, 3, 0x20));
    CHECK(ClickCandidate(true, true, false, true, 0, 0x100));  // (the high byte is Run: not Stand Still)
    // bare ground only: a monster, an item, a door or an NPC under the pointer is the game's own click
    CHECK(ClickStill(true, false));
    CHECK(!ClickStill(true, true));
    CHECK(!ClickStill(false, false));
}

int main() {
    TestClicks();
    TestWhich();
    TestWaitsForTheCommand();
    TestNeverComes();
    TestShortPressIsStretched();
    TestTailAndAgain();
    TestResetLetsGo();
    TestNothingWithoutAPress();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("stand_still_test: all %d checks passed\n", g_checks);
    return 0;
}
