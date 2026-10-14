// weight_floor.h - where the floor is for a weapon that falls ([weight]). Pure.
//
// The weight model lives in the room (OpenXR stage axes, metres) and let-go weapons fall to a floor height in it. That floor must be the GROUND
// the player sees: the hero's ground in the game's world, under his feet - not the room's floor, which is where the player's own feet are. The two
// differ by however the player's real height or posture differs from the hero's eye height in the game (seated, crouching, a tall or a short
// player, a recentre made in another posture): a staff dropped by a seated player lay a metre over the visible ground, and 30 cm over it for a
// player 30 cm shorter than the setting says (2026-10-14).
//
// The game camera's eye stands ViewHeight() world units over the hero's ground (plus a jump's lift, plus - with native OpenXR - the head's own travel from
// the recentre), and the world is scaled so that the user's eye height is that many units: upm = ViewHeight / eyeHeightM. So the head is
// (eye.y - ground.y) / upm metres over the visible ground, whatever the room says, and the ground in the room is that far below the head:
//     floorRoomY = headRoomY - HeadAboveGroundM(...)
// Units: the eye and the ground in world units, upm world units per metre, the rest metres.
#pragma once

#include <cmath>

namespace weightfloor {

constexpr float kMinAboveM = 0.25f, kMaxAboveM = 4.0f;   // a head closer to the ground or further from it than this is a bad reading

// Metres from the ground to the head. fallbackM (the user's standing eye height) when a number is not usable: the unknown eye, a scale that
// is not one, a height that is no height.
inline float HeadAboveGroundM(float eyeWorldY, float groundWorldY, float unitsPerMetre, float fallbackM) {
    if (!std::isfinite(eyeWorldY) || !std::isfinite(groundWorldY) || !std::isfinite(unitsPerMetre) || !(unitsPerMetre > 0.5f) ||
        !(unitsPerMetre < 50.0f))
        return fallbackM;
    const float m = (eyeWorldY - groundWorldY) / unitsPerMetre;
    return m >= kMinAboveM && m <= kMaxAboveM ? m : fallbackM;
}

// The floor in the room: headRoomY is the head's height in the room's axes (the room's floor is 0 there). aboveGroundM <= 0 or not finite: the
// room's own floor (0) - the way it was before.
inline double RoomFloorY(double headRoomY, double aboveGroundM) {
    if (!std::isfinite(headRoomY) || !std::isfinite(aboveGroundM) || !(aboveGroundM > 0.0)) return 0.0;
    return headRoomY - aboveGroundM;
}

}  // namespace weightfloor
