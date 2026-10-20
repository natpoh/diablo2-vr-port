// The corner map's scissor box found at any window size, shape and interface size (vr/minimap_box.h, used by vr/hud.cpp IsMapScissor),
// without the game.
//
// The map's box is made the way the measured ones are (vr/minimap_box.h): 0.533 H s wide but no wider than a third of the picture
// (1920x1200 at 100 %: 640; 1920x1440: ~640; 1699x1274 at 80 %: ~543), 1.6 times as wide as tall, its top 0.044 H s under the interface
// area's top, its outer edge at the area's side - or at the picture's edge (the other reading of the area's width). Checked at 1920x1920,
// 2400x2400, 1353x831 and 2560x1440, interface size 80 and 100, both corners:
//   - found from the game's own interface scale, and from the file's interface size when the game's is not known;
//   - found when the file's number is stale (a file with no key reads 1) but the game's scale is right - the 0.173 test missed it;
//   - found through a viewport smaller than the layer texture;
//   - not found: the whole picture, the toolbar's strip, a box at the top middle, a small one in the corner, the other corner when one
//     is asked for, a box that is not at the area's top.
// Run: minimap_box_test.exe (exit 0: all passed).

#include "minimap_box.h"

#include <cmath>
#include <cstdio>

using minimapbox::Box;
using minimapbox::Canvas;

namespace {

int g_fail = 0, g_checks = 0;
void Check(bool ok, const char* what, unsigned w, unsigned h, int pct, const char* extra = "") {
    ++g_checks;
    if (ok) return;
    ++g_fail;
    std::printf("FAIL %ux%u at %d%%: %s %s\n", w, h, pct, what, extra);
}

// The game's interface scale (pixels a layout unit) for a canvas of height h at interface size s.
float GameScale(unsigned h, float s) { return (float)h / 2160.0f * s; }

// The map's box as the game draws it. corner 1 left, 2 right; edgeAtPicture: its outer edge at the picture's edge, not the area's side.
Box MapBox(const Canvas& c, float s, int corner, bool edgeAtPicture) {
    const float W = (float)c.w, H = (float)c.h;
    const float wide = std::fmin(0.533f * H * s, W / 3.0f), tall = wide / 1.6f;
    const float side = edgeAtPicture ? 0.0f : 0.5f * W * (1.0f - s);
    const float top = 0.5f * H * (1.0f - s) + 0.044f * H * s;
    const float l = corner == 1 ? side : W - side - wide;
    return Box{c.x + (int)std::lround(l), c.y + (int)std::lround(top), c.x + (int)std::lround(l + wide), c.y + (int)std::lround(top + tall)};
}

// The test as 0.173 had it (vr/hud.cpp IsMapScissor): the file's s against the texture, the outer edge within 0.07 H of the area's side.
bool OldIsMap(const Box& r, unsigned w, unsigned h, float s, int which) {
    const float left = 0.5f * (float)w * (1.0f - s), top = 0.5f * (float)h * (1.0f - s);
    const float tol = 0.07f * (float)h;
    const int rw = r.right - r.left, rh = r.bottom - r.top;
    const float right = (float)w - left;
    const bool atLeft = std::fabs(r.left - left) < tol, atRight = std::fabs(r.right - right) < tol;
    const bool corner = which == 1 ? atLeft : which == 2 ? atRight : (atLeft || atRight);
    return corner && std::fabs(r.top - top) < tol && rw > (int)w / 8 && rw < (int)w / 2 && rh > (int)h / 8 && rh < (int)(h * 0.6f);
}

struct Size { unsigned w, h; };
constexpr Size kSizes[] = {{1920, 1920}, {2400, 2400}, {1353, 831}, {2560, 1440}};
constexpr int kPcts[] = {80, 100};

}  // namespace

int main() {
    // SafeFromScale: the area's fraction from the game's scale, nothing from a scale that makes no area of this canvas
    Check(std::fabs(minimapbox::SafeFromScale(GameScale(1920, 0.8f), 1920) - 0.8f) < 1e-4f, "SafeFromScale 80 %", 1920, 1920, 80);
    Check(std::fabs(minimapbox::SafeFromScale(GameScale(831, 1.0f), 831) - 1.0f) < 1e-4f, "SafeFromScale 100 %", 1353, 831, 100);
    Check(minimapbox::SafeFromScale(0.0f, 1920) == 0.0f, "SafeFromScale: no scale", 1920, 1920, 0);
    Check(minimapbox::SafeFromScale(GameScale(2400, 1.0f), 1200) == 0.0f, "SafeFromScale: another canvas's scale", 1200, 1200, 100);
    // CanvasOf: a viewport inside the layer and not a piece of it
    {
        const Canvas c = minimapbox::CanvasOf(2400, 2400, 0.0f, 0.0f, 1920.0f, 1920.0f);
        Check(c.w == 1920 && c.h == 1920 && c.x == 0 && c.y == 0, "CanvasOf: the viewport", 2400, 2400, 0);
        const Canvas piece = minimapbox::CanvasOf(1920, 1920, 192.0f, 260.0f, 640.0f, 400.0f);
        Check(piece.w == 1920 && piece.h == 1920, "CanvasOf: a piece (the map's own viewport) is not the canvas", 1920, 1920, 0);
        const Canvas none = minimapbox::CanvasOf(1920, 1920, 0.0f, 0.0f, 0.0f, 0.0f);
        Check(none.w == 1920 && none.h == 1920, "CanvasOf: no viewport", 1920, 1920, 0);
        const Canvas out = minimapbox::CanvasOf(1920, 1920, 0.0f, 0.0f, 2400.0f, 2400.0f);
        Check(out.w == 1920 && out.h == 1920, "CanvasOf: bigger than the layer", 1920, 1920, 0);
    }

    int oldMissed = 0, oldMissedTrue = 0, cases = 0;
    for (const Size& z : kSizes) {
        for (const int pct : kPcts) {
            const float s = (float)pct / 100.0f;
            const Canvas c{0, 0, z.w, z.h};
            const float game = minimapbox::SafeFromScale(GameScale(z.h, s), z.h);
            for (int corner = 1; corner <= 2; ++corner) {
                for (int edge = 0; edge <= 1; ++edge) {
                    const Box m = MapBox(c, s, corner, edge != 0);
                    char extra[96];
                    std::snprintf(extra, sizeof extra, "(%s corner, outer edge at the %s; box %d,%d - %d,%d)", corner == 1 ? "left" : "right",
                                  edge ? "picture's edge" : "area's side", m.left, m.top, m.right, m.bottom);
                    ++cases;
                    Check(minimapbox::Find(m, c, game, 1.0f, 0) == corner, "the game's scale (the file reads 1)", z.w, z.h, pct, extra);
                    Check(minimapbox::Find(m, c, 0.0f, s, 0) == corner, "the file's size alone", z.w, z.h, pct, extra);
                    Check(minimapbox::Find(m, c, game, s, corner) == corner, "its own corner asked for", z.w, z.h, pct, extra);
                    Check(minimapbox::Find(m, c, game, s, 3 - corner) == 0, "the other corner asked for", z.w, z.h, pct, extra);
                    if (!OldIsMap(m, z.w, z.h, 1.0f, 0)) ++oldMissed;       // 0.173 with a file that reads 1
                    if (!OldIsMap(m, z.w, z.h, s, 0)) ++oldMissedTrue;      // ... and with the file's number right
                    // through a viewport: the layer texture a quarter bigger, the interface drawn in its top left
                    const Canvas big = minimapbox::CanvasOf(z.w * 5 / 4, z.h * 5 / 4, 0.0f, 0.0f, (float)z.w, (float)z.h);
                    Check(big.w == z.w && big.h == z.h && minimapbox::Find(m, big, game, s, 0) == corner, "through a smaller viewport", z.w, z.h, pct, extra);
                }
            }
            // not the map
            const float W = (float)z.w, H = (float)z.h;
            const Box whole{0, 0, (int)z.w, (int)z.h};
            Check(minimapbox::Find(whole, c, game, s, 0) == 0, "not: the whole picture", z.w, z.h, pct);
            const Box strip{0, (int)(H * (0.5f + 0.5f * s) - 0.215f * H * s), (int)z.w, (int)(H * (0.5f + 0.5f * s))};
            Check(minimapbox::Find(strip, c, game, s, 0) == 0, "not: the toolbar's strip", z.w, z.h, pct);
            const Box middle{(int)(0.35f * W), (int)(0.5f * H * (1.0f - s)), (int)(0.65f * W), (int)(0.5f * H * (1.0f - s) + 0.2f * H)};
            Check(minimapbox::Find(middle, c, game, s, 0) == 0, "not: a box at the top middle", z.w, z.h, pct);
            const Box small{(int)(0.5f * W * (1.0f - s)), (int)(0.5f * H * (1.0f - s)), (int)(0.5f * W * (1.0f - s) + W / 16.0f),
                            (int)(0.5f * H * (1.0f - s) + H / 16.0f)};
            Check(minimapbox::Find(small, c, game, s, 0) == 0, "not: a small box in the corner", z.w, z.h, pct);
            Box low = MapBox(c, s, 2, false);
            low.top += (int)(0.3f * H); low.bottom += (int)(0.3f * H);
            Check(minimapbox::Find(low, c, game, s, 0) == 0, "not: the map's size but far under the area's top", z.w, z.h, pct);
            Box inward = MapBox(c, s, 1, false);
            const int in = (int)(0.5f * W * (1.0f - s) + 0.1f * H);
            inward.left += in; inward.right += in;
            Check(minimapbox::Find(inward, c, game, s, 0) == 0, "not: the map's size well in from the area's side", z.w, z.h, pct);
        }
    }
    std::printf("0.173's test missed %d of %d map boxes with a file that reads 1, %d with the file's number right (the outer edge at the picture's\n"
                "edge at 80 %%); this one: %s\n", oldMissed, cases, oldMissedTrue, g_fail ? "see the failures" : "none");
    Check(oldMissed > 0, "the old test is shown to miss boxes (the reason for this one)", 0, 0, 0);
    std::printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "passed", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
