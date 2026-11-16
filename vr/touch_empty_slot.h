// touch_empty_slot.h - [gameplay] melee_touch_empty_attack: a hit by touch whose attack slot (A) holds no skill is read by the game as its plain Attack;
// [gameplay] melee_touch_interact_attack: the same for a command (Interact, Loot ...) on A.
// Pure (no game calls, no globals); unit test cleanroom/tests/touch_empty_slot_test.cpp.
//
// Why (a player's log, 0.215.0, a Druid with a short sword): after a skill point was spent the game left the controller's A slot EMPTY (SlotSkill 0 = -1).
// Every touch pressed A, the game's slot selection read -1 and cast nothing ("the game read the slot but the hero never went into an attack mode",
// presses=N hits=0) - the monsters fell over from our weight physics, but took no damage. Serg: "if the touch has nothing bound it has to strike with
// the normal attack, so people are not confused".
//
// How: vrcam's slot-skill hook (the one [hands] throw_auto_skill and [gameplay] melee_skill_armed use) answers the game's two reads of the attack slot
// (the cast's slot selection, the unit's hand-skill setter) with Attack (0) instead of the empty binding, while the touch's own press is young (its
// target window, F4 only). The game then does exactly what it does for a slot that holds Attack - its own cast, checks, animation, reach, rolls and
// commands; on a joined game the host decides as ever. No server function is called. Any other read of the slot (the interface drawing it), any other
// slot, a slot that holds a skill, and every press that is not the touch's are answered as they are.
//
// [gameplay] melee_touch_interact_attack (the same way, its own key): a COMMAND on A instead of a skill. A player's log (himbrat, 0.218.3, a Warlock with a
// trident, 2026-11-16): the A slot held 357 "Interact" (the controller's pick-up / talk command - the skill wheel offers it first). Every touch pressed A,
// the game ran Interact on the monster: no attack, not one hit roll in four minutes ("0 hits landed"), the monsters only knocked about by our weight
// physics; and Stand Still was not held for the touch either ("a pick-up, not an attack"). The touch's own press now reads such a slot as Attack too - the
// cast's two reads in the hook, and Stand Still's view of the press. A command is Interact, Loot, the emote wheel, Swap Weapons, the map, Show Items, Run
// (357 .. 364) and Cube Loot (370): none of them is an attack, so a blade on a monster pressing one is never what the player meant. The player's own A
// press (a pick-up) is not the touch's and stays Interact.
#pragma once

namespace touchemptyslot {

inline constexpr int kAttack = 0;

// The controller's commands that sit in the skill table's ids but attack nothing (wheel_game.cpp GuessKey names them: SkillNameInteract .. RunToggle, CubeLoot).
inline bool IsCommand(int id) { return (id >= 357 && id <= 364) || id == 370; }

// The skill the hook answers a read of `slot` with when the game has `bound` there. `atCastSite`: the read is one of the cast's two (selection / setter);
// `emptyOn`: [gameplay] melee_touch_empty_attack, `commandOn`: [gameplay] melee_touch_interact_attack (each with hit by touch on); `touchPressYoung`: the
// touch's attack press is in its target window (F4).
inline int AnswerTouch(int bound, int slot, bool atCastSite, bool emptyOn, bool commandOn, bool touchPressYoung) {
    if (slot != 0 || !atCastSite || !touchPressYoung) return bound;
    if (bound < 0) return emptyOn ? kAttack : bound;
    return commandOn && IsCommand(bound) ? kAttack : bound;
}
// (the empty slot alone - as it was before the command case)
inline int Answer(int bound, int slot, bool atCastSite, bool enabled, bool touchPressYoung) {
    return AnswerTouch(bound, slot, atCastSite, enabled, false, touchPressYoung);
}

// Stand Still's view of the touch's press (vr/stand_still_wire.cpp): the A slot's skill as the game will read it for that press - a command there is the
// plain Attack (held still like any attack), not the pick-up it would be for the player's own A. `touch`: the press is the touch's.
inline int StillSkill(int bound, bool touch, bool commandOn) { return touch && commandOn && IsCommand(bound) ? kAttack : bound; }

// One log line per change of the slot, not per hit: told once the fallback first answers, told again when the slot held something it was not answered for
// (a skill, Attack) or the binding that fell back changed (empty -> Interact).
class Teller {
public:
    // A cast-site read of the attack slot: `bound` the game's binding, `fellBack` whether the answer gave Attack for it. True: tell it now.
    bool Read(int bound, bool fellBack) {
        if (!fellBack) {
            if (bound >= 0 && !IsCommand(bound)) told_ = false;   // (an empty slot or a command answered as itself - the option off, not the touch's press - changes nothing)
            return false;
        }
        if (told_ && bound == toldBound_) return false;
        told_ = true;
        toldBound_ = bound;
        return true;
    }

private:
    bool told_ = false;
    int toldBound_ = -1;
};

}  // namespace touchemptyslot
