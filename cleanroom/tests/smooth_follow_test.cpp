// Offline checks of vr/smooth_follow.h ([camera] smooth_follow, VR F4): the camera's anchor eased over a jerk of the look-at, walking followed
// exactly, a teleport / portal / recentre taken at once. Played frame by frame as vr/view/camera.cpp steps it, at 144, 90, 72, 60 and 45 frames a
// second (the headsets' and the monitor's: the flat F2 / F3 views ease too since 2026-11-23):
//  - walking, running, a charge, stopping, turning, a slope: the anchor is the look-at, every frame (no lag);
//  - a bump / a step up while standing and while running: the camera does not jump, it rises smoothly (never overshoots, never goes the wrong
//    way, every frame's move well under the step) and is on the look-at again within the ease's time;
//  - a staircase climbed: the steps come out as a smooth climb, never a frame's jump;
//  - the game shoving the hero aside: eased across the floor the same way;
//  - a teleport, a waypoint (far), a recentre (snap), a long gap: at once;
//  - the bounds, not-a-number, Apply for the frame's other passes, the ease's time's range;
//  - which views ease: first and third person (VR F2 / F4 / F5, flat F2 / F3), not the game's own camera, the table or the view from above.
// Run: build/Release/smooth_follow_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../../vr/smooth_follow.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace smoothfollow;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool Same(const float a[3], const float b[3], float eps = 1e-4f) { return Near(a[0], b[0], eps) && Near(a[1], b[1], eps) && Near(a[2], b[2], eps); }

// The look-at as the game gives it for a place on the ground (x, z) at height y: the height pushed along its view (vr/flat_ground.h), and back.
// The tests speak of the hero's place and height; StepG feeds the look-at as the game has it and reads the camera's anchor back on the same terms.
static Result StepG(State& s, const float ground[3], float dt, float ms, bool snap, float outGround[3], float outLook[3] = nullptr) {
    float look[3], o[3];
    Shear(ground, look);
    const Result r = Step(s, look, dt, ms, snap, o);
    Unshear(o, outGround);
    if (outLook) { outLook[0] = o[0]; outLook[1] = o[1]; outLook[2] = o[2]; }
    return r;
}

// One run: the state, the frame time, the ease's time; out on the ground's terms, look the anchor as the camera takes it.
// The behaviour checks below were tuned at a 200 ms ease; the shipped default became 500 ms (Serg 2026-11-24: "по умолчанию как у меня 0.5с").
constexpr float kTestMs = 200.0f;

struct Run {
    State s;
    float dt, ms;
    float out[3] = {}, look[3] = {};
    Result last;
    Run(float fps, float easeMs = kTestMs) : dt(1.0f / fps), ms(easeMs) {}
    Result Step(float x, float y, float z, bool snap = false) {
        const float g[3] = {x, y, z};
        last = StepG(s, g, dt, ms, snap, out, look);
        return last;
    }
};

// Walking of every kind: across the floor the anchor is the look-at, exactly, once the walk is steady (the tracker predicts it); a start, a stop or a
// turn lags or overshoots a little (well under a step's length) and settles within the ease's time; up and down it follows the floor on the spring, a
// slope a few centimetres behind (2 v / w), never more.
static void TestWalkingFollowedExactly() {
    for (float fps : {144.0f, 90.0f, 72.0f, 60.0f, 45.0f}) {
        Run r(fps);
        float x = 100.0f, y = 5.0f, z = 200.0f;
        r.Step(x, y, z);
        CHECK(r.last.did == Did::Started);
        bool exact = true, eased = false, behind = true;
        float turnMost = 0.0f;
        const float w = 6.6f / (r.ms * 0.001f);
        auto walk = [&](float vx, float vy, float vz, float secs) {
            const int frames = (int)(secs * fps);
            for (int f = 0; f < frames; ++f) {
                x += vx * r.dt; y += vy * r.dt; z += vz * r.dt;
                r.Step(x, y, z);
                const float across = std::hypot(r.out[0] - x, r.out[2] - z);
                turnMost = std::fmax(turnMost, across);
                if (f == frames - 1) exact = exact && across < 1e-3f;   // steady by the segment's end: exactly on him
                behind = behind && std::fabs(r.out[1] - y) <= 2.0f * 4.0f / w + 4.0f * r.dt + 0.02f;   // (a 4 units/s slope: 2 v / w and a frame, ~0.3 units at 200 ms)
                eased = eased || r.last.did == Did::Eased;
            }
        };
        walk(0, 0, 0, 0.3f);          // standing
        walk(7, 0, 0, 1.0f);          // walking
        walk(28, 0, 0, 1.0f);         // from a walk to a run at once (Serg's hero: ~7 m/s room metres, 28 units/s)
        walk(0, 0, 28, 1.0f);         // a right-angle turn at a run
        walk(-20, 0, 20, 1.0f);       // and another
        walk(0, 0, 0, 0.5f);          // a dead stop
        walk(28, 0, 0, 0.5f);         // from a stand to a run at once
        walk(60, 0, 0, 0.5f);         // a charge / Burst of Speed: faster at once
        walk(20, 4, 0, 1.0f);         // up a slope
        walk(20, 0, 0, 0.5f);         // level on top
        walk(20, -4, 0, 1.0f);        // down the other side
        walk(20, 0, 0, 1.0f);         // level again
        CHECK(exact);
        CHECK(turnMost < 0.6f);   // a start, a stop, a turn at a run, a charge: never more than ~15 cm off him
        if (!exact || turnMost >= 0.6f) std::printf("  walking %.0f fps: steady exact %d, a change's most %.3f\n", fps, (int)exact, turnMost);
        CHECK(behind);
        CHECK(!eased);
        CHECK(Near(r.s.off[0], 0.0f, 1e-3f) && Near(r.s.off[1], 0.0f, 1e-3f) && Near(r.s.off[2], 0.0f, 1e-3f));
        // a sharp crest (up straight into down, 8 units/s turned round in a frame): the height rounds it within the spring's lag, back on the floor after
        walk(20, 4, 0, 0.5f);
        float most = 0.0f;
        for (int f = 0; f < (int)(0.5f * fps); ++f) {
            x += 20.0f * r.dt; y -= 4.0f * r.dt;
            r.Step(x, y, z);
            most = std::fmax(most, std::fabs(r.out[1] - y));
        }
        CHECK(most < 2.0f * 8.0f / w);                               // never more than the spring's lag of the turn round
        CHECK(std::fabs(r.out[1] - y) <= 2.0f * 4.0f / w + 4.0f * r.dt + 0.02f);   // and down the slope as any slope
    }
}

// A bump while standing: the look-at jumps up 1.5 units (~0.4 m) in a frame. The camera does not jump; it rises smoothly to it.
static void TestBumpEased(float fps, float rise, float vx) {
    Run r(fps);
    float x = 0.0f, y = 10.0f, z = 0.0f;
    r.Step(x, y, z);
    for (int f = 0; f < 20; ++f) { x += vx * r.dt; r.Step(x, y, z); }
    const float before = r.out[1];
    x += vx * r.dt; y += rise;
    r.Step(x, y, z);
    CHECK(r.last.did == Did::Eased);
    const float tolV = std::fmax(kDvV * r.dt, kMinJerkV);
    CHECK(std::fabs(r.out[1] - before) <= tolV + 1e-4f);              // this frame: only what a slope begun (or the least jerk) could move
    CHECK(std::fabs(r.last.jerkV) >= std::fabs(rise) - tolV - 1e-4f);
    CHECK(Near(r.out[0], x, 1e-3f));                                // across the floor untouched
    // then: never the wrong way, never past it, no frame's move a big share of the step, there by the ease's time
    float prev = r.out[1], biggest = 0.0f;
    bool monotone = true, over = false;
    const int frames = (int)(fps * r.ms * 0.001f);
    for (int f = 0; f < frames; ++f) {
        x += vx * r.dt;
        r.Step(x, y, z);
        const float step = r.out[1] - prev;
        monotone = monotone && (rise > 0 ? step >= -1e-5f : step <= 1e-5f);
        over = over || (rise > 0 ? r.out[1] > y + 1e-4f : r.out[1] < y - 1e-4f);
        biggest = std::fmax(biggest, std::fabs(step));
        prev = r.out[1];
        CHECK(Near(r.out[0], x, 1e-3f));
    }
    CHECK(monotone);
    CHECK(!over);
    CHECK(biggest < 0.35f * std::fabs(rise) * (90.0f / fps) + 1e-4f);
    CHECK(std::fabs(r.out[1] - y) < 0.03f * std::fabs(rise));        // ~1 - 2 % left at the ease's time
    for (int f = 0; f < (int)fps; ++f) { x += vx * r.dt; r.Step(x, y, z); }
    CHECK(Near(r.out[1], y, 1e-3f));
    CHECK(r.last.did == Did::Followed);
}

// A staircase at a run: a 1-unit step every 0.12 s. No frame's camera move is a jump; the camera climbs, ending on the top.
static void TestStairs() {
    for (float fps : {90.0f, 45.0f}) {
        Run r(fps);
        float x = 0.0f, y = 0.0f;
        r.Step(x, y, 0.0f);
        float prev = r.out[1], biggest = 0.0f;
        bool down = false;
        const int every = (int)(0.12f * fps);
        for (int f = 0; f < 10 * every; ++f) {
            x += 20.0f * r.dt;
            if (f % every == 0) y += 1.0f;
            r.Step(x, y, 0.0f);
            biggest = std::fmax(biggest, r.out[1] - prev);
            down = down || r.out[1] < prev - 1e-5f;
            prev = r.out[1];
        }
        CHECK(!down);
        CHECK(biggest < 0.45f * (90.0f / fps));
        CHECK(std::fabs(r.s.off[1]) <= kMaxOffV + 1e-4f);
        for (int f = 0; f < (int)fps; ++f) { x += 20.0f * r.dt; r.Step(x, y, 0.0f); }
        CHECK(Near(r.out[1], y, 1e-3f));
    }
}

// The game shoving the hero 1.5 units aside in a frame while he runs: across the floor eased the same way, the run itself followed.
static void TestShoveAside() {
    Run r(90.0f);
    float x = 0.0f, z = 0.0f;
    r.Step(x, 0.0f, z);
    for (int f = 0; f < 30; ++f) { x += 28.0f * r.dt; r.Step(x, 0.0f, z); }
    const float zBefore = r.out[2];
    x += 28.0f * r.dt; z += 1.5f;
    r.Step(x, 0.0f, z);
    CHECK(r.last.did == Did::Eased);
    CHECK(std::fabs(r.out[2] - zBefore) < 0.6f);
    CHECK(r.last.jerkH > 0.9f);
    for (int f = 0; f < 18; ++f) { x += 28.0f * r.dt; r.Step(x, 0.0f, z); }
    CHECK(Near(r.out[2], z, 0.03f));
    CHECK(Near(r.out[0], x, 0.03f));
    for (int f = 0; f < 90; ++f) { x += 28.0f * r.dt; r.Step(x, 0.0f, z); }
    CHECK(Near(r.out[2], z, 1e-3f) && Near(r.out[0], x, 1e-3f));
}

// A teleport, a waypoint, a portal, a recentre, a long gap: at once, nothing left eased.
static void TestSnaps() {
    Run r(90.0f);
    r.Step(0.0f, 0.0f, 0.0f);
    for (int f = 0; f < 10; ++f) r.Step(0.0f, 0.0f, 0.0f);
    r.Step(0.0f, 1.5f, 0.0f);   // a bump: being eased
    CHECK(r.last.did == Did::Eased && r.s.off[1] < -1.0f);
    // a Teleport 10 units away: at once, the bump's ease dropped with it
    r.Step(10.0f, 1.5f, 0.0f);
    CHECK(r.last.did == Did::Snapped);
    { const float raw[3] = {10.0f, 1.5f, 0.0f}; CHECK(Same(r.out, raw)); }
    CHECK(Near(r.s.off[1], 0.0f) && Near(r.s.vel[0], 0.0f));
    // just past the snap distance across the floor: at once; just under it: eased
    r.Step(10.0f + kSnapH + 0.01f, 1.5f, 0.0f);
    CHECK(r.last.did == Did::Snapped);
    r.Step(10.0f + kSnapH + 0.01f + 2.0f, 1.5f, 0.0f);
    CHECK(r.last.did == Did::Eased);
    // a waypoint / town portal / another area: hundreds of units, up or down too
    r.Step(5000.0f, -40.0f, 3000.0f);
    CHECK(r.last.did == Did::Snapped);
    { const float raw[3] = {5000.0f, -40.0f, 3000.0f}; CHECK(Same(r.out, raw)); }
    // a big drop straight down (a level's floor changing under him by more than kSnapV): at once
    r.Step(5000.0f, -40.0f - kSnapV - 0.5f, 3000.0f);
    CHECK(r.last.did == Did::Snapped);
    // the recentre (the caller's snap): at once even for a small move
    r.Step(5000.0f, -51.0f, 3000.0f);
    CHECK(r.last.did == Did::Eased);
    r.Step(5000.0f, -51.0f, 3000.0f, true);
    CHECK(r.last.did == Did::Snapped);
    { const float raw[3] = {5000.0f, -51.0f, 3000.0f}; CHECK(Same(r.out, raw)); }
    // a long gap (a loading screen): at once
    r.Step(5000.0f, -51.0f, 3000.0f);
    r.Step(5000.0f, -49.5f, 3000.0f);
    CHECK(r.last.did == Did::Eased);
    r.dt = 0.5f;
    r.Step(5000.0f, -48.0f, 3000.0f);
    CHECK(r.last.did == Did::Snapped);
    r.dt = 1.0f / 90.0f;
    // a zero / bad frame time: at once (never a division by it)
    r.dt = 0.0f;
    r.Step(5000.0f, -47.0f, 3000.0f);
    CHECK(r.last.did == Did::Snapped);
    r.dt = NAN;
    r.Step(5000.0f, -46.0f, 3000.0f);
    CHECK(r.last.did == Did::Snapped);
    r.dt = 1.0f / 90.0f;
    // not a number in the look-at: passed through, and the next good one starts afresh
    r.Step(NAN, 0.0f, 0.0f);
    CHECK(r.last.did == Did::Snapped && !r.s.has);
    r.Step(1.0f, 2.0f, 3.0f);
    CHECK(r.last.did == Did::Started);
    { const float raw[3] = {1.0f, 2.0f, 3.0f}; CHECK(Same(r.out, raw)); }
}

// A run of jerks never leaves the camera past the bounds.
static void TestBounds() {
    Run r(90.0f, kMaxMs);
    float y = 0.0f;
    r.Step(0.0f, y, 0.0f);
    for (int f = 0; f < 40; ++f) {
        y += 2.0f;   // a 2-unit jump every frame (no real floor does this): eased each time, held within kMaxOffV
        r.Step(0.0f, y, 0.0f);
        CHECK(std::fabs(r.out[1] - y) <= kMaxOffV + 1e-3f);
    }
    float x = 0.0f;
    for (int f = 0; f < 40; ++f) {
        x += 3.0f;
        r.Step(x, y, 0.0f);
        CHECK(std::hypot(r.out[0] - x, r.out[2]) <= kMaxOffH + 1e-3f);
    }
}

// Apply: the frame's other passes get the same anchor; with no state, the look-at.
static void TestApply() {
    Run r(90.0f);
    r.Step(0.0f, 0.0f, 0.0f);
    r.Step(0.0f, 0.0f, 0.0f);
    r.Step(0.0f, 1.5f, 0.0f);
    const float ground[3] = {0.0f, 1.5f, 0.0f};
    float raw[3], again[3];
    Shear(ground, raw);   // (the look-at as the game gives it)
    Apply(r.s, raw, again);
    CHECK(Same(again, r.look, 1e-5f));
    State none;
    Apply(none, raw, again);
    CHECK(Same(again, raw, 1e-6f));
}

// The ease's time: within its range; a longer time eases slower.
static void TestEaseTime() {
    CHECK(Near(Ms(10.0f), kMinMs) && Near(Ms(5000.0f), kMaxMs) && Near(Ms(NAN), kDefMs) && Near(kDefMs, 500.0f) && Near(Ms(300.0f), 300.0f));
    float left[2];
    const float times[2] = {100.0f, 500.0f};
    for (int i = 0; i < 2; ++i) {
        Run r(90.0f, times[i]);
        r.Step(0.0f, 0.0f, 0.0f);
        r.Step(0.0f, 0.0f, 0.0f);
        r.Step(0.0f, 1.5f, 0.0f);
        for (int f = 0; f < 9; ++f) r.Step(0.0f, 1.5f, 0.0f);   // 0.1 s after the step
        left[i] = 1.5f - r.out[1];
    }
    CHECK(left[0] < 0.05f);          // 100 ms: there
    CHECK(left[1] > 0.5f);           // 500 ms: still on its way
}

// Which views ease (Serg 2026-11-23: "она должна работать и в 2д виде").
static void TestViews() {
    CHECK(Eases(ViewKind::FirstPerson));    // VR F4, VR F5, flat F3
    CHECK(Eases(ViewKind::ThirdPerson));    // VR F2, flat F2
    CHECK(!Eases(ViewKind::GameOwn));       // flat F1 / VR F1: the game's own orthographic view from above
    CHECK(!Eases(ViewKind::Table));         // VR F3: the diorama on the floor
    CHECK(!Eases(ViewKind::Above));         // F1 through our camera (Real geometry, native 3D F1)
}

// The views from inside: the upper body down to the waist glides with the camera (Serg 2026-11-23: "вся верхняя часть тела", "до пояса").
static void TestInside() {
    CHECK(Inside(ViewKind::FirstPerson));   // VR F4, VR F5, flat F3
    CHECK(!Inside(ViewKind::ThirdPerson) && !Inside(ViewKind::GameOwn) && !Inside(ViewKind::Table) && !Inside(ViewKind::Above));   // behind him only the camera
}

// A world offset in the hero's model axes: turned, scaled, mirrored model matrices; moved back through the rows it is the world offset again.
static void TestToModel() {
    for (float yaw : {0.0f, 0.7f, -2.3f})
        for (float scale : {1.0f, 2.94f})
            for (bool mirror : {false, true}) {
                const float c = std::cos(yaw), sn = std::sin(yaw), mx = mirror ? -1.0f : 1.0f;
                const float m[16] = {mx * c * scale, 0, mx * -sn * scale, 0,  0, scale, 0, 0,  sn * scale, 0, c * scale, 0,  100, 7, -50, 1};
                const float d[3] = {0.4f, -1.5f, 0.9f};
                float u[3];
                CHECK(ToModel(m, d, u));
                float back[3];
                for (int k = 0; k < 3; ++k) back[k] = u[0] * m[k] + u[1] * m[4 + k] + u[2] * m[8 + k];   // (model -> world, rows)
                CHECK(Same(back, d, 1e-4f));
                CHECK(Near(u[1], -1.5f / scale, 1e-5f));   // up stays up, at his scale
            }
    const float flat[16] = {1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};
    const float d[3] = {1, 1, 1};
    float u[3] = {5, 5, 5};
    CHECK(!ToModel(flat, d, u) && u[0] == 0.0f && u[1] == 0.0f && u[2] == 0.0f);   // a row with no length: nothing
}

// The waist: the hips and legs none, the chest and all above it (arms hanging lower too) the whole offset, the belly between, smoothly.
static void TestWaist() {
    CHECK(Near(WaistWeight(1.0f, 3.0f, 4.5f), 0.0f) && Near(WaistWeight(3.0f, 3.0f, 4.5f), 0.0f) && Near(WaistWeight(4.5f, 3.0f, 4.5f), 1.0f) &&
          Near(WaistWeight(9.0f, 3.0f, 4.5f), 1.0f) && Near(WaistWeight(3.75f, 3.0f, 4.5f), 0.5f));
    CHECK(Near(WaistWeight(2.0f, 3.0f, 3.0f), 1.0f));   // no chest above the pelvis: all of it
    float last = -1.0f;
    bool rising = true;
    for (int i = 0; i <= 30; ++i) { const float w = WaistWeight(3.0f + 0.05f * i, 3.0f, 4.5f); rising = rising && w >= last; last = w; }
    CHECK(rising);
    // a rig as the game's: 0 pelvis (root), 1-2 right thigh / shin, 3-4 left, 5 spine_01, 6 spine_02, 7 chest (spine_03), 8 neck, 9 head,
    // 10 right collarbone, 11 upper arm, 12 forearm, 13 hand (hanging at the hip), 14 the weapon in it, 15 left collarbone, 16 left hand
    const int parent[17] = {-1, 0, 1, 0, 3, 0, 5, 6, 7, 8, 7, 10, 11, 12, 13, 7, 15};
    const char upper[17] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    const float bindY[17] = {3.0f, 2.0f, 1.0f, 2.0f, 1.0f, 3.3f, 3.9f, 4.5f, 5.2f, 5.8f, 4.8f, 4.7f, 3.8f, 3.0f, 2.6f, 4.8f, 3.0f};
    float w[17];
    UpperWeights(17, parent, upper, bindY, bindY[0], bindY[7], w);
    CHECK(w[0] == 0.0f && w[1] == 0.0f && w[2] == 0.0f && w[3] == 0.0f && w[4] == 0.0f);   // the hips and legs stay on the hero
    CHECK(w[5] > 0.0f && w[5] < 0.3f && w[6] > w[5] && w[6] < 1.0f && Near(w[7], 1.0f));      // the belly between, the chest whole
    CHECK(Near(w[8], 1.0f) && Near(w[9], 1.0f));                                               // the neck and the head with the camera
    for (int i = 10; i <= 16; ++i) CHECK(Near(w[i], 1.0f));                                    // the arms, the hands and the weapon whole, hanging low or not
    CHECK(w[5] - w[0] < 0.3f && w[6] - w[5] < 0.7f && w[7] - w[6] < 0.7f);                     // no bone takes much more than the one under it
    // the offset added by the shares: the pelvis where it was, the hand moved by all of it
    const float off[3] = {0.0f, -1.2f, 0.4f};
    CHECK(Near(w[0] * off[1], 0.0f) && Near(w[13] * off[1], -1.2f) && Near(w[13] * off[2], 0.4f));
}

// Serg on 0.222.4, flat F3: "на 2д бегаю все трясется". A steady run must come out exactly as the look-at, every frame - zero offset, zero jitter -
// at every rate, with the game's frames uneven, with the game's frame time and ours a few milliseconds apart, over a floor not quite flat, and with
// a second step in the same frame. (0.222.4 stepped at whichever reader came first in the frame - the pose or the view - so the time between steps
// jumped about while the hero moved evenly; now VrFrame steps it once a game frame, and nothing under kMinJerkH / kMinJerkV is ever a jerk.)
static void TestSteadyRunNoJitter() {
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xFFFF) / 65535.0f; };   // 0 .. 1
    for (float fps : {144.0f, 90.0f, 72.0f, 60.0f, 45.0f, 40.0f}) {
        for (float speed : {7.0f, 28.0f, 45.0f}) {        // a walk, Serg's run, a fast run
            for (int kind = 0; kind < 5; ++kind) {        // 0 even frames; 1 uneven (50 - 150 %); 2 + our time 4 ms off the game's; 3 + a floor not quite
                                                          // flat (+-4 cm a frame); 4 + the jungle's floor: a step of 1 - 8 cm up or down every few frames
                State s;
                float x = 0.0f, y = 3.0f, z = 0.0f, out[3], ground = 0.0f;
                const float raw0[3] = {x, y, z};
                StepG(s, raw0, 0.0f, kTestMs, false, out);
                float worstH = 0.0f, worstV = 0.0f, rawDy = 0.0f, camDy = 0.0f, lastRaw = y, lastCam = y;
                float rawD2 = 0.0f, camD2 = 0.0f, lastRawMove = 0.0f, lastCamMove = 0.0f;   // the shake: how sharply the height's move changes frame to frame
                double rawSp = 0, rawSp2 = 0, camSp = 0, camSp2 = 0;   // across the floor: the speed frame to frame, by the time we measured (as it is shown)
                float lastRawX = x, lastCamX = x;
                int spN = 0;
                int eased = 0;
                for (int f = 0; f < (int)(5.0f * fps); ++f) {
                    const bool measured = f >= (int)fps;                                // (the first second: up to speed - a start, not a steady run)
                    const float game = (kind >= 1 ? 0.5f + rnd() : 1.0f) / fps;        // the game's frame time: how far he really went
                    const float ours = kind >= 2 ? std::fmax(0.003f, game + (rnd() - 0.5f) * 0.008f) : game;   // the time we measured
                    x += speed * 0.8f * game; z += speed * 0.6f * game;
                    if (kind == 4 && rnd() < 0.3f) ground += (rnd() < 0.5f ? -1.0f : 1.0f) * (0.04f + 0.28f * rnd());
                    if (kind == 4) ground = std::fmax(-0.6f, std::fmin(0.6f, ground));
                    const float floor = kind == 3 ? (rnd() - 0.5f) * 0.08f : kind == 4 ? ground : 0.0f;
                    const float raw[3] = {x, y + floor, z};
                    const Result res = StepG(s, raw, ours, kTestMs, false, out);
                    if (measured) {
                        eased += res.did == Did::Eased;
                        worstH = std::fmax(worstH, std::fmax(std::fabs(out[0] - raw[0]), std::fabs(out[2] - raw[2])));
                        worstV = std::fmax(worstV, std::fabs(out[1] - raw[1]));
                        rawDy = std::fmax(rawDy, std::fabs(raw[1] - lastRaw));
                        camDy = std::fmax(camDy, std::fabs(out[1] - lastCam));
                        rawD2 = std::fmax(rawD2, std::fabs((raw[1] - lastRaw) - lastRawMove));
                        camD2 = std::fmax(camD2, std::fabs((out[1] - lastCam) - lastCamMove));
                        const double rs = (raw[0] - lastRawX) / ours, cs = (out[0] - lastCamX) / ours;
                        rawSp += rs; rawSp2 += rs * rs; camSp += cs; camSp2 += cs * cs; ++spN;
                    }
                    lastRawX = raw[0]; lastCamX = out[0];
                    lastRawMove = raw[1] - lastRaw; lastCamMove = out[1] - lastCam;
                    lastRaw = raw[1]; lastCam = out[1];
                }
                const double rawSpread = std::sqrt(std::fmax(0.0, rawSp2 / spN - (rawSp / spN) * (rawSp / spN)));
                const double camSpread = std::sqrt(std::fmax(0.0, camSp2 / spN - (camSp / spN) * (camSp / spN)));
                const bool evenH = kind <= 1 ? worstH < 1e-4f : (camSpread < 0.85 * rawSpread + 0.05 && worstH < 0.35f);
                CHECK(evenH);   // across the floor: the game's time ours - exactly the look-at; ours off the game's - the camera moves more evenly than he does
                if (kind <= 2) {
                    CHECK(eased == 0 && worstV < 1e-4f);       // a flat floor: nothing at all
                } else {
                    CHECK(camD2 < 0.4f * rawD2 + 1e-4f);       // the floor's unevenness: the camera's height changes its move far less sharply (no shake)
                    CHECK(camDy < 0.75f * rawDy + 1e-4f);      // and never jumps as far in a frame
                    CHECK(worstV < 1.2f);                      // and stays within the floor's own spread (+-0.6 units in case 4)
                }
                if (!evenH || (kind <= 2 && (eased || worstV >= 1e-4f)) || (kind > 2 && (camD2 >= 0.4f * rawD2 + 1e-4f || camDy >= 0.75f * rawDy + 1e-4f)))
                    std::printf("  steady run %.0f fps, %.0f units/s, case %d: %d eased, across %.4f, height %.4f, a frame %.4f of the floor's %.4f, sharpness %.4f of %.4f\n",
                                fps, speed, kind, eased, worstH, worstV, camDy, rawDy, camD2, rawD2);
            }
        }
    }
    // a second step in the same game frame (dt under kMinDt) changes nothing - mid-ease too
    State s;
    float out[3], again[3];
    const float a[3] = {0.0f, 0.0f, 0.0f}, b[3] = {0.0f, 1.5f, 0.0f};
    StepG(s, a, 0.0f, kTestMs, false, out);
    StepG(s, a, 1.0f / 60.0f, kTestMs, false, out);
    StepG(s, b, 1.0f / 60.0f, kTestMs, false, out);
    const State before = s;
    const Result r = StepG(s, b, 0.0005f, kTestMs, false, again);
    CHECK(r.did == Did::Followed && Same(again, out, 1e-6f) && Same(s.off, before.off, 1e-7f) && Same(s.vel, before.vel, 1e-7f));
}

// Serg on 0.222.6 (smooth_follow_ms 1000, flat): "всё равно всё дёргается как и раньше". If the hero's place moves in the game's 25 Hz ticks while
// the frames come at 40 - 144 a second, the look-at stands still for a frame or two and then jumps (stop-go): the camera on it judders. The
// tracker must make such a run even - its speed frame to frame a small part of the look-at's spread - at the default ease and at the most, and stay
// near him (a tick's step behind at most, ~1.1 units at a run).
static void TestTickSteppedRun() {
    for (float ms : {200.0f, 1000.0f}) {
        for (float fps : {144.0f, 90.0f, 60.0f, 40.0f}) {
            for (float speed : {28.0f, 45.0f}) {
                State s;
                float out[3];
                const float dt = 1.0f / fps, tick = 1.0f / 25.0f;
                double t = 0.0;
                float shown = 0.0f;   // the place as the game last ticked it
                const float raw0[3] = {0.0f, 3.0f, 0.0f};
                StepG(s, raw0, 0.0f, ms, false, out);
                double rawSp = 0, rawSp2 = 0, camSp = 0, camSp2 = 0;
                float lastRaw = 0.0f, lastCam = 0.0f, worst = 0.0f;
                int n = 0;
                for (int f = 0; f < (int)(6.0f * fps); ++f) {
                    t += dt;
                    shown = speed * (float)(std::floor(t / tick) * tick);   // the place moves only at the ticks
                    const float raw[3] = {shown, 3.0f, 0.0f};
                    StepG(s, raw, dt, ms, false, out);
                    if (t > 2.0) {   // (after two seconds: steady)
                        const double rs = (raw[0] - lastRaw) / dt, cs = (out[0] - lastCam) / dt;
                        rawSp += rs; rawSp2 += rs * rs; camSp += cs; camSp2 += cs * cs; ++n;
                        worst = std::fmax(worst, std::fabs(out[0] - speed * (float)t));   // against where he really is by now
                    }
                    lastRaw = raw[0]; lastCam = out[0];
                }
                const double rawMean = rawSp / n, camMean = camSp / n;
                const double rawSpread = std::sqrt(std::fmax(0.0, rawSp2 / n - rawMean * rawMean));
                const double camSpread = std::sqrt(std::fmax(0.0, camSp2 / n - camMean * camMean));
                const double share = ms >= 1000.0f ? 0.08 : 0.65;   // (more ease, more even: at the most almost none of it left)
                const bool ok = camSpread < share * rawSpread + 0.5 && std::fabs(camMean - speed) < 0.5 && worst < speed * tick + 0.6f;
                CHECK(ok);
                if (!ok) std::printf("  ticks %.0f ms, %.0f fps, %.0f units/s: speed %.1f spread %.2f (the look-at's %.2f), most %.2f behind\n",
                                     ms, fps, speed, camMean, camSpread, rawSpread, worst);
            }
        }
    }
}

// Serg on 0.222.6: "рывки вправо влево", "при ходьбе когда я захожу на мост" - and "в игре много может быть таких мест". Onto a bridge the floor's
// height steps (0.85 a frame in his log) and the game pushes a height along its own view (x + 1.22 y, z + 1.22 y): the look-at jumps ~1 unit
// sideways in one frame. The camera's anchor must glide sideways with the height, never jump; on the ground's terms it stays on his place.
static void TestBridgeStep() {
    for (float fps : {144.0f, 60.0f, 40.0f}) {
        for (float rise : {0.85f, -0.6f, 0.3f}) {
            Run r(fps);
            float x = 0.0f, y = 2.0f, z = 0.0f;
            r.Step(x, y, z);
            for (int f = 0; f < (int)fps; ++f) { x += 20.0f * r.dt; z -= 15.0f * r.dt; r.Step(x, y, z); }   // a walk, steady
            float prev[3] = {r.look[0], r.look[1], r.look[2]};
            float worstSide = 0.0f, worstGround = 0.0f;
            y += rise;   // onto the bridge
            for (int f = 0; f < (int)fps; ++f) {
                x += 20.0f * r.dt; z -= 15.0f * r.dt;
                r.Step(x, y, z);
                // sideways to his way (20, -15): the anchor's move a frame across it
                const float mx = r.look[0] - prev[0], mz = r.look[2] - prev[2];
                const float side = std::fabs(mx * 0.6f + mz * 0.8f);   // (the unit across (20, -15) is (0.6, 0.8))
                worstSide = std::fmax(worstSide, side);
                worstGround = std::fmax(worstGround, std::hypot(r.out[0] - x, r.out[2] - z));
                prev[0] = r.look[0]; prev[1] = r.look[1]; prev[2] = r.look[2];
            }
            const float jump = std::fabs(rise) * kShear * (0.6f + 0.8f);   // what the look-at jumped across his way in that frame
            const float glide = 6.6f / (r.ms * 0.001f) * r.dt / 2.7182818f + 0.02f;   // a glide from rest over the ease's time: its fastest frame's share
            CHECK(worstSide < glide * jump + 1e-3f);  // the camera never moves more of it in a frame than that (a fifth at 60 fps, 200 ms)
            CHECK(worstGround < 1e-3f);               // and on the ground's terms it never left his place
            float back[3];
            const float g[3] = {x, y, z};
            Shear(g, back);
            CHECK(std::hypot(r.look[0] - back[0], r.look[2] - back[2]) < 0.01f && std::fabs(r.look[1] - back[1]) < 0.01f);   // there after a second
            if (!(worstSide < glide * jump + 1e-3f) || !(worstGround < 1e-3f))
                std::printf("  bridge %.0f fps, rise %.2f: aside %.3f a frame of %.3f, off his place %.4f\n", fps, rise, worstSide, jump, worstGround);
        }
    }
}

// Snaps aside of several sizes - a path node, a ledge's edge, a push off a wall or a monster - while he walks, runs or stands: each glides (the frame
// it comes the camera moves no more than a step of walking could, then never a big share of it a frame) and is done within the ease's time; the walk
// along his way stays exact.
static void TestSideSnaps() {
    for (float fps : {144.0f, 60.0f}) {
        for (float speed : {0.0f, 7.0f, 28.0f}) {
            for (float size : {0.4f, 0.8f, 1.5f, 3.0f}) {
                Run r(fps);
                float x = 0.0f, z = 0.0f;
                r.Step(x, 2.0f, z);
                for (int f = 0; f < (int)fps; ++f) { x += speed * r.dt; r.Step(x, 2.0f, z); }
                const float zBefore = r.out[2];
                x += speed * r.dt; z += size;   // the snap, straight across his way (+z)
                r.Step(x, 2.0f, z);
                const float tol = std::fmax(kDvH * r.dt, kMinJerkH);
                const float first = std::fabs(r.out[2] - zBefore);
                float prev = r.out[2], most = 0.0f;
                int doneAt = -1;
                for (int f = 0; f < (int)(1.5f * fps); ++f) {
                    x += speed * r.dt;
                    r.Step(x, 2.0f, z);
                    most = std::fmax(most, std::fabs(r.out[2] - prev));
                    prev = r.out[2];
                    if (doneAt < 0 && std::fabs(r.out[2] - z) < 0.02f * size) doneAt = f;
                }
                const bool ok = first <= tol + 1e-4f && most < 0.3f * size * (90.0f / fps) + 1e-3f && doneAt >= 0 && doneAt < (int)(0.35f * fps) &&
                                Near(r.out[0], x, 0.01f) && Near(r.out[2], z, 0.01f);
                CHECK(ok);
                if (!ok) std::printf("  side snap %.0f fps, %.0f units/s, %.1f: first frame %.3f (tol %.3f), most %.3f a frame, done at frame %d\n",
                                     fps, speed, size, first, tol, most, doneAt);
            }
        }
    }
}

// Serg on 0.222.7, VR F4: "тело не плавное и трясется" (F5 - no body - fine). The body drawn from inside must take the camera's offset exactly, every
// frame: the head, the chest, the hands and the weapon (share 1) the whole of it, the legs its part across the floor (they step with the camera, not
// in the game's ticks under it), the belly between - under snaps aside, a run in ticks and a bridge's step. Split's two parts add up to Apply's.
static void TestBodyWithCamera() {
    for (float fps : {144.0f, 90.0f, 60.0f}) {
        State s;
        float out[3];
        const float dt = 1.0f / fps, tick = 1.0f / 25.0f;
        double t = 0.0;
        float worstHead = 0.0f, worstLegs = 0.0f, worstLegY = 0.0f;
        const float g0[3] = {0.0f, 2.0f, 0.0f};
        StepG(s, g0, 0.0f, kTestMs, false, out);
        for (int f = 0; f < (int)(6.0f * fps); ++f) {
            t += dt;
            float g[3] = {28.0f * (float)(std::floor(t / tick) * tick), 2.0f, 0.0f};   // a run in the game's ticks
            if (t > 1.0) g[2] += 1.2f;                                              // a snap aside
            if (t > 2.5) g[1] += 0.85f;                                             // onto a bridge
            if (t > 4.0) g[2] -= 0.5f;                                              // and aside again
            float look[3], cam[3];
            Shear(g, look);
            Step(s, look, dt, kTestMs, false, cam);
            float whole[3], upper[3];
            Split(s, whole, upper);
            // the camera's offset against the look-at, and what the body's bones get: a share-1 bone (head, hands) whole + upper, the legs whole
            for (int i = 0; i < 3; ++i) {
                const float camOff = cam[i] - look[i];
                worstHead = std::fmax(worstHead, std::fabs(whole[i] + upper[i] - camOff));
            }
            // the legs: across the floor with the camera (on the ground's terms the camera's place less the look-at's, height apart)
            float camG[3];
            Unshear(cam, camG);
            worstLegs = std::fmax(worstLegs, std::fmax(std::fabs(whole[0] - (camG[0] - g[0])), std::fabs(whole[2] - (camG[2] - g[2]))));
            worstLegY = std::fmax(worstLegY, std::fabs(whole[1]));
        }
        CHECK(worstHead < 1e-4f);   // the head and the hands: exactly the camera's offset, every frame
        CHECK(worstLegs < 1e-4f);   // the legs: exactly its part across the floor
        CHECK(worstLegY == 0.0f);   // and never up or down (on the floor)
        if (!(worstHead < 1e-4f && worstLegs < 1e-4f && worstLegY == 0.0f))
            std::printf("  body with camera %.0f fps: head %.6f, legs across %.6f, legs up %.6f\n", fps, worstHead, worstLegs, worstLegY);
    }
    State none;
    float w[3] = {1, 1, 1}, u[3] = {1, 1, 1};
    Split(none, w, u);
    CHECK(w[0] == 0.0f && w[1] == 0.0f && w[2] == 0.0f && u[0] == 0.0f && u[1] == 0.0f && u[2] == 0.0f);   // nothing set: the body as the game has it
}

// Serg on 0.222.8: "на виде ф5 тело привязано к камере и ведёт себя правильно, на ф4 тело живёт своей отдельной жизнью от камеры". What F4 has and F5
// has not: [openxr] roomscale_walk / roomscale_body (his ini: both on) - the drawn body is put under the player's neck: its target is the hero's
// place plus the neck's offset in the room, the pose moves the body by (target - the look-at), and at the pose's end smooth_follow adds its own part
// across the floor. The room walk was handed the EASED anchor as the hero's place: the target carried the offset once and the pose's end added it
// again - the body went off the camera by the whole offset, frame by frame. Handed the game's look-at (camera.cpp RoomWalkView(in.lookAt)), the body's
// root stands at the camera's anchor plus the neck's offset every frame, as F5's (no room walk) at the anchor.
static void TestF4BodyRootWithCamera() {
    for (float fps : {90.0f, 72.0f}) {
        State s;
        float out[3];
        const float dt = 1.0f / fps, tick = 1.0f / 25.0f;
        const float neck[2] = {0.6f, -0.3f};   // the neck off the hero in the room, world units (a lean, a step in the room)
        double t = 0.0;
        float worstFixed = 0.0f, worstOld = 0.0f;
        const float g0[3] = {0.0f, 2.0f, 0.0f};
        StepG(s, g0, 0.0f, kTestMs, false, out);
        for (int f = 0; f < (int)(5.0f * fps); ++f) {
            t += dt;
            float g[3] = {24.0f * (float)(std::floor(t / tick) * tick), 2.0f, 0.0f};   // a run in the game's ticks
            if (t > 1.5) g[2] += 1.0f;                                               // a snap aside
            float look[3], cam[3], whole[3], upper[3];
            Shear(g, look);
            Step(s, look, dt, kTestMs, false, cam);
            Split(s, whole, upper);
            // the body's root (its pelvis: the part across the floor only), across the floor
            for (int i = 0; i < 3; i += 2) {
                const float n = neck[i / 2];
                const float fixedRoot = look[i] + ((look[i] + n) - look[i]) + whole[i];   // the target from the game's look-at, then the pose's end
                const float oldRoot = look[i] + ((cam[i] + n) - look[i]) + whole[i];      // 0.222.8: the target from the eased anchor
                const float camGround = cam[i] - kShear * cam[1];                          // the camera's anchor on the ground (unpushed)
                const float lookGround = look[i] - kShear * look[1];
                worstFixed = std::fmax(worstFixed, std::fabs((fixedRoot - kShear * look[1]) - (camGround + n)));
                worstOld = std::fmax(worstOld, std::fabs((oldRoot - kShear * look[1]) - (camGround + n)));
                (void)lookGround;
            }
        }
        CHECK(worstFixed < 1e-3f);   // the root under the camera's anchor (plus the neck's offset), every frame - as F5's
        CHECK(worstOld > 0.1f);      // (0.222.8's way: off by the whole offset - what Serg saw)
        if (!(worstFixed < 1e-3f)) std::printf("  F4 body root %.0f fps: off the camera by %.4f (0.222.8: %.4f)\n", fps, worstFixed, worstOld);
    }
}

int main() {
    TestWalkingFollowedExactly();
    for (float fps : {144.0f, 90.0f, 72.0f, 60.0f, 45.0f}) {
        TestBumpEased(fps, 1.5f, 0.0f);    // a bump up, standing
        TestBumpEased(fps, -1.0f, 0.0f);   // a step down
        TestBumpEased(fps, 0.6f, 28.0f);   // a small bump at a run
        TestBumpEased(fps, -2.5f, 20.0f);  // off a ledge at a run
    }
    TestStairs();
    TestShoveAside();
    TestSnaps();
    TestBounds();
    TestApply();
    TestEaseTime();
    TestViews();
    TestInside();
    TestToModel();
    TestWaist();
    TestSteadyRunNoJitter();
    TestTickSteppedRun();
    TestBridgeStep();
    TestSideSnaps();
    TestBodyWithCamera();
    TestF4BodyRootWithCamera();
    std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
