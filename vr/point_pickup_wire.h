// point_pickup_wire.h - [input] point_pickup's feedback and its trigger (point_pickup_wire.cpp), F4 only.
//
// The item the pointing hand's ray is on (vr/input/flat_click.cpp PointPickupAnswer, the game's UI thread) is published here every time the
// game asks its interact target. From it:
//   - the highlight: the item's own ground label gets D2R VR Unleashed's rim and name look (vr/hud/labels.cpp, vr/unleashed_loot_aim.h); when the
//     game lays out no label for it, the game's own Show items command is held for it (gamecmd: no key, no pad) and every other name is cut, so the
//     pointed one shows by itself; a soft pulse in the pointing hand when the highlight moves to another item;
//   - the trigger (vr/point_trigger.h): with that hand's grip held, its trigger press picks the item up - the layout's interact pressed
//     (actions.cpp ApplyActions) with the game's interact target answered with the item (PointPickupAnswer) until it has left the floor.
#pragma once

#include <D2RLPlugin/api.h>
#include <cstdint>

namespace pointpickup {

// From D2RLoaderLoadPlugin: the frame listener.
void Init(const D2RL::PluginContext* ctx);

// PointPickupAnswer, every call it makes a choice: the highlighted item's id (0: none) and unit, the pointing hand (direct_input's: 0 left, 1 right),
// whether A / the trigger take it from here (point_pickup_reach_cells), its name's rarity colour, whether the game's label hooks are in, and whether
// that hand's trigger may pick up (false: the hand holds a crossbow - its trigger shoots, A picks up there).
void Publish(uint32_t id, uintptr_t unit, int hand, bool inReach, const float rgb[3], bool labelHooksIn, bool triggerOk);

// The labels (the game's UI thread): the highlighted item's unit while fresh (0 none), its look, and that the game laid its label out now.
uintptr_t Lit();
bool LitInReach();
void LitRarity(float rgb[3]);
void NoteLaidOut();
// Show items is held for the pointed item now (or a moment ago): every other name the game lays out is cut.
bool OnlyLit();

// The trigger's pick-up in flight: the item's id (0 none); ended by PressEnd (taken, gone) or after kPressMs.
uint32_t PressId();
void PressEnd(const char* why);

}  // namespace pointpickup
