// point_trigger.h - [input] point_pickup's trigger (F4): with an item on the floor highlighted (the pointing hand's ray on it, vr/unleashed_loot_aim.h)
// and that hand's GRIP held - a weapon is held by the grip, so with one it is down anyway - that hand's TRIGGER picks the item up: the trigger is
// taken from the layout (no attack goes out of it) and its press starts the game's own pick-up (vr/point_pickup_wire.cpp: the layout's interact pressed,
// the game's interact target answered with the item). Serg, 2026-11-02: "point at the item, press the trigger"; "if the grip is not held the trigger does
// not fire". The trigger alone (grip let go), or no item highlighted: the trigger is the layout's, as before. A picks it up too (actions.cpp).
// Pure (cleanroom/tests/point_trigger_test.cpp); the wiring is vr/point_pickup_wire.cpp.
//
// The take starts only with the trigger up: a trigger already held for an attack when an item comes under the ray is never cut short. Once its press
// is ours the trigger stays taken until it is let go (its release is no attack either), whatever the ray does meanwhile. With a bow or crossbow the
// string hand at the string is the draw's (bow_draw_wire's own "at the string"): no take there. Another gesture's take of that trigger (a belt bottle,
// the bow's draw) in the frame before: none either.
#pragma once

#include <cstdint>

namespace pointtrigger {

struct In {
    bool ok = false;            // F4, the input focus, no menu / panel / plugin panel up, point_pickup on
    bool highlighted = false;   // an item is highlighted now, by THIS hand's ray
    bool gripHeld = false;      // this hand's grip past its click
    bool triggerDown = false;   // this hand's trigger past its click
    bool atString = false;      // a bow or crossbow's string is this hand's to take (the draw's zone)
    bool takenByOther = false;  // another gesture took this trigger in the frame before
};

struct Out {
    bool take = false;    // take the trigger from the layout this frame
    bool press = false;   // the pick-up's press starts this frame
};

class Trigger {
public:
    Out Step(const In& in) {
        Out o;
        const bool ready = in.ok && in.highlighted && in.gripHeld && !in.atString && !in.takenByOther;
        if (held_) {   // our press: the trigger stays ours until it is let go
            if (in.triggerDown) { o.take = true; return o; }
            held_ = false;
        }
        if (!in.triggerDown) { armed_ = ready; wasDown_ = false; }
        else if (armed_ && !wasDown_ && ready) { held_ = true; o.press = true; }
        else armed_ = false;   // down without being armed first (an attack already held), or the item lost: the layout's
        wasDown_ = in.triggerDown;
        o.take = armed_ || held_;
        return o;
    }
    bool Holding() const { return held_; }
    void Reset() { armed_ = held_ = wasDown_ = false; }

private:
    bool armed_ = false;    // taken, waiting for the press
    bool held_ = false;     // the press is ours until the trigger is let go
    bool wasDown_ = false;
};

// Which hand's pick is lit, either hand pointing (0 right, 1 left; -1 none): id[h] that hand's pick (0 none), off[h] how far it lies from that
// hand's pointed spot. One hand with a pick: that one. Both: the hand that had it keeps it (no flicker between two items), else the nearer.
inline int PickOwner(const uint32_t id[2], const float off[2], int was) {
    if (!id[0] && !id[1]) return -1;
    if (!id[0] || !id[1]) return id[0] ? 0 : 1;
    if (was == 0 || was == 1) return was;
    return off[0] <= off[1] ? 0 : 1;
}

}  // namespace pointtrigger
