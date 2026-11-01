// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 3d83a31ef88f58f77c7d27806ac1d9c29ac8baa7, its steamvr/shapeshift_form.h, taken into D2R VR Authentic
// for [shapeshift] forms_first_person: the namespace, the layout and the state read (two flags here, not Unleashed's effects::Status)
// changed, PlaceShoulders added (our body is not hung from a tracked neck: the grafted shoulders are put under the camera instead).
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

// Druid shapeshift forms in first person. The game draws the forms with enemy
// models that carry only claws, no weapon or shield bones (F10 capture
// 2026-10-21: data/hd/character/enemy/wolf/skeleton/wolf.skeleton, 111 bones;
// .../enemy/bear/skeleton/bear.skeleton, 93 bones). The form's body is grafted
// under the player: its torso held upright, its shoulders where a person's sit
// under the eyes, the whole rig scaled so its eyes stand at the form's own
// height. Value-only, host-testable; vr/skeletons.cpp uses it.
namespace unleashed::shapeshift {
enum Form : int { None = 0, Wolf = 1, Bear = 2 };
inline constexpr int WerewolfState = 139, WerebearState = 140;
inline int FromStates(bool known, bool werewolf, bool werebear) {
    return !known ? None : werewolf ? Wolf : werebear ? Bear : None;
}
inline int FromModel(std::string_view model) {
    if (model.find("/character/enemy/wolf/") != std::string_view::npos) return Wolf;
    if (model.find("/character/enemy/bear/") != std::string_view::npos) return Bear;
    return None;
}
inline const char* Name(int form) { return form == Wolf ? "werewolf" : form == Bear ? "werebear" : "human"; }
// The form's eyes standing, in the DRUID's model units (her bind eyes are 7.07;
// the hook converts to the form rig's units, whose model is drawn at another
// world scale - 1.0 against the druid's 0.95): the werewolf at 98 % of the
// druid, the werebear at 112 % (Unleashed's tester, 2026-10-22).
inline float StandingEye(int form) { return form == Wolf ? 6.93f : form == Bear ? 7.92f : 0.0f; }
// The druid's model units in a form rig's own: her model-to-world scale over the form's.
inline float PersonUnits(float personWorldScale, float formWorldScale) {
    return personWorldScale > 0 && formWorldScale > 0 && std::isfinite(personWorldScale / formWorldScale)
               ? std::clamp(personWorldScale / formWorldScale, 0.5f, 2.0f)
               : 1.0f;
}
// [shapeshift] wolf_height / bear_height: a fraction of that, 0.5..1.5.
inline float TargetEye(int form, float fraction) { return StandingEye(form) * std::clamp(fraction, 0.5f, 1.5f); }
// A person's proportions under the eyes, from the druid's bind (eyes 7.07, neck
// joint 6.37, shoulder joints 5.97), as fractions of the eye height. The form's
// shoulders go where a person's are; its own neck and head are hidden.
inline constexpr float ShoulderRatio = 5.972f / 7.07f;
inline constexpr float NeckAboveShoulders = 0.394f / 7.07f;
inline constexpr float EyeAboveNeck = 0.704f / 7.07f;
inline constexpr float EyeAheadOfNeck = 0.03f;
// The druid's shoulder joints 1.715 apart under eyes at 7.07. A form's arms are
// fitted to the player's reach from a person's shoulders: the werebear's are
// 3.11 apart (rest pose), and fitting its arms to the T-pose span from those
// cut them to half their length, so a forward reach fell short of the hands.
inline constexpr float ShoulderWidthRatio = 1.715f / 7.07f;
inline float ArmFitShoulders(float own, float rigEye) {
    return rigEye > 0 && own > 0 ? std::min(own, ShoulderWidthRatio * rigEye) : own;
}
// The rig's eye height once its shoulders stand where a person's do: the eye
// the arm fit and the hands are placed with, in the rig's own units.
inline float RigEye(float uprightShoulders) { return uprightShoulders > 0 ? uprightShoulders / ShoulderRatio : 0.0f; }
// The torso tipped upright about its spine root (the lateral axis only): the
// shoulders' height then. pivot and shoulders are model-space y and z.
inline float UprightShoulders(float pivotY, float pivotZ, float shoulderY, float shoulderZ) {
    return pivotY + std::hypot(shoulderY - pivotY, shoulderZ - pivotZ);
}
// The tilt (radians, about the model's lateral x axis) that stands the shoulders straight above the spine root: v' = v * R with
// R = {1,0,0; 0,cos,sin; 0,-sin,cos} takes (0, dy, dz) onto (0, |dy dz|, 0). 0 for shoulders on the root.
inline float UprightTilt(float pivotY, float pivotZ, float shoulderY, float shoulderZ) {
    return std::hypot(shoulderY - pivotY, shoulderZ - pivotZ) > 0.1f ? std::atan2(-(shoulderZ - pivotZ), shoulderY - pivotY) : 0.0f;
}
// Where the upright shoulders' middle is moved to across the floor (model x, z): under a person's neck, the eyes at the model's
// origin (where the camera stands over the hero), so the form's torso stands under the camera as a person's does - its own
// spine root may lie far behind it (the werewolf's 0.79 units). D2R VR Authentic's: Unleashed hangs the body from the tracked neck.
inline void PlaceShoulders(float rigEye, float* x, float* z) {
    *x = 0.0f;
    *z = rigEye > 0 ? -EyeAheadOfNeck * rigEye : 0.0f;
}
// The uniform scale drawn on a rig so its eyes stand at `shown`.
inline float Scale(float shown, float rigEye) {
    return rigEye > 0 && shown > 0 && std::isfinite(shown / rigEye) ? std::clamp(shown / rigEye, 0.3f, 2.0f) : 1.0f;
}

// Which rig is the hero's while the game swaps the druid's model for a form's
// (or one form's for the other, or back). A rig the game has not drawn for
// RigGoneMs (~3 frames) is gone.
inline constexpr uint64_t RigGoneMs = 40;
inline bool RigDrawn(uint64_t now, uint64_t posedAt) { return posedAt && (now <= posedAt || now - posedAt <= RigGoneMs); }
inline bool RigGone(uint64_t now, uint64_t posedAt) { return posedAt && !RigDrawn(now, posedAt); }
// The rig drawn as the hero says which body the player is in: the most recently posed of the
// person's (0) and the forms' (1, 2), while one was posed within PosedMs; -1 when none is.
inline constexpr uint64_t PosedMs = 100, AheadMs = 1500;
inline int PosedRig(uint64_t now, const uint64_t at[3]) {
    int best = -1;
    uint64_t latest = 0;
    for (int i = 0; i < 3; ++i)
        if (at[i] > latest) { latest = at[i]; best = i; }
    return latest && (now <= latest || now - latest <= PosedMs) ? best : -1;
}
// Right after the rig it names stopped being drawn: a swap, not a corpse or a load long past.
inline bool RigJustGone(uint64_t now, uint64_t posedAt) { return RigGone(now, posedAt) && now - posedAt < AheadMs; }

// The eye height the camera uses. Taking a form within a session eases over
// EaseMs, so the world's scale doesn't jump. A person's rig (ease false) and a
// hero not posed for SnapMs take the new height at once: only a form's rig is
// ever drawn scaled.
class EyeEase {
    float from = 0, to = 0;
    uint64_t at = 0, seen = 0;
public:
    static constexpr uint64_t EaseMs = 500, SnapMs = 1000;
    float Value(uint64_t now) const {
        if (now <= at) return from;
        const float t = std::min(1.0f, float(now - at) / float(EaseMs));
        return from + (to - from) * t * t * (3 - 2 * t);
    }
    float Settled() const { return to; }
    float Step(float target, uint64_t now, bool ease = true) {
        if (!(target > 0) || !std::isfinite(target)) return Value(now);
        if (!ease || !seen || now < seen || now - seen > SnapMs) { from = to = target; at = now; }
        else if (std::fabs(target - to) > 1e-4f) { from = Value(now); to = target; at = now; }
        seen = now;
        return Value(now);
    }
};

// Standing or moving, from the unit's ground position (world units): while
// standing the form's hips, legs and tail are held still; while it walks or
// runs they follow the game's stride. Returns the held weight, 1 standing, 0
// moving, blended over BlendMs. A gap of more than a second starts over standing.
class LegHold {
    float x = 0, z = 0, w = 1;
    uint64_t at = 0, step = 0;
    bool known = false, moving = false;
public:
    static constexpr uint64_t WindowMs = 60, BlendMs = 150;
    static constexpr float MovingSpeed = 0.5f;   // world units per second
    float Step(uint64_t now, float px, float pz) {
        if (!std::isfinite(px) || !std::isfinite(pz)) return w;
        if (!known || now < step || now - step > 1000) {
            x = px; z = pz; at = step = now; known = true; moving = false; w = 1;
            return w;
        }
        if (now - at >= WindowMs) {
            moving = std::hypot(px - x, pz - z) * 1000.0f / float(now - at) > MovingSpeed;
            x = px; z = pz; at = now;
        }
        const float d = float(now - step) / float(BlendMs);
        step = now;
        w = moving ? std::max(0.0f, w - d) : std::min(1.0f, w + d);
        return w;
    }
};
}  // namespace unleashed::shapeshift
