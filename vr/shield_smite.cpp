// shield_smite.cpp - [gameplay] shield_smite, the game-facing half: the shield bash casts the game's own Smite.
// The decisions are shield_smite.h's (pure, tested); this file reads what they need from the game and hands the
// result back to it, and does nothing else.
//
// What a bash does, step by step - and what is the game's:
//   1. vrcam's timer (Tick): the left controller's pose (direct_input.h's frame) and the shield's attach bone as
//      the finished pose draws it (skeletons.cpp ShieldAttach) go to the swing engine. Nothing is looked up in the
//      game until the shield is in a hard stroke.
//   2. A stroke: the client monsters round the hero are read (the room lists, as the cave ceiling's torches are),
//      their bodies built from the MonStats row (melee_bodies.h) and tested against the shield's capsule.
//   3. One touched: the pad slot Smite sits in (UiRead: the game's own controller tables, read on the UI thread)
//      is pressed for 65 ms through direct_input.h's PadPulse - a finger on the button. That is all we press.
//   4. The game's cast resolver and then the map click ask for the cast's target (SpellTarget / HoverUnit, the two
//      lookups spell_aim already answers). For 0.35 s they answer with the touched monster, once for the resolver,
//      once for the click, and the cast goes to it as a click on it would. Smite's server function takes its
//      victim from the hero's target and returns when there is none or when it is out of reach (RVA 0x564930, read
//      2026-10-14), so an aim POINT alone does nothing for Smite - the target has to ride in the cast, and this is
//      the client-side way: no server hook, no call into Attack / Smite, no Target redirect (pcvr's).
//      Reach, mana, cast rate, animation, damage, stun and knockback are the game's. The client's own check of a
//      target (the hover's validity test, 0x9B840 via 0xF1A60) still runs on the unit we hand back.
//
// From bearstonem/d2r-pcvr (MIT): the capsule, the monster body, the idea of a bash that casts Smite; the controller
// slot tables (vr/native_ui.cpp). Not taken: its Smite hook (a table scan for the server's function, a call into it).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_build.h"
#include "hit_latency.h"
#include "shield_smite.h"
#include "shield_smite_game.h"
#include "sigscan.h"
#include "skeletons.h"
#include "view_gate.h"

// direct-input's header (the per-frame input and the pad pulse). Without it in the tree the box does nothing.
#if __has_include("direct_input.h")
#include "direct_input.h"
#define D2RVR_SHIELD_INPUT 1
#else
#define D2RVR_SHIELD_INPUT 0
#endif

namespace gamestate {
uint32_t HandsKey();
uint32_t WeaponSet();
bool MenuOpen();
}  // namespace gamestate

namespace shieldsmite {
namespace {

constexpr uint64_t RVA_CTRL_BANK = 0x1195C0, RVA_BUTTON_SLOT = 0x1448C0, RVA_CTRL_TABLE = 0x2A48A90, RVA_MONSTATS = 0x975B0;
constexpr uint32_t kRecordSize = 28;   // the controller table's record: skill (s32), owner (s32), ...
constexpr int kMaxMonsters = 48;
constexpr double kPadHoldMs = 65.0;

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<int> g_slot{-1};   // the pad slot Smite sits in (0..9, UiRead), -1 none
std::atomic<bool> g_fresh{false};   // switched on just now: Tick starts from nothing and says so

SRWLOCK g_lock = SRWLOCK_INIT;   // the cast: written by the timer, read by the game's thread in the hooks
Cast g_cast;
uintptr_t g_castUnit = 0;
std::atomic<bool> g_casting{false};
std::atomic<uint32_t> g_requests{0}, g_answers{0}, g_clicks{0}, g_strokes{0}, g_noUnit{0};
hitlatency::Stats g_lat;   // the press -> hit times (vr/hit_latency.h), the timer's thread only

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    Log(b);
}

double Now() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

bool Rd(void* d, const void* s, size_t n) noexcept {
    __try {
        memcpy(d, s, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T>
bool Rd(uintptr_t a, T* v) {
    return a > 0x10000 && Rd(v, (const void*)a, sizeof *v);
}

// -- the game's functions, behind a guard ---------------------------------------------------------------------------
bool CallBank(uintptr_t fn, int* out) noexcept {
    __try {
        *out = ((int (*)())fn)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallSlot(uintptr_t fn, int buttons, bool alternate, int* out) noexcept {
    __try {
        *out = ((int (*)(int, bool))fn)(buttons, alternate);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallRow(uintptr_t fn, int bank, int cls, uintptr_t* out) noexcept {
    __try {
        *out = ((uintptr_t(*)(uint8_t, int))fn)((uint8_t)bank, cls);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A client monster unit still the one we looked at: type 1, its id, not dying or dead.
bool UnitIs(uintptr_t unit, uint32_t id) {
    uint32_t type = 0, uid = 0, mode = 0;
    return Rd(unit, &type) && type == 1 && Rd(unit + 8, &uid) && uid == id && Rd(unit + 0xC, &mode) && mode != 0 && mode != 12;
}

// -- the monsters round the hero (the timer thread; reads only, as the cave ceiling's torches do) -----------------------
struct Unit {
    uintptr_t ptr = 0;
    uint32_t id = 0, cls = 0;
    float cellX = 0, cellY = 0;
};

meleetouch::MonsterRow RowOf(uintptr_t unit, uint32_t cls, bool* ok) {
    static std::unordered_map<uint32_t, meleetouch::MonsterRow> cache;   // the timer thread only
    *ok = false;
    uint8_t bank = 0;
    if (cls > 4095 || !Rd(unit + 0x1BD, &bank) || bank > 3) return {};
    const uint32_t key = (uint32_t(bank) << 16) | cls;
    if (const auto it = cache.find(key); it != cache.end()) {
        *ok = it->second.footprintA != 0;
        return it->second;
    }
    meleetouch::MonsterRow row;
    const uintptr_t fn = d2rsig::Addr(RVA_MONSTATS);
    uintptr_t ptr = 0;
    uint8_t f[16] = {};
    if (fn && CallRow(fn, bank, (int)cls, &ptr) && ptr > 0x10000 && Rd(f, (const void*)ptr, sizeof f) && f[8] >= 1 && f[8] <= 8 && f[9] >= 1 && f[9] <= 8)
        row = {f[8], f[9], f[13]};
    cache[key] = row;   // a row that does not read is remembered too: the game's table is asked once per kind
    *ok = row.footprintA != 0;
    return row;
}

// The monsters in the hero's room and the rooms next to it, within `reach` metres: their capsules (render metres).
int Gather(uintptr_t hero, float upm, float floorM, float reachM, Monster* out, Unit* units, int cap) {
    uintptr_t path = 0, room = 0, nearRooms = 0;
    uint32_t nNear = 0;
    uint16_t hp[4] = {};
    if (!Rd(hero + 0x38, &path) || !Rd(path + 0x20, &room) || !Rd(room, &nearRooms) || !Rd(room + 0x40, &nNear) || !Rd(path, &hp) || !nearRooms ||
        nNear > 64)
        return 0;
    const float heroX = 2.0f * (hp[1] + hp[0] / 65536.0f), heroZ = 2.0f * (hp[3] + hp[2] / 65536.0f);
    uintptr_t rooms[65];
    uint32_t nRooms = 0;
    rooms[nRooms++] = room;
    for (uint32_t r = 0; r < nNear; ++r) {
        uintptr_t rm = 0;
        if (Rd(nearRooms + r * 8, &rm) && rm && rm != room) rooms[nRooms++] = rm;
    }
    int n = 0;
    for (uint32_t r = 0; r < nRooms && n < cap; ++r) {
        uintptr_t u = 0;
        if (!Rd(rooms[r] + 0xA8, &u)) continue;
        for (int k = 0; u && k < 512 && n < cap; ++k) {
            uint32_t type = 0, cls = 0, id = 0, mode = 0;
            uintptr_t up = 0, next = 0;
            if (!Rd(u, &type) || !Rd(u + 0x158, &next)) break;
            if (type == 1 && Rd(u + 4, &cls) && Rd(u + 8, &id) && Rd(u + 0xC, &mode) && mode != 0 && mode != 12 && Rd(u + 0x38, &up) && up > 0x10000) {
                uint16_t p[4] = {};
                if (Rd(up, &p)) {
                    const float gx = 2.0f * (p[1] + p[0] / 65536.0f), gz = 2.0f * (p[3] + p[2] / 65536.0f);
                    if (std::hypot(gx - heroX, gz - heroZ) <= reachM * upm) {
                        bool ok = false;
                        const meleetouch::MonsterRow row = RowOf(u, cls, &ok);
                        stroke::Body body;
                        if (ok && meleetouch::BodyFor(row, gx, gz, floorM, upm, body)) {
                            out[n].id = id;
                            out[n].body = body;
                            units[n] = {u, id, cls, gx * 0.5f, gz * 0.5f};
                            ++n;
                        }
                    }
                }
            }
            u = next;
        }
    }
    return n;
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on) {
    const bool was = g_on.exchange(on);
    if (on != was) LogF("shield smite: %s ([gameplay] shield_smite)", on ? "on" : "off");
    hitlatency::Want(hitlatency::kShield, on);   // the hit observer (vr/haptics.cpp) is put in for the press -> hit log too
    if (on && !was) g_fresh.store(true);
    if (!on) {
        g_slot.store(-1);
        g_casting.store(false);
    }
}

bool Casting() { return g_casting.load() && viewgate::F4(); }
bool On() { return g_on.load(); }
static_assert(kSmiteSkill == kSmite, "one Smite");

// -- which pad slot holds Smite (the UI thread) ------------------------------------------------------------------------
void UiRead() {
    if (!g_on.load()) return;
    static int told = -2;
    static bool toldWhy = false;
    auto fail = [&](const char* why) {
        g_slot.store(-1);
        if (!toldWhy) {
            toldWhy = true;
            LogF("shield smite: the pad slots cannot be read (%s) - a bash casts nothing", why);
        }
    };
    const uintptr_t bankFn = d2rsig::Addr(RVA_CTRL_BANK), slotFn = d2rsig::Addr(RVA_BUTTON_SLOT), table = d2rsig::Addr(RVA_CTRL_TABLE);
    if (!bankFn || !slotFn || !table) return fail("the game's controller tables not found (yet)");
    int bank = -1;
    if (!CallBank(bankFn, &bank) || (bank != 0 && bank != 16)) return fail("the controller bank is not 0 or 16");
    int skills[kSlotCount];
    unsigned used = 0;
    for (int i = 0; i < kSlotCount; ++i) {
        const PadPress p = SlotPress(i);
        int slot = -1;
        // A customised layout that gives two buttons one slot is refused: the press would cast the wrong skill.
        if (!CallSlot(slotFn, p.buttons, p.lt != 0, &slot) || slot < 0 || slot >= 16 || (used & (1u << slot))) return fail("the pad buttons do not map to ten slots");
        used |= 1u << slot;
        int32_t record[2] = {};
        if (!Rd(table + uintptr_t(bank + slot) * kRecordSize, &record) || record[0] < -1 || record[0] > 4095) return fail("a controller record does not read");
        skills[i] = record[0];
    }
    toldWhy = false;
    const int slot = SmiteSlot([&](int i) { return skills[i]; });
    g_slot.store(slot);
    if (slot != told) {
        told = slot;
        if (slot < 0) Log("shield smite: Smite is on none of the pad's ten skill slots - a bash casts nothing");
        else {
            const PadPress p = SlotPress(slot);
            LogF("shield smite: Smite sits on pad slot %d (button 0x%04X%s)", slot, p.buttons, p.lt ? " with LT" : "");
        }
    }
}

// -- the timer (every 10 ms or so) -----------------------------------------------------------------------------------------
void LatencyPoll(ULONGLONG now) {
    static ULONGLONG pollAt = 0, reportAt = 0;
    static unsigned told = 0;
    if (now - pollAt < 25) return;
    pollAt = now;
    hitlatency::Tracker::Outcome o[8];
    for (int n; (n = hitlatency::Collect(hitlatency::kShield, o, 8)) > 0;)
        for (int i = 0; i < n; ++i) {
            g_lat.Add(o[i]);
            if (o[i].hit && ++told <= 200) LogF("shield smite: press->hit %.0f ms", o[i].ms);
        }
    if (now - reportAt >= 10000) {
        reportAt = now;
        if (g_lat.Any()) {
            char b[300];
            g_lat.Line(b, sizeof b, "shield smite:");
            Log(b);
        }
    }
}

void Tick(bool play, float upm, uintptr_t hero) {
    if (!g_on.load()) return;
    LatencyPoll(GetTickCount64());
    if (!viewgate::F4() && g_casting.exchange(false)) {   // F4 only (vr/view_gate.h): a cast window open when the view changes is closed - the game's lookups are its own again
        AcquireSRWLockExclusive(&g_lock);
        g_cast.Finish();
        ReleaseSRWLockExclusive(&g_lock);
    }
#if D2RVR_FIRST_PERSON && D2RVR_SHIELD_INPUT
    static Detector detector;
    static uint32_t lastCounter = 0;
    static ULONGLONG nextReport = 0, lastGather = 0;
    if (g_fresh.exchange(false)) {
        detector.Reset();
        lastCounter = 0;
        Log("shield smite: watching the left shield (a bash needs the shield, Smite on a pad slot, the right trigger up)");
    }
    const bool shield = gamestate::LeftShield();
    if (shield && play) skel::WantShield(true);   // the skeleton hook poses the shield for us only while it is asked
    directinput::Frame f;
    if (!directinput::Latest(&f)) {
        detector.Reset();
        return;
    }
    if (f.counter == lastCounter) return;   // the frame we have already seen
    lastCounter = f.counter;

    const directinput::Hand &left = f.hand[0], &right = f.hand[1];
    Gate gate;
    gate.enabled = true;
    gate.shield = shield;
    gate.smiteSlot = g_slot.load() >= 0;
    gate.playing = play && (f.flags & directinput::kFocused) && !(f.flags & directinput::kInMenu) && !gamestate::MenuOpen() && left.active &&
                   left.grip.valid == 1;
    gate.f4 = viewgate::F4() && !(f.flags & directinput::kOtherView);
    gate.rightTrigger = right.trigger;

    skel::ShieldPose pose{};
    const bool posed = shield && skel::ShieldAttach(&pose);
    const bool scaled = std::isfinite(upm) && upm >= 1.0f && upm <= 12.0f;
    stroke::Sample sample;
    sample.time = f.time;
    sample.active = posed && scaled;
    sample.epoch = (uint64_t(f.session) << 32) | f.epoch;
    sample.context = (uint64_t(pose.heroGeneration) << 32) | (gamestate::HandsKey() ^ (gamestate::WeaponSet() << 30));
    sample.hand.p = {left.grip.pos[0], left.grip.pos[1], left.grip.pos[2]};
    sample.hand.q = {left.grip.rot[0], left.grip.rot[1], left.grip.rot[2], left.grip.rot[3]};
    if (sample.active) {
        const float k = 1.0f / upm;
        sample.blade = ShieldBlade({pose.origin[0] * k, pose.origin[1] * k, pose.origin[2] * k}, {pose.axis[0], pose.axis[1], pose.axis[2]});
        sample.anchor = {pose.player[0] * k, pose.player[1] * k, pose.player[2] * k};
        sample.scale = upm;
    }

    stroke::Sweep sweep;
    if (detector.Step(gate, sample, sweep)) {
        ++g_strokes;
        const ULONGLONG tick = GetTickCount64();
        if (hero && tick - lastGather >= 12) {   // a stroke's sweeps come every frame: the monsters are looked at 80 times a second at most
            lastGather = tick;
            Monster monsters[kMaxMonsters];
            Unit units[kMaxMonsters];
            const int n = Gather(hero, upm, sample.anchor.y, 3.0f, monsters, units, kMaxMonsters);
            size_t index = 0;
            if (n > 0 && detector.Pick(sweep, f.session, f.time, monsters, (size_t)n, index)) {
                const int slot = g_slot.load();
                const Unit& u = units[index];
                if (slot >= 0) {
                    AcquireSRWLockExclusive(&g_lock);
                    g_cast.Begin(u.id, u.cellX, u.cellY, Now());
                    g_castUnit = u.ptr;
                    ReleaseSRWLockExclusive(&g_lock);
                    g_casting.store(true);
                    const PadPress p = SlotPress(slot);
                    directinput::PadPulse({p.buttons, p.lt, 0}, (uint32_t)kPadHoldMs);
                    hitlatency::Press(hitlatency::kShield);   // (counting only: the press -> hit log)
                    g_lat.Press();
                    ++g_requests;
                    LogF("shield smite: a bash at %.1f m/s touched monster %u (kind %u) at cell %.1f %.1f - pad slot %d pressed", sweep.speed, u.id, u.cls,
                         u.cellX, u.cellY, slot);
                }
            }
        } else if (!hero && tick >= nextReport) {
            nextReport = tick + 10000;
            Log("shield smite: a bash, but the hero's unit is not known (yet) - nothing to look for monsters from");
        }
    }
    if (const ULONGLONG tick = GetTickCount64(); tick >= nextReport && (g_requests.load() || g_answers.load())) {
        nextReport = tick + 10000;
        const stroke::Diagnostics d = detector.TakeDiagnostics();
        LogF("shield smite: %u bash frames, %u casts asked, the game's lookups answered %u times, %u clicks carried a target, %u unit gone; tracking drops %u",
             g_strokes.exchange(0), g_requests.exchange(0), g_answers.exchange(0), g_clicks.exchange(0), g_noUnit.exchange(0), d.discontinuities);
    }
#else
    if (g_fresh.exchange(false)) {
        (void)play;
        (void)upm;
        (void)hero;
        Log("shield smite: this build has no input layer (vr/direct_input.h) or no first person - the box does nothing");
    }
#endif
}

// -- the game's lookups (the game's thread) ----------------------------------------------------------------------------------
void* Target(int where, int skill) {
    if (!g_casting.load() || !viewgate::F4()) return nullptr;   // (F4 only: another view's lookups are the game's)
    if (skill >= 0 && skill != kSmite) return nullptr;   // another skill's cast, pressed meanwhile: the game's own
    uintptr_t unit = 0;
    bool gone = false;
    AcquireSRWLockExclusive(&g_lock);
    if (g_cast.Open(Now())) {
        if (UnitIs(g_castUnit, g_cast.Monster())) {
            unit = g_castUnit;
            if (where == 2) g_cast.Finish();   // the click has asked: the target goes to the game's server with it
        } else {
            g_cast.Finish();
            gone = true;
        }
    }
    if (!g_cast.Open(Now())) g_casting.store(false);
    ReleaseSRWLockExclusive(&g_lock);
    if (gone) ++g_noUnit;
    if (unit) {
        ++g_answers;
        if (where == 2) ++g_clicks;
    }
    return (void*)unit;
}

void Aim(float* x, float* y) {
    if (!g_casting.load() || !x || !y || !viewgate::F4()) return;
    AcquireSRWLockExclusive(&g_lock);   // Open() closes a window that has run out
    if (g_cast.Open(Now())) {
        *x = g_cast.CellX();
        *y = g_cast.CellY();
    }
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace shieldsmite
