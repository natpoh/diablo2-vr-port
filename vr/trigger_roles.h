#pragma once
// trigger_roles.h - what each trigger is for while the gestures are in use (Serg, 2026-11-08: "if a trigger is given to a glyph it works only for the
// glyph; if not, it can be bound or set as Shift"). Per hand, [gestures] trigger_right / trigger_left:
//   AsBefore  as it always was (the left the game's Shift, the right its skill slot / the attack): nothing here touches it
//   Glyphs    it draws the strokes and casts the skill a Select glyph armed on THIS hand - nothing else reaches the game from it
//   Shift     the game's Shift (its second layer / stand still): pad LT held while the trigger is
//   Action    an action of the gestures' list (vr/gesture_actions.h) held while the trigger is, or done once at the press (an item)
// A trigger with a role other than AsBefore is the role's alone: the layout never sees it.
//
// A Glyphs trigger is both a pen and a cast control. Two rules tell them apart ([gestures] glyph_cast_hold):
//   Release (0, the default - Serg: "the strike only at the trigger's release"; "held and drawing: a glyph; held and not drawing: the strike's trajectory
//            appears so I choose where to send it; a plain strike shows nothing and fires after the release"): the press starts as a pen stroke and
//            nothing goes to the game. The hand moving more than kStrokeM within kDecideS makes it a STROKE: read by the engine at the release, a glyph
//            if it matched one - never a cast. The hand still at kDecideS makes it AIM: the stroke is cut (far too short to match), and with a skill
//            armed - one cast onto a point (`armedPoint`: vr/point_cast.h's list) has its slot held from then on, so point_cast's own arc and circle
//            show and its release casts there; any other is cast once at the release, along the hand (`aiming` out: the host draws the aim line for a
//            skill that flies, steers the cast's point along this hand). A quick still tap (let go before kDecideS) casts once at its release.
//            Cost: no hold-to-repeat on a glyph trigger (repeat by tapping); the cast lands at the release, not the press.
//   Hold (1): by the hand's motion at the press - moving more than kStrokeM within kDecideS makes it a stroke (never a cast); still at kDecideS with a
//            skill armed makes it a cast, the stroke cut (far too short to match) and the armed slot held for as long as the trigger is (the game repeats
//            it); a quick still tap casts once at its release. Cost: up to kDecideS of delay on a cast; no stray cast ever comes of a stroke.
//
// The armed slots (wheel_types.h): the right hand's skill on RT (9), held as pad RT; the left hand's on LT + RT (11), the game's own second layer -
// pad LT first, RT kLeadS later (as vr/slot_controls.h presses the second layer), held while the trigger is.
// Pure: no Windows, no game (cleanroom/tests/gesture_overlay_test.cpp).

#include <cmath>
#include <cstdint>

namespace trigroles {

enum class Role : uint8_t { AsBefore = 0, Glyphs = 1, Shift = 2, Action = 3 };
inline Role RoleOf(int v) { return v == 1 ? Role::Glyphs : v == 2 ? Role::Shift : v == 3 ? Role::Action : Role::AsBefore; }

constexpr float kOn = 0.55f, kOff = 0.35f;   // the trigger pressed / let go (hysteresis)
constexpr float kStrokeM = 0.06f;            // moved this far: a stroke
constexpr double kDecideS = 0.12;            // still this long: a cast
constexpr double kLeadS = 0.03;              // the second layer's LT ahead of RT (about one game tick)
constexpr double kReleaseWaitS = 0.08;       // Release: the engine's answer to the stroke waited for this long after the release
enum class CastRule : uint8_t { Release = 0, Hold = 1 };
constexpr int kSlotRight = 9, kSlotLeft = 11;   // RT; LT + RT
constexpr uint16_t kPadLTBit = 0, kPadRTBit = 0;   // (the triggers are analog: lt / rt below)

inline int ArmSlot(int hand) { return hand == 1 ? kSlotRight : kSlotLeft; }   // hand 0 left, 1 right

enum class Press : uint8_t { None, Deciding, Stroke, Cast, Released };   // (Released: the Release rule waiting for the engine's answer)

struct In {
    double time = 0;
    bool allowed = false;      // F4, direct input, in play, no panel or game menu up: the roles act (else every trigger is as before)
    float trigger = 0;         // 0..1
    float pos[3] = {0, 0, 0};  // the hand (metres)
    bool posOk = false;
    bool armed = false;        // a Select glyph armed this hand's slot
    CastRule rule = CastRule::Release;
    bool armedPoint = false;   // ... and it is a skill cast onto a point (point_cast: its slot held while aiming, its own arc and circle)
    double recognisedAt = -1;  // when the engine last recognised a gesture (the same clock as `time`; < 0 never)
};

struct Out {
    bool take = false;         // the trigger is taken from the layout this frame
    bool pen = false;          // the engine's pen for this hand (Glyphs: until the press is decided a cast)
    bool castHold = false;     // the armed slot's pad buttons held this frame
    bool castTap = false;      // ... or one tap now (a quick still press, let go)
    bool lt = false, rt = false;   // the pad's triggers for castHold / castTap / Shift
    bool actionDown = false, actionUp = false;   // Action: the press began / ended (edges)
    bool actionHeld = false;   // Action: down now
    bool aiming = false;       // Release: held still with a skill armed - its aim line drawn, the cast's point along this hand
    bool aimEnd = false;       // ... the aim just ended (the cast's point keeps this hand a moment after: the host's)
};

// One hand's trigger.
class Hand {
    Press press_ = Press::None;
    bool down_ = false;
    double t0_ = 0, holdSince_ = 0;
    float p0_[3] = {0, 0, 0};
    bool p0Ok_ = false;
    bool armedAtPress_ = false;
    bool pointAtPress_ = false;

public:
    Press State() const { return press_; }

    Out Step(Role role, int hand, const In& in) {
        Out o;
        const bool was = down_;
        down_ = in.trigger > (down_ ? kOff : kOn);
        if (!in.allowed || role == Role::AsBefore) {   // as before: nothing taken, any press forgotten
            press_ = Press::None;
            return o;
        }
        o.take = true;
        const bool edgeDown = down_ && !was, edgeUp = !down_ && was;
        switch (role) {
        case Role::Shift:
            o.lt = down_;
            return o;
        case Role::Action:
            o.actionDown = edgeDown;
            o.actionUp = edgeUp;
            o.actionHeld = down_;
            return o;
        default: break;
        }
        if (in.rule == CastRule::Release) {   // drawing: a glyph at the release; held still: aim, cast at the release
            if (edgeDown) {
                press_ = Press::Deciding;
                t0_ = in.time;
                p0Ok_ = in.posOk;
                for (int k = 0; k < 3; ++k) p0_[k] = in.pos[k];
                armedAtPress_ = in.armed;
                pointAtPress_ = in.armed && in.armedPoint;
            }
            if (press_ == Press::Deciding && down_) {
                float moved = 0.0f;
                if (p0Ok_ && in.posOk) {
                    const float dx = in.pos[0] - p0_[0], dy = in.pos[1] - p0_[1], dz = in.pos[2] - p0_[2];
                    moved = std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                if (moved > kStrokeM) press_ = Press::Stroke;
                else if (in.time - t0_ >= kDecideS) {
                    press_ = Press::Cast;   // (aiming: the stroke cut)
                    holdSince_ = in.time;
                }
            }
            if (edgeUp) {
                const Press was = press_;
                press_ = Press::None;
                if (was == Press::Cast) o.aimEnd = true;
                if ((was == Press::Deciding || was == Press::Cast) && armedAtPress_ && !(was == Press::Cast && pointAtPress_)) {   // a tap, or the aim let go
                    o.castTap = true;   // (a point skill aimed: its held slot let go is point_cast's cast - nothing more)
                    o.lt = hand == 0;
                    o.rt = true;
                }
                return o;
            }
            if (!down_) return o;
            o.pen = press_ == Press::Deciding || press_ == Press::Stroke;
            if (press_ == Press::Cast && armedAtPress_) {
                o.aiming = true;
                if (pointAtPress_) {   // its slot held: point_cast's arc and circle, its own cast at the let-go
                    o.castHold = true;
                    o.lt = hand == 0;
                    o.rt = hand == 1 || in.time - holdSince_ >= kLeadS;
                }
            }
            return o;
        }
        // Hold: a pen, or a cast of the armed skill - by the motion at the press
        if (edgeDown) {
            press_ = Press::Deciding;
            t0_ = in.time;
            p0Ok_ = in.posOk;
            for (int k = 0; k < 3; ++k) p0_[k] = in.pos[k];
            armedAtPress_ = in.armed;
        }
        if (press_ == Press::Deciding && down_) {
            float moved = 0.0f;
            if (p0Ok_ && in.posOk) {
                const float dx = in.pos[0] - p0_[0], dy = in.pos[1] - p0_[1], dz = in.pos[2] - p0_[2];
                moved = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            if (moved > kStrokeM) press_ = Press::Stroke;
            else if (in.time - t0_ >= kDecideS && armedAtPress_) {
                press_ = Press::Cast;
                holdSince_ = in.time;
            }
        }
        if (edgeUp) {
            if (press_ == Press::Deciding && armedAtPress_) {   // a quick still tap: one cast
                o.castTap = true;
                o.lt = hand == 0;
                o.rt = true;
            }
            press_ = Press::None;
            return o;
        }
        if (!down_) return o;
        if (press_ == Press::Deciding || press_ == Press::Stroke) o.pen = true;
        if (press_ == Press::Cast) {
            o.castHold = true;
            if (hand == 0) {   // the left: the second layer - LT first, RT a tick later
                o.lt = true;
                o.rt = in.time - holdSince_ >= kLeadS;
            } else {
                o.rt = true;
            }
        }
        return o;
    }
};

}  // namespace trigroles
