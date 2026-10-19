#pragma once
// first_start_pages.h - the words of the first-start guide's pages (vr/first_start.h), one page of the headset settings' Guide tab each
// (vr/hset_scene.h draws them, both painters). Pure data, so cleanroom/tests/first_start_test.cpp checks them (the count, every line short
// enough for its place).
//
// Every line is what the plugin does with the settings that come with the mod (d2r_vr.ini): the [direct] layout's defaults
// (vr/direct_layout.h DefaultTable, hold_ms 500), [gameplay] melee_touch / melee_touch_only, [openxr] skill_wheel / skill_panel_style /
// headset_settings / skill_rt_a, [hands] belt_grab / bow_draw / throw_by_grip / shoulder_draw, [weight] on, [bow] quiver / quiver_orbs, the F keys
// (vrcam.cpp UpdateThread). A player who changed them reads the defaults here.
// The welcome's steps follow bearstonem/d2r-pcvr's first-load prompt (MIT, steamvr/BODY_CALIBRATION.md); the rest is ours.

#include "first_start.h"

namespace firststart {

constexpr int kRowsMax = 6;

struct Row {
    const char* control;   // the left column: what you press (empty: the line spans the panel)
    const char* does;      // what it does
};
struct PageText {
    const char* title;
    Row rows[kRowsMax];
    const char* nav;       // the foot: how to page from here (under the Back / Next buttons)
};

inline constexpr const char* kNavMiddle = "Point at Back or Next and pull the trigger. Close (or B) ends the guide.";

inline constexpr PageText kPages[kPageCount] = {
    // 0: the measurement's offer (only without a stored measurement)
    {"Welcome to D2R VR - first, measure yourself",
     {{"", "Your eye height sets the world's scale; your arm span fits the hero's arms to yours. It takes a few seconds."},
      {"", "1. Stand upright where you play, eyes ahead. The headset's floor (its room setup) must be set."},
      {"", "2. Stretch both arms out sideways at shoulder height, like a T."},
      {"", "3. Let go of both triggers, then squeeze both and hold still until it says Saved."},
      {"", "Right B cancels it. The game goes on behind this guide: stand somewhere safe, in a town."},
      {"", "Later: the settings in the headset, page Body, Calibrate body."}},
     "Measure now, or not now: it is on the Body page any time too."},
    // 1: moving and fighting
    {"Walk, turn, fight",
     {{"Left stick", "walk"},
      {"Right stick", "left / right: turn"},
      {"Right trigger", "attack. A sword, axe or mace hits when you swing it through a monster (Hit by touch), so its trigger does not; a bow or crossbow shoots with it."},
      {"Right A", "pick up, open, talk (a skill on the skill panel's A plate is cast instead)"},
      {"Right B, left X, left Y", "the game's skill buttons B, X, Y. Hold the left trigger for the second row."},
      {"Right stick click", "swap weapon sets I / II"}},
     kNavMiddle},
    // 2: what opens what
    {"Open things",
     {{"Menu button", "the inventory (PS VR2: the Options button of the right controller)"},
      {"Menu button, held", "half a second: the pause menu (Esc)"},
      {"Left stick click", "a quick click: the map. Held: the items on the ground are named; half a second: the inventory."},
      {"Right stick click, held", "half a second: the skill panel; the same again closes it"},
      {"Left grip + stick click", "recentre: ahead is where you look"},
      {"In a menu", "point the right controller at it (the laser) and pull the trigger to click; the sticks and buttons are the game's gamepad there"}},
     kNavMiddle},
    // 3: the skill panel and the settings in the headset
    {"The skill panel and the settings",
     {{"Skill panel", "point at a plate and pull the trigger: from then on that trigger casts its skill"},
      {"EDIT SKILLS", "pick a plate, then a skill you have, to put it there. A skill on the RT or A plate is cast by the right trigger or A."},
      {"SETTINGS tab", "the settings in the headset, in the same window: View, Options, Video, Body, Life, Look, HUD, Belt, Melee, Left, Right"},
      {"Both grips + a click", "squeeze both grips for a moment, then click a thumbstick: the window opens on SETTINGS"},
      {"Flip pages", "flick a thumbstick left or right, or tap a grip (left back, right on). B, Y or Menu closes the window."},
      {"On the PC", "D2R VR Authentic Settings has every setting; most changes show in the game at once"}},
     kNavMiddle},
    // 4: the belt, weapons, the bow
    {"Belt, weapons, bow",
     {{"Belt potions", "hang at your hips. A hand at one lights it, the grip takes it; to your mouth, tipped: you drink. Let go elsewhere: it goes back."},
      {"Your weapon", "has weight: hold it with the grip. Both grips let go: it falls (a grip calls it back); a second on the floor and it is on your back."},
      {"Behind a shoulder", "a grip there draws the weapon off your back. An empty hand's grip behind the left shoulder puts the shield away and draws it."},
      {"Bow", "the free hand to the string, trigger or grip, pull back, let go. While drawn: B the next arrow skill, A the one before."},
      {"Javelin", "hold it with the grip, throw, and let go"},
      {"Life and mana", "sit on your wrists"}},
     kNavMiddle},
    // 5: the views
    {"Views and this guide again",
     {{"F1", "from above: the game's own camera"},
      {"F2", "behind the hero, a camera turned with your head"},
      {"F3", "the game on your floor: look down at it, walk round it"},
      {"F4", "first person - this view. The hands, the panels and this guide are F4's."},
      {"Keyboard", "the F key of a view (the game window in focus), F12 the next view, F11 recentre. The settings' View page has F1 - F4 too."},
      {"This guide again", "the settings in the headset, page Body: First-start guide"}},
     "Done closes the guide; it is on the Body page again, any time."},
};

}  // namespace firststart
