// [body] attack_lean, vr/attack_lean.h: the chest and the spine chain from a rig, the aimed point from the crosshair's ray (first person, from
// behind, at a small monster, at a tall one, up), the bend's sign and size, the turn's matrix, the bend on a pose, and the ease through an attack.
#include "attack_lean.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
static float Deg(float r) { return r * 180.0f / attacklean::kPi; }

using namespace attacklean;

// a rig: 0 root, 1 pelvis, 2 spine_01, 3 spine_02, 4 spine_03, 5 neck, 6 head, 7 R clavicle, 8 R upper arm, 9 R forearm, 10 R hand,
// 11 R weapon attach, 12 L clavicle, 13 L upper arm, 14 L hand, 15 L thigh
static const int kParent[16] = {-1, 0, 1, 2, 3, 4, 5, 4, 7, 8, 9, 10, 4, 12, 13, 1};

static void TestRig() {
    Check(Chest(16, kParent, 2, 8, 13) == 4, "the chest is the bone both upper arms hang under (spine_03)");
    const std::vector<int> chain = Chain(16, kParent, 2, 4);
    Check(chain.size() == 3 && chain[0] == 2 && chain[1] == 3 && chain[2] == 4, "the chain: spine_01, spine_02, spine_03 - spine_01 first");
    Check(Chest(16, kParent, 2, 8, 15) == -1, "an 'arm' under the pelvis (a thigh): no chest above spine_01");
    Check(Chest(16, kParent, 2, -1, 13) == -1 && Chest(16, kParent, -1, 8, 13) == -1, "missing bones: none");
    Check(Chain(16, kParent, 2, 15).empty(), "a 'chest' not above spine_01: no chain");
    const int loop[3] = {1, 0, -1};
    Check(Chain(3, loop, 2, 0).empty() && !Under(loop, 3, 0, 2), "a looping parent list ends");
    Check(Under(kParent, 16, 11, 2) && Under(kParent, 16, 6, 4) && !Under(kParent, 16, 15, 2) && !Under(kParent, 16, 1, 2),
          "under spine_01: the weapon, the head; not the thigh or the pelvis");
}

static void TestModes() {
    const int swing[] = {7, 8, 11, 12, 13, 14, 15, 16, 18}, not_[] = {-1, 0, 1, 2, 3, 4, 5, 6, 9, 10, 17, 19};
    for (int m : swing) Check(IsSwingMode(m), "attacks, throw, kick, skill and sequence modes lean");
    for (int m : not_) Check(!IsSwingMode(m), "standing, walking, a hit, a block, a cast, death, a knockback: no lean");
}

static void TestAim() {
    // units: 1 = a metre. The hero faces +z, the chest at 1.35 up, the feet's floor at 0, the reach his eye height 1.65
    const V3 chest{0.0f, 1.35f, 0.0f};
    const float floorY = 0.0f, reach = 1.65f;
    Aim a;
    // first person, the eye 1.65 up and a little ahead, looking at a small monster's middle (0.35 up) 1.4 m ahead
    {
        const V3 eye{0.0f, 1.65f, 0.1f}, at{0.0f, 0.35f, 1.4f};
        Check(AimAt(eye, {at.x - eye.x, at.y - eye.y, at.z - eye.z}, chest, floorY, reach, &a), "a ray");
        const float truth = std::atan2(at.y - chest.y, at.z - chest.z);
        Check(a.by == Met::Reach || a.by == Met::Floor, "looking down at a small monster close by: met at the reach or the floor");
        Check(Near(Deg(a.elevation), Deg(truth), 5.0f), "... within 5 deg of the monster's middle");
        Check(Deg(a.elevation) < -30.0f, "... well down");
        std::printf("first person, small monster at 1.4 m: elevation %.1f deg (its middle %.1f), pitch %.1f, %s %.2f ahead\n", Deg(a.elevation), Deg(truth),
                    Deg(a.pitch), MetName(a.by), a.ahead);
    }
    // first person, a man-sized monster's chest (1.35 up) 1.6 m ahead: nearly level
    {
        const V3 eye{0.0f, 1.65f, 0.1f}, at{0.0f, 1.35f, 1.6f};
        AimAt(eye, {at.x - eye.x, at.y - eye.y, at.z - eye.z}, chest, floorY, reach, &a);
        Check(a.by == Met::Reach && std::fabs(Deg(a.elevation)) < 3.0f, "first person at a tall one's chest: met at the reach, within 3 deg of level");
    }
    // from behind: the camera 2.5 m back and 1.3 m over the head, the crosshair on a small monster's middle 1.5 m in front
    {
        const V3 eye{0.0f, 2.95f, -2.5f}, at{0.0f, 0.4f, 1.5f};
        AimAt(eye, {at.x - eye.x, at.y - eye.y, at.z - eye.z}, chest, floorY, reach, &a);
        const float truth = std::atan2(at.y - chest.y, at.z - chest.z);
        Check(a.by == Met::Reach && Near(Deg(a.elevation), Deg(truth), 3.0f), "from behind at a small monster: the reach, within 3 deg of its middle");
        Check(Deg(a.pitch) < -30.0f, "(the camera's own pitch there)");
        std::printf("from behind, small monster at 1.5 m: elevation %.1f deg (its middle %.1f), the camera's pitch %.1f\n", Deg(a.elevation), Deg(truth), Deg(a.pitch));
        // the same monster's place, a tall one: its chest 1.35 up -> about level, though the camera looks down
        const V3 tall{0.0f, 1.35f, 1.5f};
        AimAt(eye, {tall.x - eye.x, tall.y - eye.y, tall.z - eye.z}, chest, floorY, reach, &a);
        Check(std::fabs(Deg(a.elevation)) < 4.0f, "from behind at a tall one's chest: about level - the camera's down tilt does not lean him");
    }
    // looking up at something above the head (a flyer 2.6 up, 1.5 ahead) from first person
    {
        const V3 eye{0.0f, 1.65f, 0.1f}, at{0.0f, 2.6f, 1.5f};
        AimAt(eye, {at.x - eye.x, at.y - eye.y, at.z - eye.z}, chest, floorY, reach, &a);
        Check(a.elevation > 0.5f, "looking up: the point is well above the chest");
    }
    // straight up: no floor, no reach crossing -> the ray's own pitch
    {
        AimAt({0.0f, 1.65f, 0.0f}, {0.0f, 1.0f, 0.0f}, chest, floorY, reach, &a);
        Check(a.by == Met::Ray && Near(Deg(a.elevation), 90.0f, 0.01f), "straight up: by the ray's own pitch");
    }
    // straight down from first person: the floor under him
    {
        AimAt({0.0f, 1.65f, 0.3f}, {0.0f, -1.0f, 0.0f}, chest, floorY, reach, &a);
        Check(a.by == Met::Floor && Deg(a.elevation) < -70.0f, "straight down: the floor at his feet, steeply down");
    }
    Check(!AimAt({0, 1, 0}, {0, 0, 0}, chest, floorY, reach, &a), "no forward: no aim");
    Check(!AimAt({0, NAN, 0}, {0, 0, 1}, chest, floorY, reach, &a), "a NaN: no aim");
    // turned: the hero faces +x, the same small-monster look turned the same way gives the same elevation
    {
        const V3 eye{0.1f, 1.65f, 0.0f}, at{1.4f, 0.35f, 0.0f};
        Aim b;
        AimAt(eye, {at.x - eye.x, at.y - eye.y, at.z - eye.z}, chest, floorY, reach, &b);
        AimAt({0.0f, 1.65f, 0.1f}, {0.0f, 0.35f - 1.65f, 1.3f}, chest, floorY, reach, &a);
        Check(Near(a.elevation, b.elevation, 1e-4f), "the facing does not change the elevation");
    }
}

static void TestLean() {
    Check(Near(Deg(LeanRad(-0.5f, 45.0f, 1.0f)), Deg(0.5f), 1e-3f), "a point below: bend forward by its elevation");
    Check(Near(Deg(LeanRad(-1.2f, 45.0f, 1.0f)), 45.0f, 1e-3f), "capped at the most lean");
    Check(Near(Deg(LeanRad(1.2f, 30.0f, 1.0f)), -30.0f, 1e-3f), "a point above: bend back, capped");
    Check(Near(LeanRad(-0.5f, 45.0f, 0.5f), 0.25f, 1e-5f), "times the ease's weight");
    Check(LeanRad(-0.5f, 0.0f, 1.0f) == 0.0f && LeanRad(-0.5f, 45.0f, 0.0f) == 0.0f, "0 deg or weight 0: nothing");
    // the axis and the turn: a positive turn about Axis(facing) tips up toward the facing
    for (const V3 f : {V3{0, 0, 1}, V3{1, 0, 0}, V3{-0.6f, 0, 0.8f}}) {
        float t[16];
        TurnAbout(Axis(f), 0.5f, {0, 0, 0}, t);
        const float up[3] = {t[4], t[5], t[6]};   // (0,1,0) * T: row 1
        Check(Near(up[1], std::cos(0.5f), 1e-5f) && Near(up[0] * f.x + up[2] * f.z, std::sin(0.5f), 1e-5f), "+ turn: up tips toward the facing");
    }
    {   // about a pivot: the pivot stays, a point a unit above it goes forward and down
        float t[16];
        TurnAbout(Axis({0, 0, 1}), kPi / 2, {1, 2, 3}, t);
        const float p[3] = {1 * t[0] + 2 * t[4] + 3 * t[8] + t[12], 1 * t[1] + 2 * t[5] + 3 * t[9] + t[13], 1 * t[2] + 2 * t[6] + 3 * t[10] + t[14]};
        const float q[3] = {1 * t[0] + 3 * t[4] + 3 * t[8] + t[12], 1 * t[1] + 3 * t[5] + 3 * t[9] + t[13], 1 * t[2] + 3 * t[6] + 3 * t[10] + t[14]};
        Check(Near(p[0], 1, 1e-5f) && Near(p[1], 2, 1e-5f) && Near(p[2], 3, 1e-5f), "the pivot stays");
        Check(Near(q[0], 1, 1e-5f) && Near(q[1], 2, 1e-5f) && Near(q[2], 4, 1e-5f), "a unit above it, turned 90 deg: a unit ahead");
    }
}

static void TestBend() {
    // the rig standing: each bone a translation only, spine bones 0.2 apart from 1.0 up, the arm out to the right, the weapon at the hand
    const float at[16][3] = {{0, 0, 0}, {0, 1.0f, 0}, {0, 1.1f, 0}, {0, 1.25f, 0}, {0, 1.4f, 0}, {0, 1.55f, 0}, {0, 1.65f, 0}, {-0.1f, 1.45f, 0},
                             {-0.2f, 1.45f, 0}, {-0.45f, 1.45f, 0}, {-0.7f, 1.45f, 0}, {-0.75f, 1.45f, 0.05f}, {0.1f, 1.45f, 0}, {0.2f, 1.45f, 0},
                             {0.7f, 1.45f, 0}, {0.1f, 0.9f, 0}};
    float w[16][16];
    for (int i = 0; i < 16; ++i) {
        for (int k = 0; k < 16; ++k) w[i][k] = (k % 5 == 0) ? 1.0f : 0.0f;
        w[i][12] = at[i][0]; w[i][13] = at[i][1]; w[i][14] = at[i][2];
    }
    float before[16][16];
    std::copy(&w[0][0], &w[0][0] + 256, &before[0][0]);
    const std::vector<int> chain = Chain(16, kParent, 2, Chest(16, kParent, 2, 8, 13));
    const float rad = 30.0f * kPi / 180.0f;
    Bend(w, 16, kParent, chain, Axis({0, 0, 1}), rad);
    for (int b : {0, 1, 15})
        for (int k = 0; k < 16; ++k) Check(w[b][k] == before[b][k], "the root, the pelvis and the legs stay the game's");
    Check(Near(w[2][12], before[2][12], 1e-6f) && Near(w[2][13], before[2][13], 1e-6f) && Near(w[2][14], before[2][14], 1e-6f), "spine_01 stays in its place");
    // the head: its height over spine_01 shrinks and it moves forward (+z); the turn of its frame is the whole lean (its up row tipped toward +z)
    Check(w[6][14] > 0.2f && w[6][13] < before[6][13], "the head goes forward and down");
    Check(Near(std::atan2(w[6][6], w[6][5]), rad, 1e-4f), "the head's frame is turned by the whole lean (three shares)");
    Check(Near(std::atan2(w[2][6], w[2][5]), rad / 3, 1e-4f), "spine_01's frame by a third");
    // the right hand and the weapon ride on the chest: the distance hand-weapon kept, the weapon's frame turned by the whole lean
    const float d0 = std::hypot(std::hypot(before[11][12] - before[10][12], before[11][13] - before[10][13]), before[11][14] - before[10][14]);
    const float d1 = std::hypot(std::hypot(w[11][12] - w[10][12], w[11][13] - w[10][13]), w[11][14] - w[10][14]);
    Check(Near(d0, d1, 1e-5f) && Near(std::atan2(w[11][6], w[11][5]), rad, 1e-4f), "the weapon stays in the hand and tilts with the lean");
    // every bone keeps its length from its parent (the turns are rigid about each joint)
    for (int i = 1; i < 16; ++i) {
        const int p = kParent[i];
        const float a0 = std::hypot(std::hypot(before[i][12] - before[p][12], before[i][13] - before[p][13]), before[i][14] - before[p][14]);
        const float a1 = std::hypot(std::hypot(w[i][12] - w[p][12], w[i][13] - w[p][13]), w[i][14] - w[p][14]);
        Check(Near(a0, a1, 1e-4f), "rigid: every bone keeps its length from its parent");
    }
    float none[16][16];
    std::copy(&before[0][0], &before[0][0] + 256, &none[0][0]);
    Bend(none, 16, kParent, chain, Axis({0, 0, 1}), 0.0f);
    Bend(none, 16, kParent, {}, Axis({0, 0, 1}), rad);
    Check(std::equal(&none[0][0], &none[0][0] + 256, &before[0][0]), "no lean, or no chain: nothing moves");
}

static void TestEase() {
    Ease e;
    Check(e.Step(false, 16.0f) == 0.0f, "standing: 0");
    float w = 0.0f;
    for (int i = 0; i < 4; ++i) w = e.Step(true, 16.0f);
    Check(w > 0.0f && w < 1.0f, "an attack's first poses: easing in");
    for (int i = 0; i < 10; ++i) w = e.Step(true, 16.0f);
    Check(w == 1.0f, "after kEaseInMs: the whole lean");
    w = e.Step(false, 16.0f);
    Check(w < 1.0f && w > 0.9f, "the attack over: easing out");
    for (int i = 0; i < 12; ++i) w = e.Step(false, 16.0f);
    Check(w == 0.0f, "after kEaseOutMs: the game's own pose again");
    Ease s;
    Check(s.Step(true, 5000.0f) < 1.0f, "a stall counts 100 ms at most");
}

int main() {
    TestRig();
    TestModes();
    TestAim();
    TestLean();
    TestBend();
    TestEase();
    std::printf("attack_lean_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
