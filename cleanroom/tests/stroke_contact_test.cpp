// Offline checks of the swing engine (vr/stroke_contact.h) on synthetic hand poses - no game, no headset.
// Run: build/Release/stroke_contact_test.exe   (exit code 0 = all passed)
//
//   1. SegmentDistanceSquared / TouchTime / Touches: static touch, a thin body between two samples, the outer arc
//   2. Engine: a fast straight stroke, slow hands, a slowdown inside a stroke, rest ends it, a reversal re-arms,
//      jumps / fast turns / epoch / context / time gaps drop it and 80 ms of fresh samples recover it, a
//      wrist flick with the hand in place, the same sample twice, bad blades
//   3. HitSet (once per stroke and body, a new session forgets) and PoseHistory (by stamp, stale, mixed epochs)

#include "../../vr/stroke_contact.h"
#include "../../vr/stroke_tuning.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace stroke;

static int g_failures = 0;

static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static Vec Rotate(Quat q, Vec v) {
    // v' = q v q^-1
    const Quat p{v.x, v.y, v.z, 0.0f};
    const Quat r = Multiply(Multiply(q, p), Conjugate(q));
    return {r.x, r.y, r.z};
}
static Quat AboutZ(float angle) { return {0.0f, 0.0f, std::sin(angle * 0.5f), std::cos(angle * 0.5f)}; }

// ---------------------------------------------------------------- geometry

static void GeometryTests() {
    // two segments crossing at distance 1
    Expect(Near(SegmentDistanceSquared({0, 0, 0}, {1, 0, 0}, {0.5f, -1, 1}, {0.5f, 1, 1}), 1.0f, 1e-5f), "crossing segments");
    // parallel
    Expect(Near(SegmentDistanceSquared({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}), 1.0f, 1e-5f), "parallel segments");
    // a point against a segment, a point against a point
    Expect(Near(SegmentDistanceSquared({0.5f, 2, 0}, {0.5f, 2, 0}, {0, 0, 0}, {1, 0, 0}), 4.0f, 1e-5f), "point to segment");
    Expect(Near(SegmentDistanceSquared({0, 0, 0}, {0, 0, 0}, {3, 4, 0}, {3, 4, 0}), 25.0f, 1e-4f), "point to point");
    // collinear but apart: nearest ends
    Expect(Near(SegmentDistanceSquared({0, 0, 0}, {1, 0, 0}, {3, 0, 0}, {4, 0, 0}), 4.0f, 1e-5f), "collinear apart");

    // a still blade: touching or not
    const Blade still{{0, 0, 0}, {0, 0, 1}, 0.05f};
    Expect(!Touches(still, still, Body{{0.3f, 0, 0.5f}, {0.3f, 1, 0.5f}, 0.2f}), "static: 0.3 m away, reach 0.25 -> no");
    Expect(Touches(still, still, Body{{0.3f, 0, 0.5f}, {0.3f, 1, 0.5f}, 0.3f}), "static: reach 0.35 -> yes");

    // a thin body between two samples: the blade (1 m long along z) moves 2 m along x across a pole at x = 0
    const Blade a{{-1, 0, 0}, {-1, 0, 1}, 0.04f}, b{{1, 0, 0}, {1, 0, 1}, 0.04f};
    const Body pole{{0, 0, 0.5f}, {0, 2, 0.5f}, 0.05f};
    const float t = TouchTime(a, b, pole);
    Expect(t > 0.3f && t < 0.7f, "tunnelling: the pole is met in the middle of the move", t);
    Expect(!Touches(a, b, Body{{0, 5, 0.5f}, {0, 7, 0.5f}, 0.05f}), "tunnelling: a pole far above is not met");

    // the outer arc: a 1 m blade turning from +x to +z about its grip; a post stands on the arc at 45 deg
    const Blade from{{0, 0, 0}, {1, 0, 0}, 0.03f}, to{{0, 0, 0}, {0, 0, 1}, 0.03f};
    const float s = 0.70710678f;
    const Body onArc{{s, 0, s}, {s, 1, s}, 0.05f};
    const float ta = TouchTime(from, to, onArc);
    Expect(ta > 0.3f && ta < 0.7f, "arc: the post on the outer arc is met mid-turn", ta);
    // the chord between the two tips passes 0.29 m inside the arc: a post there is also met (the blade is the whole segment)
    Expect(Touches(from, to, Body{{0.35f, 0, 0.35f}, {0.35f, 1, 0.35f}, 0.05f}), "arc: a post inside the arc is met");
    // a post beyond the tip's reach, and one behind the grip
    Expect(!Touches(from, to, Body{{1.2f, 0, 1.2f}, {1.2f, 1, 1.2f}, 0.05f}), "arc: beyond the tip is not met");
    Expect(!Touches(from, to, Body{{-0.5f, 0, -0.5f}, {-0.5f, 1, -0.5f}, 0.05f}), "arc: behind the grip is not met");
    // the first body met along the move comes first
    const Body early{{1.0f, 0, 0.1f}, {1.0f, 1, 0.1f}, 0.05f}, late{{0.1f, 0, 1.0f}, {0.1f, 1, 1.0f}, 0.05f};
    Expect(TouchTime(from, to, early) < TouchTime(from, to, late), "arc: the post near the start is met before the one near the end");
    // an exactly opposite pair falls back to the chord, with a result (no NaN)
    const Blade fwd{{0, 0, 0}, {1, 0, 0}, 0.03f}, back{{0, 0, 0}, {-1, 0, 0}, 0.03f};
    Expect(std::isfinite(TouchTime(fwd, back, Body{{0, 5, 0}, {0, 6, 0}, 0.1f})), "opposite blades: no NaN");
}

// ---------------------------------------------------------------- engine

// A hand moving in the tracking frame; the blade is a 0.65 m segment along the hand's -z, the wearer stands at
// (0, 1, 0). world == tracking in the test.
struct Rig {
    Engine eng;
    double t = 1.0;
    Vec hand{-0.5f, 1.0f, 0.0f};
    float roll = 0.0f;
    uint64_t epoch = 1, context = 1;
    float scale = 4.0f;
    float radius = 0.065f;
    float lever = -1.0f;      // Sample::lever: < 0 the blade's own length
    bool pinAnchor = false;   // false: the wearer walks along with the hand, so a long test stays inside reach
    float anchorX = 0.0f;
    unsigned sweeps = 0;
    uint64_t firstStroke = 0, lastStroke = 0;
    Sweep lastSweep;

    Sample Make() const {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = epoch;
        s.context = context;
        s.hand = {hand, AboutZ(roll)};
        const Vec axis = Rotate(s.hand.q, {0.0f, 0.0f, -0.65f});
        s.blade = {hand, hand + axis, radius};
        s.anchor = {pinAnchor ? anchorX : hand.x, 1.0f, 0.0f};
        s.scale = scale;
        s.lever = lever;
        return s;
    }
    // Advances by dt and moves the hand by `move` (m) and the roll by `turn` (rad); true when a sweep came out.
    bool Step(double dt, Vec move, float turn = 0.0f) {
        t += dt;
        hand = hand + move;
        roll += turn;
        return Feed();
    }
    bool Feed() {
        Sweep out;
        const bool got = eng.Step(Make(), out);
        if (got) {
            ++sweeps;
            if (!firstStroke) firstStroke = out.stroke;
            lastStroke = out.stroke;
            lastSweep = out;
        }
        return got;
    }
    // n steps of constant velocity v (m/s along x) at 90 Hz; the number of sweeps made.
    unsigned Run(int n, float v, double dt = 1.0 / 90.0) {
        unsigned made = 0;
        for (int i = 0; i < n; ++i) made += Step(dt, {v * static_cast<float>(dt), 0, 0}) ? 1u : 0u;
        return made;
    }
};

static void StrokeTests() {
    const double dt = 1.0 / 90.0;

    {   // a fast straight stroke
        Rig r;
        r.Feed();   // primes the engine
        unsigned before = 0, first = 0;
        Sweep firstSweep;
        for (int i = 0; i < 40; ++i) {
            const bool got = r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
            if (got && !first) { first = i + 1; firstSweep = r.lastSweep; }
            if (!got && !first) ++before;
        }
        Expect(first != 0, "fast stroke: sweeps come out");
        Expect(first >= 8, "fast stroke: nothing during the 80 ms recovery", first);
        Expect(first <= 24, "fast stroke: it begins within 0.27 s", first);
        Expect(r.sweeps > 10, "fast stroke: sweeps go on", r.sweeps);
        Expect(r.firstStroke == r.lastStroke && r.firstStroke != 0, "fast stroke: one stroke number throughout");
        Expect(Near(r.lastSweep.speed, 2.0f, 0.1f), "fast stroke: speed 2 m/s", r.lastSweep.speed);
        // the first sweep covers the wind-up: it starts at least onsetExcursion before it ends
        Expect(firstSweep.after.base.x - firstSweep.before.base.x >= 0.079f, "fast stroke: the first sweep starts where the movement began",
               firstSweep.after.base.x - firstSweep.before.base.x);
        // later sweeps are one step long
        Expect(Near(r.lastSweep.after.base.x - r.lastSweep.before.base.x, 2.0f * static_cast<float>(dt), 1e-4f), "fast stroke: later sweeps are one step long");
        Expect(r.eng.InStroke(), "fast stroke: in a stroke");
    }

    {   // slow hands never swing
        for (const float v : {0.3f, 0.5f, 0.7f}) {
            Rig r;
            r.Feed();
            r.Run(60, v);
            Expect(r.sweeps == 0, "slow hand: no stroke", v);
        }
        Rig r;   // 1.0 m/s is under the begin speed (1.2) too
        r.Feed();
        r.Run(40, 1.0f);
        Expect(r.sweeps == 0, "1.0 m/s does not begin a stroke");
    }

    {   // a stroke slowed to 0.6 m/s: no contact, but the stroke lives and picks up again with the same number
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        const uint64_t id = r.lastStroke;
        const unsigned made = r.sweeps;
        Expect(id != 0 && made > 0, "slowdown: a stroke began");
        const unsigned slow = r.Run(8, 0.6f);
        Expect(slow == 0, "slowdown: 0.6 m/s makes no contact", slow);
        Expect(r.eng.InStroke(), "slowdown: the stroke is still on");
        r.Run(6, 2.0f);
        Expect(r.lastStroke == id, "slowdown: the same stroke picks up again", static_cast<double>(r.lastStroke));
    }

    {   // rest ends the stroke; the next movement is a new one
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        const uint64_t id = r.lastStroke;
        r.Run(12, 0.0f);   // 133 ms at rest
        Expect(!r.eng.InStroke(), "rest: the stroke is over");
        r.Run(30, 2.0f);
        Expect(r.lastStroke > id, "rest: a new stroke number", static_cast<double>(r.lastStroke));
    }

    {   // a reversal re-arms: forward 0.5 s, then back at the same speed
        Rig r;
        r.Feed();
        r.Run(45, 2.0f);
        const uint64_t id = r.lastStroke;
        Expect(id != 0, "reversal: the first stroke");
        std::vector<uint64_t> ids;
        for (int i = 0; i < 30; ++i) if (r.Step(dt, {-2.0f * static_cast<float>(dt), 0, 0})) ids.push_back(r.lastSweep.stroke);
        Expect(!ids.empty(), "reversal: the way back sweeps again");
        Expect(!ids.empty() && ids.back() > id, "reversal: the way back is a new stroke", ids.empty() ? 0.0 : static_cast<double>(ids.back()));
    }

    {   // a jump of the hand drops the stroke; 80 ms of fresh samples, then a new one
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        const uint64_t id = r.lastStroke;
        r.eng.TakeDiagnostics();
        const bool got = r.Step(dt, {0.5f, 0, 0});   // 45 m/s
        Expect(!got, "jump: no sweep on the jump");
        Expect(r.eng.TakeDiagnostics().discontinuities == 1, "jump: one discontinuity counted");
        Expect(!r.eng.InStroke(), "jump: the stroke is dropped");
        unsigned within = 0;
        for (int i = 0; i < 6; ++i) within += r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0}) ? 1u : 0u;   // 66 ms
        Expect(within == 0, "jump: nothing within 80 ms", within);
        r.Run(40, 2.0f);
        Expect(r.lastStroke > id, "jump: it recovers with a new stroke", static_cast<double>(r.lastStroke));
    }

    {   // a turn faster than 60 rad/s
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        r.eng.TakeDiagnostics();
        r.Step(dt, {0, 0, 0}, 1.0f);   // 90 rad/s
        Expect(r.eng.TakeDiagnostics().discontinuities == 1, "fast turn: dropped");
        Expect(!r.eng.InStroke(), "fast turn: no stroke");
        // 40 rad/s is a real wrist: not a drop
        Rig w;
        w.Feed();
        w.Run(30, 2.0f);
        w.eng.TakeDiagnostics();
        w.Step(dt, {2.0f * static_cast<float>(dt), 0, 0}, 40.0f * static_cast<float>(dt));
        Expect(w.eng.TakeDiagnostics().discontinuities == 0, "40 rad/s: a wrist, not a drop");
    }

    {   // epoch, context, scale, time gap
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        r.eng.TakeDiagnostics();
        r.epoch = 2;
        r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
        Expect(r.eng.TakeDiagnostics().discontinuities == 1 && !r.eng.InStroke(), "epoch change drops the stroke");
        r.Run(40, 2.0f);
        r.eng.TakeDiagnostics();
        r.context = 2;
        r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
        Expect(r.eng.TakeDiagnostics().discontinuities == 1 && !r.eng.InStroke(), "context change drops the stroke");
        r.Run(40, 2.0f);
        r.eng.TakeDiagnostics();
        r.scale = 4.5f;
        r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
        Expect(r.eng.TakeDiagnostics().discontinuities == 1, "scale change drops the stroke");
        r.Run(40, 2.0f);
        r.eng.TakeDiagnostics();
        r.Step(0.2, {0.4f, 0, 0});   // a 200 ms gap
        Expect(r.eng.TakeDiagnostics().discontinuities == 1 && !r.eng.InStroke(), "a time gap drops the stroke");
    }

    {   // the same sample twice (two eyes' passes pose one stamp) changes nothing
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        const unsigned made = r.sweeps;
        Sweep out;
        const bool again = r.eng.Step(r.Make(), out);
        Expect(!again, "the same sample twice: no second sweep");
        Expect(r.eng.TakeDiagnostics().discontinuities == 0, "the same sample twice: no discontinuity");
        r.Run(2, 2.0f);
        Expect(r.sweeps == made + 2, "the same sample twice: the stroke goes on", r.sweeps);
    }

    {   // a wrist flick with the hand in place: the blade's tip carries the movement
        Rig r;
        r.Feed();
        unsigned made = 0;
        for (int i = 0; i < 20; ++i) made += r.Step(dt, {0, 0, 0}, 12.0f * static_cast<float>(dt)) ? 1u : 0u;   // 12 rad/s x 0.65 m
        Expect(made > 3, "wrist flick: the tip's speed makes a stroke", made);
        Expect(r.lastSweep.speed > 5.0f, "wrist flick: the speed is the tip's", r.lastSweep.speed);
    }

    {   // the lever of a turn is the sample's when it says so: a blade that runs on behind the grip (a staff's butt) counts its tip side only
        Rig same, shorter;
        shorter.lever = 0.2f;
        same.Feed();
        shorter.Feed();
        unsigned madeSame = 0, madeShort = 0;
        for (int i = 0; i < 20; ++i) {
            madeSame += same.Step(dt, {0, 0, 0}, 12.0f * static_cast<float>(dt)) ? 1u : 0u;
            madeShort += shorter.Step(dt, {0, 0, 0}, 12.0f * static_cast<float>(dt)) ? 1u : 0u;
        }
        Expect(madeSame > 3 && same.lastSweep.speed > 5.0f, "lever: with no lever given the blade's own length is the lever (7.8 m/s)", same.lastSweep.speed);
        Expect(Near(shorter.lastSweep.speed, 12.0f * 0.2f, 0.3f) && madeShort <= madeSame, "lever: a lever of 0.2 m makes the same turn 2.4 m/s", shorter.lastSweep.speed);
        Rig longer;
        longer.lever = 5.0f;   // held to the engine's own limit (1.5 m)
        longer.Feed();
        for (int i = 0; i < 20; ++i) longer.Step(dt, {0, 0, 0}, 12.0f * static_cast<float>(dt));
        Expect(longer.lastSweep.speed <= 12.0f * 1.5f + 0.5f, "lever: held to leverMax", longer.lastSweep.speed);
    }

    {   // bad samples reset the engine
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        Expect(r.eng.InStroke(), "bad samples: in a stroke first");
        r.radius = 0.0f;
        r.Feed();
        Expect(!r.eng.InStroke(), "a blade of no radius resets");
        r.radius = 0.065f;
        r.Run(40, 2.0f);
        Expect(r.eng.InStroke(), "the engine recovers after a bad blade");
        r.scale = 40.0f;
        r.Feed();
        Expect(!r.eng.InStroke(), "a scale out of bounds resets");
        r.scale = 4.0f;
        r.pinAnchor = true;
        r.anchorX = r.hand.x;
        r.hand.x += 5.0f;   // far from the wearer
        r.Feed();
        Expect(!r.eng.InStroke(), "a grip far from the wearer resets");
        Sample inactive = r.Make();
        inactive.active = false;
        r.pinAnchor = false;
        r.Feed();
        r.Run(40, 2.0f);
        Sweep out;
        r.eng.Step(inactive, out);
        Expect(!r.eng.InStroke(), "an inactive sample resets");
    }

    {   // stroke numbers keep counting across Reset
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        const uint64_t id = r.lastStroke;
        r.eng.Reset();
        r.Feed();
        r.Run(40, 2.0f);
        Expect(r.lastStroke > id, "Reset: stroke numbers go on", static_cast<double>(r.lastStroke));
    }

    {   // parameters apply: a lower stroke speed lets a slower hand begin with a lower begin speed
        Rig r;
        Params p;
        p.beginSpeed = 0.6f;
        p.strokeSpeed = 0.5f;
        r.eng.SetParams(p);
        r.Feed();
        r.Run(60, 1.0f);   // 1.0 m/s does not begin with the defaults (see above)
        Expect(r.sweeps > 0, "Params: a slower hand begins when the gates are lower", r.sweeps);
    }
}

// ---------------------------------------------------------------- hit set, pose history

static void SetTests() {
    HitSet h;
    Expect(h.Insert(1, 5, 7), "hitset: first insert");
    Expect(!h.Insert(1, 5, 7), "hitset: the same pair again");
    Expect(h.Contains(1, 5, 7), "hitset: contains");
    Expect(h.Insert(1, 6, 7), "hitset: another stroke, the same body");
    Expect(h.Insert(1, 5, 8), "hitset: the same stroke, another body");
    Expect(h.Insert(2, 5, 7), "hitset: a new session takes it");
    Expect(!h.Contains(1, 5, 7), "hitset: a new session forgot the old");
    for (uint32_t i = 0; i < 200; ++i) h.Insert(2, 100 + i, 1);   // the ring wraps without trouble
    Expect(h.Contains(2, 299, 1), "hitset: the newest is kept after wrapping");

    PoseHistory ph;
    auto sample = [](double t, uint64_t epoch = 1, uint64_t context = 1) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = epoch;
        s.context = context;
        return s;
    };
    ph.Push(10, sample(5.00));
    ph.Push(11, sample(5.01));
    ph.Push(12, sample(5.02));
    Sample out;
    Expect(ph.Find(11, 5.03, out) && Near(static_cast<float>(out.time), 5.01f, 1e-4f), "history: finds by stamp");
    Expect(!ph.Find(99, 5.03, out), "history: an unknown stamp");
    Expect(!ph.Find(11, 5.30, out), "history: stale");
    Expect(!ph.Find(11, 4.0, out), "history: a clock gone back");
    ph.Push(13, sample(5.03, 2));   // a new epoch: the older samples are not the same frame
    Expect(!ph.Find(11, 5.04, out), "history: an older sample of another epoch");
    Expect(ph.Find(13, 5.04, out), "history: the newest is fine");
    Sample inactive = sample(5.05);
    inactive.active = false;
    ph.Push(14, inactive);
    Expect(!ph.Find(13, 5.06, out), "history: the newest inactive -> nothing");
}

// ---------------------------------------------------------------- the user's gate: the least speed and the start distance, and what the engine says live

static void GateTests() {
    const double dt = 1.0 / 90.0;
    // the defaults ARE the engine's own numbers
    {
        const Params a{}, b = ParamsFor({0.8f, 8.0f});
        Expect(a.strokeSpeed == b.strokeSpeed && a.beginSpeed == b.beginSpeed && a.quietSpeed == b.quietSpeed && a.onsetExcursion == b.onsetExcursion,
               "gate: 0.8 m/s and 8 cm are exactly the engine's own numbers");
        Expect(Near(b.strokeSpeed, 0.8f, 1e-6f) && Near(b.beginSpeed, 1.2f, 1e-6f) && Near(b.quietSpeed, 0.45f, 1e-6f) && Near(b.onsetExcursion, 0.08f, 1e-6f), "gate: ... 0.8, 1.2, 0.45, 0.08");
        // above the default the begin and rest speeds stay (the stroke may begin as it always did, the least speed alone is higher)
        const Params hi = ParamsFor({3.0f, 8.0f});
        Expect(Near(hi.strokeSpeed, 3.0f, 1e-6f) && Near(hi.beginSpeed, 1.2f, 1e-6f) && Near(hi.quietSpeed, 0.45f, 1e-6f), "gate: a higher least speed leaves the begin and rest speeds");
        // below it they come down with it, so a gentle poke can begin a stroke and a slow hand is not "at rest"
        const Params lo = ParamsFor({0.4f, 8.0f});
        Expect(Near(lo.strokeSpeed, 0.4f, 1e-6f) && Near(lo.beginSpeed, 0.6f, 1e-5f) && Near(lo.quietSpeed, 0.225f, 1e-5f) && lo.quietSpeed < lo.strokeSpeed && lo.strokeSpeed <= lo.beginSpeed,
               "gate: a lower least speed lowers the begin speed (1.5 x) and the rest speed (0.5625 x), the order kept");
        // the start distance
        Expect(Near(ParamsFor({0.8f, 20.0f}).onsetExcursion, 0.20f, 1e-6f) && Near(ParamsFor({0.8f, 2.0f}).onsetExcursion, 0.02f, 1e-6f), "gate: the start distance is the onset excursion, cm to m");
        // the ends of the range and what is not a number
        Expect(Near(ParamsFor({0.01f, 8.0f}).strokeSpeed, 0.3f, 1e-6f) && Near(ParamsFor({99.0f, 8.0f}).strokeSpeed, 20.0f, 1e-6f) &&
               Near(ParamsFor({20.0f, 8.0f}).strokeSpeed, 20.0f, 1e-6f) && Near(ParamsFor({12.5f, 8.0f}).strokeSpeed, 12.5f, 1e-6f), "gate: the least speed is held to 0.3 .. 20 m/s");
        Expect(Near(ParamsFor({0.8f, 0.1f}).onsetExcursion, 0.02f, 1e-6f) && Near(ParamsFor({0.8f, 90.0f}).onsetExcursion, 0.30f, 1e-6f), "gate: the start distance is held to 2 .. 30 cm");
        Expect(ParamsFor({std::nanf(""), std::nanf("")}).strokeSpeed == 0.8f && Near(ParamsFor({std::nanf(""), std::nanf("")}).onsetExcursion, 0.08f, 1e-6f), "gate: not a number is the default");
        const Params keep = ParamsFor({1.0f, 8.0f}, [] { Params p; p.reverseAfter = 0.5; return p; }());
        Expect(keep.reverseAfter == 0.5, "gate: the other gates are the base's");
    }

    // the engine's telemetry: LastSpeed is the speed its gates use, Counting is "this step swept something"
    {
        Rig r;
        r.Feed();
        bool agree = true;
        int counting = 0, slowStepsOk = 0;
        for (int i = 0; i < 40; ++i) {
            const bool got = r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
            agree = agree && r.eng.Counting() == got;
            counting += got;
            if (i > 5) slowStepsOk += Near(r.eng.LastSpeed(), 2.0f, 0.05f);
        }
        Expect(agree && counting > 20, "telemetry: Counting is what Step returned, every step of a 2 m/s stroke", counting);
        Expect(slowStepsOk >= 30, "telemetry: LastSpeed is the hand's 2 m/s", slowStepsOk);
        const bool was = r.eng.Counting();
        const float speed = r.eng.LastSpeed();
        r.Feed();   // the same sample again (two eyes' passes pose one stamp)
        Expect(r.eng.Counting() == was && r.eng.LastSpeed() == speed, "telemetry: the same sample again leaves both as they were");
        r.eng.Reset();
        Expect(!r.eng.Counting() && r.eng.LastSpeed() == 0.0f, "telemetry: a reset clears both");
    }
    {   // the stroke's start ([gameplay] melee_direct_client: the hands' paths in this swing): 0 with none, set at the onset, kept, a reset clears it
        Rig r;
        r.Feed();
        Expect(r.eng.StrokeStart() == 0.0, "stroke start: none before a stroke");
        r.Run(20, 2.0f);
        const double a = r.eng.StrokeStart();
        r.Run(10, 2.0f);
        Expect(r.eng.StrokeId() != 0 && a > 0.0 && r.eng.StrokeStart() == a, "stroke start: set at the onset and kept through the stroke", a);
        r.eng.Reset();
        Expect(r.eng.StrokeStart() == 0.0, "stroke start: a reset clears it");
    }
    {   // a controller's own path: its translation since a time; a gap (tracking lost) ends it
        HandPath h;
        Expect(h.Since(0.0) == 0.0f, "hand path: nothing pushed");
        for (int i = 0; i <= 10; ++i) h.Push(i * 0.01, {i * 0.05f, 0.0f, 0.0f});   // 0.5 m in 0.1 s
        Expect(Near(h.Since(0.0), 0.5f, 1e-4f), "hand path: the whole way", h.Since(0.0));
        Expect(Near(h.Since(0.05), 0.25f, 1e-4f), "hand path: since a time", h.Since(0.05));
        for (int i = 0; i < 5; ++i) h.Push(0.1 + (i + 1) * 0.01, {0.5f, 0.0f, 0.0f});   // held still (a wrist turn moves no position)
        Expect(Near(h.Since(0.0), 0.5f, 1e-4f), "hand path: a still hand adds nothing", h.Since(0.0));
        h.Push(1.0, {3.0f, 0.0f, 0.0f});   // after a gap of 0.85 s
        Expect(Near(h.Since(0.0), 0.0f, 1e-4f), "hand path: a gap ends it (only what came after)", h.Since(0.0));
        h.Clear();
        Expect(h.Since(0.0) == 0.0f, "hand path: cleared");
    }
    {   // a drop (a jump) clears them for that step: no speed, nothing counts
        Rig r;
        r.Feed();
        r.Run(30, 2.0f);
        Expect(r.eng.Counting(), "telemetry: counting before the jump");
        r.Step(dt, {5.0f, 0, 0});   // 5 m in one step
        Expect(!r.eng.Counting() && r.eng.LastSpeed() == 0.0f, "telemetry: a jump is no speed and nothing counts");
    }

    // the least speed: below it a blade does nothing - however it moves; at and above, a contact counts
    {
        // default 0.8: a gentle 0.6 m/s never begins a stroke, never counts, and the engine still says what it saw (0.6)
        Rig r;
        r.Feed();
        int counted = 0;
        float lastSeen = 0.0f;
        for (int i = 0; i < 60; ++i) { counted += r.Step(dt, {0.6f * static_cast<float>(dt), 0, 0}) ? 1 : 0; lastSeen = r.eng.LastSpeed(); }
        Expect(counted == 0 && !r.eng.InStroke() && Near(lastSeen, 0.6f, 0.03f), "least speed 0.8: a 0.6 m/s poke does nothing, and reads 0.6", lastSeen);
    }
    {   // lowered to 0.4 (and a 4 cm start): the same gentle 0.8 m/s movement is a stroke and counts
        Rig r;
        r.eng.SetParams(ParamsFor({0.4f, 4.0f}));
        r.Feed();
        int counted = 0;
        for (int i = 0; i < 60; ++i) counted += r.Step(dt, {0.8f * static_cast<float>(dt), 0, 0}) ? 1 : 0;
        Expect(counted > 20 && r.eng.InStroke(), "least speed 0.4: a 0.8 m/s movement counts", counted);
        // ... and a 0.3 m/s one still does not (under the least speed)
        Rig q;
        q.eng.SetParams(ParamsFor({0.4f, 4.0f}));
        q.Feed();
        int slow = 0;
        for (int i = 0; i < 60; ++i) slow += q.Step(dt, {0.3f * static_cast<float>(dt), 0, 0}) ? 1 : 0;
        Expect(slow == 0, "least speed 0.4: 0.3 m/s does not", slow);
    }
    {   // raised to 3: a 2 m/s swing is a stroke (it began as it always did) but is below the least speed: nothing counts; 3.5 m/s counts
        Rig r;
        r.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        r.Feed();
        int counted = 0;
        for (int i = 0; i < 40; ++i) counted += r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0}) ? 1 : 0;
        Expect(counted == 0 && r.eng.InStroke() && !r.eng.Counting() && Near(r.eng.LastSpeed(), 2.0f, 0.05f), "least speed 3: a 2 m/s stroke is on, reads 2.0, and does not count", counted);
        int fast = 0;
        for (int i = 0; i < 20; ++i) fast += r.Step(dt, {3.5f * static_cast<float>(dt), 0, 0}) ? 1 : 0;
        Expect(fast > 10 && r.eng.Counting(), "least speed 3: speeding up to 3.5 m/s counts", fast);
        for (int i = 0; i < 20; ++i) r.Step(dt, {2.0f * static_cast<float>(dt), 0, 0});
        Expect(!r.eng.Counting(), "least speed 3: slowing back to 2 m/s stops counting");
    }
    {   // the start distance: the engine judges the last 0.12 s of the hand's path - at 1.5 m/s that is 18 cm: a stroke with a start of 8 cm,
        // never one with 25 cm (after the 80 ms the engine waits at the start)
        Rig closeRig, farRig;
        closeRig.eng.SetParams(ParamsFor({0.8f, 8.0f}));
        farRig.eng.SetParams(ParamsFor({0.8f, 25.0f}));
        closeRig.Feed();
        farRig.Feed();
        unsigned a = 0, b = 0;
        for (int i = 0; i < 40; ++i) {
            a += closeRig.Step(dt, {1.5f * static_cast<float>(dt), 0, 0}) ? 1u : 0u;
            b += farRig.Step(dt, {1.5f * static_cast<float>(dt), 0, 0}) ? 1u : 0u;
        }
        Expect(a > 5 && closeRig.eng.InStroke(), "start 8 cm: 1.5 m/s begins a stroke", a);
        Expect(b == 0 && !farRig.eng.InStroke(), "start 25 cm: 1.5 m/s (18 cm in 0.12 s) never does", b);
    }
}

// ---------------------------------------------------------------- the BLADE's own speed (Sample::room)
// A rigid weapon scripted directly: the controller (`hand`, its own turn `handYaw`) and the drawn blade (its origin `grip`, its direction `bladeYaw` in the
// ground plane, `butt` behind the origin and `length` before it) are separate, so a test can make them agree (a rigid weapon on the controller) or not (a
// weapon that lags or pivots under the hand). room == world in the test. byBlade false: the caller gives no room blade, the hand's way as ever.
static Quat AboutY(float angle) { return {0.0f, std::sin(angle * 0.5f), 0.0f, std::cos(angle * 0.5f)}; }

struct BladeRig {
    Engine eng;
    double t = 1.0;
    Vec hand{0.0f, 1.0f, 0.0f};
    Vec grip{0.0f, 1.0f, 0.0f};
    float handYaw = 0.0f, bladeYaw = 0.0f;
    float length = 0.65f, butt = 0.0f;
    bool byBlade = true;
    unsigned sweeps = 0;
    float peak = 0.0f;
    bool counted = false;
    Sweep last;

    Sample Make() const {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 1;
        s.hand = {hand, AboutY(handYaw)};
        const Vec d{std::cos(bladeYaw), 0.0f, std::sin(bladeYaw)};
        s.blade = {grip - d * butt, grip + d * length, 0.04f};
        s.anchor = {grip.x, 1.0f, 0.0f};
        s.scale = 4.0f;
        s.lever = length;
        s.room = s.blade;
        s.haveRoom = byBlade;
        return s;
    }
    bool Feed() {
        Sweep out;
        const bool got = eng.Step(Make(), out);
        if (got) { ++sweeps; last = out; }
        peak = std::max(peak, eng.LastSpeed());
        counted = counted || eng.Counting();
        return got;
    }
    bool Step(double dt, Vec dHand, Vec dGrip, float dHandYaw, float dBladeYaw) {
        t += dt;
        hand = hand + dHand;
        grip = grip + dGrip;
        handYaw += dHandYaw;
        bladeYaw += dBladeYaw;
        return Feed();
    }
    // The weapon at rest for n steps (a primed engine, past the recovery), the statistics cleared.
    void Settle(int n = 14) {
        for (int i = 0; i < n; ++i) Step(1.0 / 90.0, {}, {}, 0.0f, 0.0f);
        sweeps = 0; peak = 0.0f; counted = false;
    }
    // n steps turning the controller at wHand and the blade at wBlade rad/s, nothing else moving.
    void Spin(int n, float wBlade, float wHand) {
        const float dt = 1.0f / 90.0f;
        for (int i = 0; i < n; ++i) Step(dt, {}, {}, wHand * dt, wBlade * dt);
    }
};

static void BladeSpeedTests() {
    const double dt = 1.0 / 90.0;
    // A rigid weapon on the controller: the hand's way and the blade's say the same (the new speed is the old one where the old one was right).
    {
        BladeRig a, b;
        b.byBlade = false;
        a.Settle(); b.Settle();
        a.Spin(30, 8.0f, 8.0f); b.Spin(30, 8.0f, 8.0f);
        Expect(a.counted && b.counted && a.sweeps > 5 && b.sweeps > 5, "blade speed: a wrist flick counts either way", a.sweeps);
        Expect(Near(a.peak, 5.2f, 0.3f) && Near(b.peak, 5.2f, 0.3f), "blade speed: 8 rad/s on a 0.65 m sword is 5.2 m/s by the tip and by the hand's way", a.peak);
        Expect(Near(Length(a.last.tipVel), 5.2f, 0.3f) && a.last.haveVel && Length(a.last.baseVel) < 0.05f, "blade speed: the sweep carries the ends' velocities - the tip's, and none at the grip");
        Expect(!b.last.haveVel, "blade speed: the hand's way carries none (the stroke's own speed stands for the contact)");
    }
    // A long staff, the same turn, held at its end and in the middle: the tip speed differs as the lever does.
    {
        BladeRig end, mid;
        end.length = 1.32f; end.butt = 0.0f;
        mid.length = 0.66f; mid.butt = 0.66f;
        end.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        mid.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        end.Settle(); mid.Settle();
        end.Spin(30, 4.0f, 4.0f); mid.Spin(30, 4.0f, 4.0f);
        Expect(Near(end.peak, 5.3f, 0.3f) && Near(mid.peak, 2.64f, 0.2f), "staff: 4 rad/s is 5.3 m/s at the tip when held at the end, 2.6 m/s at either end when held in the middle", end.peak);
        Expect(Near(end.peak / mid.peak, 2.0f, 0.15f), "staff: the speeds are in the ratio of the levers", end.peak / mid.peak);
        Expect(end.sweeps > 5 && mid.sweeps == 0, "staff: with the least speed at 3 m/s the end grip counts and the middle grip does not", mid.sweeps);
    }
    // The drawn blade swings under a STILL controller (a weapon that has weight and pivots, a lagging arm): by the blade it counts, by the hand's way it never did.
    {
        BladeRig a, b;
        b.byBlade = false;
        a.Settle(); b.Settle();
        a.Spin(30, 6.0f, 0.0f); b.Spin(30, 6.0f, 0.0f);
        Expect(a.counted && a.sweeps > 5 && Near(a.peak, 3.9f, 0.3f), "still controller, swinging blade: counts by the blade's tip (3.9 m/s)", a.peak);
        Expect(!b.counted && b.sweeps == 0 && b.peak < 0.05f, "still controller, swinging blade: the hand's way read nothing", b.peak);
    }
    // The controller sweeps fast but the drawn blade hardly moves: a hand's way counted it, the blade's does not.
    {
        BladeRig a, b;
        b.byBlade = false;
        a.Settle(); b.Settle();
        for (int i = 0; i < 30; ++i) {
            a.Step(dt, {3.5f * static_cast<float>(dt), 0, 0}, {0.2f * static_cast<float>(dt), 0, 0}, 0.0f, 0.0f);
            b.Step(dt, {3.5f * static_cast<float>(dt), 0, 0}, {0.2f * static_cast<float>(dt), 0, 0}, 0.0f, 0.0f);
        }
        Expect(!a.counted && a.sweeps == 0, "fast controller, blade nearly still: by the blade nothing", a.sweeps);
        Expect(b.counted && b.sweeps > 5, "fast controller, blade nearly still: by the hand's way it counted", b.sweeps);
    }
    // A big fast arc with the hand hardly moving (a wrist flick of a 1.3 m blade): the tip's path is what counts.
    {
        BladeRig a;
        a.length = 1.3f;
        a.eng.SetParams(ParamsFor({5.0f, 8.0f}));
        a.Settle();
        for (int i = 0; i < 22; ++i) a.Step(dt, {0.04f / 22.0f, 0, 0}, {0.04f / 22.0f, 0, 0}, 7.0f * static_cast<float>(dt), 7.0f * static_cast<float>(dt));   // 7 rad/s, ~100 degrees in 0.25 s, the hand 4 cm
        Expect(a.counted && a.sweeps > 5 && Near(a.peak, 9.1f, 0.5f), "big arc, hand in place: the tip at 9 m/s counts against a least speed of 5", a.peak);
    }
    // A slow poke counts for nothing, whatever the grip: the whole blade moves slowly.
    for (const float length : {0.65f, 1.32f}) {
        for (const float butt : {0.0f, 0.66f}) {
            BladeRig a;
            a.length = length; a.butt = butt;
            a.Settle();
            for (int i = 0; i < 45; ++i) a.Step(dt, {0.5f * static_cast<float>(dt), 0, 0}, {0.5f * static_cast<float>(dt), 0, 0}, 0.0f, 0.0f);   // 0.5 m/s
            Expect(!a.counted && a.sweeps == 0 && Near(a.peak, 0.5f, 0.1f), "slow poke: 0.5 m/s of the whole weapon counts for nothing, long or short, grip at the end or not", a.peak);
        }
    }
    // A wrist wobble of a long blade: 0.3 rad/s on 1.32 m is 0.4 m/s at the tip - nothing.
    {
        BladeRig a;
        a.length = 1.32f;
        a.Settle();
        a.Spin(45, 0.3f, 0.3f);
        Expect(!a.counted && a.sweeps == 0, "slow wobble of a long blade: nothing", a.peak);
    }
    // The start distance is the TIP's: 1.5 rad/s on a 1 m blade moves the tip 18 cm in 0.12 s - a stroke at 8 cm, none at 25 cm; the controller does not have to move.
    {
        BladeRig a, b;
        a.length = b.length = 1.0f;
        a.eng.SetParams(ParamsFor({0.8f, 8.0f}));
        b.eng.SetParams(ParamsFor({0.8f, 25.0f}));
        a.Settle(); b.Settle();
        a.Spin(40, 1.5f, 0.0f); b.Spin(40, 1.5f, 0.0f);
        Expect(a.sweeps > 5 && a.eng.InStroke(), "start distance on the tip: 18 cm of tip travel begins a stroke at 8 cm", a.sweeps);
        Expect(b.sweeps == 0 && !b.eng.InStroke(), "start distance on the tip: ... and not at 25 cm", b.sweeps);
    }
    // The contact point: a blade turning about its base is slow near the grip and fast at the tip; one pivoting about its TIP has the speed at the grip end
    // - the fastest point is not where it can touch.
    {
        BladeRig a;
        a.length = 1.0f;
        a.Settle();
        a.Spin(30, 3.0f, 3.0f);
        Expect(a.sweeps > 5, "contact speed: the blade turning about its base counts", a.sweeps);
        Expect(Near(ContactSpeed(a.last, 1.0f), 3.0f, 0.2f) && Near(ContactSpeed(a.last, 0.5f), 1.5f, 0.15f) && Near(ContactSpeed(a.last, 0.25f), 0.75f, 0.1f) && ContactSpeed(a.last, 0.0f) < 0.05f,
               "contact speed: 3 m/s at the tip, half of it in the middle, a quarter at a quarter, none at the grip", ContactSpeed(a.last, 0.5f));
        Expect(Near(ContactSpeed(a.last, 5.0f), ContactSpeed(a.last, 1.0f), 1e-5f) && Near(ContactSpeed(a.last, -1.0f), ContactSpeed(a.last, 0.0f), 1e-5f), "contact speed: a place off the blade is clamped to its ends");
        // the hand's way: the stroke's speed
        Sweep none;
        none.speed = 2.5f;
        Expect(Near(ContactSpeed(none, 0.3f), 2.5f, 1e-6f), "contact speed: without the ends' velocities it is the stroke's own");
    }
    {   // pivoting about the tip: the tip stands still, the grip end moves, so the engine's (fastest-end) speed is the grip's and the speed AT the tip is none
        BladeRig a;
        a.length = 1.0f;
        const Vec tipAt{1.0f, 1.0f, 0.0f};
        float phi = 0.0f;
        auto place = [&](float angle) {   // the blade of length 1 ending at tipAt, its direction `angle`
            const Vec d{std::cos(angle), 0.0f, std::sin(angle)};
            return tipAt - d * 1.0f;
        };
        a.bladeYaw = phi; a.grip = place(phi); a.hand = a.grip; a.handYaw = phi;
        a.Settle();
        for (int i = 0; i < 30; ++i) {
            phi += 1.5f * static_cast<float>(dt);
            const Vec g = place(phi);
            a.Step(dt, g - a.hand, g - a.grip, 1.5f * static_cast<float>(dt), 1.5f * static_cast<float>(dt));
        }
        Expect(a.counted && Near(a.peak, 1.5f, 0.15f), "pivot about the tip: the blade's fastest point (the grip end) moves 1.5 m/s", a.peak);
        Expect(a.last.haveVel && Length(a.last.tipVel) < 0.05f && Near(Length(a.last.baseVel), 1.5f, 0.15f), "pivot about the tip: the tip does not move");
        Expect(ContactSpeed(a.last, 1.0f) < 0.05f && Near(ContactSpeed(a.last, 0.0f), 1.5f, 0.15f), "pivot about the tip: a touch at the tip has no speed, one at the grip end has all of it");
        // the hand's way read a pivot as a movement: hand 1.5 m/s + turn 1.5 rad/s x 1 m = up to 3 m/s
        BladeRig b;
        b.byBlade = false;
        b.length = 1.0f;
        float phi2 = 0.0f;
        b.bladeYaw = phi2; b.grip = place(phi2); b.hand = b.grip; b.handYaw = phi2;
        b.Settle();
        for (int i = 0; i < 30; ++i) {
            phi2 += 1.5f * static_cast<float>(dt);
            const Vec g = place(phi2);
            b.Step(dt, g - b.hand, g - b.grip, 1.5f * static_cast<float>(dt), 1.5f * static_cast<float>(dt));
        }
        Expect(b.peak > a.peak + 0.5f, "pivot about the tip: the hand's way overstated it (the hand's travel and its turn added up)", b.peak);
    }
}

// A long staff held in TWO hands and shaken: the controller that drives the stroke barely moves, the shaft turns about the grip between the hands, so its
// ENDS (the tip and the butt, both can strike) move fast and the middle does not. The speed is the drawn blade's, whatever the hands do.
static void ShakeTests() {
    const double dt = 1.0 / 90.0;
    const float half = 0.66f;   // the short staff: 0.66 m each way from the bone, the bone the middle of the grip
    // turned one way about its middle: both ends count, the middle does not
    {
        BladeRig a;
        a.length = half; a.butt = half;
        a.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        a.Settle();
        a.Spin(30, 6.0f, 0.0f);   // the controller still
        Expect(a.counted && a.sweeps > 5 && Near(a.peak, 3.96f, 0.3f), "shaft turned about its middle: both ends at 4 m/s count against a least speed of 3", a.peak);
        Expect(Near(ContactSpeed(a.last, 1.0f), 3.96f, 0.3f) && Near(ContactSpeed(a.last, 0.0f), 3.96f, 0.3f), "shaft turned about its middle: a touch with the tip or with the butt moves 4 m/s");
        Expect(ContactSpeed(a.last, 0.5f) < 0.1f, "shaft turned about its middle: a touch at the middle, where the hands are, moves nothing", ContactSpeed(a.last, 0.5f));
        Expect(ContactSpeed(a.last, 0.9f) > 3.0f && ContactSpeed(a.last, 0.1f) > 3.0f && ContactSpeed(a.last, 0.7f) < ContactSpeed(a.last, 0.9f), "shaft turned about its middle: the speed grows toward either end");
    }
    // shaken to and fro: +-0.5 rad about the middle at 4 Hz (12.6 rad/s at the swing's middle: 8.3 m/s at either end), the controller moving 2 cm (0.5 m/s)
    {
        BladeRig a, b;
        a.length = b.length = half; a.butt = b.butt = half;
        b.byBlade = false;
        a.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        b.eng.SetParams(ParamsFor({3.0f, 8.0f}));
        a.Settle(); b.Settle();
        float yawWas = 0.0f, xWas = 0.0f;
        unsigned fast = 0, steps = 0;
        for (int i = 1; i <= 90; ++i) {
            const double tt = static_cast<double>(i) * dt;
            const float yaw = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 4.0 * tt));
            const float x = 0.02f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 4.0 * tt));
            a.Step(dt, {x - xWas, 0, 0}, {x - xWas, 0, 0}, 0.0f, yaw - yawWas);
            b.Step(dt, {x - xWas, 0, 0}, {x - xWas, 0, 0}, 0.0f, yaw - yawWas);
            yawWas = yaw; xWas = x;
            ++steps;
            if (a.eng.LastSpeed() > 3.0f) ++fast;
        }
        Expect(a.counted && a.sweeps > 10, "shaken to and fro: the ends at up to 8 m/s count (green) in the swings", a.sweeps);
        Expect(a.peak > 7.0f && a.peak < 9.5f, "shaken to and fro: the engine reads the ends' 8 m/s", a.peak);
        Expect(fast > 40, "shaken to and fro: most of the shake is above the least speed", fast);
        Expect(!b.counted && b.sweeps == 0 && b.peak < 1.0f, "shaken to and fro: the controller's way read 0.5 m/s - it never counted", b.peak);
        (void)steps;
    }
}

// TouchTime's `along`: where on the blade the touch is.
static void TouchAlongTests() {
    const Blade blade{{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, 0.02f};
    float nearAt = -1.0f, farAt = -1.0f, none = -1.0f;
    Expect(TouchTime(blade, blade, Body{{0.25f, 0.0f, 0.0f}, {0.25f, 2.0f, 0.0f}, 0.1f}, &nearAt) >= 0.0f, "along: a post at 0.25 m is touched");
    Expect(TouchTime(blade, blade, Body{{0.9f, 0.0f, 0.0f}, {0.9f, 2.0f, 0.0f}, 0.1f}, &farAt) >= 0.0f, "along: a post at 0.9 m is touched");
    Expect(Near(nearAt, 0.25f, 0.02f) && Near(farAt, 0.9f, 0.02f), "along: the touch is 0.25 and 0.9 of the way to the tip", nearAt);
    Expect(TouchTime(blade, blade, Body{{0.5f, 0.0f, 2.0f}, {0.5f, 2.0f, 2.0f}, 0.1f}, &none) < 0.0f && none == -1.0f, "along: no touch leaves it alone");
    // the blade moving between the two samples: the place where it FIRST touches
    const Blade before{{0.0f, 1.0f, -0.5f}, {1.0f, 1.0f, -0.5f}, 0.02f}, after{{0.0f, 1.0f, 0.5f}, {1.0f, 1.0f, 0.5f}, 0.02f};
    float swept = -1.0f;
    Expect(TouchTime(before, after, Body{{0.7f, 0.0f, 0.0f}, {0.7f, 2.0f, 0.0f}, 0.1f}, &swept) >= 0.0f && Near(swept, 0.7f, 0.02f), "along: a blade swept sideways across a post meets it at the post's place", swept);
}

int main() {
    GeometryTests();
    StrokeTests();
    GateTests();
    SetTests();
    BladeSpeedTests();
    ShakeTests();
    TouchAlongTests();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("stroke_contact_test: all passed\n");
    return 0;
}
