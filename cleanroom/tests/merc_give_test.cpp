// Offline checks of [hands] merc_give_weapon (vr/merc_give.h): the shaft against the mercenary's capsule, the watch of a release (given / missed / not ours),
// and the steps of the game's own moves (to the inventory, to the hireling, back into the hand). No game.
// Run: build/Release/merc_give_test.exe   (exit code 0 = all passed)

#include "../../vr/merc_give.h"

#include <cstdio>

using namespace mercgive;

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static Capsule Merc() {   // a man-sized mercenary at x 2, z 0: 0.3 m thick, 1.8 m tall
    Capsule m;
    m.bottom[0] = 2.0f; m.bottom[1] = 0.3f; m.bottom[2] = 0.0f;
    m.top[0] = 2.0f; m.top[1] = 1.5f; m.top[2] = 0.0f;
    m.radius = 0.3f;
    return m;
}

static Sample At(int state, uint32_t release, double t, float outside, bool seen = true, bool thrown = false) {
    Sample s;
    s.state = state;
    s.release = release;
    s.nowS = t;
    s.outside = outside;
    s.mercSeen = seen;
    s.thrown = thrown;
    return s;
}

static See InHand() {
    See s;
    s.gatesOpen = true;
    s.inHand = true;
    s.itemFound = true;
    s.mercCan = true;
    return s;
}

int main() {
    {   // the geometry
        const Capsule m = Merc();
        const float b1[3] = {1.0f, 1.0f, 0.0f}, t1[3] = {1.9f, 1.0f, 0.0f};   // a sword held into his chest
        Expect(ShaftOutside(m, b1, t1) < 0.0f, "a shaft into his body is inside");
        const float b2[3] = {0.0f, 1.0f, 0.0f}, t2[3] = {0.9f, 1.0f, 0.0f};   // a metre short of him
        const float o2 = ShaftOutside(m, b2, t2);
        Expect(o2 > 0.75f && o2 < 0.85f, "a shaft short of him: 1.1 m - 0.3 m outside");
        const float b3[3] = {1.0f, 0.02f, 0.5f}, t3[3] = {2.0f, 0.02f, 0.5f};  // lying on the floor beside his feet, 0.5 m across
        const float o3 = ShaftOutside(m, b3, t3);
        Expect(o3 > 0.25f && o3 < 0.35f, "a shaft lying beside his feet: about 0.29 m outside");
        const float p[3] = {2.0f, 3.0f, 0.0f};                                   // a point above his head
        Expect(ShaftOutside(m, p, p) > 1.1f, "a point over his head is outside by its height");
    }
    {   // given in the air: released while touching him
        Watch w;
        Expect(w.Step(At(1, 1, 10.0, -0.05f), 0.3f) == Verdict::Give, "let go against his body: given");
        Expect(!w.armed, "one verdict a release");
        Expect(w.Step(At(1, 1, 10.1, -0.05f), 0.3f) == Verdict::None, "no second give from the same release");
    }
    {   // thrown to him: in the air, then touching
        Watch w;
        Expect(w.Step(At(1, 2, 0.0, 2.0f), 0.3f) == Verdict::None, "in the air far from him: nothing yet");
        Expect(w.Step(At(1, 2, 0.3, 0.4f), 0.3f) == Verdict::None, "closer, not touching: nothing yet (in the air the reach does not count)");
        Expect(w.Step(At(1, 2, 0.4, -0.01f), 0.3f) == Verdict::Give, "it reaches him in the air: given");
    }
    {   // dropped at his feet
        Watch w;
        Expect(w.Step(At(1, 3, 0.0, 0.6f), 0.3f) == Verdict::None, "falling beside him");
        Expect(w.Step(At(2, 3, 0.4, 0.2f), 0.3f) == Verdict::Give, "lands within the reach of his feet: given");
    }
    {   // dropped away from him: missed, and his walking over it later is no give
        Watch w;
        Expect(w.Step(At(1, 4, 0.0, 1.5f), 0.3f) == Verdict::None, "falling away from him");
        Expect(w.Step(At(2, 4, 0.4, 1.4f), 0.3f) == Verdict::None, "landed away from him: still the land window");
        Expect(w.Step(At(2, 4, 1.0, 1.4f), 0.3f) == Verdict::Missed, "lay past the land window away from him: missed");
        Expect(w.Step(At(2, 4, 2.0, -0.1f), 0.3f) == Verdict::None, "he walks onto it later: nothing");
    }
    {   // a throw of a throwing weapon is the game's, never ours
        Watch w;
        Expect(w.Step(At(1, 5, 0.0, -0.1f, true, true), 0.3f) == Verdict::None, "a game throw into him: not given");
        Expect(!w.armed, "a game throw is not watched");
    }
    {   // the release was taken back into a hand: not watched on
        Watch w;
        Expect(w.Step(At(1, 6, 0.0, 1.0f), 0.3f) == Verdict::None, "let go");
        Expect(w.Step(At(0, 6, 0.1, -0.1f), 0.3f) == Verdict::None, "caught again (in a hand): no give");
        Expect(!w.armed, "caught again: not watched");
        Expect(w.Step(At(1, 6, 0.2, -0.1f), 0.3f) == Verdict::None, "the same release number in the air again: still not watched");
        Expect(w.Step(At(1, 7, 0.3, -0.1f), 0.3f) == Verdict::Give, "a new release into him: given");
    }
    {   // no mercenary seen: never given; the window runs out
        Watch w;
        Expect(w.Step(At(1, 8, 0.0, -1.0f, false), 0.3f) == Verdict::None, "no mercenary: nothing");
        Expect(w.Step(At(1, 8, 3.5, -1.0f, false), 0.3f) == Verdict::Missed, "still in the air (or caught on something) past the window: missed");
    }
    {   // a held weapon is never a release
        Watch w;
        Expect(w.Step(At(0, 0, 0.0, -0.5f), 0.3f) == Verdict::None, "held in his body (a swing through him): nothing");
        Expect(w.Step(At(0, 0, 0.1, -0.5f), 0.3f) == Verdict::None, "still held: nothing");
    }
    {   // the moves: the usual way
        Next n = Step(Phase::Asked, InHand());
        Expect(n.phase == Phase::ToGrid && n.act == Act::ToGrid, "asked, all clear: the slot's quick move into the inventory");
        See s = InHand();
        s.inHand = false;
        s.itemBusy = true;
        s.sinceS = 0.1;
        n = Step(Phase::ToGrid, s);
        Expect(n.phase == Phase::ToGrid && n.act == Act::None, "on its way into the inventory: wait");
        s.itemBusy = false;
        n = Step(Phase::ToGrid, s);
        Expect(n.phase == Phase::Giving && n.act == Act::Give, "free in the inventory: the equip on the hireling");
        s.onMerc = true;
        s.itemFound = true;
        n = Step(Phase::Giving, s);
        Expect(n.phase == Phase::Done && n.act == Act::None, "he wears it: done");
    }
    {   // nothing sent when it cannot be done
        See s = InHand();
        s.mercCan = false;
        Next n = Step(Phase::Asked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "he cannot use it: nothing sent");
        s = InHand();
        s.inHand = false;
        n = Step(Phase::Asked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "not in the hand any more: nothing sent");
        s = InHand();
        s.gatesOpen = false;
        s.sinceS = 0.2;
        n = Step(Phase::Asked, s);
        Expect(n.phase == Phase::Asked && n.act == Act::None, "the game busy a moment: wait");
        s.sinceS = 0.6;
        n = Step(Phase::Asked, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "the game busy too long: nothing sent");
    }
    {   // no room in the inventory: the game does not move it, it stays in the hand
        See s = InHand();
        s.sinceS = 1.6;
        Next n = Step(Phase::ToGrid, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "still in the hand after the answer time: stopped, nothing more sent");
    }
    {   // the mercenary refuses (no room for his old weapon): back into the hand
        See s = InHand();
        s.inHand = false;
        s.sinceS = 0.5;
        Next n = Step(Phase::Giving, s);
        Expect(n.phase == Phase::Giving && n.act == Act::None, "waiting for his answer");
        s.sinceS = 1.6;
        n = Step(Phase::Giving, s);
        Expect(n.phase == Phase::Back && n.act == Act::PutBack, "no answer: back into the hand");
        s.inHand = true;
        s.sinceS = 0.2;
        n = Step(Phase::Back, s);
        Expect(n.phase == Phase::Done, "back in the hand: done");
        s.inHand = false;
        s.sinceS = 1.6;
        n = Step(Phase::Back, s);
        Expect(n.phase == Phase::Stopped && n.act == Act::None, "not put back: it stays in the inventory, nothing more sent");
    }
    {   // busy items are never moved again
        See s = InHand();
        s.inHand = false;
        s.itemBusy = true;
        s.sinceS = 2.0;
        Next n = Step(Phase::Giving, s);
        Expect(n.act == Act::None && n.phase == Phase::Giving, "his answer under way past the answer time: wait, no put-back");
        s.sinceS = 4.5;
        n = Step(Phase::Giving, s);
        Expect(n.act == Act::None && n.phase == Phase::Stopped, "busy too long: given up, nothing sent");
    }
    std::printf("merc_give_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
