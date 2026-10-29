#pragma once
// map_turn.h - [hud] map_top_down, map_rotate, map_compass: the game's map in the hand (native OpenXR, VR F4) seen from straight above,
// turned so that where you look is up, and a compass strip over it. Pure: no game, no Windows, no D3D (cleanroom/tests/map_turn_test.cpp).
//
// Taken from bearstonem/d2r-pcvr ("Unleashed", MIT): its wrist map's VIEW Isometric / Top-down and ROTATE MAP (steamvr/ui_blit.cpp,
// cf1aef7) and its wrist compass (steamvr/wrist_compass.h, interaction_ui.cpp Paint, 566b306). Theirs is a shader that blits the captured
// map into a square wrist panel; ours does the same in the shader that already draws the map in the hand (xr.cpp kHudShader, flat and
// orb), from the map's own box hud.cpp copies out (the share). What is ours: the hero's marker is where [hud] map_direction measured it
// (mapdir::kMarkerUp: a little above the box's middle - not searched for by colour each frame as theirs is), the map drawn wider in F4
// (the share's stretch) is narrowed back first, and the way one looks is our camera's on the ground (mapdir::OnGround), as the arrow's.
//
// The map's projection (theirs, the same as mapdir's): D2 draws its map isometric - the ground turned 45 degrees and its up part halved,
// so a tile is a diamond twice as wide as tall. A point t seen from above (x right, y down, rooms square and upright) is on the map at
// (t.x - t.y, (t.x + t.y) / 2). The map's own up is then the up-left diagonal of the view from above: a bearing (clockwise from the map's
// up, as the compass reads it) is 45 degrees less there (kTopDownTurn).
//
// Visual only: the map and all on it are the game's, nothing it draws is changed or left out.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "map_direction.h"   // mapdir::kMarkerUp: where the hero's marker is in the map's box

namespace mapturn {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530718f;
constexpr float kTopDownTurn = 0.785398163f;   // 45 degrees: the map's up on the view from above is its up-left diagonal

// The bearing of a way on the ground (mapdir::OnGround: x along the game screen's right, y along its UP): radians clockwise from the
// map's up, -pi..pi. 0 when it has no length.
inline float Bearing(float gx, float gy) {
    if (!std::isfinite(gx) || !std::isfinite(gy) || gx * gx + gy * gy < 1e-8f) return 0.0f;
    return std::atan2(gx, gy);
}

// What the map in the hand does this frame. topDown: seen from straight above; turning: and turned so `heading` (radians, clockwise, on
// the view from above) is up. A turning map is always seen from above: the slanted map turned would leave its tiles leaning.
struct Turn { bool topDown = false, turning = false; float heading = 0.0f; bool Any() const { return topDown || turning; } };
inline Turn For(bool topDownKey, bool rotateKey, bool headingKnown, float bearing) {
    Turn t;
    t.turning = rotateKey && headingKnown && std::isfinite(bearing);
    t.topDown = topDownKey || rotateKey;
    t.heading = t.turning ? bearing - kTopDownTurn : 0.0f;
    return t;
}

// From a point of the square picture drawn (q: -1..1 each way, x right, y DOWN; |q| = 1 the circle that fits it - the orb's edge) to the
// share's uv: uv = hero + m * q, m row-major {m[0] m[1]; m[2] m[3]}. w x h: the share's pixels; stretch: how much wider than its own shape
// the game drew it (hud's g_mapStretchNow, 1 = its own shape); zoom: the orb's map zoom (1 = as fits). The circle |q| = 1 holds the
// ellipse inside the box about the hero (the box's height and width both reached; past the box nothing is drawn).
struct Map { float hero[2] = {0.5f, 0.5f}; float m[4] = {0.5f, 0.0f, 0.0f, 0.5f}; bool ok = false; };
// The same about any point of the share: the hero at uv (heroU, heroV), the map reaching halfW share pixels left and right of it and halfH
// up and down (the nearer edge each way). [hud] map_click_big's big map: the game's whole-screen map, centred on the hero, its rows above
// the toolbar's strip only (MapFor is the corner map's: its box, the hero's marker a little above its middle).
inline Map MapAround(const Turn& t, float w, float h, float stretch, float heroU, float heroV, float halfW, float halfH, float zoom = 1.0f) {
    Map r;
    if (!t.Any() || !(w >= 16.0f) || !(h >= 16.0f) || !std::isfinite(w) || !std::isfinite(h)) return r;
    if (!std::isfinite(heroU) || !std::isfinite(heroV) || !(halfW >= 8.0f) || !(halfH >= 8.0f) || !std::isfinite(halfW) || !std::isfinite(halfH)) return r;
    stretch = std::isfinite(stretch) ? std::clamp(stretch, 0.25f, 4.0f) : 1.0f;
    zoom = std::isfinite(zoom) ? std::clamp(zoom, 0.1f, 8.0f) : 1.0f;
    r.hero[0] = heroU;
    r.hero[1] = heroV;
    // the view from above's reach: a point t with |t| = reach is on the map at most reach * sqrt 2 across and reach / sqrt 2 up or down.
    // The larger of the two: the whole ellipse inside the box (about the hero) is in the circle - nothing the slanted map showed there is
    // lost, an ally up or down the map too (the smaller one kept a square box's map only a quarter of its height above and below the hero,
    // 2026-10-28); at some turns the circle reaches past the box's sides, where nothing is drawn.
    const float s2 = 1.41421356f;
    const float across = halfW / stretch / s2, upDown = halfH * s2;   // (across in the map's own pixels: the stretch narrowed back)
    const float reach = std::max(across, upDown) / zoom;
    // q -> the view from above (turned by the heading) -> the map's isometric -> the share's uv
    const float c = t.turning ? std::cos(t.heading) : 1.0f, s = t.turning ? std::sin(t.heading) : 0.0f;
    const float rot[4] = {c, -s, s, c};             // R(h): q's up comes from the heading's way
    const float iso[4] = {1.0f, -1.0f, 0.5f, 0.5f};  // (x - y, (x + y) / 2)
    float a[4];
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) a[2 * i + j] = iso[2 * i] * rot[j] + iso[2 * i + 1] * rot[2 + j];
    const float toUv[2] = {stretch / w, 1.0f / h};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) r.m[2 * i + j] = toUv[i] * a[2 * i + j] * reach;
    r.ok = std::isfinite(r.m[0]) && std::isfinite(r.m[1]) && std::isfinite(r.m[2]) && std::isfinite(r.m[3]);
    return r;
}
inline Map MapFor(const Turn& t, float w, float h, float stretch, float zoom = 1.0f) {
    return MapAround(t, w, h, stretch, 0.5f, 0.5f - mapdir::kMarkerUp, 0.5f * w, (0.5f - mapdir::kMarkerUp) * h, zoom);
}
inline void Apply(const Map& m, float qx, float qy, float* u, float* v) {
    *u = m.hero[0] + m.m[0] * qx + m.m[1] * qy;
    *v = m.hero[1] + m.m[2] * qx + m.m[3] * qy;
}

// The way the head looks, for the map in the hand: its turn in the room held while it looks (nearly) straight down - at the hand - where
// its forward's ground part says nothing; the body's turn (the stick) still counts. Theirs too (wrist_compass.h Heading).
struct HeadHold {
    float roomYaw = 0.0f;   // atan2(-f.x, -f.z) of the head's forward in the room, as vrcam's own turn of the head
    bool known = false;
    // fx, fz: the head's forward in the room. False until a level enough look was seen.
    bool Update(float fx, float fz) {
        if (std::isfinite(fx) && std::isfinite(fz) && fx * fx + fz * fz > 0.04f) { roomYaw = std::atan2(-fx, -fz); known = true; }
        return known;
    }
};

// The compass: a strip of two revolutions side by side painted once (N, NE, E ... in steps of 1/32 turn), and a window half a turn wide
// over it, moved along by the bearing so the middle of the window is the way one looks - no repainting, no upload, the XR quad's image
// rect does it. A fixed marker (a small gold wedge) over the window's middle, from the same image.
constexpr int kPeriod = 1024, kWindow = 512, kStripH = 96, kMarkW = 32, kMarkH = 16;
constexpr int kAtlasW = kPeriod + kWindow, kAtlasH = kStripH + kMarkH;
// The window's left edge in the strip for a bearing (radians, clockwise from the map's up).
inline int Crop(float bearing) {
    if (!std::isfinite(bearing)) return 0;
    float w = std::fmod(bearing, kTau);
    if (w < 0.0f) w += kTau;
    return (int)std::lround(w * (float)kPeriod / kTau) % kPeriod;
}
// Which label (0 N, 1 NE ... 7 NW) and tick a strip column x has: the strip's column for bearing b is Crop(b) + kWindow / 2.
inline int StepAt(int x) { return (x - kWindow / 2 + kPeriod) / (kPeriod / 32); }

}  // namespace mapturn
