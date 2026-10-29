// Offline checks of vr/walk_latency.h ([debug] walk_start_log): one line per walk start of BodyWalk's, on synthetic game frames - BodyWalk's
// stick climbing from rest as its averages do, the game's walk given a vector once it passes the dead zone (or the lift), the hero moving.
// Run: build/Release/walk_latency_test.exe (exit code 0 = passed)

#include <cstdio>
#include <cstring>

#include "../../vr/walk_latency.h"

using namespace walklat;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(double a, double b, double eps = 12.0) { return a - b <= eps && b - a <= eps; }

constexpr double kFrame = 1.0 / 90.0;

// A world: BodyWalk's stick from a function of time, the game walks past the dead zone (or past `lift` when lifted), the hero walks
// `speed` units a second once the game walks, `heroLagS` after it (the game's tick and the client's step).
struct World {
    double startS = 1.0;      // when BodyWalk's stick starts to climb
    double climbS = 1.0;      // from 0 to 1 over this
    float lift = 0.0f;        // [input] bodywalk_walk_lift
    double heroLagS = 0.06;
    float speed = 3.0f;
    bool ownPushed = false;
    float Bw(double t) const {
        if (t < startS) return 0.0f;
        const double v = (t - startS) / climbS;
        return v > 1.0 ? 1.0f : (float)v;
    }
};

struct Run {
    int started = 0, done = 0;
    Result last;
};

static Run Play(const World& w, double untilS) {
    Watcher watch;
    Run r;
    double walkFrom = -1.0, heroX = 0.0;
    for (double t = 0.0; t < untilS; t += kFrame) {
        const float bw = w.Bw(t);
        const bool lifted = w.lift > 0.0f && bw >= w.lift && bw <= kDeadZone;
        const bool given = bw > kDeadZone || lifted;
        if (given && walkFrom < 0.0) walkFrom = t;
        if (!given) walkFrom = -1.0;
        if (walkFrom >= 0.0 && t - walkFrom > w.heroLagS) heroX += w.speed * kFrame;
        Sample s;
        s.t = t; s.bw = bw; s.given = given; s.givenLen = lifted ? 0.3174f : bw; s.lifted = lifted;
        s.ownPushed = w.ownPushed;
        s.heroOk = true; s.heroX = (float)heroX; s.heroZ = 0.0f;
        Result res;
        const Event e = watch.Feed(s, &res);
        if (e == Event::Started) ++r.started;
        if (e == Event::Done) { ++r.done; r.last = res; }
    }
    return r;
}

static void TestPlainStart() {
    World w;   // the stick climbs 1.0 a second: past 0.04 at 40 ms, past the dead zone at ~316 ms
    const Run r = Play(w, 4.0);
    CHECK(r.started == 1 && r.done == 1);
    CHECK(r.last.end == Result::End::Moved);
    CHECK(Near(r.last.dz, 316.0 - 40.0, 15.0));
    CHECK(Near(r.last.given, r.last.dz, 15.0));
    CHECK(r.last.moved > r.last.given && Near(r.last.moved, r.last.given + 60.0 + 17.0, 25.0));
    CHECK(!r.last.lifted);
    CHECK(r.last.peakBeforeDz > 0.30f && r.last.peakBeforeDz <= kDeadZone);
    char line[400];
    Text(r.last, line, sizeof line);
    std::printf("  %s\n", line);
    CHECK(std::strstr(line, "(on its own)") != nullptr && std::strstr(line, "the hero moved +") != nullptr);
}

static void TestLiftedStart() {
    World w;
    w.lift = 0.12f;   // the game walks from 0.12: given ~80 ms after the stir, long before the dead zone
    const Run r = Play(w, 4.0);
    CHECK(r.done == 1);
    CHECK(r.last.lifted);
    CHECK(Near(r.last.given, 120.0 - 40.0, 15.0));
    CHECK(r.last.given < r.last.dz - 150.0);
    char line[400];
    Text(r.last, line, sizeof line);
    CHECK(std::strstr(line, "lifted") != nullptr);
}

static void TestNoRestNoStart() {
    World w;
    w.startS = 0.2;   // only 0.2 s at rest before the climb
    const Run r = Play(w, 4.0);
    CHECK(r.started == 0 && r.done == 0);
}

static void TestControllerPushedAtStart() {
    World w;
    w.ownPushed = true;
    const Run r = Play(w, 4.0);
    CHECK(r.started == 0);   // the controller walks: no BodyWalk start is watched
}

static void TestStirThatNeverWalks() {
    // BodyWalk's stick to 0.2 and stays (a slow walk under the dead zone, no lift): told once, after the 3 s window
    Watcher watch;
    int done = 0;
    Result res;
    for (double t = 0.0; t < 6.0; t += kFrame) {
        Sample s;
        s.t = t; s.bw = t < 1.0 ? 0.0f : 0.2f; s.heroOk = true;
        if (watch.Feed(s, &res) == Event::Done) ++done;
    }
    CHECK(done == 1);
    CHECK(res.end == Result::End::Window && res.given < 0.0 && res.dz < 0.0 && res.moved < 0.0);
    // a short twitch (0.4 s) that never walked: nothing
    Watcher w2;
    done = 0;
    for (double t = 0.0; t < 4.0; t += kFrame) {
        Sample s;
        s.t = t; s.bw = (t > 1.0 && t < 1.4) ? 0.15f : 0.0f; s.heroOk = true;
        if (w2.Feed(s, &res) == Event::Done) ++done;
    }
    CHECK(done == 0);
}

static void TestTwoStartsTwoLines() {
    // walk, stand a second, walk again: two lines
    Watcher watch;
    int done = 0;
    double heroX = 0.0;
    for (double t = 0.0; t < 8.0; t += kFrame) {
        const bool walking = (t > 1.0 && t < 3.0) || (t > 4.5 && t < 6.5);
        const float bw = walking ? 0.6f : 0.0f;
        if (walking) heroX += 3.0 * kFrame;
        Sample s;
        s.t = t; s.bw = bw; s.given = walking; s.givenLen = bw; s.heroOk = true; s.heroX = (float)heroX;
        Result res;
        if (watch.Feed(s, &res) == Event::Done) ++done;
    }
    CHECK(done == 2);
}

int main() {
    TestPlainStart();
    TestLiftedStart();
    TestNoRestNoStart();
    TestControllerPushedAtStart();
    TestStirThatNeverWalks();
    TestTwoStartsTwoLines();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("walk_latency_test: all %d checks passed\n", g_checks);
    return 0;
}
