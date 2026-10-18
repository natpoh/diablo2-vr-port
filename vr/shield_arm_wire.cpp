// shield_arm_wire.cpp - see shield_arm_wire.h.
//
// One step per new head sample (the pair's second eye is posed from the same hands). The hand the weapon is drawn into or held by comes from the weight model (one pose late is fine). The
// put-away gesture reads the controllers' own grips (skel::Input::shieldRaw, or the layout's when there are none): a press is the grip going over 0.6 (it is let go under 0.4), and a grip
// already held when the wire starts is not a press; the hands are in the head's turn frame, so the shoulder zone is shoulderdraw::ZoneSide of them. Nothing is pressed in the game and no
// slot changes: only where the shield is drawn.

#include "shield_arm_wire.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>

#include "shield_arm.h"
#include "shoulder_draw.h"
#include "shoulder_grip.h"
#include "weight.h"

namespace shieldarmwire {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
shieldarm::Follower g_follower;
uint32_t g_sample = 0;
bool g_have = false;
bool g_down[2] = {};
bool g_wasFollow = false, g_wasStow = false, g_toldOn = false;
shouldergrip::ZoneTick g_tick[2];
// The put-away gesture told step by step (a gesture that "never fires" says where it stops): a hand coming behind the left shoulder, a squeeze there that did nothing, a squeeze elsewhere.
bool g_zoneWas[2] = {};
ULONGLONG g_zoneToldAt[2] = {}, g_pressToldAt[2] = {};
// Carrying the shield: a hand that wears it and holds its grip (squeezed anywhere, or held on arrival) free of weapon and potion HOLDS THE SHIELD; letting go behind the left shoulder puts it away.
// A grip that has just put it away or drawn it is spent until it is let go (a draw by a squeeze must not be put away again by the release that follows).
bool g_holding[2] = {}, g_spent[2] = {};
ULONGLONG g_holdSince[2] = {}, g_holdToldAt[2] = {}, g_letGoToldAt[2] = {};
shouldergrip::ZoneTick g_carryTick[2];
// The follow rule told each time the hands that hold the weapon change (a shield that "does not switch" says why: both hands on it, no hand on it, the weight model not holding it ...).
unsigned g_handsWas = 0xFFu;
ULONGLONG g_followWindowAt = 0;
int g_followTold = 0;
std::atomic<int> g_arm{-1};
std::atomic<WielderFn> g_wielder{nullptr};
std::atomic<HapticFn> g_haptic{nullptr};
std::atomic<LogFn> g_log{nullptr};

void Log(const char* text) {
    if (const LogFn f = g_log.load()) f(text);
}
const char* Side(int s) { return s == 0 ? "right" : "left"; }

void Forget() {
    g_follower.Reset();
    g_have = false;
    g_down[0] = g_down[1] = false;
    g_tick[0].Reset();
    g_tick[1].Reset();
    g_toldOn = false;
    g_zoneWas[0] = g_zoneWas[1] = false;
    g_zoneToldAt[0] = g_zoneToldAt[1] = g_pressToldAt[0] = g_pressToldAt[1] = 0;
    g_holding[0] = g_holding[1] = g_spent[0] = g_spent[1] = false;
    g_holdSince[0] = g_holdSince[1] = g_holdToldAt[0] = g_holdToldAt[1] = g_letGoToldAt[0] = g_letGoToldAt[1] = 0;
    g_carryTick[0].Reset();
    g_carryTick[1].Reset();
    g_handsWas = 0xFFu;
    g_followWindowAt = 0;   // (a new holster session: a fresh budget of lines)
    g_followTold = 0;
}
const char* HandsName(unsigned m) { return m == 1u ? "the right hand" : m == 2u ? "the left hand" : m == 3u ? "both hands" : "no hand"; }

}  // namespace

void SetWielderSource(WielderFn fn) { g_wielder.store(fn); }
void SetHaptic(HapticFn fn) { g_haptic.store(fn); }
void SetLog(LogFn fn) { g_log.store(fn); }
int Arm() { return g_arm.load(); }
bool Stowed() { return g_arm.load() == 2; }
int WearingHand() {
    const int a = g_arm.load();
    return a == 0 || a == 1 ? a : -1;
}

void Apply(skel::Input* in) {
    AcquireSRWLockExclusive(&g_lock);
    if (!in->shieldHolster || in->mode != 2) {
        if (g_arm.load() >= 0) {
            char b[260];
            snprintf(b, sizeof b, "vrcam: shield follows the sword off - the shield is on the left arm, as the game has it (%s)",
                     !in->shieldHolster ? "the holster is off - the 'shield holster - off' line says why: no shield in this weapon set, not the F4 view, a tick off" : "the arms are not on the controllers (hook mode is not 2)");
            Log(b);
        }
        Forget();
        g_arm.store(-1);
        in->shieldArm = -1;
        in->weight.shieldArm = -1;
        in->weight.shieldFollow = true;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (!g_have || in->sample != g_sample) {
        const bool first = !g_have;
        g_have = true;
        g_sample = in->sample;
        if (!g_toldOn || g_wasFollow != in->shieldFollow || g_wasStow != in->shieldStow) {
            g_toldOn = true;
            g_wasFollow = in->shieldFollow;
            g_wasStow = in->shieldStow;
            char b[260];
            snprintf(b, sizeof b, "vrcam: shield on the forearm, [hands] shoulder_draw: it %s the sword to the opposite arm (shoulder_draw_shield), it %s put away at the left shoulder (shoulder_draw_shield_stow)",
                     in->shieldFollow ? "follows" : "does NOT follow", in->shieldStow ? "can be" : "cannot be");
            Log(b);
        }
        const WielderFn wielder = g_wielder.load();
        shieldarm::In si;
        si.shield = true;
        si.follow = in->shieldFollow;
        si.stow = in->shieldStow;
        si.hands = wielder ? wielder() : 0u;
        const bool wasHolding[2] = {g_holding[0], g_holding[1]};
        for (int side = 0; side < 2; ++side) {
            shieldarm::HandIn& h = si.hand[side];
            h.tracked = (in->handsValid & (1u << side)) != 0;
            const float g = in->shieldRaw[side] >= 0.0f ? in->shieldRaw[side] : in->grip[side];
            const bool down = g > 0.6f ? true : g < 0.4f ? false : g_down[side];
            h.press = !first && h.tracked && g >= 0.0f && down && !g_down[side];
            g_down[side] = g >= 0.0f ? down : false;
            h.atShield = h.tracked && shoulderdraw::ZoneSide(in->hand[side]) == shoulderdraw::Side::Left;
            h.busy = in->shieldBusy[side] || weight::HoldsWeapon(side);
            // a hand that holds nothing and has its grip down holds the shield (when it wears it - the follower checks that): the release of it is a step of the gesture
            const bool downNow = g >= 0.0f && down;
            h.release = in->shieldStow && g_holding[side] && h.tracked && !downNow;
            if (!downNow) g_spent[side] = false;
            const bool holdingNow = in->shieldStow && h.tracked && downNow && !h.busy && !g_spent[side];
            if (holdingNow && !g_holding[side]) g_holdSince[side] = GetTickCount64();
            g_holding[side] = holdingNow;
        }
        const shieldarm::Arm was = g_follower.Now();
        const bool wasStowed = g_follower.Stowed();
        const shieldarm::Out out = g_follower.Step(si);
        if (out.event != shieldarm::Event::None && out.by >= 0) {   // the grip that did it is spent until it is let go
            g_spent[out.by] = true;
            g_holding[out.by] = false;
        }
        if (in->shieldStow) {   // the gesture, step by step
            const ULONGLONG at = GetTickCount64();
            for (int side = 0; side < 2; ++side) {
                const shieldarm::HandIn& h = si.hand[side];
                const float g = in->shieldRaw[side] >= 0.0f ? in->shieldRaw[side] : in->grip[side];
                const bool wearsNow = !wasStowed && was == (side == 0 ? shieldarm::Arm::Right : shieldarm::Arm::Left);   // (the hand whose arm the shield is on)
                const char* why = weight::HoldsWeapon(side) ? "it holds the weapon, so it can neither put the shield away nor draw it (the other hand does)"
                                : in->shieldBusy[side]     ? "it holds a belt potion, so it can neither put the shield away nor draw it"
                                : wasStowed                ? "squeeze its grip to draw the shield onto its arm"
                                : wearsNow && (g_holding[side] || h.release) ? "it holds the shield: let go of the grip here to put it away"
                                                           : "squeeze its grip, or carry the shield here with the grip held and let go, to put the shield away";
                if (wearsNow && g_holding[side] && !wasHolding[side] && at - g_holdToldAt[side] >= 1500) {   // the hand that wears the shield takes hold of it (a grip squeezed anywhere, or held on arrival)
                    g_holdToldAt[side] = at;
                    char b[260];
                    snprintf(b, sizeof b, "vrcam: shield holster - the %s hand holds the shield (grip %.2f%s): carry it behind the left shoulder and let go there to put it away", Side(side), g, first ? ", held already" : "");
                    Log(b);
                }
                if (h.release && out.event == shieldarm::Event::None && !h.atShield && wearsNow && at - g_holdSince[side] >= 250 && at - g_letGoToldAt[side] >= 1500) {   // let go of a carried shield elsewhere
                    g_letGoToldAt[side] = at;
                    const shoulderswap::Zone zz = shoulderswap::GetZone();
                    char b[320];
                    snprintf(b, sizeof b, "vrcam: shield holster - the %s hand let go of the shield's grip at x %.2f y %.2f z %.2f m (right, up, back from the head): not behind the left shoulder (x %.2f..%.2f, y %.2f..%.2f, z %.2f..%.2f) - the shield stays",
                             Side(side), in->hand[side][0], in->hand[side][1], in->hand[side][2], -zz.absXMax, -zz.absXMin, zz.yMin, zz.yMax, zz.zMin, zz.zMax);
                    Log(b);
                }
                if (h.atShield && !g_zoneWas[side] && at - g_zoneToldAt[side] >= 500) {   // the hand comes behind the left shoulder
                    g_zoneToldAt[side] = at;
                    char b[260];
                    snprintf(b, sizeof b, "vrcam: shield holster - the %s hand is behind the left shoulder (grip %.2f, read from the %s): %s", Side(side), g,
                             in->shieldRaw[side] >= 0.0f ? "controller" : "layout", why);
                    Log(b);
                }
                g_zoneWas[side] = h.atShield;
                if (h.press && out.event == shieldarm::Event::None) {   // a squeeze that did nothing
                    if (h.atShield) {
                        char b[260];
                        snprintf(b, sizeof b, "vrcam: shield holster - the %s hand squeezed behind the left shoulder and nothing happened: %s", Side(side), h.busy ? why : "another hand changed the shield in the same sample");
                        Log(b);
                    } else if (at - g_pressToldAt[side] >= 1500) {
                        g_pressToldAt[side] = at;
                        const shoulderdraw::Side zone = shoulderdraw::ZoneSide(in->hand[side]);
                        char b[320];
                        const shoulderswap::Zone zz = shoulderswap::GetZone();
                        snprintf(b, sizeof b, "vrcam: shield holster - the %s hand squeezed its grip at x %.2f y %.2f z %.2f m (right, up, back from the head): %s; the left shoulder's zone is x %.2f..%.2f, y %.2f..%.2f, z %.2f..%.2f",
                                 Side(side), in->hand[side][0], in->hand[side][1], in->hand[side][2],
                                 zone == shoulderdraw::Side::Right ? "behind the RIGHT shoulder (the weapon's), not the left" : "not behind the left shoulder",
                                 -zz.absXMax, -zz.absXMin, zz.yMin, zz.yMax, zz.zMin, zz.zMax);
                        Log(b);
                    }
                }
            }
        }
        if (out.event == shieldarm::Event::PutAway) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: shield holster - the %s hand puts the shield away at the left shoulder (it was on the %s arm)%s", Side(out.by), was == shieldarm::Arm::Right ? "right" : "left",
                     out.byRelease ? " - carried there with the grip held, let go" : " - squeezed there");
            Log(b);
        } else if (out.event == shieldarm::Event::Drawn) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: shield holster - the %s hand draws the shield from the left shoulder onto the %s arm%s", Side(out.by), out.arm == shieldarm::Arm::Right ? "right" : "left",
                     out.arm == shieldarm::Arm::Right ? " (mirrored)" : "");
            Log(b);
        }
        if (out.changed) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: shield follows the sword - the weapon is in the %s hand, the shield jumps to the %s arm%s", out.arm == shieldarm::Arm::Right ? "left" : "right",
                     out.arm == shieldarm::Arm::Right ? "right" : "left", out.arm == shieldarm::Arm::Right ? " (mirrored)" : "");
            Log(b);
        }
        if (si.hands != g_handsWas) {   // the hands that hold the weapon changed: what the shield does about it, and why - told when it does NOT jump (a jump has its line above)
            g_handsWas = si.hands;
            const ULONGLONG at = GetTickCount64();
            if (at - g_followWindowAt >= 1000) { g_followWindowAt = at; g_followTold = 0; }
            if (!out.changed && !first && ++g_followTold <= 6) {
                const bool right = out.arm == shieldarm::Arm::Right;
                const char* why = out.stowed ? "it is put away (shoulder_draw_shield_stow): there is no arm to move"
                                : !in->shieldFollow ? "[hands] shoulder_draw_shield is off"
                                : si.hands == 0u ? (weight::Placed() ? "no hand holds it (it is falling, lying, on the back, or just let go)"
                                                                     : "the weight model does not hold this weapon (see 'weight - off'): there is no hand to follow")
                                : si.hands == 3u ? "both hands are on it: the shield stays - it moves when only one hand is left on it"
                                                 : "that is the hand opposite the shield: it is where it should be";
                char b[300];
                snprintf(b, sizeof b, "vrcam: shield follow - the weapon is now in %s; the shield stays on the %s arm: %s", HandsName(si.hands), right ? "right" : "left", why);
                Log(b);
            }
        }
        // a short pulse: a hand enters the left shoulder while the shield is put away - there is something to draw; or carries the shield into it - there is something to let go
        const HapticFn pulse = g_haptic.load();
        const ULONGLONG now = GetTickCount64();
        for (int side = 0; side < 2; ++side) {
            bool tick = g_tick[side].Step(out.stowed, si.hand[side].atShield, si.hand[side].busy, now);
            const bool carries = in->shieldStow && g_holding[side] && !out.stowed && out.arm == (side == 0 ? shieldarm::Arm::Right : shieldarm::Arm::Left);
            tick = g_carryTick[side].Step(carries, si.hand[side].atShield, si.hand[side].busy, now) || tick;
            if (tick && pulse && in->shieldTick > 0.0f) pulse(side, in->shieldTick > 1.0f ? 1.0f : in->shieldTick, 30);
        }
        g_arm.store(out.stowed ? 2 : (int)out.arm);
    }
    const int arm = g_arm.load();
    in->shieldArm = arm;
    in->weight.shieldArm = arm;
    in->weight.shieldFollow = in->shieldFollow;
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace shieldarmwire
