// [render] symmetric_projection (vr/eye_fov.h CentredPair, xr.cpp CentrePair): both eyes of native OpenXR's pair drawn and submitted with one
// centred frustum that holds both, whatever the route drew them with - the game's light binning (light_bin_c, 32 x 20 tiles) rebuilds a tile's
// rays and projects a light's bounds from the projection's x and y scales alone, so an off-axis eye lights the wrong tiles. Checked: the pair
// holds every ray of both eyes and has no off-axis term; a pair already centred (runtime_fov=1 on parallel displays) comes back unchanged; the
// routes that are off-axis today (symmetric_fov + fov_crop - also what frame generation forces -, canted displays, angles by hand) and a model
// of the binning's miss: how many tiles a light lands from its own with and without the off-axis term. Pure; run: symmetric_projection_test.exe
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "eye_fov.h"
#include "eye_frame.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace eyefov;
constexpr float kD = 3.14159265f / 180.0f;
bool Near(float a, float b, float eps = 2e-4f) { return std::fabs(a - b) <= eps; }
Angles Deg(float l, float r, float u, float d) { return {l * kD, r * kD, u * kD, d * kD}; }
Tan T(const Angles& a) { return FromAngles(a.l, a.r, a.u, a.d); }
// `outer` holds every ray of `inner` (tangents, a hair of slack for the atan / tan round trip)
bool Holds(const Tan& outer, const Tan& inner) {
    constexpr float e = 1e-5f;
    return outer.l <= inner.l + e && outer.r >= inner.r - e && outer.u >= inner.u - e && outer.d <= inner.d + e;
}

// A Quest 3 (a player's runtime angles, 0.20x log: the left eye -54 / +40, up 44, down 55; the right eye its mirror)
const Angles kQ3L = Deg(-54.0f, 40.0f, 44.0f, -55.0f), kQ3R = Deg(-40.0f, 54.0f, 44.0f, -55.0f);

void TestEnclosing() {
    const PairAngles p = CentredPair(kQ3L, kQ3R);
    CHECK(p.changed);
    for (int e = 0; e < 2; ++e) {
        const Tan c = T(p.e[e]);
        CHECK(IsCentred(c));
        CHECK(Holds(c, T(kQ3L)) && Holds(c, T(kQ3R)));   // every ray either eye showed
        const Proj pr = Projection(c);
        CHECK(Near(pr.offX, 0.0f) && Near(pr.offY, 0.0f));
    }
    // the widest side each way, the tallest each way - no wider
    CHECK(Near(p.e[0].l, -54.0f * kD, 1e-5f) && Near(p.e[0].r, 54.0f * kD, 1e-5f) && Near(p.e[0].u, 55.0f * kD, 1e-5f) && Near(p.e[0].d, -55.0f * kD, 1e-5f));
    CHECK(p.e[0].l == p.e[1].l && p.e[0].r == p.e[1].r && p.e[0].u == p.e[1].u && p.e[0].d == p.e[1].d);   // the same frustum in both eyes
    // the same as runtime_fov=1's Centred (the route that is centred already)
    const Tan c = Centred(T(kQ3L), T(kQ3R));
    CHECK(Near(T(p.e[0]).l, c.l) && Near(T(p.e[0]).u, c.u));
    // not mirrored eyes: still one frustum holding both
    const Angles odd = Deg(-40.0f, 58.0f, 42.0f, -50.0f);
    const PairAngles q = CentredPair(kQ3L, odd);
    CHECK(IsCentred(T(q.e[0])) && Holds(T(q.e[0]), T(kQ3L)) && Holds(T(q.e[0]), T(odd)));
    CHECK(Near(q.e[0].r, 58.0f * kD, 1e-5f) && Near(q.e[0].d, -55.0f * kD, 1e-5f));
}

void TestAlreadyCentred() {
    // runtime_fov=1 on parallel displays: both eyes already Centred of the runtime's views - the pair comes back as it was (CentrePair: "already
    // centred, nothing changes")
    const Tan c = Centred(T(kQ3L), T(kQ3R));
    const Angles a{std::atan(c.l), std::atan(c.r), std::atan(c.u), std::atan(c.d)};
    const PairAngles p = CentredPair(a, a);
    CHECK(!p.changed);
    CHECK(Near(p.e[0].l, a.l, 1e-6f) && Near(p.e[0].r, a.r, 1e-6f) && Near(p.e[0].u, a.u, 1e-6f) && Near(p.e[0].d, a.d, 1e-6f));
    // a PS VR2 (50 / 45 / 50 / 50) through fov_override_centred: HandCentred's frustum, centred already
    const HandDraw h = HandCentred({50.0f, 45.0f, 50.0f, 50.0f});
    CHECK(IsCentred(h.drawn));
    const Angles ha{std::atan(h.drawn.l), std::atan(h.drawn.r), std::atan(h.drawn.u), std::atan(h.drawn.d)};
    CHECK(!CentredPair(ha, ha).changed);
}

void TestOldRoute() {
    // runtime_fov=0, or runtime_fov=1 with frame generation's bridge in (xr.cpp RuntimeFovNow false): symmetric_fov makes each eye as wide left
    // as right, but up and down stay the runtime's, and fov_crop narrows the picture to the window's shape about ITS OWN middle - a Quest 3 at
    // 16:9 keeps a vertical off-axis term of about -0.3 (the light bins three of the twenty rows off at the edge of the picture)
    const Options o{false, true, true, 16.0f / 9.0f};
    const Tan dl = Drawn(T(kQ3L), o), dr = Drawn(T(kQ3R), o);
    CHECK(Near(dl.l, -dl.r) && Near(dr.l, -dr.r));   // horizontally centred
    const Proj pl = Projection(dl);
    CHECK(pl.offY < -0.25f && pl.offY > -0.35f);   // vertically not
    CHECK(!IsCentred(dl));
    // symmetric_projection: one centred frustum holding both cropped views - nothing the crop showed is lost (the band the crop blacked out
    // above is drawn now: the headset shows it), no off-axis term
    const PairAngles p = CentredPair({std::atan(dl.l), std::atan(dl.r), std::atan(dl.u), std::atan(dl.d)},
                                     {std::atan(dr.l), std::atan(dr.r), std::atan(dr.u), std::atan(dr.d)});
    CHECK(p.changed);
    const Tan c = T(p.e[0]);
    CHECK(IsCentred(c) && Holds(c, dl) && Holds(c, dr));
    CHECK(Near(c.u, -dl.d) && Near(c.r, dl.r));   // as tall up as the crop went down (-45 deg), as wide as before
    // the cost: the same pixels on a taller angle - the picture's lines per tangent up and down, against the crop's
    CHECK((c.u - c.d) > (dl.u - dl.d) * 1.25f && (c.u - c.d) < (dl.u - dl.d) * 1.35f);
}

void TestCanted() {
    // a headset whose displays are turned out 5 deg each way (Pimax-like): canted_eyes draws both eyes in the head's frame, each eye's fov the
    // frustum of its true view seen from there - centred views come out off-axis (the left eye's to the left)
    const float h = 2.5f * kD;
    const eyeframe::Quat q[2] = {{0.0f, std::sin(h), 0.0f, std::cos(h)}, {0.0f, -std::sin(h), 0.0f, std::cos(h)}};   // + about y = turned left
    const eyeframe::Fov want[2] = {{-50.0f * kD, 50.0f * kD, 50.0f * kD, -50.0f * kD}, {-50.0f * kD, 50.0f * kD, 50.0f * kD, -50.0f * kD}};
    const eyeframe::Frame fr = eyeframe::Make(q, want, true);
    CHECK(fr.canted);
    const Angles a{fr.fov[0].l, fr.fov[0].r, fr.fov[0].u, fr.fov[0].d}, b{fr.fov[1].l, fr.fov[1].r, fr.fov[1].u, fr.fov[1].d};
    const float oa = Projection(T(a)).offX, ob = Projection(T(b)).offX;
    CHECK(std::fabs(oa) > 0.03f && std::fabs(ob) > 0.03f && oa * ob < 0.0f && Near(oa, -ob, 1e-3f));   // off-axis, mirrored
    const PairAngles p = CentredPair(a, b);
    CHECK(p.changed && IsCentred(T(p.e[0])) && Holds(T(p.e[0]), T(a)) && Holds(T(p.e[0]), T(b)));
    // parallel displays: Make leaves the fovs as they were - a centred route stays centred
    const eyeframe::Quat same[2] = {{}, {}};
    const eyeframe::Frame par = eyeframe::Make(same, want, true);
    CHECK(!par.canted);
    CHECK(!CentredPair({par.fov[0].l, par.fov[0].r, par.fov[0].u, par.fov[0].d}, {par.fov[1].l, par.fov[1].r, par.fov[1].u, par.fov[1].d}).changed);
}

void TestHand() {
    // fov_override without fov_override_centred (each eye exactly the hand's angles, off-axis): a player's 54 / 40 / 44 / 55
    const Angles l = FromHand({54.0f, 40.0f, 44.0f, 55.0f}, 0), r = FromHand({54.0f, 40.0f, 44.0f, 55.0f}, 1);
    CHECK(!IsCentred(T(l)) && !IsCentred(T(r)));
    const PairAngles p = CentredPair(l, r);
    CHECK(p.changed && IsCentred(T(p.e[0])) && Holds(T(p.e[0]), T(l)) && Holds(T(p.e[0]), T(r)));
    // the same frustum fov_override_centred draws
    const HandDraw d = HandCentred({54.0f, 40.0f, 44.0f, 55.0f});
    CHECK(Near(T(p.e[0]).l, d.drawn.l) && Near(T(p.e[0]).u, d.drawn.u) && Near(T(p.e[0]).d, d.drawn.d));
}

// The binning's miss, modelled: a view-space direction's tangent lands on the picture at sx * t - off (Ndc). The game's tile rays and light
// bounds take sx alone (the inverse projection's diagonal), so it puts the light at sx * t: off columns' worth of NDC away - off * tiles / 2
// tiles. The tile a direction falls in, of `tiles` across -1 .. 1.
int TileOf(float ndc, int tiles) { return std::clamp((int)std::floor((ndc + 1.0f) * 0.5f * (float)tiles), 0, tiles - 1); }
int Miss(const Tan& t, float tanX, float tanY) {
    const Proj p = Projection(t);
    const int trueCol = TileOf(Ndc(tanX, p.sx, p.offX), 32), trueRow = TileOf(Ndc(tanY, p.sy, p.offY), 20);
    const int gameCol = TileOf(Ndc(tanX, p.sx, 0.0f), 32), gameRow = TileOf(Ndc(tanY, p.sy, 0.0f), 20);   // the off-axis term left out
    return std::abs(trueCol - gameCol) + std::abs(trueRow - gameRow);
}
void TestBinningMiss() {
    // the old route's Quest 3 eye at 16:9 (vertical off-axis about -0.3): a light on the ground below the middle is binned three rows off
    const Tan dl = Drawn(T(kQ3L), {false, true, true, 16.0f / 9.0f});
    const int miss = Miss(dl, 0.3f, -0.5f);
    CHECK(miss >= 2 && miss <= 4);
    // runtime_fov=2's Quest 3 eye (off-axis -0.15 across): columns off
    CHECK(Miss(T(kQ3L), 0.4f, -0.2f) >= 2);
    // centred (symmetric_projection, or runtime_fov=1): never off, anywhere on the picture
    const Tan c = T(CentredPair(kQ3L, kQ3R).e[0]);
    int worst = 0;
    for (int i = -9; i <= 9; ++i)
        for (int j = -9; j <= 9; ++j) worst = std::max(worst, Miss(c, c.r * i / 10.0f, c.u * j / 10.0f));
    CHECK(worst == 0);
}

void TestDegenerate() {
    // the same eye twice, already centred, and a zero fov: no NaN, the pair is that eye
    const Angles z{0.0f, 0.0f, 0.0f, 0.0f};
    const PairAngles p = CentredPair(z, z);
    CHECK(!p.changed && p.e[0].l == 0.0f && p.e[1].u == 0.0f);
    const Angles sq = Deg(-45.0f, 45.0f, 45.0f, -45.0f);
    CHECK(!CentredPair(sq, sq).changed);
    CHECK(IsCentred(T(sq)) && !IsCentred(T(kQ3L)));
}
}  // namespace

int main() {
    TestEnclosing();
    TestAlreadyCentred();
    TestOldRoute();
    TestCanted();
    TestHand();
    TestBinningMiss();
    TestDegenerate();
    std::printf("symmetric_projection_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
