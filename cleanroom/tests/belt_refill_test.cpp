// Offline checks of [belt] refill_key's pure parts (vr/belt_refill.h): the press (tap or hold), the Shift + click's steps, the kinds of potion, the game's
// belt-fill plan read back per column (which inventory potion goes to which belt column), the belt before / after. No game.
// Run: build/Release/belt_refill_test.exe   (exit code 0 = all passed)

#include "../../vr/belt_refill.h"

#include <cstdio>
#include <cstring>
#include <limits>

using namespace beltrefill;

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static const uint32_t kHp4 = Code3('h', 'p', '4'), kMp5 = Code3('m', 'p', '5'), kMp3 = Code3('m', 'p', '3'), kRvl = Code3('r', 'v', 'l'),
                      kRvs = Code3('r', 'v', 's'), kYps = Code3('y', 'p', 's'), kHp1 = Code3('h', 'p', '1');

int main() {
    {   // the buttons and keys of the settings
        Expect(PadOf(1).hand == 1 && PadOf(1).bit == kSecondary, "button 1 = B on the right controller");
        Expect(PadOf(2).hand == 1 && PadOf(2).bit == kPrimary, "button 2 = A on the right");
        Expect(PadOf(3).hand == 0 && PadOf(3).bit == kSecondary, "button 3 = Y on the left");
        Expect(PadOf(4).hand == 0 && PadOf(4).bit == kPrimary, "button 4 = X on the left");
        Expect(PadOf(0).bit == 0 && PadOf(9).bit == 0 && PadOf(-1).bit == 0, "none / out of range: no button");
        Expect(KeyVk(1) == 'B' && KeyVk(0) == 0 && KeyVk(5) == 'Y' && KeyVk(7) == 0, "keyboard 1 = B, 0 none, out of range none");
        Expect(!std::strcmp(PadName(1), "B") && !std::strcmp(PadName(99), "none"), "button names");
        Expect(ClampHold(50.0f) == kHoldMinMs && ClampHold(9000.0f) == kHoldMaxMs && ClampHold(650.0f) == 650 && ClampHold(std::numeric_limits<float>::quiet_NaN()) == kHoldDefaultMs,
               "hold ms clamped, NaN = default");
    }
    {   // the press: a tap on a release before the hold time, a hold once at it, nothing on the release after a hold
        Press p;
        Expect(p.Step(true, 1000, 500) == Ev::None, "pressed: nothing yet");
        Expect(p.Step(true, 1300, 500) == Ev::None, "held 300 ms: nothing yet");
        Expect(p.Step(false, 1350, 500) == Ev::Tap, "let go at 350 ms: a tap");
        Expect(p.Step(false, 1400, 500) == Ev::None, "let go stays nothing");
        Expect(p.Step(true, 2000, 500) == Ev::None, "pressed again");
        Expect(p.Step(true, 2499, 500) == Ev::None, "499 ms: not yet");
        Expect(p.Step(true, 2500, 500) == Ev::Hold, "500 ms: the hold");
        Expect(p.Step(true, 3500, 500) == Ev::None, "the hold once only");
        Expect(p.Step(false, 3600, 500) == Ev::None, "let go after a hold: no tap");
        Press q;
        q.Step(true, 0, 500);
        q.Cancel();
        Expect(q.Step(false, 100, 500) == Ev::None, "cancelled, then let go: no tap");
        Expect(!q.Down(), "cancelled: not down");
    }
    {   // the Shift + click: Shift first, the button down and up, Shift last - each kStepMs after the one before
        Click c;
        Expect(!c.Busy() && c.Step(0) == Send::None, "idle: nothing");
        c.Start(100);
        Expect(c.Busy(), "started: busy");
        Expect(c.Step(100) == Send::ShiftDown, "Shift down at once");
        Expect(c.Step(100 + kStepMs - 1) == Send::None, "the button waits a step");
        Expect(c.Step(100 + kStepMs) == Send::ButtonDown, "then the button down");
        Expect(c.Step(100 + kStepMs + 1) == Send::None, "...");
        Expect(c.Step(100 + 2 * kStepMs) == Send::ButtonUp, "then the button up");
        Expect(c.Step(100 + 3 * kStepMs) == Send::ShiftUp, "then Shift up");
        Expect(!c.Busy() && c.Step(1000) == Send::None, "done: idle");
        bool shift = true, button = true;
        c.Start(0);
        c.Step(0);   // Shift down
        c.Abort(&shift, &button);
        Expect(shift && !button && !c.Busy(), "aborted after Shift down: Shift to let go, no button");
        c.Start(0); c.Step(0); c.Step(kStepMs);   // Shift and the button down
        c.Abort(&shift, &button);
        Expect(shift && button, "aborted with the button down: both to let go");
        c.Start(0);
        c.Abort(&shift, &button);
        Expect(!shift && !button, "aborted before anything was sent: nothing to let go");
    }
    {   // the kinds of potion
        Expect(KindOf(kHp4) == kHealth && KindOf(kHp1) == kHealth, "hp1..hp5: health");
        Expect(KindOf(kMp5) == kMana && KindOf(kMp3) == kMana, "mp1..mp5: mana");
        Expect(KindOf(kRvl) == kRejuv && KindOf(kRvs) == kRejuv, "rvs / rvl: rejuvenation");
        Expect(KindOf(kYps) == kOther, "an antidote: other");
        Expect(KindOf(0) == kNone, "no item: nothing");
        Expect(KindOf(Code3('h', 'p', '6')) == kOther && KindOf(Code3('m', 'p', '0')) == kOther, "not a real tier: other");
        const uint32_t noSpace = (uint32_t)'m' | (uint32_t)'p' << 8 | (uint32_t)'2' << 16;
        Expect(KindOf(noSpace) == kMana, "the code without its trailing space reads the same");
    }
    {   // the belt slots: the game's numbering, column = slot % 4, row = slot / 4
        Expect(ColumnOf(0) == 0 && RowOf(0) == 0, "slot 0: column 1, bottom row");
        Expect(ColumnOf(6) == 2 && RowOf(6) == 1, "slot 6: column 3, second row");
        Expect(ColumnOf(15) == 3 && RowOf(15) == 3, "slot 15: column 4, top row");
        Expect(ColumnOf(16) == -1 && ColumnOf(-1) == -1, "out of the belt");
    }
    {   // the game's plan read back: which inventory potion goes to which column (a sorceress: mana in columns 3 and 4, health in 1, rejuvenation in 2)
        Plan p;
        const uint32_t ids[] = {101, 102, 103, 104, 105, 106};
        const uint8_t slots[] = {6, 10, 7, 11, 4, 5};   // columns 3, 3, 4, 4, 1, 2 (rows 2-3)
        p.n = 6;
        for (int i = 0; i < p.n; ++i) { p.id[i] = ids[i]; p.slot[i] = slots[i]; }
        auto codeOf = [](uint32_t id) -> uint32_t {
            switch (id) {
                case 101: case 102: case 103: return kMp5;
                case 104: return kMp3;
                case 105: return kHp4;
                case 106: return kRvl;
                default: return 0;
            }
        };
        const PlanView v = View(p, codeOf);
        Expect(v.total == 6 && v.badSlot == 0, "six moves, all into the belt");
        Expect(v.column[2][kMana] == 2 && v.column[3][kMana] == 2, "columns 3 and 4: two mana each");
        Expect(v.column[0][kHealth] == 1 && v.column[1][kRejuv] == 1, "column 1: a health potion, column 2: a rejuvenation");
        Expect(v.column[0][kMana] == 0 && v.column[2][kHealth] == 0, "nothing of another kind in a column");
        Plan q;
        q.n = 2; q.id[0] = 999; q.slot[0] = 3; q.id[1] = 101; q.slot[1] = 40;
        const PlanView w = View(q, codeOf);
        Expect(w.total == 1 && w.column[3][kNone] == 1, "an id not in the inventory's list: counted as not known");
        Expect(w.badSlot == 1, "a slot past the belt: told apart, not counted");
        Plan big;
        big.n = 99;
        Expect(View(big, codeOf).total == kPlanMax, "a count past 16 is cut to the command's 16");
        Plan neg;
        neg.n = -3;
        Expect(View(neg, codeOf).total == 0, "a negative count: nothing");
    }
    {   // the belt before and after: what went in, per kind
        Belt before, after;
        before.id[0] = 1; before.code[0] = kHp4;                      // column 1 bottom: a health potion stays
        before.id[2] = 2; before.code[2] = kMp5;                      // column 3 bottom: a mana potion stays
        after = before;
        after.id[6] = 50; after.code[6] = kMp5;                       // column 3, row 2: new
        after.id[10] = 51; after.code[10] = kMp5;                     // column 3, row 3: new
        after.id[4] = 52; after.code[4] = kHp4;                       // column 1, row 2: new
        Moved m = Diff(before, after);
        Expect(m.total == 3 && m.count[kMana] == 2 && m.count[kHealth] == 1, "three went in: two mana, one health");
        char t[96];
        KindsText(m.count, t, sizeof t);
        Expect(!std::strcmp(t, "1 health, 2 mana"), "the log's words: \"1 health, 2 mana\"");
        Moved none = Diff(before, before);
        Expect(none.total == 0, "nothing changed: nothing moved");
        KindsText(none.count, t, sizeof t);
        Expect(!std::strcmp(t, "none"), "nothing: \"none\"");
        Belt drunk = before;
        drunk.id[0] = 0; drunk.code[0] = 0;                           // a potion drunk meanwhile is not a move
        Expect(Diff(before, drunk).total == 0, "a slot emptied is not counted");
        Belt shifted = before;
        shifted.id[0] = 77; shifted.code[0] = kHp4;                   // the next potion of the column fell into the place (another id): counted
        Expect(Diff(before, shifted).total == 1, "a new id in a slot is a move into the belt");
        char small[8];
        int many[kKinds] = {0, 12, 13, 14, 15};
        KindsText(many, small, sizeof small);
        Expect(std::strlen(small) < sizeof small, "a short buffer is never overrun");
    }
    {   // when the button / the key is ours
        When w;
        w.on = true; w.inventoryOpen = true; w.inWorld = true; w.laserOk = true;
        Expect(PadOurs(w) && KeyOurs(w), "ticked, the inventory open in a game, the laser usable: both ours");
        When x = w; x.on = false;
        Expect(!PadOurs(x) && !KeyOurs(x), "unticked: the game's, as before");
        x = w; x.inventoryOpen = false;
        Expect(!PadOurs(x) && !KeyOurs(x), "the inventory closed: the game's (B opens it as before)");
        x = w; x.laserOk = false;
        Expect(!PadOurs(x) && KeyOurs(x), "no laser (the game's window not in front, no hand): the controller's B is the game's; the keyboard key still ours");
        x = w; x.chatOpen = true;
        Expect(PadOurs(x) && !KeyOurs(x), "the chat line open: the letter is typed");
        x = w; x.inWorld = false;
        Expect(!PadOurs(x) && !KeyOurs(x), "no game (the menus): the game's");
        x = w; x.otherMenu = true;
        Expect(PadOurs(x) && KeyOurs(x), "the stash beside the inventory, refill_inventory_alone off: ours, as before");
        x.aloneOnly = true;
        Expect(!PadOurs(x) && !KeyOurs(x), "the stash (cube, trader) beside the inventory, refill_inventory_alone on: the game's - its Y moves the item there");
        x = w; x.aloneOnly = true;
        Expect(PadOurs(x) && KeyOurs(x), "the inventory alone, refill_inventory_alone on: ours");
        Expect(!InventoryFree(When{}), "nothing open: not free");
    }
    std::printf("belt_refill_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
