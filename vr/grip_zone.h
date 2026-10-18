// grip_zone.h - [weight] grip_hilt_only: where along the shaft the SECOND hand may take a weapon the other hand holds. Pure.
//
// A sword, an axe, a mace is held by its hilt or haft; its blade or head is not a place to grab. Without the tick the second hand takes the weapon anywhere along it (within
// [weight] grab_cm of the shaft, from the pommel to the tip), as ever. With it, a sword's, a dagger's, a scepter's, an axe's or a mace's second hand takes it only on the hilt (the
// haft, for an axe or a mace): from the pommel to the guard, plus a little for the guard itself. A grip outside that zone takes nothing - it is free for something else, a belt
// potion for one. A staff, a spear, a polearm, a javelin, a wand or a throwing weapon has no blade to keep off: the whole shaft stays the zone.
//
// Where the guard is: the weight model's shape for the kind has the hilt as its first piece (the handle, the haft) and the blade or head after it (cleanroom/heft/hold.cpp ShapeOf),
// scaled onto the weapon's drawn extents (Hold::SetDrawnShaft) - so the hilt follows the weapon's real length. The game does not tell where the guard is, so for a weapon whose hilt
// the player finds too short or too long there is a length of its own for each kind here: [weight] grip_hilt_cm_<kind> in centimetres from the pommel, 0 = the model's.
//
// The kinds are the game's weapon types (D2RVR_TYPE_*, shared/d2r_vr_state.h).
#pragma once

#include <cstdint>

#include "d2r_vr_state.h"

namespace gripzone {

struct Kind {
    const char* name;   // the ini key's tail
    uint32_t type;      // D2RVR_WeaponType
};

// The kinds with a hilt and a blade or head. In the order of the Settings list.
inline constexpr Kind kKinds[] = {
    {"sword", D2RVR_TYPE_SWORD}, {"axe", D2RVR_TYPE_AXE}, {"mace", D2RVR_TYPE_MACE}, {"scepter", D2RVR_TYPE_SCEPTER}, {"dagger", D2RVR_TYPE_DAGGER},
};
inline constexpr int kCount = (int)(sizeof(kKinds) / sizeof(kKinds[0]));

// The longest hilt a slider gives, centimetres (a two-handed axe's haft is more than a metre).
inline constexpr float kMaxCm = 150.0f;

// The row of a weapon type, -1 for a type with no hilt of its own (the whole shaft is its zone).
inline int IndexOf(uint32_t type) {
    for (int i = 0; i < kCount; ++i)
        if (kKinds[i].type == type) return i;
    return -1;
}

}  // namespace gripzone
