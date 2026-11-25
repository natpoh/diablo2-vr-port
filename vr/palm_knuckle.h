// palm_knuckle.h - [hands] palm_from_knuckle: the palm's middle measured to the middle finger's KNUCKLE, as the rig's own bones tell it. Pure: no game, no skeleton;
// skeletons.cpp asks it where a hand is placed by its palm, cleanroom/tests/palm_knuckle_test.cpp checks it on the game's eight hero rigs.
//
// Idea and numbers: Berk (bearstonem), D2R VR Unleashed, commit 12bd172 "Hands on the controllers: the palm's middle on the grip" (2026-11-25,
// https://github.com/bearstonem/d2r-pcvr/commit/12bd172) - thank you. Written here anew for our hook (no code of his taken).
//
// What was wrong. Every hand the hook places by its palm - on the controller's grip point (the wrist half a hand behind it), on a weapon's shaft
// ([weight] grip_fit, the grip library), on a held body, on the crossbow's fore-grip - takes the palm's middle as half way from the wrist to the
// middle finger's FIRST bone (Hero::mid). On five of the game's hero rigs that bone is the knuckle. On the Amazon's, the Paladin's and the Druid's
// (R_mid_finger_01_bind_jnt .. _04, four bones a finger - vr/finger_chain.h) it is a bone INSIDE the palm, half way to the knuckle (_02). Read off
// data/hd/character/player/<class>/skeleton (2026-11-25, eyes 1.634 m): their palm's middle came out 2.04 / 2.18 / 2.38 cm before the wrist instead
// of 4.23 / 4.31 / 4.42, so the hand's real palm's middle sat 2.22 / 2.14 / 2.04 cm off the controller's grip (2.19 / 2.13 / 2.03 of it along the hand,
// toward the fingertips; a held shaft lay that much toward the heel of the hand). The other five: 0.
//
// The rule, from the rig itself: the palm's middle is half way from the wrist to the middle finger's knuckle - the bone fingerchain::FirstBend names
// for the chain (the second of a four-bone finger, else the first). The hand's own axes (where it points, Hero::mid; the knuckle line) are not
// changed: only the point it is placed by.
#pragma once

#include "finger_chain.h"

#include <cmath>

namespace palmknuckle {

// Which bone of the middle finger's chain (0 = its first, the one the chain starts at under the wrist) the palm's middle is measured to.
inline int KnuckleIndex(int bones, bool namedMetacarpal) { return fingerchain::FirstBend(bones, false, namedMetacarpal, true); }

struct V { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Scale(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V Norm(V a) { const float l = std::sqrt(Dot(a, a)); return l > 1e-12f ? Scale(a, 1.0f / l) : V{}; }

// The wrist -> palm's-middle vector once the hand is turned as skeletons.cpp ApplyWrist turns it: the frame (point, along made square to it, their
// cross) of the hand as it stood is laid on (point2, along2 made square, their cross). palm: wrist -> palm's middle in the hand as it stood (half the
// wrist -> knuckle vector). The hand bone's handedness drops out: ApplyWrist lays a mirrored hand's third axis on det x (point2 x along2) and the
// same det turns up in the third axis of the hand as it stood, so the third coordinate is taken against point x along on both sides.
inline V Turned(V palm, V point, V along, V point2, V along2) {
    const V a1 = Norm(point), a2 = Norm(Sub(along, Scale(a1, Dot(along, a1)))), a3 = Cross(a1, a2);
    const V b1 = Norm(point2), b2 = Norm(Sub(along2, Scale(b1, Dot(along2, b1)))), b3 = Cross(b1, b2);
    return Add(Add(Scale(b1, Dot(palm, a1)), Scale(b2, Dot(palm, a2))), Scale(b3, Dot(palm, a3)));
}

}  // namespace palmknuckle
