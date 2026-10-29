// The bow along the hands (vr/bow_aim.h): the maths of turning the bow until its axis lies along the line from the string hand to the
// bow hand, without the game.
//
//   bow_aim_test
//
// Checks: the turn about an axis; the aim line and its refusals; the turn that takes a bow's frame onto the aim, from any frame to
// any direction (the shortest swing keeps the controller's roll; the upright one keeps the limbs as upright as the aim allows),
// at every blend; the hand's plan turned by it turns the bow by exactly that rotation (the hero's wrist, the attach bone, the bow
// as skeletons.cpp chains them); the blend in time: take, release, cancel half way, a stall, a clock gone back.

#include "bow_aim.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace bowaim;
using bowrig::Cross;
using bowrig::Direction;
using bowrig::Dot;
using bowrig::Length;
using bowrig::Unit;

namespace {

int g_fails = 0;
int g_checks = 0;

#define EXPECT(c, ...)                                         \
    do {                                                       \
        ++g_checks;                                            \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool Near(V3 a, V3 b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
bool NearMat(const Mat& a, const Mat& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) if (!Near(a.m[i], b.m[i], eps)) return false;
    return true;
}
bool IsTurn(const Mat& r, float eps = 2e-4f) {   // orthonormal, determinant +1, no translation
    const V3 x = {r.m[0], r.m[1], r.m[2]}, y = {r.m[4], r.m[5], r.m[6]}, z = {r.m[8], r.m[9], r.m[10]};
    return Near(Length(x), 1.0f, eps) && Near(Length(y), 1.0f, eps) && Near(Length(z), 1.0f, eps) && Near(Dot(x, y), 0.0f, eps) &&
           Near(Dot(y, z), 0.0f, eps) && Near(Dot(x, z), 0.0f, eps) && Near(Dot(x, Cross(y, z)), 1.0f, eps) &&
           r.m[12] == 0.0f && r.m[13] == 0.0f && r.m[14] == 0.0f;
}

unsigned g_seed = 20261014u;
float Rnd() { g_seed = g_seed * 1664525u + 1013904223u; return (float)(g_seed >> 8) / (float)(1u << 24); }
V3 RndUnit() {
    for (;;) {
        const V3 v = {Rnd() * 2 - 1, Rnd() * 2 - 1, Rnd() * 2 - 1};
        if (Length(v) > 0.2f && Length(v) < 1.0f) return Unit(v);
    }
}
Frame RndFrame() {
    Frame f;
    f.forward = RndUnit();
    const V3 any = RndUnit();
    f.up = Unit(any - f.forward * Dot(any, f.forward));
    return f;
}
constexpr V3 kUp = {0, 1, 0};

void TestRotation() {
    std::printf("rotation\n");
    for (int n = 0; n < 200; ++n) {
        const V3 axis = RndUnit();
        const float a = (Rnd() * 2 - 1) * kPi;
        const Mat r = RotationAbout(axis, a);
        EXPECT(IsTurn(r), "a turn about %.2f %.2f %.2f by %.2f", axis.x, axis.y, axis.z, a);
        EXPECT(Near(Direction(r, axis), axis, 1e-4f), "the axis stays");
        const V3 across = Unit(Cross(axis, RndUnit()));
        EXPECT(Near(AngleDeg(across, Direction(r, across)), std::fabs(a) * 57.2957795f, 0.02f), "an angle of %.3f rad", a);
        const float b = (Rnd() * 2 - 1) * kPi;
        EXPECT(NearMat(bowrig::Mul(r, RotationAbout(axis, b)), RotationAbout(axis, a + b), 3e-4f), "turns about one axis add");
    }
    EXPECT(NearMat(RotationAbout({0, 0, 1}, 0.0f), bowrig::Identity()), "no angle, no turn");
    // the same direction as the core library's turn between two directions (row-vector, a counter-clockwise turn about the axis)
    const V3 a = {1, 0, 0}, b = {0, 1, 0};
    EXPECT(NearMat(RotationAbout(Cross(a, b), kPi * 0.5f), bowrig::Rotation(a, b), 1e-5f), "the same handedness as bowrig::Rotation");
    EXPECT(Near(AngleDeg({1, 0, 0}, {0, 1, 0}), 90.0f, 1e-3f) && Near(AngleDeg({1, 0, 0}, {-1, 0, 0}), 180.0f, 1e-3f) && Near(AngleDeg({1, 0, 0}, {0, 0, 0}), 0.0f),
           "angles");
}

void TestAimVector() {
    std::printf("aim vector\n");
    V3 a;
    EXPECT(AimVector({0, 0, 0}, {0, 0, -2}, 0.1f, &a) && Near(a, {0, 0, -1}), "from the string hand to the bow hand");
    EXPECT(AimVector({1, 2, 3}, {1.5f, 2, 3}, 0.1f, &a) && Near(a, {1, 0, 0}), "any place");
    EXPECT(!AimVector({0, 0, 0}, {0.05f, 0, 0}, 0.1f, &a), "hands nearer than the least: no line");
    EXPECT(!AimVector({0, 0, 0}, {0, 0, 0}, 0.0f, &a), "the same place: no line");
    EXPECT(!AimVector({NAN, 0, 0}, {0, 0, 1}, 0.1f, &a) && !AimVector({0, 0, 0}, {0, INFINITY, 1}, 0.1f, &a) && !AimVector({0, 0, 0}, {0, 0, 1}, 0.1f, nullptr), "not numbers, no output");
}

float SwingDeg(const Mat& turn) {   // the angle a turn makes, from its trace
    const float tr = turn.m[0] + turn.m[5] + turn.m[10];
    return std::acos(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f)) * 57.2957795f;
}

void TestFollow() {
    std::printf("follow the controller's roll\n");
    for (int n = 0; n < 3000; ++n) {
        const Frame cur = RndFrame();
        const V3 aim = n % 50 == 0 ? cur.forward : n % 50 == 1 ? cur.forward * -1.0f : RndUnit();
        const Mat t = AimTurn(cur, aim, Roll::Follow, kUp, 1.0f);
        EXPECT(IsTurn(t), "draw %d: a turn", n);
        EXPECT(Near(Direction(t, cur.forward), aim, 3e-4f), "draw %d: the bow's forward lies along the aim", n);
        const V3 up = Direction(t, cur.up);
        EXPECT(Near(Dot(up, aim), 0.0f, 3e-4f) && Near(Length(up), 1.0f, 3e-4f), "draw %d: the limbs' axis across the aim", n);
        if (n % 50 > 1) {
            EXPECT(Near(SwingDeg(t), AngleDeg(cur.forward, aim), 0.05f), "draw %d: the shortest swing (%.3f of %.3f deg)", n, SwingDeg(t), AngleDeg(cur.forward, aim));
            EXPECT(Near(Direction(t, cur.up), Direction(bowrig::Rotation(cur.forward, aim), cur.up), 3e-4f), "draw %d: the roll is the controller's", n);
        }
    }
    const Frame f = {{0, 0, 1}, {0, 1, 0}};
    EXPECT(NearMat(AimTurn(f, {0, 0, 1}, Roll::Follow, kUp, 1.0f), bowrig::Identity(), 1e-5f), "already along the aim: no turn");
    const Mat back = AimTurn(f, {0, 0, -1}, Roll::Follow, kUp, 1.0f);
    EXPECT(IsTurn(back) && Near(Direction(back, f.forward), {0, 0, -1}) && Near(Direction(back, f.up), {0, 1, 0}), "a half turn about the limbs' axis, which stays");
}

void TestUpright() {
    std::printf("upright\n");
    for (int n = 0; n < 3000; ++n) {
        const Frame cur = RndFrame();
        V3 aim = RndUnit();
        if (std::fabs(aim.y) > 0.98f) continue;   // (the vertical aims are below)
        const Mat t = AimTurn(cur, aim, Roll::Upright, kUp, 1.0f);
        EXPECT(IsTurn(t), "draw %d: a turn", n);
        EXPECT(Near(Direction(t, cur.forward), aim, 3e-4f), "draw %d: forward along the aim", n);
        const V3 up = Direction(t, cur.up);
        EXPECT(Near(Dot(up, aim), 0.0f, 3e-4f), "draw %d: across the aim", n);
        EXPECT(Dot(up, kUp) > 0.0f, "draw %d: the limbs point up, not down (%.3f)", n, Dot(up, kUp));
        EXPECT(Near(Dot(Cross(up, aim), kUp), 0.0f, 5e-4f), "draw %d: the limbs' axis in the vertical plane of the aim (the bow's side horizontal)", n);
    }
    // aimed straight up or down, or nearly: nothing upright to hold - the controller's roll, finite, forward still along the aim
    for (const V3 aim : {V3{0, 1, 0}, V3{0, -1, 0}, Unit(V3{0.01f, 1, 0}), Unit(V3{0, -1, 0.02f})}) {
        const Frame cur = RndFrame();
        const Mat t = AimTurn(cur, aim, Roll::Upright, kUp, 1.0f);
        EXPECT(IsTurn(t) && Near(Direction(t, cur.forward), aim, 3e-4f), "aimed %.2f %.2f %.2f: still a turn that lies along it", aim.x, aim.y, aim.z);
    }
    // a bow held upside down is turned right way up
    const Frame down = {{0, 0, 1}, {0, -1, 0}};
    const Mat t = AimTurn(down, {1, 0, 0}, Roll::Upright, kUp, 1.0f);
    EXPECT(Near(Direction(t, down.forward), {1, 0, 0}, 3e-4f) && Near(Direction(t, down.up), {0, 1, 0}, 3e-4f), "upside down, then upright");
}

void TestBlend() {
    std::printf("blend of the turn\n");
    for (const Roll roll : {Roll::Follow, Roll::Upright}) {
        for (int n = 0; n < 300; ++n) {
            const Frame cur = RndFrame();
            const V3 aim = RndUnit();
            const float theta = AngleDeg(cur.forward, aim);
            float last = 1e9f;
            for (int k = 0; k <= 10; ++k) {
                const float s = (float)k / 10.0f;
                const Mat t = AimTurn(cur, aim, roll, kUp, s);
                EXPECT(IsTurn(t), "turn at %.1f", s);
                const float left = AngleDeg(Direction(t, cur.forward), aim);
                EXPECT(Near(left, (1.0f - s) * theta, 0.05f), "roll %d draw %d at %.1f: %.3f deg left of %.3f", (int)roll, n, s, left, theta);
                EXPECT(left <= last + 0.05f, "the bow comes toward the aim, never away");
                last = left;
            }
            EXPECT(NearMat(AimTurn(cur, aim, roll, kUp, 0.0f), bowrig::Identity(), 0.0f), "none at 0");
        }
    }
    const Frame f = {{0, 0, 1}, {0, 1, 0}};
    // opposite: half way is a quarter turn, finite
    const Mat half = AimTurn(f, {0, 0, -1}, Roll::Follow, kUp, 0.5f);
    EXPECT(IsTurn(half) && Near(AngleDeg(Direction(half, f.forward), {0, 0, -1}), 90.0f, 0.05f), "opposite directions: half way is a quarter turn");
    // not numbers, no frame, no aim: no turn
    const Mat id = bowrig::Identity();
    EXPECT(NearMat(AimTurn(f, {NAN, 0, 0}, Roll::Follow, kUp, 1.0f), id, 0.0f) && NearMat(AimTurn(f, {0, 0, 0}, Roll::Follow, kUp, 1.0f), id, 0.0f), "no aim, no turn");
    EXPECT(NearMat(AimTurn({{0, 0, 0}, {0, 1, 0}}, {1, 0, 0}, Roll::Follow, kUp, 1.0f), id, 0.0f) && NearMat(AimTurn({{NAN, 0, 1}, {0, 1, 0}}, {1, 0, 0}, Roll::Follow, kUp, 1.0f), id, 0.0f), "no frame, no turn");
    EXPECT(NearMat(AimTurn(f, {1, 0, 0}, Roll::Follow, kUp, NAN), id, 0.0f) && NearMat(AimTurn(f, {1, 0, 0}, Roll::Follow, kUp, -1.0f), id, 0.0f), "no amount, no turn");
    EXPECT(NearMat(AimTurn(f, {1, 0, 0}, Roll::Follow, kUp, 7.0f), AimTurn(f, {1, 0, 0}, Roll::Follow, kUp, 1.0f), 0.0f), "more than all the way is all the way");
    // a frame whose up is not across its forward is taken across it
    const Mat skew = AimTurn({{0, 0, 1}, {0, 0.6f, 0.8f}}, {1, 0, 0}, Roll::Follow, kUp, 1.0f);
    EXPECT(IsTurn(skew) && Near(Direction(skew, {0, 0, 1}), {1, 0, 0}), "a skew up is straightened");
}

// ---- the hand: the wrist's two directions turned by the aim turn the hand, the attach bone and the bow by exactly that
struct Plan { V3 pLocal, kLocal, point, along; float det = 1.0f; };

// skeletons.cpp ApplyWrist's rotation: the hand's local directions pLocal, kLocal taken to point, along (made square to it)
Mat HandRot(const Plan& r) {
    const V3 a1 = r.pLocal, a2 = Unit(r.kLocal - a1 * Dot(r.kLocal, a1)), a3 = Cross(a1, a2);
    const V3 b1 = r.point, b2 = Unit(r.along - b1 * Dot(r.along, b1)), b3 = Cross(b1, b2) * r.det;
    const V3 A[3] = {a1, a2, a3}, B[3] = {b1, b2, b3};
    auto at = [](V3 v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; };
    Mat m = bowrig::Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m.m[i * 4 + j] = at(A[0], i) * at(B[0], j) + at(A[1], i) * at(B[1], j) + at(A[2], i) * at(B[2], j);
    return m;
}

void TestFrameOf() {
    std::printf("frame of the bow\n");
    // the bow's axes in its attach bone's frame, carried by the bone's matrix: a turn, a scale (the bow is 0.85 on some heroes), a place
    const Mat turn = bowrig::Mul(RotationAbout({1, 0, 0}, 0.4f), RotationAbout({0, 1, 0}, -1.1f));
    Mat attach = turn;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) attach.m[i * 4 + j] *= 0.85f;
    bowrig::SetPosition(attach, {3, 4, 5});
    const Frame f = FrameOf(attach, {0, 0, 1}, {0, 1, 0});
    EXPECT(Near(f.forward, Direction(turn, {0, 0, 1})) && Near(f.up, Direction(turn, {0, 1, 0})), "forward and up carried, unit, the scale and the place gone");
    // an up that leans on the forward is made across it
    const Frame g = FrameOf(attach, {0, 0, 1}, Unit(V3{0, 0.6f, 0.8f}));
    EXPECT(Near(Length(g.up), 1.0f) && Near(Dot(g.up, g.forward), 0.0f, 1e-5f), "an up leaning on the forward is straightened");
    EXPECT(Near(g.up, f.up, 1e-5f), "to the same up");
}

void TestHand() {
    std::printf("the hand\n");
    for (int n = 0; n < 1000; ++n) {
        // the hero's wrist: the hand's own two directions (a fixed property of its bones), what the controller asks of them
        Plan plan;
        plan.pLocal = RndUnit();
        const V3 any = RndUnit();
        plan.kLocal = Unit(any - plan.pLocal * Dot(any, plan.pLocal));
        plan.point = RndUnit();
        const V3 any2 = RndUnit();
        plan.along = Unit(any2 - plan.point * Dot(any2, plan.point));
        plan.det = n % 2 ? -1.0f : 1.0f;   // the left hand's bone is a mirror
        // the attach bone sits on the hand by a fixed turn (the game's grip), the bow on it by its own axes
        const Frame grip = RndFrame();
        const Mat G = HandRot({{0, 0, 1}, {0, 1, 0}, grip.forward, grip.up, 1.0f});
        const Frame inAttach = RndFrame();
        const Mat attach = bowrig::Mul(G, HandRot(plan));
        const Frame cur = FrameOf(attach, inAttach.forward, inAttach.up);
        const V3 aim = RndUnit();
        const Roll roll = n % 3 ? Roll::Follow : Roll::Upright;
        const Mat turn = AimTurn(cur, aim, roll, kUp, 1.0f);
        Plan turned = plan;
        TurnPlan(&turned.point, &turned.along, turn);
        const Mat attach2 = bowrig::Mul(G, HandRot(turned));
        const Frame now = FrameOf(attach2, inAttach.forward, inAttach.up);
        EXPECT(Near(now.forward, aim, 5e-4f), "draw %d: the bow lies along the aim after the hand is turned", n);
        EXPECT(Near(now.up, Direction(turn, cur.up), 5e-4f), "draw %d: and its roll is what the turn made of it", n);
        EXPECT(NearMat(HandRot(turned), bowrig::Mul(HandRot(plan), turn), 5e-4f), "draw %d: the hand is turned by the turn, after its own", n);
        if (roll == Roll::Upright && std::fabs(aim.y) < 0.95f) EXPECT(Dot(now.up, kUp) > 0.0f, "draw %d: upright", n);
        // half way: the bow is half the angle from the aim
        const Mat halfTurn = AimTurn(cur, aim, roll, kUp, 0.5f);
        Plan halved = plan;
        TurnPlan(&halved.point, &halved.along, halfTurn);
        const Frame mid = FrameOf(bowrig::Mul(G, HandRot(halved)), inAttach.forward, inAttach.up);
        EXPECT(Near(AngleDeg(mid.forward, aim), 0.5f * AngleDeg(cur.forward, aim), 0.05f), "draw %d: half the way", n);
    }
    // the plan's directions stay unit
    Plan p;
    p.point = {0, 0, 1};
    p.along = {1, 0, 0};
    const Mat q = RotationAbout({0, 1, 0}, 0.5f);
    V3 pa = p.point, al = p.along;
    TurnPlan(&pa, &al, q);
    EXPECT(Near(Length(pa), 1.0f) && Near(Length(al), 1.0f) && Near(Dot(pa, al), 0.0f), "unit directions stay unit and across");
    TurnPlan(nullptr, &al, q);   // (no crash)
    EXPECT(true, "a plan with one direction missing is fine");
}

// ---- the blend in time
void TestBlendState() {
    std::printf("blend state\n");
    {   // taken, held, let go: 120 ms each way
        Blend b;
        float v = b.Step(false, 1000, 120.0f);
        EXPECT(v == 0.0f, "starts at nothing");
        for (uint64_t t = 1011; t <= 1011 + 60; t += 11) v = b.Step(true, t, 120.0f);
        EXPECT(v > 0.3f && v < 0.7f, "half way in a half time (%.2f)", v);
        for (uint64_t t = 1011 + 66; t <= 1011 + 200; t += 11) v = b.Step(true, t, 120.0f);
        EXPECT(v == 1.0f, "in after the time (%.2f)", v);
        const uint64_t t0 = 1300;
        v = b.Step(true, t0, 120.0f);
        EXPECT(v == 1.0f, "held");
        for (uint64_t t = t0 + 11; t <= t0 + 55; t += 11) v = b.Step(false, t, 120.0f);
        EXPECT(v > 0.3f && v < 0.7f, "half way out in a half time (%.2f)", v);
        for (uint64_t t = t0 + 66; t <= t0 + 200; t += 11) v = b.Step(false, t, 120.0f);
        EXPECT(v == 0.0f, "out after the time (%.2f)", v);
    }
    {   // eased: slow at the ends, so the bow does not jerk at the take or at the release
        Blend b;
        b.Step(true, 0, 100.0f);
        EXPECT(Near(b.Step(true, 25, 100.0f), 0.15625f, 1e-5f), "a quarter of the time is 0.156 of the turn");
        EXPECT(Near(b.Step(true, 75, 100.0f), 0.84375f, 1e-5f), "three quarters of the time is 0.844 of the turn");
    }
    {   // the bow's own hook runs only while there is something to pose (the arrow's lift is faded by it): after a long gap it starts from
        // nothing - the plain Step would take the stall clamp for time that passed and put the lift in at once
        Blend plain, sparse;
        plain.Step(false, 0, 120.0f);
        sparse.StepSparse(false, 0, 120.0f);
        EXPECT(plain.Step(true, 5000, 120.0f) == 1.0f, "(the plain step jumps in after a gap)");
        EXPECT(sparse.StepSparse(true, 5000, 120.0f) == 0.0f, "after a gap the sparse one starts from nothing");
        const float half = sparse.StepSparse(true, 5060, 120.0f);
        EXPECT(half > 0.4f && half < 0.6f, "and is half in at half the time (%.2f)", half);
        EXPECT(sparse.StepSparse(true, 5130, 120.0f) == 1.0f, "in after the time");
        // at the pace of a game's frames it is the plain step, to the digit
        Blend a, b;
        bool same = true;
        for (uint64_t t = 0; t <= 600; t += 11) { const bool on = t < 300; same = same && a.Step(on, t, 120.0f) == b.StepSparse(on, t, 120.0f); }
        EXPECT(same, "frame by frame the sparse step is the plain one");
        // a pause shorter than the idle time goes on from where it was; a longer one does not
        Blend c, d;
        c.StepSparse(true, 0, 400.0f); d.StepSparse(true, 0, 400.0f);
        for (uint64_t t = 20; t <= 200; t += 20) { c.StepSparse(true, t, 400.0f); d.StepSparse(true, t, 400.0f); }
        const float before = c.value;
        EXPECT(c.StepSparse(true, 200 + 250, 400.0f) >= before && c.value > before, "a 250 ms pause goes on (%.2f from %.2f)", c.value, before);
        EXPECT(d.StepSparse(true, 200 + 350, 400.0f) == 0.0f, "a 350 ms pause starts again");
    }
    {   // a stall of the game is no longer blend than half a second of it
        Blend b;
        b.Step(true, 0, 2000.0f);
        b.Step(true, 5000, 2000.0f);
        EXPECT(Near(b.value, 0.25f, 1e-5f), "five seconds of stall move a 2 s blend by a quarter (%.3f)", b.value);
    }
    {   // a cancel half way in turns back from where it is: no jump, never more than the time allows per step, eased
        Blend b;
        b.Step(false, 0, 100.0f);
        float last = 0.0f, lastRaw = 0.0f;
        bool jump = false;
        for (uint64_t t = 10; t <= 50; t += 10) { const float v = b.Step(true, t, 100.0f); jump = jump || v < last - 1e-6f; last = v; lastRaw = b.value; }
        EXPECT(Near(lastRaw, 0.5f, 1e-5f), "half in after 50 ms of 100");
        EXPECT(!jump, "rising never falls");
        for (uint64_t t = 60; t <= 200; t += 10) {
            const float before = b.value;
            const float v = b.Step(false, t, 100.0f);
            EXPECT(b.value <= before + 1e-6f && before - b.value <= 0.1f + 1e-5f, "falling by no more than the time allows (%.3f -> %.3f)", before, b.value);
            EXPECT(v >= 0.0f && v <= 1.0f, "within 0..1");
        }
        EXPECT(b.value == 0.0f, "and out");
    }
    for (const float ms : {50.0f, 120.0f, 400.0f}) {   // the times the sliders give
        Blend b;
        b.Step(true, 0, ms);
        float v = 0.0f;
        uint64_t t = 0;
        while (v < 1.0f && t < 2000) { t += 5; v = b.Step(true, t, ms); }
        EXPECT(std::fabs((float)t - ms) <= 10.0f, "%.0f ms blend is in at %llu ms", ms, (unsigned long long)t);
    }
    {   // odd times: no number, too short, too long; a stall of the game; a clock gone back; reset
        Blend b;
        b.Step(true, 100, NAN);
        EXPECT(std::isfinite(b.Step(true, 150, NAN)), "no number: the default");
        Blend c;
        c.Step(true, 100, 0.0f);
        EXPECT(c.Step(true, 112, 0.0f) == 1.0f, "a blend under 10 ms is 10 ms");
        Blend d;
        d.Step(true, 100, 100000.0f);
        const float slow = d.Step(true, 600, 100000.0f);
        EXPECT(Near(d.value, 0.25f, 1e-5f), "a blend over 2 s is 2 s: 0.5 s of it is a quarter (%.4f)", d.value);
        (void)slow;
        Blend e;
        e.Step(true, 1000, 120.0f);
        EXPECT(e.Step(true, 6000, 120.0f) == 1.0f, "a stall of five seconds is no more than half a second of blend: in");
        e.Step(false, 6010, 120.0f);
        const float before = e.value;
        e.Step(false, 5000, 120.0f);
        EXPECT(e.value == before, "a clock gone back changes nothing");
        e.Reset();
        EXPECT(e.value == 0.0f && !e.started, "reset");
    }
    {   // the whole of it: a bow 60 degrees off the aim is taken onto it over 120 ms and let go back over 120 ms
        const Frame cur = {{0, 0, 1}, {0, 1, 0}};
        const V3 aim = {std::sin(kPi / 3.0f), 0, std::cos(kPi / 3.0f)};
        Blend b;
        float angleAtTake = 0, angleHeld = 0, angleAtLoose = 0, prev = 61.0f;
        bool monotonic = true;
        for (uint64_t t = 0; t <= 600; t += 11) {
            const bool engaged = t >= 100 && t < 400;
            const float s = b.Step(engaged, t, 120.0f);
            const float angle = AngleDeg(Direction(AimTurn(cur, aim, Roll::Follow, kUp, s), cur.forward), aim);
            if (t == 99) angleAtTake = angle;
            if (t == 297) angleHeld = angle;
            if (t == 594) angleAtLoose = angle;
            if (t >= 100 && t < 222) { monotonic = monotonic && angle <= prev + 0.01f; prev = angle; }
        }
        EXPECT(Near(angleAtTake, 60.0f, 0.5f) && angleHeld < 0.01f && Near(angleAtLoose, 60.0f, 0.5f), "off, on the aim while taken, off again after: %.1f %.1f %.1f", angleAtTake, angleHeld, angleAtLoose);
        EXPECT(monotonic, "it comes onto the aim without a back step");
    }
}

// The wrist a bow hangs on: the attach bones when one is shown and the other shrunk, else the hand opposite the string hand the draw
// detector names, else vrcam's aim hand. The Amazon's quiver shows BOTH bones ("R 1.00 L 1.00" in the log of 2026-10-14, where the
// aim answered "no bow on either wrist" for it) and the bow is in the left hand.
void TestBowWrist() {
    const float nan = std::nanf("");
    {   // the bones tell: one shown, one shrunk - whatever else is said
        for (int fed = -1; fed <= 1; ++fed)
            for (int aim = -1; aim <= 1; ++aim) {
                const BowWrist l = ChooseBowWrist(0.0f, 1.0f, fed, aim), r = ChooseBowWrist(1.0f, 0.0f, fed, aim);
                EXPECT(l.side == 1 && l.by == SideBy::AttachBones, "left shown: side %d by %d (fed %d aim %d)", l.side, (int)l.by, fed, aim);
                EXPECT(r.side == 0 && r.by == SideBy::AttachBones, "right shown: side %d by %d (fed %d aim %d)", r.side, (int)r.by, fed, aim);
            }
        EXPECT(ChooseBowWrist(0.05f, 0.95f, -1, -1).side == 1 && ChooseBowWrist(0.69f, 0.0f, -1, -1).side == -1, "the 0.7 / 0.3 thresholds");
    }
    {   // both shown (the quiver): the draw detector's string hand names it - the bow is in the other one
        const BowWrist a = ChooseBowWrist(1.0f, 1.0f, 0, -1), b = ChooseBowWrist(1.0f, 1.0f, 1, -1);
        EXPECT(a.side == 1 && a.by == SideBy::DrawFeed, "string hand right: the bow is in the left (%d)", a.side);
        EXPECT(b.side == 0 && b.by == SideBy::DrawFeed, "string hand left: the bow is in the right (%d)", b.side);
        const BowWrist c = ChooseBowWrist(1.0f, 1.0f, 0, 0);   // the feed outranks the aim hand
        EXPECT(c.side == 1 && c.by == SideBy::DrawFeed, "the feed first (%d)", c.side);
    }
    {   // no feed yet: vrcam's aim hand is the bow's
        const BowWrist a = ChooseBowWrist(1.0f, 1.0f, -1, 1), b = ChooseBowWrist(1.0f, 1.0f, -1, 0);
        EXPECT(a.side == 1 && a.by == SideBy::AimHand && b.side == 0 && b.by == SideBy::AimHand, "the aim hand: %d %d", a.side, b.side);
    }
    {   // nothing tells: not known (and never a guess)
        EXPECT(ChooseBowWrist(1.0f, 1.0f, -1, -1).side == -1 && ChooseBowWrist(1.0f, 1.0f, -1, -1).by == SideBy::Nothing, "nothing known");
        EXPECT(ChooseBowWrist(1.0f, 1.0f, 2, 7).side == -1, "a side that is not 0 or 1 says nothing");
        EXPECT(ChooseBowWrist(-1.0f, -1.0f, -1, -1).side == -1, "sizes not read");
        EXPECT(ChooseBowWrist(nan, nan, -1, -1).side == -1 && ChooseBowWrist(nan, nan, 0, -1).side == 1, "NaN sizes tell nothing; the feed still does");
        EXPECT(ChooseBowWrist(nan, 1.0f, -1, 0).side == 0, "one size NaN: the aim hand");
    }
    {   // both shrunk (the bow hidden / thrown): the bones do not tell either
        EXPECT(ChooseBowWrist(0.0f, 0.0f, -1, -1).side == -1 && ChooseBowWrist(0.0f, 0.0f, 0, -1).side == 1, "both shrunk: the feed still does");
    }
    EXPECT(std::string(SideByName(SideBy::AttachBones)) == "the attach bones" && std::string(SideByName(SideBy::DrawFeed)) == "the draw's feed" &&
           std::string(SideByName(SideBy::AimHand)) == "the aim hand" && std::string(SideByName(SideBy::Nothing)) == "nothing", "the names");
}

// ---- the arrow's rest point: FIXED in the bow hand's own frame, the aim goes from the string hand THROUGH it
Frame WithSide(Frame f) { f.side = Unit(Cross(f.up, f.forward)); return f; }

void TestRest() {
    std::printf("the arrow's rest point\n");
    {   // the third axis of the frame: made from the other two, or carried like them - and across both either way, on the side the matrix says
        const Mat turn = bowrig::Mul(bowrig::Rotation({0, 0, 1}, Unit(V3{0.3f, 0.4f, 0.9f})), bowrig::Identity());
        const V3 fa = {0, 0, 1}, ua = {0, 1, 0}, sa = {1, 0, 0};
        const Frame made = FrameOf(turn, fa, ua), given = FrameOf(turn, fa, ua, sa);
        EXPECT(Near(made.side, Direction(turn, sa), 1e-4f), "a turn: up x forward is the carried x axis");
        EXPECT(Near(given.side, made.side, 1e-4f), "and the given one agrees with it");
        EXPECT(Near(Dot(given.side, given.forward), 0.0f) && Near(Dot(given.side, given.up), 0.0f) && Near(Length(given.side), 1.0f), "across both, unit");
        // a mirrored matrix (the left attach bone's): the carried axis is the model's own, not the cross product of the carried two
        Mat mirror = turn;
        for (int j = 0; j < 3; ++j) mirror.m[j] = -mirror.m[j];   // the x row negated: determinant < 0
        const Frame m = FrameOf(mirror, fa, ua, sa);
        EXPECT(Near(m.side, Unit(Direction(mirror, sa)), 1e-4f), "a mirrored matrix: the side is where the matrix put the bow's own x axis");
        EXPECT(Near(m.side, Frame{m.forward, m.up, Unit(Cross(m.up, m.forward))}.side * -1.0f, 1e-4f), "...which is the opposite of the cross product of the other two");
        const Frame plain = FrameOf(bowrig::Identity(), {0, 0, 1}, {0, 1, 0});
        EXPECT(Near(plain.side, {1, 0, 0}) && Near(OffsetIn(plain, 1, 2, 3), {2, 3, 1}), "the offset: x along forward, y across, z up -> (2 3 1)");
    }
    const float kMin = 0.05f;
    {   // nothing to rest on: the line through the hand's own point, as it always was
        const Frame cur = WithSide(RndFrame());
        const V3 s = {0.4f, 1.0f, 0.2f}, b = {0.0f, 1.2f, -0.2f};
        const RestSolution r = SolveRest(s, b, cur, {0, 0, 0}, true, Roll::Follow, kUp, 1.0f, kMin);
        V3 a;
        AimVector(s, b, kMin, &a);
        EXPECT(r.ok && Near(r.aim, a) && Near(r.point, b) && r.steps == 0 && r.settled, "zero offset: the old aim through the reference point");
        EXPECT(!SolveRest(s, s + V3{0.01f, 0, 0}, cur, {0.01f, 0, 0}, true, Roll::Follow, kUp, 1.0f, kMin).ok, "the hands on one another: no line");
        EXPECT(!SolveRest(s, b, cur, V3{NAN, 0, 0}, true, Roll::Follow, kUp, 1.0f, kMin).ok && !SolveRest(V3{NAN, 0, 0}, b, cur, {0.01f, 0, 0}, true, Roll::Follow, kUp, 1.0f, kMin).ok, "not numbers: no line");
    }
    {   // the bow is not turned: the controller-held frame is the one displayed, the point is where the offset puts it, the aim passes through it
        const Frame cur = WithSide(RndFrame());
        const V3 s = {0.4f, 1.0f, 0.2f}, b = {0.0f, 1.2f, -0.2f};
        const V3 off = OffsetIn(cur, 0.01f, -0.02f, 0.03f);
        const RestSolution r = SolveRest(s, b, cur, off, false, Roll::Follow, kUp, 1.0f, kMin);
        EXPECT(r.ok && Near(r.point, b + off) && Near(r.aim, Unit(b + off - s)) && r.steps == 1, "no turn: the point is the hand's plus the offset");
        // a turn asked for at blend 0 is no turn either
        const RestSolution z = SolveRest(s, b, cur, off, true, Roll::Follow, kUp, 0.0f, kMin);
        EXPECT(z.ok && Near(z.point, r.point) && Near(z.aim, r.aim), "blend 0: no turn");
    }
    // the bow turned along the aim: whatever the hands do, the point stays where it is in the frame the bow is DISPLAYED in. As it is played: the
    // controller holds the bow within 75 deg of the line the hands make (it is turned onto it), the hands 25 to 75 cm apart, the offset up to
    // 10 cm in every direction
    int worst = 0, unsettled = 0, total = 0;
    float worstResidual = 0.0f;
    for (const Roll roll : {Roll::Follow, Roll::Upright}) {
        for (const float blend : {1.0f, 0.6f, 0.25f}) {
            for (int n = 0; n < 1500; ++n) {
                const V3 b = {Rnd() - 0.5f, 1.0f + Rnd() * 0.5f, Rnd() - 0.5f};
                const float gap = 0.25f + Rnd() * 0.5f;
                const V3 line = RndUnit();
                const V3 s = b - line * gap;
                Frame cur;
                {   // the controller's bow: the line turned by up to 75 deg about some axis across it
                    const V3 axis = Unit(Cross(line, RndUnit()));
                    cur.forward = Unit(Direction(RotationAbout(axis, (Rnd() * 75.0f) * kPi / 180.0f), line));
                    const V3 u = RndUnit();
                    cur.up = Unit(u - cur.forward * Dot(u, cur.forward));
                    cur = WithSide(cur);
                }
                const float x = (Rnd() * 2 - 1) * 0.10f, y = (Rnd() * 2 - 1) * 0.10f, z = (Rnd() * 2 - 1) * 0.10f;
                const V3 off = OffsetIn(cur, x, y, z);
                const RestSolution r = SolveRest(s, b, cur, off, true, roll, kUp, blend, kMin);
                ++total;
                EXPECT(r.ok && std::isfinite(r.point.x + r.point.y + r.point.z), "draw %d: solved", n);
                if (!r.ok) continue;
                worst = std::max(worst, r.steps);
                if (!r.settled) {
                    ++unsettled;
                    EXPECT(Near(r.point, b + off, 1e-5f), "draw %d: not settled, the controller-held frame's point", n);
                    continue;
                }
                worstResidual = std::max(worstResidual, r.residualDeg);
                // the aim runs from the string hand THROUGH the point
                EXPECT(Near(r.aim, Unit(r.point - s), 2e-4f), "draw %d: the aim goes through the point", n);
                // the bow is displayed turned by what the solved aim makes of it, and the point is the same place in THAT frame
                const Mat t = AimTurn(cur, r.aim, roll, kUp, blend);
                const float tol = 4e-4f + 0.18f * r.residualDeg * kPi / 180.0f;   // (what the last round still moved the aim by, at the hand)
                EXPECT(Near(r.point, b + Direction(t, off), tol), "draw %d: the point is the offset carried by the displayed bow's turn", n);
                const V3 f = Direction(t, cur.forward), u = Direction(t, cur.up), sd = Direction(t, cur.side);
                const V3 rel = r.point - b;
                EXPECT(Near(Dot(rel, f), x, tol) && Near(Dot(rel, sd), y, tol) && Near(Dot(rel, u), z, tol),
                       "draw %d: in the displayed frame the point is %.4f %.4f %.4f from the hand, wanted %.4f %.4f %.4f", n, Dot(rel, f), Dot(rel, sd), Dot(rel, u), x, y, z);
            }
        }
    }
    EXPECT(worst <= 128 && worstResidual <= 0.2f, "rounds at most %d, the last change at most %.4f deg", worst, worstResidual);
    EXPECT(unsettled * 50 < total, "almost all of them settle: %d of %d did not", unsettled, total);
    {   // the hard ones - the hands 12 to 30 cm apart, the controller's bow up to 120 deg off the line, 10 cm each way: the rounds taken whole may swing
        // about; taking them by halves settles some of those, and what settles at all is still fixed in the displayed frame
        int damped = 0, settled = 0, hardTotal = 0;
        for (int n = 0; n < 4000; ++n) {
            const V3 b = {Rnd() - 0.5f, 1.0f + Rnd() * 0.5f, Rnd() - 0.5f};
            const float gap = 0.12f + Rnd() * 0.18f;
            const V3 line = RndUnit();
            const V3 s = b - line * gap;
            Frame cur;
            cur.forward = Unit(Direction(RotationAbout(Unit(Cross(line, RndUnit())), (Rnd() * 120.0f) * kPi / 180.0f), line));
            const V3 u = RndUnit();
            cur.up = Unit(u - cur.forward * Dot(u, cur.forward));
            cur = WithSide(cur);
            const float x = (Rnd() * 2 - 1) * 0.10f, y = (Rnd() * 2 - 1) * 0.10f, z = (Rnd() * 2 - 1) * 0.10f;
            const V3 off = OffsetIn(cur, x, y, z);
            const Roll roll = n % 2 ? Roll::Upright : Roll::Follow;
            const RestSolution r = SolveRest(s, b, cur, off, true, roll, kUp, 1.0f, kMin);
            ++hardTotal;
            if (!r.ok) continue;
            EXPECT(std::isfinite(r.aim.x + r.aim.y + r.aim.z + r.point.x) && Near(r.aim, Unit(r.point - s), 2e-4f), "hard %d: a line through the point, settled or not", n);
            if (!r.settled) continue;
            ++settled;
            if (r.damped) ++damped;
            const Mat t = AimTurn(cur, r.aim, roll, kUp, 1.0f);
            EXPECT(Near(r.point, b + Direction(t, off), 4e-4f + 0.18f * r.residualDeg * kPi / 180.0f), "hard %d: still fixed in the displayed frame", n);
        }
        EXPECT(damped > 0, "the half-rounds settle some that the whole ones do not (%d of %d settled, %d of them by halves)", settled, hardTotal, damped);
    }
    {   // a modest offset settles in a few rounds
        const Frame cur = WithSide(RndFrame());
        const V3 b = {0, 1.2f, 0}, s = b - Unit(V3{0.2f, 0.1f, 1.0f}) * 0.4f;
        const RestSolution r = SolveRest(s, b, cur, OffsetIn(cur, 0, 0, 0.025f), true, Roll::Follow, kUp, 1.0f, kMin);
        EXPECT(r.settled && r.steps <= 8, "2.5 cm up at 40 cm: %d rounds", r.steps);
        // the arrow rides above the bow's axis by the whole 2.5 cm: the axis is parallel to the aim through the hand, the aim passes 2.5 cm off it
        const Mat t = AimTurn(cur, r.aim, Roll::Follow, kUp, 1.0f);
        const V3 axisFrom = b, aimOff = r.point - axisFrom;
        const V3 across = aimOff - r.aim * Dot(aimOff, r.aim);
        EXPECT(Near(Length(across), 0.025f, 5e-4f) && Near(Unit(across), Direction(t, cur.up), 1e-3f), "the arrow clears the bow's axis by 2.5 cm, up the displayed limbs (%.4f)", Length(across));
    }
    {   // the hands very near with a large offset: it may not settle - then the controller-held frame's point, still through a point and finite
        const Frame cur = WithSide(RndFrame());
        const V3 b = {0, 1.2f, 0}, s = b - Unit(V3{0.1f, 0.05f, 1.0f}) * 0.10f;
        const RestSolution r = SolveRest(s, b, cur, OffsetIn(cur, 0.10f, 0.10f, 0.10f), true, Roll::Follow, kUp, 1.0f, kMin);
        EXPECT(r.ok && std::isfinite(r.aim.x + r.point.x) && Near(r.aim, Unit(r.point - s), 2e-4f), "an extreme case still gives a line through a point (settled %d, %d rounds, %.2f deg)", (int)r.settled, r.steps, r.residualDeg);
        if (!r.settled) EXPECT(Near(r.point, b + OffsetIn(cur, 0.10f, 0.10f, 0.10f), 1e-5f), "not settled: the controller-held frame's point");
    }
    {   // a point that falls on the string hand has no line: refused, not NaN
        const Frame cur = WithSide(RndFrame());
        const V3 b = {0, 1.2f, 0}, s = {0.0f, 1.2f - 0.06f, 0.0f}, off = {0.0f, -0.06f, 0.0f};   // the point is the string hand's place
        EXPECT(!SolveRest(s, b, cur, off, false, Roll::Follow, kUp, 1.0f, kMin).ok, "the point on the string hand: no line");
    }
}

// ---- the arrow clear of the hand and the forearm
// A bow arm: the shoulder, the hand (the controller's reference point), the wrist behind the hand along the line from the shoulder, and the
// elbow by two bones (0.29 and 0.26 m) bent toward `pole`.
struct Arm { V3 shoulder, hand, wrist, elbow; };
Arm MakeArm(V3 shoulder, V3 hand, V3 pole) {
    Arm a;
    a.shoulder = shoulder;
    a.hand = hand;
    const V3 out = Unit(hand - shoulder);
    a.wrist = hand - out * 0.08f;   // the hand is 8 cm from the palm to the wrist
    const V3 d = a.wrist - shoulder;
    const float dist = std::min(Length(d), 0.5499f), u = 0.29f, l = 0.26f;
    const V3 dir = Unit(d);
    const float along = (u * u - l * l + dist * dist) / (2.0f * dist), bend = std::sqrt(std::max(0.0f, u * u - along * along));
    const V3 side = Unit(pole - dir * Dot(pole, dir));
    a.elbow = shoulder + dir * along + side * bend;
    return a;
}
void Capsules(const Arm& a, Capsule out[2]) {
    out[0] = {a.hand, a.wrist, 0.050f, 0.050f};     // the fist and the back of the hand
    out[1] = {a.wrist, a.elbow, 0.042f, 0.052f};    // the wrist and the forearm
}
constexpr float kShaft = 0.006f, kMargin = 0.008f, kMaxLift = 0.08f, kStep = 0.002f, kTip = 0.35f;

void TestClearance() {
    std::printf("the arrow clear of the hand and the forearm\n");
    {   // the distance between segments
        float s, t;
        EXPECT(Near(SegmentDistance({0, 0, 0}, {2, 0, 0}, {1, -1, 1}, {1, 1, 1}, &s, &t), 1.0f) && Near(s, 0.5f) && Near(t, 0.5f), "crossing at 1 m across: 1 (%.3f %.3f)", s, t);
        EXPECT(Near(SegmentDistance({0, 0, 0}, {2, 0, 0}, {0, 0.5f, 0}, {2, 0.5f, 0}), 0.5f), "parallel: 0.5");
        EXPECT(Near(SegmentDistance({0, 0, 0}, {1, 0, 0}, {3, 0, 0}, {5, 1, 0}, &s, &t), 2.0f) && Near(s, 1.0f) && Near(t, 0.0f), "beyond the end: to the end point");
        EXPECT(Near(SegmentDistance({0, 0, 0}, {0, 0, 0}, {1, 1, 0}, {1, 1, 0}), std::sqrt(2.0f)), "two points");
        EXPECT(Near(SegmentDistance({0, 0, 0}, {0, 0, 0}, {-1, 1, 0}, {1, 1, 0}, &s, &t), 1.0f) && Near(t, 0.5f), "a point and a segment");
        EXPECT(Near(SegmentDistance({0, 2, 0}, {0, 4, 0}, {0, 0, 0}, {0, 1, 0}), 1.0f), "in line, apart: 1");
        EXPECT(Near(SegmentDistance({0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {1, 1, 1}), 0.0f), "the same segment: 0");
        for (int n = 0; n < 500; ++n) {   // symmetric, not more than the distance between any two of their points, 0 for segments that cross
            const V3 p0 = RndUnit() * Rnd(), p1 = RndUnit() * Rnd(), q0 = RndUnit() * Rnd(), q1 = RndUnit() * Rnd();
            const float d = SegmentDistance(p0, p1, q0, q1);
            EXPECT(Near(d, SegmentDistance(q0, q1, p0, p1), 1e-5f), "symmetric");
            bool ok = true;
            for (int i = 0; i <= 10 && ok; ++i)
                for (int j = 0; j <= 10; ++j) if (Length((p0 + (p1 - p0) * (i / 10.0f)) - (q0 + (q1 - q0) * (j / 10.0f))) < d - 1e-5f) { ok = false; break; }
            EXPECT(ok, "no pair of points is nearer than the answer (%.4f)", d);
        }
    }
    {   // one capsule: a horizontal axis at the height 0, radius 0.05; the arrow along x below it; lifted along up
        const Capsule c = {{-0.3f, 0, 0}, {0.3f, 0, 0}, 0.05f, 0.05f};
        const float need = 0.05f + kShaft + kMargin;
        for (const float y : {-0.04f, -0.01f, 0.0f, 0.03f}) {
            const ClearLift r = ClearanceLift({-0.5f, y, 0}, {0.5f, y, 0}, {0, 1, 0}, kTip, &c, 1, kShaft, kMargin, kMaxLift, kStep);
            EXPECT(Near(r.before, std::fabs(y) - need, 1e-5f), "arrow at %.2f: %.4f from the capsule before", y, r.before);
            // the arrow goes UP: from below it passes through the axis first - it clears only when it is above, the lift is the whole way up past it
            const float wanted = need - y;   // the height of the arrow above the capsule's axis it must reach
            if (wanted <= kMaxLift + 1e-6f) {
                EXPECT(r.cleared && r.after >= 0.0f && r.lift >= wanted - 1e-5f && r.lift <= wanted + kStep + 1e-5f, "arrow at %.2f: lifted %.4f, wanted %.4f..%.4f (cleared %d)", y, r.lift, wanted, wanted + kStep, (int)r.cleared);
            } else {
                EXPECT(!r.cleared && r.lift <= kMaxLift + 1e-6f && r.after >= r.before - 1e-6f, "arrow at %.2f: not clearable in %.2f", y, kMaxLift);
            }
        }
        // a tapered one: the radius is the one where the arrow is nearest (0.02 at a, 0.08 at b)
        const Capsule taper = {{-0.3f, 0, 0}, {0.3f, 0, 0}, 0.02f, 0.08f};
        EXPECT(Near(ArmClearance({0.25f, 0.15f, -1.0f}, {0.25f, 0.15f, 1.0f}, &taper, 1, kShaft, kMargin), 0.15f - (0.02f + 0.06f * (0.55f / 0.6f)) - kShaft - kMargin, 1e-5f), "near the thick end: its radius");
        EXPECT(Near(ArmClearance({-0.25f, 0.15f, -1.0f}, {-0.25f, 0.15f, 1.0f}, &taper, 1, kShaft, kMargin), 0.15f - (0.02f + 0.06f * (0.05f / 0.6f)) - kShaft - kMargin, 1e-5f), "near the thin end: its radius");
        const Capsule pair[2] = {{{0, 0, 0}, {0, 0, 1}, 0.01f, 0.01f}, {{1, 0, 0}, {1, 0, 1}, 0.01f, 0.01f}};
        EXPECT(Near(ArmClearance({0.9f, 0, -1}, {0.9f, 0, 2}, pair, 2, 0.0f, 0.0f), 0.1f - 0.01f, 1e-5f), "the nearer of two capsules decides");
        const ClearLift above = ClearanceLift({-0.5f, 0.2f, 0}, {0.5f, 0.2f, 0}, {0, 1, 0}, kTip, &c, 1, kShaft, kMargin, kMaxLift, kStep);
        EXPECT(above.lift == 0.0f && above.cleared && above.before > 0.0f, "already clear: no lift");
        EXPECT(ClearanceLift({0, 0, 0}, {0, 0, 0}, {0, 1, 0}, kTip, &c, 1, kShaft, kMargin, kMaxLift, kStep).lift == 0.0f, "no line: no lift");
        EXPECT(ClearanceLift({-0.5f, 0, 0}, {0.5f, 0, 0}, {0, 0, 0}, kTip, &c, 1, kShaft, kMargin, kMaxLift, kStep).lift == 0.0f, "no axis: no lift");
        EXPECT(ClearanceLift({-0.5f, 0, 0}, {0.5f, 0, 0}, {0, 1, 0}, kTip, &c, 0, kShaft, kMargin, kMaxLift, kStep).lift == 0.0f, "no capsule: no lift");
    }
    const V3 up = {0, 1, 0};
    {   // (1) "a Crusader Bow seen from above: the arrow lies along the line between the hands and passes straight THROUGH the bow hand - over the back
        // of the hand and the wrist, the red wrist medallion right under it - and its rear half crosses the forearm". The bow arm out to the front and
        // a little to the left, the string hand low in front of the chest.
        const V3 shoulder = {-0.20f, 1.45f, -0.02f}, hand = {-0.24f, 1.42f, -0.58f}, string = {0.10f, 1.34f, -0.22f};
        const Arm arm = MakeArm(shoulder, hand, {-0.4f, -1.0f, 0.1f});
        Capsule caps[2];
        Capsules(arm, caps);
        // the old default: 2.5 cm above the hand's reference point - through the hand (the fist is 5 cm round)
        const ClearLift old = ClearanceLift(string, hand + up * 0.025f, up, kTip, caps, 2, kShaft, kMargin, kMaxLift, kStep);
        EXPECT(old.before < 0.0f, "the line 2.5 cm over the reference point is through the hand: %.3f", old.before);
        // the new default: 5 cm, and the clearance does the rest
        const V3 rest = hand + up * 0.05f;
        const ClearLift now = ClearanceLift(string, rest, up, kTip, caps, 2, kShaft, kMargin, kMaxLift, kStep);
        EXPECT(now.cleared && now.after >= 0.0f && now.lift <= kMaxLift + 1e-6f, "(1) cleared: lifted %.3f, %.3f from the arm (before %.3f)", now.lift, now.after, now.before);
        std::printf("  (1) 5 cm over the hand: %.1f cm from the arm before the lift, lifted %.1f cm, %.1f cm after\n", now.before * 100.0f, now.lift * 100.0f, now.after * 100.0f);
        // checked independently: the arrow's axis, lifted, is farther from the wrist and the forearm than their radius, and from the hand
        const V3 line = rest - string, tip = rest + Unit(line) * kTip;
        const float dWrist = SegmentDistance(string + up * now.lift, tip + up * now.lift, arm.wrist, arm.elbow), dHand = SegmentDistance(string + up * now.lift, tip + up * now.lift, arm.hand, arm.wrist);
        EXPECT(dWrist >= 0.042f + kShaft && dHand >= 0.050f + kShaft, "(1) the shaft is %.3f from the forearm's axis and %.3f from the hand's", dWrist, dHand);
        // the same with the string hand where the shaft's rear half crosses the forearm: the forearm bent back across the line
        const Arm bent = MakeArm(shoulder, hand, {0.3f, -0.6f, 1.0f});   // the elbow out to the side and up
        Capsule bc[2];
        Capsules(bent, bc);
        const ClearLift b = ClearanceLift({-0.05f, 1.30f, -0.15f}, rest, up, kTip, bc, 2, kShaft, kMargin, kMaxLift, kStep);
        EXPECT(b.after >= b.before - 1e-6f && b.lift <= kMaxLift + 1e-6f, "(1b) a bent arm and the string hand lower still: never worse (%.3f -> %.3f, lifted %.3f)", b.before, b.after, b.lift);
    }
    {   // (2) "the bow hand held out to the left with the bow's limb going up behind it: the arrow runs from the right across to the left, its head far
        // left of the hand, and the shaft cuts through the middle of the wrist (between the hand and the forearm bracer) and the medallion"
        const V3 shoulder = {-0.20f, 1.45f, 0.0f}, hand = {-0.62f, 1.38f, -0.20f}, string = {0.10f, 1.40f, -0.25f};
        const Arm arm = MakeArm(shoulder, hand, {0.0f, -1.0f, 0.4f});
        Capsule caps[2];
        Capsules(arm, caps);
        // the bow turned: its limbs go up and BEHIND the hand (toward the archer), so `up` is tilted back
        const V3 tilted = Unit(V3{0.1f, 0.8f, 0.6f});
        const V3 flat = Unit(V3{0.0f, 1.0f, 0.0f});
        for (const V3 axis : {flat, tilted}) {
            const V3 rest = hand + axis * 0.05f;
            const ClearLift r = ClearanceLift(string, rest, axis, kTip, caps, 2, kShaft, kMargin, kMaxLift, kStep);
            std::printf("  (2) limb axis %.1f %.1f %.1f: %.1f cm from the arm before the lift, lifted %.1f cm, %.1f cm after\n", axis.x, axis.y, axis.z, r.before * 100.0f, r.lift * 100.0f, r.after * 100.0f);
            EXPECT(r.cleared && r.after >= 0.0f && r.lift <= kMaxLift + 1e-6f, "(2) limb axis %.1f %.1f %.1f: cleared, lifted %.3f (before %.3f, after %.3f)", axis.x, axis.y, axis.z, r.lift, r.before, r.after);
        }
        EXPECT(ClearanceLift(string, hand + flat * 0.02f, flat, kTip, caps, 2, kShaft, kMargin, kMaxLift, kStep).before < 0.0f, "(2) a rest point 2 cm over the hand is through it");
    }
    {   // the hands far apart, close, the string hand low: never worse than no lift, minimal where it clears, and mostly clear
        int cases = 0, cleared = 0, lifted = 0, lowTotal = 0, lowCleared = 0;
        for (int n = 0; n < 6000; ++n) {
            const V3 shoulder = {-0.2f + (Rnd() - 0.5f) * 0.1f, 1.45f, (Rnd() - 0.5f) * 0.1f};
            const float reach = 0.30f + Rnd() * 0.30f;
            V3 dir = Unit(V3{(Rnd() - 0.8f) * 1.2f, (Rnd() - 0.6f) * 0.8f, -Rnd() - 0.1f});
            const V3 hand = shoulder + dir * reach;
            const Arm arm = MakeArm(shoulder, hand, Unit(V3{(Rnd() - 0.5f), -1.0f, (Rnd() - 0.5f)}));
            Capsule caps[2];
            Capsules(arm, caps);
            const bool low = n % 3 == 0;   // the string hand 15 to 35 cm under the bow hand's height
            const float gap = 0.15f + Rnd() * 0.55f;
            V3 away = Unit(V3{Rnd() - 0.3f, low ? -(0.25f + Rnd() * 0.4f) : (Rnd() - 0.5f) * 0.3f, Rnd() * 0.8f + 0.2f});   // from the bow hand toward the archer
            const V3 string = hand + away * gap;
            const V3 axis = Unit(V3{(Rnd() - 0.5f) * 0.5f, 1.0f, (Rnd() - 0.5f) * 0.5f});
            const float z = 0.03f + Rnd() * 0.04f;
            const V3 rest = hand + axis * z;
            const ClearLift r = ClearanceLift(string, rest, axis, kTip, caps, 2, kShaft, kMargin, kMaxLift, kStep);
            ++cases;
            EXPECT(r.lift >= 0.0f && r.lift <= kMaxLift + 1e-6f, "case %d: the lift %.4f is within the limit", n, r.lift);
            EXPECT(r.after >= r.before - 1e-6f, "case %d: never worse (%.4f -> %.4f)", n, r.before, r.after);
            EXPECT(r.before < 0.0f || r.lift == 0.0f, "case %d: already clear, so no lift", n);
            // the clearance is what it says: recomputed from the lifted arrow
            const V3 line = rest - string, tip = rest + Unit(line) * kTip;
            const float again = ArmClearance(string + axis * r.lift, tip + axis * r.lift, caps, 2, kShaft, kMargin);
            EXPECT(Near(again, r.after, 1e-5f), "case %d: the clearance stands (%.5f, %.5f)", n, again, r.after);
            if (r.cleared) {
                EXPECT(r.after >= 0.0f, "case %d: cleared means clear", n);
                if (r.lift > 0.0f) EXPECT(ArmClearance(string + axis * (r.lift - kStep), tip + axis * (r.lift - kStep), caps, 2, kShaft, kMargin) < 0.0f, "case %d: the least lift that clears", n);
                ++cleared;
            }
            if (r.lift > 0.0f) ++lifted;
            if (low) { ++lowTotal; if (r.cleared) ++lowCleared; }
        }
        // with the string hand about the bow hand's height at least 9 in 10 are cleared within 8 cm; with it 25 to 65 cm lower there are poses no
        // lift of the arrow alone can save (the line comes up through the forearm from below) - those get the lift that comes nearest, which is
        // never worse than none (above)
        const int level = cases - lowTotal, levelCleared = cleared - lowCleared;
        EXPECT(levelCleared * 100 >= level * 90, "the string hand level: at least 9 in 10 are cleared within 8 cm: %d of %d", levelCleared, level);
        std::printf("  clearance: %d of %d cleared, %d lifted; string hand level: %d of %d cleared, low: %d of %d\n", cleared, cases, lifted, levelCleared, level, lowCleared, lowTotal);
    }
    {   // the smoothing: goes to the target, never past it, starts from the target after a gap, a number only
        Smooth s;
        EXPECT(s.Step(0.05f, 1000, 50.0f) == 0.05f, "the first value is the target");
        float v = 0.0f;
        for (uint64_t t = 1010; t <= 1400; t += 10) v = s.Step(0.0f, t, 50.0f);
        EXPECT(v >= 0.0f && v < 0.001f, "it goes down to the target: %.5f", v);
        Smooth a;
        a.Step(0.0f, 0, 50.0f);
        float prev = 0.0f;
        bool monotone = true;
        for (uint64_t t = 10; t <= 300; t += 10) { const float x = a.Step(0.1f, t, 50.0f); monotone = monotone && x >= prev - 1e-7f && x <= 0.1f + 1e-7f; prev = x; }
        EXPECT(monotone && prev > 0.099f, "up to the target without overshoot (%.4f)", prev);
        const float before = a.value;
        EXPECT(Near(a.Step(0.0f, 310, 50.0f), before + (0.0f - before) * (1.0f - std::exp(-10.0f / 50.0f)), 1e-6f), "one step is the exponential's");
        EXPECT(a.Step(0.7f, 5000, 50.0f) == 0.7f, "after a gap it starts from the target");
        Smooth slow;
        slow.Step(0.0f, 0, 1000.0f);
        EXPECT(slow.Step(0.5f, 250, 1000.0f) < 0.2f, "a slow one on the way after a short pause");
        EXPECT(slow.Step(0.9f, 250 + 400, 1000.0f) == 0.9f, "after a pause over 300 ms - even a slow one - it starts from the target");
        EXPECT(a.Step(NAN, 5010, 50.0f) < 0.7f && std::isfinite(a.value), "NaN is no target");
    }
}

}  // namespace

int main() {
    TestRotation();
    TestAimVector();
    TestFollow();
    TestUpright();
    TestBlend();
    TestFrameOf();
    TestHand();
    TestBlendState();
    TestBowWrist();
    TestRest();
    TestClearance();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
