// flat_arrow_test - [bow] arrow_flight_flat, the pure part (vr/flat_arrow.h): the shot's line a flat click notes for the drawn arrow - the release point,
// the line to what the crosshair met, the heading the game is given and the distance between the cells.
#include <cmath>
#include <cstdio>

#include "../../vr/flat_arrow.h"
#include "../../vr/flat_ground.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace flatarrow;

bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

static_assert(kShear == flatground::kShear && kPerCell == flatground::kRenderPerCell, "the shear and the cell as flat_ground.h");

// The hero at cell (100, 200) on base ground (world 200, 0, 400), the eye 5.4 units up (1.8 m at 3 units a metre), looking along +x, a little down.
LineIn Scene() {
    LineIn in;
    in.feet[0] = 200.0f; in.feet[1] = 0.0f; in.feet[2] = 400.0f;
    in.o[0] = 200.0f; in.o[1] = 5.4f; in.o[2] = 400.0f;
    const float down = 0.2f, l = std::sqrt(1.0f + down * down);
    in.d[0] = 1.0f / l; in.d[1] = -down / l; in.d[2] = 0.0f;
    in.upm = 3.0f;
    return in;
}

void TestFloor() {
    std::printf("the ray meets the game's floor: the arrow goes there and sticks\n");
    LineIn in = Scene();
    in.floor = true;
    in.floorAt[0] = 227.0f; in.floorAt[1] = 0.0f; in.floorAt[2] = 400.0f;   // 27 units out = 13.5 cells
    const Line l = MakeLine(in);
    EXPECT(l.ok && l.target && l.stick && !l.monster, "a floor hit");
    EXPECT(Near(l.start[0], 200.0f) && Near(l.start[1], 5.4f - kBelowEyeM * 3.0f) && Near(l.start[2], 400.0f), "first person: below the eye (%.2f)", l.start[1]);
    EXPECT(Near(l.at[0], 227.0f) && Near(l.at[1], 0.0f), "the point met");
    const float wx = 27.0f, wy = -(5.4f - 0.75f), len = std::sqrt(wx * wx + wy * wy);
    EXPECT(Near(l.dir[0], wx / len) && Near(l.dir[1], wy / len) && Near(l.dir[2], 0.0f), "the line from the release point to it (%.3f %.3f)", l.dir[0], l.dir[1]);
    EXPECT(Near(l.theta, 0.0f), "the heading along +x: %.4f", l.theta);
    EXPECT(Near(l.gridDist, 27.0f), "13.5 cells = 27 units: %.2f", l.gridDist);
}

void TestRaised() {
    std::printf("a floor up the stairs: the heading and the distance by its cell, the shear taken off\n");
    LineIn in = Scene();
    in.floor = true;
    const float h = 3.0f;   // a metre up
    in.floorAt[0] = 220.0f + h * kShear; in.floorAt[1] = h; in.floorAt[2] = 410.0f + h * kShear;   // drawn over cell (110, 205)
    const Line l = MakeLine(in);
    EXPECT(l.ok && l.stick, "a floor hit");
    EXPECT(Near(l.theta, std::atan2(10.0f, 20.0f)), "the hero's cell to its cell, not to where it is drawn: %.4f", l.theta);
    EXPECT(Near(l.gridDist, std::sqrt(20.0f * 20.0f + 10.0f * 10.0f)), "the cells' distance: %.2f", l.gridDist);
    EXPECT(Near(l.at[1], h), "the line goes up to the drawn point");
}

void TestBody() {
    std::printf("the ray meets a body first: a monster, its cell\n");
    LineIn in = Scene();
    in.floor = true;
    in.floorAt[0] = 240.0f; in.floorAt[1] = 0.0f; in.floorAt[2] = 400.0f;
    in.body = true;
    in.bodyMonster = true;
    in.bodyAt[0] = 216.0f; in.bodyAt[1] = 2.0f; in.bodyAt[2] = 401.0f;
    in.bodyCellX = 108.0f; in.bodyCellZ = 202.0f;   // the middle of its cell: 3 units across from where the crosshair met it
    const Line l = MakeLine(in);
    EXPECT(l.ok && l.target && l.monster && !l.stick, "a monster");
    EXPECT(Near(l.at[0], 216.0f) && Near(l.at[1], 2.0f), "the body's point, not the floor behind it");
    EXPECT(Near(l.theta, std::atan2(1.0f, 16.0f)), "the heading to where the crosshair met it, not to the middle of its cell (no aim at the monster): %.4f", l.theta);
    EXPECT(Near(l.gridDist, std::sqrt(16.0f * 16.0f + 1.0f)), "the distance to that point: %.3f", l.gridDist);
    in.bodyMonster = false;   // a chest: met, but no monster - and it does not stick
    const Line c = MakeLine(in);
    EXPECT(c.ok && c.target && !c.monster && !c.stick, "an object");
}

void TestNothing() {
    std::printf("the ray meets nothing (the sky): the line is the ray's, the heading its own\n");
    LineIn in = Scene();
    in.d[0] = 0.6f; in.d[1] = 0.6f; in.d[2] = -0.52915026f;
    const Line l = MakeLine(in);
    EXPECT(l.ok && !l.target && !l.stick && !l.monster && l.gridDist == 0.0f, "nothing met");
    EXPECT(Near(l.dir[0], 0.6f) && Near(l.dir[1], 0.6f) && Near(l.dir[2], -0.52915026f), "along the ray");
    EXPECT(Near(l.theta, std::atan2(-0.52915026f, 0.6f)), "its heading: %.4f", l.theta);
    in.d[0] = 0.0f; in.d[1] = 1.0f; in.d[2] = 0.0f;
    EXPECT(!MakeLine(in).ok, "straight up: no heading");
}

void TestThird() {
    std::printf("third person: from the hero's chest\n");
    LineIn in = Scene();
    in.third = true;
    in.o[0] = 185.0f; in.o[1] = 12.0f; in.o[2] = 396.0f;   // the camera behind and over him
    in.floor = true;
    in.floorAt[0] = 230.0f; in.floorAt[1] = 0.0f; in.floorAt[2] = 400.0f;
    const Line l = MakeLine(in);
    EXPECT(l.ok && Near(l.start[0], 200.0f) && Near(l.start[1], kChestM * 3.0f) && Near(l.start[2], 400.0f), "the chest: %.2f %.2f %.2f", l.start[0], l.start[1], l.start[2]);
    EXPECT(Near(l.theta, 0.0f), "the heading from his cell, not from the camera: %.4f", l.theta);
}

void TestEdges() {
    std::printf("the edges\n");
    LineIn in = Scene();
    in.floor = true;
    in.floorAt[0] = 200.5f; in.floorAt[1] = 0.0f; in.floorAt[2] = 400.0f;   // his own cell
    const Line l = MakeLine(in);
    EXPECT(l.ok && Near(l.theta, std::atan2(in.d[2], in.d[0])), "on his own cell: the ray's heading");
    LineIn bad = Scene();
    bad.o[0] = NAN;
    EXPECT(!MakeLine(bad).ok, "a ray that is not a number");
    LineIn noUpm = Scene();
    noUpm.upm = 0.0f;
    const Line u = MakeLine(noUpm);
    EXPECT(u.ok && Near(u.start[1], 5.4f - kBelowEyeM), "no scale: a unit a metre");
    LineIn badFloor = Scene();
    badFloor.floor = true;
    badFloor.floorAt[1] = NAN;
    const Line f = MakeLine(badFloor);
    EXPECT(f.ok && !f.target, "a floor point that is not a number: as nothing met");
}

void TestHeroCell() {
    std::printf("the hero's cell for the far point: the look-at less its height's push, in cells\n");
    float cx = 0.0f, cz = 0.0f;
    const float base[3] = {200.0f, 0.0f, 400.0f};
    EXPECT(HeroCell(base, &cx, &cz) && Near(cx, 100.0f) && Near(cz, 200.0f), "base ground: %.2f %.2f", cx, cz);
    const float up[3] = {200.0f + 3.0f * kShear, 3.0f, 400.0f + 3.0f * kShear};
    EXPECT(HeroCell(up, &cx, &cz) && Near(cx, 100.0f) && Near(cz, 200.0f), "raised ground, the same cell: %.2f %.2f", cx, cz);
    const float bad[3] = {NAN, 0.0f, 0.0f};
    EXPECT(!HeroCell(bad, &cx, &cz), "not a number");
    EXPECT(kFarCells >= 30.0f, "far enough that a whole cell is about a degree");
}

}  // namespace

int main() {
    TestFloor();
    TestRaised();
    TestBody();
    TestNothing();
    TestThird();
    TestEdges();
    TestHeroCell();
    std::printf("\nflat_arrow_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
