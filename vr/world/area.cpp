// vr/world/area.cpp - the area the hero is in: the biome the game sets (BiomeSystem::SetCurrentBiome, hooked), and whether it is open
// air ([sky] outdoor) - cut from vr/vrcam.cpp (docs/plan_modules.md M4). What vrcam.cpp makes of it (in world, underground, the act,
// a ceiling biome) is still SkyTick's.
#include "area.h"
#include "../vrcam_internal.h"
#include "../config/settings.h"
#include "../replay_pause.h"
#include "../sky_dome.h"

#include <cctype>
#include <cstring>
#include <cwctype>
#include <iterator>

namespace vrc {
namespace replay { void AreaChanged(); }   // the game set a biome: an area loads (vrcam.cpp's replay)

// Sky: only in the open air. The area's biome comes from the game's
// BiomeSystem::SetCurrentBiome (hooked below); the exe names only the outdoor
// ones, dungeons and caves bring theirs from level data, so anything not on
// this list - [sky] outdoor in the ini replaces it - gets no sky. Tristram
// (act1_tristram, area 38), the monastery's courtyard (act1_court, the Outer Cloister -
// added 2026-10-07), Kurast (act3_kurast, 79-82), Travincal
// (act3_travincal_outdoors, 83), the River of Flame (act4_lava, 107), the Chaos
// Sanctuary (act4_diab, 108) and Nihlathak's Temple (expansion_wildtemple_tempenter,
// 121) come from level data too, and are open air.
SRWLOCK g_biomeLock = SRWLOCK_INIT;
static std::vector<std::string> g_outdoor;
char g_biome[96] = "";                  // last name the game set, bare (no folder, no extension), lower case
std::atomic<uint32_t> g_biomeGen{0};    // bumped by every SetCurrentBiome
std::atomic<uint32_t> g_areaSetGen{0};  // bumped only by the game's SetCurrentBiome (an area loads): [input] portal_look_horizon / portal_auto
std::atomic<bool> g_keysReset{false};   // [input] portal_auto: an area was set - HookKeyMove clears the game's move bytes once
std::atomic<uint32_t> g_sameBiomeSets{0};   // the same biome set again (no new area for the replay), since the replay last came on

void LoadOutdoorBiomes() {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"sky", L"outdoor", L"act1_outdoors,act1_tristram,act1_court,act2_outdoors,act2_town,act3_jungle,act3_docktown,act3_kurast,act3_travincal_outdoors,act4_mesa,act4_lava,act4_diab,expansion_town,expansion_siege,expansion_mountaintop,expansion_ruins,expansion_ruins_snow,expansion_wildtemple_tempenter",
                             buf, (DWORD)std::size(buf), g_iniPath);
    std::vector<std::string> list;
    std::string cur;
    for (const wchar_t* p = buf;; ++p) {
        if (*p == L',' || *p == 0) {
            if (!cur.empty()) list.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else if (*p != L' ' && *p != L'\t' && *p < 128) cur += (char)towlower(*p);
    }
    AcquireSRWLockExclusive(&g_biomeLock); g_outdoor = list; ReleaseSRWLockExclusive(&g_biomeLock);
    g_biomeGen.fetch_add(1);   // re-judged with the new list
}

// On the [sky] outdoor list: open air. Everything else in the world is a cave,
// a dungeon or a building.
bool IsOutdoorBiome(const std::string& biome) {
    // (the Summits: open air whatever the list - the uber one, 137, is not on it - while the game draws the sky; our own effect's sky and fog
    // as they were, by the list - Serg: "Our effect (old)" unchanged)
    if (skydome::IsGameSkyboxArea(biome) && g_set.gameSky.load()) return true;
    bool outdoor = false;
    AcquireSRWLockShared(&g_biomeLock);
    for (const std::string& o : g_outdoor) if (o == biome) { outdoor = true; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    return outdoor;
}

// BiomeSystem::SetCurrentBiome(const char* name), static; logs "Setting current
// biome to %s." itself. Runs at the main menu (act2_frontend_biome), so its
// page is decrypted, and the timer hooks it, before the first area loads.
const uint8_t kSigSetBiome[16] = {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x56,0x57,0x41,0x57,0x48,0x81,0xEC,0xC0,0x00};
using SetBiomeFn = uintptr_t (*)(const char*);
SetBiomeFn OrigSetBiome;

uintptr_t HookSetBiome(const char* name) {
    char raw[160] = {};
    if (name) for (size_t i = 0; i + 1 < sizeof raw; ++i) { if (!SafeRead(&raw[i], name + i, 1) || !raw[i]) break; }
    // bare name: no folder, no extension, lower case
    const char* b = raw;
    for (const char* p = raw; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    char bare[96] = {};
    for (size_t i = 0; b[i] && b[i] != '.' && i + 1 < sizeof bare; ++i) bare[i] = (char)tolower((unsigned char)b[i]);
    AcquireSRWLockExclusive(&g_biomeLock);
    const bool isNew = replaypause::BiomeIsNew(g_biome, bare);
    memcpy(g_biome, bare, sizeof bare);
    ReleaseSRWLockExclusive(&g_biomeLock);
    g_biomeGen.fetch_add(1);
    g_areaSetGen.fetch_add(1);
    if (g_set.portalAuto.load() && g_set.platform.load() == 0) g_keysReset.store(true);   // the new area starts from no move key; one still held sets it again
    // the replay pauses for another area, its last pair finished on the GPU first; the same biome set again (every few seconds in one
    // area) is no new area - it kept the replay off most of the time (vr/replay_pause.h)
    if (isNew) replay::AreaChanged();
    else g_sameBiomeSets.fetch_add(1);
    return OrigSetBiome(name);
}

}  // namespace vrc
