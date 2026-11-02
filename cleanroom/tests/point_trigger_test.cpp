// [input] point_pickup's trigger (vr/point_trigger.h): grip held + trigger on the pointing hand picks up the highlighted item; nothing else changes.
#include "point_trigger.h"
#include <cstdio>
using namespace pointtrigger;
static int g_fail = 0;
static void Check(bool b, const char* why) {
    if (!b) { std::printf("FAIL: %s\n", why); ++g_fail; }
}
static In Ready(bool trigger) {
    In in;
    in.ok = in.highlighted = in.gripHeld = true;
    in.triggerDown = trigger;
    return in;
}

int main() {
    {   // Serg's gesture: point (item highlighted), grip held, trigger pressed -> one press; the trigger never reaches the layout meanwhile
        Trigger t;
        Out o = t.Step(Ready(false));
        Check(o.take && !o.press, "highlighted + grip: the trigger is taken, waiting");
        o = t.Step(Ready(true));
        Check(o.take && o.press, "the trigger pressed: the pick-up's press, the trigger still taken");
        o = t.Step(Ready(true));
        Check(o.take && !o.press, "held: one press only, still taken");
        In lost = Ready(true);
        lost.highlighted = false;
        o = t.Step(lost);
        Check(o.take && !o.press, "the ray left the item while the trigger is held: still ours until it is let go");
        lost.triggerDown = false;
        o = t.Step(lost);
        Check(!o.take && !o.press, "let go, no item: the layout's again");
    }
    {   // the grip not held: the trigger does what it did
        Trigger t;
        In in = Ready(false);
        in.gripHeld = false;
        Check(!t.Step(in).take, "no grip: not taken");
        in.triggerDown = true;
        const Out o = t.Step(in);
        Check(!o.take && !o.press, "no grip: the trigger is the layout's (an attack), no pick-up");
    }
    {   // no item highlighted: as before
        Trigger t;
        In in = Ready(false);
        in.highlighted = false;
        Check(!t.Step(in).take, "nothing highlighted: not taken");
        in.triggerDown = true;
        Check(!t.Step(in).press, "nothing highlighted: no pick-up");
    }
    {   // a trigger already held for an attack when an item comes under the ray is not cut short
        Trigger t;
        In in = Ready(true);
        in.highlighted = false;
        t.Step(in);   // attacking
        in.highlighted = true;
        Out o = t.Step(in);
        Check(!o.take && !o.press, "an item highlighted mid-attack: the held trigger stays the layout's");
        o = t.Step(Ready(false));
        Check(o.take && !o.press, "let go: now armed");
        Check(t.Step(Ready(true)).press, "the next press picks up");
    }
    {   // the bow's string hand at the string, or another gesture's take: no pick-up
        Trigger t;
        In in = Ready(false);
        in.atString = true;
        Check(!t.Step(in).take, "at the string: the draw's");
        in.triggerDown = true;
        Check(!t.Step(in).press, "at the string: the press draws, no pick-up");
        Trigger u;
        In other = Ready(false);
        other.takenByOther = true;
        Check(!u.Step(other).take, "a belt bottle / the draw took it: not ours");
    }
    {   // a menu or the view changed (ok false) before the press: not taken
        Trigger t;
        t.Step(Ready(false));
        In in = Ready(false);
        in.ok = false;
        Check(!t.Step(in).take, "a menu: let go");
        in.triggerDown = true;
        Check(!t.Step(in).press, "a menu: no pick-up");
    }
    {   // two picks in a row
        Trigger t;
        t.Step(Ready(false));
        Check(t.Step(Ready(true)).press, "first press");
        t.Step(Ready(false));
        Check(t.Step(Ready(true)).press, "second press after letting go");
    }
    {   // either hand points: which one's pick is lit
        const uint32_t none[2] = {0, 0}, right[2] = {7, 0}, left[2] = {0, 9}, both[2] = {7, 9};
        const float off[2] = {1.0f, 0.5f};
        Check(PickOwner(none, off, 0) == -1, "no hand on an item: none lit");
        Check(PickOwner(right, off, 1) == 0 && PickOwner(left, off, 0) == 1, "one hand on an item: that hand, whoever had it");
        Check(PickOwner(both, off, 0) == 0 && PickOwner(both, off, 1) == 1, "both on items: the hand that had it keeps it");
        Check(PickOwner(both, off, -1) == 1, "both, none had it: the nearer to its pointed spot");
    }
    if (g_fail) { std::printf("%d failed\n", g_fail); return 1; }
    std::printf("point_trigger_test: all passed\n");
    return 0;
}
