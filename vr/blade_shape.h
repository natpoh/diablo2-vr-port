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
// bone's +Y or -Y. So a blade has a TIP side (`length` along the axis) and a BUTT side (`butt` behind the origin). Both, and the
// thickness, are per kind of weapon and, where known, per base item (ForItem): constants, because the weapon models' own extent is
// not readable (the .model files are Granny GR2 files whose sections are packed with BitKnit, and the pose carries no bounds).
// Measured on the drawn weapons (the user's screenshots, Barbarian, 2026-10-14): the dagger's blade ~0.5 m, along the bone's -Y;
// a short staff's head 0.67 m from the bone (so the segment ends 0.66: the capsule's rounded end reaches one radius past the segment, and
// should end AT the wood) and its shaft ~1.6 cm in radius (the first 2.5 cm drew twice as thick as the wood). The head's place is read off
// a headset screenshot with the perspective taken out: the capsule's apparent width tapers linearly along its axis (1/depth), which puts the
// axis's vanishing point on the picture and so the angle of the staff to the view; a naive pixel count along the axis gave 6-7 cm of
// overshoot for the 0.78 m try, the corrected one 13 cm.
// Everything else - the butt of every weapon, the other staves - is an ESTIMATE, marked so below; the sliders [debug]
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

namespace bladeshape {

// A blade along its bone's Y: how far it runs from the bone's origin to the tip, how far behind the origin to the butt, how thick it is,
// and to which side of +Y its tip lies.
struct Shape {
    float length = 0.65f;   // origin -> tip, metres
    float butt = 0.0f;      // origin -> butt (the other way), metres
    float radius = 0.04f;
    int tipSign = 1;        // +1: the tip along the bone's +Y; -1: along -Y
};

// The constants by kind (the fallback where nothing better is known). twoHandsOn: held in both hands.
inline Shape ForKind(uint32_t type, bool twoHandsOn) {
    Shape s;
    switch (type) {
        case D2RVR_TYPE_UNARMED: s.length = 0.04f; s.radius = 0.08f; break;
        case D2RVR_TYPE_DAGGER: s.length = 0.50f; s.radius = 0.030f; s.tipSign = -1; break;   // measured on the drawn dagger: its blade runs along the bone's -Y
        case D2RVR_TYPE_CLAW: case D2RVR_TYPE_ORB: s.length = 0.28f; s.radius = 0.045f; break;
        case D2RVR_TYPE_WAND: s.length = 0.38f; s.radius = 0.020f; break;
        // measured on the drawn short staff: the head 0.67 m from the bone, so the segment ends 0.66 m (+ the 1.6 cm of the rounded end = the
        // wood's end), the shaft 1.6 cm in radius. The butt is an ESTIMATE: the bone is the middle of the two-handed grip, between the hands,
        // so the staff runs as far behind it as before it (centred); not seen yet - the butt end was below the picture's edge
        case D2RVR_TYPE_STAFF: s.length = 0.66f; s.butt = 0.66f; s.radius = 0.016f; break;
        // ESTIMATES (the same cause as the staff's: the bone is the middle of the grip): the grip of a spear or a pole-arm lies about a third
        // from its butt, so the butt side is half the tip side. WHICH WAY the tip lies is told by the weapon's animation class (Weapons.txt wclass),
        // which turns the attach bone: the measured weapons of 1ht (one-hand thrust: the dagger, the javelin) have their point along the bone's -Y,
        // those of 1hs (the hand axe beside the dagger, the swords: +Y, never reported wrong) and stf (the staff, measured) along +Y. A spear /
        // pike / trident is 2ht (two-hand THRUST): by the class it is -Y - an INFERENCE, no spear has been seen: if its capsule points at the butt,
        // [debug] melee_blade_flip turns it (and every other weapon)
        case D2RVR_TYPE_SPEAR: s.length = 1.50f; s.butt = 0.50f; s.radius = 0.016f; s.tipSign = -1; break;
        case D2RVR_TYPE_POLEARM: s.length = 1.50f; s.butt = 0.50f; s.radius = 0.020f; break;
        // THROWN WEAPONS (hit by touch with [gameplay] melee_touch_thrown). The JAVELIN (wclass 1ht) is MEASURED on two headset screenshots of a Javelin
        // in the right hand with the capsule drawn: the spearhead lies along the bone's -Y (the capsule's long side first went the wrong way, to the butt -
        // the first try's tip 0.80 m along +Y ran over the shaft to the right, away from the head, and the second try's 1.40 m made it worse), the
        // head's point 1.04 m from the bone (1.06 and 1.03 on the two pictures: the capsule's own width as the ruler, the perspective taken out as for the
        // short staff: D sqrt(f^2 + B^2) (1/w2 - 1/w1) between the capsule's base end of known place and the point; +-0.1). The bone sits at the hand (the
        // grip). The butt, along +Y, is not in a picture: 0.30 m, a guess ([debug] melee_thrown_butt_pct tunes it). The capsule is 2.5 cm in radius: the
        // shaft is 1 cm, the wrapped socket 1.5, the leaf-shaped head 3 cm half-width, and the head is what touches. Its axis is ~3-4 degrees off the
        // bone's Y in the first picture - not corrected, one view cannot say which way in the bone's frame. The throwing kind covers knives and axes
        // (ForItem tells them apart by the code, ESTIMATES): a short one with no butt for an item not known
        case D2RVR_TYPE_JAVELIN: s.length = 1.04f; s.butt = 0.30f; s.radius = 0.025f; s.tipSign = -1; break;
        case D2RVR_TYPE_THROWING: s.length = 0.40f; s.radius = 0.030f; break;
        case D2RVR_TYPE_AXE: case D2RVR_TYPE_MACE: case D2RVR_TYPE_SCEPTER:
            s.length = twoHandsOn ? 1.10f : 0.65f; s.butt = twoHandsOn ? 0.20f : 0.0f; s.radius = 0.050f; break;
        default: s.length = twoHandsOn ? 1.10f : 0.65f; s.butt = twoHandsOn ? 0.15f : 0.0f; s.radius = 0.035f; break;   // swords and the rest
    }
    return s;
}

// Four characters of an item code ('sst ' - the game's base weapon code, space padded) as one number.
constexpr uint32_t Code(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

// Per base item. The staves come in three size classes by the game's own inventory size (Weapons.txt invheight; the elite and exceptional
// versions share their base's): 3 cells - the short staff, Jo Staff, Walking Stick, Staff of Kings (the measured one); 4 cells - the long,
// gnarled and battle staves with their versions, the Horadric Staff; and the war staff family, 4 cells and twice as wide. Only the short
// staff's numbers are measured; the others are the short staff's scaled by their height in cells (x 4/3) - an ESTIMATE, so the sliders.
// A new row here is the way to put a measured weapon in: {code, scale of the kind's length and butt, radius or 0 for the kind's own}.
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

// The thrown weapons whose blade is not the kind's (everything ESTIMATED, the sliders tune it; the codes are Weapons.txt's - the base, the exceptional
// and the elite version of each): the throwing knives, darts and flying knives run along the bone's -Y like the measured dagger (the same 1 x 2 cells,
// shorter); the throwing axes along +Y like an axe, the 1 x 2 ones small, the 2 x 3 ones (Balanced Axe, Hurlbat, Winged Axe) as an axe; the javelins of 4 cells
// (Glaive, Throwing Spear and their versions) are the 3-cell javelin's x 4/3, as the staves of 4 cells are the short staff's. The 3-cell javelins (Javelin,
// Pilum, Short Spear, the Amazon's) are the kind's own.
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
    {Code('g', 'l', 'v', ' '), 1.39f, 0.40f, 0.025f, -1}, {Code('t', 's', 'p', ' '), 1.39f, 0.40f, 0.025f, -1}, {Code('9', 'g', 'l', ' '), 1.39f, 0.40f, 0.025f, -1},
    {Code('9', 't', 's', ' '), 1.39f, 0.40f, 0.025f, -1}, {Code('7', 'g', 'l', ' '), 1.39f, 0.40f, 0.025f, -1}, {Code('7', 't', 's', ' '), 1.39f, 0.40f, 0.025f, -1},
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

// The blade for the item in the hand: the kind's, with the base item's row on top where there is one (the staves, the thrown weapons). `code` 0 = unknown.
inline Shape ForItem(uint32_t code, uint32_t type, bool twoHandsOn) {
    Shape s = ForKind(type, twoHandsOn);
    if (meleetouch::ThrownKind(type)) {
        if (const ThrownRow* r = FindThrown(code)) {
            s.length = r->length;
            s.butt = r->butt;
            s.radius = r->radius;
            s.tipSign = r->tipSign;
        }
        return s;
    }
    if (type != D2RVR_TYPE_STAFF) return s;
    if (const ItemRow* r = FindItem(code)) {
        s.length *= r->scale;
        s.butt *= r->scale;
        if (r->radius > 0.0f) s.radius = r->radius;
    }
    return s;
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

// The weapon bone as the skeleton hook keeps it: its origin and its Y axis in the head's frame.
struct Bone {
    float origin[3] = {};
    float axisY[3] = {0.0f, 1.0f, 0.0f};
};

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
