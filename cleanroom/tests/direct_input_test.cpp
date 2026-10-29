// Offline checks of direct input (vr/direct_input.h): the layout, the release gates, the ini table, the head and hand
// maths, and the runtime fed with synthetic headset frames - no game, no OpenXR, no headset.
// Run: build/Release/direct_input_test.exe  (exit code 0 = all passed)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

#include "../../vr/direct_feed.h"
#include "../../vr/direct_input.h"
#include "../../vr/direct_layout.h"
#include "../../vr/view_gate.h"
#include "../../vr/flat_raw.h"

using namespace directinput;
using namespace directinput::layout;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// -- the pure layer ------------------------------------------------------------------------------------------------------

static Controls Held(std::initializer_list<std::pair<Ctl, float>> l) {
    Controls c;
    for (auto& [k, v] : l) c.v[k] = v;
    return c;
}

// The layout with hold-or-tap switched off ([direct] hold_enabled=0): a stick click acts at once, as before.
static Layout ClassicLayout() {
    Layout L;
    Table t = L.World();
    t.holdEnabled = false;
    L.SetWorld(t);
    return L;
}

static void TestWorldLayout() {
    Layout L = ClassicLayout();
    Outputs o = L.Step(Mode::World, Held({{kRTrigger, 1.0f}}));
    CHECK(o.actions == act::kAttack);
    CHECK(o.pad.buttons == 0 && o.pad.rt == 0);   // the right trigger is the attack, not the pad's RT
    o = L.Step(Mode::World, Held({{kRA, 1.0f}}));
    CHECK(o.actions == act::kInteract);
    o = L.Step(Mode::World, Held({{kRB, 1.0f}, {kLX, 1.0f}, {kLY, 1.0f}}));
    CHECK(o.pad.buttons == (pad::kB | pad::kX | pad::kY));
    o = L.Step(Mode::World, Held({{kLTrigger, 0.5f}}));   // the analog travel goes to the pad's LT even below "pressed"
    CHECK(o.pad.lt == 128);
    o = L.Step(Mode::World, Held({{kRStick, 1.0f}}));
    CHECK(o.actions == act::kSwap);
    o = L.Step(Mode::World, Held({{kLStick, 1.0f}}));
    CHECK(o.commands[0] == (1ull << 0x25) && o.commands[1] == 0);
    o = L.Step(Mode::World, Held({{kLMenu, 1.0f}}));
    CHECK(o.actions == act::kInventory);
    // the grips are free in the world: nothing on the pad
    o = L.Step(Mode::World, Held({{kLGrip, 1.0f}, {kRGrip, 1.0f}}));
    CHECK(o.pad.buttons == 0 && o.actions == 0);
}

// The left trigger is the second row while its line is the pad's LT; bound to its own action (or none) the second row is off (the author, 2026-10-24).
static void TestSecondRow() {
    CHECK(SecondRowOn(DefaultTable()));
    std::map<std::string, std::string> ini;
    const IniGet get = [&](const char* k, std::string* s) { auto it = ini.find(k); if (it == ini.end()) return false; *s = it->second; return true; };
    CHECK(SecondRowOn(TableFromIni(get, nullptr)));                      // unset: the default, the second row
    ini["left_trigger"] = "cmd:0x17";                                 // a potion
    Table t = TableFromIni(get, nullptr);
    CHECK(!SecondRowOn(t));
    Layout L = ClassicLayout();
    t.holdEnabled = false;
    L.SetWorld(t);
    const Outputs o = L.Step(Mode::World, Held({{kLTrigger, 1.0f}, {kRB, 1.0f}}));
    CHECK(o.pad.lt == 0 && o.pad.buttons == pad::kB && (o.commands[0] & (1ull << 0x17)));   // the potion and the FIRST row's B: no second row
    ini["left_trigger"] = "none";
    CHECK(!SecondRowOn(TableFromIni(get, nullptr)));                     // nothing on it: no second row either
    ini["left_trigger"] = "pad:LT, cmd:0x24";
    CHECK(SecondRowOn(TableFromIni(get, nullptr)));                      // still the pad's LT (with Stand still): the second row
    ini["left_trigger"] = "banana";
    CHECK(SecondRowOn(TableFromIni(get, nullptr)));                      // not understood: the default stays
}

static void TestSticks() {
    Layout L;
    Controls c;
    c.stick[0][0] = 0.0f; c.stick[0][1] = 1.0f;
    c.stick[1][0] = -1.0f; c.stick[1][1] = 0.0f;
    Outputs o = L.Step(Mode::World, c);
    CHECK(o.pad.lx == 0 && o.pad.ly == 32767 && o.pad.rx == -32767 && o.pad.ry == 0);
    c.stick[0][0] = 1.0f; c.stick[0][1] = 1.0f;   // a corner is brought into the unit disc
    o = L.Step(Mode::World, c);
    CHECK(Near(o.pad.lx / 32767.0f, 0.7071f) && Near(o.pad.ly / 32767.0f, 0.7071f));
    c.stick[0][0] = NAN;
    o = L.Step(Mode::World, c);
    CHECK(o.pad.lx == 0 && o.pad.ly == 0);
}

static void TestHysteresis() {
    Layout L;
    auto attack = [&](float v) { return L.Step(Mode::World, Held({{kRTrigger, v}})).actions == act::kAttack; };
    CHECK(!attack(0.50f));    // not yet
    CHECK(attack(0.60f));     // pressed
    CHECK(attack(0.40f));     // held through the dip
    CHECK(!attack(0.30f));    // let go
    CHECK(!attack(0.50f));    // and not pressed again below the on threshold
}

static void TestChordsAndLatch() {
    Layout L = ClassicLayout();   // (the left menu button has a long press now - the pause menu; its own test is below)
    // alone: the menu button is the inventory; with the left grip held: the map
    CHECK(L.Step(Mode::World, Held({{kLMenu, 1.0f}})).actions == act::kInventory);
    L.Step(Mode::World, Held({}));
    CHECK(L.Step(Mode::World, Held({{kLGrip, 1.0f}, {kLMenu, 1.0f}})).actions == act::kMap);
    L.Step(Mode::World, Held({}));
    // the binding is the press's: a grip added to a held button does not turn it into the chord
    CHECK(L.Step(Mode::World, Held({{kRA, 1.0f}})).actions == act::kInteract);
    CHECK(L.Step(Mode::World, Held({{kRA, 1.0f}, {kRGrip, 1.0f}})).actions == act::kInteract);
    L.Step(Mode::World, Held({}));
    // the right hand's chord: grip + B is RB, not B
    Outputs o = L.Step(Mode::World, Held({{kRGrip, 1.0f}, {kRB, 1.0f}}));
    CHECK(o.pad.buttons == pad::kRightBumper);
    L.Step(Mode::World, Held({}));
    // the other hand's grip is no modifier of this hand's button
    o = L.Step(Mode::World, Held({{kLGrip, 1.0f}, {kRB, 1.0f}}));
    CHECK(o.pad.buttons == pad::kB);
}

static void TestMenuLayout() {
    Layout L = ClassicLayout();
    Outputs o = L.Step(Mode::Menu, Held({{kRA, 1.0f}, {kRB, 1.0f}, {kLX, 1.0f}, {kLY, 1.0f}, {kLStick, 1.0f}, {kRStick, 1.0f},
                                         {kLGrip, 1.0f}, {kRGrip, 1.0f}, {kRMenu, 1.0f}}));
    CHECK(o.pad.buttons == (pad::kA | pad::kB | pad::kX | pad::kY | pad::kLeftStick | pad::kRightStick | pad::kLeftBumper |
                            pad::kRightBumper | pad::kStart));
    CHECK(o.actions == 0 && o.commands[0] == 0);
    o = L.Step(Mode::Menu, Held({{kRTrigger, 0.8f}, {kLTrigger, 0.3f}}));
    CHECK(o.pad.rt == 204 && o.pad.lt == 77);
    // switching tables lets no latched world binding through
    L.Step(Mode::World, Held({{kRA, 1.0f}}));
    o = L.Step(Mode::Menu, Held({{kRA, 1.0f}}));
    CHECK(o.actions == 0);
}

static void TestGate() {
    ReleaseGate g;
    Controls held = Held({{kRTrigger, 1.0f}, {kRA, 1.0f}});
    held.stick[0][1] = 1.0f;
    // the first context: everything held at the start does nothing
    Controls c = g.Step(1, held);
    CHECK(c.v[kRTrigger] == 0 && c.v[kRA] == 0 && c.stick[0][1] == 0);
    // let go, then pressed again: through
    c = g.Step(1, Controls{});
    c = g.Step(1, held);
    CHECK(c.v[kRTrigger] == 1.0f && c.v[kRA] == 1.0f && c.stick[0][1] == 1.0f);
    // a new context (a menu closed): the same hold is masked again until it is let go
    c = g.Step(2, held);
    CHECK(c.v[kRTrigger] == 0 && c.v[kRA] == 0 && c.stick[0][1] == 0);
    Controls partial = held;
    partial.v[kRA] = 0.0f;   // A let go, the trigger still down
    c = g.Step(2, partial);
    CHECK(c.v[kRTrigger] == 0);
    c = g.Step(2, Held({{kRA, 1.0f}}));   // a freshly pressed A passes while the trigger's mask is gone with its release
    CHECK(c.v[kRA] == 1.0f);
    // a stick comes back through only when it has been near the middle
    Controls s;
    s.stick[1][0] = 0.5f;
    c = g.Step(3, s);
    CHECK(c.stick[1][0] == 0);
    s.stick[1][0] = 0.05f;
    c = g.Step(3, s);
    s.stick[1][0] = 0.6f;
    c = g.Step(3, s);
    CHECK(c.stick[1][0] == 0.6f);
}

static void TestGateTaken() {
    ReleaseGate g;
    g.Step(1, Controls{});
    bool taken[kCtlCount] = {};
    taken[kRGrip] = true;
    Controls c = g.Step(1, Held({{kRGrip, 1.0f}}), taken);   // a gesture owns the grip
    CHECK(c.v[kRGrip] == 0);
    taken[kRGrip] = false;
    c = g.Step(1, Held({{kRGrip, 1.0f}}), taken);            // the gesture let go; the hand has not
    CHECK(c.v[kRGrip] == 0);
    c = g.Step(1, Controls{}, taken);
    c = g.Step(1, Held({{kRGrip, 1.0f}}), taken);            // a new squeeze is the layout's again
    CHECK(c.v[kRGrip] == 1.0f);
}

// A world trigger held while a panel opens and closes: the panel's click must not become an attack.
static void TestMenuRoundTrip() {
    ReleaseGate g;
    Layout L;
    auto step = [&](uint32_t ctx, Mode m, Controls c) { return L.Step(m, g.Step(ctx, c)); };
    step(1, Mode::World, Controls{});
    Controls trig = Held({{kRTrigger, 1.0f}});
    CHECK(step(1, Mode::World, trig).actions == act::kAttack);
    CHECK(step(2, Mode::Menu, trig).pad.rt == 0);        // the panel opened: the held trigger is not RT
    CHECK(step(2, Mode::Menu, Controls{}).pad.rt == 0);
    CHECK(step(2, Mode::Menu, trig).pad.rt == 255);      // a fresh press in the panel
    CHECK(step(3, Mode::World, trig).actions == 0);      // the click closed it: no attack from the same press
    CHECK(step(3, Mode::World, Controls{}).actions == 0);
    CHECK(step(3, Mode::World, trig).actions == act::kAttack);
}

// -- hold-or-tap (vr/hold_tap.h) ------------------------------------------------------------------------------------------

static void TestHoldTapMachine() {
    using Out = directinput::holdtap::Out;   // (not the layout's Out)
    using Machine = directinput::holdtap::Machine;
    // a tap: nothing until the release, then a press of 100 ms from the release
    Machine m;
    Out o = m.Step(true, 0, 500);
    CHECK(o.immediate && !o.tap && !o.hold);
    o = m.Step(true, 250, 500);
    CHECK(o.immediate && !o.tap);
    o = m.Step(false, 300, 500);
    CHECK(!o.immediate && o.tap && !o.hold);
    o = m.Step(false, 399, 500);
    CHECK(o.tap);
    o = m.Step(false, 400, 500);
    CHECK(!o.tap && !m.Busy(400));
    // a hold: once, at the threshold; no tap after it; the release does nothing more
    m = Machine();
    CHECK(m.Step(true, 1000, 500).immediate);
    o = m.Step(true, 1499, 500);
    CHECK(o.immediate && !o.hold && !o.fired);
    o = m.Step(true, 1500, 500);
    CHECK(!o.immediate && o.hold && o.fired);
    o = m.Step(true, 1550, 500);
    CHECK(o.hold && !o.fired);
    o = m.Step(true, 1600, 500);
    CHECK(!o.hold && !o.fired);   // the hold's press was 100 ms
    o = m.Step(false, 2000, 500);
    CHECK(!o.tap && !o.hold && !o.immediate && !m.Busy(2000));
    // a second press right after a tap is its own press
    m = Machine();
    m.Step(true, 0, 500);
    m.Step(false, 50, 500);                 // tap 1: pressed 50..150
    o = m.Step(true, 60, 500);              // pressed again while tap 1's press is still on
    CHECK(o.immediate && o.tap);
    o = m.Step(false, 120, 500);            // tap 2: pressed up to 220
    CHECK(o.tap);
    CHECK(m.Step(false, 219, 500).tap && !m.Step(false, 220, 500).tap);
    // a cancel under a press: no tap, no hold, and a control still held does not start again until it is let go
    m = Machine();
    m.Step(true, 0, 500);
    m.Cancel();
    o = m.Step(false, 100, 500);
    CHECK(!o.tap && !o.hold && !o.immediate);
    m.Step(true, 200, 500);
    o = m.Step(true, 800, 500);
    CHECK(o.hold);   // (a new press: it is the caller's gate that keeps a held control out)
}

// One headset frame of the layout at time t (ms) with these controls pressed.
static Outputs At(Layout& L, Mode mode, uint64_t t, std::initializer_list<std::pair<Ctl, float>> l, bool uiUp = false) {
    return L.Step(mode, Held(l), t, uiUp);
}

// -- touch lines: the finger arriving on a control -----------------------------------------------------------------------------

static Outputs AtTouch(Layout& L, Mode mode, uint64_t ms, std::initializer_list<Ctl> touching, std::initializer_list<std::pair<Ctl, float>> pressed = {},
                       bool uiUp = false, uint32_t noShot = 0) {
    Controls c;
    for (Ctl t : touching) c.touch[t] = true;
    for (const auto& p : pressed) c.v[p.first] = p.second;
    return L.Step(mode, c, ms, uiUp, noShot);
}
static bool RunWalkOn(const Outputs& o) { return (o.commands[0] >> 0x23) & 1u; }
static int TouchCmdName(const char* n) { return !_stricmp(n, "Run / walk switch") ? 0x23 : -1; }

static void TestTouchLines() {
    auto table = [](const char* key, const char* value) {
        std::map<std::string, std::string> m{{key, value}};
        const IniGet get = [m](const char* k, std::string* text) { const auto it = m.find(k); if (it == m.end()) return false; *text = it->second; return true; };
        return TableFromIni(get, &TouchCmdName);
    };
    // the ini: "<control>_touch" is a line of the controls that have a touch, in the same words as every other line; a grip or a menu button has none
    {
        int rebound = 0, bad = 0;
        std::map<std::string, std::string> m{{"right_b_touch", "cmd:0x23"}, {"left_stick_touch", "pad:X"}, {"left_grip_touch", "pad:A"},
                                             {"left_menu_touch", "pad:A"}, {"right_a_touch", "garbage:1"}};
        const IniGet get = [&](const char* k, std::string* text) { const auto it = m.find(k); if (it == m.end()) return false; *text = it->second; return true; };
        const Table t = TableFromIni(get, &TouchCmdName, &rebound, &bad);
        CHECK(t.touch[kRB].out[0].kind == kOutCommand && t.touch[kRB].out[0].value == 0x23);
        CHECK(t.touch[kLStick].out[0].kind == kOutPad && t.touch[kLStick].out[0].value == pad::kX);
        CHECK(t.touch[kLGrip].out[0].kind == kOutNone && t.touch[kLMenu].out[0].kind == kOutNone);   // (no touch there: the key is not read)
        CHECK(t.touch[kRA].out[0].kind == kOutNone && rebound == 2 && bad == 1);                      // (a line that is not understood: nothing bound)
        for (int i = 0; i < kCtlCount; ++i) CHECK(HasTouch((Ctl)i) == (i != kLGrip && i != kRGrip && i != kLMenu && i != kRMenu));
        const Table none = TableFromIni([](const char*, std::string*) { return false; }, &TouchCmdName);
        for (int i = 0; i < kCtlCount; ++i) CHECK(FormatBinding(none.touch[i]) == "none");        // none by default
    }
    // the layout: a command on the touch line is held from the touch BEGIN for as long as the finger stays (one press of the
    // game's key - 0.173, "Show items (Alt)" on a touch flashed as a pulse), let go with the finger; the next touch is the next press
    {
        Layout L;
        L.SetWorld(table("right_b_touch", "cmd:0x23"));
        Outputs o = AtTouch(L, Mode::World, 1000, {});
        CHECK(!RunWalkOn(o));
        o = AtTouch(L, Mode::World, 1010, {kRB});
        CHECK(RunWalkOn(o) && o.pad.buttons == 0);                      // the touch is not a press: the pad's B is not pressed with it
        o = AtTouch(L, Mode::World, 1060, {kRB});
        CHECK(RunWalkOn(o));                                              // (the pulse is a tap's length: the game's poll sees it)
        o = AtTouch(L, Mode::World, 1200, {kRB});
        CHECK(RunWalkOn(o));                                              // the finger stays: still held
        int dropped = 0;
        for (uint64_t t = 1210; t < 4000; t += 15) dropped += !RunWalkOn(AtTouch(L, Mode::World, t, {kRB}));
        CHECK(dropped == 0);                                              // held without a gap: one press for the game, no repeat
        o = AtTouch(L, Mode::World, 4000, {});
        CHECK(!RunWalkOn(o));
        o = AtTouch(L, Mode::World, 4100, {kRB});
        CHECK(RunWalkOn(o));                                              // the next touch
        // a finger that flickers (off and on within the pulse) is still one press for the game: the bit never drops between
        o = AtTouch(L, Mode::World, 4120, {});
        CHECK(RunWalkOn(o));
        o = AtTouch(L, Mode::World, 4140, {kRB});
        CHECK(RunWalkOn(o));
        o = AtTouch(L, Mode::World, 4260, {kRB});
        CHECK(RunWalkOn(o));                                              // (the finger came back at once: the same press goes on)
        // the press of the same control is its own line: pressing B (the touch comes first) gives the pad's B, and nothing of the touch again
        o = AtTouch(L, Mode::World, 6000, {});
        o = AtTouch(L, Mode::World, 6010, {kRB});
        o = AtTouch(L, Mode::World, 6300, {kRB}, {{kRB, 1.0f}});
        CHECK(o.pad.buttons == pad::kB && RunWalkOn(o));                  // the touch's command stays held under the finger
    }
    // a finger already on the control when the layout starts is not a touch begin; the first frame only learns where the fingers are
    {
        Layout L;
        L.SetWorld(table("right_a_touch", "cmd:0x23"));
        Outputs o = AtTouch(L, Mode::World, 1000, {kRA});
        CHECK(!RunWalkOn(o));
        o = AtTouch(L, Mode::World, 1010, {kRA});
        CHECK(!RunWalkOn(o));
        o = AtTouch(L, Mode::World, 1020, {});
        o = AtTouch(L, Mode::World, 1030, {kRA});
        CHECK(RunWalkOn(o));
        L.Reset();                                                         // (a view change: learned again)
        o = AtTouch(L, Mode::World, 3000, {kRA});
        CHECK(!RunWalkOn(o));
    }
    // each control by itself; a touch of another control gives nothing; the triggers, the face buttons and the sticks of both hands
    {
        std::map<std::string, std::string> m;
        for (int i = 0; i < kCtlCount; ++i) if (HasTouch((Ctl)i)) m[std::string(Name((Ctl)i)) + "_touch"] = "cmd:0x23";
        const IniGet get = [&](const char* k, std::string* text) { const auto it = m.find(k); if (it == m.end()) return false; *text = it->second; return true; };
        const Table t = TableFromIni(get, &TouchCmdName);
        for (int i = 0; i < kCtlCount; ++i) {
            if (!HasTouch((Ctl)i)) continue;
            Layout L;
            L.SetWorld(t);
            AtTouch(L, Mode::World, 1000, {});
            const Outputs o = AtTouch(L, Mode::World, 1010, {(Ctl)i});
            CHECK(RunWalkOn(o));
        }
        Layout L;
        L.SetWorld(t);
        AtTouch(L, Mode::World, 1000, {});
        Controls c;
        c.touch[kLGrip] = c.touch[kRMenu] = true;                         // a control that has no touch line: nothing, even if a frame said so
        CHECK(!RunWalkOn(L.Step(Mode::World, c, 1010)));
    }
    // F4's world layer only: a menu of the game, a panel of ours, a bow's draw-only mode that drops shots
    {
        Layout L;
        L.SetWorld(table("right_b_touch", "cmd:0x23"));
        AtTouch(L, Mode::Menu, 1000, {});
        CHECK(!RunWalkOn(AtTouch(L, Mode::Menu, 1010, {kRB})));           // (a menu: the pad is the game's own)
        AtTouch(L, Mode::World, 1100, {});
        CHECK(!RunWalkOn(AtTouch(L, Mode::World, 1110, {kRB}, {}, true))); // a panel of ours up
        AtTouch(L, Mode::World, 1500, {});
        CHECK(RunWalkOn(AtTouch(L, Mode::World, 1510, {kRB}, {}, false, 1u << kRB)));   // (a command is not a shot)
        Layout S;
        S.SetWorld(table("right_trigger_touch", "action:attack"));
        AtTouch(S, Mode::World, 1000, {});
        CHECK(AtTouch(S, Mode::World, 1010, {kRTrigger}).actions == act::kAttack);
        AtTouch(S, Mode::World, 1400, {});
        CHECK(AtTouch(S, Mode::World, 1410, {kRTrigger}, {}, false, 1u << kRTrigger).actions == 0);   // draw-only: the shot is dropped, as a press's is
        // the menu opening while the pulse runs ends it
        Layout M;
        M.SetWorld(table("right_b_touch", "cmd:0x23"));
        AtTouch(M, Mode::World, 1000, {});
        CHECK(RunWalkOn(AtTouch(M, Mode::World, 1010, {kRB})));
        CHECK(!RunWalkOn(AtTouch(M, Mode::Menu, 1020, {kRB})));
    }
    // the release gate: a finger on a control when the context changes is not a begin - it has to lift first; a gesture's take hides the touch too
    {
        ReleaseGate g;
        Controls c;
        c.touch[kRB] = true;
        c = g.Step(1, c);
        CHECK(!c.touch[kRB]);                                             // (the first frame: the context starts, the finger is already there)
        c = Controls{};
        c = g.Step(1, c);
        Controls on;
        on.touch[kRB] = true;
        CHECK(g.Step(1, on).touch[kRB]);                                  // lifted: the next touch counts
        CHECK(!g.Step(2, on).touch[kRB]);                                 // the context changed under a resting finger
        CHECK(!g.Step(2, on).touch[kRB]);
        CHECK(!g.Step(2, Controls{}).touch[kRB]);
        CHECK(g.Step(2, on).touch[kRB]);
        bool taken[kCtlCount] = {};
        taken[kRB] = true;
        CHECK(!g.Step(2, on, taken).touch[kRB]);                           // taken: hidden, and masked after the gesture lets go, until the finger lifts
        CHECK(!g.Step(2, on).touch[kRB]);
        g.Step(2, Controls{});
        CHECK(g.Step(2, on).touch[kRB]);
    }
}

static void TestHoldTapLayout() {
    // the right stick click: a tap is the swap, after the release; a long press asks for the skill panel, once, and swaps nothing
    {
        Layout L;
        Outputs o = At(L, Mode::World, 0, {});
        o = At(L, Mode::World, 10, {{kRStick, 1.0f}});
        CHECK(o.actions == 0 && !o.skillPanel);
        o = At(L, Mode::World, 150, {{kRStick, 1.0f}});
        CHECK(o.actions == 0);
        o = At(L, Mode::World, 160, {});                       // tap
        CHECK(o.actions == act::kSwap && !o.skillPanel);
        o = At(L, Mode::World, 250, {});
        CHECK(o.actions == act::kSwap);
        o = At(L, Mode::World, 260, {});
        CHECK(o.actions == 0);
        o = At(L, Mode::World, 1000, {{kRStick, 1.0f}});       // hold
        uint32_t panelSteps = 0, swapSteps = 0;
        bool edgeSeen = false, was = false;
        for (uint64_t t = 1010; t <= 2100; t += 10) {
            o = At(L, Mode::World, t, {{kRStick, 1.0f}});
            if (o.skillPanel) ++panelSteps;
            if (o.skillPanel && !was) edgeSeen = true;
            was = o.skillPanel;
            if (o.actions) ++swapSteps;
        }
        CHECK(edgeSeen && panelSteps >= 8 && panelSteps <= 11);   // the 100 ms press of the request
        CHECK(swapSteps == 0);
        o = At(L, Mode::World, 2200, {});                      // the release after a hold: nothing more
        CHECK(o.actions == 0 && !o.skillPanel);
        o = At(L, Mode::World, 2400, {});
        CHECK(o.actions == 0);
    }
    // the left stick click: Alt at once and while held; a tap leaves it at the release; a hold drops it and opens the inventory
    {
        Layout L;
        At(L, Mode::World, 0, {});
        Outputs o = At(L, Mode::World, 10, {{kLStick, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x25) && o.actions == 0);
        o = At(L, Mode::World, 200, {{kLStick, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x25));
        o = At(L, Mode::World, 210, {});
        CHECK(o.commands[0] == 0 && o.actions == act::kMap);   // a tap: Alt let go, the map pressed (its edge is the toggle)
        o = At(L, Mode::World, 330, {});
        CHECK(o.commands[0] == 0 && o.actions == 0);           // the press is 100 ms
        At(L, Mode::World, 1000, {{kLStick, 1.0f}});
        o = At(L, Mode::World, 1400, {{kLStick, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x25) && o.actions == 0);
        o = At(L, Mode::World, 1500, {{kLStick, 1.0f}});
        CHECK(o.commands[0] == 0 && o.actions == act::kInventory);   // the hold: Alt let go, the inventory pressed
        o = At(L, Mode::World, 1590, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kInventory);
        o = At(L, Mode::World, 1600, {{kLStick, 1.0f}});
        CHECK(o.actions == 0 && o.commands[0] == 0);           // the press is 100 ms; Alt does not come back
        o = At(L, Mode::World, 2000, {});
        CHECK(o.actions == 0 && o.commands[0] == 0);
    }
    // [direct] hold_stick_max_pct (70 %): the left stick pushed and held in a fight - no inventory; a quick click is the map at any lean, at its
    // release (the author: "if I run and need to look at the map, I must not stop"); 70 % still opens the inventory, past it not; a stick the
    // gate or a gesture hides (it reads centred) still leans by Controls::lean; 0 = no limit
    {
        auto Lean = [](Layout& L, uint64_t t, float y, bool click, float held = 0.0f) {
            Controls c;
            c.v[kLStick] = click ? 1.0f : 0.0f;
            c.stick[0][1] = y;
            c.lean[0] = held;
            return L.Step(Mode::World, c, t);
        };
        Layout L;
        Lean(L, 0, 0.0f, false);
        Outputs o = Lean(L, 10, 0.9f, true);                   // pushed forward and clicked: Alt as ever, no refusal yet
        CHECK(o.actions == 0 && o.commands[0] == (1ull << 0x25) && L.TakeLeanRefused(0) == Layout::kLeanNone);
        bool inventory = false;
        for (uint64_t t = 20; t <= 900; t += 10) {              // held long past the hold time, still leaning: never the inventory
            o = Lean(L, t, 0.9f, true);
            inventory = inventory || (o.actions & act::kInventory);
        }
        CHECK(!inventory && L.TakeLeanRefused(0) == Layout::kLeanPressed && L.TakeLeanRefused(0) == Layout::kLeanNone);
        o = Lean(L, 910, 0.9f, false);                          // let go after the hold time: no tap
        CHECK(o.actions == 0);
        Lean(L, 1000, 0.9f, false);
        Lean(L, 1010, 0.9f, true);                              // running, a quick click: the map at the release, the stick still out
        o = Lean(L, 120 + 1000, 0.9f, false);
        CHECK(o.actions == act::kMap && L.TakeLeanRefused(0) == Layout::kLeanNone);
        Lean(L, 1300, 0.0f, false);
        Lean(L, 2000, 0.69f, true);                             // 69 %: still the long press
        o = Lean(L, 2600, 0.69f, true);
        CHECK(o.actions == act::kInventory);
        Lean(L, 2700, 0.0f, false);
        Lean(L, 3000, 0.0f, true);                              // clicked centred, then out to 72 % before the hold: no inventory
        o = Lean(L, 3200, 0.72f, true);
        CHECK(o.actions == 0);
        o = Lean(L, 3700, 0.72f, true);
        CHECK(o.actions == 0 && L.TakeLeanRefused(0) == Layout::kLeanPressed);
        Lean(L, 3800, 0.0f, false);
        Lean(L, 4000, 0.0f, true, 0.9f);                        // the stick hidden (reads 0 0), the hand pushing: no inventory ...
        o = Lean(L, 4600, 0.0f, true, 0.9f);
        CHECK(o.actions == 0);
        Lean(L, 4700, 0.0f, false);
        Lean(L, 5000, 0.0f, true, 0.9f);                        // ... and its quick click is still the map
        o = Lean(L, 5100, 0.0f, false, 0.9f);
        CHECK(o.actions == act::kMap);
        Lean(L, 5300, 0.0f, false);
        Table t = L.World();                                    // 0 = no limit: pushed and held is the inventory, as before
        t.holdStickMax = 0.0f;
        L.SetWorld(t);
        Lean(L, 6000, 0.9f, true);
        o = Lean(L, 6600, 0.9f, true);
        CHECK(o.actions == act::kInventory);
    }
    // both sticks at once
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kLStick, 1.0f}, {kRStick, 1.0f}});
        Outputs o = At(L, Mode::World, 520, {{kLStick, 1.0f}, {kRStick, 1.0f}});
        CHECK(o.skillPanel && o.actions == act::kInventory && o.commands[0] == 0);
        o = At(L, Mode::World, 900, {});
        CHECK(!o.skillPanel && o.actions == 0);
    }
    // the right one let go at once, the left one held on
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kLStick, 1.0f}, {kRStick, 1.0f}});
        Outputs o = At(L, Mode::World, 60, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kSwap && o.commands[0] == (1ull << 0x25));
        o = At(L, Mode::World, 520, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kInventory && !o.skillPanel);
    }
    // a second press right after a tap
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kRStick, 1.0f}});
        At(L, Mode::World, 60, {});
        Outputs o = At(L, Mode::World, 80, {{kRStick, 1.0f}});
        CHECK(o.actions == act::kSwap);                        // the first tap's press is still on
        o = At(L, Mode::World, 600, {{kRStick, 1.0f}});
        CHECK(o.skillPanel);                                   // and the second press is a hold of its own
    }
    // a chord with the grip: the recentre at once, no Alt, no hold
    {
        Layout L;
        At(L, Mode::World, 0, {});
        Outputs o = At(L, Mode::World, 10, {{kLGrip, 1.0f}, {kLStick, 1.0f}});
        CHECK(o.actions == act::kRecenter && o.commands[0] == 0);
        o = At(L, Mode::World, 900, {{kLGrip, 1.0f}, {kLStick, 1.0f}});
        CHECK(o.actions == act::kRecenter);                    // held on as a chord is: no inventory after the threshold
        o = At(L, Mode::World, 1000, {});
        CHECK(o.actions == 0);
        // the grip pressed after the click does not turn it into the chord: it is the hold-or-tap press
        At(L, Mode::World, 1100, {{kLStick, 1.0f}});
        o = At(L, Mode::World, 1200, {{kLGrip, 1.0f}, {kLStick, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x25) && o.actions == 0);
    }
    // a hold through a context change (the focus goes, a menu opens): forgotten, no tap, no hold, even when held on
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kRStick, 1.0f}});
        L.Cancel();
        Outputs o = At(L, Mode::World, 700, {});               // (the gate has the control as let go until it is)
        CHECK(o.actions == 0 && !o.skillPanel);
        o = At(L, Mode::World, 900, {});
        CHECK(o.actions == 0 && !o.skillPanel);
        // a tap decided just before the context changes is dropped with it (a menu opening takes the click's meaning with it)
        At(L, Mode::World, 1000, {{kRStick, 1.0f}});
        At(L, Mode::World, 1050, {});
        L.Cancel();
        o = At(L, Mode::World, 1060, {});
        CHECK(o.actions == 0);
    }
    // a panel of the plugin's comes up under a press: nothing, until the control has been let go
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kRStick, 1.0f}});
        Outputs o = At(L, Mode::World, 300, {{kRStick, 1.0f}}, true);
        CHECK(o.actions == 0 && !o.skillPanel);
        o = At(L, Mode::World, 900, {{kRStick, 1.0f}}, false);   // the panel is gone, the click is still down
        CHECK(o.actions == 0 && !o.skillPanel);
        o = At(L, Mode::World, 910, {});
        CHECK(o.actions == 0);
        At(L, Mode::World, 920, {{kRStick, 1.0f}});
        o = At(L, Mode::World, 960, {});                         // a new tap is a tap again
        CHECK(o.actions == act::kSwap);
    }
    // a menu: the left stick click is the game's L3 on a tap (after the release), the inventory on a hold; the right one is R3 at once
    {
        Layout L;
        At(L, Mode::Menu, 0, {});
        Outputs o = At(L, Mode::Menu, 10, {{kRStick, 1.0f}});
        CHECK(o.pad.buttons == pad::kRightStick);
        At(L, Mode::Menu, 20, {});
        At(L, Mode::Menu, 100, {{kLStick, 1.0f}});
        o = At(L, Mode::Menu, 150, {{kLStick, 1.0f}});
        CHECK(o.pad.buttons == 0 && o.actions == 0);
        o = At(L, Mode::Menu, 160, {});
        CHECK(o.pad.buttons == pad::kLeftStick);
        At(L, Mode::Menu, 1000, {});
        At(L, Mode::Menu, 1010, {{kLStick, 1.0f}});
        o = At(L, Mode::Menu, 1520, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kInventory && o.pad.buttons == 0);
        o = At(L, Mode::Menu, 1700, {});
        CHECK(o.actions == 0 && o.pad.buttons == 0);
        // and back in the world: a press there is hold-or-tap again from its own start
        At(L, Mode::World, 2000, {{kRStick, 1.0f}});
        o = At(L, Mode::World, 2600, {{kRStick, 1.0f}});
        CHECK(o.skillPanel);
    }
    // hold_enabled=0: the old behaviour, a click at once and no hold
    {
        Layout L = ClassicLayout();
        At(L, Mode::World, 0, {});
        Outputs o = At(L, Mode::World, 10, {{kRStick, 1.0f}});
        CHECK(o.actions == act::kSwap);
        o = At(L, Mode::World, 900, {{kRStick, 1.0f}});
        CHECK(o.actions == act::kSwap && !o.skillPanel);
        o = At(L, Mode::World, 910, {{kLStick, 1.0f}, {kRStick, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x25));
        o = At(L, Mode::World, 2000, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kMap && o.commands[0] == (1ull << 0x25));   // at once: Alt and the map's press (one toggle, on its edge)
    }
    // [direct] left_stick_map=0: the left click is Alt only, a tap too
    {
        Layout L;
        L.SetWorld(DefaultTable(false));
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kLStick, 1.0f}});
        Outputs o = At(L, Mode::World, 200, {});
        CHECK(o.commands[0] == 0 && o.actions == 0);
        o = At(L, Mode::World, 1000, {{kLStick, 1.0f}});
        o = At(L, Mode::World, 1600, {{kLStick, 1.0f}});
        CHECK(o.actions == act::kInventory);                    // the hold is still the inventory
    }
    // the quick click of the left stick while a panel of ours is up: nothing, no map
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kLStick, 1.0f}}, true);
        Outputs o = At(L, Mode::World, 60, {}, true);
        CHECK(o.actions == 0 && o.commands[0] == 0);
    }
    // a tap of the left stick with the right one held: the map, the right one still in its own press
    {
        Layout L;
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kLStick, 1.0f}, {kRStick, 1.0f}});
        Outputs o = At(L, Mode::World, 60, {{kRStick, 1.0f}});
        CHECK(o.actions == act::kMap);
        o = At(L, Mode::World, 120, {});
        CHECK(o.actions == (act::kMap | act::kSwap));
    }
    // the hold line unbound ("none"): the click acts at once again
    {
        Layout L;
        Table t = L.World();
        t.hold[kRStick] = Binding{};
        L.SetWorld(t);
        At(L, Mode::World, 0, {});
        Outputs o = At(L, Mode::World, 10, {{kRStick, 1.0f}});
        CHECK(o.actions == act::kSwap);
    }
    // another hold line, from the table: the right menu button, a command
    {
        Layout L;
        Table t = L.World();
        t.hold[kRMenu] = Bind(OutCmd(0x17));
        t.holdMs = 300;
        L.SetWorld(t);
        At(L, Mode::World, 0, {});
        At(L, Mode::World, 10, {{kRMenu, 1.0f}});
        Outputs o = At(L, Mode::World, 330, {{kRMenu, 1.0f}});
        CHECK(o.commands[0] == (1ull << 0x17) && o.actions == 0);
    }
}

static int CmdName(const char* n) { return !_stricmp(n, "Swap weapons") ? 0x2C : -1; }

static void TestParse() {
    Binding b;
    CHECK(ParseBinding("pad:B", CmdName, &b) && b.out[0].kind == kOutPad && b.out[0].value == pad::kB && b.out[1].kind == kOutNone);
    CHECK(ParseBinding(" action:swap , cmd:0x2C ,pad:lt", CmdName, &b) && b.out[0].kind == kOutAction && b.out[0].value == act::kSwap &&
          b.out[1].kind == kOutCommand && b.out[1].value == 0x2C && b.out[2].kind == kOutLT);
    CHECK(ParseBinding("cmd:Swap weapons", CmdName, &b) && b.out[0].kind == kOutCommand && b.out[0].value == 0x2C);
    CHECK(ParseBinding("cmd:37", CmdName, &b) && b.out[0].value == 37);
    CHECK(ParseBinding("none", CmdName, &b) && b.out[0].kind == kOutNone);
    CHECK(ParseBinding("action:skillpanel", CmdName, &b) && b.out[0].kind == kOutSkillPanel);
    CHECK(ParseBinding("Action:SkillPanel, cmd:0x25", CmdName, &b) && b.out[0].kind == kOutSkillPanel && b.out[1].value == 0x25);
    CHECK(ParseBinding("", CmdName, &b) && b.out[0].kind == kOutNone);
    CHECK(!ParseBinding("pad:Z", CmdName, &b));
    CHECK(!ParseBinding("cmd:0x45", CmdName, &b));        // the game has 0x45 commands: 0..0x44
    CHECK(!ParseBinding("cmd:No such", CmdName, &b));
    CHECK(!ParseBinding("action:fly", CmdName, &b));
    CHECK(!ParseBinding("pad:A, pad:B, pad:X, pad:Y", CmdName, &b));   // three outputs at most
    CHECK(!ParseBinding("attack", CmdName, &b));          // a kind is needed
    // a bad line leaves the binding as it was
    Binding keep;
    keep.out[0] = OutAction(act::kMap);
    CHECK(!ParseBinding("pad:Q", CmdName, &keep) && keep.out[0].value == act::kMap);
}

// the javelin quiver's two outputs: a request, a step on the press's edge (directinput::TakeQuiverStep), none in the flat views
static void TestQuiverOutputs() {
    Binding b;
    CHECK(ParseBinding("action:quivernext", CmdName, &b) && b.out[0].kind == kOutQuiver && b.out[0].value == 1);
    CHECK(ParseBinding("Action:QuiverPrev, pad:B", CmdName, &b) && b.out[0].kind == kOutQuiver && b.out[0].value == 2 && b.out[1].kind == kOutPad);
    CHECK(!ParseBinding("action:quiver", CmdName, &b));
    Layout L;
    Table tb = DefaultTable();
    tb.plain[kRB] = Bind(OutQuiver(2));
    tb.plain[kLY] = Bind(OutQuiver(1), OutPadBtn(pad::kY));
    L.SetWorld(tb);
    Outputs o = At(L, Mode::World, 0, {});
    CHECK(o.quiver == 0);
    o = At(L, Mode::World, 10, {{kRB, 1.0f}});
    CHECK(o.quiver == 2 && o.actions == 0);
    o = At(L, Mode::World, 20, {{kRB, 1.0f}});
    CHECK(o.quiver == 2);                                   // (true while the press is on: the API takes its edge)
    o = At(L, Mode::World, 30, {});
    CHECK(o.quiver == 0);
    o = At(L, Mode::World, 40, {{kLY, 1.0f}});
    CHECK(o.quiver == 1 && (o.pad.buttons & pad::kY));      // with another output on the same control
    o = At(L, Mode::World, 50, {{kLY, 1.0f}, {kRB, 1.0f}});
    CHECK(o.quiver == 3);                                   // both
    o = At(L, Mode::World, 60, {});
    // VR F1-F3 have no quiver (nor a skill panel): stripped from the table
    const Table w = WithoutPanels(tb);
    CHECK(w.plain[kRB].out[0].kind == kOutNone && w.plain[kLY].out[0].kind == kOutNone && w.plain[kLY].out[1].kind == kOutPad);
    // in a menu the layout is the menu's: no quiver
    o = At(L, Mode::Menu, 70, {{kRB, 1.0f}});
    CHECK(o.quiver == 0);
}

static void TestMath() {
    using namespace layout::math;
    const float idq[4] = {0, 0, 0, 1};
    HeadAngles a = Angles(idq);
    CHECK(Near(a.yawDeg, 0) && Near(a.pitchDeg, 0) && Near(a.rollDeg, 0));
    const float s45 = std::sin(0.7853982f), c45 = std::cos(0.7853982f);
    const float left90[4] = {0, s45, 0, c45};   // +90 deg about up: turned left
    a = Angles(left90);
    CHECK(Near(a.yawDeg, 90.0f, 0.01f) && Near(a.pitchDeg, 0) && Near(a.rollDeg, 0));
    const float up30[4] = {std::sin(0.2617994f), 0, 0, std::cos(0.2617994f)};   // +30 about x: looking up
    a = Angles(up30);
    CHECK(Near(a.pitchDeg, 30.0f, 0.01f) && Near(a.yawDeg, 0));
    const float tiltRight[4] = {0, 0, -std::sin(0.2617994f), std::cos(0.2617994f)};   // rolled toward the right shoulder
    a = Angles(tiltRight);
    CHECK(Near(a.rollDeg, 30.0f, 0.01f));
    // a hand 1 m to the room's right of a head turned left 90 deg is behind it (z back)
    float hp[3] = {0, 1.6f, 0}, pos[3] = {1, 1.2f, 0}, outP[3], outQ[4];
    IntoHeading(1.5707963f, hp, pos, idq, outP, outQ);
    CHECK(Near(outP[0], 0) && Near(outP[1], -0.4f) && Near(outP[2], 1.0f));
    // the hand's own orientation with the head's turn taken off: a hand turned like the head is straight in the frame
    IntoHeading(1.5707963f, hp, pos, left90, outP, outQ);
    CHECK(Near(outQ[0], 0) && Near(outQ[1], 0) && Near(outQ[2], 0) && Near(std::fabs(outQ[3]), 1.0f));
}

// -- the runtime with synthetic frames ---------------------------------------------------------------------------------

static D2RVR_XrInput Input(bool focused) {
    D2RVR_XrInput in{};
    in.version = D2RVR_XR_INPUT_VERSION;
    in.focused = focused ? 1u : 0u;
    in.displayTime = 1000000000ll;
    in.head.valid = 1;
    in.head.pos[1] = 1.7f;
    in.head.rot[3] = 1.0f;
    for (int h = 0; h < 2; ++h) {
        in.hand[h].active = 1;
        in.hand[h].grip.valid = 1;
        in.hand[h].grip.rot[3] = 1.0f;
        in.hand[h].grip.pos[0] = h ? 0.3f : -0.3f;
        in.hand[h].grip.pos[1] = 1.2f;
        in.hand[h].grip.pos[2] = -0.3f;
    }
    return in;
}

static std::wstring TempIni() {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"dit", 0, path);
    return path;
}

static int g_listened = 0;
static uint32_t g_lastSession = 0, g_lastEpoch = 0, g_lastFlags = 0;
static bool g_takeGrip = false;
static uint32_t g_takeBits1 = 0;   // whatever a gesture takes of the right hand this frame (the bow's draw-only mode: kNoShotTrigger ...)
static void Listener(const Frame& f, void*) {
    ++g_listened;
    g_lastSession = f.session; g_lastEpoch = f.epoch; g_lastFlags = f.flags;
    if (g_takeGrip) Take(1, kTakeSqueeze, kSrcShoulderSwap);
    if (g_takeBits1) Take(1, g_takeBits1, kSrcBowDraw);
}

static void TestRuntime() {
    const std::wstring ini = TempIni();
    // direct mode off: the frames and the gestures' calls work, the pad is not ours
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!On() && !Active());
    CHECK(Listen(&Listener, nullptr) && !Listen(&Listener, nullptr));
    SessionBegin();
    D2RVR_XrInput in = Input(true);
    in.hand[1].trigger = 1.0f;
    Feed(&in, false);
    Frame f;
    CHECK(Latest(&f) && f.counter == 1 && (f.flags & kFocused) && !(f.flags & kDirect) && Near(f.hand[1].trigger, 1.0f));
    CHECK(g_listened == 1 && !Active());
    PadOut po;
    CHECK(!PadRead(&po));
    uint64_t cm[2];
    // a gesture's command / action / pad pulse work with BodyWalk's input
    Command(0x2C, true, kSrcShoulderSwap);
    InjectedCommands(cm);
    CHECK(cm[0] == (1ull << 0x2C));
    Command(0x2C, true, kSrcBowDraw);
    Command(0x2C, false, kSrcShoulderSwap);
    InjectedCommands(cm);
    CHECK(cm[0] == (1ull << 0x2C));   // another source still holds it
    Command(0x2C, false, kSrcBowDraw);
    InjectedCommands(cm);
    CHECK(cm[0] == 0);
    Tap(0x17);
    InjectedCommands(cm);
    CHECK(cm[0] & (1ull << 0x17));
    Sleep(120);
    InjectedCommands(cm);
    CHECK(!(cm[0] & (1ull << 0x17)));
    Action(act::kSwap, true, kSrcShoulderSwap);
    Action(act::kSwap, true, kSrcMelee);
    Action(act::kSwap, false, kSrcMelee);
    CHECK(InjectedActions() == act::kSwap);
    Action(act::kSwap, false, kSrcShoulderSwap);
    CHECK(InjectedActions() == 0);
    uint16_t pb; uint8_t plt, prt;
    CHECK(!PadPulses(&pb, &plt, &prt));
    PadPulse({pad::kB, 0, 0}, 60);
    CHECK(PadPulses(&pb, &plt, &prt) && pb == pad::kB);
    Sleep(120);
    CHECK(!PadPulses(&pb, &plt, &prt));
    // PadPulseEnd: ends that press at once (the Smite's 180 ms press when the game has taken it), and only that one
    PadPulse({pad::kX, 255, 0}, 5000);
    PadPulse({pad::kY, 0, 0}, 5000);
    CHECK(PadPulses(&pb, &plt, &prt) && pb == (pad::kX | pad::kY) && plt == 255);
    PadPulseEnd({pad::kX, 0, 0});   // another trigger: not the same press
    CHECK(PadPulses(&pb, &plt, &prt) && pb == (pad::kX | pad::kY));
    PadPulseEnd({pad::kX, 255, 0});
    CHECK(PadPulses(&pb, &plt, &prt) && pb == pad::kY && plt == 0);
    PadPulseEnd({pad::kY, 0, 0});
    CHECK(!PadPulses(&pb, &plt, &prt));
    // a panel of ours is up: the flag reaches the listeners, and goes with the lease
    SetUiUp(true);
    Feed(&in, false);
    CHECK(g_lastFlags & kUiUp);
    SetUiUp(false);
    Feed(&in, false);
    CHECK(!(g_lastFlags & kUiUp));
    SetUiUp(true);
    Sleep(300);
    Feed(&in, false);
    CHECK(!(g_lastFlags & kUiUp));
    // direct mode on, with the ini's table
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_b", L"cmd:Swap weapons", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"left_y", L"none", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_trigger", L"pad:RT", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"left_x", L"pad:Nonsense", ini.c_str());   // not understood: the default stays
    LoadSettings(ini.c_str());
    CHECK(On() && !Active());
    SessionBegin();
    // an unfocused frame: nothing pressed
    in = Input(false);
    in.hand[1].trigger = 1.0f;
    in.hand[0].stickY = 1.0f;
    Feed(&in, false);
    CHECK(Active() && PadRead(&po) && po.rt == 0 && po.ly == 0 && LayoutActions() == 0);
    // focus comes back with the trigger and the stick held: the gate holds them until they are let go
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    in.hand[0].stickY = 1.0f;
    Feed(&in, false);
    CHECK(PadRead(&po) && po.rt == 0 && po.ly == 0);
    in = Input(true);
    Feed(&in, false);
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    in.hand[0].stickY = 1.0f;
    in.hand[1].buttons = kSecondary;
    Feed(&in, false);
    CHECK(PadRead(&po) && po.rt == 255 && po.ly == 32767);   // right_trigger = pad:RT per the ini
    uint64_t lc[2];
    LayoutCommands(lc);
    CHECK(lc[0] == (1ull << 0x2C));                           // right_b = cmd:Swap weapons
    uint64_t all[2] = {0, 0};
    {   // the way link.cpp reads: the layout's plus the gestures'
        InjectedCommands(all);
        all[0] |= lc[0];
    }
    CHECK(all[0] == (1ull << 0x2C));
    {   // a taken thumbstick (the skill panel's Settings tab flips its pages with it) reads centred for the layout and for BodyWalk's hands
        in = Input(true);
        in.hand[1].stickX = 1.0f;
        in.hand[1].stickY = -0.5f;
        in.hand[0].stickY = 1.0f;
        g_takeBits1 = kTakeStick;
        Feed(&in, false);
        g_takeBits1 = 0;
        CHECK(in.hand[1].stickX == 0.0f && in.hand[1].stickY == 0.0f && in.hand[0].stickY == 1.0f);   // (the other hand's is not)
        CHECK(PadRead(&po) && po.rx == 0 && po.ry == 0 && po.ly == 32767);
        in = Input(true);
        in.hand[1].stickX = 1.0f;
        Feed(&in, false);
        CHECK(PadRead(&po) && po.rx == 32767);   // let go of the take: the stick is the layout's again at once
    }
    // left_y = none; left_x kept its default
    in = Input(true);
    in.hand[0].buttons = kSecondary | kPrimary;
    Feed(&in, false);
    CHECK(PadRead(&po) && po.buttons == pad::kX);
    // vrcam saw a panel open before the next frame: the world's outputs read as let go until Feed has caught up
    SetMenuNow(false);
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    Feed(&in, false);
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    in.hand[0].buttons = kPrimary;
    in.hand[1].buttons = 0;
    Feed(&in, false);
    CHECK(PadRead(&po) && po.rt == 255);
    SetMenuNow(true);
    CHECK(PadRead(&po) && po.buttons == 0 && po.rt == 0 && po.ly == 0);
    LayoutCommands(lc);
    CHECK(lc[0] == 0 && lc[1] == 0 && LayoutActions() == 0);
    // the menu flag: epoch moves, the layout is the menu's
    const uint32_t epochWorld = (Latest(&f), f.epoch);
    in = Input(true);
    in.hand[1].buttons = kPrimary;
    Feed(&in, true);
    Latest(&f);
    CHECK(f.epoch != epochWorld && (f.flags & kInMenu) && (f.flags & kDirect));
    in = Input(true);
    Feed(&in, true);
    in = Input(true);
    in.hand[1].buttons = kPrimary;
    Feed(&in, true);
    CHECK(PadRead(&po) && po.buttons == pad::kA && LayoutActions() == 0);
    SetMenuNow(true);   // (the frame says menu too)
    // the head and hands as vrcam reads them
    const D2RVR_Shared* sh = HeadShared();
    CHECK(sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid && sh->handsValid == 3 && Near(sh->headHeightM, 1.7f));
    CHECK(sh && Near(sh->rightHand[0], 0.3f) && Near(sh->leftHand[0], -0.3f) && Near(sh->rightHand[1], -0.5f) && Near(sh->rightHand[2], -0.3f));
    // a gesture takes the right grip: the layout never sees it, and BodyWalk's copy of the frame neither
    g_takeGrip = true;
    in = Input(true);
    in.hand[1].squeeze = 1.0f;
    in.hand[1].buttons = kSqueeze;
    Feed(&in, true);
    CHECK(in.hand[1].squeeze == 0.0f && !(in.hand[1].buttons & kSqueeze));
    CHECK(PadRead(&po) && !(po.buttons & pad::kRightBumper));
    g_takeGrip = false;
    in = Input(true);
    in.hand[1].squeeze = 1.0f;
    in.hand[1].buttons = kSqueeze;
    Feed(&in, true);   // the gesture let go, the hand did not: still not the layout's
    CHECK(PadRead(&po) && !(po.buttons & pad::kRightBumper));
    // the session ends: nothing is held, no frame is left
    Command(0x25, true, kSrcOther0);
    SessionEnd();
    CHECK(!Active() && !PadRead(&po) && !Latest(&f));
    InjectedCommands(cm);
    CHECK(cm[0] == 0);
    CHECK(LayoutActions() == 0);
    // a stopped game: frames older than a second hold nothing
    SessionBegin();
    in = Input(true);
    Feed(&in, false);
    CHECK(Active());
    Sleep(1100);
    CHECK(!Active() && !PadRead(&po));
    Unlisten(&Listener, nullptr);
    SessionEnd();
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// The runtime's long press, in real time: frames every 15 ms from a synthetic headset.
static void TestRuntimeHold() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"hold_ms", L"200", ini.c_str());
    LoadSettings(ini.c_str());
    SetMenuNow(false);
    SessionBegin();
    bool sawSwap = false, sawInventory = false;
    auto frames = [&](int ms, uint32_t rightButtons, uint32_t leftButtons, bool uiUp = false) {
        for (int t = 0; t < ms; t += 15) {
            D2RVR_XrInput in = Input(true);
            in.hand[1].buttons = rightButtons;
            in.hand[0].buttons = leftButtons;
            SetUiUp(uiUp);
            Feed(&in, false);
            const uint32_t a = LayoutActions();
            if (a & 16u) sawSwap = true;      // D2RVR_ACT_SWAP
            if (a & 4u) sawInventory = true;  // D2RVR_ACT_INVENTORY
            Sleep(15);
        }
    };
    frames(60, 0, 0);
    // a tap of the right stick click: the swap, after the release; no request for the panel
    frames(45, kStick, 0);
    CHECK(!sawSwap);
    frames(150, 0, 0);
    CHECK(sawSwap && !TakeSkillPanelToggle());
    // a long press: the request, once, and no swap
    sawSwap = false;
    frames(450, kStick, 0);
    frames(150, 0, 0);
    CHECK(!sawSwap);
    CHECK(TakeSkillPanelToggle());
    CHECK(!TakeSkillPanelToggle());
    // the left stick click held long: the inventory
    frames(450, 0, kStick);
    frames(150, 0, 0);
    CHECK(sawInventory);
    // the left menu button: a tap is the inventory (when it is let go), a long press is the pause menu (Esc) - once, and no inventory
    sawInventory = false;
    frames(60, 0, 0);
    frames(45, 0, kMenuButton);
    CHECK(!sawInventory);
    frames(150, 0, 0);
    CHECK(sawInventory && !TakePauseToggle());
    sawInventory = false;
    frames(450, 0, kMenuButton);
    frames(150, 0, 0);
    CHECK(!sawInventory);
    CHECK(TakePauseToggle());
    CHECK(!TakePauseToggle());
    // a panel of ours up: no pause either
    frames(450, 0, kMenuButton, true);
    frames(150, 0, 0, true);
    CHECK(!TakePauseToggle());
    SetUiUp(false);
    // a panel of the game's up (a menu): the menu button is the game's Start, no request
    frames(60, 0, 0);
    SetMenuNow(true);
    for (int t = 0; t < 600; t += 15) {
        D2RVR_XrInput in = Input(true);
        in.hand[0].buttons = t < 450 ? kMenuButton : 0;
        Feed(&in, true);
        Sleep(15);
    }
    CHECK(!TakePauseToggle());
    SetMenuNow(false);
    frames(60, 0, 0);
    // a request nobody took is dropped after a second
    frames(60, 0, 0);
    frames(450, 0, kMenuButton);
    frames(60, 0, 0);
    Sleep(1100);
    CHECK(!TakePauseToggle());
    // [direct] pause_button=0: the menu button is the inventory the moment it is pressed, and a long press asks for nothing
    WritePrivateProfileStringW(L"direct", L"pause_button", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    frames(60, 0, 0);
    sawInventory = false;
    frames(30, 0, kMenuButton);
    CHECK(sawInventory);
    frames(450, 0, kMenuButton);
    frames(150, 0, 0);
    CHECK(!TakePauseToggle());
    // and the pause on another button: [direct] right_a_hold = action:pause
    WritePrivateProfileStringW(L"direct", L"pause_button", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"left_menu_hold", L"none", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_a_hold", L"action:pause", ini.c_str());
    LoadSettings(ini.c_str());
    frames(60, 0, 0);
    frames(450, 0, kMenuButton);
    frames(150, 0, 0);
    CHECK(!TakePauseToggle());
    frames(450, kPrimary, 0);
    frames(150, 0, 0);
    CHECK(TakePauseToggle());
    WritePrivateProfileStringW(L"direct", L"left_menu_hold", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_a_hold", nullptr, ini.c_str());
    LoadSettings(ini.c_str());
    // a panel of ours up: the long press asks for nothing
    sawSwap = false;
    frames(450, kStick, 0, true);
    frames(150, 0, 0, true);
    CHECK(!TakeSkillPanelToggle() && !sawSwap);
    SetUiUp(false);
    // a request nobody took is dropped after a second
    frames(60, 0, 0);
    frames(450, kStick, 0);
    frames(60, 0, 0);
    Sleep(1100);
    CHECK(!TakeSkillPanelToggle());
    // hold_enabled=0: the click acts at once, nothing is asked for
    WritePrivateProfileStringW(L"direct", L"hold_enabled", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    frames(60, 0, 0);
    sawSwap = false;
    frames(30, kStick, 0);
    CHECK(sawSwap);
    frames(450, kStick, 0);
    CHECK(!TakeSkillPanelToggle());
    SetUiUp(false);
    SessionEnd();
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- F4 only: in every other view the layer is as if direct_input were 0 (vr/view_gate.h) ----------------------------------
static bool g_viewIsF4 = true;
static bool TheView() { return g_viewIsF4; }
static int g_hapticCalls = 0;
static void HapticCounter(int, float, float, float) { ++g_hapticCalls; }

static void TestOtherView() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    viewgate::Set(&TheView);
    g_viewIsF4 = true;
    SetHapticSink(&HapticCounter);
    SetMenuNow(false);
    CHECK(Listen(&Listener, nullptr));
    SessionBegin();
    PadOut po;
    Frame f;
    uint64_t cm[2];
    uint16_t pb; uint8_t plt, prt;

    // F4: the layer is in charge; a trigger held, a command, an action, a pad pulse and a take of the grip under way
    D2RVR_XrInput in = Input(true);
    Feed(&in, false);
    in = Input(true);
    Feed(&in, false);
    in = Input(true);
    in.hand[1].trigger = 1.0f;   // the right trigger: the attack
    Feed(&in, false);
    CHECK(Governing() && Active() && PadRead(&po) && (LayoutActions() & act::kAttack));
    Command(0x2C, true, kSrcShoulderSwap);
    Action(act::kSwap, true, kSrcMelee);
    PadPulse({pad::kB, 0, 0}, 5000);
    Haptic(1, 0.5f, 20.0f);
    CHECK(g_hapticCalls == 1);
    InjectedCommands(cm);
    CHECK(cm[0] == (1ull << 0x2C) && InjectedActions() == act::kSwap && PadPulses(&pb, &plt, &prt) && pb == pad::kB);
    const uint32_t epochF4 = (Latest(&f), f.epoch);
    CHECK(!(f.flags & kOtherView) && (f.flags & kDirect) && HeadShared());

    // F4 -> F2: BodyWalk's input is back at once - before any frame - and a frame lets go of everything held
    g_viewIsF4 = false;
    CHECK(On() && !Governing() && !Active());
    CHECK(!PadRead(&po) && LayoutActions() == 0 && InjectedActions() == 0 && !PadPulses(&pb, &plt, &prt));
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);
    LayoutCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);
    g_takeGrip = true;   // a gesture that forgot to stand down: its take does nothing, BodyWalk keeps the grip
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    in.hand[1].squeeze = 1.0f;
    in.hand[1].buttons = kSqueeze | kTrigger;
    g_listened = 0;
    Feed(&in, false);
    g_takeGrip = false;
    CHECK(g_listened == 1);
    CHECK(in.hand[1].squeeze == 1.0f && (in.hand[1].buttons & kSqueeze) && (in.hand[1].buttons & kTrigger));
    Latest(&f);
    CHECK((f.flags & kOtherView) && (f.flags & kInMenu) && !(f.flags & kDirect) && (f.flags & kFocused));
    CHECK(f.epoch != epochF4 && g_lastEpoch == f.epoch);
    CHECK(!Active() && !PadRead(&po) && !HeadShared() && LayoutActions() == 0);
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && InjectedActions() == 0 && !PadPulses(&pb, &plt, &prt));
    // nothing a gesture asks for acts there: no command, tap, action, pad pulse or haptic
    Command(0x2C, true, kSrcShoulderSwap);
    Tap(0x17);
    Action(act::kAttack, true, kSrcJavelin);
    PadPulse({pad::kX, 0, 0}, 5000);
    Haptic(0, 0.5f, 20.0f);
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && InjectedActions() == 0 && !PadPulses(&pb, &plt, &prt) && g_hapticCalls == 1);
    // ... a release still goes through (nothing is left held by a source that let go in the other view)
    Command(0x2C, false, kSrcShoulderSwap);
    Action(act::kAttack, false, kSrcJavelin);
    // frames keep coming and change nothing for the layout
    for (int i = 0; i < 3; ++i) { in = Input(true); in.hand[1].trigger = 1.0f; Feed(&in, false); CHECK(!Active() && !PadRead(&po) && LayoutActions() == 0); }
    // the key off in the other view: still BodyWalk's, and nothing wakes when the view comes back with the key off
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!On() && !Governing() && !Active());
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());

    // F2 -> F4 with the trigger still held: starts fresh - the gate holds it until it is let go, and nothing from before comes back
    g_viewIsF4 = true;
    CHECK(Governing());
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    Feed(&in, false);
    Latest(&f);
    CHECK(!(f.flags & kOtherView) && (f.flags & kDirect) && f.epoch != g_lastEpoch - 1);
    CHECK(Active() && PadRead(&po) && (LayoutActions() & act::kAttack) == 0);   // held through the change: not an attack
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && InjectedActions() == 0 && !PadPulses(&pb, &plt, &prt));
    in = Input(true);
    Feed(&in, false);
    in = Input(true);
    in.hand[1].trigger = 1.0f;
    Feed(&in, false);
    CHECK(PadRead(&po) && (LayoutActions() & act::kAttack));   // a fresh press is the attack again
    Command(0x2C, true, kSrcShoulderSwap);
    InjectedCommands(cm);
    CHECK(cm[0] == (1ull << 0x2C));   // the gestures work again in F4
    Haptic(1, 0.5f, 20.0f);
    CHECK(g_hapticCalls == 2);

    Unlisten(&Listener, nullptr);
    SetHapticSink(nullptr);
    SessionEnd();
    viewgate::Set(nullptr);
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- VR F1, F2, F3: the left stick walks, nothing else of direct input acts (vr/walk_flat.h, [direct] walk_flat_views) ----------

static bool g_testF4 = false, g_testFlat = true;
static bool TestViewIsF4() { return g_testF4; }
static bool TestViewIsFlat() { return g_testFlat; }

static D2RVR_XrInput Walking(bool focused, float x, float y) {
    D2RVR_XrInput in = Input(focused);
    in.hand[0].stickX = x;
    in.hand[0].stickY = y;
    return in;
}

static void TestWalkFlat() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());   // walk_flat_views left out: on
    viewgate::Set(&TestViewIsF4);
    viewgate::SetFlat(&TestViewIsFlat);
    g_testF4 = false; g_testFlat = true;
    SessionBegin();
    walkflat::Inputs w;
    WalkOut o{};
    PadOut po;
    uint64_t cm[2];
    uint16_t pb; uint8_t plt, prt;
    D2RVR_XrInput in;
    auto feed = [&](bool focused, float x, float y, bool menu = false) { in = Walking(focused, x, y); Feed(&in, menu); };
    auto walk = [&]() { return WalkRead(&o, &w); };

    // before any frame: the facts are said, the stick is not fresh and reads zero
    CHECK(walk() && w.keyOn && w.directOn && w.flatView && !w.framesFresh && o.lx == 0 && o.ly == 0);

    // a stick already held when the first frame comes in walks nothing until it is let go
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && w.framesFresh && o.lx == 0 && o.ly == 0);
    feed(true, 0.0f, 0.0f);
    CHECK(walk() && o.lx == 0 && o.ly == 0);
    // pushed forward: the pad's left stick, the frame's number moving
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && w.framesFresh && o.lx == 0 && o.ly == 32767);
    const uint32_t c1 = o.counter;
    feed(true, 1.0f, 1.0f);   // a corner is brought into the unit disc, as the F4 layout does
    CHECK(walk() && Near(o.lx / 32767.0f, 0.7071f) && Near(o.ly / 32767.0f, 0.7071f) && o.counter == c1 + 1);
    feed(true, -0.5f, 0.25f);
    CHECK(walk() && Near(o.lx / 32767.0f, -0.5f) && Near(o.ly / 32767.0f, 0.25f));

    // everything else of direct input stands down there, whatever the controls do
    in = Walking(true, 0.0f, 1.0f);
    in.hand[1].trigger = 1.0f; in.hand[1].squeeze = 1.0f;
    in.hand[1].buttons = kTrigger | kSqueeze | kPrimary | kSecondary | kStick;
    in.hand[0].buttons = kMenuButton | kStick | kPrimary;
    Feed(&in, false);
    CHECK(!Governing() && !Active() && !PadRead(&po) && LayoutActions() == 0 && InjectedActions() == 0);
    LayoutCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0 && !PadPulses(&pb, &plt, &prt) && !HeadShared());
    Command(0x2C, true, kSrcShoulderSwap);
    Tap(0x17);
    Action(act::kAttack, true, kSrcJavelin);
    PadPulse({pad::kX, 0, 0}, 5000);
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && InjectedActions() == 0 && !PadPulses(&pb, &plt, &prt));
    // ... and BodyWalk keeps every control it was given: nothing hidden, nothing taken
    CHECK(in.hand[1].trigger == 1.0f && in.hand[1].squeeze == 1.0f && (in.hand[1].buttons & kTrigger) && (in.hand[0].buttons & kMenuButton));
    CHECK(walk() && o.ly == 32767);   // (the walk went on meanwhile)

    // a panel opens: the stick is the menu's; closed with the stick still held, nothing walks until it is let go
    feed(true, 0.0f, 1.0f, true);
    CHECK(walk() && o.lx == 0 && o.ly == 0);
    feed(true, 0.0f, 1.0f, false);
    CHECK(walk() && o.lx == 0 && o.ly == 0);
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);

    // the input focus goes (the dashboard): nothing; back with the stick held, nothing until it is let go
    feed(false, 0.0f, 1.0f);
    CHECK(walk() && o.lx == 0 && o.ly == 0);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 0);
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);

    // a hand that is not there, a stick that is not a number
    in = Walking(true, 0.0f, 1.0f);
    in.hand[0].active = 0;
    Feed(&in, false);
    CHECK(walk() && o.lx == 0 && o.ly == 0);
    feed(true, 0.0f, 0.0f);
    feed(true, NAN, 1.0f);
    CHECK(walk() && o.lx == 0 && o.ly == 0);

    // F4: the layout is in charge and the walk module has nothing - no stale stick comes back
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);
    g_testF4 = true; g_testFlat = false;
    CHECK(!walk() && !w.flatView && w.keyOn && w.directOn);
    feed(true, 0.0f, 1.0f);
    CHECK(Governing() && !walk());
    // ... and back to F2 with the stick still held: it starts masked
    g_testF4 = false; g_testFlat = true;
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && w.framesFresh && o.ly == 0);
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);

    // the key off ([direct] walk_flat_views=0): nothing at once, whatever the frames bring; on again it starts masked
    WritePrivateProfileStringW(L"direct", L"walk_flat_views", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!walk() && !w.keyOn);
    feed(true, 0.0f, 1.0f);
    CHECK(!walk());
    WritePrivateProfileStringW(L"direct", L"walk_flat_views", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(walk() && w.keyOn && !w.framesFresh && o.ly == 0);   // (nothing published while it was off)
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && w.framesFresh && o.ly == 0);               // held through the switch
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);

    // direct_input off: not this module's, as before
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!walk() && !w.directOn);
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());

    // the frames stop: after a second the stick reads zero and is not fresh (a stalled game holds nothing down)
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    CHECK(walk() && w.framesFresh && o.ly == 32767);
    Sleep(1100);
    CHECK(walk() && !w.framesFresh && o.lx == 0 && o.ly == 0);

    // the session ends: gone
    feed(true, 0.0f, 0.0f);
    feed(true, 0.0f, 1.0f);
    SessionEnd();
    CHECK(walk() && !w.framesFresh && o.ly == 0);

    // no function registered for the flat views (the settings program, every other test): never
    viewgate::SetFlat(nullptr);
    CHECK(!walk() && !w.flatView);
    viewgate::Set(nullptr);
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- VR F1, F2, F3: the left trigger + a flick of a stick drinks a belt column (vr/belt_flick.h, [direct] belt_flick_flat_views) ----

static int g_beltCalls = 0;
static uint32_t g_beltCol[32];
static int g_beltStick[32], g_beltDir[32];
static void BeltSinkForTest(uint32_t column, int stick, int dir) {
    if (g_beltCalls < 32) { g_beltCol[g_beltCalls] = column; g_beltStick[g_beltCalls] = stick; g_beltDir[g_beltCalls] = dir; }
    ++g_beltCalls;
}

static void TestBeltFlat() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());   // belt_flick_flat_views and walk_flat_views left out: both on
    viewgate::Set(&TestViewIsF4);
    viewgate::SetFlat(&TestViewIsFlat);
    g_testF4 = false; g_testFlat = true;
    g_beltCalls = 0;
    SetBeltSink(&BeltSinkForTest);
    SessionBegin();
    walkflat::Inputs w;
    WalkOut o{};
    D2RVR_XrInput in;
    uint64_t cm[2];
    auto frame = [&](float trig, float lx, float ly, float rx = 0.0f, float ry = 0.0f, bool menu = false, bool focused = true) {
        in = Walking(focused, lx, ly);
        in.hand[0].trigger = trig;
        in.hand[1].stickX = rx; in.hand[1].stickY = ry;
        Feed(&in, menu);
    };
    auto walk = [&]() { return WalkRead(&o, &w); };

    // walking, then the trigger goes down: the left stick stops walking, and the push that was a walk is not a flick
    frame(0.0f, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 0);    // (the first frame in a flat view starts masked: a held stick walks nothing until it is let go)
    frame(0.0f, 0.0f, 0.0f);
    frame(0.0f, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767 && g_beltCalls == 0);
    frame(1.0f, 0.0f, 1.0f);
    CHECK(walk() && o.lx == 0 && o.ly == 0 && g_beltCalls == 0);   // the trigger alone, the stick still out: the hero stops, no potion
    frame(1.0f, 0.0f, 0.0f);
    CHECK(walk() && o.ly == 0 && g_beltCalls == 0);

    // the four ways of the left stick: columns 1..4 (0..3), one call each, the left stick never walks meanwhile
    const struct { float x, y; uint32_t col; int dir; } ways[] = {{-1, 0, 0, 1}, {0, 1, 1, 2}, {1, 0, 2, 3}, {0, -1, 3, 4}};
    int n = 0;
    for (const auto& way : ways) {
        frame(1.0f, 0.0f, 0.0f);
        frame(1.0f, way.x, way.y);
        CHECK(g_beltCalls == n + 1 && g_beltCol[n] == way.col && g_beltStick[n] == 0 && g_beltDir[n] == way.dir);
        CHECK(walk() && o.lx == 0 && o.ly == 0);
        frame(1.0f, way.x, way.y);   // held out: no second potion
        CHECK(g_beltCalls == n + 1);
        ++n;
    }
    // the right stick works the same
    frame(1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(g_beltCalls == n + 1 && g_beltCol[n] == 1 && g_beltStick[n] == 1);
    ++n;
    frame(1.0f, 0.0f, 0.0f, -1.0f, 0.0f);   // still out the right stick... back to the middle first
    CHECK(g_beltCalls == n);
    frame(1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 0.0f, -1.0f, 0.0f);
    CHECK(g_beltCalls == n + 1 && g_beltCol[n] == 0);
    ++n;
    // nothing else of direct input acts: no command, action, pulse - the press is the sink's (vrcam's gamecmd::Tap)
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0 && InjectedActions() == 0 && !Active() && LayoutActions() == 0);

    // the trigger let go with the left stick pushed: the stick walks nothing until it is let go; then it walks again
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 1.0f);
    frame(0.0f, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 0);
    frame(0.0f, 0.0f, 0.0f);
    frame(0.0f, 0.0f, 1.0f);
    CHECK(walk() && o.ly == 32767);
    n = g_beltCalls;

    // no trigger, no potion, whatever the sticks do; a panel, no focus: none
    frame(0.0f, -1.0f, 0.0f); frame(0.0f, 0.0f, 0.0f); frame(0.0f, 0.0f, 1.0f, 1.0f, 0.0f); frame(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(g_beltCalls == n);
    frame(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    frame(1.0f, -1.0f, 0.0f, 0.0f, 0.0f, true);
    CHECK(g_beltCalls == n);
    frame(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
    frame(1.0f, -1.0f, 0.0f, 0.0f, 0.0f, false, false);
    CHECK(g_beltCalls == n);
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, -1.0f, 0.0f);
    CHECK(g_beltCalls == n + 1);   // focus back, the middle, a flick: the potion
    n = g_beltCalls;

    // F4: the layout is in charge, this module has nothing - no stale flick
    g_testF4 = true; g_testFlat = false;
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, -1.0f, 0.0f);
    CHECK(g_beltCalls == n && Governing());
    frame(0.0f, 0.0f, 0.0f);
    g_testF4 = false; g_testFlat = true;
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 1.0f);
    CHECK(g_beltCalls == n + 1 && g_beltCol[n] == 1);   // back in F2 it works again
    n = g_beltCalls;

    // belt key off ([direct] belt_flick_flat_views=0): no potion, the trigger holds nothing back, the left stick walks with it held
    WritePrivateProfileStringW(L"direct", L"belt_flick_flat_views", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!BeltFlickOn());
    frame(0.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 1.0f);
    CHECK(g_beltCalls == n && walk() && o.ly == 32767);
    // the walk key off, the belt key on: potions still, no walk
    WritePrivateProfileStringW(L"direct", L"belt_flick_flat_views", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"walk_flat_views", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(BeltFlickOn() && !walk() && !w.keyOn);
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 1.0f, 0.0f);
    CHECK(g_beltCalls == n + 1 && g_beltCol[n] == 2 && !walk());
    n = g_beltCalls;
    // direct_input off: nothing
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    frame(1.0f, 0.0f, 0.0f);
    frame(1.0f, 0.0f, 1.0f);
    CHECK(g_beltCalls == n);

    SetBeltSink(nullptr);
    SessionEnd();
    viewgate::SetFlat(nullptr);
    viewgate::Set(nullptr);
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- VR F1-F3 without BodyWalk: the plugin's own controller is the game's pad ([direct] flat_controller) -------------------------------

static bool g_flatGateOk = true;
static bool FlatGateForTest() { return g_flatGateOk; }

static void TestFlatController() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());   // every flat key left out: on
    viewgate::Set(&TestViewIsF4);
    viewgate::SetFlat(&TestViewIsFlat);
    g_testF4 = false; g_testFlat = true; g_flatGateOk = true;
    g_beltCalls = 0;
    SetBeltSink(&BeltSinkForTest);
    SetFlatGate(&FlatGateForTest);
    SetMenuNow(false);
    SessionBegin();
    FlatPad fp{};
    PadOut po;
    uint64_t cm[2];
    D2RVR_XrInput in;
    auto frame = [&](bool menu = false) { Feed(&in, menu); };
    auto idle = [&]() { in = Walking(true, 0.0f, 0.0f); frame(); };
    auto pad = [&]() { return FlatPadRead(&fp); };

    // nothing before a frame
    CHECK(FlatControllerOn() && !FlatControllerLive() && !pad() && FlatActions() == 0);
    FlatCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);
    idle(); idle();
    CHECK(FlatControllerLive() && pad() && fp.buttons == 0 && fp.lt == 0 && fp.rt == 0 && fp.lx == 0 && fp.ly == 0 && fp.rx == 0 && fp.ry == 0);
    const uint32_t c0 = fp.counter;
    idle();
    CHECK(pad() && fp.counter == c0 + 1);

    // the right trigger attacks (the layout's action - the pad's A that never picks up), nothing of it on the pad itself
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame();
    CHECK((FlatActions() & act::kAttack) != 0 && pad() && (fp.buttons & pad::kA) == 0 && fp.rt == 0);
    idle();
    CHECK(FlatActions() == 0);
    // A: pick up / interact (an action); the right hand's second button B, the left hand's X and Y: the skill slots on the pad
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kPrimary; frame();
    CHECK((FlatActions() & act::kInteract) != 0);
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame();
    CHECK(pad() && (fp.buttons & pad::kB) != 0);
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[0].buttons = kPrimary; frame();
    CHECK(pad() && (fp.buttons & pad::kX) != 0);
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[0].buttons = kSecondary; frame();
    CHECK(pad() && (fp.buttons & pad::kY) != 0);
    idle();
    // the menu button: the inventory; the right stick's click: the weapon swap AT ONCE (the skill panel's long press is F4's, not here)
    in = Walking(true, 0.0f, 0.0f); in.hand[0].buttons = kMenuButton; frame();
    CHECK((FlatActions() & act::kInventory) != 0);
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kStick; frame();
    CHECK((FlatActions() & act::kSwap) != 0);
    for (int i = 0; i < 20; ++i) frame();
    Sleep(600);   // (a long press: F4 would ask for its skill panel now)
    frame();
    CHECK(!TakeSkillPanelToggle());
    idle();
    // the left stick's click: Alt (show items), the game's own key command, held
    in = Walking(true, 0.0f, 0.0f); in.hand[0].buttons = kStick; frame();
    FlatCommands(cm);
    CHECK((cm[0] & (1ull << 0x25)) != 0);
    idle();
    FlatCommands(cm);
    CHECK((cm[0] & (1ull << 0x25)) == 0);
    // the triggers: the left one is the pad's LT (the second skill bank), analog
    in = Walking(true, 0.0f, 0.0f); in.hand[0].trigger = 0.5f; frame();
    CHECK(pad() && fp.lt >= 127 && fp.lt <= 129);
    idle();
    // the right stick: the pad's right stick; the left stick: the walk, in the pad's left stick
    in = Walking(true, 0.0f, 1.0f); in.hand[1].stickX = 0.5f; in.hand[1].stickY = -0.5f; frame();
    CHECK(pad() && fp.lx == 0 && fp.ly == 32767 && fp.rx > 16000 && fp.rx < 16500 && fp.ry < -16000 && fp.ry > -16500);
    WalkOut wo{};
    walkflat::Inputs wi;
    CHECK(WalkRead(&wo, &wi) && wo.ly == 32767);   // (the walk's own word: the walking hooks read it the way they read BodyWalk's stick)
    idle();

    // the left trigger held and a flick: the belt's, the sticks are not the game's meanwhile (no walk, no aim), the trigger itself is the pad's LT
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[0].trigger = 1.0f; frame();
    in = Walking(true, 0.0f, 0.0f); in.hand[0].trigger = 1.0f; in.hand[1].stickX = -1.0f; frame();
    CHECK(g_beltCalls == 1 && g_beltCol[0] == 0);
    CHECK(pad() && fp.lt == 255 && fp.rx == 0 && fp.ry == 0 && fp.lx == 0 && fp.ly == 0);
    idle();

    // nothing of F4: the layout's outputs in F4 are not here, and nothing here is there
    CHECK(!Active() && !PadRead(&po) && LayoutActions() == 0);
    InjectedCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);

    // a panel opens: the menu layer - the game's own pad, control for control; the world's actions are not given
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kPrimary; frame();
    CHECK((FlatActions() & act::kInteract) != 0);   // (world)
    SetMenuNow(true);
    CHECK(pad() && fp.buttons == 0 && FlatActions() == 0);   // (the other layer's outputs are let go until the next frame)
    in = Walking(true, 0.0f, 0.0f); frame(true);
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kPrimary; frame(true);
    CHECK(pad() && (fp.buttons & pad::kA) != 0 && FlatActions() == 0);
    in = Walking(true, 0.0f, 0.0f); frame(true);
    in = Walking(true, 0.0f, 1.0f); frame(true);
    CHECK(pad() && fp.ly == 32767);   // (the stick is the menu's cursor)
    SetMenuNow(false);
    in = Walking(true, 0.0f, 0.0f); frame();
    in = Walking(true, 0.0f, 0.0f); frame();
    CHECK(pad() && fp.buttons == 0);

    // BodyWalk running (the gate says no): nothing of ours - not a button, not an action, not a command; its pad is the game's, as before
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; in.hand[0].buttons = kStick; frame();
    CHECK(FlatControllerLive() && (FlatActions() & act::kAttack) != 0);
    g_flatGateOk = false;
    CHECK(!FlatControllerLive() && !pad() && FlatActions() == 0);
    FlatCommands(cm);
    CHECK(cm[0] == 0 && cm[1] == 0);
    g_flatGateOk = true;
    idle();

    // F4: the layout is in charge, this has nothing; back in F2 with a control held on the way in, it does nothing until let go
    g_testF4 = true; g_testFlat = false;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame();
    CHECK(!FlatControllerLive() && !pad() && FlatActions() == 0 && Governing());
    g_testF4 = false; g_testFlat = true;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame();
    CHECK(FlatControllerLive() && pad() && (fp.buttons & pad::kB) == 0);
    idle();
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame();
    CHECK(pad() && (fp.buttons & pad::kB) != 0);
    idle();

    // the flat game (the view gate says it is no VR view): untouched
    g_testF4 = false; g_testFlat = false;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame();
    CHECK(!FlatControllerLive() && !pad() && FlatActions() == 0);
    g_testFlat = true;

    // the key off: nothing; the walk alone goes on
    WritePrivateProfileStringW(L"direct", L"flat_controller", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!FlatControllerOn() && !FlatControllerLive() && !pad());
    idle();
    in = Walking(true, 0.0f, 1.0f); frame();
    in = Walking(true, 0.0f, 0.0f); frame();
    in = Walking(true, 0.0f, 1.0f); frame();
    CHECK(!FlatControllerLive() && FlatActions() == 0 && WalkRead(&wo, &wi) && wo.ly == 32767);
    WritePrivateProfileStringW(L"direct", L"flat_controller", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    idle(); idle();
    CHECK(FlatControllerLive() && pad());

    // direct input off: nothing
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!FlatControllerLive() && !pad());
    // no gate registered (the settings program, every other test): never
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    idle(); idle();
    CHECK(FlatControllerLive());
    SetFlatGate(nullptr);
    CHECK(!FlatControllerLive() && !pad());

    SetBeltSink(nullptr);
    SessionEnd();
    SetMenuNow(false);
    viewgate::SetFlat(nullptr);
    viewgate::Set(nullptr);
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- VR F1-F3: a control pressed that nothing of ours takes is said (once in half a minute) ------------------------------------------

static int g_hintCalls = 0;
static bool g_hintAnswer = true;
static const char* g_hintWhat = "";
static bool HintSinkForTest(const char* what) { ++g_hintCalls; g_hintWhat = what; return g_hintAnswer; }

static void TestPressHint() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"flat_controller", L"0", ini.c_str());   // the controller off: the controls it would take are said in the log
    LoadSettings(ini.c_str());
    viewgate::Set(&TestViewIsF4);
    viewgate::SetFlat(&TestViewIsFlat);
    g_testF4 = false; g_testFlat = true;
    g_hintCalls = 0; g_hintAnswer = true;
    SetPressSink(&HintSinkForTest);
    SessionBegin();
    D2RVR_XrInput in;
    auto frame = [&](bool focused, bool menu = false) { Feed(&in, menu); (void)focused; };

    // the walk and the belt are the plugin's here: the left stick, the left trigger, a flick - nothing to say
    in = Walking(true, 0.0f, 1.0f); in.hand[0].trigger = 1.0f; in.hand[1].stickX = 1.0f; frame(true);
    CHECK(g_hintCalls == 0);
    // the right trigger: nothing of ours takes it - said once, with what it was
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame(true);
    CHECK(g_hintCalls == 1 && std::string(g_hintWhat) == "right trigger");
    for (int i = 0; i < 100; ++i) frame(true);
    CHECK(g_hintCalls == 1);   // not again within the window
    // other controls too, but the window is open: still once
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kPrimary; frame(true);
    in = Walking(true, 0.0f, 0.0f); in.hand[0].squeeze = 1.0f; frame(true);
    CHECK(g_hintCalls == 1);

    // a sink that says no (BodyWalk runs, a panel is open): asked again on the next press, the window not started
    SetPressSink(&HintSinkForTest);   // (a fresh window)
    g_hintCalls = 0; g_hintAnswer = false;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame(true);
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame(true);
    CHECK(g_hintCalls == 2 && std::string(g_hintWhat) == "a right-hand button");
    g_hintAnswer = true;
    in = Walking(true, 0.0f, 0.0f); in.hand[0].buttons = kMenuButton; frame(true);
    CHECK(g_hintCalls == 3 && std::string(g_hintWhat) == "a left-hand button");
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame(true);
    CHECK(g_hintCalls == 3);

    // a panel open, or no focus: the controls are the panel's / nobody's - nothing said
    SetPressSink(&HintSinkForTest);
    g_hintCalls = 0;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame(true, true);
    CHECK(g_hintCalls == 0);
    in = Walking(false, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame(false);
    CHECK(g_hintCalls == 0);
    // F4: the layout takes every control - nothing said
    g_testF4 = true; g_testFlat = false;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame(true);
    CHECK(g_hintCalls == 0);
    // the controller on ([direct] flat_controller=1): it takes those controls - nothing to say
    g_testF4 = false; g_testFlat = true;
    WritePrivateProfileStringW(L"direct", L"flat_controller", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    SetPressSink(&HintSinkForTest);
    g_hintCalls = 0;
    in = Walking(true, 0.0f, 0.0f); in.hand[1].trigger = 1.0f; frame(true);
    in = Walking(true, 0.0f, 0.0f); in.hand[1].buttons = kSecondary; frame(true);
    CHECK(g_hintCalls == 0);

    SetPressSink(nullptr);
    SessionEnd();
    viewgate::SetFlat(nullptr);
    viewgate::Set(nullptr);
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// -- [hands] bow_draw_only: a control pressed away from the string gives everything it is bound to but its shots ------------------

static Outputs AtNoShot(Layout& L, uint64_t t, std::initializer_list<std::pair<Ctl, float>> l, uint32_t noShot) {
    return L.Step(Mode::World, Held(l), t, false, noShot);
}

static void TestNoShot() {
    // which outputs are shots
    CHECK(IsShot(OutAction(act::kAttack)) && IsShot(OutPadBtn(pad::kA)) && IsShot({kOutRT, 0}));
    CHECK(!IsShot(OutAction(act::kInteract)) && !IsShot(OutAction(act::kSwap)) && !IsShot(OutPadBtn(pad::kB)) && !IsShot(OutPadBtn(pad::kRightBumper)) &&
          !IsShot(OutCmd(0x1A)) && !IsShot({kOutLT, 0}) && !IsShot(OutSkillPanel()) && !IsShot({}));
    // the mask from a frame's takes: a bit per control, per hand
    {
        const uint32_t take[2] = {kNoShotSqueeze, kNoShotTrigger};
        CHECK(NoShotMask(take) == ((1u << kLGrip) | (1u << kRTrigger)));
        const uint32_t none[2] = {kTakeTrigger | kTrigger, kSqueeze | kTakeSqueeze};
        CHECK(NoShotMask(none) == 0);   // a full take is not this
    }
    const uint32_t rt = 1u << kRTrigger;
    // right_trigger = attack, pad B, potion command: the attack goes, the rest stays
    {
        Table t = DefaultTable();
        t.holdEnabled = false;
        t.plain[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kB), OutCmd(0x1A));
        Layout L;
        L.SetWorld(t);
        Outputs o = AtNoShot(L, 0, {{kRTrigger, 1.0f}}, rt);
        CHECK(o.actions == 0 && o.pad.buttons == pad::kB && o.commands[0] == (1ull << 0x1A));
        o = AtNoShot(L, 10, {}, rt);
        CHECK(o.actions == 0 && o.pad.buttons == 0 && o.commands[0] == 0);
        Layout M;
        M.SetWorld(t);
        o = AtNoShot(M, 0, {{kRTrigger, 1.0f}}, 0);   // no mask: as bound
        CHECK(o.actions == act::kAttack && o.pad.buttons == pad::kB && o.commands[0] == (1ull << 0x1A));
        // the mask is the trigger's: the right A button next to it is untouched
        Layout N;
        N.SetWorld(t);
        o = AtNoShot(N, 0, {{kRA, 1.0f}}, rt);
        CHECK(o.actions == act::kInteract);
        // ... and the other trigger's mask leaves this one alone
        Layout P;
        P.SetWorld(t);
        o = AtNoShot(P, 0, {{kRTrigger, 1.0f}}, 1u << kLTrigger);
        CHECK(o.actions == act::kAttack);
    }
    // only shots bound: nothing at all
    {
        Layout L;
        Table t = DefaultTable();
        t.holdEnabled = false;
        L.SetWorld(t);   // right_trigger = action:attack
        Outputs o = AtNoShot(L, 0, {{kRTrigger, 1.0f}}, rt);
        CHECK(o.actions == 0 && o.pad.buttons == 0 && o.pad.rt == 0);
    }
    // pad A and the analog RT are shots, pad Y is not; the analog follows the trigger's travel even below "pressed"
    {
        Table t = DefaultTable();
        t.holdEnabled = false;
        t.plain[kRTrigger] = Bind({kOutRT, 0}, OutPadBtn(pad::kA), OutPadBtn(pad::kY));
        Layout L;
        L.SetWorld(t);
        Outputs o = AtNoShot(L, 0, {{kRTrigger, 0.3f}}, rt);   // below "pressed": only the analog travel
        CHECK(o.pad.rt == 0);
        o = AtNoShot(L, 10, {{kRTrigger, 1.0f}}, rt);
        CHECK(o.pad.rt == 0 && o.pad.buttons == pad::kY);
        Layout M;
        M.SetWorld(t);
        o = AtNoShot(M, 0, {{kRTrigger, 0.3f}}, 0);
        CHECK(o.pad.rt == 77);
        o = AtNoShot(M, 10, {{kRTrigger, 1.0f}}, 0);
        CHECK(o.pad.rt == 255 && o.pad.buttons == (pad::kA | pad::kY));
    }
    // the chord (the same hand's grip held at the press): its shots go, its other outputs stay
    {
        Table t = DefaultTable();
        t.holdEnabled = false;
        t.chord[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kY), OutAction(act::kMap));
        Layout L;
        L.SetWorld(t);
        AtNoShot(L, 0, {{kRGrip, 1.0f}}, rt);
        Outputs o = AtNoShot(L, 10, {{kRGrip, 1.0f}, {kRTrigger, 1.0f}}, rt);
        CHECK((o.actions & act::kAttack) == 0 && (o.actions & act::kMap) && o.pad.buttons == pad::kY);
    }
    // hold-or-tap on the trigger: the tap's shots and the hold's shots go, the rest stays
    {
        Table t = DefaultTable();
        t.plain[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kB));
        t.hold[kRTrigger] = Bind(OutAction(act::kAttack), OutAction(act::kMap));
        Layout L;
        L.SetWorld(t);
        AtNoShot(L, 0, {}, rt);
        AtNoShot(L, 10, {{kRTrigger, 1.0f}}, rt);
        Outputs o = AtNoShot(L, 150, {}, rt);   // a tap
        CHECK((o.actions & act::kAttack) == 0 && o.pad.buttons == pad::kB);
        AtNoShot(L, 1000, {{kRTrigger, 1.0f}}, rt);
        o = AtNoShot(L, 1600, {{kRTrigger, 1.0f}}, rt);   // a hold
        CHECK((o.actions & act::kAttack) == 0 && (o.actions & act::kMap));
        Layout M;
        M.SetWorld(t);
        AtNoShot(M, 0, {}, 0);
        AtNoShot(M, 10, {{kRTrigger, 1.0f}}, 0);
        o = AtNoShot(M, 150, {}, 0);
        CHECK((o.actions & act::kAttack) && o.pad.buttons == pad::kB);
        AtNoShot(M, 1000, {{kRTrigger, 1.0f}}, 0);
        o = AtNoShot(M, 1600, {{kRTrigger, 1.0f}}, 0);
        CHECK((o.actions & act::kAttack) && (o.actions & act::kMap));
    }
    // a grip bound to an attack and a bumper
    {
        Table t = DefaultTable();
        t.holdEnabled = false;
        t.plain[kRGrip] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kRightBumper));
        Layout L;
        L.SetWorld(t);
        Outputs o = AtNoShot(L, 0, {{kRGrip, 1.0f}}, 1u << kRGrip);
        CHECK(o.actions == 0 && o.pad.buttons == pad::kRightBumper);
    }
    // the mask turned on under a press that began without it: from then on its shots are not given (the rest goes on)
    {
        Table t = DefaultTable();
        t.holdEnabled = false;
        t.plain[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kB));
        Layout L;
        L.SetWorld(t);
        Outputs o = AtNoShot(L, 0, {{kRTrigger, 1.0f}}, 0);
        CHECK(o.actions == act::kAttack && o.pad.buttons == pad::kB);
        o = AtNoShot(L, 10, {{kRTrigger, 1.0f}}, rt);
        CHECK(o.actions == 0 && o.pad.buttons == pad::kB);
    }
}

// The runtime end to end: a gesture's Take reaches the layout as the mask (Feed), a whole take hides the control, and neither is a thing
// outside F4.
static void TestNoShotRuntime() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_trigger", L"action:attack, pad:B, cmd:0x1A", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"hold_enabled", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    viewgate::Set(&TheView);
    g_viewIsF4 = true;
    SetMenuNow(false);
    CHECK(Listen(&Listener, nullptr));
    SessionBegin();
    PadOut po;
    uint64_t lc[2];
    auto press = [&](uint32_t take) {
        g_takeBits1 = take;
        D2RVR_XrInput in = Input(true);
        Feed(&in, false);
        in = Input(true);
        Feed(&in, false);   // (let go first: the gate)
        in = Input(true);
        in.hand[1].trigger = 1.0f;
        in.hand[1].buttons = kTrigger;
        Feed(&in, false);
        PadRead(&po);
        LayoutCommands(lc);
    };
    // nothing taken: all of it
    press(0);
    CHECK((LayoutActions() & act::kAttack) && (po.buttons & pad::kB) && (lc[0] & (1ull << 0x1A)));
    // the shots only: the attack goes, the pad button and the command stay
    press(kNoShotTrigger);
    CHECK(!(LayoutActions() & act::kAttack) && (po.buttons & pad::kB) && (lc[0] & (1ull << 0x1A)));
    // taken whole: nothing of the binding fires
    press(kTakeTrigger | kTrigger);
    CHECK(LayoutActions() == 0 && !(po.buttons & pad::kB) && lc[0] == 0);
    // whole and shots-only together: whole
    press(kTakeTrigger | kTrigger | kNoShotTrigger);
    CHECK(LayoutActions() == 0 && !(po.buttons & pad::kB) && lc[0] == 0);
    // another view: the take does nothing, the binding is BodyWalk's business and the layer lets go of its own
    g_viewIsF4 = false;
    CHECK(!Active() && !PadRead(&po) && LayoutActions() == 0);
    g_viewIsF4 = true;
    g_takeBits1 = 0;
    Unlisten(&Listener, nullptr);
    SessionEnd();
    viewgate::Set(nullptr);
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// The pause button is F4's: in another view (and in the F1-F3 controller, without BodyWalk) a long press of the left menu button asks for nothing,
// and a request made in F4 and not yet taken is not taken after the view changed.
// The touch lines through the whole layer, in real time: the finger's bits of the frame (xr.cpp's TouchBit) to the game's command.
static void TestTouchRuntime() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"right_b_touch", L"cmd:Run / walk switch", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"left_stick_touch", L"none", ini.c_str());
    LoadSettings(ini.c_str());
    viewgate::Set(&TestViewIsF4);
    g_testF4 = true;
    SetMenuNow(false);
    SessionBegin();
    int edges = 0;
    bool was = false;
    uint32_t seenTouches = 0;
    auto frames = [&](int ms, uint32_t right, bool uiUp = false) {
        for (int t = 0; t < ms; t += 15) {
            D2RVR_XrInput in = Input(true);
            const uint32_t touches[2] = {0, right};
            SetUiUp(uiUp);
            Feed(&in, false, touches);
            uint64_t lc[2];
            LayoutCommands(lc);
            const bool now = (lc[0] >> 0x23) & 1u;
            if (now && !was) ++edges;
            was = now;
            Frame f;
            if (Latest(&f)) seenTouches |= f.hand[1].touches;
            Sleep(15);
        }
    };
    frames(60, 0);
    CHECK(edges == 0);
    frames(450, kTouchSecondary);                  // the finger arrives and stays: the game's switch is pressed once
    CHECK(edges == 1);
    frames(60, 0);
    frames(450, kTouchSecondary);                  // lifted and put back: once more
    CHECK(edges == 2);
    frames(60, 0);
    frames(450, kTouchPrimary);                    // another control's touch: nothing
    CHECK(edges == 2);
    frames(60, 0);
    frames(300, kTouchSecondary, true);            // a panel of ours up: nothing
    CHECK(edges == 2);
    frames(60, 0, true);
    SetUiUp(false);
    CHECK((seenTouches & kTouchSecondary) && (seenTouches & kTouchPrimary));   // (the frame the listeners get carries the touches)
    frames(60, 0);
    // the game's menu: the pad is the game's own, a touch is nothing
    SetMenuNow(true);
    for (int t = 0; t < 300; t += 15) {
        D2RVR_XrInput in = Input(true);
        const uint32_t touches[2] = {0, t >= 60 ? kTouchSecondary : 0u};
        Feed(&in, true, touches);
        uint64_t lc[2];
        LayoutCommands(lc);
        CHECK(!((lc[0] >> 0x23) & 1u));
        Sleep(15);
    }
    SetMenuNow(false);
    frames(60, 0);
    // another view: the layout does not run, nothing is pressed
    g_testF4 = false;
    frames(60, 0);
    frames(300, kTouchSecondary);
    CHECK(edges == 2);
    g_testF4 = true;
    // switched off in the ini: no touch line any more
    WritePrivateProfileStringW(L"direct", L"right_b_touch", L"none", ini.c_str());
    LoadSettings(ini.c_str());
    frames(60, 0);
    frames(300, kTouchSecondary);
    CHECK(edges == 2);
    SessionEnd();
    viewgate::Set(nullptr);
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

static void TestPauseOtherView() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    WritePrivateProfileStringW(L"direct", L"hold_ms", L"200", ini.c_str());
    LoadSettings(ini.c_str());
    viewgate::Set(&TestViewIsF4);
    viewgate::SetFlat(&TestViewIsFlat);
    g_testF4 = true;
    SetMenuNow(false);
    SessionBegin();
    auto frames = [&](int ms, uint32_t left) {
        for (int t = 0; t < ms; t += 15) {
            D2RVR_XrInput in = Input(true);
            in.hand[0].buttons = left;
            Feed(&in, false);
            Sleep(15);
        }
    };
    frames(60, 0);
    frames(450, kMenuButton);
    frames(150, 0);
    CHECK(TakePauseToggle());   // F4: asked for
    // another view: nothing is asked for
    g_testF4 = false;
    g_testFlat = true;   // VR F1-F3 (the flat controller feeds there when BodyWalk is not running)
    frames(60, 0);
    frames(450, kMenuButton);
    frames(150, 0);
    CHECK(!TakePauseToggle());
    // asked for in F4, the view changes before it is taken: dropped
    g_testF4 = true;
    g_testFlat = false;
    frames(60, 0);
    frames(450, kMenuButton);
    frames(60, 0);
    g_testF4 = false;
    CHECK(!TakePauseToggle());
    g_testF4 = true;
    CHECK(!TakePauseToggle());   // (and it was thrown away, not kept for later)
    SessionEnd();
    viewgate::SetFlat(nullptr);
    viewgate::Set(nullptr);
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

// FlatVR's raw controllers ([direct] flatvr_controls, vr/flat_raw.h): BodyWalk's Game Link sample made into the frame and fed - the layout,
// the release gate and the pad run on it as on native OpenXR's, and the session says it is Game Link's (link::Head keeps BodyWalk's head).
static BW_LinkTracking RawSample(uint32_t stamp, bool hands, float trigR, float stickLY, uint64_t buttonsR = 0) {
    BW_LinkTracking t{};
    t.header.magic = BW_LINK_MAGIC;
    t.header.version = BW_LINK_TRACKING_VERSION;
    t.header.sequence = stamp * 2;
    t.flags = BW_LINK_TRACK_HEAD | BW_LINK_TRACK_PITCH | BW_LINK_TRACK_ROLL | BW_LINK_TRACK_ROOM | BW_LINK_TRACK_CONTROLS;
    t.stamp = stamp;
    t.head.valid = 1;
    t.head.pos[0] = 0.1f; t.head.pos[1] = 1.7f; t.head.pos[2] = -0.2f;
    for (int h = 0; h < 2; ++h) {
        BW_LinkHand& o = h ? t.right : t.left;
        o.valid = hands ? 1u : 0u;
        o.pos[0] = h ? 0.3f : -0.3f; o.pos[1] = -0.5f; o.pos[2] = -0.3f;
        o.rot[3] = 1.0f;
    }
    t.right.trigger = trigR;
    t.right.buttons = buttonsR;
    t.left.stickY = stickLY;
    return t;
}

static void TestFlatRawFeed() {
    const std::wstring ini = TempIni();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(FlatVrControlsOn());   // [direct] flatvr_controls defaults to 1
    WritePrivateProfileStringW(L"direct", L"flatvr_controls", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(!FlatVrControlsOn());
    WritePrivateProfileStringW(L"direct", L"flatvr_controls", L"1", ini.c_str());
    LoadSettings(ini.c_str());
    CHECK(FlatVrControlsOn());
    SessionBegin(true);
    CHECK(FedByLink());
    flatraw::Clock clock;
    uint32_t n = 0, stamp = 1000;
    auto feed = [&](const BW_LinkTracking& t) {
        D2RVR_XrInput in;
        flatraw::ToXrInput(t, flatraw::Step(clock, t.stamp), ++n, t.stamp, &in);
        Feed(&in, false);
    };
    feed(RawSample(stamp += 111, true, 0.0f, 0.0f));   // a first frame with nothing held (the release gate opens)
    PadOut po{};
    CHECK(Active() && PadRead(&po) && po.ly == 0 && po.rt == 0);
    feed(RawSample(stamp += 111, true, 1.0f, 1.0f));   // the right trigger pulled, the left stick forward
    CHECK(PadRead(&po) && po.ly == 32767);              // the layout's left stick walks
    CHECK(LayoutActions() != 0 || po.rt == 255);        // the right trigger does what the layout binds it to (by default: attack)
    Frame f;
    CHECK(Latest(&f) && (f.flags & kFocused) && (f.flags & kDirect));
    CHECK(Near(f.hand[1].grip.pos[0], 0.4f) && Near(f.hand[1].grip.pos[1], 1.2f) && Near(f.hand[1].grip.pos[2], -0.5f));   // head + the hand's offset
    CHECK(Near((float)f.time, 0.0111f, 1e-6f));          // the plugin's stamps as time (0.1 ms each: 111 since the first)
    // FlatVR without the input focus: the hands are not tracked - nothing pressed, and the held trigger waits for its release after
    feed(RawSample(stamp += 111, false, 1.0f, 1.0f));
    CHECK(PadRead(&po) && po.ly == 0 && po.rt == 0 && LayoutActions() == 0);
    feed(RawSample(stamp += 111, true, 1.0f, 1.0f));
    CHECK(PadRead(&po) && po.ly == 0 && po.rt == 0 && LayoutActions() == 0);
    SessionEnd();
    CHECK(!FedByLink() && !Active());
    SessionBegin();
    CHECK(!FedByLink());   // a native session's
    SessionEnd();
    WritePrivateProfileStringW(L"openxr", L"direct_input", L"0", ini.c_str());
    LoadSettings(ini.c_str());
    DeleteFileW(ini.c_str());
}

int main() {
    TestHoldTapMachine();
    TestHoldTapLayout();
    TestTouchLines();
    TestWorldLayout();
    TestSecondRow();
    TestSticks();
    TestHysteresis();
    TestChordsAndLatch();
    TestMenuLayout();
    TestGate();
    TestGateTaken();
    TestMenuRoundTrip();
    TestParse();
    TestQuiverOutputs();
    TestMath();
    TestRuntime();
    TestRuntimeHold();
    TestPauseOtherView();
    TestTouchRuntime();
    TestOtherView();
    TestWalkFlat();
    TestBeltFlat();
    TestFlatController();
    TestPressHint();
    TestNoShot();
    TestNoShotRuntime();
    TestFlatRawFeed();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("direct input: all %d checks passed\n", g_checks);
    return 0;
}
