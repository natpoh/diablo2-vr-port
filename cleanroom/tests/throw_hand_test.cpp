// Offline checks of which controllers throw a thrown weapon, and whose ([hands] throw_left_hand, vr/throw_hand.h). No game, no headset.
// Run: build/Release/throw_hand_test.exe   (exit code 0 = all passed)

#include "../../vr/throw_hand.h"

#include <cstdio>
#include <cstring>

using namespace throwhand;

static int g_failures = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static void CountingTests() {
    Expect(XrOfSide(kSideRight) == kXrRight && XrOfSide(kSideLeft) == kXrLeft, "the game's hands to the controllers");
    Expect(SideOfXr(kXrRight) == kSideRight && SideOfXr(kXrLeft) == kSideLeft, "the controllers to the game's hands");
    for (int i = 0; i < 2; ++i) Expect(SideOfXr(XrOfSide(i)) == i && XrOfSide(SideOfXr(i)) == i, "there and back");
    Expect(std::strcmp(XrName(kXrLeft), "left") == 0 && std::strcmp(XrName(kXrRight), "right") == 0, "the names");
}

// A slot: thrown or not, the weight model's (placed) and who carries it.
static Slot S(bool thrown, bool placed = false, int carrier = -1, bool weapon = true) {
    Slot s;
    s.thrown = thrown;
    s.weapon = weapon || thrown;
    s.placed = placed;
    s.carrier = carrier;
    return s;
}
static In On(const Slot& right, const Slot& left, int lastRight = kSideRight, int lastLeft = kSideLeft) {
    In in;
    in.follow = true;
    in.slot[kSideRight] = right;
    in.slot[kSideLeft] = left;
    in.lastHand[kSideRight] = lastRight;
    in.lastHand[kSideLeft] = lastLeft;
    return in;
}
static bool Only(const Out& o, int xr, int slot) {
    return o.throws[xr] && o.slotOf[xr] == slot && !o.throws[xr ^ 1] && o.slotOf[xr ^ 1] == -1;
}

static void UntickedTests() {
    // unticked: the right controller when the game's weapon kind is a thrown one (as ever) - whatever the slots, carriers and last hands say
    for (int kind = 0; kind < 2; ++kind)
        for (uint32_t side = 0; side < 2; ++side) {
            In in = On(S(true, true, kSideLeft), S(true, true, kSideLeft), kSideLeft, kSideLeft);
            in.follow = false;
            in.thrownKind = kind != 0;
            in.weaponSide = side;
            const Out o = Plan(in);
            if (kind) Expect(Only(o, kXrRight, (int)side), "off, a thrown kind: the right controller, the kind's slot");
            else Expect(!o.throws[0] && !o.throws[1], "off, not a thrown kind: nobody");
            Expect(o.hand[0] == kSideLeft && o.hand[1] == kSideLeft, "off: the last hands stay as they were");
        }
}

static void TickedTests() {
    // no weight model: the game's slots
    Expect(Only(Plan(On(S(true), S(false))), kXrRight, kSideRight), "javelin right, an axe left: the right controller");
    {   // an axe right, javelin left: the game's Throw takes the right slot's weapon - it refuses this one, so nobody throws, and it is said
        const Out o = Plan(On(S(false), S(true)));
        Expect(!o.throws[0] && !o.throws[1] && o.refused[kSideLeft] && !o.refused[kSideRight], "an axe right, javelin left: refused, nobody throws");
        Expect(Only(Plan(On(S(false, false, -1, false), S(true))), kXrLeft, kSideLeft), "the right slot empty (or a shield), javelin left: the left controller");
        Expect(!GameThrowsFrom(On(S(false), S(true)).slot, kSideLeft) && GameThrowsFrom(On(S(true), S(true)).slot, kSideLeft), "the game's rule: the left only beside no weapon or a thrown one");
    }
    {
        const Out o = Plan(On(S(true), S(true)));
        Expect(o.throws[kXrRight] && o.slotOf[kXrRight] == kSideRight && o.throws[kXrLeft] && o.slotOf[kXrLeft] == kSideLeft, "two javelins: both, each its own");
    }
    Expect(!Plan(On(S(false), S(false))).throws[0] && !Plan(On(S(false), S(false))).throws[1], "nothing thrown: nobody");
    // the weight model: the hand that carries each - a javelin in the right slot handed over to the left
    Expect(Only(Plan(On(S(true, true, kSideLeft), S(false))), kXrLeft, kSideRight), "carried by the left (slot right): the left controller");
    {   // ... and nobody carries it: the hand that last did
        const Out o = Plan(On(S(true, true, -1), S(false), kSideLeft));
        Expect(Only(o, kXrLeft, kSideRight) && o.hand[kSideRight] == kSideLeft, "let go from the left: still the left");
        const Out o2 = Plan(On(S(true, true, -1), S(false), 5));
        Expect(Only(o2, kXrRight, kSideRight), "a last hand that is no hand: its own slot's");
    }
    {   // two javelins, the weight model's first and second: each hand its own
        const Out o = Plan(On(S(true, true, kSideRight), S(true, true, kSideLeft)));
        Expect(o.throws[0] && o.throws[1] && o.slotOf[kXrRight] == kSideRight && o.slotOf[kXrLeft] == kSideLeft, "two javelins, each in its hand");
        // the second let go (flying, lying): still the left's
        const Out o2 = Plan(On(S(true, true, kSideRight), S(true, true, -1)));
        Expect(o2.throws[0] && o2.slotOf[kXrLeft] == kSideLeft, "two javelins, the second let go: the left still throws it");
    }
    {   // both weapons in ONE hand (a hand-over of the first while the second is let go): the right slot's
        const Out o = Plan(On(S(true, true, kSideLeft), S(true, true, kSideLeft)));
        Expect(Only(o, kXrLeft, kSideRight), "both in the left hand: the left throws the right slot's");
    }
    {   // a javelin beside an axe the weight model has as a body of its own: the javelin's hand only, whoever carries the axe
        const Out o = Plan(On(S(true, true, kSideRight), S(false, true, kSideLeft)));
        Expect(Only(o, kXrRight, kSideRight), "javelin right, axe left (both bodies): the right controller only");
        const Out o2 = Plan(On(S(false, true, kSideRight), S(true, true, kSideLeft)));
        Expect(!o2.throws[0] && !o2.throws[1] && o2.refused[kSideLeft], "axe right, javelin left (both bodies): the game refuses - nobody");
        // the game's way: the javelin in the right slot, handed over to the left hand in VR, the axe (the second) in the right - the left throws the right slot's
        const Out o3 = Plan(On(S(true, true, kSideLeft), S(false, true, kSideRight)));
        Expect(Only(o3, kXrLeft, kSideRight), "javelin in the right slot carried by the left, the axe in the right hand: the left controller throws it");
    }
}

static void HandOverTests() {
    // the other hand holds THIS weapon (by slot: weight::HoldersOf, 1 right 2 left)
    Expect(HandOver(true, 1u | 2u, kXrRight) && HandOver(true, 1u | 2u, kXrLeft), "both hands on it: a hand-over, not a throw");
    Expect(HandOver(true, 2u, kXrRight) && HandOver(true, 1u, kXrLeft), "the other hand alone on it: a hand-over");
    Expect(!HandOver(true, 1u, kXrRight) && !HandOver(true, 2u, kXrLeft), "only the releasing hand on it: a throw");
    Expect(!HandOver(true, 0u, kXrRight) && !HandOver(true, 0u, kXrLeft), "nobody on it (or no weight model): a throw");
    Expect(!HandOver(false, 3u, kXrRight), "off: as ever, a throw");
    Expect(BeginsHeld(true, true, true), "began with a squeezed grip in grip mode: held from now");
    Expect(!BeginsHeld(false, true, true) && !BeginsHeld(true, false, true) && !BeginsHeld(true, true, false), "else not");
}

static void OwnsGripTests() {
    double until = 0.0;
    Expect(OwnsGrip(false, true, false, false, 10.0, &until) && OwnsGrip(true, false, false, false, 10.0, &until), "unticked or no weight model: owned as ever");
    Expect(OwnsGrip(true, true, true, false, 10.0, &until) && until > 10.0, "the hand holds it: owned");
    Expect(OwnsGrip(true, true, false, false, 10.2, &until), "let go a moment ago (the release that threw it): still owned");
    Expect(!OwnsGrip(true, true, false, false, 10.4, &until), "the hand empty for longer: free (a belt bottle)");
    Expect(!OwnsGrip(true, true, false, false, 20.0, &until), "... and a squeeze of the empty hand does not make it owned");
    Expect(OwnsGrip(true, true, false, true, 20.0, &until) && OwnsGrip(true, true, false, false, 20.25, &until), "a gesture under way: owned, and the grace after it");
    Expect(OwnsGrip(true, true, true, false, 30.0, &until), "the weapon back in the hand: owned again");
}

static void OwnsTests() {
    const bool right[2] = {false, true}, both[2] = {true, true}, none[2] = {false, false};
    Expect(OwnsOn(right, kXrRight) && !OwnsOn(right, kXrLeft), "the right owned: only the right's control");
    Expect(OwnsOn(both, kXrRight) && OwnsOn(both, kXrLeft), "both owned: both controls");
    Expect(!OwnsOn(none, kXrRight) && !OwnsOn(none, kXrLeft), "none owned: neither");
}

int main() {
    CountingTests();
    UntickedTests();
    TickedTests();
    HandOverTests();
    OwnsTests();
    OwnsGripTests();
    if (g_failures) {
        std::printf("%d failed\n", g_failures);
        return 1;
    }
    std::printf("throw_hand_test: all passed\n");
    return 0;
}
