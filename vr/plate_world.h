// plate_world.h - [hud] monster_bar_room: the game's own health bar and name over the monster you target, cut out of the
// interface picture and hung in the room above that monster's head (VR first person, native OpenXR). Pure maths and the small
// state the four parts of it share; no game calls. Asked in the hook (vr/mob_bars.cpp), the tick (vr/vrcam.cpp), the cut
// (vr/hud.cpp) and the layer (vr/xr.cpp).
//
// Why. In the stereo picture the interface is drawn once and laid over both eyes: a plate the game hangs over a monster by that
// monster's place on ONE eye's screen floats at the screen's own depth in the other (Discord, 2026-10-16: "where can I set the
// monsters' health bar to be right above them, not somewhere in the sky?"). A plate in the room is seen from both eyes where
// the monster is.
//
// How, in the order a frame goes:
//   1. vr/mob_bars.cpp, in the game's own anchor call for the plate (UnitToScreen from the plate's anchor helper): the game's
//      answer is the monster's head on this eye's screen. The hook RECORDS it (Capture: the monster's place on the grid, the
//      screen point, the ray through that point in the world) and gives the game a FIXED point instead (FixedPoint): the plate is
//      drawn there, in the interface layer, in a place that does not move with the monster or the head - so it can be cut out
//      without a lag, and the game's plate is no longer over the monster. Nothing else about the monster changes.
//   2. vr/vrcam.cpp, every frame: the head of the recorded monster - the ray meets the monster's upright axis (HeadOnAxis; the
//      renderer's ground shear makes "upright" slanted in the render world, melee_bodies.h) - in the head's turn-only frame
//      (metres from the hero's eyes: x right, y up, z back; RenderToHead), the frame the hands and the belt are hung in, plus the gap above the head.
//   3. vr/hud.cpp, when the game leaves the interface layer: the plate's box (from the plate's widgets, vr/gamestate.cpp) is
//      copied out and emptied in the layer.
//   4. vr/xr.cpp: that copy as a quad layer at the anchor, turned to the head, upright, its width by the distance (WidthM).
// Nothing is drawn when the game shows no plate (no monster hovered or targeted), when the record is older than kStaleMs or the
// copy is, and in every view but F4.
//
// Where the bar went (2026-10-17, a test of test-54: "the name and the bar stay at the top of the screen"). The cut must never be made from a
// rect the game did not hang at the fixed point: the plate's widgets read 0,-6 1000x58 - the game's default before a target, or its mouse
// layout, which puts the bar at the top and ignores the anchor - and that was cut (640x64 from 732,134), a strip of the picture that is not
// the plate. So: (1) the plate is cut only when its anchor widget stands where the fixed point puts it (AnchorAtFixed); (2) the game is
// asked for its controller layout when it is in the mouse one (ShouldAskPad); (3) every target gets ONE line in the log saying where its bar
// went or why not (Why, Resolve, Tracker).
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "blade_shape.h"
#include "melee_bodies.h"

namespace plateworld {

constexpr uint64_t kStaleMs = 150;      // a record, an anchor or a copy older than this is nothing to draw
constexpr float kMaxDistM = 60.0f;      // farther than this the plate is not drawn
constexpr float kMinHeadM = 0.25f, kMaxHeadM = 8.0f;   // a head's height over the floor the maths accepts (metres)
constexpr float kFallbackHeadM = 1.8f;                  // ... and what is used when the ray cannot say

// ---- the fixed point the game draws the plate at ----------------------------------------------------------------------------
// In pixels of the game's viewport: the middle of the width, a third of the way down. The plate's widgets stand there whatever the
// monster does, so the box that is cut never lags the plate. (Top middle is where the game's mouse layout puts the same plate.)
constexpr float kFixedX = 0.5f, kFixedY = 0.30f;
inline bool FixedPoint(float viewportW, float viewportH, float out[2]) {
    if (!(viewportW >= 100.0f && viewportW <= 20000.0f && viewportH >= 100.0f && viewportH <= 20000.0f)) return false;
    out[0] = kFixedX * viewportW;
    out[1] = kFixedY * viewportH;
    return true;
}

// ---- where the plate stands in the interface layer -------------------------------------------------------------------------
// The plate's box in pixels (l, t, r, b) of a w x h interface layer, from the plate's rect in the layout (vr/gamestate.cpp PlateRect: x, y from the
// top middle, width, height, in the layout's units) and the interface scale s (the screen's height over 2160 times the Safe Screen Percent; a
// value that is not a scale counts as h / 2160). The box is the rect with margins (kMarginX, kMarginY) - the same box vrcam's PlateBoxUv gives the
// effect, in pixels.
// The margins, shares of the plate's height: across for the background's caps, above and below for the frame's light top and bottom lines - both
// drawn outside the widgets' rectangles. Above and below were 15 % until 2026-10-24, and the top line stayed in the picture over a plate made smaller
// (Serg: "old frames stay round the monsters' plates, above and sometimes at the side"): now half the height all round.
constexpr float kMarginX = 0.5f, kMarginY = 0.5f;
inline bool BoxOf(const int32_t r[4], float s, uint32_t w, uint32_t h, float box[4]) {
    if (!w || !h || r[2] <= 0 || r[3] <= 0) return false;
    if (!(s > 0.05f && s < 20.0f)) s = static_cast<float>(h) / 2160.0f;
    const float top = std::max(0.0f, 0.5f * (static_cast<float>(h) - 2160.0f * s));
    const float mx = kMarginX * static_cast<float>(r[3]) * s, my = kMarginY * static_cast<float>(r[3]) * s;
    const float x0 = 0.5f * static_cast<float>(w) + static_cast<float>(r[0]) * s - mx, y0 = top + static_cast<float>(r[1]) * s - my;
    box[0] = x0;
    box[1] = y0;
    box[2] = x0 + static_cast<float>(r[2]) * s + 2.0f * mx;
    box[3] = y0 + static_cast<float>(r[3]) * s + 2.0f * my;
    return std::isfinite(box[0] + box[1] + box[2] + box[3]);
}

// ---- the box follows the plate ----------------------------------------------------------------------------------------------
// The plate's widgets are read on the UI thread every 10 ms (vr/gamestate.cpp ReadPlate) and the box is set as the effect draws. In a headset the
// plate moves on the screen with every turn of the head - 20 px a frame is nothing - and a box placed by the last read trailed it: the plate's edge
// outside the box (its light top line most of all) stayed where the game drew it, the rest was drawn smaller at its own depth (Serg, 2026-10-24).
// The hook sees where the game hangs the plate in each frame (its anchor, px of the viewport, vr/mob_bars.cpp): the box is the read one moved by
// how far the anchor has gone since that read, for each of the hook's last kFollowN answers - their union, so the plate is in it whichever of
// those frames the picture on the screen was drawn from (the game's thread runs ahead of the one that draws).
constexpr int kFollowN = 3;
struct Follow {
    uintptr_t unit = 0;
    int n = 0;                     // answers kept, newest first
    float px[kFollowN][2] = {};
    float view[2] = {};            // the viewport they are points of
    uint64_t atMs = 0;
};
// One answer of the hook for a unit's plate; another unit starts again.
inline void FollowAdd(Follow* f, uintptr_t unit, const float px[2], float viewW, float viewH, uint64_t nowMs) {
    if (unit != f->unit) {
        f->unit = unit;
        f->n = 0;
    }
    for (int i = std::min(f->n, kFollowN - 1); i > 0; --i) {
        f->px[i][0] = f->px[i - 1][0];
        f->px[i][1] = f->px[i - 1][1];
    }
    f->px[0][0] = px[0];
    f->px[0][1] = px[1];
    f->n = std::min(f->n + 1, kFollowN);
    f->view[0] = viewW;
    f->view[1] = viewH;
    f->atMs = nowMs;
}
// The box (uv: x0 y0 x1 y1) for the picture now. read: the box of the rect as read (uv); readUnit, atRead: the unit and its anchor (px) the hook
// had given at that read (atReadOk false: none). widest: the widest box read for any plate (uv) - a new target's name may be longer than the one
// read, and its own rect comes a read later: its box is that wide meanwhile, about the same middle (the plate hangs centred on its anchor).
// No answer from the hook (not in, another view, not a monster's plate, nothing lately): the box as read.
inline void FollowBox(const float read[4], uintptr_t readUnit, const float atRead[2], bool atReadOk, const Follow& f, float widest, float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = read[i];
    if (!atReadOk || f.n <= 0 || !(f.view[0] >= 1.0f && f.view[1] >= 1.0f) || !(read[2] > read[0] && read[3] > read[1])) return;
    float b[4] = {read[0], read[1], read[2], read[3]};
    if (f.unit != readUnit && widest > b[2] - b[0]) {
        const float cx = 0.5f * (b[0] + b[2]);
        b[0] = cx - 0.5f * widest;
        b[2] = cx + 0.5f * widest;
    }
    float u[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < std::min(f.n, kFollowN); ++i) {
        const float dx = (f.px[i][0] - atRead[0]) / f.view[0], dy = (f.px[i][1] - atRead[1]) / f.view[1];
        if (!std::isfinite(dx) || !std::isfinite(dy)) return;
        const float m[4] = {b[0] + dx, b[1] + dy, b[2] + dx, b[3] + dy};
        for (int k = 0; k < 4; ++k) u[k] = i == 0 ? m[k] : (k < 2 ? std::min(u[k], m[k]) : std::max(u[k], m[k]));
    }
    for (int i = 0; i < 4; ++i) out[i] = u[i];
}

// ---- reading a unit ---------------------------------------------------------------------------------------------------------
// A unit's place on the game's grid in game world units (twice the cell): the 16.16 coordinates of its path (unit + 0x38) - four
// u16s: x fraction, x, z fraction, z (vr/melee_touch.cpp GridOf reads the same). read(address, value) as vr/mob_bars.h.
template <class Read>
bool FootOf(uintptr_t unit, Read read, float* x, float* z) {
    uintptr_t path = 0;
    uint16_t p[4];
    if (unit < 0x10000 || !read(unit + 0x38, path) || path < 0x10000) return false;
    for (int i = 0; i < 4; ++i) if (!read(path + 2u * static_cast<uintptr_t>(i), p[i])) return false;
    *x = 2.0f * (static_cast<float>(p[1]) + static_cast<float>(p[0]) / 65536.0f);
    *z = 2.0f * (static_cast<float>(p[3]) + static_cast<float>(p[2]) / 65536.0f);
    return true;
}

// ---- where the head is ------------------------------------------------------------------------------------------------------
// The monster's upright axis in the render world over units-per-metre: through its feet on the floor, going up by (k, 1, k) a metre
// (the ground shear, meleetouch::RenderGround). footX / footZ: the grid place over upm (metres), floorM the floor's height.
// The ray (origin, dir) in the same metres. The axis' point nearest the ray is the head; `height` is how many metres above the floor.
// False when the ray runs along the axis (no answer), or points away from it.
inline bool HeadOnAxis(float footX, float footZ, float floorM, const float rayO[3], const float rayD[3], float* height, float head[3]) {
    const float k = meleetouch::kGroundShear;
    const float a0[3] = {footX + floorM * k, floorM, footZ + floorM * k};
    const float u[3] = {k, 1.0f, k};
    float d[3] = {rayD[0], rayD[1], rayD[2]};
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dl > 1e-6f) || !std::isfinite(dl)) return false;
    for (float& v : d) v /= dl;
    const float w[3] = {a0[0] - rayO[0], a0[1] - rayO[1], a0[2] - rayO[2]};
    const float a = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    const float b = u[0] * d[0] + u[1] * d[1] + u[2] * d[2];
    const float d1 = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    const float e = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    const float denom = a - b * b;
    if (!(denom > 1e-3f * a)) return false;   // the ray runs (nearly) along the axis
    const float t = (b * e - d1) / denom;      // metres above the floor
    const float s = e + t * b;                 // metres along the ray
    if (!std::isfinite(t) || !std::isfinite(s) || s <= 0.0f) return false;
    *height = t;
    for (int i = 0; i < 3; ++i) head[i] = a0[i] + t * u[i];
    return true;
}

// A point of the render world over upm (metres) in the head's turn-only frame, metres from the hero's eyes (x right, y up, z back): the inverse of
// bladeshape::PointToRender. The frame's three axes are the view's right, up and back; false when they do not span a space.
inline bool RenderToHead(const bladeshape::HeadFrame& h, const float p[3], float out[3]) {
    const float d[3] = {p[0] - h.eyeWorld[0] / h.upm, p[1] - h.eyeWorld[1] / h.upm, p[2] - h.eyeWorld[2] / h.upm};
    // PointToRender: render = eye + v0 * right + v1 * (0, 1, 0) + v2 * (-fwd). Solve the 3 x 3 by Cramer's rule.
    const float c0[3] = {h.right[0], h.right[1], h.right[2]};
    const float c1[3] = {0.0f, 1.0f, 0.0f};
    const float c2[3] = {-h.fwd[0], -h.fwd[1], -h.fwd[2]};
    auto det = [](const float a[3], const float b[3], const float c[3]) {
        return a[0] * (b[1] * c[2] - b[2] * c[1]) - b[0] * (a[1] * c[2] - a[2] * c[1]) + c[0] * (a[1] * b[2] - a[2] * b[1]);
    };
    const float D = det(c0, c1, c2);
    if (!(std::fabs(D) > 1e-4f) || !h.upm || !std::isfinite(D)) return false;
    out[0] = det(d, c1, c2) / D;
    out[1] = det(c0, d, c2) / D;
    out[2] = det(c0, c1, d) / D;
    return std::isfinite(out[0] + out[1] + out[2]);
}

// ---- how big ----------------------------------------------------------------------------------------------------------------
// The quad's width in metres at `distM` from the eyes: the same angle at any distance (a fifth of the distance), kept between a
// readable size close up and a size that is not a wall far off; times [hud] monster_size (percent, the slider that scaled the
// game's own plate) - the size is of the cut-out as it is, margins included.
inline float WidthM(float distM, float sizePct) {
    const float scale = std::clamp(std::isfinite(sizePct) ? sizePct : 100.0f, 10.0f, 200.0f) * 0.01f;   // (the size setting 30 .. 200 times monster_far down to 20 %: not under a tenth)
    return std::clamp(0.20f * (std::isfinite(distM) ? distM : 3.0f), 0.40f, 1.60f) * scale;
}

// ---- the bar's swapchain: one, made once ---------------------------------------------------------------------------------------
// The cut-out changes size with every name it holds (hud.cpp steps it by 128 x 32 pixels: 128 wide for a short name, 256 for the next, 384...). A swapchain
// of the cut-out's own size was made again for each - the old one destroyed with the compositor and the GPU possibly still on it - and a test of test-56
// (2026-10-17) lost the game at the seventh (128x64, 256x64, 128x64, 256x64...; the render-target heap, vr/rtv_slots.h, had wrapped too). Now the chain is ONE size,
// made at the first bar and kept until the session ends; each bar is drawn into the corner of it that it fills (FitInto) and the layer's image rect says which.
constexpr uint32_t kChainW = 2048, kChainH = 256;   // 2 MB an image; the widest bar at the biggest interface scale is about 1300 x 128
struct Fit {
    uint32_t w = 0, h = 0;   // the corner of the chain this bar is drawn into: the bar's own size, or smaller (same shape) when it is larger than the chain
};
inline bool FitInto(uint32_t srcW, uint32_t srcH, Fit* out) {
    if (!srcW || !srcH || srcW > 1000000u || srcH > 1000000u) return false;
    double f = 1.0;
    if (srcW > kChainW) f = std::min(f, static_cast<double>(kChainW) / srcW);
    if (srcH > kChainH) f = std::min(f, static_cast<double>(kChainH) / srcH);
    out->w = std::clamp<uint32_t>(static_cast<uint32_t>(srcW * f + 0.5), 1u, kChainW);
    out->h = std::clamp<uint32_t>(static_cast<uint32_t>(srcH * f + 0.5), 1u, kChainH);
    return true;
}

// ---- is it a plate at all ---------------------------------------------------------------------------------------------------
// The Health widget is 1000 units wide until the game has put a name in it (the log of a test of test-54: 0,-6 1000x58 before any target, and
// again while the game hung an NPC's name); a plate is as wide as its name - 154 and 203 in that log. A rect outside these is not read yet.
constexpr int32_t kMinPlateW = 20, kMaxPlateW = 900, kMinPlateH = 10, kMaxPlateH = 200;
inline bool SanePlate(const int32_t r[4]) { return r[2] >= kMinPlateW && r[2] <= kMaxPlateW && r[3] >= kMinPlateH && r[3] <= kMaxPlateH; }

// ---- is the plate where the fixed point put it ------------------------------------------------------------------------------
// The game hangs the plate on the point UnitToScreen gave: its widget TargetAttached stands at that point, in the layout's units off the
// interface area's top middle - x = (px - W/2) / s, y = (py - top) / s, top = the area's top, half of what the Safe Screen Percent leaves free
// (vr/vrcam.cpp PlateBoxUv: the same area). Checked against a log (2026-10-17, W = H = 1536, s = 0.569): the answer -30000,-30000 (mob_bars.h
// kFarAway) put the widget at -54075 and -52995 (TargetAttached -54075,-53075 off the panel at 0,80) - this gives -54074 and -52990. The
// widgets of a plate the game has NOT hung there read the panel's own place: 0,80 (the game's default before a target, the mouse layout).
constexpr float kAnchorTolUnits = 30.0f;   // the helper adds a few units of its own (5 in that log); the default is some 460 away at 80 %
inline bool ExpectedAnchor(const float px[2], const float viewport[2], float s, float out[2]) {
    if (!(s > 0.05f && s < 20.0f) || !(viewport[0] >= 100.0f && viewport[1] >= 100.0f)) return false;
    const float top = std::max(0.0f, 0.5f * (viewport[1] - 2160.0f * s));
    out[0] = (px[0] - 0.5f * viewport[0]) / s;
    out[1] = (px[1] - top) / s;
    return std::isfinite(out[0] + out[1]);
}
// anchor: the widget's place off the area's top middle, plus the panel's own (gamestate PlateAnchor). True when it stands at the fixed point.
inline bool AnchorAtFixed(const int32_t anchor[2], const float fixedPx[2], const float viewport[2], float s) {
    float want[2];
    if (!ExpectedAnchor(fixedPx, viewport, s, want)) return false;
    return std::fabs(static_cast<float>(anchor[0]) - want[0]) <= kAnchorTolUnits && std::fabs(static_cast<float>(anchor[1]) - want[1]) <= kAnchorTolUnits;
}

// ---- where a target's bar went, and the line that says so ---------------------------------------------------------------------
// One of these per target, in the log, once (Tracker): the hook says how far it got (Hung = it took the plate and recorded the monster), the tick
// says whether the plate then stood where it was put and was cut (Resolve).
enum class Why : uint8_t {
    Hung,          // cut out of the picture and hung over the monster's head
    Hidden,        // an ordinary monster's plate, taken off the screen ([hud] ordinary_mob_bars comes first)
    NoViewport,    // the game's viewport is not known yet: no fixed point
    NoFoot,        // the monster's place on the grid could not be read
    NoRay,         // the ray through the head's screen point could not be made
    NotShown,      // the game says the monster is not on the screen: it has no plate to move
    NoRect,        // the plate's widgets (HUDMonsterHealth) were not read lately
    MouseLayout,   // the game is in its mouse layout: the plate is at the top and ignores the anchor
    NotAnchored,   // the plate's anchor widget is not at the fixed point (the game has not hung it there - yet)
    NoAnchor,      // the head's place in the room could not be worked out (no eyes, or the monster is under 0.2 m or beyond 60 m)
    kCount
};
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::Hung: return "the bar is cut out of the picture and hung over its head";
        case Why::Hidden: return "no bar: an ordinary monster, and [hud] ordinary_mob_bars hides those (untick it for a bar over every monster)";
        case Why::NoViewport: return "the game's bar stays where the game draws it: the game's viewport size is not known yet";
        case Why::NoFoot: return "the game's bar stays where the game draws it: the monster's place on the grid could not be read";
        case Why::NoRay: return "the game's bar stays where the game draws it: the ray through the monster's head could not be made";
        case Why::NotShown: return "no bar to move: the game says the monster is not on the screen";
        case Why::NoRect: return "the game's bar stays in the picture: the plate's widgets were not read lately, or are still the game's unsized default (1000 units wide)";
        case Why::MouseLayout: return "the game's bar stays at the top: the game is in its mouse layout, which ignores the anchor (the controller layout is asked for)";
        case Why::NotAnchored: return "the game's bar stays in the picture: the plate's anchor is not at the point it was given";
        case Why::NoAnchor: return "the game's bar stays in the picture: the monster's head could not be placed in the room (no eyes, or under 0.2 m or beyond 60 m)";
        default: return "?";
    }
}
// The outcome, from what the hook did (hook: Hung = it recorded the monster, else the reason it did not) and what the tick found. pad: the game's
// layout, 1 the controller's, 0 the mouse's, -1 not known.
inline Why Resolve(Why hook, bool rectRead, bool anchored, int pad, bool headPlaced) {
    if (hook != Why::Hung) return hook;
    if (!rectRead) return Why::NoRect;
    if (!anchored) return pad == 0 ? Why::MouseLayout : Why::NotAnchored;
    if (!headPlaced) return Why::NoAnchor;
    return Why::Hung;
}

// Says "now" once for each target and each change of what became of its bar: a (unit, what) pair is told when it has stood for kSettleMs
// (the first frame of a target is the game's widgets catching up; that is not an outcome), is not what was told last, and fewer than kMaxTold
// lines have gone (a game of hours does not fill the log). `what` is a Why, or any other number the caller keeps apart (a state of the bar).
class Tracker {
public:
    static constexpr uint64_t kSettleMs = 300;
    static constexpr int kMaxTold = 80;
    bool Should(uintptr_t unit, int what, uint64_t nowMs) {
        if (!pending_ || unit != pendUnit_ || what != pendWhat_) {
            pending_ = true;
            pendUnit_ = unit;
            pendWhat_ = what;
            pendSince_ = nowMs;
        }
        if (told_ >= kMaxTold) return false;
        if (nowMs - pendSince_ < kSettleMs) return false;
        if (toldAny_ && unit == toldUnit_ && what == toldWhat_) return false;
        toldAny_ = true;
        toldUnit_ = unit;
        toldWhat_ = what;
        ++told_;
        return true;
    }
    // Nothing is targeted now: the next target (this monster again, too) is told anew.
    void Gone() {
        toldAny_ = false;
        pending_ = false;
    }
    int Told() const { return told_; }
private:
    bool pending_ = false, toldAny_ = false;
    uintptr_t pendUnit_ = 0, toldUnit_ = 0;
    int pendWhat_ = 0, toldWhat_ = 0;
    uint64_t pendSince_ = 0;
    int told_ = 0;
};

// ---- asking for the controller layout -------------------------------------------------------------------------------------------
// The plate hangs on the anchor only in the game's controller layout; in the mouse layout it is at the top. The world bar is on, a game with no
// panel open, the laser not the mouse, and the game has been in the mouse layout for kMouseMs: ask for the pad's once - and again no sooner than
// kAskEveryMs (the pad's own report takes it back by itself; this is for the quiet minutes). `since` and `lastAsk` are the caller's (ms; 0 = never).
constexpr uint64_t kMouseMs = 500, kAskEveryMs = 3000;
inline bool ShouldAskPad(bool worldOn, bool inGame, bool menu, bool laserOwns, int pad, uint64_t nowMs, uint64_t* mouseSince, uint64_t* lastAsk) {
    if (!worldOn || !inGame || menu || laserOwns || pad != 0) {
        *mouseSince = 0;
        return false;
    }
    if (!*mouseSince) *mouseSince = nowMs;
    if (nowMs - *mouseSince < kMouseMs) return false;
    if (*lastAsk && nowMs - *lastAsk < kAskEveryMs) return false;
    *lastAsk = nowMs;
    return true;
}

// ---- the state the parts share ---------------------------------------------------------------------------------------------
// What the hook recorded: the monster the game anchored the plate to, in this pass.
struct Capture {
    bool valid = false;
    float foot[2] = {};      // the monster's place on the grid, game units
    float rayO[3] = {};      // the ray through the head's screen point, in the world (game units)
    float rayD[3] = {};
    float px[2] = {};        // the head's screen point (viewport pixels), for the log
    float fixed[2] = {};     // the fixed point the game was given instead (viewport pixels) ...
    float view[2] = {};      // ... and the viewport it is a point of
    uint32_t cls = 0;
    uintptr_t unit = 0;
    uint64_t atMs = 0;
};
// What the tick made of it: the place for xr.cpp.
struct Anchor {
    bool ok = false;
    float head[3] = {};      // the plate's anchor in the head's turn-only frame, metres from the eyes (x right, y up, z back)
    float distM = 0.0f;      // from the eyes
    float gapM = 0.0f;       // above the head, in the room's up
    float sizePct = 100.0f;
    uint64_t atMs = 0;
};

// What the hook did with the latest plate it was asked about (monster or not hung, and why), for the tick's line in the log.
struct Seen {
    bool valid = false;
    uintptr_t unit = 0;
    uint32_t cls = 0;
    Why why = Why::Hung;
    float px[2] = {};
    bool footOk = false;
    float foot[2] = {};      // the monster's place on the grid, game units (its distance: monster_far, monster_depth_body)
    uint64_t atMs = 0;
};

struct State {
    std::mutex lock;
    Capture capture;
    Anchor anchor;
    Seen seen;
    Follow follow;                     // where the game hung the plate in its last frames (the hook), for the effect's box
    std::atomic<bool> followOn{false};   // [hud] monster_alpha, monster_size, monster_far...: a plate setting the effect needs the box for (vrcam)
    std::atomic<bool> active{false};   // vrcam's tick: the world bar is in force now (ticked, F4, native OpenXR, a game, no menu)
    std::atomic<float> gapCm{15.0f};
    std::atomic<float> sizePct{100.0f};
    std::atomic<float> alpha{1.0f};    // [hud] monster_alpha: the bar's opacity, 1 = as the game draws it
    std::atomic<bool> track{false};    // [hud] monster_far / monster_depth_body: the hook notes the targeted monster even without the bar in the room
    // the targeted monster's distance as the tick worked it out (ground, from the hero's feet): metres and game units, -1 = none lately
    std::atomic<float> targetM{-1.0f}, targetUnits{0.0f};
    std::atomic<uint64_t> targetAt{0};
    std::atomic<bool> wanted{false};   // [hud] monster_bar_room (the plate cut out and hung in the room; off unless asked)
    // [hud] monster_over_head: the game's own plate lifted by the game's own anchor call so it hangs over the monster's head (LiftAnchor)
    std::atomic<bool> lift{false};
    std::atomic<float> floorM{0.0f}, upm{0.0f};   // vrcam's tick: the hero's floor (metres, render world) and game units a metre; upm 0 = not known
    std::atomic<float> gameGapPx{19.0f};          // how far under the plate's lower edge the game's anchor stands (px), measured from its widgets; 19 until read
};
inline State& S() {
    static State s;
    return s;
}

// The ray through a screen point, in the world (the game's units): d2rcam::PickRay by default; a test or another frame sets its own.
using RayFn = bool (*)(float sx, float sy, float origin[3], float dir[3]);
inline std::atomic<RayFn>& RaySlot() {
    static std::atomic<RayFn> slot{nullptr};
    return slot;
}

inline void SetCapture(const Capture& c) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    s.capture = c;
}
inline bool GetCapture(Capture* out, uint64_t nowMs) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    if (!s.capture.valid || nowMs - s.capture.atMs > kStaleMs) return false;
    *out = s.capture;
    return true;
}
inline void SetAnchor(const Anchor& a) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    s.anchor = a;
}
inline bool GetAnchor(Anchor* out, uint64_t nowMs) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    if (!s.anchor.ok || nowMs - s.anchor.atMs > kStaleMs) return false;
    *out = s.anchor;
    return true;
}
inline void NoteSeen(uintptr_t unit, uint32_t cls, Why why, const float px[2], uint64_t nowMs, const float* foot = nullptr) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    s.seen.valid = true;
    s.seen.unit = unit;
    s.seen.cls = cls;
    s.seen.why = why;
    s.seen.px[0] = px ? px[0] : 0.0f;
    s.seen.px[1] = px ? px[1] : 0.0f;
    s.seen.footOk = foot != nullptr;
    s.seen.foot[0] = foot ? foot[0] : 0.0f;
    s.seen.foot[1] = foot ? foot[1] : 0.0f;
    s.seen.atMs = nowMs;
}
inline bool GetSeen(Seen* out, uint64_t nowMs) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    if (!s.seen.valid || nowMs - s.seen.atMs > kStaleMs) return false;
    *out = s.seen;
    return true;
}
inline void Clear() {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    s.capture = Capture{};
    s.anchor = Anchor{};
    s.seen = Seen{};
}
// [hud] monster_bar_room, monster_bar_gap_cm, monster_size: the player's choices. The tick sets Active from them and the view.
inline void Configure(bool wanted, float gapCm, float sizePct, float alpha = 1.0f) {
    State& s = S();
    s.wanted.store(wanted);
    s.alpha.store(std::clamp(std::isfinite(alpha) ? alpha : 1.0f, 0.0f, 1.0f));
    s.gapCm.store(std::clamp(std::isfinite(gapCm) ? gapCm : 15.0f, -50.0f, 150.0f));
    s.sizePct.store(std::clamp(std::isfinite(sizePct) ? sizePct : 100.0f, 30.0f, 200.0f));
}
inline bool Wanted() { return S().wanted.load(); }
// [hud] monster_far, monster_depth_body (vrcam, with the ini): the hook notes which monster is targeted (its place) even with no bar in the room, for the plate left in the picture.
inline void SetTrack(bool on) { S().track.store(on); }
inline bool Tracking() { return S().track.load(); }
// The ground distance from the hero's feet to a monster on the grid, in game units (both on the game's grid, twice the cell), and in metres at upm units a metre.
inline bool GroundDistance(const float foot[2], const float hero[3], float upm, float* units, float* metres) {
    if (!(upm >= 1.0f && upm <= 12.0f)) return false;
    const float dx = foot[0] - hero[0], dz = foot[1] - hero[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (!std::isfinite(d)) return false;
    *units = d;
    *metres = d / upm;
    return true;
}
// What the tick made of it, for the effect's uniforms (another call, the same thread or not): a ground distance, fresh for kStaleMs.
inline void SetTarget(float metres, float units, uint64_t nowMs) {
    State& s = S();
    s.targetM.store(metres);
    s.targetUnits.store(units);
    s.targetAt.store(nowMs);
}
inline bool GetTarget(float* metres, float* units, uint64_t nowMs) {
    State& s = S();
    const float m = s.targetM.load();
    if (!(m >= 0.0f) || nowMs - s.targetAt.load() > kStaleMs) return false;
    *metres = m;
    *units = s.targetUnits.load();
    return true;
}
// The box follows the plate (FollowBox): the hook notes where it hung the plate while a plate setting needs the box; a note is fresh for kStaleMs.
inline void SetFollow(bool on) { S().followOn.store(on); }
inline bool Following() { return S().followOn.load(); }
inline void NoteFollow(uintptr_t unit, const float px[2], float viewW, float viewH, uint64_t nowMs) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    FollowAdd(&s.follow, unit, px, viewW, viewH, nowMs);
}
inline bool GetFollow(Follow* out, uint64_t nowMs) {
    State& s = S();
    std::lock_guard<std::mutex> g(s.lock);
    if (s.follow.n <= 0 || nowMs - s.follow.atMs > kStaleMs) return false;
    *out = s.follow;
    return true;
}
// [hud] monster_alpha reaches the bar in the room too (a test of test-56: "the opacity slider does not work" - it faded only the plate left in the picture,
// by the effect, and the bar hung in the room was drawn whole). The quad's pixels are premultiplied, so the opacity multiplies all four channels (the HUD shader's
// g_opacity, xr.cpp). Under kMinOpacity nothing is drawn at all; the shader reads 0 as "whole", so a faint value is never passed as 0.
constexpr float kMinOpacity = 0.02f;
inline float Opacity() { return S().alpha.load(); }
inline bool OpacityVisible(float alpha) { return alpha >= kMinOpacity; }
inline float ShaderOpacity(float alpha) { return std::clamp(std::isfinite(alpha) ? alpha : 1.0f, kMinOpacity, 1.0f); }
inline bool Active() { return S().active.load(); }
inline void SetActive(bool on) {
    if (S().active.exchange(on) && !on) Clear();   // what was recorded is of the view that is over
}

// The anchor of a record: the head over the monster, in the head's frame. floorM: the hero's floor height (metres), upm game units per
// metre. Falls back to a head kFallbackHeadM over the floor when the ray cannot say. `view` gives the frame the hands hang in.
inline bool MakeAnchor(const Capture& c, const bladeshape::HeadFrame& view, float floorM, float gapCm, float sizePct, uint64_t nowMs, Anchor* out) {
    if (!c.valid || !view.ok || !(view.upm >= 1.0f && view.upm <= 12.0f) || !std::isfinite(floorM)) return false;
    const float upm = view.upm;
    const float fx = c.foot[0] / upm, fz = c.foot[1] / upm;
    const float ro[3] = {c.rayO[0] / upm, c.rayO[1] / upm, c.rayO[2] / upm};
    float height = kFallbackHeadM, head[3];
    if (!HeadOnAxis(fx, fz, floorM, ro, c.rayD, &height, head) || height < kMinHeadM || height > kMaxHeadM) {
        height = kFallbackHeadM;
        const float k = meleetouch::kGroundShear;
        head[0] = fx + (floorM + height) * k;
        head[1] = floorM + height;
        head[2] = fz + (floorM + height) * k;
    }
    float v[3];
    if (!RenderToHead(view, head, v)) return false;
    const float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(dist >= 0.2f && dist <= kMaxDistM)) return false;
    out->ok = true;
    for (int i = 0; i < 3; ++i) out->head[i] = v[i];
    out->distM = dist;
    out->gapM = gapCm * 0.01f;
    out->sizePct = sizePct;
    out->atMs = nowMs;
    return true;
}

// ---- [hud] monster_over_head: the game's own plate, lifted so it hangs over the monster's head ---------------------------------------------------------
// (Serg, test-57, 2026-10-17: "when the tick is on, the plate that is already there should just be moved - not drawn again; it sits a little above the monster
// anyway; now with the tick it is drawn beside the monster, not over it".) The game hangs the plate on the monster's head by UnitToScreen's answer plus a few
// pixels, the same few at any distance: its lower edge stands gameGapPx (about 19 at the interface scale 0.57) above the head's point - 7 cm over a head 1.5 m
// away, 45 cm over one 10 m away. The hook (vr/mob_bars.cpp) gives the game another y for that anchor, so that the plate's lower edge stands the player's gap
// ([hud] monster_bar_gap_cm, cm) over the head, whatever the distance. The x stays the game's: centred over the monster, as in the picture.
// How far is gap cm on the screen: from the ray through the head's point and the ray a few pixels above it (the game's own camera, PickRay), as an angle
// per pixel, and the angle at which the point gap cm straight over the head is seen. The head itself is where the ray comes nearest the monster's upright axis (HeadOnAxis;
// the render world's ground shear slants that axis); the gap is straight up from it, since the plate hangs straight up on the screen.
enum class LiftWhy : uint8_t { Lifted, NoViewport, NoWorld, NoFoot, NoRay, NoHead, kCount };
inline const char* LiftWhyText(LiftWhy w) {
    switch (w) {
        case LiftWhy::Lifted: return "lifted so that it hangs over the head";
        case LiftWhy::NoViewport: return "left as the game puts it: the game's viewport size is not known yet";
        case LiftWhy::NoWorld: return "left as the game puts it: the hero's floor is not known yet";
        case LiftWhy::NoFoot: return "left as the game puts it: the monster's place on the grid could not be read";
        case LiftWhy::NoRay: return "left as the game puts it: the ray through the head could not be made";
        case LiftWhy::NoHead: return "left as the game puts it: the head's place could not be worked out";
        default: return "?";
    }
}
constexpr float kProbePx = 16.0f;   // the second ray, this many pixels away
struct LiftIn {
    float px[2] = {};          // the game's answer: the head's screen point (viewport pixels)
    float viewportH = 0.0f;
    float foot[2] = {};        // the monster's place on the grid (game units)
    bool footOk = false;
    float floorM = 0.0f, upm = 0.0f;   // the hero's floor (metres), game units a metre; upm 0 = not known
    float gapCm = 15.0f;       // [hud] monster_bar_gap_cm: the plate's lower edge this far over the head
    float gameGapPx = 19.0f;   // the game's own gap between its anchor and the plate's lower edge
    RayFn ray = nullptr;
};
struct LiftOut {
    float anchorY = 0.0f;      // the anchor's y to give the game (viewport pixels)
    float movedPx = 0.0f;      // anchorY - the game's own (negative = higher)
    float distM = 0.0f;        // the head's distance from the camera
    float head[3] = {};        // the head's place the lift worked from (metres, render world)
};
inline LiftWhy LiftAnchor(const LiftIn& in, LiftOut* out) {
    if (!(in.viewportH >= 100.0f && in.viewportH <= 20000.0f)) return LiftWhy::NoViewport;
    if (!(in.upm >= 1.0f && in.upm <= 12.0f) || !std::isfinite(in.floorM)) return LiftWhy::NoWorld;
    if (!in.footOk) return LiftWhy::NoFoot;
    if (!in.ray) return LiftWhy::NoRay;
    float o0[3], d0[3], o1[3], d1[3];
    const float dy = in.px[1] - kProbePx >= 0.0f ? -kProbePx : kProbePx;   // (a probe above the point, else below it)
    if (!in.ray(in.px[0], in.px[1], o0, d0) || !in.ray(in.px[0], in.px[1] + dy, o1, d1)) return LiftWhy::NoRay;
    auto norm = [](float v[3]) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(l > 1e-6f) || !std::isfinite(l)) return false;
        for (int i = 0; i < 3; ++i) v[i] /= l;
        return true;
    };
    if (!norm(d0) || !norm(d1)) return LiftWhy::NoRay;
    // the head on the monster's axis, in metres (as MakeAnchor): where the ray comes nearest to the axis, else a head kFallbackHeadM high
    const float upm = in.upm, k = meleetouch::kGroundShear;
    const float fx = in.foot[0] / upm, fz = in.foot[1] / upm;
    const float ro[3] = {o0[0] / upm, o0[1] / upm, o0[2] / upm};
    float height = kFallbackHeadM, head[3];
    if (!HeadOnAxis(fx, fz, in.floorM, ro, d0, &height, head) || height < kMinHeadM || height > kMaxHeadM) {
        height = kFallbackHeadM;
        head[0] = fx + (in.floorM + height) * k;
        head[1] = in.floorM + height;
        head[2] = fz + (in.floorM + height) * k;
    }
    const float gap = in.gapCm * 0.01f;
    const float top[3] = {head[0], head[1] + gap, head[2]};   // the point gap straight over the head (the plate hangs straight up on the screen, as the game's does)
    float pd[3] = {head[0] - ro[0], head[1] - ro[1], head[2] - ro[2]};
    float qd[3] = {top[0] - ro[0], top[1] - ro[1], top[2] - ro[2]};
    const float dist = std::sqrt(pd[0] * pd[0] + pd[1] * pd[1] + pd[2] * pd[2]);
    if (!(dist >= 0.2f && dist <= kMaxDistM) || !norm(pd) || !norm(qd)) return LiftWhy::NoHead;
    // The direction the point gap higher is seen in, from the game's own head point (the ray d0): the change from the head to that point, on the ray (when the head is
    // not on it - the fallback height - the change still is the gap's, not the fallback head's place).
    float tg[3] = {d0[0] + qd[0] - pd[0], d0[1] + qd[1] - pd[1], d0[2] + qd[2] - pd[2]};
    if (!norm(tg)) return LiftWhy::NoRay;
    // Where on the screen (this column) the point gap higher is seen: Newton steps on the ray's direction, which changes by v per pixel down the screen (worked out
    // from the ray and one a few pixels off); two steps are within a pixel of the exact projection even far off the middle of the picture.
    float y = in.px[1];
    float da[3] = {d0[0], d0[1], d0[2]}, db[3] = {d1[0], d1[1], d1[2]};
    for (int step = 0; step < 2; ++step) {
        if (step) {   // the ray where the first step led, and one a few pixels off it
            float oa[3], ob[3];
            if (!in.ray(in.px[0], y, oa, da) || !in.ray(in.px[0], y + dy, ob, db) || !norm(da) || !norm(db)) return LiftWhy::NoRay;
        }
        float vv = 0.0f, wv = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float v = (db[i] - da[i]) / dy, w = tg[i] - da[i];
            vv += v * v;
            wv += w * v;
        }
        if (!(vv > 1e-12f) || !std::isfinite(wv)) return LiftWhy::NoRay;
        y += wv / vv;
        if (!std::isfinite(y)) return LiftWhy::NoRay;
    }
    const float limit = 0.5f * in.viewportH;
    const float moveDown = std::clamp(y - in.px[1], -limit, limit);   // px down the screen the point gap higher is seen from the head's point (negative: higher)
    // the plate's lower edge goes where the higher point is seen, so the anchor, which stands gameGapPx under that edge, goes there
    const float gameGap = std::clamp(std::isfinite(in.gameGapPx) ? in.gameGapPx : 19.0f, 0.0f, 0.3f * in.viewportH);
    out->anchorY = in.px[1] + moveDown + gameGap;
    out->movedPx = out->anchorY - in.px[1];
    out->distM = dist;
    for (int i = 0; i < 3; ++i) out->head[i] = head[i];
    return LiftWhy::Lifted;
}

inline void SetLift(bool on) { S().lift.store(on); }
inline bool LiftWanted() { return S().lift.load(); }
// vrcam's tick, every frame in first person: the hero's floor (metres) and game units a metre; and the game's own gap under the plate, from its widgets.
inline void SetWorld(float floorM, float upm) {
    S().floorM.store(floorM);
    S().upm.store(upm);
}
inline void SetGameGap(float px) {
    if (std::isfinite(px) && px >= 0.0f && px <= 400.0f) S().gameGapPx.store(px);
}

}  // namespace plateworld
