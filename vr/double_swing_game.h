// double_swing_game.h - [hands] double_swing_by_hands, the game-facing half (double_swing_game.cpp). The decisions are in the pure double_swing.h; this file reads what they need from the game (the pad slots,
// the skills' levels, the hero's mana) and tells the log, and hands hit by touch the pad press to make. Nothing of the game is written or called here.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

// F4 ONLY (vr/view_gate.h): the touch that calls this acts in VR first person only.
namespace doubleswing {

// Plugin start: the log.
void Init(const D2RL::PluginContext* ctx);
// [hands] double_swing_by_hands, double_swing_window_ms (200 .. 2000, 700), double_swing_skill (0 Double Swing first, 1 Frenzy first) from the ini (every read). Off: nothing below does anything.
void Configure(bool on, float windowMs, int prefer);
bool On();

// The UI thread (gamestate's read of the hands, 5 times a second; cheap when off): which pad slots hold Double Swing and Frenzy, what is on the attack button, the hero's level in each - from the game's
// own controller tables and skill list.
void UiRead();
// hit by touch's timer: the hero's mana is read for the decisions (five times a second, on the UI thread). Cheap when off.
void Tick();

// What a touch accepted becomes.
struct Press {
    bool dual = false;       // the dual skill: press the pad slot below instead of the game's attack button
    uint16_t buttons = 0;    // XInput bits of the slot's button
    uint8_t lt = 0;          // the left trigger's value (the slot is on the second bank)
    uint32_t holdMs = 65;    // how long the press is held
    int skillId = 0;         // Skills.txt: 133 Double Swing, 147 Frenzy
};
// An accepted touch of `hand` (0 right, 1 left controller) on monster `id` at `nowMs` (GetTickCount64): the verdict is told in the log (one line each) and, for the second hand of a pair with the skill on a pad
// slot, the hero having a level in it and the mana for it, the dual skill's press is returned. dualWield: a melee weapon in each hand.
Press Strike(int hand, uint32_t id, uint64_t nowMs, bool dualWield);
// A touch of the first of two blades that met one monster in the same pass, noted without a verdict (the second's Strike then sees it).
void Note(int hand, uint32_t id, uint64_t nowMs);
// The touch stands down (another view, a menu, another weapon): the pairs are forgotten.
void Forget();

// Shared with [gameplay] melee_skill_armed (vr/melee_arm_game.cpp), UI thread only: the skill (and its owner) the game has on each of the pad's ten skill slots, in shield_smite.h's SlotPress order (B X Y RB,
// LT + B X Y RB, A, LT + A); false, with *why, when the controller tables do not read as ten slots. And the hero's total level in a skill (-1 not read, 0 not learned).
bool ReadPadSlots(int skills[10], int owners[10], const char** why);
int ReadHeroLevel(int id, int owner);

}  // namespace doubleswing
