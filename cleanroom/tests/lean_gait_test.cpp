// Offline checks of [direct] walk_by_lean (vr/lean_gait.h): the stick's length handed to the game against its own run threshold, and the Run key.
// The game (0x14AA10): it runs while the length is ABOVE its threshold t (and its run is on), walks at length / t under it.
// Run: build/Release/lean_gait_test.exe  (exit code 0 = all passed)

#include <cmath>
#include <cstdio>

#include "../../vr/lean_gait.h"

using namespace leangait;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

// What the game makes of a length: runs (true) when it is above t.
static bool GameRuns(float len, float t) { return len > t; }

static void TestShape() {
    // the game's threshold under, at and over our limit: walk up to the limit, run past it, whatever t is
    for (float t : {0.30f, 0.50f, 0.70f, 0.80f, 0.90f, 0.95f}) {
        for (float limit : {0.20f, 0.50f, 0.80f, 0.95f}) {
            for (int i = 1; i <= 100; ++i) {
                const float lean = i / 100.0f;
                const float s = Shape(lean, limit, t);
                CHECK(s >= 0.0f && s <= 1.0f);
                CHECK(GameRuns(s, t) == (lean > limit + 1e-6f));
            }
            CHECK(Near(Shape(limit, limit, t), t));              // the walk at full strength (t / t) right at the limit
            CHECK(Near(Shape(limit / 2.0f, limit, t), t / 2.0f)); // half the lean, half a walk
            CHECK(Near(Shape(1.0f, limit, t), 1.0f));            // the rim stays the rim (the full run)
            // the length grows with the lean: no step back anywhere
            float was = 0.0f;
            for (int i = 0; i <= 200; ++i) {
                const float s = Shape(i / 200.0f, limit, t);
                CHECK(s + 1e-6f >= was);
                was = s;
            }
        }
    }
    // the author's case: 80 %, the game's threshold read as 0.9 - a lean of 0.85 runs (the game alone would walk), 0.75 walks
    CHECK(GameRuns(Shape(0.85f, 0.80f, 0.90f), 0.90f));
    CHECK(!GameRuns(Shape(0.75f, 0.80f, 0.90f), 0.90f));
    // ... and a threshold of 0.5: 0.6 walks (the game alone would run)
    CHECK(!GameRuns(Shape(0.60f, 0.80f, 0.50f), 0.50f));
    // nothing pushed, nonsense in: nothing out
    CHECK(Shape(0.0f, 0.8f, 0.9f) == 0.0f);
    CHECK(Shape(-0.3f, 0.8f, 0.9f) == 0.0f);
    CHECK(Shape(NAN, 0.8f, 0.9f) == 0.0f);
    CHECK(Near(Shape(1.4f, 0.8f, 0.9f), 1.0f));
    // a threshold that cannot be read (0, NaN, 1 and over): the stick as it is
    CHECK(Near(Shape(0.6f, 0.8f, 0.0f), 0.6f));
    CHECK(Near(Shape(0.6f, 0.8f, NAN), 0.6f));
    CHECK(Near(Shape(0.6f, 0.8f, 1.0f), 0.6f));
    // the limit kept in its range (the ini's 20 - 95)
    CHECK(Near(ClampLimit(0.05f), kMinLimit) && Near(ClampLimit(1.0f), kMaxLimit) && Near(ClampLimit(NAN), kDefaultLimit));
    CHECK(GameRuns(Shape(0.99f, 1.0f, 0.9f), 0.9f));        // a limit of 100 % clamps to 95: the rim still runs
}

static void TestRunKey() {
    // the switch on walk: the key from past the limit, let go only kRunKeyBand under it
    RunKey k;
    CHECK(!k.Step(0.0f, 0.8f, false));
    CHECK(!k.Step(0.79f, 0.8f, false));
    CHECK(!k.Step(0.80f, 0.8f, false));                    // at the limit: still a walk
    CHECK(k.Step(0.81f, 0.8f, false));
    CHECK(k.Step(0.78f, 0.8f, false));                     // a wobble at the edge keeps it
    CHECK(!k.Step(0.76f, 0.8f, false));                    // ... back under the band: let go
    CHECK(!k.Step(0.78f, 0.8f, false));                    // and not taken again until past the limit
    CHECK(k.Step(1.0f, 0.8f, false));
    CHECK(!k.Step(0.0f, 0.8f, false));                     // the stick let go (or no walk): let go
    // the switch on run: the game runs past its threshold anyway - no key, ever
    RunKey r;
    for (float l : {0.0f, 0.5f, 0.85f, 1.0f, 0.9f}) CHECK(!r.Step(l, 0.8f, true));
    // the switch flipped to walk while pushed far: the key at once; back to run: let go at once
    CHECK(r.Step(0.9f, 0.8f, false));
    CHECK(!r.Step(0.9f, 0.8f, true));
    CHECK(!RunKey{}.Step(NAN, 0.8f, false));
    CHECK(kRunCommand == 0x22);                            // the game's "Run (hold)" (shared/d2r_vr_shared.h)
}

int main() {
    TestShape();
    TestRunKey();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("lean gait: all %d checks passed\n", g_checks);
    return 0;
}
