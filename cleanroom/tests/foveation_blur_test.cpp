// foveation_blur_test - [render] foveated_blur (shared/foveation_blur_math.h, reshade/D2R_DepthFog.fx FovBlur*): the coarse pixels of
// foveated rendering blurred by an effect of their own, built on the distance blur's. The blur's radius is 0 wherever the tile was shaded
// whole (the sharp centre, the gaze); it grows with the tile's block size (2x2, 4x4: x strength x 1.5 pixels) and fades in past the sharp
// ellipse's edge, so no ring marks where the tiles start; the effect file has the same sums and is laid out as the distance blur's (its own
// texture, two passes after it in both eyes' techniques, its weights the distance blur's). Run: foveation_blur_test.exe
#include "../../shared/foveation_blur_math.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace foveablur;
namespace uf = unleashed::foveation;

static Region Make(uint8_t coarse, uint8_t unseen, float radius) {
    Region r;
    r.vx = 0; r.vy = 0; r.vw = 1664; r.vh = 1664;
    r.tile = 16;
    r.coarse = coarse;
    r.unseen = unseen;
    r.region = uf::RegionFor({-1, 1, 1, -1}, 0, 0, {}, false, radius);
    r.ok = true;
    return r;
}

int main() {
    // ---- none where the tile was shaded whole; the block sizes are the rate image's
    {
        const Region r = Make(uf::kRate4x4, 0, .35f);
        CHECK(Radius(r, 832, 832, .7f) == 0.0f);                 // the sharp centre
        CHECK(BlockAt(r, 832, 832) == 1 && BlockAt(r, 4, 4) == 4);
        int differ = 0;
        for (int y = 0; y < 1664; y += 7)
            for (int x = 0; x < 1664; x += 7)
                if (BlockAt(r, (float)x, (float)y) != BlockOf(uf::TileRate(x / 16, y / 16, 16, 0, 0, 1664, 1664, r.region, uf::kRate4x4, 0))) ++differ;
        CHECK(differ == 0);
        for (int y = 0; y < 1664; y += 9)   // a whole tile never blurred
            for (int x = 0; x < 1664; x += 9)
                if (BlockAt(r, (float)x, (float)y) == 1) CHECK(Radius(r, (float)x, (float)y, 1.0f) == 0.0f);
        Region off = r; off.ok = false;
        CHECK(Radius(off, 4, 4, 1.0f) == 0.0f && Radius(r, 4, 4, 0.0f) == 0.0f);
    }
    // ---- the corner: the full radius, block x strength x 1.5; 2x2 half of 4x4's
    {
        const Region r4 = Make(uf::kRate4x4, 0, .35f), r2 = Make(uf::kRate2x2, 0, .35f);
        CHECK(std::abs(Radius(r4, 4, 4, .7f) - .7f * 1.5f * 4) < 1e-4f);
        CHECK(std::abs(Radius(r2, 4, 4, .7f) - .7f * 1.5f * 2) < 1e-4f);
        CHECK(std::abs(Radius(r4, 4, 4, 5.0f) - 1.0f * 1.5f * 4) < 1e-4f);   // strength capped at 1
        std::printf("radius at the corner, strength 0.7: 4x4 %.2f px, 2x2 %.2f px\n", Radius(r4, 4, 4, .7f), Radius(r2, 4, 4, .7f));
    }
    // ---- faded in past the ellipse's edge: from 0 at the first coarse tiles to full at 1.3 times out, never jumping
    {
        const Region r = Make(uf::kRate4x4, 0, .55f);
        float last = 0.0f, worst = 0.0f;
        int firstCoarse = -1;
        for (int x = 832; x < 1664; ++x) {   // along the middle row, outwards
            const float v = Radius(r, (float)x, 832, 1.0f);
            if (firstCoarse < 0 && BlockAt(r, (float)x, 832) > 1) firstCoarse = x;
            worst = std::max(worst, std::abs(v - last));
            last = v;
        }
        CHECK(firstCoarse > 0);
        CHECK(Radius(r, (float)firstCoarse, 832, 1.0f) < 1.0f);   // small at the first coarse tile (no ring)
        CHECK(worst < 0.25f);                                     // no step bigger than a quarter pixel a pixel
        CHECK(std::abs(Radius(r, 1663, 832, 1.0f) - 6.0f) < 1e-3f);
        CHECK(EllipseDistance(r, 832, 832) == 0.0f && EllipseDistance(r, 832 + 0.55f * 832, 832) > 0.99f);
    }
    // ---- the never-shown part (4x4 there with a 2x2 periphery): the bigger radius
    {
        Region r = Make(uf::kRate2x2, uf::kRate4x4, .35f);
        r.region = uf::RegionFor({-1.2f, 1.2f, 1, -1}, 0, 0, {-1.2f, 0.9f, 1, -1}, true, .35f);   // the right strip never shown
        CHECK(BlockAt(r, 1660, 832) == 4 && BlockAt(r, 4, 832) == 2);
        CHECK(Radius(r, 1660, 832, .7f) > Radius(r, 4, 832, .7f));
    }
    // ---- the effect: the same sums, laid out as the distance blur
    {
        std::ifstream f(FX_PATH, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string fx = ss.str();
        CHECK(!fx.empty());
        CHECK(fx.find("uniform bool FovBlurOn") != std::string::npos && fx.find("texture2D FovBlurTex") != std::string::npos);
        CHECK(fx.find("saturate(FovBlurStrength) * 1.5 * b * smoothstep(1.0, 1.3,") != std::string::npos);   // Radius
        CHECK(fx.find("kDistBlurW[i]") != std::string::npos && kWeights[1] == 0.88f);   // the distance blur's weights
        // both techniques: the two passes right after the distance blur's
        const std::string after = "PixelShader = PS_DistBlurV; }";
        size_t at = 0;
        int both = 0;
        while ((at = fx.find(after, at)) != std::string::npos) {
            const size_t h = fx.find("PS_FovBlurH", at), v = fx.find("PS_FovBlurV", at), next = fx.find("pass", fx.find("PS_FovBlurV", at) + 1);
            if (h != std::string::npos && v != std::string::npos && h - at < 200 && v > h && next > v) ++both;
            at += after.size();
        }
        CHECK(both == 2);
        // the distance blur's own code as it was
        CHECK(fx.find("const float r = DistBlurMud(k) * DistBlurStrength * 0.025 * BUFFER_HEIGHT;") != std::string::npos);
    }
    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("foveation_blur_test: all passed\n");
    return 0;
}
