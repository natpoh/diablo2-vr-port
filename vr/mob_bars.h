// mob_bars.h - [hud] ordinary_mob_bars: the health and name plate of an ordinary monster out, the
// plates of champions, uniques, bosses, their minions and the NPCs kept. Presentation only: the
// plate is not drawn, nothing else about the monster (the target, the attack, the hover) changes.
//
// The idea - to answer the game's "where is this unit on the screen" for a plate's anchor so that the
// plate is not seen - and the record layout it reads are from bearstonem/d2r-pcvr (MIT, a fork of
// D2R VR 0.152: cleanroom/camera/monster_bars.h and docs/monster-bars.md), checked here against the
// game image (see below); the code is written again on our signatures (vr/mob_bars.cpp).
//
// What the game's plate does with a unit, in 3.3.93787 (D2RLoader 1.3.1 layout):
//   TooltipsPanel's target routine (0xC0420) asks the anchor helper (0xCE850) for the target's place
//   on the screen -> UnitToScreen (0x76A7D0, "above" = 1), and the plate's event (0xCEA50) carries
//   that anchor to the plate's widget update (0x1F84E0), which moves the plate (name and health bar
//   together) to it. Only that one call is answered (vr/mob_bars.cpp): the other callers of
//   UnitToScreen (the item labels, the controller's pick-up and target code) are the game's own
//   business and untouched. How the answer takes the plate away: vr/mob_bars.cpp.
//
// The record, read-only (every read can fail: a unit that goes away between frames, a stale pointer):
//   unit + 0x00         type (1 = monster)
//   unit + 0x04         class id
//   unit + 0x10         monster data
//   monster data + 0x00 the MonStats row; + 0x1A special flags (champion, unique, super unique, their
//                       minions...: any bit set keeps the plate)
//   row + 0x00          the row's id (u16; it must be the unit's class id, or the record is not read as expected)
//   row + 0x3C          flags: enabled (bit 25) and killable (bit 15) both needed;
//                       boss (6), prime evil (7), NPC (8), interact (9), in town (10) keep the plate
// Confirmed in the image: the game's own code (0x51F288..0x51F295) takes the unit's type (0x34B9D0: [unit]),
// compares it to 1, reads the monster data from unit + 0x10 and tests its byte + 0x1A against 0xE. The
// MonStats table holds 0x1FC bytes per row, indexed by class id (0x976E0).
//
// A plate is only ever hidden when the whole chain reads and says "ordinary". Anything else - a failed
// read, a record that is not as expected, a flag nobody listed - keeps the game's plate.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

// F4 ONLY (vr/view_gate.h): the other views draw every plate.
namespace mobbars {

constexpr uint32_t kUnitMonster = 1;
constexpr uintptr_t kUnitType = 0x00, kUnitClass = 0x04, kUnitData = 0x10;   // UnitAny
constexpr uintptr_t kDataRow = 0x00, kDataSpecial = 0x1A;                    // monster data
constexpr uintptr_t kRowId = 0x00, kRowFlags = 0x3C;                         // MonStats row
constexpr uint32_t kRowEnabled = 1u << 25, kRowKillable = 1u << 15;
// boss, prime evil, NPC, interact, in town
constexpr uint32_t kRowKeep = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10);
constexpr uintptr_t kMinPointer = 0x10000;   // below this a pointer is not one

// True when the unit is an ordinary monster whose plate is to go. read(address, value) fills `value`
// (a uint8_t, uint16_t, uint32_t or uintptr_t) from the game's memory and says whether it could.
template <class Read>
bool HidesPlate(uintptr_t unit, Read read) {
    if (unit < kMinPointer) return false;
    uint32_t type = 0, classId = 0, flags = 0;
    uintptr_t data = 0, row = 0;
    uint8_t special = 0;
    uint16_t rowId = 0;
    if (!read(unit + kUnitType, type) || type != kUnitMonster) return false;
    if (!read(unit + kUnitClass, classId) || classId > 0xFFFF) return false;
    if (!read(unit + kUnitData, data) || data < kMinPointer) return false;
    if (!read(data + kDataSpecial, special) || special != 0) return false;   // champion, unique, minion...
    if (!read(data + kDataRow, row) || row < kMinPointer) return false;
    if (!read(row + kRowId, rowId) || rowId != classId) return false;        // not the row of this monster
    if (!read(row + kRowFlags, flags)) return false;
    // A monster the game lists, alive to kill; a boss or an NPC keeps its plate even with no flag above.
    return (flags & (kRowEnabled | kRowKillable)) == (kRowEnabled | kRowKillable) && (flags & kRowKeep) == 0;
}

// A call of UnitToScreen that is the plate's: the setting on, the anchor at the unit's head ("above"), an
// output to fill, and the caller the plate's anchor helper (plateRet, 0xCE87C) - no other caller of the
// function is ever answered differently. plateRet 0 (not found) is no call.
inline bool IsPlateCall(bool on, uint8_t above, const float* out, uintptr_t ret, uintptr_t plateRet) {
    return on && above != 0 && out != nullptr && plateRet != 0 && ret == plateRet;
}

// The answer that takes a plate off the screen. style 0: "on the screen", at a point this far up and
// to the left (the widget update moves the plate there, nobody sees it); style 1 ([debug] mob_bars_style,
// d2r-pcvr's answer): "not on the screen" - the plate's update then skips its anchor (see vr/mob_bars.cpp).
constexpr float kFarAway = -30000.0f;   // a plate is a few hundred units wide
inline bool CullAnswer(int style, float out[2]) {
    out[0] = out[1] = kFarAway;
    return style != 1;
}

// [hud] ordinary_mob_bars, and [debug] mob_bars_style (vr/mob_bars.cpp: how the plate is taken off the screen).
void SetEnabled(bool on, int style = 0);
// Twice a second from the main tick: once the setting is on, the hook goes in as soon as the game's
// code reads (it waits for its page to be decrypted). Nothing is hooked while the setting is off.
void Install(const D2RL::PluginContext* ctx);
bool Hooked();

}  // namespace mobbars
