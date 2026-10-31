// period_bright_test - [sky] bright_<period> (vr/sky/period_bright.h): the sky's brightness for each period of the game's day.
//   1. the sliders at 1 (the default): 1 always - nothing changes until they are moved;
//   2. debug_mode's steps (the light jumps to each period's first): each period's own slider exactly;
//   3. a day passing by the light: dusk eases to the start of night's, the start of night to night's; midday's peak holds the day's,
//      the fall from it eases to dusk's; never outside the two sliders it eases between, no jump at a period's turn;
//   4. a turn seen teaches where the next period begins; a period out of range: 1.
#include <cmath>
#include <cstdio>

#include "sky/period_bright.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace periodbright;
    const float ones[kPeriods] = {1, 1, 1, 1, 1, 1};
    {
        State s;
        for (int p = 0; p < kPeriods; ++p)
            for (float l = 60.0f; l <= 255.0f; l += 13.0f) CHECK(Mul(s, p, l, ones) == 1.0f);
    }
    const float b[kPeriods] = {0.6f, 0.9f, 1.2f, 0.8f, 0.4f, 0.2f};
    {   // debug_mode's steps: each period at its first light
        State s;
        for (int round = 0; round < 2; ++round)
            for (int p = 0; p < kPeriods; ++p) CHECK(std::fabs(Mul(s, p, kStartLight[p], b) - b[p]) < 1e-6f);
    }
    {   // a day passing: day rises to 254 and falls to 150, dusk 150 -> 117, the start of night 117 -> 65, night at 65
        State s;
        float last = Mul(s, 2, 132.0f, b);
        CHECK(std::fabs(last - b[2]) < 1e-6f);
        for (float l = 132.0f; l <= 254.0f; l += 2.0f) CHECK(std::fabs(Mul(s, 2, l, b) - b[2]) < 1e-6f);   // rising to the peak: the day's
        for (float l = 254.0f; l >= 150.0f; l -= 2.0f) {
            const float m = Mul(s, 2, l, b);
            CHECK(m <= b[2] + 1e-6f && m >= b[3] - 1e-6f && m <= last + 1e-6f);   // easing down to dusk's
            last = m;
        }
        CHECK(std::fabs(last - b[3]) < 1e-3f);
        const float atDusk = Mul(s, 3, 150.0f, b);
        CHECK(std::fabs(atDusk - last) < 1e-3f);   // no jump at the turn
        CHECK(std::fabs(Mul(s, 3, 133.5f, b) - (b[3] + b[4]) * 0.5f) < 1e-3f);   // half way 150 -> 117: half way
        CHECK(std::fabs(Mul(s, 3, 117.0f, b) - b[4]) < 1e-3f);
        CHECK(std::fabs(Mul(s, 4, 117.0f, b) - b[4]) < 1e-3f);
        CHECK(std::fabs(Mul(s, 4, 91.0f, b) - (b[4] + b[5]) * 0.5f) < 1e-3f);
        CHECK(std::fabs(Mul(s, 5, 65.0f, b) - b[5]) < 1e-3f);
    }
    {   // a turn teaches where a period begins: dusk began at 160 here - a later day eases to dusk's by 160
        State s;
        Mul(s, 2, 200.0f, b);
        Mul(s, 3, 160.0f, b);
        CHECK(s.start[3] == 160.0f);
        Mul(s, 2, 254.0f, b);
        CHECK(std::fabs(Mul(s, 2, 160.0f, b) - b[3]) < 1e-3f);
        CHECK(Mul(s, -1, 100.0f, b) == 1.0f && Mul(s, 6, 100.0f, b) == 1.0f);
    }
    if (g_fail) { std::printf("period_bright_test: %d FAILED\n", g_fail); return 1; }
    std::printf("period_bright_test: the sliders at 1 change nothing; each period its own at its first light; a day passing eases from one "
                "to the next by the game's light, no jump at a turn; a turn teaches where a period begins: all passed\n");
    return 0;
}
