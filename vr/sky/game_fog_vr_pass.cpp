// vr/sky/game_fog_vr_pass.cpp - [fog] game_fog: the game's own distance fog drawn by its own pass with our numbers (vrcam's gamefogvr; the pure parts are vr/game_fog_vr.h - this file's names are declared where they are used, never at vrcam.cpp's top, where they would hide the pure ones) (cut from vr/vrcam.cpp, docs/plan_modules.md - the sky group).
// It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// [fog] game_fog (vr/game_fog_vr.h): the game's own distance fog, drawn by its own pass, with our fog's numbers - VR F4 only, in the
// world, while our fog is wanted. The cave light's hook on 0xF5DAD0 (below) hands over the blend and the frame's settings after the
// game wrote its own fog there; this checks for 30 frames that the frame's fog is the blend's (the layout: vr/game_fog_vr.h SameFog),
// then writes ours over it each frame - the frame's settings only, never the area's definitions (the game writes them again every
// frame: unticked, or out of F4, its own are back at once). While it is on the post pass draws no fog of its own (fx::FogWanted).
namespace fx { bool OurFogWanted(); bool FogColourNow(float rgb[3]); }
namespace gamefogvr {
using namespace ::gamefogvr;               // (the pure parts: vr/game_fog_vr.h)
std::atomic<bool> g_on{false};             // this frame: ticked, VR F4, our fog wanted (the present sets it)
std::atomic<int> g_at{-1};                 // where the frame's fog is in its settings (a float index; -1 not found yet)
std::atomic<bool> g_bad{false};            // not found for long: nothing written (told once)
constexpr int kMatchFrames = 30, kGiveUp = 1800;
constexpr int kScanFloats = 0x800 / 4;     // the frame's settings looked through for the blend's fog (2 KiB)
int g_cand = -1, g_candN = 0, g_misses = 0;   // the game's thread: the place seen, frames in a row there; frames with nothing found
SRWLOCK g_lock = SRWLOCK_INIT;
Fog g_fog{};                               // ours, as the game keeps it (under g_lock)
float g_seen[5] = {}, g_blend[5] = {};     // the frame's at the reading's place and the blend's, the last frame looked at (under g_lock; the log)
int g_lastFind = -1;                       // the last look's answer (under g_lock): an index, -1 none, -2 more than one
bool Active() { return g_on.load(std::memory_order_relaxed) && g_at.load(std::memory_order_relaxed) >= 0 && !g_bad.load(); }
// ours as the game draws it now ([fog] game_fog_before_fx: the same numbers before the transparent resolve)
Fog Current() {
    AcquireSRWLockShared(&g_lock);
    const Fog f = g_fog;
    ReleaseSRWLockShared(&g_lock);
    return f;
}
// The game's thread, in the hook, after the game wrote the blend's fog into the frame's settings. Only while it is wanted (VR F4, in a
// world): the menus' frames have no world fog to find - counted there, the first build gave up before the camp was even entered (PC1,
// 3.3.94995, 2026-10-27: "NOT drawn ... the game's last 0 0 0 0 0" 0.01 s after the area came). The place is looked for by value
// (vr/game_fog_vr.h FindFog: the blend's five floats, exactly, at one place) and taken after 30 frames at the same place; then our
// numbers go there every frame.
void Apply(const uint8_t* vis, void* fs) {
    if (!vis || !fs || g_bad.load(std::memory_order_relaxed) || !g_on.load(std::memory_order_relaxed)) return;
    float blend[5];
    if (!::SafeRead(blend, vis + kVisFogAt, sizeof blend)) return;
    int at = g_at.load(std::memory_order_relaxed);
    if (at < 0) {
        static float frame[kScanFloats];
        if (!::SafeRead(frame, fs, sizeof frame)) return;
        const int found = FindFog(blend, frame, kScanFloats);
        AcquireSRWLockExclusive(&g_lock);
        memcpy(g_seen, frame + kFrameFogAt / 4, sizeof g_seen);
        memcpy(g_blend, blend, sizeof g_blend);
        g_lastFind = found;
        ReleaseSRWLockExclusive(&g_lock);
        if (found >= 0) {
            g_candN = found == g_cand ? g_candN + 1 : 1;
            g_cand = found;
            if (g_candN >= kMatchFrames) g_at.store(found);
        } else if (++g_misses >= kGiveUp) {
            g_bad.store(true);
        }
        return;
    }
    AcquireSRWLockShared(&g_lock);
    const Fog ours = g_fog;
    ReleaseSRWLockShared(&g_lock);
    ::SafeWriteMem((uint8_t*)fs + (size_t)at * 4, &ours, sizeof ours);
}
// The present, each frame: on or off, our numbers, and the log once a change
void Update() {
    // ([sky] game_sky's dome: the game's fog draws over it as over everything else - Serg, 2026-10-27: the fog is the game's, never our
    // post pass's; how it leaves the sky clear is the next step)
    // Under the Arcane Sanctuary's sky ([sky] arcane_fog) our post pass draws it: the fog there is the sky straight behind each point
    // (D2R_DepthFog.fx ArcaneHaze), the platforms fading into the void round them - one colour, as the game's pass takes, cannot
    const bool arcane = g_arcaneSky.load();
    const bool want = g_set.gameFog.load() && GameDrawnView() && fx::OurFogWanted() && !arcane;
    g_on.store(want);
    static int told = -1;
    static Fog toldFog{};
    // (the Arcane Sanctuary: told on every entry, whether the game's pass drew before or not - Serg, 2026-10-28: the game's fog there "is
    // worse than the one we drew")
    const bool standAside = g_set.gameFog.load() && GameDrawnView() && arcane && fx::OurFogWanted();
    static bool toldAside = false;
    if (standAside && !toldAside)
        Log("vrcam: game fog - not here: under the Arcane Sanctuary's sky our effect draws the fog, fading into that sky ([sky] arcane_fog); "
            "the game's own distance fog keeps its own numbers");
    toldAside = standAside;
    if (!want) {
        if (told == 1 && !standAside)
            Log("vrcam: game fog - the game's own distance fog back to its own numbers ([fog] game_fog off, or not VR F4, or no fog wanted)");
        told = 0;
        return;
    }
    const bool under = g_underground.load();
    Ours o{};
    o.start = under ? g_set.fogCaveStart.load() : g_set.fogStart.load();
    o.end = under ? g_set.fogCaveEnd.load() : g_set.fogEnd.load();
    o.strength = under ? g_set.fogCaveStrength.load() : 1.0f;
    fx::FogColourNow(o.rgb);
    // [fog] game_fog_caves_black: underground the distance darkens into black, whatever [fog] color_caves says - Serg (2026-10-27): "может
    // быть в пещерах просто затенять чёрным"
    if (under && g_set.fogCavesBlack.load()) o.rgb[0] = o.rgb[1] = o.rgb[2] = 0.0f;
    const Fog f = FromOurs(o);
    float seen[5], blend[5];
    int lastFind;
    AcquireSRWLockExclusive(&g_lock);
    g_fog = f;
    memcpy(seen, g_seen, sizeof seen);
    memcpy(blend, g_blend, sizeof blend);
    lastFind = g_lastFind;
    ReleaseSRWLockExclusive(&g_lock);
    if (g_bad.load()) {
        if (told != 2)
            LogF("vrcam: game fog - NOT drawn: the area's fog (%.3f %.3f %.3f, density %.3f, plane %.1f) not found in the frame's settings in %d frames "
                 "in the world (%s; at the reading's +0x%X: %.3f %.3f %.3f %.3f %.1f) - another build or layout; our own fog stays", blend[0], blend[1],
                 blend[2], blend[3], blend[4], kGiveUp, lastFind == -2 ? "found at more than one place" : "nowhere in its first 2 KiB",
                 (unsigned)kFrameFogAt, seen[0], seen[1], seen[2], seen[3], seen[4]);
        told = 2;
        return;
    }
    if (!Active()) return;   // (still looking for the frame's fog)
    if (told != 1)
        LogF("vrcam: game fog - the frame's fog found at +0x%X in the frame's settings (the reading said +0x%X)", (unsigned)(g_at.load() * 4),
             (unsigned)kFrameFogAt);
    const bool changed = told != 1 || std::fabs(f.plane - toldFog.plane) > 0.5f || std::fabs(f.density - toldFog.density) > 0.0005f ||
                         std::fabs(f.r - toldFog.r) + std::fabs(f.g - toldFog.g) + std::fabs(f.b - toldFog.b) > 0.002f;
    if (changed)
        LogF("vrcam: game fog - the game's own distance fog draws ours: from %.0f, %.0f%% at %.0f (density %.4f, plane %.1f), colour %.3f %.3f %.3f "
             "(scene; ours %.3f %.3f %.3f)%s - the area's own were density %.3f, plane %.1f; our post pass draws no fog meanwhile",
             f.plane, kFullAt * 100.0f * o.strength, o.end, f.density, f.plane, f.r, f.g, f.b, o.rgb[0], o.rgb[1], o.rgb[2], under ? " underground" : "",
             seen[3], seen[4]);
    if (changed) {   // the two side by side (Serg, act 4: the old fog hid the far, the game's "ничего не скрывает"): the share at points of the way
        auto ours = [&](float dist) { return std::pow(std::clamp((dist - o.start) / (std::max)(o.end - o.start, 1.0f), 0.0f, 1.0f), g_set.fogCurve.load()) * o.strength; };
        const bool curve = g_set.fogOurCurve.load();
        auto game = [&](float dist) { return curve ? ours(dist) : GameFogAmount(f, dist); };
        char line[400];
        int n = 0;
        for (float k : {0.25f, 0.5f, 0.75f, 1.0f, 1.5f}) {
            const float dist = o.start + (o.end - o.start) * k;
            n += snprintf(line + n, sizeof line - n, "%s%.0f: ours %.0f%% / the game's %.0f%%", n ? ", " : "", dist, ours(dist) * 100.0f, game(dist) * 100.0f);
            if (n >= (int)sizeof line) break;
        }
        const bool height = g_set.fogHeight.load() && !under;
        LogF("vrcam: game fog - the share by distance (%s): %s; %s; %s", curve ? "the game's pass by our curve, while its patched pass is bound" : "the game's exponential",
             line, height ? "the game's pass thinned above the eye from game_fog_top - game_fog_soft, none above game_fog_top" : "no height thinning",
             g_set.fogMatch.load() ? "[fog] game_fog_match makes up the rest to ours on the world's pixels (the dome untouched)" : "[fog] game_fog_match off: as the game lays it");
    }
    told = 1;
    toldFog = f;
}
}  // namespace gamefogvr

}  // namespace vrc
