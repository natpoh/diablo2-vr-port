// vr_body_pick.h - [input] vr_body_pick, vr_body_pick_hand: in VR a monster, an NPC, a door, a shrine, a chest, a portal is pointed at by its
// body, not only by its feet (GitHub issue natpoh/diablo2-vr-port#15, "Hitbox needs more vertical height"). Pure; the test is
// cleanroom/tests/vr_body_pick_test.cpp, the wiring vr/input/flat_click.cpp (HookMouseGround, VrHandTarget), vr/input/spell_aim.cpp and
// vr/input/actions.cpp (the controller's lookups).
//
// flat_body.h's capsules and pick, fed the ray each VR view aims along:
//   - The game's mouse point (0x7E7CE0, the hover and the click; F1 - F3 with the mouse, the crosshair of [input] vr_keys_walk, F4's
//     [bow] mode 2 shift + click along the hand): the ray the game's mouse code was handed this call, which d2rcam's ScreenToRay hook tells
//     (LastPick: its pixel and who answered) - our camera's matrices through the pixel (PickRay), or the aim's ray override (the hand's
//     ray, vrcam VrRay); the game's own camera answering (a view of its own): nothing of ours, its hover is made for that view.
//   - F4's pad (the controller's own targets, no mouse): the aiming hand's ray (vrcam HandRay) against the hero's floor as drawn. What it
//     meets first answers the game's target lookups - an attack's (the cast flags', the resolver's two, the map click's: a hostile,
//     unwalled monster by the server's own verdict, never an NPC) and interact's (the controller's interact target: an object, or an NPC).
//     Only where nothing of ours answers already: leap by gaze, shield smite, hit by touch, a directed cast (spell_aim / bow_aim: the
//     game's target off) keep theirs. The game decides the rest - reach, the walk up, the hit.
#pragma once

#include <cmath>
#include <cstdint>

#include "flat_body.h"
#include "flat_ground.h"

namespace vrbody {

// ---- the game's mouse point ------------------------------------------------------------------------------------------------------------

enum class Ray { None, Pixel, Aim };   // none; the camera's ray through the mouse's pixel (d2rcam PickRay); the aim's ray override

struct MouseAsk {
    int platform = 0;        // 0 flat, 1 VR
    int view = 0;            // VR: 1 = F1 ... 4 = F4 (viewgate::Number)
    bool flatKey = false;    // [input] flat_body_pick
    bool vrKey = false;      // [input] vr_body_pick
    bool menu = false;       // a panel or menu open: the pointer is the menu's
    bool pickSeen = false;   // d2rcam's LastPick is this call's (SamePick)
    int pickBy = -1;         // ... who answered it: 0 the game's own camera, 1 our matrices, 2 the aim's ray override
};

// Flat as before: the pixel through our camera (the hook asks only with our camera on). VR: the ray the game's mouse code was handed - in F4 only
// our own aim's (the pad plays there; the desktop's cursor, parked or pushed by the mouse, is no aim of the player's).
inline Ray MouseRay(const MouseAsk& a) {
    if (a.menu) return Ray::None;
    if (a.platform == 0) return a.flatKey ? Ray::Pixel : Ray::None;
    if (a.platform != 1 || !a.vrKey || !a.pickSeen) return Ray::None;
    if (a.pickBy == 2) return Ray::Aim;
    return a.pickBy == 1 && a.view >= 1 && a.view <= 3 ? Ray::Pixel : Ray::None;
}

// d2rcam's last pick is the one the game's mouse code just made: the same pixel, a moment ago.
inline bool SamePick(float px, float py, float pickPx, float pickPy, uint64_t ageMs) {
    return ageMs <= 100 && std::fabs(px - pickPx) < 0.5f && std::fabs(py - pickPy) < 0.5f;
}

// ---- F4: the controller's lookups --------------------------------------------------------------------------------------------------------

enum class Ask { Attack, Interact };
constexpr int kInteractSkill = 357;   // the controller's interact (vr/input/spell_aim.cpp kInteractSkill)
inline Ask AskOf(int skill) { return skill == kInteractSkill ? Ask::Interact : Ask::Attack; }

// (A cast that goes along the hand - spell_aim / bow_aim / throw_aim - never gets here: its lookups are answered none before ours is asked.)
struct HandAsk {
    bool f4 = false;    // viewgate::F4()
    bool key = false;   // [input] vr_body_pick_hand
    bool menu = false;
    bool on = false;    // the mod on (vrcam g_enabled)
};
inline bool HandWanted(const HandAsk& h) { return h.f4 && h.key && !h.menu && h.on; }

// What the capsule met first may answer for. verdict: the server's - 1 hostile and nothing between, 0 not (or a wall), -1 not known yet.
// An attack: a monster the server called hostile, never an NPC. Interact: an object, or an NPC the server does not call hostile.
inline bool Answers(Ask ask, const flatbody::Cap& c, int verdict) {
    if (ask == Ask::Interact) return !c.body || (c.npc && verdict != 1);
    return c.body && !c.npc && verdict == 1;
}

// The ground for a ray with no pixel (the hand's): the hero's floor as drawn, y = floorY; INFINITY when it never comes down to it.
inline float FloorT(const float o[3], const float d[3], float floorY) {
    float p[3];
    if (!flatground::PlaneHit(o, d, floorY, p)) return INFINITY;
    return (p[0] - o[0]) * d[0] + (p[1] - o[1]) * d[1] + (p[2] - o[2]) * d[2];
}

// The hand's pick: the first capsule along the ray before the floor, and whether it answers `ask`. *index: the capsule met (-1 none) - one
// met that does not answer still hides what stands behind it (pointing at a monster in front of a chest is not pointing at the chest).
// verdictOf(cap): the server's verdict on that one (asked only of the capsule met).
inline int HandMet(const flatbody::Cap* caps, int n, const float o[3], const float d[3], float floorY) {
    flatbody::Hit h;
    return flatbody::Pick(caps, n, o, d, FloorT(o, d, floorY), &h) ? h.index : -1;
}
template <class VerdictOf>
inline bool HandPick(const flatbody::Cap* caps, int n, const float o[3], const float d[3], float floorY, Ask ask, VerdictOf verdictOf, int* index) {
    *index = HandMet(caps, n, o, d, floorY);
    return *index >= 0 && Answers(ask, caps[*index], verdictOf(caps[*index]));
}

}  // namespace vrbody
