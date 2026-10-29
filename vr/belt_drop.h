// belt_drop.h - [hands] belt_drop_ground: a full belt potion thrown or dropped on the ground in VR first person (F4) is dropped by the GAME, as the player would drop
// it: the belt slot's own click takes the item onto the cursor, the cursor's own drop puts it on the ground. Nothing else: the game's checks, its packets, its server.
//
// The game's client (D2R 3.3.93787, read off the image - side notes belt-drop-recon.md):
//   BeltWidget::PickupItem 0x2287B0(widget, int* slot)   what a plain click on a belt potion does: the slot's item onto an EMPTY cursor (C->S 0x24); the item is
//                                                        "busy" until the server answers. The widget is the HUD's "Belt" (0x855C90(0x846190(0), "Belt")).
//   0x15BDF0(item, 0)                                    the cursor's item to the ground (C->S 0x17) - what the mouse's click on the ground and the pad's drop call.
//                                                        With the controller layout the game's own InventoryItemManager::Update drops a held item by itself.
//   BeltWidget::PutCursorItem 0x2282D0(widget, int* slot) the cursor's item back into the belt slot (0x23 / 0x25) - our way back when the drop does not come.
// The server puts a dropped item on the nearest free cell round the HERO: the packet carries only the item, so the game's potion lies at his feet, not where ours fell.
//
// The pure part below decides each UI frame's step from what is seen (tested in cleanroom/tests/belt_drop_test.cpp); vr/belt_drop.cpp calls the game.
#pragma once

#include <cstdint>

namespace D2RL { struct PluginContext; }

namespace beltdrop {

// ---- the game's side (vr/belt_drop.cpp) ----
// From D2RLoaderLoadPlugin (the UI thread service).
void Init(const D2RL::PluginContext* ctx);
// Any thread: drop the bottom-row item of belt column c (0..3) whose unit id is id. False: one is under way, or the game's functions are not where this build has
// them (the log says which, once).
bool Ask(int column, uint32_t id);
// [hands] belt_throw_merc - any thread: give that item to the hero's mercenary, the game's own way: what Shift + the belt key does (the pad: the left trigger) -
// the belt key's own use of the slot (BeltWidget's ActivateBeltItem, the call the key path makes at 0x228D8C, through D2RLoader's hook of it) with "to the
// mercenary" set, behind the UI gate the game's own question asks (0x8D3B0). The game decides the rest (no merc, a refusal: the item stays). False as Ask.
bool Give(int column, uint32_t id);
// vrcam's timer: a request or a sequence under way goes to the UI thread.
void Tick();

// ---- the steps (pure) ----
enum class Phase { Idle, Asked, Picked, Giving, Done, Stopped };
enum class Act { None, Pickup, Drop, PutBack, Give };
struct See {
    bool gatesOpen = false;    // the game takes item commands now (no menu, no control lock, not the trade-like UI state)
    bool slotOurs = false;     // the belt's slot holds our item (the id we were asked for)
    bool slotBusy = false;     // ... with a request of the game's under way
    bool cursorEmpty = true;
    bool cursorOurs = false;   // the cursor holds our item
    bool cursorBusy = false;
    double sinceS = 0;         // since Ask
    double onCursorS = 0;      // how long our item has been on the cursor
    bool dropSent = false;
    bool mercGateOpen = false; // the UI gate the game's own "to the mercenary?" question asks first (0x8D3B0: false then)
};
struct Next { Phase phase; Act act; const char* why; };

constexpr double kGatesWaitS = 0.5;   // the game not taking item commands this long: given up (nothing sent)
constexpr double kGameDropS = 0.25;   // our item on the cursor this long and the game has not dropped it itself (the mouse layout, a panel open): we call the drop
constexpr double kTimeoutS = 2.0;     // still on the cursor then: the drop was refused - it goes back into the belt

inline Next Step(Phase p, const See& s) {
    switch (p) {
        case Phase::Asked:
            if (!s.slotOurs) return {Phase::Stopped, Act::None, "the item is no longer in its place in the belt: nothing sent"};
            if (!s.cursorEmpty) return {Phase::Stopped, Act::None, "something is on the cursor: nothing sent"};
            if (!s.gatesOpen || s.slotBusy) {
                if (s.sinceS > kGatesWaitS) return {Phase::Stopped, Act::None, "the game does not take item commands now: nothing sent"};
                return {Phase::Asked, Act::None, ""};
            }
            return {Phase::Picked, Act::Pickup, "the belt slot's own click: the item onto the cursor"};
        case Phase::Picked:
            if (!s.cursorEmpty && !s.cursorOurs) return {Phase::Stopped, Act::None, "another item is on the cursor: left alone"};
            if (s.cursorEmpty && !s.slotOurs) return {Phase::Done, Act::None, "dropped: the item left the belt and the cursor"};
            if (s.sinceS > kTimeoutS) {
                if (s.cursorOurs) return {Phase::Stopped, Act::PutBack, "the drop did not come: the item back into its belt slot"};
                return {Phase::Stopped, Act::None, "the game did not take the item from the belt: it stays there"};
            }
            if (s.cursorOurs && !s.cursorBusy && !s.dropSent && s.onCursorS >= kGameDropS) return {Phase::Picked, Act::Drop, "the cursor's own drop: the item to the ground"};
            return {Phase::Picked, Act::None, ""};
        case Phase::Giving:   // [hands] belt_throw_merc: one use of the slot "to the mercenary"; the game's answer is the item leaving the belt (the caller watches)
            if (!s.slotOurs) return {Phase::Stopped, Act::None, "the item is no longer in its place in the belt: not given"};
            if (!s.gatesOpen || !s.mercGateOpen || s.slotBusy) {
                if (s.sinceS > kGatesWaitS) return {Phase::Stopped, Act::None, "the game does not take item commands now: not given"};
                return {Phase::Giving, Act::None, ""};
            }
            return {Phase::Done, Act::Give, "the belt key's own use with Shift: to the mercenary"};
        default:
            return {p, Act::None, ""};
    }
}

}  // namespace beltdrop
