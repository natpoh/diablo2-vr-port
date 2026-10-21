#pragma once
// [openxr] menu_zoom: a game panel hung in front in VR F4 (the inventory, the stash, a trader, the waypoints... - vr/xr.cpp
// HangPanel, [openxr] menu_3d) is brought nearer / sent farther with the RIGHT grip held and the right stick pushed forward /
// pulled back, live; let go, the distance is written to [screen] menu_distance_m (the panels' distance key, D2R VR Settings >
// UI: F4 body and the headset's "Panel distance") and stays. The author's ask, 2026-10-21: "меню можно сделать приближение
// отдаление оверлея при зажатом грипе правой руки и джойстиком в меню торговца".
//
// Pure (no OpenXR, no Windows): cleanroom/tests/menu_zoom_test.cpp. xr.cpp's Publish runs it on the right hand's raw controls:
// Pre before the laser (the stick's Y and a trigger squeezed while zooming are taken there), Post after it (the grip itself).
//
// The idea of taking a game menu by the right grip to push or pull it is bearstonem/d2r-pcvr's (v0.3.24, MIT; steamvr/native_panels.h
// Grab, commits a3893be / 28ac2c1 / 36ab1d4: "game menus push and pull only", a grip then a click stays a click, a trigger squeezed
// while moving is consumed until let go). There the hand's own travel moves the panel; here the stick does, and the limits are
// where the panel stops being readable rather than fixed numbers.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace menuzoom {

// [screen] menu_distance_m's own range: D2R VR Settings' slider and the headset settings panel's "Panel distance" stop at 8 m
// (vr/hset_defs.h; the test keeps them equal), and the plugin never takes a panel nearer than 0.5 m (xr.cpp LoadSettings).
constexpr float kKeyMinM = 0.5f, kKeyMaxM = 8.0f;
constexpr float kSpeedDefault = 1.0f;   // [openxr] menu_zoom_speed_mps: metres a second at the stick's full travel
constexpr float kSpeedMin = 0.1f, kSpeedMax = 5.0f;
// a stall (a hitch, the headset off the head) is not a jump: one step never covers more time than this
constexpr float kMaxStepS = 0.1f;
// a grip let go without a zoom, where nothing else had it: its own press, given to the game now (the pad is polled many times in it - hold_tap.h's kPulseMs)
constexpr uint64_t kGripTapMs = 100;
// a grip and a trigger count as pressed where the direct layout presses them (direct_layout.h kGripOn / kGripOff, kTriggerOn / kTriggerOff; the test keeps
// them equal): a grip half squeezed is the zoom's from the same moment it would have been the menu's right bumper
constexpr float kGripOn = 0.60f, kGripOff = 0.40f, kTriggerOn = 0.55f, kTriggerOff = 0.35f;
// what the ini is written with ("%.2f"): the distance saved, and the change that counts as one
constexpr float kSaveStepM = 0.01f;

// An eye: its field of view as tangents (left and down negative), and the picture the runtime recommends for it - the pixels it
// shows that field with.
struct Eye {
    float tanL = 0.0f, tanR = 0.0f, tanU = 0.0f, tanD = 0.0f;
    uint32_t pixelsW = 0, pixelsH = 0;
};

struct Limits {
    float nearM = kKeyMinM, farM = kKeyMaxM;
    bool derived = false;   // from the eyes and the panel (false: the key's range only - nothing known yet)
};

// Where a panel `widthM` x `heightM` showing a picture of texW x texH stays readable:
//  near: all of it in sight at once - its half width within the outer edges of the eyes' fields (the left eye's left, the right
//        eye's right; the panel hangs ahead of their middle), its half height within the lower of the two eyes' up and down;
//        nearer, part of the menu is out of sight and the head has to go looking for it.
//  far:  a display pixel spans at most two of the panel's texels. The panel's image has no smaller copies (xr.cpp EnsureHudOut:
//        mipCount 1), and the runtime samples it bilinearly - two texels a way: farther, texels fall between the samples and the
//        thin strokes of the game's text break up and flicker. The eye with the coarser pixels decides, across and up.
// Both stay in the key's range; a far nearer than the near (a coarse headset) is the near - the readable distance is one.
inline Limits LimitsFor(float widthM, float heightM, uint32_t texW, uint32_t texH, const Eye eyes[2]) {
    Limits l;
    if (!(widthM > 0.0f) || !(heightM > 0.0f) || !texW || !texH || !eyes) return l;
    const Eye& a = eyes[0];
    const Eye& b = eyes[1];
    const float across = std::min(-a.tanL, b.tanR);
    const float up = std::min({a.tanU, b.tanU, -a.tanD, -b.tanD});
    float densX = 0.0f, densY = 0.0f;   // display pixels per unit of tangent, the coarser eye's
    for (const Eye* e : {&a, &b}) {
        const float w = e->tanR - e->tanL, h = e->tanU - e->tanD;
        if (!(w > 0.0f) || !(h > 0.0f) || !e->pixelsW || !e->pixelsH) return l;
        const float dx = (float)e->pixelsW / w, dy = (float)e->pixelsH / h;
        densX = densX > 0.0f ? std::min(densX, dx) : dx;
        densY = densY > 0.0f ? std::min(densY, dy) : dy;
    }
    if (!(across > 0.0f) || !(up > 0.0f) || !std::isfinite(across) || !std::isfinite(up)) return l;
    const float nearM = std::max(0.5f * widthM / across, 0.5f * heightM / up);
    // a texel of the panel at d is (widthM / texW) / d of tangent; a display pixel is 1 / dens: 1 / dens <= 2 (widthM / texW) / d
    const float farM = std::min(2.0f * widthM * densX / (float)texW, 2.0f * heightM * densY / (float)texH);
    if (!std::isfinite(nearM) || !std::isfinite(farM)) return l;
    l.nearM = std::clamp(nearM, kKeyMinM, kKeyMaxM);
    l.farM = std::clamp(std::max(farM, nearM), kKeyMinM, kKeyMaxM);
    l.derived = true;
    return l;
}

// One step of the distance: the stick forward (y > 0) brings the panel nearer, back sends it farther, at speed m/s at full
// travel. The limits stop a push or a pull, never move the panel by themselves: a distance already past one (set in the
// ini, or a headset with other eyes) stays where it is until the stick takes it back towards the other.
inline float Stepped(float distance, float stickY, float speed, float dtS, const Limits& l) {
    if (!std::isfinite(stickY) || stickY == 0.0f || !(dtS > 0.0f) || !(speed > 0.0f)) return distance;
    const float y = std::clamp(stickY, -1.0f, 1.0f);
    const float next = distance - y * speed * std::min(dtS, kMaxStepS);
    if (next > distance) return std::min(next, std::max(distance, l.farM));
    return std::max(next, std::min(distance, l.nearM));
}

// What the frame is: the right hand's raw controls (before the laser takes any), and whether a game panel of ours is in front.
struct In {
    bool applies = false;   // [openxr] menu_zoom on, VR F4, the focus, a game panel hung in front by us last frame
    float grip = 0.0f;      // the right grip, 0 - 1
    float trigger = 0.0f;   // the right trigger, 0 - 1
    bool triggerClick = false;   // ... or its click, where the controller has one
    float stickY = 0.0f;    // the right stick, shaped (its dead zone already out): forward > 0
    float dtS = 0.0f;       // since the last step
    float distance = 0.0f;  // the panel's distance now, m
    float speed = kSpeedDefault;
    Limits limits;
};

struct Pre {
    float distance = 0.0f;      // where the panel stands now
    bool takeY = false;         // the stick's Y goes to nobody (the game, BodyWalk, the laser's "the pad navigates")
    bool takeTrigger = false;   // the trigger goes to nobody: squeezed while zooming, until it is let go
    bool zooming = false;       // the stick has moved the panel in this hold
    bool save = false;          // the hold is over and moved the panel: write `distance`
    float from = 0.0f;          // (save) where the hold took it from
};

struct Post {
    bool hideGrip = false;   // the grip's own press waits (it may be the start of a zoom)
    bool tapGrip = false;    // the grip's own press, now: it was let go without a zoom
};

// The grip held over a game panel, frame by frame:
//  Idle    - let go.
//  Held    - squeezed over a panel: the stick's Y is the zoom's (nothing yet moved). Its own press (the menu's right bumper, where
//            the laser did not take the grip) waits: let go without a zoom, it is a tap then.
//  Zooming - the stick moved the panel in this hold. Let go: the distance is saved, the grip's press never happens. A trigger
//            squeezed meanwhile is nobody's until it is let go (no click lands where the panel was moving).
//  Spent   - this hold is not the zoom's: squeezed where no panel was, or the trigger came first (the laser's grip + trigger =
//            right click, the pad's RB + RT): everything as it would be without the zoom, until the grip is let go.
class Grab {
public:
    enum class State { Idle, Held, Zooming, Spent };

    Pre StepPre(const In& in) {
        Pre r;
        r.distance = in.distance;
        const float g = std::isfinite(in.grip) ? in.grip : 0.0f, tv = std::isfinite(in.trigger) ? in.trigger : 0.0f;
        gripDown_ = g > (gripDown_ ? kGripOff : kGripOn);
        trigDown_ = in.triggerClick || tv > (trigDown_ ? kTriggerOff : kTriggerOn);
        const bool grip = gripDown_, trigger = trigDown_;
        if (!trigger) trigTaken_ = false;
        switch (state_) {
        case State::Idle:
            if (grip) {
                state_ = in.applies && !trigger ? State::Held : State::Spent;
                seenFree_ = false;
            }
            break;
        case State::Held:
            if (!grip) state_ = State::Idle;
            else if (!in.applies || trigger) state_ = State::Spent;
            else if (in.stickY != 0.0f && std::isfinite(in.stickY)) { state_ = State::Zooming; from_ = in.distance; }
            break;
        case State::Zooming:
            if (!grip || !in.applies) {   // let go, or the panel went (closed under the hand): what it moved is kept
                r.save = std::fabs(in.distance - from_) >= 0.5f * kSaveStepM;
                r.from = from_;
                state_ = grip ? State::Spent : State::Idle;
                restY_ = true;   // the stick's Y stays the zoom's until the stick is back at rest
                break;
            }
            if (trigger) trigTaken_ = true;
            break;
        case State::Spent:
            if (!grip) state_ = State::Idle;
            break;
        }
        if (state_ == State::Zooming) {
            r.distance = Stepped(in.distance, in.stickY, in.speed, in.dtS, in.limits);
            r.zooming = true;
        }
        if (restY_ && (in.stickY == 0.0f || state_ == State::Held || state_ == State::Zooming)) restY_ = state_ == State::Zooming;
        r.takeY = state_ == State::Held || state_ == State::Zooming || restY_;
        r.takeTrigger = trigTaken_;
        return r;
    }

    // gripFree: the grip still reaches the game after the laser (it did not take it). nowMs: a monotonic clock.
    Post StepPost(bool gripFree, uint64_t nowMs) {
        Post r;
        const bool waiting = state_ == State::Held || state_ == State::Zooming;
        if (waiting && gripFree) seenFree_ = true;
        if (waiting) r.hideGrip = gripFree;
        // the hold ended in Held (StepPre went Held -> Idle this frame): its own press as a tap
        if (lastState_ == State::Held && state_ == State::Idle && seenFree_) tapUntil_ = nowMs + kGripTapMs;
        if (state_ != State::Idle && state_ != State::Held) tapUntil_ = 0;
        lastState_ = state_;
        r.tapGrip = nowMs < tapUntil_;
        return r;
    }

    void Reset() { *this = Grab{}; }
    State state() const { return state_; }

private:
    State state_ = State::Idle, lastState_ = State::Idle;
    float from_ = 0.0f;
    bool gripDown_ = false, trigDown_ = false;
    bool trigTaken_ = false;
    bool restY_ = false;
    bool seenFree_ = false;
    uint64_t tapUntil_ = 0;
};

// The distance as the ini keeps it: whole centimetres.
inline float Saved(float m) { return std::round(m / kSaveStepM) * kSaveStepM; }

}  // namespace menuzoom
