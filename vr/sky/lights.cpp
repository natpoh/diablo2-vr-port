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

// Torches from the game itself (2026-10-07): the fire found in the picture took the depth
// of the wall behind the flame, and its light jumped about as the hero walked. Objects
// near the hero, walked to the classic D2 way - the hero's path (+0x38), its room
// (+0x20), the rooms near it (+0x00, count +0x40), each room's units (+0xA8, next
// +0x158) - the offsets MapAssist reads in D2R, confirmed on this build by a probe: the
// hero's place on the path (+2/+6, subtiles, fraction below) times 2 is vrcam's look-at,
// x to x and y to z; an object's (static path +0x10/+0x14) found a tiki torch where one
// stood. What gives light and how far is the game's Objects table (gamestate::ObjectLight).
// The hero unit is the one the attack hook last worked on: hit once in an area.
namespace fx { bool CeilWanted(); }
namespace worldobj {
template <class T> bool Rd(uintptr_t a, T* out) { return a > 0x10000 && env::SafeRead((const void*)a, out, sizeof(T)); }

SRWLOCK g_lock = SRWLOCK_INIT;
Light g_lights[kMaxLights];
int g_count = 0;
std::atomic<bool> g_known{false};   // the walk worked last time: the shader takes these, not the picture's fire

// Most torches in D2R's caves are not objects at all: the HD scenery's props, whose light
// only the renderer knows. Its lists of light points were found in the heap (2026-10-07,
// read from outside while standing at the torches): runs of world points (x, y, z) 12
// bytes apart, all at one height (6.533 for the caves' torches, 6.5 the object torch's),
// metres apart - the object torches among them. Found again every 3 s on a thread of its
// own, as the game's height fog is: runs of 3 to 64 such points within 400 units of the
// hero, 2 to 30 units over his floor (the crypts' wall torches hang at ~12.4), at least 6 apart;
// mesh vertices lie closer.
constexpr int kScanMax = 128;
SRWLOCK g_scanLock = SRWLOCK_INIT;
float g_scanPts[kScanMax][3];
int g_scanCount = 0;
// Found in 2 scans at the same place or more: a torch that stands. The Fallen's torches
// are in the renderer's lists too, and each place they ran through was kept 30 s - a
// trail of lights lit the ceiling over half the cave (2026-10-07); only what stands lights it.
bool g_scanStill[kScanMax] = {};
std::atomic<bool> g_scanBusy{false};
struct ScanArgs { float hero[3]; bool full; };
// Where the last full scan found lights: 64 KB windows of the heap (2026-10-08). Reading
// all the game's memory (~5.7 GB) every 1.5 s cost a third of a core and was suspected
// of VR dropouts on weak PCs (0.143 turned the torches off for it). Now the whole heap
// is read only after an area change (1, 4 and 12 s) and every 20 s; in between only
// these windows - the renderer rewrites its light lists in place.
constexpr uintptr_t kScanWin = 0x10000;
constexpr int kScanWinMax = 256;
uintptr_t g_scanWin[kScanWinMax];   // window starts, written and read only on the scan thread
int g_scanWinCount = 0;

// One heap region; -1 if it went away under the scan.
int ScanLightRuns(const float* p, size_t n, const float hero[3], float (*out)[3], int cap, const float** at = nullptr) {
    int got = 0;
    __try {
        for (size_t i = 0; i + 3 * 3 <= n && got < cap; ++i) {
            const float y = p[i + 1];
            if (!(y > hero[1] + 2.0f && y < hero[1] + 30.0f) || !std::isfinite(p[i]) || !std::isfinite(p[i + 2])) continue;
            // the run starting here: same height, inside the box, apart from the one before
            size_t j = i, len = 0;
            while (j + 3 <= n && len < 64) {
                const float x = p[j], yy = p[j + 1], z = p[j + 2];
                // asked the way round that a NaN fails: junk in a passing buffer held NaNs, and
                // one NaN among the lights blanked the whole ceiling (2026-10-07)
                if (!(fabsf(yy - y) <= 0.05f && fabsf(x - hero[0]) <= 400.0f && fabsf(z - hero[2]) <= 400.0f)) break;
                if (len > 0) {
                    const float dx = x - p[j - 3], dz = z - p[j - 1];
                    if (dx * dx + dz * dz < 36.0f) break;
                }
                ++len; j += 3;
            }
            if (len < 3 || len >= 64) continue;
            for (size_t k = i; k < j && got < cap; k += 3) {
                out[got][0] = p[k]; out[got][1] = p[k + 1]; out[got][2] = p[k + 2];
                if (at) at[got] = p + k;
                ++got;
            }
            i = j - 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return got;
}

DWORD WINAPI ScanThread(void* arg) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    ScanArgs a = *(ScanArgs*)arg;
    delete (ScanArgs*)arg;
    const LONGLONG t0 = QpcUs();
    static float pts[1024][3];
    static const float* at[1024];
    int count = 0;
    size_t read = 0;
    MEMORY_BASIC_INFORMATION mi{};
    auto usable = [&]() {
        return mi.State == MEM_COMMIT && mi.Type == MEM_PRIVATE && mi.Protect == PAGE_READWRITE && mi.RegionSize <= (1ull << 31);
    };
    if (a.full) {
        for (uintptr_t m = 0x10000; m < 0x7FFFFFFF0000ull && VirtualQuery((void*)m, &mi, sizeof mi) && count < 1024;
             m = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
            if (!usable()) continue;
            read += mi.RegionSize;
            const int got = ScanLightRuns((const float*)mi.BaseAddress, mi.RegionSize / sizeof(float), a.hero, pts + count, 1024 - count, at + count);
            if (got > 0) count += got;
        }
    } else {
        for (int w = 0; w < g_scanWinCount && count < 1024; ++w) {
            if (!VirtualQuery((void*)g_scanWin[w], &mi, sizeof mi) || !usable()) continue;
            // the window and 1 KB past it (a run across its end), never past the region
            const uintptr_t end = std::min(g_scanWin[w] + kScanWin + 0x400, (uintptr_t)mi.BaseAddress + mi.RegionSize);
            if (end <= g_scanWin[w]) continue;
            read += end - g_scanWin[w];
            const int got = ScanLightRuns((const float*)g_scanWin[w], (end - g_scanWin[w]) / sizeof(float), a.hero, pts + count, 1024 - count, at + count);
            if (got > 0) count += got;
        }
    }
    if (a.full) {   // the windows the next scans read: one per place a point was found
        int nw = 0;
        for (int i = 0; i < count && nw < kScanWinMax; ++i) {
            const uintptr_t w = (uintptr_t)at[i] & ~(kScanWin - 1);
            int k = 0;
            while (k < nw && g_scanWin[k] != w) ++k;
            if (k == nw) g_scanWin[nw++] = w;
        }
        g_scanWinCount = nw;
    }
    // The same light is in several of the renderer's lists (6 and more were seen for each
    // torch): one point each (within 2 units), and only what is in 3 lists or more - junk
    // in a passing buffer that happened to fit the rule is in one, and came and went: 5
    // lights one scan, 76 the next (2026-10-07).
    int unique = 0;
    static float u[1024][3];
    static int seen[1024];
    for (int i = 0; i < count; ++i) {
        int k = 0;
        while (k < unique && !(fabsf(u[k][0] - pts[i][0]) < 2.0f && fabsf(u[k][2] - pts[i][2]) < 2.0f)) ++k;
        if (k < unique) { ++seen[k]; continue; }
        memcpy(u[unique], pts[i], sizeof u[0]); seen[unique] = 1; ++unique;
    }
    int kept = 0;
    for (int k = 0; k < unique && kept < kScanMax; ++k)
        if (seen[k] >= 3) { memcpy(u[kept], u[k], sizeof u[0]); ++kept; }
    unique = kept;
    // Kept: a torch stands still, and a scan that read the lists while the renderer was
    // rewriting them found it in fewer than 3 - the light came on 10 s late and went off
    // again (2026-10-07). What a scan finds is (re)stamped; what none has found for 30 s,
    // or what is 400 units off now, goes.
    static ULONGLONG stamp[kScanMax];
    static int hits[kScanMax];
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_scanLock);
    int n = g_scanCount;
    for (int i = 0; i < unique; ++i) {
        int k = 0;
        while (k < n && !(fabsf(g_scanPts[k][0] - u[i][0]) < 2.0f && fabsf(g_scanPts[k][2] - u[i][2]) < 2.0f)) ++k;
        if (k == n && n < kScanMax) { ++n; hits[k] = 0; }
        if (k < n) { memcpy(g_scanPts[k], u[i], sizeof u[0]); stamp[k] = now; hits[k] = std::min(hits[k] + 1, 1000); }
    }
    int keep = 0, still = 0;
    for (int k = 0; k < n; ++k) {
        const bool close = fabsf(g_scanPts[k][0] - a.hero[0]) <= 400.0f && fabsf(g_scanPts[k][2] - a.hero[2]) <= 400.0f;
        if (now - stamp[k] > 30000 || !close) continue;
        memcpy(g_scanPts[keep], g_scanPts[k], sizeof g_scanPts[0]); stamp[keep] = stamp[k]; hits[keep] = hits[k];
        g_scanStill[keep] = hits[k] >= 2;
        still += g_scanStill[keep] ? 1 : 0;
        ++keep;
    }
    g_scanCount = keep;
    ReleaseSRWLockExclusive(&g_scanLock);
    unique = still;
    static int told = -1;
    static ULONGLONG toldAt = 0;
    const double ms = (QpcUs() - t0) / 1000.0;
    // every full scan, and a change; the narrow ones once a minute besides
    if (unique != told || a.full || now - toldAt > 60000) {
        told = unique; toldAt = now;
        LogF("worldobj: the renderer's lights - %d standing near the hero (%d points in its lists; %s scan, %.0f MB in %.1f ms, %d windows)",
             unique, count, a.full ? "full" : "narrow", read / 1048576.0, ms, g_scanWinCount);
    }
    g_scanBusy.store(false);
    return 0;
}

// Every 100 ms while the ceiling is drawn: the 16 nearest lights - the renderer's (found
// by the scan, wherever the hero is: his place is the look-at) and the lit objects in
// the rooms near the hero, once his unit is known (the attack hook's, or the one the
// facing hook found: no need to hit anything).
void Gather() {
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 100) return;
    last = GetTickCount64();
    // [ceiling] torches=0: no lights wanted, so no scan of the game's memory either - on a
    // weak PC its read of all the game's heap every 1.5 s was suspected of VR dropouts (0.142).
    // Nor with [ceiling] fire_from_picture: the shader finds the fire in the picture itself.
    const bool want = fx::CeilWanted() && g_set.ceilTorches.load() && !g_set.ceilPictureFire.load() && !engitem::CeilingByGame();   // (the game's ceiling: its own lights)
    g_heroWanted.store(want);
    if (!want) { g_known.store(false); return; }
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    const float heroX = L[0], heroY = L[1], heroZ = L[2];   // his feet
    static ULONGLONG lastScan = 0, lastFull = 0;
    static ULONGLONG fullDue[3] = {};
    static uint32_t scanGen = ~0u;
    const ULONGLONG tick = GetTickCount64();
    if (const uint32_t gen = g_biomeGen.load(); gen != scanGen) {   // a new area: its lights, 1, 4 and 12 s in
        scanGen = gen;
        fullDue[0] = tick + 1000; fullDue[1] = tick + 4000; fullDue[2] = tick + 12000;
    }
    if (tick - lastScan > 1500 && !g_scanBusy.exchange(true)) {
        lastScan = tick;
        bool full = tick - lastFull > 20000;
        for (ULONGLONG& d : fullDue)
            if (d && tick >= d) { d = 0; full = true; }
        if (full) lastFull = tick;
        ScanArgs* a = new ScanArgs{{heroX, heroY, heroZ}, full};
        if (HANDLE th = CreateThread(nullptr, 0, ScanThread, a, 0, nullptr)) CloseHandle(th);
        else { delete a; g_scanBusy.store(false); }
    }
    Light found[64 + kScanMax];
    float dist[64 + kScanMax];
    int n = 0, objects = 0;
    uint32_t nNear = 0;
    // what the objects are, for the log: in a crypt with a lit tripod in view none gave light (2026-10-07)
    char seen[170] = "";   // LogF holds 256
    size_t seenLen = 0;
    // the objects: the attack hook's hero, else the facing hook's - checked again, it may be stale
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    if (hero && !IsHeroUnit((void*)hero)) { g_heroSeen.store(nullptr); hero = 0; }
    if (!hero) {   // the SDK's handle of the local player, if it is the unit itself (tried, never trusted)
        const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();
        if (p > 0x10000 && IsHeroUnit((void*)p)) { hero = p; g_heroSeen.store((void*)p); }
    }
    uintptr_t path = 0, room = 0, nearRooms = 0;
    const bool monsters = g_set.ceilMonsters.load() && g_set.ceilMonsterBright.load() > 0.0f;
    if (hero && Rd(hero + 0x38, &path) && Rd(path + 0x20, &room) && Rd(room, &nearRooms) && Rd(room + 0x40, &nNear) &&
        nearRooms && nNear > 0 && nNear <= 64) {
        // The rooms near the hero's and the rooms near those: a crypt's rooms are small, and
        // the near ones held 2 objects - its torches stood a room further (2026-10-07).
        uintptr_t rooms[128];
        uint32_t nRooms = 0;
        auto addRoom = [&](uintptr_t rm) {
            if (!rm || nRooms >= 128) return;
            for (uint32_t k = 0; k < nRooms; ++k) if (rooms[k] == rm) return;
            rooms[nRooms++] = rm;
        };
        for (uint32_t r = 0; r < nNear; ++r) { uintptr_t rm = 0; if (Rd(nearRooms + r * 8, &rm)) addRoom(rm); }
        const uint32_t ring1 = nRooms;
        for (uint32_t r = 0; r < ring1; ++r) {
            uintptr_t list = 0;
            uint32_t cnt = 0;
            if (!Rd(rooms[r], &list) || !Rd(rooms[r] + 0x40, &cnt) || !list || cnt > 64) continue;
            for (uint32_t q = 0; q < cnt; ++q) { uintptr_t rm = 0; if (Rd(list + q * 8, &rm)) addRoom(rm); }
        }
        nNear = nRooms;
        for (uint32_t r = 0; r < nRooms; ++r) {
            uintptr_t rm = rooms[r], u = 0;
            if (!Rd(rm + 0xA8, &u)) continue;
            for (int k = 0; u && k < 512; ++k) {
                uint32_t ut = 0, txt = 0, mode = 0;
                uintptr_t up = 0, next = 0;
                Rd(u, &ut); Rd(u + 4, &txt); Rd(u + 0xC, &mode); Rd(u + 0x38, &up); Rd(u + 0x158, &next);
                if (ut == 2 && up) {
                    ++objects;
                    float rgb[3];
                    const int lit = gamestate::ObjectLight(txt, mode, rgb);
                    uint32_t sx = 0, sy = 0;
                    if (seenLen < sizeof seen - 40) {
                        uint32_t ox = 0, oy = 0;
                        Rd(up + 0x10, &ox); Rd(up + 0x14, &oy);
                        const int w = snprintf(seen + seenLen, sizeof seen - seenLen, " #%u mode %u lit %d at %.0f,%.0f;",
                                               txt, mode, lit, 2.0f * ox - heroX, 2.0f * oy - heroZ);
                        if (w > 0) seenLen += (size_t)w;
                    }
                    if (lit > 0 && Rd(up + 0x10, &sx) && Rd(up + 0x14, &sy)) {
                        // the middle of the subtile; nearest first, the farthest dropped when full
                        const Light l{2.0f * sx + 1.0f, heroY + 4.9f, 2.0f * sy + 1.0f, (float)lit, {rgb[0], rgb[1], rgb[2]}};
                        const float d = (l.x - heroX) * (l.x - heroX) + (l.z - heroZ) * (l.z - heroZ);
                        if (n < 64) { found[n] = l; dist[n] = d; ++n; }
                    }
                } else if (ut == 1 && monsters && up && mode != 0 && mode != 12) {
                    // The Fallen carry a torch in D2R and their shamans a staff of fire (the user's
                    // screenshots, 2026-10-07) - none of it in the game's own light data (monstats2
                    // gives the shamans 5, the Fallen nothing): a hand-held torch of our own that walks
                    // with them. MonStats rows (monstats.txt): fallen1-5 19..23, fallen6-8 642..644,
                    // fallenshaman1-5 58..62, fallenshaman6-8 645..647. Dying (0) and dead (12): none.
                    const bool fallen = (txt >= 19 && txt <= 23) || (txt >= 642 && txt <= 644);
                    const bool shaman = (txt >= 58 && txt <= 62) || (txt >= 645 && txt <= 647);
                    uint16_t fx = 0, mx = 0, fz = 0, mz = 0;
                    if ((fallen || shaman) && Rd(up + 0, &fx) && Rd(up + 2, &mx) && Rd(up + 4, &fz) && Rd(up + 6, &mz)) {
                        // smaller than a standing torch's (19), and dimmer: [ceiling] monster_brightness
                        // ("too strong a glow from them", 2026-10-07)
                        const Light l{2.0f * (mx + fx / 65536.0f), heroY + 3.5f, 2.0f * (mz + fz / 65536.0f), shaman ? 10.0f : 8.0f,
                                      {1.0f, 236.0f / 255.0f, 176.0f / 255.0f}, true};
                        const float d = (l.x - heroX) * (l.x - heroX) + (l.z - heroZ) * (l.z - heroZ);
                        if (n < 64) { found[n] = l; dist[n] = d; ++n; }
                    }
                }
                u = next;
            }
        }
    }
    {   // the renderer's lights; where one is an object's too, the object's own (its colour, its reach)
        float sp[kScanMax][3];
        bool st[kScanMax];
        AcquireSRWLockShared(&g_scanLock);
        const int m = g_scanCount;
        memcpy(sp, g_scanPts, m * sizeof sp[0]);
        memcpy(st, g_scanStill, m * sizeof st[0]);
        ReleaseSRWLockShared(&g_scanLock);
        const int objs = n;
        for (int i = 0; i < m; ++i) {
            if (!st[i]) continue;   // seen once: may be a monster's torch running past
            bool dup = false;
            for (int k = 0; k < objs && !dup; ++k) dup = fabsf(found[k].x - sp[i][0]) < 4.0f && fabsf(found[k].z - sp[i][2]) < 4.0f;
            if (dup) continue;
            found[n] = Light{sp[i][0], sp[i][1], sp[i][2], 19.0f, {1.0f, 236.0f / 255.0f, 176.0f / 255.0f}};   // a torch's
            dist[n] = (sp[i][0] - heroX) * (sp[i][0] - heroX) + (sp[i][2] - heroZ) * (sp[i][2] - heroZ);
            ++n;
        }
    }
    for (int i = 1; i < n; ++i)   // a few dozen at most: insertion sort by distance
        for (int j = i; j > 0 && dist[j] < dist[j - 1]; --j) { std::swap(dist[j], dist[j - 1]); std::swap(found[j], found[j - 1]); }
    const int keep = std::min(n, kMaxLights);
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_lights, found, keep * sizeof(Light));
    g_count = keep;
    ReleaseSRWLockExclusive(&g_lock);
    g_known.store(true);
    static int told = -1, toldObjects = -1;
    static bool toldHero = false;
    if (keep != told || objects != toldObjects || (hero != 0) != toldHero) {
        told = keep; toldObjects = objects; toldHero = hero != 0;
        LogF("worldobj: %d lights near the hero light the ceiling (%d objects in %u rooms%s)", keep, objects, nNear,
             hero ? "" : "; the hero unit not found yet - the renderer's lights only");
        if (objects) LogF("worldobj: objects (Objects.txt row, mode, light radius, place from the hero):%s", seen);
    }
}

int Lights(Light* out) {
    AcquireSRWLockShared(&g_lock);
    const int n = g_count;
    memcpy(out, g_lights, n * sizeof(Light));
    ReleaseSRWLockShared(&g_lock);
    return n;
}
}  // namespace worldobj

}  // namespace vrc
