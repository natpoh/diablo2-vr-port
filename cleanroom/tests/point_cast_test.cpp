// [gameplay] teleport_circle (vr/point_cast.h) without the game and without a headset: a point skill's tap against its hold (what is taken out of the pad,
// when the arc and circle come up, a press cancelled by a menu), the arc's landing from the hand's pose (raised farther, lowered nearer, pointed down near),
// the cap, the skill list of the ini, and the ring drawn round the circle.
//
//   point_cast_test        every check below

#include "../../vr/point_cast.h"

#include <cmath>
#include <cstdio>

using namespace pointcast;

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

bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

// (the skill panel's numbering, wheel_types.h kSlotDefs) Teleport on X (slot 1), Meteor on LT + RB (slot 7), Blizzard on RT (slot 9), Hydra on LT + A (slot 10);
// Fire Bolt on B and on A (slot 8).
int SkillOf(int slot) { return slot == 1 ? kTeleport : slot == 7 ? 56 : slot == 9 ? 59 : slot == 10 ? 62 : slot == 0 || slot == 8 ? 36 : -1; }
bool IsListed(int skill) { return DefaultSkills().Has(skill); }

void Tap() {
    Hold h;
    Step s = h.Poll(0, 0, true, SkillOf, IsListed, 1000);
    EXPECT(!s.started && !s.strip, "nothing pressed");
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 1010);
    EXPECT(s.started && s.strip == kPadX && s.slot == 1 && s.skill == kTeleport, "X down: taken (%d %04X %d %d)", s.started, s.strip, s.slot, s.skill);
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 1150);
    EXPECT(s.strip == kPadX && !s.circle && !s.released, "held 140 ms: still taken, no arc");
    s = h.Poll(0, 0, true, SkillOf, IsListed, 1180);
    EXPECT(s.released && s.tap && !s.cancelled && !s.strip && s.skill == kTeleport, "let go at 170 ms: a tap (%d %d %d)", s.released, s.tap, s.cancelled);
    EXPECT(!h.Active(), "done");
}

void HoldIt() {
    Hold h;
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    Step s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 100);
    EXPECT(s.started, "down");
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 299);
    EXPECT(!s.circle && !h.CircleUp(), "199 ms: not yet");
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 300);
    EXPECT(s.circle && h.CircleUp() && s.strip == kPadX && h.Skill() == kTeleport, "200 ms: the arc (%d)", s.circle);
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 900);
    EXPECT(!s.circle && h.CircleUp() && s.strip == kPadX, "held on: once only, still taken");
    s = h.Poll(0, 0, true, SkillOf, IsListed, 950);
    EXPECT(s.released && !s.tap && !s.cancelled, "let go: the circle's cast");
    EXPECT(!h.CircleUp(), "the arc is down");
}

void Banks() {
    Hold h;
    h.Poll(0, 255, true, SkillOf, IsListed, 0);
    Step s = h.Poll(kPadRB, 255, true, SkillOf, IsListed, 10);
    EXPECT(s.started && s.slot == 7 && s.skill == 56 && s.strip == kPadRB, "LT + RB: Meteor, slot 7 (%d %d)", s.slot, s.skill);
    h.Reset();
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(kPadRB, 0, true, SkillOf, IsListed, 10);
    EXPECT(!s.started && !s.strip, "RB alone holds nothing: the game's");
    h.Reset();
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(kPadB, 0, true, SkillOf, IsListed, 10);
    EXPECT(!s.started && !s.strip, "B is Fire Bolt (not listed): the game's");
    h.Reset();
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    h.Poll(kPadX, 0, true, SkillOf, IsListed, 10);
    s = h.Poll(kPadX | kPadB, 0, true, SkillOf, IsListed, 20);
    EXPECT(s.strip == kPadX, "only X is taken: %04X", s.strip);
}

void Triggers() {
    // RT holds Blizzard: the trigger past 30 is the press, taken (the game sees it let go) while held, its slot 10
    Hold h;
    h.Poll(0, 0, 0, true, SkillOf, IsListed, 0);
    Step s = h.Poll(0, 0, 20, true, SkillOf, IsListed, 5);
    EXPECT(!s.started, "RT under the threshold: nothing");
    s = h.Poll(0, 0, 255, true, SkillOf, IsListed, 10);
    EXPECT(s.started && s.strip == kRt && s.slot == 9 && s.skill == 59, "RT pulled: Blizzard, taken (%d %X %d %d)", s.started, s.strip, s.slot, s.skill);
    s = h.Poll(0, 0, 255, true, SkillOf, IsListed, 300);
    EXPECT(s.circle && s.strip == kRt, "held: the arc, RT still taken");
    s = h.Poll(0, 0, 0, true, SkillOf, IsListed, 600);
    EXPECT(s.released && !s.tap && s.slot == 9, "let go: the circle's cast");
    // a tap of RT
    h.Reset();
    h.Poll(0, 0, 0, true, SkillOf, IsListed, 0);
    h.Poll(0, 0, 200, true, SkillOf, IsListed, 10);
    s = h.Poll(0, 0, 0, true, SkillOf, IsListed, 100);
    EXPECT(s.released && s.tap, "RT tapped: the gaze's");
    // LT + RT holds nothing here: the game's (its repeat casts as ever)
    h.Reset();
    h.Poll(0, 255, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(0, 255, 255, true, SkillOf, IsListed, 10);
    EXPECT(!s.started && !s.strip, "LT + RT (no point skill): the game's");
    // A holds Fire Bolt: the game's; LT + A holds Hydra: taken
    h.Reset();
    h.Poll(0, 0, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(kPadA, 0, 0, true, SkillOf, IsListed, 10);
    EXPECT(!s.started && !s.strip, "A (Fire Bolt): the game's");
    h.Reset();
    h.Poll(0, 255, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(kPadA, 255, 0, true, SkillOf, IsListed, 10);
    EXPECT(s.started && s.strip == kPadA && s.slot == 10 && s.skill == 62, "LT + A: Hydra, taken (%d)", s.slot);
    // the pads a release pulses
    SlotPad p = PadOf(9);
    EXPECT(p.buttons == 0 && p.lt == 0 && p.rt == 255, "RT's pulse: the right trigger");
    p = PadOf(11);
    EXPECT(p.buttons == 0 && p.lt == 255 && p.rt == 255, "LT + RT's: both triggers");
    p = PadOf(10);
    EXPECT(p.buttons == kPadA && p.lt == 255 && p.rt == 0, "LT + A's");
    p = PadOf(8);
    EXPECT(p.buttons == kPadA && p.lt == 0 && p.rt == 0, "A's");
    p = PadOf(5);
    EXPECT(p.buttons == kPadX && p.lt == 255, "LT + X's");
    p = PadOf(3);
    EXPECT(p.buttons == kPadRB && p.lt == 0, "RB's");
    p = PadOf(12);
    EXPECT(p.buttons == 0 && p.lt == 0 && p.rt == 0, "no slot: nothing");
    EXPECT(ControlOf(9) == kRt && ControlOf(11) == kRt && ControlOf(8) == kPadA && ControlOf(10) == kPadA && ControlOf(5) == kPadX && ControlOf(12) == 0u, "ControlOf");
    // every slot of the panel's table is reachable by its control and the left trigger, and the two agree
    bool round = true;
    for (int k = 0; k < kSlotCount; ++k) round = round && SlotOf(ControlOf(k), AltOf(k)) == k;
    EXPECT(round, "all twelve slots: control + row -> slot -> control + row");
    // the release's own pulse of RT starts no new hold
    h.Reset();
    h.Poll(0, 0, 0, true, SkillOf, IsListed, 0);
    s = h.Poll(0, 0, 255, true, SkillOf, IsListed, 10, kRt);
    EXPECT(!s.started && !s.strip, "the own pulse (ignored): the game's");
    s = h.Poll(0, 0, 0, true, SkillOf, IsListed, 100, kRt);
    s = h.Poll(0, 0, 255, true, SkillOf, IsListed, 200);
    EXPECT(s.started && s.strip == kRt, "a press after it: taken");
    EXPECT(SlotOf(kPadB, false) == 0 && SlotOf(kPadRB, true) == 7 && SlotOf(kPadA, false) == 8 && SlotOf(kRt, false) == 9 && SlotOf(kPadA, true) == 10 &&
               SlotOf(kRt, true) == 11 && SlotOf(0x0100u, false) == -1, "SlotOf (LB holds no slot)");
}

void NotAllowed() {
    Hold h;
    Step s = h.Poll(kPadX, 0, false, SkillOf, IsListed, 0);
    EXPECT(!s.started && !s.strip, "not allowed: the game's");
    s = h.Poll(kPadX, 0, true, SkillOf, IsListed, 10);
    EXPECT(!s.started && !s.strip, "held through: still the game's (no edge)");
    h.Reset();
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    h.Poll(kPadX, 0, true, SkillOf, IsListed, 10);
    s = h.Poll(kPadX, 0, false, SkillOf, IsListed, 400);
    EXPECT(s.strip == kPadX && !s.circle && !h.CircleUp(), "a menu opened: still taken, no arc");
    s = h.Poll(0, 0, false, SkillOf, IsListed, 500);
    EXPECT(s.released && s.cancelled, "let go: cancelled");
    h.Reset();
    h.Poll(0, 0, true, SkillOf, IsListed, 0);
    h.Poll(kPadX, 0, true, SkillOf, IsListed, 10);
    h.Poll(kPadX, 0, true, SkillOf, IsListed, 300);
    EXPECT(h.CircleUp(), "up");
    h.Poll(kPadX, 0, false, SkillOf, IsListed, 350);
    EXPECT(!h.CircleUp(), "a menu takes it down");
    s = h.Poll(0, 0, true, SkillOf, IsListed, 400);
    EXPECT(s.released && s.cancelled && !s.tap, "no cast");
}

// The thrown arc: the hero at (0, 0) on ground 0; the staff's tip 4 units up, 1 ahead (+z).
const V3 kTip{0.0f, 4.0f, 1.0f};
V3 Pitch(float deg) {   // along +z, pitched up by deg
    const float r = deg * 3.14159265f / 180.0f;
    return {0.0f, std::sin(r), std::cos(r)};
}

void Arcs() {
    const float reach = 100.0f, cap = 1000.0f;
    // 45 degrees from the ground itself lands `reach` out
    Arc a = MakeArc({0.0f, 0.0f, 0.0f}, Pitch(45.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap);
    EXPECT(a.ok && Near(a.land.x, 0.0f) && Near(a.land.z, reach, 0.05f), "45 degrees from the ground: lands at the reach (%f)", a.land.z);
    // from the tip: raised farther, lowered nearer, pointed down near
    const float level = MakeArc(kTip, Pitch(0.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).land.z;
    const float up20 = MakeArc(kTip, Pitch(20.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).land.z;
    const float up40 = MakeArc(kTip, Pitch(40.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).land.z;
    const float down30 = MakeArc(kTip, Pitch(-30.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).land.z;
    // level from 4 up: s = sqrt(2 K h) = sqrt(800) = 28.3, plus the tip's 1 ahead
    EXPECT(Near(level, 1.0f + std::sqrt(800.0f), 0.05f), "level: lands 29.3 out (%f)", level);
    EXPECT(down30 < level && level < up20 && up20 < up40, "lower nearer, higher farther: %f < %f < %f < %f", down30, level, up20, up40);
    // pointed steeply down: lands just ahead, where the line would meet the ground
    const float down60 = MakeArc(kTip, Pitch(-60.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).land.z;
    EXPECT(down60 > 1.0f && down60 < 1.0f + 4.0f / std::tan(60.0f * 3.14159265f / 180.0f) + 0.01f, "down 60: within the straight line's meet (%f)", down60);
    // the heading: pointed along +x, it lands on +x
    a = MakeArc(kTip, {1.0f, 0.2f, 0.0f}, 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap);
    EXPECT(a.ok && a.land.x > 10.0f && Near(a.land.z, 1.0f, 1e-3f), "along +x (%f %f)", a.land.x, a.land.z);
    // the points: from the tip down to the circle, the last on the ground
    a = MakeArc(kTip, Pitch(30.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap);
    EXPECT(a.n == kArcPoints && Near(a.pts[0].y, 4.0f) && Near(a.pts[0].z, 1.0f) && Near(a.pts[kArcPoints - 1].y, 0.0f) &&
               Near(a.pts[kArcPoints - 1].z, a.land.z, 1e-2f), "from the tip (%f %f) to the circle (%f %f)", a.pts[0].y, a.pts[0].z, a.pts[kArcPoints - 1].y, a.pts[kArcPoints - 1].z);
    bool rises = false;
    for (int i = 1; i < a.n; ++i) rises = rises || a.pts[i].y > a.pts[0].y + 0.5f;
    EXPECT(rises, "thrown up 30 degrees, it rises first");
    // straight up / down: no heading
    EXPECT(!MakeArc(kTip, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).ok, "straight up: none");
    EXPECT(!MakeArc(kTip, {0.0f, -1.0f, 0.01f}, 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).ok, "straight down: none");
    EXPECT(!MakeArc(kTip, {NAN, 0.0f, 1.0f}, 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap).ok, "not finite: none");
    // the ground under the start (the tip below it - a stair): taken as on it
    a = MakeArc({0.0f, -1.0f, 0.0f}, Pitch(45.0f), 0.0f, 0.0f, 0.0f, reach, kMinUnits, cap);
    EXPECT(a.ok && Near(a.land.z, reach, 0.05f), "a start under the ground: as on it (%f)", a.land.z);
}

// [gameplay] leap_arc_head: Leap's arc from the belly (about 1 m up: 3.6 units at 3.6 units a metre, over the hero's own place) along the HEAD's forward, its
// reach and cap the leap's range (11 cells = 22 units): the head raised lands farther, lowered nearer, turned turns it, and never past the range.
void HeadLeaps() {
    const V3 belly{0.0f, 3.6f, 0.0f};
    const float range = 11.0f * kUnitsPerCell;
    auto at = [&](float pitchDeg, float yawDeg) {
        const float p = pitchDeg * 3.14159265f / 180.0f, y = yawDeg * 3.14159265f / 180.0f;
        return MakeArc(belly, {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)}, 0.0f, 0.0f, 0.0f, range, kMinUnits, range);
    };
    const Arc down30 = at(-30.0f, 0.0f), down10 = at(-10.0f, 0.0f), level = at(0.0f, 0.0f), up15 = at(15.0f, 0.0f), up40 = at(40.0f, 0.0f);
    EXPECT(down30.ok && down10.ok && level.ok && up15.ok && up40.ok, "all made");
    EXPECT(Length(down30.land) < Length(down10.land) && Length(down10.land) < Length(level.land) && Length(level.land) < Length(up15.land),
           "the head lowered nearer, raised farther: %f %f %f %f", Length(down30.land), Length(down10.land), Length(level.land), Length(up15.land));
    EXPECT(Length(up40.land) <= range + 1e-3f && Length(up15.land) <= range + 1e-3f, "never past the leap's range: %f %f", Length(up15.land), Length(up40.land));
    EXPECT(up40.capped && Near(Length(up40.land), range, 1e-2f), "raised to 40: at the range, held there (%f)", Length(up40.land));
    // level from 1 m up with an 11-cell reach: sqrt(2 K h) = sqrt(2 * 22 * 3.6) = 12.6 units, a bit over half the range
    EXPECT(Near(Length(level.land), std::sqrt(2.0f * range * 3.6f), 0.05f), "level: %f", Length(level.land));
    // turned: the landing turns with the head
    const Arc right = at(0.0f, 90.0f);
    EXPECT(right.ok && Near(right.land.x, Length(level.land), 0.05f) && Near(right.land.z, 0.0f, 0.05f), "turned 90: to the side (%f %f)", right.land.x, right.land.z);
    // the arc leaves the belly
    EXPECT(Near(level.pts[0].y, 3.6f) && Near(level.pts[0].x, 0.0f) && Near(level.pts[0].z, 0.0f), "from the belly");
    // [gameplay] leap_head_gain: 2 = as far with half the tilt; the heading kept; past 45 degrees held at the farthest; 1 = as it is
    auto dirOf = [](float pitchDeg, float yawDeg) {
        const float p = pitchDeg * 3.14159265f / 180.0f, y = yawDeg * 3.14159265f / 180.0f;
        return V3{std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
    };
    auto land = [&](V3 d) { return Length(MakeArc(belly, d, 0.0f, 0.0f, 0.0f, range, kMinUnits, range).land); };
    EXPECT(Near(land(TiltGain(dirOf(5.0f, 0.0f), 2.0f)), land(dirOf(10.0f, 0.0f)), 1e-3f), "gain 2: 5 degrees up lands as 10 (%f %f)",
           land(TiltGain(dirOf(5.0f, 0.0f), 2.0f)), land(dirOf(10.0f, 0.0f)));
    EXPECT(Near(land(TiltGain(dirOf(-10.0f, 0.0f), 2.0f)), land(dirOf(-20.0f, 0.0f)), 1e-3f), "gain 2 down: -10 lands as -20");
    EXPECT(land(TiltGain(dirOf(5.0f, 0.0f), 2.0f)) > land(dirOf(5.0f, 0.0f)), "a small raise reaches farther with the gain");
    const V3 g40 = TiltGain(dirOf(40.0f, 30.0f), 3.0f);
    EXPECT(Near(std::atan2(g40.y, std::hypot(g40.x, g40.z)) * 57.2957795f, kMaxThrowDeg, 1e-3f), "gained past 45: held at 45 (the farthest)");
    EXPECT(Near(std::atan2(g40.x, g40.z) * 57.2957795f, 30.0f, 1e-3f), "the heading kept");
    const V3 one = TiltGain(dirOf(12.0f, 10.0f), 1.0f), same = dirOf(12.0f, 10.0f);
    EXPECT(one.x == same.x && one.y == same.y && one.z == same.z, "gain 1: as it is");
    const V3 up = TiltGain({0.0f, 1.0f, 0.0f}, 2.0f);
    EXPECT(up.y == 1.0f && up.x == 0.0f, "straight up: as it is (no heading)");
    EXPECT(Near(HeadGain(2.0f), 2.0f) && Near(HeadGain(0.1f), kMinHeadGain) && Near(HeadGain(9.0f), kMaxHeadGain) && Near(HeadGain(NAN), 1.0f), "HeadGain");
}

void Caps() {
    // the cap: 50 cells = 100 units; at 45 degrees the throw would go 200 out
    const float capU = 50.0f * kUnitsPerCell;
    Arc a = MakeArc(kTip, Pitch(45.0f), 0.0f, 0.0f, 0.0f, 2.0f * capU, kMinUnits, capU);
    EXPECT(a.ok && a.capped && Near(Length(a.land), capU, 1e-2f) && Near(a.land.x, 0.0f), "held at the cap along its heading (%f)", Length(a.land));
    EXPECT(Near(a.pts[kArcPoints - 1].z, capU, 0.05f) && Near(a.pts[kArcPoints - 1].y, 0.0f), "the arc bent down onto the capped circle (%f %f)", a.pts[kArcPoints - 1].z,
           a.pts[kArcPoints - 1].y);
    bool above = true;
    for (int i = 0; i + 1 < a.n; ++i) above = above && a.pts[i].y >= -1e-3f;
    EXPECT(above, "the bent arc stays over the ground");
    // within the cap: not held
    a = MakeArc(kTip, Pitch(10.0f), 0.0f, 0.0f, 0.0f, capU, kMinUnits, capU);
    EXPECT(a.ok && !a.capped && Length(a.land) < capU, "within: as thrown (%f)", Length(a.land));
    // the hero elsewhere: the cap counts from him
    a = MakeArc({10.0f, 4.0f, 10.0f}, Pitch(45.0f), 0.0f, 10.0f, 9.0f, 2.0f * capU, kMinUnits, capU);
    EXPECT(a.ok && Near(Length(a.land), capU, 1e-2f), "from the hero's place (%f)", Length(a.land));
    // never nearer the hero than the minimum
    a = MakeArc({0.0f, 0.5f, 0.0f}, {0.0f, -1.0f, 0.2f}, 0.0f, 0.0f, 0.0f, capU, kMinUnits, capU);
    EXPECT(a.ok && Near(Length(a.land), kMinUnits, 1e-3f), "at the feet: held out to the minimum (%f)", Length(a.land));
    // the ini's cap: held to [3, 48]
    EXPECT(Near(CapCells(50.0f), kHardCapCells) && Near(CapCells(20.0f), 20.0f) && Near(CapCells(0.0f), 3.0f) && Near(CapCells(NAN), kHardCapCells), "CapCells");
    // Clamp
    const Offset fb{1.0f, 0.0f};
    Offset c = Clamp(Offset{30.0f, 40.0f}, 2.0f, 25.0f, fb);
    EXPECT(Near(c.x, 15.0f) && Near(c.z, 20.0f), "50 out held to 25 on its heading: %f %f", c.x, c.z);
    c = Clamp(Offset{0.0f, 0.0f}, 2.0f, 25.0f, fb);
    EXPECT(Near(c.x, 1.0f) && Near(c.z, 0.0f), "on the hero: the fallback");
}

void Lists() {
    const SkillSet d = DefaultSkills();
    for (int id : {54, 51, 56, 59, 62, 66, 78, 75, 234, 244, 251, 261, 273, 390, 402})
        EXPECT(d.Has(id), "the default list has %d", id);
    for (int id : {0, 36, 64, 74, 93, 121, 48, 57, 132, 143, 88, 90, 275})
        EXPECT(!d.Has(id), "the default list leaves out %d", id);
    SkillSet s = ParseSkills("54, 56;59 1023 abc 7x 2000");
    EXPECT(s.Has(54) && s.Has(56) && s.Has(59) && s.Has(1023) && s.Has(7) && !s.Has(51) && !s.Has(2000), "parsed");
    s = ParseSkills("");
    bool none = true;
    for (int i = 0; i < 1024; ++i) none = none && !s.Has(i);
    EXPECT(none, "empty: none (the wire takes the default for it)");
    EXPECT(!ParseSkills(nullptr).Has(54), "null: none");
}

void Heights() {
    EXPECT(Near(HeightScale(25.0f), 0.25f) && Near(HeightScale(-5.0f), 0.0f) && Near(HeightScale(300.0f), 1.0f) && Near(HeightScale(NAN), kDefaultHeightPct / 100.0f),
           "HeightScale");
    // the same throw drawn at 100 %, 25 % and 0 %: the same landing, the peak above the tip-to-circle line a quarter, then none
    const Arc full = MakeArc(kTip, Pitch(45.0f), 0.0f, 0.0f, 0.0f, 100.0f, kMinUnits, 1000.0f, 1.0f);
    const Arc low = MakeArc(kTip, Pitch(45.0f), 0.0f, 0.0f, 0.0f, 100.0f, kMinUnits, 1000.0f, 0.25f);
    const Arc flat = MakeArc(kTip, Pitch(45.0f), 0.0f, 0.0f, 0.0f, 100.0f, kMinUnits, 1000.0f, 0.0f);
    EXPECT(full.ok && low.ok && flat.ok && Near(full.land.z, low.land.z) && Near(full.land.z, flat.land.z), "the landing is the throw's: %f %f %f", full.land.z, low.land.z,
           flat.land.z);
    float riseFull = 0.0f, riseLow = 0.0f, riseFlat = 0.0f;
    for (int i = 0; i < kArcPoints; ++i) {
        const float t = float(i) / float(kArcPoints - 1), line = kTip.y + (0.0f - kTip.y) * t;
        riseFull = std::max(riseFull, full.pts[i].y - line);
        riseLow = std::max(riseLow, low.pts[i].y - line);
        riseFlat = std::max(riseFlat, flat.pts[i].y - line);
    }
    EXPECT(riseFull > 10.0f && Near(riseLow, 0.25f * riseFull, 1e-2f) && Near(riseFlat, 0.0f, 1e-4f), "rises %f, %f, %f", riseFull, riseLow, riseFlat);
    EXPECT(Near(low.pts[0].y, kTip.y) && Near(low.pts[kArcPoints - 1].y, 0.0f), "still from the tip to the ground");
    bool above = true;
    for (int i = 0; i < kArcPoints; ++i) above = above && low.pts[i].y >= -1e-3f;
    EXPECT(above, "over the ground");
}

void Looks() {
    EXPECT(Near(Opacity(60.0f), 0.6f) && Near(Opacity(-3.0f), 0.0f) && Near(Opacity(250.0f), 1.0f) && Near(Opacity(NAN), kDefaultOpacityPct / 100.0f), "Opacity");
    const Arc base = MakeArc(kTip, Pitch(30.0f), 0.0f, 0.0f, 0.0f, 100.0f, kMinUnits, 1000.0f, 0.25f);
    Arc a = base, b = base, c = base;
    Jitter(&a, 7, 0.2f);
    Jitter(&b, 7, 0.2f);
    Jitter(&c, 8, 0.2f);
    bool same = true, differs = false, bounded = true;
    for (int i = 0; i < kArcPoints; ++i) {
        same = same && a.pts[i].x == b.pts[i].x && a.pts[i].y == b.pts[i].y && a.pts[i].z == b.pts[i].z;
        differs = differs || a.pts[i].x != c.pts[i].x;
        bounded = bounded && std::fabs(a.pts[i].x - base.pts[i].x) <= 0.2f + 1e-6f && std::fabs(a.pts[i].y - base.pts[i].y) <= 0.2f + 1e-6f &&
                  std::fabs(a.pts[i].z - base.pts[i].z) <= 0.2f + 1e-6f;
    }
    EXPECT(same && differs && bounded, "the same seed the same shake %d, another seed another %d, within the amplitude %d", same, differs, bounded);
    EXPECT(a.pts[0].x == base.pts[0].x && a.pts[0].y == base.pts[0].y && a.pts[kArcPoints - 1].z == base.pts[kArcPoints - 1].z &&
               a.pts[kArcPoints - 1].y == base.pts[kArcPoints - 1].y, "the ends stay: the staff's tip and the circle");
    Arc d = base;
    Jitter(&d, 7, 0.0f);
    EXPECT(d.pts[8].y == base.pts[8].y, "no amplitude: no shake");
}

void Colours() {
    // the elements, as the game's Skills.txt gives them
    EXPECT(ElementOf(51) == Element::Fire && ElementOf(56) == Element::Fire && ElementOf(59) == Element::Cold && ElementOf(271) == Element::Lightning &&
               ElementOf(402) == Element::Magic && ElementOf(273) == Element::Stun && ElementOf(54) == Element::None && ElementOf(66) == Element::None,
           "Fire Wall, Meteor fire; Blizzard cold; Lightning Sentry lightning; Abyss magic; Mind Blast stun; Teleport, Amplify Damage none");
    EXPECT(ElementOf(999) == Element::None, "a skill added by hand: none");
    for (int id : kDefaultSkills) {
        bool listed = false;
        for (const SkillElement& s : kElements) listed = listed || s.id == id;
        EXPECT(listed, "the default skill %d has its element", id);
    }
    float rgb[3];
    ElementRgb(Element::Fire, rgb);
    EXPECT(rgb[0] > rgb[1] && rgb[1] > rgb[2], "fire is orange");
    float cold[3], light[3];
    ElementRgb(Element::Cold, cold);
    ElementRgb(Element::Lightning, light);
    EXPECT(cold[2] > cold[1] && cold[1] > cold[0], "cold is a deep icy blue");
    EXPECT(light[2] > light[1] && light[1] > light[0] && light[0] > 0.3f, "lightning is light blue");
    EXPECT(light[1] - cold[1] > 0.25f, "lightning lighter than cold: told apart");
    ElementRgb(Element::Magic, rgb);
    EXPECT(rgb[2] > rgb[1] && rgb[0] > rgb[1], "magic is violet");
    ElementRgb(Element::None, rgb);
    EXPECT(rgb[0] > 0.85f && rgb[1] > 0.85f && rgb[2] > 0.85f, "none is a neutral white (not light blue)");
    // the choice
    const float own[3] = {1.0f, 1.0f, 1.0f}, all[3] = {0.0f, 1.0f, 0.0f};
    SkillRgb(true, 51, true, own, true, all, rgb);
    EXPECT(rgb[0] == 1.0f && rgb[1] == 1.0f && rgb[2] == 1.0f, "by skill, its own colour given: its own");
    SkillRgb(true, 51, false, own, true, all, rgb);
    float fire[3];
    ElementRgb(Element::Fire, fire);
    EXPECT(rgb[0] == fire[0] && rgb[1] == fire[1] && rgb[2] == fire[2], "by skill, none of its own: its element's (not the one for all)");
    SkillRgb(false, 51, true, own, true, all, rgb);
    EXPECT(rgb[0] == 0.0f && rgb[1] == 1.0f && rgb[2] == 0.0f, "one for all: the one for all, its own ignored");
    SkillRgb(false, 51, false, own, false, all, rgb);
    EXPECT(rgb[0] == kDefaultRgb[0] && rgb[1] == kDefaultRgb[1] && rgb[2] == kDefaultRgb[2], "one for all, not given: the red");
}

void Rings() {
    float a[20][3], b[20][3];
    const int n = Ring(1.0f, 2.0f, 3.0f, 0.5f, 20, a, b, 20);
    EXPECT(n == 20, "20 chords: %d", n);
    bool onCircle = true, closed = true, flat = true;
    for (int i = 0; i < n; ++i) {
        onCircle = onCircle && Near(std::hypot(a[i][0] - 1.0f, a[i][2] - 3.0f), 0.5f) && Near(std::hypot(b[i][0] - 1.0f, b[i][2] - 3.0f), 0.5f);
        flat = flat && a[i][1] == 2.0f && b[i][1] == 2.0f;
        const int j = (i + 1) % n;
        closed = closed && Near(b[i][0], a[j][0]) && Near(b[i][2], a[j][2]);
    }
    EXPECT(onCircle && flat && closed, "on the circle %d, on the ground %d, joined end to end %d", onCircle, flat, closed);
    EXPECT(Ring(0, 0, 0, 0.5f, 20, a, b, 7) == 7, "held to the room given");
    EXPECT(Ring(0, 0, 0, 0.0f, 20, a, b, 20) == 0 && Ring(0, 0, 0, 0.5f, 2, a, b, 20) == 0, "no radius / too few: none");
}

}  // namespace

int main() {
    Tap();
    HoldIt();
    Banks();
    Triggers();
    NotAllowed();
    Arcs();
    Caps();
    HeadLeaps();
    Lists();
    Heights();
    Looks();
    Colours();
    Rings();
    if (g_fails) {
        std::printf("point_cast_test: %d FAILED\n", g_fails);
        return 1;
    }
    std::printf("point_cast_test: all passed\n");
    return 0;
}
