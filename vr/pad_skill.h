#pragma once
// A skill slot of the game's controller pressed in a pad report - the "skill held" that lets the aiming hand direct a cast in [bow] mode 4
// (vrcam AimByVector: HookUnitFacing turns the attack vector to the hand, HookSpellResolve takes the game's target and aim assist off).
//
// The game's controller has twelve slots (wheel_types.h kSlotDefs): B X Y RB, the same with the left trigger held, A, RT, LT + A, LT + RT. The
// left trigger only picks the row, so a press is one of A B X Y RB - or the RIGHT TRIGGER, a slot of its own: [openxr] skill_rt_a's slot
// controls cast it (the trigger's press is the game's RT, vr/slot_controls.h) and so does BodyWalk's pad. The mask had only the five buttons:
// a skill on RT went with the game's own aim - the hero's facing, which the last cast along the hand had left (a player's report, 0.182: Fire
// Bolt on a button went along the hand; Ice Bolt on the trigger flew where the last Fire Bolt had gone, wherever the hand pointed).
// Pure: buttons and the trigger in, a yes or no out. Test: cleanroom/tests/pad_skill_test.cpp.
#include <cstdint>

namespace padskill {

constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000, kRB = 0x0200;   // XINPUT_GAMEPAD_*
constexpr uint16_t kSlotButtons = kA | kB | kX | kY | kRB;
constexpr uint8_t kRtDown = 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD: past it the trigger is down (point_cast.h's kRtDown, the same slot)

constexpr bool Pressed(uint16_t buttons, uint8_t rightTrigger) {
    return (buttons & kSlotButtons) != 0 || rightTrigger > kRtDown;
}

}  // namespace padskill
