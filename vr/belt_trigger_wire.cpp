// belt_trigger_wire.cpp - see belt_trigger_wire.h. A frame listener of direct_input.h: while a belt bottle is in a hand the triggers are taken from the layout
// (vr/belt_trigger.h says which), so the game's drink is not made with its "shift" (the left trigger) and no attack goes out of the right trigger.
//
// Nothing here moves a bottle or presses anything: it only keeps a trigger that belongs to the belt's gesture from reaching the pad.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>

#include "belt_trigger.h"
#include "belt_trigger_wire.h"
#include "direct_input.h"
#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "view_gate.h"

namespace belttrigger {
namespace {

constexpr ULONGLONG kStaleMs = 300;   // a belt word older than this is not "now"

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<uint32_t> g_word{0};      // bit 0 enabled, bit 1 a bottle in the right hand, bit 2 in the left
std::atomic<ULONGLONG> g_wordAt{0};
std::atomic<uint32_t> g_took{0};      // bit 0 the left trigger taken, bit 1 the right: for the log, on a change

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

void OnFrame(const directinput::Frame& f, void*) {
    // F4 only (vr/view_gate.h); a menu or panel open, or no input focus: the triggers are not the belt's (the pad is the menu's)
    const bool ok = viewgate::F4() && (f.flags & directinput::kFocused) &&
                    !(f.flags & (directinput::kInMenu | directinput::kOtherView | directinput::kUiUp));
    Out o;
    if (ok) {
        const ULONGLONG at = g_wordAt.load(std::memory_order_acquire);
        if (at && GetTickCount64() - at < kStaleMs) {
            const uint32_t w = g_word.load(std::memory_order_relaxed);
            In in;
            in.enabled = (w & 1u) != 0;
            in.holds[0] = (w & 2u) != 0;
            in.holds[1] = (w & 4u) != 0;
            o = Decide(in);
        }
    }
    if (o.left) directinput::Take(0, directinput::kTrigger | directinput::kTakeTrigger, directinput::kSrcOther3);
    if (o.right) directinput::Take(1, directinput::kTrigger | directinput::kTakeTrigger, directinput::kSrcOther3);
    const uint32_t took = (o.left ? 1u : 0u) | (o.right ? 2u : 0u);
    if (took != g_took.exchange(took)) {
        if (took)
            Log(o.right ? "vrcam: belt potions - a bottle in the hand: both triggers are the belt's now (the left one is what the game reads as Shift on a belt key, the right one its attack)"
                        : "vrcam: belt potions - a bottle in the hand: the left trigger is the belt's now (the game reads it as Shift on a belt key)");
        else
            Log("vrcam: belt potions - no bottle in the hand: the triggers are the layout's again");
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("vrcam: belt potions - direct_input's listener table is full: a trigger squeezed with a bottle in the hand stays the pad's (the game may read the drink as Shift + belt key)");
}

void Publish(bool enabled, bool rightHolds, bool leftHolds) {
    g_word.store((enabled ? 1u : 0u) | (rightHolds ? 2u : 0u) | (leftHolds ? 4u : 0u), std::memory_order_relaxed);
    g_wordAt.store(GetTickCount64(), std::memory_order_release);
}

}  // namespace belttrigger
