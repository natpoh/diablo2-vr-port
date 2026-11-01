// vr/grab_move.h: [weight] grab_moves_unit - from the drawn body and the game's to the cells apart (the height's push taken off), when the game's knockback
// is asked, the cell it is sent to (held to the step's length, the direction kept), which knock-down phases move a unit and what the unit's mode says after.
#include <cmath>
#include <cstdio>

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

    // -- which phases move the unit (shared/d2r_vr_fall.h: 1 falling, 2 lying, 3 getting up, 4 back, 5 held, 6 staggering, 7 walking home)
    Check(PhaseMoves(5) && PhaseMoves(1) && PhaseMoves(2) && PhaseMoves(3), "held, falling (thrown), lying, getting up: moved");
    Check(!PhaseMoves(0) && !PhaseMoves(4) && !PhaseMoves(6) && !PhaseMoves(7), "none, back, staggering, walking home: not");

    // -- the unit's mode
    Check(ModeAllows(1) && ModeAllows(2) && ModeAllows(3), "standing, walking, attacking: a knockback may be asked");
    Check(!ModeAllows(0) && !ModeAllows(12) && !ModeAllows(13), "dead, dying, in a knockback already: not");
    Check(Judge(13) == Outcome::Runs && Judge(0) == Outcome::Dead && Judge(12) == Outcome::Dead && Judge(1) == Outcome::Refused, "after the call: runs, dead, refused");

    std::printf("grab_move_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
