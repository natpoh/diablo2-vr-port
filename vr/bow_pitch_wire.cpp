// bow_pitch_wire.cpp - see bow_pitch_wire.h. [bow] pitch_aim: a bow's or crossbow's shot aimed by its 3D line (docs/notes/bow_pitch.md).
//
// Read-only. The scene is made on vrcam's timer from reads the plugin already makes: the monsters of meleetouch's poll (the client's unit hash, the
// game's own MonStats rows, the server's hostility and wall verdicts asked ahead on the game's thread) and the models standing near (skel::MonsterRigs:
// the renderer's skeletons, their limbs). The aim hooks ask Theta at the press; the game's attack then goes its own way along the heading given.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "aim_point.h"
#include "bow_pitch_wire.h"
#include "log_async.h"
#include "melee_bodies.h"
#include "melee_touch.h"
#include "skeletons.h"
#include "view_gate.h"

namespace bowpitchwire {
namespace {

using bowpitch::V3;

constexpr ULONGLONG kScanMs = 100;    // the scene is made again this often
constexpr ULONGLONG kFreshMs = 400;   // a scene older than this decides nothing (the poll stopped: another weapon, a menu)
constexpr int kRigs = 24;             // the models read, the nearest first
constexpr float kRigReachM = 30.0f;   // ... within this many metres of the hero as drawn
constexpr float kMatchM = 0.6f;       // a model is its unit's when its place, the shear taken off, is this near the unit's cell

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false}, g_trace{false};

// The scene, and what it was made of (for the log): the timer's writes, the hooks' and the effect's reads.
struct Made {
    bowpitch::Scene sc;
    float upm = 0.0f;
    ULONGLONG at = 0;
    int polled = 0, hostile = 0, rigs = 0, matched = 0, limbed = 0;
    float matchMeanM = 0.0f;   // the matched models' mean distance from their units' cells, the shear taken off (the shear's own check)
    float heroOffM = -1.0f;    // the hero as drawn, the shear taken off, against his cell (the camera's look-at: right only when he stands)
};
SRWLOCK g_lock = SRWLOCK_INIT;
Made g_made;
// A copy to decide in, kept off the callers' stacks (9 KB): the aim hooks and the debug ray take turns with it (a shot is rare, the ray once a frame).
SRWLOCK g_workLock = SRWLOCK_INIT;
Made g_work;
ULONGLONG g_scanAt = 0;   // the timer's

// One decision per shot of the draw: the hooks ask several times for one press. Kept with its line - a shot count read in the instant between the
// release's press and its count would find the last shot's: a line that is not about that one's is decided afresh (one that is - the same aim
// again - would be decided the same). Loose enough for the hero's walk in the 0.4 s the hooks ask in.
struct Cached {
    unsigned shot = 0;
    V3 o, d;
    bool ok = false;
    float theta = 0.0f;
    bowpitch::Result r;   // what was met ([bow] arrow_flight: where our arrow flies to)
    ULONGLONG at = 0;
};
bool SameLine(const Cached& c, V3 o, V3 d) {
    const V3 a = c.o - o;
    const float dl = bowpitch::Length(d), cl = bowpitch::Length(c.d);
    return bowpitch::Dot(a, a) < 0.6f * 0.6f && dl > 1e-6f && cl > 1e-6f && bowpitch::Dot(c.d, d) / (dl * cl) > 0.998f;
}
SRWLOCK g_cacheLock = SRWLOCK_INIT;
Cached g_cache;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

V3 FromWorld(const float w[3], float upm) { return {w[0] / upm, w[1] / upm, w[2] / upm}; }

bool Copy(Made* out) {
    AcquireSRWLockShared(&g_lock);
    const bool have = g_made.at != 0;
    if (have) *out = g_made;
    ReleaseSRWLockShared(&g_lock);
    return have && GetTickCount64() - out->at <= kFreshMs && out->sc.hero && out->upm > 1e-3f;
}

bowpitch::Params ParamsFor(float upm) {
    bowpitch::Params p;
    p.maxRangeM = aimpoint::kMaxCells * 2.0f / upm;   // the game's shot point of a bow's draw is 40 cells out: nothing farther counts
    p.maxT = p.maxRangeM * 1.5f;
    return p;
}

// [debug] bow_pitch_trace: the shot's line, written into `out` (logged by the caller, out of the lock).
void Describe(const bowpitch::Result& r, const Made& m, unsigned shot, bool fresh, char* out, size_t size) {
    const char* who = shot ? "the draw's shot" : "the hand's line";
    if (!fresh) {
        snprintf(out, size, "bow pitch: %s %u - no fresh scene (the monsters' poll is not running: another weapon, a menu, not F4) - the line's own heading", who, shot);
        return;
    }
    const float cells = r.gridDist * m.upm * 0.5f;
    char what[220];
    if (r.hit == bowpitch::Hit::Monster)
        snprintf(what, sizeof what, "meets monster #%u (%s) %.1f m along it, its ground %+.2f m against the hero's%s", r.id, r.limb ? "a limb of its own skeleton" : "the game's capsule",
                 r.t, r.groundY - m.sc.heroY, r.monster >= 0 && !m.sc.m[r.monster].placed ? " (its model not matched: the hero's ground assumed)" : "");
    else if (r.hit == bowpitch::Hit::Floor)
        snprintf(what, sizeof what, "meets the ground %.1f m along it, %+.2f m against the hero's", r.t, r.groundY - m.sc.heroY);
    else
        snprintf(what, sizeof what, "meets nothing within %.0f cells", aimpoint::kMaxCells);
    char turn[160];
    if (r.turned)
        snprintf(turn, sizeof turn, "heading %.1f deg, the line's own %.1f (turned %+.1f), its cell %.1f cells out", r.theta * 57.2957795f, r.lineTheta * 57.2957795f,
                 bowpitch::TurnDeg(r), cells);
    else
        snprintf(turn, sizeof turn, "the line's own heading %.1f deg (%s)", r.lineTheta * 57.2957795f, r.why);
    snprintf(out, size, "bow pitch: %s %u - the line %s: %s | the scene %llu ms old: %d monsters polled, %d hostile, %d models read, %d matched by the shear (%.2f m off on average), "
         "%d with limbs, %d capsules, %d ground samples; the hero as drawn against his cell %.2f m",
         who, shot, what, turn, (unsigned long long)(GetTickCount64() - m.at), m.polled, m.hostile, m.rigs, m.matched, m.matchMeanM, m.limbed, m.sc.nc, m.sc.ns, m.heroOffM);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    if (g_on.load()) Log("bow pitch: on ([bow] pitch_aim) - a bow's or crossbow's shot goes to the cell of what its line meets (F4)");
}

void Configure(bool on, bool trace) {
    const bool was = g_on.exchange(on);
    g_trace.store(trace);
    meleetouch::SetBowWanted(on);   // the monsters' poll and the server's verdicts, for a bow in hand
    if (was != on && g_ctx)
        Log(on ? "bow pitch: on ([bow] pitch_aim) - a bow's or crossbow's shot goes to the cell of what its line meets (F4)"
               : "bow pitch: off - a bow's shot goes along the line's heading on the ground, as before");
}

bool On() { return g_on.load(); }

void Poll(bool want, bool bodies, uintptr_t heroUnit, const float heroRender[3], float upm) {
    const ULONGLONG now = GetTickCount64();
    if (!want || !g_on.load() || !heroRender || !(upm > 1e-3f) || !std::isfinite(upm)) {
        if (g_scanAt) {
            g_scanAt = 0;
            AcquireSRWLockExclusive(&g_lock);
            g_made = Made{};
            ReleaseSRWLockExclusive(&g_lock);
        }
        return;
    }
    if (now - g_scanAt < kScanMs) return;
    g_scanAt = now;
    Made m;
    m.at = now;
    m.upm = upm;
    bowpitch::Scene& sc = m.sc;
    float hx, hz;
    if (heroUnit && meleetouch::UnitPlace(heroUnit, &hx, &hz) && std::isfinite(heroRender[1])) {
        sc.hero = true;
        sc.heroGx = hx / upm;
        sc.heroGz = hz / upm;
        sc.heroY = heroRender[1] / upm;
        const V3 feet = bowpitch::GridToGround(sc.heroGx, sc.heroGz, sc.heroY);
        sc.s[sc.ns++] = {feet.x, feet.z, sc.heroY};
        float gx, gz;
        bowpitch::GroundToGrid(FromWorld(heroRender, upm), &gx, &gz);
        m.heroOffM = std::hypot(gx - sc.heroGx, gz - sc.heroGz);
    }
    // the monsters of the poll, the nearest first
    meleetouch::PolledMonster pm[bowpitch::kMaxMonsters];
    const int n = static_cast<int>(meleetouch::PolledMonsters(pm, bowpitch::kMaxMonsters));
    m.polled = n;
    int order[bowpitch::kMaxMonsters];
    for (int i = 0; i < n; ++i) order[i] = i;
    if (sc.hero)
        std::sort(order, order + n, [&](int a, int b) {
            return std::hypot(pm[a].x / upm - sc.heroGx, pm[a].z / upm - sc.heroGz) < std::hypot(pm[b].x / upm - sc.heroGx, pm[b].z / upm - sc.heroGz);
        });
    float cellX[bowpitch::kMaxMonsters], cellZ[bowpitch::kMaxMonsters];
    for (int k = 0; k < n; ++k) { cellX[k] = pm[order[k]].x / upm; cellZ[k] = pm[order[k]].z / upm; }
    // the models standing near, each matched to the unit it stands for by the shear; every one is a ground sample (it stands on the ground as drawn)
    static skel::MonsterRig rigs[kRigs];   // (the timer's thread only)
    int rigOf[bowpitch::kMaxMonsters];
    for (int k = 0; k < n; ++k) rigOf[k] = -1;
    if (bodies && sc.hero) {
        m.rigs = static_cast<int>(skel::MonsterRigs(heroRender[0], heroRender[2], kRigReachM * upm, rigs, kRigs));
        float sum = 0.0f;
        for (int r = 0; r < m.rigs; ++r) {
            const V3 root = FromWorld(rigs[r].root, upm);
            if (!bowpitch::Finite(root)) continue;
            if (sc.ns < bowpitch::kMaxSamples) sc.s[sc.ns++] = {root.x, root.z, root.y};
            float dist = -1.0f;
            const int k = bowpitch::MatchUnit(root, cellX, cellZ, n, kMatchM, &dist);
            if (k < 0 || rigOf[k] >= 0) continue;
            rigOf[k] = r;
            ++m.matched;
            sum += dist;
        }
        if (m.matched) m.matchMeanM = sum / static_cast<float>(m.matched);
    }
    // the hostile ones are the bodies a shot may meet: their limbs when their model was matched and has them, else the capsule the game's numbers give them
    // (BodyFor), on their own ground when their model gives it, else on the hero's; the nearest first while there is room
    for (int k = 0; k < n && sc.nm < bowpitch::kMaxMonsters; ++k) {
        const meleetouch::PolledMonster& p = pm[order[k]];
        if (!p.hostile) continue;
        ++m.hostile;
        bowpitch::Monster& mon = sc.m[sc.nm];
        mon = bowpitch::Monster{};
        mon.id = p.id;
        mon.gx = cellX[k];
        mon.gz = cellZ[k];
        const skel::MonsterRig* rig = rigOf[k] >= 0 ? &rigs[rigOf[k]] : nullptr;
        mon.placed = rig != nullptr;
        mon.feetY = rig ? rig->root[1] / upm : sc.heroY;
        const int before = sc.nc;
        if (rig && rig->n > 0 && sc.nc + rig->n <= bowpitch::kMaxCapsules) {
            for (int i = 0; i < rig->n; ++i) {
                bowpitch::Capsule& c = sc.c[sc.nc++];
                c.a = FromWorld(rig->c[i].a, upm);
                c.b = FromWorld(rig->c[i].b, upm);
                c.r = rig->c[i].radius / upm;
                c.monster = sc.nm;
                c.limb = true;
            }
            ++m.limbed;
        } else if (sc.nc < bowpitch::kMaxCapsules) {
            stroke::Body b;
            bowpitch::Capsule& c = sc.c[sc.nc];
            if (p.rowOk && meleetouch::BodyFor(p.row, p.x, p.z, mon.feetY, upm, b)) {
                c.a = {b.bottom.x, b.bottom.y, b.bottom.z};
                c.b = {b.top.x, b.top.y, b.top.z};
                c.r = b.radius;
            } else {   // no row: a man-sized body
                const V3 feet = bowpitch::GridToGround(mon.gx, mon.gz, mon.feetY);
                c.a = feet + V3{0.0f, 0.35f, 0.0f};
                c.b = feet + V3{0.0f, 1.25f, 0.0f};
                c.r = 0.35f;
            }
            c.monster = sc.nm;
            c.limb = false;
            ++sc.nc;
        }
        if (sc.nc > before) ++sc.nm;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_made = m;
    ReleaseSRWLockExclusive(&g_lock);
}

bool Theta(const float o[3], const float d[3], unsigned shot, float* theta, bowpitch::Result* met) {
    if (!g_on.load() || !viewgate::F4() || !o || !d || !theta) return false;
    const ULONGLONG now = GetTickCount64();
    const V3 lo{o[0], o[1], o[2]}, ld{d[0], d[1], d[2]};
    if (shot) {
        AcquireSRWLockShared(&g_cacheLock);
        const Cached c = g_cache;
        ReleaseSRWLockShared(&g_cacheLock);
        if (c.shot == shot && now - c.at < 2000 && SameLine(c, lo, ld)) {
            if (c.ok) *theta = c.theta;
            if (met) *met = c.r;
            return c.ok;
        }
    }
    AcquireSRWLockExclusive(&g_workLock);
    Made& m = g_work;
    const bool fresh = Copy(&m);
    bowpitch::Result r;
    if (fresh) r = bowpitch::Decide(m.sc, lo, ld, ParamsFor(m.upm));
    if (shot) {
        AcquireSRWLockExclusive(&g_cacheLock);
        g_cache = {shot, lo, ld, r.turned, r.theta, r, now};
        ReleaseSRWLockExclusive(&g_cacheLock);
    }
    char line[700] = "";
    if (g_trace.load()) {
        static ULONGLONG toldHand = 0;
        if (shot || now - toldHand >= 500) {
            if (!shot) toldHand = now;
            Describe(r, m, shot, fresh, line, sizeof line);
        }
    }
    ReleaseSRWLockExclusive(&g_workLock);
    if (line[0]) Log(line);   // (out of the lock: a log line takes a while, the debug ray is not to wait for it)
    if (r.turned) *theta = r.theta;
    if (met) *met = r;
    return r.turned;
}

bool Meets(const float o[3], const float d[3], bowpitch::Result* out) {
    if (!g_on.load() || !viewgate::F4() || !o || !d || !out) return false;
    AcquireSRWLockExclusive(&g_workLock);
    const bool fresh = Copy(&g_work);
    if (fresh) *out = bowpitch::Decide(g_work.sc, {o[0], o[1], o[2]}, {d[0], d[1], d[2]}, ParamsFor(g_work.upm));
    ReleaseSRWLockExclusive(&g_workLock);
    return fresh;
}

}  // namespace bowpitchwire
