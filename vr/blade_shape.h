// blade_shape.h - the blade hit by touch (melee_touch.h) sweeps, built from the weapon AS DRAWN. Pure maths: no game calls, no globals.
//
// Where the weapon is. The skeleton hook (skeletons.cpp) keeps the weapon's attach bone as the finished pose draws it - its
// origin and its own axes - in the head's turn-only frame, metres from the eyes (x right, y up, z back): the very numbers the
// debug's red line and bone axes are drawn from, which the user has seen lie on the drawn weapon. FromBone() turns them into the
// render world over units-per-metre, the frame the monsters' capsules are in, with the same arithmetic vrcam's `world()` uses for
// those lines (hung off where the hands hang, g_eyeWorld, along the view's right / forward on the ground). The hero model's
// model-to-world matrix is NOT in this chain: the first version built the blade from it (skel::PosedWeaponPose::origin / axis) and
// the staff's capsule stood beside the staff, as the old touch's did before d645f42 (2026-10-14).
//
// What the blade is. The bone is where the game hangs the item - for a weapon held in two hands the middle of the grip, NOT the
// weapon's end (a staff's capsule from its bone ran off the top and left the butt bare, 2026-10-14) - and the item's long axis is the
// bone's +Y or -Y. So a blade has a TIP side (`length` along the axis) and a BUTT side (`butt` behind the origin). The thickness is per
// kind of weapon (and per item where known); the two ends are the weapon model's OWN (2026-10-21, ForItem's `unitM`): the game's weapon
// models are Granny GR2 files whose meshes carry their bounds, read offline into vr/weapon_extents.h - per base item, in the model's own
// units, with the model's origin at the bone's origin and its Y the bone's - and the skeleton hook measures the metres of a unit as drawn
// (PosedBlade::unitM). The constants below are the fallback for an item with no row (claws, flails) or a pose with no unit, and what the
// first tests of 2026-10-14 measured. THE POINT'S SIDE is the model's too: all weapons are authored with the point along +Y except the
// animation class 1ht (daggers, throwing knives, javelins), -Y - the spear's own was a wrong guess of -Y until then (the capsule of the first
// spear screenshot ran from the hand away from the head, 0.5 m ahead of it and 1.5 m behind).
// Measured on the drawn weapons (the user's screenshots, Barbarian, 2026-10-14): the dagger's blade ~0.5 m, along the bone's -Y;
// a short staff's head 0.67 m from the bone (so the segment ends 0.66: the capsule's rounded end reaches one radius past the segment, and
// should end AT the wood) and its shaft ~1.6 cm in radius (the first 2.5 cm drew twice as thick as the wood). The head's place is read off
// a headset screenshot with the perspective taken out: the capsule's apparent width tapers linearly along its axis (1/depth), which puts the
// axis's vanishing point on the picture and so the angle of the staff to the view; a naive pixel count along the axis gave 6-7 cm of
// overshoot for the 0.78 m try, the corrected one 13 cm.
// Everything else in the constants - the butt of every weapon, the other staves - is an ESTIMATE, marked so below; the sliders [debug]
// melee_blade_length_pct / melee_blade_butt_pct / melee_blade_radius_cm and the tick box melee_blade_flip tune the touch's blade
// live - what is drawn is what the touch tests - and never the game's reach or rolls.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_state.h"
#include "melee_bodies.h"
#include "room_pose.h"
#include "stroke_contact.h"
#include "weapon_extents.h"

namespace bladeshape {

// A blade along its bone's Y: how far it runs from the bone's origin to the tip, how far behind the origin to the butt, how thick it is,
// and to which side of +Y its tip lies.
struct Shape {
    float length = 0.65f;   // origin -> tip, metres
    float butt = 0.0f;      // origin -> butt (the other way), metres; NEGATIVE for a fist weapon: its blade starts ahead of the origin, at the knuckles
    float radius = 0.04f;
    int tipSign = 1;        // +1: the tip along the bone's +axis; -1: along -axis
    int axis = 1;           // the bone's axis the weapon lies along: 1 = Y (nearly everything), 2 = Z (the claws and katars)
};

// The constants by kind (the fallback where nothing better is known). twoHandsOn: held in both hands.
inline Shape ForKind(uint32_t type, bool twoHandsOn) {
    Shape s;
    switch (type) {
        case D2RVR_TYPE_UNARMED: s.length = 0.04f; s.radius = 0.08f; break;
        case D2RVR_TYPE_DAGGER: s.length = 0.50f; s.radius = 0.030f; s.tipSign = -1; break;   // measured on the drawn dagger: its blade runs along the bone's -Y
        // The claws and katars (the Assassin's): the blade lies along the bone's Z (yellow in the debug), NOT its Y - the screenshots of 2026-10-21 had the capsule
        // along Y, across the blade and over the fist, in both hands. From the knuckles (the front end of the grip, 0.14 m ahead of the bone's origin) to the point
        // (the katar's 1.273 units = 0.35 m; the claws' 1.428 = 0.39): the model's numbers at the hero's 3.65 units per metre, the fallback for a pose with no unit
        case D2RVR_TYPE_CLAW: s.length = 0.30f; s.butt = -0.18f; s.radius = 0.045f; s.axis = 2; break;
        case D2RVR_TYPE_ORB: s.length = 0.28f; s.radius = 0.045f; break;
        case D2RVR_TYPE_WAND: s.length = 0.38f; s.radius = 0.020f; break;
        // measured on the drawn short staff: the head 0.67 m from the bone, so the segment ends 0.66 m (+ the 1.6 cm of the rounded end = the
        // wood's end), the shaft 1.6 cm in radius. The butt is an ESTIMATE: the bone is the middle of the two-handed grip, between the hands,
        // so the staff runs as far behind it as before it (centred); not seen yet - the butt end was below the picture's edge
        case D2RVR_TYPE_STAFF: s.length = 0.66f; s.butt = 0.66f; s.radius = 0.016f; break;
        // The spear and the pole-arm: the point lies along the bone's +Y, as the staff's does (the model's own: the spear's crossbar and leaf at the +Y
        // end - the first guess of -Y, from the animation class 2ht, was WRONG: the first spear screenshot, 2026-10-21, had the capsule's long side
        // running from the hand toward the user and only 0.5 m of it ahead, the spearhead 2.1-2.4 m ahead of the hand). The numbers are what the
        // model says at the hero's 3.65 units per metre (the spear's mesh runs 7.585 units ahead of the bone and 2.603 behind it, the voulge's 3.64 and
        // 2.98 and the halberd's 4.94 and 3.82): the fallback for a pose with no unit; ForItem takes them from weapon_extents.h otherwise
        case D2RVR_TYPE_SPEAR: s.length = 2.06f; s.butt = 0.70f; s.radius = 0.016f; break;
        case D2RVR_TYPE_POLEARM: s.length = 1.15f; s.butt = 0.90f; s.radius = 0.020f; break;
        // THROWN WEAPONS (hit by touch with [gameplay] melee_touch_thrown). The JAVELIN (wclass 1ht) is MEASURED on two headset screenshots of a Javelin
        // in the right hand with the capsule drawn: the spearhead lies along the bone's -Y (the capsule's long side first went the wrong way, to the butt -
        // the first try's tip 0.80 m along +Y ran over the shaft to the right, away from the head, and the second try's 1.40 m made it worse), the
        // head's point 1.04 m from the bone (1.06 and 1.03 on the two pictures: the capsule's own width as the ruler, the perspective taken out as for the
        // short staff: D sqrt(f^2 + B^2) (1/w2 - 1/w1) between the capsule's base end of known place and the point; +-0.1). The bone sits at the hand (the
        // grip). The butt, along +Y, is not in a picture: the model's 2.595 units at 3.65 per metre make it 0.71 m ([debug] melee_thrown_butt_pct tunes
        // it); the point's 3.859 units make 1.057 m, within 2 % of the 1.04 measured here. The capsule is 2.5 cm in radius: the
        // shaft is 1 cm, the wrapped socket 1.5, the leaf-shaped head 3 cm half-width, and the head is what touches. Its axis is ~3-4 degrees off the
        // bone's Y in the first picture - not corrected, one view cannot say which way in the bone's frame. The throwing kind covers knives and axes
        // (ForItem tells them apart by the code, ESTIMATES): a short one with no butt for an item not known
        case D2RVR_TYPE_JAVELIN: s.length = 1.04f; s.butt = 0.70f; s.radius = 0.025f; s.tipSign = -1; break;
        case D2RVR_TYPE_THROWING: s.length = 0.40f; s.radius = 0.030f; break;
        case D2RVR_TYPE_AXE: case D2RVR_TYPE_MACE: case D2RVR_TYPE_SCEPTER:
            s.length = twoHandsOn ? 1.10f : 0.65f; s.butt = twoHandsOn ? 0.20f : 0.0f; s.radius = 0.050f; break;
        default: s.length = twoHandsOn ? 1.10f : 0.65f; s.butt = twoHandsOn ? 0.15f : 0.0f; s.radius = 0.035f; break;   // swords and the rest
    }
    return s;
}

// Four characters of an item code ('sst ' - the game's base weapon code, space padded) as one number.
constexpr uint32_t Code(char a, char b, char c, char d) { return weaponextent::C(a, b, c, d); }

// Per base item. The staves come in three size classes by the game's own inventory size (Weapons.txt invheight; the elite and exceptional
// versions share their base's): 3 cells - the short staff, Jo Staff, Walking Stick, Staff of Kings (the measured one); 4 cells - the long,
// gnarled and battle staves with their versions, the Horadric Staff; and the war staff family, 4 cells and twice as wide. Only the short
// staff's numbers are measured; the others are the short staff's scaled by their height in cells (x 4/3) - an ESTIMATE, so the sliders.
// A new row here is the way to put a measured weapon in: {code, scale of the kind's length and butt, radius or 0 for the kind's own}. With a unit (UnitM) the
// length and butt of a staff come from its model (vr/weapon_extents.h) instead - the radius of this row stays.
struct ItemRow {
    uint32_t code;
    float scale;     // x the kind's length and butt
    float radius;    // 0 = the kind's own
};
inline constexpr ItemRow kItems[] = {
    {Code('s', 's', 't', ' '), 1.0f, 0.0f}, {Code('8', 's', 's', ' '), 1.0f, 0.0f}, {Code('6', 's', 's', ' '), 1.0f, 0.0f}, {Code('m', 's', 'f', ' '), 1.0f, 0.0f},
    {Code('l', 's', 't', ' '), 4.0f / 3.0f, 0.0f}, {Code('c', 's', 't', ' '), 4.0f / 3.0f, 0.0f}, {Code('b', 's', 't', ' '), 4.0f / 3.0f, 0.0f}, {Code('h', 's', 't', ' '), 4.0f / 3.0f, 0.0f},
    {Code('8', 'l', 's', ' '), 4.0f / 3.0f, 0.0f}, {Code('8', 'c', 's', ' '), 4.0f / 3.0f, 0.0f}, {Code('8', 'b', 's', ' '), 4.0f / 3.0f, 0.0f},
    {Code('6', 'l', 's', ' '), 4.0f / 3.0f, 0.0f}, {Code('6', 'c', 's', ' '), 4.0f / 3.0f, 0.0f}, {Code('6', 'b', 's', ' '), 4.0f / 3.0f, 0.0f},
    {Code('w', 's', 't', ' '), 4.0f / 3.0f, 0.020f}, {Code('8', 'w', 's', ' '), 4.0f / 3.0f, 0.020f}, {Code('6', 'w', 's', ' '), 4.0f / 3.0f, 0.020f},
};
inline const ItemRow* FindItem(uint32_t code) {
    for (const ItemRow& r : kItems)
        if (r.code == code) return &r;
    return nullptr;
}

// The thrown weapons whose blade is not the kind's (the codes are Weapons.txt's - the base, the exceptional and the elite version of each). Their radius and
// point side are what ForItem uses; their length and butt are the constants for a pose with no unit (the weapon models' own ends, vr/weapon_extents.h, replace
// them otherwise - ESTIMATED, the sliders tune them): the throwing knives, darts and flying knives run along the bone's -Y like the measured dagger (the same
// 1 x 2 cells, shorter); the throwing axes along +Y like an axe, the 1 x 2 ones small, the 2 x 3 ones (Balanced Axe, Hurlbat, Winged Axe) as an axe; the
// javelins of 4 cells (Glaive, Throwing Spear and their versions) are the 3-cell javelin's x 4/3, as the staves of 4 cells are the short staff's. The 3-cell
// javelins (Javelin, Pilum, Short Spear, the Amazon's) are the kind's own.
struct ThrownRow {
    uint32_t code;
    float length, butt, radius;
    int tipSign;
};
inline constexpr ThrownRow kThrown[] = {
    {Code('t', 'k', 'f', ' '), 0.40f, 0.0f, 0.020f, -1}, {Code('b', 'k', 'f', ' '), 0.40f, 0.0f, 0.020f, -1}, {Code('9', 't', 'k', ' '), 0.40f, 0.0f, 0.020f, -1},
    {Code('9', 'b', 'k', ' '), 0.40f, 0.0f, 0.020f, -1}, {Code('7', 't', 'k', ' '), 0.40f, 0.0f, 0.020f, -1}, {Code('7', 'b', 'k', ' '), 0.40f, 0.0f, 0.020f, -1},
    {Code('t', 'a', 'x', ' '), 0.45f, 0.0f, 0.040f, 1}, {Code('9', 't', 'a', ' '), 0.45f, 0.0f, 0.040f, 1}, {Code('7', 't', 'a', ' '), 0.45f, 0.0f, 0.040f, 1},
    {Code('b', 'a', 'l', ' '), 0.60f, 0.0f, 0.050f, 1}, {Code('9', 'b', '8', ' '), 0.60f, 0.0f, 0.050f, 1}, {Code('7', 'b', '8', ' '), 0.60f, 0.0f, 0.050f, 1},
    {Code('g', 'l', 'v', ' '), 1.39f, 0.93f, 0.025f, -1}, {Code('t', 's', 'p', ' '), 1.39f, 0.93f, 0.025f, -1}, {Code('9', 'g', 'l', ' '), 1.39f, 0.93f, 0.025f, -1},
    {Code('9', 't', 's', ' '), 1.39f, 0.93f, 0.025f, -1}, {Code('7', 'g', 'l', ' '), 1.39f, 0.93f, 0.025f, -1}, {Code('7', 't', 's', ' '), 1.39f, 0.93f, 0.025f, -1},
};
inline const ThrownRow* FindThrown(uint32_t code) {
    for (const ThrownRow& r : kThrown)
        if (r.code == code) return &r;
    return nullptr;
}

// The six throwing potions (Rancid Gas, Oil, Choking Gas, Exploding, Strangling Gas, Fulminating Potion): the game files them under the thrown kind, like a
// knife; they are thrown, never swung, so hit by touch leaves them out. (gamestate.cpp kThrowPotions: the same codes.)
inline bool IsThrowPotion(uint32_t code) {
    return code == Code('g', 'p', 's', ' ') || code == Code('o', 'p', 's', ' ') || code == Code('g', 'p', 'm', ' ') ||
           code == Code('o', 'p', 'm', ' ') || code == Code('g', 'p', 'l', ' ') || code == Code('o', 'p', 'l', ' ');
}

// Does the touch swing this weapon at all? The kinds that are swung (meleetouch::MeleeKind), and - with [gameplay] melee_touch_thrown - a javelin, a throwing
// knife or axe. A throwing potion never (it is the kind's, told by the item code; a thrown-kind item whose code is not known yet is not swung either).
inline bool TouchKind(uint32_t type, uint32_t code, bool thrownOn) {
    if (meleetouch::MeleeKind(type)) return true;
    if (!thrownOn || !meleetouch::ThrownKind(type)) return false;
    if (type == D2RVR_TYPE_JAVELIN) return true;   // no potion is a javelin
    return code != 0 && !IsThrowPotion(code);
}

// Metres per model unit from the drawn bone's scale and the hero's units per metre (weaponextent::UnitM: 0 = unusable).
inline float UnitM(float boneRowLength, float unitsPerMetre) { return weaponextent::UnitM(boneRowLength, unitsPerMetre); }

// The weapon's own reach on a shape: the tip side, the butt side and the side of the point from the model's bounds (vr/weapon_extents.h) at `unitM` metres per
// model unit. The capsule's rounded end reaches one radius past its segment, so each end is a radius short: the capsule ends AT the wood. The radius is the
// shape's own. No unit or no row for the code: the shape as it was.
inline Shape FromModel(Shape s, uint32_t code, float unitM) {
    weaponextent::Reach r;
    if (!(unitM > 0.0f) || !weaponextent::ReachOf(code, &r)) return s;
    s.length = std::max(r.tip * unitM - s.radius, 0.0f);
    // a butt behind the origin ends a radius short of the wood; a blade that starts AHEAD of it (a fist weapon, negative) starts a radius further on
    s.butt = r.butt >= 0.0f ? std::max(r.butt * unitM - s.radius, 0.0f) : r.butt * unitM - s.radius;
    s.tipSign = r.tipSign;
    s.axis = r.axis;
    return s;
}

// The blade for the item in the hand: the kind's, with the base item's row on top where there is one (the staves, the thrown weapons), and - with a unit
// (UnitM, the pose's metres per model unit) - the model's own ends and point side on top of that (FromModel). `code` 0 = unknown; unitM 0 = the constants.
inline Shape ForItem(uint32_t code, uint32_t type, bool twoHandsOn, float unitM = 0.0f) {
    Shape s = ForKind(type, twoHandsOn);
    if (meleetouch::ThrownKind(type)) {
        if (const ThrownRow* r = FindThrown(code)) {
            s.length = r->length;
            s.butt = r->butt;
            s.radius = r->radius;
            s.tipSign = r->tipSign;
        }
        return FromModel(s, code, unitM);
    }
    if (type == D2RVR_TYPE_STAFF) {
        if (const ItemRow* r = FindItem(code)) {
            s.length *= r->scale;
            s.butt *= r->scale;
            if (r->radius > 0.0f) s.radius = r->radius;
        }
    }
    return FromModel(s, code, unitM);
}

// The user's live tuning ([debug] melee_blade_length_pct, melee_blade_butt_pct, melee_blade_radius_cm, melee_blade_flip).
struct Tuning {
    float lengthPct = 100.0f;   // 50..150: the tip side
    float radiusCm = 0.0f;      // 0 = the weapon's own; else 0.5..15
    bool flip = false;          // the tip to the other side of the bone's Y, for every weapon
    float buttPct = 100.0f;     // 0..150: the butt side (0 = no butt at all)
};
inline Tuning Clamp(Tuning t) {
    t.lengthPct = std::isfinite(t.lengthPct) ? std::clamp(t.lengthPct, 50.0f, 150.0f) : 100.0f;
    t.radiusCm = !std::isfinite(t.radiusCm) || t.radiusCm <= 0.0f ? 0.0f : std::clamp(t.radiusCm, 0.5f, 15.0f);
    t.buttPct = std::isfinite(t.buttPct) ? std::clamp(t.buttPct, 0.0f, 150.0f) : 100.0f;
    return t;
}
inline Shape Tuned(Shape s, Tuning t) {
    t = Clamp(t);
    s.length *= t.lengthPct * 0.01f;
    s.butt *= t.buttPct * 0.01f;
    if (t.radiusCm > 0.0f) s.radius = t.radiusCm * 0.01f;
    if (t.flip) s.tipSign = -s.tipSign;
    return s;
}

// The thrown weapons' own live tuning ([debug] melee_thrown_length_pct, melee_thrown_butt_pct), ON TOP of the general one above and only for the thrown kinds
// (the javelin, the throwing knife and axe): their blades are estimates and the general sliders move every weapon at once. The tip side reaches up to 250 %
// (the Glaive's 1.9 m is the 3-cell javelin's 4/3, the headset may say more), the butt side up to 300 %.
struct ThrownTuning {
    float lengthPct = 100.0f;   // 50..250: the tip side
    float buttPct = 100.0f;     // 0..300: the butt side
};
inline ThrownTuning Clamp(ThrownTuning t) {
    t.lengthPct = std::isfinite(t.lengthPct) ? std::clamp(t.lengthPct, 50.0f, 250.0f) : 100.0f;
    t.buttPct = std::isfinite(t.buttPct) ? std::clamp(t.buttPct, 0.0f, 300.0f) : 100.0f;
    return t;
}
inline Shape TunedThrown(Shape s, ThrownTuning t) {
    t = Clamp(t);
    s.length *= t.lengthPct * 0.01f;
    s.butt *= t.buttPct * 0.01f;
    return s;
}

// Two weapons: a melee weapon in each hand's slot (a Barbarian's second blade, an Assassin's second claw). A shield or a quiver
// reads as kind 0 (gamestate KindOf), a bow / crossbow / thrown weapon is not swung.
inline bool DualWield(uint32_t rightKind, uint32_t leftKind) {
    return rightKind != 0 && leftKind != 0 && meleetouch::MeleeKind(rightKind) && meleetouch::MeleeKind(leftKind);
}

// ---- the frame ------------------------------------------------------------------------------------------------------------------
// What the view says: where the hands hang in the world (g_eyeWorld, world units), the view's right and forward on the ground
// (g_stickFrame's camR / camF), and the units per metre. The head's frame -> world is vrcam's `world()`:
//   w = v.x * right - v.z * fwd, w.y += v.y   (metres), times upm, from the eyes.
struct HeadFrame {
    bool ok = false;
    float right[3] = {1.0f, 0.0f, 0.0f}, fwd[3] = {0.0f, 0.0f, 1.0f};
    float eyeWorld[3] = {};
    float upm = 4.0f;
};

// A point in the head's frame (metres from the eyes) -> the render world over upm (metres of that frame).
inline stroke::Vec PointToRender(const HeadFrame& h, const float v[3]) {
    stroke::Vec p;
    p.x = h.eyeWorld[0] / h.upm + v[0] * h.right[0] - v[2] * h.fwd[0];
    p.y = h.eyeWorld[1] / h.upm + v[0] * h.right[1] - v[2] * h.fwd[1] + v[1];
    p.z = h.eyeWorld[2] / h.upm + v[0] * h.right[2] - v[2] * h.fwd[2];
    return p;
}
// A direction in the head's frame -> the same, no origin.
inline stroke::Vec DirToRender(const HeadFrame& h, const float v[3]) {
    return {v[0] * h.right[0] - v[2] * h.fwd[0], v[0] * h.right[1] - v[2] * h.fwd[1] + v[1], v[0] * h.right[2] - v[2] * h.fwd[2]};
}

// The weapon bone as the skeleton hook keeps it: its origin and the axis the blade lies along (its Y, or - the shape's `axis` 2, LineOf - its Z) in the head's frame.
struct Bone {
    float origin[3] = {};
    float axisY[3] = {0.0f, 1.0f, 0.0f};
};

// The axis of the posed bone a shape lies along: its Y, or for the claws and katars its Z (the bone's third axis, yellow in the debug). zSign is what the GAME'S OWN pose
// says of the Z axis (skel::PosedBlade::zSign): +1 it runs the way the forearm goes toward the fist (the blade goes forward out of the fist), -1 against it (the bone of a
// hand that is the mirror of the other's), 0 unknown, taken as +1. Only the Z of a fist weapon is signed so; every Y axis is as the bone has it.
inline void LineOf(const Shape& s, const float axisY[3], const float axisZ[3], int zSign, float out[3]) {
    const float* a = s.axis == 2 ? axisZ : axisY;
    const float k = s.axis == 2 && zSign < 0 ? -1.0f : 1.0f;
    for (int i = 0; i < 3; ++i) out[i] = a[i] * k;
}

// The blade, in the render world over upm: along the bone's Y through its origin, from the butt (shape.butt behind the origin) to the tip
// (shape.length ahead of it, on the side shape.tipSign says). The blade's `base` is the butt end.
inline stroke::Blade FromBone(const HeadFrame& h, const Bone& b, const Shape& shape) {
    const stroke::Vec origin = PointToRender(h, b.origin);
    const stroke::Vec dir = stroke::Normalized(DirToRender(h, b.axisY)) * static_cast<float>(shape.tipSign);
    return {origin - dir * shape.butt, origin + dir * shape.length, shape.radius};
}

// [flail] chain: a flail's ball swinging on its chain is the blade - a ball, not the handle's capsule: a segment of 1 cm along the last link,
// centred on the ball, as thick as the ball (its radius from the model's mesh, flail_chain.h HeadOf). Its "bone" is the ball: origin its centre,
// axisY the last link's way. Every point of it moves with the ball, so the contact's speed, its way and the swing's travel are the ball's.
constexpr float kBallHalf = 0.005f;
inline Shape BallShape(float radiusM) {
    Shape s;
    s.length = kBallHalf;
    s.butt = kBallHalf;
    s.radius = radiusM;
    s.tipSign = 1;
    return s;
}

// The same blade in the ROOM (tracking) frame - the frame the hand's samples are in (room_pose.h RoomPose) - so the stroke engine can measure the
// BLADE's own motion there (Sample::room): world turning or the hero walking cannot power it, as they cannot power the hand's. The bone is in the
// head's turn-only frame (metres from the eyes, x right, y up, z back), exactly the frame the shared block's hands are in; head: where the head is
// in the room (the shared block's headRoom) and yawRad its turn (headYawRoomDeg), what RoomPose is given for a hand.
struct RoomFrame {
    float head[3] = {};
    float yawRad = 0.0f;
};
inline stroke::Blade RoomFromBone(const RoomFrame& f, const Bone& b, const Shape& shape) {
    const stroke::Vec dir = stroke::Normalized({b.axisY[0], b.axisY[1], b.axisY[2]}) * static_cast<float>(shape.tipSign);
    const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float base[3] = {b.origin[0] - dir.x * shape.butt, b.origin[1] - dir.y * shape.butt, b.origin[2] - dir.z * shape.butt};
    const float tip[3] = {b.origin[0] + dir.x * shape.length, b.origin[1] + dir.y * shape.length, b.origin[2] + dir.z * shape.length};
    return {meleetouch::RoomPose(f.head, f.yawRad, base, rot).p, meleetouch::RoomPose(f.head, f.yawRad, tip, rot).p, shape.radius};
}

}  // namespace bladeshape
