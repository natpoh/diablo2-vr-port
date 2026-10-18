#pragma once
// hset_keys.h - the headset panel's Left / Right pages: a row is one line of the ini's [direct] section, its value one of the picks of the list
// D2R VR Settings shows too (vr/direct_remap.h: Picks, Row, Current, Choose, and TableFromIni of vr/direct_layout.h, the parser the plugin reads
// the ini with). Nothing is listed or parsed twice here; this file only says which twelve lines the panel has room for and what a pick is
// called in a 248-pixel box.
//
// A line the list has no name for (several outputs, a key command the list lacks) is the pick one past the list - "Custom (ini)": shown, and left
// alone until a pick is made. A pick that is what the plugin does anyway takes the key out of the ini, as in D2R VR Settings.
// Pure: no Windows, no ini file, no game.

#include <cstring>
#include <string>

#include "direct_remap.h"

namespace hset {
namespace keys {

namespace rm = directinput::remap;
namespace lay = directinput::layout;

// The panel's binding rows, in the order of kDefs' [direct] keys (hset_defs.h; a test holds the two together): the buttons of each hand and
// the one long press a hand has the most use for - the left menu button's pause menu, the right stick's skill panel. The other long presses, the
// grips and the chords stay in D2R VR Settings and the ini.
inline constexpr int kSlotCount = 12;
struct Slot {
    lay::Ctl ctl;
    rm::Line line;
};
inline constexpr Slot kSlots[kSlotCount] = {
    {lay::kLTrigger, rm::Line::Plain}, {lay::kLX, rm::Line::Plain},      {lay::kLY, rm::Line::Plain},
    {lay::kLStick, rm::Line::Plain},   {lay::kLMenu, rm::Line::Plain},  {lay::kLMenu, rm::Line::Hold},
    {lay::kRTrigger, rm::Line::Plain}, {lay::kRA, rm::Line::Plain},     {lay::kRB, rm::Line::Plain},
    {lay::kRStick, rm::Line::Plain},   {lay::kRStick, rm::Line::Hold}, {lay::kRMenu, rm::Line::Plain},
};
// The list's row of a slot (its label is the list's long one: the panel names its rows itself).
inline rm::Row RowOf(int slot) { return rm::Row{"", kSlots[slot].ctl, kSlots[slot].line}; }
inline std::string KeyOf(int slot) { return rm::KeyOf(RowOf(slot)); }

// The picks: the list's, then Custom.
inline int PickCount() { return (int)rm::Picks().size(); }
inline int CustomPick() { return PickCount(); }

// What a pick is called in the value box (about 14 letters fit): by the pick's ini text, so a pick the list gains or moves keeps its name; the
// test fails for a pick without a short name, and until one is added the list's own label is shown.
struct Short {
    const char* ini;
    const char* name;
};
inline constexpr Short kShort[] = {
    {"none", "Nothing"},
    {"action:attack", "Attack"},
    {"action:interact", "Pick up"},
    {"action:inventory", "Inventory"},
    {"action:map", "Map"},
    {"cmd:0x25", "Show items"},
    {"cmd:0x25, action:map", "Items + map"},
    {"action:pause", "Pause menu"},
    {"cmd:0x26", "Close panels"},
    {"action:skillpanel", "Skill panel"},
    {"action:quivernext", "Quiver next"},
    {"action:quiverprev", "Quiver prev."},
    {"action:swap", "Swap weapons"},
    {"action:recenter", "Recenter"},
    {"action:viewnext", "Next view"},
    {"pad:B", "Skill btn B"},
    {"pad:X", "Skill btn X"},
    {"pad:Y", "Skill btn Y"},
    {"pad:RB", "Skill btn RB"},
    {"pad:LB", "Pad LB"},
    {"pad:A", "Pad A"},
    {"pad:LT", "Skill row 2"},
    {"pad:L3", "Pad L3"},
    {"pad:R3", "Pad R3"},
    {"cmd:0x00", "Character"},
    {"cmd:0x0C", "Skill tree"},
    {"cmd:0x04", "Quest log"},
    {"cmd:0x27", "Next skill"},
    {"cmd:0x28", "Prev. skill"},
    {"cmd:0x23", "Run / walk"},
    {"cmd:0x24", "Stand still"},
};
inline const char* ShortName(const char* ini) {
    for (const Short& s : kShort)
        if (!std::strcmp(s.ini, ini)) return s.name;
    return nullptr;
}
// The words of a pick for the value box: "Custom (ini)" for the one past the list.
inline std::string PickName(int pick) {
    const auto& picks = rm::Picks();
    if (pick < 0 || pick >= (int)picks.size()) return "Custom (ini)";
    const char* s = ShortName(picks[pick].ini);
    return s ? s : picks[pick].label;
}
// The ini text a pick stands for ("" for Custom).
inline const char* PickIni(int pick) {
    const auto& picks = rm::Picks();
    return pick >= 0 && pick < (int)picks.size() ? picks[pick].ini : "";
}

// The pick a slot's line is now (CustomPick() for a line the list has no name for), and the ini change that makes it a pick: both are the
// list's own (rm::Current / rm::Choose), `get` reads the [direct] keys.
inline int CurrentPick(const lay::IniGet& get, int slot) { return rm::Current(get, RowOf(slot)); }
inline bool Choose(const lay::IniGet& get, int slot, int pick, rm::Change* out) { return rm::Choose(get, RowOf(slot), pick, out); }

}  // namespace keys
}  // namespace hset
