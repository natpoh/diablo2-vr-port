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
#include "empty_grip.h"
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
// why nothing is lit (NoteScan / NoteOtherAnswer: the game's UI thread; read by the listener for its log)
std::atomic<int> g_scanItems{-1};
std::atomic<bool> g_rayOk[2] = {false, false};
std::atomic<float> g_armOut[2] = {0.0f, 0.0f};
std::atomic<float> g_armNeed{0.0f};
std::atomic<ULONGLONG> g_scanAt{0};
std::atomic<const char*> g_other{nullptr};
std::atomic<ULONGLONG> g_otherAt{0};
bool g_pressWas[2] = {false, false};
int g_whyLines = 0;
// [weight] grab_items_empty_grip
std::atomic<int> g_emptyWhy[2] = {(int)emptygrip::Why::Off, (int)emptygrip::Why::Off};
std::atomic<ULONGLONG> g_emptyAt[2] = {0, 0};
std::atomic<bool> g_itemNear[2] = {false, false};
bool g_gripWas[2] = {false, false};
int g_emptyLines = 0;

// Why nothing is lit for hand h (the log of a press that picked up nothing).
void WhyNothing(int h, ULONGLONG now, bool fresh, char* b, size_t n) {
    const ULONGLONG scanAt = g_scanAt.load(), otherAt = g_otherAt.load();
    const char* other = g_other.load();
    if (fresh && g_hand.load() != h)
        snprintf(b, n, "the other hand's ray has the lit item %u", g_id.load());
    else if (fresh && !g_triggerOk.load())
        snprintf(b, n, "this hand holds a crossbow: its trigger shoots, A picks up");
    else if (other && now - otherAt < kFreshMs)
        snprintf(b, n, "the game's interact target went to %s before the pointed item was looked for", other);
    else if (!scanAt || now - scanAt >= kFreshMs)
        snprintf(b, n, "the game has not asked its interact target lately (%s) - nothing is looked for", scanAt ? "over 150 ms ago" : "never this session");
    else if (g_scanItems.load() <= 0)
        snprintf(b, n, "no item on the floor round the hero (the scan found %d)", g_scanItems.load());
    else if (!g_rayOk[h].load())
        snprintf(b, n, "this hand has no ray (no drawn forearm for it); %d items round the hero", g_scanItems.load());
    else if (g_armOut[h].load() < g_armNeed.load())
        snprintf(b, n, "the arm is not held out: %.0f cm from the head, %.0f needed ([input] point_pickup_arm_cm); %d items round the hero",
                 g_armOut[h].load() * 100.0f, g_armNeed.load() * 100.0f, g_scanItems.load());
    else
        snprintf(b, n, "the ray is on none of the %d items round the hero", g_scanItems.load());
}

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
        {   // grip held + trigger pressed on nothing lit for this hand: why (20 lines a session)
            const bool both = ok && in.gripHeld && in.triggerDown;
            if (both && !g_pressWas[h] && !in.highlighted && g_whyLines < 20) {
                ++g_whyLines;
                char b[400];
                WhyNothing(h, now, fresh, b, sizeof b);
                char line[520];
                snprintf(line, sizeof line, "vrcam: F4: the %s grip held + trigger picked up nothing - %s ([input] point_pickup)%s", h ? "right" : "left", b,
                         g_whyLines == 20 ? " (no more of these lines this session)" : "");
                Log(line);
            }
            g_pressWas[h] = both;
        }
        {   // [weight] grab_items_empty_grip (vr/empty_grip.h): an empty hand's grip alone - its rising edge - picks up the item its ray has lit
            const bool rise = ok && in.gripHeld && !g_gripWas[h];
            g_gripWas[h] = ok && in.gripHeld;
            const ULONGLONG at = g_emptyAt[h].load();
            const emptygrip::Why why = at && now - at < kFreshMs ? (emptygrip::Why)g_emptyWhy[h].load() : emptygrip::Why::Off;
            // (the grip another listener took last frame - the weight model's hold, the swap, the bow string - is that one's)
            const bool gripTaken = (directinput::TakenLastFrame(h) & (directinput::kSqueeze | directinput::kTakeSqueeze)) != 0;
            if (rise && why != emptygrip::Why::Off && !in.triggerDown) {   // (with the trigger down it is grip + trigger's press, above)
                char line[560];
                line[0] = 0;
                if (why == emptygrip::Why::Takes && !gripTaken && in.highlighted && !g_pressId.load()) {
                    const uint32_t id = g_id.load();
                    g_pressId.store(id);
                    g_pressAt.store(now);
                    snprintf(line, sizeof line, "vrcam: F4: the %s hand's grip alone (an empty hand) on item %u - the game's pick-up pressed on it ([weight] grab_items_empty_grip)",
                             h ? "right" : "left", id);
                } else if (g_emptyLines < 20) {
                    ++g_emptyLines;
                    char b[400];
                    if (why != emptygrip::Why::Takes) snprintf(b, sizeof b, "%s", emptygrip::WhyText(why));
                    else if (gripTaken) snprintf(b, sizeof b, "another gesture took the grip");
                    else WhyNothing(h, now, fresh, b, sizeof b);
                    snprintf(line, sizeof line, "vrcam: F4: the %s hand's grip alone picked up nothing - %s ([weight] grab_items_empty_grip)%s", h ? "right" : "left", b,
                             g_emptyLines == 20 ? " (no more of these lines this session)" : "");
                }
                if (line[0]) Log(line);
            }
        }
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

void NoteScan(int items, const bool rayOk[2], const float armOutM[2], float armNeedM) {
    g_scanItems.store(items);
    for (int h = 0; h < 2; ++h) { g_rayOk[h].store(rayOk[h]); g_armOut[h].store(armOutM[h]); }
    g_armNeed.store(armNeedM);
    g_scanAt.store(GetTickCount64());
}
void SetEmptyGrip(int hand, int why) {
    if (hand < 0 || hand > 1) return;
    g_emptyWhy[hand].store(why);
    g_emptyAt[hand].store(GetTickCount64());
}
void SetItemNear(int hand, bool isNear) { if (hand >= 0 && hand < 2) g_itemNear[hand].store(isNear); }
bool ItemNear(int hand) { return hand >= 0 && hand < 2 && g_itemNear[hand].load(); }
void NoteOtherAnswer(const char* who) {
    g_other.store(who);
    g_otherAt.store(GetTickCount64());
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
