// bow_orbs.h - [bow] quiver_orbs: a row of energy orbs along the bow's stave, one for every bow skill the hero has (and one for the plain shot); the free hand takes one with its trigger
// (or, by choice, its grip) and that skill is armed for the next shots (the armed one's orb again, or the plain shot's, disarms). Pure: no game calls, no globals, no windows.h (cleanroom/tests/bow_orbs_test.cpp feeds it synthetic numbers). The bow's own bones give the
// stave (skel::BowStave: the riser's middle, the end of the limb that points up, which way the archer is); the orbs sit on it above the grip, on the archer's side of the limb, and
// follow the bow. What a taken orb does is the quiver's (vr/bow_quiver.h Machine::Pick): it arms the skill as the quiver's own state - nothing is written into the game's
// slots, never a server call.
//
// Which wins, the orb or the string: inside the reach of an orb (7.5 cm from its middle by default, a slider) the trigger and the grip are the orb's - the one that picks picks, the other
// does nothing - and the draw is held back there (bow_draw_wire's start block); anywhere else the string is taken as it always was. The orbs begin 18 cm up the stave and 20 cm or more from the string's resting place.
//
// Everything the player can tune is in Tuning (the ini's [bow] quiver_orb_*, live): the orb's size, the grab radius (bigger than the orb: the hand is a palm, not a point), where the row
// starts on the stave, how far apart the orbs are, a shift of the row off the stave along the bow's own axes, and a turn of the row about the same three axes (pivoting on its first orb).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "bow_draw.h"
#include "bow_quiver.h"
#include "bow_touch.h"

namespace boworbs {

using bowtouch::Vec;

inline constexpr int kMaxOrbs = 12;              // the effect's (D2R_DepthFog.fx QvPos[12])
inline constexpr float kTo = 0.92f;              // the row ends at this share of the stave's length from the grip (a long row is squeezed into it)
inline constexpr float kPlainRgb[3] = {0.86f, 0.89f, 0.96f};   // the plain shot's orb (and a non-arrow skill the slot held)

// What the player tunes (metres), the defaults and the limits. Offsets are along the bow's own axes: x across it (to the archer's right when the bow stands upright), y along the
// stave (up), z toward the archer (the side the orbs sit on), added to the place the layout works out.
struct Tuning {
    float radius = 0.022f;     // the orb's radius
    float reach = 0.075f;      // the grab radius round an orb's middle
    float start = 0.18f;       // from the grip along the stave to the first orb
    float spacing = 0.065f;    // between orbs (the widest; a long row is squeezed to fit the stave)
    float offset[3] = {0.0f, 0.0f, 0.0f};
    // The row turned about the bow's own axes (degrees; x across it, y along the stave, z toward the archer; right-hand rule), pivoting on the first orb - where the shift above puts it.
    // Applied in the order x, then y, then z, about the axes as they stand (R = Rz Ry Rx). 0, 0, 0 = the row lies along the stave, as it always did.
    float turnDeg[3] = {0.0f, 0.0f, 0.0f};
    static float Fin(float v, float def) { return std::isfinite(v) ? v : def; }
    Tuning Clamped() const {
        Tuning t;
        t.radius = std::clamp(Fin(radius, 0.022f), 0.008f, 0.060f);
        t.reach = std::clamp(Fin(reach, 0.075f), 0.020f, 0.250f);
        t.start = std::clamp(Fin(start, 0.18f), 0.0f, 0.60f);
        t.spacing = std::clamp(Fin(spacing, 0.065f), 0.020f, 0.200f);
        for (int k = 0; k < 3; ++k) t.offset[k] = std::clamp(Fin(offset[k], 0.0f), -0.30f, 0.30f);
        for (int k = 0; k < 3; ++k) t.turnDeg[k] = std::clamp(Fin(turnDeg[k], 0.0f), -180.0f, 180.0f);
        return t;
    }
};

// The bow's stave in the hands' frame (metres from the head, the head's turn): the riser's middle and the end of the limb that points up, and the unit direction from the stave toward
// the archer.
struct Stave {
    Vec grip, tip, back;
    bool fromModel = false;   // read off the bow's own bones (else made from the controller)
};

// The stave from the bow hand when the bow's bones were not read: up the world's vertical, perpendicular to where the bow points (`forward`, unit, in the same frame as the hand:
// bowdraw::BowForward of the controller's rotation, turned into the head's turn frame), 65 cm long, the archer's side behind it.
inline bool StaveFromHand(Vec hand, Vec forward, Stave* out) {
    if (!out || !bowdraw::Finite(hand) || !bowdraw::Finite(forward)) return false;
    if (!(bowdraw::Length(forward) > 0.5f)) return false;
    const Vec worldUp{0.0f, 1.0f, 0.0f};
    Vec up = worldUp - forward * bowdraw::Dot(worldUp, forward);
    if (!(bowdraw::Length(up) > 0.3f)) return false;   // the bow points straight up or down
    up = bowdraw::Normalized(up);
    out->grip = hand;
    out->tip = hand + up * 0.65f;
    out->back = forward * -1.0f;
    out->fromModel = false;
    return true;
}

inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// `v` turned by Euler angles `deg` (degrees) about the stave's own axes - `across` (x), `up` (y, along the stave), `side` (z, toward the archer), a right-handed frame as MakeLayout builds
// it - in the order x, then y, then z, each about the axis as it stands (R = Rz Ry Rx). x takes y toward z (a row standing on the stave leans toward the archer), y takes z toward x, z takes
// x toward y (the row leans to the left). A row lies along y, so a turn about y alone leaves it where it is; after a tilt about x or z it swings the tilt round the stave.
inline Vec TurnInFrame(Vec v, Vec across, Vec up, Vec side, const float deg[3]) {
    constexpr float kRad = 3.14159265358979f / 180.0f;
    const float cx = std::cos(deg[0] * kRad), sx = std::sin(deg[0] * kRad);
    const float cy = std::cos(deg[1] * kRad), sy = std::sin(deg[1] * kRad);
    const float cz = std::cos(deg[2] * kRad), sz = std::sin(deg[2] * kRad);
    float x = bowdraw::Dot(v, across), y = bowdraw::Dot(v, up), z = bowdraw::Dot(v, side);
    float t = y * cx - z * sx; z = y * sx + z * cx; y = t;   // about x: y toward z
    t = x * cy + z * sy; z = -x * sy + z * cy; x = t;        // about y: z toward x
    t = x * cz - y * sz; y = x * sz + y * cz; x = t;         // about z: x toward y
    return across * x + up * y + side * z;
}

struct Orb {
    Vec c;
    float r = 0.0f;
    float rgb[3] = {1, 1, 1};
    bowquiver::Entry entry;
    bool current = false;    // the armed skill (the plain shot's orb when none)
    float hover = 0.0f;      // 0..1: how near the hand is - 1 for the orb it is at (the one a grip takes), a ramp for those it is nearing
};
struct Layout {
    Orb orb[kMaxOrbs];
    int n = 0;
    float reach = 0.075f;    // how near the hand has to be to an orb's middle to have it (m)
    float spacing = 0.0f;
};

inline void ColourOf(const bowquiver::Entry& e, float rgb[3]) {
    if (const bowquiver::Arrow* a = bowquiver::FindArrow(e.id)) { for (int k = 0; k < 3; ++k) rgb[k] = a->rgb[k]; }
    else { for (int k = 0; k < 3; ++k) rgb[k] = kPlainRgb[k]; }
}

// The orbs of `list` (the quiver's cycle: the plain shot first, then the arrows the hero has) along the stave, the first nearest the grip. False for a stave that is not one
// (not finite, shorter than 20 cm) or an empty list. `current` marks the orb of the skill the slot holds now.
inline bool MakeLayout(const Stave& st, const bowquiver::Entry* list, size_t count, const bowquiver::Entry* current, const Tuning& tuning, Layout* out) {
    if (!out || !list || count == 0 || !bowdraw::Finite(st.grip) || !bowdraw::Finite(st.tip) || !bowdraw::Finite(st.back)) return false;
    const Tuning tn = tuning.Clamped();
    const Vec stave = st.tip - st.grip;
    const float len = bowdraw::Length(stave);
    if (!(len >= 0.20f) || !(len <= 1.50f)) return false;
    const Vec u = stave * (1.0f / len);
    Vec side = st.back - u * bowdraw::Dot(st.back, u);   // the archer's side, square to the stave
    if (!(bowdraw::Length(side) > 0.2f)) return false;
    side = bowdraw::Normalized(side);
    const Vec across = bowdraw::Normalized(Cross(u, side));   // (up x back is to the right)
    const int n = (int)std::min<size_t>(count, (size_t)kMaxOrbs);
    const float s0 = std::min(tn.start, 0.9f * len), s1 = std::max(s0, kTo * len);
    const float d = n > 1 ? std::min(tn.spacing, (s1 - s0) / (float)(n - 1)) : 0.0f;
    const float r = tn.radius;
    const bool turned = tn.turnDeg[0] != 0.0f || tn.turnDeg[1] != 0.0f || tn.turnDeg[2] != 0.0f;
    const Vec pivot = st.grip + u * (s0 + tn.offset[1]) + side * (r + 0.02f + tn.offset[2]) + across * tn.offset[0];   // the first orb: where the row starts, shift and all
    Layout l;
    l.n = n;
    l.spacing = d;
    l.reach = tn.reach;
    for (int i = 0; i < n; ++i) {
        Orb& o = l.orb[i];
        if (turned) o.c = pivot + TurnInFrame(u * (d * (float)i), across, u, side, tn.turnDeg);   // the row turned on its first orb
        else o.c = st.grip + u * (s0 + d * (float)i + tn.offset[1]) + side * (r + 0.02f + tn.offset[2]) + across * tn.offset[0];   // (as it always was, to the last bit)
        o.r = r;
        o.entry = list[i];
        ColourOf(list[i], o.rgb);
        o.current = current && list[i] == *current;
    }
    *out = l;
    return true;
}

// The orb the hand is at: the nearest one whose middle is within the reach; -1 for none. `dist` gets the distance to it (or to the nearest orb when none is within the reach).
inline int Hover(const Layout& l, Vec hand, float* dist = nullptr) {
    if (dist) *dist = 1e9f;
    if (!bowdraw::Finite(hand)) return -1;
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < l.n; ++i) {
        const float d = bowdraw::Length(l.orb[i].c - hand);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (dist) *dist = bestD;
    return best >= 0 && bestD <= l.reach ? best : -1;
}

// How far each orb has been "reached": the one the hand is at (`hovered`, from Hover) fully, the others by a ramp that starts at twice the reach, so the player sees the orb ahead
// of the grab (at most half for those the hand is not at).
inline void Approach(Layout* l, Vec hand, int hovered) {
    if (!l) return;
    for (int i = 0; i < l->n; ++i) {
        Orb& o = l->orb[i];
        if (i == hovered) { o.hover = 1.0f; continue; }
        const float d = bowdraw::Finite(hand) ? bowdraw::Length(o.c - hand) : 1e9f;
        o.hover = std::clamp((2.0f * l->reach - d) / std::max(l->reach, 1e-4f), 0.0f, 1.0f) * 0.5f;
    }
}

// ---- when ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The orbs NEVER go (the player: "они не должны никогда пропадать"): with a bow in hand in F4 they stay on the stave at rest, through the draw, at the release, and while a hand is near them.
// Only four things take them away: no bow in hand, not F4, the tick off, and the stave not read at all - and a frame that cannot read it keeps the last good row for `kHoldS` rather than
// blinking. What waits is the TAKING: the free hand is the string hand while the string is drawn, and the hand that has just let go swings past the bow, so an orb is taken (and the hand's
// hover shown) only once the draw is `kSettleS` behind it.
inline constexpr double kSettleS = 0.6;
inline bool Settled(bool drawing, double secondsSinceDrawEnded) { return !drawing && secondsSinceDrawEnded > kSettleS; }
inline constexpr double kHoldS = 0.5;
// Whether the last good row is still shown when this frame made none (the stave not read, the list not known): for `kHoldS` after it was good, never before it was good at all.
inline bool Holds(bool hadRow, double secondsSinceGood) { return hadRow && secondsSinceGood >= 0.0 && secondsSinceGood < kHoldS; }

// ---- the grab ---------------------------------------------------------------------------------------------------------------------------------------------------------
// The free hand's trigger (or grip) on an orb: a press (the control going down, from up) while the hand is at the orb takes it. One held as the hand comes in takes nothing.
inline constexpr float kPressDown = 0.55f, kPressUp = 0.20f;

struct GrabIn {
    bool tracked = false;
    bool allowed = false;     // the orbs are shown and the hand may take one (no menu, not drawn)
    Vec hand;                 // the hand's grip point, the hands' frame
    float press = -1.0f;      // 0..1: the control that takes an orb (the trigger, or the grip by choice); < 0 not known
    bool latch = false;       // a press that took an orb stays the orb's (`claim`) until it is let go, though the hand has left the orb: a trigger held on would start the game's attack
};
struct GrabOut {
    int hover = -1;           // the orb the hand is at
    float dist = 1e9f;        // how far the hand is from the nearest orb's middle (m), at an orb or not
    bool claim = false;       // the controls are the orb's now: nothing else may take them (a draw that starts on the trigger or the grip, the shot, the belt)
    int pick = -1;            // exactly one step per press: the orb taken
};

class Grab {
public:
    void Reset() { *this = Grab{}; }
    GrabOut Step(const GrabIn& in, const Layout& l) {
        GrabOut out;
        const bool known = std::isfinite(in.press) && in.press >= 0.0f;
        const bool down = known && in.press > (wasDown_ ? kPressUp : kPressDown);
        if (!in.tracked || !in.allowed || !known) { wasDown_ = down; owned_ = false; return out; }   // (a control already down when the hand comes back is nobody's press)
        out.hover = Hover(l, in.hand, &out.dist);
        out.claim = out.hover >= 0 || (in.latch && owned_ && down);
        if (out.hover >= 0 && down && !wasDown_) { out.pick = out.hover; owned_ = in.latch; }
        if (!down) owned_ = false;
        wasDown_ = down;
        return out;
    }

private:
    bool wasDown_ = false;
    bool owned_ = false;      // the press under way took an orb (and `latch` was asked for)
};

}  // namespace boworbs
