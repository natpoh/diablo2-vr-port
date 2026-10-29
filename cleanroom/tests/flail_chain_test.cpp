// A flail's chain swinging free (vr/flail_chain.h), without the game.
//
//   flail_chain_test
//
// The three flail skeletons as the game has them (flail_chain_bones.inc): which bones are chains, their rigs; the simulation: the chain never
// stretches, a free swing loses energy and never gains it, the head settles under the anchor wherever the hand takes it, the period is a
// pendulum's, "swing 0" carries it stiffly, a hit turns the head back, it stays numbers under any shaking and at any frame rate; the pose:
// rest = the game's, a hanging chain points its links along itself, only the chain's bones are written; the gate: only a flail, only the hero's,
// only ticked in F4.

#include "flail_chain.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
#include <vector>

using namespace flailchain;

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

struct BoneRow { const char* name; int parent; float bind[16]; };
#include "flail_chain_bones.inc"

struct Skel {
    std::vector<std::string_view> names;
    std::vector<int> parents;
    std::vector<Mat> bind;
};
template <size_t N> Skel Load(const BoneRow (&rows)[N]) {
    Skel s;
    for (const BoneRow& r : rows) {
        s.names.push_back(r.name);
        s.parents.push_back(r.parent);
        Mat m;
        std::memcpy(m.m, r.bind, sizeof m.m);
        s.bind.push_back(m);
    }
    return s;
}
int Find(const Skel& s, const char* name) {
    for (size_t i = 0; i < s.names.size(); ++i) if (s.names[i] == name) return (int)i;
    return -1;
}
float Dist(V3 a, V3 b) { return Length(a - b); }
bool Near(const Mat& a, const Mat& b, float tol) {
    for (int i = 0; i < 16; ++i) if (std::fabs(a.m[i] - b.m[i]) > tol) return false;
    return true;
}

// ---- which models, which bones ----

void TestModels() {
    std::printf("models\n");
    EXPECT(IsFlailModel("data/hd/items/weapon/mace/flail/skeleton/flail.skeleton"), "the Flail");
    EXPECT(IsFlailModel("data/hd/items/weapon/mace/flail_lh/skeleton/flail_lh.skeleton"), "the left hand's");
    EXPECT(IsFlailModel("data:data\\hd\\items\\weapon\\mace\\khalim_flail\\khalim_flail_lod0.model"), "Khalim's, backslashes");
    EXPECT(IsFlailModel("data/hd/items/weapon/mace/super_khalim_flail/skeleton/super_khalim_flail.skeleton"), "Khalim's Will");
    EXPECT(IsFlailModel("DATA/HD/ITEMS/WEAPON/MACE/FLAIL/FLAIL.SKELETON"), "any case");
    EXPECT(!IsFlailModel("data/hd/items/weapon/mace/mace/skeleton/mace.skeleton"), "a mace");
    EXPECT(!IsFlailModel("data/hd/items/weapon/mace/morning_star/skeleton/morning_star.skeleton"), "a morning star");
    EXPECT(!IsFlailModel("data/hd/items/weapon/mace/_monsters/skeleton_flail/skeleton/skeleton_flail.skeleton"), "a skeleton's (monster)");
    EXPECT(!IsFlailModel("data/hd/items/weapon/mace/_monsters/corrupt_rogue_flail/skeleton/corrupt_rogue_flail.skeleton"), "a rogue's (monster)");
    EXPECT(!IsFlailModel("data/hd/items/dropped_items/dropped_items_flail_flip_nw"), "one lying on the ground");
    EXPECT(!IsFlailModel("data/hd/items/weapon/spear/razortine/skeleton/razortine.skeleton"), "Razortine's chains are not a flail's");
    EXPECT(!IsFlailModel("data/hd/character/player/paladin/flail"), "not an item");
    EXPECT(!IsFlailModel(""), "nothing");
}

void TestChains() {
    std::printf("chains\n");
    {
        const Skel s = Load(kFlail);
        Layout l;
        const char* why = "x";
        EXPECT(FindChains(s.names.data(), s.parents.data(), (int)s.names.size(), &l, &why), "the Flail's: %s", why ? why : "");
        EXPECT(l.chains == 1, "one chain, %d", l.chains);
        EXPECT(l.chain[0].count == 18, "of 18 bones, %d", l.chain[0].count);
        EXPECT(l.chain[0].bone[0] == Find(s, "chain_bnd_jnt_01") && l.chain[0].bone[17] == Find(s, "chain_bnd_jnt_18"), "jnt_01 .. jnt_18");
        for (int k = 1; k < 18; ++k) EXPECT(s.parents[l.chain[0].bone[k]] == l.chain[0].bone[k - 1], "each the child of the one before (%d)", k);
        ChainRig r;
        EXPECT(Learn(l.chain[0], s.parents.data(), s.bind.data(), (int)s.names.size(), &r, &why), "learned: %s", why ? why : "");
        EXPECT(r.parent == Find(s, "cog"), "hangs off the cog");
        EXPECT(r.particles == 18, "18 places, %d", r.particles);
        EXPECT(std::fabs(r.bindLength - (2.9394f - 1.4166f)) < 1e-3f, "1.52 long, %.4f", r.bindLength);
    }
    for (int which = 0; which < 2; ++which) {
        const Skel s = which == 0 ? Load(kKhalim) : Load(kSuperKhalim);
        const char* name = which == 0 ? "Khalim's Flail" : "Khalim's Will";
        Layout l;
        const char* why = "x";
        EXPECT(FindChains(s.names.data(), s.parents.data(), (int)s.names.size(), &l, &why), "%s: %s", name, why ? why : "");
        EXPECT(l.chains == 3, "%s: three chains, %d", name, l.chains);
        bool sides[3] = {};
        for (int c = 0; c < l.chains; ++c) {
            const std::string_view first = s.names[l.chain[c].bone[0]];
            EXPECT(first.size() > 2 && first.substr(1) == "_chain_root", "%s: chain %d starts at a root, %.*s", name, c, (int)first.size(), first.data());
            const char side = first.empty() ? '?' : first[0];
            if (side == 'L') sides[0] = true;
            if (side == 'C') sides[1] = true;
            if (side == 'R') sides[2] = true;
            for (int k = 0; k < l.chain[c].count; ++k) {
                const std::string_view b = s.names[l.chain[c].bone[k]];
                EXPECT(!b.empty() && b[0] == side, "%s: chain %c holds only its own bones (%.*s)", name, side, (int)b.size(), b.data());
            }
            ChainRig r;
            EXPECT(Learn(l.chain[c], s.parents.data(), s.bind.data(), (int)s.names.size(), &r, &why), "%s: learned: %s", name, why ? why : "");
            EXPECT(r.particles == l.chain[c].count - 1, "%s: the root and its first link share one place (%d of %d)", name, r.particles, l.chain[c].count);
            EXPECT(r.part[0] == 0 && r.part[1] == 0 && r.part[2] == 1, "%s: root, 01 -> 0, 02 -> 1", name);
        }
        EXPECT(sides[0] && sides[1] && sides[2], "%s: L, C and R", name);
        EXPECT(Find(s, "mainChain") >= 0, "%s has a mainChain (a line of one: not a chain)", name);
        for (int c = 0; c < l.chains; ++c) EXPECT(l.chain[c].bone[0] != Find(s, "mainChain"), "%s: mainChain is no chain", name);
    }
    {   // a club: root > translate > anim > cog > alt_cog
        const std::string_view names[] = {"root", "translate", "anim", "cog", "alt_cog"};
        const int parents[] = {-1, 0, 1, 2, 3};
        Layout l;
        const char* why = nullptr;
        EXPECT(!FindChains(names, parents, 5, &l, &why) && why, "a club has none");
    }
    {   // a line of two chain bones is not a chain; a broken parent refuses the skeleton
        const std::string_view names[] = {"root", "cog", "chain_a", "chain_b"};
        const int parents[] = {-1, 0, 1, 2};
        Layout l;
        EXPECT(!FindChains(names, parents, 4, &l, nullptr), "two bones are no chain");
        const int bad[] = {-1, 0, 7, 2};
        EXPECT(!FindChains(names, bad, 4, &l, nullptr), "a parent out of the skeleton");
    }
    {   // chains hanging off another chain's last bone are left out (that bone is ours to move); a line through a fork ends at it
        const std::string_view names[] = {"root", "cog", "a_chain_1", "a_chain_2", "a_chain_3", "b_chain_1", "b_chain_2", "b_chain_3", "c_chain_1", "c_chain_2", "c_chain_3"};
        const int parents[] = {-1, 0, 1, 2, 3, 4, 5, 6, 4, 8, 9};   // b and c both hang off a_chain_3
        Layout l;
        EXPECT(FindChains(names, parents, 11, &l, nullptr), "a forked chain");
        EXPECT(l.chains == 1 && l.chain[0].count == 3 && l.chain[0].bone[0] == 2, "only a: %d chains, the first at %d of %d", l.chains, l.chain[0].bone[0], l.chain[0].count);
    }
}

// The Flail's rig, learned (every test below uses it).
ChainRig FlailRig(Skel* out = nullptr) {
    const Skel s = Load(kFlail);
    Layout l;
    FindChains(s.names.data(), s.parents.data(), (int)s.names.size(), &l, nullptr);
    ChainRig r;
    Learn(l.chain[0], s.parents.data(), s.bind.data(), (int)s.names.size(), &r, nullptr);
    if (out) *out = s;
    return r;
}

Mat Turn(V3 axis, float rad, V3 at, float scale = 1.0f) {   // a rotation about axis (row-vector), scaled, then moved to at
    axis = Unit(axis);
    Mat m = Identity();
    const V3 ex{1, 0, 0}, ey{0, 1, 0}, ez{0, 0, 1};
    const V3 rows[3] = {ex, ey, ez};
    for (int r = 0; r < 3; ++r) {
        const V3 v = rows[r];
        const float c = std::cos(rad), s = std::sin(rad);
        const V3 t = v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1.0f - c));
        SetRow(m, r, t * scale);
    }
    SetPosition(m, at);
    return m;
}

void TestPose() {
    std::printf("pose\n");
    Skel s;
    const ChainRig r = FlailRig(&s);
    const int cog = Find(s, "cog");
    // the rigid chain on the bind cog is the bind pose; on a turned, moved, scaled cog it is the bind pose carried by it
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int t = 0; t < 50; ++t) {
        const Mat cogPose = t == 0 ? s.bind[cog] : Turn({u(rng), u(rng), u(rng)}, 3.0f * u(rng), {5 * u(rng), 5 * u(rng), 5 * u(rng)}, 1.0f + 0.2f * u(rng));
        Mat rigid[kMaxLinks];
        Rigid(r, cogPose, rigid);
        bool ok = true;
        for (int k = 0; k < r.bones; ++k) ok = ok && Near(rigid[k], Mul(s.bind[r.bone[k]], cogPose), 2e-4f);
        EXPECT(ok, "rigid = bind carried by the cog (%d)", t);
        // the particles where the rigid chain has them: the pose is the rigid one exactly
        V3 places[kMaxLinks];
        RigidPlaces(r, rigid, places);
        Mat out[kMaxLinks];
        PoseBones(r, rigid, places, out);
        ok = true;
        for (int k = 0; k < r.bones; ++k) ok = ok && Near(out[k], rigid[k], 2e-4f);
        EXPECT(ok, "at rest the pose is the game's (%d)", t);
    }
    // the chain laid along -X from the anchor (bind: up +Y): every link's axis along the chain (the flail's links point down their +X in
    // the bind pose: row 0 = (0 1 0)), their size kept, the frames still square
    {
        Mat rigid[kMaxLinks];
        Rigid(r, s.bind[cog], rigid);
        V3 places[kMaxLinks];
        RigidPlaces(r, rigid, places);
        const V3 a = places[0];
        float along = 0.0f;
        V3 rest[kMaxLinks];
        RigidPlaces(r, rigid, rest);
        for (int i = 1; i < r.particles; ++i) { along += Length(rest[i] - rest[i - 1]); places[i] = a + V3{-along, 0, 0}; }
        Mat out[kMaxLinks];
        PoseBones(r, rigid, places, out);
        bool ok = true;
        for (int k = 0; k < r.bones; ++k) {
            ok = ok && Dist(Unit(Row(out[k], 0)), {-1, 0, 0}) < 1e-4f && std::fabs(Length(Row(out[k], 1)) - 1.0f) < 1e-4f &&
                 std::fabs(Dot(Row(out[k], 0), Row(out[k], 1))) < 1e-4f && Dist(Position(out[k]), places[r.part[k]]) < 1e-5f;
        }
        EXPECT(ok, "a chain laid along -X: each link points along it, square, at its place");
        // and hung straight down (the half turn from bind: up -> down)
        along = 0.0f;
        for (int i = 1; i < r.particles; ++i) { along += Length(rest[i] - rest[i - 1]); places[i] = a + V3{0, -along, 0}; }
        PoseBones(r, rigid, places, out);
        ok = true;
        for (int k = 0; k < r.bones; ++k) ok = ok && Dist(Unit(Row(out[k], 0)), {0, -1, 0}) < 1e-4f && std::fabs(Length(Row(out[k], 2)) - 1.0f) < 1e-4f && FiniteMat(out[k]);
        EXPECT(ok, "hung straight down (opposite the bind): each link points down, finite");
    }
    // Drive writes the chain's bones and nothing else
    {
        std::vector<Mat> pose = s.bind;
        for (Mat& m : pose) m = Mul(m, Turn({0, 0, 1}, 1.2f, {3, 4, 5}));   // the game's pose: all of it turned (the hand held out)
        const std::vector<Mat> before = pose;
        Sim sim;
        Params p;
        const Mat toSim = Identity(), fromSim = Identity();
        Drive(r, sim, toSim, fromSim, 0.0f, {0, -9.81f, 0}, 1.0f, false, p, pose.data());
        bool same = true;
        for (size_t i = 0; i < pose.size(); ++i) same = same && Near(pose[i], before[i], 1e-4f);
        EXPECT(same, "the first frame: as the game drew it");
        for (int f = 0; f < 90 * 12; ++f) Drive(r, sim, toSim, fromSim, 1.0f / 90.0f, {0, -9.81f, 0}, 1.0f, false, p, pose.data());
        bool others = true, moved = false;
        for (size_t i = 0; i < pose.size(); ++i) {
            bool chain = false;
            for (int k = 0; k < r.bones; ++k) chain = chain || r.bone[k] == (int)i;
            if (!chain) others = others && std::memcmp(pose[i].m, before[i].m, sizeof pose[i].m) == 0;
            else moved = moved || !Near(pose[i], before[i], 1e-3f);
        }
        EXPECT(others, "no bone but the chain's is written");
        EXPECT(moved, "the chain fell from the game's pose");
        EXPECT(Dist(Position(pose[r.bone[0]]), Position(before[r.bone[0]])) < 1e-5f, "the first link stays on the handle");
        const V3 head = Position(pose[r.bone[r.bones - 1]]), top = Position(pose[r.bone[0]]);
        EXPECT((head - top).y < -0.98f * r.bindLength, "it hangs down after 12 s (%.3f of %.3f)", (head - top).y, r.bindLength);
    }
}

// ---- the simulation ----

Sim HangingSim(const ChainRig& r, V3 anchor, V3 dir, const Params& p) {   // the Flail's chain laid out from the anchor along dir, at rest
    Skel s = Load(kFlail);
    Mat rigid[kMaxLinks];
    Rigid(r, s.bind[Find(s, "cog")], rigid);
    V3 rest[kMaxLinks], pts[kMaxLinks];
    RigidPlaces(r, rigid, rest);
    float along = 0.0f;
    pts[0] = anchor;
    for (int i = 1; i < r.particles; ++i) { along += Length(rest[i] - rest[i - 1]); pts[i] = anchor + Unit(dir) * along; }
    (void)p;
    Sim sim;
    sim.Start(pts, r.particles);
    return sim;
}
float MaxStretch(const Sim& s) {
    float worst = 0.0f;
    for (int i = 0; i + 1 < s.n; ++i) worst = std::max(worst, std::fabs(Length(s.x[i + 1] - s.x[i]) - s.len[i]) / std::max(1e-6f, s.len[i]));
    return worst;
}

void TestSim() {
    std::printf("simulation\n");
    const ChainRig r = FlailRig();
    const V3 g{0, -9.81f, 0};
    const V3 a{0, 2, 0};
    // a free swing from 60 degrees: never gains energy; without damping it keeps most of it a while; with damping it dies away
    for (int damped = 0; damped < 2; ++damped) {
        Params p;
        p.damping = damped ? 0.6f : 0.0f;
        p.linkDamping = damped ? p.linkDamping : 0.0f;
        Sim s = HangingSim(r, a, {std::sin(1.047f), -std::cos(1.047f), 0}, p);
        const float e0 = s.Energy(g);
        float worst = 0.0f, eMax = e0, stretch = 0.0f;
        float e2 = e0, e10 = e0;
        for (int f = 1; f <= 900; ++f) {
            s.Step(a, a, 1.0f / 90.0f, g, p);
            const float e = s.Energy(g);
            eMax = std::max(eMax, e);
            worst = std::max(worst, e - e0);
            stretch = std::max(stretch, MaxStretch(s));
            if (f == 180) e2 = e;
            if (f == 900) e10 = e;
        }
        // energy above the lowest (hanging straight down)
        Sim low = HangingSim(r, a, {0, -1, 0}, p);
        const float eLow = low.Energy(g), span = e0 - eLow;
        EXPECT(worst <= 1e-3f * span, "%s: never gains energy (+%.5f of %.5f)", damped ? "damped" : "undamped", worst, span);
        EXPECT(stretch <= 1.0f - kTaut + 1e-4f, "%s: never stretches (%.2e)", damped ? "damped" : "undamped", stretch);
        if (damped) {
            EXPECT(e10 - eLow < 0.02f * span, "damped: 10 s later it has nearly stopped (%.4f of %.4f left)", e10 - eLow, span);
            EXPECT(e2 - eLow < 0.6f * span && e2 - eLow > 0.05f * span, "damped: 2 s later it still swings, smaller (%.4f of %.4f)", e2 - eLow, span);
        } else {
            EXPECT(e2 - eLow > 0.5f * span, "undamped: 2 s later most of the swing is there (%.4f of %.4f)", e2 - eLow, span);
        }
    }
    // the period of a small swing: a pendulum's of the chain's length (the head's mass dominates), 2 pi sqrt(L / g)
    {
        Params p;
        p.damping = 0.0f;
        Sim s = HangingSim(r, a, {std::sin(0.15f), -std::cos(0.15f), 0}, p);
        const float L = s.Total();
        const float expect = 6.2832f * std::sqrt(L / 9.81f);
        float lastX = s.x[s.n - 1].x, t = 0.0f, firstCross = -1.0f, thirdCross = -1.0f;
        int crossings = 0;
        const float h = 1.0f / 240.0f;
        for (int f = 0; f < 240 * 10 && thirdCross < 0.0f; ++f) {
            s.Step(a, a, h, g, p);
            t += h;
            const float x = s.x[s.n - 1].x - a.x;
            if ((lastX - a.x) > 0.0f && x <= 0.0f) {
                ++crossings;
                if (crossings == 1) firstCross = t;
                if (crossings == 3) thirdCross = t;
            }
            lastX = s.x[s.n - 1].x;
        }
        const float period = (thirdCross - firstCross) / 2.0f;
        EXPECT(thirdCross > 0.0f && std::fabs(period - expect) < 0.12f * expect, "a small swing's period %.3f s, a pendulum's %.3f s", period, expect);
    }
    // the hand carries it: moved 1 unit sideways and held, the head settles under the new place
    {
        Params p;
        Sim s = HangingSim(r, a, {0, -1, 0}, p);
        V3 hand = a;
        for (int f = 0; f < 45; ++f) { const V3 next = a + V3{1.0f * (f + 1) / 45.0f, 0, 0}; s.Step(hand, next, 1.0f / 90.0f, g, p); hand = next; }
        float swungTo = 0.0f;
        for (int f = 0; f < 90 * 20; ++f) { s.Step(hand, hand, 1.0f / 90.0f, g, p); swungTo = std::max(swungTo, std::fabs(s.x[s.n - 1].x - hand.x)); }
        const V3 head = s.x[s.n - 1];
        EXPECT(std::fabs(head.x - hand.x) < 0.01f && std::fabs(head.z - hand.z) < 0.01f, "it settles under the hand (off %.4f %.4f)", head.x - hand.x, head.z - hand.z);
        EXPECT(std::fabs((hand.y - head.y) - s.Total()) < 0.01f, "hanging its whole length (%.3f of %.3f)", hand.y - head.y, s.Total());
        EXPECT(swungTo > 0.05f, "and it swung on the way (%.3f)", swungTo);
        EXPECT(s.x[0].x == hand.x && s.x[0].y == hand.y, "the anchor is the hand");
    }
    // swing 0: the hand's motion is not felt - a hanging chain is carried stiffly
    {
        Params p;
        p.swing = 0.0f;
        Sim s = HangingSim(r, a, {0, -1, 0}, p);
        V3 hand = a;
        float worst = 0.0f;
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        for (int f = 0; f < 600; ++f) {
            const V3 next = hand + V3{u(rng), u(rng), u(rng)} * 0.05f;
            s.Step(hand, next, 1.0f / 90.0f, g, p);
            hand = next;
            worst = std::max(worst, Dist(s.x[s.n - 1], hand + V3{0, -s.Total(), 0}));
        }
        EXPECT(worst < 1e-3f, "swing 0: still hanging under the hand (off %.5f)", worst);
    }
    // swing 1 under a steady push: the chain leans back by atan(a / g), as a pendulum in a car does
    for (float damping : {0.6f, 3.0f}) {
        Params p;
        p.damping = damping;
        Sim s = HangingSim(r, a, {0, -1, 0}, p);
        V3 hand = a, vel{};
        const float acc = 9.81f * std::tan(0.5236f);   // 30 degrees
        const float h = 1.0f / 90.0f;
        for (int f = 0; f < 90 * 16; ++f) {
            vel = vel + V3{acc * h, 0, 0};
            const V3 next = hand + vel * h;
            s.Step(hand, next, h, g, p);
            s.Shift(a - next);   // the origin kept at the hand (as the hook keeps it near the hero): small numbers
            hand = a;
        }
        const V3 d = s.x[s.n - 1] - hand;
        const float lean = std::atan2(-d.x, -d.y) * 57.2958f;
        EXPECT(std::fabs(lean - 30.0f) < 2.0f, "swing 1, damping %.1f: a steady push leans it back 30 deg (%.1f) - the damping is the links', not the air's", damping, lean);
        Params q = p;
        q.swing = 0.5f;
        Sim s2 = HangingSim(r, a, {0, -1, 0}, q);
        hand = a; vel = {};
        for (int f = 0; f < 90 * 16; ++f) { vel = vel + V3{acc * h, 0, 0}; const V3 next = hand + vel * h; s2.Step(hand, next, h, g, q); s2.Shift(a - next); hand = a; }
        const V3 d2 = s2.x[s2.n - 1] - hand;
        const float lean2 = std::atan2(-d2.x, -d2.y) * 57.2958f;
        EXPECT(std::fabs(std::tan(lean2 / 57.2958f) - 0.5f * std::tan(0.5236f)) < 0.03f, "swing 0.5: half the push felt (%.1f deg)", lean2);
    }
    // a hit: the head's motion against the hand is turned back, by Params::hit
    {
        Params p;
        p.damping = 0.0f;
        Sim s = HangingSim(r, a, {1, 0, 0}, p);   // held out level, then let fall: it swings down fast
        for (int f = 0; f < 30; ++f) s.Step(a, a, 1.0f / 90.0f, g, p);
        const V3 before = s.v[s.n - 1];
        Sim kicked = s;
        kicked.Kick(p);
        const V3 after = kicked.v[kicked.n - 1];
        EXPECT(Dot(before, after) < 0.0f, "the head goes back (%.2f %.2f -> %.2f %.2f)", before.x, before.y, after.x, after.y);
        EXPECT(std::fabs(Length(after) - p.restitution * Length(before)) < 1e-3f * Length(before), "at %.0f%% of its speed (%.3f of %.3f)", p.restitution * 100, Length(after), Length(before));
        Params none = p;
        none.hit = 0.0f;
        Sim off = s;
        off.Kick(none);
        EXPECT(Dist(off.v[off.n - 1], before) < 1e-6f, "hit 0: nothing happens");
        // the kick is against the hand: a hand moving with the chain kicks nothing
        V3 hand = a;
        Params stiff = p;
        stiff.swing = 0.0f;
        Sim carried = HangingSim(r, a, {0, -1, 0}, stiff);
        hand = a;
        for (int f = 0; f < 90; ++f) { const V3 next = hand + V3{0.02f, 0, 0}; carried.Step(hand, next, 1.0f / 90.0f, g, stiff); hand = next; }
        const V3 vb = carried.v[carried.n - 1];
        carried.Kick(stiff);
        EXPECT(Dist(carried.v[carried.n - 1], carried.anchorVel + (vb - carried.anchorVel) * (1.0f - (1.0f + stiff.restitution))) < 1e-5f, "against the hand's velocity");
        // and the swing after a hit goes on, numbers, the lengths held
        for (int f = 0; f < 300; ++f) kicked.Step(a, a, 1.0f / 90.0f, g, p);
        EXPECT(MaxStretch(kicked) < 0.015f && Finite(kicked.x[kicked.n - 1]), "after the hit: still a chain");
    }
    // shaken hard at any frame rate: numbers, no stretch, a bounded speed
    for (int rate : {30, 45, 72, 90, 120, 144, 240}) {
        Params p;
        Sim s = HangingSim(r, a, {0, -1, 0}, p);
        V3 hand = a;
        float t = 0.0f, worstStretch = 0.0f, worstSpeed = 0.0f;
        const float h = 1.0f / rate;
        bool finite = true;
        for (int f = 0; f < rate * 30; ++f) {
            t += h;
            const V3 next = a + V3{0.3f * std::sin(t * 31.4f), 0.4f * std::sin(t * 23.0f + 1.0f), 0.3f * std::cos(t * 17.0f)};
            s.Step(hand, next, h, g, p);
            if (f % 97 == 0) s.Kick(p);
            hand = next;
            for (int i = 0; i < s.n; ++i) finite = finite && Finite(s.x[i]) && Finite(s.v[i]);
            worstStretch = std::max(worstStretch, MaxStretch(s));
            worstSpeed = std::max(worstSpeed, Length(s.v[s.n - 1]));
        }
        EXPECT(finite && s.live, "%d fps shaken 30 s: numbers", rate);
        EXPECT(worstStretch < 0.015f, "%d fps: a link never more than 1.5 %% off its length (%.2e)", rate, worstStretch);
        EXPECT(worstSpeed < 300.0f, "%d fps: no runaway (the head's top speed %.1f, the hand's about 14)", rate, worstSpeed);
    }
    // the frame rate does not change the motion: the same hand motion at 45 and 144 fps ends nearly the same
    {
        V3 heads[2];
        int idx = 0;
        for (int rate : {45, 144}) {
            Params p;
            Sim s = HangingSim(r, a, {0, -1, 0}, p);
            V3 hand = a;
            const float h = 1.0f / rate;
            float t = 0.0f;
            for (int f = 0; f < rate * 3; ++f) {
                t += h;
                const V3 next = a + V3{0.5f * std::sin(t * 4.0f), 0.0f, 0.0f};
                s.Step(hand, next, h, g, p);
                hand = next;
            }
            heads[idx++] = s.x[s.n - 1];
        }
        EXPECT(Dist(heads[0], heads[1]) < 0.05f, "45 vs 144 fps: %.4f apart", Dist(heads[0], heads[1]));
    }
    // a whole frame's pause or a sudden jump starts it again where the game has it
    {
        Skel s;
        const ChainRig rr = FlailRig(&s);
        std::vector<Mat> pose = s.bind;
        Sim sim;
        Params p;
        Done d = Drive(rr, sim, Identity(), Identity(), 0.0f, g, 1.0f, false, p, pose.data());
        EXPECT(d == Done::Started, "the first frame starts it");
        d = Drive(rr, sim, Identity(), Identity(), 0.011f, g, 1.0f, false, p, pose.data());
        EXPECT(d == Done::Stepped, "the next steps it");
        d = Drive(rr, sim, Identity(), Identity(), 0.0001f, g, 1.0f, false, p, pose.data());
        EXPECT(d == Done::Held, "the same frame again steps nothing");
        d = Drive(rr, sim, Identity(), Identity(), 0.4f, g, 1.0f, false, p, pose.data());
        EXPECT(d == Done::Started, "after a pause it starts again");
        pose = s.bind;
        for (Mat& m : pose) m = Mul(m, Turn({0, 1, 0}, 0.0f, {5, 0, 0}));
        d = Drive(rr, sim, Identity(), Identity(), 0.011f, g, 1.0f, false, p, pose.data());
        EXPECT(d == Done::Started, "a jump of 5 starts it again");
    }
}

// The world through a frame: the chain simulated in a turned, scaled, moved frame hangs down in the WORLD, not in the model.
void TestFrames() {
    std::printf("frames\n");
    Skel s;
    const ChainRig r = FlailRig(&s);
    const int cog = Find(s, "cog");
    // the item's model -> world: turned 70 deg about z (the handle held out sideways), scaled 1.1, far away
    const Mat toWorld = Turn({0.2f, 0.3f, 1.0f}, 1.22f, {2000.0f, 30.0f, -1500.0f}, 1.1f);
    Mat fromWorld;
    EXPECT(Inverse(toWorld, &fromWorld), "an inverse");
    // the simulation's frame is the world less a base point near the hero (as the hook does it)
    const V3 base{2000.0f, 30.0f, -1500.0f};
    Mat toSim = toWorld;
    SetPosition(toSim, Position(toWorld) - base);
    Mat fromSim;
    Inverse(toSim, &fromSim);
    std::vector<Mat> pose = s.bind;
    Sim sim;
    Params p;
    const V3 g{0, -9.81f * 3.6f, 0};   // 3.6 units a metre
    for (int f = 0; f < 90 * 20; ++f) {
        pose = s.bind;   // the game's pose every frame: the chain stiff up along the handle
        Drive(r, sim, toSim, fromSim, 1.0f / 90.0f, g, 3.6f, false, p, pose.data());
    }
    const V3 top = Point(toWorld, Position(pose[r.bone[0]])), head = Point(toWorld, Position(pose[r.bone[r.bones - 1]]));
    const V3 handleTop = Point(toWorld, Position(Mul(s.bind[r.bone[0]], s.bind[cog])));
    EXPECT(Dist(top, handleTop) < 1e-3f, "the chain starts at the handle's top in the world (%.5f)", Dist(top, handleTop));
    const V3 d = head - top;
    EXPECT(std::fabs(d.x) < 0.01f && std::fabs(d.z) < 0.01f && d.y < 0.0f, "it hangs straight down in the world (%.3f %.3f %.3f)", d.x, d.y, d.z);
    EXPECT(std::fabs(-d.y - 1.1f * r.bindLength) < 0.01f, "its length in the world: the model's x the item's scale (%.3f of %.3f)", -d.y, 1.1f * r.bindLength);
    // the links' size is the model's (a scaled frame does not change the bones' own size)
    bool sizes = true;
    for (int k = 0; k < r.bones; ++k) sizes = sizes && std::fabs(Length(Row(pose[r.bone[k]], 0)) - 1.0f) < 2e-3f;
    EXPECT(sizes, "the links keep their size in the model");
}

// The model -> room frame against the two mappings it undoes and follows: skeletons.cpp's ControllerPoint (a copy of its ToModel) and weight.cpp's
// ToRoom (the head's yaw as heft's quaternion about +y). A controller's point taken into the model and through RoomFrame lands where weight.cpp
// puts the controller in the room; up stays up; lengths are metres (model units / upm).
V3 ToModelCopy(V3 v, float c, float s) {   // skeletons.cpp ToModel
    const V3 m = {-v.x, v.y, -v.z};
    return {m.x * c + m.z * s, m.y, -m.x * s + m.z * c};
}
V3 YawCopy(V3 v, float deg) {   // heft::Rotate(heft::QFromAxisAngle({0, 1, 0}, deg), v)
    const float h = deg * 0.0174532925f * 0.5f;
    const V3 u{0.0f, std::sin(h), 0.0f};
    const float w = std::cos(h);
    const V3 t = Cross(u, v) * 2.0f;
    return v + t * w + Cross(u, t);
}
void TestRoom() {
    std::printf("room\n");
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    float worst = 0.0f, worstUp = 0.0f, worstLen = 0.0f;
    bool ok = true;
    for (int t = 0; t < 2000; ++t) {
        const V3 eye{3.0f * u(rng), 6.0f + u(rng), 3.0f * u(rng)};
        const float upm = 3.0f + 2.0f * u(rng), yawModel = 3.14159f * u(rng), headYaw = 180.0f * u(rng);
        const V3 headRoom{2.0f * u(rng), 1.7f + 0.2f * u(rng), 2.0f * u(rng)};
        Mat m;
        ok = ok && RoomFrame(eye, upm, std::cos(yawModel), std::sin(yawModel), headRoom, headYaw, &m);
        const V3 hand{0.6f * u(rng), 0.6f * u(rng), 0.6f * u(rng)};   // a controller in the head's frame, metres
        const V3 model = eye + ToModelCopy(hand, std::cos(yawModel), std::sin(yawModel)) * upm;
        worst = std::max(worst, Dist(Point(m, model), headRoom + YawCopy(hand, headYaw)));
        worstUp = std::max(worstUp, Dist(Direction(m, {0, 1, 0}), {0, 1.0f / upm, 0}));
        const V3 d{u(rng), u(rng), u(rng)};
        worstLen = std::max(worstLen, std::fabs(Length(Direction(m, d)) - Length(d) / upm));
    }
    EXPECT(ok, "a frame every time");
    EXPECT(worst < 1e-4f, "a controller lands where weight.cpp has it in the room (off %.2e m)", worst);
    EXPECT(worstUp < 1e-6f, "the model's up is the room's up (off %.2e)", worstUp);
    EXPECT(worstLen < 1e-5f, "lengths in metres: model units / upm (off %.2e)", worstLen);
    Mat m;
    EXPECT(!RoomFrame({}, 0.0f, 1.0f, 0.0f, {}, 0.0f, &m), "no upm: no frame");
}

// The ball that strikes (hit by touch): per model, measured on the mesh; where it is on the bind pose (the mesh's own centre) and on a hanging chain.
void TestBall() {
    std::printf("ball\n");
    const Head fl = HeadOf("flail.skeleton"), lh = HeadOf("flail_lh.skeleton"), kh = HeadOf("khalim_flail.skeleton"), sk = HeadOf("super_khalim_flail_lh.skeleton");
    EXPECT(fl.ok && lh.ok && kh.ok && sk.ok && !HeadOf("mace.skeleton").ok, "a ball for each flail model, none for a mace");
    EXPECT(fl.radius == lh.radius && fl.along == lh.along && kh.radius < fl.radius && sk.along != kh.along, "each model its own (Khalim's Will is not Khalim's Flail)");
    {   // the Flail on its bind pose: the ball's centre where the mesh has it (0, 3.239, 0), radius 0.34 of a 1.52 chain
        Skel s;
        const ChainRig r = FlailRig(&s);
        V3 c, d;
        EXPECT(Ball(r, s.bind.data(), fl, &c, &d), "the Flail's ball");
        EXPECT(Dist(c, {0.0f, 3.239f, 0.0f}) < 0.003f && Dist(d, {0.0f, 1.0f, 0.0f}) < 1e-4f, "on the bind pose at the mesh's centre (%.3f %.3f %.3f)", c.x, c.y, c.z);
        // hanging: below the handle's top by the chain and the ball's offset, the way down
        std::vector<Mat> pose = s.bind;
        Sim sim;
        Params p;
        for (int f = 0; f < 90 * 15; ++f) Drive(r, sim, Identity(), Identity(), 1.0f / 90.0f, {0, -9.81f, 0}, 1.0f, false, p, pose.data());
        EXPECT(Ball(r, pose.data(), fl, &c, &d), "the hanging Flail's ball");
        const V3 top = Position(pose[r.bone[0]]);
        EXPECT(Dist(c, top + V3{0.0f, -(r.bindLength + fl.along), 0.0f}) < 0.01f && Dist(d, {0.0f, -1.0f, 0.0f}) < 0.01f, "hanging: %.3f below the handle's top (%.3f)", top.y - c.y,
               r.bindLength + fl.along);
    }
    {   // Khalim's Flail: the middle chain's ball strikes, at the mesh's centre (0, 3.284, 0)
        const Skel s = Load(kKhalim);
        Layout l;
        FindChains(s.names.data(), s.parents.data(), (int)s.names.size(), &l, nullptr);
        ChainRig rigs[kMaxChains];
        for (int c = 0; c < l.chains; ++c) Learn(l.chain[c], s.parents.data(), s.bind.data(), (int)s.names.size(), &rigs[c], nullptr);
        const int k = StrikingChain(rigs, l.chains, s.names.data(), (int)s.names.size());
        EXPECT(s.names[rigs[k].bone[0]] == "C_chain_root", "the middle chain strikes");
        V3 c, d;
        EXPECT(Ball(rigs[k], s.bind.data(), kh, &c, &d) && Dist(c, {0.0f, 3.284f, 0.0f}) < 0.003f, "at the mesh's centre (%.3f %.3f %.3f)", c.x, c.y, c.z);
    }
    {   // no known ball: none
        Skel s;
        const ChainRig r = FlailRig(&s);
        V3 c, d;
        EXPECT(!Ball(r, s.bind.data(), Head{}, &c, &d), "no ball for an unknown model");
    }
}

// Khalim's Flail: three chains, three balls - each simulated from its own root, and (the user, 2026-10-24: «сейчас они сливаются») never one lump: hanging
// still, a swing and a shake, the three balls stay at least two radii apart (Separate); without it they hang one on another. Each chain is driven (all three).
void TestThreeBalls() {
    std::printf("three balls\n");
    const Skel s = Load(kKhalim);
    Layout l;
    FindChains(s.names.data(), s.parents.data(), (int)s.names.size(), &l, nullptr);
    EXPECT(l.chains == 3, "Khalim's Flail: three chains (%d)", l.chains);
    ChainRig rigs[kMaxChains];
    for (int c = 0; c < l.chains; ++c) Learn(l.chain[c], s.parents.data(), s.bind.data(), (int)s.names.size(), &rigs[c], nullptr);
    const Head kh = HeadOf("khalim_flail");
    const float minDist = 2.0f * kh.radius * 0.95f;
    auto closest = [&](const std::vector<Mat>& pose) {
        V3 c[kMaxChains], d;
        for (int k = 0; k < l.chains; ++k) Ball(rigs[k], pose.data(), kh, &c[k], &d);
        float m = 1e9f;
        for (int a = 0; a < l.chains; ++a)
            for (int b = a + 1; b < l.chains; ++b) m = std::min(m, Dist(c[a], c[b]));
        return m;
    };
    for (int separate = 0; separate < 2; ++separate) {
        std::vector<Mat> pose = s.bind;
        Sim sims[kMaxChains];
        Params p;
        float worst = 1e9f;
        bool moved[kMaxChains] = {};
        V3 startHead[kMaxChains];
        for (int k = 0; k < l.chains; ++k) startHead[k] = Position(s.bind[rigs[k].bone[rigs[k].bones - 1]]);
        for (int f = 0; f < 90 * 12; ++f) {
            // the handle still for 6 s (hanging), then swung and shaken for 6 (the parent of the chains moved: the handle's top on a circle)
            if (f >= 90 * 6) {
                const float t = (float)(f - 90 * 6) / 90.0f, dx = 0.6f * std::sin(t * 7.0f), dz = 0.4f * std::cos(t * 11.0f);
                for (int k = 0; k < l.chains; ++k) {
                    Mat m = s.bind[rigs[k].parent];
                    SetPosition(m, Position(s.bind[rigs[k].parent]) + V3{dx, 0.0f, dz});
                    pose[rigs[k].parent] = m;
                }
            }
            for (int k = 0; k < l.chains; ++k) Drive(rigs[k], sims[k], Identity(), Identity(), 1.0f / 90.0f, {0, -9.81f, 0}, 5.0f, false, p, pose.data());
            if (separate) {
                V3 rest[kMaxChains];
                for (int k = 0; k < l.chains; ++k) rest[k] = RestHead(rigs[k], pose.data(), Identity());
                if (Separate(sims, l.chains, minDist, rest, p.iterations, kh.along) > 0)
                    for (int k = 0; k < l.chains; ++k) Repose(rigs[k], sims[k], Identity(), pose.data());
            }
            if (f > 90) worst = std::min(worst, closest(pose));
            for (int k = 0; k < l.chains; ++k)
                if (Dist(Position(pose[rigs[k].bone[rigs[k].bones - 1]]), startHead[k]) > 0.05f) moved[k] = true;
            for (const Mat& m : pose) EXPECT(FiniteMat(m), "numbers");
        }
        std::printf("  Khalim's three balls, %s: the closest two %.3f apart at worst (two radii: %.3f)\n", separate ? "kept apart" : "NOT kept apart", worst, minDist);
        if (separate) EXPECT(worst >= minDist * 0.97f, "kept apart: never one lump (%.3f against %.3f)", worst, minDist);
        else EXPECT(worst < minDist * 0.5f, "(the test's case: without it they hang one on another, %.3f)", worst);
        for (int k = 0; k < l.chains; ++k) EXPECT(moved[k], "chain %d driven (its head moved off the bind pose)", k);
    }
}

// [flail] ball_weight_pct: 1 is the model's own (exactly as before); heavier swings on longer, a hit turns it back less (it pushes through), the
// weight model's flail weighs more by the ball's part of it; the pendulum itself (its period, where it hangs) does not change with the mass.
void TestWeight() {
    std::printf("weight\n");
    const ChainRig r = FlailRig();
    const V3 g{0, -9.81f, 0}, a{0, 2, 0};
    auto swing = [&](float weight, float seconds, float* period) {
        Params p;
        p.weight = weight;
        Sim s = HangingSim(r, a, {std::sin(0.8f), -std::cos(0.8f), 0}, p);
        Sim low = HangingSim(r, a, {0, -1, 0}, p);
        const float e0 = s.Energy(g) - low.Energy(g);
        float lastX = s.x[s.n - 1].x, t = 0.0f, first = -1.0f, third = -1.0f;
        int crossings = 0;
        for (int f = 0; f < (int)(seconds * 240); ++f) {
            s.Step(a, a, 1.0f / 240.0f, g, p);
            t += 1.0f / 240.0f;
            const float x = s.x[s.n - 1].x;
            if (lastX > 0.0f && x <= 0.0f) { ++crossings; if (crossings == 1) first = t; if (crossings == 3) third = t; }
            lastX = x;
        }
        if (period) *period = (third - first) * 0.5f;
        return (s.Energy(g) - low.Energy(g)) / e0;   // the part of the swing's energy left
    };
    // 1 = as before: the same numbers as without the field touched
    {
        Params p0, p1;
        p1.weight = 1.0f;
        Sim s0 = HangingSim(r, a, {1, 0, 0}, p0), s1 = HangingSim(r, a, {1, 0, 0}, p1);
        V3 hand = a;
        for (int f = 0; f < 400; ++f) {
            const V3 next = a + V3{0.3f * std::sin(f * 0.1f), 0.0f, 0.0f};
            s0.Step(hand, next, 1.0f / 90.0f, g, p0); s1.Step(hand, next, 1.0f / 90.0f, g, p1);
            if (f % 50 == 0) { s0.Kick(p0); s1.Kick(p1); }
            hand = next;
        }
        bool same = true;
        for (int i = 0; i < s0.n; ++i) same = same && Dist(s0.x[i], s1.x[i]) == 0.0f && Dist(s0.v[i], s1.v[i]) == 0.0f;
        EXPECT(same, "100 %%: exactly as before");
    }
    float tLight = 0, tReal = 0, tHeavy = 0;
    const float light = swing(0.5f, 8.0f, &tLight), real = swing(1.0f, 8.0f, &tReal), heavy = swing(3.0f, 8.0f, &tHeavy);
    EXPECT(heavy > real && real > light, "a heavier ball swings on longer: after 8 s %.2f (x3) > %.2f (x1) > %.2f (x0.5) of its energy", heavy, real, light);
    EXPECT(std::fabs(heavy - std::exp(-0.6f / 3.0f * 8.0f)) < 0.08f, "x3: the damping a third (%.2f left, about %.2f)", heavy, std::exp(-0.6f / 3.0f * 8.0f));
    EXPECT(std::fabs(tHeavy - tReal) < 0.03f * tReal && std::fabs(tLight - tReal) < 0.03f * tReal, "the period is a pendulum's whatever the mass (%.3f %.3f %.3f s)", tLight, tReal, tHeavy);
    // a hit: a heavier ball turns back less - at x2 it keeps on through (forward), at x0.5 it bounces fully back
    for (float weight : {0.5f, 1.0f, 2.0f, 4.0f}) {
        Params p;
        p.damping = 0.0f;
        p.weight = weight;
        Sim s = HangingSim(r, a, {1, 0, 0}, p);
        for (int f = 0; f < 30; ++f) s.Step(a, a, 1.0f / 90.0f, g, p);
        const V3 before = s.v[s.n - 1];
        s.Kick(p);
        const V3 after = s.v[s.n - 1];
        const float k = Dot(after, before) / Dot(before, before);
        const float expect = 1.0f - std::min(1.35f / weight, 2.0f);
        EXPECT(std::fabs(k - expect) < 1e-3f, "x%.1f: after a hit %.3f of its speed along (%.3f)", weight, k, expect);
    }
    // the weight model's flail
    const Head fl = HeadOfCode((uint32_t)'f' | (uint32_t)'l' << 8 | (uint32_t)'a' << 16 | (uint32_t)' ' << 24);
    const Head kn = HeadOfCode((uint32_t)'9' | (uint32_t)'f' << 8 | (uint32_t)'l' << 16 | (uint32_t)' ' << 24);
    const Head kh = HeadOfCode((uint32_t)'q' | (uint32_t)'f' << 8 | (uint32_t)'1' << 16 | (uint32_t)' ' << 24);
    const Head mace = HeadOfCode((uint32_t)'m' | (uint32_t)'a' << 8 | (uint32_t)'c' << 16 | (uint32_t)' ' << 24);
    EXPECT(fl.ok && kn.ok && kh.ok && !mace.ok && fl.share == kn.share && kh.radius == HeadOf("khalim_flail").radius, "by the item: Flail and Knout one model, Khalim's its own, a mace none");
    EXPECT(WeaponMassFactor(fl, 1.0f) == 1.0f && WeaponMassFactor(mace, 3.0f) == 1.0f, "100 %% or not a flail: the weight model's mass as before");
    EXPECT(std::fabs(WeaponMassFactor(fl, 2.0f) - 1.93f) < 1e-4f && std::fabs(WeaponMassFactor(fl, 0.5f) - 0.535f) < 1e-4f, "x2: the flail 1.93 times as heavy (the ball 0.93 of it); x0.5: 0.535");
    EXPECT(WeaponMassFactor(fl, 100.0f) == WeaponMassFactor(fl, 4.0f) && WeaponMassFactor(fl, -5.0f) == WeaponMassFactor(fl, 0.25f), "held to 25..400 %%");
}

void TestGate() {
    std::printf("gate\n");
    EXPECT(Wanted(true, true, true), "ticked in F4, a flail, the hero's: posed");
    EXPECT(!Wanted(false, true, true), "unticked (or not F4): the game's pose");
    EXPECT(!Wanted(true, false, true), "not a flail: the game's pose");
    EXPECT(!Wanted(true, true, false), "another's flail: the game's pose");
}

}  // namespace

int main() {
    TestModels();
    TestChains();
    TestPose();
    TestSim();
    TestFrames();
    TestRoom();
    TestBall();
    TestThreeBalls();
    TestWeight();
    TestGate();
    std::printf("flail_chain_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
