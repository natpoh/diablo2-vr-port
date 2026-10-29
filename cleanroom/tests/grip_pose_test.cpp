// Offline checks of the grip pose's maths (vr/grip_pose.h): the hand that takes a weapon gets the game's weapon-grip pose of the fingers and the thumb, read off the game's hand and put on
// the other one in the hand's own frame - whichever hand, however the rig labels its bones' axes. A synthetic rig: a right hand (a wrist, five finger chains of three bones, each with a
// bind orientation of its own and a curl), its mirror image as the left hand (positions reflected, the bones' axes reflected and relabelled the way a rig may), the taking hand put
// anywhere in the world. Pure maths, no game.
// Run: build/Release/grip_pose_test.exe   (exit code 0 = all passed)

#include "../../vr/grip_pose.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace grippose;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(V a, V b, float eps) { return Len(Sub(a, b)) <= eps; }
static float MaxDiff(const M3& a, const M3& b) {
    float d = 0.0f;
    for (int i = 0; i < 9; ++i) d = std::fmax(d, std::fabs(a.m[i] - b.m[i]));
    return d;
}

struct Bone { M3 R; V p; };
struct Rig {
    std::vector<Bone> bind, pose;
    std::vector<int> parent;
    std::vector<std::vector<int>> chains;   // index, middle, ring, pinky, thumb: three bones each
    int wrist = 0, ind = 0, mid = 0, pnk = 0;
};

static M3 RotX(float a) { M3 r; r.m[4] = std::cos(a); r.m[5] = std::sin(a); r.m[7] = -std::sin(a); r.m[8] = std::cos(a); return r; }
static M3 RotY(float a) { M3 r; r.m[0] = std::cos(a); r.m[2] = -std::sin(a); r.m[6] = std::sin(a); r.m[8] = std::cos(a); return r; }
static M3 RotZ(float a) { M3 r; r.m[0] = std::cos(a); r.m[1] = std::sin(a); r.m[3] = -std::sin(a); r.m[4] = std::cos(a); return r; }
static V Apply(V v, const M3& r) { return {v.x * r.m[0] + v.y * r.m[3] + v.z * r.m[6], v.x * r.m[1] + v.y * r.m[4] + v.z * r.m[7], v.x * r.m[2] + v.y * r.m[5] + v.z * r.m[8]}; }
static V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

// The right hand: a bind pose (the fingers open, each bone's own orientation) and a pose (each bone's local turned by a curl).
static Rig RightHand(unsigned seed, bool curled) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    Rig r;
    const V knuckle[5] = {{0.03f, -0.088f, 0.0f}, {0.01f, -0.09f, 0.0f}, {-0.01f, -0.088f, 0.0f}, {-0.03f, -0.08f, 0.0f}, {0.035f, -0.03f, 0.03f}};
    r.bind.push_back({M3{}, {0, 0, 0}});
    r.parent.push_back(-1);
    for (int c = 0; c < 5; ++c) {
        std::vector<int> chain;
        for (int j = 0; j < 3; ++j) {
            const int parent = j == 0 ? 0 : (int)r.bind.size() - 1;
            // the bone's own local orientation (bind) and its place: the chain along the parent's -y, the first knuckle at its place on the hand
            const M3 lo = Mul(Mul(RotX(0.3f * u(rng)), RotY(0.5f * u(rng))), RotZ(0.3f * u(rng)));
            const V t = j == 0 ? knuckle[c] : V{0.0f, -0.03f, 0.0f};
            const M3& pr = r.bind[parent].R;
            Bone b;
            b.R = Mul(lo, pr);
            b.p = Add(r.bind[parent].p, Apply(t, pr));
            chain.push_back((int)r.bind.size());
            r.bind.push_back(b);
            r.parent.push_back(parent);
        }
        r.chains.push_back(chain);
    }
    r.wrist = 0; r.ind = r.chains[0][0]; r.mid = r.chains[1][0]; r.pnk = r.chains[3][0];
    // the pose: the same chain with each bone's local turned by a curl about its own x (and a little about z)
    r.pose = r.bind;
    for (size_t i = 1; i < r.bind.size(); ++i) {
        const int p = r.parent[i];
        const M3 lb = Mul(r.bind[i].R, Transpose(r.bind[p].R));   // the bind's local rotation
        const V tb = Apply(Sub(r.bind[i].p, r.bind[p].p), Transpose(r.bind[p].R));   // its place in the parent's frame
        const int j = (int)((i - 1) % 3);
        const float curl = curled ? (0.5f + 0.4f * j + 0.2f * u(rng)) : 0.0f;
        const M3 lp = Mul(Mul(RotX(curl), RotZ(curled ? 0.15f * u(rng) : 0.0f)), lb);
        r.pose[i].R = Mul(lp, r.pose[p].R);
        r.pose[i].p = Add(r.pose[p].p, Apply(tb, r.pose[p].R));
    }
    return r;
}

// The mirror image as a left hand: positions reflected across x = 0, the bones' axes reflected (a mirrored bone) and relabelled by Q0 (a rig's own habit: x and z swapped, y turned over).
static M3 Mx() { M3 r; r.m[0] = -1.0f; return r; }
static M3 Q0() { M3 q; q.m[0] = 0; q.m[1] = 0; q.m[2] = 1; q.m[3] = 0; q.m[4] = -1; q.m[5] = 0; q.m[6] = 1; q.m[7] = 0; q.m[8] = 0; return q; }
static Rig Mirror(const Rig& a, bool relabel) {
    Rig m = a;
    auto reflect = [&](std::vector<Bone>& bones) {
        for (Bone& b : bones) {
            b.p = Apply(b.p, Mx());
            b.R = Mul(relabel ? Q0() : M3{}, Mul(b.R, Mx()));
        }
    };
    reflect(m.bind);
    reflect(m.pose);
    return m;
}
// A rig moved in the world: every bone through the rigid map (R g, then + t)
static std::vector<Bone> Placed(const std::vector<Bone>& bones, const M3& g, V t) {
    std::vector<Bone> out = bones;
    for (Bone& b : out) { b.R = Mul(b.R, g); b.p = Add(Apply(b.p, g), t); }
    return out;
}
static bool FrameOfBones(const std::vector<Bone>& b, const Rig& r, int side, M3* e) { return HandFrame(b[r.wrist].p, b[r.mid].p, b[r.ind].p, b[r.pnk].p, side, e); }

int main() {
    // ---- the frames of the two hands are mirror images ----
    {
        const Rig r = RightHand(1, true);
        const Rig l = Mirror(r, true);
        M3 er, el;
        Expect(FrameOfBones(r.pose, r, 0, &er) && FrameOfBones(l.pose, l, 1, &el), "frames");
        Expect(MaxDiff(el, Mul(er, Mx())) < 1e-4f, "the left hand's frame is the right's reflected", MaxDiff(el, Mul(er, Mx())));
        Expect(Det(er) < 0.0f && Det(el) > 0.0f, "(the right hand's frame is the left-handed triple, the left's the right-handed)");
    }
    // ---- the pose crosses: every bone of the chain, thumb included, either way round, with the rig's relabelling and without ----
    for (const bool relabel : {false, true})
        for (unsigned seed = 1; seed <= 8; ++seed) {
            const Rig right = RightHand(seed, true);
            const Rig leftBind = Mirror(RightHand(seed, false), relabel);   // the left hand's own bind (what it is made of)
            const Rig leftTrue = Mirror(right, relabel);                    // the left hand as the game would pose it in the mirror image (the truth)
            // the taking hand is put anywhere: a random turn and place
            std::mt19937 rng(seed * 7919u);
            std::uniform_real_distribution<float> u(-1.0f, 1.0f);
            const M3 g = Mul(Mul(RotX(3.0f * u(rng)), RotY(3.0f * u(rng))), RotZ(3.0f * u(rng)));
            const V tg{u(rng), u(rng), u(rng)};
            const std::vector<Bone> takerOpen = Placed(leftBind.bind, g, tg);   // its own bind, where it stands now
            M3 ePoseBase, eBindBase, eBindTaker, ePoseTaker;
            Expect(FrameOfBones(right.pose, right, 0, &ePoseBase) && FrameOfBones(right.bind, right, 0, &eBindBase) && FrameOfBones(leftBind.bind, leftBind, 1, &eBindTaker) &&
                   FrameOfBones(takerOpen, leftBind, 1, &ePoseTaker), "the frames are made");
            // the taker's chain, built as the hook builds it
            std::vector<Bone> built = takerOpen;
            for (size_t c = 0; c < right.chains.size(); ++c)
                for (size_t j = 0; j < right.chains[c].size(); ++j) {
                    const int bb = right.chains[c][j], bt = leftBind.chains[c][j];
                    const M3 aB = Coords(right.bind[bb].R, eBindBase), aT = Coords(leftBind.bind[bt].R, eBindTaker);
                    const M3 p = Relabel(aB, aT);
                    const M3 cPose = Coords(right.pose[bb].R, ePoseBase);
                    const M3 rT = TakerOrientation(cPose, p, ePoseTaker);
                    const int par = leftBind.parent[bt];
                    const M3& rParent = built[par].R;   // (the chain's parent: the wrist at the taker's place, or the bone just built)
                    const M3 lt = Local(rT, rParent);
                    const V tBind = Apply(Sub(leftBind.bind[bt].p, leftBind.bind[par].p), Transpose(leftBind.bind[par].R));   // its own bind local offset
                    built[bt].R = Mul(lt, rParent);
                    built[bt].p = Add(built[par].p, Apply(tBind, rParent));
                    Expect(std::fabs(Det(lt) - 1.0f) < 1e-3f, "a local rotation is a proper one (the mirror cancels)", Det(lt));
                    // against the truth: the left hand as the mirror image poses it, put where the taker stands
                    const Bone want = {Mul(leftTrue.pose[bt].R, g), Add(Apply(leftTrue.pose[bt].p, g), tg)};
                    // (the truth is a mirror of the base's pose in the base's own place: compare in the hand's own coordinates, so the taker's place does not matter)
                    const V wantC = Apply(Sub(leftTrue.pose[bt].p, leftTrue.pose[leftBind.wrist].p), Transpose(M3{}));
                    (void)want; (void)wantC;
                    // in hand coordinates: the bone's place from the wrist, in the frame
                    const V gotC = Apply(Sub(built[bt].p, built[0].p), Transpose(ePoseTaker));
                    const V baseC = Apply(Sub(right.pose[bb].p, right.pose[0].p), Transpose(ePoseBase));
                    Expect(Near(gotC, baseC, 2e-4f), "the bone stands where the base hand's does, in the hand's own frame", Len(Sub(gotC, baseC)));
                    // the bone's axes in the frame: the relabelled pose of the base (the mirror image of the base's axes)
                    const M3 cGot = Coords(built[bt].R, ePoseTaker);
                    Expect(MaxDiff(cGot, Mul(p, cPose)) < 1e-3f, "the bone's axes in the hand's frame are the base's, relabelled", MaxDiff(cGot, Mul(p, cPose)));
                    // against the truth in the world: the mirrored pose's bone, in the hand's own frame (both sides in coordinates)
                    M3 eTrue;
                    FrameOfBones(leftTrue.pose, leftTrue, 1, &eTrue);
                    const M3 cTrue = Coords(leftTrue.pose[bt].R, eTrue);
                    Expect(MaxDiff(cGot, cTrue) < 1e-3f, "... and the mirror image of the game's pose is just that", MaxDiff(cGot, cTrue));
                }
        }
    // ---- an open pose (the base's own bind) crosses to the taker's own bind ----
    {
        const Rig right = RightHand(3, false);
        const Rig left = Mirror(right, true);
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const M3 g = Mul(RotX(2.0f * u(rng)), RotZ(2.0f * u(rng)));
        const std::vector<Bone> open = Placed(left.bind, g, {0.3f, 0.1f, -0.2f});
        M3 eBb, eBt, ePt;
        FrameOfBones(right.bind, right, 0, &eBb);
        FrameOfBones(left.bind, left, 1, &eBt);
        FrameOfBones(open, left, 1, &ePt);
        float worst = 0.0f;
        for (size_t c = 0; c < right.chains.size(); ++c)
            for (size_t j = 0; j < right.chains[c].size(); ++j) {
                const int bb = right.chains[c][j], bt = left.chains[c][j];
                const M3 p = Relabel(Coords(right.bind[bb].R, eBb), Coords(left.bind[bt].R, eBt));
                const M3 rT = TakerOrientation(Coords(right.bind[bb].R, eBb), p, ePt);
                worst = std::fmax(worst, MaxDiff(rT, open[bt].R));
            }
        Expect(worst < 1e-3f, "an open hand crosses to the taker's own open hand", worst);
    }
    // ---- which finger answers which ----
    {
        std::vector<int> out;
        const std::vector<FingerShape> base = {{0, false, 3}, {1, false, 3}, {2, false, 3}, {3, false, 3}, {4, true, 3}};
        std::vector<FingerShape> taker = {{4, true, 3}, {3, false, 3}, {2, false, 3}, {1, false, 3}, {0, false, 3}};   // the other way round, named
        Expect(MatchFingers(base, taker, &out) && out[0] == 4 && out[1] == 3 && out[4] == 0, "fingers are matched by their names when the rig has them");
        const std::vector<FingerShape> unnamed = {{-1, false, 3}, {-1, false, 3}, {-1, false, 3}, {-1, false, 3}, {-1, true, 3}};
        Expect(MatchFingers(unnamed, unnamed, &out) && out[2] == 2, "unnamed ones by their place");
        std::vector<FingerShape> shorter = unnamed;
        shorter.pop_back();
        Expect(!MatchFingers(unnamed, shorter, &out), "a hand with a finger fewer is not matched");
        std::vector<FingerShape> other = unnamed;
        other[4] = {-1, false, 3};
        Expect(!MatchFingers(unnamed, other, &out), "a thumb that is not a thumb is not matched");
        std::vector<FingerShape> longer = unnamed;
        longer[1].bones = 4;
        Expect(!MatchFingers(unnamed, longer, &out), "a finger of other length is not matched");
        Expect(!MatchFingers({}, {}, &out), "no fingers: no match");
    }
    // ---- no frame from a hand whose bones give none ----
    {
        M3 e;
        Expect(!HandFrame({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}, 0, &e), "the middle knuckle on the wrist: no frame");
        Expect(!HandFrame({0, 0, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, 0, &e), "no knuckle row: no frame");
        Expect(!HandFrame({0, 0, 0}, {0, -1, 0}, {0, -2, 0}, {0, -1, 0}, 1, &e), "a knuckle row along the fingers: no frame");
        float m[16] = {0};
        M3 r;
        Expect(!Orientation(m, &r), "a bone with no axes has no orientation");
    }
    if (g_failures == 0) std::printf("grip_pose_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
