#pragma once
// The skill wheel's layout: where each wedge, button and list entry is on the panel, and what a point on the panel
// hits. The painter (vr/wheel_paint.cpp) and the hit test read the same numbers, so what is drawn is what is clicked.
// Pure: no Windows, no OpenXR, no game.
//
// The idea (eight slots = the eight controller skill slots, a centre for the basic attack, an editor in two steps) is
// bearstonem/d2r-pcvr's wheel (MIT, steamvr/interactions.h); the numbers and the drawing are ours.
//
// Panel coordinates: u right, v down, both 0..1 over a square panel.

#include <cmath>
#include <cstdint>

namespace wheel {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kSlots = 8;          // B X Y RB, then the same four with LT held
constexpr int kOptionsPerPage = 8;   // the editor's list: 2 columns x 4 rows

enum class Page : uint8_t {
    Wheel,       // the eight wedges: pick the slot the trigger casts
    PickSlot,    // the same ring: pick the slot to assign
    PickSkill,   // a page of the learned skills for that slot
};

// What a point hits: 0..7 a wedge or a tile (Wheel, PickSlot) or a list / grid entry (PickSkill; up to 24 of them in the tiles'
// look), or one of these - numbered above every possible entry so that no entry is ever mistaken for a button.
enum Cell : int {
    kNone = -1,
    kCentre = 32,   // Wheel: BASIC ATTACK; PickSkill: CLEAR SLOT
    kPrev = 33,     // PickSkill: the page before
    kNext = 34,     // PickSkill: the page after
    kBack = 35,     // PickSlot, PickSkill: one step back
    kEdit = 36,     // Wheel: EDIT SKILLS
    kClose = 37,    // Wheel: CLOSE
    kTabSkills = 38,     // tiles: the tab strip's SKILLS (the window's top level only)
    kTabSettings = 39,   // tiles: ... and SETTINGS, when the key is on
    kFlipPrev = 40,      // tiles with the tab strip: the arrow at the window's left edge (one place back)
    kFlipNext = 41,      // ... and at its right edge (one place on)
};

struct Rect {
    float l, t, r, b;
    bool Has(float u, float v) const { return u >= l && u < r && v >= t && v < b; }
};

// The ring: the centre button inside kCentreR, the wedges between kRingIn and kRingOut, about (kCx, kCy).
constexpr float kCx = 0.5f, kCy = 0.44f;
constexpr float kCentreR = 0.10f, kRingIn = 0.135f, kRingOut = 0.39f;
// Wedge k covers the angles [k, k+1) * 45 degrees clockwise from straight up: wedge 0 is the upper right.
constexpr float kWedge = kPi / 4.0f;
constexpr float kWedgeHold = 0.035f;   // radians a pointer may stray into the next wedge before the hover moves (jitter)

inline int FooterCount(Page page) { return page == Page::Wheel ? 2 : page == Page::PickSlot ? 1 : 3; }
inline Cell FooterCell(Page page, int index) {
    if (page == Page::Wheel) return index == 0 ? kEdit : kClose;
    if (page == Page::PickSlot) return kBack;
    return index == 0 ? kPrev : index == 1 ? kNext : kBack;
}
inline Rect FooterRect(Page page, int index) {
    const int n = FooterCount(page);
    const float gap = 0.02f, span = 0.94f, w = (span - gap * (n - 1)) / n;
    const float l = 0.03f + index * (w + gap);
    return {l, 0.905f, l + w, 0.978f};
}

// The editor's list entry i (0..7), and the CLEAR SLOT button above it.
inline Rect OptionRect(int i) {
    const float x = 0.05f + (i % 2) * 0.46f, y = 0.23f + (i / 2) * 0.145f;
    return {x, y, x + 0.44f, y + 0.13f};
}
constexpr Rect kClearRect{0.64f, 0.115f, 0.95f, 0.195f};

// The centre of wedge k, as an angle clockwise from straight up.
inline float WedgeAngle(int k) { return (k + 0.5f) * kWedge; }
// A point `radius` out from the ring's centre along wedge k's middle, in panel units (where its icon and name go).
inline void WedgePoint(int k, float radius, float* u, float* v) {
    const float a = WedgeAngle(k);
    *u = kCx + std::sin(a) * radius;
    *v = kCy - std::cos(a) * radius;
}

// The wedge a point is in, or -1 (outside the ring, or in its hole). `previous` (0..7 or -1) is the wedge the pointer was
// on: it keeps the hover while the pointer is within kWedgeHold of its edge.
inline int WedgeAt(float u, float v, int previous) {
    const float x = u - kCx, y = v - kCy, r = std::hypot(x, y);
    if (r < kRingIn || r > kRingOut) return -1;
    float a = std::atan2(x, -y);
    if (a < 0.0f) a += 2.0f * kPi;
    if (previous >= 0 && previous < kSlots) {
        const float d = std::remainder(a - WedgeAngle(previous), 2.0f * kPi);
        if (std::fabs(d) < kWedge * 0.5f + kWedgeHold) return previous;
    }
    return int(std::floor(a / kWedge)) % kSlots;
}

// What a point on the panel hits on this page. `options`: how many entries the PickSkill page shows. `previous`: the hover
// last frame (for WedgeAt's hold).
inline int HitTest(Page page, float u, float v, int previous, int options) {
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return kNone;
    for (int i = 0; i < FooterCount(page); ++i)
        if (FooterRect(page, i).Has(u, v)) return FooterCell(page, i);
    if (page == Page::PickSkill) {
        if (kClearRect.Has(u, v)) return kCentre;
        for (int i = 0; i < kOptionsPerPage && i < options; ++i)
            if (OptionRect(i).Has(u, v)) return i;
        return kNone;
    }
    if (std::hypot(u - kCx, v - kCy) < kCentreR) return page == Page::Wheel ? int(kCentre) : int(kNone);
    return WedgeAt(u, v, previous);
}

}  // namespace wheel
