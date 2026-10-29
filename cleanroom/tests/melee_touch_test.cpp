// Offline checks of melee by touch's shapes (vr/melee_bodies.h) and of the whole detector on synthetic poses:
// a swinging hand + the swing engine (vr/stroke_contact.h) + monster capsules built from MonStats-like bytes.
// No game, no headset. Run: build/Release/melee_touch_test.exe   (exit code 0 = all passed)

#include "../../vr/blade_shape.h"
#include "../../vr/melee_bodies.h"
#include "../../vr/melee_press.h"
#include "../../vr/room_pose.h"
#include "../../vr/stroke_tuning.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace stroke;
using namespace meleetouch;

static int g_failures = 0;

static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static void ShapeTests() {
    Expect(Near(WeaponBlade(D2RVR_TYPE_DAGGER, false).length, 0.28f, 1e-6f), "dagger 0.28 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_CLAW, false).length, 0.28f, 1e-6f), "claw 0.28 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_WAND, false).length, 0.38f, 1e-6f), "wand 0.38 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_STAFF, true).length, 1.2f, 1e-6f), "staff 1.2 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_SPEAR, false).length, 1.5f, 1e-6f), "spear 1.5 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_POLEARM, true).length, 1.5f, 1e-6f), "polearm 1.5 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_SWORD, false).length, 0.65f, 1e-6f), "one-handed sword 0.65 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_SWORD, true).length, 1.1f, 1e-6f), "two-handed sword 1.1 m");
    Expect(Near(WeaponBlade(D2RVR_TYPE_UNARMED, false).length, 0.04f, 1e-6f) && Near(WeaponBlade(D2RVR_TYPE_UNARMED, false).radius, 0.08f, 1e-6f),
           "a fist: 0.04 m long, 0.08 m thick");
    Expect(MeleeKind(D2RVR_TYPE_SWORD) && MeleeKind(D2RVR_TYPE_UNARMED) && MeleeKind(D2RVR_TYPE_STAFF), "swung kinds");
    Expect(!MeleeKind(D2RVR_TYPE_BOW) && !MeleeKind(D2RVR_TYPE_CROSSBOW) && !MeleeKind(D2RVR_TYPE_JAVELIN) &&
           !MeleeKind(D2RVR_TYPE_THROWING) && !MeleeKind(D2RVR_TYPE_UNKNOWN), "kinds with their own gestures or unknown are not swung");
    // the thrown kinds are swung only by the thrown switch (bladeshape::TouchKind): a javelin, a throwing knife or axe - and the potions filed with them
    Expect(ThrownKind(D2RVR_TYPE_JAVELIN) && ThrownKind(D2RVR_TYPE_THROWING), "thrown kinds: javelin and throwing");
    Expect(!ThrownKind(D2RVR_TYPE_SWORD) && !ThrownKind(D2RVR_TYPE_BOW) && !ThrownKind(D2RVR_TYPE_CROSSBOW) && !ThrownKind(D2RVR_TYPE_SPEAR) && !ThrownKind(D2RVR_TYPE_UNKNOWN), "thrown kinds: nothing else");
    for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k)
        Expect(!(MeleeKind(k) && ThrownKind(k)), "thrown kinds: never both a swung kind and a thrown one", k);

    // the ground shear round trip
    const Vec grid = GridPlane(RenderGround({3.0f, 0.0f, 5.0f}, 2.0f));
    Expect(Near(grid.x, 3.0f, 1e-4f) && Near(grid.z, 5.0f, 1e-4f), "shear: grid -> render -> grid");

    // a monster body: a 2-cell footprint, 80 px tall, 4 game units to the metre, on the flat
    Body b;
    Expect(BodyFor({2, 2, 80}, 3.0f, 0.0f, 0.0f, 4.0f, b), "body: a row that reads");
    Expect(Near(b.radius, 0.40f, 1e-4f), "body: radius = footprint 0.35 + 0.05", b.radius);
    Expect(Near(b.bottom.y, 0.40f, 1e-4f) && Near(b.top.y, 1.60f, 1e-4f), "body: from 0.4 to 1.6 m", b.top.y);
    Expect(Near(b.bottom.x, 0.75f, 1e-4f) && Near(b.bottom.z, 0.0f, 1e-4f), "body: 3 game units = 0.75 m", b.bottom.x);
    // on ground 1 m up the render place is sheared
    Expect(BodyFor({2, 2, 80}, 3.0f, 0.0f, 1.0f, 4.0f, b) && Near(b.bottom.x, 0.75f + kGroundShear, 1e-4f) && Near(b.bottom.y, 1.4f, 1e-4f),
           "body: sheared on raised ground");
    // no height byte: 72 px; a huge footprint is capped by the height
    Expect(BodyFor({1, 1, 0}, 0.0f, 0.0f, 0.0f, 4.0f, b) && Near(b.top.y, 72 * 0.10f / 4.0f - b.radius, 1e-4f), "body: 72 px when the row has none");
    Expect(BodyFor({8, 8, 40}, 0.0f, 0.0f, 0.0f, 4.0f, b) && Near(b.radius, 40 * 0.10f / 4.0f * 0.45f + 0.05f, 1e-4f), "body: the radius is capped by the height");
    // never an inverted capsule
    Expect(BodyFor({8, 8, 10}, 0.0f, 0.0f, 0.0f, 4.0f, b) && b.top.y >= b.bottom.y, "body: a short wide one is not upside down");
    // rows that do not read
    Expect(!BodyFor({0, 2, 80}, 0, 0, 0, 4.0f, b), "body: footprint 0");
    Expect(!BodyFor({9, 2, 80}, 0, 0, 0, 4.0f, b), "body: footprint 9");
    Expect(!BodyFor({2, 2, 80}, 0, 0, 0, 0.0f, b), "body: no scale");
    Expect(!BodyFor({2, 2, 80}, std::nanf(""), 0, 0, 4.0f, b), "body: NaN place");
}

// The room pose back from the shared block's head-turn frame: the forward direction is XrIntoShared's (vr/vrcam.cpp), copied.
static void RoomTests() {
    const float head[3] = {1.5f, 1.7f, -2.0f};
    const float hands[3][3] = {{1.9f, 1.2f, -2.4f}, {0.7f, 1.0f, -1.1f}, {1.5f, 2.0f, -3.0f}};
    for (const float yawDeg : {0.0f, 33.0f, -110.0f, 179.0f}) {
        const float yaw = yawDeg * 0.0174532925f;
        for (const auto& g : hands) {
            // a room orientation: some turn about each axis
            const Quat qa = {0.0f, std::sin(0.4f), 0.0f, std::cos(0.4f)}, qb = {std::sin(0.25f), 0.0f, 0.0f, std::cos(0.25f)};
            const Quat room = Multiply(qa, qb);
            // XrIntoShared's put()
            const float c = std::cos(yaw), sn = std::sin(yaw), qs = std::sin(-yaw * 0.5f), qc = std::cos(-yaw * 0.5f);
            const float dx = g[0] - head[0], dy = g[1] - head[1], dz = g[2] - head[2];
            const float pos[3] = {dx * c - dz * sn, dy, dx * sn + dz * c};
            const float rot[4] = {qc * room.x + qs * room.z, qc * room.y + qs * room.w, qc * room.z - qs * room.x, qc * room.w - qs * room.y};
            const Pose back = RoomPose(head, yaw, pos, rot);
            Expect(Near(back.p.x, g[0], 1e-4f) && Near(back.p.y, g[1], 1e-4f) && Near(back.p.z, g[2], 1e-4f), "room pose: the place comes back", yawDeg);
            Expect(RotationDistance(back.q, room) < 1e-3f, "room pose: the orientation comes back", yawDeg);
        }
    }
    // a hand held still in the room while the head turns: the room pose does not move, though the shared block's does
    const float g[3] = {1.9f, 1.2f, -2.4f};
    Pose first{}, second{};
    Vec shared[2];
    for (int k = 0; k < 2; ++k) {
        const float yaw = (k ? 1.2f : 0.2f);
        const float c = std::cos(yaw), sn = std::sin(yaw);
        const float dx = g[0] - head[0], dy = g[1] - head[1], dz = g[2] - head[2];
        const float pos[3] = {dx * c - dz * sn, dy, dx * sn + dz * c};
        const float qs = std::sin(-yaw * 0.5f), qc = std::cos(-yaw * 0.5f);
        const float rot[4] = {0.0f, qs, 0.0f, qc};   // XrIntoShared's rotation for a room orientation of identity
        (k ? second : first) = RoomPose(head, yaw, pos, rot);
        shared[k] = {pos[0], pos[1], pos[2]};
    }
    Expect(Length(shared[1] - shared[0]) > 0.3f, "room pose: the head-frame hand does move with the head's turn", Length(shared[1] - shared[0]));
    Expect(Length(first.p - second.p) < 1e-4f, "room pose: a still hand stays still while the head turns");
}

// A swing of a one-handed sword along +z across a monster that stands in front of the hero (+x), and not across one behind.
struct Monster { Body body; bool touched = false; bool pressed = false; };

static void SwingTest() {
    const float upm = 4.0f;
    Engine eng;
    HitSet hits;
    std::vector<Monster> ms(3);
    BodyFor({2, 2, 80}, 3.0f, 0.0f, 0.0f, upm, ms[0].body);     // in front: 0.75 m ahead (x)
    BodyFor({2, 2, 80}, -3.0f, 0.0f, 0.0f, upm, ms[1].body);    // behind
    BodyFor({2, 2, 80}, 3.0f, 12.0f, 0.0f, upm, ms[2].body);    // in front but 3 m to the side: out of the swing
    const BladeShape shape = WeaponBlade(D2RVR_TYPE_SWORD, false);

    // the hand sweeps z from -0.5 to +0.6 m at 2.2 m/s, 90 Hz, 1.0 m up, 0.3 m ahead; the blade points ahead (+x)
    double t = 1.0;
    Vec hand{0.3f, 1.0f, -0.5f};
    unsigned sweeps = 0, presses = 0;
    uint64_t firstStroke = 0;
    for (int i = 0; i < 60; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        s.hand = {hand, {}};
        s.blade = {hand, hand + Vec{shape.length, 0.0f, 0.0f}, shape.radius};
        s.anchor = {0.0f, 1.0f, 0.0f};
        s.scale = upm;
        Sweep sw;
        if (eng.Step(s, sw)) {
            ++sweeps;
            if (!firstStroke) firstStroke = sw.stroke;
            Expect(sw.stroke == firstStroke, "swing: one stroke");
            // the first monster the blade meets on this sweep
            int best = -1;
            float bestT = 2.0f;
            for (size_t m = 0; m < ms.size(); ++m) {
                const float tt = TouchTime(sw.before, sw.after, ms[m].body);
                if (tt >= 0.0f) ms[m].touched = true;
                if (tt >= 0.0f && tt < bestT && !hits.Contains(1, sw.stroke, static_cast<uint32_t>(m))) { best = static_cast<int>(m); bestT = tt; }
            }
            if (best >= 0 && hits.Insert(1, sw.stroke, static_cast<uint32_t>(best))) { ms[static_cast<size_t>(best)].pressed = true; ++presses; }
        }
        t += 1.0 / 90.0;
        hand.z += 2.2f / 90.0f;
    }
    Expect(sweeps > 10, "swing: the engine made sweeps", sweeps);
    Expect(ms[0].touched, "swing: the monster in front is touched");
    Expect(!ms[1].touched, "swing: the monster behind is not");
    Expect(!ms[2].touched, "swing: the monster 3 m to the side is not");
    Expect(presses == 1 && ms[0].pressed, "swing: one press, for the monster in front, once per stroke", presses);
}

// The same hand at rest or waved slowly with the blade inside the monster presses nothing.
static void RestTest() {
    const float upm = 4.0f;
    Engine eng;
    Body body;
    BodyFor({2, 2, 80}, 3.0f, 0.0f, 0.0f, upm, body);
    double t = 1.0;
    Vec hand{0.3f, 1.0f, 0.0f};   // the blade lies inside the monster
    unsigned sweeps = 0;
    for (int i = 0; i < 90; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        s.hand = {hand, {}};
        s.blade = {hand, hand + Vec{0.65f, 0.0f, 0.0f}, 0.065f};
        s.anchor = {0.0f, 1.0f, 0.0f};
        s.scale = upm;
        Sweep sw;
        if (eng.Step(s, sw)) ++sweeps;
        t += 1.0 / 90.0;
        hand.z += (i % 2 ? 0.3f : -0.3f) / 90.0f;   // a fidget at 0.3 m/s
    }
    Expect(sweeps == 0, "rest: a blade held inside a monster and fidgeted presses nothing", sweeps);
}

// F4 only (vr/view_gate.h): the press and the window the game's lookups are answered in, and what a view change does to them.
static void PressTests() {
    Press p;
    Expect(!p.Down() && !p.Pending(1000) && p.Target(1000) == 0, "press: nothing under way");
    Expect(p.Begin(7, 1000), "press: a touch accepted puts the press down");
    Expect(p.Down() && p.Pending(1100) && p.Target(1100) == 7, "press: the window answers with the touched monster");
    Expect(!p.Begin(8, 1030), "press: a second touch inside the press does not press again");
    Expect(p.Target(1100) == 8, "press: ... and the window follows the new monster");
    Expect(!p.Release(1090, false) && p.Down(), "press: held for its 70 ms (from the last touch)");
    Expect(p.Release(1100, false) && !p.Down(), "press: let go when they are over");
    Expect(p.Pending(1370) && !p.Pending(1380), "press: the window is 350 ms from the last touch");
    Expect(!p.Release(1200, true), "press: nothing to let go twice");

    // another view in the middle of a touch: the press up at once, the window closed - nothing is answered
    Press q;
    q.Begin(9, 2000);
    Expect(q.Down() && q.Pending(2010), "view: a touch under way");
    q.CloseWindow();
    Expect(!q.Pending(2010) && q.Target(2010) == 0, "view: the window is closed at once - the game's lookups are its own");
    Expect(q.Release(2010, true) && !q.Down(), "view: the press is let go by force, not at its time");
    Expect(!q.Release(2011, true), "view: ... once");
    // a window nobody asked about, closed with no press down (the press had run out first)
    Press r;
    r.Begin(10, 3000);
    r.Release(3100, false);
    r.CloseWindow();
    Expect(!r.Pending(3101), "view: a window left after the press is closed too");
    // back in F4: it starts fresh
    Expect(r.Begin(11, 4000) && r.Down() && r.Target(4001) == 11, "view: back in F4 a new touch presses and opens its window");

    // [hands] double_swing_by_hands: the second hand's touch is a skill pressed on the pad, not the attack key - only the window opens, nothing goes down
    Press d;
    d.OpenWindow(12, 5000);
    Expect(!d.Down() && d.Pending(5100) && d.Target(5100) == 12, "dual: the window answers with the touched monster, no press of the attack key");
    Expect(d.Pending(5349) && !d.Pending(5350), "dual: ... for 350 ms");
    Expect(!d.Release(5100, true), "dual: nothing to let go (the pad's pulse lets itself go)");
    d.CloseWindow();
    Expect(!d.Pending(5100), "dual: another view closes this window as well");
    d.OpenWindow(13, 6000);
    Expect(d.Begin(14, 6100) && d.Down() && d.Target(6101) == 14, "dual: a normal touch after it presses as ever and takes the window");
}

// The least speed end to end ([gameplay] melee_min_speed): the same swing across a monster at different speeds, the engine set the way the
// ini sets it (stroke::ParamsFor). A touch presses only when the swing's speed is at or above the least speed; the very number the engine
// reports (LastSpeed) and the flag it raises (Counting) are the ones the touch's gate and the drawing's colour use.
static unsigned PressesAt(float speed, float minSpeed, float startCm, bool* everCounting = nullptr, float* peakSeen = nullptr) {
    const float upm = 4.0f;
    Engine eng(ParamsFor({minSpeed, startCm}));
    HitSet hits;
    Monster m;
    BodyFor({2, 2, 80}, 3.0f, 0.0f, 0.0f, upm, m.body);   // in front of the swing
    const BladeShape shape = WeaponBlade(D2RVR_TYPE_SWORD, false);
    double t = 1.0;
    Vec hand{0.3f, 1.0f, -0.6f};
    unsigned presses = 0;
    const int steps = static_cast<int>(1.4 / (speed / 90.0)) + 2;   // 1.4 m of travel at 90 Hz
    for (int i = 0; i < steps && i < 1000; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        s.hand = {hand, {}};
        s.blade = {hand, hand + Vec{shape.length, 0.0f, 0.0f}, shape.radius};
        s.anchor = {0.0f, 1.0f, 0.0f};
        s.scale = upm;
        Sweep sw;
        const bool got = eng.Step(s, sw);
        if (everCounting && eng.Counting()) *everCounting = true;
        if (peakSeen) *peakSeen = std::max(*peakSeen, eng.LastSpeed());
        if (got && TouchTime(sw.before, sw.after, m.body) >= 0.0f && hits.Insert(1, sw.stroke, 1u)) ++presses;
        t += 1.0 / 90.0;
        hand.z += speed / 90.0f;
    }
    return presses;
}

static void SpeedGateTest() {
    // a swing of 2.2 m/s hits with the default 0.8; a gentle 0.5 m/s poke does not (it never even begins a stroke)
    Expect(PressesAt(2.2f, 0.8f, 8.0f) == 1, "least speed 0.8: a 2.2 m/s swing hits once");
    Expect(PressesAt(0.5f, 0.8f, 8.0f) == 0, "least speed 0.8: a 0.5 m/s poke does nothing");
    // lowered to 0.3 (start 4 cm): the poke hits - this is the user's "I just prodded the enemy and it took damage"
    Expect(PressesAt(0.5f, 0.3f, 4.0f) == 1, "least speed 0.3: the same 0.5 m/s poke hits");
    // raised: a 2.2 m/s swing hits with 2.0, does not with 3.0; a 4 m/s swing hits with 3.0
    Expect(PressesAt(2.2f, 2.0f, 8.0f) == 1, "least speed 2.0: a 2.2 m/s swing hits");
    Expect(PressesAt(2.2f, 3.0f, 8.0f) == 0, "least speed 3.0: the 2.2 m/s swing does not");
    Expect(PressesAt(4.0f, 3.0f, 8.0f) == 1, "least speed 3.0: a 4 m/s swing hits");
    // the colour's flag: raised while the swing is at or above the least speed, never below it
    bool counting = false;
    float peak = 0.0f;
    PressesAt(2.2f, 3.0f, 8.0f, &counting, &peak);
    Expect(!counting && Near(peak, 2.2f, 0.1f), "least speed 3.0: a 2.2 m/s swing never counts (the blade stays cyan) and the engine reads 2.2", peak);
    counting = false;
    PressesAt(2.2f, 2.0f, 8.0f, &counting, &peak);
    Expect(counting, "least speed 2.0: the 2.2 m/s swing counts (the blade turns green)");
    // once per stroke, whatever the threshold: a long slow stroke at 1 m/s through the monster presses once with a low least speed
    Expect(PressesAt(1.0f, 0.4f, 4.0f) == 1, "once per stroke: a slow stroke with a low least speed presses once");
    // the start distance: 30 cm within 0.12 s needs 2.5 m/s - a 2.2 m/s swing never starts a stroke then, a 4 m/s one does and hits
    Expect(PressesAt(2.2f, 0.8f, 30.0f) == 0, "start 30 cm: a 2.2 m/s swing (26 cm in 0.12 s) is not a stroke");
    Expect(PressesAt(4.0f, 0.8f, 30.0f) == 1, "start 30 cm: a 4 m/s swing is, and hits");
    Expect(PressesAt(2.2f, 0.8f, 8.0f) == 1 && PressesAt(2.2f, 0.8f, 2.0f) == 1, "start 8 and 2 cm: the 2.2 m/s swing hits");
}

// A stab along +x or -x with a blade (a thrown weapon's own shape from bladeshape::ForItem: tip ahead of the hand, butt behind it), the hand 1 m up,
// the engine as the ini sets it: the hand moves `distance` m at `speed` from startX, then stops. How many presses the stab makes on a monster whose
// footprint stands at game-unit x monsterGridX (4 game units to the metre). The blade is laid along the hand's way, as a held javelin is.
static unsigned StabPresses(const bladeshape::Shape& sh, float dir, float startX, float distance, float speed, float monsterGridX) {
    const float upm = 4.0f;
    Engine eng(ParamsFor({0.8f, 8.0f}));
    HitSet hits;
    Monster m;
    BodyFor({2, 2, 80}, monsterGridX, 0.0f, 0.0f, upm, m.body);
    double t = 1.0;
    Vec hand{startX, 1.0f, 0.0f};
    unsigned presses = 0;
    const int moving = static_cast<int>(distance / (speed / 90.0f));
    for (int i = 0; i < moving + 20 && i < 1000; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        s.hand = {hand, {}};
        const Vec axis{1.0f, 0.0f, 0.0f};   // the tip points ahead (+x) whichever way the hand goes
        s.blade = {hand - axis * sh.butt, hand + axis * sh.length, sh.radius};
        s.anchor = {0.0f, 1.0f, 0.0f};
        s.scale = upm;
        s.lever = sh.length;
        Sweep sw;
        if (eng.Step(s, sw) && TouchTime(sw.before, sw.after, m.body) >= 0.0f && hits.Insert(1, sw.stroke, 1u)) ++presses;
        t += 1.0 / 90.0;
        if (i < moving) hand.x += dir * speed / 90.0f;
    }
    return presses;
}

// Thrown weapons by touch: a stab with a javelin, a throwing knife and a throwing axe is a stroke like a sword's, once per stroke, through the same engine.
static void ThrownStabTest() {
    const uint32_t jav = bladeshape::Code('j', 'a', 'v', ' '), tkf = bladeshape::Code('t', 'k', 'f', ' '), tax = bladeshape::Code('t', 'a', 'x', ' ');
    const bladeshape::Shape js = bladeshape::ForItem(jav, D2RVR_TYPE_JAVELIN, false), ks = bladeshape::ForItem(tkf, D2RVR_TYPE_THROWING, false),
                            as = bladeshape::ForItem(tax, D2RVR_TYPE_THROWING, false);
    Expect(StabPresses(js, 1.0f, -0.4f, 1.0f, 2.5f, 4.0f) == 1, "stab: a javelin thrust at a monster 1 m ahead hits once");
    Expect(StabPresses(js, 1.0f, -0.4f, 1.0f, 0.4f, 4.0f) == 0, "stab: a javelin nudged at 0.4 m/s does nothing (the least speed is the engine's, as for any weapon)");
    Expect(StabPresses(js, 1.0f, -0.4f, 1.0f, 2.5f, 20.0f) == 0, "stab: a monster 5 m ahead is out of a javelin's reach");
    Expect(StabPresses(ks, 1.0f, -0.4f, 1.0f, 2.5f, 4.0f) == 1, "stab: a throwing knife thrust at a monster 1 m ahead hits once");
    Expect(StabPresses(as, 1.0f, -0.4f, 1.0f, 2.5f, 4.0f) == 1, "stab: a throwing axe thrust at a monster 1 m ahead hits once");
    // the javelin reaches farther than a knife: a monster whose near edge is 1.35 m ahead (the hand ends 0.6 m ahead): the javelin's head (1.04 m) gets there, the knife's (0.4 m) does not
    Expect(js.length > ks.length + 0.3f, "stab: the javelin reaches farther than the knife", js.length - ks.length);
    Expect(StabPresses(js, 1.0f, -0.4f, 1.0f, 2.5f, 7.0f) == 1 && StabPresses(ks, 1.0f, -0.4f, 1.0f, 2.5f, 7.0f) == 0,
           "stab: a monster 1.35 m ahead is reached by a javelin's tip and not by a knife's");
    // the measured javelin (the head 1.04 m from the bone) reaches a monster whose near edge is 1.5 m ahead when the hand ends 0.6 m ahead (the point at 1.64 m); the first try (0.8 m) did not
    bladeshape::Shape firstTry = js;
    firstTry.length = 0.80f;
    firstTry.butt = 0.40f;
    Expect(StabPresses(js, 1.0f, -0.4f, 1.0f, 2.5f, 7.6f) == 1 && StabPresses(firstTry, 1.0f, -0.4f, 1.0f, 2.5f, 7.6f) == 0,
           "stab: a monster whose near edge is 1.5 m ahead is reached by the measured javelin (head 1.04 m) and was not by the first try (0.8 m)");
    // the butt behind the grip: a blade (the tip ahead) pulled BACK to 0.25 m with a monster behind, its near edge at -0.1 m: only a butt of 0.4 m behind the hand gets there
    bladeshape::Shape withButt = js, noButt = js;
    withButt.butt = 0.40f;
    noButt.butt = 0.0f;
    Expect(StabPresses(withButt, -1.0f, 1.0f, 0.75f, 2.5f, -2.0f) == 1, "stab: the butt of a blade behind the grip touches a monster behind the hand");
    Expect(StabPresses(noButt, -1.0f, 1.0f, 0.75f, 2.5f, -2.0f) == 0, "stab: ... and with no butt the same pull touches nothing");
}

// The speed that counts is the blade's where it touches ([gameplay] melee_min_speed, stroke::ContactSpeed): a blade 1 m long turning about its base at
// 4 rad/s moves 4 m/s at the tip and 1.2 m/s 0.3 m from the grip. Two thin posts stand on its arc, one 0.3 m from the grip and one 0.9 m; the engine is
// fed the blade's ends in the room (Sample::room) and the touch loop is the one melee_touch.cpp runs: a touch counts when the blade's speed AT the
// contact is at or above the least speed, else it waits (the post is not used up).
static unsigned ContactPresses(float omega, float least, bool nearPost, bool farPost, bool handTurnsToo, unsigned* slow = nullptr) {
    const float upm = 4.0f;
    Engine eng(ParamsFor({least, 8.0f}));
    HitSet hits;
    const float length = 1.0f;
    std::vector<Body> posts;
    auto post = [&](float radius, float angle) {
        const float x = radius * std::cos(angle), z = radius * std::sin(angle);
        return Body{{x, 0.3f, z}, {x, 1.8f, z}, 0.05f};
    };
    std::vector<unsigned> ids;
    if (nearPost) { posts.push_back(post(0.3f, 0.35f)); ids.push_back(1); }
    if (farPost) { posts.push_back(post(0.9f, 0.95f)); ids.push_back(2); }
    double t = 1.0;
    float angle = 0.0f;
    unsigned presses = 0;
    for (int i = 0; i < 70; ++i) {
        const Vec d{std::cos(angle), 0.0f, std::sin(angle)};
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        s.hand = {{0.0f, 1.0f, 0.0f}, handTurnsToo ? Quat{0.0f, std::sin(angle * 0.5f), 0.0f, std::cos(angle * 0.5f)} : Quat{}};
        s.blade = {{0.0f, 1.0f, 0.0f}, Vec{0.0f, 1.0f, 0.0f} + d * length, 0.02f};
        s.anchor = {0.0f, 1.0f, 0.0f};
        s.scale = upm;
        s.lever = length;
        s.room = s.blade;
        s.haveRoom = true;
        Sweep sw;
        if (eng.Step(s, sw)) {
            for (size_t k = 0; k < posts.size(); ++k) {
                const Contact c = TouchOf(sw, posts[k]);
                if (c.t < 0.0f || hits.Contains(1, sw.stroke, ids[k])) continue;
                if (c.speed < least) { if (slow) ++*slow; continue; }
                if (hits.Insert(1, sw.stroke, ids[k])) ++presses;
            }
        }
        t += 1.0 / 90.0;
        if (i >= 12) angle += omega / 90.0f;   // at rest for the first 12 samples, then it turns
    }
    return presses;
}

static void ContactGateTest() {
    // the blade turns at 4 rad/s: 4 m/s at the tip, 3.6 m/s at 0.9 m, 1.2 m/s at 0.3 m from the grip. Least speed 3.0: the far post counts, the near one does not
    unsigned slow = 0;
    Expect(ContactPresses(4.0f, 3.0f, true, false, true, &slow) == 0 && slow > 0, "contact speed: a post 0.3 m from the grip (1.2 m/s there) is not hit with the least speed at 3", slow);
    Expect(ContactPresses(4.0f, 3.0f, false, true, true) == 1, "contact speed: a post 0.9 m from the grip (3.6 m/s there) is hit");
    Expect(ContactPresses(4.0f, 3.0f, true, true, true) == 1, "contact speed: both on the arc: only the far one is hit, the near one waits");
    // the least speed 1.0: the near post counts too (1.2 m/s there), each once
    Expect(ContactPresses(4.0f, 1.0f, true, true, true) == 2, "contact speed: with the least speed at 1 both are hit, once each");
    // a slower turn: 2 rad/s is 1.8 m/s at 0.9 m - under 3.0, nothing; over 1.0, the far one
    Expect(ContactPresses(2.0f, 3.0f, true, true, true) == 0, "contact speed: a 2 rad/s turn hits nothing at the least speed 3");
    Expect(ContactPresses(2.0f, 1.0f, true, true, true) >= 1, "contact speed: ... but the far post at the least speed 1 (1.8 m/s there)");
    // the controller need not turn: the blade's own motion is what counts
    Expect(ContactPresses(4.0f, 3.0f, false, true, false) == 1, "contact speed: the controller still, the drawn blade turning: the far post is hit all the same");
}

int main() {
    PressTests();
    ShapeTests();
    RoomTests();
    SwingTest();
    RestTest();
    SpeedGateTest();
    ThrownStabTest();
    ContactGateTest();
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("melee_touch_test: all passed\n");
    return 0;
}
