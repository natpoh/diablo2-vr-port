// simpler_sun_test.cpp - [render] sun_shadow_radius_m / sun_shadow_every, the pure part (vr/simpler_sun.h): which light is the sun's, which models
// cast into its map, when a kept map may stand for this frame's light, the frame decision, and the barrier that ends the map's drawing.
// The GPU side (the copies on a real list through the plugin's hooks) is tools/simpler_sun_warp_test.cpp.
//
//   simpler_sun_test

#include <cmath>
#include <cstdio>
#include <cstring>

#include "simpler_sun.h"

using namespace simplersun;

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) { ++g_failed; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    } while (0)

// row-vector matrices (clip = [x y z 1] * M), as the game's
static void Ortho(float m[16], float halfW, float cx, float cz, float depthScale = 0.001f) {
    // the sun straight down: x -> clip x, z -> clip y, height y -> depth
    std::memset(m, 0, 64);
    m[0] = 1.0f / halfW;      // x
    m[9] = 1.0f / halfW;      // z -> y
    m[6] = depthScale;        // y -> z
    m[12] = -cx / halfW;
    m[13] = -cz / halfW;
    m[14] = 0.5f;
    m[15] = 1.0f;
}
static void Perspective(float m[16]) {
    std::memset(m, 0, 64);
    m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[11] = 1.0f; m[14] = -0.1f;   // w = z
}
static void Identity(float m[16]) {
    std::memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void TestLight() {
    float o[16], p[16], v[16];
    Ortho(o, 100.0f, 0.0f, 0.0f);
    Perspective(p);
    Identity(v);
    CHECK(Orthographic(o));
    CHECK(!Orthographic(p));   // a spot / point light: never the sun's
    CHECK(Orthographic(v));    // a rigid view matrix alone passes too - why Filter asks both the culling and the drawing matrix
    float nan[16];
    std::memcpy(nan, o, 64);
    nan[5] = std::nanf("");
    CHECK(!Orthographic(nan));
    float q[16];
    std::memcpy(q, o, 64);
    q[11] = 0.01f;   // a sliver of perspective
    CHECK(!Orthographic(q));

    const float pt[3] = {50.0f, 2.0f, -25.0f};
    float c[3];
    Project(o, pt, c);
    CHECK(std::fabs(c[0] - 0.5f) < 1e-6f && std::fabs(c[1] + 0.25f) < 1e-6f && std::fabs(c[2] - 0.502f) < 1e-6f);
}

static void TestStill() {
    const float side = 4096.0f, center[3] = {10.0f, 0.0f, 20.0f}, reach = 30.0f;
    float a[16], b[16];
    Ortho(a, 100.0f, 10.0f, 20.0f);
    std::memcpy(b, a, 64);
    Shift s = LightShift(a, b, center, reach, side);
    CHECK(s.texels == 0.0f && s.depth == 0.0f);
    CHECK(Still(a, b, center, reach, side));
    // the light camera moved by one texel: 200 world units across 4096 texels -> 0.0488 units a texel
    const float texel = 200.0f / side;
    Ortho(b, 100.0f, 10.0f + texel, 20.0f);
    s = LightShift(a, b, center, reach, side);
    CHECK(std::fabs(s.texels - 1.0f) < 1e-3f);
    CHECK(!Still(a, b, center, reach, side));
    // a tenth of a texel (the sun's day drift between two frames): still
    Ortho(b, 100.0f, 10.0f + 0.1f * texel, 20.0f);
    CHECK(Still(a, b, center, reach, side));
    // the hero walked a step (0.5 world units = ~10 texels): moved
    Ortho(b, 100.0f, 10.5f, 20.0f);
    CHECK(!Still(a, b, center, reach, side));
    // the fit grew (zoom): the centre stays, the ring moves
    Ortho(b, 100.5f, 10.0f, 20.0f);
    s = LightShift(a, b, center, reach, side);
    CHECK(s.texels > 1.0f);
    CHECK(!Still(a, b, center, reach, side));
    // only the depth range changed
    Ortho(b, 100.0f, 10.0f, 20.0f, 0.0012f);
    s = LightShift(a, b, center, reach, side);
    CHECK(s.texels == 0.0f && s.depth > kStillDepth);
    CHECK(!Still(a, b, center, reach, side));
    // a perspective matrix is never "still" with the sun's
    float p[16];
    Perspective(p);
    CHECK(!Still(a, p, center, reach, side));
    CHECK(!Still(p, p, center, reach, side));
    // a 2048 map: the same shift is half the texels
    Ortho(b, 100.0f, 10.0f + texel, 20.0f);
    CHECK(std::fabs(LightShift(a, b, center, reach, 2048.0f).texels - 0.5f) < 1e-3f);
}

static void TestSelect() {
    Hero h;
    h.ok = true;
    h.at[0] = 100.0f; h.at[1] = 5.0f; h.at[2] = -40.0f;
    h.upm = 6.0f;            // world units a metre
    h.entity = 0x1234u;
    const float nearP[3] = {100.0f + 6.0f * 9.0f, 0.0f, -40.0f};      // 9 m east
    const float farP[3] = {100.0f, 0.0f, -40.0f + 6.0f * 13.0f};      // 13 m south
    const float edge[3] = {100.0f + 6.0f * 6.0f, 0.0f, -40.0f + 6.0f * 8.0f};   // 10 m exactly (6-8-10)
    const float high[3] = {100.0f + 6.0f * 9.0f, 900.0f, -40.0f};    // 9 m away on the ground, high up: the ground distance counts
    CHECK(Select(0.0f, h, 7, kNoEntity, farP) == Cast::kOff);
    CHECK(Select(-3.0f, h, 7, kNoEntity, farP) == Cast::kOff);
    CHECK(Select(10.0f, h, 7, kNoEntity, nearP) == Cast::kKeep);
    CHECK(Select(10.0f, h, 7, kNoEntity, farP) == Cast::kCull);
    CHECK(Culled(Select(10.0f, h, 7, kNoEntity, farP)));
    CHECK(Select(10.0f, h, 7, kNoEntity, edge) == Cast::kKeep);
    CHECK(Select(10.0f, h, 7, kNoEntity, high) == Cast::kKeep);
    CHECK(Select(14.0f, h, 7, kNoEntity, farP) == Cast::kKeep);
    // the hero and what hangs on him: never left out, however farP his origin reads
    CHECK(Select(10.0f, h, 0x1234u, kNoEntity, farP) == Cast::kHero);
    CHECK(Select(10.0f, h, 0x00A01234u, kNoEntity, farP) == Cast::kHero);   // (the entity's version bits above the low 20 do not matter)
    CHECK(Select(10.0f, h, 99, 0x1234u, farP) == Cast::kHero);
    CHECK(Select(10.0f, h, 99, 0x7701234u, farP) == Cast::kHero);
    CHECK(!Culled(Select(10.0f, h, 0x1234u, kNoEntity, farP)));
    // a radius under the minimum is the minimum: 1 m -> 4 m
    const float m3[3] = {100.0f + 6.0f * 3.5f, 0.0f, -40.0f}, m5[3] = {100.0f + 6.0f * 5.0f, 0.0f, -40.0f};
    CHECK(Select(1.0f, h, 7, kNoEntity, m3) == Cast::kKeep);
    CHECK(Select(1.0f, h, 7, kNoEntity, m5) == Cast::kCull);
    // the hero's place not known: nothing left out
    Hero lost = h;
    lost.ok = false;
    CHECK(Select(10.0f, lost, 7, kNoEntity, farP) == Cast::kNoHero);
    lost = h;
    lost.upm = 0.0f;
    CHECK(Select(10.0f, lost, 7, kNoEntity, farP) == Cast::kNoHero);
    lost = h;
    lost.at[0] = std::nanf("");
    CHECK(Select(10.0f, lost, 7, kNoEntity, farP) == Cast::kNoHero);
    // the hero's entity not known: the distance alone (his origin is at his place anyway)
    Hero anon = h;
    anon.entity = kNoEntity;
    CHECK(Select(10.0f, anon, 0x1234u, kNoEntity, farP) == Cast::kCull);
    CHECK(Select(10.0f, anon, kNoEntity, kNoEntity, nearP) == Cast::kKeep);   // (a model with no entity id is not mistaken for the hero)
    // a model whose place does not read as a number: kept
    const float bad[3] = {std::nanf(""), 0.0f, 0.0f};
    CHECK(Select(10.0f, h, 7, kNoEntity, bad) == Cast::kKeep);
    // the scale: the same world distance is 13 m at 6 units a metre, 6.5 m at 12
    Hero big = h;
    big.upm = 12.0f;
    CHECK(Select(10.0f, big, 7, kNoEntity, farP) == Cast::kKeep);
}

static void TestMode() {
    Hero h;
    h.ok = true;
    h.upm = 6.0f;
    h.entity = 0x55u;
    const float farP[3] = {6.0f * 20.0f, 0.0f, 0.0f}, nearP[3] = {6.0f * 10.0f, 0.0f, 0.0f};
    // none: every sun model left out, but while its map is being found
    CHECK(SelectMode(kModeNone, false, 0.0f, h, 7, kNoEntity, nearP) == Cast::kNoShadow);
    CHECK(Culled(SelectMode(kModeNone, false, 0.0f, h, 0x55u, kNoEntity, nearP)));   // (the hero's too: no sun shadow at all)
    CHECK(SelectMode(kModeNone, true, 0.0f, h, 7, kNoEntity, nearP) == Cast::kLearn);
    CHECK(!Culled(SelectMode(kModeNone, true, 0.0f, h, 7, kNoEntity, farP)));
    // near: the radius, 15 m when 0
    CHECK(SelectMode(kModeNear, false, 0.0f, h, 7, kNoEntity, nearP) == Cast::kKeep);
    CHECK(SelectMode(kModeNear, false, 0.0f, h, 7, kNoEntity, farP) == Cast::kCull);
    CHECK(SelectMode(kModeNear, false, 25.0f, h, 7, kNoEntity, farP) == Cast::kKeep);
    CHECK(SelectMode(kModeNear, false, 8.0f, h, 7, kNoEntity, nearP) == Cast::kCull);
    CHECK(SelectMode(kModeNear, false, 8.0f, h, 0x55u, kNoEntity, farP) == Cast::kHero);
    // as the game: nothing left out, whatever the radius key says
    CHECK(SelectMode(kModeGame, false, 8.0f, h, 7, kNoEntity, farP) == Cast::kOff);
    // the lit clear: the reversed depth's far plane
    CHECK(kLitDepth == 0.0f);
    CHECK(LitClearNeeded(kModeNone, false, true, 0x1, 1.0f));
    CHECK(LitClearNeeded(kModeNone, false, true, 0x3, 0.5f));    // (depth + stencil cleared: ours the depth only)
    CHECK(!LitClearNeeded(kModeNone, false, true, 0x1, 0.0f));   // the game's own clear is lit already: none
    CHECK(!LitClearNeeded(kModeNone, true, true, 0x1, 1.0f));    // its map being found
    CHECK(!LitClearNeeded(kModeNone, false, false, 0x1, 1.0f));  // not the sun's map
    CHECK(!LitClearNeeded(kModeNone, false, true, 0x2, 1.0f));   // a stencil clear only
    CHECK(!LitClearNeeded(kModeNear, false, true, 0x1, 1.0f));
    CHECK(!LitClearNeeded(kModeGame, false, true, 0x1, 1.0f));
}

static void TestDecide() {
    // every frame asked, not F4: drawn
    CHECK(Decide(true, 1, 1, true, true, true).plan == Plan::kDraw);
    CHECK(Decide(true, 1, 1, true, true, true).why == Why::kOff);
    CHECK(Decide(false, 3, 1, true, true, true).why == Why::kNotF4);
    // every 2nd: the frame right after a drawn one may keep it
    CHECK(Decide(true, 2, 1, true, true, true).plan == Plan::kHold);
    CHECK(Decide(true, 2, 2, true, true, true).why == Why::kTurn);
    CHECK(Decide(true, 3, 2, true, true, true).plan == Plan::kHold);
    CHECK(Decide(true, 3, 3, true, true, true).plan == Plan::kDraw);
    // no copy kept, the light not seen yet, the light moved: drawn
    CHECK(Decide(true, 2, 1, false, true, true).why == Why::kNoKeep);
    CHECK(Decide(true, 2, 1, true, false, true).why == Why::kNoLight);
    CHECK(Decide(true, 2, 1, true, true, false).why == Why::kMoved);
    CHECK(Decide(true, 8, 1000000, true, true, true).why == Why::kTurn);
    for (int w = 0; w < (int)Why::kCount; ++w) CHECK(std::strcmp(WhyText((Why)w), "?") != 0);
}

// the frame sequence the runtime runs (vr/simpler_sun.cpp DecideLocked, without the device): every N, the light still, the keep valid after each
// drawn frame's copy
static int Sequence(int every, const bool* still, int frames, char* out) {
    Frames f;
    int held = 0;
    for (int i = 0; i < frames; ++i) {
        f.Next();
        CHECK(!f.Has());
        const Decision d = f.Take(Decide(true, every, f.DrawnAgo(), f.keepValid, true, still ? still[i] : true));
        CHECK(f.Has());
        if (d.plan == Plan::kDraw) f.keepValid = true;   // (its copy recorded at its barrier)
        out[i] = d.plan == Plan::kDraw ? 'D' : 'k';
        held += d.plan == Plan::kHold;
    }
    out[frames] = 0;
    return held;
}

static void TestFrames() {
    char s[32];
    CHECK(Sequence(1, nullptr, 6, s) == 0 && std::strcmp(s, "DDDDDD") == 0);
    CHECK(Sequence(2, nullptr, 6, s) == 3 && std::strcmp(s, "DkDkDk") == 0);
    CHECK(Sequence(3, nullptr, 7, s) == 4 && std::strcmp(s, "DkkDkkD") == 0);
    // walking from frame 3 to 5: drawn each of them, kept again once it stands
    const bool walk[8] = {true, true, false, false, false, true, true, true};
    Sequence(3, walk, 8, s);
    CHECK(std::strcmp(s, "DkDDDkkD") == 0);
    printf("  frames, every 3, walking at frames 3-5: %s\n", s);
    // a reset (the keys changed): drawn first
    Frames f;
    f.Next();
    f.Take(Decide(true, 2, f.DrawnAgo(), false, true, true));
    f.keepValid = true;
    f.Reset();
    f.Next();
    CHECK(!f.Has());
    CHECK(f.DrawnAgo() >= 1000000);
    CHECK(Decide(true, 2, f.DrawnAgo(), f.keepValid, true, true).plan == Plan::kDraw);
}

static void TestBarrier() {
    int map = 0, other = 0;
    bool whole = true;
    Transition b[4];
    b[0] = {0, 0, &other, kAllSubresources, kDepthWrite, 0x80};
    b[1] = {0, 0, &map, kAllSubresources, 0x80, kDepthWrite};          // into depth-write: the drawing begins, not ends
    b[2] = {0, 0, &map, kAllSubresources, kDepthWrite, 0x80 | 0x40};    // out of it: ends
    b[3] = {0, 0, &map, kAllSubresources, kDepthWrite, 0x80};
    CHECK(MapDone(b, 4, &map, 1, &whole) == 2 && whole);
    CHECK(MapDone(b, 2, &map, 1, &whole) == -1 && !whole);
    CHECK(MapDone(b, 4, nullptr, 1, &whole) == -1);
    CHECK(MapDone(nullptr, 4, &map, 1, &whole) == -1);
    // split barriers are passed over
    Transition sp = {0, 1, &map, kAllSubresources, kDepthWrite, 0x80};
    CHECK(MapDone(&sp, 1, &map, 1, &whole) == -1);
    // an aliasing / UAV barrier (type 1, 2) is not a transition
    Transition al = {1, 0, &map, kAllSubresources, kDepthWrite, 0x80};
    CHECK(MapDone(&al, 1, &map, 1, &whole) == -1);
    // depth-write kept with a read added (depth read + write is not an end)
    Transition dw = {0, 0, &map, kAllSubresources, kDepthWrite, kDepthWrite | 0x20};
    CHECK(MapDone(&dw, 1, &map, 1, &whole) == -1);
    // one slice of a map of two: found, not whole (no copy made then); of a map of one: whole
    Transition s0 = {0, 0, &map, 0, kDepthWrite, 0x80};
    CHECK(MapDone(&s0, 1, &map, 2, &whole) == 0 && !whole);
    CHECK(MapDone(&s0, 1, &map, 1, &whole) == 0 && whole);
    // into a copy (the game's own copy of its map): an end of the drawing too
    Transition cp = {0, 0, &map, kAllSubresources, kDepthWrite, kCopySource};
    CHECK(MapDone(&cp, 1, &map, 1, &whole) == 0);
    // drawn again after its end (a second part): into depth-write from a read state - not from depth-write, not another resource
    CHECK(Reopens(&b[1], 1, &map));
    CHECK(!Reopens(&b[2], 1, &map));
    CHECK(!Reopens(&b[0], 1, &map));
    CHECK(!Reopens(&dw, 1, &map));
    CHECK(!Reopens(&b[1], 1, &other));
    CHECK(!Reopens(&al, 1, &map));
    CHECK(Reopens(b, 4, &map));
    // ... then nothing is kept: every frame draws
    CHECK(Decide(true, 2, 1, true, true, true, true).why == Why::kParts);
    CHECK(Decide(true, 1, 1, true, true, true, true).why == Why::kOff);
    CHECK(Decide(false, 2, 1, true, true, true, true).why == Why::kNotF4);
}

int main() {
    TestLight();
    TestStill();
    TestSelect();
    TestMode();
    TestDecide();
    TestFrames();
    TestBarrier();
    printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
