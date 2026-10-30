// Offline checks of vr/weapon_mirror.h ([weapon_<kind>] on the left hand's weapon): the same slider values give the left weapon the exact mirror image of
// the right one's pose - on the Amazon's own rig (its bind pose's hands and attach bones, read from the game's amazon.skeleton) and on made-up rigs whose
// left bone is any other turn-over of the right one, a mirrored (det < 0) left bone too. Run: weapon_mirror_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "weapon_mirror.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

struct M { float m[16]; };

// skeletons.cpp's: row vectors, the rows the bone's axes, the place in 12..14
static M Mul(const M& a, const M& b) {
    M o{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) o.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return o;
}
static M Ident() { M r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
static M Turn(int axis, float deg) {   // skeletons.cpp's [weapon_<kind>] turn about one of the bone's own axes
    M r = Ident();
    const float t = deg * 0.0174532925f, c = std::cos(t), s = std::sin(t);
    const int i = (axis + 1) % 3, j = (axis + 2) % 3;
    r.m[i * 4 + i] = c; r.m[i * 4 + j] = s; r.m[j * 4 + i] = -s; r.m[j * 4 + j] = c;
    return r;
}
static M MirrorX() { M r = Ident(); r.m[0] = -1.0f; return r; }   // the body's plane: x = 0
static M FromK(const weaponmirror::K& k) {
    M r = Ident();
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = 0.0f;
        r.m[i * 4 + k.col[i]] = (float)k.sign[i];
    }
    return r;
}
// skeletons.cpp's whole adjustment of one bone: turned in its own frame, moved along its own unit axes (cm, upm units a metre)
static M Adjust(const M& bone, const M& turn, const float move[3], float upm) {
    float shift[3] = {0, 0, 0};
    for (int r = 0; r < 3; ++r) {
        const float x = bone.m[r * 4], y = bone.m[r * 4 + 1], z = bone.m[r * 4 + 2], l = std::sqrt(x * x + y * y + z * z);
        for (int k = 0; k < 3; ++k) shift[k] += bone.m[r * 4 + k] / l * move[r] * 0.01f * upm;
    }
    M o = Mul(turn, bone);
    for (int k = 0; k < 3; ++k) o.m[12 + k] += shift[k];
    return o;
}
static float Diff(const M& a, const M& b) {
    float d = 0.0f;
    for (int i = 0; i < 16; ++i) d = std::fmax(d, std::fabs(a.m[i] - b.m[i]));
    return d;
}
static float Rnd() { return (float)std::rand() / (float)RAND_MAX * 2.0f - 1.0f; }
static M RandomBone(float scale) {   // a turn (three random turns), a scale, a place
    M r = Mul(Mul(Turn(0, Rnd() * 180.0f), Turn(1, Rnd() * 180.0f)), Turn(2, Rnd() * 180.0f));
    for (int i = 0; i < 12; ++i) r.m[i] *= scale;
    r.m[12] = Rnd() * 3.0f; r.m[13] = 3.0f + Rnd(); r.m[14] = Rnd();
    return r;
}

// The Amazon's bind pose (amazon.skeleton: the inverse of each bone's InverseWorldTransform), model units
static const M kHandR = {{0.551443f, 0.763809f, -0.33542f, 0, 0.802418f, -0.595609f, -0.0370986f, 0, -0.228115f, -0.248689f, -0.941339f, 0, -2.02859f, 4.21461f, 0.340608f, 1}};
static const M kHandL = {{0.551443f, -0.763809f, 0.33542f, 0, 0.802417f, 0.595608f, 0.0370985f, 0, -0.228115f, 0.248689f, 0.941338f, 0, 2.02858f, 4.21459f, 0.340605f, 1}};
static const M kWeapR = {{0.551444f, 0.76381f, -0.335419f, 0, 0.228114f, 0.248688f, 0.941339f, 0, 0.802417f, -0.595608f, -0.0370983f, 0, -2.10044f, 3.90726f, 0.438887f, 1}};
static const M kWeapL = {{0.551444f, -0.76381f, 0.335419f, 0, -0.228114f, 0.248688f, 0.941339f, 0, -0.802417f, -0.595608f, -0.0370983f, 0, 2.09971f, 3.90827f, 0.438445f, 1}};

static M InvRigid(const M& a) {   // a turn and a place (rows unit): the transpose, the place carried back
    M o = Ident();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) o.m[i * 4 + j] = a.m[j * 4 + i];
    for (int j = 0; j < 3; ++j) o.m[12 + j] = -(a.m[12] * o.m[j] + a.m[13] * o.m[4 + j] + a.m[14] * o.m[8 + j]);
    return o;
}

int main() {
    std::srand(1234);
    const float upm = 3.28084f;

    // 1. The Amazon: hands each other's turn-over in all three axes, the attach bones in x only (the shaft, +y, and z as reflected)
    weaponmirror::K kh, kw;
    CHECK(weaponmirror::FromMirrorPair(kHandR.m, kHandL.m, &kh));
    CHECK(kh.col[0] == 0 && kh.col[1] == 1 && kh.col[2] == 2 && kh.sign[0] < 0 && kh.sign[1] < 0 && kh.sign[2] < 0);
    CHECK(weaponmirror::FromMirrorPair(kWeapR.m, kWeapL.m, &kw));
    CHECK(kw.col[0] == 0 && kw.col[1] == 1 && kw.col[2] == 2 && kw.sign[0] < 0 && kw.sign[1] > 0 && kw.sign[2] > 0);
    // ... and the same from the game's grips (each attach bone against its wrist, here the bind pose's)
    {
        weaponmirror::K kg;
        const M aR = Mul(kWeapR, InvRigid(kHandR)), aL = Mul(kWeapL, InvRigid(kHandL));
        CHECK(weaponmirror::FromGrips(aR.m, aL.m, kh, &kg));
        CHECK(kg.col[0] == kw.col[0] && kg.col[1] == kw.col[1] && kg.col[2] == kw.col[2] && kg.sign[0] == kw.sign[0] && kg.sign[1] == kw.sign[1] && kg.sign[2] == kw.sign[2]);
    }

    // 2. The user's words (2026-10-30): "14 on the right, -14 on the left" - Turn about Y 14 is Turn about Y -14 on the Amazon's left bone; Turn about Z
    // the same; Turn about X (across the blade's flat) stays; Move X goes the other way, Move Y / Z stay.
    {
        const float none[3] = {0, 0, 0};
        float move[3];
        M out;
        weaponmirror::Carry(kw, Turn(1, 14.0f).m, none, out.m, move);
        CHECK(Diff(out, Turn(1, -14.0f)) < 1e-5f);
        weaponmirror::Carry(kw, Turn(2, 14.0f).m, none, out.m, move);
        CHECK(Diff(out, Turn(2, -14.0f)) < 1e-5f);
        weaponmirror::Carry(kw, Turn(0, 14.0f).m, none, out.m, move);
        CHECK(Diff(out, Turn(0, 14.0f)) < 1e-5f);
        const float mv[3] = {3.0f, -2.0f, 1.5f};
        weaponmirror::Carry(kw, Ident().m, mv, out.m, move);
        CHECK(std::fabs(move[0] + 3.0f) < 1e-6f && std::fabs(move[1] + 2.0f) < 1e-6f && std::fabs(move[2] - 1.5f) < 1e-6f);
    }

    // 3. Any pose, any sliders: the left weapon (= K * right weapon * X, any turn-over K - a mirrored left bone too) adjusted with the carried numbers is the
    // right weapon adjusted with the user's numbers, reflected. Each of the 48 turn-overs, 40 poses each.
    int ks = 0;
    for (int p = 0; p < 6; ++p) {
        static const int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (int sg = 0; sg < 8; ++sg) {
            weaponmirror::K k;
            for (int i = 0; i < 3; ++i) { k.col[i] = perm[p][i]; k.sign[i] = (sg >> i) & 1 ? -1 : 1; }
            ++ks;
            float worst = 0.0f;
            for (int t = 0; t < 40; ++t) {
                const M right = RandomBone(0.5f + std::fabs(Rnd()));
                const M left = Mul(Mul(FromK(k), right), MirrorX());
                weaponmirror::K read;   // the correspondence read back off the pair
                CHECK(weaponmirror::FromMirrorPair(right.m, left.m, &read) && read.col[0] == k.col[0] && read.col[1] == k.col[1] && read.col[2] == k.col[2] &&
                      read.sign[0] == k.sign[0] && read.sign[1] == k.sign[1] && read.sign[2] == k.sign[2]);
                const float a[6] = {Rnd() * 40.0f, Rnd() * 40.0f, Rnd() * 40.0f, Rnd() * 180.0f, Rnd() * 180.0f, Rnd() * 180.0f};
                const M turn = Mul(Mul(Turn(0, a[3]), Turn(1, a[4])), Turn(2, a[5]));
                M turnL;
                float moveL[3];
                weaponmirror::Carry(read, turn.m, a, turnL.m, moveL);
                const M rightAdj = Adjust(right, turn, a, upm);
                const M leftAdj = Adjust(left, turnL, moveL, upm);
                const M want = Mul(Mul(FromK(k), rightAdj), MirrorX());
                worst = std::fmax(worst, Diff(leftAdj, want));
            }
            CHECK(worst < 2e-4f);
            if (!(worst < 2e-4f)) std::printf("  turn-over col %d %d %d sign %d %d %d: worst %.6f\n", k.col[0], k.col[1], k.col[2], k.sign[0], k.sign[1], k.sign[2], worst);
        }
    }
    CHECK(ks == 48);

    // 4. As before, the same numbers in its own axes, was the mirror only for a left bone that is the right one plainly reflected (K = 1): not the Amazon's
    {
        const M right = RandomBone(1.0f);
        const M left = Mul(Mul(FromK(kw), right), MirrorX());
        const float a[3] = {0, 0, 0};
        const M turn = Turn(1, 14.0f);
        CHECK(Diff(Adjust(left, turn, a, upm), Mul(Mul(FromK(kw), Adjust(right, turn, a, upm)), MirrorX())) > 0.05f);
    }

    // 5. No clear correspondence (a left bone half a right angle off any turn-over): none read - the caller keeps the numbers as they were
    {
        const M right = RandomBone(1.0f);
        const M left = Mul(Mul(Turn(1, 45.0f), right), MirrorX());
        weaponmirror::K k;
        CHECK(!weaponmirror::FromMirrorPair(right.m, left.m, &k));
        M zero{};
        CHECK(!weaponmirror::FromMirrorPair(right.m, zero.m, &k));   // a bone shrunk to a point
    }

    std::printf("weapon_mirror_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
