// Offline checks of vr/doll_feet.h ([body] doll_feet): the hero's feet are the doll's, not the game's - planted feet stay put in the world, the feet
// take turns and keep their sides walking forward, back, running back, turning round standing (the camera round him) and changing way; standing still
// they do not step; a jump of the body puts them back under it. Run: doll_feet_test.exe (exit 0 = all passed).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "doll_feet.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using dollfeet::P2;
static constexpr float kPi = 3.14159265f;
static constexpr float kUpm = 3.28084f;   // the Amazon rig's units a metre
static const P2 kStance[2] = {{-0.38f, -0.12f}, {0.38f, -0.12f}};   // right, left (her bind pose's feet against the origin)

struct Run {
    int steps = 0, flights = 0;
    float slide = 0.0f;        // the most a foot on the ground moved in one pose (metres)
    float sideWorst = 1e9f;    // the least of each foot's own side of the middle line (metres; < 0 = crossed)
    float twistWorst = 0.0f;   // the most a planted foot turned from the facing (degrees)
    int sameTwice = 0;         // a foot stepping again right after itself while he went (not taking turns)
};

// dirOf(t): the travel's way (radians) or NAN standing; speed(t) m/s; yawOf(t) the facing.
template <typename D, typename S, typename Y>
static Run Go(float seconds, D dirOf, S speedOf, Y yawOf, float settle = 0.5f) {
    Run r;
    dollfeet::Doll doll;
    P2 body{};
    const float dt = 1.0f / 90.0f;
    P2 was[2];
    bool wasDown[2] = {false, false};
    int lastStep = -1, stepsSeen = 0;
    for (int i = 0; i * dt < seconds; ++i) {
        const float t = i * dt;
        const float sp = speedOf(t), dir = dirOf(t), yaw = yawOf(t);
        const P2 vel = std::isfinite(dir) ? dollfeet::Mul(dollfeet::Ahead(dir), sp * kUpm) : P2{};
        body = dollfeet::Add(body, dollfeet::Mul(vel, dt));
        doll.Step(dt, body, yaw, vel, kStance, kUpm);
        if (doll.Steps() > stepsSeen) {   // which foot left
            for (int side = 0; side < 2; ++side)
                if (doll.Get(side).flying && doll.Get(side).u < 0.2f) {
                    if (side == lastStep && sp > 0.5f && t > settle) ++r.sameTwice;
                    lastStep = side;
                }
            stepsSeen = doll.Steps();
        }
        for (int side = 0; side < 2; ++side) {
            const dollfeet::Foot& f = doll.Get(side);
            const float sign = side ? 1.0f : -1.0f;
            r.sideWorst = std::min(r.sideWorst, dollfeet::Dot(dollfeet::Sub(f.at, body), dollfeet::Left(yaw)) * sign / kUpm);
            if (!f.flying) {
                if (wasDown[side]) r.slide = std::max(r.slide, dollfeet::Len(dollfeet::Sub(f.at, was[side])) / kUpm);
                if (t > settle) r.twistWorst = std::max(r.twistWorst, std::fabs(dollfeet::WrapPi(yaw - f.yaw)) * 57.2957795f);
            }
            wasDown[side] = !f.flying;
            was[side] = f.at;
        }
    }
    r.steps = doll.Steps();
    r.flights = doll.Flights();
    return r;
}

static void Print(const char* what, const Run& r, float seconds) {
    std::printf("%-34s %2d steps (%.1f a second), %d both feet up; a planted foot moved at most %.4f m, its own side at least %.3f m, turned from the "
                "facing at most %.0f deg; the same foot twice %d\n", what, r.steps, r.steps / seconds, r.flights, r.slide, r.sideWorst, r.twistWorst, r.sameTwice);
}

int main() {
    const float none = std::nanf("");
    auto ahead = [](float) { return 0.0f; };
    auto behind = [](float) { return kPi; };
    auto facing0 = [](float) { return 0.0f; };
    {   // standing still: no step at all
        const Run r = Go(3.0f, [&](float) { return none; }, [](float) { return 0.0f; }, facing0);
        Print("standing", r, 3.0f);
        CHECK(r.steps == 0);
        CHECK(r.slide < 1e-5f);
    }
    for (int k = 0; k < 3; ++k) {   // walking ahead, walking back, running back - facing ahead
        const float speed = k == 2 ? 5.0f : 1.5f;
        const Run r = k == 0 ? Go(4.0f, ahead, [&](float) { return speed; }, facing0) : Go(4.0f, behind, [&](float) { return speed; }, facing0);
        Print(k == 0 ? "walking ahead 1.5 m/s" : k == 1 ? "walking back 1.5 m/s" : "running back 5 m/s", r, 4.0f);
        CHECK(r.slide < 1e-5f);              // a foot on the ground stays where it was put
        CHECK(r.sideWorst > 0.05f);          // left stays left, right right
        CHECK(r.sameTwice == 0);             // they take turns
        if (k < 2) { CHECK(r.steps >= 7 && r.steps <= 11); CHECK(r.flights == 0); }   // about two a second walking, never both feet up
        else { CHECK(r.steps >= 15 && r.steps <= 24); CHECK(r.flights > 0); }         // running: four to five a second, with flights
    }
    {   // standing, the body turned round 90 deg a second (the camera round him): the feet step after it, never twisted far, never crossed
        const Run r = Go(4.0f, [&](float) { return none; }, [](float) { return 0.0f; }, [](float t) { return 1.5707963f * t; });
        Print("turned round standing 90 deg/s", r, 4.0f);
        CHECK(r.steps >= 6);
        CHECK(r.twistWorst < dollfeet::kTurnDeg + 30.0f);   // (a fast turn: what it turns while the other foot steps comes on top)
        CHECK(r.sideWorst > 0.05f);
        CHECK(r.slide < 1e-5f);
    }
    {   // ahead 1.5 s, then back
        const Run r = Go(4.0f, [](float t) { return t < 1.5f ? 0.0f : kPi; }, [](float) { return 1.5f; }, facing0);
        Print("ahead, then back at 1.5 s", r, 4.0f);
        CHECK(r.slide < 1e-5f);
        CHECK(r.sideWorst > 0.05f);
    }
    {   // running back 6.5 m/s on a leg that reaches 0.4 m along the ground from under the hip (Serg's paladin, 0.190.1: the person's stride put the feet
        // ~0.85 m out and the legs could not reach them): no planted foot farther than its reach (and its stance) from the body; still in turns, no slide
        dollfeet::Doll doll;
        P2 body{};
        const float dt = 1.0f / 90.0f, reach = 0.4f;
        float farthest = 0.0f, slide = 0.0f;
        P2 was[2];
        bool down[2] = {false, false};
        for (int i = 0; i < 360; ++i) {
            const P2 vel = dollfeet::Mul(dollfeet::Ahead(kPi), 6.5f * kUpm);
            body = dollfeet::Add(body, dollfeet::Mul(vel, dt));
            doll.Step(dt, body, 0.0f, vel, kStance, kUpm, reach);
            for (int side = 0; side < 2; ++side) {
                const dollfeet::Foot& f = doll.Get(side);
                if (!f.flying && i > 45) farthest = std::max(farthest, std::fabs(dollfeet::Dot(dollfeet::Sub(f.at, body), dollfeet::Ahead(0.0f))) / kUpm);
                if (!f.flying && down[side]) slide = std::max(slide, dollfeet::Len(dollfeet::Sub(f.at, was[side])) / kUpm);
                down[side] = !f.flying;
                was[side] = f.at;
            }
        }
        std::printf("running back 6.5 m/s, the leg reaching 0.4 m: a planted foot at most %.2f m along from the body (its stance %.2f), %d steps in 4 s, "
                    "slide %.4f\n", farthest, std::fabs(kStance[0].z) / kUpm, doll.Steps(), slide);
        CHECK(farthest < 0.8f * reach + std::fabs(kStance[0].z) / kUpm + 0.05f);
        CHECK(doll.Steps() > 25);   // the steps come faster instead
        CHECK(slide < 1e-5f);
    }
    {   // standing, the body swung 22 deg one way (a turn with the mouse, under the walk's 30): both feet follow it, squared
        dollfeet::Doll doll;
        const float dt = 1.0f / 90.0f;
        for (int i = 0; i < 180; ++i) doll.Step(dt, P2{}, i < 20 ? 0.0f : 0.384f, P2{}, kStance, kUpm);
        const float t0 = std::fabs(dollfeet::WrapPi(0.384f - doll.Get(0).yaw)) * 57.2957795f, t1 = std::fabs(dollfeet::WrapPi(0.384f - doll.Get(1).yaw)) * 57.2957795f;
        std::printf("standing, turned 22 deg: %d steps, the feet left %.1f / %.1f deg off the facing\n", doll.Steps(), t0, t1);
        CHECK(doll.Steps() == 2);
        CHECK(t0 < 1.0f && t1 < 1.0f);
    }
    {   // the body jumps 20 m (an area change): the feet are put back under it, not flown there
        dollfeet::Doll doll;
        doll.Step(0.011f, P2{}, 0.0f, P2{}, kStance, kUpm);
        doll.Step(0.011f, P2{20.0f * kUpm, 0.0f}, 0.0f, P2{}, kStance, kUpm);
        CHECK(!doll.Get(0).flying && !doll.Get(1).flying);
        CHECK(std::fabs(doll.Get(1).at.x - (20.0f * kUpm + kStance[1].x)) < 1e-3f);
        CHECK(doll.Steps() == 0);
    }
    std::printf("doll_feet_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
