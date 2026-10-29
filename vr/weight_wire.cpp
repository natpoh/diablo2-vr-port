// weight_wire.cpp - see weight_wire.h. A frame listener of direct_input.h (native OpenXR, with the plugin's own controls or with BodyWalk's):
// while the weight model has a hand on the weapon, that hand's grip is taken from the layout; the model reads the real grips from the frame.
//
// Nothing here moves a weapon or presses anything: it only keeps a grip that is the weapon's from being a chord's modifier.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "direct_input.h"
#include "view_gate.h"
#include "weight.h"
#include "weight_wire.h"

namespace weightwire {
namespace {

constexpr ULONGLONG kStaleMs = 300;   // frames older than this are not "now"

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<uint32_t> g_counter{0};
std::atomic<ULONGLONG> g_counterAt{0};
std::atomic<bool> g_taking{false};
std::atomic<bool> g_gripNoChord{false};   // [weight] weapon_grip_no_chord

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

void OnFrame(const directinput::Frame& f, void*) {
    if (f.counter != g_counter.load(std::memory_order_relaxed)) {
        g_counter.store(f.counter, std::memory_order_relaxed);
        g_counterAt.store(GetTickCount64(), std::memory_order_relaxed);
    }
    // F4 only (vr/view_gate.h); a menu or panel open, or no input focus: the grips are not the weapon's (the pad is the menu's)
    const bool ok = weight::Placed() && viewgate::F4() && (f.flags & directinput::kFocused) &&
                    !(f.flags & (directinput::kInMenu | directinput::kOtherView));
    bool took = false;
    if (ok) {
        for (int side = 0; side < 2; ++side) {   // weight's hands: 0 right, 1 left; direct_input's: 0 left, 1 right
            if (!weight::HoldsWeapon(side)) continue;
            directinput::Take(1 - side, directinput::kSqueeze | directinput::kTakeSqueeze, directinput::kSrcOther1);
            took = true;
        }
    }
    if (took != g_taking.exchange(took))
        Log(took ? "vrcam: weight - the grip of a hand on the weapon is the weapon's: not a chord's modifier"
                 : "vrcam: weight - no hand on the weapon: the grips are the layout's again");
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("vrcam: weight - direct_input's listener table is full: a grip held on the weapon stays a chord's modifier");
}

bool RawGrips(float out[2]) {
    if (!weight::Placed()) return false;   // (nothing of the weight model's: the shared block's grips are used as ever)
    if (GetTickCount64() - g_counterAt.load(std::memory_order_relaxed) > kStaleMs) return false;
    directinput::Frame f;
    if (!directinput::Latest(&f) || f.version != directinput::kVersion) return false;
    // (unfocused: the poses are held ones and nothing is pressed: the grips read as let go, as the controllers' real state)
    out[0] = std::clamp(f.hand[1].squeeze, 0.0f, 1.0f);   // right
    out[1] = std::clamp(f.hand[0].squeeze, 0.0f, 1.0f);   // left
    return true;
}

void ConfigureGripNoChord(bool on) {
    const bool was = g_gripNoChord.exchange(on);
    static bool told = false;
    if (told && was == on) return;
    told = true;
    Log(on ? "vrcam: weight - a grip that holds the weapon (or that a gesture holds) is no modifier: it neither makes the wheel's grip + click chord nor keeps the right A / trigger from their skill slots ([weight] weapon_grip_no_chord)"
           : "vrcam: weight - a grip that holds the weapon still counts for the wheel's chord and the slot controls, as before ([weight] weapon_grip_no_chord=0)");
}

bool GripNoChord() { return g_gripNoChord.load(); }

bool GripOnWeapon(int hand) {
    if (!g_gripNoChord.load() || hand < 0 || hand > 1 || !viewgate::F4() || !weight::Placed()) return false;
    return weight::HoldsWeapon(1 - hand);   // weight's hands: 0 right, 1 left
}

bool RawTriggers(float out[2]) {
    if (!weight::Placed()) return false;
    if (GetTickCount64() - g_counterAt.load(std::memory_order_relaxed) > kStaleMs) return false;
    directinput::Frame f;
    if (!directinput::Latest(&f) || f.version != directinput::kVersion) return false;
    out[0] = std::clamp(f.hand[1].trigger, 0.0f, 1.0f);   // right
    out[1] = std::clamp(f.hand[0].trigger, 0.0f, 1.0f);   // left
    return true;
}

}  // namespace weightwire
