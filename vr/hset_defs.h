#pragma once
// The keys the in-headset settings panel offers ([openxr] headset_settings, vr/xr.cpp, docs in
// d2r_vr.ini). One line a key; a new tick box of a feature is one line more. Pages are the panel's tabs
// (at most kMaxPages, at most kRowsPerPage rows each - kRowsMax on a page that stands them closer); a toggle is a key with min 0, max 1, step 1.
//
// The pages: View, Options, Left, Right (one tab: Controls), Vibration, HUD, Life, Look, Belt, Melee and World's four are rows of this table; Video (the VR render size, vr/hset_video.h), Body (the
// T-pose measurement, vr/hset_body.h), Co-op (the D2R Rooms plugin's relay bridge: its own d2r-rooms.ini, not d2r_vr.ini; vr/hset_coop.h) and Guide (the
// first-start guide, vr/first_start.h; vr/hset_scene.h) are drawn by hand; Video and Body own the keys
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
#include <string>

#include "ui_text.h"   // the rows' words in the player's language ([hud] headset_language)
#include "quality_presets.h"   // the Video page's Quality preset: the keys it writes (shared/quality_presets.h)
#include "draw_distance.h"   // the Video page's Draw distance: its stops (shared/draw_distance.h kReach)

namespace hset {

constexpr int kMaxPages = 23;
constexpr int kRowsPerPage = 6;
constexpr int kRowsMax = 7;   // a page with a row more than kRowsPerPage stands its rows closer (the Melee page; hset_model.h RowStep)
// A page is a line more here, in kPageNames and in kMaxPages / kPageCount (the tabs share the panel's width in proportion to their names:
// hset_model.h kTab). The order (Serg, 2026-11-02: "пересмотреть структуру"): what every view has first - View, Options, Video, Controls (the
// two key-binding pages, Left and Right, under one tab) - then the controllers' Vibration, the interface (HUD, the low-life vignette's Life and
// Look), the body's pages (Body, Belt, Melee, World), Co-op with its Party page, and the first-start guide last. In VR F1-F3 the pages and rows
// of F4's body are not shown (PageShown / KeyShown below).
// World (Serg, 2026-10-31: the basic world-interaction switches on one page, "выноси весь список") is one TAB of four pages - Contact,
// Monsters react, Grab, More - that follow each other like any pages (a flick, a grip's tap) and are picked by a row of four buttons on top
// (hset_model.h kWorldSub). So a page is not always a tab: kTabCount tabs, TabOf / TabPage between the two.
// Party (Serg, 2026-11-01: invite to a party from the headset, vr/hset_party.h) has no tab either: it is the Co-op tab's second page, reached by
// the Co-op page's Party button (the bottom bar) and back by its Relay button; a flick passes it between World's pages and the Guide.
// Stats (Serg, 2026-11-06: the strike statistics, [stats] strike_stats; vr/hset_stats.h) is a tab of its own before the Guide: it only shows.
// Render (Serg, 2026-11-08: foveated rendering and the other render-cost switches in the headset) has no tab either: it is the Video tab's second page,
// reached by the Video page's Render button (the bottom bar) and back by its Video button, as Party is Co-op's; F4 only (its features are).
// F3 (Serg 2026-11-08: "Yes, add them" - the Draw distance slider looked for in the headset and not found) has no tab either: it is the View tab's
// second page, reached by the View page's F3 button (the bottom bar) and back by its View button, as Render is Video's; VR F3 only (its keys are
// [table]'s: the game on the floor) - D2R VR Settings > Camera > "F3 The game on your floor" has the same two.
enum Page : int {
    kPageView = 0, kPageF3 = 1, kPageOptions = 2, kPageVideo = 3, kPageRender = 4, kPageKeysL = 5, kPageKeysR = 6, kPageVibration = 7, kPageHud = 8,
    kPageLife = 9, kPageLook = 10, kPageBody = 11, kPageBelt = 12, kPageMelee = 13, kPageMeleeAttack = 14,
    kPageWorld = 15, kPageWorldReact = 16, kPageWorldGrab = 17, kPageWorldMore = 18, kPageCoop = 19, kPageParty = 20, kPageStats = 21, kPageGuide = 22
};
constexpr int kPageCount = 23;
constexpr int kWorldPages = 4;   // kPageWorld .. kPageWorldMore
constexpr bool IsWorldPage(int page) { return page >= kPageWorld && page < kPageWorld + kWorldPages; }
// Controls (Serg, 2026-11-02: "лефт райт объединить в контролс") is one TAB of the two key-binding pages, Left and Right, picked by a row of two
// buttons on top as World's four are.
constexpr int kKeysPages = 2;    // kPageKeysL, kPageKeysR
constexpr bool IsKeysPage(int page) { return page >= kPageKeysL && page < kPageKeysL + kKeysPages; }
// Melee (Serg, 2026-11-06: the swing timer, the faster attack and a four-way Direct hit on it - ten rows) is one TAB of two pages: Touch (hit by touch's
// six) and Attack (quick blow, the faster attack, Direct hit, the swing timer), picked by a row of two buttons on top as Controls' two are.
constexpr int kMeleePages = 2;   // kPageMelee, kPageMeleeAttack
constexpr bool IsMeleePage(int page) { return page >= kPageMelee && page < kPageMelee + kMeleePages; }
// A tab of several pages (World, Controls, Melee): a row of buttons on top, one a page; its first page, how many.
constexpr bool HasSubRow(int page) { return IsWorldPage(page) || IsKeysPage(page) || IsMeleePage(page); }
constexpr int SubFirst(int page) { return IsWorldPage(page) ? kPageWorld : IsKeysPage(page) ? kPageKeysL : IsMeleePage(page) ? kPageMelee : page; }
constexpr int SubCount(int page) { return IsWorldPage(page) ? kWorldPages : IsKeysPage(page) ? kKeysPages : IsMeleePage(page) ? kMeleePages : 1; }
// The page whose tab a page stands under: a tab of several pages' first one, Party's Co-op, Render's Video, F3's View, else the page itself.
constexpr int LeadOf(int page) {
    return HasSubRow(page) ? SubFirst(page) : page == kPageParty ? kPageCoop : page == kPageRender ? kPageVideo : page == kPageF3 ? kPageView : page;
}
constexpr bool StartsTab(int page) { return LeadOf(page) == page; }
// The tab a page is under, and the page a tab opens.
constexpr int TabOf(int page) {
    int t = 0;
    for (int p = 0; p < LeadOf(page); ++p) if (StartsTab(p)) ++t;
    return t;
}
constexpr int TabPage(int tab) {
    for (int p = 0; p < kPageCount; ++p)
        if (StartsTab(p) && tab-- == 0) return p;
    return kPageGuide;
}
constexpr int kTabControls = TabOf(kPageKeysL), kTabWorld = TabOf(kPageWorld), kTabGuide = TabOf(kPageGuide), kTabCount = kTabGuide + 1;
static_assert(kTabCount == 15 && TabPage(kTabWorld) == kPageWorld && TabPage(kTabControls) == kPageKeysL && TabOf(kPageParty) == TabOf(kPageCoop) &&
                  TabOf(kPageMeleeAttack) == TabOf(kPageMelee) && TabOf(kPageRender) == TabOf(kPageVideo) && TabPage(TabOf(kPageVideo)) == kPageVideo &&
                  TabOf(kPageF3) == TabOf(kPageView) && TabPage(TabOf(kPageF3)) == kPageView,
              "fifteen tabs over the twenty-three pages");
constexpr int kNoPage = -1;   // a key with no row of its own: a hand-drawn page reads and writes it

// A set of keys, one bit a key of the table below (what an Apply writes, what a draft has changed). 256 bits: the Vibration page took the
// table past 64 keys, the Quality preset's keys (shared/quality_presets.h, 2026-11-08) past 128. Used as an integer would be (|, &, ~, ==, a test
// in an if); 0 is the empty set.
struct Mask {
    static constexpr int kWords = 4;
    uint64_t w[kWords] = {};
    constexpr Mask() = default;
    constexpr Mask(uint64_t low) : w{low, 0, 0, 0} {}
    constexpr explicit operator bool() const { return w[0] || w[1] || w[2] || w[3]; }
    friend constexpr Mask operator|(Mask a, Mask b) { for (int i = 0; i < kWords; ++i) a.w[i] |= b.w[i]; return a; }
    friend constexpr Mask operator&(Mask a, Mask b) { for (int i = 0; i < kWords; ++i) a.w[i] &= b.w[i]; return a; }
    friend constexpr Mask operator~(Mask a) { for (int i = 0; i < kWords; ++i) a.w[i] = ~a.w[i]; return a; }
    friend constexpr bool operator==(Mask a, Mask b) { return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2] && a.w[3] == b.w[3]; }
    friend constexpr bool operator!=(Mask a, Mask b) { return !(a == b); }
    constexpr Mask& operator|=(Mask b) { for (int i = 0; i < kWords; ++i) w[i] |= b.w[i]; return *this; }
    constexpr Mask& operator&=(Mask b) { for (int i = 0; i < kWords; ++i) w[i] &= b.w[i]; return *this; }
    constexpr int Count() const {   // how many keys
        int n = 0;
        for (int i = 0; i < kWords; ++i)
            for (uint64_t v = w[i]; v; v &= v - 1) ++n;
        return n;
    }
};
constexpr Mask Bit(int key) {
    Mask m;
    if (key >= 0 && key < 64 * Mask::kWords) m.w[key / 64] = uint64_t(1) << (key % 64);
    return m;
}

// What a row's value is: a number or a switch (min / max / step, shown by fmt or off / on), or a pick of the key bindings' list (vr/hset_keys.h: the
// value is the pick's index, one past the list = a line the list does not name; shown by the pick's name, stepped round the list), or a number of the
// game's own (Game: not the ini's - the row shows what the game says, Model::Players, its - / + ask the game, Do::Players; the store neither reads
// nor writes it).
// Derived: a row made of other keys (the Quality preset: shared/quality_presets.h; the Draw distance: shared/draw_distance.h kReach) - shown by
// the preset / stop they match, its < / > write them; the store neither reads nor writes the row's own key ("@preset", "@distance": no ini keys).
enum class DefType : uint8_t { Number, Choice, Game, Derived };

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
    int also = -1;               // a second page the same row stands on (the World pages' Hit by touch is the Melee page's), kNoPage none
};

inline constexpr Def kDefs[] = {
    // View: how the picture stands
    {L"screen", L"menu_distance_m", "Panel distance", "A panel open in the game: inventory, stash, trader",
     1.6f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", kPageView},
    {L"openxr", L"flat_distance_m", "Screen distance", "Menus, loading screens, the flat views F1 and F3",
     2.0f, 0.5f, 10.0f, 0.1f, "%.1f m", "", "", kPageView},
    {L"openxr", L"flat_width_m", "Screen width", "How wide the menus and panels stand",
     2.4f, 0.5f, 8.0f, 0.1f, "%.1f m", "", "", kPageView},
    {L"openxr", L"brightness", "Brightness", "Everything in the headset, 1 = as the game draws it",
     1.0f, 0.2f, 1.5f, 0.05f, "%.2f", "", "", kPageView},
    {L"stereo", L"user_height_m", "Your height", "World and hands scale; BodyWalk's Body Height wins",
     1.75f, 1.10f, 2.30f, 0.01f, "%.2f m", "", "", kPageView},
    // Options
    {L"openxr", L"laser", "Laser on menus", "Point a controller at a menu and it is the mouse",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageOptions},
    {L"openxr", L"laser_hand", "Laser hand", "The hand the laser comes from",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Left", "Right", kPageOptions},
    {L"openxr", L"menu_3d", "Menus as a panel", "The world stays in 3D behind an open menu",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageOptions},
    // How the right stick turns (shared/turn_mode.h, after bearstonem/d2r-pcvr, MIT): the toggle is its own key, so the angle and
    // the speed are both kept when it is switched. An older ini has no `smooth` and says snap=0 for smooth turning: the store
    // reads it that way (hset_store.cpp), and writes `smooth` with `snap` so an angle chosen later does not flip the mode.
    // The panel's speed starts at 30 (a step of 30 from 30 keeps the default 120 on the grid); the ini takes 10 - 720.
    {L"turn", L"smooth", "Smooth turning", "Right stick: turn continuously, or one snap a flick",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageOptions},
    {L"turn", L"snap", "Snap turn angle", "Degrees per flick, while smooth turning is off",
     30.0f, 15.0f, 180.0f, 15.0f, "%.0f deg", "", "", kPageOptions},
    {L"turn", L"speed", "Smooth turn speed", "Degrees per second, while smooth turning is on",
     120.0f, 30.0f, 720.0f, 30.0f, "%.0f deg/s", "", "", kPageOptions},
    // Serg (2026-10-30, a player's question in Discord: how to set "players 8" in VR): "я имел в виду выбор сложности". The game's players count, 1 - 8:
    // the value is the game's own (its Options > Gameplay "Offline Difficulty Scaling", Settings.json), not a key of d2r_vr.ini; - / + send the game's own
    // chat command "/players N" (vr/players_cmd.cpp), with every check of the game's: only in a game of one's own (single player, a TCP/IP host). The
    // seventh row: the page's rows stand closer (hset_model.h RowStep). [openxr] headset_players (no row, below) switches it on.
    {L"game", L"players", "Players (difficulty)", "As /players: tougher monsters, more experience and drops",
     1.0f, 1.0f, 8.0f, 1.0f, "%.0f", "", "", kPageOptions, false, DefType::Game},
    // F3 (the View tab's second page): the game on the floor's two of D2R VR Settings > Camera > "F3 The game on your floor", in its words (a row's
    // name short, the rest in its line). [table] full_width: no board edge, the game's own sky dome and fog round the game (vr/view/view_state.cpp
    // TableFull); [table] draw_distance: F3's own Draw distance stop, 0 = as the other views (shared/draw_distance.h ViewStop). Both live, as there.
    {L"table", L"full_width", "Full width", "The game's own sky and fog round the game, no edge",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageF3, true},
    {L"table", L"draw_distance", "F3 draw distance", "Near (1) - far (8); 0 = as the other views",
     0.0f, 0.0f, 8.0f, 1.0f, "%.0f", "", "", kPageF3, true},
    // ADD NEW TICK BOXES HERE, one line each (page 0 is full: its sixth row is the game view's F1 - F4 buttons; page 1 is full too (seven rows): the
    // next key needs a page of its own - kMaxPages and the tabs' places in the tile window and the panel).
    //
    // Life and Look: the low-life vignette ([hud] low_hp_*, vr/low_hp.h; D2R VR Settings > UI: F4 body > Low life edits the same
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
    // Belt: where the belt's potions hang on the hero ([hands] belt_models_*, the keys of D2R VR Settings > UI: F4 body > Belt potions;
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
    // Melee: the "Hit by touch" group of D2R VR Settings > Strikes - the tick, its two numbers and the trigger's tick, the same keys, ranges and defaults as there
    // ([gameplay] melee_touch, melee_min_speed, melee_stroke_start_cm, melee_touch_only; the plugin follows the ini live, vr/vrcam.cpp, vr/stroke_tuning.h). Live, F4 only.
    // The rest of the Strikes tab (thrown weapons, which contact counts, the kinds, Direct hit's Fully on and its numbers) stays in D2R VR Settings.
    {L"gameplay", L"melee_touch", "Hit by touch", "The blade you swing through a monster hits it (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMelee, true, DefType::Number, kPageWorldReact},
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
    // Serg (2026-10-30): "во вкладку меле добавить директ хит по правилам галочку" - D2R VR Settings > Strikes' "Direct hit" (shared/direct_hit_mode.h) as a
    // tick: on = By the rules (melee_direct 2, melee_direct_client 1), off = Off (0, 0). The store reads and writes the two keys together (hset_store.cpp);
    // Fully on stays in D2R VR Settings (the tick shows off then, the page's line says so, ticking it is By the rules). The seventh row: the page's rows
    // stand closer (hset_model.h RowStep). Live, F4 only.
    // Serg (2026-11-02): Direct hit changes the game's own logic - the detail says so, and the page's line warns while it is on (hset_scene.h StatusText).
    // Serg (2026-11-05, 0.204): «хорошая замена директ хит давай поставим его галочкой во внутреннем оверлее по умолчанию а директ хит отключим» - the
    // seventh row is [gameplay] quick_blow now (vr/quick_blow.h: the game's own attack, its blow on the first frame, the swing never stops the hero; on by
    // default; in co-op the host's tick holds). Direct hit has no row any more (kNoPage): D2R VR Settings > Strikes; the page's line still warns while it is on.
    // Serg (2026-11-06): the Melee tab's second page, Attack - "там должна быть галочка ускоренной атаки которую мы сделали и директ хит селектор с
    // несколькими выборами 1 выключен 2 только двуручное оружие на вторую и последующие цели по правилам 3 по правилам 4 включено всегда не рекомендуется",
    // and the swing timer's tick ("эта галочка отображать кольцо должна быть внутри оверлея меле"). Live, F4 only.
    {L"gameplay", L"quick_blow", "Quick blow", "The blow lands at once, the swing never stops you (F4)",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMeleeAttack, true},
    // [gameplay] quick_blow_faster (vr/quick_blow.h): the next attack once the blow is out, past the game's attack speed (quick_blow_min_ms in D2R VR Settings)
    {L"gameplay", L"quick_blow_faster", "Faster attack", "With quick blow: the next one past the game's speed (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMeleeAttack, true},
    // -- no row: Direct hit's first key (By the rules / Always is what melee_direct_client's row writes; the page's line warns while it is on).
    {L"gameplay", L"melee_direct", "Direct hit by the rules", "Changes the game's own logic: hits go to the server (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage, true},
    // Direct hit: the four choices of D2R VR Settings > Strikes (shared/direct_hit_mode.h) on [gameplay] melee_direct_client's row - its value is the mode
    // (0 Off, 1 Two-handed extras, 2 By the rules, 3 Always), named by vr/hset_names.h; the store reads and writes melee_direct, melee_direct_client and
    // melee_direct_scope together (hset_store.cpp).
    {L"gameplay", L"melee_direct_client", "Direct hit", "2H extras: a two-handed swing's further monsters",
     1.0f, 0.0f, 3.0f, 1.0f, "%.0f", "", "", kPageMeleeAttack, true},
    // -- no row: Direct hit's scope (0 the two-handed extras, 1 every touch by the rules; -1 missing: 0.204's way).
    {L"gameplay", L"melee_direct_scope", "Direct hit's scope", "0 two-handed extras, 1 every touch by the rules",
     -1.0f, -1.0f, 1.0f, 1.0f, "%.0f", "", "", kNoPage, true},
    // [hud] swing_timer (vr/swing_timer.h): the ring after each attack until the game lets the next one in, the blow's outcome inside (style / numbers: Settings)
    {L"hud", L"swing_timer", "Swing timer", "A ring shows when the next attack counts (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMeleeAttack, true},
    // [hud] swing_timer_reasons (vr/swing_reason.h, Serg 2026-11-06): in the same ring, why a touch was no attack - shaft, slow, short, wrong way ...
    {L"hud", L"swing_timer_reasons", "Swing timer: why not", "Shaft, slow, short, wrong way ... in the ring (F4)",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageMeleeAttack, true},
    //
    // Left and Right (the Controls tab's two pages): the key bindings, lines of [direct] (the plugin's own controls, vr/direct_layout.h): the value is a pick of the
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
    // [hands] left_handed (vr/hand_swap.h; Serg, 2026-11-09: "надо галочку внутрь оверлея игры"): the Controls tab's first page, its seventh row, under the
    // twelve bindings (kept one after the other: kKeyBindFirst). Live: written a moment after the press, and the plugin trades the controllers at
    // the next frame (xr.cpp Publish). Once on, the panel is pointed at with either trigger as ever; it opens with the long press of the LEFT
    // stick click then (the layout's right stick is the left controller's). F4 only.
    {L"hands", L"left_handed", "Left-handed", "Swap the controllers: the left one holds the weapon (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageKeysL, true},
    //
    // Vibration (Serg, 2026-10-31: the vibration on a tab of its own, in D2R VR Settings and in the headset): [openxr] haptics, its strength and the
    // extra pulses after bearstonem/d2r-pcvr (MIT; vr/haptics.h Extras) - the keys, defaults and ranges of D2R VR Settings > Vibration (its steps coarser).
    // The extras' other keys (the low mana share, the game rumble's strength, the pad mute, the motor) stay in D2R VR Settings. Live, F4 only.
    {L"openxr", L"haptics", "Controller vibration", "Pulses in the controllers on what happens (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    {L"openxr", L"haptics_scale", "Strength", "Every pulse stronger or weaker, 1 = the usual",
     1.0f, 0.2f, 2.0f, 0.1f, "%.1f", "", "", kPageVibration, true},
    {L"openxr", L"haptics_touch", "Pulse on every touch", "Hit by touch: landed or not, the blade's hand buzzes",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    {L"openxr", L"haptics_hit_long", "Longer tap for a blow", "95% for 90 ms instead of 72% for 45 ms",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    {L"openxr", L"haptics_spell", "Pulse when a cast starts", "The aiming hand, for a cast the game took",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    {L"openxr", L"haptics_low_mana", "Low mana alert", "The left hand, again every 10 s while it is low",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    {L"openxr", L"haptics_game_rumble", "The game's own rumble", "A hit taken, a skill used: as the game rumbles a pad",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageVibration, true},
    //
    // World (one tab of four pages): the basic switches of the world's touch, from D2R VR Settings' Weapon / Strikes / Hands tabs - the same keys and
    // defaults, their labels as there without "(a test)" (shortened where a row has no room: a test measures them), the line under each from the tooltip. Live: each is
    // written by itself a moment after the click, the other pages' rule; the plugin follows what it follows live. Hit by touch is the Melee page's row too.
    // Contact: what stops your weapon and hands - walls, your own body ([weight] own_body), the monsters' bodies and what they hold. The walls and your own body
    // are marked "(experimental)" as in the Settings exe (Serg, 0.192.41: off by default, "кто хочет, сам включит, потестит").
    {L"weight", L"world_contact", "Walls stop your weapon (experimental)", "Walls, pillars, trees and big props stop it (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorld, true},
    {L"weight", L"world_contact_hands", "Walls stop your bare hands (experimental)", "An empty hand stays at the wall's surface (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorld, true},
    {L"weight", L"own_body", "Your own body stops the weapon (experimental)", "It stops on your trunk and legs; never a hit on you",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorld, true},
    {L"weight", L"monster_bodies", "The weapon stops on monsters' bodies", "A cut or thrust stops on the body; an edge glances",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorld, true},
    {L"weight", L"monster_shields", "Monsters' shields and weapons stop your weapon", "Their shields and weapons too: a stopped blow no hit",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorld, true},
    // Monsters react (Hit by touch, the Melee page's row, stands first)
    {L"weight", L"strike_by_force", "Hit by the strike's force, not the blade's speed", "Not the blade's speed: a light touch is no hit",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    {L"weight", L"monster_ragdoll", "Monsters' bodies give way by their weight", "A push moves the limb it meets (humanoids)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    {L"weight", L"monster_ragdoll_all", "Every monster gives way, not only humanoids", "Animals, insects, demons too, not only humanoids",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    {L"weight", L"knock_down", "A hard blow knocks a humanoid off its feet", "It falls as a ragdoll and gets up again",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    {L"weight", L"stab_in", "A pointed weapon sticks into the body", "Driven point first into the body, 0.5 m/s or more",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    {L"weight", L"impale_drag", "Impale and drag a humanoid on your spear", "Run into a humanoid point first: it is carried",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldReact, true},
    // Grab
    {L"weight", L"grab_bodies", "Grab monsters and allies with your hand's grip", "An empty hand's grip takes hold of a body",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"npc_bodies", "Town NPCs and allies have bodies too", "Your weapon stops on them as on monsters",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"grab_throw", "Lift and throw a humanoid you grab", "Let go while you swing and it flies",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"grab_balance", "A pulled humanoid steps to keep its feet", "It steps after you; only a faster pull fells it",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"grab_items", "Pick up items from the ground with your hand's grip", "The game's own pick-up, at your palm",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"grab_props", "Take the props in your hand", "Cups, books, stools: carried by your grip",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    {L"weight", L"knock_props", "Props are knocked about by your weapon", "Cups, plates, vases, books and stools (F4)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldGrab, true},
    // More: the hands' strength, the skeleton's bone cap, and the two gestures
    {L"weight", L"strength", "Hands' strength, %", "How hard the hands pull the weapon after the controllers",
     100.0f, 10.0f, 500.0f, 5.0f, "%.0f%%", "", "", kPageWorldMore, true},
    {L"weight", L"skeleton_bones_max", "Monsters' skeletons: bones read at most", "Higher reads longer skeletons and costs more",
     256.0f, 64.0f, 1024.0f, 16.0f, "%.0f", "", "", kPageWorldMore, true},
    {L"hands", L"throw_gesture", "Throw a javelin with a throwing motion", "A javelin: hold the trigger, swing, let go",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldMore, true},
    {L"gameplay", L"shield_smite", "Shield bash casts Smite", "Paladin: a hard swing of the shield casts it",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageWorldMore, true},
    // -- no row: the Keys pages' note - the plugin's own controls are off ([openxr] direct_input=0: BodyWalk's mapping is the controls), so these lines do nothing.
    {L"openxr", L"direct_input", "Controls without BodyWalk", "The plugin reads the controllers itself",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Options page's Players row works ([openxr] headset_players, D2R VR Settings beside the panel's own tick); off, the row only shows the game's count.
    // -- no row: the Party page ([openxr] headset_party, vr/hset_party.h): the Co-op page's Party button and the game's party buttons for the players
    //    in the game. On as shipped (a menu button: nothing happens until it is pressed); off, the page only says so.
    {L"openxr", L"headset_party", "Party in the headset", "The players in the game and the game's party buttons",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"openxr", L"headset_players", "Players from the headset", "The Options page's Players row asks the game",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Co-op page's row "The game's map shows" ([openxr] headset_map_party, hset_party.h MapShow): the game's own Show Party / Show
    //    Party Names, shown and flipped by the game's key commands 10 / 11. On as shipped (a row: nothing happens until it is pressed); off, no row.
    {L"openxr", L"headset_map_party", "Party on the map from the headset", "The Co-op page's Show Party / Show Party Names row",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Belt page's note - the potions on the belt are off in D2R VR Settings, so its sliders do nothing.
    {L"hands", L"belt_models", "Potions on the belt", "The game's own potion models on the hero's hips",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    //
    // Video (Serg 2026-11-08: "добавить туда тип рендера, убрать кнопки с разрешением экрана, оставить только ползунок, добавить селекторы
    // отрисовки тумана и неба: игрой, или движком, или не рисовать"): three rows under the render size's slider, named as D2R VR Settings names them.
    // "Render type" (Serg 2026-11-08: five entries, one pass the default; shared/render_type.h): Native two passes, Native one pass, Native eyes by
    // turns, FlatVR stereo, FlatVR depth - a stepper over [openxr] on, [stereo] afr / pair_per_tick / native_replay / source (the row sits on
    // pair_per_tick; its value box names the type, vr/hset_names.h); a step writes what D2R VR Settings' "3D in the headset" list writes for it
    // (hset_model.h). (max 4: a stepper, not a tick; the file's pair_per_tick stays 0 / 1)
    // the Quality preset on top (Serg 2026-11-08: "и прямо внутрь игры вынести, в оверлей игры"): Potato / Low / Medium as D2R VR Settings has them
    // (shared/quality_presets.h), shown by the preset the keys match (Custom: none); < / > write every key of the next one into the draft, Apply writes
    // them. Room rings and the models' radius are read at the game's start: the line says so
    {L"@preset", L"preset", "Quality preset", "Potato, Low, Medium - room rings and models from the next start",
     2.0f, 0.0f, 3.0f, 1.0f, "%.0f", "", "", kPageVideo, false, DefType::Derived},
    // the Draw distance under it (Serg 2026-11-09: "ползунок дравдистанс давай вернем" - back after 0.208 hid it, the default as today): D2R VR
    // Settings' Home slider, the same stops (shared/draw_distance.h kReach) - shown by the stop the keys match (Custom: none); < / > write its six
    // keys (room rings, models' radius, the fog's four distances) into the draft, Apply writes them. 1 is Low / Potato's distance, 2 Medium's.
    {L"@distance", L"stop", "Draw distance", "Near (1) - far (7): room rings, models, the fog. 2 = Medium's, the default",
     2.0f, 1.0f, 7.0f, 1.0f, "%.0f", "", "", kPageVideo, false, DefType::Derived},
    {L"stereo", L"pair_per_tick", "Render type", "Native two passes / one pass / eyes by turns, FlatVR stereo / depth (one pass: from the next game start)",
     0.0f, 0.0f, 4.0f, 1.0f, "%.0f", "", "", kPageVideo},
    // "Sky drawn by" / "Fog drawn by": none, our effect, the game's (vr/draw_by.h) - a stepper over [sky] game_sky + [sky] enabled, [fog] game_fog +
    // [fog] enabled; its value box names the way (vr/hset_names.h). (max 2: a stepper, not a tick; the file's keys stay 0 / 1)
    {L"sky", L"game_sky", "Sky drawn by", "None, our effect, or the game's own dome (no frame time of ours)",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kPageVideo},
    {L"fog", L"game_fog", "Fog drawn by", "None, our effect, or the game's own fog pass (no frame time of ours)",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kPageVideo},
    // [render] potato_light_brightness (Serg 2026-11-08: "для режима потато надо добавить ползунок яркость солнца"): the Potato light's level - with
    // [render] potato_light on (D2R VR Settings > Performance > Render cost) everything is lit evenly by one light (vr/lit_flat.h); live
    {L"render", L"potato_light_brightness", "Potato light: brightness", "With Potato light on (Performance > Render cost): its brightness",
     1.0f, 0.3f, 5.0f, 0.05f, "%.2f", "", "", kPageVideo},
    // ... and underground ("для пещер отдельный ползунок яркости"): caves, dungeons - the areas the fog counts as underground
    {L"render", L"potato_light_brightness_caves", "... in caves", "The Potato light's brightness underground (caves, dungeons, crypts)",
     1.0f, 0.3f, 5.0f, 0.05f, "%.2f", "", "", kPageVideo},
    // Render (Serg 2026-11-08: "почему в настройках графики я не вижу галочки фоверальный рендеринг, чтобы его выключить"): the Video tab's second page
    // (its bottom bar's Render button, Video back - as Co-op's Party), the render-cost switches D2R VR Settings has on its Render tab, named as there. All
    // read live by the plugin; foveated rendering, the shadows once and the grass rate put their hooks in at the game's start when on then - first
    // switched on, they work from the next start (the rows' lines say so). F4 only, as the features.
    {L"render", L"foveated", "Foveated rendering", "Less detail at each eye's edges - saves graphics card time (first switched on: from the next start)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageRender},
    {L"render", L"foveated_strength", "... how strong", "How much keeps every pixel, how coarse the rest is",
     1.0f, 0.0f, 3.0f, 1.0f, "%.0f", "", "", kPageRender},
    {L"render", L"foveated_gaze", "... following the eyes", "Eye tracking where the headset gives it to games (PS VR2, Quest Pro, Steam Frame)",
     1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageRender},
    {L"render", L"foveated_fullscreen", "... the heavy passes too", "Grass lighting, reflections, hair: coarse at the edges as well",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kPageRender},
    {L"render", L"grass_lighting_coarse", "Grass lighting rate", "The grass lit at 2x1 or 2x2 over the whole picture (with or without foveated)",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kPageRender},
    {L"render", L"shadows_once_a_pair", "Shadows once a pair", "One pass: the shadow maps drawn once for both eyes (first switched on: from the next start)",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageRender},
    {L"render", L"lens_mask", "Lens mask", "Skip the pixels the lenses never show - saves graphics card time",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kPageRender},
    // -- no row: the Video page's rows' other keys
    {L"stereo", L"afr", "Stereo: by frames", "1 = native OpenXR's own stereo modes", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"stereo", L"replay_right", "Stereo: right eye replayed", "1 with both eyes a frame", 0.0f, 0.0f, 3.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"stereo", L"replay_ahead", "Stereo: replay ahead", "2 with both eyes a frame", 1.0f, 1.0f, 3.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"stereo", L"native_replay", "Stereo: one pass", "1 = the right eye drawn again from the left pass (the default)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"stereo", L"source", "Stereo: FlatVR's source", "2 the game's pair, 1 the depth, 0 none", 2.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"openxr", L"on", "Native OpenXR", "The game in the headset itself (off: FlatVR)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"sky", L"enabled", "Sky", "The sky drawn at all", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"enabled", "Distance fog", "Our effect's fog drawn at all", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Quality preset's other keys (shared/quality_presets.h), each with the plugin's own default for a missing key
    {L"render", L"native_effects", "Our effect", "A Quality preset key ([render] native_effects)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"render", L"game_stages", "The game's render stages", "A Quality preset key ([render] game_stages)", 0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"game_outline", "The game's hover outline", "A Quality preset key ([render] game_outline)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"render", L"potato_light", "Potato light", "A Quality preset key ([render] potato_light)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"render", L"potato_light_game", "Potato light in the game's colours", "A Quality preset key ([render] potato_light_game)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"render", L"rings", "Room rings", "A Quality preset key ([render] rings)", 2.0f, 1.0f, 8.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"model_radius", "Model radius", "A Quality preset key ([render] model_radius)", 200.0f, 150.0f, 1000.0f, 10.0f, "%.0f", "", "", kNoPage},
    {L"render", L"big_radius", "Big objects' radius", "A Quality preset key ([render] big_radius)", 0.0f, 0.0f, 3000.0f, 10.0f, "%.0f", "", "", kNoPage},
    {L"fog", L"light", "Light fog", "A Quality preset key ([fog] light)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_start", "Fog start", "A Quality preset key ([fog] game_fog_start)", 150.0f, 0.0f, 100000.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"fog", L"game_fog_end", "Full fog", "A Quality preset key ([fog] game_fog_end)", 600.0f, 0.0f, 200000.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"fog", L"game_fog_caves", "Fog in caves", "A Quality preset key ([fog] game_fog_caves)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_caves_start", "Fog start in caves", "A Quality preset key ([fog] game_fog_caves_start)", 150.0f, 0.0f, 100000.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"fog", L"game_fog_caves_end", "Full fog in caves", "A Quality preset key ([fog] game_fog_caves_end)", 600.0f, 0.0f, 200000.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"fog", L"game_fog_mist", "The game's fog over its mist", "A Quality preset key ([fog] game_fog_mist)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_height", "The game's fog thinned with height", "A Quality preset key ([fog] game_fog_height)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_before_fx", "The fog before smoke and fire", "A Quality preset key ([fog] game_fog_before_fx)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_fx_depth", "The effects fogged by their depth", "A Quality preset key ([fog] game_fog_fx_depth)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_our_curve", "The game's fog by our curve", "A Quality preset key ([fog] game_fog_our_curve)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"fog", L"game_fog_caves_black", "The game's fog black in caves", "A Quality preset key ([fog] game_fog_caves_black)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"ceiling", L"enabled", "Cave ceiling", "A Quality preset key ([ceiling] enabled)", 0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    {L"ceiling", L"steps", "Cave ceiling relief", "A Quality preset key ([ceiling] steps)", 12.0f, 1.0f, 64.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"stereo", L"headset_rate", "Headset refresh rate", "A Quality preset key ([stereo] headset_rate)", 3.0f, 0.0f, 4.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"monitor_eye", "Monitor shows one eye", "A Quality preset key ([render] monitor_eye)", 1.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"openxr", L"hands_over_panels", "Your hands over the Gestures tab", "A Quality preset key ([openxr] hands_over_panels)", 1.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage},
    // -- no row: the Video page. [render] vr_resolution_px = a square's side in pixels (0 = not used), vr_resolution = the older
    //    mode (0 the window's own size, 1 the headset's recommendation, 2..6 a square); shared/render_size.h reads the two together.
    {L"render", L"vr_resolution", "VR render size: mode", "0 window, 1 headset, 2-6 an older square",
     0.0f, 0.0f, 6.0f, 1.0f, "%.0f", "", "", kNoPage},
    {L"render", L"vr_resolution_px", "VR render size: side", "A square's side in pixels, 0 = not used",
     0.0f, 0.0f, 4096.0f, 1.0f, "%.0f", "", "", kNoPage},
    // -- no row: the Body page reads the T-pose measurement (the plugin writes it, vr/calibration.cpp) and can clear it. Its Arm span stepper sets
    //    arm_span_m by hand (with version and use): live, written by themselves a moment after the press, as a row's live key.
    {L"calibration", L"use", "Use the measurement", "The measured height and arm span are in use",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Off", "On", kNoPage, true},
    {L"calibration", L"version", "Measurement stored", "1 = a measurement is stored",
     0.0f, 0.0f, 1.0f, 1.0f, "", "No", "Yes", kNoPage, true},
    {L"calibration", L"eye_height_m", "Measured eye height", "Metres over the floor",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage},
    {L"calibration", L"arm_span_m", "Measured arm span", "Metres, grip to grip",
     0.0f, 0.0f, 3.0f, 0.0001f, "%.2f m", "", "", kNoPage, true},
    // Body: its one table row, above the hand-drawn part - how the game is played ([calibration] seated, vr/calibration.h; D2R VR
    // Settings > Body has it next to the measurement). Seated: the recentre sets the hero's eye height, Calibrate reads the arm span only. Live.
    {L"calibration", L"seated", "How you play", "Seated: Recentre sets the eye height; Calibrate reads the arm span",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Standing", "Seated", kPageBody, true},
    // HUD. [hud] bar_place is the toolbar type's row: its value box shows the type's name (vr/hset_names.h), a click steps the type
    // and takes [hud] hide to at least 1 with it (the model, hset_model.h), "In the picture" being hide 0; hide has no row of its own.
    {L"hud", L"bar_place", "Toolbar type", "Life and mana on the wrists, or the whole toolbar",
     5.0f, 0.0f, 5.0f, 1.0f, "%.0f", "", "", kPageHud},
    {L"hud", L"monster_height", "Plates: height on the screen", "+ higher, % of the screen (F4); the bar in the room has its own",
     0.0f, -60.0f, 40.0f, 5.0f, "%+.0f%%", "", "", kPageHud},
    {L"hud", L"ordinary_mob_bars", "Plates: ordinary monsters", "Shown: all plates. Hidden: only champions, bosses, NPCs",
     0.0f, 0.0f, 1.0f, 1.0f, "", "Shown", "Hidden", kPageHud},
    // [hud] allies_place (shared/allies_place.h): where the allies (the merc, the summons, the party) are shown in F4 - one of three (Serg, test 0.205.6):
    // the game's own row at the top, our grid under the map in the hand, our tiles on the right wrist. A stepper: its value box names the place (vr/hset_names.h).
    {L"hud", L"allies_place", "Allies shown", "The merc, the summons, the party: one place of three (F4)",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kPageHud},
    // our names over monsters and NPCs ([hud] monster_names_own / npc_names_own, vr/own_names.h NameLook): their own three - D2R VR Settings has them too. Live.
    {L"hud", L"names_own_size_pct", "Our names: size", "Our sharp names over monsters and NPCs, % of the game's",
     100.0f, 20.0f, 300.0f, 5.0f, "%.0f%%", "", "", kPageHud, true},
    {L"hud", L"names_own_far_pct", "Our names: size far away", "% of the size for a monster far off",
     60.0f, 10.0f, 150.0f, 5.0f, "%.0f%%", "", "", kPageHud, true},
    {L"hud", L"names_own_alpha", "Our names: backdrop", "A dark box behind them: 0 none, 1 solid",
     0.0f, 0.0f, 1.0f, 0.05f, "%.2f", "", "", kPageHud, true},
    // -- no row: the toolbar type's other key. 0 the toolbar stays in the picture, 1 it is cut out, 2 the whole interface is.
    {L"hud", L"hide", "Interface out of the picture", "0 nothing, 1 the toolbar, 2 all of it",
     0.0f, 0.0f, 2.0f, 1.0f, "%.0f", "", "", kNoPage},
};
constexpr int kCount = (int)(sizeof kDefs / sizeof kDefs[0]);
static_assert(kCount <= 64 * Mask::kWords, "a change mask is 256 bits (hset::Mask)");
constexpr Mask AllKeysMask() {
    Mask m = 0;
    for (int i = 0; i < kCount; ++i) m |= Bit(i);
    return m;
}
constexpr Mask kAllKeys = AllKeysMask();   // every key of the table (a request with a bit past them is refused)
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
constexpr int kKeyPairPerTick = IndexOf(L"stereo", L"pair_per_tick"), kKeyAfr = IndexOf(L"stereo", L"afr"), kKeyReplayRight = IndexOf(L"stereo", L"replay_right"),
              kKeyReplayAhead = IndexOf(L"stereo", L"replay_ahead");
constexpr int kKeyGameSky = IndexOf(L"sky", L"game_sky"), kKeySkyOn = IndexOf(L"sky", L"enabled"), kKeyGameFog = IndexOf(L"fog", L"game_fog"),
              kKeyFogOn = IndexOf(L"fog", L"enabled");
constexpr int kKeyPreset = IndexOf(L"@preset", L"preset");   // the Video page's Quality preset (shared/quality_presets.h)
constexpr bool PresetKeysIn() {   // every key a preset writes has its place here (the render type: its row's keys)
    for (const qualitypreset::Key& k : qualitypreset::kKeys)
        if (!qualitypreset::IsRenderType(k) && IndexOf(k.section, k.key) < 0) return false;
    return true;
}
static_assert(kKeyPreset >= 0 && PresetKeysIn(), "the Quality preset's row and every key it writes are in the table");
// the Video page's Draw distance (shared/draw_distance.h kReach) and the six keys it writes
constexpr int kKeyDistance = IndexOf(L"@distance", L"stop");
constexpr int kKeyRings = IndexOf(L"render", L"rings"), kKeyModelRadius = IndexOf(L"render", L"model_radius"), kKeyFogStart = IndexOf(L"fog", L"game_fog_start"),
              kKeyFogEnd = IndexOf(L"fog", L"game_fog_end"), kKeyCavesStart = IndexOf(L"fog", L"game_fog_caves_start"), kKeyCavesEnd = IndexOf(L"fog", L"game_fog_caves_end");
static_assert(kKeyDistance >= 0 && kKeyRings >= 0 && kKeyModelRadius >= 0 && kKeyFogStart >= 0 && kKeyFogEnd >= 0 && kKeyCavesStart >= 0 && kKeyCavesEnd >= 0,
              "the Draw distance's row and every key it writes are in the table");
constexpr int kKeyNativeReplay = IndexOf(L"stereo", L"native_replay"), kKeySource = IndexOf(L"stereo", L"source"), kKeyOpenXrOn = IndexOf(L"openxr", L"on");
// the Render page's steppers named (vr/hset_names.h)
constexpr int kKeyFoveatedStrength = IndexOf(L"render", L"foveated_strength"), kKeyFoveatedPasses = IndexOf(L"render", L"foveated_fullscreen"),
              kKeyGrassRate = IndexOf(L"render", L"grass_lighting_coarse");
static_assert(kKeyFoveatedStrength >= 0 && kKeyFoveatedPasses >= 0 && kKeyGrassRate >= 0, "the Render page's steppers are in the table");
// The rows that need variable rate shading tier 2: greyed with the reason on a card without it (video::Env::vrs 0)
constexpr int kKeyFoveated = IndexOf(L"render", L"foveated"), kKeyFoveatedGaze = IndexOf(L"render", L"foveated_gaze");
constexpr bool NeedsVrs(int key) {
    return key >= 0 && (key == kKeyFoveated || key == kKeyFoveatedStrength || key == kKeyFoveatedGaze || key == kKeyFoveatedPasses || key == kKeyGrassRate);
}
static_assert(kKeyFoveated >= 0 && kKeyFoveatedGaze >= 0, "the foveation rows are in the table");
static_assert(kKeyNativeReplay >= 0 && kKeySource >= 0 && kKeyOpenXrOn >= 0, "the render type's keys are in the table");
static_assert(kKeyPairPerTick >= 0 && kKeyAfr >= 0 && kKeyReplayRight >= 0 && kKeyReplayAhead >= 0 && kKeyGameSky >= 0 && kKeySkyOn >= 0 && kKeyGameFog >= 0 &&
                  kKeyFogOn >= 0,
              "the Video page's rows and their keys");
constexpr int kKeyBeltOn = IndexOf(L"hands", L"belt_models");     // the Belt page's note
constexpr int kKeyBeltUp = IndexOf(L"hands", L"belt_models_up");
constexpr int kKeyQuickBlow = IndexOf(L"gameplay", L"quick_blow");   // Melee > Attack's first row (0.204)
constexpr int kKeyMeleeDirectScope = IndexOf(L"gameplay", L"melee_direct_scope");   // Direct hit's third key (no row)
constexpr int kKeyMeleeTouch = IndexOf(L"gameplay", L"melee_touch");   // the Melee page's tick: its two numbers do nothing without it
constexpr int kKeyMeleeArmed = IndexOf(L"gameplay", L"melee_skill_armed");   // the Melee page's skill arming (needs the touch)
constexpr int kKeyMeleeDirect = IndexOf(L"gameplay", L"melee_direct");   // the Melee page's Direct hit: a tick over its two keys (hset_store.cpp)
constexpr int kKeyMeleeDirectClient = IndexOf(L"gameplay", L"melee_direct_client");
constexpr int kKeyAlliesPlace = IndexOf(L"hud", L"allies_place");   // [hud] allies_place: a stepper with names (vr/hset_names.h AlliesPlaceName)
static_assert(kKeyMeleeDirect >= 0 && kKeyMeleeDirectClient >= 0, "Direct hit's keys are in the table");
constexpr int kKeyGrabTrigger = IndexOf(L"belt", L"grab_trigger");   // the two-button take: the Belt page's last row (it reads the trigger through direct input)
constexpr int kKeyVersion = IndexOf(L"calibration", L"version");
constexpr int kKeyEye = IndexOf(L"calibration", L"eye_height_m");
constexpr int kKeySpan = IndexOf(L"calibration", L"arm_span_m");
constexpr int kKeySeated = IndexOf(L"calibration", L"seated");   // the Body page's row: what the measurement reads and what the page says
static_assert(kKeyMode >= 0 && kKeyPx >= 0 && kKeyUse >= 0 && kKeyVersion >= 0 && kKeyEye >= 0 && kKeySpan >= 0 && kKeySeated >= 0,
              "the Video and Body pages' keys are in the table");
static_assert(kKeySmooth >= 0 && kKeySnap >= 0, "the turn keys are in the table");
static_assert(kKeyBeltOn >= 0 && kKeyBeltUp >= 0 && kKeyGrabTrigger >= 0 && kKeyMeleeTouch >= 0 && kKeyMeleeArmed >= 0, "the belt's and the melee page's keys are in the table");
constexpr int kKeyPlayers = IndexOf(L"game", L"players");                 // the Options page's Players row: the game's count (DefType::Game)
constexpr int kKeyPlayersOn = IndexOf(L"openxr", L"headset_players");    // ... and its switch
constexpr int kKeyPartyOn = IndexOf(L"openxr", L"headset_party");        // the Party page's switch
constexpr int kKeyMapPartyOn = IndexOf(L"openxr", L"headset_map_party");  // the Co-op page's map row's switch
static_assert(kKeyMapPartyOn >= 0, "the Co-op page's map row's switch is in the table");
static_assert(kKeyPartyOn >= 0, "the Party page's switch is in the table");
static_assert(kKeyPlayers >= 0 && kKeyPlayersOn >= 0, "the Players row's keys are in the table");
constexpr int kKeyDirectOn = IndexOf(L"openxr", L"direct_input");   // the Keys pages' note
constexpr int kKeyBindFirst = IndexOf(L"direct", L"left_trigger");  // the twelve binding keys, one after the other (vr/hset_keys.h kSlots)
constexpr int kBindCount = 12;
static_assert(kKeyDirectOn >= 0 && kKeyBindFirst >= 0, "the Keys pages' keys are in the table");
constexpr bool IsChoice(int key) { return key >= 0 && key < kCount && kDefs[key].type == DefType::Choice; }
constexpr bool IsDerived(int key) { return key >= 0 && key < kCount && kDefs[key].type == DefType::Derived; }   // not the ini's either (the preset)
constexpr bool IsGame(int key) { return key >= 0 && key < kCount && kDefs[key].type == DefType::Game; }   // not the ini's: the store leaves it alone
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
// The pages' names: a tab's for its page, the World pages' their own (the buttons of the row on top of them, hset_scene.h).
constexpr const char* kPageNames[kMaxPages] = {"View", "F3", "Options", "Video", "Render", "Left", "Right", "Vibration", "HUD", "Life", "Look", "Body", "Belt", "Touch",
                                               "Attack", "Contact", "Monsters react", "Grab", "More", "Co-op", "Party", "Stats", "Guide"};
constexpr const char* kTabNames[kTabCount] = {"View", "Options", "Video", "Controls", "Vibration", "HUD", "Life", "Look", "Body", "Belt", "Melee",
                                              "World", "Co-op", "Stats", "Guide"};
// The tabs' font (vr/hset_paint.cpp: Segoe UI, semibold, GDI) and the English names' widths in it, pixels: the strip is shared by them (hset_model.h kTab).
// Fourteen names by their letters did not leave "Options" and "HUD" room, nor did 30 pixels letters by their widths; 26 by their widths did for
// fourteen (939 of 960); fifteen (World) take 24, and so do fourteen again (Controls in place of Left and Right); fifteen with Stats take 22 (925 of 960:
// at 24 they need 978). tools/hset_test.cpp measures them again.
constexpr int kTabFontPx = 22;
constexpr int kTabNameW[kTabCount] = {38, 62, 45, 67, 73, 36, 29, 38, 39, 30, 47, 48, 48, 39, 46};
// The panel's words in the player's language (vr/ui_text.h; the keys are made from this table, vr/ui_text_tables.h lists them for lang\en.txt):
// a row's name and the line under it "vr.hset.<section>.<key>.label / .detail" (a Choice row's line is its ini text: not a word), a switch's two
// faces by their English "vr.hset.value.<word>" (one "On" for every row), a tab "vr.hset.page.<name>". The tabs' widths stay the English names'.
inline std::string DefKey(int i, const char* field) {
    std::string k = "vr.hset.";
    for (const wchar_t* p = kDefs[i].section; *p; ++p) k += (char)*p;
    k += '.';
    for (const wchar_t* p = kDefs[i].key; *p; ++p) k += (char)*p;
    return k + "." + field;
}
inline const char* DefLabel(int i) { return uitext::Tr(DefKey(i, "label"), kDefs[i].label); }
inline const char* DefDetail(int i) { return uitext::Tr(DefKey(i, "detail"), kDefs[i].detail); }
inline std::string ValueKey(const char* en) { return "vr.hset.value." + uitext::Slug(en); }
inline const char* ValueWord(const char* en) { return *en ? uitext::Tr(ValueKey(en), en) : en; }
inline std::string PageKey(int page) { return "vr.hset.page." + uitext::Slug(kPageNames[page]); }
inline const char* PageName(int page) { return uitext::Tr(PageKey(page), kPageNames[page]); }
inline std::string TabKey(int tab) { return "vr.hset.page." + uitext::Slug(kTabNames[tab]); }   // (a tab's page has the same key: one word)
inline const char* TabName(int tab) { return uitext::Tr(TabKey(tab), kTabNames[tab]); }
// The low-life vignette's two pages: while one is open the plugin previews the vignette (vr/low_hp_game.h SetPreview).
constexpr bool IsLifePage(int page) { return page == kPageLife || page == kPageLook; }

// -- what a view shows -----------------------------------------------------------------------------------------------------------------------------
// The overlay is in every VR view (Serg, 2026-11-02: "игровой оверлей ... во все режимы ф1 ф2 ф3, убрать только то что не относится"): in VR F1, F2, F3
// (view 1 - 3) the pages and rows of F4's body are not shown - what the plugin does there in F4 only (vr/view_gate.h): the controllers' pulses
// (vr/haptics.cpp), the low-life vignette (vr/low_hp.h), the T-pose measurement, the belt's potions, hit by touch and World's weapon, hands and
// bodies; and on the pages that stay, a menu as a panel in front ([openxr] menu_3d: F4's pair) and the monsters' plates (vr/mob_bars.cpp: hooked
// in F4 only). View 4 (F4) and 0 (not known: D2R VR Settings' own panel, a test) show everything, as before.
constexpr bool FlatView(int view) { return view >= 1 && view <= 3; }
constexpr bool F4OnlyPage(int page) {
    return page == kPageVibration || page == kPageLife || page == kPageLook || page == kPageBody || page == kPageBelt || IsMeleePage(page) || IsWorldPage(page) ||
           page == kPageStats || page == kPageRender;   // (Render: foveated rendering, the lens mask, the shadows once - F4 only)   // (the strike statistics: the touch counts in F4 only)
}
constexpr bool F4OnlyKey(int key) {
    return key >= 0 && key < kCount && (F4OnlyPage(kDefs[key].page) || key == IndexOf(L"openxr", L"menu_3d") || key == IndexOf(L"hud", L"monster_height") ||
                                        key == IndexOf(L"hud", L"ordinary_mob_bars") || key == kKeyAlliesPlace || key == IndexOf(L"hud", L"names_own_size_pct") ||
                                        key == IndexOf(L"hud", L"names_own_far_pct") || key == IndexOf(L"hud", L"names_own_alpha") ||
                                        key == IndexOf(L"hands", L"left_handed"));   // (where the allies are, our names, the left-handed swap: F4 only)
}
// F3's page (the game on the floor's keys): VR F3 only, and view 0 (not known: a test) - "only what relates" (Serg, 2026-11-02) the other way round.
constexpr bool F3OnlyPage(int page) { return page == kPageF3; }
constexpr bool PageShown(int page, int view) {
    return page >= 0 && page < kPageCount && !(FlatView(view) && F4OnlyPage(page)) && !(F3OnlyPage(page) && view != 0 && view != 3);
}
constexpr bool KeyShown(int key, int view) { return !(FlatView(view) && F4OnlyKey(key)); }
constexpr bool TabShown(int tab, int view) { return tab >= 0 && tab < kTabCount && PageShown(TabPage(tab), view); }
static_assert(IndexOf(L"openxr", L"menu_3d") >= 0 && IndexOf(L"hud", L"monster_height") >= 0 && IndexOf(L"hud", L"ordinary_mob_bars") >= 0,
              "the rows F1 - F3 do not show are in the table");

// A page's rows in a view (0 / 4: all of them; F1 - F3 leave out F4's: KeyShown above).
constexpr bool OnPage(int key, int page, int view) {
    return (kDefs[key].page == page || (kDefs[key].also >= 0 && kDefs[key].also == page)) && KeyShown(key, view);
}
constexpr int RowsOn(int page, int view = 0) {
    int n = 0;
    for (int i = 0; i < kCount; ++i) if (OnPage(i, page, view)) ++n;
    return n;
}
static_assert(RowsOn(kPageView) <= kViewRow, "the first page's table rows leave its last row to the game view's buttons");
// The index in kDefs of the row-th key of a page in a view, -1 past the end.
constexpr int KeyAt(int page, int row, int view = 0) {
    int seen = 0;
    for (int i = 0; i < kCount; ++i)
        if (OnPage(i, page, view)) {
            if (seen == row) return i;
            ++seen;
        }
    return -1;
}

using Values = std::array<float, kCount>;

}  // namespace hset
