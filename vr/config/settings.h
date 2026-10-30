// vr/config/settings.h - the plugin's settings: d2r_vr.ini as the VR layer reads it, every field an atomic the update thread's
// LoadSettings writes (vr/vrcam.cpp, again whenever the ini changes) and every other thread reads (docs/plan_modules.md, step M2).
#pragma once

#include <atomic>
#include <cstdint>

#include "d2r_vr_shared.h"   // D2RVR_TYPE_COUNT
// the defaults the fields start at, from the features' pure headers
#include "../body_calibration.h"
#include "../grip_zone.h"
#include "../lean_gait.h"
#include "../point_cast.h"
#include "../skeletons.h"
#include "../slide_kinds.h"
#include "../staff_upright.h"
#include "../stick_pitch.h"
#include "../throw_aim.h"
#include "turn_mode.h"

namespace vrc {

// Settings (d2r_vr.ini, re-read on change)
struct Settings {
    // [mode] - the settings program's Home page, and F1..F4 in the game.
    // platform 0 flat (monitor, mouse, no BodyWalk), 1 VR (headset, BodyWalk, FlatVR).
    // Flat views: 1 the game's own camera, 2 third person, 3 first person.
    // VR views: 1 from above, 2 third person, 3 first person with the hero
    // animated by the game, 4 first person with the body on the player's.
    std::atomic<int>   platform{1};
    std::atomic<int>   flatView{3};
    std::atomic<int>   vrView{3};
    std::atomic<float> fov{60.0f};          // vertical FOV of the game camera; match the FlatVR screen's angular height
    std::atomic<bool>  fovFromFlatVR{true}; // fov follows the FlatVR screen's size and distance (BodyWalk's settings)
    std::atomic<float> distance{-1.0f};     // camera behind the eye point along the view; below 0 = in front of it (out of the head)
    std::atomic<float> height{8.0f};        // pivot above the ground look-at point (world units)
    std::atomic<bool>  heightAuto{true};    // first person: the eyes at the hero's own (each class its height); height unused
    std::atomic<float> eyeOffset{0.0f};     // added to that, world units
    std::atomic<bool>  followJump{true};    // the camera goes up when the animation lifts the hero (Leap)
    std::atomic<float> jumpFrom{0.3f};      // hip rise (model units) still taken for the stride's bob
    std::atomic<float> pitch{-60.0f};       // camera pitch relative to the game's own (deg); -60 = level, more negative = higher
    std::atomic<float> flatShiftX{0.0f}, flatShiftY{0.0f};   // [camera] flat_shift_x / _y: flat F2 / F3, the picture moved by % of its width (right) / height (down)
    std::atomic<bool>  stickPitchF2{false}; // [camera] stick_pitch_f2: VR F2, the right stick's up / down tilts the camera (vr/stick_pitch.h)
    std::atomic<bool>  stickPitchFlat{true}; // [camera] stick_pitch_flat: flat F2 / F3, a real pad's right stick up / down looks up / down as the mouse does
    std::atomic<float> stickPitchSpeed{stickpitch::kSpeedDefault};   // deg/s at a full push
    std::atomic<float> stickPitchMin{stickpitch::kMinDefault}, stickPitchMax{stickpitch::kMaxDefault};   // the highest / lowest the stick takes the camera, [camera] pitch's units
    std::atomic<float> stickPitchSign{1.0f};   // flip if pushing up looks down
    std::atomic<int>   rings{4};            // [render] rings: room rings drawn around the hero (cleanroom/drawdist)
    std::atomic<float> modelRadius{1000.0f}; // [render] model_radius: houses, props, units drawn out to this (game: 150)
    std::atomic<float> bigRadius{0.0f};     // [render] big_radius: trees, houses, walls kept farther by their size (0 = off; D2R VR Unleashed's)
    std::atomic<int>   renderPoolMb{64};    // [render] render_pool_mb: the game's Renderer Pool at least this big (game: 12)
    std::atomic<int>   entityPoolMb{280};   // [render] entity_pool_mb: its Entity System Pool (game: 70) - cleanroom/pools
    std::atomic<int>   hudMap{0};           // [hud] map: 0 in the picture, 1 out of it, into its own texture
    std::atomic<bool>  mapDirection{true};  // [hud] map_direction: an arrow on the hero's marker of the game's map, the way our camera looks (vr/map_direction.h)
    std::atomic<bool>  mapTurn{false};      // [hud] map_rotate or map_compass: the way the head looks, for the map in the hand (vr/map_turn.h, xr.cpp)
    // Per view since the Home page (2026-10-05): [hud_top], [hud_third], [hud_floor], [hud_inside]
    // - the Interface tab of each view - falling back to [hud] where unset.
    std::atomic<float> barNearTop{0.0f}, barNearThird{0.0f}, barNearInside{0.0f};   // bar_near per view
    std::atomic<float> barSizeTop{1.0f}, barSizeThird{1.0f};   // [hud_top] / [hud_third] bar_size, %: the toolbar in the picture, smaller or larger
    std::atomic<int>   classicTop{0}, classicThird{0};   // classic per view (from above, from behind)
    std::atomic<float> mapZoomInside{1.0f};  // [hud_inside] map_zoom: first person without the body, the map in the picture this much bigger
    // [hud_floor]: VR F3, the game on the floor (head-locked screen) - its own toolbar, it had F2's.
    std::atomic<int>   classicFloor{0};     // [hud_floor] classic (not on the settings page): 1 = hung in the room
    std::atomic<float> barNearFloor{0.0f}, barSizeFloor{1.0f};
    // [hud_floor] labels_*: the names of items on the ground (2026-10-06, "too big and too opaque")
    std::atomic<float> labelsAlphaFloor{0.5f};   // labels_alpha: their dark box, 1 = as the game draws it, 0 = none
    std::atomic<float> labelsSizeFloor{1.0f};    // labels_size, %: the names and their box smaller (needs the game's label code, LabelHooks)
    std::atomic<bool>  labelsNativeFloor{true};  // labels_native (not on the settings page): 0 = never the game's code, the box faded as a picture
    std::atomic<bool>  labelsWholeEye{false};    // [hud] labels_whole_eye: the label layout's height check given the whole picture (vr/label_area.h), every view
    // The same two for the other views (2026-10-06): [hud_top] F1, [hud_third] F2, [hud] F4 - the game's own by default.
    std::atomic<float> labelsAlphaTop{1.0f}, labelsAlphaThird{1.0f}, labelsAlphaBody{1.0f};
    std::atomic<float> plateAlpha{1.0f};   // [hud] monster_alpha: the name plate over the target monster, 1 = as the game draws it
    std::atomic<float> plateSize{1.0f};    // [hud] monster_size: that plate larger or smaller about its middle, 1 = the game's
    std::atomic<float> plateFar{1.0f};         // [hud] monster_far: the plate this share of its size for a monster 10 m away and farther (1 = no change)
    std::atomic<float> plateDepthBody{0.0f};   // [hud] monster_depth_body: the plate in the picture at its monster's depth, 1 = exactly there (0 = where the game leaves it)
    std::atomic<float> plateNearBody{0.0f};   // [hud] monster_near_body: that plate's own stereo depth in first person (absent in the ini: labels_near_body, as before)
    // [hud] allies_x / allies_y: the allies' portraits (the mercenary, the party) moved on the screen, uv (+x right,
    // +y DOWN; the ini's allies_y is + up, %)
    std::atomic<float> alliesX{0.0f}, alliesY{0.0f};
    // [hud] durability_*: the item-condition figure (the knight whose parts go red as the items wear; HUDWarnings > DurabilityWarning) in first person (F4): moved
    // (uv, +x right, +y DOWN; the ini's durability_y is + up, %), sized about its middle, or hidden
    std::atomic<bool> durHide{false};
    std::atomic<float> durX{0.0f}, durY{0.0f}, durScale{1.0f};
    std::atomic<float> labelsSizeTop{1.0f}, labelsSizeThird{1.0f}, labelsSizeBody{1.0f};
    // bar_x / bar_y per view: the toolbar moved on the screen, uv (+x right, +y DOWN; the ini's bar_y is + up, %)
    std::atomic<float> barXTop{0.0f}, barYTop{0.0f}, barXThird{0.0f}, barYThird{0.0f}, barXFloor{0.0f}, barYFloor{0.0f};
    // [hud_floor] map_size / map_near / map_x / map_y: the corner map on the floor (F3), as its toolbar
    std::atomic<float> mapSizeFloor{1.0f}, mapNearFloor{0.0f}, mapXFloor{0.0f}, mapYFloor{0.0f};
    std::atomic<float> barNear{0.0f};       // [hud] bar_near: from above and behind, the toolbar nearer (+) or farther (-), % of the half screen per eye
    std::atomic<float> labelsNear{0.0f};     // [hud] labels_near: from above in stereo, labels nearer (+) / farther (-), % of the half screen per eye
    std::atomic<float> labelsTilt{0.0f};
    std::atomic<float> labelsNearBody{0.0f};   // [hud] labels_near_body: the same in first person (F4) - native too
    std::atomic<float> labelsDepthBody{0.0f};  // [hud] labels_depth_body: F4, each item label at its item's depth, 1 = exactly (0 = the game's)
    std::atomic<float> labelsFarBody{1.0f};    // [hud] labels_far: F4, a label 10 m off and farther this much of labels_size (1 = no change)
    // [hud_top] / [hud_third] pointer_near, pointer_tilt: the mouse pointer FlatVR lays into the stereo pair, F1 / F2,
    // in the labels' units - nearer (+) and how much nearer at the bottom of the screen than at the top
    std::atomic<float> pointerNear[2] = {0.0f, 0.0f}, pointerTilt[2] = {0.0f, 0.0f};
    // [hud_top] / [hud_third] pointer_top, pointer_mid, pointer_bottom: the pointer's depth at three heights of the
    // screen, the labels' units, a smooth curve through them - a straight line fitted the bottom and the middle and
    // came apart far off at the top (2026-10-06). Unset: from pointer_near / pointer_tilt (about the middle).
    std::atomic<float> pointerAt[2][3] = {};
    std::atomic<float> crosshairDepth{0.0f};   // [hud_third] crosshair_depth: VR F2 with mouse look, the crosshair's depth (the pointer's units)
    std::atomic<bool>  crosshairOn{true};      // [hud_third] crosshair: VR F2 with mouse look, the crosshair shown (off = no pointer at all)
    std::atomic<float> crosshairSize{100.0f};  // [hud_third] crosshair_size: %, of the size the window's height picks (32 / 48 / 64 px)
    // [hud_third] crosshair_depth is its depth looking ahead, crosshair_depth_down looking straight down; in between by the sine of the look down
    std::atomic<float> crosshairDepthDown{0.0f};
    std::atomic<float> pointerTopSize[2] = {100.0f, 100.0f};   // [hud_top] / [hud_third] pointer_top_size: %, its size at the top of the screen     // [hud] labels_tilt: ... and how much nearer the bottom of the screen than the top
    std::atomic<int>   hudClassic{0};       // [hud] classic: from above and behind 0 = toolbar and map in the picture (own size), 1 = as in first person
    std::atomic<int>   hudHide{0};          // [hud] hide: 0 nothing, 1 the toolbar, 2 the whole interface out of the picture (hud.cpp)
    std::atomic<bool>  mouseLook{false};    // [input] mouse_look: the mouse turns the view, W A S D walk (F9 toggles)
    std::atomic<float> mouseSpeed{0.15f};   // [input] mouse_speed: degrees per pixel
    std::atomic<float> nearClip{1.5f};      // nothing closer than this is drawn: hides the hero's own head and hair
    std::atomic<float> camSide{0.0f};       // eye point to the right (+) / left (-) of where you look, world units
    std::atomic<float> camForward{0.0f};    // eye point ahead (+) / behind (-) along where you look, world units
    std::atomic<bool>  neckModel{true};     // [camera] neck_model: the head turns about the neck, below and behind the eyes
    std::atomic<float> neckUpCm{10.0f};     // [camera] neck_up_cm: eyes above that pivot
    std::atomic<float> neckFwdCm{9.0f};     // [camera] neck_forward_cm: eyes in front of it
    std::atomic<bool>  headYaw{true};       // yaw from the headset via BodyWalk (Game Link)
    std::atomic<float> yawSign{1.0f};       // flip if turning the head left turns the view right
    std::atomic<float> yawScale{1.0f};
    std::atomic<bool>  headPredict{false};  // carry the head on between BodyWalk's updates (off: FlatVR places the screen by the sample's own stamp)
    // [flatvr] - FlatVR as good as native OpenXR (docs/flatvr_sync.md); each off = as before
    std::atomic<int>   flatSync{0};         // frame_sync: 0 our timer; 1 a pair on FlatVR's next frame (every other one when a pair does not fit); 2 on every new one
    std::atomic<float> flatSyncLate{0.5f};  // frame_sync_late: a FlatVR head older than this share of a frame at a pair's start is let go
    std::atomic<bool>  flatLevel{false};    // head_level: pitch and roll as the headset has them (F4)
    std::atomic<bool>  flatPosition{false}; // head_position: the camera moves with the head in the room (F4)
    std::atomic<bool>  flatRoomCentre{false};  // room_centre: the yaw from the room's own zero against our F11; BodyWalk's reset = F11
    std::atomic<bool>  flatEnterCentre{false}; // recenter_on_enter: straight ahead taken when a game is entered
    std::atomic<bool>  headPitch{true};     // pitch from the headset (BodyWalk with BW_TrackingData v2)
    std::atomic<float> pitchSign{1.0f};     // flip if looking up tilts the view down
    std::atomic<bool>  headRoll{true};      // roll from the headset (BodyWalk with BW_TrackingData v3)
    std::atomic<float> rollSign{1.0f};      // flip if tilting the head right tilts the world the wrong way
    std::atomic<bool>  stickRotate{true};   // turn the XInput left stick by the camera yaw
    std::atomic<float> stickSign{1.0f};     // flip if pushing forward walks backwards after a turn
    std::atomic<float> stickSquash{1.0f};   // vertical squash of the stick after turning (iso screen); 1 = none
    std::atomic<bool>  stickExact{true};    // project through the real camera axes instead of sign/squash
    std::atomic<float> stickAngleScale{1.0f}; // approximate mode: the stick turns by camera yaw times this
    std::atomic<bool>  rightTurn{true};     // right stick X turns the body (camera + walking)
    std::atomic<float> turnSpeed{120.0f};   // deg/s at full deflection (smooth turn)
    std::atomic<float> snapAngle{0.0f};     // >0: snap turn by this many degrees per flick instead (0 while [turn] smooth is on: turnmode::EffectiveSnapAngle)
    std::atomic<float> turnSign{1.0f};      // flip if right turns left
    std::atomic<bool>  afr{false};          // alternate-frame stereo: left eye, right eye, left...
    std::atomic<float> ipd{0.27f};          // eye separation in world units (hero ~7.5 tall = 1.8 m)
    std::atomic<bool>  afrSwap{false};      // the presented frame is the other eye: swap the halves
    std::atomic<bool>  fogOn{false};        // D2R_DepthFog.fx driven from here through ReShade
    std::atomic<float> fogStart{150.0f};    // world units from the eye
    std::atomic<float> fogEnd{600.0f};
    std::atomic<float> fogCaveStart{150.0f};   // [fog] caves_start / caves_end: the same, in caves and dungeons (every area off [sky] outdoor)
    std::atomic<float> fogCaveEnd{600.0f};
    std::atomic<float> fogCaveStrength{1.0f};  // [fog] caves_strength: how much the full fog covers in caves, 0..1
    std::atomic<float> fogCurve{1.6f};
    std::atomic<float> fogBlur{3.0f};      // the fog over a 5x5 grid this many pixels apart: holes and specks in the depth melt away
    std::atomic<bool>  fogLight{false};    // [fog] light: the fog's short way for weak graphics cards (D2R_DepthFog.fx LightFog)
    std::atomic<bool>  skyOn{false};        // sky in the void, same shader as the fog
    std::atomic<bool>  gameSky{false};      // [sky] game_sky: our painted sky as the game's own dome (engitem::SkyDome)
    std::atomic<bool>  domeFogHorizon{true};   // [sky] dome_fog_horizon: the game's dome's low part into the act's fog colour (D2R_DepthFog.fx DomeHorizon)
    std::atomic<bool>  gameSkybox{false};   // [sky] game_skybox: at the Summits the game's dome laid as far as the depth holds (an option; off: as any act 5 area)
    std::atomic<bool>  summitBackdropHidden{true};   // [sky] summit_backdrop_hidden: the Summit's ground picture (skybox01) hidden in first person (vr/own_effects.h)
    std::atomic<int>   weatherFirstPerson{0};   // [sky] weather_first_person: the game's rain and snow in VR F4 / flat F3 - 0 the game's place, 1 hidden, 2 round the viewer (vr/weather_place.h)
    std::atomic<int>   gameSkyTest{0};         // [debug] game_sky_test: 1 the dome in magenta, 2 the game's own Summit sky model, 3 its unit naming our model, 4 our model under its path
    std::atomic<float> gameSkyRadius{200.0f};   // [sky] game_sky_radius: the dome's radius, world units (200: a near horizon under the game's fog)
    std::atomic<bool>  gameDomeCaves{false};    // [sky] game_dome_caves: a black dome round the eye underground (engitem::CaveDomeFrame)
    std::atomic<float> gameDomeCavesSize{1.0f}; // [sky] game_dome_caves_size: its radius as a share of [render] model_radius
    std::atomic<float> skyClouds{1.0f};     // times the biome's own cloud cover
    std::atomic<bool>  skyDrift{false};     // [sky] drift: the painted sky turns slowly (off: it stands still - painted stars went round with the clouds)
    std::atomic<bool>  skyDayNight{true};   // [sky] day_night: the sky and the open-air fog follow the game's own light
    std::atomic<float> lightDay{128.0f};    // [sky] light_day / light_night: the game's light (0..255) that counts as full day / full night
    std::atomic<float> lightNight{65.0f};
    std::atomic<float> nightBright{0.25f};  // [sky] night_brightness: the sky and the fog at full night, times their day brightness
    std::atomic<bool>  skyAlways{false};    // whatever the biome (testing): caves get a sky too
    std::atomic<bool>  arcaneSky{true};     // [sky] arcane_sky: the Arcane Sanctuary's own star layers all round (after bearstonem/d2r-pcvr), [sky] enabled or not
    std::atomic<bool>  arcaneFog{false};    // [sky] arcane_fog: the fog in the Arcane Sanctuary too, fading into its sky (off: no fog under that sky, as before)
    std::atomic<bool>  skyHidePanels{true}; // no sky while a side panel shifts the picture
    std::atomic<bool>  skyOverRims{true};   // [sky] cover_rims: the sky over the dark rim round things against the void (bearstonem/d2r-pcvr 4071a4a), VR F4
    std::atomic<bool>  fogCaves{true};      // [fog] caves: the fog in caves and dungeons too (every area off [sky] outdoor)
    std::atomic<bool>  fogEaseIn{false};    // [fog] ease_in: the fog eased in over its first 40 units, no hard ring (D2R VR Unleashed's, shared/unleashed_fog.h)
    std::atomic<bool>  fogNoRiver{false};   // [fog] no_fog_river: no added fog in the River of Flame, as caves=0 underground (D2R VR Unleashed's)
    std::atomic<bool>  ceilOn{false};       // [ceiling] enabled: a vault over the void in caves (first person only)
    std::atomic<bool>  gameCeiling{false};       // [ceiling] game_ceiling: the vault as the game's own lit rock slab (engitem::CeilingFrame), VR F4
    std::atomic<bool>  gameCeilingWall{false};   // [ceiling] game_ceiling_wall: its rim bent down into a wall of the same rock (the void past its edge closed)
    std::atomic<bool>  gameCeilingBeams{false};  // [ceiling] game_ceiling_beams: the barracks' own wooden ceiling frame under the slab (engitem::CeilingFrame)
    std::atomic<bool>  ceilFromWalls{false};     // [ceiling] height_from_walls: each room's ceiling on its walls' top (measured from the game's models)
    std::atomic<bool>  gameCeilingDome{false};   // [ceiling] game_ceiling_dome: the cathedral's dome over its chapel in the game's ceiling (engitem::CeilingFrame)
    std::atomic<bool>  gameCeilingDomesTall{false};   // [ceiling] game_ceiling_worldstone_dome: the same over the Worldstone, level 132 (ceilmeasure::kWorldstoneBiome)
    std::atomic<float> gameCeilingDomeSize{1.0f};   // [ceiling] game_ceiling_dome_size: times the measured dome round its middle (gameceiling::DomeSize)
    std::atomic<float> worldstoneWiderM{10.0f};       // [ceiling] game_ceiling_worldstone_dome_wider_m: the Worldstone's dome that many metres wider (gameceiling::WiderSize)
    std::atomic<float> gameCeilingDomeRise{1.0f};   // [ceiling] game_ceiling_dome_rise: times the least rise that passes over the canopy (1 .. 2)
    std::atomic<float> ceilHeight{18.0f};   // a room with no height_<biome> of its own (LoadCeilingBiomes: kCeilHeightAlone), world units
    std::atomic<float> ceilScale{10.0f};    // [ceiling] scale: world units per tile (the test grid's cell)
    std::atomic<float> ceilBright{1.0f};    // [ceiling] brightness
    std::atomic<float> ceilRelief{6.0f};    // [ceiling] relief: how far the rock hangs down, world units (0 = flat)
    std::atomic<int>   ceilSteps{12};       // [ceiling] steps: the ray's steps through the rock (the frame's cost)
    std::atomic<float> ceilLight{25.0f};    // [ceiling] light_radius: world units from the hero where his light is down to half
    std::atomic<bool>  ceilTorches{false};  // [ceiling] torches: the game's own lights (found by the memory scan) light the ceiling
    std::atomic<bool>  ceilPictureFire{false};   // [ceiling] fire_from_picture: fire seen in the picture lights it instead - any fire, no scan
    std::atomic<float> ceilTorchBright{0.6f};   // [ceiling] torch_brightness
    std::atomic<float> ceilTorchRadius{20.0f};  // [ceiling] torch_radius: world units where a torch's light is down to half
    std::atomic<float> ceilFireNearer{0.0f};    // [ceiling] fire_nearer: world units a fire found in the picture is put nearer the eye
    std::atomic<float> ceilTorchDist{80.0f};    // [ceiling] torch_distance: torches farther from the hero fade out (behind walls)
    std::atomic<bool>  ceilMonsters{false};      // [ceiling] monster_lights: the Fallen's torches light the ceiling (off: their units are not even read)
    std::atomic<float> ceilMonsterBright{0.35f}; // [ceiling] monster_brightness: the Fallen's torches, times the torches' glow
    std::atomic<float> ceilWall{200.0f};         // [ceiling] wall_distance: the stone wall round the hero closing the void past the floor (0 = none)
    std::atomic<float> ceilFloor{5.0f};          // [ceiling] floor_depth: stone this far under the hero's floor past the drawn floor's edge (0 = none)
    std::atomic<float> ceilTorchWarm{0.6f};      // [ceiling] torch_warmth: the game's pale torch colour toward fire's orange, 0..1
    std::atomic<float> ceilHalo{1.0f};          // [ceiling] torch_halo: the game's glow round a flame over the ceiling, 0..1
    std::atomic<float> ceilWet{0.35f};      // [ceiling] wet: how much the rock shines, like the walls
    std::atomic<float> ceilDetail{0.8f};    // [ceiling] detail: world units the picture's light parts stand out of the rock
    std::atomic<float> ceilContrast{1.6f};  // [ceiling] contrast: the picture's fine detail times this
    std::atomic<int>   armsMode{0};        // 0 game animation, 1 test pose (arms ahead), 2 controllers
    std::atomic<bool>  hideHead{false};     // shrink the hero's head away (first person)
    std::atomic<float> armScale{1.0f};      // reach on top of the hero/user height ratio
    std::atomic<bool>  fullReach{true};     // [arms] full_reach: a hand out of the arm's reach is drawn on its controller all the same - the shoulder comes along, then the arm lengthens (F4; vr/full_reach.h)
    std::atomic<bool>  defaultSpan{true};   // [calibration] default_span: nothing measured - the arms fitted to a person's span for the eye height (F4; bodycal::DefaultSpan)
    std::atomic<bool>  bodywalkPad{true};   // [input] bodywalk_pad: the game's pad is BodyWalk's own report (pad_mirror_shared.h) - no virtual pad needed
    std::atomic<bool>  interactOnePress{false};   // [input] interact_one_press: one target per press of "pick up / interact" (vr/interact_press.h), F4 only
    std::atomic<int>   inventoryPad{0};     // [input] inventory_button: the pad button "D2R: Inventory" sends - 0 Menu (Start), 1 View (Back)
    std::atomic<bool>  aAttackOnly{false};  // [input] a_attack_only: pad A never picks up or interacts; "D2R: Pick up / interact" does
    std::atomic<bool>  flatKeyMove{true};   // [input] flat_keyboard_move: flat W A S D walk through the game's own keyboard move (0x8A960), no pad
    std::atomic<bool>  vrKeyWalk{true};     // [input] vr_keys_walk: VR view F2 without mouse look - W A S D walk where the camera looks, the mouse stays the game's
    std::atomic<bool>  directWalk{true};    // [input] direct_walk: VR F2-F4, BodyWalk's stick walks the hero straight (StickGet / KeyMove hooks), not through the pad
    std::atomic<bool>  walkByLean{false};   // [direct] walk_by_lean: VR F4, the controller's left stick walks up to walk_by_lean_pct of its lean, runs past it (vr/lean_gait.h)
    std::atomic<float> walkByLeanLimit{leangait::kDefaultLimit};   // [direct] walk_by_lean_pct / 100
    std::atomic<float> bwWalkLift{0.0f};    // [input] bodywalk_walk_lift: F4, BodyWalk's stick from this long up counts as just past the game's dead zone (vr/walk_path.h Lift); 0 off
    std::atomic<bool>  walkStartLog{false}; // [debug] walk_start_log: one line per walk start of BodyWalk's - stick stirred, past the dead zone, the game's walk given, the hero moved (vr/walk_latency.h)
    std::atomic<bool>  flatNoPad{true};     // [input] flat_no_pad: with it, flat mode shows the game no pad at all (its UI never turns to A/B/X/Y)
    std::atomic<bool>  flatPadOnTouch{true};   // [input] flat_pad_on_touch: ... until the real pad is touched - then it goes through
    std::atomic<bool>  flatCrosshair{true};  // [input] flat_crosshair: flat mouse look, the pointer is our crosshair, not the game's gauntlet
    std::atomic<bool>  flatClickShoot{true}; // [input] flat_click_shoot: flat mouse look, a click with no target under the crosshair = a shot there, never a walk
    std::atomic<bool>  flatAimGround{true};  // [input] flat_aim_ground: flat with our camera, the mouse's point is the cell of the ground drawn under it (raised floors)
    std::atomic<bool>  flatBodyPick{true};   // [input] flat_body_pick: flat F2 / F3, a unit or object clicked by its body (vr/flat_body.h)
    std::atomic<bool>  flatPortalNoOval{true};   // [input] flat_portal_no_oval: flat F2 / F3, the town portal's black oval not drawn (vr/portal_oval.h)
    std::atomic<bool>  portalNoOval{false};      // [render] portal_no_oval: the same in VR, F2 / F3 / F4 (F1 from above keeps it)
    std::atomic<bool>  portalLookHorizon{false}; // [input] portal_look_horizon: flat F3, the look back to the horizon after a portal or waypoint (vr/portal_auto.h)
    std::atomic<bool>  portalAuto{false};        // [input] portal_auto: flat F2 / F3, walking into a portal enters it by the game's own click (vr/portal_auto.h)
    std::atomic<float> portalAutoRadius{4.0f};   // [input] portal_auto_radius: how near (world units, 2 a cell)
    std::atomic<int>   portalAutoDelay{150};     // [input] portal_auto_delay_ms: how long that near, walking, before it is entered
    std::atomic<bool>  keepRoomHeights{false};   // [render] floors_stay: a room of the level keeps the height the game first drew it at (no floor that slides on a way back)
    std::atomic<bool>  findPrevMatrix{false};
    std::atomic<bool>  dlssPrevFix{false};
    std::atomic<int>   dlssMv{2};              // [render] dlss_mv: 0 off, 1 check vrcam's matrices against the game's motion vectors (logs), 2 hand DLSS the same eye's     // [render] dlss_prev_fix: the game's previous-frame view made the same eye's (experiment)   // [debug] find_prev_matrix: once, where the game keeps past frames' view matrices (DLSS motion vectors)
    std::atomic<bool>  frameLog{false};     // [debug] frame_log: every pair, view, hero pose and present to d2r_vr_frames.csv
    std::atomic<bool>  poseOrderLog{false}; // [debug] pose_order: which of eye / look-at / hero matrix is stale per pass, to the log
    std::atomic<bool>  flatPickLog{false};  // [debug] flat_pick: flat mouse look, what the game's targeting sees at each click and when its pixel leaves the middle
    std::atomic<float> facingSign{1.0f};    // flip if the arms swing the wrong way when the model turns
    std::atomic<int>   facingSource{1};     // [hands] facing_source: 1 the hero's TransformComponent (structural), 0 the memory search
    std::atomic<float> facingHoldMs{4000.0f};   // [hands] facing_hold_ms: the last facing kept this long while none reads
    std::atomic<bool>  dollRebuild{true};   // [hands] doll_rebuild: the doll held through the hero's model rebuilt - a weapon swap (skel::OursNow)
    std::atomic<bool>  dollTrace{false};    // [debug] doll_trace: one line each time the doll lets go (the facing, the hero's rig), with why and what instead
    std::atomic<float> handUp{0.0f}, handFwd{0.0f}, handSide{0.0f};   // cm, both hands
    std::atomic<float> handsFollowCam{1.0f}; // [arms] follow_camera: 1 = hands hang off the camera, 0 = off the eye point ([camera] distance not applied)
    std::atomic<int>   bowMode{4};          // attack aim: 0 off, 1 stick while held (ignored), 2 shift+click (flips the UI to mouse), 3 turn first, then A, 4 the game's target vector turned to the hand
    std::atomic<float> turnMs{80.0f};
    std::atomic<float> aimYawDeg{0.0f};     // [bow] aim_yaw: the hand's aim turned this many degrees about the vertical, + = right
    std::atomic<float> staffAimYawDeg{0.0f};   // [bow] staff_aim_yaw: the same for a staff, in place of aim_yaw
    std::atomic<float> xbowAimYawDeg{0.0f};
    std::atomic<bool>  xbowTwoHands{true};    // [bow] xbow_two_hands: a crossbow as the staff - the right hand holds it, the left takes it with the grip
    std::atomic<bool>  phantomRay{false};      // [debug] phantom_ray: the crossbow's shot drawn as a red line (D2R_DepthFog.fx PS_Phantom)
    std::atomic<bool>  boneAxes{false};
    std::atomic<bool>  weaponDiag{false};      // [debug] weapon_diag: the 2H weapon's bones, item and entity, logged once a second
    char shrinkBone[48] = {};                  // [debug] shrink_bone: a bone of the hero's drawn shrunk (what hangs on it vanishes)        // [debug] bone_axes: the crossbow bone's own axes drawn (X green, Y blue, Z yellow)
    // [hands] xbow_line_pitch / _yaw / _roll: the shot line alone turned, degrees; 34/15 laid by the user
    // onto the model with its axes drawn (2026-10-05)
    std::atomic<float> xbowLine[3] = {34.0f, 15.0f, 0.0f};
    std::atomic<bool>  xbowGunFrame{true};    // [hands] xbow_gun_frame: 0 = the crossbow in the game's grip, like the staff
    std::atomic<int>   xbowStockAxis{2};      // [hands] xbow_stock_axis: the bone axis laid along the controller, +-1..3; 0 = from the grip
    std::atomic<int>   xbowHand{1};           // [hands] xbow_hand: the wrist the game hangs a crossbow on (0 right, 1 left), for its grip
    // [bow] *_range_one / *_range_two: the attack point this many cells out along the
    // aim with one hand on the weapon / with both - steadier held in two.
    std::atomic<float> xbowRangeOne{5.0f}, xbowRangeTwo{30.0f}, staffRangeOne{5.0f}, staffRangeTwo{30.0f};    // [bow] xbow_aim_yaw: the same for a crossbow (along the controller's -Z); unset = 0
    // [bow] caster_range / hand_range: how far out along the hand a spell's point goes ([hands] spell_aim_model on, F4), cells - a wand or orb / bare
    // hands (a staff: staff_range_one / staff_range_two); below 3 the game's own point
    std::atomic<float> casterRange{20.0f}, handRange{5.0f};
    std::atomic<bool>  aimByArm{false};     // [bow] aim_distance_by_arm: a spell's aim point (wand, orb, a staff in one hand, bare hands) follows how far the aiming arm is stretched (vr/arm_reach.h), F4 only
    std::atomic<float> aimNearCells{3.0f};  // [bow] aim_near_cells: the distance with the arm at the body (the far end is the weapon's own)
    std::atomic<float> aimRange{20.0f};     // mode 4 with a bow: the attack point this many grid cells along the hand       // mode 3: how long the stick turns the hero before A goes through
    std::atomic<int>   bowHand{1};          // 0 right, 1 left: the aiming hand of weapon set I
    std::atomic<int>   bowHand2{0};         // the same for weapon set II
    std::atomic<bool>  rangedLeft{true};    // a bow or crossbow is always aimed with the left hand (it sits in no right hand)
    std::atomic<bool>  staffTwoHands{true}; // [bow] staff_two_hands: with a staff, aim from the left hand to the right one
    std::atomic<bool>  staffHands{true};    // [hands] follow_staff: with a staff, the wrists turn with it, not with the controllers
    std::atomic<bool>  staffFreeLeft{true}; // [hands] staff_free_left: the right hand holds the staff, the left takes it with the grip
    // [hands] spell_aim_model: the aim along the hand from the hero as drawn - a staff's, wand's or orb's axis, else the hand's fingers or palm
    // (vr/spell_aim_model.h), F4 only, with no hand-set correction; off (or no fresh pose) = the controller's axes and [bow] aim_yaw / staff_aim_yaw
    std::atomic<bool>  spellAimModel{false};
    // [hands] staff_upright_arm: with spell_aim_model, a staff held upright aims along the drawn arm (shoulder -> palm), blended with its own axis as it
    // tilts (vr/staff_upright.h), F4 only; staff_upright_full_deg / _none_deg: off vertical, the arm alone / the staff alone. Off = the staff's line, as before
    std::atomic<bool>  staffUprightArm{false};
    std::atomic<float> staffUprightFullDeg{staffupright::kFullDeg}, staffUprightNoneDeg{staffupright::kNoneDeg};
    std::atomic<bool>  spellAimRay{false};     // [debug] spell_aim_ray: the spell's ray drawn live (green from the model, orange the old controller line)
    std::atomic<bool>  spellAim{true};      // [hands] spell_aim: a spell that flies goes along the hand, no auto-target (mode 4); off = the game's
    std::atomic<bool>  aimNoChase{true};    // [gameplay] aim_no_chase: the cast's own look at the controller's target (0x1454A0) finds none for a directed cast
    std::atomic<bool>  leapByGaze{true};    // [gameplay] leap_by_gaze: Leap and Leap Attack go where the head looks (F4, vr/leap_gaze.h); off = the game's aim
    std::atomic<bool>  teleportCircle{true};   // [gameplay] teleport_circle: the point skills (Teleport, Fire Wall, Meteor ...) by a tap where the head looks, by a hold to an arc's circle (F4); off = the game's
    std::atomic<bool>  leapArc{false};         // [gameplay] leap_arc: Leap and Leap Attack on the arc too (off: leap_by_gaze alone)
    std::atomic<bool>  leapArcHead{false};     // [gameplay] leap_arc_head: their arc from the drawn hero's belly, aimed by the head (off: from the weapon or the hand)
    std::atomic<float> leapHeadGain{pointcast::kDefaultHeadGain};   // [gameplay] leap_head_gain: the head's tilt times this aims it (0.5 .. 4; 2 = as far with half the tilt)
    std::atomic<float> pointMaxCells{pointcast::kDefaultCapCells};   // [gameplay] point_max_cells: the circle's farthest from the hero (held to 3 .. 48)
    std::atomic<float> pointArcHeightPct{pointcast::kDefaultHeightPct};   // [gameplay] point_arc_height: the arc drawn this % of the throw's own rise (0 .. 100)
    std::atomic<float> pointArcRgb[3] = {pointcast::kDefaultRgb[0], pointcast::kDefaultRgb[1], pointcast::kDefaultRgb[2]};   // [gameplay] point_arc_color
    std::atomic<bool>  pointArcAllSet{false};      // ... it was given (not empty)
    std::atomic<bool>  pointArcBySkill{true};      // [gameplay] point_arc_by_skill: each skill its own colour (point_arc_color_<id>, else its element's)
    std::atomic<uint32_t> pointArcOwn[1024] = {};   // [gameplay] point_arc_color_<id>: 0x01RRGGBB, 0 none (the element's)
    std::atomic<float> pointArcOpacity{pointcast::kDefaultOpacityPct / 100.0f};   // [gameplay] point_arc_opacity, 0 .. 1
    std::atomic<bool>  bowAim{false};       // [hands] bow_aim: a bow's or crossbow's attack goes along the hand, no auto-target (mode 4)
    std::atomic<bool>  throwAim{false};     // [hands] throw_aim: the same for javelins and throwing weapons (off: the game's auto-target helps a throw)
    std::atomic<float> throwAutoaimDeg{15.0f};     // [hands] throw_autoaim_deg: the half-angle of the cone round a throw's line a monster snaps the point in (0 = off)
    std::atomic<float> throwPotionReachPct{throwaim::kDefaultReachPct}; // [hands] throw_potion_reach_pct: a throwing potion's hand-estimated distance scaled (50 - 1500, 800)
    std::atomic<bool>  bowLatency{false};   // [debug] bow_latency: one log line for every bow shot - where the time between the release and the arrow goes (vr/bow_latency.h)
    std::atomic<bool>  throwTrace{true};    // [debug] throw_trace: after a throw's press the log tells what the hero and the missiles do (vr/throw_trace.h)
    std::atomic<bool>  throwAutoSkill{true}; // [hands] throw_auto_skill: a throw's A press is read by the game as its Throw skill (no skill slot needed)
    std::atomic<bool>  bowDrawArrow{false};  // [bow] draw_shoots_arrow: a bow draw's release is the bow's own arrow, whatever not-bow skill the A slot holds (SlotSkillFor)
    std::atomic<bool>  throwGesture{false}; // [hands] throw_gesture: a javelin thrown by the hand's motion ends in the game's own throw along it (vr/throw_gesture_wire.cpp)
    std::atomic<bool>  bowRigPose{false};   // [bow] rig_pose: the bow's own string, limbs and arrow follow the drawing hand (skeletons.cpp, vr/bow_rig.h)
    std::atomic<bool>  bowRigGrip{false};   // [debug] bow_rig_grip: the string hand's grip squeezed draws it, with no draw detector feeding SetBowDraw
    std::atomic<bool>  flailChain{false};   // [flail] chain: the hero's flail's head swings free on its chain (skeletons.cpp, vr/flail_chain.h), F4 only
    std::atomic<float> flailDampingPct{100.0f}, flailSwingPct{100.0f}, flailHitPct{100.0f};   // [flail] damping_pct / swing_pct / hit_pct, live
    std::atomic<float> flailWeightPct{100.0f};   // [flail] ball_weight_pct, live: the ball heavier (or lighter) than the model's
    std::atomic<bool>  bowAimAlong{false};  // [bow] aim_along_hands: while the string is taken the bow, its arrow and the bow hand lie along the line from the string hand to the bow hand (vr/bow_aim.h, skeletons.cpp)
    std::atomic<float> bowAimBlendMs{120.0f};   // [bow] aim_blend_ms: the turn comes in and goes out over this long (take, release, cancel), 50..400
    std::atomic<int>   bowAimRoll{0};       // [bow] aim_roll: 0 the bow keeps the roll the controller has, 1 it is kept upright
    std::atomic<bool>  bowRestClear{true};     // [bow] arrow_rest_clear: where the arrow would pass through the bow hand, wrist or forearm it is lifted off the arm, by as little as clears it, at most 8 cm (vr/bow_aim.h ClearanceLift)
    std::atomic<float> bowRestCm[3] = {0.0f, 0.0f, 5.0f};   // [bow] arrow_rest_x_cm / y / z: the point the nocked arrow passes the bow hand at, in the hand's own frame: along the bow, across it, up (z: arrow_lift_cm's old key is its default), -10..10 each; all 0 = through the hand's reference point (vr/bow_aim.h SolveRest, skeletons.cpp)
    std::atomic<bool>  bowRestModel{false};    // [bow] arrow_rest_model: the rest point is the bow model's own arrow rest (bowrig::ArrowRestPoint), the cm offsets only a fallback
    std::atomic<bool>  bowRestAims{true};      // [bow] arrow_rest_aims: the shot's aim goes along the line through the rest point too, as the arrow lies; 0 = the shot as before (string hand through the bow hand's reference point)
    std::atomic<bool>  bowHandPose{false};  // [bow] hand_pose: the free hand of a bow is relaxed and hooks on the string while it is drawn (skeletons.cpp)
    std::atomic<bool>  bowHookGame{true};   // [bow] hook_game_pose: the string hand takes the game's own draw pose (vr/bow_hook.h) while the string is taken, not the fist table's hook
    std::atomic<float> bowHook{0.85f};      // [bow] hook_close: how far the fingers close on the string (0..1); 0.70 was too little to see
    std::atomic<float> bowHookCurl{1.0f};   // [bow] hook_curl_pct: the four fingers' bend in the game's draw pose, % of the measured one (0..150; live)
    std::atomic<float> bowHookThumbDeg{0.0f};   // [bow] hook_thumb_deg: the thumb turned further in toward the palm (+) or out (-) than the game's draw pose, degrees (-60..60; live)
    std::atomic<bool>  bowStringAnchor{true};   // [bow] string_anchor: the bow's string is drawn to the first joints of the hooked fingers, not to the grip point in the middle of the palm (vr/bow_hook.h)
    std::atomic<float> bowAnchorCm[3] = {0.0f, 0.0f, 0.8f};   // [bow] string_anchor_along_cm / across_cm / palm_cm: that point moved along the fingers, toward the thumb side, out of the palm (-15..15; live)
    std::atomic<bool>  meleeTouch{false};   // [gameplay] melee_touch: the blade touching a monster picks the target and the moment of the game's own attack
    std::atomic<bool>  meleeTouchUnchecked{false};   // [debug] melee_touch_unchecked: no hostility / wall check from the server (to see whether it is what stops a touch)
    std::atomic<bool>  meleeTouchClientVerdict{false};   // [debug] melee_touch_client_verdict: the client's units answer it even offline (what a TCP/IP client gets)
    // [autotarget] s<skill>=1: that skill keeps the game's auto-target whatever spell_aim, bow_aim, throw_aim say - one
    // that seeks its target itself (Chain Lightning, the user 2026-10-13). Bit n = skill n.
    std::atomic<uint64_t> autoKeep[16] = {};
    // [fist] <class>_close / <class>_open: how far a free hand closes, % of the full fist, with the
    // grip squeezed / let go - per hero class (skel::kHeroClasses), the rigs bend differently
    std::atomic<float> fistClose[8] = {100.0f, 100.0f, 100.0f, 65.0f, 100.0f, 100.0f, 100.0f, 100.0f};
    std::atomic<float> fistOpen[8] = {20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f};
    std::atomic<bool>  fist{true};          // [hands] fist: a free hand closes with its controller's grip, relaxes when let go
    std::atomic<bool>  shieldFist{true};    // [hands] shield_fist: the hand with a shield on it does the same (F4 only; the fingers only)
    std::atomic<bool>  forearmTwist{true};  // [hands] forearm_twist: the forearm turns with the wrist (F4 only)
    std::atomic<float> forearmShare{0.5f};  // [hands] forearm_twist_share: how much of the wrist's roll the forearm bone takes, 0..1
    std::atomic<bool>  shieldForearm{true}; // [shield] forearm: the shield strapped to the left forearm, not hung on the hand (F4 only)
    std::atomic<bool>  headsHang{true};     // [shield] heads_hang: a Necromancer's head hangs from its cord under the hand and swings (F4 only; skeletons.cpp, vr/head_hang.h)
    std::atomic<bool>  shieldBlink{false};   // [debug] shield_blink
    std::atomic<bool>  shoulderZoneBody{false};   // [hands] shoulder_zone_body: the shoulders' reach fits the body (its height) and begins at the spine
    std::atomic<float> shieldAdj[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // [shield] x y z (cm), pitch yaw roll (deg)
    std::atomic<float> staffOffset[3] = {0.0f, 0.0f, 0.0f};   // [hands] staff_x/y/z: cm, the staff past the right hand, its axes
    std::atomic<float> staffGrabCm{15.0f};  // [hands] staff_grab_cm: the left grip takes the staff only this near it
    std::atomic<float> leftStaffOffset[3] = {0.0f, 0.0f, 0.0f};
    std::atomic<float> xbowLeftOffset[3] = {0.0f, 0.0f, 0.0f};   // [hands] xbow_left_x/y/z: the same for a crossbow's left hand
    std::atomic<float> xbowLeftRoll{0.0f};
    // [hands] xbow_grip_latch / xbow_grip_wide / xbow_wrist_tracked / xbow_support_model (+ xbow_support_drop/back_adj_cm), xbow_aim_barrel and [debug] xbow_barrel_line:
    // the crossbow's left hand (vr/xbow_grip.h, skel::Input xbow*), VR F4 only; off = as before
    std::atomic<bool>  xbowGripLatch{false}, xbowGripWide{false}, xbowWristTracked{false}, xbowSupportModel{false}, xbowBarrelLine{false};
    std::atomic<float> xbowSupportDropCm{0.0f}, xbowSupportBackCm{0.0f};   // adjustments around d2r-pcvr's 7 / 6 cm
    std::atomic<bool>  xbowAimBarrel{false};   // [hands] xbow_aim_barrel
    std::atomic<bool>  xbowHoldGrip{false};    // [hands] xbow_hold_grip
    std::atomic<bool>  xbowSupportGame{false}; // [hands] xbow_support_game
    std::atomic<bool>  xbowSupportClose{false}; // [hands] xbow_support_close
    std::atomic<bool>  xbowHandover{false};     // [hands] xbow_handover
    std::atomic<bool>  xbowBarrelForward{false}; // [hands] xbow_barrel_forward
    std::atomic<float> xbowLeftAtCm{30.0f};                      // [hands] xbow_left_at_cm: where the left hand holds the crossbow, cm ahead of the right fist                       // [hands] xbow_left_roll: degrees about the stock (from below = palm up)   // [hands] left_staff_x/y/z: cm, staff_free_left's left hand on the staff, its axes
    // Two-handed weapons held like the staff (plan_two_handed_grip.md, 0.138): the right hand
    // holds, the left takes it with the grip. [hands] polearm_two_hands: spears and polearms,
    // the left slides along the shaft; sword_two_hands: two-handed swords held in both hands,
    // the left on the hilt sword_left_at_cm from the right fist, sliding sword_slide_cm either
    // way. spear_hand / sword_hand: the wrist the game hangs it on (0 right, 1 left), used only
    // when its attach bones do not tell (skel::GripHand).
    std::atomic<bool>  polearmTwoHands{true}, swordTwoHands{true};
    std::atomic<float> swordLeftAtCm{10.0f}, swordSlideCm{4.0f};
    std::atomic<float> leftSwordOffset[3] = {0.0f, 0.0f, 0.0f};   // [hands] left_sword_x/y/z: the left fist on the hilt, its axes
    std::atomic<int>   spearHand{0}, swordHand{0}, axeHand{0};   // axe_hand: a two-handed axe or mace - the right wrist (seen 2026-10-06)
    std::atomic<int>   swordHiltAxis{-2};
    // [hands] axe_shaft_axis / spear_shaft_axis: the haft as the bone's axis, from the right fist toward
    // where the left slides; 0 = from the game's grip. A great axe: -2, its Y runs to the head (2026-10-06).
    // One rule (2026-10-06): a weapon's length is its attach bone's Y, +Y to the blade, head or tip - the
    // sword, the great axe and the crossbow (xbow_stock_axis +2) all showed it; the palms in the game's grip
    // ran across the weapon whenever its left hand was off it.
    std::atomic<int>   axeShaftAxis{-2}, spearShaftAxis{-2};
    // [hands] staff_hand / staff_shaft_axis: a staff's wrist and its shaft as the bone's axis (every hero)
    std::atomic<int>   staffHand{0}, staffShaftAxis{-2};
    std::atomic<int>   carryAxis{2};
    std::atomic<float> carryCm{40.0f};       // [hands] carry_cm: how far along it
    // [weight] (vr/weight.cpp, plan_weapon_weight.md step 1b): a melee weapon held alone has weight - it lags
    // behind a hard swing, a two-handed one wants both hands - and a hand takes it where its grip closes on
    // the shaft, passing it from hand to hand. Off: the weapon sits fast in the game's grip, as before.
    std::atomic<bool>  weightOn{false};      // [weight] on
    std::atomic<float> weightFeel{50.0f};   // [weight] feel: % of the weapon's mass
    std::atomic<float> weightStrength{100.0f};   // [weight] strength: % of the hands' strength
    std::atomic<float> weightGravity{100.0f};    // [weight] gravity_pct: % of g (0 = no gravity, only inertia)
    std::atomic<float> weightDamping{100.0f};    // [weight] damping_pct: % of the muscles' dampers
    std::atomic<bool>  weightGravityFeel{true};  // [weight] gravity_feel: the wrists soft enough for the weight to sag them; off = the old stiff wrists
    std::atomic<int>   weightHandsModel{0};      // [weight] hands_model: 0 our own physics, 1 the weight-physics library (when it is beside the plugin; else ours)
    std::atomic<bool>  weightHandsPalm{false};   // [weight] hands_model_palm: the library's weight in the palm
    std::atomic<int>   weightGripModel{0};       // [weight] grip_model: 0 our own way of holding, 1 the grip library (when it is beside the plugin; else ours)
    std::atomic<bool>  weightHeroStrength{false};  // [weight] hero_strength: the hero's Strength against the weapon's requirement scales the steel and the hands (vr/hero_strength.h)
    std::atomic<float> weightStrengthEffect{100.0f};   // [weight] strength_effect: how much it matters, %
    std::atomic<float> weightStrengthClass[8] = {};    // [weight] str_<class>: the class' multiplier, % (skel::kHeroClasses order)
    std::atomic<bool>  weightStrengthHonest{false};    // [weight] strength_honest: the ratio is Str / requirement as it is (no +50; a weapon asking none counts as 10)
    std::atomic<bool>  weightTwoHandsDouble{false};    // [weight] two_hands_double: both hands on the weapon - each hand's strength x2
    std::atomic<float> weightGrabCm{12.0f};  // [weight] grab_cm: how near the shaft a grip takes it
    std::atomic<float> weightLetGoCm{25.0f}; // [weight] let_go_cm: hands pulled this far apart off their places let go
    std::atomic<float> weightFist[3] = {0.0f, 0.0f, 0.0f};   // [weight] fist_x/y/z: cm, the closed fist's middle from the right controller, its axes
    // [weight] grip_fit (vr/grip_fit.h): a hand that took the weapon is laid on its shaft (palm's middle on the axis, knuckles along it, closing with the squeeze), by default
    // sitting on it as the game's own grip has the base hand (grip_fit_like_base); and EACH HAND has live corrections in the shaft's own frame: right_grip_* / left_grip_* -
    // x y z in cm (along the shaft, across toward the fingertips, up out of the palm), rx ry rz in degrees (the roll about the shaft, the tilt about across, the tilt about up).
    std::atomic<bool>  weightGripFit{true};
    std::atomic<bool>  weightGripLikeBase{true};
    std::atomic<bool>  weightGripFingers{true};   // [weight] grip_fit_fingers: the taking hand's fingers and thumb take the game's own weapon-grip pose (vr/grip_pose.h)
    std::atomic<bool>  weightAttachApart{true};   // [weight] attach_apart: the weapon's two attach bones kept carry_cm apart along its axis (in one point the game draws it flat)
    std::atomic<float> weightGripHand[2][6] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    // [weight] after_drop: what becomes of a weapon let go that has landed - 0 it lies on the floor, 1 after the delay it goes to the hero's back
    // (vr/weapon_back.h), 2 after the delay it is not drawn at all. The back place per class (long: staffs, spears, polearms, javelins, anything
    // two-handed; short: the rest), x y z in cm and pitch yaw roll in deg on top of the default.
    std::atomic<int>   weightAfterDrop{1};
    std::atomic<float> weightToBackMs{600.0f};
    std::atomic<bool>  weightTakeHaptic{true};      // [weight] take_haptic: a pulse in a hand entering the take zone behind a shoulder while a weapon is away
    std::atomic<float> weightTakeHapticPct{40.0f};  // [weight] take_haptic_pct: its strength
    std::atomic<bool>  shieldDraw{true};            // [hands] shoulder_draw_shield: with shoulder_draw, the shield too (it follows the sword to the forearm opposite the hand that holds it)
    std::atomic<bool>  rangedHolster{false};        // [hands] shoulder_draw_ranged: with shoulder_draw, a bow or crossbow can be put away behind a shoulder and drawn from it
    std::atomic<bool>  rangedSwap{false};           // [hands] shoulder_draw_ranged_swap: a bow is drawn into the hand that reached (a crossbow always back into its own)
    std::atomic<bool>  shieldStow{false};           // [hands] shoulder_draw_shield_stow: with shoulder_draw, a grip behind the LEFT shoulder puts the shield away and draws it (the weapon is drawn from the right shoulder only then)
    std::atomic<float> shoulderDrawStowMs{1000.0f}; // [hands] shoulder_draw_stow_ms: how long a weapon let go lies on the floor (it is recalled by a grip meanwhile) before it goes to the back
    std::atomic<bool>  shoulderDraw{false};         // [hands] shoulder_draw: the zone behind each shoulder is a holster (vr/shoulder_draw.h); the weight model draws the weapon from it, the swap is off
    std::atomic<bool>  weightRecallAny{true};       // [weight] recall_any_hand: the grip of any free hand, anywhere, calls a weapon that is away
    std::atomic<bool>  weightMonsterBodies{false};  // [weight] monster_bodies: the held weapon stops on the hostile monsters' bodies or glances off them (vr/monster_bodies.h)
    std::atomic<bool>  weightStabIn{false};         // [weight] stab_in: with monster_bodies, a thrusting weapon driven point first into a body goes in (cleanroom/heft/stab.h)
    // ... its sliders (live; heft::stab::FromSliders clamps them)
    std::atomic<float> stabMinSpeed{0.5f}, stabMaxAngleDeg{60.0f}, stabResistPct{30.0f}, stabDepthCm{0.0f}, stabTearCm{45.0f}, stabUnlockN{150.0f};
    std::atomic<bool>  stickByStrength{false};   // [weight] stick_by_strength: how hard a weapon sticks by the strength table's class multiplier (vr/stick_strength.h)
    std::atomic<float> stickPct{100.0f};         // [weight] stick_pct: ... and a general factor, %
    std::atomic<bool>  stabDebug{false};            // [debug] stab_debug: the stab's readout over the weapon hand and its marks with the melee boxes (vr/stab_debug.h)
    std::atomic<bool>  stabWithRun{false};          // [gameplay] melee_thrust_with_run: the hero's run counts for the stab too (its speed against the body)
    std::atomic<bool>  strikeMeasure{false};        // [debug] strike_measure: each strike on a body measured into the log (vr/strike_measure.h)
    std::atomic<bool>  strikeByForce{false};        // [weight] strike_by_force: the weight model's strikes judged by their force (vr/strike_force.h; with monster_bodies)
    std::atomic<bool>  weightMonsterRagdoll{false}; // [weight] monster_ragdoll (probe): ... and their limbs give way to it by their weight (the doll library, vr/doll_lib.h)
    std::atomic<bool>  npcDialogNoBar{true};        // [hud] npc_dialog_no_bar: an NPC's dialogue box's panel without the toolbar (0 = with it, as before)
    std::atomic<bool>  weightGrabBodies{false};     // [weight] grab_bodies: an empty hand's grip holds a monster's or an NPC's body (vr/grab_bodies.h, grab_wire.cpp)
    std::atomic<float> weightGrabReachCm{8.0f};     // [weight] grab_reach_cm: the palm this near a body's surface takes it
    std::atomic<float> weightGrabBreakCm{80.0f};    // [weight] grab_break_cm: the controller this far off the held hand lets go
    std::atomic<bool>  weightGrabArmReach{true};    // [weight] grab_arm_reach: a held point past the drawn arm's reach lets go (grabbodies::PastArm)
    std::atomic<bool>  orbDrawnHand{true};          // [hud] hand_vitals_orb_drawn_hand: the wrist medallions on the hand as drawn at last (skel::HeroArmWorld read at the pose's end)
    std::atomic<float> weightGrabMusclePct{100.0f}; // [weight] grab_muscle_pct: a held monster's joints' muscles, % of what its size gives
    std::atomic<bool>  weightGrabProps{false};      // [weight] grab_props: an empty hand's grip takes the level's small props (knock_props' world: vr/knock_props.h, grab_wire.cpp)
    std::atomic<bool>  weightGrabWrap{true};        // [weight] grab_wrap: a holding hand laid on the body, its fingers closed round it (the grip library)
    std::atomic<bool>  weightGrabItems{false};      // [weight] grab_items: an empty hand's grip on an item on the ground picks it up through the game's own pick-up (vr/item_grab.h)
    std::atomic<float> weightGrabItemsReachCm{25.0f};   // [weight] grab_items_reach_cm: the palm this near an item's place on the ground
    std::atomic<float> weightThrownFlyMs{3000.0f};  // [weight] thrown_fly_ms: a thrown weapon's model flies at most this long after the release
    std::atomic<float> weightThrowReachPct{800.0f}; // [weight] throw_reach_pct: the horizontal release velocity x this (a real arm throws a few metres)
    std::atomic<float> weightThrowMaxCells{48.0f};  // [weight] throw_max_cells: the landing is held to this many cells from the hero (the game refuses > 50 on an axis)
    std::atomic<float> weightBackTakeCm{25.0f};     // [weight] back_take_cm: a fist this near the weapon stowed on the back takes it with the grip
    std::atomic<int>   weightThrownModel{0};        // [weight] thrown_model: 0 the model vanishes the moment the game takes the throw, 1 it flies on physically
    std::atomic<float> weightThrownHandoffMs{0.0f};   // [weight] thrown_handoff_ms: the thrown model flies out of the hand this long (or to the game's action frame) before it hands over to the game's missile and vanishes (0: at once)
    std::atomic<float> weightThrownHandEmptyMs{900.0f};   // [weight] thrown_hand_empty_ms: the hand stays empty (the weapon not drawn) while the game throws, at most this long
    std::atomic<bool>  grabNearest{true};           // [belt] grab_nearest: a grip that could take a belt potion or the weapon goes to the NEARER one (the swap's volume: the swap)
    std::atomic<bool>  grabTrigger{false};          // [belt] grab_trigger: a belt potion needs the trigger squeezed with the grip
    std::atomic<bool>  holdTakesTrigger{true};      // [belt] hold_takes_trigger: the triggers are the belt's while a bottle is in a hand (the left one is the game's Shift: with it no drink)
    std::atomic<bool>  quickThrowPotions{false};    // [hands] quick_throw_potions: quick_throw cuts a throwing potion's animation too (default: a potion keeps its own timing)
    std::atomic<bool>  weightPotionFlies{true};     // [weight] potion_model_flies (or [hands] potion_throw_ours): a thrown potion's model flies to its landing, the game's take does not hand it over
    std::atomic<bool>  weightSlide{true};           // [weight] slide_second_hand: the second hand slides along a long shaft while its grip stays squeezed
    std::atomic<bool>  weightHiltOnly{false};       // [weight] grip_hilt_only: the second hand takes a sword (an axe, a mace ...) only on its hilt
    std::atomic<float> weightHiltCm[gripzone::kCount] = {};   // [weight] grip_hilt_cm_<kind>: the hilt's length, cm; 0 = the weight model's own
    std::atomic<uint32_t> weightSlideKinds{slidekinds::DefaultMask()};   // [weight] slide_second_hand_<kind>: ... on these kinds of weapon (a bit each, vr/slide_kinds.h): a staff's, a spear's, a polearm's
    std::atomic<float> weightBackLong[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::atomic<float> weightBackShort[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::atomic<bool>  bothAttach{true};     // [hands] both_attach: 0 = the other attach bone left alone (for comparing)   // [hands] carry_axis: moving, the game lays a 2H weapon from the right attach bone to the left - the left one put along this axis   // [hands] sword_hilt_axis: the hilt as the sword bone's axis, +-1..3 (x y z); 0 = from the game's grip
    float weaponAdj[D2RVR_TYPE_COUNT][6] = {};
    float weaponLeft[D2RVR_TYPE_COUNT][3] = {};   // (no longer used: one for every two-handed weapon, leftHold)
    std::atomic<float> leftHold[3] = {0.0f, 0.0f, 0.0f};
    std::atomic<float> leftHoldTurn[3] = {0.0f, 0.0f, 0.0f};   // [hands] left_hold_pitch/yaw/roll: deg, the same hand turned about its own axes   // [hands] left_hold_x/y/z: cm, the left hand once it holds any two-handed weapon (not the crossbow), its own axes   // [weapon_<kind>] left_x/y/z: cm, the left hand once it holds the weapon, its own axes (on top of left_staff_* etc.)   // [weapon_<kind>] x y z (cm) pitch yaw roll (deg); written and read on the update thread
    std::atomic<bool>  lockUpper{true};     // nothing above the pelvis from the game's animation
    std::atomic<bool>  legsUnder{true};     // with it: pelvis and legs kept under that body (no lunge, no hip turn)
    std::atomic<bool>  flatHoldTrunk{false};   // [body] flat_hold_trunk: flat F3, the trunk and hips held under the camera through a swing (drawn only)
    std::atomic<bool>  underCamera{true};   // [body] under_camera: the drawn body under the camera through a leap's arc and the game camera's trail (F4; vr/under_camera.h)
    std::atomic<bool>  leapCameraUp{false}; // [body] leap_camera_up: with it, the camera rises with the leap's arc (off: it stays, the body is kept down with it)
    std::atomic<bool>  crouch{false};       // [body] crouch: the hero crouches when you crouch (F4 only; vr/crouch.h)
    std::atomic<float> crouchCm{20.0f};     // [body] crouch_cm: the head this far below its height at the recentre is a crouch
    std::atomic<float> bodyYawDeg{0.0f};    // added to the hero's facing: fixes a body that stands sideways
    std::atomic<bool>  thirdTurn{true};     // [body] turn_third: from behind too the whole hero faces where the camera looks (the right stick turns him)
    std::atomic<bool>  gaitThird{false};    // [body] gait_legs_third: from behind, his legs step along his travel (the gait library) - back steps, side steps
    std::atomic<bool>  gaitF4{false};       // [body] gait_legs_f4: the same on our own F4 body (the whole body turned with the look)
    std::atomic<bool>  walkBack{true};      // [body] walk_back: turned with the look and walking the other way, the game's walk mirrored - the legs step back
    std::atomic<bool>  walkBackReverse{true};   // [body] walk_back_reverse: ... the game's walk played backwards instead of mirrored (0 = the mirror, as 0.185)
    std::atomic<int>   bodyTurn{2};         // 0 game facing, 1 above the pelvis, 2 whole body turns to where the camera looks
    std::atomic<bool>  wrist{true};         // arms mode 2: the hands turn with the controllers
    std::atomic<float> wristPitch{0.0f}, wristYaw{0.0f}, wristRoll{0.0f};   // degrees on top, controller axes
    std::atomic<float> convergence{15.0f};  // distance (world units) that sits ON the screen; 0 = parallel eyes
    std::atomic<float> thirdDistance{7.0f}; // third person: camera this far behind the eye point
    std::atomic<float> tableFloor{0.05f};   // [table] floor: the table view lifts the game this far off black (the headset keys the black)
    std::atomic<float> tableScale{15.0f};   // world units per metre of the room, from [table] hero_cm: the game laid on the floor at that size
    std::atomic<bool>  topPersp{false};     // [top] perspective: VR F1 through our camera, the game's view in true perspective
    std::atomic<float> topTilt{0.0f};       // [top] tilt: that camera turned about the hero, degrees; + nearer the horizon
    std::atomic<float> tableHeightM{0.0f};  // [table] height_m: the game's ground this far above the room's floor (0 = on the floor, ~0.75 = a table)
    std::atomic<float> tableAheadM{1.0f};
    std::atomic<float> tableBounds{1.0f};
    std::atomic<int>   tablePlace{0};
    std::atomic<float> tableTurnDeg{0.0f};  // [table] turn: the world turned about the hero on the board, degrees (45: its grid square to the board)       // [table] place: the settings program's "put the game in front of me" button, one more per press   // [table] bounds: the diorama is the ground the game's own view shows, times this; 0 = no edge   // [table] ahead_m: the hero this far ahead of where the head is when the view is taken (F5, F11)
    std::atomic<float> thirdHeight{6.5f};   // third person: the eye point above the ground
    std::atomic<bool>  stampPixels{true};
    std::atomic<bool>  stamps{true};
    std::atomic<bool>  pace{true};        // [stereo] pace: game frames held to the headset's rate (see pace::Tick)
    std::atomic<bool>  vcache{false};     // [render] vcache: the game's threads on the big-L3 cores (vr/vcache.h)
    std::atomic<bool>  paceFlatVrOnly{true};   // [stereo] pace_flatvr_only: that only while FlatVR runs (nothing to pace to otherwise)
    std::atomic<int>   headsetHz{90};     // [stereo] headset_rate (0..4: 72 75 80 90 120)
    std::atomic<bool>  pictureRing{false};  // [stereo] picture_ring (off by default since 0.152: it juddered in head turns): FlatVR's addon hands over the newest finished picture (2.19: ReShade.ini [FLATVR] ColourRing)        // [stereo] stamps: each frame's head-pose moment to FlatVR (0: none at all, the screen at the head as it is)
    std::atomic<bool>  turnFxFence{true};   // [stereo] turn_fx_fence: eyes by turns - each present's effects wait for the last one's renders (vr/sky_view_eyes.h FenceEffects)
    std::atomic<int>   pipelineDepth{0};    // the presented frame is this many of its eye's views older than the newest (D3D12 queueing)   // the frame stamp strip for FlatVR (bottom-right corner)
    std::atomic<bool>  topStereo{false};    // the game's own view from above (F12 off) in stereo too: each eye turned about the hero
    std::atomic<float> topAngle{3.0f};      // that turn between the eyes, degrees
    std::atomic<bool>  trueScale{true};     // eyes and zero-parallax plane from the FlatVR screen and the user's height: the world 1:1
    std::atomic<float> eyeMm{63.0f};        // the user's own eye distance, millimetres
    std::atomic<float> eyeHeightM{1.64f};   // the user's eyes above the floor STANDING: metres -> world units, seated or not
    std::atomic<bool>  dlssPerEye{true};   // [render] dlss_per_eye: real stereo with DLSS - a DLSS instance of its own for each eye
    std::atomic<bool>  pairPerTick{false};  // AFR: both eyes drawn from one game frame (the frame drawn twice), not by turns
    std::atomic<float> rightDtMs{0.01f};    // [stereo] right_dt_ms: the frame time the right pass of a pair gets (0 = none)
    std::atomic<bool>  pairFxSync{true};   // [stereo] pair_fx_sync: in a pair, an eye's effects wait for the same eye's last on the GPU (vr/sky_view_eyes.h FxRing)
    std::atomic<bool>  slowPairWorld{true};   // [stereo] slow_pair_world: native's pair is the world's when the camera's follow ran since the last pair began (vr/pair_world.h)
    std::atomic<bool>  bgFullSpeed{true};   // no Sleep(10) per frame while the game window is not in front
    std::atomic<bool>  gameHeightFog{false};   // [render] game_height_fog: keep the game's own height fog (gamefog::)
    std::atomic<bool>  gameDepthFog{false};    // [render] game_depth_fog: keep the game's own depth fog (gamefog::)
    std::atomic<bool>  gameFog{false};         // [fog] game_fog: the game's own distance fog drawn with our numbers (gamefogvr::)
    std::atomic<bool>  fogHeight{false};       // [fog] game_fog_height: the game's fog thinned with height in its own pass (vr/fog_height.h)
    std::atomic<float> fogTop{60.0f};          // [fog] game_fog_top: no fog above this, world units above the eye
    std::atomic<bool>  fogTopFromFloor{false}; // [fog] game_fog_top_from_floor: game_fog_top counted from the hero's floor (gamefogvr::TopAboveEye)
    std::atomic<float> fogSoft{50.0f};         // [fog] game_fog_soft: how far below the top the fog fades in
    std::atomic<bool>  fogMist{true};          // [fog] game_fog_mist: the game's volumetric mist fogged by distance too (no light band in caves)
    std::atomic<bool>  fogCavesBlack{true};    // [fog] game_fog_caves_black: underground the game's fog black, the void left alone
    std::atomic<bool>  fogBeforeFx{false};     // [fog] game_fog_before_fx: the fog laid on the scene before the transparent resolve (fogfx::)
    std::atomic<bool>  fogOurCurve{false};     // [fog] game_fog_our_curve: the game's fog by our effect's curve, not its exponential
    std::atomic<bool>  fogKeepsColour{false};  // [fog] fog_keeps_colour: the act's fog colour at every hour, not darkened with the game's light
    std::atomic<bool>  fogIntoSky{false};      // [fog] fog_into_sky: the fog's colour the sky straight behind each point (D2R_DepthFog.fx SkyBehind)
    std::atomic<bool>  fogMatch{false};        // [fog] game_fog_match: the game's fog shown in the act's colour, its grading measured (D2R_DepthFog.fx GameFogTrue)
    std::atomic<bool>  gameDrawnFlat{false};   // [render] game_drawn_flat: the game-drawn sky dome, ceiling and fog in the flat game too (GameDrawnView)
    std::atomic<int>   solidWalls{1};          // [render] solid_walls: 0 the game's see-through walls, 1 solid but in F1, 2 solid in every view
    std::atomic<bool>  keepRoofs{false};       // [render] keep_roofs: F4 - the game's roof cut-away (fade to 0) answered with 1 (vr/roof_keep.h)
    std::atomic<bool>  roofsTwoSided{false};   // [render] roofs_two_sided: VR - roof materials built double-sided at an area's load (vr/roof_sides.h)
    std::atomic<bool>  propsOncePair{false};   // [stereo] props_once_a_pair: F4 - partial-ragdoll props stepped in the left pass only (vr/prop_hold.h)
    std::atomic<float> uiShift{0.0f};       // AFR: the HUD moved apart per eye in the shader, % of the half screen; more = nearer
    std::atomic<float> hudTop{0.70f};       // the HUD is looked for below this height (0 top .. 1 bottom)
};
extern Settings g_set;

// vr/config/load_settings.cpp: d2r_vr.ini into g_set and to every feature; again when the file (or d2r_vr_settings.ini's language) changed.
void LoadSettings();
bool ReloadIfChanged();


// vr/config/reconcile.cpp: the game's own Settings.json as VR and flat mode want it
void MiniMapForHand();
void NoTemporalAA();
void ReconcileFrameCap();
void ReconcileGameWindow();
bool ReconcileSafeScreen();
void SafeGameWindow();

}  // namespace vrc
