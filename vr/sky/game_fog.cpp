// vr/sky/game_fog.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M8); what it gives: vr/sky/world_light.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The game's own height fog (vis json heightFogDensity). Of all the game's areas
// only the act 2 town has it, by day, dawn and dusk (and the character-select
// scenes). Its edge hangs at a height tied to the camera: off-screen from the
// game's camera high above, a hard line across half the picture from ours behind
// the hero - a pale veil over everything above it, moving back as the head looks
// down (2026-10-05). [render] game_height_fog=0 (the default) finds each loaded
// area's definitions in the heap and sets their density to 0; the game's frame
// fog follows on the next frame. =1 puts back what was taken.
// Its depth fog (depthFogDensity) goes too: every area in the open air has it
// (act 4's plains at 10), from a plane 870..900 out - its camera's distance from
// the ground, so what lies farther than the middle of the picture is fogged. In
// the square window that is a grey haze in all four corners, from above (F1) as
// well; from our camera it lay over the near ground in the bottom corners and up
// the sides, the hand in front of it clear (act 1, the user's screenshots,
// 2026-10-12). [render] game_depth_fog=0 (the default) sets it to 0, =1 puts it back.
// A definition, as floats from the depth fog's colour: colour (3), density,
// plane; the height fog's colour (3), density, plane - 7 floats before it the
// volumetric fog's player radius, 5 before its anisotropy. Game areas have the
// depth plane at 870..900, the menu scenes at 1..30: those are left alone.
namespace gamefog {
struct Patch { float* density; float was; };
SRWLOCK g_lock = SRWLOCK_INIT;
// what was set to 0, to put back: [0] the height fog's density, [1] the depth fog's
enum { kHeight, kDepth };
constexpr int kField[2] = {8, 3};   // the density's place in the definition
std::vector<Patch> g_patched[2];
bool KeepHeight() { return g_set.gameHeightFog.load(); }
bool KeepDepth() { return g_set.gameDepthFog.load(); }
// [debug] fog_poke: "k=v k=v", fields by their place from the depth fog's colour (as the
// log's "fog def" lines number them, | = 0) set to v in every loaded definition - to
// find which field draws the act 1 caves' veil without restarting. Taken back when changed.
std::vector<float*> g_defs;               // the definitions of the last scan (24 at most)
std::vector<Patch> g_poked;               // what the poke changed, to put back
std::wstring g_pokeWant;                  // under g_lock
std::atomic<uint32_t> g_pokeGen{0};
uint32_t g_pokedGen = ~0u;                // under g_lock
std::atomic<bool> g_busy{false};

// Before it: the volumetric fog's diffuse colour (-17..-15), emissive (-14..-12),
// density grain (-10), height fade (-8), player radius (-7), anisotropy (-5).
// The depth plane from 500: with 100 two transforms in the heap matched as well
// (planes 105 and 121, a 1 where the density sits) - found by a test, 2026-10-05.
bool Looks(const float* f) {
    if (!(f[4] >= 500.0f && f[4] <= 5000.0f && f[9] >= 1.0f && f[9] <= 1000.0f)) return false;
    if (!(f[3] >= 0.0f && f[3] <= 20.0f && f[8] >= 0.0f && f[8] <= 10.0f)) return false;   // act 4's plains: depth 10
    for (int k : {0, 1, 2, 5, 6, 7, -17, -16, -15})
        if (!(f[k] >= 0.0f && f[k] <= 1.0f)) return false;
    for (int k : {-14, -13, -12})
        if (!(f[k] >= 0.0f && f[k] <= 100.0f)) return false;
    if (!(f[0] + f[1] + f[2] > 0.3f && f[5] + f[6] + f[7] > 0.3f)) return false;   // no run of zeros
    return f[-7] >= 1.0f && f[-7] <= 1000.0f && f[-5] >= -1.0f && f[-5] <= 1.0f &&
           f[-10] >= 0.0f && f[-10] <= 10.0f && f[-8] >= 0.0f && f[-8] <= 10.0f;
}

// One heap region: each definition's height fog and depth fog, those `take` says,
// get density 0 (in out[kind]). How many, or -1 if the region went away under the scan.
// Each definition's floats from -20 to +13 go to `dump` (up to dumpCap), for the log in
// a cave: the act 1 caves have no height fog, yet a veil's hard edge crosses their floor
// at a distance from the hero (2026-10-07) - which field it is, is read from these.
constexpr int kDumpFrom = -20, kDumpN = 34;
int ScanRegion(float* p, size_t n, const bool take[2], Patch (*out)[32], int got[2], int* defs, float (*dump)[kDumpN],
               float** where, int dumpCap) {
    got[0] = got[1] = 0;
    __try {
        for (size_t i = 20; i + 14 < n && got[0] < 32 && got[1] < 32; ++i) {
            if (!(p[i + 4] >= 500.0f && p[i + 4] <= 5000.0f) || !Looks(p + i)) continue;
            if (*defs < dumpCap) { memcpy(dump[*defs], p + i + kDumpFrom, sizeof dump[0]); where[*defs] = p + i; }
            ++*defs;
            for (int k : {kHeight, kDepth}) {
                float* d = p + i + kField[k];
                if (take[k] && *d > 0.0f) {
                    out[k][got[k]++] = {d, *d};
                    *d = 0.0f;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return got[0] + got[1];
}

DWORD WINAPI ScanThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    const ULONGLONG t0 = GetTickCount64();
    int defs = 0, taken[2] = {};
    static float dump[24][kDumpN];
    static float* where[24];
    MEMORY_BASIC_INFORMATION mi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mi, sizeof mi);
         a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
        // guard pages (thread stacks) have PAGE_GUARD in Protect and are skipped with the rest
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE || mi.RegionSize > (1ull << 31))
            continue;
        // what is switched back on meanwhile is left alone (Tick puts back what a scan took after that)
        const bool take[2] = {!KeepHeight(), !KeepDepth()};
        if (!take[0] && !take[1]) break;
        Patch found[2][32];
        int got[2];
        if (ScanRegion((float*)mi.BaseAddress, mi.RegionSize / sizeof(float), take, found, got, &defs, dump, where, 24) <= 0)
            continue;
        AcquireSRWLockExclusive(&g_lock);
        for (int k : {kHeight, kDepth}) {
            g_patched[k].insert(g_patched[k].end(), found[k], found[k] + got[k]);
            taken[k] += got[k];
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
    static int toldDefs = -1;
    if (taken[0] || taken[1] || defs != toldDefs) {
        toldDefs = defs;
        LogF("vrcam: the game's fog - %d area definitions loaded; set to 0: height fog in %d, depth fog in %d (%.1f s)",
             defs, taken[kHeight], taken[kDepth], (GetTickCount64() - t0) / 1000.0);
    }
    AcquireSRWLockExclusive(&g_lock);
    g_defs.assign(where, where + std::min(defs, 24));
    g_pokedGen = ~0u;   // the new ones get [debug] fog_poke too
    ReleaseSRWLockExclusive(&g_lock);
    if (g_ceilBiome.load()) {   // in a cave: every definition's fields, -20 .. +13 from its depth fog's colour
        for (int d = 0; d < std::min(defs, 24); ++d) {
            char b[640];
            int len = snprintf(b, sizeof b, "vrcam: fog def %d:", d);
            for (int k = 0; k < kDumpN && len < (int)sizeof b - 16; ++k)
                len += snprintf(b + len, sizeof b - len, " %s%g", k + kDumpFrom == 0 ? "|" : "", dump[d][k]);
            Log(b);
        }
    }
    g_busy.store(false);
    return 0;
}

bool PutBack(const Patch& p, int kind) {
    __try {
        if (*p.density == 0.0f && Looks(p.density - kField[kind])) *p.density = p.was;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PokeOne(float* f, float v, float* was) {
    __try { *was = *f; *f = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ApplyPoke() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_pokedGen != g_pokeGen.load()) {
        g_pokedGen = g_pokeGen.load();
        for (auto it = g_poked.rbegin(); it != g_poked.rend(); ++it) { float x; PokeOne(it->density, it->was, &x); }
        g_poked.clear();
        int set = 0;
        for (const wchar_t* q = g_pokeWant.c_str(); *q;) {
            int k = 0; float v = 0.0f; int used = 0;
            if (swscanf_s(q, L" %d = %f%n", &k, &v, &used) != 2 || used <= 0) break;
            q += used;
            if (k < kDumpFrom || k >= kDumpFrom + kDumpN) continue;
            for (float* d : g_defs) { float was; if (PokeOne(d + k, v, &was)) { g_poked.push_back({d + k, was}); ++set; } }
        }
        if (!g_pokeWant.empty() || set) LogF("vrcam: [debug] fog_poke '%s' - %d fields set", Utf8(g_pokeWant.c_str()).c_str(), set);
    }
    ReleaseSRWLockExclusive(&g_lock);
}
void SetPoke(const std::wstring& w) {
    AcquireSRWLockExclusive(&g_lock);
    const bool changed = w != g_pokeWant;
    g_pokeWant = w;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) g_pokeGen.fetch_add(1);
}

// On the update thread: a scan 1, 4 and 12 s after every biome change (an area's
// definitions are loaded as it is entered), and after either fog's key is set back
// to 0, on a thread of its own.
void Tick() {
    ApplyPoke();
    static uint32_t seenGen = ~0u;
    static ULONGLONG due[3] = {};
    const ULONGLONG now = GetTickCount64();
    static bool wasKept[2] = {false, false};
    const bool keep[2] = {KeepHeight(), KeepDepth()};
    if (const uint32_t gen = g_biomeGen.load();
        gen != seenGen || (wasKept[0] && !keep[0]) || (wasKept[1] && !keep[1])) {
        seenGen = gen;
        due[0] = now + 1000; due[1] = now + 4000; due[2] = now + 12000;
    }
    wasKept[0] = keep[0]; wasKept[1] = keep[1];
    AcquireSRWLockExclusive(&g_lock);
    for (int k : {kHeight, kDepth}) {
        if (!keep[k] || g_patched[k].empty()) continue;
        int back = 0;
        for (const Patch& p : g_patched[k]) back += PutBack(p, k) ? 1 : 0;
        LogF("vrcam: the game's %s fog back on ([render] game_%s_fog=1) - %d definitions restored",
             k == kHeight ? "height" : "depth", k == kHeight ? "height" : "depth", back);
        g_patched[k].clear();
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (keep[0] && keep[1]) {
        for (ULONGLONG& d : due) d = 0;
        return;
    }
    if (!g_inWorld.load()) return;
    for (ULONGLONG& d : due) {
        if (!d || now < d || g_busy.load()) continue;
        d = 0;
        g_busy.store(true);
        if (HANDLE h = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr)) CloseHandle(h); else g_busy.store(false);
        break;
    }
}
}  // namespace gamefog

}  // namespace vrc
