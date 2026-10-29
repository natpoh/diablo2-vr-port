// Offline checks of vr/belt_flick.h: belt potions in VR F1, F2, F3 with the left trigger held and a flick of a thumbstick.
// Run: build/Release/belt_flick_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../vr/belt_flick.h"

using namespace beltflick;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static const Stick kMid{0.0f, 0.0f};
static Stick At(float x, float y) { return Stick{x, y}; }

static void TestDirections() {
    CHECK(DirOf(At(-1.0f, 0.0f)) == Dir::Left && ColumnOf(Dir::Left) == 0);
    CHECK(DirOf(At(0.0f, 1.0f)) == Dir::Up && ColumnOf(Dir::Up) == 1);
    CHECK(DirOf(At(1.0f, 0.0f)) == Dir::Right && ColumnOf(Dir::Right) == 2);
    CHECK(DirOf(At(0.0f, -1.0f)) == Dir::Down && ColumnOf(Dir::Down) == 3);
    CHECK(ColumnOf(Dir::None) == -1);
    // a little off the axis is still that way
    CHECK(DirOf(At(-0.9f, 0.3f)) == Dir::Left);
    CHECK(DirOf(At(0.3f, 0.9f)) == Dir::Up);
    CHECK(DirOf(At(0.9f, -0.4f)) == Dir::Right);
    CHECK(DirOf(At(-0.4f, -0.9f)) == Dir::Down);
    // a diagonal is no direction yet
    CHECK(DirOf(At(0.7f, 0.7f)) == Dir::None);
    CHECK(DirOf(At(-0.6f, 0.7f)) == Dir::None);
    CHECK(DirOf(At(0.0f, 0.0f)) == Dir::None);
    CHECK(DirOf(At(NAN, 1.0f)) == Dir::None && DirOf(At(1.0f, INFINITY)) == Dir::None);
    for (int d = 0; d < 5; ++d) CHECK(DirName((Dir)d)[0] != 0);
}

// Drives the machine the way Feed does: a frame at a time.
struct Run {
    Machine m;
    std::vector<int> fired;
    bool holding = false;
    void Frame(float trigger, Stick l, Stick r, bool enabled = true) {
        const Machine::Out o = m.Step(trigger, l, r, enabled);
        holding = o.holding;
        if (o.column >= 0) fired.push_back(o.column);
    }
    void Left(float trigger, float x, float y) { Frame(trigger, At(x, y), kMid); }
    void Right(float trigger, float x, float y) { Frame(trigger, kMid, At(x, y)); }
};

static void TestFourWays() {
    const struct { float x, y; int column; } ways[] = {{-1, 0, 0}, {0, 1, 1}, {1, 0, 2}, {0, -1, 3}};
    for (int s = 0; s < 2; ++s) {   // either stick
        for (const auto& w : ways) {
            Run r;
            r.Frame(1.0f, kMid, kMid);          // the trigger goes down, the sticks are in the middle
            if (s == 0) r.Left(1.0f, w.x * 0.5f, w.y * 0.5f); else r.Right(1.0f, w.x * 0.5f, w.y * 0.5f);   // halfway: not yet
            CHECK(r.fired.empty());
            if (s == 0) r.Left(1.0f, w.x, w.y); else r.Right(1.0f, w.x, w.y);
            CHECK(r.fired.size() == 1 && r.fired[0] == w.column);
        }
    }
}

static void TestOnePerFlick() {
    Run r;
    r.Frame(1.0f, kMid, kMid);
    r.Left(1.0f, 0.0f, 1.0f);
    CHECK(r.fired.size() == 1 && r.fired[0] == 1);
    // held out: no second potion, however long
    for (int i = 0; i < 100; ++i) r.Left(1.0f, 0.0f, 1.0f);
    CHECK(r.fired.size() == 1);
    // part of the way back is not the middle
    r.Left(1.0f, 0.0f, 0.5f);
    r.Left(1.0f, 0.0f, 1.0f);
    CHECK(r.fired.size() == 1);
    // to the middle and out again: the next potion; another way, another column
    r.Left(1.0f, 0.0f, 0.1f);
    r.Left(1.0f, 1.0f, 0.0f);
    CHECK(r.fired.size() == 2 && r.fired[1] == 2);
    r.Left(1.0f, 0.0f, 0.0f);
    r.Left(1.0f, -1.0f, 0.0f);
    r.Left(1.0f, 0.0f, 0.0f);
    r.Left(1.0f, 0.0f, -1.0f);
    CHECK(r.fired.size() == 4 && r.fired[2] == 0 && r.fired[3] == 3);
    // a swing from one side through the middle to the other counts twice: the middle was passed (the frame inside kOff) - and only then
    Run q;
    q.Frame(1.0f, kMid, kMid);
    q.Left(1.0f, -1.0f, 0.0f);
    q.Left(1.0f, 1.0f, 0.0f);   // straight across in one frame: no frame in the middle between, so no second potion
    CHECK(q.fired.size() == 1 && q.fired[0] == 0);
    q.Left(1.0f, 0.0f, 0.0f);
    q.Left(1.0f, 1.0f, 0.0f);
    CHECK(q.fired.size() == 2 && q.fired[1] == 2);
}

static void TestTrigger() {
    // no trigger, no potion, whatever the sticks do
    Run r;
    for (int i = 0; i < 5; ++i) { r.Left(0.0f, 0.0f, 0.0f); r.Left(0.0f, 0.0f, 1.0f); r.Left(0.0f, -1.0f, 0.0f); r.Right(0.0f, 1.0f, 0.0f); }
    CHECK(r.fired.empty() && !r.holding);
    // a half-pressed trigger below the level does not count
    r.Frame(0.5f, kMid, kMid);
    r.Left(0.5f, 0.0f, 1.0f);
    CHECK(r.fired.empty() && !r.holding);
    // the trigger alone does nothing, and holds the left stick back from walking
    Run t;
    t.Frame(0.9f, kMid, kMid);
    CHECK(t.fired.empty() && t.holding);
    // hysteresis: 0.4 is still held after 0.9, not before
    t.Frame(0.4f, kMid, kMid);
    CHECK(t.holding);
    t.Frame(0.3f, kMid, kMid);
    CHECK(!t.holding);
    t.Frame(0.4f, kMid, kMid);
    CHECK(!t.holding);
    t.Frame(0.56f, kMid, kMid);
    CHECK(t.holding);
    // a stick already pushed when the trigger goes down is a walk, not a flick - until it has been to the middle
    Run w;
    w.Left(0.0f, 0.0f, 1.0f);       // walking forward
    w.Left(1.0f, 0.0f, 1.0f);       // the trigger goes down: still pushed
    w.Left(1.0f, 0.0f, 1.0f);
    CHECK(w.fired.empty() && w.holding);
    w.Left(1.0f, 1.0f, 0.0f);       // another way without the middle in between: still nothing
    CHECK(w.fired.empty());
    w.Left(1.0f, 0.0f, 0.0f);
    w.Left(1.0f, 1.0f, 0.0f);
    CHECK(w.fired.size() == 1 && w.fired[0] == 2);
    // the trigger let go and pressed again: armed again only from the middle
    w.Left(0.0f, 1.0f, 0.0f);
    w.Left(1.0f, 1.0f, 0.0f);
    CHECK(w.fired.size() == 1);
    w.Left(1.0f, 0.0f, 0.0f);
    w.Left(1.0f, -1.0f, 0.0f);
    CHECK(w.fired.size() == 2 && w.fired[1] == 0);
    // letting the trigger go with the stick out: nothing then either
    w.Left(0.0f, 0.0f, 1.0f);
    w.Left(0.0f, 0.0f, 0.0f);
    CHECK(w.fired.size() == 2 && !w.holding);
}

static void TestBothSticks() {
    // both flicked in one frame: one potion (the left stick's), and the right one is spent too
    Run r;
    r.Frame(1.0f, kMid, kMid);
    r.Frame(1.0f, At(0.0f, 1.0f), At(1.0f, 0.0f));
    CHECK(r.fired.size() == 1 && r.fired[0] == 1);
    r.Frame(1.0f, At(0.0f, 1.0f), At(1.0f, 0.0f));
    CHECK(r.fired.size() == 1);
    // one after the other: two potions
    Run q;
    q.Frame(1.0f, kMid, kMid);
    q.Right(1.0f, 0.0f, -1.0f);
    q.Frame(1.0f, At(-1.0f, 0.0f), At(0.0f, -1.0f));   // the right one still out; the left flicks
    CHECK(q.fired.size() == 2 && q.fired[0] == 3 && q.fired[1] == 0);
    // either stick is armed on its own: the right one resting in the middle, the left one pushed since before: only the right flicks
    Run p;
    p.Frame(0.0f, At(0.0f, 1.0f), kMid);
    p.Frame(1.0f, At(0.0f, 1.0f), kMid);
    p.Frame(1.0f, At(0.0f, 1.0f), At(-1.0f, 0.0f));
    CHECK(p.fired.size() == 1 && p.fired[0] == 0);
}

static void TestDiagonal() {
    Run r;
    r.Frame(1.0f, kMid, kMid);
    r.Left(1.0f, 0.55f, 0.55f);     // a diagonal at full strength (0.78): wait
    CHECK(r.fired.empty());
    r.Left(1.0f, 0.75f, 0.45f);     // settling towards the right: 0.75 / 0.45 = 1.67
    CHECK(r.fired.size() == 1 && r.fired[0] == 2);
    // a diagonal that goes back to the middle without settling: no potion, and the stick is ready again
    Run q;
    q.Frame(1.0f, kMid, kMid);
    q.Left(1.0f, 0.55f, 0.55f);
    q.Left(1.0f, 0.2f, 0.2f);
    CHECK(q.fired.empty() && q.m.Armed(0));
    q.Left(1.0f, 0.0f, 1.0f);
    CHECK(q.fired.size() == 1 && q.fired[0] == 1);
}

static void TestEnabled() {
    // not enabled (the key off, a panel, no focus, a view that is not ours): nothing, nothing held, everything let go
    Run r;
    r.Frame(1.0f, kMid, kMid);
    r.Left(1.0f, 0.0f, 1.0f);
    CHECK(r.fired.size() == 1);
    r.Frame(1.0f, kMid, kMid, false);
    CHECK(!r.holding && !r.m.Held());
    r.Frame(1.0f, At(0.0f, 1.0f), kMid, false);
    CHECK(r.fired.size() == 1 && !r.holding);
    // back on with the trigger still down and the stick still out: not armed until the middle
    r.Frame(1.0f, At(0.0f, 1.0f), kMid);
    r.Frame(1.0f, At(1.0f, 0.0f), kMid);
    CHECK(r.fired.size() == 1 && r.holding);
    r.Frame(1.0f, kMid, kMid);
    r.Frame(1.0f, At(1.0f, 0.0f), kMid);
    CHECK(r.fired.size() == 2 && r.fired[1] == 2);
    // a trigger that is not a number
    r.Frame(NAN, At(0.0f, 1.0f), kMid);
    CHECK(!r.holding && r.fired.size() == 2);
    // sticks that are not numbers do not fire
    Run n;
    n.Frame(1.0f, kMid, kMid);
    n.Frame(1.0f, At(NAN, NAN), At(INFINITY, 0.0f));
    CHECK(n.fired.empty());
}

static void TestSlowPush() {
    // a slow push fires once, when it passes the flick circle, not before and not again
    Run r;
    r.Frame(1.0f, kMid, kMid);
    int firedAt = -1;
    for (int i = 0; i <= 20; ++i) {
        r.Left(1.0f, 0.0f, i / 20.0f);
        if (!r.fired.empty() && firedAt < 0) firedAt = i;
    }
    CHECK(r.fired.size() == 1 && (firedAt == 14 || firedAt == 15));   // 14 / 20 = 0.70, the circle's edge (a float's last bit decides)
}

int main() {
    TestDirections();
    TestFourWays();
    TestOnePerFlick();
    TestTrigger();
    TestBothSticks();
    TestDiagonal();
    TestEnabled();
    TestSlowPush();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("belt_flick_test: all %d checks passed\n", g_checks);
    return 0;
}
