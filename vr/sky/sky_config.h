// vr/sky/sky_config.h - what the rest of the VR layer uses of vr/sky/sky_config.cpp (the sky's and the ceiling's configuration).
// (CeilingNow has default arguments: it is declared, with them, in each file that calls it.)
#pragma once

#include <string>

namespace vrc {

int ActOfBiome(const std::string& biome);               // the act (1..6) a biome belongs to by its name; 0 = cannot tell
int SkyPaletteFor(const std::string& biome, bool always);   // -1 = no sky here
bool IsCeilingBiome(const std::string& biome);          // [ceiling] biomes
bool IsGameCeilingBiome(const std::string& biome);      // [ceiling] game_ceiling_biomes
bool SkyFileExists(const std::wstring& name);

}  // namespace vrc
