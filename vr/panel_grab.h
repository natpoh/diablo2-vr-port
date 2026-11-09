#pragma once
// [openxr] panel_grab: a game panel hung in front in VR F4 (vr/xr.cpp HangPanel, [openxr] menu_3d: the inventory, the stash, a trader, the
// cube, the waypoints...) is taken by a grip and carried freely in the room: it follows the hand as if held (the point taken stays where the
// hand holds it), pulling the hand in brings it nearer, pushing it out sends it farther - kPushGain times the hand's own travel, so a panel
// 1.6 m away comes to arm's length with one pull - and the stick of that hand forward / back does the same. Let go, it stays where it is in
// the room; where it was let go (relative to the gaze) is where it opens next time. Serg, 2026-11-09, after a Discord player could not hit
// the inventory with the laser: "надо сделать чтобы инвентарь можно было двигать свободно грипом, приближать отдалять, и торговца, чтобы не
// ограничивалось это лучем".
//
// Only a grip that STARTS on the panel takes it: the hand's own ray (its aim pose) meets the panel, or the hand itself is at the panel (within
// kTouchM of its face, inside it) when the grip goes down. Anywhere else the hold is not the panel's (Spent) until let go - weapon and potion
// grabs never see the panel. A grip on the panel is not a grab yet (Held): a trigger pressed first (either hand: the laser's right click with
// its own grip, a Shift click with the other's) makes it what it was before; let go without moving, its own press is given back as a tap (the
// menu's bumper, where the laser did not take it). It becomes a grab (Moving) once the hand travels kMoveStartM or the stick moves: then the
// trigger of that hand clicks nothing until let go, and the hold is no page and no Shift.
//
// Nearer than where it is all in sight (menu_zoom.h LimitsFor: the readable near) the panel keeps its size in view - it shrinks with the
// distance (WidthAt) - so at arm's length it is a tablet the laser hits from a few centimetres, not a wall wider than the eyes see.
//
// Pure (no OpenXR, no Windows): cleanroom/tests/panel_grab_test.cpp. xr.cpp runs Step before the laser (on the raw controls) and Post after it.
// The idea of grabbing a game menu is bearstonem/d2r-pcvr's (MIT, steamvr/native_panels.h Grab); the hold / tap / click rules are menu_zoom.h's.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace panelgrab {

constexpr float kMoveStartM = 0.04f;   // the hand's travel from where the grip went down that makes the hold a grab (tremble and a click's jolt stay under it)
constexpr float kPushGain = 3.0f;      // the panel's travel nearer / farther per metre the hand comes in / goes out (1 = held rigidly)
constexpr float kStickSpeed = 1.5f;    // m/s at the stick's full travel (as the big map's grab)
constexpr float kMinM = 0.25f, kMaxM = 4.0f;   // the panel's middle from the eyes, while carried
constexpr float kTouchM = 0.08f;       // the hand at the panel: this near its face (either side), inside its edges
constexpr float kMaxStepS = 0.1f;      // a stall is not a jump
constexpr float kGripOn = 0.60f, kGripOff = 0.40f, kTriggerOn = 0.55f, kTriggerOff = 0.35f;   // as menu_zoom.h (the direct layout's)
constexpr uint64_t kGripTapMs = 100;   // a grip let go without a grab, where nothing else had it: its own press now (menu_zoom.h kGripTapMs)
constexpr float kInViewSideRad = 0.873f;   // a kept place is used at the opening only within 50 degrees of the gaze ...
constexpr float kInViewUpM = 1.5f;         // ... and 1.5 m of the eyes' height

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline V3 Unit(V3 a, V3 fallback = {0.0f, 0.0f, 1.0f}) { const float l = Len(a); return l > 1e-6f && std::isfinite(l) ? Mul(a, 1.0f / l) : fallback; }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// A rotation (x, y, z, w), as OpenXR's.
struct Q { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };
inline V3 Rot(Q q, V3 v) {
    const V3 u{q.x, q.y, q.z};
    const V3 t = Mul(Cross(u, v), 2.0f);
    return Add(Add(v, Mul(t, q.w)), Cross(u, t));
}
inline Q Inv(Q q) { return {-q.x, -q.y, -q.z, q.w}; }

// A panel in the room: its middle and its axes (right, up, front = towards the eyes), as the quad is hung.
struct Panel {
    V3 at, right{1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f}, front{0.0f, 0.0f, 1.0f};
    bool ok = false;
};
// Turned to the eyes: its front from its middle to them, upright (no roll; looked at from straight above or below, the last up is kept).
inline Panel FaceEyes(V3 at, V3 eyes) {
    Panel p;
    p.at = at;
    p.front = Unit(Sub(eyes, at));
    V3 r = Cross(V3{0.0f, 1.0f, 0.0f}, p.front);
    if (Len(r) < 1e-3f) r = {1.0f, 0.0f, 0.0f};
    p.right = Unit(r);
    p.up = Cross(p.front, p.right);
    p.ok = Finite(at) && Finite(p.front);
    return p;
}

// Where it was let go, relative to the gaze: `distM` out on the ground, `sideRad` round to the left (+), `upM` above the eyes.
struct Kept { float distM = 0.0f, sideRad = 0.0f, upM = 0.0f; bool ok = false; };
inline float Wrap(float a) { return std::atan2(std::sin(a), std::cos(a)); }
inline float YawTo(V3 from, V3 to) { return std::atan2(-(to.x - from.x), -(to.z - from.z)); }   // 0 along -z, + to the left (xr.cpp YawOf's sense)
inline Kept Relative(V3 at, V3 eyes, float headYaw) {
    Kept k;
    const float dx = at.x - eyes.x, dz = at.z - eyes.z;
    k.distM = std::sqrt(dx * dx + dz * dz);
    k.sideRad = Wrap(YawTo(eyes, at) - headYaw);
    k.upM = at.y - eyes.y;
    k.ok = std::isfinite(k.distM + k.sideRad + k.upM);
    return k;
}
// seen at the opening: near the gaze, not behind the head, not under the floor - else the place in front as without the key
inline bool InView(const Kept& k) {
    return k.ok && std::isfinite(k.distM + k.sideRad + k.upM) && std::fabs(Wrap(k.sideRad)) <= kInViewSideRad && k.distM >= 0.15f &&
           k.distM <= kMaxM && std::fabs(k.upM) <= kInViewUpM && std::sqrt(k.distM * k.distM + k.upM * k.upM) >= kMinM;
}
inline Panel PlaceKept(V3 eyes, float headYaw, const Kept& k) {
    const float y = headYaw + k.sideRad;
    const V3 at = Add(Add(eyes, Mul(V3{-std::sin(y), 0.0f, -std::cos(y)}, k.distM)), V3{0.0f, k.upM, 0.0f});
    return FaceEyes(at, eyes);
}

// The panel's width at a distance: its own (`widthM`) out where all of it is in sight (`nearM`, menu_zoom.h LimitsFor; 0 = not known), nearer
// it keeps the size it has in view there - narrower with the distance.
inline float WidthAt(float distM, float widthM, float nearM) {
    if (!(nearM > 0.0f) || !(distM > 0.0f) || distM >= nearM) return widthM;
    return widthM * distM / nearM;
}

// A hand this frame, raw (before the laser took anything).
struct Hand {
    bool tracked = false;   // its aim pose located
    V3 at;                  // the aim pose's place (the ray's start; the hand)
    Q turn;                 // ... and turn
    V3 dir{0.0f, 0.0f, -1.0f};   // the ray
    float grip = 0.0f;      // 0 - 1 (its click counted as 1 by the caller)
    float trigger = 0.0f;
    bool triggerClick = false;
    float stickY = 0.0f;    // shaped, forward > 0
};

struct In {
    bool applies = false;   // the key on, VR F4, the focus, a game panel hung in front by us (not on the hand), the settings panel down
    Panel panel;            // where it hangs now (last frame's)
    float widthM = 0.0f, heightM = 0.0f;   // its size now
    V3 eyes;                // the middle of the eyes
    float dtS = 0.0f;
    Hand hand[2];
};

struct Out {
    int hand = -1;            // the hand holding it (Held or Moving), -1 none
    bool moving = false;      // it is carried: `panel` is where it hangs this frame
    Panel panel;
    float distM = 0.0f;       // (moving) its middle from the eyes
    bool started = false;     // this frame the hold became a grab
    bool released = false;    // this frame a grab ended: the panel stays at `panel`; keep it
    float fromM = 0.0f;       // (released) where the grab took it from
    float travelM = 0.0f;     // (released) how far its middle went
    bool takeY[2] = {};       // the stick's Y goes to nobody
    bool takeTrigger[2] = {}; // the trigger goes to nobody (a grab under way, until it is let go)
    bool spent[2] = {};       // this hold was a grab: no tap, no Shift, no page from it
};

// A ray (unit `dir` from `from`) against a panel: the distance and the point in its frame (metres from its middle, x right, y up).
inline bool RayPanel(V3 from, V3 dir, const Panel& p, float w, float h, float* t, float* lx, float* ly) {
    if (!p.ok) return false;
    const float dn = Dot(dir, p.front);
    if (!(dn < -1e-4f)) return false;   // from its front only
    const float d = Dot(Sub(p.at, from), p.front) / dn;
    if (!(d > 0.0f) || !std::isfinite(d)) return false;
    const V3 hit = Sub(Add(from, Mul(dir, d)), p.at);
    const float x = Dot(hit, p.right), y = Dot(hit, p.up);
    if (std::fabs(x) > 0.5f * w || std::fabs(y) > 0.5f * h) return false;
    *t = d; *lx = x; *ly = y;
    return true;
}
// The hand at the panel: within kTouchM of its face, inside its edges.
inline bool Touches(V3 at, const Panel& p, float w, float h) {
    if (!p.ok) return false;
    const V3 d = Sub(at, p.at);
    return std::fabs(Dot(d, p.front)) <= kTouchM && std::fabs(Dot(d, p.right)) <= 0.5f * w && std::fabs(Dot(d, p.up)) <= 0.5f * h;
}

class Grab {
public:
    enum class State { Idle, Held, Moving, Spent };

    Out Step(const In& in) {
        Out r;
        const float dt = in.dtS > 0.0f && std::isfinite(in.dtS) ? std::min(in.dtS, kMaxStepS) : 0.0f;
        bool trigAny = false;
        for (int h = 0; h < 2; ++h) {
            const Hand& o = in.hand[h];
            const float g = std::isfinite(o.grip) ? o.grip : 0.0f, tv = std::isfinite(o.trigger) ? o.trigger : 0.0f;
            grip_[h] = g > (grip_[h] ? kGripOff : kGripOn);
            trig_[h] = o.triggerClick || tv > (trig_[h] ? kTriggerOff : kTriggerOn);
            if (!trig_[h]) trigTaken_[h] = false;
            trigAny = trigAny || trig_[h];
        }
        for (int h = 0; h < 2; ++h) {
            const Hand& o = in.hand[h];
            State& s = state_[h];
            switch (s) {
            case State::Idle:
                if (!grip_[h]) break;
                s = State::Spent;
                seenFree_[h] = false;
                if (in.applies && !trigAny && o.tracked && state_[1 - h] != State::Held && state_[1 - h] != State::Moving) {
                    float t = 0.0f, lx = 0.0f, ly = 0.0f;
                    const bool onRay = RayPanel(o.at, o.dir, in.panel, in.widthM, in.heightM, &t, &lx, &ly);
                    if (onRay || Touches(o.at, in.panel, in.widthM, in.heightM)) {
                        s = State::Held;
                        start_ = o.at;
                        // the panel's middle in the hand's frame: it moves with the hand as if held there
                        off_ = Rot(Inv(o.turn), Sub(in.panel.at, o.at));
                        reachPrev_ = Len(Sub(o.at, in.eyes));
                        push_ = 0.0f;
                        panel_ = in.panel;
                        from_ = Len(Sub(in.panel.at, in.eyes));
                    }
                }
                break;
            case State::Held:
                if (!grip_[h]) { s = State::Idle; break; }
                if (!in.applies || trigAny || !o.tracked) { s = State::Spent; break; }
                if (Len(Sub(o.at, start_)) >= kMoveStartM || (o.stickY != 0.0f && std::isfinite(o.stickY))) {
                    s = State::Moving;
                    r.started = true;
                    reachPrev_ = Len(Sub(o.at, in.eyes));   // (the travel until now was the hold's: counted 1:1 by the attach, not pushed)
                }
                break;
            case State::Moving:
                if (!grip_[h] || !in.applies || !o.tracked) {   // let go, or the panel went (closed under the hand): it stays where it was carried
                    r.released = true;
                    r.fromM = from_;
                    r.travelM = Len(Sub(panel_.at, startPanel_));
                    r.panel = panel_;
                    s = grip_[h] ? State::Spent : State::Idle;
                    spentHold_[h] = grip_[h];
                    break;
                }
                if (trig_[h]) trigTaken_[h] = true;
                break;
            case State::Spent:
                if (!grip_[h]) { s = State::Idle; spentHold_[h] = false; }
                break;
            }
            if (s == State::Moving) {
                if (r.started) startPanel_ = in.panel.at;
                const float reach = Len(Sub(o.at, in.eyes));
                const float stick = std::isfinite(o.stickY) ? std::clamp(o.stickY, -1.0f, 1.0f) : 0.0f;
                push_ += (kPushGain - 1.0f) * (reach - reachPrev_) - stick * kStickSpeed * dt;
                reachPrev_ = reach;
                const V3 held = Add(o.at, Rot(o.turn, off_));   // where the hand holds it, rigidly
                const V3 out = Unit(Sub(held, in.eyes));
                float d = Len(Sub(held, in.eyes)) + push_;
                const float dc = std::clamp(d, kMinM, kMaxM);
                push_ += dc - d;   // the push stops at the ends: pulling back from one moves it at once
                d = dc;
                const V3 at = Add(in.eyes, Mul(out, d));
                if (Finite(at)) panel_ = FaceEyes(at, in.eyes);
                r.moving = true;
                r.panel = panel_;
                r.distM = d;
            }
            if (s == State::Held || s == State::Moving) r.hand = h;
            r.takeY[h] = s == State::Held || s == State::Moving;
            r.takeTrigger[h] = trigTaken_[h];
            r.spent[h] = s == State::Moving || spentHold_[h];
        }
        return r;
    }

    // After the laser: gripFree[h] - that grip still reaches the game (the laser did not take it). nowMs: a monotonic clock.
    // hide[h]: its press waits (it may be the start of a grab); tap[h]: its own press, now (let go on the panel without a grab).
    void Post(const bool gripFree[2], uint64_t nowMs, bool hide[2], bool tap[2]) {
        for (int h = 0; h < 2; ++h) {
            const State s = state_[h];
            const bool waiting = s == State::Held || s == State::Moving;
            if (waiting && gripFree[h]) seenFree_[h] = true;
            hide[h] = waiting && gripFree[h];
            if (last_[h] == State::Held && s == State::Idle && seenFree_[h]) tapUntil_[h] = nowMs + kGripTapMs;
            if (s != State::Idle && s != State::Held) tapUntil_[h] = 0;
            last_[h] = s;
            tap[h] = nowMs < tapUntil_[h];
        }
    }

    State state(int h) const { return state_[h & 1]; }
    bool Carrying() const { return state_[0] == State::Moving || state_[1] == State::Moving; }
    void Reset() { *this = Grab{}; }

private:
    State state_[2] = {State::Idle, State::Idle}, last_[2] = {State::Idle, State::Idle};
    bool grip_[2] = {}, trig_[2] = {}, trigTaken_[2] = {}, seenFree_[2] = {}, spentHold_[2] = {};
    uint64_t tapUntil_[2] = {};
    V3 start_, off_, startPanel_;
    float reachPrev_ = 0.0f, push_ = 0.0f, from_ = 0.0f;
    Panel panel_;
};

}  // namespace panelgrab
