// Hit by touch: the strike rules (vr/strike_direction.h) - toward the monster, the head, the amplitude - and the engine's two numbers per weapon kind. Pure maths with the real swing engine
// (stroke_contact.h) on synthetic swings.
// Run: build/Release/strike_direction_test.exe [-v]   (exit code 0 = all passed; -v prints every contact of the swings)

#include "../../vr/strike_direction.h"
#include "../../vr/metal_head.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace stroke;
using namespace strikedir;

static int g_failures = 0;
static bool g_verbose = false;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static const float kRad = 0.0174532925f;
static bool NearV(Vec a, Vec b, float tol) { return Length(a - b) <= tol; }

// [gameplay] melee_metal_head: where the head starts, from the game's own models (vr/weapon_heads.h) - or the kind's default where they gave none
static void MetalHeadTests() {
    using namespace metalhead;
    Source from = Source::None;
    // a Great Poleaxe (7h7): butt 3.82 units behind the grip, its point 4.943 ahead, the head's edge 2.314 ahead -> 70 % of the length
    float a = Along(weaponextent::C('7', 'h', '7', ' '), kPolearm, 0.0f, 1.0f, 0.0f, &from);
    Expect(from == Source::Model && std::fabs(a - (3.82f + 2.314f) / (3.82f + 4.943f)) < 1e-3f, "a Great Poleaxe: its head from its model, 70 %", a);
    // a Long Sword (lsd) as drawn, 0.2 m a unit, the capsule a radius short at each end: the blade from just ahead of the grip (the guard), about a fifth along
    const float r = 0.02f, butt = 0.698f * 0.2f - r, len = 3.836f * 0.2f - r;
    a = Along(weaponextent::C('l', 's', 'd', ' '), kSword, butt, len, 0.2f, &from);
    Expect(from == Source::Model && std::fabs(a - (butt + 0.285f * 0.2f) / (butt + len)) < 1e-3f && a > 0.15f && a < 0.3f, "a Long Sword: the hilt is no strike, the blade from the guard", a);
    // wood: a club, a staff - the whole length strikes
    Expect(Along(weaponextent::C('c', 'l', 'b', ' '), kMace, 0.1f, 0.6f, 0.2f, &from) < 0.0f && from == Source::Wood, "a Club: wood, its whole length strikes");
    Expect(Along(weaponextent::C('s', 's', 't', ' '), kStaff, 0.6f, 0.6f, 0.2f, &from) < 0.0f && from == Source::None, "a staff: not judged");
    Expect(Along(weaponextent::C('l', 's', 'd', ' '), kClaw, 0.1f, 0.6f, 0.2f, &from) < 0.0f && from == Source::None, "a claw kind: not judged");
    // a Falchion: its model gave no head (the blade reads no guard) - the sword's default, said so
    a = Along(weaponextent::C('f', 'l', 'c', ' '), kSword, 0.1f, 0.6f, 0.2f, &from);
    Expect(from == Source::KindDefault && std::fabs(a - (1.0f - DefaultShare(kSword))) < 1e-4f, "a Falchion: the sword's default share", a);
    a = Along(0, kAxe, 0.1f, 0.6f, 0.0f, &from);
    Expect(from == Source::KindDefault && std::fabs(a - 0.6f) < 1e-4f, "an unknown axe: the axe's default", a);
    Expect(Along(weaponextent::C('7', 'h', '7', ' '), kPolearm, 0.0f, 0.0f, 0.0f, &from) < 0.0f, "no blade length: nothing");
    // [gameplay] melee_head_volume: a Large Axe's head is a volume of its own beside the shaft (its blade along the model's +Z), its shaft as thin as the model's
    Volume vol;
    Expect(VolumeOf(weaponextent::C('l', 'a', 'x', ' '), kAxe, &vol), "a Large Axe has a head volume");
    Expect(vol.shaftR > 0.05f && vol.shaftR < 0.12f, "... its shaft about 0.09 model units round", vol.shaftR);
    Expect(vol.a[1] > 1.0f && vol.a[1] < 1.8f && std::fabs(vol.a[1] - vol.b[1]) < 1e-3f, "... the volume across the shaft at the head's height (1.03 .. 1.77)", vol.a[1]);
    Expect(std::max(vol.a[2], vol.b[2]) + vol.r > 0.8f && std::fabs(vol.a[0]) < 0.05f, "... reaching out along +Z to the blade's edge (0.88)", std::max(vol.a[2], vol.b[2]) + vol.r);
    Expect(!VolumeOf(weaponextent::C('l', 's', 'd', ' '), kSword, nullptr) && !VolumeOf(weaponextent::C('c', 'l', 'b', ' '), kMace, nullptr) && !VolumeOf(0, kAxe, nullptr),
           "no head volume: a sword (the blade is the capsule), a club (wood), an unknown item");
    Expect(VolumeOf(weaponextent::C('m', 'a', 'c', ' '), kMace, &vol) && std::fabs(vol.a[2] - vol.b[2]) < 1e-3f && vol.b[1] > vol.a[1], "a Mace: its head round the shaft, along it");
    {   // the model's point where the posed bone puts it: a bone turned (X -> -Z, Z -> X), scaled rows, 0.2 m a unit
        const float origin[3] = {1.0f, 2.0f, 3.0f}, axes[3][3] = {{0, 0, -3}, {0, 2, 0}, {0.5f, 0, 0}}, p[3] = {0, 1, 0.5f};
        float out[3];
        Expect(ModelPoint(origin, axes, 0.2f, p, out) && std::fabs(out[0] - 1.1f) < 1e-5f && std::fabs(out[1] - 2.2f) < 1e-5f && std::fabs(out[2] - 3.0f) < 1e-5f, "a model point on the posed bone", out[0]);
        const float flat[3][3] = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        Expect(!ModelPoint(origin, flat, 0.2f, p, out), "an axis with no length: none");
    }
    {   // the shaft thinned: radius 5 cm -> 0.086 units of 0.27 m (2.3 cm); the ends stay where the wood ends
        float length = 0.43f, bu = 0.47f, radius = 0.05f;
        ThinShaft(0.086f, 0.27f, &length, &bu, &radius);
        Expect(std::fabs(radius - 0.02322f) < 1e-4f && std::fabs(length + radius - 0.48f) < 1e-4f && std::fabs(bu + radius - 0.52f) < 1e-4f, "the shaft thinned, its ends kept", radius);
        float l2 = 0.3f, b2 = 0.0f, r2 = 0.01f;
        ThinShaft(0.2f, 0.27f, &l2, &b2, &r2);
        Expect(r2 == 0.01f && l2 == 0.3f, "never thicker than it was");
    }
    // every row of the table: a head inside the weapon, never at the point
    for (const weaponhead::Row& row : weaponhead::kRows) {
        weaponextent::Reach rr;
        Expect(weaponextent::ReachOf(row.code, &rr), "every head row has its extents row", static_cast<double>(row.code));
        const float al = Along(row.code, kSword, rr.butt, rr.tip, 0.0f, &from);
        Expect(from == Source::Wood ? al < 0.0f : (from == Source::Model && al > 0.0f && al <= kMaxAlong), "every head row: a head inside the weapon", static_cast<double>(row.code));
    }
}

static void KindTests() {
    Expect(KindIndex(D2RVR_TYPE_MACE) == kMace && KindIndex(D2RVR_TYPE_SCEPTER) == kScepter && KindIndex(D2RVR_TYPE_SWORD) == kSword && KindIndex(D2RVR_TYPE_AXE) == kAxe &&
               KindIndex(D2RVR_TYPE_STAFF) == kStaff && KindIndex(D2RVR_TYPE_JAVELIN) == kJavelin && KindIndex(D2RVR_TYPE_THROWING) == kThrowing && KindIndex(D2RVR_TYPE_DAGGER) == kDagger,
           "kind indices");
    Expect(KindIndex(D2RVR_TYPE_BOW) < 0 && KindIndex(D2RVR_TYPE_CROSSBOW) < 0 && KindIndex(D2RVR_TYPE_UNKNOWN) < 0, "a bow, a crossbow, nothing: no kind");
    for (int i = 0; i < kKinds; ++i) Expect(KindName(i)[0] != '?' && KindNameW(i)[0] != L'?' && KindLabel(i)[0] != L'?', "every kind has its names", i);
    Expect(FamilyOf(kMace) == Family::Blunt && FamilyOf(kScepter) == Family::Blunt && FamilyOf(kStaff) == Family::Staff && FamilyOf(kSword) == Family::Edged && FamilyOf(kAxe) == Family::Edged &&
               FamilyOf(kPolearm) == Family::Edged && FamilyOf(kDagger) == Family::Piercing && FamilyOf(kSpear) == Family::Piercing && FamilyOf(kClaw) == Family::Piercing &&
               FamilyOf(kUnarmed) == Family::Piercing && FamilyOf(kJavelin) == Family::Piercing,
           "the families");
    // the defaults: every kind judged toward the monster, the blunt ones by their head only, the amplitude by family
    Rule r;
    Expect(r.toward && r.amplitude && r.head && TowardApplies(r, kSword) && TowardApplies(r, kMace) && TowardApplies(r, kThrowing), "defaults: toward for every kind");
    Expect(HeadApplies(r, kMace) && HeadApplies(r, kScepter) && !HeadApplies(r, kSword) && !HeadApplies(r, kStaff), "defaults: only maces and scepters by the head");
    Expect(std::fabs(AmplitudeCm(r, kDagger) - 8.0f) < 1e-4f && std::fabs(AmplitudeCm(r, kSpear) - 8.0f) < 1e-4f, "amplitude: piercing 8 cm", AmplitudeCm(r, kDagger));
    Expect(std::fabs(AmplitudeCm(r, kSword) - 11.2f) < 1e-3f && std::fabs(AmplitudeCm(r, kAxe) - 11.2f) < 1e-3f, "amplitude: edged 1.4 x 8", AmplitudeCm(r, kSword));
    Expect(std::fabs(AmplitudeCm(r, kMace) - 16.0f) < 1e-3f && std::fabs(AmplitudeCm(r, kScepter) - 16.0f) < 1e-3f, "amplitude: blunt 2 x 8", AmplitudeCm(r, kMace));
    Expect(std::fabs(AmplitudeCm(r, kStaff) - 14.0f) < 1e-3f, "amplitude: staff 1.75 x 8", AmplitudeCm(r, kStaff));
    Expect(AmplitudeCm(r, kMace) > AmplitudeCm(r, kSword) && AmplitudeCm(r, kSword) > AmplitudeCm(r, kDagger), "amplitude: blunt > edged > piercing");
    // the slider of a family moves its kinds; a kind's own factor beats its family's (0 = follow)
    r.pierceCm = 10.0f;
    r.bluntX = 3.0f;
    Expect(std::fabs(AmplitudeCm(r, kMace) - 30.0f) < 1e-3f && std::fabs(AmplitudeCm(r, kScepter) - 30.0f) < 1e-3f && std::fabs(AmplitudeCm(r, kDagger) - 10.0f) < 1e-3f, "amplitude: the base and the blunt factor move the kinds");
    r.kindX[kScepter] = 1.5f;
    Expect(std::fabs(AmplitudeCm(r, kScepter) - 15.0f) < 1e-3f && std::fabs(AmplitudeCm(r, kMace) - 30.0f) < 1e-3f, "amplitude: a kind's own factor overrides its family's, the others follow");
    r.amplitude = false;
    Expect(AmplitudeCm(r, kMace) == 0.0f, "amplitude: off asks nothing");
    Expect(AmplitudeX(r, -1) == 1.0f && AmplitudeCm(r, -1) == 0.0f, "no kind: nothing");
    // clamped
    Rule c = Clamp({true, 0xFFFFFFFFu, 200.0f, true, 0xFFFFFFFFu, 500.0f, true, 500.0f, 99.0f, 99.0f, 99.0f, {}});
    Expect(c.towardDeg == 89.0f && c.headPct == 100.0f && c.pierceCm == 60.0f && c.edgedX == 6.0f && c.towardKinds == kAllKinds && c.headKinds == kAllKinds, "clamped high", c.towardDeg);
    Rule d = Clamp({true, 1u, -5.0f, true, 1u, NAN, true, NAN, NAN, -1.0f, 0.0f, {}});
    Expect(d.towardDeg == 20.0f && d.headPct == kDefaultHeadPct && d.pierceCm == kDefaultPierceCm && d.edgedX == kDefaultEdgedX && d.bluntX == 0.5f, "clamped low and not a number", d.towardDeg);
    Rule e;
    e.kindX[kAxe] = 0.1f;
    e.kindX[kMace] = 99.0f;
    e = Clamp(e);
    Expect(e.kindX[kAxe] == 0.0f && e.kindX[kMace] == 8.0f, "a kind's factor under a quarter is none, over 8 is 8");
}

static void TravelTests() {
    StrokeTravel t;
    Vec p{0.0f, 1.0f, 0.5f};
    double ms = 1.0;
    for (int i = 0; i < 8; ++i) { t.Feed(ms, p, 0); ms += 1.0 / 90.0; p.z += 0.01f; }   // moving, no stroke yet: nothing travelled
    Expect(t.Meters() == 0.0f && t.Stroke() == 0, "travel: no stroke, none");
    t.Feed(ms, p, 7);   // a stroke begins now: the last 0.12 s (11 poses of 11 ms) count
    p.z += 0.01f; ms += 1.0 / 90.0;
    Expect(t.Stroke() == 7 && t.Meters() > 0.07f && t.Meters() < 0.095f, "travel: a stroke starts with its onset window's path (about 8-9 cm of the 10 cm moved)", t.Meters());
    const float atStart = t.Meters();
    for (int i = 0; i < 10; ++i) { t.Feed(ms, p, 7); ms += 1.0 / 90.0; p.z += 0.01f; }
    Expect(std::fabs(t.Meters() - (atStart + 0.10f)) < 0.002f, "travel: it adds every step of the stroke (10 steps of 1 cm)", t.Meters());
    // a new stroke starts again from its onset window, not from the old total
    t.Feed(ms, p, 8);
    Expect(t.Stroke() == 8 && t.Meters() < atStart + 0.05f, "travel: a new stroke does not carry the old one's path", t.Meters());
    // the path is the head's own: a tip that goes there and back counts both ways
    StrokeTravel w;
    ms = 1.0;
    p = {0.0f, 1.0f, 0.5f};
    for (int i = 0; i < 6; ++i) { w.Feed(ms, p, 3); ms += 1.0 / 90.0; p.y += 0.02f; }
    for (int i = 0; i < 6; ++i) { w.Feed(ms, p, 3); ms += 1.0 / 90.0; p.y -= 0.02f; }
    Expect(w.Meters() > 0.2f, "travel: up and back is a path of both", w.Meters());
    // no stroke: reset; time going back: reset; a tip that is not a number: reset
    w.Feed(ms, p, 0);
    Expect(w.Meters() == 0.0f, "travel: the stroke ended");
    w.Feed(ms, p, 4);
    w.Feed(ms - 5.0, p, 4);
    Expect(w.Meters() >= 0.0f, "travel: time going back is survived");
    w.Feed(ms, {NAN, 0.0f, 0.0f}, 4);
    Expect(w.Meters() == 0.0f && w.Stroke() == 0, "travel: a tip that is not a number resets it");
}

static void WayTests() {
    TipWay w;
    double ms = 1.0;
    Vec p{0.0f, 1.0f, 0.5f};
    Expect(Length(w.Way()) == 0.0f, "way: none before anything");
    for (int i = 0; i < 6; ++i) { w.Feed(ms, p); ms += 1.0 / 90.0; p.y += 0.01f; }
    const Vec up = w.Way();
    Expect(NearV(up, {0.0f, 1.0f, 0.0f}, 1e-4f), "way: going up");
    for (int i = 0; i < 6; ++i) { w.Feed(ms, p); ms += 1.0 / 90.0; p.z -= 0.01f; }
    const Vec back = w.Way();   // the last 40 ms: back
    Expect(back.z < -0.7f, "way: the last 40 ms say back", back.z);
    TipWay still;
    for (int i = 0; i < 6; ++i) still.Feed(1.0 + i / 90.0, {0.0f, 1.0f, 0.5f});
    Expect(Length(still.Way()) == 0.0f, "way: a tip that did not move has none");
}

// ---- Judge on single sweeps: a monster whose axis stands at z = 1 (radius 0.3), the blade 1 m long, the tip the touching point ----
static Body Monster() { return {{0.0f, 0.0f, 1.0f}, {0.0f, 1.8f, 1.0f}, 0.3f}; }
static Sweep TipSweep(Vec from, Vec to) {
    Sweep sw;
    sw.before = {{0.0f, 1.2f, 0.0f}, from, 0.065f};
    sw.after = {{0.0f, 1.2f, 0.0f}, to, 0.065f};
    return sw;
}
static Contact AtTip(float t = 0.5f, float along = 1.0f) { Contact c; c.t = t; c.along = along; c.speed = 4.0f; return c; }

static Judgement JudgeTip(const Rule& r, int kind, Vec from, Vec to, float travelCm = 40.0f, float along = 1.0f, float t = 0.5f) {
    return Judge(r, kind, TipSweep(from, to), AtTip(t, along), Monster(), travelCm * 0.01f);
}

static void JudgeTests() {
    const Rule rule;   // toward 60 deg for every kind, the head for maces and scepters, the amplitude by family
    // An overhead blow: the head high above the monster, coming down and forward onto it: toward, counts (the axis' top is the nearest point)
    Judgement j = JudgeTip(rule, kMace, {0.0f, 2.3f, 0.8f}, {0.0f, 2.0f, 0.95f});
    Expect(j.why == Why::Counts && j.haveWay && j.haveAxis && j.angleDeg < 20.0f, "overhead down onto the monster: counts", j.angleDeg);
    // An uppercut, a wheel from below: up and forward INTO the monster: counts, whatever the vertical direction
    j = JudgeTip(rule, kMace, {0.0f, 0.5f, 0.6f}, {0.0f, 0.7f, 0.8f});
    Expect(j.why == Why::Counts && j.angleDeg < 50.0f && j.dir.y > 0.5f, "an uppercut from below into the monster: counts", j.angleDeg);
    // The wind-up: the head goes up and back, away from the monster in front of it: refused
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.95f}, {0.0f, 1.7f, 0.6f});
    Expect(j.why == Why::Away && j.angleDeg > 120.0f, "a wind-up up and back from a monster in front: refused as not toward", j.angleDeg);
    // ... and past the monster's side, rising and drawing back: across it, refused
    j = JudgeTip(rule, kMace, {0.3f, 1.0f, 0.9f}, {0.3f, 1.8f, 0.5f});
    Expect(j.why == Why::Away && j.angleDeg > 80.0f, "a wind-up rising past the monster's side: refused (across it)", j.angleDeg);
    // A sideways sweep into the monster: counts; a graze along its front (no way in): refused
    j = JudgeTip(rule, kMace, {-0.6f, 1.2f, 0.9f}, {-0.2f, 1.2f, 0.9f});
    Expect(j.why == Why::Counts && j.angleDeg < 25.0f, "a sideways sweep into the monster: counts", j.angleDeg);
    j = JudgeTip(rule, kMace, {-0.6f, 1.2f, 0.66f}, {0.6f, 1.2f, 0.66f});
    Expect(j.why == Why::Away && j.angleDeg > 80.0f && j.angleDeg < 100.0f, "a graze along the front of the monster: refused (across it)", j.angleDeg);
    // A thrust / a shove of the head into it: counts (no arc needed)
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f});
    Expect(j.why == Why::Counts && j.angleDeg < 5.0f, "a shove of the head straight into the monster: counts", j.angleDeg);
    // Pulling the head back after the blow: away
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.8f}, {0.0f, 1.2f, 0.5f});
    Expect(j.why == Why::Away && j.angleDeg > 170.0f, "the pull-back: refused", j.angleDeg);
    // The angle is the user's: 45 deg to the axis counts at 60 and not at 40
    const Vec a45from{0.0f, 1.2f, 0.6f}, a45to{0.0f, 1.2f + 0.2f * 0.7071f, 0.6f + 0.2f * 0.7071f};   // 45 deg above the horizontal, toward the axis (level with the tip)
    j = JudgeTip(rule, kMace, a45from, a45to);
    Expect(j.why == Why::Counts && std::fabs(j.angleDeg - 45.0f) < 3.0f, "a movement 45 deg off the line to the axis counts at 60", j.angleDeg);
    Rule tight = rule;
    tight.towardDeg = 40.0f;
    j = JudgeTip(tight, kMace, a45from, a45to);
    Expect(j.why == Why::Away, "... and not at 40", j.angleDeg);
    // the head: the outer 40 % of the blade for maces and scepters; the shaft or the handle is not
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.3f);
    Expect(j.why == Why::NotHead, "a mace: the shaft (30 % along it) is not the head", j.along);
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.59f);
    Expect(j.why == Why::NotHead, "a mace: just under the head's start (59 %): not the head");
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.62f);
    Expect(j.why == Why::Counts, "a mace: 62 % along it is the head");
    j = JudgeTip(rule, kSword, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.3f);
    Expect(j.why == Why::Counts, "a sword: any part of the blade strikes (the head rule is for maces and scepters)");
    Rule wide = rule;
    wide.headPct = 100.0f;
    Expect(JudgeTip(wide, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.05f).why == Why::Counts, "head 100 %: the whole blade");
    Rule axeHead = rule;
    axeHead.headKinds |= Bit(kAxe);
    Expect(JudgeTip(axeHead, kAxe, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.3f).why == Why::NotHead, "an axe ticked: the head only");
    // [gameplay] melee_not_butt: a poleaxe held in the middle (a player, 2026-10-24: "killing monsters with the back end of the shaft on the return stroke") -
    // the front hand at 50 % along it: the butt half never strikes, however fast; the head does; unticked, or no front hand known, as before
    Rule pole = rule;
    pole.frontAlong = 0.5f;
    j = JudgeTip(pole, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.2f);
    Expect(j.why == Why::Butt, "a poleaxe held in the middle: its butt end (20 % along it) is no hit", j.along);
    j = JudgeTip(pole, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.49f);
    Expect(j.why == Why::Butt, "... just behind the front hand (49 %): no hit");
    Expect(JudgeTip(pole, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.9f).why == Why::Counts, "... its head (90 %) strikes");
    Expect(JudgeTip(pole, kSword, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.2f).why == Why::Butt, "the same for any kind: a sword's pommel behind the hand is no hit");
    Rule poleOff = pole;
    poleOff.notButt = false;
    Expect(JudgeTip(poleOff, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.2f).why == Why::Counts, "melee_not_butt off: the butt strikes, as before");
    Expect(JudgeTip(rule, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.2f).why == Why::Counts, "no front hand known (a fist weapon, a ball): as before");
    // [gameplay] melee_metal_head: the head starts at 70 % (a Great Poleaxe's, from its model) - the shaft below it, in front of the hands too, pushes and is no hit
    Rule metal = rule;
    metal.headAlong = 0.7f;
    j = JudgeTip(metal, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.55f);
    Expect(j.why == Why::Shaft, "a polearm: the shaft in front of the hands (55 %) is no hit", j.along);
    Expect(JudgeTip(metal, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.72f).why == Why::Counts, "... its head (72 %) strikes");
    Rule metalPole = metal;
    metalPole.frontAlong = 0.5f;
    Expect(JudgeTip(metalPole, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.2f).why == Why::Butt, "... behind the hands it is still the butt's refusal");
    Rule metalOff = metal;
    metalOff.metalHead = false;
    Expect(JudgeTip(metalOff, kPolearm, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.55f).why == Why::Counts, "melee_metal_head off: the shaft strikes, as before");
    Expect(JudgeTip(ForBall(metal), kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.3f).why == Why::Counts, "a flail's ball: nothing below a head to refuse");
    Expect(std::strcmp(WhyText(Why::Shaft), WhyText(Why::Butt)) != 0 && std::strcmp(WhyText(Why::Shaft), WhyText(Why::Counts)) != 0, "the shaft's own text");
    {   // [gameplay] melee_head_volume: a head volume's contact is the head - none of the shaft's, the butt's or the head share's refusals
        Rule hr = metalPole;
        hr.headAlong = 0.83f;
        hr.headKinds |= Bit(kAxe);
        Expect(JudgeTip(ForHeadVolume(hr), kAxe, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.1f).why == Why::Counts, "the head volume's contact counts wherever along it");
        Expect(JudgeTip(ForHeadVolume(hr), kAxe, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.1f}, 40.0f, 0.1f).why == Why::Away, "... toward the monster still judged");
    }
    // the amplitude: a short tap fails for a mace (16 cm) and passes for a dagger (8 cm); a full swing passes for both
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 6.0f);
    Expect(j.why == Why::TooSmall && std::fabs(j.needCm - 16.0f) < 1e-3f && std::fabs(j.travelCm - 6.0f) < 1e-3f, "a short tap with a mace: too small", j.travelCm);
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 10.0f);
    Expect(j.why == Why::TooSmall, "a 10 cm jab with a mace: still too small");
    j = JudgeTip(rule, kDagger, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 10.0f);
    Expect(j.why == Why::Counts, "the same 10 cm jab with a dagger: counts");
    j = JudgeTip(rule, kDagger, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 6.0f);
    Expect(j.why == Why::TooSmall, "a 6 cm flick with a dagger: too small");
    j = JudgeTip(rule, kSword, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 10.0f);
    Expect(j.why == Why::TooSmall, "10 cm with a sword (11.2): too small");
    j = JudgeTip(rule, kSword, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 12.0f);
    Expect(j.why == Why::Counts, "12 cm with a sword: counts");
    j = JudgeTip(rule, kStaff, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 13.0f);
    Expect(j.why == Why::TooSmall, "13 cm with a staff (14): too small");
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f);
    Expect(j.why == Why::Counts, "a full swing with a mace: counts");
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 20.0f);
    Expect(j.why == Why::Counts, "20 cm with a mace (16): counts - a sledge shove with enough amplitude");
    // the order: not the head first, then not toward, then too small
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.8f}, {0.0f, 1.2f, 0.5f}, 2.0f, 0.2f);
    Expect(j.why == Why::NotHead, "the reasons in order: the head first");
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.8f}, {0.0f, 1.2f, 0.5f}, 2.0f);
    Expect(j.why == Why::Away, "... then toward");
    // every switch: the rules off count everything, as they always did
    Rule off = rule;
    off.toward = false;
    Expect(JudgeTip(off, kMace, {0.0f, 1.2f, 0.95f}, {0.0f, 1.7f, 0.6f}).why == Why::Counts, "toward off: the wind-up counts");
    off = rule;
    off.towardKinds &= ~Bit(kMace);
    Expect(JudgeTip(off, kMace, {0.0f, 1.2f, 0.95f}, {0.0f, 1.7f, 0.6f}).why == Why::Counts && JudgeTip(off, kSword, {0.0f, 1.2f, 0.95f}, {0.0f, 1.7f, 0.6f}).why == Why::Away, "toward off for the mace alone: the mace counts it, the sword does not");
    off = rule;
    off.head = false;
    Expect(JudgeTip(off, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.3f).why == Why::Counts, "head off: the shaft counts");
    off = rule;
    off.amplitude = false;
    Expect(JudgeTip(off, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 1.0f).why == Why::Counts, "amplitude off: a flick counts");
    // every kind obeys the direction rule - a backswing away from a monster hits with none
    for (int k = 0; k < kKinds; ++k) Expect(JudgeTip(rule, k, {0.0f, 1.2f, 0.8f}, {0.0f, 1.2f, 0.5f}, 60.0f, 0.9f).why == (HeadApplies(rule, k) && 0.9f < 0.6f ? Why::NotHead : Why::Away), "every kind: moving away from the monster is refused", k);
    Expect(JudgeTip(rule, -1, {0.0f, 1.2f, 0.8f}, {0.0f, 1.2f, 0.5f}).why == Why::Counts, "no kind (not a melee weapon): nothing is judged");
    // the contact's marks: the point at the moment of the touch (half way), the way, the nearest point of the axis
    j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.8f});
    Expect(NearV(j.at, {0.0f, 1.2f, 0.6f}, 1e-5f) && NearV(j.dir, {0.0f, 0.0f, 1.0f}, 1e-5f) && NearV(j.onAxis, {0.0f, 1.2f, 1.0f}, 1e-5f) && j.haveAxis, "the touching point, its way and the axis' nearest point are given for the drawing");
    // a point on the monster's axis has no direction to it: only the other rules judge
    Contact onAxis = AtTip();
    onAxis.t = 1.0f;
    j = Judge(rule, kMace, TipSweep({0.0f, 1.2f, 0.7f}, {0.0f, 1.2f, 1.0f}), onAxis, Monster(), 0.4f);
    Expect(!j.haveAxis && j.why == Why::Counts, "on the axis itself: no direction to it, counts");
    // under 5 mm: no way to speak of
    j = Judge(rule, kMace, TipSweep({0.0f, 1.2f, 0.9f}, {0.0f, 1.2f, 0.903f}), AtTip(), Monster(), 0.4f);
    Expect(!j.haveWay && j.why == Why::Counts, "a movement under 5 mm has no way: counts (the speed gates stop it anyway)");
    // the point's own place: a contact at the middle of a blade that turns about its grip is judged by that point's way
    Sweep turn;
    turn.before = {{0.0f, 1.2f, 0.0f}, {0.0f, 2.2f, 0.0f}, 0.065f};   // pointing up
    turn.after = {{0.0f, 1.2f, 0.0f}, {0.0f, 1.2f, 1.0f}, 0.065f};    // swung down to point at the monster
    Contact mid;
    mid.t = 0.7f; mid.along = 0.6f; mid.speed = 4.0f;
    j = Judge(rule, kMace, turn, mid, Monster(), 0.6f);
    Expect(j.haveWay && j.along == 0.6f, "a blade turned about its grip: the way of the point that touched");
}

static void LookTests() {
    const Rule rule;
    const Body m = Monster();
    const Vec tip{0.0f, 1.2f, 0.8f};
    Expect(LookOf(rule, kMace, false, tip, {0.0f, 0.0f, 1.0f}, &m, 0.5f) == Look::Cold, "look: no stroke counting - cold");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, 1.0f}, &m, 0.5f) == Look::Hot, "look: counting, toward the monster, a big swing - hot");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, -1.0f}, &m, 0.5f) == Look::Away, "look: counting but moving away from the monster - away (blue)");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 1.0f, 0.0f}, &m, 0.5f) == Look::Away, "look: counting, rising beside it - away (blue)");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, 1.0f}, &m, 0.05f) == Look::Small, "look: toward it but the swing is only 5 cm of a mace's 16 - small (amber)");
    Expect(LookOf(rule, kDagger, true, tip, {0.0f, 0.0f, 1.0f}, &m, 0.10f) == Look::Hot, "look: the same 10 cm with a dagger - hot");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, -1.0f}, nullptr, 0.5f) == Look::Hot, "look: no monster near - the direction cannot be judged, as it was");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, -1.0f}, nullptr, 0.05f) == Look::Small, "look: ... but the amplitude can");
    Expect(LookOf(rule, kMace, true, tip, {0.0f, 0.0f, 0.0f}, &m, 0.5f) == Look::Hot, "look: no way known - not judged");
    Rule off = rule;
    off.toward = false;
    off.amplitude = false;
    Expect(LookOf(off, kMace, true, tip, {0.0f, 0.0f, -1.0f}, &m, 0.0f) == Look::Hot, "look: the rules off - green, as it was");
}

static void TuningTests() {
    const StrokeTuning general{0.8f, 8.0f};
    StrokeTuning t = Effective(general, {});
    Expect(t.minSpeed == 0.8f && t.startCm == 8.0f, "no override: the general numbers");
    t = Effective(general, {3.0f, 0.0f});
    Expect(t.minSpeed == 3.0f && t.startCm == 8.0f, "a speed override only");
    t = Effective(general, {0.0f, 14.0f});
    Expect(t.minSpeed == 0.8f && t.startCm == 14.0f, "a start override only");
    t = Effective(general, {0.2f, 1.0f});
    Expect(t.minSpeed == 0.8f && t.startCm == 8.0f, "under the engine's least (0.3 m/s, 2 cm): not an override");
    t = Effective(general, {50.0f, 90.0f});
    Expect(t.minSpeed == 20.0f && t.startCm == 30.0f, "clamped (the least speed to 20 m/s, the start to 30 cm)");
    t = Effective(general, {15.0f, 0.0f});
    Expect(t.minSpeed == 15.0f && t.startCm == 8.0f, "a kind's own least speed may be as high as the general slider's top");
    t = Effective(general, {NAN, NAN});
    Expect(t.minSpeed == 0.8f && t.startCm == 8.0f, "not numbers: the general ones");
}

// ---- the real engine on swings ----

struct Run { unsigned presses = 0; int refusedAway = 0, refusedHead = 0, refusedSmall = 0, firstStep = -1, pressesAtRaise = 0, pressesAtBlow = 0; };

// The touch loop of melee_touch.cpp on a path of the blade: the engine, the travel, the contact, the speed gate, the rules, the hit set.
template <class PoseOf>
static Run Loop(const Rule& rule, int kind, StrokeTuning tune, int steps, PoseOf poseOf, const Body& monster, bool* raising = nullptr) {
    Engine eng(ParamsFor(tune));
    HitSet hits;
    StrokeTravel travel;
    Run run;
    double t = 1.0;
    for (int i = 0; i < steps; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        Blade b;
        Quat q;
        poseOf(i, &b, &q);
        s.hand = {b.base, q};
        s.blade = b;
        s.anchor = {0.0f, 1.2f, 0.0f};
        s.scale = 4.0f;
        s.lever = Length(b.tip - b.base);
        s.room = s.blade;
        s.haveRoom = true;
        Sweep sw;
        const bool swept = eng.Step(s, sw);
        travel.Feed(t, s.room.tip, eng.StrokeId());
        if (swept) {
            const Contact c = TouchOf(sw, monster);
            if (c.t >= 0.0f && !hits.Contains(1, sw.stroke, 1) && c.speed >= eng.GetParams().strokeSpeed) {
                const Judgement j = Judge(rule, kind, sw, c, monster, travel.Meters());
                if (g_verbose)
                    std::printf("      step %3d stroke %llu travel %5.1f cm way %5.1f deg along %.2f -> %s\n", i, static_cast<unsigned long long>(sw.stroke), travel.Meters() * 100.0f, j.angleDeg, j.along, WhyText(j.why));
                if (j.why == Why::Away) ++run.refusedAway;
                else if (j.why == Why::NotHead) ++run.refusedHead;
                else if (j.why == Why::TooSmall) ++run.refusedSmall;
                else if (hits.Insert(1, sw.stroke, 1)) {
                    ++run.presses;
                    if (run.firstStep < 0) run.firstStep = i;
                    if (raising && *raising) ++run.pressesAtRaise; else ++run.pressesAtBlow;
                }
            }
        }
        t += 1.0 / 90.0;
    }
    return run;
}

// A hammer 0.8 m long about the hand at (0, 1.3, 0.25), straight ahead (z) at the angle theta above the horizontal: rest at +20 deg (the head already in the monster, its axis at z = 1.2), raised to
// 120 deg - up and BACK, away from the monster - at `up` rad/s, then brought down onto it to -30 deg at `down` rad/s. raising says which half the step is in.
static Run Overhead(const Rule& rule, int kind, float up, float down) {
    const Body monster{{0.0f, 0.0f, 1.2f}, {0.0f, 1.8f, 1.2f}, 0.3f};
    float theta = 20.0f * kRad;
    int rest = 0;
    bool raising = true, turned = false;
    const auto pose = [&](int, Blade* b, Quat* q) {
        const float a = -theta;
        const Vec dir{0.0f, std::sin(theta), std::cos(theta)}, hand{0.0f, 1.3f, 0.25f};
        *b = {hand, hand + dir * 0.8f, 0.065f};
        *q = Quat{std::sin(a * 0.5f), 0.0f, 0.0f, std::cos(a * 0.5f)};
        // the next pose's angle
        if (rest < 12) ++rest;
        else if (!turned) { theta += up / 90.0f; if (theta >= 120.0f * kRad) { turned = true; raising = false; } }
        else if (theta > -30.0f * kRad) theta -= down / 90.0f;
    };
    return Loop(rule, kind, {0.8f, 8.0f}, 160, pose, monster, &raising);
}

static void SwingTests() {
    // the old behaviour (every rule off): the raise through the monster hits - the reported bug
    Rule none;
    none.toward = false;
    none.head = false;
    none.amplitude = false;
    Run r = Overhead(none, kMace, 9.0f, 12.0f);
    std::printf("  a hammer raised up and back, then brought down onto the monster - rules off: %u presses (%d on the raise, %d on the blow)\n", r.presses, r.pressesAtRaise, r.pressesAtBlow);
    Expect(r.pressesAtRaise >= 1, "rules off: the raise hits (the reported bug)", r.pressesAtRaise);
    Expect(r.pressesAtBlow >= 1, "rules off: the blow down hits too (a stroke of its own)", r.pressesAtBlow);
    // the default rules: the raise (away from the monster) is refused, the blow down hits
    const Rule rule;
    r = Overhead(rule, kMace, 9.0f, 12.0f);
    std::printf("  ... default rules (a mace): %u presses (%d on the raise, %d on the blow), refused: %d not toward, %d not the head, %d too small\n", r.presses, r.pressesAtRaise, r.pressesAtBlow, r.refusedAway,
                r.refusedHead, r.refusedSmall);
    Expect(r.pressesAtRaise == 0, "a mace: the raise does not hit", r.pressesAtRaise);
    Expect(r.pressesAtBlow >= 1, "a mace: the blow down hits", r.pressesAtBlow);
    Expect(r.refusedAway >= 1, "a mace: the raise's contacts are refused as not toward the monster, not lost", r.refusedAway);
    // slower and faster raises: the rule is about the way, not the speed
    r = Overhead(rule, kMace, 5.0f, 12.0f);
    Expect(r.pressesAtRaise == 0 && r.pressesAtBlow >= 1, "a slower raise: still no hit, the blow hits", r.pressesAtRaise);
    r = Overhead(rule, kMace, 14.0f, 14.0f);
    Expect(r.pressesAtRaise == 0 && r.pressesAtBlow >= 1, "a fast raise: still no hit", r.pressesAtRaise);
    // a sword and an axe obey the direction rule too: the backswing away from the monster hits with none
    r = Overhead(rule, kSword, 9.0f, 12.0f);
    Expect(r.pressesAtRaise == 0 && r.pressesAtBlow >= 1, "a sword: the raise away from the monster does not hit either", r.pressesAtRaise);
    // ... unless that kind is switched off
    Rule swordFree = rule;
    swordFree.towardKinds &= ~Bit(kSword);
    r = Overhead(swordFree, kSword, 9.0f, 12.0f);
    Expect(r.pressesAtRaise >= 1, "toward off for the sword: the raise hits as it always did", r.pressesAtRaise);
    // the angle slider: at 89 deg nearly anything not away counts, the raise still goes away
    Rule loose = rule;
    loose.towardDeg = 89.0f;
    r = Overhead(loose, kMace, 9.0f, 12.0f);
    Expect(r.pressesAtRaise == 0, "toward 89 deg: the raise still goes away from the monster", r.pressesAtRaise);
}

// A rod 0.8 m long moved straight ahead (along +z, the head in front) toward a thin post: the `distance` in `samples` poses after a rest. The kinds' own numbers: the stroke's start distance 2 cm so that
// even a short tap is a stroke - what keeps it from hitting is the amplitude.
static Run PokeRun(const Rule& rule, int kind, float distance, int samples, float speedCheck = 0.8f) {
    const float tipStart = 0.25f + 0.8f;
    const Body post{{0.0f, 0.0f, tipStart + distance + 0.065f + 0.03f - 0.004f}, {0.0f, 1.8f, tipStart + distance + 0.065f + 0.03f - 0.004f}, 0.03f};
    const auto pose = [&](int i, Blade* b, Quat* q) {
        const float k = i < 12 ? 0.0f : std::min(1.0f, static_cast<float>(i - 12) / static_cast<float>(samples));
        const Vec hand{0.0f, 1.2f, 0.25f + distance * k};
        *b = {hand, hand + Vec{0.0f, 0.0f, 0.8f}, 0.065f};
        *q = Quat{};
    };
    return Loop(rule, kind, {speedCheck, 2.0f}, 70, pose, post);
}
static unsigned Poke(const Rule& rule, int kind, float distance, int samples, float speedCheck = 0.8f) { return PokeRun(rule, kind, distance, samples, speedCheck).presses; }
// Refused for the swing's size, not for want of a contact (the loop saw the touch and said "too small").
static bool TooSmall(const Rule& rule, int kind, float distance, int samples) {
    const Run r = PokeRun(rule, kind, distance, samples);
    return r.presses == 0 && r.refusedSmall >= 1;
}

static void AmplitudeTests() {
    const Rule rule;
    // a 6 cm tap in 3 poses (1.8 m/s): a stroke at the start distance 2 cm, a touch at 1.8 m/s - but a mace needs 16 cm, a dagger 8; a sledge shove of 30 cm hits; a dagger jab of 12 cm hits
    Expect(TooSmall(rule, kMace, 0.06f, 3), "a 6 cm tap with a mace: seen as a touch and refused (too small)");
    Expect(TooSmall(rule, kDagger, 0.06f, 3), "a 6 cm tap with a dagger: refused too (8 cm)");
    Expect(Poke(rule, kDagger, 0.12f, 8) == 1, "a 12 cm jab with a dagger: hits");
    Expect(TooSmall(rule, kMace, 0.12f, 8), "the same 12 cm shove with a mace: refused (16 cm)");
    Expect(Poke(rule, kMace, 0.30f, 14) == 1, "a 30 cm shove of a mace head into the monster: hits (no arc needed)");
    Expect(Poke(rule, kSword, 0.12f, 8) == 1, "12 cm with a sword (11.2): hits");
    Expect(TooSmall(rule, kStaff, 0.12f, 8) && Poke(rule, kStaff, 0.20f, 12) == 1, "a staff (14 cm): 12 cm no, 20 cm yes");
    // the sliders: the piercing amplitude is the base, the blunt factor moves the maces alone
    Rule r2 = rule;
    r2.pierceCm = 4.0f;
    Expect(Poke(r2, kDagger, 0.06f, 3) == 1 && Poke(r2, kMace, 0.12f, 8) == 1 && TooSmall(r2, kMace, 0.06f, 3), "piercing 4 cm: the dagger's 6 cm hits, the mace (8 cm now) wants more than 6");
    Rule r3 = rule;
    r3.bluntX = 1.0f;
    Expect(Poke(r3, kMace, 0.12f, 8) == 1, "blunt x1: the mace needs only the piercing 8 cm");
    Rule r4 = rule;
    r4.kindX[kMace] = 4.0f;
    Expect(TooSmall(r4, kMace, 0.30f, 14) && Poke(r4, kMace, 0.40f, 18) == 1 && Poke(r4, kScepter, 0.20f, 10) == 1, "a mace's own factor 4 (32 cm): 30 cm no, 40 cm yes; the scepter keeps the blunt one");
    Rule r5 = rule;
    r5.amplitude = false;
    Expect(Poke(r5, kMace, 0.06f, 3) == 1, "amplitude off: the short tap hits");
    // a tap too slow to be a stroke is not a touch at all
    Expect(Poke(rule, kDagger, 0.06f, 5) == 0 && PokeRun(rule, kDagger, 0.06f, 5).refusedSmall == 0, "a 6 cm push at 1.1 m/s (under the engine's 1.2 start speed) is no stroke: no touch to judge");
    // the least speed is the engine's (the kind's own number is the engine's gate): a slow shove under it does nothing
    Expect(Poke(rule, kDagger, 0.12f, 60, 0.8f) == 0, "a very slow shove (under the engine's speed): nothing - the amplitude does not replace the speed");
}

// [flail] chain: a flail's ball (ForBall) - the whole ball is the head: a touch anywhere on it is not refused as "not the head"; toward the monster and the
// amplitude still judge it, by the ball's own motion.
static void BallTests() {
    const Rule rule;
    const Rule ball = ForBall(rule);
    // a touch at the blade's grip end (along 0): the mace's head rule refuses it; the ball's does not
    Judgement j = JudgeTip(rule, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.0f);
    Expect(j.why == Why::NotHead, "ball: the same touch at a mace's grip end is not the head", static_cast<double>(j.why));
    j = JudgeTip(ball, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 40.0f, 0.0f);
    Expect(j.why == Why::Counts, "ball: anywhere on the ball counts", static_cast<double>(j.why));
    // moving away from the monster: refused, ball or not
    j = JudgeTip(ball, kMace, {0.0f, 1.2f, 0.7f}, {0.0f, 1.2f, 0.4f}, 40.0f, 0.5f);
    Expect(j.why == Why::Away, "ball: going away from the monster does not count", static_cast<double>(j.why));
    // a limp ball that hardly swung: too small
    j = JudgeTip(ball, kMace, {0.0f, 1.2f, 0.4f}, {0.0f, 1.2f, 0.7f}, 2.0f, 0.5f);
    Expect(j.why == Why::TooSmall, "ball: a swing of 2 cm is too small", static_cast<double>(j.why));
    Expect(ball.toward == rule.toward && ball.amplitude == rule.amplitude && ball.towardDeg == rule.towardDeg && !ball.head, "ball: only the head rule is off");
}

// [gameplay] melee_not_backward: a spear held forward (+z), its butt 0.7 m behind the grip; a movement from its point toward its handle never strikes.
static Sweep SpearSweep(Vec move) {
    Sweep sw;
    sw.before = {{0.0f, 1.2f, -0.7f}, {0.0f, 1.2f, 1.3f}, 0.03f};
    sw.after = {sw.before.base + move, sw.before.tip + move, 0.03f};
    return sw;
}
static void BackwardTests() {
    Rule rule;   // the spear: toward on, not the head only
    const Body behind{{0.0f, 0.0f, -1.0f}, {0.0f, 1.8f, -1.0f}, 0.3f};
    // A jerk back that meets a monster BEHIND with the butt: the butt moves toward it, so `toward` alone counts it ...
    Judgement j = Judge(rule, kSpear, SpearSweep({0.0f, 0.0f, -0.2f}), AtTip(0.5f, 0.0f), behind, 0.4f);
    Expect(j.why == Why::Counts && j.axisCos < -0.99f, "backward off: a butt-first jerk into a monster behind counts (as before)", j.axisCos);
    // ... the rule refuses it
    rule.backward = true;
    j = Judge(rule, kSpear, SpearSweep({0.0f, 0.0f, -0.2f}), AtTip(0.5f, 0.0f), behind, 0.4f);
    Expect(j.why == Why::Backward, "backward on: a butt-first jerk is refused - from the point toward the handle");
    // the thrust itself, point first into a monster in front: counts
    const Body ahead{{0.0f, 0.0f, 1.7f}, {0.0f, 1.8f, 1.7f}, 0.3f};
    j = Judge(rule, kSpear, SpearSweep({0.0f, 0.0f, 0.2f}), AtTip(), ahead, 0.4f);
    Expect(j.why == Why::Counts && j.axisCos > 0.99f, "backward on: the thrust forward counts", j.axisCos);
    // a sideways sweep of the spear (across its axis): counts
    const Body right{{0.6f, 0.0f, 1.0f}, {0.6f, 1.8f, 1.0f}, 0.3f};
    j = Judge(rule, kSpear, SpearSweep({0.2f, 0.0f, 0.0f}), AtTip(0.5f, 0.85f), right, 0.4f);
    Expect(j.why == Why::Counts && std::fabs(j.axisCos) < 0.05f, "backward on: a sideways sweep counts", j.axisCos);
    // back and sideways: 45 deg off the handle's direction is refused, 70 deg is not
    j = Judge(rule, kSpear, SpearSweep({0.1414f, 0.0f, -0.1414f}), AtTip(0.5f, 0.0f), behind, 0.4f);
    Expect(j.why == Why::Backward, "backward on: 45 deg off the handle's direction is refused", j.axisCos);
    j = Judge(rule, kSpear, SpearSweep({0.188f, 0.0f, -0.0684f}), AtTip(0.5f, 0.85f), right, 0.4f);
    Expect(j.why != Why::Backward && j.axisCos > -0.5f, "backward on: 70 deg off it is not backward", j.axisCos);
}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && argv[1][0] == '-' && argv[1][1] == 'v';
    BackwardTests();
    BallTests();
    KindTests();
    MetalHeadTests();
    TravelTests();
    WayTests();
    JudgeTests();
    LookTests();
    TuningTests();
    SwingTests();
    AmplitudeTests();
    if (g_failures == 0) std::printf("strike_direction_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
