// room_pose.h - a hand as the shared block carries it, back in the room (tracking) frame. Pure maths.
//
// The shared block (shared/d2r_vr_shared.h, made by XrIntoShared / Game Link) has each hand as metres from the head in the
// frame of the head's turn alone (x right, y up, z back; yaw + turned left), the rotation with that turn taken off. A stroke's
// speed is measured in the room instead, so the world turning under a still hand, or the hero walking, cannot power a hit.
#pragma once

#include <cmath>

#include "stroke_contact.h"

namespace meleetouch {

inline stroke::Pose RoomPose(const float headRoom[3], float yawRad, const float pos[3], const float rot[4]) {
    const float c = std::cos(yawRad), s = std::sin(yawRad);
    stroke::Pose p;
    p.p = {headRoom[0] + pos[0] * c + pos[2] * s, headRoom[1] + pos[1], headRoom[2] - pos[0] * s + pos[2] * c};
    p.q = stroke::Multiply(stroke::Quat{0.0f, std::sin(yawRad * 0.5f), 0.0f, std::cos(yawRad * 0.5f)}, stroke::Quat{rot[0], rot[1], rot[2], rot[3]});
    return p;
}

}  // namespace meleetouch
