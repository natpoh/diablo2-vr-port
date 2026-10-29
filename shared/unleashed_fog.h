// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed:
//   843b3a464ab1bc5b8dc36eff797ab80620b85914 "Fog: eased in over its first 40 units, no hard ring round the hero"
//       (reshade/D2R_DepthFog.fx FogAt) - kEaseUnits, Ease, kEaseHlsl;
//   d799f93ad2a9d56c5cb6e9ba7bd74a752b08f324 "Fog: none in the River of Flame, as in caves"
//       (steamvr/biome_list.h, vr/vrcam.cpp [fog] no_fog_areas) - kNoFogBiomes, NoFogArea.
// Taken into D2R VR Authentic behind switches of its own ([fog] ease_in, [fog] no_fog_river; both off = the fog as before). The rest of
// D2R VR Authentic is MIT; this file and reshade/D2R_FogEase_Unleashed.fxh stay MPL 2.0.
#pragma once
#include <string>

namespace unleashed::fog {

// ---- The fog eased in (843b3a4) ----
// Under curve 1 the fog's curve leaves its start with no limit to its slope, and the clear ground round the hero ended in a hard ring
// (Unleashed's owner, 2026-10-28: "a circle of clarity around the player ... with a very sharp line defining its border"). The fog's
// share is multiplied by a smoothstep over its first kEaseUnits past the start: 0 at the start, 1 from start + kEaseUnits on.
inline constexpr float kEaseUnits = 40.0f;

// smoothstep(start, start + kEaseUnits, dist), as HLSL's
inline float Ease(float dist, float start) {
    float x = (dist - start) / kEaseUnits;
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return x * x * (3.0f - 2.0f * x);
}

// The same in HLSL, for a shader built from a string (vr/fog_before_fx.cpp): word for word the function in
// reshade/D2R_FogEase_Unleashed.fxh (unleashed_fog_test checks it). on 0: 1, the fog as before.
inline constexpr const char kEaseHlsl[] =
    "float UnleashedFogEase(float dist, float start, float on)\n"
    "{\n"
    "    return on > 0.5 ? smoothstep(start, start + 40.0, dist) : 1.0;\n"
    "}\n";

// ---- No fog in the River of Flame (d799f93) ----
// The River of Flame (act4_lava) is open air, on [sky] outdoor, yet its lava's own glow and haze are the game's: with the added fog
// over them it is a grey wash. The biomes that get no added fog there, as caves do with [fog] caves=0.
inline constexpr const char* kNoFogBiomes[] = {"act4_lava"};

// The area's biome (lower case, as vrcam keeps it) gets no added fog: `on` ([fog] no_fog_river) and the biome on the list, matched whole.
inline bool NoFogArea(const std::string& biome, bool on) {
    if (!on) return false;
    for (const char* b : kNoFogBiomes)
        if (biome == b) return true;
    return false;
}

}   // namespace unleashed::fog
