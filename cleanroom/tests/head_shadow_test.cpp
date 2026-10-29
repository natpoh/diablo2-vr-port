// head_shadow_test - [arms] head_shadow, the pure part (vr/head_shadow.h): the head shrunk in the eyes' skinning only, whole in the shadows'. No game: a small rig
// is posed by hand and skinned the way Granny's composite does it (a vertex in the bind pose, times the bone's inverse bind matrix, times its world matrix).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../vr/head_shadow.h"

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

using namespace headshadow;

struct M { float m[16]; };
struct V { float x, y, z; };

M Identity() { M r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
M Translate(float x, float y, float z) { M r = Identity(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
M RotY(float deg) {
    const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    M r = Identity();
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
M RotX(float deg) {
    const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
    M r = Identity();
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
// row vectors: a then b
M Mul(const M& a, const M& b) {
    M r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}
M InvRigid(const M& a) {   // rotation + translation only
    M r = Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = a.m[j * 4 + i];
    for (int j = 0; j < 3; ++j) r.m[12 + j] = -(a.m[12] * r.m[0 * 4 + j] + a.m[13] * r.m[1 * 4 + j] + a.m[14] * r.m[2 * 4 + j]);
    return r;
}
V Apply(const M& a, V v) {
    return {v.x * a.m[0] + v.y * a.m[4] + v.z * a.m[8] + a.m[12], v.x * a.m[1] + v.y * a.m[5] + v.z * a.m[9] + a.m[13],
            v.x * a.m[2] + v.y * a.m[6] + v.z * a.m[10] + a.m[14]};
}
float Dist(V a, V b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// A small rig: root > spine > neck > head > (jaw, helm attach); the neck and everything under it is what the shrink covers (skeletons.cpp's inNeck).
enum { kRoot, kSpine, kNeck, kHead, kJaw, kHelm, kBones };
const int kParent[kBones] = {-1, kRoot, kSpine, kNeck, kHead, kHead};
const char kInNeck[kBones] = {0, 0, 1, 1, 1, 1};
const M kLocalBind[kBones] = {Identity(), Translate(0, 1.0f, 0), Translate(0, 0.5f, 0), Translate(0, 0.15f, 0), Translate(0, -0.05f, 0.08f), Translate(0, 0.12f, 0)};

std::vector<M> World(const M* local) {
    std::vector<M> w(kBones);
    for (int i = 0; i < kBones; ++i) w[i] = kParent[i] < 0 ? local[i] : Mul(local[i], w[kParent[i]]);
    return w;
}

// The skinning of one vertex bound to one bone: the composite (inverse bind times world), as BuildIndexedCompositeBuffer makes it.
V Skin(const std::vector<M>& bind, const std::vector<M>& pose, int bone, V v) { return Apply(Mul(InvRigid(bind[bone]), pose[bone]), v); }

void TestShrinkIsTheOldOne() {
    std::printf("the shrink is what the pose was given before (vr/skeletons.cpp, 2026-10-02 - 10-25)\n");
    M local[kBones];
    for (int i = 0; i < kBones; ++i) local[i] = Mul(Mul(RotY(13.0f * i), RotX(7.0f * i - 10.0f)), kLocalBind[i]);
    std::vector<M> pose = World(local), old = pose;
    // the old code, verbatim in its numbers
    {
        const int pivot = kNeck;
        const float p[3] = {old[pivot].m[12], old[pivot].m[13], old[pivot].m[14]};
        for (int i = 0; i < kBones; ++i)
            if (kInNeck[i]) {
                for (int r = 0; r < 3; ++r)
                    for (int cc = 0; cc < 3; ++cc) old[i].m[r * 4 + cc] *= 0.001f;
                old[i].m[12] = p[0]; old[i].m[13] = p[1]; old[i].m[14] = p[2];
            }
    }
    Shrink(reinterpret_cast<float(*)[16]>(pose.data()), kBones, kInNeck, kNeck);
    EXPECT(std::memcmp(pose.data(), old.data(), sizeof(M) * kBones) == 0, "the same bits as the old loop");
    // nothing to do: no flags, a pivot out of range, no matrices
    std::vector<M> same = World(local), before = same;
    Shrink(reinterpret_cast<float(*)[16]>(same.data()), kBones, kInNeck, kBones);
    Shrink(reinterpret_cast<float(*)[16]>(same.data()), kBones, kInNeck, -1);
    Shrink(reinterpret_cast<float(*)[16]>(same.data()), kBones, nullptr, kNeck);
    Shrink(nullptr, kBones, kInNeck, kNeck);
    EXPECT(std::memcmp(same.data(), before.data(), sizeof(M) * kBones) == 0, "a bad pivot or no flags leave the pose alone");
}

void TestShadowKeepsTheHead() {
    std::printf("the shadows' skinning has the head, the eyes' has it shrunk - and the eyes see what they saw before\n");
    const std::vector<M> bind = World(kLocalBind);
    // the hero turned and nodding: a posed rig
    M local[kBones];
    for (int i = 0; i < kBones; ++i) local[i] = kLocalBind[i];
    local[kRoot] = Mul(RotY(40.0f), Translate(3.0f, 0.0f, -2.0f));
    local[kNeck] = Mul(RotX(15.0f), kLocalBind[kNeck]);
    local[kHead] = Mul(RotX(-20.0f), kLocalBind[kHead]);
    const std::vector<M> posed = World(local);
    // a vertex on the crown (bound to the head), one on the chest (bound to the spine), one of the helm (its rigid model rides the attach bone)
    const V crown = {0.0f, bind[kHead].m[13] + 0.12f, 0.02f}, chest = {0.0f, bind[kSpine].m[13] + 0.3f, 0.12f}, helm = {0.0f, bind[kHelm].m[13] + 0.02f, 0.0f};
    const V pivot = {posed[kNeck].m[12], posed[kNeck].m[13], posed[kNeck].m[14]};

    // before: the pose itself shrunk - every pass, the shadows' too
    std::vector<M> oldPose = posed;
    Shrink(reinterpret_cast<float(*)[16]>(oldPose.data()), kBones, kInNeck, kNeck);
    // now: the pose whole (the shadows'), the eyes' a copy shrunk
    const std::vector<M> shadowPose = posed;
    std::vector<M> eyesPose = posed;
    Shrink(reinterpret_cast<float(*)[16]>(eyesPose.data()), kBones, kInNeck, kNeck);

    const V crownShadowOld = Skin(bind, oldPose, kHead, crown), crownShadowNew = Skin(bind, shadowPose, kHead, crown), crownEyes = Skin(bind, eyesPose, kHead, crown);
    std::printf("  the crown: shadow before %.3f m from the neck's base, shadow now %.3f m, the eyes now %.4f m\n", Dist(crownShadowOld, pivot), Dist(crownShadowNew, pivot),
                Dist(crownEyes, pivot));
    EXPECT(Dist(crownShadowOld, pivot) < 0.002f, "before, the shadow's head was a point at the neck (%.4f m)", Dist(crownShadowOld, pivot));
    EXPECT(Dist(crownShadowNew, pivot) > 0.2f, "now the shadow's head is where the head is (%.3f m from the neck's base)", Dist(crownShadowNew, pivot));
    EXPECT(Dist(crownEyes, pivot) < 0.002f, "the eyes' head is still a point at the neck (%.4f m)", Dist(crownEyes, pivot));
    // the head where the pose has it: up the neck and the head from the neck's base, nodded
    const V crownWant = Apply(Mul(InvRigid(bind[kHead]), posed[kHead]), crown);
    EXPECT(Dist(crownShadowNew, crownWant) < 1e-5f, "the shadow's crown is the posed crown");
    // the eyes see exactly what they saw before (bit for bit the same composite)
    for (int b = 0; b < kBones; ++b) {
        const M was = Mul(InvRigid(bind[b]), oldPose[b]), now = Mul(InvRigid(bind[b]), eyesPose[b]);
        EXPECT(std::memcmp(&was, &now, sizeof was) == 0, "bone %d: the eyes' skinning is the old one", b);
    }
    // the chest is not touched in either
    EXPECT(Dist(Skin(bind, shadowPose, kSpine, chest), Skin(bind, posed, kSpine, chest)) < 1e-6f, "the shadow's chest as posed");
    EXPECT(Dist(Skin(bind, eyesPose, kSpine, chest), Skin(bind, posed, kSpine, chest)) < 1e-6f, "the eyes' chest as posed");
    // the helm rides its attach bone: whole in the shadow's pose (so its model is left out of the eyes' passes instead - HangsOnHead)
    EXPECT(Dist(Skin(bind, shadowPose, kHelm, helm), pivot) > 0.2f, "the helm's bone is whole in the shadow's pose");
}

void TestDecide() {
    std::printf("when the pose keeps the head\n");
    Gate g;
    g.wanted = g.f4 = g.hooksIn = g.xformIn = true;
    g.bones = 180;
    g.now = 100000;
    g.eyesSeenAt = 99950;
    EXPECT(Decide(g) == Why::EyesOnly, "all there: the head in the pose, shrunk for the eyes");
    Gate t = g; t.wanted = false; EXPECT(Decide(t) == Why::Off, "unticked: as before");
    t = g; t.f4 = false; EXPECT(Decide(t) == Why::NotF4, "not F4: as before");
    t = g; t.faulted = true; EXPECT(Decide(t) == Why::Faulted, "a fault: as before");
    t = g; t.hooksIn = false; EXPECT(Decide(t) == Why::NoHooks, "no hooks: as before");
    t = g; t.xformIn = false; EXPECT(Decide(t) == Why::NoXform, "no world-matrix hook (a helm would show): as before");
    t = g; t.along = true; EXPECT(Decide(t) == Why::Along, "a rig posed along: as before");
    t = g; t.bones = kMaxBones + 1; EXPECT(Decide(t) == Why::TooManyBones, "too many bones: as before");
    t = g; t.bones = 0; EXPECT(Decide(t) == Why::TooManyBones, "no bones: as before");
    t = g; t.eyesSeenAt = 0; EXPECT(Decide(t) == Why::NotSeenYet, "the eyes' copy never seen taken: shrunk in the pose meanwhile");
    t = g; t.eyesSeenAt = g.now - kFreshMs - 1; EXPECT(Decide(t) == Why::NotSeenYet, "not seen for longer than kFreshMs: back to the pose's shrink");
    t = g; t.eyesSeenAt = g.now - kFreshMs; EXPECT(Decide(t) == Why::EyesOnly, "seen just kFreshMs ago: still on");
    t = g; t.eyesSeenAt = g.now + 5; EXPECT(Decide(t) == Why::NotSeenYet, "a time from the future is not trusted");
    for (int w = 0; w <= (int)Why::NotSeenYet; ++w) EXPECT(Text((Why)w)[0] != 0, "a text for every state (%d)", w);
}

void TestPasses() {
    std::printf("which passes lose the head\n");
    EXPECT(!EyesPass(kPassShadow), "the shadow passes keep it");
    EXPECT(EyesPass(0), "the scene's passes (the eyes) lose it");
    EXPECT(!EyesPass(kPassNone), "outside a draw collector call nothing is changed");
}

void TestHangsOnHead() {
    std::printf("what hangs on the head is left out of the eyes\n");
    const uintptr_t hero = 0x7FF612340000ull, other = 0x7FF612350000ull;
    EXPECT(HangsOnHead(false, true, hero, kHelm, hero, kInNeck, kBones), "a model on the helm's attach bone of the hero");
    EXPECT(HangsOnHead(false, true, hero, kHead, hero, kInNeck, kBones), "a model on the head bone itself");
    EXPECT(!HangsOnHead(false, true, hero, kSpine, hero, kInNeck, kBones), "a model on the back (a stowed weapon) stays");
    EXPECT(!HangsOnHead(false, true, other, kHelm, hero, kInNeck, kBones), "another unit's helm stays");
    EXPECT(!HangsOnHead(false, false, hero, kHelm, hero, kInNeck, kBones), "no attachment, parent not marked: stays");
    EXPECT(HangsOnHead(true, false, 0, -1, hero, kInNeck, kBones), "under a marked entity (the helm's model under its unit): left out too");
    EXPECT(!HangsOnHead(false, true, hero, kBones, hero, kInNeck, kBones), "a bone out of the rig: stays");
    EXPECT(!HangsOnHead(false, true, hero, -1, hero, kInNeck, kBones), "no bone: stays");
    EXPECT(!HangsOnHead(false, true, 0, kHelm, 0, kInNeck, kBones), "no hero known: stays");
    EXPECT(!HangsOnHead(false, true, hero, kHelm, hero, nullptr, kBones), "no flags: stays");
}

}  // namespace

int main() {
    TestShrinkIsTheOldOne();
    TestShadowKeepsTheHead();
    TestDecide();
    TestPasses();
    TestHangsOnHead();
    std::printf("head_shadow_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
