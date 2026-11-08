// unleashed_foveation_test - [render] foveated (shared/unleashed_foveation.h; MPL 2.0, bearstonem's fixed foveated rendering from D2R VR
// Unleashed): the shading-rate image's tiles per eye and per fov. Bear's rule word for word (FixedRate) equals ours (TileRate) where the
// picture is the runtime's own view; the lens centre comes from each eye's frustum (mirrored between the eyes of an asymmetric headset);
// where we draw wider than the runtime shows ([openxr] symmetric_fov, runtime_fov) the ellipse is measured in the shown part and the part
// never shown takes the coarsest rate; which draws and targets take the image. It prints how much of each eye is shaded coarsely - the
// most the GPU can save on the shading of the opaque world. Run: unleashed_foveation_test.exe
#include "../../shared/unleashed_foveation.h"
#include "../../vr/fullscreen_passes.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace unleashed::foveation;

static Tan Deg(float l, float r, float u, float d) {
    const float k = 3.14159265f / 180.0f;
    return {std::tan(l * k), std::tan(r * k), std::tan(u * k), std::tan(d * k)};
}

int main() {
    // ---- Bear's rule and ours agree where the whole picture is shown
    {
        int compared = 0, differ = 0;
        const float vps[][4] = {{0, 0, 2160, 2160}, {0, 0, 1448, 1448}, {40, 24, 1800, 1600}, {0, 0, 3840, 2160}};
        const float centres[][2] = {{.5f, .5f}, {.606f, .5f}, {.394f, .47f}, {.1f, .9f}};
        const float radii[] = {.35f, .55f, .70f, .85f};
        for (const auto& vp : vps)
            for (const auto& c : centres)
                for (float rad : radii)
                    for (uint32_t tile : {8u, 16u, 32u}) {
                        Region g;
                        g.cx = c[0]; g.cy = c[1]; g.ax = g.ay = .5f * rad; g.ok = true;
                        const uint32_t tw = Tiles(uint32_t(vp[0] + vp[2]) + 16, tile), th = Tiles(uint32_t(vp[1] + vp[3]) + 16, tile);
                        for (uint32_t y = 0; y < th; ++y)
                            for (uint32_t x = 0; x < tw; ++x) {
                                ++compared;
                                if (TileRate(x, y, tile, vp[0], vp[1], vp[2], vp[3], g, kRate2x2) !=
                                    FixedRate(x, y, tile, vp[0], vp[1], vp[2], vp[3], c[0], c[1], rad, kRate2x2)) ++differ;
                            }
                    }
        CHECK(compared > 100000 && differ == 0);
        std::printf("Bear's FixedRate vs TileRate: %d tiles compared, %d differ\n", compared, differ);
    }

    // ---- the lens centre from the eye's frustum (Bear's -l/(r-l), u/(u-d)), mirrored between the eyes
    {
        const Tan left = Deg(-57, 45, 50, -50), right = Deg(-45, 57, 50, -50);   // each eye wider on its outer side
        const Region gl = RegionFor(left, 0, 0, left, true, .55f), gr = RegionFor(right, 0, 0, right, true, .55f);
        CHECK(gl.ok && gr.ok);
        CHECK(std::abs(gl.cx - (-left.l / (left.r - left.l))) < 1e-6f && std::abs(gl.cy - left.u / (left.u - left.d)) < 1e-6f);
        CHECK(gl.cx > .55f && gr.cx < .45f && std::abs(gl.cx + gr.cx - 1.0f) < 1e-5f);   // the left eye's centre nearer the nose (right)
        CHECK(std::abs(gl.cy - .5f) < 1e-6f);
        CHECK(gl.x0 == 0 && gl.x1 == 1 && gl.y0 == 0 && gl.y1 == 1);                  // drawn as shown: all of it seen
        CHECK(std::abs(gl.ax - .275f) < 1e-6f && std::abs(gl.ay - .275f) < 1e-6f);    // Bear's half-axes
        std::printf("asymmetric eyes (-57/+45, mirrored): lens centre x left %.3f, right %.3f\n", gl.cx, gr.cx);
    }

    // ---- drawn wider than shown ([openxr] symmetric_fov: as wide on both sides; runtime_fov: one centred frustum for both)
    {
        const Tan runtimeL = Deg(-57, 45, 50, -50);
        const Tan drawn = {runtimeL.l, -runtimeL.l, runtimeL.u, runtimeL.d};   // Enclosing
        const Region g = RegionFor(drawn, 0, 0, runtimeL, true, .55f);
        CHECK(g.ok && std::abs(g.cx - .5f) < 1e-6f);                         // straight ahead is the middle of a symmetric frustum
        const float x1 = (runtimeL.r - drawn.l) / (drawn.r - drawn.l);
        CHECK(g.x0 == 0 && std::abs(g.x1 - x1) < 1e-6f && g.x1 < .85f);     // the nose side beyond 45 degrees is never shown
        CHECK(std::abs(g.ax - .5f * .55f * x1) < 1e-6f);                     // the ellipse measured in the shown part
        // tiles: the centre whole, the far nose side never seen (unseen rate), the outer edge coarse
        const uint32_t tile = 16;
        const float vw = 2160, vh = 2160;
        CHECK(TileRate(uint32_t(vw * .5f) / tile, uint32_t(vh * .5f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == 0);
        CHECK(TileRate(uint32_t(vw * .97f) / tile, uint32_t(vh * .5f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == kRate4x4);
        CHECK(TileRate(uint32_t(vw * .02f) / tile, uint32_t(vh * .5f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == kRate2x2);
        CHECK(TileRate(uint32_t(vw * .97f) / tile, uint32_t(vh * .5f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, 0) == kRate2x2);   // unseen off
        // a tile straddling the shown edge is not "unseen"
        const uint32_t edge = uint32_t(vw * g.x1) / tile;
        CHECK(TileRate(edge, uint32_t(vh * .5f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == kRate2x2);
        // runtime_fov: Centred of both eyes, each shown its own part
        const Tan runtimeR = Deg(-45, 57, 50, -50);
        const float h = std::max({-runtimeL.l, runtimeL.r, -runtimeR.l, runtimeR.r}), v = std::max({runtimeL.u, -runtimeL.d});
        const Tan centred = {-h, h, v, -v};
        const Region a = RegionFor(centred, 0, 0, runtimeL, true, .55f), b = RegionFor(centred, 0, 0, runtimeR, true, .55f);
        CHECK(std::abs(a.cx - .5f) < 1e-6f && std::abs(b.cx - .5f) < 1e-6f && std::abs(a.x1 - (1.0f - b.x0)) < 1e-5f);
    }

    // ---- canted displays: the eye drawn turned, its straight-ahead off the frustum's middle
    {
        const Tan drawn = Deg(-50, 50, 50, -50);
        const Region g = RegionFor(drawn, std::tan(10.0f * 3.14159265f / 180.0f), 0, drawn, false, .55f);
        CHECK(g.ok && g.cx > .55f && g.x1 == 1);
    }

    // ---- outside the viewport: whole (Bear); a viewport off the corner
    {
        Region g = RegionFor(Deg(-50, 50, 50, -50), 0, 0, Tan{}, false, .55f);
        CHECK(TileRate(0, 0, 16, 64, 64, 1800, 1800, g, kRate2x2) == 0);           // left of / above the viewport
        CHECK(TileRate(200, 200, 16, 0, 0, 1800, 1800, g, kRate2x2) == 0);         // beyond it
        CHECK(TileRate(4, 4, 16, 64, 64, 1800, 1800, g, kRate2x2) == kRate2x2);    // its corner: coarse
    }

    // ---- nothing sensible in: all whole
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(!RegionFor(Deg(-50, 50, 50, -50), 0, 0, Tan{}, false, nan).ok);
        CHECK(!RegionFor(Tan{0, 0, 0, 0}, 0, 0, Tan{}, false, .55f).ok);
        CHECK(!RegionFor(Deg(-50, 50, 50, -50), nan, 0, Tan{}, false, .55f).ok);
        std::vector<uint8_t> m;
        const MaskCount n = BuildMask(2160, 2160, 16, 0, 0, 2160, 2160, Region{}, kRate2x2, kRate4x4, m);
        CHECK(n.coarse == 0 && m.size() == 135u * 135u);
        CHECK(TileRate(3, 3, 0, 0, 0, 100, 100, RegionFor(Deg(-50, 50, 50, -50), 0, 0, Tan{}, false, .55f), kRate2x2) == 0);   // no tile size
    }

    // ---- the strength levels (Bear's table) and the rate image's size
    {
        CHECK(StrengthPreset(0).radius == .70f && StrengthPreset(0).rate == 2);
        CHECK(StrengthPreset(1).radius == .55f && StrengthPreset(1).rate == 2);
        CHECK(StrengthPreset(2).radius == .55f && StrengthPreset(2).rate == 4);
        CHECK(StrengthPreset(3).radius == .35f && StrengthPreset(3).rate == 4);
        CHECK(StrengthPreset(-1).radius == .55f && StrengthPreset(9).radius == .55f);   // out of range: Medium
        CHECK(Tiles(2160, 16) == 135 && Tiles(2161, 16) == 136 && Tiles(1, 32) == 1);
    }

    // ---- which draws take the image
    {
        CHECK(Classify(true, true, true, true, true) == 0);
        CHECK(Classify(false, true, true, true, true) == kUnknown);
        CHECK(Classify(true, false, true, true, true) == kNoPixel);
        CHECK(Classify(true, true, false, true, true) == kNoDepth);
        CHECK(Classify(true, true, true, false, true) == kBlended);
        CHECK(Classify(true, true, true, true, false) == kSamples);
        Bound b;
        b.w = b.dw = 2160; b.h = b.dh = 2160; b.texture2d = b.single = true; b.vw = b.vh = 2160;
        CHECK(Eligible(b, 2160, 2160));
        Bound c = b; c.dw = c.dh = 1080; CHECK(!Eligible(c, 2160, 2160));            // depth of another size
        c = b; c.dw = c.dh = 0; CHECK(!Eligible(c, 2160, 2160));                      // no depth
        c = b; c.w = c.dw = 512; c.h = c.dh = 512; c.vw = c.vh = 512; CHECK(!Eligible(c, 2160, 2160));   // a small target (shadow, bloom)
        c = b; c.w = c.dw = 2160; c.h = c.dh = 1080; c.vh = 1080; CHECK(!Eligible(c, 2160, 2160));         // another aspect
        c = b; c.single = false; CHECK(!Eligible(c, 2160, 2160));
        c = b; c.vw = c.vh = 900; CHECK(!Eligible(c, 2160, 2160));                    // a small viewport
        c = b; c.vx = 400; CHECK(!Eligible(c, 2160, 2160));                           // a viewport out of its target
        c = b; c.w = c.dw = 2400; c.h = c.dh = 2400; c.vw = c.vh = 1448; CHECK(Eligible(c, 2160, 2160));   // DLSS: rendered smaller, a bigger target
        CHECK(!Eligible(b, 0, 0));
    }

    // ---- how much of an eye is shaded coarsely (2160 x 2160, 16-pixel tiles): the most the shading of the opaque world can save
    {
        std::printf("coarse share of one eye, 2160x2160, tile 16 (Low / Medium / High / Maximum):\n");
        const Tan runtimeL = Deg(-57, 45, 50, -50);
        const Tan enclosing = {runtimeL.l, -runtimeL.l, runtimeL.u, runtimeL.d};
        for (int pass = 0; pass < 2; ++pass) {
            std::printf("  %s:", pass == 0 ? "drawn as the runtime's view (Unleashed)       " : "drawn symmetric, unseen part 4x4 (symmetric_fov)");
            for (int level = 0; level < kStrengthCount; ++level) {
                const Preset p = StrengthPreset(level);
                const uint8_t coarse = p.rate >= 4 ? kRate4x4 : kRate2x2;
                const Region g = pass == 0 ? RegionFor(runtimeL, 0, 0, runtimeL, true, p.radius) : RegionFor(enclosing, 0, 0, runtimeL, true, p.radius);
                std::vector<uint8_t> m;
                const MaskCount n = BuildMask(2160, 2160, 16, 0, 0, 2160, 2160, g, coarse, pass == 0 ? 0 : kRate4x4, m);
                double shaded = 0;   // pixel shader invocations relative to all 1x1
                for (uint8_t r : m) shaded += r == kRate4x4 ? 1.0 / 16 : r == kRate2x2 ? 0.25 : 1.0;
                std::printf("  %4.1f%% coarse (%4.1f%% unseen), %4.1f%% of the shading", 100.0 * n.coarse / m.size(), 100.0 * n.unseen / m.size(),
                            100.0 * shaded / m.size());
                CHECK(n.coarse > 0 && n.coarse < m.size());
                if (level == 1 && pass == 0) CHECK(shaded / m.size() > .4 && shaded / m.size() < .8);
            }
            std::printf("\n");
        }
    }

    // ---- the gaze followed (eye tracking): Bear's bank, his margin, his freshness and jump rules; the gaze in our drawn picture
    {
        CHECK(StrengthPreset(1).gazeRadius == .35f && StrengthPreset(0).gazeRadius == .45f && StrengthPreset(3).gazeRadius == .30f);
        CHECK(GazeCell(0, 0) == 0 && GazeCell(1, 1) == kGazeMasks - 1 && GazeCell(.5f, .5f) == 4 * kGazeGrid + 4);
        CHECK(GazeCell(-3, 7) == (kGazeGrid - 1) * kGazeGrid);   // clamped
        CHECK(std::abs(GazeGridCoordinate(4) - .5f) < 1e-6f && std::abs(kGazeMargin - .1767767f) < 1e-5f);
        // the cell's centre is never more than the margin from the gaze (in halves of the viewport)
        float worst = 0;
        for (int i = 0; i <= 200; ++i)
            for (int j = 0; j <= 200; ++j) {
                const float u = i / 200.0f, v = j / 200.0f;
                const int c = GazeCell(u, v);
                const float du = 2 * (u - GazeGridCoordinate(c % kGazeGrid)), dv = 2 * (v - GazeGridCoordinate(c / kGazeGrid));
                worst = std::max(worst, std::sqrt(du * du + dv * dv));
            }
        CHECK(worst <= kGazeMargin + 1e-5f);
        const int64_t now = 5'000'000'000;
        CHECK(GazeFresh(now, now - 100'000'000) && GazeFresh(now, now + 20'000'000) && GazeFresh(now, now));
        CHECK(!GazeFresh(now, now - 100'000'001) && !GazeFresh(now, now + 20'000'001) && !GazeFresh(now, 0) && !GazeFresh(0, now));
        GazeJump j;
        CHECK(!j.Steady(.5f, .5f));    // came back: whole once
        CHECK(j.Steady(.55f, .52f));   // a small move: followed
        CHECK(!j.Steady(.9f, .5f));    // a saccade: whole once
        CHECK(j.Steady(.92f, .5f));
        j.Reset();
        CHECK(!j.Steady(.92f, .5f));
        // the gaze in the picture: straight ahead in a symmetric frustum is the middle; a mirrored asymmetric eye's straight-ahead is its lens centre
        const Tan sym = Deg(-50, 50, 50, -50), left = Deg(-57, 45, 50, -50);
        float u = 0, v = 0;
        CHECK(GazeUV(sym, 0, 0, &u, &v) && std::abs(u - .5f) < 1e-6f && std::abs(v - .5f) < 1e-6f);
        CHECK(GazeUV(left, 0, 0, &u, &v) && std::abs(u - RegionFor(left, 0, 0, left, true, .55f).cx) < 1e-6f);
        CHECK(GazeUV(sym, std::tan(20 * 3.14159265f / 180), 0, &u, &v) && u > .6f);    // looking right
        CHECK(GazeUV(sym, 0, std::tan(20 * 3.14159265f / 180), &u, &v) && v < .4f);    // looking up: nearer the top
        CHECK(!GazeUV(sym, std::tan(60 * 3.14159265f / 180), 0, &u, &v));               // beyond the picture
        CHECK(!GazeUV(sym, std::numeric_limits<float>::quiet_NaN(), 0, &u, &v));
        // a bank cell's region: centred on the cell, the shown window and the unseen part kept
        const Tan enclosing = {left.l, -left.l, left.u, left.d};
        const Region fixed = RegionFor(enclosing, 0, 0, left, true, .55f);
        const Region g = GazeRegion(fixed, GazeCell(.25f, .75f), StrengthPreset(1).gazeRadius);
        CHECK(g.ok && g.cx == .25f && g.cy == .75f && g.x1 == fixed.x1);
        CHECK(std::abs(g.ax - .5f * (.35f * (fixed.x1 - fixed.x0) + kGazeMargin)) < 1e-6f);   // (with the margin about the fixed size at Medium: the same saving, following the eye)
        CHECK(!GazeRegion(fixed, -1, .35f).ok && !GazeRegion(fixed, kGazeMasks, .35f).ok && !GazeRegion(Region{}, 0, .35f).ok);
        // the tile under the gaze is whole, the middle far from it coarse
        const uint32_t tile = 16;
        const float vw = 2160, vh = 2160;
        CHECK(TileRate(uint32_t(vw * .25f) / tile, uint32_t(vh * .75f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == 0);
        CHECK(TileRate(uint32_t(vw * .5f) / tile, uint32_t(vh * .3f) / tile, tile, 0, 0, vw, vh, g, kRate2x2, kRate4x4) == kRate2x2);
        std::vector<uint8_t> m;
        const MaskCount n = BuildMask(2160, 2160, 16, 0, 0, 2160, 2160, GazeRegion(fixed, GazeCell(.5f, .5f), .35f), kRate2x2, kRate4x4, m);
        double shaded = 0;
        for (uint8_t r : m) shaded += r == kRate4x4 ? 1.0 / 16 : r == kRate2x2 ? 0.25 : 1.0;
        {   // the bank's one-time cost at a real eye size (vr/foveation.cpp makes 2 + 2 x 81 masks when an eye tracker is bound)
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<uint8_t> b;
            uint32_t sum = 0;
            for (int e = 0; e < 2 + 2 * kGazeMasks; ++e) sum += BuildMask(2160, 2160, 16, 0, 0, 2160, 2160, e < 2 ? fixed : GazeRegion(fixed, (e - 2) % kGazeMasks, .35f), kRate2x2, kRate4x4, b).coarse;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("the bank (164 masks of 135 x 135 tiles): %.1f ms to make (%u coarse tiles in all)\n", ms, sum);
        }
        std::printf("gaze followed, Medium (0.35 + margin), symmetric_fov + unseen: %.1f%% coarse, %.1f%% of the shading\n", 100.0 * n.coarse / m.size(),
                    100.0 * shaded / m.size());
    }

    // ---- the heavy full-screen passes by their shader (vr/fullscreen_passes.h: [render] foveated_fullscreen, grass_lighting_coarse)
    {
        namespace fsp = fullscreenpasses;
        CHECK(fsp::Fnv("a", 1) == 0x44bd8ad473cd9906ull);   // uitrace's FNV-1a 64 (its basis 1469598103934665603, not the standard one): the trace's "ps" numbers
        size_t n = 0;
        const fsp::Known* t = fsp::Table(&n);
        int grass = 0, refl = 0, hair = 0;
        for (size_t i = 0; i < n; ++i) {
            grass += t[i].kind == fsp::kGrassLighting;
            refl += t[i].kind == fsp::kReflections;
            hair += t[i].kind == fsp::kHair;
            CHECK(t[i].bytes > 1000 && t[i].hash);
        }
        CHECK(grass == 1 && refl == 1 && hair == 17);   // one grass lighting and one reflections permutation in the game: no lighter variant
        std::vector<uint8_t> junk(57580, 0x5a);          // the grass lighting's size, not its bytes
        CHECK(fsp::KindOf(junk.data(), junk.size()) == fsp::kNone);
        CHECK(fsp::KindOf(junk.data(), 123) == fsp::kNone && fsp::KindOf(nullptr, 57580) == fsp::kNone);
        CHECK(!fsp::Widened(fsp::kGrassLighting, 0) && fsp::Widened(fsp::kGrassLighting, 1) && fsp::Widened(fsp::kReflections, 1));
        CHECK(!fsp::Widened(fsp::kHair, 1) && fsp::Widened(fsp::kHair, 2) && !fsp::Widened(fsp::kNone, 2));
    }

    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("unleashed_foveation_test: all passed\n");
    return 0;
}
