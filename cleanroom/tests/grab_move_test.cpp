// vr/grab_move.h: [weight] grab_moves_unit - from the drawn body and the game's to the cells apart (the height's push taken off), when the game's knockback
// is asked, the cell it is sent to (held to the step's length, the direction kept), which knock-down phases move a unit and what the unit's mode says after.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../../vr/grab_move.h"

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    using namespace grabmove;
    const float upm = 3.2f;   // render units a metre (the game's at a 1.75 m eye)

    // -- the cells apart: the same height - plain metres to cells (x -> x, z -> y; two world units a cell)
    {
        const float game[3] = {10.0f, 1.0f, 20.0f}, drawn[3] = {12.5f, 1.0f, 18.75f};
        float dx = 0.0f, dy = 0.0f;
        Check(CellsApart(drawn, game, upm, &dx, &dy), "apart: made");
        Check(Near(dx, 2.5f * upm / 2.0f) && Near(dy, -1.25f * upm / 2.0f), "apart: 2.5 m on x and -1.25 m on z are 4 and -2 cells");
    }
    // -- lifted in the hands straight up over its own cell: no cells apart (the height's push on x and z taken off)
    {
        const float game[3] = {10.0f, 1.0f, 20.0f};
        const float h = 0.8f;
        const float drawn[3] = {10.0f + h * kShear, 1.0f + h, 20.0f + h * kShear};
        float dx = 1.0f, dy = 1.0f;
        Check(CellsApart(drawn, game, upm, &dx, &dy) && Near(dx, 0.0f) && Near(dy, 0.0f), "lifted over its cell: 0 cells apart");
    }
    // -- lying on the ground 1.5 m ahead (its pelvis 0.8 m lower than standing): only the 1.5 m counts
    {
        const float game[3] = {0.0f, 0.9f, 0.0f};
        const float drawn[3] = {1.5f - 0.8f * kShear, 0.1f, -0.8f * kShear};
        float dx = 0.0f, dy = 0.0f;
        Check(CellsApart(drawn, game, upm, &dx, &dy) && Near(dx, 1.5f * upm / 2.0f) && Near(dy, 0.0f), "lying ahead: its cell, not its height");
    }
    {
        const float a[3] = {0, 0, 0}, nan3[3] = {NAN, 0, 0};
        float dx = 0.0f, dy = 0.0f;
        Check(!CellsApart(nan3, a, upm, &dx, &dy) && !CellsApart(a, a, 0.0f, &dx, &dy) && !CellsApart(nullptr, a, upm, &dx, &dy), "apart: bad input refused");
    }

    // -- when it is asked
    Check(!Wants(1.9f, -1.9f, 1000, 0), "under two cells on both axes: not asked");
    Check(Wants(2.0f, 0.0f, 1000, 0) && Wants(0.0f, -3.0f, 1000, 0), "two cells on either axis: asked");
    Check(!Wants(5.0f, 0.0f, 1000, 900), "asked again within the gap: not");
    Check(Wants(5.0f, 0.0f, 1000 + kGapMs, 1000), "after the gap: asked");
    Check(Wants(5.0f, 0.0f, 500, 900), "a clock that went back: asked");
    Check(!Wants(NAN, 9.0f, 1000, 0), "not a number: not asked");

    // -- the cell it goes to
    {
        int tx = 0, ty = 0;
        Check(Target(5000, 4000, 3.4f, -2.6f, &tx, &ty) && tx == 5003 && ty == 3997, "the nearest cell to the drawn body");
        Check(Target(5000, 4000, 60.0f, 30.0f, &tx, &ty) && tx == 5000 + (int)kMaxStepCells && ty == 4000 + (int)(kMaxStepCells / 2), "a long throw: one step's length, the direction kept");
        Check(!Target(5000, 4000, 0.4f, -0.3f, &tx, &ty), "under half a cell: no move");
        Check(Target(2, 2, -10.0f, -10.0f, &tx, &ty) && tx == 1 && ty == 1, "held to the map's edge");
        Check(!Target(5000, 4000, NAN, 1.0f, &tx, &ty), "not a number: nothing");
    }

    // -- the hero in the way (0.195.1, Serg's log): the cases as they came
    {
        const auto dist = [](float hx, float hy, int x, int y) { return std::hypot(x - hx, y - hy); };
        // monster 28 on 5008,6192 asked to 5005,6193, the hero on 5007,6194 (world 10014.7, 12387.4 / 2): across him - moved +0,+0
        int tx = 5005, ty = 6193;
        Check(ClearOfHero(5007.0f, 6194.0f, 5008, 6192, &tx, &ty), "across the hero: still a cell to go to");
        Check(dist(5007.0f, 6194.0f, tx, ty) >= kHeroClearCells - 0.75f, "across the hero: the cell asked is clear of him");
        Check((tx - 5008) * (5007 - 5008) + (ty - 6192) * (6194 - 6192) <= 0 || dist(5007.0f, 6194.0f, tx, ty) >= 2.0f, "across the hero: not into him");
        // "NPC 100" (a fallen) on 4614,5736, asked to 4613,5734, the hero on 4614,5736 itself (9227 / 2, 11471 / 2)
        tx = 4613; ty = 5734;
        Check(ClearOfHero(4613.5f, 5735.5f, 4614, 5736, &tx, &ty) && dist(4613.5f, 5735.5f, tx, ty) >= kHeroClearCells - 0.75f, "on the hero's own cell: out of him");
        // monster 59 on 4914,6236 asked to 4915,6238, the hero on 4914,6235 (it landed): away from him - as asked
        tx = 4915; ty = 6238;
        Check(ClearOfHero(4914.0f, 6235.0f, 4914, 6236, &tx, &ty) && tx == 4915 && ty == 6238, "away from the hero: as asked");
        // monster 89 on 4971,5946 asked to 4974,5948, the hero on 4972,5950 (it landed): sideways - as asked
        tx = 4974; ty = 5948;
        Check(ClearOfHero(4971.75f, 5950.2f, 4971, 5946, &tx, &ty) && tx == 4974 && ty == 5948, "sideways past the hero, clear of him: as asked");
        // a cell asked behind the hero, the unit 3 cells in front: round him a quarter turn at most, never through
        tx = 5000; ty = 4997;
        Check(ClearOfHero(5000.0f, 5000.0f, 5000, 5003, &tx, &ty), "behind him: a cell round him");
        Check(std::abs(ty - 5000) <= 1 && std::abs(tx - 5000) >= 2, "behind him: the step goes to his side (a quarter turn), not across");
        // no hero read: as asked
        tx = 10; ty = 10;
        Check(ClearOfHero(NAN, NAN, 5, 5, &tx, &ty) && tx == 10 && ty == 10, "no hero: as asked");
    }

    // -- which phases move the unit (shared/d2r_vr_fall.h: 1 falling, 2 lying, 3 getting up, 4 back, 5 held, 6 staggering, 7 walking home)
    Check(PhaseMoves(5) && PhaseMoves(1) && PhaseMoves(2) && PhaseMoves(3), "held, falling (thrown), lying, getting up: moved");
    Check(!PhaseMoves(0) && !PhaseMoves(4) && !PhaseMoves(6) && !PhaseMoves(7), "none, back, staggering, walking home: not");

    // -- the settle (Serg 2026-11-04: «иногда пытается вернуться на старое место»): at rest the unit is brought onto the body's own cell - a body 1.5 cells off a
    // unit the knockback left short was walked back to; held or flying, two cells as before
    Check(StepFor(5) == kStepCells && StepFor(1) == kStepCells, "held, falling: two cells");
    Check(StepFor(2) == kSettleCells && StepFor(3) == kSettleCells && StepFor(7) == kSettleCells, "lying, getting up, walking home: one cell");
    Check(!Wants(1.5f, 0.0f, 1000, 0) && !Wants(1.5f, 0.0f, 1000, 0, StepFor(1)), "1.5 cells off while it flies: not asked");
    Check(Wants(1.5f, 0.0f, 1000, 0, StepFor(2)) && Wants(0.0f, -1.0f, 1000, 0, StepFor(7)), "1.5 / 1 cells off lying or walking home: asked");
    Check(!Wants(0.9f, 0.9f, 1000, 0, StepFor(2)), "under a cell: not asked (its own cell)");
    Check(!Wants(1.5f, 0.0f, 1000, 900, StepFor(2)), "the settle keeps the gap");
    Check(!Wants(0.6f, 0.0f, 1000, 0, 0.2f) && Wants(1.0f, 0.0f, 1000, 0, 0.2f), "never under a cell whatever less is asked");
    Target(10, 10, 1.2f, 0.0f, nullptr, nullptr);   // (null outs: refused, no crash)
    {
        int tx = 0, ty = 0;
        Check(Target(10, 10, 1.2f, -0.4f, &tx, &ty) && tx == 11 && ty == 10, "the settle's target: the next cell");
    }

    // -- [weight] knock_down_moves_unit: what a blow knocks down moves (a fall, a sweep, a shove, a spear's carry), a stagger's step back never; a hand's take always;
    // a blow's few knockbacks only
    Check(KindMoves(false, 0) && KindMoves(false, 1) && KindMoves(false, 2), "a blow's knock-down, a sweep, a shove: moved");
    Check(!KindMoves(false, 3), "a stagger: not moved");
    Check(KindMoves(true, 3) && KindMoves(true, 0), "taken by a hand: moved whatever it was");
    Check(kKnockMoves >= 2 && kKnockMoves <= 4, "a blow: where it lands and where it settles, a few knockbacks only");

    // -- the unit's mode
    Check(ModeAllows(1) && ModeAllows(2) && ModeAllows(3), "standing, walking, attacking: a knockback may be asked");
    Check(!ModeAllows(0) && !ModeAllows(12) && !ModeAllows(13), "dead, dying, in a knockback already: not");
    Check(Judge(13) == Outcome::Runs && Judge(0) == Outcome::Dead && Judge(12) == Outcome::Dead && Judge(1) == Outcome::Refused, "after the call: runs, dead, refused");

    std::printf("grab_move_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
