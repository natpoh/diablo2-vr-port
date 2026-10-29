// The string hand's fingers as the game's own bow-draw animation holds them (vr/bow_hook.h): the table, the rotation matrices in the game's
// row-vector convention, the blend between two poses of a bone, and the index finger of the Amazon's own skeleton posed with the table - the
// fingertip lands where the game's draw animation puts it.
//
//   bow_hook_test

#include "bow_hook.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace bowhook;

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
float Len3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

void TestNames() {
    std::printf("names\n");
    EXPECT(KindOf("R_ind_finger_01_bind_jnt") == kIndex && KindOf("R_mid_finger_01_bind_jnt") == kMiddle && KindOf("R_rng_finger_01_bind_jnt") == kRing, "the fingers");
    EXPECT(KindOf("R_pnk_finger_01_bind_jnt") == kPinky && KindOf("R_tmb_finger_01_bind_jnt") == kThumb, "the little finger and the thumb");
    EXPECT(KindOf("L_IND_FINGER_01_BIND_JNT") == kIndex, "any case, either hand");
    EXPECT(KindOf("R_hand_bind_jnt") == kNone && KindOf("R_weapon_attach") == kNone && KindOf("hand_pinky_01_L_JNT") == kNone && KindOf("") == kNone, "not a finger of the Amazon's rig");
    EXPECT(Joints(kIndex) == 4 && Joints(kMiddle) == 4 && Joints(kRing) == 4 && Joints(kPinky) == 4 && Joints(kThumb) == 3 && Joints(kNone) == 0 && Joints(9) == 0, "the joints of each");
}

void TestTable() {
    std::printf("the table\n");
    for (int k = 0; k < kKinds; ++k)
        for (int j = 0; j < Joints(k); ++j) {
            const Q& q = Rel(k, j);
            EXPECT(Near(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w), 1.0f, 2e-5f) && q.w >= 0.0f, "finger %d joint %d: a unit rotation, the short way round", k, j);
        }
    // the hook, not a fist: the knuckles stay open (the first joint of the four fingers), the last two joints bend, the thumb is turned in
    for (int k : {kIndex, kMiddle, kRing, kPinky}) {
        const Q& knuckle = Rel(k, 0);
        const float angle = 2.0f * std::acos(std::min(knuckle.w, 1.0f)) * 57.29578f;
        EXPECT(angle < 8.0f, "finger %d's knuckle stays open (%.1f deg)", k, angle);
    }
    float tips = 0.0f;
    for (int k : {kIndex, kMiddle, kRing, kPinky}) tips += 2.0f * std::acos(std::min(Rel(k, 3).w, 1.0f)) * 57.29578f;
    EXPECT(tips / 4.0f > 25.0f, "the finger tips are curled: %.0f deg on average", tips / 4.0f);
    {   // the thumb base: turned in some 57 degrees (read right: the first reading, with the wrong quaternion layout for the thumb's first quarter second, said
        // 145 and put the thumb out of the hand - TestThumb places it)
        const float thumb = 2.0f * std::acos(std::min(Rel(kThumb, 0).w, 1.0f)) * 57.29578f;
        EXPECT(thumb > 45.0f && thumb < 70.0f, "the thumb is turned in at its base by %.1f deg", thumb);
    }
    // out of range: no turn
    EXPECT(Rel(-1, 0).w == 1.0f && Rel(0, 9).w == 1.0f && Rel(kThumb, 3).w == 1.0f, "outside the table: none");
}

void TestMatrix() {
    std::printf("matrices\n");
    // a turn about z by 60 deg: the row-vector matrix has its first row (cos, sin, 0) - the transpose of the column-vector one
    const float t = 60.0f * 3.14159265f / 180.0f;
    float m[16];
    RowMatrix({0.0f, 0.0f, std::sin(t / 2), std::cos(t / 2)}, m);
    EXPECT(Near(m[0], std::cos(t)) && Near(m[1], std::sin(t)) && Near(m[4], -std::sin(t)) && Near(m[5], std::cos(t)) && Near(m[10], 1.0f) && m[15] == 1.0f && m[12] == 0.0f, "about z: the row-vector layout");
    // the point (1 0 0) times it is turned counter-clockwise about +z
    EXPECT(Near(1.0f * m[0], std::cos(t)) && Near(1.0f * m[1], std::sin(t)), "(1 0 0) * R is (cos, sin, 0)");
    // identity, and a quaternion that is not a unit is made one
    RowMatrix({0, 0, 0, 1}, m);
    EXPECT(Near(m[0], 1) && Near(m[5], 1) && Near(m[10], 1) && Near(m[1], 0) && Near(m[4], 0), "identity");
    RowMatrix({0, 0, 0, 0}, m);
    EXPECT(Near(m[0], 1) && Near(m[5], 1) && Near(m[10], 1), "no rotation: identity");
    // every table entry is a proper rotation, and its quaternion comes back out of the matrix
    for (int k = 0; k < kKinds; ++k)
        for (int j = 0; j < Joints(k); ++j) {
            float r[16];
            RowMatrix(Rel(k, j), r);
            const float a[3] = {r[0], r[1], r[2]}, b[3] = {r[4], r[5], r[6]}, c[3] = {r[8], r[9], r[10]};
            EXPECT(Near(Len3(a), 1.0f) && Near(Len3(b), 1.0f) && Near(Len3(c), 1.0f) && Near(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], 0.0f) &&
                       Near(a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]), 1.0f),
                   "finger %d joint %d: orthonormal, determinant 1", k, j);
            const Q back = QuatOf(r);
            const Q q = Rel(k, j);
            const float d = std::fabs(back.x * q.x + back.y * q.y + back.z * q.z + back.w * q.w);
            EXPECT(Near(d, 1.0f, 1e-4f), "finger %d joint %d: the quaternion comes back out of the matrix (%.6f)", k, j, d);
        }
    // random rotations: round trip, including the branches of the conversion
    unsigned seed = 7;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / (float)(1u << 24) * 2.0f - 1.0f; };
    for (int n = 0; n < 2000; ++n) {
        const Q q = Normalised({rnd(), rnd(), rnd(), rnd()});
        float r[16];
        RowMatrix(q, r);
        const Q back = QuatOf(r);
        EXPECT(Near(std::fabs(back.x * q.x + back.y * q.y + back.z * q.z + back.w * q.w), 1.0f, 2e-4f), "random %d: round trip", n);
    }
    // a scaled bone: the rotation comes out, the scale does not matter
    float s[16];
    RowMatrix(Rel(kMiddle, 2), s);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) s[i * 4 + j] *= 1.7f;
    EXPECT(Near(std::fabs(QuatOf(s).x * Rel(kMiddle, 2).x + QuatOf(s).y * Rel(kMiddle, 2).y + QuatOf(s).z * Rel(kMiddle, 2).z + QuatOf(s).w * Rel(kMiddle, 2).w), 1.0f, 1e-4f), "a scaled bone");
}

void TestBlend() {
    std::printf("blend\n");
    float a[16], b[16], o[16];
    RowMatrix(Rel(kIndex, 2), a);
    RowMatrix(Rel(kMiddle, 3), b);
    a[12] = 1.0f; a[13] = 2.0f; a[14] = 3.0f;
    b[12] = 3.0f; b[13] = 0.0f; b[14] = -1.0f;
    BlendBone(a, b, 0.0f, o);
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && Near(o[i], a[i], 2e-4f);
    EXPECT(same, "t = 0 is the first");
    BlendBone(a, b, 1.0f, o);
    same = true;
    for (int i = 0; i < 16; ++i) same = same && Near(o[i], b[i], 2e-4f);
    EXPECT(same, "t = 1 is the second");
    BlendBone(a, b, 0.5f, o);
    EXPECT(Near(o[12], 2.0f) && Near(o[13], 1.0f) && Near(o[14], 1.0f) && o[15] == 1.0f && o[3] == 0.0f && o[7] == 0.0f && o[11] == 0.0f, "half way: the position in a straight line");
    const float ax[3] = {o[0], o[1], o[2]}, ay[3] = {o[4], o[5], o[6]}, az[3] = {o[8], o[9], o[10]};
    EXPECT(Near(Len3(ax), 1.0f) && Near(Len3(ay), 1.0f) && Near(Len3(az), 1.0f) && Near(ax[0] * ay[0] + ax[1] * ay[1] + ax[2] * ay[2], 0.0f), "half way: still a rotation");
    // the angle from a grows evenly with t
    const Q qa = QuatOf(a), qb = QuatOf(b);
    float last = -1.0f;
    bool monotone = true;
    for (float t = 0.0f; t <= 1.0001f; t += 0.1f) {
        BlendBone(a, b, t, o);
        const Q q = QuatOf(o);
        const float ang = 2.0f * std::acos(std::min(std::fabs(q.x * qa.x + q.y * qa.y + q.z * qa.z + q.w * qa.w), 1.0f));
        monotone = monotone && ang >= last - 1e-3f;
        last = ang;
    }
    EXPECT(monotone, "the turn away from the first only grows");
    // scales: lengths blend too; NaN is no blend
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) b[i * 4 + j] *= 2.0f;
    BlendBone(a, b, 0.5f, o);
    const float lx[3] = {o[0], o[1], o[2]};
    EXPECT(Near(Len3(lx), 1.5f, 1e-3f), "the axes' lengths in a straight line (%.3f)", Len3(lx));
    BlendBone(a, b, NAN, o);
    EXPECT(Near(o[12], a[12]) && Near(o[0], a[0], 2e-4f), "NaN is no blend");
    // opposite hemispheres of one rotation are the same rotation
    Q q = Rel(kRing, 3);
    float m1[16], m2[16];
    RowMatrix(q, m1);
    RowMatrix({-q.x, -q.y, -q.z, -q.w}, m2);
    BlendBone(m1, m2, 0.5f, o);
    same = true;
    for (int i = 0; i < 12; ++i) same = same && Near(o[i], m1[i], 2e-4f);
    EXPECT(same, "q and -q blend to themselves");
    // the slerp itself: the short way whichever hemisphere the second quaternion is given in
    const Q sa = Rel(kIndex, 2), sb = Rel(kMiddle, 3);
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const Q x = Slerp(sa, sb, t), y = Slerp(sa, {-sb.x, -sb.y, -sb.z, -sb.w}, t);
        const float d = std::fabs(x.x * y.x + x.y * y.y + x.z * y.z + x.w * y.w);
        EXPECT(Near(d, 1.0f, 1e-5f), "t %.2f: the same rotation from either hemisphere", t);
    }
    {
        const Q h = Slerp({0, 0, 0, 1}, {0, 0, 1, 0}, 0.5f);   // half way to a half turn about z is a quarter turn
        EXPECT(Near(std::fabs(h.z), 0.70710678f, 1e-5f) && Near(std::fabs(h.w), 0.70710678f, 1e-5f), "half of a half turn is a quarter turn");
        const Q s = Slerp({0, 0, 0, 1}, {0, 0, std::sin(0.1f), std::cos(0.1f)}, 0.5f);   // (the nearly equal branch)
        EXPECT(Near(s.z, std::sin(0.05f), 1e-4f), "a small turn halved");
    }
}

// The Amazon's right index finger as the skeleton has it: the chain 01 - 04 under the hand, each bone's position in its parent and its bind
// rotation (the skeleton file's own numbers). The hand bone at the origin. The fingertip's distance from it in the bind pose and in the hook.
struct Bone { float pos[3]; Q rot; };
const Bone kIndexChain[4] = {
    {{-0.13374069f, 0.006657134f, -0.08791517f}, {-0.002346463f, -0.036906771f, 0.023098996f, 0.999049067f}},
    {{-0.18494773f, 0.00000064f, 0.00000159f}, {0.0000000196f, -0.0000000163f, -0.10331638f, 0.99464858f}},
    {{-0.15040106f, 0.00000008f, 0.00000004f}, {0.00000008f, 0.00000027f, -0.21389407f, 0.97685683f}},
    {{-0.10426956f, 0.00000358f, -0.00000066f}, {0.00000011f, -0.00000016f, -0.10836685f, 0.99411100f}},
};

void Tip(bool hook, float out[3]) {
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int j = 0; j < 4; ++j) {
        float local[16];
        RowMatrix(kIndexChain[j].rot, local);
        for (int i = 0; i < 3; ++i) local[12 + i] = kIndexChain[j].pos[i];
        float used[16];
        if (hook) {
            float rel[16];
            RowMatrix(Rel(kIndex, j), rel);
            Mul4(rel, local, used);
        } else {
            std::memcpy(used, local, sizeof used);
        }
        float next[16];
        Mul4(used, world, next);
        std::memcpy(world, next, sizeof world);
    }
    out[0] = world[12]; out[1] = world[13]; out[2] = world[14];
}

void TestIndexFinger() {
    std::printf("the index finger of the Amazon's skeleton\n");
    float bind[3], hook[3];
    Tip(false, bind);
    Tip(true, hook);
    // the numbers of the offline check (the same composition done with numpy over the skeleton file): 0.571 bind, 0.507 hook, 0.099 apart
    EXPECT(Near(Len3(bind), 0.571f, 0.002f), "the bind pose's fingertip is %.4f from the hand, the skeleton says 0.571", Len3(bind));
    EXPECT(Near(Len3(hook), 0.507f, 0.002f), "the hooked fingertip is %.4f from the hand, the game's animation 0.507", Len3(hook));
    const float d[3] = {hook[0] - bind[0], hook[1] - bind[1], hook[2] - bind[2]};
    EXPECT(Near(Len3(d), 0.099f, 0.003f), "the tip moved %.4f, the game's animation 0.099", Len3(d));
    // the knuckle (bone 01) stays where it is: only the last joints take the tip toward the hand
    EXPECT(Len3(hook) < Len3(bind), "the fingertip comes toward the hand");
    // and the identity of the table (no rotation) gives back the bind pose
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int j = 0; j < 4; ++j) {
        float local[16], next[16];
        RowMatrix(kIndexChain[j].rot, local);
        for (int i = 0; i < 3; ++i) local[12 + i] = kIndexChain[j].pos[i];
        float ident[16], used[16];
        RowMatrix({0, 0, 0, 1}, ident);
        Mul4(ident, local, used);
        Mul4(used, world, next);
        std::memcpy(world, next, sizeof world);
    }
    EXPECT(Near(world[12], bind[0], 1e-5f) && Near(world[13], bind[1], 1e-5f) && Near(world[14], bind[2], 1e-5f), "no rotation: the bind pose");
}

// The other chains of the Amazon's right hand, from the same skeleton file (each bone: its position in its parent, its bind rotation).
const Bone kThumbChain[3] = {
    {{-0.10945586f, 0.05162587f, -0.05049809f}, {0.44944745f, -0.45188755f, 0.13406266f, 0.75882941f}},
    {{-0.16708925f, -0.00000606f, 0.00000788f}, {0.01299047f, -0.01649814f, -0.30988979f, 0.95054054f}},
    {{-0.13706857f, 0.00000137f, -0.00000094f}, {0.00256216f, -0.00162525f, -0.02336358f, 0.99972254f}},
};
const Bone kMiddleChain[4] = {
    {{-0.15383954f, -0.00817381f, -0.02542034f}, {-0.01765004f, 0.03167518f, -0.01822427f, 0.99917614f}},
    {{-0.16998872f, -0.00000218f, -0.00000028f}, {0.0f, -0.00000004f, -0.07672907f, 0.99705195f}},
    {{-0.15174384f, 0.00000822f, 0.00000052f}, {0.0f, -0.00000014f, -0.19763260f, 0.98027617f}},
    {{-0.11077636f, -0.00000828f, 0.00000186f}, {0.0000002f, 0.00000066f, -0.07203436f, 0.99740225f}},
};
const Bone kRingChain[4] = {
    {{-0.15303421f, -0.00065753f, 0.00875232f}, {-0.08129612f, 0.15913054f, -0.01096100f, 0.98384356f}},
    {{-0.15844718f, -0.00000014f, -0.00000062f}, {0.00000001f, 0.00000009f, -0.09920611f, 0.99506688f}},
    {{-0.14488703f, 0.00000629f, 0.00000087f}, {0.0f, -0.00000012f, -0.17071193f, 0.98532093f}},
    {{-0.08490986f, -0.00000133f, 0.00000002f}, {-0.00000001f, 0.00000003f, -0.06645271f, 0.99778956f}},
};
const Bone kPinkyChain[4] = {
    {{-0.15792371f, -0.00478520f, 0.08468590f}, {-0.09995072f, 0.19858462f, -0.07206438f, 0.97230691f}},
    {{-0.12929899f, 0.00000415f, 0.00000131f}, {0.0f, 0.00000009f, -0.01974755f, 0.99980497f}},
    {{-0.11306983f, 0.00000172f, -0.00000031f}, {0.00000002f, -0.00000026f, -0.21131364f, 0.97741830f}},
    {{-0.07960186f, -0.00000204f, -0.00000037f}, {0.00000008f, 0.00000024f, -0.10484196f, 0.99448889f}},
};

// Every bone's origin of a chain under the hand (the hand bone at the origin, its frame the world's), in the bind pose or the hook as tuned;
// `first` replaces the first joint's rotation (a table entry to compare with).
void ChainJoints(int kind, const Bone* chain, int count, bool hook, const Tune& tune, float out[][3], const Q* first = nullptr) {
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int j = 0; j < count; ++j) {
        float local[16];
        RowMatrix(chain[j].rot, local);
        for (int i = 0; i < 3; ++i) local[12 + i] = chain[j].pos[i];
        float used[16];
        if (hook) {
            float rel[16];
            RowMatrix(j == 0 && first ? *first : Tuned(kind, j, tune), rel);
            Mul4(rel, local, used);
        } else {
            std::memcpy(used, local, sizeof used);
        }
        float next[16];
        Mul4(used, world, next);
        std::memcpy(world, next, sizeof world);
        out[j][0] = world[12]; out[j][1] = world[13]; out[j][2] = world[14];
    }
}

float Dist3(const float* a, const float* b) { const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; return Len3(d); }
float AngleOf(Q q) {   // (atan2, not acos: a turn of a twentieth of a degree is 1 - 1e-7 in w)
    q = Normalised(q);
    return 2.0f * std::atan2(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), std::fabs(q.w)) * 57.29578f;
}

void TestThumb() {
    std::printf("the thumb\n");
    float bind[3][3], hook[3][3], old[3][3];
    ChainJoints(kThumb, kThumbChain, 3, false, {}, bind);
    ChainJoints(kThumb, kThumbChain, 3, true, {}, hook);
    const Q oldFirst = {0.083010f, -0.932619f, -0.181229f, 0.300812f};   // the first reading of the thumb base: 145 degrees
    ChainJoints(kThumb, kThumbChain, 3, true, {}, old, &oldFirst);
    // the offline check (the same composition with numpy over the skeleton file, the animation read with the right layout)
    const float want1[3] = {-0.2025f, 0.1242f, -0.1687f}, want2[3] = {-0.3395f, 0.1251f, -0.1743f};
    EXPECT(Dist3(hook[1], want1) < 0.003f, "the thumb's middle joint is at (%.4f %.4f %.4f)", hook[1][0], hook[1][1], hook[1][2]);
    EXPECT(Dist3(hook[2], want2) < 0.003f, "the thumb's tip is at (%.4f %.4f %.4f)", hook[2][0], hook[2][1], hook[2][2]);
    EXPECT(Dist3(hook[0], bind[0]) < 1e-5f, "the base stays where it is");
    // the thumb stays on its own side, near where the open hand has it - not folded over the palm and out of the back of the hand
    EXPECT(Dist3(hook[2], bind[2]) < 0.15f, "the tip moved %.3f from the bind pose", Dist3(hook[2], bind[2]));
    EXPECT(Dist3(old[2], bind[2]) > 0.4f, "(the first reading moved it %.3f: out of the hand)", Dist3(old[2], bind[2]));
    EXPECT(hook[2][2] < -0.1f && old[2][2] > 0.1f, "the tip is on the thumb's side of the hand (z %.3f), not the little finger's (the first reading: %.3f)", hook[2][2], old[2][2]);
    // the thumb slider: + turns it in toward the palm (+y of the hand) and the fingers (+z), - out; 0 leaves the table as it is
    {
        const Tune in20{1.0f, 20.0f}, out20{1.0f, -20.0f};
        float a[3][3], b[3][3];
        ChainJoints(kThumb, kThumbChain, 3, true, in20, a);
        ChainJoints(kThumb, kThumbChain, 3, true, out20, b);
        EXPECT(a[2][1] > hook[2][1] + 0.03f && a[2][2] > hook[2][2] + 0.02f, "+20: the tip goes toward the palm and the fingers (dy %.3f dz %.3f)", a[2][1] - hook[2][1], a[2][2] - hook[2][2]);
        EXPECT(b[2][1] < hook[2][1] - 0.03f && b[2][2] < hook[2][2] - 0.02f, "-20: the tip goes out (dy %.3f dz %.3f)", b[2][1] - hook[2][1], b[2][2] - hook[2][2]);
        EXPECT(Dist3(a[0], hook[0]) < 1e-5f, "the base is the turn's pivot");
        const Q base = Rel(kThumb, 0), tuned = Tuned(kThumb, 0, in20);
        EXPECT(Near(AngleOf(QMul({-base.x, -base.y, -base.z, base.w}, tuned)), 20.0f, 0.05f), "the extra turn is 20 degrees");
        for (int j = 1; j < 3; ++j) {
            const Q t = Tuned(kThumb, j, in20), r = Rel(kThumb, j);
            EXPECT(t.x == r.x && t.y == r.y && t.z == r.z && t.w == r.w, "thumb joint %d is as measured", j);
        }
        // 0 is the table itself; too much is held at the limit; not a number is 0
        const Q z0 = Tuned(kThumb, 0, Tune{1.0f, 0.0f}), zn = Tuned(kThumb, 0, Tune{1.0f, NAN});
        EXPECT(z0.x == base.x && z0.w == base.w && zn.x == base.x && zn.w == base.w, "0 and NaN: as measured");
        const Q big = Tuned(kThumb, 0, Tune{1.0f, 1000.0f}), cap = Tuned(kThumb, 0, Tune{1.0f, kMaxThumbDeg});
        EXPECT(Near(big.x, cap.x, 1e-6f) && Near(big.w, cap.w, 1e-6f), "held at %.0f degrees", kMaxThumbDeg);
        const Q bigN = Tuned(kThumb, 0, Tune{1.0f, -1000.0f}), capN = Tuned(kThumb, 0, Tune{1.0f, -kMaxThumbDeg});
        EXPECT(Near(bigN.x, capN.x, 1e-6f) && Near(bigN.w, capN.w, 1e-6f) && !Near(bigN.x, cap.x, 1e-3f), "held at -%.0f degrees", kMaxThumbDeg);
        // the other fingers do not take the thumb's slider
        for (int k : {kIndex, kMiddle, kRing, kPinky})
            for (int j = 0; j < 4; ++j) {
                const Q t = Tuned(k, j, in20), r = Rel(k, j);
                EXPECT(t.x == r.x && t.y == r.y && t.z == r.z && t.w == r.w, "finger %d joint %d ignores the thumb slider", k, j);
            }
    }
}

void TestTune() {
    std::printf("tuning\n");
    // the quaternion helpers
    const Q q90 = AxisAngle(0, 0, 1, 90.0f);
    EXPECT(Near(q90.z, 0.70710678f, 1e-6f) && Near(q90.w, 0.70710678f, 1e-6f) && q90.x == 0.0f, "90 degrees about z");
    const Q any = AxisAngle(0, 0, 5.0f, 90.0f);
    EXPECT(Near(any.z, q90.z, 1e-6f), "the axis' length does not matter");
    const Q none = AxisAngle(0, 0, 0, 90.0f), nan = AxisAngle(1, 0, 0, NAN);
    EXPECT(none.w == 1.0f && none.x == 0.0f && nan.w == 1.0f, "no axis, no angle: no turn");
    {   // QMul in the row-vector convention: the matrix of a*b is the matrix of b times the matrix of a
        const Q a = Normalised({0.3f, -0.2f, 0.5f, 0.7f}), b = Normalised({-0.1f, 0.6f, 0.2f, 0.4f});
        float ma[16], mb[16], mab[16], prod[16];
        RowMatrix(a, ma); RowMatrix(b, mb); RowMatrix(QMul(a, b), mab);
        Mul4(mb, ma, prod);
        bool same = true;
        for (int i = 0; i < 16; ++i) same = same && Near(mab[i], prod[i], 1e-5f);
        EXPECT(same, "QMul(a, b) is the matrix of b then a");
        const Q i = QMul(a, {-a.x, -a.y, -a.z, a.w});
        EXPECT(Near(i.w, 1.0f, 1e-5f) && Near(i.x, 0.0f, 1e-5f), "a times its conjugate is no turn");
    }
    // Scaled: k times the angle about the same axis, the short way round
    for (int k : {kIndex, kMiddle, kRing, kPinky})
        for (int j = 0; j < 4; ++j) {
            const Q q = Rel(k, j);
            for (float s : {0.0f, 0.25f, 0.5f, 1.0f, 1.5f}) {
                const Q x = Scaled(q, s);
                EXPECT(Near(AngleOf(x), AngleOf(q) * s, 0.02f), "finger %d joint %d x %.2f: %.3f deg of %.3f", k, j, s, AngleOf(x), AngleOf(q));
                if (AngleOf(q) > 1.0f && s > 0.0f)   // the same axis
                    EXPECT(x.x * q.x + x.y * q.y + x.z * q.z > 0.0f || std::fabs(x.x * q.x + x.y * q.y + x.z * q.z) < 1e-6f, "finger %d joint %d x %.2f: the same way", k, j, s);
            }
        }
    {
        const Q q = Rel(kMiddle, 2), n = {-q.x, -q.y, -q.z, -q.w};
        const Q a = Scaled(q, 0.5f), b = Scaled(n, 0.5f);
        EXPECT(Near(std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 1.0f, 1e-5f), "either hemisphere of the rotation: the same half");
        EXPECT(Scaled(q, NAN).w == 1.0f && Scaled({0, 0, 0, 1}, 2.0f).w == 1.0f, "NaN or no turn: none");
    }
    // the curl slider: the four fingers' rotation angle times it, the thumb not touched; 1 (and NaN) the table itself
    for (int k : {kIndex, kMiddle, kRing, kPinky})
        for (int j = 0; j < 4; ++j) {
            const Q r = Rel(k, j);
            const Q one = Tuned(k, j, Tune{1.0f, 0.0f}), nanq = Tuned(k, j, Tune{NAN, 0.0f});
            EXPECT(one.x == r.x && one.z == r.z && one.w == r.w && nanq.x == r.x && nanq.z == r.z && nanq.w == r.w, "finger %d joint %d at 1 (and NaN): as measured", k, j);
            EXPECT(Near(AngleOf(Tuned(k, j, Tune{0.0f, 0.0f})), 0.0f, 1e-3f), "finger %d joint %d at 0: open", k, j);
            EXPECT(Near(AngleOf(Tuned(k, j, Tune{0.5f, 0.0f})), AngleOf(r) * 0.5f, 0.02f), "finger %d joint %d at 0.5: half", k, j);
            EXPECT(Near(AngleOf(Tuned(k, j, Tune{1.5f, 0.0f})), AngleOf(r) * 1.5f, 0.03f), "finger %d joint %d at 1.5", k, j);
            EXPECT(Near(AngleOf(Tuned(k, j, Tune{9.0f, 0.0f})), AngleOf(r) * kMaxCurl, 0.03f), "finger %d joint %d: held at %.1f", k, j, kMaxCurl);
            EXPECT(Near(AngleOf(Tuned(k, j, Tune{-3.0f, 0.0f})), 0.0f, 1e-3f), "finger %d joint %d: not below 0", k, j);
        }
    for (int j = 0; j < 3; ++j) {
        const Q r = Rel(kThumb, j), t = Tuned(kThumb, j, Tune{0.0f, 0.0f});
        EXPECT(t.x == r.x && t.w == r.w, "the thumb joint %d does not take the finger curl", j);
    }
    // the fingertip comes nearer the hand the more it curls (the index finger of the skeleton)
    float last = 1e9f;
    for (float c : {0.0f, 0.5f, 1.0f, 1.5f}) {
        float j[4][3];
        ChainJoints(kIndex, kIndexChain, 4, true, Tune{c, 0.0f}, j);
        const float d = Len3(j[3]);
        EXPECT(d < last, "curl %.1f: the tip is %.3f from the hand (was %.3f)", c, d, last);
        last = d;
    }
    float open[4][3], bind[4][3];
    ChainJoints(kIndex, kIndexChain, 4, true, Tune{0.0f, 0.0f}, open);
    ChainJoints(kIndex, kIndexChain, 4, false, {}, bind);
    EXPECT(Dist3(open[3], bind[3]) < 1e-4f, "curl 0 is the bind pose");
}

// ---- where the string is held ----

Vec V(const float* p) { return {p[0], p[1], p[2]}; }
Vec Rot(Q q, Vec v) {   // the column-vector rotation of v by the unit quaternion q
    const Vec u = {q.x, q.y, q.z};
    const Vec t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
bool NearV(Vec a, Vec b, float eps) { return Length(a - b) <= eps; }

struct Hand {   // the points the frame and the anchor are made of, model space
    Vec wrist, middle, index, pinky, joints[3];
};

// The Amazon's right hand in its own frame (the skeleton's numbers, the hook pose): the wrist at the origin.
Hand AmazonHand() {
    Hand h;
    h.wrist = {0, 0, 0};
    h.middle = V(kMiddleChain[0].pos);
    h.index = V(kIndexChain[0].pos);
    h.pinky = V(kPinkyChain[0].pos);
    float a[4][3], b[4][3], c[4][3];
    ChainJoints(kIndex, kIndexChain, 4, true, {}, a);
    ChainJoints(kMiddle, kMiddleChain, 4, true, {}, b);
    ChainJoints(kRing, kRingChain, 4, true, {}, c);
    h.joints[0] = V(a[kAnchorJoint]);
    h.joints[1] = V(b[kAnchorJoint]);
    h.joints[2] = V(c[kAnchorJoint]);
    return h;
}

// A hand moved by a rigid turn, a uniform scale and a shift (another class's hand, held another way); `mirror` makes its left-hand twin first
// (z -> -z in the hand's own frame).
Hand Moved(Hand h, Q turn, float scale, Vec shift, bool mirror) {
    auto go = [&](Vec p) {
        if (mirror) p.z = -p.z;
        return Rot(turn, p * scale) + shift;
    };
    h.wrist = go(h.wrist); h.middle = go(h.middle); h.index = go(h.index); h.pinky = go(h.pinky);
    for (Vec& j : h.joints) j = go(j);
    return h;
}

void TestInAnchor() {
    std::printf("the joints the string lies in\n");
    EXPECT(InAnchor(kIndex) && InAnchor(kMiddle) && InAnchor(kRing) && !InAnchor(kPinky) && !InAnchor(kThumb) && !InAnchor(kNone), "index, middle and ring");
    EXPECT(kAnchorJoint == 2, "the third bone of each chain");
    // the hooked fingers are bent from that bone on - which is why the string lies in its joint: the bone before it is nearly straight (index,
    // middle), and the bend of the ring finger sits just past it
    EXPECT(AngleOf(Rel(kIndex, kAnchorJoint)) > 40.0f && AngleOf(Rel(kMiddle, kAnchorJoint)) > 40.0f, "the index and the middle bend there");
    EXPECT(AngleOf(Rel(kIndex, 1)) < 10.0f, "and not before it");
}

void TestFrame() {
    std::printf("a hand's frame\n");
    const Hand h = AmazonHand();
    const Frame f = HandFrame(h.wrist, h.middle, h.index, h.pinky, 0);
    EXPECT(f.ok, "the Amazon's right hand has a frame");
    EXPECT(Near(Length(f.along), 1.0f, 1e-5f) && Near(Length(f.across), 1.0f, 1e-5f) && Near(Length(f.palm), 1.0f, 1e-5f), "unit axes");
    EXPECT(Near(Dot(f.along, f.across), 0.0f, 1e-5f) && Near(Dot(f.along, f.palm), 0.0f, 1e-5f) && Near(Dot(f.across, f.palm), 0.0f, 1e-5f), "square to each other");
    // in the hand's own frame the fingers run along -x, the thumb side is -z, and the fingers curl toward +y (the offline check)
    EXPECT(Dot(f.along, {-1, 0, 0}) > 0.9f, "along: toward the fingertips (%.2f)", Dot(f.along, {-1, 0, 0}));
    EXPECT(Dot(f.across, {0, 0, -1}) > 0.95f, "across: toward the thumb side (%.2f)", Dot(f.across, {0, 0, -1}));
    EXPECT(Dot(f.palm, {0, 1, 0}) > 0.9f, "palm: the way the fingers curl (%.2f)", Dot(f.palm, {0, 1, 0}));
    // the curled fingers' first joints really lie on the palm side of the straight hand: the centroid is on +palm of the line of the knuckles
    EXPECT(Dot((h.joints[0] + h.joints[1] + h.joints[2]) * (1.0f / 3.0f), f.palm) > 0.0f, "the hooked joints are on the palm side");
    // either hand: the mirror twin (z -> -z) of the right hand, as a left hand, has the mirrored axes in its own sense: the thumb side is +z now,
    // the fingers still curl toward +y (palm = the mirrored palm: the left's sign of the cross product undoes the mirror's)
    const Hand m = Moved(h, {0, 0, 0, 1}, 1.0f, {0, 0, 0}, true);
    const Frame g = HandFrame(m.wrist, m.middle, m.index, m.pinky, 1);
    const auto mir = [](Vec v) { return Vec{v.x, v.y, -v.z}; };
    EXPECT(g.ok && Dot(g.along, mir(f.along)) > 0.9999f && Dot(g.palm, mir(f.palm)) > 0.9999f && Dot(g.across, mir(f.across)) > 0.9999f && Dot(g.across, {0, 0, 1}) > 0.95f && Dot(g.palm, {0, 1, 0}) > 0.9f,
           "the left hand's twin: every axis mirrored, the palm still the way the fingers curl");
    // the wrong side's rule would turn the palm over (a mutation of the sign): the right hand's palm is -(along x across)
    EXPECT(Dot(f.palm, Cross(f.along, f.across)) < -0.99f && Dot(g.palm, Cross(g.along, g.across)) > 0.99f, "the right palm is -(along x across), the left +");
    // degenerate: nothing to make a frame of
    EXPECT(!HandFrame(h.wrist, h.wrist, h.index, h.pinky, 0).ok, "the middle knuckle on the wrist");
    EXPECT(!HandFrame(h.wrist, h.middle, h.index, h.index, 0).ok, "index and little finger on one point");
    EXPECT(!HandFrame(h.wrist, h.middle, h.wrist + (h.middle - h.wrist) * 2.0f, h.wrist, 0).ok, "across in line with the hand");
    EXPECT(!HandFrame({NAN, 0, 0}, h.middle, h.index, h.pinky, 0).ok && !HandFrame(h.wrist, h.middle, h.index, {0, INFINITY, 0}, 0).ok, "numbers that are not");
}

void TestAnchor() {
    std::printf("the string's place\n");
    const Hand h = AmazonHand();
    const Frame f = HandFrame(h.wrist, h.middle, h.index, h.pinky, 0);
    const float upm = 3.0f;   // model units to the metre
    Vec centre, p;
    EXPECT(AnchorPoint(h.joints, 3, f, 0, 0, 0, upm, &centre), "a point");
    // the offline numbers: the middle of the three first joints
    EXPECT(NearV(centre, {-0.4557f, 0.0418f, -0.0446f}, 0.003f), "the centre of the first joints is (%.4f %.4f %.4f)", centre.x, centre.y, centre.z);
    // the controller's grip point is the middle of the palm - half way from the wrist to the middle knuckle: the string used to be drawn from there
    const Vec palm = (h.wrist + h.middle) * 0.5f;
    const float behind = Length(centre - palm) / upm * 100.0f;
    EXPECT(behind > 9.0f && behind < 15.0f, "the string used to be %.1f cm behind the first joints", behind);
    EXPECT(Dot(centre - palm, f.along) > 0.3f, "and the joints are further along the fingers");
    // the sliders: cm along their own axes, in model units (upm to the metre)
    EXPECT(AnchorPoint(h.joints, 3, f, 10, 0, 0, upm, &p) && NearV(p, centre + f.along * (0.1f * upm), 1e-5f), "10 cm along the fingers");
    EXPECT(AnchorPoint(h.joints, 3, f, 0, -4, 0, upm, &p) && NearV(p, centre - f.across * (0.04f * upm), 1e-5f), "4 cm toward the little finger");
    EXPECT(AnchorPoint(h.joints, 3, f, 0, 0, 0.8f, upm, &p) && NearV(p, centre + f.palm * (0.008f * upm), 1e-5f), "0.8 cm out of the palm");
    EXPECT(AnchorPoint(h.joints, 3, f, 2, -3, 1, upm * 2.0f, &p) && NearV(p, centre + (f.along * 2.0f - f.across * 3.0f + f.palm * 1.0f) * (0.01f * upm * 2.0f), 1e-5f), "all three, in a hand twice the size");
    // held at 15 cm; not numbers are 0
    EXPECT(AnchorPoint(h.joints, 3, f, 999, 0, 0, upm, &p) && NearV(p, centre + f.along * (kMaxAnchorCm * 0.01f * upm), 1e-5f), "held at %.0f cm", kMaxAnchorCm);
    EXPECT(AnchorPoint(h.joints, 3, f, -999, 0, 0, upm, &p) && NearV(p, centre - f.along * (kMaxAnchorCm * 0.01f * upm), 1e-5f), "held at -%.0f cm", kMaxAnchorCm);
    EXPECT(AnchorPoint(h.joints, 3, f, NAN, NAN, INFINITY, upm, &p) && NearV(p, centre, 1e-6f), "NaN and infinity: 0");
    EXPECT(Near(ClampCm(NAN), 0.0f) && Near(ClampCm(1e9f), kMaxAnchorCm) && Near(ClampCm(-1e9f), -kMaxAnchorCm) && Near(ClampCm(3.0f), 3.0f), "ClampCm");
    // one joint, two, three: the middle of those given
    Vec two;
    EXPECT(AnchorPoint(h.joints, 2, f, 0, 0, 0, upm, &two) && NearV(two, (h.joints[0] + h.joints[1]) * 0.5f, 1e-6f), "the middle of two");
    EXPECT(AnchorPoint(h.joints, 1, f, 0, 0, 0, upm, &two) && NearV(two, h.joints[0], 1e-6f), "one");
    // nothing to go on
    EXPECT(!AnchorPoint(h.joints, 0, f, 0, 0, 0, upm, &p) && !AnchorPoint(nullptr, 3, f, 0, 0, 0, upm, &p), "no joints");
    EXPECT(!AnchorPoint(h.joints, 3, Frame{}, 0, 0, 0, upm, &p), "no frame");
    EXPECT(!AnchorPoint(h.joints, 3, f, 0, 0, 0, 0.0f, &p) && !AnchorPoint(h.joints, 3, f, 0, 0, 0, -1.0f, &p) && !AnchorPoint(h.joints, 3, f, 0, 0, 0, NAN, &p), "no scale");
    Vec bad[3] = {h.joints[0], {NAN, 0, 0}, h.joints[2]};
    EXPECT(!AnchorPoint(bad, 3, f, 0, 0, 0, upm, &p), "a joint that is not a number");
}

// The hands of the eight hero classes differ (their size, how they are turned in the model, which hand): whatever the hand, the anchor is the same
// point of it - the sliders' directions its own, the cm real centimetres. A hand of another size is the Amazon's scaled (the class's proportions
// differ a little, which only moves the points, not what the maths does with them).
void TestClasses() {
    std::printf("per hero class\n");
    const Hand base = AmazonHand();
    const float scales[8] = {1.0f, 0.9f, 1.05f, 1.1f, 1.3f, 1.15f, 0.95f, 1.2f};   // amazon sorceress necromancer paladin barbarian druid assassin warlock
    unsigned seed = 11;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / (float)(1u << 24) * 2.0f - 1.0f; };
    for (int cls = 0; cls < 8; ++cls) {
        for (int trial = 0; trial < 30; ++trial) {
            const bool left = (trial & 1) != 0;
            const Q turn = Normalised({rnd(), rnd(), rnd(), rnd() + 0.2f});
            const Vec shift = {rnd() * 3.0f, rnd() * 3.0f, rnd() * 3.0f};
            const float scale = scales[cls];
            const float upm = 3.0f * scale;   // (the hero's units to the metre grow with the hand)
            const Hand h = Moved(base, turn, scale, shift, left);
            const Frame f = HandFrame(h.wrist, h.middle, h.index, h.pinky, left ? 1 : 0);
            EXPECT(f.ok, "class %d trial %d: a frame", cls, trial);
            if (!f.ok) continue;
            // the frame is the Amazon's, turned and mirrored along: along and palm are the same vectors turned; across is turned (and mirrored for a left)
            const Frame f0 = HandFrame(base.wrist, base.middle, base.index, base.pinky, 0);
            const auto mir = [&](Vec v) { return left ? Vec{v.x, v.y, -v.z} : v; };   // (the left hand is the right one mirrored first)
            EXPECT(NearV(f.along, Rot(turn, mir(f0.along)), 2e-4f) && NearV(f.palm, Rot(turn, mir(f0.palm)), 2e-4f) && NearV(f.across, Rot(turn, mir(f0.across)), 2e-4f),
                   "class %d trial %d: the axes follow the hand", cls, trial);
            // the anchor: the same point of the hand, however it is turned, scaled or mirrored
            Vec a0, a1;
            EXPECT(AnchorPoint(base.joints, 3, f0, 1.5f, -2.0f, 0.8f, 3.0f, &a0), "class %d trial %d: the Amazon's anchor", cls, trial);
            EXPECT(AnchorPoint(h.joints, 3, f, 1.5f, -2.0f, 0.8f, upm, &a1), "class %d trial %d: the anchor", cls, trial);
            // (the same slider values on either hand give the mirrored point: the sliders mean the same on both)
            Vec want = a0;
            if (left) want.z = -want.z;
            want = Rot(turn, want * scale) + shift;
            EXPECT(NearV(a1, want, 3e-4f * scale + 1e-4f), "class %d trial %d: the same point of the hand (%.4f away)", cls, trial, Length(a1 - want));
            // and a slider's centimetre is a real one: 1 cm out of the palm moves it 0.01 * upm
            Vec b;
            EXPECT(AnchorPoint(h.joints, 3, f, 0, 0, 0, upm, &a1) && AnchorPoint(h.joints, 3, f, 0, 0, 1.0f, upm, &b) && Near(Length(b - a1), 0.01f * upm, 1e-5f * upm), "class %d trial %d: 1 cm is 1 cm", cls, trial);
        }
    }
}

void TestLerp() {
    std::printf("the ease\n");
    const Vec a = {1, 2, 3}, b = {5, -2, 7};
    EXPECT(NearV(Lerp(a, b, 0.0f), a, 1e-6f) && NearV(Lerp(a, b, 1.0f), b, 1e-6f) && NearV(Lerp(a, b, 0.25f), {2, 1, 4}, 1e-6f), "0, 1, a quarter");
    EXPECT(NearV(Lerp(a, b, -1.0f), a, 1e-6f) && NearV(Lerp(a, b, 7.0f), b, 1e-6f) && NearV(Lerp(a, b, NAN), a, 1e-6f), "held between, NaN is the start");
}

}  // namespace

int main() {
    TestNames();
    TestTable();
    TestMatrix();
    TestBlend();
    TestIndexFinger();
    TestThumb();
    TestTune();
    TestInAnchor();
    TestFrame();
    TestAnchor();
    TestClasses();
    TestLerp();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
