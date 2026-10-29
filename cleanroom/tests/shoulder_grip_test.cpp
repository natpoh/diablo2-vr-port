// Offline checks of which action a grip pressed behind a shoulder is (vr/shoulder_grip.h): the weapon swap, or the take of the weapon
// stowed on the back - never both. No game, no headset. Run: build/Release/shoulder_grip_test.exe   (exit code 0 = all passed)

#include "../../vr/shoulder_grip.h"

#include <cstdio>

using namespace shouldergrip;

static int g_failures = 0;

static void Expect(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

int main() {
    // exhaustive: every combination of the five facts
    int swaps = 0, takes = 0;
    for (int bits = 0; bits < 32; ++bits) {
        In in;
        in.press = bits & 1;
        in.inReach = bits & 2;
        in.stowedOnBack = bits & 4;
        in.swapAsked = bits & 8;
        in.potion = bits & 16;
        const Action a = Choose(in);
        // never a swap and a take for one press: one answer by construction; check what each answer needs
        if (a == Action::Swap) {
            ++swaps;
            Expect(in.press && in.inReach && in.swapAsked && !in.stowedOnBack, "a swap needs a press in the reach, asked for, with nothing on the back");
        }
        if (a == Action::TakeStowed) {
            ++takes;
            Expect(in.press && in.inReach && in.stowedOnBack && !in.potion, "a take needs a press in the reach with a weapon on the back and no potion");
        }
        if (!in.press || !in.inReach) Expect(a == Action::None, "no press, or out of the reach: nothing");
        if (in.stowedOnBack) Expect(a != Action::Swap, "a weapon on the back: the swap is never asked");
        if (!in.stowedOnBack && in.press && in.inReach && in.swapAsked) Expect(a == Action::Swap, "nothing on the back: the swap as ever");
        if (in.stowedOnBack && in.press && in.inReach && !in.potion) Expect(a == Action::TakeStowed, "a weapon on the back: the press takes it");
    }
    Expect(swaps == 2, "the swap in exactly two of the 32 cases (the potion bit is free)");
    Expect(takes == 2, "the take in exactly two of the 32 cases (the swap-asked bit is free)");
    // the tick at the take zone
    {
        ZoneTick z;
        unsigned long long t = 1000;
        Expect(!z.Step(false, true, false, t), "tick: nothing stowed, no tick");
        Expect(!z.Step(true, false, false, t += 20), "tick: stowed, hand outside, no tick");
        Expect(z.Step(true, true, false, t += 20), "tick: the hand enters the zone with a weapon stowed: one tick");
        Expect(!z.Step(true, true, false, t += 20), "tick: staying inside is silent");
        Expect(!z.Step(true, true, false, t += 500), "tick: still silent a long while later");
        Expect(!z.Step(true, false, false, t += 20), "tick: leaving is silent");
        Expect(z.Step(true, true, false, t += 400), "tick: the next entry ticks again");
        // a hand shivering on the edge ticks at most once per 300 ms
        Expect(!z.Step(true, false, false, t += 10), "tick: out again");
        Expect(!z.Step(true, true, false, t += 10), "tick: back in 20 ms after the last one: too soon");
        Expect(!z.Step(true, false, false, t += 10), "tick: out");
        Expect(z.Step(true, true, false, t += 400), "tick: and an entry after the gap ticks");
        // a potion in the hand, or the weapon leaving the back, never ticks; a hand already inside when the weapon goes there ticks once
        ZoneTick p;
        Expect(!p.Step(true, true, true, 5000), "tick: a hand with a potion does not tick");
        Expect(p.Step(true, true, false, 5100), "tick: the potion gone, the hand inside: an entry");
        ZoneTick w;
        Expect(!w.Step(false, true, false, 6000), "tick: hand inside, nothing stowed");
        Expect(w.Step(true, true, false, 6020), "tick: the weapon goes to the back with the hand in the zone: one tick");
        Expect(!w.Step(false, true, false, 6040) && !w.Step(false, false, false, 6060), "tick: the weapon taken: silent");
    }
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("shoulder_grip_test: all 32 combinations and the zone tick passed\n");
    return 0;
}
