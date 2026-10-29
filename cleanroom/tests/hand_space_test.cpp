// Where a controller's grip comes from when the room's locate gives no whole pose (vr/hand_space.h): the flag sets from the Quest 2
// logs, the order of the fallbacks, the runtimes it is on for, and the pose sums that carry LOCAL into the room and the aim to the grip.
// No game, no headset.
//
//   hand_space_test     (exit code 0 = all passed)

#include <cmath>
#include <cstdio>

#include "hand_space.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace handspace;

// the log's letters: P/p position valid/tracked, R/r orientation
static uint64_t F(const char* s) {
    return (s[0] == 'P' ? kPosValid : 0) | (s[1] == 'p' ? kPosTracked : 0) | (s[2] == 'R' ? kOriValid : 0) | (s[3] == 'r' ? kOriTracked : 0);
}
static bool Near(const Pose& a, const Pose& b, float e = 1e-4f) {
    bool same = true, flipped = true;   // (q and -q are one turn)
    for (int i = 0; i < 4; ++i) { same = same && std::fabs(a.q[i] - b.q[i]) < e; flipped = flipped && std::fabs(a.q[i] + b.q[i]) < e; }
    for (int i = 0; i < 3; ++i) if (std::fabs(a.p[i] - b.p[i]) >= e) return false;
    return same || flipped;
}
static Pose Make(float ax, float ay, float az, float deg, float x, float y, float z) {
    Pose p;
    const float l = std::sqrt(ax * ax + ay * ay + az * az), h = 0.5f * deg * 0.0174532925f;
    p.q[0] = ax / l * std::sin(h); p.q[1] = ay / l * std::sin(h); p.q[2] = az / l * std::sin(h); p.q[3] = std::cos(h);
    p.p[0] = x; p.p[1] = y; p.p[2] = z;
    return p;
}

int main() {
    // -- the flags -------------------------------------------------------------
    CHECK(Whole(F("PpRr")));
    CHECK(Whole(F("P-R-")));                // valid, not tracked (a last known pose): whole, as before
    CHECK(!Whole(F("P--r")));               // the 0.183 Quest log: a turn "tracked" but not valid
    CHECK(!Sane(F("P--r")));
    CHECK(!Sane(F("-pR-")));                // 0.175's left grip: a place "tracked" but not valid
    CHECK(Sane(F("--Rr")));                 // a turn alone is a sane set
    CHECK(!Whole(F("--Rr")));
    CHECK(Whole(Repair(F("P--r"))));        // its numbers read as valid: a whole pose
    CHECK(Whole(Repair(F("-pR-"))));
    CHECK(!Whole(Repair(F("Pp--"))));       // no turn at all: nothing to repair
    CHECK(!Whole(Repair(F("----"))));
    CHECK(Repair(F("PpRr")) == F("PpRr"));

    // -- the order --------------------------------------------------------------
    Facts f;
    f.room = F("PpRr");
    f.localKnown = true; f.local = F("PpRr"); f.aim = true; f.learned = true; f.roomNumbers = true; f.repairedNearAim = true;
    CHECK(Pick(f) == Source::Room);         // a whole grip in the room: as it comes, nothing else looked at

    // the Quest 2 log: P--r in the room, the aim whole (the laser)
    Facts q;
    q.room = F("P--r"); q.roomNumbers = true;
    q.localKnown = true; q.local = F("PpRr");
    q.aim = true;
    CHECK(Pick(q) == Source::Local);        // the grip in LOCAL first
    q.localKnown = false;
    CHECK(Pick(q) == Source::AimRaw);       // LOCAL not placed in the room and nothing learned, the room's numbers far from the aim
    q.localKnown = true; q.local = F("P--r");   // LOCAL as broken as the room
    q.learned = true;
    CHECK(Pick(q) == Source::AimLearned);   // the aim with the learned offset before the room's doubtful numbers
    q.learned = false; q.repairedNearAim = true;
    CHECK(Pick(q) == Source::Repaired);     // the room's numbers where the aim says the controller is
    q.repairedNearAim = false;
    CHECK(Pick(q) == Source::AimRaw);       // ... not where the room's numbers put it far from the aim
    q.aim = false;
    CHECK(Pick(q) == Source::Repaired);     // no aim to hold them against: the room's numbers
    q.roomNumbers = false;
    CHECK(Pick(q) == Source::AsBefore);     // numbers that are none: as before

    // the second Quest 2 run (his hand tracking off, 2026-10-26 18:31): P--- in the room all the run, the laser's aim in LOCAL whole -
    // no turn to repair, so LOCAL, else the aim
    Facts p;
    p.room = F("P---"); p.roomNumbers = true; p.aim = true; p.repairedNearAim = true;
    CHECK(!Whole(Repair(p.room)));
    p.localKnown = true; p.local = F("PpRr");
    CHECK(Pick(p) == Source::Local);
    p.local = F("P---");
    CHECK(Pick(p) == Source::AimRaw);
    p.learned = true;
    CHECK(Pick(p) == Source::AimLearned);
    p.aim = false;
    CHECK(Pick(p) == Source::AsBefore);

    // a turn alone, nothing else (a controller out of the cameras' view): as before - held by the head, a turn alone
    Facts t;
    t.room = F("--Rr"); t.roomNumbers = true; t.localKnown = true; t.local = F("--Rr");
    CHECK(Pick(t) == Source::AsBefore);
    t.aim = true;
    CHECK(Pick(t) == Source::AimRaw);
    Facts none;
    CHECK(Pick(none) == Source::AsBefore);

    // -- the runtimes -----------------------------------------------------------
    CHECK(On(Mode::Auto, "Oculus"));
    CHECK(On(Mode::Auto, "Meta"));
    CHECK(!On(Mode::Auto, "SteamVR/OpenXR"));   // SteamVR, PS VR2 on it, Virtual Desktop: as before
    CHECK(!On(Mode::Auto, "VirtualDesktopXR"));
    CHECK(!On(Mode::Auto, nullptr));
    CHECK(On(Mode::Always, "SteamVR/OpenXR"));
    CHECK(!On(Mode::Never, "Oculus"));
    CHECK(ModeOf(0) == Mode::Never);
    CHECK(ModeOf(-1) == Mode::Never);
    CHECK(ModeOf(1) == Mode::Always);
    CHECK(ModeOf(2) == Mode::Auto);
    CHECK(ModeOf(5) == Mode::Auto);

    // -- the poses --------------------------------------------------------------
    // LOCAL 1.6 m under the room's floor-level origin, turned 30 deg: a grip located in LOCAL, carried into the room, equals the room's
    const Pose localInRoom = Make(0, 1, 0, 30.0f, 0.2f, 1.6f, -0.1f);
    const Pose gripInLocal = Make(1, 0.3f, -0.2f, 70.0f, 0.25f, -0.4f, -0.3f);
    const Pose gripInRoom = Then(localInRoom, gripInLocal);
    CHECK(Near(Then(Inverse(localInRoom), gripInRoom), gripInLocal));   // back again
    CHECK(Near(Inverse(Inverse(localInRoom)), localInRoom));
    {   // the room located in LOCAL and turned about is LOCAL in the room
        const Pose roomInLocal = Inverse(localInRoom);
        CHECK(Near(Then(Inverse(roomInLocal), gripInLocal), gripInRoom));
    }
    // a rotation by hand: 90 deg about +y takes +x to -z
    {
        const Pose r = Make(0, 1, 0, 90.0f, 0, 0, 0);
        Pose x; x.p[0] = 1.0f;
        const Pose o = Then(r, x);
        CHECK(std::fabs(o.p[0]) < 1e-5f && std::fabs(o.p[2] + 1.0f) < 1e-5f);
    }
    // the grip's offset learned from the aim gives the grip back from the aim alone, wherever the controller goes
    {
        const Pose aim = Make(0.2f, 1, 0.1f, -40.0f, 0.3f, 1.2f, -0.4f);
        const Pose grip = Then(aim, Make(1, 0, 0, 35.0f, 0.0f, -0.03f, 0.06f));   // a Touch's grip a few cm behind and under its pointer
        const Pose learned = Relative(aim, grip);
        CHECK(Near(Then(aim, learned), grip));
        const Pose moved = Then(Make(0, 0, 1, 80.0f, -0.5f, 0.2f, 0.3f), aim);   // the controller somewhere else
        const Pose gripMoved = Then(Make(0, 0, 1, 80.0f, -0.5f, 0.2f, 0.3f), grip);
        CHECK(Near(Then(moved, learned), gripMoved));
        CHECK(Distance(aim.p, grip.p) < kNearM);
    }
    // turns made unit length; none from nonsense
    {
        float u[4] = {0.0f, 0.0f, 0.0f, 1.1f};
        CHECK(Normalize(u) && std::fabs(u[3] - 1.0f) < 1e-6f);
        float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        CHECK(!Normalize(z));                // the numbers a runtime leaves when it gives none
        float n[4] = {NAN, 0.0f, 0.0f, 1.0f};
        CHECK(!Normalize(n));
    }

    std::printf("hand_space_test: %d of %d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
