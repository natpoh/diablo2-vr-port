#pragma once
// map_laser.h - [hud] map_click_big: the right hand's laser on the map in the hand (VR F4, native OpenXR, [hud] map=1), a click makes it
// big. Pure: no game, no XR, no Windows (cleanroom/tests/map_laser_test.cpp); xr.cpp MapLaserStep / HangBigMap wire it.
//
// The right controller's aim pointing at the map in the hand (its flat quad, or the orb's ball) draws the laser's beam and dot on it. A
// trigger pressed there opens the game's own BIG map: the game's whole-screen automap (its map mode 0, set by the game's own setter -
// vr/minimap_side.h), taken out of the picture with the rest of the interface and hung big in front, a "Rotate" button under it. While it
// is big a press on the button flips [hud] map_rotate (the map in the hand turned with the look, vr/map_turn.h) and is saved; a press
// anywhere else - on the big map or away from it - puts the map back in the hand (its corner mode again, minimapside::Keeper).
//
// The trigger is the laser's only while it points at the map (or while the map is big: then it is all the laser's), and that is decided
// while the trigger is let go - a press that began as an attack stays the game's to its end, one that began on the map never reaches it.
// Visual only: the game's own map in its own two modes; nothing is clicked in the game.

#include <cmath>

namespace maplaser {

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// A quad in the room: its middle, its right / up / front (unit, front toward the viewer), its size; round: only the circle inside it
// counts (the orb's ball: `ball` its diameter, else the quad's width).
struct Quad { V3 at, right, up, front; float w = 0.0f, h = 0.0f; bool round = false; float ball = 0.0f; bool ok = false; };
struct Hit { bool hit = false; float t = 0.0f; float u = 0.0f, v = 0.0f; V3 at; };   // u, v: 0..1 from the quad's top left

// A ray (from, unit dir) against a quad's front, at most `reach` metres out.
inline Hit RayQuad(V3 from, V3 dir, const Quad& q, float reach = 4.0f) {
    Hit h;
    if (!q.ok || !(q.w > 0.0f) || !(q.h > 0.0f)) return h;
    const float dn = Dot(dir, q.front);
    if (!(dn < -1e-4f)) return h;   // from behind, or along it
    const float t = Dot(Sub(q.at, from), q.front) / dn;
    if (!(t > 0.0f) || t > reach || !std::isfinite(t)) return h;
    const V3 p = Add(from, Mul(dir, t));
    const V3 d = Sub(p, q.at);
    const float x = Dot(d, q.right), y = Dot(d, q.up);
    if (q.round) {
        const float r = 0.5f * (q.ball > 0.0f ? q.ball : q.w);
        if (x * x + y * y > r * r) return h;
    } else if (std::fabs(x) > 0.5f * q.w || std::fabs(y) > 0.5f * q.h) {
        return h;
    }
    h.hit = true; h.t = t; h.at = p;
    h.u = x / q.w + 0.5f; h.v = 0.5f - y / q.h;
    return h;
}

// What one frame of the laser on the map does (Clicker::Step).
enum class Event { None, Open, Close, Rotate, Front };   // Front: the big map to the front / back on the hand ([hud] map_big_on_hand)
struct Frame {
    bool claim = false;   // the laser hand's trigger is the laser's this frame (hidden from the game)
    bool beam = false;    // the beam and dot drawn (it points at the map, the big map or its button - or the map is big)
    Event event = Event::None;
};

// The click's state from frame to frame. allowed: the key ticked, VR F4, the hand tracked, no menu / settings panel / skill wheel / other
// laser; onMap: it points at the map in the hand (not big); onBig, onButton: at the big map, at its Rotate button (while big); onFront: at
// the button that puts the big map in front / back on the hand (while big, [hud] map_big_on_hand); trigger: pressed.
class Clicker {
    bool big_ = false, claimed_ = false, was_ = false;
public:
    bool Big() const { return big_; }
    Frame Step(bool allowed, bool onMap, bool onBig, bool onButton, bool trigger) { return Step(allowed, onMap, onBig, onButton, false, trigger); }
    Frame Step(bool allowed, bool onMap, bool onBig, bool onButton, bool onFront, bool trigger) {
        Frame f;
        if (!allowed && big_) { big_ = false; f.event = Event::Close; }   // the map goes back to the hand when the laser cannot act
        if (!trigger) claimed_ = allowed && (big_ || onMap);              // who has the trigger: decided while it is let go
        f.claim = claimed_;
        f.beam = allowed && (big_ || onMap);
        if (allowed && claimed_ && trigger && !was_ && f.event == Event::None) {   // a fresh press, the laser's
            if (!big_) {
                if (onMap) { big_ = true; f.event = Event::Open; }
            } else if (onButton) {
                f.event = Event::Rotate;
            } else if (onFront) {
                f.event = Event::Front;
            } else {
                big_ = false; f.event = Event::Close;
                (void)onBig;   // (on the big map or away from it: back to the hand)
            }
        }
        was_ = trigger;
        return f;
    }
    void Reset() { *this = Clicker{}; }
};

// Where the big map hangs: ahead of the eyes at the head's turn when it opened, `distM` out, `belowM` under the eyes, `widthM` wide; its
// button centred under it, a gap below. Positions in the room (y up); yaw: the head's turn (radians, 0 = looking along -z, + to the left).
struct Place { V3 at, button; float w = 0.0f, h = 0.0f, bw = 0.0f, bh = 0.0f; };
inline Place PlaceBig(V3 eyes, float yaw, float aspectWOverH, float distM = 1.3f, float widthM = 1.2f, float belowM = 0.1f) {
    Place p;
    const float a = aspectWOverH > 0.2f && aspectWOverH < 5.0f ? aspectWOverH : 1.0f;
    const V3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    p.w = widthM; p.h = widthM / a;
    p.at = Add(Add(eyes, Mul(ahead, distM)), V3{0.0f, -belowM, 0.0f});
    p.bw = 0.28f * widthM; p.bh = 0.25f * p.bw;
    p.button = Add(p.at, V3{0.0f, -0.5f * p.h - 0.04f - 0.5f * p.bh, 0.0f});
    return p;
}

// -- the big map in front: where it stands in the room (Serg, 2026-10-28) ------------------------------------------------------------
// A spot: the big map's middle in the room and its turn (it faces the eyes along it). Opened, it is put at a place relative to the gaze -
// `distM` out (on the ground), `sideRad` round to the left (+), `upM` above the eyes - and stays there in the room (world-locked: the head
// may look away and back, rise or bend; its height never follows). [hud] map_big_follow: once the gaze is more than `dead` off it, it
// comes round after the gaze, lazily (the part past the dead zone eased away with time constant `tauS`) - round the head, at its distance
// and height, no snap. [hud] map_big_grab: the laser and the right grip drag it (Drag), the stick changes how far; where it is let go is
// kept relative to the gaze (Relative) and is where it opens from then on - unless that is out of view (InView), then the place in front.
struct Spot { V3 at; float yaw = 0.0f; bool ok = false; };
constexpr float kDistM = 1.3f, kSideRad = 0.0f, kUpM = -0.1f;   // the standard place: 1.3 m ahead, 10 cm under the eyes
inline float Wrap(float a) { return std::atan2(std::sin(a), std::cos(a)); }
// the turn that looks from `from` to `to` on the ground (Yaw's sense: 0 along -z, + to the left)
inline float YawTo(V3 from, V3 to) { return std::atan2(-(to.x - from.x), -(to.z - from.z)); }
inline Spot SpotFor(V3 eyes, float headYaw, float distM, float sideRad, float upM) {
    Spot s;
    const float y = headYaw + sideRad;
    s.at = Add(Add(eyes, Mul(V3{-std::sin(y), 0.0f, -std::cos(y)}, distM)), V3{0.0f, upM, 0.0f});
    s.yaw = Wrap(y);
    s.ok = std::isfinite(s.at.x + s.at.y + s.at.z + s.yaw);
    return s;
}
// a kept place is seen at its opening: no more than 50 degrees off the gaze, 0.3 - 4 m out, within 1.5 m of the eyes' height
inline bool InView(float distM, float sideRad, float upM) {
    return std::isfinite(distM + sideRad + upM) && std::fabs(Wrap(sideRad)) <= 0.873f && distM >= 0.3f && distM <= 4.0f && std::fabs(upM) <= 1.5f;
}
// the spot as kept: relative to the gaze (head's yaw) and the eyes
inline void Relative(const Spot& s, V3 eyes, float headYaw, float* distM, float* sideRad, float* upM) {
    const float dx = s.at.x - eyes.x, dz = s.at.z - eyes.z;
    *distM = std::sqrt(dx * dx + dz * dz);
    *sideRad = Wrap(YawTo(eyes, s.at) - headYaw);
    *upM = s.at.y - eyes.y;
}
// the lazy follow: past `dead` off the gaze, eased round the head (its own height and distance kept)
inline Spot Follow(const Spot& s, V3 eyes, float headYaw, float dtS, float dead = 0.61f, float tauS = 0.6f) {
    if (!s.ok || !(dtS > 0.0f)) return s;
    const float at = YawTo(eyes, s.at), off = Wrap(headYaw - at);
    if (std::fabs(off) <= dead) return s;
    const float past = off - (off > 0.0f ? dead : -dead);
    const float k = 1.0f - std::exp(-std::fmin(dtS, 0.25f) / tauS);
    const float y = at + past * k;
    const float dx = s.at.x - eyes.x, dz = s.at.z - eyes.z, d = std::sqrt(dx * dx + dz * dz);
    Spot n;
    n.at = {eyes.x - std::sin(y) * d, s.at.y, eyes.z - std::cos(y) * d};
    n.yaw = Wrap(y);
    n.ok = true;
    return n;
}
// dragged by the laser: the point it held stays on the ray at `grabDist` (the stick moves it along), the map turned to the eyes
inline Spot Drag(V3 from, V3 dir, float grabDist, V3 offset, V3 eyes) {
    Spot s;
    s.at = Add(Add(from, Mul(dir, grabDist)), offset);
    s.yaw = YawTo(eyes, s.at);
    s.ok = std::isfinite(s.at.x + s.at.y + s.at.z + s.yaw);
    return s;
}
// the big map and its Rotate button at a spot
inline Place PlaceAt(const Spot& s, float aspectWOverH, float widthM = 1.2f) {
    Place p;
    const float a = aspectWOverH > 0.2f && aspectWOverH < 5.0f ? aspectWOverH : 1.0f;
    p.w = widthM; p.h = widthM / a;
    p.at = s.at;
    p.bw = 0.28f * widthM; p.bh = 0.25f * p.bw;
    p.button = Add(p.at, V3{0.0f, -0.5f * p.h - 0.04f - 0.5f * p.bh, 0.0f});
    return p;
}

// [hud] map_beside_inventory: the inventory's panel on the left hand ([openxr] hand_inventory) would cover the map held in the same hand -
// the map hangs beside the panel's left edge instead, in its plane, its top level with the panel's top, a gap between (w x h: the map's
// visible size - the orb's ball, not its quad). `at`, `right`, `up`: the panel's middle and axes; panelW x panelH its size.
inline V3 BesideLeft(V3 at, V3 right, V3 up, float panelW, float panelH, float w, float h, float gap = 0.02f) {
    return Add(Add(at, Mul(right, -(0.5f * panelW + gap + 0.5f * w))), Mul(up, 0.5f * panelH - 0.5f * h));
}
// [hud] map_big_on_hand: the Rotate button over the big map held on the hand (under it the controller): centred, a gap above its top edge.
inline V3 ButtonAbove(V3 at, V3 up, float panelH, float bh, float gap = 0.02f) { return Add(at, Mul(up, 0.5f * panelH + gap + 0.5f * bh)); }

}  // namespace maplaser
