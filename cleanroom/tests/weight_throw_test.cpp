// Offline checks of "a grip release that is a throw" (vr/weight_throw.h): synthetic grip timelines against a throw gesture's word, and how a thrown
// weapon's model flies and ends. No game, no headset. Run: build/Release/weight_throw_test.exe   (exit code 0 = all passed)

#include "../../vr/weight_throw.h"

#include <cstdio>
#include <initializer_list>

using weight::ThrowRelease;
using weight::ThrownFly;
using Act = ThrowRelease::Act;
using End = ThrownFly::End;

static int g_failures = 0;

static void Expect(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

// One step: the grip, the time, whether the throw gesture owns the grip, whether the weapon is held, and its last throw.
static Act Step(ThrowRelease& t, float grip, uint64_t now, uint64_t lastThrow, bool owned = true, bool holds = true) {
    ThrowRelease::In in;
    in.owned = owned;
    in.holds = holds;
    in.grip = grip;
    in.now = now;
    in.lastThrow = lastThrow;
    return t.Step(in);
}

int main() {
    // not the throw gesture's grip: a release is a drop, nothing waits
    {
        ThrowRelease t;
        Expect(Step(t, 1.0f, 1000, 0, false) == Act::None, "not owned: held grip");
        Expect(Step(t, 0.0f, 1020, 0, false) == Act::None, "not owned: the release is a plain release");
        Expect(!t.Waiting(), "not owned: nothing waits");
    }
    // a release with no throw: pending for the grace (the weapon is let go all the while - it is not held after the release), then a plain drop
    {
        ThrowRelease t;
        for (uint64_t now = 1000; now < 1100; now += 10) Expect(Step(t, 1.0f, now, 0) == Act::None, "squeezed: nothing special");
        Expect(Step(t, 0.0f, 1100, 0) == Act::Pending, "released: the verdict is outstanding");
        Expect(Step(t, 0.0f, 1130, 0, true, false) == Act::Pending, "released, no longer held (let go at the release): still pending at 30 ms");
        Expect(Step(t, 0.0f, 1249, 0, true, false) == Act::Pending, "still pending at 149 ms");
        Expect(Step(t, 0.0f, 1250, 0, true, false) == Act::None, "no throw in 150 ms - a drop (which has been happening all along)");
        Expect(!t.Waiting(), "dropped: no wait left");
        Expect(Step(t, 0.0f, 1260, 0, true, false) == Act::None, "after the drop: quiet");
    }
    // the throw is decided in the same frame as the release
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 1100) == Act::Thrown, "a throw at the release: thrown at once");
        Expect(!t.Waiting(), "thrown: no wait left");
        Expect(t.ReleasedAt() == 1100, "thrown: the release it belongs to");
    }
    // the throw is decided a few frames after the release; the weapon is already let go by then
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 0) == Act::Pending, "late throw: pending");
        Expect(Step(t, 0.0f, 1133, 0, true, false) == Act::Pending, "late throw: still pending, the weapon already falling");
        Expect(Step(t, 0.0f, 1160, 1150, true, false) == Act::Thrown, "late throw: thrown when it comes (the weapon is not held)");
        Expect(t.ReleasedAt() == 1100, "late throw: it belongs to the release at 1100");
    }
    // a throw a little before the release belongs to it; an old one does not
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 1000) == Act::Thrown, "a throw 100 ms before the release: thrown");
    }
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1200, 1000) == Act::Pending, "a throw 200 ms before the release: not this one's");
    }
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 1050) == Act::Thrown, "a throw 50 ms before the release: thrown");
    }
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 900);   // the last throw was before this squeeze
        Expect(Step(t, 0.0f, 1500, 900) == Act::Pending, "an old throw: not this release's");
        Expect(Step(t, 0.0f, 1650, 900, true, false) == Act::None, "an old throw: the release is a drop");
    }
    // a throw that comes after the grace is too late
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Step(t, 0.0f, 1100, 0);
        Expect(Step(t, 0.0f, 1250, 0, true, false) == Act::None, "grace over: a drop");
        Expect(Step(t, 0.0f, 1260, 1255, true, false) == Act::None, "too late: nothing happens");
    }
    // hysteresis: a grip between 0.4 and 0.6 is neither pressed nor released
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.5f, 1010, 0) == Act::None, "0.5: not a release");
        Expect(Step(t, 0.45f, 1020, 0) == Act::None, "0.45: not a release");
        Expect(Step(t, 0.35f, 1030, 0) == Act::Pending, "0.35: a release");
    }
    // the gesture lets go of the grip while a release waits: it was a plain release
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 0) == Act::Pending, "waits");
        Expect(Step(t, 0.0f, 1110, 1105, false) == Act::None, "ownership lost: no throw counts, nothing waits");
        Expect(!t.Waiting(), "ownership lost: no wait");
    }
    // not holding it (it lies on the floor, or flies): a squeeze and a release do nothing here
    {
        ThrowRelease t;
        Expect(Step(t, 1.0f, 1000, 0, true, false) == Act::None, "not held: squeeze");
        Expect(Step(t, 0.0f, 1100, 0, true, false) == Act::None, "not held: release");
        Expect(!t.Waiting(), "not held: nothing waits");
    }
    // after a throw the next squeeze and release is a new cycle
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, 0.0f, 1100, 1100) == Act::Thrown, "first throw");
        Expect(Step(t, 0.0f, 1110, 1100, true, false) == Act::None, "the model flies: quiet");
        Expect(Step(t, 0.0f, 1120, 1100, true, true) == Act::None, "a fresh one in the hand, grip open: not a release (it sits there without the grip)");
        Step(t, 1.0f, 1500, 1100);
        Expect(Step(t, 0.0f, 1700, 1100) == Act::Pending, "second cycle: a release is pending again");
        Expect(Step(t, 0.0f, 1710, 1705, true, false) == Act::Thrown, "second throw");
    }
    // an unknown grip (< 0) is neither
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Expect(Step(t, -1.0f, 1010, 0) == Act::None, "unknown grip: nothing");
    }
    // Reset forgets a wait
    {
        ThrowRelease t;
        Step(t, 1.0f, 1000, 0);
        Step(t, 0.0f, 1100, 0);
        Expect(t.Waiting(), "waiting");
        t.Reset();
        Expect(!t.Waiting(), "reset");
    }


    // ---- how a thrown model ends. Taken: the game took the throw (the hero went into an action mode no earlier than the release); landed; the game's
    // animation over (thrown_model 1); thrown_fly_ms. Whichever is first. The hero's mode is Note'd on every step, also before the verdict (Start). ----
    const bool kVanish = true, kFlyOn = false;
    {   // THE rule: the game takes the throw (mode 11 after the release): the model is gone at once - no landing, no timeout, no flight on
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1010, 1);
        Expect(f.Step(1010, false, kVanish, 900) == End::None, "neutral right after the release: still flying");
        f.Note(1050, 1);
        Expect(f.Step(1050, false, kVanish, 900) == End::None, "still neutral at 50 ms: the game has not answered");
        f.Note(1080, 11);   // the hero goes into the throw
        Expect(f.Step(1080, false, kVanish, 900) == End::Taken, "the hero went into the throw: the model vanishes at once");
        Expect(!f.Active() && f.Step(1090, false, kVanish, 900) == End::None, "gone: nothing more");
    }
    {   // every action mode counts: attack 1 and 2, cast, throw, skills
        for (const int m : {7, 8, 10, 11, 13, 14, 15, 16}) {
            ThrownFly f;
            f.Note(900, 1);
            f.Start(1000);
            f.Note(1060, m);
            Expect(f.Step(1060, false, kVanish, 900) == End::Taken, "an action mode takes it");
        }
        for (const int m : {0, 1, 2, 3, 4, 5, 6, 9, 12, 17, 18}) {
            ThrownFly f;
            f.Note(900, 1);
            f.Start(1000);
            f.Note(1060, m);
            Expect(f.Step(1060, false, kVanish, 900) != End::Taken, "walking, being hit, blocking, dying... is not the game taking the throw");
        }
    }
    {   // late: the throw gesture's verdict (Start) comes AFTER the game has taken the press - the same step ends it
        ThrownFly f;
        f.Note(1000, 1);
        f.Note(1040, 11);   // the press was taken 40 ms after the release at 1000
        f.Start(1000);   // the gesture says "thrown" 100 ms after the release
        Expect(f.Step(1100, false, kVanish, 900) == End::Taken, "a late verdict, the game already in the throw: gone at once");
    }
    {   // not taken: the hero never leaves neutral - the model's own flight is all there is, and it ends as before (landing, or the cap)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        for (uint64_t t = 1000; t < 1500; t += 50) {
            f.Note(t, 1);
            Expect(f.Step(t, false, kVanish, 900) == End::None, "not taken: it flies on");
        }
        f.Note(1500, 1);
        Expect(f.Step(1500, true, kVanish, 900) == End::Landed, "not taken: it landed");
    }
    {
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1899, 1);
        Expect(f.Step(1899, false, kVanish, 900) == End::None, "not taken: 899 ms, still flying");
        f.Note(1900, 1);
        Expect(f.Step(1900, false, kVanish, 900) == End::Timeout, "not taken: thrown_fly_ms is up");
    }
    {   // thrown_fly_ms is the cap, whatever it is set to
        ThrownFly f;
        f.Note(4900, 1);
        f.Start(5000);
        f.Note(5300, 1);
        Expect(f.Step(5300, false, kVanish, 300) == End::Timeout, "thrown_fly_ms 300: over at 300 ms");
    }
    {   // an older animation still playing at the release is NOT the game taking this throw; it must end first, and a new one begin
        ThrownFly f;
        f.Note(500, 11);   // the previous throw's animation
        f.Start(1000);
        f.Note(1010, 11);
        Expect(f.Step(1010, false, kVanish, 900) == End::None, "the old animation is still on: not this throw's");
        f.Note(1100, 1);
        Expect(f.Step(1100, false, kVanish, 900) == End::None, "it ended: the game has not taken this one");
        f.Note(1200, 11);   // the new press taken
        Expect(f.Step(1200, false, kVanish, 900) == End::Taken, "a new animation after the release: this throw's, taken");
    }
    {   // an action that began a moment BEFORE the release (clock jitter, the press went out with the release) is this throw's
        ThrownFly f;
        f.Note(1000, 1);
        f.Note(950, 11);   // (out of order by 50 ms - the slack covers it)
        f.Start(1000);
        Expect(f.Step(1001, false, kVanish, 900) == End::Taken, "an action begun 50 ms before the release counts");
        ThrownFly g;
        g.Note(700, 11);
        g.Start(1000);
        Expect(g.Step(1001, false, kVanish, 900) == End::None, "an action begun 300 ms before the release does not");
    }
    {   // thrown_model 1: the model flies on after the game has taken the throw, until it lands...
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1050, 11);
        Expect(f.Step(1050, false, kFlyOn, 900) == End::None, "flies on: the game took the throw, the model stays");
        f.Note(1300, 11);
        Expect(f.Step(1300, false, kFlyOn, 900) == End::None, "flies on: still in the throw animation");
        f.Note(1500, 11);
        Expect(f.Step(1500, true, kFlyOn, 900) == End::Landed, "flies on: it landed");
    }
    {   // ... or the game's animation is over
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1050, 11);
        f.Step(1050, false, kFlyOn, 900);
        f.Note(1450, 1);
        Expect(f.Step(1450, false, kFlyOn, 900) == End::Animation, "flies on: back to neutral - the animation is over, the next one is in the hand");
    }
    {   // ... and with thrown_model 1 an old animation ending is not "this throw's over"
        ThrownFly f;
        f.Note(500, 11);
        f.Start(1000);
        f.Note(1100, 1);
        Expect(f.Step(1100, false, kFlyOn, 900) == End::None, "flies on: an older animation ended - not this throw's");
    }
    {   // the mode is not known (no hero unit yet): nothing is taken or over; the model ends by landing or the cap
        ThrownFly f;
        f.Note(900, -1);
        f.Start(1000);
        f.Note(1100, -1);
        Expect(f.Step(1100, false, kVanish, 900) == End::None, "mode unknown: nothing");
        f.Note(1950, -1);
        Expect(f.Step(1950, false, kVanish, 900) == End::Timeout, "mode unknown: the cap ends it");
    }
    {   // landed and taken at the same step: the game's taking wins (one answer; the model must not be a second weapon)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1400, 11);
        Expect(f.Step(1400, true, kVanish, 900) == End::Taken, "taken wins a tie with landing");
    }
    {   // not started: nothing; Reset
        ThrownFly f;
        f.Note(1000, 11);
        Expect(f.Step(5000, true, kVanish, 900) == End::None, "not started: nothing");
        f.Start(1000);
        f.Reset();
        Expect(!f.Active() && f.Step(5000, true, kVanish, 900) == End::None, "reset: nothing");
    }
    {   // a clock that goes back is not a timeout
        ThrownFly f;
        f.Note(1000, 1);
        f.Start(2000);
        f.Note(1500, 1);
        Expect(f.Step(1500, false, kVanish, 900) == End::None, "the clock went back: no timeout");
    }
    // ---- the handoff: the model flies out of the hand along the throw, THEN hands over to the game's missile (the action frame, or the handoff time) ----
    {   // taken at 60 ms, handoff 350: it flies until 350 ms after the release
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1060, false, kVanish, 3000, 350) == End::None && f.Taken(), "taken at 60 ms: the model flies on (handoff 350)");
        Expect(f.Step(1200, false, kVanish, 3000, 350) == End::None, "still flying at 200 ms");
        Expect(f.Step(1349, false, kVanish, 3000, 350) == End::None, "still flying at 349 ms");
        Expect(f.Step(1350, false, kVanish, 3000, 350) == End::HandoffTime && !f.Active(), "350 ms after the release: handed over to the game's missile");
        Expect(ThrownFly::IsHandoff(End::HandoffTime) && ThrownFly::IsHandoff(End::HandoffAction) && ThrownFly::IsHandoff(End::Taken) && !ThrownFly::IsHandoff(End::Landed),
               "the handoff ends are the ones that empty the hand");
    }
    {   // the game's action frame (its missile is up) before the time: handed over there
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1100, false, kVanish, 3000, 350, false) == End::None, "no action frame yet");
        Expect(f.Step(1180, false, kVanish, 3000, 350, true) == End::HandoffAction, "the action frame at 180 ms: handed over there");
    }
    {   // an action frame that comes before the game has taken the throw is nothing (the game must have taken it: its missile exists)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        Expect(f.Step(1100, false, kVanish, 3000, 350, true) == End::None && !f.Taken(), "action frame without a take: no handoff");
    }
    {   // handoff 0: at the moment the game takes it (as before)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1060, false, kVanish, 3000, 0) == End::Taken, "handoff 0: vanishes at once");
    }
    {   // taken late (500 ms), handoff 350: nothing left to wait for - handed over at the take
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1500, 11);
        Expect(f.Step(1500, false, kVanish, 3000, 350) == End::HandoffTime, "taken after the handoff time: handed over at once");
    }
    {   // it lands before the handoff, after the take: Landed, and the take is remembered (the hand is emptied all the same)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1060, false, kVanish, 3000, 800) == End::None, "taken, handoff 800");
        Expect(f.Step(1500, true, kVanish, 3000, 800) == End::Landed && f.Taken(), "landed at 500 ms: Landed, and the game had taken it");
    }
    {   // not taken: no handoff at all, the flight is all there is
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1100, 1);
        Expect(f.Step(1400, false, kVanish, 3000, 350) == End::None && !f.Taken(), "not taken: it flies on past the handoff time");
        Expect(f.Step(1600, true, kVanish, 3000, 350) == End::Landed && !f.Taken(), "not taken: landed, not taken");
    }
    {   // a cap shorter than the handoff: Timeout (and the take is remembered)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1300, false, kVanish, 300, 900) == End::Timeout && f.Taken(), "thrown_fly_ms before the handoff time");
    }
    {   // thrown_model 1: no handoff, whatever the numbers
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1400, false, kFlyOn, 3000, 350, true) == End::None, "thrown_model 1: it flies on, handoff or not");
    }
    // ---- a throwing POTION's model flies to its landing (potion_model_flies): the game taking the throw, the handoff and the animation ending end nothing ----
    {   // taken at 60 ms with handoff 0 (the javelins' rule: vanish at once) - a potion flies on
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1060, false, kVanish, 3000, 0, false, true) == End::None && f.Active(), "a potion: the game took the throw, it flies on (no Taken)");
        Expect(f.Taken(), "...the take is remembered (the hand stays empty after the landing)");
        Expect(f.Step(1100, false, kVanish, 3000, 0, true, true) == End::None, "the game's missile being up (action frame) does not take it either");
    }
    {   // handoff 350: still no handoff, at the time or at the action frame
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1500, false, kVanish, 3000, 350, true, true) == End::None, "a potion: neither the handoff time nor the action frame ends it");
    }
    {   // the whole flight: taken, the animation over, then it lands: Landed (and the take is known)
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        f.Step(1060, false, kVanish, 3000, 0, false, true);
        f.Note(1700, 1);   // the hero's animation is over
        Expect(f.Step(1700, false, kVanish, 3000, 0, false, true) == End::None, "the animation over: still flying (even with thrown_model 0)");
        Expect(f.Step(1750, false, kFlyOn, 3000, 0, false, true) == End::None, "...and with thrown_model 1 the animation ending is no end for a potion either");
        Expect(f.Step(2300, true, kVanish, 3000, 0, false, true) == End::Landed && f.Taken() && !f.Active(), "it lands: Landed, and the game had taken the throw");
        Expect(!ThrownFly::IsHandoff(End::Landed), "a landing is no handoff: the potion breaks");
    }
    {   // not taken (refused): the flight is all there is - lands
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1100, 1);
        Expect(f.Step(1800, false, kVanish, 3000, 0, false, true) == End::None && !f.Taken(), "refused: it flies on");
        Expect(f.Step(2200, true, kVanish, 3000, 0, false, true) == End::Landed && !f.Taken(), "refused: landed, not taken");
    }
    {   // a throw that never lands (a ceiling, a pit): thrown_fly_ms ends it
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(3900, false, kVanish, 3000, 0, false, true) == End::None, "2.9 s: not yet");
        Expect(f.Step(4000, false, kVanish, 3000, 0, false, true) == End::Timeout && f.Taken(), "thrown_fly_ms: Timeout");
    }
    {   // the same events for a javelin (fliesToLanding false) are unchanged: the take ends it
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        Expect(f.Step(1060, false, kVanish, 3000, 0, false, false) == End::Taken, "a javelin: Taken at once, as ever");
    }
    {   // a new Start forgets the old take
        ThrownFly f;
        f.Note(900, 1);
        f.Start(1000);
        f.Note(1060, 11);
        f.Step(1060, false, kVanish, 3000, 350);
        f.Start(5000);
        Expect(!f.Taken(), "a new throw: not taken yet");
    }
    {   // the reasons that are logged
        Expect(ThrownFly::Why(End::Taken)[0] != 0 && ThrownFly::Why(End::Landed)[0] != 0 && ThrownFly::Why(End::Animation)[0] != 0 && ThrownFly::Why(End::Timeout)[0] != 0 &&
               ThrownFly::Why(End::HandoffAction)[0] != 0 && ThrownFly::Why(End::HandoffTime)[0] != 0,
               "every end has a reason in words");
    }
    // ---- the hand stays EMPTY while the game throws: from the game taking the throw until the animation is over (+ a tail), a cap on top ----
    using weight::EmptyHand;
    using EEnd = EmptyHand::End;
    {   // taken, the animation plays, it ends: the hand is empty all along, and for the tail after it
        EmptyHand h;
        h.Start(1000);
        Expect(h.Active(), "started");
        Expect(h.Step(1010, 11, 900) == EEnd::None && h.Active(), "throwing (mode 11): empty");
        Expect(h.Step(1300, 11, 900) == EEnd::None, "still throwing");
        Expect(h.Step(1500, 1, 900) == EEnd::None, "the mode is neutral: the tail begins, still empty");
        Expect(h.Step(1560, 1, 900) == EEnd::None, "60 ms of neutral: still empty");
        Expect(h.Step(1619, 1, 900) == EEnd::None, "119 ms of neutral: still empty");
        Expect(h.Step(1620, 1, 900) == EEnd::AnimationOver && !h.Active(), "120 ms of neutral: the animation is over, the next one is drawn");
        Expect(h.Step(1700, 1, 900) == EEnd::None, "done: nothing more");
    }
    {   // a blip out of the action mode and back (an attack into the next frame of the same animation): the tail starts over
        EmptyHand h;
        h.Start(1000);
        h.Step(1100, 11, 900);
        h.Step(1200, 1, 900);          // out ...
        Expect(h.Step(1300, 1, 900) == EEnd::None, "100 ms out: not yet");
        h.Step(1310, 7, 900);          // ... back in (the next swing's mode)
        Expect(h.Step(1400, 1, 900) == EEnd::None, "out again at 1400: the tail counts from here");
        Expect(h.Step(1519, 1, 900) == EEnd::None, "119 ms after that");
        Expect(h.Step(1520, 1, 900) == EEnd::AnimationOver, "120 ms after that: over");
    }
    {   // the mode signal is lost (never read): the cap ends it, never later
        EmptyHand h;
        h.Start(1000);
        Expect(h.Step(1500, -1, 900) == EEnd::None, "no mode: still empty at 500 ms");
        Expect(h.Step(1899, -1, 900) == EEnd::None, "899 ms: still empty");
        Expect(h.Step(1900, -1, 900) == EEnd::Cap && !h.Active(), "900 ms: the cap, the hand is full again");
    }
    {   // the hero stays in the throw mode for ever (a stuck signal): the cap ends it
        EmptyHand h;
        h.Start(1000);
        for (uint64_t t = 1000; t < 1900; t += 50) Expect(h.Step(t, 11, 900) == EEnd::None, "in the throw mode, under the cap");
        Expect(h.Step(1900, 11, 900) == EEnd::Cap, "the cap ends it though the mode says throw");
    }
    {   // the cap is the slider's: shorter than the animation, it ends first
        EmptyHand h;
        h.Start(5000);
        Expect(h.Step(5299, 11, 300) == EEnd::None && h.Step(5300, 11, 300) == EEnd::Cap, "a 300 ms cap");
    }
    {   // cap 0: not hidden at all - it ends at the first step
        EmptyHand h;
        h.Start(1000);
        Expect(h.Step(1000, 11, 0) == EEnd::Cap && !h.Active(), "cap 0: the hand is not kept empty");
    }
    {   // not started / reset / a clock that goes back
        EmptyHand h;
        Expect(h.Step(1000, 1, 900) == EEnd::None && !h.Active(), "not started: nothing");
        h.Start(2000);
        Expect(h.Step(1500, 11, 900) == EEnd::None, "the clock went back: no cap");
        h.Reset();
        Expect(!h.Active() && h.Step(9000, 1, 900) == EEnd::None, "reset: nothing");
        Expect(EmptyHand::Why(EEnd::AnimationOver)[0] != 0 && EmptyHand::Why(EEnd::Cap)[0] != 0, "the reasons in words");
    }
    // a late-starting neutral: the hero was already out of the action by the time the hand was emptied (a short throw): the tail, then drawn
    {
        EmptyHand h;
        h.Start(1000);
        Expect(h.Step(1000, 1, 900) == EEnd::None, "neutral at the start: the tail begins");
        Expect(h.Step(1120, 1, 900) == EEnd::AnimationOver, "and the hand is full after it, not held for the cap");
    }
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weight_throw_test: all passed\n");
    return 0;
}
