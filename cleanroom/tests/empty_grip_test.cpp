// [weight] grab_items_empty_grip (vr/empty_grip.h): who gets an empty hand's grip press - the pick-up of the pointed item, or the weapon, the belt, the swap,
// a body / prop, grab_items' own nearer item, a panel of ours. Run: empty_grip_test.exe

#include <cstdio>

#include "empty_grip.h"

static int g_checks = 0, g_fails = 0;
#define EXPECT(c, msg)                                                   \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, msg); } \
    } while (0)

int main() {
    using emptygrip::Decide;
    using emptygrip::In;
    using emptygrip::Why;
    In in;
    in.on = true;
    EXPECT(Decide(in) == Why::Takes, "an empty hand, nothing else wants the grip: the pick-up's");
    In off = in;
    off.on = false;
    EXPECT(Decide(off) == Why::Off, "the tick off (or not F4): grip + trigger as before");
    In busy = in;
    busy.busy = 1;   // the weapon's slot, a bottle, the swap, the string, the throw, a take zone
    EXPECT(Decide(busy) == Why::NotEmpty, "a weapon / bottle / shoulder in the hand: grip + trigger");
    In recall = in;
    recall.recall = true;
    EXPECT(Decide(recall) == Why::Recall, "the right grip calls a dropped weapon back: the recall keeps it");
    In body = in;
    body.bodyOrProp = true;
    EXPECT(Decide(body) == Why::Body, "a body or a prop under the palm: the grab's");
    In near = in;
    near.nearItem = true;
    EXPECT(Decide(near) == Why::NearItem, "an item at the palm: grab_items picks that one, not the pointed one too");
    In panel = in;
    panel.panel = true;
    panel.busy = 1;
    EXPECT(Decide(panel) == Why::Panel, "a panel of ours (the gestures tab recording) wins over all");
    In all = in;
    all.busy = 4;
    all.recall = all.bodyOrProp = all.nearItem = true;
    EXPECT(Decide(all) == Why::NotEmpty, "the hand's own load is said first");
    for (int w = 0; w <= (int)Why::NearItem; ++w) EXPECT(emptygrip::WhyText((Why)w)[0] != '?', "every answer has its words");
    if (g_fails) { std::printf("empty_grip_test: %d of %d checks FAILED\n", g_fails, g_checks); return 1; }
    std::printf("empty_grip_test: all %d checks passed\n", g_checks);
    return 0;
}
