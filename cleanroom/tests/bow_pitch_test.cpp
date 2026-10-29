// [bow] pitch_aim (vr/bow_pitch.h) on synthetic scenes, without the game and without a headset: the ray against a capsule, the ground's shear and
// its inverse, the models matched to their units by it, and whole shots - down the stairs, up them, on flat ground, at the ground with nothing on
// the line, into the air - against what the shot's heading was before (the line's own heading on the ground).
//
//   bow_pitch_test        every check below
//   bow_pitch_test -v     the shots' numbers too

#include "../../vr/bow_pitch.h"
#include "../../vr/melee_bodies.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace bowpitch;

namespace {

int g_fails = 0;
bool g_verbose = false;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

constexpr float kDeg = 57.2957795f;

V3 Unit(V3 v) { return v * (1.0f / Length(v)); }

// How far the cell (gx, gz) is from the 2D shot out of the hero's cell along `theta` (metres; a cell behind the hero: its distance from the hero).
float MissBy(const Scene& sc, float theta, float gx, float gz) {
    const float ux = std::cos(theta), uz = std::sin(theta), dx = gx - sc.heroGx, dz = gz - sc.heroGz;
    if (dx * ux + dz * uz < 0.0f) return std::sqrt(dx * dx + dz * dz);
    return std::fabs(dx * uz - dz * ux);
}

void TestRayCapsule() {
    std::printf("ray against a capsule\n");
    const V3 a{5, 0.3f, 0}, b{5, 1.5f, 0};
    float t = -1;
    // straight at the body: enters at 5 - r
    EXPECT(RayCapsule({0, 1, 0}, {1, 0, 0}, a, b, 0.3f, 100, &t) && std::fabs(t - 4.7f) < 1e-4f, "body: t %.4f", t);
    // over it: misses; just over the top ball's centre: meets the ball, a little later than the body would be met
    EXPECT(!RayCapsule({0, 1.85f, 0}, {1, 0, 0}, a, b, 0.3f, 100, &t), "over the top");
    EXPECT(RayCapsule({0, 1.75f, 0}, {1, 0, 0}, a, b, 0.3f, 100, &t) && t > 4.7f && t < 5.0f, "the top ball: t %.4f", t);
    // beside it
    EXPECT(!RayCapsule({0, 1, 0.31f}, {1, 0, 0}, a, b, 0.3f, 100, &t), "beside");
    // down along the axis: the top ball first
    EXPECT(RayCapsule({5, 4, 0}, {0, -1, 0}, a, b, 0.3f, 100, &t) && std::fabs(t - (4.0f - 1.8f)) < 1e-4f, "along the axis: t %.4f", t);
    // starting inside: 0
    EXPECT(RayCapsule({5, 1, 0.1f}, {1, 0, 0}, a, b, 0.3f, 100, &t) && t == 0.0f, "inside: t %.4f", t);
    // behind the start: none; beyond maxT: none
    EXPECT(!RayCapsule({0, 1, 0}, {-1, 0, 0}, a, b, 0.3f, 100, &t), "behind");
    EXPECT(!RayCapsule({0, 1, 0}, {1, 0, 0}, a, b, 0.3f, 4.0f, &t), "beyond maxT");
    // a slanted capsule and slanted rays: the entry is on its surface and the ray is outside it just before (against a brute walk)
    const V3 sa{3, 0.2f, 1}, sb{4, 1.6f, 1.5f};
    const float r = 0.35f;
    int met = 0, agree = 0;
    for (int i = 0; i < 400; ++i) {
        const float u = (i % 20) / 19.0f, v = (i / 20) / 19.0f;
        const V3 o{0, 1.6f, 0};
        const V3 d = Unit({3.5f, -1.5f + 2.6f * v, -0.5f + 3.0f * u});
        float tc = -1;
        const bool hit = RayCapsule(o, d, sa, sb, r, 100, &tc);
        float tw = -1;   // the brute walk: the first step inside
        for (float s = 0; s < 10; s += 0.0005f)
            if (SegmentDist2(o + d * s, sa, sb) <= r * r) { tw = s; break; }
        if (hit) ++met;
        if (hit == (tw >= 0) && (!hit || std::fabs(tc - tw) < 0.002f)) ++agree;
        else if (g_verbose) std::printf("    ray %d: capsule %d t %.4f, walk t %.4f\n", i, hit, tc, tw);
    }
    EXPECT(agree == 400, "the walk agrees on %d of 400 rays", agree);
    EXPECT(met > 60 && met < 340, "a fair share of the fan met it: %d", met);
}

void TestShear() {
    std::printf("the ground's shear\n");
    EXPECT(kShear == meleetouch::kGroundShear, "the same constant as the touch's: %.9f %.9f", kShear, meleetouch::kGroundShear);
    // a cell on raised ground is drawn pushed along x and z by the same amount; the push taken off gives the cell back
    const V3 p = GridToGround(10.0f, -4.0f, 1.5f);
    EXPECT(std::fabs(p.x - (10.0f + 1.5f * kShear)) < 1e-5f && std::fabs(p.z - (-4.0f + 1.5f * kShear)) < 1e-5f && p.y == 1.5f, "drawn at %.3f %.3f %.3f", p.x, p.y, p.z);
    float gx, gz;
    GroundToGrid(p, &gx, &gz);
    EXPECT(std::fabs(gx - 10.0f) < 1e-5f && std::fabs(gz + 4.0f) < 1e-5f, "back: %.4f %.4f", gx, gz);
    // the touch's own drawing of a ground place agrees (melee_bodies.h RenderGround)
    const stroke::Vec q = meleetouch::RenderGround({10.0f, 0.0f, -4.0f}, 1.5f);
    EXPECT(std::fabs(q.x - p.x) < 1e-5f && std::fabs(q.z - p.z) < 1e-5f, "RenderGround %.3f %.3f", q.x, q.z);
}

void TestMatch() {
    std::printf("models matched to their units\n");
    const float gx[3] = {4.0f, 6.0f, 6.4f}, gz[3] = {2.0f, 2.0f, -1.0f};
    // the model of the unit at (6, 2), standing on ground 1.5 m below: drawn 1.84 m off on each axis
    float dist = -1;
    const int m = MatchUnit(GridToGround(6.0f, 2.0f, -1.5f), gx, gz, 3, 0.5f, &dist);
    EXPECT(m == 1 && dist < 1e-4f, "matched %d at %.3f", m, dist);
    // the same model taken as if on flat ground (no shear) is 2.6 m from its unit - nearer the wrong one or none
    const V3 drawn = GridToGround(6.0f, 2.0f, -1.5f);
    const int flat = MatchUnit({drawn.x, 0.0f, drawn.z}, gx, gz, 3, 0.5f);
    EXPECT(flat != 1, "without the shear it is not matched to its unit: %d", flat);
    // nothing within the tolerance: none
    EXPECT(MatchUnit(GridToGround(20.0f, 20.0f, 0.0f), gx, gz, 3, 0.5f) == -1, "none near");
}

// A monster standing on its cell, its ground h high: a capsule as drawn (upright, from its drawn feet), its feet a ground sample.
void AddMonster(Scene& sc, uint32_t id, float gx, float gz, float h, float radius = 0.35f, float height = 1.6f) {
    Monster& m = sc.m[sc.nm];
    m.id = id;
    m.gx = gx;
    m.gz = gz;
    m.placed = true;
    m.feetY = h;
    const V3 feet = GridToGround(gx, gz, h);
    Capsule& c = sc.c[sc.nc++];
    c.a = feet + V3{0, radius, 0};
    c.b = feet + V3{0, height - radius, 0};
    c.r = radius;
    c.monster = sc.nm++;
    c.limb = false;
    sc.s[sc.ns++] = {feet.x, feet.z, h};
}

// The hero on his cell (hx, hz), ground h0; his feet a sample too. The eyes 1.6 m over his drawn feet.
Scene Hero(float hx, float hz, float h0) {
    Scene sc;
    sc.hero = true;
    sc.heroGx = hx;
    sc.heroGz = hz;
    sc.heroY = h0;
    const V3 feet = GridToGround(hx, hz, h0);
    sc.s[sc.ns++] = {feet.x, feet.z, h0};
    return sc;
}
V3 Eyes(const Scene& sc) { return GridToGround(sc.heroGx, sc.heroGz, sc.heroY) + V3{0, 1.6f, 0}; }
// the bow hand: in front of the eyes, a little right and below
V3 Hand(const Scene& sc) { return Eyes(sc) + V3{0.25f, -0.3f, 0.1f}; }

void Report(const char* name, const Scene& sc, const Result& r, float targetGx, float targetGz) {
    if (!g_verbose) return;
    std::printf("    %s: %s (%s) at t %.2f m, drawn %.2f %.2f %.2f, cell %.2f %.2f; heading %.1f deg, the line's own %.1f (turned %.1f); "
                "the old shot misses the target's cell by %.2f m, this one by %.2f m\n",
                name, HitName(r.hit), r.why, r.t, r.at.x, r.at.y, r.at.z, r.gx, r.gz, r.theta * kDeg, r.lineTheta * kDeg, TurnDeg(r),
                MissBy(sc, r.lineTheta, targetGx, targetGz), MissBy(sc, r.theta, targetGx, targetGz));
}

void TestStairs() {
    std::printf("down and up the stairs\n");
    const Params p;
    // the hero on his cell (0, 0) at ground 0; a monster 8 m out and 2 m across on a floor 1.5 m below
    for (float h : {-1.5f, -3.0f, 1.5f, 2.5f}) {
        Scene sc = Hero(0, 0, 0);
        AddMonster(sc, 7, 8.0f, 2.0f, h);
        const V3 chest = GridToGround(8.0f, 2.0f, h) + V3{0, 1.0f, 0};   // what the player aims at: the monster as drawn
        const V3 o = Hand(sc);
        const Result r = Decide(sc, o, chest - o, p);
        char name[64];
        snprintf(name, sizeof name, "a monster %+.1f m", h);
        Report(name, sc, r, 8.0f, 2.0f);
        EXPECT(r.hit == Hit::Monster && r.id == 7 && r.turned, "%s: the line meets the monster: %s, id %u", name, HitName(r.hit), r.id);
        EXPECT(std::fabs(r.gx - 8.0f) < 1e-4f && std::fabs(r.gz - 2.0f) < 1e-4f, "%s: the target is its cell: %.2f %.2f", name, r.gx, r.gz);
        EXPECT(MissBy(sc, r.theta, 8.0f, 2.0f) < 1e-3f, "%s: the shot goes through its cell", name);
        // and the old shot - the line's heading on the ground - passes the monster by more than its body: the bug the players saw
        EXPECT(MissBy(sc, r.lineTheta, 8.0f, 2.0f) > 0.6f, "%s: the old heading missed it by %.2f m", name, MissBy(sc, r.lineTheta, 8.0f, 2.0f));
    }
}

void TestFlat() {
    std::printf("flat ground\n");
    const Params p;
    // the same monster on the hero's own ground: the line's heading was nearly right (only the hand's place beside the eyes moves it); the shot
    // goes through the cell either way
    Scene sc = Hero(3, -2, 0.4f);
    AddMonster(sc, 9, 11.0f, 0.0f, 0.4f);
    const V3 o = Hand(sc), chest = GridToGround(11.0f, 0.0f, 0.4f) + V3{0, 1.0f, 0};
    const Result r = Decide(sc, o, chest - o, p);
    Report("flat", sc, r, 11.0f, 0.0f);
    EXPECT(r.hit == Hit::Monster && r.id == 9, "the monster: %s %u", HitName(r.hit), r.id);
    EXPECT(MissBy(sc, r.theta, 11.0f, 0.0f) < 1e-3f, "through its cell");
    EXPECT(std::fabs(TurnDeg(r)) < 4.0f, "a small turn on flat ground: %.2f deg", TurnDeg(r));
    // from the eyes themselves on flat ground the line's heading and the cell's agree exactly
    const V3 e = Eyes(sc);
    const Result re = Decide(sc, e, chest - e, p);
    EXPECT(re.hit == Hit::Monster && std::fabs(TurnDeg(re)) < 0.05f, "from the eyes: turned %.3f deg", TurnDeg(re));
}

void TestNearestFirst() {
    std::printf("the first body on the line\n");
    const Params p;
    Scene sc = Hero(0, 0, 0);
    AddMonster(sc, 1, 12.0f, 0.0f, 0.0f);
    AddMonster(sc, 2, 6.0f, 0.0f, 0.0f, 0.3f, 1.0f);   // in front of it on the same line, a small one (a fallen)
    const V3 o = Eyes(sc);
    const Result r = Decide(sc, o, V3{1, -0.15f, 0}, p);
    EXPECT(r.hit == Hit::Monster && r.id == 2, "the nearer one: %u", r.id);
    // a line over the near one's head, down onto the far one: the far one
    const V3 far = GridToGround(12.0f, 0.0f, 0.0f) + V3{0, 0.8f, 0};
    const Result r2 = Decide(sc, o, far - o, p);
    EXPECT(r2.hit == Hit::Monster && r2.id == 1, "over the near one's head: %u", r2.id);
}

void TestGround() {
    std::printf("the ground, nothing on the line\n");
    const Params p;
    // a player stands on the floor 1.5 m below (a sample of that floor), no monster: the line down onto that floor near him lands there - the cell
    // under the point is where the shot goes, which the old heading missed
    Scene sc = Hero(0, 0, 0);
    const float h = -1.5f;
    const V3 him = GridToGround(7.0f, 3.0f, h);
    sc.s[sc.ns++] = {him.x, him.z, h};
    const V3 spot = GridToGround(7.5f, 2.0f, h);   // a point on that floor near him, as drawn
    const V3 o = Hand(sc);
    const Result r = Decide(sc, o, spot - o, p);
    Report("the lower floor", sc, r, 7.5f, 2.0f);
    EXPECT(r.hit == Hit::Floor && r.turned, "the ground: %s", HitName(r.hit));
    EXPECT(std::fabs(r.groundY - h) < 1e-4f, "the lower floor's height: %.3f", r.groundY);
    EXPECT(std::fabs(r.gx - 7.5f) < 0.05f && std::fabs(r.gz - 2.0f) < 0.05f, "the cell under the point: %.3f %.3f", r.gx, r.gz);
    EXPECT(MissBy(sc, r.lineTheta, 7.5f, 2.0f) > 0.6f, "the old heading missed it by %.2f m", MissBy(sc, r.lineTheta, 7.5f, 2.0f));
    // nothing known there: the ground is the hero's own, flat - from the eyes the heading is the line's own (no change where nothing is known)
    Scene bare = Hero(0, 0, 0);
    const V3 e = Eyes(bare);
    const Result rb = Decide(bare, e, V3{1.0f, -0.2f, 0.4f}, p);
    EXPECT(rb.hit == Hit::Floor && std::fabs(rb.groundY) < 1e-5f, "flat ground met: %s %.3f", HitName(rb.hit), rb.groundY);
    EXPECT(std::fabs(TurnDeg(rb)) < 0.05f, "the line's own heading: turned %.3f deg", TurnDeg(rb));
    // a line that goes into the hero's own floor long before a monster far out (it would pass under its feet): the ground
    Scene ledge = Hero(0, 0, 0);
    AddMonster(ledge, 5, 20.0f, 0.0f, 0.0f);
    const Result rl = Decide(ledge, Eyes(ledge), V3{1.0f, -0.25f, 0.0f}, p);
    EXPECT(rl.hit == Hit::Floor && rl.t < 7.0f, "the ground before the far monster: %s at %.2f", HitName(rl.hit), rl.t);
    // the start below a higher floor's sample (the foot of the stairs, a monster up there near): no crossing counted until the line is above it
    Scene foot = Hero(0, 0, 0);
    const V3 up = GridToGround(1.0f, 0.0f, 2.5f);
    foot.s[foot.ns++] = {up.x, up.z, 2.5f};
    const Result rf = Decide(foot, Eyes(foot), V3{1.0f, 0.02f, 0.0f}, p);
    EXPECT(rf.hit == Hit::None || rf.t > 1.0f, "no ground met at the start: %s at %.2f", HitName(rf.hit), rf.t);
}

void TestNothing() {
    std::printf("into the air, and the hero's own cell\n");
    const Params p;
    Scene sc = Hero(0, 0, 0);
    AddMonster(sc, 3, 8.0f, 0.0f, 0.0f);
    const Result r = Decide(sc, Eyes(sc), V3{1.0f, 0.6f, 0.2f}, p);
    EXPECT(r.hit == Hit::None && !r.turned && r.theta == r.lineTheta, "nothing: %s, turned %d", HitName(r.hit), r.turned);
    // straight down at the hero's own feet: the cell is his own - no heading from it, the line's own stays
    const Result rd = Decide(sc, Eyes(sc), V3{0.01f, -1.0f, 0.0f}, p);
    EXPECT(rd.hit == Hit::Floor && !rd.turned, "at the feet: %s, turned %d (%s)", HitName(rd.hit), rd.turned, rd.why);
    // no hero: nothing decided
    Scene none = sc;
    none.hero = false;
    EXPECT(!Decide(none, Eyes(sc), V3{1, -0.1f, 0}, p).turned, "no hero, no turn");
    // a line that is no line
    EXPECT(Decide(sc, Eyes(sc), V3{0, 0, 0}, p).hit == Hit::None, "no line");
    EXPECT(Decide(sc, V3{NAN, 0, 0}, V3{1, 0, 0}, p).hit == Hit::None, "a NaN start");
}

void TestCost() {
    std::printf("cost\n");
    // the fullest scene: every monster, capsule and sample used, a line that meets nothing (the longest walk)
    Scene sc = Hero(0, 0, 0);
    for (int i = 0; sc.nm < kMaxMonsters && sc.nc + 4 <= kMaxCapsules && sc.ns < kMaxSamples; ++i) {
        AddMonster(sc, 100 + i, -10.0f - (i % 8), -10.0f - (i / 8), -0.5f * (i % 3));
        for (int k = 0; k < 3 && sc.nc < kMaxCapsules; ++k) {
            sc.c[sc.nc] = sc.c[sc.nc - 1];
            ++sc.nc;
        }
    }
    const Params p;
    const auto t0 = std::chrono::steady_clock::now();
    float sink = 0;
    constexpr int kRuns = 200;
    for (int i = 0; i < kRuns; ++i) sink += Decide(sc, Eyes(sc), V3{1.0f, -0.01f, 0.3f}, p).t;
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / kRuns;
    std::printf("    a shot decided in %.1f us (%d monsters, %d capsules, %d samples)%s\n", us, sc.nm, sc.nc, sc.ns, sink > 1e9f ? "" : "");
    EXPECT(us < 2000.0, "under 2 ms: %.1f us", us);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "-v")) g_verbose = true;
    TestRayCapsule();
    TestShear();
    TestMatch();
    TestStairs();
    TestFlat();
    TestNearestFirst();
    TestGround();
    TestNothing();
    TestCost();
    if (g_fails) {
        std::printf("bow_pitch_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("bow_pitch_test: all passed\n");
    return 0;
}
