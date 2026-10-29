// shoulder_swap_wire.cpp - the weapon-set swap over the shoulder: [hands] shoulder_swap.
//
// A hand reached behind either shoulder, its grip pressed once, swaps weapon set I and II. The detector is
// shoulder_swap.h (pure, with a test on synthetic poses); here it is fed from direct_input.h's per-frame hands
// - native OpenXR, with the plugin's own controls ([openxr] direct_input) or with BodyWalk's - and its answer
// goes out through the game's own command: 0x2C "Swap weapons", pressed by gamecmd's executor as the key would be,
// with every one of the game's gates (a swap it refuses is not taken, and nothing else happens). Nothing here
// equips anything. What the game did is read back from its weapon set (gamestate::WeaponSet): a swap not seen
// to take effect is logged and never asked again by itself - the next press is a new request.
//
// The grip belongs to the gesture while the hand is behind a shoulder: direct_input's Take keeps it from the
// pad layout, and Claims() keeps the belt's bottles from it (vr/vrcam.cpp). Reaching back without the grip
// claims nothing, so a javelin's wind-up stays free.
//
// FlatVR with BodyWalk's input (no native session, no frames here): the old way stays - a BodyWalk gesture zone
// behind the shoulder bound to "D2R key: Swap weapons" (docs/SHOULDER_SWAP.md).
//
// Idea and numbers: bearstonem/d2r-pcvr (MIT), steamvr/shoulder_swap.h; written afresh.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "direct_input.h"
#include "shoulder_grip.h"
#include "shoulder_swap.h"
#include "shoulder_swap_wire.h"
#include "throw_gesture_wire.h"
#include "view_gate.h"

namespace gamestate { uint32_t WeaponSet(); }

namespace shoulderwire {
namespace {

constexpr uint32_t kSwapWeapons = 0x2C;   // the game's command table 0x22A7930: "Swap weapons" (shared/d2r_vr_shared.h)
constexpr ULONGLONG kClaimMs = 250;       // a claim older than this is stale (no frames)

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<bool> g_draw{false};            // [hands] shoulder_draw: the zone is a holster for both hands (the weight model draws), the swap is off
std::atomic<int> g_hand{0};                 // 0 right, 1 left
std::atomic<ULONGLONG> g_claimUntil[2] = {};   // [0 right, 1 left]: GetTickCount64
std::atomic<WeaponFn> g_weapon{nullptr};       // shoulder_draw: the weight model has a weapon (null: always)
std::atomic<StowedFn> g_stowed{nullptr};       // [weight] to_back: a weapon is stowed on the back (null: never)

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool Active() { return g_on.load() || g_draw.load(); }

// The draw thread's (OnFrame) alone.
struct State {
    shoulderswap::Gesture gesture;
    shoulderswap::Gesture holster[2];   // shoulder_draw: one claim for each hand (0 right, 1 left)
    shoulderswap::Heading holsterHeading;
    shoulderswap::Pending pending;
    shoulderswap::Heading heading;
    uint32_t session = 0, epoch = 0;
    bool seen = false;
    int jumps = 0;
};

void Release() { g_claimUntil[0].store(0); g_claimUntil[1].store(0); }

void OnFrame(const directinput::Frame& f, void*) {
    static State s;
    if (!Active()) {
        if (s.seen) { s = State{}; Release(); }
        return;
    }
    // A new native session forgets everything; a new context (focus, a menu) lets go of the press in progress.
    if (!s.seen || f.session != s.session) {
        s = State{};
        s.seen = true;
        s.session = f.session;
        s.epoch = f.epoch;
    } else if (f.epoch != s.epoch) {
        s.epoch = f.epoch;
        s.gesture.Reset();
        s.holster[0].Reset();
        s.holster[1].Reset();
    }
    if (g_draw.load()) {
        // The zone is a holster: both hands, each its own claim of a grip pressed behind a shoulder (and until it is let go). Nothing is swapped; the weight model reads
        // the same hand frames and the real grips and draws the weapon (vr/weight.cpp).
        const bool focused = (f.flags & directinput::kFocused) != 0;
        const WeaponFn weaponFn = g_weapon.load();
        const bool menu = (f.flags & (directinput::kInMenu | directinput::kOtherView)) != 0 || !viewgate::F4() || (weaponFn && !weaponFn());
        if (f.head.valid == 1) s.holsterHeading.Update(f.head.rot);
        for (int xr = 0; xr < 2; ++xr) {   // direct_input's: 0 left, 1 right
            const int side = xr ? 0 : 1;   // 0 right, 1 left (the ini's, D2RVR_Shared's)
            const directinput::Hand& h = f.hand[xr];
            shoulderswap::Input in;
            in.time = f.time;
            in.hand = side;
            in.tracked = focused && f.head.valid == 1 && h.active && h.grip.valid == 1 && s.holsterHeading.Relative(f.head.pos, h.grip.pos, in.pos);
            in.grip = h.squeeze;
            in.allowed = focused && !menu && !throwwire::GripIsThrowsAt(xr);
            in.canSwap = false;   // (the swap is never asked for)
            const shoulderswap::Output out = s.holster[side].Step(in);
            if (out.claim) {
                directinput::Take(xr, directinput::kSqueeze | directinput::kTakeSqueeze, directinput::kSrcShoulderSwap);
                g_claimUntil[side].store(GetTickCount64() + kClaimMs);
            } else {
                g_claimUntil[side].store(0);
            }
        }
        return;
    }
    const int hand = g_hand.load() ? 1 : 0;   // 0 right, 1 left (the ini's, D2RVR_Shared's)
    const int xrHand = hand ? 0 : 1;          // direct_input's: 0 left, 1 right
    const directinput::Hand& h = f.hand[xrHand];
    const bool focused = (f.flags & directinput::kFocused) != 0;
    // F4 only (vr/view_gate.h): another view is a menu as far as the gesture goes - no claim, no swap, the press in progress dropped
    const bool menu = (f.flags & (directinput::kInMenu | directinput::kOtherView)) != 0 || !viewgate::F4();
    if (f.head.valid == 1) s.heading.Update(f.head.rot);

    // What the game shows now, before the next request is allowed.
    const uint32_t set = gamestate::WeaponSet();
    char b[160];
    switch (s.pending.Update(f.time, set)) {
        case shoulderswap::Pending::Result::Applied:
            snprintf(b, sizeof b, "vrcam: shoulder swap - the game shows weapon set %u", set);
            Log(b);
            break;
        case shoulderswap::Pending::Result::NotTaken:
            Log("vrcam: shoulder swap - the game's weapon set did not change (a gate of the game's said no, or both sets are the "
                "same): not asked again - press the grip behind the shoulder once more");
            break;
        default:
            break;
    }

    shoulderswap::Input in;
    in.time = f.time;
    in.hand = hand;
    in.tracked = focused && f.head.valid == 1 && h.active && h.grip.valid == 1 && s.heading.Relative(f.head.pos, h.grip.pos, in.pos);
    in.grip = h.squeeze;
    // a menu or panel open, or no game yet: the grip is not ours; nor is the throwing hand's grip (the right; the left with [hands] throw_left_hand) while it is a throw's ([hands] throw_by_grip:
    // a throwing weapon in that hand, the grip held for as long as it is - swap sets by the right stick click, or the left hand)
    in.allowed = focused && !menu && !throwwire::GripIsThrowsAt(xrHand);
    in.canSwap = !s.pending.Busy() && (set == 1 || set == 2);
    const shoulderswap::Output out = s.gesture.Step(in);

    if (out.jump && s.jumps < 5) {
        ++s.jumps;
        Log("vrcam: shoulder swap - a jump of the hand's tracking: this press is not taken");
    }
    if (out.claim) {
        directinput::Take(xrHand, directinput::kSqueeze | directinput::kTakeSqueeze, directinput::kSrcShoulderSwap);
        g_claimUntil[hand].store(GetTickCount64() + kClaimMs);
    } else {
        g_claimUntil[hand].store(0);
    }
    g_claimUntil[1 - hand].store(0);
    if (out.swap) {
        // With a weapon stowed on the back ([weight] to_back) this press takes it into the hand instead (the weight model does that on its
        // own, from the same press); it is not also a swap. Nothing stowed: the swap, as ever.
        const StowedFn stowedFn = g_stowed.load();
        shouldergrip::In gi;
        gi.press = true;
        gi.inReach = out.inReach;
        gi.stowedOnBack = stowedFn && stowedFn();
        gi.swapAsked = true;
        const shouldergrip::Action act = shouldergrip::Choose(gi);
        if (act == shouldergrip::Action::TakeStowed) {
            Log("vrcam: shoulder swap - a weapon is stowed on the back: this grip behind the shoulder takes it, no swap");
        } else if (act == shouldergrip::Action::Swap && s.pending.Begin(f.time, set)) {
            snprintf(b, sizeof b, "vrcam: shoulder swap - the %s hand behind the shoulder, grip: Swap weapons (set %u shown, %u expected)",
                     hand ? "left" : "right", set, s.pending.Expected());
            Log(b);
            directinput::Tap(kSwapWeapons);
        }
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("vrcam: shoulder swap - direct_input's listener table is full: the shoulder swap does nothing");
}

void Configure(bool on, bool leftHand) {
    const bool was = g_on.exchange(on);
    const int wasHand = g_hand.exchange(leftHand ? 1 : 0);
    if (!on) Release();
    if (was != on || (on && wasHand != (leftHand ? 1 : 0))) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: shoulder swap %s%s%s", on ? "on - the " : "off", on ? (leftHand ? "left" : "right") : "",
                 on ? " hand behind either shoulder, grip (native OpenXR)" : "");
        Log(b);
    }
}

bool Claims(int hand) {
    if (hand < 0 || hand > 1 || !Active() || !viewgate::F4()) return false;
    return GetTickCount64() < g_claimUntil[hand].load();
}

void SetStowedSource(StowedFn stowedOnBack) { g_stowed.store(stowedOnBack); }
void SetWeaponSource(WeaponFn weaponPlaced) { g_weapon.store(weaponPlaced); }

void SetDraw(bool on) {
    const bool was = g_draw.exchange(on);
    if (!on) Release();
    if (was != on) Log(on ? "vrcam: shoulder draw on - the zone behind each shoulder holds that hand's item; \"Swap weapons\" is not pressed from it"
                          : "vrcam: shoulder draw off");
}

}  // namespace shoulderwire
