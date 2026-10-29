#pragma once
// map_direction.h - [hud] map_direction: an arrow on the hero's marker of the game's map, where our camera looks. Pure: no game, no Windows
// (cleanroom/tests/map_direction_test.cpp).
//
// The game's map (the corner mini map and the whole-screen one) is drawn for the game's own camera: its screen's up is the game camera's
// up, whatever our camera does. In the game's view from above that is enough - the picture and the map turn together. With our camera
// (flat F2 / F3, VR F2 / F4) the map stays put while the view turns, and nothing on it says which way one looks (a player with a DualSense
// in flat third / first person asked for it). So vrcam hands hud.cpp the way our camera looks ON THE MAP, and hud.cpp paints a small
// arrow beside the hero's marker in the middle of the map's box once the game has drawn the map (visual only: the map and its data are
// the game's).
//
// The way on the map, from what vrcam has for the stick anyway (StickFrame): our camera's forward in the world, and the game camera's
// screen right and up in the world (columns of the game's view). On the ground (world y up): how much of our forward goes along the game
// screen's right and how much along its up, each axis taken flat and of unit length - the map is a view from straight above turned so
// that the game's up is up. Then the map's own projection: D2's map is isometric, its tiles diamonds twice as wide as tall, so the up
// part is halved (kIsoSquash) - a look along a tile's edge, 45 degrees off the game's up on the ground, comes out 26.6 degrees above the
// map's level (63.4 off its up), as the map's own lines go. The game camera's own tilt plays no part (its up has less of the ground the more it looks down; the
// map does not). A projection of the game's that mirrors an axis (mirX / mirY, as the stick's) mirrors the map's too.
//
// The arrow lies ON the map, as its other marks do (the author in the headset: "the map is isometric, and the arrow must be isometric"):
// a triangle drawn flat on the ground - its base `gap` lengths out from the marker along the way, its tip one length further - then put
// through the map's own projection, the same halving of the up part (IsoArrow). Its tip runs along the corridor one looks down; pointing
// up the map it comes out half as long, pointing across it its width is halved, as a tile's is. Painted as rows of pixel
// rectangles (hud.cpp clears them: no pipeline, no shader of ours in the game's interface pass), a dark one a little bigger under a light
// one so it reads on any part of the map.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mapdir {

constexpr float kIsoSquash = 0.5f;   // the map's diamonds: half as tall as wide

// A way: unit length when ok. On the map (OnMap) in its pixels, x right, y DOWN; on the ground (OnGround) x along the game screen's
// right and y along its UP, flat - before the map's projection.
struct Dir { float x = 0.0f, y = 0.0f; bool ok = false; };

// fwd: our camera's forward in the world; scrR / scrU: the game camera's screen right / up in the world; mirX / mirY: -1 where the game's
// projection mirrors that screen axis. The way on the ground, the game screen's right and up taken flat and the mirrors applied; not ok
// when our forward or the game's axes have no length on the ground (looking straight down).
inline Dir OnGround(const float fwd[3], const float scrR[3], const float scrU[3], float mirX, float mirY) {
    Dir d;
    float rx = scrR[0], rz = scrR[2];
    const float rl = std::sqrt(rx * rx + rz * rz);
    if (!(rl > 1e-4f)) return d;
    rx /= rl; rz /= rl;
    float ux = scrU[0], uz = scrU[2];
    const float along = ux * rx + uz * rz;   // (the game's camera has no roll: none; kept square anyway)
    ux -= along * rx; uz -= along * rz;
    const float ul = std::sqrt(ux * ux + uz * uz);
    if (!(ul > 1e-4f)) return d;
    ux /= ul; uz /= ul;
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
    if (!(fl > 1e-4f) || !std::isfinite(fl)) return d;
    const float a = (fwd[0] * rx + fwd[2] * rz) / fl;   // along the game screen's right
    const float b = (fwd[0] * ux + fwd[2] * uz) / fl;   // along its up
    d.x = a * (mirX < 0.0f ? -1.0f : 1.0f); d.y = b * (mirY < 0.0f ? -1.0f : 1.0f);
    d.ok = std::isfinite(d.x) && std::isfinite(d.y);
    return d;
}

// A point on the ground (x right, y up, from the marker) on the map: the up part halved, pixels y down.
struct Pt { float x = 0.0f, y = 0.0f; };
inline Pt Project(float gx, float gy, float squash = kIsoSquash) { return Pt{gx, -gy * squash}; }

// The way on the map's pixels (the ground's way through the map's projection, of unit length again).
inline Dir ToMap(const Dir& g, float squash = kIsoSquash) {
    Dir d;
    if (!g.ok) return d;
    const Pt p = Project(g.x, g.y, squash);
    const float l = std::sqrt(p.x * p.x + p.y * p.y);
    if (!(l > 1e-6f) || !std::isfinite(l)) return d;
    d.x = p.x / l; d.y = p.y / l; d.ok = true;
    return d;
}
inline Dir OnMap(const float fwd[3], const float scrR[3], const float scrU[3], float mirX, float mirY, float squash = kIsoSquash) {
    return ToMap(OnGround(fwd, scrR, scrU, mirX, mirY), squash);
}

struct Tri { Pt p[3]; };   // p[0] the tip
struct Rect { int32_t left = 0, top = 0, right = 0, bottom = 0; };

// The arrow about the marker at (cx, cy): base `gap` x len out along d, tip len further, half as wide as `half` x len each side.
inline Tri Arrow(float cx, float cy, const Dir& d, float len, float gap = 0.55f, float half = 0.45f) {
    const float nx = -d.y, ny = d.x;   // across
    const float bx = cx + d.x * gap * len, by = cy + d.y * gap * len;
    Tri t;
    t.p[0] = {cx + d.x * (gap + 1.0f) * len, cy + d.y * (gap + 1.0f) * len};
    t.p[1] = {bx + nx * half * len, by + ny * half * len};
    t.p[2] = {bx - nx * half * len, by - ny * half * len};
    return t;
}

// The arrow lying on the map: the same triangle about the marker at (cx, cy) on the ground (g: the way on the ground, OnGround; len and
// the rest in ground pixels - a length across the map), each corner then put through the map's projection.
inline Tri IsoArrow(float cx, float cy, const Dir& g, float len, float squash = kIsoSquash, float gap = 0.55f, float half = 0.45f) {
    const Tri flat = Arrow(0.0f, 0.0f, Dir{g.x, g.y, true}, len, gap, half);   // (on the ground, y up)
    Tri t;
    for (int i = 0; i < 3; ++i) {
        const Pt p = Project(flat.p[i].x, flat.p[i].y, squash);
        t.p[i] = {cx + p.x, cy + p.y};
    }
    return t;
}

// A triangle grown by `o` pixels all round: each edge moved out by o, the corners where the moved edges meet.
inline Tri Grow(const Tri& t, float o) {
    const float turn = (t.p[1].x - t.p[0].x) * (t.p[2].y - t.p[0].y) - (t.p[1].y - t.p[0].y) * (t.p[2].x - t.p[0].x);
    if (!(std::fabs(turn) > 1e-6f)) return t;
    const float side = turn > 0.0f ? 1.0f : -1.0f;
    Pt at[3], dir[3];   // edge e moved out: through at[e], along dir[e]
    for (int e = 0; e < 3; ++e) {
        const Pt& a = t.p[e];
        const Pt& b = t.p[(e + 1) % 3];
        const float vx = b.x - a.x, vy = b.y - a.y, l = std::sqrt(vx * vx + vy * vy);
        dir[e] = {vx, vy};
        at[e] = {a.x + side * vy / l * o, a.y - side * vx / l * o};
    }
    Tri g;
    for (int i = 0; i < 3; ++i) {   // corner i: where edges i-1 and i meet
        const int e1 = (i + 2) % 3, e2 = i;
        const float den = dir[e1].x * dir[e2].y - dir[e1].y * dir[e2].x;
        const float s = ((at[e2].x - at[e1].x) * dir[e2].y - (at[e2].y - at[e1].y) * dir[e2].x) / den;
        g.p[i] = {at[e1].x + s * dir[e1].x, at[e1].y + s * dir[e1].y};
    }
    return g;
}
// The dark rim under the arrow: the arrow grown by o.
inline Tri Rim(float cx, float cy, const Dir& d, float len, float o, float gap = 0.55f, float half = 0.45f) {
    return Grow(Arrow(cx, cy, d, len, gap, half), o);
}

// The arrow's length in pixels for a map box `boxH` tall (the corner map: 0.377 H s): a little under a twentieth of it, at least 8.
inline float LengthFor(float boxH) { return std::clamp(0.05f * boxH, 8.0f, 96.0f); }

// The hero's marker on the map, as the game draws it: a blue ring lying on the ground (an ellipse twice as wide as tall), its middle a
// little ABOVE the middle of the map's box, both in proportion to the box (the map's own scale). Measured off the author's headset shots
// of 54b28c59 (2026-10-25, VR F4, a 639 px box on a 2048 layer, the arrow pointing up, down, left and right): the ring 14 x 7 where the
// arrow was 11.5 long (32 px) - 39 x 19.5 px, half its width 0.031 of the box's height; its middle 3 - 3.5 px of those above the arrow's
// centre in all four - 9 px, 0.014 of the box's height. The arrow sat on the ring ("move it higher and further from the cross").
constexpr float kMarkerUp = 0.014f;       // the ring's middle above the box's middle, x the box's height
constexpr float kMarkerRadius = 0.031f;   // the ring's radius on the ground (half its width on the map), x the box's height
constexpr float kMarkerClear = 2.0f;      // the arrow's base this many ring radii out on the ground: a ring's width of map between them

// Where the arrow goes for a map box whose middle is (boxCx, boxCy) and height boxH: about the ring's middle, its length, and its base's
// distance out in lengths (the `gap` of Arrow / IsoArrow).
struct Place { float cx = 0.0f, cy = 0.0f, len = 0.0f, gap = 0.0f; };
inline Place PlaceFor(float boxCx, float boxCy, float boxH) {
    Place p;
    p.cx = boxCx;
    p.cy = boxCy - kMarkerUp * boxH;
    p.len = LengthFor(boxH);
    p.gap = p.len > 0.0f ? kMarkerClear * kMarkerRadius * boxH / p.len : 0.0f;
    return p;
}

constexpr int kMaxRows = 160;      // rows an arrow may take (LengthFor's 96 px with its rim stays under it)
constexpr int kClearBatch = 8;     // rectangles a clear: a few at a time
constexpr uint32_t kSteadyPasses = 2;   // interface passes the map was drawn in before its arrow

// Everything in place for an arrow (hud.cpp PaintArrow): the way finite and of unit length, the marker and the box's height finite, the
// box at least 32 px each way and inside the target (tw x th) - `clip` is cut to the target - the marker inside the box, and the map
// drawn `passes` interface passes running. False: nothing is drawn.
inline bool Ready(const Dir& d, float cx, float cy, float boxH, uint32_t tw, uint32_t th, uint32_t passes, Rect* clip) {
    if (!clip || passes < kSteadyPasses || tw < 64 || th < 64 || tw > 32768 || th > 32768) return false;
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(boxH)) return false;
    const float l2 = d.x * d.x + d.y * d.y;
    if (!(l2 > 0.81f && l2 < 1.21f) || !(boxH >= 32.0f)) return false;
    clip->left = std::max(clip->left, 0);
    clip->top = std::max(clip->top, 0);
    clip->right = std::min(clip->right, (int32_t)tw);
    clip->bottom = std::min(clip->bottom, (int32_t)th);
    if (clip->right - clip->left < 32 || clip->bottom - clip->top < 32) return false;
    return cx >= (float)clip->left && cx <= (float)clip->right && cy >= (float)clip->top && cy <= (float)clip->bottom;
}

// The triangle as rows of pixels: every pixel whose middle is inside, one rectangle a row, kept inside `clip`. Returns how many (at most max).
inline int Rows(const Tri& t, const Rect& clip, Rect* out, int max) {
    float y0 = std::min({t.p[0].y, t.p[1].y, t.p[2].y}), y1 = std::max({t.p[0].y, t.p[1].y, t.p[2].y});
    if (!std::isfinite(y0) || !std::isfinite(y1)) return 0;
    const int r0 = std::max((int)std::ceil(y0 - 0.5f), clip.top), r1 = std::min((int)std::floor(y1 - 0.5f), clip.bottom - 1);
    int n = 0;
    for (int row = r0; row <= r1 && n < max; ++row) {
        const float y = (float)row + 0.5f;
        float xa = 1e30f, xb = -1e30f;
        for (int e = 0; e < 3; ++e) {
            const Pt& a = t.p[e];
            const Pt& b = t.p[(e + 1) % 3];
            if ((y < a.y) == (y < b.y)) continue;   // this row does not cross the edge (a flat edge never does)
            const float x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            xa = std::min(xa, x); xb = std::max(xb, x);
        }
        if (xb < xa) continue;
        const int left = std::max((int)std::ceil(xa - 0.5f), clip.left), right = std::min((int)std::floor(xb - 0.5f) + 1, clip.right);
        if (right <= left) continue;
        out[n++] = Rect{left, row, right, row + 1};
    }
    return n;
}

}  // namespace mapdir
