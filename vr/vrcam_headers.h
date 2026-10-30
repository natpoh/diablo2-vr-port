// vrcam_headers.h - every header the VR layer's vr/vrcam.cpp is built on, and the declarations of the plugin's other files it calls
// (moved here from the top of vrcam.cpp, docs/plan_modules.md M2): included by vrcam.cpp, and by a module cut from it whose code
// reaches into most features (vr/config/load_settings.cpp). A module of one feature includes only its own headers.
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dbghelp.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <intrin.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <share.h>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <string>
#include <vector>

#include <shlobj.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <D2RLPlugin/api.h>
#include <reshade.hpp>
#include <type_traits>
#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "game_device_only.h"
#include "light_bounds.h"
#include "fog_spare.h"      // [fog] spare_effects: the fog spares fire, spells and smoke (the game's transparent targets at its resolve)
#include "effects_layer.h"  // [stereo] turns_keep_own 3, 4: eyes by turns - where the game's effects are in the eye drawn (xr.cpp reads it)
#include "fog_in_frame.h"   // [render] fog_in_frame: our fog drawn in the game's frame, before its transparent resolve
#include "game_fog_vr.h"   // [fog] game_fog: the game's own distance fog with our numbers (its own pass, none of ours)
#include "sky_dome.h"      // [sky] game_sky: our painted sky as the game's own dome (the pure parts)
#include "game_ceiling.h"  // [ceiling] game_ceiling: the cave ceiling as the game's own lit rock slab (the pure parts, shared with Settings)
#include "ceiling_measures.h"   // [ceiling] height_from_walls, game_ceiling_beams: the rooms measured from the game's own models

namespace probe {
void Run(const float hero[3], const wchar_t* outPath);
void FindHero(const float hero[3], const wchar_t* outPath, const char* tag);
int FindHeroMatrices(const float hero[3], uintptr_t* out, int max);
}
#include "skeletons.h"
#include "melee_touch.h"   // hit by touch (melee_touch.cpp)
#include "strike_direction.h"   // hit by touch: the strike rules (toward the monster, the head, the amplitude), the engine's numbers per weapon kind
#include "metal_head.h"   // [weight] dual_collide: a headed weapon's head as its own volume (melee_head_volume), for the other weapon of a pair
#include "weapon_head_parts.h"   // ... a flat head as the thin slab of capsules its mesh is
#include "thrust_stroke.h"   // hit by touch: the thrust - a poke along the blade with its own speed and distance
#include "thrust_debug_wire.h"   // [debug] thrust_debug: the thrust's readout over the weapon hand
#include "stab_debug.h"          // [debug] stab_debug: the stab's readout and marks
#include "melee_direct.h"  // the user's experimental direct hit (melee_direct.cpp)
#include "melee_direct_client.h"   // [gameplay] melee_direct_client: the touch of a joined TCP/IP player as the game's own command (melee_direct_client.cpp)
#include "pad_presence.h"  // pad 0 is never answered "not connected" in VR once the plugin is the pad (the game's reconnect panel)
#include "pad_skill.h"     // a skill slot's control down in a pad report (the right trigger's RT slot too): the cast goes along the hand
#include "laser_reach.h"   // [openxr] laser_fit_window: the game's window kept on its monitor, so the laser reaches all of the picture
#include "stand_still_wire.h"   // [gameplay] attack_in_place: an attack of ours is pressed with the game's Stand Still (stand_still_wire.cpp)
#include "stand_still.h"        // ... and a click on bare ground in F4 gets Stand Still too (HookMapClick)
#include "move_attack_wire.h"   // [gameplay] move_while_attacking: the hero walks on once the blow or the shot is out (move_attack_wire.cpp)
#include "reshade_env.h"        // another mod's RESHADE_* variables, cleared in this process before ReShade64.dll loads (LoadReShade)
#include "stray_walk_wire.h"    // [gameplay] block_stray_walk / block_stray_clicks: no stray walk, no stray mouse button in the F4 world (stray_walk_wire.cpp)
#include "view_gate.h"     // everything added after the base mod acts in VR first person (F4) only
#include "vcache.h"        // [render] vcache: the game's threads on the big-L3 cores (Ryzen X3D with two CCDs)
#include "interact_press.h"   // [input] interact_one_press: one target per press of "pick up / interact"
#include "depth_pick.h"    // the depth-stencil our effects copy, picked once and kept (scenedepth::)
#include "flat_pick.h"     // [debug] flat_pick: the flat mouse look's targeting log (pure part, tested)
#include "flat_ground.h"   // [input] flat_aim_ground: the mouse's point on raised ground (pure part, tested)
#include "flat_shift.h"    // [camera] flat_shift_x / _y: the flat picture moved on the screen, and where the hero is on it (pure part, tested)
#include "flat_body.h"     // [input] flat_body_pick: a body or an object clicked by its body in flat F2 / F3 (pure part, tested)
#include "unit_scan.h"     // ... the client's unit hash, the walk hit by touch uses
#include "portal_oval.h"   // [render] flat_portal_no_oval: the town portal's black oval not drawn in flat F2 / F3 (pure part, tested)
#include "portal_auto.h"   // [input] portal_look_horizon / portal_auto: flat, after a portal and into one (pure part, tested)
#include "../shared/unleashed_fog.h"           // [fog] ease_in / no_fog_river: D2R VR Unleashed's fog ease and no fog in the River of Flame (MPL 2.0)
#include "../shared/unleashed_far_objects.h"   // [render] big_radius: D2R VR Unleashed's Big objects (MPL 2.0, bearstonem) - the setting's steps
#include "room_heights.h"  // [render] floors_stay: a room keeps the height the game first drew it at (pure part, tested)
#include "attack_view.h"   // VR F2 and F4: the attack goes where the player looks; VR F1 and F3 (top-down): where the hero faces (pure part, tested)
#include "window_json.h"   // the game's windowed size in Settings.json and what each platform does to it (shared/, pure, tested)
#include "turn_mode.h"     // [turn] smooth / snap / speed: how the right stick turns, and what an older ini's snap=0 means (shared/, pure, tested)
#include "stick_pitch.h"   // [camera] stick_pitch_f2: the right stick's up / down tilts the camera in VR F2 (pure part, tested)
#include "frame_cap.h"     // the game's frame cap in Settings.json: VR's rate is VR's, flat mode has the player's own (shared/, pure, tested)
#include "safe_screen.h"   // the game's interface size in Settings.json: VR starts it at 80 where the player chose none, flat mode has his own (shared/, pure, tested)
#include "flat_raw.h"      // [direct] flatvr_controls: BodyWalk's raw controllers (Game Link) as the plugin's own controls in FlatVR (pure part, tested)
#include "map_direction.h"   // [hud] map_direction: the way our camera looks, on the game's isometric map (pure part, tested)
#include "map_turn.h"        // [hud] map_rotate / map_compass: the head's turn held while it looks down at the map in the hand
#include "minimap_side.h"    // [hud] map_corner: the corner the start's Settings.json rewrite puts the game's map in (pure part, tested)
#include "walk_path.h"     // [openxr] direct_bodywalk_walk: BodyWalk's walking stick mixed into the controller's in direct mode (pure part, tested)
#include "walk_latency.h"  // [debug] walk_start_log: one line per walk start of BodyWalk's, from its stick leaving rest to the hero moving (pure part, tested)
#include "room_walk.h"      // [openxr] roomscale_walk: your steps in the room walk the hero (pure part, tested)
#include "lean_gait.h"      // [direct] walk_by_lean: walk or run by the left stick's lean (pure part, tested)
#include "room_walk_wire.h" // ... and its wiring
#include "belt_flick.h"    // [direct] belt_flick_flat_views: the left trigger + a flick of a thumbstick drinks a belt column in VR F1-F3 (pure part, tested)
#include "melee_boxes.h"   // [debug] melee_boxes: the capsules of hit by touch drawn in the headset
#include "skill_triggers.h"   // [debug] skill_triggers: what casts a skill (the shield bash -> Smite so far) drawn in the headset
#include "player_bodies.h"   // [multiplayer] pose_share_bodies: the other players' bodies and weapons as capsules our weapon meets
#include "pose_share_wire.h"
#include "gait_lib.h"   // [multiplayer] pose_share_legs: the optional gait library (d2r_vr_gait.dll)   // [multiplayer] pose_share: the VR players' heads and controllers to each other through D2RLoader's plugin network (a probe: logged, three markers)
#include "blade_shape.h"   // the touch's blade as drawn (dual wield, the sliders)
#include "flail_chain.h"   // [flail] ball_weight_pct: the flail's mass on the weight model (WeaponMassFactor)
#include "weight.h"
#include "grab_wire.h"     // [weight] grab_bodies: an empty hand's grip holds a body
#include "grab_bodies.h"   // ... which hand may take a body (BusyWhy)
#include "weight_lib.h"    // the weight-physics library (d2r_vr_weight.dll beside the plugin), when there is one: [weight] hands_model 1
#include "doll_lib.h"   // [weight] monster_ragdoll: the doll library
#include "grip_lib.h"      // the grip library (d2r_vr_grip.dll beside the plugin), when there is one: [weight] grip_model 1
#include "hero_strength.h"  // [weight] hero_strength: the default class multipliers
#include "stick_strength.h"  // [weight] stick_by_strength, stick_pct: how hard a weapon sticks
#include "weight_drive.h"    // [weight]: a weapon held alone in either hand is the weight model's (HeldAlone)
#include "slide_kinds.h"    // [weight] slide_second_hand_<kind>: the weapon kinds whose second hand slides
#include "grip_zone.h"      // [weight] grip_hilt_only, grip_hilt_cm_<kind>: where the second hand takes a sword, an axe, a mace
#include "aim_source.h"    // where a staff's aim comes from: the controllers, never the weapon as drawn
#include "hold.h"
#include "writewatch.h"
namespace uitrace { void Register(bool trace, bool skip, bool depthViaReShade); bool Arm(const std::wstring& path); bool Busy(); void SetSkip(uint64_t pipe); void SetSkipShaders(const uint64_t* hashes, int n);
                    void SetDepthRange(bool on, float range); uint32_t DepthRangeFixes(); int DepthRangeLayouts();
                    bool DepthFixFor(uint64_t layout, uint32_t param); void FixDepthRangeAt(uint8_t* cb); }
#include "game_hud_shared.h"
namespace memdiff { void Step(uintptr_t base, const std::wstring& outPath, void (*log)(const char*)); void Reset(void (*log)(const char*)); }
namespace hud { void Register(); void SetHide(int mode); void SetInterfaceScale(float s); void SetToolbarCut(float upPct, float downPct, bool tint); void SetMenuOpen(bool open); void SetPanelOut(bool out); void PollMenuReader(); bool FlatVrTakesMenu(); void SetMenuPlace(float widthM, float distanceM, float belowM, uint32_t laser, uint32_t window); int FlatVrLaserState(); void SetMapMode(int mode); bool LayerFound(); void SetLogger(void (*log)(const char*)); void SetLook(const FlatVRGameHudLook& look); void SetMapCorner(int corner); void SetMapDirection(bool want, bool wholeScreen); void SetMapHeading(float x, float y); void SetMapTurnHeading(float x, float y); bool ToggleMapShown();
                void SetPointer(uint32_t mode); void SetPointerDepth(float base, float tilt, float curve, float topScale); int Anchor(int panel); void SetForearm(int panel, bool valid, const float elbow[3], const float wrist[3], const float across[3]);
                void SetClassicView(bool on); void SetPictureZoom(float bar, float map); bool PictureWanted(); void SetPictureReady(bool ready);
                bool PictureNow(int i, float box[4], uint64_t* srv); void SetPictureBarMoved(bool moved); void SetPictureBarOffset(float x, float y); void SetPictureMapMoved(bool moved); void SetPictureMapOffset(float x, float y);
                void SetLabels(bool on); bool LabelsNow(float keepBar[4], float keepMap[4], uint64_t* srv); void SetUiMask(bool on); void SetUiMaskInMenu(bool on); bool UiMaskNow();
                void SetGlobes(bool want); bool GlobesNow(float* fit, uint64_t* srv); int GlobesWhy(); }
#include "d2rcam.h"
#include "sigscan.h"
#include "mat4.h"
namespace gamecmd { void Init(const D2RL::PluginContext* ctx); void Tick(); void Tap(uint32_t cmd); bool Pressed(uint32_t cmd); }   // the game's key commands from BodyWalk (gamecmd.cpp)
namespace skillwheel { void Init(const D2RL::PluginContext* ctx); void Tick(); void Shutdown(); }   // [openxr] skill_wheel (skill_wheel.cpp)
namespace skillwheel { bool Enabled(); }
namespace wheelgame { void Tick(bool on, bool open); }   // the wheel's reads of the controller's skill slots (wheel_game.cpp): a throw by gesture keeps them going for the Throw slot
namespace gamestate { void Init(const D2RL::PluginContext* ctx); void Tick(); uint32_t WeaponClass(); uint32_t WeaponSet(); uint32_t WeaponType(); uint32_t HandsHeld(); uint32_t TwoHanded(); uint32_t WeaponHand(); uint32_t HandKind(int side); uint32_t HandCode(int side); void SetWantHeroStrength(bool on); int HeroStrength(); int HandRequiredStrength(int side); uint32_t HandsKey(); bool MenuOpen(); void SetLaserPanels(bool on); uint64_t TitleScreenAt(); int ObjectLight(uint32_t txt, uint32_t mode, float rgb[3]); uint64_t LocalPlayer(); void SetViewMode(uint32_t mode); bool AutoMapOpen(); int MapMode(); bool SetAutoMap(bool open); void SetMapSide(int corner); void PollPlate(); bool PlateRect(int32_t out[4]); bool PlateReadAt(uintptr_t* unit, float px[2]); bool PlateAnchor(int32_t out[2]); void PollDurability(); bool DurabilityRect(int32_t panel[4], int32_t widget[4], bool* widgetRead); void PollNotice(); bool NoticeRect(int32_t out[4]); bool SkillAimed(int skill); void BeltCodes(uint32_t out[4]); void BeltIds(uint32_t out[4]); }
namespace gamestate { int HandThrust(int side); }   // hit by touch's thrust: the item's class in the game's data, 1 a thrust (1ht, 2ht), 0 not, -1 not known
namespace gamestate { bool HandIsThrowPotion(int side); }   // the held item is one of the six throwing potions (gamestate.cpp)
namespace gamestate { bool InventoryOnly(); }   // [openxr] hand_inventory: every menu open is the inventory (gamestate.cpp)
namespace gamestate { void PollCursor(); }      // ... whether an item is on the game's cursor, read on the UI thread (CursorHeld, for xr.cpp's drop)
namespace gamestate { bool QueueHandWalk(void (*fn)()); }   // ... and the stick's own walk with the inventory up, run on the game's UI thread
namespace gamestate { bool NpcDialogOnly(); }   // an NPC's dialogue box (Talk / Trade / Cancel) is the only menu open (gamestate.cpp)
namespace hud { void SetPanelNoBar(bool on); }   // [hud] npc_dialog_no_bar: that box's panel without the toolbar (hud.cpp)
namespace gamestate { uint32_t CodeOfItemClass(uint32_t classId); }   // [weight] grab_items: an item class's code, 0 not known (gamestate.cpp)
namespace gamestate { int HeadSide(); }   // [shield] heads_hang: the hand slot a Necromancer's head (ItemTypes head) is in: 1 left, 0 right, -1 none (gamestate.cpp)
#include "d2r_vr_state.h"

#include "MinHook.h"
#include "crosshair_cursor.h"
#include "d2r_vr_shared.h"
#include "link.h"
#include "direct_feed.h"
#include "afr_eye_shared.h"
#include "dlss_mv.h"
#include "xr.h"
#include "ui_lang.h"   // [hud] headset_language: the headset's words in the language of D2R VR Settings (vr/ui_text.h)
#include "eye_fov.h"   // native OpenXR: an eye's projection from its frustum (Projection)
#include "turn_math.h"   // eyes by turns: the cameras handed to xr::SetTurnView (FromAxes)
#include "sky_view_eyes.h"   // which eye's sky view and stamp a camera build writes in a pair (the replayed right eye's put in by the replay)
#include "views3d.h"      // [openxr] views_3d: F1 and F3 with the runtime's eyes (the pure maths)
#include "pointer3d.h"    // [openxr] pointer_3d: the game's mouse pointer drawn on F1's and F3's ground (the pure maths)
#include "frame_hitch.h"
#include "pair_world.h"   // [stereo] slow_pair_world: a pair after a pass of over a second is still the world's (the pure rule)
#include "gpu_fault.h"   // [debug] dred: the D3D12 device lost, told at once with what the GPU was doing (vr/gpu_fault.h)
#include "fog_height.h"  // [fog] game_fog_height: the game's own fog thinned with height in its own pass
#include "stall_guard.h"  // [debug] stall_guard: a queue that stands still for good (vr/stall_guard.h)
#include "ecl_hook.h"     // [debug] replay_marks 3, 4: the game's own ExecuteCommandLists calls (vr/ecl_hook.h)
#include "overlay_work.h" // eyes by turns: the overlay techniques rendered only with something on for them (replay::OverlaysHaveWork)
#include "no_reshade.h"   // [render] no_reshade: whether ReShade is loaded, the mode it is for, what is off (tools/no_reshade_test)
#include "native_depth.h" // [render] no_reshade: the scene's depth copied for the eye made (tools/native_d3d12_test runs it on WARP)
#include "native_fx_depth.h" // [render] no_reshade: the depth copied in flat too for our sky / fog / ceiling, drawn by only when fresh
#include "sky_paths.h"       // who draws the sky and the fog: the game's or ours, never both; the interface by the eyes only in stereo
#include "fx_pass_keep.h" // [render] no_reshade's runtime: the passes into the picture left out while their switches are off
#include "fx_picture_keep.h" // ... and only the act's sky and the ceiling's stone on the graphics card
#include "fx_native.h"    // [render] no_reshade: D2R_DepthFog.fx run by the plugin itself (the effect code below is templates over the runtime)
#include "monitor_eye.h"  // [render] monitor_eye: the window shows one eye (bearstonem/d2r-pcvr's desktop_one_eye)
#include "native_d3d12.h" // [render] no_reshade: the plugin's own D3D12 / DXGI hooks in place of ReShade's events (docs/plan_no_reshade.md)
#include "fog_before_fx.h" // [fog] game_fog_before_fx: the game's fog laid on the scene before its transparent resolve (no_reshade)
#include "native_rsapi.h"  // [render] no_reshade: vr/hud.cpp on those hooks, through a stand-in for ReShade's command list and device
#include "replay_pause.h" // when the replay may run: 240 settled pairs in a loaded area (vr/replay_pause.h)
#include "replay_groups.h"  // ... and the replay's submissions made of them (vr/replay_groups.h)
#include "frame_trace.h"   // [debug] frame_trace: where a slow pair's time went, the plugin's part counted apart (vr/frame_trace.h)
#include "health.h"        // [debug] health: one line per feature every 10 s, a mark where a chain is broken (vr/health.h)
#include "vrcam_internal.h"   // what the parts cut from this file share with it (docs/plan_modules.md)
#include "diag/diag.h"       // M1: the observers - frame log, pair times, draw profile, cb compare, replay trace, crash reports
#include "config/settings.h"  // M2: the settings (struct Settings, g_set)
#include "world/area.h"       // M4: the area (the biome the game set, [sky] outdoor)
#include "view/view_state.h"  // M5: which view (FullBody, ThirdPerson, Overhead, MenuFlat, ...)
#include "sky/sky_config.h"   // the sky's and the ceiling's configuration
#include "sky/sky_choice.h"   // M6: who draws the sky, the fog, the ceiling (FogWanted, OurSkyWanted, CeilWanted, ...)
#include "engine/engitem.h"   // M7: units the game draws as its own (the belt, the domes, the ceiling, the weather)
#include "combat/combat.h"    // the attacks' ticks (bow latency, the press trace, anim cut, throw trace, the hit by touch)
#include "frame/frame_order.h"   // M17: the frame's order (the draw of a pair, the update thread)
#include "view/camera.h"      // M16: the head, our camera, the views
#include "render/effects/effects.h"   // M15: our effect (its uniforms, the present, no_reshade)
#include "render/stereo/stereo.h"   // M14: the stereo (the replay, prevscan, frame sync, pair fx, pace)
#include "render/render_switches.h"   // M13b: the game's render switched (room heights, draw distance, walls, roofs, props)
#include "render/depth/depth.h"   // M13: the depth (the depth fix, the scene's depth for our effects)
#include "body/body.h"        // M12: the hero's facing and arms
#include "mp/pose_share.h"    // M11: [multiplayer] pose_share
#include "input/input.h"      // M10: the input (spell aim, ...)
#include "hud/hud_vr.h"       // M9: the interface (the looks per view, the crosshair, the label hooks, the floor pointer, the hand's inventory)
#include "sky/world_light.h"  // M8: the game's light and fog (day and night, its fogs taken out, the cave light, the lights near the hero)
#include "aim_point.h"
#include "leap_gaze.h"     // [gameplay] leap_by_gaze: Leap and Leap Attack where the head looks
#include "under_camera.h"  // [body] under_camera: the drawn body under the camera while the game's camera trails the hero
#include "point_cast_wire.h"   // [gameplay] teleport_circle: the skills cast onto a point aimed with an arc and a circle (vr/point_cast.h)
#include "weapon_extents.h"    // ... the arc leaves the staff's tip as the game's own model has it
#include "arm_reach.h"
#include "spell_aim_model.h"
#include "shot_trace_wire.h"
#include "throw_aim.h"
#include "throw_hand.h"
#include "throw_cast.h"
#include "throw_trace.h"
#include "panel_input.h"
#include "typed_keys.h"   // [openxr] laser_typing: the plugin's own keys carry kOwnKeyTag; a real one lets the keyboard layout in
#include "shoulder_swap_wire.h"   // [hands] shoulder_swap: weapon sets by the grip behind a shoulder
#include "shield_arm_wire.h"      // [hands] shoulder_draw, the shield: which forearm it is strapped to, or put away
#include "label_area.h"           // [hud] labels_whole_eye: item labels down to the picture's bottom (the rule; the hook is InstallLabelArea)
#include "spatial_audio.h"        // [audio] head_follow: the game's 3D sounds turn with your view (two FMOD hooks, from bearstonem/d2r-pcvr)
#include "sound_trace.h"          // [debug] sound_trace: the game's sounds by name as it makes and plays them
#include "clash_sound.h"          // [weight] clash_sound: a knock of steel on steel plays the game's block sound, as loud as it was hard
#include "stowed_block.h"         // [hands] shield_stow_no_block: a shield put away does not block (the game's block check told the hero has none)
#include "sewer_interaction.h"     // the Kurast sewer stairs taken with A while standing (the game's Interact query, when it finds none)
#include "ranged_hand_wire.h"     // [hands] shoulder_draw_ranged: a bow or crossbow put away behind a shoulder, drawn into the hand that reached
#include "shoulder_swap.h"
#include "weight_floor.h"         // [weight]: the floor a let-go weapon falls to is the game's ground under the hero
#include "weight_wire.h"          // [weight]: the grips that hold the weapon are taken from the layout, read as they really are
#include "belt_trigger_wire.h"    // [belt] hold_takes_trigger: the triggers while a belt bottle is in a hand (the game reads the left one as Shift: the drink must be a plain one)
#include "bow_draw.h"
#include "bow_draw_wire.h"   // [hands] bow_draw: a released draw's shot along its line
#include "bow_pitch_wire.h"  // [bow] pitch_aim: a bow's shot to the cell of what its 3D line meets (the stairs)
#include "arrow_flight_wire.h"  // [bow] arrow_flight: the shot arrow drawn by us from the bow along the shot's line
#include "bow_quiver.h"       // bowquiver::FindArrow: a bow skill left on the attack's slot, the armed one's name (HookSlotSkill)
#include "bow_quiver_wire.h"  // [bow] quiver: arms a bow skill (orbs, the string hand's buttons); ShotSkill answers the release
#include "throw_gesture_wire.h"
#include "quiver.h"          // [hands] javelin_quiver: the skill of the next throw chosen from the quiver (vr/quiver_wire.cpp)
#include "quiver_wire.h"
#include "throw_born.h"
#include "potion_hide.h"
#include "knock_props.h"   // [weight] knock_props: the level's small props knocked about (drawn only)
#include "own_effects.h"          // [render] dim_own_effects: the hero's own effects (Bone Armor ...) drawn fainter in first person
#include "head_shadow.h"          // [arms] head_shadow: the hidden head still casts its shadow
#include "grab_priority.h"   // [belt] grab_nearest, grab_trigger: a grip press and the belt potion or the weapon
#include "belt_drop.h"       // [hands] belt_drop_ground: the game's own drop of a belt potion (the belt slot's click, the cursor's drop)
#include "belt_glow.h"       // [hands] belt_inner_glow: a bottle in reach lit with the game's own highlight
#include "belt_bottle.h"     // [hands] belt_empty_bottle, belt_drop_ground: past the drink - the emptied bottle, a full one onto the ground (and bottle_fall.h, their fall)
#include "anim_cut.h"          // [bow] quick_release, [hands] quick_throw: the action-frame flag of the hero's bow attack / throw rows moved earlier (vr/anim_cut.cpp)
#include "press_trace.h"       // [debug] melee_press_trace: one line per attack press of hit by touch, from the press to what the game did with it (vr/press_trace.h)
#include "wheel_game.h"        // ... the A slot and the bank as the skill panel reads them
#include "bow_latency.h"       // [debug] bow_latency: the split of the second between a bow's release and its arrow (vr/bow_latency.h)
#include "calibration.h"
#include "body_calibration.h"   // [calibration] default_span: bodycal::DefaultSpan (the span the headset panel shows)
#include "first_start_wire.h"     // [onboarding]: the first-start guide in the headset (vr/first_start.h)
#include "crouch.h"          // [body] crouch: the hero crouches when you do (F4; vr/crouch.h)
#include "flatvr_pose.h"     // [flatvr]: FlatVR as good as native OpenXR - the roll back, the head's move, pairs on FlatVR's frames (docs/flatvr_sync.md)
#include "haptics.h"
#include "mob_bars.h"
#include "durability_figure.h"   // the item-condition figure: found, moved, sized or hidden
#include "plate_look.h"    // the monster's plate and the item labels kept apart in the interface layer's effect
#include "plate_world.h"
#include "notice_card.h"   // [hud] notice_card: the game's quest / level-up notice on a card in the room (NoticeCardTick)
#include "hand_inventory.h"   // [openxr] hand_inventory: away from town the inventory on the left hand, the left stick walking on (HandInventoryTick)
namespace hud { void SetPlateCut(bool want, const int32_t layout[4], float scale); void SetNoticeCut(bool want, const int32_t widget[4], float scale); }   // [hud] monster_bar_room: where the plate is, to be cut out (vr/hud.cpp)
namespace hud { void SetGameInterfaceScale(float k); }   // the game's own interface scale: where the corner map's box is looked for (vr/minimap_box.h)
namespace hud { void SetMapProjection(float ratio); }   // F4: our projection's x over y scale - the corner map's box drawn that much wider in a picture of another shape
#include "low_hp_game.h"   // [hud] low_hp_vignette: the red heartbeat vignette at low life (vr/low_hp.h)
#include "roof_keep.h"     // [render] keep_roofs: in F4 the roof of a building you walk into stays
#include "weather_place.h" // [sky] weather_first_person: the game's rain and snow hidden or round the viewer in first person
#include "roof_sides.h"    // [render] roofs_two_sided: roof materials built double-sided (their underside, not the sky)
#include "prop_hold.h"     // [stereo] props_once_a_pair: hanging lanterns stepped once a stereo pair
#include "merc_tint_game.h"   // [hud] merc_tint: the wounded merc tinted red by the effect (vr/merc_tint.h)
#include "hand_vitals_wire.h"   // [hud] hand_vitals: life and mana on the hand when the palm turns to the face (vr/hand_vitals.h)
#include "wrist_status_wire.h"   // [hud] wrist_status: the hero's companions and the effects on it, over a wrist turned to the eyes (vr/wrist_status.h)
#include "hand_orbs.h"   // [hud] hand_vitals_style orbs: life and mana as 3D balls on the wrists, drawn by the effect (vr/hand_orbs.h)
#include "render_resolution.h"   // [render] vr_resolution: a render size apart from the window's
#include "mod_layout.h"   // D2R VR as the D2RLoader mod D2R-VR: ReShade from the mod's own folder (shared/mod_layout.h)
#include "depth_source.h"   // [render] depth_fix_mode 2: the depth range written once a frame at the game's frame builder
#include <timeapi.h>   // timeBeginPeriod: the pacer's 1 ms sleeps
#pragma comment(lib, "winmm.lib")
#include "pad_mirror_shared.h"
#include "shield_smite_game.h"   // [gameplay] shield_smite: a bash of the left shield casts the game's own Smite
#include "parry_wire.h"           // [weight] parry_*: the monster's blow met by the weapon or the shield
#include "melee_trigger_wire.h"   // [gameplay] melee_touch_only: with hit by touch live the trigger does not attack (the plugin's own controls)
#include "double_swing_game.h"   // [hands] double_swing_by_hands: with a weapon in each hand the second hand's touch casts the game's own Double Swing / Frenzy
#include "melee_arm.h"        // [gameplay] melee_skill_armed: which skills arm (the log names them)
#include "melee_arm_game.h"   // [gameplay] melee_skill_armed: a melee skill's button arms it; every hit by touch is then that skill (vr/melee_arm.h)
#include "armed_icon.h"       // [gameplay] melee_skill_armed_icon: the armed skill's icon in the mana medallion (vr/armed_icon.h)
#include "wheel_icons.h"      // ... the game's own skill icon, as the skill panel's tiles draw it
#include "hit_claim.h"   // whose blow just landed: the presses claim it, the hit observer's hand source takes it (the controller that buzzes)
