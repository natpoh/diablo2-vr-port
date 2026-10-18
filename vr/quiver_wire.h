// quiver_wire.h - [hands] javelin_quiver: the quick choice of the skill of the next throw (vr/quiver.h, vr/quiver_ctl.h), wired to the headset frames
// (vr/direct_input.h) and to the game's skill list (the skill wheel's reads, vr/wheel_game.h). F4 ONLY (vr/view_gate.h).
//
// With a javelin or a throwing weapon in the weapon hand and the hero knowing a skill that weapon allows, reaching a hand behind a shoulder (the javelin bag
// on the back) and tapping its trigger steps the choice: the right hand to the next skill, the left to the previous one, with a pulse in that hand
// (two when the choice is back at the plain throw). A button bound to action:quivernext / action:quiverprev in [direct] steps it from anywhere.
// The choice is not written into the game: the plain attack is READ as the chosen skill by the hook of the controller's slot->skill read (vr/vrcam.cpp
// HookSlotSkill), so the throw gesture, the touch and the pad's own A all cast it through the game's own cast - mana, range, ammunition and the attack
// rate stay the game's; a skill the weapon does not allow, or the hero does not have, is never offered.
//
// Unticked: nothing exists beyond a listener that returns at once.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace quiverwire {

// Once, from the plugin's start: the log.
void Init(const D2RL::PluginContext* ctx);

// [hands] javelin_quiver and javelin_quiver_reach, on every read of the ini (live). Either off lets everything go at the next frame.
void Configure(bool on, bool reach);
bool On();

// The hook's question (any thread): the skill the plain attack is read as now, and whether it is a cast at a spot (a throw may use it) or a blow in
// reach. False: nothing is chosen (the plain throw), or the choice is not for what is in the hand.
bool Chosen(int32_t* skill, bool* ranged);

// The skill wheel's reads (the learned list) are wanted: on, a thrown weapon in the hand, in play. vrcam keeps wheelgame::Tick going for it.
bool WantsSkills();

// What an overlay would show (any thread).
struct Info {
    char name[48] = {};
    int position = 0, count = 0;   // "2 of 5"
    int32_t skill = -1;
    bool ranged = true;
    bool active = false;           // there is something to choose from
};
bool Current(Info* out);

}  // namespace quiverwire
