#pragma once
// [body] under_camera (VR F4): the drawn body kept under the camera through a leap and while the game's camera trails the hero.
//
// F4's camera stands on the game camera's look-at - the hero's point on the ground - with the hero's eyes over it; the renderer draws the hero
// by his own model matrix. The two part two ways:
//  - up: the game lifts the model matrix itself on a Leap's arc. The author's log (2026-10-26, PS VR2, 20 leaps): "model matrix 12.55 above the
//    look-at" - 12 to 13.7 units (~3.5 m) at the top of every Leap and Leap Attack, the look-at on the ground, the hips only 0.75 over standing.
//    [camera] follow_jump takes the hips alone, so the camera stayed on the ground and the body flew up out from under it ("the model jumps
//    out from under the camera"). 0.182's fix, across the floor only, never came into it there.
//  - across the floor: with the controller settings' cameraSmoothingEnabled the game's follow (D2Prism CameraTranslation::RenderTick
//    0x7BD350) pulls the look-at to the hero on a damped spring (cameraSpringConstant, cameraSpringDamping, cameraMass, the lag held to
//    cameraSpringMaxDistance, a jump past cameraJumpDistance taken at once) - a fast throw leaves the camera behind him.
// So the drawn body's origin is put where the camera stands on: across the floor the look-at, up the camera's own lift - by default the
// camera stays on the ground and the body is pulled down to it; with [body] leap_camera_up the camera rises with the game's arc (Lift: the
// matrix's height over the look-at, eased in like the hips' jump_from). The move is in the hero's model units (vrcam moves the drawn bones by it, as [openxr]
// roomscale_body; the eyes are taken against the new origin). Drawn bones only: where he stands, his hit box, his path and every game state
// stay the game's. Off: as before. Pure: no game reads (under_camera_test).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace undercam {

// World units (2 a grid cell). Apart further than this across the floor or up, it is no trail and no leap: an area change, a waypoint
// (cameraJumpDistance takes those at once), a wrong read - nothing is moved.
constexpr float kMaxGap = 40.0f;
// Under this height of the matrix over the look-at nothing follows (walking they agree: the "jump" diagnostic never fired off a leap in the
// author's logs); from there to twice that it eases in to the whole lift, as [camera] jump_from does for the hips - no step.
constexpr float kLiftFrom = 0.3f;

// How far apart the hero's model matrix `m` (row vectors, p' = p * M, the translation in m[12..14]) and the camera's look-at are: across
// the floor (`gap`) and how high the matrix stands over it (`lift`, + up), world units. False: not numbers, or apart past kMaxGap.
inline bool Measure(const float m[16], const float look[3], float* gap, float* lift) {
    const float dx = look[0] - m[12], dz = look[2] - m[14];
    *gap = std::sqrt(dx * dx + dz * dz);
    *lift = m[13] - look[1];
    return std::isfinite(*gap) && std::isfinite(*lift) && *gap <= kMaxGap && std::fabs(*lift) <= kMaxGap;
}

// The part of the matrix's lift the camera rises by (world units; [body] leap_camera_up): none under kLiftFrom, the whole from twice it.
// A lift down (the matrix under the look-at) is not a jump: none.
inline float Lift(float lift) {
    if (!(lift > kLiftFrom)) return 0.0f;
    const float t = std::clamp((lift - kLiftFrom) / kLiftFrom, 0.0f, 1.0f);
    return lift * t * t * (3.0f - 2.0f * t);
}

// The move, in the hero's model units (x, y, z), that draws his model's origin at `to` (world) instead of at m's translation: the world move
// dotted with each row over its squared length (the rows carry the model's scale; turned, scaled or mirrored alike). False: not numbers, a
// matrix with no turn in it.
inline bool MoveInModel(const float m[16], const float to[3], float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    const float d[3] = {to[0] - m[12], to[1] - m[13], to[2] - m[14]};
    for (int r = 0; r < 3; ++r) {
        const float* row = m + r * 4;
        const float l2 = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];
        if (!(l2 > 1e-8f)) { out[0] = out[1] = out[2] = 0.0f; return false; }
        out[r] = (d[0] * row[0] + d[1] * row[1] + d[2] * row[2]) / l2;
    }
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) { out[0] = out[1] = out[2] = 0.0f; return false; }
    return true;
}

// Where the drawn body's origin goes: the look-at across the floor, and up by the lift the camera rises by in the view this pose is drawn
// with (`cameraUp`, world: the skeleton poses before its pass rebuilds the view, so it is this pose's own Lift - 0 without leap_camera_up, so
// the body stays down with the camera).
inline void Target(const float look[3], float cameraUp, float to[3]) {
    to[0] = look[0];
    to[1] = look[1] + cameraUp;
    to[2] = look[2];
}
// What the eyes are taken against at the pose (vrcam's eye in model space). The skeleton works the pose out in his model space with the
// move across the floor already in the bones and the move up added only at the end (skeletons.cpp t_poseUp): the hands' targets are
// that space's, so across the floor the eye is taken against his own place (`m`'s translation - against the look-at the hands landed
// off the controllers by the gap), and up against the body's new ground, so that its height there is over the feet (the hands' scale,
// the arm span read it so). The eye at the pose is put together from the look-at now and the last view's eye over its look-at, which
// carries the last view's lift (`lastViewUp`): against the look-at up by that, it comes out as drawn - (look + lastEye + (cameraUp -
// lastViewUp)) - (look + cameraUp).
inline void EyesOrigin(const float m[16], const float look[3], float lastViewUp, float at[3]) {
    at[0] = m[12];
    at[1] = look[1] + lastViewUp;
    at[2] = m[14];
}

// For the log: one line per leap or trail, not one a frame. It starts with the matrix over kTellGap from the look-at (up or across the floor)
// and ends once both are back under half of that; the end tells the largest of each, how long, how many poses moved the body and, of those
// that did not, the last reason.
constexpr float kTellGap = 1.0f;   // world units (~28 cm at a man's height)
struct Trail {
    bool on = false;
    float gap = 0.0f, lift = 0.0f;
    uint64_t since = 0;
    int moved = 0, left = 0;
    const char* why = nullptr;   // the last reason a pose was left as it was (a static string)
};
struct TrailEnd {
    float gap = 0.0f, lift = 0.0f;   // the largest across the floor, the highest up (world units)
    uint64_t ms = 0;
    int moved = 0, left = 0;
    const char* why = nullptr;
};
// One pose: its gap and lift (Measure), whether the body was moved, and if not why (nullptr: nothing to move). True when a leap or trail has
// just ended: *end is what it was.
inline bool Step(Trail& t, float gap, float lift, bool moved, const char* why, uint64_t now, TrailEnd* end) {
    if (!std::isfinite(gap) || !std::isfinite(lift)) return false;
    const float size = std::max(gap, std::fabs(lift));
    if (!t.on) {
        if (!(size > kTellGap)) return false;
        t = Trail{};
        t.on = true;
        t.since = now;
    }
    t.gap = std::max(t.gap, gap);
    t.lift = std::max(t.lift, lift);
    if (moved) ++t.moved;
    else { ++t.left; if (why) t.why = why; }
    if (size >= 0.5f * kTellGap) return false;
    t.on = false;
    end->gap = t.gap; end->lift = t.lift; end->ms = now >= t.since ? now - t.since : 0;
    end->moved = t.moved; end->left = t.left; end->why = t.why;
    return true;
}

}  // namespace undercam
