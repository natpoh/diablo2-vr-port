// stand_still_wire.h - [gameplay] attack_in_place on the game's pad poll (vr/stand_still_wire.cpp; the logic is vr/stand_still.h).
#pragma once

#include <cstdint>

namespace standstill {

void SetLogger(void (*log)(const char*));
// [gameplay] attack_in_place, live (vrcam's LoadSettings). Off: nothing is held, nothing held back, and what was held is let go.
void SetWanted(bool on);
bool Wanted();
// Any thread: an attack of the plugin's is pressed now with Stand Still (or in the short tail after it): the hero must not be run off by it (straywalk).
bool AttackActive();

// The game's click on the map (vrcam's HookMapClick): a click with nothing under the pointer was set to Stand Still in VR first
// person. Counts them and says so in the log - the first, then a summary at most every 10 s, never one line a click.
void NoteClick(int type);

// [hands] throw_stand_still, live (vrcam's LoadSettings): 1 (the default) a throw by gesture's A press is held with Stand Still like any attack of ours;
// 0 it is not (a throw is a cast at a spot: it never walks the hero). `isThrowPress` (any thread): the A attack down now is a throw's
// (throwwire::PressIsThrow, registered once by vrcam).
void SetThrowStill(bool on);
void SetThrowPressFn(bool (*isThrowPress)());

// What to do with this pad poll's attack presses.
struct Gate {
    bool holdBackA = false;    // do not press A for the attack this poll (Stand Still is not down yet)
    uint16_t holdBack = 0;     // ... nor these skill buttons
    bool addA = false;         // press A for the attack although the source let go (a press stretched to its least length)
    uint16_t add = 0;          // ... and these buttons
};

// The game's pad poll (vrcam's ApplyActions, pad 0, after the pad is composed): `attackA` is the plugin's attack action this
// poll (the layout's trigger, a gesture's press, hit by touch - not a pick-up), `touch` that it is hit by touch's; `padButtons`,
// `lt` the pad as composed (the skill buttons B X Y RB the plugin presses are in it); `menu` a panel is open. In VR first
// person (F4) with the switch on it asks the game's own key command "Stand still" (0x24) while an attack of ours is pressed,
// and holds the press back until the game has it down. Anywhere else it returns an empty Gate.
Gate Poll(bool attackA, bool touch, bool menu, uint16_t padButtons, uint8_t lt);

}  // namespace standstill
