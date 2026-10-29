// The string hand's closing (vr/bow_hand.h) on synthetic numbers, without the game and without a headset: what asks the fingers
// to close (the draw, the controller's own trigger and squeeze, the layout's grip), the amount that makes of the fist's bend, and
// the easing in and out.
//
//   bow_hand_test

#include "../../vr/bow_hand.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace bowhand;

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

bool Eq(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) < tol; }

constexpr float kDt = 1.0f / 90.0f;

// What the pose did before this change: the draw's word, else the layout's grip, and nothing else.
float Before(const Reading& r) { return r.drawing ? 1.0f : Clean(r.gameGrip); }

void TestSqueeze() {
    std::printf("what asks the hand to close\n");
    Reading r;
    EXPECT(SqueezeOf(r).k == 0.0f && SqueezeOf(r).source == Source::Rest, "nothing read, nothing squeezed");

    // the string taken: closed, whatever the controls read - the draw took them from the layout, so its grip says 0
    r = {};
    r.drawing = true;
    r.gameGrip = 0.0f;
    r.haveRaw = true;
    r.trigger = 0.0f;
    r.grip = 0.0f;
    EXPECT(SqueezeOf(r).k == 1.0f && SqueezeOf(r).source == Source::Draw, "the string taken closes the hand with every control reading 0");
    r = {};
    r.drawing = true;
    EXPECT(SqueezeOf(r).k == 1.0f && SqueezeOf(r).source == Source::Draw, "... and with no reading at all");

    // the controller's own squeeze: the stronger of the trigger and the grip
    r = {};
    r.haveRaw = true;
    r.trigger = 0.5f;
    r.gameGrip = 0.0f;
    EXPECT(Eq(SqueezeOf(r).k, 0.5f) && SqueezeOf(r).source == Source::Trigger, "the trigger half down: half closed, though the layout's grip reads 0");
    r.trigger = 0.3f;
    r.grip = 0.8f;
    EXPECT(Eq(SqueezeOf(r).k, 0.8f) && SqueezeOf(r).source == Source::Grip, "the grip stronger than the trigger: the grip");
    r.trigger = 0.0f;
    r.grip = 0.0f;
    EXPECT(SqueezeOf(r).k == 0.0f && SqueezeOf(r).source == Source::Rest, "both let go: relaxed");
    r.gameGrip = 0.9f;
    EXPECT(SqueezeOf(r).k == 0.0f, "a raw reading of 0 is a reading: the layout's grip is not asked behind it");

    // no raw reading (the draw switched off, or not F4): the layout's grip, as the fist of an empty hand has it
    r = {};
    r.gameGrip = 0.6f;
    EXPECT(Eq(SqueezeOf(r).k, 0.6f) && SqueezeOf(r).source == Source::GameGrip, "no raw reading: the layout's grip");
    r.gameGrip = -1.0f;
    EXPECT(SqueezeOf(r).k == 0.0f && SqueezeOf(r).source == Source::Rest, "no grip sent: relaxed");

    // garbage
    r = {};
    r.haveRaw = true;
    r.trigger = std::numeric_limits<float>::quiet_NaN();
    r.grip = std::numeric_limits<float>::infinity();
    EXPECT(SqueezeOf(r).k >= 0.0f && SqueezeOf(r).k <= 1.0f && std::isfinite(SqueezeOf(r).k), "NaN and infinity stay in 0..1");
    r.trigger = 7.0f;
    r.grip = 0.0f;
    EXPECT(SqueezeOf(r).k == 1.0f, "a value over 1 is 1");
}

void TestTheFault() {
    std::printf("the string hand that did not close\n");
    // The draw took the control it was started with from the layout, so the layout's grip reads 0 from the first frame of the take; the feed's
    // `drawing` was the only thing left, and a frame it was late (or stale) left the hand relaxed. The controller's own trigger, read raw,
    // keeps it closing.
    Reading r;
    r.drawing = false;         // the feed has not (yet) said the string is taken
    r.gameGrip = 0.0f;         // the layout sees the control as let go (taken by the draw)
    r.haveRaw = true;
    r.trigger = 1.0f;          // the finger is on the trigger
    EXPECT(Before(r) == 0.0f, "before: the hand stays relaxed with the feed late");
    EXPECT(SqueezeOf(r).k == 1.0f, "now: it closes with the trigger");

    // grip start: the grip is the draw's, the layout's reads 0 once taken
    r.trigger = 0.0f;
    r.grip = 1.0f;
    EXPECT(Before(r) == 0.0f && SqueezeOf(r).k == 1.0f, "the same with the grip");
}

void TestAmount() {
    std::printf("the fist's bend\n");
    EXPECT(Eq(Amount(0.0f, 0.20f, kHookDefault), 0.20f), "relaxed: the open amount");
    EXPECT(Eq(Amount(1.0f, 0.20f, kHookDefault), 0.85f), "hooked: the hook");
    EXPECT(Eq(Amount(0.5f, 0.20f, kHookDefault), 0.525f), "half: between");
    EXPECT(Eq(Amount(3.0f, 0.20f, kHookDefault), 0.85f) && Eq(Amount(-1.0f, 0.20f, kHookDefault), 0.20f), "k is held to 0..1");
    EXPECT(kHookDefault > 0.70f, "the default hook is stronger than the 0.70 that could not be seen");
    // the change the player sees from the string taken: a share of a full fist's bend (80, 95, 65 degrees at the knuckle, middle, tip)
    const float seen = Amount(1.0f, 0.20f, kHookDefault) - Amount(0.0f, 0.20f, kHookDefault);
    EXPECT(seen >= 0.6f, "the swing from relaxed to hooked is %.2f of a fist (the old one was 0.50)", seen);
    EXPECT(kThumbShare < 0.5f, "the thumb stays loose");
}

void TestEase() {
    std::printf("easing\n");
    Ease e;
    for (int i = 0; i < 4; ++i) e.Step(1.0f, kDt);
    EXPECT(e.v > 0.3f && e.v < 0.7f, "4 frames of a 90 Hz headset in: half way (%.2f)", e.v);
    for (int i = 0; i < 5; ++i) e.Step(1.0f, kDt);
    EXPECT(e.v == 1.0f && e.Settled(1.0f), "hooked in 80 ms (%.2f)", e.v);
    for (int i = 0; i < 8; ++i) e.Step(0.0f, kDt);
    EXPECT(e.v > 0.4f && e.v < 0.7f, "8 frames out: about half open (%.2f) - letting go is slower", e.v);
    for (int i = 0; i < 10; ++i) e.Step(0.0f, kDt);
    EXPECT(e.v == 0.0f && e.Settled(0.0f), "open in 180 ms");

    // partial squeeze
    e = {};
    for (int i = 0; i < 20; ++i) e.Step(0.4f, kDt);
    EXPECT(Eq(e.v, 0.4f) && e.Settled(0.4f), "a half squeeze settles at the squeeze");
    EXPECT(!e.Settled(0.9f), "and is not at 0.9");

    // a hitch is not a long swing
    e = {};
    e.v = 1.0f;
    e.Step(0.0f, 1.0f);
    EXPECT(e.v > 0.3f && e.v < 0.6f, "a one-second hitch lets go no further than 100 ms of it (%.2f)", e.v);

    // nothing to move by
    e = {};
    e.v = 0.5f;
    EXPECT(e.Step(1.0f, 0.0f) == 0.5f && e.Step(1.0f, -1.0f) == 0.5f && e.Step(1.0f, std::numeric_limits<float>::quiet_NaN()) == 0.5f,
           "no time, no move");
    EXPECT(e.Step(std::numeric_limits<float>::quiet_NaN(), kDt) <= 0.5f, "a NaN target is 0");
}

void TestCycle() {
    std::printf("a draw from start to release, the layout's grip reading 0 throughout\n");
    // 90 Hz: 0.5 s of relaxed hand, the trigger goes down over 0.1 s (the feed says the string is taken once it is over 0.55), held for
    // 0.5 s, let go over 0.05 s, 0.5 s after. The hand is where the fist's open and hook amounts put it.
    Ease e;
    const float open = 0.20f;
    float lowest = 1.0f, highest = 0.0f, lastBeforeTake = 0.0f;
    float amountAtTake = -1.0f, amountHeld = -1.0f, amountAfter = -1.0f;
    float trig = 0.0f;
    bool taken = false;
    const int total = (int)(2.0f / kDt);
    for (int i = 0; i < total; ++i) {
        const float t = i * kDt;
        if (t < 0.5f) trig = 0.0f;
        else if (t < 0.6f) trig = (t - 0.5f) / 0.1f;
        else if (t < 1.1f) trig = 1.0f;
        else if (t < 1.15f) trig = 1.0f - (t - 1.1f) / 0.05f;
        else trig = 0.0f;
        taken = t >= 0.5f && trig >= 0.55f && t < 1.1f;
        Reading r;
        r.drawing = taken;
        r.haveRaw = true;
        r.trigger = trig;
        r.gameGrip = 0.0f;
        const Squeeze s = SqueezeOf(r);
        const float used = e.Step(s.k, kDt);
        const float amount = Amount(used, open, kHookDefault);
        lowest = std::min(lowest, amount);
        highest = std::max(highest, amount);
        if (t < 0.5f) lastBeforeTake = amount;
        if (taken && amountAtTake < 0.0f) amountAtTake = amount;
        if (t > 0.9f && t < 1.05f) amountHeld = amount;
        if (t > 1.6f) amountAfter = amount;
    }
    EXPECT(Eq(lastBeforeTake, open), "relaxed before the take (%.2f)", lastBeforeTake);
    EXPECT(Eq(amountHeld, kHookDefault), "hooked while the string is held (%.2f)", amountHeld);
    EXPECT(Eq(amountAfter, open), "open again after the release (%.2f)", amountAfter);
    EXPECT(highest - lowest >= 0.6f, "the hand swings %.2f of a fist from relaxed to hooked", highest - lowest);
    EXPECT(amountAtTake < kHookDefault, "and closes over the frames, not at a jump (%.2f at the take)", amountAtTake);
}

}   // namespace

int main() {
    TestSqueeze();
    TestTheFault();
    TestAmount();
    TestEase();
    TestCycle();
    if (g_fails) {
        std::printf("%d FAILED\n", g_fails);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
