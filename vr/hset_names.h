#pragma once
// Names for the values of the in-headset settings panel's stepper rows that are not numbers. Pure.
//
// Today one: the HUD page's "Toolbar type" row, whose value is the pair [hud] hide + [hud] bar_place (shared/toolbar_type.h):
// the box shows "Wrists", "Chest, low", "Low in front", "Left forearm", "Right forearm", "Not shown" or "In the picture".
// A row with no name is shown with its Def's own printf format, as every other.

#include <cmath>

#include "hset_defs.h"
#include "toolbar_type.h"

namespace hset {

// The name to show for a row's value, nullptr for a row that shows a number.
inline const char* ValueName(const Values& v, int key) {
    if (key == kKeyBarPlace) return toolbartype::kShort[toolbartype::TypeOf((int)std::lround(v[kKeyHide]), (int)std::lround(v[kKeyBarPlace]))];
    return nullptr;
}

}  // namespace hset
