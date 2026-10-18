// direct_remap.h - the list of controller buttons in D2R VR Settings (Controls tab, "Buttons of the controllers"): which button does what,
// chosen from a list of things to do instead of typed into the ini's [direct] section by hand. Pure: no game, no Windows - the Settings
// program feeds it the ini's keys, the unit test (cleanroom/tests/direct_remap_test.cpp) feeds it synthetic ones.
//
// It writes the SAME keys the plugin reads (vr/direct_layout.h TableFromIni, one parser for both): "right_trigger = action:attack" for a button,
// "left_stick_hold = ..." for its long press, "left_grip+left_menu = ..." with the hand's grip held. A line that is what the plugin does anyway
// is not written (the key goes), so an ini the player never touched stays small and a new default reaches him; a button the player wants
// bare is written as "none". A line the list has no name for (several outputs, a key command not in the list) is shown as "Custom" and left
// alone - the list never overwrites what it does not understand.
#pragma once

#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "d2r_vr_shared.h"
#include "direct_layout.h"

namespace directinput {
namespace remap {

using layout::Binding;
using layout::Ctl;

// A key command of the game by its name in the ini ("Swap weapons", "D2R key: Swap weapons"), its number; -1 if there is none.
inline int CmdByName(const char* name) {
    static const char kPrefix[] = "D2R key: ";
    for (const D2RVRCommand& c : kD2RVRCommands) {
        const char* n = c.action;
        if (layout::EqNoCase(name, n)) return c.cmd;
        if (!std::strncmp(n, kPrefix, sizeof kPrefix - 1) && layout::EqNoCase(name, n + sizeof kPrefix - 1)) return c.cmd;
    }
    return -1;
}

// What a button can be given: the name the list shows and the ini's text for it. Order is the list's order; "Nothing" first.
struct Pick {
    const char* label;
    const char* ini;
};
inline const std::vector<Pick>& Picks() {
    static const std::vector<Pick> picks = {
        {"Nothing", "none"},
        {"Attack", "action:attack"},
        {"Pick up / interact", "action:interact"},
        {"Inventory", "action:inventory"},
        {"Map (show / hide)", "action:map"},
        {"Show items on the ground (Alt)", "cmd:0x25"},
        {"Show items (Alt); a quick click: the map", "cmd:0x25, action:map"},
        {"Pause menu (Esc)", "action:pause"},
        {"Close all panels", "cmd:0x26"},
        {"Skill panel (the wheel)", "action:skillpanel"},
        {"Javelin quiver: next skill of the throw", "action:quivernext"},
        {"Javelin quiver: previous skill of the throw", "action:quiverprev"},
        {"Swap weapons", "action:swap"},
        {"Recenter: put the game in front of me", "action:recenter"},
        {"Next view (F1 - F4)", "action:viewnext"},
        {"Gamepad B (skill button)", "pad:B"},
        {"Gamepad X (skill button)", "pad:X"},
        {"Gamepad Y (skill button)", "pad:Y"},
        {"Gamepad RB (skill button)", "pad:RB"},
        {"Gamepad LB", "pad:LB"},
        {"Gamepad A", "pad:A"},
        {"Gamepad left trigger (second row of skills)", "pad:LT"},
        {"Gamepad L3 (left stick click)", "pad:L3"},
        {"Gamepad R3 (right stick click)", "pad:R3"},
        {"Character screen", "cmd:0x00"},
        {"Skill tree", "cmd:0x0C"},
        {"Quest log", "cmd:0x04"},
        {"Next skill", "cmd:0x27"},
        {"Previous skill", "cmd:0x28"},
        {"Run / walk switch", "cmd:0x23"},
        {"Stand still", "cmd:0x24"},
    };
    return picks;
}

// Which line of the button: pressed alone, held for hold_ms (the long press), pressed with the same hand's grip held, or touched (the finger
// arriving on it: capacitive touch, a control that has one - layout::HasTouch).
enum class Line { Plain, Hold, Chord, Touch };
struct Row {
    const char* label;   // as the list shows it
    Ctl ctl;
    Line line;
};
inline const std::vector<Row>& Rows() {
    using namespace layout;
    static const std::vector<Row> rows = {
        // right hand
        {"Right trigger", kRTrigger, Line::Plain},
        {"Right A", kRA, Line::Plain},
        {"Right B", kRB, Line::Plain},
        {"Right stick click", kRStick, Line::Plain},
        {"Right menu button (Vive, WMR; a Touch has none)", kRMenu, Line::Plain},
        {"Right grip", kRGrip, Line::Plain},
        // left hand
        {"Left trigger", kLTrigger, Line::Plain},
        {"Left X", kLX, Line::Plain},
        {"Left Y", kLY, Line::Plain},
        {"Left stick click", kLStick, Line::Plain},
        {"Left menu button (PS VR2: the right Options)", kLMenu, Line::Plain},
        {"Left grip", kLGrip, Line::Plain},
        // a long press
        {"Left menu button (long press; PS VR2: the right Options)", kLMenu, Line::Hold},
        {"Left stick click", kLStick, Line::Hold},
        {"Right stick click", kRStick, Line::Hold},
        {"Right menu button", kRMenu, Line::Hold},
        {"Left X", kLX, Line::Hold},
        {"Left Y", kLY, Line::Hold},
        {"Right A", kRA, Line::Hold},
        {"Right B", kRB, Line::Hold},
        // with the grip held
        {"Left grip + menu button", kLMenu, Line::Chord},
        {"Left grip + stick click", kLStick, Line::Chord},
        {"Left grip + X", kLX, Line::Chord},
        {"Left grip + Y", kLY, Line::Chord},
        {"Right grip + menu button", kRMenu, Line::Chord},
        {"Right grip + stick click", kRStick, Line::Chord},
        {"Right grip + A", kRA, Line::Chord},
        {"Right grip + B", kRB, Line::Chord},
        // the finger on the control (not a press): the triggers, the face buttons, the thumbsticks
        {"Right trigger (touch)", kRTrigger, Line::Touch},
        {"Right A (touch)", kRA, Line::Touch},
        {"Right B (touch)", kRB, Line::Touch},
        {"Right stick (touch)", kRStick, Line::Touch},
        {"Left trigger (touch)", kLTrigger, Line::Touch},
        {"Left X (touch)", kLX, Line::Touch},
        {"Left Y (touch)", kLY, Line::Touch},
        {"Left stick (touch)", kLStick, Line::Touch},
    };
    return rows;
}

// The [direct] section as the Win32 API hands it over ("key=value" lines, comments already gone), kept in a map the parser can ask. An ini's keys
// are not case-sensitive and are trimmed, and so are the values' ends; a line without "=" or starting with ";" is nothing.
using Section = std::map<std::string, std::string>;
inline void AddLine(Section& section, const std::string& line) {
    auto trim = [](const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
        return s.substr(a, b - a);
    };
    const size_t eq = line.find('=');
    if (eq == std::string::npos || (!line.empty() && line[0] == ';')) return;
    std::string key = trim(line.substr(0, eq));
    if (key.empty()) return;
    for (char& c : key) c = (char)std::tolower((unsigned char)c);
    section[key] = trim(line.substr(eq + 1));
}
inline layout::IniGet Getter(const Section& section) {
    return [&section](const char* key, std::string* text) {
        std::string k = key;
        for (char& c : k) c = (char)std::tolower((unsigned char)c);
        const auto it = section.find(k);
        if (it == section.end()) return false;
        *text = it->second;
        return true;
    };
}

// The ini key of a row: "right_trigger", "left_stick_hold", "left_grip+left_menu".
inline std::string KeyOf(const Row& r) {
    std::string k = layout::Name(r.ctl);
    if (r.line == Line::Hold) k += "_hold";
    else if (r.line == Line::Touch) k += "_touch";
    else if (r.line == Line::Chord) k = std::string(layout::Name(layout::GripOf(layout::HandOf(r.ctl)))) + "+" + k;
    return k;
}

inline const Binding& LineOf(const layout::Table& t, const Row& r) {
    return r.line == Line::Hold ? t.hold[r.ctl] : r.line == Line::Chord ? t.chord[r.ctl] : r.line == Line::Touch ? t.touch[r.ctl] : t.plain[r.ctl];
}

// The pick the row shows now, from the ini (`get`: a key of [direct] -> its text, false when it is not there): the index in Picks(), or
// Picks().size() - "Custom" - for a line the list has no name for. A line the plugin would not understand is the default's, as in the plugin.
inline int Current(const layout::IniGet& get, const Row& r) {
    const Binding now = LineOf(layout::TableFromIni(get, &CmdByName), r);
    const std::vector<Pick>& picks = Picks();
    for (size_t i = 0; i < picks.size(); ++i) {
        Binding b;
        if (layout::ParseBinding(picks[i].ini, &CmdByName, &b) && layout::SameBinding(b, now)) return (int)i;
    }
    return (int)picks.size();
}

// What the ini must do to give the row this pick: write `text` under `key`, or take the key out (`erase`) when the pick is what the plugin does
// without it. False for "Custom" or an index that is not in the list - nothing to write.
struct Change {
    std::string key;
    bool erase = false;
    std::string text;
};
inline bool Choose(const layout::IniGet& get, const Row& r, int pick, Change* out) {
    const std::vector<Pick>& picks = Picks();
    if (pick < 0 || pick >= (int)picks.size()) return false;
    Binding wanted;
    if (!layout::ParseBinding(picks[pick].ini, &CmdByName, &wanted)) return false;
    const Binding& standard = LineOf(layout::DefaultsFromIni(get), r);
    Change c;
    c.key = KeyOf(r);
    if (layout::SameBinding(wanted, standard)) c.erase = true;   // the plugin does this anyway: no line
    else c.text = layout::FormatBinding(wanted);                 // "none" when the button is to be bare
    *out = c;
    return true;
}

}  // namespace remap
}  // namespace directinput
