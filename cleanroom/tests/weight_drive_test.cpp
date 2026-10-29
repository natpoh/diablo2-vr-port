// Offline checks of which hand swings a weapon that has weight (vr/weight_drive.h), with the swing engine (vr/stroke_contact.h):
// the hand that carries it drives the stroke, a weapon nobody holds strikes nothing. No game, no headset.
// Run: build/Release/weight_drive_test.exe   (exit code 0 = all passed)

#include "../../vr/stroke_contact.h"
#include "../../vr/weight_drive.h"

#include <cmath>
#include <cstdio>

using namespace stroke;

static int g_failures = 0;

static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

static void DriveTests() {
    using meleetouch::DrivingHand;
    // [weight] off or no weapon held that way: the slot's hand, as ever
    Expect(DrivingHand(false, -1, 0).strikes && DrivingHand(false, -1, 0).hand == 0, "no weight: the right slot's hand");
    Expect(DrivingHand(false, -1, 1).strikes && DrivingHand(false, -1, 1).hand == 1, "no weight: the left slot's hand");
    Expect(DrivingHand(false, 1, 0).hand == 0, "no weight: a carrying hand means nothing");
    // with weight: the carrying hand, whichever slot the game has it in
    Expect(DrivingHand(true, 0, 0).strikes && DrivingHand(true, 0, 0).hand == 0, "weight: right carries, right slot");
    Expect(DrivingHand(true, 1, 0).strikes && DrivingHand(true, 1, 0).hand == 1, "weight: passed to the left hand, the left drives");
    Expect(DrivingHand(true, 0, 1).strikes && DrivingHand(true, 0, 1).hand == 0, "weight: the left slot's weapon in the right hand");
    // let go: it strikes nothing
    Expect(!DrivingHand(true, -1, 0).strikes, "weight: let go, nothing strikes");
    Expect(!DrivingHand(true, -1, 1).strikes, "weight: let go from the left slot, nothing strikes");
    Expect(DrivingHand(true, 2, 0).strikes == false, "weight: a carrying hand out of range is nobody");
}

// What the weight model holds for the hands the game reports (meleetouch::WeightHolds, vrcam PushArms). The kinds are heft::hold::Kind values; -1 is what
// vrcam's kindOf gives a bow, a crossbow, a claw, an orb, a shield and a quiver - none of them a body of the weight model.
static void HoldsTests() {
    using meleetouch::HeldAlone;
    using meleetouch::WeightHolds;
    constexpr int kSword = 0, kAxe = 1;
    // A bow with its quiver (0.178, a player: "the bow doesn't work"; the game: "hands right 'sbw ' left 'aqv '"): the quiver is in the left slot but
    // is no weapon (no D2RVR_LEFT_WEAPON), so the bow is "alone"; the bow's kind is none - the weight model holds nothing, neither hand is its.
    {
        const bool alone = HeldAlone(false, true);
        const meleetouch::Holds h = WeightHolds(true, -1, alone, -1);
        Expect(alone, "bow + quiver: the bow is alone (the quiver is no weapon)");
        Expect(!h.on && !h.second && h.kind == -1 && h.secondKind == -1, "bow + quiver: the weight model holds nothing");
    }
    // ... and if the game ever reported the quiver as a weapon of the left slot (not alone), still nothing: the bow has no kind
    {
        const meleetouch::Holds h = WeightHolds(true, -1, HeldAlone(true, true), -1);
        Expect(!h.on && !h.second, "bow + a left 'weapon' with no kind: nothing held");
    }
    // a crossbow with its bolts: the same
    Expect(!WeightHolds(true, -1, HeldAlone(false, true), -1).on, "crossbow + bolts: nothing held");
    // a sword alone: held, no second
    {
        const meleetouch::Holds h = WeightHolds(true, kSword, HeldAlone(false, true), -1);
        Expect(h.on && !h.second && h.kind == kSword, "a sword alone: held");
    }
    // a sword and a shield (no weapon on the left): held, no second
    Expect(WeightHolds(true, kSword, HeldAlone(false, true), -1).on, "a sword and a shield: held");
    // a weapon in each hand: both bodies
    {
        const meleetouch::Holds h = WeightHolds(true, kSword, HeldAlone(true, true), kAxe);
        Expect(h.on && h.second && h.kind == kSword && h.secondKind == kAxe, "a sword and an axe: each a body");
    }
    // a melee weapon beside a second slot the model cannot hold (a claw): not held at all, as before 0.178
    Expect(!WeightHolds(true, kSword, HeldAlone(true, true), -1).on, "a sword beside a claw: not held");
    // not wanted ([weight] off, not F4): nothing, whatever is in the hands
    Expect(!WeightHolds(false, kSword, true, -1).on && !WeightHolds(false, kSword, false, kAxe).second, "not wanted: nothing held");
}

// A sword carried by the LEFT hand while the right hand (the game's slot) is held still: the stroke is the left hand's.
struct Hands {
    Engine eng[2];
    double t = 1.0;
    Vec p[2] = {{0.4f, 1.0f, 0.0f}, {-0.4f, 1.0f, 0.0f}};
    unsigned sweeps[2] = {0, 0};

    // dx: the carrying hand's move this frame. `placed` / `carrying` as weight:: says them; slot = 0 (right).
    void Frame(bool placed, int carrying, Vec move) {
        const meleetouch::Drive d = meleetouch::DrivingHand(placed, carrying, 0);
        t += 1.0 / 90.0;
        if (!d.strikes) {   // nothing swings it: no sample reaches an engine, and what was under way is dropped
            eng[0].Reset();
            eng[1].Reset();
            return;
        }
        p[d.hand] = p[d.hand] + move;
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 100u + static_cast<uint32_t>(d.hand);
        s.hand = {p[d.hand], {}};
        s.blade = {p[d.hand], p[d.hand] + Vec{0.0f, 0.0f, -0.65f}, 0.065f};
        s.anchor = {p[d.hand].x, 1.0f, 0.0f};
        s.scale = 4.0f;
        Sweep sw;
        if (eng[d.hand].Step(s, sw)) ++sweeps[d.hand];
    }
};

static void PassTests() {
    const Vec fast{2.0f / 90.0f, 0.0f, 0.0f};   // 2 m/s
    Hands h;
    // the right hand carries it and swings
    for (int i = 0; i < 40; ++i) h.Frame(true, 0, fast);
    Expect(h.sweeps[0] > 5, "right carries: its swing makes a stroke", h.sweeps[0]);
    Expect(h.sweeps[1] == 0, "right carries: the left hand makes none", h.sweeps[1]);
    // passed to the left: from now on the left hand drives, the right one's motion is nothing
    unsigned rightBefore = h.sweeps[0];
    for (int i = 0; i < 40; ++i) h.Frame(true, 1, fast);
    Expect(h.sweeps[1] > 5, "left carries: its swing makes a stroke", h.sweeps[1]);
    Expect(h.sweeps[0] == rightBefore, "left carries: the right hand's engine got no more samples", h.sweeps[0]);
    // let go: it lies; nobody's swing strikes, whatever the hands do
    unsigned left = h.sweeps[1];
    for (int i = 0; i < 40; ++i) h.Frame(true, -1, fast);
    Expect(h.sweeps[0] == rightBefore && h.sweeps[1] == left, "let go: no stroke at all");
    // taken up again by the right hand: a new stroke needs fresh samples (the engines were reset)
    unsigned after = 0;
    for (int i = 0; i < 40; ++i) {
        const unsigned was = h.sweeps[0];
        h.Frame(true, 0, {-fast.x, 0, 0});
        after += h.sweeps[0] - was;
    }
    Expect(after > 5, "taken up again: strokes come back", after);
}

int main() {
    DriveTests();
    HoldsTests();
    PassTests();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weight_drive_test: all passed\n");
    return 0;
}
