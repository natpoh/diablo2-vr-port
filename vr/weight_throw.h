// weight_throw.h - a grip release that is a THROW ([weight] with [hands] throw_by_grip). Pure.
//
// A thrown weapon (a javelin, a throwing knife or axe, a potion) in the right hand is held by the weight rules like any weapon: it is held while the
// grip is squeezed, falls when the grip is let go. The one exception: "to throw, swing the hand with the grip squeezed and let it go at the end of
// the swing" - the release IS the throw, and the game's own throw (started by the throw gesture, vr/throw_gesture_wire.h) sends its own missile.
// What the weight model must NOT do for a throw is what it does for a drop: lie on the floor, or go to the back (after_drop). What it does do is
// let the weapon go at the release and let it leave the hand and fall with the motion it had (the user, 2026-10-14: "a thrown potion flew out of the
// hand as a model and fell"). BUT the game throws a missile of its own: our model flying on beside it is a second instance of the same weapon
// (2026-10-14, test-11). So the model's life is short: it ends the moment the game takes the throw - the hero goes into an action mode (attack,
// cast, throw: Diablo II's unit modes 7 8 10 11 13..16) - and a fresh weapon stands in the hand, drawn as the game draws it ([weight] thrown_model 0).
// The game may refuse the press (the skill's weapon check, nothing to aim at, no Throw skill on a pad slot): then the model's own flight is all there
// is, and it ends as any thrown one: when it lands, or after thrown_fly_ms. ([weight] thrown_model 1: it flies on physically after the game has
// taken the throw too, until it lands, the game's animation is over, or thrown_fly_ms - as in the build before.)
//
// The throw gesture says two things (registered with weight::SetThrowSource, vr/weight.h): that it owns the right grip now (a thrown weapon wielded
// and throw_by_grip on) and when the last throw was released (GetTickCount64).
//  - ThrowRelease watches the grip of a held weapon of an owned grip. At the release the weapon is let go AT ONCE (the weight model does that, from
//    the grip itself); the release waits kGraceMs for the throw gesture's word (it may decide a frame or two after the grip opened): a throw within
//    that wait after the release, or at most kBeforeMs before it, means "thrown"; none by the end of the wait, the release was a plain drop, which
//    the weight model has been doing all along (so nothing is undone). Pending tells the weight model "a verdict is outstanding": it must not
//    put the landed weapon on the back yet.
//  - ThrownFly ends a thrown weapon's visit. It is fed the hero's mode on every step (Note), also before the verdict is in: the game may take the
//    press before the throw gesture has said "thrown" (the verdict can come 150 ms after the release).
#pragma once

#include <cstdint>

namespace weight {

class ThrowRelease {
public:
    static constexpr uint64_t kGraceMs = 150;   // how long a release waits for the throw gesture's word
    static constexpr uint64_t kBeforeMs = 120;  // a throw up to this long BEFORE the release belongs to it (the same frame's, in effect)

    struct In {
        bool owned = false;         // the throw gesture owns this grip now, and the weapon is a thrown kind
        bool holds = false;         // the weapon is held by this hand (the last step's word)
        float grip = -1.0f;         // the real grip, 0..1; < 0 not known
        uint64_t now = 0;           // GetTickCount64
        uint64_t lastThrow = 0;     // the throw gesture's last released throw (GetTickCount64), 0 none
    };
    enum class Act {
        None,       // nothing special
        Pending,    // a held weapon's grip was released lately and the throw gesture has not said yet: a throw or a drop
        Thrown,     // it was a throw (once)
    };

    Act Step(const In& in) {
        justReleased_ = false;
        if (!in.owned) {   // nothing to watch (and nothing waits any more)
            Reset();
            down_ = in.grip >= 0.0f && in.grip > 0.6f;
            return Act::None;
        }
        const bool wasDown = down_;
        if (in.grip > 0.6f) down_ = true;
        else if (in.grip >= 0.0f && in.grip < 0.4f) down_ = false;
        if (!waiting_) {
            if (!in.holds || !(wasDown && !down_)) return Act::None;   // not a release of a held weapon
            waiting_ = true;
            releasedAt_ = in.now;
            justReleased_ = true;
        }
        // (a release under way is followed whether or not the weapon is still held: it is let go at the release, and the verdict comes after)
        if (Near(in.lastThrow)) {   // a throw belongs to this release
            lastReleasedAt_ = releasedAt_;
            Reset();
            return Act::Thrown;
        }
        if (in.now - releasedAt_ >= kGraceMs) {   // none came: a drop
            Reset();
            return Act::None;
        }
        return Act::Pending;
    }

    // Forget a release under way (the weapon changed, the gesture let go, the view changed...).
    void Reset() { waiting_ = false; releasedAt_ = 0; }
    bool Waiting() const { return waiting_; }
    // This Step saw the release (the first of its answers: Pending, or Thrown at once). For the moment the weapon's velocity is taken.
    bool JustReleased() const { return justReleased_; }
    // The release a Pending answer waits on (GetTickCount64).
    uint64_t WaitingSince() const { return releasedAt_; }
    // The release (GetTickCount64) the last Thrown answer belongs to.
    uint64_t ReleasedAt() const { return lastReleasedAt_; }

private:
    bool Near(uint64_t lastThrow) const {
        if (!lastThrow) return false;
        return lastThrow + kBeforeMs >= releasedAt_ && lastThrow <= releasedAt_ + kGraceMs;
    }

    bool down_ = false;
    bool waiting_ = false;
    bool justReleased_ = false;
    uint64_t releasedAt_ = 0, lastReleasedAt_ = 0;
};

// A thrown weapon's model flies and falls; this says when it has done. Whichever is first (why: End):
//   Taken     - the game took the throw: the hero went into an action mode (attack, cast, throw: Diablo II's unit modes) no earlier than kSlackMs before the
//               release - NOT an older animation still playing at the release (that one must end first, then a new one begin). [weight] thrown_model 0.
//   Landed    - the model lies on the ground.
//   Animation - thrown_model 1 only: the game's animation of this throw is over (the hero has been in an action mode since the release and is not any more).
//   Timeout   - thrown_fly_ms from the release.
// A throwing POTION ([weight] potion_model_flies) is the exception to all of it: the model is THE potion the player sees (the game's own missile is hidden,
// vr/potion_hide.h), so the game taking the throw ends nothing - it flies the whole thrown trajectory and ends at its landing (a landed potion breaks: it is gone
// and the next one is in the hand), or at Timeout. Neither the handoff nor the game's animation ending is its end (fliesToLanding).
// The hero's mode is Note'd on every step, also before Start: a press the game took before the throw gesture said "thrown" is already in it.
class ThrownFly {
public:
    // Taken: the game took the throw and the handoff is 0 - the model is gone at once. HandoffAction / HandoffTime: the game took the throw, and the model, which
    // has flown out of the hand along the throw, hands over to the game's own missile: at the game's action frame (its missile is up) or after the handoff
    // time from the release, whichever comes first. Landed / Timeout: as ever (after a take they hand over as well: see Taken()).
    enum class End { None, Taken, HandoffAction, HandoffTime, Landed, Animation, Timeout };
    static constexpr uint64_t kSlackMs = 120;   // an action that began up to this long before the release is this throw's (clock jitter)
    static bool ActionMode(int mode) { return mode == 7 || mode == 8 || mode == 10 || mode == 11 || (mode >= 13 && mode <= 16); }

    // The hero's mode now (Diablo II's unit mode), < 0 not known (nothing changes).
    void Note(uint64_t now, int mode) {
        if (mode < 0) return;
        if (ActionMode(mode)) {
            if (!inAction_) { inAction_ = true; actionSince_ = now; }
        } else {
            inAction_ = false;
        }
    }
    void Start(uint64_t releasedAt) {
        active_ = true;
        releasedAt_ = releasedAt;
        wasThrowAnim_ = false;
        takenAt_ = 0;
    }
    void Reset() { active_ = false; wasThrowAnim_ = false; }
    bool Active() const { return active_; }
    // The game took this throw (seen since Start; kept after the end, until the next Start): the model's end then hands over to the game's missile and the hand stays empty.
    bool Taken() const { return takenAt_ != 0; }
    // landed: the model lies on the ground. vanishOnTaken: [weight] thrown_model 0. flyMs: [weight] thrown_fly_ms. handoffMs: [weight] thrown_handoff_ms (0: vanish
    // at the moment the game takes the throw); actionFrame: the game's animation has passed its action frame - its own missile for this throw is up (not
    // known: false - the fixed handoff time then). fliesToLanding: a thrown potion's model ([weight] potion_model_flies): neither the game's take, nor its handoff,
    // nor its animation ending ends it - only Landed and Timeout.
    End Step(uint64_t now, bool landed, bool vanishOnTaken, uint64_t flyMs, uint64_t handoffMs = 0, bool actionFrame = false, bool fliesToLanding = false) {
        if (!active_) return End::None;
        const bool theGames = inAction_ && actionSince_ + kSlackMs >= releasedAt_;   // an action of this throw's, begun after the release (give or take the slack)
        if (theGames) {
            wasThrowAnim_ = true;
            if (!takenAt_) takenAt_ = now > 0 ? now : 1;
        }
        End e = End::None;
        if (vanishOnTaken && takenAt_ && !fliesToLanding) {
            if (handoffMs == 0) e = End::Taken;
            else if (actionFrame) e = End::HandoffAction;
            else if (now >= releasedAt_ && now - releasedAt_ >= handoffMs) e = End::HandoffTime;
        }
        if (e == End::None) {
            if (landed) e = End::Landed;
            else if (!vanishOnTaken && !fliesToLanding && wasThrowAnim_ && !inAction_) e = End::Animation;
            else if (now >= releasedAt_ && now - releasedAt_ >= flyMs) e = End::Timeout;
        }
        if (e != End::None) active_ = false;
        return e;
    }
    // The model is gone because the game's missile took over (or at once: Taken) - and so the hand stays empty - or because it just landed / ran out after a take.
    static bool IsHandoff(End e) { return e == End::Taken || e == End::HandoffAction || e == End::HandoffTime; }
    static const char* Why(End e) {
        switch (e) {
        case End::Taken: return "the game took the throw - its own missile flies";
        case End::HandoffAction: return "action frame: the game's own missile is up";
        case End::HandoffTime: return "the handoff time";
        case End::Landed: return "it landed";
        case End::Animation: return "the game's throw animation is over";
        case End::Timeout: return "thrown_fly_ms ran out";
        default: return "";
        }
    }

private:
    bool active_ = false, wasThrowAnim_ = false, inAction_ = false;
    uint64_t releasedAt_ = 0, actionSince_ = 0, takenAt_ = 0;
};

// The hand stays EMPTY while the game throws. When the game takes a throw (ThrownFly::End::Taken) our model is gone at once - but a fresh weapon put in the
// hand right then shows the player the spear still in his hand while the game's own missile flies (2026-10-14, test-14: "I swung the hand with the spear, it
// flew, and it stayed in my hand"). So the fresh one is there for the weight model (it can be taken, thrown again) but NOT DRAWN - the attach bone is squeezed
// to a point - for the whole of the game's throw animation: from the moment the game took the throw until the hero's animation mode has left the action
// modes (attack, cast, throw, skills) for kTailMs, then the game's own next one is in the hand (it redraws it itself) and ours is drawn again. A cap
// ([weight] thrown_hand_empty_ms) ends it whatever the mode says: a lost mode signal can never leave the hand empty. Cap 0: not hidden at all.
class EmptyHand {
public:
    enum class End { None, AnimationOver, Cap };
    static constexpr uint64_t kTailMs = 120;   // the hero out of the action modes this long: the animation is over
    void Start(uint64_t now) {
        active_ = true;
        startedAt_ = now;
        overAt_ = 0;
        sawAction_ = true;   // (the game took the throw: the hero was in an action mode)
    }
    void Reset() { active_ = false; overAt_ = 0; }
    bool Active() const { return active_; }
    uint64_t StartedAt() const { return startedAt_; }
    // Every step while active: the hero's mode now (< 0 not known: it says nothing), the cap in ms.
    End Step(uint64_t now, int heroMode, uint64_t capMs) {
        if (!active_) return End::None;
        End e = End::None;
        if (capMs == 0 || (now >= startedAt_ && now - startedAt_ >= capMs)) {
            e = End::Cap;
        } else if (heroMode >= 0) {
            if (ThrownFly::ActionMode(heroMode)) {
                sawAction_ = true;
                overAt_ = 0;
            } else if (sawAction_) {
                if (overAt_ == 0) overAt_ = now > 0 ? now : 1;
                if (now >= overAt_ && now - overAt_ >= kTailMs) e = End::AnimationOver;
            }
        }
        if (e != End::None) active_ = false;
        return e;
    }
    static const char* Why(End e) {
        switch (e) {
        case End::AnimationOver: return "the game's throw animation is over";
        case End::Cap: return "thrown_hand_empty_ms ran out";
        default: return "";
        }
    }

private:
    bool active_ = false, sawAction_ = false;
    uint64_t startedAt_ = 0, overAt_ = 0;
};

}  // namespace weight
