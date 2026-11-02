// vr/sky/lights.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M8); what it gives: vr/sky/world_light.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// [fog] game_fog: vrcam.cpp's gamefogvr (the cave light's hook hands it the blend) - as cavelight saw it there, with the pure parts
namespace gamefogvr {
using namespace ::gamefogvr;
void Apply(const uint8_t* vis, void* fs);
}  // namespace gamefogvr


// Light in caves and dungeons, [cave_light] (docs/plan_cave_light.md). Underground the
// game has no sun: a dungeon is lit by the hero's own light, the torches, and the light
// that fills it all - beyond the hero's circle only that one. It is the area's vis json
// iblIntensity: the act 2 tombs 0.12, act 1's caves 2, the maggot lair 12.5. On a headset
// whose screen loses the darkest shades (an LCD: Meta Quest) a cave was black where an
// OLED (PS VR2) showed all of it (the user, 2026-10-13). Lifting the black level would
// have greyed the whole game; this is light - the game multiplies the stone's own
// colour by it before its tone mapping, so the colours keep their strength.
// VisualDataSystem::Update (0xF5D1A0) blends the areas' vis data and hands the blend to
// 0xF5DAD0 (system, ?, const VisualDataComponent*, FrameSettings*), which writes its
// ambient light (+4) to a global through 0xF690F0 - unless the byte before that global
// is set: the game's own set-by-hand, which its debug menu's slider sets (0xF2736F); the
// renderer reads the global (0xE12749). After it, in an area off [sky] outdoor, the hook
// takes that byte and writes the global itself: the blend's own value times [cave_light]
// ambient (or ambient_<biome>), at least ambient_min - from the blend, never multiplied in
// place, and with the byte taken the game no longer writes its own value first, so no
// frame is drawn with it in between. Back to the game's own (outdoors, unticked) the byte
// is given back and the global gets the blend's value. A byte the game set itself is left
// alone. Drawing only - no stat, no light radius, nothing the game plays by.
// The hook goes in only once the box is ticked: unticked, the game's code is as it was.
namespace cavelight {
constexpr uint64_t RVA_VIS_APPLY = 0xF5DAD0;
const uint8_t kSigVisApply[16] = {0x4C,0x8B,0xDC,0x55,0x41,0x56,0x41,0x57,0x49,0x8D,0x6B,0xA1,0x48,0x81,0xEC,0x00};
constexpr uint64_t RVA_IBL_BY_HAND = 0x3543AD0, RVA_IBL = 0x3543AD4;
// The hero's own light, the same way: the blend's +0x40 power, +0x44 attenuation, +0x48 distance
// scaler, +0x4C two contributions go to five globals, unless the byte 0x34B6DC4 is set (its debug
// slider 0xF25BD0 sets it). The renderer's unit lights (0x74B324) give the hero's light the power
// and the attenuation, and its reach as his light radius times the distance scaler (1.333): the
// circle round him. The ambient light lights only inside it - more of it made the circle brighter,
// not wider (the user, 2026-10-13), so [cave_light] hero_reach widens it.
constexpr uint64_t RVA_HERO_BY_HAND = 0x34B6DC4, RVA_HERO_LIGHT = 0x267C724;
constexpr size_t kIblAt = 4, kHeroAt = 0x40, kHeroN = 5;   // VisualDataComponent (the game's own reflection data)
using VisApplyFn = void (*)(void*, void*, const uint8_t*, void*);
VisApplyFn OrigVisApply = nullptr;

std::atomic<bool> g_on{false};          // [cave_light] enabled
std::atomic<float> g_ambient{2.0f};     // [cave_light] ambient: times the area's own
std::atomic<float> g_ambientMin{0.0f};  // [cave_light] ambient_min: at least this (0 = no floor)
std::atomic<float> g_heroReach{1.5f};   // [cave_light] hero_reach: the hero's light circle, times the area's own
std::atomic<float> g_heroPower{1.0f};   // [cave_light] hero_power: his light's strength, times the area's own
// [cave_light] hero_falloff: his light's attenuation (5.5), times the area's own. Past x10 the reach made the
// circle no wider (the user, 2026-10-13), so it stops at 10; this changed nothing either - hidden in the settings.
std::atomic<float> g_heroFalloff{1.0f};
struct BiomeCfg { std::string biome; float ambient = -1.0f, reach = -1.0f; };   // -1 = the common one
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<BiomeCfg> g_perBiome;       // [cave_light] ambient_<biome>, hero_reach_<biome>, under g_lock
std::atomic<uint32_t> g_loadGen{0};

std::atomic<bool> g_hooked{false};
std::atomic<float*> g_ibl{nullptr}, g_hero{nullptr};
std::atomic<uint8_t*> g_iblByHand{nullptr}, g_heroByHand{nullptr};
std::atomic<float> g_mul{1.0f}, g_min{0.0f};   // in the area now: 1 and 0 = the game's own
std::atomic<float> g_reach{1.0f}, g_power{1.0f}, g_falloff{1.0f};
std::atomic<float> g_seen{-1.0f}, g_seenReach{-1.0f};   // the blend's own, last frame (for the log)
bool g_tookIbl = false, g_tookHero = false;    // the set-by-hand bytes are ours (the game's thread only, in the hook)

// n floats from the blend to the globals: the game's own (want false: the byte given back if
// it was ours) or ours, the byte taken so the game no longer writes its own first. A byte the
// game set itself is left alone.
void Put(float* dst, uint8_t* byHand, bool* took, const float* own, const float* ours, size_t n, bool want) {
    if (!byHand) return;
    if (!want) {
        if (*took) { memcpy(dst, own, n * sizeof(float)); *byHand = 0; *took = false; }   // the game's own again, from this frame
        return;
    }
    if (*byHand && !*took) return;
    memcpy(dst, ours, n * sizeof(float));
    *byHand = 1;
    *took = true;
}

void HookVisApply(void* sys, void* a, const uint8_t* vis, void* fs) {
    OrigVisApply(sys, a, vis, fs);
    if (!vis) return;
    gamefogvr::Apply(vis, fs);   // [fog] game_fog: our numbers into the frame's fog, once its layout is seen to hold
    if (float* ibl = g_ibl.load(std::memory_order_relaxed); ibl) {
        float own;
        memcpy(&own, vis + kIblAt, sizeof own);
        if (std::isfinite(own) && own >= 0.0f) {
            g_seen.store(own, std::memory_order_relaxed);
            const float mul = g_mul.load(std::memory_order_relaxed), lo = g_min.load(std::memory_order_relaxed);
            const float ours = std::max(own * mul, lo);
            Put(ibl, g_iblByHand.load(std::memory_order_relaxed), &g_tookIbl, &own, &ours, 1, mul != 1.0f || lo > 0.0f);
        }
    }
    if (float* hero = g_hero.load(std::memory_order_relaxed); hero) {
        float own[kHeroN];
        memcpy(own, vis + kHeroAt, sizeof own);
        bool sane = true;
        for (float v : own) sane = sane && std::isfinite(v) && v >= 0.0f;
        if (sane) {
            g_seenReach.store(own[2], std::memory_order_relaxed);
            const float reach = g_reach.load(std::memory_order_relaxed), power = g_power.load(std::memory_order_relaxed);
            const float falloff = g_falloff.load(std::memory_order_relaxed);
            const float ours[kHeroN] = {own[0] * power, own[1] * falloff, own[2] * reach, own[3], own[4]};
            Put(hero, g_heroByHand.load(std::memory_order_relaxed), &g_tookHero, own, ours, kHeroN,
                reach != 1.0f || power != 1.0f || falloff != 1.0f);
        }
    }
}

// With the other settings: the section's keys, ambient_<biome> and hero_reach_<biome> among them.
void Load() {
    g_on.store(IniB(L"cave_light", L"enabled", false));
    g_ambient.store(std::clamp(IniF(L"cave_light", L"ambient", 2.0f), 0.0f, 20.0f));
    g_ambientMin.store(std::clamp(IniF(L"cave_light", L"ambient_min", 0.0f), 0.0f, 20.0f));
    g_heroReach.store(std::clamp(IniF(L"cave_light", L"hero_reach", 1.5f), 0.25f, 10.0f));
    g_heroPower.store(std::clamp(IniF(L"cave_light", L"hero_power", 1.0f), 0.0f, 50.0f));
    g_heroFalloff.store(std::clamp(IniF(L"cave_light", L"hero_falloff", 1.0f), 0.01f, 10.0f));
    std::vector<BiomeCfg> list;
    wchar_t buf[8192];
    const DWORD n = GetPrivateProfileSectionW(L"cave_light", buf, (DWORD)std::size(buf), g_iniPath);
    for (const wchar_t* p = buf; p < buf + n && *p; p += wcslen(p) + 1) {
        while (*p == L' ' || *p == L'\t') ++p;
        const wchar_t* eq = wcschr(p, L'=');
        if (!eq) continue;
        const bool amb = !wcsncmp(p, L"ambient_", 8), reach = !wcsncmp(p, L"hero_reach_", 11);
        if (!amb && !reach) continue;
        std::string biome;
        for (const wchar_t* q = p + (amb ? 8 : 11); q < eq; ++q)
            if (*q != L' ' && *q != L'\t' && *q < 128) biome += (char)towlower(*q);
        if (biome.empty() || (amb && biome == "min")) continue;
        wchar_t* end = nullptr;
        const float v = wcstof(eq + 1, &end);
        if (end == eq + 1 || !std::isfinite(v)) continue;
        BiomeCfg* c = nullptr;
        for (BiomeCfg& e : list) if (e.biome == biome) c = &e;
        if (!c) { list.push_back({biome}); c = &list.back(); }
        if (amb) c->ambient = std::clamp(v, 0.0f, 20.0f);
        else c->reach = std::clamp(v, 0.25f, 10.0f);
    }
    AcquireSRWLockExclusive(&g_lock); g_perBiome = list; ReleaseSRWLockExclusive(&g_lock);
    g_loadGen.fetch_add(1);
}

// Twice a second: the hook once it is wanted and its page is decrypted (quiet until then).
void Install() {
    static bool failed = false;
    if (g_hooked.load() || failed || !g_ctx || !(g_on.load() || g_set.gameFog.load())) return;   // ([fog] game_fog rides on it too)
    if (!Matches(RVA_VIS_APPLY, kSigVisApply, sizeof kSigVisApply)) return;
    float* ibl = (float*)d2rsig::Addr(RVA_IBL);
    uint8_t* iblByHand = (uint8_t*)d2rsig::Addr(RVA_IBL_BY_HAND);
    float* hero = (float*)d2rsig::Addr(RVA_HERO_LIGHT);
    uint8_t* heroByHand = (uint8_t*)d2rsig::Addr(RVA_HERO_BY_HAND);
    if ((!ibl || !iblByHand) && (!hero || !heroByHand)) {
        failed = true;
        Log("vrcam: cave light - the game's ambient and hero light not found in this build: [cave_light] does nothing");
        return;
    }
    if (ibl && iblByHand) { g_ibl.store(ibl); g_iblByHand.store(iblByHand); }
    if (hero && heroByHand) { g_hero.store(hero); g_heroByHand.store(heroByHand); }
    g_hooked.store(d2rsig::Hook(RVA_VIS_APPLY, kSigVisApply, sizeof kSigVisApply, (void*)&HookVisApply, (void**)&OrigVisApply));
    failed = !g_hooked.load();
    if (g_hooked.load())
        LogF("vrcam: cave light hook in - [cave_light] lights caves and dungeons (ambient %s, hero's light %s)",
             g_ibl.load() ? "yes" : "NOT FOUND", g_hero.load() ? "yes" : "NOT FOUND");
    else
        Log("vrcam: cave light hook FAILED - [cave_light] does nothing");
}

// Every update: the area's multipliers and floor (only off [sky] outdoor); to the log a
// second after each change, with the game's own values the hook saw.
void Tick(const std::string& biome, bool underground) {
    static uint32_t seenBiome = ~0u, seenLoad = ~0u;
    static bool seenOn = false, seenUnder = false;
    static ULONGLONG tellAt = 0;
    const uint32_t bg = g_biomeGen.load(), lg = g_loadGen.load();
    const bool on = g_on.load();
    if (bg != seenBiome || lg != seenLoad || on != seenOn || underground != seenUnder) {
        seenBiome = bg; seenLoad = lg; seenOn = on; seenUnder = underground;
        float mul = 1.0f, lo = 0.0f, reach = 1.0f, power = 1.0f, falloff = 1.0f;
        if (on && underground) {
            mul = g_ambient.load();
            lo = g_ambientMin.load();
            reach = g_heroReach.load();
            power = g_heroPower.load();
            falloff = g_heroFalloff.load();
            AcquireSRWLockShared(&g_lock);
            for (const BiomeCfg& c : g_perBiome)
                if (c.biome == biome) {
                    if (c.ambient >= 0.0f) mul = c.ambient;
                    if (c.reach >= 0.0f) reach = c.reach;
                    break;
                }
            ReleaseSRWLockShared(&g_lock);
        }
        g_mul.store(mul);
        g_min.store(lo);
        g_reach.store(reach);
        g_power.store(power);
        g_falloff.store(falloff);
        tellAt = on ? GetTickCount64() + 1000 : 0;
    }
    if (tellAt && GetTickCount64() >= tellAt) {
        tellAt = 0;
        const float own = g_seen.load(), mul = g_mul.load(), lo = g_min.load();
        const float ownReach = g_seenReach.load(), reach = g_reach.load(), power = g_power.load(), falloff = g_falloff.load();
        if (!g_hooked.load()) LogF("vrcam: cave light - '%s': the hook is not in yet", biome.c_str());
        else if (!underground) LogF("vrcam: cave light - '%s': outdoors, left alone (ambient %.2f, hero's light reach %.3f)",
                                    biome.c_str(), own, ownReach);
        else LogF("vrcam: cave light - '%s': ambient %.2f -> %.2f (x%.2f, at least %.2f); hero's light reach %.3f -> %.3f (x%.2f), "
                  "power x%.2f, falloff x%.2f", biome.c_str(), own, std::max(own * mul, lo), mul, lo, ownReach, ownReach * reach, reach, power,
                  falloff);
    }
}
}  // namespace cavelight

}  // namespace vrc
