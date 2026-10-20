// [bow] arrow_fire_cut (vr/arrow_fire_cut.h): the nocked arrow's own effect goes out at the release that shoots, at once or after the set delay, and lights again on the
// next arrow (a new draw, the game showing an arrow again, another skill). Synthetic frames, no game.

#include "../../vr/arrow_fire_cut.h"

#include <cmath>
#include <cstdio>

namespace {

int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

constexpr double kDt = 1.0 / 90.0;

struct Run {
    arrowcut::Cut cut;
    arrowcut::In in;
    Run(float ms) { in.delayS = arrowcut::DelayS(ms); in.skill = 7; in.arrowRead = true; }
    bool Frame(bool shot = false) { in.shot = shot; const bool lit = cut.Step(in); in.t += kDt; return lit; }
    bool For(double s) { bool lit = true; for (double e = 0.0; e < s; e += kDt) lit = Frame(); return lit; }
};

void AtOnce() {
    Run r(0.0f);
    Check(r.For(0.5), "lit at rest before any shot");
    r.in.drawing = true;
    Check(r.For(0.5), "lit while drawn");
    r.in.drawing = false;
    Check(!r.Frame(true), "0 ms: out on the very frame of the release");
    Check(!r.For(1.0), "stays out on the arrow still in the hand");
    r.in.drawing = true;
    Check(r.Frame(), "a new draw lights it");
}

void Delayed() {
    Run r(250.0f);
    r.in.drawing = true;
    r.For(0.3);
    r.in.drawing = false;
    Check(r.Frame(true), "250 ms: still lit at the release");
    Check(r.For(0.2), "still lit 0.2 s after");
    Check(!r.For(0.1), "out once 250 ms have passed");
    Check(!r.For(1.0), "and stays out");
}

void NextArrowShown() {
    Run r(0.0f);
    r.Frame(true);
    Check(!r.For(0.2), "out after the shot");
    r.in.arrowRead = false;
    Check(!r.For(0.4), "out while the game shows no arrow");
    r.in.arrowRead = true;
    Check(r.Frame(), "the next arrow nocked at rest: lit");
    Check(r.For(1.0), "and stays lit");
}

void DrawBeforeDelay() {
    Run r(1000.0f);
    r.Frame(true);
    r.For(0.3);
    r.in.drawing = true;
    Check(r.For(1.5), "a new draw before the delay ran out drops the cut");
}

void ArrowBackBeforeDelay() {
    Run r(1000.0f);
    r.Frame(true);
    r.in.arrowRead = false;
    r.For(0.2);
    r.in.arrowRead = true;
    Check(r.For(1.5), "the next arrow shown before the delay ran out drops the cut");
}

void SkillChange() {
    Run r(0.0f);
    r.Frame(true);
    Check(!r.For(0.2), "out after the shot");
    r.in.skill = -1;
    r.For(0.1);
    r.in.skill = 7;
    Check(!r.For(0.2), "a moment of no skill read, then the same one: still out");
    r.in.skill = 27;
    Check(r.Frame(), "another skill chosen lights it");
}

void Off() {
    Run r(0.0f);
    r.in.on = false;
    Check(r.Frame(true), "off: a shot changes nothing");
    Check(r.For(1.0), "off: lit");
    r.in.on = true;
    Check(r.For(0.1), "on again, no shot since: lit");
}

void DrawOnTheShotFrame() {   // the string still reads as taken on the release's frame, let go the next
    Run r(0.0f);
    r.in.drawing = true;
    r.For(0.3);
    Check(!r.Frame(true), "out on the release's frame though the string still reads as drawn");
    r.in.drawing = false;
    Check(!r.For(0.3), "stays out after the string reads as let go");
}

void Delays() {
    Check(arrowcut::DelayS(0.0f) == 0.0, "0 ms");
    Check(std::fabs(arrowcut::DelayS(150.0f) - 0.15) < 1e-6, "150 ms");
    Check(arrowcut::DelayS(-5.0f) == 0.0, "below 0 clamps");
    Check(std::fabs(arrowcut::DelayS(9000.0f) - 2.0) < 1e-6, "above the top clamps");
    Check(arrowcut::DelayS(NAN) == 0.0, "not a number: 0");
}

}  // namespace

int main() {
    AtOnce();
    Delayed();
    NextArrowShown();
    DrawBeforeDelay();
    ArrowBackBeforeDelay();
    SkillChange();
    Off();
    DrawOnTheShotFrame();
    Delays();
    if (g_fail) { std::printf("arrow_fire_cut_test: %d FAILED\n", g_fail); return 1; }
    std::printf("arrow_fire_cut_test: all passed\n");
    return 0;
}
