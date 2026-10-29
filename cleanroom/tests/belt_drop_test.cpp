// Offline checks of the steps of the game's own drop of a belt potion (vr/belt_drop.h): the belt slot's click, the cursor's drop, the way back. No game.
// Run: build/Release/belt_drop_test.exe   (exit code 0 = all passed)

#include "../../vr/belt_drop.h"

#include <cstdio>

using namespace beltdrop;

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static See InBelt() {
    See s;
    s.gatesOpen = true;
    s.slotOurs = true;
    return s;
}

int main() {
    {   // the usual way with the controller layout: the click, the item on the cursor, the game drops it itself
        Next n = Step(Phase::Asked, InBelt());
        Expect(n.phase == Phase::Picked && n.act == Act::Pickup, "asked, all clear: the belt slot's click");
        See s = InBelt();
        s.slotBusy = true;
        s.sinceS = 0.05;
        n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Picked && n.act == Act::None, "the server has not answered: wait");
        s = See();
        s.cursorEmpty = false; s.cursorOurs = true; s.cursorBusy = false; s.onCursorS = 0.1; s.sinceS = 0.2;
        n = Step(Phase::Picked, s);
        Expect(n.act == Act::None, "on the cursor a moment: the game's own drop has its chance first");
        s.cursorEmpty = true; s.cursorOurs = false; s.slotOurs = false; s.sinceS = 0.4;
        n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Done && n.act == Act::None, "gone from belt and cursor: dropped");
    }
    {   // the mouse layout: the game does not drop a held item by itself - the cursor's own drop, once
        See s;
        s.cursorEmpty = false; s.cursorOurs = true; s.onCursorS = 0.3; s.sinceS = 0.5;
        Next n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Picked && n.act == Act::Drop, "on the cursor 0.3 s: the cursor's drop");
        s.dropSent = true;
        n = Step(Phase::Picked, s);
        Expect(n.act == Act::None, "sent once only");
        s.cursorBusy = true; s.dropSent = false;
        Expect(Step(Phase::Picked, s).act == Act::None, "a busy cursor item: no drop");
    }
    {   // refusals and the way back
        See s;
        s.cursorEmpty = false; s.cursorOurs = true; s.dropSent = true; s.sinceS = 2.1;
        Next n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::PutBack, "still on the cursor after 2 s: back into the belt");
        s = InBelt();
        s.sinceS = 2.1;
        n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "the game never took it: it stays in the belt, nothing sent");
        s = See();
        s.cursorEmpty = false; s.cursorOurs = false; s.sinceS = 0.3;
        n = Step(Phase::Picked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "another item on the cursor: left alone");
    }
    {   // before anything is sent
        See s = InBelt();
        s.slotOurs = false;
        Expect(Step(Phase::Asked, s).phase == Phase::Stopped && Step(Phase::Asked, s).act == Act::None, "the item not in its place: nothing sent");
        s = InBelt();
        s.cursorEmpty = false;
        Expect(Step(Phase::Asked, s).phase == Phase::Stopped, "the cursor holds something: nothing sent");
        s = InBelt();
        s.gatesOpen = false; s.sinceS = 0.2;
        Expect(Step(Phase::Asked, s).phase == Phase::Asked && Step(Phase::Asked, s).act == Act::None, "a menu open: wait a little");
        s.sinceS = 0.6;
        Expect(Step(Phase::Asked, s).phase == Phase::Stopped, "still closed after half a second: given up");
        s = InBelt();
        s.slotBusy = true; s.sinceS = 0.1;
        Expect(Step(Phase::Asked, s).act == Act::None, "the item busy with another request: wait");
        Expect(Step(Phase::Idle, InBelt()).phase == Phase::Idle && Step(Phase::Done, InBelt()).act == Act::None, "idle and done do nothing");
    }
    {   // [hands] belt_throw_merc: one use of the slot to the mercenary, behind the game's own gates
        See s = InBelt();
        s.mercGateOpen = true;
        Next n = Step(Phase::Giving, s);
        Expect(n.phase == Phase::Done && n.act == Act::Give, "all clear: the belt key's use with Shift, once");
        s.mercGateOpen = false; s.sinceS = 0.1;
        n = Step(Phase::Giving, s);
        Expect(n.phase == Phase::Giving && n.act == Act::None, "the game's own merc question would say no now: wait");
        s.sinceS = 0.6;
        Expect(Step(Phase::Giving, s).phase == Phase::Stopped && Step(Phase::Giving, s).act == Act::None, "still no after half a second: not given");
        s = InBelt();
        s.mercGateOpen = true; s.slotOurs = false;
        Expect(Step(Phase::Giving, s).phase == Phase::Stopped && Step(Phase::Giving, s).act == Act::None, "the item gone from its place: not given");
        s = InBelt();
        s.mercGateOpen = true; s.slotBusy = true; s.sinceS = 0.1;
        Expect(Step(Phase::Giving, s).act == Act::None, "the item busy: wait");
        s = InBelt();
        s.mercGateOpen = true; s.cursorEmpty = false;
        Expect(Step(Phase::Giving, s).act == Act::Give, "something on the cursor does not stop a use (the key does not care either)");
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
