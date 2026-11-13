// wall_collide.cpp - [weight] wall_contact_mode 2 (Bear's) (vr/wall_collide.h says what it is): the cells around the hero asked for, turned into the weight model's
// walls in the hands' frame, and the knock when the weapon strikes one.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "melee_touch.h"
#include "unleashed_wall_cells_game.h"
#include "wall_collide.h"
#include "world_contact.h"

namespace uw = unleashedwalls;
namespace hw = heft::walls;

namespace wallcollide {

namespace {

constexpr float kReachM = 2.5f;          // m round the eyes' column: an arm and the longest weapon
constexpr uint64_t kStaleMs = 3000;      // a patch older than this is not used (the sampler renews it every 500 ms)
constexpr uint64_t kStrikeGapMs = 250;   // the same cell met again within this is the same strike

LogFn g_log = nullptr;
PulseFn g_pulse = nullptr;
std::atomic<bool> g_on{false};
SRWLOCK g_frameLock = SRWLOCK_INIT;
Frame g_frame;
uint64_t g_frameAt = 0;

SRWLOCK g_noteLock = SRWLOCK_INIT;
struct Counts {
    unsigned steps = 0, walls = 0, strikes[3] = {}, pulses = 0, returns = 0, noPatch = 0;
    double us = 0.0, worstUs = 0.0;
    uw::Counts cells;
} g_n;
uint32_t g_lastId = 0;
uint64_t g_lastOnAt = 0, g_pulseAt = 0, g_reportAt = 0;
unsigned g_toldStrikes = 0;

void Log(const char* text) {
    if (g_log) g_log(text);
}
template <class... A> void LogF(const char* fmt, A... a) {
    char b[512];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

const char* KindOf(uint32_t id) {
    if (id == kFloorId) return "the floor";
    return (id & kDoorIdBit) ? "a closed door" : "a wall";
}
int KindIndex(uint32_t id) { return id == kFloorId ? 0 : (id & kDoorIdBit) ? 2 : 1; }

void Report(uint64_t now) {
    if (now - g_reportAt < 10000) return;
    g_reportAt = now;
    const uw::SampleStats s = uw::TakeStats();
    Counts n;
    AcquireSRWLockExclusive(&g_noteLock);
    n = g_n;
    g_n = Counts{};
    ReleaseSRWLockExclusive(&g_noteLock);
    if (!s.samples && !s.refused && !n.steps) return;
    const char* state = s.faulted ? "OFF for the session (a fault)" : s.state == 2 ? "the game's cell functions not found (another build?)" :
                        s.state == 3 ? "no game server here (a joined game: the weapon passes walls)" : s.state == 1 ? "read" : "not read yet (the hero not known to hit by touch?)";
    LogF("wall collide (10 s): cells %s - %u samples, %u refused, %u of %d blocked, %.0f us on the game thread; weapon steps with walls %u (no patch %u), "
         "strikes: floor %u, walls %u, closed doors %u, %u knocks felt, %u back into the hand; the window: %u walls (%u doors, %u inside a wall), left out %u objects' "
         "footprints, %u unloaded, %u outside the sample, %u over the limit; %.1f us a step (worst %.1f)",
         state, s.samples, s.refused, s.blocked, uw::kPatchCells * uw::kPatchCells, s.us, n.steps, n.noPatch, n.strikes[0], n.strikes[1], n.strikes[2], n.pulses,
         n.returns, n.cells.walls, n.cells.doors, n.cells.inside, n.cells.objects, n.cells.unknown, n.cells.uncovered, n.cells.dropped,
         n.steps ? n.us / n.steps : 0.0, n.worstUs);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx, LogFn log, PulseFn pulse) {
    g_log = log;
    g_pulse = pulse;
    uw::Init(ctx);
}

void Configure(bool on) {
    meleetouch::SetWallsWanted(on);   // (the server's unit lookup observed: the hero's server unit is found by it)
    const bool was = g_on.exchange(on);
    if (was != on) {
        Log(on ? "wall collide: on ([weight] wall_contact_mode 2, Bear's wall map) - the held weapon stops at the level's walls, closed doors and the floor (the game's own collision cells; "
                 "drawn and felt only, F4, single player or host)"
               : "wall collide: off - the weapon passes walls, as before");
        if (!on) uw::Forget();
    }
}

bool Wanted() { return g_on.load(); }

void Tick(const Frame& f) {
    const uint64_t now = GetTickCount64();
    if (!g_on.load()) return;
    AcquireSRWLockExclusive(&g_frameLock);
    g_frame = f;
    g_frameAt = now;
    ReleaseSRWLockExclusive(&g_frameLock);
    if (f.ok && f.session && std::isfinite(f.hero[0] + f.hero[1] + f.hero[2])) uw::Want(f.session, uw::CellAt(f.hero[0], f.hero[2], f.hero[1]), now);
    Report(now);
}

int Walls(const monsterbodies::View& v, hw::Wall* out, int max) {
    if (!g_on.load() || !out || max <= 0) return 0;
    LARGE_INTEGER t0, t1, fq;
    QueryPerformanceCounter(&t0);
    const uint64_t now = GetTickCount64();
    Frame f;
    uint64_t frameAt;
    AcquireSRWLockShared(&g_frameLock);
    f = g_frame;
    frameAt = g_frameAt;
    ReleaseSRWLockShared(&g_frameLock);
    uw::Patch p;
    const bool have = uw::Latest(p);
    const float upm = std::clamp(v.upm, 1.0f, 12.0f);
    const float cx = v.eye[0] / upm, cz = v.eye[2] / upm;
    int n = 0;
    uw::Counts cells;
    const bool fresh = have && f.ok && f.session && p.session == f.session && now - p.at <= kStaleMs && now - frameAt <= kStaleMs;
    if (fresh) {
        static uw::Box boxes[hw::kMaxWalls];   // (not on the pose thread's stack)
        const int nb = uw::Walls(p, f.hero[1], upm, cx, cz, kReachM, boxes, std::min(max, hw::kMaxWalls) - 1, &cells);
        for (int i = 0; i < nb && n < max; ++i) {
            const uint32_t id = CellId(boxes[i].x, boxes[i].z) | (boxes[i].kind == uw::Kind::Door ? kDoorIdBit : 0u);
            if (ToHead(v, boxes[i].lo, boxes[i].hi, boxes[i].open, id, &out[n])) ++n;
        }
        uw::Box floor;
        if (n < max && uw::Floor(f.hero[1], upm, cx, cz, &floor) && ToHead(v, floor.lo, floor.hi, floor.open, kFloorId, &out[n])) ++n;
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&fq);
    const double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)fq.QuadPart;
    AcquireSRWLockExclusive(&g_noteLock);
    if (fresh) {
        ++g_n.steps;
        g_n.us += us;
        g_n.worstUs = std::max(g_n.worstUs, us);
        g_n.cells = cells;
    } else {
        ++g_n.noPatch;
    }
    ReleaseSRWLockExclusive(&g_noteLock);
    return n;
}

void NoteWeapon(const worldcontact::WeaponNote& n) {
    if (!g_on.load()) return;
    const uint64_t now = GetTickCount64();
    bool strike = false;
    AcquireSRWLockExclusive(&g_noteLock);
    if (g_lastId && now - g_lastOnAt > kStrikeGapMs) g_lastId = 0;
    strike = n.walls > 0 && n.id != g_lastId && KnockAmplitude(n.approach) > 0.0f;
    if (n.walls > 0) { g_lastId = n.id; g_lastOnAt = now; }
    if (strike) ++g_n.strikes[KindIndex(n.id)];
    g_n.returns += (unsigned)n.returned;
    const bool pulse = strike && g_pulse && now - g_pulseAt >= 150;
    if (pulse) { g_pulseAt = now; ++g_n.pulses; }
    ReleaseSRWLockExclusive(&g_noteLock);
    if (pulse) {   // a knock in the hands that hold it (PS VR2 feels 0.7 / 45 ms; a gentler one is lost)
        const float amp = KnockAmplitude(n.approach);
        for (int side = 0; side < 2; ++side)
            if (n.holds[side]) g_pulse(side, amp, 45);
    }
    if (strike && g_toldStrikes < 12) {
        ++g_toldStrikes;
        LogF("wall collide: the weapon struck %s at %.1f m/s (%.1f cm in, put back on its face)", KindOf(n.id), n.approach, n.depthCm);
    }
}

}  // namespace wallcollide
