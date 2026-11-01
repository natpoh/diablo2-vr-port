// melee_bodies.h - the shapes melee by touch sweeps: the capsule of the weapon in hand and the capsule of a
// monster's body. Pure maths (no game calls): the caller reads the game's numbers (the weapon's kind, a
// monster's MonStats bytes and place) and these turn them into metres in the frame the swing engine
// (stroke_contact.h) works in.
//
// The weapon lengths by kind and the monster body are bearstonem/d2r-pcvr's (MIT; vr/vrcam.cpp
// MeleeContactTick and vr/melee.cpp BodyFor): "deliberately approximate" there too - the ground footprint
// the game keeps for a monster, a height from the legacy artwork height, no mesh. Their ground shear
// (render = grid + height * sqrt(3/2) on both horizontal axes) is verified on paired positions in a cave.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_state.h"
#include "stroke_contact.h"

namespace meleetouch {

// A blade for the weapon in hand, along its attach bone's +Y from the grip (the shaft; -Y is the hilt).
struct BladeShape { float length = 0.65f, radius = 0.065f; };

// Does a held weapon of this kind swing at monsters? A bow, a crossbow and things thrown do not: they have their own gestures.
// (The things thrown - javelins, throwing knives and axes - are a kind of their own below: ThrownKind, swung only with [gameplay]
// melee_touch_thrown on, see blade_shape.h TouchKind. MeleeKind is the kinds that are swung whatever the switches say - and the ones a
// second weapon in the other hand makes a dual wield of.)
inline bool MeleeKind(uint32_t type) {
    switch (type) {
        case D2RVR_TYPE_BOW: case D2RVR_TYPE_CROSSBOW: case D2RVR_TYPE_THROWING: case D2RVR_TYPE_JAVELIN: case D2RVR_TYPE_UNKNOWN: return false;
        default: return true;
    }
}

// A weapon that is also thrown: a javelin, a throwing knife or axe (and the throwing potions, which the game files under the same kind - the
// item code tells them apart, blade_shape.h IsThrowPotion). They have a one-hand melee damage of their own in the game's Weapons table.
inline bool ThrownKind(uint32_t type) { return type == D2RVR_TYPE_THROWING || type == D2RVR_TYPE_JAVELIN; }

// Conservative capsules by kind, not by class or animation: no mesh bounds. twoHandsOn: held in both hands (a two-handed sword).
inline BladeShape WeaponBlade(uint32_t type, bool twoHandsOn) {
    BladeShape s;
    switch (type) {
        case D2RVR_TYPE_UNARMED: s.length = 0.04f; s.radius = 0.08f; break;
        case D2RVR_TYPE_DAGGER: case D2RVR_TYPE_CLAW: case D2RVR_TYPE_ORB: s.length = 0.28f; break;
        case D2RVR_TYPE_WAND: s.length = 0.38f; break;
        case D2RVR_TYPE_STAFF: s.length = 1.2f; break;
        case D2RVR_TYPE_SPEAR: case D2RVR_TYPE_POLEARM: s.length = 1.5f; break;
        default: if (twoHandsOn) s.length = 1.1f; break;
    }
    return s;
}

// The renderer shears elevated ground along both horizontal axes by cot(30 deg) / sqrt(2) = sqrt(3/2) per unit of height.
inline constexpr float kGroundShear = 1.22474487139f;
// The grid plane of a render position (any one unit throughout: metres, or game units).
inline stroke::Vec GridPlane(stroke::Vec render) { return {render.x - render.y * kGroundShear, 0.0f, render.z - render.y * kGroundShear}; }
inline stroke::Vec RenderGround(stroke::Vec grid, float height) {
    return {grid.x + height * kGroundShear, height, grid.z + height * kGroundShear};
}

// The margin a monster's capsule is given round its footprint (BodyFor): a touch counts a little outside the body. The bodies the held weapon
// meets ([weight] monster_bodies, monster_bodies.h) are the capsule without it.
inline constexpr float kTouchSlackM = 0.05f;

// What the monster's row in the game's MonStats table says (bytes 8, 9 and 13 of the row).
struct MonsterRow { uint8_t footprintA = 0, footprintB = 0, height = 0; };

// A monster's capsule in render metres. gridX / gridZ: its place on the game's grid in game WORLD units (twice the
// cell), floorM: the height of the ground it stands on in metres (the hero's own, a flat ground is assumed), upm:
// game units per metre at the hero's size. False for a row that does not read like one.
inline bool BodyFor(const MonsterRow& r, float gridX, float gridZ, float floorM, float upm, stroke::Body& out) {
    if (!std::isfinite(gridX + gridZ + floorM) || !std::isfinite(upm) || upm < 1.0f || upm > 12.0f) return false;
    if (r.footprintA < 1 || r.footprintA > 8 || r.footprintB < 1 || r.footprintB > 8) return false;
    const float footprint = std::max(0.12f, static_cast<float>(std::max(r.footprintA, r.footprintB)) * 0.7f / upm);
    const float height = std::clamp(static_cast<float>(r.height ? r.height : 72) * 0.10f / upm, 0.4f, 4.0f);
    const float radius = std::min(footprint, height * 0.45f) + kTouchSlackM;
    const stroke::Vec ground = RenderGround({gridX / upm, 0.0f, gridZ / upm}, floorM);
    out.radius = radius;
    out.bottom = {ground.x, floorM + radius, ground.z};
    out.top = {ground.x, std::max(floorM + radius, floorM + height - radius), ground.z};
    return true;
}

}  // namespace meleetouch
