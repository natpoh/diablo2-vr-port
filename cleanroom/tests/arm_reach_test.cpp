// vr/arm_reach.h: the aim point's distance from the stretch of the aiming arm ([bow] aim_distance_by_arm). Synthetic arms: a shoulder, a length, a hand and the way it points.
#include <cmath>
#include <cstdio>

#include "../../vr/arm_reach.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The head's frame: x right, y up, z back. A right shoulder 0.2 m out, 0.2 m under the eyes, 0.08 behind them; an arm 0.65 m to the grip.
static const float kShoulder[3] = {0.20f, -0.20f, 0.08f};
static const float kLen = 0.65f;

static float Ext(const float hand[3], const float aim[3], bool* ok = nullptr) {
    float e = -1.0f;
    const bool good = armreach::Extension(hand, kShoulder, aim, kLen, &e);
    if (ok) *ok = good;
    return e;
}

int main() {
    using namespace armreach;

    // -- a straight arm along the aim is 1, whatever the elevation of the aim
    for (int deg = -80; deg <= 80; deg += 10) {
        const float t = deg * 3.14159265f / 180.0f;
        const float aim[3] = {0.0f, std::sin(t), -std::cos(t)};   // forward (-z) and up by t
        const float hand[3] = {kShoulder[0] + aim[0] * kLen, kShoulder[1] + aim[1] * kLen, kShoulder[2] + aim[2] * kLen};
        Check(Near(Ext(hand, aim), 1.0f, 1e-3f), "a straight arm pointing along the aim: 1 at every elevation");
    }
    // -- an arm out to the side with the controller turned forward: only its forward part counts
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        const float hand[3] = {kShoulder[0] + kLen, kShoulder[1], kShoulder[2]};   // straight out to the right, level (the T-pose)
        Check(Near(Ext(hand, aim), 0.0f), "a T-pose arm, the hand pointing forward: no stretch along the aim");
    }
    // -- the arm hanging, the controller turned forward: about 0
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        const float hand[3] = {kShoulder[0], kShoulder[1] - kLen, kShoulder[2]};
        Check(Near(Ext(hand, aim), 0.0f), "a hanging arm: 0");
    }
    // -- the elbow bent in, the hand at the chest, forward of the shoulder by 0.2 m
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        const float hand[3] = {0.05f, -0.30f, kShoulder[2] - 0.20f};
        const float e = Ext(hand, aim);
        Check(e > 0.25f && e < 0.35f, "an elbow bent in: a third of the arm");
    }
    // -- half out
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        const float hand[3] = {kShoulder[0], kShoulder[1], kShoulder[2] - kLen * 0.5f};
        Check(Near(Ext(hand, aim), 0.5f), "half the length out along the aim: 0.5");
    }
    // -- behind the shoulder, or farther than the arm (a tracking jump): clamped
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        const float behind[3] = {kShoulder[0], kShoulder[1], kShoulder[2] + 0.3f};
        const float far[3] = {kShoulder[0], kShoulder[1], kShoulder[2] - 2.0f};
        Check(Near(Ext(behind, aim), 0.0f), "behind the shoulder: 0");
        Check(Near(Ext(far, aim), 1.0f), "farther than the arm: 1");
    }
    // -- a direction need not be a unit vector
    {
        const float aim[3] = {0.0f, 0.0f, -7.0f};
        const float hand[3] = {kShoulder[0], kShoulder[1], kShoulder[2] - kLen * 0.5f};
        Check(Near(Ext(hand, aim), 0.5f), "the aim is normalised");
    }
    // -- nothing to measure with
    {
        bool ok = true;
        const float aim[3] = {0.0f, 0.0f, -1.0f}, nan3[3] = {NAN, 0.0f, 0.0f}, zero[3] = {0.0f, 0.0f, 0.0f}, hand[3] = {0.2f, -0.2f, -0.5f};
        float e = 5.0f;
        Check(!armreach::Extension(nan3, kShoulder, aim, kLen, &e) && e == 5.0f, "a hand that is not a number");
        Check(!armreach::Extension(hand, kShoulder, zero, kLen, &e), "no aim");
        Check(!armreach::Extension(hand, kShoulder, aim, 0.0f, &e) && !armreach::Extension(hand, kShoulder, aim, NAN, &e), "no arm length");
        Check(!armreach::Extension(hand, kShoulder, aim, kLen, nullptr), "no output");
        (void)Ext(hand, nan3, &ok);
        Check(!ok, "an aim that is not a number");
    }

    // -- cells: the near end at 0, the weapon's own at 1, straight between
    Check(Near(Cells(0.0f, 3.0f, 30.0f), 3.0f) && Near(Cells(1.0f, 3.0f, 30.0f), 30.0f) && Near(Cells(0.5f, 3.0f, 30.0f), 16.5f), "cells: near, far, half");
    Check(Near(Cells(0.5f, 10.0f, 30.0f), 20.0f), "cells: another near end");
    Check(Near(Cells(0.0f, 30.0f, 20.0f), 20.0f) && Near(Cells(1.0f, 30.0f, 20.0f), 20.0f), "a near end above the far one: the far one, whatever the arm does");
    Check(Near(Cells(0.5f, 1.0f, 30.0f), 16.5f), "a near end under 3 is 3");
    Check(Near(Cells(-1.0f, 3.0f, 30.0f), 3.0f) && Near(Cells(7.0f, 3.0f, 30.0f), 30.0f), "an extension outside 0..1 is held to it");
    Check(Near(Cells(NAN, 3.0f, 30.0f), 30.0f), "no extension: the weapon's own");
    Check(Near(Cells(0.5f, NAN, 30.0f), 16.5f), "no near end: 3");
    Check(Near(Cells(0.5f, 3.0f, NAN), 3.0f), "no far end: the least");
    Check(Near(Cells(0.5f, 3.0f, 2.0f), 3.0f), "a weapon that asks under 3 cells: 3");
    // the cells never leave the range the game accepts (vr/aim_point.h keeps them under 40 as well)
    for (float e = 0.0f; e <= 1.0f; e += 0.05f)
        for (float far : {5.0f, 20.0f, 30.0f, 40.0f}) {
            const float c = Cells(e, 3.0f, far);
            Check(c >= 3.0f - 1e-4f && c <= std::max(3.0f, far) + 1e-4f, "cells between 3 and the far end");
        }

    // -- the smoother: the first value as it is, then toward the new one; a gap forgets
    {
        Smoother s;
        Check(!s.Have(), "no value yet");
        Check(Near(s.Step(0.2f, 10.0), 0.2f), "the first value is taken as it is");
        const float one = s.Step(1.0f, 10.0 + kSmoothS);
        Check(one > 0.2f && one < 1.0f && Near(one, 0.2f + 0.8f * (1.0f - std::exp(-1.0f)), 1e-3f), "one time constant later: 63% of the way");
        float v = one;
        for (int i = 1; i <= 60; ++i) v = s.Step(1.0f, 10.0 + kSmoothS + i * 0.016);
        Check(v > 0.99f, "a second of 1 and it is 1");
        Check(Near(s.Step(0.0f, 20.0), 0.0f), "after a gap (more than kGapS) the value starts again");
        // time going backwards does not blow up
        const float back = s.Step(1.0f, 5.0);
        Check(Near(back, 1.0f), "time going back: taken as it is");
        s.Reset();
        Check(!s.Have(), "Reset");
        // a hand shaking by a few percent moves the point less than the hand does
        Smoother j;
        float lo = 1.0f, hi = 0.0f;
        double t = 100.0;
        for (int i = 0; i < 200; ++i, t += 0.011) {
            const float raw = 0.6f + ((i & 1) ? 0.04f : -0.04f);
            const float f = j.Step(raw, t);
            if (i > 20) { lo = std::min(lo, f); hi = std::max(hi, f); }
        }
        Check(hi - lo < 0.04f, "a jitter of 8% in the hand is under 4% in the point");
    }

    // -- cells to metres on the ground
    Check(Near(CellsToMetres(30.0f, 3.2f), 30.0f * 2.0f / 3.2f) && CellsToMetres(30.0f, 0.0f) == 0.0f, "cells to metres");

    // -- a walk through one pull: the arm drawn out along the aim, the point follows from near to far
    {
        const float aim[3] = {0.0f, 0.0f, -1.0f};
        float prev = 0.0f;
        bool rising = true;
        for (int step = 0; step <= 10; ++step) {
            const float hand[3] = {kShoulder[0], kShoulder[1], kShoulder[2] - kLen * (step / 10.0f)};
            float e = 0.0f;
            armreach::Extension(hand, kShoulder, aim, kLen, &e);
            const float c = Cells(e, 3.0f, 30.0f);
            if (step && c < prev) rising = false;
            prev = c;
        }
        Check(rising && Near(prev, 30.0f), "stretching the arm moves the point from near to the weapon's own, never back");
    }

    if (g_fail) { std::fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    std::puts("arm_reach_test: all passed");
    return 0;
}
