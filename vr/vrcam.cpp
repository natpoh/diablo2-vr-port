// vrcam - Diablo II: Resurrected in a headset, or as a first-person game.
//
// The camera itself is cleanroom/camera (d2rcam): it hands this layer the game's
// own view and the hero point once a game frame and puts back the view and
// projection made here. Render distance is cleanroom/drawdist. This file is the
// VR layer on top:
//   - the view turns with the head, read from BodyWalk's D2R Bridge (shared
//     memory, see shared/d2r_vr_shared.h), or with the mouse;
//   - alternate-frame stereo for FlatVR, fog and sky through ReShade;
//   - the hero's body turns to the view, his head hides, his arms follow the
//     controllers (skeletons.cpp);
//   - the left stick the game reads through XInput is turned by the view's yaw,
//     so "forward" is forward in the view - BodyWalk walking, a VR stick, a pad
//     or W A S D;
//   - every number lives in d2r_vr.ini next to this DLL, re-read when it changes.
//
// Keys: F12 off / first person / third person, F11 recenter, F9 mouse look and
// W A S D on / off, F10 skeleton dump. Middle mouse drag turns the view too.

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
#include "game_device_only.h"

namespace probe {
void Run(const float hero[3], const wchar_t* outPath);
void FindHero(const float hero[3], const wchar_t* outPath, const char* tag);
int FindHeroMatrices(const float hero[3], uintptr_t* out, int max);
}
#include "skeletons.h"
#include "writewatch.h"
namespace uitrace { void Register(); bool Arm(const std::wstring& path); void SetSkip(uint64_t pipe); void SetSkipShaders(const uint64_t* hashes, int n); }
#include "game_hud_shared.h"
namespace memdiff { void Step(uintptr_t base, const std::wstring& outPath, void (*log)(const char*)); void Reset(void (*log)(const char*)); }
namespace hud { void Register(); void SetHide(int mode); void SetInterfaceScale(float s); void SetMenuOpen(bool open); void SetMapMode(int mode); bool LayerFound(); void SetLogger(void (*log)(const char*)); void SetLook(const FlatVRGameHudLook& look); void SetMapCorner(int corner); bool ToggleMapShown();
                void SetPointer(uint32_t mode); void SetPointerDepth(float base, float tilt, float curve, float topScale); int Anchor(int panel); void SetForearm(int panel, bool valid, const float elbow[3], const float wrist[3], const float across[3]);
                void SetClassicView(bool on); void SetPictureZoom(float bar, float map); bool PictureWanted(); void SetPictureReady(bool ready);
                bool PictureNow(int i, float box[4], uint64_t* srv); void SetPictureBarMoved(bool moved); void SetPictureBarOffset(float x, float y); void SetPictureMapMoved(bool moved); void SetPictureMapOffset(float x, float y);
                void SetLabels(bool on); bool LabelsNow(float keepBar[4], float keepMap[4], uint64_t* srv); void SetUiMask(bool on); bool UiMaskNow(); }
#include "d2rcam.h"
#include "sigscan.h"
#include "mat4.h"
namespace gamecmd { void Init(const D2RL::PluginContext* ctx); void Tick(); }   // the game's key commands from BodyWalk (gamecmd.cpp)
namespace gamestate { void Init(const D2RL::PluginContext* ctx); void Tick(); uint32_t WeaponClass(); uint32_t WeaponSet(); uint32_t WeaponType(); uint32_t HandsHeld(); uint32_t TwoHanded(); uint32_t WeaponHand(); uint32_t HandsKey(); bool MenuOpen(); int ObjectLight(uint32_t txt, uint32_t mode, float rgb[3]); uint64_t LocalPlayer(); void SetViewMode(uint32_t mode); bool AutoMapOpen(); bool SetAutoMap(bool open); void PollPlate(); bool PlateRect(int32_t out[4]); }
#include "d2r_vr_state.h"

#include "MinHook.h"
#include "crosshair_cursor.h"
#include "d2r_vr_shared.h"
#include "afr_eye_shared.h"
#include "dlss_mv.h"
#include "xr.h"
#include <timeapi.h>   // timeBeginPeriod: the pacer's 1 ms sleeps
#pragma comment(lib, "winmm.lib")
#include "pad_mirror_shared.h"

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace {
namespace gamefog { void SetPoke(const std::wstring& want); }   // [debug] fog_poke, below

const PluginContext* g_ctx = nullptr;
uintptr_t g_base = 0;
HMODULE g_self = nullptr;


#pragma optimize("", off)
bool SafeRead(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool SafeWriteMem(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
#pragma optimize("", on)

// The game's bytes at rva are the ones expected - at the address this build has
// for what 3.3.93787 has at rva (cleanroom/sigscan).
bool Matches(uint64_t rva, const uint8_t* sig, size_t n) {
    return g_ctx != nullptr && d2rsig::Check(rva, sig, (uint32_t)n);
}


// Crash reports (namespace crash, after the replay): the start's steps and the log's first minutes
// to a file of our own as they come, and an exception the game dies of to d2r_vr_crash.txt.
namespace crash { void Install(const char* version); void Uninstall(); void Keep(); void Mirror(const char* text); void Step(const char* name); void Mark(const char* what); }

void Log(const char* text) {
    if (g_ctx != nullptr) g_ctx->LogInfo(text);
    crash::Mirror(text);
}
void LogF(const char* fmt, ...);
// To the game's console as well as the log.
void Say(const char* text) {
    if (g_ctx == nullptr) return;
    g_ctx->LogInfo(text);
    crash::Mirror(text);
    g_ctx->WriteConsoleMessage(text, ConsoleMessageKind::Output);
}

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
    std::atomic<int>   rings{4};            // [render] rings: room rings drawn around the hero (cleanroom/drawdist)
    std::atomic<float> modelRadius{1000.0f}; // [render] model_radius: houses, props, units drawn out to this (game: 150)
    std::atomic<int>   hudMap{0};           // [hud] map: 0 in the picture, 1 out of it, into its own texture
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
    // The same two for the other views (2026-10-06): [hud_top] F1, [hud_third] F2, [hud] F4 - the game's own by default.
    std::atomic<float> labelsAlphaTop{1.0f}, labelsAlphaThird{1.0f}, labelsAlphaBody{1.0f};
    std::atomic<float> plateAlpha{1.0f};   // [hud] monster_alpha: the name plate over the target monster, 1 = as the game draws it
    std::atomic<float> labelsSizeTop{1.0f}, labelsSizeThird{1.0f}, labelsSizeBody{1.0f};
    // bar_x / bar_y per view: the toolbar moved on the screen, uv (+x right, +y DOWN; the ini's bar_y is + up, %)
    std::atomic<float> barXTop{0.0f}, barYTop{0.0f}, barXThird{0.0f}, barYThird{0.0f}, barXFloor{0.0f}, barYFloor{0.0f};
    // [hud_floor] map_size / map_near / map_x / map_y: the corner map on the floor (F3), as its toolbar
    std::atomic<float> mapSizeFloor{1.0f}, mapNearFloor{0.0f}, mapXFloor{0.0f}, mapYFloor{0.0f};
    std::atomic<float> barNear{0.0f};       // [hud] bar_near: from above and behind, the toolbar nearer (+) or farther (-), % of the half screen per eye
    std::atomic<float> labelsNear{0.0f};     // [hud] labels_near: from above in stereo, labels nearer (+) / farther (-), % of the half screen per eye
    std::atomic<float> labelsTilt{0.0f};
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
    std::atomic<bool>  headYaw{true};       // yaw from the headset via D2R Bridge
    std::atomic<float> yawSign{1.0f};       // flip if turning the head left turns the view right
    std::atomic<float> yawScale{1.0f};
    std::atomic<bool>  headPredict{false};  // carry the head on between BodyWalk's updates (off: FlatVR places the screen by the sample's own stamp)
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
    std::atomic<float> snapAngle{0.0f};     // >0: snap turn by this many degrees per flick instead
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
    std::atomic<bool>  skyOn{false};        // sky in the void, same shader as the fog
    std::atomic<float> skyClouds{1.0f};     // times the biome's own cloud cover
    std::atomic<bool>  skyDrift{false};     // [sky] drift: the painted sky turns slowly (off: it stands still - painted stars went round with the clouds)
    std::atomic<bool>  skyDayNight{true};   // [sky] day_night: the sky and the open-air fog follow the game's own light
    std::atomic<float> lightDay{128.0f};    // [sky] light_day / light_night: the game's light (0..255) that counts as full day / full night
    std::atomic<float> lightNight{65.0f};
    std::atomic<float> nightBright{0.25f};  // [sky] night_brightness: the sky and the fog at full night, times their day brightness
    std::atomic<bool>  skyAlways{false};    // whatever the biome (testing): caves get a sky too
    std::atomic<bool>  skyHidePanels{true}; // no sky while a side panel shifts the picture
    std::atomic<bool>  fogCaves{true};      // [fog] caves: the fog in caves and dungeons too (every area off [sky] outdoor)
    std::atomic<bool>  ceilOn{false};       // [ceiling] enabled: a vault over the void in caves (first person only)
    std::atomic<float> ceilHeight{30.0f};   // [ceiling] height: over the hero's floor, world units
    std::atomic<float> ceilScale{10.0f};    // [ceiling] scale: world units per tile (the test grid's cell)
    std::atomic<float> ceilBright{1.0f};    // [ceiling] brightness
    std::atomic<float> ceilRelief{6.0f};    // [ceiling] relief: how far the rock hangs down, world units (0 = flat)
    std::atomic<int>   ceilSteps{12};       // [ceiling] steps: the ray's steps through the rock (the frame's cost)
    std::atomic<float> ceilLight{25.0f};    // [ceiling] light_radius: world units from the hero where his light is down to half
    std::atomic<bool>  ceilTorches{false};  // [ceiling] torches: fire seen in the picture lights the ceiling
    std::atomic<float> ceilTorchBright{0.6f};   // [ceiling] torch_brightness
    std::atomic<float> ceilTorchRadius{20.0f};  // [ceiling] torch_radius: world units where a torch's light is down to half
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
    std::atomic<bool>  bodywalkPad{true};   // [input] bodywalk_pad: the game's pad is BodyWalk's own report (pad_mirror_shared.h) - no virtual pad needed
    std::atomic<int>   inventoryPad{0};     // [input] inventory_button: the pad button "D2R: Inventory" sends - 0 Menu (Start), 1 View (Back)
    std::atomic<bool>  aAttackOnly{false};  // [input] a_attack_only: pad A never picks up or interacts; "D2R: Pick up / interact" does
    std::atomic<bool>  flatKeyMove{true};   // [input] flat_keyboard_move: flat W A S D walk through the game's own keyboard move (0x8A960), no pad
    std::atomic<bool>  vrKeyWalk{true};     // [input] vr_keys_walk: VR view F2 without mouse look - W A S D walk where the camera looks, the mouse stays the game's
    std::atomic<bool>  directWalk{true};    // [input] direct_walk: VR F2-F4, BodyWalk's stick walks the hero straight (StickGet / KeyMove hooks), not through the pad
    std::atomic<bool>  flatNoPad{true};     // [input] flat_no_pad: with it, flat mode shows the game no pad at all (its UI never turns to A/B/X/Y)
    std::atomic<bool>  flatCrosshair{true};  // [input] flat_crosshair: flat mouse look, the pointer is our crosshair, not the game's gauntlet
    std::atomic<bool>  flatClickShoot{true}; // [input] flat_click_shoot: flat mouse look, a click with no target under the crosshair = a shot there, never a walk
    std::atomic<bool>  findPrevMatrix{false};
    std::atomic<bool>  dlssPrevFix{false};
    std::atomic<int>   dlssMv{2};              // [render] dlss_mv: 0 off, 1 check vrcam's matrices against the game's motion vectors (logs), 2 hand DLSS the same eye's     // [render] dlss_prev_fix: the game's previous-frame view made the same eye's (experiment)   // [debug] find_prev_matrix: once, where the game keeps past frames' view matrices (DLSS motion vectors)
    std::atomic<bool>  frameLog{false};     // [debug] frame_log: every pair, view, hero pose and present to d2r_vr_frames.csv
    std::atomic<bool>  poseOrderLog{false}; // [debug] pose_order: which of eye / look-at / hero matrix is stale per pass, to the log
    std::atomic<float> facingSign{1.0f};    // flip if the arms swing the wrong way when the model turns
    std::atomic<int>   facingSource{1};     // [hands] facing_source: 1 the hero's TransformComponent (structural), 0 the memory search
    std::atomic<float> facingHoldMs{4000.0f};   // [hands] facing_hold_ms: the last facing kept this long while none reads
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
    std::atomic<float> aimRange{20.0f};     // mode 4 with a bow: the attack point this many grid cells along the hand       // mode 3: how long the stick turns the hero before A goes through
    std::atomic<int>   bowHand{1};          // 0 right, 1 left: the aiming hand of weapon set I
    std::atomic<int>   bowHand2{0};         // the same for weapon set II
    std::atomic<bool>  rangedLeft{true};    // a bow or crossbow is always aimed with the left hand (it sits in no right hand)
    std::atomic<bool>  staffTwoHands{true}; // [bow] staff_two_hands: with a staff, aim from the left hand to the right one
    std::atomic<bool>  staffHands{true};    // [hands] follow_staff: with a staff, the wrists turn with it, not with the controllers
    std::atomic<bool>  staffFreeLeft{true}; // [hands] staff_free_left: the right hand holds the staff, the left takes it with the grip
    // [fist] <class>_close / <class>_open: how far a free hand closes, % of the full fist, with the
    // grip squeezed / let go - per hero class (skel::kHeroClasses), the rigs bend differently
    std::atomic<float> fistClose[8] = {100.0f, 100.0f, 100.0f, 65.0f, 100.0f, 100.0f, 100.0f, 100.0f};
    std::atomic<float> fistOpen[8] = {20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f};
    std::atomic<bool>  fist{true};          // [hands] fist: a free hand closes with its controller's grip, relaxes when let go
    std::atomic<float> staffOffset[3] = {0.0f, 0.0f, 0.0f};   // [hands] staff_x/y/z: cm, the staff past the right hand, its axes
    std::atomic<float> staffGrabCm{15.0f};  // [hands] staff_grab_cm: the left grip takes the staff only this near it
    std::atomic<float> leftStaffOffset[3] = {0.0f, 0.0f, 0.0f};
    std::atomic<float> xbowLeftOffset[3] = {0.0f, 0.0f, 0.0f};   // [hands] xbow_left_x/y/z: the same for a crossbow's left hand
    std::atomic<float> xbowLeftRoll{0.0f};
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
    std::atomic<bool>  bothAttach{true};     // [hands] both_attach: 0 = the other attach bone left alone (for comparing)   // [hands] carry_axis: moving, the game lays a 2H weapon from the right attach bone to the left - the left one put along this axis   // [hands] sword_hilt_axis: the hilt as the sword bone's axis, +-1..3 (x y z); 0 = from the game's grip
    float weaponAdj[D2RVR_TYPE_COUNT][6] = {};
    float weaponLeft[D2RVR_TYPE_COUNT][3] = {};   // (no longer used: one for every two-handed weapon, leftHold)
    std::atomic<float> leftHold[3] = {0.0f, 0.0f, 0.0f};
    std::atomic<float> leftHoldTurn[3] = {0.0f, 0.0f, 0.0f};   // [hands] left_hold_pitch/yaw/roll: deg, the same hand turned about its own axes   // [hands] left_hold_x/y/z: cm, the left hand once it holds any two-handed weapon (not the crossbow), its own axes   // [weapon_<kind>] left_x/y/z: cm, the left hand once it holds the weapon, its own axes (on top of left_staff_* etc.)   // [weapon_<kind>] x y z (cm) pitch yaw roll (deg); written and read on the update thread
    std::atomic<bool>  lockUpper{true};     // nothing above the pelvis from the game's animation
    std::atomic<bool>  legsUnder{true};     // with it: pelvis and legs kept under that body (no lunge, no hip turn)
    std::atomic<float> bodyYawDeg{0.0f};    // added to the hero's facing: fixes a body that stands sideways
    std::atomic<bool>  thirdTurn{true};     // [body] turn_third: from behind too the whole hero faces where the camera looks (the right stick turns him)
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
    std::atomic<int>   headsetHz{90};     // [stereo] headset_rate (0..4: 72 75 80 90 120)
    std::atomic<bool>  pictureRing{false};  // [stereo] picture_ring (off by default since 0.152: it juddered in head turns): FlatVR's addon hands over the newest finished picture (2.19: ReShade.ini [FLATVR] ColourRing)        // [stereo] stamps: each frame's head-pose moment to FlatVR (0: none at all, the screen at the head as it is)
    std::atomic<int>   pipelineDepth{0};    // the presented frame is this many of its eye's views older than the newest (D3D12 queueing)   // the frame stamp strip for FlatVR (bottom-right corner)
    std::atomic<bool>  topStereo{false};    // the game's own view from above (F12 off) in stereo too: each eye turned about the hero
    std::atomic<float> topAngle{3.0f};      // that turn between the eyes, degrees
    std::atomic<bool>  trueScale{true};     // eyes and zero-parallax plane from the FlatVR screen and the user's height: the world 1:1
    std::atomic<float> eyeMm{63.0f};        // the user's own eye distance, millimetres
    std::atomic<float> eyeHeightM{1.64f};   // the user's eyes above the floor STANDING: metres -> world units, seated or not
    std::atomic<bool>  dlssPerEye{true};   // [render] dlss_per_eye: real stereo with DLSS - a DLSS instance of its own for each eye
    std::atomic<bool>  pairPerTick{false};  // AFR: both eyes drawn from one game frame (the frame drawn twice), not by turns
    std::atomic<float> rightDtMs{0.01f};    // [stereo] right_dt_ms: the frame time the right pass of a pair gets (0 = none)
    std::atomic<bool>  bgFullSpeed{true};   // no Sleep(10) per frame while the game window is not in front
    std::atomic<bool>  gameHeightFog{false};   // [render] game_height_fog: keep the game's own height fog (gamefog::)
    std::atomic<int>   solidWalls{1};          // [render] solid_walls: 0 the game's see-through walls, 1 solid but in F1, 2 solid in every view
    std::atomic<float> uiShift{0.0f};       // AFR: the HUD moved apart per eye in the shader, % of the half screen; more = nearer
    std::atomic<float> hudTop{0.70f};       // the HUD is looked for below this height (0 top .. 1 bottom)
} g_set;

wchar_t g_iniPath[MAX_PATH];
FILETIME g_iniTime{};

float IniF(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, g_iniPath);
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? v : def;
}
bool IniB(const wchar_t* sec, const wchar_t* key, bool def) { return IniF(sec, key, def ? 1.0f : 0.0f) != 0.0f; }

// Sky: only in the open air. The area's biome comes from the game's
// BiomeSystem::SetCurrentBiome (hooked below); the exe names only the outdoor
// ones, dungeons and caves bring theirs from level data, so anything not on
// this list - [sky] outdoor in the ini replaces it - gets no sky. Tristram
// (act1_tristram, area 38), the monastery's courtyard (act1_court, the Outer Cloister -
// added 2026-10-07), Kurast (act3_kurast, 79-82), Travincal
// (act3_travincal_outdoors, 83), the River of Flame (act4_lava, 107), the Chaos
// Sanctuary (act4_diab, 108) and Nihlathak's Temple (expansion_wildtemple_tempenter,
// 121) come from level data too, and are open air.
SRWLOCK g_biomeLock = SRWLOCK_INIT;
std::vector<std::string> g_outdoor;
char g_biome[96] = "";                  // last name the game set, bare (no folder, no extension), lower case
std::atomic<uint32_t> g_biomeGen{0};    // bumped by every SetCurrentBiome

void LoadOutdoorBiomes() {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"sky", L"outdoor", L"act1_outdoors,act1_tristram,act1_court,act2_outdoors,act2_town,act3_jungle,act3_docktown,act3_kurast,act3_travincal_outdoors,act4_mesa,act4_lava,act4_diab,expansion_town,expansion_siege,expansion_mountaintop,expansion_ruins,expansion_ruins_snow,expansion_wildtemple_tempenter",
                             buf, (DWORD)std::size(buf), g_iniPath);
    std::vector<std::string> list;
    std::string cur;
    for (const wchar_t* p = buf;; ++p) {
        if (*p == L',' || *p == 0) {
            if (!cur.empty()) list.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else if (*p != L' ' && *p != L'\t' && *p < 128) cur += (char)towlower(*p);
    }
    AcquireSRWLockExclusive(&g_biomeLock); g_outdoor = list; ReleaseSRWLockExclusive(&g_biomeLock);
    g_biomeGen.fetch_add(1);   // re-judged with the new list
}

// Cave ceiling (docs/plan_cave_ceiling.md): the biomes that get a vault over the
// void, [ceiling] biomes: act 1's caves, then its crypts (2026-10-07); the other kinds
// of dungeon one by one.
std::vector<std::string> g_ceilBiomes;   // under g_biomeLock
// Each kind of dungeon its own: [ceiling] height_<biome>, brightness_<biome>,
// light_radius_<biome>; a key not there takes the common one (height, brightness,
// light_radius). The user sets them per dungeon in the settings window.
// And relief_<biome>; its picture texture_<biome> (LoadCeilingTextures, below), one of
// the shader's 4 slots: a crypt's brick vault is not a cave's rock.
// And wet_, detail_, contrast_<biome>: the barracks' smooth slabs shone and stood
// grainy with the cave rock's (the user, 2026-10-07).
// And a cathedral's groin vault instead of rock: vault_<biome> 1, bay_ (its bays' width),
// rib_width_, rib_depth_, bay_x_ / bay_z_ (the bays moved onto the game's columns);
// relief_ is then how far the vault rises from its springing to the crown.
// And dome_<biome> 1: over what the game draws higher than the ceiling (an altar's canopy)
// it rises as a sphere dome_radius_ round, by dome_max_ at most (the shader's height map).
// dome_<biome> 1: over what the game draws higher than dome_find_ (or the ceiling) - the cathedral's
// altar canopy - one smooth dome dome_radius_ round, by dome_max_ at most; tex_size_<biome>: world
// units one picture spans (0 = 4 tiles); relief_pic_<biome>: 0 noise, 1 light / -1 dark / 2 coloured
// parts of the picture stand out. (The groin vault, the vault on the columns or the walls and the
// walls at the floor's edge were tried on 2026-10-07 and taken out - commit 4ac321d has them.)
struct CeilVaultCfg { bool dome = false; float domeRadius = 30.0f, domeMax = 30.0f, domeFind = 0.0f;
                      float texSize = 0.0f, reliefPic = 0.0f; };
struct CeilBiomeCfg { std::string biome; float height, bright, light, relief, wet, detail, contrast; CeilVaultCfg vault; int slot = 0; };
std::vector<CeilBiomeCfg> g_ceilBiomeCfg;   // under g_biomeLock

void LoadCeilingBiomes() {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"ceiling", L"biomes", L"act1_caves,act1_crypt,act1_barracks,act1_cathedral,act1_catacombs,act2_sewer,act2_palace_clean", buf, (DWORD)std::size(buf), g_iniPath);
    std::vector<std::string> list;
    std::string cur;
    for (const wchar_t* p = buf;; ++p) {
        if (*p == L',' || *p == 0) {
            if (!cur.empty()) list.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else if (*p != L' ' && *p != L'\t' && *p < 128) cur += (char)towlower(*p);
    }
    std::vector<CeilBiomeCfg> cfg;
    for (const std::string& b : list) {
        const std::wstring w(b.begin(), b.end());
        cfg.push_back({b, std::clamp(IniF(L"ceiling", (L"height_" + w).c_str(), g_set.ceilHeight.load()), 1.0f, 500.0f),
                       std::clamp(IniF(L"ceiling", (L"brightness_" + w).c_str(), g_set.ceilBright.load()), 0.0f, 4.0f),
                       std::clamp(IniF(L"ceiling", (L"light_radius_" + w).c_str(), g_set.ceilLight.load()), 1.0f, 1000.0f),
                       std::clamp(IniF(L"ceiling", (L"relief_" + w).c_str(), g_set.ceilRelief.load()), 0.0f, 100.0f),
                       std::clamp(IniF(L"ceiling", (L"wet_" + w).c_str(), g_set.ceilWet.load()), 0.0f, 3.0f),
                       std::clamp(IniF(L"ceiling", (L"detail_" + w).c_str(), g_set.ceilDetail.load()), 0.0f, 10.0f),
                       std::clamp(IniF(L"ceiling", (L"contrast_" + w).c_str(), g_set.ceilContrast.load()), 0.0f, 5.0f)});
        CeilVaultCfg& v = cfg.back().vault;
        v.dome = IniB(L"ceiling", (L"dome_" + w).c_str(), false);
        v.domeRadius = std::clamp(IniF(L"ceiling", (L"dome_radius_" + w).c_str(), 30.0f), 5.0f, 200.0f);
        v.domeMax = std::clamp(IniF(L"ceiling", (L"dome_max_" + w).c_str(), 30.0f), 0.0f, 300.0f);
        v.domeFind = std::clamp(IniF(L"ceiling", (L"dome_find_" + w).c_str(), 0.0f), 0.0f, 500.0f);
        v.texSize = std::clamp(IniF(L"ceiling", (L"tex_size_" + w).c_str(), 0.0f), 0.0f, 1000.0f);
        v.reliefPic = std::clamp(IniF(L"ceiling", (L"relief_pic_" + w).c_str(), 0.0f), -1.0f, 2.0f);
    }
    AcquireSRWLockExclusive(&g_biomeLock);
    const bool same = g_ceilBiomes == list;
    g_ceilBiomes = list;
    for (CeilBiomeCfg& c : cfg)   // the picture's slot kept until LoadCeilingTextures looks again: no flicker on a reload
        for (const CeilBiomeCfg& old : g_ceilBiomeCfg) if (old.biome == c.biome) c.slot = old.slot;
    g_ceilBiomeCfg = cfg;
    ReleaseSRWLockExclusive(&g_biomeLock);
    if (!same) g_biomeGen.fetch_add(1);
}

// The ceiling's height, brightness, light reach, relief and picture slot (0 = none: the
// shader's own stone) in the area now - its biome's own, or the common ones.
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr) {
    float r = g_set.ceilRelief.load();
    int sl = 0;
    *height = g_set.ceilHeight.load(); *bright = g_set.ceilBright.load(); *light = g_set.ceilLight.load();
    AcquireSRWLockShared(&g_biomeLock);
    for (const CeilBiomeCfg& c : g_ceilBiomeCfg)
        if (c.biome == g_biome) { *height = c.height; *bright = c.bright; *light = c.light; r = c.relief; sl = c.slot; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    if (relief) *relief = r;
    if (slot) *slot = sl;
}

// The stone's look in the area now: its shine, how far the picture's light parts stand out, its fine detail.
void CeilingLookNow(float* wet, float* detail, float* contrast, CeilVaultCfg* vault) {
    *wet = g_set.ceilWet.load(); *detail = g_set.ceilDetail.load(); *contrast = g_set.ceilContrast.load();
    *vault = CeilVaultCfg{};
    AcquireSRWLockShared(&g_biomeLock);
    for (const CeilBiomeCfg& c : g_ceilBiomeCfg)
        if (c.biome == g_biome) { *wet = c.wet; *detail = c.detail; *contrast = c.contrast; *vault = c.vault; break; }
    ReleaseSRWLockShared(&g_biomeLock);
}

bool IsCeilingBiome(const std::string& biome) {
    bool on = false;
    AcquireSRWLockShared(&g_biomeLock);
    for (const std::string& b : g_ceilBiomes) if (b == biome) { on = true; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    return on;
}

// Starting palettes, by eye; picked by a part of the biome's name.
// act: the act slot 1..6 (6 = act 5 snow) whose brightness and pictures the area uses.
// bright: the ini key of the area's own brightness ([sky] brightness_*, on top of the common one).
struct SkyPalette { const char* part; float zenith[3], horizon[3], clouds, sun, sunElevDeg, sunAzDeg, stars; int act; };
const SkyPalette kPalettes[] = {
    {"snow",      {0.45f, 0.50f, 0.58f}, {0.80f, 0.82f, 0.85f}, 0.80f, 0.35f, 12, 140, 0.0f, 6},   // expansion_ruins_snow
    {"expansion", {0.20f, 0.25f, 0.32f}, {0.55f, 0.58f, 0.62f}, 0.60f, 0.30f, 10, 140, 0.0f, 5},   // cold ruins
    {"act5",      {0.20f, 0.25f, 0.32f}, {0.55f, 0.58f, 0.62f}, 0.60f, 0.30f, 10, 140, 0.0f, 5},
    {"act1",      {0.10f, 0.13f, 0.18f}, {0.35f, 0.30f, 0.28f}, 0.70f, 0.50f,  5, 230, 0.20f, 1},  // stormy night; painted by default (D2R_Sky_act1.png)
    {"act2",      {0.25f, 0.45f, 0.75f}, {0.85f, 0.75f, 0.55f}, 0.15f, 1.00f, 45,  60, 0.0f, 2},   // desert
    {"act3",      {0.15f, 0.30f, 0.35f}, {0.55f, 0.60f, 0.45f}, 0.50f, 0.40f, 35, 100, 0.0f, 3},   // humid haze
    {"act4",      {0.12f, 0.02f, 0.02f}, {0.60f, 0.15f, 0.05f}, 0.60f, 0.00f, 20,   0, 0.0f, 4},   // hell: smoke, no sun
};
constexpr int kDefaultPalette = 3;   // act1
// Per act, from [sky]: brightness_<act>, texture_<act> (the band round the
// viewer), cap_<act> (the zenith). An empty texture = the shader draws the sky
// from the palette. File names go to ReShade as preprocessor definitions.
const char* const kActKeys[7] = {"", "act1", "act2", "act3", "act4", "act5", "snow"};
const char* const kActDefs[7] = {"", "ACT1", "ACT2", "ACT3", "ACT4", "ACT5", "SNOW"};
struct SkyAct { float bright; std::string band, cap; bool bandOk, capOk; float fog[3]; bool fogSet;
                std::string nightBand, nightCap; bool nightOk; };   // [sky] night_<act> / night_cap_<act>: the night picture (gen_night_sky.py)
SRWLOCK g_skyCfgLock = SRWLOCK_INIT;
SkyAct g_skyAct[7];
// [fog] color_caves: one colour for every area off the outdoor list - caves,
// dungeons, crypts, towers. Empty = the act's own colour, as before.
struct CaveFog { float rgb[3]; bool set; };
CaveFog g_caveFog{};
std::atomic<uint32_t> g_skyCfgGen{0};   // bumped when a picture name changes

std::string Utf8(const wchar_t* w) {
    char b[1024];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, nullptr, nullptr);
    return n > 0 ? std::string(b) : std::string();
}

// "#RRGGBB" (a leading # and spaces optional) to 0..1; false = empty or not a colour.
bool ParseHexColor(const wchar_t* text, float rgb[3]) {
    unsigned v = 0;
    while (*text == L' ' || *text == L'#') ++text;
    const bool ok = wcslen(text) >= 6 && swscanf_s(text, L"%6x", &v) == 1;
    for (int c = 0; c < 3; ++c) rgb[c] = ok ? ((v >> (16 - 8 * c)) & 0xFF) / 255.0f : 0.0f;
    return ok;
}

// [table] background: the void's fill in the table view; empty or unreadable = black.
SRWLOCK g_tableBgLock = SRWLOCK_INIT;
float g_tableBg[3] = {0.0f, 0.0f, 0.0f};

void LoadTableBackground() {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"table", L"background", L"", buf, (DWORD)std::size(buf), g_iniPath);
    float rgb[3];
    if (!ParseHexColor(buf, rgb)) rgb[0] = rgb[1] = rgb[2] = 0.0f;
    AcquireSRWLockExclusive(&g_tableBgLock);
    for (int c = 0; c < 3; ++c) g_tableBg[c] = rgb[c];
    ReleaseSRWLockExclusive(&g_tableBgLock);
}

void LoadCaveFog() {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"fog", L"color_caves", L"#100D0B", buf, (DWORD)std::size(buf), g_iniPath);
    CaveFog f{};
    f.set = ParseHexColor(buf, f.rgb);
    AcquireSRWLockExclusive(&g_skyCfgLock); g_caveFog = f; ReleaseSRWLockExclusive(&g_skyCfgLock);
}

// A bare name is looked for where ReShade looks (reshade-shaders\Textures next to the game).
bool SkyFileExists(const std::wstring& name) {
    if (name.find(L':') != std::wstring::npos || name.rfind(L"\\\\", 0) == 0) return GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES;
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    while (n && dir[n - 1] != L'\\') --n;
    dir[n] = 0;
    return GetFileAttributesW((std::wstring(dir) + L"reshade-shaders\\Textures\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES;
}

void LoadCrosshairFile();   // [input] crosshair (the flat crosshair's picture), with the cursor code

// The ceiling's pictures, one per kind of dungeon: [ceiling] texture_<biome>, by default
// D2R_Sky_ours/D2R_Ceiling_<biome>.png, else [ceiling] texture (the act 1 caves' stone).
// The different files go to the shader's 4 slots as D2R_CEILING_1..4 (like the skies);
// a biome whose file is missing gets slot 0: the shader draws the stone itself.
// 8: a fifth dungeon picture (the act 2 sewers' brick) found no slot and drew the shader's own stone (2026-10-07)
constexpr int kCeilSlots = 8;
std::string g_ceilSlot[kCeilSlots + 1];   // [1..8] the files, under g_skyCfgLock
bool g_ceilSlotOk[kCeilSlots + 1] = {};

std::wstring IniPath(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"ceiling", key, def, buf, MAX_PATH, g_iniPath);
    std::wstring f = buf;
    while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
    while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
    return f;
}

void LoadCeilingTextures() {
    const std::wstring common = IniPath(L"texture", L"D2R_Sky_ours/D2R_Ceiling_act1_caves_walls.png");
    AcquireSRWLockExclusive(&g_biomeLock);
    std::vector<CeilBiomeCfg> cfg = g_ceilBiomeCfg;
    ReleaseSRWLockExclusive(&g_biomeLock);
    std::string slot[kCeilSlots + 1];
    int used = 0;
    for (CeilBiomeCfg& c : cfg) {
        const std::wstring w(c.biome.begin(), c.biome.end());
        std::wstring f = IniPath((L"texture_" + w).c_str(), (L"D2R_Sky_ours/D2R_Ceiling_" + w + L".png").c_str());
        if (f.empty() || !SkyFileExists(f)) f = common;
        c.slot = 0;
        if (f.empty() || !SkyFileExists(f)) { LogF("vrcam: cave ceiling %s - no picture found, the shader draws the stone itself", c.biome.c_str()); continue; }
        for (wchar_t& ch : f) if (ch == L'\\') ch = L'/';   // an FX string takes no backslashes
        const std::string name = Utf8(f.c_str());
        for (int k = 1; k <= used && !c.slot; ++k) if (slot[k] == name) c.slot = k;
        if (!c.slot && used < kCeilSlots) { slot[++used] = name; c.slot = used; }
    }
    AcquireSRWLockExclusive(&g_biomeLock);
    for (CeilBiomeCfg& now : g_ceilBiomeCfg)
        for (const CeilBiomeCfg& c : cfg) if (c.biome == now.biome) now.slot = c.slot;
    ReleaseSRWLockExclusive(&g_biomeLock);
    bool changed = false;
    AcquireSRWLockExclusive(&g_skyCfgLock);
    for (int k = 1; k <= kCeilSlots; ++k) {
        const bool ok = k <= used;
        if (g_ceilSlot[k] != slot[k] || g_ceilSlotOk[k] != ok) changed = true;
        g_ceilSlot[k] = slot[k]; g_ceilSlotOk[k] = ok;
    }
    ReleaseSRWLockExclusive(&g_skyCfgLock);
    if (changed) g_skyCfgGen.fetch_add(1);
}

void LoadSkyActs() {
    bool changed = false;
    for (int a = 1; a <= 6; ++a) {
        wchar_t key[32], buf[MAX_PATH];
        const std::wstring act(kActKeys[a], kActKeys[a] + strlen(kActKeys[a]));
        swprintf_s(key, L"brightness_%ls", act.c_str());
        const float bright = std::clamp(IniF(L"sky", key, 1.0f), 0.0f, 4.0f);
        std::string files[2]; bool ok[2];
        for (int k = 0; k < 2; ++k) {
            swprintf_s(key, k ? L"cap_%ls" : L"texture_%ls", act.c_str());
            wchar_t def[96];   // tools/gen_sky.py's names, in their own folder (the game's skies sit beside it)
            swprintf_s(def, k ? L"D2R_Sky_ours/D2R_SkyCap_%ls.png" : L"D2R_Sky_ours/D2R_Sky_%ls.png", act.c_str());
            GetPrivateProfileStringW(L"sky", key, def, buf, MAX_PATH, g_iniPath);
            std::wstring f = buf;
            while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
            while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
            ok[k] = !f.empty() && SkyFileExists(f);
            if (!f.empty() && !ok[k]) LogF("vrcam: sky %s %s '%s' NOT FOUND - %s", kActKeys[a], k ? "cap" : "texture", Utf8(f.c_str()).c_str(),
                                          k ? "the zenith takes the palette colour" : "the shader draws this sky itself");
            for (wchar_t& c : f) if (c == L'\\') c = L'/';   // an FX string takes no backslashes
            files[k] = Utf8(f.c_str());
        }
        // The night pictures: both must be there (the cap blends with the band).
        std::string night[2]; bool nightOk = true;
        for (int k = 0; k < 2; ++k) {
            swprintf_s(key, k ? L"night_cap_%ls" : L"night_%ls", act.c_str());
            wchar_t def[96];
            swprintf_s(def, k ? L"D2R_Sky_ours/D2R_SkyCapNight_%ls.png" : L"D2R_Sky_ours/D2R_SkyNight_%ls.png", act.c_str());
            GetPrivateProfileStringW(L"sky", key, def, buf, MAX_PATH, g_iniPath);
            std::wstring f = buf;
            while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
            while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
            nightOk = nightOk && !f.empty() && SkyFileExists(f);
            for (wchar_t& ch : f) if (ch == L'\\') ch = L'/';
            night[k] = Utf8(f.c_str());
        }
        // [fog] color_<act>: #RRGGBB; empty = the sky's horizon (or the dark default where there is no sky)
        swprintf_s(key, L"color_%ls", act.c_str());
        // defaults: each painted sky's horizon, a little darker (key missing; an empty key = from the sky)
        static const wchar_t* const kFogDef[7] = {L"", L"#242323", L"#683114", L"#50512F", L"#6C1809", L"#322A24", L"#7E8D99"};
        GetPrivateProfileStringW(L"fog", key, kFogDef[a], buf, MAX_PATH, g_iniPath);
        float fog[3] = {};
        const bool fogSet = ParseHexColor(buf, fog);
        AcquireSRWLockExclusive(&g_skyCfgLock);
        SkyAct& s = g_skyAct[a];
        if (s.band != files[0] || s.cap != files[1] || s.bandOk != ok[0] || s.capOk != ok[1] ||
            s.nightBand != night[0] || s.nightCap != night[1] || s.nightOk != nightOk) changed = true;
        s = {bright, files[0], files[1], ok[0], ok[1], {fog[0], fog[1], fog[2]}, fogSet, night[0], night[1], nightOk};
        ReleaseSRWLockExclusive(&g_skyCfgLock);
    }
    if (changed) g_skyCfgGen.fetch_add(1);
    LoadCaveFog();
}

// The act (1..6) a biome belongs to by its name, outdoors or not; 0 = cannot tell.
int ActOfBiome(const std::string& biome) {
    for (const SkyPalette& p : kPalettes) if (biome.find(p.part) != std::string::npos) return p.act;
    return 0;
}

// On the [sky] outdoor list: open air. Everything else in the world is a cave,
// a dungeon or a building.
bool IsOutdoorBiome(const std::string& biome) {
    bool outdoor = false;
    AcquireSRWLockShared(&g_biomeLock);
    for (const std::string& o : g_outdoor) if (o == biome) { outdoor = true; break; }
    ReleaseSRWLockShared(&g_biomeLock);
    return outdoor;
}

// -1 = no sky here. Called on the timer thread.
int SkyPaletteFor(const std::string& biome, bool always) {
    if (!always && !IsOutdoorBiome(biome)) return -1;
    for (int i = 0; i < (int)std::size(kPalettes); ++i) if (biome.find(kPalettes[i].part) != std::string::npos) return i;
    return kDefaultPalette;
}


// The game's own interface size ("Safe Screen Percent" in Saved Games\Diablo II
// Resurrected\Settings.json, 50..100), as a fraction: the toolbar's size and
// place follow it (hud.cpp). 1 when it cannot be read.
float GameInterfaceScale() {
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return 1.0f;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const size_t at = text.find("\"Safe Screen Percent\"");
    const size_t colon = at == std::string::npos ? at : text.find(':', at);
    if (colon == std::string::npos) return 1.0f;
    const float pct = (float)atof(text.c_str() + colon + 1);
    return pct >= 30.0f && pct <= 100.0f ? pct / 100.0f : 1.0f;
}

// Real stereo with the game's temporal anti-aliasing ("Anti Aliasing" 2 and up in Settings.json):
// it blends each picture with the one before - the other eye's - and every moving thing trails a
// ghost (the staff in the hand, 2026-10-09). At the plugin's start, before the game reads the
// file, it is set to FXAA (1); [stereo] keep_taa=1 leaves it alone.
void NoTemporalAA() {
    if (!IniB(L"stereo", L"afr", false) || IniB(L"stereo", L"keep_taa", false)) return;
    PWSTR dir = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &dir))) path = std::wstring(dir) + L"\\Diablo II Resurrected\\Settings.json";
    if (dir) CoTaskMemFree(dir);
    FILE* f = path.empty() ? nullptr : _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string text(1 << 16, ' ');
    text.resize(fread(text.data(), 1, text.size(), f));
    fclose(f);
    const size_t at = text.find("\"Anti Aliasing\"");
    const size_t colon = at == std::string::npos ? at : text.find(':', at);
    if (colon == std::string::npos) return;
    size_t b = colon + 1;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) ++b;
    size_t e = b;
    while (e < text.size() && isdigit((unsigned char)text[e])) ++e;
    if (e == b) return;
    const int aa = atoi(text.c_str() + b);
    if (aa < 2) return;
    text.replace(b, e - b, "1");
    if (FILE* w = _wfopen(path.c_str(), L"wb")) {
        fwrite(text.data(), 1, text.size(), w);
        fclose(w);
        LogF("vrcam: the game's Anti Aliasing was %d (temporal: it mixes the eyes, a ghost behind everything moving) - set to FXAA ([stereo] keep_taa=1 to keep it)", aa);
    }
}

void StartMatrixWatch();   // [debug] matrix_writer, below PickHeroMatrix

namespace drawprof { extern std::atomic<bool> g_want; }
namespace cbcmp { extern std::atomic<bool> g_go; void NoteView(int eye, const float v[16]); }
namespace rtrace { extern std::atomic<bool> g_go; }
extern std::atomic<bool> g_xrThirdGaze; extern std::atomic<int> g_projFix; extern std::atomic<float> g_shiftMul; extern std::atomic<int> g_replayViews; extern std::atomic<int> g_viewMask; extern std::atomic<int> g_replayPositions; extern std::atomic<int> g_replayPrev;
namespace replay { extern std::atomic<bool> g_fxSync; extern std::atomic<bool> g_dlssBoth; extern std::atomic<bool> g_replayCopy; }
namespace replay { extern std::atomic<int> g_peekWant; }
namespace camfix { extern std::atomic<int> g_scanWant; }
namespace flog { void StartDiag(); }
namespace replay { extern std::atomic<int> g_want; extern std::atomic<int> g_ahead; extern std::atomic<bool> g_depthCopy; extern std::atomic<bool> g_on; extern std::atomic<bool> g_fxAtHold; extern std::atomic<bool> g_holdSignalsOn; }
// The left eye's view as last built, and the half-eye shift in it (VrViewInner): the
// replay makes the right eye's camera constants from them.
float g_leftView[16] = {};
SRWLOCK g_leftViewLock = SRWLOCK_INIT;
std::atomic<float> g_eyeHalf{0.0f};
void LoadSettings() {
    g_set.platform.store(IniF(L"mode", L"platform", 1.0f) != 0.0f ? 1 : 0);
#if D2RVR_FIRST_PERSON
    g_set.flatView.store(std::clamp((int)IniF(L"mode", L"flat_view", 3.0f), 1, 3));
#else
    g_set.flatView.store(std::clamp((int)IniF(L"mode", L"flat_view", 1.0f), 1, 2));
#endif
    // [mode] vr_mode: 1 above, 2 third person, 3 the game on the floor, 4 first person
    // with the body. It replaced vr_view (2026-10-05), which also had the mouse-and-
    // keyboard first person as 3 - an ini with only that is carried over. 3 and 4 were
    // the other way round until vr_mode_order=2 (2026-10-05: the body, not ready for
    // everyone, went last); an older ini's is swapped once. Nothing set: from above.
    {
        int v = (int)IniF(L"mode", L"vr_mode", 0.0f);
        bool write = false;
        if (v < 1 || v > 4) {
            static const int kFromOld[6] = {1, 1, 2, 4, 4, 3};
            v = kFromOld[std::clamp((int)IniF(L"mode", L"vr_view", 0.0f), 0, 5)];
            write = true;
        } else if (IniF(L"mode", L"vr_mode_order", 1.0f) < 2.0f) {
            v = v == 3 ? 4 : v == 4 ? 3 : v;
            write = true;
        }
        if (write) {
            wchar_t b[8]; swprintf_s(b, L"%d", v);
            WritePrivateProfileStringW(L"mode", L"vr_mode", b, g_iniPath);
            WritePrivateProfileStringW(L"mode", L"vr_mode_order", L"2", g_iniPath);
        }
#if !D2RVR_FIRST_PERSON
        if (v > 3) v = 1;
#endif
        g_set.vrView.store(v);
    }
    g_set.fov.store(std::clamp(IniF(L"camera", L"fov", 60.0f), 20.0f, 140.0f));
    g_set.fovFromFlatVR.store(IniB(L"camera", L"fov_from_flatvr", true) && g_set.platform.load() == 1);   // no FlatVR on a monitor
    g_set.distance.store(std::clamp(IniF(L"camera", L"distance", -1.0f), -20.0f, 1500.0f));
    g_set.height.store(IniF(L"camera", L"height", 8.0f));
    g_set.heightAuto.store(IniB(L"camera", L"height_auto", true));
    g_set.eyeOffset.store(std::clamp(IniF(L"camera", L"eye_offset", 0.0f), -5.0f, 5.0f));
    g_set.followJump.store(IniB(L"camera", L"follow_jump", true));
    g_set.jumpFrom.store(std::clamp(IniF(L"camera", L"jump_from", 0.3f), 0.0f, 3.0f));
    g_set.pitch.store(std::clamp(IniF(L"camera", L"pitch", -60.0f), -150.0f, 60.0f));
    g_set.rings.store(std::clamp((int)IniF(L"render", L"rings", 4.0f), 1, 8));
    g_set.modelRadius.store(std::clamp(IniF(L"render", L"model_radius", 1000.0f), 10.0f, 10000.0f));
    g_set.mouseLook.store(IniB(L"input", L"mouse_look", false));
    g_set.hudHide.store(std::clamp((int)IniF(L"hud", L"hide", 0.0f), 0, 2));
    hud::SetHide(g_set.hudHide.load());
    g_set.hudMap.store(std::clamp((int)IniF(L"hud", L"map", 0.0f), 0, 1));
    hud::SetMapMode(g_set.hudMap.load());
    hud::SetInterfaceScale(GameInterfaceScale());
    hud::SetMapCorner((int)IniF(L"hud", L"map_corner", 0.0f));
    g_set.hudClassic.store(std::clamp((int)IniF(L"hud", L"classic", 0.0f), 0, 1));
    g_set.barNear.store(std::clamp(IniF(L"hud", L"bar_near", 0.0f), -20.0f, 20.0f));
    g_set.classicTop.store(std::clamp((int)IniF(L"hud_top", L"classic", (float)g_set.hudClassic.load()), 0, 1));
    g_set.classicThird.store(std::clamp((int)IniF(L"hud_third", L"classic", (float)g_set.hudClassic.load()), 0, 1));
    g_set.barNearTop.store(std::clamp(IniF(L"hud_top", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barNearThird.store(std::clamp(IniF(L"hud_third", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barNearInside.store(std::clamp(IniF(L"hud_inside", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barSizeTop.store(std::clamp(IniF(L"hud_top", L"bar_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.barSizeThird.store(std::clamp(IniF(L"hud_third", L"bar_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.mapZoomInside.store(std::clamp(IniF(L"hud_inside", L"map_zoom", 1.0f), 0.3f, 3.0f));
    g_set.classicFloor.store(std::clamp((int)IniF(L"hud_floor", L"classic", 0.0f), 0, 1));
    g_set.barNearFloor.store(std::clamp(IniF(L"hud_floor", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barSizeFloor.store(std::clamp(IniF(L"hud_floor", L"bar_size", 100.0f), 20.0f, 300.0f) * 0.01f);
    // bar_x: % of the screen's width, + right; bar_y: % of its height, + up (0, 0 = where the game puts it)
    g_set.barXTop.store(std::clamp(IniF(L"hud_top", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYTop.store(-std::clamp(IniF(L"hud_top", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barXThird.store(std::clamp(IniF(L"hud_third", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYThird.store(-std::clamp(IniF(L"hud_third", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barXFloor.store(std::clamp(IniF(L"hud_floor", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYFloor.store(-std::clamp(IniF(L"hud_floor", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.mapSizeFloor.store(std::clamp(IniF(L"hud_floor", L"map_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.mapNearFloor.store(std::clamp(IniF(L"hud_floor", L"map_near", 0.0f), -20.0f, 20.0f));
    g_set.mapXFloor.store(std::clamp(IniF(L"hud_floor", L"map_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.mapYFloor.store(-std::clamp(IniF(L"hud_floor", L"map_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.labelsAlphaFloor.store(std::clamp(IniF(L"hud_floor", L"labels_alpha", 0.5f), 0.0f, 1.0f));
    g_set.labelsSizeFloor.store(std::clamp(IniF(L"hud_floor", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsNativeFloor.store(IniF(L"hud_floor", L"labels_native", 1.0f) != 0.0f);
    g_set.labelsAlphaTop.store(std::clamp(IniF(L"hud_top", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaThird.store(std::clamp(IniF(L"hud_third", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaBody.store(std::clamp(IniF(L"hud", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.plateAlpha.store(std::clamp(IniF(L"hud", L"monster_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsSizeTop.store(std::clamp(IniF(L"hud_top", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsSizeThird.store(std::clamp(IniF(L"hud_third", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsSizeBody.store(std::clamp(IniF(L"hud", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsNear.store(std::clamp(IniF(L"hud", L"labels_near", 0.0f), -20.0f, 20.0f));
    g_set.labelsTilt.store(std::clamp(IniF(L"hud", L"labels_tilt", 0.0f), -40.0f, 40.0f));
    for (int i = 0; i < 2; ++i) {
        const wchar_t* sec = i == 0 ? L"hud_top" : L"hud_third";
        g_set.pointerNear[i].store(std::clamp(IniF(sec, L"pointer_near", 0.0f), -20.0f, 20.0f));
        g_set.pointerTilt[i].store(std::clamp(IniF(sec, L"pointer_tilt", 0.0f), -40.0f, 40.0f));
        const float n = g_set.pointerNear[i].load(), t = g_set.pointerTilt[i].load();
        g_set.pointerAt[i][0].store(std::clamp(IniF(sec, L"pointer_top", n - 0.5f * t), -40.0f, 40.0f));
        g_set.pointerAt[i][1].store(std::clamp(IniF(sec, L"pointer_mid", n), -40.0f, 40.0f));
        g_set.pointerAt[i][2].store(std::clamp(IniF(sec, L"pointer_bottom", n + 0.5f * t), -40.0f, 40.0f));
        g_set.pointerTopSize[i].store(std::clamp(IniF(sec, L"pointer_top_size", 100.0f), 10.0f, 200.0f));
    }
    g_set.crosshairDepth.store(std::clamp(IniF(L"hud_third", L"crosshair_depth", 0.0f), -40.0f, 40.0f));
    g_set.crosshairOn.store(IniB(L"hud_third", L"crosshair", true));
    g_set.crosshairSize.store(std::clamp(IniF(L"hud_third", L"crosshair_size", 100.0f), 25.0f, 400.0f));
    g_set.crosshairDepthDown.store(std::clamp(IniF(L"hud_third", L"crosshair_depth_down", g_set.crosshairDepth.load()), -40.0f, 40.0f));
    {   // [hud] how FlatVR shows the toolbar and the map (game_hud_shared.h)
        FlatVRGameHudLook look = FlatVRGameHudDefaultLook();
        look.bar_anchor = (uint32_t)std::clamp((int)IniF(L"hud", L"bar_place", (float)look.bar_anchor), 0, 4);
        // A set of its own on a forearm (bar_*_arm) and on the body (bar_*_body); the
        // one key of before (bar_*) stands in for both until they are set.
        const wchar_t* set = look.bar_anchor == 1 || look.bar_anchor == 2 ? L"_arm" : L"_body";
        auto bar = [&](const wchar_t* name, float def) {
            wchar_t key[64];
            swprintf_s(key, L"bar_%s%s", name, set);
            wchar_t old[64];
            swprintf_s(old, L"bar_%s", name);
            return IniF(L"hud", key, IniF(L"hud", old, def));
        };
        look.bar_roll_deg = std::clamp(bar(L"roll", 0.0f), -180.0f, 180.0f);
        look.bar_tip_deg = std::clamp(bar(L"tip", 0.0f), -90.0f, 90.0f);
        look.bar_spin_deg = std::clamp(bar(L"spin", 0.0f), -180.0f, 180.0f);
        look.bar_lift_cm = std::clamp(bar(L"lift", 0.0f), -10.0f, 20.0f);
        look.bar_side_cm = std::clamp(bar(L"side", 0.0f), -20.0f, 20.0f);
        look.bar_width_m = std::clamp(bar(L"width", look.bar_width_m), 0.05f, 1.5f);
        look.bar_along_cm = std::clamp(bar(L"along", look.bar_along_cm), -30.0f, 40.0f);
        look.bar_split = (uint32_t)std::clamp((int)bar(L"split", 0.0f), 0, 2);   // two halves side by side, both orbs at one end
        look.map_anchor = (uint32_t)std::clamp((int)IniF(L"hud", L"map_place", (float)look.map_anchor), 0, 4);
        look.map_orb = IniB(L"hud", L"map_orb", look.map_orb != 0) ? 1u : 0u;
        look.map_width_m = std::clamp(IniF(L"hud", L"map_width", look.map_width_m), 0.05f, 1.5f);
        look.orb_size_m = std::clamp(IniF(L"hud", L"orb_size", look.orb_size_m), 0.03f, 0.6f);
        look.orb_zoom = std::clamp(IniF(L"hud", L"orb_zoom", look.orb_zoom), 0.5f, 4.0f);
        look.orb_glow = std::clamp(IniF(L"hud", L"orb_glow", look.orb_glow), 0.0f, 1.0f);
        look.orb_alpha = std::clamp(IniF(L"hud", L"orb_alpha", look.orb_alpha), 0.02f, 1.0f);
        look.encoded = IniB(L"hud", L"colors_as_monitor", look.encoded != 0) ? 1u : 0u;
        wchar_t col[64];
        GetPrivateProfileStringW(L"hud", L"orb_color", L"", col, (DWORD)std::size(col), g_iniPath);
        float rgb[3];
        if (ParseHexColor(col, rgb))
            look.orb_rgb = ((uint32_t)std::lround(rgb[0] * 255.0f) << 16) | ((uint32_t)std::lround(rgb[1] * 255.0f) << 8) |
                           (uint32_t)std::lround(rgb[2] * 255.0f);
        hud::SetLook(look);
    }
    g_set.mouseSpeed.store(std::clamp(IniF(L"input", L"mouse_speed", 0.15f), 0.01f, 2.0f));
    g_set.nearClip.store(std::clamp(IniF(L"camera", L"near", 1.5f), 0.0f, 20.0f));
    g_set.camSide.store(std::clamp(IniF(L"camera", L"side", 0.0f), -10.0f, 10.0f));
    g_set.camForward.store(std::clamp(IniF(L"camera", L"forward", 0.0f), -10.0f, 10.0f));
    g_set.neckModel.store(IniB(L"camera", L"neck_model", true));
    g_set.neckUpCm.store(std::clamp(IniF(L"camera", L"neck_up_cm", 10.0f), 0.0f, 30.0f));
    g_set.neckFwdCm.store(std::clamp(IniF(L"camera", L"neck_forward_cm", 9.0f), 0.0f, 30.0f));
    g_set.headYaw.store(IniB(L"head", L"yaw_from_head", true));
    g_set.yawSign.store(IniF(L"head", L"yaw_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.yawScale.store(IniF(L"head", L"yaw_scale", 1.0f));
    g_set.headPitch.store(IniB(L"head", L"pitch_from_head", true));
    g_set.headPredict.store(IniB(L"head", L"predict", false));
    g_set.pitchSign.store(IniF(L"head", L"pitch_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.headRoll.store(IniB(L"head", L"roll_from_head", true));
    g_set.rollSign.store(IniF(L"head", L"roll_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.stickRotate.store(IniB(L"stick", L"rotate", true));
    g_set.stickSign.store(IniF(L"stick", L"sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.stickSquash.store(std::clamp(IniF(L"stick", L"squash", 1.0f), 0.1f, 4.0f));
    g_set.stickExact.store(IniB(L"stick", L"exact", true));
    g_set.stickAngleScale.store(std::clamp(IniF(L"stick", L"angle_scale", 1.0f), 0.0f, 4.0f));
    g_set.rightTurn.store(IniB(L"turn", L"right_stick", true));
    g_set.turnSpeed.store(std::clamp(IniF(L"turn", L"speed", 120.0f), 10.0f, 720.0f));
    g_set.snapAngle.store(std::clamp(IniF(L"turn", L"snap", 0.0f), 0.0f, 180.0f));
    g_set.turnSign.store(IniF(L"turn", L"sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.afr.store(IniB(L"stereo", L"afr", false) && g_set.platform.load() == 1);   // on a monitor alternate eyes only flicker
    g_set.ipd.store(std::clamp(IniF(L"stereo", L"ipd", 0.27f), 0.0f, 5.0f));
    g_set.afrSwap.store(IniB(L"stereo", L"swap", false));
    g_set.dlssPerEye.store(IniB(L"render", L"dlss_per_eye", true));
    g_set.pairPerTick.store(IniB(L"stereo", L"pair_per_tick", false));
    g_set.rightDtMs.store(std::clamp(IniF(L"stereo", L"right_dt_ms", 0.01f), 0.0f, 5.0f));
    g_set.trueScale.store(IniB(L"stereo", L"true_scale", true));
    g_set.eyeMm.store(std::clamp(IniF(L"stereo", L"eye_mm", 63.0f), 45.0f, 80.0f));
    // From the user's height: the eyes of an adult sit at ~0.936 of it.
    g_set.eyeHeightM.store(0.936f * std::clamp(IniF(L"stereo", L"user_height_m", 1.75f), 1.1f, 2.3f));
    g_set.topStereo.store(IniB(L"stereo", L"top_down", false) && g_set.platform.load() == 1);
    g_set.topAngle.store(std::clamp(IniF(L"stereo", L"top_angle", 3.0f), 0.0f, 20.0f));
    g_set.convergence.store(std::clamp(IniF(L"stereo", L"convergence", 15.0f), 0.0f, 1000.0f));
    g_set.bgFullSpeed.store(IniB(L"render", L"background_full_speed", true));
    g_set.gameHeightFog.store(IniB(L"render", L"game_height_fog", false));
    g_set.solidWalls.store(std::clamp((int)IniF(L"render", L"solid_walls", 1.0f), 0, 2));
    g_set.stampPixels.store(IniB(L"stereo", L"stamp_pixels", true));
    g_set.stamps.store(IniB(L"stereo", L"stamps", true));
    g_set.pictureRing.store(IniB(L"stereo", L"picture_ring", false));
    g_set.pace.store(IniB(L"stereo", L"pace", true));
    {
        static const int kHz[5] = {72, 75, 80, 90, 120};
        g_set.headsetHz.store(kHz[std::clamp((int)IniF(L"stereo", L"headset_rate", 3.0f), 0, 4)]);
    }
    replay::g_want.store((int)IniF(L"stereo", L"replay_right", 0.0f));
    replay::g_ahead.store(std::clamp((int)IniF(L"stereo", L"replay_ahead", 1.0f), 1, 3));
    replay::g_depthCopy.store(IniB(L"stereo", L"replay_depth", false));
    replay::g_holdSignalsOn.store(IniB(L"stereo", L"replay_hold_signals", false));
    g_projFix.store((int)IniF(L"stereo", L"replay_proj", 3.0f));
    g_shiftMul.store(IniF(L"stereo", L"replay_shift_mul", 1.0f));
    g_replayViews.store(std::clamp((int)IniF(L"stereo", L"replay_views", 8.0f), 0, 8));
    g_viewMask.store((int)IniF(L"stereo", L"replay_view_mask", 0.0f));
    g_replayPositions.store((int)IniF(L"stereo", L"replay_positions", 2.0f));
    g_replayPrev.store((int)IniF(L"stereo", L"replay_prev", 1.0f));
    replay::g_fxSync.store(IniB(L"stereo", L"replay_fx_sync", true));
    replay::g_dlssBoth.store(IniB(L"stereo", L"replay_dlss", false));
    replay::g_replayCopy.store(IniB(L"stereo", L"replay_copy", true));
    replay::g_fxAtHold.store(IniB(L"stereo", L"replay_fx", true));
    xr::LoadSettings(g_iniPath);   // [openxr]: the game in the headset itself (vr/xr.cpp)
    g_xrThirdGaze.store(IniB(L"openxr", L"third_gaze", false));
    {   // [debug] profile_draw: one 5 s profile of the draw thread each time it turns 1
        static bool was = false;
        const bool on = IniB(L"debug", L"profile_draw", false);
        if (on && !was) drawprof::g_want.store(true);
        was = on;
        static bool wasGo = false;
        const bool go = IniB(L"debug", L"cb_compare_go", false);
        if (go && !wasGo) cbcmp::g_go.store(true);
        wasGo = go;
        static bool wasScan = false;   // [debug] replay_scan_go 0 -> 1: copies of the left camera in upload memory
        const bool scan = IniB(L"debug", L"replay_scan_go", false);
        if (scan && !wasScan) camfix::g_scanWant.store(1);
        wasScan = scan;
        static bool wasPeek = false;   // [debug] replay_peek_go 0 -> 1: one pair's two pictures compared
        const bool peek = IniB(L"debug", L"replay_peek_go", false);
        if (peek && !wasPeek) replay::g_peekWant.store(1);
        wasPeek = peek;
        static bool wasRt = false;
        const bool rt = IniB(L"debug", L"replay_trace_go", false);
        if (rt && !wasRt) rtrace::g_go.store(true);
        wasRt = rt;
        // [debug] diag_go: D2R VR Settings' Collect logs writes a new value - 10 s of the frame
        // log (and FlatVR's pose trace beside it) for the logs it zips. The value at start is
        // only noted: a capture begins on a change while the game runs.
        static int lastDiag = INT_MIN;
        const int diag = (int)IniF(L"debug", L"diag_go", 0.0f);
        if (lastDiag != INT_MIN && diag != lastDiag) flog::StartDiag();
        lastDiag = diag;
    }
    g_set.pipelineDepth.store(std::clamp((int)IniF(L"stereo", L"pipeline_depth", 0.0f), 0, 3));
    g_set.thirdDistance.store(std::clamp(IniF(L"third", L"distance", 7.0f), -5.0f, 200.0f));
    g_set.tableFloor.store(std::clamp(IniF(L"table", L"floor", 0.05f), 0.0f, 0.3f));
    // [table] hero_cm: how tall the hero stands on the floor - the size of the whole
    // game field, as a number one can picture. A hero is ~6 world units tall.
    g_set.tableScale.store(6.0f / (std::clamp(IniF(L"table", L"hero_cm", 40.0f), 5.0f, 250.0f) * 0.01f));
    g_set.topPersp.store(IniB(L"top", L"perspective", false));
    g_set.topTilt.store(std::clamp(IniF(L"top", L"tilt", 0.0f), -40.0f, 60.0f));
    g_set.tableHeightM.store(std::clamp(IniF(L"table", L"height_m", 0.0f), -2.0f, 2.0f));
    g_set.tableTurnDeg.store(std::clamp(IniF(L"table", L"turn", 0.0f), -180.0f, 180.0f));
    g_set.tablePlace.store((int)IniF(L"table", L"place", 0.0f));
    g_set.tableBounds.store(std::clamp(IniF(L"table", L"bounds", 1.0f), 0.0f, 5.0f));
    g_set.tableAheadM.store(std::clamp(IniF(L"table", L"ahead_m", 1.0f), 0.0f, 5.0f));
    LoadTableBackground();
    g_set.thirdHeight.store(std::clamp(IniF(L"third", L"height", 6.5f), 0.0f, 40.0f));
    g_set.uiShift.store(std::clamp(IniF(L"stereo", L"ui_near", 0.0f), 0.0f, 20.0f));
    g_set.hudTop.store(std::clamp(IniF(L"stereo", L"ui_top", 0.70f), 0.0f, 1.0f));
    g_set.fogOn.store(IniB(L"fog", L"enabled", false));
    g_set.fogStart.store(std::clamp(IniF(L"fog", L"start", 150.0f), 0.0f, 100000.0f));
    g_set.fogEnd.store(std::clamp(IniF(L"fog", L"end", 600.0f), g_set.fogStart.load() + 1.0f, 200000.0f));
    // caves: their own range, the open air's when not set (caves are smaller, their fog nearer)
    g_set.fogCaveStart.store(std::clamp(IniF(L"fog", L"caves_start", g_set.fogStart.load()), 0.0f, 100000.0f));
    g_set.fogCaveEnd.store(std::clamp(IniF(L"fog", L"caves_end", g_set.fogEnd.load()), g_set.fogCaveStart.load() + 1.0f, 200000.0f));
    g_set.fogCaveStrength.store(std::clamp(IniF(L"fog", L"caves_strength", 1.0f), 0.0f, 1.0f));
    g_set.fogCurve.store(std::clamp(IniF(L"fog", L"curve", 1.6f), 0.3f, 4.0f));
    g_set.fogBlur.store(std::clamp(IniF(L"fog", L"blur", 3.0f), 0.0f, 12.0f));
    g_set.fogCaves.store(IniB(L"fog", L"caves", true));
    {
        wchar_t poke[256];
        GetPrivateProfileStringW(L"debug", L"fog_poke", L"", poke, (DWORD)std::size(poke), g_iniPath);
        gamefog::SetPoke(poke);
        // [debug] skip_pipeline: a game pipeline (hex, as Ctrl+F10's trace prints "last pipe") whose draws are
        // skipped - which pass draws something, found by elimination; nothing in the game's memory changes
        wchar_t sp[64];
        GetPrivateProfileStringW(L"debug", L"skip_pipeline", L"0", sp, (DWORD)std::size(sp), g_iniPath);
        static uint64_t toldSkip = 0;
        const uint64_t skip = wcstoull(sp, nullptr, 16);
        uitrace::SetSkip(skip);
        if (skip != toldSkip) { toldSkip = skip; LogF("vrcam: [debug] skip_pipeline %llx", (unsigned long long)skip); }
        // [render] skip_shaders: the game's passes left out for good, by their pixel shader (Ctrl+F10's "ps"),
        // space-separated hex. Empty = none; the default is filled in once one is confirmed.
        wchar_t ss[512];
        GetPrivateProfileStringW(L"render", L"skip_shaders", L"", ss, (DWORD)std::size(ss), g_iniPath);
        uint64_t hs[8];
        int nh = 0;
        for (const wchar_t* q = ss; *q && nh < 8;) {
            wchar_t* end = nullptr;
            const uint64_t h = wcstoull(q, &end, 16);
            if (end == q) { ++q; continue; }
            if (h) hs[nh++] = h;
            q = end;
        }
        uitrace::SetSkipShaders(hs, nh);
        static std::wstring toldShaders;
        if (toldShaders != ss) { toldShaders = ss; LogF("vrcam: [render] skip_shaders - %d shader(s) left out", nh); }
    }
    g_set.skyOn.store(IniB(L"sky", L"enabled", false));
    LoadSkyActs();
    LoadCrosshairFile();
    g_set.skyClouds.store(std::clamp(IniF(L"sky", L"clouds", 1.0f), 0.0f, 3.0f));
    g_set.skyDayNight.store(IniB(L"sky", L"day_night", true));
    g_set.skyDrift.store(IniB(L"sky", L"drift", false));
    g_set.lightDay.store(std::clamp(IniF(L"sky", L"light_day", 128.0f), 1.0f, 255.0f));
    g_set.lightNight.store(std::clamp(IniF(L"sky", L"light_night", 65.0f), 0.0f, 254.0f));
    g_set.nightBright.store(std::clamp(IniF(L"sky", L"night_brightness", 0.25f), 0.0f, 1.0f));
    g_set.skyAlways.store(IniB(L"sky", L"always", false));
    g_set.skyHidePanels.store(IniB(L"sky", L"hide_with_panels", true));
    LoadOutdoorBiomes();
    g_set.ceilOn.store(IniB(L"ceiling", L"enabled", false));
    g_set.ceilHeight.store(std::clamp(IniF(L"ceiling", L"height", 30.0f), 1.0f, 500.0f));
    g_set.ceilScale.store(std::clamp(IniF(L"ceiling", L"scale", 10.0f), 0.5f, 1000.0f));
    g_set.ceilBright.store(std::clamp(IniF(L"ceiling", L"brightness", 1.0f), 0.0f, 4.0f));
    g_set.ceilRelief.store(std::clamp(IniF(L"ceiling", L"relief", 6.0f), 0.0f, 100.0f));
    g_set.ceilSteps.store(std::clamp((int)IniF(L"ceiling", L"steps", 12.0f), 1, 64));
    g_set.ceilLight.store(std::clamp(IniF(L"ceiling", L"light_radius", 25.0f), 1.0f, 1000.0f));
    g_set.ceilTorches.store(IniB(L"ceiling", L"torches", false));
    g_set.ceilTorchBright.store(std::clamp(IniF(L"ceiling", L"torch_brightness", 0.6f), 0.0f, 5.0f));
    g_set.ceilTorchRadius.store(std::clamp(IniF(L"ceiling", L"torch_radius", 20.0f), 1.0f, 500.0f));
    g_set.ceilTorchDist.store(std::clamp(IniF(L"ceiling", L"torch_distance", 80.0f), 5.0f, 1000.0f));
    g_set.ceilMonsters.store(IniB(L"ceiling", L"monster_lights", false));
    g_set.ceilMonsterBright.store(std::clamp(IniF(L"ceiling", L"monster_brightness", 0.35f), 0.0f, 3.0f));
    g_set.ceilHalo.store(std::clamp(IniF(L"ceiling", L"torch_halo", 1.0f), 0.0f, 1.0f));
    g_set.ceilTorchWarm.store(std::clamp(IniF(L"ceiling", L"torch_warmth", 0.6f), 0.0f, 1.0f));
    g_set.ceilWall.store(std::clamp(IniF(L"ceiling", L"wall_distance", 200.0f), 0.0f, 5000.0f));
    g_set.ceilFloor.store(std::clamp(IniF(L"ceiling", L"floor_depth", 5.0f), 0.0f, 500.0f));
    g_set.ceilWet.store(std::clamp(IniF(L"ceiling", L"wet", 0.35f), 0.0f, 3.0f));
    g_set.ceilDetail.store(std::clamp(IniF(L"ceiling", L"detail", 0.8f), 0.0f, 10.0f));
    g_set.ceilContrast.store(std::clamp(IniF(L"ceiling", L"contrast", 1.6f), 0.0f, 5.0f));
    LoadCeilingBiomes();
    LoadCeilingTextures();
    g_set.armsMode.store(std::clamp((int)IniF(L"arms", L"mode", 0.0f), 0, 2));
    g_set.hideHead.store(IniB(L"arms", L"hide_head", false));
    g_set.armScale.store(std::clamp(IniF(L"arms", L"scale", 1.0f), 0.3f, 3.0f));
    g_set.poseOrderLog.store(IniB(L"debug", L"pose_order", false));
    g_set.frameLog.store(IniB(L"debug", L"frame_log", false));
    g_set.findPrevMatrix.store(IniB(L"debug", L"find_prev_matrix", false));
    g_set.dlssPrevFix.store(IniB(L"render", L"dlss_prev_fix", false));
    g_set.dlssMv.store(std::clamp((int)IniF(L"render", L"dlss_mv", 2.0f), 0, 2));
    dlssmv::SetMode(g_set.dlssMv.load());
    // [render] dlss_mv 2: DLSS gets the same eye's jitter step and, in the far part, the camera's turn.
    // dlss_mv_sign: the camera's part's sign in the game's vectors (-1); dlss_mv_lag: the picture's view,
    // evaluations back (0); dlss_mv_far_turn: how much of the turn the far part gets (1);
    // dlss_mv_near: the near part redone from the eyes' parallax (off: it swam on grass); dlss_mv_jitter (on).
    dlssmv::SetSign(IniF(L"render", L"dlss_mv_sign", -1.0f));
    dlssmv::SetLag((int)IniF(L"render", L"dlss_mv_lag", 0.0f));
    dlssmv::SetFarTurn(IniF(L"render", L"dlss_mv_far_turn", 1.0f));
    dlssmv::SetFixes(IniB(L"render", L"dlss_mv_near", false), IniB(L"render", L"dlss_mv_jitter", true));
    g_set.phantomRay.store(IniB(L"debug", L"phantom_ray", false));
    g_set.boneAxes.store(IniB(L"debug", L"bone_axes", false));
    g_set.weaponDiag.store(IniB(L"debug", L"weapon_diag", false));
    {
        wchar_t sb[48] = {};
        GetPrivateProfileStringW(L"debug", L"shrink_bone", L"", sb, (DWORD)std::size(sb), g_iniPath);
        char nb[48] = {};
        WideCharToMultiByte(CP_UTF8, 0, sb, -1, nb, (int)sizeof nb - 1, nullptr, nullptr);
        memcpy(g_set.shrinkBone, nb, sizeof nb);
    }
    {   // [debug] matrix_writer: switched on = one 3 s watch of who writes the hero's matrix
        static bool was = false;
        const bool now = IniB(L"debug", L"matrix_writer", false);
        if (now && !was) StartMatrixWatch();
        was = now;
    }
    g_set.bodywalkPad.store(IniB(L"input", L"bodywalk_pad", true));
    g_set.aAttackOnly.store(IniB(L"input", L"a_attack_only", false));
    g_set.flatKeyMove.store(IniB(L"input", L"flat_keyboard_move", true));
    g_set.vrKeyWalk.store(IniB(L"input", L"vr_keys_walk", true));
    g_set.directWalk.store(IniB(L"input", L"direct_walk", true));
    g_set.flatNoPad.store(IniB(L"input", L"flat_no_pad", true));
    g_set.flatClickShoot.store(IniB(L"input", L"flat_click_shoot", true));
    g_set.flatCrosshair.store(IniB(L"input", L"flat_crosshair", true));
    g_set.inventoryPad.store(IniF(L"input", L"inventory_button", 0.0f) != 0.0f ? 1 : 0);
    g_set.facingSign.store(IniF(L"arms", L"facing_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.facingSource.store(IniF(L"hands", L"facing_source", 1.0f) != 0.0f ? 1 : 0);
    g_set.facingHoldMs.store(std::clamp(IniF(L"hands", L"facing_hold_ms", 4000.0f), 0.0f, 60000.0f));
    g_set.bodyTurn.store(std::clamp((int)IniF(L"body", L"turn", 2.0f), 0, 2));
    g_set.thirdTurn.store(IniB(L"body", L"turn_third", true));
    g_set.lockUpper.store(IniB(L"body", L"lock", true));
    g_set.legsUnder.store(IniB(L"body", L"legs_under_body", true));
    g_set.handUp.store(std::clamp(IniF(L"arms", L"up", 0.0f), -50.0f, 50.0f));
    g_set.handFwd.store(std::clamp(IniF(L"arms", L"forward", 0.0f), -50.0f, 50.0f));
    g_set.handSide.store(std::clamp(IniF(L"arms", L"side", 0.0f), -50.0f, 50.0f));
    g_set.handsFollowCam.store(std::clamp(IniF(L"arms", L"follow_camera", 100.0f), 0.0f, 100.0f) / 100.0f);
    g_set.bowMode.store(std::clamp((int)IniF(L"bow", L"mode", 4.0f), 0, 4));
    g_set.turnMs.store(std::clamp(IniF(L"bow", L"turn_ms", 80.0f), 20.0f, 400.0f));
    g_set.aimRange.store(std::clamp(IniF(L"bow", L"range", 20.0f), 3.0f, 60.0f));
    g_set.aimYawDeg.store(std::clamp(IniF(L"bow", L"aim_yaw", 0.0f), -30.0f, 30.0f));
    g_set.staffAimYawDeg.store(std::clamp(IniF(L"bow", L"staff_aim_yaw", 0.0f), -45.0f, 45.0f));
    {
        // its own 0 when unset: a crossbow shoots along the controller's -Z, not round a bow's grip
        g_set.xbowAimYawDeg.store(std::clamp(IniF(L"bow", L"xbow_aim_yaw", 0.0f), -45.0f, 45.0f));
    }
    g_set.xbowTwoHands.store(IniB(L"bow", L"xbow_two_hands", true));
    g_set.xbowHand.store(IniF(L"hands", L"xbow_hand", 1.0f) != 0.0f ? 1 : 0);   // the game hangs it on the left wrist (seen 2026-10-05)
    // +y: what the grip gave when the crossbow lay right (log 2026-10-05); taken again in
    // another pose it gave another axis and the model jumped a quarter turn
    g_set.xbowStockAxis.store(std::clamp((int)IniF(L"hands", L"xbow_stock_axis", 2.0f), -3, 3));
    g_set.xbowGunFrame.store(IniB(L"hands", L"xbow_gun_frame", true));
    g_set.xbowLine[0].store(std::clamp(IniF(L"hands", L"xbow_line_pitch", 34.0f), -180.0f, 180.0f));
    g_set.xbowLine[1].store(std::clamp(IniF(L"hands", L"xbow_line_yaw", 15.0f), -180.0f, 180.0f));
    g_set.xbowLine[2].store(std::clamp(IniF(L"hands", L"xbow_line_roll", 0.0f), -180.0f, 180.0f));
    g_set.xbowRangeOne.store(std::clamp(IniF(L"bow", L"xbow_range_one", 5.0f), 2.0f, 60.0f));
    g_set.xbowRangeTwo.store(std::clamp(IniF(L"bow", L"xbow_range_two", 30.0f), 2.0f, 60.0f));
    g_set.staffRangeOne.store(std::clamp(IniF(L"bow", L"staff_range_one", 5.0f), 2.0f, 60.0f));
    g_set.staffRangeTwo.store(std::clamp(IniF(L"bow", L"staff_range_two", 30.0f), 2.0f, 60.0f));
    g_set.bowHand.store(IniF(L"bow", L"set1_left", 1.0f) != 0.0f ? 1 : 0);
    g_set.bowHand2.store(IniF(L"bow", L"set2_left", 0.0f) != 0.0f ? 1 : 0);
    g_set.rangedLeft.store(IniB(L"bow", L"ranged_left", true));
    g_set.staffTwoHands.store(IniB(L"bow", L"staff_two_hands", true));
    g_set.staffHands.store(IniB(L"hands", L"follow_staff", true));
    g_set.staffFreeLeft.store(IniB(L"hands", L"staff_free_left", true));
    g_set.fist.store(IniB(L"hands", L"fist", true));
#if D2RVR_FIRST_PERSON
    for (int c = 0; c < 8; ++c) {
        wchar_t close[32], open[32];
        swprintf_s(close, L"%hs_close", skel::kHeroClasses[c]);
        swprintf_s(open, L"%hs_open", skel::kHeroClasses[c]);
        g_set.fistClose[c].store(std::clamp(IniF(L"fist", close, c == 3 ? 65.0f : 100.0f), 0.0f, 150.0f));
        g_set.fistOpen[c].store(std::clamp(IniF(L"fist", open, 20.0f), 0.0f, 150.0f));
    }
#endif
    for (int t = 1; t < D2RVR_TYPE_COUNT; ++t) {
        wchar_t sec[40] = L"weapon_";
        for (const char* c = kD2RVRWeaponTypeNames[t]; *c && wcslen(sec) < 38; ++c) { const size_t l = wcslen(sec); sec[l] = (wchar_t)tolower((unsigned char)*c); sec[l + 1] = 0; }
        static const wchar_t* const kKeys[6] = {L"x", L"y", L"z", L"pitch", L"yaw", L"roll"};
#if D2RVR_FIRST_PERSON
        // All 0. The crossbow's turn and shift it about the right controller's own axes
        // from its stock laid where the controller points (skeletons.cpp GunFrame).
#endif
        const float def[6] = {};
        for (int k = 0; k < 6; ++k) g_set.weaponAdj[t][k] = std::clamp(IniF(sec, kKeys[k], def[k]), k < 3 ? -40.0f : -180.0f, k < 3 ? 40.0f : 180.0f);
        static const wchar_t* const kLeft[3] = {L"left_x", L"left_y", L"left_z"};
        for (int k = 0; k < 3; ++k) g_set.weaponLeft[t][k] = std::clamp(IniF(sec, kLeft[k], 0.0f), -20.0f, 20.0f);
        if (t == 1) {
            static const wchar_t* const kHold[3] = {L"left_hold_x", L"left_hold_y", L"left_hold_z"};
            for (int k = 0; k < 3; ++k) g_set.leftHold[k].store(std::clamp(IniF(L"hands", kHold[k], 0.0f), -20.0f, 20.0f));
            static const wchar_t* const kTurn[3] = {L"left_hold_pitch", L"left_hold_yaw", L"left_hold_roll"};
            for (int k = 0; k < 3; ++k) g_set.leftHoldTurn[k].store(std::clamp(IniF(L"hands", kTurn[k], 0.0f), -180.0f, 180.0f));
        }
    }
    g_set.staffOffset[0].store(std::clamp(IniF(L"hands", L"staff_x", 0.0f), -40.0f, 40.0f));
    g_set.staffOffset[1].store(std::clamp(IniF(L"hands", L"staff_y", 0.0f), -40.0f, 40.0f));
    g_set.staffOffset[2].store(std::clamp(IniF(L"hands", L"staff_z", 0.0f), -40.0f, 40.0f));
    g_set.staffGrabCm.store(std::clamp(IniF(L"hands", L"staff_grab_cm", 15.0f), 3.0f, 100.0f));
    g_set.leftStaffOffset[0].store(std::clamp(IniF(L"hands", L"left_staff_x", 0.0f), -20.0f, 20.0f));
    g_set.leftStaffOffset[1].store(std::clamp(IniF(L"hands", L"left_staff_y", 0.0f), -20.0f, 20.0f));
    g_set.leftStaffOffset[2].store(std::clamp(IniF(L"hands", L"left_staff_z", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[0].store(std::clamp(IniF(L"hands", L"xbow_left_x", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[1].store(std::clamp(IniF(L"hands", L"xbow_left_y", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[2].store(std::clamp(IniF(L"hands", L"xbow_left_z", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftRoll.store(std::clamp(IniF(L"hands", L"xbow_left_roll", 0.0f), -180.0f, 180.0f));
    g_set.xbowLeftAtCm.store(std::clamp(IniF(L"hands", L"xbow_left_at_cm", 30.0f), 5.0f, 80.0f));
    g_set.polearmTwoHands.store(IniB(L"hands", L"polearm_two_hands", true));
    g_set.swordTwoHands.store(IniB(L"hands", L"sword_two_hands", true));
    g_set.swordLeftAtCm.store(std::clamp(IniF(L"hands", L"sword_left_at_cm", 10.0f), -30.0f, 30.0f));
    g_set.swordSlideCm.store(std::clamp(IniF(L"hands", L"sword_slide_cm", 4.0f), 0.0f, 20.0f));
    g_set.leftSwordOffset[0].store(std::clamp(IniF(L"hands", L"left_sword_x", 0.0f), -20.0f, 20.0f));
    g_set.leftSwordOffset[1].store(std::clamp(IniF(L"hands", L"left_sword_y", 0.0f), -20.0f, 20.0f));
    g_set.leftSwordOffset[2].store(std::clamp(IniF(L"hands", L"left_sword_z", 0.0f), -20.0f, 20.0f));
    g_set.spearHand.store(IniF(L"hands", L"spear_hand", 0.0f) != 0.0f ? 1 : 0);   // right: a halberd hung off the hand with 1 (2026-10-06)
    g_set.swordHand.store(IniF(L"hands", L"sword_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.axeHand.store(IniF(L"hands", L"axe_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.swordHiltAxis.store(std::clamp((int)IniF(L"hands", L"sword_hilt_axis", -2.0f), -3, 3));
    g_set.axeShaftAxis.store(std::clamp((int)IniF(L"hands", L"axe_shaft_axis", -2.0f), -3, 3));
    g_set.carryAxis.store(std::clamp((int)IniF(L"hands", L"carry_axis", 2.0f), -3, 3));
    if (g_set.carryAxis.load() == 0) g_set.carryAxis.store(2);
    g_set.carryCm.store(std::clamp(IniF(L"hands", L"carry_cm", 40.0f), 0.0f, 150.0f));
    g_set.bothAttach.store(IniB(L"hands", L"both_attach", true));
    g_set.staffHand.store(IniF(L"hands", L"staff_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.staffShaftAxis.store(std::clamp((int)IniF(L"hands", L"staff_shaft_axis", -2.0f), -3, 3));
    g_set.spearShaftAxis.store(std::clamp((int)IniF(L"hands", L"spear_shaft_axis", -2.0f), -3, 3));
    g_set.bodyYawDeg.store(std::clamp(IniF(L"body", L"yaw", 0.0f), -180.0f, 180.0f));
    g_set.wrist.store(IniB(L"hands", L"wrist", true));
    g_set.wristPitch.store(std::clamp(IniF(L"hands", L"pitch", 0.0f), -180.0f, 180.0f));
    g_set.wristYaw.store(std::clamp(IniF(L"hands", L"yaw", 0.0f), -180.0f, 180.0f));
    g_set.wristRoll.store(std::clamp(IniF(L"hands", L"roll", 0.0f), -180.0f, 180.0f));
    char b[256];
    snprintf(b, sizeof b, "vrcam settings: fov %.0f distance %.2f height %.1f pitch %.0f | head yaw %s sign %+.0f | stick rotate %s %s sign %+.0f squash %.2f angle x%.2f",
             g_set.fov.load(), g_set.distance.load(), g_set.height.load(), g_set.pitch.load(), g_set.headYaw.load() ? "on" : "off",
             g_set.yawSign.load(), g_set.stickRotate.load() ? "on" : "off", g_set.stickExact.load() ? "exact" : "approx",
             g_set.stickSign.load(), g_set.stickSquash.load(), g_set.stickAngleScale.load());
    Log(b);
}

// Re-read when the file's write time moves. Returns true if it did.
bool ReloadIfChanged() {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fa)) return false;
    if (CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) == 0) return false;
    g_iniTime = fa.ftLastWriteTime;
    LoadSettings();
    return true;
}

// State
std::atomic<bool> g_enabled{false};
std::atomic<float> g_mouseYaw{0.0f}, g_mousePitch{0.0f};   // the mouse's turn on top of the head (deg)
std::atomic<float> g_recenter{0.0f};    // head yaw that counts as straight ahead
std::atomic<float> g_recenterPitch{0.0f}; // head pitch that counts as level (F11)
std::atomic<float> g_recenterRoll{0.0f};  // head roll that counts as upright (F11)
std::atomic<float> g_turnYaw{0.0f};     // body turn from the right stick; F11 keeps it
// Held for a pair's two passes (HookDrawGameScreen): the stick and the mouse
// move on a 10 ms timer, and between the passes the eyes got different turns.
std::atomic<float> g_heldYaw{0.0f}, g_heldPitch{0.0f};
extern std::atomic<bool> g_pairNow;
bool NativeView();
std::atomic<float> g_rightX{0.0f};      // last right stick X the game polled, -1..1
// When a poll last got BodyWalk's own pad (PadFromBodyWalk). Some polls of pad 0
// came back without it (the pad trace flipped 2000 / 0000 within a millisecond)
// and wrote the right stick back to 0 between BodyWalk's: the body did not turn
// in F4 (2026-10-07). While BodyWalk's pad is live only its polls set what
// TurnStick keeps.
std::atomic<ULONGLONG> g_padMirrorOkAt{0};
std::atomic<float> g_camYaw{0.0f};
std::atomic<bool> g_facingReset{false};   // F11: forget the hero-facing candidates, search again      // the yaw last folded into the view; the stick turns by this
std::atomic<uint32_t> g_gen{1};

// D2R Bridge
HANDLE g_map = nullptr;
const D2RVR_Shared* g_shared = nullptr;

bool HeadYaw(float* yaw) {
    const D2RVR_Shared* s = g_shared;
    if (!s || s->version != D2RVR_SHARED_VERSION || !s->headValid) return false;
    *yaw = s->headYawDeg;
    return std::isfinite(*yaw);
}

float WrapDeg(float a) { return std::remainder(a, 360.0f); }   // into -180..180

// The head between BodyWalk's updates. They come ~100 times a second, the
// game draws up to 240: frames in between got the same angles, the camera
// turned in steps, and each step held for two frames read as a double image
// in a head turn. Each angle now goes on at the speed of the last two updates
// for up to 15 ms past the latest ([head] predict=0: as sent).
struct HeadTrack { uint32_t counter; double t; float v[3], vel[3]; bool ok; };
SRWLOCK g_headLock = SRWLOCK_INIT;
HeadTrack g_head{};

double NowSeconds() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

// A pair is being drawn from one game frame (HookPrismBlit): both eyes take the
// head as sent, never carried on - the second eye is drawn later, and would
// otherwise get a head a few milliseconds further along than the first.
std::atomic<bool> g_pairNow{false};

// yaw, pitch, roll as sent (degrees), advanced to now.
void PredictHead(const D2RVR_Shared* s, float out[3]) {
    float raw[3] = {s->headYawDeg, s->pitchValid ? s->headPitchDeg : 0.0f, s->rollValid ? s->headRollDeg : 0.0f};
    for (float& v : raw) if (!std::isfinite(v)) v = 0.0f;   // one NaN kept in the track would stay there for good
    if (!g_set.headPredict.load() || g_pairNow.load()) { memcpy(out, raw, sizeof raw); return; }
    const double now = NowSeconds();
    AcquireSRWLockExclusive(&g_headLock);
    HeadTrack& h = g_head;
    if (!h.ok) { h = {s->counter, now, {raw[0], raw[1], raw[2]}, {0, 0, 0}, true}; }
    else if (s->counter != h.counter) {
        const double dt = now - h.t;
        for (int i = 0; i < 3; ++i) {
            const float v = dt > 0.002 && dt < 0.1 ? WrapDeg(raw[i] - h.v[i]) / (float)dt : 0.0f;
            h.vel[i] = 0.5f * h.vel[i] + 0.5f * v;   // a little smoothing: arrival times jitter by a frame
            h.v[i] = raw[i];
        }
        h.counter = s->counter; h.t = now;
    }
    const float ahead = (float)std::min(now - h.t, 0.015);
    for (int i = 0; i < 3; ++i) out[i] = h.v[i] + h.vel[i] * ahead;
    ReleaseSRWLockExclusive(&g_headLock);
}

// The yaw the camera should have now: head (if on) plus the mouse's extra.
float TargetYaw() {
    float yaw = g_pairNow.load() ? g_heldYaw.load() : g_mouseYaw.load() + g_turnYaw.load(), head = 0.0f;
    // Native OpenXR (F4/F2 drawn with the runtime's eyes): the head yaw those eyes were drawn with,
    // from xr's own recentre - the native view's own turn. Not D2RVR_Shared's: in a pair that is the
    // room's raw yaw, against g_recenter from whichever block was there at the last F11 - the body
    // stood ~100 deg off the view at game start until a lucky F11 (2026-10-10).
    if (float rel = 0.0f; NativeView() && xr::PairHeadYaw(&rel)) return WrapDeg(yaw - rel * 57.2957795f);
    if (g_set.headYaw.load() && HeadYaw(&head)) {
        float p[3]; PredictHead(g_shared, p);
        yaw += g_set.yawSign.load() * g_set.yawScale.load() * WrapDeg(p[0] - g_recenter.load());
    }
    return WrapDeg(yaw);
}

// Roll about the line of sight from the head tilting sideways, 0 when off.
float TargetRoll() {
    const D2RVR_Shared* sh = g_shared;
    if (!g_set.headRoll.load() || !sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !sh->rollValid || !std::isfinite(sh->headRollDeg))
        return 0.0f;
    float p[3]; PredictHead(sh, p);
    return std::clamp(g_set.rollSign.load() * (p[2] - g_recenterRoll.load()), -80.0f, 80.0f);
}

// Pitch relative to the game's own steep camera: ini level point, the mouse's
// extra, and the head looking up (more negative) or down. Kept short of the
// vertical, where the rotation would flip.
float TargetPitch() {
    float pitch = g_set.pitch.load() + (g_pairNow.load() ? g_heldPitch.load() : g_mousePitch.load());
    const D2RVR_Shared* sh = g_shared;
    if (g_set.headPitch.load() && sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid && sh->pitchValid && std::isfinite(sh->headPitchDeg))
    {
        float p[3]; PredictHead(sh, p);
        pitch -= g_set.pitchSign.load() * (p[1] - g_recenterPitch.load());
    }
    return std::clamp(pitch, -150.0f, 60.0f);
}

// What the stick needs, taken where the view is folded: our camera's right and
// forward flattened onto the ground, and the game's own camera's screen axes
// in the world. The game reads its stick as a direction on ITS screen.
struct StickFrame { float camR[3], camF[3], scrR[3], scrU[3], mirX, mirY; float yawAtBuild; bool valid; };   // yawAtBuild: TargetYaw this view was built with
SRWLOCK g_stickLock = SRWLOCK_INIT;
StickFrame g_stickFrame{};


// F12 goes round: off, first person, third person, off. Third person is the
// same head-driven camera moved back ([third] distance), with the hero whole:
// his head shown, his body and arms the game's own animation.
std::atomic<int> g_view{1};   // while on: 1 first person, 2 third person
bool ThirdPerson() { return g_view.load() == 2; }
// [mode]: the view the Home page, F1..F4 or F12 picked (Settings::platform).
int ViewNow() { return g_set.platform.load() == 1 ? g_set.vrView.load() : g_set.flatView.load(); }
#if D2RVR_FIRST_PERSON
int ViewCount() { return g_set.platform.load() == 1 ? 4 : 3; }
#else
int ViewCount() { return g_set.platform.load() == 1 ? 3 : 2; }   // F4 and flat F3 are first person: coming soon
#endif
// VR first person with the body on the player's: arms on the controllers, the
// trunk held - everything that was here before the Home page.
#if D2RVR_FIRST_PERSON
bool FullBody() { return g_set.platform.load() == 1 && g_set.vrView.load() == 4; }
#else
bool FullBody() { return false; }
#endif
// First person whose hero the game animates (flat, or VR view 3): turned only
// about the vertical, mouse and W A S D, the interface in the picture.
#if D2RVR_FIRST_PERSON
bool InsideFree() { return g_enabled.load() && !ThirdPerson() && !FullBody(); }   // flat F3 only: VR has no such view since 2026-10-05
#else
bool InsideFree() { return false; }
#endif
// Native OpenXR ([openxr] on, vr/xr.cpp) draws these views as the headset's own eyes: first
// person (F4) and third person (F2). The others go to the headset as a flat picture.
bool NativeView() { return g_set.platform.load() == 1 && (FullBody() || (ThirdPerson() && g_set.vrView.load() == 2)); }
// [openxr] third_gaze: in F2 the camera swings round the hero with the gaze (1, as on FlatVR's
// screen) or stays behind him on the body's turn while the head looks round (0, default).
std::atomic<bool> g_xrThirdGaze{false};
// VR view 5: the game laid on the room's floor and looked at from above - our
// own camera where the head is in the room ([table] hero_cm, TableCamera), the
// hero whole as the game animates him, the void black for the headset to key.
bool TableView() { return g_set.platform.load() == 1 && g_set.vrView.load() == 3; }
// VR view 1 with [top] perspective: the game's view from above, but through our
// camera - the FlatVR screen's field of view and real stereo (TopCamera).
bool TopPersp() { return g_set.platform.load() == 1 && g_set.vrView.load() == 1 && g_set.topPersp.load(); }
// Our camera looking down on a hero the game animates whole, turned by nobody: F1 in perspective and the floor.
std::atomic<float> g_topUnitsPerM{0.0f};   // F1 in perspective: world units per metre on the screen's plane (TopCamera), 0 = not known yet
bool Overhead() { return TableView() || TopPersp(); }
#if D2RVR_FIRST_PERSON
int ArmsMode() { return FullBody() ? 2 : g_set.armsMode.load() == 1 ? 1 : 0; }   // 1 = the test pose, kept for debugging
#else
int ArmsMode() { return 0; }
#endif
// Mouse look: the flat views and VR view 3 are played with the mouse; the rest as the ini says.
bool MouseLookForView() {
    if (g_set.platform.load() == 0) return ViewNow() != 1;
    if (Overhead()) return false;   // from above and the floor: the head and the pad
    // VR F2 with vr_keys_walk is flat's F2: it starts with mouse look (F9 frees the pointer).
    if (ViewNow() == 2 && g_set.vrKeyWalk.load()) return true;
    return g_set.mouseLook.load();
}
// The interface in the picture (not taken out for FlatVR), per view.
bool ClassicNow() {
    if (g_set.platform.load() == 0) return true;   // no FlatVR to take it
    if (!g_enabled.load() || TopPersp()) return g_set.classicTop.load() == 0;
    if (TableView()) return g_set.classicFloor.load() == 0;   // before ThirdPerson: ApplyView runs the floor as view 2
    if (ThirdPerson()) return g_set.classicThird.load() == 0;
    return !FullBody();
}
float BarNearNow() {
    if (!g_enabled.load() || TopPersp()) return g_set.barNearTop.load();
    if (TableView()) return g_set.barNearFloor.load();
    if (ThirdPerson()) return g_set.barNearThird.load();
    return FullBody() ? 0.0f : g_set.barNearInside.load();
}
// The toolbar's size in the picture, from above, from behind and on the floor (the game's own is 1).
float BarSizeNow() {
    if (!g_enabled.load() || TopPersp()) return g_set.barSizeTop.load();
    if (TableView()) return g_set.barSizeFloor.load();
    if (ThirdPerson()) return g_set.barSizeThird.load();
    return 1.0f;
}
// ... and where on the screen, uv from where the game puts it (+x right, +y down).
void BarOffsetNow(float* x, float* y) {
    *x = *y = 0.0f;
    if (!g_enabled.load() || TopPersp()) { *x = g_set.barXTop.load(); *y = g_set.barYTop.load(); }
    else if (TableView()) { *x = g_set.barXFloor.load(); *y = g_set.barYFloor.load(); }
    else if (ThirdPerson()) { *x = g_set.barXThird.load(); *y = g_set.barYThird.load(); }
}
float ViewDistance() { return ThirdPerson() ? g_set.thirdDistance.load() : g_set.distance.load(); }
// Model units to world: the hero's model-to-world matrix scales by ~0.93
// (FreshYawInModel keeps the live value).
std::atomic<float> g_heroScale{0.93f};
// First person: the eyes where the hero's are standing - his rig's eyeballs in
// the bind pose (skel::HeroEye), so a sorceress sees from 5.3 units, a druid
// from 6.5, and the world's scale (TrueScale) follows. Without the hero's pose
// seen yet, or with [camera] height_auto=0, the ini's height.
float ViewHeight() {
    if (ThirdPerson()) return g_set.thirdHeight.load();
    float eye = 0.0f, lift = 0.0f;
    if (g_set.heightAuto.load() && skel::HeroEye(&eye, &lift))
        return std::max(1.0f, eye * g_heroScale.load() + g_set.eyeOffset.load());
    return g_set.height.load();
}
// How far a jump lifts the eyes now, world units: on top of ViewHeight in the
// view only - the world's scale must not change in mid-air.
float JumpLift() {
    float eye = 0.0f, lift = 0.0f;
    if (ThirdPerson() || !g_set.followJump.load() || !skel::HeroEye(&eye, &lift)) return 0.0f;
    return lift * g_heroScale.load();
}

std::atomic<float> g_eyeWorld[3];   // the camera in the world (third person: its pivot), last view rebuild - the hands hang off it
// F3: our camera over the game's ground, for the mouse pointer laid on it (FloorPointerTick) - the eyes this
// high above the hero's ground (world units), and the world-up (y) parts of the view's forward and up.
std::atomic<float> g_floorEyeH{0.0f}, g_floorFwdY{0.0f}, g_floorUpY{0.0f};
std::atomic<float> g_viewFwdY{0.0f};   // every view: the world-up part of where the camera looks (- = down), for the F2 crosshair
std::atomic<bool> g_floorCamOk{false};
// ... and the projection it is drawn with (VrProj): M[0], M[5], the eyes apart and zero parallax, world units.
std::atomic<float> g_projSx{0.0f}, g_projSy{0.0f}, g_stereoIpd{0.0f}, g_stereoConv{0.0f};
// The off-axis shift of the left eye's projection (M[8]; the right eye's is minus it), 0 without:
// the replayed right eye has only the left pass's projection, moved by twice this.
std::atomic<float> g_projShift{0.0f};
// [stereo] replay_proj: 0 off, 1 the right eye's off-axis projection (+ its inverse), 2 without
// the inverse, 3 (default) also the eye's shift folded into the projection instead of the view:
// the objects come with their place already in the left eye's view space (per-object constants,
// not replayed) - a moved view left them where they were, the right eye showed them as the left.
std::atomic<int> g_projFix{3};
std::atomic<float> g_shiftMul{1.0f};
// [stereo] replay_views (8 = all): how many of a camera buffer's view matrices the right eye
// moves - two passes a frame move all five of D2R's (cb_diff over the whole 4 KB, 2026-10-09).
std::atomic<int> g_replayViews{8};
// [stereo] replay_view_mask (testing; 0 = by replay_views): bit k on = the k-th view found in a
// camera buffer is moved for the right eye
std::atomic<int> g_viewMask{0};
// [stereo] replay_positions: camera positions found on their own, moved for the right eye -
// 2 (default) only in buffers with one (two passes a frame: those differ between the eyes - the
// camera position at words 4 / 68 - while a buffer with four, one a camera record at words 112,
// 304, 496, 688, is the same for both: moved, the right eye's lighting and shadows went wrong),
// 1 all, 0 none
std::atomic<int> g_replayPositions{2};
// [stereo] replay_prev (1): the last frame's view*projection each camera record keeps 96 words
// after its view (what the game's temporal filter reprojects its history with) made the right
// eye's in BOTH eyes. The replay draws the right eye last with the same lists: the history both
// eyes read is the last pair's right picture, and the left eye's matrix put it beside itself -
// speckled skin and doubled hair in the right eye (2026-10-09).
std::atomic<int> g_replayPrev{1};
   // [stereo] replay_shift_mul (testing): the right eye's shift times this
std::atomic<uint32_t> g_nFold{0}, g_nView{0}, g_nPos{0};   // patched a pair: folded into the projection, the view moved, positions
std::atomic<uint32_t> g_viewBuilds{0};   // world camera view rebuilds so far (pose-order diagnostics)
// The eye point less the camera's look-at (height, side, forward), from the last
// view rebuild. The skeleton adds it to the look-at as it is at the pose: on the
// first pass after the game moves on, the look-at has moved since the last
// rebuild, and the hands hung off the old eye trailed a walk in the left eye.
std::atomic<float> g_eyeFromLook[3];
std::atomic<bool> g_eyeOk{false};

// Attack along the hand (bow mode 2), see AimByMouse.
std::atomic<bool> g_aimRay{false};
std::atomic<ULONGLONG> g_aimRayUntil{0};
std::atomic<float> g_aimO[3], g_aimD[3];
bool g_aimDown = false;
bool GameFocused();

bool AfrOn();
extern std::atomic<int> g_eye;
// Each eye's last four view stamps, newest at g_stampHistAt: the one written
// into a presented frame is [stereo] pipeline_depth views back - the engine
// may present a frame one or two of its eye's views after it was built, which
// leaves the right eye but the wrong moment (cyberpunk-vr-port's
// EnginePipelineDepth: "judder that survives a perfectly paired pose ring").
uint32_t g_stampHist[2][4] = {};
int g_stampHistAt[2] = {};
std::atomic<float> g_lastNear{1.5f};    // the near plane last written; the fog turns depth into distance with it

// What the sky needs to turn a pixel into a direction, per eye: the projection
// (M[0], M[5], M[8], M[9]) and the camera's axes in the world (right, up, back).
// ReShade runs on the frame being presented, so it picks the eye the FlatVR
// addon files that frame under. Only ever read as a whole under the lock.
// eyeRel: the eye less the hero, world units; hero: the hero (his feet) in the world, the same view build
struct SkyView { float proj[4]; float axes[9]; float eyeRel[3]; float hero[3]; bool proj_ok, axes_ok; };
// The game on the floor's bounds (D2R_DepthFog.fx TableKey): the ground the game's
// own camera would show. Its view x projection and the hero, from the same view build.
struct TableBox { float vp[16]; float hero[3]; bool ok; };
TableBox g_tableBox{};   // under g_skyLock
SRWLOCK g_skyLock = SRWLOCK_INIT;
SkyView g_skyView[2]{};
// Each eye's last four views as the sky saw them, newest at g_skyHistAt (pushed when a view's
// projection is in, the last of its parts): the sky is drawn [stereo] pipeline_depth views
// back, as the frame stamp is - the picture presented is that older view, and the sky by the
// newest one shook against the world once the stamp was right (2026-10-09).
SkyView g_skyHist[2][4]{};
int g_skyHistAt[2] = {};
SkyView SkyBack(int e);   // below, by g_set

// The FlatVR screen's angular height, from BodyWalk's own settings file: the
// game camera's vertical FOV set to it maps the picture onto the screen one to
// one - the world at its true angular size, the hands where the real ones are -
// the way an OpenXR game's view simply is. Width = 3 m x flat_vr_scale (FlatVR's
// reference screen), height = width / the picture's aspect, at flat_vr_distance;
// a cylinder bends sideways only, so its height angle is the flat one.
std::atomic<float> g_flatFov{0.0f};     // 0 = not known
std::atomic<float> g_lastAspect{16.0f / 9.0f};

bool JsonNumber(const std::string& text, const char* key, double* v) {
    const std::string k = std::string("\"") + key + "\"";
    size_t i = text.find(k);
    if (i == std::string::npos) return false;
    i = text.find(':', i + k.size());
    if (i == std::string::npos) return false;
    const char* s = text.c_str() + i + 1;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    if (!strncmp(s, "true", 4)) { *v = 1; return true; }
    if (!strncmp(s, "false", 5)) { *v = 0; return true; }
    char* end = nullptr;
    *v = strtod(s, &end);
    return end != s;
}

void UpdateFovFromFlatVR() {
    if (!g_set.fovFromFlatVR.load()) { g_flatFov.store(0.0f); return; }
    wchar_t path[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n >= MAX_PATH - 40) return;
    wcscat_s(path, L"\\BodyWalkVR\\usersettings.json");
    static FILETIME seen{};
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return;
    static float seenAspect = 0.0f;
    const float aspect = g_lastAspect.load();
    if (CompareFileTime(&fa.ftLastWriteTime, &seen) == 0 && fabsf(aspect - seenAspect) < 1e-3f) return;
    seen = fa.ftLastWriteTime; seenAspect = aspect;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string text(fa.nFileSizeLow, '\0');
    DWORD got = 0;
    const bool ok = ReadFile(h, text.data(), (DWORD)text.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok) return;
    text.resize(got);
    double scale = 1.0, dist = 5.0, locked = 0.0;
    if (!JsonNumber(text, "flat_vr_scale", &scale) || !JsonNumber(text, "flat_vr_distance", &dist) || !(dist > 0.1) || !(scale > 0.05)) return;
    JsonNumber(text, "flat_vr_head_locked", &locked);
    // The match below is a flat screen's. A curved, sphere or fisheye one
    // bends the picture on top: the middle magnified, the edges swimming on
    // head turns - it reads as "the FOV does not follow the screen".
    double shape = 0.0, strength = 0.0, curv = 0.0;
    JsonNumber(text, "flat_vr_screen_shape", &shape);
    JsonNumber(text, "flat_vr_shape_strength", &strength);
    JsonNumber(text, "flat_vr_curvature", &curv);
    static const char* const kShapes[4] = {"flat", "curved", "sphere", "fisheye"};
    const int sh = std::clamp((int)shape, 0, 3);
    const double bend = sh == 1 ? curv : sh >= 2 ? strength : 0.0;
    if (sh != 0 && bend > 0.001)
        LogF("vrcam: FlatVR screen shape is %s (%.2f) - the game camera's FOV matches a FLAT screen only; set Screen Shape = Flat in FlatVR", kShapes[sh], bend);
    const double width = 3.0 * scale, height = width / std::max(0.2f, aspect);
    const float fov = (float)(2.0 * atan(height * 0.5 / dist) * 57.29577951);
    g_flatFov.store(std::clamp(fov, 20.0f, 140.0f));
    LogF("vrcam: FlatVR screen %.2f m wide at %.2f m (%s) -> game camera FOV %.1f deg%s", width, dist, locked ? "head-locked" : "NOT head-locked", fov,
         locked ? "" : " - the camera follows the head, so the screen should too (FlatVR: head-locked)");
}

// The screen as FlatVR shows it right now (FlatVRScreenGeom, written every
// FlatVR frame): width AND height, so a screen of any aspect - the picture's
// own, a crop - gets a frustum that is exactly it. Taken while its counter
// moves; an older FlatVR, or one not running, leaves the usersettings path.
// KeepBlockName: a block BodyWalk or FlatVR makes (screen size, pad, the bridge's
// actions) is opened once and its HANDLE kept, not only the view. A section loses
// its name when its last handle closes: with BodyWalk restarted while the game
// ran, the new BodyWalk made new blocks and this kept reading the dead ones - the
// D2R: Map action never arrived (2026-10-04; FlatVR had the same with the AFR flag).
const FlatVRScreenGeom* g_geom = nullptr;

bool LiveScreen(float* w, float* h, float* d) {
    if (!g_geom) {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 1000) return false;
        lastTry = GetTickCount64();
        if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, FLATVR_SCREEN_GEOM_NAME)) {
            g_geom = (const FlatVRScreenGeom*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(FlatVRScreenGeom));
            if (!g_geom) CloseHandle(m);   // else kept: see KeepBlockName
        }
        if (!g_geom) return false;
    }
    static uint32_t seen = 0;
    static ULONGLONG movedAt = 0;
    const uint32_t c = g_geom->counter;
    if (c != seen) { seen = c; movedAt = GetTickCount64(); }
    if (g_geom->version != FLATVR_SCREEN_GEOM_VERSION || GetTickCount64() - movedAt > 1000) return false;
    *w = g_geom->width_m; *h = g_geom->height_m; *d = g_geom->distance_m;
    if (!(*w > 0.05f && *h > 0.05f && *d > 0.1f && *w < 100.0f && *h < 100.0f && *d < 100.0f)) return false;
    // what is seen goes to the log when it changes
    static float told[3] = {};
    static uint32_t toldShape = ~0u;
    if (fabsf(*w - told[0]) > 0.01f * told[0] || fabsf(*h - told[1]) > 0.01f * told[1] || fabsf(*d - told[2]) > 0.01f * told[2]) {
        told[0] = *w; told[1] = *h; told[2] = *d;
        LogF("vrcam: FlatVR screen (live) %.2f x %.2f m at %.2f m%s -> game camera FOV %.1f x %.1f deg", *w, *h, *d,
             g_geom->head_locked ? "" : " - NOT head-locked: the camera follows the head, so the screen should too",
             2.0f * atanf(0.5f * *w / *d) * 57.2957795f, 2.0f * atanf(0.5f * *h / *d) * 57.2957795f);
    }
    if (g_geom->shape != toldShape) {
        toldShape = g_geom->shape;
        static const char* const kShapes[4] = {"flat", "curved", "sphere", "fisheye"};
        if (g_geom->shape != 0 && g_geom->bend > 0.001f)
            LogF("vrcam: FlatVR screen shape is %s (%.2f) - the game camera matches a FLAT screen only; set Screen Shape = Flat in FlatVR",
                 kShapes[std::min(g_geom->shape, 3u)], g_geom->bend);
    }
    return true;
}

// The user's eyes above the floor standing: from the height set in BodyWalk
// (Avatar > Body Height, handed over by the bridge), else [stereo]
// user_height_m; an adult's eyes sit at ~0.936 of the height. Seated or not:
// it is the scale, not where the head is now.
float UserEyeHeightM() {
    const D2RVR_Shared* sh = g_shared;
    if (sh && sh->version == D2RVR_SHARED_VERSION && sh->userHeightMm >= 1100 && sh->userHeightMm <= 2500)
        return 0.936f * sh->userHeightMm * 0.001f;
    return g_set.eyeHeightM.load();
}

// The world at its own size ([stereo] true_scale). The screen hangs d metres
// ahead; whatever the pair puts at zero parallax is seen ON it. With a fixed
// convergence of 15 world units (~3.6 m) on a screen 0.76 m away, everything
// came out ~5 times too near and too small, and the hands moved more than the
// real ones against it. So: zero parallax at the screen's own distance and
// the eyes the user's own distance apart, both in world units - units per
// metre being the eye height in the game over the head height in the room,
// the same scale the hands are hung by. The frustum already is the screen
// (LiveScreen), so this makes the view orthostereo.
bool TrueScale(float* ipd, float* conv) {
    float w, h, d;
    if (!g_set.trueScale.load() || !g_set.fovFromFlatVR.load() || !LiveScreen(&w, &h, &d)) return false;
    // The user's STANDING eye height, not the head as it is now: seated, the
    // head is lower, and the world and the hands came out scaled for a child.
    // The table view has its own scale: the game laid on the floor at that size.
    const float headM = UserEyeHeightM();
    const float upm = TableView() ? g_set.tableScale.load() : TopPersp() ? g_topUnitsPerM.load() : ViewHeight() / headM;
    if (!(upm > 0.5f && upm < 50.0f)) return false;
    *ipd = g_set.eyeMm.load() * 0.001f * upm;
    *conv = d * upm;
    static float told[2] = {};
    if (fabsf(*ipd - told[0]) > 0.02f * told[0] || fabsf(*conv - told[1]) > 0.02f * told[1]) {
        told[0] = *ipd; told[1] = *conv;
        LogF("vrcam: world 1:1 - %.2f units per metre (eyes %.1f units over a %.2f m head): eyes %.3f units apart, zero parallax at %.2f units (the screen's %.2f m)",
             upm, ViewHeight(), headM, *ipd, *conv, d);
    }
    return true;
}

float CameraFov() {
    const float f = g_flatFov.load();
    return g_set.fovFromFlatVR.load() && f > 0.0f ? f : g_set.fov.load();
}

// Frame log ([debug] frame_log): every pair, view build, hero pose and
// present as one CSV line, stamped with QueryPerformanceCounter in
// microseconds - the clock every process shares, so FlatVR's own per-frame
// trace (flat_vr_pose_trace.csv, switched on by the flag file this writes)
// lines up with it. Lines collect in memory; the timer thread writes them out.
namespace flog {
std::atomic<bool> g_on{false};
std::atomic<ULONGLONG> g_diagUntil{0};   // [debug] diag_go: the frame log runs until then
void StartDiag();
SRWLOCK g_lock = SRWLOCK_INIT;
std::string g_buf;
constexpr uint64_t kDrawCounterRva = 0x33ED6D8, kFrameTimeRva = 0x27D31D0;   // as RVA_DRAW_COUNTER / RVA_FRAME_TIME below

double UsNow() {
    static const double k = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1e6 / (double)f.QuadPart; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * k;
}
uint32_t Pass() { uint32_t v = 0; SafeRead(&v, (void*)d2rsig::Addr(kDrawCounterRva), 4); return v; }
float Dt() { float v = -1.0f; SafeRead(&v, (void*)d2rsig::Addr(kFrameTimeRva), 4); return v; }

void Line(const char* fmt, ...) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    char b[768];
    int n = snprintf(b, sizeof b, "%.0f,", UsNow());
    va_list a; va_start(a, fmt);
    const int m = vsnprintf(b + n, sizeof b - n, fmt, a);
    va_end(a);
    n = std::min<int>(n + std::max(m, 0), (int)sizeof b - 1);
    AcquireSRWLockExclusive(&g_lock);
    if (g_buf.size() < (16u << 20)) { g_buf.append(b, n); g_buf += '\n'; }
    ReleaseSRWLockExclusive(&g_lock);
}

std::wstring FlagPath() {
    wchar_t p[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH);
    if (!n || n >= MAX_PATH - 60) return L"";
    return std::wstring(p) + L"\\BodyWalkVR\\flat_vr_pose_trace.on";
}

// The timer thread: open / close with the setting, write what collected.
void Tick(bool want) {
    static FILE* f = nullptr;
    static size_t written = 0;
    if (want && !f) {
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_iniPath);
        if (wchar_t* slash = wcsrchr(path, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"d2r_vr_frames.csv");
        f = _wfsopen(path, L"wb", _SH_DENYWR);
        if (!f) return;
        written = 0;
        fputs("# t_us = QueryPerformanceCounter in microseconds (FlatVR's flat_vr_pose_trace.csv uses the same clock)\n"
              "# S: t_us,S,pass,dt,heldYaw,headYaw,headPitch,headRoll,sampleStamp          - a pair begins (head and turn held for both passes)\n"
              "# V: t_us,V,pass,dt,eye,targetYaw,camYaw,lookX,lookZ,stamp                  - a world view built\n"
              "# P: t_us,P,pass,dt,eye,matrix,rawX,rawZ,rawYaw,lookX,lookZ,usedYaw,bodyYaw,eyeX,eyeY,eyeZ,result,logicFacing,logicAgeMs - a hero pose\n"
              "# R: t_us,R,pass,eye,stamp                                                    - ReShade on a present\n"
              "# C: t_us,C,pass,eye,addr:yaw,...                                             - every hero matrix copy at the pose (address: low 20 bits)\n"
              "# Y: t_us,Y,pass,replayed,waitPrev,waitRestore,holdLeft,waitLastPair,camera,submit,presentR,copyL,presentL - a one-pass pair's steps, us\n", f);
        if (const std::wstring flag = FlagPath(); !flag.empty())
            if (FILE* t = _wfopen(flag.c_str(), L"wb")) fclose(t);
        g_on.store(true);
        Log("vrcam: frame log ON - d2r_vr_frames.csv beside the ini; FlatVR writes flat_vr_pose_trace.csv alongside");
    }
    if (!f) return;
    std::string out;
    AcquireSRWLockExclusive(&g_lock); out.swap(g_buf); ReleaseSRWLockExclusive(&g_lock);
    if (!out.empty()) { fwrite(out.data(), 1, out.size(), f); written += out.size(); }
    if (!want || written > (300u << 20)) {
        g_on.store(false);
        AcquireSRWLockExclusive(&g_lock); out.clear(); out.swap(g_buf); ReleaseSRWLockExclusive(&g_lock);
        if (!out.empty()) fwrite(out.data(), 1, out.size(), f);
        fclose(f); f = nullptr;
        if (const std::wstring flag = FlagPath(); !flag.empty()) DeleteFileW(flag.c_str());
        Log(want ? "vrcam: frame log stopped at 300 MB" : "vrcam: frame log OFF");
    } else fflush(f);
}
}  // namespace flog

// Settings' Collect logs: the frame log for 10 s and the state it ran in, once in the log.
bool AfrOn();
extern std::atomic<bool> g_inWorld;
namespace replay { extern std::atomic<int> g_want; extern std::atomic<bool> g_on; extern std::atomic<int> g_ahead; extern std::atomic<uint32_t> g_replays; }
void flog::StartDiag() {
    g_diagUntil.store(GetTickCount64() + 10000);
    LogF("vrcam: diagnostic capture (D2R VR Settings, Collect logs): 10 s of d2r_vr_frames.csv - stereo %d, pair per frame %d, "
         "replay want %d on %d ahead %d (pairs replayed so far %u), pace %d at %d Hz, pipeline depth %d, stamps %d, in world %d",
         AfrOn() ? 1 : 0, IniB(L"stereo", L"pair_per_tick", false) ? 1 : 0, replay::g_want.load(), replay::g_on.load() ? 1 : 0,
         replay::g_ahead.load(), replay::g_replays.load(), g_set.pace.load() ? 1 : 0, g_set.headsetHz.load(),
         g_set.pipelineDepth.load(), g_set.stamps.load() ? 1 : 0, g_inWorld.load() ? 1 : 0);
}


// Alternate-frame stereo: the eye flips once a game frame, and the FlatVR addon
// (same process) is told which, so it files each presented frame into its half
// of a side-by-side pair. See afr_eye_shared.h.
HANDLE g_afrMap = nullptr;
FlatVRAfrEye* g_afrBlock = nullptr;
std::atomic<int> g_eye{0};   // also declared above BuildProj

void OpenAfrBlock() {
    if (g_afrBlock) return;
    g_afrMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FlatVRAfrEye), FLATVR_AFR_EYE_NAME);
    if (g_afrMap) g_afrBlock = (FlatVRAfrEye*)MapViewOfFile(g_afrMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FlatVRAfrEye));
    if (g_afrBlock) { memset(g_afrBlock, 0, sizeof *g_afrBlock); g_afrBlock->version = FLATVR_AFR_EYE_VERSION; }
}

extern std::atomic<bool> g_inWorld;
// The game's own view from above in stereo: F12 off, in a game area.
bool TopStereoOn() { return !g_enabled.load() && g_set.topStereo.load() && g_set.afr.load() && g_inWorld.load(); }
// Only in a game area: the menus have no world camera, so nothing flips the
// eye there, and a pair announced from the menu was one fresh half and one frozen.
bool AfrOn() { return g_set.afr.load() && g_inWorld.load() && (g_enabled.load() || TopStereoOn()); }
// From above and behind: the toolbar moved nearer or farther - only real stereo has a depth to move it in.
bool BarNearWanted() { return AfrOn() && ClassicNow() && std::abs(BarNearNow()) > 0.01f; }
// The map on the floor (F3) nearer or farther in stereo ([hud_floor] map_near), as the toolbar.
bool MapNearWanted() { return AfrOn() && ClassicNow() && TableView() && std::abs(g_set.mapNearFloor.load()) > 0.01f; }
// VR F3, the game on the floor, with our camera on (ClassicNow's order: off is F1 whatever the view).
bool FloorView() { return g_enabled.load() && TableView(); }
// The item labels' box opacity and size for the view now - each VR view's Interface tab has
// its own (F3 [hud_floor], F1 [hud_top], F2 [hud_third], F4 [hud]); 1 and 1 = the game's own.
// Flat keeps the game's labels: it has no such tabs.
void LabelLookNow(float* alpha, float* size) {
    *alpha = 1.0f; *size = 1.0f;
    if (g_set.platform.load() != 1) return;
    if (FloorView()) { *alpha = g_set.labelsAlphaFloor.load(); *size = g_set.labelsSizeFloor.load(); }
    else if (!g_enabled.load() || TopPersp()) { *alpha = g_set.labelsAlphaTop.load(); *size = g_set.labelsSizeTop.load(); }
    else if (ThirdPerson()) { *alpha = g_set.labelsAlphaThird.load(); *size = g_set.labelsSizeThird.load(); }
    else if (FullBody()) { *alpha = g_set.labelsAlphaBody.load(); *size = g_set.labelsSizeBody.load(); }
}
// The game's item-label code hooked (LabelHooks, further down): the box's alpha and the size set per label.
std::atomic<bool> g_labelPaintIn{false}, g_labelSizeIn{false};
// [hud_floor] labels_alpha by the game's own label code (exact, per label) rather than as a picture.
bool LabelBoxNative() { return g_labelPaintIn.load() && g_set.labelsNativeFloor.load(); }
// From above only (the camera ours is off) in stereo: the labels over monsters on a plane tilted like the ground.
// On the floor (F3): the labels faded as a picture ([hud_floor] labels_alpha) - only while the game's
// own label code is not hooked (another game build, or labels_native=0), which fades the box exactly.
// From above in stereo with a depth or a tilt for the labels: they come out to lie on their plane.
bool LabelsTilted() {
    return AfrOn() && !g_enabled.load() && g_set.classicTop.load() == 0 &&
           (std::abs(g_set.labelsNear.load()) > 0.01f || std::abs(g_set.labelsTilt.load()) > 0.01f);
}
// [hud] monster_alpha: the plate over the target monster faded - the interface comes out
// for that too, drawn back where the game puts it.
bool PlateFaded() {
    int32_t r[4];
    return g_inWorld.load() && g_set.plateAlpha.load() < 0.995f && gamestate::PlateRect(r);
}
bool LabelsWanted() {
    if (PlateFaded()) return true;
    if (FloorView()) return g_inWorld.load() && g_set.labelsAlphaFloor.load() < 0.995f && !LabelBoxNative();
    return LabelsTilted();
}

// FlatVR's 3D source as [stereo] has it - real stereo: the game's pair; else the
// depth (source 1) or none (0) - told through the D2R Bridge's events whenever
// the bridge comes up (BodyWalk started: a profile may have set the pair on)
// and whenever the setting changes. FlatVR with the pair on and one picture
// coming shows it flat (2026-10-08).
void FlatVr3DTick() {
    static int sent = -1;
    static bool had = false;
    static const wchar_t* const kName[3] = {D2RVR_FLATVR_3D_NONE_NAME, D2RVR_FLATVR_3D_DEPTH_NAME, D2RVR_FLATVR_3D_PAIR_NAME};
    const int want = g_set.afr.load() ? 2 : ((int)IniF(L"stereo", L"source", 1.0f) == 0 ? 0 : 1);
    HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, kName[want]);
    const bool have = e != nullptr;   // the bridge makes the events: they are there while it runs
    if (have && (!had || want != sent)) {
        SetEvent(e);
        sent = want;
        static const char* const kSaid[3] = {"none (a flat screen)", "the depth (ReShade)", "the game's stereo pair"};
        LogF("vrcam: FlatVR's 3D source set to %s (through the D2R Bridge)", kSaid[want]);
    }
    had = have;
    if (e) CloseHandle(e);
}

// The addon and FlatVR take a pair while the block says so. Its writers run on
// the world camera's copy and in a pair's passes - neither runs in the menus -
// so the timer takes the word back as soon as stereo is off.
void AfrQuietWhenOff() {
    if (g_afrBlock && g_afrBlock->enabled && !AfrOn()) g_afrBlock->enabled = 0u;
}
bool PairWanted();

// Once a game frame: the per-frame camera update's copy(second, MASTER).
// Drawing a pair per game frame, the eye is set by HookDrawGameScreen instead.
void AfrNextFrame() {
    if (!g_afrBlock) return;
    const bool on = AfrOn();
    if (on && !PairWanted()) g_eye.store(g_eye.load() ^ 1);
    g_afrBlock->swap = g_set.afrSwap.load() ? 1u : 0u;
    g_afrBlock->eye = (uint32_t)g_eye.load();
    g_afrBlock->frame++;
    g_afrBlock->enabled = on ? 1u : 0u;
}

// ---- The camera, on cleanroom/camera (d2rcam) -------------------------------
// Once a game frame, right after the game placed its own camera, d2rcam calls
// VrFrame, then VrView and VrProj with the game's view and the hero point, and
// puts what they return into the camera (picking and culling follow it).

using d2rcam::m4::V3;

// The hero point of the last frame, for code off the frame thread.
SRWLOCK g_lookLock = SRWLOCK_INIT;
float g_lookAt[3] = {};
bool g_lookOk = false;

// The hero point now: read from the camera, else the last frame's.
bool LookAtNow(float out[3]) {
    if (d2rcam::LookAtNow(out) && std::isfinite(out[0] + out[1] + out[2])) return true;
    AcquireSRWLockShared(&g_lookLock);
    const bool ok = g_lookOk;
    memcpy(out, g_lookAt, sizeof g_lookAt);
    ReleaseSRWLockShared(&g_lookLock);
    return ok;
}

// Where a heading (radians, 0 = toward -z, + = left) and a tilt below the
// horizon (radians) point.
V3 Facing(float heading, float down) {
    return {-sinf(heading) * cosf(down), -sinf(down), -cosf(heading) * cosf(down)};
}

namespace prevscan { void FrameDone(); }
void VrFrame() { prevscan::FrameDone(); AfrNextFrame(); }

// The game's view from above, per eye: orthographic, so moving the camera
// aside would only slide the picture. Each eye's view is turned instead, half
// the angle either way about the screen's vertical through the hero: he stays
// put, nearer and farther things part.
bool TopStereoView(const d2rcam::WorldView& in, float out[16]) {
    if (!in.lookAtValid) return false;
    // the hero in view space
    const float* V = in.gameView;
    const float* L = in.lookAt;
    const float hx = L[0] * V[0] + L[1] * V[4] + L[2] * V[8] + V[12];
    const float hz = L[0] * V[2] + L[1] * V[6] + L[2] * V[10] + V[14];
    const float a = (g_eye.load() == 0 ? 0.5f : -0.5f) * g_set.topAngle.load() * 3.14159265f / 180.0f;
    const float c = cosf(a), sn = sinf(a);
    float turn[16] = {c, 0, -sn, 0,  0, 1, 0, 0,  sn, 0, c, 0,  0, 0, 0, 1};
    turn[12] = hx - (hx * c + hz * sn);
    turn[14] = hz - (hz * c - hx * sn);
    d2rcam::m4::Mul(V, turn, out);
    static bool told = false;
    if (!told) { told = true; LogF("vrcam: stereo from above - eyes turned %.1f deg apart about the hero", g_set.topAngle.load()); }
    return true;
}

// The table view (F5): the game laid on the room's floor. When the view is
// taken (F5, F11, the room's head first seen) the hero is put on the floor
// [table] ahead_m in front of the head, the game's own "up the screen" pointing
// away from the player; from then on the camera is where the head is in the
// room, turned as it is turned, at the size [table] hero_cm gives. The
// hero stays on his spot and the world slides under him, as in the game's own
// view; walking round him or crouching is the camera walking round or crouching.
// The anchor: the floor point under the hero (room x, z) and the room-to-game turn.
struct TableAnchor { float fx, fz, phi, floorY, headX, headZ, yaw, ahead; bool ok; };   // head and yaw: where it was put from
TableAnchor g_tableAnchor{};             // render thread only
std::atomic<bool> g_tableReanchor{true}; // set by F5 and F11, taken by the next view build

// The head in the room (bridge 0.15+): stage position, metres, and its room yaw
// (degrees, + left), carried on to now with the angles' prediction.
bool RoomHead(float pos[3], float ang[3]) {
    const D2RVR_Shared* sh = g_shared;
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || sh->roomMagic != D2RVR_ROOM_MAGIC) return false;
    for (int i = 0; i < 3; ++i) pos[i] = sh->headRoom[i];
    PredictHead(sh, ang);
    // PredictHead carries headYawDeg, the head as the output service sends it; the
    // room's yaw may sit apart from it by a recenter - the same turn, its own zero.
    ang[0] += WrapDeg(sh->headYawRoomDeg - sh->headYawDeg);
    return std::isfinite(pos[0] + pos[1] + pos[2] + ang[0] + ang[1] + ang[2]);
}

// F1 in true perspective: the game camera's own direction, from far enough back
// along it that the ground round the hero fills the FlatVR screen as the game's
// own picture fills the monitor. The hero sits on the screen's plane (TopScale).
bool TopCamera(const d2rcam::WorldView& in, V3* eye, V3* fwd, V3* up, V3* right, V3* ahead) {
    const float* G = in.gameView;
    const float* P = in.gameProj;
    const float* L = in.lookAt;
    float sw, sh, sd;
    if (!LiveScreen(&sw, &sh, &sd)) { sh = 2.0f * tanf(0.5f * CameraFov() * 3.14159265f / 180.0f); sd = 1.0f; }
    // The game's half height of view at the hero, world units: orthographic, or a perspective's at his distance.
    const float m5 = fabsf(P[5]);
    if (!(m5 > 1e-6f)) return false;
    const bool ortho = fabsf(P[11]) < 0.5f;
    const float hz = L[0] * G[2] + L[1] * G[6] + L[2] * G[10] + G[14];   // the hero in the game's view space (looks down -z)
    const float half = ortho ? 1.0f / m5 : std::max(-hz, 1.0f) / m5;
    const float dist = half * 2.0f * sd / sh;   // our camera that far back sees the same height on the screen
    *fwd = {-G[2], -G[6], -G[10]};
    *up = {G[1], G[5], G[9]};
    *right = {G[0], G[4], G[8]};
    // [top] tilt: turned about the hero, about the camera's right - + raises the view
    // toward the horizon (more of the world ahead, and the sky past it)
    if (const float tilt = g_set.topTilt.load(); tilt != 0.0f) {
        const float t = tilt * 3.14159265f / 180.0f, c = cosf(t), s = sinf(t);
        const V3 f = *fwd, u = *up;
        *fwd = f * c + u * s;
        *up = u * c - f * s;
    }
    const float heading = atan2f(-fwd->x, -fwd->z);
    *ahead = Facing(heading, 0.0f);
    *eye = V3{L[0], L[1], L[2]} - *fwd * dist;
    g_topUnitsPerM.store(dist / sd);
    static bool told = false;
    if (!told) { told = true; LogF("vrcam: from above in true perspective - %s game view, camera %.1f units back, %.2f units per metre", ortho ? "orthographic" : "perspective", dist, dist / sd); }
    return true;
}

bool TableCamera(const d2rcam::WorldView& in, V3* eye, V3* fwd, V3* up, V3* right, V3* ahead, float* yawOut) {
    float p[3], ang[3];
    if (!RoomHead(p, ang)) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: the game on the floor (F3) needs the head in the room - BodyWalk 1.72+ with the D2R bridge 0.15+"); }
        return false;
    }
    const float d2r = 3.14159265f / 180.0f;
    const float* G = in.gameView;
    const V3 gameFwd = {-G[2], -G[6], -G[10]};
    const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
    TableAnchor& A = g_tableAnchor;
    // BodyWalk's room can move under the head at once - seen 2026-10-05: the head at
    // 1.83 m, then 0.67 m, in one session, and the game a metre off the floor until
    // F11. No head moves half a metre between two views: such a jump is the room's.
    static float lastP[3] = {};
    static bool lastOk = false;
    const float jump = sqrtf((p[0] - lastP[0]) * (p[0] - lastP[0]) + (p[1] - lastP[1]) * (p[1] - lastP[1]) + (p[2] - lastP[2]) * (p[2] - lastP[2]));
    const bool roomMoved = lastOk && jump > 0.5f;
    memcpy(lastP, p, sizeof lastP);
    lastOk = true;
    if (roomMoved) LogF("vrcam: the table - BodyWalk's room moved %.2f m under the head: the game put down again", jump);
    // The settings program's button, as F11 (the first value seen is only remembered).
    static int placeSeen = INT_MIN;
    const int place = g_set.tablePlace.load();
    const bool pressed = placeSeen != INT_MIN && place != placeSeen;
    placeSeen = place;
    if (g_tableReanchor.exchange(false) || !A.ok || roomMoved || pressed) {
        const float t = ang[0] * d2r, ah = g_set.tableAheadM.load();
        A.fx = p[0] - sinf(t) * ah;   // room forward at yaw t is (-sin t, 0, -cos t)
        A.fz = p[2] - cosf(t) * ah;
        A.phi = heading0 - t;
        A.headX = p[0]; A.headZ = p[2]; A.yaw = t; A.ahead = ah;
        // The real floor: BodyWalk's room is not always the floor's (seen 2026-10-05: the
        // head at 0.72 m standing, the game a metre up). The player stands when the view
        // is taken, so the floor is their standing eye height below the head.
        A.floorY = p[1] - UserEyeHeightM();
        A.ok = true;
        LogF("vrcam: the table - the hero on the floor %.2f m ahead (room %.2f, %.2f), the floor %.2f m below the head (room y %.2f), %.1f units per metre",
             ah, A.fx, A.fz, p[1] - A.floorY, A.floorY, g_set.tableScale.load());
    }
    // [table] ahead_m moved: the game slides along the line it was put on, at once.
    if (const float ah = g_set.tableAheadM.load(); fabsf(ah - A.ahead) > 1e-4f) {
        A.fx = A.headX - sinf(A.yaw) * ah;
        A.fz = A.headZ - cosf(A.yaw) * ah;
        A.ahead = ah;
    }
    // [table] turn: the world about the hero, the board where it is - so the room-to-game
    // turn grows by it, and the bounds undo it (TableTurn in the shader).
    const float phi = A.phi + g_set.tableTurnDeg.load() * d2r;
    const float heading = phi + ang[0] * d2r;
    const float down = std::clamp(-g_set.pitchSign.load() * ang[1] * d2r, -1.55f, 1.55f);
    const float roll = std::clamp(g_set.rollSign.load() * ang[2], -80.0f, 80.0f) * d2r;
    *fwd = Facing(heading, down);
    *ahead = Facing(heading, 0.0f);
    *right = {cosf(heading), 0.0f, -sinf(heading)};
    const V3 up0 = d2rcam::m4::Cross(*right, *fwd);
    *up = up0 * cosf(roll) + *right * sinf(roll);
    // The head from the anchor, turned into the game's axes (about +y by phi).
    const float dx = p[0] - A.fx, dz = p[2] - A.fz, c = cosf(phi), sn = sinf(phi), s = g_set.tableScale.load();
    const float* L = in.lookAt;
    *eye = V3{L[0] + (dx * c + dz * sn) * s, L[1] + std::max(p[1] - A.floorY - g_set.tableHeightM.load(), 0.05f) * s, L[2] + (-dx * sn + dz * c) * s};
    *yawOut = (heading0 - heading) / d2r;   // as TargetYaw: + turned right of the game's camera
    {
        TableBox b{};
        d2rcam::m4::Mul(in.gameView, in.gameProj, b.vp);
        memcpy(b.hero, L, sizeof b.hero);
        b.ok = true;
        AcquireSRWLockExclusive(&g_skyLock); g_tableBox = b; ReleaseSRWLockExclusive(&g_skyLock);
    }
    return true;
}

// Our view: the game camera's heading and tilt, turned by TargetYaw / Pitch /
// Roll (head, mouse, right stick), from the hero's eyes (ViewHeight) or behind
// them ([camera] distance, third person).
// Where the game keeps the views of frames past (2026-10-08, [debug]
// find_prev_matrix=1): DLSS's motion vectors are made against the previous
// frame's camera, which with real stereo is the other eye's. To give each eye's
// DLSS instance its own eye's previous camera the game's copy has to be found.
// Every view vrcam hands the game is kept with its frame number (512 frames);
// a thread then reads the game's memory - the heap and the GPU upload buffers
// mapped into it - for exact copies, straight or transposed, and logs each copy
// with how many frames old it was when read. A place that is always 1 frame
// old is where the previous frame is kept. Turn the head while it runs, so every
// frame's view is its own.
namespace prevscan {
constexpr int kRing = 512;
struct Entry { uint32_t frame; float m[16]; };
SRWLOCK g_lock = SRWLOCK_INIT;
Entry g_ring[kRing];
uint32_t g_count = 0;
std::atomic<uint32_t> g_frame{0};
std::atomic<bool> g_busy{false};
// After the scans: the copies found in the game's own heap, watched in-frame -
// read each time vrcam hands the game a new view, before it is written, and
// logged with how many views old each holds. Write-combined GPU buffers are
// left out: transport, not where the game keeps anything.
constexpr int kWatch = 24;
uintptr_t g_watch[kWatch];
std::atomic<int> g_watchCount{0};
std::atomic<int> g_watchLeft{0};
int g_ages[kWatch][80];   // what the watch saw, per place and call
int g_seen = 0;
// The game's copy of the previous frame's view: the place whose age goes 2, 1, 2, 1
// call by call (vrcam is asked twice a frame, it is written once, at the frame's
// start). 0 = not found.
std::atomic<uintptr_t> g_prevAddr{0};
// The view of each game frame (the last one handed over before the next frame).
float g_frameView[4][16];
uint32_t g_frameViews = 0;
float g_lastView[16];
bool g_lastViewOk = false;
int g_lastEye = 0;
float g_lastProj[16];
bool g_lastProjOk = false;

void PickPrev() {
    for (int i = 0; i < g_watchCount.load(); ++i) {
        int good = 0;
        for (int c = 2; c < g_seen; ++c) {
            const int a = g_ages[i][c], b = g_ages[i][c - 1];
            if ((a == 2 && b == 1) || (a == 1 && b == 2)) ++good;
        }
        if (g_seen > 20 && good >= g_seen - 6) {
            g_prevAddr.store(g_watch[i]);
            LogF("vrcam: the game keeps the previous frame's view at %p (%d of %d calls fit)%s", (void*)g_watch[i], good, g_seen - 2,
                 g_set.dlssPrevFix.load() ? " - [render] dlss_prev_fix: it gets the same eye's, two frames back" : "");
            return;
        }
    }
    Log("vrcam: no place in the game's heap holds the previous frame's view exactly");
}

// Once a game frame (VrFrame): the view the frame was drawn with.
void FrameDone() {
    if (!g_lastViewOk) return;
    if (g_lastProjOk) dlssmv::RecordFrame(g_lastView, g_lastProj, g_lastEye);
    memcpy(g_frameView[g_frameViews % 4], g_lastView, sizeof g_lastView);
    ++g_frameViews;
}

// After the game copied its view into its previous-frame place (at the frame's
// start): the same eye's view two frames back instead, when it holds just what
// the game puts there - the last frame's view - and nothing else.
void FixPrev() {
    const uintptr_t a = g_prevAddr.load();
    if (!a || !g_set.dlssPrevFix.load() || !AfrOn() || PairWanted() || g_frameViews < 3) return;
    const float* last = g_frameView[(g_frameViews - 1) % 4];
    const float* same = g_frameView[(g_frameViews - 2) % 4];
    float now[16];
    if (!SafeRead(now, (const void*)a, sizeof now)) { g_prevAddr.store(0); return; }
    if (memcmp(now, same, sizeof now) == 0) return;   // already ours this frame
    if (memcmp(now, last, sizeof now) != 0) {         // not the game's previous view any more: let go
        static int off = 0;
        if (++off > 30) { g_prevAddr.store(0); off = 0; Log("vrcam: the previous-frame place holds something else now - let go"); }
        return;
    }
    __try { memcpy((void*)a, same, 64); } __except (EXCEPTION_EXECUTE_HANDLER) { g_prevAddr.store(0); }
}

// How many views old the 16 floats at a are, -1 none kept, -2 unreadable.
int AgeAt(uintptr_t a, bool* transposed) {
    float m[16];
    if (!SafeRead(m, (const void*)a, sizeof m)) return -2;
    const uint32_t now = g_frame.load();
    AcquireSRWLockShared(&g_lock);
    int age = -1;
    const uint32_t c = g_count;
    for (int k = 0; k < 64 && k < (int)c && age < 0; ++k) {
        const Entry& e = g_ring[(c - 1 - k) % kRing];
        if (memcmp(m, e.m, sizeof m) == 0) { age = (int)(now - e.frame); *transposed = false; break; }
        bool same = true;
        for (int r = 0; r < 4 && same; ++r)
            for (int q = 0; q < 4 && same; ++q) same = m[r * 4 + q] == e.m[q * 4 + r];
        if (same) { age = (int)(now - e.frame); *transposed = true; }
    }
    ReleaseSRWLockShared(&g_lock);
    return age;
}

// On the render thread, as a view is about to be handed over.
void Watch() {
    if (g_watchLeft.load() <= 0) return;
    const int n = g_watchCount.load();
    char line[512];
    int len = snprintf(line, sizeof line, "vrcam: prev watch (call %u):", g_frame.load() + 1);
    for (int i = 0; i < n && len < (int)sizeof line - 16; ++i) {
        bool tr = false;
        const int age = AgeAt(g_watch[i], &tr);
        len += snprintf(line + len, sizeof line - len, " %d%s", age, tr ? "t" : "");
        if (g_seen < 80) g_ages[i][g_seen] = age;
    }
    Log(line);
    if (g_seen < 80) ++g_seen;
    if (g_watchLeft.fetch_sub(1) == 1) PickPrev();
}

void Record(const float m[16]) {
    const uint32_t f = g_frame.fetch_add(1) + 1;
    AcquireSRWLockExclusive(&g_lock);
    Entry& e = g_ring[g_count % kRing];
    e.frame = f;
    memcpy(e.m, m, sizeof e.m);
    ++g_count;
    ReleaseSRWLockExclusive(&g_lock);
}

struct Hit { uintptr_t addr; int age; bool transposed; uint32_t prot; };

// One region: every 16-byte step whose 16 floats are a kept view (or its transpose).
int ScanRegion(const uint8_t* base, size_t size, const Entry* snap, int n, const uint32_t* first, const uint32_t* firstT,
               const uint8_t* bloom, Hit* out, int cap, uint32_t prot) {
    int got = 0;
    __try {
        for (size_t o = 0; o + 64 <= size && got < cap; o += 16) {
            const uint32_t w = *(const uint32_t*)(base + o);
            if (w == 0 || !bloom[(w * 2654435761u) >> 20]) continue;
            for (int k = 0; k < n && got < cap; ++k) {
                if (w == first[k] && memcmp(base + o, snap[k].m, 64) == 0) {
                    out[got++] = {(uintptr_t)(base + o), (int)(g_frame.load() - snap[k].frame), false, prot};
                    break;
                }
                if (w == firstT[k]) {
                    const float* f = (const float*)(base + o);
                    bool same = true;
                    for (int r = 0; r < 4 && same; ++r)
                        for (int c = 0; c < 4 && same; ++c) same = f[r * 4 + c] == snap[k].m[c * 4 + r];
                    if (same) { out[got++] = {(uintptr_t)(base + o), (int)(g_frame.load() - snap[k].frame), true, prot}; break; }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return got;
}

DWORD WINAPI Thread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    static Entry snap[kRing];
    static uint32_t first[kRing], firstT[kRing];
    static Hit hits[4096];
    static uint8_t bloom[4096];
    for (int pass = 1; pass <= 3; ++pass) {
        const ULONGLONG t0 = GetTickCount64();
        int nh = 0;
        size_t read = 0;
        MEMORY_BASIC_INFORMATION mi{};
        for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && nh < 4096; a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
            // a region going away under the walk is stepped over, not the end of it
            if (!VirtualQuery((void*)a, &mi, sizeof mi)) { mi.BaseAddress = (void*)a; mi.RegionSize = 0x10000; continue; }
            const uint32_t prot = mi.Protect & 0xFF;
            if (mi.State != MEM_COMMIT || (mi.Protect & PAGE_GUARD) || (prot != PAGE_READWRITE && prot != PAGE_WRITECOPY) ||
                mi.RegionSize > (1ull << 31) || (mi.Type != MEM_PRIVATE && mi.Type != MEM_MAPPED))
                continue;
            // the views kept now, the last 64 frames: what a region holds was written lately
            int n = 0;
            AcquireSRWLockShared(&g_lock);
            const uint32_t c = g_count;
            for (int k = 0; k < 64 && k < (int)c; ++k) snap[n++] = g_ring[(c - 1 - k) % kRing];
            ReleaseSRWLockShared(&g_lock);
            memset(bloom, 0, sizeof bloom);
            for (int k = 0; k < n; ++k) {
                memcpy(&first[k], &snap[k].m[0], 4);
                memcpy(&firstT[k], &snap[k].m[0], 4);   // m[0] is on the diagonal: the same either way
                bloom[(first[k] * 2654435761u) >> 20] = 1;
            }
            read += mi.RegionSize;
            nh += ScanRegion((const uint8_t*)mi.BaseAddress, mi.RegionSize, snap, n, first, firstT, bloom, hits + nh, 4096 - nh, mi.Protect);
        }
        LogF("vrcam: prev-matrix scan %d: %.0f MB read in %.1f s, %d copies of vrcam's views found", pass, read / 1048576.0,
             (GetTickCount64() - t0) / 1000.0, nh);
        for (int i = 0; i < nh && i < 200; ++i) {
            MEMORY_BASIC_INFORMATION r{};
            VirtualQuery((void*)hits[i].addr, &r, sizeof r);
            const uintptr_t mod = (uintptr_t)GetModuleHandleW(nullptr);
            LogF("vrcam:   copy at %p (region %p +0x%llX, %s, protect 0x%X%s) - %d frame(s) old%s", (void*)hits[i].addr, r.AllocationBase,
                 (unsigned long long)(hits[i].addr - (uintptr_t)r.AllocationBase), r.Type == MEM_MAPPED ? "mapped" : "private",
                 hits[i].prot, (uintptr_t)r.AllocationBase == mod ? ", the game's image" : "", hits[i].age,
                 hits[i].transposed ? ", transposed" : "");
        }
        // the heap copies of every pass, unique, for the in-frame watch
        static int n = 0;
        if (pass == 1) n = 0;
        for (int i = 0; i < nh && n < kWatch; ++i) {
            if ((hits[i].prot & 0x400) != 0) continue;   // write-combined: GPU transport
            bool dup = false;
            for (int k = 0; k < n && !dup; ++k) dup = g_watch[k] == hits[i].addr;
            if (!dup) g_watch[n++] = hits[i].addr;
        }
        if (pass == 3) {
            g_watchCount.store(n);
            char b[1024];
            int len = snprintf(b, sizeof b, "vrcam: prev watch - %d heap places, in this order:", n);
            for (int k = 0; k < n && len < (int)sizeof b - 24; ++k) len += snprintf(b + len, sizeof b - len, " %p", (void*)g_watch[k]);
            Log(b);
            g_seen = 0;
            g_watchLeft.store(80);
        }
        Sleep(500);
    }
    g_busy.store(false);
    return 0;
}

// Once per switch of [debug] find_prev_matrix to 1.
void Tick() {
    static bool was = false;
    const bool want = g_set.findPrevMatrix.load();
    if (want && !was && !g_busy.exchange(true)) {
        Log("vrcam: prev-matrix scan starting - turn the head slowly for the next few seconds");
        if (HANDLE h = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr)) CloseHandle(h);
        else g_busy.store(false);
    }
    was = want;
}
}  // namespace prevscan

// Native OpenXR: the eyes this pass is drawn with - the open pair's (PairEyes), when they are
// eyes at all (finite, their turns of unit length). A pair that did not open, or eyes that came
// back broken, keep the last good ones up to 2 s while the session runs in the world: falling
// back to BodyWalk's head for those frames meant its other zero and the camera swung between
// the two (2026-10-10). False: no native eyes - the camera is built as on FlatVR.
SRWLOCK g_goodEyesLock = SRWLOCK_INIT;
xr::Eyes g_goodEyes{};
ULONGLONG g_goodEyesAt = 0;
bool EyesSane(const xr::Eyes& e) {
    for (int i = 0; i < 2; ++i) {
        const float* q = e.quat[i];
        const float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        if (!std::isfinite(n2) || fabsf(n2 - 1.0f) > 0.05f) return false;
        for (int k = 0; k < 3; ++k) if (!std::isfinite(e.pos[i][k]) || fabsf(e.pos[i][k]) > 20.0f) return false;
        if (!std::isfinite(e.tanL[i]) || !std::isfinite(e.tanR[i]) || !std::isfinite(e.tanU[i]) || !std::isfinite(e.tanD[i]) ||
            !(e.tanR[i] - e.tanL[i] > 0.01f) || !(e.tanU[i] - e.tanD[i] > 0.01f))
            return false;
    }
    return true;
}
bool NativeEyes(xr::Eyes* out) {
    if (!NativeView()) return false;
    xr::Eyes e{};
    const bool got = xr::PairEyes(&e) && EyesSane(e);
    const ULONGLONG now = GetTickCount64();
    bool ok = got;
    AcquireSRWLockExclusive(&g_goodEyesLock);
    if (got) {
        g_goodEyes = e;
        g_goodEyesAt = now;
        *out = e;
    } else if (g_goodEyesAt && now - g_goodEyesAt < 2000 && xr::Running() && !gamestate::MenuOpen()) {
        *out = g_goodEyes;
        ok = true;
        static ULONGLONG toldAt = 0;
        if (!toldAt || now - toldAt > 10000) {
            toldAt = now;
            LogF("vrcam: openxr - no eyes from the runtime for this pass: the last good ones kept (%llu ms old) - the camera stays put",
                 (unsigned long long)(now - g_goodEyesAt));
        }
    }
    ReleaseSRWLockExclusive(&g_goodEyesLock);
    return ok;
}

bool VrViewInner(const d2rcam::WorldView& in, float out[16]);
extern std::atomic<bool> g_inWorld;
bool VrView(const d2rcam::WorldView& in, float out[16]) {
    prevscan::Watch();
    const bool ours = VrViewInner(in, out);
    if (ours) {
        prevscan::Record(out);
        memcpy(prevscan::g_lastView, out, sizeof prevscan::g_lastView);
        prevscan::g_lastViewOk = true;
        prevscan::g_lastEye = g_eye.load() & 1;
        prevscan::FixPrev();
    }
    return ours;
}
bool VrViewInner(const d2rcam::WorldView& in, float out[16]) {
    AcquireSRWLockExclusive(&g_lookLock);
    memcpy(g_lookAt, in.lookAt, sizeof g_lookAt);
    g_lookOk = in.lookAtValid;
    ReleaseSRWLockExclusive(&g_lookLock);
    // The menus (the character screen's 3D scene too: it has a hero point) keep the game's camera.
    if (!g_inWorld.load()) return false;
    if (TopStereoOn()) return TopStereoView(in, out);
    if (!g_enabled.load() || !in.lookAtValid) return false;
    const float* G = in.gameView;   // row-major, v * M: column i is the game camera's axis i in the world
    const float* L = in.lookAt;
    V3 fwd, ahead, right, up, eye, hang;
    float yaw = 0.0f;
    // Native OpenXR (vr/xr.cpp): the runtime's eyes for the moment this frame is shown.
    xr::Eyes xe{};
    const bool native = NativeEyes(&xe);
    float nativeIpd = 0.0f;   // world units
    if (native) {
        // The head as the runtime has it - its turn, tilt and place in the room, metres from
        // where it was at the recentre - on top of the body's turn (stick, mouse); no neck
        // model, no head prediction: the runtime predicts for the display time.
        const V3 gameFwd = {-G[2], -G[6], -G[10]};
        const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
        const float d2r = 3.14159265f / 180.0f;
        const float body = WrapDeg(g_pairNow.load() ? g_heldYaw.load() : g_mouseYaw.load() + g_turnYaw.load());
        const float base = heading0 - body * d2r;   // the room's -z in the world
        const float cb = cosf(base), sb = sinf(base);
        auto toWorld = [&](V3 v) { return V3{v.x * cb + v.z * sb, v.y, -v.x * sb + v.z * cb}; };
        const float* q = xe.quat[0];   // the left eye's turn (the eyes are parallel on most headsets: logged at the start)
        const V3 hf = d2rcam::m4::Rotate(q, {0.0f, 0.0f, -1.0f});
        fwd = toWorld(hf);
        up = toWorld(d2rcam::m4::Rotate(q, {0.0f, 1.0f, 0.0f}));
        right = toWorld(d2rcam::m4::Rotate(q, {1.0f, 0.0f, 0.0f}));
        yaw = WrapDeg(body - atan2f(-hf.x, -hf.z) / d2r);   // + turns right, as TargetYaw
        const float heading = heading0 - yaw * d2r;
        ahead = Facing(heading, 0.0f);
        const V3 side = {cosf(heading), 0.0f, -sinf(heading)};
        const V3 pivot = V3{L[0], L[1] + ViewHeight() + JumpLift(), L[2]} + side * g_set.camSide.load() + ahead * g_set.camForward.load();
        const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre, as TrueScale
        const V3 mid = {0.5f * (xe.pos[0][0] + xe.pos[1][0]), 0.5f * (xe.pos[0][1] + xe.pos[1][1]), 0.5f * (xe.pos[0][2] + xe.pos[1][2])};
        V3 head = pivot + toWorld(mid) * upm;
        if (!ThirdPerson() && g_set.neckModel.load()) {
            // the head turns about the top of the neck, as with FlatVR (VrViewInner's neck model): looking
            // down the eyes go forward and down over the chest. On top of the headset's own place since
            // 2026-10-10 - only with lean_m 0 before, so with the default 0.1 "Head turns about the neck"
            // did nothing in native and looking down saw the chest's armour from behind it (PS VR2).
            const float nu = g_set.neckUpCm.load() * 0.01f * upm, nf = g_set.neckFwdCm.load() * 0.01f * upm;
            head = head - V3{0.0f, nu, 0.0f} + up * nu + fwd * nf;
        }
        if (ThirdPerson()) {
            // behind the hero: on the body's turn, the head free to look round (or with the gaze)
            eye = head - (g_xrThirdGaze.load() ? fwd : Facing(base, 0.0f)) * ViewDistance();
            hang = pivot;
        } else {
            eye = head - fwd * ViewDistance();
            hang = head + (eye - head) * g_set.handsFollowCam.load();
        }
        const V3 apart = {xe.pos[1][0] - xe.pos[0][0], xe.pos[1][1] - xe.pos[0][1], xe.pos[1][2] - xe.pos[0][2]};
        nativeIpd = d2rcam::m4::Length(apart) * upm;
    } else if (TableView()) {
        if (!TableCamera(in, &eye, &fwd, &up, &right, &ahead, &yaw)) return false;
        hang = eye;
        g_floorEyeH.store(eye.y - L[1]); g_floorFwdY.store(fwd.y); g_floorUpY.store(up.y); g_floorCamOk.store(true);
    } else if (TopPersp()) {
        if (!TopCamera(in, &eye, &fwd, &up, &right, &ahead)) return false;
        hang = eye;
    } else {
    const V3 gameFwd = {-G[2], -G[6], -G[10]};
    const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
    const float down0 = asinf(std::clamp(-gameFwd.y, -1.0f, 1.0f));
    const float d2r = 3.14159265f / 180.0f;
    yaw = TargetYaw();                                   // + turns right
    const float heading = heading0 - yaw * d2r;
    const float down = std::clamp(down0 + TargetPitch() * d2r, -1.55f, 1.55f);   // + looks further down
    fwd = Facing(heading, down);
    ahead = Facing(heading, 0.0f);
    right = {cosf(heading), 0.0f, -sinf(heading)};
    const V3 up0 = d2rcam::m4::Cross(right, fwd);
    const float roll = TargetRoll() * d2r;               // + = the right ear down
    up = up0 * cosf(roll) + right * sinf(roll);

    // The eye point: the hero's eyes, moved along where we look, flattened on the ground.
    const V3 pivot = V3{L[0], L[1] + ViewHeight() + JumpLift(), L[2]} + right * g_set.camSide.load() + ahead * g_set.camForward.load();
    // Neck model: the head turns about the top of the neck - over the body's
    // middle, below and behind the eyes - not about the eyes. Level, the eyes
    // sit in front of the neck; looking down they go forward and down with the
    // head, and see the chest instead of the neck below them. The headset sends
    // only turns, so this is where the eyes' own travel comes from.
    V3 eyes = pivot;
    if (!ThirdPerson() && g_set.neckModel.load()) {
        const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre at this hero's size
        const float nu = g_set.neckUpCm.load() * 0.01f * upm, nf = g_set.neckFwdCm.load() * 0.01f * upm;
        const V3 neck = pivot - V3{0.0f, nu, 0.0f};
        eyes = neck + up * nu + fwd * nf;
    }
    eye = eyes - fwd * ViewDistance();

    // The hands and the aim ray hang off the camera itself, as the real ones
    // hang off the headset: [camera] distance moves the camera off the pivot
    // along the view, and hands hung off the pivot slid against the real ones.
    // But moved ahead, the camera takes the hands out of the arms' reach:
    // [arms] follow_camera takes that share of the move only (0 = the eyes).
    // Third person keeps the pivot (the camera is far behind the hero there).
    // The head centre, before the AFR half-eye shift.
    hang = ThirdPerson() ? pivot : pivot + (eye - pivot) * g_set.handsFollowCam.load();
    }
    {   // a view that is no view (an angle come through not finite): the last good one, never a
        // frame of stretched triangles (2026-10-10). The render thread only.
        struct Good { V3 fwd, ahead, right, up, eye, hang; float yaw, ipd; bool ok; };
        static Good good{};
        auto fin = [](const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (fin(fwd) && fin(ahead) && fin(right) && fin(up) && fin(eye) && fin(hang) && std::isfinite(yaw) && std::isfinite(nativeIpd)) {
            good = {fwd, ahead, right, up, eye, hang, yaw, nativeIpd, true};
        } else {
            static ULONGLONG toldAt = 0;
            if (!toldAt || GetTickCount64() - toldAt > 10000) { toldAt = GetTickCount64(); Log("vrcam: a view came out not finite - the last good one kept"); }
            if (!good.ok) return false;
            fwd = good.fwd; ahead = good.ahead; right = good.right; up = good.up; eye = good.eye; hang = good.hang; yaw = good.yaw;
            nativeIpd = good.ipd;
        }
    }
    g_viewBuilds.fetch_add(1);
    g_viewFwdY.store(fwd.y);
    float view[16];
    d2rcam::m4::LookTo(eye, fwd, up, view);
    const float hangA[3] = {hang.x, hang.y, hang.z};
    for (int i = 0; i < 3; ++i) g_eyeWorld[i].store(hangA[i]);
    for (int i = 0; i < 3; ++i) g_eyeFromLook[i].store(hangA[i] - L[i]);
    g_eyeOk.store(true);

    V3 camPos = eye;   // this eye's camera, for the table's bounds
    if (AfrOn()) {
        // Camera half an eye to the left (eye 0) or right (eye 1): the
        // world then sits that much to the right or left in view space.
        float ipd = g_set.ipd.load(), conv = 0.0f;
        if (native) ipd = nativeIpd;   // the runtime's eyes, at the world's scale
        else TrueScale(&ipd, &conv);
        const float half = 0.5f * ipd;
        view[12] += g_eye.load() == 0 ? half : -half;
        g_eyeHalf.store(half);
        camPos = eye + right * (g_eye.load() == 0 ? -half : half);
    }
    // When this eye's view was built: the head pose it is drawn for is the
    // one of this moment (PredictHead carries it on to now). FlatVR places
    // the screen at the head pose of this stamp and the headset's own
    // reprojection does the rest - see FlatVRAfrHalves.
    // [stereo] stamps=0 (a D2R VR Settings box, 2026-10-08): no stamp at all - FlatVR
    // then keeps its screen at the head as it is, as in the views without a head-turned
    // camera, which stayed smooth while tabletop and first person juddered for players.
    if (g_afrBlock && !g_set.stamps.load()) g_afrBlock->stamp_magic = 0;
    if (g_afrBlock && g_set.stamps.load()) {
        // Without prediction the head in this view is BodyWalk's last
        // sample as it was: its own stamp is the exact moment. With it,
        // the head carried on to now.
        uint32_t stamp = FlatVRAfrStampNow();
        const D2RVR_Shared* sh = g_shared;
        if (!g_set.headPredict.load() && sh && sh->version == D2RVR_SHARED_VERSION && sh->sampleStampMagic == D2RVR_SAMPLE_STAMP_MAGIC)
            stamp = sh->sampleStamp;
        g_afrBlock->stamp[g_eye.load() & 1] = stamp;
        // and remembered: the frame presented may be an older view of this eye
        const int e = g_eye.load() & 1;
        g_stampHist[e][g_stampHistAt[e] = (g_stampHistAt[e] + 1) & 3] = stamp;
        if (e == 0 && replay::g_on.load() && replay::g_want.load()) {   // replayed: the right eye is this view too, no view of its own
            g_afrBlock->stamp[1] = stamp;
            g_stampHist[1][g_stampHistAt[1] = (g_stampHistAt[1] + 1) & 3] = stamp;
        }
        g_afrBlock->stamp_magic = FLATVR_AFR_STAMP_MAGIC;
    }
    {   // Row-major, v*M: the columns are the camera's axes in the world.
        AcquireSRWLockExclusive(&g_skyLock);
        SkyView& sv = g_skyView[g_eye.load() & 1];
        for (int i = 0; i < 3; ++i) { sv.axes[i] = view[i*4]; sv.axes[3 + i] = view[i*4 + 1]; sv.axes[6 + i] = view[i*4 + 2]; }
        sv.eyeRel[0] = camPos.x - L[0]; sv.eyeRel[1] = camPos.y - L[1]; sv.eyeRel[2] = camPos.z - L[2];
        memcpy(sv.hero, L, sizeof sv.hero);
        sv.axes_ok = true;
        if ((g_eye.load() & 1) == 0 && AfrOn() && replay::g_on.load() && replay::g_want.load()) {
            // replayed: the right eye's sky view is the left one moved by the eye separation
            SkyView& r1 = g_skyView[1];
            const bool projOk = r1.proj_ok;
            float proj[4]; memcpy(proj, r1.proj, sizeof proj);
            r1 = sv;   // the projection: VrProjInner's (the left one shifted the other way)
            if (projOk) memcpy(r1.proj, proj, sizeof proj); else r1.proj_ok = sv.proj_ok;
            const float sh = 2.0f * g_eyeHalf.load();
            r1.eyeRel[0] += right.x * sh; r1.eyeRel[1] += right.y * sh; r1.eyeRel[2] += right.z * sh;
        }
        ReleaseSRWLockExclusive(&g_skyLock);
    }
    g_camYaw.store(yaw);
    // What the stick needs: our right and forward on the ground, and the game
    // camera's screen axes in the world (it reads its stick on ITS screen).
    StickFrame f{};
    const V3 r = right, a = ahead;
    f.camR[0] = r.x; f.camR[1] = r.y; f.camR[2] = r.z;
    f.camF[0] = a.x; f.camF[1] = a.y; f.camF[2] = a.z;
    f.scrR[0] = G[0]; f.scrR[1] = G[4]; f.scrR[2] = G[8];
    f.scrU[0] = G[1]; f.scrU[1] = G[5]; f.scrU[2] = G[9];
    // The game's own projection may mirror an axis; the stick is read on
    // its SCREEN, so a mirrored axis turns the screen direction around.
    f.mirX = in.gameProj[0] < 0.0f ? -1.0f : 1.0f;
    f.mirY = in.gameProj[5] < 0.0f ? -1.0f : 1.0f;
    f.yawAtBuild = yaw;
    f.valid = true;
    AcquireSRWLockExclusive(&g_stickLock); g_stickFrame = f; ReleaseSRWLockExclusive(&g_stickLock);
    flog::Line("V,%u,%.4f,%d,%.3f,%.3f,%.3f,%.3f,%u", flog::Pass(), flog::Dt(), g_eye.load() & 1, yaw,
               atan2f(f.camF[0], f.camF[2]) * 57.2957795f, L[0], L[2],
               g_afrBlock ? g_afrBlock->stamp[g_eye.load() & 1] : 0u);
    memcpy(out, view, sizeof view);
    cbcmp::NoteView(g_eye.load(), view);
    if (g_eye.load() == 0) { AcquireSRWLockExclusive(&g_leftViewLock); memcpy(g_leftView, view, sizeof view); ReleaseSRWLockExclusive(&g_leftViewLock); }
    return true;
}

// Our projection: the FlatVR screen's frustum (or [camera] fov), the near
// clip that cuts the hero's own head away, and in AFR each eye's off-axis
// shift so what lies at `convergence` sits on the screen.
bool VrProjInner(const d2rcam::WorldView& in, float M[16]);
bool VrProj(const d2rcam::WorldView& in, float M[16]) {
    const bool ours = VrProjInner(in, M);
    if (ours) {
        memcpy(prevscan::g_lastProj, M, sizeof prevscan::g_lastProj);
        prevscan::g_lastProjOk = true;
        if (prevscan::g_lastViewOk) dlssmv::SetPending(prevscan::g_lastView, M, g_eye.load() & 1);
    }
    return ours;
}
bool VrProjInner(const d2rcam::WorldView& in, float M[16]) {
    if (!g_enabled.load() || !g_inWorld.load()) return false;   // the menus: the game's own (no near clip through the heroes' heads)
    const float ratio = in.viewportH > 0.0f ? in.viewportW / in.viewportH : 0.0f;
    const float aspect = ratio > 0.1f && ratio < 10.0f ? ratio : 16.0f / 9.0f;
    g_lastAspect.store(aspect);
    // The table: nothing of the hero to cut away, and the head may come down close to the game - 5 cm.
    const float nearZ = TableView() ? 0.05f * g_set.tableScale.load() : TopPersp() ? 0.5f : std::max(g_set.nearClip.load(), 0.05f);
    g_lastNear.store(nearZ);
    float shift = 0.0f;
    xr::Eyes xe{};
    if (NativeEyes(&xe)) {
        // Native OpenXR: this eye's own frustum, from the runtime's fov (tangents; left and down
        // negative). The replay makes the right eye from the left projection with its x offset
        // moved by twice `shift` (camfix): the right eye's own offset, with the mirrored fovs
        // headsets have - each eye wider on its outer side.
        const int e = g_eye.load() & 1;
        auto offX = [&](int i) { return (xe.tanR[i] + xe.tanL[i]) / (xe.tanR[i] - xe.tanL[i]); };
        d2rcam::m4::PerspectiveRevZ(1.0f, 1.0f, nearZ, M);
        M[0] = 2.0f / (xe.tanR[e] - xe.tanL[e]);
        M[5] = 2.0f / (xe.tanU[e] - xe.tanD[e]);
        M[8] = offX(e);
        M[9] = (xe.tanU[e] + xe.tanD[e]) / (xe.tanU[e] - xe.tanD[e]);
        shift = 0.5f * (offX(0) - offX(1));
        static bool told = false;
        if (!told && e == 0) {
            told = true;
            const float r0 = 2.0f / (xe.tanR[1] - xe.tanL[1]), r5 = 2.0f / (xe.tanU[1] - xe.tanD[1]);
            LogF("vrcam: openxr - projection x %.4f y %.4f, off-axis %.4f / %.4f (left / right); the right eye's x %.4f y %.4f%s", M[0], M[5], M[8],
                 offX(1), r0, r5, fabsf(r0 - M[0]) > 0.01f * M[0] || fabsf(r5 - M[5]) > 0.01f * M[5] ? " - NOT mirrored: the replayed right eye is a little off" : "");
        }
        g_projSx.store(M[0]); g_projSy.store(M[5]); g_stereoIpd.store(2.0f * g_eyeHalf.load()); g_stereoConv.store(0.0f);
    } else {
    d2rcam::m4::PerspectiveRevZ(CameraFov() * 3.14159265f / 180.0f, aspect, nearZ, M);
    // The frustum IS the FlatVR screen: each side by its own size at its distance.
    if (float sw, sh, sd; g_set.fovFromFlatVR.load() && LiveScreen(&sw, &sh, &sd)) { M[0] = 2.0f * sd / sw; M[5] = 2.0f * sd / sh; }
    // Off-axis frustum per eye: what lies at `convergence` gets no parallax and
    // sits on the screen, farther goes in behind it. With parallel frusta the
    // whole world was in front of the screen and read as shallow.
    float ipd = g_set.ipd.load(), conv = g_set.convergence.load();
    TrueScale(&ipd, &conv);
    g_projSx.store(M[0]); g_projSy.store(M[5]); g_stereoIpd.store(ipd); g_stereoConv.store(conv);
    if (AfrOn() && conv > 0.0f) {
        shift = M[0] * 0.5f * ipd / conv;
        M[8] += g_eye.load() == 0 ? shift : -shift;
    }
    }
    if ((g_eye.load() & 1) == 0) g_projShift.store(shift);
    AcquireSRWLockExclusive(&g_skyLock);
    SkyView& sv = g_skyView[g_eye.load() & 1];
    sv.proj[0] = M[0]; sv.proj[1] = M[5]; sv.proj[2] = M[8]; sv.proj[3] = M[9]; sv.proj_ok = true;
    const bool replayed = (g_eye.load() & 1) == 0 && AfrOn() && replay::g_on.load() && replay::g_want.load();
    if (replayed) {
        // replayed: no right pass builds the right eye's projection - the left one, shifted the other way
        SkyView& r1 = g_skyView[1];
        r1.proj[0] = M[0]; r1.proj[1] = M[5]; r1.proj[2] = M[8] - 2.0f * shift; r1.proj[3] = M[9]; r1.proj_ok = true;
    }
    for (int e = 0; e < 2; ++e)
        if (e == (g_eye.load() & 1) || (e == 1 && replayed))
            g_skyHist[e][g_skyHistAt[e] = (g_skyHistAt[e] + 1) & 3] = g_skyView[e];
    ReleaseSRWLockExclusive(&g_skyLock);
    return true;
}

// While attacking along the hand, "under the cursor" is under the hand's ray.
bool VrRay(float origin[3], float dir[3]) {
    if (!g_enabled.load() || !g_aimRay.load()) return false;
    if (!g_aimDown && GetTickCount64() >= g_aimRayUntil.load()) { g_aimRay.store(false); return false; }
    for (int i = 0; i < 3; ++i) { origin[i] = g_aimO[i].load(); dir[i] = g_aimD[i].load(); }
    return true;
}

void LogF(const char* fmt, ...) {
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    Log(b);
}

// XInput: turn the left stick by the camera yaw
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
struct XHook { const wchar_t* dll; const char* name; int ordinal; XInputGetStateFn orig; bool done; std::atomic<uint32_t> calls; };
XHook g_xhooks[] = {
    {L"xinput1_4.dll",   "XInputGetState",   0,   nullptr, false, {0}},
    {L"xinput1_4.dll",   "XInputGetStateEx", 100, nullptr, false, {0}},
    {L"xinput1_3.dll",   "XInputGetState",   0,   nullptr, false, {0}},
    {L"xinput1_3.dll",   "XInputGetStateEx", 100, nullptr, false, {0}},
    {L"xinput9_1_0.dll", "XInputGetState",   0,   nullptr, false, {0}},
};

// Bow aim: while a skill button is held, the left stick is the bow hand's
// pointing, so the game aims (and turns the hero) that way instead of where
// his feet face. The hand's direction is in the head's turn frame, which the
// camera turns with, so it goes in as a stick "relative to the view" and the
// usual camera-yaw turn below makes it a direction on the game's screen.
// Pointing = the grip's -Z (a controller held like a gun) plus its -Y (a fist
// round a bow's grip): flattened on the ground, one of the two always remains.
bool BowStick(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 1) return false;
    const XINPUT_GAMEPAD& g = s->Gamepad;
    const WORD skills = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (!(g.wButtons & skills) && g.bRightTrigger < 128) return false;
    const D2RVR_Shared* sh = g_shared;
    const int hand = g_set.bowHand.load();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand))) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    auto rot = [&](float vx, float vy, float vz, float* o) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        o[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        o[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        o[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    const float x = a[0] + b[0], fwd = -(a[2] + b[2]);   // head frame: x right, z back
    const float l = sqrtf(x*x + fwd*fwd);
    if (!(l > 0.05f)) return false;
    s->Gamepad.sThumbLX = (SHORT)std::clamp(x / l * 32767.0f, -32768.0f, 32767.0f);
    s->Gamepad.sThumbLY = (SHORT)std::clamp(fwd / l * 32767.0f, -32768.0f, 32767.0f);
    return true;
}

// Attack along the hand (bow mode 2). On the pad the game attacks where the
// hero's feet face and never reads the stick for it (2026-10-02 log: ten shots,
// stick turned each time, facing stayed 82.5). With the mouse it attacks the
// point under the cursor, standing still while Shift is held - and the point
// under the cursor is found through the world camera's ray, which RayBuild
// hands us anyway. So while A is held: the ray starts at the aiming hand and
// runs where it points, A itself is kept from the game, and Shift + left
// button go in instead. Shooting and striking both go that way, running
// backwards included.

// The aiming hand per weapon set, as the player sets it: a bow in set I aimed
// with the left hand, a sword in set II with the right - or both sets right,
// or both left; the weapon itself does not decide.
// Bow and crossbow: the left hand ([bow] ranged_left). Otherwise the set's hand; if only the other one is tracked (a controller asleep or out
// of view), that one - an untracked hand left every arrow flying straight ahead.
int AimHand() {
    const uint32_t wc = gamestate::WeaponClass();
    const bool ranged = wc == 3 || wc == 4;   // D2RVR_WEAPON_BOW, D2RVR_WEAPON_CROSSBOW
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;   // thrown from the right hand
    const int hand = thrown ? 0 : ranged && g_set.rangedLeft.load() ? 1 : gamestate::WeaponSet() == 2 ? g_set.bowHand2.load() : g_set.bowHand.load();
    const D2RVR_Shared* sh = g_shared;
    if (sh && sh->version == D2RVR_SHARED_VERSION && !(sh->handsValid & (1u << hand)) && (sh->handsValid & (1u << (hand ^ 1))))
        return hand ^ 1;
    return hand;
}

// A staff is held in both hands, the right one ahead: it points from the
// left hand to the right one, whichever way the wrists are turned. With
// [hands] staff_free_left the right hand alone holds it: it points along the
// shaft as the skeleton hook last posed it, the left hand on it or not.
// A crossbow is held the same way ([bow] xbow_two_hands): the right hand alone,
// the left taking it with the grip - the right hand behind, the left ahead.
bool XbowLikeStaff() { return g_set.xbowTwoHands.load() && gamestate::WeaponType() == D2RVR_TYPE_CROSSBOW; }
// A melee weapon held in both hands the staff's way, without its aim (they do not shoot):
// 1 a spear or a polearm (the left slides along the shaft), 2 a two-handed sword with the
// other hand empty (the left on the hilt); 0 none - one-handed, or a barbarian's two-handed
// sword beside another weapon.
// A staff held by the general rule (staff_hand wrist, shaft = bone axis): every hero's, the sorceress's too
// ("make hers like everyone's", 2026-10-06 - her left wrist was a guess from before the rule).
bool StaffByRule() { return gamestate::WeaponType() == D2RVR_TYPE_STAFF; }
int HeldLikeStaff() {
    const uint32_t type = gamestate::WeaponType(), two = gamestate::TwoHanded();
    if (!(two & D2RVR_TWO_HANDS_ON)) return 0;
    // a two-handed axe or mace (maul) has a haft like a polearm's: the left slides along it too
    if ((type == D2RVR_TYPE_SPEAR || type == D2RVR_TYPE_POLEARM || type == D2RVR_TYPE_AXE || type == D2RVR_TYPE_MACE ||
         type == D2RVR_TYPE_OTHER) && g_set.polearmTwoHands.load()) return 1;
    if (type == D2RVR_TYPE_SWORD && g_set.swordTwoHands.load()) return 2;
    return 0;
}
bool StaffAim() {
    // Only with the body (F4): there the controllers hold the weapon and the line
    // between them is its line. Anywhere else the hands are just held, and that
    // line points sideways - a crossbow in third person shot to the left
    // (2026-10-06); there it aims with one hand, like the bow.
    if (!FullBody()) return false;
    const D2RVR_Shared* sh = g_shared;
    // along the staff in the right hand (staff_free_left) too, not only aimed from hand to hand ([bow] staff_two_hands)
    const bool staffNow = (g_set.staffTwoHands.load() || g_set.staffFreeLeft.load()) && gamestate::WeaponType() == D2RVR_TYPE_STAFF;
    if (!(staffNow || XbowLikeStaff()) || !sh || sh->version != D2RVR_SHARED_VERSION) return false;
#if D2RVR_FIRST_PERSON
    if (g_set.staffFreeLeft.load() && ArmsMode() == 2) return (sh->handsValid & 1u) && skel::StaffAxis(nullptr);
#endif
    return (sh->handsValid & 3u) == 3u;
}

bool ShooterActive();
bool HandRay(float* o, float* d) {
    // Mouse look and W A S D (flat first person): no hands - the blow or the
    // shot goes where the camera looks, a little down so it meets the ground.
    if (ShooterActive() && g_eyeOk.load()) {
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        const float fl = sqrtf(f.camF[0] * f.camF[0] + f.camF[2] * f.camF[2]);
        if (!f.valid || fl < 1e-3f) return false;
        const float dw[3] = {f.camF[0] / fl, -0.08f, f.camF[2] / fl};
        const float l = sqrtf(1.0f + 0.08f * 0.08f);
        for (int i = 0; i < 3; ++i) { d[i] = dw[i] / l; o[i] = g_eyeWorld[i].load(); }
        return true;
    }
    const D2RVR_Shared* sh = g_shared;
    const bool staff = StaffAim();
    const int hand = staff ? 0 : AimHand();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand)) || !g_eyeOk.load()) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    const float* p = hand == 0 ? sh->rightHand : sh->leftHand;
    float staffDir[3] = {sh->rightHand[0] - sh->leftHand[0], sh->rightHand[1] - sh->leftHand[1], sh->rightHand[2] - sh->leftHand[2]};
#if D2RVR_FIRST_PERSON
    if (staff && g_set.staffFreeLeft.load() && ArmsMode() == 2) skel::StaffAxis(staffDir);
#endif
    // Both lines run from the left hand toward the right one - a staff's way, the
    // right hand ahead. A crossbow has the left ahead and the right on the stock.
    if (staff && XbowLikeStaff()) for (float& v : staffDir) v = -v;
    auto rot = [&](float vx, float vy, float vz, float* r) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        r[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        r[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        r[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    // The knuckles (-Y) first: round a bow's grip they point at the target however the
    // bow is turned about the arrow, upright or flat. -Z (a controller held like a gun)
    // only when the knuckles point up or down - it ran along the fist of a flat bow.
    // A javelin or a throwing weapon lies along the fist, as the rod through a
    // closed hand: it flies along +Z (its point is on that side - -Z threw it
    // backwards), never where the knuckles point.
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;
    const float rod[3] = {-a[0], -a[1], -a[2]};
#if D2RVR_FIRST_PERSON
    // A crossbow held as the staff shoots along its stock as the model lies (skeletons.cpp
    // GunFrame), like the staff: the model, the shot and the left hand's line are one.
#endif
    const float* h = staff ? staffDir : thrown ? rod : fabsf(b[1]) < 0.7f ? b : a;
    // head frame (x right, y up, z back) -> world: right, up, forward
    float dw[3];
    for (int i = 0; i < 3; ++i) dw[i] = h[0] * f.camR[i] - h[2] * f.camF[i];
    dw[1] += h[1];
    // [bow] aim_yaw: the shots land a little to one side of where the hand points
    // (the controller's axes are not quite the arrow's), so the aim is turned
    // about the vertical by a set angle. On the ground plane the camera spans,
    // so + is to the right as seen in the headset, whatever the grid does.
    // A staff has its own ([bow] staff_aim_yaw): it aims along the shaft, not the knuckles;
    // and a crossbow ([bow] xbow_aim_yaw): held like a gun, not round a bow's grip.
    const float yawDeg = type == D2RVR_TYPE_CROSSBOW ? g_set.xbowAimYawDeg.load() : staff ? g_set.staffAimYawDeg.load() : g_set.aimYawDeg.load();
    if (yawDeg != 0.0f) {
        float F[3] = {f.camF[0], 0.0f, f.camF[2]};
        const float fl = sqrtf(F[0]*F[0] + F[2]*F[2]);
        float R[3] = {f.camR[0], 0.0f, f.camR[2]};
        const float rl = sqrtf(R[0]*R[0] + R[2]*R[2]);
        if (fl > 1e-3f && rl > 1e-3f) {
            for (int i = 0; i < 3; i += 2) { F[i] /= fl; R[i] /= rl; }
            const float vf = dw[0]*F[0] + dw[2]*F[2], vr = dw[0]*R[0] + dw[2]*R[2];
            const float a = yawDeg * 3.14159265f / 180.0f, c = cosf(a), sn = sinf(a);
            const float nf = vf * c - vr * sn, nr = vf * sn + vr * c;
            dw[0] = nf * F[0] + nr * R[0];
            dw[2] = nf * F[2] + nr * R[2];
        }
    }
    // a ray that never comes down never finds the ground: tip it a little
    const float horiz = sqrtf(dw[0]*dw[0] + dw[2]*dw[2]);
    if (horiz < 1e-3f) return false;
    if (dw[1] > -0.08f * horiz) dw[1] = -0.08f * horiz;
    const float l = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
    const float headM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
    const float upm = std::max(1.0f, ViewHeight()) / headM;
    for (int i = 0; i < 3; ++i) {
        d[i] = dw[i] / l;
        o[i] = g_eyeWorld[i].load() + upm * (p[0] * f.camR[i] - p[2] * f.camF[i]);
    }
    o[1] += upm * p[1];
    return true;
}

// Shift goes in as a scan code (left Shift, 0x2A): the game reads keys by scan
// code, and a virtual-key-only Shift never reached it - the click came through
// alone and the hero walked to the point instead of shooting at it (0.19.2 log).
void SendShift(bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_LSHIFT;
    in.ki.wScan = 0x2A;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof in);
}
void SendLeftButton(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof in);
}
// The press in two steps a frame apart, so Shift is already down when the
// click is read: 0 idle, 1 Shift down, 2 Shift and button down.
int g_aimStage = 0;
ULONGLONG g_aimStageAt = 0;
void SendKeysMouse(bool down) {
    if (down) { SendShift(true); g_aimStage = 1; g_aimStageAt = GetTickCount64(); }
    else {
        if (g_aimStage == 2) SendLeftButton(false);
        if (g_aimStage >= 1) SendShift(false);
        g_aimStage = 0;
    }
}

void AimByMouse(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 2) {
        if (g_aimDown) { SendKeysMouse(false); g_aimDown = false; }
        return;
    }
    // In a menu A is the menu's own button: trade, inventory, dialogue.
    const bool held = (s->Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen();
    float o[3], d[3];
    if (held && HandRay(o, d)) {
        for (int i = 0; i < 3; ++i) { g_aimO[i].store(o[i]); g_aimD[i].store(d[i]); }
        g_aimRay.store(true);
        g_aimRayUntil.store(GetTickCount64() + 200);   // the click is read a little after it is sent
        s->Gamepad.wButtons &= ~XINPUT_GAMEPAD_A;
        if (g_aimDown && g_aimStage == 1 && GetTickCount64() - g_aimStageAt >= 20) { SendLeftButton(true); g_aimStage = 2; }
        if (!g_aimDown && GameFocused()) {
            SendKeysMouse(true);
            g_aimDown = true;
            LogF("aim: attack along the %s hand, ray %.2f %.2f %.2f from %.1f %.1f %.1f",
                 AimHand() ? "left" : "right", d[0], d[1], d[2], o[0], o[1], o[2]);
        }
    } else if (g_aimDown) {
        SendKeysMouse(false);
        g_aimDown = false;
    }
    static bool toldWhy = false;
    const bool pressed = (s->Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    if (pressed && !g_aimDown && !toldWhy) {
        toldWhy = true;
        LogF("aim: A left to the game - %s", gamestate::MenuOpen() ? "a menu is open" : "no hand ray (hand not tracked, or no camera yet)");
    } else if (!pressed) toldWhy = false;
}

// Attack along the hand, pad only (bow mode 3). While A is held the game
// attacks where the hero faces and never reads the stick; without A the stick
// turns him at once. So a press of A is held back for turn_ms while the stick
// points along the aiming hand (he turns, maybe half a step), then the stick
// goes quiet and A goes through: the shot or blow leaves the way he now faces.
// Held A keeps attacking that way; when the hand swings more than 25 degrees
// off, A is held back again for another short turn. No mouse or keyboard, so
// the game's UI stays in controller mode (mode 2 flipped it to mouse).
//
// The hand's pointing, flattened, as a stick relative to the view: the grip's
// -Z held like a gun, -Y (wrist to knuckles) round a bow's upright grip.
bool HandStick(float* sx, float* sy) {
    const D2RVR_Shared* sh = g_shared;
    const int hand = AimHand();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand))) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    auto rot = [&](float vx, float vy, float vz, float* r) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        r[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        r[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        r[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    // The knuckles (-Y) first: round a bow's grip they point at the target however the
    // bow is turned about the arrow, upright or flat. -Z (a controller held like a gun)
    // only when the knuckles point up or down - it ran along the fist of a flat bow.
    const float* h = fabsf(b[1]) < 0.7f ? b : a;
    const float x = h[0], fwd = -h[2];   // head frame: x right, z back
    const float l = sqrtf(x*x + fwd*fwd);
    if (!(l > 0.05f)) return false;
    *sx = x / l; *sy = fwd / l;
    return true;
}

struct PreTurn { int stage = 0; ULONGLONG at = 0; float sx = 0, sy = 0; } g_pre;   // stage 0 idle, 1 turning, 2 attacking

void AimByTurn(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 3) { g_pre.stage = 0; return; }
    XINPUT_GAMEPAD& g = s->Gamepad;
    const bool held = (g.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen();
    if (!held) { g_pre.stage = 0; return; }
    float sx, sy;
    const bool hand = HandStick(&sx, &sy);
    const ULONGLONG now = GetTickCount64();
    if (g_pre.stage == 0 || (g_pre.stage == 2 && hand && sx * g_pre.sx + sy * g_pre.sy < 0.906f)) {   // cos 25 deg
        if (!hand) { g_pre.stage = 2; return; }   // no hand to aim with: A as it is
        g_pre = {1, now, sx, sy};
        LogF("aim: turning to the %s hand (stick %.2f %.2f) before the attack", AimHand() ? "left" : "right", sx, sy);
    }
    if (g_pre.stage == 1) {
        if (now - g_pre.at < (ULONGLONG)g_set.turnMs.load()) {
            g.wButtons &= ~XINPUT_GAMEPAD_A;
            g.sThumbLX = (SHORT)std::clamp(g_pre.sx * 32767.0f, -32768.0f, 32767.0f);
            g.sThumbLY = (SHORT)std::clamp(g_pre.sy * 32767.0f, -32768.0f, 32767.0f);
            return;
        }
        g_pre.stage = 2;
    }
    // attacking: the stick stays quiet so he does not walk off the aim
    g.sThumbLX = 0; g.sThumbLY = 0;
}

// The controller's target vector (bow mode 4, and its log in every mode).
//
// ControllerInputHandler builds the point an attack goes to in one function
// (RVA 0x143960, D2RLoader layout): from a start - the hero, or the current
// free-target point - it steps along a direction vector, float x/y in GAME
// (grid) coordinates, up to 11 cells, checking the ground, and writes the
// point to two ints. The vector is the controller manager's slot field +0x195C
// (the left stick turned onto the grid); its callers keep the point at
// manager+0x39BD4. Found 2026-10-02 from the controllersettings.json fields
// freeTargetMovementPerMS / freeTargetStickyPullFactor (manager +0x1F98/+0x1F9C).
//
// Mode 4 hands it the aiming hand's direction instead while A is held. Game
// and world axes differ by a fixed turn (and maybe a mirror), learned while
// the hero walks: the vector the stick gives against the way he then moves.
constexpr uint64_t RVA_ATTACK_TARGET = 0x143960;
const uint8_t kSigAttackTarget[16] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x4C,0x89,0x70,0x20,0x41};
using AttackTargetFn = uint64_t (*)(void* self, void* unit, float* dir, int* outX, int* outY);
AttackTargetFn OrigAttackTarget = nullptr;

std::atomic<bool> g_aHeld{false};            // A is down (and no menu), from the pad
std::atomic<float> g_walkTheta{0.0f};        // the hero's walking direction in the world, atan2(dz, dx)
std::atomic<bool> g_walkSteady{false};
struct Calib { double c1 = 0, s1 = 0, c2 = 0, s2 = 0; int n = 0; float angle = 0; int mirror = 0; bool ok = false; };
SRWLOCK g_calLock = SRWLOCK_INIT;
Calib g_cal;

// The aiming hand's direction on the ground, in world terms: atan2(z, x).
bool HandWorldTheta(float* theta) {
    float o[3], d[3];
    if (!HandRay(o, d)) return false;
    *theta = atan2f(d[2], d[0]);
    return true;
}

// The aiming hand as a unit vector on the game's grid. The grid's axes are the
// world's x and z (measured 2026-10-02: 0 deg, not mirrored, agreement 0.98);
// what the walk teaches overrides that once it is sure.
bool HandGridDir(float* gx, float* gy) {
    float theta;
    if (!HandWorldTheta(&theta)) return false;
    AcquireSRWLockShared(&g_calLock); const Calib c = g_cal; ReleaseSRWLockShared(&g_calLock);
    const float tg = !c.ok ? theta : c.mirror ? c.angle - theta : c.angle + theta;
    *gx = cosf(tg); *gy = sinf(tg);
    return true;
}

// A skill button is down (A, B, X, Y, RB), no menu: the attack or spell it casts goes along the hand.
std::atomic<bool> g_skillHeld{false};
bool AimByVector() { return g_enabled.load() && g_set.bowMode.load() == 4 && (g_aHeld.load() || g_skillHeld.load()); }

// The other half of the controller's aim. With the stick at rest the aim
// function (0x1446C0) takes the attack's direction from the hero's own facing:
// UnitFacingVector (RVA 0x349EF0, (unit, float out[2])), called at 0x14481F.
// For that call only, and only while A is held in mode 4, the answer is the
// aiming hand - the real vector, so turning the hand while firing turns the
// shots, with no step. Everyone else (the other caller, 0x18BD6D) gets the facing.
constexpr uint64_t RVA_UNIT_FACING = 0x349EF0, RVA_AIM_FACING_RET = 0x144824;
const uint8_t kSigUnitFacing[16] = {0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x48};
using UnitFacingFn = void (*)(void* unit, float* out);
UnitFacingFn OrigUnitFacing = nullptr;

// The hero's facing as the game's logic has it (UnitFacingVector's answer for
// the hero, the unit the aim works on), with when it was asked - for the frame
// log: is it this frame's on the first pass of a pair, where the render matrix
// is a frame old, and does it say where the model is drawn facing?
std::atomic<void*> g_heroUnit{nullptr};
// The hero unit found without an attack, for the cave ceiling's torches (worldobj): while
// it is wanted, every unit the game asks the facing of is tried - a player (type 0)
// whose place on its path, times 2, is the look-at (2026-10-07: "on entering the cave").
std::atomic<void*> g_heroSeen{nullptr};
std::atomic<bool> g_heroWanted{false};

bool IsHeroUnit(const void* unit) {
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    __try {
        const uint8_t* u = (const uint8_t*)unit;
        if (*(const uint32_t*)u != 0) return false;
        const uint8_t* path = *(const uint8_t* const*)(u + 0x38);
        if ((uintptr_t)path < 0x10000) return false;
        const float x = 2.0f * *(const uint16_t*)(path + 2), z = 2.0f * *(const uint16_t*)(path + 6);
        return fabsf(x - L[0]) < 3.0f && fabsf(z - L[2]) < 3.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
std::atomic<float> g_logicFacingDeg{0.0f};
std::atomic<LONGLONG> g_logicFacingAt{0};

// The hero's facing asked of the game right now (UnitFacingVector on the hero
// unit), guarded: the unit pointer is the one the aim last worked on, and a
// stale one after an area change must not take the game down. false = none.
bool AskHeroFacingNow(float* deg) noexcept {
    void* unit = g_heroUnit.load();
    if (!unit || !OrigUnitFacing) return false;
    float out[2] = {0.0f, 0.0f};
    __try { OrigUnitFacing(unit, out); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || (out[0] == 0.0f && out[1] == 0.0f)) return false;
    *deg = atan2f(out[1], out[0]) * 57.2957795f;
    return true;
}

LONGLONG QpcUs() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000 / f.QuadPart;
}

void HookUnitFacing(void* unit, float* out) {
    OrigUnitFacing(unit, out);
    if (unit && g_heroWanted.load() && !g_heroSeen.load() && IsHeroUnit(unit)) g_heroSeen.store(unit);   // until one is found
    if (out && unit && unit == g_heroUnit.load()) {
        g_logicFacingDeg.store(atan2f(out[1], out[0]) * 57.2957795f);
        g_logicFacingAt.store(QpcUs());
    }
    if (!out || (uintptr_t)_ReturnAddress() != d2rsig::Addr(RVA_AIM_FACING_RET) || !AimByVector()) return;
    float gx, gy;
    if (!HandGridDir(&gx, &gy)) return;
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 250) {
        told = GetTickCount64();
        LogF("target: attack vector %.2f %.2f -> the hand %.2f %.2f", out[0], out[1], gx, gy);
    }
    out[0] = gx; out[1] = gy;
}

// The point the attack goes to: AttackPoint (RVA 0x18BAD0, (manager, float
// out[2], unit, float2 from, float2 dir)) = from + dir * range, with range the
// default attack distance (~3 cells) or the skill's. The command that carries
// it rounds to whole cells, and at 3 cells that is a 20-degree staircase - the
// shots went in steps, not where the bow pointed (0.22 test). With a bow or
// crossbow in hand, while A is held in mode 4, the point goes [bow] range
// cells out along the hand instead: steps of about 3 degrees, and an arrow
// flies on past its point anyway. A melee weapon keeps the game's point, so
// the hero does not walk off towards a far one.
constexpr uint64_t RVA_ATTACK_POINT = 0x18BAD0, RVA_ATTACK_POINT_RET = 0x14484A;   // the aim's own call returns there
const uint8_t kSigAttackPoint[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
using AttackPointFn = float* (*)(void* mgr, float* out, void* unit, uint64_t from, uint64_t dir);
AttackPointFn OrigAttackPoint = nullptr;

float* HookAttackPoint(void* mgr, float* out, void* unit, uint64_t from, uint64_t dir) {
    float* r = OrigAttackPoint(mgr, out, unit, from, dir);
    const uint32_t w = gamestate::WeaponClass();
    float gx, gy;
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;
    if (!out || !AimByVector() || (w != 3 && w != 4 && !thrown && !StaffAim()) || !HandGridDir(&gx, &gy)) return r;
    float f[2]; memcpy(f, &from, sizeof f);
    // A crossbow or a staff aims finer held in both hands: the point farther out.
    float range = g_set.aimRange.load();
    if (StaffAim()) {
        const bool xbow = type == D2RVR_TYPE_CROSSBOW;
        bool leftOn = true;   // both controllers, the staff between them: always both
#if D2RVR_FIRST_PERSON
        if (g_set.staffFreeLeft.load() && ArmsMode() == 2) skel::StaffAxis(nullptr, nullptr, &leftOn);
#endif
        range = leftOn ? (xbow ? g_set.xbowRangeTwo : g_set.staffRangeTwo).load() : (xbow ? g_set.xbowRangeOne : g_set.staffRangeOne).load();
    }
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 250) {
        told = GetTickCount64();
        LogF("target: attack point %.1f %.1f (%.1f cells out) -> %.1f %.1f along the hand", out[0], out[1],
             sqrtf((out[0]-f[0])*(out[0]-f[0]) + (out[1]-f[1])*(out[1]-f[1])), f[0] + gx * range, f[1] + gy * range);
    }
    out[0] = f[0] + gx * range;
    out[1] = f[1] + gy * range;
    return r;
}

uint64_t HookAttackTarget(void* self, void* unit, float* dir, int* outX, int* outY) {
    if (unit) g_heroUnit.store(unit);
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    float use[2];
    float* d = dir;
    const float gx = dir ? dir[0] : 0.0f, gy = dir ? dir[1] : 0.0f, mag = sqrtf(gx*gx + gy*gy);
    // learn the axes: the stick's grid vector against the way the hero walks
    if (dir && mag > 0.5f && g_walkSteady.load()) {
        const float tg = atan2f(gy, gx), tw = g_walkTheta.load();
        AcquireSRWLockExclusive(&g_calLock);
        Calib& c = g_cal;
        c.c1 += cos(tg - tw); c.s1 += sin(tg - tw); c.c2 += cos(tg + tw); c.s2 += sin(tg + tw); c.n++;
        if (c.n >= 15 && c.n % 15 == 0) {
            const double r1 = sqrt(c.c1*c.c1 + c.s1*c.s1) / c.n, r2 = sqrt(c.c2*c.c2 + c.s2*c.s2) / c.n;
            c.mirror = r2 > r1 ? 1 : 0;
            c.angle = (float)(c.mirror ? atan2(c.s2, c.c2) : atan2(c.s1, c.c1));
            const bool was = c.ok;
            c.ok = std::max(r1, r2) > 0.8;
            if (!was || c.n % 150 == 0)
                LogF("target: grid vs world axes %s, %.1f deg %s (agreement %.2f / %.2f over %d steps)", c.ok ? "LEARNED" : "unclear",
                     c.angle * 57.2957795f, c.mirror ? "mirrored" : "turned", r1, r2, c.n);
        }
        ReleaseSRWLockExclusive(&g_calLock);
    }
    // mode 4: while A is held, the vector points along the aiming hand
    bool turned = false;
    float theta = 0.0f;
    // only the aim function's own call (0x144845, returns to 0x14484A): the
    // walking calls keep the stick, or the hero would walk after the hand
    if (dir && caller == d2rsig::Rva(RVA_ATTACK_POINT_RET) && AimByVector() && HandGridDir(&use[0], &use[1])) {
        d = use;
        turned = true;
    }
    const uint64_t r = OrigAttackTarget(self, unit, d, outX, outY);
    // the log: which caller, what vector, what point - a few times a second per caller
    static uintptr_t lastCaller[4] = {};
    static ULONGLONG lastAt[4] = {};
    int slot = 0;
    for (; slot < 4 && lastCaller[slot] && lastCaller[slot] != caller; ++slot) {}
    if (slot < 4) {
        lastCaller[slot] = caller;
        const ULONGLONG now = GetTickCount64();
        if (now - lastAt[slot] >= (g_aHeld.load() ? 150u : 1000u)) {
            lastAt[slot] = now;
            LogF("target: caller 0x%llX unit %p vector %.2f %.2f%s -> %d %d (A %d, hand %.0f deg)", (unsigned long long)caller, unit, gx, gy,
                 turned ? " TURNED to the hand" : "", outX ? *outX : -1, outY ? *outY : -1, g_aHeld.load() ? 1 : 0,
                 HandWorldTheta(&theta) ? theta * 57.2957795f : -999.0f);
        }
    }
    return r;
}

// What the pad does while shooting, for the log: every change of buttons or
// triggers, and four times a second while the bow aim holds the stick - the
// stick before us, after the bow, and as the game finally gets it.
struct PadTrace { WORD buttons = 0; BYTE lt = 0, rt = 0; ULONGLONG lastAim = 0; };
PadTrace g_padTrace;

// A stick in our view (x right, y ahead, length <= 1) made the same stick on
// the game's screen - the convention of XInput after the game's dead zone, and
// of the keyboard move 0x8A960 (x right, y up): the camera's yaw taken in, or
// with [stick] exact the screen's own axes. False = left as it was (turning
// off, no view yet, or no length).
bool ViewStickToGame(float* px, float* py) {
    if (!g_set.stickRotate.load()) return false;
    const float x = *px, y = *py;
    const float mag = sqrtf(x*x + y*y);
    if (mag < 1e-4f) return false;
    if (g_set.stickExact.load()) {
        // Stick in our view -> direction on the ground -> that direction on the game's screen.
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        if (!f.valid) return false;
        float w[3];
        for (int i = 0; i < 3; ++i) w[i] = x * f.camR[i] + y * f.camF[i];
        float sx = w[0]*f.scrR[0] + w[1]*f.scrR[1] + w[2]*f.scrR[2];
        float sy = w[0]*f.scrU[0] + w[1]*f.scrU[1] + w[2]*f.scrU[2];
        const float sm = sqrtf(sx*sx + sy*sy);
        if (sm < 1e-6f) return false;
        *px = sx * f.mirX * mag / sm; *py = sy * f.mirY * mag / sm;
        return true;
    }
    const float t = g_set.stickSign.load() * g_set.stickAngleScale.load() * g_camYaw.load() * 3.14159265f / 180.0f;
    float rx = x * cosf(t) - y * sinf(t), ry = x * sinf(t) + y * cosf(t);
    ry *= g_set.stickSquash.load();
    const float rm = sqrtf(rx*rx + ry*ry);
    if (rm < 1e-6f) return false;
    *px = rx * mag / rm; *py = ry * mag / rm;
    return true;
}

bool DirectWalkNow();
bool StickHookIn();
void TurnStick(XINPUT_STATE* s, bool fromBodyWalk) {
    if (!s || !g_enabled.load()) return;
    const XINPUT_GAMEPAD in = s->Gamepad;
    // What is kept (held buttons, the right stick, the log) only from BodyWalk's pad while it is live.
    const bool own = fromBodyWalk || GetTickCount64() - g_padMirrorOkAt.load() > 1000;
    if (own) {
        g_aHeld.store((in.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen());
        g_skillHeld.store((in.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0 &&
                          !gamestate::MenuOpen());
    }
    const bool lt = in.bLeftTrigger >= 128, rt = in.bRightTrigger >= 128;
    if (own && (in.wButtons != g_padTrace.buttons || lt != (g_padTrace.lt >= 128) || rt != (g_padTrace.rt >= 128))) {
        LogF("pad: buttons %04X LT %u RT %u stick %d %d", in.wButtons, in.bLeftTrigger, in.bRightTrigger, in.sThumbLX, in.sThumbLY);
        g_padTrace.buttons = in.wButtons; g_padTrace.lt = in.bLeftTrigger; g_padTrace.rt = in.bRightTrigger;
    }
    // A menu (inventory, trade, stash...) gets the pad as it is: the left stick
    // moves its cursor on the screen, so turning it by the camera's yaw sent the
    // cursor the wrong way, and the right stick is the menu's too.
    if (gamestate::MenuOpen()) { if (own) g_rightX.store(0.0f); return; }
    AimByMouse(s);
    AimByTurn(s);
    const bool aimed = BowStick(s);
    struct Out { const XINPUT_GAMEPAD in, bow; XINPUT_STATE* s; bool aimed; ~Out() {
        if (!aimed || GetTickCount64() - g_padTrace.lastAim < 250) return;
        g_padTrace.lastAim = GetTickCount64();
        LogF("bow aim: stick in %d %d, hand %d %d, to the game %d %d (camera yaw %.1f)", in.sThumbLX, in.sThumbLY,
             bow.sThumbLX, bow.sThumbLY, s->Gamepad.sThumbLX, s->Gamepad.sThumbLY, g_camYaw.load());
    } } out{in, s->Gamepad, s, aimed};
    if (g_set.rightTurn.load()) {
        // The right stick is ours while on: it turns the body, the game never sees it.
        if (own) g_rightX.store(s->Gamepad.sThumbRX / 32767.0f);
        s->Gamepad.sThumbRX = 0; s->Gamepad.sThumbRY = 0;
    }
    // Walking straight from BodyWalk (HookStickGet / HookKeyMove): the pad's left
    // stick is kept from the game in the world - it would walk twice, and a stick
    // pushed flips the game's interface to the controller.
    if (DirectWalkNow() && StickHookIn()) { s->Gamepad.sThumbLX = 0; s->Gamepad.sThumbLY = 0; return; }
    float x = s->Gamepad.sThumbLX / 32767.0f, y = s->Gamepad.sThumbLY / 32767.0f;
    if (!ViewStickToGame(&x, &y)) return;
    s->Gamepad.sThumbLX = (SHORT)std::clamp(x * 32767.0f, -32768.0f, 32767.0f);
    s->Gamepad.sThumbLY = (SHORT)std::clamp(y * 32767.0f, -32768.0f, 32767.0f);
}

// BodyWalk's pad, read straight from BodyWalk (pad_mirror_shared.h): the
// report it would hand the virtual Xbox pad, so the game needs neither the
// virtual device nor its driver. Taken for the first pad while BodyWalk keeps
// writing it (its counter moved in the last second), whatever is plugged in.
// Read from the XInput hooks (VR) and from the keyboard move hook on the
// game's thread (flat), so the mapping and the counter's watch are atomics.
std::atomic<const BWPadMirror*> g_padMirror{nullptr};

bool PadMirrorRead(BWPadMirror* out) {
    if (!g_set.bodywalkPad.load()) return false;
    const BWPadMirror* p = g_padMirror.load();
    if (!p) {
        static std::atomic<ULONGLONG> lastTry{0};
        const ULONGLONG now = GetTickCount64();
        ULONGLONG was = lastTry.load();
        if (now - was < 1000 || !lastTry.compare_exchange_strong(was, now)) return false;   // one opener a second
        if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, BW_PAD_MIRROR_NAME)) {
            p = (const BWPadMirror*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(BWPadMirror));
            if (!p) CloseHandle(m);   // else kept: see KeepBlockName
            if (p) { g_padMirror.store(p); Log("vrcam: BodyWalk's pad is read directly - no virtual pad needed"); }
        }
        if (!p) return false;
    }
    static std::atomic<uint32_t> seen{0};
    static std::atomic<ULONGLONG> movedAt{0};
    const BWPadMirror m = *p;
    if (m.counter != seen.load()) { seen.store(m.counter); movedAt.store(GetTickCount64()); }
    if (m.version != BW_PAD_MIRROR_VERSION || GetTickCount64() - movedAt.load() > 1000) return false;
    *out = m;
    return true;
}

bool PadFromBodyWalk(XINPUT_STATE* s) {
    BWPadMirror m;
    if (!PadMirrorRead(&m)) return false;
    s->dwPacketNumber = m.counter;
    s->Gamepad.wButtons = m.buttons;
    s->Gamepad.bLeftTrigger = m.left_trigger;
    s->Gamepad.bRightTrigger = m.right_trigger;
    s->Gamepad.sThumbLX = m.thumb_lx; s->Gamepad.sThumbLY = m.thumb_ly;
    s->Gamepad.sThumbRX = m.thumb_rx; s->Gamepad.sThumbRY = m.thumb_ry;
    g_padMirrorOkAt.store(GetTickCount64());
    return true;
}

// Attack apart from pick-up / interact. Pad A is the default attack, and on a
// target it picks up, opens or talks instead - the game's
// allowInteractOnDefaultAttack (controllersettings.json, read into RVA
// 0x22BD840; asked by 0x145CB0 for skill slot 0 on a pad). With it off, A only
// attacks. The bridge's two actions press A with it off, or on.
constexpr uint64_t RVA_INTERACT_ON_ATTACK = 0x22BD840;
const D2RVR_Actions* g_actions = nullptr;

uint32_t ActionsHeld() {
    if (!g_actions) {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 1000) return 0;
        lastTry = GetTickCount64();
        if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, D2RVR_ACTIONS_NAME)) {
            g_actions = (const D2RVR_Actions*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(D2RVR_Actions));
            if (!g_actions) CloseHandle(m);   // else kept: see KeepBlockName
        }
        if (!g_actions) return 0;
    }
    return g_actions->version == D2RVR_ACTIONS_VERSION ? g_actions->held : 0u;
}

// What the pad would pick up / open / talk to now: the controller's interact
// target, InteractTarget (RVA 0x18D960, (controller manager, player id)),
// which the tooltip panel asks every frame (TooltipsPanel::Draw, 0x880ACE) for
// its "(A) Interact" prompt, and the A-press decision asks through 0x18D370.
// Hooked to remember the answer: "pick up" presses A only while there is a
// target, so it never turns into an attack. With the game's flag off (A only
// attacks) the target is asked again with it on, for us alone - the panel
// keeps its own answer.
using InteractTargetFn = uintptr_t (*)(uintptr_t mgr, uint32_t player);
InteractTargetFn OrigInteractTarget;
constexpr uint64_t RVA_INTERACT_TARGET = 0x18D960;
const uint8_t kSigInteractTarget[16] = {0x40,0x53,0x55,0x56,0x57,0x41,0x55,0x48,0x83,0xEC,0x20,0x4C,0x89,0x64,0x24,0x50};
std::atomic<uintptr_t> g_interactTarget{0};
std::atomic<ULONGLONG> g_interactTargetAt{0};
std::atomic<bool> g_interactHooked{false};   // = g_h.interact, which is declared further down
uintptr_t HookInteractTarget(uintptr_t mgr, uint32_t player);

bool InteractTargetNow() {
    return g_interactHooked.load() && g_interactTarget.load() != 0 && GetTickCount64() - g_interactTargetAt.load() < 200;
}

void ApplyActions(XINPUT_STATE* s) {
    const uint32_t held = ActionsHeld();
    // Pick up only onto a target: nothing there, nothing pressed - never an attack.
    // Without the hook (another build) it cannot tell, and stays a plain A.
    const bool interact = (held & D2RVR_ACT_INTERACT) && (InteractTargetNow() || !g_interactHooked.load());
    static bool wasInteract = false, toldMiss = false;
    if ((held & D2RVR_ACT_INTERACT) && !interact) {
        if (!toldMiss) { toldMiss = true; Log("vrcam: pick up - nothing to pick up or open here, A not pressed"); }
    } else toldMiss = false;
    if (interact != wasInteract) { wasInteract = interact; if (interact) LogF("vrcam: pick up / interact - target %p", (void*)g_interactTarget.load()); }
    if ((held & D2RVR_ACT_ATTACK) || interact) s->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    // A pad button, not the keyboard's I: a key press flips the game's UI to
    // mouse and keyboard, the way Shift + click did in bow mode 2. Menu (Start)
    // by default: View (Back) opens the map in D2R's pad layout - "the inventory
    // button opens the map" (2026-10-05); [input] inventory_button=1 puts it back.
    if (held & D2RVR_ACT_INVENTORY) s->Gamepad.wButtons |= g_set.inventoryPad.load() ? XINPUT_GAMEPAD_BACK : XINPUT_GAMEPAD_START;
    // Weapon swap: the right stick click, which is the game's swap on a pad
    // (log 2026-10-04: buttons 0080 -> "weapon set 2"). A pad button, so the
    // UI stays on the controller.
    if (held & D2RVR_ACT_SWAP) s->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    // The map: a state of ours flipped on the press, never a button sent to the
    // game - a pad press only shuffles the game's map, and a key flips its UI to
    // keyboard. hud.cpp then publishes the map panel shown or hidden.
    static bool mapWas = false;
    if (const bool mapNow = (held & D2RVR_ACT_MAP) != 0; mapNow != mapWas) {
        mapWas = mapNow;
        // The game's own map, opened or closed by its own panel message (no
        // key); the orb follows whatever the game draws. Without the SDK's
        // widget service only the orb can be switched.
        if (mapNow) {
            const bool open = !gamestate::AutoMapOpen();
            if (gamestate::SetAutoMap(open)) LogF("vrcam: map %s in the game (D2R: Map action)", open ? "opening" : "closing");
            else LogF("vrcam: map %s - the orb only, the game's map cannot be set", hud::ToggleMapShown() ? "shown" : "hidden");
        }
    }
    // The game's flag, as it loaded it, kept to put back.
    static int original = -1;
    uint8_t* flag = (uint8_t*)d2rsig::Addr(RVA_INTERACT_ON_ATTACK);
    uint8_t now = 0;
    if (!flag || !SafeRead(&now, flag, 1) || now > 1) return;
    const bool separate = g_set.aAttackOnly.load() || (held & D2RVR_ACT_ATTACK);
    if (original < 0) { if (!separate && !interact) return; original = now; }
    const uint8_t want = interact ? 1 : separate ? 0 : (uint8_t)original;
    if (now != want) {
        *flag = want;
        static uint8_t told = 2;
        if (want != told) { told = want; LogF("vrcam: pad A %s", want ? "picks up / interacts on a target" : "only attacks"); }
    }
}

uintptr_t HookInteractTarget(uintptr_t mgr, uint32_t player) {
    static thread_local bool inside = false;
    const uintptr_t r = OrigInteractTarget(mgr, player);
    if (inside) return r;
    uintptr_t t = r;
    uint8_t* flag = (uint8_t*)d2rsig::Addr(RVA_INTERACT_ON_ATTACK);
    uint8_t f = 1;
    if (flag && SafeRead(&f, flag, 1) && f == 0) {   // A only attacks: ask as if it interacted, for us
        inside = true;
        *flag = 1;
        t = OrigInteractTarget(mgr, player);
        *flag = 0;
        inside = false;
    }
    g_interactTarget.store(t);
    g_interactTargetAt.store(GetTickCount64());
    return r;
}

// Mouse look and W A S D ([input] mouse_look, F9): the first-person game
// without a headset. Live while our camera shows a game area and no menu is open.
std::atomic<bool> g_mouseLookOn{false};
bool ShooterActive() { return g_mouseLookOn.load() && g_enabled.load() && g_inWorld.load() && !gamestate::MenuOpen(); }

// The game's chat line (Enter) is open: then W A S D are letters typed, not
// ours. D2's UI-state array, one byte per panel at 0x2A2ADA0 (GetUIState
// 0xCE500), index 5 = ChatPanel: the HUD's chat key toggles it (0xCDE00(5)),
// the chat's Esc / Enter close it (0xC7D30(5)) - docs/move_recon.md "chat input".
// Both byte checks below pin the address and the index on this build; another
// build reads nothing and keeps the old behaviour.
constexpr uint64_t RVA_GET_UI_STATE = 0xCE500, RVA_HUD_CHAT_CHECK = 0x2E4AA3, RVA_UI_STATES = 0x2A2ADA0;
constexpr int kUiChat = 5;
const uint8_t kSigGetUiState[15] = {0x48,0x63,0xC1,0x48,0x8D,0x0D,0x96,0xC8,0x95,0x02,0x0F,0xB6,0x04,0x08,0xC3};  // movsxd rax, ecx; lea rcx, [0x2A2ADA0]; movzx eax, byte [rax+rcx]; ret
const uint8_t kSigHudChatCheck[10] = {0xB9,0x05,0x00,0x00,0x00,0xE8,0x53,0x9A,0xDE,0xFF};                         // mov ecx, 5; call 0xCE500
std::atomic<ULONGLONG> g_chatEnterAt{0};   // Enter pressed with the line shut: the game may not have opened it yet

bool ChatFlagFound() {
    static std::atomic<int> ok{-1};
    if (ok.load() < 0) {
        const bool found = Matches(RVA_GET_UI_STATE, kSigGetUiState, sizeof kSigGetUiState) &&
                           Matches(RVA_HUD_CHAT_CHECK, kSigHudChatCheck, sizeof kSigHudChatCheck) && d2rsig::Addr(RVA_UI_STATES);
        if (ok.exchange(found ? 1 : 0) < 0)
            LogF(found ? "vrcam: chat line flag found - W A S D go to the game while typing"
                       : "vrcam: chat line flag NOT found on this build - W A S D stay ours while typing");
    }
    return ok.load() == 1;
}

bool ChatFlag() {
    uint8_t v = 0;
    return ChatFlagFound() && SafeRead(&v, (void*)(d2rsig::Addr(RVA_UI_STATES) + kUiChat), 1) && v == 1;
}

bool ChatOpen() {
    const bool open = ChatFlag() || GetTickCount64() - g_chatEnterAt.load() < 250;
    static std::atomic<int> told{-1};
    if (told.exchange(open ? 1 : 0) != (open ? 1 : 0) && ChatFlagFound())
        LogF("vrcam: chat line %s", open ? "open - W A S D go to the game" : "closed - W A S D walk");
    return open;
}

// -- Flat mode without any pad: the game's own keyboard move ------------------
// docs/move_recon.md. In mouse/keyboard mode the per-player controller update
// 0x14C540 takes the walking vector from 0x8A960 (its only caller, 0x14C667):
// a float2 in RAX (low dword x right, high dword y up - the pad stick's
// convention after the game's dead zone), built from the four "Move Up/Right/
// Down/Left" bytes 0x2A23754..57, or 0 when a gate says no (the option
// 0x77D80, no game, [0x2A2347C], the control lock 0x13DE60, the loading
// screen 0xCC1E0, 0x92A10). The vector then runs the very pipe the pad stick
// runs (run hysteresis, the 0x42 command, the stop when it goes back to 0),
// and the UI stays on the mouse: nothing here ever asks for controller mode.
// So in flat mode ([mode] platform=0, [input] flat_keyboard_move=1) W A S D
// (or BodyWalk's stick) go in here, turned by our camera like a pad stick;
// [input] flat_no_pad=1 shows the game no pad at all.
constexpr uint64_t RVA_KEY_MOVE = 0x8A960, RVA_MOVE_KEYS = 0x2A23754;
// sub rsp, 0x48; movaps [rsp+0x30], xmm6; xorps xmm6, xmm6; movaps [rsp+0x20], xmm7 (no rip, no rel32)
const uint8_t kSigKeyMove[17] = {0x48,0x83,0xEC,0x48,0x0F,0x29,0x74,0x24,0x30,0x0F,0x57,0xF6,0x0F,0x29,0x7C,0x24,0x20};
using KeyMoveFn = uint64_t (*)();
KeyMoveFn OrigKeyMove;
std::atomic<int> g_keyMove{0};               // the hook: 0 not in yet, 1 in, 2 not possible (another build) - the old pad way then
std::atomic<uint32_t> g_keyMoveCalls{0};     // how often the game asked: still while W is held = its UI is on the pad

// Flat play through the keyboard move: on while the hook is in or may still go in.
bool FlatKeyMode() { return g_set.platform.load() == 0 && g_set.flatKeyMove.load() && g_keyMove.load() != 2; }
// ... and the game sees no pad at all.
bool FlatNoPad() { return FlatKeyMode() && g_set.flatNoPad.load(); }
// VR third person (F2) played with the mouse and keyboard ([input] vr_keys_walk):
// W A S D walk through the same keyboard move, ahead = where our camera looks.
// With mouse look (F2 starts with it, F9 frees the pointer) it is flat's F2:
// the mouse turns the camera, the pointer is our crosshair in the middle and a
// click goes there (KeyMoveMode below). The game's own Move keys go north on its screen whatever
// the camera does ("W walks backwards when the camera faces south", 2026-10-07).
// The mouse stays the game's: a click attacks, picks up or walks there as ever.
// Only while the game's UI is in mouse mode - it asks 0x8A960 only then; a pad
// stick (BodyWalk's) flips it to the controller until the next click.
bool VrKeyWalk() {
    return g_set.platform.load() == 1 && g_set.vrKeyWalk.load() && g_keyMove.load() != 2 && g_enabled.load() &&
           g_inWorld.load() && ViewNow() == 2;
}
// W A S D as the keyboard move wants them, turned by our camera; false = nothing held.
bool VrKeyInput(float* px, float* py) {
    if (!GameFocused() || ChatOpen()) return false;
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    float y = (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
    float x = (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
    if (x == 0.0f && y == 0.0f) return false;
    if (x != 0.0f && y != 0.0f) { x *= 0.70710678f; y *= 0.70710678f; }
    ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}
// The game in mouse mode with W A S D through its keyboard move: flat, or VR F2.
// With mouse look on top the mouse is the game's, held in the middle (our
// crosshair, a click goes where the camera looks); the old pad stick
// (KeysAsStick) only where neither is.
bool KeyMoveMode() { return FlatKeyMode() || VrKeyWalk(); }

// VR walk straight from BodyWalk ([input] direct_walk): its stick, read from its
// own report (pad_mirror_shared.h), goes into the two places the game takes the
// walking vector from - 0x8A960 in mouse mode (HookKeyMove) and the walk's call of
// ControllerInputHandler's stick getter 0x13CF10 in controller mode (HookStickGet)
// - so the game's pad left stick is not needed and is kept from it in the world.
// Our camera's views only (F2-F4: the stick turned like any other), no menu, no chat.
std::atomic<int> g_stickHook{0};   // HookStickGet: 0 not in yet, 1 in, 2 not possible
bool DirectWalkNow() {
    return g_set.platform.load() == 1 && g_set.directWalk.load() && g_enabled.load() && g_inWorld.load() &&
           !gamestate::MenuOpen() && !ChatOpen();
}
// BodyWalk's stick as the game takes a walking vector (x right, y up on its screen,
// length <= 1, its dead zone 10349 of 32767 with no rescale), turned by our camera.
bool BodyWalkWalk(float* px, float* py) {
    BWPadMirror m;
    if (!PadMirrorRead(&m)) return false;
    const float lx = (float)m.thumb_lx, ly = (float)m.thumb_ly;
    if (lx * lx + ly * ly <= 10349.0f * 10349.0f) return false;
    float x = std::clamp(lx / 32767.0f, -1.0f, 1.0f), y = std::clamp(ly / 32767.0f, -1.0f, 1.0f);
    ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}

// What the player asks for now, as the game's keyboard move wants it (x right,
// y up on the game's screen, length <= 1); false = nothing (the game then
// stops the hero itself). W A S D while our mouse look is on, the window in
// front and the chat line shut, relative to the camera; else BodyWalk's stick,
// with the game's own dead zone (0x1210DA0: 10349, no rescale), relative to
// the camera while ours is on.
bool FlatMoveInput(float* px, float* py, const char** from) {
    float x = 0.0f, y = 0.0f;
    if (ShooterActive() && GameFocused() && !ChatOpen()) {
        auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        y = (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
        x = (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
        if (x != 0.0f && y != 0.0f) { x *= 0.70710678f; y *= 0.70710678f; }
        *from = "keys";
    }
    if (x == 0.0f && y == 0.0f) {
        BWPadMirror m;
        if (!PadMirrorRead(&m)) return false;
        const float lx = (float)m.thumb_lx, ly = (float)m.thumb_ly;
        if (lx * lx + ly * ly <= 10349.0f * 10349.0f) return false;
        x = std::clamp(lx / 32767.0f, -1.0f, 1.0f);
        y = std::clamp(ly / 32767.0f, -1.0f, 1.0f);
        *from = "BodyWalk stick";
    }
    if (g_enabled.load()) ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}

// The game's gates are its own: the Up byte set for the call, the original
// run, the bytes put back. Its answer non-zero = every gate passed (it also
// marks "moved by keys", 0x2A23745, which the game's stop reads) - then our
// vector goes back instead. With no input the original runs untouched.
uint64_t HookKeyMove() {
    g_keyMoveCalls.fetch_add(1, std::memory_order_relaxed);
    // A move key let go while another window was in front (the settings window, its
    // sliders) never reached the game: its Move byte stayed set and the hero walked on
    // by himself (2026-10-07). With the game not in front its move bytes are cleared;
    // a key still held sets them again as soon as it is back.
    if ((FlatKeyMode() || VrKeyWalk()) && !GameFocused()) {
        if (volatile uint8_t* k = (volatile uint8_t*)d2rsig::Addr(RVA_MOVE_KEYS)) k[0] = k[1] = k[2] = k[3] = 0;
    }
    float x = 0.0f, y = 0.0f;
    const char* from = "";
    bool input = false;
    if (FlatKeyMode()) input = FlatMoveInput(&x, &y, &from);
    else if (VrKeyWalk() && VrKeyInput(&x, &y)) { input = true; from = "VR keys"; }
    else if (DirectWalkNow() && BodyWalkWalk(&x, &y)) { input = true; from = "BodyWalk, straight"; }
    if (!input) return OrigKeyMove();
    volatile uint8_t* keys = (volatile uint8_t*)d2rsig::Addr(RVA_MOVE_KEYS);
    if (!keys) return OrigKeyMove();
    const uint8_t keep[4] = {keys[0], keys[1], keys[2], keys[3]};
    keys[0] = 1; keys[1] = 0; keys[2] = 0; keys[3] = 0;
    const uint64_t r = OrigKeyMove();
    for (int i = 0; i < 4; ++i) keys[i] = keep[i];
    const bool ok = r != 0;
    static ULONGLONG lastLog = 0;
    if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 1000) {
        lastLog = now;
        LogF("vrcam: flat move v=(%.2f,%.2f) gate=%s (%s)", x, y, ok ? "ok" : "blocked", from);
    }
    if (!ok) return r;
    uint64_t out = 0;
    const float v[2] = {x, y};
    memcpy(&out, v, sizeof out);
    return out;
}

// Only in flat mode with the key on: VR never has this hook. Tried twice a
// second; the page decrypts the first time the game runs it (in a game area,
// in mouse mode), so only 10 s in a game area without the bytes means another build.
void InstallKeyMoveHook() {
    static int misses = 0;
    // Flat with flat_keyboard_move, or VR with vr_keys_walk (third person, VrKeyWalk).
    const bool flat = g_set.platform.load() == 0;
    if (!g_ctx || g_keyMove.load() != 0 || !(flat ? g_set.flatKeyMove.load() : g_set.vrKeyWalk.load() || g_set.directWalk.load())) return;
    if (!Matches(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove)) {
        // In VR the game may stay on the pad for a long time, its keyboard move never
        // run and so never decrypted: no verdict there, only tried again.
        if (flat && g_inWorld.load() && ++misses >= 20) {
            g_keyMove.store(2);
            Log("vrcam: flat: keyboard move hook NOT possible (0x8A960 not as expected - another build?) - W A S D walk the old way, through a pad");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove, (void*)&HookKeyMove, (void**)&OrigKeyMove);
    g_keyMove.store(in ? 1 : 2);
    if (flat)
        Log(in ? "vrcam: flat: keyboard move hook in - W A S D walk through the game's own keyboard move, the mouse clicks where you look"
               : "vrcam: flat: keyboard move hook FAILED - W A S D walk the old way, through a pad");
    else
        Log(in ? "vrcam: VR: keyboard move hook in - in third person (F2) W A S D walk where the camera looks"
               : "vrcam: VR: keyboard move hook FAILED - W A S D in third person stay the game's own keys");
}

// ControllerInputHandler's walking stick, 0x13CF10(handler, float2* out, player):
// copies the stick its events left (handler + 0x20 + player * 0x1C8) into out and
// returns out. Six callers (the walk, the attack's target, panels); only the walk's
// (0x14C642, returning to 0x14C647) gets BodyWalk's stick instead - nothing while it
// is in its dead zone, so the pad's stick never walks on its own.
constexpr uint64_t RVA_STICK_GET = 0x13CF10, RVA_WALK_STICK_RET = 0x14C647;
// mov [rsp+8], rbx; mov [rsp+0x18], rsi; push rdi; sub rsp, 0x20; mov edi, r8d (no rip, no rel32)
const uint8_t kSigStickGet[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
using StickGetFn = float* (*)(void* handler, float* out, uint32_t player);
StickGetFn OrigStickGet;
bool StickHookIn() { return g_stickHook.load() == 1; }

float* HookStickGet(void* handler, float* out, uint32_t player) {
    float* r = OrigStickGet(handler, out, player);
    if (r && (uintptr_t)_ReturnAddress() == d2rsig::Addr(RVA_WALK_STICK_RET) && DirectWalkNow()) {
        float x = 0.0f, y = 0.0f;
        const bool on = BodyWalkWalk(&x, &y);
        r[0] = on ? x : 0.0f;
        r[1] = on ? y : 0.0f;
        static ULONGLONG lastLog = 0;
        if (on && GetTickCount64() - lastLog >= 2000) {
            lastLog = GetTickCount64();
            LogF("vrcam: walk straight from BodyWalk (controller mode) v=(%.2f,%.2f)", x, y);
        }
    }
    return r;
}

// VR with direct_walk; tried with the others twice a second. The code decrypts the
// first time it runs (in controller mode), so no "not possible" verdict by time.
void InstallStickHook() {
    if (!g_ctx || g_stickHook.load() != 0 || g_set.platform.load() != 1 || !g_set.directWalk.load()) return;
    if (!Matches(RVA_STICK_GET, kSigStickGet, sizeof kSigStickGet) || !d2rsig::Addr(RVA_WALK_STICK_RET)) return;
    const bool in = d2rsig::Hook(RVA_STICK_GET, kSigStickGet, sizeof kSigStickGet, (void*)&HookStickGet, (void**)&OrigStickGet);
    g_stickHook.store(in ? 1 : 2);
    Log(in ? "vrcam: VR: walking stick hook in - BodyWalk's stick walks the hero straight, the pad's left stick is kept from the game"
           : "vrcam: VR: walking stick hook FAILED - the hero walks through the pad's left stick as before");
}

// 0xFE3B0(unit, type, x, y, flags) - the game's click on the map (move_recon.md
// §5): type 0/1 the left button pressed/held, 3/4 the right; the 5th argument
// is 0x8AFD0's word, its low byte "Stand Still" (-> 0x20, attack in place),
// the high byte Run. Inside, the target is 0xF1900(0x9A820(unit)) - the unit
// under the pointer, null on bare ground - and a click with no target and no
// Stand Still is a walk there (0x101EF0). Flat with mouse look: the pointer
// is the crosshair, so with no target the click gets Stand Still - the skill
// goes off at the crosshair (an arrow, a swing, a spell) and the hero stays;
// with a target, the game's own click (attack, pick up, door, NPC).
constexpr uint64_t RVA_MAP_CLICK = 0xFE3B0, RVA_CLIENT_INDEX = 0x9A820, RVA_HOVER_UNIT = 0xF1900;
// mov [rsp+0x10], rbx; push rbp/rsi/rdi/r12..r15 (no rip, no rel32)
const uint8_t kSigMapClick[16] = {0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
const uint8_t kSigClientIndex[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20};
const uint8_t kSigHoverUnit[12] = {0x40,0x53,0x55,0x56,0x48,0x83,0xEC,0x30,0x8B,0xF1,0x8B,0xD9};
using MapClickFn = uint64_t (*)(void* unit, int type, int x, int y, uint64_t flags);
using ClientIndexFn = int (*)(void* unit);
using HoverUnitFn = void* (*)(int index);
MapClickFn OrigMapClick;
std::atomic<int> g_mapClick{0};   // 0 not in yet, 1 in, 2 not possible

uint64_t HookMapClick(void* unit, int type, int x, int y, uint64_t flags) {
    if (unit && (type == 0 || type == 1 || type == 3 || type == 4) && !(flags & 0xFF) &&
        g_set.flatClickShoot.load() && KeyMoveMode() && ShooterActive()) {
        const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(unit);
        if (idx >= 0 && idx < 8 && !((HoverUnitFn)d2rsig::Addr(RVA_HOVER_UNIT))(idx)) {
            flags |= 1;
            static ULONGLONG lastLog = 0;
            if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 2000) {
                lastLog = now;
                LogF("vrcam: flat click type %d, no target - a shot at the crosshair (Stand Still)", type);
            }
        }
    }
    return OrigMapClick(unit, type, x, y, flags);
}

// Like the keyboard move: flat only, tried twice a second until the pages are
// decrypted (the first click in a game area), 10 s in a game area without the
// bytes = another build, and clicks stay the game's own.
void InstallMapClickHook() {
    static int misses = 0;
    // Flat with flat_keyboard_move, or VR with vr_keys_walk (F2's mouse look shoots the same way).
    const bool flat = g_set.platform.load() == 0;
    if (!g_ctx || g_mapClick.load() != 0 || !(flat ? g_set.flatKeyMove.load() : g_set.vrKeyWalk.load())) return;
    if (!Matches(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick) || !Matches(RVA_CLIENT_INDEX, kSigClientIndex, sizeof kSigClientIndex) ||
        !Matches(RVA_HOVER_UNIT, kSigHoverUnit, sizeof kSigHoverUnit)) {
        if (flat && g_inWorld.load() && ++misses >= 20) {   // VR: no verdict, the code may run (and decrypt) much later
            g_mapClick.store(2);
            Log("vrcam: flat: click hook NOT possible (0xFE3B0 / 0x9A820 / 0xF1900 not as expected - another build?) - a click on the ground walks");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick, (void*)&HookMapClick, (void**)&OrigMapClick);
    g_mapClick.store(in ? 1 : 2);
    Log(in ? "vrcam: flat: click hook in - with no target under the crosshair a click is a shot there, never a walk"
           : "vrcam: flat: click hook FAILED - a click on the ground walks");
}

// Twice a second: says when the game loses or gets back its pads, and when W A
// S D are held but the game never asks for a keyboard move (its UI is on a pad:
// the "controller" option, or a pad's press - a mouse click asks for the mouse).
void FlatPadTick() {
    static int told = -1;
    const int now = FlatNoPad() ? 1 : 0;
    if (now != told) {
        if (now == 1) Log("vrcam: flat: no pad for the game (W A S D and the mouse; [input] flat_no_pad=0 lets a real pad through)");
        else if (told == 1) Log("vrcam: flat: the game sees its pads again");
        told = now;
    }
    static uint32_t lastCalls = 0;
    static ULONGLONG lastTold = 0;
    const uint32_t calls = g_keyMoveCalls.load();
    const bool keys = (GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') | GetAsyncKeyState('D')) & 0x8000;
    if (g_keyMove.load() == 1 && ((FlatKeyMode() && ShooterActive()) || VrKeyWalk()) && GameFocused() && keys && calls == lastCalls &&
        GetTickCount64() - lastTold > 10000) {
        lastTold = GetTickCount64();
        Log("vrcam: flat: W A S D held, but the game does not ask for a keyboard move - its UI is in controller mode "
            "(a mouse click switches it back; or the game's Options: controller off)");
    }
    lastCalls = calls;
}

// W A S D as a left stick relative to the view (up = ahead); TurnStick then turns
// it onto the game's screen like any other stick. With no pad plugged in, the
// game gets a pad that has only this stick.
// The pad is there the whole time the shooter controls are: one that came and
// went with each key press was never taken up by the game. The left mouse
// button is A - the blow, the shot or the action, where the camera looks
// (HandRay, [bow] mode 4) - never a walk to the pointer: the mouse itself
// never reaches the game (GameWndProc), which so stays in controller mode,
// its pointer hidden.
// Flat mode with the keyboard move (FlatKeyMode) has no such pad: kept only
// for another game build, where the keyboard move hook cannot go in.
void KeysAsStick(XINPUT_STATE* s, DWORD* r) {
    if (KeyMoveMode() || !ShooterActive() || !GameFocused()) return;
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    if (*r != ERROR_SUCCESS) { memset(s, 0, sizeof *s); *r = ERROR_SUCCESS; }
    const int swap = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;   // the primary button
    if (held(swap)) s->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    const bool typing = ChatOpen();   // then W A S D are letters for the chat line
    const float ahead = typing ? 0.0f : (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
    const float side = typing ? 0.0f : (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
    if (ahead != 0.0f || side != 0.0f) {
        const float k = 32767.0f / sqrtf(ahead * ahead + side * side);
        s->Gamepad.sThumbLX = (SHORT)(side * k);
        s->Gamepad.sThumbLY = (SHORT)(ahead * k);
    }
    static DWORD packet = 0;
    s->dwPacketNumber = ++packet;
}

// xinput9_1_0's XInputGetState calls into xinput1_4's, and both are hooked.
// Only the outermost call on a thread touches the state: done at both levels
// the left stick was turned twice and the inner call's zeroing of the right
// stick reached the outer one, which then read the turn as 0.
thread_local int t_xDepth = 0;

template <int I> DWORD WINAPI XDetour(DWORD idx, XINPUT_STATE* s) {
    g_xhooks[I].calls++;
    // Flat without a pad: no controller anywhere, so no pad event ever asks
    // the game for its controller UI (docs/move_recon.md §6).
    if (FlatNoPad()) {
        if (s) memset(s, 0, sizeof *s);
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    ++t_xDepth;
    DWORD r = g_xhooks[I].orig(idx, s);
    --t_xDepth;
    // Flat with a real pad let through (flat_no_pad=0): that pad only, its stick
    // turned by the camera below; BodyWalk's stick walks through the keyboard move.
    bool mirror = false;
    if (t_xDepth == 0 && idx == 0 && s && !FlatKeyMode() && PadFromBodyWalk(s)) { r = ERROR_SUCCESS; mirror = true; }
    if (t_xDepth == 0 && idx == 0 && s) KeysAsStick(s, &r);
    if (r == ERROR_SUCCESS && t_xDepth == 0) {
        if (idx == 0) ApplyActions(s);
        {   // which entry polls what, with BodyWalk's pad or without (2026-10-07): at most 30 lines a session
            static std::atomic<int> lines{0};
            static ULONGLONG told[std::size(g_xhooks)] = {};
            if (lines.load() < 30 && GetTickCount64() - told[I] > 2000) {
                told[I] = GetTickCount64();
                lines.fetch_add(1);
                LogF("pad diag: %ls!%s pad %u, BodyWalk's pad %s, buttons %04X, right X %d, thread %lu", g_xhooks[I].dll,
                     g_xhooks[I].name, idx, mirror ? "yes" : "no", s->Gamepad.wButtons, s->Gamepad.sThumbRX, GetCurrentThreadId());
            }
        }
        TurnStick(s, mirror);
    }
    return r;
}
void* const kXDetours[] = {(void*)&XDetour<0>, (void*)&XDetour<1>, (void*)&XDetour<2>, (void*)&XDetour<3>, (void*)&XDetour<4>};

// XInput DLLs are often loaded late, so this runs until each one is in.
void HookXInput() {
    for (size_t i = 0; i < std::size(g_xhooks); ++i) {
        XHook& h = g_xhooks[i];
        if (h.done) continue;
        HMODULE m = GetModuleHandleW(h.dll);
        if (!m) continue;
        h.done = true;   // one attempt per export
        void* fn = (void*)GetProcAddress(m, h.ordinal ? MAKEINTRESOURCEA(h.ordinal) : h.name);
        char b[160];
        if (!fn) { snprintf(b, sizeof b, "vrcam: %ls has no %s", h.dll, h.name); Log(b); continue; }
        const bool ok = MH_CreateHook(fn, kXDetours[i], (void**)&h.orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
        snprintf(b, sizeof b, "vrcam: %ls!%s %s", h.dll, h.name, ok ? "hooked" : "hook FAILED");
        Log(b);
    }
}

// Logs once which XInput entry the game actually polls - the proof the stick path works.
void ReportXInputUse() {
    static bool told[std::size(g_xhooks)] = {};
    for (size_t i = 0; i < std::size(g_xhooks); ++i)
        if (!told[i] && g_xhooks[i].calls.load()) {
            told[i] = true;
            char b[128]; snprintf(b, sizeof b, "vrcam: game polls %ls!%s", g_xhooks[i].dll, g_xhooks[i].name); Log(b);
        }
}

// ---- Mouse and keys ----------------------------------------------------------
bool GameFocused() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return fg && pid == GetCurrentProcessId();
}

// The game's main window: its biggest visible top-level window.
HWND g_gameWnd = nullptr;
WNDPROC g_gameWndProc = nullptr;

BOOL CALLBACK FindGameWindow(HWND h, LPARAM best) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT rc{};
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) || !GetClientRect(h, &rc)) return TRUE;
    HWND* out = (HWND*)best;
    RECT cur{};
    if (!*out || (GetClientRect(*out, &cur) && (rc.right * rc.bottom > cur.right * cur.bottom))) *out = h;
    return TRUE;
}

// The crosshair in place of the game's gauntlet (flat mouse look). D2R's
// pointer is a plain Windows cursor (its own HCURSORs through SetCursor,
// GetCursorInfo showing, 2026-10-06), so the swap is the cursor itself:
// user32!SetCursor hooked, the game's handle replaced by ours while the
// crosshair is wanted, and WM_SETCURSOR answered with it. Hot spot in the
// middle - the window's middle, where MouseTick keeps the pointer and every
// click lands. The picture: tools/gen_crosshair.py (OpenAI), 32/48/64 px by
// the window's height. Off ([input] flat_crosshair=0, F9, a panel open, F1):
// the game's own pointer at its next SetCursor.
// The player's own picture: [input] crosshair, like the skies - a bare name
// under reshade-shaders\Textures, or a full path; empty or unreadable = the
// built-in one. Fitted into the square, its middle the hot spot; a picture
// with no transparency at all has its black made see-through (as ours was
// drawn). Built on the update thread (WIC) whenever the key changes.
using SetCursorFn = HCURSOR (WINAPI*)(HCURSOR);
SetCursorFn OrigSetCursor;
std::atomic<HCURSOR> g_cross[std::size(crosshair::kSizes)] = {};
std::atomic<bool> g_crossHooked{false};

bool CrosshairNow() {
    return g_set.flatCrosshair.load() && KeyMoveMode() && ShooterActive() && (g_set.platform.load() == 0 || g_set.crosshairOn.load());
}
// VR F2 with mouse look and [hud_third] crosshair=0: no pointer in the headset at all.
bool CrosshairHidden() { return g_set.platform.load() == 1 && ViewNow() == 2 && KeyMoveMode() && ShooterActive() && !g_set.crosshairOn.load(); }

HCURSOR MakeCrosshair(const crosshair::Size& s) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi; bi.bV5Width = s.side; bi.bV5Height = -s.side; bi.bV5Planes = 1; bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    memcpy(bits, s.bgra, (size_t)s.side * s.side * 4);
    HBITMAP mask = CreateBitmap(s.side, s.side, 1, 1, nullptr);   // all 0: the alpha decides
    ICONINFO ii{FALSE, (DWORD)(s.side / 2), (DWORD)(s.side / 2), mask, color};
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return c;
}

// The player's picture as side x side BGRA (straight alpha), fitted and centred; false = cannot read it.
bool ReadCrosshairFile(const std::wstring& path, int side, std::vector<uint8_t>* out) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
        const UINT fw = std::max(1u, (UINT)((uint64_t)side * w / std::max(w, h))), fh = std::max(1u, (UINT)((uint64_t)side * h / std::max(w, h)));
        std::vector<uint8_t> fit((size_t)fw * fh * 4);
        if (SUCCEEDED(scaler->Initialize(conv, fw, fh, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(scaler->CopyPixels(nullptr, fw * 4, (UINT)fit.size(), fit.data()))) {
            bool opaque = true;
            for (size_t i = 3; i < fit.size(); i += 4) if (fit[i] < 250) { opaque = false; break; }
            out->assign((size_t)side * side * 4, 0);
            const UINT ox = (side - fw) / 2, oy = (side - fh) / 2;
            for (UINT y = 0; y < fh; ++y)
                for (UINT x = 0; x < fw; ++x) {
                    const uint8_t* p = &fit[((size_t)y * fw + x) * 4];
                    uint8_t* q = &(*out)[((size_t)(oy + y) * side + ox + x) * 4];
                    float a = p[3] / 255.0f;
                    if (opaque) a = std::clamp((std::max({p[0], p[1], p[2]}) / 255.0f - 0.06f) / 0.5f, 0.0f, 1.0f);
                    const float k = opaque ? 1.0f / std::max(a, 1e-3f) : 1.0f / std::max(p[3] / 255.0f, 1e-3f);   // straight colour
                    for (int c = 0; c < 3; ++c) q[c] = (uint8_t)std::min(255.0f, p[c] * k + 0.5f);
                    q[3] = (uint8_t)(a * 255.0f + 0.5f);
                }
            ok = true;
        }
    }
    for (IUnknown* u : {(IUnknown*)scaler, (IUnknown*)conv, (IUnknown*)frame, (IUnknown*)dec, (IUnknown*)wic}) if (u) u->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

SRWLOCK g_crossLock = SRWLOCK_INIT;
std::wstring g_crossFile;              // [input] crosshair as written
std::atomic<uint32_t> g_crossGen{1};   // bumped when it changes; CrosshairTick rebuilds
std::atomic<HCURSOR> g_crossVr{nullptr};   // VR: one more, at [hud_third] crosshair_size (CrosshairTick)

// The size the window's height picks: 32 px below 900, 48 below 1500, 64 above.
int CrosshairSizeIndex() {
    RECT rc{};
    const int h = g_gameWnd && GetClientRect(g_gameWnd, &rc) ? rc.bottom : 1080;
    return h < 900 ? 0 : h < 1500 ? 1 : 2;
}

// The built-in picture at any size: each output pixel the average of the source
// pixels it covers (straight alpha, colour weighted by it).
std::vector<uint8_t> ScaledBuiltin(int side) {
    const crosshair::Size& s = crosshair::kSizes[std::size(crosshair::kSizes) - 1];
    std::vector<uint8_t> out((size_t)side * side * 4, 0);
    const float k = (float)s.side / (float)side;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const int x0 = (int)(x * k), x1 = std::max(x0 + 1, (int)((x + 1) * k)), y0 = (int)(y * k), y1 = std::max(y0 + 1, (int)((y + 1) * k));
            float c[3] = {}, a = 0.0f;
            int n = 0;
            for (int sy = y0; sy < std::min(y1, s.side); ++sy)
                for (int sx = x0; sx < std::min(x1, s.side); ++sx, ++n) {
                    const uint8_t* p = &s.bgra[((size_t)sy * s.side + sx) * 4];
                    const float pa = p[3] / 255.0f;
                    for (int i = 0; i < 3; ++i) c[i] += p[i] * pa;
                    a += pa;
                }
            uint8_t* q = &out[((size_t)y * side + x) * 4];
            if (n && a > 1e-4f) for (int i = 0; i < 3; ++i) q[i] = (uint8_t)std::min(255.0f, c[i] / a + 0.5f);
            q[3] = n ? (uint8_t)(a / n * 255.0f + 0.5f) : 0;
        }
    return out;
}

void LoadCrosshairFile() {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"input", L"crosshair", L"D2R_Sky_ours\\D2R_Crosshair.png", buf, MAX_PATH, g_iniPath);
    std::wstring f = buf;
    while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
    while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
    AcquireSRWLockExclusive(&g_crossLock);
    const bool changed = f != g_crossFile;
    g_crossFile = f;
    ReleaseSRWLockExclusive(&g_crossLock);
    if (changed) g_crossGen.fetch_add(1);
}

// The update thread: the three sizes again when the picture changed. The old
// handles are left alone (one may be the cursor on screen right now).
void CrosshairTick() {
    static uint32_t built = 0;
    static int builtVrSide = 0;
    const uint32_t gen = g_crossGen.load();
    if (g_set.platform.load() != 0 && !g_set.vrKeyWalk.load()) return;   // flat, or VR F2's mouse look
    // VR: the window's size times [hud_third] crosshair_size
    const int vrSide = g_set.platform.load() == 1
        ? std::clamp((int)lroundf(crosshair::kSizes[CrosshairSizeIndex()].side * g_set.crosshairSize.load() * 0.01f), 8, 256) : 0;
    if (gen == built && vrSide == builtVrSide) return;
    const bool all = gen != built;
    built = gen;
    builtVrSide = vrSide;
    AcquireSRWLockShared(&g_crossLock); std::wstring f = g_crossFile; ReleaseSRWLockShared(&g_crossLock);
    std::wstring full = f;
    if (!f.empty() && f.find(L':') == std::wstring::npos && f.rfind(L"\\\\", 0) != 0) {
        wchar_t dir[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
        while (n && dir[n - 1] != L'\\') --n;
        dir[n] = 0;
        full = std::wstring(dir) + L"reshade-shaders\\Textures\\" + f;
    }
    if (vrSide) {
        std::vector<uint8_t> px;
        HCURSOR c = nullptr;
        if (!full.empty() && ReadCrosshairFile(full, vrSide, &px)) c = MakeCrosshair({vrSide, px.data()});
        if (!c) { px = ScaledBuiltin(vrSide); c = MakeCrosshair({vrSide, px.data()}); }
        g_crossVr.store(c);
        LogF("vrcam: VR: crosshair %d px (%.0f%%)", vrSide, g_set.crosshairSize.load());
    }
    if (!all) return;
    bool own = false;
    for (size_t k = 0; k < std::size(crosshair::kSizes); ++k) {
        const int side = crosshair::kSizes[k].side;
        std::vector<uint8_t> px;
        HCURSOR c = nullptr;
        if (!full.empty() && ReadCrosshairFile(full, side, &px)) { c = MakeCrosshair({side, px.data()}); own = c != nullptr; }
        if (!c) c = MakeCrosshair(crosshair::kSizes[k]);
        g_cross[k].store(c);
    }
    if (own) LogF("vrcam: flat: crosshair from '%s'", Utf8(full.c_str()).c_str());
    else if (f.empty()) Log("vrcam: flat: the built-in crosshair");
    else LogF("vrcam: flat: crosshair '%s' NOT readable - the built-in one", Utf8(full.c_str()).c_str());
}

// The size for the game window's height (CrosshairSizeIndex); in VR the one at [hud_third] crosshair_size.
HCURSOR Crosshair() {
    if (g_set.platform.load() == 1)
        if (HCURSOR c = g_crossVr.load()) return c;
    return g_cross[CrosshairSizeIndex()].load();
}

HCURSOR WINAPI HookSetCursor(HCURSOR c) {
    if (c && CrosshairNow())
        if (HCURSOR x = Crosshair()) c = x;
    return OrigSetCursor(c);
}

// Only in flat mode; once (user32 is there from the start).
void HookCursor() {
    if (g_crossHooked.load() || (g_set.platform.load() != 0 && !g_set.vrKeyWalk.load())) return;   // flat, or VR F2's mouse look
    g_crossHooked.store(true);
    void* fn = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetCursor");
    const bool ok = fn && MH_CreateHook(fn, (void*)&HookSetCursor, (void**)&OrigSetCursor) == MH_OK && MH_EnableHook(fn) == MH_OK;
    Log(ok ? "vrcam: flat: user32!SetCursor hooked - the crosshair in place of the gauntlet while the mouse looks"
           : "vrcam: flat: user32!SetCursor hook FAILED - the game's own pointer stays");
}

// W A S D are ours while the shooter controls are on (the game has skills
// and panels on them) - but not while the chat line is open: then they are
// letters, and every other key goes to the game anyway.
LRESULT CALLBACK GameWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    // F1..F3 (F4 in VR) pick the view: the game, which has skills on them, never sees them.
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && w >= VK_F1 && (int)w < VK_F1 + ViewCount()) return 0;
    // Enter opening the chat line: its letters may come before the game has
    // set its flag, so they are let through for a moment (not on the Enter
    // that sends the line - W pressed right after it walks).
    if (msg == WM_KEYDOWN && w == VK_RETURN && !(l & (1 << 30)) && (ShooterActive() || VrKeyWalk()))
        g_chatEnterAt.store(ChatFlagFound() && !ChatFlag() ? GetTickCount64() : 0);
    // Still swallowed with the keyboard move (FlatKeyMode): HookKeyMove's vector
    // replaces 0x8A960's answer, never adds to it, so a W A S D the player bound
    // to the game's own Move keys would change nothing - but by default the game
    // has other commands on them (W swaps weapons), which would fire with every step.
    // A key's release always goes through: pressed in F1 / F2 (the game's own walk, its
    // Move byte set), let go in F3, a swallowed release left that byte set and the hero
    // walking by himself back in F1 (2026-10-07). The game's commands are on the press.
    // VR third person with vr_keys_walk (VrKeyWalk): the same - W A S D walk there.
    if ((msg == WM_KEYDOWN || msg == WM_CHAR) && (ShooterActive() || VrKeyWalk()) && !ChatOpen()) {
        const WPARAM k = msg == WM_CHAR && w >= 'a' && w <= 'z' ? w - ('a' - 'A') : w;
        if (k == 'W' || k == 'A' || k == 'S' || k == 'D') return 0;
    }
    // Flat with the keyboard move: the game stays in mouse mode, so the mouse is
    // the game's - its pointer kept in the window's middle (MouseTick), a click
    // goes there: on a monster an attack, on an item a pick-up, on a door or a
    // person the action, on the ground a walk to that point, Shift held = in
    // place - all through the game's own mouse code, along the camera's middle
    // ray (d2rcam's ScreenToRay). Every mouse message carries the middle, so
    // the pointer's travel between two MouseTicks never moves the game's point.
    // Raw mouse input (the game registers none) is dropped as before.
    if (ShooterActive() && KeyMoveMode()) {
        if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT && CrosshairNow() && OrigSetCursor)
            if (HCURSOR x = Crosshair()) { OrigSetCursor(x); return TRUE; }
        RECT rc{};
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL && GetClientRect(h, &rc))
            l = MAKELPARAM(rc.right / 2, rc.bottom / 2);
        if (msg == WM_INPUT) {
            RAWINPUTHEADER hdr{};
            UINT size = sizeof hdr;
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &hdr, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && hdr.dwType == RIM_TYPEMOUSE)
                return DefWindowProcW(h, msg, w, l);
        }
        return CallWindowProcW(g_gameWndProc, h, msg, w, l);
    }
    // The mouse is ours too (KeysAsStick): the pointer kept in the middle would
    // flip the game to mouse mode - its pointer drawn there, a click a walk to it.
    if (ShooterActive()) {
        if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEHOVER || msg == WM_MOUSELEAVE) return 0;
        if (msg == WM_INPUT) {   // raw input: the mouse's dropped, the keyboard's passed on
            RAWINPUTHEADER hdr{};
            UINT size = sizeof hdr;
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &hdr, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && hdr.dwType == RIM_TYPEMOUSE)
                return DefWindowProcW(h, msg, w, l);
        }
    }
    return CallWindowProcW(g_gameWndProc, h, msg, w, l);
}

void HookGameWindow() {
    if (g_gameWndProc) return;
    HWND h = nullptr;
    EnumWindows(FindGameWindow, (LPARAM)&h);
    RECT rc{};
    if (!h || !GetClientRect(h, &rc) || rc.right < 320 || rc.bottom < 200) return;
    g_gameWndProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&GameWndProc);
    if (g_gameWndProc) g_gameWnd = h;
}

// The mouse's turn on top of the head: with the shooter controls the pointer's
// travel from the window's middle (it is put back there - a crosshair, clicks
// go where you look), else a middle-button drag.
bool g_dragging = false;
POINT g_dragLast{};

void MouseTick() {
    if (!g_enabled.load() || !GameFocused()) { g_dragging = false; return; }
    POINT p{};
    if (!GetCursorPos(&p)) return;
    int dx = 0, dy = 0;
    RECT rc{};
    if (ShooterActive() && g_gameWnd && GetClientRect(g_gameWnd, &rc)) {
        POINT mid{rc.right / 2, rc.bottom / 2};
        ClientToScreen(g_gameWnd, &mid);
        dx = p.x - mid.x; dy = p.y - mid.y;
        if (dx || dy) SetCursorPos(mid.x, mid.y);
        g_dragging = false;
    } else if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
        if (g_dragging) { dx = p.x - g_dragLast.x; dy = p.y - g_dragLast.y; }
        g_dragging = true;
        g_dragLast = p;
    } else {
        g_dragging = false;
        return;
    }
    if (!dx && !dy) return;
    const float k = g_set.mouseSpeed.load();
    g_mouseYaw.store(WrapDeg(g_mouseYaw.load() + dx * k));                       // right turns right
    g_mousePitch.store(std::clamp(g_mousePitch.load() + dy * k, -90.0f, 90.0f));  // down looks down
    g_gen.fetch_add(1);
}

// [render] rings and model_radius into cleanroom/drawdist (the old
// renderdistance plugin's job); the radius retries until its hook is in.
std::atomic<bool> g_renderDirty{true};
void ApplyRender() {
    if (!g_renderDirty.load()) return;
    d2rcam::SetRenderRadius(g_set.rings.load());
    if (d2rcam::SetModelRadius(g_set.modelRadius.load())) {
        g_renderDirty.store(false);
        LogF("vrcam: render distance - %d room rings, models out to %.0f", d2rcam::GetRenderRadius(), g_set.modelRadius.load());
    }
}

void ToggleShooter() {
    g_mouseLookOn.store(!g_mouseLookOn.load());
    Log(g_mouseLookOn.load() ? "vrcam: mouse look and W A S D on (F9 = off: free pointer for menus)" : "vrcam: mouse look and W A S D off");
}

// Installs whatever is not in yet. D2R decrypts its code page by page as it
// first runs it, so a hook refused at load is tried again on the next F12.
struct HookState { bool camera, skeleton, target, facing, point, biome, blit, interact; } g_h{};

// SkeletonInstance::ComputeSelfWorldPose (skeletons.cpp), D2RLoader layout.
constexpr uint64_t RVA_COMPUTE_SELF_WORLD_POSE = 0xF78740;
const uint8_t kSigComputeSelfWorldPose[16] = {0x48,0x8B,0xC4,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x89,0x68};

// BiomeSystem::SetCurrentBiome(const char* name), static; logs "Setting current
// biome to %s." itself. Runs at the main menu (act2_frontend_biome), so its
// page is decrypted, and the timer hooks it, before the first area loads.
constexpr uint64_t RVA_SET_BIOME = 0xE68C20;
const uint8_t kSigSetBiome[16] = {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x56,0x57,0x41,0x57,0x48,0x81,0xEC,0xC0,0x00};
using SetBiomeFn = uintptr_t (*)(const char*);
SetBiomeFn OrigSetBiome;

uintptr_t HookSetBiome(const char* name) {
    char raw[160] = {};
    if (name) for (size_t i = 0; i + 1 < sizeof raw; ++i) { if (!SafeRead(&raw[i], name + i, 1) || !raw[i]) break; }
    // bare name: no folder, no extension, lower case
    const char* b = raw;
    for (const char* p = raw; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    char bare[96] = {};
    for (size_t i = 0; b[i] && b[i] != '.' && i + 1 < sizeof bare; ++i) bare[i] = (char)tolower((unsigned char)b[i]);
    AcquireSRWLockExclusive(&g_biomeLock); memcpy(g_biome, bare, sizeof bare); ReleaseSRWLockExclusive(&g_biomeLock);
    g_biomeGen.fetch_add(1);
    return OrigSetBiome(name);
}

// The game's main loop sleeps 10 ms every frame while its window is not in
// front (profiler zone "Background Sleep", D2RLoader layout 0xB6A9F): ~60 fps
// instead of 180 - 30 per eye in AFR stereo - whenever BodyWalk or the headset
// has the focus. Both calls' argument 10 becomes 0 (a yield). In memory only;
// [render] background_full_speed=0 puts the 10 back.
void ApplyBackgroundSleep() {
    static const uint64_t kSites[2] = {0xB6C32, 0xB6C92};
    static const uint8_t kTen[5] = {0xB9, 0x0A, 0x00, 0x00, 0x00}, kZero[5] = {0xB9, 0x00, 0x00, 0x00, 0x00};   // mov ecx, 10 / 0
    static int told = -1;
    if (!g_ctx) return;
    const bool want = g_set.bgFullSpeed.load();
    int done = 0;
    for (uint64_t rva : kSites) {
        if (Matches(rva, want ? kZero : kTen, 5)) { ++done; continue; }
        if (Matches(rva, want ? kTen : kZero, 5) && d2rsig::Patch(rva, want ? kTen : kZero, 5, want ? kZero : kTen, 5)) ++done;
    }
    const int state = done == 2 ? (want ? 1 : 0) : 2;
    if (state != told) {
        told = state;
        Log(state == 1 ? "vrcam: background sleep removed - full frame rate with the game window behind BodyWalk"
            : state == 0 ? "vrcam: background sleep back (10 ms a frame while the window is behind)"
                         : "vrcam: background sleep NOT patched (code not decrypted yet, or another build)");
    }
}

// A stereo pair from one game frame ([stereo] pair_per_tick, AFR only).
// The engine draws with one camera per frame (RenderGraph::BeginFrameWork
// takes one Camera*), so a true second camera is out of reach. Instead the
// game's whole draw runs twice per game frame: left eye, then right eye with
// the frame time held at zero, so nothing moves on between them. Both eyes
// then share one game state and one head, and which eye each Present carries
// is known, not guessed.
// The unit drawn twice is sDrawGameScreen (0x93B40), which the main loop calls
// right after its timer (0x8C862 -> 0x8C869): the old D2 draw builds the HUD
// and its upload lists, then PrismEndDraw -> PrismBlit draws the world and
// presents. Doubling PrismBlit alone (0.48.0) removed the device: its second
// pass drew the HUD from upload lists the first pass had already reset. The
// view and projection are rebuilt inside PrismBlit (UpdateSystems 0xE39390 ->
// GetProj / ViewRebuild), so each pass gets its eye's camera.
// D2RLoader layout, game 3.3.93787; recon in docs/HANDOVER.md.
constexpr uint64_t RVA_DRAW_GAME_SCREEN = 0x93B40;
const uint8_t kSigDrawGameScreen[16] = {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48};
// The frame time: GetFrameTime (0xA19200) and GetRawFrameTime (0xA19210) read
// two floats the main loop's timer (0xA19220) writes once per game frame.
constexpr uint64_t RVA_GET_FRAME_TIME = 0xA19200, RVA_GET_RAW_FRAME_TIME = 0xA19210;
constexpr uint64_t RVA_FRAME_TIME = 0x27D31D0, RVA_RAW_FRAME_TIME = 0x27D31D4;
const uint8_t kSigGetFrameTime[9] = {0xF3,0x0F,0x10,0x05,0xC8,0x9F,0xDB,0x01,0xC3};      // movss xmm0, [0x27D31D0]; ret
const uint8_t kSigGetRawFrameTime[9] = {0xF3,0x0F,0x10,0x05,0xBC,0x9F,0xDB,0x01,0xC3};   // movss xmm0, [0x27D31D4]; ret
using DrawGameScreenFn = uintptr_t (*)(int);
DrawGameScreenFn OrigDrawGameScreen;
extern std::atomic<bool> g_inWorld;

bool FrameTimeFound() {
    static int ok = -1;
    if (ok < 0) ok = Matches(RVA_GET_FRAME_TIME, kSigGetFrameTime, sizeof kSigGetFrameTime) &&
                     Matches(RVA_GET_RAW_FRAME_TIME, kSigGetRawFrameTime, sizeof kSigGetRawFrameTime) &&
                     d2rsig::Addr(RVA_FRAME_TIME) && d2rsig::Addr(RVA_RAW_FRAME_TIME) ? 1 : 0;
    return ok == 1;
}

// Without the frame time held, the second pass would play the frame's
// animation and particles a second time: the pair is drawn only with it.
bool PairWanted() { return g_h.blit && AfrOn() && g_set.pairPerTick.load() && g_inWorld.load() && FrameTimeFound(); }

void SetEye(int eye) {
    g_eye.store(eye);
    if (!g_afrBlock) return;
    g_afrBlock->swap = g_set.afrSwap.load() ? 1u : 0u;
    g_afrBlock->eye = (uint32_t)eye;
    g_afrBlock->frame++;
    g_afrBlock->enabled = 1u;
}

void TurnTick();

// Where a pair's time goes (2026-10-08, the frame budget: ~80 pairs/s whatever
// DLSS or the window size, the GPU at 85-88%). Microseconds on the QPC clock,
// summed over the log's 10 s and printed with the pairs/s line: outside the
// pair (the game's own frame), each pass, and in each pass the stretch from
// ReShade's effects to the Present event (effects) and from Present to the pass's
// end (the Present call itself: waiting for the GPU or the swap chain).
namespace pairtime {
std::atomic<double> g_effBegin{0.0}, g_effSum{0.0}, g_presentAt{0.0};
std::atomic<uint32_t> g_presents{0};   // real presents (ReShade's), never cleared: the replay watchdog's
std::atomic<DWORD> g_drawThread{0};
double UsNow() { return flog::UsNow(); }
// This thread's own CPU time, us: a pass's wall time minus it is waiting. From its
// cycle count (GetThreadTimes moves only at the scheduler's tick: it read 0) at the
// clock's rate, measured once against QPC over 50 ms - a turbo clock reads it a little off.
double CpuUs() {
    static const double cyclesPerUs = [] {
        ULONG64 c0 = 0, c1 = 0;
        const double t0 = UsNow();
        QueryThreadCycleTime(GetCurrentThread(), &c0);
        while (UsNow() - t0 < 50000.0) {}
        QueryThreadCycleTime(GetCurrentThread(), &c1);
        return (double)(c1 - c0) / (UsNow() - t0);
    }();
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return cyclesPerUs > 0.0 ? (double)c / cyclesPerUs : 0.0;
}
// The game's busiest threads over the last call's span, % of one core, by their
// cycle counts (Toolhelp + QueryThreadCycleTime): "game thread 38%, 1234 92% (the
// render thread?)". Called with the pairs/s line; the pass's own thread is marked.
std::string BusyThreads(double spanUs) {
    static std::unordered_map<DWORD, ULONG64> last;
    std::unordered_map<DWORD, ULONG64> now;
    std::vector<std::pair<double, DWORD>> busy;
    const double cpu0 = CpuUs(); (void)cpu0;   // calibrates cyclesPerUs on first use
    ULONG64 one = 0; QueryThreadCycleTime(GetCurrentThread(), &one);
    const double perUs = one && CpuUs() > 0.0 ? (double)one / CpuUs() : 0.0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE || perUs <= 0.0) return "?";
    THREADENTRY32 te{sizeof te};
    const DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        ULONG64 c = 0;
        if (QueryThreadCycleTime(h, &c)) {
            now[te.th32ThreadID] = c;
            if (auto it = last.find(te.th32ThreadID); it != last.end() && spanUs > 0.0)
                busy.push_back({100.0 * (double)(c - it->second) / perUs / spanUs, te.th32ThreadID});
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    last.swap(now);
    std::sort(busy.begin(), busy.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string out;
    char b[64];
    for (size_t i = 0; i < busy.size() && i < 5; ++i) {
        snprintf(b, sizeof b, "%s%lu%s %.0f%%", i ? ", " : "", busy[i].second,
                 busy[i].second == g_drawThread.load() ? " (draw)" : "", busy[i].first);
        out += b;
    }
    return out;
}
}  // namespace pairtime

// A sampling profile of the draw thread ([debug] profile_draw=1, live; it turns
// itself off after one run): 5 s at ~1 kHz, the thread suspended for each sample
// just long enough to read its context and unwind its stack into a fixed array
// (no allocation, no locks of ours while it is stopped). Then, by module and by
// function (the unwind table's start): self = the sample's leaf is there,
// incl = it is anywhere on the stack. Where the draw thread's ~6 ms an eye go.
namespace drawprof {
std::atomic<bool> g_want{false}, g_running{false};
constexpr int kDepth = 24;
struct Sample { int n; DWORD64 pc[kDepth]; };

std::string ModuleOf(DWORD64 pc, DWORD64* base) {
    HMODULE m = nullptr;
    *base = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)pc, &m) || !m)
        return "?";
    *base = (DWORD64)m;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* b = strrchr(path, '\\');
    return b ? b + 1 : path;
}

// A stopped thread's stack, copied, walked by the unwind tables. Under SEH: a
// frame kept in a register other than RSP/RBP still points into the live stack,
// whatever is there now - a bad read ends the walk, not the game.
// The game's own unwind table: D2RLoader rebuilds the image after Windows registered
// it, so RtlLookupFunctionEntry finds nothing in it - its .pdata is read in place
// (the header's exception directory, as in dump_loader's snapshot: 0x3BFF000).
PRUNTIME_FUNCTION GameFunction(DWORD64 pc, DWORD64* imageBase) {
    static const DWORD64 base = (DWORD64)GetModuleHandleW(nullptr);
    static const RUNTIME_FUNCTION* table = nullptr;
    static size_t count = 0;
    static DWORD imageSize = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        __try {
            const auto* dos = (const IMAGE_DOS_HEADER*)base;
            const auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
            const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
            imageSize = nt->OptionalHeader.SizeOfImage;
            if (d.VirtualAddress && d.Size) { table = (const RUNTIME_FUNCTION*)(base + d.VirtualAddress); count = d.Size / sizeof(RUNTIME_FUNCTION); }
        } __except (EXCEPTION_EXECUTE_HANDLER) { table = nullptr; }
    }
    if (!table || pc < base || pc >= base + imageSize) return nullptr;
    const DWORD rva = (DWORD)(pc - base);
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (table[mid].EndAddress <= rva) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= count || table[lo].BeginAddress > rva) return nullptr;
    PRUNTIME_FUNCTION f = (PRUNTIME_FUNCTION)&table[lo];
    // chained entries (UNW_FLAG_CHAININFO) are followed by RtlVirtualUnwind itself
    *imageBase = base;
    return f;
}

PRUNTIME_FUNCTION FindFunction(DWORD64 pc, DWORD64* imageBase) {
    if (PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(pc, imageBase, nullptr)) return f;
    return GameFunction(pc, imageBase);
}

int UnwindCopy(CONTEXT c, uint8_t* copy, size_t copied, DWORD64* out) {
    const DWORD64 orig = c.Rsp, origEnd = c.Rsp + copied, delta = (DWORD64)copy - orig;
    const DWORD64 lo = (DWORD64)copy, hi = (DWORD64)copy + copied;
    c.Rsp += delta;
    if (c.Rbp >= orig && c.Rbp < origEnd) c.Rbp += delta;
    int n = 0;
    __try {
        for (int d = 0; d < kDepth && c.Rip; ++d) {
            out[n++] = c.Rip;
            if (c.Rsp < lo || c.Rsp + 8 > hi) break;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION f = FindFunction(c.Rip, &imageBase);
            if (!f) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; continue; }   // a leaf: the return address on top
            void* handlerData = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, f, &c, &handlerData, &establisher, nullptr);
            if (c.Rbp >= orig && c.Rbp < origEnd) c.Rbp += delta;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

DWORD WINAPI Run(void* arg) {
    const DWORD tid = (DWORD)(uintptr_t)arg;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) { Log("vrcam: profile_draw - cannot open the draw thread"); g_running.store(false); return 0; }
    static Sample samples[6000];
    static uint8_t stackCopy[32768];
    int count = 0;
    const ULONGLONG until = GetTickCount64() + 5000;
    while (GetTickCount64() < until && count < 6000) {
        // Stopped only for the context and a memcpy of its stack: nothing that
        // takes a lock (the first version unwound while it was stopped and hung
        // the game - the unwinder waited on a lock the stopped thread held).
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        size_t copied = 0;
        if (SuspendThread(th) == (DWORD)-1) { Sleep(1); continue; }
        const bool got = GetThreadContext(th, &c) != 0;
        if (got && c.Rsp) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                const DWORD64 regionEnd = (DWORD64)mbi.BaseAddress + mbi.RegionSize;
                copied = (size_t)std::min<DWORD64>(sizeof stackCopy, regionEnd - c.Rsp);
                memcpy(stackCopy, (void*)c.Rsp, copied);
            }
        }
        ResumeThread(th);
        if (!got || !copied) { Sleep(1); continue; }
        // Unwind the copy: the stack and frame registers moved into it.
        Sample& sm = samples[count];
        sm.n = UnwindCopy(c, stackCopy, copied, sm.pc);
        if (sm.n) ++count;
        Sleep(1);
    }
    CloseHandle(th);

    // by module and by function: self and inclusive
    struct Acc { int self = 0, incl = 0; };
    std::unordered_map<std::string, Acc> mods, funcs, callers, callers2, under1, under2, under3;
    // the tree under sDrawGameScreen (D2R 0x93B40): what each pass calls, two and three levels down
    const DWORD64 drawRoot = (DWORD64)GetModuleHandleW(nullptr) + 0x93B40;
    int underN = 0;
    std::unordered_map<DWORD64, std::pair<std::string, DWORD64>> modCache;
    auto modOf = [&](DWORD64 pc) -> std::pair<std::string, DWORD64> {
        const DWORD64 page = pc & ~0xFFFull;
        auto it = modCache.find(page);
        if (it != modCache.end()) return it->second;
        DWORD64 base = 0; std::string m = ModuleOf(pc, &base);
        return modCache[page] = {m, base};
    };
    auto funcOf = [&](DWORD64 pc) -> std::string {
        auto [m, base] = modOf(pc);
        DWORD64 ib = 0;
        PRUNTIME_FUNCTION f = FindFunction(pc, &ib);
        char b[128];
        if (f && ib) snprintf(b, sizeof b, "%s+0x%llX", m.c_str(), (unsigned long long)f->BeginAddress);
        else snprintf(b, sizeof b, "%s+0x%llX?", m.c_str(), (unsigned long long)(base ? pc - base : pc));
        return b;
    };
    for (int i = 0; i < count; ++i) {
        const Sample& sm = samples[i];
        {
            int r = -1;
            for (int d = 0; d < sm.n; ++d) {
                DWORD64 ib = 0;
                PRUNTIME_FUNCTION f = FindFunction(sm.pc[d], &ib);
                if (f && ib + f->BeginAddress == drawRoot) { r = d; break; }
            }
            if (r >= 1) {
                ++underN;
                const std::string a = funcOf(sm.pc[r - 1]);
                under1[a].incl++;
                if (r >= 2) {
                    const std::string b = a + " > " + funcOf(sm.pc[r - 2]);
                    under2[b].incl++;
                    if (r >= 3) under3[b + " > " + funcOf(sm.pc[r - 3])].incl++;
                }
            }
        }
        std::vector<std::string> seenM, seenF;
        for (int d = 0; d < sm.n; ++d) {
            const std::string m = modOf(sm.pc[d]).first, fn = funcOf(sm.pc[d]);
            if (d == 0) { mods[m].self++; funcs[fn].self++; }
            // who calls the leaf, and the first caller outside the system DLLs (ntdll, kernel, win32u, D3D12Core, the driver)
            if (d == 1) callers[funcOf(sm.pc[0]) + " <- " + fn].self++;
            if (d >= 1 && m != "ntdll.dll" && m != "KERNELBASE.dll" && m != "kernel32.dll" && m != "win32u.dll" &&
                m != "D3D12Core.dll" && m != "nvwgf2umx.dll" && m != "?" && seenM.empty() == false) {
                static thread_local int lastSample = -1;
                if (lastSample != i) { lastSample = i; callers2[modOf(sm.pc[0]).first + " <- " + fn].self++; }
            }
            if (std::find(seenM.begin(), seenM.end(), m) == seenM.end()) { seenM.push_back(m); mods[m].incl++; }
            if (std::find(seenF.begin(), seenF.end(), fn) == seenF.end()) { seenF.push_back(fn); funcs[fn].incl++; }
        }
    }
    auto dump = [&](const char* what, std::unordered_map<std::string, Acc>& map, bool bySelf, size_t top) {
        std::vector<std::pair<std::string, Acc>> v(map.begin(), map.end());
        std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return bySelf ? a.second.self > b.second.self : a.second.incl > b.second.incl; });
        LogF("vrcam: profile_draw %s (%d samples):", what, count);
        for (size_t i = 0; i < v.size() && i < top; ++i)
            LogF("vrcam:   %5.1f%% self %5.1f%% incl  %s", 100.0 * v[i].second.self / count, 100.0 * v[i].second.incl / count, v[i].first.c_str());
    };
    {   // every sample's stack as module+rva, leaf first, for an offline call tree (tools/prof_tree.py)
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_iniPath);
        if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
        wcscat_s(path, L"\\..\\logs\\d2r_vr_profile.txt");
        if (FILE* fp = _wfopen(path, L"w")) {
            for (int i = 0; i < count; ++i) {
                for (int d = 0; d < samples[i].n; ++d) {
                    auto [m, base] = modOf(samples[i].pc[d]);
                    fprintf(fp, "%s%s+%llX", d ? " " : "", m.c_str(), (unsigned long long)(base ? samples[i].pc[d] - base : samples[i].pc[d]));
                }
                fputc('\n', fp);
            }
            fclose(fp);
            Log("vrcam: profile_draw - the stacks went to d2rloader\\logs\\d2r_vr_profile.txt");
        }
    }
    if (count) {
        dump("modules", mods, false, 14);
        dump("functions by self", funcs, true, 25);
        dump("functions by inclusive", funcs, false, 20);
        dump("leaf <- its caller", callers, true, 30);
        LogF("vrcam: profile_draw - %d of %d samples inside sDrawGameScreen (0x93B40)", underN, count);
        dump("under sDrawGameScreen, level 1", under1, false, 15);
        dump("under sDrawGameScreen, level 2", under2, false, 25);
        dump("under sDrawGameScreen, level 3", under3, false, 30);
        dump("leaf module <- first caller in an app module", callers2, true, 30);
    }
    g_running.store(false);
    return 0;
}


// From the present callback (the draw thread): start a run when asked.
void Tick() {
    if (!g_want.load() || g_running.exchange(true)) return;
    g_want.store(false);
    Log("vrcam: profile_draw - sampling the draw thread for 5 s");
    if (HANDLE h = CreateThread(nullptr, 0, Run, (void*)(uintptr_t)GetCurrentThreadId(), 0, nullptr)) CloseHandle(h);
    else g_running.store(false);
}
}  // namespace drawprof

// One CPU pass for two GPU frames - first, can it be done at all? (2026-10-09)
// [debug] cb_compare=1 (read when vrcam joins ReShade: the game restarted with it)
// listens to every root constant buffer and root constant the game binds; each time
// [debug] cb_compare_go turns 1, one stereo pair is recorded - the left pass's
// bindings, then the right's - and compared: how many of the right eye's constants
// the left eye had byte for byte, and, by root parameter, which ones differ. If only
// a few (the camera's) differ, the left eye's command lists can be replayed for the
// right eye with those patched; if every object's own constants differ, they hold
// the view and replay is out.
namespace cbcmp {
using namespace reshade::api;
std::atomic<bool> g_on{false}, g_go{false};
std::atomic<int> g_state{0};   // 0 idle, 1 armed for the next pair, 2 recording
std::atomic<int> g_eyeRec{0};
constexpr int kMax = 40000, kHead = 4096;   // the whole 4 KB: camera buffers keep four views, the last at word 596
struct Rec { uint64_t hash; uint32_t param, kind, len, tid; uint64_t layout; uint8_t head[kHead]; };
Rec* g_rec[2] = {nullptr, nullptr};
float g_view[2][16] = {};   // vrcam's view (row-major, v*M) for each eye, the last one built while recording
void NoteView(int eye, const float v[16]) { if (g_state.load(std::memory_order_relaxed) == 2) memcpy(g_view[eye & 1], v, sizeof g_view[0]); }
std::atomic<int> g_n[2];
SRWLOCK g_lock = SRWLOCK_INIT;

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

void Add(uint32_t kind, uint64_t layout, uint32_t param, const void* data, uint32_t len) {
    const int e = g_eyeRec.load() & 1;
    const int i = g_n[e].fetch_add(1);
    if (i >= kMax) return;
    Rec& r = g_rec[e][i];
    r.kind = kind; r.layout = layout; r.param = param; r.tid = GetCurrentThreadId();
    r.len = std::min<uint32_t>(len, kHead);
    memcpy(r.head, data, r.len);
    r.hash = Fnv(r.head, r.len) ^ ((uint64_t)r.len << 56);
}

void OnPushDescriptors(command_list* cl, shader_stage, pipeline_layout layout, uint32_t param, const descriptor_table_update& u) {
    if (g_state.load(std::memory_order_relaxed) != 2 || u.type != descriptor_type::constant_buffer || !u.count || !u.descriptors) return;
    const buffer_range& br = static_cast<const buffer_range*>(u.descriptors)[0];
    device* dev = cl->get_device();
    const resource_desc d = dev->get_resource_desc(br.buffer);
    if (d.type != resource_type::buffer || br.offset >= d.buffer.size) return;
    const uint64_t len = std::min<uint64_t>(kHead, d.buffer.size - br.offset);
    void* p = nullptr;
    if (!dev->map_buffer_region(br.buffer, br.offset, len, map_access::read_only, &p) || !p) return;
    uint8_t tmp[kHead];
    __try { memcpy(tmp, p, (size_t)len); } __except (EXCEPTION_EXECUTE_HANDLER) { dev->unmap_buffer_region(br.buffer); return; }
    dev->unmap_buffer_region(br.buffer);
    Add(1, layout.handle, param, tmp, (uint32_t)len);
}

void OnPushConstants(command_list*, shader_stage, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void* values) {
    if (g_state.load(std::memory_order_relaxed) != 2 || !values) return;
    Add(2, layout.handle, param | (first << 16), values, count * 4);
}

void Register() {
    if (!IniB(L"debug", L"cb_compare", false)) return;
    g_rec[0] = (Rec*)VirtualAlloc(nullptr, sizeof(Rec) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_rec[1] = (Rec*)VirtualAlloc(nullptr, sizeof(Rec) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_rec[0] || !g_rec[1]) return;
    reshade::register_event<reshade::addon_event::push_descriptors>(&d2rvr::D3D12Only<&OnPushDescriptors>::Call);
    reshade::register_event<reshade::addon_event::push_constants>(&d2rvr::D3D12Only<&OnPushConstants>::Call);
    g_on.store(true);
    Log("vrcam: cb_compare on - [debug] cb_compare_go=1 records one stereo pair's constant buffers");
}

// The pair's hook, before the left pass / between / after the right.
void PairBegin() {
    if (!g_on.load()) return;
    if (g_go.exchange(false)) g_state.store(1);
    if (g_state.load() == 1) { g_n[0] = 0; g_n[1] = 0; g_eyeRec = 0; g_state.store(2); }
}
void PairMiddle() { if (g_state.load() == 2) g_eyeRec = 1; }

void Report() {
    const int n0 = std::min<int>(g_n[0].load(), kMax), n1 = std::min<int>(g_n[1].load(), kMax);
    std::unordered_map<uint64_t, int> left;
    for (int i = 0; i < n0; ++i) left[g_rec[0][i].hash]++;
    struct By { int n = 0, same = 0, firstDiff = -1; uint32_t len = 0; uint64_t layout = 0; };
    std::map<std::pair<uint32_t, uint32_t>, By> by;   // (kind, param)
    int same = 0;
    for (int i = 0; i < n1; ++i) {
        const Rec& r = g_rec[1][i];
        By& b = by[{r.kind, r.param}];
        b.n++; b.len = r.len; b.layout = r.layout;
        auto it = left.find(r.hash);
        if (it != left.end() && it->second > 0) { it->second--; same++; b.same++; }
        else if (b.firstDiff < 0) b.firstDiff = i;
    }
    LogF("vrcam: cb_compare - left %d bindings, right %d; %d of the right eye's (%.1f%%) the left eye had byte for byte",
         n0, n1, same, n1 ? 100.0 * same / n1 : 0.0);
    for (auto& [k, b] : by) {
        if (b.n == b.same) continue;
        LogF("vrcam:   differ: %s param %u (first %u) layout %llX - %d of %d differ, %u bytes read",
             k.first == 1 ? "root CBV" : "root constants", k.second & 0xFFFF, k.second >> 16,
             (unsigned long long)b.layout, b.n - b.same, b.n, b.len);
    }
    // the first differing binding of the three most common differing params: its floats, right eye and
    // the left eye's binding of the same param at the same position in the draw thread's order
    int shown = 0;
    for (auto& [k, b] : by) {
        if (b.n == b.same || b.firstDiff < 0 || shown >= 4) continue;
        ++shown;
        const Rec& r = g_rec[1][b.firstDiff];
        int nth = 0;
        for (int i = 0; i < b.firstDiff; ++i) if (g_rec[1][i].kind == r.kind && g_rec[1][i].param == r.param && g_rec[1][i].tid == r.tid) ++nth;
        const Rec* l = nullptr;
        for (int i = 0, m = 0; i < n0; ++i)
            if (g_rec[0][i].kind == r.kind && g_rec[0][i].param == r.param) { if (m++ == nth) { l = &g_rec[0][i]; break; } }
        for (int part = 0; part < 2; ++part) {
            const Rec* x = part ? l : &r;
            if (!x) continue;
            std::string line;
            char b2[32];
            const float* f = (const float*)x->head;
            for (uint32_t j = 0; j < std::min<uint32_t>(x->len / 4, 40); ++j) { snprintf(b2, sizeof b2, "%s%.4g", j ? " " : "", f[j]); line += b2; }
            LogF("vrcam:   %s param %u %s: %.200s", k.first == 1 ? "CBV" : "consts", k.second & 0xFFFF, part ? "left " : "right", line.c_str());
            if (line.size() > 200) LogF("vrcam:      ...%.200s", line.c_str() + 200);
        }
    }
}
// Both eyes' records to d2rloader\logs\d2r_vr_cbdump.bin for tools/cb_diff.py:
// "CBD1", then per eye an int count and the records as they are (Rec, packed by the compiler).
void Dump() {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_cbdump.bin");
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fwrite("CBD1", 1, 4, f);
    const uint32_t recSize = sizeof(Rec);
    fwrite(&recSize, 4, 1, f);
    for (int e = 0; e < 2; ++e) {
        const int n = std::min<int>(g_n[e].load(), kMax);
        fwrite(&n, 4, 1, f);
        fwrite(g_rec[e], sizeof(Rec), n, f);
    }
    fwrite("VIEW", 1, 4, f);
    fwrite(g_view, sizeof g_view, 1, f);
    fclose(f);
    Log("vrcam: cb_compare - both eyes' records went to d2rloader\\logs\\d2r_vr_cbdump.bin");
}
void PairEnd() {
    if (g_state.load() != 2) return;
    g_state.store(0);
    Report();
    Dump();
}
}  // namespace cbcmp

// The submissions of one stereo pair, for replaying the left eye's command lists as
// the right eye's ([debug] replay_trace=1 when vrcam joins ReShade; replay_trace_go
// 0 -> 1 records one pair): command lists reset, closed and executed (queue, list),
// the back buffer bound as a render target (which list draws the final picture),
// the queue's Signal and Wait (the game's frame fences, D3D12Core hooked through the
// queue's vtable) and Present - in order, with the time, thread and eye, to
// d2rloader\logs\d2r_vr_replaytrace.txt.
namespace rtrace {
using namespace reshade::api;
std::atomic<bool> g_on{false}, g_go{false};
std::atomic<int> g_state{0};   // 0 idle, 1 armed, 2 recording
struct Ev { double t; DWORD tid; int eye; char kind; uint64_t a, b, c; };
constexpr int kMax = 60000;
Ev* g_ev = nullptr;
std::atomic<int> g_n{0};
double g_t0 = 0.0;
std::atomic<uint64_t> g_bb[8];   // the swap chain's back buffers (resource handles), from Present

void Add(char kind, uint64_t a, uint64_t b = 0, uint64_t c = 0) {
    if (g_state.load(std::memory_order_relaxed) != 2) return;
    const int i = g_n.fetch_add(1);
    if (i >= kMax) return;
    g_ev[i] = {pairtime::UsNow() - g_t0, GetCurrentThreadId(), g_eye.load(), kind, a, b, c};
}

void OnReset(command_list* cl) { Add('R', (uint64_t)cl->get_native()); }
void OnClose(command_list* cl) { Add('C', (uint64_t)cl->get_native()); }
void OnExecute(command_queue* q, command_list* cl) { Add('X', (uint64_t)q->get_native(), (uint64_t)cl->get_native(), (uint64_t)q->get_type()); }
void OnPresent(command_queue* q, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    const uint32_t n = std::min<uint32_t>(sc->get_back_buffer_count(), 8);
    for (uint32_t i = 0; i < n; ++i) g_bb[i] = sc->get_back_buffer(i).handle;
    Add('P', (uint64_t)q->get_native(), (uint64_t)sc->get_native(), sc->get_current_back_buffer_index());
}
void OnBindRT(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view) {
    if (g_state.load(std::memory_order_relaxed) != 2) return;
    device* dev = cl->get_device();
    for (uint32_t i = 0; i < count; ++i) {
        if (!rtvs[i].handle) continue;
        const uint64_t r = dev->get_resource_from_view(rtvs[i]).handle;
        for (int k = 0; k < 8; ++k)
            if (r && g_bb[k].load() == r) { Add('B', (uint64_t)cl->get_native(), r, k); break; }
    }
}

using SignalFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
using WaitFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
SignalFn OrigSignal = nullptr;
WaitFn OrigWait = nullptr;
HRESULT STDMETHODCALLTYPE HookSignal(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { Add('S', (uint64_t)q, (uint64_t)f, v); return OrigSignal(q, f, v); }
HRESULT STDMETHODCALLTYPE HookWait(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { Add('W', (uint64_t)q, (uint64_t)f, v); return OrigWait(q, f, v); }
std::atomic<bool> g_hooked{false};
void HookQueue(command_queue* q) {
    if (g_hooked.exchange(true)) return;
    void** vt = *(void***)q->get_native();
    const bool ok = MH_CreateHook(vt[14], (void*)&HookSignal, (void**)&OrigSignal) == MH_OK && MH_EnableHook(vt[14]) == MH_OK &&
                    MH_CreateHook(vt[15], (void*)&HookWait, (void**)&OrigWait) == MH_OK && MH_EnableHook(vt[15]) == MH_OK;
    LogF("vrcam: replay_trace - queue Signal/Wait hooks %s", ok ? "in" : "FAILED");
}
void OnExecuteHook(command_queue* q, command_list* cl) { HookQueue(q); OnExecute(q, cl); }

void Register() {
    if (!IniB(L"debug", L"replay_trace", false)) return;
    g_ev = (Ev*)VirtualAlloc(nullptr, sizeof(Ev) * kMax, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_ev) return;
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::close_command_list>(&d2rvr::D3D12Only<&OnClose>::Call);
    reshade::register_event<reshade::addon_event::execute_command_list>(&d2rvr::D3D12Only<&OnExecuteHook>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBindRT>::Call);
    g_on.store(true);
    Log("vrcam: replay_trace on - [debug] replay_trace_go=1 records one stereo pair's submissions");
}

void PairBegin() {
    if (!g_on.load()) return;
    if (g_go.exchange(false)) { g_n = 0; g_t0 = pairtime::UsNow(); g_state.store(2); Add('L', 0); }
}
void PairMiddle() { Add('M', 0); }
// After the right pass: its Present (at the end of PrismBlit) is in.
void PairEnd() {
    if (g_state.load() != 2) return;
    Add('E', 0);
    g_state.store(0);
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_replaytrace.txt");
    FILE* f = _wfopen(path, L"w");
    if (!f) return;
    const int n = std::min<int>(g_n.load(), kMax);
    for (int i = 0; i < n; ++i) {
        const Ev& e = g_ev[i];
        fprintf(f, "%9.1f %6lu e%d %c %llX %llX %llX\n", e.t, e.tid, e.eye, e.kind,
                (unsigned long long)e.a, (unsigned long long)e.b, (unsigned long long)e.c);
    }
    fclose(f);
    LogF("vrcam: replay_trace - %d events of one pair to d2rloader\\logs\\d2r_vr_replaytrace.txt", n);
}
}  // namespace rtrace

// One CPU pass, two GPU frames - prototype ([debug] replay_proto=1 when vrcam joins
// ReShade; [stereo] replay_right live: 1 replay, 3 no replay - the left picture twice).
// The left eye's pass runs as ever; its direct and compute command lists are noted
// as the game executes them (the copy queue's uploads are not: their data stays where
// they put it). Its Present is held back: in the flip model a presented back buffer
// belongs to DXGI - reading it, or drawing into it again, removed the device
// (0x887A002B, access denied). Instead the left picture is copied out of the back
// buffer, the noted lists are executed once more on the same queues (our own fence
// between them) - they draw into the same back buffer, still the current one - and
// that is presented as the right eye; then the left copy goes into the next back
// buffer and is presented as the left eye. Both through the game's swap chain
// (ReShade's proxy: the effects and FlatVR's add-on run on them). The camera's
// constant buffers are not patched yet: the right eye is the left one again - this
// proves the replay. DLSS must be off (its evaluation is recorded with one history).
// The right eye's camera in the replayed lists. The game's per-object constants are
// the same for both eyes (cb_compare); the camera's are not. Its constant buffers are
// found as the left pass binds them: the native command lists' Set*RootSignature and
// Set*RootConstantBufferView are hooked; each (root signature, parameter) is judged
// once by its content (does it hold the left eye's view matrix, or the camera's
// position?), and the camera ones' GPU addresses are noted. The game's upload buffers
// are known from ReShade's init_resource (vrcam joins before the game makes its
// device), so a GPU address reads as CPU memory. After the left pass is done on the
// GPU those buffers are rewritten for the right eye - the view, its inverse, view *
// projection and its inverse, the camera position - and the lists run again.
namespace camfix {
using namespace reshade::api;
struct Upload { uint64_t va, size; uint8_t* cpu; uint64_t res; };
std::vector<Upload> g_uploads;     // sorted by va
SRWLOCK g_upLock = SRWLOCK_INIT;
std::atomic<bool> g_capture{false};
std::atomic<uint32_t> g_patched{0};
std::atomic<uint32_t> g_nNote{0}, g_nNoRS{0}, g_nNoCpu{0}, g_nJudged{0}, g_nUploads{0}, g_nInit{0};

void OnInitResource(device* dev, const resource_desc& d, const subresource_data*, resource_usage, resource res) {
    if (d.type == resource_type::buffer) g_nInit.fetch_add(1);
    if (d.type != resource_type::buffer || d.heap != memory_heap::cpu_to_gpu) return;
    g_nUploads.fetch_add(1);
    ID3D12Resource* r = (ID3D12Resource*)res.handle;
    void* cpu = nullptr;
    if (!dev->map_buffer_region(res, 0, UINT64_MAX, map_access::write_only, &cpu) || !cpu) return;
    const Upload u{r->GetGPUVirtualAddress(), d.buffer.size, (uint8_t*)cpu, res.handle};
    AcquireSRWLockExclusive(&g_upLock);
    g_uploads.insert(std::upper_bound(g_uploads.begin(), g_uploads.end(), u, [](const Upload& a, const Upload& b) { return a.va < b.va; }), u);
    ReleaseSRWLockExclusive(&g_upLock);
}
void OnDestroyResource(device*, resource res) {
    AcquireSRWLockExclusive(&g_upLock);
    for (size_t i = 0; i < g_uploads.size(); ++i)
        if (g_uploads[i].res == res.handle) { g_uploads.erase(g_uploads.begin() + i); break; }
    ReleaseSRWLockExclusive(&g_upLock);
}
// CPU memory of a GPU address in an upload buffer, and how many bytes follow it there.
uint8_t* Cpu(uint64_t va, uint64_t* room) {
    AcquireSRWLockShared(&g_upLock);
    uint8_t* out = nullptr;
    auto it = std::upper_bound(g_uploads.begin(), g_uploads.end(), va, [](uint64_t v, const Upload& u) { return v < u.va; });
    if (it != g_uploads.begin()) {
        --it;
        if (va < it->va + it->size) { out = it->cpu + (va - it->va); *room = it->va + it->size - va; }
    }
    ReleaseSRWLockShared(&g_upLock);
    return out;
}

// the left view (row-major, v*M) and the right one: the half-eye shift taken twice off x
void Views(double L[16], double R[16]) {
    float v[16];
    AcquireSRWLockShared(&g_leftViewLock); memcpy(v, g_leftView, sizeof v); ReleaseSRWLockShared(&g_leftViewLock);
    for (int i = 0; i < 16; ++i) L[i] = R[i] = v[i];
    R[12] = L[12] - 2.0 * g_eyeHalf.load();
}
bool Inv4(const double M[16], double out[16]) {   // Gauss-Jordan, partial pivoting
    double a[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { a[r][c] = M[r * 4 + c]; a[r][4 + c] = r == c ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
        if (fabs(a[piv][c]) < 1e-12) return false;
        if (piv != c) for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[piv][k]);
        const double d = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c || a[r][c] == 0.0) continue;
            const double m = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= m * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][4 + c];
    return true;
}
void InvRigid(const double V[16], double out[16]) {   // rotation transposed, translation -t*R^T
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out[r * 4 + c] = V[c * 4 + r];
    out[3] = out[7] = out[11] = 0.0;
    for (int c = 0; c < 3; ++c) out[12 + c] = -(V[12] * out[c] + V[13] * out[4 + c] + V[14] * out[8 + c]);
    out[15] = 1.0;
}
void Mul(const double A[16], const double B[16], double out[16]) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += A[r * 4 + k] * B[k * 4 + c];
        out[r * 4 + c] = s;
    }
}
bool Near(const float* f, const double* m, int n, double rel, double abs_) {
    for (int i = 0; i < n; ++i) if (fabs(f[i] - m[i]) > abs_ + rel * fabs(m[i])) return false;
    return true;
}
void Put(float* f, const double* m, int n) { for (int i = 0; i < n; ++i) f[i] = (float)m[i]; }

// Where a camera buffer keeps the view: learned once per (root signature, parameter)
// against the left view vrcam built (loosely - that one may have moved on with the
// head since), then rewritten from the buffer's own view each time.
struct Layout {
    // view matrices (row-major, v*M) at these words: D2R's camera buffers keep five - four camera
    // records of 192 words (20, 212, 404, 596) and one more at 788 (two passes: all five move for
    // the right eye; with four looked for the fifth, which the shadows use, stayed the left one's)
    int nv = 0, vo[8] = {};
    uint8_t parts[8] = {};         // 2 its inverse follows, 4 view*projection at +64 (projection at +32), 8 its inverse at +80
    int nc = 0, co[4] = {};        // the camera position on its own
};
bool Rigid(const float* m) {   // rows 0-2 a rotation, column 3 = (0,0,0,1)
    for (int r = 0; r < 3; ++r) {
        double len = 0.0;
        for (int c = 0; c < 3; ++c) len += (double)m[r * 4 + c] * m[r * 4 + c];
        if (fabs(len - 1.0) > 1e-3 || fabs(m[r * 4 + 3]) > 1e-6) return false;
    }
    return fabs(m[15] - 1.0f) < 1e-6;
}
// The view and what goes with it, judged against the left view L: rotation within 0.05,
// translation within 2 units (the head may have turned a little since L was built).
Layout Learn(const float* f, int n, const double L[16]) {
    Layout lay;
    double iL[16];
    InvRigid(L, iL);
    std::vector<int> covered;
    for (int o = 0; o + 16 <= n && lay.nv < 8; ++o) {
        if (!Rigid(f + o) || !Near(f + o, L, 12, 0.0, 0.05) || !Near(f + o + 12, L + 12, 3, 0.0, 2.0)) continue;
        double V[16], iV[16];
        for (int i = 0; i < 16; ++i) V[i] = f[o + i];
        InvRigid(V, iV);
        uint8_t parts = 0;
        if (o + 32 <= n && Near(f + o + 16, iV, 16, 1e-5, 2e-2)) parts |= 2;
        if (o + 96 <= n) {
            double P[16], Pi[16], vp[16], ivp[16];
            for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; Pi[i] = f[o + 48 + i]; }
            Mul(V, P, vp);
            if (Near(f + o + 64, vp, 16, 1e-4, 1e-2)) {
                parts |= 4;
                Mul(Pi, iV, ivp);
                if (Near(f + o + 80, ivp, 16, 1e-3, 1e-1)) parts |= 8;
            }
        }
        lay.vo[lay.nv] = o; lay.parts[lay.nv] = parts; ++lay.nv;
        covered.push_back(o);
        if (parts & 2) covered.push_back(o + 16);
        o += 15;
    }
    for (int o = 0; o + 3 <= n && lay.nc < 4; ++o) {
        if (!Near(f + o, iL + 12, 3, 0.0, 0.25) || fabs(f[o]) < 10.0f) continue;   // a position out in the world, not a small number
        bool inside = false;
        for (int c : covered) if (o >= c && o < c + 16) inside = true;
        if (inside) continue;
        lay.co[lay.nc++] = o;
        o += 2;
    }
    return lay;
}
// The right eye's values over the left eye's, by the learned layout: the buffer's own
// view moved by the eye separation along its x (vrcam's half-eye shift, twice).
// prev (row-major) moved by the right eye's frustum: prev * inverse(P) * P_right, P_right = the
// fold's T(-shift) * P with the other off-axis shift. False if P is not ours.
bool PrevMoved(const float* f, int o, double shift, double out[16]) {
    const double sx = g_projSx.load();
    if (sx == 0.0 || fabs(f[o + 32] - sx) > 1e-3 * fabs(sx)) return false;
    double P[16], Pr[16], Pi[16], prev[16], C[16];
    for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; prev[i] = f[o + 96 + i]; }
    for (int i = 0; i < 16; ++i) Pr[i] = P[i];
    Pr[8] -= 2.0 * g_projShift.load();
    for (int c = 0; c < 4; ++c) Pr[12 + c] = Pr[12 + c] - shift * Pr[c];
    if (!Inv4(P, Pi)) return false;
    Mul(Pi, Pr, C);
    Mul(prev, C, out);
    for (int i = 0; i < 16; ++i) if (!std::isfinite(out[i])) return false;
    return true;
}
// Both eyes' staged values (see g_replayPrev): each camera record's last-frame view*projection
// made the right eye's.
void PrevToRightEye(float* f, int n, const Layout& lay, double shift) {
    if (!g_replayPrev.load()) return;
    const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(mask & (1 << k)) || !(lay.parts[k] & 4)) continue;
        const int o = lay.vo[k];
        if (o + 112 > n) continue;
        double out[16];
        if (PrevMoved(f, o, shift, out)) Put(f + o + 96, out, 16);
    }
}
int PatchKnown(float* f, int n, const Layout& lay, double shift, const double right[3]) {
    int done = 0;
    double ax[3] = {right[0], right[1], right[2]};
    const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
    for (int k = 0; k < lay.nv; ++k) {
        if (!(mask & (1 << k))) continue;
        // Folded (replay_proj 3), a view with no projection beside it stays the left one: what reads
        // it pairs it with a projection that carries the eye's shift already (D2R's view at word
        // 980 moved too put the right eye's shading off by a second eye's shift, 2026-10-10).
        if (g_projFix.load() == 3 && !(lay.parts[k] & 4)) continue;
        const int o = lay.vo[k];
        if (o + 16 > n || !Rigid(f + o)) continue;
        double V[16], R[16], iV[16], iR[16];
        for (int i = 0; i < 16; ++i) R[i] = V[i] = f[o + i];
        ax[0] = V[0]; ax[1] = V[4]; ax[2] = V[8];   // the camera's right, in the world
        // Folded (replay_proj 3): the view stays the left one, the projection takes the shift.
        bool fold = false;
        if (g_projFix.load() == 3 && (lay.parts[k] & 4) && o + 80 <= n) {
            const double sx = g_projSx.load();
            fold = sx != 0.0 && fabs(f[o + 32] - sx) < 1e-3 * fabs(sx);
        }
        {   // which way each view goes, once per (layout, view, way)
            static std::vector<std::pair<int, int>> told;
            static SRWLOCK tl = SRWLOCK_INIT;
            const int way = fold ? 1 : 2;
            const int pk = o + 48 <= n ? (int)(f[o + 32] * 1000.0f) : -1;
            AcquireSRWLockExclusive(&tl);
            bool seen = false;
            for (const auto& t : told) if (t.first == pk && t.second == o * 4 + way) seen = true;
            // full: no more lines - native OpenXR's projection moves a little every frame, and a key
            // per frame filled the list and then logged every view of every pair (2026-10-10)
            if (!seen) { if (told.size() < 200) told.push_back({pk, o * 4 + way}); else seen = true; }
            ReleaseSRWLockExclusive(&tl);
            if (!seen) LogF("vrcam: replay - view at word %d (parts %u) %s: P %.4f %.4f %.4f %.4f / %.4f %.4f (ours x %.4f)", o, lay.parts[k],
                            fold ? "FOLDED" : "MOVED", o + 48 <= n ? f[o + 32] : 0.f, o + 48 <= n ? f[o + 37] : 0.f,
                            o + 48 <= n ? f[o + 40] : 0.f, o + 48 <= n ? f[o + 41] : 0.f, o + 48 <= n ? f[o + 43] : 0.f,
                            o + 48 <= n ? f[o + 46] : 0.f, g_projSx.load());
        }
        // A view whose projection is not the eye's (orthographic, P 0.006 - 1.0: the shadow cascades'
        // light cameras, turned nearly as the eye is and so taken for it) is no eye camera: moved,
        // the right eye's shadow maps were drawn from a light shifted by the eyes' distance and its
        // shadows lay where the left eye's were (2026-10-10). Left alone, in every mode.
        if ((lay.parts[k] & 4) && o + 48 <= n) {
            const double sx = g_projSx.load();
            if (sx == 0.0 || fabs(f[o + 32] - sx) > 1e-3 * fabs(sx) || f[o + 43] == 0.0f) continue;
        }
        if (fold) {
            double P[16], Pr[16], Pi[16], vp[16], ivp[16];
            for (int i = 0; i < 16; ++i) P[i] = f[o + 32 + i];
            P[8] -= 2.0 * g_projShift.load();   // the right eye's own off-axis frustum
            // T * P, T moving x by -shift in view space (row-major, v*M): only row 3 changes
            for (int i = 0; i < 16; ++i) Pr[i] = P[i];
            for (int c = 0; c < 4; ++c) Pr[12 + c] = P[12 + c] - shift * P[c];
            bool piOk = Inv4(Pr, Pi);
            for (int i = 0; i < 16 && piOk; ++i) piOk = std::isfinite(Pi[i]);
            Put(f + o + 32, Pr, 16);
            if (piOk) Put(f + o + 48, Pi, 16);
            Mul(V, Pr, vp);
            Put(f + o + 64, vp, 16);
            if ((lay.parts[k] & 8) && o + 96 <= n && piOk) {
                InvRigid(V, iV);
                Mul(Pi, iV, ivp);
                Put(f + o + 80, ivp, 16);
            }
            ++done;
            g_nFold.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        g_nView.fetch_add(1, std::memory_order_relaxed);
        R[12] -= shift;
        InvRigid(V, iV); InvRigid(R, iR);
        Put(f + o, R, 16);
        if ((lay.parts[k] & 2) && o + 32 <= n) Put(f + o + 16, iR, 16);
        if ((lay.parts[k] & 4) && o + 80 <= n) {
            double P[16], vp[16];
            for (int i = 0; i < 16; ++i) P[i] = f[o + 32 + i];
            // The right eye's own off-axis frustum: the left pass's projection (ours - its x
            // scale is vrcam's) shifted the other way. Without it the right eye had the left
            // eye's shift: parallel eyes, the world in front of the screen ("mono").
            double Pi[16];
            bool piOk = false;
            const double ps = g_projShift.load(), sx = g_projSx.load();
            const int fix = g_projFix.load();
            if (fix && ps != 0.0 && sx != 0.0 && fabs(P[0] - sx) < 1e-3 * fabs(sx)) {
                P[8] -= 2.0 * ps;
                Put(f + o + 32, P, 16);
                if (fix == 1) {
                    piOk = Inv4(P, Pi);
                    for (int i = 0; i < 16 && piOk; ++i) piOk = std::isfinite(Pi[i]);
                    if (piOk) Put(f + o + 48, Pi, 16);
                }
            }
            Mul(R, P, vp);
            Put(f + o + 64, vp, 16);
            if ((lay.parts[k] & 8) && o + 96 <= n) {
                double ivp[16];
                if (!piOk) for (int i = 0; i < 16; ++i) Pi[i] = f[o + 48 + i];
                Mul(Pi, iR, ivp);
                Put(f + o + 80, ivp, 16);
            }
        }
        ++done;
    }
    const int posMode = g_replayPositions.load();
    for (int k = 0; k < (posMode == 1 || (posMode == 2 && lay.nc == 1) ? lay.nc : 0); ++k) {
        const int o = lay.co[k];
        if (o + 3 > n) continue;
        for (int i = 0; i < 3; ++i) f[o + i] = (float)(f[o + i] + ax[i] * shift);
        g_nPos.fetch_add(1, std::memory_order_relaxed);
        ++done;
    }
    return done;
}

// One constant buffer, in place (words; n of them): returns what it found.
int Patch(float* f, int n, const double L[16], const double R[16]) {
    double iL[16], iR[16];
    InvRigid(L, iL); InvRigid(R, iR);
    int found = 0;
    std::vector<int> covered;
    for (int o = 0; o + 16 <= n; ++o) {
        if (!Near(f + o, L, 16, 1e-5, 1e-3)) continue;
        Put(f + o, R, 16); found |= 1; covered.push_back(o);
        if (o + 32 <= n && Near(f + o + 16, iL, 16, 1e-5, 2e-2)) { Put(f + o + 16, iR, 16); found |= 2; covered.push_back(o + 16); }
        if (o + 96 <= n) {
            double P[16], Pi[16], vpL[16], vpR[16], ivpL[16], ivpR[16];
            for (int i = 0; i < 16; ++i) { P[i] = f[o + 32 + i]; Pi[i] = f[o + 48 + i]; }
            Mul(L, P, vpL); Mul(R, P, vpR);
            if (Near(f + o + 64, vpL, 16, 1e-4, 1e-2)) {
                Put(f + o + 64, vpR, 16); found |= 4;
                Mul(Pi, iL, ivpL); Mul(Pi, iR, ivpR);
                if (Near(f + o + 80, ivpL, 16, 1e-3, 1e-1)) { Put(f + o + 80, ivpR, 16); found |= 8; }
            }
        }
        o += 15;
    }
    for (int o = 0; o + 3 <= n; ++o) {   // the camera position on its own (not inside a matrix just written)
        if (!Near(f + o, iL + 12, 3, 1e-6, 1e-3)) continue;
        bool inside = false;
        for (int c : covered) if (o >= c && o < c + 16) inside = true;
        if (inside) continue;
        Put(f + o, iR + 12, 3); found |= 16;
        o += 2;
    }
    return found;
}

// Native command list hooks: the current root signature per list, the camera bindings.
using SetRSFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using SetCBVFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
// D3D12 gives each command list type its own vtable (its own functions): the direct
// lists' and the compute lists' are hooked, each with its own trampolines (slot).
SetRSFn OrigSetGfxRS[2] = {}, OrigSetCompRS[2] = {};
SetCBVFn OrigSetGfxCBV[2] = {}, OrigSetCompCBV[2] = {};
thread_local ID3D12GraphicsCommandList* t_list = nullptr;
thread_local ID3D12RootSignature* t_rs[2] = {};
struct Key { ID3D12RootSignature* rs; UINT param; int verdict; Layout lay; };   // verdict 1 camera, 0 not
std::vector<Key> g_keys;
SRWLOCK g_keyLock = SRWLOCK_INIT;
struct Va { uint64_t va; Layout lay; };
std::vector<Va> g_vas;             // this left pass's camera buffers
SRWLOCK g_vaLock = SRWLOCK_INIT;

// Each thread's own cache of verdicts (31,000 root CBVs a pass: no lock on the way).
struct Cached { ID3D12RootSignature* rs; UINT param; int verdict; };
thread_local Cached t_cache[64] = {};
thread_local uint32_t t_cacheGen = 0;
extern std::atomic<uint32_t> g_keyGen;
int VerdictSlow(ID3D12RootSignature* rs, UINT param, uint64_t va);
inline int Verdict(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    if (t_cacheGen != g_keyGen.load(std::memory_order_relaxed)) { t_cacheGen = g_keyGen.load(); for (Cached& x : t_cache) x = {}; }
    Cached& c = t_cache[(((uintptr_t)rs >> 4) ^ (param * 7u)) & 63];
    if (c.rs == rs && c.param == param) return c.verdict;
    const int v = VerdictSlow(rs, param, va);
    if (v >= 0) c = {rs, param, v};
    return v > 0 ? v : 0;
}
// -1: no verdict yet (not in an upload buffer this time)
int VerdictSlow(ID3D12RootSignature* rs, UINT param, uint64_t va) {
    AcquireSRWLockShared(&g_keyLock);
    int v = -1;
    for (size_t i = 0; i < g_keys.size(); ++i) if (g_keys[i].rs == rs && g_keys[i].param == param) { v = g_keys[i].verdict ? 1 + (int)i : 0; break; }
    ReleaseSRWLockShared(&g_keyLock);
    if (v >= 0) return v;
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) { g_nNoCpu.fetch_add(1); return -1; }   // not an upload buffer: unknown this time, asked again next time
    g_nJudged.fetch_add(1);
    // the whole slot's worth (4 KB): judged by the first 1 KB, a buffer with its view further on
    // (the right eye's shadows: one copy of the left view outside every slot) was no camera
    float f[1024];
    const int n = (int)std::min<uint64_t>(room, sizeof f) / 4;
    memcpy(f, cpu, n * 4);
    double L[16], R[16];
    Views(L, R);
    const Layout lay = Learn(f, n, L);
    const bool cam = lay.nv || lay.nc;
    AcquireSRWLockExclusive(&g_keyLock);
    g_keys.push_back({rs, param, cam ? 1 : 0, lay});
    v = cam ? (int)g_keys.size() : 0;   // 1 + its index
    ReleaseSRWLockExclusive(&g_keyLock);
    if (cam) {
        char vs[160] = "";
        for (int k = 0, at = 0; k < lay.nv && at < 150; ++k) at += snprintf(vs + at, sizeof vs - at, " %d(%u)", lay.vo[k], lay.parts[k]);
        LogF("vrcam: replay - camera constants: root signature %p parameter %u - %d view(s) at%s, %d position(s) at %d %d %d %d",
             (void*)rs, param, lay.nv, vs, lay.nc, lay.co[0], lay.co[1], lay.co[2], lay.co[3]);
    }
    return v;
}
uint64_t Redirect(uint64_t va, const Layout& lay, int key, bool* fresh);
int KeyCount(int key);
extern std::atomic<bool> g_redirect;
extern std::atomic<int> g_nSlots;
constexpr int kSlotsFwd = 256;
// A "camera" key whose buffers fill the slots is an object's (its position near the hero
// passed for the camera's): not a camera from now on, in every thread's cache too.
std::atomic<uint32_t> g_keyGen{0};
void Demote(int key) {
    AcquireSRWLockExclusive(&g_keyLock);
    if (g_keys[key - 1].verdict) {
        g_keys[key - 1].verdict = 0;
        g_keyGen.fetch_add(1);
        LogF("vrcam: replay - root signature %p parameter %u: too many buffers a pass - an object's, not the camera's",
             (void*)g_keys[key - 1].rs, g_keys[key - 1].param);
    }
    ReleaseSRWLockExclusive(&g_keyLock);
}
uint64_t Note(int which, ID3D12GraphicsCommandList* cl, UINT param, uint64_t va) {
    if (!g_capture.load(std::memory_order_relaxed) || !va) return 0;
    ID3D12RootSignature* rs = t_list == cl ? t_rs[which] : nullptr;
    if (!rs) return 0;
    const int key = Verdict(rs, param, va);
    if (!key) return 0;
    AcquireSRWLockShared(&g_keyLock);
    const Layout lay = g_keys[key - 1].lay;
    ReleaseSRWLockShared(&g_keyLock);
    if (g_redirect.load(std::memory_order_relaxed)) {
        bool fresh = false;
        const uint64_t to = Redirect(va, lay, key, &fresh);
        if (fresh && !lay.nv && KeyCount(key) > 24) Demote(key);   // a position-only key with a 25th buffer in a pass: an object's
        return to;
    }
    AcquireSRWLockExclusive(&g_vaLock);
    bool seen = false;
    for (const Va& x : g_vas) if (x.va == va) { seen = true; break; }
    if (!seen) g_vas.push_back({va, lay});
    ReleaseSRWLockExclusive(&g_vaLock);
    return 0;
}
template <int S> void STDMETHODCALLTYPE HookSetGfxRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[1] = nullptr; }
    t_rs[0] = rs; OrigSetGfxRS[S](cl, rs);
}
template <int S> void STDMETHODCALLTYPE HookSetCompRS(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs) {
    if (t_list != cl) { t_list = cl; t_rs[0] = nullptr; }
    t_rs[1] = rs; OrigSetCompRS[S](cl, rs);
}
template <int S> void STDMETHODCALLTYPE HookSetGfxCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    const uint64_t to = Note(0, cl, p, va);
    OrigSetGfxCBV[S](cl, p, to ? to : va);
}
template <int S> void STDMETHODCALLTYPE HookSetCompCBV(ID3D12GraphicsCommandList* cl, UINT p, D3D12_GPU_VIRTUAL_ADDRESS va) {
    const uint64_t to = Note(1, cl, p, va);
    OrigSetCompCBV[S](cl, p, to ? to : va);
}

std::vector<void*> g_hookedFns;   // function addresses hooked already (vtables may share them)
SRWLOCK g_hookLock = SRWLOCK_INIT;
bool HookOne(void* fn, void* detour, void** orig) {
    if (std::find(g_hookedFns.begin(), g_hookedFns.end(), fn) != g_hookedFns.end()) return true;
    const bool ok = MH_CreateHook(fn, detour, orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (ok) g_hookedFns.push_back(fn);
    return ok;
}
template <int S> bool HookTable(void** vt) {
    bool ok = true;
    ok &= HookOne(vt[29], (void*)&HookSetCompRS<S>, (void**)&OrigSetCompRS[S]);
    ok &= HookOne(vt[30], (void*)&HookSetGfxRS<S>, (void**)&OrigSetGfxRS[S]);
    ok &= HookOne(vt[37], (void*)&HookSetCompCBV<S>, (void**)&OrigSetCompCBV[S]);
    ok &= HookOne(vt[38], (void*)&HookSetGfxCBV<S>, (void**)&OrigSetGfxCBV[S]);
    return ok;
}
bool g_slotDone[2] = {};
void OnReset(command_list* cl) {
    if (g_slotDone[0] && g_slotDone[1]) return;
    ID3D12GraphicsCommandList* n = (ID3D12GraphicsCommandList*)cl->get_native();
    const D3D12_COMMAND_LIST_TYPE t = n->GetType();
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : -1;
    if (slot < 0) return;
    AcquireSRWLockExclusive(&g_hookLock);
    if (!g_slotDone[slot]) {
        g_slotDone[slot] = true;
        void** vt = *(void***)n;
        const bool ok = slot == 0 ? HookTable<0>(vt) : HookTable<1>(vt);
        LogF("vrcam: replay - %s command list hooks (root signature, root CBV) %s, vtable %p",
             slot == 0 ? "direct" : "compute", ok ? "in" : "FAILED", (void*)vt);
    }
    ReleaseSRWLockExclusive(&g_hookLock);
}

void Register() {
    reshade::register_event<reshade::addon_event::init_resource>(&d2rvr::D3D12Only<&OnInitResource>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&OnDestroyResource>::Call);
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
}
// No waiting for the GPU (2026-10-09): the camera buffers the left pass binds are bound
// from slots of our own GPU buffer instead. At the binding, the game's values are copied
// into an upload staging slot for the left eye and, moved by the eye separation, into
// one for the right; the GPU copies the left ones into the slots before the left pass's
// lists and the right ones before the replay - the queue keeps the order, the CPU never
// waits, and the game's own buffers are never touched.
constexpr int kSlots = 256, kSlotBytes = 4096, kRing = 4;   // slots a pass; staging for kRing pairs
ID3D12Resource* g_slotsBuf = nullptr;      // DEFAULT: what the shaders read
ID3D12Resource* g_stageBuf = nullptr;      // UPLOAD: [ring][eye][slot]
uint8_t* g_stageCpu = nullptr;
uint64_t g_slotsVa = 0;
int g_ring = 0;                            // this pair's staging
struct SlotUse { uint64_t gameVa; int bytes; int key; };
SlotUse g_slotUse[kSlots];
std::atomic<int> g_nSlots{0};
SRWLOCK g_slotLock = SRWLOCK_INIT;
std::atomic<bool> g_redirect{false};       // binding from slots this left pass
std::atomic<uint32_t> g_overflow{0};

bool MakeSlots(ID3D12Device* dev) {
    if (g_slotsBuf) return true;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Width = (UINT64)kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            __uuidof(ID3D12Resource), (void**)&g_slotsBuf))) return false;
    rd.Width = (UINT64)kRing * 2 * kSlots * kSlotBytes;
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            __uuidof(ID3D12Resource), (void**)&g_stageBuf))) return false;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_stageBuf->Map(0, &none, (void**)&g_stageCpu))) return false;
    g_slotsVa = g_slotsBuf->GetGPUVirtualAddress();
    LogF("vrcam: replay - camera slots made: %d x %d bytes on the GPU, staging for %d pairs", kSlots, kSlotBytes, kRing);
    return true;
}
uint8_t* Stage(int ring, int eye, int slot) { return g_stageCpu + ((size_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }
uint64_t StageOffset(int ring, int eye, int slot) { return ((uint64_t)(ring * 2 + eye) * kSlots + slot) * kSlotBytes; }

// From the binding hook: the slot to bind instead of the game's buffer (0: bind the game's).
int KeyCount(int key) {
    AcquireSRWLockShared(&g_slotLock);
    int c = 0;
    const int n = g_nSlots.load();
    for (int i = 0; i < n; ++i) c += g_slotUse[i].key == key;
    ReleaseSRWLockShared(&g_slotLock);
    return c;
}
// A buffer's slot this pass, found without a lock: the binding hook runs on the game's five
// recording threads ~250 times a pass, and the exclusive lock with a scan of the slots under
// it held them up - a third of the draw thread's time waited in it (2026-10-09, 69 pairs/s
// with the replay against 108 without). Open addressing, va 0 = empty, cleared each left pass;
// an entry is written whole under g_slotLock (slot first, then va), read without it.
constexpr int kHash = 1024;   // a power of two, 4 x kSlots
std::atomic<uint64_t> g_hashVa[kHash];
std::atomic<int> g_hashSlot[kHash];
uint32_t HashOf(uint64_t va) { return (uint32_t)((va >> 8) * 0x9E3779B97F4A7C15ull >> 54) & (kHash - 1); }
int FindSlot(uint64_t va) {
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1)) {
        const uint64_t v = g_hashVa[h].load(std::memory_order_acquire);
        if (v == va) return g_hashSlot[h].load(std::memory_order_relaxed);
        if (!v) return -1;
    }
    return -1;
}
void PutSlot(uint64_t va, int slot) {   // under g_slotLock
    for (uint32_t h = HashOf(va), i = 0; i < kHash; ++i, h = (h + 1) & (kHash - 1))
        if (!g_hashVa[h].load(std::memory_order_relaxed)) {
            g_hashSlot[h].store(slot, std::memory_order_relaxed);
            g_hashVa[h].store(va, std::memory_order_release);
            return;
        }
}
void ClearSlots() { for (int i = 0; i < kHash; ++i) g_hashVa[i].store(0, std::memory_order_relaxed); }
uint64_t Redirect(uint64_t va, const Layout& lay, int key, bool* fresh) {
    if (const int i = FindSlot(va); i >= 0) return g_slotsVa + (uint64_t)i * kSlotBytes;
    AcquireSRWLockExclusive(&g_slotLock);
    const int n = g_nSlots.load();
    if (const int i = FindSlot(va); i >= 0) { ReleaseSRWLockExclusive(&g_slotLock); return g_slotsVa + (uint64_t)i * kSlotBytes; }
    *fresh = true;
    if (n >= kSlots) { ReleaseSRWLockExclusive(&g_slotLock); g_overflow.fetch_add(1); return 0; }
    uint64_t room = 0;
    uint8_t* cpu = Cpu(va, &room);
    if (!cpu) { ReleaseSRWLockExclusive(&g_slotLock); return 0; }
    const int bytes = (int)std::min<uint64_t>(room, kSlotBytes) & ~3;
    uint8_t* left = Stage(g_ring, 0, n);
    uint8_t* right = Stage(g_ring, 1, n);
    memcpy(left, cpu, bytes);
    if (g_replayPrev.load() >= 1) PrevToRightEye((float*)left, bytes / 4, lay, 2.0 * g_eyeHalf.load() * g_shiftMul.load());
    memcpy(right, left, bytes);
    if (g_replayPrev.load() == 2) {   // the right eye's "last frame" = the left eye's current view*projection
        const int mask = g_viewMask.load() ? g_viewMask.load() : (1 << g_replayViews.load()) - 1;
        for (int k = 0; k < lay.nv; ++k) {
            if (!(mask & (1 << k)) || !(lay.parts[k] & 4)) continue;
            const int o = lay.vo[k];
            if ((o + 112) * 4 <= bytes) memcpy(right + (o + 96) * 4, left + (o + 64) * 4, 64);
            // and the rest of "the pass before" as two passes give the right record (cb dump,
            // 2026-10-10): the left eye's view from its third word at +130, its inverse at +144,
            // its projection's first 12 words at +160 - zeros in the left record. Without them the
            // right eye's character came out darker (its lighting blended against nothing).
            if ((o + 172) * 4 <= bytes) {
                memcpy(right + (o + 130) * 4, left + (o + 2) * 4, 14 * 4);
                memcpy(right + (o + 144) * 4, left + (o + 16) * 4, 16 * 4);
                memcpy(right + (o + 160) * 4, left + (o + 32) * 4, 12 * 4);
            }
        }
    }
    double L[16], R[16];
    Views(L, R);
    const double right3[3] = {L[0], L[4], L[8]};
    PatchKnown((float*)right, bytes / 4, lay, 2.0 * g_eyeHalf.load() * g_shiftMul.load(), right3);
    g_slotUse[n] = {va, bytes, key};
    g_nSlots.store(n + 1);
    PutSlot(va, n);
    ReleaseSRWLockExclusive(&g_slotLock);
    return g_slotsVa + (uint64_t)n * kSlotBytes;
}
// Our copy list's body: this pair's staged values of one eye into the slots.
void RecordSlots(ID3D12GraphicsCommandList* l, int eye, int from = 0, int to = -1) {
    const int n = to < 0 ? g_nSlots.load() : to;
    for (int i = from; i < n; ++i)
        l->CopyBufferRegion(g_slotsBuf, (UINT64)i * kSlotBytes, g_stageBuf, StageOffset(g_ring, eye, i), (UINT64)g_slotUse[i].bytes);
}

// [debug] replay_scan_go 0 -> 1, once: every copy of the left eye's camera (the view, and the
// view*projection, of the first camera slot with a view) anywhere in the game's upload buffers,
// logged with whether one of our slots covers it - the right eye's shadows stayed where the left
// eye had them: a pass reads the camera from a buffer the root CBV hook never sees.
std::atomic<int> g_scanWant{0};
void ScanCopies() {
    AcquireSRWLockShared(&g_slotLock);
    const int n = g_nSlots.load();
    int si = -1, vo = -1; uint8_t parts = 0;
    for (int i = 0; i < n && si < 0; ++i) {
        const int key = g_slotUse[i].key;
        if (key <= 0) continue;
        AcquireSRWLockShared(&g_keyLock);
        const Layout lay = g_keys[key - 1].lay;
        ReleaseSRWLockShared(&g_keyLock);
        if (lay.nv) { si = i; vo = lay.vo[0]; parts = lay.parts[0]; }
    }
    float V[16] = {}, VP[16] = {};
    if (si >= 0) {
        const float* f = (const float*)Stage(g_ring, 0, si);
        memcpy(V, f + vo, sizeof V);
        if (parts & 4) memcpy(VP, f + vo + 64, sizeof VP);
    }
    std::vector<std::pair<uint64_t, int>> slotsCopy;
    for (int i = 0; i < n; ++i) slotsCopy.push_back({g_slotUse[i].gameVa, g_slotUse[i].bytes});
    ReleaseSRWLockShared(&g_slotLock);
    if (si < 0) { Log("vrcam: replay scan - no camera slot with a view this pass"); return; }
    AcquireSRWLockShared(&g_upLock);
    const std::vector<Upload> ups = g_uploads;
    ReleaseSRWLockShared(&g_upLock);
    uint64_t scanned = 0; int hits = 0, uncovered = 0;
    const double t0 = flog::UsNow();
    for (const Upload& u : ups) {
        if (!u.cpu || u.size < 64) continue;
        scanned += u.size;
        const float* w = (const float*)u.cpu;
        const uint64_t nw = u.size / 4;
        for (uint64_t k = 0; k + 16 <= nw; ++k) {
            const bool isV = w[k] == V[0] && w[k + 1] == V[1] && w[k + 5] == V[5] && memcmp(w + k, V, 64) == 0;
            const bool isVP = !isV && (parts & 4) && w[k] == VP[0] && w[k + 5] == VP[5] && memcmp(w + k, VP, 64) == 0;
            if (!isV && !isVP) continue;
            const uint64_t va = u.va + k * 4;
            bool covered = false;
            for (const auto& sc : slotsCopy) if (va >= sc.first && va < sc.first + (uint64_t)sc.second) covered = true;
            ++hits; if (!covered) ++uncovered;
            if (hits <= 40) LogF("vrcam: replay scan - %s at %llx (buffer %llx + %llu of %llu)%s", isV ? "view" : "view*proj",
                                 (unsigned long long)va, (unsigned long long)u.va, (unsigned long long)(k * 4),
                                 (unsigned long long)u.size, covered ? " - in a slot" : " - NOT in a slot");
            k += 15;
        }
    }
    LogF("vrcam: replay scan - %d copies (%d not in a slot) in %.1f MB of %zu upload buffers, %.1f ms; slots %d",
         hits, uncovered, scanned / 1048576.0, ups.size(), (flog::UsNow() - t0) / 1000.0, n);
}
void BeginLeft(bool redirect) {
    AcquireSRWLockExclusive(&g_vaLock); g_vas.clear(); ReleaseSRWLockExclusive(&g_vaLock);
    g_ring = (g_ring + 1) % kRing;
    g_nSlots.store(0);
    ClearSlots();
    g_redirect.store(redirect && g_slotsBuf);
    g_capture.store(true);
}

// What was rewritten, to be put back: the game does not write a camera buffer again
// while the camera holds still - it kept drawing the left eye with our right camera
// (stereo for a few seconds, then both eyes the right one).
struct Saved { uint8_t* cpu; int n; float orig[256], mine[256]; };
std::vector<Saved> g_saved;
// After the replay is done on the GPU, before the game's next left pass reaches the GPU:
// the left values back, where the game has not written new ones meanwhile.
void Restore() {
    for (const Saved& sv : g_saved)
        if (memcmp(sv.cpu, sv.mine, sv.n * 4) == 0) memcpy(sv.cpu, sv.orig, sv.n * 4);
    g_saved.clear();
}
// After the left pass is done on the GPU: its camera buffers, rewritten for the right eye.
int RightEye() {
    g_capture.store(false);
    std::vector<Va> vas;
    AcquireSRWLockExclusive(&g_vaLock); vas.swap(g_vas); ReleaseSRWLockExclusive(&g_vaLock);
    double L[16], R[16];
    Views(L, R);
    const double shift = 2.0 * g_eyeHalf.load();
    const double right[3] = {L[0], L[4], L[8]};
    int done = 0;
    for (const Va& x : vas) {
        uint64_t room = 0;
        uint8_t* cpu = Cpu(x.va, &room);
        if (!cpu) continue;
        float f[256];
        const int n = (int)std::min<uint64_t>(room, sizeof f) / 4;
        memcpy(f, cpu, n * 4);
        Saved sv;
        sv.cpu = cpu; sv.n = n;
        memcpy(sv.orig, f, n * 4);
        if (PatchKnown(f, n, x.lay, shift, right)) {
            memcpy(cpu, f, n * 4);
            memcpy(sv.mine, f, n * 4);
            g_saved.push_back(sv);
            ++done;
        }
    }
    if (g_patched.fetch_add(1) % 900 == 0) {
        LogF("vrcam: replay - the right eye's camera: %d of %zu camera buffers rewritten", done, vas.size());
        LogF("vrcam: replay - root CBVs seen %u (no root signature %u, not in an upload buffer %u, judged %u); buffers made %u, upload ones %u, known %zu",
             g_nNote.load(), g_nNoRS.load(), g_nNoCpu.load(), g_nJudged.load(), g_nInit.load(), g_nUploads.load(), g_uploads.size());
    }
    return done;
}
}  // namespace camfix

namespace replay {
using namespace reshade::api;
std::atomic<bool> g_on{false};
std::atomic<int> g_capturing{0};   // 1 while the left pass runs
std::atomic<int> g_want{0};        // [stereo] replay_right, from LoadSettings
struct Sub { ID3D12CommandQueue* q; ID3D12CommandList* cl; };
std::vector<Sub> g_subs;           // the left pass's direct and compute submissions, in order
SRWLOCK g_lock = SRWLOCK_INIT;
IDXGISwapChain* g_proxy = nullptr; // the game's swap chain (ReShade's proxy): what it presents through
UINT g_presentFlags = 0;
IDXGISwapChain3* g_native = nullptr;
HANDLE g_latency = nullptr;   // the swap chain's frame latency waitable object, if it was made with one
bool g_latencyAsked = false;
bool g_leftHeld = false;           // the left pass's Present was held back and its picture copied out
uint8_t* g_wrap = nullptr;         // the game's swap chain wrapper (its present's r8); the game keeps the
                                   // back buffer index it draws into next at +0x98 (D2R 0x10FAE65: right after
                                   // its present it asks the swap chain and stores it there)
ID3D12Device* g_dev = nullptr;
ID3D12CommandQueue* g_direct = nullptr;
// A fence per queue: one fence signalled from two queues went back down when the
// second finished first, and a GPU wait on it never ended (the game hung).
struct Pt { ID3D12Fence* f = nullptr; UINT64 v = 0; };
// Where a replayed pair's time goes, us summed over the 10 s log period (Steps).
enum { kWaitPrev, kWaitRestore, kHold, kWaitLeft, kPatch, kSubmit, kPresentR, kCopyL, kPresentL, kSteps };
double g_stepUs[kSteps] = {};
uint32_t g_stepPairs = 0;
struct StepTimer { int k; double t0; StepTimer(int k_) : k(k_), t0(pairtime::UsNow()) {} ~StepTimer() { g_stepUs[k] += pairtime::UsNow() - t0; } };
// The frame log's Y line: this pair's steps, us (the sums since the last pair; they restart every 10 s).
void PairLine(bool replayed) {
    static double last[kSteps] = {};
    double d[kSteps];
    for (int i = 0; i < kSteps; ++i) { d[i] = g_stepUs[i] >= last[i] ? g_stepUs[i] - last[i] : g_stepUs[i]; last[i] = g_stepUs[i]; }
    flog::Line("Y,%u,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f", flog::Pass(), replayed ? 1 : 0,
               d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8]);
}
struct QFence { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; };
std::vector<QFence> g_qf;
Pt g_lastReplay, g_prevReplay;
ID3D12Fence* g_fence = nullptr;   // (made with the first one: Setup's "ready")
std::atomic<bool> g_restorePending{false};
std::atomic<int> g_ahead{1};   // [stereo] replay_ahead: replays the CPU may run ahead of (1 or 2)   // the camera buffers to put back before the left pass reaches the GPU
HANDLE g_event = nullptr;
constexpr int kAllocs = 16;
ID3D12CommandAllocator* g_alloc[kAllocs] = {};
Pt g_allocDone[kAllocs] = {};
int g_allocAt = 0, g_openAlloc = 0;
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Resource* g_leftImg = nullptr;   // the left eye's picture, out of the back buffer
// One present a pair ([stereo] replay_fx, on): at the hold the left eye's effects run on its
// back buffer with its own depth (render_effects) and FlatVR's add-on takes it from that
// effect pass; the replay draws the right eye into the same back buffer, presented once -
// as many presents as the game's waits (two a pair drifted and hung), the sky by each
// eye's own depth, no copy of the left picture.
reshade::api::effect_runtime* g_rt = nullptr;
std::atomic<bool> g_fxAtHold{true};
// ReShade renders its effects once between two presents (render_effects returns at once the
// second time): the right eye's, rendered after the left eye's at the hold, never ran - no sky
// and FlatVR's add-on, which takes the picture from the effect pass, had no right eyes (its
// stream stopped). The right eye gets only our technique (D2R_DepthFog_R, render_technique:
// no once-a-frame check, and the begin / finish effects events FlatVR's add-on takes it from).
reshade::api::effect_technique g_rightTech{};
// DLSS in the replay: the left pass's list carries both evaluations - the game's DLSS instance
// and the right eye's twin - each under D3D12 predication on this buffer: [0] (u64) the game's,
// [1] the twin's, 0 = skipped. Before the left pass {1, 0}, before the replay {0, 1}: each eye
// runs on its own instance and history (one instance for both smeared each eye into the other).
ID3D12Resource* g_predBuf = nullptr;   // DEFAULT, 16 bytes used, D3D12_RESOURCE_STATE_PREDICATION once written
ID3D12Resource* g_predSrc = nullptr;   // UPLOAD: {1, 0} at 0, {0, 1} at 16
bool g_predReady = false;
std::atomic<uint32_t> g_predEvals{0};
std::atomic<bool> g_dlssBoth{false};   // [stereo] replay_dlss: 1 both eyes' evaluations, predicated; 0 the game's alone (one instance)
// [stereo] replay_fx_sync (on): the left eye's effects wait for the last pair to be done on the
// GPU. ReShade writes an effect's constants in place, once per render: with the CPU a pair
// ahead, the next left eye's values (its frame stamps among them) were in the buffer before
// the GPU drew the last right eye's effects - its picture went to FlatVR with the next pair's
// head stamp, and the right eye juddered as the head turned.
std::atomic<bool> g_fxSync{true};
uint64_t g_rtvRes[8] = {};
reshade::api::resource_view g_rtv[8] = {};
// The depth buffer ReShade's effects read (D2R_DepthFog.fx FogDepthTex's binding) and its
// state as the game's last barrier left it: the left eye is presented after the replay,
// when that buffer holds the right eye's depth - the sky and the fog of the left eye were
// drawn by the right eye's depth (a dark seam beside the head). Its depth goes back first.
std::atomic<uint64_t> g_depthRes{0};
std::atomic<uint32_t> g_depthState{0};   // 0: no barrier seen - the game keeps it as a depth target
// [stereo] replay_depth (off): the copy needs the buffer's true state - COMMON assumed lost the
// shadows, DEPTH_WRITE removed the device (0x887A0001); no barrier on it is ever seen.
std::atomic<bool> g_depthCopy{false};
// The state to transition the depth buffer from: the game's last barrier on it, else DEPTH_WRITE
// (COMMON assumed for a depth target the game never moves broke its compression: no shadows).
D3D12_RESOURCE_STATES DepthState() {
    const uint32_t st = g_depthState.load();
    return st ? (D3D12_RESOURCE_STATES)st : D3D12_RESOURCE_STATE_DEPTH_WRITE;
}
ID3D12Resource* g_leftDepth = nullptr;
std::atomic<uint32_t> g_replays{0}, g_fails{0};
// The back buffer indices of the last pairs, for the watchdog: the one the left pass drew in
// (DXGI's, and the game's own at its wrapper +0x98), the right eye's, DXGI's after our present.
struct PairIdx { UINT hold, game, right, after, presents; HRESULT hr; };
PairIdx g_pairIdx[16] = {};
uint32_t g_pairIdxAt = 0;
UINT g_gameIdxNow = ~0u;

// The game's end-of-frame signals held until the replay is on the queue (2026-10-09). The
// game signals its frame fences after its last lists, and on the GPU those came before our
// replay: the game took the frame for done and freed what it drew with while the replay
// still read it (the device hung at an area change, 0x887A0006). A direct or compute queue's
// signals are kept back while the left pass runs; the next submission on that queue lets
// them go first (they were mid-frame - compute waits on them), and the ones left at the
// game's present are issued after the replay.
using QSignalFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
// each queue type may have its own vtable (the command lists do): a trampoline per slot
QSignalFn OrigQSignalT[3] = {};
QSignalFn OrigQSignal = nullptr;   // any of them, for our own calls (they go through the queue's vtable anyway)
struct HeldSig { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; };
std::vector<HeldSig> g_held;
SRWLOCK g_heldLock = SRWLOCK_INIT;
std::atomic<bool> g_holdSignals{false};
// [stereo] replay_hold_signals (off): the counters showed none ever left for the replay (the
// game's own queue waits let them go), and a signal held while the game loads an area is a
// wait that never ends.
std::atomic<bool> g_holdSignalsOn{false};
std::atomic<uint32_t> g_heldMax{0};
thread_local bool t_ours = false;   // our own signals pass
std::atomic<uint32_t> g_sigCalls{0}, g_sigHeld{0}, g_sigWhileHolding{0};
// The game's own Signal / Wait / ExecuteCommandLists on its queues, the last 1024, for the
// watchdog: what a stuck queue waits for and who was to signal it (an area change hung the
// direct queue inside the replay, 2026-10-09).
struct GameOp { ID3D12CommandQueue* q; ID3D12Fence* f; UINT64 v; DWORD tid; char op; };
GameOp g_gameOps[1024];
std::atomic<uint32_t> g_gameOpAt{0};
UINT64 SafeDone(ID3D12Fence* f) { __try { return f->GetCompletedValue(); } __except (EXCEPTION_EXECUTE_HANDLER) { return ~0ull; } }
void NoteGame(ID3D12CommandQueue* q, char op, ID3D12Fence* f, UINT64 v) {
    if (op != 'w' && q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COPY) return;   // the streaming's flood: only its waits
    const uint32_t i = g_gameOpAt.fetch_add(1, std::memory_order_relaxed) & 1023;
    g_gameOps[i] = {q, f, v, GetCurrentThreadId(), op};
}
template <int S> HRESULT STDMETHODCALLTYPE HookQSignal(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) {
    g_sigCalls.fetch_add(1, std::memory_order_relaxed);
    if (!t_ours) NoteGame(q, 's', f, v);
    if (g_holdSignals.load(std::memory_order_relaxed)) g_sigWhileHolding.fetch_add(1, std::memory_order_relaxed);
    if (!t_ours && g_holdSignals.load(std::memory_order_relaxed) && q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COPY) {
        g_sigHeld.fetch_add(1, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_heldLock);
        g_held.push_back({q, f, v});
        ReleaseSRWLockExclusive(&g_heldLock);
        return S_OK;
    }
    return OrigQSignalT[S](q, f, v);
}
// Let a queue's held signals go (before its next submission, or after the replay).
void ReleaseHeld(ID3D12CommandQueue* q) {
    AcquireSRWLockExclusive(&g_heldLock);
    std::vector<HeldSig> keep;
    for (const HeldSig& h : g_held) {
        if (!q || h.q == q) { t_ours = true; h.q->Signal(h.f, h.v); t_ours = false; }
        else keep.push_back(h);
    }
    g_held.swap(keep);
    ReleaseSRWLockExclusive(&g_heldLock);
}
// A wait the game puts on its direct or compute queue lets every held signal go first: the
// other queue may be waiting for one of them (direct signals, compute waits and signals,
// direct waits for that - held, it was a deadlock). The copy queue's waits may wait on.
using QWaitFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
QWaitFn OrigQWaitT[3] = {};
template <int S> HRESULT STDMETHODCALLTYPE HookQWait(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) {
    if (!t_ours) NoteGame(q, 'w', f, v);
    if (!t_ours && g_holdSignals.load(std::memory_order_relaxed) && q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COPY) ReleaseHeld(nullptr);
    return OrigQWaitT[S](q, f, v);
}
std::atomic<bool> g_sigHooked{false};   // the direct queue's (the one that matters) is in
std::vector<void*> g_qHookedFns;
bool g_qSlotDone[3] = {};
template <int S> bool HookQTable(void** vt) {
    bool ok = true;
    if (std::find(g_qHookedFns.begin(), g_qHookedFns.end(), vt[14]) == g_qHookedFns.end()) {
        ok &= MH_CreateHook(vt[14], (void*)&HookQSignal<S>, (void**)&OrigQSignalT[S]) == MH_OK && MH_EnableHook(vt[14]) == MH_OK;
        g_qHookedFns.push_back(vt[14]);
    }
    if (std::find(g_qHookedFns.begin(), g_qHookedFns.end(), vt[15]) == g_qHookedFns.end()) {
        ok &= MH_CreateHook(vt[15], (void*)&HookQWait<S>, (void**)&OrigQWaitT[S]) == MH_OK && MH_EnableHook(vt[15]) == MH_OK;
        g_qHookedFns.push_back(vt[15]);
    }
    return ok;
}
void HookQueueSignal(ID3D12CommandQueue* q) {
    const D3D12_COMMAND_LIST_TYPE t = q->GetDesc().Type;
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : 2;
    if (g_qSlotDone[slot]) return;
    g_qSlotDone[slot] = true;
    void** vt = *(void***)q;
    const bool ok = slot == 0 ? HookQTable<0>(vt) : slot == 1 ? HookQTable<1>(vt) : HookQTable<2>(vt);
    if (slot == 0) g_sigHooked.store(ok);
    LogF("vrcam: replay - the %s queue's Signal/Wait hooked %s, vtable %p", slot == 0 ? "direct" : slot == 1 ? "compute" : "copy",
         ok ? "in" : "FAILED", (void*)vt);
}

void WaitFor(const Pt& p);
bool Setup(ID3D12CommandQueue* q);
void CopySlots(int eye);
void CopyLateSlots(ID3D12CommandQueue* nq);
bool MakePred();
void RecordPred(ID3D12GraphicsCommandList* l, int eye);
int g_leftCopied = 0;
std::atomic<uint32_t> g_lateSlots{0};
// The game's lists on its direct and compute queues outside the left pass (not replayed: what
// they draw is the left eye's in both - the right eye's shadows stayed where the left eye had
// them), by thread: the draw thread's own, and others'.
std::atomic<uint32_t> g_outsideDraw{0}, g_outsideOther{0}, g_insideLists{0};
std::atomic<DWORD> g_captureThread{0};
std::atomic<bool> g_replayCopy{true};   // [stereo] replay_copy (see OnExecute)
std::atomic<uint32_t> g_copyLists{0};
// who sends them: the last few threads, queue types and the moment (us since the pair's left pass began)
struct Outside { DWORD tid; int type; double atUs; };
Outside g_outside[8] = {};
std::atomic<uint32_t> g_outsideAt{0};
std::atomic<double> g_pairStartUs{0.0};
void OnExecute(command_queue* q, command_list* cl) {
    HookQueueSignal((ID3D12CommandQueue*)q->get_native());
    if (!t_ours) NoteGame((ID3D12CommandQueue*)q->get_native(), 'x', nullptr, 0);
    if (!t_ours && g_on.load(std::memory_order_relaxed) && g_want.load(std::memory_order_relaxed) == 1) {
        const command_queue_type t = q->get_type();
        if ((t & command_queue_type::graphics) != 0 || (t & command_queue_type::compute) != 0) {
            if (g_capturing.load(std::memory_order_relaxed)) g_insideLists.fetch_add(1, std::memory_order_relaxed);
            else if (GetCurrentThreadId() == g_captureThread.load(std::memory_order_relaxed)) g_outsideDraw.fetch_add(1, std::memory_order_relaxed);
            else {
                g_outsideOther.fetch_add(1, std::memory_order_relaxed);
                const uint32_t i = g_outsideAt.fetch_add(1) & 7;
                g_outside[i] = {GetCurrentThreadId(), (t & command_queue_type::graphics) != 0 ? 0 : 2, pairtime::UsNow() - g_pairStartUs.load()};
            }
        }
    }
    if (!g_capturing.load(std::memory_order_relaxed)) return;
    const command_queue_type t = q->get_type();
    // The copy queue: its lists from the draw thread inside the left pass are part of the frame -
    // mid-frame the direct queue signals, the copy queue waits for it, copies, and the direct
    // queue waits for that ([stereo] replay_copy, on: replayed too; left out, the right eye read
    // the left eye's copy - its character sat in shadows of its own, 2026-10-10). The streaming's
    // uploads come from other threads and stay out.
    if ((t & command_queue_type::graphics) == 0 && (t & command_queue_type::compute) == 0 &&
        !(g_replayCopy.load(std::memory_order_relaxed) && GetCurrentThreadId() == g_captureThread.load(std::memory_order_relaxed))) return;
    ReleaseHeld((ID3D12CommandQueue*)q->get_native());   // signals mid-frame: before this queue's next lists
    if (g_restorePending.exchange(false)) {   // the left pass's first list: the left camera into the slots first
        StepTimer st(kWaitRestore);
        if (camfix::g_redirect.load()) {
            if (!g_direct) {   // (the first pair: the direct queue is this one)
                D3D12_COMMAND_QUEUE_DESC d = ((ID3D12CommandQueue*)q->get_native())->GetDesc();
                if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_direct = (ID3D12CommandQueue*)q->get_native();
            }
            if (g_direct && Setup(g_direct)) CopySlots(0);
        } else {
            WaitFor(g_lastReplay);
            camfix::Restore();
        }
    }
    else CopyLateSlots((ID3D12CommandQueue*)q->get_native());
    AcquireSRWLockExclusive(&g_lock);
    g_subs.push_back({(ID3D12CommandQueue*)q->get_native(), (ID3D12CommandList*)cl->get_native()});
    ReleaseSRWLockExclusive(&g_lock);
}

void OnBarrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to) {
    const uint64_t d = g_depthRes.load(std::memory_order_relaxed);
    if (!d) return;
    for (uint32_t i = 0; i < count; ++i)
        if (res[i].handle == d) g_depthState.store((uint32_t)to[i], std::memory_order_relaxed);
}

void OnPresent(command_queue*, swapchain* sc, const rect*, const rect*, uint32_t, const rect*) {
    if (!g_native) {
        IDXGISwapChain* n = (IDXGISwapChain*)sc->get_native();
        if (n) n->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&g_native);
    }
}

bool Setup(ID3D12CommandQueue* q) {
    if (g_list) return true;
    if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&g_dev))) return false;
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence))) return false;
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (int i = 0; i < kAllocs; ++i)
        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_alloc[i]))) return false;
    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_list))) return false;
    g_list->Close();
    camfix::MakeSlots(g_dev);
    Log("vrcam: replay - fence, allocators and the copy list made");
    return true;
}

// The last 64 signals and GPU waits we put on the queues, for the watchdog.
struct Hist { ID3D12CommandQueue* q; char op; ID3D12Fence* f; UINT64 v; const char* why; };
Hist g_hist[256];
int g_histAt = 0;
const char* g_why = "";
void Note(ID3D12CommandQueue* q, char op, ID3D12Fence* f, UINT64 v) { g_hist[g_histAt++ & 255] = {q, op, f, v, g_why}; }
// [debug] replay_marks (on while the hang is hunted): a signal after every replayed list and
// around the right eye's effects and present - the watchdog's "done" then says which one the
// queue stopped at.
bool g_marks = true;
const char* const kListMark[32] = {
    "replay: list 0", "replay: list 1", "replay: list 2", "replay: list 3", "replay: list 4", "replay: list 5", "replay: list 6", "replay: list 7",
    "replay: list 8", "replay: list 9", "replay: list 10", "replay: list 11", "replay: list 12", "replay: list 13", "replay: list 14", "replay: list 15",
    "replay: list 16", "replay: list 17", "replay: list 18", "replay: list 19", "replay: list 20", "replay: list 21", "replay: list 22", "replay: list 23",
    "replay: list 24", "replay: list 25", "replay: list 26", "replay: list 27", "replay: list 28", "replay: list 29", "replay: list 30", "replay: list 31+"};
void GpuWait(ID3D12CommandQueue* q, const Pt& p) { if (p.f) { t_ours = true; q->Wait(p.f, p.v); t_ours = false; Note(q, 'W', p.f, p.v); } }
Pt SignalOn(ID3D12CommandQueue* q) {
    for (QFence& x : g_qf)
        if (x.q == q) {
            t_ours = true; q->Signal(x.f, ++x.v); t_ours = false;
            Note(q, 'S', x.f, x.v); return {x.f, x.v};
        }
    QFence x{q, nullptr, 0};
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&x.f))) return {};
    g_qf.push_back(x);
    return SignalOn(q);
}
void WaitFor(const Pt& p) {
    if (!p.f || p.f->GetCompletedValue() >= p.v) return;
    p.f->SetEventOnCompletion(p.v, g_event);
    WaitForSingleObject(g_event, 100);
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// Native OpenXR (vr/xr.cpp) on the game's queue: the runtime's own signals pass our queue
// hooks as ours do.
bool XrBegin() {
    t_ours = true;
    const bool on = xr::BeginPair(g_dev, g_direct);
    t_ours = false;
    return on;
}
void XrCopy(int eye, ID3D12Resource* bb) { t_ours = true; xr::CopyEye(eye, bb); t_ours = false; }
void XrEnd() { t_ours = true; xr::EndPair(); t_ours = false; }
void XrPresent(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    t_ours = true;
    xr::OnPresent(dev, q, bb, g_pairNow.load() ? (g_eye.load() & 1) : -1);   // a pass of two: its eye
    t_ours = false;
}

// Our own list on the direct queue: the next allocator of the ring, reset and opened.
ID3D12GraphicsCommandList* Open() {
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitFor(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    g_openAlloc = a;
    return g_list;
}
void Submit(ID3D12CommandQueue* q) {
    g_list->Close();
    ID3D12CommandList* l = g_list;
    q->ExecuteCommandLists(1, &l);
    g_allocDone[g_openAlloc] = SignalOn(q);
}
// One eye's camera values into the slots, on the direct queue.
// The left values' copies this pass reached (slots made later - by lists recorded after the
// pass's first submission - had last pair's right values in the left eye: they get theirs
// before the next submission).
void CopySlots(int eye) {
    if (eye == 0) g_leftCopied = 0;   // a new left pass: none of its slots copied yet
    if (!g_direct) return;
    MakePred();
    if (!camfix::g_nSlots.load() && !g_predBuf) return;
    g_why = eye ? "slots: right" : "slots: left";
    const int n = camfix::g_nSlots.load();
    ID3D12GraphicsCommandList* l = Open();
    camfix::RecordSlots(l, eye, 0, n);
    RecordPred(l, eye);
    if (eye == 0) g_leftCopied = n;
    Submit(g_direct);
}

// Slots made since the left values went in: theirs now, on the direct queue; a compute
// submission waits for that copy.
void CopyLateSlots(ID3D12CommandQueue* nq) {
    if (!camfix::g_redirect.load() || !g_direct || camfix::g_nSlots.load() <= g_leftCopied) return;
    const int n = camfix::g_nSlots.load();
    g_lateSlots.fetch_add((uint32_t)(n - g_leftCopied), std::memory_order_relaxed);
    g_why = "slots: left, late";
    camfix::RecordSlots(Open(), 0, g_leftCopied, n);
    g_leftCopied = n;
    Submit(g_direct);
    if (nq != g_direct) { g_why = "slots: left, late (compute waits)"; GpuWait(nq, g_allocDone[g_openAlloc]); }
}

// One copy on the direct queue, with our own list (a ring of allocators).
void Copy(ID3D12Resource* dst, D3D12_RESOURCE_STATES dstState, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState) {
    g_why = "picture copy";
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitFor(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    D3D12_RESOURCE_BARRIER b[2] = {Tr(src, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE), Tr(dst, dstState, D3D12_RESOURCE_STATE_COPY_DEST)};
    g_list->ResourceBarrier(2, b);
    g_list->CopyResource(dst, src);
    D3D12_RESOURCE_BARRIER c[2] = {Tr(src, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState), Tr(dst, D3D12_RESOURCE_STATE_COPY_DEST, dstState)};
    g_list->ResourceBarrier(2, c);
    g_list->Close();
    ID3D12CommandList* l = g_list;
    g_direct->ExecuteCommandLists(1, &l);
    g_allocDone[a] = SignalOn(g_direct);
}

// [debug] replay_peek_go: one pair's left picture (after its effects, at the hold) and right
// picture (after its effects, before the present) read back and compared - whether the right
// eye is a picture of its own (the headset showed "mono" with the replay).
std::atomic<int> g_peekWant{0};
int g_peekStage = 0;   // 0 idle, 1 this pair, 2 both copies queued
ID3D12Resource* g_peekBuf[2] = {};
D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_peekFp{};
UINT g_peekW = 0, g_peekH = 0;
Pt g_peekDone{};
void Peek(int eye, ID3D12Resource* bb) {
    const D3D12_RESOURCE_DESC d = bb->GetDesc();
    UINT64 total = 0;
    g_dev->GetCopyableFootprints(&d, 0, 1, 0, &g_peekFp, nullptr, nullptr, &total);
    g_peekW = (UINT)d.Width; g_peekH = d.Height;
    if (!g_peekBuf[eye]) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = total; rd.Height = 1;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&g_peekBuf[eye]))) { g_peekStage = 0; return; }
    }
    g_why = "peek";
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitFor(g_allocDone[a]);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
    D3D12_RESOURCE_BARRIER b = Tr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    g_list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = g_peekBuf[eye]; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = g_peekFp;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = bb; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    g_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER c = Tr(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    g_list->ResourceBarrier(1, &c);
    g_list->Close();
    ID3D12CommandList* l = g_list;
    g_direct->ExecuteCommandLists(1, &l);
    g_allocDone[a] = g_peekDone = SignalOn(g_direct);
}
void PeekReport() {
    for (int i = 0; i < 30 && g_peekDone.f && g_peekDone.f->GetCompletedValue() < g_peekDone.v; ++i) WaitFor(g_peekDone);
    const uint8_t* m[2] = {};
    D3D12_RANGE r{0, 0};
    for (int e = 0; e < 2; ++e) if (g_peekBuf[e]) g_peekBuf[e]->Map(0, nullptr, (void**)&m[e]);
    if (m[0] && m[1]) {
        const UINT pitch = g_peekFp.Footprint.RowPitch, w = g_peekW, h = g_peekH;
        const UINT bpp = std::max<UINT>(1, std::min<UINT>(pitch / std::max<UINT>(1, w), 8));
        uint64_t diff = 0, all = 0;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x, ++all)
                if (memcmp(m[0] + (size_t)y * pitch + x * bpp, m[1] + (size_t)y * pitch + x * bpp, bpp) != 0) ++diff;
        // the right picture against the left moved sideways: the shift that fits best (4-byte pixels,
        // the green byte, the middle half of the rows)
        int best = 0; double bestErr = 1e300, err0 = 0.0;
        if (bpp == 4)
            for (int sft = -80; sft <= 80; ++sft) {
                double e = 0.0; uint64_t n = 0;
                for (UINT y = h / 4; y < h * 3 / 4; y += 4)
                    for (UINT x = 100; x + 100 < w; x += 2) {
                        const int a = m[0][(size_t)y * pitch + x * 4 + 1], b = m[1][(size_t)y * pitch + (x + sft) * 4 + 1];
                        e += std::abs(a - b); ++n;
                    }
                e /= std::max<uint64_t>(1, n);
                if (sft == 0) err0 = e;
                if (e < bestErr) { bestErr = e; best = sft; }
            }
        LogF("vrcam: replay peek - %ux%u (%u bytes a pixel): left and right differ in %.1f%% of pixels; best sideways fit %d px (mean |diff| %.2f, at 0 px %.2f)",
             w, h, bpp, 100.0 * diff / std::max<uint64_t>(1, all), best, bestErr, err0);
    } else {
        Log("vrcam: replay peek - could not map the read-back pictures");
    }
    for (int e = 0; e < 2; ++e) if (m[e]) g_peekBuf[e]->Unmap(0, &r);
}

// Prototype diagnostics: the device's health after each step, the first failure logged.
bool MakePred() {
    if (g_predBuf) return true;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 256; rd.Height = 1; rd.DepthOrArraySize = 1;
    rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&g_predSrc))) return false;
    uint64_t* v = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_predSrc->Map(0, &none, (void**)&v))) return false;
    v[0] = 1; v[1] = 0; v[2] = 0; v[3] = 1;
    g_predSrc->Unmap(0, nullptr);
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&g_predBuf))) return false;
    Log("vrcam: replay - DLSS predicate made (each eye its own DLSS instance in the replay)");
    return true;
}
void RecordPred(ID3D12GraphicsCommandList* l, int eye) {
    if (!g_predBuf) return;
    if (g_predReady) { D3D12_RESOURCE_BARRIER b = Tr(g_predBuf, D3D12_RESOURCE_STATE_PREDICATION, D3D12_RESOURCE_STATE_COPY_DEST); l->ResourceBarrier(1, &b); }
    l->CopyBufferRegion(g_predBuf, 0, g_predSrc, eye ? 16 : 0, 16);
    D3D12_RESOURCE_BARRIER c = Tr(g_predBuf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PREDICATION);
    l->ResourceBarrier(1, &c);
    g_predReady = true;
}
// The left pass being recorded for a replay, with the predicate in place: both evaluations.
bool DlssBoth() { return g_dlssBoth.load() && g_on.load() && g_want.load() == 1 && g_capturing.load() == 1 && g_predReady && camfix::g_redirect.load(); }

void Check(const char* step, HRESULT hr = S_OK) {
    static bool told = false;
    if (told || !g_dev) return;
    const HRESULT dr = g_dev->GetDeviceRemovedReason();
    if (dr != S_OK || FAILED(hr)) {
        told = true;
        LogF("vrcam: replay - after %s: hr %08lX, device removed reason %08lX", step, (unsigned long)hr, (unsigned long)dr);
    }
}

ID3D12Resource* BackBuffer(UINT i) {
    ID3D12Resource* r = nullptr;
    g_native->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&r);
    return r;
}

// The left eye's effects on its back buffer now (the depth is still its own); right: the
// right eye's technique alone (see g_rightTech).
bool EffectsAtHold(ID3D12Resource* bb, UINT index, bool right = false) {
    using namespace reshade::api;
    if (!g_rt || index >= 8) return false;
    device* dev = g_rt->get_device();
    const resource res{(uint64_t)bb};
    if (g_rtvRes[index] != res.handle) {
        if (g_rtv[index].handle) dev->destroy_resource_view(g_rtv[index]);
        g_rtv[index] = {0};
        const D3D12_RESOURCE_DESC d = bb->GetDesc();
        if (!dev->create_resource_view(res, resource_usage::render_target, resource_view_desc((format)d.Format), &g_rtv[index])) return false;
        g_rtvRes[index] = res.handle;
    }
    command_queue* q = g_rt->get_command_queue();
    command_list* cl = q->get_immediate_command_list();
    t_ours = true;   // ReShade's and FlatVR's own signals in here are not the game's: they pass
    cl->barrier(res, resource_usage::present, resource_usage::render_target);
    if (right) g_rt->render_technique(g_rightTech, cl, g_rtv[index], g_rtv[index]);
    else g_rt->render_effects(cl, g_rtv[index], g_rtv[index]);
    cl->barrier(res, resource_usage::render_target, resource_usage::present);
    q->flush_immediate_command_list();
    t_ours = false;
    return true;
}

// The left pass's Present: its picture copied out, the Present held back (told the game it went).
bool HoldLeft() {
    if (!g_native) return false;
    ID3D12CommandQueue* direct = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (const Sub& s : g_subs)
        if (s.q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) { direct = s.q; break; }
    ReleaseSRWLockExclusive(&g_lock);
    if (!direct) return false;
    g_direct = direct;
    if (!Setup(direct)) return false;
    const UINT holdIndex = g_native->GetCurrentBackBufferIndex();
    g_pairIdx[g_pairIdxAt & 15] = {holdIndex, g_gameIdxNow, ~0u, ~0u, 0u, S_OK};
    ID3D12Resource* bb = BackBuffer(holdIndex);
    if (!bb) return false;
    if (g_fxAtHold.load() && g_rt) {   // one present a pair: the left eye goes out from this effect pass
        if (g_fxSync.load() && g_lastReplay.f) { StepTimer st(kWaitLeft); WaitFor(g_lastReplay); }
        const bool ok = EffectsAtHold(bb, holdIndex);
        if (ok && g_peekStage == 1) Peek(0, bb);
        if (ok) XrCopy(0, bb);   // native OpenXR: the left eye, as FlatVR's add-on takes it from that effect pass
        bb->Release();
        return ok;
    }
    const D3D12_RESOURCE_DESC d = bb->GetDesc();
    if (g_leftImg) {
        const D3D12_RESOURCE_DESC h = g_leftImg->GetDesc();
        if (h.Width != d.Width || h.Height != d.Height || h.Format != d.Format) { g_leftImg->Release(); g_leftImg = nullptr; }
    }
    if (!g_leftImg) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = d;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&g_leftImg))) { bb->Release(); return false; }
        LogF("vrcam: replay - the left picture's texture made, %llux%u", (unsigned long long)d.Width, d.Height);
    }
    Check("before holding the left picture");
    Copy(g_leftImg, D3D12_RESOURCE_STATE_COMMON, bb, D3D12_RESOURCE_STATE_PRESENT);
    Check("copying the left picture out");
    XrCopy(0, bb);
    if (ID3D12Resource* depth = g_depthCopy.load() ? (ID3D12Resource*)g_depthRes.load() : nullptr) {
        const D3D12_RESOURCE_DESC dd = depth->GetDesc();
        if (g_leftDepth) {
            const D3D12_RESOURCE_DESC h = g_leftDepth->GetDesc();
            if (h.Width != dd.Width || h.Height != dd.Height || h.Format != dd.Format) { g_leftDepth->Release(); g_leftDepth = nullptr; }
        }
        if (!g_leftDepth) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            if (SUCCEEDED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                         __uuidof(ID3D12Resource), (void**)&g_leftDepth)))
                LogF("vrcam: replay - the left depth's copy made, %llux%u format %d, state %X",
                     (unsigned long long)dd.Width, dd.Height, (int)dd.Format, g_depthState.load());
        }
        if (g_leftDepth) Copy(g_leftDepth, D3D12_RESOURCE_STATE_COMMON, depth, DepthState());
    }
    bb->Release();
    return true;
}

// The game's present (D2R 0x109F4C0: rcx its device, rdx a result, r8 its swap chain
// wrapper - [0] the IDXGISwapChain it presents through, +0x50 the sync interval).
constexpr uint64_t RVA_GAME_PRESENT = 0x109F4C0;
const uint8_t kSigGamePresent[16] = {0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x4D};
using GamePresentFn = void* (*)(void*, void*, void*);
GamePresentFn OrigGamePresent = nullptr;
void* HookGamePresent(void* dev, void* result, void* wrap) {
    if (wrap) { __try { g_gameIdxNow = *(const uint32_t*)((const uint8_t*)wrap + 0x98); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    if (wrap) {
        __try {   // and the flags it presents with: tearing allowed (0x200) as the game decides it
            const uint8_t* w = (const uint8_t*)wrap;
            g_proxy = *(IDXGISwapChain**)wrap;
            g_presentFlags = *((const uint8_t*)dev + 0x80) && *(const uint32_t*)(w + 0x50) == 0 && !w[0x38] && w[0x39] ? 0x200u : 0u;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    double holdT0 = pairtime::UsNow();
    if (g_capturing.load() && !g_leftHeld && HoldLeft()) {
        g_stepUs[kHold] += pairtime::UsNow() - holdT0;
        g_leftHeld = true;
        g_wrap = (uint8_t*)wrap;
        *(uint16_t*)result = 0;                     // the game's own success: code 0, HRESULT 0
        *(uint32_t*)((uint8_t*)result + 4) = 0;
        return result;
    }
    return OrigGamePresent(dev, result, wrap);
}

// A watchdog for the prototype: no present for 3 s while replaying - the fences' state
// (signalled vs done) and where the draw thread waits, to the log, once.
void LogStack(DWORD tid) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) return;
    static uint8_t copy[32768];
    CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
    size_t copied = 0;
    if (SuspendThread(th) != (DWORD)-1) {
        if (GetThreadContext(th, &c) && c.Rsp) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                copied = (size_t)std::min<DWORD64>(sizeof copy, (DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
                memcpy(copy, (void*)c.Rsp, copied);
            }
        }
        ResumeThread(th);
    }
    CloseHandle(th);
    if (!copied) return;
    DWORD64 pcs[24];
    const int n = drawprof::UnwindCopy(c, copy, copied, pcs);
    for (int i = 0; i < n; ++i) {
        DWORD64 base = 0;
        const std::string m = drawprof::ModuleOf(pcs[i], &base);
        LogF("vrcam: watchdog -   %s+0x%llX", m.c_str(), (unsigned long long)(base ? pcs[i] - base : pcs[i]));
    }
}
// Every thread's stack to d2rloader\logs\d2r_vr_hang.txt, written straight to the file
// (the log itself may be what is stuck) with the modules looked up from a list taken
// beforehand (GetModuleHandleEx takes the loader lock a stuck thread may hold).
struct Mod { uint64_t base, end; char name[64]; };
std::vector<Mod> g_mods;
void ListModules() {   // psapi reads the module list in place: no thread made, no loader lock
    g_mods.clear();
    HMODULE mods[1024];
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i) {
        MODULEINFO mi{};
        if (!K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
        Mod m{(uint64_t)mi.lpBaseOfDll, (uint64_t)mi.lpBaseOfDll + mi.SizeOfImage, {}};
        K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], m.name, sizeof m.name);
        g_mods.push_back(m);
    }
}
void DumpAllStacks() {
    ListModules();
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *slash = 0;
    wcscat_s(path, L"\\..\\logs\\d2r_vr_hang.txt");
    FILE* f = _wfopen(path, L"w");
    if (!f) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof te};
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    static uint8_t copy[65536];
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        size_t copied = 0;
        if (SuspendThread(th) != (DWORD)-1) {
            if (GetThreadContext(th, &c) && c.Rsp) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                    copied = (size_t)std::min<DWORD64>(sizeof copy, (DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
                    memcpy(copy, (void*)c.Rsp, copied);
                }
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        fprintf(f, "thread %lu%s\n", te.th32ThreadID, te.th32ThreadID == pairtime::g_drawThread.load() ? " (draw)" : "");
        if (!copied) continue;
        DWORD64 pcs[24];
        const int n = drawprof::UnwindCopy(c, copy, copied, pcs);
        for (int i = 0; i < n; ++i) {
            const Mod* m = nullptr;
            for (const Mod& x : g_mods) if (pcs[i] >= x.base && pcs[i] < x.end) { m = &x; break; }
            fprintf(f, "    %s+0x%llX\n", m ? m->name : "?", (unsigned long long)(m ? pcs[i] - m->base : pcs[i]));
        }
    }
    CloseHandle(snap);
    fclose(f);
}
DWORD WINAPI Watchdog(void*) {
    bool told = false;
    Log("vrcam: replay - watchdog running (no present for 3 s while replaying: every thread's stack to d2r_vr_hang.txt)");
    // By the presents' count: g_presentAt, cleared at every pair's start, stayed 0 when the
    // game hung inside a pair with its left present held - no dump (2026-10-09, an area change).
    uint32_t seen = 0;
    double since = 0.0;
    for (;;) {
        Sleep(500);
        const uint32_t now = pairtime::g_presents.load();
        if (now != seen || !g_replays.load()) { seen = now; since = pairtime::UsNow(); told = false; continue; }
        if (pairtime::UsNow() - since < 3e6) continue;
        if (told) continue;
        told = true;
        DumpAllStacks();   // first, to its own file: the log may be the stuck thing
        Log("vrcam: watchdog - no present for 3 s while replaying:");
        for (const QFence& x : g_qf)
            LogF("vrcam: watchdog -   queue %p (type %d): our fence signalled %llu, done %llu", (void*)x.q, (int)x.q->GetDesc().Type,
                 (unsigned long long)x.v, (unsigned long long)x.f->GetCompletedValue());
        if (g_native) {
            UINT last = 0; g_native->GetLastPresentCount(&last);
            DXGI_FRAME_STATISTICS fs{};
            const HRESULT hs = g_native->GetFrameStatistics(&fs);
            LogF("vrcam: watchdog -   DXGI: presents issued %u, frame statistics hr %08lX: present count %u, present refresh %u, sync refresh %u; back buffer now %u, the game's %u",
                 last, (unsigned long)hs, fs.PresentCount, fs.PresentRefreshCount, fs.SyncRefreshCount, g_native->GetCurrentBackBufferIndex(), g_gameIdxNow);
            for (uint32_t i = g_pairIdxAt - std::min<uint32_t>(g_pairIdxAt + 1, 16); i != g_pairIdxAt + 1; ++i) {
                const PairIdx& pi = g_pairIdx[i & 15];
                LogF("vrcam: watchdog -   pair: left drew in %u (game's %u), right %u, after %u, hr %08lX, presents %u",
                     pi.hold, pi.game, pi.right, pi.after, (unsigned long)pi.hr, pi.presents);
            }
        }
        LogF("vrcam: watchdog -   last replay point %llu, two back %llu; slots %d", (unsigned long long)g_lastReplay.v,
             (unsigned long long)g_prevReplay.v, camfix::g_nSlots.load());
        for (int i = 256 - 96; i < 256; ++i) {
            const Hist& h = g_hist[(g_histAt + i) & 255];
            if (!h.q) continue;
            LogF("vrcam: watchdog -   %c q %p f %p %llu (%s)", h.op, (void*)h.q, (void*)h.f, (unsigned long long)h.v, h.why);
        }
        {   // the game's own, oldest first; a wait shows its fence's completed value now
            const uint32_t end = g_gameOpAt.load();
            const uint32_t n = std::min<uint32_t>(end, 160);
            Log("vrcam: watchdog -   the game's queue ops (s signal, w wait [done now], x submit), last 160:");
            for (uint32_t i = end - n; i != end; ++i) {
                const GameOp& o = g_gameOps[i & 1023];
                if (!o.q) continue;
                if (o.op == 'x') LogF("vrcam: watchdog -   x q %p (type %d) thread %lu", (void*)o.q, (int)o.q->GetDesc().Type, o.tid);
                else LogF("vrcam: watchdog -   %c q %p (type %d) f %p %llu [done %llu] thread %lu", o.op, (void*)o.q, (int)o.q->GetDesc().Type,
                          (void*)o.f, (unsigned long long)o.v, (unsigned long long)SafeDone(o.f), o.tid);
            }
        }
        LogF("vrcam: watchdog -   the draw thread %lu waits in:", pairtime::g_drawThread.load());
        LogStack(pairtime::g_drawThread.load());
    }
}

// On unless [debug] replay_proto=0: the hooks cost next to nothing while [stereo] replay_right is
// off, and the Settings' "Stereo" mode switches it on live.
void Register() {
    if (!IniB(L"debug", L"replay_proto", true)) return;
    if (HANDLE h = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(h);
    reshade::register_event<reshade::addon_event::execute_command_list>(&d2rvr::D3D12Only<&OnExecute>::Call);
    reshade::register_event<reshade::addon_event::present>(&d2rvr::D3D12Only<&OnPresent>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&OnBarrier>::Call);
    camfix::Register();
    g_on.store(true);
    Log("vrcam: replay prototype on - [stereo] replay_right=1 draws the right eye from the left eye's lists");
}

// The game's present is hooked when first wanted, in the world: at ReShade's start the
// game's code may not be decrypted yet.
// Only in a loaded area and after 2 s of it: a menu or a loading screen has nothing to gain,
// and the game frees and makes resources then (it hung at an area change, 2026-10-09).
int g_worldPairs = 0;
bool Wanted() {
    if (!g_on.load() || !g_want.load()) return false;
    if (!g_inWorld.load() || !d2rcam::InWorld()) {
        if (g_worldPairs >= 240) Log("vrcam: replay - paused (no world: a menu or an area change)");
        g_worldPairs = 0;
        return false;
    }
    if (g_worldPairs < 240) {
        if (++g_worldPairs < 240) return false;
        Log("vrcam: replay - on (the area settled)");
    }
    static int hooked = -1;
    if (hooked < 0) {
        hooked = d2rsig::Hook(RVA_GAME_PRESENT, kSigGamePresent, sizeof kSigGamePresent, (void*)&HookGamePresent, (void**)&OrigGamePresent) ? 1 : 0;
        LogF("vrcam: replay - the game's present %s", hooked ? "hooked" : "NOT where expected - no replay");
    }
    return hooked == 1 && g_proxy && g_native;   // both known after a present through the hook
}

// Before a pair's left pass. The previous replay must be done on the GPU first: the game
// resets the left eye's command allocators once its own frame fence passes, which comes
// before our replay of them.
void BeginLeft() {
    // the replay two pairs back: the game's allocators of that left pass come round again
    // (3 back buffers, 2 presents a pair); the last one may still run while this pass records
    {
        StepTimer st(kWaitPrev);
        if (g_fence) WaitFor(g_ahead.load() >= 2 ? g_prevReplay : g_lastReplay);
        // The skipped right pass's wait on the swap chain's frame latency object: the game waits
        // once a pass, we present twice a pair - with one wait a pair the count drifted, DXGI
        // handed out the back buffer still on screen and the GPU waited for a newer present
        // queued behind it (the game hung at replay_ahead 2). Here, where the pass would wait.
        if (g_ahead.load() == 2 && g_latency && !(g_fxAtHold.load() && g_rt)) WaitForSingleObject(g_latency, 100);
    }
    if (g_peekStage == 2) { PeekReport(); g_peekStage = 0; }
    if (g_peekStage == 1) g_peekStage = 0;   // armed but the pair went out otherwise: again
    if (g_peekWant.exchange(0)) g_peekStage = 1;
    g_prevReplay = g_lastReplay;
    g_restorePending.store(true);
    AcquireSRWLockExclusive(&g_lock);
    g_subs.clear();
    ReleaseSRWLockExclusive(&g_lock);
    g_leftHeld = false;
    g_captureThread.store(GetCurrentThreadId());
    g_pairStartUs.store(pairtime::UsNow());
    g_capturing.store(1);
    if (g_sigHooked.load() && g_holdSignalsOn.load()) g_holdSignals.store(true);
    if (g_want.load() == 1 || g_want.load() == 4) camfix::BeginLeft(g_want.load() == 1);
}

// Our presents' results: a present that is not S_OK, or that leaves the back buffer index
// where it was (DXGI_STATUS_OCCLUDED: the window covered - it is dropped), told once a second.
void NotePresent(HRESULT hr, UINT before, UINT after) {
    static uint32_t n = 0, odd = 0, stuck = 0;
    static HRESULT lastOdd = S_OK;
    static ULONGLONG since = GetTickCount64();
    ++n;
    if (hr != S_OK) { ++odd; lastOdd = hr; }
    if (before == after) ++stuck;
    if (GetTickCount64() - since >= 1000) {
        if (odd || stuck) LogF("vrcam: replay - presents: %u, not S_OK %u (last %08lX), back buffer index unchanged %u", n, odd, (unsigned long)lastOdd, stuck);
        n = odd = stuck = 0; since = GetTickCount64();
    }
}

void SetGameBackBuffer(uint8_t* wrap, UINT index) {
    __try { *(uint32_t*)(wrap + 0x98) = index; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// After the left pass, instead of the right one. False: the left eye went out as usual
// (its Present was not held), the caller draws the right eye the usual way.
bool RightFromLeft() {
    g_capturing.store(0);
    g_holdSignals.store(false);
    std::vector<Sub> subs;
    AcquireSRWLockExclusive(&g_lock);
    subs.swap(g_subs);
    ReleaseSRWLockExclusive(&g_lock);
    if (!g_leftHeld) { ReleaseHeld(nullptr); g_fails.fetch_add(1); return false; }
    g_leftHeld = false;
    const int mode = g_want.load();
    camfix::g_capture.store(false);
    if (camfix::g_scanWant.exchange(0)) camfix::ScanCopies();
    if (mode == 1) {   // the right eye's camera into the slots, on the direct queue before the replay
        StepTimer st(kPatch);
        CopySlots(1);
    }
    if (mode == 4) {   // the left pass done on the GPU (every queue it used), then its camera rewritten
        std::vector<ID3D12CommandQueue*> qs;
        for (const Sub& s : subs) if (std::find(qs.begin(), qs.end(), s.q) == qs.end()) qs.push_back(s.q);
        std::vector<Pt> pts;
        for (ID3D12CommandQueue* q : qs) pts.push_back(SignalOn(q));
        {
            StepTimer st(kWaitLeft);
            for (const Pt& pt : pts) WaitFor(pt);
        }
        StepTimer st(kPatch);
        camfix::RightEye();
    }
    double t = pairtime::UsNow();
    if (mode != 3) {   // the lists again, a batch per run of one queue; a fence between queues
        // (the first batch may be the compute queue's: it must see the right eye's slots too)
        if (mode == 1 && !subs.empty() && subs[0].q != g_direct) { g_why = "replay: first batch not direct"; GpuWait(subs[0].q, SignalOn(g_direct)); }
        ID3D12CommandQueue* prev = nullptr;
        size_t i = 0;
        while (i < subs.size()) {
            ID3D12CommandQueue* q = subs[i].q;
            std::vector<ID3D12CommandList*> batch;
            while (i < subs.size() && subs[i].q == q) batch.push_back(subs[i++].cl);
            if (prev && prev != q) { g_why = "replay: queue switch"; GpuWait(q, SignalOn(prev)); }
            if (g_marks) {
                for (size_t j = 0; j < batch.size(); ++j) {   // k: the list's place in the pass
                    const size_t k = i - batch.size() + j;
                    q->ExecuteCommandLists(1, &batch[j]);
                    g_why = kListMark[std::min<size_t>(k, 31)];
                    SignalOn(q);
                }
            } else {
                q->ExecuteCommandLists((UINT)batch.size(), batch.data());
            }
            prev = q;
        }
        if (prev && prev != g_direct) { g_why = "replay: back to direct"; GpuWait(g_direct, SignalOn(prev)); }
    }
    // One wait on the swap chain's frame latency object a present, as the game does before its
    // frame: it waits once a game frame, we present twice - unthrottled, the GPU ran into a back
    // buffer still queued behind presents waiting after it (the game hung, replay_ahead 2).
    if (!g_latencyAsked) {
        g_latencyAsked = true;
        DXGI_SWAP_CHAIN_DESC1 d{};
        IDXGISwapChain2* s2 = nullptr;
        if (SUCCEEDED(g_native->GetDesc1(&d)) && (d.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) &&
            SUCCEEDED(g_native->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&s2))) {
            g_latency = s2->GetFrameLatencyWaitableObject();
            // Each wait let through only when every earlier present was done (latency 1): the CPU
            // waited for the GPU's replay. [stereo] replay_latency frames may queue instead.
            UINT was = 0;
            s2->GetMaximumFrameLatency(&was);
            const UINT want = (UINT)std::clamp((int)IniF(L"stereo", L"replay_latency", 3.0f), 1, 8);
            const bool change = g_ahead.load() >= 2 && !(g_fxAtHold.load() && g_rt) && want != was;
            if (change) s2->SetMaximumFrameLatency(want);
            LogF("vrcam: replay - frame latency %u -> %u", was, change ? want : was);
            s2->Release();
        }
        LogF("vrcam: replay - the swap chain %s a frame latency waitable object (flags %X, %u buffers)",
             g_latency ? "has" : "has NO", d.Flags, d.BufferCount);
    }
    // (only with [stereo] replay_ahead=2: that wait keeps presents to the monitor's refresh -
    // 65 pairs/s - so by default the CPU runs only one replay ahead and needs no throttle)
    // [stereo] replay_ahead 2: the presents kept bounded by the GPU instead - the last pair
    // done before this one's go out (the replay of this one runs on while the CPU records
    // the next left pass). 3: the latency object as above (follows the monitor).
    if (g_latency && g_ahead.load() >= 3) { StepTimer st(kWaitPrev); WaitForSingleObject(g_latency, 100); }
    {   // the game's end-of-frame signals, now behind the replay on their queues
        AcquireSRWLockShared(&g_heldLock);
        const uint32_t n = (uint32_t)g_held.size();
        ReleaseSRWLockShared(&g_heldLock);
        if (n > g_heldMax.load()) { g_heldMax.store(n); LogF("vrcam: replay - %u end-of-frame signal(s) of the game held for the replay", n); }
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 10000) {
            told = GetTickCount64();
            LogF("vrcam: replay - queue signals: %u in all, %u while holding, %u held; at this replay %u still held",
                 g_sigCalls.load(), g_sigWhileHolding.load(), g_sigHeld.load(), n);
        }
        ReleaseHeld(nullptr);
    }
    // the right eye: the back buffer as the lists left it (mode 3: the left picture again)
    const UINT bbRight = g_native->GetCurrentBackBufferIndex();
    Check("the replay");
    SetEye(1);
    // ReShade renders no effects at a present once an add-on has rendered them in that frame:
    // with the left eye's at the hold, the right eye's went without (no sky, and FlatVR's
    // add-on, which takes the picture from the effect pass, had only left eyes - its stream
    // stopped). The right eye's are rendered here too, on the replayed picture and depth.
    if (g_marks) { g_why = "before the right eye's effects"; SignalOn(g_direct); }
    if (g_fxAtHold.load() && g_rt && g_rightTech.handle)
        if (ID3D12Resource* bb = BackBuffer(bbRight)) {
            EffectsAtHold(bb, bbRight, true);
            if (g_peekStage == 1) { Peek(1, bb); g_peekStage = 2; }
            bb->Release();
        }
    if (xr::On())   // native OpenXR: the right eye
        if (ID3D12Resource* bb = BackBuffer(bbRight)) { XrCopy(1, bb); bb->Release(); }
    if (g_marks) { g_why = "after the right eye's effects, before its present"; SignalOn(g_direct); }
    double t1 = pairtime::UsNow(); g_stepUs[kSubmit] += t1 - t; t = t1;
    const HRESULT hrR = g_proxy->Present(0, g_presentFlags);
    Check("the right eye's present", hrR);
    NotePresent(hrR, bbRight, g_native->GetCurrentBackBufferIndex());
    {
        PairIdx& pi = g_pairIdx[g_pairIdxAt++ & 15];
        pi.right = bbRight; pi.after = g_native->GetCurrentBackBufferIndex(); pi.hr = hrR;
        UINT c = 0; g_native->GetLastPresentCount(&c); pi.presents = c;
    }
    t1 = pairtime::UsNow(); g_stepUs[kPresentR] += t1 - t; t = t1;
    // the left eye: its copy into the next back buffer (not with its effects run at the hold:
    // it went out from that effect pass)
    const UINT bbLeft = g_native->GetCurrentBackBufferIndex();
    if (g_fxAtHold.load() && g_rt) {
        g_why = "end of pair";
        g_lastReplay = SignalOn(g_direct);
        if (g_wrap) SetGameBackBuffer(g_wrap, g_native->GetCurrentBackBufferIndex());
        if (g_replays.fetch_add(1) % 900 == 0)
        {
            LogF("vrcam: replay - a pair from one pass, one present: %zu lists again (mode %d), back buffer %u; %d camera slots (%u overflowed)",
                 subs.size(), mode, bbRight, camfix::g_nSlots.load(), camfix::g_overflow.load());
            const uint32_t pairs = std::max<uint32_t>(1, g_replays.load());
            LogF("vrcam: replay - slots made after the left values went in: %.2f a pair (copied late)", g_lateSlots.load() / (double)pairs);
            LogF("vrcam: replay - the game's lists a pair: %.1f in the left pass (replayed), %.1f outside it on the draw thread, %.1f from other threads (neither replayed)",
                 g_insideLists.load() / (double)pairs, g_outsideDraw.load() / (double)pairs, g_outsideOther.load() / (double)pairs);
            for (int i = 0; i < 8; ++i)
                if (g_outside[i].tid) LogF("vrcam: replay -   outside: thread %lu (draw thread %lu), queue type %d, %.2f ms after the left pass began",
                                           g_outside[i].tid, g_captureThread.load(), g_outside[i].type, g_outside[i].atUs / 1000.0);
            LogF("vrcam: replay - the right eye's camera, a pair: %.1f folded into the projection, %.1f views moved, %.1f positions; shift %.4f (eye half %.4f x2 x%.2f), off-axis %.4f, x scale %.4f",
                 g_nFold.load() / (double)pairs, g_nView.load() / (double)pairs, g_nPos.load() / (double)pairs,
                 2.0 * g_eyeHalf.load() * g_shiftMul.load(), g_eyeHalf.load(), g_shiftMul.load(), g_projShift.load(), g_projSx.load());
        }
        ++g_stepPairs;
        PairLine(true);
        return true;
    }
    if (ID3D12Resource* bb = BackBuffer(bbLeft)) {
        Copy(bb, D3D12_RESOURCE_STATE_PRESENT, g_leftImg, D3D12_RESOURCE_STATE_COMMON);
        bb->Release();
    }
    Check("copying the left picture in");
    if (ID3D12Resource* depth = g_depthCopy.load() ? (ID3D12Resource*)g_depthRes.load() : nullptr; depth && g_leftDepth)
        Copy(depth, DepthState(), g_leftDepth, D3D12_RESOURCE_STATE_COMMON);
    g_why = "end of pair";
    g_lastReplay = SignalOn(g_direct);
    SetEye(0);
    t1 = pairtime::UsNow(); g_stepUs[kCopyL] += t1 - t; t = t1;
    const HRESULT hrL = g_proxy->Present(0, g_presentFlags);
    Check("the left eye's present", hrL);
    NotePresent(hrL, bbLeft, g_native->GetCurrentBackBufferIndex());
    g_stepUs[kPresentL] += pairtime::UsNow() - t;
    ++g_stepPairs;
    PairLine(true);
    // the game asked for the back buffer index right after its (held) present, before our two
    // went out: it would draw the next frame into one already presented (device removed, 0x887A002B)
    if (g_wrap) SetGameBackBuffer(g_wrap, g_native->GetCurrentBackBufferIndex());
    if (g_replays.fetch_add(1) % 900 == 0)
        LogF("vrcam: replay - a pair from one pass: %zu lists again (mode %d), right in back buffer %u, left in %u; %d camera slots (%u overflowed)",
             subs.size(), mode, bbRight, bbLeft, camfix::g_nSlots.load(), camfix::g_overflow.load());
    return true;
}
}  // namespace replay

// Crash reports (2026-10-10, 0.157): a player's game died at its start with nothing in the log to
// say where. Nothing of this costs a frame anything:
// - d2rloader\logs\d2r_vr_start.txt: the log's first two minutes, line by line straight to the
//   file (WriteFile - no buffer left to lose when the game dies), each start step before and
//   after: its last line is the step that died. The run before is kept as d2r_vr_start_prev.txt.
// - d2r_vr_crash.txt: an exception the game dies of - the top-level filter, put first again every
//   2 s (the game's own crash handler is called after it) - with the module and offset it hit in,
//   the code, the registers, the stack and the module list, and a minidump beside it
//   (d2r_vr_crash.dmp). The few exceptions nothing catches (stack overflow, heap corruption, an
//   illegal instruction) are written from a vectored handler at once; an access violation is only
//   noted there (many are caught on purpose - SafeRead) and named in a report that follows.
// Collect logs (D2R VR Settings > Home) takes them all.
namespace crash {
wchar_t g_dir[MAX_PATH] = L"";   // d2rloader\logs\ with its slash, "" until Install
const char* g_version = "?";
HANDLE g_start = INVALID_HANDLE_VALUE;
std::atomic<ULONGLONG> g_mirrorUntil{0};
char g_step[96] = "loading the plugin";   // the last start step begun, or the last milestone
ULONGLONG g_stepAt = 0, g_installedAt = 0;
std::atomic<int> g_busy{0}, g_firstChance{0}, g_dumps{0};
std::atomic<DWORD> g_filterThread{0};
LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;
void* g_veh = nullptr;
using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpFn g_miniDump = nullptr;
struct Seen { DWORD tid; DWORD64 pc, target; ULONG_PTR rw; ULONGLONG at; };
Seen g_lastAv{};   // the last access violation, first chance (it may well have been caught)

// One line to d2r_vr_start.txt, with the time.
void Line(const char* text) {
    if (g_start == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[640];
    int n = snprintf(b, sizeof b, "[%02d:%02d:%02d.%03d] %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
    if (n <= 0) return;
    if (n >= (int)sizeof b) { n = (int)sizeof b - 1; b[n - 2] = '\r'; b[n - 1] = '\n'; }
    DWORD w = 0;
    WriteFile(g_start, b, (DWORD)n, &w, nullptr);
}
void Mirror(const char* text) {
    if (GetTickCount64() < g_mirrorUntil.load(std::memory_order_relaxed)) Line(text);
}

// The start's steps: the one before done (with its time), this one begun. nullptr: the start is over.
void Step(const char* name) {
    const ULONGLONG now = GetTickCount64();
    char b[200];
    if (g_stepAt) { snprintf(b, sizeof b, "vrcam start: %s - done (%llu ms)", g_step, now - g_stepAt); Log(b); }
    if (name) {
        strncpy_s(g_step, name, _TRUNCATE);
        g_stepAt = now;
        snprintf(b, sizeof b, "vrcam start: %s ...", name);
        Log(b);
    } else {
        strncpy_s(g_step, "the plugin loaded, the game going on", _TRUNCATE);
        g_stepAt = 0;
    }
}
// A milestone after the start (the first present, ReShade joined...): the report names the last one.
void Mark(const char* what) {
    strncpy_s(g_step, what, _TRUNCATE);
    char b[160];
    snprintf(b, sizeof b, "vrcam milestone: %s", what);
    Line(b);
}

// The report's own memory: the heap may be what broke, and a stack overflow leaves little stack.
char g_buf[32768];
size_t g_len = 0;
void Add(const char* fmt, ...) {
    if (g_len >= sizeof g_buf - 1) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_buf + g_len, sizeof g_buf - g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len = std::min(g_len + (size_t)n, sizeof g_buf - 1);
}
struct ModInfo { uint64_t base, end; char name[56]; };
HMODULE g_mods[1024];
ModInfo g_modInfo[1024];
int g_modCount = 0;
void ListMods() {   // psapi reads the module list in place: no loader lock (DumpAllStacks)
    g_modCount = 0;
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), g_mods, sizeof g_mods, &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i) {
        MODULEINFO mi{};
        if (!K32GetModuleInformation(GetCurrentProcess(), g_mods[i], &mi, sizeof mi)) continue;
        ModInfo& m = g_modInfo[g_modCount++];
        m.base = (uint64_t)mi.lpBaseOfDll;
        m.end = m.base + mi.SizeOfImage;
        if (!K32GetModuleBaseNameA(GetCurrentProcess(), g_mods[i], m.name, sizeof m.name)) strcpy_s(m.name, "?");
    }
}
void AddWhere(DWORD64 pc) {
    for (int i = 0; i < g_modCount; ++i)
        if (pc >= g_modInfo[i].base && pc < g_modInfo[i].end) { Add("%s+0x%llX", g_modInfo[i].name, pc - g_modInfo[i].base); return; }
    Add("0x%llX (in no module)", pc);
}
const char* CodeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case 0xC0000374: return "heap corruption";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error (a file the code was read from went away)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case 0xE06D7363: return "C++ exception not caught";
    case 0xC0000409: return "stack buffer overrun / fail fast";
    case 0x80000003: return "breakpoint";
    default: return "";
    }
}
void Write(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_dir);
    wcscat_s(path, name);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, g_buf, (DWORD)g_len, &w, nullptr);
    CloseHandle(f);
}

// To d2r_vr_crash.txt: what, where, the registers, the stack, the last access violation before it;
// `fatal` (the one the game dies of) the whole module list too.
void Report(EXCEPTION_POINTERS* ep, const char* kind, bool walk, bool fatal) {
    if (!g_dir[0] || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return;
    if (!fatal) {   // first chance: the first few, never two at once
        if (g_firstChance.load() >= 4 || g_busy.exchange(1)) return;
        ++g_firstChance;
    } else {        // the one the game dies of waits for one being written (a writer stuck that long: anyway)
        for (int i = 0; i < 200 && g_busy.exchange(1); ++i) Sleep(1);
    }
    const EXCEPTION_RECORD& r = *ep->ExceptionRecord;
    const CONTEXT& c = *ep->ContextRecord;
    ListMods();
    g_len = 0;
    SYSTEMTIME t;
    GetLocalTime(&t);
    Add("==== %04d-%02d-%02d %02d:%02d:%02d - D2R VR vrcam %s - %s ====\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, g_version, kind);
    Add("exception 0x%08lX %s at ", r.ExceptionCode, CodeName(r.ExceptionCode));
    AddWhere((DWORD64)r.ExceptionAddress);
    if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2)
        Add(" - %s 0x%llX", r.ExceptionInformation[0] == 1 ? "writing" : r.ExceptionInformation[0] == 8 ? "running code at" : "reading",
            (unsigned long long)r.ExceptionInformation[1]);
    const DWORD tid = GetCurrentThreadId();
    Add("\r\nthread %lu%s; %.1f s after the plugin loaded; the start's last step: %s\r\n", tid,
        tid == pairtime::g_drawThread.load() ? " (the game's draw thread)" : "", (GetTickCount64() - g_installedAt) / 1000.0, g_step);
    Add("rip %016llX rsp %016llX rbp %016llX\r\nrax %016llX rbx %016llX rcx %016llX rdx %016llX\r\nrsi %016llX rdi %016llX r8  %016llX r9  %016llX\r\n"
        "r10 %016llX r11 %016llX r12 %016llX r13 %016llX\r\nr14 %016llX r15 %016llX\r\n",
        c.Rip, c.Rsp, c.Rbp, c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15);
    if (walk && c.Rsp) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((void*)c.Rsp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
            DWORD64 pcs[drawprof::kDepth];
            const size_t have = (size_t)((DWORD64)mbi.BaseAddress + mbi.RegionSize - c.Rsp);
            const int n = drawprof::UnwindCopy(c, (uint8_t*)c.Rsp, have, pcs);   // the stack itself: the copy at no offset
            Add("stack:\r\n");
            for (int i = 0; i < n; ++i) { Add("    "); AddWhere(pcs[i]); Add("\r\n"); }
        }
    }
    const Seen av = g_lastAv;
    if (av.at && r.ExceptionCode != EXCEPTION_ACCESS_VIOLATION && GetTickCount64() - av.at < 10000) {
        Add("an access violation %.1f s before (thread %lu, maybe caught): at ", (GetTickCount64() - av.at) / 1000.0, av.tid);
        AddWhere(av.pc);
        Add(" %s 0x%llX\r\n", av.rw == 1 ? "writing" : av.rw == 8 ? "running code at" : "reading", (unsigned long long)av.target);
    }
    if (fatal) {
        Add("modules:\r\n");
        for (int i = 0; i < g_modCount; ++i)
            Add("    %016llX %8llX %s\r\n", g_modInfo[i].base, g_modInfo[i].end - g_modInfo[i].base, g_modInfo[i].name);
    }
    Add("\r\n");
    Write(L"d2r_vr_crash.txt");
    Line(kind);
    Line("vrcam: the crash went to d2rloader\\logs\\d2r_vr_crash.txt");
    g_busy.store(0);
}

// A minidump beside the report, once a run: the threads, their stacks, the modules.
void Dump(EXCEPTION_POINTERS* ep) {
    if (!g_miniDump || !g_dir[0] || g_dumps.exchange(1)) return;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_dir);
    wcscat_s(path, L"d2r_vr_crash.dmp");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    const BOOL ok = g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                               (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                               &mei, nullptr, nullptr);
    CloseHandle(f);
    Line(ok ? "vrcam: minidump d2rloader\\logs\\d2r_vr_crash.dmp written" : "vrcam: the minidump could not be written");
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep) {
    const DWORD me = GetCurrentThreadId();
    if (g_filterThread.load() == me) return EXCEPTION_CONTINUE_SEARCH;   // a filter chained back to ours
    DWORD none = 0;
    const bool first = g_filterThread.compare_exchange_strong(none, me);
    if (first) {
        Report(ep, "UNHANDLED - the game closes on this", true, true);
        Dump(ep);
    }
    LONG r = EXCEPTION_CONTINUE_SEARCH;
    if (g_prev && g_prev != &Unhandled) r = g_prev(ep);   // the game's own crash handler, as before
    if (first) g_filterThread.store(0);
    return r;
}

LONG WINAPI Vectored(EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep ? ep->ExceptionRecord : nullptr;
    if (!r) return EXCEPTION_CONTINUE_SEARCH;
    switch (r->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:   // noted only: caught ones are many (SafeRead)
        g_lastAv = {GetCurrentThreadId(), (DWORD64)r->ExceptionAddress, r->NumberParameters >= 2 ? (DWORD64)r->ExceptionInformation[1] : 0,
                    r->NumberParameters >= 1 ? r->ExceptionInformation[0] : 0, GetTickCount64()};
        break;
    case EXCEPTION_STACK_OVERFLOW:     // little stack left: no walk
        Report(ep, "stack overflow (first chance)", false, false);
        break;
    case 0xC0000374:                   // heap corruption: the process ends without the filter
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
        Report(ep, "first chance - an exception seldom caught", true, false);
        break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// First thing at the plugin's load: the files, the filter and the vectored handler.
void Install(const char* version) {
    g_version = version;
    g_installedAt = GetTickCount64();
    wcscpy_s(g_dir, g_iniPath);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\')) slash[1] = 0;
    wcscat_s(g_dir, L"..\\logs\\");
    CreateDirectoryW(g_dir, nullptr);
    wchar_t a[MAX_PATH], b[MAX_PATH];
    auto at = [&](wchar_t* out, const wchar_t* name) { wcscpy_s(out, MAX_PATH, g_dir); wcscat_s(out, MAX_PATH, name); };
    at(a, L"d2r_vr_start.txt"); at(b, L"d2r_vr_start_prev.txt");
    MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    g_start = CreateFileW(a, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    g_mirrorUntil.store(g_installedAt + 120000);
    at(a, L"d2r_vr_crash.txt"); at(b, L"d2r_vr_crash_old.txt");
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(a, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 1024 * 1024)) MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    wchar_t sys[MAX_PATH];
    if (UINT n = GetSystemDirectoryW(sys, MAX_PATH); n && n < MAX_PATH - 16) {
        wcscat_s(sys, L"\\dbghelp.dll");
        if (HMODULE d = LoadLibraryW(sys)) g_miniDump = (MiniDumpFn)GetProcAddress(d, "MiniDumpWriteDump");
    }
    g_prev = SetUnhandledExceptionFilter(&Unhandled);
    g_veh = AddVectoredExceptionHandler(0, &Vectored);
    char line[160];
    snprintf(line, sizeof line, "D2R VR vrcam %s starting - this file is the log's first two minutes, written as it goes", version);
    Line(line);
}

// Every 2 s (the update thread): our filter first again if something set its own since; theirs after.
void Keep() {
    const LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(&Unhandled);
    if (cur == &Unhandled) return;
    g_prev = cur;
    static int told = 0;
    if (told++ < 3) LogF("vrcam: crash reports - another crash handler was set (%p): ours goes first again, then it", (void*)cur);
}

void Uninstall() {
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
    const LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(g_prev);
    if (cur != &Unhandled) SetUnhandledExceptionFilter(cur);   // not ours any more: left as it was
    g_mirrorUntil.store(0);
    const HANDLE h = g_start;
    g_start = INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}
}  // namespace crash

// Native OpenXR: the pair's head and hands from the game's own session, at the moment the
// frame will be shown - what the D2R Bridge makes of BodyWalk's frame (hands from the head in
// the frame of its turn alone), without the trip out to BodyWalk and back. The rest of the
// block (the user's height) stays the bridge's.
bool XrIntoShared(D2RVR_Shared* out, const D2RVR_Shared* bridge) {
    D2RVR_XrInput in;
    if (!xr::PairInput(&in) || !in.head.valid) return false;
    if (!bridge) memset(out, 0, sizeof *out);
    const float r2d = 57.2957795f;
    const V3 f = d2rcam::m4::Rotate(in.head.rot, {0.0f, 0.0f, -1.0f});
    const V3 r = d2rcam::m4::Rotate(in.head.rot, {1.0f, 0.0f, 0.0f});
    const V3 u = d2rcam::m4::Rotate(in.head.rot, {0.0f, 1.0f, 0.0f});
    const float yaw = atan2f(-f.x, -f.z);   // in the room, + turned left
    out->version = D2RVR_SHARED_VERSION;
    out->headValid = 1;
    out->headYawDeg = yaw * r2d;
    out->headHeightM = in.head.pos[1];
    out->headPitchDeg = asinf(std::clamp(f.y, -1.0f, 1.0f)) * r2d; out->pitchValid = 1;
    out->headRollDeg = atan2f(-r.y, u.y) * r2d; out->rollValid = 1;
    const float c = cosf(yaw), sn = sinf(yaw), qs = sinf(-yaw * 0.5f), qc = cosf(-yaw * 0.5f);
    auto put = [&](const D2RVR_XrPose& g, float* pos, float* rot) {
        const float dx = g.pos[0] - in.head.pos[0], dy = g.pos[1] - in.head.pos[1], dz = g.pos[2] - in.head.pos[2];
        pos[0] = dx * c - dz * sn;
        pos[1] = dy;
        pos[2] = dx * sn + dz * c;
        const float x = g.rot[0], y = g.rot[1], z = g.rot[2], w = g.rot[3];   // the head's turn taken off
        rot[0] = qc * x + qs * z;
        rot[1] = qc * y + qs * w;
        rot[2] = qc * z - qs * x;
        rot[3] = qc * w - qs * y;
    };
    uint32_t hands = 0;
    // a hand only with its place too (valid 1): PoseOut gives none else, this keeps it so
    if (in.hand[1].grip.valid == 1) { put(in.hand[1].grip, out->rightHand, out->rightRot); hands |= 1u; }
    if (in.hand[0].grip.valid == 1) { put(in.hand[0].grip, out->leftHand, out->leftRot); hands |= 2u; }
    out->handsValid = hands;
    out->rightGrip = (hands & 1u) ? std::clamp(in.hand[1].squeeze, 0.0f, 1.0f) : 0.0f;
    out->leftGrip = (hands & 2u) ? std::clamp(in.hand[0].squeeze, 0.0f, 1.0f) : 0.0f;
    out->gripMagic = D2RVR_GRIP_MAGIC;
    for (int i = 0; i < 3; ++i) out->headRoom[i] = in.head.pos[i];
    out->headYawRoomDeg = yaw * r2d;
    out->roomMagic = D2RVR_ROOM_MAGIC;
    out->sampleStamp = D2RVRStampNow();
    out->sampleStampMagic = D2RVR_SAMPLE_STAMP_MAGIC;
    out->counter++;
    return true;
}

uintptr_t HookDrawGameScreen(int a) {
    static thread_local int depth = 0;
    static bool told = false, toldWhy = false;
    if (depth || !PairWanted()) {
        if (!toldWhy && g_h.blit && AfrOn() && g_set.pairPerTick.load() && !FrameTimeFound()) {
            toldWhy = true;
            Log("vrcam: pair per game frame NOT possible - the frame time was not where expected (another build?); eyes go by turns");
        }
        return OrigDrawGameScreen(a);
    }
    ++depth;
    static bool toldTry = false;   // before the first pair: if the game dies on it, the log says where
    if (!toldTry) { toldTry = true; Log("vrcam: pair per game frame - drawing the first pair"); crash::Mark("drawing the first stereo pair"); }
    // One head for the pair: BodyWalk's block may change between the passes.
    static D2RVR_Shared held;
    const D2RVR_Shared* live = g_shared;
    if (live) { memcpy(&held, live, sizeof held); g_shared = &held; }
    TurnTick();   // the turn up to this very moment, not to the last timer tick
    g_heldYaw.store(g_mouseYaw.load() + g_turnYaw.load());
    g_heldPitch.store(g_mousePitch.load());
    g_pairNow.store(true);
    if (const D2RVR_Shared* hs = g_shared)
        flog::Line("S,%u,%.4f,%.3f,%.3f,%.3f,%.3f,%u", flog::Pass(), flog::Dt(), g_heldYaw.load(), hs->headYawDeg, hs->headPitchDeg, hs->headRollDeg, hs->sampleStamp);

    static double lastEnd = 0.0, sumGap = 0.0, sumPass[2] = {}, sumPresent[2] = {}, sumEff[2] = {}, maxPair = 0.0, sumCpu = 0.0, sumWall = 0.0;
    const double t0 = pairtime::UsNow(), c0 = pairtime::CpuUs();
    pairtime::g_drawThread.store(GetCurrentThreadId());
    if (lastEnd > 0.0) sumGap += t0 - lastEnd;
    pairtime::g_presentAt.store(0.0); pairtime::g_effSum.store(0.0);
    cbcmp::PairBegin();
    rtrace::PairBegin();
    const bool replayRight = replay::Wanted();
    // Native OpenXR (first person, one pass two pictures): the runtime's frame - its wait paces
    // the pair, its views are the eyes the camera draws with (VrViewInner, VrProjInner). Before
    // the left pass is taken: the game's lists from other threads while it blocks stay out of it.
    // (a panel open - inventory, stash, trade, the pause menu: the flat picture, where the head looks)
    // Two passes too (before the replay is on, 240 pairs into an area): the eyes then come with
    // each pass's present (XrPresent) - stereo at once, not 5 s of the flat picture eye after eye
    // (2026-10-10). Only with the world camera running, as the replay itself.
    const bool native = NativeView() && !gamestate::MenuOpen() && d2rcam::InWorld() && replay::XrBegin();
    if (native && XrIntoShared(&held, live)) g_shared = &held;   // the game's own head and hands for this pair
    if (replayRight) replay::BeginLeft();
    SetEye(0);
    d2rcam::Refresh();
    const uintptr_t leftR = OrigDrawGameScreen(a);
    cbcmp::PairMiddle();
    rtrace::PairMiddle();
    const double t1 = pairtime::UsNow();
    sumPass[0] += t1 - t0;
    if (const double pa = pairtime::g_presentAt.load(); pa > t0) sumPresent[0] += t1 - pa;
    sumEff[0] += pairtime::g_effSum.load();
    pairtime::g_presentAt.store(0.0); pairtime::g_effSum.store(0.0);

    float* dt = (float*)d2rsig::Addr(RVA_FRAME_TIME);
    float* rawDt = (float*)d2rsig::Addr(RVA_RAW_FRAME_TIME);
    const float keep = *dt, keepRaw = *rawDt;
    // [stereo] right_dt_ms: the right pass gets this sliver of time instead of none - effects
    // that skip a frame of no time (butterflies seen in the left eye only, 2026-10-04) draw then.
    const float rightDt = g_set.rightDtMs.load() * 0.001f;
    *dt = rightDt; *rawDt = rightDt;
    SetEye(1);
    d2rcam::Refresh();
    const uintptr_t r = replayRight && replay::RightFromLeft() ? leftR : OrigDrawGameScreen(a);
    if (native) replay::XrEnd();   // both eyes to the runtime, or an empty frame if one went missing
    xr::PairOver();
    *dt = keep; *rawDt = keepRaw;
    cbcmp::PairEnd();
    rtrace::PairEnd();
    const double t2 = pairtime::UsNow();
    sumPass[1] += t2 - t1;
    if (const double pa = pairtime::g_presentAt.load(); pa > t1) sumPresent[1] += t2 - pa;
    sumEff[1] += pairtime::g_effSum.load();
    if (lastEnd > 0.0) maxPair = std::max(maxPair, t2 - lastEnd);
    lastEnd = t2;
    sumCpu += pairtime::CpuUs() - c0; sumWall += t2 - t0;

    // BodyWalk's block back before the pair is over: the timer thread, seeing no pair, would
    // otherwise read the held one (the game's own head in native: another zero) meanwhile
    if (live || native) g_shared = live;
    g_pairNow.store(false);
    --depth;

    static ULONGLONG since = GetTickCount64();
    static uint32_t pairs = 0;
    ++pairs;
    if (!told) { told = true; Log("vrcam: pair per game frame ON - left eye, then right eye with the frame time held"); }
    if (const ULONGLONG now = GetTickCount64(); now - since >= 10000) {
        LogF("vrcam: %.1f pairs/s from one game frame each", pairs * 1000.0 / (double)(now - since));
        if (pairs) {
            const double k = 0.001 / pairs;   // us summed -> ms a pair
            LogF("vrcam: a pair, ms: game %.2f | left %.2f (effects %.2f, Present %.2f) | right %.2f (effects %.2f, Present %.2f) | longest %.1f",
                 sumGap * k, sumPass[0] * k, sumEff[0] * k, sumPresent[0] * k, sumPass[1] * k, sumEff[1] * k, sumPresent[1] * k,
                 maxPair * 0.001);
            LogF("vrcam: the two passes busy the game's thread %.0f%% of their time (%.2f ms CPU a pair; the rest waits - GPU, fences, locks)",
                 sumWall > 0.0 ? 100.0 * sumCpu / sumWall : 0.0, sumCpu * k);
        }
        if (replay::g_stepPairs) {
            const double k2 = 0.001 / replay::g_stepPairs;
            double* u = replay::g_stepUs;
            LogF("vrcam: replay steps, ms a pair: wait prev replay %.2f | wait before restore %.2f | hold left %.2f | wait last pair (effects) %.2f | camera %.2f | submit %.2f | present R %.2f | copy L %.2f | present L %.2f",
                 u[0] * k2, u[1] * k2, u[2] * k2, u[3] * k2, u[4] * k2, u[5] * k2, u[6] * k2, u[7] * k2, u[8] * k2);
            for (int i = 0; i < replay::kSteps; ++i) u[i] = 0.0;
            replay::g_stepPairs = 0;
        }
        sumGap = 0.0; maxPair = 0.0; sumCpu = 0.0; sumWall = 0.0;
        for (int e = 0; e < 2; ++e) sumPass[e] = sumPresent[e] = sumEff[e] = 0.0;
        since = now; pairs = 0;
    }
    return r;
}

// Quiet: tried twice a second until the page is decrypted, logs only once it is in.
void InstallBiomeHook() {
    static bool failed = false;
    if (g_h.biome || failed || !g_ctx || !Matches(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome)) return;
    g_h.biome = d2rsig::Hook(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome, (void*)&HookSetBiome, (void**)&OrigSetBiome);
    failed = !g_h.biome;
    Log(g_h.biome ? "vrcam: biome hook in - the sky knows the area" : "vrcam: biome hook FAILED - the sky stays off unless [sky] always=1");
}

// -- Item labels on the ground, through the game's own code (F3 floor) --------
// Recon on the D2RLoader-layout snapshot: docs/labels_recon.md. A label is a
// record of the game's ground-label list: +0x00 its box (x, y, w, h, ints),
// +0x14 the box's colour (float RGBA, the game's own {0, 0, 0, 0.6}), +0x24
// the name, +0xA4 the name's text style.
//   LabelLayout 0x1FA9F0 (unit, name, box out, min x, max x, flag) -> shown:
//     measures the name (TextMeasure 0x909560 at the scale UiScale 0x8460F0
//     returns), puts the box round it with the padding (the profile's
//     inGameTargetPadding times that scale), centred over the item, its
//     bottom at the item.
//   LabelPaint 0x1FA8E0 (box, name, &colour): fills the box (0x657B90 reads
//     the colour there and then), then draws the name in it with TextDraw
//     0x902E20 (name, box, style, scale = UiScale again).
// [hud_floor] labels_alpha: LabelPaint gets a copy of the colour, its alpha
// that much lower - the box fades, the name stays as it is.
// [hud_floor] labels_size: the box made again round the name measured at the
// other scale, about the same bottom middle, and the name drawn at that
// scale - inside LabelPaint only (t_labelScale), every other text untouched.
// Only on the floor; the hooks go in the first time the floor wants them.
constexpr uint64_t RVA_LABEL_LAYOUT = 0x1FA9F0, RVA_LABEL_PAINT = 0x1FA8E0, RVA_TEXT_DRAW = 0x902E20;
constexpr uint64_t RVA_TEXT_MEASURE = 0x909560, RVA_UI_SCALE = 0x8460F0, RVA_IS_HD = 0x846210;
// push rbp, rbx, rsi, r12, r15; mov rbp, rsp; sub rsp, 0x50; mov r12d, r9d; mov byte [rdx], 0; mov rsi, r8
const uint8_t kSigLabelLayout[24] = {0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x57,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x50,0x45,0x8B,0xE1,0xC6,0x02,0x00,0x49,0x8B,0xF0};
// mov [rsp+0x10], rbx; mov [rsp+0x18], rsi; push rdi; sub rsp, 0x40; mov rbx, r8 (the colour)
const uint8_t kSigLabelPaint[18] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x40,0x49,0x8B,0xD8};
// mov [rsp+8], rbx; mov [rsp+0x10], rsi; push rdi; sub rsp, 0x70; movaps [rsp+0x60], xmm6 (the scale is kept in xmm6)
const uint8_t kSigTextDraw[20] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x70,0x0F,0x29,0x74,0x24,0x60};
// called, never hooked: push rbx, rbp, rsi, rdi; mov eax, 0x26A8 (its frame, then __chkstk)
const uint8_t kSigTextMeasure[10] = {0x40,0x53,0x55,0x56,0x57,0xB8,0xA8,0x26,0x00,0x00};
// push rbx; sub rsp, 0x20; mov rbx, [rip + 0x2BFA073] (the interface's scale object)
const uint8_t kSigUiScale[13] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x1D,0x73,0xA0,0xBF,0x02};
// mov rax, [rip + 0x2BF9F59]; test rax, rax; je +8; movzx eax, byte [rax + 0xB9]; ret - the HD graphics on
const uint8_t kSigIsHd[20] = {0x48,0x8B,0x05,0x59,0x9F,0xBF,0x02,0x48,0x85,0xC0,0x74,0x08,0x0F,0xB6,0x80,0xB9,0x00,0x00,0x00,0xC3};
using LabelLayoutFn = bool (*)(void* unit, char* name, int32_t* box, int32_t minX, int32_t maxX, uint8_t flag);
using LabelPaintFn = uintptr_t (*)(const int32_t* box, const char* name, const float* colour);
using TextDrawFn = uintptr_t (*)(const char* text, const int32_t* box, const void* style, float scale);
using TextMeasureFn = uintptr_t (*)(const char* text, const void* style, int32_t* size, float scale, const int32_t* most);
using UiScaleFn = float (*)();
using IsHdFn = bool (*)();
LabelLayoutFn OrigLabelLayout;
LabelPaintFn OrigLabelPaint;
TextDrawFn OrigTextDraw;
thread_local float t_labelScale = 0.0f;   // inside LabelPaint: the name drawn at its scale times this (0 = not inside)

// [hud_floor] labels_size now, 1 = the game's own.
float LabelSizeNow() {
    float alpha, size;
    LabelLookNow(&alpha, &size);
    return g_set.labelsNativeFloor.load() && g_labelSizeIn.load() ? size : 1.0f;
}

bool HookLabelLayout(void* unit, char* name, int32_t* box, int32_t minX, int32_t maxX, uint8_t flag) {
    const bool shown = OrigLabelLayout(unit, name, box, minX, maxX, flag);
    const float k = LabelSizeNow();
    if (!shown || !box || !name || std::abs(k - 1.0f) < 0.005f) return shown;
    if (!((IsHdFn)d2rsig::Addr(RVA_IS_HD))()) return shown;   // the old graphics: their text has no scale
    const float s = ((UiScaleFn)d2rsig::Addr(RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) return shown;
    const int32_t most[2] = {0x7FFFFFFF, 0x7FFFFFFF};   // as the layout asks: one line, no limit
    int32_t was[2] = {}, now[2] = {};
    const TextMeasureFn measure = (TextMeasureFn)d2rsig::Addr(RVA_TEXT_MEASURE);
    measure(name, name + 0x80, was, s, most);
    measure(name, name + 0x80, now, s * k, most);
    // the padding the game put round the name, scaled with it; anything else is not the box expected
    const int32_t padW = box[2] - was[0], padH = box[3] - was[1];
    if (was[0] <= 0 || was[1] <= 0 || now[0] <= 0 || now[1] <= 0 || padW < 0 || padH < 0 || padW > 400 || padH > 200)
        return shown;
    const int32_t w = now[0] + (int32_t)std::ceil((float)padW * k), h = now[1] + (int32_t)std::ceil((float)padH * k);
    const int32_t mid = box[0] + box[2] / 2, bottom = box[1] + box[3];
    box[0] = mid - w / 2; box[1] = bottom - h; box[2] = w; box[3] = h;
    return shown;
}

uintptr_t HookLabelPaint(const int32_t* box, const char* name, const float* colour) {
    float alpha, size;
    LabelLookNow(&alpha, &size);
    if (!colour || !g_set.labelsNativeFloor.load() || (alpha >= 0.995f && std::abs(size - 1.0f) < 0.005f))
        return OrigLabelPaint(box, name, colour);
    float c[4];
    memcpy(c, colour, sizeof c);
    if (c[3] > 0.0f) c[3] *= alpha;   // below 0 the game skips the fill: left so
    const float keep = t_labelScale;
    t_labelScale = LabelSizeNow();
    const uintptr_t r = OrigLabelPaint(box, name, c);
    t_labelScale = keep;
    return r;
}

uintptr_t HookTextDraw(const char* text, const int32_t* box, const void* style, float scale) {
    const float k = t_labelScale;
    return OrigTextDraw(text, box, style, k > 0.0f ? scale * k : scale);
}

// The first time the floor wants them; quiet while the label code is still
// encrypted (D2R decrypts a page the first time it runs: labels shown once).
void InstallLabelHooks() {
    static bool paintFailed = false, sizeFailed = false, drawIn = false;
    static int misses = 0;
    if (!g_ctx || !g_inWorld.load() || !g_set.labelsNativeFloor.load()) return;
    float alpha, size;
    LabelLookNow(&alpha, &size);   // the view now: the hooks go in the first time any view wants them
    const bool wantBox = alpha < 0.995f;
    const bool wantSize = std::abs(size - 1.0f) > 0.005f;
    if (!wantBox && !wantSize) return;
    if (!g_labelPaintIn.load()) {
        if (paintFailed) return;
        if (!Matches(RVA_LABEL_PAINT, kSigLabelPaint, sizeof kSigLabelPaint)) {
            if (++misses == 120)   // a minute on the floor
                Log("vrcam: the game's item-label code not found yet (no labels shown so far, or another game build) - "
                    "[hud_floor] labels_alpha fades them as a picture, labels_size does nothing");
            return;
        }
        const bool in = d2rsig::Hook(RVA_LABEL_PAINT, kSigLabelPaint, sizeof kSigLabelPaint, (void*)&HookLabelPaint, (void**)&OrigLabelPaint);
        paintFailed = !in;
        g_labelPaintIn.store(in);
        Log(in ? "vrcam: item-label hook in - on the floor the labels' box fades by the game's own code ([hud_floor] labels_alpha)"
               : "vrcam: item-label hook FAILED - [hud_floor] labels_alpha fades them as a picture, labels_size does nothing");
        if (!in) return;
    }
    if (!wantSize || g_labelSizeIn.load() || sizeFailed) return;
    if (!Matches(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout) || !Matches(RVA_TEXT_DRAW, kSigTextDraw, sizeof kSigTextDraw))
        return;   // not decrypted yet
    if (!Matches(RVA_TEXT_MEASURE, kSigTextMeasure, sizeof kSigTextMeasure) || !Matches(RVA_UI_SCALE, kSigUiScale, sizeof kSigUiScale) ||
        !Matches(RVA_IS_HD, kSigIsHd, sizeof kSigIsHd)) {
        sizeFailed = true;   // these run all the time, so not encrypted: another build
        Log("vrcam: item-label size NOT possible - the game's text code is not as expected (another build?)");
        return;
    }
    // The name drawn at the other scale first: a smaller box round a name at the old size would wrap it.
    if (!drawIn) drawIn = d2rsig::Hook(RVA_TEXT_DRAW, kSigTextDraw, sizeof kSigTextDraw, (void*)&HookTextDraw, (void**)&OrigTextDraw);
    const bool in = drawIn && d2rsig::Hook(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout, (void*)&HookLabelLayout, (void**)&OrigLabelLayout);
    sizeFailed = !in;
    g_labelSizeIn.store(in);
    Log(in ? "vrcam: item-label size hooks in - on the floor [hud_floor] labels_size sets the labels' size"
           : "vrcam: item-label size hooks FAILED - labels_size does nothing");
}

// The game's addresses for the settings program's Status tab: d2r_vr_game_code.txt
// beside the ini, rewritten whenever the resolver's report changes. Its Scan
// button bumps [status] scan in the ini: what is not found is searched for again.
void GameCodeTick(bool iniChanged) {
    static int answered = -1;
    static uint32_t written = ~0u;
    if (!g_ctx) return;
    if (iniChanged || answered < 0) {
        const int scan = (int)GetPrivateProfileIntW(L"status", L"scan", 0, g_iniPath);
        if (answered >= 0 && scan != answered) {
            Log("vrcam: Scan pressed in D2R VR Settings - the game's addresses not found yet are looked for again");
            d2rsig::Resolve(g_ctx, true);
        }
        if (scan != answered) written = ~0u;   // the program waits for its number in the file
        answered = scan;
    }
    if (d2rsig::Generation() == written) return;
    written = d2rsig::Generation();
    std::wstring path = g_iniPath;
    path.resize(path.size() - wcslen(L"d2r_vr.ini"));
    path += L"d2r_vr_game_code.txt";
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "time %04d-%02d-%02d %02d:%02d:%02d\nscan %d\nsummary %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
            t.wSecond, answered, d2rsig::Summary().c_str());
    fputs(d2rsig::Report().c_str(), f);
    fclose(f);
}

void InstallHooks() {
    if (!g_ctx) return;
    const PluginContext* ctx = g_ctx;
    if (!g_h.camera) {   // cleanroom/camera + drawdist; safe to call again until it is all in
        g_h.camera = d2rcam::Install(ctx);
        LogF("vrcam: camera and render distance %s", g_h.camera ? "in (cleanroom)" : "NOT in yet (code not decrypted, or another build)");
    }
    if (!g_h.skeleton) g_h.skeleton = d2rsig::Hook(RVA_COMPUTE_SELF_WORLD_POSE, kSigComputeSelfWorldPose, sizeof kSigComputeSelfWorldPose, skel::Detour(), skel::OrigSlot());
    if (!g_h.target) {
        g_h.target = d2rsig::Hook(RVA_ATTACK_TARGET, kSigAttackTarget, sizeof kSigAttackTarget, (void*)&HookAttackTarget, (void**)&OrigAttackTarget);
        LogF("vrcam: controller target hook %s", g_h.target ? "in" : "NOT in (code not decrypted yet, or another build)");
    }
    if (!g_h.facing) {
        g_h.facing = d2rsig::Hook(RVA_UNIT_FACING, kSigUnitFacing, sizeof kSigUnitFacing, (void*)&HookUnitFacing, (void**)&OrigUnitFacing);
        LogF("vrcam: attack facing hook %s", g_h.facing ? "in" : "NOT in");
    }
    if (!g_h.point) {
        g_h.point = d2rsig::Hook(RVA_ATTACK_POINT, kSigAttackPoint, sizeof kSigAttackPoint, (void*)&HookAttackPoint, (void**)&OrigAttackPoint);
        LogF("vrcam: attack point hook %s", g_h.point ? "in" : "NOT in");
    }
    if (!g_h.interact) {
        g_h.interact = d2rsig::Hook(RVA_INTERACT_TARGET, kSigInteractTarget, sizeof kSigInteractTarget, (void*)&HookInteractTarget, (void**)&OrigInteractTarget);
        g_interactHooked.store(g_h.interact);
        LogF("vrcam: interact target hook %s", g_h.interact ? "in (pick up never attacks)" : "NOT in (pick up may attack where there is nothing)");
    }
    if (!g_h.blit) {
        g_h.blit = d2rsig::Hook(RVA_DRAW_GAME_SCREEN, kSigDrawGameScreen, sizeof kSigDrawGameScreen, (void*)&HookDrawGameScreen, (void**)&OrigDrawGameScreen);
        LogF("vrcam: draw hook (stereo pair per game frame) %s", g_h.blit ? "in" : "NOT in");
    }
    InstallBiomeHook();
}

bool AllHooksIn() { return g_h.camera && g_h.skeleton && g_h.target && g_h.facing && g_h.point && g_h.blit && g_h.interact; }

// The game put into the view [mode] names. View 1 is the game's own camera
// (vrcam off), 2 third person, 3 and 4 first person (FullBody, InsideFree).
// False while the camera's hooks are not in: tried again on the next tick.
bool ApplyView(const char** said) {
    const bool vr = g_set.platform.load() == 1;
    const int v = ViewNow();
    // F1 is the game's own view from above: vrcam off. The table (F5) is our
    // camera where the head is in the room (TableCamera), the hero whole as from
    // behind; the void black, the headset shows the room there (docs/plan_tabletop_mr.md).
    const bool table = vr && v == 3;   // the game on the floor is F3 since vr_mode_order 2 (as TableView)
    const bool on = v != 1 || (vr && g_set.topPersp.load());
    if (on && !AllHooksIn()) InstallHooks();
    if (on && !g_h.camera) { *said = "vrcam: no camera yet - its hooks are not in (see the log)"; return false; }
    g_view.store(v == 2 || table || (vr && v == 1) ? 2 : 1);
    if (table) g_tableReanchor.store(true);   // the hero put down in front of where the head is now
    g_enabled.store(on);
    g_mouseLookOn.store(MouseLookForView());
    // 4: the game on the floor - our camera follows the head, so FlatVR's screen must too.
    // F1 in true perspective is sent as the game's own view (0): its camera is the game's,
    // not the head's, and with Head Lock on (the bridge locks 1, 3 and 4) the picture
    // stuck to the head ("the camera sticks to my head, it must not", 2026-10-05).
    gamestate::SetViewMode(!on ? 0u : table ? 4u : vr && v == 1 ? 0u : (uint32_t)g_view.load());
    g_gen.fetch_add(1);
    static const char* const kFlat[] = {"", "flat: the game's own camera (F1)", "flat: third person (F2)",
                                        "flat: first person (F3) - mouse look, W A S D"};
    static const char* const kVr[] = {"", "VR: from above (F1)", "VR: third person (F2)",
                                      "VR: the game on the floor (F3) - walk round it; F11 puts it in front of you again",
                                      "VR: first person, the body is yours (F4)"};
    *said = vr ? kVr[v] : kFlat[v];
    return true;
}

// Called from the update thread on every tick and after a view key: applies
// the view once whenever [mode] names another one (the Home page writes it,
// the keys below write it too).
void FollowMode() {
    static int applied = -1;
    static bool toldWait = false;
    const int want = g_set.platform.load() * 10 + ViewNow() + (TopPersp() ? 100 : 0);
    if (want == applied) return;
    const char* said = "";
    if (ApplyView(&said)) { applied = want; toldWait = false; Say(said); }
    else if (!toldWait) { toldWait = true; Log(said); }
}

// A view of the current platform, by number: remembered in the ini, applied at once.
void SetView(int v) {
    if (v < 1 || v > ViewCount()) return;
    const bool vr = g_set.platform.load() == 1;
    (vr ? g_set.vrView : g_set.flatView).store(v);
    wchar_t b[8];
    swprintf_s(b, L"%d", v);
    WritePrivateProfileStringW(L"mode", vr ? L"vr_mode" : L"flat_view", b, g_iniPath);
    FollowMode();
}

// F12 (and the console's "vrcam"): the next view of this platform, round.
const char* Toggle() {
    SetView(ViewNow() % ViewCount() + 1);
    return "vrcam: next view (F12)";
}

void Recenter() {
    float head = 0.0f;
    if (HeadYaw(&head)) g_recenter.store(head);
    if (const D2RVR_Shared* sh = g_shared; sh && sh->pitchValid && std::isfinite(sh->headPitchDeg)) g_recenterPitch.store(sh->headPitchDeg);
    if (const D2RVR_Shared* sh = g_shared; sh && sh->rollValid && std::isfinite(sh->headRollDeg)) g_recenterRoll.store(sh->headRollDeg);
    g_mouseYaw.store(0.0f); g_mousePitch.store(0.0f);
    g_facingReset.store(true);   // and the hero's facing is looked for again
    g_tableReanchor.store(true); // and the table view puts the game in front of the head again
    xr::Recenter();              // and native OpenXR's straight ahead taken from the next views
#if D2RVR_FIRST_PERSON
    skel::ResetGrip();           // and the weapon's grip taken again
#endif
    g_gen.fetch_add(1);
    Log("vrcam: recentered");
}

// Console: "vrcam" = F12.
ConsoleCommandResult __cdecl CmdVrcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}
void OpenShared() {
    if (g_shared) return;
    if (!g_map) g_map = OpenFileMappingW(FILE_MAP_READ, FALSE, D2RVR_SHARED_NAME);
    // The whole section, not sizeof: an older bridge made it smaller (no grips),
    // and a view longer than the section fails. A page holds either.
    if (g_map) g_shared = (const D2RVR_Shared*)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, 0);
    if (g_shared) Log("vrcam: connected to D2R Bridge");
}

// Right stick turn: smooth at turnSpeed, or one snap per flick. Run on the 1 ms
// timer and once more at the start of every pair, so a pair's turn is exactly
// the time since the last one. The time is QPC's: GetTickCount64 moves in
// ~16 ms steps, and the turn went on in steps of 0 or 16 ms - an uneven
// speed that read as judder on stick turns.
SRWLOCK g_turnLock = SRWLOCK_INIT;

void TurnTick() {
    AcquireSRWLockExclusive(&g_turnLock);
    static double last = NowSeconds();
    static bool armed = true;
    const double now = NowSeconds();
    const float dt = (float)std::min(now - last, 0.1);
    last = now;
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_turnLock); } } unlock;
    if (!g_enabled.load() || !g_set.rightTurn.load()) { g_rightX.store(0.0f); return; }
    const float x = g_set.turnSign.load() * g_rightX.load();
    const float dead = 0.25f, a = fabsf(x);
    const float snap = g_set.snapAngle.load();
    if (snap > 0.0f) {
        if (a > 0.7f && armed) { g_turnYaw.store(WrapDeg(g_turnYaw.load() - (x > 0 ? snap : -snap))); armed = false; g_gen.fetch_add(1); }
        if (a < dead) armed = true;
        return;
    }
    if (a < dead) return;
    const float k = (a - dead) / (1.0f - dead);   // 0..1 past the dead zone
    g_turnYaw.store(WrapDeg(g_turnYaw.load() - (x > 0 ? 1.0f : -1.0f) * k * k * g_set.turnSpeed.load() * dt));
    g_gen.fetch_add(1);
}

// F10: the read-only skeleton probe (probe.cpp), on its own thread. It needs
// the hero's position, which is the world camera's look-at point.
std::atomic<bool> g_probeBusy{false};

DWORD WINAPI ProbeThread(void* p) {
    float* hero = (float*)p;
    wchar_t out[MAX_PATH];
    wcscpy_s(out, g_iniPath);
    if (wchar_t* slash = wcsrchr(out, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), L"d2r_vr_herofind.txt");
    static int press = 0;
    char tag[64]; snprintf(tag, sizeof tag, "F10 press %d at tick %llu", ++press, (unsigned long long)GetTickCount64());
    Log("vrcam: looking for the hero transform - writing d2r_vr_herofind.txt");
    probe::FindHero(hero, out, tag);
    Log("vrcam: probe done");
    delete[] hero;
    g_probeBusy.store(false);
    return 0;
}

// The hero's arms (skeletons.cpp): mode, head hiding and the controllers from
// D2R Bridge, handed over every 10 ms. Off whenever the VR camera is off.
// Where the hero's model faces, against where the camera looks. The renderer's
// model-to-world matrix is found by a background search (probe::FindHeroMatrices)
// and then read in place for as long as its translation stays on the hero.
// Several matrices sit on the hero; the one that TURNS when he turns wins.
std::atomic<uintptr_t> g_heroMatrix{0};
std::atomic<bool> g_facingBusy{false};
std::atomic<ULONGLONG> g_facingLastSearch{0};
float g_heroPosNow[3] = {};

// lastOk: when it last read as the hero. late: pairs in which it still changed AFTER the pose of
// the left pass (between that pose and the right pass's) - a copy written after the pose, a frame
// old whenever the left eye's pose reads it, and the pose read off one of those shakes in the left
// eye (frame log 2026-10-04: one copy fresh 125 of 125, the one the walk vote then switched to
// stale 102 of 109). Copies written before the pose never change in between. leftYaw: its yaw
// at the last left-pass pose.
// prevYaw / moved: its yaw at the last pose and when it last changed. An early copy can stop
// being written altogether (2026-10-04: frozen from one spot on while the late ones went on
// turning) - the game has moved the hero's early transform elsewhere; PickHeroMatrix drops it
// and asks for a new search (g_facingSearchNow).
// bad: turning pairs in which its yaw at the left pose was not the frame's yaw (as the most late
// copy, which the renderer writes this frame, has it at the right pose) - stale for the left eye,
// whether written after the pose (late) or only at the end of the pair (0.113-0.115 took one of
// those: it never changes between the poses, yet is a frame old in both). The pick wants none.
struct FacingCand { uintptr_t at; float yaw0; bool turned; int score; ULONGLONG lastOk; uint32_t late = 0; uint32_t bad = 0; float nowYaw = 1000.0f; float leftYaw = 1000.0f;
                    float prevYaw = 1000.0f; ULONGLONG moved = 0; };
std::atomic<bool> g_facingSearchNow{false};
SRWLOCK g_candLock = SRWLOCK_INIT;
std::vector<FacingCand> g_cands;

// The hero point (g_heroPosNow) is the camera's look-at, which follows the hero
// on a spring: running, it trails him by several units. Within 3 units the
// right matrix failed in a run, was dropped, and the body flickered to another.
bool ReadHeroMatrix(uintptr_t a, float* m) {
    return a && SafeRead(m, (void*)a, 64) && m[15] == 1.0f &&
           fabsf(m[12] - g_heroPosNow[0]) < 12.0f && fabsf(m[14] - g_heroPosNow[2]) < 12.0f && m[5] > 0.3f;
}
float MatYawDeg(const float* m) { return atan2f(m[8], m[10]) * 57.2957795f; }

// ---- Binding the doll: where the hero's facing comes from (docs/doll_binding.md, 0.135) ----
// [hands] facing_source=1: straight from the hero's TransformComponent in the renderer's ECS
// (skel::HeroTransform: the SkeletonInstance the pose hook knows as the hero -> its entity ->
// the component's local matrix at +0x00, which the game sets before the frame's poses - the
// "early" copy of plan_left_eye_shake.md). No memory search and no turn needed: bound on the
// first pose of a new model (weapon swap), a new area or after a load. 0 - or whenever the
// chain does not read - the memory search below. Either way the last good facing is held for
// [hands] facing_hold_ms while none reads, so the doll does not come unbound in the gap.
std::atomic<bool> g_facingStruct{false};        // g_heroMatrix is the structural one
std::atomic<bool> g_structOff{false};           // measured a frame old at the left pose: the search instead (F11 retries)
std::atomic<uint32_t> g_structEntity{0};
std::atomic<ULONGLONG> g_structFailSince{0};    // the structural chain has not read since (0 = it reads)
std::atomic<ULONGLONG> g_facingFastUntil{0};    // after an event: the search retried every second, not on the backoff
std::atomic<uint32_t> g_facingWaitMs{2000};      // the search's backoff (FacingThread)

bool StructWanted() { return g_set.facingSource.load() == 1 && !g_structOff.load(); }

// The structural facing, read now: the address of the hero's local matrix (comp + 0x00).
bool FacingStructural(uintptr_t* at, uint32_t* entity = nullptr) {
    static std::atomic<const char*> toldWhy{nullptr};
    static std::atomic<ULONGLONG> toldAt{0};
    auto fail = [&](const char* why) {
        // the reason, when it changes (at most every 5 s)
        if (why != toldWhy.load() && GetTickCount64() - toldAt.load() > 5000) {
            toldWhy.store(why); toldAt.store(GetTickCount64());
            LogF("vrcam: structural facing does not read: %s", why);
        }
        return false;
    };
    skel::HeroXform x;
    const char* why = nullptr;
    if (!skel::HeroTransform(&x, &why)) return fail(why ? why : "?");
    if ((x.parent & 0xFFFFF) != 0xFFFFF) return fail("the hero's transform has a parent - its local matrix is not his world");
    float m[16];
    if (!ReadHeroMatrix(x.comp, m)) return fail("the hero's local matrix is not on the camera's look-at");
    if (toldWhy.load()) { toldWhy.store(nullptr); toldAt.store(0); }
    *at = x.comp;
    if (entity) *entity = x.entity;
    return true;
}

// What unbound the facing last, and when: the hero's model rebuilt (a weapon swap), a new
// area, the first frames in a game area, F11. Kept until the facing is bound again.
SRWLOCK g_facingEvLock = SRWLOCK_INIT;
char g_facingEv[96] = "start";
ULONGLONG g_facingEvAt = 0;

void FacingEvent(const char* what) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_facingEvLock);
    if (!g_facingEvAt) { strncpy_s(g_facingEv, what, _TRUNCATE); g_facingEvAt = now; }   // the first since the last bind
    ReleaseSRWLockExclusive(&g_facingEvLock);
    g_facingFastUntil.store(now + 20000);
    if (g_structFailSince.load()) g_structFailSince.store(now);   // the structural one gets its 3 s from here
    // The search, when it is the source: at once, not after the backoff.
    if (!StructWanted()) { g_facingSearchNow.store(true); g_facingWaitMs.store(1000u); }
}

void PollFacingEvents() {
    static uint32_t heroGen = 0, biomeGen = 0;
    static bool world = false, first = true;
    const uint32_t hg = skel::HeroGen(), bg = g_biomeGen.load();
    const bool w = g_inWorld.load();
    if (first) { first = false; heroGen = hg; biomeGen = bg; world = w; AcquireSRWLockExclusive(&g_facingEvLock); g_facingEvAt = GetTickCount64(); ReleaseSRWLockExclusive(&g_facingEvLock); return; }
    if (w && !world) FacingEvent("the first frames in a game area (load)");
    if (bg != biomeGen) FacingEvent("an area change");
    if (hg != heroGen) FacingEvent("the hero's model rebuilt (weapon swap, new area or load)");
    {   // One hero skeleton is assumed: two that alternate would rebuild the binding all the time.
        static uint32_t gen0 = 0;
        static ULONGLONG t0 = 0;
        static bool told = false;
        const ULONGLONG now = GetTickCount64();
        if (now - t0 > 5000) { t0 = now; gen0 = hg; }
        else if (hg - gen0 > 10 && !told && w) {
            told = true;
            Log("vrcam: more than 10 hero skeletons in 5 s - two /character/player/ models alternate? (facing binding assumes one)");
        }
    }
    heroGen = hg; biomeGen = bg; world = w;
}

// Bound / lost, to the log with the time since the event that unbound it.
void NoteFacing(bool ok, uintptr_t at, bool held) {
    static bool wasOk = false;
    static uintptr_t lastAt = 0;
    if (ok && (!wasOk || at != lastAt)) {
        char ev[96]; ULONGLONG evAt;
        AcquireSRWLockExclusive(&g_facingEvLock);
        memcpy(ev, g_facingEv, sizeof ev); evAt = g_facingEvAt; g_facingEvAt = 0;
        ReleaseSRWLockExclusive(&g_facingEvLock);
        char how[128];
        if (g_facingStruct.load()) snprintf(how, sizeof how, "structural (hero entity %X, TransformComponent local matrix)", g_structEntity.load());
        else snprintf(how, sizeof how, "memory search copy");
        if (evAt) LogF("vrcam: facing bound: %s %p, in %llu ms after %s", how, (void*)at, (unsigned long long)(GetTickCount64() - evAt), ev);
        else LogF("vrcam: facing bound: %s %p (moved, no event)", how, (void*)at);
    } else if (!ok && wasOk) {
        AcquireSRWLockExclusive(&g_facingEvLock);
        if (!g_facingEvAt) { strncpy_s(g_facingEv, "(no event seen)", _TRUNCATE); g_facingEvAt = GetTickCount64(); }
        const std::string ev = g_facingEv;
        ReleaseSRWLockExclusive(&g_facingEvLock);
        LogF("vrcam: facing lost (%s) - %s", ev.c_str(), held ? "holding the last facing ([hands] facing_hold_ms)" : "nothing to hold");
    }
    wasOk = ok;
    if (ok) lastAt = at;
}

// The last good model matrix, held while none reads: its turn, at the look-at as it is now.
SRWLOCK g_holdLock = SRWLOCK_INIT;
float g_holdM[16] = {};
std::atomic<ULONGLONG> g_holdAt{0};

void KeepFacing(const float* m) {
    AcquireSRWLockExclusive(&g_holdLock); memcpy(g_holdM, m, sizeof g_holdM); ReleaseSRWLockExclusive(&g_holdLock);
    g_holdAt.store(GetTickCount64());
}

bool HeldFacing(float* m) {
    const ULONGLONG at = g_holdAt.load();
    if (!at || (float)(GetTickCount64() - at) > g_set.facingHoldMs.load() || !g_inWorld.load()) return false;
    float look[3];
    if (!LookAtNow(look)) return false;
    AcquireSRWLockShared(&g_holdLock); memcpy(m, g_holdM, sizeof g_holdM); ReleaseSRWLockShared(&g_holdLock);
    m[12] = look[0]; m[13] = look[1]; m[14] = look[2];
    return true;
}

// May the whole-memory search run? Always with facing_source=0; with 1 only once the structural
// chain has not read for 3 s (another game build, or something this does not know).
bool SearchAllowed() {
    if (!StructWanted()) return true;
    const ULONGLONG since = g_structFailSince.load();
    return since && GetTickCount64() - since > 3000;
}

// Is the structural matrix this frame's at the left eye's pose? Its world matrix (+0x40) at
// the right pose is the frame's turn (worked out in the left pass, after its pose); the local
// one at the left pose must already have it. Counted in turning pairs, to the log every 10 s;
// a frame old in more than a quarter of 30+ pairs and the search takes over (left-eye shake).
void StructFreshness(uintptr_t at, const float* local) {
    if (!g_pairNow.load()) return;
    float w[16];
    if (!SafeRead(w, (void*)(at + 0x40), 64) || w[15] != 1.0f) return;
    static SRWLOCK lock = SRWLOCK_INIT;
    static float leftLocal = 1000.0f, leftWorld = 1000.0f, lastTruth = 1000.0f;
    static uint32_t fresh = 0, stale = 0, worldStale = 0, allFresh = 0, allStale = 0;
    static ULONGLONG lastLog = 0;
    AcquireSRWLockExclusive(&lock);
    if ((g_eye.load() & 1) == 0) { leftLocal = MatYawDeg(local); leftWorld = MatYawDeg(w); }
    else {
        const float truth = MatYawDeg(w);
        if (leftLocal < 999.0f && lastTruth < 999.0f && fabsf(WrapDeg(truth - lastTruth)) > 0.01f) {
            if (fabsf(WrapDeg(leftLocal - truth)) > 0.01f) { ++stale; ++allStale; } else { ++fresh; ++allFresh; }
            if (fabsf(WrapDeg(leftWorld - truth)) > 0.01f) ++worldStale;
        }
        lastTruth = truth;
        leftLocal = 1000.0f;
    }
    const ULONGLONG now = GetTickCount64();
    if (fresh + stale && now - lastLog > 10000) {
        lastLog = now;
        LogF("vrcam: structural facing at the left pose, turning pairs: this frame's %u, a frame old %u "
             "(its world matrix +0x40 a frame old there in %u - the late copy)", fresh, stale, worldStale);
        fresh = stale = worldStale = 0;
    }
    const bool off = allFresh + allStale >= 30 && allStale * 4 > allFresh + allStale;
    if (off) { allFresh = allStale = 0; }
    ReleaseSRWLockExclusive(&lock);
    if (off && !g_structOff.exchange(true)) {
        Log("vrcam: structural facing is a frame old at the left pose too often - back to the memory search "
            "(F11 tries the structural one again; [hands] facing_source=0 keeps the search)");
        g_facingStruct.store(false);
        g_facingSearchNow.store(true);
    }
}

// The whole-memory search is heavy: run back to back every 2 s while it found
// nothing (an area loading, the hero not drawn yet), it took the game from 90
// pairs a second to 58. So it runs at the lowest priority, and each empty
// search doubles the wait before the next, up to 30 s; a find resets it (g_facingWaitMs,
// above). For 20 s after an event (FacingEvent) the wait is a second at most.

DWORD WINAPI FacingThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    float hero[3]; memcpy(hero, g_heroPosNow, sizeof hero);
    uintptr_t found[64];
    const int n = probe::FindHeroMatrices(hero, found, 64);
    std::vector<FacingCand> c;
    for (int i = 0; i < n; ++i) { float m[16]; if (ReadHeroMatrix(found[i], m)) c.push_back({found[i], MatYawDeg(m), false, 0, GetTickCount64()}); }
    // A copy found again keeps what was learnt of it (turned, the walk's votes, stale counts):
    // a search after an event no longer forgets the pick and waits for a turn to choose again.
    AcquireSRWLockExclusive(&g_candLock);
    for (FacingCand& nc : c)
        for (const FacingCand& oc : g_cands) if (oc.at == nc.at) { nc = oc; nc.lastOk = GetTickCount64(); break; }
    g_cands = c;
    ReleaseSRWLockExclusive(&g_candLock);
    g_facingWaitMs.store(c.empty() ? std::min(g_facingWaitMs.load() * 2u, 30000u) : 2000u);
    LogF("vrcam: %d matrices on the hero - the one that turns with him will be used%s", (int)c.size(),
         c.empty() ? " (none: the next search waits longer)" : "");
    g_facingBusy.store(false);
    return 0;
}

// Where the hero walks, sampled every 100 ms: the game turns him to his
// walking direction, so while he walks steadily the right matrix reads that
// yaw and a wrong one does not. False while he stands or turns.
bool WalkYaw(float* yawDeg) {
    static float last[3] = {}, lastYaw = 1000.0f;
    static ULONGLONG lastT = 0;
    static bool steady = false;
    const ULONGLONG now = GetTickCount64();
    if (now - lastT < 100) return false;
    lastT = now;
    const float dx = g_heroPosNow[0] - last[0], dz = g_heroPosNow[2] - last[2];
    memcpy(last, g_heroPosNow, sizeof last);
    const float d = sqrtf(dx*dx + dz*dz);
    if (d < 0.8f || d > 30.0f) { steady = false; lastYaw = 1000.0f; g_walkSteady.store(false); return false; }   // standing, or a teleport
    const float y = atan2f(dx, dz) * 57.2957795f;
    steady = lastYaw < 999.0f && fabsf(WrapDeg(y - lastYaw)) < 15.0f;
    g_walkTheta.store(atan2f(dz, dx));
    lastYaw = y;
    *yawDeg = y;
    g_walkSteady.store(steady);
    return steady;
}

// The candidate to read: once copies have been seen stale at the left pose (bad,
// see FacingCand), the least stale one the walk does not vote against - the one
// read now kept while it is among those; then the one that agrees with
// the walking direction; before any walk, the first that has turned since it was
// found; then the first not at yaw 0 (a fixed one reads exactly 0), else the first.
uintptr_t PickHeroMatrix() {
    if (g_facingReset.exchange(false)) {
        AcquireSRWLockExclusive(&g_candLock); g_cands.clear(); ReleaseSRWLockExclusive(&g_candLock);
        g_heroMatrix.store(0);
        g_facingStruct.store(false);
        g_facingLastSearch.store(0);
        g_structOff.store(false);   // the structural one gets another chance
        FacingEvent("F11");
        Log("vrcam: F11 - looking for the hero's facing again");
    }
    float walk = 0.0f;
    const bool voteNow = WalkYaw(&walk);   // true at most once per 100 ms (also the stick's walk direction, g_walkTheta)
    // Structural first ([hands] facing_source=1): no candidates, no votes.
    if (StructWanted()) {
        uintptr_t sat = 0; uint32_t ent = 0;
        if (FacingStructural(&sat, &ent)) {
            g_structFailSince.store(0);
            g_structEntity.store(ent);
            g_facingStruct.store(true);
            g_heroMatrix.store(sat);
            return sat;
        }
        if (!g_structFailSince.load()) g_structFailSince.store(GetTickCount64());
        if (!SearchAllowed()) { g_facingStruct.store(false); return 0; }   // a short gap: held, no search yet
    }
    g_facingStruct.store(false);
    uintptr_t pick = 0, nonZero = 0, first = 0, best = 0, fresh = 0;
    int bestScore = 2;   // a few agreeing steps before the walk decides
    uint32_t maxBad = 0, minBad = UINT32_MAX;
    bool currentLeast = false;
    const uintptr_t current = g_heroMatrix.load();
    AcquireSRWLockExclusive(&g_candLock);
    for (size_t i = 0; i < g_cands.size();) {
        float m[16];
        FacingCand& c = g_cands[i];
        // A miss is skipped, not fatal: a matrix gone for good (a new area, death, a
        // teleport) is dropped after 1.5 s of misses, one that just failed a read stays.
        if (!ReadHeroMatrix(c.at, m)) {
            if (GetTickCount64() - c.lastOk > 1500) g_cands.erase(g_cands.begin() + i); else ++i;
            continue;
        }
        c.lastOk = GetTickCount64();
        const float yaw = MatYawDeg(m);
        if (fabsf(WrapDeg(yaw - c.yaw0)) > 3.0f) c.turned = true;
        if (voteNow) c.score = std::clamp(c.score + (fabsf(WrapDeg(yaw - walk)) < 35.0f ? 1 : -1), -20, 20);
        if (c.score <= -6) { g_cands.erase(g_cands.begin() + i); continue; }   // walked against it often enough
        if (c.score > bestScore) { bestScore = c.score; best = c.at; }
        if (!first) first = c.at;
        if (!nonZero && fabsf(yaw) > 0.5f) nonZero = c.at;
        if (!pick && c.turned) pick = c.at;
        ++i;
    }
    // Frozen: not moved for 1.5 s while another copy moved in the last half second.
    ULONGLONG newest = 0;
    for (const FacingCand& c : g_cands) newest = std::max(newest, c.moved);
    const ULONGLONG nowT = GetTickCount64();
    const bool turningNow = newest && nowT - newest < 500;
    // Only one seen moving before can freeze: a copy never seen to move (not the hero's turn at
    // all, or not sampled yet) is the walk vote's to judge (0.114 dropped 32 of 35 that way).
    auto frozen = [&](const FacingCand& c) { return turningNow && c.moved && newest - c.moved > 1500; };
    bool currentFrozen = false;
    for (size_t i = 0; i < g_cands.size();) {
        const FacingCand& c = g_cands[i];
        if (frozen(c)) {
            if (c.at == current) currentFrozen = true;
            if (best == c.at) best = 0;
            if (pick == c.at) pick = 0;
            g_cands.erase(g_cands.begin() + i);
            continue;
        }
        maxBad = std::max(maxBad, c.bad);
        if (c.score >= 0) {
            if (c.bad < minBad) { minBad = c.bad; fresh = c.at; currentLeast = false; }
            if (c.bad == minBad && c.at == current) currentLeast = true;
        }
        ++i;
    }
    if (first && std::none_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == first; })) first = g_cands.empty() ? 0 : g_cands[0].at;
    if (nonZero && std::none_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == nonZero; })) nonZero = 0;
    // Without a turn: the first of three copies 0x40 apart is a TransformComponent's local matrix
    // (local, world, the world before - docs/doll_binding.md), the copy set before the poses.
    uintptr_t head = 0;
    {
        auto has = [&](uintptr_t a) { return std::any_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == a; }); };
        for (const FacingCand& c : g_cands)
            if (c.score >= 0 && has(c.at + 0x40) && has(c.at + 0x80) && !has(c.at - 0x40)) { head = c.at; break; }
    }
    const size_t left = g_cands.size();
    ReleaseSRWLockExclusive(&g_candLock);
    if (currentFrozen) {   // its successor is somewhere the last search did not see: look again
        g_facingSearchNow.store(true);
        LogF("vrcam: facing copy %p stopped moving while the hero turns - looking for his matrices again", (void*)current);
    }
    // All gone - a new area, death, a teleport: look again now, not 2 s after the last search.
    static size_t hadLeft = 0;
    if (left == 0 && hadLeft > 0) { g_facingLastSearch.store(0); g_facingWaitMs.store(2000u); Log("vrcam: the hero's matrices are gone (new area?) - looking again"); }
    hadLeft = left;
    // A few stale poses seen before it decides; the copy read now stays while it is among the least stale.
    if (maxBad < 3 || minBad * 4 > maxBad) fresh = 0;
    else if (currentLeast) fresh = current;
    const uintptr_t a = fresh ? fresh : best ? best : head ? head : pick ? pick : nonZero ? nonZero : first;
    if (a != g_heroMatrix.load()) {
        g_heroMatrix.store(a);
        LogF("vrcam: facing from %p (%s, %d candidates, stale at the left pose: this %u, most %u)", (void*)a,
             fresh ? "this frame's at the left pose" : best ? "agrees with the walk" : head ? "first of a 0x40 group - a transform's local matrix" :
             pick ? "turns, walk not checked yet" : nonZero ? "turned, not yet seen turning" : "not seen turning yet", (int)left,
             fresh ? minBad : 0u, maxBad);
    }
    return a;
}

// [debug] matrix_writer - who writes the hero's model matrix, and when (left-eye shake:
// on the first pass of a pair it is written after the pose is worked out). Hardware write
// breakpoints on its yaw word (A, m[8]) and its x (B, m[12]) for 3 s, each hit tagged with
// the pass and the eye; with the frame log on, the times line up with its P and V rows.
uint64_t MatrixWatchTag() {
    return ((uint64_t)flog::Pass() << 32) | (g_pairNow.load() ? 0x100u : 0u) | (uint64_t)(g_eye.load() & 1);
}
void StartMatrixWatch() {
    const uintptr_t at = g_heroMatrix.load();
    if (!at) { Log("vrcam: matrix_writer - no hero matrix found yet (walk a step), nothing watched"); return; }
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"d2r_vr_matrix_writers.csv");
    if (ww::Start(at + 0x20, at + 0x30, 3000, path, MatrixWatchTag, Log))
        LogF("vrcam: matrix_writer - watching the hero matrix %p for 3 s (flags: 100 = in a pair, 1 = right eye)", (void*)at);
}

// Camera forward and eye point in the hero's model space, from model matrix m.
bool YawFromMatrix(const float* m, float* out, float* eyeModel, bool* eyeOk, float rows[3][3]) {
    *eyeOk = false;
    AcquireSRWLockShared(&g_stickLock); StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    // The camera's forward as it will be drawn, not as last built: the skeleton
    // runs before its pass rebuilds the view, so on the first pass of a pair the
    // stored forward was the last pair's and the body shook in the left eye on
    // every turn. Turned on by what the yaw has moved since that build; a yaw
    // up turns this forward the other way (pose-order log: cam = const - tgt).
    {
        const float d = WrapDeg(TargetYaw() - f.yawAtBuild) * 0.0174532925f;
        if (d != 0.0f && std::isfinite(d)) {
            const float c = cosf(d), sn = sinf(d), x = f.camF[0], z = f.camF[2];
            f.camF[0] = x * c - z * sn; f.camF[2] = z * c + x * sn;
        }
    }
    // world -> model for a row-vector rotation: dot with each (normalised) row
    for (int r = 0; r < 3; ++r) {
        const float l = sqrtf(m[r*4]*m[r*4] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2]);
        for (int c = 0; c < 3; ++c) rows[r][c] = m[r*4+c] / l;
    }
    const float cx = f.camF[0]*rows[0][0] + f.camF[1]*rows[0][1] + f.camF[2]*rows[0][2];
    const float cz = f.camF[0]*rows[2][0] + f.camF[1]*rows[2][1] + f.camF[2]*rows[2][2];
    *out = atan2f(cx, cz);
    // The eye point in model space: (world - translation), dotted with each row
    // over its squared length (the rows carry the model's scale). The eye from
    // the look-at as it is now (g_eyeFromLook); the last rebuild's if unreadable.
    if (g_eyeOk.load()) {
        float eye[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()}, look[3];
        if (LookAtNow(look))
            for (int i = 0; i < 3; ++i) eye[i] = look[i] + g_eyeFromLook[i].load();
        const float d[3] = {eye[0] - m[12], eye[1] - m[13], eye[2] - m[14]};
        for (int r = 0; r < 3; ++r) {
            const float l2 = m[r*4]*m[r*4] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2];
            eyeModel[r] = (d[0]*m[r*4] + d[1]*m[r*4+1] + d[2]*m[r*4+2]) / l2;
        }
        *eyeOk = std::isfinite(eyeModel[0] + eyeModel[1] + eyeModel[2]) && eyeModel[1] > 1.0f;
    }
    return true;
}

// For the skeleton hook, on every pose (skel::SetFresh): the matrix picked on the
// timer, read now. While the game turns the hero to the stick it changes every
// frame, and the 10 ms copy left the body part-turned and shaking.

constexpr uint64_t RVA_DRAW_COUNTER = 0x33ED6D8;   // ++ after every PrismBlit (PrismEndDraw 0x6580DF; read by 0x658230)

// Which of eye, camera look-at and the hero's matrices is a frame old on which
// pass: while the hero walks, every 3 s, the next 8 hero poses go to the log.
// Of one pair's two passes (frame time > 0, then 0) a value that differs
// between them was stale on the first.
void DiagPose(float yawInModel) {
    static ULONGLONG next = 0;
    static int left = 0;
    static float lastLook[3] = {}, lastCamYaw = 0.0f;
    const ULONGLONG now = GetTickCount64();
    float look[3] = {};
    if (!LookAtNow(look)) return;
    AcquireSRWLockShared(&g_stickLock); const StickFrame sf = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    const float camYaw = atan2f(sf.camF[0], sf.camF[2]) * 57.2957795f;
    if (left == 0) {
        const float dx = look[0] - lastLook[0], dz = look[2] - lastLook[2], dy = WrapDeg(camYaw - lastCamYaw);
        memcpy(lastLook, look, sizeof look); lastCamYaw = camYaw;
        if (now < next || (dx*dx + dz*dz < 1e-6f && fabsf(dy) < 0.05f)) return;   // standing and not turning: nothing to see
        left = 16; next = now + 3000;
        Log("pose-order: pass dt vr eye(x z) look(x z) camYaw target bodyInModel | hero matrices (x z yaw)...");
    }
    --left;
    uint32_t pass = 0; float dt = -1.0f;
    SafeRead(&pass, (void*)d2rsig::Addr(RVA_DRAW_COUNTER), 4);
    SafeRead(&dt, (void*)d2rsig::Addr(RVA_FRAME_TIME), 4);
    char b[600];
    int n = snprintf(b, sizeof b, "pose-order: %u %.4f vr %u eye %.3f %.3f look %.3f %.3f cam %.2f tgt %.2f body %.2f |", pass, dt, g_viewBuilds.load(),
                     g_eyeWorld[0].load(), g_eyeWorld[2].load(), look[0], look[2], camYaw, TargetYaw(), yawInModel * 57.2957795f);
    // the matrix in use and its neighbours 0xD0 either side (the second slot seen in the log)
    const uintptr_t at = g_heroMatrix.load();
    for (const intptr_t off : {(intptr_t)-0xD0, (intptr_t)0, (intptr_t)0xD0}) {
        float m[16];
        if (at && SafeRead(m, (void*)(at + off), 64) && m[15] == 1.0f)
            n += snprintf(b + n, sizeof b - n, " %s%03X %.3f %.3f %.1f", off ? "" : "*",
                          (unsigned)((at + off) & 0xFFF), m[12], m[14], atan2f(m[8], m[10]) * 57.2957795f);
    }
    Log(b);
}

// The hero's model matrix is written by the renderer while the poses are
// worked out on other threads: on the first pass of a pair it is sometimes
// this frame's and sometimes the last's (pose-order log, two runs, both
// ways). Whichever, it is made this frame's: the translation is the camera's
// look-at (exactly the hero's position, and already this frame's), and a
// matrix whose translation is not on it yet is a frame old, so its yaw gets
// the last step it turned by.
void HeroMatrixNow(uintptr_t at, float* m) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static uintptr_t lastAt = 0;
    static float lastT[3] = {}, lastYaw = 0.0f, step = 0.0f;
    float look[3];
    if (!LookAtNow(look)) return;
    const float yaw = atan2f(m[8], m[10]);
    const float off = fabsf(m[12] - look[0]) + fabsf(m[14] - look[2]);
    AcquireSRWLockExclusive(&lock);
    if (at != lastAt) { lastAt = at; step = 0.0f; memcpy(lastT, m + 12, sizeof lastT); lastYaw = yaw; }
    else if (m[12] != lastT[0] || m[14] != lastT[2] || yaw != lastYaw) {   // a new write: how far it turned
        const float d = WrapDeg((yaw - lastYaw) * 57.2957795f) * 0.0174532925f;
        step = fabsf(d) < 0.5f ? d : 0.0f;
        memcpy(lastT, m + 12, sizeof lastT); lastYaw = yaw;
    }
    const float turn = off > 1e-3f && off < 2.0f ? step : 0.0f;   // not on the look-at yet: last frame's
    ReleaseSRWLockExclusive(&lock);
    if (turn != 0.0f) {   // rows turned about the world's vertical (row-vector, axes in the rows)
        const float c = cosf(turn), sn = sinf(turn);
        for (int r = 0; r < 3; ++r) {
            const float x = m[r*4], z = m[r*4 + 2];
            m[r*4] = x * c + z * sn; m[r*4 + 2] = -x * sn + z * c;
        }
    }
    if (off < 2.0f) { m[12] = look[0]; m[13] = look[1]; m[14] = look[2]; }
}

// The matrix to read at this pose: the structural one resolved now (its array moves when it
// grows - the 10 ms copy of the address could point at the freed one), else the picked copy.
uintptr_t FacingAtNow(bool* structural) {
    *structural = false;
    if (StructWanted()) {
        uintptr_t sat = 0;
        if (FacingStructural(&sat)) { *structural = true; return sat; }
        if (!SearchAllowed()) return 0;   // the gap: the hook keeps the held values (Input)
    }
    return g_facingStruct.load() ? 0 : g_heroMatrix.load();
}

bool FreshYawInModel(float* yawInModel, float* eyeModel, bool* eyeOk) {
    float m[16], rows[3][3], yaw = 0.0f;
    if (!g_enabled.load()) return false;
    bool structural = false;
    const uintptr_t at = FacingAtNow(&structural);
    if (!ReadHeroMatrix(at, m)) {
        flog::Line("P,%u,%.4f,%d,%p,,,,,,,,,,,nomatrix", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at);
        return false;
    }
    // A frame-old matrix on the first pass of a pair was the left eye's shake: the
    // copy read was one the renderer writes AFTER the poses (or at the pair's end).
    // PickHeroMatrix now keeps to a copy that is this frame's at the left pose
    // (FacingCand::bad); HeroMatrixNow's step is left for the moments it is not.
    float raw[16]; memcpy(raw, m, sizeof raw);
    if (structural) StructFreshness(at, raw);
    if (const float sc = sqrtf(raw[0]*raw[0] + raw[1]*raw[1] + raw[2]*raw[2]); sc > 0.2f && sc < 5.0f) g_heroScale.store(sc);
    HeroMatrixNow(at, m);
    {   // Diagnostics for the jump: does the game lift the unit itself (matrix
        // above the look-at), or only the animation (the hips, skel::HeroEye)?
        static ULONGLONG told = 0;
        float eye = 0.0f, lift = 0.0f, rawLift = 0.0f;
        const float unitLift = raw[13] - m[13];
        if (skel::HeroEye(&eye, &lift, &rawLift) && (rawLift > 0.6f || fabsf(unitLift) > 0.3f) && GetTickCount64() - told > 500) {
            told = GetTickCount64();
            LogF("vrcam: jump - hips %.2f above standing (camera up %.2f units), model matrix %.2f above the look-at",
                 rawLift, lift * g_heroScale.load(), unitLift);
        }
    }
    if (!YawFromMatrix(m, &yaw, eyeModel, eyeOk, rows)) {
        flog::Line("P,%u,%.4f,%d,%p,%.3f,%.3f,%.3f,,,,,,,,noview", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at,
                   raw[12], raw[14], atan2f(raw[8], raw[10]) * 57.2957795f);
        return false;
    }
    const LONGLONG logicAt = g_logicFacingAt.load();
    flog::Line("P,%u,%.4f,%d,%p,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%.2f", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at,
               raw[12], raw[14], atan2f(raw[8], raw[10]) * 57.2957795f, m[12], m[14], atan2f(m[8], m[10]) * 57.2957795f,
               (g_set.facingSign.load() * yaw) * 57.2957795f, eyeModel[0], eyeModel[1], eyeModel[2], *eyeOk ? "ok" : "noeye",
               [] { float d = -999.0f; return AskHeroFacingNow(&d) ? d : -999.0f; }(), logicAt ? (QpcUs() - logicAt) / 1000.0 : -1.0);
    {   // every copy of the hero's matrix at this pose: late (written after the pose) and bad (not
        // yet this frame's at the left pose - FacingCand)
        const bool logIt = flog::g_on.load();
        const int eye = g_eye.load() & 1;
        const bool pair = g_pairNow.load();
        char b[700]; int k = logIt ? snprintf(b, sizeof b, "C,%u,%d", flog::Pass(), eye) : 0;
        static float lastTruth = 1000.0f;   // this frame's yaw as the right pass found it, last pair
        AcquireSRWLockExclusive(&g_candLock);
        float truth = 1000.0f; uint32_t truthLate = 2;   // the most late copy: written this frame by the renderer
        for (FacingCand& c : g_cands) {
            float cm[16];
            c.nowYaw = 1000.0f;
            if (!SafeRead(cm, (void*)c.at, 64) || cm[15] != 1.0f) continue;
            const float y = c.nowYaw = MatYawDeg(cm);
            if (c.prevYaw < 999.0f && fabsf(WrapDeg(y - c.prevYaw)) > 0.01f) c.moved = GetTickCount64();
            c.prevYaw = y;
            if (pair && eye == 1 && c.leftYaw < 999.0f && fabsf(WrapDeg(y - c.leftYaw)) > 0.01f) c.late++;
            if (pair && eye == 1 && c.late > truthLate) { truthLate = c.late; truth = y; }
            if (logIt && k < (int)sizeof b - 24)
                k += snprintf(b + k, sizeof b - k, ",%llX:%.2f", (unsigned long long)(c.at & 0xFFFFF), y);
        }
        for (FacingCand& c : g_cands) {
            if (pair && eye == 0) { c.leftYaw = c.nowYaw; continue; }
            // While he turns (this frame's yaw is new), a copy that did not have it at the left pose is bad.
            if (pair && truth < 999.0f && lastTruth < 999.0f && fabsf(WrapDeg(truth - lastTruth)) > 0.01f &&
                c.leftYaw < 999.0f && fabsf(WrapDeg(c.leftYaw - truth)) > 0.01f) c.bad++;
            c.leftYaw = 1000.0f;
        }
        if (pair && eye == 1 && truth < 999.0f) lastTruth = truth;
        ReleaseSRWLockExclusive(&g_candLock);
        if (logIt) flog::Line("%s", b);
    }
    if (g_set.poseOrderLog.load()) DiagPose(g_set.facingSign.load() * yaw);
    *yawInModel =g_set.facingSign.load() * yaw + g_set.bodyYawDeg.load() * 0.0174532925f;
    return true;
}

// Camera forward in the hero's model space as a yaw (rad) about its up axis,
// 0 = the model faces where we look. False while the matrix is not known.
bool CameraYawInModel(float* out, float* eyeModel, bool* eyeOk) {
    *eyeOk = false;
    PollFacingEvents();
    const uintptr_t a = PickHeroMatrix();
    // The copy read has stopped moving while the others turn (PickHeroMatrix), or an event asked
    // for it (FacingEvent): search again now, the game has moved the hero's copies somewhere new.
    if (g_facingSearchNow.load() && SearchAllowed() && !g_facingBusy.load() && d2rcam::InWorld()) {
        g_facingSearchNow.store(false);
        g_facingBusy.store(true);
        g_facingLastSearch.store(GetTickCount64());
        if (HANDLE h = CreateThread(nullptr, 0, FacingThread, nullptr, 0, nullptr)) CloseHandle(h); else g_facingBusy.store(false);
    }
    float m[16];
    const bool ok = ReadHeroMatrix(a, m);
    bool held = false;
    if (ok) KeepFacing(m);
    else {
        // lost (moved, reallocated) or never found: look again, 2 s after the last search, longer after
        // empty ones (a second at most for 20 s after an event) - unless the structural one is the source
        uint32_t wait = g_facingWaitMs.load();
        if (GetTickCount64() < g_facingFastUntil.load()) wait = std::min(wait, 1000u);
        if (SearchAllowed() && !g_facingBusy.load() && GetTickCount64() - g_facingLastSearch.load() > wait && d2rcam::InWorld()) {
            g_facingBusy.store(true);
            g_facingLastSearch.store(GetTickCount64());
            if (HANDLE h = CreateThread(nullptr, 0, FacingThread, nullptr, 0, nullptr)) CloseHandle(h); else g_facingBusy.store(false);
        }
        // Not dropped: the last good facing, at the hero's place now, for [hands] facing_hold_ms.
        held = HeldFacing(m);
    }
    NoteFacing(ok, a, held);
    if (!ok && !held) return false;
    float rows[3][3];
    if (!YawFromMatrix(m, out, eyeModel, eyeOk, rows)) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    // Facing diagnostics, four times a second while on: the camera's yaw, the
    // model matrix's facing in the world, the yaw handed to the body, and the
    // animation's own root/anim/pelvis yaw in model space.
    static ULONGLONG lastDiag = 0;
    if (GetTickCount64() - lastDiag >= 250) {
        lastDiag = GetTickCount64();
        float py[3]; skel::PoseYaws(py);
        // the source, and with the search the structural yaw beside it (a live comparison)
        char src[96];
        if (!ok) snprintf(src, sizeof src, "held");
        else if (g_facingStruct.load()) snprintf(src, sizeof src, "structural");
        else {
            uintptr_t sat = 0; float sm[16];
            if (FacingStructural(&sat) && ReadHeroMatrix(sat, sm)) snprintf(src, sizeof src, "search, structural %.1f", MatYawDeg(sm));
            else snprintf(src, sizeof src, "search, no structural");
        }
        LogF("facing: cam %.1f | model %.1f (mat %p, %s) | cam-in-model %.1f | pose root %.1f anim %.1f pelvis %.1f | hero %.1f %.1f",
             atan2f(f.camF[0], f.camF[2]) * 57.2957795f, atan2f(rows[2][0], rows[2][2]) * 57.2957795f, (void*)a, src,
             *out * 57.2957795f, py[0], py[1], py[2], g_heroPosNow[0], g_heroPosNow[2]);
    }
    return true;
}

// For the skeleton hook, on every pose: the controllers from the block the
// camera reads its head from (during a pair, the pair's own copy).
bool FreshHands(skel::Input* in) {
    const D2RVR_Shared* sh = g_shared;
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid) return false;
    in->handsValid = sh->handsValid;
    memcpy(in->hand[0], sh->rightHand, sizeof in->hand[0]); memcpy(in->hand[1], sh->leftHand, sizeof in->hand[1]);
    memcpy(in->rot[0], sh->rightRot, sizeof in->rot[0]); memcpy(in->rot[1], sh->leftRot, sizeof in->rot[1]);
    in->userHeadM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
    in->grip[0] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->rightGrip : -1.0f;
    in->grip[1] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->leftGrip : -1.0f;
    return true;
}

void PushArms() {
    // Only in a game area: on the main menu's character screen the hero stood
    // headless, his arms on the controllers (2026-10-05) - there he is the game's.
    const bool on = g_enabled.load() && g_inWorld.load();
    const D2RVR_Shared* sh = g_shared;
    skel::Input in;
    if (sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid) {
        in.handsValid = sh->handsValid;
        memcpy(in.hand[0], sh->rightHand, sizeof in.hand[0]); memcpy(in.hand[1], sh->leftHand, sizeof in.hand[1]);
        memcpy(in.rot[0], sh->rightRot, sizeof in.rot[0]); memcpy(in.rot[1], sh->leftRot, sizeof in.rot[1]);
        in.userHeadM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
        in.grip[0] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->rightGrip : -1.0f;
        in.grip[1] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->leftGrip : -1.0f;
    }
    float hero[3];
    if (LookAtNow(hero)) {
        skel::SetHeroPos(hero[0], hero[1], hero[2]);
        memcpy(g_heroPosNow, hero, sizeof hero);
    }
    // third person, the game on the floor and F1 in perspective: the whole hero as the game
    // animates him, head included (on the floor he stood headless, 2026-10-05)
    const bool own = on && !ThirdPerson() && !Overhead();
    in.mode = own ? ArmsMode() : 0;
    // From behind the game animates all of him, but he still turns with the
    // camera: else the right stick only swung the camera round a hero who
    // kept facing wherever he last walked.
    // Without the body the hero is turned whole, about the vertical only, and seen from his eyes.
    in.bodyTurn = own ? (FullBody() ? g_set.bodyTurn.load() : 2) : on && ThirdPerson() && !Overhead() && g_set.thirdTurn.load() ? 2 : 0;
#if D2RVR_FIRST_PERSON
    in.hideHead = own && (g_set.hideHead.load() || !FullBody());
    const int likeStaff = HeldLikeStaff();
    // a staff in the hands: aimed from hand to hand ([bow] staff_two_hands) or fast in the right one with
    // the left free (staff_free_left) - the latter no longer waits for the aim's box (off by default since
    // 2026-10-06, it left the free left hand stiff in the staff's grip, never closing with the grip)
    in.staff = own && (((g_set.staffTwoHands.load() || g_set.staffFreeLeft.load()) && gamestate::WeaponType() == D2RVR_TYPE_STAFF) ||
                       XbowLikeStaff() || likeStaff);
    in.staffHand = gamestate::WeaponType() == D2RVR_TYPE_CROSSBOW ? g_set.xbowHand.load()
                 : likeStaff == 1 ? (gamestate::WeaponType() == D2RVR_TYPE_SPEAR || gamestate::WeaponType() == D2RVR_TYPE_POLEARM
                                     ? g_set.spearHand.load() : g_set.axeHand.load())
                 : likeStaff == 2 ? g_set.swordHand.load()
                 // a staff: [hands] staff_hand, right by default like every other weapon (a barbarian's
                 // battle staff floated off the hand with the old left wrist, 2026-10-06)
                 : StaffByRule() ? g_set.staffHand.load() : 1;
    in.staffHandAuto = likeStaff != 0;   // the staff and the crossbow keep the wrist they were found on
    in.leftHilt = likeStaff == 2;
    in.leftHiltAtM = g_set.swordLeftAtCm.load() * 0.01f;
    in.leftHiltSlideM = g_set.swordSlideCm.load() * 0.01f;
    {
        const uint32_t t = gamestate::WeaponType();
        in.shaftAxis = likeStaff == 2 ? g_set.swordHiltAxis.load()
                     : likeStaff == 1 ? (t == D2RVR_TYPE_SPEAR || t == D2RVR_TYPE_POLEARM ? g_set.spearShaftAxis.load() : g_set.axeShaftAxis.load())
                     : StaffByRule() ? g_set.staffShaftAxis.load()
                     : 0;
    }
    in.stockAxis = g_set.xbowStockAxis.load();
    in.gunFrame = g_set.xbowGunFrame.load();
    for (int i = 0; i < 3; ++i) in.lineTurnDeg[i] = g_set.xbowLine[i].load();
    in.staffHands = g_set.staffHands.load();
    // spears, polearms and two-handed swords have only this way (the old one turned the
    // weapon through both hands as the animation holds them)
    in.staffFreeLeft = g_set.staffFreeLeft.load() || likeStaff;
    // [hands] fist: a hand whose slot holds nothing is free. Bows and crossbows
    // keep the animation's hands (the drawing hand holds the string); a staff
    // sits in the right slot, and the left is free only in staff_free_left
    // (the hook then keeps it from a fist while it is on the shaft).
    in.fist = own && g_set.fist.load();
    for (int c = 0; c < 8; ++c) { in.fistClose[c] = g_set.fistClose[c].load() * 0.01f; in.fistOpen[c] = g_set.fistOpen[c].load() * 0.01f; }
    {
        const uint32_t held = gamestate::HandsHeld(), type = gamestate::WeaponType();
        const bool xbowHeld = type == D2RVR_TYPE_CROSSBOW && in.staff;   // held as the staff, see XbowLikeStaff
        const bool ranged = type == D2RVR_TYPE_BOW || (type == D2RVR_TYPE_CROSSBOW && !xbowHeld);
        const bool staffBoth = (type == D2RVR_TYPE_STAFF || xbowHeld) && !(in.staffFreeLeft && in.staff);
        // Held in the right hand alone (staff_free_left): the left is free whichever slot the
        // game keeps it in - a crossbow sits in the left one, and that hand stayed stiff in the
        // animation's grip, never relaxed nor closed with the grip as on the staff (2026-10-05).
        const bool rightHolds = in.staff && in.staffFreeLeft;
        for (int side = 0; side < 2; ++side)
            in.handFree[side] = rightHolds ? side == 1 : !(held & (1u << side)) && !ranged && !staffBoth;
    }
    {   // staff_free_left: once per staff taken up, what the hook made of it
        static bool told = false;
        static uint32_t toldKind = 0;   // told again for another weapon held this way
        float dir[3], gap = 0.0f;
        const bool now = in.staff && in.staffFreeLeft && in.mode == 2 && skel::StaffAxis(dir, &gap);
        const uint32_t kindNow = gamestate::WeaponType() * 4u + (uint32_t)likeStaff;
        if (now && (!told || kindNow != toldKind)) {
            toldKind = kindNow;
            float sc[2];
            const int found = skel::GripHand(sc);
            const int wrist = in.staffHandAuto && found >= 0 ? found : in.staffHand;
            const uint32_t type = gamestate::WeaponType();
            LogF("vrcam: %s%s in the right hand (the game hangs it on the %s wrist%s; attach bones R %.2f L %.2f), hands %.2f model units "
                 "apart in its grip, shaft %.2f %.2f %.2f; left hand on it %s%s", likeStaff == 2 ? "two-handed " : "",
                 type < D2RVR_TYPE_COUNT ? kD2RVRWeaponTypeNames[type] : "?", wrist == 0 ? "right" : "left",
                 !in.staffHandAuto ? "" : found >= 0 ? ", read from its attach bones" : ", attach bones did not tell - [hands] spear_hand/sword_hand",
                 sc[0], sc[1], gap, dir[0], dir[1], dir[2],
                 in.grip[1] < 0.0f ? "always (bridge sends no grip - older than 0.13)" : "while its grip is held",
                 in.leftHilt ? " (on the hilt, does not turn it)" : "");
        }
        told = now;
        // which bone axis the crossbow's stock was laid along (skeletons GunFrame) - it jumped with the grip
        static int toldAxis = 0;
        if (const int ax = now ? skel::GunAxis() : 0; ax && ax != toldAxis) {
            toldAxis = ax;
            LogF("vrcam: crossbow stock along its bone's %c%c axis (grip gap %.2f)%s", ax < 0 ? '-' : '+', "xyz"[abs(ax) - 1], gap,
                 g_set.xbowStockAxis.load() ? " - [hands] xbow_stock_axis" : " - from the grip");
        }
    }
    in.weaponType = (int)gamestate::WeaponType();
    in.weaponSide = (int)gamestate::WeaponHand();
    // The grip is taken again for any change of what the two hands hold, not only of the kind: a barbarian's
    // axe and shield, then the shield for a dagger - still "Axe", the dagger hung where the shield's stance had
    // the left attach bone, out of the left hand (2026-10-06). Kept in the bits above the kind's own key.
    const int handsKey = (int)(gamestate::HandsKey() & 0x7FFFu) << 16;
    in.carryAxis = g_set.carryAxis.load();
    in.carryM = g_set.carryCm.load() * 0.01f;
    in.bothAttach = g_set.bothAttach.load() && (gamestate::TwoHanded() & D2RVR_TWO_HANDS_ON) && in.weaponType != D2RVR_TYPE_BOW && in.weaponType != D2RVR_TYPE_CROSSBOW;
    if (in.bothAttach) {   // which attach bone the game shows the weapon on, when that changes
        static int told = -1;
        float sz[2]; skel::AttachSizes(sz);
        const int now = (sz[0] > 0.5f ? 1 : 0) | (sz[1] > 0.5f ? 2 : 0);
        if (now != told) { told = now; LogF("vrcam: two-handed weapon's attach bones as the game animates them: R %.2f L %.2f", sz[0], sz[1]); }
        static ULONGLONG diagAt = 0;   // what the game's animation does with them, once a second ([debug] weapon_diag)
        if (g_set.weaponDiag.load() && GetTickCount64() - diagAt > 1000) {
            diagAt = GetTickCount64();
            float d[14]; skel::WeaponDiag(d);
            LogF("vrcam: 2H diag (game pose) R attach vs grip %.1f cm %.0f deg | L attach vs grip %.1f cm %.0f deg | skins apart %.1f cm | "
                 "attaches apart %.1f cm | from right wrist R %.1f L %.1f cm | walking %d", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], g_walkSteady.load() ? 1 : 0);
            LogF("vrcam: 2H ours: R attach vs grip %.1f cm %.0f deg (as we leave it) | det L %.2f R %.2f", d[10], d[11], d[12], d[13]);
            Log(("vrcam: 2H item: " + skel::WeaponItemDiag()).c_str());   // LogF stops at 256
            Log(("vrcam: 2H entity: " + skel::WeaponEntityDiag()).c_str());
        }
    }
    in.gunStock = in.staff && in.weaponType == D2RVR_TYPE_CROSSBOW;
    // a sword in both hands is held otherwise than in one (the game's 2HS animation): its own grip
    if (likeStaff == 2) in.weaponType += 0x100;
    // and the grip is taken relative to the wrist it hangs on: another wrist, another grip
    if (in.staff) in.weaponType += 0x1000 * (in.staffHand + 1);
    in.weaponType += handsKey;
    // Every weapon sits fast in its grip: following the game's animation, the bow,
    // the crossbow and the rest jumped about in the hand at every shot or blow
    // ("fix it all, no movement", 2026-10-05). Its own parts still move (a bow's
    // string). Javelins and throwing weapons too for now - a thrown one is hidden
    // by the game shrinking its bone, which stays the game's (skeletons.cpp);
    // that it flies out of the hand comes back later as an exception.
    in.holdGrip = true;
    in.attacking = g_skillHeld.load();
    for (int i = 0; i < 3; ++i) in.staffOffset[i] = g_set.staffOffset[i].load();
    for (int i = 0; i < 3; ++i) in.leftStaffOffset[i] = (in.gunStock ? g_set.xbowLeftOffset[i] : in.leftHilt ? g_set.leftSwordOffset[i] : g_set.leftStaffOffset[i]).load();
    // Weapon Adjust: the same for every two-handed weapon ("for all of them", 2026-10-06 - it was per kind);
    // a crossbow keeps its own (xbow_left_*), its left hand sits under the stock
    for (int i = 0; i < 3; ++i) in.leftStaffTurnDeg[i] = in.gunStock ? 0.0f : g_set.leftHoldTurn[i].load();
    if (!in.gunStock)
        for (int i = 0; i < 3; ++i) in.leftStaffOffset[i] += g_set.leftHold[i].load();
    in.leftStaffRollDeg = in.gunStock ? g_set.xbowLeftRoll.load() : 0.0f;
    in.leftStaffAtM = in.gunStock ? g_set.xbowLeftAtCm.load() * 0.01f : -1.0f;
    in.staffGrabM = g_set.staffGrabCm.load() * 0.01f;
    if (const uint32_t type = gamestate::WeaponType(); type < D2RVR_TYPE_COUNT) memcpy(in.weaponAdj, g_set.weaponAdj[type], sizeof in.weaponAdj);
    memcpy(in.shrinkBone, g_set.shrinkBone, sizeof in.shrinkBone);
    in.lockUpper = own && FullBody() && g_set.lockUpper.load();   // the game's animation in the other views
#endif
    in.legsUnder = g_set.legsUnder.load();
    in.followJump = own && g_set.followJump.load();
    in.jumpFrom = g_set.jumpFrom.load();
    {   // the hero's own eye height, once per hero (class) and when it changes
        static float toldEye = 0.0f;
        float eye = 0.0f, lift = 0.0f;
        if (skel::HeroEye(&eye, &lift) && fabsf(eye - toldEye) > 0.01f) {
            toldEye = eye;
            LogF("vrcam: the hero's eyes stand %.2f model units up = %.2f world units (scale %.3f)%s",
                 eye, eye * g_heroScale.load(), g_heroScale.load(),
                 g_set.heightAuto.load() ? " - the camera's height" : " - unused, [camera] height_auto=0");
        }
    }
    static int toldLock = 0;
    if (const int ls = skel::LockState(); ls != toldLock) {
        toldLock = ls;
        Log(ls > 0 ? "vrcam: bind pose read - the body above the pelvis can be held still"
                   : "vrcam: the bind pose did not read as a pose - [body] lock stays off");
    }
    in.scale = g_set.armScale.load();
    in.wrist = g_set.wrist.load();
    in.wristDeg[0] = g_set.wristPitch.load(); in.wristDeg[1] = g_set.wristYaw.load(); in.wristDeg[2] = g_set.wristRoll.load();
    float yaw = 0.0f;
    in.handOffset[0] = g_set.handSide.load() * 0.01f;
    in.handOffset[1] = g_set.handUp.load() * 0.01f;
    in.handOffset[2] = -g_set.handFwd.load() * 0.01f;
    bool eyeOk = false;
    in.haveYaw = on && (in.mode == 2 || in.bodyTurn > 0) && CameraYawInModel(&yaw, in.eyeModel, &eyeOk);
    in.haveEye = in.haveYaw && eyeOk;
    // the offset turns arms and body together: both read "ahead" from this yaw
    in.yawInModel = in.haveYaw ? g_set.facingSign.load() * yaw + g_set.bodyYawDeg.load() * 0.0174532925f : 0.0f;
    skel::Set(in);
}

// F10: one second of distinct skeletons into d2r_vr_skeletons.txt (skeletons.cpp).
void StartProbe();

void DumpSkeletons() {
    wchar_t out[MAX_PATH];
    wcscpy_s(out, g_iniPath);
    if (wchar_t* slash = wcsrchr(out, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), L"d2r_vr_skeletons.txt");
    Log(g_h.skeleton ? (skel::Dump(out) ? "vrcam: dumping skeletons for one second" : "vrcam: could not open d2r_vr_skeletons.txt")
                     : "vrcam: skeleton hook not installed - no dump");
    StartProbe();   // and the whole-memory search for where the hero's facing lives
}

void StartProbe() {
    if (!d2rcam::InWorld() || g_probeBusy.exchange(true)) return;
    float* hero = new float[3];
    if (!LookAtNow(hero)) { delete[] hero; g_probeBusy.store(false); return; }
    if (HANDLE h = CreateThread(nullptr, 0, ProbeThread, hero, 0, nullptr)) CloseHandle(h);
    else { delete[] hero; g_probeBusy.store(false); }
}

// Sky on or off, judged on the timer: the vrcam camera on, [sky] enabled, an
// outdoor biome, no menu and no side panel. Each change of mind goes to the log
// with its reason - the biome names of caves and dungeons are learnt from it.
std::atomic<int> g_skyPalette{-1};   // kPalettes index while the sky shows, else -1
std::atomic<int> g_fogAct{0};        // the area's act for the fog colour, caves too; 0 = unknown
std::atomic<bool> g_underground{false};   // in the world, off the outdoor list: the fog takes [fog] color_caves
std::atomic<bool> g_inWorld{false};  // a game area is loaded: not the main menu (its own "frontend" biome), not before any
std::atomic<bool> g_ceilBiome{false};   // the area's biome is on [ceiling] biomes

// The game's own day and night (2026-10-05). The game draws a debug line with
// the time of day ("End of dusk, start of night" ... and "Env Cycle = %i") from
// an environment struct it reaches as [global] + 8: +0x00 the period (0..5, its
// six names), +0x08 the time in ticks (25 a second), +0x0C the light now, 0..255,
// which the game eases from period to period, +0x20/+0x28 the sun's direction.
// The global is found through that code - the only place in the image where
//   48 8B 0D <global> E8 <getter> 48 63 C8 4C 8D 05 <"%s"> BA 32 00 00 00 48 8D 05 <the names>
// stands - so another build of the game is found again; the reads are guarded.
namespace env {
std::atomic<uintptr_t> g_slot{0};   // the global's address, 0 = not found yet

bool SafeRead(const void* p, void* out, size_t n) {
    __try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PageReadable(uintptr_t a) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery((void*)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    const DWORD pr = mi.Protect & 0xFF;
    return !(mi.Protect & PAGE_GUARD) && pr != PAGE_NOACCESS;
}

// Every 5 s until found (the image's pages are decrypted late); on the update thread.
void Find() {
    if (g_slot.load() || !g_base) return;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 5000) return;
    last = GetTickCount64();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!SafeRead((void*)g_base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    if (!SafeRead((void*)(g_base + dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return;
    static const uint8_t kSig[] = {0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x48, 0x63, 0xC8, 0x4C, 0x8D, 0x05, 0, 0, 0, 0,
                                   0xBA, 0x32, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05};
    static const bool kAny[] = {0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    constexpr size_t kChunk = 0x10000, kLen = sizeof kSig;
    static uint8_t buf[kChunk + kLen];
    const uintptr_t end = g_base + nt.OptionalHeader.SizeOfImage;
    for (uintptr_t a = g_base + 0x1000; a < end; a += kChunk) {
        const size_t n = (size_t)std::min<uintptr_t>(kChunk + kLen, end - a);
        if (!PageReadable(a) || !SafeRead((void*)a, buf, n)) continue;
        for (size_t i = 0; i + kLen <= n; ++i) {
            if (buf[i] != 0x48 || buf[i + 1] != 0x8B) continue;
            size_t k = 0;
            while (k < kLen && (kAny[k] || buf[i + k] == kSig[k])) ++k;
            if (k < kLen) continue;
            int32_t disp;
            memcpy(&disp, buf + i + 3, 4);
            g_slot.store(a + i + 7 + disp);
            LogF("vrcam: the game's day and night found - environment through the global at +0x%llX", (unsigned long long)(a + i + 7 + disp - g_base));
            return;
        }
    }
    static bool told = false;
    if (!told) { told = true; Log("vrcam: the game's day and night not found yet (looked for the Env Cycle code) - the sky stays as set"); }
}

// The light now (0..255) and the period (0..5); false in menus, between games or not found.
bool Now(int* light, int* period) {
    const uintptr_t slot = g_slot.load();
    uintptr_t obj = 0, e = 0;
    int32_t v[4];
    if (!slot || !SafeRead((void*)slot, &obj, 8) || obj < 0x10000 || !SafeRead((void*)(obj + 8), &e, 8) || e < 0x10000 ||
        !SafeRead((void*)e, v, sizeof v))
        return false;
    if (v[0] < 0 || v[0] > 5 || v[3] < 0 || v[3] > 255) return false;
    *period = v[0];
    *light = v[3];
    return true;
}
}  // namespace env

// What the sky and the open-air fog are multiplied by now: 1 by day, [sky]
// night_brightness at night, in between as the game's own light goes - so they
// darken and brighten with the world, at its pace.
// The daylight 0 (full night) .. 1 (full day) from the game's light; -1 not known.
float DayNightKRaw(int* light, int* period) {
    if (!g_set.skyDayNight.load() || !env::Now(light, period)) return -1.0f;
    const float lo = g_set.lightNight.load(), hi = std::max(lo + 1.0f, g_set.lightDay.load());
    return std::clamp((*light - lo) / (hi - lo), 0.0f, 1.0f);
}
// For the fog: unknown = 0, the act's colour as set (what it was before day and night).
float DayNightK() { int l = 0, p = 0; return std::max(0.0f, DayNightKRaw(&l, &p)); }

float DayNightMul() {
    int light = 0, period = 0;
    const float k = DayNightKRaw(&light, &period);
    if (k < 0.0f) return 1.0f;
    static int toldLight = -100, toldPeriod = -1;
    if (abs(light - toldLight) >= 10 || period != toldPeriod) {
        toldLight = light; toldPeriod = period;
        static const char* const kNames[6] = {"end of night, dawn", "end of dawn, day", "day", "end of day, dusk", "end of dusk, night", "night"};
        LogF("vrcam: the game's light %d (%s) - sky and fog x%.2f", light, kNames[period], g_set.nightBright.load() + (1.0f - g_set.nightBright.load()) * k);
    }
    return g_set.nightBright.load() + (1.0f - g_set.nightBright.load()) * k;
}

// The game's own height fog (vis json heightFogDensity). Of all the game's areas
// only the act 2 town has it, by day, dawn and dusk (and the character-select
// scenes). Its edge hangs at a height tied to the camera: off-screen from the
// game's camera high above, a hard line across half the picture from ours behind
// the hero - a pale veil over everything above it, moving back as the head looks
// down (2026-10-05). [render] game_height_fog=0 (the default) finds each loaded
// area's definitions in the heap and sets their density to 0; the game's frame
// fog follows on the next frame. =1 puts back what was taken.
// A definition, as floats from the depth fog's colour: colour (3), density,
// plane; the height fog's colour (3), density, plane - 7 floats before it the
// volumetric fog's player radius, 5 before its anisotropy. Game areas have the
// depth plane at 870..900, the menu scenes at 1..30: those are left alone.
namespace gamefog {
struct Patch { float* density; float was; };
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Patch> g_patched;   // what was set to 0, to put back
// [debug] fog_poke: "k=v k=v", fields by their place from the depth fog's colour (as the
// log's "fog def" lines number them, | = 0) set to v in every loaded definition - to
// find which field draws the act 1 caves' veil without restarting. Taken back when changed.
std::vector<float*> g_defs;               // the definitions of the last scan (24 at most)
std::vector<Patch> g_poked;               // what the poke changed, to put back
std::wstring g_pokeWant;                  // under g_lock
std::atomic<uint32_t> g_pokeGen{0};
uint32_t g_pokedGen = ~0u;                // under g_lock
std::atomic<bool> g_busy{false};

// Before it: the volumetric fog's diffuse colour (-17..-15), emissive (-14..-12),
// density grain (-10), height fade (-8), player radius (-7), anisotropy (-5).
// The depth plane from 500: with 100 two transforms in the heap matched as well
// (planes 105 and 121, a 1 where the density sits) - found by a test, 2026-10-05.
bool Looks(const float* f) {
    if (!(f[4] >= 500.0f && f[4] <= 5000.0f && f[9] >= 1.0f && f[9] <= 1000.0f)) return false;
    if (!(f[3] >= 0.0f && f[3] <= 2.0f && f[8] >= 0.0f && f[8] <= 10.0f)) return false;
    for (int k : {0, 1, 2, 5, 6, 7, -17, -16, -15})
        if (!(f[k] >= 0.0f && f[k] <= 1.0f)) return false;
    for (int k : {-14, -13, -12})
        if (!(f[k] >= 0.0f && f[k] <= 100.0f)) return false;
    if (!(f[0] + f[1] + f[2] > 0.3f && f[5] + f[6] + f[7] > 0.3f)) return false;   // no run of zeros
    return f[-7] >= 1.0f && f[-7] <= 1000.0f && f[-5] >= -1.0f && f[-5] <= 1.0f &&
           f[-10] >= 0.0f && f[-10] <= 10.0f && f[-8] >= 0.0f && f[-8] <= 10.0f;
}

// One heap region: each definition with a height fog gets density 0. -1 if the
// region went away under the scan.
// Each definition's floats from -20 to +13 go to `dump` (up to dumpCap), for the log in
// a cave: the act 1 caves have no height fog, yet a veil's hard edge crosses their floor
// at a distance from the hero (2026-10-07) - which field it is, is read from these.
constexpr int kDumpFrom = -20, kDumpN = 34;
int ScanRegion(float* p, size_t n, Patch* out, int cap, int* defs, float (*dump)[kDumpN], float** where, int dumpCap) {
    int got = 0;
    __try {
        for (size_t i = 20; i + 14 < n && got < cap; ++i) {
            if (!(p[i + 4] >= 500.0f && p[i + 4] <= 5000.0f) || !Looks(p + i)) continue;
            if (*defs < dumpCap) { memcpy(dump[*defs], p + i + kDumpFrom, sizeof dump[0]); where[*defs] = p + i; }
            ++*defs;
            if (p[i + 8] > 0.0f) {
                out[got++] = {p + i + 8, p[i + 8]};
                p[i + 8] = 0.0f;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return got;
}

DWORD WINAPI ScanThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    const ULONGLONG t0 = GetTickCount64();
    int defs = 0, taken = 0;
    static float dump[24][kDumpN];
    static float* where[24];
    MEMORY_BASIC_INFORMATION mi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mi, sizeof mi);
         a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
        // guard pages (thread stacks) have PAGE_GUARD in Protect and are skipped with the rest
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE || mi.RegionSize > (1ull << 31))
            continue;
        if (g_set.gameHeightFog.load()) break;   // switched back on meanwhile
        Patch found[32];
        const int n = ScanRegion((float*)mi.BaseAddress, mi.RegionSize / sizeof(float), found, 32, &defs, dump, where, 24);
        if (n <= 0) continue;
        taken += n;
        AcquireSRWLockExclusive(&g_lock);
        g_patched.insert(g_patched.end(), found, found + n);
        ReleaseSRWLockExclusive(&g_lock);
    }
    static int toldDefs = -1;
    if (taken || defs != toldDefs) {
        toldDefs = defs;
        LogF("vrcam: the game's height fog - %d area definitions loaded, %d had it, set to 0 (%.1f s)", defs, taken,
             (GetTickCount64() - t0) / 1000.0);
    }
    AcquireSRWLockExclusive(&g_lock);
    g_defs.assign(where, where + std::min(defs, 24));
    g_pokedGen = ~0u;   // the new ones get [debug] fog_poke too
    ReleaseSRWLockExclusive(&g_lock);
    if (g_ceilBiome.load()) {   // in a cave: every definition's fields, -20 .. +13 from its depth fog's colour
        for (int d = 0; d < std::min(defs, 24); ++d) {
            char b[640];
            int len = snprintf(b, sizeof b, "vrcam: fog def %d:", d);
            for (int k = 0; k < kDumpN && len < (int)sizeof b - 16; ++k)
                len += snprintf(b + len, sizeof b - len, " %s%g", k + kDumpFrom == 0 ? "|" : "", dump[d][k]);
            Log(b);
        }
    }
    g_busy.store(false);
    return 0;
}

bool PutBack(const Patch& p) {
    __try {
        if (*p.density == 0.0f && Looks(p.density - 8)) *p.density = p.was;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PokeOne(float* f, float v, float* was) {
    __try { *was = *f; *f = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ApplyPoke() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_pokedGen != g_pokeGen.load()) {
        g_pokedGen = g_pokeGen.load();
        for (auto it = g_poked.rbegin(); it != g_poked.rend(); ++it) { float x; PokeOne(it->density, it->was, &x); }
        g_poked.clear();
        int set = 0;
        for (const wchar_t* q = g_pokeWant.c_str(); *q;) {
            int k = 0; float v = 0.0f; int used = 0;
            if (swscanf_s(q, L" %d = %f%n", &k, &v, &used) != 2 || used <= 0) break;
            q += used;
            if (k < kDumpFrom || k >= kDumpFrom + kDumpN) continue;
            for (float* d : g_defs) { float was; if (PokeOne(d + k, v, &was)) { g_poked.push_back({d + k, was}); ++set; } }
        }
        if (!g_pokeWant.empty() || set) LogF("vrcam: [debug] fog_poke '%s' - %d fields set", Utf8(g_pokeWant.c_str()).c_str(), set);
    }
    ReleaseSRWLockExclusive(&g_lock);
}
void SetPoke(const std::wstring& w) {
    AcquireSRWLockExclusive(&g_lock);
    const bool changed = w != g_pokeWant;
    g_pokeWant = w;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) g_pokeGen.fetch_add(1);
}

// On the update thread: a scan 1, 4 and 12 s after every biome change (an area's
// definitions are loaded as it is entered), on a thread of its own.
void Tick() {
    ApplyPoke();
    static uint32_t seenGen = ~0u;
    static ULONGLONG due[3] = {};
    const ULONGLONG now = GetTickCount64();
    static bool wasKept = false;
    const bool keep = g_set.gameHeightFog.load();
    if (const uint32_t gen = g_biomeGen.load(); gen != seenGen || (wasKept && !keep)) {
        seenGen = gen;
        due[0] = now + 1000; due[1] = now + 4000; due[2] = now + 12000;
    }
    wasKept = keep;
    if (keep) {
        AcquireSRWLockExclusive(&g_lock);
        if (!g_patched.empty()) {
            int back = 0;
            for (const Patch& p : g_patched) back += PutBack(p) ? 1 : 0;
            LogF("vrcam: the game's height fog back on ([render] game_height_fog=1) - %d definitions restored", back);
            g_patched.clear();
        }
        ReleaseSRWLockExclusive(&g_lock);
        for (ULONGLONG& d : due) d = 0;
        return;
    }
    if (!g_inWorld.load()) return;
    for (ULONGLONG& d : due) {
        if (!d || now < d || g_busy.load()) continue;
        d = 0;
        g_busy.store(true);
        if (HANDLE h = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr)) CloseHandle(h); else g_busy.store(false);
        break;
    }
}
}  // namespace gamefog

// Walls the game makes see-through (2026-10-07). From its own camera high above, a wall
// between it and the hero is drawn at half alpha so the hero stays in sight; from our
// camera that is any wall we look at. The alpha is data: transparentWallAlpha, 0.5, in
// data/hd/global/excel/translation_settings.json, loaded into GlobalTranslationSettings - a
// static block in the image (RVA 0x2401498 on this build): highlight -0.3, its characters'
// 0.5, item shift 0, attack fx (bool), wind 0.9, 0.45, monster fade (bool), fade in 0.15,
// out 0.45, monster light 1, transition 0.45, then the wall alpha 0.5. Found by those
// values (another build: scanned), set to 1 per [render] solid_walls: 0 the game's,
// 1 every view but F1 (the default: from above the hero is still wanted in sight - the
// user, 2026-10-07: "see-through only for F1"), 2 in every view.
namespace solidwalls {
std::atomic<uintptr_t> g_alpha{0};   // the wall alpha's address, 0 = not found yet
float g_was = 0.5f;                  // the game's own value

bool Fits(const float* f) {   // the block's values, the bools skipped
    return f[0] == -0.3f && f[1] == 0.5f && f[2] == 0.0f && f[4] == 0.9f && f[5] == 0.45f && f[7] == 0.15f &&
           f[8] == 0.45f && f[10] == 0.45f && f[11] > 0.0f && f[11] <= 1.0f;
}

void Find() {
    if (g_alpha.load() || !g_base) return;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 5000) return;
    last = GetTickCount64();
    float f[12];
    if (env::SafeRead((void*)(g_base + 0x2401498), f, sizeof f) && Fits(f)) {
        g_was = f[11];
        g_alpha.store(g_base + 0x2401498 + 11 * 4);
        LogF("vrcam: the game's see-through wall alpha found (%.2f) at +0x24014C4", g_was);
        return;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!env::SafeRead((void*)g_base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    if (!env::SafeRead((void*)(g_base + dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return;
    constexpr size_t kChunk = 0x10000;
    static float buf[kChunk / 4 + 12];
    const uintptr_t end = g_base + nt.OptionalHeader.SizeOfImage;
    for (uintptr_t a = g_base + 0x1000; a < end; a += kChunk) {
        const size_t n = (size_t)std::min<uintptr_t>(kChunk + 48, end - a);
        if (!env::PageReadable(a) || !env::SafeRead((void*)a, buf, n)) continue;
        for (size_t i = 0; i + 12 <= n / 4; ++i) {
            if (buf[i] != -0.3f || !Fits(buf + i)) continue;
            g_was = buf[i + 11];
            g_alpha.store(a + (i + 11) * 4);
            LogF("vrcam: the game's see-through wall alpha found (%.2f) at +0x%llX", g_was, (unsigned long long)(a + (i + 11) * 4 - g_base));
            return;
        }
    }
    static bool told = false;
    if (!told) { told = true; Log("vrcam: the game's see-through wall alpha NOT found (another build?) - walls fade as the game has them"); }
}

// On the update thread: the alpha the view wants, written when it changes.
void Tick() {
    Find();
    const uintptr_t a = g_alpha.load();
    if (!a) return;
    const int mode = g_set.solidWalls.load();
    const bool inside = g_enabled.load() && !TopPersp();   // every view but F1 (the game's camera, or ours from above): the floor too
    const float want = mode == 2 || (mode == 1 && inside) ? 1.0f : g_was;
    float now = 0.0f;
    if (!env::SafeRead((void*)a, &now, sizeof now) || now == want) return;
    __try { *(volatile float*)a = want; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    LogF("vrcam: walls %s (alpha %.2f)", want >= 1.0f ? "solid" : "see-through as the game has them", want);
}
}  // namespace solidwalls

// Torches from the game itself (2026-10-07): the fire found in the picture took the depth
// of the wall behind the flame, and its light jumped about as the hero walked. Objects
// near the hero, walked to the classic D2 way - the hero's path (+0x38), its room
// (+0x20), the rooms near it (+0x00, count +0x40), each room's units (+0xA8, next
// +0x158) - the offsets MapAssist reads in D2R, confirmed on this build by a probe: the
// hero's place on the path (+2/+6, subtiles, fraction below) times 2 is vrcam's look-at,
// x to x and y to z; an object's (static path +0x10/+0x14) found a tiki torch where one
// stood. What gives light and how far is the game's Objects table (gamestate::ObjectLight).
// The hero unit is the one the attack hook last worked on: hit once in an area.
namespace fx { bool CeilWanted(); }
namespace worldobj {
template <class T> bool Rd(uintptr_t a, T* out) { return a > 0x10000 && env::SafeRead((const void*)a, out, sizeof(T)); }

struct Light { float x, y, z, lit, rgb[3]; bool monster = false; };   // world point (vrcam's, absolute); Objects.txt Lit; colour; a monster's
constexpr int kMaxLights = 16;
SRWLOCK g_lock = SRWLOCK_INIT;
Light g_lights[kMaxLights];
int g_count = 0;
std::atomic<bool> g_known{false};   // the walk worked last time: the shader takes these, not the picture's fire

// Most torches in D2R's caves are not objects at all: the HD scenery's props, whose light
// only the renderer knows. Its lists of light points were found in the heap (2026-10-07,
// read from outside while standing at the torches): runs of world points (x, y, z) 12
// bytes apart, all at one height (6.533 for the caves' torches, 6.5 the object torch's),
// metres apart - the object torches among them. Found again every 3 s on a thread of its
// own, as the game's height fog is: runs of 3 to 64 such points within 400 units of the
// hero, 2 to 30 units over his floor (the crypts' wall torches hang at ~12.4), at least 6 apart;
// mesh vertices lie closer.
constexpr int kScanMax = 128;
SRWLOCK g_scanLock = SRWLOCK_INIT;
float g_scanPts[kScanMax][3];
int g_scanCount = 0;
// Found in 2 scans at the same place or more: a torch that stands. The Fallen's torches
// are in the renderer's lists too, and each place they ran through was kept 30 s - a
// trail of lights lit the ceiling over half the cave (2026-10-07); only what stands lights it.
bool g_scanStill[kScanMax] = {};
std::atomic<bool> g_scanBusy{false};
struct ScanArgs { float hero[3]; bool full; };
// Where the last full scan found lights: 64 KB windows of the heap (2026-10-08). Reading
// all the game's memory (~5.7 GB) every 1.5 s cost a third of a core and was suspected
// of VR dropouts on weak PCs (0.143 turned the torches off for it). Now the whole heap
// is read only after an area change (1, 4 and 12 s) and every 20 s; in between only
// these windows - the renderer rewrites its light lists in place.
constexpr uintptr_t kScanWin = 0x10000;
constexpr int kScanWinMax = 256;
uintptr_t g_scanWin[kScanWinMax];   // window starts, written and read only on the scan thread
int g_scanWinCount = 0;

// One heap region; -1 if it went away under the scan.
int ScanLightRuns(const float* p, size_t n, const float hero[3], float (*out)[3], int cap, const float** at = nullptr) {
    int got = 0;
    __try {
        for (size_t i = 0; i + 3 * 3 <= n && got < cap; ++i) {
            const float y = p[i + 1];
            if (!(y > hero[1] + 2.0f && y < hero[1] + 30.0f) || !std::isfinite(p[i]) || !std::isfinite(p[i + 2])) continue;
            // the run starting here: same height, inside the box, apart from the one before
            size_t j = i, len = 0;
            while (j + 3 <= n && len < 64) {
                const float x = p[j], yy = p[j + 1], z = p[j + 2];
                // asked the way round that a NaN fails: junk in a passing buffer held NaNs, and
                // one NaN among the lights blanked the whole ceiling (2026-10-07)
                if (!(fabsf(yy - y) <= 0.05f && fabsf(x - hero[0]) <= 400.0f && fabsf(z - hero[2]) <= 400.0f)) break;
                if (len > 0) {
                    const float dx = x - p[j - 3], dz = z - p[j - 1];
                    if (dx * dx + dz * dz < 36.0f) break;
                }
                ++len; j += 3;
            }
            if (len < 3 || len >= 64) continue;
            for (size_t k = i; k < j && got < cap; k += 3) {
                out[got][0] = p[k]; out[got][1] = p[k + 1]; out[got][2] = p[k + 2];
                if (at) at[got] = p + k;
                ++got;
            }
            i = j - 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return got;
}

DWORD WINAPI ScanThread(void* arg) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    ScanArgs a = *(ScanArgs*)arg;
    delete (ScanArgs*)arg;
    const LONGLONG t0 = QpcUs();
    static float pts[1024][3];
    static const float* at[1024];
    int count = 0;
    size_t read = 0;
    MEMORY_BASIC_INFORMATION mi{};
    auto usable = [&]() {
        return mi.State == MEM_COMMIT && mi.Type == MEM_PRIVATE && mi.Protect == PAGE_READWRITE && mi.RegionSize <= (1ull << 31);
    };
    if (a.full) {
        for (uintptr_t m = 0x10000; m < 0x7FFFFFFF0000ull && VirtualQuery((void*)m, &mi, sizeof mi) && count < 1024;
             m = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
            if (!usable()) continue;
            read += mi.RegionSize;
            const int got = ScanLightRuns((const float*)mi.BaseAddress, mi.RegionSize / sizeof(float), a.hero, pts + count, 1024 - count, at + count);
            if (got > 0) count += got;
        }
    } else {
        for (int w = 0; w < g_scanWinCount && count < 1024; ++w) {
            if (!VirtualQuery((void*)g_scanWin[w], &mi, sizeof mi) || !usable()) continue;
            // the window and 1 KB past it (a run across its end), never past the region
            const uintptr_t end = std::min(g_scanWin[w] + kScanWin + 0x400, (uintptr_t)mi.BaseAddress + mi.RegionSize);
            if (end <= g_scanWin[w]) continue;
            read += end - g_scanWin[w];
            const int got = ScanLightRuns((const float*)g_scanWin[w], (end - g_scanWin[w]) / sizeof(float), a.hero, pts + count, 1024 - count, at + count);
            if (got > 0) count += got;
        }
    }
    if (a.full) {   // the windows the next scans read: one per place a point was found
        int nw = 0;
        for (int i = 0; i < count && nw < kScanWinMax; ++i) {
            const uintptr_t w = (uintptr_t)at[i] & ~(kScanWin - 1);
            int k = 0;
            while (k < nw && g_scanWin[k] != w) ++k;
            if (k == nw) g_scanWin[nw++] = w;
        }
        g_scanWinCount = nw;
    }
    // The same light is in several of the renderer's lists (6 and more were seen for each
    // torch): one point each (within 2 units), and only what is in 3 lists or more - junk
    // in a passing buffer that happened to fit the rule is in one, and came and went: 5
    // lights one scan, 76 the next (2026-10-07).
    int unique = 0;
    static float u[1024][3];
    static int seen[1024];
    for (int i = 0; i < count; ++i) {
        int k = 0;
        while (k < unique && !(fabsf(u[k][0] - pts[i][0]) < 2.0f && fabsf(u[k][2] - pts[i][2]) < 2.0f)) ++k;
        if (k < unique) { ++seen[k]; continue; }
        memcpy(u[unique], pts[i], sizeof u[0]); seen[unique] = 1; ++unique;
    }
    int kept = 0;
    for (int k = 0; k < unique && kept < kScanMax; ++k)
        if (seen[k] >= 3) { memcpy(u[kept], u[k], sizeof u[0]); ++kept; }
    unique = kept;
    // Kept: a torch stands still, and a scan that read the lists while the renderer was
    // rewriting them found it in fewer than 3 - the light came on 10 s late and went off
    // again (2026-10-07). What a scan finds is (re)stamped; what none has found for 30 s,
    // or what is 400 units off now, goes.
    static ULONGLONG stamp[kScanMax];
    static int hits[kScanMax];
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_scanLock);
    int n = g_scanCount;
    for (int i = 0; i < unique; ++i) {
        int k = 0;
        while (k < n && !(fabsf(g_scanPts[k][0] - u[i][0]) < 2.0f && fabsf(g_scanPts[k][2] - u[i][2]) < 2.0f)) ++k;
        if (k == n && n < kScanMax) { ++n; hits[k] = 0; }
        if (k < n) { memcpy(g_scanPts[k], u[i], sizeof u[0]); stamp[k] = now; hits[k] = std::min(hits[k] + 1, 1000); }
    }
    int keep = 0, still = 0;
    for (int k = 0; k < n; ++k) {
        const bool close = fabsf(g_scanPts[k][0] - a.hero[0]) <= 400.0f && fabsf(g_scanPts[k][2] - a.hero[2]) <= 400.0f;
        if (now - stamp[k] > 30000 || !close) continue;
        memcpy(g_scanPts[keep], g_scanPts[k], sizeof g_scanPts[0]); stamp[keep] = stamp[k]; hits[keep] = hits[k];
        g_scanStill[keep] = hits[k] >= 2;
        still += g_scanStill[keep] ? 1 : 0;
        ++keep;
    }
    g_scanCount = keep;
    ReleaseSRWLockExclusive(&g_scanLock);
    unique = still;
    static int told = -1;
    static ULONGLONG toldAt = 0;
    const double ms = (QpcUs() - t0) / 1000.0;
    // every full scan, and a change; the narrow ones once a minute besides
    if (unique != told || a.full || now - toldAt > 60000) {
        told = unique; toldAt = now;
        LogF("worldobj: the renderer's lights - %d standing near the hero (%d points in its lists; %s scan, %.0f MB in %.1f ms, %d windows)",
             unique, count, a.full ? "full" : "narrow", read / 1048576.0, ms, g_scanWinCount);
    }
    g_scanBusy.store(false);
    return 0;
}

// Every 100 ms while the ceiling is drawn: the 16 nearest lights - the renderer's (found
// by the scan, wherever the hero is: his place is the look-at) and the lit objects in
// the rooms near the hero, once his unit is known (the attack hook's, or the one the
// facing hook found: no need to hit anything).
void Gather() {
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 100) return;
    last = GetTickCount64();
    // [ceiling] torches=0: no lights wanted, so no scan of the game's memory either - on a
    // weak PC its read of all the game's heap every 1.5 s was suspected of VR dropouts (0.142)
    const bool want = fx::CeilWanted() && g_set.ceilTorches.load();
    g_heroWanted.store(want);
    if (!want) { g_known.store(false); return; }
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    const float heroX = L[0], heroY = L[1], heroZ = L[2];   // his feet
    static ULONGLONG lastScan = 0, lastFull = 0;
    static ULONGLONG fullDue[3] = {};
    static uint32_t scanGen = ~0u;
    const ULONGLONG tick = GetTickCount64();
    if (const uint32_t gen = g_biomeGen.load(); gen != scanGen) {   // a new area: its lights, 1, 4 and 12 s in
        scanGen = gen;
        fullDue[0] = tick + 1000; fullDue[1] = tick + 4000; fullDue[2] = tick + 12000;
    }
    if (tick - lastScan > 1500 && !g_scanBusy.exchange(true)) {
        lastScan = tick;
        bool full = tick - lastFull > 20000;
        for (ULONGLONG& d : fullDue)
            if (d && tick >= d) { d = 0; full = true; }
        if (full) lastFull = tick;
        ScanArgs* a = new ScanArgs{{heroX, heroY, heroZ}, full};
        if (HANDLE th = CreateThread(nullptr, 0, ScanThread, a, 0, nullptr)) CloseHandle(th);
        else { delete a; g_scanBusy.store(false); }
    }
    Light found[64 + kScanMax];
    float dist[64 + kScanMax];
    int n = 0, objects = 0;
    uint32_t nNear = 0;
    // what the objects are, for the log: in a crypt with a lit tripod in view none gave light (2026-10-07)
    char seen[170] = "";   // LogF holds 256
    size_t seenLen = 0;
    // the objects: the attack hook's hero, else the facing hook's - checked again, it may be stale
    uintptr_t hero = (uintptr_t)g_heroUnit.load();
    if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
    if (hero && !IsHeroUnit((void*)hero)) { g_heroSeen.store(nullptr); hero = 0; }
    if (!hero) {   // the SDK's handle of the local player, if it is the unit itself (tried, never trusted)
        const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();
        if (p > 0x10000 && IsHeroUnit((void*)p)) { hero = p; g_heroSeen.store((void*)p); }
    }
    uintptr_t path = 0, room = 0, nearRooms = 0;
    const bool monsters = g_set.ceilMonsters.load() && g_set.ceilMonsterBright.load() > 0.0f;
    if (hero && Rd(hero + 0x38, &path) && Rd(path + 0x20, &room) && Rd(room, &nearRooms) && Rd(room + 0x40, &nNear) &&
        nearRooms && nNear > 0 && nNear <= 64) {
        // The rooms near the hero's and the rooms near those: a crypt's rooms are small, and
        // the near ones held 2 objects - its torches stood a room further (2026-10-07).
        uintptr_t rooms[128];
        uint32_t nRooms = 0;
        auto addRoom = [&](uintptr_t rm) {
            if (!rm || nRooms >= 128) return;
            for (uint32_t k = 0; k < nRooms; ++k) if (rooms[k] == rm) return;
            rooms[nRooms++] = rm;
        };
        for (uint32_t r = 0; r < nNear; ++r) { uintptr_t rm = 0; if (Rd(nearRooms + r * 8, &rm)) addRoom(rm); }
        const uint32_t ring1 = nRooms;
        for (uint32_t r = 0; r < ring1; ++r) {
            uintptr_t list = 0;
            uint32_t cnt = 0;
            if (!Rd(rooms[r], &list) || !Rd(rooms[r] + 0x40, &cnt) || !list || cnt > 64) continue;
            for (uint32_t q = 0; q < cnt; ++q) { uintptr_t rm = 0; if (Rd(list + q * 8, &rm)) addRoom(rm); }
        }
        nNear = nRooms;
        for (uint32_t r = 0; r < nRooms; ++r) {
            uintptr_t rm = rooms[r], u = 0;
            if (!Rd(rm + 0xA8, &u)) continue;
            for (int k = 0; u && k < 512; ++k) {
                uint32_t ut = 0, txt = 0, mode = 0;
                uintptr_t up = 0, next = 0;
                Rd(u, &ut); Rd(u + 4, &txt); Rd(u + 0xC, &mode); Rd(u + 0x38, &up); Rd(u + 0x158, &next);
                if (ut == 2 && up) {
                    ++objects;
                    float rgb[3];
                    const int lit = gamestate::ObjectLight(txt, mode, rgb);
                    uint32_t sx = 0, sy = 0;
                    if (seenLen < sizeof seen - 40) {
                        uint32_t ox = 0, oy = 0;
                        Rd(up + 0x10, &ox); Rd(up + 0x14, &oy);
                        const int w = snprintf(seen + seenLen, sizeof seen - seenLen, " #%u mode %u lit %d at %.0f,%.0f;",
                                               txt, mode, lit, 2.0f * ox - heroX, 2.0f * oy - heroZ);
                        if (w > 0) seenLen += (size_t)w;
                    }
                    if (lit > 0 && Rd(up + 0x10, &sx) && Rd(up + 0x14, &sy)) {
                        // the middle of the subtile; nearest first, the farthest dropped when full
                        const Light l{2.0f * sx + 1.0f, heroY + 4.9f, 2.0f * sy + 1.0f, (float)lit, {rgb[0], rgb[1], rgb[2]}};
                        const float d = (l.x - heroX) * (l.x - heroX) + (l.z - heroZ) * (l.z - heroZ);
                        if (n < 64) { found[n] = l; dist[n] = d; ++n; }
                    }
                } else if (ut == 1 && monsters && up && mode != 0 && mode != 12) {
                    // The Fallen carry a torch in D2R and their shamans a staff of fire (the user's
                    // screenshots, 2026-10-07) - none of it in the game's own light data (monstats2
                    // gives the shamans 5, the Fallen nothing): a hand-held torch of our own that walks
                    // with them. MonStats rows (monstats.txt): fallen1-5 19..23, fallen6-8 642..644,
                    // fallenshaman1-5 58..62, fallenshaman6-8 645..647. Dying (0) and dead (12): none.
                    const bool fallen = (txt >= 19 && txt <= 23) || (txt >= 642 && txt <= 644);
                    const bool shaman = (txt >= 58 && txt <= 62) || (txt >= 645 && txt <= 647);
                    uint16_t fx = 0, mx = 0, fz = 0, mz = 0;
                    if ((fallen || shaman) && Rd(up + 0, &fx) && Rd(up + 2, &mx) && Rd(up + 4, &fz) && Rd(up + 6, &mz)) {
                        // smaller than a standing torch's (19), and dimmer: [ceiling] monster_brightness
                        // ("too strong a glow from them", 2026-10-07)
                        const Light l{2.0f * (mx + fx / 65536.0f), heroY + 3.5f, 2.0f * (mz + fz / 65536.0f), shaman ? 10.0f : 8.0f,
                                      {1.0f, 236.0f / 255.0f, 176.0f / 255.0f}, true};
                        const float d = (l.x - heroX) * (l.x - heroX) + (l.z - heroZ) * (l.z - heroZ);
                        if (n < 64) { found[n] = l; dist[n] = d; ++n; }
                    }
                }
                u = next;
            }
        }
    }
    {   // the renderer's lights; where one is an object's too, the object's own (its colour, its reach)
        float sp[kScanMax][3];
        bool st[kScanMax];
        AcquireSRWLockShared(&g_scanLock);
        const int m = g_scanCount;
        memcpy(sp, g_scanPts, m * sizeof sp[0]);
        memcpy(st, g_scanStill, m * sizeof st[0]);
        ReleaseSRWLockShared(&g_scanLock);
        const int objs = n;
        for (int i = 0; i < m; ++i) {
            if (!st[i]) continue;   // seen once: may be a monster's torch running past
            bool dup = false;
            for (int k = 0; k < objs && !dup; ++k) dup = fabsf(found[k].x - sp[i][0]) < 4.0f && fabsf(found[k].z - sp[i][2]) < 4.0f;
            if (dup) continue;
            found[n] = Light{sp[i][0], sp[i][1], sp[i][2], 19.0f, {1.0f, 236.0f / 255.0f, 176.0f / 255.0f}};   // a torch's
            dist[n] = (sp[i][0] - heroX) * (sp[i][0] - heroX) + (sp[i][2] - heroZ) * (sp[i][2] - heroZ);
            ++n;
        }
    }
    for (int i = 1; i < n; ++i)   // a few dozen at most: insertion sort by distance
        for (int j = i; j > 0 && dist[j] < dist[j - 1]; --j) { std::swap(dist[j], dist[j - 1]); std::swap(found[j], found[j - 1]); }
    const int keep = std::min(n, kMaxLights);
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_lights, found, keep * sizeof(Light));
    g_count = keep;
    ReleaseSRWLockExclusive(&g_lock);
    g_known.store(true);
    static int told = -1, toldObjects = -1;
    static bool toldHero = false;
    if (keep != told || objects != toldObjects || (hero != 0) != toldHero) {
        told = keep; toldObjects = objects; toldHero = hero != 0;
        LogF("worldobj: %d lights near the hero light the ceiling (%d objects in %u rooms%s)", keep, objects, nNear,
             hero ? "" : "; the hero unit not found yet - the renderer's lights only");
        if (objects) LogF("worldobj: objects (Objects.txt row, mode, light radius, place from the hero):%s", seen);
    }
}

int Lights(Light* out) {
    AcquireSRWLockShared(&g_lock);
    const int n = g_count;
    memcpy(out, g_lights, n * sizeof(Light));
    ReleaseSRWLockShared(&g_lock);
    return n;
}
}  // namespace worldobj

void SkyTick() {
    env::Find();
    solidwalls::Tick();
    worldobj::Gather();
    gamefog::Tick();
    static uint32_t seenGen = ~0u;
    static std::string biome;
    static int palette = -1;
    if (const uint32_t gen = g_biomeGen.load(); gen != seenGen) {
        seenGen = gen;
        AcquireSRWLockShared(&g_biomeLock); biome = g_biome; ReleaseSRWLockShared(&g_biomeLock);
        palette = SkyPaletteFor(biome, g_set.skyAlways.load());
        // Not a world: the main menu (act2_frontend_biome) and the menu scenes of
        // their own - character creation is 'ui_characterselectscreen' (2026-10-08:
        // taken for a world it got the first person's stereo, HUD shift and hidden heads).
        const bool world = !biome.empty() && biome.find("frontend") == std::string::npos &&
                           biome.rfind("ui_", 0) != 0;
        g_inWorld.store(world);
        g_fogAct.store(world ? ActOfBiome(biome) : 0);
        g_underground.store(world && !IsOutdoorBiome(biome));
        g_ceilBiome.store(world && IsCeilingBiome(biome));
        static std::string told = "?";   // no biome name has a question mark
        if (biome != told) {
            told = biome;
            LogF("vrcam: biome '%s' - %s%s", biome.empty() ? "(none yet)" : biome.c_str(),
                 palette >= 0 ? kPalettes[palette].part : "no sky here (not in [sky] outdoor)",
                 g_ceilBiome.load() ? ", a cave ceiling ([ceiling] biomes)" : "");
        }
    }
    {   // the cave ceiling: each change of mind to the log, like the sky's
        const char* cw = nullptr;
        if (!g_set.ceilOn.load()) cw = "[ceiling] enabled=0";
        else if (!g_ceilBiome.load()) cw = "not on [ceiling] biomes";
        else if (!g_enabled.load() || ThirdPerson()) cw = "not first person";
        else if (gamestate::MenuOpen()) cw = "a menu is open";
        static std::string toldCeil = "?";
        if (std::string(cw ? cw : "") != toldCeil) {
            toldCeil = cw ? cw : "";
            if (cw) LogF("vrcam: cave ceiling off - %s", cw);
            else {
                float height, bright, light;
                CeilingNow(&height, &bright, &light);
                LogF("vrcam: cave ceiling on - %.0f units over the hero's floor, brightness %.2f, light reach %.0f", height, bright, light);
            }
        }
    }
    const char* why = nullptr;
    if (!g_set.skyOn.load()) why = "[sky] enabled=0";
    else if (!g_enabled.load()) why = "the VR camera is off (F12)";
    else if (TableView()) why = "the table view (the room round the game)";
    else if (TopPersp()) why = "the view from above";
    else if (palette < 0) why = "not outdoors";
    else if (gamestate::MenuOpen()) why = "a menu is open";
    g_skyPalette.store(why ? -1 : palette);
    static std::string toldWhy = "?";
    if (std::string(why ? why : "") != toldWhy) {
        toldWhy = why ? why : "";
        if (why) LogF("vrcam: sky off - %s", why);
        else LogF("vrcam: sky on - %s palette", kPalettes[palette].part);
    }
}

// Fog: D2R_DepthFog.fx runs inside ReShade, which this plugin joins as an
// add-on so the ini (and the settings window) can switch it and set its range.
// ReShade's own UI for the effect stays usable; what we write wins each frame.
namespace fx {
bool g_registered = false;
// The effect's two techniques: [0] the left eye's frames (and mono), [1] the
// right eye's. The eye of a frame is which one is on, not a uniform: under
// D3D12 ReShade rewrites the effect's one constant buffer in place at every
// present, and with a pair per game frame the right eye's present wrote it
// before the GPU had run the left eye's effect - the left eye drew the right
// eye's sky. A technique's state is read when its commands are recorded.
reshade::api::effect_technique g_tech[2]{};
// The eye of the frame presented last (OnBeginEffects); the next is the other.
int g_fxEye = 0;

// DLSS with real stereo (2026-10-08): DLSS builds each picture out of the ones
// before it, and with the eyes by turns the one before is the OTHER eye's - its
// silhouettes showed through the other eye (with real stereo off DLSS looked
// right). So each eye gets a DLSS instance of its own: the driver's NGX
// (_nvngx.dll, which the game's own NGX library calls by name) is hooked, a
// second SuperSampling feature is made beside the game's, and the right eye's
// frames are evaluated on it. The motion vectors are still the game's, made
// against the previous frame - the other eye's (see the plan, next step).
namespace dlsseyes {
struct NgxHandle { unsigned int Id; };
using CreateFn = int(__cdecl*)(void* cmdList, int feature, void* params, NgxHandle** out);
using EvalFn = int(__cdecl*)(void* cmdList, const NgxHandle* h, const void* params, void* callback);
using ReleaseFn = int(__cdecl*)(NgxHandle* h);
CreateFn OrigCreate = nullptr;
EvalFn OrigEval = nullptr;
ReleaseFn OrigRelease = nullptr;
constexpr int kSuperSampling = 1;   // NVSDK_NGX_Feature_SuperSampling
SRWLOCK g_lock = SRWLOCK_INIT;
struct Twin { const NgxHandle* game; NgxHandle* twin; };
std::vector<Twin> g_twins;
std::vector<const NgxHandle*> g_noTwin;   // a twin was tried for these and failed: not again
bool Ok(int r) { return (r & 0xFFF00000) != 0xBAD00000; }

NgxHandle* TwinOf(const NgxHandle* h) {
    AcquireSRWLockShared(&g_lock);
    NgxHandle* t = nullptr;
    for (const Twin& x : g_twins) if (x.game == h) { t = x.twin; break; }
    ReleaseSRWLockShared(&g_lock);
    return t;
}
bool TriedBefore(const NgxHandle* h) {
    AcquireSRWLockShared(&g_lock);
    const bool tried = std::find(g_noTwin.begin(), g_noTwin.end(), h) != g_noTwin.end();
    ReleaseSRWLockShared(&g_lock);
    return tried;
}
// A second instance from the same parameters: at the game's own creation, or -
// for a feature made before the hook was in - from the evaluation's parameters
// (the game keeps one parameter set, creation keys and all).
NgxHandle* MakeTwin(void* cmdList, const NgxHandle* game, void* params, const char* when) {
    NgxHandle* twin = nullptr;
    const int r = OrigCreate(cmdList, kSuperSampling, params, &twin);
    AcquireSRWLockExclusive(&g_lock);
    if (Ok(r) && twin) g_twins.push_back({game, twin});
    else { g_noTwin.push_back(game); twin = nullptr; }
    ReleaseSRWLockExclusive(&g_lock);
    if (twin) LogF("vrcam: DLSS - a second instance for the right eye (%s): game's %u, the eye's %u", when, game->Id, twin->Id);
    else LogF("vrcam: DLSS - the right eye's instance could not be made (%s, 0x%08X): both eyes share the game's", when, (unsigned)r);
    return twin;
}

int __cdecl HookCreate(void* cmdList, int feature, void* params, NgxHandle** out) {
    const int r = OrigCreate(cmdList, feature, params, out);
    if (feature == kSuperSampling && Ok(r) && out && *out && g_set.dlssPerEye.load())
        MakeTwin(cmdList, *out, params, "with the game's");
    return r;
}
int __cdecl HookEval(void* cmdList, const NgxHandle* h, const void* params, void* callback) {
    if (h && g_set.dlssPerEye.load() && AfrOn()) {
        // the eye this frame draws: a pair per game frame sets it for each pass;
        // by turns, the one after the eye presented last
        const int eye = PairWanted() ? (g_eye.load() & 1) : (g_fxEye ^ 1);
        if (eye == 0 && replay::DlssBoth()) {   // the left pass of a replay: both instances, predicated
            NgxHandle* twin = TwinOf(h);
            if (!twin && !TriedBefore(h)) twin = MakeTwin(cmdList, h, const_cast<void*>(params), "for the replay");
            if (twin) {
                auto* cl = (ID3D12GraphicsCommandList*)cmdList;
                cl->SetPredication(replay::g_predBuf, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, 0) : nullptr;
                const int r = OrigEval(cmdList, h, params, callback);
                dlssmv::AfterEvaluate(params, restore);
                cl->SetPredication(replay::g_predBuf, 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
                OrigEval(cmdList, twin, params, callback);
                cl->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
                if (replay::g_predEvals.fetch_add(1) == 0) Log("vrcam: DLSS - the replay's left pass carries both eyes' evaluations (predicated)");
                return r;
            }
        }
        if (eye == 1) {
            NgxHandle* twin = TwinOf(h);
            if (!twin && !TriedBefore(h)) twin = MakeTwin(cmdList, h, const_cast<void*>(params), "at the first evaluation");
            if (twin) {
                void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, eye) : nullptr;
                const int r = OrigEval(cmdList, twin, params, callback);
                dlssmv::AfterEvaluate(params, restore);
                return r;
            }
        }
        if (TwinOf(h)) {   // the left eye on the game's own instance
            void* restore = g_inWorld.load() ? dlssmv::BeforeEvaluate(cmdList, params, eye) : nullptr;
            const int r = OrigEval(cmdList, h, params, callback);
            dlssmv::AfterEvaluate(params, restore);
            return r;
        }
    }
    return OrigEval(cmdList, h, params, callback);
}
int __cdecl HookRelease(NgxHandle* h) {
    NgxHandle* twin = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    for (size_t i = 0; i < g_twins.size(); ++i)
        if (g_twins[i].game == h) { twin = g_twins[i].twin; g_twins.erase(g_twins.begin() + i); break; }
    g_noTwin.erase(std::remove(g_noTwin.begin(), g_noTwin.end(), h), g_noTwin.end());
    ReleaseSRWLockExclusive(&g_lock);
    if (twin) OrigRelease(twin);
    return OrigRelease(h);
}

// Once the driver's NGX is in the process (the game loads it when it starts NGX).
void Install() {
    static bool done = false;
    if (done) return;
    HMODULE m = GetModuleHandleW(L"_nvngx.dll");
    if (!m) return;
    done = true;
    void* c = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_CreateFeature");
    void* e = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_EvaluateFeature");
    void* r = (void*)GetProcAddress(m, "NVSDK_NGX_D3D12_ReleaseFeature");
    const bool ok = c && e && r &&
                    MH_CreateHook(c, (void*)&HookCreate, (void**)&OrigCreate) == MH_OK &&
                    MH_CreateHook(e, (void*)&HookEval, (void**)&OrigEval) == MH_OK &&
                    MH_CreateHook(r, (void*)&HookRelease, (void**)&OrigRelease) == MH_OK &&
                    MH_EnableHook(c) == MH_OK && MH_EnableHook(e) == MH_OK && MH_EnableHook(r) == MH_OK;
    LogF("vrcam: DLSS hooks %s (_nvngx.dll) - with real stereo each eye gets its own DLSS instance", ok ? "in" : "NOT in");
}
}  // namespace dlsseyes

// The views of the toolbar's and the map's copies bound to the effect (hud::PictureNow), 0 = none yet.
uint64_t g_pieceBound[3] = {};   // the toolbar, the map, the labels' layer

void Forget(reshade::api::effect_runtime*) { g_tech[0] = g_tech[1] = {0}; replay::g_rightTech = {0}; g_pieceBound[0] = g_pieceBound[1] = g_pieceBound[2] = 0; }

void SetFloats(reshade::api::effect_runtime* rt, const char* name, const float* v, size_t n) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_float(u, v, n);
}
void SetFloat(reshade::api::effect_runtime* rt, const char* name, float v) { SetFloats(rt, name, &v, 1); }
void SetBool(reshade::api::effect_runtime* rt, const char* name, bool v) {
    const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", name);
    if (u.handle) rt->set_uniform_value_bool(u, &v, 1);
}

// Not in the main menu: its scene got the fog of act 2 (its biome is act2_frontend_biome).
// The HUD's per-eye shift (D2R_DepthFog.fx PS_Hud): only in AFR stereo, in a game area.
bool HudWanted() { return AfrOn() && g_set.uiShift.load() > 0.0f && g_inWorld.load(); }
// The crossbow held as the staff, its shot line ([debug] phantom_ray) or its bone's axes ([debug] bone_axes) drawn.
#if D2RVR_FIRST_PERSON
bool XbowDrawable() { return g_enabled.load() && g_inWorld.load() && XbowLikeStaff() && StaffAim(); }
// the line the attack goes along, for any weapon in hand (only where HandRay has one)
bool PhantomWanted() { return (g_set.phantomRay.load() || g_set.boneAxes.load()) && g_enabled.load() && g_inWorld.load(); }
// the axes of whatever weapon is in hand, not only the crossbow's (2026-10-06)
bool BoneAxesWanted() { return g_set.boneAxes.load() && g_enabled.load() && g_inWorld.load(); }
#else
bool PhantomWanted() { return false; }
bool BoneAxesWanted() { return false; }
#endif

// The frame stamp strip (D2R_DepthFog.fx PS_Stamp): whenever the camera turns
// with the head, so that FlatVR can place its screen at the pose of the frame.
// [stereo] pipeline_depth where the engine pipelines: one picture a game frame (mono, the
// depth, eyes by turns). A pair from one game frame presents each eye from inside its own
// pass, the view just built: 0 there.
int PipeDepth() { return PairWanted() ? 0 : g_set.pipelineDepth.load(); }
SkyView SkyBack(int e) {   // under g_skyLock
    const int d = PipeDepth();
    if (d <= 0) return g_skyView[e];
    const SkyView& h = g_skyHist[e][(g_skyHistAt[e] - d) & 3];
    return h.axes_ok && h.proj_ok ? h : g_skyView[e];
}
bool StampWanted() { return g_enabled.load() && g_afrBlock && g_set.stamps.load() && g_set.stampPixels.load(); }

bool FogWanted() {
    return g_set.fogOn.load() && g_enabled.load() && !Overhead() && g_inWorld.load() && !gamestate::MenuOpen() &&
           (g_set.fogCaves.load() || !g_underground.load());   // [fog] caves=0: none underground
}
// The table (F5): the void black, the game lifted off black - D2R_DepthFog.fx TableKey.
bool TableKeyWanted() { return TableView() && g_inWorld.load() && !gamestate::MenuOpen(); }
// The cave ceiling (D2R_DepthFog.fx Ceiling): from inside only - VR F4 and flat F3,
// never from above, from behind or on the floor (all three run as ThirdPerson).
bool CeilWanted() {
    return g_set.ceilOn.load() && g_enabled.load() && !ThirdPerson() && g_inWorld.load() && g_ceilBiome.load() &&
           !gamestate::MenuOpen();
}

// The pictures of each act as the shader's preprocessor definitions; only what
// differs is set, since every change recompiles the effect. True if it did.
// Done here, after the effect pass, like the technique switch below.
bool ApplySkyPictures(reshade::api::effect_runtime* rt) {
    static uint32_t applied = ~0u;
    const uint32_t gen = g_skyCfgGen.load();
    if (gen == applied) return false;
    applied = gen;
    bool set = false;
    for (int a = 1; a <= 6; ++a) {
        AcquireSRWLockShared(&g_skyCfgLock); const SkyAct act = g_skyAct[a]; ReleaseSRWLockShared(&g_skyCfgLock);
        for (int k = 0; k < 4; ++k) {   // the day band and cap, then the night ones
            char name[32];
            static const char* const kDef[4] = {"D2R_SKY_%s", "D2R_SKYCAP_%s", "D2R_SKYN_%s", "D2R_SKYCAPN_%s"};
            snprintf(name, sizeof name, kDef[k], kActDefs[a]);
            const bool ok = k == 0 ? act.bandOk : k == 1 ? act.capOk : act.nightOk;
            const std::string& file = k == 0 ? act.band : k == 1 ? act.cap : k == 2 ? act.nightBand : act.nightCap;
            // a slot without a picture keeps the act 1 file: never sampled, nothing to miss
            const std::string want = "\"" + (ok ? file : std::string(k & 1 ? "D2R_Sky_ours/D2R_SkyCap_act1.png" : "D2R_Sky_ours/D2R_Sky_act1.png")) + "\"";
            char have[1024] = {};
            size_t size = sizeof have;
            if (rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, have, &size) && want == have) continue;
            rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, want.c_str());
            LogF("vrcam: sky picture %s = %s", name, want.c_str());
            set = true;
        }
    }
    {   // the cave ceiling's pictures, a slot each; an empty slot keeps one that is there (never sampled)
        std::string file[kCeilSlots + 1];
        bool ok[kCeilSlots + 1] = {};
        AcquireSRWLockShared(&g_skyCfgLock);
        for (int k = 1; k <= kCeilSlots; ++k) { file[k] = g_ceilSlot[k]; ok[k] = g_ceilSlotOk[k]; }
        ReleaseSRWLockShared(&g_skyCfgLock);
        const std::string spare = ok[1] ? file[1] : std::string("D2R_Sky_ours/D2R_Ceiling_act1_caves_walls.png");
        for (int k = 1; k <= kCeilSlots; ++k) {
            char name[24];
            snprintf(name, sizeof name, "D2R_CEILING_%d", k);
            const std::string want = "\"" + (ok[k] ? file[k] : spare) + "\"";
            char have[1024] = {};
            size_t size = sizeof have;
            if (rt->get_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, have, &size) && want == have) continue;
            rt->set_preprocessor_definition_for_effect("D2R_DepthFog.fx", name, want.c_str());
            LogF("vrcam: cave ceiling picture %s = %s", name, want.c_str());
            set = true;
        }
    }
    if (set) Forget(rt);
    return set;
}

// Switching the technique happens after the effect pass, never inside it:
// turned on from reshade_begin_effects, ReShade went on to draw an effect whose
// resources it had not created yet, and the game died in D3D12 (null+0x19C).
// From here it takes effect on the next frame, with everything in place.
void OnFinishEffects(reshade::api::effect_runtime* rt, reshade::api::command_list*, reshade::api::resource_view, reshade::api::resource_view) {
    if (const double b = pairtime::g_effBegin.exchange(0.0); b > 0.0) pairtime::g_effSum.store(pairtime::g_effSum.load() + pairtime::UsNow() - b);
    if (ApplySkyPictures(rt)) return;   // the effect reloads with the new pictures
    if (!g_tech[0].handle) g_tech[0] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog");
    if (!g_tech[1].handle) g_tech[1] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog_R");
    replay::g_rightTech = g_tech[1];
    if (!g_tech[0].handle) { hud::SetPictureReady(false); return; }
    // Off while a menu is open: the fog lies over the whole picture, the
    // inventory and trade panels included, and darkened them.
    const bool want = FogWanted() || g_skyPalette.load() >= 0 || CeilWanted() || HudWanted() || StampWanted() || hud::PictureWanted() || TableKeyWanted() || PhantomWanted() || BoneAxesWanted();
    // The toolbar and the map are taken out of the game's picture to be drawn
    // back at their own size only while this effect runs, and is new enough to.
    {
        const bool effects = rt->get_effects_state(), fresh = rt->find_uniform_variable("D2R_DepthFog.fx", "GameBarOn").handle != 0;
        hud::SetPictureReady(want && effects && fresh);
        static int told = -1;   // why the toolbar can or cannot be moved in the picture, once per change
        const int now = !hud::PictureWanted() ? 0 : !effects ? 1 : !fresh ? 2 : 3;
        if (now != told) {
            told = now;
            static const char* const kWhy[] = {"not wanted (first person, or all sliders 0, or no AFR stereo)",
                                               "NOT possible - ReShade's effects are switched off",
                                               "NOT possible - D2R_DepthFog.fx is old or failed to compile (no GameBarOn)",
                                               "on - taken out and drawn back by D2R_DepthFog.fx"};
            LogF("vrcam: toolbar / labels moved in the picture: %s", kWhy[now]);
        }
    }
    // AFR: frames alternate, the next present is the other eye's. An older
    // shader without the right eye's technique keeps the left one for both.
    const int next = AfrOn() && g_tech[1].handle ? g_fxEye ^ 1 : 0;
    for (int k = 0; k < 2; ++k) {   // the next eye's first: the effect never has none on in between
        const int e = next ^ k;
        if (!g_tech[e].handle) continue;
        const bool on = want && e == next;
        if (rt->get_technique_state(g_tech[e]) != on) rt->set_technique_state(g_tech[e], on);
    }
}

// How much of the screen the game draws its scene at (2026-10-08). With DLSS on, the
// scene is drawn into the top left 0.5 - 0.67 of screen-size targets and only then
// scaled up; ReShade's depth is that buffer, so the fog and the sky took the whole
// screen's depth from a corner of it (a dark box on the picture, players' screenshots).
// Read from the game itself: the viewport set while a screen-size depth buffer is bound.
namespace renderscale {
using namespace reshade::api;
thread_local command_list* t_cl = nullptr;      // a list is recorded on one thread at a time
thread_local uint32_t t_dsW = 0, t_dsH = 0;     // its depth buffer now (0 = none)
thread_local float t_vpW = 0.0f, t_vpH = 0.0f;  // its viewport now
std::atomic<uint32_t> g_screenW{0}, g_screenH{0};
// The shares seen lately and how often: the scene's passes are most of them - the
// interface, drawn after the upscale, would make the largest one the whole screen.
struct Seen { uint32_t x, y, n; };
SRWLOCK g_lock = SRWLOCK_INIT;
Seen g_seen[8];
int g_seenCount = 0;

void Note() {
    const uint32_t w = g_screenW.load(std::memory_order_relaxed), h = g_screenH.load(std::memory_order_relaxed);
    if (!w || t_dsW != w || t_dsH != h || t_vpW < 16.0f || t_vpH < 16.0f) return;   // not a screen-size depth buffer
    if (t_vpW > w * 1.01f || t_vpH > h * 1.01f) return;
    const uint32_t x = (uint32_t)(t_vpW / w * 1000.0f + 0.5f), y = (uint32_t)(t_vpH / h * 1000.0f + 0.5f);
    AcquireSRWLockExclusive(&g_lock);
    int k = 0;
    while (k < g_seenCount && !(g_seen[k].x == x && g_seen[k].y == y)) ++k;
    if (k < g_seenCount) ++g_seen[k].n;
    else if (g_seenCount < 8) g_seen[g_seenCount++] = {x, y, 1};
    ReleaseSRWLockExclusive(&g_lock);
}
void OnBind(command_list* cl, uint32_t, const resource_view*, resource_view dsv) {
    if (cl != t_cl) { t_cl = cl; t_vpW = t_vpH = 0.0f; }
    t_dsW = t_dsH = 0;
    if (dsv.handle) {
        device* dev = cl->get_device();
        const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(dsv));
        t_dsW = d.texture.width; t_dsH = d.texture.height;
    }
    Note();
}
void OnViewports(command_list* cl, uint32_t first, uint32_t count, const viewport* vps) {
    if (first != 0 || count == 0) return;
    if (cl != t_cl) { t_cl = cl; t_dsW = t_dsH = 0; }
    t_vpW = vps[0].width; t_vpH = vps[0].height;
    Note();
}
// Once a frame, before the effect: the share seen over the last half second, kept when
// none was seen (a menu, a loading screen).
void Frame(effect_runtime* rt, float out[2]) {
    uint32_t w = 0, h = 0;
    rt->get_screenshot_width_and_height(&w, &h);
    g_screenW.store(w); g_screenH.store(h);
    static float scale[2] = {1.0f, 1.0f};
    static ULONGLONG since = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - since >= 500) {
        since = now;
        Seen s[8];
        AcquireSRWLockExclusive(&g_lock);
        const int n = g_seenCount;
        memcpy(s, g_seen, sizeof s);
        g_seenCount = 0;
        ReleaseSRWLockExclusive(&g_lock);
        int best = -1;
        for (int k = 0; k < n; ++k) if (best < 0 || s[k].n > s[best].n) best = k;
        if (best >= 0) {
            const float sx = std::clamp(s[best].x / 1000.0f, 0.25f, 1.0f), sy = std::clamp(s[best].y / 1000.0f, 0.25f, 1.0f);
            if (fabsf(sx - scale[0]) > 0.002f || fabsf(sy - scale[1]) > 0.002f) {
                char all[160] = "";
                size_t len = 0;
                for (int k = 0; k < n && len < sizeof all - 24; ++k)
                    len += snprintf(all + len, sizeof all - len, " %.3fx%.3f:%u", s[k].x / 1000.0f, s[k].y / 1000.0f, s[k].n);
                LogF("vrcam: the game draws its scene at %.3f x %.3f of the screen (%ux%u)%s; seen:%s", sx, sy, w, h,
                     sx < 0.99f ? " - an upscaler (DLSS): the depth is read from that part" : "", all);
            }
            scale[0] = sx; scale[1] = sy;
        }
    }
    out[0] = scale[0]; out[1] = scale[1];
}
}  // namespace renderscale

// The camera behind the depth, for FlatVR's 3D from ReShade's depth (2026-10-08):
// with it FlatVR shifts the second eye by each pixel's distance in metres and the
// user's IPD - the world at its own size, as real stereo with [stereo] true_scale
// has it - instead of its depth sliders' guess, which made the world look bigger.
// Written while the world is 1:1 (TrueScale: the frustum is FlatVR's screen);
// otherwise the counter stops and FlatVR goes back to its sliders.
namespace depthcam {
FlatVRDepthCamera* g_block = nullptr;
void Publish() {
    if (!g_block) {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 2000) return;
        lastTry = GetTickCount64();
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FlatVRDepthCamera), FLATVR_DEPTH_CAMERA_NAME);
        if (!m) return;
        g_block = (FlatVRDepthCamera*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FlatVRDepthCamera));
        if (!g_block) { CloseHandle(m); return; }   // else the handle is kept: the name lives while we do
    }
    float ipd = 0.0f, conv = 0.0f, w = 0.0f, h = 0.0f, d = 0.0f;
    if (!g_enabled.load() || !g_inWorld.load() || !TrueScale(&ipd, &conv) || !LiveScreen(&w, &h, &d) || d <= 0.0f) return;
    const float eyeM = g_set.eyeMm.load() * 0.001f;
    if (!(eyeM > 0.01f)) return;
    g_block->near_world = g_lastNear.load();
    g_block->units_per_metre = ipd / eyeM;
    g_block->tan_half_w = 0.5f * w / d;
    g_block->tan_half_h = 0.5f * h / d;
    g_block->version = FLATVR_DEPTH_CAMERA_VERSION;
    MemoryBarrier();
    g_block->counter = g_block->counter + 1;
}
}  // namespace depthcam

// Right before the swap chain's real Present (after ReShade's effects and overlay): pairtime.
// [stereo] pace (on): the presents held to the headset's rate - a game frame per headset frame.
// FlatVR shows the newest picture each headset frame; a game faster than the headset judders
// on head turns (2026-10-09: 90 fps against 90 Hz smooth, 170 fps not), whatever the game's
// own frame cap says (the game rewrites its Settings.json while it runs, so the Settings
// program cannot set it then). Presents a headset frame: eyes by turns and two passes a pair
// two, a replayed pair (one present), mono and the depth one.
namespace pace {
double g_next = 0.0;
uint32_t g_lastReplays = 0;
int g_replayedRecently = 0;   // presents since a replayed pair was seen, capped
void Tick() {
    if (!g_set.pace.load() || !g_inWorld.load()) { g_next = 0.0; return; }
    const uint32_t r = replay::g_replays.load(std::memory_order_relaxed);
    if (r != g_lastReplays) { g_lastReplays = r; g_replayedRecently = 0; }
    else if (g_replayedRecently < 8) ++g_replayedRecently;
    const bool replayed = g_replayedRecently < 4;
    const int perFrame = AfrOn() && !replayed ? 2 : 1;
    const double period = 1e6 / ((double)g_set.headsetHz.load() * perFrame);
    double now = pairtime::UsNow();
    if (g_next <= 0.0 || now - g_next > 2.0 * period) { g_next = now + period; return; }   // behind: no catching up
    if (now < g_next) {
        static bool fine = (timeBeginPeriod(1), true);
        (void)fine;
        while (g_next - now > 1500.0) { Sleep(1); now = pairtime::UsNow(); }
        while (now < g_next) { YieldProcessor(); now = pairtime::UsNow(); }
    }
    g_next += period;
}
}  // namespace pace

void OnReshadePresent(reshade::api::effect_runtime* rt) {
    static bool firstSeen = false;
    if (!firstSeen) { firstSeen = true; crash::Mark("the first picture presented (ReShade's present)"); }
    // Native OpenXR: the runtime paces the game (xrWaitFrame); a present outside a pair goes
    // to the headset as a flat picture.
    xr::SetWindow((HWND)rt->get_hwnd());   // the swap chain's own window: where the pointer is
    {   // native OpenXR: straight ahead and the body taken again when a game is entered (the hero
        // appears) - the head is wherever it was in the menus; not at an area change, the hero stays
        static bool hadHero = false;
        const bool hero = gamestate::LocalPlayer() != 0;
        if (hero && !hadHero && xr::On()) { Recenter(); Log("vrcam: openxr - a game entered: recentred"); }
        hadHero = hero;
    }
    if (xr::On() || xr::Running() || xr::StopPending())
        replay::XrPresent((ID3D12Device*)rt->get_device()->get_native(), (ID3D12CommandQueue*)rt->get_command_queue()->get_native(),
                          (ID3D12Resource*)rt->get_current_back_buffer().handle);
    else pace::Tick();
    const double t = pairtime::UsNow();
    pairtime::g_presentAt.store(t);
    pairtime::g_presents.fetch_add(1, std::memory_order_relaxed);
    drawprof::Tick();
    // Any mode (mono, stereo, menus): the presents a second and the game's busiest
    // threads, every 10 s - the frame budget's hunt (~7 ms a present, 2026-10-08). On a
    // thread of its own: the snapshot of every thread (Toolhelp) held this present up
    // 30-50 ms every 10 s - a frame far too late, a jerk in a head turn (2026-10-09).
    static std::atomic<DWORD> presentThread{0};
    if (!presentThread.load()) {
        presentThread.store(GetCurrentThreadId());
        if (HANDLE h = CreateThread(nullptr, 0, [](void*) -> DWORD {
                double since = pairtime::UsNow();
                uint32_t n0 = pairtime::g_presents.load();
                pairtime::BusyThreads(0.0);
                for (;;) {
                    Sleep(10000);
                    const double now = pairtime::UsNow();
                    const uint32_t n = pairtime::g_presents.load();
                    if (!pairtime::g_drawThread.load()) pairtime::g_drawThread.store(presentThread.load());
                    LogF("vrcam: %.1f presents/s; the game's busiest threads, %% of a core (present thread %lu): %s",
                         (n - n0) * 1e6 / (now - since), presentThread.load(), pairtime::BusyThreads(now - since).c_str());
                    since = now; n0 = n;
                }
            }, nullptr, 0, nullptr)) CloseHandle(h);
    }
}

void OnBeginEffects(reshade::api::effect_runtime* rt, reshade::api::command_list*, reshade::api::resource_view, reshade::api::resource_view) {
    pairtime::g_effBegin.store(pairtime::UsNow());
    if (replay::g_on.load()) replay::g_rt = rt;
    if (replay::g_on.load()) {   // the depth buffer our effect reads: replay keeps the left eye's for its present
        const reshade::api::effect_texture_variable v = rt->find_texture_variable("D2R_DepthFog.fx", "FogDepthTex");
        if (v.handle) {
            reshade::api::resource_view srv{0};
            rt->get_texture_binding(v, &srv, nullptr);
            const uint64_t res = srv.handle ? rt->get_device()->get_resource_from_view(srv).handle : 0;
            if (res && res != replay::g_depthRes.load()) {
                replay::g_depthRes.store(res);
                LogF("vrcam: replay - the effects' depth buffer: %p", (void*)res);
            }
        }
    }
    {
        float s[2];
        renderscale::Frame(rt, s);
        SetFloats(rt, "DepthScale", s, 2);
    }
    depthcam::Publish();
    {   // FlatVR's ring of finished pictures, on or off from D2R VR Settings: the addon
        // (FlatVR_DepthProvider 2.19+) reads ReShade.ini [FLATVR] ColourRing once a second
        static int told = -1;
        const int on = g_set.pictureRing.load() ? 1 : 0;
        if (on != told) {
            told = on;
            reshade::set_config_value(nullptr, "FLATVR", "ColourRing", on ? "1" : "0");
            LogF("vrcam: FlatVR's ring of finished pictures %s (ReShade.ini [FLATVR] ColourRing=%d, [stereo] picture_ring)",
                 on ? "on" : "off", on);
        }
    }
    if (!g_tech[0].handle) return;
    if (!rt->get_technique_state(g_tech[0]) && !(g_tech[1].handle && rt->get_technique_state(g_tech[1]))) return;
    SetFloat(rt, "NearPlane", g_lastNear.load());
    SetFloat(rt, "FogStart", g_underground.load() ? g_set.fogCaveStart.load() : g_set.fogStart.load());
    SetFloat(rt, "FogEnd", g_underground.load() ? g_set.fogCaveEnd.load() : g_set.fogEnd.load());
    SetFloat(rt, "FogStrength", g_underground.load() ? g_set.fogCaveStrength.load() : 1.0f);
    SetFloat(rt, "FogCurve", g_set.fogCurve.load());
    SetFloat(rt, "FogBlur", g_set.fogBlur.load());
    SetBool(rt, "FogOn", FogWanted());
    SetBool(rt, "TableKeyOn", TableKeyWanted());
    SetFloat(rt, "TableFloor", g_set.tableFloor.load());
    if (TableKeyWanted()) {   // the bounds: this eye's ray, and the game's own view to test the ground against
        SkyView sv[2];
        TableBox b;
        AcquireSRWLockShared(&g_skyLock); sv[0] = g_skyView[0]; sv[1] = g_skyView[1]; b = g_tableBox; ReleaseSRWLockShared(&g_skyLock);
        const bool on = b.ok && g_set.tableBounds.load() > 0.0f && sv[0].proj_ok && sv[0].axes_ok;
        SetBool(rt, "TableBoundsOn", on);
        SetFloat(rt, "TableBounds", g_set.tableBounds.load());
        if (on) {
            for (int e = 0; e < 2; ++e) {
                const SkyView& v = sv[e].axes_ok && sv[e].proj_ok ? sv[e] : sv[0];
                char n[16];
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, v.proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, v.axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, v.axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, v.axes + 6, 3);
                snprintf(n, sizeof n, "TableEye%d", e); SetFloats(rt, n, v.eyeRel, 3);
            }
            // row-major v * M: clip component j is the point dotted with column j
            const int cols[3] = {0, 1, 3};
            const char* const names[3] = {"TableGameX", "TableGameY", "TableGameW"};
            for (int k = 0; k < 3; ++k) {
                const int j = cols[k];
                const float c[4] = {b.vp[j], b.vp[4 + j], b.vp[8 + j], b.vp[12 + j]};
                SetFloats(rt, names[k], c, 4);
            }
            SetFloats(rt, "TableHero", b.hero, 3);
            const float t = g_set.tableTurnDeg.load() * 0.0174532925f;
            const float turn[2] = {cosf(t), sinf(t)};
            SetFloats(rt, "TableTurn", turn, 2);
        }
    }
    {
        float bg[3];
        AcquireSRWLockShared(&g_tableBgLock);
        for (int c = 0; c < 3; ++c) bg[c] = g_tableBg[c];
        ReleaseSRWLockShared(&g_tableBgLock);
        SetFloats(rt, "TableBackground", bg, 3);
    }
    {   // the act's own fog colour; without one the fog takes the sky's horizon, or stays dark
        static const float kDark[3] = {0.02f, 0.02f, 0.025f};
        SkyAct fa{};
        CaveFog cave{};
        AcquireSRWLockShared(&g_skyCfgLock);
        if (const int a = g_fogAct.load(); a > 0) fa = g_skyAct[a];
        cave = g_caveFog;
        ReleaseSRWLockShared(&g_skyCfgLock);
        if (g_underground.load() && cave.set) { fa.fogSet = true; memcpy(fa.fog, cave.rgb, sizeof fa.fog); }   // caves and dungeons: their own
        else if (fa.fogSet) {   // in the open the act's fog (its day colour) darkens at night with the sky
            const float m = DayNightMul();
            for (float& f : fa.fog) f *= m;
        }
        SetBool(rt, "FogFixed", fa.fogSet);
        SetFloats(rt, "FogColor", fa.fogSet ? fa.fog : kDark, 3);
    }
    const int pal = g_skyPalette.load();
    // The eye of the frame being presented. Read straight from the block it
    // raced the game thread flipping the eye, so now and then the sky got the
    // other eye's convergence shift and shook sideways. Frames alternate, so the
    // eye simply flips each present, and every 30 presents its phase is checked
    // against those readings by majority - one wrong reading no longer shows.
    int eye = g_eye.load() & 1;
    // A pair per game frame presents from inside its own pass, on this thread,
    // after the pass set its eye: no race, the picture is the eye just set.
    if (PairWanted()) eye = g_eye.load() & 1;
    else if (AfrOn() && g_afrBlock) {
        static int e = 0, samples = 0, disagree = 0;
        e ^= 1;
        if ((((g_eye.load() ^ (int)g_afrBlock->swap) & 1)) != e) ++disagree;
        if (++samples >= 30) {
            static ULONGLONG told = 0;   // how much the raced reading disagrees: a flickering HUD or sky would show here
            if (g_set.uiShift.load() > 0.0f && GetTickCount64() - told > 5000) {
                told = GetTickCount64();
                LogF("vrcam: eye phase - %d of 30 raced readings disagreed%s", disagree, disagree > samples / 2 ? ", phase flipped" : "");
            }
            if (disagree > samples / 2) e ^= 1;
            samples = disagree = 0;
        }
        eye = e;
    }
    g_fxEye = eye;
    // Both eyes' views: the technique on picks one. Without AFR the one eye there is fills both.
    SkyView sv[2];
    AcquireSRWLockShared(&g_skyLock);
    sv[0] = SkyBack(AfrOn() ? 0 : eye); sv[1] = SkyBack(AfrOn() ? 1 : eye);
    ReleaseSRWLockShared(&g_skyLock);
    bool sky = pal >= 0;
    for (const SkyView& v : sv) sky = sky && v.proj_ok && v.axes_ok && v.proj[0] != 0.0f && v.proj[1] != 0.0f;
    SetBool(rt, "SkyOn", sky);
    {   // the cave ceiling: a plane CeilHeight over the hero's floor, met by each eye's ray
        bool ceil = !sky && CeilWanted();
        for (const SkyView& v : sv) ceil = ceil && v.proj_ok && v.axes_ok && v.proj[0] != 0.0f && v.proj[1] != 0.0f;
        SetBool(rt, "CeilOn", ceil);
        if (ceil) {
            const float scale = g_set.ceilScale.load();
            // The hero's place wrapped on a whole number of tiles: the world's own
            // coordinates in a float would round the pattern into steps far from the
            // origin; what repeats every tile cannot tell the wrap.
            const double period = 64.0 * scale;
            // Heights over the area's floor, not the hero's feet: on a ledge the ceiling rose with
            // him (the user, 2026-10-07). The floor: the lowest the hero has stood since the area
            // changed; everything the shader measures (the ceiling, its height map) follows it.
            static float floorY = 0.0f;
            static uint32_t floorGen = ~0u;
            if (floorGen != g_biomeGen.load()) { floorGen = g_biomeGen.load(); floorY = sv[0].hero[1]; }
            floorY = std::min(floorY, sv[0].hero[1]);
            for (int e = 0; e < 2; ++e) {
                char n[16];
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, sv[e].proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, sv[e].axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, sv[e].axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, sv[e].axes + 6, 3);
                const float eyeOverFloor[3] = {sv[e].eyeRel[0], sv[e].eyeRel[1] + (sv[e].hero[1] - floorY), sv[e].eyeRel[2]};
                snprintf(n, sizeof n, "CeilEye%d", e); SetFloats(rt, n, eyeOverFloor, 3);
                double hx = fmod((double)sv[e].hero[0], period), hz = fmod((double)sv[e].hero[2], period);
                if (hx < 0.0) hx += period;
                if (hz < 0.0) hz += period;
                const float hero[2] = {(float)hx, (float)hz};
                snprintf(n, sizeof n, "CeilHero%d", e); SetFloats(rt, n, hero, 2);
            }
            float height, bright, light, relief;
            int slot;
            CeilingNow(&height, &bright, &light, &relief, &slot);
            SetFloat(rt, "CeilHeight", height);
            SetFloat(rt, "CeilScale", scale);
            SetFloat(rt, "CeilBrightness", bright);
            SetFloat(rt, "CeilRelief", relief);
            SetFloat(rt, "CeilTexSlot", (float)slot);
            SetFloat(rt, "CeilLightRadius", light);
            SetBool(rt, "CeilTorches", g_set.ceilTorches.load());
            SetFloat(rt, "CeilTorchBright", g_set.ceilTorchBright.load());
            SetFloat(rt, "CeilTorchRadius", g_set.ceilTorchRadius.load());
            SetFloat(rt, "CeilHalo", g_set.ceilHalo.load());
            SetFloat(rt, "CeilTorchWarm", g_set.ceilTorchWarm.load());
            SetFloat(rt, "CeilWallDist", g_set.ceilWall.load());
            SetFloat(rt, "CeilFloorDepth", g_set.ceilFloor.load());
            {   // the lit objects the game has near the hero, relative to him; the flame ~7 units up
                worldobj::Light l[worldobj::kMaxLights];
                const int n = worldobj::g_known.load() ? worldobj::Lights(l) : 0;
                float pos[worldobj::kMaxLights * 4] = {}, col[worldobj::kMaxLights * 4] = {};
                for (int i = 0; i < n; ++i) {
                    pos[i * 4 + 0] = l[i].x - sv[0].hero[0];
                    pos[i * 4 + 1] = l[i].y - floorY;   // over the area's floor, as the ceiling
                    pos[i * 4 + 2] = l[i].z - sv[0].hero[2];
                    pos[i * 4 + 3] = l[i].lit;
                    for (int c = 0; c < 3; ++c) col[i * 4 + c] = l[i].rgb[c];
                    // farther than [ceiling] torch_distance from the hero: gone, fading over its last quarter -
                    // the torches of the next hall lit the ceiling over the walls between (2026-10-07)
                    const float d = sqrtf(pos[i * 4] * pos[i * 4] + pos[i * 4 + 2] * pos[i * 4 + 2]), reach = g_set.ceilTorchDist.load();
                    col[i * 4 + 3] = std::clamp((reach - d) / (0.25f * reach), 0.0f, 1.0f) * (l[i].monster ? g_set.ceilMonsterBright.load() : 1.0f);
                }
                SetBool(rt, "GameLightsOn", worldobj::g_known.load());
                SetFloats(rt, "GameLightPos", pos, worldobj::kMaxLights * 4);
                SetFloats(rt, "GameLightCol", col, worldobj::kMaxLights * 4);
                static ULONGLONG told = 0;   // what goes to the shader, every 5 s: the nearest light, as the hero sees it
                if (GetTickCount64() - told > 5000) {
                    told = GetTickCount64();
                    const bool hasPos = rt->find_uniform_variable("D2R_DepthFog.fx", "GameLightPos").handle != 0;
                    LogF("worldobj: to the shader %d lights (uniform %s), nearest at %.1f %.1f %.1f radius %.0f colour %.2f %.2f %.2f; hero %.1f %.1f %.1f, eye %.1f %.1f %.1f, ceiling %.0f",
                         n, hasPos ? "found" : "MISSING", pos[0], pos[1], pos[2], pos[3], col[0], col[1], col[2],
                         sv[0].hero[0], sv[0].hero[1], sv[0].hero[2], sv[0].eyeRel[0], sv[0].eyeRel[1], sv[0].eyeRel[2], height);
                }
            }
            float wet, detail, contrast;
            CeilVaultCfg vault;
            CeilingLookNow(&wet, &detail, &contrast, &vault);
            SetBool(rt, "CeilDome", vault.dome);
            SetFloat(rt, "CeilTexSize", vault.texSize);
            SetFloat(rt, "CeilReliefPic", vault.reliefPic);
            SetFloat(rt, "CeilDomeRadius", vault.domeRadius);
            SetFloat(rt, "CeilDomeMax", vault.domeMax);
            SetFloat(rt, "CeilDomeFind", vault.domeFind);
            SetFloat(rt, "CeilMapGen", (float)(g_biomeGen.load() & 0xFFFFF));   // another area: the height map starts again
            SetFloat(rt, "CeilWet", wet);
            SetFloat(rt, "CeilBump", detail);
            SetFloat(rt, "CeilContrast", contrast);
            if (const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", "CeilSteps"); u.handle) {
                const int steps = g_set.ceilSteps.load();
                rt->set_uniform_value_int(u, &steps, 1);
            }
            SetBool(rt, "CeilTexOn", slot > 0);
        }
    }
    {   // from above: the labels on the tilted plane
        float keepBar[4], keepMap[4];
        uint64_t srv = 0;
        const bool on = hud::LabelsNow(keepBar, keepMap, &srv);
        if (srv && srv != g_pieceBound[2]) {
            rt->update_texture_bindings("D2R_GAME_LAYER", reshade::api::resource_view{srv}, reshade::api::resource_view{srv});
            g_pieceBound[2] = srv;
        }
        SetBool(rt, "LabelsOn", on && srv == g_pieceBound[2]);
        const bool mask = srv && srv == g_pieceBound[2] && hud::UiMaskNow();
        SetBool(rt, "UiMaskOn", mask);
        if (on) {   // on the floor (or for the plate alone) flat where the game puts them, each eye's own: only faded
            const bool floor = FloorView();
            const bool tilted = !floor && LabelsTilted();
            SetFloat(rt, "LabelBase", tilted ? g_set.labelsNear.load() * 0.005f : 0.0f);
            SetFloat(rt, "LabelTilt", tilted ? g_set.labelsTilt.load() * 0.005f : 0.0f);
            SetFloat(rt, "LabelAlpha", floor && !LabelBoxNative() ? g_set.labelsAlphaFloor.load() : 1.0f);
            float box[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int32_t r[4];
            const uint32_t sw = renderscale::g_screenW.load(), sh = renderscale::g_screenH.load();
            if (g_set.plateAlpha.load() < 0.995f && sw && sh && gamestate::PlateRect(r)) {
                box[0] = (float)r[0] / sw; box[1] = (float)r[1] / sh;
                box[2] = (float)(r[0] + r[2]) / sw; box[3] = (float)(r[1] + r[3]) / sh;
            }
            SetFloats(rt, "PlateBox", box, 4);
            SetFloat(rt, "PlateAlpha", g_set.plateAlpha.load());
        }
        if (on || mask) {   // the toolbar and the map in the layer: never faded, never keyed away (TableKey)
            SetFloats(rt, "LabelKeep0", keepBar, 4);
            SetFloats(rt, "LabelKeep1", keepMap, 4);
        }
    }
    SetFloat(rt, "GameBarShift", BarNearWanted() ? BarNearNow() * 0.005f : 0.0f);
    SetFloat(rt, "GameMapShift", MapNearWanted() ? g_set.mapNearFloor.load() * 0.005f : 0.0f);
    for (int i = 0; i < 2; ++i) {   // the toolbar and the map back in the picture, at their own size and depth (classic views)
        float box[4];
        uint64_t srv = 0;
        const bool on = hud::PictureNow(i, box, &srv);
        if (srv && srv != g_pieceBound[i]) {
            rt->update_texture_bindings(i ? "D2R_GAME_MAP" : "D2R_GAME_BAR", reshade::api::resource_view{srv}, reshade::api::resource_view{srv});
            g_pieceBound[i] = srv;
        }
        SetBool(rt, i ? "GameMapOn" : "GameBarOn", on && srv == g_pieceBound[i]);
        if (on) SetFloats(rt, i ? "GameMapBox" : "GameBarBox", box, 4);
    }
    // the left eye's HUD moves right, the right eye's left: nearer than the screen
    SetFloat(rt, "HudShift", HudWanted() ? g_set.uiShift.load() * 0.005f : 0.0f);
#if D2RVR_FIRST_PERSON
    {   // [debug] phantom_ray (or bone_axes): the red line the left hand takes the weapon by -
        // a crossbow's is also where it shoots ("make the phantom's vector", 2026-10-05: the
        // model must lie on this line); a staff's, spear's or two-handed sword's is its shaft
        // or hilt as the skeleton hook laid it (skel::GrabLine)
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        auto world = [&](const float* v, float* w) {   // head turn-only frame -> world, as HandRay
            for (int i = 0; i < 3; ++i) w[i] = v[0] * f.camR[i] - v[2] * f.camF[i];
            w[1] += v[1];
        };
        {   // each eye's camera from where the hands hang, and its projection (set here too:
            // the sky's block sets them only while the sky is drawn). Without AFR (flat) only
            // the one eye there is is kept fresh: both take it, as the sky's block does - the
            // other's stale view, written here, froze the cave ceiling in flat F3 (2026-10-07).
            SkyView sv[2];
            AcquireSRWLockShared(&g_skyLock);
            sv[0] = SkyBack(AfrOn() ? 0 : eye); sv[1] = SkyBack(AfrOn() ? 1 : eye);
            ReleaseSRWLockShared(&g_skyLock);
            for (int e = 0; e < 2; ++e) {
                const SkyView& s = sv[e].axes_ok && sv[e].proj_ok ? sv[e] : sv[0];
                float off[3];
                for (int i = 0; i < 3; ++i) off[i] = s.eyeRel[i] - g_eyeFromLook[i].load();
                char n[16];
                snprintf(n, sizeof n, "PhantomEye%d", e); SetFloats(rt, n, off, 3);
                if (!s.axes_ok || !s.proj_ok) continue;
                snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, s.proj, 4);
                snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, s.axes, 3);
                snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, s.axes + 3, 3);
                snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, s.axes + 6, 3);
            }
        }
        float o[3], d[3], ga[3], gb[3];
        const bool xbow = XbowLikeStaff() && StaffAim();
        const bool ray = PhantomWanted() && xbow && HandRay(o, d);
        const bool grab = PhantomWanted() && !xbow && f.valid && skel::GrabLine(ga, gb);
        SetBool(rt, "PhantomOn", ray || grab);
        if (ray) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre
            float a[3], b[3];
            for (int i = 0; i < 3; ++i) { a[i] = o[i] - g_eyeWorld[i].load(); b[i] = a[i] + d[i] * 1.5f * upm; }
            SetFloats(rt, "PhantomA", a, 3);
            SetFloats(rt, "PhantomB", b, 3);
        } else if (grab) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            float a[3], b[3];
            world(ga, a); world(gb, b);
            for (int i = 0; i < 3; ++i) { a[i] *= upm; b[i] *= upm; }
            SetFloats(rt, "PhantomA", a, 3);
            SetFloats(rt, "PhantomB", b, 3);
        }
        // and the bone of the weapon in hand, its own axes from its origin, 0.5 m each: X green, Y blue, Z yellow
        float bo[3], bx[3][3];
        const bool bone = BoneAxesWanted() && skel::GunBone(bo, bx);
        SetBool(rt, "BoneOn", bone);
        if (bone && f.valid) {
            const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            float o[3]; world(bo, o);
            for (float& v : o) v *= upm;
            SetFloats(rt, "BoneO", o, 3);
            static const char* const kNames[3] = {"BoneX", "BoneY", "BoneZ"};
            for (int i = 0; i < 3; ++i) {
                float d[3]; world(bx[i], d);
                for (int k = 0; k < 3; ++k) d[k] = o[k] + d[k] * 0.5f * upm;
                SetFloats(rt, kNames[i], d, 3);
            }
        }
    }
#endif
    SetFloat(rt, "HudTop", g_set.hudTop.load());
    {   // each eye's stamp, into the picture
        const bool on = StampWanted() && g_afrBlock->stamp_magic == FLATVR_AFR_STAMP_MAGIC;
        SetBool(rt, "StampOn", on);
        if (on) {
            uint32_t v[2];
            for (int e = 0; e < 2; ++e) {
                const int from = AfrOn() ? e : (eye & 1);
                v[e] = g_stampHist[from][(g_stampHistAt[from] - PipeDepth()) & 3];
                const reshade::api::effect_uniform_variable u = rt->find_uniform_variable("D2R_DepthFog.fx", e ? "FrameStamp1" : "FrameStamp0");
                if (u.handle) rt->set_uniform_value_uint(u, &v[e], 1);
            }
            flog::Line("R,%u,%d,%u,%u", flog::Pass(), eye & 1, v[0], v[1]);
        }
    }
    if (!sky) return;
    const SkyPalette& p = kPalettes[pal];
    for (int e = 0; e < 2; ++e) {
        char n[16];
        snprintf(n, sizeof n, "SkyProj%d", e); SetFloats(rt, n, sv[e].proj, 4);
        snprintf(n, sizeof n, "CamRight%d", e); SetFloats(rt, n, sv[e].axes, 3);
        snprintf(n, sizeof n, "CamUp%d", e); SetFloats(rt, n, sv[e].axes + 3, 3);
        snprintf(n, sizeof n, "CamBack%d", e); SetFloats(rt, n, sv[e].axes + 6, 3);
    }
    SetFloats(rt, "SkyZenith", p.zenith, 3);
    SetFloats(rt, "SkyHorizon", p.horizon, 3);
    const float el = p.sunElevDeg * 0.0174532925f, az = p.sunAzDeg * 0.0174532925f;
    const float sun[3] = {cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az)};
    SetFloats(rt, "SunDir", sun, 3);
    SetFloat(rt, "SkySun", p.sun);
    SetFloat(rt, "SkyClouds", std::clamp(p.clouds * g_set.skyClouds.load(), 0.0f, 1.0f));
    SetFloat(rt, "SkyStars", p.stars);
    AcquireSRWLockShared(&g_skyCfgLock); const SkyAct act = g_skyAct[p.act]; ReleaseSRWLockShared(&g_skyCfgLock);
    SetFloat(rt, "SkyTex", act.bandOk ? (float)p.act : 0.0f);
    SetBool(rt, "SkyCapOn", act.capOk);
    // An act with a night picture: the picture itself goes dark (SkyNightMix), the
    // brightness stays; without one the day picture is dimmed.
    SetFloat(rt, "SkyNightMix", act.nightOk && act.bandOk ? 1.0f - DayNightK() : 0.0f);
    SetFloat(rt, "SkyDrift", g_set.skyDrift.load() ? 1.0f : 0.0f);
    SetFloat(rt, "SkyBrightness", act.nightOk && act.bandOk ? act.bright : act.bright * DayNightMul());
}

// ReShade may load after us; tried twice a second until it is there.
void TryRegister() {
    if (g_registered || !g_self) return;
    if (!reshade::register_addon(g_self)) return;
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(&OnBeginEffects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(&OnFinishEffects);
    reshade::register_event<reshade::addon_event::reshade_present>(&OnReshadePresent);
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&Forget);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&renderscale::OnBind>::Call);
    reshade::register_event<reshade::addon_event::bind_viewports>(&d2rvr::D3D12Only<&renderscale::OnViewports>::Call);
    uitrace::Register();   // Ctrl + F10: one frame's render targets to d2r_vr_uitrace.txt
    cbcmp::Register();     // [debug] cb_compare: one pair's constant buffers, left against right
    rtrace::Register();    // [debug] replay_trace: one pair's command list submissions
    replay::Register();    // [debug] replay_proto: the right eye from the left eye's command lists
    // native OpenXR's session gone before the game's device is (the game closing)
    reshade::register_event<reshade::addon_event::destroy_device>([](reshade::api::device* d) { xr::DeviceGone((ID3D12Device*)d->get_native()); });
    hud::SetLogger(&Log);
    dlssmv::SetLogger(&Log);
    hud::Register();       // the interface's own layer: [hud] hide
    g_registered = true;
    Log("vrcam: joined ReShade - the fog and the sky follow d2r_vr.ini [fog] [sky]");
    crash::Mark("joined ReShade (its present events)");
    // Both read ReShade's depth, which Generic Depth provides (FlatVR's addon
    // only corrects its pick); without it the shader sees no depth and stays off.
    wchar_t ini[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, ini, MAX_PATH);
    while (n && ini[n - 1] != L'\\') --n;
    ini[n] = 0;
    wcscat_s(ini, L"ReShade.ini");
    wchar_t off[512] = {};
    GetPrivateProfileStringW(L"ADDON", L"DisabledAddons", L"", off, 512, ini);
    if (wcsstr(off, L"Generic Depth"))
        Log("vrcam: ReShade's Generic Depth add-on is OFF (ReShade.ini DisabledAddons) - the fog and the sky get no depth and stay off; tick it on ReShade's Add-ons tab");
}
}  // namespace fx

// Keys, mouse, settings reload, bridge connect, XInput hooks: one 1 ms timer
// (a high-resolution waitable one - SetTimer does not go below 10 ms). What
// need not run that often goes by the clock, not by the tick count.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
DWORD g_updateThreadId = 0;

// F3, the game on the floor: the mouse pointer laid on the game's ground by
// itself, wherever the head looks from. A plane seen from our camera has
// 1/depth linear in the screen's height, so FlatVR's per-eye shift
// (FlatVRGameHudPointerDepth: base + tilt v + curve v^2, v 0 top .. 1 bottom, a
// fraction of one eye's width, + nearer) is exactly a line: with the off-axis
// pair of VrView / VrProj, shift(v) = M[0] ipd / 4 (1/z(v) - 1/conv). Its size at
// the top of the screen follows the depths' ratio (far off it is small, as the
// ground it points at). Sent when it moves, not every tick: hud's lock is the
// render thread's too.
bool FloorPointerAuto() {
    return g_set.platform.load() == 1 && g_enabled.load() && TableView() && g_floorCamOk.load() && g_inWorld.load() &&
           !gamestate::MenuOpen();
}
void FloorPointerTick() {
    static float sent[3] = {1e9f, 1e9f, 1e9f};
    static bool told = false;
    if (!FloorPointerAuto()) { sent[0] = 1e9f; told = false; return; }
    const float h = g_floorEyeH.load(), sx = g_projSx.load(), sy = g_projSy.load();
    if (h < 1e-3f || sx <= 0.0f || sy <= 0.0f) return;
    const float fy = g_floorFwdY.load(), uy = g_floorUpY.load(), conv = g_stereoConv.load();
    auto inv = [&](float v) { return -(fy + uy * (1.0f - 2.0f * v) / sy) / h; };   // 1 / the ground's depth at height v
    const float k = sx * g_stereoIpd.load() * 0.25f, c = conv > 0.0f ? 1.0f / conv : 0.0f;
    const float top = inv(0.0f), bottom = inv(1.0f);
    const float base = k * (top - c), tilt = k * (bottom - top);
    const float topScale = bottom > 1e-6f ? std::clamp(top / bottom, 0.3f, 1.0f) : 1.0f;
    if (fabsf(base - sent[0]) < 2e-4f && fabsf(tilt - sent[1]) < 2e-4f && fabsf(topScale - sent[2]) < 0.005f) return;
    sent[0] = base; sent[1] = tilt; sent[2] = topScale;
    hud::SetPointerDepth(base, tilt, 0.0f, topScale);
    if (!told) {
        told = true;
        LogF("vrcam: mouse pointer for FlatVR laid on the floor's ground: shift %.4f at the top, %.4f at the bottom, size at the top %.2f",
             base, base + tilt, topScale);
    }
}

// VR F2 with mouse look: the crosshair's depth, from [hud_third] crosshair_depth
// looking ahead to crosshair_depth_down looking straight down, by the sine of the
// camera's look down - the ground in the middle of the screen comes nearer as the
// view tips down, and is far off looking ahead. Every tick (the head
// moves), sent only when it changed: hud's lock is the render thread's too.
bool CrosshairDepthNow() {
    return g_set.platform.load() == 1 && ViewNow() == 2 && KeyMoveMode() && ShooterActive() && g_set.crosshairOn.load();
}
void CrosshairDepthTick() {
    static float sent = 1e9f;
    if (!CrosshairDepthNow()) { sent = 1e9f; return; }
    const float down = std::clamp(-g_viewFwdY.load(), 0.0f, 1.0f);
    const float ahead = g_set.crosshairDepth.load();
    const float d = (ahead + (g_set.crosshairDepthDown.load() - ahead) * down) * 0.005f;
    if (fabsf(d - sent) < 2e-4f) return;
    sent = d;
    hud::SetPointerDepth(d, 0.0f, 0.0f, 1.0f);
}

// A key's press, once per press, only while the game is in front.
struct Key {
    int vk;
    bool was = false;
    bool Pressed(bool focus) {
        const bool now = focus && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = now && !was;
        was = now;
        return edge;
    }
};

DWORD WINAPI UpdateThread(void*) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const bool highRes = timer != nullptr;
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = -10000;   // 1 ms, then every 1 ms
    SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE);
    LogF("vrcam: 1 ms update timer (%s)", highRes ? "high resolution" : "plain - may run at the system's timer resolution");
    Key f12{VK_F12}, f11{VK_F11}, f10{VK_F10}, f9{VK_F9};
    Key fView[5] = {{VK_F1}, {VK_F2}, {VK_F3}, {VK_F4}, {VK_F5}};   // the views, as on the Home page
    ULONGLONG nextState = 0, nextSlow = 0;
    bool toldWorld = false;
    for (;;) {
        const DWORD w = MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (w != WAIT_OBJECT_0) {
            MSG m;
            bool quit = false;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) quit = true; else DispatchMessageW(&m); }
            if (quit) break;
            continue;
        }
        const ULONGLONG nowMs = GetTickCount64();
        const bool focus = GameFocused();
        if (f12.Pressed(focus)) Toggle();
        {   // the same from BodyWalk's mapping: the bridge's view actions, at the press
            static uint32_t was = 0;
            const uint32_t held = ActionsHeld(), pressed = held & ~was;
            was = held;
            // The bits keep the old numbering (an older bridge sends them): 3 was the VR
            // mouse view, gone; 4 the body, now F3; 5 the floor, now F4.
            static const int kVrOf[5] = {1, 2, 0, 4, 3};   // the bridge's actions: above, third, (flat F3), body = F4, table = F3
            const bool vr = g_set.platform.load() == 1;
            for (int k = 0; k < 5; ++k)
                if (pressed & (D2RVR_ACT_VIEW_1 << k)) SetView(vr ? kVrOf[k] : k + 1);
            if (pressed & D2RVR_ACT_VIEW_NEXT) Toggle();
            if (pressed & D2RVR_ACT_RECENTER) Recenter();
        }
        for (int k = 0; k < 5; ++k)
            if (fView[k].Pressed(focus) && k < ViewCount()) SetView(k + 1);
        if (f11.Pressed(focus)) Recenter();
        if (f10.Pressed(focus)) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {   // Ctrl + F10: one frame of render targets, to find the interface's
                std::wstring path = g_iniPath;
                path.resize(path.find_last_of(L'\\') + 1);
                path += L"d2r_vr_uitrace.txt";
                Log(uitrace::Arm(path) ? "vrcam: recording one frame of render targets to d2r_vr_uitrace.txt (Ctrl+F10)"
                                       : "vrcam: a frame recording is already under way");
            } else DumpSkeletons();
        }
        if (f9.Pressed(focus)) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {   // Ctrl + F9: memdiff, to find a flag by flipping it (Ctrl+Shift: start over)
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) memdiff::Reset(&Log);
                else {
                    std::wstring path = g_iniPath;
                    path.resize(path.find_last_of(L'\\') + 1);
                    path += L"d2r_vr_memdiff.txt";
                    memdiff::Step(g_base, path, &Log);
                }
            } else ToggleShooter();
        }
        // our camera while on, and for the stereo view from above while off
        d2rcam::SetEnabled(g_enabled.load() || (g_set.topStereo.load() && g_set.afr.load()));
        MouseTick();
        TurnTick();
        PushArms();
#if D2RVR_FIRST_PERSON
        {   // the toolbar on the hero's own forearm (the game's rig): left forearm = skeleton side 1
            const int a = hud::Anchor(0);
            float e[3], w[3], x[3];
            const bool ok = (a == 1 || a == 2) && skel::HeroForearm(a == 1 ? 1 : 0, e, w, x);
            hud::SetForearm(0, ok, e, w, x);
        }
#endif
        // From above (vrcam off) and from behind the toolbar and the map stay in
        // the picture, at their own size; on the hands only in first person.
        hud::SetClassicView(ClassicNow());
        // first person without the body: the map at a size of its own, from its corner
        // ... and on the floor (F3) its own size, depth and place, as the toolbar's
        hud::SetPictureZoom(BarSizeNow(), InsideFree() ? g_set.mapZoomInside.load() : TableView() ? g_set.mapSizeFloor.load() : 1.0f);
        hud::SetPictureBarMoved(BarNearWanted());
        hud::SetPictureMapMoved(MapNearWanted());
        if (ClassicNow() && TableView()) hud::SetPictureMapOffset(g_set.mapXFloor.load(), g_set.mapYFloor.load());
        else hud::SetPictureMapOffset(0.0f, 0.0f);
        {   // [hud_*] bar_x / bar_y: the toolbar somewhere else on the screen
            float bx = 0.0f, by = 0.0f;
            if (ClassicNow()) BarOffsetNow(&bx, &by);
            hud::SetPictureBarOffset(bx, by);
        }
        if (g_inWorld.load() && g_set.plateAlpha.load() < 0.995f) {   // the plate's place, 10 times a second
            static ULONGLONG nextPlate = 0;
            if (nowMs >= nextPlate) { nextPlate = nowMs + 100; gamestate::PollPlate(); }
        }
        hud::SetLabels(LabelsWanted());
        // the fog leaves the interface alone; the floor's key never takes its text for the void
        hud::SetUiMask((fx::FogWanted() || fx::TableKeyWanted()) && !LabelsWanted());
        SkyTick();
        flog::Tick(g_set.frameLog.load() || GetTickCount64() < flog::g_diagUntil.load());
        gamecmd::Tick();   // skills, potions, Alt... held in BodyWalk: pressed on the UI thread
        FloorPointerTick();   // F3: the mouse pointer on the game's ground, as the head moves
        CrosshairDepthTick();   // F2's crosshair: nearer as the camera looks down
        AfrQuietWhenOff();
        if (nowMs >= nextState) {   // what the hero holds, 5 times a second
            nextState = nowMs + 200;
            gamestate::Tick();
            // A weapon change rebuilds the hero's model: the matrix the facing was
            // read from stays where it was and reads yaw 0 (log 2026-10-04: "model
            // 0.0", "8.00 above the look-at"), and the doll stopped following the
            // hands until a search much later. Looked for again at once, as F11 does.
            {
                static uint32_t lastSet = 0xFFFFFFFFu, lastType = 0xFFFFFFFFu;
                const uint32_t set = gamestate::WeaponSet(), type = gamestate::WeaponType();
                if (lastType != 0xFFFFFFFFu && (set != lastSet || type != lastType)) {
                    g_facingReset.store(true);
                    LogF("vrcam: weapon changed (set %u, kind %u) - looking for the hero's facing again", set, type);
                }
                lastSet = set; lastType = type;
            }
            // the interface is hidden in a game area, whatever the view, with no panel open;
            // the main menu and panels (inventory, trade...) keep all of it
            hud::SetMenuOpen(gamestate::MenuOpen() || !g_inWorld.load());
            // The mouse pointer in the headset: in menus, at a trader, from above and
            // from behind it is wanted (it aims there, or picks items); in first person
            // it aims otherwise and the pointer only gets in the way.
            {
                const bool menu = gamestate::MenuOpen() || !g_inWorld.load();
                const bool firstPerson = g_enabled.load() && !ThirdPerson() && FullBody();   // the mouse views keep it: it is the crosshair
                hud::SetPointer((menu || !firstPerson) && !CrosshairHidden() ? 1u : 2u);   // [hud_third] crosshair=0: none in F2
                // its depth over the ground: F1 (from above) and F2 (behind), not over a panel
                // By the key, F1 or F2 (ViewNow): F1 whether our camera or the game's own is up there, and
                // never by ThirdPerson() - VR F1 in perspective runs as g_view 2, it read as F2 and took
                // [hud_third]'s zeros ("whatever tilt I set, nothing changes", 2026-10-06)
                const int key = ViewNow();
                const int view = menu ? -1 : key == 1 ? 0 : key == 2 ? 1 : -1;
                static int toldView = -2;
                if (view != toldView) {
                    toldView = view;
                    LogF("vrcam: mouse pointer depth for FlatVR: %s (top %.1f middle %.1f bottom %.1f)", view == 0 ? "F1" : view == 1 ? "F2" : "none - on the screen",
                         view < 0 ? 0.0f : g_set.pointerAt[view][0].load(), view < 0 ? 0.0f : g_set.pointerAt[view][1].load(),
                         view < 0 ? 0.0f : g_set.pointerAt[view][2].load());
                }
                // the curve through the top, middle and bottom depths: FlatVR takes a + b v + c v^2 (v 0 top, 1 bottom)
                // VR F2 with mouse look: the pointer is the crosshair, held in the middle - one depth, its own.
                const bool cross = view == 1 && g_set.platform.load() == 1 && ShooterActive() && KeyMoveMode();
                static int toldCross = -1;
                if ((int)cross != toldCross) {
                    toldCross = cross;
                    if (cross) LogF("vrcam: crosshair depth for FlatVR: %.1f ([hud_third] crosshair_depth)", g_set.crosshairDepth.load());
                }
                if (FloorPointerAuto()) {}   // F3: on the ground by itself, FloorPointerTick
                else if (view < 0) hud::SetPointerDepth(0.0f, 0.0f, 0.0f, 1.0f);
                else if (cross) {}   // F2's crosshair: CrosshairDepthTick, with the head's look down
                else {
                    const float T = g_set.pointerAt[view][0].load() * 0.005f, M = g_set.pointerAt[view][1].load() * 0.005f,
                                B = g_set.pointerAt[view][2].load() * 0.005f;
                    hud::SetPointerDepth(T, 4.0f * M - 3.0f * T - B, 2.0f * B - 4.0f * M + 2.0f * T, g_set.pointerTopSize[view].load() * 0.01f);
                }
            }
        }
        if (nowMs >= nextSlow) {   // twice a second
            nextSlow = nowMs + 500;
            static ULONGLONG nextCrashKeep = 0;
            if (nowMs >= nextCrashKeep) { nextCrashKeep = nowMs + 2000; crash::Keep(); }
            FlatVr3DTick();
            xr::FollowBridge();
            fx::dlsseyes::Install();
            prevscan::Tick();
            const bool iniChanged = ReloadIfChanged();
            if (iniChanged) {
                g_gen.fetch_add(1);
                g_renderDirty.store(true);
                g_mouseLookOn.store(MouseLookForView());
            }
            FollowMode();
            d2rsig::Resolve(g_ctx);   // what waits for its page to be decrypted
            GameCodeTick(iniChanged);
            if (!AllHooksIn()) InstallHooks();
            ApplyRender();
            HookGameWindow();
            OpenShared();
            UpdateFovFromFlatVR();
            HookXInput();
            ReportXInputUse();
            fx::TryRegister();
            InstallBiomeHook();
            InstallLabelHooks();
            InstallKeyMoveHook();
            InstallStickHook();
            InstallMapClickHook();
            HookCursor();
            CrosshairTick();
            FlatPadTick();
            ApplyBackgroundSleep();
            static bool toldLayer = false;
            if (!toldLayer && hud::LayerFound()) { toldLayer = true; Log("vrcam: the interface's layer found (R16G16B16A16_FLOAT cleared to 0,0,0,1) - [hud] hide works on it"); }
            if (const bool world = d2rcam::InWorld(); world != toldWorld) {
                toldWorld = world;
                Log(world ? "vrcam: the game's world camera is up" : "vrcam: no world camera (menu)");
            }
        }
    }
    if (timer) CloseHandle(timer);
    return 0;
}

}  // namespace

// ReShade from the game's folder as ReShade64.dll, loaded here - only ever
// under D2RLoader. As dxgi.dll it broke the plain game: D2R.exe checks the
// signature of the dxgi.dll it loads, ReShade's has none, and the game stopped
// with "Failed to initialize graphics system" (blz-log: "LoadLibrary(dxgi.dll)
// Failed", 0x80090006). Loaded under any other name ReShade hooks dxgi and
// D3D12 itself; plugins load before the game builds its renderer, so it is in
// time. With a dxgi.dll still in the folder (the game ships none: it is
// ReShade the old way) that one loads later by itself, and a second ReShade
// is not wanted.
void LoadReShade() {
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    while (n > 0 && dir[n - 1] != L'\\') --n;
    dir[n] = 0;
    std::wstring proxy = std::wstring(dir) + L"dxgi.dll";
    if (GetFileAttributesW(proxy.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Log("vrcam: dxgi.dll in the game's folder (ReShade the old way) - the plain game (D2R.exe) will not start with it there; "
            "rename it to ReShade64.dll and vrcam loads it");
        return;
    }
    wchar_t path[MAX_PATH];
    wcscpy_s(path, dir);
    wcscat_s(path, L"ReShade64.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { Log("vrcam: no ReShade64.dll beside the game - no fog, sky or FlatVR picture"); return; }
    if (LoadLibraryW(path)) Log("vrcam: ReShade loaded from ReShade64.dll");
    else LogF("vrcam: ReShade64.dll did not load (error %lu)", GetLastError());
}

static const char g_info_version[] = "0.157.0";

static const PluginInfo g_info = {
    PluginInfoSize, D2RL_PLUGIN_ABI_VERSION, "d2r-vr-vrcam", "vrcam", g_info_version, "BodyWalkVR",
    "D2R in a headset or as a first-person game: head-driven camera through BodyWalk's D2R Bridge, mouse look and W A S D. "
    "F12 off / first / third person, F11 recenter, F9 mouse look.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* ctx) noexcept {
    if (ctx == nullptr) return true;
    g_ctx = ctx;
    g_base = ctx->exeBase;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&UpdateThread, &g_self);

    // d2r_vr.ini beside this DLL
    DWORD n = GetModuleFileNameW(g_self, g_iniPath, MAX_PATH);
    while (n > 0 && g_iniPath[n - 1] != L'\\') --n;
    g_iniPath[n] = 0;
    wcscat_s(g_iniPath, L"d2r_vr.ini");
    crash::Install(g_info_version);   // first: a start that dies leaves its last step in d2r_vr_start.txt
    crash::Step("settings (d2r_vr.ini)");
    xr::SetLogger(&Log);   // before the settings: [openxr] on says so in the log
    ReloadIfChanged();
    NoTemporalAA();
    g_mouseLookOn.store(MouseLookForView());
    LogF("vrcam %s: game build %s (%s), made for D2R 3.3.93787 under D2RLoader 1.3.1", g_info_version,
         ctx->buildVersion ? ctx->buildVersion : "?", ctx->buildName ? ctx->buildName : "?");
    // Every game address this build has, before any hook rewrites the bytes it is found by.
    crash::Step("game addresses (signatures)");
    d2rsig::SetShiftTest(GetPrivateProfileIntW(L"debug", L"sig_shift_test", 0, g_iniPath));
    d2rsig::Resolve(ctx);

    crash::Step("ReShade (ReShade64.dll and its add-ons, FlatVR's depth add-on among them)");
    LoadReShade();
    crash::Step("camera callbacks, MinHook, the hero's skeleton");
    d2rcam::SetCallbacks(&VrView, &VrProj);
    d2rcam::SetFrameCallback(&VrFrame);
    d2rcam::SetRayOverride(&VrRay);
    MH_Initialize();   // XInput only; game code is hooked through the loader
    skel::SetGameBase(g_base);
    skel::TrackHero(true);   // the hero's SkeletonInstance is how his facing is found (FacingStructural)
    skel::SetFresh(&FreshYawInModel);
    skel::SetFreshHands(&FreshHands);
    crash::Step("the game's hooks");
    InstallHooks();
    crash::Step("XInput hooks");
    HookXInput();
    crash::Step("shared memory (D2R Bridge, FlatVR)");
    OpenShared();
    OpenAfrBlock();
    crash::Step("game state");
    gamestate::Init(ctx);
    crash::Step("game commands");
    gamecmd::Init(ctx);
    crash::Step("update thread and console command");
    if (HANDLE h = CreateThread(nullptr, 0, UpdateThread, nullptr, 0, &g_updateThreadId)) CloseHandle(h);
    if (!ctx->RegisterConsoleCommand("vrcam", &CmdVrcam, "vrcam - the camera: off / first person / third person (F12)"))
        ctx->LogError("vrcam: the console command did not register");
    crash::Step(nullptr);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    xr::Shutdown();   // the session before anything it runs on goes; FlatVR started again
    g_enabled.store(false);
    d2rcam::SetEnabled(false);
    if (fx::g_registered) { reshade::unregister_addon(g_self); fx::g_registered = false; }
    if (g_gameWnd && g_gameWndProc) SetWindowLongPtrW(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)g_gameWndProc);
    if (g_updateThreadId) PostThreadMessageW(g_updateThreadId, WM_QUIT, 0, 0);
    crash::Uninstall();
    g_ctx = nullptr;
}
