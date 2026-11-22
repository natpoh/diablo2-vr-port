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
//     [gameplay] melee_touch_thrown_held (off by default) takes this one away: the weapon is in the hand from the moment it is equipped,
//     like a sword - the weight model's own word (a hand holds it, vr/weight_drive.h) decides, an open grip does not;
//   - after the touch pressed the attack with a thrown weapon, a release of the gesture is dropped for kHoldOffMs: one throw per swing,
//     at the monster the blade met. A throw at nothing near (no touch fired) is the gesture's, exactly as it always was.
// What the touch's press IS for the game - a blow or a throw - is the skill it carries: see MeleeAnswer below ([gameplay] melee_touch_thrown_melee).
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
    bool heldAtOnce = false;     // [gameplay] melee_touch_thrown_held: in the hand from the equip, like a sword - an open grip does not refuse
};

// Why a touch may not fire with the thrown weapon in hand right now.
enum class ThrownWhy : uint8_t { None, AfterThrow, GripOpen };
inline ThrownWhy ThrownWhyNot(const ThrownState& s) {
    if (s.lastThrowMs && s.nowMs >= s.lastThrowMs && s.nowMs - s.lastThrowMs < kAfterThrowMs) return ThrownWhy::AfterThrow;
    if (s.gripOwns && !s.gripHeld && !s.heldAtOnce) return ThrownWhy::GripOpen;
    return ThrownWhy::None;
}
// May a touch fire with the thrown weapon in hand right now?
inline bool ThrownTouchAllowed(const ThrownState& s) { return ThrownWhyNot(s) == ThrownWhy::None; }

// Until when the gesture's release is dropped, after a touch pressed the attack at nowMs.
inline uint64_t HoldOffUntil(uint64_t nowMs) { return nowMs + kHoldOffMs; }

// ---- [gameplay] melee_touch_thrown_melee: the touch with a thrown weapon is the game's one-handed BLOW, not a throw ----
// How the game itself picks a blow or a throw with a javelin, a throwing knife or axe in hand (read in the game: skills.txt of 3.3 and the server's
// Attack 0x42FEB0): by the SKILL the press carries, never by the distance.
//   - Attack (skill 0, anim A1, range "both", srvdofunc 1): the server's Attack makes a missile only for a weapon of class bow or crossbow
//     (0x3B7490: weapon class 1 / 7 -> Shoot 0x4333F0); any other weapon - a javelin is class 1ht, a throwing axe 1hs - is the melee blow: the target,
//     the melee range check (Range 0x348650 inside 0x44B720), the hit roll, the weapon's ONE-HAND damage (weapons.txt mindam / maxdam, not the
//     missile's minmisdam / maxmisdam), attack mode 7 / 8. So a plain Attack with a javelin is already its one-handed stab.
//   - Throw (skill 2, anim TH, mode 11, srvdofunc 3), Left Hand Throw (4), and the ranged javelin skills (Poison Javelin, Lightning Bolt, Plague
//     Javelin, Lightning Fury, Double Throw - skills.txt range "rng") throw it.
//   - The melee skills a thrown weapon allows (Jab, Power Strike, Impale, Charged Strike, Fend, Lightning Strike with a javelin - it counts as a spear;
//     Poison Dagger with a throwing knife; Bash, Zeal ... with any of them - a combo weapon counts as a melee weapon) are blows too.
// The touch presses the pad's A (the attack slot); the game reads that slot's skill twice at the press (vr/input/spell_aim.cpp HookSlotSkill). What it
// carries is the player's A binding, or what the javelin quiver / a live throw by gesture answered for it - so a touch is a throw only when A holds
// Throw or a ranged skill, or the quiver chose one. With this switch the touch's own press is answered as the plain Attack whenever the skill would
// throw: the game then strikes with the weapon in hand, its own blow (its reach, rolls, damage and animation). A blow skill stays what it is.
inline constexpr int32_t kSkillAttack = 0;
inline bool ThrowsTheWeapon(int32_t skill) {
    switch (skill) {
        case 2:     // Throw
        case 4:     // Left Hand Throw
        case 15:    // Poison Javelin
        case 20:    // Lightning Bolt
        case 25:    // Plague Javelin
        case 35:    // Lightning Fury
        case 140:   // Double Throw
            return true;
        default: return false;
    }
}
struct MeleeAsk {
    bool on = false;            // [gameplay] melee_touch_thrown_melee
    bool touchPress = false;    // the read belongs to the touch's own press (meleetouch::TargetPending: young, F4)
    bool atCastSite = false;    // one of the game's two reads that pick the cast's skill (the slot selection's, the hand-skill setter's)
    bool attackSlot = false;    // the slot read is the attack slot (A, slot 0) - the one the touch presses
    bool thrownInHand = false;  // a javelin, throwing knife or axe is the weapon in hand (a potion is never swung)
    bool throwLive = false;     // a throw by gesture is live (its press, not the touch's)
};
// What the game's read answers: `answered` (what the slot and the other hooks made of it), or the plain Attack when that would throw the weapon.
inline int32_t MeleeAnswer(int32_t answered, const MeleeAsk& a) {
    if (!a.on || !a.touchPress || !a.atCastSite || !a.attackSlot || !a.thrownInHand || a.throwLive) return answered;
    return ThrowsTheWeapon(answered) ? kSkillAttack : answered;
}

}  // namespace meleetouch
