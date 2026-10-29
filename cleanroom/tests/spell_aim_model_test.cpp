// Offline checks of the aim from the hero as drawn ([hands] spell_aim_model, vr/spell_aim_model.h): which part of the model aims, and that its
// line is taken as it is - no correction, the heading kept. No game, no headset. Run: build/Release/spell_aim_model_test.exe   (exit code 0 = all passed)

#include "../../vr/spell_aim_model.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace spellmodel;

static int g_failures = 0, g_checks = 0;

static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static void Set(float v[3], float x, float y, float z) { v[0] = x; v[1] = y; v[2] = z; }
static bool Unit(const float d[3]) { return Near(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1.0f); }

// a sorceress with a staff in the right hand: the bone 30 cm ahead, 40 cm below the eyes; the staff ahead and a little up; the hand drawn too
static In WithStaff() {
    In in;
    in.caster = true;
    in.weaponOk = true;
    Set(in.weaponOrigin, 0.2f, -0.4f, -0.3f);
    Set(in.weaponTip, 0.0f, 0.3f, -0.954f);
    in.handOk = true;
    Set(in.palm, 0.22f, -0.42f, -0.25f);
    Set(in.fingers, 0.5f, 0.0f, -0.866f);
    Set(in.face, 0.866f, 0.0f, 0.5f);
    return in;
}

static In EmptyHand() {
    In in = WithStaff();
    in.caster = false;
    in.weaponOk = false;
    return in;
}

int main() {
    // ---- a staff, wand or orb: its own axis, from its bone ----
    {
        const In in = WithStaff();
        const Out o = Choose(in);
        Expect(o.src == Src::Weapon, "a caster drawn by the hook: its bone's axis aims");
        Expect(Near(o.origin[0], 0.2f) && Near(o.origin[1], -0.4f) && Near(o.origin[2], -0.3f), "  from the bone's origin");
        Expect(Unit(o.dir) && Near(o.dir[1], 0.3f / std::sqrt(0.09f + 0.954f * 0.954f)), "  along its +Y, made unit, the up part kept (no tip-down here)");
        Expect(Near(std::atan2(o.dir[2], o.dir[0]), std::atan2(-0.954f, 0.0f)), "  the heading is the weapon's, no turn added");
    }
    {   // the hand does not count while the weapon gives a line
        In in = WithStaff();
        Set(in.fingers, 1.0f, 0.0f, 0.0f);
        Expect(Choose(in).src == Src::Weapon, "a caster: the hand's fingers elsewhere do not move the aim");
    }
    {   // upright staff: no heading, its hand aims
        In in = WithStaff();
        Set(in.weaponTip, 0.0f, 1.0f, 0.0f);
        const Out o = Choose(in);
        Expect(o.src == Src::Fingers, "a staff held upright: the hand that holds it aims (its fingers)");
        Expect(Near(o.origin[0], 0.22f) && Near(o.origin[2], -0.25f), "  from the palm");
    }
    {   // the 5 degree dead zone, both sides of it
        In in = WithStaff();
        const float a4 = 4.0f * 3.14159265f / 180.0f, a6 = 6.0f * 3.14159265f / 180.0f;
        Set(in.weaponTip, std::sin(a4), std::cos(a4), 0.0f);
        Expect(Choose(in).src != Src::Weapon, "4 degrees off vertical: too steep for a heading");
        Set(in.weaponTip, std::sin(a6), std::cos(a6), 0.0f);
        Expect(Choose(in).src == Src::Weapon, "6 degrees off vertical: its heading is taken");
        Set(in.weaponTip, 0.0f, -1.0f, 0.0f);
        Expect(Choose(in).src == Src::Fingers, "pointing straight down: the hand aims");
    }
    {   // not posed lately
        In in = WithStaff();
        in.weaponOk = false;
        Expect(Choose(in).src == Src::Fingers, "a caster not posed lately: the hand aims");
        in.handOk = false;
        const Out o = Choose(in);
        Expect(o.src == Src::None, "neither posed: None (the old aim)");
        Expect(o.why && *o.why, "  with a reason for the log");
    }
    // ---- [weight] carries it: the grip's shaft, never the drawn bone ----
    {
        In in = WithStaff();
        in.weightCarries = true;
        Set(in.gripDir, -0.6f, 0.0f, -0.8f);
        Set(in.gripFrom, 0.1f, -0.5f, -0.2f);
        const Out o = Choose(in);
        Expect(o.src == Src::Grip, "[weight] carries the staff: the shaft as the grip holds it");
        Expect(Near(o.dir[0], -0.6f) && Near(o.dir[2], -0.8f) && Near(o.origin[1], -0.5f), "  that line, from that controller - the drawn bone is not used");
        Set(in.gripDir, 0.0f, 1.0f, 0.0f);
        Expect(Choose(in).src == Src::Fingers, "[weight]'s shaft upright: the hand aims, not the lagging drawn bone");
    }
    // ---- no caster: the hand as drawn ----
    {
        const Out o = Choose(EmptyHand());
        Expect(o.src == Src::Fingers, "an empty hand: its fingers");
        Expect(Near(o.dir[0], 0.5f) && Near(o.dir[2], -0.866f), "  exactly along them");
    }
    {
        In in = EmptyHand();
        Set(in.fingers, 0.0f, 0.96f, -0.28f);   // hand raised, fingers up
        Set(in.face, 0.1f, 0.0f, -0.995f);       // palm to the target
        const Out o = Choose(in);
        Expect(o.src == Src::Palm, "fingers up: out of the palm");
        Expect(Near(std::atan2(o.dir[2], o.dir[0]), std::atan2(-0.995f, 0.1f)), "  the palm's heading");
    }
    {
        In in = EmptyHand();
        Set(in.fingers, 0.0f, 0.75f, -0.66f);   // not level, but a heading
        Set(in.face, 0.0f, 1.0f, 0.0f);          // palm up
        Expect(Choose(in).src == Src::Fingers, "fingers tipped up, palm up: still the fingers");
        Set(in.fingers, 0.0f, 1.0f, 0.0f);
        Expect(Choose(in).src == Src::None, "fingers straight up, palm straight up: None (the old aim)");
    }
    {   // the level rule is HandRay's 0.7 on the unit vector, whatever the input's length
        In in = EmptyHand();
        Set(in.fingers, 0.0f, 0.69f, -0.7238f);
        Expect(Choose(in).src == Src::Fingers, "0.69 up: level");
        Set(in.fingers, 0.0f, 0.71f, -0.7042f);
        Set(in.face, 0.0f, 0.0f, -1.0f);
        Expect(Choose(in).src == Src::Palm, "0.71 up: the palm");
    }
    {   // garbage in
        In in = EmptyHand();
        in.fingers[0] = std::numeric_limits<float>::quiet_NaN();
        in.face[1] = std::numeric_limits<float>::infinity();
        Expect(Choose(in).src == Src::None, "not a number: None");
        In w = WithStaff();
        w.weaponOrigin[2] = std::numeric_limits<float>::quiet_NaN();
        Expect(Choose(w).src == Src::Fingers, "the weapon's origin not a number: the hand");
        In z = EmptyHand();
        Set(z.fingers, 0.0f, 0.0f, 0.0f);
        Set(z.face, 0.0f, 0.0f, 0.0f);
        Expect(Choose(z).src == Src::None, "zero vectors: None");
    }
    {   // any heading round the circle comes out as it went in, at any length and tilt below the dead zone
        int bad = 0;
        for (int deg = 0; deg < 360; ++deg) {
            const float a = deg * 3.14159265f / 180.0f;
            for (const float up : {-0.9f, -0.3f, 0.0f, 0.4f, 0.95f}) {
                In in = WithStaff();
                const float h = std::sqrt(1.0f - up * up);
                Set(in.weaponTip, 2.5f * h * std::cos(a), 2.5f * up, 2.5f * h * std::sin(a));
                const Out o = Choose(in);
                const float hz = Horizontal(o.dir);
                const float along = hz > 0.0f ? (o.dir[0] * std::cos(a) + o.dir[2] * std::sin(a)) / hz : 0.0f;   // cos of the angle between the two headings
                if (o.src != Src::Weapon || !Unit(o.dir) || along < 0.99999f) ++bad;
            }
        }
        Expect(bad == 0, "every heading and tilt: the weapon's own heading, unit length");
    }
    // ---- both hands on the staff ([weight]): along the staff as drawn between the grips (test-54, 0.170) ----
    {
        // 0.170's picture: the staff tilted up and to the left from the right hand; the first hand's grip shaft (and the controllers' line)
        // point straight ahead - the ray went ahead, at a clear angle to the staff
        In in = WithStaff();
        in.weightCarries = true;
        Set(in.gripDir, 0.0f, 0.0f, -1.0f);        // the right hand took it first: its grip's shaft straight ahead
        Set(in.gripFrom, 0.2f, -0.4f, -0.3f);
        in.bothHands = true;
        in.shaftOk = true;
        Set(in.shaftButt, 0.25f, -0.55f, -0.2f);    // the drawn staff: butt low at the right ...
        Set(in.shaftTip, -0.35f, 0.15f, -0.9f);     // ... tip up, left and ahead
        const Out o = Choose(in);
        Expect(o.src == Src::Hands, "both hands on it: the staff as drawn");
        const float d[3] = {-0.6f, 0.7f, -0.7f};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        Expect(Near(o.dir[0], d[0] / l) && Near(o.dir[1], d[1] / l) && Near(o.dir[2], d[2] / l), "  butt -> tip, not the grip's straight ahead");
        Expect(Near(o.origin[0], -0.35f) && Near(o.origin[1], 0.15f) && Near(o.origin[2], -0.9f), "  from the staff's tip");
        const float hz = Horizontal(o.dir);
        Expect(std::atan2(o.dir[0] / hz, -o.dir[2] / hz) < -0.6f, "  its heading 40+ degrees to the left, as the staff (the grip's was 0)");
        // the tip behind the butt (the staff held reversed): the ray follows the drawn tip, whatever the grip says
        Set(in.shaftButt, -0.35f, 0.15f, -0.9f);
        Set(in.shaftTip, 0.25f, -0.55f, -0.2f);
        const Out b = Choose(in);
        Expect(b.src == Src::Hands && Near(b.dir[0], -d[0] / l) && Near(b.origin[2], -0.2f), "  reversed staff: toward its drawn tip");
        Set(in.shaftButt, 0.25f, -0.55f, -0.2f);
        Set(in.shaftTip, 0.26f, -0.56f, -0.21f);    // a shaft of 1.7 cm: no line
        Expect(Choose(in).src == Src::Grip, "  a drawn shaft under 5 cm: the carrying hand's grip");
        Set(in.shaftTip, -0.35f, 0.15f, -0.9f);
        in.shaftOk = false;
        Expect(Choose(in).src == Src::Grip, "  no drawn shaft lately: the carrying hand's grip");
        in.shaftOk = true;
        in.bothHands = false;
        Expect(Choose(in).src == Src::Grip, "one hand on it: its grip, as before (the drawn shaft is not used)");
        In u = WithStaff();
        u.weightCarries = true; u.bothHands = true; u.shaftOk = true;
        Set(u.gripDir, 0.0f, 0.0f, -1.0f);
        Set(u.shaftButt, 0.2f, -0.6f, -0.3f);
        Set(u.shaftTip, 0.2f, 0.9f, -0.31f);       // the staff upright between the hands
        Expect(Choose(u).src == Src::Grip, "both hands, the staff upright: the grip's shaft (if it has a heading)");
        u.shaftButt[2] = std::numeric_limits<float>::quiet_NaN();
        Set(u.shaftTip, -0.35f, 0.15f, -0.9f);
        Expect(Choose(u).src == Src::Grip, "both hands, the drawn butt not a number: the grip");
    }
    // ---- [hands] staff_upright_arm: an upright staff aims along the arm (vr/staff_upright.h) ----
    {
        In in = WithStaff();
        in.staff = true;
        in.shoulderOk = true;
        Set(in.shoulder, 0.17f, -0.2f, 0.0f);          // the drawn right shoulder; the palm at (0.22, -0.42, -0.25)
        Set(in.weaponTip, 0.0f, 1.0f, 0.0f);           // the staff upright
        Expect(Choose(in).src == Src::Fingers, "upright staff, the blend off: its hand's fingers, as before");
        in.uprightArm = true;
        const Out o = Choose(in);
        float arm[3] = {in.palm[0] - in.shoulder[0], in.palm[1] - in.shoulder[1], in.palm[2] - in.shoulder[2]};
        const float al = std::sqrt(arm[0] * arm[0] + arm[1] * arm[1] + arm[2] * arm[2]);
        Expect(o.src == Src::Weapon && Near(o.armW, 1.0f), "upright staff, the blend on: the weapon's line, all arm");
        Expect(Near(o.dir[0], arm[0] / al) && Near(o.dir[1], arm[1] / al) && Near(o.dir[2], arm[2] / al), "  along the arm, shoulder to palm");
        Expect(Near(o.origin[1], -0.4f) && Near(o.shoulder[0], 0.17f) && Near(o.armHand[2], -0.25f), "  from the staff's bone; the arm kept for the ray");
        Set(in.weaponTip, 0.0f, 0.3f, -0.954f);        // levelled out like a wand
        const Out flat = Choose(in);
        Expect(flat.src == Src::Weapon && Near(flat.armW, 0.0f) && Near(flat.dir[1], 0.3f / std::sqrt(0.09f + 0.954f * 0.954f)), "levelled out: the staff's own line, no arm");
        const float t = 20.0f * 3.14159265f / 180.0f;
        Set(in.weaponTip, std::sin(t), std::cos(t), 0.0f);   // 20 degrees off upright toward the right
        const Out half = Choose(in);
        Expect(half.src == Src::Weapon && half.armW > 0.4f && half.armW < 0.6f && half.dir[0] > arm[0] / al + 0.1f, "tilted to the right: half arm, turned to the right");
        In wand = in;
        wand.staff = false;
        Set(wand.weaponTip, 0.0f, 1.0f, 0.0f);
        Expect(Choose(wand).src == Src::Fingers, "a wand held upright: not a staff, its hand as before");
        In noArm = in;
        noArm.shoulderOk = false;
        Set(noArm.weaponTip, 0.0f, 1.0f, 0.0f);
        Expect(Choose(noArm).src == Src::Fingers, "no shoulder lately: as before");
        In shortArm = in;
        Set(shortArm.shoulder, 0.2f, -0.35f, -0.2f);   // 9 cm from the palm
        Set(shortArm.weaponTip, 0.0f, 1.0f, 0.0f);
        Expect(Choose(shortArm).src == Src::Fingers, "an arm under 15 cm: as before");
        In grip = in;
        grip.weightCarries = true;
        Set(grip.gripDir, 0.02f, 1.0f, 0.0f);
        Set(grip.gripFrom, 0.2f, -0.4f, -0.3f);
        const Out g = Choose(grip);
        Expect(g.src == Src::Grip && Near(g.armW, 1.0f) && Near(g.dir[2], arm[2] / al), "[weight] carries it upright: the grip's line, along the arm");
    }
    // ---- how far out the spell's point goes, by what is held ----
    {
        Expect(Near(Cells(Hold::Hand, 5.0f, 20.0f, 30.0f), 5.0f), "bare hands: hand_range");
        Expect(Near(Cells(Hold::OneHandCaster, 5.0f, 20.0f, 30.0f), 20.0f), "a wand or orb: caster_range");
        Expect(Near(Cells(Hold::Staff, 5.0f, 20.0f, 30.0f), 30.0f), "a staff: its own range");
        Expect(Near(Cells(Hold::Hand, std::numeric_limits<float>::quiet_NaN(), 20.0f, 30.0f), 0.0f), "not a number: 0 (the game's point)");
    }
    {   // names for the log
        Expect(*Name(Src::Weapon) && *Name(Src::Grip) && *Name(Src::Hands) && *Name(Src::Fingers) && *Name(Src::Palm) && *Name(Src::None), "every source has a name");
    }

    if (g_failures) {
        std::printf("spell_aim_model_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("spell_aim_model_test: all %d checks passed\n", g_checks);
    return 0;
}
