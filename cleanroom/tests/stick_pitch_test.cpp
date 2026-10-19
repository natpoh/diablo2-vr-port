// The right stick's up / down tilt in VR F2 ([camera] stick_pitch_f2, vr/stick_pitch.h), on synthetic sticks - no pad, no game:
//
//   stick_pitch_test        every case; exit code 0 = all passed
//
// rest      the stick at rest, inside the dead zone, or not a number: no tilt, the offset as it was
// turn      a push more sideways than up / down (a turn) never tilts; a push more up than sideways does
// shape     up looks higher, the speed at a full push, the square past the dead zone, the sign, no jump at the dead zone
// limits    the camera kept between min and max; a [camera] pitch outside them is never pulled in, and 0 is always allowed
// ini       the speed and the limits read from the ini: ranges, swapped limits, not a number

#include "stick_pitch.h"

#include <cmath>
#include <cstdio>

using namespace stickpitch;

namespace {

int g_failed = 0, g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void Rest() {
    Check(Step(0.0f, 0.0f, 0.01f, 60.0f, 1.0f) == 0.0f, "rest: a centred stick does not tilt");
    Check(Step(0.0f, 0.24f, 0.01f, 60.0f, 1.0f) == 0.0f, "rest: inside the dead zone (up)");
    Check(Step(0.0f, -0.24f, 0.01f, 60.0f, 1.0f) == 0.0f, "rest: inside the dead zone (down)");
    Check(Step(0.0f, NAN, 0.01f, 60.0f, 1.0f) == 0.0f, "rest: y not a number");
    Check(Step(NAN, 1.0f, 0.01f, 60.0f, 1.0f) == 0.0f, "rest: x not a number");
    Check(Step(0.0f, 1.0f, 0.0f, 60.0f, 1.0f) == 0.0f, "rest: no time, no tilt");
    Check(Step(0.0f, 1.0f, -1.0f, 60.0f, 1.0f) == 0.0f, "rest: time backwards, no tilt");
    const Limits l;
    Check(Apply(0.0f, 0.0f, -60.0f, l) == 0.0f, "rest: the offset stays 0");
    Check(Apply(-12.5f, 0.0f, -60.0f, l) == -12.5f, "rest: the offset is kept");
    Check(Apply(NAN, 0.0f, -60.0f, l) == 0.0f, "rest: an offset not a number goes back to 0");
}

void Turn() {
    Check(Step(1.0f, 0.0f, 0.01f, 60.0f, 1.0f) == 0.0f, "turn: full right does not tilt");
    Check(Step(-1.0f, 0.3f, 0.01f, 60.0f, 1.0f) == 0.0f, "turn: left, a little up, does not tilt");
    Check(Step(0.8f, -0.6f, 0.01f, 60.0f, 1.0f) == 0.0f, "turn: right, somewhat down, does not tilt");
    Check(Step(0.7f, 0.7f, 0.01f, 60.0f, 1.0f) == 0.0f, "turn: the exact diagonal does not tilt");
    Check(Step(0.5f, 0.8f, 0.01f, 60.0f, 1.0f) < 0.0f, "turn: more up than sideways tilts up");
}

void Shape() {
    Check(Near(Step(0.0f, 1.0f, 1.0f, 60.0f, 1.0f), -60.0f), "shape: full up for a second = 60 degrees higher");
    Check(Near(Step(0.0f, -1.0f, 1.0f, 60.0f, 1.0f), 60.0f), "shape: full down for a second = 60 degrees lower");
    Check(Near(Step(0.0f, 1.0f, 1.0f, 60.0f, -1.0f), 60.0f), "shape: sign -1 turns it round");
    Check(Near(Step(0.0f, 1.0f, 0.5f, 120.0f, 1.0f), -60.0f), "shape: speed times time");
    const float half = Step(0.0f, kDead + 0.5f * (1.0f - kDead), 1.0f, 60.0f, 1.0f);
    Check(Near(half, -15.0f), "shape: half way past the dead zone = a quarter of the speed (the square)");
    Check(std::fabs(Step(0.0f, kDead + 1e-4f, 1.0f, 60.0f, 1.0f)) < 1e-3f, "shape: no jump just past the dead zone");
    Check(Near(Step(0.0f, 1.5f, 1.0f, 60.0f, 1.0f), -60.0f), "shape: a corner past 1 is a full push, no more");
}

void LimitsCase() {
    const Limits l{-90.0f, 0.0f};
    Check(Near(Apply(0.0f, -100.0f, -60.0f, l), -30.0f), "limits: up stops at min (-60 + -30 = -90)");
    Check(Near(Apply(0.0f, 100.0f, -60.0f, l), 60.0f), "limits: down stops at max (-60 + 60 = 0)");
    Check(Near(Apply(-30.0f, -1.0f, -60.0f, l), -30.0f), "limits: held at min, more up does nothing");
    Check(Near(Apply(-30.0f, 1.0f, -60.0f, l), -29.0f), "limits: from min, down moves at once");
    // [camera] pitch -120, higher than min: the stick never pulls it in, only down towards max and back
    Check(Apply(0.0f, -5.0f, -120.0f, l) == 0.0f, "limits: base above min, up does nothing");
    Check(Near(Apply(0.0f, 5.0f, -120.0f, l), 5.0f), "limits: base above min, down moves");
    Check(Near(Apply(0.0f, 500.0f, -120.0f, l), 120.0f), "limits: base above min, down stops at max");
    // [camera] pitch 20, lower than max
    Check(Apply(0.0f, 5.0f, 20.0f, l) == 0.0f, "limits: base below max, down does nothing");
    Check(Near(Apply(0.0f, -500.0f, 20.0f, l), -110.0f), "limits: base below max, up stops at min");
    Check(Apply(0.0f, NAN, -60.0f, l) == 0.0f, "limits: a step not a number changes nothing");
}

void Ini() {
    Check(Speed(NAN) == kSpeedDefault, "ini: speed not a number = the default");
    Check(Speed(1.0f) == kSpeedMin && Speed(9999.0f) == kSpeedMax && Speed(45.0f) == 45.0f, "ini: speed range");
    const Limits d = FromIni(NAN, NAN);
    Check(d.min == kMinDefault && d.max == kMaxDefault, "ini: limits not a number = the defaults");
    const Limits s = FromIni(10.0f, -100.0f);
    Check(s.min == -100.0f && s.max == 10.0f, "ini: swapped limits are put in order");
    const Limits r = FromIni(-500.0f, 500.0f);
    Check(r.min == kPitchMin && r.max == kPitchMax, "ini: limits kept in [camera] pitch's range");
}

}  // namespace

int main() {
    Rest();
    Turn();
    Shape();
    LimitsCase();
    Ini();
    std::printf("stick_pitch_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
