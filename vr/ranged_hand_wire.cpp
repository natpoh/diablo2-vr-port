// ranged_hand_wire.cpp - see ranged_hand_wire.h.
//
// One step per new head sample (the pair's second eye is posed from the same hands). The grips are the controllers' own (skel::Input::shieldRaw, or the layout's when there are none): a press
// is the grip going over 0.6 (it is let go under 0.4), and a grip already held when the holster starts is not a press; the hands are in the head's turn frame, so the shoulder zone is
// shoulderdraw::ZoneSide of them. Nothing is pressed in the game and no slot changes: only where the weapon is drawn and which controller is its hand.

#include "ranged_hand_wire.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>

#include "ranged_hand.h"
#include "shoulder_draw.h"
#include "shoulder_grip.h"

namespace rangedwire {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
rangedhand::Holster g_holster;
uint32_t g_sample = 0;
bool g_have = false;
bool g_down[2] = {};
bool g_toldOn = false;
shouldergrip::ZoneTick g_tick[2];
std::atomic<bool> g_stowed{false};
std::atomic<int> g_side{-1};
std::atomic<HapticFn> g_haptic{nullptr};
std::atomic<LogFn> g_log{nullptr};

void Log(const char* text) {
    if (const LogFn f = g_log.load()) f(text);
}
const char* Side(int s) { return s == 0 ? "right" : "left"; }

void Forget() {
    g_holster.Reset();
    g_have = false;
    g_down[0] = g_down[1] = false;
    g_tick[0].Reset();
    g_tick[1].Reset();
    g_toldOn = false;
}

}  // namespace

void SetHaptic(HapticFn fn) { g_haptic.store(fn); }
void SetLog(LogFn fn) { g_log.store(fn); }
bool Stowed() { return g_stowed.load(); }
int Side() { return g_side.load(); }

void Apply(skel::Input* in) {
    AcquireSRWLockExclusive(&g_lock);
    if (!in->rangedHolster || in->mode != 2) {
        if (g_side.load() >= 0) Log("vrcam: ranged holster off - the bow or crossbow is in its own hand, as the game has it");
        Forget();
        g_stowed.store(false);
        g_side.store(-1);
        in->rangedStowed = false;
        in->rangedSide = -1;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (!g_have || in->sample != g_sample) {
        const bool first = !g_have;
        g_have = true;
        g_sample = in->sample;
        if (!g_toldOn) {
            g_toldOn = true;
            g_holster.Reset(in->rangedHome);
            char b[220];
            snprintf(b, sizeof b, "vrcam: ranged holster on - a grip behind a shoulder puts the %s away and draws it%s", in->rangedBow ? "bow" : "crossbow",
                     in->rangedSwap && in->rangedBow ? ", into the hand that reached (the swap)" : ", back into its own hand");
            Log(b);
        }
        rangedhand::In si;
        si.weapon = true;
        si.swap = in->rangedSwap;
        si.bow = in->rangedBow;
        si.home = in->rangedHome;
        for (int side = 0; side < 2; ++side) {
            rangedhand::HandIn& h = si.hand[side];
            h.tracked = (in->handsValid & (1u << side)) != 0;
            const float g = in->shieldRaw[side] >= 0.0f ? in->shieldRaw[side] : in->grip[side];
            const bool down = g > 0.6f ? true : g < 0.4f ? false : g_down[side];
            h.press = !first && h.tracked && g >= 0.0f && down && !g_down[side];
            g_down[side] = g >= 0.0f ? down : false;
            h.inZone = h.tracked && shoulderdraw::ZoneSide(in->hand[side]) != shoulderdraw::Side::None;
            h.busy = in->shieldBusy[side] || in->rangedBusy[side];
        }
        const rangedhand::Out out = g_holster.Step(si);
        if (out.event == rangedhand::Event::PutAway) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: ranged holster - the %s hand puts the %s away behind a shoulder", Side(out.by), in->rangedBow ? "bow" : "crossbow");
            Log(b);
        } else if (out.event == rangedhand::Event::Drawn) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: ranged holster - the %s hand draws the %s from behind a shoulder, into the %s hand%s", Side(out.by), in->rangedBow ? "bow" : "crossbow", Side(out.side),
                     out.side != in->rangedHome ? " (not its own: the swap)" : "");
            Log(b);
        }
        // a short pulse: a hand enters a shoulder while the weapon is put away - there is something to draw
        const HapticFn pulse = g_haptic.load();
        const ULONGLONG now = GetTickCount64();
        for (int side = 0; side < 2; ++side) {
            const bool tick = g_tick[side].Step(out.stowed, si.hand[side].inZone, si.hand[side].busy, now);
            if (tick && pulse && in->shieldTick > 0.0f) pulse(side, in->shieldTick > 1.0f ? 1.0f : in->shieldTick, 30);
        }
        g_stowed.store(out.stowed);
        g_side.store(out.side);
    }
    in->rangedStowed = g_stowed.load();
    in->rangedSide = g_side.load();
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace rangedwire
