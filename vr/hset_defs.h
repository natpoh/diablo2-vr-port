#pragma once
// The keys the in-headset settings panel offers ([openxr] headset_settings, vr/xr.cpp, docs in
// d2r_vr.ini). One line a key; a new tick box of a feature is one line more. Pages are the panel's tabs
// (at most kMaxPages, at most kRowsPerPage rows each); a toggle is a key with min 0, max 1, step 1.
//
// Twelve pages: View, Options, Life, Look, HUD, Belt, Melee, Left and Right are rows of this table; Video (the VR render size, vr/hset_video.h), Body (the
// T-pose measurement, vr/hset_body.h) and Guide (the first-start guide, vr/first_start.h; vr/hset_scene.h) are drawn by hand; Video and Body own the keys
// at the end of the table, which have no row (kNoPage) - but for the Body page's one table row on top, how you play (standing / seated):
// the panel still reads them and writes the ones that changed, with the same conflict check as every other key.
// Life and Look are the low-life vignette's ([hud] low_hp_*, vr/low_hp.h; D2R VR Settings > UI: F4 body > Low life has the same keys):
// the rows' ranges, steps and defaults are the plugin's (a test keeps them equal), and while one of the two pages is open the plugin
// shows the vignette at half severity so the player sees what he tunes (lowhp::SetPreview). The preview's pulses in the hands are silent: the Look
// page-open preview is nothing: the bottom bar's "Preview 5 s" button (kPreviewBtn) shows the vignette and plays the beat for five seconds on request.
// HUD: the toolbar's type (shared/toolbar_type.h: one stepper over [hud] bar_place and [hud] hide, named by vr/hset_names.h), then the plates "Plates:" -
// the height of the monster's plate on the screen and the tick that takes the plates of ordinary monsters away. D2R VR Settings has them in its group
// "Monster plates" - and the plate lifted over the monster's head ([hud] monster_over_head, monster_bar_gap_cm, vr/plate_world.h LiftAnchor) too, which is
// NOT here: Serg, 2026-10-17, "в шлеме не нужна эта настройка" (a test keeps it out).
//
// Panel after bearstonem/d2r-pcvr's vr_settings.h (MIT): a table of definitions, a draft of the values,
// Apply writes only what changed.

#include <array>
#include <cstdint>

namespace hset {

constexpr int kMaxPages = 12;
constexpr int kRowsPerPage = 6;
// A page is a line more here, in kPageNames and in kMaxPages / kPageCount (the tabs share the panel's width in proportion to their names:
// hset_model.h kTab). After HUD come the belt's page, the hit-by-touch page (Melee) and the two key-binding pages (Left, Right).
enum Page : int {
    kPageView = 0, kPageOptions = 1, kPageVideo = 2, kPageBody = 3, kPageLife = 4, kPageLook = 5, kPageHud = 6,
    kPageBelt = 7, kPageMelee = 8, kPageKeysL = 9, kPageKeysR = 10, kPageGuide = 11
};
constexpr int kPageCount = 12;
constexpr int kNoPage = -1;   // a key with no row of its own: a hand-drawn page reads and writes it

// A set of keys, one bit a key of the table below (what an Apply writes, what a draft has changed).
using Mask = uint64_t;
constexpr Mask Bit(int key) { return Mask(1) << key; }

// What a row's value is: a number or a switch (min / max / step, shown by fmt or off / on), or a pick of the key bindings' list (vr/hset_keys.h: the
// value is the pick's index, one past the list = a line the list does not name; shown by the pick's name, stepped round the list).
enum class DefType : uint8_t { Number, Choice };

struct Def {
    const wchar_t* section;
    const wchar_t* key;
    const char* label;
    const char* detail;
    float def, min, max, step;   // def = what the game assumes when the key is missing from the file
    const char* fmt;             // a printf format for one float, a stepper's shown value ("%.1f m")
    const char* off;             // a toggle's two faces
    const char* on;
    int page;
    bool live = false;           // written by itself a moment after a change, as D2R VR Settings' sliders are (the belt)
    DefType type = DefType::Number;
};

inline constexpr Def kDefs[] = {
    // page 0: how the picture stands
    {L"screen", L"menu_distance_m", "Panel distance", "A panel open in the game: inventory, stash, trader",
     1.6f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"flat_distance_m", "Screen distance", "Menus, loading screens, the flat views F1 and F3",
     2.0f, 0.5f, 10.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"flat_width_m", "Screen width", "How wide the menus and panels stand",
     2.4f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", 0},
    {L"openxr", L"brightness", "Brightness", "Everything in the headset, 1 = as the game draws it",
     1.0f, 0.2f, 1.5f, 0.05f, "%.2f", "", "", 0},
    {L"stereo", L"user_height_m", "Your height", "World and hands scale; BodyWalk's Body Height wins",
     1.75f, 1.10f, 2.30f, 0.01f, "%.2f m", "", "", 0},
    // page 1: options
    {L"openxr", L"laser", "Laser on menus", "Point a controller at a menu and it is the mouse",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    {L"openxr", L"laser_hand", "Laser hand", "The hand the laser comes from",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Left", "Right", 1},
    {L"openxr", L"menu_3d", "Menus as a panel", "The world stays in 3D behind an open menu",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    // How the right stick turns (shared/turn_mode.h, after bearstonem/d2r-pcvr, MIT): the toggle is its own key, so the angle and
    // the speed are both kept when it is switched. An older ini has no `smooth` and says snap=0 for smooth turning: the store
    // reads it that way (hset_store.cpp), and writes `smooth` with `snap` so an angle chosen later does not flip the mode.
    // The panel's speed starts at 30 (a step of 30 from 30 keeps the default 120 on the grid); the ini takes 10 - 720.
    {L"turn", L"smooth", "Smooth turning", "Right stick: turn continuously, or one snap a flick",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", 1},
    {L"turn", L"snap", "Snap turn angle", "Degrees per flick, while smooth turning is off",
     30.0f, 15.0f, 180.0f, 15.0f, "%.0f deg", "", "", 1},
    {L"turn", L"speed", "Smooth turn speed", "Degrees per second, while smooth turning is on",
     120.0f, 30.0f, 720.0f, 30.0f, "%.0f deg/s", "", "", 1},
    // ADD NEW TICK BOXES HERE, one line each (page 0 is full: its sixth row is the game view's F1 - F4 buttons; page 1 is full too: the
    // next key needs a page of its own - kMaxPages and the tabs' places in the tile window and the panel).
    //
    // page 4, Life, and page 5, Look: the low-life vignette ([hud] low_hp_*, vr/low_hp.h; D2R VR Settings > UI: F4 body > Low life edits the same
    // keys). Ranges and defaults are lowhp::Config's, in the ini's units: percent for the threshold and the size. The steps are coarser than D2R VR
    // Settings' sliders (5 percent, 0.05) so the panel's buttons get somewhere in a few presses; a value off the grid in the file is shown on it.
    {L"hud", L"low_hp_vignette", "Low life: red vignette", "The edges beat red while your life is low (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageLife},
    {L"hud", L"low_hp_threshold", "Comes on under", "Share of your maximum life",
     30.0f, 10.0f, 60.0f, 5.0f, "%.0f%%", "", "", kPageLife},
    {L"hud", L"low_hp_intensity_start", "Strength just under it", "How red the edges are as it begins",
     0.25f, 0.05f, 1.0f, 0.05f, "%.2f", "", "", kPageLife},
    {L"hud", L"low_hp_intensity_end", "Strength at the lowest life", "How red the edges are when you are nearly dead",
     0.85f, 0.05f, 1.0f, 0.05f, "%.2f", "", "", kPageLife},
    {L"hud", L"low_hp_saturation", "Red", "A dark grey (0) to the deep red (1)",
     0.8f, 0.0f, 1.0f, 0.05f, "%.2f", "", "", kPageLife},
    {L"hud", L"low_hp_radius", "Size", "How far in from the edge it reaches (view angle)",
     40.0f, 20.0f, 95.0f, 5.0f, "%.0f%%", "", "", kPageLife},
    {L"hud", L"low_hp_softness", "Soft edge", "1 fades all the way in, 0 is a short ramp",
     0.8f, 0.0f, 1.0f, 0.05f, "%.2f", "", "", kPageLook},
    {L"hud", L"low_hp_roundness", "Shape", "2 an oval ... 6 a rounded square",
     2.5f, 2.0f, 6.0f, 0.25f, "%.2f", "", "", kPageLook},
    {L"hud", L"low_hp_gentle", "Gentle", "Slower, softer, less red",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageLook},
    {L"hud", L"low_hp_haptics", "Beat in the controllers", "A pulse in both hands (needs Controller vibration)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageLook},
    {L"hud", L"low_hp_haptics_strength", "Pulse strength", "Stronger the lower your life; this scales it",
     1.0f, 0.1f, 2.0f, 0.1f, "%.1f", "", "", kPageLook},
    // page 7, Belt: where the belt's potions hang on the hero ([hands] belt_models_*, the keys of D2R VR Settings > UI: F4 body > Belt potions;
    // shared with the plugin's placing of them in vrcam.cpp). Up and out are parts of his eye height as there; right / left is a shift of the
    // whole arc across him (+ to his right), turn swings the arc round him (+ to his right), degrees. Live: the plugin follows the ini.
    {L"hands", L"belt_models_up", "Belt height", "How high on you, in parts of your eye height",
     0.57f, 0.3f, 0.8f, 0.01f, "%.2f", "", "", kPageBelt, true},
    {L"hands", L"belt_models_ahead", "Belt distance", "Out from your body: the arc's radius",
     0.13f, 0.05f, 0.4f, 0.01f, "%.2f", "", "", kPageBelt, true},
    {L"hands", L"belt_models_across", "Belt right / left", "Shifted to your right (+) or left (-)",
     0.0f, -0.3f, 0.3f, 0.01f, "%+.2f", "", "", kPageBelt, true},
    {L"hands", L"belt_models_turn", "Belt turn", "Swung round you: to the right (+) or left (-)",
     0.0f, -90.0f, 90.0f, 5.0f, "%+.0f deg", "", "", kPageBelt, true},
    // The grab reach: how far from a bottle a hand's palm may be to light it and take it ([hands] belt_grab_cm, the radius of the zone round each).
    // A wide one steals the grip meant for the weapon - narrower, or the belt lower / farther to the side, and the weapon is the grip's again.
    {L"hands", L"belt_grab_cm", "Belt grab reach", "How near a hand must be to a potion to take it",
     12.0f, 4.0f, 30.0f, 1.0f, "%.0f cm", "", "", kPageBelt, true},
    // The two-button take: [belt] grab_trigger (vr/grab_priority.h) - a belt potion is taken with the trigger squeezed too, the grip alone goes to the weapon.
    // It needs the plugin's own controls ([openxr] direct_input) to read the trigger; the page's line says so when they are off.
    {L"belt", L"grab_trigger", "Take a belt potion with", "A potion needs the trigger too; the grip alone is the weapon's",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Grip", "Trigger + grip", kPageBelt, true},
    // (the spread, [hands] belt_models_arc, and the potions' size, [hands] belt_models_scale, are in D2R VR Settings only)
    //
    // page 8, Melee: the "Hit by touch" group of D2R VR Settings > Strikes - the tick, its two numbers and the trigger's tick, the same keys, ranges and defaults as there
    // ([gameplay] melee_touch, melee_min_speed, melee_stroke_start_cm, melee_touch_only; the plugin follows the ini live, vr/vrcam.cpp, vr/stroke_tuning.h). Live, F4 only.
    // The rest of the Strikes tab (thrown weapons, which contact counts, the kinds, the direct hit) stays in D2R VR Settings.
    {L"gameplay", L"melee_touch", "Hit by touch", "The blade you swing through a monster hits it (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMelee, true},
    {L"gameplay", L"melee_min_speed", "Least blade speed that hits", "Below it a touch does nothing, m/s",
     0.8f, 0.3f, 20.0f, 0.1f, "%.1f m/s", "", "", kPageMelee, true},
    {L"gameplay", L"melee_stroke_start_cm", "Blade moves to start a stroke", "How far it must move to begin one, cm",
     8.0f, 2.0f, 30.0f, 1.0f, "%.0f cm", "", "", kPageMelee, true},
    // The trigger does not attack (a part of hit by touch, on with it - the way back): only the blade's touch does; a bow and a crossbow keep it ([gameplay] melee_touch_only, vr/melee_trigger.h; the plugin's own controls, F4).
    {L"gameplay", L"melee_touch_only", "Trigger does not attack", "Only the blade's touch attacks; a bow shoots (F4)",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMelee, true},
    // The author (2026-10-19): the skill arming on the panel too - [gameplay] melee_skill_armed (vr/melee_arm.h: a melee skill's pad button arms it, every touch is then that skill) and its icon in
    // the mana medallion ([gameplay] melee_skill_armed_icon, vr/armed_icon.h). The same keys and defaults as D2R VR Settings > Strikes; which skills arm ([melee_armed]), the icon's sliders and
    // the bow's icon stay there. Live, F4 only.
    {L"gameplay", L"melee_skill_armed", "Skill button arms the skill", "Every touch is then that skill (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMelee, true},
    {L"gameplay", L"melee_skill_armed_icon", "Armed skill in the mana", "Its icon in the mana medallion",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMelee, true},
    //
    // pages 9 and 10, Left and Right: the key bindings, lines of [direct] (the plugin's own controls, vr/direct_layout.h): the value is a pick of the
    // list D2R VR Settings shows (vr/direct_remap.h, named for the box in vr/hset_keys.h), min / max / def are not used (the list's length is
    // the range, 0 = the first pick, one past the last = a line the list does not name, "Custom"). Not live: a binding is applied with Apply.
    // The keys are the rows of hset::keys::kSlots, in that order (a test holds the two together). Chords and the other long presses stay
    // in D2R VR Settings and the ini.
    {L"direct", L"left_trigger", "Left trigger", "[direct] left_trigger", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"left_x", "Left X", "[direct] left_x", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"left_y", "Left Y", "[direct] left_y", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"left_stick", "Left stick click", "[direct] left_stick", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"left_menu", "Left menu button", "[direct] left_menu", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"left_menu_hold", "Left menu, long press", "[direct] left_menu_hold", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysL, false, DefType::Choice},
    {L"direct", L"right_trigger", "Right trigger", "[direct] right_trigger", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    {L"direct", L"right_a", "Right A", "[direct] right_a", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    {L"direct", L"right_b", "Right B", "[direct] right_b", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    {L"direct", L"right_stick", "Right stick click", "[direct] right_stick", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    {L"direct", L"right_stick_hold", "Right stick, long press", "[direct] right_stick_hold", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    {L"direct", L"right_menu", "Right menu button", "[direct] right_menu", 0.0f, 0.0f, 0.0f, 1.0f, "", "", "", kPageKeysR, false, DefType::Choice},
    // -- no row: the Keys pages' note - the plugin's own controls are off ([openxr] direct_input=0: BodyWalk's mapping is the controls), so these lines do nothing.
    {L"openxr", L"direct_input", "Controls without BodyWalk", "The plugin reads the controllers itself",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Belt page's note - the potions on the belt are off in D2R VR Settings, so its sliders do nothing.
    {L"hands", L"belt_models", "Potions on the belt", "The game's own potion models on the hero's hips",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    //
    // -- no row: the Video page. [render] vr_resolution_px = a square's side in pixels (0 = not used), vr_resolution = the older
    //    mode (0 the window's own size, 1 the headset's recommendation, 2..6 a square); shared/render_size.h reads the two together.
    {L"render", L"vr_resolution", "VR render size: mode", "0 window, 1 headset, 2-6 an older square",
     0.0f, 0.0f, 6.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"vr_resolution_px", "VR render size: side", "A square's side in pixels, 0 = not used",
     0.0f, 0.0f, 4096.0f, 1.0f, "%.0f", "", "", kNoPage},
    // -- no row: the Body page reads the T-pose measurement (the plugin writes it, vr/calibration.cpp) and can clear it.
    {L"calibration", L"use", "Use the measurement", "The measured height and arm span are in use",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"calibration", L"version", "Measurement stored", "1 = a measurement is stored",
     0.0f, 0.0f, 1.0f, 1.0f, "", "No", "Yes", kNoPage},
    {L"calibration", L"eye_height_m", "Measured eye height", "Metres over the floor",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage},
    {L"calibration", L"arm_span_m", "Measured arm span", "Metres, grip to grip",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage},
    // page 3, Body: its one table row, above the hand-drawn part - how the game is played ([calibration] seated, vr/calibration.h; D2R VR
    // Settings > Body has it next to the measurement). Seated: the recentre sets the hero's eye height, Calibrate reads the arm span only. Live.
    {L"calibration", L"seated", "How you play", "Seated: Recentre sets the eye height; Calibrate reads the arm span",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Standing", "Seated", kPageBody, true},
    // page 6, HUD. [hud] bar_place is the toolbar type's row: its value box shows the type's name (vr/hset_names.h), a click steps the type
    // and takes [hud] hide to at least 1 with it (the model, hset_model.h), "In the picture" being hide 0; hide has no row of its own.
    {L"hud", L"bar_place", "Toolbar type", "Life and mana on the wrists, or the whole toolbar",
     5.0f, 0.0f, 5.0f, 1.0f, "%.0f", "", "", kPageHud},
    {L"hud", L"monster_height", "Plates: height on the screen", "+ higher, % of the screen (F4); the bar in the room has its own",
     0.0f, -60.0f, 40.0f, 5.0f, "%+.0f%%", "", "", kPageHud},
    {L"hud", L"ordinary_mob_bars", "Plates: ordinary monsters", "Shown: all plates. Hidden: only champions, bosses, NPCs",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Shown", "Hidden", kPageHud},
    // -- no row: the toolbar type's other key. 0 the toolbar stays in the picture, 1 it is cut out, 2 the whole interface is.
    {L"hud", L"hide", "Interface out of the picture", "0 nothing, 1 the toolbar, 2 all of it",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kNoPage},
};
constexpr int kCount = (int)(sizeof kDefs / sizeof kDefs[0]);
static_assert(kCount <= 64, "a change mask is 64 bits (hset::Mask)");
constexpr int kViewRow = kRowsPerPage - 1;   // the first page's last row is not the table's: the game view's four buttons (hset_model.h)

constexpr bool SameW(const wchar_t* a, const wchar_t* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
// The index in kDefs of a key, -1 if it is not in the table.
constexpr int IndexOf(const wchar_t* section, const wchar_t* key) {
    for (int i = 0; i < kCount; ++i)
        if (SameW(kDefs[i].section, section) && SameW(kDefs[i].key, key)) return i;
    return -1;
}
// The keys the hand-drawn pages work on.
constexpr int kKeyMode = IndexOf(L"render", L"vr_resolution");
constexpr int kKeyPx = IndexOf(L"render", L"vr_resolution_px");
constexpr int kKeyUse = IndexOf(L"calibration", L"use");
constexpr int kKeySmooth = IndexOf(L"turn", L"smooth");   // the turn keys: a legacy ini needs care (hset_store.cpp)
constexpr int kKeySnap = IndexOf(L"turn", L"snap");
constexpr int kKeyBarPlace = IndexOf(L"hud", L"bar_place");   // the toolbar type's two keys
constexpr int kKeyHide = IndexOf(L"hud", L"hide");
constexpr int kKeyBeltOn = IndexOf(L"hands", L"belt_models");     // the Belt page's note
constexpr int kKeyBeltUp = IndexOf(L"hands", L"belt_models_up");
constexpr int kKeyMeleeTouch = IndexOf(L"gameplay", L"melee_touch");   // the Melee page's tick: its two numbers do nothing without it
constexpr int kKeyMeleeArmed = IndexOf(L"gameplay", L"melee_skill_armed");   // the Melee page's skill arming (needs the touch)
constexpr int kKeyGrabTrigger = IndexOf(L"belt", L"grab_trigger");   // the two-button take: the Belt page's last row (it reads the trigger through direct input)
constexpr int kKeyVersion = IndexOf(L"calibration", L"version");
constexpr int kKeyEye = IndexOf(L"calibration", L"eye_height_m");
constexpr int kKeySpan = IndexOf(L"calibration", L"arm_span_m");
constexpr int kKeySeated = IndexOf(L"calibration", L"seated");   // the Body page's row: what the measurement reads and what the page says
static_assert(kKeyMode >= 0 && kKeyPx >= 0 && kKeyUse >= 0 && kKeyVersion >= 0 && kKeyEye >= 0 && kKeySpan >= 0 && kKeySeated >= 0,
              "the Video and Body pages' keys are in the table");
static_assert(kKeySmooth >= 0 && kKeySnap >= 0, "the turn keys are in the table");
static_assert(kKeyBeltOn >= 0 && kKeyBeltUp >= 0 && kKeyGrabTrigger >= 0 && kKeyMeleeTouch >= 0 && kKeyMeleeArmed >= 0, "the belt's and the melee page's keys are in the table");
constexpr int kKeyDirectOn = IndexOf(L"openxr", L"direct_input");   // the Keys pages' note
constexpr int kKeyBindFirst = IndexOf(L"direct", L"left_trigger");  // the twelve binding keys, one after the other (vr/hset_keys.h kSlots)
constexpr int kBindCount = 12;
static_assert(kKeyDirectOn >= 0 && kKeyBindFirst >= 0, "the Keys pages' keys are in the table");
constexpr bool IsChoice(int key) { return key >= 0 && key < kCount && kDefs[key].type == DefType::Choice; }
constexpr bool IsBindKey(int key) { return key >= kKeyBindFirst && key < kKeyBindFirst + kBindCount; }
constexpr int BindSlot(int key) { return key - kKeyBindFirst; }   // 0 .. kBindCount - 1, the index in hset::keys::kSlots

// The keys an Apply writes by themselves (see Def::live).
constexpr Mask LiveMask() {
    Mask m = 0;
    for (int i = 0; i < kCount; ++i) if (kDefs[i].live) m |= Bit(i);
    return m;
}
constexpr Mask kLiveMask = LiveMask();
static_assert(kKeyBarPlace >= 0 && kKeyHide >= 0, "the toolbar type's keys are in the table");

constexpr bool IsToggle(const Def& d) { return d.min == 0.0f && d.max == 1.0f && d.step == 1.0f; }

constexpr int PageCount() { return kPageCount; }
constexpr const char* kPageNames[kMaxPages] = {"View", "Options", "Video", "Body", "Life", "Look", "HUD", "Belt", "Melee", "Left", "Right", "Guide"};
// The low-life vignette's two pages: while one is open the plugin previews the vignette (vr/low_hp_game.h SetPreview).
constexpr bool IsLifePage(int page) { return page == kPageLife || page == kPageLook; }

constexpr int RowsOn(int page) {
    int n = 0;
    for (const Def& d : kDefs) if (d.page == page) ++n;
    return n;
}
static_assert(RowsOn(kPageView) <= kViewRow, "the first page's table rows leave its last row to the game view's buttons");
// The index in kDefs of the row-th key of a page, -1 past the end.
constexpr int KeyAt(int page, int row) {
    int seen = 0;
    for (int i = 0; i < kCount; ++i)
        if (kDefs[i].page == page) {
            if (seen == row) return i;
            ++seen;
        }
    return -1;
}

using Values = std::array<float, kCount>;

}  // namespace hset
