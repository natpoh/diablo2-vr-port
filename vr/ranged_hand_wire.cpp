// ranged_hand_wire.cpp - see ranged_hand_wire.h.
//
// One step per new head sample (the pair's second eye is posed from the same hands). The grips are the controllers' own (skel::Input::shieldRaw, or the layout's when there are none): a press
// is the grip going over 0.6 (it is let go under 0.4), and a grip already held when the holster starts is not a press; the hands are in the head's turn frame, so the shoulder zone is
// shoulderdraw::ZoneSide of them. Nothing is pressed in the game and no slot changes: only where the weapon is drawn and which controller is its hand.
// [bow] swap_by_grab: the hand without the bow is tested against the bow's grip (bowswap::AtGrip, from the bow hand's place and turn in the same frame); its press there passes the
// bow (Event::Passed) - a log line, a pulse in the hand that took it, and the side to vrcam to remember.

#include "ranged_hand_wire.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>

#include "bow_swap.h"
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
std::atomic<RememberFn> g_remember{nullptr};
std::atomic<bool> g_blockDraw{false};
std::atomic<ULONGLONG> g_blockAt{0};
constexpr ULONGLONG kFreshMs = 250;

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
    g_blockDraw.store(false);
}

bowdraw::Vec HandV(const skel::Input* in, int side) { return {in->hand[side][0], in->hand[side][1], in->hand[side][2]}; }
bowdraw::Quat HandQ(const skel::Input* in, int side) { return {in->rot[side][0], in->rot[side][1], in->rot[side][2], in->rot[side][3]}; }

}  // namespace

void SetHaptic(HapticFn fn) { g_haptic.store(fn); }
void SetLog(LogFn fn) { g_log.store(fn); }
void SetRemember(RememberFn fn) { g_remember.store(fn); }
bool BlocksDraw() {
    if (!g_blockDraw.load()) return false;
    const ULONGLONG at = g_blockAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}
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
            char b[320];
            if (in->rangedShoulder) {
                snprintf(b, sizeof b, "vrcam: ranged holster on - a grip behind a shoulder puts the %s away and draws it%s", in->rangedBow ? "bow" : "crossbow",
                         in->rangedSwap && in->rangedBow ? ", into the hand that reached (the swap)" : in->rangedGrab && in->rangedBow ? ", into the hand it was in" : ", back into its own hand");
                Log(b);
            }
            if (in->rangedGrab && in->rangedBow) {
                snprintf(b, sizeof b, "vrcam: bow swap by grab on - the bow starts in the %s hand ([bow] bow_hand); the free hand's grip squeezed at the bow's grip (within %.0f cm, nearer it "
                         "than the string) takes it", bowswap::SideName(in->rangedHome), bowswap::kReachM * 100.0f);
                Log(b);
            }
        }
        rangedhand::In si;
        si.weapon = true;
        si.swap = in->rangedSwap;
        si.bow = in->rangedBow;
        si.home = in->rangedHome;
        si.shoulder = in->rangedShoulder;
        si.grab = in->rangedGrab;
        const bool grab = in->rangedGrab && in->rangedBow;
        const int holder = g_holster.Side();
        bool blockDraw = false;
        for (int side = 0; side < 2; ++side) {
            rangedhand::HandIn& h = si.hand[side];
            h.tracked = (in->handsValid & (1u << side)) != 0;
            const float g = in->shieldRaw[side] >= 0.0f ? in->shieldRaw[side] : in->grip[side];
            const bool down = g > 0.6f ? true : g < 0.4f ? false : g_down[side];
            h.press = !first && h.tracked && g >= 0.0f && down && !g_down[side];
            g_down[side] = g >= 0.0f ? down : false;
            h.inZone = h.tracked && shoulderdraw::ZoneSide(in->hand[side]) != shoulderdraw::Side::None;
            h.busy = in->shieldBusy[side] || in->rangedBusy[side];
            // [bow] swap_by_grab: the hand without the bow, against the bow's grip (both hands tracked: the bow is where its hand is)
            if (grab && !g_holster.Stowed() && side != holder && h.tracked && (in->handsValid & (1u << holder))) {
                h.atGrip = bowswap::AtGrip(HandV(in, side), HandV(in, holder), HandQ(in, holder));
                blockDraw = h.atGrip && !h.busy;
                const bowdraw::Vec at = HandV(in, side), bow = HandV(in, holder);
                const float toGrip = bowdraw::Length(at - bow), toString = bowdraw::Length(at - bowswap::StringPoint(bow, HandQ(in, holder)));
                if (h.press && toGrip < 0.45f) {   // each squeeze near the bow, for the log: where the hand was against its grip and its string
                    char b[260];
                    snprintf(b, sizeof b, "vrcam: bow swap - the %s hand's grip %.0f cm from the bow's grip, %.0f cm from its string: %s", Side(side), toGrip * 100.0f, toString * 100.0f,
                             h.busy ? "the hand is busy (the string, a potion)" : h.atGrip ? "at the grip - takes the bow"
                             : toGrip > bowswap::kReachM ? "too far from the grip" : "nearer the string - the string's (the draw)");
                    Log(b);
                }
            }
            if (in->rangedShoulder && h.press) {   // every grip press, for the log: where the hand was against the shoulder box (0.170: "the crossbow does not go behind the back")
                const shoulderswap::Zone z = shoulderswap::GetZone();
                char b[300];
                snprintf(b, sizeof b, "vrcam: ranged holster - the %s hand's grip at x %.0f y %.0f z %.0f cm from the head (x right, y up, z back): %s the shoulder box "
                         "(|x| %.0f..%.0f, y %.0f..%.0f, z %.0f..%.0f)%s", Side(side), in->hand[side][0] * 100.0f, in->hand[side][1] * 100.0f, in->hand[side][2] * 100.0f,
                         h.inZone ? "INSIDE" : "outside", z.absXMin * 100.0f, z.absXMax * 100.0f, z.yMin * 100.0f, z.yMax * 100.0f, z.zMin * 100.0f, z.zMax * 100.0f,
                         h.busy ? " - but the hand is busy (a potion, or the string)" : "");
                Log(b);
            }
        }
        g_blockDraw.store(blockDraw);
        g_blockAt.store(GetTickCount64());
        const rangedhand::Out out = g_holster.Step(si);
        if (out.event == rangedhand::Event::Passed) {
            char b[260];
            snprintf(b, sizeof b, "vrcam: bow swap - the %s hand takes the bow at its grip: the bow is in the %s hand now, the string the %s hand's (remembered: [bow] bow_hand=%d)",
                     Side(out.by), Side(out.side), Side(out.side ^ 1), out.side);
            Log(b);
            if (const HapticFn pulse = g_haptic.load()) if (in->shieldTick > 0.0f) pulse(out.by, in->shieldTick > 1.0f ? 1.0f : in->shieldTick, 45);
        }
        if (grab && out.side != holder && (out.event == rangedhand::Event::Passed || out.event == rangedhand::Event::Drawn))
            if (const RememberFn remember = g_remember.load()) remember(out.side);
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
