// Offline checks of the touch's blade (vr/blade_shape.h): the shape by kind and by base item, the live tuning (length %, butt %, radius, flip), the
// dual-wield rule and the frame - the weapon bone as the skeleton hook keeps it (origin and Y axis in the head's frame) put in the render world with the
// same arithmetic as vrcam's red line, so that the capsule lies ON the drawn weapon: its centre line passes through the bone's origin along the bone's
// axis, zero sideways offset, and it covers the weapon from the butt behind the bone to the tip before it. Synthetic poses, no game, no headset.
// Run: build/Release/blade_shape_test.exe   (exit code 0 = all passed)

#include "../../vr/blade_shape.h"

#include <cmath>
#include <cstdio>

using namespace stroke;
using namespace bladeshape;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// The distance from a point to the infinite line through a and b.
static float LineDistance(Vec p, Vec a, Vec b) {
    const Vec d = Normalized(b - a);
    const Vec r = p - a;
    return Length(r - d * Dot(r, d));
}
// How far along the line a -> b a point is (metres from a).
static float Along(Vec p, Vec a, Vec b) { return Dot(p - a, Normalized(b - a)); }

// vrcam's red line (the phantom block of OnBeginEffects): a point of the head's frame, metres from the eyes -> world units from where the hands hang.
static Vec RedLine(const HeadFrame& h, const float v[3]) {
    Vec w;
    w.x = (v[0] * h.right[0] - v[2] * h.fwd[0]) * h.upm;
    w.y = (v[0] * h.right[1] - v[2] * h.fwd[1] + v[1]) * h.upm;
    w.z = (v[0] * h.right[2] - v[2] * h.fwd[2]) * h.upm;
    return w;
}

static HeadFrame MakeHead(float yawDeg, float upm, const float eyeWorld[3]) {
    HeadFrame h;
    h.ok = true;
    const float y = yawDeg * 0.0174532925f;
    // the view's forward on the ground and its right (a right-handed pair: right = fwd x up)
    h.fwd[0] = std::sin(y); h.fwd[1] = 0.0f; h.fwd[2] = std::cos(y);
    h.right[0] = std::cos(y); h.right[1] = 0.0f; h.right[2] = -std::sin(y);
    for (int i = 0; i < 3; ++i) h.eyeWorld[i] = eyeWorld[i];
    h.upm = upm;
    return h;
}

static void ShapeTests() {
    Shape s = ForKind(D2RVR_TYPE_DAGGER, false);
    Expect(Near(s.length, 0.50f, 1e-6f) && s.tipSign == -1 && Near(s.radius, 0.030f, 1e-6f) && s.butt == 0.0f, "shape: the dagger - 0.5 m, its tip along the bone's -Y, no butt");
    s = ForKind(D2RVR_TYPE_STAFF, true);
    Expect(Near(s.length, 0.66f, 1e-6f) && s.tipSign == 1, "shape: the staff - the segment ends 0.66 m from the bone (the head at 0.67, measured on the short staff), tip along +Y");
    Expect(Near(s.radius, 0.016f, 1e-6f) && s.radius < 0.025f * 0.7f, "shape: the staff's shaft is 1.6 cm in radius - the 2.5 cm of the first try drew twice as thick as the wood");
    Expect(Near(s.butt, 0.66f, 1e-6f), "shape: the staff runs as far behind the bone as before it (the bone is the middle of the grip), so its butt is covered");
    Expect(ForKind(D2RVR_TYPE_SPEAR, true).butt > 0.0f && ForKind(D2RVR_TYPE_POLEARM, true).butt > 0.0f && ForKind(D2RVR_TYPE_SPEAR, true).butt < ForKind(D2RVR_TYPE_SPEAR, true).length,
           "shape: a spear and a pole-arm have a butt behind the grip, shorter than the tip side");
    Expect(Near(ForKind(D2RVR_TYPE_SWORD, false).length, 0.65f, 1e-6f) && Near(ForKind(D2RVR_TYPE_SWORD, true).length, 1.10f, 1e-6f), "shape: a sword 0.65 m, two-handed 1.1 m");
    Expect(ForKind(D2RVR_TYPE_SWORD, false).butt == 0.0f && ForKind(D2RVR_TYPE_SWORD, true).butt > 0.0f, "shape: a one-handed sword has no butt, a two-handed one a little (its hilt)");
    Expect(Near(ForKind(D2RVR_TYPE_SPEAR, true).length, 2.06f, 1e-6f) && Near(ForKind(D2RVR_TYPE_SPEAR, true).butt, 0.70f, 1e-6f) &&
           Near(ForKind(D2RVR_TYPE_POLEARM, true).length, 1.15f, 1e-6f) && Near(ForKind(D2RVR_TYPE_POLEARM, true).butt, 0.90f, 1e-6f),
           "shape: spear 2.06 m ahead and 0.70 behind, polearm 1.15 and 0.90 (the models' own at the hero's 3.65 units per metre, the fallback)");
    Expect(Near(ForKind(D2RVR_TYPE_UNARMED, false).length, 0.04f, 1e-6f) && Near(ForKind(D2RVR_TYPE_UNARMED, false).radius, 0.08f, 1e-6f), "shape: a fist 4 cm long, 8 cm thick");
    Expect(Near(ForKind(D2RVR_TYPE_WAND, false).length, 0.38f, 1e-6f) && Near(ForKind(D2RVR_TYPE_ORB, false).length, 0.28f, 1e-6f), "shape: wand 0.38 m, orb 0.28 m");
    {   // the claws and katars: along the bone's Z, from the knuckles (0.18 m ahead of the origin, the cap reaching the grip's front end) to the point
        const Shape claw = ForKind(D2RVR_TYPE_CLAW, false);
        Expect(claw.axis == 2 && claw.tipSign == 1 && Near(claw.length, 0.30f, 1e-6f) && Near(claw.butt, -0.18f, 1e-6f) && Near(claw.radius, 0.045f, 1e-6f),
               "shape: the claw lies along the bone's Z (the Y of the first try ran across the blade and over the fist), from the knuckles to the point");
        int zKinds = 0;
        for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k) zKinds += ForKind(k, false).axis == 2;
        Expect(zKinds == 1, "shape: the claws are the only kind along the Z", zKinds);
    }
    int minus = 0;
    for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k) minus += ForKind(k, false).tipSign < 0;
    Expect(minus == 2, "shape: only the one-hand thrust kinds (class 1ht) lie along -Y by default - the dagger and the javelin, both measured and both the model's own", minus);
    Expect(ForKind(D2RVR_TYPE_DAGGER, false).tipSign < 0 && ForKind(D2RVR_TYPE_JAVELIN, false).tipSign < 0 && ForKind(D2RVR_TYPE_SPEAR, false).tipSign > 0,
           "shape: which two - and the spear (class 2ht) is NOT one of them: its point is along +Y by its model, the first guess of -Y put the capsule on the butt side");
    for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k) {
        const Shape a = ForKind(k, false), b = ForKind(k, true);
        Expect(a.length > 0.0f && a.butt > -a.length && a.radius >= 0.01f && a.radius <= 0.2f && b.length >= a.length - 1e-6f, "shape: every kind has a usable blade for the engine (radius <= 0.2; a fist weapon's butt is ahead of the origin, never past the point)", k);
    }
}

static void ItemTests() {
    const Shape staff = ForKind(D2RVR_TYPE_STAFF, true);
    // the codes as the game gives them: four characters, space padded
    Expect(Code('s', 's', 't', ' ') == (static_cast<uint32_t>('s') | static_cast<uint32_t>('s') << 8 | static_cast<uint32_t>('t') << 16 | static_cast<uint32_t>(' ') << 24), "item: the code packing is a | b << 8 | c << 16 | d << 24");
    // the short staff family is the measured one: the kind's own numbers
    for (const uint32_t c : {Code('s', 's', 't', ' '), Code('8', 's', 's', ' '), Code('6', 's', 's', ' '), Code('m', 's', 'f', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_STAFF, true);
        Expect(Near(s.length, staff.length, 1e-6f) && Near(s.butt, staff.butt, 1e-6f) && Near(s.radius, staff.radius, 1e-6f), "item: the short staff and its versions - the measured numbers", c);
    }
    // the 4-cell staves are longer by 4/3 (an estimate), the war staff family thicker
    for (const uint32_t c : {Code('l', 's', 't', ' '), Code('c', 's', 't', ' '), Code('b', 's', 't', ' '), Code('h', 's', 't', ' '), Code('8', 'l', 's', ' '), Code('8', 'c', 's', ' '),
                             Code('8', 'b', 's', ' '), Code('6', 'l', 's', ' '), Code('6', 'c', 's', ' '), Code('6', 'b', 's', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_STAFF, true);
        Expect(Near(s.length, staff.length * 4.0f / 3.0f, 1e-5f) && Near(s.butt, staff.butt * 4.0f / 3.0f, 1e-5f) && Near(s.radius, staff.radius, 1e-6f), "item: the long, gnarled and battle staves and their versions - 4/3 of the short one", c);
    }
    for (const uint32_t c : {Code('w', 's', 't', ' '), Code('8', 'w', 's', ' '), Code('6', 'w', 's', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_STAFF, true);
        Expect(Near(s.length, staff.length * 4.0f / 3.0f, 1e-5f) && Near(s.radius, 0.020f, 1e-6f), "item: the war staff family - long and thicker", c);
    }
    // an unknown code, or no code, is the kind's own; a code of another kind of weapon changes nothing
    Expect(Near(ForItem(Code('z', 'z', 'z', ' '), D2RVR_TYPE_STAFF, true).length, staff.length, 1e-6f) && Near(ForItem(0, D2RVR_TYPE_STAFF, true).length, staff.length, 1e-6f), "item: an unknown or missing code is the kind's own");
    Expect(Near(ForItem(Code('l', 's', 't', ' '), D2RVR_TYPE_SWORD, false).length, 0.65f, 1e-6f), "item: a staff's code does not lengthen a sword (the row is for staves)");
    Expect(Near(ForItem(Code('d', 'g', 'r', ' '), D2RVR_TYPE_DAGGER, false).length, 0.5f, 1e-6f) && ForItem(Code('d', 'g', 'r', ' '), D2RVR_TYPE_DAGGER, false).tipSign == -1, "item: the dagger keeps its own");
    // the sliders sit on top of the item's numbers
    const Shape lst = ForItem(Code('l', 's', 't', ' '), D2RVR_TYPE_STAFF, true);
    const Shape tuned = Tuned(lst, Tuning{50.0f, 0.0f, false, 50.0f});
    Expect(Near(tuned.length, lst.length * 0.5f, 1e-5f) && Near(tuned.butt, lst.butt * 0.5f, 1e-5f), "item: the sliders scale the item's numbers");
}

// Thrown weapons (hit by touch with [gameplay] melee_touch_thrown): which kinds and items the touch swings, and the blades they get.
static void ThrownTests() {
    // the kinds: swung whatever the switch says / only with the switch / never
    Expect(TouchKind(D2RVR_TYPE_SWORD, 0, false) && TouchKind(D2RVR_TYPE_STAFF, Code('s', 's', 't', ' '), false) && TouchKind(D2RVR_TYPE_UNARMED, 0, false), "touch kind: swung kinds, switch off");
    Expect(!TouchKind(D2RVR_TYPE_JAVELIN, Code('j', 'a', 'v', ' '), false) && !TouchKind(D2RVR_TYPE_THROWING, Code('t', 'k', 'f', ' '), false), "touch kind: thrown weapons are not swung with the switch off");
    Expect(TouchKind(D2RVR_TYPE_JAVELIN, Code('j', 'a', 'v', ' '), true) && TouchKind(D2RVR_TYPE_JAVELIN, 0, true), "touch kind: a javelin is swung with the switch on, code known or not");
    Expect(TouchKind(D2RVR_TYPE_THROWING, Code('t', 'k', 'f', ' '), true) && TouchKind(D2RVR_TYPE_THROWING, Code('t', 'a', 'x', ' '), true), "touch kind: a throwing knife and a throwing axe are swung with the switch on");
    Expect(!TouchKind(D2RVR_TYPE_BOW, 0, true) && !TouchKind(D2RVR_TYPE_CROSSBOW, 0, true) && !TouchKind(D2RVR_TYPE_UNKNOWN, 0, true), "touch kind: a bow, a crossbow and an unknown kind are never swung");
    // the six throwing potions are filed under the thrown kind: never swung, the switch on or off
    int potions = 0;
    for (const uint32_t c : {Code('g', 'p', 's', ' '), Code('o', 'p', 's', ' '), Code('g', 'p', 'm', ' '), Code('o', 'p', 'm', ' '), Code('g', 'p', 'l', ' '), Code('o', 'p', 'l', ' ')}) {
        Expect(IsThrowPotion(c), "potion: one of the six is told", c);
        Expect(!TouchKind(D2RVR_TYPE_THROWING, c, true) && !TouchKind(D2RVR_TYPE_THROWING, c, false), "potion: never swung", c);
        ++potions;
    }
    Expect(potions == 6, "potion: six codes");
    Expect(!IsThrowPotion(Code('t', 'k', 'f', ' ')) && !IsThrowPotion(Code('j', 'a', 'v', ' ')) && !IsThrowPotion(0), "potion: a knife, a javelin and no code are not potions");
    Expect(!TouchKind(D2RVR_TYPE_THROWING, 0, true), "potion: a thrown-kind item whose code is not read yet is not swung (it might be a potion)");
    // the kinds' own shapes: a javelin like a short spear with a butt, a throwing weapon short
    const Shape jav = ForKind(D2RVR_TYPE_JAVELIN, false);
    Expect(Near(jav.length, 1.04f, 1e-6f) && Near(jav.butt, 0.70f, 1e-6f) && Near(jav.radius, 0.025f, 1e-6f) && jav.tipSign == -1,
           "thrown shape: a javelin - the head 1.04 m from the bone along -Y (measured on two screenshots: 1.06 and 1.03), the butt 0.70 m along +Y (its model), 2.5 cm");
    Expect(jav.tipSign == -1 && jav.length > jav.butt * 1.4f, "thrown shape: the HEAD is on the -Y side and the long one - the first two tries put the long side on +Y, over the shaft to the butt");
    Expect(jav.radius >= 0.020f && jav.radius <= 0.035f, "thrown shape: thick enough for the head (3 cm half-width), no fatter than a spear's head");
    // the weapons' own side (vr/weapon_extents.h, the animation class Weapons.txt wclass): 1ht (the dagger, the javelin, the throwing knives) -Y; 1hs, stf and 2ht (the spears) +Y
    Expect(ForKind(D2RVR_TYPE_DAGGER, false).tipSign == -1 && jav.tipSign == -1, "side: the one-hand thrust class (1ht) has its point along -Y");
    Expect(ForKind(D2RVR_TYPE_SWORD, false).tipSign == 1 && ForKind(D2RVR_TYPE_AXE, false).tipSign == 1 && ForKind(D2RVR_TYPE_STAFF, true).tipSign == 1 &&
           ForKind(D2RVR_TYPE_POLEARM, true).tipSign == 1 && ForKind(D2RVR_TYPE_MACE, false).tipSign == 1 && ForKind(D2RVR_TYPE_SPEAR, false).tipSign == 1,
           "side: the swing, staff and two-hand thrust classes (1hs, stf, 2ht) have it along +Y");
    const Shape thr = ForKind(D2RVR_TYPE_THROWING, false);
    Expect(Near(thr.length, 0.40f, 1e-6f) && thr.butt == 0.0f && thr.tipSign == 1, "thrown shape: a throwing weapon of an unknown item - short, no butt");
    // by item: the knives along -Y like the dagger, the axes along +Y, the long javelins 4/3 of the javelin
    for (const uint32_t c : {Code('t', 'k', 'f', ' '), Code('b', 'k', 'f', ' '), Code('9', 't', 'k', ' '), Code('9', 'b', 'k', ' '), Code('7', 't', 'k', ' '), Code('7', 'b', 'k', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_THROWING, false);
        Expect(s.tipSign == -1 && Near(s.length, 0.40f, 1e-6f) && s.butt == 0.0f && Near(s.radius, 0.020f, 1e-6f), "thrown item: throwing knives, darts and flying knives - short, along the bone's -Y like the dagger", c);
    }
    for (const uint32_t c : {Code('t', 'a', 'x', ' '), Code('9', 't', 'a', ' '), Code('7', 't', 'a', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_THROWING, false);
        Expect(s.tipSign == 1 && Near(s.length, 0.45f, 1e-6f) && Near(s.radius, 0.040f, 1e-6f), "thrown item: the small throwing axes", c);
    }
    for (const uint32_t c : {Code('b', 'a', 'l', ' '), Code('9', 'b', '8', ' '), Code('7', 'b', '8', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_THROWING, false);
        Expect(s.tipSign == 1 && Near(s.length, 0.60f, 1e-6f) && Near(s.radius, 0.050f, 1e-6f), "thrown item: the big throwing axes - as an axe", c);
    }
    for (const uint32_t c : {Code('g', 'l', 'v', ' '), Code('t', 's', 'p', ' '), Code('9', 'g', 'l', ' '), Code('9', 't', 's', ' '), Code('7', 'g', 'l', ' '), Code('7', 't', 's', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_JAVELIN, false);
        Expect(Near(s.length, jav.length * 4.0f / 3.0f, 0.005f) && Near(s.butt, jav.butt * 4.0f / 3.0f, 0.005f) && s.tipSign == -1, "thrown item: the 4-cell javelins - the javelin's x 4/3, the head along -Y too", c);
    }
    for (const uint32_t c : {Code('j', 'a', 'v', ' '), Code('p', 'i', 'l', ' '), Code('s', 's', 'p', ' '), Code('a', 'm', '5', ' '), Code('a', 'm', 'a', ' '), Code('a', 'm', 'f', ' ')}) {
        const Shape s = ForItem(c, D2RVR_TYPE_JAVELIN, false);
        Expect(Near(s.length, jav.length, 1e-6f) && Near(s.butt, jav.butt, 1e-6f), "thrown item: the 3-cell javelins are the kind's own", c);
    }
    // the table is for the thrown kinds: a knife's code does not shorten a sword, a staff's row still works
    Expect(Near(ForItem(Code('t', 'k', 'f', ' '), D2RVR_TYPE_SWORD, false).length, 0.65f, 1e-6f), "thrown item: a knife's code does not touch a sword");
    Expect(Near(ForItem(Code('l', 's', 't', ' '), D2RVR_TYPE_STAFF, true).length, ForKind(D2RVR_TYPE_STAFF, true).length * 4.0f / 3.0f, 1e-5f), "thrown item: the staves' table is as it was");
    // every row of the table is a thrown weapon of its own (no duplicates), and sane
    for (size_t i = 0; i < sizeof kThrown / sizeof kThrown[0]; ++i) {
        const ThrownRow& r = kThrown[i];
        Expect(r.length > 0.1f && r.length < 2.0f && r.butt >= 0.0f && r.butt < 1.0f && r.radius > 0.005f && r.radius < 0.1f && (r.tipSign == 1 || r.tipSign == -1), "thrown table: sane numbers", static_cast<double>(i));
        for (size_t k = i + 1; k < sizeof kThrown / sizeof kThrown[0]; ++k) Expect(kThrown[k].code != r.code, "thrown table: no code twice", static_cast<double>(k));
        Expect(!IsThrowPotion(r.code), "thrown table: no potion in it", static_cast<double>(i));
    }
    // the sliders sit on top, the blade is built along the bone like any other (the butt behind the origin, the tip on the item's side)
    const float eye[3] = {1000.0f, 6.0f, 2000.0f};
    const HeadFrame h = MakeHead(30.0f, 4.0f, eye);
    Bone b;
    b.origin[0] = 0.2f; b.origin[1] = -0.3f; b.origin[2] = -0.4f;
    const Vec y = Normalized(Vec{0.3f, 0.8f, -0.5f});
    b.axisY[0] = y.x; b.axisY[1] = y.y; b.axisY[2] = y.z;
    const Vec origin = PointToRender(h, b.origin);
    const Blade javelin = FromBone(h, b, ForItem(Code('j', 'a', 'v', ' '), D2RVR_TYPE_JAVELIN, false));
    Expect(Near(Length(javelin.tip - origin), 1.04f, 1e-4f) && Near(Length(javelin.base - origin), 0.70f, 1e-4f) && LineDistance(origin, javelin.base, javelin.tip) < 1e-4f,
           "thrown frame: a javelin's capsule runs from 0.70 m (the butt) one side of the bone to 1.04 m (the head) the other, through the bone's origin");
    Expect(Dot(javelin.tip - origin, y) < 0.0f && Dot(javelin.base - origin, y) > 0.0f, "thrown frame: the HEAD end is on the bone's -Y side, the butt end on +Y");
    {   // THE guard: a javelin held with the bone's +Y pointing back (toward the butt, behind the hand) - the head must lie IN FRONT, and a monster in front of the hand is
        // touched by the head while one behind it is not (the first two tries had the long side on +Y: the capsule ran away from the head, over the shaft to the butt)
        Bone back;
        back.origin[0] = 0.2f; back.origin[1] = -0.3f; back.origin[2] = -0.4f;
        back.axisY[0] = 0.0f; back.axisY[1] = 0.0f; back.axisY[2] = 1.0f;   // the head's frame: z is BACK, so +Y points behind the hand
        const Vec o = PointToRender(h, back.origin);
        const Blade jb = FromBone(h, back, ForItem(Code('j', 'a', 'v', ' '), D2RVR_TYPE_JAVELIN, false));
        const Vec toHead = Normalized(jb.tip - jb.base);   // along the capsule from the butt end to the head end
        const float facing[3] = {0.0f, 0.0f, -1.0f};   // the head's frame: ahead is -z
        const Vec ahead = Normalized(DirToRender(h, facing));   // the way the user faces, on the ground
        Expect(Dot(toHead, ahead) > 0.99f, "javelin guard: with the bone's +Y pointing back, the head end of the capsule is IN FRONT of the hand", Dot(toHead, ahead));
        Expect(Dot(jb.tip - o, ahead) > 1.0f && Dot(jb.base - o, ahead) < 0.0f && Dot(jb.base - o, ahead) > -0.75f, "javelin guard: the head 1.04 m in front, the butt 0.7 m behind");
        auto post = [&](float along) {   // a thin upright post `along` metres from the hand, + IN FRONT of the user (the way he faces), - behind him - not "toward the capsule's end"
            const Vec p = o + ahead * along;
            return Body{{p.x, p.y - 1.0f, p.z}, {p.x, p.y + 1.0f, p.z}, 0.10f};
        };
        Expect(Touches(jb, jb, post(0.90f)), "javelin guard: a monster 0.9 m in front of the hand is touched by the head");
        Expect(Touches(jb, jb, post(1.05f)), "javelin guard: ... and one at the head's very point");
        Expect(!Touches(jb, jb, post(-0.90f)), "javelin guard: a monster 0.9 m BEHIND the hand is not touched by the head (it was, by the long side, while the tip lay on +Y)");
        Expect(!Touches(jb, jb, post(-1.00f)), "javelin guard: ... nor one 1 m behind (the butt reaches 0.7 m)");
        Expect(Touches(jb, jb, post(-0.25f)), "javelin guard: the butt behind the hand touches what is just behind it");
        Expect(!Touches(jb, jb, post(1.40f)), "javelin guard: a monster 1.4 m in front is out of the javelin's reach");
        // the very same with the staff (head along +Y, measured): a bone pointing back would put its head behind - the sign is per kind, not global
        const Blade sb = FromBone(h, back, ForItem(Code('s', 's', 't', ' '), D2RVR_TYPE_STAFF, true));
        Expect(Dot(Normalized(sb.tip - sb.base), ahead) < -0.99f, "javelin guard: the staff's head follows +Y (behind, for a bone pointing back) - only the 1ht kinds are flipped");
    }
    const Blade knife = FromBone(h, b, ForItem(Code('t', 'k', 'f', ' '), D2RVR_TYPE_THROWING, false));
    Expect(Dot(knife.tip - origin, y) < 0.0f && Near(Length(knife.tip - origin), 0.40f, 1e-4f), "thrown frame: a throwing knife's tip lies along the bone's -Y");
    const Shape flipped = Tuned(ForItem(Code('t', 'k', 'f', ' '), D2RVR_TYPE_THROWING, false), Tuning{100.0f, 0.0f, true, 100.0f});
    Expect(flipped.tipSign == 1, "thrown frame: the flip tick turns a knife's tip to +Y");
    // the dual-wield rule is as it was: a thrown weapon is not a second blade
    Expect(!DualWield(D2RVR_TYPE_JAVELIN, D2RVR_TYPE_JAVELIN) && !DualWield(D2RVR_TYPE_THROWING, D2RVR_TYPE_THROWING), "thrown dual: two thrown weapons are still not a dual wield");

    // the thrown weapons' own sliders ([debug] melee_thrown_length_pct / melee_thrown_butt_pct): only on top of a thrown shape
    Expect(Near(TunedThrown(jav, ThrownTuning{}).length, jav.length, 1e-6f) && Near(TunedThrown(jav, ThrownTuning{}).butt, jav.butt, 1e-6f), "thrown tuning: 100 % changes nothing");
    const Shape longer = TunedThrown(jav, ThrownTuning{130.0f, 200.0f});
    Expect(Near(longer.length, jav.length * 1.3f, 1e-5f) && Near(longer.butt, jav.butt * 2.0f, 1e-5f) && Near(longer.radius, jav.radius, 1e-6f) && longer.tipSign == jav.tipSign,
           "thrown tuning: the tip side and the butt side scale, the radius and the tip's side do not");
    const Shape noButt = TunedThrown(jav, ThrownTuning{100.0f, 0.0f});
    Expect(noButt.butt == 0.0f && Near(noButt.length, jav.length, 1e-6f), "thrown tuning: 0 % leaves no butt");
    const ThrownTuning c = Clamp(ThrownTuning{1000.0f, 1000.0f}), c0 = Clamp(ThrownTuning{1.0f, -5.0f}), cn = Clamp(ThrownTuning{std::nanf(""), std::nanf("")});
    Expect(c.lengthPct == 250.0f && c.buttPct == 300.0f && c0.lengthPct == 50.0f && c0.buttPct == 0.0f && cn.lengthPct == 100.0f && cn.buttPct == 100.0f, "thrown tuning: clamped, rubbish is 100");
    // the general sliders and the thrown ones multiply: 120 % general x 150 % thrown = 180 % of the javelin's tip side
    const Shape both = TunedThrown(Tuned(jav, Tuning{120.0f, 0.0f, false, 100.0f}), ThrownTuning{150.0f, 100.0f});
    Expect(Near(both.length, jav.length * 1.8f, 1e-4f), "thrown tuning: on top of the general one, the two multiply");
    // the largest the slider gives reaches 250 % of 1.40 m: the Glaive's own 1.87 m is inside the range
    Expect(TunedThrown(jav, ThrownTuning{250.0f, 100.0f}).length >= ForItem(Code('g', 'l', 'v', ' '), D2RVR_TYPE_JAVELIN, false).length, "thrown tuning: the range covers the longest javelin");
}

static void TuningTests() {
    const Shape staff = ForKind(D2RVR_TYPE_STAFF, true);
    // 100 % and radius 0 change nothing
    Shape t = Tuned(staff, Tuning{});
    Expect(Near(t.length, staff.length, 1e-6f) && Near(t.butt, staff.butt, 1e-6f) && Near(t.radius, staff.radius, 1e-6f) && t.tipSign == staff.tipSign, "tuning: the default changes nothing");
    // the length slider scales the tip side only
    t = Tuned(staff, Tuning{50.0f, 0.0f, false});
    Expect(Near(t.length, staff.length * 0.5f, 1e-6f) && Near(t.butt, staff.butt, 1e-6f), "tuning: 50 % is half the tip side, the butt as it was", t.length);
    t = Tuned(staff, Tuning{150.0f, 0.0f, false});
    Expect(Near(t.length, staff.length * 1.5f, 1e-5f), "tuning: 150 % is one and a half", t.length);
    t = Tuned(staff, Tuning{80.0f, 0.0f, false});
    Expect(Near(t.length, staff.length * 0.8f, 1e-5f) && Near(t.radius, staff.radius, 1e-6f), "tuning: the length slider leaves the radius alone");
    // the butt slider scales the butt side only
    t = Tuned(staff, Tuning{100.0f, 0.0f, false, 0.0f});
    Expect(t.butt == 0.0f && Near(t.length, staff.length, 1e-6f), "tuning: butt 0 % - no butt, the tip side as it was");
    t = Tuned(staff, Tuning{100.0f, 0.0f, false, 150.0f});
    Expect(Near(t.butt, staff.butt * 1.5f, 1e-5f) && Near(t.length, staff.length, 1e-6f), "tuning: butt 150 %");
    Expect(Tuned(staff, Tuning{100.0f, 0.0f, false, 900.0f}).butt <= staff.butt * 1.5f + 1e-5f && Tuned(staff, Tuning{100.0f, 0.0f, false, -5.0f}).butt == 0.0f, "tuning: the butt is held to 0..150 %");
    Expect(Tuned(ForKind(D2RVR_TYPE_SWORD, false), Tuning{100.0f, 0.0f, false, 150.0f}).butt == 0.0f, "tuning: no butt to scale on a one-handed sword");
    // out of range and not finite
    Expect(Near(Tuned(staff, Tuning{10.0f, 0.0f, false}).length, staff.length * 0.5f, 1e-6f), "tuning: below 50 % is 50 %");
    Expect(Near(Tuned(staff, Tuning{900.0f, 0.0f, false}).length, staff.length * 1.5f, 1e-5f), "tuning: above 150 % is 150 %");
    const Shape nan = Tuned(staff, Tuning{NAN, NAN, false, NAN});
    Expect(Near(nan.length, staff.length, 1e-6f) && Near(nan.radius, staff.radius, 1e-6f) && Near(nan.butt, staff.butt, 1e-6f), "tuning: a value that is not a number is the default");
    // the radius slider
    t = Tuned(staff, Tuning{100.0f, 8.0f, false});
    Expect(Near(t.radius, 0.08f, 1e-6f) && Near(t.length, staff.length, 1e-6f), "tuning: 8 cm is a radius of 0.08 m, the length alone", t.radius);
    Expect(Near(Tuned(staff, Tuning{100.0f, 1.0f, false}).radius, 0.01f, 1e-6f), "tuning: 1 cm is 1 cm (a thin staff needs it)");
    Expect(Near(Tuned(staff, Tuning{100.0f, 0.2f, false}).radius, 0.005f, 1e-6f), "tuning: under 0.5 cm is 0.5 cm");
    Expect(Near(Tuned(staff, Tuning{100.0f, 40.0f, false}).radius, 0.15f, 1e-6f), "tuning: over 15 cm is 15 cm (the engine takes no more than 20)");
    Expect(Near(Tuned(staff, Tuning{100.0f, -3.0f, false}).radius, staff.radius, 1e-6f), "tuning: a negative radius is the weapon's own");
    // the flip
    Expect(Tuned(staff, Tuning{100.0f, 0.0f, true}).tipSign == -1, "tuning: flip turns +Y to -Y");
    Expect(Tuned(ForKind(D2RVR_TYPE_DAGGER, false), Tuning{100.0f, 0.0f, true}).tipSign == 1, "tuning: flip turns the dagger's -Y to +Y");
    const Tuning c = Clamp(Tuning{1000.0f, 1000.0f, true, 1000.0f});
    Expect(c.lengthPct == 150.0f && c.radiusCm == 15.0f && c.flip && c.buttPct == 150.0f, "tuning: Clamp");
    // every kind scales with the slider
    for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k) {
        const Shape a = ForKind(k, false), b = Tuned(a, Tuning{125.0f, 0.0f, false});
        Expect(Near(b.length, a.length * 1.25f, 1e-5f), "tuning: 125 % of every kind", k);
    }
}

static void DualTests() {
    Expect(DualWield(D2RVR_TYPE_DAGGER, D2RVR_TYPE_AXE), "dual: a dagger in one hand, a hand axe in the other (the Barbarian)");
    Expect(DualWield(D2RVR_TYPE_CLAW, D2RVR_TYPE_CLAW), "dual: two claws (the Assassin)");
    Expect(DualWield(D2RVR_TYPE_SWORD, D2RVR_TYPE_SWORD), "dual: two swords");
    Expect(!DualWield(D2RVR_TYPE_SWORD, 0), "dual: a sword and a shield or an empty hand (kind 0) is one blade");
    Expect(!DualWield(0, D2RVR_TYPE_SWORD), "dual: nothing in the right hand slot is not dual either");
    Expect(!DualWield(0, 0), "dual: empty hands");
    Expect(!DualWield(D2RVR_TYPE_SWORD, D2RVR_TYPE_BOW) && !DualWield(D2RVR_TYPE_BOW, D2RVR_TYPE_SWORD), "dual: a bow in a hand is not a blade");
    Expect(!DualWield(D2RVR_TYPE_SWORD, D2RVR_TYPE_THROWING) && !DualWield(D2RVR_TYPE_JAVELIN, D2RVR_TYPE_DAGGER), "dual: thrown weapons are not swung");
    Expect(!DualWield(D2RVR_TYPE_CROSSBOW, D2RVR_TYPE_CROSSBOW), "dual: crossbows are not swung");
    Expect(!DualWield(D2RVR_TYPE_UNKNOWN, D2RVR_TYPE_AXE), "dual: an unknown kind is not a blade");
}

// A capsule on a bone: through the bone's origin, along (+-) its Y axis, from `butt` behind it to `length` before it, the radius of the shape - in the render world.
static void FrameTests() {
    const float eye[3] = {1000.0f, 6.0f, 2000.0f};
    const float yaws[] = {0.0f, 30.0f, 90.0f, 217.0f, -45.0f};
    for (const float upm : {3.65f, 4.0f}) {
        for (const float yaw : yaws) {
            const HeadFrame h = MakeHead(yaw, upm, eye);
            // a bone as the skeleton hook keeps it: its origin 0.2 right, 0.3 down, 0.4 ahead of the eyes (z is BACK: -0.4), its Y axis turned arbitrarily
            Bone b;
            b.origin[0] = 0.2f; b.origin[1] = -0.3f; b.origin[2] = -0.4f;
            const Vec y = Normalized(Vec{0.3f, 0.8f, -0.5f});
            b.axisY[0] = y.x; b.axisY[1] = y.y; b.axisY[2] = y.z;
            const Vec origin = PointToRender(h, b.origin);
            for (uint32_t kind = D2RVR_TYPE_UNARMED; kind < D2RVR_TYPE_COUNT; ++kind) {
                const Shape shape = ForKind(kind, kind == D2RVR_TYPE_STAFF || kind == D2RVR_TYPE_SPEAR);
                const Blade blade = FromBone(h, b, shape);
                // from the butt to the tip, as long as the two sides say, as thick as the shape
                Expect(Near(Length(blade.tip - blade.base), shape.length + shape.butt, 3e-4f) && Near(blade.radius, shape.radius, 1e-6f), "frame: the blade is butt + length long and as thick as the shape", kind);
                // the bone's origin lies on the capsule's axis, `butt` from the butt end and `length` from the tip
                Expect(Near(Length(blade.base - origin), std::fabs(shape.butt), 3e-4f) && Near(Length(blade.tip - origin), shape.length, 3e-4f), "frame: the bone's origin is `butt` from the butt end and `length` from the tip", kind);
                // the tip lies along the bone's Y, on the side the shape says, in the world frame of the red line
                const Vec dirWorld = Normalized(blade.tip - blade.base);
                const float vy[3] = {b.axisY[0], b.axisY[1], b.axisY[2]};
                const Vec expect = Normalized(DirToRender(h, vy)) * static_cast<float>(shape.tipSign);
                Expect(Length(dirWorld - expect) < 5e-3f, "frame: the tip lies along the bone's Y on the shape's side", kind);
                // ZERO SIDEWAYS OFFSET: the capsule's centre line passes through the bone's origin and through every point of the bone's Y line
                // (the red line the user has seen lie on the drawn weapon), whichever way the view is turned
                Expect(LineDistance(origin, blade.base, blade.tip) < 3e-4f, "frame: the capsule's axis passes through the bone's origin", kind);
                if (shape.length + shape.butt >= 0.25f)   // (a 4 cm fist cannot tell where a line runs 1 m on: its own direction has the float noise of a 274 m position)
                for (const float t : {-0.3f, 0.17f, 0.6f, 1.2f}) {
                    const float p[3] = {b.origin[0] + b.axisY[0] * t, b.origin[1] + b.axisY[1] * t, b.origin[2] + b.axisY[2] * t};
                    const Vec onBone = PointToRender(h, p);
                    Expect(LineDistance(onBone, blade.base, blade.tip) < 2e-3f, "frame: every point of the bone's Y line is on the capsule's axis (sideways offset 0)", kind * 10 + static_cast<int>(t * 10.0f));
                }
            }
            // against the red line itself: the capsule's end points, in world units from the eyes (what the effect takes as HitboxA / B), are
            // the red line's own arithmetic on the same bone points - the butt end behind the origin, the tip before it
            const Shape staff = ForKind(D2RVR_TYPE_STAFF, true);
            const Blade bl = FromBone(h, b, staff);
            const float buttPoint[3] = {b.origin[0] - b.axisY[0] * staff.butt, b.origin[1] - b.axisY[1] * staff.butt, b.origin[2] - b.axisY[2] * staff.butt};
            const Vec baseRel = bl.base * upm - Vec{eye[0], eye[1], eye[2]};
            Expect(Length(baseRel - RedLine(h, buttPoint)) < 1e-2f, "frame: the capsule's butt end against the red line's point 0.66 m behind the bone, in world units from the eyes", Length(baseRel - RedLine(h, buttPoint)));
            const float tipPoint[3] = {b.origin[0] + b.axisY[0] * staff.length, b.origin[1] + b.axisY[1] * staff.length, b.origin[2] + b.axisY[2] * staff.length};
            const Vec tipRel = bl.tip * upm - Vec{eye[0], eye[1], eye[2]};
            Expect(Length(tipRel - RedLine(h, tipPoint)) < 1e-2f, "frame: the capsule's tip against the red line at 0.66 m", Length(tipRel - RedLine(h, tipPoint)));
            // and the bone's origin itself is where the red line is at 0
            const Vec originRel = origin * upm - Vec{eye[0], eye[1], eye[2]};
            Expect(Length(originRel - RedLine(h, b.origin)) < 1e-2f, "frame: the bone's origin against the red line's point", Length(originRel - RedLine(h, b.origin)));
        }
    }
    // the staff covers the drawn staff from end to end: a staff 1.34 m long centred on the bone is covered from -0.67 to +0.67, none of it bare
    {
        const HeadFrame h = MakeHead(35.0f, 3.65f, eye);
        Bone b;
        b.origin[0] = 0.1f; b.origin[1] = -0.2f; b.origin[2] = -0.5f;
        const Vec y = Normalized(Vec{0.5f, 0.7f, -0.2f});
        b.axisY[0] = y.x; b.axisY[1] = y.y; b.axisY[2] = y.z;
        const Blade bl = FromBone(h, b, ForItem(Code('s', 's', 't', ' '), D2RVR_TYPE_STAFF, true));
        const Vec origin = PointToRender(h, b.origin);
        const Vec axisW = Normalized(bl.tip - bl.base);
        bool covered = true;
        for (float t = -0.66f - 0.016f; t <= 0.66f + 0.016f; t += 0.02f) {   // the rounded ends reach one radius past the segment
            const Vec p = origin + axisW * t;   // a point of the staff on its axis
            covered = covered && Along(p, bl.base, bl.tip) >= -bl.radius - 1e-3f && Along(p, bl.base, bl.tip) <= Length(bl.tip - bl.base) + bl.radius + 1e-3f;
        }
        Expect(covered, "frame: every point of the 1.34 m staff, butt to head, lies within the capsule (its segment plus the rounded ends)");
        Expect(Along(origin, bl.base, bl.tip) > 0.65f && Along(origin, bl.base, bl.tip) < 0.67f, "frame: the bone sits in the middle of the capsule (0.66 m from the butt end of the segment)", Along(origin, bl.base, bl.tip));
        // the same staff with the butt slider at 0: the capsule starts at the bone, the old behaviour for the butt side
        const Blade noButt = FromBone(h, b, Tuned(ForItem(Code('s', 's', 't', ' '), D2RVR_TYPE_STAFF, true), Tuning{100.0f, 0.0f, false, 0.0f}));
        Expect(Length(noButt.base - origin) < 3e-4f, "frame: butt 0 % starts the capsule at the bone");
    }
    // two bones, two hands: they land apart, each on its own origin (the left axe is not the right dagger)
    {
        const HeadFrame h = MakeHead(20.0f, 3.65f, eye);
        Bone right, left;
        right.origin[0] = 0.25f; right.origin[1] = -0.35f; right.origin[2] = -0.35f;
        left.origin[0] = -0.25f; left.origin[1] = -0.30f; left.origin[2] = -0.40f;
        const Blade a = FromBone(h, right, ForKind(D2RVR_TYPE_DAGGER, false)), c = FromBone(h, left, ForKind(D2RVR_TYPE_AXE, false));
        Expect(Length(a.base - c.base) > 0.4f && Length(a.base - PointToRender(h, right.origin)) < 1e-4f && Length(c.base - PointToRender(h, left.origin)) < 1e-4f,
               "frame: two hands' blades each on its own bone");
        // the dagger's tip runs the other way along the same bone axis as the axe's
        Expect(Dot(Normalized(a.tip - a.base), DirToRender(h, right.axisY)) < -0.99f && Dot(Normalized(c.tip - c.base), DirToRender(h, left.axisY)) > 0.99f,
               "frame: the dagger along -Y, the axe along +Y");
    }
    // the flip and the sliders reach the blade: what is drawn is what the touch tests
    {
        const HeadFrame h = MakeHead(0.0f, 3.65f, eye);
        Bone b;
        b.origin[2] = -0.4f;
        const Vec origin = PointToRender(h, b.origin);
        const Shape base = ForKind(D2RVR_TYPE_STAFF, true);
        const Blade plain = FromBone(h, b, base), longer = FromBone(h, b, Tuned(base, Tuning{150.0f, 0.0f, false})), thick = FromBone(h, b, Tuned(base, Tuning{100.0f, 10.0f, false})),
                    flipped = FromBone(h, b, Tuned(base, Tuning{100.0f, 0.0f, true})), buttier = FromBone(h, b, Tuned(base, Tuning{100.0f, 0.0f, false, 150.0f}));
        Expect(Near(Length(longer.tip - longer.base), base.length * 1.5f + base.butt, 1e-4f) && Length(longer.base - plain.base) < 1e-5f, "frame: the length slider lengthens the tip side from the same butt end");
        Expect(Near(thick.radius, 0.10f, 1e-6f), "frame: the radius slider thickens the blade");
        Expect(Near(Length(buttier.tip - buttier.base), base.length + base.butt * 1.5f, 1e-4f) && Length(buttier.tip - plain.tip) < 1e-5f, "frame: the butt slider lengthens the butt side, the tip stays");
        // the flip turns the whole blade about the bone: the tip goes where the butt was and the other way round
        Expect(Near(Length(flipped.tip - origin), base.length, 3e-4f) && Near(Length(flipped.base - origin), base.butt, 3e-4f) && Dot(Normalized(flipped.tip - origin), Normalized(plain.tip - origin)) < -0.99f,
               "frame: the flip turns the blade about the bone's origin, butt and tip changing sides");
    }
    // the pieces of the conversion on their own
    {
        const float e[3] = {10.0f, 20.0f, 30.0f};
        HeadFrame h = MakeHead(0.0f, 4.0f, e);
        const float v[3] = {0.0f, 0.0f, 0.0f};
        const Vec p = PointToRender(h, v);
        Expect(Near(p.x, 2.5f, 1e-6f) && Near(p.y, 5.0f, 1e-6f) && Near(p.z, 7.5f, 1e-6f), "frame: the eyes themselves are the world position over upm");
        const float fwd[3] = {0.0f, 0.0f, -1.0f};   // z is BACK: -1 is straight ahead
        const Vec d = DirToRender(h, fwd);
        Expect(Near(d.x, h.fwd[0], 1e-6f) && Near(d.z, h.fwd[2], 1e-6f) && Near(d.y, 0.0f, 1e-6f), "frame: ahead (z = -1) is the view's forward on the ground");
        const float up[3] = {0.0f, 1.0f, 0.0f};
        Expect(Near(DirToRender(h, up).y, 1.0f, 1e-6f), "frame: y is up");
    }
}

// The blade in the ROOM frame (the stroke engine's Sample::room): the bone, kept in the head's turn-only frame, put back in the room with the head's
// place and yaw - the way the hand's samples are - so a world that turns under a still weapon, or a hero that walks, moves nothing in it.
static void RoomBladeTests() {
    const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (const uint32_t kind : {static_cast<uint32_t>(D2RVR_TYPE_SWORD), static_cast<uint32_t>(D2RVR_TYPE_STAFF), static_cast<uint32_t>(D2RVR_TYPE_DAGGER)}) {
        const Shape shape = ForKind(kind, true);
        RoomFrame f;
        f.head[0] = 1.5f; f.head[1] = 1.7f; f.head[2] = -0.8f;
        f.yawRad = 0.6f;
        Bone b;
        b.origin[0] = 0.25f; b.origin[1] = -0.35f; b.origin[2] = -0.40f;
        const Vec y = Normalized(Vec{0.2f, 0.9f, -0.4f});
        b.axisY[0] = y.x; b.axisY[1] = y.y; b.axisY[2] = y.z;
        const Blade room = RoomFromBone(f, b, shape);
        // by hand: the ends in the head's frame, each put in the room as a hand's position is
        const Vec dir = y * static_cast<float>(shape.tipSign);
        const float baseH[3] = {b.origin[0] - dir.x * shape.butt, b.origin[1] - dir.y * shape.butt, b.origin[2] - dir.z * shape.butt};
        const float tipH[3] = {b.origin[0] + dir.x * shape.length, b.origin[1] + dir.y * shape.length, b.origin[2] + dir.z * shape.length};
        Expect(Length(room.base - meleetouch::RoomPose(f.head, f.yawRad, baseH, rot).p) < 1e-5f && Length(room.tip - meleetouch::RoomPose(f.head, f.yawRad, tipH, rot).p) < 1e-5f,
               "room blade: the ends are the bone's ends put in the room as a hand is", kind);
        Expect(Near(Length(room.tip - room.base), shape.length + shape.butt, 1e-4f) && Near(room.radius, shape.radius, 1e-6f), "room blade: the shape's whole length and thickness", kind);
        // the origin is between the ends, butt behind it and the tip before it: the same blade as FromBone's, only in another frame
        const Vec origin = meleetouch::RoomPose(f.head, f.yawRad, b.origin, rot).p;
        Expect(Near(Length(room.base - origin), shape.butt, 1e-4f) && Near(Length(room.tip - origin), shape.length, 1e-4f), "room blade: the bone's origin lies shape.butt from the base and shape.length from the tip", kind);
    }
    // a weapon held still in the room while the head turns in place: the head's frame turns under it, the room blade does not move
    {
        const Shape shape = ForKind(D2RVR_TYPE_STAFF, true);
        const Vec head{1.0f, 1.65f, 2.0f};
        const Vec roomOrigin{1.3f, 1.2f, 1.5f};            // the bone's origin in the room, fixed
        const Vec roomAxis = Normalized(Vec{0.1f, 0.2f, -1.0f});
        Blade first;
        for (const float yaw : {0.0f, 0.7f, -1.9f, 3.0f}) {
            // the head-frame coordinates of the fixed point for this yaw (the inverse of RoomPose's turn)
            const float c = std::cos(yaw), s = std::sin(yaw);
            auto toHead = [&](Vec p) { return Vec{(p.x - head.x) * c - (p.z - head.z) * s, p.y - head.y, (p.x - head.x) * s + (p.z - head.z) * c}; };
            const Vec o = toHead(roomOrigin), a = toHead(roomOrigin + roomAxis) - o;
            Bone b;
            b.origin[0] = o.x; b.origin[1] = o.y; b.origin[2] = o.z;
            b.axisY[0] = a.x; b.axisY[1] = a.y; b.axisY[2] = a.z;
            RoomFrame f;
            f.head[0] = head.x; f.head[1] = head.y; f.head[2] = head.z;
            f.yawRad = yaw;
            const Blade room = RoomFromBone(f, b, shape);
            if (yaw == 0.0f) first = room;
            else Expect(Length(room.base - first.base) < 1e-4f && Length(room.tip - first.tip) < 1e-4f, "room blade: the head turns, the weapon stands still in the room - the room blade does not move", yaw);
        }
        Expect(Near(Length(first.tip - first.base), shape.length + shape.butt, 1e-4f), "room blade: and it has its length");
    }
}

// [flail] chain: a flail's ball is the blade (BallShape) - a ball, not the handle: the segment sits on the ball's centre, 1 cm long, as thick as the ball,
// in the world and in the room alike; a ball that swings past a monster within its reach touches it, and the speed where it touches is the ball's.
static void BallTests() {
    const float eye[3] = {1000.0f, 6.0f, 2000.0f};
    const HeadFrame h = MakeHead(30.0f, 4.0f, eye);
    const float r = 0.09f;
    const Shape ball = BallShape(r);
    Bone b;
    b.origin[0] = 0.1f; b.origin[1] = -0.9f; b.origin[2] = -0.5f;   // the ball's centre, hanging below the hand
    const Vec way = Normalized(Vec{0.1f, -1.0f, 0.2f});            // the last link, toward the ball
    b.axisY[0] = way.x; b.axisY[1] = way.y; b.axisY[2] = way.z;
    const Blade w = FromBone(h, b, ball);
    const Vec centre = PointToRender(h, b.origin);
    Expect(Length((w.base + w.tip) * 0.5f - centre) < 1e-4f && Near(Length(w.tip - w.base), 2.0f * kBallHalf, 1e-4f) && Near(w.radius, r, 1e-6f),
           "ball: a 1 cm segment on the ball's centre, as thick as the ball");
    RoomFrame f;
    f.head[0] = 0.4f; f.head[1] = 1.7f; f.head[2] = -0.2f;
    f.yawRad = -0.8f;
    const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const Blade room = RoomFromBone(f, b, ball);
    Expect(Length((room.base + room.tip) * 0.5f - meleetouch::RoomPose(f.head, f.yawRad, b.origin, rot).p) < 1e-4f && Near(room.radius, r, 1e-6f), "ball: in the room the same ball");
    // swept across 0.45 m in front of a monster's axis: a monster of radius 0.37 is touched (0.37 + the ball's 0.09 > 0.45), one 1.45 m off is not
    const Vec from{-0.8f, 0.9f, -0.55f}, to{0.8f, 0.9f, -0.55f};
    const Blade before{from - way * kBallHalf, from + way * kBallHalf, r}, after{to - way * kBallHalf, to + way * kBallHalf, r};
    float along = -1.0f;
    const float t = TouchTime(before, after, Body{{0.0f, 0.0f, -1.0f}, {0.0f, 1.8f, -1.0f}, 0.37f}, &along);
    Expect(t >= 0.0f && t <= 1.0f, "ball: swung past a monster within its reach, it touches", t);
    const float thin = TouchTime(before, after, Body{{0.0f, 0.0f, -1.0f}, {0.0f, 1.8f, -1.0f}, 0.30f});
    Expect(thin < 0.0f, "ball: a thinner monster (0.30 + 0.09 < 0.45) is missed - the ball's radius counts, nothing more", thin);
    const float far = TouchTime(before, after, Body{{0.0f, 0.0f, -2.0f}, {0.0f, 1.8f, -2.0f}, 0.3f});
    Expect(far < 0.0f, "ball: one 1.45 m off is not touched", far);
    Sweep sw;
    sw.before = before; sw.after = after; sw.haveVel = true;
    sw.baseVel = (to - from) * 90.0f; sw.tipVel = (to - from) * 90.0f;   // a 90 fps step
    for (const float a : {0.0f, 0.5f, 1.0f}) Expect(Near(ContactSpeed(sw, a), Length(to - from) * 90.0f, 1e-3f), "ball: the speed where it touches is the ball's, wherever on it", a);
}

// The weapon models' own reach (vr/weapon_extents.h): the capsule's ends from the mesh bounds the game's files give, at the metres of a model unit the pose
// draws it in (UnitM) - the spear's capsule covers the spear to its point, on the side of its head (the first spear screenshot, 2026-10-21: the capsule
// ran from the hand toward the user, 0.5 m of it ahead, the spearhead 2.1-2.4 m ahead).
static void ModelTests() {
    const uint32_t spr = Code('s', 'p', 'r', ' '), pik = Code('p', 'i', 'k', ' '), tri = Code('t', 'r', 'i', ' '), hal = Code('h', 'a', 'l', ' '), vou = Code('v', 'o', 'u', ' '),
                   sst = Code('s', 's', 't', ' '), jav = Code('j', 'a', 'v', ' '), dgr = Code('d', 'g', 'r', ' '), lsd = Code('l', 's', 'd', ' '), tkf = Code('t', 'k', 'f', ' '),
                   clw = Code('c', 'l', 'w', ' '), fla = Code('f', 'l', 'a', ' ');
    const float u = UnitM(1.0f, 3.65f);   // the hero's 3.65 units per metre, the bone unscaled
    Expect(Near(u, 1.0f / 3.65f, 1e-6f), "unit: an unscaled bone over the hero's 3.65 units per metre is 0.274 m per model unit", u);
    // the table: what the game's meshes say (the Y of each mesh's bounding box, the model's own units)
    weaponextent::Reach r;
    Expect(weaponextent::ReachOf(spr, &r) && Near(r.tip, 7.585f, 1e-3f) && Near(r.butt, 2.603f, 1e-3f) && r.tipSign == 1, "model: the spear runs 7.585 units ahead of the bone along +Y and 2.603 behind it");
    Expect(weaponextent::ReachOf(jav, &r) && Near(r.tip, 3.859f, 1e-3f) && Near(r.butt, 2.595f, 1e-3f) && r.tipSign == -1, "model: the javelin's point is 3.859 units along -Y, its butt 2.595 along +Y");
    Expect(weaponextent::ReachOf(dgr, &r) && Near(r.tip, 1.515f, 1e-3f) && Near(r.butt, 0.179f, 1e-3f) && r.tipSign == -1, "model: the dagger's blade is 1.515 units along -Y");
    Expect(weaponextent::ReachOf(sst, &r) && Near(r.tip, 2.734f, 1e-3f) && Near(r.butt, 3.511f, 1e-3f) && r.tipSign == 1, "model: the short staff 2.734 units ahead, 3.511 behind");
    Expect(weaponextent::ReachOf(hal, &r) && Near(r.tip, 4.943f, 1e-3f) && Near(r.butt, 3.82f, 1e-3f) && r.tipSign == 1, "model: the halberd 4.943 ahead, 3.82 behind");
    Expect(!weaponextent::ReachOf(0, &r) && !weaponextent::ReachOf(Code('z', 'z', 'z', ' '), &r) && !weaponextent::ReachOf(Code('a', 'x', 'f', ' '), &r) && !weaponextent::ReachOf(fla, &r),
           "model: no row for no code, an unknown code, the Hatchet Hands (blades spread sideways) and the flail (its own chain logic)");
    Expect(weaponextent::ReachOf(Code('k', 't', 'r', ' '), &r) && r.axis == 2 && r.tipSign == 1 && Near(r.tip, 1.273f, 1e-3f) && Near(r.butt, -0.5f, 1e-3f),
           "model: the katar's blade runs along Z from 0.5 units (the front of the grip, the knuckles) to the point at 1.273");
    Expect(weaponextent::ReachOf(clw, &r) && r.axis == 2 && Near(r.tip, 1.428f, 1e-3f) && weaponextent::ReachOf(Code('s', 'k', 'r', ' '), &r) && r.axis == 2 && Near(r.tip, 2.019f, 1e-3f),
           "model: the claws' point is 1.428 along Z, the scissors katar's 2.019");
    // the table itself: sane rows, no code twice, every row the way the code is written, the sign by class
    size_t n = 0;
    for (const weaponextent::Row& row : weaponextent::kRows) {
        ++n;
        const double id = static_cast<double>(row.code);
        Expect(row.lo < row.hi && row.hi - row.lo > 0.5f && row.hi - row.lo < 12.0f && (row.tipSign == 1 || row.tipSign == -1), "model table: a sane row", id);
        for (int k = 0; k < 4; ++k) {
            const unsigned ch = (row.code >> (8 * k)) & 0xFFu;
            Expect(ch >= 0x20 && ch < 0x7F, "model table: the code is four printable characters", id);
        }
        Expect(row.lo <= 0.5f && row.hi > -0.5f && (row.axis == 1 || row.axis == 2), "model table: the bone's origin is in or at the weapon (the grip), not far off it", id);
        weaponextent::Reach rr;
        Expect(weaponextent::ReachOf(row.code, &rr) && rr.tip > 0.4f && (rr.butt >= 0.0f || row.axis == 2) && rr.axis == row.axis && rr.tip + rr.butt >= row.hi - row.lo - 1e-3f,
               "model table: reach covers the row", id);
        if (row.tipSign < 0) Expect(-row.lo > row.hi, "model table: the point side of a 1ht weapon is the long one (a blade, a head)", id);
    }
    Expect(n > 200 && n < 300, "model table: a row per base weapon of the game's (about 230)", static_cast<double>(n));
    for (size_t i = 0; i < n; ++i)
        for (size_t k = i + 1; k < n; ++k) Expect(weaponextent::kRows[i].code != weaponextent::kRows[k].code, "model table: no code twice", static_cast<double>(k));
    // the constants' own sides and rows agree with the model's
    Expect(weaponextent::Find(dgr)->tipSign == ForKind(D2RVR_TYPE_DAGGER, false).tipSign && weaponextent::Find(jav)->tipSign == ForKind(D2RVR_TYPE_JAVELIN, false).tipSign &&
           weaponextent::Find(spr)->tipSign == ForKind(D2RVR_TYPE_SPEAR, false).tipSign && weaponextent::Find(sst)->tipSign == ForKind(D2RVR_TYPE_STAFF, true).tipSign &&
           weaponextent::Find(hal)->tipSign == ForKind(D2RVR_TYPE_POLEARM, true).tipSign && weaponextent::Find(lsd)->tipSign == ForKind(D2RVR_TYPE_SWORD, false).tipSign,
           "model: the kinds' constants point to the same side as the models do");
    for (const ThrownRow& tr : kThrown) Expect(weaponextent::Find(tr.code) && weaponextent::Find(tr.code)->tipSign == tr.tipSign, "model: a thrown row's side is the model's", static_cast<double>(tr.code));
    for (const ItemRow& ir : kItems) Expect(weaponextent::Find(ir.code) && weaponextent::Find(ir.code)->tipSign == 1, "model: a staff row has a model, its point on +Y", static_cast<double>(ir.code));

    // the unit: the bone's scale over the hero's units per metre; rubbish is no unit
    Expect(Near(UnitM(1.15f, 3.65f), 1.15f / 3.65f, 1e-6f) && Near(UnitM(1.0f, 4.0f), 0.25f, 1e-6f), "unit: a scaled bone and a bigger hero change it");
    Expect(UnitM(0.0f, 3.65f) == 0.0f && UnitM(-1.0f, 3.65f) == 0.0f && UnitM(1.0f, 0.0f) == 0.0f && UnitM(std::nanf(""), 3.65f) == 0.0f && UnitM(1.0f, std::nanf("")) == 0.0f &&
           UnitM(INFINITY, 3.65f) == 0.0f && UnitM(1.0f, 100.0f) == 0.0f && UnitM(50.0f, 1.0f) == 0.0f, "unit: zero, negative, not a number, infinite or out of any weapon's size is no unit");

    // the spear: its point 7.585 units ahead = 2.08 m, the capsule's end AT the wood (the rounded end reaches one radius past the segment), the butt 0.71 m
    const Shape spear = ForItem(spr, D2RVR_TYPE_SPEAR, true, u);
    Expect(spear.tipSign == 1 && Near(spear.length + spear.radius, 7.585f * u, 1e-4f) && Near(spear.butt + spear.radius, 2.603f * u, 1e-4f) && Near(spear.radius, 0.016f, 1e-6f),
           "spear: the capsule ends at the spearhead's point, 2.08 m ahead on +Y, and at the butt 0.71 m behind", spear.length);
    Expect(spear.length > 2.0f && spear.length < 2.2f && spear.butt > 0.6f && spear.butt < 0.8f, "spear: about 2.06 m ahead of the hand and 0.70 behind");
    // a pike, a trident and the polearms by their own models
    for (const uint32_t c : {pik, tri, hal, vou}) {
        weaponextent::ReachOf(c, &r);
        const Shape s = ForItem(c, c == pik || c == tri ? D2RVR_TYPE_SPEAR : D2RVR_TYPE_POLEARM, true, u);
        Expect(s.tipSign == 1 && Near(s.length + s.radius, r.tip * u, 1e-4f) && Near(s.butt + s.radius, r.butt * u, 1e-4f), "polearm: each by its own model, the point along +Y", static_cast<double>(c));
    }
    Expect(ForItem(spr, D2RVR_TYPE_SPEAR, true, u).length > ForItem(tri, D2RVR_TYPE_SPEAR, true, u).length && ForItem(hal, D2RVR_TYPE_POLEARM, true, u).length < ForItem(spr, D2RVR_TYPE_SPEAR, true, u).length,
           "polearm: the spear reaches farther than the trident and the halberd");
    // the javelin and the staff against what the headset screenshots measured: the javelin's point 1.04 m from the bone, the staff's head 0.67
    const Shape javelin = ForItem(jav, D2RVR_TYPE_JAVELIN, false, u);
    Expect(javelin.tipSign == -1 && Near(javelin.length + javelin.radius, 1.04f, 0.03f), "javelin: the model's point is within 3 cm of the 1.04 m measured on the screenshots", javelin.length + javelin.radius);
    Expect(Near(javelin.butt + javelin.radius, 2.595f * u, 1e-4f) && javelin.butt > 0.6f, "javelin: the butt 0.71 m behind the bone (it was a 0.30 m guess)");
    const Shape staff = ForItem(sst, D2RVR_TYPE_STAFF, true, u);
    Expect(staff.tipSign == 1 && Near(staff.length + staff.radius, 2.734f * u, 1e-4f) && Near(staff.butt + staff.radius, 3.511f * u, 1e-4f) && staff.radius == 0.016f, "staff: its model's ends (0.75 ahead, 0.96 behind), the shaft's radius as before");
    Expect(staff.length + staff.radius > 0.67f && staff.length + staff.radius < 0.82f, "staff: the head's end lies near the 0.67 m the screenshot measured (0.75 by the model: within 12 %)");
    const Shape dagger = ForItem(dgr, D2RVR_TYPE_DAGGER, false, u), knife = ForItem(tkf, D2RVR_TYPE_THROWING, false, u);
    Expect(dagger.tipSign == -1 && Near(dagger.length + dagger.radius, 1.515f * u, 1e-4f) && knife.tipSign == -1 && Near(knife.length + knife.radius, 1.804f * u, 1e-4f), "dagger and throwing knife: the points along -Y by the models");
    // no unit, no row: the constants as they were; a one-hand or two-hand grip changes nothing about a model
    Expect(Near(ForItem(spr, D2RVR_TYPE_SPEAR, true, 0.0f).length, 2.06f, 1e-6f) && Near(ForItem(spr, D2RVR_TYPE_SPEAR, true, std::nanf("")).length, 2.06f, 1e-6f) && Near(ForItem(spr, D2RVR_TYPE_SPEAR, true, -1.0f).butt, 0.70f, 1e-6f),
           "fallback: no unit - the kind's constants");
    Expect(Near(ForItem(Code('a', 'x', 'f', ' '), D2RVR_TYPE_CLAW, false, u).length, 0.30f, 1e-6f) && ForItem(Code('a', 'x', 'f', ' '), D2RVR_TYPE_CLAW, false, u).axis == 2 &&
           Near(ForItem(fla, D2RVR_TYPE_MACE, false, u).length, 0.65f, 1e-6f) && Near(ForItem(0, D2RVR_TYPE_SWORD, false, u).length, 0.65f, 1e-6f),
           "fallback: no row (the Hatchet Hands, the flail, no code) - the kind's constants even with a unit, the Hatchet Hands' still along Z");
    Expect(Near(ForItem(lsd, D2RVR_TYPE_SWORD, false, u).length, ForItem(lsd, D2RVR_TYPE_SWORD, true, u).length, 1e-6f) && Near(ForItem(lsd, D2RVR_TYPE_SWORD, false, u).butt, ForItem(lsd, D2RVR_TYPE_SWORD, true, u).butt, 1e-6f),
           "model: a sword is as long in one hand as in two");
    // with a unit the item's own model decides the side and the ends, not the kind the game files it under (the kind only gives the thickness)
    Expect(ForItem(dgr, D2RVR_TYPE_SWORD, false, u).tipSign == -1 && ForItem(spr, D2RVR_TYPE_DAGGER, false, u).tipSign == 1 && Near(ForItem(spr, D2RVR_TYPE_DAGGER, false, u).radius, 0.030f, 1e-6f),
           "model: the side comes from the item's model, the radius from the kind");
    // the unit scales the capsule: twice the unit, twice the weapon; a hero of a different size (units per metre) or a scaled bone is as drawn
    const Shape big = ForItem(spr, D2RVR_TYPE_SPEAR, true, 2.0f * u);
    Expect(Near(big.length + big.radius, 2.0f * (spear.length + spear.radius), 1e-4f) && Near(big.butt + big.radius, 2.0f * (spear.butt + spear.radius), 1e-4f), "model: twice the unit is twice the weapon");
    const Shape scaled = ForItem(spr, D2RVR_TYPE_SPEAR, true, UnitM(1.15f, 3.65f)), roomy = ForItem(spr, D2RVR_TYPE_SPEAR, true, UnitM(1.0f, 4.0f));
    Expect(Near(scaled.length + scaled.radius, 1.15f * (spear.length + spear.radius), 1e-3f) && Near(roomy.length + roomy.radius, 3.65f / 4.0f * (spear.length + spear.radius), 1e-3f),
           "model: a bone scaled 1.15 makes the spear 15 % longer, a hero of 4 units per metre 9 % shorter");
    // the sliders sit on top of the model's numbers, the flip too
    const Shape half = Tuned(spear, Tuning{50.0f, 0.0f, false, 50.0f});
    Expect(Near(half.length, spear.length * 0.5f, 1e-5f) && Near(half.butt, spear.butt * 0.5f, 1e-5f) && Tuned(spear, Tuning{100.0f, 0.0f, true}).tipSign == -1, "model: the sliders and the flip work on the model's blade");
    Expect(Near(TunedThrown(javelin, ThrownTuning{150.0f, 100.0f}).length, javelin.length * 1.5f, 1e-5f), "model: the thrown sliders work on the model's javelin");

    // THE GUARD, in the picture of the screenshot: a spear held with its bone's +Y ahead of the user. The capsule runs from 0.7 m behind the hand to the point 2.08 m ahead,
    // so a monster at the spearhead (2.0 m) is touched and one 2.3 m off is out of reach; the old guess (the tip along -Y, 1.5 m ahead of ... the butt) lay the other way
    {
        const float eye[3] = {1000.0f, 6.0f, 2000.0f};
        const HeadFrame h = MakeHead(30.0f, 4.0f, eye);
        Bone ahead;
        ahead.origin[0] = 0.2f; ahead.origin[1] = -0.3f; ahead.origin[2] = -0.4f;
        ahead.axisY[0] = 0.0f; ahead.axisY[1] = 0.0f; ahead.axisY[2] = -1.0f;   // the head's frame: z is BACK, so -1 is the way the user faces: the spear's head is ahead
        const Vec o = PointToRender(h, ahead.origin);
        const float facing[3] = {0.0f, 0.0f, -1.0f};
        const Vec fwd = Normalized(DirToRender(h, facing));
        const Blade bl = FromBone(h, ahead, spear);
        Expect(Dot(Normalized(bl.tip - bl.base), fwd) > 0.99f, "spear guard: the capsule's point end is AHEAD when the bone's +Y is ahead", Dot(Normalized(bl.tip - bl.base), fwd));
        Expect(Near(Dot(bl.tip - o, fwd) + bl.radius, 2.078f, 2e-3f) && Near(-Dot(bl.base - o, fwd) + bl.radius, 0.713f, 2e-3f), "spear guard: the point 2.08 m ahead of the hand, the butt 0.71 m behind it (the rounded ends included)");
        auto post = [&](float along) {   // a thin upright post `along` metres from the hand, + ahead
            const Vec p = o + fwd * along;
            return Body{{p.x, p.y - 1.0f, p.z}, {p.x, p.y + 1.0f, p.z}, 0.10f};
        };
        Expect(Touches(bl, bl, post(1.80f)) && Touches(bl, bl, post(2.00f)) && Touches(bl, bl, post(2.15f)), "spear guard: a monster at 1.8 m, at the spearhead (2.0 m) and just beyond its point is touched");
        Expect(!Touches(bl, bl, post(2.30f)) && !Touches(bl, bl, post(2.60f)), "spear guard: one 2.3 m ahead is out of the spear's reach");
        Expect(Touches(bl, bl, post(-0.50f)) && !Touches(bl, bl, post(-0.95f)), "spear guard: the butt touches what is 0.5 m behind the hand, not what is 0.95 m behind");
        Shape guess;   // what the first tests had: the tip along -Y 1.5 m, the butt 0.5 m along +Y
        guess.length = 1.50f; guess.butt = 0.50f; guess.radius = 0.016f; guess.tipSign = -1;
        const Blade old = FromBone(h, ahead, guess);
        Expect(!Touches(old, old, post(2.00f)) && !Touches(old, old, post(1.80f)) && Touches(old, old, post(-1.40f)), "spear guard: the old guess put its long side BEHIND the hand and missed the spearhead - this is what the fix undoes");
    }
}

// The Assassin's katars and claws (the screenshots of 2026-10-21): the blade lies along the bone's Z (yellow in the debug), the capsule of the first try along its Y (blue)
// ran across the blade and over the fist. Both hands; the game's own pose tells which way the Z runs against the forearm (PosedBlade::zSign).
static void ClawTests() {
    const float u = UnitM(1.0f, 3.65f);
    const uint32_t ktr = Code('k', 't', 'r', ' '), clw = Code('c', 'l', 'w', ' '), skr = Code('s', 'k', 'r', ' ');
    const Shape katar = ForItem(ktr, D2RVR_TYPE_CLAW, false, u);
    Expect(katar.axis == 2 && katar.tipSign == 1 && Near(katar.length + katar.radius, 1.273f * u, 1e-4f) && Near(-katar.butt - katar.radius, 0.5f * u, 1e-4f) && katar.butt < 0.0f,
           "claw: the katar's capsule runs from its grip's front end (0.137 m ahead of the bone) to its point (0.349 m), along Z, 4.5 cm thick");
    Expect(Near(katar.length, 0.304f, 2e-3f) && Near(katar.butt, -0.182f, 2e-3f), "claw: segment 0.30 m ahead and 0.18 m ahead (the cap's rounded end reaches the grip's front)");
    const Shape claws = ForItem(clw, D2RVR_TYPE_CLAW, false, u), scissors = ForItem(skr, D2RVR_TYPE_CLAW, false, u);
    Expect(claws.axis == 2 && Near(claws.length + claws.radius, 1.428f * u, 1e-4f) && scissors.axis == 2 && Near(scissors.length + scissors.radius, 2.019f * u, 1e-4f) && scissors.length > claws.length && claws.length > katar.length,
           "claw: the claws are longer than the katar, the scissors katar longest");
    Expect(ForItem(ktr, D2RVR_TYPE_SWORD, false, u).axis == 2 && ForItem(Code('s', 'p', 'r', ' '), D2RVR_TYPE_CLAW, false, u).axis == 1,
           "claw: with a unit the item's model names the axis, not the kind it is filed under");
    // the sign: LineOf takes the Z of a fist weapon with the sign of the game's pose, the Y of everything else as the bone has it
    const float ay[3] = {0.0f, 1.0f, 0.0f}, az[3] = {0.0f, 0.0f, 1.0f};
    float l[3];
    LineOf(katar, ay, az, 1, l);
    Expect(l[2] == 1.0f && l[1] == 0.0f, "line: a katar takes the bone's Z");
    LineOf(katar, ay, az, -1, l);
    Expect(l[2] == -1.0f && l[1] == 0.0f, "line: ... against the forearm it is the Z turned round");
    LineOf(katar, ay, az, 0, l);
    Expect(l[2] == 1.0f, "line: ... not told: as the bone has it");
    LineOf(ForKind(D2RVR_TYPE_DAGGER, false), ay, az, -1, l);
    Expect(l[1] == 1.0f && l[2] == 0.0f, "line: a dagger takes the Y, whatever sign the Z has");
    LineOf(ForKind(D2RVR_TYPE_SPEAR, true), ay, az, -1, l);
    Expect(l[1] == 1.0f && l[2] == 0.0f, "line: a spear the same");
    // the screenshots: the right katar's blade along the yellow axis (up and left from the hand), the left katar's along its own Z (up and right), the Y of each at about 90 degrees to it
    const float eye[3] = {1000.0f, 6.0f, 2000.0f};
    const HeadFrame h = MakeHead(20.0f, 3.65f, eye);
    struct Hand { const char* name; float y[3], z[3]; int zSign; };
    const Vec yR = Normalized(Vec{0.60f, 0.75f, -0.28f}), yL = Normalized(Vec{-0.20f, -0.70f, -0.68f});
    const Vec vR{-0.62f, 0.55f, -0.56f}, vL{0.55f, 0.55f, -0.62f};
    const Vec zR = Normalized(vR - yR * Dot(vR, yR)), zL = Normalized(vL - yL * Dot(vL, yL));   // the bone's Z across its Y, as an orthonormal bone has it
    const Hand hands[] = {{"right", {yR.x, yR.y, yR.z}, {zR.x, zR.y, zR.z}, 1}, {"left", {yL.x, yL.y, yL.z}, {zL.x, zL.y, zL.z}, 1}, {"left, Z against the forearm", {yL.x, yL.y, yL.z}, {-zL.x, -zL.y, -zL.z}, -1}};
    for (const Hand& hd : hands) {
        Bone b;
        b.origin[0] = 0.2f; b.origin[1] = -0.35f; b.origin[2] = -0.4f;
        float line[3];
        LineOf(katar, hd.y, hd.z, hd.zSign, line);
        for (int i = 0; i < 3; ++i) b.axisY[i] = line[i];
        const Vec o = PointToRender(h, b.origin);
        const Blade bl = FromBone(h, b, katar);
        const Vec dir = Normalized(bl.tip - bl.base);
        const float zl[3] = {hd.z[0] * hd.zSign, hd.z[1] * hd.zSign, hd.z[2] * hd.zSign}, yv[3] = {hd.y[0], hd.y[1], hd.y[2]};
        const Vec zW = Normalized(DirToRender(h, zl)), yW = Normalized(DirToRender(h, yv));
        Expect(Dot(dir, zW) > 0.999f && std::fabs(Dot(dir, yW)) < 0.2f, "claw frame: the capsule lies along the blade (the Z the game's pose points forward), across the bone's Y", Dot(dir, zW));
        Expect(Dot(bl.base - o, zW) > 0.15f && Dot(bl.tip - o, zW) > 0.28f && Dot(bl.tip - o, zW) < 0.32f, "claw frame: from the knuckles ahead of the bone to the point 0.30 m (+ the cap)", Dot(bl.tip - o, zW));
        auto at = [&](float along, float across) {   // a small monster `along` the blade and `across` it (the old, wrong capsule's way: the bone's Y)
            const Vec p = o + zW * along + yW * across;
            return Body{{p.x, p.y - 0.02f, p.z}, {p.x, p.y + 0.02f, p.z}, 0.04f};
        };
        Expect(Touches(bl, bl, at(0.30f, 0.0f)) && Touches(bl, bl, at(0.37f, 0.0f)), "claw frame: a monster at the katar's point is touched", 0);
        Expect(!Touches(bl, bl, at(0.0f, 0.25f)) && !Touches(bl, bl, at(0.0f, -0.25f)) && !Touches(bl, bl, at(0.0f, 0.12f)), "claw frame: one 12 - 25 cm off along the bone's Y - where the first try's capsule was - is not");
        Expect(!Touches(bl, bl, at(0.0f, 0.0f)) && !Touches(bl, bl, at(-0.10f, 0.0f)), "claw frame: the fist (the grip, behind the knuckles) is not a blade: nothing there is touched");
        Expect(!Touches(bl, bl, at(0.55f, 0.0f)), "claw frame: one 55 cm ahead is out of the katar's reach");
        // the first try's capsule, along the bone's Y, 0.28 m: it lay across the blade - this is what the fix undoes
        Shape first;
        first.length = 0.28f; first.butt = 0.0f; first.radius = 0.045f;
        Bone by;
        by.origin[0] = b.origin[0]; by.origin[1] = b.origin[1]; by.origin[2] = b.origin[2];
        for (int i = 0; i < 3; ++i) by.axisY[i] = hd.y[i];
        const Blade old = FromBone(h, by, first);
        Expect(Dot(Normalized(old.tip - old.base), yW) > 0.99f && Touches(old, old, at(0.0f, 0.20f)) && !Touches(old, old, at(0.30f, 0.0f)),
               "claw frame: the first try's capsule (along the bone's Y) touched what lay across the blade and missed the katar's point");
    }
}

int main() {
    BallTests();
    ShapeTests();
    ItemTests();
    ModelTests();
    ClawTests();
    ThrownTests();
    TuningTests();
    DualTests();
    RoomBladeTests();
    FrameTests();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("blade_shape_test: all passed\n");
    return 0;
}
