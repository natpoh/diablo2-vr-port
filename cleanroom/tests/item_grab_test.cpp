// [weight] grab_items: the pure picker (vr/item_grab.h) - which item a grip asks the game's pick-up for, and when the press ends.
//
// Checks: a grip squeezed past 0.85 by a free palm within reach of an item asks for that item (the nearest), out of reach or a busy hand nothing; one press per squeeze
// (held on after a press ended asks nothing until let go); the item gone from a fresh scan is taken - the hand holds our copy while the grip is held, let go it is
// dropped; a stale scan is no proof; the grip let go before the game answered ends the press; the game not answering in 2.5 s ends it; the target is the item's
// unit as the scan has it now, and none outside a press.

#include <cstdio>

#include "item_grab.h"

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

using namespace itemgrab;

Hand At(float x, float y, float z, float grip, bool free = true) {
    Hand h;
    h.tracked = true;
    h.free = free;
    h.grip = grip;
    h.palm[0] = x; h.palm[1] = y; h.palm[2] = z;
    return h;
}
Item It(uint32_t id, float x, float z, uintptr_t unit = 0) {
    Item i;
    i.id = id;
    i.unit = unit ? unit : 0x10000000u + id;
    i.p[0] = x; i.p[1] = 0.0f; i.p[2] = z;
    return i;
}

void TestAsk() {
    const Item items[2] = {It(7, 1.0f, 1.0f), It(8, 1.15f, 1.0f)};
    Picker p;
    Hand h[2] = {At(1.1f, 0.15f, 1.0f, 0.5f), At(0, 1, 0, 0)};
    EXPECT(p.Step(0.0, h, items, 2, true, kReachM) == 0 && p.phase() == Phase::Idle, "a light grip asks nothing");
    h[0].grip = 0.9f;
    const unsigned ev = p.Step(0.01, h, items, 2, true, kReachM);
    EXPECT((ev & kAsked) && p.Pressing() && p.side() == 0 && p.item().id == 8, "the nearest item asked (id %u)", p.item().id);
    EXPECT(p.Target() == 0x10000008u, "its unit is the target");
    Picker q;
    Hand far[2] = {At(2.0f, 0.15f, 2.0f, 0.0f), At(0, 1, 0, 0)};
    q.Step(0.0, far, items, 2, true, kReachM);
    far[0].grip = 1.0f;
    EXPECT(q.Step(0.01, far, items, 2, true, kReachM) == 0 && !q.Pressing() && q.Target() == 0, "out of reach: nothing");
    Picker b;
    Hand busy[2] = {At(1.0f, 0.1f, 1.0f, 0.0f, false), At(0, 1, 0, 0)};
    b.Step(0.0, busy, items, 2, true, kReachM);
    busy[0].grip = 1.0f;
    EXPECT(b.Step(0.01, busy, items, 2, true, kReachM) == 0, "a busy hand (a weapon, a body) asks nothing");
    Picker l;
    Hand left[2] = {At(0, 1, 0, 0), At(1.0f, 0.1f, 1.0f, 0.0f)};
    l.Step(0.0, left, items, 2, true, kReachM);
    left[1].grip = 0.95f;
    EXPECT((l.Step(0.01, left, items, 2, true, kReachM) & kAsked) && l.side() == 1 && l.item().id == 7, "the left hand too");
}

void TestTaken() {
    Item items[1] = {It(5, 0.0f, 0.0f)};
    Picker p;
    Hand h[2] = {At(0.05f, 0.1f, 0.0f, 0.0f), At(0, 1, 0, 0)};
    p.Step(0.0, h, items, 1, true, kReachM);
    h[0].grip = 0.9f;
    p.Step(0.1, h, items, 1, true, kReachM);
    items[0].unit = 0x5555;
    p.Step(0.2, h, items, 1, true, kReachM);
    EXPECT(p.Target() == 0x5555, "the target follows the scan's unit");
    EXPECT(p.Step(0.3, h, items, 0, false, kReachM) == 0 && p.Pressing(), "a stale scan without it is no proof");
    const unsigned ev = p.Step(0.4, h, items, 0, true, kReachM);
    EXPECT((ev & kTaken) && !(ev & kDropped) && p.HoldingHand() == 0 && p.Target() == 0, "off the ground: taken, the hand holds our copy");
    h[0].grip = 0.6f;
    EXPECT(p.Step(0.5, h, items, 0, true, kReachM) == 0 && p.HoldingHand() == 0, "held on (above the let-go)");
    h[0].grip = 0.2f;
    EXPECT((p.Step(0.6, h, items, 0, true, kReachM) & kDropped) && p.phase() == Phase::Idle, "let go: the copy goes");
}

void TestEnds() {
    Item items[1] = {It(5, 0.0f, 0.0f)};
    {
        Picker p;
        Hand h[2] = {At(0.0f, 0.1f, 0.0f, 0.0f), At(0, 1, 0, 0)};
        p.Step(0.0, h, items, 1, true, kReachM);
        h[0].grip = 0.9f;
        p.Step(0.1, h, items, 1, true, kReachM);
        h[0].grip = 0.1f;
        EXPECT((p.Step(0.2, h, items, 1, true, kReachM) & kLetGoEarly) && !p.Pressing(), "let go before the game took it");
    }
    {
        Picker p;
        Hand h[2] = {At(0.0f, 0.1f, 0.0f, 0.0f), At(0, 1, 0, 0)};
        p.Step(0.0, h, items, 1, true, kReachM);
        h[0].grip = 0.9f;
        p.Step(0.1, h, items, 1, true, kReachM);
        EXPECT(p.Step(2.0, h, items, 1, true, kReachM) == 0 && p.Pressing(), "still pressing at 1.9 s");
        EXPECT((p.Step(2.7, h, items, 1, true, kReachM) & kNotTaken) && !p.Pressing(), "not taken after 2.5 s");
        EXPECT(p.Step(2.8, h, items, 1, true, kReachM) == 0 && !p.Pressing(), "the same squeeze asks no more");
        h[0].grip = 0.0f;
        p.Step(2.9, h, items, 1, true, kReachM);
        h[0].grip = 0.9f;
        EXPECT((p.Step(3.0, h, items, 1, true, kReachM) & kAsked), "a new squeeze asks again");
    }
    {   // taken while the grip was already let go: dropped at once
        Picker p;
        Hand h[2] = {At(0.0f, 0.1f, 0.0f, 0.0f), At(0, 1, 0, 0)};
        p.Step(0.0, h, items, 1, true, kReachM);
        h[0].grip = 0.9f;
        p.Step(0.1, h, items, 1, true, kReachM);
        h[0].tracked = false;
        const unsigned ev = p.Step(0.2, h, items, 0, true, kReachM);
        EXPECT((ev & kTaken) && (ev & kDropped) && p.phase() == Phase::Idle, "taken with the hand lost: no copy held");
    }
}

}  // namespace

int main() {
    TestAsk();
    TestTaken();
    TestEnds();
    std::printf(g_fails ? "item_grab_test: %d check(s) failed\n" : "item_grab_test: all checks passed\n", g_fails);
    return g_fails ? 1 : 0;
}
