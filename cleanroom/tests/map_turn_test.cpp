// [hud] map_top_down / map_rotate / map_compass (vr/map_turn.h): the map in the hand seen from above and turned with the look - the way one
// looks comes out up, the map's tile edges come out square, the hero stays in the middle, the circle stays in the box - and the compass's window.
#include "map_turn.h"

#include <cmath>
#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }
float Deg(float r) { return r * 180.0f / mapturn::kPi; }

// A uv step of the share back to the map's own pixels (the stretch narrowed back): x right, y down.
void ToMapPx(const mapturn::Map& m, float qx, float qy, float w, float h, float stretch, float* x, float* y) {
    float u, v;
    mapturn::Apply(m, qx, qy, &u, &v);
    *x = (u - m.hero[0]) * w / stretch;
    *y = (v - m.hero[1]) * h;
}
}   // namespace

int main() {
    using namespace mapturn;
    const float W = 639.0f, H = 452.0f;   // the author's corner map box (2048 layer, 80 %)
    {   // off: nothing to do
        const Turn t = For(false, false, true, 1.0f);
        Check(!t.Any(), "both keys off: no turn");
        Check(!MapFor(t, W, H, 1.0f).ok, "both keys off: no map");
    }
    {   // rotate needs a heading; without one it is the view from above, north up
        const Turn t = For(false, true, false, 1.0f);
        Check(t.topDown && !t.turning, "rotate with no heading: from above, not turning");
        const Turn u = For(false, true, true, 1.0f);
        Check(u.topDown && u.turning && Near(u.heading, 1.0f - kTopDownTurn), "rotate: from above, turning, the heading 45 off the bearing");
    }
    {   // from above, north up: the tile edges square and upright, the map's own up at the up-left diagonal
        const Turn t = For(true, false, true, 0.0f);
        const Map m = MapFor(t, W, H, 1.0f);
        Check(m.ok, "top-down: a map");
        Check(Near(m.hero[0], 0.5f) && Near(m.hero[1], 0.5f - mapdir::kMarkerUp), "the hero's marker in the middle of the picture");
        float ax, ay, bx, by;
        ToMapPx(m, 1.0f, 0.0f, W, H, 1.0f, &ax, &ay);   // the picture's right
        ToMapPx(m, 0.0f, 1.0f, W, H, 1.0f, &bx, &by);   // its down
        // on the map: tile edges, (1, 1/2) and (-1, 1/2) times something - a diamond's sides
        Check(Near(ay / ax, 0.5f) && Near(by / bx, -0.5f), "top-down: the picture's right and down are the map's tile edges");
        Check(Near((ax * ax + ay * ay) / (bx * bx + by * by), 1.0f), "top-down: both edges as long (rooms square)");
        // the map's up (0, -1) comes from the picture's up-left diagonal
        float ux, uy;
        ToMapPx(m, -0.70710678f, -0.70710678f, W, H, 1.0f, &ux, &uy);
        Check(Near(ux, 0.0f, 1e-2f) && uy < 0.0f, "top-down, north up: the map's up is the picture's up-left");
    }
    {   // turning: the way one looks comes out up, for every bearing, wider maps too
        const float stretches[] = {1.0f, 1.3f, 0.8f};
        int bad = 0;
        for (float st : stretches) {
            for (int deg = -180; deg < 180; deg += 15) {
                const float b = (float)deg * kPi / 180.0f;
                const float gx = std::sin(b), gy = std::cos(b);   // the way on the ground (x right, y up of the game screen)
                Check(Near(Bearing(gx, gy), b, 1e-3f) || Near(std::fabs(b), kPi, 1e-3f), "Bearing: clockwise from the map's up");
                const Map m = MapFor(For(false, true, true, Bearing(gx, gy)), W * st, H, st);
                const mapdir::Dir want = mapdir::ToMap(mapdir::Dir{gx, gy, true});   // the look on the map's pixels (y down)
                float x, y;
                ToMapPx(m, 0.0f, -1.0f, W * st, H, st, &x, &y);   // the picture's up
                const float l = std::sqrt(x * x + y * y);
                if (!(l > 0.0f) || !Near(x / l, want.x, 2e-3f) || !Near(y / l, want.y, 2e-3f)) {
                    if (++bad <= 4) std::printf("  bearing %d deg, stretch %.1f: up shows %.3f,%.3f on the map, the look is %.3f,%.3f\n", deg, st, x / l, y / l, want.x, want.y);
                }
            }
        }
        Check(bad == 0, "turning: the picture's up is the way one looks on the map");
    }
    {   // nothing the box shows about the hero is lost: its inscribed ellipse (the box's width and height from the hero) is inside the circle at
        // every turn - the corner map 639 x 452 and the author's square 639 x 639 (2026-10-27 co-op: an ally up the map must stay) - and the
        // circle is no bigger than it must be (the ellipse touches it)
        const float boxes[2][2] = {{W, H}, {639.0f, 639.0f}};
        for (const auto& bx : boxes) {
            float most = 0.0f;
            bool inside = true;
            for (int deg = 0; deg < 360; deg += 5) {
                const Map m = MapFor(For(false, true, true, (float)deg * kPi / 180.0f), bx[0], bx[1], 1.0f);
                const float det = m.m[0] * m.m[3] - m.m[1] * m.m[2];
                for (int a = 0; a < 360; a += 3) {   // a point of the ellipse, uv off the hero, back to the picture's q
                    const float du = 0.5f * std::cos((float)a * kPi / 180.0f), dv = (0.5f - mapdir::kMarkerUp) * std::sin((float)a * kPi / 180.0f);
                    const float qx = (m.m[3] * du - m.m[1] * dv) / det, qy = (-m.m[2] * du + m.m[0] * dv) / det;
                    const float r = std::sqrt(qx * qx + qy * qy);
                    if (r > 1.0005f) inside = false;
                    most = std::fmax(most, r);
                }
            }
            Check(inside, "the box's ellipse about the hero stays inside the circle at every turn");
            Check(most > 0.98f, "the circle just holds it (not shrunk past it)");
        }
        const Map z = MapFor(For(true, false, true, 0.0f), W, H, 1.0f, 2.0f), o = MapFor(For(true, false, true, 0.0f), W, H, 1.0f, 1.0f);
        Check(Near(z.m[0] * 2.0f, o.m[0]) && Near(z.m[3] * 2.0f, o.m[3]), "the orb's zoom 2: half as far");
    }
    {   // the big map: about the hero at the canvas middle of a 2496 x 2688 layer, its rows above the toolbar (2100): the look up, the
        // ellipse to the nearer edges inside the circle, nothing below the toolbar's top reached by the ellipse
        const float w = 2496.0f, h = 2688.0f, rows = 2100.0f, hu = 0.5f, hv = 0.5f;
        const float halfW = std::fmin(hu, 1.0f - hu) * w, halfH = std::fmin(hv * h, rows - hv * h);
        for (int deg = -180; deg < 180; deg += 30) {
            const float b = (float)deg * kPi / 180.0f;
            const Map m = MapAround(For(false, true, true, b), w, h, 1.04f, hu, hv, halfW, halfH);
            Check(m.ok && Near(m.hero[0], hu) && Near(m.hero[1], hv), "the big map: about the hero");
            float u, v;
            Apply(m, 0.0f, -1.0f, &u, &v);
            const mapdir::Dir want = mapdir::ToMap(mapdir::Dir{std::sin(b), std::cos(b), true});
            const float x = (u - hu) * w / 1.04f, y = (v - hv) * h, l = std::sqrt(x * x + y * y);
            Check(Near(x / l, want.x, 2e-3f) && Near(y / l, want.y, 2e-3f), "the big map: the look is up");
        }
        const Map m = MapAround(For(true, false, true, 0.0f), w, h, 1.04f, hu, hv, halfW, halfH);
        const float det = m.m[0] * m.m[3] - m.m[1] * m.m[2];
        bool inside = true;
        for (int a = 0; a < 360; a += 3) {
            const float du = halfW / w * std::cos((float)a * kPi / 180.0f), dv = halfH / h * std::sin((float)a * kPi / 180.0f);
            const float qx = (m.m[3] * du - m.m[1] * dv) / det, qy = (-m.m[2] * du + m.m[0] * dv) / det;
            if (std::sqrt(qx * qx + qy * qy) > 1.0005f) inside = false;
        }
        Check(inside, "the big map: the ellipse to the nearer edges inside the circle");
        Check(!MapAround(For(true, false, true, 0.0f), w, h, 1.0f, hu, hv, 0.0f, halfH).ok, "no room about the hero: no map");
    }
    {   // a bad box or stretch: no map, or a clamped one
        Check(!MapFor(For(true, false, true, 0.0f), 0.0f, H, 1.0f).ok, "no width: no map");
        Check(MapFor(For(true, false, true, 0.0f), W, H, NAN).ok, "a stretch that is not a number: taken as 1");
    }
    {   // the head's turn held while it looks down at the hand
        HeadHold k;
        Check(!k.Update(0.0f, -0.1f), "nothing known while looking straight down from the start");
        Check(k.Update(-1.0f, 0.0f) && Near(Deg(k.roomYaw), 90.0f), "level, looking along -x: 90 degrees");
        Check(k.Update(0.0f, -0.15f) && Near(Deg(k.roomYaw), 90.0f), "then straight down: the last turn held");
        Check(k.Update(0.0f, -0.5f) && Near(Deg(k.roomYaw), 0.0f), "level again along -z: 0");
    }
    {   // the compass: the window's middle is the bearing's label
        Check(Crop(0.0f) == 0, "north: the window from the strip's start");
        Check(StepAt(Crop(0.0f) + kWindow / 2) % 32 == 0, "north: N in the window's middle");
        Check(StepAt(Crop(kPi / 2) + kWindow / 2) % 32 == 8, "east: E (step 8) in the middle");
        Check(StepAt(Crop(-kPi / 2) + kWindow / 2) % 32 == 24, "west (as -90): W (step 24) in the middle");
        Check(StepAt(Crop(kPi) + kWindow / 2) % 32 == 16, "south: S in the middle");
        Check(Crop(kTau + 0.1f) == Crop(0.1f) && Crop(-kTau + 0.1f) == Crop(0.1f), "a turn more or less: the same window");
        Check(Crop(NAN) == 0, "not a number: north");
        for (int deg = -720; deg <= 720; deg += 7) {
            const int c = Crop((float)deg * kPi / 180.0f);
            if (c < 0 || c + kWindow > kAtlasW) { Check(false, "the window inside the strip"); break; }
        }
    }
    std::printf(g_fails ? "map_turn_test: %d failed\n" : "map_turn_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
