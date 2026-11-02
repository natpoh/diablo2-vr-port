// point_pickup_wire.cpp - see point_pickup_wire.h. [input] point_pickup's highlight and trigger, F4 only.
//
// Nothing here picks anything up by itself: the trigger's press is the layout's interact (actions.cpp ApplyActions), its target the game's own
// interact lookup answered with the item (vr/input/flat_click.cpp PointPickupAnswer) - the game's pick-up, its walk, its checks. Show items is the
// game's own command (gamecmd's executor: no key, no pad - the UI stays the controller's).

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

#include "bow_draw_wire.h"
#include "direct_input.h"
#include "haptics.h"
#include "log_async.h"
#include "point_pickup_wire.h"
#include "point_trigger.h"
#include "unleashed_loot_aim.h"
#include "view_gate.h"

namespace pointpickup {
namespace {

constexpr ULONGLONG kFreshMs = 150;       // a highlight older than this is not "now" (PointPickupAnswer stopped asking: a menu, another view)
constexpr ULONGLONG kPressMs = 3000;      // a trigger press the game has not answered (the item still on the floor) ends after this
constexpr ULONGLONG kLaidOutMs = 300;     // the game laid the lit item's label out this lately by itself: no Show items held for it
constexpr ULONGLONG kHoldLeaseMs = 250;   // the other names are cut this far ahead while Show items is held (OnlyPointedUntil)
constexpr uint32_t kCmdShowItems = 0x25;  // the game's Show items (Alt) command (shared/d2r_vr_shared.h kD2RVRCommands)
constexpr directinput::Source kSrc = directinput::kSrcOther4;

const D2RL::PluginContext* g_ctx = nullptr;
// the highlight (PointPickupAnswer, the game's UI thread)
std::atomic<uint32_t> g_id{0};
std::atomic<uintptr_t> g_unit{0};
std::atomic<int> g_hand{-1};
std::atomic<bool> g_triggerOk{true};     // that hand's trigger may pick up (not a crossbow's)
std::atomic<bool> g_inReach{true};
std::atomic<float> g_rgb[3] = {1.0f, 1.0f, 1.0f};
std::atomic<bool> g_labelsIn{false};
std::atomic<ULONGLONG> g_at{0};
std::atomic<ULONGLONG> g_laidOutAt{0};    // the lit item's label laid out, when (the label hook)
std::atomic<ULONGLONG> g_litSince{0};     // the highlight came to this item, when
// Show items held for the lit item (the listener)
std::atomic<bool> g_holding{false};
std::atomic<ULONGLONG> g_onlyUntil{0};
// the trigger's press
std::atomic<uint32_t> g_pressId{0};
std::atomic<ULONGLONG> g_pressAt{0};

pointtrigger::Trigger g_trigger[2];       // the listener's thread
bool g_tookLast[2] = {false, false};

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool Fresh(ULONGLONG now) {
    const ULONGLONG at = g_at.load();
    return g_id.load() && at && now >= at && now - at < kFreshMs;
}

void OnFrame(const directinput::Frame& f, void*) {
    const ULONGLONG now = GetTickCount64();
    const bool ok = viewgate::F4() && (f.flags & directinput::kFocused) && !(f.flags & (directinput::kInMenu | directinput::kOtherView | directinput::kUiUp));
    const bool fresh = ok && Fresh(now);
    const int lit = fresh && g_triggerOk.load() ? g_hand.load() : -1;   // (a crossbow's hand: its trigger shoots, A picks up)
    for (int h = 0; h < 2; ++h) {
        pointtrigger::In in;
        in.ok = ok;
        in.highlighted = lit == h;
        in.gripHeld = (f.hand[h].buttons & directinput::kSqueeze) != 0;
        in.triggerDown = (f.hand[h].buttons & directinput::kTrigger) != 0;
        in.atString = bowdrawwire::AtString(h);
        // (what the listeners took last frame holds our own take too: another's is one we did not make)
        in.takenByOther = !g_tookLast[h] && (directinput::TakenLastFrame(h) & (directinput::kTrigger | directinput::kTakeTrigger)) != 0;
        const pointtrigger::Out o = g_trigger[h].Step(in);
        g_tookLast[h] = o.take;
        if (o.take) directinput::Take(h, directinput::kTrigger | directinput::kTakeTrigger, kSrc);
        if (o.press) {
            const uint32_t id = g_id.load();
            g_pressId.store(id);
            g_pressAt.store(now);
            char b[200];
            snprintf(b, sizeof b, "vrcam: F4: the %s grip held + trigger on item %u - the game's pick-up pressed on it ([input] point_pickup)", h ? "right" : "left", id);
            Log(b);
        }
    }
    // Show items for the lit item when the game lays out no label for it by itself (labels off): the game's own command held, the other names cut
    const bool laidOut = now - g_laidOutAt.load() < kLaidOutMs;
    const bool was = g_holding.load();
    // (a new highlight waits kLaidOutMs for the game's own layout first: with names already shown nothing is held, nothing cut)
    const bool waited = now - g_litSince.load() >= kLaidOutMs;
    const bool hold = fresh && g_labelsIn.load() && (was || (waited && !laidOut));
    if (hold != was) {
        g_holding.store(hold);
        directinput::Command(kCmdShowItems, hold, kSrc);
        static ULONGLONG told = 0;
        if (now - told > 2000) {
            told = now;
            Log(hold ? "vrcam: F4: the pointed item's name - the game's Show items held for it, the other names cut ([input] point_pickup)"
                     : "vrcam: F4: the pointed item's name - Show items let go");
        }
    }
    g_onlyUntil.store(unleashed::lootaim::OnlyPointedUntil(hold, false, g_onlyUntil.load(), now, kHoldLeaseMs));
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("vrcam: point_pickup - direct_input's listener table is full: grip + trigger does not pick up (A still does)");
}

void Publish(uint32_t id, uintptr_t unit, int hand, bool inReach, const float rgb[3], bool labelHooksIn, bool triggerOk) {
    const uint32_t was = g_id.exchange(id);
    g_unit.store(unit);
    g_hand.store(hand);
    g_triggerOk.store(triggerOk);
    g_inReach.store(inReach);
    for (int i = 0; i < 3; ++i) g_rgb[i].store(rgb[i]);
    g_labelsIn.store(labelHooksIn);
    g_at.store(GetTickCount64());
    if (id != was) g_litSince.store(GetTickCount64());
    if (id && id != was && hand >= 0) {   // the highlight moved to another item: a soft tick in the pointing hand (lifted for a weak motor: PS VR2)
        const haptics::Request r = haptics::ForMotor(haptics::CurrentMotor(), 0.25f, 20);
        haptics::Pulse(hand, r.amplitude, (int)r.durationMs);
    }
}

uintptr_t Lit() {
    const ULONGLONG now = GetTickCount64();
    return Fresh(now) ? g_unit.load() : 0;
}
bool LitInReach() { return g_inReach.load(); }
void LitRarity(float rgb[3]) { for (int i = 0; i < 3; ++i) rgb[i] = g_rgb[i].load(); }
void NoteLaidOut() { if (!g_holding.load()) g_laidOutAt.store(GetTickCount64()); }   // (laid out because we hold Show items: not the game's own)
bool OnlyLit() { return unleashed::lootaim::OnlyPointed(g_onlyUntil.load(), GetTickCount64()); }

uint32_t PressId() {
    const uint32_t id = g_pressId.load();
    if (!id) return 0;
    if (GetTickCount64() - g_pressAt.load() > kPressMs) { PressEnd("not taken in 3 s (a full inventory? another player's? out of the hero's reach?)"); return 0; }
    return id;
}
void PressEnd(const char* why) {
    const uint32_t id = g_pressId.exchange(0);
    if (!id) return;
    char b[260];
    snprintf(b, sizeof b, "vrcam: F4: the trigger's pick-up of item %u ends - %s ([input] point_pickup)", id, why);
    Log(b);
}

}  // namespace pointpickup
