// stand_still_wire.cpp - attacks that never walk the hero, on the game's pad poll: see stand_still.h for why and for the
// logic, and docs/move_recon.md section 5 for the click it avoids.
//
// Only the game's own paths: the key command "Stand still" (0x24) is held through direct_input.h's Tap (a lease of a few
// ms, refreshed every poll, so a stopped poll lets it go by itself) and pressed by vr/gamecmd.cpp with the game's
// executor; the attack goes out as it always did, a pad button the game reads in its own poll - only a few ms later, once the game
// says the command is down. F4 only (vr/view_gate.h).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include "direct_feed.h"   // PadPulses (and, behind it, direct_input.h)
#include "shot_trace_wire.h"
#include "stand_still.h"
#include "stand_still_wire.h"
#include "view_gate.h"
#include "wheel_game.h"
#include "wheel_types.h"

namespace gamecmd { bool Pressed(uint32_t cmd); }   // vr/gamecmd.cpp

namespace standstill {
namespace {

constexpr uint32_t kStandStill = 0x24;   // the game's "Stand still" (Shift)
constexpr uint16_t kSkillButtons = 0x2000 | 0x4000 | 0x8000 | 0x0200;   // B X Y RB: the skill slots a pad press names
constexpr uint16_t kPadA = 0x1000;
constexpr int kLogMax = 300;             // lines a session: one per press

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_on{true};
std::atomic<bool> g_busy{false};             // the machine waits for / delivers / is in the tail of an attack press (mirror of g_m.Busy())
std::atomic<bool> g_throwStill{true};        // [hands] throw_stand_still
bool (*g_isThrowPress)() = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
Machine g_m;
// the skill slots as the skill panel's reading has them (empty while that is off: every slot unknown)
wheel::Snapshot g_snap;
uint32_t g_rev = 0;
bool g_have = false;
ULONGLONG g_snapAt = 0;
// the press in progress
bool g_pressing = false, g_pendA = false;
uint16_t g_pend = 0;
char g_what[96] = "";
int g_lines = 0;
bool g_degradedTold = false;

void Say(const char* fmt, ...) {
    if (!g_log || g_lines >= kLogMax) return;
    char b[256];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    ++g_lines;
    g_log(b);
    if (g_lines == kLogMax) g_log("vrcam: stand still - 300 lines said, quiet from here on");
}

void Refresh(ULONGLONG now) {
    if (now - g_snapAt < 100) return;
    g_snapAt = now;
    g_have = wheelgame::Latest(&g_snap, &g_rev) && g_snap.ready;
}

// The skill a pad button casts (with the left trigger held: its second row); -1 not known.
int32_t SlotSkill(uint16_t button, bool alt) {
    if (!g_have) return -1;
    for (int i = 0; i < wheel::kSlotCount; ++i) {
        const wheel::SlotDef& d = wheel::kSlotDefs[i];
        if (d.button == button && d.alt == alt) return g_snap.slot[i].known ? g_snap.slot[i].id : -1;
    }
    return -1;
}

const char* ButtonName(uint16_t b) {
    switch (b) {
    case 0x2000: return "B";
    case 0x4000: return "X";
    case 0x8000: return "Y";
    case 0x0200: return "RB";
    default: return "?";
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

bool Wanted() { return g_on.load(); }
bool AttackActive() { return g_busy.load(std::memory_order_acquire); }

void NoteClick(int type) {
    static std::atomic<uint32_t> pending{0}, total{0};
    static std::atomic<ULONGLONG> lastSaid{0};
    pending.fetch_add(1);
    const uint32_t all = total.fetch_add(1) + 1;
    const ULONGLONG now = GetTickCount64();
    ULONGLONG was = lastSaid.load();
    if (all != 1 && now - was < 10000) return;
    if (!lastSaid.compare_exchange_strong(was, now)) return;   // (another thread is saying it)
    const uint32_t n = pending.exchange(0);
    if (!g_log) return;
    char b[256];
    if (all == 1 && n == 1)
        snprintf(b, sizeof b, "vrcam: stand still - a click on bare ground (type %d) was set to Stand Still: an attack in place, not a walk "
                              "([gameplay] attack_in_place, VR first person only)", type);
    else
        snprintf(b, sizeof b, "vrcam: stand still - %u click(s) on bare ground set to Stand Still since the last line (%u in all this run)", n, all);
    g_log(b);
}
void SetThrowStill(bool on) {
    if (g_throwStill.exchange(on) != on && g_log)
        g_log(on ? "vrcam: stand still - [hands] throw_stand_still=1: a throw by gesture is pressed with Stand Still like any attack of ours"
                 : "vrcam: stand still - [hands] throw_stand_still=0: a throw by gesture is pressed WITHOUT Stand Still");
}
void SetThrowPressFn(bool (*isThrowPress)()) { g_isThrowPress = isThrowPress; }

void SetWanted(bool on) {
    if (g_on.exchange(on) != on) {
        AcquireSRWLockExclusive(&g_lock);
        g_m.Reset();
        g_pressing = false;
        ReleaseSRWLockExclusive(&g_lock);
        if (g_log) g_log(on ? "vrcam: stand still - [gameplay] attack_in_place=1: in VR first person an attack of the plugin's is pressed with the game's Stand Still, so it never walks the hero"
                            : "vrcam: stand still - [gameplay] attack_in_place=0: attacks as they were");
    }
}

Gate Poll(bool attackA, bool touch, bool menu, uint16_t padButtons, uint8_t lt) {
    Gate g;
    const ULONGLONG now = GetTickCount64();
    const bool alt = lt >= 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD: the second row of slots
    // The skill buttons of ours in this poll: what the gestures pulse (an attack skill by their nature: the bow's arrow, the smite, the
    // panel's cast - an unknown skill there is held for), and, when the plugin is the input, the pad's own layout (an unknown skill
    // there is left alone: it may be a Leap).
    uint16_t pulsed = 0, layout = 0;
    {
        uint16_t pb = 0;
        uint8_t plt = 0, prt = 0;
        if (directinput::PadPulses(&pb, &plt, &prt)) pulsed = pb & kSkillButtons;
        if (directinput::Active()) layout = padButtons & kSkillButtons & (uint16_t)~pulsed;
    }
    const uint16_t buttons = (uint16_t)(pulsed | layout);
    const bool on = g_on.load() && viewgate::F4() && !menu;
    AcquireSRWLockExclusive(&g_lock);
    if (!on) {
        if (g_m.Busy()) { g_m.Reset(); g_pressing = false; }   // (the lease runs out within a few ms)
        g_busy.store(false, std::memory_order_release);
        shottrace::Observe(false);
        ReleaseSRWLockExclusive(&g_lock);
        return g;
    }
    if (attackA || buttons) Refresh(now);
    // which of the presses want Stand Still
    bool wanted = false;
    uint16_t wantedButtons = 0;
    Why attackWhy = Why::Held;
    if (attackA) {
        const int32_t id = SlotSkill(kPadA, alt);
        attackWhy = id < 0 ? Why::Held : SkillWhy(id);   // the default attack is a basic attack unless the slot says otherwise
        if (attackWhy == Why::Held && !g_throwStill.load() && g_isThrowPress && g_isThrowPress()) attackWhy = Why::Throw;
        wanted = attackWhy == Why::Held;
    }
    Why firstWhy = attackA ? attackWhy : Why::Held;
    int32_t firstSkill = -1;
    uint16_t firstBtn = 0;
    for (uint16_t bit = 1; bit; bit = (uint16_t)(bit << 1)) {
        if (!(buttons & bit)) continue;
        const int32_t id = SlotSkill(bit, alt);
        const Why w = (pulsed & bit) && id < 0 ? Why::Held : SkillWhy(id);
        if (!firstBtn) { firstBtn = bit; firstSkill = id; if (!attackA) firstWhy = w; }
        if (w == Why::Held) { wanted = true; wantedButtons |= bit; }
    }
    const bool pressNow = attackA || buttons != 0;
    // one line for every press: what it is, and whether Stand Still is held for it
    if (pressNow && !g_pressing) {
        const char* what = touch ? "hit by touch" : attackA ? "attack" : "skill";
        if (attackA) snprintf(g_what, sizeof g_what, "%s", what);
        else snprintf(g_what, sizeof g_what, "skill button %s, skill %d", ButtonName(firstBtn), (int)firstSkill);
        if (!wanted) Say("vrcam: stand still - press (%s): %s", g_what, WhyText(firstWhy));
    }
    g_pressing = pressNow;
    if (wanted) { g_pendA = attackA && attackWhy == Why::Held; g_pend = wantedButtons; }
    const bool confirmed = gamecmd::Pressed(kStandStill);
    const Out o = g_m.Step(wanted, confirmed, now);
    g_busy.store(g_m.Busy(), std::memory_order_release);
    shottrace::Observe(g_m.Busy());   // [debug] shot_trace: a shot begins and ends with the machine
    if (o.holdCommand) directinput::Tap(kStandStill);   // a lease of a few ms: refreshed by the next poll
    if (o.delivered) {
        if (o.timedOut)
            Say("vrcam: stand still - press (%s): the game's Stand Still was NOT down after %u ms - the press goes out without%s", g_what,
                (unsigned)o.waitedMs, g_m.Degraded() ? " (twice in a row: not waited for any more)" : "");
        else
            Say("vrcam: stand still - press (%s): Stand Still held (down after %u ms)", g_what, (unsigned)o.waitedMs);
    }
    if (g_m.Degraded() && !g_degradedTold) {
        g_degradedTold = true;
        Say("vrcam: stand still - the game's Stand Still command did not come down in time twice in a row: attacks are no longer held back for it "
            "(still asked for) - is the game's command executor found? (see 'game commands' above in the log)");
    }
    // the poll's verdict
    if (wanted && !o.deliver) {
        g.holdBackA = g_pendA && attackA;
        g.holdBack = wantedButtons;
    } else if (!wanted && o.deliver && g_m.Busy()) {   // the source let go while the press is stretched to its least length
        g.addA = g_pendA;
        g.add = g_pend;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return g;
}

}  // namespace standstill
