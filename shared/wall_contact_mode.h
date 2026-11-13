// wall_contact_mode.h - [weight] wall_contact_mode: what stops the held weapon at the level's walls, one choice of three (Serg 2026-11-13: compare
// ours and Bear's, never both at once):
//   0 Off      the weapon passes walls (the default)
//   1 Ours     world contact: the walls from the game's own meshes (vr/world_contact.h; D2R VR Settings makes d2r_vr_props\solids.bin for it)
//   2 Bear's   the game's wall map: its collision cells (vr/wall_collide.h, vr/unleashed_wall_cells.h - from D2R VR Unleashed, MPL 2.0)
// An ini from before the choice has [weight] world_contact (0/1) instead: missing wall_contact_mode, world_contact=1 reads as Ours. The bare hands
// ([weight] world_contact_hands) and the own body ([weight] own_body) keep their own ticks.
// Read the same way by the plugin (vr/config/load_settings.cpp), D2R VR Settings, the in-headset settings (vr/hset_store.cpp); the Setup carries an
// old world_contact=1 over once (installer/ini_choice.inc MoveToWallContactMode).
#pragma once

#include <cmath>

namespace wallcontact {

constexpr int kOff = 0, kOurs = 1, kBears = 2, kCount = 3;

// raw: the ini's wall_contact_mode (< 0: missing); legacyWorldContact: its [weight] world_contact.
inline int FromKeys(float raw, bool legacyWorldContact) {
    if (raw >= 0.0f && std::isfinite(raw)) {
        const int m = (int)std::lround(raw);
        return m <= kOff ? kOff : m >= kBears ? kBears : m;
    }
    return legacyWorldContact ? kOurs : kOff;
}

}  // namespace wallcontact
