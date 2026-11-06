#pragma once
// Names for the values of the in-headset settings panel's stepper rows that are not numbers. Pure.
//
// The HUD page's "Toolbar type" row, whose value is the pair [hud] hide + [hud] bar_place (shared/toolbar_type.h):
// the box shows "Wrists", "Chest, low", "Low in front", "Left forearm", "Right forearm", "Not shown" or "In the picture".
// A row with no name is shown with its Def's own printf format, as every other.

#include <cmath>

#include "hset_defs.h"
#include "toolbar_type.h"
#include "ui_text.h"

namespace hset {

// A toolbar type's short name in the player's language ("vr.toolbar.<name>": vr/ui_text_tables.h lists them for lang\en.txt).
inline std::string ToolbarKey(int type) { return "vr.toolbar." + uitext::Slug(toolbartype::kShort[type]); }
inline const char* ToolbarName(int type) { return uitext::Tr(ToolbarKey(type), toolbartype::kShort[type]); }

// The Melee > Attack page's Direct hit (shared/direct_hit_mode.h): its four choices, short for the value box (the line under the row says the rest).
inline const char* DirectHitName(int mode) {
    switch (mode) {
        case 1: return uitext::Tr("vr.directhit.cleave", "2H extras");
        case 2: return uitext::Tr("vr.directhit.rules", "By the rules");
        case 3: return uitext::Tr("vr.directhit.always", "Always");
        default: return uitext::Tr("vr.directhit.off", "Off");
    }
}

// The HUD page's "Allies shown" ([hud] allies_place, shared/allies_place.h): its three places, short for the value box.
inline const char* AlliesPlaceName(int place) {
    switch (place) {
        case 1: return uitext::Tr("vr.allies.map", "Under the map");
        case 2: return uitext::Tr("vr.allies.wrist", "On the wrist");
        default: return uitext::Tr("vr.allies.game", "At the top");
    }
}

// The name to show for a row's value, nullptr for a row that shows a number.
inline const char* ValueName(const Values& v, int key) {
    if (key == kKeyAlliesPlace) return AlliesPlaceName((int)std::lround(v[kKeyAlliesPlace]));
    if (key == kKeyBarPlace) return ToolbarName(toolbartype::TypeOf((int)std::lround(v[kKeyHide]), (int)std::lround(v[kKeyBarPlace])));
    if (key == kKeyMeleeDirectClient) return DirectHitName((int)std::lround(v[kKeyMeleeDirectClient]));
    return nullptr;
}

}  // namespace hset
