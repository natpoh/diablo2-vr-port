// slide_kinds.h - [weight] slide_second_hand_<kind>: on which weapons the second hand slides along the shaft. Pure.
//
// Two hands on a long shaft: the second hand may slide along it with its controller while its grip stays squeezed (cleanroom/heft/slide.h; with the grip library also loose by the trigger).
// It is a staff's way - the left hand runs up and down the pole - and a spear's and a polearm's; a sword's, an axe's or a maul's second hand sits where it took the hilt and stays there. So
// each kind of weapon that can have a long shaft has a tick of its own: ticked, its second hand slides; unticked, it holds where it closed (tight: the point and the turn fixed, held by the
// grip alone). The same list drives the mod's own way of holding and the grip library's. Kinds whose shaft is under a metre and a tenth (a dagger, a wand, a scepter, a throwing weapon,
// a one-handed sword) never slide, ticked or not - there is nothing to slide on - and so have no tick.
//
// The kinds are the game's weapon types (D2RVR_TYPE_*, shared/d2r_vr_state.h); the keys are `slide_second_hand_<name>` in [weight], 1 = slides. The master key `slide_second_hand` (1) still
// switches all of them off.
#pragma once

#include <cstdint>

#include "d2r_vr_state.h"

namespace slidekinds {

struct Kind {
    const char* name;      // the ini key's tail, and the log's word
    uint32_t type;         // D2RVR_WeaponType
    bool slides;           // the default: does the second hand slide
};

// In the order of the Settings list. Staffs, spears and polearms slide by default; swords, axes, maces, javelins and the unnamed kind do not.
inline constexpr Kind kKinds[] = {
    {"sword", D2RVR_TYPE_SWORD, false},   {"axe", D2RVR_TYPE_AXE, false},       {"mace", D2RVR_TYPE_MACE, false},   {"staff", D2RVR_TYPE_STAFF, true},
    {"polearm", D2RVR_TYPE_POLEARM, true}, {"spear", D2RVR_TYPE_SPEAR, true},   {"javelin", D2RVR_TYPE_JAVELIN, false}, {"other", D2RVR_TYPE_OTHER, false},
};
inline constexpr int kCount = (int)(sizeof(kKinds) / sizeof(kKinds[0]));
inline constexpr uint32_t kAll = (1u << kCount) - 1u;   // every kind: the old behaviour (every long shaft's second hand slides)

inline constexpr uint32_t DefaultMask() {
    uint32_t m = 0;
    for (int i = 0; i < kCount; ++i)
        if (kKinds[i].slides) m |= 1u << i;
    return m;
}

// The tick of a weapon type, -1 for a type whose second hand never slides (no tick: a short shaft, or no melee weapon).
inline int IndexOf(uint32_t type) {
    for (int i = 0; i < kCount; ++i)
        if (kKinds[i].type == type) return i;
    return -1;
}

// Does the second hand of a weapon of this type slide, with this mask of ticks?
inline bool Allows(uint32_t mask, uint32_t type) {
    const int i = IndexOf(type);
    return i >= 0 && ((mask >> i) & 1u) != 0;
}

}  // namespace slidekinds
