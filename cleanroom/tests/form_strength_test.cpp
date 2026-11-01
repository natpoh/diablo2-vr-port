// form_strength_test.cpp - [shapeshift] wolf_strength_pct / bear_strength_pct (vr/form_strength.h): a grafted form's paws stronger in the weight model's grab.
// Run: form_strength_test.exe
#include "form_strength.h"
#include <cmath>
#include <cstdio>
using namespace formstrength;
static int g_fail = 0, g_pass = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } else ++g_pass; } while (0)
static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-4f; }

int main() {
    // A person (no grafted form): [weight] strength as it is, whatever the forms' sliders say.
    CHECK(Near(Factor(0, 200, 300), 1.0f) && Near(Factor(0, 600, 600), 1.0f) && Near(Factor(5, 600, 600), 1.0f));
    CHECK(Near(Hands(1.3f, 0, 200, 300), 1.3f));
    // The defaults (Serg 2026-11-01): the werewolf three times the hero, the werebear five times - the bear the stronger.
    CHECK(Near(Factor(1, kWolfDefaultPct, kBearDefaultPct), 3.0f) && Near(Factor(2, kWolfDefaultPct, kBearDefaultPct), 5.0f));
    CHECK(Factor(2, kWolfDefaultPct, kBearDefaultPct) > Factor(1, kWolfDefaultPct, kBearDefaultPct));
    // Each form its own slider.
    CHECK(Near(Factor(1, 250, 400), 2.5f) && Near(Factor(2, 250, 400), 4.0f));
    // On top of [weight] strength: a weak hero's paws are weak paws too, only stronger than his hands.
    CHECK(Near(Hands(0.5f, 1, 200, 300), 1.0f) && Near(Hands(0.5f, 2, 200, 300), 1.5f) && Near(Hands(2.0f, 2, 200, 300), 6.0f));
    // Bounds: never weaker than the person (100), at most 600; not a number is 100.
    CHECK(Near(Factor(1, 20, 300), 1.0f) && Near(Factor(2, 200, 5000), 6.0f) && Near(Factor(1, NAN, 300), 1.0f) && Near(Factor(2, 200, INFINITY), 1.0f));
    CHECK(Near(ClampPct(-50), kMinPct) && Near(ClampPct(1e9f), kMaxPct) && Near(ClampPct(NAN), 100.0f));
    // A broken [weight] strength (0, below, not a number) is the person's 1 - the paws still their factor of it.
    CHECK(Near(Hands(0.0f, 2, 200, 300), 3.0f) && Near(Hands(-1.0f, 1, 200, 300), 2.0f) && Near(Hands(NAN, 0, 200, 300), 1.0f));
    std::printf("form_strength_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
