// arrow_flight_wire.cpp - see arrow_flight_wire.h. [bow] arrow_flight: the hero's shot arrow drawn by us (docs/notes/arrow_flight.md).
//
// The hooks are vr/potion_hide.cpp's: its visual-init callback hook shows us every entity the renderer makes with the unit it belongs to - the hero's arrow
// missile's entities of a shot noted here (its window: arrowflight::kWindowMs after the press) are taken; its world-matrix hook hands us every entity's
// matrix after the original computed it - for the root entity of a taken arrow the matrix is written again where arrowflight::Step puts ours, and the
// arrow's other entities are carried with it unless the function already computed them from the root we moved. Nothing else is written, ever.
//
// Safety, as the potion hide's: every read and write of the game's memory is behind an SEH guard, a fault turns the option off for the session (the game's
// arrow is drawn as ever again), in any other view than F4 nothing is written, and with the box unticked the hooks find nothing of ours to do.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "arrow_flight.h"
#include "arrow_flight_wire.h"
#include "log_async.h"
#include "potion_hide.h"
#include "view_gate.h"

namespace arrowflightwire {
namespace {

using arrowflight::V3;

constexpr int kSlots = 12;               // arrows at once (a Multiple Shot is up to two dozen at the top: the rest are drawn by the game, as ever)
constexpr int kEnts = 8;                 // entities of one arrow (its root, the mesh, an effect, a light)
constexpr uint64_t kIdleMs = 400;        // an arrow whose matrix was not asked for this long is gone (the game destroyed its missile)
constexpr uint64_t kMaxLifeMs = 12000;   // ... or this long after it was made (a stuck arrow's missile flies on unseen to its range: 50 cells at 24 a tick is ~2 s)
constexpr uint64_t kNewFrameUs = 4000;   // a matrix asked for again within this is the same frame's (the function computes a parent once for each of its children)

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false}, g_trace{false}, g_fxOff{false}, g_faulted{false};
std::atomic<bool> g_straight{false};   // [bow] arrow_flight_straight: the shot's own arrow along its line when nothing was met (arrowflight::Params::straight)
bool (*g_heroId)(uint32_t*) = nullptr;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[600];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

// The shot: written at the game's read of its aim, read by the visual-init look.
SRWLOCK g_shotLock = SRWLOCK_INIT;
arrowflight::Shot g_shot;
std::atomic<uint64_t> g_shotAt{0};   // (the look's cheap gate: no shot within its window, no arrow to take)

struct Ent {
    uint32_t id = 0;
    bool root = false;
    int fx = -2;                  // potionhide::EntityEffect's answer (-2 not asked yet)
    float last[16] = {}, prev[16] = {};
    bool haveLast = false, havePrev = false;
    int64_t lastUs = 0;
};
struct Slot {
    bool used = false;
    uint32_t unit = 0, cls = 0;
    uint64_t createdMs = 0, seenMs = 0;
    arrowflight::Track tr;
    Ent e[kEnts];
    int n = 0;
    float gameRoot[16] = {}, ourRoot[16] = {};
    bool haveRoot = false;
    // what its flight was, for the trace's summary
    unsigned frames = 0, writes = 0, follows = 0, already = 0, fails = 0;
    float travel = 0.0f, u = 0.0f, startGap = -1.0f;
    bool aimed = false, stuck = false, toldFirst = false;
    arrowflight::Mode mode = arrowflight::Mode::Blend;
    char first[260] = "";
};
SRWLOCK g_lock = SRWLOCK_INIT;
Slot g_slots[kSlots];
std::array<std::atomic<uint32_t>, kSlots * kEnts> g_ids;   // the taken entities, slot by slot (0xFFFFFFFF none): the matrix hook's lock-free look
std::atomic<int> g_live{0};                                // slots in use
std::atomic<unsigned> g_otherTold{0};

constexpr uint32_t kNone = 0xFFFFFFFFu;

int64_t NowUs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (int64_t)((double)t.QuadPart * 1e6 / (double)f.QuadPart);
}

const char* ClassName(uint32_t c) {
    using namespace arrowflight;
    switch (c) {
        case kArrow: return "arrow";
        case kFireArrow: return "Fire Arrow";
        case kMagicArrow: return "Magic Arrow";
        case kIceArrow: return "Ice Arrow";
        case kBolt: return "bolt";
        case kColdArrow: return "Cold Arrow";
        case kExplodingArrow: return "Exploding Arrow";
        case kImmolationArrow: return "Immolation Arrow";
        case kGuidedArrow: return "Guided Arrow";
        case kFreezingArrow: return "Freezing Arrow";
        case kMultiArrow: return "Multiple Shot arrow";
        case kMultiBolt: return "Multiple Shot bolt";
        case kStrafeArrow: return "Strafe arrow";
        case kStrafeBolt: return "Strafe bolt";
        default: return "missile";
    }
}

bool ReadUnit(uintptr_t unit, arrowflight::Unit* u) noexcept {
    if (unit < 0x10000) return false;
    __try {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(unit);
        u->type = *reinterpret_cast<const uint32_t*>(p + 0x0);
        u->cls = *reinterpret_cast<const uint32_t*>(p + 0x4);
        u->id = *reinterpret_cast<const uint32_t*>(p + 0x8);
        u->flags = *reinterpret_cast<const uint32_t*>(p + 0x128);
        u->ownerType = *reinterpret_cast<const uint32_t*>(p + 0xE8);
        u->ownerId = *reinterpret_cast<const uint32_t*>(p + 0xEC);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// the TransformComponent's world matrix (+0x40, what the draw reads) and the previous frame's (+0x80, the motion vectors): potion_hide.cpp
constexpr size_t kCompWorld = 0x40, kCompPrev = 0x80;

bool ReadM(uintptr_t at, float m[16]) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(m, reinterpret_cast<const void*>(at), sizeof(float) * 16); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 1 written, -1 a fault.
int WriteM(uintptr_t out, uintptr_t comp, const float m[16], const float prev[16]) noexcept {
    if (out < 0x10000 || comp < 0x10000) return -1;
    __try {
        std::memcpy(reinterpret_cast<void*>(out), m, sizeof(float) * 16);
        std::memcpy(reinterpret_cast<void*>(comp + kCompWorld), m, sizeof(float) * 16);
        std::memcpy(reinterpret_cast<void*>(comp + kCompPrev), prev, sizeof(float) * 16);   // (what was drawn the frame before: no streak from the game's place)
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    LogF("arrow flight: a fault in %s - off for this session, the game's arrows are drawn by the game again", where);
}

int Find(uint32_t entity) {
    for (int i = 0; i < kSlots * kEnts; ++i)
        if (g_ids[i].load(std::memory_order_relaxed) == entity) return i;
    return -1;
}

// (under g_lock)
void Free(Slot& s, int k) {
    for (int j = 0; j < kEnts; ++j) g_ids[k * kEnts + j].store(kNone);
    if (s.used) g_live.fetch_sub(1);
    s = Slot{};
}

// (under g_lock) the summary of a slot's flight, for the trace, written into `out`
void Summary(const Slot& s, char* out, size_t size) {
    int fxHas = 0, fxNone = 0;
    for (int j = 0; j < s.n; ++j) {
        if (s.e[j].fx > 0) ++fxHas;
        else if (s.e[j].fx == 0) ++fxNone;
    }
    const float upm = s.tr.shot.upm > 1e-4f ? s.tr.shot.upm : 1.0f;
    snprintf(out, size,
             "arrow flight: the hero's %s #%u is done - %u frames drawn by us (%u matrices written), %s; the game's arrow came %.1f m; ours began %.2f m from where the "
             "game's began; %d entit%s (the root and %d more: %u carried with it, %u already moved by the game's own parenting, %u could not be), %d with an effect "
             "component%s, %d without",
             ClassName(s.cls), s.unit, s.frames, s.writes,
             s.aimed ? (s.stuck ? "aimed and STUCK in the ground where the line met it" : s.tr.shot.monster ? "aimed along the line at the monster it met" : "aimed along the line at the ground it met")
                     : s.mode == arrowflight::Mode::Line ? "straight along the shot's line (it met nothing)"
                                                         : "joined the game's own path (not the shot's own arrow: a side arrow, a homing one, or its heading not the one given)",
             s.travel / upm, s.startGap / upm, s.n, s.n == 1 ? "y" : "ies", s.n - 1, s.follows, s.already, s.fails, fxHas,
             g_fxOff.load() ? " (switched off: [debug] arrow_flight_fx_off)" : "", fxNone);
}

void OnVisual(uint32_t entity, uintptr_t unit) {
    if (!g_on.load(std::memory_order_relaxed) || g_faulted.load(std::memory_order_relaxed)) return;
    const uint64_t now = GetTickCount64();
    const uint64_t at = g_shotAt.load(std::memory_order_relaxed);
    const bool windowed = at && now >= at && now - at <= arrowflight::kWindowMs;
    if (!windowed && g_live.load(std::memory_order_relaxed) == 0) return;   // the usual call: no shot of ours, nothing of ours in flight
    arrowflight::Unit u;
    if (!ReadUnit(unit, &u)) return;
    uint32_t hero = 0;
    if (g_heroId) { uint32_t id = 0; if (g_heroId(&id)) hero = id; }
    if (windowed && viewgate::F4() && arrowflight::IsHeroArrow(u, hero)) {
        AcquireSRWLockShared(&g_shotLock);
        const arrowflight::Shot sh = g_shot;
        ReleaseSRWLockShared(&g_shotLock);
        if (!arrowflight::Armed(sh, now)) return;
        int slot = -1, ent = -1;
        bool fresh = false;
        AcquireSRWLockExclusive(&g_lock);
        for (int k = 0; k < kSlots && slot < 0; ++k)
            if (g_slots[k].used && g_slots[k].unit == u.id) slot = k;
        if (slot < 0) {
            for (int k = 0; k < kSlots && slot < 0; ++k)
                if (!g_slots[k].used) slot = k;
            if (slot >= 0) {
                Slot& s = g_slots[slot];
                s = Slot{};
                s.used = true;
                s.unit = u.id;
                s.cls = u.cls;
                s.createdMs = s.seenMs = now;
                s.tr.shot = sh;
                s.tr.cls = u.cls;
                g_live.fetch_add(1);
                fresh = true;
            }
        }
        if (slot >= 0) {
            Slot& s = g_slots[slot];
            bool have = false;
            for (int j = 0; j < s.n; ++j) have = have || s.e[j].id == entity;
            if (!have && s.n < kEnts) {
                ent = s.n++;
                s.e[ent] = Ent{};
                s.e[ent].id = entity;
                s.e[ent].root = ent == 0;
                g_ids[slot * kEnts + ent].store(entity);
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
        if (ent < 0) {
            if (slot < 0 && g_trace.load() && g_otherTold.fetch_add(1) < 6)
                LogF("arrow flight: the hero's %s #%u (entity %08X) - no room for it (%d arrows already ours): the game draws it", ClassName(u.cls), u.id, entity, kSlots);
            return;
        }
        // the effect component (an elemental arrow's fire, frost, sparks): asked of the game's own storage; [debug] arrow_flight_fx_off switches it off
        const int fx = potionhide::EntityEffect(entity, g_fxOff.load());
        AcquireSRWLockExclusive(&g_lock);
        if (g_slots[slot].used && g_slots[slot].unit == u.id && ent < g_slots[slot].n && g_slots[slot].e[ent].id == entity) g_slots[slot].e[ent].fx = fx;
        ReleaseSRWLockExclusive(&g_lock);
        if (g_trace.load())
            LogF("arrow flight: the hero's %s #%u (class %u) - entity %08X %s, %s%s: drawn by us%s", ClassName(u.cls), u.id, u.cls, entity,
                 ent == 0 ? "(its root: the matrix we place)" : "(another of its entities: carried with the root)",
                 fx > 0 ? "an effect component" : fx == 0 ? "no effect component" : "its effect component not readable",
                 fx > 0 && g_fxOff.load() ? " (switched off: arrow_flight_fx_off)" : "",
                 fresh ? (sh.target ? (sh.monster ? " - the shot's line met a monster" : sh.stick ? " - the shot's line met the ground (known there: it sticks)" : " - the shot's line met the ground (a guess there: it flies on)")
                               : " - the shot's line met nothing: from the bow onto the game's own path")
                       : "");
        return;
    }
    if (g_live.load(std::memory_order_relaxed) != 0) {   // an entity of ours that now belongs to another unit (an index the game reused): ours no more
        const int i = Find(entity);
        if (i >= 0) {
            char b[600];
            bool told = false;
            AcquireSRWLockExclusive(&g_lock);
            Slot& s = g_slots[i / kEnts];
            if (s.used && s.unit != u.id) {
                if (g_trace.load()) { Summary(s, b, sizeof b); told = true; }
                Free(s, i / kEnts);
            }
            ReleaseSRWLockExclusive(&g_lock);
            if (told) Log(b);
        }
    }
    if (windowed && g_trace.load() && u.type == arrowflight::kTypeMissile && !arrowflight::IsArrowClass(u.cls) && (u.flags & arrowflight::kFlagHasOwner) &&
        u.ownerType == arrowflight::kTypePlayer && (hero == 0 || u.ownerId == hero) && g_otherTold.fetch_add(1) < 6)
        LogF("arrow flight: the hero's missile #%u class %u (entity %08X) in a shot's window is not an arrow or bolt - the game draws it (a hit's explosion, a cloud...)",
             u.id, u.cls, entity);
}

void OnXform(uint32_t entity, uintptr_t comp, uintptr_t out) {
    if (g_live.load(std::memory_order_relaxed) == 0 || g_faulted.load(std::memory_order_relaxed)) return;
    const int i = Find(entity);
    if (i < 0 || !viewgate::F4()) return;   // (another view: the game's own place)
    float m[16];
    if (!ReadM(out, m)) { Fault("reading an arrow's matrix"); return; }
    if (!arrowflight::FiniteM(m)) return;
    const uint64_t now = GetTickCount64();
    const int64_t us = NowUs();
    float w[16], prev[16];
    bool write = false;
    char first[260] = "";
    AcquireSRWLockExclusive(&g_lock);
    Slot& s = g_slots[i / kEnts];
    Ent& e = s.e[i % kEnts];
    if (s.used && e.id == entity) {
        s.seenMs = now;
        if (e.root) {
            arrowflight::Params prm;
            prm.straight = g_straight.load(std::memory_order_relaxed);
            const arrowflight::Pose p = arrowflight::Step(s.tr, arrowflight::Place(m), prm);
            if (arrowflight::Compose(m, p.gameDir, p.dir, p.pos, w)) {
                std::memcpy(s.gameRoot, m, sizeof m);
                std::memcpy(s.ourRoot, w, sizeof w);
                s.haveRoot = true;
                write = true;
                s.travel = p.travel;
                s.u = p.u;
                s.aimed = p.aimed;
                s.stuck = p.stuck;
                s.mode = p.mode;
                if (s.startGap < 0.0f) s.startGap = arrowflight::Length(s.tr.shot.start - arrowflight::Place(m));
                if (!s.toldFirst) {   // the game's own matrix at its first frame: its scale, and which of its axes lies along the heading it was given
                    s.toldFirst = true;
                    const V3 h = arrowflight::Heading(s.tr.shot.theta);
                    float len[3], best = -2.0f;
                    int axis = -1;
                    for (int r = 0; r < 3; ++r) {
                        const V3 a{m[r * 4], m[r * 4 + 1], m[r * 4 + 2]};
                        len[r] = arrowflight::Length(a);
                        const float c = len[r] > 1e-6f ? std::fabs(arrowflight::Dot(a, h)) / len[r] : 0.0f;
                        if (c > best) { best = c; axis = r; }
                    }
                    const float upm = s.tr.shot.upm > 1e-4f ? s.tr.shot.upm : 1.0f;
                    const V3 g = arrowflight::Place(m), hero = s.tr.shot.hero;
                    snprintf(first, sizeof first,
                             "arrow flight: #%u's first frame - the game's arrow %.2f m from the hero (%.2f m above his ground), ours %.2f m from it; its matrix's axes %.3f %.3f %.3f "
                             "long, axis %d along the heading given (|cos| %.2f)",
                             s.unit, arrowflight::Length(V3{g.x - hero.x, 0.0f, g.z - hero.z}) / upm, (g.y - hero.y) / upm, s.startGap / upm, len[0], len[1], len[2], axis, best);
                }
            } else {
                ++s.fails;
            }
        } else if (s.haveRoot) {
            const float upm = s.tr.shot.upm > 1e-4f ? s.tr.shot.upm : 1.0f;
            if (arrowflight::AlreadyFollows(m, s.gameRoot, s.ourRoot, 0.01f * upm)) ++s.already;
            else if (arrowflight::Follow(m, s.gameRoot, s.ourRoot, w)) { write = true; ++s.follows; }
            else ++s.fails;
        }
        if (write) {
            if (e.haveLast && us - e.lastUs > (int64_t)kNewFrameUs) {   // a new frame: what was drawn last is the previous
                std::memcpy(e.prev, e.last, sizeof e.prev);
                e.havePrev = true;
                if (e.root) ++s.frames;
            }
            if (!e.haveLast && e.root) ++s.frames;
            std::memcpy(prev, e.havePrev ? e.prev : w, sizeof prev);
            std::memcpy(e.last, w, sizeof w);
            e.haveLast = true;
            e.lastUs = us;
            ++s.writes;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (first[0] && g_trace.load()) Log(first);
    if (write && WriteM(out, comp, w, prev) < 0) Fault("writing an arrow's matrix");
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    for (auto& id : g_ids) id.store(kNone);
    potionhide::SetVisualObserver(&OnVisual);
    potionhide::SetXformWriter(&OnXform);
    if (g_on.load()) Log("arrow flight: on ([bow] arrow_flight) - the hero's shot arrow is drawn by us from the bow along the shot's line; the game's missile is the game's (F4)");
}

void SetHeroIdSource(bool (*fn)(uint32_t* id)) { g_heroId = fn; }

void Configure(bool on, bool trace, bool fxOff, bool straight) {
    const bool was = g_on.exchange(on);
    g_trace.store(trace);
    g_fxOff.store(fxOff);
    g_straight.store(straight);
    potionhide::WantHooksForArrows(on);
    if (was != on && g_ctx)
        Log(on ? "arrow flight: on ([bow] arrow_flight) - the hero's shot arrow is drawn by us from the bow along the shot's line; the game's missile is the game's (F4)"
               : "arrow flight: off - the game draws its arrows, as before");
    if (!on) g_shotAt.store(0);
}

bool On() { return g_on.load() && !g_faulted.load(); }

void NoteShot(unsigned shot, const float o[3], const float d[3], float theta, const bowpitch::Result* met, const float heroRender[3], float upm) {
    if (!On() || !viewgate::F4() || !o || !d || !heroRender || !(upm > 1e-4f) || !std::isfinite(upm) || !std::isfinite(theta)) return;
    const uint64_t now = GetTickCount64();
    arrowflight::Shot s;
    s.ok = true;
    s.atMs = now;
    s.shot = shot;
    s.start = V3{o[0], o[1], o[2]} * upm;
    s.dir = arrowflight::Unit3(V3{d[0], d[1], d[2]}, V3{std::cos(theta), 0.0f, std::sin(theta)});
    s.theta = theta;
    s.hero = V3{heroRender[0], heroRender[1], heroRender[2]};
    s.upm = upm;
    if (met && met->turned && met->hit != bowpitch::Hit::None && bowpitch::Finite(met->at)) {
        s.target = true;
        s.at = met->at * upm;
        s.monster = met->hit == bowpitch::Hit::Monster;
        s.stick = met->hit == bowpitch::Hit::Floor && met->groundKnown;
        s.gridDist = met->gridDist * upm;
    }
    if (!bowpitch::Finite(s.start) || !bowpitch::Finite(s.hero)) return;
    bool told = false;
    AcquireSRWLockExclusive(&g_shotLock);
    const bool same = shot != 0 && g_shot.ok && g_shot.shot == shot && arrowflight::Armed(g_shot, now);   // (the first note of a shot stands for it)
    if (!same) {
        g_shot = s;
        g_shotAt.store(now);
        told = true;
    }
    ReleaseSRWLockExclusive(&g_shotLock);
    if (told && g_trace.load() && (shot != 0 || s.target)) {
        const float pitch = std::asin(std::clamp(s.dir.y, -1.0f, 1.0f)) * 57.2957795f;
        LogF("arrow flight: %s %u noted - the line %+.1f deg up, %s", shot ? "the draw's shot" : "the hand's line", shot, pitch,
             s.target ? (s.monster ? "it met a monster: our arrow flies to it" : s.stick ? "it met the ground, known there: our arrow sticks in it" : "it met the ground (a guess there): our arrow flies on past it")
                      : "it met nothing ([bow] pitch_aim off, or into the air): our arrow leaves the bow and joins the game's own path");
    }
}

void Tick() {
    if (g_live.load() == 0) return;
    const uint64_t now = GetTickCount64();
    char lines[kSlots][600];
    int n = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (int k = 0; k < kSlots; ++k) {
        Slot& s = g_slots[k];
        if (!s.used) continue;
        if (now - s.seenMs > kIdleMs || now - s.createdMs > kMaxLifeMs || !g_on.load() || g_faulted.load()) {
            if (g_trace.load()) Summary(s, lines[n++], sizeof lines[0]);
            Free(s, k);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    for (int i = 0; i < n; ++i) Log(lines[i]);
}

}  // namespace arrowflightwire
