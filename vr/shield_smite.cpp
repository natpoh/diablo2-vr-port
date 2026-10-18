// shield_smite.cpp - [gameplay] shield_smite, the game-facing half: the shield bash casts the game's own Smite.
// The decisions are shield_smite.h's (pure, tested); this file reads what they need from the game and hands the
// result back to it, and does nothing else.
//
// What a bash does, step by step - and what is the game's:
//   1. vrcam's timer (Tick): the left controller's pose (direct_input.h's frame) and the shield's attach bone as
//      the finished pose draws it (skeletons.cpp ShieldAttach) go to the swing engine. Nothing is looked up in the
//      game until the shield is in a hard stroke.
//   2. A stroke: the client monsters round the hero are read (the client's unit hash - hit by touch's own scan,
//      meleetouch::MonstersNear / unit_scan.h), their bodies built from the MonStats row (melee_bodies.h) and tested
//      against the shield's capsule.
//   3. One touched: the pad slot Smite sits in (UiRead: the game's own controller tables, read on the UI thread)
//      is pressed through direct_input.h's PadPulse - a finger on the button: 65 ms, 180 ms on the left trigger's row
//      (shield_smite.h PulseMs), let go as soon as the game's click has asked for the target. That is all we press.
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

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_build.h"
#include "hand_vitals_game.h"   // the hero's mana, for the debug line
#include "hit_claim.h"
#include "hit_latency.h"
#include "melee_touch.h"        // HostileVerdict: the server's word on the nearest monster, for the debug line
#include "shield_smite.h"
#include "shield_arm_wire.h"     // [hands] shoulder_draw: the shield may be on the right arm, or put away
#include "skill_triggers.h"     // [debug] skill_triggers: the shield's capsule, marks, trail and line
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

constexpr uint64_t RVA_CTRL_BANK = 0x1195C0, RVA_BUTTON_SLOT = 0x1448C0, RVA_CTRL_TABLE = 0x2A48A90;
constexpr uint32_t kRecordSize = 28;   // the controller table's record: skill (s32), owner (s32), ...
constexpr int kMaxMonsters = 48;

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<int> g_slot{-1};   // the pad slot Smite sits in (0..9, UiRead), -1 none
std::atomic<bool> g_fresh{false};   // switched on just now: Tick starts from nothing and says so
std::atomic<float> g_minSpeed{stroke::kDefaultMinSpeed}, g_startCm{stroke::kDefaultStartCm};   // [gameplay] shield_smite_min_speed (m/s) / shield_smite_stroke_cm (cm), live

SRWLOCK g_lock = SRWLOCK_INIT;   // the cast: written by the timer, read by the game's thread in the hooks
Cast g_cast;
uintptr_t g_castUnit = 0;
std::atomic<bool> g_casting{false};
std::atomic<uint32_t> g_pressed{0};   // the pad press of the last bash (buttons | lt << 16), let go when the game's click has asked
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

// A client monster unit still the one we looked at: type 1, its id, not dying or dead.
bool UnitIs(uintptr_t unit, uint32_t id) {
    uint32_t type = 0, uid = 0, mode = 0;
    return Rd(unit, &type) && type == 1 && Rd(unit + 8, &uid) && uid == id && Rd(unit + 0xC, &mode) && mode != 0 && mode != 12;
}

// -- the monsters round the hero (the timer thread; reads only) --------------------------------------------------------
struct Unit {
    uintptr_t ptr = 0;
    uint32_t id = 0, cls = 0;
    float cellX = 0, cellY = 0;
    float d2 = 0;   // squared distance to the hero on the grid, game WORLD units (twice the cell) - the drawing sorts by it
};

// The monsters within `reach` metres of the hero: their capsules (render metres). It is hit by touch's own scan (meleetouch::MonstersNear,
// the client's unit hash, vr/unit_scan.h): the bash and the touch see one world. (The room lists this once walked found a monster once
// in a whole test with one standing 0.9 m away - see unit_scan.h.)
int Gather(uintptr_t hero, float upm, float floorM, float reachM, Monster* out, Unit* units, int cap) {
    uintptr_t path = 0;
    uint16_t hp[4] = {};
    if (cap <= 0 || !Rd(hero + 0x38, &path) || !Rd(path, &hp)) return 0;
    const float heroX = 2.0f * (hp[1] + hp[0] / 65536.0f), heroZ = 2.0f * (hp[3] + hp[2] / 65536.0f);
    unitscan::Near found[kMaxMonsters];
    const size_t n = meleetouch::MonstersNear(heroX, heroZ, reachM * upm, upm, floorM, found, std::min<size_t>(size_t(cap), kMaxMonsters));
    for (size_t i = 0; i < n; ++i) {
        out[i].id = found[i].id;
        out[i].body = found[i].body;
        units[i] = {found[i].unit, found[i].id, found[i].cls, found[i].x * 0.5f, found[i].z * 0.5f, found[i].d2};
    }
    return int(n);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    skilltriggers::SetLogSink(&Log);   // [debug] skill_triggers writes its numbers through the same log
}

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
bool Watching() { return g_on.load() || skilltriggers::Wanted(); }
void SetTuning(float minSpeed, float strokeCm) {
    const stroke::StrokeTuning t = stroke::Clamp({minSpeed, strokeCm});   // 0.3 .. 8 m/s, 2 .. 30 cm; a number that is not one is the default
    g_minSpeed.store(t.minSpeed);
    g_startCm.store(t.startCm);
}
static_assert(kSmiteSkill == kSmite, "one Smite");

// -- which pad slot holds Smite (the UI thread) ------------------------------------------------------------------------
void UiRead() {
    if (!g_on.load() && !skilltriggers::Wanted()) return;   // ([debug] skill_triggers reads it too, to say whether a Smite slot is there)
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
    const bool on = g_on.load();
    const bool boxes = skilltriggers::Wanted();   // [debug] skill_triggers: the shield is drawn and described even with shield_smite off
    if (!on && !boxes) return;
    if (on) LatencyPoll(GetTickCount64());
    if (!viewgate::F4() && g_casting.exchange(false)) {   // F4 only (vr/view_gate.h): a cast window open when the view changes is closed - the game's lookups are its own again
        AcquireSRWLockExclusive(&g_lock);
        g_cast.Finish();
        ReleaseSRWLockExclusive(&g_lock);
    }
#if D2RVR_FIRST_PERSON && D2RVR_SHIELD_INPUT
    static Detector detector;
    static uint32_t lastCounter = 0;
    static ULONGLONG nextReport = 0, lastGather = 0;
    static bool wasOn = false;
    if (on != wasOn || g_fresh.exchange(false)) {
        wasOn = on;
        detector.Reset();
        lastCounter = 0;
        if (on) Log("shield smite: watching the left shield (a bash needs the shield, Smite on a pad slot, the right trigger up)");
    }
    if (const float speed = g_minSpeed.load(), cm = g_startCm.load();
        speed != detector.Params().strokeSpeed || std::fabs(cm * 0.01f - detector.Params().onsetExcursion) > 1e-6f) {   // the bash's two numbers, live
        detector.SetParams(BashParams(speed, cm));
        LogF("shield smite: the bash speed is %.2f m/s, a stroke begins at %.2f m/s and after %.0f cm ([gameplay] shield_smite_min_speed, shield_smite_stroke_cm)", speed,
             detector.Params().beginSpeed, cm);
    }
    // [hands] shoulder_draw: the shield is on the arm shieldarmwire says (-1: not handled, the left one) - the right one after a cross draw of the sword
    const int wear = shieldarmwire::Arm();
    const bool shield = gamestate::LeftShield() && !shieldarmwire::Stowed();   // (put away at the left shoulder: nothing to bash with)
    if (shield && play) skel::WantShield(true);   // the skeleton hook poses the shield for us only while it is asked
    directinput::Frame f;
    if (!directinput::Latest(&f)) {
        detector.Reset();
        return;
    }
    if (f.counter == lastCounter) return;   // the frame we have already seen
    lastCounter = f.counter;

    // (direct_input's hands: 0 left, 1 right.) `left` is the hand the shield is on - the right one after a hand swap - and `right` the other: the trigger that cancels a bash.
    const bool onRight = BashSide(wear) == 0;
    const directinput::Hand &left = f.hand[onRight ? 1 : 0], &right = f.hand[onRight ? 0 : 1];
    Gate gate;
    gate.enabled = on;   // (off with only the debug drawing on: the engine stays shut, nothing counts, nothing is pressed)
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
    sample.context = ((uint64_t(pose.heroGeneration) << 32) | (gamestate::HandsKey() ^ (gamestate::WeaponSet() << 30))) ^ (uint64_t)(wear + 2) << 52;   // (the shield on another arm is another stroke)
    sample.hand.p = {left.grip.pos[0], left.grip.pos[1], left.grip.pos[2]};
    sample.hand.q = {left.grip.rot[0], left.grip.rot[1], left.grip.rot[2], left.grip.rot[3]};
    if (sample.active) {
        const float k = 1.0f / upm;
        sample.blade = ShieldBlade({pose.origin[0] * k, pose.origin[1] * k, pose.origin[2] * k}, {pose.axis[0], pose.axis[1], pose.axis[2]});
        sample.anchor = {pose.player[0] * k, pose.player[1] * k, pose.player[2] * k};
        sample.scale = upm;
    }

    stroke::Sweep sweep;
    const bool swept = detector.Step(gate, sample, sweep);
    static float lastSweepSpeed = 0.0f;   // for the debug line
    static ULONGLONG lastSweepAt = 0;
    if (swept) {
        lastSweepSpeed = sweep.speed;
        lastSweepAt = GetTickCount64();
    }
    if (swept) {
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
                    hitclaim::Press(BashSide(wear), (int64_t)GetTickCount64());   // the blow is the shield arm's: the game's hit comes after its click closed the cast, so the press says whose it is (vr/hit_claim.h)
                    const PadPress p = SlotPress(slot);
                    g_pressed.store((uint32_t)p.buttons | (uint32_t)p.lt << 16);
                    directinput::PadPulse({p.buttons, p.lt, 0}, PulseMs(p));
                    hitlatency::Press(hitlatency::kShield);   // (counting only: the press -> hit log)
                    g_lat.Press();
                    ++g_requests;
                    skilltriggers::StruckBody(u.id, meleeboxes::kStruckKey, tick, monsters[index].body);   // [debug] skill_triggers: the monster flashes red (drawing only)
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

    // [debug] skill_triggers (vr/skill_triggers.h): the shield's capsule, speed marks, trail and line. DRAWING AND LOGGING ONLY: read-only on everything
    // above; the detector, the gate and the press are not asked anything else and are not changed by it. Own scan (20 Hz) so the monsters follow.
    if (boxes && viewgate::F4()) {
        static ULONGLONG scanAt = 0;
        static Monster dm[kMaxMonsters];
        static Unit du[kMaxMonsters];
        static int dn = 0;
        const ULONGLONG tick = GetTickCount64();
        if (sample.active && hero && tick - scanAt >= 50) {
            scanAt = tick;
            dn = Gather(hero, upm, sample.anchor.y, 3.0f, dm, du, kMaxMonsters);   // (the same reach the bash looks in)
        } else if (!sample.active || !hero) {
            dn = 0;
        }
        handvitals::game::Tick(true);   // the hero's mana, five times a second, read on the UI thread (the line says whether Smite can be afforded)
        skilltriggers::BashFrame sf;
        sf.nowMs = tick;
        sf.upm = upm;
        sf.floorY = sample.anchor.y;
        skilltriggers::BashIn& si = sf.bash;
        si.have = sample.active;
        meleeboxes::Monster shown[kMaxMonsters];
        size_t shownN = 0;
        if (si.have) {
            si.blade = sample.blade;
            si.inStroke = detector.InStroke();
            si.speed = detector.Speed();
            si.minSpeed = detector.Params().strokeSpeed;
            si.beginSpeed = detector.Params().beginSpeed;
            si.startCm = detector.Params().onsetExcursion * 100.0f;
            si.counting = detector.Counting();
            si.sweepSpeed = lastSweepSpeed;
            si.sweepMs = lastSweepAt;
            si.rightTrigger = right.trigger;
            si.slot = g_slot.load();
            if (si.slot >= 0) {   // the pad button the bash would press
                const PadPress p = SlotPress(si.slot);
                si.padButtons = p.buttons;
                si.padLt = p.lt != 0;
            }
            si.manaNeed = kSmiteMana;
            handvitals::Vitals vit;
            double ageMs = 0.0;
            if (handvitals::game::Latest(&vit, &ageMs) && vit.known && ageMs < 3000.0) si.mana = vit.mana;
            // the monsters of the scan, the nearest to the shield's capsule (the gap between the two capsules' surfaces: < 0 is a touch now)
            float best = 1e9f;
            for (int i = 0; i < dn; ++i) {
                const int hv = meleetouch::HostileVerdict(du[i].id);
                const skilltriggers::Verdict verdict = hv < 0 ? skilltriggers::Verdict::Unknown : hv ? skilltriggers::Verdict::Hostile : skilltriggers::Verdict::Refused;
                shown[shownN++] = {du[i].id, du[i].cls, du[i].d2, dm[i].body, verdict};
                const float gap = std::sqrt(stroke::SegmentDistanceSquared(sample.blade.base, sample.blade.tip, dm[i].body.bottom, dm[i].body.top)) -
                                  (dm[i].body.radius + sample.blade.radius);
                if (gap < best) {
                    best = gap;
                    si.haveNear = true;
                    si.nearCls = du[i].cls;
                    si.nearHeroM = std::sqrt(du[i].d2) / upm;
                    si.nearGapM = gap;
                    si.nearVerdict = verdict;
                }
            }
            Facts facts;
            facts.enabled = on;
            facts.playing = gate.playing && gate.f4;
            facts.rightTrigger = right.trigger;
            facts.slot = g_slot.load() >= 0;
            facts.speed = si.speed;
            facts.minSpeed = si.minSpeed;
            facts.inStroke = si.inStroke;
            facts.sinceRequest = detector.SinceRequest(f.time);
            facts.heroKnown = hero != 0;
            facts.haveNear = si.haveNear;
            facts.nearVerdict = si.nearVerdict;
            facts.mana = si.mana;
            si.why = Explain(facts);
        }
        std::sort(shown, shown + shownN, [](const meleeboxes::Monster& a, const meleeboxes::Monster& b) { return a.d2 < b.d2; });   // the module keeps the nearest few
        sf.monsters = shown;
        sf.count = shownN;
        skilltriggers::UpdateBash(sf);
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
#if D2RVR_SHIELD_INPUT
    if (unit && where == 2) {   // the cast is on its way: the button need not be held longer (PulseMs)
        const uint32_t pressed = g_pressed.exchange(0);
        if (pressed) directinput::PadPulseEnd({(uint16_t)(pressed & 0xFFFF), (uint8_t)(pressed >> 16), 0});
    }
#endif
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
