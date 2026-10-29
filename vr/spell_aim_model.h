// spell_aim_model.h - [hands] spell_aim_model: the line a spell is aimed along, taken from the hero as drawn (vrcam's HandRay). Pure.
//
// The old aim (spell_aim_model off, and the fallback here) is the controller's own axes: its knuckles' -Y, or -Z when they point up or down, a staff's
// line between the controllers, then [bow] aim_yaw / staff_aim_yaw turned on top by hand. With this on, the line comes from the model:
//
//   a staff, wand or orb in hand (the weapon the game says is in hand):
//     drawn by the skeleton hook - its attach bone's +Y as the finished pose has it, from the bone's origin: the axis the weapon lies on, toward its
//                      head (vr/blade_shape.h has the tip on +Y for all three). Weapon.
//     carried by [weight] - the drawn weapon lags, sags and slides (vr/aim_source.h), so not that: the shaft toward the tip as the carrying hand's
//                      grip holds it, turned by that controller (weight::AimInfo::shaft) - the same weapon axis, fixed by the grip. Grip.
//                      Both hands on it (test-54: "with both hands the ray follows the hand that took it first - it must go along the vector
//                      between the hands, where the staff looks"): the staff as the weight model holds it between the two grips - its butt to
//                      its tip (weight::ShaftEnds), from the tip. Not the line between the two CONTROLLERS (0.170: "with both hands the ray goes
//                      off to the side, not along the staff"): each grip sits a fist's offset from its controller, turned with it, so with
//                      the hands near each other that line ran tens of degrees off the drawn staff. Hands.
//   otherwise, the casting hand as drawn (the rig's hand bone and its first knuckles):
//     the fingers - wrist -> middle knuckle - while they lie within the old rule's 0.7 of level (as HandRay's knuckles). Fingers.
//     the palm - out of it - when the fingers point up or down (a hand raised, palm to the target). Palm.
//   A line within 5 degrees of straight up or down has no heading on the ground: the next rule is tried (a staff held upright aims with its hand).
//   [hands] staff_upright_arm (vr/staff_upright.h): a STAFF near upright aims along the arm instead - the drawn shoulder to the drawn palm of the
//   hand that holds it - blended with the staff's own line as it tilts (Out::armW, the arm's share); off, or no fresh arm: as above.
//   Nothing usable (no fresh pose): None - the caller keeps the old aim, corrections and all.
//
// How far out the cast's point goes along that line ("accuracy", the cells HookAttackPoint puts it out - the farther, the finer the steps of the
// command's whole cells), by what the hero holds (test-54): a staff keeps [bow] staff_range_one / staff_range_two (one hand on it / both); a wand or
// orb [bow] caster_range (20); anything else - bare hands - [bow] hand_range (5). Below aimpoint's 3 cells: the game's own point.
//
// Idea after bearstonem/d2r-pcvr (MIT): vr/vrcam.cpp HandRay / BowTick aim a staff, wand, scepter or orb along the posed weapon bone's casting axis
// (+Y; a staff's shaft toward its casting tip) and a free hand along its tracked aim pose; steamvr/spell_cast.h PlanarAim drops a line within five
// degrees of vertical. Here the free hand is the drawn hand, not the controller, and the game's own cast is untouched: only its target point is
// steered (vrcam HookSpellResolve / HookAttackTarget / HookUnitFacing).
#pragma once

#include "staff_upright.h"

#include <cmath>

namespace spellmodel {

enum class Src { None, Weapon, Grip, Hands, Fingers, Palm };

// sin 5 degrees: a line steeper than this (its part on the ground shorter) has no heading the game can take (d2r-pcvr's dead zone)
constexpr float kSteepSin = 0.0871557f;
// both hands on a staff: a drawn shaft shorter than this (metres, butt to tip) gives no line - the carrying hand's grip aims
constexpr float kHandsMinM = 0.05f;
// the fingers count as level while |their up part| is below this - HandRay's own rule for the knuckles (-Y) against -Z
constexpr float kLevelY = 0.7f;

struct In {
    bool caster = false;             // a staff, wand or orb is the weapon in hand
    // [weight] carries it: the grip's shaft toward the tip, in the head's turn-only frame (x right, y up, z back), and the controller it starts at
    bool weightCarries = false;
    float gripDir[3] = {0, 0, -1}, gripFrom[3] = {};
    // [weight]: both hands hold it, both tracked - and the staff between them as the weight model draws it: its butt and its tip (same frame)
    bool bothHands = false;
    bool shaftOk = false;
    float shaftButt[3] = {}, shaftTip[3] = {};
    // the weapon as the skeleton hook drew it: its attach bone's origin and +Y (head frame, metres from the eyes)
    bool weaponOk = false;
    float weaponOrigin[3] = {}, weaponTip[3] = {0, 1, 0};
    // the casting hand as drawn: the palm's middle, wrist -> middle knuckle, out of the palm (same frame)
    bool handOk = false;
    float palm[3] = {}, fingers[3] = {0, 0, -1}, face[3] = {0, 0, -1};
    // [hands] staff_upright_arm: the caster is a staff, the blend is on (its two angles off vertical: vr/staff_upright.h), and the shoulder of the
    // hand above as drawn (skel::ArmReach, same frame) - the arm runs from it to `palm`
    bool staff = false;
    bool uprightArm = false;
    float uprightFullDeg = staffupright::kFullDeg, uprightNoneDeg = staffupright::kNoneDeg;
    bool shoulderOk = false;
    float shoulder[3] = {};
};

struct Out {
    Src src = Src::None;
    float origin[3] = {}, dir[3] = {0, 0, -1};   // head frame; dir unit
    const char* why = "";
    // [hands] staff_upright_arm: the arm's share of dir (0 = the staff's own line alone), and the arm it came from (shoulder -> palm, head frame)
    float armW = 0.0f;
    float shoulder[3] = {}, armHand[3] = {};
};

inline float Horizontal(const float d[3]) { return std::sqrt(d[0] * d[0] + d[2] * d[2]); }

// d as a unit vector into out; false when it is not a direction or lies within kSteepSin of vertical.
inline bool Usable(const float d[3], float out[3]) {
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!std::isfinite(l) || l < 1e-6f) return false;
    for (int i = 0; i < 3; ++i) out[i] = d[i] / l;
    return Horizontal(out) >= kSteepSin;
}

inline bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

inline Out Choose(const In& in) {
    Out o;
    auto take = [&](Src s, const float from[3], const float d[3], const char* why) {
        if (!Finite3(from) || !Usable(d, o.dir)) return false;
        o.src = s;
        for (int i = 0; i < 3; ++i) o.origin[i] = from[i];
        o.why = why;
        return true;
    };
    // [hands] staff_upright_arm: a staff's line d, steered toward the arm as it stands upright (vr/staff_upright.h); d itself when that is off or has no arm
    float steered[3], armW = 0.0f;
    auto byArm = [&](const float d[3]) -> const float* {
        armW = 0.0f;
        float arm[3];
        if (!in.staff || !in.uprightArm || !in.shoulderOk || !in.handOk || !Finite3(in.shoulder) || !Finite3(in.palm) ||
            !staffupright::Arm(in.shoulder, in.palm, arm)) return d;
        const float w = staffupright::ArmWeight(d, in.uprightFullDeg, in.uprightNoneDeg);
        if (w <= 0.0f || !staffupright::Blend(d, arm, w, steered)) return d;
        armW = w;
        return steered;
    };
    auto takeStaff = [&](Src s, const float from[3], const float d[3], const char* why, const char* whyArm) {
        const float* line = byArm(d);
        if (!take(s, from, line, armW > 0.0f ? whyArm : why)) return false;
        o.armW = armW;
        if (armW > 0.0f) for (int i = 0; i < 3; ++i) { o.shoulder[i] = in.shoulder[i]; o.armHand[i] = in.palm[i]; }
        return true;
    };
    if (in.caster) {   // (upright, or not posed lately: the hand that holds it aims)
        if (in.weightCarries && in.bothHands && in.shaftOk) {   // the staff as drawn between the two grips, butt -> tip, from its tip
            float d[3];
            for (int i = 0; i < 3; ++i) d[i] = in.shaftTip[i] - in.shaftButt[i];
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (Finite3(in.shaftButt) && l >= kHandsMinM && takeStaff(Src::Hands, in.shaftTip, d, "both hands on it: the staff as drawn between the grips, butt to tip",
                                                                      "both hands on it, upright: along the arm, blended with the staff between the grips")) return o;
        }
        if (in.weightCarries) {
            if (takeStaff(Src::Grip, in.gripFrom, in.gripDir, "[weight] carries it: its shaft toward the tip as the grip holds it",
                          "[weight] carries it, upright: along the arm, blended with the shaft as the grip holds it")) return o;
        } else if (in.weaponOk && takeStaff(Src::Weapon, in.weaponOrigin, in.weaponTip, "the weapon's axis as drawn (its bone's +Y)",
                                            "upright: along the arm (shoulder to palm), blended with the weapon's axis as drawn")) {
            return o;
        }
    }
    if (in.handOk) {
        const bool level = std::fabs(in.fingers[1]) < kLevelY;
        if (level && take(Src::Fingers, in.palm, in.fingers, in.caster ? "the hand's fingers as drawn (the weapon gave none)" : "the hand's fingers as drawn")) return o;
        if (take(Src::Palm, in.palm, in.face, in.caster ? "out of the palm as drawn (the weapon gave none)" : "out of the palm as drawn")) return o;
        if (!level && take(Src::Fingers, in.palm, in.fingers, "the hand's fingers as drawn (the palm faces up or down)")) return o;
        o.why = in.caster ? "the weapon and the hand both point straight up or down" : "the hand points straight up or down and the palm faces up or down";
        return o;
    }
    o.why = in.caster ? "neither the weapon nor the casting hand posed lately" : "the casting hand not posed lately";
    return o;
}

inline const char* Name(Src s) {
    switch (s) {
        case Src::Weapon: return "the weapon as drawn";
        case Src::Grip: return "the weapon's grip ([weight])";
        case Src::Hands: return "the staff as drawn between both hands ([weight])";
        case Src::Fingers: return "the drawn hand's fingers";
        case Src::Palm: return "the drawn palm";
        default: return "the controller (the old aim)";
    }
}

// What the hero holds, for how far out the cast's point goes (HookAttackPoint): a staff (its own two ranges), a wand or orb, or bare hands.
enum class Hold { Hand, OneHandCaster, Staff };
// The cells for that hold: staffCells is the staff's range for the hands on it now (staff_range_one / _two). Below kMinCells the caller keeps
// the game's point (aimpoint::Point refuses it).
inline float Cells(Hold h, float handCells, float casterCells, float staffCells) {
    const float c = h == Hold::Staff ? staffCells : h == Hold::OneHandCaster ? casterCells : handCells;
    return std::isfinite(c) ? c : 0.0f;
}

}  // namespace spellmodel
