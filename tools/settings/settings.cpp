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
#include <tlhelp32.h>
#include <winhttp.h>    // the update check

#include <algorithm>
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
#include "d2r_vr_shared.h"   // the bridge's block: is head tracking reaching the game
#include "afr_eye_shared.h"  // FlatVR's head sample: is FlatVR running
#include "game_sigs.h"       // the game addresses the mod looks for (the Status tab)
#include "d2r_vr_version.h"  // D2RVR_VERSION: vrcam's g_info_version, made by tools/settings/version.cmake

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

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
    struct Need { const wchar_t* section; const wchar_t* key; float value; int item; };
    std::vector<Need> needs;                 // greyed out unless every such key has its value (item: set by Build)
    int tab = 0;                             // set by Build
    int modes = kBoth;                       // shown in flat, VR or both (Build folds in its tab's and group's)
    std::vector<HWND> radios;                // Radio: one button per value
    int soon = 1 << 20;                      // Radio: the choices from this one on are coming soon - shown, greyed, never picked
    unsigned soonMask = 0;                   // Radio: these choices (bit k = choice k) are coming soon as well, wherever they sit
    COLORREF color = 0;                      // Status: the line's colour now
    bool hidden = false;                     // left out of the page for now (the update row until there is one)
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
// Left out of the page until the code shows it (Item::hidden).
Item Hide(Item i) { i.hidden = true; return i; }

// Greyed out while section/key is not `value` (a tick box: 1 = ticked); nests for several.
Item Needs(const wchar_t* section, const wchar_t* key, float value, Item i) {
    i.needs.push_back({section, key, value, -1}); return i;
}

constexpr const wchar_t* kSkyPicture = L"A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
                                       L"Empty (x) = the shader draws this sky itself.";
constexpr const wchar_t* kFogColour = L"The fog colour of this act, its caves included; the low part of the sky sinks into it. "
                                      L"Empty (x) = the sky's horizon colour, or dark without a sky.";

// Order = layout order: a Tab starts a tab, a Group a group on it.
std::vector<Item> g_items = {
    Tab(L"Home"),
    Group(L"How you play"),
    // Flat is open for players since 2026-10-06 (greyed "coming soon" 2026-10-05 .. 10-06).
    Radio(L"mode", L"platform", 0, 1,
          {L"Flat: on the monitor, mouse and keyboard", L"VR: in the headset, with BodyWalk and FlatVR"},
          L"VR needs BodyWalk with the D2R Bridge plugin and FlatVR - the Status column says what is missing."),
#if D2RVR_FIRST_PERSON
    Only(kFlat, Group(L"View (F1 - F3 in the game)")),
    Radio(L"mode", L"flat_view", 1, 3,
          {L"F1  The game's own camera", L"F2  Third person", L"F3  First person: mouse look, W A S D"},
          L"Switch in the game with F1, F2, F3 (F12 goes round). The game does not get these keys: put its skills on others."),
    Only(kVr, Group(L"View (F1 - F4 in the game)")),
    Radio(L"mode", L"vr_mode", 1, 1,
          {L"F1  From above", L"F2  Third person", L"F3  The game on your floor: look down at it and walk round it (mixed reality)",
           L"F4  First person: the body follows yours (controllers)"},
          L"Switch in the game with F1 - F4 (F12 goes round). F3: the game lies on the floor of your room. "
          L"F4: arms on the controllers, the trunk held. The game does not get F1 - F4."),
#else
    Only(kFlat, Group(L"View (F1, F2 in the game)")),
    Soon(2, Radio(L"mode", L"flat_view", 1, 1,
          {L"F1  The game's own camera", L"F2  Third person", L"F3  First person - coming soon"},
          L"Switch in the game with F1, F2 (F12 goes round). The game does not get these keys: put its skills on others.")),
    Only(kVr, Group(L"View (F1 - F3 in the game)")),
    Soon(3, Radio(L"mode", L"vr_mode", 1, 1,
          {L"F1  From above", L"F2  Third person", L"F3  The game on your floor: look down at it and walk round it (mixed reality)",
           L"F4  First person: the body follows yours - coming soon"},
          L"Switch in the game with F1 - F3 (F12 goes round). F3: the game lies on the floor of your room. "
          L"The game does not get F1 - F4.")),
#endif
    Only(kVr, Group(L"Status")),
    Status(L"reshade", L"ReShade and the mod's effect"),
    Status(L"depth_addon", L"FlatVR depth add-on"),
    Status(L"game_video", L"The game's video settings"),
    Status(L"bw_installed", L"BodyWalk installed"),
    Status(L"vigem", L"Xbox controller driver (ViGEmBus)"),
    Status(L"bw_running", L"BodyWalk running"),
    Status(L"bridge", L"D2R Bridge plugin"),
    Status(L"universal", L"Universal tracking output"),
    Status(L"flatvr_on", L"FlatVR switched on"),
    Status(L"head_lock", L"Head Lock follows the game"),
    Status(L"frame_gen", L"Frame generation off"),
    Status(L"flatvr_running", L"FlatVR running"),
    Status(L"head", L"Head tracking reaches the game"),
    Status(L"game", L"The game runs with the mod"),
    Button(L"@update", L"check_now", L"Check for updates",
           L"Asks bodywalkvr.com now, even with the check at start switched off. Nothing is downloaded."),
    // The version and the update check (StartUpdateCheck). [update] is the settings program's
    // own: vrcam never reads it.
    Group(L"D2R VR"),
    Status(L"version", L"D2R VR " D2RVR_VERSION_W),
    Status(L"update", L"Update check is off"),
    Hide(Status(L"changelog", L"")),
    Hide(Button(L"@update", L"download", L"Download",
           L"Opens the download page in your browser. This program never downloads or runs anything itself.")),
    Toggle(L"update", L"check", L"Check for updates when this program opens", 1,
           L"Asks bodywalkvr.com for the newest version of the mod. Nothing is downloaded."),
    Needs(L"update", L"check", 1, Toggle(L"update", L"develop", L"Beta versions too (develop channel)", 0,
           L"Also offers test builds before they are released to everyone.")),
    // Start BodyWalk from here, so nobody hunts for its shortcut: shown while a
    // BodyWalk is found and not running (RefreshStatus, BodyWalkExe).
    Only(kVr, Hide(Button(L"@run", L"bodywalk", L"Start BodyWalk",
           L"Starts BodyWalk - the one in the game's folder that D2R VR Setup put in, or your installed one."))),
    // FlatVR's own START / STOP, so nobody hunts for them in BodyWalk: shown
    // while BodyWalk runs with the bridge (its events exist), one at a time.
    Only(kVr, Hide(Button(L"@run", L"flatvr_start", L"Start FlatVR",
           L"Starts FlatVR in BodyWalk - the picture goes into the headset. The same as START FLATVR on BodyWalk's FlatVR tab."))),
    Only(kVr, Hide(Button(L"@run", L"flatvr_stop", L"Stop FlatVR",
           L"Stops FlatVR in BodyWalk. The same as STOP FLATVR on BodyWalk's FlatVR tab."))),

    Tab(L"Camera"),
    // One group per view (Home, F1 - F4 in VR, F1 - F3 flat), the shared ones first.
    Group(L"Every view"),
    Only(kVr, Toggle(L"camera", L"fov_from_flatvr", L"Field of view from the FlatVR screen", 1,
           L"The field of view follows the size and distance of the FlatVR screen in BodyWalk, like an OpenXR game's view.")),
    Needs(L"camera", L"fov_from_flatvr", 0, Slider(L"camera", L"fov", L"Field of view, °", 20, 120, 1, 60,
           L"Vertical field of view of the game camera, used when the box above is off.")),
    Slider(L"camera", L"pitch", L"Horizon (lower = looks higher)", -120, 30, 1, -45,
           L"Pitch on top of the game's own camera, degrees. -60 = level with the horizon."),
    Only(kVr, Toggle(L"head", L"yaw_from_head", L"Turn with the headset", 1, L"Needs BodyWalk with the D2R Bridge plugin.")),
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
    Group(L"F3  First person"),
    Slider(L"camera", L"near", L"Near clip (hides the hero's head)", 0, 5, 0.1f, 1.5f,
           L"Nothing closer than this is drawn: hides the hero's own head and hair. World units, about 0.24 m each."),
    Toggle(L"camera", L"height_auto", L"Eye height from the hero's class", 1,
           L"First person: the eyes at the hero's own height, taken from the model (the sorceress is the shortest, "
           L"the druid the tallest). The world's scale follows it."),
    Needs(L"camera", L"height_auto", 1, Slider(L"camera", L"eye_offset", L"Eye height correction", -2, 2, 0.05f, 0,
           L"Added to the hero's eye height. World units, about 0.24 m each.")),
    Needs(L"camera", L"height_auto", 0, Slider(L"camera", L"height", L"Fixed eye height", 0, 15, 0.1f, 6.5f,
           L"Eye point above the ground under the hero, used when the class height is off. World units, about 0.24 m each.")),
    Slider(L"camera", L"distance", L"Camera ahead (-) / behind (+)", -5, 10, 0.1f, -1,
           L"Camera behind the eye point; below 0 = in front of the face."),
    Slider(L"camera", L"side", L"Eyes left (-) / right (+)", -3, 3, 0.05f, 0,
           L"The hero's head is off the model's centre line; without this the eye can sit on a shoulder."),
    Slider(L"camera", L"forward", L"Eyes ahead (+) along the ground", -3, 3, 0.05f, 0,
           L"Eye point ahead (+) / behind (-) along where you look, without tilting with the head."),
    Toggle(L"camera", L"neck_model", L"Head turns about the neck", 1,
           L"Like a real head: it turns about the top of the neck, below and behind the eyes. Level, the eyes sit in front of "
           L"the neck; looking down they move forward and down and see the chest, not the neck."),
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
    Needs(L"turn", L"right_stick", 1, Slider(L"turn", L"speed", L"Turn speed, °/s", 10, 360, 5, 120, L"Smooth turn speed at full deflection.")),
    Needs(L"turn", L"right_stick", 1, Slider(L"turn", L"snap", L"Snap turn, ° (0 = smooth)", 0, 90, 5, 0, L"Above 0 = turn by this many degrees per flick instead of smoothly.")),
    Needs(L"turn", L"right_stick", 1, Invert(L"turn", L"sign", L"Reverse turning", 1, L"Tick if pushing right turns you left.")),
    Group(L"Mouse and keyboard (no headset)"),
    Only(kVr, Toggle(L"input", L"mouse_look", L"Mouse look in VR views F2 and F4 too", 0,
           L"Flat and VR view F3 always start with it. The mouse turns the view (the pointer stays in the middle as a crosshair, "
           L"clicks go where you look), W A S D walk. F9 switches it in the game - off frees the pointer for menus.")),
    Needs(L"input", L"mouse_look", 1, Slider(L"input", L"mouse_speed", L"Mouse speed, ° per pixel", 0.02f, 1, 0.01f, 0.15f)),
    Only(kFlat, File(L"input", L"crosshair", L"Crosshair picture", L"D2R_Sky_ours\\D2R_Crosshair.png",
           L"The pointer while the mouse looks: your own picture (png, jpg, bmp), its middle is where you aim; it is "
           L"fitted into 32-64 pixels by the window's height. A picture with no transparency: its black is see-through. "
           L"A bare name is looked for in reshade-shaders\\Textures; a full path works too. Empty (x) = the built-in one.")),
    Only(kVr, Group(L"Input")),
    Toggle(L"input", L"bodywalk_pad", L"Gamepad straight from BodyWalk", 1,
           L"The game's gamepad is BodyWalk's own report, read directly: no virtual gamepad (ViGEm) needed."),
    Choice(L"input", L"inventory_button", L"\"D2R: Inventory\" presses", 0, {L"Menu (Start)", L"View (Back)"},
           L"The pad button BodyWalk's \"D2R: Inventory\" action sends to the game. In D2R's pad layout View opens the map."),
    Toggle(L"input", L"a_attack_only", L"A button only attacks", 0,
           L"Picking up, opening and talking then go to the BodyWalk action \"D2R: Pick up / interact\"."),

    Only(kVr, Tab(L"Stereo")),
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
    Toggle(L"stereo", L"swap", L"Swap eyes", 0, L"Tick if the eyes come out swapped (depth looks inside out)."),
    Group(L"Scale and depth"),
    Toggle(L"stereo", L"true_scale", L"Life-size world", 1,
           L"Eye distance and the screen plane from the FlatVR screen and your height. "
           L"The two sliders below it are then unused."),
    Needs(L"stereo", L"true_scale", 1, Slider(L"stereo", L"eye_mm", L"My eye distance, mm", 45, 80, 0.5f, 63)),
    Needs(L"stereo", L"true_scale", 1, Slider(L"@bodywalk", L"userHeight", L"My height, m (BodyWalk)", 1.2f, 2.3f, 0.01f, 1.75f,
           L"Your height as BodyWalk knows it (Mapping > Avatar > Body Height): the same number, changed in both places. "
           L"The game takes it from BodyWalk; with BodyWalk not running it is kept here and used until it is.")),
    Needs(L"stereo", L"true_scale", 0, Slider(L"stereo", L"ipd", L"Eye separation", 0, 1, 0.01f, 0.2f,
           L"World units (the hero is ~7.5 tall, so 0.27 is about 6.4 cm). More = stronger 3D.")),
    Needs(L"stereo", L"true_scale", 0, Slider(L"stereo", L"convergence", L"Depth behind the screen", 0, 100, 0.5f, 15,
           L"The distance that sits on the FlatVR screen; farther goes deeper behind it. "
           L"0 = parallel eyes (everything in front of the screen). Smaller = deeper.")),
    Group(L"Timing"),
    Slider(L"stereo", L"pipeline_depth", L"Pipeline depth (smoother head turns)", 0, 3, 1, 0,
           L"How many of an eye's views the shown frame lags the newest one. Try 0, 1 and 2 for the smoothest head turn."),

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
    Slider(L"@game", L"Safe Screen Percent", L"Game interface size, % (restart)", 50, 100, 1, 100,
           L"Below 100 the game lays its whole interface out in a smaller box in the middle of the screen. "
           L"Read when the game starts: change it with the game closed, or the game puts its own value back on exit."),
    Needs(L"stereo", L"afr", 1, Slider(L"stereo", L"ui_near", L"Interface nearer (0 = on the screen)", 0, 10, 0.1f, 0,
           L"Real stereo only: the HUD is moved apart per eye by the shader, % of the half screen.")),
    Slider(L"stereo", L"ui_top", L"Interface top edge", 0.4f, 1, 0.01f, 0.7f,
           L"The HUD is looked for below this height: 0 = top of the screen, 1 = bottom."),

    Group(L"Item labels on the ground"),
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

    // VR F3, the game on the floor: [hud_floor]. Until 2026-10-05 this view used
    // F2's [hud_third] (vrcam runs it as view 2); its screen is head-locked, so
    // the toolbar stays in view wherever you look - hence size, depth and place.
    Group(L"Item labels on the ground"),
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
    Group(L"Item labels on the ground"),
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

    Only(kVr, Tab(L"UI: F4 body")),
    // Read by BodyWalk's D2R Bridge (not vrcam), which asks FlatVR for it (host API 6).
    Group(L"Item labels on the ground"),
    Slider(L"hud", L"labels_alpha", L"Item labels: opacity", 0, 1, 0.05f, 1,
           L"How much of the dark box behind the names of items on the ground is drawn in first person (F4): 1 = as the game draws "
           L"it, 0 = no box, the names alone. The names stay bright. Through the game's label code (vrcam's log says "
           L"\"item-label hook in\"); on another game build it does nothing here."),
    Slider(L"hud", L"labels_size", L"Item labels: size, %", 20, 150, 5, 100,
           L"The names of items on the ground and their boxes in first person (F4), smaller or larger, each about the point under it. "
           L"100 = the game's own. Item tooltips and all other text keep their size."),
    Group(L"Panels (inventory, trade...)"),
    Slider(L"screen", L"menu_distance_m", L"Screen distance while a panel is open (inventory, trade...), m", 0, 8, 0.1f, 1.6f,
           L"While the inventory, a trader, the stash or another panel is open in first person (F4), FlatVR moves its "
           L"screen this far from your eyes, so the whole panel is in view, and brings it back to your own distance when "
           L"the panel closes - smoothly, in under half a second. Your FlatVR Distance setting is not changed. A screen "
           L"already farther away than this comes nearer instead. 0 = the screen stays where it is. Needs BodyWalk with "
           L"the D2R Bridge plugin and FlatVR's \"Screen distance follows the game\" (on by default)."),
    Group(L"In the game's picture"),
    Choice(L"hud", L"map", L"Corner map", 0, {L"In the picture", L"Out of the picture (its own texture)"},
           L"The game draws the corner map under a scissor of its own: its draws go to a texture of their own, the labels "
           L"over monsters stay in the picture - those over the map too."),
    Needs(L"hud", L"map", 1, Choice(L"hud", L"map_corner", L"Corner map is", 0, {L"Found by itself", L"Top left", L"Top right"},
           L"Where the game puts its mini map (the game's own option). Found by itself: either top corner.")),
    Choice(L"hud", L"hide", L"Hide the game's interface", 0, {L"Nothing", L"The toolbar", L"All of it"},
           L"The game draws its whole interface into a layer of its own. \"The toolbar\" empties only the toolbar's strip "
           L"(found from the screen's size and the game's interface size); "
           L"labels over monsters, tooltips and shops stay. \"All of it\": the world alone."),
    Toggle(L"hud", L"colors_as_monitor", L"Colours as on the monitor", 1,
           L"Flip it if the toolbar and the map come out too dark or washed out in the headset."),
    Group(L"Toolbar in the headset"),
    Choice(L"hud", L"bar_place", L"Where", 2, {L"Not shown", L"Left forearm", L"Right forearm", L"Low in front", L"Chest, low"},
           L"Where FlatVR hangs the toolbar taken out of the picture (Hide the game's interface: The toolbar). "
           L"On a forearm it lies along the inside of the arm: turn the palm up to read it."),
    Slider(L"hud", L"bar_width", L"Length, m", 0.1f, 1.0f, 0.01f, 0.22f),
    Choice(L"hud", L"bar_split", L"Cut in two", 0, {L"No - one strip", L"Yes, the red half on one side", L"Yes, the red half on the other side"},
           L"The strip cut at the middle and the halves laid side by side: the blue orb's half stays where it was, the red "
           L"orb's half turned end for end beside it, so both orbs sit together at the wrist. Half as long, twice as wide. "
           L"If they end up at the elbow, Rotate Z 180."),
    Slider(L"hud", L"bar_along", L"Move X (along the arm), cm", -15, 25, 0.5f, 0, L"+ further up the arm, - towards the hand."),
    Slider(L"hud", L"bar_side", L"Move Y (across the arm), cm", -20, 20, 0.5f, 0, L"Sideways in the strip's own plane."),
    Slider(L"hud", L"bar_lift", L"Move Z (off the arm), cm", -10, 20, 0.5f, 0, L"+ further from the arm (or the chest), - closer."),
    Slider(L"hud", L"bar_roll", L"Rotate X (round the arm), °", -180, 180, 1, 0, L"Turns the strip round its own length - on a forearm, round the arm."),
    Slider(L"hud", L"bar_tip", L"Rotate Y (tip the far end), °", -90, 90, 1, 0, L"Raises or lowers the end towards the hand."),
    Slider(L"hud", L"bar_spin", L"Rotate Z (in its plane), °", -180, 180, 1, 0, L"Spins the strip flat on the arm; 180 = the other way round."),
    Group(L"Map in the headset"),
    Choice(L"hud", L"map_place", L"Where", 1, {L"Not shown", L"Left hand", L"Right hand", L"Low in front", L"Chest, low"},
           L"Where FlatVR hangs the corner map taken out of the picture (Corner map: Out of the picture). "
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
    Choice(L"render", L"solid_walls", L"Walls in front of the hero", 1, {L"See-through (the game's)", L"Solid, see-through only in F1", L"Solid in every view"},
           L"The game draws a wall between its camera and the hero at half alpha. From inside every wall you look at "
           L"was see-through; from above it hid the hero."),
    Group(L"Render distance"),
    Slider(L"render", L"rings", L"Room rings around the hero", 1, 8, 1, 4, L"Extra room rings around the hero. With fog, 4-6 is enough."),
    Slider(L"render", L"model_radius", L"Model visibility radius", 150, 6000, 50, 1500, L"The game's own is 150."),

    Tab(L"Sky"),
    Group(L"Sky"),
    Toggle(L"sky", L"enabled", L"Sky outdoors (via ReShade)", 0,
           L"A sky in the black void beyond the world, through the fog shader. Only in the open air; "
           L"hidden while a menu or side panel is open."),
    Needs(L"sky", L"enabled", 1, Slider(L"sky", L"clouds", L"Clouds (0 = clear)", 0, 2, 0.05f, 1, L"Times each area's own cloud cover.")),
    Needs(L"sky", L"enabled", 1, Toggle(L"sky", L"drift", L"The sky turns slowly (clouds drift)", 0,
           L"The painted sky goes slowly round you. Off: it stands still - the stars painted into the night skies went round with the clouds.")),
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
           L"First person only; for now the caves of act 1."),
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
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"torches", L"Torches light the ceiling", 1,
           L"Fire seen in the picture (torches, braziers, fire spells) lights the ceiling over it; "
           L"remembered for a few seconds after it leaves the view.")),
    Needs(L"ceiling", L"torches", 1, Slider(L"ceiling", L"torch_brightness", L"Torch glow", 0, 3, 0.05f, 0.6f,
           L"How bright the torches' light on the ceiling is. A small flame gives less light than a big one.")),
    Needs(L"ceiling", L"torches", 1, Slider(L"ceiling", L"torch_radius", L"Torch light reach", 3, 100, 1, 20,
           L"World units from a fire where its light on the ceiling is down to half.")),
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
    Group(L"Ceiling: act 1 caves"),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"height_act1_caves", L"Height over the floor", 5, 150, 0.1f, 30,
           L"World units (the hero is ~7.5 tall). Below the walls' tops they pierce the ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"brightness_act1_caves", L"Brightness (0 = black)", 0, 3, 0.05f, 1,
           L"Lower = a darker ceiling.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"light_radius_act1_caves", L"Hero's light reach", 5, 200, 1, 25,
           L"World units from the hero where his light on the ceiling is down to half; farther it goes dark.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_caves", L"Relief (0 = flat)", 0, 30, 0.5f, 6,
           L"How far the uneven rock hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_caves", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_caves_walls.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = D2R_Ceiling_act1_caves.png, else the caves' stone.")),
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
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_crypt", L"Relief (0 = flat)", 0, 30, 0.5f, 1.5,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_crypt", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = D2R_Ceiling_act1_crypt.png, else the caves' stone.")),
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
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_barracks", L"Relief (0 = flat)", 0, 30, 0.5f, 1.5,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_barracks", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = D2R_Ceiling_act1_barracks.png, else the caves' stone.")),
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
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_catacombs", L"Relief (0 = flat)", 0, 30, 0.5f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_catacombs", L"Picture", L"D2R_Sky_ours\\D2R_Ceiling_act1_crypt_brick.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\\Textures; a full path works too. "
           L"Empty (x) = D2R_Ceiling_act1_catacombs.png, else the caves' stone.")),
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
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"relief_act1_cathedral", L"Relief (0 = flat)", 0, 30, 0.5f, 0,
           L"How far the uneven masonry hangs down from the ceiling, world units.")),
    Needs(L"ceiling", L"enabled", 1, File(L"ceiling", L"texture_act1_cathedral", L"Picture", L"D2R_Sky_ours\D2R_Ceiling_act1_barracks_smooth.png",
           L"A square picture that tiles. A bare name is looked for in reshade-shaders\Textures; a full path works too. "
           L"Empty (x) = D2R_Ceiling_act1_cathedral.png, else the caves' stone.")),
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
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"pillars_act1_cathedral", L"Vault on the columns", 1,
           L"The ceiling comes down onto the columns and walls the game draws, and rises between them in ribbed vaults. "
           L"Height is the crown; Relief is how far below it the vault rests on the columns; Bay width is twice how far "
           L"from a column it reaches the crown.")),
    Needs(L"ceiling", L"pillars_act1_cathedral", 1, Slider(L"ceiling", L"column_min_act1_cathedral", L"Column top lowest", 5, 100, 0.5f, 20,
           L"World units over the floor. A column is where the game's highest point in a spot lies between this and the highest "
           L"below; the vault rests on its top. ReShade's Show ceiling cut paints that band cyan and the columns found yellow.")),
    Needs(L"ceiling", L"pillars_act1_cathedral", 1, Slider(L"ceiling", L"column_max_act1_cathedral", L"Column top highest", 5, 100, 0.5f, 30,
           L"What reaches higher (the altar's spire) is not a column. The vault rises from the columns' tops to "
           L"Height over the floor.")),
    Needs(L"ceiling", L"pillars_act1_cathedral", 1, Slider(L"ceiling", L"column_width_act1_cathedral", L"Column top width at least", 0, 5, 0.1f, 1,
           L"How wide every way a column's top must be (the spread of its points, world units). Crosses and wall tops are "
           L"flat one way: raise it when the vault comes down onto them, lower it when a column is missed.")),
    Needs(L"ceiling", L"pillars_act1_cathedral", 1, Slider(L"ceiling", L"column_lift_act1_cathedral", L"Vault above the column top", -5, 20, 0.5f, 0,
           L"World units: how far over a column's top the vault starts. Raise it when the capital's top is cut off.")),
    Needs(L"ceiling", L"pillars_act1_cathedral", 1, Slider(L"ceiling", L"column_radius_act1_cathedral", L"Column thickness", 0, 15, 0.5f, 4,
           L"World units from a column's middle to where the vault rests on it.")),
    Needs(L"ceiling", L"enabled", 1, Toggle(L"ceiling", L"vault_act1_cathedral", L"Groin vault", 1,
           L"Pointed stone vaults over square bays, ribs along their edges and diagonals, instead of a flat ceiling. "
           L"Height is the crown; Relief is how far the vault comes down from it to the corners of the bays.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"bay_act1_cathedral", L"Bay width", 10, 120, 1, 40,
           L"World units between the vault's corners - set it to the columns' spacing.")),
    Needs(L"ceiling", L"vault_act1_cathedral", 1, Slider(L"ceiling", L"bay_x_act1_cathedral", L"Bays moved along X", -60, 60, 0.5f, 0,
           L"Moves the bays so their corners stand on the game's columns.")),
    Needs(L"ceiling", L"vault_act1_cathedral", 1, Slider(L"ceiling", L"bay_z_act1_cathedral", L"Bays moved along Z", -60, 60, 0.5f, 0,
           L"Moves the bays so their corners stand on the game's columns.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"rib_width_act1_cathedral", L"Rib width", 0, 6, 0.1f, 1.2f,
           L"World units; 0 = no ribs.")),
    Needs(L"ceiling", L"enabled", 1, Slider(L"ceiling", L"rib_depth_act1_cathedral", L"Rib depth", 0, 4, 0.1f, 0.8f,
           L"How far the ribs stand out under the vault, world units.")),
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
    Only(kVr, Group(L"Attack along the hand (A button)")),
    Choice(L"bow", L"mode", L"Aiming", 4,
           {L"Off", L"Left stick while A is held", L"Shift + click along the hand", L"Turn the hero to the hand first",
            L"Attack vector along the hand"},
           L"Shift + click works, but flips the game's interface to the mouse. "
           L"Turn first: the stick turns the hero to the hand, then A goes through (gamepad only). "
           L"Attack vector: while A is held the game's own attack points along the hand - turn the hand while firing."),
    Needs(L"bow", L"mode", 3, Slider(L"bow", L"turn_ms", L"Turn before the attack, ms", 20, 300, 10, 80,
           L"\"Turn the hero first\" only: how long the hero is turned before the attack. More if he does not turn in time.")),
    Toggle(L"bow", L"xbow_two_hands", L"Crossbow held like the staff", 1,
           L"The right hand holds the crossbow on the stock and aims it alone; the left takes it with the grip (ahead of the right) "
           L"and then it aims along the line from the right hand to the left. Off: as a bow, the game's own animation."),
    Needs(L"bow", L"xbow_two_hands", 1, Choice(L"hands", L"xbow_hand", L"The game hangs the crossbow on", 1, {L"the right wrist", L"the left wrist"},
           L"For its grip: if the crossbow sits wrong or turns away when the left hand takes it, try the other.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_line_pitch", L"Crossbow: shot line, tilt up/down, °", -180, 180, 1, 34,
           L"Turns the line the bolt flies along and the left hand takes - the red line ([debug] phantom_ray) - not the model. Lay it on the crossbow.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_line_yaw", L"Crossbow: shot line, turn left/right, °", -180, 180, 1, 15)),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_at_cm", L"Crossbow: left hand holds it, cm ahead of the right", 5, 80, 1, 30,
           L"The one place the left hand takes the crossbow (under the stock, just behind the bow); it does not slide. "
           L"The grip takes it only with the left hand that near this place.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_roll", L"Crossbow: left hand turned about the stock, °", -180, 180, 1, 0,
           L"The left hand on the crossbow turned round the stock: from below, palm up, holding it up.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_x", L"Crossbow: left hand X, cm", -20, 20, 0.5f, 0,
           L"The left hand on the crossbow moved along its own axes, until it sits under the stock.")),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_y", L"Crossbow: left hand Y, cm", -20, 20, 0.5f, 0)),
    Needs(L"bow", L"xbow_two_hands", 1, Slider(L"hands", L"xbow_left_z", L"Crossbow: left hand Z, cm", -20, 20, 0.5f, 0)),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"xbow_range_one", L"Crossbow aim point, one hand, cells", 2, 60, 1, 5,
           L"How far out along the aim the attack point is with the right hand alone: nearer = rougher, the crossbow sways more.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"xbow_range_two", L"Crossbow aim point, two hands, cells", 2, 60, 1, 30,
           L"The same with the left hand on it too: farther = finer, held in two the aim is steady.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"staff_range_one", L"Staff aim point, one hand, cells", 2, 60, 1, 5,
           L"The staff's, with the right hand alone.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"staff_range_two", L"Staff aim point, two hands, cells", 2, 60, 1, 30,
           L"The staff's, with the left hand on it too.")),
    Needs(L"bow", L"mode", 4, Slider(L"bow", L"range", L"Bow aim point, cells", 3, 60, 1, 20,
           L"\"Attack vector\" with a bow: the attack point this many grid cells out along the hand (more = finer aim).")),
    Slider(L"bow", L"aim_yaw", L"Bow aim correction, ° (+ = right)", -30, 30, 1, 0,
           L"Turns the aim about the vertical, if the arrows land beside where the hand points."),
    Slider(L"bow", L"xbow_aim_yaw", L"Crossbow aim correction, ° (+ = right)", -45, 45, 1, 0,
           L"The same for a crossbow, in place of the bow's: it is held like a gun, not round a bow's grip."),
    Slider(L"bow", L"staff_aim_yaw", L"Staff aim correction, ° (+ = right)", -45, 45, 1, 0,
           L"The same for a staff, in place of the one above: turns its aim about the vertical, if spells land beside where the staff points."),
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
    Tab(L"Debug"),
    Group(L"Debug"),
#if D2RVR_FIRST_PERSON
    Toggle(L"debug", L"phantom_ray", L"Draw the line the left hand takes", 0,
           L"A red line along which the left hand takes the weapon in hand and slides (staff, spear, polearm, two-handed "
           L"sword); with a crossbow also where the bolt flies (Crossbow: shot line sliders turn it). "
           L"\"Draw the axes of the weapon in hand\" draws it too."),
    Toggle(L"debug", L"bone_axes", L"Draw the axes of the weapon in hand", 0,
           L"The bone the game hangs the weapon in hand on: its own axes from its origin, 0.5 m each - X green, Y blue, Z yellow; and the red line the left hand takes it by. "
           L"Weapon Adjust's \"Turn about X / Y / Z\" turn the weapon about these."),
#endif
    Toggle(L"debug", L"frame_log", L"Log every frame", 0,
           L"Every pair, view, hero pose and present to d2r_vr_frames.csv beside the ini, and FlatVR its per-frame pose trace."),
    Toggle(L"debug", L"matrix_writer", L"Find who writes the hero matrix", 0,
           L"Switched on: for 3 s, while walking, marks where the game writes the hero's model matrix "
           L"(d2r_vr_matrix_writers.csv beside the ini, a summary in the log). Turn it off and on again for another run."),
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
HFONT g_font = nullptr, g_bold = nullptr;
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

// reshade-shaders\Textures next to the game: the ini sits in <game>\d2rloader\plugins.
std::wstring TexturesDir() {
    std::wstring d = g_ini;
    for (int up = 0; up < 3; ++up) { const size_t s = d.find_last_of(L'\\'); if (s == std::wstring::npos) return L""; d.resize(s); }
    return d + L"\\reshade-shaders\\Textures";
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

float ReadValue(const Item& it) {
    if (wcscmp(it.section, L"@game") == 0) { float v = it.def; ReadGameValue(it, &v); return v; }
    if (wcscmp(it.section, L"@bodywalk") == 0) return ReadBodyWalkValue(it);
    wchar_t buf[64], def[64];
    swprintf_s(def, L"%g", it.def);
    // [hud_top] / [hud_third] / [hud_floor] / [hud_inside]: a view's own interface; unset, the
    // value all views shared before, in [hud] (vrcam reads it the same way).
    if (wcsncmp(it.section, L"hud_", 4) == 0) {
        GetPrivateProfileStringW(it.section, it.key, L"#", buf, 64, g_ini);
        if (wcscmp(buf, L"#") == 0) GetPrivateProfileStringW(L"hud", it.key, def, buf, 64, g_ini);
    } else
    GetPrivateProfileStringW(SectionOf(it), it.key, def, buf, 64, FileOf(it));
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? v : it.def;
}

void WriteValue(const Item& it, float v) {
    if (wcscmp(it.section, L"@game") == 0) { WriteGameValue(it, v); return; }
    if (wcscmp(it.section, L"@bodywalk") == 0) { WriteBodyWalkValue(it, v); return; }
    wchar_t buf[64];
    if (it.step >= 1.0f) swprintf_s(buf, L"%d", (int)std::lround(v));
    else swprintf_s(buf, L"%g", std::round(v / it.step) * it.step);
    WritePrivateProfileStringW(SectionOf(it), it.key, buf, FileOf(it));
}

int Steps(const Item& it) { return (int)std::lround((it.max - it.min) / it.step); }
float FromPos(const Item& it, int pos) { return it.min + pos * it.step; }
int ToPos(const Item& it, float v) { return std::max(0, std::min(Steps(it), (int)std::lround((v - it.min) / it.step))); }

void ShowValue(const Item& it, float v) {
    wchar_t buf[32];
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
            it.ctl = Make(page, L"STATIC", it.label, SS_LEFT | SS_NOPREFIX | (it.key[0] == L'w' && it.key[1] == L'k' ? SS_NOTIFY : 0),
                          it.key[0] == L'w' && it.key[1] == L'k' ? id : -1);   // wraps to two lines
            it.color = GetSysColor(COLOR_GRAYTEXT);
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
    }
    g_tabCount = tab + 1;
    for (Item& it : g_items)
        for (Item::Need& nd : it.needs)
            for (size_t j = 0; j < g_items.size(); ++j)
                if (g_items[j].key && wcscmp(g_items[j].section, nd.section) == 0 && wcscmp(g_items[j].key, nd.key) == 0) nd.item = (int)j;
}

// Greys out what the current values make unused (the FOV slider while the FOV comes from FlatVR...).
void UpdateEnabled() {
    for (const Item& it : g_items) {
        if (it.needs.empty()) continue;
        BOOL on = TRUE;
        for (const Item::Need& nd : it.needs)
            if (nd.item >= 0 && std::lround(ReadValue(g_items[nd.item])) != std::lround(nd.value)) on = FALSE;
        for (HWND h : {it.name, it.ctl, it.value, it.browse, it.clear})
            if (h) EnableWindow(h, on);
        for (HWND h : it.radios) EnableWindow(h, on);
    }
}

bool ShownNow(const Item& it) { return it.kind != Kind::Tab && it.tab == g_tab && (it.modes & PlatformBit()) != 0 && !it.hidden; }

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
        const int c = (int)(std::min_element(colY.begin(), colY.end()) - colY.begin());
        const int x = m + c * (colW + gap);
        int y = colY[c];
        const Item& head = g_items[g.first];
        places.push_back({head.name, x, y + S(4), colW, S(20)});
        places.push_back({head.line, x, y + S(26), colW, 2});
        y += headH;
        for (size_t i = g.first + 1; i < g.end; ++i) {
            const Item& it = g_items[i];
            if (!(it.modes & PlatformBit()) || it.hidden) continue;
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
            case Kind::Slider:
                places.push_back({it.name, x, y + S(6), labelW - S(8), S(20)});
                places.push_back({it.ctl, x + labelW, y + S(2), colW - labelW - valueW - S(4), S(26)});
                places.push_back({it.value, x + colW - valueW + S(4), y + S(6), valueW - S(4), S(20)});
                break;
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
    HFONT oldFont = g_font, oldBold = g_bold;
    g_font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    g_bold = CreateFontIndirectW(&lf);
    SendMessageW(g_tabs, TCM_SETPADDING, 0, MAKELPARAM(S(12), S(4)));   // before the font: the tabs size on WM_SETFONT
    EnumChildWindows(g_main, SetFontProc, (LPARAM)g_font);
    SendMessageW(g_tip, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, S(420));
    for (const Item& it : g_items) {
        if (it.kind == Kind::Group) SendMessageW(it.name, WM_SETFONT, (WPARAM)g_bold, TRUE);
        if (it.kind == Kind::Slider) SendMessageW(it.ctl, TBM_SETTHUMBLENGTH, S(20), 0);
    }
    if (oldFont) DeleteObject(oldFont);
    if (oldBold) DeleteObject(oldBold);
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

void PlatformChanged() {
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

// BodyWalkVR.exe on this PC, "" when there is none: the portable copy D2R VR
// Setup puts in the game's folder (next to this program), else an installer's
// or Steam's uninstall entry - the ones the Setup looks for.
std::wstring BodyWalkExe() {
    wchar_t self[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    while (n && self[n - 1] != L'\\') --n;
    const std::wstring portable = std::wstring(self, n) + L"BodyWalkVR\\BodyWalkVR.exe";
    if (Exists(portable)) return portable;
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
                if (Exists(exe)) return exe;
            }
    return L"";
}

void StartBodyWalk() {
    const std::wstring exe = BodyWalkExe();
    if (exe.empty()) return;
    const std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    ShellExecuteW(g_main, L"open", exe.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

Item* FindItem(const wchar_t* section, const wchar_t* key);

// D2R VR Settings' Start / Stop FlatVR: the bridge's event (D2RVR_FLATVR_START_NAME);
// the bridge asks BodyWalk, which answers on its next frame.
void SignalBridge(const wchar_t* name) {
    if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name)) {
        SetEvent(ev);
        CloseHandle(ev);
    }
}

// The VR status column: what BodyWalk, its D2R Bridge, FlatVR and the game
// say right now. BodyWalk's own settings file, the plugin on disk, and the
// three shared blocks each side writes while it runs.
void RefreshStatus() {
    if (g_platform != 1 || g_tab != 0) return;
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
    // ReShade as the mod wants it beside the game (this program sits next to
    // D2R.exe): ReShade64.dll, which vrcam loads itself - a dxgi.dll is refused
    // by the plain D2R.exe and makes vrcam step aside - plus the mod's effect.
    // And FlatVR's depth add-on, which ReShade loads from the same folder.
    {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        while (n && exe[n - 1] != L'\\') --n;
        const std::wstring game(exe, n);
        if (Exists(game + L"dxgi.dll"))
            SetStatus(L"reshade", kBad, L"dxgi.dll is beside the game: D2R refuses it - rename it to ReShade64.dll");
        else if (!Exists(game + L"ReShade64.dll"))
            SetStatus(L"reshade", kBad, L"ReShade not installed - run D2R VR Setup again");
        else if (!Exists(game + L"reshade-shaders\\Shaders\\D2R_DepthFog.fx"))
            SetStatus(L"reshade", kBad, L"ReShade is there, the mod's effect D2R_DepthFog.fx is not - run D2R VR Setup again");
        else
            SetStatus(L"reshade", kOk, L"ReShade and the mod's effect installed");
        const bool addon = Exists(game + L"FlatVR_DepthProvider.addon64");
        SetStatus(L"depth_addon", addon ? kOk : kBad, addon ? L"FlatVR depth add-on installed"
                                                            : L"FlatVR depth add-on missing beside the game - run D2R VR Setup again");
    }

    // The game's own video settings (Saved Games\Diablo II Resurrected\Settings.json,
    // written by the game when its options close). The mod draws the eyes in turn:
    // anything that builds a frame from the ones before it - DLSS, TAA - mixes the
    // two eyes, and VSync halves the frames each eye gets. "Anti Aliasing": 1 is
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
            if (value("NVIDIA DLSS") > 0) bad += L"DLSS on - switch it off. ";
            if (value("VSync") > 0) bad += L"Vertical Sync on - switch it off. ";
            // Off, FXAA or MSAA are all fine; only TAA (2) mixes the eyes
            // (docs/plan_left_eye_shake.md: "Anti Aliasing": 2 was TAA).
            if (value("Anti Aliasing") == 2) bad += L"Anti-Aliasing is TAA - pick FXAA or MSAA. ";
            const int cap = value("Framerate Cap");
            if (cap > 0 && cap < 180) warn += L"Framerate Cap below 180 (90 for each eye). ";
            if (!bad.empty()) SetStatus(L"game_video", kBad, (L"Game video: " + bad + L"(Options > Video)").c_str());
            else if (!warn.empty()) SetStatus(L"game_video", kWarn, (L"Game video: " + warn).c_str());
            else SetStatus(L"game_video", kOk, L"Game video settings fine: DLSS, VSync and TAA off");
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
    SetStatus(L"bw_installed", have || !bwExe.empty() ? kOk : kBad,
              have || !bwExe.empty() ? L"BodyWalk installed" : L"BodyWalk not found - install it and start it once");
    // BodyWalk's virtual Xbox pad, which the D2R Bridge drives. BodyWalk's own
    // installer and ours put it in; a portable BodyWalk from the zip asks for it.
    HKEY vigem = nullptr;
    const bool pad = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\ViGEmBus", 0,
                                   KEY_READ | KEY_WOW64_64KEY, &vigem) == ERROR_SUCCESS;
    if (vigem) RegCloseKey(vigem);
    SetStatus(L"vigem", pad ? kOk : kBad, pad ? L"Xbox controller driver installed (ViGEmBus)"
                                              : L"Xbox controller driver (ViGEmBus) not installed - BodyWalk offers it in Xbox mode");
    const bool running = ProcessRunning(L"BodyWalkVR.exe");
    SetStatus(L"bw_running", running ? kOk : kBad, running ? L"BodyWalk running" : L"BodyWalk is not running - start it");
    if (Item* start = FindItem(L"@run", L"bodywalk"); start && start->hidden != (running || bwExe.empty())) {
        start->hidden = running || bwExe.empty();
        LayoutPage();
    }

    const bool dll = Exists(dir + L"\\plugins\\d2r_bridge\\d2r_bridge.dll");
    if (!dll) SetStatus(L"bridge", kBad, L"D2R Bridge plugin not installed (BodyWalk\\plugins\\d2r_bridge)");
    else if (have && PluginOff(text, "d2r_bridge")) SetStatus(L"bridge", kBad, L"D2R Bridge plugin is switched off in BodyWalk's plugin list");
    else SetStatus(L"bridge", kOk, L"D2R Bridge plugin installed");

    auto flag = [&](const wchar_t* key, const char* json, bool want, int missState, const wchar_t* good, const wchar_t* bad) {
        if (!have) { SetStatus(key, kUnknown, good); return; }
        const int v = JsonBool(text, json);
        if (v < 0) SetStatus(key, kUnknown, good);
        else SetStatus(key, (v == 1) == want ? kOk : missState, (v == 1) == want ? good : bad);
    };
    flag(L"universal", "universal_tracking_output", true, kBad, L"Universal tracking output on",
         L"Universal tracking output is off - switch it on in BodyWalk (Startup tab)");
    if (have && PluginOff(text, "flatvr")) SetStatus(L"flatvr_on", kBad, L"FlatVR is switched off in BodyWalk's plugin list");
    else flag(L"flatvr_on", "flat_vr_enabled", true, kBad, L"FlatVR switched on", L"FlatVR is off in BodyWalk - switch it on");
    flag(L"head_lock", "flat_vr_head_lock_from_game", true, kWarn, L"Head Lock follows the game",
         L"\"Head Lock follows the game\" is off in FlatVR: the screen will not lock to the head in first person");
    flag(L"frame_gen", "flat_vr_optical_flow", false, kWarn, L"Frame generation off",
         L"FlatVR frame generation is on: the picture shakes in D2R - switch it off");

    FlatVRHeadSample hs{};
    const bool flatvr = ReadBlock(FLATVR_HEAD_SAMPLE_NAME, &hs, sizeof hs) && Moving(FLATVR_HEAD_SAMPLE_NAME, hs.counter);
    // Start / Stop FlatVR: while the bridge's events are there (BodyWalk runs
    // with it), the one that fits FlatVR's state now.
    {
        HANDLE ev = OpenEventW(SYNCHRONIZE, FALSE, D2RVR_FLATVR_START_NAME);
        const bool bridge = ev != nullptr;
        if (ev) CloseHandle(ev);
        bool relayout = false;
        if (Item* s = FindItem(L"@run", L"flatvr_start"); s && s->hidden != (!bridge || flatvr)) { s->hidden = !bridge || flatvr; relayout = true; }
        if (Item* s = FindItem(L"@run", L"flatvr_stop"); s && s->hidden != (!bridge || !flatvr)) { s->hidden = !bridge || !flatvr; relayout = true; }
        if (relayout) LayoutPage();
    }
    SetStatus(L"flatvr_running", flatvr ? kOk : kWarn, flatvr ? L"FlatVR running" : L"FlatVR is not started - press START in FlatVR");
    D2RVR_Shared sh{};
    const bool live = ReadBlock(D2RVR_SHARED_NAME, &sh, offsetof(D2RVR_Shared, headPitchDeg)) && Moving(D2RVR_SHARED_NAME, sh.counter);
    SetStatus(L"head", live && sh.headValid ? kOk : kWarn,
              live && sh.headValid ? L"Head tracking reaches the game" :
              live ? L"The bridge runs, but the headset is not tracking" : L"No head tracking from BodyWalk yet");
    D2RVR_State st{};
    const bool game = ReadBlock(D2RVR_STATE_NAME, &st, sizeof st) && Moving(D2RVR_STATE_NAME, st.counter);
    SetStatus(L"game", game ? kOk : kUnknown, game ? L"The game runs with the mod" : L"The game is not running (or the mod is not loaded)");
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

void LogLine(const std::wstring& line) { OutputDebugStringW((L"[D2R VR Settings] " + line + L"\n").c_str()); }

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
            if (it.kind == Kind::Choice && HIWORD(w) == CBN_SELCHANGE) {
                const int sel = (int)SendMessageW(it.ctl, CB_GETCURSEL, 0, 0);
                WriteValue(it, FromPos(it, sel));
                if (wcscmp(it.section, L"@ui") == 0 && wcscmp(it.key, L"weapon") == 0) ShowWeapon(sel + 1);
                UpdateEnabled();
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button && wcscmp(it.section, L"@update") == 0) {
                if (wcscmp(it.key, L"check_now") == 0) StartUpdateCheck(true);
                else OpenDownload();
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button && wcscmp(it.section, L"@run") == 0) {
                if (wcscmp(it.key, L"bodywalk") == 0) StartBodyWalk();
                else SignalBridge(wcscmp(it.key, L"flatvr_start") == 0 ? D2RVR_FLATVR_START_NAME : D2RVR_FLATVR_STOP_NAME);
            } else if (HIWORD(w) == BN_CLICKED && it.kind == Kind::Button) {
                WriteValue(it, (float)(((int)ReadValue(it) + 1) % 1000000));
            } else if (HIWORD(w) == BN_CLICKED && (it.kind == Kind::Toggle || it.kind == Kind::Invert)) {
                const bool on = SendMessageW(it.ctl, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (it.kind == Kind::Toggle) WriteValue(it, on ? 1.0f : 0.0f);
                else WriteValue(it, on ? -1.0f : 1.0f);
                if (wcscmp(it.section, L"@ui") == 0) FollowWeapon();
                UpdateEnabled();
                if (wcscmp(it.section, L"update") == 0) StartUpdateCheck();   // switched on, or the other channel
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
        Build(g_page);
        UpdateEnabled();
        g_platform = GetPrivateProfileIntW(L"mode", L"platform", 1, g_ini) != 0 ? 1 : 0;
        g_tab = std::clamp(g_tab, 0, std::max(0, g_tabCount - 1));
        RebuildTabs();
        ApplyScale();
        SetTimer(wnd, 1, 500, nullptr);   // the weapon in hand, the view keys, the status column
        FollowWeapon();
        RefreshStatus();
        StartUpdateCheck();
        return 0;
    }
    case WM_APP_UPDATE: OnUpdateResult((UpdateResult*)l); return 0;
    case WM_TIMER: {
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
// bridge), FlatVR on, Head Lock follows the game, frame generation off (it
// shakes the picture in D2R) - and the D2R Bridge plugin not switched off.
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
        EnablePlugin(text, "d2r_bridge");
        EnablePlugin(text, "\"flatvr\"");
        std::ofstream f(argv[i], std::ios::binary | std::ios::trunc);
        f << text;
        if (!f.good()) ++failed;
    }
    return failed ? 1 : found ? 0 : 2;
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
        if (argv) LocalFree(argv);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);   // ShellExecute of the download page asks for it

    // The program sits next to the game's exe (where players look for it, 2026-10-05); the
    // ini stays in d2rloader\plugins beside the mod. An ini right next to it wins (the old place).
    DWORD n = GetModuleFileNameW(nullptr, g_ini, MAX_PATH);
    while (n && g_ini[n - 1] != L'\\') --n;
    g_ini[n] = 0;
    {
        wchar_t here[MAX_PATH];
        wcscpy_s(here, g_ini);
        wcscat_s(g_ini, L"d2r_vr.ini");
        if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(g_ini, here);
            wcscat_s(g_ini, L"d2rloader\\plugins\\d2r_vr.ini");
        }
    }
    wcscpy_s(g_uiIni, g_ini);
    g_uiIni[wcslen(g_uiIni) - wcslen(L"d2r_vr.ini")] = 0;
    wcscat_s(g_uiIni, L"d2r_vr_settings.ini");
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"There is no d2r_vr.ini in d2rloader\\plugins next to this program.\n"
                             L"Put this program next to the game's D2R.exe, with the mod installed.",
                    L"D2R VR Settings", MB_ICONWARNING);
        return 1;
    }
    wchar_t buf[32];
    GetPrivateProfileStringW(L"window", L"zoom", L"1", buf, 32, g_uiIni);
    g_zoom = std::clamp(std::round(wcstof(buf, nullptr) * 10.0f) / 10.0f, 0.7f, 2.0f);
    AddGameCodeTab();
    g_tab = GetPrivateProfileIntW(L"window", L"tab", 0, g_uiIni);
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

    HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"D2RVRSettings", L"D2R VR Settings " D2RVR_VERSION_W, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
