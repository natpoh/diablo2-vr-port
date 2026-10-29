// metal_head.h - [gameplay] melee_metal_head: where on a weapon its HEAD begins, the part that strikes (hit by touch, vr/melee_touch.cpp; the rule is
// vr/strike_direction.h Rule::headAlong). A pole-arm's blade, an axe's or a mace's head, a spear's point, a sword's or a dagger's blade: a touch with the
// shaft or the handle below it pushes (the weight model, the blade push) but is no strike. The author, 2026-10-24: weapons with a metal head should not kill
// with the shaft, only push. Wooden weapons (staves, wands, the plain clubs) strike with their whole length, as before; the claws and orbs are not judged.
//
// Where the head begins is the game's own model's (vr/weapon_heads.h, read off each base weapon's mesh by tools/gen_weapon_heads.py); an item it has no row
// for takes its kind's default share (kDefaultShare) - and the caller says so in the log. Pure maths, no game calls.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "strike_direction.h"
#include "weapon_extents.h"
#include "weapon_heads.h"

namespace metalhead {

// The kinds judged: those with a metal head or an edge. A staff, a wand, a claw, an orb, bare fists, the rest: never.
inline bool Applies(int kind) {
    switch (kind) {
        case strikedir::kSword: case strikedir::kAxe: case strikedir::kMace: case strikedir::kScepter: case strikedir::kPolearm: case strikedir::kSpear:
        case strikedir::kDagger: case strikedir::kJavelin: case strikedir::kThrowing: return true;
        default: return false;
    }
}

// A kind's head as a share of the weapon's whole length (butt end to point), for an item the models gave nothing for: about what the models give that kind
// (the table's own spread - swords 73 .. 83 %, daggers 65 .. 80 %, axes 15 .. 57 %, maces 12 .. 33 %, pole-arms 22 .. 38 %, spears 13 .. 35 %).
inline float DefaultShare(int kind) {
    switch (kind) {
        case strikedir::kSword: return 0.75f;
        case strikedir::kDagger: return 0.65f;
        case strikedir::kThrowing: return 0.6f;
        case strikedir::kAxe: return 0.4f;
        case strikedir::kMace: return 0.3f;
        case strikedir::kScepter: return 0.25f;
        case strikedir::kPolearm: return 0.3f;
        case strikedir::kSpear: return 0.2f;
        case strikedir::kJavelin: return 0.3f;
        default: return 1.0f;
    }
}

enum class Source { None, Model, Wood, KindDefault };
inline const char* SourceText(Source s) {
    return s == Source::Model ? "the model's own" : s == Source::Wood ? "wood: its whole length strikes" : s == Source::KindDefault ? "the kind's default: the model gave no head" : "not judged";
}

// The head's start along the blade the touch sweeps (0 its butt end, 1 its point - stroke::Contact::along), for the item `code` of kind `kind` (strikedir's
// index). buttM, lengthM: the blade's two sides of the weapon bone's origin (bladeshape::Shape butt, length); unitM: metres per model unit as drawn (0 = not
// known: the model's share is used). -1: nothing to refuse (a kind not judged, a wooden weapon, a blade with no length). Never beyond kMaxAlong: the point
// itself always strikes.
inline constexpr float kMaxAlong = 0.9f;
inline float Along(uint32_t code, int kind, float buttM, float lengthM, float unitM, Source* source) {
    Source s = Source::None;
    float along = -1.0f;
    const float len = buttM + lengthM;
    if (Applies(kind) && len > 1e-3f) {
        weaponextent::Reach r;
        float head = 0.0f;
        if (weaponhead::Find(code, &head) && weaponextent::ReachOf(code, &r) && r.tip + r.butt > 1e-4f) {
            const float ahead = head * static_cast<float>(r.tipSign);   // model units ahead of the origin, toward the point
            if (ahead <= -r.butt + 1e-4f) s = Source::Wood;               // (the row's edge at the butt end: the whole length)
            else {
                s = Source::Model;
                along = unitM > 0.0f ? (buttM + ahead * unitM) / len : (r.butt + ahead) / (r.butt + r.tip);
            }
        } else {
            s = Source::KindDefault;
            along = 1.0f - DefaultShare(kind);
        }
    }
    if (source) *source = s;
    return along > 0.0f ? std::min(along, kMaxAlong) : -1.0f;
}

// [gameplay] melee_head_volume: a headed weapon's head as its own volume where the model has it (vr/weapon_heads.h Row::cap - an axe's blade beside the
// shaft, a hammer's face and spike, a mace's head round it) and its shaft as thin as the model's (Row::shaftR). Model units, the mesh's own x y z: the
// weapon bone's axes. False: none (a blade, wood, a claw, an item with no row).
struct Volume {
    float a[3] = {}, b[3] = {};
    float r = 0.0f;
    float shaftR = 0.0f;
};
inline bool VolumeOf(uint32_t code, int kind, Volume* out) {
    if (!Applies(kind) || kind == strikedir::kSword || kind == strikedir::kDagger) return false;
    const weaponhead::Row* row = weaponhead::RowOf(code);
    if (!row || !(row->cap[6] > 0.0f) || !(row->shaftR > 0.0f)) return false;
    if (out) {
        for (int k = 0; k < 3; ++k) { out->a[k] = row->cap[k]; out->b[k] = row->cap[3 + k]; }
        out->r = row->cap[6];
        out->shaftR = row->shaftR;
    }
    return true;
}
// A point of the model (its own x y z, model units) where the posed bone puts it: origin + x X + y Y + z Z, each of the bone's axes (rows, any length) a unit,
// times unitM (metres per model unit as drawn). False: an axis with no length.
inline bool ModelPoint(const float origin[3], const float axes[3][3], float unitM, const float p[3], float out[3]) {
    float u[3][3];
    for (int i = 0; i < 3; ++i) {
        const float l = std::sqrt(axes[i][0] * axes[i][0] + axes[i][1] * axes[i][1] + axes[i][2] * axes[i][2]);
        if (!(l > 1e-6f)) return false;
        for (int k = 0; k < 3; ++k) u[i][k] = axes[i][k] / l;
    }
    for (int k = 0; k < 3; ++k) out[k] = origin[k] + (p[0] * u[0][k] + p[1] * u[1][k] + p[2] * u[2][k]) * unitM;
    return true;
}
// The shaft thinned to its own thickness (radius shaftR x unitM, at least kMinShaftM): its two ends stay where the wood ends (the capsule's ends are a
// radius short of them, bladeshape::FromModel). length / butt / radius: the shape's, changed in place.
inline constexpr float kMinShaftM = 0.01f;
inline void ThinShaft(float shaftR, float unitM, float* length, float* butt, float* radius) {
    if (!(shaftR > 0.0f) || !(unitM > 0.0f)) return;
    const float r = std::max(kMinShaftM, shaftR * unitM);
    if (!(r < *radius)) return;
    const float d = *radius - r;
    *length += d;
    if (*butt > 0.0f) *butt += d;
    *radius = r;
}

}  // namespace metalhead
