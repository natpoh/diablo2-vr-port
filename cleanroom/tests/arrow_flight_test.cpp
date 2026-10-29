// Offline checks of [bow] arrow_flight's maths (vr/arrow_flight.h): which missiles are the hero's arrows, where our arrow is drawn as the game's own one
// flies (to what the shot's line met, or out of the bow onto the game's path), and the matrices it is drawn with. No game, no headset.
//
//   arrow_flight_test        (exit code 0 = all passed)

#include "../../vr/arrow_flight.h"

#include <cmath>
#include <cstdio>

using arrowflight::V3;

static int g_fails = 0;
static void Expect(bool ok, const char* what, double a = 0.0, double b = 0.0) {
    if (!ok) {
        ++g_fails;
        std::printf("FAIL %s (%g %g)\n", what, a, b);
    }
}
static bool Near(V3 a, V3 b, float tol) { return arrowflight::Length(a - b) <= tol; }
static bool NearF(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

constexpr float kUpm = 50.0f;   // world units a metre (the numbers in the game are of this order; any will do)

static V3 M(V3 v) { return v * kUpm; }   // metres -> world units

static void TestClasses() {
    using namespace arrowflight;
    Unit u;
    u.type = kTypeMissile;
    u.cls = kArrow;
    u.flags = kFlagHasOwner;
    u.ownerType = kTypePlayer;
    u.ownerId = 7;
    Expect(IsHeroArrow(u, 7), "the hero's plain arrow");
    Expect(IsHeroArrow(u, 0), "the hero's id unknown: a player's arrow counts");
    Expect(!IsHeroArrow(u, 8), "another player's arrow");
    for (uint32_t c : {kFireArrow, kMagicArrow, kIceArrow, kBolt, kColdArrow, kExplodingArrow, kImmolationArrow, kGuidedArrow, kFreezingArrow, kMultiArrow,
                       kMultiBolt, kStrafeArrow, kStrafeBolt}) {
        u.cls = c;
        Expect(IsHeroArrow(u, 7), "a bow skill's arrow or bolt", c);
    }
    for (uint32_t c : {1u, 42u, 44u, 49u, 83u, 88u, 196u, 230u}) {   // a javelin, the explosion, the potions, a stuck arrow, the freezing burst, a skeleton's arrow, fire
        u.cls = c;
        Expect(!IsHeroArrow(u, 7), "not an arrow of ours", c);
    }
    u.cls = kArrow;
    u.ownerType = 1;   // a monster's
    Expect(!IsHeroArrow(u, 7), "a monster's arrow");
    u.ownerType = kTypePlayer;
    u.flags = 0;
    Expect(!IsHeroArrow(u, 7), "no owner recorded");
    u.flags = kFlagHasOwner;
    u.type = 1;
    Expect(!IsHeroArrow(u, 7), "not a missile");
    Expect(Homes(kGuidedArrow) && !Homes(kArrow), "Guided Arrow homes");
    Shot s;
    s.ok = true;
    s.atMs = 1000;
    Expect(Armed(s, 1000) && Armed(s, 1000 + kWindowMs) && !Armed(s, 1001 + kWindowMs) && !Armed(s, 999), "the shot's window");
    s.ok = false;
    Expect(!Armed(s, 1000), "no shot");
}

static void TestRotate() {
    using arrowflight::RotateFromTo;
    const V3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    Expect(Near(RotateFromTo(x, y, x), y, 1e-5f), "x onto y");
    Expect(Near(RotateFromTo(x, y, z), z, 1e-5f), "the axis across both stays");
    Expect(Near(RotateFromTo(x, x, V3{0.3f, -2.0f, 5.0f}), V3{0.3f, -2.0f, 5.0f}, 1e-5f), "the same way: nothing turns");
    const V3 back = RotateFromTo(x, V3{-1, 0, 0}, x);
    Expect(Near(back, V3{-1, 0, 0}, 1e-5f), "opposed: a half turn");
    const V3 d = arrowflight::Unit3(V3{1, -0.4f, 0.2f}, x);
    const V3 v{0.2f, 3.0f, -1.0f};
    Expect(NearF(arrowflight::Length(RotateFromTo(x, d, v)), arrowflight::Length(v), 1e-4f), "a turn keeps lengths");
}

// A game matrix for an arrow flying along heading h (flat), its own long axis its row 2 (scaled s), at p.
static void GameMatrix(V3 h, float s, V3 p, float m[16]) {
    const V3 up{0, 1, 0};
    const V3 side = arrowflight::Unit3(arrowflight::Cross(up, h), V3{1, 0, 0});
    const V3 rows[3] = {side * s, up * s, h * s};
    for (int r = 0; r < 3; ++r) { m[r * 4] = rows[r].x; m[r * 4 + 1] = rows[r].y; m[r * 4 + 2] = rows[r].z; m[r * 4 + 3] = 0.0f; }
    m[12] = p.x; m[13] = p.y; m[14] = p.z; m[15] = 1.0f;
}

static void TestMatrices() {
    using namespace arrowflight;
    const V3 h{0, 0, 1};
    float g[16], o[16];
    GameMatrix(h, 2.0f, M(V3{1, 1, 3}), g);
    const V3 down = Unit3(V3{0, -0.5f, 1}, h);
    Expect(Compose(g, h, down, M(V3{0, 1.5f, 0}), o), "compose");
    const V3 axis{o[8], o[9], o[10]};
    Expect(Near(axis * 0.5f, down, 1e-4f), "the arrow's own axis turned onto our line, its scale kept", axis.y);
    Expect(NearF(Length(V3{o[0], o[1], o[2]}), 2.0f, 1e-4f) && NearF(Length(V3{o[4], o[5], o[6]}), 2.0f, 1e-4f), "the other axes keep their scale");
    Expect(Near(Place(o), M(V3{0, 1.5f, 0}), 1e-3f), "put at our place");
    Expect(NearF(Dot(V3{o[0], o[1], o[2]}, V3{o[8], o[9], o[10]}), 0.0f, 1e-3f), "still square");
    // the inverse
    float inv[16], id[16];
    Expect(InverseAffine(g, inv), "the inverse");
    Multiply(g, inv, id);
    bool ident = true;
    for (int i = 0; i < 16; ++i) ident = ident && NearF(id[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4f);
    Expect(ident, "M x M^-1 = 1");
    // a child half a metre along the arrow (its head) and 10 cm up, in the game: carried with ours, it is half a metre along OUR line
    float child[16];
    GameMatrix(h, 2.0f, Place(g) + M(V3{0, 0.1f, 0.5f}), child);
    float f[16];
    Expect(Follow(child, g, o, f), "follow");
    const V3 want = Place(o) + down * (0.5f * kUpm) + RotateFromTo(h, down, V3{0, 1, 0}) * (0.1f * kUpm);
    Expect(Near(Place(f), want, 1e-2f), "the child keeps its place against the root", Length(Place(f) - want));
    Expect(Near(V3{f[8], f[9], f[10]} * 0.5f, down, 1e-4f), "the child turned with it");
    // already moved by the game's own parenting: near ours, not the game's
    Expect(AlreadyFollows(f, g, o, 0.01f * kUpm), "a child the function computed from our root");
    Expect(!AlreadyFollows(child, g, o, 0.01f * kUpm), "a child still where the game's root is");
    Expect(AlreadyFollows(child, g, g, 0.01f * kUpm), "the roots are one place: nothing to move");
    float bad[16];
    std::memcpy(bad, g, sizeof bad);
    bad[5] = NAN;
    Expect(!Compose(bad, h, down, Place(o), o), "a matrix that is not finite is left alone");
}

// The game's arrow: from g0 along heading h at one unit of travel a step.
static V3 GameAt(V3 g0, V3 h, float travelM) { return g0 + h * (travelM * kUpm); }

static arrowflight::Shot ShotAt(V3 startM, V3 atM, bool monster, bool stick, V3 heroM, float gridDistM) {
    arrowflight::Shot s;
    s.ok = true;
    s.start = M(startM);
    s.at = M(atM);
    s.dir = arrowflight::Unit3(s.at - s.start, V3{1, 0, 0});
    s.theta = std::atan2(atM.z - heroM.z, atM.x - heroM.x);
    s.target = true;
    s.monster = monster;
    s.stick = stick;
    s.gridDist = gridDistM * kUpm;
    s.hero = M(heroM);
    s.upm = kUpm;
    return s;
}

static void TestAimedMonster() {
    using namespace arrowflight;
    // the hero at the origin on his ground (0), the bow at 1.5 m; a monster 8 m out along x, on a floor 2 m below - its body met at (8, -1, 0) as drawn,
    // its cell 8 m out in the game (the shear put aside: the cell along the heading)
    const V3 hero{0, 0, 0}, start{0.2f, 1.5f, 0.1f}, at{8.0f, -1.0f, 0.0f};
    Track tr;
    tr.shot = ShotAt(start, at, true, false, hero, 8.0f);
    tr.cls = kArrow;
    const V3 h = Heading(tr.shot.theta);
    const V3 g0 = M(V3{0.4f, 1.0f, 0.0f});   // the game's arrow begins at the hero's chest, 0.4 m out
    Pose p = Step(tr, GameAt(g0, h, 0.0f));
    Expect(Near(p.pos, tr.shot.start, 1e-3f), "aimed: it begins at the bow");
    Expect(p.aimed, "aimed before its heading is read (the shot's own, presumed)");
    p = Step(tr, GameAt(g0, h, 3.8f));   // half the way: the cell is 8 m from the hero, the game's arrow began 0.4 m out - 7.6 m to go
    Expect(tr.decided && tr.aimed, "the heading read: the shot's own");
    Expect(NearF(p.u, 0.5f, 1e-3f), "half the game's way, half ours", p.u);
    Expect(Near(p.pos, (tr.shot.start + tr.shot.at) * 0.5f, 1e-2f), "half way along our line");
    Expect(Near(p.dir, tr.shot.dir, 1e-4f) && p.dir.y < -0.2f, "pointing down the line", p.dir.y);
    p = Step(tr, GameAt(g0, h, 7.6f));
    Expect(NearF(p.u, 1.0f, 1e-3f) && Near(p.pos, tr.shot.at, 1e-2f), "at the monster's body when the game's arrow is at its cell", p.u);
    p = Step(tr, GameAt(g0, h, 9.5f));
    Expect(!p.stuck && p.u > 1.2f && p.pos.y < tr.shot.at.y, "a miss flies on along the line (the game kills a missile that hits)", p.u);
    Expect(Near(p.gameDir, h, 1e-3f), "the game's own heading read from its travel");
}

static void TestAimedGround() {
    using namespace arrowflight;
    const V3 hero{0, 0, 0}, start{0.2f, 1.5f, 0.0f}, at{6.0f, 0.0f, 3.0f};
    for (int known = 0; known < 2; ++known) {
        Track tr;
        tr.shot = ShotAt(start, at, false, known != 0, hero, std::hypot(6.0f, 3.0f));
        tr.cls = kFireArrow;
        const V3 h = Heading(tr.shot.theta);
        const V3 g0 = M(V3{0, 1.0f, 0});
        const float span = std::hypot(6.0f, 3.0f);
        Step(tr, GameAt(g0, h, 0.0f));   // (its first frame: where the game's arrow began)
        Pose p = Step(tr, GameAt(g0, h, span));
        Expect(Near(p.pos, tr.shot.at, 1e-2f), "at the ground met", known);
        p = Step(tr, GameAt(g0, h, span + 4.0f));
        if (known) Expect(p.stuck && Near(p.pos, tr.shot.at, 1e-2f), "known ground: it stays stuck where it met it");
        else Expect(!p.stuck && arrowflight::Length(p.pos - tr.shot.at) > 3.0f * kUpm, "a guessed ground: it flies on past it");
    }
}

static void TestBlend() {
    using namespace arrowflight;
    Track tr;
    tr.shot.ok = true;
    tr.shot.start = M(V3{0.25f, 1.45f, -0.1f});
    tr.shot.theta = 0.6f;
    tr.shot.dir = Heading(0.6f);
    tr.shot.upm = kUpm;
    tr.shot.hero = M(V3{0, 0, 0});
    tr.cls = kArrow;
    const V3 h = Heading(0.6f);
    const V3 g0 = M(V3{0.3f, 1.0f, 0.2f});
    Params pr;
    pr.straight = false;   // ([bow] arrow_flight_straight off: the shot's own arrow blends too, as 0.179 drew it)
    Pose p = Step(tr, GameAt(g0, h, 0.0f), pr);
    Expect(!p.aimed && Near(p.pos, tr.shot.start, 1e-3f), "blend: it begins at the bow");
    p = Step(tr, GameAt(g0, h, pr.blendM * 0.5f), pr);
    const V3 g = GameAt(g0, h, pr.blendM * 0.5f);
    Expect(NearF(p.u, 0.5f, 1e-3f) && Near(p.pos, g + (tr.shot.start - g0) * 0.5f, 1e-2f), "half joined half way", p.u);
    Expect(std::isfinite(p.dir.x + p.dir.y + p.dir.z) && NearF(Length(p.dir), 1.0f, 1e-3f) && Dot(p.dir, h) > 0.9f, "it points along its way");
    p = Step(tr, GameAt(g0, h, pr.blendM + 1.0f), pr);
    Expect(Near(p.pos, GameAt(g0, h, pr.blendM + 1.0f), 1e-3f) && Near(p.dir, h, 1e-3f), "joined: the game's own place and heading");
    // continuous: no jump between steps
    Track t2 = tr;
    t2.started = false;
    t2.decided = false;
    V3 last = Step(t2, GameAt(g0, h, 0.0f), pr).pos;
    float worst = 0.0f;
    for (int i = 1; i <= 200; ++i) {
        const V3 q = Step(t2, GameAt(g0, h, i * 0.02f), pr).pos;
        worst = std::fmax(worst, Length(q - last));
        last = q;
    }
    Expect(worst < 0.05f * kUpm, "blend: no jump along the way", worst / kUpm);
}

// The shot's own arrow, the line met nothing ([bow] arrow_flight_straight): straight along the line from the bow - no bend onto the game's path beside it.
static void TestLine() {
    using namespace arrowflight;
    Track tr;
    tr.shot.ok = true;
    tr.shot.start = M(V3{0.25f, 1.45f, -0.1f});
    tr.shot.theta = 0.6f;
    tr.shot.dir = Unit3(V3{std::cos(0.6f), 0.2f, std::sin(0.6f)}, Heading(0.6f));   // a little up, into the air: nothing met
    tr.shot.upm = kUpm;
    tr.cls = kArrow;
    const V3 h = Heading(0.6f);
    const V3 g0 = M(V3{0.3f, 1.0f, 0.2f});
    Pose p = Step(tr, GameAt(g0, h, 0.0f));
    Expect(p.mode == Mode::Line && Near(p.pos, tr.shot.start, 1e-3f), "line: it begins at the bow");
    float worstOff = 0.0f;
    for (int i = 1; i <= 60; ++i) {
        p = Step(tr, GameAt(g0, h, i * 0.25f));
        const V3 rel = p.pos - tr.shot.start;
        const V3 off = rel - tr.shot.dir * Dot(rel, tr.shot.dir);
        worstOff = std::fmax(worstOff, Length(off));
        Expect(Near(p.dir, tr.shot.dir, 1e-4f), "line: it points along the line all the way");
    }
    Expect(worstOff < 1e-3f * kUpm, "line: never off the line (no bend)", worstOff / kUpm);
    const V3 flat{p.pos.x - tr.shot.start.x, 0.0f, p.pos.z - tr.shot.start.z};
    Expect(NearF(Length(flat), 15.0f * kUpm, 0.01f * kUpm), "line: as far across the ground as the game's arrow came", Length(flat) / kUpm);
    // straight up: no faster than the game's arrow at minAcross
    Track up = tr;
    up.started = up.decided = false;
    up.shot.dir = V3{0.0f, 1.0f, 0.0f};
    Step(up, GameAt(g0, h, 0.0f));
    const Pose q = Step(up, GameAt(g0, h, 1.0f));
    Expect(std::isfinite(q.pos.y) && NearF((q.pos.y - up.shot.start.y) / kUpm, 1.0f / Params{}.minAcross, 1e-3f), "line: straight up, bounded", (q.pos.y - up.shot.start.y) / kUpm);
}

static void TestNotTheShotsOwn() {
    using namespace arrowflight;
    const V3 hero{0, 0, 0}, start{0.2f, 1.5f, 0.0f}, at{8.0f, 0.0f, 0.0f};
    // a Multiple Shot's side arrow: 15 degrees off the heading given - not aimed at the point met, it joins its own path
    Track side;
    side.shot = ShotAt(start, at, true, false, hero, 8.0f);
    side.cls = kMultiArrow;
    const V3 h15 = Heading(side.shot.theta + 15.0f * 0.0174533f);
    const V3 g0 = M(V3{0.3f, 1.0f, 0.0f});
    Step(side, GameAt(g0, h15, 0.0f));
    const Pose p = Step(side, GameAt(g0, h15, 4.0f));
    Expect(side.decided && !side.aimed && !p.aimed, "a side arrow is not aimed at the point met");
    Expect(Near(p.pos, GameAt(g0, h15, 4.0f), 1e-3f), "it is on its own path past the blend");
    // the centre one of the same shot: aimed
    Track mid;
    mid.shot = side.shot;
    mid.cls = kMultiArrow;
    Step(mid, GameAt(g0, Heading(mid.shot.theta), 0.0f));
    Step(mid, GameAt(g0, Heading(mid.shot.theta), 1.0f));
    Expect(mid.aimed, "the shot's own arrow is aimed");
    // Guided Arrow homes: never aimed
    Track guided;
    guided.shot = side.shot;
    guided.cls = kGuidedArrow;
    Step(guided, GameAt(g0, Heading(guided.shot.theta), 0.0f));
    const Pose q = Step(guided, GameAt(g0, Heading(guided.shot.theta), 1.0f));
    Expect(!q.aimed && !guided.aimed, "Guided Arrow follows the game");
}

// The whole: [bow] pitch_aim's decision on a stairway scene, and our arrow sent to what it met.
static void TestWithPitch() {
    using namespace bowpitch;
    Scene sc;
    sc.hero = true;
    sc.heroGx = 0.0f;
    sc.heroGz = 0.0f;
    sc.heroY = 0.0f;
    sc.s[sc.ns++] = {0.0f, 0.0f, 0.0f};
    // a monster 8 m out, a floor 1.5 m below: drawn at its cell + h * shear on x and z
    Monster& mon = sc.m[sc.nm++];
    mon.id = 99;
    mon.gx = 8.0f;
    mon.gz = 0.0f;
    mon.placed = true;
    mon.feetY = -1.5f;
    const V3 feet = GridToGround(8.0f, 0.0f, -1.5f);
    sc.s[sc.ns++] = {feet.x, feet.z, feet.y};
    Capsule& c = sc.c[sc.nc++];
    c.a = feet + V3{0, 0.3f, 0};
    c.b = feet + V3{0, 1.4f, 0};
    c.r = 0.35f;
    c.monster = 0;
    const V3 o{0.2f, 1.5f, 0.0f};
    const V3 aimAt = feet + V3{0, 0.9f, 0};
    const Result r = Decide(sc, o, aimAt - o, Params{});
    Expect(r.hit == Hit::Monster && r.turned, "pitch: the monster below is met");
    arrowflight::Track tr;
    tr.cls = arrowflight::kArrow;
    tr.shot.ok = true;
    tr.shot.start = o * kUpm;
    tr.shot.dir = arrowflight::Unit3(aimAt - o, V3{1, 0, 0});
    tr.shot.theta = r.theta;
    tr.shot.target = true;
    tr.shot.at = r.at * kUpm;
    tr.shot.monster = true;
    tr.shot.gridDist = r.gridDist * kUpm;
    tr.shot.hero = V3{0, 0, 0};
    tr.shot.upm = kUpm;
    const V3 h = arrowflight::Heading(r.theta);
    const V3 g0 = V3{0.4f, 1.0f, 0.0f} * kUpm;
    arrowflight::Step(tr, g0);
    const float toCell = r.gridDist - 0.4f * std::cos(r.theta);   // the game's arrow at the monster's cell
    const arrowflight::Pose p = arrowflight::Step(tr, g0 + h * (toCell * kUpm));
    Expect(Near(p.pos, r.at * kUpm, 0.02f * kUpm), "our arrow is at the monster's drawn body when the game's reaches its cell", arrowflight::Length(p.pos - r.at * kUpm) / kUpm);
    Expect(p.dir.y < -0.15f, "and it flew down the stairs", p.dir.y);
}

// pitch_aim's ground met is told known (a sample stands within reach: our arrow may stick) or guessed (the hero's height where nothing is known: it flies on)
static void TestGroundKnown() {
    using namespace bowpitch;
    Scene sc;
    sc.hero = true;
    sc.s[sc.ns++] = {0.0f, 0.0f, 0.0f};   // the hero's feet
    sc.s[sc.ns++] = {6.0f, 0.0f, 0.0f};   // something standing 6 m out
    const V3 o{0.0f, 1.5f, 0.0f};
    Result near = Decide(sc, o, V3{6.0f, -1.5f, 0.0f}, Params{});
    Expect(near.hit == Hit::Floor && near.groundKnown, "the ground met beside something standing: known", near.at.x);
    Result far = Decide(sc, o, V3{14.0f, -1.5f, 0.3f}, Params{});
    Expect(far.hit == Hit::Floor && !far.groundKnown, "the ground met where nothing stands: a guess", far.at.x);
}

int main() {
    TestGroundKnown();
    TestClasses();
    TestRotate();
    TestMatrices();
    TestAimedMonster();
    TestAimedGround();
    TestBlend();
    TestLine();
    TestNotTheShotsOwn();
    TestWithPitch();
    if (g_fails) {
        std::printf("arrow_flight_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("arrow_flight_test: all passed\n");
    return 0;
}
