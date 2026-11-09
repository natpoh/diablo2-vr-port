// vr/sky/game_fog_vr_pass.cpp - [fog] game_fog: the game's own distance fog drawn by its own pass with our numbers (vrcam's gamefogvr; the pure parts are vr/game_fog_vr.h - this file's names are declared where they are used, never at vrcam.cpp's top, where they would hide the pure ones) (cut from vr/vrcam.cpp, docs/plan_modules.md - the sky group).
// It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"
#include "fog_times.h"   // [fog] game_fog_colour: ours (game_fog_<act>_dawn / _day / _dusk / _night) by the game's time, or the game's raw colour

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// [fog] game_fog (vr/game_fog_vr.h): the game's own distance fog, drawn by its own pass, with our fog's numbers - VR F4 only, in the
// world, while our fog is wanted. The cave light's hook on 0xF5DAD0 (below) hands over the blend and the frame's settings after the
// game wrote its own fog there; this checks for 30 frames that the frame's fog is the blend's (the layout: vr/game_fog_vr.h SameFog),
// then writes ours over it each frame - the frame's settings only, never the area's definitions (the game writes them again every
// frame: unticked, or out of F4, its own are back at once). While it is on the post pass draws no fog of its own (fx::FogWanted).
namespace fx { bool OurFogWanted(); bool GameFogWanted(); bool FogColourNow(float rgb[3]); bool NoGameFogUnderground(); }
namespace gamefogvr {
using namespace ::gamefogvr;               // (the pure parts: vr/game_fog_vr.h)
std::atomic<bool> g_on{false};             // this frame: ticked, VR F4, our fog wanted (the present sets it)
std::atomic<bool> g_none{false};           // ... or [fog] caves=0 underground: no fog at all written over the area's own (Active stays false)
std::atomic<int> g_at{-1};                 // where the frame's fog is in its settings (a float index; -1 not found yet)
std::atomic<bool> g_bad{false};            // not found for long: nothing written (told once)
constexpr int kMatchFrames = 30, kGiveUp = 1800;
constexpr int kScanFloats = 0x800 / 4;     // the frame's settings looked through for the blend's fog (2 KiB)
int g_cand = -1, g_candN = 0, g_misses = 0;   // the game's thread: the place seen, frames in a row there; frames with nothing found
SRWLOCK g_lock = SRWLOCK_INIT;
Fog g_fog{};                               // ours, as the game keeps it (under g_lock)
float g_seen[5] = {}, g_blend[5] = {};     // the frame's at the reading's place and the blend's, the last frame looked at (under g_lock; the log)
int g_lastFind = -1;                       // the last look's answer (under g_lock): an index, -1 none, -2 more than one
// The colour by the game's time of day (Serg: "наш туман всегда одного цвета а ночью он должен быть темным" - act 1's camp at night, the
// far huts in a pale day fog under our fixed colour; "отдельную страницу с ползунками нового тумана"). In the open air (vr/sky/fog_times.h):
// [fog] game_fog_colour=0 (the default) the act's game_fog_<act>_dawn / _day / _dusk / _night (empty: vr/sky/fog_times.h's defaults - the
// old fog's colour by day, times night_brightness at night, times the game's own change at dawn and dusk) in scene units, blended by the
// game's own time - the present works it out (g_time); =1 the game's raw colour, its three floats left as it wrote them - only our
// distances. Underground ours as before (the caves' colour, or black). g_lastBlend: the game's colour.
std::atomic<bool> g_timeOk{false};
std::atomic<float> g_time[3] = {0.0f, 0.0f, 0.0f};
std::atomic<float> g_lastBlend[3] = {0.0f, 0.0f, 0.0f};
std::atomic<float> g_lastFrame[3] = {0.0f, 0.0f, 0.0f};   // the frame's own fog colour as the game wrote it this frame, before ours (the log)
std::atomic<uint32_t> g_applied{0};   // frames our fog went into the frame's settings (the log waits for one after a change: no stale colour)
bool Active() { return g_on.load(std::memory_order_relaxed) && g_at.load(std::memory_order_relaxed) >= 0 && !g_bad.load(); }
// ours as the game draws it now ([fog] game_fog_before_fx: the same numbers before the transparent resolve)
// The five floats Apply last put into the game's frame settings - what the game's fog pass draws with (its colour as the mode wants it:
// the game's own, ours by its time, or ours as set). [fog] game_fog_before_fx's step takes exactly these (Serg 2026-11-09: with the step
// the fog was "только легкий серый оттенок" - it had our colour as set, not the game's pass's by its time)
std::atomic<float> g_wrote[5] = {};
std::atomic<uint32_t> g_wroteN{0};
bool Written(Fog* out) {
    if (!g_wroteN.load(std::memory_order_acquire)) return false;
    out->r = g_wrote[0].load(std::memory_order_relaxed);
    out->g = g_wrote[1].load(std::memory_order_relaxed);
    out->b = g_wrote[2].load(std::memory_order_relaxed);
    out->density = g_wrote[3].load(std::memory_order_relaxed);
    out->plane = g_wrote[4].load(std::memory_order_relaxed);
    return true;
}
void NoteWritten(const float colour[3], float density, float plane) {
    for (int c = 0; c < 3; ++c) g_wrote[c].store(colour[c], std::memory_order_relaxed);
    g_wrote[3].store(density, std::memory_order_relaxed);
    g_wrote[4].store(plane, std::memory_order_relaxed);
    g_wroteN.fetch_add(1, std::memory_order_release);
}
Fog Current() {
    AcquireSRWLockShared(&g_lock);
    const Fog f = g_fog;
    ReleaseSRWLockShared(&g_lock);
    return f;
}
// The colour the game's fog draws now, as Apply writes it (scene units): the game's raw blend ([fog] game_fog_colour=1), ours by the
// game's time (=0, once worked out), else ours as set - the dome's low band is baked into it ([sky] dome_fog_baked)
void ColourNow(float rgb[3]) {
    const bool open = !g_none.load(std::memory_order_relaxed) && !g_underground.load(std::memory_order_relaxed);
    if (open && g_set.gameFogColour.load(std::memory_order_relaxed)) {
        for (int c = 0; c < 3; ++c) rgb[c] = g_lastBlend[c].load(std::memory_order_relaxed);
        return;
    }
    if (open && g_timeOk.load(std::memory_order_relaxed)) {
        for (int c = 0; c < 3; ++c) rgb[c] = g_time[c].load(std::memory_order_relaxed);
        return;
    }
    const Fog f = Current();
    rgb[0] = f.r; rgb[1] = f.g; rgb[2] = f.b;
}
// The game's thread, in the hook, after the game wrote the blend's fog into the frame's settings. Only while it is wanted (VR F4, in a
// world): the menus' frames have no world fog to find - counted there, the first build gave up before the camp was even entered (PC1,
// 3.3.94995, 2026-10-27: "NOT drawn ... the game's last 0 0 0 0 0" 0.01 s after the area came). The place is looked for by value
// (vr/game_fog_vr.h FindFog: the blend's five floats, exactly, at one place) and taken after 30 frames at the same place; then our
// numbers go there every frame.
void Apply(const uint8_t* vis, void* fs) {
    if (!vis || !fs || g_bad.load(std::memory_order_relaxed) || !(g_on.load(std::memory_order_relaxed) || g_none.load(std::memory_order_relaxed)))
        return;
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
    Fog ours = g_fog;
    ReleaseSRWLockShared(&g_lock);
    for (int c = 0; c < 3; ++c) g_lastBlend[c].store(blend[c], std::memory_order_relaxed);
    float frame[3] = {0.0f, 0.0f, 0.0f};
    if (::SafeRead(frame, (uint8_t*)fs + (size_t)at * 4, sizeof frame))   // (what the game itself put there this frame - the log: the same as the blend's)
        for (int c = 0; c < 3; ++c) g_lastFrame[c].store(frame[c], std::memory_order_relaxed);
    g_applied.fetch_add(1, std::memory_order_relaxed);
    const bool open = !g_none.load(std::memory_order_relaxed) && !g_underground.load(std::memory_order_relaxed);
    if (open && g_set.gameFogColour.load(std::memory_order_relaxed)) {
        // the game's own colour: the three floats the game wrote this frame left as they are - only our density and plane go in
        ::SafeWriteMem((uint8_t*)fs + (size_t)at * 4 + 3 * sizeof(float), &ours.density, 2 * sizeof(float));
        NoteWritten(frame, ours.density, ours.plane);   // (the colour the game put there itself this frame)
        return;
    }
    if (open && g_timeOk.load(std::memory_order_relaxed)) {   // ours for the time, in the game's units
        ours.r = g_time[0].load(std::memory_order_relaxed);
        ours.g = g_time[1].load(std::memory_order_relaxed);
        ours.b = g_time[2].load(std::memory_order_relaxed);
    }
    ::SafeWriteMem((uint8_t*)fs + (size_t)at * 4, &ours, sizeof ours);
    const float rgb[3] = {ours.r, ours.g, ours.b};
    NoteWritten(rgb, ours.density, ours.plane);
}
// The present, each frame: on or off, our numbers, and the log once a change
void Update() {
    // ([sky] game_sky's dome: the game's fog draws over it as over everything else - Serg, 2026-10-27: the fog is the game's, never our
    // post pass's; how it leaves the sky clear is the next step)
    // Under the Arcane Sanctuary's sky ([sky] arcane_fog) our post pass draws it: the fog there is the sky straight behind each point
    // (D2R_DepthFog.fx ArcaneHaze), the platforms fading into the void round them - one colour, as the game's pass takes, cannot
    const bool arcane = g_arcaneSky.load();
    const bool want = GameFogDrawnView() && fx::GameFogWanted() && !arcane;   // ([fog] enabled not asked: it is our effect's fog)
    g_on.store(want);
    // [fog] caves=0 underground, the game's fog chosen: its pass would draw the area's own fog there - a fog of nothing goes over it (the
    // density 0: none at any distance), every frame as ours would; nothing else takes it for the game's fog drawing ours (Active false)
    const bool none = !want && g_set.gameFog.load() && GameFogDrawnView() && fx::NoGameFogUnderground();
    if (none) {
        AcquireSRWLockExclusive(&g_lock);
        g_fog = Fog{0.0f, 0.0f, 0.0f, 0.0f, 1000.0f};
        ReleaseSRWLockExclusive(&g_lock);
    }
    g_none.store(none);
    static bool toldNone = false;
    if (none != toldNone) {
        toldNone = none;
        Log(none ? "vrcam: game fog - none underground ([fog] caves=0): the game's own distance fog there written to nothing"
                 : "vrcam: game fog - [fog] caves=0's nothing no longer written");
    }
    static int told = -1;
    static Fog toldFog{};
    // (the Arcane Sanctuary: told on every entry, whether the game's pass drew before or not - Serg, 2026-10-28: the game's fog there "is
    // worse than the one we drew")
    const bool standAside = GameFogDrawnView() && arcane && fx::GameFogWanted();
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
    {   // [fog] game_fog_colour=0: ours for the game's time (vr/sky/fog_times.h), for the game's thread; and a line at each turn of the
        // game's lighting (or of the choice): the colour in use, in the game's units
        int vp = -1, ve = -1;
        float vf = 0.0f;
        const bool known = cavelight::VisTime(&vp, &vf, &ve);
        const int act = g_fogAct.load(std::memory_order_relaxed);
        const bool gameColour = g_set.gameFogColour.load();
        float mine[3] = {0.0f, 0.0f, 0.0f}, files[fogtimes::kPeriods][3] = {};
        bool mineOk = false;
        GameFogAct gf{};
        if (!gameColour && known && act >= 1 && act <= 6) {
            // the act's game fog colours ([fog] game_fog_<act>_dawn / _day / _dusk / _night); empty: the old fog's colour by day ([fog]
            // color_<act>, or the old fog's own choice where that is empty too), times [sky] night_brightness at night, times the game's own
            // change from its day colour at dawn and dusk (vr/sky/fog_times.h) - all in scene units, as FromOurs takes the display colours
            AcquireSRWLockShared(&g_skyCfgLock);
            gf = g_gameFogAct[act];
            const bool oldSet = g_skyAct[act].fogSet;
            float old[3];
            memcpy(old, g_skyAct[act].fog, sizeof old);
            ReleaseSRWLockShared(&g_skyCfgLock);
            if (!oldSet) fx::FogColourNow(old);
            auto scene = [](const float d[3], float out[3]) {
                const fogframe::V3 v = fogframe::SceneFromDisplay({d[0], d[1], d[2]});
                out[0] = v.r; out[1] = v.g; out[2] = v.b;
            };
            fogtimes::Set set{};
            for (int g = 0; g < fogtimes::kGroups; ++g) {
                set.set[g] = gf.set[g];
                if (gf.set[g]) scene(gf.colour[g], set.colour[g]);
            }
            float dayDisplay[3], nightDisplay[3], day[3], nightDefault[3];
            memcpy(dayDisplay, gf.set[fogtimes::kDay] ? gf.colour[fogtimes::kDay] : old, sizeof dayDisplay);
            const float nb = g_set.nightBright.load();
            for (int c = 0; c < 3; ++c) nightDisplay[c] = dayDisplay[c] * nb;   // (the old fog's night: its display colour times night_brightness)
            scene(dayDisplay, day);
            scene(nightDisplay, nightDefault);
            fogtimes::FileColours(act, set, day, nightDefault, files);
            if (ve == 2) { memcpy(mine, files[fogtimes::kPeriods - 1], sizeof mine); mineOk = true; }   // (the eclipse: the night's)
            else mineOk = fogtimes::ColourNow(vp, vf, files, mine);
            for (int c = 0; c < 3; ++c) g_time[c].store(mine[c]);
        }
        g_timeOk.store(mineOk);
        static int toldKey = -3, pendingKey = -3;
        static uint32_t pendingAt = 0;
        const int key = (known ? (ve == 2 ? 100 : vp) : -1) * 2 + (gameColour ? 1 : 0) + act * 1000 + (g_underground.load() ? 500 : 0);
        // (written once the game's thread has put our fog in at least twice since the change: the game's colour read is this area's, not
        // the last frame before the fog was found - the manager's first line read 0 0 0)
        if (!Active() || key == toldKey) pendingKey = -3;
        else if (key != pendingKey) { pendingKey = key; pendingAt = g_applied.load(); }
        if (Active() && key != toldKey && key == pendingKey && g_applied.load() - pendingAt >= 2) {
            toldKey = key;
            const char* when = !known ? "its time not read" : ve == 2 ? "under the eclipse (its event)" : vp >= 0 && vp < 6 ? fogtimes::kPeriodNames[vp] : "?";
            if (g_underground.load())
                LogF("vrcam: game fog - underground: our caves' colour as before (game_fog_colour %d)", gameColour ? 1 : 0);
            else if (gameColour)
                LogF("vrcam: game fog - the colour is the game's own ([fog] game_fog_colour=1: its three floats in the frame left as the game wrote "
                     "them, only our density and plane written), the lighting at %s: the frame's %.3f %.3f %.3f, the blend's %.3f %.3f %.3f (the "
                     "colour the fog pass takes, scene units)", when, g_lastFrame[0].load(), g_lastFrame[1].load(), g_lastFrame[2].load(),
                     g_lastBlend[0].load(), g_lastBlend[1].load(), g_lastBlend[2].load());
            else
                LogF("vrcam: game fog - the colour is ours by the game's time ([fog] game_fog_colour=0): act %d, the files' (scene) dawn1 %.4f %.4f "
                     "%.4f, dawn2 %.4f %.4f %.4f, day %.4f %.4f %.4f, dusk1 %.4f %.4f %.4f, dusk2 %.4f %.4f %.4f, night %.4f %.4f %.4f (set: dawn %d, "
                     "day %d, dusk %d, night %d - the rest the defaults); the lighting at %s: %.4f %.4f %.4f%s; the game's own there %.3f %.3f %.3f",
                     act, files[0][0], files[0][1], files[0][2], files[1][0], files[1][1], files[1][2], files[2][0], files[2][1], files[2][2],
                     files[3][0], files[3][1], files[3][2], files[4][0], files[4][1], files[4][2], files[5][0], files[5][1], files[5][2],
                     gf.set[0] ? 1 : 0, gf.set[1] ? 1 : 0, gf.set[2] ? 1 : 0, gf.set[3] ? 1 : 0, when, mine[0], mine[1], mine[2],
                     mineOk ? "" : " - the time not known: the old fog's colour meanwhile", g_lastBlend[0].load(), g_lastBlend[1].load(), g_lastBlend[2].load());
        }
    }
    const bool under = g_underground.load();
    Ours o{};
    const fx::FogNumbers n = fx::GameFogNumbersNow();   // ([fog] arcane_own: the Arcane Sanctuary's own)
    o.start = n.start;
    o.end = n.end;
    o.strength = n.strength;
    fx::FogColourNow(o.rgb);
    // [fog] game_fog_caves_black: underground the distance darkens into black, whatever [fog] color_caves says - Serg (2026-10-27): "может
    // быть в пещерах просто затенять чёрным" (not the Arcane Sanctuary with its own fog: its colour)
    if (under && g_set.fogCavesBlack.load() && !fx::ArcaneOwnFog()) o.rgb[0] = o.rgb[1] = o.rgb[2] = 0.0f;
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
        LogF("vrcam: game fog - the game's own distance fog draws ours: from %.0f, %.0f%% at %.0f (density %.4f, plane %.1f), colour %s %.3f %.3f %.3f "
             "(scene; ours %.3f %.3f %.3f)%s - the area's own were density %.3f, plane %.1f; our post pass draws no fog meanwhile",
             f.plane, kFullAt * 100.0f * o.strength, o.end, f.density, f.plane,
             under ? "ours" : g_set.gameFogColour.load() ? "the game's raw colour by its time ([fog] game_fog_colour=1: see the colour lines), not ours"
                                                         : "ours by the game's time ([fog] game_fog_colour=0: see the colour lines), here as set",
             f.r, f.g, f.b, o.rgb[0], o.rgb[1], o.rgb[2], under ? " underground" : "", seen[3], seen[4]);
    if (changed) {   // the two side by side (Serg, act 4: the old fog hid the far, the game's "ничего не скрывает"): the share at points of the way
        auto ours = [&](float dist) { return std::pow(std::clamp((dist - o.start) / (std::max)(o.end - o.start, 1.0f), 0.0f, 1.0f), n.curve) * o.strength; };
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
        LogF("vrcam: game fog - the share by distance (%s): %s; %s", curve ? "the game's pass by our curve, while its patched pass is bound" : "the game's exponential",
             line, height ? "the game's pass thinned above the eye from game_fog_top - game_fog_soft, none above game_fog_top" : "no height thinning");
    }
    told = 1;
    toldFog = f;
}
}  // namespace gamefogvr

}  // namespace vrc
