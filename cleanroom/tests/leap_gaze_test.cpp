// [gameplay] leap_by_gaze (vr/leap_gaze.h) without the game and without a headset: the gaze's ray to the ground, the range clamp, the point on the
// grid (and the map's edge), and Leap Attack's monster under the gaze.
//
//   leap_gaze_test        every check below

#include "../../vr/leap_gaze.h"

#include <cmath>
#include <cstdio>

using namespace leapgaze;

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

bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }
float DownOf(float deg) { return std::tan(deg * 3.14159265f / 180.0f); }

void Skills() {
    EXPECT(IsLeap(132) && IsLeap(143), "Leap and Leap Attack");
    EXPECT(!IsLeap(0) && !IsLeap(107) && !IsLeap(151) && !IsLeap(54), "Attack, Charge, Whirlwind, Teleport are not (yet)");
}

void Range() {
    EXPECT(Near(RangeCells(12.0f), 12.0f), "the game's own point's distance is the range: %f", RangeCells(12.0f));
    EXPECT(Near(RangeCells(3.0f), kMaxCells), "the ~3-cell attack default is no leap's range: %f", RangeCells(3.0f));
    EXPECT(Near(RangeCells(0.0f), kMaxCells) && Near(RangeCells(NAN), kMaxCells) && Near(RangeCells(-5.0f), kMaxCells), "none / not finite: 40 cells");
    EXPECT(Near(RangeCells(90.0f), kMaxCells), "never past 40 cells: %f", RangeCells(90.0f));
}

void GroundTests() {
    // eyes 3 cells up (about 1.8 m at 3.3 units a metre), looking 20 degrees down: the ground 3 / tan 20 = 8.24 cells out
    Ground g = GroundCells(3.0f, DownOf(20.0f), 12.0f);
    EXPECT(g.met && Near(g.cells, 3.0f / DownOf(20.0f), 1e-2f), "20 deg down meets the ground 8.2 cells out: %d %f", g.met, g.cells);
    // 10 degrees down: 17 cells, past a 12-cell range - the full range along the heading
    g = GroundCells(3.0f, DownOf(10.0f), 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "beyond the range: the full range: %d %f", g.met, g.cells);
    // looking level and up: no ground - the full range
    g = GroundCells(3.0f, 0.0f, 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "level: the full range: %d %f", g.met, g.cells);
    g = GroundCells(3.0f, DownOf(-25.0f), 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "up: the full range: %d %f", g.met, g.cells);
    g = GroundCells(3.0f, kMinDown * 0.5f, 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "a hair under level: the full range: %d %f", g.met, g.cells);
    // at the feet: held out to kMinCells
    g = GroundCells(3.0f, DownOf(85.0f), 12.0f);
    EXPECT(g.met && Near(g.cells, kMinCells), "at the feet: %f cells", g.cells);
    // no range known (40 cells): a far gaze meets the ground
    g = GroundCells(3.0f, DownOf(5.0f), RangeCells(0.0f));
    EXPECT(g.met && Near(g.cells, 3.0f / DownOf(5.0f), 1e-2f), "5 deg down, 40-cell cap: %f", g.cells);
    // bad eyes: the full range, not met
    g = GroundCells(0.0f, DownOf(30.0f), 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "no eye height: %d %f", g.met, g.cells);
    g = GroundCells(NAN, DownOf(30.0f), 12.0f);
    EXPECT(!g.met && Near(g.cells, 12.0f), "not finite: %d %f", g.met, g.cells);
    g = GroundCells(3.0f, DownOf(20.0f), NAN);
    EXPECT(g.met && Near(g.cells, 3.0f / DownOf(20.0f), 1e-2f), "range not finite: 40 cells: %f", g.cells);
}

void Down() {
    float d = 0.0f;
    EXPECT(DownPerAcross(1.0f, -1.0f, 0.0f, &d) && Near(d, 1.0f), "45 deg down: %f", d);
    EXPECT(DownPerAcross(0.0f, 0.5f, -2.0f, &d) && Near(d, -0.25f), "up: %f", d);
    EXPECT(!DownPerAcross(0.0f, -1.0f, 0.0f, &d), "straight down: no heading");
    EXPECT(!DownPerAcross(NAN, -0.3f, 1.0f, &d), "not finite");
}

void PointOnGrid() {
    float px = 0.0f, py = 0.0f;
    EXPECT(Point(1000.0f, 2000.0f, 3.0f, 4.0f, 10.0f, 12.0f, &px, &py) && Near(px, 1006.0f) && Near(py, 2008.0f), "10 cells along (0.6, 0.8): %f %f", px, py);
    EXPECT(Point(1000.0f, 2000.0f, 1.0f, 0.0f, 30.0f, 12.0f, &px, &py) && Near(px, 1012.0f) && Near(py, 2000.0f), "held to the range: %f %f", px, py);
    EXPECT(Point(1000.0f, 2000.0f, 1.0f, 0.0f, 0.2f, 12.0f, &px, &py) && Near(px, 1001.0f), "held out to kMinCells: %f", px);
    EXPECT(Point(1000.0f, 2000.0f, 0.0f, -1.0f, 80.0f, 90.0f, &px, &py) && Near(py, 2000.0f - kMaxCells), "never past 40: %f", py);
    // the map's edge: a nearer point on the same line
    EXPECT(Point(5.0f, 2000.0f, -1.0f, 0.0f, 10.0f, 12.0f, &px, &py) && Near(px, 1.0f) && Near(py, 2000.0f), "the edge: %f %f", px, py);
    px = py = -7.0f;
    EXPECT(!Point(1.2f, 2000.0f, -1.0f, 0.0f, 10.0f, 12.0f, &px, &py) && px == -7.0f && py == -7.0f, "no room: refused, untouched");
    EXPECT(!Point(1000.0f, 2000.0f, 0.0f, 0.0f, 10.0f, 12.0f, &px, &py), "no heading");
    EXPECT(!Point(NAN, 2000.0f, 1.0f, 0.0f, 10.0f, 12.0f, &px, &py), "no hero");
}

void MonsterTests() {
    const Body body{0.5f, 1.2f};   // cells
    // eyes 3 cells up, the gaze along +x
    // a monster 6 cells ahead: looked at its chest (the ray 0.8 cells up there): found
    Monster m[4] = {{11, 6.0f, 0.0f}, {12, 3.0f, 2.0f}, {13, 9.0f, 0.1f}, {14, -4.0f, 0.0f}};
    const float chest = (3.0f - 0.8f) / 6.0f;
    Pick p = PickMonster(1.0f, 0.0f, 3.0f, chest, 12.0f, m, 4, body);
    EXPECT(p.found && p.id == 11 && Near(p.along, 6.0f), "the chest of the one ahead: %d %u %f", p.found, p.id, p.along);
    // looked over its head (the ray 1.6 up at 6 cells): the one behind it, 9 cells out, when the ray comes down into it
    const float over = (3.0f - 1.6f) / 6.0f;   // at 9 cells: 3 - 9 * 0.233 = 0.9 up - in its body
    p = PickMonster(1.0f, 0.0f, 3.0f, over, 12.0f, m, 4, body);
    EXPECT(p.found && p.id == 13, "over the first, into the second: %d %u", p.found, p.id);
    // looked at the ground before it (the ray meets the ground 4 cells out): none
    p = PickMonster(1.0f, 0.0f, 3.0f, 3.0f / 4.0f, 12.0f, m, 4, body);
    EXPECT(!p.found, "the ground before it: none (%u)", p.id);
    // to the side: the gaze along (3, 2) meets the one off to the left
    const float d = std::hypot(3.0f, 2.0f);
    p = PickMonster(3.0f, 2.0f, 3.0f, (3.0f - 0.6f) / d, 12.0f, m, 4, body);
    EXPECT(p.found && p.id == 12 && Near(p.across, 0.0f, 1e-3f), "the one to the left: %d %u %f", p.found, p.id, p.across);
    // beyond the range: none
    p = PickMonster(1.0f, 0.0f, 3.0f, chest, 4.0f, m, 4, body);
    EXPECT(!p.found, "out of range: none (%u)", p.id);
    // behind the hero: never
    p = PickMonster(-1.0f, 0.0f, 3.0f, (3.0f - 0.6f) / 4.0f, 12.0f, m, 4, body);
    EXPECT(p.found && p.id == 14, "behind is ahead when looked at: %d %u", p.found, p.id);
    Monster only[1] = {{14, -4.0f, 0.0f}};
    p = PickMonster(1.0f, 0.0f, 3.0f, 0.0f, 12.0f, only, 1, body);
    EXPECT(!p.found, "the one behind is not under a gaze ahead");
    // a near one and a far one on the same line, the ray through both: the nearer
    Monster two[2] = {{21, 8.0f, 0.0f}, {22, 4.0f, 0.0f}};
    p = PickMonster(1.0f, 0.0f, 0.6f, 0.0f, 12.0f, two, 2, body);
    EXPECT(p.found && p.id == 22, "the nearer: %u", p.id);
    // just off the side (more than the radius): none
    Monster side[1] = {{31, 6.0f, 0.6f}};
    p = PickMonster(1.0f, 0.0f, 3.0f, chest, 12.0f, side, 1, body);
    EXPECT(!p.found, "0.6 cells off a 0.5 radius: none");
    // nothing to read / no heading / not finite
    EXPECT(!PickMonster(1.0f, 0.0f, 3.0f, chest, 12.0f, nullptr, 0, body).found, "no monsters");
    EXPECT(!PickMonster(0.0f, 0.0f, 3.0f, chest, 12.0f, m, 4, body).found, "no heading");
    EXPECT(!PickMonster(1.0f, 0.0f, NAN, chest, 12.0f, m, 4, body).found, "eyes not finite");
}

void Wider() {
    // [gameplay] teleport_circle: a point skill's own hard cap (48) instead of the leap's 40
    Ground g = GroundCells(3.0f, 3.0f / 45.0f, 48.0f, 48.0f);
    EXPECT(g.met && Near(g.cells, 45.0f, 1e-2f), "45 cells out within a 48 cap: %f", g.cells);
    g = GroundCells(3.0f, 3.0f / 45.0f, 48.0f);
    EXPECT(!g.met && Near(g.cells, kMaxCells), "the leap's own 40 by default: %f", g.cells);
    float px = 0.0f, py = 0.0f;
    EXPECT(Point(1000.0f, 2000.0f, 1.0f, 0.0f, 46.0f, 48.0f, &px, &py, 48.0f) && Near(px, 1046.0f), "46 cells with the 48 cap: %f", px);
    EXPECT(Point(1000.0f, 2000.0f, 1.0f, 0.0f, 46.0f, 48.0f, &px, &py) && Near(px, 1040.0f), "the leap's 40 by default: %f", px);
}

void NearPick() {
    // [gameplay] leap_arc: the monster at the circle
    Monster m[3] = {{1, 10.0f, 0.0f}, {2, 10.5f, 0.4f}, {3, 3.0f, 3.0f}};
    Pick p = PickNear(10.4f, 0.3f, 1.0f, m, 3);
    EXPECT(p.found && p.id == 2 && Near(p.along, std::hypot(10.5f, 0.4f), 1e-3f), "the nearest to the circle: %u", p.id);
    p = PickNear(6.0f, 0.0f, 1.0f, m, 3);
    EXPECT(!p.found, "none within the radius");
    EXPECT(!PickNear(10.0f, 0.0f, 1.0f, nullptr, 0).found && !PickNear(10.0f, 0.0f, 0.0f, m, 3).found && !PickNear(NAN, 0.0f, 1.0f, m, 3).found, "nothing to pick");
}

}  // namespace

int main() {
    Skills();
    Range();
    GroundTests();
    Down();
    PointOnGrid();
    MonsterTests();
    Wider();
    NearPick();
    if (g_fails) {
        std::printf("leap_gaze_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("leap_gaze_test: all passed\n");
    return 0;
}
