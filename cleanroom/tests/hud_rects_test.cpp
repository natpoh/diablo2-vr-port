// hud_rects_test - vr/hud_rects.h: the toolbar's strip in the game's interface layer ([hud] hide = 1), and the hide_up_pct / hide_down_pct sliders that make it taller.
// Pure: the strip is a few lines of arithmetic over the layer's size and Safe Screen Percent, so the test pins it down where it matters - the default is the strip it always was,
// the sliders move its edges by exactly their share of the picture's height and are held in their ranges, the strip never leaves the layer or turns thin, and what was measured on
// the real layers (a 1920 x 1920 dump at 80 %: the art's rows; the user's 2048 x 2048 picture at 80 %: the cut row and the rows of the art over it) is covered at the settings that cover it.
//
// Run: hud_rects_test.exe        (exit code 0 = all passed)

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "hud_globes.h"
#include "hud_rects.h"

using namespace hudrects;

namespace {

int g_failed = 0, g_checks = 0;
void Expect(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok) return;
    ++g_failed;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    printf("  FAIL: %s\n", b);
}

// The strip as vr/hud.cpp computed it before the sliders (copied here, so that the default is checked against the old arithmetic and not against itself).
Box OldToolbarRect(uint32_t w, uint32_t h, float s) {
    const float H = (float)h;
    const float bottom = H * (0.5f + 0.5f * s), top = bottom - 0.215f * H * s;
    const float l = 0.0f, t = top, r = (float)w, b = bottom + 0.004f * H;
    return Box{std::max(0, (int32_t)l), std::max(0, (int32_t)t), std::min((int32_t)w, (int32_t)r), std::min((int32_t)h, (int32_t)b)};
}
bool Same(const Box& a, const Box& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

struct Size { uint32_t w, h; float safe; };
const Size kSizes[] = {{1920, 1920, 0.8f}, {2048, 2048, 0.8f}, {1536, 1536, 1.0f}, {1920, 1080, 1.0f}, {2560, 1440, 0.9f}, {3840, 2160, 0.5f},
                       {1920, 1200, 0.8f}, {3440, 1440, 1.0f}, {1024, 1024, 0.3f}, {4096, 4096, 0.7f}, {1806, 1262, 0.8f}};

void TestDefault() {
    printf("the default is the strip it always was\n");
    for (const Size& z : kSizes) {
        const Box now = Toolbar(z.w, z.h, z.safe), old = OldToolbarRect(z.w, z.h, z.safe);
        Expect(Same(now, old), "%ux%u at %.0f%%: %d..%d vs %d..%d", z.w, z.h, z.safe * 100.0f, now.top, now.bottom, old.top, old.bottom);
        Expect(Same(now, Toolbar(z.w, z.h, z.safe, 0.0f, 0.0f)), "0 and 0 are the default");
        Expect(now.left == 0 && now.right == (int32_t)z.w, "%ux%u: the whole width", z.w, z.h);
    }
    // the numbers the comment of hud_globes.h and the notes quote
    const Box a = Toolbar(1920, 1920, 0.8f), b = Toolbar(2048, 2048, 0.8f);
    Expect(a.top == 1397 && a.bottom == 1735, "1920 x 1920 at 80%%: 1397..1735 (%d..%d)", a.top, a.bottom);
    Expect(b.top == 1490 && b.bottom == 1851, "2048 x 2048 at 80%%: 1490..1851 (%d..%d)", b.top, b.bottom);
}

void TestSliders() {
    printf("the sliders move the edges by their share of the picture's height\n");
    for (const Size& z : kSizes) {
        const Box base = Toolbar(z.w, z.h, z.safe);
        const float H = (float)z.h;
        const Box up = Toolbar(z.w, z.h, z.safe, 4.0f, 0.0f);
        Expect(up.bottom == base.bottom && up.left == base.left && up.right == base.right, "%ux%u: up moves only the top", z.w, z.h);
        Expect(std::abs((float)up.top - std::max(0.0f, (float)base.top - 0.04f * H)) <= 1.0f, "%ux%u: +4%% is 0.04 H higher (%d -> %d)", z.w, z.h, base.top, up.top);
        const Box down = Toolbar(z.w, z.h, z.safe, 0.0f, 2.0f);
        Expect(down.top == base.top && std::abs((float)down.bottom - std::min((float)z.h, (float)base.bottom + 0.02f * H)) <= 1.0f, "%ux%u: down +2%% moves only the bottom (%d -> %d)", z.w, z.h, base.bottom, down.bottom);
        const Box less = Toolbar(z.w, z.h, z.safe, -3.0f, 0.0f);
        Expect(less.top >= base.top, "%ux%u: negative up makes the strip shorter", z.w, z.h);
    }
    // an exact case: 2048 x 2048 at 80%, +3.5 / +1
    const Box a = Toolbar(2048, 2048, 0.8f, 3.5f, 1.0f);
    Expect(a.top == 1419 && a.bottom == 1871, "2048 at 80%% with 3.5 / 1: 1419..1871 (%d..%d)", a.top, a.bottom);
}

void TestRanges() {
    printf("the sliders are held in their ranges\n");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    Expect(UpPct(-50.0f) == -5.0f && UpPct(50.0f) == 15.0f && UpPct(7.5f) == 7.5f, "up: -5 .. 15");
    Expect(DownPct(-50.0f) == -2.0f && DownPct(50.0f) == 5.0f && DownPct(1.25f) == 1.25f, "down: -2 .. 5");
    Expect(UpPct(nan) == 0.0f && UpPct(inf) == 0.0f && UpPct(-inf) == 0.0f && DownPct(nan) == 0.0f && DownPct(inf) == 0.0f, "a number that is not one: 0");
    const Box wild = Toolbar(2048, 2048, 0.8f, 1000.0f, 1000.0f), top = Toolbar(2048, 2048, 0.8f, 15.0f, 5.0f);
    Expect(Same(wild, top), "past the ends is the end");
    Expect(Same(Toolbar(2048, 2048, 0.8f, nan, nan), Toolbar(2048, 2048, 0.8f)), "NaN sliders: the default");
    // the interface size that is not one
    Expect(Same(Toolbar(1920, 1080, nan), Toolbar(1920, 1080, 1.0f)) && Same(Toolbar(1920, 1080, 7.0f), Toolbar(1920, 1080, 1.0f)) && Same(Toolbar(1920, 1080, 0.0f), Toolbar(1920, 1080, 0.3f)),
           "a Safe Screen Percent out of 30 - 100 is held in it");
}

void TestStaysInTheLayer() {
    printf("the strip stays in the layer and never turns thin\n");
    const float ups[] = {-5.0f, -1.0f, 0.0f, 4.0f, 15.0f}, downs[] = {-2.0f, 0.0f, 1.0f, 5.0f};
    for (const Size& z : kSizes)
        for (const float u : ups)
            for (const float d : downs) {
                const Box b = Toolbar(z.w, z.h, z.safe, u, d);
                Expect(b.left == 0 && b.right == (int32_t)z.w && b.top >= 0 && b.bottom <= (int32_t)z.h && b.top < b.bottom, "%ux%u %.0f%% up %.0f down %.0f: %d..%d", z.w, z.h, z.safe * 100.0f, u, d, b.top, b.bottom);
                // the smallest interface with the strip shrunk as far as it goes: still a strip (a hundredth of the picture at the least, unless the layer's bottom edge stops it)
                if (b.bottom < (int32_t)z.h) Expect((float)(b.bottom - b.top) >= 0.009f * (float)z.h, "%ux%u: not thin (%d px)", z.w, z.h, b.bottom - b.top);
            }
    // the worst shrink: the smallest interface, up -5 and down -2: the strip would be negative without the floor
    const Box t = Toolbar(1000, 1000, 0.3f, -5.0f, -2.0f);
    Expect(t.bottom - t.top >= 9, "the thinnest strip: %d px of 1000", t.bottom - t.top);
    // a layer the size of nothing
    const Box z0 = Toolbar(0, 0, 0.8f, 5.0f, 5.0f);
    Expect(z0.left == 0 && z0.top == 0 && z0.right == 0 && z0.bottom == 0, "an empty layer: an empty box");
}

void TestCoversTheArt() {
    printf("what was measured on the real layers\n");
    // the 1920 x 1920 dump at 80%: the art (alpha < 0.98) spans rows 1437 .. 1729 over the bottom rows, the tallest thing being the devil's horn
    const Box dump = Toolbar(1920, 1920, 0.8f);
    Expect(dump.top <= 1437 && dump.bottom > 1729, "the default strip covers the 1920 dump's art, rows 1437..1729 (strip %d..%d)", dump.top, dump.bottom);
    // the user's 2048 x 2048 picture at 80% (a screenshot of the headset's render): the cut was at row 1490 - the same as this arithmetic - and the art stayed over it up to row 1436
    // (the horn tips), 1464 (the devil's claw), 1475 (the angel's hand), 1480 (the balls' tops)
    const Box user = Toolbar(2048, 2048, 0.8f);
    Expect(user.top == 1490, "the user's cut row 1490 is what the arithmetic gives (%d)", user.top);
    const int art[] = {1436, 1464, 1475, 1480};
    int over = 0;
    for (const int r : art) over += r < user.top ? 1 : 0;
    Expect(over == 4, "...and all four of those rows are over the default strip (they were what stayed)");
    const Box fixed = Toolbar(2048, 2048, 0.8f, 3.5f, 0.0f);
    bool all = true;
    for (const int r : art) all = all && r >= fixed.top;
    Expect(all && fixed.top < 1436, "up 3.5 covers them all, with a margin (top row %d)", fixed.top);
    // ... and the 1920 dump's art is still inside it (a setup that did not need the slider does not lose anything with it)
    Expect(Toolbar(1920, 1920, 0.8f, 3.5f, 0.0f).top < 1437, "the same slider on the 1920 picture");
}

void TestGlobesStayPut() {
    printf("the globes' own boxes do not move with the sliders\n");
    // hudglobes::Compute takes no cut: it is the game's layout alone. The wrists' globe cut is made from the layer before the strip is emptied, so it must lie inside the strip
    // at the default - and the sliders (up grows it, the bottom may shrink) leave those boxes where they are.
    for (const Size& z : kSizes) {
        const hudglobes::Boxes g = hudglobes::Compute(z.w, z.h, z.safe);
        if (!g.ok) continue;
        const Box strip = Toolbar(z.w, z.h, z.safe);
        const bool inside = g.top >= strip.top && g.top + 2 * g.half <= strip.bottom;
        Expect(inside, "%ux%u at %.0f%%: the globes' squares (%d..%d) are inside the strip (%d..%d)", z.w, z.h, z.safe * 100.0f, g.top, g.top + 2 * g.half, strip.top, strip.bottom);
        const hudglobes::Boxes again = hudglobes::Compute(z.w, z.h, z.safe);
        Expect(again.top == g.top && again.left[0] == g.left[0] && again.left[1] == g.left[1] && again.half == g.half, "the same boxes every time");
    }
}

}  // namespace

int main() {
    TestDefault();
    TestSliders();
    TestRanges();
    TestStaysInTheLayer();
    TestCoversTheArt();
    TestGlobesStayPut();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
