// vr/world/kurast_probe.cpp - [world] kurast_boat: where the renderer DRAWS the Kurast Docks' canoe, told once a visit to the town (read only).
// Serg 2026-11-03 ("каноэ над водой не работает") while D2R VR Settings had the town's preset made with the canoe at y 0 (shared/kurast_boat.h):
// whether the game drew from that file is seen only in the game. Among the renderer's pieces (knockprops::PresetPieces): the canoe's hull by
// its place under its prefab - where it is drawn says on the water or in the sky - and the game's own placement at y 50 (prefab014), there only
// when the town came from the game's own preset. With it, what the mod's file says of the canoe. Nothing is drawn, written or asked of the game.
#include "area.h"
#include "../vrcam_internal.h"
#include "../knock_props.h"
#include "kurast_boat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vrc {
namespace kurastprobe {
namespace {

constexpr int kTries = 10;            // the town's pieces may come a moment after its biome: every 2 s, this many times
constexpr ULONGLONG kEveryMs = 2000;

struct Found {
    unsigned gen = ~0u;
    bool read = false, hull = false, sky = false;
    float world[3] = {};
    size_t pieces = 0;
    double ms = 0.0;
};
SRWLOCK g_lock = SRWLOCK_INIT;
Found g_found;   // (under g_lock)
std::atomic<bool> g_busy{false};
std::string g_modFile;   // what the mod's preset says of the canoe: read once, by the first look (the worker's; Tick reads it after that look's answer)

// The mod's preset: <mod>\<mod>.mpq\data\hd\env\preset\act3\docktown\docktown3.json beside the plugin's ini (mods\D2R-VR\d2rloader\plugins).
std::string ModFile() {
    std::wstring path = g_iniPath;
    for (int up = 0; up < 3; ++up) {
        const size_t cut = path.find_last_of(L"\\/");
        path = cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
    }
    const size_t leaf = path.find_last_of(L"\\/");
    std::wstring file = path + L"\\" + (leaf == std::wstring::npos ? path : path.substr(leaf + 1)) + L".mpq\\";
    for (const char* c = kurastboat::kPreset; *c; ++c) file += *c == '/' ? L'\\' : (wchar_t)*c;
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) return "the mod's docktown3.json is not there (D2R VR Settings makes it: [world] kurast_boat)";
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (32u << 20)) text.append(buf, n);
    fclose(f);
    double y = 0.0;
    char out[160];
    if (kurastboat::PlacedY(text, &y)) snprintf(out, sizeof out, "the mod's docktown3.json (%zu bytes) places it at y %.1f", text.size(), y);
    else snprintf(out, sizeof out, "the mod's docktown3.json (%zu bytes) has no canoe entity", text.size());
    return out;
}

DWORD WINAPI Look(void* arg) {
    const unsigned gen = (unsigned)(uintptr_t)arg;
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (g_modFile.empty()) g_modFile = ModFile();
    const float places[3][3] = {{kurastboat::kHull[0], kurastboat::kHull[1], kurastboat::kHull[2]},
                                {kurastboat::kHullFlatSky[0], kurastboat::kHullFlatSky[1], kurastboat::kHullFlatSky[2]},
                                {kurastboat::kPlacementSky[0], kurastboat::kPlacementSky[1], kurastboat::kPlacementSky[2]}};
    std::vector<knockprops::PresetPiece> got;
    Found r;
    r.gen = gen;
    r.read = knockprops::PresetPieces(places, 3, &got);
    r.pieces = got.size();
    for (const knockprops::PresetPiece& p : got) {
        if (std::fabs(p.local[0] - kurastboat::kHull[0]) < 1e-2f && std::fabs(p.local[2] - kurastboat::kHull[2]) < 1e-2f) {
            r.hull = true;
            std::memcpy(r.world, p.world, sizeof r.world);
        } else if (std::fabs(p.local[1] - kurastboat::kPlacementSky[1]) < 1e-2f) {
            r.sky = true;
        }
    }
    QueryPerformanceCounter(&t1);
    r.ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
    AcquireSRWLockExclusive(&g_lock); g_found = r; ReleaseSRWLockExclusive(&g_lock);
    g_busy.store(false);
    return 0;
}

}  // namespace

// SkyTick (the update thread's timer): a new area in the town - looked for every 2 s until the hull is found, kTries times; each answer told once.
void Tick() {
    static uint32_t seenGen = ~0u, visit = 0;
    static bool here = false, done = false;
    static int tries = 0, told = -1;
    static ULONGLONG askedAt = 0;
    if (const uint32_t gen = g_biomeGen.load(); gen != seenGen) {
        seenGen = gen;
        AcquireSRWLockShared(&g_biomeLock); const bool town = strcmp(g_biome, "act3_docktown") == 0; ReleaseSRWLockShared(&g_biomeLock);
        if (town && !here) {   // a visit: the town entered from elsewhere (the same biome set again while in it is no new one)
            ++visit;
            done = false;
            tries = 0;
            told = -1;
            askedAt = GetTickCount64();   // (the first look 2 s after the town's biome: its pieces come with the area)
        }
        here = town;
    }
    if (!here || done) return;
    Found found;
    AcquireSRWLockShared(&g_lock); found = g_found; ReleaseSRWLockShared(&g_lock);
    if (found.gen == visit && told != tries && !g_busy.load()) {
        told = tries;
        const kurastboat::Drawn d = kurastboat::Judge(found.hull, found.world[1], found.sky);
        if (found.hull)
            LogF("vrcam: kurast boat - the canoe is drawn at %.1f %.1f %.1f: %s (the game's own placement at y 50 %s; %s; %zu piece(s), %.1f ms)",
                 found.world[0], found.world[1], found.world[2],
                 d == kurastboat::kDrawnInSky ? "IN THE SKY - the town was not made from the mod's docktown3.json" : "on the water - the mod's docktown3.json is the town's",
                 found.sky ? "among the pieces" : "not among them", g_modFile.c_str(), found.pieces, found.ms);
        else
            LogF("vrcam: kurast boat - the canoe's hull NOT found among the renderer's pieces (%s; the game's own placement at y 50 %s; %s; look %d of %d)%s",
                 found.read ? "the renderer read" : "the renderer's registry does not read", found.sky ? "IS among them: the game's own preset" : "not among them",
                 g_modFile.c_str(), tries, kTries, tries < kTries ? " - again in 2 s" : "");
        if (found.hull || tries >= kTries) done = true;
    }
    const ULONGLONG now = GetTickCount64();
    if (!done && tries < kTries && !g_busy.load() && now - askedAt >= kEveryMs && (found.gen != visit || told == tries)) {
        ++tries;
        askedAt = now;
        g_busy.store(true);
        if (HANDLE th = CreateThread(nullptr, 0, Look, (void*)(uintptr_t)visit, 0, nullptr)) CloseHandle(th);
        else g_busy.store(false);
    }
}

}  // namespace kurastprobe
}  // namespace vrc
