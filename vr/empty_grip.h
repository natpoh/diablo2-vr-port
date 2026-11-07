// empty_grip.h - [weight] grab_items_empty_grip: an EMPTY hand's grip alone picks up the floor item its ray points at ([input] point_pickup's lit item) - no
// trigger. Pure: no game, no windows.h (cleanroom/tests/empty_grip_test.cpp).
//
// Serg, 2026-11-07: "if the hand is empty, holding the grip is enough to pick an item up from the floor". The pick-up itself is the game's own (the layout's
// interact pressed, its interact target answered with the item - vr/point_pickup_wire.cpp, the same press as grip + trigger); this only says whether a grip
// press of this hand is the pick-up's. With anything in the hand the rule stays grip + trigger.
//
// Who else wants a hand's grip, and who wins (the grip goes to the pick-up only when none of them wants it):
//   - the weapon: a hand whose slot holds it, holds it (the weight model's hold), or would take it now - its shaft or the back within reach (grabbodies::BusyWhy's
//     take zone) - or would CALL IT BACK from afar (weight::TakeZone 3: the right grip calls a dropped weapon back) is not empty: the weapon's.
//   - the belt: a bottle in the hand, or one lit that the grip would take (engitem::BeltBusy, in BusyWhy): the belt's.
//   - the shoulder swap, the bow's string, the throw by grip: their claims are in BusyWhy: theirs.
//   - a body or a prop under the palm or held ([weight] grab_bodies / grab_props): theirs.
//   - an item within [weight] grab_items' own reach of the palm: grab_items' pick-up of THAT item (the nearest to the hand) goes first.
//   - our panels (the skill panel and its GESTURES tab, whose recording takes the trigger as BodyWalk's pen): direct_input marks those frames UI-up and the
//     pick-up's listener takes no press then; `panel` says so here too.
//   - another listener that took the grip last frame: the caller leaves it to that one.
#pragma once

#include <cstdint>

namespace emptygrip {

enum class Why : uint8_t { Takes, Off, Panel, NotEmpty, Recall, Body, NearItem };

struct In {
    bool on = false;           // [weight] grab_items_empty_grip, F4, the arms on the controllers
    bool panel = false;        // a panel of ours is up (the skill panel, its gestures tab)
    uint32_t busy = 0;         // grabbodies::BusyWhy for this hand (0: free - no weapon in its slot or hold, no bottle, no swap / string / throw claim, no take zone)
    bool recall = false;       // a grip now would call the weapon back from afar (weight::TakeZone 3)
    bool bodyOrProp = false;   // over or holding a body or a prop
    bool nearItem = false;     // an item within grab_items' reach of the palm (its pick-up goes first)
};

inline Why Decide(const In& in) {
    if (!in.on) return Why::Off;
    if (in.panel) return Why::Panel;
    if (in.busy) return Why::NotEmpty;
    if (in.recall) return Why::Recall;
    if (in.bodyOrProp) return Why::Body;
    if (in.nearItem) return Why::NearItem;
    return Why::Takes;
}

inline const char* WhyText(Why w) {
    switch (w) {
        case Why::Takes: return "an empty hand: its grip alone picks up the pointed item";
        case Why::Off: return "off ([weight] grab_items_empty_grip, F4)";
        case Why::Panel: return "a panel of ours is up";
        case Why::NotEmpty: return "the hand is not empty (a weapon, a bottle, a shoulder, a bow string or a throw has its grip): grip + trigger";
        case Why::Recall: return "the grip would call the weapon back";
        case Why::Body: return "over or holding a body or a prop";
        case Why::NearItem: return "an item within grab_items' reach of the palm: that one is picked up";
    }
    return "?";
}

}  // namespace emptygrip
