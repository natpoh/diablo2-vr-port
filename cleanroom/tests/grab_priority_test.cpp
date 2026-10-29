// grab_priority_test - [belt] grab_nearest and [belt] grab_trigger (vr/grab_priority.h): when a grip press could take a belt potion or the weapon, who gets it, and the log line
// that says so.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../../vr/grab_priority.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace grabprio;

In At(bool nearest, Zone zone, float zoneCm, int bottle, float bottleCm) {
    In i;
    i.nearest = nearest;
    i.zone = zone;
    i.zoneCm = zoneCm;
    i.bottle = bottle;
    i.bottleCm = bottleCm;
    return i;
}

void TestNearest() {
    std::printf("the nearer of a potion and the weapon\n");
    // only one in reach: that one, with the option on or off
    for (const bool nearest : {true, false}) {
        const Out b = Decide(At(nearest, Zone::None, 1e6f, 1, 8.0f));
        EXPECT(b.who == Who::Belt && b.bottleLit && b.bottleTakes, "a hand at the belt, no weapon near (nearest=%d): the potion", nearest);
        const Out w = Decide(At(nearest, Zone::Shaft, 6.0f, -1, 1e6f));
        EXPECT(w.who == Who::Weapon && !w.bottleLit && !w.bottleTakes, "a hand on the shaft, no potion near (nearest=%d): the weapon", nearest);
    }
    // BOTH in reach: the nearer wins - the case of the report
    {
        const Out near = Decide(At(true, Zone::Shaft, 5.0f, 2, 9.0f));
        EXPECT(near.who == Who::Weapon && !near.bottleLit && !near.bottleTakes, "the shaft 5 cm, the potion 9 cm: the weapon, the potion not lit");
        const Out belt = Decide(At(true, Zone::Shaft, 11.0f, 2, 4.0f));
        EXPECT(belt.who == Who::Belt && belt.bottleLit && belt.bottleTakes, "the shaft 11 cm, the potion 4 cm: the potion");
        const Out tie = Decide(At(true, Zone::Shaft, 7.0f, 2, 7.0f));
        EXPECT(tie.who == Who::Weapon && !tie.bottleLit, "equally near: the weapon");
        const Out back = Decide(At(true, Zone::Back, 18.0f, 0, 10.0f));
        EXPECT(back.who == Who::Belt, "the weapon on the back 18 cm, the potion 10 cm: the potion");
        const Out back2 = Decide(At(true, Zone::Back, 6.0f, 0, 10.0f));
        EXPECT(back2.who == Who::Weapon, "the weapon on the back 6 cm, the potion 10 cm: the weapon");
        const Out old = Decide(At(false, Zone::Shaft, 2.0f, 2, 11.0f));
        EXPECT(old.who == Who::Belt && old.bottleLit && old.bottleTakes, "the option off: the potion in reach wins whatever the weapon's distance, as before");
    }
    // the weapon is away and this grip would call it back (Recall): its distance counts like any - a hand at the belt takes the potion, a hand near the weapon the weapon
    {
        EXPECT(Decide(At(true, Zone::Recall, 40.0f, 1, 9.0f)).who == Who::Belt, "a recall from 40 cm, the potion 9 cm: the potion (the hand is at the belt)");
        EXPECT(Decide(At(true, Zone::Recall, 40.0f, -1, 1e6f)).who == Who::Weapon, "a recall, no potion: the weapon is called");
        EXPECT(Decide(At(true, Zone::Recall, 8.0f, 1, 10.0f)).who == Who::Weapon, "a recall from 8 cm (the weapon lies at the hip), the potion 10 cm: the weapon");
        EXPECT(Decide(At(false, Zone::Recall, 40.0f, -1, 1e6f)).who == Who::Weapon, "the option off, no potion: the recall");
    }
    // a hand that holds the weapon takes no bottle, as before
    {
        In i = At(true, Zone::None, 1e6f, 1, 5.0f);
        i.holdsWeapon = true;
        const Out o = Decide(i);
        EXPECT(o.who == Who::HeldWeapon && !o.bottleLit && !o.bottleTakes, "the hand that holds it");
        i.nearest = false;
        EXPECT(Decide(i).who == Who::HeldWeapon && !Decide(i).bottleLit, "... also with the option off");
    }
    // the shoulder swap: the hand behind a shoulder is the swap's, a potion is never taken there
    {
        In i = At(true, Zone::None, 1e6f, 1, 6.0f);
        i.swapNear = true;
        EXPECT(Decide(i).who == Who::Swap && !Decide(i).bottleLit && !Decide(i).bottleTakes, "nearest: a hand in the swap's volume near a potion: the swap, the potion not lit");
        i.swapNear = false;
        i.swapClaims = true;
        EXPECT(Decide(i).who == Who::Swap && !Decide(i).bottleLit, "the swap's claim");
        i.nearest = false;
        EXPECT(Decide(i).who == Who::Swap && Decide(i).bottleLit && Decide(i).bottleTakes, "option off: the swap has the grip, the potion is lit and the hand busy, as it was");
        In n = At(false, Zone::None, 1e6f, 1, 6.0f);
        n.swapNear = true;
        EXPECT(Decide(n).who == Who::Belt, "option off: merely being in the volume is no claim - the potion, as before");
    }
    // nothing in reach
    {
        const Out o = Decide(At(true, Zone::None, 1e6f, -1, 1e6f));
        EXPECT(o.who == Who::Nothing && !o.bottleLit && !o.bottleTakes, "nothing in reach");
    }
}

void TestTrigger() {
    std::printf("trigger + grip\n");
    In base = At(true, Zone::None, 1e6f, 1, 5.0f);
    base.needTrigger = true;
    base.triggerKnown = true;
    {   // a potion only: the grip alone does not take it - the ball is lit, nothing is taken, the weapon is not held off
        const Out o = Decide(base);
        EXPECT(o.who == Who::Nothing && o.bottleLit && !o.bottleTakes && o.triggerHeld, "the grip alone at the belt: nothing is taken, the ball shows, it is the trigger's doing");
        In t = base;
        t.triggerDown = true;
        const Out p = Decide(t);
        EXPECT(p.who == Who::Belt && p.bottleLit && p.bottleTakes && !p.triggerHeld, "trigger + grip: the potion");
    }
    {   // the weapon in reach as well and nearer than the potion: the weapon whatever the trigger says
        In t = At(true, Zone::Shaft, 4.0f, 1, 9.0f);
        t.needTrigger = t.triggerKnown = true;
        EXPECT(Decide(t).who == Who::Weapon && !Decide(t).bottleTakes, "the weapon nearer: the weapon");
        t.triggerDown = true;
        EXPECT(Decide(t).who == Who::Weapon, "... also with the trigger squeezed (the potion is the farther one)");
    }
    {   // the potion nearer than the weapon: grip alone goes to the weapon in reach (the weapon is not held off by the bottle); with the trigger the potion
        In t = At(true, Zone::Shaft, 11.0f, 1, 3.0f);
        t.needTrigger = t.triggerKnown = true;
        const Out a = Decide(t);
        EXPECT(a.who == Who::Weapon && a.bottleLit && !a.bottleTakes && a.triggerHeld, "the potion nearer, the trigger up: the grip goes to the weapon, the ball shows, the hand is not busy");
        t.triggerDown = true;
        const Out b = Decide(t);
        EXPECT(b.who == Who::Belt && b.bottleTakes, "the trigger down: the potion");
    }
    {   // the weapon that would be called back from afar, the hand at the belt: the grip alone calls the weapon
        In t = At(true, Zone::Recall, 40.0f, 1, 5.0f);
        t.needTrigger = t.triggerKnown = true;
        EXPECT(Decide(t).who == Who::Weapon && !Decide(t).bottleTakes, "a recall with the hand at the belt and the trigger up: the weapon");
        t.triggerDown = true;
        EXPECT(Decide(t).who == Who::Belt, "... with the trigger squeezed: the potion");
    }
    {   // the trigger cannot be read (no direct input): the grip alone takes, as before
        In t = base;
        t.triggerKnown = false;
        EXPECT(Decide(t).who == Who::Belt && Decide(t).bottleTakes, "no trigger reading: the grip alone takes");
    }
    {   // the option off: the trigger is nothing to it
        In t = base;
        t.needTrigger = false;
        EXPECT(Decide(t).who == Who::Belt && Decide(t).bottleTakes, "grab_trigger off: the grip alone takes");
    }
    {   // the swap and the held weapon do not care
        In t = base;
        t.swapNear = true;
        EXPECT(Decide(t).who == Who::Swap, "behind a shoulder: the swap");
        t.swapNear = false;
        t.holdsWeapon = true;
        EXPECT(Decide(t).who == Who::HeldWeapon, "the hand that holds it");
    }
    {   // the old way (grab_nearest off) with the trigger
        In t = base;
        t.nearest = false;
        EXPECT(Decide(t).who == Who::Nothing && Decide(t).triggerHeld && !Decide(t).bottleTakes, "nearest off, trigger on, the trigger up: nothing is taken");
        t.triggerDown = true;
        EXPECT(Decide(t).who == Who::Belt && Decide(t).bottleTakes, "... the trigger down: the potion");
    }
}

void TestLine() {
    std::printf("the log line\n");
    char b[480];
    In i = At(true, Zone::Shaft, 5.0f, 1, 9.0f);
    Out o = Decide(i);
    Line(i, o, 1, "hp1", b, sizeof b);
    EXPECT(std::strstr(b, "left hand") && std::strstr(b, "WEAPON") && std::strstr(b, "5 cm") && std::strstr(b, "12 cm") && std::strstr(b, "belt potion 2 (hp1)") && std::strstr(b, "9 cm") &&
               std::strstr(b, "not lit, not taken") && std::strstr(b, "nearer than the potion"), "the weapon wins: %s", b);
    i = At(true, Zone::None, 1e6f, 3, 4.0f);
    o = Decide(i);
    Line(i, o, 0, nullptr, b, sizeof b);
    EXPECT(std::strstr(b, "right hand") && std::strstr(b, "BELT") && std::strstr(b, "the only thing in reach") && std::strstr(b, "belt potion 4") && std::strstr(b, "no weapon zone"), "the belt: %s", b);
    i = At(true, Zone::Shaft, 11.0f, 0, 3.0f);
    o = Decide(i);
    Line(i, o, 1, "mp2", b, sizeof b);
    EXPECT(std::strstr(b, "BELT") && std::strstr(b, "nearer than the weapon"), "the potion nearer: %s", b);
    i = At(false, Zone::Shaft, 5.0f, 0, 3.0f);
    o = Decide(i);
    Line(i, o, 1, "mp2", b, sizeof b);
    EXPECT(std::strstr(b, "BELT") && std::strstr(b, "grab_nearest is off"), "off: the belt wins and the line says why: %s", b);
    i = At(true, Zone::Back, 22.0f, -1, 1e6f);
    i.swapNear = true;
    o = Decide(i);
    Line(i, o, 1, nullptr, b, sizeof b);
    EXPECT(std::strstr(b, "SWAP") && std::strstr(b, "behind a shoulder"), "the swap: %s", b);
    i = At(true, Zone::Recall, 40.0f, -1, 1e6f);
    o = Decide(i);
    Line(i, o, 1, nullptr, b, sizeof b);
    EXPECT(std::strstr(b, "WEAPON") && std::strstr(b, "away") && std::strstr(b, "call it back"), "the recall: %s", b);
    i = At(true, Zone::None, 1e6f, 1, 5.0f);
    i.needTrigger = i.triggerKnown = true;
    o = Decide(i);
    Line(i, o, 0, "hp3", b, sizeof b);
    EXPECT(std::strstr(b, "nothing") && std::strstr(b, "trigger + grip") && std::strstr(b, "[trigger not squeezed]") && std::strstr(b, "lit (the ball), not taken"), "grip alone, trigger mode: %s", b);
    i.triggerDown = true;
    o = Decide(i);
    Line(i, o, 0, "hp3", b, sizeof b);
    EXPECT(std::strstr(b, "BELT") && std::strstr(b, "[trigger squeezed]"), "trigger + grip: %s", b);
    i = At(true, Zone::None, 1e6f, -1, 1e6f);
    o = Decide(i);
    Line(i, o, 0, nullptr, b, sizeof b);
    EXPECT(std::strstr(b, "nothing") && std::strstr(b, "no belt potion in reach") && !std::strstr(b, "trigger"), "nothing: %s", b);
    i.holdsWeapon = true;
    o = Decide(i);
    Line(i, o, 1, nullptr, b, sizeof b);
    EXPECT(std::strstr(b, "already holds"), "the holding hand: %s", b);
    char tiny[24];
    EXPECT(Line(i, o, 0, nullptr, tiny, sizeof tiny) > 0 && std::strlen(tiny) < sizeof tiny, "a short buffer is cut, never overrun");
}

}  // namespace

int main() {
    TestNearest();
    TestTrigger();
    TestLine();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
