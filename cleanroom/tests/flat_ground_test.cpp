// [input] flat_aim_ground (vr/flat_ground.h): the flat mouse look's point on raised ground. A hero walks from base ground up onto a ledge and down
// again; a monster stands a few cells ahead on his floor; the crosshair is on its feet. The game's mouse ground (world 1) as the base plane its own
// view needs: its cell is right on base ground and wrong on the ledge; ours (the floor's cell) is right everywhere; along the game's own view both agree.
//   flat_ground_test [-v]
#include "flat_ground.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static int g_fail = 0;
static bool g_verbose = false;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

using namespace flatground;

static void Normalize(float d[3]) {
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; ++i) d[i] /= l;
}

// One look: the hero on ground heroY at cell (hx, hz), his eyes eyeH over his drawn point (our first-person camera is the game's look-at, its
// drawn hero point, + ViewHeight); the crosshair on the feet of a monster at cell (tx, tz) on ground targetY. What the game's own pick gives
// (world 1 as the plane y = baseY) and what ours gives (the floor hit is the monster's drawn feet).
struct Look { float gameOff, oursOff, downDeg; bool gameOk; };
static Look LookAt(float hx, float hz, float heroY, float eyeH, float tx, float tz, float targetY, float baseY = 0.0f) {
    float hero[3], feet[3];
    Drawn(hx, hz, heroY, hero);
    Drawn(tx, tz, targetY, feet);
    const float eye[3] = {hero[0], hero[1] + eyeH, hero[2]};
    float d[3] = {feet[0] - eye[0], feet[1] - eye[1], feet[2] - eye[2]};
    Normalize(d);
    float game[3] = {NAN, NAN, NAN};
    const bool gameOk = PlaneHit(eye, d, baseY, game);
    const Pick p = Choose(gameOk, game, true, feet);
    Look l;
    l.gameOk = p.gameOk;
    l.gameOff = p.gameOk ? std::hypot(p.gameGx - tx, p.gameGz - tz) : INFINITY;
    l.oursOff = std::hypot(p.gx - tx, p.gz - tz);
    l.downDeg = std::asin(-d[1]) * 57.2957795f;
    return l;
}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

    // ---- the shear: a cell drawn on any ground comes back as itself; the game's cell is it only at y 0 ----
    for (float y : {-6.0f, -2.0f, 0.0f, 1.5f, 3.0f, 9.0f}) {
        float p[3], gx, gz, ggx, ggz;
        Drawn(12.25f, -40.5f, y, p);
        Cell(p, &gx, &gz);
        GameCell(p, &ggx, &ggz);
        Check(Near(gx, 12.25f) && Near(gz, -40.5f), "Cell undoes the drawn shear at every height");
        Check((y == 0.0f) == (Near(ggx, 12.25f) && Near(ggz, -40.5f)), "the game's hit * 0.5 is the cell only on base ground");
    }

    // ---- the game's own view: along the shear, the base plane's hit is the cell at any height (why the game's pick needs no height) ----
    {
        float worst = 0.0f;
        for (float y : {0.0f, 1.0f, 3.0f, 6.0f, -4.0f}) {
            float feet[3];
            Drawn(30.0f, 70.0f, y, feet);
            float d[3] = {-0.6123724f, -0.5f, -0.6123724f};   // the game's "floor under a cell" direction, downward (0x6B0350)
            const float eye[3] = {feet[0] - d[0] * 60.0f, feet[1] - d[1] * 60.0f, feet[2] - d[2] * 60.0f};
            float game[3];
            Check(PlaneHit(eye, d, 0.0f, game), "the game's view meets its base plane");
            const Pick p = Choose(true, game, true, feet);
            worst = std::fmax(worst, std::fmax(std::hypot(p.gameGx - 30.0f, p.gameGz - 70.0f), p.offCells));
            Check(p.ours && Near(p.gx, 30.0f) && Near(p.gz, 70.0f), "ours is the cell along the game's view");
        }
        Check(worst < 1e-3f, "along the game's own view the game's cell and ours agree on raised ground too");
        if (g_verbose) std::printf("the game's view: game vs ours %.5f cells at worst\n", worst);
    }

    // ---- the walk: base ground (y 0) -> a ledge 3 units up -> back. A monster 4 cells ahead on the hero's floor, his eyes 5.3 up. ----
    // The look's heading: the four axes of the grid and the diagonals (the shear is along the diagonal, so the error depends on it).
    {
        struct Leg { const char* where; float y; };
        const Leg walk[] = {{"base ground", 0.0f}, {"on the ledge, 3 up", 3.0f}, {"on a stair, 1.5 up", 1.5f}, {"back on base ground", 0.0f}};
        for (const Leg& leg : walk) {
            float gameWorst = 0.0f, gameBest = INFINITY, oursWorst = 0.0f;
            int missed = 0;
            for (int h = 0; h < 8; ++h) {
                const float a = h * 0.785398163f;
                const float hx = 50.0f, hz = 50.0f;
                const Look l = LookAt(hx, hz, leg.y, 5.3f, hx + 4.0f * std::cos(a), hz + 4.0f * std::sin(a), leg.y);
                gameWorst = std::fmax(gameWorst, l.gameOff);
                gameBest = std::fmin(gameBest, l.gameOff);
                oursWorst = std::fmax(oursWorst, l.oursOff);
                if (l.gameOff > 1.0f) ++missed;
            }
            if (g_verbose)
                std::printf("%-22s the game's cell off by %.2f .. %.2f cells (over a cell on %d of 8 headings), ours %.4f\n", leg.where, gameBest,
                            gameWorst, missed, oursWorst);
            Check(oursWorst < 1e-3f, "ours is the monster's cell on every floor and heading");
            if (leg.y == 0.0f) Check(gameWorst < 1e-3f, "on base ground the game's own cell is right (aiming works there)");
            // (looking the way the game's own camera looks - along the shear - the slip mostly cancels; every other way it does not)
            else Check(missed >= 5 && gameWorst > 2.0f, "on raised ground the game's cell misses the monster by over a cell on most headings (the report)");
        }
    }

    // ---- far and shallow, 1 m up (~3.1 units: ViewHeight 5.3 / eye 1.7 m): how far off the game's pick is, by how steeply one looks ----
    {
        float off10 = 0.0f;
        for (float cells : {3.0f, 8.0f, 20.0f}) {
            for (int h = 0; h < 8; ++h) {
                const float a = h * 0.785398163f;
                const Look l = LookAt(0.0f, 0.0f, 3.1f, 5.3f, cells * std::cos(a), cells * std::sin(a), 3.1f);
                Check(l.oursOff < 1e-3f, "ours at any distance");
                if (cells == 8.0f) off10 = std::fmax(off10, l.gameOff);
                if (g_verbose && h % 2 == 0)
                    std::printf("1 m up, %4.0f cells out, heading %3d deg, %4.1f deg down: the game's cell %5.1f cells off\n", cells, h * 45, l.downDeg,
                                l.gameOff);
            }
        }
        Check(off10 > 3.0f, "8 cells out on a floor 1 m up the game's point is several cells off");
    }

    // ---- down from a ledge onto base ground, and up from base ground onto a ledge: ours is the monster's cell, the game's only when IT is on base ----
    {
        const Look down = LookAt(0.0f, 0.0f, 3.0f, 5.3f, 6.0f, 2.0f, 0.0f);
        Check(down.oursOff < 1e-3f, "from the ledge down: ours");
        Check(down.gameOff < 1e-3f, "from the ledge down: the game's is right too (the monster's feet are on its base plane)");
        const Look up = LookAt(0.0f, 0.0f, 0.0f, 5.3f, 6.0f, 2.0f, 3.0f);
        Check(up.oursOff < 1e-3f, "from below up onto the ledge: ours");
        Check(up.gameOff > 1.0f, "from below up onto the ledge: the game's misses");
    }

    // ---- what goes to the game ----
    {
        const float hit[3] = {10.0f, 0.0f, 20.0f}, floor[3] = {13.0f, 2.0f, 23.0f};
        Pick p = Choose(true, hit, false, floor);
        Check(p.ok && !p.ours && Near(p.gx, 5.0f) && Near(p.gz, 10.0f), "no floor hit: the game's own point, unchanged");
        p = Choose(false, hit, false, floor);
        Check(!p.ok, "neither: no point (the game's function says false, as before)");
        p = Choose(false, hit, true, floor);
        Check(p.ok && p.ours && Near(p.gx, (13.0f - 2.0f * kShear) / 2.0f), "the game's raycast missed, the floor did not: ours");
        const float nan[3] = {NAN, 0.0f, 0.0f};
        p = Choose(true, hit, true, nan);
        Check(p.ok && !p.ours && Near(p.gx, 5.0f), "a floor hit that is no number: the game's");
        p = Choose(true, hit, true, floor);
        Check(p.ours && p.offCells > 0.0f, "both: ours, and how far the game's was");
    }

    if (g_fail) { std::printf("flat_ground_test: %d FAILED\n", g_fail); return 1; }
    std::printf("flat_ground_test: all passed\n");
    return 0;
}
