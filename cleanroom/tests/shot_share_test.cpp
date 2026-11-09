// shot_share_test - a player's bow shot to the other players, the pure part (vr/shot_share.h): the message on the wire, and the players' lines kept by unit
// (the press's heading, the latest line, how long either counts).
#include <cmath>
#include <cstdio>

#include "../../vr/shot_share.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace shotshare;

bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

Shot Sample() {
    Shot s;
    s.seq = 41;
    s.flags = kTarget | kStick | kPress;
    s.theta = -0.8f;
    s.start[0] = 10680.25f; s.start[1] = 4.6f; s.start[2] = 8402.5f;
    const float d[3] = {0.6f, -0.1f, -0.79f};
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; ++i) s.dir[i] = d[i] / l;
    s.at[0] = 10700.0f; s.at[1] = 0.25f; s.at[2] = 8376.0f;
    s.hero[0] = 10680.0f; s.hero[1] = 0.0f; s.hero[2] = 8402.0f;
    s.gridDist = 32.4f;
    s.upm = 3.15f;
    return s;
}

void TestWire() {
    std::printf("the message: built and read back\n");
    const Shot s = Sample();
    uint8_t b[poseshare::kMaxPayload];
    const uint32_t n = BuildShotMsg(7001, s, b, sizeof b);
    EXPECT(n == kShotMsgBytes && n <= poseshare::kMaxPayload, "%u bytes", n);
    uint32_t unit = 0;
    Shot r;
    EXPECT(ParseShotMsg(b, n, &unit, &r), "read back");
    EXPECT(unit == 7001 && r.seq == 41 && r.flags == s.flags, "the unit, the seq, the flags");
    EXPECT(Near(r.theta, s.theta, 1e-4f), "the heading %.5f", r.theta);
    for (int i = 0; i < 3; ++i) {
        EXPECT(r.start[i] == s.start[i] && r.at[i] == s.at[i] && r.hero[i] == s.hero[i], "the points exact (%d)", i);
        EXPECT(Near(r.dir[i], s.dir[i], 1e-4f), "the line (%d): %.5f", i, r.dir[i]);
    }
    EXPECT(Near(r.gridDist, 32.4f, 0.006f) && Near(r.upm, 3.15f, 0.0006f), "the distance %.3f and the scale %.4f", r.gridDist, r.upm);
}

void TestRefused() {
    std::printf("what is refused\n");
    Shot s = Sample();
    uint8_t b[poseshare::kMaxPayload];
    EXPECT(BuildShotMsg(0, s, b, sizeof b) == 0, "no unit");
    EXPECT(BuildShotMsg(1, s, b, kShotMsgBytes - 1) == 0, "no room");
    s.start[1] = NAN;
    EXPECT(BuildShotMsg(1, s, b, sizeof b) == 0, "a point that is not a number");
    s = Sample();
    const uint32_t n = BuildShotMsg(9, s, b, sizeof b);
    uint32_t unit = 0;
    Shot r;
    EXPECT(!ParseShotMsg(b, n - 1, &unit, &r), "short");
    b[0] = poseshare::kVersion + 1;
    EXPECT(!ParseShotMsg(b, n, &unit, &r), "another version");
    BuildShotMsg(9, s, b, sizeof b);
    for (int i = 0; i < 6; ++i) b[23 + i] = 0;
    EXPECT(!ParseShotMsg(b, n, &unit, &r), "no line");
}

void TestOwners() {
    std::printf("the players' lines: the press's heading kept through its updates\n");
    Owners o;
    Shot s = Sample();
    o.Note(5, s, 1000);
    s.flags = kTarget;
    s.theta = -0.6f;   // the crosshair moved during the attack
    o.Note(5, s, 1300);
    Line l;
    EXPECT(o.Get(5, 1400, &l) && l.pressKnown && Near(l.pressTheta, -0.8f, 1e-6f) && Near(l.shot.theta, -0.6f, 1e-6f), "the press's -0.8, the latest -0.6");
    s.seq = 42;
    s.theta = 1.0f;
    o.Note(5, s, 2000);
    EXPECT(o.Get(5, 2100, &l) && Near(l.pressTheta, 1.0f, 1e-6f), "a new seq: a new press");
    EXPECT(!o.Get(5, 2000 + kLineMs + 1, &l), "a stale line");
    EXPECT(!o.Get(6, 2100, &l), "another player's: none");
    Owners p;
    Shot t = Sample();
    p.Note(5, t, 0);
    t.flags = 0;
    for (uint64_t at = 500; at <= 3500; at += 500) p.Note(5, t, at);   // the same press's updates for 3.5 s
    EXPECT(!p.Get(5, 3600, &l), "a press older than %llu ms counts for none of its updates", (unsigned long long)kPressMs);
    Owners q;
    for (uint32_t u = 1; u <= kMaxOwners + 2; ++u) q.Note(u, Sample(), 100 * u);
    EXPECT(q.Get(kMaxOwners + 2, 100 * (kMaxOwners + 2), &l) && !q.Get(1, 100 * (kMaxOwners + 2), &l), "full: the oldest goes");
}

}  // namespace

int main() {
    TestWire();
    TestRefused();
    TestOwners();
    std::printf("\nshot_share_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
