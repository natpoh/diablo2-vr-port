// [hud] map_direction (vr/map_direction.h): the way our camera looks, on the game's isometric map - the game screen's up is the map's up, the
// map's diamonds halve the up part, the game camera's tilt and turn play no part, its mirrors do - and the arrow as rows of pixels.
#include "map_direction.h"

#include <cmath>
#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }
constexpr float kPi = 3.14159265f;

// The game's camera turned `yaw` about the world's up and looking `pitch` below the level: its screen right and up in the world (y up).
struct Game { float r[3], u[3]; };
Game GameCam(float yawDeg, float pitchDeg) {
    const float y = yawDeg * kPi / 180.0f, p = pitchDeg * kPi / 180.0f;
    const float fx = std::sin(y), fz = std::cos(y);   // its forward on the ground
    Game g;
    g.r[0] = fz; g.r[1] = 0.0f; g.r[2] = -fx;
    g.u[0] = fx * std::sin(p); g.u[1] = std::cos(p); g.u[2] = fz * std::sin(p);
    return g;
}
// A way on the ground `deg` clockwise from the game camera's forward (seen from above, the game's right at +90).
void Ground(float yawDeg, float deg, float out[3]) {
    const float a = (yawDeg + deg) * kPi / 180.0f;
    out[0] = std::sin(a); out[1] = -0.2f; out[2] = std::cos(a);   // (a little down: only the ground part counts)
}
// The arrow's angle on the map clockwise from its up, degrees.
float MapDeg(const mapdir::Dir& d) { return std::atan2(d.x, -d.y) * 180.0f / kPi; }
}   // namespace

int main() {
    using namespace mapdir;
    {   // the four ways, and the diagonal through the map's diamonds
        const Game g = GameCam(0.0f, 50.0f);
        float f[3];
        Ground(0.0f, 0.0f, f);
        Dir d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(d.x, 0.0f) && Near(d.y, -1.0f), "the game's forward: up the map");
        Ground(0.0f, 90.0f, f);
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(d.x, 1.0f) && Near(d.y, 0.0f), "the game's right: right on the map");
        Ground(0.0f, 180.0f, f);
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(d.x, 0.0f) && Near(d.y, 1.0f), "behind: down the map");
        Ground(0.0f, -90.0f, f);
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(d.x, -1.0f) && Near(d.y, 0.0f), "the game's left: left on the map");
        Ground(0.0f, 45.0f, f);
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(MapDeg(d), 63.435f, 0.01f), "45 on the ground (a tile's edge) is 63.4 off the map's up: the diamonds' 2:1");
        Check(Near(-d.y / d.x, 0.5f), "... the slope of the map's own lines");
        Ground(0.0f, -135.0f, f);
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
        Check(d.ok && Near(MapDeg(d), -116.565f, 0.01f), "-135 on the ground: -116.6 on the map");
        d = OnMap(f, g.r, g.u, 1.0f, 1.0f, 1.0f);
        Check(d.ok && Near(MapDeg(d), -135.0f, 0.01f), "no squash: the angle on the ground as it is");
    }
    {   // the game camera's tilt and turn play no part; every way round comes out the same
        bool same = true, unit = true, turnsOn = true;
        float last = -1000.0f;
        for (float a = -170.0f; a <= 170.0f; a += 10.0f) {   // (not 180: there the map's angle is +180 or -180 as it rounds)
            float ref = 0.0f;
            for (int k = 0; k < 6; ++k) {
                const float yaw = -120.0f + 47.0f * (float)k, pitch = 25.0f + 9.0f * (float)k;
                const Game g = GameCam(yaw, pitch);
                float f[3];
                Ground(yaw, a, f);
                const Dir d = OnMap(f, g.r, g.u, 1.0f, 1.0f);
                if (!d.ok) { same = false; continue; }
                unit = unit && Near(d.x * d.x + d.y * d.y, 1.0f);
                const float m = MapDeg(d);
                if (k == 0) ref = m;
                else same = same && Near(m, ref, 0.01f);
            }
            if (a > -170.0f) turnsOn = turnsOn && ref > last;   // clockwise on the ground: clockwise on the map
            last = ref;
        }
        Check(same, "the same arrow whatever the game camera's turn and tilt");
        Check(unit, "unit length");
        Check(turnsOn, "turning right turns the arrow clockwise, all the way round");
    }
    {   // a mirrored projection mirrors the map
        const Game g = GameCam(30.0f, 40.0f);
        float f[3];
        Ground(30.0f, 60.0f, f);
        const Dir d = OnMap(f, g.r, g.u, 1.0f, 1.0f), mx = OnMap(f, g.r, g.u, -1.0f, 1.0f), my = OnMap(f, g.r, g.u, 1.0f, -1.0f);
        Check(mx.ok && Near(mx.x, -d.x) && Near(mx.y, d.y), "mirrored x: left for right");
        Check(my.ok && Near(my.x, d.x) && Near(my.y, -d.y), "mirrored y: down for up");
    }
    {   // nothing to show
        const Game g = GameCam(0.0f, 45.0f);
        const float down[3] = {0.0f, -1.0f, 0.0f};
        Check(!OnMap(down, g.r, g.u, 1.0f, 1.0f).ok, "looking straight down: no way on the ground");
        const float up[3] = {0.0f, 1.0f, 0.0f}, f[3] = {0.0f, 0.0f, 1.0f};
        Check(!OnMap(f, g.r, up, 1.0f, 1.0f).ok, "a game camera looking straight ahead (no ground in its up): none");
        const float nan[3] = {std::nanf(""), 0.0f, 1.0f};
        Check(!OnMap(nan, g.r, g.u, 1.0f, 1.0f).ok, "not a number: none");
    }
    {   // the arrow as rows: inside the box, its tip where it points, the dark rim under all of it
        const Rect clip{100, 50, 740, 450};
        const float cx = 420.0f, cy = 250.0f, len = LengthFor(400.0f);
        Check(Near(len, 20.0f), "a 400 px box: a 20 px arrow");
        Check(LengthFor(10.0f) == 8.0f && LengthFor(1e5f) == 96.0f, "the length's bounds");
        Rect rows[128], rim[128];
        const Dir up{0.0f, -1.0f, true};
        const int n = Rows(Arrow(cx, cy, up, len), clip, rows, 128);
        Check(n >= (int)len - 1 && n <= (int)len + 1, "pointing up: one row a pixel of its length");
        bool above = true, inside = true;
        int top = 1 << 30, widest = 0;
        for (int i = 0; i < n; ++i) {
            above = above && rows[i].bottom <= (int)(cy - 0.55f * len) + 1;
            inside = inside && rows[i].left >= clip.left && rows[i].right <= clip.right && rows[i].right > rows[i].left;
            top = rows[i].top < top ? rows[i].top : top;
            widest = rows[i].right - rows[i].left > widest ? rows[i].right - rows[i].left : widest;
        }
        Check(above, "the marker itself left clear: the arrow starts above it");
        Check(inside, "rows inside the box, none empty");
        Check(std::abs(top - (int)std::lround(cy - 1.55f * len)) <= 1, "its tip 1.55 lengths up (its last pixel row may be too thin to fill)");
        Check(widest >= (int)(0.9f * len) - 1 && widest <= (int)(0.9f * len) + 1, "its base 0.9 lengths wide");
        const int m = Rows(Rim(cx, cy, up, len, 1.5f), clip, rim, 128);
        bool covered = true;
        for (int i = 0; i < n; ++i) {
            bool c = false;
            for (int j = 0; j < m; ++j)
                c = c || (rim[j].top == rows[i].top && rim[j].left < rows[i].left && rim[j].right > rows[i].right);
            covered = covered && c;
        }
        Check(m > n && covered, "the rim wider than the arrow on every row and longer");
        // turned: its far end where the way points
        const float s = std::sqrt(0.8f);
        const Dir dr{s, std::sqrt(0.2f), true};   // right and a little down
        const int k = Rows(Arrow(cx, cy, dr, len), clip, rows, 128);
        int maxRight = 0;
        for (int i = 0; i < k; ++i) maxRight = rows[i].right > maxRight ? rows[i].right : maxRight;
        Check(k > 0 && std::abs(maxRight - (int)std::lround(cx + dr.x * 1.55f * len)) <= 1, "turned: the tip out where it points");
        // at the box's edge: cut, never outside
        const Rect small{410, 240, 425, 255};
        const int c = Rows(Arrow(cx, cy, up, len), small, rows, 128);
        bool in = true;
        for (int i = 0; i < c; ++i) in = in && rows[i].top >= small.top && rows[i].bottom <= small.bottom && rows[i].left >= small.left && rows[i].right <= small.right;
        Check(in, "cut at the clip");
        Check(Rows(Arrow(cx, cy, up, len), clip, rows, 4) == 4, "at most max rows");
    }
    {   // Ready: nothing drawn unless all is in place (the 0.179 run that ended at the map's first opening, VR F4, 2048x2048)
        const Dir up{0.0f, -1.0f, true};
        const Rect box{1204, 264, 1843, 903};
        Rect c = box;
        Check(Ready(up, 1523.5f, 583.5f, 639.0f, 2048, 2048, 2, &c) && c.left == 1204 && c.right == 1843, "that run's box: ready, as it is");
        c = box;
        Check(!Ready(up, 1523.5f, 583.5f, 639.0f, 2048, 2048, 1, &c), "the map's first pass: not yet");
        c = box;
        Check(!Ready(up, 1523.5f, 583.5f, 639.0f, 2048, 2048, 0, &c), "no map drawn: none");
        c = box;
        const Dir zero{0.0f, 0.0f, true}, nan{std::nanf(""), -1.0f, true}, big{3.0f, 0.0f, true};
        Check(!Ready(zero, 1523.5f, 583.5f, 639.0f, 2048, 2048, 5, &c) && !Ready(nan, 1523.5f, 583.5f, 639.0f, 2048, 2048, 5, &c) &&
              !Ready(big, 1523.5f, 583.5f, 639.0f, 2048, 2048, 5, &c), "a way of no length, not a number or not unit: none");
        c = box;
        Check(!Ready(up, std::nanf(""), 583.5f, 639.0f, 2048, 2048, 5, &c) && !Ready(up, 1523.5f, 583.5f, std::nanf(""), 2048, 2048, 5, &c),
              "a marker or a height not a number: none");
        c = box;
        Check(!Ready(up, 1523.5f, 583.5f, 10.0f, 2048, 2048, 5, &c), "a box too small: none");
        c = box;
        Check(!Ready(up, 1523.5f, 583.5f, 639.0f, 0, 0, 5, &c), "no target size: none");
        c = box;
        Check(!Ready(up, 1523.5f, 583.5f, 639.0f, 1024, 1024, 5, &c), "the box outside a smaller target: none");
        c = Rect{1800, 264, 2400, 903};
        Check(Ready(up, 1900.0f, 583.5f, 639.0f, 2048, 2048, 5, &c) && c.right == 2048, "a box over the target's edge: cut to it");
        c = box;
        Check(!Ready(up, 100.0f, 583.5f, 639.0f, 2048, 2048, 5, &c), "the marker outside the box: none");
        Check(!Ready(up, 1523.5f, 583.5f, 639.0f, 2048, 2048, 5, nullptr), "no clip: none");
        // the largest arrow fits the rows' buffer
        Rect rows[kMaxRows];
        const Rect all{0, 0, 4096, 4096};
        const float len = LengthFor(1e6f);
        bool fits = true;
        for (float a = 0.0f; a < 6.3f; a += 0.1f) {
            const Dir d{std::sin(a), -std::cos(a), true};
            fits = fits && Rows(Rim(2048.0f, 2048.0f, d, len, std::max(1.0f, 0.08f * len)), all, rows, kMaxRows) < kMaxRows;
        }
        Check(fits, "the largest arrow and its rim in under kMaxRows rows, any way round");
    }
    {   // the arrow lies on the map: the ground's triangle through the map's 2:1 projection (the author in the headset: "the map is
        // isometric, and the arrow must be isometric")
        const Game g = GameCam(0.0f, 50.0f);
        // the cells' four ways, 45 degrees off the game screen's up on the ground: along the map's diagonals, 2 across to 1 up
        const float cells[4] = {45.0f, 135.0f, -135.0f, -45.0f};
        const float sx[4] = {1.0f, 1.0f, -1.0f, -1.0f}, sy[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        for (int k = 0; k < 4; ++k) {
            float f[3];
            Ground(0.0f, cells[k], f);
            const Dir gd = OnGround(f, g.r, g.u, 1.0f, 1.0f);
            const Dir md = ToMap(gd);
            const float n = std::sqrt(1.25f);
            Check(gd.ok && md.ok && Near(md.x, sx[k] / n) && Near(md.y, 0.5f * sy[k] / n), "a cell's way: along the map's diagonal, 2:1");
            const Tri t = IsoArrow(500.0f, 400.0f, gd, 20.0f);
            const float tx = t.p[0].x - 500.0f, ty = t.p[0].y - 400.0f;
            Check(Near(tx * sy[k] * 0.5f - ty * sx[k], 0.0f, 1e-3f) && tx * sx[k] > 0.0f, "... its tip out along that diagonal");
        }
        const Dir up{0.0f, 1.0f, true}, right{1.0f, 0.0f, true};
        const float len = 20.0f, cx = 420.0f, cy = 250.0f;
        Tri t = IsoArrow(cx, cy, up, len);
        Check(Near(t.p[0].x, cx) && Near(t.p[0].y, cy - 1.55f * len * 0.5f), "up the map: the tip half as far up (the map's halving)");
        Check(Near(std::fabs(t.p[1].x - t.p[2].x), 0.9f * len) && Near(t.p[1].y, cy - 0.55f * len * 0.5f), "... its base as wide, half as far out");
        t = IsoArrow(cx, cy, right, len);
        Check(Near(t.p[0].x, cx + 1.55f * len) && Near(t.p[0].y, cy), "across the map: the tip as far");
        Check(Near(std::fabs(t.p[1].y - t.p[2].y), 0.9f * len * 0.5f), "... its base half as tall: flat on the map");
        bool onLine = true, halfArea = true;
        auto area = [](const Tri& q) {
            return 0.5f * std::fabs((q.p[1].x - q.p[0].x) * (q.p[2].y - q.p[0].y) - (q.p[1].y - q.p[0].y) * (q.p[2].x - q.p[0].x));
        };
        for (float a = 0.05f; a < 6.3f; a += 0.1f) {   // any way round: the tip on the way the map shows, the area half the ground's
            const Dir gd{std::sin(a), std::cos(a), true};
            const Dir md = ToMap(gd);
            const Tri iso = IsoArrow(0.0f, 0.0f, gd, len), flat = Arrow(0.0f, 0.0f, gd, len);
            const float cross = iso.p[0].x * md.y - iso.p[0].y * md.x, dot = iso.p[0].x * md.x + iso.p[0].y * md.y;
            onLine = onLine && Near(cross, 0.0f, 1e-3f) && dot > 0.0f;
            halfArea = halfArea && Near(area(iso), 0.5f * area(flat), 1e-2f);
        }
        Check(onLine, "the tip on the way the map shows, any way round");
        Check(halfArea, "the arrow's area half the ground's, as a tile's");
        Rect rows[kMaxRows];
        const Rect clip{0, 0, 2048, 2048};
        const int n = Rows(IsoArrow(cx, cy, up, len), clip, rows, kMaxRows);
        Check(n >= (int)(0.5f * len) - 1 && n <= (int)(0.5f * len) + 1, "up the map: half as many rows");
        bool fits = true;
        const float big = LengthFor(1e6f);
        for (float a = 0.0f; a < 6.3f; a += 0.1f) {
            const Dir gd{std::sin(a), std::cos(a), true};
            fits = fits && Rows(Grow(IsoArrow(2048.0f, 2048.0f, gd, big), std::max(1.0f, 0.08f * big)), Rect{0, 0, 4096, 4096}, rows, kMaxRows) < kMaxRows;
        }
        Check(fits, "the largest iso arrow and its rim fit the rows' buffer");
        // OnMap is OnGround through the projection
        float f[3];
        Ground(0.0f, 30.0f, f);
        const Dir a = OnMap(f, g.r, g.u, 1.0f, 1.0f), b = ToMap(OnGround(f, g.r, g.u, 1.0f, 1.0f));
        Check(a.ok && Near(a.x, b.x) && Near(a.y, b.y), "OnMap = ToMap(OnGround)");
    }
    {   // clear of the hero's ring (the author: "move it higher and further from the cross"): the run's box, 639 px on a 2048 layer
        const float bx = 1523.5f, by = 583.5f, bh = 639.0f;
        const Place p = PlaceFor(bx, by, bh);
        Check(Near(p.cx, bx) && Near(by - p.cy, 8.946f, 0.01f), "about the ring's middle: 9 px above the box's");
        Check(Near(p.len, 31.95f, 0.01f) && Near(p.gap * p.len, 2.0f * 0.031f * bh, 0.01f), "its base two ring radii out on the ground");
        const float r = kMarkerRadius * bh;   // the ring: r across, r / 2 up and down (it lies on the ground)
        // the worst a pixel of the arrow or its rim comes to the ring, any way round: (dx / r)^2 + (dy / (r/2))^2, square-rooted
        auto nearest = [&](float cx, float cy, float gap) {
            Rect rows[kMaxRows];
            float worst = 1e9f;
            for (float a = 0.0f; a < 6.3f; a += 0.05f) {
                const Dir gd{std::sin(a), std::cos(a), true};
                const int n = Rows(Grow(IsoArrow(cx, cy, gd, p.len, kIsoSquash, gap), std::max(1.0f, 0.08f * p.len)), Rect{0, 0, 2048, 2048}, rows, kMaxRows);
                for (int i = 0; i < n; ++i)
                    for (int x = rows[i].left; x < rows[i].right; ++x) {
                        const float dx = ((float)x + 0.5f - p.cx) / r, dy = ((float)rows[i].top + 0.5f - p.cy) / (0.5f * r);
                        worst = std::min(worst, std::sqrt(dx * dx + dy * dy));
                    }
            }
            return worst;
        };
        const float now = nearest(p.cx, p.cy, p.gap), before = nearest(bx, by, 0.55f);
        std::printf("map_direction_test: the arrow's nearest pixel to the ring - %.2f ring radii (54b28c59's place: %.2f)\n", now, before);
        Check(now > 1.3f, "no pixel of the arrow or its rim on the ring, any way round, with room between");
        Check(before < 1.0f, "54b28c59's place (the box's middle, 0.55 lengths out) was on the ring - as the shots showed");
        Rect c{1204, 264, 1843, 903};
        Check(Ready(Dir{0.0f, 1.0f, true}, p.cx, p.cy, bh, 2048, 2048, 2, &c), "ready about the ring's middle");
    }
    std::printf(g_fails ? "map_direction_test: %d failed\n" : "map_direction_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
