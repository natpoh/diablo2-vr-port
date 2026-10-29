// point_cast_wire.cpp - see point_cast_wire.h. [gameplay] teleport_circle: the skills cast onto a point aimed with an arc and a circle (VR F4).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "double_swing_game.h"   // ReadPadSlots: the pad's slots, as double swing reads them
#include "log_async.h"
#include "point_cast_wire.h"
#include "shield_smite.h"        // kSlotCount, PulseMs: the controller's ten slots double swing reads, how long a slot's press is held
#include "wheel_game.h"          // Latest: the wheel's read of the slots - RT and LT + RT
#include "view_gate.h"

namespace pointwire {
namespace {

using pointcast::Arc;
using pointcast::Offset;

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false}, g_leapArc{false};
SRWLOCK g_setLock = SRWLOCK_INIT;
pointcast::SkillSet g_skills;
constexpr int kSlots = pointcast::kSlotCount;
std::atomic<int> g_slot[kSlots];   // the skill on each slot, pointcast's numbering (-1 none / not read)
struct SlotInit { SlotInit() { for (auto& s : g_slot) s.store(-1); } } g_slotInit;
const char* SlotName(int slot) { return slot >= 0 && slot < kSlots ? wheel::kSlotDefs[slot].label : "?"; }

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// The pad poll's state (its thread) and what the others read of it (the lock).
pointcast::Hold g_hold;
SRWLOCK g_lock = SRWLOCK_INIT;
bool g_up = false;
Arc g_arc;
struct Release {
    bool circle = false;   // a hold's: its point is the circle's (false: a tap - the gaze's)
    int skill = -1;
    Offset at;
    uint64_t ms = 0;
};
Release g_release;
std::atomic<uint32_t> g_drawnFx{0}, g_drawnBoxes{0};   // presents that drew the arc while this hold's was up: by the effect's PaOn, by the capsules
const char* SourceName(int s) {
    return s == 1 ? "the weapon's tip as drawn" : s == 2 ? "the drawn caster or hand" : s == 3 ? "the controller" : s == 4 ? "the belly, aimed by the head" : "?";
}
// The release's own press of the slot (directinput::PadPulse), until it is over: the pulses are in the pad this filter sees, and that press is not a new hold.
uint32_t g_ownControl = 0;
uint64_t g_ownUntil = 0;

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    if (g_on.load()) Log("point cast: on ([gameplay] teleport_circle) - hold a point skill's button for an arc and a circle, let go to cast there (F4)");
}

void Configure(bool on, const char* skills, bool leapArc) {
    pointcast::SkillSet set = skills && skills[0] ? pointcast::ParseSkills(skills) : pointcast::DefaultSkills();
    if (leapArc) {
        set.Add(pointcast::kLeap);
        set.Add(pointcast::kLeapAttack);
    }
    AcquireSRWLockExclusive(&g_setLock);
    const bool changed = memcmp(&set, &g_skills, sizeof set) != 0;
    g_skills = set;
    ReleaseSRWLockExclusive(&g_setLock);
    const bool was = g_on.exchange(on);
    g_leapArc.store(leapArc);
    if ((was != on || changed) && g_ctx) {
        if (!on) {
            Log("point cast: off - the point skills' buttons cast as ever");
            return;
        }
        char list[400];
        int at = snprintf(list, sizeof list, "point cast: on ([gameplay] teleport_circle) - the arc and circle for skills");
        for (int id = 0; id < 1024 && at > 0 && at < (int)sizeof list - 8; ++id)
            if (set.Has(id)) at += snprintf(list + at, sizeof list - at, " %d", id);
        Log(list);
    }
}

bool On() { return g_on.load(); }

bool IsPoint(int skill) {
    if (!g_on.load()) return false;
    AcquireSRWLockShared(&g_setLock);
    const bool has = g_skills.Has(skill);
    ReleaseSRWLockShared(&g_setLock);
    return has;
}

void UiRead() {
    if (!g_on.load()) return;
    int skills[shieldsmite::kSlotCount], owners[shieldsmite::kSlotCount];
    const char* why = "";
    static bool toldWhy = false;
    // all twelve: the skill panel's own snapshot of the controller's slots (the table it binds through), fresh. Latest copies the whole snapshot only when its
    // revision changed and otherwise brings just `at` up to date - so it is read into the one kept copy, never a fresh local: with a local, an unchanged
    // snapshot read as not ready, `at` went stale after 3 s and RT (which only the snapshot has) dropped out of the list until the next change (the log of
    // 2026-10-25 19:33-19:38: RT in, out a few seconds later, never a hold on it).
    static uint32_t rev = 0;
    static wheel::Snapshot kept;
    wheelgame::Latest(&kept, &rev);
    const bool fresh = kept.ready && GetTickCount64() - kept.at < 3000;
    if (fresh) {
        for (int i = 0; i < kSlots; ++i) g_slot[i].store(wheel::CastsSkill(kept.slot[i]) ? kept.slot[i].id : -1);
        toldWhy = false;
    } else if (doubleswing::ReadPadSlots(skills, owners, &why)) {
        // no snapshot (yet): the controller tables as double swing reads them - B X Y RB, LT + those, A, LT + A (its order) - and RT not known
        for (int i = 0; i < 8; ++i) g_slot[i].store(skills[i]);
        g_slot[8].store(skills[8]);
        g_slot[10].store(skills[9]);
        g_slot[9].store(-1);
        g_slot[11].store(-1);
        toldWhy = false;
    } else {
        for (auto& s : g_slot) s.store(-1);
        if (!toldWhy) {
            toldWhy = true;
            LogF("point cast: the pad slots cannot be read (%s) - the skill buttons cast as ever", why);
        }
        return;
    }
    static uint32_t told = 0xFFFFFFFFu;
    uint32_t mask = 0;
    for (int i = 0; i < kSlots; ++i)
        if (IsPoint(g_slot[i].load())) mask |= 1u << i;
    if (mask != told) {
        told = mask;
        char line[300];
        int at = snprintf(line, sizeof line, "point cast: the pad slots with a point skill:");
        for (int i = 0; i < kSlots && at > 0 && at < (int)sizeof line - 40; ++i)
            if (mask & (1u << i)) at += snprintf(line + at, sizeof line - at, " %s (skill %d)", SlotName(i), g_slot[i].load());
        if (!mask) snprintf(line + at, sizeof line - at, " none");
        Log(line);
    }
}

bool WantsSlots() { return g_on.load(); }

void FilterPad(uint16_t* buttons, uint8_t lt, uint8_t* rt, const PollIn& in, ArcFn arcFor) {
    if (!buttons || !rt) return;
    if (!g_on.load() && !g_hold.Active()) return;
    const bool live = g_on.load() && in.allowed && viewgate::F4();
    const uint64_t nowMs = GetTickCount64();
    const uint32_t ignore = nowMs < g_ownUntil ? g_ownControl : 0u;
    const pointcast::Step s = g_hold.Poll(*buttons, lt, *rt, live, [](int slot) { return slot >= 0 && slot < kSlots ? g_slot[slot].load() : -1; },
                                          [](int skill) { return IsPoint(skill); }, nowMs, ignore);
    *buttons = (uint16_t)(*buttons & ~(s.strip & 0xFFFFu));
    if (s.strip & pointcast::kRt) *rt = 0;   // the right trigger held for the arc: the game sees it let go
    if (g_hold.CircleUp()) {   // the arc follows the hand every poll; the last one that could be made stays (a hand lost for a moment)
        Arc a;
        const bool ok = arcFor && arcFor(g_hold.Skill(), &a) && a.ok;
        AcquireSRWLockExclusive(&g_lock);
        a.skill = g_hold.Skill();
        if (ok) g_arc = a;
        if (s.circle) g_up = ok;   // the first: up only with an arc
        else if (ok) g_up = true;
        ReleaseSRWLockExclusive(&g_lock);
        if (s.circle && !ok) Log("point cast: held - no arc can be made yet (no pose of the hand or the staff, or it points straight up or down)");
        if (s.circle) {
            g_drawnFx.store(0);
            g_drawnBoxes.store(0);
            if (ok) LogF("point cast: %s (skill %d) held - the arc up, from %s, landing %.1f cells out%s", SlotName(s.slot), s.skill, SourceName(a.source),
                         pointcast::Length(a.land) / pointcast::kUnitsPerCell, a.capped ? " (held at the cap)" : "");
        }
    }
    if (!s.released) return;
    AcquireSRWLockExclusive(&g_lock);
    const bool had = g_up && g_arc.ok;
    const Offset at = g_arc.land;
    g_up = false;
    g_arc = Arc{};
    const bool cast = !s.cancelled && (s.tap || had);
    if (cast) g_release = {!s.tap, s.skill, at, nowMs};
    ReleaseSRWLockExclusive(&g_lock);
    const char* name = SlotName(s.slot);
    if (!cast) {
        LogF("point cast: %s (skill %d) let go - %s: no cast", name, s.skill, s.cancelled ? "a menu or another view came up while it was held" : "no arc was made");
        return;
    }
    // the cast: the slot's own control, once, as the shield bash presses Smite's (the game reads it in its own poll; its mana, cooldown, checks) - held
    // longer on the left trigger's row, which the game's pad code prepares first (shield_smite.h PulseMs)
    const pointcast::SlotPad p = pointcast::PadOf(s.slot);
    const uint32_t ms = p.lt ? shieldsmite::kAltBankPulseMs : shieldsmite::kPulseMs;
    g_ownControl = pointcast::ControlOf(s.slot);
    g_ownUntil = nowMs + ms + 60;
    directinput::PadPulse({p.buttons, p.lt, p.rt}, ms);
    if (s.tap) LogF("point cast: %s (skill %d) tapped - cast where you look (the slot's button pressed once)", name, s.skill);
    else LogF("point cast: %s (skill %d) let go - cast to the circle, %.1f cells out (the slot's button pressed once); the arc was drawn in %u presents%s", name,
              s.skill, pointcast::Length(at) / pointcast::kUnitsPerCell, g_drawnFx.load() + g_drawnBoxes.load(),
              g_drawnBoxes.load() ? " (as capsules: the effect has no PaOn - an older D2R_DepthFog.fx)"
              : g_drawnFx.load() ? "" : " - NONE: the effects never asked for it (no ReShade / no effect running, or the render thread saw no arc)");
}

bool Shown(Arc* out) {
    AcquireSRWLockShared(&g_lock);
    const bool up = g_up && g_arc.ok && viewgate::F4();
    if (up && out) *out = g_arc;
    ReleaseSRWLockShared(&g_lock);
    return up;
}

void NoteDrawn(bool byEffect) { (byEffect ? g_drawnFx : g_drawnBoxes).fetch_add(1, std::memory_order_relaxed); }

bool CastPoint(int skill, uint64_t nowMs, Offset* out, uint64_t maxAgeMs) {
    if (!g_on.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const Release r = g_release;
    ReleaseSRWLockShared(&g_lock);
    if (!r.circle || !r.ms || r.skill != skill || nowMs - r.ms > maxAgeMs) return false;
    if (out) *out = r.at;
    return true;
}

void CastDone() {
    AcquireSRWLockExclusive(&g_lock);
    g_release = {};
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace pointwire
