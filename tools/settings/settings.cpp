// D2R_VR_Settings.exe - sliders for d2r_vr.ini.
//
// Lives next to the ini in <game>\d2rloader\plugins. Every slider or tick box
// writes its key the moment it moves; vrcam re-reads the file within half a
// second, so the game changes while you watch.
//
// The settings are split over tabs. On a tab the groups flow into as many
// columns as the window is wide, and what does not fit scrolls. Per-monitor
// DPI aware; Ctrl + wheel (or Ctrl +, Ctrl -, Ctrl 0) zooms on top of that.
// The window's own size, zoom and tab go to d2r_vr_settings.ini, never to
// d2r_vr.ini (a write there makes vrcam re-read it).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "d2r_vr_build.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>   // CommandLineToArgvW: the installer's --setup-bodywalk
#include "arcane_assets.h"   // [sky] arcane_sky: the Arcane Sanctuary's sky pictures out of the player's own game (CascLib)
#include <tlhelp32.h>
#pragma comment(lib, "version.lib")   // GetFileVersionInfo: which BodyWalk is new enough
#include <winhttp.h>    // the update check, D2RLoader's download
#include <wincrypt.h>   // CryptBinaryToStringW: PowerShell's -EncodedCommand
#include <bcrypt.h>     // SHA-256 of the D2RLoader download

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <deque>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "d2r_vr_state.h"
#include "d2r_vr_shared.h"   // BodyWalk Game Link (bodywalk/game_link.h): is BodyWalk there, head tracking, FlatVR wishes
#include "afr_eye_shared.h"  // FlatVR's head sample: is FlatVR running
#include "game_sigs.h"       // the game addresses the mod looks for (the Status tab)
#include "d2r_vr_version.h"  // D2RVR_VERSION: vrcam's g_info_version, made by tools/settings/version.cmake
#include "render_size.h"     // [render] vr_resolution_px / vr_resolution: the VR render size, read the way the plugin reads it
#include "window_json.h"     // the game's windowed size in Settings.json and what each platform does to it (the plugin's policy, tested)
#include "turn_mode.h"       // [turn] smooth / snap / speed: how the right stick turns, and what an older ini's snap=0 means (the plugin's rule)
#include "mod_layout.h"      // D2R VR as the D2RLoader mod D2R-VR: its ini and ReShade under <game>\mods\D2R-VR (shared/mod_layout.h)
#include "reshade_as.h"      // Home: the ReShade the player has as dxgi.dll is used as it is; the buttons for it, tested (shared/reshade_as.h)
#include "reshade_paths.h"   // D2R VR's two search paths in that ReShade's ReShade.ini: append-only text edits, tested (shared/reshade_paths.h)
#include "bw_output.h"      // BodyWalk's Output in its settings file: does its pad reach the game (the Status row, tested)
#include "bw_path.h"        // how BodyWalk's input reaches the game: Game Link direct, or its gamepad and Mapping (the Status row, tested)
#include "frame_cap.h"       // the game's frame cap in Settings.json: VR's rate is VR's, flat mode has the player's own (the plugin's policy, tested)
#include "toolbar_type.h"   // the toolbar's type: one choice over [hud] hide and bar_place (the headset panel has it too)
#include "../../vr/direct_remap.h"   // [direct]: the list of controller buttons ("@remap" rows), read and written by the plugin's own parser (tested)
#include "../../vr/xr_stick_check.h" // is the thumbstick bound by SteamVR? the plugin's verdict (state block bits 13 - 15) in words
#include "safe_screen.h"     // the game's interface size in Settings.json: VR starts it at 80 where the player chose none, flat mode has his own (the plugin's policy, tested)

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// What players call this program: the Start menu entry and the desktop shortcut, the window's title, the captions of its message boxes, Setup's finish page - the mod's own
// settings (D2R VR Authentic's). A display name only: the file stays D2R_VR_Settings.exe, the ini and the ids as they are.
constexpr const wchar_t* kAppName = L"D2R VR Authentic Settings";

// Tab and Group are headers; Invert: a tick box that writes -1 / 1; Choice: a
// drop-down for a small integer; File: a picture path; Color: #RRGGBB or empty.
// Radio: a Choice shown as one button per value (min = the first value);
// Status: a line of the Home page's status column, filled in by RefreshStatus.
enum class Kind { Tab, Group, Slider, Toggle, Invert, Choice, File, Color, Radio, Status, Button };

// Which of the two ways to play an item is shown in (the Home page's [mode] platform).
constexpr int kFlat = 1, kVr = 2, kBoth = 3;

struct Item {
    Kind kind = Kind::Slider;
    const wchar_t* section = nullptr;
    const wchar_t* key = nullptr;
    const wchar_t* label = nullptr;
    float min = 0, max = 1, step = 1, def = 0;
    const wchar_t* sdef = nullptr;           // File: the default path; Color: what an empty value means
    const wchar_t* tip = nullptr;            // the tooltip: what the short label leaves out
    std::vector<const wchar_t*> choices;     // Choice: the names of 0, 1, 2...
    struct Need { const wchar_t* section; const wchar_t* key; float value; int item; const wchar_t* orKey = nullptr; int orItem = -1; };
    std::vector<Need> needs;                 // greyed out unless every such key has its value - or its orKey has (items: set by Build)
    int tab = 0;                             // set by Build
    int modes = kBoth;                       // shown in flat, VR or both (Build folds in its tab's and group's)
    std::vector<HWND> radios;                // Radio: one button per value
    int soon = 1 << 20;                      // Radio: the choices from this one on are coming soon - shown, greyed, never picked
    unsigned soonMask = 0;                   // Radio: these choices (bit k = choice k) are coming soon as well, wherever they sit
    COLORREF color = 0;                      // Status: the line's colour now
    bool hidden = false;                     // left out of the page for now (the update row until there is one)
    int col = -1;                            // Group: the column it goes in (0 left); -1 = the shortest column so far (LayoutPage)
    bool big = false;                        // Button: a large one (the Home page's START)
    int cell = -1;                           // a row of a table (the Strikes tab's kinds): which cell, 0 = the row's label, which starts the row; the others sit beside it (a slider shows its number box only)
    struct ShowFor { const wchar_t* section = nullptr; const wchar_t* key = nullptr; unsigned mask = 0; int item = -1; } showFor;
    bool off = false;                        // left out while showFor's key has a value not in its mask (UpdateEnabled)
    const wchar_t* oldKey = nullptr;         // Slider / Choice: while its own key is not in the ini yet it starts from this older key of the same section (ReadValue)
    std::vector<float> oldMap;               // ... through this table when there is one (the older key's value v gives oldMap[v])
    HWND name = nullptr, ctl = nullptr, value = nullptr, browse = nullptr, clear = nullptr, line = nullptr;
    HBRUSH brush = nullptr;                  // Color: the swatch
};

Item Tab(const wchar_t* title) { Item i; i.kind = Kind::Tab; i.label = title; return i; }
Item Group(const wchar_t* title) { Item i; i.kind = Kind::Group; i.label = title; return i; }

Item Slider(const wchar_t* section, const wchar_t* key, const wchar_t* label, float min, float max, float step, float def,
            const wchar_t* tip = nullptr) {
    Item i;
    i.section = section; i.key = key; i.label = label;
    i.min = min; i.max = max; i.step = step; i.def = def; i.tip = tip;
    return i;
}
// A push button: each press adds one to its ini value, and vrcam acts on the change.
Item Button(const wchar_t* section, const wchar_t* key, const wchar_t* label, const wchar_t* tip = nullptr);
Item Toggle(const wchar_t* section, const wchar_t* key, const wchar_t* label, float def, const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, label, 0, 1, 1, def, tip); i.kind = Kind::Toggle; return i;
}
Item Invert(const wchar_t* section, const wchar_t* key, const wchar_t* label, float def, const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, label, -1, 1, 2, def, tip); i.kind = Kind::Invert; return i;
}
Item Choice(const wchar_t* section, const wchar_t* key, const wchar_t* label, float def,
            std::initializer_list<const wchar_t*> names, const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, label, 0, (float)names.size() - 1, 1, def, tip);
    i.kind = Kind::Choice; i.choices = names;
    return i;
}
Item File(const wchar_t* section, const wchar_t* key, const wchar_t* label, const wchar_t* def, const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, label, 0, 0, 0, 0, tip); i.kind = Kind::File; i.sdef = def; return i;
}
Item Color(const wchar_t* section, const wchar_t* key, const wchar_t* label, const wchar_t* empty, const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, label, 0, 0, 0, 0, tip); i.kind = Kind::Color; i.sdef = empty; return i;
}

Item Radio(const wchar_t* section, const wchar_t* key, int first, float def, std::initializer_list<const wchar_t*> names,
           const wchar_t* tip = nullptr) {
    Item i = Slider(section, key, L"", (float)first, (float)(first + (int)names.size() - 1), 1, def, tip);
    i.kind = Kind::Radio; i.choices = names;
    return i;
}
Item Button(const wchar_t* section, const wchar_t* key, const wchar_t* label, const wchar_t* tip) {
    Item i = Slider(section, key, label, 0, 1000000, 1, 0, tip); i.kind = Kind::Button; return i;
}
// A radio whose choices from `from` on are coming soon (the public build's first person).
Item Soon(int from, Item i) { i.soon = from; return i; }
Item SoonChoice(int k, Item i) { i.soonMask |= 1u << k; return i; }
Item Status(const wchar_t* key, const wchar_t* label) { Item i = Slider(L"@status", key, label, 0, 0, 0, 0); i.kind = Kind::Status; return i; }
// Shown only in flat (kFlat) or only in VR (kVr): a tab, a group or a single item.
Item Only(int modes, Item i) { i.modes = modes; return i; }
// A slider that took over from an older key: until its own key is written, it shows (and the plugin uses) the older one's value.
Item StartsFrom(const wchar_t* oldKey, Item i) { i.oldKey = oldKey; return i; }
Item StartsFromMapped(const wchar_t* oldKey, std::initializer_list<float> map, Item i) { i.oldKey = oldKey; i.oldMap = map; return i; }
// Left out of the page until the code shows it (Item::hidden).
Item Hide(Item i) { i.hidden = true; return i; }
// A group in a column of its own choice (0 left); with fewer columns than that it goes in the shortest one.
Item InColumn(int column, Item i) { i.col = column; return i; }
// A large push button (taller, bigger letters).
Item Big(Item i) { i.big = true; return i; }

// Greyed out while section/key is not `value` (a tick box: 1 = ticked); nests for several.
Item Needs(const wchar_t* section, const wchar_t* key, float value, Item i) {
    i.needs.push_back({section, key, value, -1}); return i;
}
// The same with two keys of a section, either of which may have the value (either tick box ticked).
Item NeedsEither(const wchar_t* section, const wchar_t* key, const wchar_t* orKey, float value, Item i) {
    i.needs.push_back({section, key, value, -1, orKey, -1}); return i;
}

// The items of "Low life: red heartbeat vignette" (UI: F4 body): greyed unless native OpenXR is on and the vignette ticked.
Item LowLife(Item i) { return Needs(L"openxr", L"on", 1, Needs(L"hud", L"low_hp_vignette", 1, i)); }

// On the page only while section/key (a Choice) has one of the values in `mask` (bit v = value v).
Item ShowFor(const wchar_t* section, const wchar_t* key, unsigned mask, Item i) {
    i.showFor.section = section; i.showFor.key = key; i.showFor.mask = mask; return i;
}
// The items of "A dropped weapon" that only matter while the weapon goes away ([weight] after_drop 1 = the back, 2 = vanishes): on the page then,
// greyed unless the arms are on the controllers and weight is on. And those that place it on the back, for 1 alone.
Item AwayShow(Item i) { return ShowFor(L"weight", L"after_drop", (1u << 1) | (1u << 2), Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, i))); }
Item BackNeeds(Item i) { return ShowFor(L"weight", L"after_drop", 1u << 1, Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, i))); }
// One spell on the Auto-target tab: ticked = it keeps the game's auto-target ([autotarget] s<skill>), greyed while
// spell_aim is off (then every spell has it) or the attack is not along the hand ([bow] mode 4).
Item Spell(const wchar_t* key, const wchar_t* name, float def = 0) {
    return Needs(L"bow", L"mode", 4, Needs(L"hands", L"spell_aim", 1, Toggle(L"autotarget", key, name, def)));
}

// One skill of [gameplay] melee_skill_armed's list (vr/melee_arm.h kRows, the same order and defaults): ticked = its pad button arms it ([melee_armed] s<skill>); greyed while hit by
// touch or the arming is off.
constexpr const wchar_t* kArmedTip = L"Ticked: this skill's pad button (B X Y RB, with or without LT) ARMS it - every hit by touch is then the game's attack with it, paying its mana (the "
                                     L"plain attack when the mana is short). Unticked: its button casts it as ever. Live.";
constexpr const wchar_t* kArmedMoverTip = L"A skill that MOVES the hero - off by default. Ticked: its pad button arms it, and every hit by touch is the game's own cast of it at the monster "
                                          L"you touched: Whirlwind whirls to and through that monster's place, Leap Attack leaps at it, Charge charges it, Dragon Flight flies to it - the hero "
                                          L"(and so you) moves, as the game moves him; Stand Still is not held for such a hit. Its mana as the game counts it; the plain attack when it is "
                                          L"short. Unticked: its button casts it as ever (with the game's own aim). Live. Not tried in the game yet.";
Item ArmedSkill(const wchar_t* key, const wchar_t* name, float def = 1, const wchar_t* tip = kArmedTip) {
    return Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_skill_armed", 1, Toggle(L"melee_armed", key, name, def, tip)));
}

// The toolbar on a forearm (Where: 1, 2) or on the body (3, 4): a set of sliders each.
constexpr unsigned kBarArm = (1u << 1) | (1u << 2), kBarBody = (1u << 3) | (1u << 4), kBarWrist = 1u << 5;   // bar_place 5: life and mana on the wrists

constexpr const wchar_t* kSkyPicture = L"A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
                                       L"Empty (x) = the shader draws this sky itself.";
constexpr const wchar_t* kFogColour = L"The fog colour of this act, its caves included; the low part of the sky sinks into it. "
                                      L"Empty (x) = the sky's horizon colour, or dark without a sky.";
constexpr const wchar_t* kTorchesTip = L"WARNING: very heavy on the CPU. To find the torches the mod reads all of the game's memory "
                                       L"every 1.5 s while a ceiling is drawn (about a third of a CPU core) - on a weaker PC the game "
                                       L"and VR stutter. Fire seen in the picture (torches, braziers, fire spells) then lights the "
                                       L"ceiling over it.";
constexpr const wchar_t* kPictureFireTip = L"Fire found in the picture by ReShade lights the ceiling: torches, braziers, fire spells, "
                                           L"burning monsters - any fire on the screen, as the first ceilings did. No reading of the "
                                           L"game's memory, so light on the CPU (a little GPU). A fire out of sight is remembered for "
                                           L"8 s; one that goes out in plain view goes dark at once. In place of the torches above.";
constexpr const wchar_t* kOrdinaryBarsTip = L"The name plate and health bar the game hangs over the monster you target are taken off the screen for an "
                                            L"ordinary monster; a champion's, a unique's, a boss's, their minions' and the NPCs' stay. F4 only; the other views draw every plate. Only what is "
                                            L"drawn changes: the target, the attack and everything else about the monster are the game's own. Works on the "
                                            L"plate that hangs over the monster (the game's controller layout, the one D2R VR plays in). Off: every plate as "
                                            L"the game draws it. Live; the hook goes in the first time the game runs its plate code (target a monster).";

// The render size's two tips say what the headset recommends and what a step costs: rebuilt (UpdateResolutionTexts) as the slider
// moves and as the game writes d2r_vr_headset.ini. The tooltips hold these buffers, not copies.
wchar_t g_resTip[2600] = L"The size of the game's picture in the headset, a square of its side in pixels, whatever size the game window is. "
                         L"Drag it to any size from 1024 to 4096; the left end is the game window's own size.";
wchar_t g_resTipHeadset[900] = L"Take the size the headset's runtime recommends per eye, its own shape.";

// "@remap" rows: one button of a controller, its action chosen from a list (vr/direct_remap.h - the same list and the same ini keys the plugin
// reads, [direct] right_trigger, left_stick_hold, left_grip+left_menu ...). The list's names, once; the last is "Custom" - a line written by
// hand that the list has no name for, which it leaves alone.
const std::vector<const wchar_t*>& RemapNames() {
    static std::deque<std::wstring> text;
    static std::vector<const wchar_t*> names;
    if (names.empty()) {
        for (const directinput::remap::Pick& p : directinput::remap::Picks()) text.emplace_back(p.label, p.label + std::strlen(p.label));
        text.emplace_back(L"Custom (written by hand in d2r_vr.ini)");
        for (const std::wstring& s : text) names.push_back(s.c_str());
    }
    return names;
}
// What a touch row adds to its tip: the finger ARRIVING on the control, not a press.
constexpr const wchar_t* kTouchTip =
    L" TOUCH, not a press: this fires once when your finger ARRIVES on the control (the capacitive touch of a Touch controller - PS VR2 on SteamVR is "
    L"one - or an Index controller), nothing more while it stays, and once again the next time it arrives. It is on top of what pressing the same control "
    L"does (the touch comes first). A Vive, WMR or simple controller reports no touch: nothing happens there. First person (F4), in the world only - not in "
    L"a menu or with a panel of the mod's own up. Nothing is bound by default; to put the game's run / walk switch here pick \"Run / walk switch\". "
    L"The log (d2r-vr-vrcam.log) says which touch inputs your controller reports.";
Item RemapRow(const wchar_t* key, const wchar_t* label, const wchar_t* touchTip = nullptr) {
    static std::deque<std::wstring> tips;   // (the tooltip holds the pointer)
    std::wstring tip = L"What this does, from the list; it is written to d2r_vr.ini, section [direct], as \"";
    tip += key;
    tip += L" = ...\". The standard choice is not written at all (the line goes), so a new default still reaches you; \"Nothing\" is written as "
           L"none. \"Custom\" is a line you wrote by hand that this list has no name for (two outputs, a key command not in the list): it is left "
           L"as it is until you pick something here. A pad button (Gamepad B, X, Y, RB...) is the game's own skill button; \"Pause menu (Esc)\" taps the "
           L"keyboard's Esc for the game. Live. Works in F4 and, for the controllers in F1 - F3 without BodyWalk, there too (the pause menu and "
           L"the skill panel are F4's).";
    if (touchTip) tip += touchTip;
    tips.push_back(tip);
    Item i = Slider(L"@remap", key, label, 0, (float)RemapNames().size() - 1, 1, 0, tips.back().c_str());
    i.kind = Kind::Choice;
    i.choices = RemapNames();
    return i;
}
// The rows of the list that need the controls without BodyWalk (and, for a long press, hold_enabled).
Item DirectNeeds(Item i) { return Needs(L"openxr", L"direct_input", 1, i); }
Item LongPressNeeds(Item i) { return Needs(L"direct", L"hold_enabled", 1, DirectNeeds(i)); }

// Order = layout order: a Tab starts a tab, a Group a group on it.
std::vector<Item> g_items = {
    Tab(L"Home"),
    // The Home page, for someone who opens it first (2026-10-14, the user's wish): START on top, then how you play and how the
    // picture gets into the headset, the view and the version in the middle, the game's and the system's state on the right.
    InColumn(0, Group(L"Start")),
    // Launch the game under D2RLoader (LaunchButton: greyed while the game runs, out while there is no D2RLoader.exe).
    Big(Button(L"@run", L"d2r_launch", L"START",
           L"Starts Diablo II under D2RLoader, the way the mod runs. In VR with native OpenXR and \"Controls without BodyWalk\" "
           L"nothing else is started; with BodyWalk and FlatVR it also starts BodyWalk, and FlatVR when Game Link is up.")),
    InColumn(0, Group(L"How you play")),
    // Flat is open for players since 2026-10-06 (greyed "coming soon" 2026-10-05 .. 10-06).
    Radio(L"mode", L"platform", 0, 1,
          {L"Flat: on the monitor, mouse and keyboard", L"VR: in the headset"},
          L"Native OpenXR (the default 3D choice below) needs no BodyWalk with \"Controls without BodyWalk\"; the FlatVR choices need BodyWalk with Game Link and FlatVR - the Status column says what is missing."),
    // Moved here from the Performance tab (2026-10-14, the user's wish): the way the picture gets into the headset and its rate.
    Only(kVr, InColumn(0, Group(L"In the headset"))),
    // The three ways kept in the list (2026-10-14, the user's decision): 0 native OpenXR (the default), 1 FlatVR stereo, 2 FlatVR 3D
    // from the depth. Stereo - eyes by turns, Mono and Native OpenXR - eyes by turns are left out of the list; their ini keys still work.
    Only(kVr, Choice(L"@stereo3d", L"source", L"3D in the headset", 0,
           {L"Native OpenXR (default)", L"FlatVR stereo", L"FlatVR 3D depth"},
           L"Native OpenXR (the default): the game is an OpenXR app and shows its own stereo pair in the headset, one pass two "
           L"pictures - each eye's real view and the head's place in the room, paced by the headset, no FlatVR - from the game's "
           L"first frame. First (F4) and third person (F2) in stereo, F1 and F3 in 3D too (Stereo tab, \"F1 and F3 in real 3D\"); menus and open panels as a flat picture where you look. "
           L"FlatVR is stopped while the game holds the headset, so BodyWalk's own panels are not in it; buttons and gestures need "
           L"BodyWalk's OpenXR layer and the profile's gesture source set to OpenXR (or \"Controls without BodyWalk\"). Switches "
           L"while the game runs. FlatVR stereo: the game draws each frame once and the right eye is drawn again from the same work "
           L"with its own camera - true 3D at nearly the mono frame rate; it needs a game restart to switch on the first time. "
           L"FlatVR 3D depth: one picture, FlatVR makes the second eye from ReShade's depth (works with DLSS). The FlatVR choices "
           L"set the Stereo tab's Real stereo and, while BodyWalk runs, FlatVR's 3D source (BodyWalk 1.76 or later).")),
    Only(kVr, Choice(L"stereo", L"headset_rate", L"Headset refresh rate", 3, {L"72 Hz", L"75 Hz", L"80 Hz", L"90 Hz", L"120 Hz"},
           L"Sets the game's frame cap (in its own options, Settings.json) - the line under it says what is written and why: the rate "
           L"itself for one picture a frame (mono, the 3D from the depth) and for both eyes from one game frame (one pass, two "
           L"pictures: FlatVR stereo); twice the rate for eyes by turns (one eye a game frame, each eye at the rate); none for native "
           L"OpenXR (the headset paces the game itself). More pictures than the headset shows make the frame shake. The game reads it "
           L"when it starts: set it with the game closed (it writes its own options on exit), or restart the game after. "
           L"VR only: in flat mode (How you play) the game runs at the frame cap you set in its own options - yours is kept before the "
           L"rate writes its own, and comes back when you switch to Flat or start the game in flat mode.")),
    Only(kVr, Status(L"frame_cap", L"")),
    // The render size next to the rate (2026-10-14, the user's wish: the resolution belongs with the headset's choices on Home), as a
    // SLIDER (2026-10-14, the user's wish): the square's side, 1024 - 4096 in steps of 64, [render] vr_resolution_px; the stop under
    // the smallest reads "window's size" (Desktop, 0). The tick box under it is the older mode 1, [render] vr_resolution: the headset's
    // own recommendation, the slider out of reach. An older ini's vr_resolution 2..6 still reads (shared/render_size.h); both are
    // the pseudo-section @render. The tips are rebuilt as things change (UpdateResolutionTexts).
    Only(kVr, Needs(L"@render", L"vr_headset", 0, Slider(L"@render", L"vr_px", L"Resolution (VR render size)",
           (float)rendersize::kStopWindow, (float)rendersize::kSideMax, (float)rendersize::kSideStep, (float)rendersize::kStopWindow, g_resTip))),
    Only(kVr, Toggle(L"@render", L"vr_headset", L"Use the headset's recommended size", 0, g_resTipHeadset)),
    Only(kVr, Status(L"vr_resolution", L"")),
    Only(kVr, Toggle(L"render", L"vr_resolution_ui", L"Menus and the mouse follow the VR render size", 1,
           L"With a VR render size apart from the window, the game still sized its interface and limited its mouse to the WINDOW: on a "
           L"window wider than the picture the menus lost their right edge, and on a window lower than the picture the mouse - the laser - "
           L"could not reach the bottom (the main menu's Play and Create Character buttons). On: the game's interface is laid out over the "
           L"whole picture (its toolbar at the picture's bottom, as with a window of the picture's shape) and the mouse reaches all of it. Live. Off: the window's size, as before. "
           L"(Found by bearstonem/d2r-pcvr.)")),
    Only(kVr, Slider(L"@bodywalk", L"userHeight", L"My height, m", 1.2f, 2.3f, 0.01f, 1.75f,
           L"Your height: the world is scaled to it (Stereo > Scale and depth > Life-size world). The same number as BodyWalk's "
           L"Mapping > Avatar > Body Height, changed in both places; with BodyWalk not running it is kept here. "
           L"Measure it with the T-pose on the Body tab if you want it exact.")),
    Only(kVr, Status(L"native_xr", L"Native OpenXR (the first 3D choice above) needs a BodyWalk that takes the game's headset (1.78 or later) or \"Controls without BodyWalk\"")),
#if D2RVR_FIRST_PERSON
    Only(kFlat, InColumn(1, Group(L"View (F1 - F3 in the game)"))),
    Radio(L"mode", L"flat_view", 1, 3,
          {L"F1  The game's own camera", L"F2  Third person", L"F3  First person: mouse look, W A S D"},
          L"Switch in the game with F1, F2, F3 (F12 goes round). The game does not get these keys: put its skills on others."),
    Only(kVr, InColumn(1, Group(L"View (F1 - F4 in the game)"))),
    Radio(L"mode", L"vr_mode", 1, 1,
          {L"F1  From above", L"F2  Third person", L"F3  The game on your floor: look down at it and walk round it (mixed reality)",
           L"F4  First person: the body follows yours (controllers)"},
          L"Switch in the game with F1 - F4 (F12 goes round). F3: the game lies on the floor of your room. "
          L"F4: arms on the controllers, the trunk held. The game does not get F1 - F4."),
#else
    Only(kFlat, InColumn(1, Group(L"View (F1, F2 in the game)"))),
    Soon(2, Radio(L"mode", L"flat_view", 1, 1,
          {L"F1  The game's own camera", L"F2  Third person", L"F3  First person - coming soon"},
          L"Switch in the game with F1, F2 (F12 goes round). The game does not get these keys: put its skills on others.")),
    Only(kVr, InColumn(1, Group(L"View (F1 - F3 in the game)"))),
    Soon(3, Radio(L"mode", L"vr_mode", 1, 1,
          {L"F1  From above", L"F2  Third person", L"F3  The game on your floor: look down at it and walk round it (mixed reality)",
           L"F4  First person: the body follows yours - coming soon"},
          L"Switch in the game with F1 - F3 (F12 goes round). F3: the game lies on the floor of your room. "
          L"The game does not get F1 - F4.")),
#endif
    // D2RLoader and ReShade: both ways to play (flat had no Install ReShade - a player found it in
    // VR only, 2026-10-11 - the whole Status group was VR's).
    InColumn(2, Group(L"The game and ReShade")),
    Status(L"d2rloader", L"D2RLoader"),
    // Download and install D2RLoader: shown while there is no D2RLoader.exe beside D2R.exe (D2RLoaderStatus).
    Hide(Button(L"@run", L"d2rloader_get", L"Download and install D2RLoader 1.3.1",
           L"Downloads D2RLoader 1.3.1 from d2rloader.net, checks it against the SHA-256 the site publishes and unpacks it "
           L"into the game's folder, beside D2R.exe. The mod runs only under D2RLoader.")),
    Status(L"reshade", L"ReShade and the mod's effect"),
    // Install ReShade: shown while there is no ReShade64.dll beside D2R.exe (RefreshStatus).
    Hide(Button(L"@run", L"reshade_get", L"Install ReShade",
           L"Downloads the current ReShade with add-on support from reshade.me, installs it for DirectX 10/11/12 and puts it "
           L"in the mod as mods\\D2R-VR\\reshade\\ReShade64.dll (renamed from dxgi.dll: the plain D2R.exe refuses a dxgi.dll), "
           L"which the mod loads itself. Shown only while there is no ReShade (no dxgi.dll next to D2R.exe either). "
           L"The fog, the sky, the ceiling and the depth for FlatVR need it.")),
    // A ReShade the player has as dxgi.dll is used as it is (shared/reshade_as.h): Setup and these buttons never rename, move or delete it. It needs D2R VR's two
    // search paths in its ReShade.ini to find the mod's effect and pictures (D2R_VR_Settings.exe --reshade-paths, run by Setup); the one explicit choice is
    // to put it aside (dxgi.dll.off) so that D2R VR's own ReShade can be installed. By the files there (RefreshStatus).
    Hide(Button(L"@run", L"reshade_paths", L"Add D2R VR's folders to my ReShade (its search paths)",
           L"The ReShade you have as dxgi.dll next to D2R.exe looks for effects and pictures in the game's folder, not in mods\\D2R-VR\\reshade where D2R VR's "
           L"effect (D2R_DepthFog.fx) and pictures are. This adds D2R VR's two folders to EffectSearchPaths and TextureSearchPaths in your ReShade.ini - "
           L"two list items at the end, nothing else of the file changes (a copy is kept as ReShade.ini.before_d2rvr; D2R VR's uninstaller takes them out "
           L"again). D2R VR Setup does this by itself; press it if you installed ReShade after D2R VR. The game must be closed. Takes effect at the next "
           L"start of the game.")),
    Hide(Button(L"@run", L"reshade_aside", L"Put my dxgi.dll aside (dxgi.dll.off) - D2R VR gets its own ReShade",
           L"Your ReShade stays as it is while it is dxgi.dll; D2R VR uses it. Only press this if you want D2R VR to have a ReShade of its own "
           L"(mods\\D2R-VR\\reshade) instead: dxgi.dll is renamed to dxgi.dll.off (nothing is deleted, nothing replaced), then Install ReShade is offered. "
           L"The game started without D2RLoader has no ReShade then. \"Bring my dxgi.dll back\" undoes it. Takes effect at the next start of the game.")),
    Hide(Button(L"@run", L"reshade_back", L"Bring my dxgi.dll back (dxgi.dll.off)",
           L"Renames dxgi.dll.off back to dxgi.dll - the ReShade you put aside. Takes effect at the next start of the game.")),
    Status(L"reshade_note", L"IMPORTANT"),
    Status(L"arcane_sky", L"Arcane Sanctuary sky: not made yet"),
    Button(L"@run", L"arcane_make", L"Make the Arcane Sanctuary sky now",
           L"Reads the star layers and clouds out of your game's own files (read only) and makes the pictures the mod draws the "
           L"Arcane Sanctuary's sky with (D2R_Arcane, in ReShade's reshade-shaders\\Textures: mods\\D2R-VR\\reshade for the mod), and "
           L"takes the game's flat star cards out of the level's files in mods\\D2R-VR\\D2R-VR.mpq (the game's own files stay as "
           L"they are). This program does it by itself when it opens; "
           L"press this after the game was updated or a picture was deleted. Nothing already right is written again. The game "
           L"may be running: it finds the pictures within 2 seconds."),
    Only(kVr, InColumn(2, Group(L"Status"))),   // the right column, under "The game and ReShade" (2026-10-14, the user's wish)
    Status(L"depth_addon", L"FlatVR depth add-on"),
    Status(L"game_video", L"The game's video settings"),
    Status(L"bw_installed", L"BodyWalk installed"),
    Status(L"vigem", L"Xbox controller driver (ViGEmBus)"),
    Status(L"bw_running", L"BodyWalk running"),
    Status(L"game_link", L"Game Link plugin"),
    Status(L"universal", L"Universal tracking output"),
    Status(L"bw_output", L"BodyWalk's Output"),
    Status(L"flatvr_on", L"FlatVR switched on"),
    Status(L"head_lock", L"Head Lock follows the game"),
    Status(L"frame_gen", L"Frame generation off"),
    Status(L"flatvr_running", L"FlatVR running"),
    Status(L"head", L"Head tracking reaches the game"),
    Status(L"bw_path", L"BodyWalk's path to the game"),
    Status(L"game", L"The game runs with the mod"),
    Only(kVr, Status(L"xr_stick", L"Thumbsticks (native OpenXR)")),   // did SteamVR bind them? (vr/xr_stick_check.h; the plugin's verdict is in the state block)
    Button(L"@update", L"check_now", L"Check for updates",
           L"Asks bodywalkvr.com now, even with the check at start switched off. Nothing is downloaded."),
    // The version and the update check (StartUpdateCheck). [update] is the settings program's
    // own: vrcam never reads it.
    InColumn(1, Group(L"D2R VR")),   // where it was: under the view, in the middle column
    Status(L"version", L"D2R VR " D2RVR_VERSION_W),
    Status(L"update", L"Update check is off"),
    Hide(Status(L"changelog", L"")),
    Hide(Button(L"@update", L"download", L"Download",
           L"Opens the download page in your browser. This program never downloads or runs anything itself.")),
    Button(L"@run", L"collect_logs", L"Collect logs for support",
           L"Makes one zip on your desktop with what we need to find a problem: the mod's, D2RLoader's, ReShade's and "
           L"FlatVR's logs, the settings, the versions, the graphics card and Windows. While the game runs it first "
           L"records 10 s of how the frames are drawn - keep playing and turn your head. Nothing is sent anywhere: "
           L"post the zip in our Discord (#diablo-2-vr)."),
    Status(L"collect", L"Collect logs: a zip on your desktop to post in our Discord"),
    Toggle(L"update", L"check", L"Check for updates when this program opens", 1,
           L"Asks bodywalkvr.com for the newest version of the mod. Nothing is downloaded."),
    Needs(L"update", L"check", 1, Toggle(L"update", L"develop", L"Beta versions too (develop channel)", 0,
           L"Also offers test builds before they are released to everyone.")),
    // Start BodyWalk from here, so nobody hunts for its shortcut: shown while a
    // BodyWalk is found and not running (RefreshStatus, BodyWalkExe).
    Only(kVr, Hide(Button(L"@run", L"bodywalk", L"Start BodyWalk",
           L"Starts BodyWalk - the one in the game's folder that D2R VR Setup put in, or your installed one."))),
    // FlatVR's own START / STOP, so nobody hunts for them in BodyWalk: shown
    // while BodyWalk runs with Game Link (its Host block moves), one at a time.
    Only(kVr, Hide(Button(L"@run", L"flatvr_start", L"Start FlatVR",
           L"Starts FlatVR in BodyWalk - the picture goes into the headset. The same as START FLATVR on BodyWalk's FlatVR tab."))),
    Only(kVr, Hide(Button(L"@run", L"flatvr_stop", L"Stop FlatVR",
           L"Stops FlatVR in BodyWalk. The same as STOP FLATVR on BodyWalk's FlatVR tab."))),

    Tab(L"Performance"),
    Group(L"Processor"),
    Toggle(L"render", L"vcache", L"Game on the cores with the big cache (Ryzen X3D)", 0,
           L"On a Ryzen with 3D V-Cache on one of its two core groups (7900X3D, 7950X3D, 9900X3D, 9950X3D) the game's threads "
           L"are asked to run on the cores with the big L3 cache, and its drawing thread gets a higher priority - D2R's frame is "
           L"bound by that one thread. Other processors: nothing changes. The log says which cores were taken. Live."),
    Group(L"Quality"),
    Choice(L"@preset", L"preset", L"Quality preset", 2, {L"Potato", L"Low", L"Medium", L"High", L"Custom"},
           L"Sets the settings below at once. Potato: as Low, but no ceiling, the 3D made from ReShade's depth instead of real stereo "
           L"(one frame a picture, not two) and the headset at 72 Hz, the game capped at it; the others 90 Hz. "
           L"Low: 1 room ring, models out to 150, near fog (30 to 100; in caves 60 to 100), ceiling on with coarse relief (8 steps). "
           L"Medium: 2 rings, 200, fog 30 to 200 (in caves 100 to 200), ceiling on. High: 3 rings, 400, fog 30 to 250, finer ceiling relief and torches lighting "
           L"the ceiling (heavy on the CPU). Every preset also sets the 3D in the headset: FlatVR stereo (Low, Medium, High) or FlatVR 3D depth "
           L"(Potato) - it switches Native OpenXR off (set it again on the Home page). With Native OpenXR on this shows Custom. "
           L"Changing any of them by hand shows Custom."),
    Slider(L"render", L"rings", L"Room rings around the hero", 1, 8, 1, 4, L"Extra room rings around the hero. With fog, 4-6 is enough."),
    Slider(L"render", L"model_radius", L"Model visibility radius", 150, 1000, 50, 200, L"The game's own is 150."),
    Toggle(L"fog", L"enabled", L"Distance fog", 0,
           L"Through ReShade (D2R_DepthFog.fx in reshade-shaders\\Shaders). With the sky on, it fades into the sky."),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"start", L"Fog start", 0, 1000, 5, 150,
           L"Where the fog begins. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"end", L"Full fog", 10, 2000, 10, 600,
           L"Where the fog is complete. World units (the hero is ~7.5 tall). Near fog hides what a short render distance leaves out.")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"caves_start", L"Fog start in caves", 0, 1000, 5, 150,
           L"Where the fog begins in caves, dungeons and crypts. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"caves_end", L"Full fog in caves", 10, 2000, 10, 600,
           L"Where the fog is complete in caves, dungeons and crypts. World units (the hero is ~7.5 tall).")),
    Toggle(L"sky", L"enabled", L"Sky outdoors (via ReShade)", 0,
           L"A painted sky over the black void outdoors. It needs ReShade's depth (below)."),
    Toggle(L"@reshade", L"generic_depth", L"ReShade depth map (Generic Depth)", 1,
           L"ReShade's Generic Depth add-on finds the game's depth buffer every frame. Off saves GPU time, but the fog, "
           L"the sky, the ceiling and the 3D from the depth need it. Applies when the game is started again."),
    Toggle(L"ceiling", L"enabled", L"Ceiling in caves (via ReShade)", 0,
           L"A stone vault over the black void in dungeons, first person only."),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"steps", L"Ceiling relief steps", 4, 32, 1, 12,
           L"Steps of the ray through the rock: more = finer relief, a dearer frame.")),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"torches", L"Torches light the ceiling (CPU heavy!)", 0,
           kTorchesTip)),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"fire_from_picture", L"Any fire lights the ceiling (ReShade, from the picture)", 0,
           kPictureFireTip)),
    Only(kVr, Slider(L"@game", L"Framerate Cap", L"Game frame cap, fps (0 = none)", 0, 300, 1, 180,
           L"The game's own frame cap (its Settings.json). Set from the headset's rate and the 3D choice (both on the Home page); can be changed by hand.")),
    Status(L"perf_restart", L"Need to restart the game: the render distance (room rings, models), the headset rate and the frame cap "
                            L"take effect when the game starts again."),

    Tab(L"Camera"),
    // One group per view (Home, F1 - F4 in VR, F1 - F3 flat), the shared ones first.
    Group(L"Every view"),
    Only(kVr, Toggle(L"camera", L"fov_from_flatvr", L"Field of view from the FlatVR screen", 1,
           L"The field of view follows the size and distance of the FlatVR screen in BodyWalk, like an OpenXR game's view.")),
    Needs(L"camera", L"fov_from_flatvr", 0, Slider(L"camera", L"fov", L"Field of view, °", 20, 120, 1, 60,
           L"Vertical field of view of the game camera, used when the box above is off.")),
    Slider(L"camera", L"pitch", L"Horizon (lower = looks higher)", -120, 30, 1, -60,
           L"Pitch on top of the game's own camera, degrees. -60 = level with the horizon."),
    Only(kVr, Toggle(L"head", L"yaw_from_head", L"Turn with the headset", 1, L"Needs BodyWalk with the Game Link plugin.")),
    Only(kVr, Needs(L"head", L"yaw_from_head", 1, Invert(L"head", L"yaw_sign", L"Reverse turning", -1, L"Tick if turning the head left turns the view right."))),
    Only(kVr, Toggle(L"head", L"pitch_from_head", L"Look up and down with the headset", 1)),
    Only(kVr, Needs(L"head", L"pitch_from_head", 1, Invert(L"head", L"pitch_sign", L"Reverse up and down", 1, L"Tick if looking up tilts the view down."))),
    Only(kVr, Toggle(L"head", L"roll_from_head", L"Roll with the headset", 1, L"Tilting the head sideways rolls the camera too.")),
    Only(kVr, Needs(L"head", L"roll_from_head", 1, Invert(L"head", L"roll_sign", L"Reverse roll", 1, L"Tick if tilting the head right tilts the picture the wrong way."))),
    Only(kVr, Group(L"F1  From above")),
    Toggle(L"stereo", L"top_down", L"Stereo from above", 0,
           L"The game's own view from above in stereo too (needs Real stereo on the Stereo tab): each eye turned about the hero by half the angle below."),
    Needs(L"stereo", L"top_down", 1, Slider(L"stereo", L"top_angle", L"Top-down depth, °", 0, 12, 0.1f, 3, L"Degrees between the eyes from above; more = deeper.")),
    Group(L"F2  Third person"),
    Slider(L"third", L"distance", L"Camera back", 0, 30, 0.5f, 7, L"How far back from the eye point. World units, about 0.24 m each."),
    Slider(L"third", L"height", L"Camera height", 0, 20, 0.1f, 6.5f, L"Eye point above the ground. World units, about 0.24 m each."),
#if D2RVR_FIRST_PERSON
    Group(L"F4  First person"),
    Slider(L"camera", L"near", L"Near clip (hides the hero's head)", 0, 5, 0.1f, 1.5f,
           L"Nothing closer than this is drawn: hides the hero's own head and hair. World units, about 0.24 m each."),
    Toggle(L"camera", L"height_auto", L"Eye height from the hero's class", 1,
           L"First person: the eyes at the hero's own height, taken from the model (the sorceress is the shortest, "
           L"the druid the tallest). The world's scale follows it."),
    Needs(L"camera", L"height_auto", 1, Slider(L"camera", L"eye_offset", L"Eye height correction", -2, 2, 0.05f, 0,
           L"Added to the hero's eye height. World units, about 0.24 m each.")),
    Needs(L"camera", L"height_auto", 0, Slider(L"camera", L"height", L"Fixed eye height", 0, 15, 0.1f, 8,
           L"Eye point above the ground under the hero, used when the class height is off. World units, about 0.24 m each.")),
    Slider(L"camera", L"distance", L"Camera ahead (-) / behind (+)", -5, 10, 0.1f, -1,
           L"Camera behind the eye point; below 0 = in front of the face."),
    Slider(L"camera", L"side", L"Eyes left (-) / right (+)", -3, 3, 0.05f, 0,
           L"The hero's head is off the model's centre line; without this the eye can sit on a shoulder."),
    Slider(L"camera", L"forward", L"Eyes ahead (+) along the ground", -3, 3, 0.05f, 0,
           L"Eye point ahead (+) / behind (-) along where you look, without tilting with the head."),
    Toggle(L"camera", L"neck_model", L"Head turns about the neck", 1,
           L"Like a real head: it turns about the top of the neck, below and behind the eyes. Level, the eyes sit in front of "
           L"the neck; looking down they move forward and down and see the chest, not the neck. Native OpenXR too, on top "
           L"of the headset's own place."),
    Needs(L"camera", L"neck_model", 1, Slider(L"camera", L"neck_up_cm", L"Eyes above the neck pivot, cm", 0, 25, 1, 10)),
    Needs(L"camera", L"neck_model", 1, Slider(L"camera", L"neck_forward_cm", L"Eyes in front of the neck pivot, cm", 0, 25, 1, 9)),
    Toggle(L"camera", L"follow_jump", L"Camera rises when the hero jumps", 1,
           L"The camera goes up when the animation lifts the hero (the barbarian's Leap); the held upper body goes with it."),
    Needs(L"camera", L"follow_jump", 1, Slider(L"camera", L"jump_from", L"Jump threshold (hip rise)", 0, 1.5f, 0.05f, 0.3f,
           L"Hip rise still taken for the stride's bob, not a jump. Above twice this the camera follows it whole.")),
#endif
    Only(kVr, Group(L"F3  The game on your floor")),
    Slider(L"table", L"hero_cm", L"Size: the hero's height on the floor, cm", 5, 200, 1, 40,
           L"F3 lays the game on the floor of your room and you look at it from above, walking round it. This is its size: "
           L"how tall the hero stands. The whole field grows and shrinks with him. About 180 = life size."),
    Button(L"table", L"place", L"Put the game in front of me (F11)",
           L"Puts the game down again in front of where you stand and look, at the distance below - as F11 in the game. "
           L"Stand up straight: the floor is found from your height in BodyWalk."),
    Slider(L"table", L"bounds", L"Diorama size (1 = as much as the game shows)", 0, 3, 0.05f, 1,
           L"The game on the floor ends where the game's own view from above would: 1 = the same ground as on the monitor, "
           L"more shows more round the hero, less a smaller board. 0 = no edge, the world as far as it is drawn. "
           L"A wall at the edge is kept whole: what counts is the ground under it."),
    Slider(L"table", L"turn", L"Turn the map, degrees", -180, 180, 1, 0,
           L"Turns the game world about the hero, the board stays where it is. The game draws its world at 45 degrees "
           L"to the screen: 45 or -45 puts its grid square to the board's edges."),
    Slider(L"table", L"height_m", L"Height above the floor, m", -1, 1.5f, 0.01f, 0,
           L"0 = the game lies on the floor. About 0.75 = on a table top. Below 0 lowers it, if it looks above the real floor."),
    Slider(L"table", L"ahead_m", L"Distance ahead, m", 0, 3, 0.05f, 1,
           L"Where the hero is put: this far in front of you when you press F3 or F11. Moving it moves the game at once, along where you looked then."),
    Color(L"table", L"background", L"Background round the game", L"black",
          L"What the void round the game is filled with: Virtual Desktop's passthrough cuts it by colour and shows "
          L"your room there - set its chroma key to the same colour. Black by default; a pure colour (green #00FF00, "
          L"blue #0000FF, magenta #FF00FF) if black cuts holes in the game. Empty (x) = black."),
    Slider(L"table", L"floor", L"Lift the game off black", 0, 0.3f, 0.01f, 0.05f,
           L"With a black background: the game's own dark parts are lifted this far off black, so the chroma key does not "
           L"cut them too. Raise it if shadows show the room, lower it if the picture looks washed out."),

#if D2RVR_FIRST_PERSON
    Tab(L"Body"),
    // The arms follow the controllers in VR view F4 and the game's animation in every other ([arms] mode is gone from here).
    Only(kVr, Group(L"Arms (F4: the body follows yours)")),
    Toggle(L"arms", L"hide_head", L"Hide the hero's head and neck", 0,
           L"Shrinks the hero's head and neck away, for a camera right at the eyes: looking down shows the chest. "
           L"Keep \"Camera ahead / behind\" at 0 then - moved ahead, the camera leaves the hands out of reach."),
    Needs(L"arms", L"mode", 2, Slider(L"arms", L"scale", L"Arm length (scale)", 0.3f, 3, 0.05f, 1, L"Arm reach on top of the hero-to-your-height ratio.")),
    Needs(L"arms", L"mode", 2, Slider(L"arms", L"up", L"Hands up (+) / down (-), cm", -40, 40, 1, 0)),
    Needs(L"arms", L"mode", 2, Slider(L"arms", L"forward", L"Hands forward (+) / back (-), cm", -40, 40, 1, 0)),
    Needs(L"arms", L"mode", 2, Slider(L"arms", L"side", L"Hands right (+) / left (-), cm", -40, 40, 1, 0)),
    Needs(L"arms", L"mode", 2, Slider(L"arms", L"follow_camera", L"Hands follow the camera shift, %", 0, 100, 5, 100,
           L"With \"Camera ahead / behind\" off 0: at 100% the hands hang off the camera and match your real ones, but a "
           L"camera moved ahead puts them out of the arms' reach. At 0% they hang off the hero's eyes: the body stays put, "
           L"the hands always reach, a little nearer than your real ones.")),
    Only(kVr, Group(L"Measured height and arm span (T-pose)")),
    Only(kVr, Toggle(L"calibration", L"use", L"Use the measured height and arm span", 0,
           L"The world's scale from your measured eye height, and the hero's arms fitted to your measured arm span (the hands stay "
           L"1:1 with the controllers), in place of \"My height\" / BodyWalk's Body Height and \"Arm length (scale)\". Unticked: "
           L"those, exactly as before. A good measurement ticks it. F4 only: the other views keep the old numbers.")),
    Only(kVr, Button(L"calibration", L"measure", L"Measure my height and arm span...",
           L"Native OpenXR only, with the game running and the headset on: a panel in front of you says what to do. Stand upright, "
           L"look straight ahead, stretch both arms out sideways at shoulder height, let go of both triggers, then squeeze both and "
           L"hold still for a second. Needs the room's floor set up in your headset. The game goes on behind the panel: measure "
           L"somewhere safe. F4 only.")),
    Only(kVr, Button(L"@run", L"calibration_forget", L"Forget the measurement",
           L"Clears the stored measurement: the height from BodyWalk / \"My height\" and \"Arm length (scale)\" apply again.")),
    // [onboarding] (vr/first_start.h): the guide in the headset at the first start in F4 - the T-pose measurement offered first, then what to press for what
    Only(kVr, Group(L"First-start guide (in the headset)")),
    Only(kVr, Toggle(L"onboarding", L"show", L"Show the first-start guide", 1,
           L"The first time you stand in the world in first person (F4) with Native OpenXR and Controls without BodyWalk, a panel in front of you "
           L"offers the T-pose measurement (when none is stored), then shows in five short pages what to press to open what: the inventory, the "
           L"map, the skill panel, the belt, the settings in the headset, the pause menu and the F views. It is the Guide page of the headset settings (the window "
           L"the long press of the right stick click opens): Next and Back with the laser, Done on the last page. It never presses anything and takes no button "
           L"of its own: the game runs on behind it. Finished or closed (closing the window closes it), it ticks \"First-start guide seen\". Unticked: it never "
           L"comes up by itself; the headset settings' Body page has a First-start guide button, and the Guide tab shows it, either way. Live.")),
    Needs(L"onboarding", L"show", 1, Only(kVr, Toggle(L"onboarding", L"done", L"First-start guide seen", 0,
           L"Ticked by the game when you finish or close the guide. Untick it to see the guide again the next time you stand in the world in F4 "
           L"(with the game running: in a few seconds). Not in the file at all (settings from before 0.171): seen if a measurement is stored, "
           L"not seen otherwise. Live."))),
    Group(L"Body"),
    Only(kVr, Toggle(L"body", L"lock", L"No game animation above the pelvis (F4)", 1,
           L"Nothing above the pelvis follows the game's animation: arms and hands come from the controllers, "
           L"the fingers keep the grip.")),
    Needs(L"body", L"lock", 1, Only(kVr, Toggle(L"body", L"legs_under_body", L"Legs stay under the body", 1,
           L"The hips and legs keep their place and direction under the body: a blow no longer lunges them out "
           L"ahead or twists them. They still step, crouch and jump. Off = the game's hips and legs as animated."))),
    Only(kVr, Choice(L"body", L"turn", L"Hero turns to where you look (F4)", 2, {L"No (the game's facing)", L"Above the pelvis", L"Whole body"},
           L"In the other first-person views the whole hero always turns with you.")),
    Toggle(L"body", L"turn_third", L"From behind too (the right stick turns him)", 1,
           L"In the view from behind the whole hero faces where the camera looks, so the right stick turns him. "
           L"Off: the right stick only swings the camera round him, and he faces where he last walked."),
    Slider(L"body", L"yaw", L"Body turn correction, °", -180, 180, 5, 0,
           L"Added to where the hero faces (the arms too) if he stands sideways: try 90, -90 or 180."),
    Only(kVr, Group(L"Hands (arms on VR controllers)")),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"wrist", L"Hands turn with the controllers", 1, L"Off = the hands only follow the forearm.")),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"wrist", 1, Slider(L"hands", L"pitch", L"Hand up / down, °", -180, 180, 5, 0, L"On top of the controller, in its own axes."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"wrist", 1, Slider(L"hands", L"yaw", L"Hand left / right, °", -180, 180, 5, 0, L"On top of the controller, in its own axes."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"wrist", 1, Slider(L"hands", L"roll", L"Hand roll, °", -180, 180, 5, 0,
           L"Turns the hand about the forearm; mirrored for the left hand."))),
    Only(kVr, Group(L"Effects around the hero (F4)")),
    Only(kVr, Toggle(L"render", L"dim_own_effects", L"Draw the hero's own effects fainter (not working yet)", 0,
           L"NOT WORKING YET (0.170): the log shows the effect's \"Fade\" being set, but nothing changes on screen. "
           L"Bone Armor wraps round the eyes of the first-person view and blinds. On: the effects that sit on the hero - the overlays of his own states (Bone Armor, Frozen / "
           L"Shiver / Chilling Armor, Energy Shield, Cyclone Armor, Blade Shield, Fade ...), each with its casting flash, and the aura rings at his feet - are faded to the percentages "
           L"below, with the effect's own \"Fade\" parameter (the one the game fades an overlay with when its unit fades; 0 % hides the effect). Off in the settings that come with the mod. Visual only: the buff and its numbers are the game's; monsters', mercenaries' and other players' effects, hits and "
           L"skills' casts are drawn as ever. Holy Shield is a colour on the shield, not an overlay: not covered. An effect already on when you tick this is dimmed when it is cast "
           L"again; the log says, for every effect, what was found and dimmed. First person (F4) only. Live.")),
    Needs(L"render", L"dim_own_effects", 1, Only(kVr, Slider(L"render", L"dim_own_effects_pct", L"Own effects left, %", 0, 100, 5, 30,
           L"The value the effect's Fade parameter gets: 100 = as the game draws them, 0 = hidden (the effect is switched off). Bone Armor and the armors. Live."))),
    Needs(L"render", L"dim_own_effects", 1, Only(kVr, Slider(L"render", L"dim_own_auras_pct", L"Aura rings at the feet left, %", 0, 100, 5, 50,
           L"The rings of the paladin and barbarian auras and the like, at the hero's feet. The value the effect's Fade parameter gets: 100 = as the game draws them, 0 = hidden. Live."))),

#endif
    Tab(L"Controls"),
    Group(L"Walking (left stick)"),
    Toggle(L"stick", L"rotate", L"Walk where I look", 1, L"The left stick is turned by the camera's yaw, so forward is forward in the view."),
    Toggle(L"stick", L"exact", L"Exact mode", 0,
           L"The stick goes through the real camera axes; the direction and diagonal settings below are then unused."),
    Needs(L"stick", L"exact", 0, Invert(L"stick", L"sign", L"Reverse walking direction", -1, L"Tick if after a head turn forward on the stick walks the wrong way.")),
    Needs(L"stick", L"exact", 0, Slider(L"stick", L"angle_scale", L"Turn strength", 0, 2, 0.01f, 1,
           L"Approximate mode: the stick is turned by the camera's yaw times this (0.5 = half).")),
    Needs(L"stick", L"exact", 0, Slider(L"stick", L"squash", L"Diagonal correction", 0.1f, 3, 0.01f, 1,
           L"Below 1 if diagonals drift after a turn (the isometric screen squash). 1 = off.")),
    Group(L"Turning (right stick)"),
    Toggle(L"turn", L"right_stick", L"Turn with the right stick", 1,
           L"Right stick X turns the body (the camera and walking); the game no longer gets the right stick."),
    // [turn] smooth is its own key (as in bearstonem/d2r-pcvr, MIT), so switching keeps the angle and the speed. An ini made before it says
    // snap=0 for smooth turning: "@turn" reads and writes the pair by that rule (ReadTurnValue / WriteTurnValue, shared/turn_mode.h).
    Needs(L"turn", L"right_stick", 1, Toggle(L"@turn", L"smooth", L"Smooth turning", 1,
           L"On: the right stick turns continuously, as fast as you push it up to the speed below. Off: one snap of the angle below "
           L"each time you flick it. Switching keeps both the speed and the angle.")),
    Needs(L"turn", L"right_stick", 1, Slider(L"turn", L"speed", L"Smooth turn speed, °/s", 10, 720, 10, 120, L"Degrees per second at full deflection; used while smooth turning is on.")),
    Needs(L"turn", L"right_stick", 1, Slider(L"@turn", L"snap", L"Snap turn angle, °", 15, 180, 15, 30, L"Degrees turned by one flick; used while smooth turning is off.")),
    Needs(L"turn", L"right_stick", 1, Invert(L"turn", L"sign", L"Reverse turning", 1, L"Tick if pushing right turns you left.")),
    // The thumbsticks' dead zones against stick drift (a player's wish, 2026-10-18): [direct] keys, read live by the plugin (vr/stick_shape.h, vr/direct_input.cpp)
    // whatever "Controls without BodyWalk" says - the VR controllers' sticks, native OpenXR and FlatVR's raw controllers alike. 0 % and 100 % = as before.
    Only(kVr, Group(L"Thumbsticks: dead zone (stick drift)")),
    Only(kVr, Slider(L"direct", L"stick_deadzone_left_pct", L"Left stick dead zone, %", 0, 50, 1, 0,
           L"A left stick that drifts (walks or moves the menu by itself, or the belt flick does not re-arm): raise this until it rests. "
           L"Inside this much of a full push the stick reads as centred; past it the push starts again from zero, so there is no jump, "
           L"and it is measured on the stick's length, so diagonals are cut no sooner than a straight push. 0 = none (as before). "
           L"The VR controllers only (native OpenXR, and FlatVR with its raw controllers), not a gamepad. Live.")),
    Only(kVr, Slider(L"direct", L"stick_outer_left_pct", L"Left stick full push at, %", 60, 100, 1, 100,
           L"A left stick that never quite reaches a full push: from this much of its throw on it counts as fully pushed (the walk runs, a "
           L"flick counts). 100 = only the rim (as before). Live.")),
    Only(kVr, Slider(L"direct", L"stick_deadzone_right_pct", L"Right stick dead zone, %", 0, 50, 1, 0,
           L"A right stick that drifts (the view turns by itself, or a snap turn or flick does not re-arm): raise this until it rests. "
           L"Inside this much of a full push the stick reads as centred; past it the push starts again from zero, so there is no jump. "
           L"0 = none (as before). The VR controllers only, not a gamepad. Live.")),
    Only(kVr, Slider(L"direct", L"stick_outer_right_pct", L"Right stick full push at, %", 60, 100, 1, 100,
           L"A right stick that never quite reaches a full push: from this much of its throw on it counts as fully pushed (the fastest smooth "
           L"turn, a snap). 100 = only the rim (as before). Live.")),
    Group(L"Mouse and keyboard (no headset)"),
    Only(kVr, Toggle(L"input", L"mouse_look", L"Mouse look in VR views F2 and F4 too", 0,
           L"Flat F2 and F3 always start with it, and VR F2 too while the switch below is on; never VR F3 (the game on the floor) "
           L"or F1 in real geometry / 3D. The mouse turns the view "
           L"(the pointer stays in the middle as a crosshair, clicks go where you look), W A S D walk. F9 switches it in "
           L"the game - off frees the pointer for menus.")),
    Slider(L"input", L"mouse_speed", L"Mouse speed, ° per pixel", 0.02f, 1, 0.01f, 0.15f),
    Only(kVr, Toggle(L"input", L"vr_keys_walk", L"VR view F2 like flat: mouse look, crosshair, W A S D", 1,
           L"Third person with the mouse and keyboard, as in flat mode: the mouse turns the camera, the pointer is the crosshair "
           L"in the middle (its depth: UI: F2 behind tab), a click goes there - on a monster an attack, on an item a pick-up, "
           L"on the ground a shot - and W A S D walk where the camera looks, not north on the game's map. F9 frees the pointer "
           L"for menus; W A S D keep walking. Needs the game in mouse mode: after a pad stick, click once.")),
    File(L"input", L"crosshair", L"Crosshair picture", L"D2R_Sky_ours\\D2R_Crosshair.png",
           L"The pointer while the mouse looks: your own picture (png, jpg, bmp), its middle is where you aim; it is "
           L"fitted into 32-64 pixels by the window's height. A picture with no transparency: its black is see-through. "
           L"A bare name is looked for in reshade-shaders\\Textures; a full path works too. Empty (x) = the built-in one."),
    Only(kVr, Group(L"Input")),
    Toggle(L"input", L"bodywalk_pad", L"Gamepad straight from BodyWalk", 1,
           L"The game's gamepad is BodyWalk's own report, read directly: no virtual gamepad (ViGEm) needed."),
    Only(kVr, Toggle(L"input", L"direct_walk", L"Walk straight from BodyWalk, not through the gamepad", 1,
           L"Views F2-F4: BodyWalk's stick moves the hero through the game's own walking code, turned by the camera, in "
           L"mouse mode and in controller mode alike; the gamepad's left stick is kept from the game outside menus (it "
           L"would walk twice and switch the interface to the controller). Off: the hero walks through the gamepad's stick.")),
    Choice(L"input", L"inventory_button", L"\"D2R: Inventory\" presses", 0, {L"Menu (Start)", L"View (Back)"},
           L"The pad button BodyWalk's \"D2R: Inventory\" action sends to the game. In D2R's pad layout View opens the map."),
    Toggle(L"input", L"a_attack_only", L"A button only attacks", 0,
           L"Picking up, opening and talking then go to the BodyWalk action \"D2R: Pick up / interact\"."),
    Only(kVr, Toggle(L"input", L"interact_one_press", L"Pick up / interact: one thing per press", 0,
           L"VR first person (F4): \"pick up / interact\" (the right A without BodyWalk, or BodyWalk's action) acts on what is under it "
           L"when you press it, and on nothing else: a chest opened by a press does not also give you its loot, a press held over a pile "
           L"takes one item - let go and press again for the next. A press on nothing does nothing, even if something comes under it while "
           L"held, and a press held while a menu opens or closes counts only after you let go. Off: held, it takes whatever is under it, "
           L"as before. F1, F2, F3: as before. Live. (After bearstonem/d2r-pcvr, by CalMac91.)")),
    Only(kVr, Group(L"Gestures (native OpenXR)")),
    Only(kVr, Toggle(L"hands", L"shoulder_swap", L"Swap weapon sets: the grip behind the shoulder", 0,
           L"Native OpenXR: reach a hand behind either shoulder and press its grip once - the game's own \"Swap weapons\" "
           L"(not while \"The shoulders hold your weapons\" is ticked on Weapon Weight: then that zone draws instead) "
           L"(weapon set I / II) is pressed, as its key would be, with all of the game's gates. The hand has to rest there "
           L"for a moment first; the grip pressed there is the swap's and takes no belt potion; reaching back without the grip "
           L"(a javelin's wind-up) does nothing. The game's weapon set says whether it took: a swap that did not is not "
           L"repeated by itself. Off: as before. With FlatVR, bind BodyWalk's \"D2R key: Swap weapons\" to a gesture behind "
           L"the shoulder instead. Live. F4 only.")),
    Needs(L"hands", L"shoulder_swap", 1, Choice(L"hands", L"shoulder_swap_hand", L"Swap weapon sets: hand", 0, {L"Right", L"Left"},
           L"The hand that reaches behind the shoulder and presses its grip. Live.")),
    Only(kVr, Needs(L"openxr", L"on", 1, Toggle(L"openxr", L"runes_draw", L"Draw gesture runes and trails in the headset", 0,
           L"Native OpenXR, first person (F4), with BodyWalk running. A gesture you draw in the air with a hand (a circle, a rune) "
           L"is shown the way a spell rune is: its picture hangs in the room in front of you and lights up as your hand draws it, "
           L"and the path of your hand is a glowing trail that ends in a bright blob at the hand. When the shape is complete the "
           L"picture flashes and fades (the gesture's own binding fires - that is set in BodyWalk, not here); a stray attempt just "
           L"fades. BodyWalk recognises the shape and decides what to show; this only draws it, in the headset, over the game. "
           L"Off, or in the other views (F1 - F3), or with no BodyWalk: nothing is drawn, the game as before. Live."))),
    // What each button of the controllers does (2026-10-16, players' wish: the pause menu, and rebinding without the ini). The same keys as
    // d2r_vr.ini [direct]; the rows are greyed unless the controls without BodyWalk are on (Stereo > Native OpenXR).
    Only(kVr, Group(L"Buttons of the controllers: right hand")),
    DirectNeeds(RemapRow(L"right_trigger", L"Right trigger")),
    DirectNeeds(RemapRow(L"right_a", L"Right A")),
    DirectNeeds(RemapRow(L"right_b", L"Right B")),
    DirectNeeds(RemapRow(L"right_stick", L"Right stick click")),
    DirectNeeds(RemapRow(L"right_menu", L"Right menu button (Vive, WMR; a Touch has none)")),
    DirectNeeds(RemapRow(L"right_grip", L"Right grip (the belt and two hands use it)")),
    Only(kVr, Group(L"Buttons of the controllers: left hand")),
    DirectNeeds(RemapRow(L"left_trigger", L"Left trigger")),
    DirectNeeds(RemapRow(L"left_x", L"Left X")),
    DirectNeeds(RemapRow(L"left_y", L"Left Y")),
    DirectNeeds(RemapRow(L"left_stick", L"Left stick click")),
    DirectNeeds(RemapRow(L"left_menu", L"Left menu button (PS VR2: the right Options)")),
    DirectNeeds(RemapRow(L"left_grip", L"Left grip (the belt and two hands use it)")),
    Only(kVr, Group(L"Buttons of the controllers: a long press")),
    Needs(L"direct", L"hold_enabled", 1, DirectNeeds(Toggle(L"direct", L"pause_button", L"Pause menu (Esc): long press of the left menu button (PS VR2: Options)", 1,
           L"With the controls without BodyWalk, first person (F4): hold the left menu button for half a second and the game gets Esc - its own "
           L"pause menu (or the open panels close, whatever Esc does at that moment). A short press is the inventory, as before (it opens when "
           L"you let go). On PS VR2 (SteamVR runs its controllers as Touch controllers) the \"left menu button\" is the OPTIONS button of the RIGHT controller: "
           L"SteamVR's own remapping gives a game that one and no Create button, so hold Options. The list below puts the pause menu on any other button "
           L"(\"Pause menu (Esc)\"). Unticked: the menu button opens the inventory the moment it is pressed, as it did before the pause button. "
           L"A Touch controller has no right menu button (that one is the system's). Needs the long presses (\"Long presses\" in "
           L"the Native OpenXR group). In d2r_vr.ini: pause_button, left_menu_hold = action:pause. The log (d2r-vr-vrcam.log) says \"the ... hand's menu button "
           L"pressed\" with the path and the headset for each press, and what happened to the Esc. Live. F4 only."))),
    LongPressNeeds(RemapRow(L"left_menu_hold", L"Left menu button (long press; PS VR2: the right Options)")),
    LongPressNeeds(RemapRow(L"left_stick_hold", L"Left stick click (long press)")),
    LongPressNeeds(RemapRow(L"right_stick_hold", L"Right stick click (long press)")),
    LongPressNeeds(RemapRow(L"right_menu_hold", L"Right menu button (long press)")),
    LongPressNeeds(RemapRow(L"left_x_hold", L"Left X (long press)")),
    LongPressNeeds(RemapRow(L"left_y_hold", L"Left Y (long press)")),
    LongPressNeeds(RemapRow(L"right_a_hold", L"Right A (long press)")),
    LongPressNeeds(RemapRow(L"right_b_hold", L"Right B (long press)")),
    Only(kVr, Group(L"Buttons of the controllers: with the grip held")),
    DirectNeeds(RemapRow(L"left_grip+left_menu", L"Left grip + menu button")),
    DirectNeeds(RemapRow(L"left_grip+left_stick", L"Left grip + stick click")),
    DirectNeeds(RemapRow(L"left_grip+left_x", L"Left grip + X")),
    DirectNeeds(RemapRow(L"left_grip+left_y", L"Left grip + Y")),
    DirectNeeds(RemapRow(L"right_grip+right_menu", L"Right grip + menu button")),
    DirectNeeds(RemapRow(L"right_grip+right_stick", L"Right grip + stick click")),
    DirectNeeds(RemapRow(L"right_grip+right_a", L"Right grip + A")),
    DirectNeeds(RemapRow(L"right_grip+right_b", L"Right grip + B")),
    Only(kVr, Group(L"Buttons of the controllers: the finger on a control (touch)")),
    DirectNeeds(RemapRow(L"right_trigger_touch", L"Right trigger (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"right_a_touch", L"Right A (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"right_b_touch", L"Right B (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"right_stick_touch", L"Right stick (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"left_trigger_touch", L"Left trigger (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"left_x_touch", L"Left X (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"left_y_touch", L"Left Y (touch)", kTouchTip)),
    DirectNeeds(RemapRow(L"left_stick_touch", L"Left stick (touch)", kTouchTip)),

    Only(kVr, Tab(L"Stereo")),
    // Native OpenXR (Performance > 3D in the headset > Native OpenXR): applied at once while the game runs.
    Group(L"Native OpenXR (the game itself in the headset)"),
    Slider(L"openxr", L"brightness", L"Brightness in the headset", 0.2f, 1.5f, 0.05f, 1.0f,
           L"Everything the game shows in the headset, darker or brighter: the picture, the flat picture, the toolbar and the "
           L"map. 1 = as the game draws it. The headset's runtime does it (SteamVR, Virtual Desktop...); one without it leaves "
           L"the picture as it is - vrcam's log says which."),
    Slider(L"openxr", L"flat_distance_m", L"Flat picture: distance, m", 0.5f, 10.0f, 0.1f, 2.0f,
           L"Menus and loading (F1 and F3 too, when \"F1 and F3 in real 3D\" is off) stand this far ahead, where you looked when they came up. "
           L"F1 in 3D is a window this far away. Farther = smaller in view. An open panel (inventory, trade...) has its own distance, below."),
    Slider(L"openxr", L"flat_width_m", L"Flat picture: width, m", 0.5f, 8.0f, 0.1f, 2.4f,
           L"How wide that picture is - and F1's window in 3D; its height follows the game window's shape."),
    Toggle(L"openxr", L"views_3d", L"F1 and F3 in real 3D (native OpenXR)", 1,
           L"Native OpenXR: F3 (the game on your floor) is the diorama standing in your room, seen with the headset's own eyes - walk "
           L"round it, lean over it, crouch; it lies on the headset's own floor when it has one (else your height decides), and the "
           L"world is small there, so the eyes' parallax follows. F1 (from above) is the game's view from above in true perspective "
           L"seen through a window as wide and as far as the flat picture (below), your hero on its middle: lean left or right to "
           L"look round things; F1 is in true perspective then, whatever \"Real geometry\" (UI: F1 above) says. F2 and F4 are in 3D as ever. "
           L"Menus, loading and the character screens stay the flat picture. Where it is put (\"Put the game in front of me\", "
           L"F11, the headset's own recentre) and the size of the diorama are in the F3 group on the Camera tab. Off: F1 and F3 as the "
           L"flat picture, as before. Switches while the game runs."),
    Needs(L"openxr", L"views_3d", 1, Toggle(L"openxr", L"pointer_3d", L"Show the game's mouse pointer in F1 and F3", 1,
           L"In F1 and F3 in 3D the game is played with the mouse, and the pointer it shows (the gauntlet, the sword over a monster...) used to "
           L"be nowhere in the headset - the scene stands in the room, there is no picture to show it on. With this on the same pointer is hung on "
           L"the game's ground where the game's mouse points: the ground point a click would walk to, a few centimetres large, turned to your eyes, "
           L"its tip on the point. Only a picture: the mouse, the clicks and the hero's movement are not touched. It is shown while the game shows its "
           L"pointer (it hides it in the controller layout) and the mouse is over the game's window. Off: no pointer in these views, as before. "
           L"Switches while the game runs.")),
    Slider(L"screen", L"menu_distance_m", L"Flat picture with a panel open (inventory, trade...): distance, m", 0, 8, 0.1f, 1.6f,
           L"While the inventory, a trader, the stash or another panel is open in a game, the flat picture moves this far "
           L"away - smoothly - and back when it closes. With a square window the game's interface is as wide as the picture: "
           L"farther, its left and right edges come into view. 0 = the distance above. The same setting as \"Screen distance "
           L"while a panel is open\" for FlatVR (UI: F4 body)."),
    Toggle(L"openxr", L"busy_guard", L"Never hang on a busy headset (native OpenXR)", 1,
           L"A runtime waits for a headset another app holds for good: FlatVR through BodyWalk Lite, then back to Native, and the game stood until Task Manager. On: the "
           L"runtime's two blocking calls (the frame wait and the session's start) run on a thread of their own, and the game waits for them a fraction of a second; if they "
           L"have not come the headset gets no picture that frame and the game goes on flat (the log says so, and when the headset answers). And no headset session is "
           L"started over a FlatVR that holds the headset and cannot be stopped (BodyWalk Lite has no Game Link): the Status column says to close BodyWalk and restart "
           L"SteamVR, and it is tried again while FlatVR runs. Needs a game restart. Off: as before. (d2r_vr.ini: wait_frame_ms.)"),
    Toggle(L"openxr", L"menus_wait_tracking", L"Menus placed only when the headset is tracked", 1,
           L"The flat picture (menus, loading, the title screen) and a menu's panel are put in the room only once the headset says your "
           L"head is tracked - before that a runtime may give a guessed place, and the picture stayed at the floor or behind you. Until "
           L"then the picture follows your gaze (a headset that never says it is tracked gets 3 seconds). And when the headset's focus "
           L"comes back after 2 seconds or more away (taken off, the runtime's own menu) they come up in front of you again - for a "
           L"player who sat down or stood up meanwhile. Live. Off: the first half second follows the gaze, then placed, as before."),
    Slider(L"openxr", L"lean_m", L"Head off the body, m", 0.0f, 0.5f, 0.01f, 0.1f,
           L"First and third person: how far the head may lean away from the hero's body. Farther, the body follows - "
           L"walking in the room never takes the camera out of the hero. 0 = turning only."),
    Toggle(L"openxr", L"roomscale_walk", L"Room-scale walking: your steps in the room walk the hero (F4)", 0,
           L"Native OpenXR, VR first person (F4) only. Walk in your room and the hero walks the same way: the plugin gives the game's own walking "
           L"stick - the one the controller's left stick uses - the direction and the speed of your steps, so the game's pathing, walls, walk / run "
           L"and speed stay the game's; the hero is never moved or put anywhere by the plugin. The view stays with your head the whole time and "
           L"the hero catches up under it. Turning your head, nodding or a small lean (the dead zone below) walks nothing. The stick asks the game "
           L"for your own walking speed (its walk speed follows the stick's tilt; the hero's speed at a full stick is learned as you play), so a "
           L"steady walk is one steady tilt and the hero walks with you - slow walks too while \"walk straight\" ([input] direct_walk, on by "
           L"default) is on; without it the gamepad's dead zone makes the game's slowest walk faster than a step in a room, and the hero walks and "
           L"waits. A brisk stride can reach a run when the game's Run / Run Lock is on. Turning is never done by it: the hero's facing stays as it is. "
           L"The controller's left stick wins while it is pushed (the view goes with the hero then, as always). When the hero is blocked (a wall, a "
           L"monster) and you walk on, the view leaves him by at most the limit below; past it the room slides with you, as \"Head off the body\" "
           L"does. A panel or the chat open: no walk, your place is kept. F11 or the headset's own recentre puts the hero under your head again. "
           L"Off: walking in the room does not move the hero (the body follows the head past \"Head off the body\", as before). FlatVR: nothing - "
           L"its camera has no head place to walk by. The log says \"room walk - live / holds / stands down\" and why. Live."),
    Needs(L"openxr", L"roomscale_walk", 1, Slider(L"openxr", L"roomscale_dead_zone_m", L"Room-scale walking: dead zone, m", 0.03f, 0.5f, 0.01f, 0.12f,
           L"How far the top of your neck may move off the hero's place before he walks: leaning and swaying inside it walk nothing. "
           L"Smaller: he follows every small step (and a lean); larger: only real steps walk him. Live.")),
    Needs(L"openxr", L"roomscale_walk", 1, Slider(L"openxr", L"roomscale_cap_m", L"Room-scale walking: the farthest the view leaves a blocked hero, m", 0.2f, 2.0f, 0.05f, 0.6f,
           L"When the hero cannot follow (a wall, a monster, a door) and you walk on, the view goes ahead of him at most this far; past it the room "
           L"slides with you - the view stays this far from him, without a jump. Step back and he walks under your head again. Live.")),
    Toggle(L"openxr", L"direct_input", L"Controls without BodyWalk (the plugin reads the controllers itself)", 0,
           L"Native OpenXR only. The plugin reads the sticks, triggers, grips and buttons of both controllers and gives the game "
           L"its own gamepad, actions and key commands - BodyWalk is not needed for the controls. While this is on, BodyWalk's "
           L"gamepad buttons, gestures and actions are ignored (it can stay on for FlatVR); its walking is the exception, see the box below. Layout: the left stick walks, the "
           L"right stick turns; the right trigger attacks, A picks up or opens, B / X / Y are the gamepad's skill buttons "
           L"(left trigger held: the second row), the right stick click swaps weapons, the left stick click shows items (Alt) and, as a quick click, the map, "
           L"the menu button opens the inventory (held for half a second: the game's pause menu, Esc; on PS VR2 the menu button is the Options button of the RIGHT controller); in a menu the controllers are the game's own gamepad (A B X Y, the grips "
           L"are the bumpers) and the laser works as before. Change any of it in the lists under Controls > Buttons of the controllers, or in d2r_vr.ini, section [direct]. "
           L"Off: BodyWalk's controls, as before. F4 only: in F1, F2, F3 and the flat modes BodyWalk's controls are the controls, as if this were off - "
           L"except the controllers in F1-F3 when BodyWalk is not running (the box \"Play with the controllers in F1, F2, F3 without BodyWalk\" below)."),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"flat_controller", L"Play with the controllers in F1, F2, F3 without BodyWalk", 1,
           L"VR views F1 (from above), F2 (third person) and F3 (the game on the floor) with no BodyWalk running: the plugin's own controllers are the "
           L"game's gamepad there - the same layout as first person (F4): the right trigger attacks, A picks up or opens, B / X / Y are the gamepad's skill "
           L"buttons (left trigger held: the second row), the right stick click swaps weapons, the left stick click shows items (Alt) and, as a quick click, "
           L"the map, the menu button opens the inventory (the pause menu's long press is F4's), the sticks are the gamepad's (the left one walks the hero, the left trigger + a flick of a stick "
           L"drinks a belt potion, see the boxes below); in a menu they are the game's own gamepad. Rebind in d2r_vr.ini, section [direct] (the same lines "
           L"as F4). What stays F4 only: gestures, hit by touch, the bow's draw, throws, the skill wheel and aiming by the hand (in F1 and F3 the hero "
           L"attacks where he faces). With BodyWalk running (its Game Link alive, or its gamepad moving) nothing changes: BodyWalk's gamepad is the "
           L"gamepad, whole. If a real gamepad answers as well, the controllers are added to it, never over it. Off: no buttons from the controllers in "
           L"F1-F3 without BodyWalk (the walk and the potions only). The log says \"the plugin's own controller is the game's pad\" when it takes over. Live.")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"flatvr_controls", L"FlatVR: the controllers by this layout (BodyWalk's Mapping not needed)", 1,
           L"In FlatVR the controllers work as in native OpenXR: BodyWalk hands the controllers to the game as they are (Game Link, BodyWalk with raw "
           L"controls - 1.83 with the D2R VR link), and the layout above - the buttons, the triggers, the sticks, rebindable in Controls > Buttons of the "
           L"controllers - and everything of first person (the belt, gestures, the bow, the quivers, the shoulders, the pause menu) runs on them. BodyWalk's "
           L"Mapping and its gamepad are not read at all then (Game Link direct: no double presses); in F1, F2, F3 the controllers act as without BodyWalk "
           L"(the box above). BodyWalk's walk comes through Game Link - its Output \"Game Link\" (BodyWalk's Engine: FlatVR + D2R VR) - into the "
           L"controllers' stick (the box \"BodyWalk's walking still walks the hero\"). The camera and the arms keep BodyWalk's head and hands, as before. "
           L"An older BodyWalk sends no raw controllers: its Mapping stays the controls (the log says so). Status says \"BodyWalk: Game Link direct\". "
           L"Off: BodyWalk's Mapping and gamepad in FlatVR, as before. Live.")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"hold_enabled", L"Long presses: stick click (right = skill panel, left = inventory), left menu button = pause menu", 1,
           L"With the controls above: hold the right stick's click for half a second and the skill panel opens (the wheel, when it is "
           L"set to open that way); hold the left stick's click and the inventory opens - or closes, the button is a toggle; hold the "
           L"left menu button (PS VR2: the right controller's Options) and the game gets Esc, its pause menu (the box \"Pause menu (Esc)\" in Controls > Buttons of the controllers). "
           L"A short click is what it was: the right stick swaps weapons (when you let go), the left one shows the items (Alt), the "
           L"menu button opens the inventory (when you let go). In d2r_vr.ini [direct]: hold_ms for the time, right_stick_hold, "
           L"left_stick_hold and left_menu_hold for what they do (any button can have a long press: Controls > Buttons of the controllers). "
           L"Off: a click acts the moment it is pressed, there are no long presses (and so no pause button, unless you put the "
           L"pause menu on a plain button in that list).")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"left_stick_map", L"Quick click of the left stick: show / hide the map", 1,
           L"With the controls above: a short click of the left thumbstick shows or hides the map (the same as the left grip with "
           L"the menu button). Held, the left click is still Alt (show items); held for half a second it is the inventory (see "
           L"above). Off: the left click is Alt only, as before. F4 only, like the rest of the controls. Live.")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"walk_flat_views", L"Walk with the left stick in F1, F2, F3 too (movement only)", 1,
           L"F1-F3: movement. With the controls above, the left thumbstick also walks the hero in the other VR views - from above (F1), "
           L"third person (F2) and the table (F3) - when BodyWalk is not running (the buttons are the box above; gestures, panels and vibration "
           L"stay F4 only). The walk is the game's own: the same walking code, dead zone and "
           L"stop BodyWalk's stick uses, the stick turned into the game's frame the way BodyWalk's stick is in that view (F1 from above: "
           L"forward is up on the screen). Panels and menus keep the stick for themselves. BodyWalk running (its Game Link alive): nothing "
           L"changes, its gamepad is the gamepad whole, as before. Off: F1-F3 have no walking without BodyWalk, as before. "
           L"The log (d2r-vr-vrcam.log) says \"walking in VR F1-F3 - ...\" and why when it does not walk. Live.")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"direct", L"belt_flick_flat_views", L"Belt potions in F1, F2, F3: left trigger + a flick of a stick", 1,
           L"F1-F3: potions. With the controls above, in the other VR views - from above (F1), third person (F2) and the table (F3) - "
           L"hold the LEFT TRIGGER and flick a thumbstick (either one): LEFT drinks belt column 1, UP column 2, RIGHT column 3, DOWN column 4 - the game's "
           L"own belt keys 1-4, the same press the hand makes when it drinks a potion from the belt. One flick, one potion: bring the stick back to the "
           L"middle before the next one; a stick that was already pushed when you pressed the trigger does nothing until it has been to the middle. "
           L"While the trigger is held the left stick does not walk (the hero stops, so a flick never walks him). The trigger alone does nothing. "
           L"Works only without BodyWalk (BodyWalk running: its own buttons, nothing changes). First person (F4) is not touched: there the trigger "
           L"and the sticks are the layout's. Off: no potions from the sticks in F1-F3. The log says \"belt flick - ...\" for each potion and why a flick was not taken. Live.")),
    Needs(L"openxr", L"direct_input", 1, Toggle(L"openxr", L"direct_bodywalk_walk", L"BodyWalk's walking still walks the hero (with the controllers' stick)", 1,
           L"With the controls above: BodyWalk walks by the motion of your body - steps in place, an arm swing, a lean - and turns it into a "
           L"walking stick. On: BodyWalk is still given your head and hands (never a button or a stick of the controllers: it presses nothing "
           L"a second time) and ITS LEFT STICK, nothing else of its gamepad, walks the hero through the game's own walking code, mixed into "
           L"the controller's left stick: each counts past the game's dead zone, together they add up (never past a full stick), and when they "
           L"pull in opposite ways the stronger one wins. Panels and menus keep the controller's stick for themselves. Off: BodyWalk's walking "
           L"is ignored with the rest of its input in direct mode, and BodyWalk is given no headset (as before). BodyWalk must be running and "
           L"write its gamepad (Input Source: Game Link). The log (d2r-vr-vrcam.log) says \"BodyWalk's walk - combined / ignored / dropped\" and why. "
           L"F4 only: in F1, F2, F3 BodyWalk's gamepad is the gamepad whole, as before. Live.")),
    Toggle(L"openxr", L"skill_wheel", L"Skill wheel: the controller's skill slots in the headset", 0,
           L"Native OpenXR only. A wheel of the controller's eight skill slots (the gamepad's B, X, Y, RB and the same four with "
           L"the left trigger held) in front of you. Open it: hold the grip of the hand that does not point (the left one with "
           L"the right laser hand) and click the pointing hand's thumbstick. Point a wedge with the laser and pull the trigger: "
           L"from then on that hand's trigger casts the slot - the game's own gamepad button, with its own animation, mana and "
           L"cooldown. The centre is the basic attack again. EDIT SKILLS -> a slot -> a skill assigns a learned skill to the "
           L"slot, through the game's own setter and checked back. Names are the game's own text; for icons run "
           L"tools/skill_icons/prepare_skill_icons.py once (it reads YOUR installed game; nothing of it ships), "
           L"without them the wheel shows names. Off: nothing of it exists."),
    Needs(L"openxr", L"skill_wheel", 1, Choice(L"openxr", L"skill_wheel_open", L"Skill panel opens by", 1, {L"Left grip + right stick click", L"Long press of the right stick"},
           L"How you open (and close) the skill panel. Long press of the right stick: hold the right thumbstick's click for about half a "
           L"second (a short click is still the weapon swap) - needs \"Controls without BodyWalk\"; the same long press closes it. "
           L"Left grip + right stick click: hold the grip of the hand that does not point and click the pointing hand's thumbstick. "
           L"With the long press chosen but BodyWalk's input in use, the grip chord still works. Live.")),
    Needs(L"openxr", L"skill_wheel", 1, Choice(L"openxr", L"skill_panel_style", L"Skill panel style", 0, {L"Wheel", L"Tiles"},
           L"How the skill panel looks. Wheel: a ring of eight wedges. Tiles: a window made like the game's own skill dialog - two rows of "
           L"six copper plates (A X B Y RB RT, and the same six with the left trigger held), red where a slot holds a skill, the pad "
           L"button's mark under each (and, with \"Controls without BodyWalk\" on, the controller button that presses it); EDIT SKILLS "
           L"shows every skill you have as a grid of plates. The same trigger cast, the same editor; only the look. Live.")),
    Needs(L"openxr", L"skill_wheel", 1, Slider(L"openxr", L"skill_wheel_distance_m", L"Skill panel: distance, m", 0.4f, 2.0f, 0.05f, 0.8f,
           L"How far in front of your head the wheel or the tiles hang when they open. Live.")),
    ShowFor(L"openxr", L"skill_panel_style", 1u << 0, Needs(L"openxr", L"skill_wheel", 1, Slider(L"openxr", L"skill_wheel_width_m", L"Skill wheel: width, m", 0.4f, 1.6f, 0.05f, 0.8f,
           L"How wide the wheel is (it is square). Live."))),
    ShowFor(L"openxr", L"skill_panel_style", 1u << 1, Needs(L"openxr", L"skill_wheel", 1, Slider(L"openxr", L"skill_tiles_width_m", L"Skill tiles: width, m", 0.5f, 1.6f, 0.05f, 0.9f,
           L"How wide the tiles' window is (its height follows). Live."))),
    Needs(L"openxr", L"skill_wheel", 1, Toggle(L"openxr", L"skill_categories", L"Skill editor: categories (All, the class's skill trees, General / Items)", 1,
           L"EDIT SKILLS -> a slot: a strip over the list of skills with the tabs All, your class's three skill trees (named as the game "
           L"names them, in your language) and General / Items (the town portal, the basic attack, a skill an item gives), each with its "
           L"count. Which tree a skill is on comes from the game's own tables, read once by tools/skill_icons/prepare_skill_icons.py "
           L"(run it again after updating the mod, an older icon cache has no trees: then there is only the one list). Off: the one "
           L"list of every skill, as before. Live. Idea after bearstonem/d2r-pcvr (MIT).")),
    Needs(L"openxr", L"skill_wheel", 1, Toggle(L"openxr", L"skill_run_walk", L"Skill panel: a RUN / WALK button", 0,
           L"A RUN / WALK button on the skill panel's first page (a tile beside the first row in Tiles, a button in the footer of the "
           L"Wheel). Pointing at it and pulling the trigger presses the game's own run / walk command, the one on the controller, and "
           L"the panel stays open; it shows which one the game is on now. Off (the default): no button - put the command on a button of "
           L"yours instead: Controls > Buttons of the controllers (and the headset's Left / Right pages) offer \"Run / walk switch\", "
           L"and BodyWalk's bindings offer \"D2R key: Run / walk switch\". Live. Idea after bearstonem/d2r-pcvr (MIT).")),
    Needs(L"openxr", L"skill_wheel", 1, Toggle(L"openxr", L"skill_rt_a", L"Skill panel: skills on the right trigger and on A", 0,
           L"In first person, with the controls driven by the mod (direct input): the right trigger casts the skill on the game's RT slot and the right A "
           L"the skill on its A slot - the game's own buttons, so its mana, cooldown and animation - and with the left trigger held, LT + RT and LT + A. Put a "
           L"skill on them in the panel's EDIT SKILLS (the Tiles look): point at the RT or A plate, pick a skill; pick \"Attack\" there to put the slot back. A slot with "
           L"nothing, Attack or Interact on it leaves its control as it is today (the trigger is the attack, A interacts). A slot you picked on the "
           L"panel's first page still owns the trigger. Hit by touch's \"the trigger no longer attacks\" withholds only the plain attack: a skill on RT casts. "
           L"Off (the default): the trigger and A are what the layout makes of them. Live.")),
    Toggle(L"openxr", L"symmetric_fov", L"Labels over monsters meet in both eyes", 1,
           L"Each eye drawn as wide to the left as to the right: the game's interface (the names over monsters and items) "
           L"is the same in both eyes and meets. Off: each eye's own view, a little fewer pixels, the names apart."),
    Toggle(L"openxr", L"fov_crop", L"Sharper: the picture keeps the window's shape", 1,
           L"Each eye's view cut to the game window's shape: square pixels, all the window's lines on a smaller angle, so the "
           L"picture is sharper; above and below it the headset shows black (nothing drawn there, it costs nothing). "
           L"Off: the picture stretched over the headset's whole view - taller, softer up and down."),
    // The eye distance (IPD) of native OpenXR's stereo pair: the runtime's own (automatic) or the player's, for a headset that did not pick it up.
    Toggle(L"openxr", L"ipd_auto", L"Eye distance: from the headset", 1,
           L"On (the default): the distance between the two eyes of the stereo picture is what the headset's runtime says (the position of each "
           L"eye's view). Off: the number of your own below - the two eyes keep their middle and their turns and stand that far apart. Use it "
           L"when the headset's was not picked up (a runtime that puts both eyes in one place gets 64 mm: the line below says what it reports) or "
           L"was picked up wrong: the world looks too small / too big, or two images do not merge. Your IPD is on the headset's wheel or in its "
           L"software too (the picture there is the headset's own); this is the distance the GAME's two cameras are drawn at. Native OpenXR only, "
           L"live; FlatVR has its own (Stereo > Scale and depth)."),
    Needs(L"openxr", L"ipd_auto", 0, Slider(L"openxr", L"ipd_mm", L"Eye distance, mm", 50.0f, 80.0f, 0.5f, 63.0f,
           L"The distance between your eyes in millimetres (about 63 for an adult; the headset's own is the line below). The game's two cameras are "
           L"drawn this far apart, about the middle of your head. Live.")),
    Status(L"ipd_measured", L""),
    // Headsets whose displays are canted (each eye's view turned outward: Pimax, Varjo, Index and others): the game's camera is one turn for both eyes.
    Toggle(L"openxr", L"canted_eyes", L"Canted displays: both eyes in the head's frame", 1,
           L"For headsets whose two displays are turned outward (Pimax, Varjo, Valve Index and some others). The game's camera is one view for "
           L"both eyes, but each eye used to be handed to the headset with its own turn, so on such a headset the eyes came out crossed and the "
           L"centre stretched (until a Steam menu was opened over the game). On: when the headset's two views differ by more than a quarter of a "
           L"degree both eyes are drawn and handed over in the head's frame, each with the frustum that covers its own view (the headset turns "
           L"them to its displays). Headsets with parallel displays are not touched. Off: as before. Live. The game's log says whether yours "
           L"are canted (\"each eye's own view from the head's frame\")."),
    Toggle(L"@window", L"square", L"Square game window (no black bars)", 0,
           L"In VR (native OpenXR or FlatVR): writes the game's own Settings.json, a window, as wide as it is tall (the size below). "
           L"The headset's eye is about square, so the whole picture fills it - no black bars with the box above ticked. vrcam keeps "
           L"the window square while the game runs (the game writes a cut size back into its file). The window you had is kept "
           L"first; in flat mode (Home > How you play) there is no square: the game uses the window and resolution you set in its "
           L"own options, and your window comes back at once. Off: the size it had before."),
    Slider(L"openxr", L"window_size", L"Square window: size, px", 640.0f, 4000.0f, 8.0f, 2000.0f,
           L"The square window's side, as far as a monitor holds it (past a monitor's edge the game itself closes): about 2060 "
           L"on a 4K screen. Bigger: make a virtual monitor that size in BodyWalk (Virtual Monitor, no need to make it primary) - "
           L"vrcam moves the game's window onto it by itself. Bigger is sharper and slower."),
    Status(L"square_window", L"The game window as the game has it"),
    Toggle(L"openxr", L"third_gaze", L"F2: the camera swings round the hero with the gaze", 0,
           L"Off: the camera stays behind the hero on his turn and the head looks round freely. On: it swings round him "
           L"with the head, as on FlatVR's screen."),
    Toggle(L"openxr", L"haptics", L"Controller vibration", 0,
           L"A short pulse in the hand when a blow of yours lands, a belt potion is taken or drunk, the laser clicks on a menu, "
           L"or you pick something up or open it. The game itself is not changed - the pulses only watch. Native OpenXR only "
           L"(the headset's runtime drives the motors); off: silent, as before. Live. F4 only."),
    Needs(L"openxr", L"haptics", 1, Slider(L"openxr", L"haptics_scale", L"Vibration: strength", 0.2f, 2.0f, 0.05f, 1.0f,
           L"All pulses stronger or weaker; 1 = the usual (a landed blow is about 70% of the motor's strength). Live.")),
    Toggle(L"openxr", L"frame_profile", L"Steam Frame controllers: their own layout", 1,
           L"Native OpenXR. When the runtime offers the Steam Frame's controllers as themselves (XR_VALVE_frame_controller_interaction), "
           L"they get their own buttons instead of the runtime's stand-in for a Quest or an Index controller: the right A and B as "
           L"before, the left hand's D-pad down and up are its first and second face buttons (X and Y on a Quest controller), the left "
           L"View and the right Menu buttons are the menu. Other controllers and runtimes without it: nothing changes. Taken when the "
           L"headset session starts. Off: the runtime's stand-in layout, as before."),
    Toggle(L"openxr", L"index_grip_force", L"Valve Index: the grip is how hard you squeeze", 0,
           L"Native OpenXR, Valve Index controllers. The Index reads its grip from the fingers round the handle, so a relaxed hand that "
           L"only holds the controller counts as a squeezed grip (a weapon or a potion taken, the laser's Shift). On: the grip is the "
           L"handle's force sensor - only a real squeeze counts. Other controllers: nothing changes. Live. Off: as before."),
    Needs(L"openxr", L"haptics", 1, Toggle(L"openxr", L"haptics_hand_actions", L"Vibration: a channel of its own for each hand", 1,
           L"Each controller's motor gets a vibration action of its own, so a pulse can only go to the hand it is for (some runtimes sent "
           L"a pulse of the shared action to the wrong hand). Taken when the headset session starts. Off: one action for both hands, "
           L"as before.")),
    Needs(L"openxr", L"haptics", 1, Toggle(L"openxr", L"haptics_stop", L"Vibration: stop each pulse on time", 1,
           L"Each pulse is stopped by its own call when its time is up, even while the game is loading or stalls - some controllers "
           L"(Steam Frame's) went on buzzing past a short pulse. Live. Off: the runtime ends a pulse by itself, as before.")),
    Group(L"Stereo 3D"),
    Toggle(L"stereo", L"afr", L"Real stereo (alternate frames)", 0,
           L"The game draws the left eye, the right eye, the left... Needs the FlatVR addon in the game, "
           L"FlatVR picture source = UEVR (full-size SBS), VSync off and a high frame cap (180 fps = 90 per eye)."),
    Needs(L"stereo", L"afr", 1, Toggle(L"stereo", L"pair_per_tick", L"Both eyes from one game frame", 0,
           L"The frame is drawn twice (left, then right with the game time held) instead of one eye per frame by turns. "
           L"Same pairs per second; the game itself updates half as often.")),
    Needs(L"stereo", L"pair_per_tick", 1, Slider(L"stereo", L"right_dt_ms", L"Right eye frame time, ms", 0, 2, 0.01f, 0.01f,
           L"The right eye's pass gets this much game time instead of none. Effects that skip a frame of no time "
           L"(butterflies only in the left eye) are drawn then; 0.01 is enough, more moves the world between the eyes.")),
    Needs(L"stereo", L"pair_per_tick", 1, Toggle(L"stereo", L"props_once_a_pair", L"Hanging lanterns move once a pair (F4)", 0,
           L"Props the game swings - hanging lanterns, chains, signs - are stepped in the left eye's pass only, and both eyes get "
           L"that pose. Where the right eye is drawn by a second pass (no replay, or before the replay starts in a new area) they "
           L"were stepped again with the tiny right-eye time and twitched. VR F4 only; the other views as before. Idea: "
           L"bearstonem/d2r-pcvr. Off: as before.")),
    Toggle(L"render", L"fx_fence", L"Effects: each eye's values exactly its own", 0,
           L"The effects (fog, sky, ceiling) wait until the graphics card has drawn the last eye's effects before the next "
           L"eye's values go in. Both eyes of a pair get the same values anyway; this matters for FlatVR's frame stamps "
           L"with one picture a frame. Costs a little speed. Live. F4 only."),
    Toggle(L"stereo", L"swap", L"Swap eyes", 0, L"Tick if the eyes come out swapped (depth looks inside out)."),
    Toggle(L"stereo", L"pace_flatvr_only", L"Hold frames to the headset only while FlatVR runs", 1,
           L"In FlatVR modes the plugin holds the game's pictures to the headset's rate so FlatVR gets them evenly. With FlatVR "
           L"(BodyWalk) not running nothing takes them: ticked, the game then runs as fast as it can. Unticked: held to the "
           L"headset's rate always, as before. Live."),
    Group(L"Scale and depth"),
    Toggle(L"stereo", L"true_scale", L"Life-size world", 1,
           L"Eye distance and the screen plane from the FlatVR screen and your height. "
           L"The two sliders below it are then unused."),
    Needs(L"stereo", L"true_scale", 1, Slider(L"stereo", L"eye_mm", L"My eye distance, mm", 45, 80, 0.5f, 63)),
    Needs(L"stereo", L"true_scale", 0, Slider(L"stereo", L"ipd", L"Eye separation", 0, 1, 0.01f, 0.27f,
           L"World units (the hero is ~7.5 tall, so 0.27 is about 6.4 cm). More = stronger 3D.")),
    Needs(L"stereo", L"true_scale", 0, Slider(L"stereo", L"convergence", L"Depth behind the screen", 0, 100, 0.5f, 15,
           L"The distance that sits on the FlatVR screen; farther goes deeper behind it. "
           L"0 = parallel eyes (everything in front of the screen). Smaller = deeper.")),
    Group(L"Timing"),
    Toggle(L"stereo", L"stamps", L"Frame stamps (FlatVR places each frame at its head pose)", 1,
           L"Each frame tells FlatVR when its view was taken, so the screen sits where the head was then. "
           L"Untick if tabletop or first person judders or doubles when you turn your head - FlatVR then keeps "
           L"the screen at the head as it is, like in the other views. Takes effect at once."),
    Toggle(L"stereo", L"picture_ring", L"Newest finished picture (FlatVR frame ring)", 0,
           L"FlatVR takes the newest picture the game has finished instead of the one shared picture. "
           L"Off by default: with it head turns in tabletop and first person juddered (FlatVR addon 2.19 and newer). "
           L"Takes effect within a second."),
    Needs(L"stereo", L"stamps", 1, Slider(L"stereo", L"pipeline_depth", L"Pipeline depth (smoother head turns)", 0, 3, 1, 0,
           L"How many of an eye's views the shown frame lags the newest one. Try 0, 1 and 2 for the smoothest head turn.")),
    Group(L"FlatVR like native OpenXR"),
    Choice(L"flatvr", L"frame_sync", L"FlatVR: pictures on FlatVR's frames", 0,
           {L"Off - our own timer (as before)", L"On - every other frame when a pair does not fit (even)", L"On - every frame FlatVR has a head for"},
           L"FlatVR only (no native session). Each picture starts on FlatVR's next frame, with the head FlatVR hands out then - as "
           L"native OpenXR starts each pair on the headset's own wait. Before, the game ran on a timer of ours and its own frame cap "
           L"while FlatVR showed at the headset's rate: the pictures came 1, 2, 1, 1, 2 times each and walking, the hands and animations "
           L"jerked. A pair that takes longer than a headset frame goes on every other frame (an even 45 of 90, as a runtime halves a "
           L"game that misses) - or, with the third choice, on every new head (more pictures, unevenly). The game's own frame cap is "
           L"written as twice the rate then (only a stop for a game running away). No head from FlatVR: our timer as before. Live."),
    ShowFor(L"flatvr", L"frame_sync", (1u << 1) | (1u << 2), Slider(L"flatvr", L"frame_sync_late", L"FlatVR frames: a head is late after, share of a frame", 0.1f, 1.0f, 0.05f, 0.5f,
           L"A head from FlatVR older than this share of a frame when the last pair ends is let go and the next one waited for. Live.")),
    Toggle(L"flatvr", L"head_level", L"FlatVR: the horizon is the real horizon", 0,
           L"FlatVR, F4 only. The camera's pitch and roll are the headset's own against gravity, as in native OpenXR. Before, the "
           L"camera's pitch was the game's camera plus [camera] pitch plus the head's pitch since the last F11: any amount apart "
           L"from the head turned into a lean of the world on every head turn (FlatVR hangs the picture at the head's full pose), "
           L"and BodyWalk's roll came out too small when looking up or down. Off: as before. Live."),
    Toggle(L"flatvr", L"head_position", L"FlatVR: the camera moves with your head", 0,
           L"FlatVR, F4 only. Leaning, crouching, a step aside move the camera as far in the game (at the hero's size), from where "
           L"the head was at the last recentre (F11) - as native OpenXR does. Off: turns only, and the world moves along with the "
           L"head when it leans. Live."),
    Toggle(L"flatvr", L"room_centre", L"FlatVR: one centre with BodyWalk", 0,
           L"FlatVR. Straight ahead is taken from the room FlatVR places its screen in, at our recentre (F11). BodyWalk's own "
           L"Reset Center (its button or key) no longer turns the world away from FlatVR's screen: it recentres the game too. "
           L"Off: as before (after BodyWalk's reset an F11 was needed). Live."),
    Toggle(L"flatvr", L"recenter_on_enter", L"FlatVR: straight ahead when a game starts", 0,
           L"FlatVR. When the hero appears (a game entered), straight ahead is where you look then - as native OpenXR does. "
           L"Off: straight ahead stays where it was (where the headset looked when BodyWalk started, or the last F11)."),

    // The interface, one tab per view (F1 - F4 on the Home page). [hud_top],
    // [hud_third], [hud_floor] and [hud_inside] are that view's own; unset, the old shared
    // [hud] value is read (vrcam does the same). Full body keeps [hud].
    Only(kVr, Tab(L"UI: F1 above")),
    Group(L"View from above"),
    Toggle(L"top", L"perspective", L"Real geometry", 0,
           L"The view from above through our own camera: the same picture, but in true perspective - the field of view of "
           L"the FlatVR screen and real stereo (Real stereo on the Stereo tab), the hero on the screen's plane. Off = the "
           L"game's own flat picture."),
    Needs(L"top", L"perspective", 1, Slider(L"top", L"tilt", L"Real geometry: camera tilt, °", -40, 60, 1, 0,
           L"The camera turned about the hero: more = nearer the horizon, more of the world ahead and the sky past it.")),
    Group(L"Toolbar and map, from above"),
    Choice(L"hud_top", L"classic", L"Toolbar and map", 0, {L"In the picture", L"Hung in the room (as F4)"},
           L"In the picture: the map as the game draws it, the toolbar at the bottom of the screen at the depth below. "
           L"Hung in the room: where the F4 tab puts them (forearm, hand)."),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud_top", L"bar_size", L"Toolbar size, %", 30, 200, 1, 100,
           L"The toolbar in the picture, smaller or larger, about the middle of its bottom edge (then moved by the sliders below).")),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud_top", L"bar_near", L"Toolbar nearer / farther", -10, 10, 0.1f, 0,
           L"Real stereo only: + brings the toolbar towards the eyes, - pushes it behind the screen; 0 = where the game "
           L"puts it. Needs D2R_DepthFog.fx on in ReShade (it draws it back); without it the game's own toolbar stays.")),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud_top", L"bar_x", L"Toolbar left / right, %", -50, 50, 1, 0,
           L"The toolbar moved across the screen, % of its width: + right, - left; 0 = the middle, where the game puts it.")),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud_top", L"bar_y", L"Toolbar up / down, %", -30, 100, 1, 0,
           L"The toolbar moved up the screen, % of its height: + up, - down (partly off the screen); 0 = the bottom, "
           L"where the game puts it.")),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud", L"labels_tilt", L"Labels tilt", -20, 20, 0.1f, 0,
           L"In stereo: the names over monsters lie on a plane tilted like the ground - the top of the "
           L"screen stays as it is, + brings the bottom (the near monsters) out, more the lower. 0 and depth 0 = as the game draws them.")),
    Needs(L"hud_top", L"classic", 0, Slider(L"hud", L"labels_near", L"Labels nearer / farther", -10, 10, 0.1f, 0,
           L"The whole plane of the names nearer (+) or farther (-); the toolbar and the map stay as they are.")),
    Slider(L"hud_top", L"pointer_top", L"Mouse pointer depth: top of the screen", -30, 30, 0.1f, 0,
           L"In stereo the mouse pointer sat at the screen's own depth and came apart over the ground. Set where it sits "
           L"with the pointer at the top, the middle and the bottom of the screen: + nearer, - farther; in between it "
           L"follows a smooth curve. Needs BodyWalk 1.74 or newer."),
    Slider(L"hud_top", L"pointer_mid", L"Mouse pointer depth: middle", -30, 30, 0.1f, 0),
    Slider(L"hud_top", L"pointer_bottom", L"Mouse pointer depth: bottom of the screen", -30, 30, 0.1f, 0),
    Slider(L"hud_top", L"pointer_top_size", L"Mouse pointer size at the top, %", 10, 150, 1, 100,
           L"The pointer smaller the higher it is on the screen - far off, as what it points at is: this size at the "
           L"top, its own at the bottom, a straight line between. 100 = its own size everywhere."),
    Group(L"Every view"),
    Needs(L"stereo", L"afr", 1, Slider(L"stereo", L"ui_near", L"Interface nearer (0 = on the screen)", 0, 10, 0.1f, 0,
           L"Real stereo only: the HUD is moved apart per eye by the shader, % of the half screen.")),
    Slider(L"stereo", L"ui_top", L"Interface top edge", 0.4f, 1, 0.01f, 0.7f,
           L"The HUD is looked for below this height: 0 = top of the screen, 1 = bottom."),
    Group(L"Monster plates"),
    Toggle(L"hud", L"ordinary_mob_bars", L"Hide the plates of ordinary monsters (prototype)", 0, kOrdinaryBarsTip),

    Group(L"Item labels on the ground"),
    Hide(Slider(L"hud", L"monster_alpha", L"Name over a monster: opacity", 0, 1, 0.05f, 1,
           L"The name plate the game shows over the monster under the cursor (its MonsterHealth panel), box and name "
           L"alike: 1 = as the game draws it, 0 = gone. One setting for every view. Needs D2R_DepthFog.fx on in ReShade.")),   // hidden: not working yet
    Slider(L"hud_top", L"labels_alpha", L"Item labels: opacity", 0, 1, 0.05f, 1,
           L"How much of the dark box behind the names of items on the ground is drawn in the view from above (F1): 1 = as the game draws "
           L"it, 0 = no box, the names alone. The names stay bright. Through the game's label code (vrcam's log says "
           L"\"item-label hook in\"); on another game build it does nothing here."),
    Slider(L"hud_top", L"labels_size", L"Item labels: size, %", 20, 150, 5, 100,
           L"The names of items on the ground and their boxes in the view from above (F1), smaller or larger, each about the point under it. "
           L"100 = the game's own. Item tooltips and all other text keep their size."),

    Only(kVr, Tab(L"UI: F2 behind")),
    Group(L"Toolbar and map, from behind"),
    Choice(L"hud_third", L"classic", L"Toolbar and map", 0, {L"In the picture", L"Hung in the room (as F4)"},
           L"In the picture: the map as the game draws it, the toolbar at the bottom of the screen at the depth below. "
           L"Hung in the room: where the F4 tab puts them (forearm, hand)."),
    Needs(L"hud_third", L"classic", 0, Slider(L"hud_third", L"bar_size", L"Toolbar size, %", 30, 200, 1, 100,
           L"The toolbar in the picture, smaller or larger, about the middle of its bottom edge (then moved by the sliders below).")),
    Needs(L"hud_third", L"classic", 0, Slider(L"hud_third", L"bar_near", L"Toolbar nearer / farther", -10, 10, 0.1f, 0,
           L"Real stereo only: + brings the toolbar towards the eyes, - pushes it behind the screen; 0 = where the game puts it.")),
    Needs(L"hud_third", L"classic", 0, Slider(L"hud_third", L"bar_x", L"Toolbar left / right, %", -50, 50, 1, 0,
           L"The toolbar moved across the screen, % of its width: + right, - left; 0 = the middle, where the game puts it.")),
    Needs(L"hud_third", L"classic", 0, Slider(L"hud_third", L"bar_y", L"Toolbar up / down, %", -30, 100, 1, 0,
           L"The toolbar moved up the screen, % of its height: + up, - down (partly off the screen); 0 = the bottom, "
           L"where the game puts it.")),
    Group(L"Crosshair (mouse look)"),
    Toggle(L"hud_third", L"crosshair", L"Enable crosshair", 1,
           L"With mouse look in F2 the pointer is a crosshair in the middle of the screen. Off: no pointer in the headset "
           L"at all - a click still goes to the middle."),
    Needs(L"hud_third", L"crosshair", 1, Slider(L"hud_third", L"crosshair_size", L"Crosshair size, %", 25, 400, 5, 100,
           L"100 = the size the game window's height picks (32, 48 or 64 pixels); the picture is the one on the Controls tab.")),
    Needs(L"hud_third", L"crosshair", 1, Slider(L"hud_third", L"crosshair_depth", L"Crosshair depth, looking ahead", -30, 30, 0.1f, 0,
           L"With mouse look in F2 the pointer is a crosshair in the middle of the screen: + brings it nearer, - pushes it "
           L"farther, 0 = on the screen. Set it where the ground ahead is with the camera level, so it does not come apart "
           L"in stereo. Real stereo only, as the mouse pointer's depth in F1.")),
    Needs(L"hud_third", L"crosshair", 1, Slider(L"hud_third", L"crosshair_depth_down", L"Crosshair depth, looking down", -30, 30, 0.1f, 0,
           L"The same with the camera looking straight down at the hero, where the ground is much nearer; in between it "
           L"goes from one to the other as the view tips down.")),

    // VR F3, the game on the floor: [hud_floor]. Until 2026-10-05 this view used
    // F2's [hud_third] (vrcam runs it as view 2); its screen is head-locked, so
    // the toolbar stays in view wherever you look - hence size, depth and place.
    Group(L"Item labels on the ground"),
    Hide(Slider(L"hud", L"monster_alpha", L"Name over a monster: opacity", 0, 1, 0.05f, 1,
           L"The name plate the game shows over the monster under the cursor (its MonsterHealth panel), box and name "
           L"alike: 1 = as the game draws it, 0 = gone. One setting for every view. Needs D2R_DepthFog.fx on in ReShade.")),   // hidden: not working yet
    Slider(L"hud_third", L"labels_alpha", L"Item labels: opacity", 0, 1, 0.05f, 1,
           L"How much of the dark box behind the names of items on the ground is drawn in third person (F2): 1 = as the game draws "
           L"it, 0 = no box, the names alone. The names stay bright. Through the game's label code (vrcam's log says "
           L"\"item-label hook in\"); on another game build it does nothing here."),
    Slider(L"hud_third", L"labels_size", L"Item labels: size, %", 20, 150, 5, 100,
           L"The names of items on the ground and their boxes in third person (F2), smaller or larger, each about the point under it. "
           L"100 = the game's own. Item tooltips and all other text keep their size."),

    Only(kVr, Tab(L"UI: F3 floor")),
    Group(L"Toolbar, the game on your floor"),
    Slider(L"hud_floor", L"bar_size", L"Toolbar size, %", 20, 200, 1, 100,
           L"The F3 screen moves with your head, so the toolbar stays in view wherever you look: make it smaller to keep it "
           L"out of the way. It grows or shrinks about the middle of its bottom edge. Needs D2R_DepthFog.fx on in ReShade "
           L"(it draws the toolbar back); without it the game's own toolbar stays."),
    Slider(L"hud_floor", L"bar_near", L"Toolbar nearer / farther", -10, 10, 0.1f, 0,
           L"Real stereo only: + brings the toolbar towards the eyes, - pushes it behind the screen; 0 = where the game "
           L"puts it. Set it where looking at it is comfortable."),
    Slider(L"hud_floor", L"bar_x", L"Toolbar left / right, %", -50, 50, 1, 0,
           L"The toolbar moved across the screen, % of its width: + right, - left; 0 = the middle, where the game puts it."),
    Slider(L"hud_floor", L"bar_y", L"Toolbar up / down, %", -30, 100, 1, 0,
           L"The toolbar moved up the screen, % of its height: + up, - down (partly off the screen); 0 = the bottom, "
           L"where the game puts it."),
    Group(L"Map, the game on your floor"),
    Slider(L"hud_floor", L"map_size", L"Map size, %", 30, 300, 1, 100,
           L"The corner map (Tab) smaller or larger, about its own corner. Needs D2R_DepthFog.fx on in ReShade (it draws "
           L"the map back); without it the game's own map stays. The mouse pointer lies on the game's ground by itself here."),
    Slider(L"hud_floor", L"map_near", L"Map nearer / farther", -10, 10, 0.1f, 0,
           L"Real stereo only: + brings the map towards the eyes, - pushes it behind the screen; 0 = where the game puts it."),
    Slider(L"hud_floor", L"map_x", L"Map left / right, %", -50, 50, 1, 0,
           L"The map moved across the screen, % of the screen's width: + right, - left; 0 = its corner, where the game puts it."),
    Slider(L"hud_floor", L"map_y", L"Map up / down, %", -100, 30, 1, 0,
           L"The map moved up the screen, % of the screen's height: + up (partly off the screen), - down; 0 = the top, "
           L"where the game puts it."),
    Group(L"Item labels on the ground"),
    Hide(Slider(L"hud", L"monster_alpha", L"Name over a monster: opacity", 0, 1, 0.05f, 1,
           L"The name plate the game shows over the monster under the cursor (its MonsterHealth panel), box and name "
           L"alike: 1 = as the game draws it, 0 = gone. One setting for every view. Needs D2R_DepthFog.fx on in ReShade.")),   // hidden: not working yet
    Slider(L"hud_floor", L"labels_alpha", L"Item labels: opacity", 0, 1, 0.05f, 0.5f,
           L"How much of the dark box behind the names of items on the ground is drawn: 1 = as the game draws it, "
           L"0 = no box, the names alone. The names themselves stay bright. Should the game's label code not be found "
           L"(another game build), the labels are faded as a picture by D2R_DepthFog.fx instead: the boxes fade, the "
           L"names stay. Only on the floor (F3)."),
    Slider(L"hud_floor", L"labels_size", L"Item labels: size, %", 20, 150, 5, 100,
           L"The names of items on the ground and their boxes, smaller or larger, each about the point under it; "
           L"the game makes room for them at that size. 100 = the game's own. Through the game's label code: on "
           L"another game build it does nothing (vrcam's log says so). Only on the floor (F3); item tooltips and every "
           L"other text keep their size."),

#if D2RVR_FIRST_PERSON
    Only(kFlat, Tab(L"UI: F3 eyes")),
    Group(L"First person with mouse and keyboard"),
    Slider(L"hud_inside", L"map_zoom", L"Corner map size", 0.3f, 3, 0.05f, 1,
           L"The corner map in the picture this much bigger (1 = as the game draws it), grown from its own corner. "
           L"For it top left, set the game's mini map to the left in its options. Needs D2R_DepthFog.fx on in ReShade."),
    Group(L"Monster plates"),
    Toggle(L"hud", L"ordinary_mob_bars", L"Hide the plates of ordinary monsters (prototype)", 0, kOrdinaryBarsTip),

    Only(kVr, Tab(L"UI: F4 body")),
    Group(L"Menus"),
    Toggle(L"openxr", L"laser", L"Laser pointer on menus and panels (prototype)", 0,
           L"Point a controller at the flat picture of the menus, the inventory, the stash or a trader and it is the mouse: "
           L"the trigger clicks, with the grip held it right-clicks, a held trigger drags. The other hand's trigger is Ctrl "
           L"(an item to the stash or the trader), its grip Shift. While the laser points at the picture its triggers and "
           L"grips are not BodyWalk's; a stick or A / B gives the panel back to the pad until you point somewhere else. "
           L"The game stays in its controller layout. Not on F1 and F3."),
    Needs(L"openxr", L"laser", 1, Choice(L"openxr", L"laser_hand", L"Laser hand", 1, {L"Left", L"Right"},
           L"The hand the laser comes from; the other one's trigger and grip are Ctrl and Shift.")),
    Needs(L"openxr", L"laser", 1, Toggle(L"openxr", L"laser_tooltips", L"Item tooltips at once under the laser", 0,
           L"Pointing the laser at an item shows the game's own tooltip at once, with the comparison against the item you "
           L"wear beside it - no 1000 ms wait for the tooltip and no Shift held for the comparison. It is the game's tooltip "
           L"and the game's comparison, only earlier; Shift still buys stacks and moves items as before. Off: the game's "
           L"delay, and Shift for the comparison. F4 only.")),
    Toggle(L"openxr", L"menu_3d", L"Menus as a panel, the world stays in 3D", 1,
           L"First person (F4), native OpenXR or FlatVR: opening the inventory, the stash, a trader, the skills or the game "
           L"menu keeps the world in stereo around you, and the game's interface hangs in front of you as a panel of its own - "
           L"where the flat picture stood (its width, the panels' distance), the world seen through wherever the menu leaves "
           L"it. Native: the laser points at the panel. FlatVR: needs a BodyWalk that hangs it (an older one keeps the flat "
           L"menus). Off: a menu shows the flat picture, as before. F1, F2 and F3 always do."),
    Toggle(L"openxr", L"headset_settings", L"Settings panel in the headset (prototype)", 0,
           L"Native OpenXR without BodyWalk's menus: squeeze both grips for a moment, then click a thumbstick, and a small settings "
           L"panel opens in front of you - the flat picture's distance and width, brightness, your height, the laser, the VR render size "
           L"(Video page: presets and a slider; applying it makes the game rebuild its picture, a few seconds frozen) and Calibrate body "
           L"(Body page: the T-pose measurement, started from the headset) and the game view F1 - F4 (F1 - F3 close the panel; back to "
           L"F4 by the F4 key, D2R VR Authentic Settings > Home, or a view action in BodyWalk's mapping). Point a controller at "
           L"it and pull the trigger; Apply writes only what you changed into d2r_vr.ini (the original is kept once as "
           L"d2r_vr.ini.before-headset-menu; a change made elsewhere meanwhile is shown, not overwritten). With the skill panel in the "
           L"Tiles look (Skill panel style: Tiles) these pages are its Settings tab, beside Skills: the right stick's long press opens "
           L"the window on the tab it was last on, the grips and a click open it on Settings. While it is open both "
           L"controllers are the panel's: nothing of them reaches the game, and it never sends the game a click. Grips and a click "
           L"again, B / Y or Menu close it. While both grips are squeezed the thumbstick clicks are the panel's, not the game's. "
           L"Off: no panel and no chord. F4 only."),
    // [screen] menu_distance_m: vrcam asks FlatVR for it through Game Link (GameState, host API 6).
    Group(L"Belt potions"),
    Toggle(L"hands", L"belt_models", L"Potions on the belt, the game's own models", 1,
           L"First person (F4): the potions in the belt's bottom row hang on the hero's hips as the game's own 3D models - "
           L"lit, shaded and fogged like everything else (the game draws them). Off: as before."),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_ahead", L"Belt potions: distance from the body", 0.05f, 0.4f, 0.01f, 0.13f,
           L"The arc's radius round the hero's middle, in parts of his eye height: less = closer to the body, more = farther "
           L"out. Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_up", L"Belt potions: height", 0.3f, 0.8f, 0.01f, 0.57f,
           L"How high on the hero they hang, in parts of his eye height (0.57 = the hips). Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_arc", L"Belt potions: degrees between them", 10, 60, 1, 35,
           L"How far round the body the four spread: degrees on the arc from one column to the next. Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_across", L"Belt potions: right (+) / left (-)", -0.3f, 0.3f, 0.01f, 0,
           L"The whole belt shifted across the hero, in parts of his eye height: + to his right, - to his left. The headset settings panel's Belt page "
           L"writes the same key. Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_turn", L"Belt potions: turn round him, °", -90, 90, 1, 0,
           L"The arc of potions swung round the hero: + to his right, - to his left. The headset settings panel's Belt page writes the same key. Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_models_scale", L"Belt potions: size", 0.2f, 1.5f, 0.02f, 0.56f,
           L"1 = the size the game gives them on the ground. Live.")),
    Needs(L"hands", L"belt_models", 1, Slider(L"hands", L"belt_grab_cm", L"Belt potions: grab reach, cm", 4, 30, 1, 12,
           L"How near a hand's palm must be to a potion to light it and take it with the grip. A wide reach steals the grip meant for the weapon: "
           L"narrower, or the belt lower or farther to the side (the sliders above), and the weapon is yours again. The headset settings panel's "
           L"Belt page writes the same key. Live.")),
    Needs(L"hands", L"belt_models", 1, Toggle(L"hands", L"belt_grab", L"Belt potions: take them in the hand and drink", 1,
           L"A hand at a bottle lights it (a see-through ball, the bottle a little larger); the grip takes it into the hero's hand. "
           L"Brought to the mouth and tipped over it is drunk (the game's own belt key 1-4); let go anywhere else it goes back. "
           L"Switch BodyWalk's belt zones off with it. Live.")),
    Needs(L"hands", L"belt_grab", 1, Toggle(L"belt", L"grab_nearest", L"Grip: the nearer of a belt potion and the weapon wins", 1,
           L"A grip press that could take a belt potion OR the weapon goes to the NEARER one: your palm's distance to the bottle against your fist's distance to the weapon (the shaft of "
           L"a weapon your other hand holds - a staff, a two-hander, a crossbow, the second hand; one lying on the floor; the weapon stowed on your back). A tie goes to the weapon; "
           L"a hand behind a shoulder is the weapon swap's. A potion is lit (the ball) and taken only when it is the nearer one; where the weapon is nearer no potion is lit and the "
           L"weapon takes the grip. The log says, for every grip press, who took it and why. Unticked: as before - a potion in reach wins over the weapon. First person (F4). Live.")),
    Needs(L"hands", L"belt_grab", 1, Toggle(L"belt", L"grab_trigger", L"Take a belt potion with trigger + grip", 0,
           L"On: a belt potion is taken only with the trigger squeezed together with the grip. The grip alone never takes a potion - it goes to the weapon or the shoulder swap - and a "
           L"bottle in reach is only lit (the ball), it does not keep the grip from the weapon. Needs the controller's trigger from direct input; without it the grip alone takes, as "
           L"before. Off (the default): the grip alone takes a potion. Live.")),
    Needs(L"hands", L"belt_grab", 1, Toggle(L"belt", L"hold_takes_trigger", L"Bottle in hand: the triggers are not the game's", 1,
           L"While a belt bottle is in a hand (taken, or being drunk) the controller's triggers do not reach the game: the LEFT one (whichever hand holds the bottle) - the game reads it as "
           L"Shift on a belt key, and Shift + a belt key is not a drink, so a potion taken with trigger + grip was not drunk - and a right hand's own trigger (no attack out of a drink). A trigger "
           L"taken stays out until you let it go. Unticked: the triggers pass on as before. First person (F4). Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_close", L"In the hand: the fist closes to, %", 0, 150, 5, 55,
           L"How far the hand closes round a bottle it holds (100 = the full fist of the grip): enough to hold it, not through it. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_x", L"In the hand: x, cm", -15, 15, 0.5f, 0,
           L"The bottle moved along the controller's own x (across the hand). The hold is set on the left hand; the right one "
           L"takes its mirror (x, yaw and roll the other way). Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_y", L"In the hand: y, cm", -15, 15, 0.5f, 0,
           L"Along the controller's own y (out of the fist, past the thumb). Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_z", L"In the hand: z, cm", -15, 15, 0.5f, 0,
           L"Along the controller's own z (back toward the wrist; less = toward where it points). Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_pitch", L"In the hand: pitch, degrees", -180, 180, 5.0f, 0,
           L"The bottle turned about the controller's x. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_yaw", L"In the hand: yaw, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's y. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_hold_roll", L"In the hand: roll, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's z. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_close", L"Vials in the hand: the fist closes to, %", 0, 150, 5.0f, 0,
           L"How far the hand closes round a thin vial (stamina, thawing). Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_x", L"Vials in the hand: x, cm", -15, 15, 0.5f, 0,
           L"A thin vial moved across the hand (as the flasks' x). Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_y", L"Vials in the hand: y, cm", -15, 15, 0.5f, 0,
           L"Out of the fist, past the thumb. Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_z", L"Vials in the hand: z, cm", -15, 15, 0.5f, 0,
           L"Back toward the wrist. Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_pitch", L"Vials in the hand: pitch, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's x. Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_yaw", L"Vials in the hand: yaw, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's y. Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_vial_roll", L"Vials in the hand: roll, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's z. Not set = the flasks' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_close", L"Antidote in the hand: the fist closes to, %", 0, 150, 5.0f, 0,
           L"How far the hand closes round the antidote (the black vial). Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_x", L"Antidote in the hand: x, cm", -15, 15, 0.5f, 0,
           L"The antidote moved across the hand. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_y", L"Antidote in the hand: y, cm", -15, 15, 0.5f, 0,
           L"Out of the fist, past the thumb. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_z", L"Antidote in the hand: z, cm", -15, 15, 0.5f, 0,
           L"Back toward the wrist. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_pitch", L"Antidote in the hand: pitch, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's x. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_yaw", L"Antidote in the hand: yaw, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's y. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_antidote_roll", L"Antidote in the hand: roll, degrees", -180, 180, 5.0f, 0,
           L"Turned about the controller's z. Not set = the vials' value. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_ball_alpha", L"Lit ball: opacity", 0, 1, 0.05f, 0.35f,
           L"How much of the see-through ball on a bottle a hand is at shows: 0 = none, 1 = solid. Live.")),
    Needs(L"hands", L"belt_grab", 1, Slider(L"hands", L"belt_ball_radius", L"Lit ball: radius, cm", 2, 20, 0.5f, 6,
           L"The ball's size round the bottle. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_health", L"Lit ball: healing", L"red",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = red. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_mana", L"Lit ball: mana", L"light blue",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = light blue. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_stamina", L"Lit ball: stamina", L"white",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = white. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_rejuv", L"Lit ball: rejuvenation", L"lilac",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = lilac. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_antidote", L"Lit ball: antidote", L"green",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = green. Live.")),
    Needs(L"hands", L"belt_grab", 1, Color(L"hands", L"belt_ball_thawing", L"Lit ball: thawing", L"orange",
           L"The see-through ball on a bottle of this kind while a hand is at it. Empty (x) = orange. Live.")),
    Group(L"Item labels on the ground"),
    Slider(L"hud", L"labels_alpha", L"Item labels: opacity", 0, 1, 0.05f, 1,
           L"How much of the dark box behind the names of items on the ground is drawn in first person (F4): 1 = as the game draws "
           L"it, 0 = no box, the names alone. The names stay bright. Through the game's label code (vrcam's log says "
           L"\"item-label hook in\"); on another game build it does nothing here."),
    Slider(L"hud", L"labels_size", L"Item labels: size, %", 20, 150, 5, 100,
           L"The names of items on the ground and their boxes in first person (F4), smaller or larger, each about the point under it. "
           L"100 = the game's own. Item tooltips and all other text keep their size."),
    Slider(L"hud", L"labels_far", L"Item labels: size far away, % of the size above", 20, 100, 5, 100,
           L"Nearer names larger, farther ones smaller, as signs in the room: the size above for an item 1.5 m away and nearer, "
           L"this much of it for one 10 m away and farther, smoothly in between. 100 = the same size at any distance."),
    Slider(L"hud", L"labels_depth_body", L"Item labels: at their items, %", 0, 150, 5, 0,
           L"In stereo the game draws the labels on the same spot in both eyes - in the headset they float far off, not on "
           L"their items. Here each label is set at its own item's depth, by the item's distance from you: 100 = exactly "
           L"there, 0 = where the game leaves them. Their interface layer is taken out of the picture for it; the toolbar "
           L"and the map stay."),
    Slider(L"hud", L"labels_near_body", L"Item labels: stereo depth on top, all", -20, 20, 0.1f, 0,
           L"Every label (and the rest of the interface left in the picture, chat) nearer (+) or "
           L"farther (-) by the same amount, on top of the slider above. The name and bar over a monster have a depth of their own "
           L"(Monster plates, below) - none of the item labels' settings touch them."),
    Group(L"Monster plates"),
    // [hud] monster_alpha, monster_size: the plate over the monster - HUDMonsterHealth's TargetAttached with a pad (gamestate ReadPlate)
    Slider(L"hud", L"monster_alpha", L"Name over a monster: opacity", 0, 1, 0.05f, 1,
           L"The name plate the game shows for the monster you target (box, frame and name alike): 1 = as the game draws it, "
           L"0 = gone. Every view. Needs D2R_DepthFog.fx on in ReShade."),
    Slider(L"hud", L"monster_size", L"Name over a monster: size, %", 30, 200, 5, 100,
           L"That plate smaller or larger about its middle. 100 = the game's own. Every view."),
    // [hud] monster_far (vr/plate_look.h DistanceK): the item labels' rule, for the monster's plate
    Slider(L"hud", L"monster_far", L"Name over a monster: size far away, % of the size above", 20, 100, 5, 100,
           L"First person (F4). Nearer plates larger, farther ones smaller, as the item labels (Item labels: size far away): the size above for a monster 1.5 m "
           L"away and nearer, this much of it for one 10 m away and farther, smoothly in between. 100 = the same size at any distance. Live."),
    // [hud] monster_over_head (vr/plate_world.h LiftAnchor): the game's own plate, lifted over the monster's head in the game's own anchor call (vr/mob_bars.cpp)
    Toggle(L"hud", L"monster_over_head", L"Plate over the monster's head (prototype)", 0,
           L"First person (F4). The name and health bar the game hangs over the monster you target stay the game's own - nothing is cut out or drawn again - but are "
           L"lifted (or lowered) so that their lower edge hangs the gap below over the monster's head, whatever the distance. The game's own gap is the same few pixels "
           L"at any distance: some 7 cm over a head 1.5 m away, 45 cm over one 10 m away. The plate stays centred over the monster, as the game draws it. While on it "
           L"replaces the height slider. Needs the game's controller layout (the one D2R VR plays in). Off: the plate where the game puts it. Live."),
    Needs(L"hud", L"monster_over_head", 1, Slider(L"hud", L"monster_bar_gap_cm", L"Plate: gap over the head, cm", -50, 150, 1, 15,
           L"How far over the monster's head the plate's lower edge hangs; below 0 it lowers into the head. The head's height is worked out from the game's own anchor "
           L"of the plate and the monster's place. Live.")),
    // [hud] monster_height: the plate's anchor moved in the game's own call (vr/mob_bars.cpp), first person (F4) only
    Needs(L"hud", L"monster_over_head", 0, Slider(L"hud", L"monster_height", L"Name over a monster: height, %", -60, 40, 1, 0,
           L"First person (F4) only, while the plate over the monster's head (above) is off. The name and health bar over the monster you target, higher (+) or lower (-): "
           L"% of the screen's height; the whole plate moves, still following its monster. Too high, or cut off at the top of your view? "
           L"Lower it (try -15 to -30). 0 = where the game puts it. Monsters and NPCs; the plate that hangs over the monster "
           L"(the game's controller layout, the one D2R VR plays in). Live. Needs no ReShade effect.")),
    // [hud] monster_depth_body (vr/plate_look.h DepthShift): the item labels' "at their items", for the monster's plate
    Slider(L"hud", L"monster_depth_body", L"Name over a monster: at its monster, %", 0, 150, 5, 0,
           L"First person (F4) in stereo. The game draws the plate in both eyes on the same spot, so in the headset it floats far off, not on its monster. Here it is "
           L"set at its monster's depth, by the monster's distance from you, as the item labels are (Item labels: at their items): 100 = exactly there, 0 = where the "
           L"game leaves it (the stereo depth below). Live."),
    // [hud] monster_near_body (vr/plate_look.h): the plate at its own stereo depth, apart from the item labels' (labels_near_body)
    Slider(L"hud", L"monster_near_body", L"Name over a monster: stereo depth", -20, 20, 0.1f, 1.6f,
           L"First person (F4) in stereo: the name and bar over the monster you target nearer (+) or farther (-), % of the half screen - its own, "
           L"apart from the item labels' (\"Item labels: stereo depth on top\" above does not move it). Live."),
    Toggle(L"hud", L"ordinary_mob_bars", L"Hide the plates of ordinary monsters (prototype)", 0, kOrdinaryBarsTip),
    Group(L"Low life: red heartbeat vignette"),
    // [hud] low_hp_vignette, low_hp_threshold, low_hp_intensity_start / _end, low_hp_saturation, low_hp_radius, low_hp_softness,
    // low_hp_roundness, low_hp_gentle, low_hp_haptics, low_hp_haptics_strength (vr/low_hp.h)
    Needs(L"openxr", L"on", 1, Toggle(L"hud", L"low_hp_vignette", L"Red heartbeat vignette at low life", 0,
           L"Native OpenXR, first person (F4). While your life is under the threshold below, the edges of what you see tint deep "
           L"red and beat like a heart - a double beat, then a rest - faster and redder the lower your life goes. It comes up "
           L"within a second of dropping under the threshold, goes out after you heal a little above it, and is never there "
           L"when you are dead, in a menu or outside a game. It is a colour over the picture's edges only: the picture is not "
           L"scaled, moved or shaken. The sliders below set how it looks. Needs the new D2R_DepthFog.fx in ReShade, copied in "
           L"with the DLL. Off: the picture exactly as before. Live.")),
    LowLife(Slider(L"hud", L"low_hp_threshold", L"Low life: from, % of your life", 10, 60, 1, 30,
           L"The vignette comes on when your life falls under this share of its maximum and goes out a little above it. Live.")),
    LowLife(Slider(L"hud", L"low_hp_intensity_start", L"Intensity just under the threshold", 0.05f, 1, 0.05f, 0.25f,
           L"How red the edges are at the top of a beat when your life has only just fallen under the threshold (1 = solid red at "
           L"the rim). Between this and the next slider the intensity follows your life in a straight line - and so does the "
           L"heartbeat in the controllers. Live.")),
    LowLife(Slider(L"hud", L"low_hp_intensity_end", L"Intensity at the lowest life", 0.05f, 1, 0.05f, 0.85f,
           L"How red the edges are at the top of a beat when you are nearly dead. Live.")),
    LowLife(Slider(L"hud", L"low_hp_saturation", L"Red: saturation", 0, 1, 0.05f, 0.8f,
           L"The colour of the vignette: 0 = a dark grey, one that only darkens the edges; 1 = the full deep red. Live.")),
    LowLife(Slider(L"hud", L"low_hp_radius", L"Vignette size, % of the view", 20, 95, 1, 40,
           L"How far in from the edge of the view the colour reaches, in % of the view's ANGLE: small = only a thin rim, large = a "
           L"deep vignette. 100 minus this is the clear middle: at 40 the middle 60% stays clear (on a 100 degree view that is the "
           L"middle 60 degrees). The plugin takes your headset's real field of view from its runtime, so the same value is the same "
           L"angle on any headset; headsets differ in what you see through the lenses, so set it to your taste. 40 is about what "
           L"the first version did. Live.")),
    LowLife(Slider(L"hud", L"low_hp_softness", L"Edge: softness", 0, 1, 0.05f, 0.8f,
           L"How gradually the colour builds up over that reach: 1 = a smooth fade all the way in, 0 = a short ramp and then flat "
           L"colour to the rim. Live.")),
    LowLife(Slider(L"hud", L"low_hp_roundness", L"Shape: oval .. rounded square", 2, 6, 0.25f, 2.5f,
           L"The shape of the clear middle: 2 = an oval, higher = a rounder and rounder rectangle (the clear middle then reaches "
           L"further toward the corners of the view). Live.")),
    LowLife(Toggle(L"hud", L"low_hp_gentle", L"Gentle (slower, softer)", 0,
           L"A slower heartbeat (50 up to 72 beats a minute, against 60 up to 90), a softer beat with a shallower pulse, less red "
           L"and a slower fade - for anyone who finds the pulse in the corner of the eye too much. Live.")),
    LowLife(Needs(L"openxr", L"haptics", 1, Toggle(L"hud", L"low_hp_haptics", L"Heartbeat in the controllers", 0,
           L"A short pulse in both controllers on each beat, as the red climbs, stronger the lower your life is (the same line as "
           L"the intensity above). Needs Controller vibration (Stereo > Native OpenXR) ticked; its own strength slider applies "
           L"too. Live."))),
    LowLife(Needs(L"openxr", L"haptics", 1, Needs(L"hud", L"low_hp_haptics", 1, Slider(L"hud", L"low_hp_haptics_strength", L"Heartbeat in the controllers: strength", 0.1f, 2, 0.05f, 1,
           L"A scale on the pulse's strength: 1 = about half the motor's strength at the lowest life, and weaker the higher your "
           L"life. The pulse is never stronger than four fifths of the motor, so 2 is plenty. Live.")))),
    Needs(L"openxr", L"on", 1, Toggle(L"hud", L"merc_tint", L"Wounded mercenary tinted red (F4)", 0,
           L"VR first person (F4). While your mercenary is under the threshold below of its life, the effect tints its body red, "
           L"so you see when it needs a potion. Drawn by the plugin over the finished picture: the merc is found and its life only "
           L"READ, nothing in the game is changed (no state set on the merc, unlike d2r-pcvr's version of the idea). Needs the new "
           L"D2R_DepthFog.fx in ReShade, copied in with the DLL. Idea: bearstonem/d2r-pcvr. Off: the picture exactly as before. Live.")),
    Needs(L"openxr", L"on", 1, Needs(L"hud", L"merc_tint", 1, Slider(L"hud", L"merc_tint_threshold", L"Mercenary tint: under, % of its life", 10, 90, 1, 50,
           L"The red comes on while the mercenary's life is under this share of its maximum. Live."))),
    Needs(L"openxr", L"on", 1, Needs(L"hud", L"merc_tint", 1, Slider(L"hud", L"merc_tint_strength", L"Mercenary tint: strength", 0.1f, 1, 0.05f, 0.6f,
           L"How far the mercenary's colours go toward the red: 1 = fully red. Live."))),
    Group(L"Allies' portraits"),
    // [hud] allies_x, allies_y: the game draws the portraits itself, no layout's widget - vrcam moves their corner (AlliesBoxUv)
    Slider(L"hud", L"allies_x", L"Allies' portraits: left / right, %", -20, 80, 1, 0,
           L"The portraits of your mercenary and your party (top left) elsewhere on the screen: % of its width, + right. "
           L"0 = where the game puts them. Every view. Needs D2R_DepthFog.fx on in ReShade."),
    Slider(L"hud", L"allies_y", L"Allies' portraits: up / down, %", -80, 20, 1, 0,
           L"The same up (+) or down (-): % of the screen's height."),
    Group(L"Item condition figure"),
    // [hud] durability_*: the game's HUDWarnings > DurabilityWarning (the knight whose sword, bracers and boots go red as the items wear), vrcam moves its box (vr/durability_figure.h)
    Slider(L"hud", L"durability_x", L"Item condition figure: left / right, %", -80, 20, 1, 0,
           L"First person (F4) only. The little knight at the top right whose sword, armour, bracers and boots go red as your items wear out (the game's own "
           L"durability warning, shown from half worn) hangs at the very corner of the interface - at the edge of your view, half behind it. Here it is moved: "
           L"% of the picture's width, + right (it is at the right edge, so the useful way is left, -). 0 = where the game puts it. Needs D2R_DepthFog.fx on in ReShade. Live."),
    Slider(L"hud", L"durability_y", L"Item condition figure: up / down, %", -80, 20, 1, 0,
           L"The same up (+) or down (-): % of the picture's height. The figure is at the top, so down (-) brings it into view."),
    Slider(L"hud", L"durability_scale", L"Item condition figure: size, %", 30, 300, 5, 100,
           L"The figure smaller or larger about its middle. 100 = the game's own."),
    Toggle(L"hud", L"durability_hide", L"Hide the item condition figure", 0,
           L"First person (F4) only. The figure is not shown at all. Off: the figure where the sliders above put it."),
    Group(L"Panels (inventory, trade...)"),
    Slider(L"screen", L"menu_distance_m", L"Screen distance while a panel is open (inventory, trade...), m", 0, 8, 0.1f, 1.6f,
           L"While the inventory, a trader, the stash or another panel is open in first person (F4), FlatVR moves its "
           L"screen this far from your eyes, so the whole panel is in view, and brings it back to your own distance when "
           L"the panel closes - smoothly, in under half a second. Your FlatVR Distance setting is not changed. A screen "
           L"already farther away than this comes nearer instead. 0 = the screen stays where it is. Needs BodyWalk with "
           L"the Game Link plugin and FlatVR's \"Screen distance follows the game\" (on by default). With native OpenXR "
           L"the flat picture a panel is shown on goes this far, in any view (Stereo > Native OpenXR)."),
    // Here from "UI: F1 above" (every view; nobody found it there, 2026-10-12): the square window's panels need it.
    Slider(L"@game", L"Safe Screen Percent", L"Game interface size, % (restart)", 50, 100, 1, 80,
           L"Below 100 the game lays its whole interface out in a smaller box in the middle of the screen - in every view. "
           L"At 100, the game's default, the edges of the interface are cut off in the headset (the inventory, the stash and the other panels are "
           L"wider than the picture; with a trader or the mercenary open beside the inventory the outer columns of both cannot be reached): VR starts "
           L"it at 80 where you have chosen no other size (the box below; 74 for the headset's own, taller picture) - the most two side panels fit "
           L"a square picture at. A size of yours other than 100 stays as it is (the line below the box says whether it fits); flat mode keeps your own. "
           L"Read when the game starts: change it with the game closed, or the game puts its own value back on exit."),
    Only(kVr, Toggle(L"hud", L"safe_screen_vr", L"VR: start the interface at 80 %", 1,
           L"On: when the game's interface size is 100 (its default), D2R VR sets 80 for VR - at 100 the edges of the interface are cut off in the "
           L"headset - when the game starts, or at once with the game closed. The size you had is kept and comes back in flat mode. Any size of "
           L"yours other than 100 is left alone. Off: the game's interface size is never touched (and an 80 D2R VR set goes back to what you had)."
           L"With it on a 100 of yours does not stay in VR: untick it to keep 100.")),
    // (two side panels open - a trader or the mercenary left, the inventory right - need room: the line says whether this size gives it, for the shape of the picture)
    Only(kVr, Status(L"safe_screen_fit", L"")),
    Group(L"Toolbar (life, mana, belt) in first person"),
    // The toolbar's type (shared/toolbar_type.h): one choice over [hud] hide and [hud] bar_place; the headset panel has the same
    Choice(L"@toolbar", L"type", L"Toolbar type (first person, F4)", 0,
           {toolbartype::kLongW[0], toolbartype::kLongW[1], toolbartype::kLongW[2], toolbartype::kLongW[3], toolbartype::kLongW[4], toolbartype::kLongW[5], toolbartype::kLongW[6]},
           L"The game's bottom panel is its toolbar: the life and mana globes, the belt and the skill buttons. This one choice sets Hide the game's "
           L"interface and Where (below) together, so a place always works. Where it is, view by view: F1, F2, F3 and the flat modes - in the picture "
           L"where the game draws it (those tabs size or move it; set to 'Hung in the room' a view follows this choice like F4). F4: 'Watches' - cut out "
           L"of the picture, your life and mana as two balls on the wrists (needs D2R_DepthFog.fx in ReShade). 'Chest', 'low in front', 'forearm' - "
           L"cut out and hung there as the whole toolbar (its size and place are the sliders below). 'In the picture' - left where the game draws it: "
           L"it moves with your head, at the bottom of your view, in front of your eyes. With a panel open (inventory, stash, trader, the game's menu) "
           L"the game's whole interface is back in every view, the toolbar with it: in the picture, or at the bottom of the menu panel. Live."),
    Group(L"In the game's picture"),
    Choice(L"hud", L"map", L"Corner map", 0, {L"In the picture", L"Out of the picture (its own texture)"},
           L"The game draws the corner map under a scissor of its own: its draws go to a texture of their own, the labels "
           L"over monsters stay in the picture - those over the map too."),
    Needs(L"hud", L"map", 1, Choice(L"hud", L"map_corner", L"Corner map is", 0, {L"Found by itself", L"Top left", L"Top right"},
           L"Where the game puts its mini map (the game's own option). Found by itself: either top corner.")),
    Choice(L"hud", L"hide", L"Hide the game's interface", 0, {L"Nothing", L"The toolbar", L"All of it"},
           L"The game draws its whole interface into a layer of its own. \"The toolbar\" empties only the toolbar's strip "
           L"(found from the screen's size and the game's interface size); "
           L"labels over monsters, tooltips and shops stay. \"All of it\": the world alone. 'Toolbar type' above sets this and Where together."),
    // The strip \"The toolbar\" empties is worked out from the picture's size and the game's interface size; the two sliders make it taller (vr/hud_rects.h)
    ShowFor(L"hud", L"hide", 1u << 1, Slider(L"hud", L"hide_up_pct", L"Toolbar cut: higher, % of the picture", -5, 15, 0.1f, 0,
           L"How much HIGHER the strip that \"The toolbar\" empties reaches, in percent of the picture's height. These two sliders act on what the game draws as its toolbar "
           L"(the strip across the bottom of its interface). Raise this if a bit of the statues' horns, their hands or the tops of the life and mana globes stay in the "
           L"picture above the cut; lower it if the cut takes more than the toolbar. 0 is how it always was. A square 2048 picture at Safe Screen Percent 80 wants about 3 - 4. "
           L"Live; tick \"Show the cut strip\" below to see where its top edge is.")),
    ShowFor(L"hud", L"hide", 1u << 1, Slider(L"hud", L"hide_down_pct", L"Toolbar cut: lower, % of the picture", -2, 5, 0.1f, 0,
           L"How much LOWER the strip that \"The toolbar\" empties reaches, in percent of the picture's height: raise it if something of the toolbar stays under the cut, "
           L"lower it if the cut eats what is drawn below the toolbar. 0 is how it always was. Live.")),
    ShowFor(L"hud", L"hide", 1u << 1, Toggle(L"debug", L"hide_strip_tint", L"Show the cut strip (magenta veil)", 0,
           L"Paints the strip that \"The toolbar\" empties a see-through magenta, so its edges can be seen while the two sliders above are tuned. Off when done. Live.")),
    Toggle(L"hud", L"colors_as_monitor", L"Colours as on the monitor", 1,
           L"Flip it if the toolbar and the map come out too dark or washed out in the headset."),
    Group(L"Toolbar in the headset"),
    Choice(L"hud", L"bar_place", L"Where", 5, {L"Not shown", L"Left forearm", L"Right forearm", L"Low in front", L"Chest, low", L"Wrists: life left, mana right"},
           L"Where the toolbar taken out of the picture hangs (Hide the game's interface: The toolbar; FlatVR hangs it, native OpenXR hangs it itself). "
           L"On a forearm it lies along the inside of the arm: turn the palm up to read it. \"Wrists\": not the toolbar but only your life and "
           L"mana, as two balls on the back of the wrists like medallions - life on the left, mana on the right (first person, F4; they only read your "
           L"life and mana; the rest of the toolbar is not shown in the headset then). Needs the new D2R_DepthFog.fx in ReShade. 'Toolbar type' above sets this and Hide the game's interface together"),
    // On the wrists (bar_place 5): the look and the place of the two balls (vr/hand_orbs.h)
    ShowFor(L"hud", L"bar_place", kBarWrist, Choice(L"hud", L"wrist_look", L"Look", 0,
           {L"3D glass balls with liquid", L"The game's own globes (cut out of its toolbar)"},
           L"Glass balls: real 3D balls the mod draws (a liquid rising and falling with the numbers, glass, a halo), with depth - a hand or a monster in front "
           L"hides them. The game's own globes: the life and mana globes of the game's toolbar, cut out of its interface as they are drawn, with the real "
           L"level of liquid, laid on the same wrist balls (they cannot be cut where the game's safe screen area is smaller than the globes; the log says so).")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Choice(L"hud", L"hand_vitals_orb_shape", L"Shape", 0,
           {L"Medallion (a glass dome on a metal base)", L"Full glass ball"},
           L"The medallion: a half ball of glass - the liquid in it - sitting on a round metal base, flat on the back of the wrist, like the frame of the game's own globes "
           L"(Size is the medallion's diameter, the base's). The game's own globes sit on the same base. Full glass ball: the ball alone, as before. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_shape", 0, Choice(L"hud", L"hand_vitals_orb_metal", L"Base: metal", 0,
           {L"Gold", L"Bronze", L"Silver"}, L"The base's metal. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_shape", 0, Slider(L"hud", L"hand_vitals_orb_patina", L"Base: patina, %", 0, 100, 5, 55,
           L"How aged the base is: 0 clean metal; more and it darkens, goes green in the crevices and at the foot of the dome, and stays bright where it would be worn - "
           L"the same on every frame, fixed on the medallion (it does not swim as the hand moves). Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"wrist_look", 0, StartsFromMapped(L"hand_vitals_orb_liquid", {0.0f, 5.0f},
           Choice(L"hud", L"hand_vitals_orb_fill", L"Fill direction", 4,
           {L"Along the arm: the elbow side full", L"Along the arm, the other way: the hand side full", L"Across the hand: the thumb side full",
            L"Across the hand: the little finger side full", L"Out of the hand: from the skin up, the level parallel to the back of the hand", L"Level in the world"},
           L"Which way the liquid fills - each (but the last) a fixed direction of the WRIST's own frame, so it turns with the wrist and never with your head: "
           L"the level rises along it and the liquid sits on the other side. Out of the hand: the liquid lies on the skin and rises, its surface parallel to the back of the hand "
           L"(level when the back of the hand is up, as a medallion's liquid reads). Along the arm: a surface across the forearm, from the elbow side (or the hand side). "
           L"Across the hand: a surface across it, from the thumb side or the little finger's. Level in the world: always horizontal, whatever the hand does. "
           L"(Replaces the older Liquid level: along the arm is the first, level in the world the last.) Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"wrist_look", 1, Needs(L"hud", L"hand_vitals_orb_shape", 1, Choice(L"hud", L"hand_vitals_orb_disc", L"Disc faces", 1,
           {L"The eyes (as before)", L"Fixed on the wrist"},
           L"The game's own globe as a ball (Shape: Full glass ball) is a flat picture: it can turn to face your eyes all the time, or stay fixed on the wrist - a disc lying on the back of "
           L"the hand, its up along the hand - and turn with it. On the medallion it is always on the base. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Choice(L"hud", L"hand_vitals_orb_hands", L"Which wrist", 0,
           {L"Life on the left, mana on the right", L"Life on the right, mana on the left", L"Both on the left", L"Both on the right"},
           L"The game's own globes have life on the left and mana on the right. Both on one wrist puts them side by side across the hand. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_size_cm", L"Size, cm", 3, 9, 0.5f, 5, L"The ball's diameter. 5 is a medallion's size. Live.")),
    // The ball's place on each wrist: X along, Y across, Z up, in the wrist's own frame, from the wrist joint (turning with the hand)
    ShowFor(L"hud", L"bar_place", kBarWrist, Toggle(L"hud", L"hand_vitals_orb_mirror", L"Right wrist mirrors the left", 0,
           L"The right wrist's ball takes the left wrist's three numbers (the same along, the same across toward the thumb, the same up), so the two sit as mirror "
           L"images. The right wrist's own sliders keep their values, greyed, for when it is unticked. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, StartsFrom(L"hand_vitals_orb_along_cm", Slider(L"hud", L"hand_vitals_orb_left_along_cm", L"Left wrist X, along the arm, cm", -10, 10, 0.5f, 3,
           L"From the wrist joint along the hand's axis: + toward the knuckles, - toward the elbow. 0 is at the wrist. Turns with the hand. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_left_side_cm", L"Left wrist Y, across the hand, cm", -10, 10, 0.5f, 0,
           L"Across the hand: + toward the thumb, - toward the little finger. 0 is in the middle of the back of the hand. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, StartsFrom(L"hand_vitals_orb_up_cm", Slider(L"hud", L"hand_vitals_orb_left_up_cm", L"Left wrist Z, up off the hand, cm", -10, 10, 0.5f, 4,
           L"Out of the back of the hand, from the wrist's axis (- into it). The arm's skin is about 2 - 3 cm out: 4 sinks the ball a little into the skin, "
           L"6 or more floats it above. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, StartsFrom(L"hand_vitals_orb_along_cm",
           Slider(L"hud", L"hand_vitals_orb_right_along_cm", L"Right wrist X, along the arm, cm", -10, 10, 0.5f, 3,
           L"From the wrist joint along the hand's axis: + toward the knuckles, - toward the elbow. 0 is at the wrist. Turns with the hand. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, Slider(L"hud", L"hand_vitals_orb_right_side_cm", L"Right wrist Y, across the hand, cm", -10, 10, 0.5f, 0,
           L"Across the hand: + toward the thumb, - toward the little finger (the same way as on the left wrist, so the same number is the mirror image). Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, StartsFrom(L"hand_vitals_orb_up_cm",
           Slider(L"hud", L"hand_vitals_orb_right_up_cm", L"Right wrist Z, up off the hand, cm", -10, 10, 0.5f, 4,
           L"Out of the back of the hand, from the wrist's axis (- into it). Live.")))),
    // The medallion's own turn about the wrist's three axes, degrees, a set for each wrist (the right one greyed while it mirrors the left)
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_left_rot_x_deg", L"Left wrist rotation X, round the arm, °", -180, 180, 1, 0,
           L"Turns the whole medallion - its base, its dome and the way the liquid fills - about its own middle, round the wrist's axis along the hand: + turns its face toward the thumb, - toward the little finger. Anatomical: the same number on the other wrist is the mirror image. 0 keeps it flat on the back of the hand. (For the game's own globes as a ball facing your eyes it has no effect.) Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_left_rot_y_deg", L"Left wrist rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Turns the whole medallion about its own middle, round the axis across the hand: + turns its face toward the elbow, - toward the knuckles. Anatomical: the same number on the other wrist is the mirror image. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_left_rot_z_deg", L"Left wrist rotation Z, up off the hand, °", -180, 180, 1, 0,
           L"Turns the whole medallion about its own middle, round the axis out of the back of the hand (like turning a watch face on the wrist): + turns its \"up\" from the knuckles toward the little finger. Anatomical: the same number on the other wrist is the mirror image. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, Slider(L"hud", L"hand_vitals_orb_right_rot_x_deg", L"Right wrist rotation X, round the arm, °", -180, 180, 1, 0,
           L"Turns the whole medallion - its base, its dome and the way the liquid fills - about its own middle, round the wrist's axis along the hand: + turns its face toward the thumb, - toward the little finger. Anatomical: the same number on the other wrist is the mirror image. 0 keeps it flat on the back of the hand. (For the game's own globes as a ball facing your eyes it has no effect.) Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, Slider(L"hud", L"hand_vitals_orb_right_rot_y_deg", L"Right wrist rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Turns the whole medallion about its own middle, round the axis across the hand: + turns its face toward the elbow, - toward the knuckles. Anatomical: the same number on the other wrist is the mirror image. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_mirror", 0, Slider(L"hud", L"hand_vitals_orb_right_rot_z_deg", L"Right wrist rotation Z, up off the hand, °", -180, 180, 1, 0,
           L"Turns the whole medallion about its own middle, round the axis out of the back of the hand (like turning a watch face on the wrist): + turns its \"up\" from the knuckles toward the little finger. Anatomical: the same number on the other wrist is the mirror image. Live."))),
    // The wrist under a shield (the left arm, or the right one after the shoulder draw swaps the hands) rolls its ball to the other side of the wrist so the shield does not cover it; VR F4 only
    ShowFor(L"hud", L"bar_place", kBarWrist, Toggle(L"hud", L"hand_vitals_orb_under_shield", L"Under a shield: the ball goes to the other side of the wrist", 1,
           L"The wrist that wears the shield (the left arm, or the right one after the shoulder draw swaps your hands) rolls its ball half a turn round the arm to the inner side of the wrist, so the shield "
           L"does not cover it, and rolls it back when the shield leaves that arm - a short smooth turn. Only in VR F4. Unticked: the ball stays on the back of the hand under a shield. The sliders below "
           L"tune the new place and are added to the wrist's own numbers above. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_turn_deg", L"Under a shield: turn round the arm, °", -180, 180, 1, 180,
           L"How far the ball rolls round the wrist's axis when a shield is on that arm. 180 is the opposite side of the wrist (the inner side); a smaller number stops short of it. + sends the back of the "
           L"hand over the thumb side, - over the little finger side (the same on both wrists, so the roll is the mirror image). Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm", L"Under a shield: X, along the arm, cm", -10, 10, 0.5f, 0,
           L"Added to the wrist's own X while a shield is on that arm: + toward the knuckles, - toward the elbow. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_side_cm", L"Under a shield: Y, across the hand, cm", -10, 10, 0.5f, 0,
           L"Added to the wrist's own Y while a shield is on that arm, measured in the rolled frame: at the full half turn + points toward the little finger (the thumb has gone round to the other side). Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm", L"Under a shield: Z, up off the skin, cm", -10, 10, 0.5f, 0,
           L"Added to the wrist's own Z while a shield is on that arm, measured out of the skin where the ball now sits: + floats it higher, - sinks it. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg", L"Under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added to the wrist's own rotation X while a shield is on that arm (see the wrist rotation sliders above for the meaning). Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg", L"Under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added to the wrist's own rotation Y while a shield is on that arm. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg", L"Under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added to the wrist's own rotation Z while a shield is on that arm. Live."))),
    // Under a shield, per hero class: the forearm's model - and so how deep the inner side of the wrist lies - differs with the class: a depth and an along offset for each, added to the shield's own above
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Status(L"hero_class_shield", L"The hero playing now"))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_amazon", L"Amazon, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Amazon wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_amazon", L"Amazon, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Amazon wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_amazon", L"Amazon, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Amazon wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_amazon", L"Amazon, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Amazon wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_amazon", L"Amazon, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Amazon wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_sorceress", L"Sorceress, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Sorceress wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_sorceress", L"Sorceress, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Sorceress wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_sorceress", L"Sorceress, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Sorceress wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_sorceress", L"Sorceress, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Sorceress wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_sorceress", L"Sorceress, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Sorceress wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_necromancer", L"Necromancer, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Necromancer wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_necromancer", L"Necromancer, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Necromancer wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_necromancer", L"Necromancer, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Necromancer wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_necromancer", L"Necromancer, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Necromancer wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_necromancer", L"Necromancer, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Necromancer wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_paladin", L"Paladin, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Paladin wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_paladin", L"Paladin, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Paladin wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_paladin", L"Paladin, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Paladin wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_paladin", L"Paladin, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Paladin wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_paladin", L"Paladin, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Paladin wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_barbarian", L"Barbarian, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Barbarian wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_barbarian", L"Barbarian, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Barbarian wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_barbarian", L"Barbarian, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Barbarian wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_barbarian", L"Barbarian, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Barbarian wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_barbarian", L"Barbarian, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Barbarian wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_druid", L"Druid, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Druid wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_druid", L"Druid, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Druid wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_druid", L"Druid, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Druid wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_druid", L"Druid, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Druid wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_druid", L"Druid, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Druid wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_assassin", L"Assassin, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Assassin wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_assassin", L"Assassin, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Assassin wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_assassin", L"Assassin, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Assassin wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_assassin", L"Assassin, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Assassin wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_assassin", L"Assassin, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Assassin wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_up_cm_warlock", L"Warlock, under a shield: depth (Z), cm", -5, 5, 0.1f, 0,
           L"Added - while a Warlock wears a shield - to the shield's own Z above, measured out of the skin where the ball now sits, on the inner side of the wrist: + floats it off the arm, - sinks it in. The forearm's model differs "
           L"with the class, so one depth fits one hero only. 0 = no value of its own: the shield's Z above, as before. Edit the row of the hero you play - the line above says which one the game has now. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_along_cm_warlock", L"Warlock, under a shield: along the arm, cm", -5, 5, 0.1f, 0,
           L"Added - while a Warlock wears a shield - to the shield's own X above: + toward the knuckles, - toward the elbow, in the rolled frame. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_x_deg_warlock", L"Warlock, under a shield: rotation X, round the arm, °", -180, 180, 1, 0,
           L"Added - while a Warlock wears a shield - to the shield's own rotation X above and to the wrist's own: the medallion turned round the arm along the hand (+ turns the medallion's face toward the thumb). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_y_deg_warlock", L"Warlock, under a shield: rotation Y, across the hand, °", -180, 180, 1, 0,
           L"Added - while a Warlock wears a shield - to the shield's own rotation Y above and to the wrist's own: the medallion turned round the axis across the hand (+ turns its face toward the elbow). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"hud", L"hand_vitals_orb_under_shield", 1, Slider(L"hud", L"hand_vitals_orb_shield_rot_z_deg_warlock", L"Warlock, under a shield: rotation Z, up off the skin, °", -180, 180, 1, 0,
           L"Added - while a Warlock wears a shield - to the shield's own rotation Z above and to the wrist's own: the medallion turned round the axis out of the skin where the ball now sits (like turning a watch face). Anatomical: the same number on the other wrist is the mirror image. "
           L"It eases in and out with the roll. 0 = no value of its own. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_opacity", L"Opacity", 0.3f, 1, 0.05f, 0.95f,
           L"How solid the balls are. They also fade when a hand is held far from your eyes. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Toggle(L"hud", L"hand_vitals_pulse", L"Soft heartbeat at low life", 0,
           L"While your life is under the low-life threshold (the one of the red heartbeat vignette above) the life ball glows and swells softly like a "
           L"heart (50 - 72 beats a minute, never flashing). Live.")),
    // The wrist status (vr/wrist_status.h): what the hero has, as small icons and counters over a wrist turned to the eyes - a feature of its own, its tick first, the rest live only while it is ticked
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Toggle(L"hud", L"wrist_status", L"Wrist status: summons, mercenary, buffs, auras, curses (F4)", 0,
           L"Small icons and counters that come up beside the medallions when the BACK of a wrist is turned to your eyes (VR first person, F4, native OpenXR). By default the left wrist shows "
           L"what is ON you - the auras and buffs you have (Might, Holy Shield, Frozen Armor, Battle Orders, a potion, a shrine ...) with the curses and ailments that hurt you (Amplify "
           L"Damage, Decrepify, Chilled, Poisoned ...) under a red rim - and the right wrist what you COMMAND: how many skeletons, mages, golems, wolves, ravens, vines, sentries, hydras, "
           L"demons you have, and your mercenary (a red 0 when it is dead). Read from the game's own lists, nothing is changed. The skill icons come from the local icon cache (see the skill "
           L"wheel); without it the chips show initials. Needs the plugin and the effect of this version; the wrists as such are the medallions above. Live."))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Toggle(L"hud", L"wrist_status_companions", L"Show the summons and the mercenary", 1,
           L"Counters for your skeletons, mages, golems, revived monsters, valkyrie, decoy, ravens, wolves, grizzly, spirit, vines, shadows, sentries, traps, hydras, demons and the mercenary. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Toggle(L"hud", L"wrist_status_effects", L"Show the buffs, auras and curses", 1,
           L"One chip for each effect that is on you: the helpful ones (auras, buffs, potions, shrines) under a green rim, the harmful ones (curses, ailments) first under a red one, a form or a "
           L"trade-off under a gold one. More than the panel holds: the last chip says how many are left. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Choice(L"hud", L"wrist_status_sides", L"Which wrist shows what", 0,
           {L"Effects on the left, summons on the right", L"Summons on the left, effects on the right", L"Both on both wrists"},
           L"Where the two kinds of chips come up. A wrist with nothing to show does not come up at all. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Slider(L"hud", L"wrist_status_size_cm", L"Panel width, cm", 6, 16, 0.5f, 10,
           L"How wide the panel is; the chips are a sixth of it. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Slider(L"hud", L"wrist_status_height_cm", L"Panel height over the wrist, cm", -5, 15, 0.5f, 7,
           L"How far above the wrist the panel hangs (and 3 cm toward your eyes, always turned to them). Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Slider(L"hud", L"wrist_status_along_cm", L"Panel along the arm, cm", -10, 10, 0.5f, 0,
           L"Moves the panel along the forearm: + toward the elbow, - toward the fingers. 0 hangs it over the controller. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Slider(L"hud", L"wrist_status_cone", L"How squarely the wrist must face you, degrees", 20, 60, 1, 40,
           L"The back of the hand must point this close to your eyes for the panel to come up (a little wider to stay up), with the wrist raised in front of your face. Live.")))),
    ShowFor(L"hud", L"bar_place", kBarWrist, Needs(L"openxr", L"on", 1, Needs(L"hud", L"wrist_status", 1, Slider(L"hud", L"wrist_status_opacity", L"Panel opacity", 0.3f, 1, 0.05f, 1,
           L"How solid the chips are. Live.")))),
    // The wrist medallions' depth per hero class: the wrist bone and the arm's model differ with the class, so one Z fits one hero
    ShowFor(L"hud", L"bar_place", kBarWrist, Status(L"hero_class", L"The hero playing now")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_amazon", L"Amazon: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Amazon plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_sorceress", L"Sorceress: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Sorceress plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_necromancer", L"Necromancer: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Necromancer plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_paladin", L"Paladin: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Paladin plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_barbarian", L"Barbarian: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Barbarian plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_druid", L"Druid: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Druid plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_assassin", L"Assassin: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Assassin plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    ShowFor(L"hud", L"bar_place", kBarWrist, Slider(L"hud", L"hand_vitals_orb_z_cm_warlock", L"Warlock: depth (Z) offset, cm", -5, 5, 0.1f, 0,
           L"Added to the wrists' Z (up off the hand, above) of BOTH wrists - mirrored or not - while a Warlock plays: the wrist bone and the arm's model differ with the class, so one depth "
           L"fits one hero only. + lifts the medallion off the arm, - sinks it in. 0 = no value of its own: the wrists' own Z, as before. Edit the row of the hero "
           L"you play - the line above says which one the game has now. Live.")),
    // On a forearm (bar_*_arm)
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_width_arm", L"Length, m", 0.1f, 1.0f, 0.01f, 0.22f)),
    ShowFor(L"hud", L"bar_place", kBarArm, Choice(L"hud", L"bar_split_arm", L"Cut in two", 0,
           {L"No - one strip", L"Yes, the red half on one side", L"Yes, the red half on the other side"},
           L"The strip cut at the middle and the halves laid side by side: the blue orb's half stays where it was, the red "
           L"orb's half turned end for end beside it, so both orbs sit together at the wrist. Half as long, twice as wide. "
           L"If they end up at the elbow, Rotate Z 180.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_along_arm", L"Move X (along the arm), cm", -15, 25, 0.5f, 0, L"+ further up the arm, - towards the hand.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_side_arm", L"Move Y (across the arm), cm", -20, 20, 0.5f, 0, L"Sideways in the strip's own plane.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_lift_arm", L"Move Z (off the arm), cm", -10, 20, 0.5f, 0, L"+ further from the arm, - closer.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_roll_arm", L"Rotate X (round the arm), °", -180, 180, 1, 0, L"Turns the strip round the arm.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_tip_arm", L"Rotate Y (tip the far end), °", -90, 90, 1, 0, L"Raises or lowers the end towards the hand.")),
    ShowFor(L"hud", L"bar_place", kBarArm, Slider(L"hud", L"bar_spin_arm", L"Rotate Z (in its plane), °", -180, 180, 1, 0, L"Spins the strip flat on the arm; 180 = the other way round.")),
    // On the body (bar_*_body): low in front or on the chest - no moving along an arm
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_width_body", L"Length, m", 0.1f, 1.0f, 0.01f, 0.22f)),
    ShowFor(L"hud", L"bar_place", kBarBody, Choice(L"hud", L"bar_split_body", L"Cut in two", 0,
           {L"No - one strip", L"Yes, the red half on one side", L"Yes, the red half on the other side"},
           L"The strip cut at the middle and the halves laid side by side, both orbs together at one end. Half as long, twice as wide.")),
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_side_body", L"Move Y (across the strip), cm", -20, 20, 0.5f, 0, L"Sideways in the strip's own plane.")),
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_lift_body", L"Move Z (off its face), cm", -10, 20, 0.5f, 0, L"+ towards the eyes, - away.")),
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_roll_body", L"Rotate X (round its length), °", -180, 180, 1, 0, L"Tilts the strip towards you or away.")),
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_tip_body", L"Rotate Y (tip one end), °", -90, 90, 1, 0, L"Raises or lowers one end.")),
    ShowFor(L"hud", L"bar_place", kBarBody, Slider(L"hud", L"bar_spin_body", L"Rotate Z (in its plane), °", -180, 180, 1, 0, L"Spins the strip flat; 180 = the other way round.")),
    Group(L"Map in the headset"),
    Choice(L"hud", L"map_place", L"Where", 1, {L"Not shown", L"Left hand", L"Right hand", L"Low in front", L"Chest, low"},
           L"Where the corner map taken out of the picture hangs (Corner map: Out of the picture; FlatVR hangs it, native OpenXR hangs it itself). "
           L"The left hand by default: the right one is usually the working hand. "
           L"Show or hide it from a button or a gesture: BodyWalk's app action \"FlatVR Game Map\"."),
    Toggle(L"hud", L"map_orb", L"A magic orb", 1, L"A see-through glass ball in the palm with the map laid over it. Off: a flat panel."),
    Needs(L"hud", L"map_orb", 1, Slider(L"hud", L"orb_size", L"Orb size, m", 0.05f, 0.4f, 0.01f, 0.12f)),
    Needs(L"hud", L"map_orb", 1, Slider(L"hud", L"orb_zoom", L"Map zoom on the orb", 0.5f, 4.0f, 0.05f, 1.0f,
           L"1 = the map's width across the ball; more = a closer look around the hero.")),
    Needs(L"hud", L"map_orb", 1, Slider(L"hud", L"orb_glow", L"Orb glow", 0, 1, 0.01f, 0.35f, L"The halo and the light on its rim.")),
    Needs(L"hud", L"map_orb", 1, Slider(L"hud", L"orb_alpha", L"Orb opacity", 0.02f, 1, 0.01f, 1,
           L"How solid the glass, its rim and the halo are; the map's lines stay as they are.")),
    Needs(L"hud", L"map_orb", 1, Color(L"hud", L"orb_color", L"Orb colour", L"magic blue", L"The halo, the rim and the mist inside.")),
    Needs(L"hud", L"map_orb", 0, Slider(L"hud", L"map_width", L"Map width, m", 0.1f, 1.0f, 0.01f, 0.22f)),

#endif
    Tab(L"World"),
    Group(L"Fog"),
    Toggle(L"fog", L"enabled", L"Distance fog", 0,
           L"Through ReShade (D2R_DepthFog.fx in reshade-shaders\\Shaders). With the sky on, it fades into the sky."),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"start", L"Fog start", 0, 1000, 5, 150, L"Where the fog begins. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"end", L"Full fog", 10, 2000, 10, 600, L"Where the fog is complete. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"curve", L"Falloff", 0.3f, 4, 0.05f, 1.6f, L"1 = linear; more = a clearer middle distance.")),
    Needs(L"fog", L"enabled", 1, Slider(L"fog", L"blur", L"Depth blur, px (0 = off)", 0, 12, 0.5f, 3,
           L"The fog over a 5x5 grid this many pixels apart: holes and specks in the depth melt away.")),
    Needs(L"fog", L"enabled", 1, Toggle(L"fog", L"spare_effects", L"Fog spares fire, spells and smoke (test)", 0,
           L"Fire, spells, smoke and glows are fogged only as much as the scene behind them is not: a torch or a Fire Bolt in "
           L"front of a far wall stays bright instead of fogged like the wall. The game's own transparent layers of each "
           L"frame are copied for it (VR F4 only). Applies when the game is started again; off = the fog as before.")),
    Needs(L"fog", L"enabled", 1, Toggle(L"fog", L"caves", L"Fog in caves and dungeons", 1,
           L"Off: no fog underground (every area that is not outdoors); the open air keeps it.")),
    Needs(L"fog", L"caves", 1, Slider(L"fog", L"caves_start", L"Fog start in caves", 0, 1000, 5, 150,
           L"Where the fog begins in caves, dungeons and crypts. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"caves", 1, Slider(L"fog", L"caves_end", L"Full fog in caves", 10, 2000, 10, 600,
           L"Where the fog is complete in caves, dungeons and crypts. World units (the hero is ~7.5 tall).")),
    Needs(L"fog", L"caves", 1, Slider(L"fog", L"caves_strength", L"Fog strength in caves", 0, 1, 0.01f, 1,
           L"How much the full fog covers in caves: 1 = the far end is gone in the fog colour, less = it shows through.")),
    Needs(L"fog", L"enabled", 1, Color(L"fog", L"color_caves", L"Fog colour in caves", L"same as the act",
          L"Caves, dungeons, crypts: every area that is not outdoors. Empty (x) = that act's colour.")),
    Toggle(L"render", L"game_height_fog", L"The game's own height fog", 0,
           L"Only the act 2 town has it. From our camera its edge is a hard line across the picture with a pale "
           L"veil above it (the game's camera never sees that edge). Off: vrcam sets it to 0."),
    Toggle(L"render", L"game_depth_fog", L"The game's own depth fog", 0,
           L"Every outdoor area has it, from its camera's distance out: a grey haze in the four corners of the picture "
           L"(from above too), over the near ground from our camera. Off: vrcam sets it to 0."),
    Toggle(L"render", L"depth_fix", L"The game's effects measure depth from our camera", 1,
           L"The game keeps a copy of the depth for its fog, height fog, ambient occlusion, reflections and water, made for "
           L"its camera from above (550 to 1060 units away). From ours everything is nearer and read as one flat wall "
           L"550 away: the act 2 town's straight line with a pale veil above it, the haze in the corners. On: that depth "
           L"runs from our camera. Off: the game's own."),
    Choice(L"render", L"own_depth", L"Depth for the effects", 1, {L"ReShade's own depth", L"Our own, picked once"},
           L"Where the fog, the sky and the cave ceiling get the scene's depth. Our own (the default): the game's scene "
           L"depth-stencil, picked once and kept until it goes away or the window or render size changes, copied for each eye before its "
           L"effects - one copy on the graphics card an eye, nothing for the game to wait on. Needed in a square window and "
           L"with a render size: there ReShade alone takes the 4096 shadow map and the fog flickers with a view from above. "
           L"ReShade's own: ReShade's Generic Depth picks it (fine in a window that is not square). VR only. Live."),
    Status(L"depth_pick", L""),
    Needs(L"render", L"depth_fix", 1, Choice(L"render", L"depth_fix_mode", L"How that depth is set", 1,
           {L"Off (the game's own)", L"In each pass as it is bound", L"Once a frame where the game makes it"},
           L"In each pass as it is bound (D2R VR's way, as before): every constant buffer the game binds gets our depth range - a pass "
           L"that gets its constants another way keeps the game's. Once a frame where the game makes it (found by bearstonem/d2r-pcvr): the "
           L"range is written right after the game builds the frame's constants, before any pass reads them, so every pass - fog, ambient "
           L"occlusion, particles, grass - reads the same depth; it runs from the near plane of our camera to the game's own far end (the "
           L"distance below is not used). To compare: the act 1 grass, the act 2 town's veil, the haze in the corners. Off: the game's own "
           L"depth, as \"The game's effects measure depth from our camera\" unticked. Live.")),
    Needs(L"render", L"depth_fix", 1, Needs(L"render", L"depth_fix_mode", 1, Slider(L"render", L"depth_range", L"How far that depth reaches", 200, 5000, 50, 1000,
           L"World units (the hero is ~7.5 tall). Farther counts as sky to the game's fog; shorter is finer near the hero."))),
    Choice(L"render", L"solid_walls", L"Walls in front of the hero", 1, {L"See-through (the game's)", L"Solid, see-through only in F1", L"Solid in every view"},
           L"The game draws a wall between its camera and the hero at half alpha. From inside every wall you look at "
           L"was see-through; from above it hid the hero."),
    Toggle(L"render", L"keep_roofs", L"Keep the roof when you walk into a building (F4)", 0,
           L"For its camera high above the game takes away the roof of a building the hero walks into (Kurast Docks, the "
           L"town's huts); from the eyes that showed the sky through the ceiling. On: the game's request to fade the roof out "
           L"is answered with the roof kept, in VR F4 only - F1, F2, F3 keep the game's cut-aways. Nothing else that fades is "
           L"touched. Idea: bearstonem/d2r-pcvr. Off: the game's own."),
    Toggle(L"render", L"roofs_two_sided", L"Roofs: their underside, not the sky", 0,
           L"The game draws a roof only from above, so from under a kept roof the sky showed through it. On: the materials of the "
           L"roofs (a roof model's material named \"roof\") are built double-sided, and their underside is drawn. A material is "
           L"built when its area loads: the change shows in the next area you go to (what is loaded now keeps its sides until the "
           L"game loads it again, at the latest the next start). The same for the walls and stairs of the Kurast sewer entrances "
           L"(Act 3), whose backs showed the sky from the stairs. VR only. Nothing else is changed. From bearstonem/d2r-pcvr (MIT). "
           L"Off: the game's own."),
    Toggle(L"render", L"light_bounds_fix", L"Fix the game's lighting at a square or wide window", 1,
           L"The game cuts the picture into 32 x 20 tiles and finds each tile's depth range for its lights with a shader that "
           L"is right only at a 16:10 window. At a square or wide one (the VR window, 16:9) it misses rows: lights vanish in hard "
           L"rectangles and a roof's shading flickers as the head moves. On: our own shader does that one job right while VR "
           L"renders (at 16:10 the numbers are the same as the game's). Off at once; ticked on again it takes effect at the "
           L"next start of the game. Off: the game's own."),
    Needs(L"render", L"light_bounds_fix", 1, Toggle(L"render", L"light_bounds_flat", L"... in flat mode too", 1,
           L"The same fix in flat mode (a monitor or a window, native OpenXR's flat picture too): the game's shader is wrong at "
           L"16:9 and at a square window as well, not only in VR. Follows the mode at once. Off: flat mode keeps the game's own "
           L"shader, as before.")),
    Group(L"Render distance"),
    Slider(L"render", L"rings", L"Room rings around the hero", 1, 8, 1, 4, L"Extra room rings around the hero. With fog, 4-6 is enough."),
    Slider(L"render", L"model_radius", L"Model visibility radius", 150, 1000, 50, 200, L"The game's own is 150."),

    Tab(L"Sky"),
    Group(L"Sky"),
    Toggle(L"sky", L"enabled", L"Sky outdoors (via ReShade)", 0,
           L"A sky in the black void beyond the world, through the fog shader. Only in the open air; "
           L"hidden while a menu or side panel is open."),
    Needs(L"sky", L"enabled", 1, Slider(L"sky", L"clouds", L"Clouds (0 = clear)", 0, 2, 0.05f, 1, L"Times each area's own cloud cover.")),
    Needs(L"sky", L"enabled", 1, Toggle(L"sky", L"drift", L"The sky turns slowly (clouds drift)", 0,
           L"The painted sky goes slowly round you. Off: it stands still - the stars painted into the night skies went round with the clouds.")),
    Toggle(L"sky", L"arcane_sky", L"Arcane Sanctuary: its own stars all round", 1,
           L"In the Arcane Sanctuary the void round the platforms shows the game's own star layers and clouds in every "
           L"direction, below the platforms too (after bearstonem/d2r-pcvr). The pictures are made from your game's files "
           L"when this program opens (Home: Arcane Sanctuary sky) and live inside the mod (mods\\D2R-VR); nothing of the game is changed. Not in the views from "
           L"above. Works with the outdoor sky off too. Off: the void as the game draws it."),
    Needs(L"sky", L"arcane_sky", 1, Toggle(L"sky", L"arcane_fog", L"Arcane Sanctuary: fog fades into its sky", 0,
           L"The distance fog ([fog] on, the Fog tab) in the Arcane Sanctuary too: the far platforms dissolve into the sky behind "
           L"them - its dark and its nebula, not its stars. Off: no fog under that sky, the platforms sharp to the end.")),
    Group(L"Day and night"),
    Toggle(L"sky", L"day_night", L"Sky follows the game's day and night", 1,
           L"The sky and the outdoor fog darken at dusk and brighten at dawn with the game's own light, at the same pace."),
    Needs(L"sky", L"day_night", 1, Slider(L"sky", L"night_brightness", L"Sky at night", 0, 1, 0.01f, 0.25f,
           L"How bright the sky and the fog stay at full night, times their day brightness.")),
    Needs(L"sky", L"day_night", 1, Slider(L"sky", L"light_day", L"Game light that is full day", 1, 255, 1, 128,
           L"The game's light runs 0..255 (vrcam's log writes it as it changes). From this up: full day.")),
    Needs(L"sky", L"day_night", 1, Slider(L"sky", L"light_night", L"Game light that is full night", 0, 254, 1, 65,
           L"From this down: full night. In between the sky goes from one to the other.")),
    Group(L"Act 1 (night, storm)"),
    Slider(L"sky", L"brightness_act1", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_act1", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_act1.png", kSkyPicture),
    File(L"sky", L"cap_act1", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_act1.png", kSkyPicture),
    Color(L"fog", L"color_act1", L"Fog colour", L"from the sky", kFogColour),
    Group(L"Act 2 (desert)"),
    Slider(L"sky", L"brightness_act2", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_act2", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_act2.png", kSkyPicture),
    File(L"sky", L"cap_act2", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_act2.png", kSkyPicture),
    Color(L"fog", L"color_act2", L"Fog colour", L"from the sky", kFogColour),
    Group(L"Act 3 (jungle)"),
    Slider(L"sky", L"brightness_act3", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_act3", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_act3.png", kSkyPicture),
    File(L"sky", L"cap_act3", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_act3.png", kSkyPicture),
    Color(L"fog", L"color_act3", L"Fog colour", L"from the sky", kFogColour),
    Group(L"Act 4 (Hell)"),
    Slider(L"sky", L"brightness_act4", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_act4", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_act4.png", kSkyPicture),
    File(L"sky", L"cap_act4", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_act4.png", kSkyPicture),
    Color(L"fog", L"color_act4", L"Fog colour", L"from the sky", kFogColour),
    Group(L"Act 5 (ruins)"),
    Slider(L"sky", L"brightness_act5", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_act5", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_act5.png", kSkyPicture),
    File(L"sky", L"cap_act5", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_act5.png", kSkyPicture),
    Color(L"fog", L"color_act5", L"Fog colour", L"from the sky", kFogColour),
    Group(L"Act 5 (snow)"),
    Slider(L"sky", L"brightness_snow", L"Brightness", 0, 2, 0.05f, 1),
    File(L"sky", L"texture_snow", L"Sky panorama", L"D2R_Sky_ours\\D2R_Sky_snow.png", kSkyPicture),
    File(L"sky", L"cap_snow", L"Zenith", L"D2R_Sky_ours\\D2R_SkyCap_snow.png", kSkyPicture),
    Color(L"fog", L"color_snow", L"Fog colour", L"from the sky", kFogColour),

    Tab(L"Ceiling"),
    Group(L"Cave ceiling"),
    Toggle(L"ceiling", L"enabled", L"Ceiling in caves (via ReShade)", 0,
           L"A stone vault over the black void in caves, at its true depth in both eyes. "
           L"First person only; for now act 1's caves, crypts, barracks, catacombs and cathedral and act 2's sewers and palace."),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"scale", L"Tile size", 1, 100, 0.5f, 10,
           L"World units per tile of the relief; the stone picture spans 4 tiles.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"steps", L"Relief steps", 4, 32, 1, 12,
           L"Steps of the ray through the rock: more = finer relief, a dearer frame.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wall_distance", L"Far wall distance (0 = none)", 0, 600, 5, 200,
           L"A wall of the same stone this far round the hero, world units: the black void past the floor's edge is closed. "
           L"It sinks in the fog like the rest.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"floor_depth", L"Floor below the floor (0 = none)", 0, 100, 0.5f, 5,
           L"Stone this far under the hero's floor, world units: looking down past the drawn floor's edge shows it, not the void.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast", L"Stone contrast", 0, 4, 0.05f, 1.6f,
           L"The picture's fine detail times this: more = crisper, harder rock, like the walls.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail", L"Detail relief", 0, 4, 0.05f, 0.8f,
           L"How far the picture's grains and cracks stand out of the rock: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet", L"Wet shine", 0, 2, 0.05f, 0.35f,
           L"Glints of the hero's and the torches' light on wet rock, as on the walls (0 = dry).")),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"torches", L"Torches light the ceiling (CPU heavy!)", 0,
           kTorchesTip)),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"fire_from_picture", L"Any fire lights the ceiling (ReShade, from the picture)", 0,
           kPictureFireTip)),
    NeedsEither(L"ceiling", L"torches", L"fire_from_picture", 1, Slider(L"ceiling", L"torch_brightness", L"Torch glow", 0, 3, 0.05f, 0.6f,
           L"How bright the torches' light on the ceiling is. A small flame gives less light than a big one.")),
    NeedsEither(L"ceiling", L"torches", L"fire_from_picture", 1, Slider(L"ceiling", L"torch_radius", L"Torch light reach", 3, 100, 1, 20,
           L"World units from a fire where its light on the ceiling is down to half.")),
    Needs(L"ceiling", L"fire_from_picture", 1, Slider(L"ceiling", L"fire_nearer", L"Fire light nearer to you", 0, 60, 0.5f, 0,
           L"A flame is drawn without depth, so the fire found in the picture is put where the wall or the floor behind "
           L"it is, and its light on the ceiling sits behind the fire. This brings it nearer to you, world units along "
           L"your line of sight to the flame (the hero is ~7.5 tall). 0 = where the depth says.")),
    Needs(L"ceiling", L"torches", 1, Slider(L"ceiling", L"torch_warmth", L"Torch warmth (yellow)", 0, 1, 0.05f, 0.6f,
           L"0 = the game's own pale torch colour; more = the orange-yellow of fire on the stone.")),
    Needs(L"ceiling", L"torches", 1, Slider(L"ceiling", L"torch_distance", L"Torch light distance", 10, 300, 5, 80,
           L"Only torches this near the hero light the ceiling; farther ones fade out - those of the next hall shone over the walls.")),
    Needs(L"ceiling", L"torches", 1, Toggle(L"ceiling", L"monster_lights", L"Monsters with fire light the ceiling", 0,
           L"The torches the Fallen carry and their shamans' fire. Off: the monsters are not looked at at all.")),
    Needs(L"ceiling", L"monster_lights", 1, Slider(L"ceiling", L"monster_brightness", L"Monsters' fire glow", 0, 2, 0.05f, 0.35f,
           L"The torches the Fallen carry and their shamans' fire, times the torches' glow. 0 = none.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"torch_halo", L"Torch halo in caves", 0, 1, 0.05f, 1,
           L"How much of the game's own glow round a flame shows over the ceiling and the far floor (1 = all). "
           L"The flame itself always stays.")),
    Group(L"Light in caves"),
    Toggle(L"cave_light", L"enabled", L"Brighter caves and dungeons", 0,
           L"The game's own light in caves, dungeons and crypts (every area that is not outdoors), given before the game "
           L"makes its colours, so they keep their strength - not a lifted black level. Underground there is no sun: the "
           L"hero's circle of light, the torches and the light that fills the place, which lights only inside that circle. "
           L"Drawing only: the light radius and everything the game plays by stay as they are. For a headset that loses "
           L"the darkest shades (an LCD, such as Meta Quest); an OLED (PS VR2) shows them as they are. Works without the ceiling."),
    Needs(L"cave_light", L"enabled", 1, Slider(L"cave_light", L"hero_reach", L"Hero's light circle (times the game's)", 0.5f, 10, 0.05f, 1.5f,
           L"How far the hero's own light reaches, times the game's: a wider circle round him. 1 = as the game has it.")),
    Needs(L"cave_light", L"enabled", 1, Slider(L"cave_light", L"hero_power", L"Hero's light strength (times the game's)", 0.25f, 50, 0.05f, 1,
           L"How bright the hero's own light is, times the game's. 1 = as the game has it.")),
    // hidden: in the headset it changed nothing (the user, 2026-10-13); the key stays for a later look
    Hide(Needs(L"cave_light", L"enabled", 1, Slider(L"cave_light", L"hero_falloff", L"Hero's light falloff (times the game's)", 0.05f, 3, 0.05f, 1,
           L"The game's attenuation of the hero's light (5.5), times this: how it fades with the distance. Past about 10 "
           L"the circle above grows no wider - the light has faded before its edge; this changes that fading. 1 = as the game has it."))),
    Needs(L"cave_light", L"enabled", 1, Slider(L"cave_light", L"ambient", L"Fill light (times the area's own)", 0.5f, 8, 0.1f, 2,
           L"The light that fills the place, inside the hero's circle: brighter, not wider. 1 = as the game has it. Each "
           L"dungeon has its own: the act 2 tombs 0.12, the act 2 sewers 0.5, crypts and catacombs 1, act 1's caves 2, "
           L"the maggot lair 12.5.")),
    Needs(L"cave_light", L"enabled", 1, Slider(L"cave_light", L"ambient_min", L"Fill light at least", 0, 5, 0.05f, 0,
           L"Whatever the dungeon has, at least this much: only for the darkest ones (the act 2 tombs: 0.12). Where the "
           L"area's own times the slider above is more already (act 1's caves: 2 x 2 = 4), it does nothing. 0 = no floor.")),
    Group(L"Ceiling: act 1 caves"),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"torches", L"Torches light the ceiling (CPU heavy!)", 0,
           kTorchesTip)),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"fire_from_picture", L"Any fire lights the ceiling (ReShade, from the picture)", 0,
           kPictureFireTip)),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_caves", L"Height over the floor", 5, 150, 0.1f, 30,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_caves", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_caves", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_caves", L"Relief (0 = flat)", 0, 30, 0.05f, 6,
           L"How far the uneven rock hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act1_caves", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_caves", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_caves_walls.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act1_caves", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act1_caves", L"Stone contrast", 0, 4, 0.05f, 1.6f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act1_caves", L"Detail relief", 0, 4, 0.05f, 0.8f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act1_caves", L"Wet shine", 0, 2, 0.05f, 0.35f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
    Group(L"Ceiling: act 1 crypts"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_crypt", L"Height over the floor", 5, 150, 0.1f, 30,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_crypt", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_crypt", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_crypt", L"Relief (0 = flat)", 0, 30, 0.05f, 1.5,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act1_crypt", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_crypt", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act1_crypt", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act1_crypt", L"Stone contrast", 0, 4, 0.05f, 1.6f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act1_crypt", L"Detail relief", 0, 4, 0.05f, 0.8f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act1_crypt", L"Wet shine", 0, 2, 0.05f, 0.35f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),

    Group(L"Ceiling: act 1 barracks"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_barracks", L"Height over the floor", 5, 150, 0.1f, 14,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_barracks", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_barracks", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_barracks", L"Relief (0 = flat)", 0, 30, 0.05f, 1.5,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act1_barracks", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_barracks", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act1_barracks", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act1_barracks", L"Stone contrast", 0, 4, 0.05f, 1.6f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act1_barracks", L"Detail relief", 0, 4, 0.05f, 0.8f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act1_barracks", L"Wet shine", 0, 2, 0.05f, 0.35f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
    Group(L"Ceiling: act 1 catacombs"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_catacombs", L"Height over the floor", 5, 150, 0.1f, 20,
           L"World units (the hero is ~7.5 tall). What the game draws higher is cut off by the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_catacombs", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_catacombs", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_catacombs", L"Relief (0 = flat)", 0, 30, 0.05f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act1_catacombs", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_catacombs", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt_brick.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act1_catacombs", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act1_catacombs", L"Stone contrast", 0, 4, 0.05f, 1.2f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act1_catacombs", L"Detail relief", 0, 4, 0.05f, 0.5f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act1_catacombs", L"Wet shine", 0, 2, 0.05f, 0.2f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
    Group(L"Ceiling: act 1 cathedral"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_cathedral", L"Height over the floor", 5, 150, 0.1f, 35,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_cathedral", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_cathedral", L"Hero's light reach", 5, 200, 1, 33,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_cathedral", L"Relief (0 = flat)", 0, 30, 0.05f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act1_cathedral", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_cathedral", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_barracks_smooth.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act1_cathedral", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act1_cathedral", L"Stone contrast", 0, 4, 0.05f, 1.2f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act1_cathedral", L"Detail relief", 0, 4, 0.05f, 0.5f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act1_cathedral", L"Wet shine", 0, 2, 0.05f, 0.2f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"dome_act1_cathedral", L"Dome over what is higher", 1,
           L"Where the game draws something higher than the ceiling (the altar's canopy), the ceiling rises over it as a sphere "
           L"instead of cutting it off.")),
    Needs(L"ceiling", L"dome_act1_cathedral", 1, Slider(L"ceiling", L"dome_find_act1_cathedral", L"Dome over what reaches above", 0, 150, 0.5f, 0,
           L"World units over the floor: only what the game draws higher than this gets a dome (set it between the columns' tops "
           L"and the altar canopy's spire). Under the ceiling's height it is that height. ReShade's Show ceiling cut shows heights.")),
    Needs(L"ceiling", L"dome_act1_cathedral", 1, Slider(L"ceiling", L"dome_radius_act1_cathedral", L"Dome radius", 10, 150, 1, 40,
           L"World units: how wide the sphere over a high thing is.")),
    Needs(L"ceiling", L"dome_act1_cathedral", 1, Slider(L"ceiling", L"dome_max_act1_cathedral", L"Dome height at most", 0, 100, 1, 30,
           L"World units over the ceiling; what stands higher still is cut off.")),
    Group(L"Ceiling: act 2 sewers"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act2_sewer", L"Height over the floor", 5, 150, 0.1f, 35,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act2_sewer", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act2_sewer", L"Hero's light reach", 5, 200, 1, 33,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act2_sewer", L"Relief (0 = flat)", 0, 30, 0.05f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act2_sewer", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act2_sewer", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_barracks_smooth.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act2_sewer", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act2_sewer", L"Stone contrast", 0, 4, 0.05f, 1.2f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act2_sewer", L"Detail relief", 0, 4, 0.05f, 0.5f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act2_sewer", L"Wet shine", 0, 2, 0.05f, 0.2f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"dome_act2_sewer", L"Dome over what is higher", 1,
           L"Where the game draws something higher than the ceiling (the altar's canopy), the ceiling rises over it as a sphere "
           L"instead of cutting it off.")),
    Needs(L"ceiling", L"dome_act2_sewer", 1, Slider(L"ceiling", L"dome_find_act2_sewer", L"Dome over what reaches above", 0, 150, 0.5f, 0,
           L"World units over the floor: only what the game draws higher than this gets a dome (set it between the columns' tops "
           L"and the altar canopy's spire). Under the ceiling's height it is that height. ReShade's Show ceiling cut shows heights.")),
    Needs(L"ceiling", L"dome_act2_sewer", 1, Slider(L"ceiling", L"dome_radius_act2_sewer", L"Dome radius", 10, 150, 1, 40,
           L"World units: how wide the sphere over a high thing is.")),
    Needs(L"ceiling", L"dome_act2_sewer", 1, Slider(L"ceiling", L"dome_max_act2_sewer", L"Dome height at most", 0, 100, 1, 30,
           L"World units over the ceiling; what stands higher still is cut off.")),
    Group(L"Ceiling: act 2 palace"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act2_palace_clean", L"Height over the floor", 5, 150, 0.1f, 20,
           L"World units (the hero is ~7.5 tall). What the game draws higher is cut off by the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act2_palace_clean", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act2_palace_clean", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act2_palace_clean", L"Relief (0 = flat)", 0, 30, 0.05f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_pic_act2_palace_clean", L"Relief from the picture", -1, 2, 1, 0,
           L"What stands out of the ceiling by Relief: 0 lumpy noise; 1 the picture's light parts; -1 its dark parts; "
           L"2 its coloured parts (bricks in grey mortar). Needs a picture.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act2_palace_clean", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt_brick.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = the common picture ([ceiling] texture, the act 1 caves' stone); missing too, the shader's own stone.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"tex_size_act2_palace_clean", L"Picture size (0 = 4 tiles)", 0, 200, 1, 0,
           L"World units one picture spans: smaller = finer bricks or stones. 0 = four tiles (Tile size above).")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"contrast_act2_palace_clean", L"Stone contrast", 0, 4, 0.05f, 1.2f,
           L"The picture's fine detail times this: more = crisper, harder stone; less = smoother.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"detail_act2_palace_clean", L"Detail relief", 0, 4, 0.05f, 0.5f,
           L"How far the picture's grains and cracks stand out of the stone: they catch the light.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"wet_act2_palace_clean", L"Wet shine", 0, 2, 0.05f, 0.2f,
           L"Glints of the hero's and the torches' light on the stone (0 = dry).")),
#if D2RVR_FIRST_PERSON
    Only(kVr, Tab(L"Weapon Adjust")),
    Group(L"Weapon in hand"),
    Toggle(L"@ui", L"weapon_follow", L"Follow the weapon in hand", 1,
           L"The sliders below edit the kind of weapon the hero holds now (read from the running game). "
           L"Off: pick the kind yourself."),
    Needs(L"@ui", L"weapon_follow", 0, Choice(L"@ui", L"weapon", L"Weapon", 1,
           {L"Unarmed", L"Bow", L"Crossbow", L"Sword", L"Axe", L"Mace", L"Scepter", L"Wand", L"Staff",
            L"Polearm", L"Spear", L"Dagger", L"Throwing", L"Javelin", L"Claw", L"Orb", L"Other"})),
    Slider(L"@weapon", L"x", L"Move along X, cm", -40, 40, 0.5f, 0,
           L"With the arms on the controllers: the weapon moved in the hand, along its holder's own axes. Kept per kind of weapon."),
    Slider(L"@weapon", L"y", L"Move along Y, cm", -40, 40, 0.5f, 0),
    Slider(L"@weapon", L"z", L"Move along Z, cm", -40, 40, 0.5f, 0),
    Slider(L"@weapon", L"pitch", L"Turn about X, °", -180, 180, 1, 0,
           L"The weapon turned in the hand about its holder's own axes. Kept per kind of weapon."),
    Slider(L"@weapon", L"yaw", L"Turn about Y, °", -180, 180, 1, 0),
    Slider(L"@weapon", L"roll", L"Turn about Z, °", -180, 180, 1, 0),
    Slider(L"hands", L"left_hold_x", L"Left hand when it holds: X, cm", -20, 20, 0.5f, 0,
           L"The left hand once its grip has taken a two-handed weapon (staff, spear, polearm, axe, mace, sword), moved "
           L"along its own axes until the fist sits round the shaft. One setting for all of them; a crossbow has its own."),
    Slider(L"hands", L"left_hold_y", L"Left hand when it holds: Y, cm", -20, 20, 0.5f, 0),
    Slider(L"hands", L"left_hold_z", L"Left hand when it holds: Z, cm", -20, 20, 0.5f, 0),
    Slider(L"hands", L"left_hold_pitch", L"Left hand when it holds: turn about X, °", -180, 180, 1, 0,
           L"The same hand turned about its own axes, round its point on the shaft - the fist stays on it. One for every "
           L"two-handed weapon."),
    Slider(L"hands", L"left_hold_yaw", L"Left hand when it holds: turn about Y, °", -180, 180, 1, 0),
    Slider(L"hands", L"left_hold_roll", L"Left hand when it holds: turn about Z, °", -180, 180, 1, 0),
    Group(L"Shield (first person)"),
    Needs(L"arms", L"mode", 2, Toggle(L"shield", L"forearm", L"Shield strapped to the forearm", 1,
           L"First person (F4): the shield on your left arm is strapped to the forearm as the model was made, not hung on the hand "
           L"from the game's animation - it goes where the forearm goes and turns as it turns (with \"Forearm turns with the wrist\", "
           L"rolling the hand rolls the shield). The weapon's sliders above never move it; the ones below do. A shield bash "
           L"(\"Shield bash casts Smite\") is felt where the shield is drawn. Off: on the hand, as before. Live.")),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"x", L"Shield: move along X, cm", -40, 40, 0.5f, 0,
           L"The shield moved on the forearm, along its own axes."))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"y", L"Shield: move along Y, cm", -40, 40, 0.5f, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"z", L"Shield: move along Z, cm", -40, 40, 0.5f, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"pitch", L"Shield: turn about X, °", -180, 180, 1, 0,
           L"The shield turned on the forearm about its own axes."))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"yaw", L"Shield: turn about Y, °", -180, 180, 1, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Slider(L"shield", L"roll", L"Shield: turn about Z, °", -180, 180, 1, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"shield", L"forearm", 1, Toggle(L"debug", L"shield_blink", L"Blink the strapped shield (a check: does the shield you see move with these?)", 0,
           L"A test, not a setting: the bone the strapped shield hangs on is hidden for half a second in every two. If the shield on the screen blinks, it is the one these sliders, the "
           L"put-away behind the left shoulder and the jump to the other arm act on. If it does not blink, the shield you see hangs on something else - say so. Live."))),
    Only(kVr, Group(L"Attack along the hand (A button)")),
    Choice(L"bow", L"mode", L"Aiming", 4,
           {L"Off", L"Left stick while A is held", L"Shift + click along the hand", L"Turn the hero to the hand first",
            L"Attack vector along the hand"},
           L"Shift + click works, but flips the game's interface to the mouse. "
           L"Turn first: the stick turns the hero to the hand, then A goes through (gamepad only). "
           L"Attack vector: while A is held the game's own attack points along the hand - turn the hand while firing."),
    Needs(L"bow", L"mode", 3, Slider(L"bow", L"turn_ms", L"Turn before the attack, ms", 20, 300, 10, 80,
           L"\"Turn the hero first\" only: how long the hero is turned before the attack. More if he does not turn in time.")),
    Toggle(L"gameplay", L"attack_in_place", L"Attacks never walk the hero (stand still)", 1,
           L"VR first person (F4) only. An attack the plugin presses - the trigger, a swing by touch, a throw, a shot from the bow's draw, "
           L"a skill button when the skill panel knows its skill - is pressed together with the game's own Stand still (the Shift key's "
           L"command), so with nothing under the point it is a swing or a shot in place and the hero never walks forward. The press waits "
           L"a few milliseconds until the game has Stand still down. Skills that move the hero (Teleport, Charge, Leap, Whirlwind, Dragon "
           L"Flight) and picking things up are left alone. While an attack is held the hero stands. A mouse click (or the laser's) on bare "
           L"ground is the same: a skill in place, never a walk there; a click on a monster, item, door or NPC is the game's own. F1, F2, F3 "
           L"(where a click on the floor walks) and the flat modes: as before. Off: attacks and clicks as they were. Live."),
    Toggle(L"gameplay", L"block_stray_clicks", L"No stray mouse clicks in the world", 1,
           L"VR first person (F4) only. While you play in the world with no panel or menu open, the game gets no mouse button at all - a "
           L"click that comes from outside the plugin (a mouse, the headset's or the streaming software's controller-to-mouse, a driver: the "
           L"log says 'from a device') used to run the hero forward in the middle of a bow shot. Panels, menus, the title and character "
           L"screens (the laser clicks there), mouse look and [bow] mode 2's own Shift + click are untouched. F1, F2, F3 and the flat modes: as "
           L"before. Off: every click reaches the game. Live. The log names the origin and the controllers' trigger of each dropped click."),
    Toggle(L"gameplay", L"block_stray_walk", L"No stray walks (a run to a point out of a click)", 1,
           L"VR first person (F4) only. The game's client sends every command of the player through one funnel; a walk or run to a POINT "
           L"that comes out of a click on bare ground is dropped there, before the hero starts it. Never touched: the left stick's walk, "
           L"walks to a monster, item, door, NPC or waypoint, every skill (Teleport, Leap, Charge are skills), the stick's stop and the "
           L"game's Force Move. A walk to a point from any other place is only written to the log. F1, F2, F3 (where a click on the floor "
           L"walks) and the flat modes: as before. Off: commands as they were. Live."),
    Needs(L"gameplay", L"block_stray_walk", 1, Toggle(L"gameplay", L"block_attack_run", L"An attack never runs the hero off (a bow shot, a blow, a throw)", 0,
           L"VR first person (F4) only. A shot aimed at a point in the air made the game RUN the hero toward that point, in the middle of the shot, even with "
           L"Stand Still held (the log shows the run starting 3 ms after the pad's A went down). With this on, a walk or run to a point out of the pad's cast "
           L"of an attack of ours is dropped; the shot itself, its animation and damage are the game's. Walks to a monster, an item or a door are never touched. "
           L"Off (the default): such a run is only written to the log. CAUTION: when the game turns an aimed shot into that run, dropping it leaves NO shot - the cause is being fixed. Live.")),
    Toggle(L"gameplay", L"aim_no_chase", L"A shot along the hand never sends the hero after a monster", 1,
           L"VR first person (F4) only. Why the game ran the hero off after a shot: before every cast it asks the pad's target (the monster nearest the aim); with one "
           L"found, it takes Stand Still off the cast so the hero can attack that monster. A shot along the hand has its target taken away (that is the aim), "
           L"and what is left is a click on the ground: a run to the point. With this on, that first question gets no target either, so the cast keeps Stand Still "
           L"and is the same shot in place as with no monster near. Aim point, damage, animation and rate stay the game's. Off: as before. Live."),
    Group(L"Bow"),
    Only(kVr, Toggle(L"hands", L"bow_draw", L"Bow: draw the string with both hands", 0,
           L"Native OpenXR: with a bow in hand, press the trigger of the hand without the bow at the bow's nock point (about 16 cm "
           L"behind its grip), pull back along the line to the bow and let go - the game's own button is pressed once, aimed along the "
           L"draw line. The game does the rest: its animation, attack rate, mana and arrows; the length of the pull adds nothing. "
           L"A trigger pressed away from the string shoots nothing (the next box) or, with that box unticked, is the plain trigger "
           L"as before. Needs native OpenXR and the first person view (F4): the other views keep everything as before. Off: the "
           L"trigger as before. Live.")),
    Needs(L"hands", L"bow_draw", 1, Toggle(L"hands", L"bow_draw_only", L"Bow: a press away from the string shoots nothing", 1,
           L"On: with a bow in hand (F4) the bow shoots only by a draw and a release. In the green grab zone the free hand's trigger "
           L"(and the grip, if it starts the draw) is the draw's: nothing you bound to it in the controls (a pad button, a key command, "
           L"an action) fires there - at the press, during the pull or at the release. Away from the zone only its shots are dropped - "
           L"the game's attack, pad A and RT, and the skill the skill wheel picked - and everything else bound to it works as bound. "
           L"The draw's own release still presses its attack or skill button. Other weapons, other views and the menus are as before. "
           L"Off: a trigger pressed away from the string is the plain trigger as before. Live.")),
    Needs(L"hands", L"bow_draw", 1, Choice(L"hands", L"bow_draw_button", L"Bow draw: the release presses", 0,
           {L"The attack", L"The skill on B", L"The skill on X", L"The skill on Y", L"The skill on RB",
            L"The skill on LT + B", L"The skill on LT + X", L"The skill on LT + Y", L"The skill on LT + RB"},
           L"The attack: a bow's own arrow. A skill button: that slot of the controller's skill bar, whatever skill is on it - "
           L"Magic, Fire, Cold, Ice, Exploding, Immolation, Freezing or Guided Arrow, Multiple Shot (its fan is the game's), "
           L"Strafe (its own volley). Pressed once, 65 ms, as a thumb would. Live.")),
    Needs(L"hands", L"bow_draw", 1, Choice(L"hands", L"bow_draw_start", L"Bow draw: the string is taken with", 0,
           {L"The trigger", L"The grip", L"The trigger or the grip"},
           L"The trigger: as before. The grip: the natural grab - squeeze it at the string, pull, let go to shoot; a grip pressed "
           L"away from the string keeps its other duties (the belt's potions, the shoulder swap, the wheel); at the string it is "
           L"the draw's. Both: whichever is pressed first, and the draw is that one's. Live.")),
    Needs(L"hands", L"bow_draw", 1, Choice(L"hands", L"bow_draw_nock", L"Bow draw: where the string is", 1,
           {L"16 cm behind the grip", L"The bow's own string"},
           L"The string of the bow in your hand, read off the model where it rests (used when it lies 5 to 45 cm from the grip), "
           L"or a fixed point 16 cm behind the grip along the bow. The touch radius round it is 28 cm either way. "
           L"Debug > \"Draw the bow draw's vectors\" shows both points. Live.")),
    Needs(L"hands", L"bow_draw", 1, Slider(L"hands", L"bow_draw_reach_cm", L"Bow draw: grab zone radius, cm", 5, 50, 1, 28,
           L"The radius of the green zone round the string (Debug > \"Draw the bow draw's vectors\" draws it at exactly this size, red "
           L"outside, green once the free hand is in): the free hand has to be inside it when the trigger or grip is pressed. 28 = as it "
           L"was. Smaller = nearer the string (a press elsewhere is not a draw more often). Live.")),
    Needs(L"hands", L"bow_draw", 1, Slider(L"hands", L"bow_draw_nock_shift_cm", L"Bow draw: grab zone shift, cm", -25, 25, 1, 0,
           L"Moves the centre of the green zone along the bow's axis: + away from the bow's grip (further toward you), - toward the grip. "
           L"For the bow's own string and the fixed point alike - set it with the debug vectors on until the zone sits on the string you "
           L"see. 0 = as it was. Live.")),
    Needs(L"hands", L"bow_draw", 1, Slider(L"hands", L"bow_draw_min_pull_cm", L"Bow draw: the least pull that shoots, cm", 5, 40, 1, 12,
           L"How far the string hand has to be pulled back from where it took the string for a release to shoot (it also has to be 16 cm "
           L"+ this far from the bow hand, and held for 0.12 s). 12 = as it was; less = a short pull shoots, more = a longer one is "
           L"needed. A shorter pull shoots nothing and the bow goes back to rest. Debug > \"Draw the bow draw's vectors\" draws a ring "
           L"round the bow hand at the radius from which a release shoots: red until the free hand has pulled far enough, green "
           L"after. Live.")),
    Toggle(L"bow", L"rig_pose", L"The bow's string and limbs follow the drawing hand", 0,
           L"On: with a bow in hand, while the string is drawn (the draw gesture), the game's own bow bones are posed from your "
           L"hands - the string at the hand, the limbs folded to it, the arrow on the string - and snap back at the release. "
           L"No bow is drawn over the game's: it is the game's bow, its bones moved; the shot, its aim and damage are the game's. "
           L"Any bow a hero holds (15 models); a rig it does not know keeps the game's animation. Off: the game's own animation. F4 only."),
    Toggle(L"bow", L"aim_along_hands", L"The bow lies along the line between the hands", 0,
           L"On: while the string is taken, the bow, its arrow and the bow hand are turned until the bow points along the line from the "
           L"string hand through the bow hand - the line the shot goes along - as a real bow does, instead of sitting in the hand turned "
           L"the way the controller is. The hand and the forearm turn with it. It comes in and goes out smoothly at the take, the "
           L"release and a cancel. The shot itself is as before. Off: the bow is turned like the controller. F4 only."),
    Needs(L"bow", L"aim_along_hands", 1, Slider(L"bow", L"aim_blend_ms", L"Bow along the hands: blend, ms", 50, 400, 10, 120,
           L"How long the bow takes to swing onto the line when the string is taken, and back when it is let go or the pull is "
           L"cancelled. Live.")),
    Needs(L"bow", L"aim_along_hands", 1, Choice(L"bow", L"aim_roll", L"Bow along the hands: the bow's roll", 0, {L"Follow the controller's roll", L"Keep the bow upright"},
           L"About its own axis: the roll the bow controller has, or the limbs kept as upright as the aim lets them be. Live.")),
    Needs(L"bow", L"rig_pose", 1, Slider(L"bow", L"arrow_rest_x_cm", L"Arrow rest X: along the bow, cm", -10, 10, 0.5f, 0,
           L"The point the nocked arrow passes the bow hand at is FIXED in the hand's own frame, so it does not slide about as the hands "
           L"move (now on the hand as it should be, now through the middle of the palm): this far from the hand's reference point. X: "
           L"along the bow - + toward the target. The arrow, and the bow lying along the hands, lie on the line from the string hand through "
           L"that point. Debug > \"Draw the bow draw's vectors\" shows it as an orange dot. All three 0 = through the reference point itself, "
           L"as it was. It comes in and goes out with the draw over the blend time above. Live. F4 only.")),
    Needs(L"bow", L"rig_pose", 1, Slider(L"bow", L"arrow_rest_y_cm", L"Arrow rest Y: across the bow, cm", -10, 10, 0.5f, 0,
           L"Across the bow, the arrow's rest point on the bow hand: + toward the archer's left (the bow arm's side, for a bow held upright), "
           L"- toward the right. See X above. Live.")),
    Needs(L"bow", L"rig_pose", 1, Slider(L"bow", L"arrow_rest_z_cm", L"Arrow rest Z: up off the hand, cm", -10, 10, 0.5f, 5,
           L"Up the bow's limbs, the arrow's rest point on the bow hand: + toward the upper limb, off the hand - the arrow lies above the "
           L"knuckle and the thumb like on a real bow's shelf instead of through the palm. 5 is about the top of the fist. See X above. Live.")),
    Needs(L"bow", L"rig_pose", 1, Toggle(L"bow", L"arrow_rest_clear", L"Keep the arrow clear of the hand and the forearm", 1,
           L"On: where the arrow's line would still pass through the bow hand, the wrist or the forearm - the string hand low or far to a side, "
           L"the hand turned out - the arrow, and the nock with it, is lifted off the arm up the bow's limbs, by as little as clears it and "
           L"at most 8 cm, parallel to the line (so the aim is not changed). Off: the point exactly as the three offsets put it. Live. F4 only.")),
    Needs(L"bow", L"rig_pose", 1, Toggle(L"bow", L"arrow_rest_aims", L"The shot goes through the arrow's rest point too", 1,
           L"On: the shot's aim is the line from the string hand through the arrow's rest point - the line the arrow lies on and the bow "
           L"is turned along, so the shot goes where the arrow points. Off: the shot as before - through the bow hand's reference point. "
           L"Only matters while an offset above is not 0. Live. F4 only.")),
    Toggle(L"bow", L"hand_pose", L"The hand that takes the string: relaxed, fingers hooked on it", 0,
           L"On: with a bow in hand, the hand that takes the string is relaxed (not the game's animation of it) and its fingers close "
           L"on the string, the thumb loose: they follow the controller's trigger and grip, and the moment the string is taken they are "
           L"hooked whatever the controls read; let go, they open. Without the draw gesture, as its grip closes. The bow hand keeps "
           L"the game's animation. Off: the game's animation, as before."),
    Needs(L"bow", L"hand_pose", 1, Toggle(L"bow", L"hook_game_pose", L"String hand: the game's own draw pose", 1,
           L"On: the moment the string is taken the hand takes the pose of the game's own bow-draw animation - the fingers hooked on the "
           L"string (the knuckles open, the last two joints of each finger curled), the thumb turned in - held as it is whatever the "
           L"controller's grip and trigger read, and let go on the release. Not a fist. The Amazon's right hand only; any other keeps "
           L"the closing below. Off: the hand closes by the amount below, as before. Live.")),
    Needs(L"bow", L"hand_pose", 1, Slider(L"bow", L"hook_close", L"String hand: how far the fingers close, %", 0, 100, 5, 85,
           L"The bend of the fingers on the string, 100 = a full fist's, from the same table the free hand's fist uses (it looks like a fist "
           L"- the game's own pose above does not). Used with the game's draw pose off, for a rig that pose is not measured for, and "
           L"for the squeeze before the string is taken. The thumb stays loose.")),
    Needs(L"bow", L"hand_pose", 1, Needs(L"bow", L"hook_game_pose", 1, Slider(L"bow", L"hook_curl_pct", L"String hand: finger curl, % of the game's pose", 0, 150, 5, 100,
           L"How far the four fingers bend in the game's draw pose: 100 = exactly as the game's own animation has them (the last two joints of "
           L"each finger curled round the string), 0 = the open hand, more = tighter. For looking at it; the thumb has its own slider. Live."))),
    Needs(L"bow", L"hand_pose", 1, Needs(L"bow", L"hook_game_pose", 1, Slider(L"bow", L"hook_thumb_deg", L"String hand: thumb in (+) / out (-), °", -60, 60, 1, 0,
           L"The thumb's base turned further in toward the palm and the fingers (+) or out (-) than the game's own draw pose has it, degrees. "
           L"0 = as the game's animation. Live."))),
    Toggle(L"bow", L"string_anchor", L"The string is held at the fingers' first joints", 1,
           L"On: while the string is drawn it runs to the first joints of the index, middle and ring fingers instead of to the controller's grip point in the middle of "
           L"the palm, a hand's length behind them (the string then met the base of the hand and hooked nothing). With the string hand's pose on (above) the joints are "
           L"those of the hooked pose - the game's own draw pose, the Amazon's right hand; with it OFF they are those of the fingers as the game animates them, which "
           L"for a bow in hand is the hook already. The arrow's nock and its line, and the bow's turn along the hands, start there. It comes in and goes out with the "
           L"draw; the three offsets below move it. Off: the grip point, as before. Live. F4 only."),
    Needs(L"bow", L"string_anchor", 1, Slider(L"bow", L"string_anchor_along_cm", L"String at the fingers: along them, cm", -15, 15, 0.5f, 0,
           L"The string's place moved along the fingers: + toward the fingertips, - toward the wrist. 0 = at the middle of the first joints of "
           L"index, middle and ring. Live.")),
    Needs(L"bow", L"string_anchor", 1, Slider(L"bow", L"string_anchor_across_cm", L"String at the fingers: across them, cm", -15, 15, 0.5f, 0,
           L"The string's place moved across the hand: + toward the thumb and the index finger, - toward the little finger (the same on both "
           L"hands). Live.")),
    Needs(L"bow", L"string_anchor", 1, Slider(L"bow", L"string_anchor_palm_cm", L"String at the fingers: out of the palm, cm", -15, 15, 0.5f, 0.8f,
           L"The string's place moved out of the palm, the way the fingers curl: + into the crook of the curled fingers, - toward the back of "
           L"the hand. 0.8 puts it on the finger's inner side, not through the middle of the bone. Live.")),
    Toggle(L"bow", L"quick_release", L"Quick release: the arrow leaves earlier in the attack (changes the game's timing)", 0,
           L"A CHANGE OF THE GAME'S TIMING (off when the key is missing; on in the settings that come with the mod). The game makes a bow's arrow at one frame of its attack animation (for an Amazon frame 6 of 14: "
           L"0.24 s after the animation starts, ~0.3 s after the release). On: that frame is moved earlier in the game's animation table, so the arrow is created "
           L"~0.2 s sooner INSIDE THE SAME ATTACK: the animation's length and the attack rate stay (the next attack cannot start earlier), the damage, mana and "
           L"arrows are the game's, but they happen earlier, and a monster's hit in those first ~0.2 s no longer cancels the shot (it has left already). "
           L"Only in VR first person with a bow in hand, and only offline (this game's own tables); it is put back exactly when you switch it off, change weapon "
           L"or view, or leave the game. Live. [debug] bow_latency shows the arrow's create time."),
    Needs(L"bow", L"quick_release", 1, Slider(L"bow", L"quick_release_frame", L"Quick release: the frame the arrow is made at", 1, 12, 1, 2,
           L"The animation frame (a frame is 0.04 s) the arrow is made at instead of the original (6 for an Amazon's bow; 7 to 11 for the others). 1 = the "
           L"arrow leaves at once, 2 = as soon as the draw starts. A value at or past the original changes nothing. Live.")),
    Toggle(L"bow", L"quiver", L"Quiver: pick the arrow of the next shot (a bow)", 0,
           L"On: the bow skill the next shot fires - Magic, Fire, Cold, Exploding, Ice, Guided, Freezing or Immolation Arrow, Multiple Shot, Strafe - is picked "
           L"without the skill panel, from the skills the hero has learned (points or items; none learned: a short buzz and nothing else). It is the game's own skill "
           L"setter, the one the skill panel uses: the same as picking the skill there, on the slot the shot presses (a bow's release: \"Bow draw: the release presses\" "
           L"below). The arrow glows in the skill's colour while the string is drawn and the controller ticks at each change. The shot is the game's own - mana, "
           L"arrows, attack rate unchanged. With the string draw: a button of the string hand while the string is drawn (B = the next, A = the previous; the [direct] "
           L"quiver_next / quiver_prev lines of the ini change that), or the energy orbs along the bow below. Idea from the fork d2r-pcvr (bearstonem, MIT). "
           L"Needs the game's controller layout. F4 only. Live."),
    Needs(L"bow", L"quiver", 1, Toggle(L"bow", L"quiver_orbs", L"Quiver: energy orbs along the bow - touch one to pick the arrow", 0,
           L"On: with a bow in hand, a row of glowing orbs hangs along its stave - one for the plain shot and one for every bow skill the hero has learned (points or "
           L"items), each in its skill's colour; the skill the next shot fires is the bigger, brighter one. Reach the free hand to an orb and pull its trigger (below: or "
           L"squeeze the grip): that skill is the next shot's (the game's own skill setter, as picking it in the skill panel; no direct cast). At an orb the trigger is the "
           L"orb's - no shot, no draw - and the string is not taken there; anywhere else the trigger and the string are as always. The orbs sit on the bow's own stave "
           L"(read off its bones, so they follow the bow) and never go: at rest, while the string is drawn, at the release (nothing is taken from them then). Needs the game's controller layout and the effect D2R_DepthFog.fx of "
           L"this version. F4 only. Live.")),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Toggle(L"bow", L"quiver_orb_trigger", L"Orbs: pick with the trigger (off: with the grip)", 1,
           L"On: the free hand's TRIGGER at an orb picks it; the grip does nothing there. The trigger a press took an orb with stays the orb's until you let go, so moving "
           L"the hand off the orb with it down does not start the game's attack. Off: the grip picks, as the orbs first did (the trigger is then the string's and the "
           L"game's as ever). Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_size_cm", L"Orbs: size (radius), cm", 0.8f, 6.0f, 0.1f, 2.2f,
           L"How big each orb is drawn: its radius. The orb the skill slot holds is a quarter bigger, the one your hand is at is 1.6 times. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_reach_cm", L"Orbs: grab radius, cm", 2.0f, 20.0f, 0.5f, 7.5f,
           L"How near your hand's grip point (the middle of the palm) has to be to an orb's middle for the orb to be the one your hand is at - it grows and brightens, and the "
           L"grip takes it. Bigger than the orb on purpose: the fingers reach further than the palm's point. Where two reaches overlap the nearer orb wins. The log says how "
           L"far the hand was when it found, left or took an orb. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_start_cm", L"Orbs: start up the stave, cm", 0.0f, 60.0f, 1.0f, 18.0f,
           L"Where the first orb (the plain shot) hangs, measured from the bow's grip along its stave toward the limb that points up. The others follow it up the stave. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_spacing_cm", L"Orbs: distance between orbs, cm", 2.0f, 20.0f, 0.5f, 6.5f,
           L"How far apart the orbs hang along the stave. A row that does not fit between the start and the limb's end is squeezed to fit. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_x_cm", L"Orbs: shift across the bow (x), cm", -20.0f, 20.0f, 0.5f, 0.0f,
           L"The whole row moved across the bow, + to the right of an upright bow. Along the bow's own axes, so it follows the bow when it is tilted. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_y_cm", L"Orbs: shift along the stave (y), cm", -20.0f, 20.0f, 0.5f, 0.0f,
           L"The whole row moved along the stave, + toward the limb that points up (up for an upright bow). Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_z_cm", L"Orbs: shift toward the archer (z), cm", -20.0f, 20.0f, 0.5f, 0.0f,
           L"The whole row moved toward you (+) or away from you (-), on top of the clearance that keeps the orbs off the limb. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_rot_x_deg", L"Orbs: turn about x (across the bow), °", -180.0f, 180.0f, 1.0f, 0.0f,
           L"The row turned about the axis across the bow, on its first orb (where the shifts above put it): + swings the far end of the row toward you, - away from you. The turns "
           L"are about the bow's own axes, so they follow the bow when it is tilted. Applied in the order x, y, z. 0 = the row lies along the stave. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_rot_y_deg", L"Orbs: turn about y (along the stave), °", -180.0f, 180.0f, 1.0f, 0.0f,
           L"The row turned about the stave's own axis, on its first orb. A row lying along the stave does not move for this alone; it swings a row that the x or z turn has "
           L"tilted round the stave (+ toward the right of an upright bow, when the tilt is toward you). Applied after x, before z. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"quiver_orbs", 1, Slider(L"bow", L"quiver_orb_rot_z_deg", L"Orbs: turn about z (toward the archer), °", -180.0f, 180.0f, 1.0f, 0.0f,
           L"The row turned about the axis that points at you, on its first orb: + leans the far end of the row to the left of an upright bow, - to the right. Applied last, "
           L"after x and y. 0 = the row lies along the stave. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"arrow_fx", 1, Toggle(L"bow", L"arrow_fx_needs_mana", L"Arrow effects: out while the mana is short for the chosen arrow", 1,
           L"On: with an arrow skill chosen that the hero cannot pay - by the game's own numbers, the same as for the plain arrow below - the arrow looks plain: the fire, the frost or the arcane "
           L"light goes out (and the small coloured glow), and lights again when the mana is back, at rest as much as while you pull. It is believed after 0.3 s and eases over 0.2 s, so it does "
           L"not flicker when the mana is at the edge. Off: the effect stays whatever the mana. F4 only. Live."))),
    Needs(L"bow", L"quiver", 1, Toggle(L"bow", L"quiver_plain_no_mana", L"Quiver: no mana for the chosen arrow - shoot the plain arrow", 1,
           L"On: with an arrow skill chosen (an orb, a button, the skill panel) and too little mana to pay it, the release is the game's plain attack: a normal arrow flies instead "
           L"of the game's refusal (the hero saying \"not enough mana\" and nothing shot). The skill stays chosen, so when the mana is back the next release is the skill again. Whether the "
           L"hero can pay is the game's own arithmetic on the game's own numbers - the skill's row in Skills.txt and its level against the hero's mana, less what our last cast took if the "
           L"mana read has not caught up - never a guess; a skill this does not know is left to the game. The plain attack is pressed the game's way (its own attack button, the cast reading "
           L"the slot as Attack): its checks, animation and arrows are the game's; nothing is called on the server. Off: the game's refusal, as it is. F4 only. Live.")),
    Needs(L"bow", L"quiver", 1, Toggle(L"bow", L"quiver_icon", L"Quiver: show the chosen arrow skill in the mana medallion", 1,
           L"The bow skill you chose (an orb, a quiver button, the skill panel) shows its icon - the game's own - on the bottom of the MANA medallion on your wrist, under the liquid, "
           L"the way an armed melee skill does. Full while the hero can pay a shot with it (the release's own check: the skill's mana cost at his level against his mana); faint when "
           L"he cannot. The plain arrow: no icon. Its size, depth and the two opacities are the sliders on the Strikes tab (\"Skill icon in the mana\"). Needs the hand vitals as "
           L"the medallions (the glass orbs look, the medallion shape) and the skill icons made from your game. Unticked: the medallion as before. Live. F4 only.")),
    Needs(L"bow", L"quiver", 1, Toggle(L"bow", L"arrow_fx", L"Arrow effects: fire, frost and arcane light on the arrow", 1,
           L"On: the arrow burns, freezes or crackles by the skill that is chosen - from the moment you choose it (an orb, a button, the game's own slot), nocked at rest, and through the "
           L"draw; the plain shot has none - flames and rising embers for Fire, Exploding and "
           L"Immolation Arrow; frost, ice crystals, glints and a falling mist for Cold, Ice and Freezing Arrow; an arcane glow with sparks circling the shaft for Magic Arrow. "
           L"It is light the effect draws in the picture (D2R_DepthFog.fx of this version; nothing of the game's is created), in front of what is behind it and hidden by what is "
           L"nearer: a hand, a sleeve. The skills that have no element of their own (the plain shot, Guided Arrow, Multiple Shot, Strafe) keep the small coloured glow, and with "
           L"this off every skill does (while the string is drawn). It costs a few hundredths of a millisecond per eye on a fast card. The idea is bearstonem/d2r-pcvr's (MIT). F4 only. Live.")),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"arrow_fx", 1, Slider(L"bow", L"arrow_fx_size_pct", L"Arrow effects: size, %", 30.0f, 300.0f, 5.0f, 100.0f,
           L"How big the flames, the frost and the glow are: 100 is a flame 4.5 cm round the arrow's head. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"arrow_fx", 1, Slider(L"bow", L"arrow_fx_bright_pct", L"Arrow effects: brightness, %", 20.0f, 300.0f, 5.0f, 100.0f,
           L"How bright the effect is, on every skill together. The heavier skills (Exploding, Immolation, Ice, Freezing) are a little brighter than the rest already. Live."))),
    Needs(L"bow", L"quiver", 1, Needs(L"bow", L"arrow_fx", 1, Slider(L"bow", L"arrow_fx_along_cm", L"Arrow effects: along the arrow, cm", -20.0f, 60.0f, 0.5f, 0.0f,
           L"Moves the effect's centre along the arrow's shaft: more is forward, toward the arrowhead; less is back toward the string. 0 = where it is found. "
           L"Fire, frost and arcane alike, on the arrow at rest and on the drawn one. Live."))),
    Needs(L"hands", L"bow_draw", 0, Group(L"Bow without the string draw (the A button, mode above)")),
    Needs(L"hands", L"bow_draw", 0, Needs(L"bow", L"mode", 4, Toggle(L"hands", L"bow_aim", L"Arrows along the hand, no auto-target", 0,
           L"On: with a bow or a crossbow in hand, the attack and the bow's arrow skills go exactly along the hand: the game's "
           L"auto-target and aim assist are off for that shot; its animation, attack rate and arrows stay the game's. "
           L"Off: shots as before, the game may still snap to a monster near the aim."))),
    Needs(L"hands", L"bow_draw", 0, Needs(L"bow", L"mode", 4, Slider(L"bow", L"range", L"Bow aim point, cells", 3, 40, 1, 20,
           L"\"Attack vector\" with a bow: the attack point this many grid cells out along the hand (more = finer aim)."))),
    Needs(L"hands", L"bow_draw", 0, Slider(L"bow", L"aim_yaw", L"Bow aim correction, ° (+ = right)", -30, 30, 1, 0,
           L"Turns the aim about the vertical, if the arrows land beside where the hand points.")),
    Group(L"Throws"),
    Needs(L"bow", L"mode", 4, Toggle(L"hands", L"throw_aim", L"Throws along the hand, no auto-target", 0,
           L"On: javelins and throwing weapons (and their skills) go exactly along the hand, no auto-target. Off: the game's "
           L"auto-target helps a throw find its target - a throw is hard to aim in VR.")),
    Needs(L"openxr", L"on", 1, Needs(L"bow", L"mode", 4, Toggle(L"hands", L"throw_gesture", L"Throw a javelin with a throwing motion", 0,
           L"Native OpenXR. On: with a javelin or a throwing weapon in the hand, hold the right trigger, swing the hand and let go - the "
           L"hero throws it along the way the hand was going. The throw is the game's own attack: its missile, speed, ammunition and "
           L"animation stay; the gesture only presses the attack at the release and aims it, no auto-target. A hand at rest, a slow "
           L"swing, a jab with the trigger up or a trigger held too long throw nothing. The right trigger is the gesture's while such a "
           L"weapon is in the hand - it does not attack on its own. Put the game's Throw skill on one of the pad's skill buttons (B X Y RB, "
           L"or with the left trigger; the skill wheel does it): the throw presses that button - the game's Throw skill, a cast at a spot with no "
           L"monster needed. The A attack cannot throw a potion at all and needs an enemy at the point for a knife, axe or javelin. "
           L"Off: as before. F4 only."))),
    Needs(L"openxr", L"on", 1, Needs(L"openxr", L"direct_input", 1, Needs(L"bow", L"mode", 4, Toggle(L"hands", L"throw_by_grip",
           L"Throw by the grip: hold = in the hand, let go at the end of a swing = thrown", 1,
           L"Native OpenXR with \"Controls without BodyWalk\", first person (F4). On: with a javelin or a throwing weapon in the hand the "
           L"right GRIP is the weapon in your hand - squeezed, it is held, as long as you like. Swing the hand with the grip squeezed "
           L"and let go of it at the end of the swing: the hero throws it along the way the hand was going - the game's own attack: its "
           L"missile, speed, ammunition and animation stay; the gesture only presses the attack and aims it. A grip let go with the hand "
           L"at rest, or after a slow movement, throws nothing. The right grip is the throw's while such a weapon is wielded: no belt "
           L"bottle with it, no weapon swap behind the shoulder with that hand (the right stick click still swaps). "
           L"On by default (it does nothing without native OpenXR and \"Controls without BodyWalk\"). The throw casts the game's own Throw skill by "
           L"itself (the tick box below) - no skill button needed. Instead of the trigger gesture above. Off: as before.")))),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Toggle(L"hands", L"throw_auto_skill", L"Throws use the Throw skill by themselves (no skill slot needed)", 1,
           L"The pad's A button is the game's Attack, which cannot throw a potion at all and throws a knife, axe or javelin only at an enemy. With a throwing "
           L"weapon or potion in your hand the game's mouse already turns the plain attack into its Throw skill - a throw at a spot, no monster needed; "
           L"this does the same for the throw's A press, so you do not have to put Throw on a skill button. The game's own cast, checks, animation and ammunition. "
           L"Unticked: the throw presses the pad button of the skill slot that holds Throw (put it there with the skill wheel).")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Slider(L"hands", L"throw_autoaim_deg", L"Throw auto-aim: cone round the throw's line, °", 0, 45, 1, 15,
           L"A throw flies along the line your hand was going. This is a gentle help inside a narrow cone around that line - the "
           L"HALF-angle of the cone, in degrees: a hostile monster that stands within it (and within the game's range, ahead of you) snaps the "
           L"throw's aim point to itself - the one nearest the line. Nothing in the cone: exactly along your hand, no help at all. 0 = off, "
           L"purely along the hand. The game's own attack takes the point: its range, line of sight and who counts as hostile stay the game's. "
           L"For a throwing potion only monsters within a quarter of its landing distance count. One setting for the trigger and the grip throw. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Slider(L"hands", L"throw_potion_reach_pct", L"Throwing potions: how far, % of the real throw", 50, 1500, 10, 800,
           L"A throwing potion (Rancid Gas, Oil, Choking Gas, Exploding, Strangling Gas, Fulminating) is thrown to a spot on the ground: "
           L"where a real object would land, thrown the way your hand moved at its fastest just before the release, from the height of your "
           L"hand. This scales that distance: 100 = as it would really fly (a gentle swing in a room lands a few steps away on the game's "
           L"screen); 800 (the default, the same as the weight tab's reach) = a comfortable swing of 3 - 5 m/s lands 15 - 25 m away; more if it "
           L"still falls short. At least 3 and at most 48 cells (about 30 m) whatever it says - the game refuses a point farther off. "
           L"With the weapon-weight tab on, the weight model works the throw out itself and this is not used. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Toggle(L"hands", L"potion_throw_ours", L"Throwing potions: our potion flies, the game's throw is hidden, the explosion where it lands", 0,
           L"ONE tick for a thrown potion that is only YOURS to see. A thrown potion is two things: your potion (the weapon-weight model's model) and the game's own "
           L"throw - a missile that leaves the hero a third of a second after the release and flies straight to the spot. This turns on the three things that make it "
           L"one: your potion flies the whole trajectory you threw it on and is the only potion you see (the tick \"Your potion flies all the way\" on the Weapon Weight "
           L"tab); the game's own flying potion is not drawn (\"The game's own flying potion is not drawn\" below - only what is drawn changes, never the game's missile, "
           L"its damage or its stack); and the game's own explosion (damage, gas, sound - the game's, untouched) happens where and when your potion lands "
           L"(\"explode where and when the potion lands\" below, experimental). With it ticked the three are on whatever their own ticks say; unticked, each one "
           L"decides by itself (the separate ticks stay for tuning). The weapon-weight tab must be on, the throw by gesture or by grip too. VR first person only. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Toggle(L"hands", L"potion_hide_game", L"The game's own flying potion is not drawn (while yours flies; experimental)", 0,
           L"EXPERIMENTAL (off when the key is missing; on in the settings that come with the mod): its hide has not been proven in the game. A thrown potion's model is yours; the game also draws its own potion flying to the spot - a second one, a moment late. This takes that one out of the "
           L"picture: only what is DRAWN changes - the game's missile still flies, hits, spends the stack and explodes as ever; its explosion is drawn. Only the hero's "
           L"own thrown potion in the throw you just made, in VR first person, and only while your potion flies (the weapon-weight tab on and \"Your potion flies "
           L"all the way\" ticked, or the tick above); another's missile, a trap's, an arrow are never touched. Needs the renderer's two functions to be found; if "
           L"they are not, or one faults, it turns itself off for the session and the game's potion is drawn as before. The log says how many entities of it were hidden. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Choice(L"hands", L"potion_throw_mode", L"Throwing potions: when the game's own throw happens", 0,
           {L"at once (the game's potion flies and explodes)", L"when the potion lands (it flies first, the game's throw follows)"},
           L"A thrown potion is two things: the potion in your hand (the weapon-weight model's model) and the game's own throw - the Throw skill, a missile that "
           L"leaves the hero's body after his throw animation (about a third of a second), flies straight to the spot at 25 cells a second and explodes there "
           L"(the game's own explosion, damage and gas). \"At once\": the game's throw happens at the release; your potion is taken by it and vanishes. "
           L"\"When the potion lands\": your potion flies the trajectory you threw it on (the weapon-weight tab must be on; its flight time is the wait) and "
           L"when it lands the game's throw is made at the spot - the game's missile still has to fly there, so the explosion comes a moment after "
           L"the landing (the throw animation, then the flight: about a second for a spot 20 cells away). Without the weight tab: at once. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Toggle(L"hands", L"throw_direct", L"Throwing potions: explode where and when the potion lands (experimental)", 0,
           L"A thrown potion is two things: your potion's model and the game's own missile. This ties them together: the game's throw is pressed at the "
           L"release, so the hero's throw animation plays while your potion flies (the least input lag), and the one place in the game that creates its "
           L"potion missile is told to start it on the way, so that it reaches the landing spot exactly as your potion lands - the game's own explosion "
           L"(damage, gas, sound) happens there and then. Everything else is the game's own (the animation, the line-of-sight and mana gates, the stack). "
           L"The weapon-weight tab must be on. Offline only. If the game's code is not found, or it faults once, it turns itself off for the session and "
           L"the throw is as before. When your potion flies longer than the game's own potion would, the game's own flight is left alone. Unticked: as "
           L"before. Live.")),
    NeedsEither(L"hands", L"throw_gesture", L"throw_by_grip", 1, Toggle(L"hands", L"throw_stand_still", L"Throws are pressed with the game's Stand Still", 1,
           L"Like every attack of ours in first person, a throw's press holds the game's Stand Still (Shift), so it can never walk the hero. A throw is a cast at "
           L"a spot and does not walk anyway: unticked, it is pressed without Stand Still. Live.")),
    Needs(L"openxr", L"on", 1, Needs(L"openxr", L"direct_input", 1, Toggle(L"hands", L"javelin_quiver", L"Javelin quiver: choose the skill of the next throw", 1,
           L"Native OpenXR with \"Controls without BodyWalk\", first person (F4). With a javelin or a throwing weapon in your weapon hand and a skill it allows learned "
           L"(an Amazon's Poison Javelin, Lightning Bolt, Plague Javelin, Lightning Fury and the spear skills Jab, Power Strike, Impale, Charged Strike, Fend, Lightning Strike; "
           L"a Barbarian's Double Throw with a thrown weapon in each hand; a Necromancer's Poison Dagger with a throwing knife), you pick the skill of the next throw from "
           L"your quiver: reach a hand behind a shoulder (the javelin bag on your back) and tap its trigger - the right hand steps to the NEXT skill, the left to the PREVIOUS one, "
           L"with a pulse in that hand (two pulses: back at the plain Throw). Only skills the hero has and the weapon in hand allows are offered; the choice is dropped when "
           L"the weapon changes. The game's own cast does it: the pad's attack is read as the chosen skill, so mana, range, ammunition and the attack rate stay the game's. "
           L"A blow in reach (Jab ...) is for the plain attack and the hit by touch; a throw by gesture stays the Throw then. A button bound in [direct] to "
           L"action:quivernext / action:quiverprev steps it from anywhere. Needs the skill panel's reads (they run by themselves while a thrown weapon is in hand). On by "
           L"default; it does nothing without native OpenXR and \"Controls without BodyWalk\". F4 only."))),
    Needs(L"hands", L"javelin_quiver", 1, Toggle(L"hands", L"javelin_quiver_reach", L"Javelin quiver: tap a trigger with a hand behind the shoulder", 1,
           L"On: the reach behind a shoulder (the volume of the weapon swap over the shoulder) with a trigger tap steps the choice - the trigger of a hand in the reach "
           L"is the quiver's, not the attack's. Off: only a button bound to action:quivernext / action:quiverprev in [direct] steps it. Live.")),
    Toggle(L"hands", L"quick_throw", L"Quick throw: the missile leaves earlier in the throw animation (changes the game's timing)", 0,
           L"A CHANGE OF THE GAME'S TIMING (off when the key is missing; on in the settings that come with the mod). The game makes a throw's missile - a potion, a javelin, a knife, an axe - at one frame of its throw animation "
           L"(for an Amazon frame 9 of 16: 0.36 s after the animation starts), so after your own wind-up swing the hero winds up AGAIN before it flies. On: that frame is "
           L"moved earlier in the game's animation table, so the missile is created ~0.3 s sooner INSIDE THE SAME THROW: the animation's length and the attack rate stay, "
           L"the stack, mana and damage are the game's but happen earlier, and a monster's hit in those first ~0.3 s no longer cancels the throw (it has left already). "
           L"It applies to every throw of your class whatever you hold, in VR first person only, and only offline (this game's own tables); it is put back exactly when you "
           L"switch it off, change view, or leave the game. Never the bow's or a melee attack's animation. Live. [debug] bow_latency shows the missile's create time."),
    Needs(L"hands", L"quick_throw", 1, Slider(L"hands", L"quick_throw_frame", L"Quick throw: the frame the missile is made at", 1, 12, 1, 1,
           L"The animation frame (a frame is 0.04 s) the missile is made at instead of the original (7 to 11 for the classes' throws). 1 is the earliest the game can act: "
           L"frame 0 is never looked at when an animation starts, so it would throw nothing. A value at or past the original changes nothing. Live.")),
    Needs(L"hands", L"quick_throw", 1, Toggle(L"hands", L"quick_throw_potions", L"Quick throw: throwing potions too", 0,
           L"Off (the default): a throwing POTION keeps the game's own timing - the quick throw is put back while a potion is in your hand and back on with a javelin, knife or axe. "
           L"A potion's missile is made at the throw's action frame and flies 25 cells a second, so its explosion comes about when your potion (the weapon-weight model's) lands; "
           L"cut to the start, it comes ~0.3 s BEFORE your potion lands. On: potions are cut like the rest. Live.")),
    Group(L"Crossbow"),
    Toggle(L"bow", L"xbow_two_hands", L"Crossbow held like the staff", 1,
           L"The right hand holds the crossbow on the stock and aims it alone; the left takes it with the grip (ahead of the right) "
           L"and then it aims along the line from the right hand to the left. Off: as a bow, the game's own animation."),
    Needs(L"bow", L"xbow_two_hands", 1, Choice(L"hands", L"xbow_hand", L"The game hangs the crossbow on", 1, {L"the right wrist", L"the left wrist"},
           L"For its grip: if the crossbow sits wrong or turns away when the left hand takes it, try the other.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_hold_grip", L"Crossbow: the right hand holds it by its grip", 0,
           L"The crossbow sits in the holding palm by its trigger grip - where the game's own trigger hand holds it in the game's two-handed "
           L"hold (learned from the game's pose a moment after the crossbow is in hand) - not by the bone it hangs on. "
           L"The log says once how far the two are apart. Unticked: as before. F4 only. Still being corrected: in a test (2026-10-18) the crossbow's grip was still off.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_support_game", L"Crossbow: left hand on the game's own fore-grip", 0,
           L"The left hand takes the crossbow at its fore-grip, where the game's own hand that carries it holds it in the game's two-handed hold - learned from the game's "
           L"pose a moment after the crossbow is in hand, nothing set by hand. Until then (and unticked) the place under the drawn barrel, or the "
           L"shot line. The log says once where it is and how the game's palm faces the stock. F4 only. Not finished: in a test (2026-10-18) it did not work yet, so it is off in the settings that come with the mod.")),
    Needs(L"hands", L"xbow_support_game", 1, Toggle(L"hands", L"xbow_support_close", L"Crossbow: the left hand closes on the stock", 0,
           L"On the game's own fore-grip the left hand takes the game's own fore-grip hand's turn and finger pose (the game's left hand; "
           L"mirrored when the right hand supports) - the palm toward the stock, the fingers closed round it - instead of the controller's turn with an open hand. "
           L"Unticked: as before. F4 only. Not finished: in a test (2026-10-18) it did not work yet, so it is off in the settings that come with the mod.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_barrel_forward", L"Crossbow: its barrel along the controller", 0,
           L"The crossbow is laid by its own drawn barrel: the barrel points where the controller points and the crossbow's top is the controller's "
           L"top - not by a bone axis, which tilted it about 34 degrees up and a little aside in the hand. Until the barrel is read (a moment after "
           L"the crossbow is in hand) as before. F4 only. Not finished: in a test (2026-10-18) it did not work yet, so it is off in the settings that come with the mod.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_handover", L"Crossbow: pass it from hand to hand", 0,
           L"Take the crossbow with the free hand anywhere (the fore-grip) and let go of the grip of the hand that held it: it stays in the other "
           L"hand. Or squeeze the free hand's grip anew near the holding hand (within 25 cm, nearer it than the fore-grip), and the "
           L"crossbow is in that hand; the other one lets go and is free - squeeze it at the fore-grip to support the crossbow there (mirrored). "
           L"The shot follows the barrel of the hand that holds it. Only how it is held and aimed: the game's weapon slots and attack are its "
           L"own. Another weapon or the tick off: back in the right hand. F4 only. Not finished: in a test (2026-10-18) the hand-over did not work yet, so it is off in the settings that come with the mod.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_aim_barrel", L"Crossbow: shoot along the drawn barrel", 0,
           L"The bolt goes where the crossbow you see points: along its barrel as the model is drawn (its bolt helper bone, as bearstonem/d2r-pcvr "
           L"aims) - no correction by hand, raised or rolled alike. The game's own bolt, damage, ammunition and rate of fire are unchanged: only the "
           L"direction of the game's attack is steered, as the shot line does. While the barrel is not read (an unknown model, a moment after a swap) "
           L"the shot line below is used. Unticked: the shot line. F4 only.")),
    Needs(L"bow", L"xbow_two_hands", 1, Needs(L"hands", L"xbow_aim_barrel", 0, Slider(L"hands", L"xbow_line_pitch", L"Crossbow: shot line, tilt up/down, °", -180, 180, 1, 34,
           L"Turns the line the bolt flies along and the left hand takes - the red line ([debug] phantom_ray) - not the model. Lay it on the crossbow. "
           L"With \"shoot along the drawn barrel\" ticked it is only the stand-in while the barrel is not read."))),
    Needs(L"bow", L"xbow_two_hands", 1, Needs(L"hands", L"xbow_aim_barrel", 0, Slider(L"hands", L"xbow_line_yaw", L"Crossbow: shot line, turn left/right, °", -180, 180, 1, 15))),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_at_cm", L"Crossbow: left hand ahead, cm", 5, 80, 1, 30,
           L"The one place the left hand takes the crossbow (under the stock, just behind the bow); it does not slide. "
           L"The grip takes it only with the left hand that near this place.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_roll", L"Crossbow: left hand roll, °", -180, 180, 1, 0,
           L"The left hand on the crossbow turned round the stock: from below, palm up, holding it up.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_x", L"Crossbow: left hand X, cm", -20, 20, 0.5f, 0,
           L"The left hand on the crossbow moved along its own axes, until it sits under the stock.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_y", L"Crossbow: left hand Y, cm", -20, 20, 0.5f, 0)),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_z", L"Crossbow: left hand Z, cm", -20, 20, 0.5f, 0)),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_grip_latch", L"Crossbow: left grip lets go cleanly", 0,
           L"The left hand holds the crossbow while the left grip is squeezed - it may be squeezed first and then brought to the stock. It lets go at once "
           L"when you swap weapons or the hero's model changes, and when the left controller loses tracking (the hand stays free: no jump onto the stock). "
           L"A squeeze still held from the shoulder weapon swap takes nothing: let go and squeeze again. No grip reading from the bridge = not held. "
           L"Unticked: as before (the grip takes it near the place; a lost controller puts the hand on the stock). F4 only. The shot is not changed.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_wrist_tracked", L"Crossbow: left wrist follows the controller", 0,
           L"The left hand on the crossbow keeps your controller's turn - hold the stock from below, palm up, or from above, as you like - and its palm's "
           L"middle sits on the place. Unticked: the hand is turned into the game's grip round the stock (the roll slider turns it). "
           L"The left hand X/Y/Z and roll sliders still act on top. F4 only.")),
    Needs(L"hands", L"xbow_wrist_tracked", 1, Toggle(L"hands", L"xbow_grip_wide", L"Crossbow: take the grip right after a swap", 0,
           L"The game's grip of the crossbow is taken even from a pose with the hands far apart (the idle right after a swap from a shield set), "
           L"since the crossbow is placed by the controller anyway - before, the left hand could not take it until a pose with the hands together "
           L"(a shot). Needs \"left wrist follows the controller\". F4 only.")),
    Needs(L"bow", L"xbow_two_hands", 1, Toggle(L"hands", L"xbow_support_model", L"Crossbow: left palm under the drawn barrel", 0,
           L"The left palm goes under the crossbow's barrel as the model is drawn (its bolt helper bone), \"palm behind the limbs\" behind the front "
           L"of the stock and \"palm below the barrel\" under it - each crossbow its own. Unticked: on the shot line set by eye. "
           L"The bolt still flies along the shot line: only the hand moves. The log says whether the barrel was read (\"crossbow barrel\"). F4 only.")),
    Needs(L"hands", L"xbow_support_model", 1, Slider(L"hands", L"xbow_support_drop_adj_cm", L"Crossbow: palm lower (+) / higher, cm", -10, 10, 0.5f, 0,
           L"A fine adjustment around 0: the palm's middle sits 7 cm under the drawn barrel (as d2r-pcvr); + lower, - higher.")),
    Needs(L"hands", L"xbow_support_model", 1, Slider(L"hands", L"xbow_support_back_adj_cm", L"Crossbow: palm back (+) / forward, cm", -10, 20, 0.5f, 0,
           L"A fine adjustment around 0: the palm sits 6 cm behind the front of the stock, where the bow's limbs meet it (as d2r-pcvr); "
           L"+ farther back, - forward. With the palm under the drawn barrel ticked, \"left hand ahead\" is not used.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"xbow_range_one", L"Crossbow aim point, one hand, cells", 2, 40, 1, 5,
           L"How far out along the aim the attack point is with the right hand alone: nearer = rougher, the crossbow sways more.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"xbow_range_two", L"Crossbow aim point, two hands, cells", 2, 40, 1, 30,
           L"The same with the left hand on it too: farther = finer, held in two the aim is steady.")),
    Slider(L"bow", L"xbow_aim_yaw", L"Crossbow aim correction, ° (+ = right)", -45, 45, 1, 0,
           L"The same for a crossbow, in place of the bow's: it is held like a gun, not round a bow's grip."),
    Group(L"Staff: aim point and correction"),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"staff_range_one", L"Staff aim point, one hand, cells", 2, 40, 1, 5,
           L"The staff's, with the right hand alone.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"staff_range_two", L"Staff aim point, two hands, cells", 2, 40, 1, 30,
           L"The staff's, with the left hand on it too.")),
#if D2RVR_FIRST_PERSON
    Needs(L"hands", L"spell_aim_model", 1, Slider(L"bow", L"caster_range", L"Wand or orb aim point, cells", 0, 40, 1, 20,
           L"VR first person (F4), with Auto-target's \"Aim spells by the drawn hand or staff\": how far out along the wand or orb a spell's point goes - "
           L"farther = finer steps of the aim (the game's own point is about 3 cells, a 20-degree staircase). Below 3: the game's own point. A staff has the two above.")),
    Needs(L"hands", L"spell_aim_model", 1, Slider(L"bow", L"hand_range", L"Bare hands aim point, cells", 0, 40, 1, 5,
           L"The same for a spell cast with bare hands (or with any weapon that is not a staff, wand or orb): how far out along the hand its point goes. "
           L"Below 3: the game's own point.")),
    Group(L"Aim distance by the arm"),
    Needs(L"bow", L"mode", 4, Toggle(L"bow", L"aim_distance_by_arm", L"A spell's aim distance follows how far the arm is stretched", 0,
           L"VR first person (F4), spells only: a wand, an orb, a staff held in one hand, bare hands. As in flat Diablo, where a click near the hero spreads Teeth wide and one "
           L"far away keeps it in a tight cone: here an arm held at the body gives the near distance below, an arm stretched out along the aim gives the weapon's own "
           L"distance (the wand, orb and bare-hand sliders above; for a staff the two-hand one). The stretch is measured on your own arm - the drawn shoulder and the length to the grip, "
           L"fitted to your T-pose span or your height - and smoothed a little. A staff with only one hand on it follows the arm too; a staff held in BOTH hands always aims at its far "
           L"distance, the tightest cone, whatever the arm does. A bow, a crossbow and throws keep their fixed distance. It only chooses where the game's own spell is aimed, as a "
           L"click would. Off: the distances above, as before. Live.")),
    Needs(L"bow", L"aim_distance_by_arm", 1, Slider(L"bow", L"aim_near_cells", L"Aim point with the arm at the body, cells", 3, 20, 1, 3,
           L"How near a spell's aim point comes with the arm at the body (the far end is the weapon's own distance). Smaller = a wider fan of Teeth and the like. Live.")),
#endif
    Slider(L"bow", L"staff_aim_yaw", L"Staff aim correction, ° (+ = right)", -45, 45, 1, 0,
           L"The same for a staff, in place of the one above: turns its aim about the vertical, if spells land beside where the staff points. "
           L"In VR F4 with Auto-target's \"Aim spells by the drawn hand or staff\" on, only when that has no fresh pose (the staff's own axis needs no correction)."),
    Group(L"Which hand aims"),
    Toggle(L"bow", L"ranged_left", L"Bows and crossbows: always the left hand", 1,
           L"A bow or crossbow is always aimed with the left hand, whatever the weapon set."),
    Toggle(L"bow", L"set1_left", L"Weapon set I: left hand", 1, L"The aiming hand of weapon set I (not bows). Off = right hand."),
    Toggle(L"bow", L"set2_left", L"Weapon set II: left hand", 0, L"The aiming hand of weapon set II (not bows). Off = right hand."),
    // Every kind at a glance, the adjusted ones marked (RefreshWeaponList); a click picks it for the sliders.
    Group(L"All 17 kinds (green = adjusted, click to edit)"),
    Status(L"wk1", L"Unarmed"),
    Status(L"wk2", L"Bow"),
    Status(L"wk3", L"Crossbow"),
    Status(L"wk4", L"Sword"),
    Status(L"wk5", L"Axe"),
    Status(L"wk6", L"Mace"),
    Status(L"wk7", L"Scepter"),
    Status(L"wk8", L"Wand"),
    Status(L"wk9", L"Staff"),
    Status(L"wk10", L"Polearm"),
    Status(L"wk11", L"Spear"),
    Status(L"wk12", L"Dagger"),
    Status(L"wk13", L"Throwing"),
    Status(L"wk14", L"Javelin"),
    Status(L"wk15", L"Claw"),
    Status(L"wk16", L"Orb"),
    Status(L"wk17", L"Other"),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"fist", L"Free hands make a fist with the grip", 1,
           L"A hand holding nothing closes into a fist as you squeeze the controller's grip and relaxes when you let go.")),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"shield_fist", L"Hand with a shield closes with the grip", 1,
           L"First person (F4): the hand with a shield on it closes into a fist as you squeeze the controller's grip and opens when you "
           L"let go, like a free hand. Only the fingers move - the shield stays on the arm exactly as the game draws it, and a shield "
           L"bash (\"Shield bash casts Smite\") works the same with the hand closed. Off: the game's own animation of the hand.")),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"forearm_twist", L"Forearm turns with the wrist", 1,
           L"First person (F4): when you roll the controller (palm up, palm down) the forearm turns with it, as a real one does - "
           L"a little at the elbow, all of it at the wrist - instead of the hand alone screwing round on a still arm. A shield strapped "
           L"to the forearm turns with it. Off: the forearm keeps the game's animation and only the hand turns, as before. Live.")),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"forearm_twist", 1, Slider(L"hands", L"forearm_twist_share", L"Forearm's share of the roll", 0, 1, 0.05f, 0.5f,
           L"How much of the wrist's roll the forearm takes at the elbow end: 0 = none there (the hand and the wrist end turn), "
           L"1 = the whole forearm turns as one with the hand. Toward the wrist it always grows to all of it. Live."))),
    Group(L"Staff (held in both hands)"),
    Toggle(L"bow", L"staff_two_hands", L"Staff: aim from the left hand to the right", 1,
           L"With a staff and both controllers: spells and attacks go along the line from your left hand to your right one, "
           L"the right hand ahead - like holding the staff out at the target. With \"Staff in the right hand\" they go along the staff."),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"staff_free_left", L"Staff in the right hand, left takes it with the grip", 1,
           L"The right hand holds the staff fast and it never slides there. The left hand is free; hold its grip and it takes "
           L"the staff and slides along it with the controller, let go and it comes off. Off: the staff hangs between both hands.")),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"staff_free_left", 1, Slider(L"hands", L"staff_grab_cm", L"Left hand takes the staff within, cm", 3, 100, 1, 15,
           L"With \"Staff in the right hand\": the grip takes the staff only with the left hand this near it. Pressed farther away, "
           L"it takes nothing until let go and pressed again near the staff."))),
    // (the left hand on the shaft is moved by Weapon Adjust's "Left hand when it holds", one for every
    // two-handed weapon; left_staff_* / left_sword_* still read from the ini, no sliders - they doubled it)
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"staff_free_left", 0, Toggle(L"hands", L"follow_staff", L"Hands turn with the staff", 1,
           L"With a staff, both wrists turn with it - gripping it the way the game's animation does - and the controllers' "
           L"own turn is ignored. The hands stay where the controllers are."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"staff_free_left", 0, Slider(L"hands", L"staff_x", L"Staff at the right hand: X, cm", -40, 40, 1, 0,
           L"A staff is turned to run through the right hand too. These move that point along the hand's own axes "
           L"until the staff sits in the fist."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"staff_free_left", 0, Slider(L"hands", L"staff_y", L"Staff at the right hand: Y, cm", -40, 40, 1, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"staff_free_left", 0, Slider(L"hands", L"staff_z", L"Staff at the right hand: Z, cm", -40, 40, 1, 0))),
    Group(L"Spears, polearms, two-handed axes, maces and swords"),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"polearm_two_hands", L"Spear, polearm, two-handed axe or mace: left takes it with the grip", 1,
           L"As with the staff: the right hand holds the spear or polearm fast; hold the left grip near the shaft and the left "
           L"hand takes it and slides along it, turning it with you. Let go and it comes off. Uses \"Left hand takes the staff within\".")),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"sword_two_hands", L"Two-handed sword: left takes the hilt with the grip", 1,
           L"A two-handed sword with nothing in the other hand: the right hand holds it; hold the left grip near the hilt and the "
           L"left hand closes on it next to the right one. The sword still points where the right hand does. "
           L"A barbarian's two-handed sword beside another weapon stays in one hand.")),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"polearm_two_hands", 1, Slider(L"hands", L"axe_shaft_axis", L"Two-handed axe or mace: haft along axis", -3, 3, 1, -2,
           L"The weapon bone's own axis the haft runs along, from the right fist toward where the left hand slides - the axes "
           L"Debug \"Draw the axes\" shows: 1 X green, 2 Y blue, 3 Z yellow, minus = the other way. 0: from the game's grip."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"polearm_two_hands", 1, Slider(L"hands", L"spear_shaft_axis", L"Spear or polearm: shaft along axis", -3, 3, 1, -2,
           L"As above for spears and polearms. 0: from the game's grip (the line between its hands)."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"sword_two_hands", 1, Slider(L"hands", L"sword_hilt_axis", L"Two-handed sword: hilt along axis", -3, 3, 1, -2,
           L"The sword bone's own axis the hilt runs along, from the right fist toward the pommel - the axes Debug \"Draw the axes\" "
           L"shows: 1 X green, 2 Y blue, 3 Z yellow, minus = the other way. -2: against the blue, which runs to the tip. "
           L"0: from the game's grip. The red line shows where the left hand takes it."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"sword_two_hands", 1, Slider(L"hands", L"sword_left_at_cm", L"Two-handed sword: left hand from the right, cm", -30, 30, 0.5f, 10,
           L"Where the left hand takes the hilt, measured from the right fist toward where the game's animation puts the left "
           L"hand (negative: the other way)."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"sword_two_hands", 1, Slider(L"hands", L"sword_slide_cm", L"Two-handed sword: left hand slides, cm", 0, 20, 0.5f, 4,
           L"How far the left hand may slide along the hilt either way with the controller. 0: it stays in one place."))),
    // Strikes (2026-10-16, the user's wish): every setting that decides whether, and how, a touch of the blade becomes an attack, in one place. Moved here from Weapon Adjust (hit by touch, direct hit, quick
    // strike, the shield bash) and the Debug tab's touch ticks (duplicated in the group at the bottom). The ini keys are the same as ever, only the place changed; the blade's geometry stays in Weapon Adjust.
    Only(kVr, Tab(L"Strikes")),
    Group(L"Hit by touch"),
    Toggle(L"gameplay", L"melee_touch", L"Hit by touch", 0,
           L"First person (F4), the arms on the controllers, a melee weapon (or bare fists) in hand: the blade you swing through a "
           L"monster's body picks the target and the moment of the game's own attack - it is pressed on the contact and aimed at "
           L"the monster you touched. The game does the rest: its animation, its reach (out of reach it walks the hero up, as a "
           L"click on a monster does), its hit rolls and speed. A blade held still or waved slowly does nothing; a swing hits "
           L"a monster once. The skill on the attack button is what is pressed - a spell there is cast at the monster you touched. "
           L"Works with BodyWalk's tracking and with native OpenXR. Off: as before. F4 only."),
    Needs(L"gameplay", L"melee_touch", 1, Slider(L"gameplay", L"melee_min_speed", L"Hit by touch: the least blade speed that hits, m/s", 0.3f, 20, 0.1f, 0.8f,
           L"How fast the blade must be moving for a contact to count. A blade moving slower than this does nothing, however it touches "
           L"a monster - so a gentle poke hits only if this is low. The same number for every weapon (the weapon's own blade is its only "
           L"difference); the default 0.8 is what it always was. The speed is the BLADE's, measured on the weapon as drawn, frame to frame "
           L"(the faster of its two ends - the tip of a sword - and at a touch the speed of the point that touches: the tip of a blade "
           L"swinging about your hand is fast, the end you hold is not) - not your hand's, so a grip at the root or in the middle, a big arc "
           L"with a still wrist and a poke all read what the blade really does. A monster is hit once per stroke, at its first contact. A "
           L"normal swing's tip reads about 3-6 m/s: tick \"Draw the capsules of Hit by touch\" (Debug group below) and the blade turns green while its "
           L"fastest part moves at least that fast (a touch counts with the part that touches, the tip), back to cyan when it slows; the log line "
           L"\"melee boxes:\" shows the blade's speed and its peak against this "
           L"number. Lower, and the speed that may begin a stroke and the speed that counts as rest go lower with it; from 0.8 up the rest "
           L"is as before. Only decides when the touch presses the game's own attack - never its reach, rolls or the attack itself. "
           L"Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Slider(L"gameplay", L"melee_stroke_start_cm", L"Hit by touch: how far the blade moves to start a stroke, cm", 2, 30, 1, 8,
           L"A movement is a stroke - and can hit - only after the blade (its faster end: the tip of a sword, as drawn - not the hand) has "
           L"moved this far within about a tenth of a second, and fast enough (above). Smaller: a short jab counts; larger: only a long swing does. "
           L"A blade moving slower than the least speed above does nothing, a monster is hit once per stroke. Default 8 as before. "
           L"Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_touch_thrown", L"Thrown weapons hit by touch too (a point-blank throw)", 1,
           L"A javelin, a throwing knife or a throwing axe in hand is swung like any weapon: its blade through a monster's body presses the "
           L"game's own attack at it - for a thrown weapon that is the game's Throw at that monster, point blank, with the ammunition spent as "
           L"the game spends it (never the Direct hit route). Throwing potions are not swung. The throw by gesture (Weapon Adjust > Throws: by the "
           L"grip or the trigger) stays as it is and does not fire together with the touch: with the grip throw on, the touch counts only "
           L"while the right grip is squeezed on the weapon; for 0.7 s after a throw is released the touch stands down; and a touch that "
           L"pressed the attack drops the release of the same swing (one throw per swing). The blade's size per weapon is an estimate - "
           L"tune it with the blade sliders of the Debug group below and \"Draw the capsules of Hit by touch\". Unticked: a thrown weapon does nothing "
           L"by touch, as before. Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_touch_only", L"The trigger does not attack (only the blade's touch; a bow or a crossbow shoots with it)", 1,
           L"A part of hit by touch, on with it: a trigger that attacks lets you skip the swing and just pull it, which is a bug of hit by touch, not an option. First person (F4), hit by touch on, the plugin's own "
           L"controls (Stereo > Native OpenXR > Controls without BodyWalk): the trigger's attack is not passed to the game - only the blade's touch attacks. That is every weapon the touch swings: melee weapons, "
           L"bare hands and throwing weapons (javelins, throwing knives and axes - the touch or the throw gesture throws them); only a BOW and a CROSSBOW keep the trigger (they are shot, not swung). "
           L"Spells and skills on the other buttons (B, X, Y, RB, the skill wheel) are as they were. Only OUR controller press is withheld; nothing of the game is blocked or hooked, and everything else the "
           L"trigger is bound to works as bound (a skill button, a command). The trigger works as ever where hit by touch does not run: no tracking, a menu or a panel up, another view, a throwing weapon with its "
           L"touch off, a throwing potion. With BodyWalk's own input (not the plugin's) the trigger is BodyWalk's and this cannot reach it. The log says what it decided (\"melee touch only: ...\"). "
           L"Unticked: the trigger attacks as it did before - the way back. Live. F4 only.")),
    Group(L"Which contact counts as a strike"),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_toward", L"Toward the monster: the blade must move at it", 1,
           L"For every kind of weapon (single kinds can be switched off in the table below): a contact counts as an attack only while the point of the blade that touches the monster is "
           L"moving TOWARD it - the angle between that point's velocity and the line from the point to the nearest point of the monster's body axis is under the angle below. An overhead "
           L"blow down, an uppercut from below, a sideways sweep into the monster and a thrust all go toward it. The wind-up (the head going up and back, away from a monster in front of you), "
           L"the pull-back after a blow and a graze along its side do not count - and do not use the monster up: the blow that follows does. Only decides whether a contact counts as an "
           L"attack: the game's own attack, its damage, rolls and reach are the game's, nothing is scaled. Unticked: every contact counts, as before. With \"Draw the capsules of Hit by "
           L"touch\" (Debug group below) a refused contact is RED (BLUE with the tick unticked). Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_toward", 1, Slider(L"gameplay", L"melee_toward_deg", L"Toward the monster: the widest angle, deg", 20, 89, 1, 60,
           L"How far off the straight line to the monster a movement may be and still count as toward it: 60 = the cosine of the angle between the two directions above 0.5. Lower: only a "
           L"movement straight at the monster counts; higher: a glancing blow counts too; 89: anything that is not away from it. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_amplitude", 1, Slider(L"gameplay", L"melee_amp_blunt_x", L"Blunt weapons: x piercing", 0.5f, 6, 0.05f, 2.0f,
           L"The least swing amplitude of maces, hammers, clubs and scepters as a multiple of the piercing weapons' (below): 2 x 8 cm = 16 cm - a hammer needs a real swing, a flick or a tap "
           L"does nothing, a shove of the head into the monster over that distance counts. The cm it makes is in the label. Each kind can have a number of its own in the table. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_head_only", L"Only the head of the weapon strikes (maces, scepters)", 1,
           L"For the kinds ticked in the table (maces, hammers, clubs and scepters by default): a contact counts only when the HEAD of the weapon touches the monster - the outer part of the "
           L"blade, not the shaft or the handle. A touch with the shaft is refused (GREY in the debug) and does not use the monster up. Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_head_only", 1, Slider(L"gameplay", L"melee_head_pct", L"The head is the outer part of the blade, %", 10, 100, 5, 40,
           L"The head is this part of the blade's length, counted from the tip: 40 = the outer two fifths. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_amplitude", L"A swing must be big enough (the head's travel in the stroke)", 1,
           L"For a contact to count, the head must have travelled at least the kind's amplitude in this stroke before it touched: a real swing, not a flick of the wrist or a tap. The base is "
           L"the piercing weapons' number below (a dagger's or a spear's short jab can count); edged weapons, blunt weapons and staffs need it times their factor; any kind can have a factor "
           L"of its own in the table. Not a speed - the least speed above, and each kind's own in the table, is separate. Unticked: no swing size is asked. Live. F4 only.")),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_amplitude", 1, Slider(L"gameplay", L"melee_amp_pierce_cm", L"Piercing: least swing amplitude, cm", 1, 60, 1, 8,
           L"Wands, spears, daggers, claws, orbs, bare fists, javelins, throwing weapons and the rest: the least swing amplitude in cm - the base for all the other kinds. 8 = a jab of a "
           L"hand's width. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_amplitude", 1, Slider(L"gameplay", L"melee_amp_edged_x", L"Edged weapons: x piercing", 0.5f, 6, 0.05f, 1.4f,
           L"Swords, axes and polearms: the least swing amplitude as a multiple of the piercing weapons'. 1.4 x 8 cm = 11.2 cm. The cm it makes is in the label. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_amplitude", 1, Slider(L"gameplay", L"melee_amp_staff_x", L"Staffs: x piercing", 0.5f, 6, 0.05f, 1.75f,
           L"Staffs: the least swing amplitude as a multiple of the piercing weapons'. 1.75 x 8 cm = 14 cm. The cm it makes is in the label. Live."))),
    Group(L"Each kind of weapon (type a number; 0 = follow the general one)"),
    Group(L"Melee skills armed by their button (F4)"),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_skill_armed", L"A melee skill's button arms it: every hit is that skill", 0,
           L"Hit by touch on (F4 only), a melee weapon or fists swung. A press of a pad skill button (B, X, Y, RB, with or without LT) whose slot holds a melee attack skill - Bash, Stun, Concentrate, "
           L"Berserk, Frenzy, Double Swing; Zeal, Smite, Sacrifice, Vengeance, Conversion; Jab, Fend, Impale, Power Strike, Charged Strike, Lightning Strike; Maul, Fire Claws, Hunger, Rabies, Fury, "
           L"Feral Rage; Tiger Strike, Dragon Talon, Dragon Claw, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Dragon Tail, Royal Strike; Poison Dagger; Cleave (the skills the game's "
           L"Skills.txt gives melee range) - no longer casts it: it ARMS it, like the bow's chosen arrow skill (a short buzz in both hands). Every touch of your blade is then the game's attack with "
           L"that skill: its mana, cooldown, weapon check and animation are the game's, the monster you touched its target, one touch one cast. When the hero cannot pay its mana, that touch is the "
           L"plain attack and the skill stays armed. The same button again, or a button with the plain attack on it, disarms; another melee skill's button arms that one. Which skills: the "
           L"ticks of \"Armed by its button\" below (these are ticked; Whirlwind, Charge, Leap Attack and Dragon Flight are there unticked). Spells, buffs, warcries and an unticked skill "
           L"cast from their buttons as ever, and a bow, a crossbow or a thrown weapon is not touched. Needs the plain attack on the attack button (A). The log says "
           L"what was armed and what each touch was (\"melee armed: ...\"). Not tried in the game yet. Unticked: the skill buttons cast as before. Live.")),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_skill_armed", 1, Toggle(L"gameplay", L"melee_skill_armed_icon", L"Show the armed skill in the mana medallion", 1,
           L"The armed skill's icon - the game's own, the one the skill panel shows - lies on the bottom of the MANA medallion on your wrist, under the blue liquid, cut to a circle inside the "
           L"glass. Full while the hero can pay the next hit with it (the same check the hit makes: the skill's mana cost at his level against his mana); half see-through when he cannot - the "
           L"next hit will be the plain attack - and bright again when the mana is back. Nothing armed: no icon. Needs the life and mana on the hands as the medallions (Hand vitals: the glass "
           L"orbs look, the medallion shape) and the skill icons made from your game (the skill wheel's). Unticked: the medallion exactly as before. Not tried in the game yet. Live. F4 only."))),
    Slider(L"gameplay", L"melee_skill_armed_icon_size_pct", L"Skill icon in the mana: size, % of the inner circle", 30.0f, 100.0f, 0.5f, 87.5f,
           L"How big the skill's icon in the mana medallion is: a percent of the medallion's inner circle (the glass dome's foot). 87.5 = as it first was; 100 = the whole floor under "
           L"the glass. For the armed melee skill's icon and the bow's chosen arrow skill's (Bow tab, \"Quiver: show the chosen arrow skill in the mana medallion\"). Live."),
    Slider(L"gameplay", L"melee_skill_armed_icon_depth_pct", L"Skill icon in the mana: depth, % (100 = the bottom)", 0.0f, 100.0f, 1.0f, 100.0f,
           L"How deep in the medallion the icon lies: 100 = on the very bottom, under all the liquid (as it first was); 0 = as high as it fits - its rim just under the glass, over the "
           L"liquid when the mana is low. It is the dome's own shape: the bigger the icon, the less room over it (at 100% size it can only lie on the bottom). Seen from the side it "
           L"shifts against the floor like a real thing in the glass. Live."),
    Slider(L"gameplay", L"melee_skill_armed_icon_opacity_pct", L"Skill icon in the mana: opacity, %", 0.0f, 100.0f, 1.0f, 100.0f,
           L"How solid the icon is while the hero can pay the next hit (the shot, for a bow): 100 = fully (as it first was), 0 = not seen. Live."),
    Slider(L"gameplay", L"melee_skill_armed_icon_faint_pct", L"Skill icon in the mana: opacity when the mana is short, %", 0.0f, 100.0f, 1.0f, 35.0f,
           L"How solid the icon is while the hero CANNOT pay the next hit - the hit will be the plain attack (the bow: the plain arrow, or the game's refusal). 35 = as it first was; "
           L"0 = it goes out until the mana is back. The icon eases between the two opacities. Live."),
    Group(L"Armed by its button: Amazon (F4)"),
    ArmedSkill(L"s10", L"Jab"), ArmedSkill(L"s14", L"Power Strike"), ArmedSkill(L"s19", L"Impale"),
    ArmedSkill(L"s24", L"Charged Strike"), ArmedSkill(L"s30", L"Fend"), ArmedSkill(L"s34", L"Lightning Strike"),
    Group(L"Armed by its button: Necromancer (F4)"),
    ArmedSkill(L"s73", L"Poison Dagger"),
    Group(L"Armed by its button: Paladin (F4)"),
    ArmedSkill(L"s96", L"Sacrifice"), ArmedSkill(L"s97", L"Smite"), ArmedSkill(L"s106", L"Zeal"),
    ArmedSkill(L"s111", L"Vengeance"), ArmedSkill(L"s116", L"Conversion"), ArmedSkill(L"s107", L"Charge", 0, kArmedMoverTip),
    Group(L"Armed by its button: Barbarian (F4)"),
    ArmedSkill(L"s126", L"Bash"), ArmedSkill(L"s133", L"Double Swing"), ArmedSkill(L"s139", L"Stun"),
    ArmedSkill(L"s144", L"Concentrate"), ArmedSkill(L"s147", L"Frenzy"), ArmedSkill(L"s152", L"Berserk"),
    ArmedSkill(L"s143", L"Leap Attack", 0, kArmedMoverTip), ArmedSkill(L"s151", L"Whirlwind", 0, kArmedMoverTip),
    Group(L"Armed by its button: Druid (F4)"),
    ArmedSkill(L"s232", L"Feral Rage"), ArmedSkill(L"s233", L"Maul"), ArmedSkill(L"s238", L"Rabies"),
    ArmedSkill(L"s239", L"Fire Claws"), ArmedSkill(L"s242", L"Hunger"), ArmedSkill(L"s248", L"Fury"),
    Group(L"Armed by its button: Assassin (F4)"),
    ArmedSkill(L"s254", L"Tiger Strike"), ArmedSkill(L"s255", L"Dragon Talon"), ArmedSkill(L"s259", L"Fists of Fire"),
    ArmedSkill(L"s260", L"Dragon Claw"), ArmedSkill(L"s265", L"Cobra Strike"), ArmedSkill(L"s269", L"Claws of Thunder"),
    ArmedSkill(L"s270", L"Dragon Tail"), ArmedSkill(L"s274", L"Blades of Ice"), ArmedSkill(L"s280", L"Royal Strike"),
    ArmedSkill(L"s275", L"Dragon Flight", 0, kArmedMoverTip),
    Group(L"Armed by its button: Warlock (F4)"),
    ArmedSkill(L"s391", L"Cleave"),
    Group(L"Two weapons: Double Swing by the hands (F4)"),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"hands", L"double_swing_by_hands", L"Double Swing by the two hands", 0,
           L"A Barbarian with a melee weapon in each hand, hit by touch on (F4 only). The first hand's touch on a monster is the game's normal attack, as ever. When the OTHER hand then touches the SAME monster "
           L"within the time below, that second strike is the game's own Double Swing instead of a normal attack: its pad button is pressed once (the skill must sit on one of the pad's ten skill slots - B, X, Y, RB "
           L"with and without LT, A, LT + A) and the touched monster is handed to the cast as its target, the way the shield bash casts Smite. Only when the hero has a level in the skill and the mana for it "
           L"(the game's own numbers: the skill's cost at its level against the hero's mana); otherwise the second touch is a normal attack too. Frenzy the same way when it is the skill on a slot. The swings, the "
           L"damage, the animation and the mana are the game's; nothing is called that attacks. A pair is spent by the dual skill: the next touch starts a new one. With Double Swing on the attack button itself "
           L"every touch casts it already and this adds nothing. The log says every decision once (\"double swing: ...\"). Not tried in the game yet. Unticked: both hands' touches are normal attacks, as before. Live.")),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"hands", L"double_swing_by_hands", 1, Slider(L"hands", L"double_swing_window_ms", L"Double Swing: the other hand within, ms", 200, 2000, 50, 700,
           L"How long after one hand's strike on a monster the other hand's strike on the same monster is the second of a pair - the dual skill's. 700 ms is about one swing of the arm and back. Longer: a slower "
           L"second blow still counts; shorter: only a quick one-two does. Live."))),
    Needs(L"gameplay", L"melee_touch", 1, Needs(L"hands", L"double_swing_by_hands", 1, Choice(L"hands", L"double_swing_skill", L"Double Swing: the skill pressed first", 0,
           {L"Double Swing, then Frenzy", L"Frenzy, then Double Swing"},
           L"Which of the two the second hand presses when both sit on pad slots: the first one that the hero has a level in and can pay. With only one of them on a slot, that one. Live."))),
    Group(L"Direct hit (experimental)"),
    Needs(L"gameplay", L"melee_touch", 1, Toggle(L"gameplay", L"melee_direct",
           L"Direct hit (experimental, offline only)", 0,
           L"EXPERIMENTAL, for comparing with the other way. Off: the touch presses the game's own attack, as above. On: at the touch the "
           L"plugin calls the game's own basic attack on the game's server itself - no animation, no wait for the game's attack rate, "
           L"no walking up to the monster (the blade's contact is the reach), no start checks (mana, ammunition present, town). Still "
           L"the game's: the hit roll, the damage, the weapon's wear. Only against a monster the game calls hostile and with no wall "
           L"between. Offline only: with no local game server found, or after a fault, it falls back to the game's own key. "
           L"Off: as before. Live. F4 only, like hit by touch.")),
    Needs(L"gameplay", L"melee_direct", 1, Slider(L"gameplay", L"melee_direct_min_ms", L"Direct hit: min gap between hits, ms", 0, 1500, 50, 0,
           L"0: no limit beyond \"a monster is hit once per stroke\". More: after a direct hit, the touches within this time do nothing - "
           L"to keep the rate near the game's own if hits come too fast. Live.")),
    Group(L"Quick strike"),
    Toggle(L"hands", L"quick_strike", L"Quick strike: the blow lands earlier in the attack animation (changes the game's timing)", 0,
           L"A CHANGE OF THE GAME'S TIMING (off when the key is missing; on in the settings that come with the mod). The game's melee blow lands at one frame of the hero's attack animation - for a Paladin with a one-hand weapon frame 7 or 8, "
           L"0.28 - 0.32 s after the animation starts, in your log 317 - 339 ms after the touch pressed the attack (the Direct hit lands in 15 - 50 ms). On: that frame is moved to the "
           L"frame below in the game's animation table, so the blow - its hit roll, damage and the monster's reaction - comes up to ~0.3 s sooner INSIDE THE SAME ATTACK. The "
           L"animation's length and the attack rate stay (the next attack can still start only when the animation ends), mana and wear are the game's but happen earlier, and "
           L"a monster's hit in the first ~0.3 s no longer cancels your blow. It applies to every melee attack of your class - the plain attack and the skills that play it (Zeal, "
           L"Bash, Berserk, Sacrifice...), the Paladin's Smite too - in VR first person only and offline only (this game's own tables); put back exactly when you switch it "
           L"off, change view or leave the game. Never the bow's, the crossbow's or a throw's animation. Live. [debug] bow_latency shows the animation row on the press-to-hit line."),
    Needs(L"hands", L"quick_strike", 1, Slider(L"hands", L"quick_strike_frame", L"Quick strike: the frame the blow lands at", 1, 12, 1, 1,
           L"The animation frame (a frame is 0.04 s) the blow lands at instead of the original (6 to 14 for the classes' melee attacks). 1 is the earliest the game can act: frame 0 "
           L"is never looked at when an animation starts. A value at or past the original changes nothing. Live.")),
    Group(L"Shield bash"),
    Needs(L"openxr", L"on", 1, Toggle(L"gameplay", L"shield_smite", L"Shield bash casts Smite", 0,
           L"Paladin, native OpenXR, first person (F4): with Smite on one of the pad's ten skill slots and a shield on your left "
           L"arm, a hard swing or thrust of the shield through a monster casts one Smite at it - the game's own: the slot's "
           L"button is pressed for a moment and the monster the shield touched is the cast's target. Reach, mana, cast rate, "
           L"animation, damage, stun and knockback stay the game's. A shield at rest does nothing, nor does a bash with the right "
           L"trigger down. Off: the shield casts nothing. F4 only.")),
    Needs(L"gameplay", L"shield_smite", 1, Slider(L"gameplay", L"shield_smite_min_speed", L"Shield bash: the least speed, m/s", 0.3f, 8, 0.1f, 0.8f,
           L"How fast the shield must be moving for a bash to count. A shield moving slower does nothing, however it touches a monster. "
           L"The default 0.8 is what the shield always used. This is the bash's OWN number - \"Hit by touch: the least blade speed\" does not "
           L"apply to the shield. The speed is your left hand's: its movement plus its turn times the shield's length (0.4 m), frame to "
           L"frame. Lower, and the speed that may begin a bash (1.5 x, at most 1.2) and the speed that counts as rest go lower with it; "
           L"from 0.8 up they are as before. A movement is also a bash only after the shield has moved the distance below within about a "
           L"tenth of a second. Tick \"Draw what casts skills\" (Debug group below) to see it: the shield turns green while a bash would count, "
           L"the white and orange balls show where its speed reached this number and fell below it, and the log line shows the speed "
           L"against it. Only decides when the press goes out - never the cast itself. Live. F4 only.")),
    Needs(L"gameplay", L"shield_smite", 1, Slider(L"gameplay", L"shield_smite_stroke_cm", L"Shield bash: how far the shield moves to start a bash, cm", 2, 30, 1, 8,
           L"A movement is a bash - and can cast - only after the shield has moved this far within about a tenth of a second, and fast "
           L"enough (above). Smaller: a short thrust counts; larger: only a long swing does. Default 8 as it always was; the bash's own number, "
           L"apart from \"Hit by touch: how far the blade moves to start a stroke\". Live. F4 only.")),
    Group(L"Debug (the same ticks as on the Debug tab)"),
    Toggle(L"debug", L"melee_boxes", L"Draw the capsules of Hit by touch", 0,
           L"First person (F4) with \"Hit by touch\" on: the blade's capsule (cyan) and the capsule of every monster the touch looks at "
           L"(yellow; pale where the game's own checks refuse it - not hostile, or a wall between), drawn through everything, so they can be "
           L"seen against the monsters. The monster a touch was accepted on shows red for half a second when the game's own attack key "
           L"was pressed for it, magenta when \"Direct hit\" took it. Five times a second the log gets a line of numbers: the nearest "
           L"monsters' capsule size and height, how far the blade's tip is from them, the stroke speed. A drawing and a log only - "
           L"it changes nothing in what the touch does. Live. The nearest three monsters are always drawn, hostile or not (pale where "
           L"the game refuses them); with two melee weapons held (a Barbarian's, an Assassin's claws) both blades are drawn, the "
           L"second one teal. While a contact would count (a stroke on, the blade at or above \"Hit by touch: the least blade speed\") "
           L"the blade is green; a swing leaves marks: a WHITE ball where the blade's speed reached that least speed (and the start "
           L"distance was met), an ORANGE ball where it fell below it again, and the tip's path as a thin trail, cyan where a contact "
           L"would not count and green where it would - tune the least speed and the start distance by where they land in your swing. "
           L"A contact is judged by the strike rules (the Strikes tab): the blade, the trail and the marks are green only while a contact with the monster nearest the blade would count now. "
           L"At every contact that was REFUSED, for \"Hit by touch: how long the speed marks and the trail stay\": a BALL on the point of the blade that touched, an ARROW along the way that point "
           L"was moving, a thin LINE from it to the nearest point of the monster's body axis, and the monster's capsule tinted with the colour of the reason - RED: not toward the monster (a "
           L"wind-up, a pull-back, a graze; BLUE instead with \"Not toward the monster in red\" unticked - red is also the monster's flash when a blow is pressed); AMBER: the swing "
           L"was too small for this kind, or too slow where it touched, or the blade entered the monster in a movement that never became a stroke; GREY: not the head (the shaft or the handle of "
           L"a mace touched). A contact that counted leaves green marks and the monster's own red / magenta flash. The log says why, one line for each refused contact (the kind, the angle, the "
           L"speed or the amplitude), at most one every 250 ms for each reason; the first twelve are always told."),
    Slider(L"debug", L"melee_mark_ms", L"Hit by touch: how long the speed marks and the trail stay, ms (0 = none)", 0, 3000, 100, 800,
           L"With \"Draw the capsules of Hit by touch\" on: how long the white / orange balls (where the blade's speed reached the least "
           L"speed and where it fell below it) and the tip's trail stay in the picture after a swing. 0 = no marks and no trail, the "
           L"blade's colour only. Live."),
    Toggle(L"debug", L"melee_direction_red", L"Hit by touch: not toward the monster in red (off: blue)", 1,
           L"With \"Draw the capsules of Hit by touch\" on: the colour of a contact the direction rule refused (the blade was moving away from the monster or across it - a wind-up, a pull-back, "
           L"a graze). On (the default): RED. Off: BLUE - red is also the monster's flash when a blow is pressed, which a blue refusal cannot be taken for. The other reasons keep their "
           L"colours: amber - the swing too small or too slow, grey - not the head. A drawing only. Live."),
    Toggle(L"debug", L"skill_triggers", L"Draw what casts skills", 0,
           L"First person (F4): draws and logs the triggers that cast a skill, so they can be tuned by eye - for now the SHIELD BASH that "
           L"casts Smite (\"Shield bash casts Smite\"): the shield's capsule (the one the bash tests monsters with) on the left arm in violet, "
           L"green while a bash would count (a stroke on, at or above \"Shield bash: the least speed\", the right trigger up, Smite on a pad "
           L"slot...); a WHITE ball at the shield's centre where its speed reached that speed and an ORANGE one where it fell below it; its "
           L"path as a thin trail, cyan where a bash would not count and green where it would; the monsters near it (yellow) and the one a "
           L"bash pressed Smite on, red. Five times a second the log gets a \"skill trigger shield bash:\" line: the shield's speed against "
           L"the bash speed and its start distance, the right trigger, the pad button of Smite's slot, the mana against Smite's cost, the "
           L"nearest monster, and why a bash would not count (too slow, no stroke yet, right trigger down, no Smite slot, no mana, not hostile...). "
           L"It works with the shield bash off too (the line then says so). Other skill triggers (the bow's draw, a throw) can join it later. A drawing and a log only - it changes "
           L"nothing in when anything fires. Apart from \"Draw the capsules of Hit by touch\", which is for the melee blade. Needs the "
           L"D2R_DepthFog.fx of the same build (the old one draws the shield's capsule green). Live. F4 only."),
    Slider(L"debug", L"skill_mark_ms", L"Skill triggers: how long the speed marks and the trail stay, ms (0 = none)", 0, 3000, 100, 800,
           L"With \"Draw what casts skills\" on: how long the white / orange balls and the shield's trail stay in the picture after a bash. "
           L"0 = no marks and no trail, the capsule's colour only. Live."),
    Slider(L"debug", L"melee_blade_length_pct", L"Hit by touch: the blade's length from the weapon bone to its tip, % of the weapon's own", 50, 150, 5, 100,
           L"Tunes the blade of Hit by touch - the capsule drawn by \"Draw the capsules of Hit by touch\" is the blade the touch tests - to "
           L"the weapon as you see it: 100 = the length the plugin has for this weapon, from the weapon bone (for a staff, spear or two-handed "
           L"weapon the middle of the grip, not the end of the weapon) along the weapon to its tip. It only tunes where the touch counts a "
           L"hit; it never changes the game's reach, rolls or attack. First person (F4) only. Live."),
    Slider(L"debug", L"melee_blade_butt_pct", L"Hit by touch: the blade's length behind the weapon bone (the butt end), % of the weapon's own", 0, 150, 5, 100,
           L"The other side of the weapon bone: how far the blade runs behind the grip to the butt of the staff, spear or pole-arm. 100 = what "
           L"the plugin has for this weapon (a staff: as far behind as before), 0 = none, so that only the tip side counts. Same rules as the "
           L"length: only where the touch counts a hit, never the game's reach or rolls. First person (F4) only. Live."),
    Slider(L"debug", L"melee_blade_radius_cm", L"Hit by touch: the blade's thickness (radius), cm (0 = the weapon's own)", 0, 15, 0.5f, 0,
           L"0: the radius the plugin has for this weapon (a staff's is thin, an axe's wide). Else the blade's radius in cm, 0.5 to 15. "
           L"Only where the touch counts a hit - never the game's reach or rolls. First person (F4) only. Live."),
    Slider(L"debug", L"melee_thrown_length_pct", L"Hit by touch: a thrown weapon's blade, tip side (javelin, throwing knife and axe), %", 50, 250, 5, 100,
           L"The tip side of the blade of a JAVELIN, THROWING KNIFE or THROWING AXE only, on top of the general length above - those blades are "
           L"estimates (the javelin's head side, 1.04 m from the weapon bone along its -Y, was measured on two screenshots; the others were not "
           L"seen). With \"Draw the capsules of Hit by touch\" on, the capsule's far end - the HEAD side - should reach the weapon's real point, "
           L"the spearhead's; raise or lower this until it does. Only where the touch counts a hit - never the game's reach or rolls. "
           L"First person (F4) only. Live."),
    Slider(L"debug", L"melee_thrown_butt_pct", L"Hit by touch: a thrown weapon's blade, butt side (behind the weapon bone), %", 0, 300, 10, 100,
           L"The side of the blade behind the weapon bone - the BUTT end, behind the hand - for a javelin, throwing knife or throwing axe only "
           L"(the javelin's is 0.30 m, a guess; the bone is at the hand). Same rules as the head side. First person (F4) only. Live."),
    Toggle(L"debug", L"melee_blade_flip", L"Hit by touch: the blade runs the other way along the weapon", 0,
           L"On: the blade's tip lies on the other side of the weapon bone than the plugin assumes, for every weapon (the dagger's "
           L"already runs the other way by default). For a weapon whose capsule points away from its drawn blade. Only where the touch "
           L"counts a hit. First person (F4) only. Live."),
    // [weight] (vr/weight.cpp): weapons with weight, taken where the grip closes on the shaft, passed from hand to hand
    Only(kVr, Tab(L"Weapon Weight")),
    Group(L"Weight (F4, the arms on the controllers)"),
    Needs(L"arms", L"mode", 2, Toggle(L"weight", L"on", L"Weapons have weight; take and pass them with the grip", 0,
           L"A melee weapon held alone is heavy: it lags behind a hard swing, a two-handed one wants both hands, a spear "
           L"held by its end is slower than held where the game holds it. You hold it with the grips: let both go and it falls. "
           L"First person (F4) only, with BodyWalk's tracking or native OpenXR (with or without \"Controls without BodyWalk\"). "
           L"A grip next to the belt is still a potion, a grip behind a shoulder is still the weapon swap, and hit by touch follows "
           L"the weapon as it is drawn - the hand that carries it swings it; one let go strikes nothing. "
           L"Off: the weapon sits fast in the game's grip, as before.")),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"feel", L"Weight, % of the real mass", 0, 300, 5, 50,
           L"A dagger weighs 0.4 kg, a sword 1.2, a two-handed sword 2.5, a spear 1, a polearm or a maul 3.5. "
           L"50 by default (half the real mass); 100 = the real mass; 0: no weight, the weapon only goes from hand to hand."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"strength", L"Hands' strength, %", 10, 500, 5, 100,
           L"How hard the hands pull the weapon after the controllers: stronger, it lags less and swings round less. "
           L"With the gravity feel it also stiffens the wrists, so the weight sags them less."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"gravity_feel", L"Gravity feel: wrists the weight can sag", 1,
           L"Ticked: the weight is gravity. The wrist is soft near the line of your controller and firm far off it, so a sword held out level "
           L"sinks a few degrees, a polearm or a two-hander in one hand sinks a lot (two hands hold it much better), a swing down is helped "
           L"by the weight and lifting it up is slowed by it, and a hard cut is still followed. Unticked: the old stiff wrists, where the weight "
           L"showed only as a lag. Gravity and Damping below work either way."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"gravity_pct", L"Gravity, %", 0, 300, 5, 100,
           L"How hard gravity pulls the weapon's steel down (100 = the real 9.8 m/s2). Held out, a heavy weapon sinks by this; a swing down is "
           L"helped by it and lifting up is slowed by it. 0: no gravity at all, only inertia (the weapon lags the same way up and down). "
           L"A weapon you let go always falls at the real g."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"damping_pct", L"Damping, %", 0, 200, 5, 100,
           L"How much the wrists damp the weapon's motion against the controller's (100 = nearly critical, so it settles without a wobble). "
           L"The damper follows the controller's speed, it is not a drag: lower, the weapon rings and lags more in a swing, higher, it follows more tightly."))),
    // [weight] hands_model: the weight-physics library (d2r_vr_weight.dll beside the plugin, vr/weight_lib.h); greyed out while there is none
    Hide(Slider(L"@weightlib", L"found", L"", 0, 1, 1, 0)),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Status(L"weight_lib", L""))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"@weightlib", L"found", 1, Toggle(L"weight", L"hands_model", L"Move the weapon with the weight-physics library (A/B)", 0,
           L"Ticked: the weapon and the hands are moved by the weight-physics library (d2r_vr_weight.dll, in d2rloader\\d2r_vr_libs) instead of the mod's own physics. "
           L"Gravity %, Damping %, Weight % and Hands' strength % work with either. Switch it while you hold the weapon to compare. Greyed out while the library is not "
           L"there: the mod then has only its own physics.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"@weightlib", L"found", 1, Needs(L"weight", L"hands_model", 1, Toggle(L"weight", L"hands_model_palm",
           L"Library: the weapon's weight in the palm", 0,
           L"Takes the weapon's centre of mass into the palm while it is held: a heavy weapon turns about your hand, nothing sags, a swing needs far less effort. "
           L"A comfort setting; unticked, the weight sits where the steel is. Only with the library."))))),
    // [weight] grip_model: the grip library (d2r_vr_grip.dll beside the plugin, vr/grip_lib.h); greyed out while there is none
    Hide(Slider(L"@griplib", L"found", L"", 0, 1, 1, 0)),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Status(L"grip_lib", L""))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"@griplib", L"found", 1, Toggle(L"weight", L"grip_model", L"Hold the weapon the grip library's way (A/B)", 0,
           L"Ticked: how a hand takes hold of the shaft and looks while it does is the grip library's (d2r_vr_grip.dll, in d2rloader\\d2r_vr_libs) instead of the mod's own: the hand sits on the wood "
           L"with its palm touching it, the fingers close until they meet it, the second hand of a long shaft is held by its grip - tight, where it is - or, with the grip let go and the "
           L"trigger squeezed, loose: it slides along the shaft and turns about it; the hand feels a pulse when it comes within reach, closes and lets go. The weight, the aim and the "
           L"game's own attacks are not touched. Greyed out while the library is not there: the mod then has only its own way.")))),
    Group(L"Where the second hand takes the weapon (F4)"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"grip_hilt_only", L"Swords, axes, maces: the second hand takes only the hilt", 0,
           L"Ticked: with one hand holding a sword, a dagger, a scepter, an axe or a mace, the OTHER hand takes it only on its hilt (the haft, for an axe or a mace), from the pommel to the "
           L"guard and a few centimetres past - not on the blade or the head. A grip there takes nothing, so it is free for something else (a belt potion, for one). The staff, the spear, "
           L"the polearm and the other long weapons keep the whole shaft. Works with the mod's own way of holding and with the grip library. Unticked: the second hand takes the weapon "
           L"anywhere along it, as ever. The guard's place is not in the game's data, so it comes from the weight model's shape for the kind, scaled onto the weapon as it is drawn; the "
           L"lengths below set it by hand. The log says where the hilt ends for each weapon."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_hilt_only", 1, Slider(L"weight", L"grip_hilt_cm_sword", L"Sword: the hilt, cm (0 = from the model)", 0, 150, 1, 0,
           L"Live. How long the hilt is, in centimetres from the pommel to the guard: the second hand takes the weapon only up to here (and a few centimetres past it). 0: the weight model's own - the first piece of its shape for this kind, scaled onto the weapon as it is drawn (a two-handed sword about 30 cm, a one-handed one about 17).")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_hilt_only", 1, Slider(L"weight", L"grip_hilt_cm_axe", L"Axe: the haft, cm (0 = from the model)", 0, 150, 1, 0,
           L"Live. How long the hilt is, in centimetres from the pommel to the guard: the second hand takes the weapon only up to here (and a few centimetres past it). 0: the weight model's own - the first piece of its shape for this kind, scaled onto the weapon as it is drawn (a two-handed axe about 90 cm).")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_hilt_only", 1, Slider(L"weight", L"grip_hilt_cm_mace", L"Mace, hammer: the haft, cm (0 = from the model)", 0, 150, 1, 0,
           L"Live. How long the hilt is, in centimetres from the pommel to the guard: the second hand takes the weapon only up to here (and a few centimetres past it). 0: the weight model's own - the first piece of its shape for this kind, scaled onto the weapon as it is drawn (a maul about 90 cm).")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_hilt_only", 1, Slider(L"weight", L"grip_hilt_cm_scepter", L"Scepter: the shaft, cm (0 = from the model)", 0, 150, 1, 0,
           L"Live. How long the hilt is, in centimetres from the pommel to the guard: the second hand takes the weapon only up to here (and a few centimetres past it). 0: the weight model's own - the first piece of its shape for this kind, scaled onto the weapon as it is drawn.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_hilt_only", 1, Slider(L"weight", L"grip_hilt_cm_dagger", L"Dagger: the hilt, cm (0 = from the model)", 0, 150, 1, 0,
           L"Live. How long the hilt is, in centimetres from the pommel to the guard: the second hand takes the weapon only up to here (and a few centimetres past it). 0: the weight model's own - the first piece of its shape for this kind, scaled onto the weapon as it is drawn (about 10 cm).")))),
    Group(L"The hero's strength (F4)"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"hero_strength", L"The hero's strength decides how heavy a weapon feels", 0,
           L"Ticked: the hero's Strength (the game's stat: points, gear and all) against the weapon's own Strength requirement (the game's Weapons table) scales how heavy it feels - the "
           L"steel's mass (the lag behind a swing, the sag held out, the swing's inertia) and the hands' strength (a two-hander in one hand sags and lags less in a strong hand, so the second "
           L"hand is needed less). A Barbarian with plenty of Strength swings a Great Sword or a Maul easily; a Sorceress with little feels the same weapon heavy. It works with the mod's "
           L"own physics and with the weight-physics library alike. Unticked: the weight is the same for every hero, as ever. The class multipliers and \"Strength matters\" below tune it; "
           L"the log says what it did for each weapon."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"strength_effect", L"Strength matters, %", 0, 200, 5, 100,
           L"How much the hero's strength moves the weight. 100: twice the margin over the requirement halves the lag; 0: not at all (the same as unticked); 200: twice as much.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_amazon", L"Amazon: strength multiplier, %", 10, 300, 5, 100,
           L"The hero's Strength stat times this is what counts: a class that is stronger than its stat says gets more, a weaker one less. Barbarian highest by default.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_assassin", L"Assassin: strength multiplier, %", 10, 300, 5, 100)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_barbarian", L"Barbarian: strength multiplier, %", 10, 300, 5, 130)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_druid", L"Druid: strength multiplier, %", 10, 300, 5, 95)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_necromancer", L"Necromancer: strength multiplier, %", 10, 300, 5, 90)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_paladin", L"Paladin: strength multiplier, %", 10, 300, 5, 110)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_sorceress", L"Sorceress: strength multiplier, %", 10, 300, 5, 80)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"hero_strength", 1, Slider(L"weight", L"str_warlock", L"Warlock: strength multiplier, %", 10, 300, 5, 95)))),
    Group(L"Taking and passing (the grip)"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"grab_cm", L"A grip takes the weapon within, cm", 3, 40, 1, 12,
           L"A hand holds the weapon while its grip is squeezed. Squeeze it with your fist this near the shaft (between its butt "
           L"and its tip) and that hand takes the weapon there. Let a grip go and that hand lets go at once - the other hand, if it "
           L"holds, keeps it. Both let go, it falls to the floor; take it up there, or squeeze the grip of the hand the game holds "
           L"it in, away from it, and it flies back into that fist. A new weapon sits in the game's hand until you first let that "
           L"grip go. A hand with a belt potion takes nothing."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"let_go_cm", L"The game's hand lets go pulled away, cm", 5, 80, 1, 25,
           L"A new weapon in the game's hand, its grip never squeezed yet, with the other hand on it too: pull the game's hand "
           L"this far away from its place on the weapon and it lets go."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"slide_second_hand", L"The second hand slides along a long shaft", 1,
           L"Two hands on a long shaft (a staff, a spear, a polearm, a two-handed sword, axe or mace): the first hand holds where it took it, and the "
           L"second hand - while its grip stays squeezed - slides along the shaft with its controller, as the left hand did on a staff before. The "
           L"weapon turns along the line between the hands, as ever. Off the end of the shaft, or pulled away from it, that hand lets go. Off: the "
           L"second hand holds where its grip closed, on every weapon. On: it slides on the kinds of weapon ticked in the list below - a staff's, a spear's and a polearm's "
           L"second hand runs along the pole, a sword's, an axe's or a maul's stays on the hilt."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_sword", L"Swords (two-handed)", 0,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_axe", L"Axes (two-handed)", 0,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_mace", L"Maces, hammers (two-handed)", 0,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_staff", L"Staffs", 1,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_polearm", L"Polearms", 1,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_spear", L"Spears", 1,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_javelin", L"Javelins", 0,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"slide_second_hand", 1, Toggle(L"weight", L"slide_second_hand_other", L"Other weapons", 0,
           L"Ticked: with both hands on this kind of weapon the second hand slides along the shaft with its controller while its grip stays squeezed (with the grip library also "
           L"loose by the trigger); off an end, or pulled away, it lets go. Unticked: the second hand holds where it closed - tight, the point and the turn fixed - and the weapon turns "
           L"with the line between the hands. A staff's, a spear's and a polearm's second hand runs along the pole; a sword's, an axe's or a maul's stays on the hilt. Works with the "
           L"mod's own way of holding and with the grip library alike.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"attach_apart", L"Keep the weapon's two attach bones apart (the staff is not flat)", 1,
           L"The game lays a two-handed weapon (a staff, a spear, a polearm, a two-handed sword, axe or mace) along the line between its two attach bones - one on each wrist, "
           L"both hands on the haft. A weapon with weight is drawn where the model has it, and with both bones put in ONE point that line has no length: the weapon is drawn "
           L"squashed flat along its length - the staff shows as a dark disc though the grip and the red line are right. Ticked: the second attach bone is kept [hands] carry_cm "
           L"(40 cm, in the ini) along the weapon's own axis from the first, as it always was for a weapon in hand, so the weapon keeps its length and lies where the model has it. "
           L"Unticked: both in one point. The log says once for each weapon what the game's own two bones look like."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"fist_x",L"Fist's middle from the controller: X, cm", -15, 15, 0.5f, 0,
           L"Where the closed fist sits round the shaft, from the controller, along its own axes (mirrored for the left hand). "
           L"Move it until a hand that takes the weapon closes round the shaft, not beside it."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"fist_y", L"Fist's middle from the controller: Y, cm", -15, 15, 0.5f, 0))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"fist_z", L"Fist's middle from the controller: Z, cm", -15, 15, 0.5f, 0))),
    // [weight] grip_fit (vr/grip_fit.h): the hand that takes the weapon is laid on its shaft; live corrections per hand in the shaft's own frame
    Group(L"Grip fit (the hand on the shaft)"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"grip_fit", L"Lay a hand that takes the weapon on the shaft", 1,
           L"A hand that TAKES the weapon (it closes its fist on the shaft: the second hand, a hand that takes it over from the other, the game's hand taking it up again) "
           L"sits on the shaft like the base grip: the middle of its palm goes onto the shaft's axis (the red line) at the place it took it, the line through its knuckles is "
           L"laid along the shaft (the roll about the shaft stays your controller's), and its fingers close round it as you squeeze the grip - also with \"Free hands make a "
           L"fist\" off. The fist sliders above decide WHERE along the shaft the hand takes it; the sliders below move and turn each hand from there. The hand the game holds "
           L"the weapon in keeps the game's own pose: only its sliders act on it. Unticked: the hands are drawn at the controllers as before. [debug] phantom_ray shows it: "
           L"a yellow dot where the hand takes the shaft, a green ring where its palm really is, a white line between them."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Toggle(L"weight", L"grip_fit_like_base", L"The taking hand sits on the shaft like the game's own grip", 1,
           L"The game's own pose of the hand that holds the weapon (the base grip) is the reference: how far its palm stands from the shaft's axis (across the hand and out of the "
           L"palm) and how its knuckle line leans to the shaft is measured live, and a hand that takes the weapon is given the same - so both hands sit on the staff the same way "
           L"(the fingers: see the next tick). Unticked: the taking hand's palm is exactly on the axis.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Toggle(L"weight", L"grip_fit_fingers", L"The taking hand's fingers take the game's weapon-grip pose", 1,
           L"The fingers and the thumb of a hand that TAKES the weapon get the game's own weapon-grip pose - the one the hand the game holds it in has (wrapped round the shaft, the "
           L"thumb on it), read off the game's animation and put on the taking hand mirrored as that hand needs. It is held as it is while the hand holds the weapon (the squeeze does "
           L"not drive the fingers any more) and the fingers open as the hand lets go or hands the weapon over. Independent of \"Free hands make a fist\". A staff, a spear, a sword in "
           L"two hands: whatever the game's hand does on that weapon. Unticked: the fingers just close with your squeeze.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_x", L"Right hand X: along the shaft (+ toward the tip), cm", -30, 30, 0.5f, 0,
           L"Live. The RIGHT hand on the weapon, moved from where it holds the shaft in the SHAFT'S OWN FRAME, turned as the hand holds it - so a fix found for one grip is right at "
           L"any roll: X along the shaft (toward the tip), Y across (toward the fingertips), Z up (out of the palm). The rotations are about the same axes: X the roll about the "
           L"shaft, Y the tilt about the across axis (the fingers up / down), Z the tilt about the up axis (the fingers sideways). It acts whichever state the right hand is in: the "
           L"game's hand as placed (these move it from the game's pose), or a hand that took the weapon (on top of the fit and the base grip's relation); one hand or two; "
           L"carrying it or not. The weapon never moves with these - only the hand. The left hand's turns are mirrored from the right's. They work with the grip library too (\"Hold the weapon the grip library's way\"): the same numbers, the same frame - the moves put the hand exactly where they put ours, the turns orbit it about the shaft.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_y", L"Right hand Y: across (+ toward the fingertips), cm", -30, 30, 0.5f, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_z", L"Right hand Z: up (+ out of the palm), cm", -30, 30, 0.5f, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_rx", L"Right hand rotation X: roll about the shaft, deg", -180, 180, 1, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_ry", L"Right hand rotation Y: tilt about across, deg", -180, 180, 1, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"right_grip_rz", L"Right hand rotation Z: tilt about up, deg", -180, 180, 1, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_x", L"Left hand X: along the shaft (+ toward the tip), cm", -30, 30, 0.5f, 0,
           L"Live. The same six for the LEFT hand on the weapon, in the same frame (X along the shaft toward the tip, Y across toward the fingertips, Z up out of the palm; the "
           L"rotations the roll about the shaft and the two tilts). The same numbers on the two hands make mirror images. All zero: no change at all.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_y", L"Left hand Y: across (+ toward the fingertips), cm", -30, 30, 0.5f, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_z", L"Left hand Z: up (+ out of the palm), cm", -30, 30, 0.5f, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_rx", L"Left hand rotation X: roll about the shaft, deg", -180, 180, 1, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_ry", L"Left hand rotation Y: tilt about across, deg", -180, 180, 1, 0)))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"weight", L"grip_fit", 1, Slider(L"weight", L"left_grip_rz", L"Left hand rotation Z: tilt about up, deg", -180, 180, 1, 0)))),
    // [weight] thrown_fly_ms (vr/weight_throw.h): a thrown weapon's model
    Group(L"A thrown weapon"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Choice(L"weight", L"thrown_model", L"A thrown weapon's model", 0,
           {L"Vanishes when the game throws", L"Flies on until it lands"},
           L"With the throw by the grip (Weapon Adjust > Throws): letting the grip go as a throw lets the weapon's model leave the hand and fall with the motion it "
           L"had, like any weapon you drop - but it does not lie on the floor or go to the back. The game then throws a missile of its own, and a model "
           L"flying on beside it is a second weapon. Vanishes: the model is gone the moment the game takes the throw (the hero goes into its throw), and "
           L"a fresh one stands in the hand, drawn by the game. Flies on: the model keeps falling until it lands, the game's throw animation is over, or "
           L"the time below. If the game does not take the throw (refused: no Throw skill on a pad slot, nothing to aim at), the model flies and ends as in "
           L"the second choice, either way."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"weight", L"potion_model_flies", L"Your potion flies all the way to its landing", 1,
           L"A thrown POTION's model (not a javelin's, a knife's, an axe's - those follow the choice above) is not handed over to the game's own missile when the game "
           L"takes the throw: it flies the whole trajectory you threw it on and breaks where it lands (it is gone, the next potion is in your hand). It is the potion "
           L"you see: to hide the game's own one, which would fly beside it, tick the tick that does it (\"The game's own flying potion is not drawn\" on the Weapon Adjust tab). "
           L"Without it the potion's model is gone the moment the game takes the throw, as a javelin's is. Live."))),
    ShowFor(L"weight", L"thrown_model", 1u << 0, Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"thrown_handoff_ms",
           L"The model flies out of the hand this long, then hands over, ms", 0, 1500, 10, 0,
           L"With the throw by the grip: when you let the grip go as a throw the weapon's model FLIES OUT of your hand along the throw (the weight model's own "
           L"flight, with the speed your hand gave it); then it hands over to the game's own missile - it vanishes, and the game's animation and missile are "
           L"what continues the throw; the hand stays empty until that animation is over (the next slider). It hands over when the game's missile is up "
           L"(its animation's action frame, where that is known) or this long after the release, whichever comes first. 0 (the default): at the moment the game takes the throw, "
           L"with no flight to see - only the game's own missile flies, so there are never two weapons in the air (one on the weight model's trajectory, one drawn by the animation).")))),
    ShowFor(L"weight", L"thrown_model", 1u << 0, Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"thrown_hand_empty_ms",
           L"The hand stays empty while the game throws, at most, ms", 0, 3000, 50, 900,
           L"When the game takes a throw the model is gone at once, and the hand must stay EMPTY while the game's own throw animation plays - else the spear "
           L"you threw is seen in your hand while the game's own flies. The weapon is not drawn from the moment the game takes the throw until the hero's "
           L"animation is over (a moment after it), then the game's next one is in the hand. This is the longest it may stay empty: it never stays hidden "
           L"if the signal is lost. 0: not hidden (the next one is drawn at once). If it was the last one, the game takes it out of the hand anyway.")))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"thrown_fly_ms", L"A thrown weapon's model flies at most, ms", 100, 5000, 50, 3000,
           L"How long the model of a thrown weapon may fly after the release at the most: it ends sooner when it lands (or, with Vanishes, when the game "
           L"takes the throw). The game's own missile flies and hits as ever."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"throw_reach_pct", L"Throws: how far, % of the real throw", 50, 1500, 10, 800,
           L"The weight model works the throw out itself: the weapon's peak velocity of the last 0.15 s before the release - as the hand really moved it, "
           L"a heavy weapon leaves slower than a light one - flown over the real ground. A real arm throws a few metres; a game screen wants tens. This scales "
           L"the HORIZONTAL part of that velocity (the fall stays real), and the throws of Weapon Adjust > Throws land where it says. Light and fast goes far, heavy "
           L"goes short. Lower it if a gentle swing goes too far."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Slider(L"weight", L"throw_max_cells", L"Throws: the farthest, cells", 10, 48, 1, 48,
           L"A throw never lands farther than this from the hero. The game's own command check refuses an aim point more than 50 cells off the hero on an "
           L"axis (never bypassed), so 48 is the most; a cell is two world units, about 0.5 m at this hero's size (the log says how many metres)."))),
    // [weight] after_drop (vr/weapon_back.h): what becomes of a weapon let go that has landed
    Group(L"A dropped weapon"),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Choice(L"weight", L"after_drop", L"A dropped weapon", 1,
           {L"Lies on the floor", L"Goes to the back", L"Vanishes"},
           L"What becomes of a weapon you let go, once it has landed. Lies on the floor: it stays where it fell, as before. Goes to the back: "
           L"after the delay below it disappears from the floor and hangs on the hero's back (a staff, a spear, a polearm, a javelin or any "
           L"two-handed weapon across it diagonally with the tip over the shoulder, a one-handed one low across it); it follows his body. "
           L"Vanishes: after the delay it is not drawn at all. The game's weapon stays his equipped one either way; only its drawing moves. "
           L"To take it back, in both: reach a hand behind either shoulder and squeeze the grip - it flies into that hand (the grip behind the "
           L"shoulder is the weapon swap only while nothing is away); or squeeze the grip of a free hand anywhere else, and it flies into that "
           L"hand. A weapon that is away strikes nothing."))),
    // [hands] shoulder_draw (vr/shoulder_draw.h)
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Toggle(L"hands", L"shoulder_draw", L"The shoulders hold your weapons: draw from behind a shoulder", 0,
           L"A weapon you let go (or throw) lies where it fell IN THE WORLD - it does not walk with you - and the grip of EITHER hand, squeezed anywhere, calls it back "
           L"into that hand while it is in the air or lies there. After the delay below it goes to the back (the choice above is not used), and from then on you "
           L"reach a hand behind EITHER shoulder and squeeze the grip to DRAW it: it flies into the hand that reached - either hand does (the left hand at the right "
           L"shoulder takes it into the left hand: a cross draw, for a tired arm). A short pulse in a hand that enters a shoulder where something is to draw. Behind "
           L"the shoulder the grip no longer swaps weapon sets: the game's own swap stays on its key. Off: as before (the swap behind the shoulder, the choice above)."))),
    Needs(L"arms", L"mode", 2, Needs(L"weight", L"on", 1, Needs(L"hands", L"shoulder_draw", 1, Slider(L"hands", L"shoulder_draw_stow_ms", L"Delay on the floor before it goes to the back, ms", 0, 5000, 50, 1000,
           L"How long a weapon you let go lies on the floor - where the grip of either hand calls it back from afar - before it goes to the back and has to be drawn from behind a shoulder. 0: at once. Live.")))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"shoulder_draw", 1, Toggle(L"hands", L"shoulder_draw_shield", L"... and the shield follows the sword to the other arm", 1,
           L"With a shield in the left hand: it stays strapped to the forearm, no grip holds it, and sword and shield are always on OPPOSITE arms: whenever the sword ends up in "
           L"the hand that wears the shield - drawn into it, taken up by it, passed into it - the shield jumps to the other forearm (mirrored on the right, where it blocks and "
           L"bashes (Smite) as well). A sword let go leaves it where it is. Nothing changes in the game's slots. Off: the shield stays on the left arm, and the hand that wears "
           L"it cannot take the weapon. Live."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"shoulder_draw", 1, Toggle(L"hands", L"shoulder_draw_shield_stow", L"... and the shield can be put away and drawn from behind the left shoulder", 0,
           L"A grip squeezed behind the LEFT shoulder by a hand that holds nothing PUTS THE SHIELD AWAY (it is not drawn and bashes nothing; it is still your equipped shield) - or, as with a "
           L"weapon, carry it there: the hand that wears the shield holds its grip (squeezed anywhere, or held already) and LETS GO behind the left shoulder. The "
           L"same grip while it is away DRAWS it onto the arm of the hand that reached - the right hand puts it on the right forearm, mirrored. Independent of the tick above: "
           L"with both, a shield drawn while the sword is in the other hand sits opposite it. With this on the left shoulder is the shield's and the weapon is drawn from the "
           L"right one only. A short pulse in a hand that enters the left shoulder while the shield is away. Live."))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"shoulder_draw", 1, Needs(L"hands", L"shoulder_draw_shield_stow", 1, Toggle(L"hands", L"shield_stow_no_block",
           L"... and a shield put away does not block", 0,
           L"While the shield is away behind the left shoulder the game rolls no block for you: it treats the hero as one with no shield and goes on to its own dodge check. "
           L"Drawn onto an arm, the shield blocks as ever - the game's own chance, roll, animation and Faster Block Rate; nothing is decided by where you hold it. The shield's "
           L"Defense stays (it is still equipped), and the character screen still shows its block chance. Off: a shield put away still blocks, as the game has it. Live.")))),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"shoulder_draw", 1, Toggle(L"hands", L"shoulder_draw_ranged", L"A bow or crossbow can be put away and drawn from behind a shoulder", 0,
           L"A grip squeezed behind EITHER shoulder by a hand that is not holding the string (or a potion) PUTS THE BOW OR CROSSBOW AWAY (it is not drawn, no string, no aim; it is still "
           L"your equipped weapon); the same grip while it is away draws it back, into its own hand. Independent of the shield ticks. A short pulse in a hand that enters a shoulder "
           L"while it is away. Live."))),
    Needs(L"arms", L"mode", 2, Toggle(L"hands", L"shoulder_zone_body", L"The shoulder zones fit your body (and reach to the spine)", 0,
           L"The reach behind a shoulder - for the weapon-set swap, the weapon's draw, the shield's and the bow's put-away - scaled by your eye height (the T-pose measurement when it is used, else "
           L"your height setting) and begun at the spine instead of 10 cm out: a hand reaching back over the neck lands in it. Off: the fixed reach, as before. Live.")),
    Needs(L"arms", L"mode", 2, Needs(L"hands", L"shoulder_draw", 1, Needs(L"hands", L"shoulder_draw_ranged", 1, Toggle(L"hands", L"shoulder_draw_ranged_swap", L"... and a bow is drawn into the hand that reached", 0,
           L"A BOW put away with one hand and drawn with the other goes into the hand that reached: a cross draw, for a tired arm. It is then held, drawn on, aimed and fed from that hand "
           L"- the mirror image of the left-hand bow; the string, the quiver and the orbs go to the other hand. A crossbow always comes back into its own hand (its stock and grip are "
           L"wired to the hands the game gives them). Off: a bow always comes back into the hand the settings hold it in. Live.")))),
    AwayShow(Slider(L"weight", L"to_back_delay_ms", L"Delay on the floor before it goes away, ms", 0, 3000, 50, 600,
           L"How long a weapon that has landed lies on the floor before it goes to the back or vanishes. 0: at once.")),
    AwayShow(Toggle(L"weight", L"recall_any_hand", L"The grip of any free hand calls it back", 1,
           L"On: squeezing the grip of an empty hand anywhere (not only behind a shoulder) calls the weapon that is away into that hand. "
           L"Off: only the hand the game holds it in does, and a hand behind a shoulder takes it. A hand with a belt potion takes nothing. "
           L"Note: a grip squeezed for a button chord (left grip + menu, left grip + stick click) calls it too, while it is away.")),
    AwayShow(Slider(L"weight", L"back_take_cm", L"Take zone at the weapon on the back, cm", 0, 60, 1, 25,
           L"A hand whose fist is this near the weapon where it hangs on the back - anywhere along it, not only behind a shoulder - takes it with the grip. "
           L"0: only the shoulder zone (a hand reached behind either shoulder). The weapon also hangs low across the back, below the shoulder zone: "
           L"reach to where it is. The log says how far the hand was when a squeeze took nothing.")),
    AwayShow(Toggle(L"weight", L"take_haptic", L"A pulse in the hand at the take zone", 1,
           L"A short pulse in a controller when its hand reaches behind a shoulder while a weapon is away, so the hand feels where to grab. "
           L"Needs controller haptics on (OpenXR: Haptics).")),
    AwayShow(Needs(L"weight", L"take_haptic", 1, Slider(L"weight", L"take_haptic_pct", L"Pulse strength, %", 5, 100, 5, 40))),
    BackNeeds(Slider(L"weight", L"back_long_x", L"Back, long weapon: right (+) / left (-), cm", -40, 40, 0.5f, 0,
           L"Live: move it while it hangs there. Long weapons (staff, spear, polearm, javelin, two-handed) lie diagonally across the back, "
           L"tip over the right shoulder. Axes of the hero's back: right, up, away from the back.")),
    BackNeeds(Slider(L"weight", L"back_long_y", L"Back, long weapon: up (+) / down (-), cm", -40, 40, 0.5f, 0)),
    BackNeeds(Slider(L"weight", L"back_long_z", L"Back, long weapon: away from the back (+), cm", -40, 40, 0.5f, 0)),
    BackNeeds(Slider(L"weight", L"back_long_pitch", L"Back, long weapon: pitch, deg", -180, 180, 1, 0,
           L"Turned about the hero's right axis (the tip toward the back or the front).")),
    BackNeeds(Slider(L"weight", L"back_long_yaw", L"Back, long weapon: yaw, deg", -180, 180, 1, 0,
           L"Turned about the vertical (the tip toward the left or right, back or front).")),
    BackNeeds(Slider(L"weight", L"back_long_roll", L"Back, long weapon: roll, deg", -180, 180, 1, 0,
           L"Turned about the shaft itself (which side of a blade faces the back).")),
    BackNeeds(Slider(L"weight", L"back_short_x", L"Back, short weapon: right (+) / left (-), cm", -40, 40, 0.5f, 0,
           L"Live: move it while it hangs there. Short weapons (sword, axe, mace, dagger, wand, scepter) lie across the lower back, tip to "
           L"the hero's left.")),
    BackNeeds(Slider(L"weight", L"back_short_y", L"Back, short weapon: up (+) / down (-), cm", -40, 40, 0.5f, 0)),
    BackNeeds(Slider(L"weight", L"back_short_z", L"Back, short weapon: away from the back (+), cm", -40, 40, 0.5f, 0)),
    BackNeeds(Slider(L"weight", L"back_short_pitch", L"Back, short weapon: pitch, deg", -180, 180, 1, 0)),
    BackNeeds(Slider(L"weight", L"back_short_yaw", L"Back, short weapon: yaw, deg", -180, 180, 1, 0)),
    BackNeeds(Slider(L"weight", L"back_short_roll", L"Back, short weapon: roll, deg", -180, 180, 1, 0)),
    Group(L"See it"),
    Toggle(L"debug", L"phantom_ray", L"Draw the weapon's shaft (red line)", 0,
           L"With weight on: the red line runs along the whole shaft as the weight model holds it - where a grip can take it. "
           L"The same switch as Debug's \"Draw the line the left hand takes\"."),
    // [flail] chain (vr/flail_chain.h): its own tick, independent of the weight above (2026-10-18, the user's wish: "the weight on a chain hangs and swings")
    Group(L"Flail: the head on its chain (F4)"),
    Needs(L"arms", L"mode", 2, Toggle(L"flail", L"chain", L"A flail's head swings free on its chain", 0,
           L"Flail, Knout, Scourge, Khalim's Flail and Khalim's Will: the spiked head hangs on its chain and swings as you move the hand - gravity "
           L"pulls it down, a swing throws it out and it lags behind, it settles when you stop - and when the weapon hits a monster by touch the head "
           L"bounces back off it. It is the game's own flail model: its chain's bones are moved, nothing is drawn over it. With hit by touch (Strikes) "
           L"it is the BALL that strikes - a sphere as big as the model's head, where the chain has it, judged by the ball's own speed and way (a swung "
           L"ball hits, a limp one does not); the handle strikes nothing. The attack it presses is the game's own: its reach, damage and rolls are as "
           L"before. First person (F4) only. Unticked: the chain keeps the game's animation (stiff in the hand) and the touch the handle's blade, "
           L"as before. Live.")),
    Needs(L"arms", L"mode", 2, Needs(L"flail", L"chain", 1, Slider(L"flail", L"damping_pct", L"Flail: damping, %", 0, 500, 5, 100,
           L"Live. How fast a swing of the head dies down against the hand (the links rubbing): 100 = a free swing halves in about 2 seconds; "
           L"less, it swings on longer; more, it settles quickly. 0: it never settles by itself."))),
    Needs(L"arms", L"mode", 2, Needs(L"flail", L"chain", 1, Slider(L"flail", L"swing_pct", L"Flail: how much the hand swings it, %", 0, 200, 5, 100,
           L"Live. How much of the hand's motion the head feels: 100 = real, it lags and swings out as a weight on a chain does; less, it is carried "
           L"along more stiffly (0: it only hangs under gravity); more, it swings out further than it would."))),
    Needs(L"arms", L"mode", 2, Needs(L"flail", L"chain", 1, Slider(L"flail", L"ball_weight_pct", L"Flail: the ball's weight, %", 25, 400, 5, 100,
           L"Live. How heavy the ball is against the model's own (100 = as before). Heavier: a swing carries on longer and dies down slower (the links' "
           L"rubbing slows a heavy ball less), a hit by touch turns it back less - it pushes through instead of bouncing - and, with \"Weapons have weight\" "
           L"on, the whole flail weighs more by the ball's part of it (about nine tenths of a flail is its iron ball): it lags behind the hand and sags more. "
           L"Lighter: the other way. Only the look and the weight model's feel; the game's attack and damage are as before."))),
    Needs(L"arms", L"mode", 2, Needs(L"flail", L"chain", 1, Slider(L"flail", L"hit_pct", L"Flail: bounce off a hit, %", 0, 200, 5, 100,
           L"Live. When the weapon hits a monster by touch (Strikes: hit by touch), the head bounces back off it: 100 = it comes back at about a third of "
           L"its speed; 0 = no bounce (it swings through); 200 = it is thrown back harder. Only the look - the hit itself is the game's."))),
    Only(kVr, Tab(L"Fists")),
    Group(L"Amazon"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"amazon_close", L"Grip squeezed, %", 0, 150, 1, 100, 
           L"With the arms on the controllers, a hand holding nothing (or the left one on a two-handed hilt) closes as you squeeze the grip: "
           L"how far, % of a full fist. Each hero's hand is built differently - set it per hero.")),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"amazon_open", L"Grip let go, %", 0, 150, 1, 20, 
           L"How far the fingers stay bent with the grip let go, % of a full fist. 0: an open hand.")),
    Group(L"Sorceress"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"sorceress_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"sorceress_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Necromancer"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"necromancer_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"necromancer_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Paladin"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"paladin_close", L"Grip squeezed, %", 0, 150, 1, 65)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"paladin_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Barbarian"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"barbarian_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"barbarian_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Druid"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"druid_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"druid_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Assassin"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"assassin_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"assassin_open", L"Grip let go, %", 0, 150, 1, 20)),
    Group(L"Warlock"),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"warlock_close", L"Grip squeezed, %", 0, 150, 1, 100)),
    Needs(L"arms", L"mode", 2, Slider(L"fist", L"warlock_open", L"Grip let go, %", 0, 150, 1, 20)),
#endif
    Only(kVr, Tab(L"Auto-target")),
    Group(L"Spells"),
    Needs(L"bow", L"mode", 4, Toggle(L"hands", L"spell_aim", L"Disable auto-target for spells", 1,
           L"On: a spell that flies - Fire Bolt, Charged Bolt, Teeth, Frozen Orb, Bone Spear... (as the game's own skill table says) - "
           L"aims along the hand as every attack does here, and the game's auto-target and aim assist are off for that cast: it no "
           L"longer turns to a monster. F4 only: in F1, F2, F3 and the flat modes the game's auto-target works as always. "
           L"Spells placed on the ground (Blizzard, Fissure, Meteor), summons, curses and auras keep the game's targeting. "
           L"Off: casting as before, with the game's auto-target. Below, a tick keeps the game's auto-target for that one spell - "
           L"one that seeks its target itself, as Chain Lightning.")),
#if D2RVR_FIRST_PERSON
    Toggle(L"hands", L"spell_aim_model", L"Aim spells by the drawn hand or staff", 0,
           L"VR first person (F4) only. On: the line a spell goes along comes from the hero as he is drawn, with no correction: a staff, wand or orb "
           L"in hand aims along itself, toward its head (with the weapon's weight on: along the shaft as your grip holds it); with none, along the hand's "
           L"fingers - or out of the palm when the fingers point up or down. A staff held upright aims with its hand. The attack along the hand ([bow] "
           L"mode 4) goes the same way. A bow, crossbow, javelin or throwing weapon keeps its own line. The game's own cast - skill, mana, missile - "
           L"is not changed, only where it is aimed. Off, or no fresh pose: the controller's line and the aim correction sliders (Weapon Adjust), as before."
           L"After d2r-pcvr (bearstonem, MIT). Debug > Aim rays: \"Spell's ray\" shows it."),
#endif
    Group(L"Amazon (ticked: the game's auto-target)"),
    Spell(L"s6", L"Magic Arrow"), Spell(L"s7", L"Fire Arrow"), Spell(L"s11", L"Cold Arrow"), Spell(L"s12", L"Multiple Shot"),
    Spell(L"s15", L"Poison Javelin"), Spell(L"s16", L"Exploding Arrow"), Spell(L"s20", L"Lightning Bolt"), Spell(L"s21", L"Ice Arrow"),
    Spell(L"s22", L"Guided Arrow", 1), Spell(L"s25", L"Plague Javelin"), Spell(L"s27", L"Immolation Arrow"),
    Spell(L"s31", L"Freezing Arrow"), Spell(L"s35", L"Lightning Fury"),
    Group(L"Sorceress (ticked: the game's auto-target)"),
    Spell(L"s36", L"Fire Bolt"), Spell(L"s38", L"Charged Bolt"), Spell(L"s39", L"Ice Bolt"), Spell(L"s41", L"Inferno"),
    Spell(L"s45", L"Ice Blast"), Spell(L"s47", L"Fire Ball"), Spell(L"s49", L"Lightning"), Spell(L"s53", L"Chain Lightning", 1),
    Spell(L"s55", L"Glacial Spike"), Spell(L"s64", L"Frozen Orb"),
    Group(L"Necromancer (ticked: the game's auto-target)"),
    Spell(L"s67", L"Teeth"), Spell(L"s84", L"Bone Spear"), Spell(L"s93", L"Bone Spirit", 1),
    Group(L"Paladin (ticked: the game's auto-target)"),
    Spell(L"s101", L"Holy Bolt"),
    Group(L"Druid (ticked: the game's auto-target)"),
    Spell(L"s229", L"Molten Boulder"), Spell(L"s230", L"Arctic Blast"), Spell(L"s240", L"Twister"), Spell(L"s243", L"Shock Wave"),
    Spell(L"s245", L"Tornado"),
    Group(L"Assassin (ticked: the game's auto-target)"),
    Spell(L"s266", L"Blade Fury"),
    Group(L"Warlock (ticked: the game's auto-target)"),
    Spell(L"s395", L"Miasma Bolt"),
    Tab(L"Debug"),
    Group(L"Debug"),
    Choice(L"debug", L"replay_marks", L"Replay: how the right eye's lists go to the GPU", 1,
           {L"0 - one batch a queue (hung, test-60..62)", L"1 - each list with a GPU mark (safe, default)", L"2 - each list, no marks",
            L"3 - in the game's own calls", L"4 - the game's calls, a mark after each"},
           L"Stereo from one pass: how the game's replayed command lists are sent to the GPU for the right eye. "
           L"1 (the default): each list on its own with a signal after it, so a hang's log names the list the GPU stopped at; every pair ~35 separate "
           L"submissions (~0.4 ms of the game's thread). 0: each queue's run of lists in one submission - on 2026-10-18 the graphics queue then ran a list "
           L"that never ended, 30 s to 3 min in. 2: each list on its own without the signals. 3: the same lists in the same calls as the game made them, no "
           L"marks - the game's own number of submissions (the first time, restart the game). 4: that with a signal after each call. Live."),
    Toggle(L"debug", L"ui_trace", L"Ctrl+F10 frame trace (slows every frame; restart the game)", 0,
           L"Ctrl+F10 writes one frame of the game's render targets and draws to d2r_vr_uitrace.txt. For that the plugin "
           L"watches every draw the game makes - ReShade then calls it thousands of times a frame, and looks up every "
           L"constant buffer the game binds: mono ran ~5-8% slower. Off (the default): Ctrl+F10 does nothing. Read at the game's start."),
#if D2RVR_FIRST_PERSON
    Toggle(L"debug", L"phantom_ray", L"Draw the line the left hand takes", 0,
           L"A red line along which the left hand takes the weapon in hand and slides (staff, spear, polearm, two-handed "
           L"sword); with a crossbow also where the bolt flies (Crossbow: shot line sliders turn it). "
           L"\"Draw the axes of the weapon in hand\" draws it too."),
    Toggle(L"debug", L"xbow_barrel_line", L"Crossbow: draw the barrel as modelled", 0,
           L"In VR F4 with a crossbow held the gun's way: a cyan line along the crossbow's barrel as the model has it (its bolt helper, "
           L"40 cm back and 30 cm ahead of the front of the stock) and a magenta ball where \"left palm under the drawn barrel\" puts the palm. "
           L"Cyan = as the renderer draws the crossbow; orange = the renderer's placing is not seen yet (the hand then stays on the shot line). "
           L"Every 3 s the log gives the points (\"crossbow barrel\"). "
           L"And where the shot goes, live: a ray from the muzzle - green along the drawn barrel (\"shoot along the drawn barrel\"), orange along "
           L"the shot line (unticked, or the barrel not read) - to the floor (a full ball) or 8 m out (a ring); the game aims by its heading. "
           L"With \"Draw the line the left hand takes\" the red shot line is drawn too: if the two lie together, the shot line sliders already "
           L"match the model. Needs the shader D2R_DepthFog.fx of the same build. Live."),
    Toggle(L"debug", L"bone_axes", L"Draw the axes of the weapon in hand", 0,
           L"The bone the game hangs the weapon in hand on: its own axes from its origin, 0.5 m each - X green, Y blue, Z yellow; and the red line the left hand takes it by. "
           L"Weapon Adjust's \"Turn about X / Y / Z\" turn the weapon about these."),
    Toggle(L"debug", L"bow_vectors", L"Draw the bow draw's vectors", 0,
           L"Native OpenXR, \"Bow: draw the string with both hands\" on. In the headset: the bow's axis (cyan), the touch sphere "
           L"round the string (green with the free hand inside, red outside, deeper once the string is taken), the hands (white = "
           L"bow, yellow = free), the fixed point 16 cm behind the grip (small cyan), the bow's own string (small magenta), the pull "
           L"line (orange) and the draw's aim (blue); and a line of numbers in the log five times a second. Needs the shader "
           L"D2R_DepthFog.fx of the same build. Live."),
    Toggle(L"debug", L"bow_latency", L"Log where the time goes between a release and its arrow / thrown missile", 0,
           L"Native OpenXR, \"Bow: draw the string with both hands\" or a throw gesture on. One line in the log for every bow shot and every throw: the milliseconds "
           L"after the release to the gesture's press, the pad's button reaching the game, the hero's attack or throw mode, the body's turn toward the aim, the "
           L"animation row the action runs on (name, frames, the action frame), the game's own missile being created (read only: the game's missile create is "
           L"watched, nothing is changed) and the client showing a missile. Nothing is drawn and nothing is changed in the game. Live."),
    Toggle(L"debug", L"melee_boxes", L"Draw the capsules of Hit by touch", 0,
           L"First person (F4) with \"Hit by touch\" on: the blade's capsule (cyan) and the capsule of every monster the touch looks at "
           L"(yellow; pale where the game's own checks refuse it - not hostile, or a wall between), drawn through everything, so they can be "
           L"seen against the monsters. The monster a touch was accepted on shows red for half a second when the game's own attack key "
           L"was pressed for it, magenta when \"Direct hit\" took it. Five times a second the log gets a line of numbers: the nearest "
           L"monsters' capsule size and height, how far the blade's tip is from them, the stroke speed. A drawing and a log only - "
           L"it changes nothing in what the touch does. Live. The nearest three monsters are always drawn, hostile or not (pale where "
           L"the game refuses them); with two melee weapons held (a Barbarian's, an Assassin's claws) both blades are drawn, the "
           L"second one teal. While a contact would count (a stroke on, the blade at or above \"Hit by touch: the least blade speed\") "
           L"the blade is green; a swing leaves marks: a WHITE ball where the blade's speed reached that least speed (and the start "
           L"distance was met), an ORANGE ball where it fell below it again, and the tip's path as a thin trail, cyan where a contact "
           L"would not count and green where it would - tune the least speed and the start distance by where they land in your swing. "
           L"A contact is judged by the strike rules (the Strikes tab): the blade, the trail and the marks are green only while a contact with the monster nearest the blade would count now. "
           L"At every contact that was REFUSED, for \"Hit by touch: how long the speed marks and the trail stay\": a BALL on the point of the blade that touched, an ARROW along the way that point "
           L"was moving, a thin LINE from it to the nearest point of the monster's body axis, and the monster's capsule tinted with the colour of the reason - RED: not toward the monster (a "
           L"wind-up, a pull-back, a graze; BLUE instead with \"Not toward the monster in red\" unticked - red is also the monster's flash when a blow is pressed); AMBER: the swing "
           L"was too small for this kind, or too slow where it touched, or the blade entered the monster in a movement that never became a stroke; GREY: not the head (the shaft or the handle of "
           L"a mace touched). A contact that counted leaves green marks and the monster's own red / magenta flash. The log says why, one line for each refused contact (the kind, the angle, the "
           L"speed or the amplitude), at most one every 250 ms for each reason; the first twelve are always told."),
    Slider(L"debug", L"melee_mark_ms", L"Hit by touch: how long the speed marks and the trail stay, ms (0 = none)", 0, 3000, 100, 800,
           L"With \"Draw the capsules of Hit by touch\" on: how long the white / orange balls (where the blade's speed reached the least "
           L"speed and where it fell below it) and the tip's trail stay in the picture after a swing. 0 = no marks and no trail, the "
           L"blade's colour only. Live."),
    Toggle(L"debug", L"melee_direction_red", L"Hit by touch: not toward the monster in red (off: blue)", 1,
           L"With \"Draw the capsules of Hit by touch\" on: the colour of a contact the direction rule refused (the blade was moving away from the monster or across it - a wind-up, a pull-back, "
           L"a graze). On (the default): RED. Off: BLUE - red is also the monster's flash when a blow is pressed, which a blue refusal cannot be taken for. The other reasons keep their "
           L"colours: amber - the swing too small or too slow, grey - not the head. A drawing only. Live."),
    Toggle(L"debug", L"skill_triggers", L"Draw what casts skills", 0,
           L"First person (F4): draws and logs the triggers that cast a skill, so they can be tuned by eye - for now the SHIELD BASH that "
           L"casts Smite (\"Shield bash casts Smite\"): the shield's capsule (the one the bash tests monsters with) on the left arm in violet, "
           L"green while a bash would count (a stroke on, at or above \"Shield bash: the least speed\", the right trigger up, Smite on a pad "
           L"slot...); a WHITE ball at the shield's centre where its speed reached that speed and an ORANGE one where it fell below it; its "
           L"path as a thin trail, cyan where a bash would not count and green where it would; the monsters near it (yellow) and the one a "
           L"bash pressed Smite on, red. Five times a second the log gets a \"skill trigger shield bash:\" line: the shield's speed against "
           L"the bash speed and its start distance, the right trigger, the pad button of Smite's slot, the mana against Smite's cost, the "
           L"nearest monster, and why a bash would not count (too slow, no stroke yet, right trigger down, no Smite slot, no mana, not hostile...). "
           L"It works with the shield bash off too (the line then says so). Other skill triggers (the bow's draw, a throw) can join it later. A drawing and a log only - it changes "
           L"nothing in when anything fires. Apart from \"Draw the capsules of Hit by touch\", which is for the melee blade. Needs the "
           L"D2R_DepthFog.fx of the same build (the old one draws the shield's capsule green). Live. F4 only."),
    Slider(L"debug", L"skill_mark_ms", L"Skill triggers: how long the speed marks and the trail stay, ms (0 = none)", 0, 3000, 100, 800,
           L"With \"Draw what casts skills\" on: how long the white / orange balls and the shield's trail stay in the picture after a bash. "
           L"0 = no marks and no trail, the capsule's colour only. Live."),
    Slider(L"debug", L"melee_blade_length_pct", L"Hit by touch: the blade's length from the weapon bone to its tip, % of the weapon's own", 50, 150, 5, 100,
           L"Tunes the blade of Hit by touch - the capsule drawn by \"Draw the capsules of Hit by touch\" is the blade the touch tests - to "
           L"the weapon as you see it: 100 = the length the plugin has for this weapon, from the weapon bone (for a staff, spear or two-handed "
           L"weapon the middle of the grip, not the end of the weapon) along the weapon to its tip. It only tunes where the touch counts a "
           L"hit; it never changes the game's reach, rolls or attack. First person (F4) only. Live."),
    Slider(L"debug", L"melee_blade_butt_pct", L"Hit by touch: the blade's length behind the weapon bone (the butt end), % of the weapon's own", 0, 150, 5, 100,
           L"The other side of the weapon bone: how far the blade runs behind the grip to the butt of the staff, spear or pole-arm. 100 = what "
           L"the plugin has for this weapon (a staff: as far behind as before), 0 = none, so that only the tip side counts. Same rules as the "
           L"length: only where the touch counts a hit, never the game's reach or rolls. First person (F4) only. Live."),
    Slider(L"debug", L"melee_blade_radius_cm", L"Hit by touch: the blade's thickness (radius), cm (0 = the weapon's own)", 0, 15, 0.5f, 0,
           L"0: the radius the plugin has for this weapon (a staff's is thin, an axe's wide). Else the blade's radius in cm, 0.5 to 15. "
           L"Only where the touch counts a hit - never the game's reach or rolls. First person (F4) only. Live."),
    Slider(L"debug", L"melee_thrown_length_pct", L"Hit by touch: a thrown weapon's blade, tip side (javelin, throwing knife and axe), %", 50, 250, 5, 100,
           L"The tip side of the blade of a JAVELIN, THROWING KNIFE or THROWING AXE only, on top of the general length above - those blades are "
           L"estimates (the javelin's head side, 1.04 m from the weapon bone along its -Y, was measured on two screenshots; the others were not "
           L"seen). With \"Draw the capsules of Hit by touch\" on, the capsule's far end - the HEAD side - should reach the weapon's real point, "
           L"the spearhead's; raise or lower this until it does. Only where the touch counts a hit - never the game's reach or rolls. "
           L"First person (F4) only. Live."),
    Slider(L"debug", L"melee_thrown_butt_pct", L"Hit by touch: a thrown weapon's blade, butt side (behind the weapon bone), %", 0, 300, 10, 100,
           L"The side of the blade behind the weapon bone - the BUTT end, behind the hand - for a javelin, throwing knife or throwing axe only "
           L"(the javelin's is 0.30 m, a guess; the bone is at the hand). Same rules as the head side. First person (F4) only. Live."),
    Toggle(L"debug", L"melee_blade_flip", L"Hit by touch: the blade runs the other way along the weapon", 0,
           L"On: the blade's tip lies on the other side of the weapon bone than the plugin assumes, for every weapon (the dagger's "
           L"already runs the other way by default). For a weapon whose capsule points away from its drawn blade. Only where the touch "
           L"counts a hit. First person (F4) only. Live."),
#endif
    Toggle(L"debug", L"shot_trace", L"Log what the game does with each attack of ours", 0,
           L"On: for every attack the plugin presses (a bow's shot, a blow, a throw), from its press until 1.5 s after it, the log tells every map click "
           L"(with the Stand Still and Run flags it carries), the command the game's click builds from it, and every command through the game's funnel "
           L"with its way back up the stack. To find out why the game sometimes runs the hero off after a shot. Read only. Needs \"attack in place\" on. Live."),
    Toggle(L"debug", L"frame_log", L"Log every frame", 0,
           L"Every pair, view, hero pose and present to d2r_vr_frames.csv beside the ini, and FlatVR its per-frame pose trace."),
    Toggle(L"debug", L"frame_trace", L"Log the long frames and what the plugin spent in them", 0,
           L"For a stutter that comes with a swing or a hit. A pair (both eyes of one game frame) longer than \"Frame trace: long over\" is told in the log "
           L"(d2r-vr-vrcam.log), the worst one of each second: where its time went (the game's own frame, the runtime's wait, the left and the right pass, "
           L"the draw thread's real work against its waiting), what the plugin itself spent in it (the pose of every skeleton, the hit observer, the aim "
           L"hooks, the server's lookups, the 100 ms probe of Hit by touch, the haptic call, the log lines), the situation it fell in (standing, walking, "
           L"a swing, a hit landed) and the access violations caught. Every 10 s one line counts how many pairs were long in each situation - send that "
           L"log in. It only measures and writes lines; it changes nothing in play. Costs a clock read or two in each hook while it is on. Live."),
    Slider(L"debug", L"frame_trace_ms", L"Frame trace: long over, ms", 5, 60, 1, 15,
           L"A pair longer than this is a long one for \"Log the long frames and what the plugin spent in them\". 15 ms is a little over one frame at "
           L"72 Hz (13.9 ms); at 90 Hz (11.1 ms) 15 catches every missed frame. Raise it if the game runs at 40-50 frames a second anyway "
           L"(a mean pair of 20 ms), so that only the real stutters are told. Live."),
    Toggle(L"debug", L"roomscale_log", L"Log the room-scale walk once a second", 0,
           L"With \"Room-scale walking\" on (Stereo > Native OpenXR): a line a second in the log while it is live - how far your head is off the "
           L"hero's place, the stick it gives the game, walking yes / no, blocked yes / no (at the limit: the room slides), your head's and the "
           L"hero's speed, whether the controller's stick is held and which pad the game reads. Changes nothing in play. Live."),
    Toggle(L"debug", L"log_async", L"Write the plugin's log from a thread of its own", 0,
           L"For a stutter that comes with a swing or a hit. Every press writes a handful of lines to the log (the pad poll alone: seven), and a second of "
           L"swinging 20 to 50, each written by the game's own thread in the middle of a frame - about a millisecond a line on the machine this was found "
           L"on, more where the log's file is slow (an antivirus, a disk that waits). On: the line is copied into a ring and a thread of its own writes it, "
           L"in order, so the game's thread does not wait. Every line is kept; a line that waited more than 50 ms says so at its end (the log stamps the "
           L"time it is WRITTEN); what waits is written at a crash and at unload. It changes nothing in play. Off: each thread writes its own lines, as "
           L"always. Live."),
    Toggle(L"debug", L"dred", L"Say why the GPU's device was lost (DRED)", 1,
           L"The game ends - after a failed check in its own DX12 code, a moment before the headset's runtime logs \"HRESULT failure [887a0005]\" - when the "
           L"graphics device is removed (a GPU hang, a page fault, a driver reset). On: Windows' DRED is asked for before the game makes its device, and the "
           L"moment the device goes the log (d2r-vr-vrcam.log) and d2rloader\\logs\\d2r_vr_gpu_fault.txt say why: the reason, what each command list had "
           L"done when the GPU stopped, the address of a page fault and what was allocated or freed there, and the plugin's own last events that touch the "
           L"GPU's memory. Send that file in. It costs the runtime a marker between commands. Needs a game restart. Off: the device loss is still told, "
           L"with its reason only."),
    Toggle(L"debug", L"d3d12_validation", L"D3D12 debug layer: errors in the log (slow)", 0,
           L"To find what takes the graphics device down. Needs Windows' optional feature Graphics Tools. On: before the game makes its device the D3D12 debug layer is switched "
           L"on, and its errors and warnings go to the log (d2r-vr-vrcam.log) in its own words - a barrier that names the wrong state, a copy past the end of a buffer, a "
           L"resource released while a list that names it has not run. The game runs much slower: for a test, not for play. Needs a game restart. (d2r_vr.ini: 2 also "
           L"checks every shader access on the GPU, ten times slower again - for a flat repro only.)"),
    Toggle(L"debug", L"stall_guard", L"Free a GPU queue that stands still for good", 1,
           L"Two hangs left the game's graphics queue standing for good - no device removal, the game alive and frozen, its drawing thread inside ReShade's "
           L"flush. On: while the queue has not moved for a quarter of a second the plugin's optional work that would wait on it (the copy to the headset, an "
           L"eye's ReShade effects, a depth copy) is not started; after eight seconds with the picture stopped, the plugin signals its own cross-queue fences from "
           L"the CPU, and says each in the log (\"stall guard\"). A queue that runs a command list which never ends is not freed by this: see the next switch. "
           L"Needs a game restart. Off: as before. (d2r_vr.ini: stall_skip_ms, stall_unstick_ms; stall_unstick_game also signals the game's own fences.)"),
    Toggle(L"debug", L"stall_remove", L"Hang report: take the device away after 20 s of a hung queue", 0,
           L"For the graphics queue that stands for good (see \"Free a GPU queue...\"; needs that on). Switched on: after 20 s with the picture stopped and the queue "
           L"still, the plugin takes the graphics device away on purpose, so that Windows' DRED (\"Say why the GPU's device was lost\" - on) writes into "
           L"d2rloader\\logs\\d2r_vr_gpu_fault.txt which command list the queue was in. The game ends, as after a driver reset - for a test of the hang, not for play. "
           L"Needs a game restart."),
    Toggle(L"debug", L"matrix_writer", L"Find who writes the hero matrix", 0,
           L"Switched on: for 3 s, while walking, marks where the game writes the hero's model matrix "
           L"(d2r_vr_matrix_writers.csv beside the ini, a summary in the log). Turn it off and on again for another run."),
#if D2RVR_FIRST_PERSON
    // the rays of where an aim goes - the bow's, the crossbow's and the spell's, drawn alike (green from the model, orange the fallback)
    Group(L"Aim rays"),
    Toggle(L"debug", L"spell_aim_ray", L"Spell's ray", 0,
           L"VR first person (F4), no bow, crossbow or thrown weapon in hand: where a spell goes, live - a ray from where its aim starts (the staff, wand or "
           L"orb, or the palm), along the direction the aim uses, to the floor (a full ball) or 8 m out (a ring). Green: from the model (Auto-target: "
           L"\"Aim spells by the drawn hand or staff\") - it should lie along the staff, wand or orb, or along the hand's fingers. Orange: the old line "
           L"(the controller and the aim correction sliders) - unticked, or no fresh pose. The game aims by its heading on the ground. Needs the shader "
           L"D2R_DepthFog.fx of the same build. Live."),
    Toggle(L"debug", L"bow_aim_ray", L"Bow: draw where the arrow goes (a ray from the bow)", 0,
           L"Native OpenXR, \"Bow: draw the string with both hands\" on, a bow in hand (first person, F4). While the string is drawn - and for a moment after the shot - a long ray in the "
           L"headset: from the arrow's release point (the bow model's own arrow rest point when it is read, else the bow hand's point) along the line the bow code shoots along (the string "
           L"hand through that point; the game itself takes only its heading on the ground), to where it meets the floor (a full ball) or 8 m out (a ring). Green = the line goes "
           L"through the bow model's arrow rest point (\"The shot goes through the arrow's rest point too\" on); orange = the plain hand-set line (no rest point read, or that setting off). "
           L"Nothing is changed in the game. Needs the shader D2R_DepthFog.fx of the same build. Live."),
#endif
};

wchar_t g_ini[MAX_PATH], g_uiIni[MAX_PATH];

// The Weapon Adjust tab edits [weapon_<kind>] of one kind at a time ("@weapon"
// items); "@ui" items live in d2r_vr_settings.ini, not in the game's ini.
int g_weaponType = D2RVR_TYPE_BOW;
std::wstring g_weaponSec = L"weapon_bow";

void SetWeaponSection(int type) {
    if (type <= D2RVR_TYPE_UNKNOWN || type >= D2RVR_TYPE_COUNT) return;
    g_weaponType = type;
    g_weaponSec = L"weapon_";
    for (const char* c = kD2RVRWeaponTypeNames[type]; *c; ++c) g_weaponSec += (wchar_t)tolower((unsigned char)*c);
}

const wchar_t* SectionOf(const struct Item& it);
const wchar_t* FileOf(const struct Item& it);
HWND g_main = nullptr, g_tabs = nullptr, g_page = nullptr, g_footer = nullptr, g_tip = nullptr;
HFONT g_font = nullptr, g_bold = nullptr, g_big = nullptr;
UINT g_dpi = 96;
float g_zoom = 1.0f, g_scale = 1.0f;   // g_scale = DPI / 96 x zoom
int g_tab = 0, g_tabCount = 0;   // g_tab: the tab shown, by its place in g_items (Item::tab), not in the strip
// The tab strip holds only the tabs of the current platform; this is which tab each of its items is.
std::vector<int> g_tabIds;
int g_platform = 1;   // [mode] platform: 0 flat, 1 VR
int PlatformBit() { return g_platform == 1 ? kVr : kFlat; }
bool ShownNow(const struct Item& it);
int g_contentH = 0, g_viewH = 0, g_scrollY = 0;
constexpr int kFirstId = 100, kBrowseId = 2000, kClearId = 3000, kValueId = 4000, kRadioId = 5000;   // radios: kRadioId + item * 8 + value
constexpr int kRowH = 30;   // at 96 DPI, zoom 1

int S(float v) { return (int)std::lround(v * g_scale); }

// "@game" items are not ours: they are keys in the game's own Settings.json
// (Saved Games\Diablo II Resurrected). "Safe Screen Percent" is the console
// safe area the PC menus do not show; below 100 the game lays its whole UI out
// in a smaller box in the middle of the screen. Read when the game starts, so
// the change shows on the next start, and the game must be closed when it is
// written or it puts its own value back on exit.
std::wstring GameSettingsPath() {
    // (for tests: a Settings.json of their own, so a test never touches the player's file)
    wchar_t over[MAX_PATH];
    if (const DWORD n = GetEnvironmentVariableW(L"D2RVR_GAME_SETTINGS_JSON", over, MAX_PATH); n > 0 && n < MAX_PATH) return over;
    PWSTR dir = nullptr;
    std::wstring p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) p = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    return p;
}

bool ReadFileText(const std::wstring& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); *out = ss.str();
    return true;
}

std::string KeyPattern(const wchar_t* key) {
    std::string k;
    for (const wchar_t* c = key; *c; ++c) k += (char)*c;
    return "(\"" + k + "\"\\s*:\\s*)(-?[0-9.]+)";
}

bool ReadGameValue(const Item& it, float* v) {
    std::string text;
    if (!ReadFileText(GameSettingsPath(), &text)) return false;
    std::smatch m;
    if (!std::regex_search(text, m, std::regex(KeyPattern(it.key)))) return false;
    *v = std::stof(m[2].str());
    return true;
}

// The game's windowed size in its Settings.json: "Window Mode" (0 = a window) and
// "Screen Resolution (Windowed)" "WxH" (D2R VR Settings > Stereo > Square game window).
// (the file's two keys are read and spliced by shared/window_json.h, which vrcam uses at its start and which has the tests)
bool ReadGameWindow(int* mode, int* w, int* h) {
    std::string text;
    if (!ReadFileText(GameSettingsPath(), &text)) return false;
    const winjson::Windowed r = winjson::Read(text);
    *mode = r.mode;
    if (!r.ok) return false;
    *w = r.w;
    *h = r.h;
    return true;
}
// The windowed size, and "Window Mode" set to `mode` (0 = a window: the square wants one; -1 = left as it is).
// A read-only Settings.json (players were told to make it so for the window size) has the flag taken off first.
bool WriteGameWindow(int w, int h, int mode = 0) {
    const std::wstring path = GameSettingsPath();
    std::string text;
    if (!ReadFileText(path, &text)) return false;
    bool ok = false;
    const std::string out = winjson::With(text, w, h, mode, &ok);
    if (!ok) return false;
    if (out == text) return true;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
    return (bool)f;
}

void WriteGameValue(const Item& it, float v) {
    const std::wstring path = GameSettingsPath();
    std::string text;
    if (!ReadFileText(path, &text)) return;
    // Spliced by hand, not regex_replace: "$1" followed by the number made "$188",
    // read as group 18, which ate the key and the first digit - the game's
    // Settings.json was left as a bare "8," and stopped being JSON.
    std::smatch m;
    if (!std::regex_search(text, m, std::regex(KeyPattern(it.key)))) return;
    const std::string out = m.prefix().str() + m[1].str() + std::to_string((int)std::lround(v)) + m.suffix().str();
    if (out == text) return;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
}

// "@bodywalk" items: keys in BodyWalk's config.json, in its settings folder
// (BodyWalkDir; beside the exe before 1.74), while a BodyWalk runs - it reads
// the file again within a second of a change. The value is mirrored into the
// ini ([stereo] user_height_m for userHeight), which vrcam falls back to - and
// which is read here when BodyWalk is not running.
std::wstring BodyWalkDir();
std::wstring BodyWalkConfigPath() {
    std::wstring path;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return path;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && path.empty(); ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"BodyWalkVR.exe") != 0) continue;
        if (!BodyWalkDir().empty()) path = BodyWalkDir() + L"\\config.json";
    }
    CloseHandle(snap);
    return path;
}

const wchar_t* IniMirrorOf(const Item& it) { return wcscmp(it.key, L"userHeight") == 0 ? L"user_height_m" : nullptr; }

float ReadBodyWalkValue(const Item& it) {
    std::string text;
    const std::wstring path = BodyWalkConfigPath();
    std::smatch m;
    if (!path.empty() && ReadFileText(path, &text) && std::regex_search(text, m, std::regex(KeyPattern(it.key))))
        return std::stof(m[2].str());
    wchar_t buf[64];
    if (const wchar_t* mirror = IniMirrorOf(it)) {
        GetPrivateProfileStringW(L"stereo", mirror, L"", buf, 64, g_ini);
        wchar_t* end = nullptr;
        const float v = wcstof(buf, &end);
        if (end != buf && std::isfinite(v)) return v;
    }
    return it.def;
}

void WriteBodyWalkValue(const Item& it, float v) {
    wchar_t num[32];
    swprintf_s(num, L"%.2f", v);
    if (const wchar_t* mirror = IniMirrorOf(it)) WritePrivateProfileStringW(L"stereo", mirror, num, g_ini);
    const std::wstring path = BodyWalkConfigPath();
    std::string text;
    if (path.empty() || !ReadFileText(path, &text)) return;
    std::smatch m;
    if (!std::regex_search(text, m, std::regex(KeyPattern(it.key)))) return;
    char n8[32];
    snprintf(n8, sizeof n8, "%.2f", v);
    const std::string out = m.prefix().str() + m[1].str() + n8 + m.suffix().str();
    if (out == text) return;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
}

std::wstring ReadPath(const Item& it) {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(it.section, it.key, it.kind == Kind::File && it.sdef ? it.sdef : L"", buf, MAX_PATH, g_ini);
    return buf;
}

// reshade-shaders\Textures of D2R VR's own ReShade files (ReShadeFiles: the mod's reshade\, or the game's folder before the mod layout).
std::wstring ReShadeFiles();
std::wstring ReShadeFolder();
std::wstring TexturesDir() {
    return ReShadeFiles() + L"reshade-shaders\\Textures";
}

// A picture under the Textures folder is kept relative to it, anything else as a full path.
void ChoosePicture(HWND wnd, Item& it) {
    wchar_t file[MAX_PATH] = {};
    const std::wstring dir = TexturesDir();
    OPENFILENAMEW of{sizeof of};
    of.hwndOwner = wnd;
    of.lpstrFilter = L"Pictures (png, jpg, bmp, dds, tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.dds;*.tga\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrInitialDir = dir.c_str();
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return;
    std::wstring path = file;
    // under Textures (its subfolders too: D2R_Sky_ours): kept relative to it
    if (!dir.empty() && _wcsnicmp(path.c_str(), (dir + L"\\").c_str(), dir.size() + 1) == 0)
        path = path.substr(dir.size() + 1);
    WritePrivateProfileStringW(it.section, it.key, path.c_str(), g_ini);
    SetWindowTextW(it.ctl, path.c_str());
}

// A colour row: the field shows #RRGGBB on that very colour, or what empty means.
bool ParseColor(const std::wstring& s, COLORREF* c) {
    const wchar_t* p = s.c_str();
    while (*p == L' ' || *p == L'#') ++p;
    unsigned rgb = 0;
    if (wcslen(p) < 6 || swscanf_s(p, L"%6x", &rgb) != 1) return false;
    *c = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

void ShowColor(Item& it) {
    if (it.brush) { DeleteObject(it.brush); it.brush = nullptr; }
    COLORREF c;
    const std::wstring v = ReadPath(it);
    if (ParseColor(v, &c)) {
        it.brush = CreateSolidBrush(c);
        wchar_t buf[16]; swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
        SetWindowTextW(it.ctl, buf);
    } else SetWindowTextW(it.ctl, it.sdef ? it.sdef : L"");
    InvalidateRect(it.ctl, nullptr, TRUE);
}

void ChooseFogColor(HWND wnd, Item& it) {
    static COLORREF custom[16] = {};
    CHOOSECOLORW cc{sizeof cc};
    cc.hwndOwner = wnd;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ParseColor(ReadPath(it), &cc.rgbResult)) cc.rgbResult = RGB(5, 5, 6);
    if (!ChooseColorW(&cc)) return;
    wchar_t buf[16];
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    WritePrivateProfileStringW(it.section, it.key, buf, g_ini);
    ShowColor(it);
}

const wchar_t* SectionOf(const Item& it) {
    if (wcscmp(it.section, L"@weapon") == 0) return g_weaponSec.c_str();
    if (wcscmp(it.section, L"@ui") == 0) return L"weapon";
    return it.section;
}
const wchar_t* FileOf(const Item& it) { return wcscmp(it.section, L"@ui") == 0 ? g_uiIni : g_ini; }

// The Performance tab's presets (2026-10-08): Potato (Low with the 3D from the depth), Low, Medium (the shipped defaults), High.
// Not kept anywhere: the drop-down shows whichever preset the keys match, else Custom.
constexpr int kPresets = 4;   // Potato, Low, Medium, High; then Custom
struct PresetKey { const wchar_t* section; const wchar_t* key; float v[kPresets]; };
const PresetKey kPreset[] = {
    {L"render", L"rings", {1, 1, 2, 3}},
    {L"render", L"model_radius", {150, 150, 200, 400}},   // Medium 200: 300 cost ~2 ms CPU a pair (2026-10-10)
    {L"fog", L"enabled", {1, 1, 1, 1}},
    {L"fog", L"start", {30, 30, 30, 30}},
    {L"fog", L"end", {100, 100, 200, 250}},
    {L"fog", L"caves", {1, 1, 1, 1}},
    {L"fog", L"caves_start", {60, 60, 100, 100}},
    {L"fog", L"caves_end", {100, 100, 200, 200}},
    {L"sky", L"enabled", {1, 1, 1, 1}},
    {L"@reshade", L"generic_depth", {1, 1, 1, 1}},
    {L"ceiling", L"enabled", {0, 1, 1, 1}},   // Low too (8 relief steps): players on Low had no ceilings at all
    {L"ceiling", L"steps", {8, 8, 12, 20}},
    {L"ceiling", L"torches", {0, 0, 0, 1}},
    {L"@stereo3d", L"source", {2, 1, 1, 1}},   // Potato: FlatVR 3D depth (ReShade's depth, one frame a picture); the others FlatVR stereo (as before)
    {L"stereo", L"headset_rate", {0, 3, 3, 3}},   // Potato 72 Hz, the others 90 (the game's cap follows: ApplyFrameCap)
};
float ReadValue(const Item& it);
const Item* PresetItem(const wchar_t* section, const wchar_t* key) {
    for (const Item& it : g_items)
        if (it.key && it.section && wcscmp(it.section, section) == 0 && wcscmp(it.key, key) == 0) return &it;
    return nullptr;
}
int MatchPreset() {
    for (int p = 0; p < kPresets; ++p) {
        bool all = true;
        for (const PresetKey& k : kPreset) {
            const Item* it = PresetItem(k.section, k.key);
            if (it && fabsf(ReadValue(*it) - k.v[p]) > 0.001f) { all = false; break; }
        }
        if (all) return p;
    }
    return kPresets;
}

std::wstring GameFolder();
// D2R VR installed as the D2RLoader mod D2R-VR: its d2r_vr.ini is <game>\mods\D2R-VR\d2rloader\plugins\d2r_vr.ini (g_ini).
bool ModInstall() {
    const std::wstring plugins = modlayout::ModPlugins(GameFolder());
    return _wcsnicmp(g_ini, plugins.c_str(), plugins.size()) == 0;
}
// The folder ReShade and its files are in: the mod's reshade\ when the mod has its own ReShade64.dll, else the game's folder
// (as vrcam's ReShadeDir decides, from the plugin's side).
std::wstring ReShadeFolder() {
    const std::wstring game = GameFolder();
    const std::wstring mod = ModInstall() ? modlayout::ModRoot(game) : std::wstring();
    const bool modReShade = !mod.empty() && GetFileAttributesW((mod + modlayout::kReShadeFolder + L"ReShade64.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    return modlayout::ReShadeDir(game, mod, modReShade);
}
// Where D2R VR's OWN ReShade files are - D2R_DepthFog.fx, the sky / ceiling / Arcane pictures, FlatVR's add-on (the plugin's OurFilesDir): the mod's reshade\
// whenever D2R VR is the mod - whether ReShade is its own ReShade64.dll there (ReShadeFolder) or a dxgi.dll of the player's beside the game, which finds
// them through two search paths in its ReShade.ini (shared/reshade_paths.h) - else the game's folder.
std::wstring ReShadeFiles() {
    const std::wstring game = GameFolder();
    return modlayout::OurFilesDir(game, ModInstall() ? modlayout::ModRoot(game) : std::wstring());
}
int ReShadePathsCheck();   // (below) are D2R VR's two folders in the search paths of the player's dxgi.dll ReShade? 0 yes, 1 no, 2 no dxgi.dll / not the mod
// Where ReShade's files are for the Home buttons (shared/reshade_as.h): the game's folder, and the mod's reshade\ when D2R VR is the mod.
reshadeas::Where ReShadeWhere() {
    const std::wstring game = GameFolder();
    return {game, ModInstall() ? modlayout::ModReShade(game) : std::wstring()};
}
// "@reshade" generic_depth: ReShade's Generic Depth add-on, on unless its name is in
// [ADDON] DisabledAddons of the ReShade.ini in ReShade's folder (read when the game starts).
constexpr const wchar_t* kGenericDepth = L"Generic Depth";
std::vector<std::wstring> DisabledAddons() {
    wchar_t buf[2048] = {};
    GetPrivateProfileStringW(L"ADDON", L"DisabledAddons", L"", buf, 2048, (ReShadeFolder() + L"ReShade.ini").c_str());
    std::vector<std::wstring> out;
    std::wstring cur;
    for (const wchar_t* c = buf;; ++c) {
        if (*c == L',' || !*c) {
            while (!cur.empty() && cur.back() == L' ') cur.pop_back();
            size_t b = 0;
            while (b < cur.size() && cur[b] == L' ') ++b;
            if (b < cur.size()) out.push_back(cur.substr(b));
            cur.clear();
            if (!*c) break;
        } else cur += *c;
    }
    return out;
}
float ReadReShadeValue(const Item&) {
    for (const std::wstring& a : DisabledAddons()) if (_wcsicmp(a.c_str(), kGenericDepth) == 0) return 0.0f;
    return 1.0f;
}
void WriteReShadeValue(const Item&, float v) {
    const std::wstring ini = ReShadeFolder() + L"ReShade.ini";
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return;   // no ReShade: nothing to switch
    std::wstring list;
    for (const std::wstring& a : DisabledAddons()) {
        if (_wcsicmp(a.c_str(), kGenericDepth) == 0) continue;
        list += (list.empty() ? L"" : L",") + a;
    }
    if (v == 0.0f) list += (list.empty() ? L"" : L",") + std::wstring(kGenericDepth);
    WritePrivateProfileStringW(L"ADDON", L"DisabledAddons", list.c_str(), ini.c_str());
}

// "@stereo3d" source, the six modes (5: native OpenXR with the eyes by turns - [openxr] on with
// afr alone, 2026-10-11): 0 stereo (one pass, two pictures - [stereo] afr,
// pair_per_tick and replay_right), 1 stereo by turns (afr alone), 2 3D from ReShade's depth,
// 3 mono, 4 native OpenXR (mode 0's keys and [openxr] on: the game in the headset itself,
// vr/xr.cpp) - kept as [stereo] source (0 none, 1 depth, 2 the game's pair) and those keys: the
// Stereo tab switches afr and pair_per_tick alone (two passes a frame: pair_per_tick without
// replay_right, reads as stereo here).
BW_LinkRequests* LinkRequests();
bool LinkHost(uint32_t* caps = nullptr);
// The list shows three of the six modes (index = the list's item): 0 native OpenXR (modes 4 and 5), 1 FlatVR stereo (modes 0 and 1;
// mono, which the list no longer offers, is shown here too), 2 FlatVR 3D depth (mode 2). WriteStereo3D takes the list's index.
float ReadStereo3D() {
    if (GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0) return 0.0f;
    const bool afr = GetPrivateProfileIntW(L"stereo", L"afr", 0, g_ini) != 0;
    const int source = GetPrivateProfileIntW(L"stereo", L"source", afr ? 2 : 1, g_ini);
    if (afr) return 1.0f;
    return source == 0 ? 1.0f : 2.0f;
}
int ExeVersion(const std::wstring& exe);
std::wstring BodyWalkExe();
void SetStatus(const wchar_t* key, int state, const wchar_t* text);
// Native OpenXR's buttons and gestures go through BodyWalk (host API 9): its Game Link says
// so while it runs, or the BodyWalk found is 1.78 or later.
bool BodyWalkTakesGameHeadset() {
    if (uint32_t caps = 0; LinkHost(&caps) && (caps & BW_LINK_CAP_XR_INPUT)) return true;
    const std::wstring exe = BodyWalkExe();
    return !exe.empty() && ExeVersion(exe) >= 1078;
}
// False: not written (native OpenXR without a BodyWalk for it).
void ApplyWindowForPlatform();   // (the game's window: VR has the square, flat mode the player's own - below)
bool WriteStereo3D(float item) {
    static const float kModeOfItem[3] = {4.0f, 0.0f, 2.0f};   // the list's item -> the mode below (native, FlatVR stereo, FlatVR 3D depth)
    const float v = kModeOfItem[std::clamp((int)std::lround(item), 0, 2)];
    const bool native = std::lround(v) == 4 || std::lround(v) == 5;   // the one-pass stereo, or the eyes by turns (5)
    const bool wasNative = GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0;
    // (with "Controls without BodyWalk" the controls need no BodyWalk at all)
    if (native && !wasNative && !BodyWalkTakesGameHeadset() && GetPrivateProfileIntW(L"openxr", L"direct_input", 0, g_ini) == 0) {
        SetStatus(L"native_xr", 1, L"Native OpenXR not set: this BodyWalk cannot take the game's headset (buttons and gestures). "
                                   L"Start BodyWalk if it is 1.78 or later, or update it");
        return false;
    }
    if (native != wasNative)
        SetStatus(L"native_xr", native ? 0 : 3, native ? L"Native OpenXR on: BodyWalk's Mapping takes the game's headset (Game Link)"
                                                     : L"Native OpenXR off: the picture goes through FlatVR");
    WritePrivateProfileStringW(L"openxr", L"on", native ? L"1" : L"0", g_ini);
    // BodyWalk's Mapping Input Source with it: the game's headset, or FlatVR's again
    if (native != wasNative)
        if (BW_LinkRequests* r = LinkRequests())
            bw::link::Request(r, &r->presenter, native ? BW_LINK_PRESENTER_GAME : BW_LINK_PRESENTER_FLATVR);
    const int m = native ? (std::lround(v) == 5 ? 1 : 0) : std::clamp((int)std::lround(v), 0, 3);
    static const int kSource[4] = {2, 2, 1, 0};
    const int s = kSource[m];
    wchar_t buf[8];
    swprintf_s(buf, L"%d", s);
    WritePrivateProfileStringW(L"stereo", L"source", buf, g_ini);
    WritePrivateProfileStringW(L"stereo", L"afr", s == 2 ? L"1" : L"0", g_ini);
    if (m <= 1) {
        WritePrivateProfileStringW(L"stereo", L"pair_per_tick", m == 0 ? L"1" : L"0", g_ini);
        WritePrivateProfileStringW(L"stereo", L"replay_right", m == 0 ? L"1" : L"0", g_ini);
        if (m == 0) WritePrivateProfileStringW(L"stereo", L"replay_ahead", L"2", g_ini);
    }
    // BodyWalk's FlatVR follows, through Game Link (s is BW_LINK_STEREO_*; done only while BodyWalk runs)
    if (BW_LinkRequests* r = LinkRequests()) bw::link::Request(r, &r->stereoSource, s);
    return true;
}

// "Square game window": on while the game's window is a square of any size. On: the game's
// Settings.json gets a window of [openxr] window_size both ways (the size it had kept in
// [openxr] window_before), and ReShade's depth heuristic is kept from "multiples of
// resolution" (UseAspectRatioHeuristics=2 takes the 4096 x 4096 shadow map for the square
// scene's depth, 2026-10-11). Off: the size kept before.
bool ProcessRunning(const wchar_t* exe);
float ReadSquareWindow() {
    return GetPrivateProfileIntW(L"openxr", L"square_window", 0, g_ini) != 0 ? 1.0f : 0.0f;
}
void WriteSquareWindow(bool on) {
    if (g_platform != 1) {   // flat mode has no square window: the box is only remembered, the game's window stays the player's own
        WritePrivateProfileStringW(L"openxr", L"square_window", on ? L"1" : L"0", g_ini);
        ApplyWindowForPlatform();
        return;
    }
    int mode = -1, w = 0, h = 0;
    if (!ReadGameWindow(&mode, &w, &h)) {
        // Kept all the same: vrcam makes the window square in a game from our ini (BigWindowTick). It was
        // dropped here, and the box unticked itself at the next look - "the setting disappears" (2026-10-12).
        WritePrivateProfileStringW(L"openxr", L"square_window", on ? L"1" : L"0", g_ini);
        const std::wstring p = GameSettingsPath();
        const bool there = !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
        SetStatus(L"square_window", 1, !on ? L"Off" : there
            ? L"Kept. The game's Settings.json has no windowed size in it: in the game set Options > Video > Window Mode to Windowed, then quit the game once"
            : L"Kept. The game's Settings.json is not there yet (Saved Games\\Diablo II Resurrected): the game writes it when it quits - vrcam makes the window square in a game anyway");
        return;
    }
    const bool running = ProcessRunning(L"D2RLoader.exe") || ProcessRunning(L"D2R.exe");
    int side = 0;
    if (on) {
        if (w != h) {   // the size it had, and its mode, for "off" and for flat mode (ApplyWindowForPlatform)
            wchar_t b[32];
            swprintf_s(b, L"%dx%d", w, h);
            WritePrivateProfileStringW(L"openxr", L"window_before", b, g_ini);
            swprintf_s(b, L"%d", mode);
            WritePrivateProfileStringW(L"openxr", L"window_mode_before", b, g_ini);
        }
        side = std::clamp((int)GetPrivateProfileIntW(L"openxr", L"window_size", 2000, g_ini), 640, 4000);
        side -= side % 8;
        if (!WriteGameWindow(side, side)) { SetStatus(L"square_window", 1, L"The game's Settings.json could not be written"); return; }
        const std::wstring rs = ReShadeFolder() + L"ReShade.ini";
        if (GetPrivateProfileIntW(L"DEPTH", L"UseAspectRatioHeuristics", 1, rs.c_str()) == 2)
            WritePrivateProfileStringW(L"DEPTH", L"UseAspectRatioHeuristics", L"1", rs.c_str());
    } else {
        wchar_t b[32] = L"";
        GetPrivateProfileStringW(L"openxr", L"window_before", L"", b, 32, g_ini);
        int bw = 0, bh = 0;
        if (swscanf_s(b, L"%dx%d", &bw, &bh) != 2 || bw < 320 || bh < 200 || bw == bh) { bw = 1920; bh = 1080; }
        const int modeBefore = (int)GetPrivateProfileIntW(L"openxr", L"window_mode_before", -1, g_ini);
        if (!WriteGameWindow(bw, bh, modeBefore >= 0 ? modeBefore : 0)) { SetStatus(L"square_window", 1, L"The game's Settings.json could not be written"); return; }
    }
    WritePrivateProfileStringW(L"openxr", L"square_window", on ? L"1" : L"0", g_ini);   // vrcam keeps the window to it
    wchar_t msg[256];
    if (on) swprintf_s(msg, L"The game window: %d x %d%s", side, side, running ? L" - set now by vrcam in native OpenXR" : L" from the next start");
    else swprintf_s(msg, L"The game window back to its own size%s", running ? L" from the next start (close the game first: it writes its size back when it quits)" : L" from the next start");
    SetStatus(L"square_window", running ? 2 : 0, msg);
}

// The game's window for the platform now chosen (Home > How you play): at once on a switch of it, and on START (LaunchGame). The plugin
// does the same at its own start (vrcam's ReconcileGameWindow) - that also covers a game started some other way, and the size the game
// writes back at its quit. VR (native OpenXR and FlatVR alike): with the box ticked, the square for the next start. FLAT has no square
// window and uses the window the PLAYER set in the game: nothing is chosen or written for it but the player's own window put back
// where VR's square was (kept in [openxr] window_before and window_mode_before before the square was written; none kept: nothing is
// written).
void ApplyWindowForPlatform() {
    if (g_platform == 1) {
        if (ReadSquareWindow() != 0.0f) WriteSquareWindow(true);
        return;
    }
    int mode = -1, w = 0, h = 0;
    if (!ReadGameWindow(&mode, &w, &h)) return;
    winjson::Windowed now;
    now.ok = true;
    now.mode = mode;
    now.w = w;
    now.h = h;
    winjson::Own own;
    {
        wchar_t b[32] = L"";
        GetPrivateProfileStringW(L"openxr", L"window_before", L"", b, 32, g_ini);
        swscanf_s(b, L"%dx%d", &own.w, &own.h);
        own.mode = (int)GetPrivateProfileIntW(L"openxr", L"window_mode_before", -1, g_ini);
    }
    const winjson::Plan plan = winjson::ForPlatform(false, ReadSquareWindow() != 0.0f, now, own);
    if (plan.saveOwn && (plan.own.w != own.w || plan.own.h != own.h || plan.own.mode != own.mode)) {   // the player's own window, as it is now
        wchar_t b[32];
        swprintf_s(b, L"%dx%d", plan.own.w, plan.own.h);
        WritePrivateProfileStringW(L"openxr", L"window_before", b, g_ini);
        swprintf_s(b, L"%d", plan.own.mode);
        WritePrivateProfileStringW(L"openxr", L"window_mode_before", b, g_ini);
    }
    if (plan.unknownOwn) {
        SetStatus(L"square_window", 2, L"Flat mode: the game's window is a square (VR's) and no window of yours was kept: the game uses it - set the window you want in its Options > Video");
        return;
    }
    if (!plan.write) return;
    if (!WriteGameWindow(plan.w, plan.h, plan.mode)) { SetStatus(L"square_window", 1, L"The game's Settings.json could not be written"); return; }
    const bool running = ProcessRunning(L"D2RLoader.exe") || ProcessRunning(L"D2R.exe");
    wchar_t msg[256];
    swprintf_s(msg, L"Flat mode: the game window back to your own, %d x %d%s", plan.w, plan.h,
               running ? L" from the next start (the game writes its window back when it quits - the plugin sets it right at the next start)" : L" from the next start");
    SetStatus(L"square_window", running ? 2 : 0, msg);
}

// The VR render size: the slider (@render vr_px) and its tick box (@render vr_headset) over [render] vr_resolution_px and vr_resolution.
float ReadRenderSize(const Item& it) {
    const int mode = (int)GetPrivateProfileIntW(L"render", L"vr_resolution", 0, g_ini);
    const int px = (int)GetPrivateProfileIntW(L"render", L"vr_resolution_px", 0, g_ini);
    if (wcscmp(it.key, L"vr_headset") == 0) return mode == 1 ? 1.0f : 0.0f;
    // (while the box holds, the slider shows what unticking it brings back)
    return (float)rendersize::StopOf(rendersize::FromIni(mode == 1 ? 0 : mode, px));
}
void UpdateResolutionTexts();
void UpdateSessionNote();
void WriteRenderSize(const Item& it, float v) {
    const int mode = (int)GetPrivateProfileIntW(L"render", L"vr_resolution", 0, g_ini);
    wchar_t buf[16];
    if (wcscmp(it.key, L"vr_headset") == 0) {
        swprintf_s(buf, L"%d", v != 0.0f ? 1 : (mode == 1 ? 0 : mode));   // unticked: the slider's side, or Desktop
        WritePrivateProfileStringW(L"render", L"vr_resolution", buf, g_ini);
    } else {
        swprintf_s(buf, L"%d", rendersize::PxOfStop((int)std::lround(v)));   // the left end: 0, not used
        WritePrivateProfileStringW(L"render", L"vr_resolution_px", buf, g_ini);
        if (const int m = rendersize::ModeAfterSlider(mode); m != mode) {   // an older square mode is the slider's now
            swprintf_s(buf, L"%d", m);
            WritePrivateProfileStringW(L"render", L"vr_resolution", buf, g_ini);
        }
    }
    UpdateResolutionTexts();
}

// "@turn" items: [turn] smooth and snap, read and written by the plugin's rule (shared/turn_mode.h). An ini without `smooth` says
// snap=0 for smooth turning, so the toggle shows what the plugin does and the angle shows 30 for that 0; a new angle is written with
// the mode it is chosen in (the explicit `smooth`), so it never flips an older profile to snap turning.
float TurnKey(const wchar_t* key, float missing) {
    wchar_t b[64];
    GetPrivateProfileStringW(L"turn", key, L"", b, 64, g_ini);
    if (!b[0]) return missing;
    wchar_t* end = nullptr;
    const float v = wcstof(b, &end);
    return end != b && std::isfinite(v) ? v : missing;
}
float ReadTurnValue(const Item& it) {
    const float snap = TurnKey(L"snap", 0.0f), smooth = TurnKey(L"smooth", -1.0f);
    return wcscmp(it.key, L"smooth") == 0 ? (turnmode::Smooth(snap, smooth) ? 1.0f : 0.0f) : turnmode::SnapDegrees(snap);
}
void WriteTurnValue(const Item& it, float v) {
    const bool smoothNow = turnmode::Smooth(TurnKey(L"snap", 0.0f), TurnKey(L"smooth", -1.0f));
    wchar_t buf[32];
    if (wcscmp(it.key, L"smooth") == 0) {
        WritePrivateProfileStringW(L"turn", L"smooth", v != 0.0f ? L"1" : L"0", g_ini);
        return;
    }
    swprintf_s(buf, L"%d", (int)std::lround(std::clamp(v, turnmode::kSnapMin, turnmode::kSnapMax)));
    WritePrivateProfileStringW(L"turn", L"snap", buf, g_ini);
    WritePrivateProfileStringW(L"turn", L"smooth", smoothNow ? L"1" : L"0", g_ini);
}

Item* FindItem(const wchar_t* section, const wchar_t* key);
void RefreshControls();
// "@toolbar" type: the toolbar's type over [hud] hide and [hud] bar_place (shared/toolbar_type.h).
float ReadToolbarType();
void WriteToolbarType(float v);

// "@remap" items: a button of the controllers, [direct] <key> of d2r_vr.ini, through vr/direct_remap.h (the plugin's own table parser). The whole
// section is read once per call (GetPrivateProfileSection: one read of the file, not forty) into a map the parser asks; keys are not case-sensitive in an ini.
std::string AsciiOf(const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s.push_back(*w < 128 ? (char)*w : '?');
    return s;
}
directinput::remap::Section DirectSection() {
    directinput::remap::Section m;
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetPrivateProfileSectionW(L"direct", buf.data(), (DWORD)buf.size(), g_ini);
    for (const wchar_t* p = buf.data(); (DWORD)(p - buf.data()) < n && *p; p += wcslen(p) + 1) directinput::remap::AddLine(m, AsciiOf(p));
    return m;
}
const directinput::remap::Row* RemapRowOf(const Item& it) {
    const std::string key = AsciiOf(it.key);
    for (const directinput::remap::Row& r : directinput::remap::Rows())
        if (directinput::remap::KeyOf(r) == key) return &r;
    return nullptr;
}
float ReadRemapValue(const Item& it) {
    const directinput::remap::Row* r = RemapRowOf(it);
    if (!r) return 0.0f;
    const directinput::remap::Section m = DirectSection();
    return (float)directinput::remap::Current(directinput::remap::Getter(m), *r);
}
void RefreshControls();
void WriteRemapValue(const Item& it, float v) {
    const directinput::remap::Row* r = RemapRowOf(it);
    if (!r) return;
    const directinput::remap::Section m = DirectSection();
    directinput::remap::Change c;
    if (!directinput::remap::Choose(directinput::remap::Getter(m), *r, (int)std::lround(v), &c)) { RefreshControls(); return; }   // "Custom": nothing to write, the list shows the ini again
    const std::wstring key(c.key.begin(), c.key.end());
    const std::wstring text(c.text.begin(), c.text.end());
    WritePrivateProfileStringW(L"direct", key.c_str(), c.erase ? nullptr : text.c_str(), g_ini);
}

// The weight-physics and grip libraries: d2r_vr_weight.dll / d2r_vr_grip.dll in d2rloader\d2r_vr_libs, or beside the plugin (and d2r_vr.ini) as the plugin
// looks for them (vr/weight_lib.cpp, vr/grip_lib.cpp: <the plugin's folder>\..\d2r_vr_libs first - 0.169 moved them out of the plugins, where D2RLoader took
// them for plugins). For the D2RLoader mod D2R-VR that is mods\D2R-VR\d2rloader\d2r_vr_libs. Optional; "found" greys out its tick while it is not there.
bool LibFileFound(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, g_ini);
    if (wchar_t* slash = wcsrchr(dir, L'\\')) slash[1] = 0;
    const std::wstring libs = std::wstring(dir) + L"..\\d2r_vr_libs\\" + name;
    return GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES || GetFileAttributesW((std::wstring(dir) + name).c_str()) != INVALID_FILE_ATTRIBUTES;
}
bool WeightLibFound() { return LibFileFound(L"d2r_vr_weight.dll"); }
bool GripLibFound() { return LibFileFound(L"d2r_vr_grip.dll"); }

float ReadValue(const Item& it) {
    if (wcscmp(it.section, L"@weightlib") == 0) return WeightLibFound() ? 1.0f : 0.0f;

    if (wcscmp(it.section, L"@toolbar") == 0) return ReadToolbarType();
    if (wcscmp(it.section, L"@remap") == 0) return ReadRemapValue(it);
    if (wcscmp(it.section, L"@griplib") == 0) return GripLibFound() ? 1.0f : 0.0f;
    if (wcscmp(it.section, L"@turn") == 0) return ReadTurnValue(it);
    if (wcscmp(it.section, L"@render") == 0) return ReadRenderSize(it);
    if (wcscmp(it.section, L"@preset") == 0) return (float)MatchPreset();
    if (wcscmp(it.section, L"@stereo3d") == 0) return ReadStereo3D();
    if (wcscmp(it.section, L"@reshade") == 0) return ReadReShadeValue(it);
    if (wcscmp(it.section, L"@game") == 0) { float v = it.def; ReadGameValue(it, &v); return v; }
    if (wcscmp(it.section, L"@window") == 0) return ReadSquareWindow();
    if (wcscmp(it.section, L"@bodywalk") == 0) return ReadBodyWalkValue(it);
    wchar_t buf[64], def[64];
    swprintf_s(def, L"%g", it.def);
    // [hud_top] / [hud_third] / [hud_floor] / [hud_inside]: a view's own interface; unset, the
    // value all views shared before, in [hud] (vrcam reads it the same way).
    if (wcsncmp(it.section, L"hud_", 4) == 0) {
        GetPrivateProfileStringW(it.section, it.key, L"#", buf, 64, g_ini);
        if (wcscmp(buf, L"#") == 0) GetPrivateProfileStringW(L"hud", it.key, def, buf, 64, g_ini);
    } else if (it.oldKey) {
        GetPrivateProfileStringW(SectionOf(it), it.key, L"#", buf, 64, FileOf(it));
        if (wcscmp(buf, L"#") == 0) {
            GetPrivateProfileStringW(SectionOf(it), it.oldKey, def, buf, 64, FileOf(it));
            if (!it.oldMap.empty()) {   // the older key's values mean other things here: through the table
                wchar_t* e2 = nullptr;
                const float ov = wcstof(buf, &e2);
                if (e2 != buf && std::isfinite(ov)) swprintf_s(buf, L"%g", it.oldMap[(size_t)std::clamp((int)std::lround(ov), 0, (int)it.oldMap.size() - 1)]);
            }
        }
    } else
    GetPrivateProfileStringW(SectionOf(it), it.key, def, buf, 64, FileOf(it));
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? v : it.def;
}

void ShowPreset();
void ApplyFrameCap();
void WriteVrFrameCap(int value);
void ApplySafeScreenForPlatform();
void SyncTwins(const Item& from);
void RefreshStrikeCm();
void RefreshControls();
void WriteValue(const Item& it, float v) {
    if (wcscmp(it.section, L"@toolbar") == 0) { WriteToolbarType(v); return; }
    if (wcscmp(it.section, L"@remap") == 0) { WriteRemapValue(it, v); return; }
    if (wcscmp(it.section, L"@weightlib") == 0 || wcscmp(it.section, L"@griplib") == 0) return;   // a fact about the folder, not a setting
    if (wcscmp(it.section, L"@turn") == 0) { WriteTurnValue(it, v); return; }
    if (wcscmp(it.section, L"@render") == 0) { WriteRenderSize(it, v); return; }
    if (wcscmp(it.section, L"@preset") == 0) return;   // ApplyPreset, from the drop-down
    if (wcscmp(it.section, L"@stereo3d") == 0) {
        if (!WriteStereo3D(v)) { RefreshControls(); return; }   // the list back to what is set
        ApplyFrameCap(); ShowPreset(); return;
    }
    if (wcscmp(it.section, L"@reshade") == 0) { WriteReShadeValue(it, v); ShowPreset(); return; }
    if (wcscmp(it.section, L"@game") == 0) {
        if (wcscmp(it.key, L"Framerate Cap") == 0 && g_platform == 1) WriteVrFrameCap((int)std::lround(v));   // VR's own cap: the player's kept first
        else WriteGameValue(it, v);
        // a size the player picks here is his, whatever it is - an 80 of his is not VR's 80 (100, the game's default, VR starts at 80 again: the box below keeps it)
        if (wcscmp(it.key, L"Safe Screen Percent") == 0) WritePrivateProfileStringW(L"hud", L"safe_screen_written", L"-1", g_ini);
        return;
    }
    if (wcscmp(it.section, L"@window") == 0) { WriteSquareWindow(v != 0.0f); return; }
    if (wcscmp(it.section, L"@bodywalk") == 0) { WriteBodyWalkValue(it, v); return; }
    wchar_t buf[64];
    if (it.step >= 1.0f) swprintf_s(buf, L"%d", (int)std::lround(v));
    else swprintf_s(buf, L"%g", std::round(v / it.step) * it.step);
    WritePrivateProfileStringW(SectionOf(it), it.key, buf, FileOf(it));
    if (wcscmp(it.section, L"openxr") == 0 && wcscmp(it.key, L"window_size") == 0 && ReadSquareWindow() != 0.0f) WriteSquareWindow(true);   // the new size at once
    if (wcscmp(it.section, L"stereo") == 0 && (wcscmp(it.key, L"headset_rate") == 0 || wcscmp(it.key, L"afr") == 0 || wcscmp(it.key, L"pair_per_tick") == 0)) ApplyFrameCap();
    if (wcscmp(it.section, L"flatvr") == 0 && wcscmp(it.key, L"frame_sync") == 0) ApplyFrameCap();   // FlatVR's frames pace the game: the cap goes to 2 x the rate
    if (wcscmp(it.section, L"hud") == 0 && wcscmp(it.key, L"safe_screen_vr") == 0) ApplySafeScreenForPlatform();   // ticked: 80 now (game closed); unticked: the player's own back
    ShowPreset();   // a key of a preset changed by hand: Custom (or the preset it now matches)
    SyncTwins(it);   // the same key shown on another tab (the Strikes tab's debug ticks are the Debug tab's)
    if (wcscmp(it.section, L"gameplay") == 0 && wcsncmp(it.key, L"melee_amp", 9) == 0) RefreshStrikeCm();   // the swing amplitudes in cm
}

float ReadToolbarType() {
    const Item* hide = FindItem(L"hud", L"hide");
    const Item* place = FindItem(L"hud", L"bar_place");
    return hide && place ? (float)toolbartype::TypeOf((int)std::lround(ReadValue(*hide)), (int)std::lround(ReadValue(*place))) : 0.0f;
}
void WriteToolbarType(float v) {
    const Item* hide = FindItem(L"hud", L"hide");
    const Item* place = FindItem(L"hud", L"bar_place");
    if (!hide || !place) return;
    int h = (int)std::lround(ReadValue(*hide)), p = (int)std::lround(ReadValue(*place));
    toolbartype::Choose((int)std::lround(v), &h, &p);
    WriteValue(*hide, (float)h);
    WriteValue(*place, (float)p);
    RefreshControls();   // the two drop-downs below show what the type made of them
}

// The game's frame cap (its Settings.json) from the headset's rate: a game frame per
// headset frame - FlatVR shows the newest picture each headset frame, and a game faster or
// slower than the headset judders on head turns (2026-10-09: 90 against 90 smooth, 170 not).
// Eyes by turns draw one eye a game frame: twice the rate. Mono, the depth and Stereo (both
// eyes from one game frame, [stereo] pair_per_tick) the rate itself.
const Item* PresetItem(const wchar_t* section, const wchar_t* key);
void RefreshControls();
// The cap belongs to VR (shared/frame_cap.h, tested by frame_cap_test): the player's own cap in the file is kept ([stereo] cap_before)
// before VR writes its own, what VR wrote is kept too ([stereo] cap_written) to tell it from a cap the player set, and flat mode
// puts the player's own back - at once on a switch of the platform, on START, and at the plugin's start (vrcam's ReconcileFrameCap).
// Nothing kept: nothing is written in flat mode.
framecap::State FrameCapState() {
    framecap::State s;
    s.own = (int)GetPrivateProfileIntW(L"stereo", L"cap_before", -1, g_ini);
    s.written = (int)GetPrivateProfileIntW(L"stereo", L"cap_written", -1, g_ini);
    return s;
}
void SaveFrameCapState(const framecap::Plan& p) {
    wchar_t b[16];
    if (p.saveOwn) { swprintf_s(b, L"%d", p.own); WritePrivateProfileStringW(L"stereo", L"cap_before", b, g_ini); }
    if (p.saveWritten) { swprintf_s(b, L"%d", p.written); WritePrivateProfileStringW(L"stereo", L"cap_written", b, g_ini); }
}
// The cap in the game's file, -1 when there is none to read.
int FrameCapInFile() {
    std::string text;
    return ReadFileText(GameSettingsPath(), &text) ? framecap::Read(text) : -1;
}
bool WriteFrameCapToFile(int value) {
    const std::wstring path = GameSettingsPath();
    std::string text;
    if (!ReadFileText(path, &text)) return false;
    bool ok = false;
    const std::string out = framecap::With(text, value, &ok);
    if (!ok) return false;
    if (out == text) return true;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
    return (bool)f;
}
// VR's cap (the headset's rate for the 3D choice, or the "Game frame cap" slider): the player's own kept first.
void WriteVrFrameCap(int value) {
    const framecap::Plan p = framecap::ForVr(FrameCapInFile(), FrameCapState(), value);
    SaveFrameCapState(p);
    if (p.write) WriteFrameCapToFile(p.value);
}
// Flat mode: the cap is the player's own - VR's goes, the one kept comes back (none kept: nothing is written).
void RestoreFrameCapForFlat() {
    const framecap::Plan p = framecap::ForFlat(FrameCapInFile(), FrameCapState());
    SaveFrameCapState(p);
    if (p.write && !WriteFrameCapToFile(p.value)) SetStatus(L"perf_restart", 1, L"The game's Settings.json could not be written (your frame cap)");
}
// The cap the headset's rate and the 3D choice make (shared/frame_cap.h VrCapFor - one rule for the write, this status line and vrcam's log).
framecap::VrCap VrCapNow(int* hz = nullptr) {
    const int h = framecap::HzOfRate((int)GetPrivateProfileIntW(L"stereo", L"headset_rate", 3, g_ini));
    if (hz) *hz = h;
    return framecap::VrCapFor(h, GetPrivateProfileIntW(L"stereo", L"afr", 0, g_ini) != 0,
                              GetPrivateProfileIntW(L"stereo", L"pair_per_tick", 0, g_ini) != 0,
                              GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0,
                              GetPrivateProfileIntW(L"flatvr", L"frame_sync", 0, g_ini) != 0);
}
// "Frame cap in the game's file: 240. For this choice VR writes 240 = 120 Hz x 2: eyes by turns ..." under the headset rate: what is written and why.
void UpdateFrameCapStatus() {
    if (g_platform != 1) return;
    int hz = 0;
    const framecap::VrCap c = VrCapNow(&hz);
    const int inFile = FrameCapInFile();
    const char* r = framecap::Reason(c.why);
    std::wstring line = L"Frame cap in the game's file: " + (inFile >= 0 ? std::to_wstring(inFile) : std::wstring(L"unknown")) + L". For this choice VR writes " +
                        (c.value ? std::to_wstring(c.value) : std::wstring(L"none (0)")) +
                        (c.factor ? L" = " + std::to_wstring(hz) + L" Hz x " + std::to_wstring(c.factor) : std::wstring()) + L": " + std::wstring(r, r + strlen(r)) + L".";
    SetStatus(L"frame_cap", inFile == c.value ? 0 : 3, line.c_str());   // (green: the file has it; grey: it does not yet)
}
void LogLine(const std::wstring& line);
void ApplyFrameCap() {
    if (g_platform != 1) return;   // flat mode has no cap of ours
    int hz = 0;
    const framecap::VrCap c = VrCapNow(&hz);
    WriteVrFrameCap(c.value);
    const char* r = framecap::Reason(c.why);
    LogLine(L"frame cap: " + (c.value ? std::to_wstring(c.value) : std::wstring(L"none (0)")) + L" written for the headset's " + std::to_wstring(hz) + L" Hz x " +
            std::to_wstring(c.factor) + L" - " + std::wstring(r, r + strlen(r)));
    RefreshControls();
    UpdateFrameCapStatus();
}
// A switch of the platform (and START in flat mode): flat gets its own cap back, VR the headset's rate again.
void ApplyFrameCapForPlatform() {
    if (g_platform == 1) ApplyFrameCap();
    else RestoreFrameCapForFlat();
}

// The game's interface size ("Safe Screen Percent"): VR starts it at 80 where the player chose none, flat mode has his own (shared/safe_screen.h, tested by
// safe_screen_test; the plugin does the same at its start, vrcam's ReconcileSafeScreen). The player's value before VR's is kept in [hud] safe_screen_before,
// what VR wrote in [hud] safe_screen_written. Only with the game CLOSED: the game reads the file at its start and writes its own copy back at its quit, and
// the toolbar's cut follows the number in the file - a running game's picture would not match it. The plugin deals with it at the game's next start.
bool SafeScreenVrBox() { return GetPrivateProfileIntW(L"hud", L"safe_screen_vr", 1, g_ini) != 0; }
safescreen::State SafeScreenState() {
    safescreen::State s;
    s.own = (int)GetPrivateProfileIntW(L"hud", L"safe_screen_before", -1, g_ini);
    s.written = (int)GetPrivateProfileIntW(L"hud", L"safe_screen_written", -1, g_ini);
    return s;
}
void SaveSafeScreenState(const safescreen::Plan& p) {
    wchar_t b[16];
    if (p.saveOwn) { swprintf_s(b, L"%d", p.own); WritePrivateProfileStringW(L"hud", L"safe_screen_before", b, g_ini); }
    if (p.saveWritten) { swprintf_s(b, L"%d", p.written); WritePrivateProfileStringW(L"hud", L"safe_screen_written", b, g_ini); }
    if (p.clearWritten) WritePrivateProfileStringW(L"hud", L"safe_screen_written", L"-1", g_ini);
}
// The shape (width over height) of the picture the game's interface is laid out in, as the ini says it: native OpenXR's render size (a square, or the
// headset's own shape when its box is ticked and the game has told it), else the game's window (FlatVR: its picture; a square when D2R VR's square window is on).
float PictureAspectNow(const std::string& settingsJson) {
    const rendersize::Pick pick = rendersize::FromIni((int)GetPrivateProfileIntW(L"render", L"vr_resolution", 0, g_ini),
                                                     (int)GetPrivateProfileIntW(L"render", L"vr_resolution_px", 0, g_ini));
    const bool native = GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0;
    wchar_t hs[MAX_PATH];   // what the headset's runtime recommended at the game's last headset session (d2r_vr_headset.ini beside the ini)
    wcscpy_s(hs, g_ini);
    if (wchar_t* slash = wcsrchr(hs, L'\\')) slash[1] = 0;
    wcscat_s(hs, L"d2r_vr_headset.ini");
    const int rw = (int)GetPrivateProfileIntW(L"headset", L"recommended_w", 0, hs), rh = (int)GetPrivateProfileIntW(L"headset", L"recommended_h", 0, hs);
    const winjson::Windowed win = winjson::Read(settingsJson);
    return safescreen::PictureAspect(native && pick.kind == rendersize::Kind::Square, native && pick.kind == rendersize::Kind::Headset, rw, rh,
                                     ReadSquareWindow() != 0.0f, win.ok ? win.w : 0, win.ok ? win.h : 0);
}
void ApplySafeScreenForPlatform() {
    // (a test's own Settings.json - D2RVR_GAME_SETTINGS_JSON - is nobody's running game's)
    const bool testFile = GetEnvironmentVariableW(L"D2RVR_GAME_SETTINGS_JSON", nullptr, 0) > 0;
    if (!testFile && (ProcessRunning(L"D2RLoader.exe") || ProcessRunning(L"D2R.exe"))) return;   // (the plugin's start does it)
    std::string text;
    if (!ReadFileText(GameSettingsPath(), &text)) return;
    const int now = safescreen::Read(text);
    if (now < 0) return;
    const safescreen::Plan p = g_platform == 1 && SafeScreenVrBox() ? safescreen::ForVr(now, SafeScreenState(), true, safescreen::VrValueFor(PictureAspectNow(text)))
                                                                    : safescreen::ForFlat(now, SafeScreenState(), false);
    SaveSafeScreenState(p);
    if (!p.write) return;
    bool ok = false;
    const std::string out = safescreen::With(text, p.value, &ok);
    if (!ok || out == text) return;
    const std::wstring path = GameSettingsPath();
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << out;
    RefreshControls();   // (the slider shows the number now in the file)
}

// Two side panels (a trader, the mercenary or a player's trade on the left, the inventory on the right) need room side by side: whether the
// game's interface size in the file gives it, for the shape of the picture (shared/safe_screen.h, the numbers of the game's own layout).
// Above the limit the panels lie over one another or run off the picture's edges - the outer columns cannot be seen or pointed at.
void UpdateSafeScreenStatus() {
    if (g_platform != 1) return;
    std::string text;
    const int pct = ReadFileText(GameSettingsPath(), &text) ? safescreen::Read(text) : -1;
    if (pct < 0) {
        SetStatus(L"safe_screen_fit", 3, L"The game's interface size is not in its Settings.json yet (the game writes the file when it quits the first time).");
        return;
    }
    const float aspect = PictureAspectNow(text);   // (FlatVR has no render size: its picture is the window)
    const int fit = safescreen::FitPercent(aspect);
    wchar_t line[300];
    if (safescreen::PanelsFit(pct, aspect))
        swprintf_s(line, L"Interface size %d %%: two side panels (trader or mercenary + inventory) fit this picture (up to %d %%).", pct, fit);
    else
        swprintf_s(line, L"Interface size %d %%: too big - two open side panels (trader or mercenary + inventory) overlap or run off this picture's edge. %d %% or less.",
                   pct, fit);
    SetStatus(L"safe_screen_fit", safescreen::PanelsFit(pct, aspect) ? 0 : 2, line);
}

int Steps(const Item& it) { return (int)std::lround((it.max - it.min) / it.step); }
float FromPos(const Item& it, int pos) { return it.min + pos * it.step; }
int ToPos(const Item& it, float v) { return std::max(0, std::min(Steps(it), (int)std::lround((v - it.min) / it.step))); }

void ShowValue(const Item& it, float v) {
    wchar_t buf[32];
    if (wcscmp(it.section, L"@render") == 0 && wcscmp(it.key, L"vr_px") == 0) {   // "2048 x 2048", the left stop "window's size"
        const int side = rendersize::PxOfStop((int)std::lround(v));
        if (side) swprintf_s(buf, L"%d x %d", side, side);
        else wcscpy_s(buf, L"window's size");
    } else
    if (it.step >= 1.0f) swprintf_s(buf, L"%d", (int)std::lround(v));
    else if (it.step >= 0.1f) swprintf_s(buf, L"%.1f", v);
    else swprintf_s(buf, L"%.2f", v);
    SetWindowTextW(it.value, buf);
}

// A typed number: kept to the slider's range and step, written, the slider moved to it.
void CommitTyped(Item& it) {
    wchar_t buf[64];
    GetWindowTextW(it.value, buf, 64);
    for (wchar_t* c = buf; *c; ++c) if (*c == L',') *c = L'.';
    wchar_t* end = nullptr;
    float v = wcstof(buf, &end);
    if (end == buf || !std::isfinite(v)) v = FromPos(it, (int)SendMessageW(it.ctl, TBM_GETPOS, 0, 0));
    v = FromPos(it, ToPos(it, v));
    SendMessageW(it.ctl, TBM_SETPOS, TRUE, ToPos(it, v));
    ShowValue(it, v);
    WriteValue(it, v);
}

// Enter in a value box takes the number (a single-line EDIT says nothing of it on its own).
LRESULT CALLBACK ValueEditProc(HWND wnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && w == VK_RETURN) {
        const int id = GetDlgCtrlID(wnd);
        if (id >= kValueId && id < kValueId + (int)g_items.size()) CommitTyped(g_items[id - kValueId]);
        SendMessageW(wnd, EM_SETSEL, 0, -1);
        return 0;
    }
    if (msg == WM_CHAR && (w == VK_RETURN || w == L'\n')) return 0;   // no beep
    return DefSubclassProc(wnd, msg, w, l);
}

// The same key on two tabs (the Strikes tab's debug ticks are the Debug tab's): the other control shows what was just written.
void SyncTwins(const Item& from) {
    if (!from.key || !from.section) return;
    for (Item& o : g_items) {
        if (&o == &from || !o.key || !o.ctl || !o.section || wcscmp(o.key, from.key) != 0 || wcscmp(o.section, from.section) != 0) continue;
        const float v = ReadValue(o);
        switch (o.kind) {
        case Kind::Slider:
            SendMessageW(o.ctl, TBM_SETPOS, TRUE, ToPos(o, v));
            ShowValue(o, v);
            break;
        case Kind::Toggle:
        case Kind::Invert:
            SendMessageW(o.ctl, BM_SETCHECK, (o.kind == Kind::Toggle ? v != 0.0f : v < 0.0f) ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case Kind::Choice: SendMessageW(o.ctl, CB_SETCURSEL, ToPos(o, v), 0); break;
        default: break;
        }
    }
}

// The Strikes tab's table: one row for each weapon kind the touch swings, in vr/strike_direction.h's order (strikedir::KindName - the plugin reads melee_<key>_<name> for each of them).
constexpr int kStrikeKindCount = 15;
const wchar_t* const kStrikeKinds[kStrikeKindCount] = {L"sword", L"axe", L"mace", L"scepter", L"wand", L"staff", L"polearm", L"spear", L"dagger", L"claw", L"orb", L"unarmed", L"other", L"javelin", L"throwing"};
const wchar_t* const kStrikeLabels[kStrikeKindCount] = {L"Swords", L"Axes", L"Maces", L"Scepters", L"Wands", L"Staffs", L"Polearms", L"Spears", L"Daggers", L"Claws", L"Orbs", L"Fists", L"Other", L"Javelins", L"Throwing"};
// 0 piercing, 1 edged, 2 blunt, 3 staff (strikedir::FamilyOf): which factor a kind follows while it has none of its own.
const int kStrikeFamily[kStrikeKindCount] = {1, 1, 2, 2, 0, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr const wchar_t* kStrikeTableGroup = L"Each kind of weapon (type a number; 0 = follow the general one)";

float StrikeIni(const wchar_t* key, float def, float lo, float hi) {
    wchar_t buf[64], dflt[32];
    swprintf_s(dflt, L"%g", def);
    GetPrivateProfileStringW(L"gameplay", key, dflt, buf, 64, g_ini);
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}

// The swing amplitudes as the plugin works them out (strikedir::AmplitudeCm): the piercing cm times the kind's factor - its own, or its family's. Shown in the table's cm column and in the
// labels of the three family sliders.
void RefreshStrikeCm() {
    const bool on = GetPrivateProfileIntW(L"gameplay", L"melee_amplitude", 1, g_ini) != 0;
    const float base = StrikeIni(L"melee_amp_pierce_cm", 8.0f, 1.0f, 60.0f);
    const float family[4] = {1.0f, StrikeIni(L"melee_amp_edged_x", 1.4f, 0.5f, 6.0f), StrikeIni(L"melee_amp_blunt_x", 2.0f, 0.5f, 6.0f), StrikeIni(L"melee_amp_staff_x", 1.75f, 0.5f, 6.0f)};
    for (int k = 0; k < kStrikeKindCount; ++k) {
        wchar_t key[48], text[32], now[32];
        swprintf_s(key, L"melee_amp_x_%s", kStrikeKinds[k]);
        const float own = StrikeIni(key, 0.0f, 0.0f, 8.0f);
        if (on) swprintf_s(text, L"%.1f", base * (own >= 0.25f ? own : family[kStrikeFamily[k]]));
        else wcscpy_s(text, L"-");
        swprintf_s(key, L"st_cm_%s", kStrikeKinds[k]);
        for (Item& it : g_items)
            if (it.kind == Kind::Status && it.cell >= 0 && it.ctl && wcscmp(it.key, key) == 0) {
                GetWindowTextW(it.ctl, now, 32);
                if (wcscmp(now, text) != 0) SetWindowTextW(it.ctl, text);
            }
    }
    const wchar_t* const sliders[3] = {L"melee_amp_edged_x", L"melee_amp_blunt_x", L"melee_amp_staff_x"};
    for (int f = 0; f < 3; ++f)
        for (Item& it : g_items)
            if (it.kind == Kind::Slider && it.name && it.key && it.section && wcscmp(it.section, L"gameplay") == 0 && wcscmp(it.key, sliders[f]) == 0) {
                wchar_t text[160];
                if (on) swprintf_s(text, L"%s = %.1f cm", it.label, base * family[f + 1]);
                else wcscpy_s(text, it.label);
                SetWindowTextW(it.name, text);
            }
}

// Created hidden and with no size: LayoutPage places and shows them.
HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

void AddTip(HWND ctl, const wchar_t* text) {
    if (!ctl || !text) return;
    TTTOOLINFOW ti{sizeof ti};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(ctl);
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void Build(HWND page) {
    int tab = -1, tabModes = kBoth, groupModes = kBoth;
    for (size_t i = 0; i < g_items.size(); ++i) {
        Item& it = g_items[i];
        const int id = kFirstId + (int)i;
        constexpr DWORD kLabel = SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX | SS_NOTIFY;   // SS_NOTIFY: the tooltip sees the mouse
        switch (it.kind) {
        case Kind::Tab:   // into the strip by RebuildTabs, for the platform shown
            it.tab = ++tab;
            tabModes = it.modes;
            groupModes = kBoth;
            continue;
        case Kind::Group:
            it.modes &= tabModes;
            groupModes = it.modes;
            it.name = Make(page, L"STATIC", it.label, SS_LEFT | SS_NOPREFIX, id);
            it.line = Make(page, L"STATIC", L"", SS_ETCHEDHORZ, -1);
            break;
        case Kind::Radio: {
            it.modes &= groupModes;
            const int v = (int)std::lround(ReadValue(it));
            for (size_t k = 0; k < it.choices.size(); ++k) {
                // WS_GROUP on the first: each set of radios is a group of its own
                HWND b = Make(page, L"BUTTON", it.choices[k], BS_AUTORADIOBUTTON | WS_TABSTOP | (k == 0 ? WS_GROUP : 0),
                              kRadioId + (int)i * 8 + (int)k);
                SendMessageW(b, BM_SETCHECK, v == (int)it.min + (int)k ? BST_CHECKED : BST_UNCHECKED, 0);
                AddTip(b, it.tip);
                it.radios.push_back(b);
                if ((int)k >= it.soon || (it.soonMask >> k & 1u)) EnableWindow(b, FALSE);
            }
            break;
        }
        case Kind::Status:
            it.modes &= groupModes;
            // the weapon list's rows take clicks (SS_NOTIFY): one picks that kind for the sliders
            it.ctl = Make(page, L"STATIC", it.label, SS_LEFT | SS_NOPREFIX | ((it.key[0] == L'w' && it.key[1] == L'k') || it.cell >= 0 ? SS_NOTIFY : 0),
                          it.key[0] == L'w' && it.key[1] == L'k' ? id : -1);   // wraps to two lines
            it.color = GetSysColor(it.cell >= 0 ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);   // (a table's cells are plain text)
            break;
        case Kind::File:
        case Kind::Color:
            it.modes &= groupModes;
            it.name = Make(page, L"STATIC", it.label, kLabel, -1);
            it.ctl = Make(page, L"EDIT", ReadPath(it).c_str(), ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, id);
            it.browse = Make(page, L"BUTTON", L"...", BS_PUSHBUTTON | WS_TABSTOP, kBrowseId + (int)i);
            it.clear = Make(page, L"BUTTON", L"x", BS_PUSHBUTTON | WS_TABSTOP, kClearId + (int)i);
            if (it.kind == Kind::Color) ShowColor(it);
            AddTip(it.browse, it.kind == Kind::Color ? L"Pick a colour" : L"Pick a picture");
            AddTip(it.clear, L"Clear");
            break;
        case Kind::Slider: {
            const float v = ReadValue(it);
            it.modes &= groupModes;
            it.name = Make(page, L"STATIC", it.label, kLabel, -1);
            it.ctl = Make(page, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_FIXEDLENGTH | WS_TABSTOP, id);
            SendMessageW(it.ctl, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(it.ctl, TBM_SETRANGEMAX, FALSE, Steps(it));
            SendMessageW(it.ctl, TBM_SETPAGESIZE, 0, std::max(1, Steps(it) / 20));
            SendMessageW(it.ctl, TBM_SETPOS, TRUE, ToPos(it, v));
            // A box to type the number into as well: Enter or leaving it takes the value.
            it.value = Make(page, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, kValueId + (int)i);
            SetWindowSubclass(it.value, ValueEditProc, 0, 0);
            ShowValue(it, v);
            break;
        }
        case Kind::Choice: {
            it.modes &= groupModes;
            it.name = Make(page, L"STATIC", it.label, kLabel, -1);
            it.ctl = Make(page, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id);
            for (const wchar_t* c : it.choices) SendMessageW(it.ctl, CB_ADDSTRING, 0, (LPARAM)c);
            SendMessageW(it.ctl, CB_SETCURSEL, ToPos(it, ReadValue(it)), 0);
            break;
        }
        case Kind::Button:
            it.modes &= groupModes;
            it.ctl = Make(page, L"BUTTON", it.label, BS_PUSHBUTTON | WS_TABSTOP, id);
            break;
        case Kind::Toggle:
        case Kind::Invert: {
            it.modes &= groupModes;
            const float v = ReadValue(it);
            it.ctl = Make(page, L"BUTTON", it.label, BS_AUTOCHECKBOX | WS_TABSTOP, id);
            const bool on = it.kind == Kind::Toggle ? v != 0.0f : v < 0.0f;
            SendMessageW(it.ctl, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        }
        }
        it.tab = tab;
        AddTip(it.name, it.tip);
        AddTip(it.ctl, it.tip);
        if (it.cell >= 0 && it.kind == Kind::Slider) AddTip(it.value, it.tip);   // (a table's slider shows its number box only)
    }
    g_tabCount = tab + 1;
    for (Item& it : g_items)
        for (Item::Need& nd : it.needs)
            for (size_t j = 0; j < g_items.size(); ++j)
                if (g_items[j].key && wcscmp(g_items[j].section, nd.section) == 0) {
                    if (wcscmp(g_items[j].key, nd.key) == 0) nd.item = (int)j;
                    if (nd.orKey && wcscmp(g_items[j].key, nd.orKey) == 0) nd.orItem = (int)j;
                }
    for (Item& it : g_items)
        if (it.showFor.key)
            for (size_t j = 0; j < g_items.size(); ++j)
                if (g_items[j].key && wcscmp(g_items[j].section, it.showFor.section) == 0 && wcscmp(g_items[j].key, it.showFor.key) == 0) it.showFor.item = (int)j;
}

// Greys out what the current values make unused (the FOV slider while the FOV comes from FlatVR...).
void LayoutPage();
// [hud] bar_*: one set for every place before; now a set for the forearm and one
// for the body, both starting from it.
void CarryBarKeys() {
    for (const wchar_t* name : {L"width", L"split", L"along", L"side", L"lift", L"roll", L"tip", L"spin"}) {
        wchar_t old[32], v[64];
        swprintf_s(old, L"bar_%s", name);
        if (!GetPrivateProfileStringW(L"hud", old, L"", v, 64, g_ini) || !v[0]) continue;
        for (const wchar_t* set : {L"_arm", L"_body"}) {
            wchar_t key[48], have[64];
            swprintf_s(key, L"bar_%s%s", name, set);
            if (!GetPrivateProfileStringW(L"hud", key, L"", have, 64, g_ini) || !have[0])
                WritePrivateProfileStringW(L"hud", key, v, g_ini);
        }
    }
}
void UpdateEnabled() {
    bool moved = false;   // a ShowFor item came or went: the page laid out again
    for (Item& it : g_items) {
        if (it.showFor.item < 0) continue;
        const long v = std::lround(ReadValue(g_items[it.showFor.item]));
        const bool off = v < 0 || v > 31 || !(it.showFor.mask & (1u << v));
        if (off != it.off) { it.off = off; moved = true; }
    }
    if (moved && g_page) LayoutPage();
    for (const Item& it : g_items) {
        if (it.needs.empty()) continue;
        BOOL on = TRUE;
        for (const Item::Need& nd : it.needs)
            if (nd.item >= 0 && std::lround(ReadValue(g_items[nd.item])) != std::lround(nd.value) &&
                !(nd.orItem >= 0 && std::lround(ReadValue(g_items[nd.orItem])) == std::lround(nd.value))) on = FALSE;
        for (HWND h : {it.name, it.ctl, it.value, it.browse, it.clear})
            if (h) EnableWindow(h, on);
        for (HWND h : it.radios) EnableWindow(h, on);
    }
}

// Every control as the ini has it now: after a preset wrote several keys, the same key
// shown on two tabs, etc.
void RefreshControls() {
    for (Item& it : g_items) {
        if (!it.key || !it.ctl || !it.section) continue;
        if (wcscmp(it.section, L"@status") == 0 || wcscmp(it.section, L"@update") == 0 || wcscmp(it.section, L"@run") == 0) continue;
        switch (it.kind) {
        case Kind::Slider: {
            const float v = ReadValue(it);
            SendMessageW(it.ctl, TBM_SETPOS, TRUE, ToPos(it, v));
            ShowValue(it, v);
            break;
        }
        case Kind::Toggle:
        case Kind::Invert: {
            const float v = ReadValue(it);
            const bool on = it.kind == Kind::Toggle ? v != 0.0f : v < 0.0f;
            SendMessageW(it.ctl, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        }
        case Kind::Choice: SendMessageW(it.ctl, CB_SETCURSEL, ToPos(it, ReadValue(it)), 0); break;
        default: break;
        }
    }
}

void ShowPreset() {
    for (Item& it : g_items)
        if (it.ctl && it.section && wcscmp(it.section, L"@preset") == 0) SendMessageW(it.ctl, CB_SETCURSEL, MatchPreset(), 0);
}

void ApplyPreset(int p) {
    if (p < 0 || p >= kPresets) return;
    for (const PresetKey& k : kPreset) {
        if (const Item* it = PresetItem(k.section, k.key)) { WriteValue(*it, k.v[p]); continue; }
        wchar_t buf[32];
        swprintf_s(buf, L"%g", k.v[p]);
        WritePrivateProfileStringW(k.section, k.key, buf, g_ini);
    }
    RefreshControls();
    UpdateEnabled();
}

bool ShownNow(const Item& it) { return it.kind != Kind::Tab && it.tab == g_tab && (it.modes & PlatformBit()) != 0 && !it.hidden && !it.off; }

// The strip: the current platform's tabs only. The tab shown stays if it is one of them.
void RebuildTabs() {
    SendMessageW(g_tabs, TCM_DELETEALLITEMS, 0, 0);
    g_tabIds.clear();
    for (const Item& it : g_items) {
        if (it.kind != Kind::Tab || !(it.modes & PlatformBit())) continue;
        TCITEMW ti{TCIF_TEXT};
        ti.pszText = (LPWSTR)it.label;
        SendMessageW(g_tabs, TCM_INSERTITEMW, g_tabIds.size(), (LPARAM)&ti);
        g_tabIds.push_back(it.tab);
    }
    auto at = std::find(g_tabIds.begin(), g_tabIds.end(), g_tab);
    if (at == g_tabIds.end()) { g_tab = g_tabIds.empty() ? 0 : g_tabIds[0]; at = g_tabIds.begin(); }
    SendMessageW(g_tabs, TCM_SETCURSEL, at - g_tabIds.begin(), 0);
}
int ShownTabIndex() {
    auto at = std::find(g_tabIds.begin(), g_tabIds.end(), g_tab);
    return at == g_tabIds.end() ? 0 : (int)(at - g_tabIds.begin());
}

// Places the current tab's groups in as many columns as fit, each group in the
// shortest column so far; hides the other tabs' controls.
void LayoutPage() {
    RECT pr; GetClientRect(g_page, &pr);
    const int W = pr.right, m = S(14), gap = S(32), rowH = S(kRowH), headH = S(34), groupGap = S(16);
    struct Span { size_t first, end; };
    std::vector<Span> groups;
    for (size_t i = 0; i < g_items.size(); ++i) {
        if (g_items[i].kind != Kind::Group || !ShownNow(g_items[i])) continue;
        size_t e = i + 1;
        while (e < g_items.size() && g_items[e].kind != Kind::Group && g_items[e].kind != Kind::Tab) ++e;
        groups.push_back({i, e});
    }
    const int fit = (W - 2 * m + gap) / (S(400) + gap);
    const int ncols = std::clamp(fit, 1, std::max(1, std::min(3, (int)groups.size())));
    const int colW = std::max(S(200), std::min(S(640), (W - 2 * m - (ncols - 1) * gap) / ncols));
    const int labelW = std::clamp(colW * 53 / 100, S(150), S(320)), valueW = S(54);

    struct Place { HWND h; int x, y, w, ht; };
    std::vector<Place> places;
    std::vector<int> colY(ncols, m);
    for (const Span& g : groups) {
        const int want = g_items[g.first].col;
        const int c = want >= 0 && want < ncols ? want : (int)(std::min_element(colY.begin(), colY.end()) - colY.begin());
        const int x = m + c * (colW + gap);
        int y = colY[c];
        const Item& head = g_items[g.first];
        places.push_back({head.name, x, y + S(4), colW, S(20)});
        places.push_back({head.line, x, y + S(26), colW, 2});
        y += headH;
        for (size_t i = g.first + 1; i < g.end; ++i) {
            const Item& it = g_items[i];
            if (!(it.modes & PlatformBit()) || it.hidden || it.off) continue;
            if (it.cell >= 0) {   // a row of a table: its cells side by side (cell 0, the label, starts the row and takes its height; a slider shows its number box only)
                const int labelCol = colW * 27 / 100, cw = (colW - labelCol) / 6;
                const int cx = it.cell == 0 ? x : x + labelCol + (it.cell - 1) * cw, w2 = (it.cell == 0 ? labelCol : cw) - S(4);
                const int ry = it.cell == 0 ? y : y - rowH;
                if (it.kind == Kind::Status) places.push_back({it.ctl, cx, ry + S(6), w2, S(20)});
                else if (it.kind == Kind::Slider) places.push_back({it.value, cx, ry + S(5), w2, S(22)});
                else places.push_back({it.ctl, cx + std::max(0, w2 / 2 - S(8)), ry + S(4), S(24), S(24)});
                if (it.cell == 0) y += rowH;
                continue;
            }
            switch (it.kind) {
            case Kind::Radio:   // a row each
                for (size_t k = 0; k < it.radios.size(); ++k)
                    places.push_back({it.radios[k], x, y + S(4) + (int)k * rowH, colW, S(24)});
                y += rowH * ((int)it.radios.size() - 1);
                break;
            case Kind::Status:   // up to two lines
                places.push_back({it.ctl, x, y + S(4), colW, S(40)});
                y += S(46) - rowH;
                break;
            case Kind::Slider: {
                // the render size's box holds "2048 x 2048" and "window's size": wider than a number's
                // (and a narrower label, so the slider keeps a track to drag along)
                const bool render = it.section && wcscmp(it.section, L"@render") == 0;
                const int vw = render ? S(118) : valueW, lw = render ? std::min(labelW, S(172)) : labelW;
                places.push_back({it.name, x, y + S(6), lw - S(8), S(20)});
                places.push_back({it.ctl, x + lw, y + S(2), colW - lw - vw - S(4), S(26)});
                places.push_back({it.value, x + colW - vw + S(4), y + S(6), vw - S(4), S(20)});
                break;
            }
            case Kind::Choice:
                places.push_back({it.name, x, y + S(6), labelW - S(8), S(20)});
                places.push_back({it.ctl, x + labelW, y + S(3), colW - labelW, S(240)});   // the height is the open list's
                break;
            case Kind::File:
            case Kind::Color:
                places.push_back({it.name, x, y + S(6), labelW - S(8), S(20)});
                places.push_back({it.ctl, x + labelW, y + S(4), colW - labelW - S(66), S(23)});
                places.push_back({it.browse, x + colW - S(62), y + S(3), S(30), S(25)});
                places.push_back({it.clear, x + colW - S(28), y + S(3), S(28), S(25)});
                break;
            case Kind::Button:
                if (it.big) {
                    places.push_back({it.ctl, x, y + S(4), colW, S(58)});
                    y += S(66) - rowH;
                    break;
                }
                places.push_back({it.ctl, x, y + S(4), colW, S(24)});
                break;
            default:
                places.push_back({it.ctl, x, y + S(4), colW, S(24)});
                break;
            }
            y += rowH;
        }
        colY[c] = y + groupGap;
    }
    g_contentH = *std::max_element(colY.begin(), colY.end()) - groupGap + m;
    g_viewH = pr.bottom;
    g_scrollY = std::clamp(g_scrollY, 0, std::max(0, g_contentH - g_viewH));
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, std::max(0, g_contentH - 1), (UINT)std::max(0, g_viewH), g_scrollY};
    SetScrollInfo(g_page, SB_VERT, &si, TRUE);

    HDWP dw = BeginDeferWindowPos((int)places.size() + 64);
    for (const Item& it : g_items) {
        if (it.kind == Kind::Tab || ShownNow(it)) continue;
        for (HWND h : {it.name, it.ctl, it.value, it.browse, it.clear, it.line})
            if (h && IsWindowVisible(h)) dw = DeferWindowPos(dw, h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        for (HWND h : it.radios)
            if (IsWindowVisible(h)) dw = DeferWindowPos(dw, h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    }
    for (const Place& p : places)
        if (p.h) dw = DeferWindowPos(dw, p.h, nullptr, p.x, p.y - g_scrollY, p.w, p.ht, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    EndDeferWindowPos(dw);
    InvalidateRect(g_page, nullptr, TRUE);
}

// The footer as tall as its text wraps to, the tabs above it, the page inside the tabs.
void LayoutMain() {
    RECT rc; GetClientRect(g_main, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    const int m = S(8), fx = S(14);
    wchar_t text[512];
    GetWindowTextW(g_footer, text, 512);
    RECT fr{0, 0, rc.right - 2 * fx, 0};
    HDC dc = GetDC(g_footer);
    HGDIOBJ old = SelectObject(dc, g_font);
    DrawTextW(dc, text, -1, &fr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(g_footer, dc);
    const int footerY = rc.bottom - fr.bottom - m;
    MoveWindow(g_footer, fx, footerY, rc.right - 2 * fx, fr.bottom, TRUE);
    RECT tr{m, m, rc.right - m, std::max<LONG>(m + S(60), footerY - m)};
    MoveWindow(g_tabs, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    RECT dr{0, 0, tr.right - tr.left, tr.bottom - tr.top};
    SendMessageW(g_tabs, TCM_ADJUSTRECT, FALSE, (LPARAM)&dr);
    MoveWindow(g_page, tr.left + dr.left, tr.top + dr.top, std::max<LONG>(0, dr.right - dr.left), std::max<LONG>(0, dr.bottom - dr.top), TRUE);
    LayoutPage();
}

BOOL CALLBACK SetFontProc(HWND h, LPARAM font) { SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE); return TRUE; }

// Fonts and sizes for the current DPI and zoom.
void ApplyScale() {
    g_scale = g_dpi / 96.0f * g_zoom;
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, g_dpi);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = (LONG)std::lround(lf.lfHeight * g_zoom);
    HFONT oldFont = g_font, oldBold = g_bold, oldBig = g_big;
    g_font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    g_bold = CreateFontIndirectW(&lf);
    lf.lfHeight = lf.lfHeight * 5 / 3;   // the START button's letters
    g_big = CreateFontIndirectW(&lf);
    SendMessageW(g_tabs, TCM_SETPADDING, 0, MAKELPARAM(S(12), S(4)));   // before the font: the tabs size on WM_SETFONT
    EnumChildWindows(g_main, SetFontProc, (LPARAM)g_font);
    SendMessageW(g_tip, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, S(420));
    for (const Item& it : g_items) {
        if (it.kind == Kind::Group) SendMessageW(it.name, WM_SETFONT, (WPARAM)g_bold, TRUE);
        if (it.kind == Kind::Button && it.big) SendMessageW(it.ctl, WM_SETFONT, (WPARAM)g_big, TRUE);
        if (it.kind == Kind::Slider) SendMessageW(it.ctl, TBM_SETTHUMBLENGTH, S(20), 0);
    }
    if (oldFont) DeleteObject(oldFont);
    if (oldBold) DeleteObject(oldBold);
    if (oldBig) DeleteObject(oldBig);
}

void SetZoom(float z) {
    z = std::clamp(std::round(z * 10.0f) / 10.0f, 0.7f, 2.0f);
    if (z == g_zoom) return;
    g_zoom = z;
    ApplyScale();
    LayoutMain();
}

// t: a place in the strip (wraps round).
void SelectTab(int t) {
    const int n = (int)g_tabIds.size();
    if (n == 0) return;
    t = ((t % n) + n) % n;
    g_tab = g_tabIds[t];
    SendMessageW(g_tabs, TCM_SETCURSEL, t, 0);
    g_scrollY = 0;
    RefreshControls();   // a key shown on two tabs (Performance and its own) changed on the other
    RefreshStrikeCm();
    LayoutPage();
}

void ScrollTo(int y) {
    y = std::clamp(y, 0, std::max(0, g_contentH - g_viewH));
    if (y == g_scrollY) return;
    ScrollWindowEx(g_page, 0, g_scrollY - y, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    g_scrollY = y;
    SCROLLINFO si{sizeof si, SIF_POS | SIF_DISABLENOSCROLL};
    si.nPos = g_scrollY;
    SetScrollInfo(g_page, SB_VERT, &si, TRUE);
}

bool AnyListOpen() {
    for (const Item& it : g_items)
        if (it.kind == Kind::Choice && SendMessageW(it.ctl, CB_GETDROPPEDSTATE, 0, 0)) return true;
    return false;
}

LRESULT ColorStatic(HWND l, HDC dc) {
    for (const Item& it : g_items)   // read-only edits ask here too: a colour row is painted in its colour
        if (it.kind == Kind::Color && it.ctl == l && it.brush) {
            LOGBRUSH lb{}; GetObjectW(it.brush, sizeof lb, &lb);
            const int luma = GetRValue(lb.lbColor) * 3 + GetGValue(lb.lbColor) * 6 + GetBValue(lb.lbColor);
            SetBkColor(dc, lb.lbColor);
            SetTextColor(dc, luma > 1280 ? RGB(0, 0, 0) : RGB(255, 255, 255));
            return (LRESULT)it.brush;
        }
    for (const Item& it : g_items)
        if (it.kind == Kind::Status && it.ctl == l) SetTextColor(dc, it.color);
    if (l == g_footer) SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    SetBkMode(dc, TRANSPARENT);
    return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
}

// The weapon kind changed: the "@weapon" sliders show that kind's values.
void ShowWeapon(int type) {
    if (type == g_weaponType) return;
    SetWeaponSection(type);
    for (Item& it : g_items) {
        if (!it.key) continue;
        if (wcscmp(it.section, L"@weapon") == 0 && it.kind == Kind::Slider) {
            const float v = ReadValue(it);
            SendMessageW(it.ctl, TBM_SETPOS, TRUE, ToPos(it, v));
            ShowValue(it, v);
        }
        if (wcscmp(it.section, L"@ui") == 0 && wcscmp(it.key, L"weapon") == 0) SendMessageW(it.ctl, CB_SETCURSEL, g_weaponType - 1, 0);
    }
}

// What the hero holds, from the running game (vrcam's gamestate block), while
// "Follow the weapon in hand" is ticked.
void FollowWeapon() {
    bool follow = true;
    for (const Item& it : g_items)
        if (it.key && wcscmp(it.section, L"@ui") == 0 && wcscmp(it.key, L"weapon_follow") == 0) follow = ReadValue(it) != 0.0f;
    if (!follow) return;
    // A frozen counter = that game is gone. Our view outlives it, but the name
    // dies with the game's handle: a restarted game makes a new block, and this
    // window kept showing the old one's last weapon. Opened afresh then.
    static const D2RVR_State* state = nullptr;
    static uint32_t lastCounter = 0;
    static ULONGLONG movedAt = 0;
    if (state && GetTickCount64() - movedAt > 2000) { UnmapViewOfFile(state); state = nullptr; }
    if (!state) {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 1000) return;
        lastTry = GetTickCount64();
        if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, D2RVR_STATE_NAME)) {
            state = (const D2RVR_State*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(D2RVR_State));
            CloseHandle(m);
        }
        if (!state) return;
        lastCounter = state->counter;
        movedAt = GetTickCount64();
    }
    if (state->counter != lastCounter) { lastCounter = state->counter; movedAt = GetTickCount64(); }
    if (state->version == D2RVR_STATE_VERSION) ShowWeapon((int)state->weaponType);
}

// ---------------------------------------------------------------------------
// The Home page.

void ApplyFrameCapForPlatform();
void PlatformChanged() {
    ApplyWindowForPlatform();   // the right window for the next start: flat gets its own back, VR its square (if ticked)
    ApplyFrameCapForPlatform();   // ... and the right frame cap: flat its own, VR the headset's rate
    ApplySafeScreenForPlatform();   // ... and the right interface size: flat its own, VR 80 where the player chose none
    RebuildTabs();
    g_scrollY = 0;
    LayoutPage();
    UpdateEnabled();
}

// The radios as the ini has them now: F1 - F4 in the game write the view there.
void RefreshRadios() {
    for (Item& it : g_items) {
        if (it.kind != Kind::Radio) continue;
        const int v = (int)std::lround(ReadValue(it));
        for (size_t k = 0; k < it.radios.size(); ++k) {
            const bool want = v == (int)it.min + (int)k;
            if ((SendMessageW(it.radios[k], BM_GETCHECK, 0, 0) == BST_CHECKED) != want)
                SendMessageW(it.radios[k], BM_SETCHECK, want ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        if (wcscmp(it.key, L"platform") == 0 && v != g_platform) { g_platform = v; PlatformChanged(); }
    }
}

std::wstring BodyWalkDir() {
    wchar_t p[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH);
    return n && n < MAX_PATH - 20 ? std::wstring(p) + L"\\BodyWalkVR" : std::wstring();
}
bool Exists(const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ProcessRunning(const wchar_t* exe) {
    bool found = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

// The first `size` bytes of another process's named block, if it is there.
bool ReadBlock(const wchar_t* name, void* out, size_t size) {
    HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!m) return false;
    const void* v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, size);
    const bool ok = v != nullptr;
    if (ok) { memcpy(out, v, size); UnmapViewOfFile(v); }
    CloseHandle(m);
    return ok;
}
// Its writer is alive: the counter moved within the last few seconds.
bool Moving(const wchar_t* name, uint32_t counter) {
    struct Seen { uint32_t counter; ULONGLONG at; };
    static std::map<std::wstring, Seen> seen;
    const ULONGLONG now = GetTickCount64();
    auto it = seen.find(name);
    if (it == seen.end()) { seen[name] = {counter, 0}; return false; }
    if (it->second.counter != counter) { it->second = {counter, now}; }
    return it->second.at != 0 && now - it->second.at < 3000;
}

// "key": true / false in BodyWalk's settings; -1 when the key is not there.
int JsonBool(const std::string& text, const char* key) {
    std::smatch m;
    if (!std::regex_search(text, m, std::regex(std::string("\"") + key + "\"\\s*:\\s*(true|false)"))) return -1;
    return m[1].str() == "true" ? 1 : 0;
}
// BodyWalk's "disabled_plugins" list mentions this name.
bool PluginOff(const std::string& text, const char* part) {
    std::smatch m;
    if (!std::regex_search(text, m, std::regex("\"disabled_plugins\"\\s*:\\s*\\[([^\\]]*)\\]"))) return false;
    return m[1].str().find(part) != std::string::npos;
}

enum { kOk, kBad, kWarn, kUnknown };
void SetStatus(const wchar_t* key, int state, const wchar_t* text) {
    static const COLORREF kColor[] = {RGB(0, 140, 60), RGB(200, 40, 40), RGB(200, 120, 0), 0};
    for (Item& it : g_items) {
        if (it.kind != Kind::Status || wcscmp(it.key, key) != 0) continue;
        const COLORREF c = state == kUnknown ? GetSysColor(COLOR_GRAYTEXT) : kColor[state];
        std::wstring line = L"\u25CF  ";
        line += text;
        wchar_t now[512];
        GetWindowTextW(it.ctl, now, 512);
        if (line != now || c != it.color) {
            it.color = c;
            SetWindowTextW(it.ctl, line.c_str());
            InvalidateRect(it.ctl, nullptr, TRUE);
        }
    }
}

// What the headset's runtime said at the last session the game made (vrcam writes d2r_vr_headset.ini beside d2r_vr.ini when a session
// starts): the recommended size per eye and the most a swapchain may be. Missing until the game has run a headset session once.
struct HeadsetInfo {
    int recW = 0, recH = 0, maxW = 0, maxH = 0;
    wchar_t name[96] = L"";
    bool known() const { return recW > 0 && recH > 0; }
};
HeadsetInfo ReadHeadsetInfo() {
    HeadsetInfo h;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_ini);
    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
    wcscat_s(path, L"d2r_vr_headset.ini");
    h.recW = (int)GetPrivateProfileIntW(L"headset", L"recommended_w", 0, path);
    h.recH = (int)GetPrivateProfileIntW(L"headset", L"recommended_h", 0, path);
    h.maxW = (int)GetPrivateProfileIntW(L"headset", L"max_w", 0, path);
    h.maxH = (int)GetPrivateProfileIntW(L"headset", L"max_h", 0, path);
    GetPrivateProfileStringW(L"headset", L"name", L"", h.name, 96, path);
    return h;
}

// The render size's tips and its status line: what the size costs against 2048 x 2048, what the headset recommends and holds.
void UpdateResolutionTexts() {
    const int mode = (int)GetPrivateProfileIntW(L"render", L"vr_resolution", 0, g_ini);
    const int px = (int)GetPrivateProfileIntW(L"render", L"vr_resolution_px", 0, g_ini);
    const rendersize::Pick pick = rendersize::FromIni(mode, px);
    const HeadsetInfo hs = ReadHeadsetInfo();
    auto cost = [](double w, double h) { wchar_t b[24]; swprintf_s(b, L"%.2f", rendersize::CostVs2048(w, h)); return std::wstring(b); };
    std::wstring rec;   // "2496 x 2688 (Meta Quest 3)"
    if (hs.known()) {
        rec = std::to_wstring(hs.recW) + L" x " + std::to_wstring(hs.recH);
        if (hs.name[0]) rec += std::wstring(L" (") + hs.name + L")";
    }
    const int most = hs.maxW > 0 && hs.maxH > 0 ? std::min(hs.maxW, hs.maxH) : 0;   // a square of this side fits the headset's limits

    std::wstring tip = L"The size of the game's picture in the headset: a square, its side in pixels, whatever size the game window is - "
                       L"the window keeps its size. Drag to any size from 1024 to 4096 (steps of 64); the left end, \"window's size\", is "
                       L"Desktop: the picture is the window's, as before.\n\nBigger is sharper and dearer, by the pixels - against 2048 x 2048:";
    for (const int side : {1024, 1536, 2048, 2560, 3072, 3584, 4096}) tip += L" " + std::to_wstring(side) + L" = " + cost(side, side) + L" x" + (side == 4096 ? L"." : L",");
    tip += L"\n\n";
    if (hs.known()) {
        tip += L"Your headset " + (hs.name[0] ? std::wstring(L"(") + hs.name + L") " : std::wstring()) + L"recommends " + std::to_wstring(hs.recW) + L" x " +
               std::to_wstring(hs.recH) + L" per eye";
        if (most > 0) tip += L" and takes up to " + std::to_wstring(hs.maxW) + L" x " + std::to_wstring(hs.maxH) + L"; a size past that is shrunk to it";
        tip += L". (Read at the last game start with a headset.)";
    } else {
        tip += L"What your headset recommends is read when the game first starts a headset session; it will be here then.";
    }
    tip += L"\n\nThe game is asked about a second after you stop dragging, and only once the hero has stood in a world; the game's picture is "
           L"rebuilt and ReShade builds its effects again (a pause of a few seconds). The window's size comes back at the left end, when the "
           L"headset session ends and when the headset has been off for half a minute. A size the headset refuses falls back to the last "
           L"one that worked. Native OpenXR only; applies while the game runs. With the box below ticked this slider is out of reach.";
    wcsncpy_s(g_resTip, _countof(g_resTip), tip.c_str(), _TRUNCATE);

    std::wstring tipH = L"Take the size the headset's runtime recommends per eye, in its own shape (not always square), instead of the "
                        L"slider's. ";
    if (hs.known()) tipH += L"For your headset: " + rec + L", " + cost(hs.recW, hs.recH) + L" x the pixels of 2048 x 2048. ";
    else tipH += L"It is read when the game first starts a headset session. ";
    tipH += L"Unticked: the slider's size (or the window's).";
    wcsncpy_s(g_resTipHeadset, _countof(g_resTipHeadset), tipH.c_str(), _TRUNCATE);

    std::wstring line;
    int state = kUnknown;
    if (pick.kind == rendersize::Kind::Headset) {
        if (hs.known()) { line = L"Resolution: the headset's recommendation, " + rec + L" - " + cost(hs.recW, hs.recH) + L" x the pixels of 2048 x 2048."; state = kOk; }
        else { line = L"Resolution: the headset's recommendation - not known yet (read when the game first starts a headset session); the window's size until then."; state = kWarn; }
    } else if (pick.kind == rendersize::Kind::Square) {
        line = L"Resolution: " + std::to_wstring(pick.side) + L" x " + std::to_wstring(pick.side) + L" - " + cost(pick.side, pick.side) +
               L" x the pixels of 2048 x 2048.";
        state = kOk;
        if (most > 0 && pick.side > most) { line += L" Your headset takes " + std::to_wstring(most) + L" at most: shrunk to that."; state = kWarn; }
        else if (hs.known()) line += L" Your headset recommends " + std::to_wstring(hs.recW) + L" x " + std::to_wstring(hs.recH) + L".";
    } else {
        line = L"Resolution: the game window's own size (the left end).";
        if (hs.known()) line += L" Your headset recommends " + std::to_wstring(hs.recW) + L" x " + std::to_wstring(hs.recH) + L".";
    }
    SetStatus(L"vr_resolution", state, line.c_str());
}

// The "Native OpenXR" row: why the game has no headset session (vrcam writes [session] note into d2r_vr_headset.ini beside d2r_vr.ini - FlatVR holds the headset and nothing
// can stop it); gone with the note.
void UpdateSessionNote() {
    static bool shown = false;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_ini);
    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
    wcscat_s(path, L"d2r_vr_headset.ini");
    wchar_t note[256] = L"";
    GetPrivateProfileStringW(L"session", L"note", L"", note, 256, path);
    if (note[0]) {
        SetStatus(L"native_xr", kBad, (std::wstring(L"No headset session: ") + note).c_str());
        shown = true;
    } else if (shown) {
        shown = false;
        SetStatus(L"native_xr", kUnknown, L"Native OpenXR (the first 3D choice above) needs a BodyWalk that takes the game's headset (1.78 or later) or \"Controls without BodyWalk\"");
    }
}

// World > Fog, "Depth for the effects": the buffer vrcam picked, its size and when and why it was last picked (d2r_vr_depth.ini
// beside d2r_vr.ini, written at each pick), or that ReShade's own depth is in use.
void UpdateDepthStatus() {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_ini);
    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
    wcscat_s(path, L"d2r_vr_depth.ini");
    std::wstring line;
    int state = kUnknown;
    if (!GetPrivateProfileIntW(L"render", L"own_depth", 1, g_ini)) {
        line = L"Depth: ReShade's own (Generic Depth's pick).";
    } else {
        wchar_t buf[64] = L"", at[16] = L"", why[96] = L"";
        GetPrivateProfileStringW(L"depth", L"buffer", L"", buf, 64, path);
        GetPrivateProfileStringW(L"depth", L"picked_at", L"", at, 16, path);
        GetPrivateProfileStringW(L"depth", L"why", L"", why, 96, path);
        const int w = (int)GetPrivateProfileIntW(L"depth", L"width", 0, path), h = (int)GetPrivateProfileIntW(L"depth", L"height", 0, path);
        const int picks = (int)GetPrivateProfileIntW(L"depth", L"picks", 0, path), others = (int)GetPrivateProfileIntW(L"depth", L"other_buffers", 0, path);
        if (!buf[0]) {
            line = L"Depth: our own - nothing picked yet (the game in VR with a square window or a render size picks it).";
        } else {
            line = L"Depth: our own, buffer " + std::wstring(buf) + L", " + std::to_wstring(w) + L" x " + std::to_wstring(h) +
                   L"; last picked at " + at + L" (" + why + L"); picks " + std::to_wstring(picks) + L", another buffer " +
                   std::to_wstring(others) + L" times.";
            state = kOk;
        }
    }
    SetStatus(L"depth_pick", state, line.c_str());
}

// Weapon weight, "Move the weapon with the weight-physics library": whether d2r_vr_weight.dll is beside the plugin, and what the tick does with or without it. The tick is greyed
// out (UpdateEnabled) the moment the file appears or goes, also while the program is open.
void UpdateEnabled();
void UpdateWeightLibStatus() {
    static int last = -1;
    const bool there = WeightLibFound();
    std::wstring line;
    int state;
    if (there) {
        line = L"Weight-physics library: d2r_vr_weight.dll is in d2rloader\\d2r_vr_libs - the tick below moves the weapon with it.";
        state = kOk;
    } else {
        line = L"Weight-physics library: not found in d2rloader\\d2r_vr_libs (d2r_vr_weight.dll) - the mod keeps its own weight physics, the tick below is unavailable.";
        state = kWarn;
    }
    SetStatus(L"weight_lib", state, line.c_str());
    if (last != (int)there) {
        last = (int)there;
        UpdateEnabled();
    }
}

// Weapon weight, "Hold the weapon the grip library's way": the same for d2r_vr_grip.dll.
void UpdateGripLibStatus() {
    static int last = -1;
    const bool there = GripLibFound();
    std::wstring line;
    int state;
    if (there) {
        line = L"Grip library: d2r_vr_grip.dll is in d2rloader\\d2r_vr_libs - the tick below holds the weapon its way.";
        state = kOk;
    } else {
        line = L"Grip library: not found in d2rloader\\d2r_vr_libs (d2r_vr_grip.dll) - the mod keeps its own way of holding, the tick below is unavailable.";
        state = kWarn;
    }
    SetStatus(L"grip_lib", state, line.c_str());
    if (last != (int)there) {
        last = (int)there;
        UpdateEnabled();
    }
}

// The Weapon Adjust tab's list: each kind, green with its numbers where
// [weapon_<kind>] moves or turns the weapon at all, grey where it is untouched;
// the kind the sliders edit now is marked.
void RefreshWeaponList() {
    for (int t = D2RVR_TYPE_UNARMED; t < D2RVR_TYPE_COUNT; ++t) {
        std::wstring sec = L"weapon_";
        for (const char* c = kD2RVRWeaponTypeNames[t]; *c; ++c) sec += (wchar_t)tolower((unsigned char)*c);
        static const wchar_t* const kKeys[6] = {L"x", L"y", L"z", L"pitch", L"yaw", L"roll"};
        float v[6];
        bool moved = false;
        for (int k = 0; k < 6; ++k) {
            wchar_t b[64];
            GetPrivateProfileStringW(sec.c_str(), kKeys[k], L"0", b, 64, g_ini);
            v[k] = wcstof(b, nullptr);
            moved = moved || fabsf(v[k]) > 1e-4f;
        }
        wchar_t name[32];
        swprintf_s(name, L"%hs", kD2RVRWeaponTypeNames[t]);
        wchar_t text[200];
        if (moved)
            swprintf_s(text, L"%s - adjusted: %.1f %.1f %.1f cm, %.0f %.0f %.0f\u00B0%s", name, v[0], v[1], v[2], v[3], v[4], v[5],
                       t == g_weaponType ? L"   \u25C0 editing" : L"");
        else
            swprintf_s(text, L"%s - not adjusted%s", name, t == g_weaponType ? L"   \u25C0 editing" : L"");
        wchar_t key[8];
        swprintf_s(key, L"wk%d", t);
        SetStatus(key, moved ? kOk : kUnknown, text);
    }
}

// The BodyWalk D2R VR needs: 1.79 and on (0.163: its strike gestures measure the hands in the
// room - on 1.78 a head turn or a step attacked, 2026-10-12; 0.156: the belt's zones in the headset and the
// Mapping on the game's own headset, host API 9/10, FlatVR's late pictures left out; 1.77
// for FlatVR's frame conveyor and the add-on's present stamp - with 1.76 and the add-on
// 2.19 a player's eyes went out of line after ~10 s, 2026-10-10). Setup holds the same minimum.
constexpr int kMinBodyWalkMajor = 1, kMinBodyWalkMinor = 79;

// An exe's own version block as major * 1000 + minor (1.77 -> 1077), 0 when it has none.
int ExeVersion(const std::wstring& exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (!size) return 0;
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", (void**)&fi, &len) || !fi)
        return 0;
    return (int)HIWORD(fi->dwFileVersionMS) * 1000 + (int)LOWORD(fi->dwFileVersionMS);
}
bool NewEnough(int version) { return version >= kMinBodyWalkMajor * 1000 + kMinBodyWalkMinor; }
std::wstring VersionText(int version) {
    if (!version) return L"?";
    wchar_t b[32];
    swprintf_s(b, L"%d.%d", version / 1000, version % 1000);
    return b;
}

// BodyWalkVR.exe on this PC, "" when there is none: the newest of the portable copy
// D2R VR Setup puts in the game's folder (next to this program), an installer's and
// Steam's (the uninstall entries the Setup looks for). The newest, not the first: an
// old Steam BodyWalk beside the portable 1.77 was started and the game crashed.
std::wstring BodyWalkExe() {
    std::vector<std::wstring> found;
    wchar_t self[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    while (n && self[n - 1] != L'\\') --n;
    // D2R VR as the D2RLoader mod D2R-VR keeps its BodyWalk Portable in mods\D2R-VR\BodyWalkVR; an earlier install's is beside this program
    const std::wstring inMod = ModInstall() ? modlayout::ModRoot(GameFolder()) + L"BodyWalkVR\\BodyWalkVR.exe" : std::wstring();
    if (!inMod.empty() && Exists(inMod)) found.push_back(inMod);
    const std::wstring portable = std::wstring(self, n) + L"BodyWalkVR\\BodyWalkVR.exe";
    if (Exists(portable)) found.push_back(portable);
    for (const wchar_t* key : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{5E9E3C51-4043-4245-8B24-817C647DE553}_is1",
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BodyWalkVR_is1",
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 4711120"})
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
            for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
                wchar_t dir[MAX_PATH] = {};
                DWORD size = sizeof dir;
                if (RegGetValueW(root, key, L"InstallLocation", RRF_RT_REG_SZ | (view == KEY_WOW64_64KEY ? RRF_SUBKEY_WOW6464KEY : RRF_SUBKEY_WOW6432KEY),
                                 nullptr, dir, &size) != ERROR_SUCCESS || !dir[0])
                    continue;
                std::wstring exe(dir);
                if (exe.back() != L'\\') exe += L'\\';
                exe += L"BodyWalkVR.exe";
                if (Exists(exe) && std::find(found.begin(), found.end(), exe) == found.end()) found.push_back(exe);
            }
    std::wstring best;
    int bestVersion = -1;
    for (const std::wstring& exe : found)   // ties: the earlier one (the portable copy first)
        if (const int v = ExeVersion(exe); v > bestVersion) { best = exe; bestVersion = v; }
    return best;
}

// The running BodyWalkVR.exe's path, "" when none runs (or it cannot be read).
std::wstring RunningBodyWalkExe() {
    std::wstring path;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return path;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && path.empty(); ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"BodyWalkVR.exe") != 0) continue;
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID)) {
            wchar_t buf[MAX_PATH];
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, buf, &len)) path.assign(buf, len);
            CloseHandle(h);
        }
    }
    CloseHandle(snap);
    return path;
}

Item* FindItem(const wchar_t* section, const wchar_t* key);
void StartBodyWalk();
// FlatVR started by Launch once BodyWalk's Game Link is up (FollowLaunch): until this tick, 0 = not wanted.
ULONGLONG g_flatVrStartBy = 0;
// `--start`: the START button without the window (StartHeadless below). Nothing may open a message box then: it says
// what it has to say on stdout and in the launch log (StartSay).
bool g_headless = false;
void StartSay(const std::wstring& line);
// The game under D2RLoader, from its own folder (this program sits beside D2R.exe). For VR,
// BodyWalk with it - the head, the buttons and the gestures go through its Game Link, and
// nobody should have to know to start it first (2026-10-10) - and FlatVR once Game Link is
// up, unless the game holds the headset itself (native OpenXR: it stops FlatVR anyway).
// 0 started, 3 D2RLoader.exe could not be started (the window ignores the answer; `--start` returns it).
int LaunchGame() {
    ApplyWindowForPlatform();   // START in flat mode never meets VR's square (and in VR the square, if ticked)
    if (g_platform != 1) RestoreFrameCapForFlat();   // ... nor VR's frame cap (in VR START changes nothing: the rate was written when it was chosen)
    ApplySafeScreenForPlatform();   // ... nor VR's interface size (and in VR the game starts at 80 where the player chose none; the game is not running yet)
    const std::wstring game = GameFolder();
    const std::wstring exe = game + L"D2RLoader.exe";
    // installed as the D2RLoader mod D2R-VR: D2RLoader is told to run that mod (its plugins load only then)
    const std::wstring args = ModInstall() ? modlayout::LaunchArguments() : std::wstring();
    {   // (ShellExecuteEx: `--start` must not meet an error box of the shell's - a D2RLoader.exe that is not a program says so by the exit code)
        SHELLEXECUTEINFOW sei{sizeof sei};
        sei.fMask = SEE_MASK_NOASYNC | (g_headless ? SEE_MASK_FLAG_NO_UI : 0);
        sei.hwnd = g_main;
        sei.lpVerb = L"open";
        sei.lpFile = exe.c_str();
        sei.lpParameters = args.empty() ? nullptr : args.c_str();
        sei.lpDirectory = game.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) return 3;
    }
    if (g_platform != 1) return 0;
    const bool native = GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0;
    const bool noBodyWalk = native && GetPrivateProfileIntW(L"openxr", L"direct_input", 0, g_ini) != 0;   // native with its own controls: BodyWalk is not needed
    if (!noBodyWalk && RunningBodyWalkExe().empty()) StartBodyWalk();
    g_flatVrStartBy = native ? 0 : GetTickCount64() + 60000;
    return 0;
}
// Every timer tick: FlatVR's Start asked once Game Link is up (BodyWalk loaded with it),
// if FlatVR is not running already; given up after a minute.
void FollowLaunch() {
    if (!g_flatVrStartBy) return;
    if (GetTickCount64() > g_flatVrStartBy) { g_flatVrStartBy = 0; return; }
    uint32_t caps = 0;
    if (!LinkHost(&caps) || !(caps & BW_LINK_CAP_FLATVR_RUNNING)) return;
    FlatVRHeadSample hs{};
    if (!(ReadBlock(FLATVR_HEAD_SAMPLE_NAME, &hs, sizeof hs) && Moving(FLATVR_HEAD_SAMPLE_NAME, hs.counter)))
        if (BW_LinkRequests* r = LinkRequests()) bw::link::Request(r, &r->flatvrRunning, 1);
    g_flatVrStartBy = 0;
}
// Shown while D2RLoader.exe is there and the game is not running.
void LaunchButton() {
    Item* b = FindItem(L"@run", L"d2r_launch");
    if (!b) return;
    const bool hide = !Exists(GameFolder() + L"D2RLoader.exe");
    const bool running = ProcessRunning(L"D2RLoader.exe") || ProcessRunning(L"D2R.exe");
    if (b->hidden != hide) { b->hidden = hide; LayoutPage(); }
    static int shown = -1;   // (only on a change: a button redrawn twice a second flickers)
    if (b->ctl && shown != (running ? 1 : 0)) {   // while the game runs the big button stays, greyed, and says so
        shown = running ? 1 : 0;
        EnableWindow(b->ctl, running ? FALSE : TRUE);
        SetWindowTextW(b->ctl, running ? L"The game is running" : L"START");
    }
}

void StartBodyWalk() {
    const std::wstring exe = BodyWalkExe();
    if (exe.empty()) return;
    // Never one older than D2R VR needs (an old Steam BodyWalk with no newer copy beside it).
    if (const int v = ExeVersion(exe); !NewEnough(v)) {
        const std::wstring t = L"BodyWalk " + VersionText(v) + L" (" + exe + L") is older than D2R VR needs (" +
                               VersionText(kMinBodyWalkMajor * 1000 + kMinBodyWalkMinor) +
                               L").\n\nRun D2R VR Setup again: it puts BodyWalk Portable in the game's folder, and this button starts that one.";
        if (g_headless) StartSay(L"warning: " + t.substr(0, t.find(L"\n\n")));   // started without BodyWalk; the game still starts
        else MessageBoxW(g_main, t.c_str(), L"D2R VR", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    ShellExecuteW(g_main, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

Item* FindItem(const wchar_t* section, const wchar_t* key);

// BodyWalk Game Link (bodywalk/game_link.h): Start / Stop FlatVR, FlatVR's 3D source and who
// holds the headset go as Requests - BodyWalk does them on its next frame, only those made
// while it runs. Made, or opened, once and kept: a request lives in the block.
BW_LinkRequests* LinkRequests() {
    static HANDLE map = nullptr;
    static BW_LinkRequests* r = nullptr;
    if (!r) r = (BW_LinkRequests*)bw::link::Open(BW_LINK_REQUESTS_NAME, &map);
    return r;
}
// BodyWalk runs with Game Link: its Host block moves and its process is there; caps = what
// that BodyWalk can do (BW_LINK_CAP_*).
bool LinkHost(uint32_t* caps) {
    static HANDLE map = nullptr;
    static const BW_LinkHost* h = nullptr;
    static bw::link::Pulse pulse;
    if (caps) *caps = 0;
    if (!h) h = (const BW_LinkHost*)bw::link::Open(BW_LINK_HOST_NAME, &map);
    if (!h || !pulse.Alive(&h->header, 2000)) return false;
    BW_LinkHost c;
    if (caps && bw::link::Read(h, &c)) *caps = c.caps;
    return true;
}

// The VR status column: what BodyWalk, its Game Link, FlatVR and the game
// say right now. BodyWalk's own settings file, the plugin on disk, and the
// three shared blocks each side writes while it runs.
void D2RLoaderStatus();   // D2RLoader's row and its button (below, by the update check)
extern std::atomic<bool> g_rsBusy;   // Install ReShade (below)
extern std::wstring g_rsError;

void LaunchButton();
std::wstring Widen(const std::string& s);   // UTF-8 to UTF-16 (below)
// Without BodyWalk (native OpenXR with its own controls) the BodyWalk and FlatVR rows of the Status block tell the player nothing
// he can use - and show red - so they are left out (2026-10-14, the user's wish); back as soon as one of the two is off.
void BodyWalkRows() {
    static const wchar_t* const kRows[] = {L"bw_installed", L"vigem", L"bw_running", L"game_link", L"universal", L"bw_output",
                                           L"flatvr_on", L"head_lock", L"frame_gen", L"flatvr_running", L"head", L"bw_path"};
    const bool none = GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0 && GetPrivateProfileIntW(L"openxr", L"direct_input", 0, g_ini) != 0;
    bool changed = false;
    for (const wchar_t* k : kRows)
        if (Item* it = FindItem(L"@status", k); it && it->hidden != none) { it->hidden = none; changed = true; }
    if (changed) LayoutPage();
}
// The eye distance beside its box and slider (Stereo > Native OpenXR): what the headset's runtime measured - the game writes [headset] ipd_mm to
// d2r_vr_headset.ini at a headset session (vr/xr.cpp NoteMeasuredIpd) - and whose number is in use: the headset's or the player's.
void UpdateIpdStatus() {
    if (g_platform != 1) return;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_ini);
    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
    wcscat_s(path, L"d2r_vr_headset.ini");
    wchar_t b[32] = L"", m[32] = L"";
    GetPrivateProfileStringW(L"headset", L"ipd_mm", L"", b, 32, path);
    GetPrivateProfileStringW(L"openxr", L"ipd_mm", L"63", m, 32, g_ini);
    const bool known = b[0] != 0;
    const float measured = known ? wcstof(b, nullptr) : 0.0f;
    float manual = wcstof(m, nullptr);
    if (!std::isfinite(manual)) manual = 63.0f;
    manual = std::clamp(manual, 40.0f, 90.0f);
    const bool autoOn = GetPrivateProfileIntW(L"openxr", L"ipd_auto", 1, g_ini) != 0;
    wchar_t line[320];
    int state = 3;   // grey: nothing to say yet
    if (!known) {
        swprintf_s(line, L"The headset's eye distance is read when the game starts a headset session; not known yet.%s", autoOn ? L"" : L" Yours is in use.");
    } else if (measured < 20.0f) {
        swprintf_s(line, L"The headset reports no distance between the eyes (it puts both in one place). %s", autoOn ? L"64 mm is used: tick off \"from the headset\" and set yours." : L"Yours is in use.");
        state = autoOn ? 2 : 0;
    } else if (autoOn) {
        swprintf_s(line, L"The headset reports %.1f mm between the eyes (read at the game's last headset session). In use: the headset's.", measured);
        state = 0;
    } else {
        swprintf_s(line, L"The headset reports %.1f mm between the eyes (read at the game's last headset session). In use: yours, %.1f mm.", measured, manual);
        state = 0;
    }
    SetStatus(L"ipd_measured", state, line);
}
void RefreshStatus() {
    LaunchButton();   // flat and VR alike
    BodyWalkRows();
    UpdateDepthStatus();   // World > Fog: the depth our effects read (any tab: it is a no-op without its row)
    UpdateSafeScreenStatus();   // UI: F4 body: do two side panels fit the picture at the interface size in the file
    UpdateIpdStatus();          // Stereo > Native OpenXR: the eye distance the headset reports and the one in use
    UpdateWeightLibStatus();   // Weapon weight: the weight-physics library beside the plugin
    UpdateGripLibStatus();     // ... and the grip library
    if (g_platform != 1 || g_tab != 0) return;
    UpdateResolutionTexts();   // the render size's status line and tips (the game may have written the headset's numbers meanwhile)
    UpdateSessionNote();       // ... and why there is no headset session, if the game said so (FlatVR holds the headset)
    UpdateFrameCapStatus();    // the frame cap in the file and the one VR writes for this choice
    D2RLoaderStatus();
    const std::wstring dir = BodyWalkDir();
    // Standalone or Steam build: whichever settings file was written last.
    std::wstring cfg;
    FILETIME newest{};
    for (const wchar_t* name : {L"\\usersettings.json", L"\\usersettings_steam.json"}) {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        const std::wstring p = dir + name;
        if (!dir.empty() && GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa) && CompareFileTime(&fa.ftLastWriteTime, &newest) > 0) {
            newest = fa.ftLastWriteTime;
            cfg = p;
        }
    }
    // ReShade, and D2R VR's files in it. Two ways, by what is next to D2R.exe (shared/reshade_as.h, shared/reshade_paths.h):
    //  - no dxgi.dll: D2R VR's own ReShade64.dll in mods\D2R-VR\reshade, which vrcam loads itself (the plain D2R.exe refuses a dxgi.dll), with the mod's effect;
    //  - a dxgi.dll (the player's own ReShade): it is used as it is, and found D2R VR's effect and pictures through two search paths in its ReShade.ini.
    // And FlatVR's depth add-on, which sits with the rest in the mod's reshade\ (ReShade loads it from there, or vrcam does for a dxgi.dll ReShade).
    {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        while (n && exe[n - 1] != L'\\') --n;
        const std::wstring root(exe, n);
        const std::wstring own = ReShadeFolder();     // where D2R VR's own ReShade64.dll is: the mod's reshade\ once it has one, else the game's folder
        const std::wstring files = ReShadeFiles();    // where D2R VR's effect, pictures and add-on are: the mod's reshade\ (the game's folder before the mod layout)
        const bool r64 = Exists(own + L"ReShade64.dll"), dx = Exists(root + L"dxgi.dll");
        const bool mod = ModInstall();
        const bool fx = Exists(files + L"reshade-shaders\\Shaders\\D2R_DepthFog.fx");
        const bool pathsIn = dx && mod && ReShadePathsCheck() == 0;
        if (dx && !fx)
            SetStatus(L"reshade", kBad, L"ReShade is there (dxgi.dll), the mod's effect D2R_DepthFog.fx is not - run D2R VR Setup again");
        else if (dx && mod && !pathsIn)
            SetStatus(L"reshade", kWarn, L"Your ReShade (dxgi.dll next to D2R.exe) is used as it is, but its ReShade.ini does not list D2R VR's folders yet "
                                         L"(mods\\D2R-VR\\reshade): no fog, sky or ceiling through it. Press \"Add D2R VR's folders to my ReShade\" (game closed)");
        else if (dx && mod)
            SetStatus(L"reshade", kOk, L"Your ReShade (dxgi.dll next to D2R.exe) is used as it is; its search paths reach D2R VR's effect and pictures in mods\\D2R-VR\\reshade");
        else if (dx)
            SetStatus(L"reshade", kOk, L"ReShade as dxgi.dll: the game loads it itself, D2R VR works with it");
        else if (!r64) {
            if (!g_rsBusy.load())
                SetStatus(L"reshade", kBad, g_rsError.empty() ? L"ReShade not installed - press Install ReShade" : g_rsError.c_str());
        }
        else if (!fx)
            SetStatus(L"reshade", kBad, L"ReShade is there, the mod's effect D2R_DepthFog.fx is not - run D2R VR Setup again");
        else
            SetStatus(L"reshade", kOk, mod ? L"D2R VR's own ReShade and the mod's effect installed (mods\\D2R-VR\\reshade)" : L"ReShade and the mod's effect installed");
        // which buttons, by the files there (shared/reshade_as.h, tested)
        const reshadeas::Buttons show = reshadeas::ButtonsShown(ReShadeWhere(), pathsIn);
        for (const auto& [key, want] : {std::pair<const wchar_t*, bool>{L"reshade_get", show.install}, {L"reshade_paths", show.paths},
                                        {L"reshade_aside", show.aside}, {L"reshade_back", show.back}})
            if (Item* b = FindItem(L"@run", key)) {
                const bool hide = !want;
                if (b->hidden != hide) { b->hidden = hide; LayoutPage(); }
            }
        // the note: only while there is a dxgi.dll of the player's to say something about
        SetStatus(L"reshade_note", kUnknown,
                  mod ? L"D2R VR does not rename, move or delete your dxgi.dll. To give D2R VR a ReShade of its own instead, put yours aside (the button above)."
                      : L"A ReShade you have as dxgi.dll is used as it is.");
        if (Item* nt = FindItem(L"@status", L"reshade_note")) {
            const bool hide = !dx;
            if (nt->hidden != hide) { nt->hidden = hide; LayoutPage(); }
        }
        const bool addon = Exists(files + L"FlatVR_DepthProvider.addon64");
        SetStatus(L"depth_addon", addon ? kOk : kBad, addon ? L"FlatVR depth add-on installed"
                                                            : L"FlatVR depth add-on missing from the mod's reshade folder - run D2R VR Setup again");
    }

    // The game's own video settings (Saved Games\Diablo II Resurrected\Settings.json,
    // written by the game when its options close). The mod draws the eyes in turn:
    // TAA builds a frame from the ones before it and mixes the eyes (DLSS no longer:
    // each eye gets a DLSS of its own and its own motion, 0.145);
    // VSync halves the frames each eye gets. "Anti Aliasing": 1 is
    // FXAA, 2 TAA (as the game's own menu shows them, 2026-10-06).
    {
        std::string json;
        const std::wstring path = GameSettingsPath();
        const bool read = !path.empty() && ReadFileText(path, &json);
        auto value = [&](const char* key) {
            std::smatch m;
            return std::regex_search(json, m, std::regex(std::string("\"") + key + "\"\\s*:\\s*(-?\\d+)")) ? std::stoi(m[1].str()) : -1;
        };
        if (!read) {
            SetStatus(L"game_video", kUnknown, L"The game's video settings: not found yet (start the game once)");
        } else {
            std::wstring bad, warn;
            if (value("VSync") > 0) bad += L"Vertical Sync on - switch it off. ";
            // Off, FXAA or MSAA are all fine; only TAA (2) mixes the eyes
            // (docs/plan_left_eye_shake.md: "Anti Aliasing": 2 was TAA).
            if (value("Anti Aliasing") == 2) bad += L"Anti-Aliasing is TAA - pick FXAA or MSAA. ";
            const int cap = value("Framerate Cap");
            {   // the headset's rate for one picture a frame, twice it for real stereo (Performance tab)
                static const int kHz[5] = {72, 75, 80, 90, 120};
                const int hz = kHz[std::clamp((int)GetPrivateProfileIntW(L"stereo", L"headset_rate", 3, g_ini), 0, 4)];
                const bool pair = GetPrivateProfileIntW(L"stereo", L"afr", 0, g_ini) != 0 &&
                                  GetPrivateProfileIntW(L"stereo", L"pair_per_tick", 0, g_ini) == 0;   // eyes by turns
                const int want = pair ? 2 * hz : hz;
                if (GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0) {   // native: the headset paces it
                    if (cap > 0 && cap < 120) warn += L"Framerate Cap " + std::to_wstring(cap) + L" - with native OpenXR the headset paces the game: 0 (none) is best. ";
                } else if (cap > 0 && cap < want) warn += L"Framerate Cap below " + std::to_wstring(want) + (pair ? L" (the headset's rate for each eye). " : L" (the headset's rate). ");
                else if (cap > want) warn += L"Framerate Cap above " + std::to_wstring(want) + L" - head turns judder when the game outruns the headset. ";
            }
            if (!bad.empty()) SetStatus(L"game_video", kBad, (L"Game video: " + bad + L"(Options > Video)").c_str());
            else if (!warn.empty()) SetStatus(L"game_video", kWarn, (L"Game video: " + warn).c_str());
            else SetStatus(L"game_video", kOk, L"Game video settings fine: VSync and TAA off");
        }
    }

    std::string text;
    const bool have = !cfg.empty() && ReadFileText(cfg, &text);
    // No settings file yet: installed but never started, or not there at all.
    // The portable copy D2R VR Setup puts in the game's folder, or an installer's
    // uninstall entry (as the Setup looks for it).
    const std::wstring bwExe = BodyWalkExe();
    // Installed = its settings file, or its exe found (never started yet: the
    // "not running" line below says the one thing to do).
    const int bwVersion = bwExe.empty() ? 0 : ExeVersion(bwExe);
    if (!bwExe.empty() && !NewEnough(bwVersion)) {
        const std::wstring t = L"BodyWalk " + VersionText(bwVersion) + L" is older than D2R VR needs (" +
                               VersionText(kMinBodyWalkMajor * 1000 + kMinBodyWalkMinor) + L") - run D2R VR Setup again, it puts in BodyWalk Portable";
        SetStatus(L"bw_installed", kBad, t.c_str());
    } else
        SetStatus(L"bw_installed", have || !bwExe.empty() ? kOk : kBad,
                  have || !bwExe.empty() ? L"BodyWalk installed" : L"BodyWalk not found - install it and start it once");
    // BodyWalk's virtual Xbox pad: walking, sticks and buttons reach the game through it. BodyWalk's own
    // installer and ours put it in; a portable BodyWalk from the zip asks for it.
    HKEY vigem = nullptr;
    const bool pad = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\ViGEmBus", 0,
                                   KEY_READ | KEY_WOW64_64KEY, &vigem) == ERROR_SUCCESS;
    if (vigem) RegCloseKey(vigem);
    SetStatus(L"vigem", pad ? kOk : kBad, pad ? L"Xbox controller driver installed (ViGEmBus)"
                                              : L"Xbox controller driver (ViGEmBus) not installed - BodyWalk offers it in Xbox mode");
    const bool running = ProcessRunning(L"BodyWalkVR.exe");
    // The running one may be an older copy than the one Start BodyWalk would start (Steam's).
    const std::wstring runExe = running ? RunningBodyWalkExe() : L"";
    const int runVersion = runExe.empty() ? 0 : ExeVersion(runExe);
    if (running && runVersion && !NewEnough(runVersion)) {
        const std::wstring t = L"BodyWalk " + VersionText(runVersion) + L" is running - D2R VR needs " +
                               VersionText(kMinBodyWalkMajor * 1000 + kMinBodyWalkMinor) +
                               (NewEnough(bwVersion) ? L": close it and press Start BodyWalk" : L": run D2R VR Setup again");
        SetStatus(L"bw_running", kBad, t.c_str());
    } else
        SetStatus(L"bw_running", running ? kOk : kBad, running ? L"BodyWalk running" : L"BodyWalk is not running - start it");
    if (Item* start = FindItem(L"@run", L"bodywalk"); start && start->hidden != (running || bwExe.empty())) {
        start->hidden = running || bwExe.empty();
        LayoutPage();
    }

    const bool dll = Exists(dir + L"\\plugins\\game_link\\game_link.dll");
    if (!dll) SetStatus(L"game_link", kBad, L"Game Link plugin not installed (BodyWalk\\plugins\\game_link) - run D2R VR Setup again");
    else if (have && PluginOff(text, "game_link")) SetStatus(L"game_link", kBad, L"Game Link plugin is switched off in BodyWalk's plugin list");
    else if (running && !LinkHost()) SetStatus(L"game_link", kWarn, L"Game Link plugin installed, but the running BodyWalk has not loaded it - restart BodyWalk");
    else SetStatus(L"game_link", kOk, running ? L"Game Link plugin running" : L"Game Link plugin installed");
    // The D2R Bridge plugin of 0.157 and before, left beside Game Link (BodyWalk held it while
    // Setup ran and was not closed): every action twice in BodyWalk's Mapping.
    if (dll && Exists(dir + L"\\plugins\\d2r_bridge\\d2r_bridge.dll"))
        SetStatus(L"game_link", kWarn, L"The old D2R Bridge plugin is still in BodyWalk\\plugins\\d2r_bridge - close BodyWalk and run D2R VR Setup again");

    auto flag = [&](const wchar_t* key, const char* json, bool want, int missState, const wchar_t* good, const wchar_t* bad) {
        if (!have) { SetStatus(key, kUnknown, good); return; }
        const int v = JsonBool(text, json);
        if (v < 0) SetStatus(key, kUnknown, good);
        else SetStatus(key, (v == 1) == want ? kOk : missState, (v == 1) == want ? good : bad);
    };
    flag(L"universal", "universal_tracking_output", true, kBad, L"Universal tracking output on",
         L"Universal tracking output is off - switch it on in BodyWalk (Startup tab)");
    // BodyWalk's Output (bw_output.h): up to BodyWalk 1.83 its buttons reach the game in FlatVR only with Xbox Controller (or Game Link).
    {
        const std::string mode = have ? bwout::Mode(text) : std::string();
        const std::wstring m = Widen(mode);
        switch (bwout::Judge(mode)) {
        case bwout::Verdict::Unknown: SetStatus(L"bw_output", kUnknown, L"BodyWalk's Output"); break;
        case bwout::Verdict::Ok: SetStatus(L"bw_output", kOk, (L"BodyWalk's Output: " + m).c_str()); break;
        case bwout::Verdict::Warn:
            SetStatus(L"bw_output", kWarn, (L"BodyWalk's Output is " + m + L": in FlatVR its buttons may not reach the game - pick "
                                            L"\"Xbox Controller (vJoy)\" on BodyWalk's Main tab (untick Hide locomotion on Startup to see it)").c_str());
            break;
        }
    }
    if (have && PluginOff(text, "flatvr")) SetStatus(L"flatvr_on", kBad, L"FlatVR is switched off in BodyWalk's plugin list");
    else flag(L"flatvr_on", "flat_vr_enabled", true, kBad, L"FlatVR switched on", L"FlatVR is off in BodyWalk - switch it on");
    flag(L"head_lock", "flat_vr_head_lock_from_game", true, kWarn, L"Head Lock follows the game",
         L"\"Head Lock follows the game\" is off in FlatVR: the screen will not lock to the head in first person");
    flag(L"frame_gen", "flat_vr_optical_flow", false, kWarn, L"Frame generation off",
         L"FlatVR frame generation is on: the picture shakes in D2R - switch it off");

    FlatVRHeadSample hs{};
    const bool flatvr = ReadBlock(FLATVR_HEAD_SAMPLE_NAME, &hs, sizeof hs) && Moving(FLATVR_HEAD_SAMPLE_NAME, hs.counter);
    // Start / Stop FlatVR: while BodyWalk runs with Game Link (and can do it), the one
    // that fits FlatVR's state now.
    {
        uint32_t caps = 0;
        const bool link = LinkHost(&caps) && (caps & BW_LINK_CAP_FLATVR_RUNNING);
        bool relayout = false;
        if (Item* s = FindItem(L"@run", L"flatvr_start"); s && s->hidden != (!link || flatvr)) { s->hidden = !link || flatvr; relayout = true; }
        if (Item* s = FindItem(L"@run", L"flatvr_stop"); s && s->hidden != (!link || !flatvr)) { s->hidden = !link || !flatvr; relayout = true; }
        if (relayout) LayoutPage();
    }
    SetStatus(L"flatvr_running", flatvr ? kOk : kWarn, flatvr ? L"FlatVR running" : L"FlatVR is not started - press START in FlatVR");
    BW_LinkTracking tr{};
    const bool live = ReadBlock(BW_LINK_TRACKING_NAME, &tr, sizeof tr) && tr.header.magic == BW_LINK_MAGIC && Moving(BW_LINK_TRACKING_NAME, tr.header.sequence);
    const bool tracked = live && (tr.flags & BW_LINK_TRACK_HEAD) != 0;
    SetStatus(L"head", tracked ? kOk : kWarn,
              tracked ? L"Head tracking reaches the game" :
              live ? L"BodyWalk sends tracking, but the headset is not tracking" : L"No head tracking from BodyWalk yet");
    {   // Game Link direct, or BodyWalk's gamepad and Mapping (bw_path.h)
        bwpath::Facts pf;
        pf.trackingLive = live;
        pf.trackingVersion = tr.header.version;
        pf.trackingFlags = tr.flags;
        BW_LinkWalk wk{};
        pf.walkLive = ReadBlock(BW_LINK_WALK_NAME, &wk, sizeof wk) && wk.header.magic == BW_LINK_MAGIC && Moving(BW_LINK_WALK_NAME, wk.header.sequence) &&
                      (wk.flags & BW_LINK_WALK_ACTIVE) != 0;
        pf.directInput = GetPrivateProfileIntW(L"openxr", L"direct_input", 0, g_ini) != 0;
        pf.flatVrControls = GetPrivateProfileIntW(L"direct", L"flatvr_controls", 1, g_ini) != 0;
        const bwpath::Path path = bwpath::Decide(pf);
        const bwpath::Tone tone = bwpath::ToneOf(path);
        SetStatus(L"bw_path", tone == bwpath::Tone::Ok ? kOk : tone == bwpath::Tone::Warn ? kWarn : kUnknown, bwpath::Text(path));
    }
    D2RVR_State st{};
    const bool game = ReadBlock(D2RVR_STATE_NAME, &st, sizeof st) && Moving(D2RVR_STATE_NAME, st.counter);
    SetStatus(L"game", game ? kOk : kUnknown, game ? L"The game runs with the mod" : L"The game is not running (or the mod is not loaded)");
    {   // did the OpenXR runtime bind the thumbsticks? (the plugin checks the sources of its stick action against the buttons'; no conclusion = the game or the controllers are not there yet)
        const bool native = GetPrivateProfileIntW(L"openxr", L"on", 0, g_ini) != 0;
        const xrstick::Verdict v = game && native ? xrstick::FromBits(st.twoHanded) : xrstick::Verdict::Unknown;
        SetStatus(L"xr_stick", v == xrstick::Verdict::Bound ? kOk : v == xrstick::Verdict::UnboundSpare ? kWarn : v == xrstick::Verdict::Unbound ? kBad : kUnknown,
                  !native ? L"Native OpenXR is off" : !game ? L"The game is not running" : xrstick::StatusText(v));
    }
    {   // the hero playing now (the plugin puts the class of the posed hero's model in bits 8 - 12 of the state block's twoHanded): the per-class wrist depth is edited by it
        static const wchar_t* const kHero[8] = {L"Amazon", L"Sorceress", L"Necromancer", L"Paladin", L"Barbarian", L"Druid", L"Assassin", L"Warlock"};
        const int hero = game ? D2RVRHeroOf(st.twoHanded) : -1;
        for (const wchar_t* key : {L"hero_class", L"hero_class_shield"}) {   // (the per-class depth rows and the under-a-shield rows each have the line)
            if (hero >= 0 && hero < 8) SetStatus(key, kOk, (std::wstring(kHero[hero]) + L" - edit its row below").c_str());
            else SetStatus(key, kUnknown, game ? L"Not known yet (the hero has not been drawn in first person yet)" : L"The game is not running");
        }
    }
}

// ---------------------------------------------------------------------------
// The Status tab: the game's addresses (cleanroom/sigscan/game_sigs.h). vrcam
// looks for every address it uses each time the game starts and writes what it
// found to d2r_vr_game_code.txt beside the ini; Scan bumps [status] scan and
// vrcam looks again for whatever is not found yet. A row per address: green
// found, red NOT OK (and what goes off), orange waiting for the game to run that code.

constexpr size_t kSigCount = sizeof(d2rsig::kSigs) / sizeof(d2rsig::kSigs[0]);
std::deque<std::wstring> g_sigText;   // the rows' keys and labels (an Item keeps the pointers)

std::wstring Wide(const char* s) {
    std::wstring w;
    for (; s && *s; ++s) w += (wchar_t)(unsigned char)*s;
    return w;
}

const wchar_t* Keep(std::wstring s) { return g_sigText.emplace_back(std::move(s)).c_str(); }

// The Strikes tab's table, after its group: a header and one row for each weapon kind the touch swings - the kind, its swing amplitude (x the piercing weapons'), the cm that makes, its least speed, its
// start distance, "toward" and "head only". Built here because the keys are made from the kind's name (kStrikeKinds, strikedir's order).
void AddStrikeTable() {
    size_t at = 0;
    while (at < g_items.size() && !(g_items[at].kind == Kind::Group && wcscmp(g_items[at].label, kStrikeTableGroup) == 0)) ++at;
    if (at == g_items.size()) return;
    std::vector<Item> rows;
    const auto cell = [](int c, Item i) { i.cell = c; return i; };
    const wchar_t* const head[7] = {L"Weapon", L"Swing x", L"= cm", L"Speed", L"Start cm", L"Toward", L"Head"};
    const wchar_t* const headTip[7] = {
        L"The kinds of weapon the touch swings (the game's own kinds: Maces are maces, hammers, clubs and flails). A bow or a crossbow is shot, not swung. The numbers of a row are for that kind alone; "
        L"0 in a number box = follow the general one.",
        L"The kind's least swing amplitude as a multiple of the piercing weapons' (\"Piercing weapons: least swing amplitude, cm\"). 0 = follow the kind's family: edged, blunt or staff, "
        L"or the piercing x1. 0.25 to 8. Type a number and press Enter.",
        L"What the kind's least swing amplitude comes to, in cm: the piercing weapons' cm times the kind's factor (its own, or its family's). A dash: the swing size is not asked.",
        L"The least blade speed, m/s, for a contact of this kind to count. 0 = the general one (\"Hit by touch: the least blade speed that hits\"). 0.3 to 20.",
        L"How far the blade moves to start a stroke, cm, for this kind. 0 = the general one (\"Hit by touch: how far the blade moves to start a stroke\"). 2 to 30.",
        L"Ticked: a contact of this kind must be toward the monster (\"Toward the monster\" above). Unticked: this kind counts whichever way the blade moves.",
        L"Ticked: only the head of this kind of weapon strikes - the outer part of the blade; a touch with the shaft or the handle is refused. Maces and scepters by default."};
    for (int c = 0; c < 7; ++c) {
        Item h = Status(Keep(L"st_h" + std::to_wstring(c)), head[c]);
        h.tip = headTip[c];
        rows.push_back(cell(c, h));
    }
    for (int k = 0; k < kStrikeKindCount; ++k) {
        const std::wstring n = kStrikeKinds[k];
        const wchar_t* const label = kStrikeLabels[k];
        const auto tip = [&](const wchar_t* what) { return Keep(std::wstring(label) + L": " + what); };
        const wchar_t* const familyName[4] = {L"piercing (x1)", L"edged", L"blunt", L"staff"};
        Item name = Status(Keep(L"st_n_" + n), label);
        name.tip = tip(L"one row of numbers for this kind alone. 0 in a number box = follow the general one.");
        rows.push_back(cell(0, name));
        rows.push_back(cell(1, Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_amplitude", 1,
            Slider(L"gameplay", Keep(L"melee_amp_x_" + n), L"", 0, 8, 0.05f, 0,
                   Keep(std::wstring(label) + L": the least swing amplitude, x the piercing weapons'. 0 (or under 0.25) = follow the family this kind belongs to: " + familyName[kStrikeFamily[k]] +
                        L". Live."))))));
        Item cm = Status(Keep(L"st_cm_" + n), L"");
        cm.tip = tip(L"the least swing amplitude in cm: the piercing weapons' cm times this row's factor (or its family's).");
        rows.push_back(cell(2, cm));
        rows.push_back(cell(3, Needs(L"gameplay", L"melee_touch", 1,
            Slider(L"gameplay", Keep(L"melee_min_speed_" + n), L"", 0, 20, 0.1f, 0,
                   tip(L"the least blade speed, m/s, for a contact to count. 0 (or under 0.3) = the general one. Only decides whether a contact counts; the game's damage is the game's. Live.")))));
        rows.push_back(cell(4, Needs(L"gameplay", L"melee_touch", 1,
            Slider(L"gameplay", Keep(L"melee_stroke_start_cm_" + n), L"", 0, 30, 1, 0,
                   tip(L"how far the blade moves to start a stroke, cm. A longer one refuses short fake swings for this kind. 0 (or under 2) = the general one. Live.")))));
        rows.push_back(cell(5, Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_toward", 1,
            Toggle(L"gameplay", Keep(L"melee_toward_" + n), L"", 1,
                   tip(L"a contact must be toward the monster (the rule above) to count. Unticked: it counts whichever way the blade moves. Live."))))));
        rows.push_back(cell(6, Needs(L"gameplay", L"melee_touch", 1, Needs(L"gameplay", L"melee_head_only", 1,
            Toggle(L"gameplay", Keep(L"melee_head_only_" + n), L"", k == 2 || k == 3 ? 1.0f : 0.0f,
                   tip(L"only the head of the weapon strikes (the outer part of the blade); a touch with the shaft or the handle is refused. Live."))))));
    }
    g_items.insert(g_items.begin() + (std::ptrdiff_t)at + 1, rows.begin(), rows.end());
}

// The tab goes last, after Debug.
void AddGameCodeTab() {
    std::vector<Item> add;
    add.push_back(Tab(L"Status"));
    add.push_back(Group(L"The game's code"));
    add.push_back(Status(L"sig_summary", L"Not scanned yet"));
    add.push_back(Button(L"status", L"scan", L"Scan",
                         L"The mod looks for its addresses in the game every time the game starts. After a game or D2RLoader "
                         L"update, start the game and press Scan: whatever is not found yet is looked for again now."));
    const char* area = nullptr;
    for (const d2rsig::Sig& s : d2rsig::kSigs) {
        if (!area || strcmp(area, s.area) != 0) {
            area = s.area;
            add.push_back(Group(Keep(Wide(area))));
        }
        add.push_back(Status(Keep(L"sig:" + Wide(s.name)), Keep(Wide(s.name) + L" - " + Wide(s.without))));
    }
    g_items.insert(g_items.end(), add.begin(), add.end());
}

void RefreshGameCode() {
    const Item* head = FindItem(L"@status", L"sig_summary");
    if (!head || g_tab != head->tab) return;
    std::wstring path = g_ini;
    path.resize(path.size() - wcslen(L"d2r_vr.ini"));
    path += L"d2r_vr_game_code.txt";
    std::ifstream f(path);
    std::string line, time, summary;
    int answered = -1;
    std::map<std::string, std::pair<std::string, std::string>> rows;   // name -> (state, RVA now)
    while (f && std::getline(f, line)) {
        if (line.rfind("time ", 0) == 0) time = line.substr(5);
        else if (line.rfind("scan ", 0) == 0) answered = atoi(line.c_str() + 5);
        else if (line.rfind("summary ", 0) == 0) summary = line.substr(8);
        else {
            std::vector<std::string> c;
            std::stringstream ss(line);
            for (std::string part; std::getline(ss, part, '\t');) c.push_back(part);
            if (c.size() >= 5) rows[c[0]] = {c[4], c[3]};
        }
    }
    const int pressed = GetPrivateProfileIntW(L"status", L"scan", 0, g_ini);
    D2RVR_State st{};
    const bool game = ReadBlock(D2RVR_STATE_NAME, &st, sizeof st) && Moving(D2RVR_STATE_NAME, st.counter);
    if (summary.rfind("game code: ", 0) == 0) summary = summary.substr(11);
    if (rows.empty()) {
        SetStatus(L"sig_summary", kUnknown, L"Not scanned yet - start the game: the mod looks for its addresses every time it starts");
    } else if (answered < pressed) {
        SetStatus(L"sig_summary", kWarn, game ? L"Scanning..." : L"Start the game - the mod scans when it starts");
    } else {
        int ok = 0;
        for (const auto& r : rows) ok += r.second.first == "ok" || r.second.first == "moved";
        std::wstring text = Wide(summary.c_str()) + L" - " + Wide(time.c_str()) + (game ? L"" : L" (the last time the game ran)");
        SetStatus(L"sig_summary", ok == (int)kSigCount ? kOk : kBad, text.c_str());
    }
    for (const d2rsig::Sig& s : d2rsig::kSigs) {
        const std::wstring key = L"sig:" + Wide(s.name), name = Wide(s.name), without = Wide(s.without);
        const auto r = rows.find(s.name);
        wchar_t was[24];
        swprintf_s(was, L"0x%llX", (unsigned long long)s.rva);
        if (r == rows.end()) SetStatus(key.c_str(), kUnknown, (name + L" - not scanned yet (" + without + L")").c_str());
        else if (r->second.first == "ok") SetStatus(key.c_str(), kOk, (name + L": ok (" + was + L")").c_str());
        else if (r->second.first == "moved")
            SetStatus(key.c_str(), kOk, (name + L": ok, moved to " + Wide(r->second.second.c_str()) + L" (was " + was + L")").c_str());
        else if (r->second.first == "waiting")
            SetStatus(key.c_str(), kWarn, (name + L": waiting - the game has not run this code yet (" + without + L")").c_str());
        else SetStatus(key.c_str(), kBad, (name + L": NOT OK - " + without + L" off").c_str());
    }
}

// ---------------------------------------------------------------------------
// The update check (the Home page's "D2R VR" group), as BodyWalk does its own:
// once when the program opens, again when [update] check / develop change. A
// thread asks bodywalkvr.com's /api/version for product d2r_vr (or
// /api/version/develop, the beta channel) and posts the answer to the window as
// WM_APP_UPDATE; "0.0.0" = up to date. Nothing is downloaded or run: Download
// opens the page in the browser. One line per check goes to OutputDebugString
// (this program keeps no log).

constexpr UINT WM_APP_UPDATE = WM_APP + 1;
constexpr const wchar_t* kModsPage = L"https://bodywalkvr.com/mods";
constexpr size_t kShortChangelog = 110;   // what fits the Home page's two lines; longer is not shown

struct UpdateResult {
    int gen = 0;                  // StartUpdateCheck's count when it was asked: an older answer is dropped
    bool develop = false;
    std::wstring error;           // empty = the server answered
    std::wstring version;         // empty = up to date
    std::wstring url, changelog;
};
int g_updateGen = 0;
std::wstring g_updateUrl;         // what Download opens

void LogLine(const std::wstring& line) { OutputDebugStringW((L"[D2R VR Authentic Settings] " + line + L"\n").c_str()); }

// Only our own site's pages are opened: https://bodywalkvr.com/..., plain characters.
bool SafeUrl(const std::wstring& u) {
    static const wchar_t kSite[] = L"https://bodywalkvr.com/";
    if (u.size() > 2048 || u.compare(0, wcslen(kSite), kSite) != 0) return false;
    for (wchar_t c : u)
        if (c <= L' ' || c > L'~' || c == L'"' || c == L'\\') return false;
    return true;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

void AppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// The four hex digits of a \u escape at s[at], or -1.
int Hex4(const std::string& s, size_t at) {
    if (at + 4 > s.size()) return -1;
    int v = 0;
    for (size_t i = at; i < at + 4; ++i) {
        const char c = s[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

// "key": "text" in the server's JSON, unescaped; false when it is not a string (missing, null).
// A quote inside a JSON string is always escaped, so "key" cannot match inside another value.
bool JsonString(const std::string& body, const char* key, std::wstring* out) {
    std::smatch m;
    if (!std::regex_search(body, m, std::regex(std::string("\"") + key + "\"\\s*:\\s*\""))) return false;
    std::string s;
    for (size_t i = (size_t)(m.position(0) + m.length(0)); i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') { *out = Widen(s); return true; }
        if (c != '\\') { s += c; continue; }
        if (++i >= body.size()) break;
        switch (body[i]) {
        case 'n': s += '\n'; break;
        case 'r': s += '\r'; break;
        case 't': s += '\t'; break;
        case 'b': case 'f': s += ' '; break;
        case 'u': {
            int cp = Hex4(body, i + 1);
            if (cp < 0) return false;
            i += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < body.size() && body[i + 1] == '\\' && body[i + 2] == 'u') {
                const int lo = Hex4(body, i + 3);
                if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
            }
            AppendUtf8(s, cp >= 0xD800 && cp < 0xE000 ? 0xFFFD : (unsigned)cp);
            break;
        }
        default: s += body[i]; break;   // \" \\ \/
        }
    }
    return false;
}

// "0.132.0" as numbers; false unless it is digits and dots only.
bool VersionParts(const std::wstring& v, std::vector<unsigned long>* parts) {
    parts->clear();
    if (v.empty() || v.size() > 32) return false;
    unsigned long n = 0;
    bool digit = false;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') { n = n * 10 + (c - L'0'); digit = true; }
        else if (c == L'.' && digit) { parts->push_back(n); n = 0; digit = false; }
        else return false;
    }
    if (!digit) return false;
    parts->push_back(n);
    return true;
}
// a newer than b, part by part (a missing part is 0).
bool Newer(const std::vector<unsigned long>& a, const std::vector<unsigned long>& b) {
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const unsigned long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

// On its own thread: asks the server, posts the answer to `wnd` (which then owns it).
void UpdateThread(HWND wnd, UpdateResult r) {
    const std::wstring channel = r.develop ? L"develop" : L"production";
    const std::wstring path = std::wstring(r.develop ? L"/api/version/develop" : L"/api/version") + L"?product=d2r_vr&v=" D2RVR_VERSION_W;
    std::string body;
    DWORD status = 0;
    HINTERNET session = WinHttpOpen(L"D2RVR-Settings-UpdateCheck/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = nullptr, request = nullptr;
    const wchar_t* failed = nullptr;
    DWORD failedError = 0;
    auto fail = [&](const wchar_t* step) { failed = step; failedError = GetLastError(); };
    if (!session) fail(L"WinHttpOpen");
    else {
        WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);   // resolve, connect, send, receive
        connect = WinHttpConnect(session, L"bodywalkvr.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) fail(L"WinHttpConnect");
    }
    if (!failed) {
        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE);
        if (!request) fail(L"WinHttpOpenRequest");
    }
    if (!failed && !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail(L"WinHttpSendRequest");
    if (!failed && !WinHttpReceiveResponse(request, nullptr)) fail(L"WinHttpReceiveResponse");
    if (!failed) {
        DWORD size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                            &size, WINHTTP_NO_HEADER_INDEX);
        for (DWORD avail = 0; body.size() < 65536 && WinHttpQueryDataAvailable(request, &avail) && avail > 0;) {
            std::string chunk(std::min<DWORD>(avail, 65536), '\0');
            DWORD got = 0;
            if (!WinHttpReadData(request, chunk.data(), (DWORD)chunk.size(), &got) || got == 0) break;
            body.append(chunk.data(), got);
        }
    }
    if (failed) {
        wchar_t err[96];
        swprintf_s(err, L"%s failed, error %lu", failed, failedError);
        r.error = err;
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);

    std::vector<unsigned long> theirs, ours;
    VersionParts(D2RVR_VERSION_W, &ours);
    std::wstring version;
    if (r.error.empty() && status != 200) r.error = L"the server answered HTTP " + std::to_wstring(status);
    if (r.error.empty() && (!JsonString(body, "version", &version) || !VersionParts(version, &theirs)))
        r.error = L"no version in the server's answer";
    if (r.error.empty() && version != L"0.0.0" && Newer(theirs, ours)) {   // the server says 0.0.0 when we are up to date
        r.version = version;
        JsonString(body, "download_url", &r.url);
        if (!SafeUrl(r.url)) {
            LogLine(L"update check: download_url refused (not https://bodywalkvr.com/...): " + r.url);
            r.url = kModsPage;
        }
        JsonString(body, "changelog", &r.changelog);
    }
    if (!r.error.empty()) LogLine(L"update check (" + channel + L"): " + r.error);
    else if (r.version.empty()) LogLine(L"update check (" + channel + L"): up to date, " D2RVR_VERSION_W L" (server: " + version + L")");
    else LogLine(L"update check (" + channel + L"): " + r.version + L" available at " + r.url);

    auto* post = new UpdateResult(std::move(r));
    if (!PostMessageW(wnd, WM_APP_UPDATE, 0, (LPARAM)post)) delete post;   // the window is gone
}

Item* FindItem(const wchar_t* section, const wchar_t* key) {
    for (Item& it : g_items)
        if (it.key && wcscmp(it.section, section) == 0 && wcscmp(it.key, key) == 0) return &it;
    return nullptr;
}

// The Download button and the changelog line in or out of the page.
void ShowUpdateRow(bool download, bool changelog) {
    Item* d = FindItem(L"@update", L"download");
    Item* c = FindItem(L"@status", L"changelog");
    if (!d || !c || (d->hidden == !download && c->hidden == !changelog)) return;
    d->hidden = !download;
    c->hidden = !changelog;
    LayoutPage();
}

// manual: the "Check for updates" button - asks even with [update] check=0.
// ---------------------------------------------------------------------------
// D2RLoader: its row on the Home page's Status, and the button that puts it in.
// The mod runs under D2RLoader 1.3.1 (vrcam is made for its layout). A player
// found its download refused by Edge and Chrome (2026-10-08): the button fetches
// that very file from d2rloader.net, checks it against the SHA-256 the site
// publishes and unpacks it beside D2R.exe - only when the player clicks it.
// On 2026-10-13 the author repacked the 1.3.1 ZIP under the same name (compression and
// order of the files; D2RCore.dll, D2RLoader.exe and d2rloader.mpq are byte for byte the
// same): the page lists the new hash as current and the old one as historical. Both are
// accepted, any other file is not.
constexpr wchar_t kD2RLoaderHost[] = L"d2rloader.net";
constexpr wchar_t kD2RLoaderPath[] = L"/downloads/D2RLoader-1.3.1-beta.zip";
constexpr wchar_t kD2RLoaderZip[] = L"D2RLoader-1.3.1-beta.zip";
constexpr char kD2RLoaderSha256[] = "f7bf0287398890b09bcc68b576327e727304bd88f7be80160f9676e8bd6871e2";
constexpr char kD2RLoaderSha256Historical[] = "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7";
constexpr UINT WM_APP_D2RL = WM_APP + 2;
std::atomic<bool> g_d2rlBusy{false};
std::wstring g_d2rlError;   // the last try's failure, kept on the row until D2RLoader is there (UI thread)
struct D2RLProgress { int state; std::wstring text; bool done; };

// The game's folder: this program sits beside D2R.exe, and a copy in d2rloader\plugins.
std::wstring GameFolder() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    while (n && exe[n - 1] != L'\\') --n;
    std::wstring dir(exe, n);
    const std::wstring tail = L"d2rloader\\plugins\\";
    if (dir.size() > tail.size() && _wcsicmp(dir.c_str() + dir.size() - tail.size(), tail.c_str()) == 0)
        dir.resize(dir.size() - tail.size());
    return dir;
}

// allow_global_extensions = true in d2rloader\config\d2rloader.toml: D2RLoader loads
// the plugins in d2rloader\plugins only with it (as D2R VR Setup's EnableGlobalExtensions).
void EnableGlobalExtensions(const std::wstring& game) {
    const std::wstring path = game + L"d2rloader\\config\\d2rloader.toml";
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string off = "allow_global_extensions = false";
    const size_t at = text.find(off);
    if (at == std::string::npos) return;
    text.replace(at, off.size(), "allow_global_extensions = true");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    LogLine(L"d2rloader: allow_global_extensions switched on - D2RLoader loads the mod's plugins");
}

void D2RLoaderStatus() {
    const std::wstring game = GameFolder();
    const bool have = Exists(game + L"D2RLoader.exe");
    if (!g_d2rlBusy.load()) {
        if (have) {
            EnableGlobalExtensions(game);
            SetStatus(L"d2rloader", kOk, L"D2RLoader installed");
        } else {
            SetStatus(L"d2rloader", kBad, g_d2rlError.empty() ? L"D2RLoader is not in the game's folder - the mod runs only under it"
                                                               : g_d2rlError.c_str());
        }
    }
    if (Item* b = FindItem(L"@run", L"d2rloader_get"); b && b->hidden != have) {
        b->hidden = have;
        LayoutPage();
    }
}

void PostD2RL(HWND wnd, int state, std::wstring text, bool done) {
    auto* p = new D2RLProgress{state, std::move(text), done};
    if (!PostMessageW(wnd, WM_APP_D2RL, 0, (LPARAM)p)) delete p;   // the window is gone
}

bool Sha256Hex(const std::vector<uint8_t>& data, std::string* hex) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    uint8_t out[32];
    const bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
                    BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0 &&
                    BCryptHashData(h, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
                    BCryptFinishHash(h, out, sizeof out, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return false;
    static const char digits[] = "0123456789abcdef";
    hex->clear();
    for (uint8_t b : out) { *hex += digits[b >> 4]; *hex += digits[b & 15]; }
    return true;
}

// PowerShell takes a whole script as base64 of UTF-16LE (-EncodedCommand): no quoting of paths on its command line.
std::wstring EncodedCommand(const std::wstring& script) {
    DWORD n = 0;
    const BYTE* bytes = (const BYTE*)script.data();
    const DWORD size = (DWORD)(script.size() * sizeof(wchar_t));
    if (!CryptBinaryToStringW(bytes, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n)) return L"";
    std::wstring out(n, L'\0');
    if (!CryptBinaryToStringW(bytes, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &n)) return L"";
    out.resize(n);
    return out;
}

// A path inside a PowerShell single-quoted string.
std::wstring PsQuote(const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) { out += c; if (c == L'\'') out += L'\''; }
    return out + L"'";
}

void D2RLoaderThread(HWND wnd, std::wstring game) {
    auto fail = [&](const std::wstring& why) { LogLine(L"d2rloader: " + why); PostD2RL(wnd, kBad, why, true); };
    std::vector<uint8_t> data;
    {
        HINTERNET session = WinHttpOpen(L"D2RVR-Settings/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        HINTERNET connect = session ? WinHttpConnect(session, kD2RLoaderHost, INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
        HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", kD2RLoaderPath, nullptr, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        DWORD status = 0, size = sizeof status;
        const bool sent = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                          WinHttpReceiveResponse(request, nullptr) &&
                          WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                              &status, &size, WINHTTP_NO_HEADER_INDEX) && status == 200;
        if (sent) {
            DWORD total = 0;
            size = sizeof total;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &total, &size, WINHTTP_NO_HEADER_INDEX);
            int shown = -1;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail) || !avail) break;
                const size_t at = data.size();
                data.resize(at + avail);
                DWORD got = 0;
                if (!WinHttpReadData(request, data.data() + at, avail, &got)) { data.clear(); break; }
                data.resize(at + got);
                const int pct = total ? (int)((uint64_t)data.size() * 100 / total) : -1;
                if (pct >= 0 && pct / 5 != shown / 5) {
                    shown = pct;
                    PostD2RL(wnd, kUnknown, L"Downloading D2RLoader 1.3.1 from d2rloader.net... " + std::to_wstring(pct) + L"%", false);
                }
            }
        }
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
        if (!sent) { fail(L"Could not download D2RLoader from d2rloader.net (HTTP " + std::to_wstring(status) + L") - try again, or get it from the site"); return; }
        if (data.empty()) { fail(L"The download of D2RLoader broke off - try again"); return; }
    }
    std::string hex;
    if (!Sha256Hex(data, &hex) || (hex != kD2RLoaderSha256 && hex != kD2RLoaderSha256Historical)) {
        fail(L"The downloaded D2RLoader is not the 1.3.1 ZIP d2rloader.net publishes (SHA-256 differs) - nothing was installed");
        return;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring zip = std::wstring(tmp) + kD2RLoaderZip;
    {
        std::ofstream out(zip, std::ios::binary | std::ios::trunc);
        out.write((const char*)data.data(), (std::streamsize)data.size());
        if (!out) { fail(L"Could not save the D2RLoader download to " + zip); return; }
    }
    PostD2RL(wnd, kUnknown, L"Unpacking D2RLoader into the game's folder...", false);
    // Unpacked to a folder of its own first: wherever D2RLoader.exe lies in the
    // archive, its folder's contents go beside D2R.exe.
    const std::wstring unpack = std::wstring(tmp) + L"d2rloader_unpack";
    const std::wstring script =
        L"$ErrorActionPreference='Stop'; $t=" + PsQuote(unpack) + L"; $g=" + PsQuote(game) +
        L"; if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }" +
        L"; Expand-Archive -LiteralPath " + PsQuote(zip) + L" -DestinationPath $t -Force" +
        L"; $e = Get-ChildItem -LiteralPath $t -Recurse -Filter 'D2RLoader.exe' | Select-Object -First 1" +
        L"; if (-not $e) { exit 3 }" +
        L"; Get-ChildItem -LiteralPath $e.DirectoryName | Copy-Item -Destination $g -Recurse -Force" +
        L"; Remove-Item -LiteralPath $t -Recurse -Force; exit 0";
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + EncodedCommand(script);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    DWORD code = 1;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 120000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    DeleteFileW(zip.c_str());
    if (code == 3) { fail(L"The D2RLoader archive has no D2RLoader.exe in it - nothing was installed"); return; }
    if (code != 0) { fail(L"Could not unpack D2RLoader into the game's folder (code " + std::to_wstring(code) + L")"); return; }
    if (!Exists(game + L"D2RLoader.exe")) {
        // Unpacked, and gone: an antivirus took it. Said as it is; the player decides.
        fail(L"D2RLoader was unpacked, but D2RLoader.exe is not in the game's folder - an antivirus may have removed it "
             L"(Windows Security > Protection history)");
        return;
    }
    EnableGlobalExtensions(game);
    LogLine(L"d2rloader: 1.3.1 installed into " + game);
    PostD2RL(wnd, kOk, L"D2RLoader 1.3.1 installed - start the game with D2RLoader.exe", true);
}

// ---- Install ReShade (Home > Status): reshade.me's current setup with add-on support,
// headless, for D2RLoader.exe (D2R.exe while there is none); then its dxgi.dll is
// renamed to ReShade64.dll and its search paths fixed, as D2R VR Setup did up to 0.145.
std::atomic<bool> g_rsBusy{false};
std::wstring g_rsError;
constexpr UINT WM_APP_RESHADE = WM_APP + 41;

bool HttpGet(const wchar_t* host, const std::wstring& path, std::vector<uint8_t>* data, DWORD* status) {
    HINTERNET session = WinHttpOpen(L"D2RVR-Settings/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    DWORD size = sizeof *status;
    *status = 0;
    bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr) &&
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                  status, &size, WINHTTP_NO_HEADER_INDEX) && *status == 200;
    if (ok) {
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || !avail) break;
            const size_t at = data->size();
            data->resize(at + avail);
            DWORD got = 0;
            if (!WinHttpReadData(request, data->data() + at, avail, &got)) { ok = false; break; }
            data->resize(at + got);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok && !data->empty();
}

void PostReShade(HWND wnd, int state, std::wstring text, bool done) {
    auto* p = new D2RLProgress{state, std::move(text), done};
    if (!PostMessageW(wnd, WM_APP_RESHADE, 0, (LPARAM)p)) delete p;
}

void ReShadeThread(HWND wnd, std::wstring game) {
    auto fail = [&](const std::wstring& why) { LogLine(L"reshade: " + why); PostReShade(wnd, kBad, why, true); };
    // the current setup's name on reshade.me's front page, as BodyWalk finds it
    std::vector<uint8_t> page;
    DWORD status = 0;
    if (!HttpGet(L"reshade.me", L"/", &page, &status)) { fail(L"Could not reach reshade.me (HTTP " + std::to_wstring(status) + L") - try again"); return; }
    const std::string html(page.begin(), page.end());
    std::string name;
    for (size_t p = html.find("ReShade_Setup_"); p != std::string::npos; p = html.find("ReShade_Setup_", p + 1)) {
        const size_t e = html.find("_Addon.exe", p);
        if (e != std::string::npos && e - p < 40) { name = html.substr(p, e - p + 10); break; }
    }
    if (name.empty()) { fail(L"Could not find the current ReShade with add-on support on reshade.me"); return; }
    const std::wstring wname(name.begin(), name.end());
    PostReShade(wnd, kUnknown, L"Downloading " + wname + L" from reshade.me...", false);
    std::vector<uint8_t> setup;
    if (!HttpGet(L"reshade.me", L"/downloads/" + wname, &setup, &status)) { fail(L"Could not download " + wname + L" (HTTP " + std::to_wstring(status) + L")"); return; }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring exe = std::wstring(tmp) + wname;
    {
        std::ofstream out(exe, std::ios::binary | std::ios::trunc);
        out.write((const char*)setup.data(), (std::streamsize)setup.size());
        if (!out) { fail(L"Could not save the ReShade setup to " + exe); return; }
    }
    PostReShade(wnd, kUnknown, L"Installing ReShade into the game's folder...", false);
    const std::wstring modReShade = ModInstall() ? modlayout::ModReShade(game) : std::wstring();
    const std::wstring pluginsDir = ModInstall() ? modlayout::ModPlugins(game) : game + L"d2rloader\\plugins";
    // One script: the headless setup for D2RLoader.exe (D2R.exe while there is none - the
    // setup quits with code 1 for an exe that is not there), the search paths ReShade 6.8
    // cannot read ('\**\**' -> '\**'), dxgi.dll -> ReShade64.dll, our mark for the uninstaller.
    // Elevated when the game's folder takes no writes (Program Files).
    const std::wstring script =
        L"$ErrorActionPreference='Stop'; $g=" + PsQuote(game) + L"; $s=" + PsQuote(exe) +
        L"; $t = Join-Path $g 'D2RLoader.exe'; if (-not (Test-Path -LiteralPath $t)) { $t = Join-Path $g 'D2R.exe' }" +
        L"; $p = Start-Process -FilePath $s -ArgumentList ('\"' + $t + '\" --api dxgi --headless') -Wait -PassThru" +
        L"; if ($p.ExitCode -ne 0) { exit 10 + $p.ExitCode }" +
        L"; $ini = Join-Path $g 'ReShade.ini'; if (Test-Path -LiteralPath $ini) { $c = [IO.File]::ReadAllText($ini); $n = $c.Replace('\\**\\**', '\\**'); if ($n -ne $c) { [IO.File]::WriteAllText($ini, $n) } }" +
        L"; $d = Join-Path $g 'dxgi.dll'; $r = Join-Path $g 'ReShade64.dll'" +
        L"; if (Test-Path -LiteralPath $d) { if (Test-Path -LiteralPath $r) { Remove-Item -LiteralPath $d -Force } else { Rename-Item -LiteralPath $d -NewName 'ReShade64.dll' } }" +
        L"; if (-not (Test-Path -LiteralPath $r)) { exit 3 }" +
        // installed as the D2RLoader mod D2R-VR: ReShade and its ini go into the mod's own reshade\ (vrcam's ReShadeDir loads it from there)
        L"; $mr = " + PsQuote(modReShade) + L"; if ($mr) { New-Item -ItemType Directory -Force -Path $mr | Out-Null" +
        L"; Move-Item -LiteralPath $r -Destination (Join-Path $mr 'ReShade64.dll') -Force" +
        L"; if ((Test-Path -LiteralPath $ini) -and -not (Test-Path -LiteralPath (Join-Path $mr 'ReShade.ini'))) { Move-Item -LiteralPath $ini -Destination (Join-Path $mr 'ReShade.ini') } }" +
        L"; $m = " + PsQuote(pluginsDir) + L"; if (Test-Path -LiteralPath $m) { Set-Content -LiteralPath (Join-Path $m 'd2r_vr_reshade_ours.txt') -Value 'ReShade was installed by D2R VR Settings.' }" +
        L"; exit 0";
    const std::wstring args = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " + EncodedCommand(script);
    bool writable = false;
    {
        const std::wstring probe = game + L"d2r_vr_write_probe.tmp";
        HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (f != INVALID_HANDLE_VALUE) { writable = true; CloseHandle(f); }
    }
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = writable ? L"open" : L"runas";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    DWORD code = 1;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        DeleteFileW(exe.c_str());
        fail(writable ? L"Could not start the ReShade setup" : L"The game's folder needs administrator rights - allow it, and press Install ReShade again");
        return;
    }
    WaitForSingleObject(sei.hProcess, 300000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    DeleteFileW(exe.c_str());
    if (code == 3) { fail(L"ReShade's setup ran, but no ReShade64.dll came out - an antivirus may have removed it"); return; }
    if (code >= 10) { fail(L"ReShade's setup did not finish (code " + std::to_wstring(code - 10) + L") - try again, or install it from reshade.me"); return; }
    if (code != 0) { fail(L"Could not install ReShade (code " + std::to_wstring(code) + L")"); return; }
    LogLine(L"reshade: " + wname + L" installed into " + game);
    PostReShade(wnd, kOk, L"ReShade installed - start the game with D2RLoader.exe", true);
}

void StartReShadeInstall() {
    if (g_rsBusy.exchange(true)) return;
    if (Item* b = FindItem(L"@run", L"reshade_get")) EnableWindow(b->ctl, FALSE);
    SetStatus(L"reshade", kUnknown, L"Looking for the current ReShade on reshade.me...");
    try {
        std::thread(ReShadeThread, g_main, GameFolder()).detach();
    } catch (const std::exception&) {
        g_rsBusy.store(false);
        SetStatus(L"reshade", kBad, L"Could not start the ReShade setup");
    }
}

void StartArcaneSky();
void OnReShadeProgress(D2RLProgress* raw) {
    std::unique_ptr<D2RLProgress> p(raw);
    SetStatus(L"reshade", p->state, p->text.c_str());
    if (!p->done) return;
    g_rsBusy.store(false);
    g_rsError = p->state == kOk ? L"" : p->text;
    if (Item* b = FindItem(L"@run", L"reshade_get")) EnableWindow(b->ctl, TRUE);
    if (p->state == kOk) { RefreshStatus(); StartArcaneSky(); }   // the button goes away with ReShade there; the Arcane sky has its folder now
}

// ---------------------------------------------------------------------------
// Collect logs for support (Home): one zip on the desktop the player posts in our
// Discord. While the game runs, [debug] diag_go makes vrcam write 10 s of its frame log
// (d2r_vr_frames.csv: every pair, view, pose, present and one-pass step) and FlatVR its
// pose trace beside it; then PowerShell copies the logs, the settings (BodyWalk's only
// the keys D2R VR needs - no e-mail, no headset serials), the versions and the PC's
// graphics card, CPU and Windows into the zip, and Explorer shows it. Nothing is sent.
// A crash too (0.157): vrcam's d2r_vr_crash.txt and d2r_vr_start.txt come with the logs
// folder, its minidump (*.dmp, the last day's), and Windows' last three Application Error /
// Application Hang events of the game or D2RLoader (the module it died in, when vrcam had
// not loaded yet).
constexpr UINT WM_APP_COLLECT = WM_APP + 42;
std::atomic<bool> g_collectBusy{false};

void PostCollect(HWND wnd, int state, std::wstring text, bool done) {
    auto* p = new D2RLProgress{state, std::move(text), done};
    if (!PostMessageW(wnd, WM_APP_COLLECT, 0, (LPARAM)p)) delete p;
}

void CollectThread(HWND wnd, std::wstring game, bool running) {
    if (running) {
        wchar_t v[32];
        swprintf_s(v, L"%lu", GetTickCount());
        WritePrivateProfileStringW(L"debug", L"diag_go", v, g_ini);
        for (int left = 11; left > 0; --left) {
            PostCollect(wnd, kUnknown, L"Recording how the frames are drawn: " + std::to_wstring(left) +
                        L" s - keep playing and turn your head", false);
            Sleep(1000);
        }
    }
    PostCollect(wnd, kUnknown, L"Packing the logs...", false);
    wchar_t* desk = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk)) && desk) dir = desk;
    CoTaskMemFree(desk);
    if (dir.empty()) dir = game;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"D2R_VR_logs_%04d-%02d-%02d_%02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring zip = dir + L"\\" + name + L".zip";
    std::wstring script = LR"PS($ErrorActionPreference='SilentlyContinue'
$g=__GAME__; $z=__ZIP__; $n=__NAME__
$t=Join-Path $env:TEMP $n; New-Item -ItemType Directory -Force $t | Out-Null
function C($p,$sub){ if(Test-Path -LiteralPath $p){ $d=Join-Path $t $sub; New-Item -ItemType Directory -Force $d | Out-Null; Copy-Item -LiteralPath $p -Destination $d -Force } }
$pl=Join-Path $g 'd2rloader\plugins'; $lg=Join-Path $g 'd2rloader\logs'; $rs=$g; $mlg=$null; $md=__MOD__
if($md){ $pl=Join-Path $md 'd2rloader\plugins'; $mlg=Join-Path $md 'd2rloader\logs'; $rs=Join-Path $md 'reshade' }
Get-ChildItem -LiteralPath $lg -File | Where-Object { ($_.Extension -in '.log','.txt','.csv') -and $_.Length -lt 50MB } | ForEach-Object { C $_.FullName 'd2rloader_logs' }
if($mlg){ Get-ChildItem -LiteralPath $mlg -File | Where-Object { ($_.Extension -in '.log','.txt','.csv') -and $_.Length -lt 50MB } | ForEach-Object { C $_.FullName 'mod_logs' } }
foreach($f in 'd2r_vr_about.json','d2r_vr_start.txt','d2r_vr_crash.txt'){ C (Join-Path $pl $f) 'plugins' }
foreach($f in 'd2r_vr.ini','d2r_vr.default.ini','d2r_vr_frames.csv','d2r_vr_uitrace.txt'){ C (Join-Path $pl $f) 'plugins' }
Get-ChildItem -LiteralPath (Join-Path $g 'd2rloader\crashes') -File -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object { C $_.FullName 'd2rloader_crashes' }
Get-ChildItem -LiteralPath $lg -File -Filter '*.dmp' | Where-Object { $_.LastWriteTime -gt (Get-Date).AddDays(-1) -and $_.Length -lt 50MB } | ForEach-Object { C $_.FullName 'd2rloader_logs' }
foreach($f in 'ReShade.log','ReShade.ini','ReShadePreset.ini','FlatVR_DepthProvider.log'){ C (Join-Path $rs $f) 'game' }
$ev=Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error','Application Hang'; StartTime=(Get-Date).AddDays(-14)} -MaxEvents 400 | Where-Object { $_.Message -match 'D2R|D2RLoader|Diablo' } | Select-Object -First 3
if($ev){ $ev | Format-List TimeCreated,ProviderName,Id,Message | Out-String -Width 400 | Set-Content -LiteralPath (Join-Path $t 'windows_crash_events.txt') -Encoding UTF8 }
C (Join-Path $env:USERPROFILE 'Saved Games\Diablo II Resurrected\Settings.json') 'game'
$b=Join-Path $env:LOCALAPPDATA 'BodyWalkVR'
foreach($f in 'flat_vr_pose_trace.csv','flat_vr_init_log.txt','system_info.txt','plugin_profiles.json','openxr_layer.log'){ C (Join-Path $b $f) 'bodywalk' }
foreach($f in 'usersettings.json','usersettings_steam.json'){
  $p=Join-Path $b $f
  if(Test-Path -LiteralPath $p){
    $j=Get-Content -LiteralPath $p -Raw | ConvertFrom-Json; $o=[ordered]@{}
    foreach($pr in $j.PSObject.Properties){ $k=$pr.Name
      if(($k -match '^(flat_vr_|gesture_source$|universal_tracking_output$|disabled_plugins$|loaded_profile_name$|current_engine$|output_mode)') -and ($k -notmatch 'serial|email|token|author|password|license')){ $o[$k]=$pr.Value } }
    New-Item -ItemType Directory -Force (Join-Path $t 'bodywalk') | Out-Null
    $o | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path (Join-Path $t 'bodywalk') $f) -Encoding UTF8 } }
$i=@('D2R VR logs '+$n,'')
function V($p){ if(Test-Path -LiteralPath $p){ $e=Get-Item -LiteralPath $p; '{0}: {1} ({2:yyyy-MM-dd HH:mm})' -f $p,$e.VersionInfo.FileVersion,$e.LastWriteTime } else { $p+': none' } }
foreach($p in (Join-Path $pl 'd2rl-vrcam.dll'),(Join-Path $g 'D2R_VR_Settings.exe'),(Join-Path $rs 'FlatVR_DepthProvider.addon64'),(Join-Path $rs 'ReShade64.dll'),(Join-Path $g 'D2RLoader.exe'),(Join-Path $(if($md){$md}else{$g}) 'BodyWalkVR\BodyWalkVR.exe')){ $i+=V $p }
Get-Process BodyWalkVR,D2R,D2RLoader | ForEach-Object { $i+=('running: '+$_.ProcessName+' '+$_.Path+' '+$_.MainModule.FileVersionInfo.FileVersion) }
$i+=''
Get-CimInstance Win32_OperatingSystem | ForEach-Object { $i+=('Windows: '+$_.Caption+' '+$_.Version+', RAM '+[math]::Round($_.TotalVisibleMemorySize/1MB,1)+' GB') }
Get-CimInstance Win32_Processor | ForEach-Object { $i+=('CPU: '+$_.Name) }
Get-CimInstance Win32_VideoController | ForEach-Object { $i+=('GPU: '+$_.Name+', driver '+$_.DriverVersion+', '+$_.CurrentHorizontalResolution+'x'+$_.CurrentVerticalResolution+' at '+$_.CurrentRefreshRate+' Hz') }
$i | Set-Content -LiteralPath (Join-Path $t 'versions_and_pc.txt') -Encoding UTF8
Compress-Archive -Path (Join-Path $t '*') -DestinationPath $z -Force
Remove-Item -LiteralPath $t -Recurse -Force
if(Test-Path -LiteralPath $z){ exit 0 } else { exit 2 })PS";
    auto put = [&](const wchar_t* key, const std::wstring& value) {
        const size_t at = script.find(key);
        if (at != std::wstring::npos) script.replace(at, wcslen(key), PsQuote(value));
    };
    put(L"__GAME__", game);
    put(L"__MOD__", ModInstall() ? modlayout::ModRoot(game) : std::wstring());   // D2R VR as the D2RLoader mod: its plugin folder, logs and ReShade are the mod's
    put(L"__ZIP__", zip);
    put(L"__NAME__", name);
    const std::wstring args = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -EncodedCommand " + EncodedCommand(script);
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) { PostCollect(wnd, kBad, L"Could not start PowerShell to pack the logs", true); return; }
    WaitForSingleObject(sei.hProcess, 180000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code != 0 || !Exists(zip)) { PostCollect(wnd, kBad, L"Could not make the zip (code " + std::to_wstring(code) + L")", true); return; }
    LogLine(L"collect logs: " + zip);
    const std::wstring sel = L"/select,\"" + zip + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", sel.c_str(), nullptr, SW_SHOWNORMAL);
    PostCollect(wnd, kOk, L"Saved on your desktop: " + std::wstring(name) + L".zip - post it in our Discord (#diablo-2-vr)", true);
}

// ReShade the player has as dxgi.dll (Home > The game and ReShade; shared/reshade_as.h, shared/reshade_paths.h): D2R VR uses it as it is. Nothing of ours - not
// Setup, not the uninstaller, not these buttons - renames, moves or deletes it. For D2R VR's effect and pictures (in mods\D2R-VR\reshade) to reach it, two list
// items are added to EffectSearchPaths and TextureSearchPaths of the ReShade.ini it reads (append-only, once, a copy kept as ReShade.ini.before_d2rvr); Setup runs
// this at the end, the uninstaller takes them out again, the Home button does it for a ReShade installed later.
std::string Utf8Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    std::string s(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
    return s;
}
// The ReShade.ini a dxgi.dll in the game's folder reads - ReShade's own rule (its dll_main.cpp get_base_path): [INSTALL] BasePath in <game>\ReShade.ini names another base
// folder (relative to the dll's, the game's), else the game's folder is the base.
std::wstring ReShadeIniOfDxgi(const std::wstring& game, bool* redirected) {
    *redirected = false;
    std::string text;
    if (ReadFileText(game + L"ReShade.ini", &text)) {
        const std::string bp = reshadepaths::InstallBasePath(text);
        if (!bp.empty()) {
            std::wstring full = Widen(bp);
            if (full.size() < 2 || (full[1] != L':' && full.rfind(L"\\\\", 0) != 0)) full = game + full;   // (relative to the game's folder, the dll's)
            wchar_t buf[2 * MAX_PATH];
            const DWORD n = GetFullPathNameW(full.c_str(), (DWORD)std::size(buf), buf, nullptr);
            if (n && n < std::size(buf)) full = buf;
            const DWORD a = GetFileAttributesW(full.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
                *redirected = true;
                return modlayout::WithSlash(full) + L"ReShade.ini";
            }
        }
    }
    return game + L"ReShade.ini";
}
struct PathForms {
    reshadepaths::Entries use;                      // what is written: relative to the base when that is the game's folder, else absolute
    std::vector<reshadepaths::Entries> all;         // both spellings: what is taken out again
};
PathForms ReShadePathForms(const std::wstring& game, const std::wstring& iniPath) {
    const std::wstring mod = modlayout::ModReShade(game);
    PathForms f;
    const reshadepaths::Entries rel = reshadepaths::EntriesFor(".\\" + Utf8Narrow(mod.substr(game.size())));
    const reshadepaths::Entries abs = reshadepaths::EntriesFor(Utf8Narrow(mod));
    f.all = {rel, abs};
    const bool baseIsGame = _wcsnicmp(iniPath.c_str(), game.c_str(), game.size()) == 0 && iniPath.find(L'\\', game.size()) == std::wstring::npos;
    f.use = baseIsGame ? rel : abs;
    return f;
}
// The text of an ini without its UTF-8 mark (kept apart); false for a UTF-16 file, which this does not edit.
bool SplitIniText(const std::string& raw, std::string* bom, std::string* body) {
    if (raw.size() >= 2 && ((unsigned char)raw[0] == 0xFF || (unsigned char)raw[0] == 0xFE) && ((unsigned char)raw[1] == 0xFE || (unsigned char)raw[1] == 0xFF)) return false;
    const bool utf8 = raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF;
    *bom = utf8 ? raw.substr(0, 3) : std::string();
    *body = utf8 ? raw.substr(3) : raw;
    return true;
}
bool WriteWhole(const std::wstring& path, const std::string& data) {   // beside it first, then moved over: ReShade never meets half a file
    const std::wstring tmp = path + L".d2rvr.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), (std::streamsize)data.size());
        if (!out) { out.close(); DeleteFileW(tmp.c_str()); return false; }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
// Exit codes (also of --reshade-paths add): 0 they are in (or were), 1 could not write, 2 nothing to do (not the mod, or no dxgi.dll next to D2R.exe), 3 the game runs
// (ReShade writes its ini back when the game ends: an edit now would be lost).
int ReShadePathsAdd() {
    const std::wstring game = GameFolder();
    if (!ModInstall() || !Exists(game + L"dxgi.dll")) return 2;
    if (ProcessRunning(L"D2R.exe") || ProcessRunning(L"D2RLoader.exe")) return 3;
    bool redirected = false;
    const std::wstring ini = ReShadeIniOfDxgi(game, &redirected);
    std::string raw, bom, body;
    const bool had = ReadFileText(ini, &raw);
    if (had && !SplitIniText(raw, &bom, &body)) { LogLine(L"reshade paths: " + ini + L" is not a UTF-8 / ANSI file - not edited"); return 1; }
    const PathForms f = ReShadePathForms(game, ini);
    bool changed = false;
    const std::string out = reshadepaths::Add(body, f.use, &changed);
    if (!changed) { LogLine(L"reshade paths: already in " + ini); return 0; }
    if (had && !Exists(ini + L".before_d2rvr")) CopyFileW(ini.c_str(), (ini + L".before_d2rvr").c_str(), TRUE);   // the player's file as it was, once
    if (!had) { const size_t cut = ini.find_last_of(L'\\'); if (cut != std::wstring::npos) CreateDirectoryW(ini.substr(0, cut).c_str(), nullptr); }
    if (!WriteWhole(ini, bom + out)) { LogLine(L"reshade paths: could not write " + ini); return 1; }
    LogLine(L"reshade paths: D2R VR's two folders added to " + ini);
    return 0;
}
// 0 taken out (or not there), 1 could not write. Never needs a dxgi.dll to be there; the backup goes when the file is back to it.
int ReShadePathsRemove() {
    const std::wstring game = GameFolder();
    if (!ModInstall()) return 2;
    int rc = 0;
    bool redirected = false;
    const std::wstring first = ReShadeIniOfDxgi(game, &redirected);
    std::vector<std::wstring> inis{first};
    if (redirected) inis.push_back(game + L"ReShade.ini");
    for (const std::wstring& ini : inis) {
        std::string raw, bom, body;
        if (!ReadFileText(ini, &raw) || !SplitIniText(raw, &bom, &body)) continue;
        bool changed = false;
        const std::string out = reshadepaths::Remove(body, ReShadePathForms(game, ini).all, &changed);
        if (!changed) continue;
        if (!WriteWhole(ini, bom + out)) { rc = 1; continue; }
        std::string backup;
        if (ReadFileText(ini + L".before_d2rvr", &backup) && backup == bom + out) DeleteFileW((ini + L".before_d2rvr").c_str());   // nothing else changed: the copy is not needed
        LogLine(L"reshade paths: D2R VR's folders taken out of " + ini);
    }
    return rc;
}
// 0 both are in, 1 not, 2 no dxgi.dll beside the game (or not the mod).
int ReShadePathsCheck() {
    const std::wstring game = GameFolder();
    if (!ModInstall() || !Exists(game + L"dxgi.dll")) return 2;
    bool redirected = false;
    const std::wstring ini = ReShadeIniOfDxgi(game, &redirected);
    std::string raw, bom, body;
    if (!ReadFileText(ini, &raw) || !SplitIniText(raw, &bom, &body)) return 1;
    const PathForms f = ReShadePathForms(game, ini);
    for (const reshadepaths::Entries& e : f.all)
        if (reshadepaths::Has(body, e)) return 0;
    return 1;
}
void ReShadePathsButton() {
    const int rc = ReShadePathsAdd();
    if (rc == 3)
        MessageBoxW(g_main, L"The game is running: ReShade writes its ReShade.ini back when the game ends, so a change now would be lost. Close the game and press the button again.",
                    kAppName, MB_OK | MB_ICONINFORMATION);
    else if (rc == 1)
        MessageBoxW(g_main, L"Could not change your ReShade.ini (it may be read-only, in a folder that needs administrator rights, or not a plain text file). Nothing was changed.",
                    kAppName, MB_OK | MB_ICONWARNING);
    RefreshStatus();
}
// The one explicit choice: the player's dxgi.dll renamed to dxgi.dll.off (and back). Asks first; deletes and replaces nothing.
void ReShadeAside(bool back) {
    if (!back &&
        MessageBoxW(g_main,
                    L"Put your ReShade (dxgi.dll next to D2R.exe) aside as dxgi.dll.off?\n\nNothing is deleted. D2R VR then has no ReShade until you press Install ReShade, which "
                    L"gives it one of its own in mods\\D2R-VR\\reshade. The game started without D2RLoader has no ReShade meanwhile. \"Bring my dxgi.dll back\" undoes this.",
                    kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    if (ProcessRunning(L"D2R.exe") || ProcessRunning(L"D2RLoader.exe")) {
        MessageBoxW(g_main, L"The game is running - close it first.", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    const reshadeas::Result r = reshadeas::Apply(back ? reshadeas::Act::Back : reshadeas::Act::Aside, ReShadeWhere());
    LogLine(std::wstring(L"reshade: ") + r.did + (r.ok ? L"" : L" - FAILED"));
    if (!r.ok)
        MessageBoxW(g_main, (L"Could not rename the file (error " + std::to_wstring(r.error) + L"). Nothing was changed. The folder may need administrator rights.").c_str(),
                    kAppName, MB_OK | MB_ICONWARNING);
    RefreshStatus();
}

// "Forget the measurement": the stored T-pose numbers are cleared (the game reads the ini again at once and goes back to the
// height from BodyWalk / "My height" and to "Arm length (scale)"). The box "Use the measured ..." is left as it is.
void ForgetCalibration() {
    WritePrivateProfileStringW(L"calibration", L"version", L"0", g_ini);
    WritePrivateProfileStringW(L"calibration", L"eye_height_m", L"0", g_ini);
    WritePrivateProfileStringW(L"calibration", L"arm_span_m", L"0", g_ini);
}

// [sky] arcane_sky: the pictures made from the game's own files (arcane_assets.cpp), on a thread of its own -
// opening the game's archives takes a moment. At the start, and when the tick is switched on.
constexpr UINT WM_APP_ARCANE = WM_APP + 43;
std::atomic<bool> g_arcaneBusy{false};

// Where the pictures go, the way the plugin finds them (its ReShadeDir()): ReShade's own folder - the mod's reshade\ in the D2RLoader mod
// D2R-VR (mods\D2R-VR\reshade, ReShade's base there), the game's folder otherwise - and, in the mod, the mod's own data folder for the
// level's visual files. The game's archives are read from the game's folder.
arcane::Result PrepareArcane() {
    const std::wstring game = GameFolder();
    return arcane::Prepare(game, ReShadeFiles(), ModInstall() ? modlayout::ModRoot(game) : std::wstring());
}

void ArcaneThread(HWND wnd) {
    const arcane::Result r = PrepareArcane();
    LogLine(L"arcane sky: " + r.text);
    const int state = r.state == arcane::kMade ? kOk : r.state == arcane::kNotHere ? kWarn : kBad;
    auto* p = new D2RLProgress{state, L"Arcane Sanctuary sky: " + r.text, true};
    if (!PostMessageW(wnd, WM_APP_ARCANE, 0, (LPARAM)p)) delete p;
}

void StartArcaneSky() {
    if (GetPrivateProfileIntW(L"sky", L"arcane_sky", 1, g_ini) == 0) {
        SetStatus(L"arcane_sky", kUnknown, L"Arcane Sanctuary sky: off ([sky] arcane_sky, the Sky tab)");
        return;
    }
    if (g_arcaneBusy.exchange(true)) return;
    SetStatus(L"arcane_sky", kUnknown, L"Arcane Sanctuary sky: making it from the game's files...");
    try {
        std::thread(ArcaneThread, g_main).detach();
    } catch (const std::exception&) {
        g_arcaneBusy.store(false);
        SetStatus(L"arcane_sky", kBad, L"Arcane Sanctuary sky: could not be started");
    }
}

void OnArcaneDone(D2RLProgress* raw) {
    std::unique_ptr<D2RLProgress> p(raw);
    g_arcaneBusy.store(false);
    SetStatus(L"arcane_sky", p->state, p->text.c_str());
}

void StartCollectLogs() {
    if (g_collectBusy.exchange(true)) return;
    if (Item* b = FindItem(L"@run", L"collect_logs")) EnableWindow(b->ctl, FALSE);
    SetStatus(L"collect", kUnknown, L"Collecting the logs...");
    try {
        std::thread(CollectThread, g_main, GameFolder(), ProcessRunning(L"D2R.exe")).detach();
    } catch (const std::exception&) {
        g_collectBusy.store(false);
        SetStatus(L"collect", kBad, L"Could not start collecting the logs");
    }
}

void OnCollectProgress(D2RLProgress* raw) {
    std::unique_ptr<D2RLProgress> p(raw);
    SetStatus(L"collect", p->state, p->text.c_str());
    if (!p->done) return;
    g_collectBusy.store(false);
    if (Item* b = FindItem(L"@run", L"collect_logs")) EnableWindow(b->ctl, TRUE);
}

void StartD2RLoaderInstall() {
    if (g_d2rlBusy.exchange(true)) return;
    if (Item* b = FindItem(L"@run", L"d2rloader_get")) EnableWindow(b->ctl, FALSE);
    SetStatus(L"d2rloader", kUnknown, L"Downloading D2RLoader 1.3.1 from d2rloader.net...");
    try {
        std::thread(D2RLoaderThread, g_main, GameFolder()).detach();
    } catch (const std::exception&) {
        g_d2rlBusy.store(false);
        SetStatus(L"d2rloader", kBad, L"Could not start the download");
    }
}

void OnD2RLProgress(D2RLProgress* raw) {
    std::unique_ptr<D2RLProgress> p(raw);
    SetStatus(L"d2rloader", p->state, p->text.c_str());
    if (!p->done) return;
    g_d2rlBusy.store(false);
    g_d2rlError = p->state == kOk ? L"" : p->text;
    if (Item* b = FindItem(L"@run", L"d2rloader_get")) EnableWindow(b->ctl, TRUE);
    if (p->state == kOk) D2RLoaderStatus();   // the button goes away with D2RLoader there
}

void StartUpdateCheck(bool manual = false) {
    ++g_updateGen;   // an answer still on its way is stale now
    g_updateUrl.clear();
    ShowUpdateRow(false, false);
    if (!manual && GetPrivateProfileIntW(L"update", L"check", 1, g_ini) == 0) {
        SetStatus(L"update", kUnknown, L"Update check is off");
        LogLine(L"update check: off ([update] check=0)");
        return;
    }
    UpdateResult r;
    r.gen = g_updateGen;
    r.develop = GetPrivateProfileIntW(L"update", L"develop", 0, g_ini) != 0;
    SetStatus(L"update", kUnknown, r.develop ? L"Checking for updates (beta channel)..." : L"Checking for updates...");
    try {
        std::thread(UpdateThread, g_main, std::move(r)).detach();
    } catch (const std::exception&) {
        SetStatus(L"update", kUnknown, L"Could not check for updates");
        LogLine(L"update check: no thread");
    }
}

void OnUpdateResult(UpdateResult* raw) {
    std::unique_ptr<UpdateResult> r(raw);
    if (r->gen != g_updateGen) return;
    if (!r->error.empty()) {
        SetStatus(L"update", kUnknown, (L"Could not check for updates: " + r->error).c_str());
        return;
    }
    if (r->version.empty()) {
        SetStatus(L"update", kOk, r->develop ? L"Up to date (beta channel)" : L"Up to date");
        return;
    }
    SetStatus(L"update", kWarn, (L"Update available: v" + r->version).c_str());
    g_updateUrl = r->url;
    if (Item* d = FindItem(L"@update", L"download")) SetWindowTextW(d->ctl, (L"Download v" + r->version).c_str());
    // The changelog on one line, if it is short enough for the page.
    std::wstring text;
    for (wchar_t ch : r->changelog) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
        if (ch == L' ' && (text.empty() || text.back() == L' ')) continue;
        text += ch;
    }
    while (!text.empty() && text.back() == L' ') text.pop_back();
    const bool shortText = !text.empty() && text.size() <= kShortChangelog;
    if (shortText)
        if (Item* c = FindItem(L"@status", L"changelog")) SetWindowTextW(c->ctl, (L"What's new: " + text).c_str());
    ShowUpdateRow(true, shortText);
}

// Download: the page in the browser, never anything run here. Checked again before ShellExecute.
void OpenDownload() {
    const std::wstring url = SafeUrl(g_updateUrl) ? g_updateUrl : std::wstring(kModsPage);
    const INT_PTR r = (INT_PTR)ShellExecuteW(g_main, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (r <= 32) LogLine(L"update: ShellExecute failed (" + std::to_wstring(r) + L") for " + url);
}

LRESULT CALLBACK PageProc(HWND wnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_MOUSEWHEEL: ScrollTo(g_scrollY - GET_WHEEL_DELTA_WPARAM(w) * 3 * S(kRowH) / WHEEL_DELTA); return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof si, SIF_ALL};
        GetScrollInfo(wnd, SB_VERT, &si);
        int y = g_scrollY;
        switch (LOWORD(w)) {
        case SB_LINEUP: y -= S(kRowH); break;
        case SB_LINEDOWN: y += S(kRowH); break;
        case SB_PAGEUP: y -= g_viewH; break;
        case SB_PAGEDOWN: y += g_viewH; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = g_contentH; break;
        }
        ScrollTo(y);
        return 0;
    }
    case WM_HSCROLL: {
        const int id = GetDlgCtrlID((HWND)l);
        if (id >= kFirstId && id < kFirstId + (int)g_items.size()) {
            Item& it = g_items[id - kFirstId];
            if (it.kind == Kind::Slider) {
                const float v = FromPos(it, (int)SendMessageW(it.ctl, TBM_GETPOS, 0, 0));
                ShowValue(it, v);
                WriteValue(it, v);
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(w);
        if (HIWORD(w) == BN_CLICKED && id >= kRadioId && id < kRadioId + (int)g_items.size() * 8) {
            Item& it = g_items[(id - kRadioId) / 8];
            const int v = (int)it.min + (id - kRadioId) % 8;
            WriteValue(it, (float)v);
            if (wcscmp(it.key, L"platform") == 0 && v != g_platform) { g_platform = v; PlatformChanged(); RefreshStatus(); }
            UpdateEnabled();
            return 0;
        }
        if (HIWORD(w) == BN_CLICKED && id >= kBrowseId && id < kBrowseId + (int)g_items.size()) {
            Item& it = g_items[id - kBrowseId];
            if (it.kind == Kind::Color) ChooseFogColor(g_main, it); else ChoosePicture(g_main, it);
            return 0;
        }
        if (HIWORD(w) == EN_KILLFOCUS && id >= kValueId && id < kValueId + (int)g_items.size()) {
            CommitTyped(g_items[id - kValueId]);
            return 0;
        }
        if (HIWORD(w) == BN_CLICKED && id >= kClearId && id < kClearId + (int)g_items.size()) {
            // empty = the shader draws this sky itself (the zenith: the palette's colour; the fog: the sky's horizon)
            Item& it = g_items[id - kClearId];
            WritePrivateProfileStringW(it.section, it.key, L"", g_ini);
            if (it.kind == Kind::Color) ShowColor(it); else SetWindowTextW(it.ctl, L"");
            return 0;
        }
        if (id >= kFirstId && id < kFirstId + (int)g_items.size()) {
            Item& it = g_items[id - kFirstId];
            // a row of the weapon list: stop following the hand, edit that kind
            if (it.kind == Kind::Status && HIWORD(w) == STN_CLICKED) {
                const int t = _wtoi(it.key + 2);
                for (Item& o : g_items) {
                    if (!o.key || wcscmp(o.section, L"@ui") != 0) continue;
                    if (wcscmp(o.key, L"weapon_follow") == 0) { WriteValue(o, 0.0f); SendMessageW(o.ctl, BM_SETCHECK, BST_UNCHECKED, 0); }
                    if (wcscmp(o.key, L"weapon") == 0) WriteValue(o, (float)(t - 1));
                }
                ShowWeapon(t);
                UpdateEnabled();
                RefreshWeaponList();
                return 0;
            }
            if (it.kind == Kind::Choice && HIWORD(w) == CBN_SELCHANGE && wcscmp(it.section, L"@preset") == 0) {
                ApplyPreset((int)SendMessageW(it.ctl, CB_GETCURSEL, 0, 0));
                ShowPreset();   // Custom picked: back to what the keys match
            } else if (it.kind == Kind::Choice && HIWORD(w) == CBN_SELCHANGE) {
                const int sel = (int)SendMessageW(it.ctl, CB_GETCURSEL, 0, 0);
                WriteValue(it, FromPos(it, sel));
                if (wcscmp(it.section, L"@ui") == 0 && wcscmp(it.key, L"weapon") == 0) ShowWeapon(sel + 1);
                UpdateEnabled();
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button && wcscmp(it.section, L"@update") == 0) {
                if (wcscmp(it.key, L"check_now") == 0) StartUpdateCheck(true);
                else OpenDownload();
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button && wcscmp(it.section, L"@run") == 0) {
                if (wcscmp(it.key, L"bodywalk") == 0) StartBodyWalk();
                else if (wcscmp(it.key, L"d2r_launch") == 0) LaunchGame();
                else if (wcscmp(it.key, L"d2rloader_get") == 0) StartD2RLoaderInstall();
                else if (wcscmp(it.key, L"reshade_get") == 0) StartReShadeInstall();
                else if (wcscmp(it.key, L"reshade_paths") == 0) ReShadePathsButton();
                else if (wcscmp(it.key, L"reshade_aside") == 0) ReShadeAside(false);
                else if (wcscmp(it.key, L"reshade_back") == 0) ReShadeAside(true);
                else if (wcscmp(it.key, L"collect_logs") == 0) StartCollectLogs();
                else if (wcscmp(it.key, L"arcane_make") == 0) StartArcaneSky();
                else if (wcscmp(it.key, L"calibration_forget") == 0) ForgetCalibration();
                else if (BW_LinkRequests* r = LinkRequests()) bw::link::Request(r, &r->flatvrRunning, wcscmp(it.key, L"flatvr_start") == 0 ? 1 : 0);
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button) {
                WriteValue(it, (float)(((int)ReadValue(it) + 1) % 1000000));
            } else if (HIWORD(w) == BN_CLICKED && (it.kind == Kind::Toggle || it.kind == Kind::Invert)) {
                const bool on = SendMessageW(it.ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (it.kind == Kind::Toggle) WriteValue(it, on ? 1.0f : 0.0f);
                else WriteValue(it, on ? -1.0f : 1.0f);
                if (wcscmp(it.section, L"@ui") == 0) FollowWeapon();
                if (wcscmp(it.section, L"direct") == 0) RefreshControls();   // left_stick_map, pause_button: what the button list calls "the standard" moved
                UpdateEnabled();
                if (wcscmp(it.section, L"update") == 0) StartUpdateCheck();   // switched on, or the other channel
                if (wcscmp(it.section, L"sky") == 0 && wcscmp(it.key, L"arcane_sky") == 0) StartArcaneSky();
            }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return ColorStatic((HWND)l, (HDC)w);
    }
    return DefWindowProcW(wnd, msg, w, l);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_main = wnd;
        g_dpi = GetDpiForWindow(wnd);
        const HINSTANCE inst = GetModuleHandleW(nullptr);
        g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                0, 0, 0, 0, wnd, nullptr, inst, nullptr);
        SendMessageW(g_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
        g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                                 0, 0, 0, 0, wnd, nullptr, inst, nullptr);
        g_page = CreateWindowExW(WS_EX_CONTROLPARENT, L"D2RVRSettingsPage", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                 0, 0, 0, 0, wnd, nullptr, inst, nullptr);
        SetWindowPos(g_page, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // above the tab control
        g_footer = CreateWindowExW(0, L"STATIC",
                                   L"Changes apply at once while the game runs.   F1 - F4: the view (Home).   F12: next view.   "
                                   L"F11: \"ahead\" is where I look.   Ctrl + wheel: zoom.   Ctrl + Tab: next tab.",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, wnd, nullptr, inst, nullptr);
        CarryBarKeys();
        Build(g_page);
        UpdateEnabled();
        RefreshStrikeCm();
        g_platform =GetPrivateProfileIntW(L"mode", L"platform", 1, g_ini) != 0 ? 1 : 0;
        g_tab = std::clamp(g_tab, 0, std::max(0, g_tabCount - 1));
        RebuildTabs();
        ApplyScale();
        SetTimer(wnd, 1, 500, nullptr);   // the weapon in hand, the view keys, the status column
        FollowWeapon();
        RefreshStatus();
        StartUpdateCheck();
        StartArcaneSky();
        return 0;
    }
    case WM_APP_UPDATE: OnUpdateResult((UpdateResult*)l); return 0;
    case WM_APP_D2RL: OnD2RLProgress((D2RLProgress*)l); return 0;
    case WM_APP_RESHADE: OnReShadeProgress((D2RLProgress*)l); return 0;
    case WM_APP_COLLECT: OnCollectProgress((D2RLProgress*)l); return 0;
    case WM_APP_ARCANE: OnArcaneDone((D2RLProgress*)l); return 0;
    case WM_TIMER: {
        FollowLaunch();
        FollowWeapon();
        RefreshRadios();
        RefreshWeaponList();
        RefreshGameCode();
        static int tick = 0;
        if (++tick % 2 == 0) RefreshStatus();
        return 0;
    }
    case WM_SIZE: if (w != SIZE_MINIMIZED) LayoutMain(); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {S(440), S(360)};
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        ApplyScale();
        const RECT* r = (const RECT*)l;
        SetWindowPos(wnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutMain();
        return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR*)l)->hwndFrom == g_tabs && ((NMHDR*)l)->code == TCN_SELCHANGE) {
            SelectTab((int)SendMessageW(g_tabs, TCM_GETCURSEL, 0, 0));
            RefreshStatus();
        }
        return 0;
    case WM_CTLCOLORSTATIC: return ColorStatic((HWND)l, (HDC)w);
    case WM_DESTROY: {
        WINDOWPLACEMENT wp{sizeof wp};
        GetWindowPlacement(wnd, &wp);
        const float toLogical = 96.0f / g_dpi;
        wchar_t buf[32];
        swprintf_s(buf, L"%d", (int)std::lround((wp.rcNormalPosition.right - wp.rcNormalPosition.left) * toLogical));
        WritePrivateProfileStringW(L"window", L"width", buf, g_uiIni);
        swprintf_s(buf, L"%d", (int)std::lround((wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) * toLogical));
        WritePrivateProfileStringW(L"window", L"height", buf, g_uiIni);
        swprintf_s(buf, L"%g", g_zoom);
        WritePrivateProfileStringW(L"window", L"zoom", buf, g_uiIni);
        swprintf_s(buf, L"%d", g_tab);
        WritePrivateProfileStringW(L"window", L"tab", buf, g_uiIni);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(wnd, msg, w, l);
}

}  // namespace

// ---------------------------------------------------------------------------
// D2R_VR_Settings.exe --setup-bodywalk <settings file>...
//
// For the mod's installer (installer/D2R_VR_Setup.iss): BodyWalk's settings
// as the VR mode needs them - Universal tracking output (the head reaches the
// Game Link), FlatVR on, Head Lock follows the game, frame generation off (it
// shakes the picture in D2R), the background and glow round the screen off -
// and the Game Link plugin not switched off.
// Each file given that exists is edited as text (BodyWalk's own JSON, keys
// added at the top when missing), BodyWalk must not be running. Exit code: 0
// all written, 1 a file could not be written, 2 none of them exists.
namespace {

void SetJsonBool(std::string& text, const char* key, bool value) {
    const std::regex re(std::string("(\"") + key + "\"\\s*:\\s*)(true|false)");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        text = m.prefix().str() + m[1].str() + (value ? "true" : "false") + m.suffix().str();
        return;
    }
    const size_t brace = text.find('{');
    if (brace == std::string::npos) return;
    text.insert(brace + 1, std::string("\n    \"") + key + "\": " + (value ? "true" : "false") + ",");
}

// "disabled_plugins": [...] without any entry naming this plugin.
void EnablePlugin(std::string& text, const char* part) {
    std::smatch m;
    if (!std::regex_search(text, m, std::regex("(\"disabled_plugins\"\\s*:\\s*\\[)([^\\]]*)(\\])"))) return;
    std::vector<std::string> keep;
    const std::string body = m[2].str();
    const std::regex quoted("\"[^\"]*\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), quoted); it != std::sregex_iterator(); ++it)
        if (it->str().find(part) == std::string::npos) keep.push_back(it->str());
    std::string list;
    for (size_t i = 0; i < keep.size(); ++i) list += (i ? ", " : "") + keep[i];
    text = m.prefix().str() + m[1].str() + list + m[3].str() + m.suffix().str();
}

int SetupBodyWalk(int argc, wchar_t** argv) {
    int found = 0, failed = 0;
    for (int i = 2; i < argc; ++i) {
        std::string text;
        if (!ReadFileText(argv[i], &text)) continue;
        ++found;
        SetJsonBool(text, "universal_tracking_output", true);
        SetJsonBool(text, "flat_vr_enabled", true);
        SetJsonBool(text, "flat_vr_head_lock_from_game", true);
        SetJsonBool(text, "flat_vr_optical_flow", false);
        SetJsonBool(text, "flat_vr_enable_ambilight", false);   // FlatVR's background and glow round the screen: off for D2R
        EnablePlugin(text, "game_link");
        EnablePlugin(text, "\"flatvr\"");
        std::ofstream f(argv[i], std::ios::binary | std::ios::trunc);
        f << text;
        if (!f.good()) ++failed;
    }
    return failed ? 1 : found ? 0 : 2;
}

// D2R_VR_Settings.exe --game-link-manifest <file>
//
// For installer/build_installer.ps1: the game's Game Link manifest as BodyWalk keeps
// its copy (bodywalk/game_link.h, BW_LINK_CACHE_DIR) - what vrcam sends (vr/link.cpp),
// from the same tables. The installer puts it in BodyWalk's game_link folder, so the
// actions are in BodyWalk's Mapping from its first start, before the game ever ran.
// Exit code 0 written, 1 not.
int WriteGameLinkManifest(const wchar_t* path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return 1;
    f << "game " D2RVR_GAME_ID "\n" << "name " D2RVR_GAME_NAME "\n" << "mod " D2RVR_MOD_NAME " " D2RVR_VERSION "\n";
    for (const D2RVRActionName& a : kD2RVRActions) f << "action " << a.name << "\n";
    for (const D2RVRCommand& c : kD2RVRCommands) f << "action " << c.action << "\n";
    D2RVRTabs([&](const char* name, const char* desc) { f << "tab " << name << "|" << desc << "\n"; });
    return f.good() ? 0 : 1;
}

// The program sits next to the game's exe (where players look for it, 2026-10-05); the ini is the mod's:
// <game>\mods\D2R-VR\d2rloader\plugins\d2r_vr.ini (D2R VR as the D2RLoader mod D2R-VR, shared/mod_layout.h), else the global plugin's in
// <game>\d2rloader\plugins (an install from before the mod layout). An ini right next to it wins (the oldest place).
// Sets g_ini and g_uiIni (they may name files that are not there: the caller looks).
void FindIni() {
    DWORD n = GetModuleFileNameW(nullptr, g_ini, MAX_PATH);
    while (n && g_ini[n - 1] != L'\\') --n;
    g_ini[n] = 0;
    {
        wchar_t here[MAX_PATH];
        wcscpy_s(here, g_ini);
        wcscat_s(g_ini, L"d2r_vr.ini");
        if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {   // D2R VR as the D2RLoader mod D2R-VR (shared/mod_layout.h)
            const std::wstring mod = modlayout::ModPlugins(here) + L"d2r_vr.ini";
            wcscpy_s(g_ini, mod.c_str());
        }
        if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {   // the global plugin, as before
            wcscpy_s(g_ini, here);
            wcscat_s(g_ini, L"d2rloader\\plugins\\d2r_vr.ini");
        }
    }
    wcscpy_s(g_uiIni, g_ini);
    g_uiIni[wcslen(g_uiIni) - wcslen(L"d2r_vr.ini")] = 0;
    wcscat_s(g_uiIni, L"d2r_vr_settings.ini");
}

// D2R_VR_Settings.exe --start
//
// The shared VR mod launcher's way to start D2R VR Authentic (docs/launcher_authentic.md): the START button without the window. The
// launcher runs this with what it chose for this start in the environment - XR_RUNTIME_JSON (the OpenXR runtime, only for this start) and
// VRMOD_* (who was started, for the logs) - and D2RLoader.exe, so the game, inherits it.
// What it does, as the button does: reads [mode] platform from d2r_vr.ini; sets the game's window for it (flat: the player's own; VR: the
// square, if ticked); starts `D2RLoader.exe -mod D2R-VR` from the game's folder (the plugin loads only under that mod; an install from
// before the mod layout is started without it, as before); in VR also BodyWalk (not with native OpenXR and "Controls without BodyWalk") and, for
// FlatVR, asks FlatVR to start once BodyWalk's Game Link is up (it waits up to a minute for that; native OpenXR returns at once). It does
// not wait for the game to end, opens no window and no message box.
// What it says goes to stdout (a pipe or a console, when there is one) and to d2rloader\logs\d2r_vr_launch.txt.
// Exit code: 0 started; 1 no d2r_vr.ini (not in the mod); 2 no D2RLoader.exe; 3 D2RLoader.exe could not be started; 4 the game runs.
void StartSay(const std::wstring& line) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    auto utf8 = [](const std::wstring& w) {
        std::string s(static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), (int)s.size(), nullptr, nullptr);
        return s;
    };
    const std::string text = utf8(line) + "\r\n";
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
        DWORD w = 0;
        WriteFile(out, text.data(), (DWORD)text.size(), &w, nullptr);
    }
    // the D2RLoader mod D2R-VR keeps its own logs (mods\D2R-VR\d2rloader\logs, where vrcam's start/crash files are too); an earlier install's global folder
    const std::wstring base = ModInstall() ? modlayout::ModRoot(GameFolder()) : GameFolder();
    const std::wstring dir = base + L"d2rloader\\logs\\";
    CreateDirectoryW((base + L"d2rloader").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    const HANDLE f = CreateFileW((dir + L"d2r_vr_launch.txt").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    const std::string logged = utf8(stamp) + text;
    DWORD w = 0;
    WriteFile(f, logged.data(), (DWORD)logged.size(), &w, nullptr);
    CloseHandle(f);
}
std::wstring EnvText(const wchar_t* name) {
    wchar_t b[400];
    const DWORD n = GetEnvironmentVariableW(name, b, 400);
    return n > 0 && n < 400 ? std::wstring(b, n) : std::wstring();
}
int StartHeadless() {
    g_headless = true;
    // run from a console, a GUI-subsystem program has no stdout: take the parent's (a pipe the launcher gave us is already there)
    const HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((!so || so == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        const HANDLE con = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (con != INVALID_HANDLE_VALUE) SetStdHandle(STD_OUTPUT_HANDLE, con);
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // ShellExecute asks for it
    FindIni();
    const std::wstring game = GameFolder();
    StartSay(L"start: D2R VR " D2RVR_VERSION_W L" in " + game + L", settings " + g_ini);
    const std::wstring by = EnvText(L"VRMOD_ACTIVE_ID");
    if (!by.empty())
        StartSay(L"start: by the VR mod launcher as " + by + L" " + EnvText(L"VRMOD_ACTIVE_VERSION") + L", runtime " + EnvText(L"VRMOD_RUNTIME") +
                 L", store " + EnvText(L"VRMOD_STORE") + (EnvText(L"XR_RUNTIME_JSON").empty() ? L"" : L", XR_RUNTIME_JSON " + EnvText(L"XR_RUNTIME_JSON")));
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {
        StartSay(L"start: there is no d2r_vr.ini in mods\\D2R-VR\\d2rloader\\plugins (nor in d2rloader\\plugins) - not started");
        return 1;
    }
    if (!Exists(game + L"D2RLoader.exe")) {
        StartSay(L"start: D2RLoader.exe is not in the game's folder - D2R VR runs only under it - not started");
        return 2;
    }
    if (ProcessRunning(L"D2RLoader.exe") || ProcessRunning(L"D2R.exe")) {
        StartSay(L"start: the game is running already - not started");
        return 4;
    }
    g_platform = GetPrivateProfileIntW(L"mode", L"platform", 1, g_ini) != 0 ? 1 : 0;
    const int r = LaunchGame();
    if (r) {
        StartSay(L"start: D2RLoader.exe could not be started - not started");
        return r;
    }
    StartSay(std::wstring(L"start: D2RLoader.exe ") + (ModInstall() ? L"-mod D2R-VR " : L"") + L"started (" + (g_platform == 1 ? L"VR" : L"flat") + L")");
    // FlatVR: its Start goes to BodyWalk once Game Link is up (the window does this on a timer; here we wait)
    while (g_flatVrStartBy) {
        FollowLaunch();
        if (!g_flatVrStartBy) break;
        Sleep(500);
    }
    StartSay(L"start: done");
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    {   // the installer's mode: no window
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2 && wcscmp(argv[1], L"--setup-bodywalk") == 0) {
            const int r = SetupBodyWalk(argc, argv);
            LocalFree(argv);
            return r;
        }
        if (argv && argc >= 2 && wcscmp(argv[1], L"--prepare-assets") == 0) {
            // D2R VR Setup, once at the end ([sky] arcane_sky): the Arcane Sanctuary's sky pictures out of the
            // player's own game. 0 = made or up to date, 2 = no ReShade folder to put them in, 1 = could not.
            // Setup goes on whatever comes back: the program makes them again at its next start.
            LocalFree(argv);
            FindIni();   // the mod's d2r_vr.ini (mods\D2R-VR\d2rloader\plugins), as the window finds it - not a path of the global layout
            if (GetPrivateProfileIntW(L"sky", L"arcane_sky", 1, g_ini) == 0) return 0;   // unticked: nothing made
            const arcane::Result r = PrepareArcane();
            LogLine(L"arcane sky: " + r.text);
            return r.state == arcane::kMade ? 0 : r.state == arcane::kNotHere ? 2 : 1;
        }
        if (argv && argc >= 3 && wcscmp(argv[1], L"--reshade-paths") == 0) {
            // D2R VR Setup (add, once at the end) and its uninstaller (remove): D2R VR's two folders in the search paths of the ReShade the player has as dxgi.dll
            // (shared/reshade_paths.h; the dxgi.dll itself is never touched). add: 0 they are in, 1 could not write, 2 nothing to do (no dxgi.dll), 3 the game runs;
            // remove: 0 out (or never in), 1 could not write; check: 0 in, 1 not, 2 no dxgi.dll. Setup goes on whatever comes back.
            const std::wstring verb = argv[2];
            LocalFree(argv);
            FindIni();
            if (verb == L"add") return ReShadePathsAdd();
            if (verb == L"remove") return ReShadePathsRemove();
            if (verb == L"check") return ReShadePathsCheck();
            return 64;
        }
        if (argv && argc >= 3 && wcscmp(argv[1], L"--game-link-manifest") == 0) {   // the installer's build
            const int r = WriteGameLinkManifest(argv[2]);
            LocalFree(argv);
            return r;
        }
        if (argv && argc >= 2 && wcscmp(argv[1], L"--start") == 0) {   // the shared VR mod launcher: START without the window
            LocalFree(argv);
            return StartHeadless();
        }
        if (argv) LocalFree(argv);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // ShellExecute of the download page asks for it

    FindIni();
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"There is no d2r_vr.ini in mods\\D2R-VR\\d2rloader\\plugins (nor in d2rloader\\plugins) next to this program.\n"
                             L"Put this program next to the game's D2R.exe, with the mod installed.",
                    kAppName, MB_ICONWARNING);
        return 1;
    }
    wchar_t buf[32];
    GetPrivateProfileStringW(L"window", L"zoom", L"1", buf, 32, g_uiIni);
    g_zoom = std::clamp(std::round(wcstof(buf, nullptr) * 10.0f) / 10.0f, 0.7f, 2.0f);
    AddStrikeTable();
    AddGameCodeTab();
    g_tab = 0;   // always the Home page first (2026-10-14, the user's wish); the tab last used is no longer restored
    SetWeaponSection(GetPrivateProfileIntW(L"weapon", L"weapon", 1, g_uiIni) + 1);   // the kind last picked by hand
    const int savedW = GetPrivateProfileIntW(L"window", L"width", 0, g_uiIni);
    const int savedH = GetPrivateProfileIntW(L"window", L"height", 0, g_uiIni);

    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"D2RVRSettings";
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));   // d2r_vr.ico (settings.rc)
    RegisterClassW(&wc);
    wc.lpfnWndProc = PageProc;
    wc.hIcon = nullptr;
    wc.lpszClassName = L"D2RVRSettingsPage";
    RegisterClassW(&wc);

    HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"D2RVRSettings", (std::wstring(kAppName) + L" " D2RVR_VERSION_W).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, inst, nullptr);
    {   // the saved size (or three columns wide), no bigger than the monitor's work area, centred on it
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        const RECT& work = mi.rcWork;
        const float dpi = g_dpi / 96.0f;
        const int w = std::min<int>(work.right - work.left, (int)std::lround((savedW > 0 ? savedW : 1400) * dpi));
        const int h = std::min<int>(work.bottom - work.top, (int)std::lround((savedH > 0 ? savedH : 900) * dpi));
        SetWindowPos(wnd, nullptr, work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h, SWP_NOZORDER);
    }
    ShowWindow(wnd, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (m.message == WM_MOUSEWHEEL) {
            if (ctrl) { SetZoom(g_zoom + (GET_WHEEL_DELTA_WPARAM(m.wParam) > 0 ? 0.1f : -0.1f)); continue; }
            // the wheel scrolls the page, never a slider or a list under the pointer (it changed settings by
            // accident) - unless a drop-down is open
            if (!AnyListOpen()) { SendMessageW(g_page, m.message, m.wParam, m.lParam); continue; }
        }
        if (m.message == WM_KEYDOWN && ctrl) {
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            if (m.wParam == VK_OEM_PLUS || m.wParam == VK_ADD) { SetZoom(g_zoom + 0.1f); continue; }
            if (m.wParam == VK_OEM_MINUS || m.wParam == VK_SUBTRACT) { SetZoom(g_zoom - 0.1f); continue; }
            if (m.wParam == '0' || m.wParam == VK_NUMPAD0) { SetZoom(1.0f); continue; }
            if (m.wParam == VK_TAB) { SelectTab(ShownTabIndex() + (shift ? -1 : 1)); continue; }
        }
        if (!IsDialogMessageW(wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return 0;
}
