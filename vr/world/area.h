// vr/world/area.h - the area the hero is in (vr/world/area.cpp, docs/plan_modules.md M4): the biome the game set last, the open-air
// list, the hook on the game's BiomeSystem::SetCurrentBiome.
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace vrc {

extern SRWLOCK g_biomeLock;                 // g_biome, [sky] outdoor and vrcam.cpp's ceiling lists are read and written under it
extern char g_biome[96];                    // last name the game set, bare (no folder, no extension), lower case
extern std::atomic<uint32_t> g_biomeGen;    // bumped by every SetCurrentBiome
extern std::atomic<uint32_t> g_areaSetGen;  // bumped only by the game's SetCurrentBiome (an area loads)
extern std::atomic<bool> g_keysReset;       // [input] portal_auto: an area was set - HookKeyMove clears the game's move bytes once
extern std::atomic<uint32_t> g_sameBiomeSets;   // the same biome set again (no new area for the replay), since the replay last came on

void LoadOutdoorBiomes();                   // [sky] outdoor, from the ini
bool IsOutdoorBiome(const std::string& biome);

// BiomeSystem::SetCurrentBiome (InstallBiomeHook in vrcam.cpp puts it in)
constexpr uint64_t RVA_SET_BIOME = 0xE68C20;
extern const uint8_t kSigSetBiome[16];
using SetBiomeFn = uintptr_t (*)(const char*);
extern SetBiomeFn OrigSetBiome;
uintptr_t HookSetBiome(const char* name);

}  // namespace vrc
