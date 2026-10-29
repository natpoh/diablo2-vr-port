// The Necromancer's head hanging from its cord (vr/head_hang.h), without the game.
//
//   head_hang_test
//
// The five head models' mesh boxes as the game's files have them: where each hangs from and how long its cord is; the head hangs straight down
// whatever the hand's angle (mirrored too), a moved hand swings it and it settles, its period is a pendulum's, the cord never stretches, a
// twist of the hand turns the head after it with a little lag and overshoot, turning the hand over never flips the head, it stays numbers under
// any shaking and at any frame rate; the frame hook: the head turned rigidly about the point it hangs from, started afresh after a pause, a jump
// or a mirror, the same frame posed twice the same.

#include "head_hang.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace headhang;

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

constexpr float kDeg = 57.2957795f;
const V3 kG{0.0f, -9.81f, 0.0f};
const V3 kDown{0.0f, -1.0f, 0.0f};

float AngleDeg(V3 a, V3 b) { return std::acos(std::clamp(Dot(Unit(a), Unit(b)), -1.0f, 1.0f)) * kDeg; }
// The signed turn about `axis` from a to b (both projected square to it), degrees.
float TurnDeg(V3 axis, V3 a, V3 b) {
    const V3 k = Unit(axis);
    const V3 pa = Unit(a - k * Dot(k, a)), pb = Unit(b - k * Dot(k, b));
    return std::atan2(Dot(Cross(pa, pb), k), Dot(pa, pb)) * kDeg;
}
void Frame(V3 axis, float rad, V3* out) {
    const V3 e[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) out[i] = TurnAbout(Unit(axis), rad, e[i]);
}

// The models' meshes' boxes (bbMin / bbMax of each mesh's ExtendedData), read off the installed game's lod0 models (3.3.93787).
struct Model { const char* name; std::vector<Box> boxes; float length; };
std::vector<Model> Models() {
    return {
        {"demon_head", {{{-0.407f, -1.382f, -0.376f}, {0.372f, 0.513f, 0.321f}}}, 0.692f},
        {"gargoyle_head", {{{-0.332f, -1.795f, -0.464f}, {0.331f, 1.337f, 0.466f}}}, 0.898f},
        {"preserved_head", {{{-0.334f, -1.147f, -0.389f}, {0.366f, 0.31f, 0.389f}}, {{-0.334f, -1.153f, -0.39f}, {0.332f, 0.0f, 0.393f}},
                            {{-0.351f, -1.183f, -0.41f}, {0.349f, 0.0f, 0.413f}}, {{-0.334f, -1.147f, -0.389f}, {0.366f, 0.31f, 0.389f}}}, 0.592f},
        {"unraveller_head", {{{-0.318f, -2.133f, -0.444f}, {0.317f, 1.253f, 0.448f}}}, 1.067f},
        {"zombie_head", {{{-0.081f, -0.196f, -0.081f}, {0.081f, 0.0f, 0.081f}}, {{-0.081f, 0.22f, -0.081f}, {0.081f, 0.22f, 0.081f}},
                         {{-0.359f, -1.621f, -0.356f}, {0.45f, 0.624f, 0.276f}}, {{-0.102f, -0.265f, -0.11f}, {0.106f, 0.294f, 0.107f}}}, 0.813f},
    };
}

void TestModels() {
    EXPECT(IsHeadModel("data/hd/items/armor/voodoo_head/zombie_head/zombie_head.model"), "a head");
    EXPECT(IsHeadModel("data\\hd\\items\\armor\\voodoo_head\\demon_head\\skeleton\\demon_head.skeleton"), "a head, the other slashes");
    EXPECT(!IsHeadModel("data/hd/items/armor/shield/buckler/buckler.model"), "a shield is not");
    EXPECT(!IsHeadModel("data/hd/items/weapon/mace/flail/flail.model"), "a flail is not");
    EXPECT(!IsHeadModel(""), "nothing is not");
}

void TestHang() {
    for (const Model& m : Models()) {
        const Hang h = HangOf(m.boxes.data(), (int)m.boxes.size());
        EXPECT(h.ok, "%s hangs", m.name);
        EXPECT(std::fabs(h.length - m.length) < 0.01f, "%s's cord %.3f, its hanging part's centre %.3f", m.name, h.length, m.length);
        EXPECT(h.dir.y < -0.99f, "%s hangs along its -Y (%.3f %.3f %.3f)", m.name, h.dir.x, h.dir.y, h.dir.z);
        EXPECT(std::fabs(Length(h.dir) - 1.0f) < 1e-4f, "a direction");
    }
    const Box above[1] = {{{-0.1f, 0.1f, -0.1f}, {0.1f, 0.5f, 0.1f}}};
    EXPECT(!HangOf(above, 1).ok, "a model with nothing below its origin does not hang");
    EXPECT(!HangOf(nullptr, 0).ok, "no boxes");
    const float nan = std::nanf("");
    const Box bad[2] = {{{nan, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}, {{-0.2f, -1.0f, -0.2f}, {0.2f, 0.4f, 0.2f}}};
    const Hang h = HangOf(bad, 2);
    EXPECT(h.ok && std::fabs(h.length - 0.5f) < 1e-4f, "a box that is not numbers is left out (%.3f)", h.length);
    const Box inside_out[1] = {{{0.2f, -1.0f, 0.2f}, {-0.2f, 0.0f, -0.2f}}};
    EXPECT(!HangOf(inside_out, 1).ok, "a box inside out is none");
}

// The five heads' skeletons as the game poses them (their .skeleton files - 8, 24, 20, 16 and 13 bones, the counts the game's log shows): each
// bone's bind place, the model's space, read off the installed 3.3.93787 files.
struct Bones { const char* name; std::vector<V3> places; };
std::vector<Bones> Skeletons() {
    return {
        {"preserved_head", {{0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.095f, -0.145f, -0.080f}, {0.000f, -0.716f, 0.000f}, {0.000f, -0.716f, 0.000f}}},
        {"zombie_head", {{0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.002f, -0.310f, 0.000f}, {0.002f, -0.448f, -0.016f}, {-0.002f, -0.625f, -0.036f}, {0.013f, 0.236f, 0.006f}, {0.021f, 0.359f, 0.003f}, {0.072f, -1.229f, 0.155f}, {0.077f, -1.330f, 0.155f}, {-0.156f, -0.933f, 0.150f}, {-0.168f, -1.018f, 0.152f}, {-0.106f, -1.196f, 0.171f}, {-0.108f, -1.290f, 0.171f}, {-0.172f, -1.113f, 0.150f}, {0.156f, -0.956f, 0.149f}, {0.158f, -1.029f, 0.149f}, {0.160f, -1.101f, 0.149f}, {0.028f, 0.482f, 0.000f}, {0.075f, -1.430f, 0.155f}, {-0.113f, -1.384f, 0.168f}, {-0.170f, -1.199f, 0.147f}, {0.163f, -1.174f, 0.148f}}},
        {"demon_head", {{0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.357f, -0.145f}, {0.000f, 0.166f, -0.086f}, {0.000f, 0.038f, -0.059f}, {0.000f, -0.066f, 0.001f}, {0.000f, -0.292f, -0.024f}, {-0.138f, -0.934f, 0.185f}, {-0.136f, -1.082f, 0.185f}, {-0.168f, -0.627f, 0.192f}, {-0.172f, -0.734f, 0.189f}, {0.180f, -0.641f, 0.179f}, {0.188f, -0.747f, 0.172f}, {0.134f, -0.934f, 0.164f}, {0.134f, -1.019f, 0.162f}, {-0.133f, -1.230f, 0.185f}, {-0.176f, -0.846f, 0.186f}, {0.196f, -0.851f, 0.160f}, {0.134f, -1.105f, 0.159f}}},
        {"gargoyle_head", {{0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.389f, 0.000f}, {0.000f, 0.476f, 0.000f}, {0.000f, 0.663f, 0.000f}, {0.000f, 0.850f, 0.000f}, {-0.016f, -1.399f, 0.003f}, {-0.031f, -1.530f, 0.003f}, {0.019f, -1.397f, 0.003f}, {0.028f, -1.529f, 0.003f}, {0.000f, 1.036f, 0.000f}, {-0.046f, -1.661f, 0.003f}, {0.038f, -1.661f, 0.003f}}},
        {"unraveller_head", {{0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.000f, 0.000f, 0.000f}, {0.010f, -1.428f, 0.015f}, {-0.001f, -1.406f, -0.008f}, {0.000f, 0.345f, 0.000f}, {0.000f, 0.430f, 0.000f}, {0.000f, 0.702f, 0.000f}, {0.131f, -1.762f, -0.018f}, {-0.084f, -1.686f, 0.009f}, {0.000f, 0.974f, 0.000f}}},
    };
}

void TestHangOfBones() {
    // the boxes do not read in the game (the .skeleton's model has no meshes): the bones say where it hangs - a little short of the meshes
    const float want[5] = {0.363f, 0.718f, 0.621f, 0.831f, 0.881f};
    const float meshes[5] = {0.592f, 0.813f, 0.692f, 0.898f, 1.067f};
    std::vector<Bones> all = Skeletons();
    for (size_t i = 0; i < all.size(); ++i) {
        const Hang h = HangOfBones(all[i].places.data(), (int)all[i].places.size());
        EXPECT(h.ok, "%s hangs by its bones", all[i].name);
        EXPECT(std::fabs(h.length - want[i]) < 0.01f, "%s: the cord %.3f by its bones (%.3f)", all[i].name, h.length, want[i]);
        EXPECT(h.length > meshes[i] * 0.55f && h.length <= meshes[i], "%s: by its bones %.3f, by its meshes %.3f - shorter, not much", all[i].name, h.length, meshes[i]);
        EXPECT(h.dir.y < -0.98f, "%s hangs along its -Y (%.3f %.3f %.3f)", all[i].name, h.dir.x, h.dir.y, h.dir.z);
    }
    // a head must hang in every case
    const V3 above[3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.4f, 0.0f}, {0.1f, 0.8f, 0.0f}};
    Hang h = HangOfBones(above, 3);
    EXPECT(h.ok && Length(h.dir - kDown) < 1e-6f && std::fabs(h.length - Length(above[2]) * 0.5f) < 1e-5f, "nothing below its origin: along -Y, half its farthest bone (%.3f)", h.length);
    h = HangOfBones(nullptr, 0);
    EXPECT(h.ok && Length(h.dir - kDown) < 1e-6f && h.length == 1.0f, "no bones at all: along -Y, one unit");
    const float nan = std::nanf("");
    const V3 bad[3] = {{nan, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1e9f, -1e9f, 0.0f}};
    h = HangOfBones(bad, 3);
    EXPECT(h.ok && std::fabs(h.length - 0.5f) < 1e-5f, "a bone that is not numbers, or far off, is left out (%.3f)", h.length);
}

void TestRig() {
    Hang h;
    h.ok = true; h.dir = Unit(V3{0.1f, -1.0f, 0.0f}); h.length = 0.8f;
    // a carrier turned 90 degrees about z and moved: the line in its own axes is the line turned back
    Mat bind = flailchain::Identity();
    V3 rows[3];
    Frame({0, 0, 1}, kPi * 0.5f, rows);
    for (int i = 0; i < 3; ++i) flailchain::SetRow(bind, i, rows[i]);
    flailchain::SetPosition(bind, {0.0f, 0.3f, -0.1f});
    const Rig r = RigOf(h, 3, &bind);
    EXPECT(r.ok && r.carrier == 3, "a rig");
    EXPECT(AngleDeg(flailchain::Direction(bind, r.down), h.dir) < 0.01f, "the carrier's down, through its bind pose, is the model's line");
    EXPECT(std::fabs(r.length - 0.8f) < 1e-6f, "the length kept");
    const Rig root = RigOf(h, -1, nullptr);
    EXPECT(root.ok && root.carrier == -1 && AngleDeg(root.down, h.dir) < 1e-3f, "no carrier: the model's frame");
    EXPECT(!RigOf(Hang{}, 3, &bind).ok, "no hang, no rig");
}

void TestTurnBetween() {
    V3 a[3], b[3];
    Frame({0.3f, 1.0f, -0.2f}, 0.4f, a);
    const V3 axis = Unit(V3{1.0f, 0.5f, 0.2f});
    for (int i = 0; i < 3; ++i) b[i] = TurnAbout(axis, 0.25f, a[i]);
    const V3 w = TurnBetween(a, b);
    EXPECT(Length(w - axis * 0.25f) < 1e-4f, "the turn's vector %.4f %.4f %.4f", w.x, w.y, w.z);
    EXPECT(Length(TurnBetween(a, a)) < 1e-6f, "no turn");
    V3 ma[3] = {a[0], a[1], a[2] * -1.0f}, mb[3] = {b[0], b[1], b[2] * -1.0f};   // mirrored frames turn the same
    EXPECT(Length(TurnBetween(ma, mb) - axis * 0.25f) < 1e-4f, "mirrored: the same turn");
}

// One run of a hand: `at(t, rows, place)` gives its frame and its place; steps at fps for `seconds`; calls `each(t, state)`.
template <class Hand, class Each>
void Run(State& s, V3 down, float cord, float fps, float seconds, Hand&& at, Each&& each, const Params& p = Params{}) {
    V3 rows[3], place;
    at(0.0f, rows, &place);
    s.Start(rows, down, place, cord, kG);
    const int frames = (int)(seconds * fps);
    for (int f = 1; f <= frames; ++f) {
        const float t = f / fps;
        at(t, rows, &place);
        s.Step(rows, down, place, 1.0f / fps, kG, p);
        each(t, s);
    }
}

void TestHangsDown() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    int worst = 0;
    float worstDeg = 0.0f;
    for (int k = 0; k < 40; ++k) {
        V3 rows[3];
        Frame({u(rng), u(rng), u(rng) + 0.01f}, u(rng) * kPi, rows);
        if (k % 2) rows[2] = rows[2] * -1.0f;   // a mirrored hand frame (the left attach bone is one in some rigs)
        const V3 down = Unit(V3{u(rng) * 0.1f, -1.0f, u(rng) * 0.1f});
        State s;
        s.Start(rows, down, {0.2f, 1.1f, -0.3f}, 0.25f, kG);
        EXPECT(s.live, "started");
        float deg = AngleDeg(Through(s.head, down), kDown);
        EXPECT(deg < 0.05f, "at the start it hangs straight down whatever the hand's angle (%.3f deg, hand %d)", deg, k);
        EXPECT((Det(s.head) < 0.0f) == (Det(rows) < 0.0f), "the head keeps the hand's handedness");
        for (int f = 0; f < 270; ++f) s.Step(rows, down, {0.2f, 1.1f, -0.3f}, 1.0f / 90.0f, kG, Params{});
        deg = AngleDeg(Through(s.head, down), kDown);
        if (deg > worstDeg) { worstDeg = deg; worst = k; }
        EXPECT(Length(s.sim.x[1] - V3{0.2f, 1.1f - 0.25f, -0.3f}) < 1e-3f, "a still hand: the weight straight below it");
    }
    EXPECT(worstDeg < 0.1f, "a still hand: it stays hanging straight down (worst %.3f deg, hand %d)", worstDeg, worst);
}

void TestSwing() {
    // the hand jerked 30 cm sideways in 0.15 s, then held: the head lags, swings out, settles
    State s;
    float maxDeg = 0.0f, lateDeg = 0.0f, stretch = 0.0f, offLine = 0.0f;
    float energyAt[8] = {};
    const float cord = 0.25f;
    Run(s, kDown, cord, 90.0f, 8.0f,
        [](float t, V3* rows, V3* place) {
            Frame({0, 1, 0}, 0.0f, rows);
            const float k = std::clamp(t / 0.15f, 0.0f, 1.0f);
            *place = {0.3f * k * k * (3.0f - 2.0f * k), 1.2f, 0.0f};
        },
        [&](float t, const State& st) {
            const float deg = AngleDeg(st.Line(), kDown);
            maxDeg = std::max(maxDeg, deg);
            if (t > 6.0f) lateDeg = std::max(lateDeg, deg);
            offLine = std::max(offLine, AngleDeg(Through(st.head, kDown), st.Line()));
            stretch = std::max(stretch, Length(st.sim.x[1] - st.sim.x[0]) - cord);
            const int sec = (int)t;
            if (t > 0.5f && sec < 8) energyAt[sec] = std::max(energyAt[sec], st.Energy(kG) - st.sim.x[0].y * 9.81f);   // against the cord's top
        });
    EXPECT(maxDeg > 15.0f, "a jerked hand swings the head out (%.1f deg)", maxDeg);
    EXPECT(lateDeg < 1.0f, "it settles: after 6 s within %.2f deg of straight down", lateDeg);
    EXPECT(stretch < cord * 1e-3f, "the cord never stretches (%.5f)", stretch);
    EXPECT(offLine < 0.1f, "the head swings with its cord: on the cord's line throughout (%.3f deg)", offLine);
    for (int i = 2; i < 8; ++i) EXPECT(energyAt[i] <= energyAt[i - 1] + 1e-3f, "the held swing never gains energy (second %d: %.4f after %.4f)", i, energyAt[i], energyAt[i - 1]);
}

void TestPeriod() {
    // a free swing after a small jerk: its period is a pendulum's, 2 pi sqrt(L / g)
    const float cord = 0.25f;
    State s;
    std::vector<float> crossings;
    float last = 0.0f;
    Run(s, kDown, cord, 240.0f, 6.0f,
        [](float t, V3* rows, V3* place) {
            Frame({0, 1, 0}, 0.0f, rows);
            const float k = std::clamp(t / 0.1f, 0.0f, 1.0f);
            *place = {0.05f * k * k * (3.0f - 2.0f * k), 1.2f, 0.0f};
        },
        [&](float t, const State& st) {
            const float x = st.sim.x[1].x - st.sim.x[0].x;
            if (t > 0.2f && last < 0.0f && x >= 0.0f) crossings.push_back(t);
            last = x;
        });
    const float want = 2.0f * kPi * std::sqrt(cord / 9.81f);
    EXPECT(crossings.size() >= 3, "it swings back and forth (%d crossings)", (int)crossings.size());
    if (crossings.size() >= 3) {
        const float period = (crossings.back() - crossings.front()) / (float)(crossings.size() - 1);
        EXPECT(std::fabs(period - want) < want * 0.05f, "the period %.3f s, a pendulum's %.3f s", period, want);
    }
}

void TestTwist() {
    // the hand turned 90 degrees about the cord (straight down) in 0.1 s, then held: the head follows it late, overshoots a little, settles
    State s;
    V3 start[3];
    float at01 = 0.0f, most = 0.0f, late = 0.0f;
    bool first = true;
    Run(s, kDown, 0.25f, 90.0f, 7.0f,
        [](float t, V3* rows, V3* place) {
            const float k = std::clamp(t / 0.1f, 0.0f, 1.0f);
            Frame({0, 1, 0}, 0.5f * kPi * k, rows);
            *place = {0.0f, 1.2f, 0.0f};
        },
        [&](float t, const State& st) {
            if (first) { for (int i = 0; i < 3; ++i) start[i] = st.head[i]; first = false; }   // (the first frame's turn is a ninth of the whole: small)
            const float deg = TurnDeg({0, 1, 0}, V3{1, 0, 0}, st.head[0]);
            if (std::fabs(t - 0.1f) < 0.006f) at01 = deg;
            most = std::max(most, deg);
            if (t > 6.0f) late = std::max(late, std::fabs(deg - 90.0f));
        });
    EXPECT(at01 > 0.0f && at01 < 60.0f, "the head lags the hand's twist: %.1f deg when the hand is at 90", at01);
    EXPECT(most > 92.0f && most < 130.0f, "it overshoots a little (%.1f deg at most)", most);
    EXPECT(late < 1.0f, "it settles at the hand's twist (%.2f deg off after 6 s)", late);
    EXPECT(AngleDeg(Through(s.head, kDown), kDown) < 0.1f, "twisting, it still hangs straight down");
}

void TestTurnOver() {
    // the hand turned over (180 degrees about a level axis, 2 s): the head's frame never jumps, it hangs down throughout
    State s;
    float worstStep = 0.0f, worstHang = 0.0f;
    V3 prev[3];
    bool have = false;
    Run(s, kDown, 0.25f, 90.0f, 3.0f,
        [](float t, V3* rows, V3* place) {
            const float k = std::clamp(t / 2.0f, 0.0f, 1.0f);
            Frame({1, 0, 0.2f}, kPi * k, rows);
            *place = {0.0f, 1.2f, 0.0f};
        },
        [&](float, const State& st) {
            if (have) for (int i = 0; i < 3; ++i) worstStep = std::max(worstStep, AngleDeg(prev[i], st.head[i]));
            for (int i = 0; i < 3; ++i) prev[i] = st.head[i];
            have = true;
            worstHang = std::max(worstHang, AngleDeg(Through(st.head, kDown), st.Line()));
        });
    EXPECT(worstStep < 5.0f, "no jump of the head as the hand turns over (worst %.2f deg a frame)", worstStep);
    EXPECT(worstHang < 0.1f, "the head on its cord's line throughout (%.3f deg)", worstHang);
    EXPECT(AngleDeg(s.Line(), kDown) < 1.0f, "and hanging down at the end (%.2f deg)", AngleDeg(s.Line(), kDown));
}

void TestFrameRates() {
    // the same hand at 30, 90 and 144 frames a second: the same swing, settled the same
    float lateDeg[3] = {}, maxDeg[3] = {};
    const float fps[3] = {30.0f, 90.0f, 144.0f};
    for (int r = 0; r < 3; ++r) {
        State s;
        Run(s, kDown, 0.3f, fps[r], 8.0f,
            [](float t, V3* rows, V3* place) {
                Frame({0, 0, 1}, 0.6f * std::sin(t * 3.0f) * (t < 2.0f ? 1.0f : 0.0f), rows);
                *place = {0.2f * std::sin(t * 4.0f) * (t < 2.0f ? 1.0f : 0.0f), 1.2f, 0.0f};
            },
            [&](float t, const State& st) {
                const float deg = AngleDeg(st.Line(), kDown);
                maxDeg[r] = std::max(maxDeg[r], deg);
                if (t > 7.5f) lateDeg[r] = std::max(lateDeg[r], deg);
            });
    }
    for (int r = 0; r < 3; ++r) EXPECT(lateDeg[r] < 1.5f, "%.0f fps: settled (%.2f deg)", fps[r], lateDeg[r]);
    EXPECT(std::fabs(maxDeg[0] - maxDeg[2]) < maxDeg[2] * 0.15f + 1.0f, "the swing's reach the same at 30 and 144 fps (%.1f, %.1f deg)", maxDeg[0], maxDeg[2]);

    // shaken at random, any frame time up to a quarter second: numbers, the cord no longer than it is
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f), dtr(0.002f, 0.25f);
    State s;
    V3 rows[3];
    Frame({0, 1, 0}, 0.0f, rows);
    s.Start(rows, kDown, {0, 1, 0}, 0.3f, kG);
    bool finite = true;
    float stretch = 0.0f;
    for (int i = 0; i < 3000; ++i) {
        Frame({u(rng), u(rng), u(rng) + 0.01f}, u(rng) * kPi, rows);
        s.Step(rows, kDown, {u(rng) * 0.4f, 1.0f + u(rng) * 0.4f, u(rng) * 0.4f}, dtr(rng), kG, Params{});
        finite = finite && s.live && Finite(s.sim.x[1]) && Finite(s.head[0]) && Finite(s.head[1]) && Finite(s.head[2]) && std::isfinite(s.wind);
        stretch = std::max(stretch, Length(s.sim.x[1] - s.sim.x[0]) - 0.3f);
    }
    EXPECT(finite, "shaken at random: numbers throughout");
    EXPECT(stretch < 0.3f * 1e-3f, "shaken at random: the cord never stretches (%.5f)", stretch);
    EXPECT(std::fabs(Det(s.head) - 1.0f) < 1e-3f, "the head's frame still square (det %.5f)", Det(s.head));
}

// A head's pose as the game hands it to the hook: root, translate, anim at the origin, cog, the head below.
struct Pose { Mat m[5]; };
Pose GamePose() {
    Pose p;
    for (Mat& m : p.m) m = flailchain::Identity();
    flailchain::SetPosition(p.m[3], {0.0f, 0.05f, 0.0f});    // cog
    flailchain::SetPosition(p.m[4], {0.02f, -0.7f, 0.05f});  // head
    return p;
}
// The model's space -> the room: the hand tilted so the head's -Y points along +X, 0.3 m per unit, at (0.1, 1.2, -0.2).
Mat ToRoom(bool mirror = false) {
    V3 rows[3];
    Frame({0, 0, 1}, kPi * 0.5f, rows);   // -Y -> +X
    Mat m = flailchain::Identity();
    for (int i = 0; i < 3; ++i) flailchain::SetRow(m, i, rows[i] * (mirror && i == 2 ? -0.3f : 0.3f));
    flailchain::SetPosition(m, {0.1f, 1.2f, -0.2f});
    return m;
}

void TestDrive() {
    Hang h;
    h.ok = true; h.dir = kDown; h.length = 0.7f;
    const Pose game = GamePose();
    const Rig rig = RigOf(h, 3, &game.m[3]);
    const Mat toRoom = ToRoom();
    Mat fromRoom;
    EXPECT(flailchain::Inverse(toRoom, &fromRoom), "the room frame inverts");
    State s;
    Pose p = game;
    Done d = Drive(rig, s, toRoom, fromRoom, 1.0f, kG, 1.0f, Params{}, p.m, 5);
    EXPECT(d == Done::Started, "a first frame starts it");
    const V3 anchor = flailchain::Position(toRoom);
    EXPECT(Length(flailchain::Point(toRoom, flailchain::Position(p.m[0])) - anchor) < 1e-5f, "the point it hangs from stays where the hand has it");
    const V3 cogDown = flailchain::Direction(flailchain::Mul(p.m[3], toRoom), rig.down);
    EXPECT(AngleDeg(cogDown, kDown) < 0.05f, "the hand holds it sideways, it hangs down (%.3f deg)", AngleDeg(cogDown, kDown));
    const V3 headRoom = flailchain::Point(toRoom, flailchain::Position(p.m[4]));
    EXPECT(headRoom.y < anchor.y - 0.2f, "the head below the hand (%.3f under it)", anchor.y - headRoom.y);
    float rigid = 0.0f;
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b)
            rigid = std::max(rigid, std::fabs(Length(flailchain::Position(p.m[a]) - flailchain::Position(p.m[b])) -
                                              Length(flailchain::Position(game.m[a]) - flailchain::Position(game.m[b]))));
    EXPECT(rigid < 1e-4f, "turned as a whole: the bones' distances kept (%.6f)", rigid);
    EXPECT(std::fabs(std::fabs(flailchain::Length(flailchain::Row(p.m[4], 0))) - 1.0f) < 1e-4f, "no scale added to a bone");
    EXPECT(std::fabs(s.length - 0.7f * 0.3f) < 1e-5f, "the cord's length is the model's, in the room (%.4f m)", s.length);

    // the same frame posed twice (the second eye): held, the same pose
    Pose q = game;
    d = Drive(rig, s, toRoom, fromRoom, 0.0002f, kG, 1.0f, Params{}, q.m, 5);
    EXPECT(d == Done::Held, "the same frame again is held");
    float same = 0.0f;
    for (int i = 0; i < 5; ++i) for (int k = 0; k < 16; ++k) same = std::max(same, std::fabs(q.m[i].m[k] - p.m[i].m[k]));
    EXPECT(same < 1e-6f, "and posed the same (%.7f)", same);

    // frames go on: stepped
    q = game;
    d = Drive(rig, s, toRoom, fromRoom, 1.0f / 90.0f, kG, 1.0f, Params{}, q.m, 5);
    EXPECT(d == Done::Stepped, "a next frame steps");
    // a pause, a jump, a mirror: started afresh
    q = game;
    EXPECT(Drive(rig, s, toRoom, fromRoom, 0.3f, kG, 1.0f, Params{}, q.m, 5) == Done::Started, "after a pause it starts again");
    Mat far = toRoom;
    flailchain::SetPosition(far, {3.0f, 1.2f, 0.0f});
    Mat farInv;
    flailchain::Inverse(far, &farInv);
    q = game;
    EXPECT(Drive(rig, s, far, farInv, 1.0f / 90.0f, kG, 1.0f, Params{}, q.m, 5) == Done::Started, "a jump of the hand starts it again");
    const Mat mirror = ToRoom(true);
    Mat mirrorInv;
    flailchain::Inverse(mirror, &mirrorInv);
    q = game;
    EXPECT(Drive(rig, s, mirror, mirrorInv, 1.0f / 90.0f, kG, 1.0f, Params{}, q.m, 5) == Done::Started, "a mirrored hand frame starts it again");
    const V3 mDown = flailchain::Direction(flailchain::Mul(q.m[3], mirror), rig.down);
    EXPECT(AngleDeg(mDown, kDown) < 0.05f, "mirrored: it hangs down too (%.3f deg)", AngleDeg(mDown, kDown));
    float mrigid = 0.0f;
    for (int a = 0; a < 5; ++a)
        mrigid = std::max(mrigid, std::fabs(Length(flailchain::Position(q.m[a]) - flailchain::Position(q.m[3])) - Length(flailchain::Position(game.m[a]) - flailchain::Position(game.m[3]))));
    EXPECT(mrigid < 1e-4f, "mirrored: turned as a whole, not reflected (%.6f)", mrigid);

    // the game animating its cog (a swing of its own): the hang overrides it - the cog hangs as the state says
    State t;
    Pose a = game;
    Drive(rig, t, toRoom, fromRoom, 1.0f, kG, 1.0f, Params{}, a.m, 5);
    for (int f = 0; f < 20; ++f) {
        Pose g = game;
        V3 rows[3];
        Frame({1, 0, 0}, 0.5f * std::sin(f * 0.3f), rows);
        for (int i = 0; i < 3; ++i) flailchain::SetRow(g.m[3], i, rows[i]);
        Drive(rig, t, toRoom, fromRoom, 1.0f / 90.0f, kG, 1.0f, Params{}, g.m, 5);
        const V3 down = flailchain::Direction(flailchain::Mul(g.m[3], toRoom), rig.down);
        EXPECT(AngleDeg(down, t.Line()) < 0.05f, "frame %d: the game's own turn of the cog does not take the head off its cord (%.3f deg)", f, AngleDeg(down, t.Line()));
    }

    // no rig, a carrier past the bones: nothing written
    Pose n = game;
    State u;
    EXPECT(Drive(Rig{}, u, toRoom, fromRoom, 1.0f, kG, 1.0f, Params{}, n.m, 5) == Done::Held && std::memcmp(&n, &game, sizeof n) == 0, "no rig: the game's pose");
    Rig past = rig;
    past.carrier = 7;
    EXPECT(Drive(past, u, toRoom, fromRoom, 1.0f, kG, 1.0f, Params{}, n.m, 5) == Done::Held && std::memcmp(&n, &game, sizeof n) == 0, "a carrier not in the skeleton: the game's pose");
}

}  // namespace

int main() {
    TestModels();
    TestHang();
    TestHangOfBones();
    TestRig();
    TestTurnBetween();
    TestHangsDown();
    TestSwing();
    TestPeriod();
    TestTwist();
    TestTurnOver();
    TestFrameRates();
    TestDrive();
    std::printf("head_hang_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
