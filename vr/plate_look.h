// plate_look.h - the monster's plate and the item labels on the ground, kept apart in the interface layer's effect (reshade/D2R_DepthFog.fx GameLabels).
//
// The game's interface layer comes out of the picture whole for D2R_DepthFog.fx, which draws it back per eye. Until now everything in it but the toolbar and the
// map moved by ONE shift (LabelBase, [hud] labels_near_body: "Item labels: stereo depth on top, all") - the monster's plate with the item labels, so a player
// who set the labels' depth moved the bars over the monsters with it (Serg, test-56, 2026-10-17: "the item settings change the bars over monsters the same way,
// and Name over a monster's own do not"). Now, in first person (F4), the plate has its own depth ([hud] monster_near_body) and its own opacity and size
// ([hud] monster_alpha, monster_size); the item labels keep labels_near_body, labels_depth_body, labels_alpha, labels_size and labels_far, and nothing of the plate's.
// (Everywhere else the plate stays on the labels' plane, as the plane is tilted there.) The bar hung in the room (vr/plate_world.h) is a layer of its own and is
// touched by none of the labels' settings at all.
//
// This header is the part a test can hold: which settings each side reads (the signatures: a plate function takes no label setting and a label function no plate
// setting - except the one place where the plate's box is needed, PlateNeedsBox) and, as C++, what the effect does with a pixel (LayerPixel mirrors GameLabels'
// plate branch line by line).
#pragma once

#include <algorithm>
#include <cmath>

namespace platelook {

// percent of the half screen -> uv (vrcam.cpp has always used 0.005)
constexpr float kShiftPerPercent = 0.005f;
inline float Shift(float percent) { return percent * kShiftPerPercent; }

// ---- the same two rules the item labels have, for the monster's plate --------------------------------------------------------------------------
// "Size far away" ([hud] labels_far / monster_far): the size you set for a thing 1.5 m away and nearer, this share of it for one 10 m away and farther, a straight
// line between; the distance is the ground distance from the hero's feet (the item's, the monster's).
constexpr float kNearM = 1.5f, kFarM = 10.0f;
inline float DistanceK(float groundM, float farFraction) {
    const float t = std::clamp((groundM - kNearM) / (kFarM - kNearM), 0.0f, 1.0f);
    return 1.0f + (farFraction - 1.0f) * t;
}
// "At their items" / "at its monster" ([hud] labels_depth_body / monster_depth_body): the shift per eye (uv, + nearer) that puts a picture at the depth of the thing
// it belongs to instead of where the game leaves it (the convergence): k * 0.5 * M0 * half the eyes' gap * (1/z - 1/conv), z the eyes' distance to the thing (the
// ground distance and the camera's height over the feet, game units). k = 1 is exactly there, 0 is where the game leaves it.
inline float DepthShift(float groundUnits, float eyeHeightUnits, float m0, float halfGap, float conv, float k) {
    const float z = std::max(1.0f, std::sqrt(groundUnits * groundUnits + eyeHeightUnits * eyeHeightUnits));
    return k * 0.5f * m0 * halfGap * (1.0f / z - (conv > 0.0f ? 1.0f / conv : 0.0f));
}

// Eyes by turns: the game lays its interface layer out for the eye DRAWN this frame, the left one and then the right one, so a label in it stands where
// that eye sees its thing - and the same shifts every frame made it jump by its whole disparity in each eye: doubled (the target's plate over a near NPC,
// 2026-10-26). The half-disparity at the thing's own depth (uv; the depth rule at k = 1) is what the effect takes that eye's layout off by first
// (D2R_DepthFog.fx PlateTurnH, LabelTurnH; LayerPixel's layerEye): each eye then shows the same every frame. 0 = nothing known (no depth, no stereo).
inline float TurnsHalf(float groundUnits, float eyeHeightUnits, float m0, float halfGap, float conv) {
    return m0 > 0.0f && halfGap > 0.0f ? DepthShift(groundUnits, eyeHeightUnits, m0, halfGap, conv, 1.0f) : 0.0f;
}

// What the plate's look needs, from the plate's own settings (the "Monster plates" group) and the monster it hangs on - none of the item labels' settings.
struct PlateIn {
    bool firstPerson = false;
    float depthPercent = 0.0f;         // [hud] monster_near_body: its own stereo depth (percent of the half screen)
    float alpha = 1.0f;                // [hud] monster_alpha
    float size = 1.0f;                 // [hud] monster_size, 1 = the game's
    float farFraction = 1.0f;          // [hud] monster_far: this share of the size for a monster 10 m away and farther (1 = no change)
    float atMonster = 0.0f;            // [hud] monster_depth_body: 0 where the game leaves the plate .. 1.5 (1 = at the monster's depth)
    // the targeted monster, when the hook has seen one lately (groundM < 0: none)
    float groundM = -1.0f;             // ground distance from the hero's feet, metres
    float groundUnits = 0.0f;          // the same, game units
    // the stereo geometry (vrcam: g_projSx, half g_stereoIpd, g_stereoConv, the camera's height); stereo false = not a stereo picture (no depth rule)
    bool stereo = false;
    float eyeHeightUnits = 0.0f, m0 = 0.0f, halfGap = 0.0f, conv = 0.0f;
};
// What the effect is told about the plate (uniforms PlateOwn, PlateShift, PlateAlpha, PlateScale).
struct PlateUniforms {
    bool own;          // PlateOwn: the plate is drawn at its own depth (first person only)
    float shift;       // PlateShift: uv, + nearer
    float alpha;       // PlateAlpha
    float scale;       // PlateScale
};
inline PlateUniforms Plate(const PlateIn& in) {
    PlateUniforms u;
    u.own = in.firstPerson;
    u.shift = in.firstPerson ? Shift(in.depthPercent) : 0.0f;
    u.alpha = in.alpha;
    u.scale = in.size;
    if (in.groundM >= 0.0f) {
        u.scale *= DistanceK(in.groundM, in.farFraction);
        if (in.firstPerson && in.stereo && in.atMonster > 0.0f)
            u.shift += DepthShift(in.groundUnits, in.eyeHeightUnits, in.m0, in.halfGap, in.conv, in.atMonster);
    }
    return u;
}

// What it is told about the item labels (LabelBase and LabelTilt come from the label settings of the view; the plate is none of it).
struct LabelUniforms {
    float base;        // LabelBase
    float tilt;        // LabelTilt
    float alpha;       // LabelAlpha
};
inline LabelUniforms Labels(bool tilted, float depthPercent, bool tiltedPlane, float tiltPercent, bool floor, bool boxNative, float alphaFloor) {
    LabelUniforms u;
    u.base = tilted ? Shift(depthPercent) : 0.0f;
    u.tilt = tilted && tiltedPlane ? Shift(tiltPercent) : 0.0f;
    u.alpha = floor && !boxNative ? alphaFloor : 1.0f;
    return u;
}

// Does the effect need the plate's box (the polled rect as uv)? For the plate's own look: faded or sized. In first person also when its depth is not the
// labels' - the one place the two sides meet: only so the effect can tell the plate's pixels from the labels'; the look it gives is the plate's alone.
inline bool PlateNeedsBox(bool firstPerson, float plateDepth, float labelsDepth, float alpha, float scale, float farFraction = 1.0f, float atMonster = 0.0f) {
    if (alpha < 0.995f || std::fabs(scale - 1.0f) > 0.005f || farFraction < 0.995f) return true;
    return firstPerson && (std::fabs(plateDepth - labelsDepth) > 0.01f || atMonster > 0.005f);
}

// ---- the effect's plate branch, as C++ ----------------------------------------------------------------------------------------------------------
// Which pixel of the interface layer an output pixel (u, v) of an eye shows, for the part of GameLabels that concerns the plate. eyeSign: +1 the left eye (moves
// right = nearer), -1 the right. box: x0 y0 x1 y1 in uv, empty (x1 <= x0) = no plate. Result: a pixel of the layer at (outU, outV), or nothing (kind Empty:
// the effect leaves it empty - the world shows), plus the plate's alpha for that pixel. Eyes by turns: layerEye the eye the layer was laid out for (+1 left,
// -1 right; 0 = the same layer for both eyes, as without turns) and turnH the plate's half-disparity at its target's depth (TurnsHalf).
enum class Kind { Layer, Empty };
struct Pick {
    Kind kind;
    float u, v;
    bool plate;        // the pixel is the plate's (its alpha applies)
};
inline bool InBox(float u, float v, const float b[4]) { return u >= b[0] && v >= b[1] && u < b[2] && v < b[3]; }
inline Pick LayerPixel(float u, float v, float eyeSign, float labelBase, float labelTilt, bool own, float plateShift, float plateScale, const float box[4],
                       float layerEye = 0.0f, float turnH = 0.0f) {
    const float su = u - eyeSign * (labelBase + labelTilt * v), sv = v;   // where the label layer's pixel is (GameLabels' src)
    const bool havePlate = box[2] > box[0];
    if (own && havePlate) {
        const float cu = 0.5f * (box[0] + box[2]), cv = 0.5f * (box[1] + box[3]);
        const float pu = u - (eyeSign * plateShift - layerEye * turnH);
        const float s = plateScale > 1e-3f ? plateScale : 1.0f;
        const float psu = cu + (pu - cu) / s, psv = cv + (v - cv) / s;
        if (InBox(psu, psv, box)) return {Kind::Layer, psu, psv, true};
        if (InBox(su, sv, box)) return {Kind::Empty, 0.0f, 0.0f, false};   // its place on the labels' plane, left empty
    }
    return {Kind::Layer, su, sv, false};
}

// ---- which labels the item-label hooks are for -------------------------------------------------------------------------------------------------
// The game's label code (LabelLayout 0x1FA9F0, LabelPaint 0x1FA8E0) lays out and paints the labels of ITEMS on the ground: both callers walk the label lists
// asking for the unit of the record by its id as type 4 (docs/labels_recon.md; checked again in the image, 2026-10-17). The hooks (labels_alpha, labels_size,
// labels_far, labels_depth_body) still ask: a unit that is READ and is not an item is none of theirs. A unit that cannot be read counts as an item, as it always did.
constexpr unsigned kUnitItem = 4;
template <class Read>
bool IsLabelOfItem(const void* unit, Read read) {
    if (!unit) return true;
    unsigned type = 0;
    if (!read(reinterpret_cast<const unsigned char*>(unit), type)) return true;
    return type == kUnitItem;
}

}  // namespace platelook
