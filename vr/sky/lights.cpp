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

// The game's own lighting's time of day (vr/sky/three_skies.h VisShares): the VisualDataSystem - this hook's `sys`, the rdi its Update
// hands on - holds its period at +0x190 (0..5) and how far into it at +0x194 (0..1), which that Update blends the six vis files by just
// before it calls here. Read every frame on the game's thread; a value out of range: this build lays them elsewhere - not used.
// The 2D game's day cycle hands its period and fraction over every update (its env update 0x3529E0 emits them at 0x352B83 to the HD
// side's WeatherTranslation, D2Prism weather_translation.cpp, whose handler 0x7CD8F0 sets them here) unless the game's own
// "OverrideTimeOfDay" switch is set: the byte at +0x1A9 (its setting's handler 0x7CB650: "1" / "true"; its debug panel's checkbox) - the
// time then stays where it was (day, 0, as the system is made). Both read for the log: does the lighting follow the clock here?
constexpr size_t kVisPeriodAt = 0x190, kVisFracAt = 0x194, kVisOverrideAt = 0x1A9;
std::atomic<int> g_visPeriod{-1};          // -1 not read yet, -2 out of range (another layout)
std::atomic<float> g_visFrac{0.0f};
std::atomic<int> g_visOverride{-1};        // the game's OverrideTimeOfDay byte (0 / 1; -1 not read)
// The lighting's event look (+0x1A0 -> {.., +8 its id, +0xC on}): the 2D side's eclipse - Act 2's outdoor levels 40 - 46 while the Tainted
// Sun is on (0x352A6C signals the hub's +0x468; WeatherTranslation 0x7CADA0 turns event 2 on through 0xF5B1A0 and off through 0xF5B3F0) -
// stands in for the time of day: the area's event file (act2_town_desecrated: key light 7, red, ash) whatever the clock. -1 none.
std::atomic<int> g_visEvent{-1};
std::atomic<ULONGLONG> g_visAt{0};          // when last read (GetTickCount64)

void HookVisApply(void* sys, void* a, const uint8_t* vis, void* fs) {
    OrigVisApply(sys, a, vis, fs);
    if (sys) {
        int32_t period;
        float frac;
        memcpy(&period, (const uint8_t*)sys + kVisPeriodAt, sizeof period);
        memcpy(&frac, (const uint8_t*)sys + kVisFracAt, sizeof frac);
        const bool sane = period >= 0 && period <= 5 && frac >= 0.0f && frac <= 1.0f;   // (NaN fails too)
        g_visPeriod.store(sane ? period : -2, std::memory_order_relaxed);
        g_visFrac.store(sane ? frac : 0.0f, std::memory_order_relaxed);
        g_visOverride.store(((const uint8_t*)sys)[kVisOverrideAt], std::memory_order_relaxed);
        uintptr_t ev = 0;
        int32_t evId = -1;
        uint8_t evOn = 0;
        const bool evRead = env::SafeRead((const uint8_t*)sys + 0x1A0, &ev, sizeof ev) &&
                            (!ev || (env::SafeRead((const void*)(ev + 8), &evId, 4) && env::SafeRead((const void*)(ev + 0xC), &evOn, 1)));
        g_visEvent.store(evRead && ev && evOn ? evId : -1, std::memory_order_relaxed);
        g_visAt.store(GetTickCount64(), std::memory_order_relaxed);
    }
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

// The game's lighting's time of day as the hook last read it (within 2 s: in a game); false not read, or out of range in this build.
bool VisTime(int* period, float* frac, int* event) {
    const int p = g_visPeriod.load(std::memory_order_relaxed);
    if (p < 0 || GetTickCount64() - g_visAt.load(std::memory_order_relaxed) > 2000) return false;
    *period = p;
    *frac = g_visFrac.load(std::memory_order_relaxed);
    if (event) *event = g_visEvent.load(std::memory_order_relaxed);
    return true;
}

// ---- The lighting's time handed on (Serg: the HD lighting stayed at day in act 2 while the 2D clock ran; the manager's chain probe,
// 0.209.4: "4 the HD handler: 1782 calls (+1781), last sent day 0.537, OverrideTimeOfDay 0 | 5 the setter took it 1 times (+0) |
// 6 the lighting now day 0.000"). The 2D day cycle hands {fraction, period} to the HD side's WeatherTranslation handler 0x7CD8F0 every
// tick; unless the OverrideTimeOfDay byte (renderer +0x11909) is set, it jumps on to SetTimeOfDay through a stub slot (image +0x3E2A630)
// - and on our runs that slot's target (0xC0DE...: not SetTimeOfDay's bytes) did not set the time: only its first call took, the debug
// stepping (which calls 0xF5B190 straight) did. So after the game's own handler: if the override is off and the lighting's time is not
// what the game just sent, the game's own SetTimeOfDay (0xF5B190, its bytes checked) is called with the game's own values - what the
// handler was to do. Where the game's own call takes, nothing is done. The slot's target is told once (its first bytes, its module), so
// the cause can be read. RVAs of 3.3.94995 under D2RLoader 1.3.1; only where the bytes match.
namespace timehand {
constexpr uint64_t RVA_HANDLER = 0x7CD8F0, RVA_SETTER = 0xF5B190, RVA_STUB_SLOT = 0x3E2A630, RVA_RENDERER = 0x34F6480;
constexpr size_t kVisAt = 0x11760;
const uint8_t kSigHandler[9] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA};
const uint8_t kSetterBytes[16] = {0xF3, 0x0F, 0x11, 0x89, 0x94, 0x01, 0x00, 0x00, 0x44, 0x89, 0x81, 0x90, 0x01, 0x00, 0x00, 0xC3};
using HandlerFn = void (*)(void*, const void*);
using SetterFn = void (*)(void*, float, int);   // rcx the lighting (renderer +0x11760), xmm1 the fraction, r8d the period
HandlerFn OrigHandler = nullptr;
SetterFn g_setter = nullptr;
std::atomic<bool> g_in{false};
std::atomic<uint64_t> g_calls{0}, g_tookOwn{0}, g_handed{0};
std::atomic<int> g_lastPeriod{-1};
std::atomic<float> g_lastFrac{-1.0f};
std::atomic<int> g_firstHandover{0};   // 1 once we handed one on (told by the update thread)

bool Read(uintptr_t a, void* out, size_t n) { return a && env::SafeRead((const void*)a, out, n); }
uintptr_t Vis() {
    uintptr_t r = 0;
    return Read(d2rsig::Addr(RVA_RENDERER), &r, sizeof r) && r > 0x10000 ? r + kVisAt : 0;
}
void HookHandler(void* self, const void* ev) {
    float frac = -1.0f;
    int32_t period = -1;
    const bool evOk = Read((uintptr_t)ev, &frac, 4) && Read((uintptr_t)ev + 4, &period, 4);
    OrigHandler(self, ev);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (!evOk || period < 0 || period > 5 || !(frac >= 0.0f && frac <= 1.0f)) return;   // (as the game clamps it: not ours to judge)
    g_lastPeriod.store(period, std::memory_order_relaxed);
    g_lastFrac.store(frac, std::memory_order_relaxed);
    const uintptr_t vis = Vis();
    uint8_t over = 1;
    int32_t p = -1;
    float f = -1.0f;
    if (!vis || !Read(vis + 0x1A9, &over, 1) || over) return;   // (the override on: the game holds the time - left alone)
    if (!Read(vis + 0x190, &p, 4) || !Read(vis + 0x194, &f, 4)) return;
    if (p == period && std::fabs(f - frac) < 1e-4f) { g_tookOwn.fetch_add(1, std::memory_order_relaxed); return; }   // (the game's own call took)
    if (!g_setter) return;
    g_setter((void*)vis, frac, period);
    if (g_handed.fetch_add(1, std::memory_order_relaxed) == 0) g_firstHandover.store(1, std::memory_order_relaxed);
}
// Twice a second until in (quiet until the code is readable)
void Install() {
    static bool done = false;
    if (done || !g_ctx) return;
    if (!Matches(RVA_HANDLER, kSigHandler, sizeof kSigHandler) || !Matches(RVA_SETTER, kSetterBytes, sizeof kSetterBytes)) {
        static int looks = 0;
        if (++looks == 240) {
            done = true;
            Log("vrcam: lighting time - the HD handler or SetTimeOfDay not where 3.3.94995 has them (another build?): the game's own path as it is");
        }
        return;
    }
    done = true;
    g_setter = (SetterFn)d2rsig::Addr(RVA_SETTER);
    g_in.store(d2rsig::Hook(RVA_HANDLER, kSigHandler, sizeof kSigHandler, (void*)&HookHandler, (void**)&OrigHandler));
    uintptr_t slot = 0;
    uint8_t target[48] = {};
    const bool slotRead = Read(d2rsig::Addr(RVA_STUB_SLOT), &slot, sizeof slot) && Read(slot, target, sizeof target);
    char hex[48 * 3 + 1] = {};
    for (size_t i = 0; slotRead && i < sizeof target; ++i) snprintf(hex + i * 3, 4, "%02X ", target[i]);
    HMODULE mod = nullptr;
    wchar_t modName[MAX_PATH] = L"";
    if (slot && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)slot, &mod) && mod)
        GetModuleFileNameW(mod, modName, MAX_PATH);
    MEMORY_BASIC_INFORMATION mi{};
    if (slot) VirtualQuery((void*)slot, &mi, sizeof mi);
    LogF("vrcam: lighting time - the HD handler 0x7CD8F0 %s (after the game's own handler: the time handed to SetTimeOfDay 0xF5B190 if the "
         "game's own call did not set it); its stub slot -> %p (%s; allocation %p, %s): %s%s", g_in.load() ? "watched" : "NOT watched - hook failed",
         (void*)slot, !slotRead ? "not readable" : !memcmp(target, kSetterBytes, sizeof kSetterBytes) ? "SetTimeOfDay's own bytes" : "NOT SetTimeOfDay's bytes",
         mi.AllocationBase, *modName ? Utf8(modName).c_str() : "no module (allocated memory)", slotRead ? "first bytes " : "", hex);
}
// The update thread: the first handover told, then once in 5 minutes the counts
void Tick() {
    static ULONGLONG toldAt = 0;
    const ULONGLONG now = GetTickCount64();
    const bool first = g_firstHandover.exchange(0) == 1;
    if (!first && (!toldAt || now - toldAt < 300000)) { if (!toldAt) toldAt = now; return; }
    toldAt = now;
    static const char* const kVis[6] = {"dawn1", "dawn2", "day", "dusk1", "dusk2", "night"};
    const int p = g_lastPeriod.load();
    LogF("vrcam: lighting time - %s: the HD handler %llu calls, the game's own call set it %llu times, handed on by us %llu times (last sent %s %.3f)",
         first ? "the game's own call through its stub did NOT set the lighting's time - handed to SetTimeOfDay with the game's own values from now on"
               : "5 min", (unsigned long long)g_calls.load(), (unsigned long long)g_tookOwn.load(), (unsigned long long)g_handed.load(),
         p >= 0 && p < 6 ? kVis[p] : "?", g_lastFrac.load());
}
}  // namespace timehand

// ---- The day / night chain probe (0.209.4 test line; Serg: "надо разобраться где теряется освещение" - read only, to come out before a
// release). Every link from the 2D day cycle to the HD lighting, counted, so the first one that goes silent shows where the time is lost:
//   1 the 2D clock's period (env::Now) - logged at each turn;
//   2 the 2D side's send: 0x3529E0 (the classic env update's emit; at 0x352B83 it signals {fraction, period} on the hub's +0x448) - a
//     pass-through tap on its entry counts the calls and keeps the env's period;
//   3 the hub's switch: 0x3527B0 dispatches only while 0x3622F0's byte (image +0x2A9AEF0) is set;
//   4 the HD side's WeatherTranslation handler 0x7CD8F0 (rdx -> {float fraction, int period}; skips while the renderer's +0x11909 - the
//     OverrideTimeOfDay byte - is set): a pass-through tap counts the calls, keeps what came, and after it reads the lighting's time back
//     (did the setter take it?);
//   5 the setter: the handler jumps through the stub slot image +0x3E2A630 - its target's first bytes against SetTimeOfDay 0xF5B190's;
//   6 the lighting's time itself, the renderer (image +0x34F6480) +0x11760 +0x190 / +0x194 / +0x1A9, read once a second straight from
//     memory - also with the VisApply hook out (an A/B with [fog] game_fog=0 and [cave_light] off).
// RVAs of 3.3.94995 under D2RLoader 1.3.1; each tap goes in only where its first bytes are as expected.
namespace chain {
constexpr uint64_t RVA_EMIT = 0x3529E0, RVA_HANDLER = 0x7CD8F0, RVA_SETTER = 0xF5B190;
constexpr uint64_t RVA_HUB_ON = 0x2A9AEF0, RVA_STUB_SLOT = 0x3E2A630, RVA_RENDERER = 0x34F6480;
constexpr size_t kVisAt = 0x11760;
const uint8_t kSigEmit[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83};
const uint8_t kSigHandler[9] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA};
const uint8_t kSetterBytes[16] = {0xF3, 0x0F, 0x11, 0x89, 0x94, 0x01, 0x00, 0x00, 0x44, 0x89, 0x81, 0x90, 0x01, 0x00, 0x00, 0xC3};
using EmitFn = void (*)(void*, uint64_t, uint64_t);
using HandlerFn = void (*)(void*, const void*);
EmitFn OrigEmit = nullptr;
HandlerFn OrigHandler = nullptr;
std::atomic<uint64_t> g_emits{0}, g_handled{0}, g_taken{0};
std::atomic<int> g_emitPeriod{-1}, g_sentPeriod{-1}, g_sentOverride{-1}, g_afterPeriod{-1};
std::atomic<float> g_emitTime{0.0f}, g_sentFrac{-1.0f}, g_afterFrac{-1.0f};
std::atomic<bool> g_emitIn{false}, g_handlerIn{false};

bool Read(uintptr_t a, void* out, size_t n) { return a && env::SafeRead((const void*)a, out, n); }
uintptr_t Vis() {
    uintptr_t r = 0;
    return Read(d2rsig::Addr(RVA_RENDERER), &r, sizeof r) && r > 0x10000 ? r + kVisAt : 0;
}
void HookEmit(void* envp, uint64_t level, uint64_t act) {
    g_emits.fetch_add(1, std::memory_order_relaxed);
    int32_t period = -1;
    float t = 0.0f;
    if (Read((uintptr_t)envp, &period, 4)) g_emitPeriod.store(period, std::memory_order_relaxed);
    if (Read((uintptr_t)envp + 8, &t, 4)) g_emitTime.store(t, std::memory_order_relaxed);
    OrigEmit(envp, level, act);
}
void HookHandler(void* self, const void* ev) {
    g_handled.fetch_add(1, std::memory_order_relaxed);
    float frac = -1.0f;
    int32_t period = -1;
    Read((uintptr_t)ev, &frac, 4);
    Read((uintptr_t)ev + 4, &period, 4);
    g_sentFrac.store(frac, std::memory_order_relaxed);
    g_sentPeriod.store(period, std::memory_order_relaxed);
    const uintptr_t vis = Vis();
    uint8_t over = 0xFF;
    if (vis) Read(vis + 0x1A9, &over, 1);
    g_sentOverride.store(over == 0xFF ? -1 : over, std::memory_order_relaxed);
    OrigHandler(self, ev);
    int32_t p = -1;
    float f = -1.0f;
    if (vis && Read(vis + 0x190, &p, 4) && Read(vis + 0x194, &f, 4)) {
        g_afterPeriod.store(p, std::memory_order_relaxed);
        g_afterFrac.store(f, std::memory_order_relaxed);
        if (p == period && std::fabs(f - frac) < 1e-4f) g_taken.fetch_add(1, std::memory_order_relaxed);
    }
}
// Once the image's code is readable (twice a second until then; quiet)
void Install() {
    static bool done = false;
    if (done || !g_ctx) return;
    if (!Matches(RVA_EMIT, kSigEmit, sizeof kSigEmit) || !Matches(RVA_HANDLER, kSigHandler, sizeof kSigHandler)) {
        static int looks = 0;
        if (++looks == 240) Log("vrcam: day/night chain probe - the 2D send or the HD handler not where 3.3.94995 has them (another build?): taps not in");
        if (looks < 240) return;
        done = true;
        return;
    }
    done = true;
    g_emitIn.store(d2rsig::Hook(RVA_EMIT, kSigEmit, sizeof kSigEmit, (void*)&HookEmit, (void**)&OrigEmit));
    // (the HD handler is watched by timehand - one hook on it: its counts below)
    g_handlerIn.store(timehand::g_in.load());
    uintptr_t slot = 0;
    uint8_t target[16] = {};
    const bool slotRead = Read(d2rsig::Addr(RVA_STUB_SLOT), &slot, sizeof slot) && Read(slot, target, sizeof target);
    LogF("vrcam: day/night chain probe - taps in: the 2D send 0x3529E0 %s, the HD handler 0x7CD8F0 %s (read only, each calls the game's "
         "own); the handler's stub slot -> %p: %s", g_emitIn.load() ? "yes" : "FAILED", g_handlerIn.load() ? "yes" : "FAILED", (void*)slot,
         !slotRead ? "not readable" : !memcmp(target, kSetterBytes, sizeof target) ? "SetTimeOfDay's own bytes (0xF5B190's) - the setter"
                                                                                   : "NOT SetTimeOfDay's bytes - another function");
}
// Every update: a line at each 2D clock turn and once in 30 s - every link's count and value
void Tick() {
    static int toldClock = -3;
    static ULONGLONG toldAt = 0;
    static uint64_t lastEmits = 0, lastHandled = 0, lastTaken = 0;
    const ULONGLONG now = GetTickCount64();
    int light = 0, period = -1;
    if (!env::Now(&light, &period)) period = -1;
    if (period == toldClock && now - toldAt < 30000) return;
    const bool turn = period != toldClock;
    toldClock = period;
    toldAt = now;
    uint8_t hub = 0xFF;
    Read(d2rsig::Addr(RVA_HUB_ON), &hub, 1);
    const uintptr_t vis = Vis();
    int32_t vp = -1;
    float vf = -1.0f;
    uint8_t over = 0xFF;
    const bool visRead = vis && Read(vis + 0x190, &vp, 4) && Read(vis + 0x194, &vf, 4) && Read(vis + 0x1A9, &over, 1);
    const uint64_t e = g_emits.load(), h = timehand::g_calls.load(), t = timehand::g_tookOwn.load(), handed = timehand::g_handed.load();
    g_handlerIn.store(timehand::g_in.load());
    static const char* const kClock[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
    static const char* const kVis[6] = {"dawn1", "dawn2", "day", "dusk1", "dusk2", "night"};
    auto clockName = [](int p) { return p >= 0 && p < 6 ? kClock[p] : "not known"; };
    auto visName = [](int p) { return p >= 0 && p < 6 ? kVis[p] : "?"; };
    LogF("vrcam: day/night chain (%s) - 1 the 2D clock %s, light %d | 2 the 2D send %s: %llu calls (+%llu), the env's period %s, time %.0f | "
         "3 the hub's switch %d | 4 the HD handler %s: %llu calls (+%llu), last sent %s %.3f | 5 the game's own call set it %llu times (+%llu), "
         "handed on by us %llu | 6 the lighting now %s %.3f, OverrideTimeOfDay %d",
         turn ? "the clock turned" : "30 s", clockName(period), light, g_emitIn.load() ? "tapped" : "NOT tapped", (unsigned long long)e,
         (unsigned long long)(e - lastEmits), clockName(g_emitPeriod.load()), g_emitTime.load(), hub == 0xFF ? -1 : hub,
         g_handlerIn.load() ? "watched" : "NOT watched", (unsigned long long)h, (unsigned long long)(h - lastHandled),
         visName(timehand::g_lastPeriod.load()), timehand::g_lastFrac.load(), (unsigned long long)t, (unsigned long long)(t - lastTaken),
         (unsigned long long)handed, visRead ? visName(vp) : "not readable", vf, over == 0xFF ? -1 : over);
    lastEmits = e;
    lastHandled = h;
    lastTaken = t;
}
}  // namespace chain

// Twice a second: the hook once it is wanted and its page is decrypted (quiet until then).
void Install() {
    timehand::Install();   // (the lighting's time handed on: whatever the settings say)
    chain::Install();   // (the day / night chain probe: whatever the settings say)
    static bool failed = false;
    // ([fog] game_fog rides on it too, and [sky] game_sky reads the game's lighting's time of day from it - VisTime)
    if (g_hooked.load() || failed || !g_ctx || !(g_on.load() || g_set.gameFog.load() || g_set.gameSky.load())) return;
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
        LogF("vrcam: cave light hook in - [cave_light] lights caves and dungeons (ambient %s, hero's light %s); the game's lighting's time of "
             "day read from it for [sky] game_sky's pictures", g_ibl.load() ? "yes" : "NOT FOUND", g_hero.load() ? "yes" : "NOT FOUND");
    else
        Log("vrcam: cave light hook FAILED - [cave_light] does nothing");
}

// Every update: the area's multipliers and floor (only off [sky] outdoor); to the log a
// second after each change, with the game's own values the hook saw.
void Tick(const std::string& biome, bool underground) {
    timehand::Tick();
    chain::Tick();
    {   // the game's lighting's time of day, written as it turns (and its override switch) - with the 2D clock beside it; and once in each
        // new area a second in (the clock is read only in a world: at the menu it is "not known")
        static int toldP = -3, toldO = -3, toldClock = -3;
        static uint32_t toldArea = ~0u;
        static ULONGLONG toldAt = 0, areaAt = 0;
        const ULONGLONG now = GetTickCount64();
        const int p = g_visPeriod.load(std::memory_order_relaxed), o = g_visOverride.load(std::memory_order_relaxed);
        const bool fresh = now - g_visAt.load(std::memory_order_relaxed) <= 2000;
        const uint32_t area = g_biomeGen.load(std::memory_order_relaxed);
        if (area != toldArea) { toldArea = area; areaAt = now; }
        int clockLight = 0, clockPeriod = -1;
        if (!env::Now(&clockLight, &clockPeriod)) clockPeriod = -1;
        const bool areaDue = areaAt && now - areaAt >= 1000;
        if (fresh && (p != toldP || o != toldO || clockPeriod != toldClock || areaDue) && now - toldAt >= 1000) {
            toldP = p;
            toldO = o;
            toldClock = clockPeriod;
            areaAt = 0;
            toldAt = now;
            static const char* const kVis[6] = {"dawn1", "dawn2", "day", "dusk1", "dusk2", "night"};
            static const char* const kClock[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
            int light = 0, period = -1;
            const bool clock = env::Now(&light, &period);
            const int ev = g_visEvent.load(std::memory_order_relaxed);
            LogF("vrcam: the game's lighting - time of day %s, %.2f into it; its event look %s; its OverrideTimeOfDay %s; the 2D clock %s, light %d "
                 "(area '%s')", p >= 0 && p < 6 ? kVis[p] : "out of range (another build)", g_visFrac.load(std::memory_order_relaxed),
                 ev < 0 ? "none" : ev == 2 ? "2 ON - the eclipse (the Tainted Sun): the area's event file whatever the time" : "ON (another event)",
                 o < 0 ? "not read" : o ? "ON - the lighting does not follow the clock" : "off", clock && period >= 0 && period < 6 ? kClock[period] : "not known",
                 light, biome.c_str());
        }
    }
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
