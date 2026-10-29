// Offline checks of D2R VR Settings' button list (vr/direct_remap.h): what each row shows from the ini, what it writes back, and that the plugin's
// own parser (vr/direct_layout.h TableFromIni) reads back exactly what the list wrote. No game, no Windows, no ini file: a map stands in for [direct].
// Run: build/Release/direct_remap_test.exe  (exit code 0 = all passed)

#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "../../vr/direct_remap.h"

using namespace directinput;
using namespace directinput::layout;
using namespace directinput::remap;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// A [direct] section as a map (keys as the ini has them, lower case); a key not in it is not in the file. remap::Getter asks it.
using Ini = Section;
// What Settings does with a Change: write the text, or take the key out.
static void Apply(Ini& ini, const Change& c) {
    if (c.erase) ini.erase(c.key);
    else ini[c.key] = c.text;
}
static int PickOf(const char* ini) {
    const auto& p = Picks();
    for (size_t i = 0; i < p.size(); ++i) if (std::string(p[i].ini) == ini) return (int)i;
    return -1;
}
static int RowOf(const char* key) {
    const auto& r = Rows();
    for (size_t i = 0; i < r.size(); ++i) if (KeyOf(r[i]) == key) return (int)i;
    return -1;
}

static void TestLists() {
    // every pick's text is something the plugin understands, and writes back as itself
    for (const Pick& p : Picks()) {
        Binding b;
        CHECK(ParseBinding(p.ini, &CmdByName, &b));
        Binding again;
        CHECK(ParseBinding(FormatBinding(b).c_str(), &CmdByName, &again) && SameBinding(b, again));
    }
    // no two picks are the same thing (a row would show the first of them for both)
    std::set<std::string> seen;
    for (const Pick& p : Picks()) {
        Binding b;
        ParseBinding(p.ini, &CmdByName, &b);
        CHECK(seen.insert(FormatBinding(b)).second);
    }
    // every row has its own ini key, and the key is one the plugin reads
    std::set<std::string> keys;
    for (const Row& r : Rows()) CHECK(keys.insert(KeyOf(r)).second);
    CHECK(KeyOf(Rows()[RowOf("right_trigger")]) == "right_trigger");
    CHECK(RowOf("left_stick_hold") >= 0 && RowOf("left_menu_hold") >= 0 && RowOf("left_grip+left_menu") >= 0 && RowOf("right_grip+right_b") >= 0);
    // the asked-for ones are in the list
    CHECK(PickOf("cmd:0x25") >= 0);           // Show items on the ground (Alt)
    CHECK(PickOf("action:pause") >= 0);       // the pause menu (Esc)
    CHECK(PickOf("none") == 0);               // Nothing is the first
}

// An ini nobody touched: every row shows what the plugin does, and a rebuild of the table from the rows gives the plugin's own default.
static void TestDefaults() {
    const Ini empty;
    const IniGet get = Getter(empty);
    const Table plugin = TableFromIni(get, &CmdByName);
    for (const Row& r : Rows()) {
        const int cur = Current(get, r);
        CHECK(cur >= 0 && cur < (int)Picks().size());   // nothing of the default is "Custom"
        Binding b;
        CHECK(ParseBinding(Picks()[cur].ini, &CmdByName, &b) && SameBinding(b, LineOf(plugin, r)));
    }
    CHECK(Current(get, Rows()[RowOf("right_trigger")]) == PickOf("action:attack"));
    CHECK(Current(get, Rows()[RowOf("right_a")]) == PickOf("action:interact"));
    CHECK(Current(get, Rows()[RowOf("left_stick")]) == PickOf("cmd:0x25, action:map"));
    CHECK(Current(get, Rows()[RowOf("left_menu")]) == PickOf("action:inventory"));
    CHECK(Current(get, Rows()[RowOf("left_menu_hold")]) == PickOf("action:pause"));   // the pause button is there out of the box
    CHECK(Current(get, Rows()[RowOf("right_stick_hold")]) == PickOf("action:skillpanel"));
    CHECK(Current(get, Rows()[RowOf("left_grip")]) == 0 && Current(get, Rows()[RowOf("right_grip")]) == 0);   // the grips: nothing
    CHECK(Current(get, Rows()[RowOf("left_grip+left_menu")]) == PickOf("action:map"));
    CHECK(Current(get, Rows()[RowOf("left_grip+left_stick")]) == PickOf("action:recenter"));
    CHECK(Current(get, Rows()[RowOf("right_grip+right_b")]) == PickOf("pad:RB"));
    CHECK(Current(get, Rows()[RowOf("left_x_hold")]) == 0);

    // the toggles change what "default" is, and so what the row shows
    Ini noPause{{"pause_button", "0"}};
    CHECK(Current(Getter(noPause), Rows()[RowOf("left_menu_hold")]) == 0);
    Ini noMap{{"left_stick_map", "0"}};
    CHECK(Current(Getter(noMap), Rows()[RowOf("left_stick")]) == PickOf("cmd:0x25"));
}

// Choosing writes the key the plugin reads, nothing when the choice is what the plugin does anyway.
static void TestChoose() {
    Ini ini;
    Change c;
    const Row& trig = Rows()[RowOf("right_trigger")];
    CHECK(Choose(Getter(ini), trig, PickOf("action:attack"), &c) && c.erase && c.key == "right_trigger");   // the default: no line
    CHECK(Choose(Getter(ini), trig, PickOf("pad:A"), &c) && !c.erase && c.key == "right_trigger" && c.text == "pad:A");
    CHECK(Choose(Getter(ini), trig, 0, &c) && !c.erase && c.text == "none");                                 // bare, written down
    CHECK(!Choose(Getter(ini), trig, -1, &c));
    CHECK(!Choose(Getter(ini), trig, (int)Picks().size(), &c));                                              // "Custom" is not a choice
    // a button the plugin leaves bare, left bare: no line
    CHECK(Choose(Getter(ini), Rows()[RowOf("left_grip")], 0, &c) && c.erase);
    // the pause button: the asked-for remap - Esc on the right A, the left menu button back to the inventory only
    CHECK(Choose(Getter(ini), Rows()[RowOf("right_a_hold")], PickOf("action:pause"), &c) && c.key == "right_a_hold" && c.text == "action:pause");
    CHECK(Choose(Getter(ini), Rows()[RowOf("left_menu_hold")], 0, &c) && c.key == "left_menu_hold" && c.text == "none");
    // Alt on the left grip: the list writes it, the plugin reads it back
    CHECK(Choose(Getter(ini), Rows()[RowOf("left_grip")], PickOf("cmd:0x25"), &c));
    Apply(ini, c);
    CHECK(ini["left_grip"] == "cmd:0x25");
    const Table t = TableFromIni(Getter(ini), &CmdByName);
    CHECK(t.plain[kLGrip].out[0].kind == kOutCommand && t.plain[kLGrip].out[0].value == 0x25);
    // the default depends on the toggles: with pause_button off the pause pick on the long press is a line, "Nothing" is not
    Ini noPause{{"pause_button", "0"}};
    CHECK(Choose(Getter(noPause), Rows()[RowOf("left_menu_hold")], 0, &c) && c.erase);
    CHECK(Choose(Getter(noPause), Rows()[RowOf("left_menu_hold")], PickOf("action:pause"), &c) && !c.erase && c.text == "action:pause");
}

// Every row, every pick: written through Choose into a fresh ini, the plugin's parser gives that pick back on that row - and touches nothing else.
static void TestRoundTrip() {
    const Table base = TableFromIni(Getter(Ini{}), &CmdByName);
    for (size_t ri = 0; ri < Rows().size(); ++ri) {
        const Row& r = Rows()[ri];
        for (size_t pi = 0; pi < Picks().size(); ++pi) {
            Ini ini;
            Change c;
            CHECK(Choose(Getter(ini), r, (int)pi, &c));
            Apply(ini, c);
            CHECK(Current(Getter(ini), r) == (int)pi);
            // all the other rows are as they were
            const Table t = TableFromIni(Getter(ini), &CmdByName);
            for (size_t rj = 0; rj < Rows().size(); ++rj) {
                if (rj == ri) continue;
                CHECK(SameBinding(LineOf(t, Rows()[rj]), LineOf(base, Rows()[rj])));
            }
            // choosing it again changes nothing (idempotent)
            Change again;
            CHECK(Choose(Getter(ini), r, (int)pi, &again));
            Ini twice = ini;
            Apply(twice, again);
            CHECK(twice == ini);
            // choosing the default pick takes the key out again
            const int def = Current(Getter(Ini{}), r);
            Change back;
            CHECK(Choose(Getter(ini), r, def, &back) && back.erase);
            Apply(ini, back);
            CHECK(ini.empty());
        }
    }
}

// A line the list has no name for is shown as Custom and is not in the list's way: the other rows still write, and that line stays.
static void TestCustom() {
    Ini ini{{"right_b", "pad:B, cmd:0x17"}, {"left_stick", "garbage:1"}, {"left_x", "none"}};
    const IniGet get = Getter(ini);
    CHECK(Current(get, Rows()[RowOf("right_b")]) == (int)Picks().size());                 // two outputs the list has no name for
    CHECK(Current(get, Rows()[RowOf("left_stick")]) == PickOf("cmd:0x25, action:map"));   // not understood: the plugin keeps its default, so the row shows it
    CHECK(Current(get, Rows()[RowOf("left_x")]) == 0);                                     // unbound on purpose
    Change c;
    CHECK(Choose(get, Rows()[RowOf("right_a")], PickOf("action:map"), &c));
    Apply(ini, c);
    CHECK(ini["right_b"] == "pad:B, cmd:0x17");                                           // untouched
    // a command by its name is read like the plugin reads it
    Ini named{{"left_grip", "cmd:Show items (Alt)"}};
    CHECK(Current(Getter(named), Rows()[RowOf("left_grip")]) == PickOf("cmd:0x25"));
    CHECK(CmdByName("Swap weapons") == 0x2C && CmdByName("D2R key: Swap weapons") == 0x2C && CmdByName("nothing like it") == -1);
}

// The section as the Win32 API hands it over (GetPrivateProfileSection) into the map the parser asks: what Settings reads the file through.
static void TestSection() {
    Section s;
    for (const char* line : {"hold_enabled=1", "  Right_Trigger  =  pad:A  ", ";left_x = pad:B", "no equals sign", "=novalue", "Left_Menu_Hold=none\r", "left_stick = cmd:Show items (Alt), action:map"})
        AddLine(s, line);
    std::string t;
    CHECK(Getter(s)("hold_enabled", &t) && t == "1");
    CHECK(Getter(s)("right_trigger", &t) && t == "pad:A");               // trimmed, and the key's case does not matter
    CHECK(Getter(s)("RIGHT_TRIGGER", &t) && t == "pad:A");
    CHECK(!Getter(s)("left_x", &t));                                     // a comment is nothing
    CHECK(Getter(s)("left_menu_hold", &t) && t == "none");               // (and a CR at the end of a line is not part of it)
    CHECK(s.size() == 4);
    // what the plugin makes of that section
    const Table table = TableFromIni(Getter(s), &CmdByName);
    CHECK(table.plain[kRTrigger].out[0].kind == kOutPad && table.plain[kRTrigger].out[0].value == pad::kA);
    CHECK(table.hold[kLMenu].out[0].kind == kOutNone);                   // "none" took the default long press away
    CHECK(table.plain[kLStick].out[0].kind == kOutCommand && table.plain[kLStick].out[0].value == 0x25 && table.plain[kLStick].out[1].kind == kOutAction);
    // and the list reads the same
    CHECK(Current(Getter(s), Rows()[RowOf("right_trigger")]) == PickOf("pad:A"));
    CHECK(Current(Getter(s), Rows()[RowOf("left_menu_hold")]) == 0);
}

// The pause output of the layout: a long press of the left menu button, a tap stays the inventory; off, as before.
static void TestPauseButton() {
    Layout L;
    L.SetWorld(DefaultTable());
    Controls on;
    on.v[kLMenu] = 1.0f;
    Controls off;
    // a tap: the inventory when it is let go, no pause
    Outputs o = L.Step(Mode::World, on, 1000);
    CHECK(!o.pause && o.actions == 0);
    o = L.Step(Mode::World, off, 1100);
    CHECK(!o.pause && o.actions == act::kInventory);
    o = L.Step(Mode::World, off, 1200);
    CHECK(o.actions == 0);
    // held: the pause request after hold_ms, once, and no inventory when let go
    o = L.Step(Mode::World, on, 2000);
    CHECK(!o.pause);
    o = L.Step(Mode::World, on, 2000 + L.World().holdMs + 10);
    CHECK(o.pause && o.actions == 0);
    o = L.Step(Mode::World, on, 2000 + L.World().holdMs + 20);
    CHECK(o.pause);   // the press lasts the hold-tap pulse (100 ms): the plugin takes its rising edge, once
    o = L.Step(Mode::World, on, 2000 + L.World().holdMs + 200);
    CHECK(!o.pause);  // and is over while the button is still held: not again
    o = L.Step(Mode::World, off, 3000);
    CHECK(!o.pause && o.actions == 0);
    // pause_button off: the menu button is the inventory the moment it is pressed, as before
    Layout K;
    K.SetWorld(DefaultTable(true, false));
    o = K.Step(Mode::World, on, 1000);
    CHECK(o.actions == act::kInventory && !o.pause);
    // F1-F3 have no Esc of ours: the table made for them has no pause, and the left menu button acts at once
    const Table flat = WithoutPanels(DefaultTable());
    Layout F;
    F.SetWorld(flat);
    o = F.Step(Mode::World, on, 1000);
    CHECK(o.actions == act::kInventory && !o.pause);
    // in a menu (a panel open) the button is the game's Start, as it was
    Layout M;
    o = M.Step(Mode::Menu, on, 1000);
    CHECK(o.pad.buttons == pad::kStart && !o.pause);
    // the ini's text for it
    Out out;
    CHECK(ParseOut("action:pause", &CmdByName, &out) && out.kind == kOutPause);
    CHECK(ParseOut("action:Esc", &CmdByName, &out) && out.kind == kOutPause);
    CHECK(FormatOut(OutPause()) == "action:pause");
}

// Run / walk: the game's own switch (command 0x23) is one pick of the list, on a plain button, a long press or a chord alike; what the plugin makes of
// it is that command, once for a press of the button. (It is the way to put it on a button: the skill panel's own RUN / WALK button is off by default.)
static void TestRunWalkPick() {
    int n = 0;
    for (const Pick& p : Picks()) n += std::strcmp(p.ini, "cmd:0x23") == 0;
    CHECK(n == 1 && PickOf("cmd:0x23") > 0);
    CHECK(std::strcmp(Picks()[PickOf("cmd:0x23")].label, "Run / walk switch") == 0);
    CHECK(CmdByName("Run / walk switch") == 0x23 && CmdByName("D2R key: Run / walk switch") == 0x23 && CmdByName("run / walk SWITCH") == 0x23);
    for (const char* key : {"right_b", "left_x_hold", "right_grip+right_a", "left_trigger"}) {
        Ini ini;
        Change c;
        const Row& row = Rows()[RowOf(key)];
        CHECK(Choose(Getter(ini), row, PickOf("cmd:0x23"), &c) && !c.erase && c.key == key && c.text == "cmd:0x23");
        Apply(ini, c);
        CHECK(Current(Getter(ini), row) == PickOf("cmd:0x23"));
        const Table t = TableFromIni(Getter(ini), &CmdByName);
        const Binding& b = LineOf(t, row);
        CHECK(b.out[0].kind == kOutCommand && b.out[0].value == 0x23 && b.out[1].kind == kOutNone);
    }
    Ini byName{{"right_b", "cmd:Run / walk switch"}, {"right_a", "cmd:D2R key: Run / walk switch"}};   // written by hand with the game's name
    CHECK(Current(Getter(byName), Rows()[RowOf("right_b")]) == PickOf("cmd:0x23"));
    CHECK(Current(Getter(byName), Rows()[RowOf("right_a")]) == PickOf("cmd:0x23"));
    // on the controller's frames: the button's press is the command, and the button let go takes it away again
    Ini ini{{"right_b", "cmd:0x23"}};
    Layout L;
    L.SetWorld(TableFromIni(Getter(ini), &CmdByName));
    Controls on, off;
    on.v[kRB] = 1.0f;
    Outputs o = L.Step(Mode::World, off, 1000);
    CHECK(!(o.commands[0] & (1ull << 0x23)));
    bool pressed = false;
    for (uint64_t t = 1100; t < 1500; t += 20) pressed = pressed || (L.Step(Mode::World, on, t).commands[0] & (1ull << 0x23)) != 0;
    CHECK(pressed);
    o = L.Step(Mode::World, off, 1600);
    o = L.Step(Mode::World, off, 1700);
    CHECK(!(o.commands[0] & (1ull << 0x23)));
}

// The touch rows: the finger arriving on a control (capacitive touch), the same list of picks, the keys "<control>_touch"; none is bound by default, and
// "Run / walk switch" on one is the command 0x23 given once per touch begin (the layout's own test is in direct_input_test).
static void TestTouchRows() {
    int touchRows = 0;
    for (const Row& r : Rows()) {
        if (r.line != Line::Touch) continue;
        ++touchRows;
        CHECK(HasTouch(r.ctl));                                                     // only controls that have a touch
        const std::string key = KeyOf(r);
        CHECK(key.size() > 6 && key.compare(key.size() - 6, 6, "_touch") == 0 && std::string(Name(r.ctl)) + "_touch" == key);
        CHECK(RowOf(key.c_str()) >= 0 && Rows()[RowOf(key.c_str())].line == Line::Touch);
        const Ini empty;
        CHECK(Current(Getter(empty), r) == 0);                                      // nothing by default: the list shows "Nothing"
    }
    CHECK(touchRows == 8);
    int touchable = 0;
    for (int i = 0; i < kCtlCount; ++i) touchable += HasTouch((Ctl)i);
    CHECK(touchable == touchRows);                                                  // (every control that has a touch has its row)
    for (const char* key : {"right_trigger_touch", "right_a_touch", "right_b_touch", "right_stick_touch", "left_trigger_touch", "left_x_touch", "left_y_touch",
                            "left_stick_touch"})
        CHECK(RowOf(key) >= 0);
    CHECK(RowOf("left_grip_touch") < 0 && RowOf("left_menu_touch") < 0);            // a grip and a menu button have none
    // choosing writes the key, reads back, and the plugin's parser has the command on the touch line and not on the press
    for (const char* key : {"right_b_touch", "left_stick_touch", "right_trigger_touch", "left_y_touch"}) {
        Ini ini;
        Change c;
        const Row& row = Rows()[RowOf(key)];
        CHECK(Choose(Getter(ini), row, PickOf("cmd:0x23"), &c) && !c.erase && c.key == key && c.text == "cmd:0x23");
        Apply(ini, c);
        CHECK(Current(Getter(ini), row) == PickOf("cmd:0x23"));
        const Table t = TableFromIni(Getter(ini), &CmdByName);
        CHECK(LineOf(t, row).out[0].kind == kOutCommand && LineOf(t, row).out[0].value == 0x23);
        // the press of the same control is untouched: it still has its default
        const Table plain = DefaultsFromIni(Getter(ini));
        for (const Row& other : Rows())
            if (other.ctl == row.ctl && other.line == Line::Plain) CHECK(SameBinding(LineOf(t, other), LineOf(plain, other)));
        // choosing Nothing takes the line out again: "none" is what the default is, so the key goes
        CHECK(Choose(Getter(ini), row, 0, &c) && c.erase && c.key == key);
        Apply(ini, c);
        CHECK(ini.find(key) == ini.end() && Current(Getter(ini), row) == 0);
    }
    // press and touch are two choices: Run / walk on the press of B and something else on its touch, side by side
    Ini both;
    Change c;
    CHECK(Choose(Getter(both), Rows()[RowOf("right_b")], PickOf("cmd:0x23"), &c));
    Apply(both, c);
    CHECK(Choose(Getter(both), Rows()[RowOf("right_b_touch")], PickOf("cmd:0x27"), &c));
    Apply(both, c);
    CHECK(Current(Getter(both), Rows()[RowOf("right_b")]) == PickOf("cmd:0x23") && Current(Getter(both), Rows()[RowOf("right_b_touch")]) == PickOf("cmd:0x27"));
    // a touch line written by hand that the list has no name for is "Custom" and left alone
    Ini custom{{"right_a_touch", "pad:B, cmd:0x17"}};
    CHECK(Current(Getter(custom), Rows()[RowOf("right_a_touch")]) == (int)Picks().size());
}

int main() {
    TestLists();
    TestDefaults();
    TestChoose();
    TestRoundTrip();
    TestCustom();
    TestSection();
    TestPauseButton();
    TestRunWalkPick();
    TestTouchRows();
    std::printf("direct_remap_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
