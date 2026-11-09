// shot_release_test - the shot at release, the pure part (vr/shot_release.h): which of the hero's arrow creates are turned to the crosshair of the release,
// by how much, and where their target goes.
#include <cmath>
#include <cstdio>

#include "../../vr/shot_release.h"

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

using namespace shotrelease;

constexpr float kDeg = kPi / 180.0f;
bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// The hero at cell (5000, 4000), the click's point 40 cells out along +x.
In Arrow() {
    In in;
    in.cls = (int32_t)arrowflight::kArrow;
    in.sx = 5000; in.sy = 4000;
    in.tx = 5040; in.ty = 4000;
    in.hasTarget = true;
    in.pressKnown = true;
    in.pressTheta = 0.0f;
    in.releaseTheta = 0.0f;
    return in;
}

float HeadingOf(const In& in, const Out& o) { return std::atan2((float)(o.ty - in.sy), (float)(o.tx - in.sx)); }

void TestTurn() {
    std::printf("the crosshair turned 20 degrees during the animation: the arrow goes there, 40 cells out\n");
    In in = Arrow();
    in.releaseTheta = 20.0f * kDeg;
    const Out o = Decide(in);
    EXPECT(o.write, "written: %s", o.why);
    EXPECT(Near(HeadingOf(in, o), 20.0f * kDeg, 1.5f * kDeg), "the heading %.2f deg", HeadingOf(in, o) / kDeg);
    const float cells = std::hypot((float)(o.tx - in.sx), (float)(o.ty - in.sy));
    EXPECT(Near(cells, kFarCells, 0.75f), "40 cells out: %.2f", cells);
    in.releaseTheta = 0.0f;
    const Out same = Decide(in);
    EXPECT(same.write && same.tx == 5040 && same.ty == 4000, "no turn: the same point (%d %d)", same.tx, same.ty);
}

void TestFan() {
    std::printf("a Multiple Shot's fan turns as a whole\n");
    In in = Arrow();
    in.cls = (int32_t)arrowflight::kMultiArrow;
    in.releaseTheta = -30.0f * kDeg;
    for (int side = -2; side <= 2; ++side) {
        const float a = side * 10.0f * kDeg;
        in.tx = in.sx + (int)std::lround(std::cos(a) * 12.0f);
        in.ty = in.sy + (int)std::lround(std::sin(a) * 12.0f);
        const float own = std::atan2((float)(in.ty - in.sy), (float)(in.tx - in.sx));
        const Out o = Decide(in);
        EXPECT(o.write, "arrow %d written: %s", side, o.why);
        EXPECT(Near(HeadingOf(in, o), Wrap(own - 30.0f * kDeg), 1.5f * kDeg), "arrow %d: %.2f deg, its own %.2f turned by -30", side, HeadingOf(in, o) / kDeg, own / kDeg);
    }
}

void TestNoPress() {
    std::printf("the press's heading not known: the arrow goes to the release's heading\n");
    In in = Arrow();
    in.pressKnown = false;
    in.tx = 5010; in.ty = 4003;
    in.releaseTheta = 135.0f * kDeg;
    const Out o = Decide(in);
    EXPECT(o.write && Near(HeadingOf(in, o), 135.0f * kDeg, 1.5f * kDeg), "%.2f deg", HeadingOf(in, o) / kDeg);
}

void TestWrap() {
    std::printf("across the half turn\n");
    In in = Arrow();
    in.pressTheta = 175.0f * kDeg;
    in.tx = 4960; in.ty = 4000;   // 180 deg
    in.releaseTheta = -175.0f * kDeg;   // 10 deg further round
    const Out o = Decide(in);
    EXPECT(o.write && Near(Wrap(HeadingOf(in, o) - (-170.0f * kDeg)), 0.0f, 1.5f * kDeg), "%.2f deg", HeadingOf(in, o) / kDeg);
}

void TestLeftAlone() {
    std::printf("left as the game made them\n");
    In in = Arrow();
    in.releaseTheta = 20.0f * kDeg;
    in.cls = (int32_t)arrowflight::kStrafeArrow;
    EXPECT(!Decide(in).write, "Strafe");
    in.cls = (int32_t)arrowflight::kGuidedArrow;
    EXPECT(!Decide(in).write, "Guided Arrow");
    in.cls = 42;   // Exploding Arrow's explosion
    EXPECT(!Decide(in).write, "a hit's explosion");
    in.cls = 44;   // a throwing potion
    EXPECT(!Decide(in).write, "a potion");
    in = Arrow();
    in.releaseTheta = 20.0f * kDeg;
    in.targetUnit = true;
    EXPECT(!Decide(in).write, "aimed at a unit");
    in = Arrow();
    in.hasTarget = false;
    EXPECT(!Decide(in).write, "no target of its own");
    in = Arrow();
    in.tx = in.sx; in.ty = in.sy;
    EXPECT(!Decide(in).write, "its target its start");
    in = Arrow();
    in.releaseTheta = NAN;
    EXPECT(!Decide(in).write, "no heading");
    in = Arrow();
    in.sx = 10; in.tx = 50; in.releaseTheta = kPi;
    EXPECT(!Decide(in).write, "off the map's coordinates");
    EXPECT(Turns((int32_t)arrowflight::kBolt) && Turns((int32_t)arrowflight::kFireArrow) && Turns((int32_t)arrowflight::kMultiBolt), "bolts, elemental arrows, a fan's bolts");
}

}  // namespace

int main() {
    TestTurn();
    TestFan();
    TestNoPress();
    TestWrap();
    TestLeftAlone();
    std::printf("\nshot_release_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
