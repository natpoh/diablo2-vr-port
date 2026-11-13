// touch_empty_slot.h - [gameplay] melee_touch_empty_attack: a hit by touch whose attack slot (A) holds no skill is read by the game as its plain Attack.
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
#pragma once

namespace touchemptyslot {

inline constexpr int kAttack = 0;

// The skill the hook answers a read of `slot` with when the game has `bound` there. `atCastSite`: the read is one of the cast's two (selection / setter);
// `enabled`: [gameplay] melee_touch_empty_attack (and hit by touch on); `touchPressYoung`: the touch's attack press is in its target window (F4).
inline int Answer(int bound, int slot, bool atCastSite, bool enabled, bool touchPressYoung) {
    return bound < 0 && slot == 0 && atCastSite && enabled && touchPressYoung ? kAttack : bound;
}

// One log line per change of the slot, not per hit: told once the fallback first answers, told again only after a read saw the slot hold a skill.
class Teller {
public:
    // A cast-site read of the attack slot: `bound` the game's binding, `fellBack` whether Answer gave Attack for it. True: tell it now.
    bool Read(int bound, bool fellBack) {
        if (bound >= 0) {
            told_ = false;
            return false;
        }
        if (!fellBack || told_) return false;
        told_ = true;
        return true;
    }

private:
    bool told_ = false;
};

}  // namespace touchemptyslot
