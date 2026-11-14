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
//     is asked for, a box that is not at the area's top;
//   - F4 in a picture of another shape than our frustum (Stretch, 0.183): the author's box in his wide 3840x2136 picture (0.66 of it
//     wide) missed without the stretch and found with it; boxes drawn 1.04 / 1.4 / 1.87 times as wide found at every size, and the
//     boxes that are not the map still refused - and one across nearly the whole top.
// Run: minimap_box_test.exe (exit 0: all passed).

#include "minimap_box.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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
// 2764x2780: SteamVR's recommended size for a Pico 4 (2026-10-28, a player's map not in the hand there; 1536, 2048, 2368 worked)
constexpr Size kSizes[] = {{1920, 1920}, {2400, 2400}, {1353, 831}, {2560, 1440}, {2764, 2780}, {1536, 1536}};
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
                    // WhyNot (the log's reason) says nothing where Corner takes it, "corner" where only [hud] map_corner refuses it
                    Check(minimapbox::WhyNot(m, c, game, 0) == nullptr && minimapbox::WhyNot(m, c, game, corner) == nullptr, "WhyNot: taken", z.w, z.h, pct, extra);
                    const char* other = minimapbox::WhyNot(m, c, game, 3 - corner);
                    Check(other && std::strcmp(other, "corner") == 0, "WhyNot: the other corner asked for -> corner", z.w, z.h, pct, extra);
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
            auto why = [&](const Box& r, const char* want) { const char* w = minimapbox::WhyNot(r, c, game, 0); return w && std::strcmp(w, want) == 0; };
            Check(why(whole, "width") && why(strip, "width") && why(small, "width"), "WhyNot: whole picture / strip / small box -> width", z.w, z.h, pct);
            Check(why(low, "top") && why(inward, "side") && why(middle, "side"), "WhyNot: low -> top, inward / middle -> side", z.w, z.h, pct);
            Check(std::strcmp(minimapbox::WhyNot(whole, Canvas{0, 0, 32, 32}, game, 0), "no area") == 0, "WhyNot: no canvas", z.w, z.h, pct);
        }
    }
    // F4 in a picture of another shape than our frustum (Stretch): the box drawn that much wider, from its outer edge (2026-10-26, 0.183)
    Check(std::fabs(minimapbox::Stretch(0.7265f, 0.7002f, 2048, 2048) - 1.0375f) < 1e-3f, "Stretch: a square picture, the headset's frustum", 2048, 2048, 0);
    Check(std::fabs(minimapbox::Stretch(0.7265f, 0.7002f, 3840, 2136) - 1.8653f) < 1e-3f, "Stretch: the author's wide picture", 3840, 2136, 0);
    Check(std::fabs(minimapbox::Stretch(2136.0f / 3840.0f, 1.0f, 3840, 2136) - 1.0f) < 1e-4f, "Stretch: a projection of the picture's shape", 3840, 2136, 0);
    Check(minimapbox::Stretch(0.0f, 0.7f, 3840, 2136) == 1.0f && minimapbox::Stretch(0.7f, 0.0f, 3840, 2136) == 1.0f, "Stretch: not known", 3840, 2136, 0);
    Check(minimapbox::Stretch(0.7f, 0.7f, 0, 2136) == 1.0f && minimapbox::Stretch(50.0f, 0.7f, 3840, 2136) == 1.0f, "Stretch: no sense", 3840, 2136, 0);
    {   // the author's: started flat in a maximized 3840x2136 window, the headset switched on in the game; the box read off his mirror
        // (its left edge cut straight, the hero's marker in its middle, its rows those of the flat box at 100 %)
        const Canvas c{0, 0, 3840, 2136};
        const Box his{1258, 94, 3792, 818};
        const float game = minimapbox::SafeFromScale(GameScale(2136, 1.0f), 2136), f = minimapbox::Stretch(0.7265f, 0.7002f, 3840, 2136);
        Check(minimapbox::Find(his, c, game, 1.0f, 0) == 0, "the author's wide box was not found before (the map left at the picture's top)", 3840, 2136, 100);
        Check(minimapbox::Find(his, c, game, 1.0f, 0, f) == 2, "the author's wide box found with the projection's stretch", 3840, 2136, 100);
        Check(minimapbox::Find(his, c, game, 1.0f, 1, f) == 0, "the author's wide box: not when the left corner is asked for", 3840, 2136, 100);
        const Box across{77, 94, 3840, 818};   // as tall, nearly the whole top: a panel, not the map
        Check(minimapbox::Find(across, c, game, 1.0f, 0, f) == 0, "not: a box across nearly the whole top, stretch or not", 3840, 2136, 100);
    }
    for (const Size& z : kSizes) {
        for (const int pct : kPcts) {
            const float s = (float)pct / 100.0f;
            const Canvas c{0, 0, z.w, z.h};
            const float game = minimapbox::SafeFromScale(GameScale(z.h, s), z.h);
            const float W = (float)z.w, H = (float)z.h;
            for (const float f : {1.0375f, 1.4f, 1.8653f}) {
                char extra[64];
                std::snprintf(extra, sizeof extra, "(drawn %.2f times as wide)", f);
                for (int corner = 1; corner <= 2; ++corner) {
                    for (int edge = 0; edge <= 1; ++edge) {
                        Box m = MapBox(c, s, corner, edge != 0);
                        const int wide = (int)std::lround((float)(m.right - m.left) * f);
                        if (corner == 1) m.right = m.left + wide; else m.left = m.right - wide;
                        if (m.left < 0 || m.right > (int)z.w) continue;   // (wider than the picture holds)
                        Check(minimapbox::Find(m, c, game, s, 0, f) == corner, "a stretched box found", z.w, z.h, pct, extra);
                        Check(minimapbox::Find(m, c, game, s, 3 - corner, f) == 0, "a stretched box: the other corner asked for", z.w, z.h, pct, extra);
                    }
                }
                const Box whole{0, 0, (int)z.w, (int)z.h};
                Check(minimapbox::Find(whole, c, game, s, 0, f) == 0, "not: the whole picture", z.w, z.h, pct, extra);
                const Box strip{0, (int)(H * (0.5f + 0.5f * s) - 0.215f * H * s), (int)z.w, (int)(H * (0.5f + 0.5f * s))};
                Check(minimapbox::Find(strip, c, game, s, 0, f) == 0, "not: the toolbar's strip", z.w, z.h, pct, extra);
                const Box middle{(int)(0.35f * W), (int)(0.5f * H * (1.0f - s)), (int)(0.65f * W), (int)(0.5f * H * (1.0f - s) + 0.2f * H)};
                Check(minimapbox::Find(middle, c, game, s, 0, f) == 0, "not: a box at the top middle", z.w, z.h, pct, extra);
                Box low = MapBox(c, s, 2, false);
                low.top += (int)(0.3f * H); low.bottom += (int)(0.3f * H);
                Check(minimapbox::Find(low, c, game, s, 0, f) == 0, "not: the map's size but far under the area's top", z.w, z.h, pct, extra);
            }
        }
    }
    std::printf("0.173's test missed %d of %d map boxes with a file that reads 1, %d with the file's number right (the outer edge at the picture's\n"
                "edge at 80 %%); this one: %s\n", oldMissed, cases, oldMissedTrue, g_fail ? "see the failures" : "none");
    Check(oldMissed > 0, "the old test is shown to miss boxes (the reason for this one)", 0, 0, 0);

    // The game's map box capped at ~640 px (Serg's log 2026-11-10, square pictures, interface 80 %, F4 stretch 1.04): every measured box found,
    // the 1024 one too - wider than half the picture, taller than 0.6 of it, past its right edge - and what is not the map still refused there.
    {
        struct M { unsigned n; Box b; } measured[] = {
            {1024, {512, 132, 1151, 771}}, {1280, {512, 165, 1152, 805}}, {1536, {742, 197, 1381, 836}},
            {1920, {1088, 247, 1728, 887}}, {2816, {1895, 362, 2534, 1001}}};
        for (const M& m : measured) {
            const Canvas c{0, 0, m.n, m.n};
            const float game = minimapbox::SafeFromScale(GameScale(m.n, 0.8f), m.n);
            Check(minimapbox::Find(m.b, c, game, 0.8f, 0, 1.04f) == 2, "a measured capped box found (right)", m.n, m.n, 80);
            Check(minimapbox::Find(m.b, c, game, 0.8f, 1, 1.04f) == 0, "a measured capped box: the left corner asked for", m.n, m.n, 80);
            Check(minimapbox::WhyNot(m.b, c, 0.8f, 0, 1.04f) == nullptr, "a measured capped box: no reason against it", m.n, m.n, 80);
            const Box mirror{(int)m.n - m.b.right, m.b.top, (int)m.n - m.b.left, m.b.bottom};
            Check(minimapbox::Find(mirror, c, game, 0.8f, 0, 1.04f) == 1, "the same box in the left corner found (left)", m.n, m.n, 80);
        }
        {   // PS VR2 (Serg 2026-11-10, 0.211.1): 1664 square, the F4 frustum makes the tiles 0.72 as wide - the box stays 639 px
            const Canvas p{0, 0, 1664, 1664};
            const float g = minimapbox::SafeFromScale(GameScale(1664, 0.8f), 1664);
            const Box psvr2{857, 214, 1496, 853};
            Check(minimapbox::Find(psvr2, p, g, 0.8f, 0, 0.72f) == 2, "PS VR2 1664 at stretch 0.72: the capped box found", 1664, 1664, 80);
            Check(minimapbox::WhyNot(psvr2, p, 0.8f, 0, 0.72f) == nullptr, "PS VR2 1664: no reason against it", 1664, 1664, 80);
            const Box mid{512, 214, 1151, 853};
            Check(minimapbox::Find(mid, p, g, 0.8f, 0, 0.72f) == 0, "PS VR2 1664: the same box at the top middle refused", 1664, 1664, 80);
        }
        const Canvas c{0, 0, 1024, 1024};
        const float game = minimapbox::SafeFromScale(GameScale(1024, 0.8f), 1024);
        const Box whole{0, 0, 1024, 1024}, toolbar{0, 789, 1024, 921}, small{1012, 789, 1143, 921};
        const Box low{512, 432, 1151, 1071}, tall{512, 132, 1151, 1024}, wide{300, 132, 1100, 771};
        Check(minimapbox::Find(whole, c, game, 0.8f, 0, 1.04f) == 0, "1024: not the whole picture", 1024, 1024, 80);
        Check(minimapbox::Find(toolbar, c, game, 0.8f, 0, 1.04f) == 0, "1024: not the toolbar's strip", 1024, 1024, 80);
        Check(minimapbox::Find(small, c, game, 0.8f, 0, 1.04f) == 0, "1024: not a small box in the corner", 1024, 1024, 80);
        Check(minimapbox::Find(low, c, game, 0.8f, 0, 1.04f) == 0, "1024: not the capped size far under the area's top", 1024, 1024, 80);
        Check(minimapbox::Find(tall, c, game, 0.8f, 0, 1.04f) == 0, "1024: not a capped width much taller than wide", 1024, 1024, 80);
        Check(minimapbox::Find(wide, c, game, 0.8f, 0, 1.04f) == 0, "1024: not a box wider than the cap", 1024, 1024, 80);
    }
    // The box is a third of the game WINDOW's client width (WindowBox, 2026-11-14): the players' logs - window -> box, every picture
    {
        struct P { unsigned win; int box; } seen[] = {{987, 328}, {1380, 459}, {1392, 477}, {1920, 639}, {1920, 640}};
        for (const P& q : seen) {
            Check(minimapbox::WindowBox((float)q.box, (float)q.box, 3264.0f, q.win), "a player's box is a third of the window", q.win, q.win, 0);
            Check(!minimapbox::WindowBox((float)q.box * 1.3f, (float)q.box, 3264.0f, q.win), "a box 30 % wider is not", q.win, q.win, 0);
            Check(!minimapbox::WindowBox((float)q.box * 0.7f, (float)q.box, 3264.0f, q.win), "a box 30 % narrower is not", q.win, q.win, 0);
        }
        Check(minimapbox::WindowBox(640.0f, 360.0f, 1080.0f, 1920), "1920x1080: 640 x 360", 1920, 1080, 0);
        Check(!minimapbox::WindowBox(640.0f, 200.0f, 1080.0f, 1920), "not a flat strip of that width", 1920, 1080, 0);
        Check(!minimapbox::WindowBox(340.0f, 340.0f, 3264.0f, 0), "no window known: no window rule", 0, 0, 0);
        Check(!minimapbox::WindowBox(100.0f, 100.0f, 3264.0f, 300), "a window too small to be known: no window rule", 300, 300, 0);
    }
    {   // zeezour's TV (2026-11-13): the main display 1920x1080, the window 1020x1020 under a 3072x3264 picture, interface 74 %: the box a
        // third of the window (340), at the right area's side and top as at 1380 (his box there 2212,481 - 2671,940 on 3072x3072)
        const Canvas c{0, 0, 3072, 3264};
        const float game = minimapbox::SafeFromScale(GameScale(3264, 0.74f), 3264);
        const int side = (int)std::lround(0.5f * 3072.0f * 0.26f), top = (int)std::lround(0.5f * 3264.0f * 0.26f) + 82;
        const Box tv{3072 - side - 340, top, 3072 - side, top + 340};
        Check(minimapbox::Find(tv, c, game, 0.74f, 0, 0.98f) == 0, "the TV's box was refused before (no map in the hand)", 3072, 3264, 74);
        Check(minimapbox::Find(tv, c, game, 0.74f, 0, 0.98f, 1020) == 2, "the TV's box found with the window's width", 3072, 3264, 74);
        Check(minimapbox::WhyNot(tv, c, game, 0, 0.98f, 1020) == nullptr, "the TV's box: no reason against it", 3072, 3264, 74);
        const char* before = minimapbox::WhyNot(tv, c, game, 0, 0.98f);
        Check(before && std::strcmp(before, "width") == 0, "the TV's box: 'width' without the window", 3072, 3264, 74);
        Check(minimapbox::Find(tv, c, game, 0.74f, 1, 0.98f, 1020) == 0, "the TV's box: not when the left corner is asked for", 3072, 3264, 74);
        Box mirror = tv;
        mirror.left = side; mirror.right = side + 340;
        Check(minimapbox::Find(mirror, c, game, 0.74f, 0, 0.98f, 1020) == 1, "the TV's box in the left corner found", 3072, 3264, 74);
        // not the map at that window either: the map's size far under the area's top, in from its side, at the top middle; the toolbar strip
        Box low = tv; low.top += 900; low.bottom += 900;
        Box in = tv; in.left -= 500; in.right -= 500;
        const Box middle{1366, top, 1706, top + 340}, strip{0, 2700, 3072, 3000}, whole{0, 0, 3072, 3264};
        Check(minimapbox::Find(low, c, game, 0.74f, 0, 0.98f, 1020) == 0, "TV: not the box far under the area's top", 3072, 3264, 74);
        Check(minimapbox::Find(in, c, game, 0.74f, 0, 0.98f, 1020) == 0, "TV: not the box well in from the area's side", 3072, 3264, 74);
        Check(minimapbox::Find(middle, c, game, 0.74f, 0, 0.98f, 1020) == 0, "TV: not the box at the top middle", 3072, 3264, 74);
        Check(minimapbox::Find(strip, c, game, 0.74f, 0, 0.98f, 1020) == 0 && minimapbox::Find(whole, c, game, 0.74f, 0, 0.98f, 1020) == 0,
              "TV: not the toolbar's strip, not the whole picture", 3072, 3264, 74);
        // his 1380 window (no TV): the box he had is found as before, with the window or without
        const Canvas sq{0, 0, 3072, 3072};
        const float g2 = minimapbox::SafeFromScale(GameScale(3072, 0.74f), 3072);
        const Box his{2212, 481, 2671, 940};
        Check(minimapbox::Find(his, sq, g2, 0.74f, 0, 0.98f) == 2 && minimapbox::Find(his, sq, g2, 0.74f, 0, 0.98f, 1380) == 2,
              "1380 window: his box found, the window known or not", 3072, 3072, 74);
    }
    {   // Serg's 1024 picture in his 1920 window: the window rule takes the capped box, and refuses what the cap refused
        const Canvas c{0, 0, 1024, 1024};
        const float game = minimapbox::SafeFromScale(GameScale(1024, 0.8f), 1024);
        const Box capped{512, 132, 1151, 771}, low{512, 432, 1151, 1071}, tall{512, 132, 1151, 1024}, wide{300, 132, 1100, 771};
        Check(minimapbox::Find(capped, c, game, 0.8f, 0, 1.04f, 1920) == 2, "1024 in a 1920 window: the box found", 1024, 1024, 80);
        Check(minimapbox::Find(low, c, game, 0.8f, 0, 1.04f, 1920) == 0 && minimapbox::Find(tall, c, game, 0.8f, 0, 1.04f, 1920) == 0 &&
              minimapbox::Find(wide, c, game, 0.8f, 0, 1.04f, 1920) == 0, "1024 in a 1920 window: low / tall / wide still refused", 1024, 1024, 80);
    }
    std::printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "passed", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
