// Offline checks of vr/attack_path.h ([gameplay] flat_attack_path): when it acts, the speed, and who a blow is for.
// Run: build/Release/attack_path_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>

#include "../../vr/attack_path.h"

using namespace attackpath;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestActs() {
    Ask a;
    a.key = 1; a.flatView = true; a.allowed = true; a.mode = 7;
    CHECK(Acts(a));
    a.mode = 8; CHECK(Acts(a));
    a.mode = 11; CHECK(Acts(a));
    for (uint32_t m : {1u, 2u, 3u, 4u, 9u, 10u, 12u, 13u, 18u}) { a.mode = m; CHECK(!Acts(a)); }
    a.mode = 7;
    a.key = 0; CHECK(!Acts(a)); a.key = 1;
    a.flatView = false; CHECK(!Acts(a)); a.flatView = true;
    a.menu = true; CHECK(!Acts(a)); a.menu = false;
    a.allowed = false; CHECK(!Acts(a)); a.allowed = true;
    a.skillFlags = 1; CHECK(!Acts(a));
    a.skillFlags = 0x1000; CHECK(!Acts(a));
    a.skillFlags = 4; CHECK(Acts(a));
    CHECK(!RunAllowed(1) && RunAllowed(2));
}

static void TestSpeed() {
    CHECK(PercentOf(kBaseWalk, false) == 100);
    CHECK(PercentOf(kBaseRun, true) == 100);
    CHECK(PercentOf(kBaseWalk * 3 / 2, false) == 150);
    CHECK(VelocityFor(100, false) == kBaseWalk);
    CHECK(VelocityFor(150, true) == kBaseRun * 3 / 2);
    CHECK(VelocityFor(0, false) == kBaseWalk / 4);
    CHECK(PercentOf(0, false) == 100);
}

static void TestGrid() {
    float x, y;
    ScreenToGrid(0.0f, 1.0f, &x, &y);   // up the screen: both grid axes go down
    CHECK(x < 0.0f && y < 0.0f && std::fabs(x - y) < 1e-5f);
    ScreenToGrid(1.0f, 0.0f, &x, &y);   // right on the screen: x up, y down
    CHECK(x > 0.0f && y < 0.0f && std::fabs(x + y) < 1e-5f);
    CHECK(std::fabs(x * x + y * y - 1.0f) < 1e-5f);
}

static void TestPick() {
    const float cone = std::cos(45.0f * 0.0174532925f);
    uint32_t out[kCandidates];
    const Candidate some[] = {{1, 3.0f, 0.0f}, {2, 1.0f, 0.0f}, {3, 0.0f, 2.0f}, {4, -1.0f, 0.0f}, {5, 30.0f, 0.0f}, {6, 2.0f, 1.0f}};
    size_t n = PickFront(0, 0, 1, 0, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 3);                        // 1, 2 and 6 before him; 3 at the side, 4 behind, 5 too far
    CHECK(out[0] == 2 && out[1] == 6 && out[2] == 1);   // the nearest first
    n = PickFront(0, 0, -1, 0, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 1 && out[0] == 4);         // he turned: another target
    n = PickFront(0, 0, 0, -1, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 0);                        // nobody before him: the blow goes into the air
    const Candidate onTop[] = {{9, 0.0f, 0.0f}};
    CHECK(PickFront(0, 0, 1, 0, cone, 10.0f, onTop, 1, out, kCandidates) == 1);
    Candidate many[20];
    for (int i = 0; i < 20; ++i) many[i] = {(uint32_t)(i + 1), 20.0f - (float)i, 0.0f};
    n = PickFront(0, 0, 1, 0, cone, 100.0f, many, 20, out, kCandidates);
    CHECK(n == kCandidates && out[0] == 20 && out[7] == 13);   // more than fit: the nearest stay
    CHECK(PickFront(0, 0, 1, 0, cone, 100.0f, many, 20, out, 2) == 2);
}

static void TestStartedFirst() {
    uint32_t ids[4] = {5, 6, 7, 8};
    StartedFirst(ids, 4, 7);   // the monster clicked still before the camera: first, the rest in their order
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 4, 7);   // already first
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 4, 9);   // not there (gone, or no longer before the camera): the nearest stays first
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 2, 8);   // only the first n count
    CHECK(ids[0] == 7 && ids[1] == 5);
    StartedFirst(ids, 0, 7);
    CHECK(ids[0] == 7);
}

static void TestRest() {
    CHECK(Ranged(11, 4) && Ranged(7, 2) && Ranged(7, 3) && !Ranged(7, 4) && !Ranged(8, 0));
    CHECK(StepOn(10, 10) && StepOn(13, 10) && !StepOn(14, 10));
}

static void TestNet() {
    CHECK(Allowed(false, true, false) && Allowed(false, false, true));   // the server here; a host that said yes
    CHECK(!Allowed(false, false, false));                                // joined, the host silent or without the tick
    CHECK(!Allowed(true, true, true));                                   // never online
    CHECK(ClientKey(false, true, 1, false, 0) == 1 && ClientKey(false, true, 0, true, 1) == 0);   // the server here: our own box
    CHECK(ClientKey(false, false, 0, true, 1) == 1 && ClientKey(false, false, 1, true, 0) == 0);  // joined: the host's, whatever ours says
    CHECK(ClientKey(false, false, 1, false, 1) == 0);                                             // the host silent: as in the game
    CHECK(ClientKey(true, false, 1, true, 1) == 0 && ClientKey(true, true, 1, true, 1) == 0);     // never online
    CHECK(Fresh(100, 90, 20) && !Fresh(200, 90, 20) && !Fresh(100, 0, 20) && !Fresh(80, 90, 20));
    CHECK(SyncNow(15, 10) && !SyncNow(14, 10));
    HelloMsg h{7}, h2{};
    CHECK(ParseHello(&h, sizeof h, &h2) && h2.hero == 7);
    CHECK(!ParseHello(&h, 3, &h2));
    h.hero = 0; CHECK(!ParseHello(&h, sizeof h, &h2));
    AllowMsg a{2}, a2{};
    CHECK(ParseAllow(&a, sizeof a, &a2) && a2.key == 2);
    a.key = 3; CHECK(!ParseAllow(&a, sizeof a, &a2));
    FrontMsg f{}, f2{};
    f.hero = 5; f.n = 2; f.ids[0] = 11; f.ids[1] = 12;
    CHECK(ParseFront(&f, sizeof f, &f2) && f2.n == 2 && f2.ids[1] == 12);
    CHECK(sizeof f <= 224);
    f.n = kCandidates + 1; CHECK(!ParseFront(&f, sizeof f, &f2));
    CHECK(!ParseFront(&f, 4, &f2));
    const uint32_t x[2] = {1, 2}, y[2] = {1, 3};
    CHECK(SameFront(x, 2, x, 2) && !SameFront(x, 2, y, 2) && !SameFront(x, 1, x, 2) && SameFront(x, 0, y, 0));
}

int main() {
    TestNet();
    TestActs();
    TestSpeed();
    TestGrid();
    TestPick();
    TestStartedFirst();
    TestRest();
    std::printf("attack_path_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
