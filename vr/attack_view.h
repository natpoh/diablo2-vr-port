// attack_view.h - in which views the aiming hand (or the head's look) may direct the hero's attack. Pure; the test is cleanroom/tests/attack_view_test.cpp,
// the wiring is vr/vrcam.cpp (AimByVector: the aim function's facing, target vector and point hooks).
//
// The user's rule (2026-10-15, in his words through the coordinator): VR F2 (third person) and VR F4 (first person) - the hero attacks where the
// player looks (the aiming hand, [bow] mode 4: as it has been); VR F1 (from above) and VR F3 (the game on the floor) are top-down views, "where the
// hero faces, there he hits": the game's own direction, nothing of the head or the hand. The flat game (platform 0) is left exactly as it was.
//
// The fault (the log of test-30, 17:24-17:27): in F1 and F3 the hand turned the attack - "target: attack vector 0.62 0.79 -> the hand -0.79 -0.62" (the
// hero's facing against the hand: almost opposite), "TURNED to the hand" 53 times - because AimByVector() did not ask the view at all.
#pragma once

namespace attackview {

// VR platform = 1. vrView: 1 from above, 2 third person, 3 the game on the floor, 4 first person with the body.
inline bool HandMayAim(int platform, int vrView) {
    if (platform != 1) return true;           // the flat game: not touched
    return vrView == 2 || vrView == 4;        // VR: F2 and F4 look where the player looks; F1 and F3 (top-down) keep the hero's own facing
}

}  // namespace attackview
