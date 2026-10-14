// thrown_touch.h - hit by touch with a THROWN weapon ([gameplay] melee_touch_thrown): the rules that keep the touch and the throw by
// gesture ([hands] throw_by_grip / throw_gesture, vr/throw_gesture_wire.h) from both firing for one swing. Pure: no game calls, no globals.
//
// A javelin, throwing knife or throwing axe in the hand can be used two ways. The throw by gesture: the grip is the weapon "in the hand",
// the hand swings and the grip is let go at the end of the swing - the game's own throw, flung along the hand's way. The touch: the blade
// passes through a monster and the game's own attack is pressed at it - a point-blank throw (the game's Attack on a thrown weapon; with a
// monster at the point it is allowed, see throw_cast.h). A swing that is a throw must not also count as a touch, and a touch must not be
// followed by the gesture's own throw of the same swing:
//   - after a throw by gesture was released, the touch is held off for kAfterThrowMs (the follow-through of the swing, the weapon leaving
//     the hand, the game's throw animation);
//   - with throw_by_grip on (the grip gesture OWNS the right grip: GripIsThrows) the weapon is "in the hand" only while that grip is
//     squeezed, so the touch counts only then - an open hand with a javelin in it pokes nothing. The squeeze is the GESTURE's reading of
//     the controller (throwwire::GripHeld) - never the shared block's grip: a control the gesture Takes reads as let go there, so with a
//     thrown weapon in hand that grip is always 0 (the first version asked it and the touch never started for a javelin, 2026-10-14);
//   - after the touch pressed the attack with a thrown weapon, a release of the gesture is dropped for kHoldOffMs: one throw per swing,
//     at the monster the blade met. A throw at nothing near (no touch fired) is the gesture's, exactly as it always was.
#pragma once

#include <cstdint>

namespace meleetouch {

constexpr uint64_t kAfterThrowMs = 700;   // ms after a released throw: the touch stands down
constexpr uint64_t kHoldOffMs = 800;      // ms after a touch with a thrown weapon: the gesture's release is dropped

struct ThrownState {
    bool gripOwns = false;       // throwwire::GripIsThrows(): throw_by_grip is on and a thrown weapon is in hand
    bool gripHeld = true;        // throwwire::GripHeld(): the right grip is squeezed on the weapon (read off the controller, not the shared block)
    uint64_t nowMs = 0;          // GetTickCount64
    uint64_t lastThrowMs = 0;    // throwwire::LastThrowTickMs(): the last released throw, 0 none
};

// Why a touch may not fire with the thrown weapon in hand right now.
enum class ThrownWhy : uint8_t { None, AfterThrow, GripOpen };
inline ThrownWhy ThrownWhyNot(const ThrownState& s) {
    if (s.lastThrowMs && s.nowMs >= s.lastThrowMs && s.nowMs - s.lastThrowMs < kAfterThrowMs) return ThrownWhy::AfterThrow;
    if (s.gripOwns && !s.gripHeld) return ThrownWhy::GripOpen;
    return ThrownWhy::None;
}
// May a touch fire with the thrown weapon in hand right now?
inline bool ThrownTouchAllowed(const ThrownState& s) { return ThrownWhyNot(s) == ThrownWhy::None; }

// Until when the gesture's release is dropped, after a touch pressed the attack at nowMs.
inline uint64_t HoldOffUntil(uint64_t nowMs) { return nowMs + kHoldOffMs; }

}  // namespace meleetouch
