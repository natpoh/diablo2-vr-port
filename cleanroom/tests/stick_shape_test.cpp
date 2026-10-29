// The thumbsticks' dead zones against drift (vr/stick_shape.h: [direct] stick_deadzone_*_pct, stick_outer_*_pct), on synthetic stick values - no
// headset, no game:
//
//   stick_shape_test        every case; exit code 0 = all passed
//
// defaults   0 % and 100 % change nothing, bit for bit (a corner past 1, a NaN, the smallest values included): the plugin as before
// percent    FromPercent: the ranges, a value out of them, not a number
// drift      a stick resting at 0.08 reads as centred with 10 %; a real push past the dead zone still moves
// radial     the length decides, not each axis: a diagonal is kept where a per-axis cut would lose it, the direction is kept, every heading alike
// edge       just past the dead zone the length starts again from 0 (no jump); it grows steadily up to a full push at the outer edge
// outer      the outer edge saturates: from there on a full push, the direction kept; with no dead zone too
// readers    the readers after the shape keep their thresholds: the belt flick re-arms on a drifting stick only with its dead zone, a full flick still
//            counts, the pad's stick (walkflat::StickToPad) of a resting stick is 0 and of a full push is full, the game's dead zone test agrees

#include "belt_flick.h"
#include "stick_shape.h"
#include "walk_flat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace stickshape;

namespace {

int g_failed = 0, g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
float Len(float x, float y) { return std::sqrt(x * x + y * y); }

struct Xy { float x, y; };
Xy Shaped(const Zone& z, float x, float y) { Apply(z, &x, &y); return {x, y}; }

void Defaults() {
    const Zone z = FromPercent(kInnerDefaultPct, kOuterDefaultPct);
    Check(IsIdentity(z), "defaults: 0 % and 100 % is no shape");
    Check(IsIdentity(Zone{}), "defaults: a Zone as constructed is no shape");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float vals[] = {0.0f, -0.0f, 1e-7f, 0.01f, 0.08f, -0.08f, 0.3f, 0.5f, -0.7071f, 0.99f, 1.0f, -1.0f, 1.2f, nan, inf};
    bool same = true;
    for (float x : vals)
        for (float y : vals) {
            float sx = x, sy = y;
            Apply(z, &sx, &sy);
            if (!SameBits(sx, x) || !SameBits(sy, y)) same = false;
        }
    Check(same, "defaults: every stick value comes out bit for bit as it went in (as before the keys)");
    // the corners of a square-gated stick past the unit circle stay as they are with the defaults: the readers normalise them as before
    const Xy c = Shaped(z, 1.0f, 1.0f);
    Check(c.x == 1.0f && c.y == 1.0f, "defaults: a corner (1, 1) is left alone");
}

void Percent() {
    Zone z = FromPercent(10.0f, 90.0f);
    Check(Near(z.inner, 0.10f) && Near(z.outer, 0.90f), "percent: 10 % and 90 %");
    z = FromPercent(-5.0f, 150.0f);
    Check(z.inner == 0.0f && z.outer == 1.0f && IsIdentity(z), "percent: below 0 and above 100 clamp to no shape");
    z = FromPercent(80.0f, 20.0f);
    Check(Near(z.inner, 0.50f) && Near(z.outer, 0.60f), "percent: the dead zone stops at 50 %, the outer edge at 60 % (always past it)");
    z = FromPercent(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    Check(IsIdentity(z), "percent: not a number takes the defaults");
    Check(!IsIdentity(FromPercent(1.0f, 100.0f)) && !IsIdentity(FromPercent(0.0f, 99.0f)), "percent: any dead zone, or any outer edge, is a shape");
}

void Drift() {
    const Zone z = FromPercent(10.0f, 100.0f);
    const Xy rest[] = {{0.08f, 0.0f}, {0.0f, -0.08f}, {-0.0566f, 0.0566f}, {0.05f, 0.06f}, {0.1f, 0.0f}};
    bool zero = true;
    for (const Xy& r : rest) {
        const Xy s = Shaped(z, r.x, r.y);
        if (s.x != 0.0f || s.y != 0.0f) zero = false;
    }
    Check(zero, "drift: 0.08 at rest (any way, and the dead zone's own edge) reads as centred with 10 %");
    const Xy none = Shaped(FromPercent(0.0f, 100.0f), 0.08f, 0.0f);
    Check(none.x == 0.08f && none.y == 0.0f, "drift: with 0 % the drift goes on as before");
    const Xy push = Shaped(z, 0.55f, 0.0f);
    Check(Near(push.x, 0.5f) && push.y == 0.0f, "drift: a real push past it moves - 0.55 with 10 % is half a push");
    const Xy full = Shaped(z, 0.0f, 1.0f);
    Check(full.x == 0.0f && Near(full.y, 1.0f), "drift: a full push is still a full push");
}

void Radial() {
    const Zone z = FromPercent(10.0f, 100.0f);
    // (0.09, 0.09): each axis under the dead zone, the length (0.127) past it - a per-axis cut would lose it, the radial one keeps it, on its own heading
    const Xy d = Shaped(z, 0.09f, 0.09f);
    Check(d.x > 0.0f && d.y > 0.0f, "radial: a diagonal whose axes are each under the dead zone still moves (a per-axis cut would centre it)");
    Check(Near(d.x, d.y), "radial: ... on its own heading (45 degrees)");
    Check(Near(Len(d.x, d.y), (Len(0.09f, 0.09f) - 0.1f) / 0.9f), "radial: ... with the length rescaled");
    // (0.3, 0.05): a per-axis cut would turn it to (0.3, 0) - flatten it onto the axis; the radial one keeps its heading
    const Xy t = Shaped(z, 0.3f, 0.05f);
    Check(Near(std::atan2(t.y, t.x), std::atan2(0.05f, 0.3f)), "radial: a push near an axis keeps its heading (no snap onto the axis)");
    // every heading alike: the same length in, the same length out
    bool alike = true;
    for (int i = 0; i < 64; ++i) {
        const float a = (float)i * 6.2831853f / 64.0f;
        for (float r : {0.05f, 0.1f, 0.2f, 0.5f, 0.95f, 1.0f}) {
            const Xy s = Shaped(z, r * std::cos(a), r * std::sin(a));
            const float want = r <= 0.1f ? 0.0f : (r - 0.1f) / 0.9f;
            if (!Near(Len(s.x, s.y), want, 2e-5f)) alike = false;
            if (want > 0.0f && !Near(std::atan2(s.y, s.x), std::atan2(r * std::sin(a), r * std::cos(a)), 2e-4f)) alike = false;
        }
    }
    Check(alike, "radial: every heading the same length out for the same length in, the heading kept");
}

void Edge() {
    for (const Zone z : {FromPercent(10.0f, 100.0f), FromPercent(25.0f, 80.0f), FromPercent(50.0f, 60.0f)}) {
        const float in = z.inner;
        Check(ShapeLength(z, in) == 0.0f, "edge: at the dead zone itself: centred");
        const float slope = 1.0f / (z.outer - z.inner);   // how fast the length grows past the dead zone
        Check(ShapeLength(z, in + 1e-4f) > 0.0f && ShapeLength(z, in + 1e-4f) <= 1e-4f * slope * 1.01f, "edge: just past it the length starts again from 0 - no jump");
        bool steady = true;
        float was = 0.0f;
        for (int i = 0; i <= 2000; ++i) {
            const float len = (float)i / 2000.0f * 1.2f;
            const float out = ShapeLength(z, len);
            if (out < was || out - was > 1.2f / 2000.0f * slope + 1e-5f) steady = false;   // never back, never a step
            was = out;
        }
        Check(steady, "edge: from the middle to past the rim the length only grows, without a step");
        Check(Near(ShapeLength(z, (in + z.outer) * 0.5f), 0.5f), "edge: half way between the dead zone and the outer edge is half a push");
        Check(Near(ShapeLength(z, z.outer - 1e-4f), 1.0f, 1e-4f * slope * 1.01f) && ShapeLength(z, z.outer) == 1.0f, "edge: at the outer edge it meets a full push - no jump there either");
    }
}

void Outer() {
    const Zone z = FromPercent(0.0f, 80.0f);
    Check(!IsIdentity(z), "outer: 80 % is a shape");
    const Xy a = Shaped(z, 0.4f, 0.0f);
    Check(Near(a.x, 0.5f), "outer: no dead zone, 80 %: 0.4 is half a push");
    const Xy b = Shaped(z, 0.0f, -0.85f);
    Check(b.x == 0.0f && Near(b.y, -1.0f), "outer: past 80 % a full push");
    const Xy c = Shaped(z, 0.6f, 0.6f);   // length 0.85, at 45 degrees
    Check(Near(Len(c.x, c.y), 1.0f) && Near(c.x, c.y), "outer: a diagonal past it saturates on its own heading");
    const Xy d = Shaped(FromPercent(10.0f, 90.0f), 1.0f, 1.0f);   // a square-gated corner with a shape: on the unit circle, its heading kept
    Check(Near(Len(d.x, d.y), 1.0f) && Near(d.x, d.y), "outer: a corner past the rim is a full push on its heading");
    const Xy e = Shaped(z, 0.0f, 0.0f);
    Check(e.x == 0.0f && e.y == 0.0f, "outer: the middle stays the middle");
    float nx = std::numeric_limits<float>::quiet_NaN(), ny = 0.5f;
    Apply(FromPercent(10.0f, 90.0f), &nx, &ny);
    Check(std::isnan(nx) && ny == 0.5f, "outer: not a number is left alone (every reader takes it as centred)");
}

void Readers() {
    // The belt flick (vr/belt_flick.h): re-arms only back inside 0.30. A right stick worn to rest at 0.35 never re-arms - no potion from it -
    // until its dead zone takes the drift away; then a full flick past 0.70 counts as before.
    auto flick = [](const Zone& z) {
        beltflick::Machine m;
        auto step = [&](float rx, float ry) {
            float x = rx, y = ry;
            Apply(z, &x, &y);
            return m.Step(1.0f, beltflick::Stick{}, beltflick::Stick{x, y}, true);
        };
        for (int i = 0; i < 5; ++i) step(0.35f, 0.0f);   // the trigger held, the stick resting where it drifts
        return step(-1.0f, 0.0f).column;                   // a full flick to the left: belt 1
    };
    Check(flick(FromPercent(0.0f, 100.0f)) < 0, "readers: belt flick - a stick resting at 0.35 never re-arms with no dead zone (the drift's harm)");
    Check(flick(FromPercent(40.0f, 100.0f)) == 0, "readers: belt flick - with 40 % it rests centred, re-arms, and a full flick drinks belt 1");
    // a flick that only just reaches 0.8 of the throw with 20 %: 0.75 of the rest - past the flick's 0.70, so it still counts
    Check(ShapeLength(FromPercent(20.0f, 100.0f), 0.8f) >= beltflick::kOn, "readers: a push to 0.8 with 20 % is still past the flick's 0.70");
    // the pad's stick (walkflat::StickToPad, the same unit disc and range the layout's pad uses) and the game's dead zone test
    const Zone z = FromPercent(10.0f, 100.0f);
    int16_t lx = 1, ly = 1;
    Xy s = Shaped(z, 0.08f, 0.05f);
    walkflat::StickToPad(s.x, s.y, &lx, &ly);
    Check(lx == 0 && ly == 0, "readers: a drifting stick's pad stick is 0 with 10 %");
    s = Shaped(z, 0.0f, 1.0f);
    walkflat::StickToPad(s.x, s.y, &lx, &ly);
    Check(lx == 0 && ly == 32767 && walkflat::StickPushed(lx, ly), "readers: a full push is the pad's full stick, pushed past the game's dead zone");
    s = Shaped(z, 0.3f, 0.0f);   // 0.22 after the shape: under the game's dead zone (0.316) - the game does not walk on it, as it would not on 0.3 either
    walkflat::StickToPad(s.x, s.y, &lx, &ly);
    Check(!walkflat::StickPushed(lx, ly), "readers: a light tilt stays under the game's dead zone");
    walkflat::StickToPad(0.3f, 0.0f, &lx, &ly);
    Check(!walkflat::StickPushed(lx, ly), "readers: ... as it did with no shape");
}

}  // namespace

int main() {
    Defaults();
    Percent();
    Drift();
    Radial();
    Edge();
    Outer();
    Readers();
    std::printf("%d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
