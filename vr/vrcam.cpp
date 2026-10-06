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
#include <unordered_set>
#include <string>
#include <vector>

#include <shlobj.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <D2RLPlugin/api.h>
#include <reshade.hpp>

namespace probe {
void Run(const float hero[3], const wchar_t* outPath);
void FindHero(const float hero[3], const wchar_t* outPath, const char* tag);
int FindHeroMatrices(const float hero[3], uintptr_t* out, int max);
}
#include "skeletons.h"
#include "writewatch.h"
namespace uitrace { void Register(); bool Arm(const std::wstring& path); }
#include "game_hud_shared.h"
namespace memdiff { void Step(uintptr_t base, const std::wstring& outPath, void (*log)(const char*)); void Reset(void (*log)(const char*)); }
namespace hud { void Register(); void SetHide(int mode); void SetInterfaceScale(float s); void SetMenuOpen(bool open); void SetMapMode(int mode); bool LayerFound(); void SetLogger(void (*log)(const char*)); void SetLook(const FlatVRGameHudLook& look); void SetMapCorner(int corner); bool ToggleMapShown();
                void SetPointer(uint32_t mode); void SetPointerDepth(float base, float tilt, float curve, float topScale); int Anchor(int panel); void SetForearm(int panel, bool valid, const float elbow[3], const float wrist[3], const float across[3]);
                void SetClassicView(bool on); void SetPictureZoom(float bar, float map); bool PictureWanted(); void SetPictureReady(bool ready);
                bool PictureNow(int i, float box[4], uint64_t* srv); void SetPictureBarMoved(bool moved); void SetPictureBarOffset(float x, float y);
                void SetLabels(bool on); bool LabelsNow(float keepBar[4], float keepMap[4], uint64_t* srv); void SetUiMask(bool on); bool UiMaskNow(); }
#include "d2rcam.h"
#include "mat4.h"
namespace gamestate { void Init(const D2RL::PluginContext* ctx); void Tick(); uint32_t WeaponClass(); uint32_t WeaponSet(); uint32_t WeaponType(); uint32_t HandsHeld(); uint32_t TwoHanded(); uint32_t WeaponHand(); bool MenuOpen(); void SetViewMode(uint32_t mode); bool AutoMapOpen(); bool SetAutoMap(bool open); }
#include "d2r_vr_state.h"

#include "MinHook.h"
#include "crosshair_cursor.h"
#include "d2r_vr_shared.h"
#include "afr_eye_shared.h"
#include "pad_mirror_shared.h"

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace {

const PluginContext* g_ctx = nullptr;
uintptr_t g_base = 0;
HMODULE g_self = nullptr;


#pragma optimize("", off)
bool SafeRead(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool SafeWriteMem(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
#pragma optimize("", on)

// The game's bytes at rva are the ones expected (another game build: no).
bool Matches(uint64_t rva, const uint8_t* sig, size_t n) {
    return g_ctx != nullptr && g_ctx->CheckExpectedBytes(rva, sig, n);
}


void Log(const char* text) {
    if (g_ctx != nullptr) g_ctx->LogInfo(text);
}
void LogF(const char* fmt, ...);
// To the game's console as well as the log.
void Say(const char* text) {
    if (g_ctx == nullptr) return;
    g_ctx->LogInfo(text);
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
    std::atomic<float> labelsSizeTop{1.0f}, labelsSizeThird{1.0f}, labelsSizeBody{1.0f};
    // bar_x / bar_y per view: the toolbar moved on the screen, uv (+x right, +y DOWN; the ini's bar_y is + up, %)
    std::atomic<float> barXTop{0.0f}, barYTop{0.0f}, barXThird{0.0f}, barYThird{0.0f}, barXFloor{0.0f}, barYFloor{0.0f};
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
    std::atomic<int>   armsMode{0};        // 0 game animation, 1 test pose (arms ahead), 2 controllers
    std::atomic<bool>  hideHead{false};     // shrink the hero's head away (first person)
    std::atomic<float> armScale{1.0f};      // reach on top of the hero/user height ratio
    std::atomic<bool>  bodywalkPad{true};   // [input] bodywalk_pad: the game's pad is BodyWalk's own report (pad_mirror_shared.h) - no virtual pad needed
    std::atomic<int>   inventoryPad{0};     // [input] inventory_button: the pad button "D2R: Inventory" sends - 0 Menu (Start), 1 View (Back)
    std::atomic<bool>  aAttackOnly{false};  // [input] a_attack_only: pad A never picks up or interacts; "D2R: Pick up / interact" does
    std::atomic<bool>  flatKeyMove{true};   // [input] flat_keyboard_move: flat W A S D walk through the game's own keyboard move (0x8A960), no pad
    std::atomic<bool>  flatNoPad{true};     // [input] flat_no_pad: with it, flat mode shows the game no pad at all (its UI never turns to A/B/X/Y)
    std::atomic<bool>  flatCrosshair{true};  // [input] flat_crosshair: flat mouse look, the pointer is our crosshair, not the game's gauntlet
    std::atomic<bool>  flatClickShoot{true}; // [input] flat_click_shoot: flat mouse look, a click with no target under the crosshair = a shot there, never a walk
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
    std::atomic<int>   pipelineDepth{0};    // the presented frame is this many of its eye's views older than the newest (D3D12 queueing)   // the frame stamp strip for FlatVR (bottom-right corner)
    std::atomic<bool>  topStereo{false};    // the game's own view from above (F12 off) in stereo too: each eye turned about the hero
    std::atomic<float> topAngle{3.0f};      // that turn between the eyes, degrees
    std::atomic<bool>  trueScale{true};     // eyes and zero-parallax plane from the FlatVR screen and the user's height: the world 1:1
    std::atomic<float> eyeMm{63.0f};        // the user's own eye distance, millimetres
    std::atomic<float> eyeHeightM{1.64f};   // the user's eyes above the floor STANDING: metres -> world units, seated or not
    std::atomic<bool>  pairPerTick{false};  // AFR: both eyes drawn from one game frame (the frame drawn twice), not by turns
    std::atomic<float> rightDtMs{0.01f};    // [stereo] right_dt_ms: the frame time the right pass of a pair gets (0 = none)
    std::atomic<bool>  bgFullSpeed{true};   // no Sleep(10) per frame while the game window is not in front
    std::atomic<bool>  gameHeightFog{false};   // [render] game_height_fog: keep the game's own height fog (gamefog::)
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
// this list - [sky] outdoor in the ini replaces it - gets no sky.
SRWLOCK g_biomeLock = SRWLOCK_INIT;
std::vector<std::string> g_outdoor;
char g_biome[96] = "";                  // last name the game set, bare (no folder, no extension), lower case
std::atomic<uint32_t> g_biomeGen{0};    // bumped by every SetCurrentBiome

void LoadOutdoorBiomes() {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"sky", L"outdoor", L"act1_outdoors,act2_outdoors,act2_town,act3_jungle,act3_docktown,act4_mesa,expansion_town,expansion_siege,expansion_mountaintop,expansion_ruins,expansion_ruins_snow",
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

void StartMatrixWatch();   // [debug] matrix_writer, below PickHeroMatrix

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
    g_set.labelsAlphaFloor.store(std::clamp(IniF(L"hud_floor", L"labels_alpha", 0.5f), 0.0f, 1.0f));
    g_set.labelsSizeFloor.store(std::clamp(IniF(L"hud_floor", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsNativeFloor.store(IniF(L"hud_floor", L"labels_native", 1.0f) != 0.0f);
    g_set.labelsAlphaTop.store(std::clamp(IniF(L"hud_top", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaThird.store(std::clamp(IniF(L"hud_third", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaBody.store(std::clamp(IniF(L"hud", L"labels_alpha", 1.0f), 0.0f, 1.0f));
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
    {   // [hud] how FlatVR shows the toolbar and the map (game_hud_shared.h)
        FlatVRGameHudLook look = FlatVRGameHudDefaultLook();
        look.bar_anchor = (uint32_t)std::clamp((int)IniF(L"hud", L"bar_place", (float)look.bar_anchor), 0, 4);
        look.bar_roll_deg = std::clamp(IniF(L"hud", L"bar_roll", 0.0f), -180.0f, 180.0f);
        look.bar_tip_deg = std::clamp(IniF(L"hud", L"bar_tip", 0.0f), -90.0f, 90.0f);
        look.bar_spin_deg = std::clamp(IniF(L"hud", L"bar_spin", 0.0f), -180.0f, 180.0f);
        look.bar_lift_cm = std::clamp(IniF(L"hud", L"bar_lift", 0.0f), -10.0f, 20.0f);
        look.bar_side_cm = std::clamp(IniF(L"hud", L"bar_side", 0.0f), -20.0f, 20.0f);
        look.bar_width_m = std::clamp(IniF(L"hud", L"bar_width", look.bar_width_m), 0.05f, 1.5f);
        look.bar_along_cm = std::clamp(IniF(L"hud", L"bar_along", look.bar_along_cm), -30.0f, 40.0f);
        look.bar_split = (uint32_t)std::clamp((int)IniF(L"hud", L"bar_split", 0.0f), 0, 2);   // two halves side by side, both orbs at one end
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
    g_set.stampPixels.store(IniB(L"stereo", L"stamp_pixels", true));
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
    g_set.fogCurve.store(std::clamp(IniF(L"fog", L"curve", 1.6f), 0.3f, 4.0f));
    g_set.fogBlur.store(std::clamp(IniF(L"fog", L"blur", 3.0f), 0.0f, 12.0f));
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
    g_set.armsMode.store(std::clamp((int)IniF(L"arms", L"mode", 0.0f), 0, 2));
    g_set.hideHead.store(IniB(L"arms", L"hide_head", false));
    g_set.armScale.store(std::clamp(IniF(L"arms", L"scale", 1.0f), 0.3f, 3.0f));
    g_set.poseOrderLog.store(IniB(L"debug", L"pose_order", false));
    g_set.frameLog.store(IniB(L"debug", L"frame_log", false));
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
std::atomic<float> g_rightX{0.0f};      // last right stick X the game polled, -1..1
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
    const float raw[3] = {s->headYawDeg, s->pitchValid ? s->headPitchDeg : 0.0f, s->rollValid ? s->headRollDeg : 0.0f};
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
struct SkyView { float proj[4]; float axes[9]; float eyeRel[3]; bool proj_ok, axes_ok; };   // eyeRel: the eye less the hero, world units
// The game on the floor's bounds (D2R_DepthFog.fx TableKey): the ground the game's
// own camera would show. Its view x projection and the hero, from the same view build.
struct TableBox { float vp[16]; float hero[3]; bool ok; };
TableBox g_tableBox{};   // under g_skyLock
SRWLOCK g_skyLock = SRWLOCK_INIT;
SkyView g_skyView[2]{};

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
SRWLOCK g_lock = SRWLOCK_INIT;
std::string g_buf;
constexpr uint64_t kDrawCounterRva = 0x33ED6D8, kFrameTimeRva = 0x27D31D0;   // as RVA_DRAW_COUNTER / RVA_FRAME_TIME below

double UsNow() {
    static const double k = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1e6 / (double)f.QuadPart; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * k;
}
uint32_t Pass() { uint32_t v = 0; SafeRead(&v, (void*)(g_base + kDrawCounterRva), 4); return v; }
float Dt() { float v = -1.0f; SafeRead(&v, (void*)(g_base + kFrameTimeRva), 4); return v; }

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
              "# C: t_us,C,pass,eye,addr:yaw,...                                             - every hero matrix copy at the pose (address: low 20 bits)\n", f);
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
bool LabelsWanted() {
    if (FloorView()) return g_inWorld.load() && g_set.labelsAlphaFloor.load() < 0.995f && !LabelBoxNative();
    return AfrOn() && !g_enabled.load() && g_set.classicTop.load() == 0 &&
           (std::abs(g_set.labelsNear.load()) > 0.01f || std::abs(g_set.labelsTilt.load()) > 0.01f);
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

void VrFrame() { AfrNextFrame(); }

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
bool VrView(const d2rcam::WorldView& in, float out[16]) {
    AcquireSRWLockExclusive(&g_lookLock);
    memcpy(g_lookAt, in.lookAt, sizeof g_lookAt);
    g_lookOk = in.lookAtValid;
    ReleaseSRWLockExclusive(&g_lookLock);
    if (TopStereoOn()) return TopStereoView(in, out);
    if (!g_enabled.load() || !in.lookAtValid) return false;
    const float* G = in.gameView;   // row-major, v * M: column i is the game camera's axis i in the world
    const float* L = in.lookAt;
    V3 fwd, ahead, right, up, eye, hang;
    float yaw = 0.0f;
    if (TableView()) {
        if (!TableCamera(in, &eye, &fwd, &up, &right, &ahead, &yaw)) return false;
        hang = eye;
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
    g_viewBuilds.fetch_add(1);
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
        TrueScale(&ipd, &conv);
        const float half = 0.5f * ipd;
        view[12] += g_eye.load() == 0 ? half : -half;
        camPos = eye + right * (g_eye.load() == 0 ? -half : half);
    }
    // When this eye's view was built: the head pose it is drawn for is the
    // one of this moment (PredictHead carries it on to now). FlatVR places
    // the screen at the head pose of this stamp and the headset's own
    // reprojection does the rest - see FlatVRAfrHalves.
    if (g_afrBlock) {
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
        g_afrBlock->stamp_magic = FLATVR_AFR_STAMP_MAGIC;
    }
    {   // Row-major, v*M: the columns are the camera's axes in the world.
        AcquireSRWLockExclusive(&g_skyLock);
        SkyView& sv = g_skyView[g_eye.load() & 1];
        for (int i = 0; i < 3; ++i) { sv.axes[i] = view[i*4]; sv.axes[3 + i] = view[i*4 + 1]; sv.axes[6 + i] = view[i*4 + 2]; }
        sv.eyeRel[0] = camPos.x - L[0]; sv.eyeRel[1] = camPos.y - L[1]; sv.eyeRel[2] = camPos.z - L[2];
        sv.axes_ok = true;
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
    return true;
}

// Our projection: the FlatVR screen's frustum (or [camera] fov), the near
// clip that cuts the hero's own head away, and in AFR each eye's off-axis
// shift so what lies at `convergence` sits on the screen.
bool VrProj(const d2rcam::WorldView& in, float M[16]) {
    if (!g_enabled.load()) return false;
    const float ratio = in.viewportH > 0.0f ? in.viewportW / in.viewportH : 0.0f;
    const float aspect = ratio > 0.1f && ratio < 10.0f ? ratio : 16.0f / 9.0f;
    g_lastAspect.store(aspect);
    // The table: nothing of the hero to cut away, and the head may come down close to the game - 5 cm.
    const float nearZ = TableView() ? 0.05f * g_set.tableScale.load() : TopPersp() ? 0.5f : std::max(g_set.nearClip.load(), 0.05f);
    g_lastNear.store(nearZ);
    d2rcam::m4::PerspectiveRevZ(CameraFov() * 3.14159265f / 180.0f, aspect, nearZ, M);
    // The frustum IS the FlatVR screen: each side by its own size at its distance.
    if (float sw, sh, sd; g_set.fovFromFlatVR.load() && LiveScreen(&sw, &sh, &sd)) { M[0] = 2.0f * sd / sw; M[5] = 2.0f * sd / sh; }
    // Off-axis frustum per eye: what lies at `convergence` gets no parallax and
    // sits on the screen, farther goes in behind it. With parallel frusta the
    // whole world was in front of the screen and read as shallow.
    float ipd = g_set.ipd.load(), conv = g_set.convergence.load();
    TrueScale(&ipd, &conv);
    if (AfrOn() && conv > 0.0f) {
        const float shift = M[0] * 0.5f * ipd / conv;
        M[8] += g_eye.load() == 0 ? shift : -shift;
    }
    AcquireSRWLockExclusive(&g_skyLock);
    SkyView& sv = g_skyView[g_eye.load() & 1];
    sv.proj[0] = M[0]; sv.proj[1] = M[5]; sv.proj[2] = M[8]; sv.proj[3] = M[9]; sv.proj_ok = true;
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
    const bool staffNow = g_set.staffTwoHands.load() && gamestate::WeaponType() == D2RVR_TYPE_STAFF;
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
    if (out && unit && unit == g_heroUnit.load()) {
        g_logicFacingDeg.store(atan2f(out[1], out[0]) * 57.2957795f);
        g_logicFacingAt.store(QpcUs());
    }
    if (!out || (uintptr_t)_ReturnAddress() != g_base + RVA_AIM_FACING_RET || !AimByVector()) return;
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
constexpr uint64_t RVA_ATTACK_POINT = 0x18BAD0;
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
    if (dir && caller == 0x14484A && AimByVector() && HandGridDir(&use[0], &use[1])) {
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

void TurnStick(XINPUT_STATE* s) {
    if (!s || !g_enabled.load()) return;
    const XINPUT_GAMEPAD in = s->Gamepad;
    g_aHeld.store((in.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen());
    g_skillHeld.store((in.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0 &&
                      !gamestate::MenuOpen());
    const bool lt = in.bLeftTrigger >= 128, rt = in.bRightTrigger >= 128;
    if (in.wButtons != g_padTrace.buttons || lt != (g_padTrace.lt >= 128) || rt != (g_padTrace.rt >= 128)) {
        LogF("pad: buttons %04X LT %u RT %u stick %d %d", in.wButtons, in.bLeftTrigger, in.bRightTrigger, in.sThumbLX, in.sThumbLY);
        g_padTrace.buttons = in.wButtons; g_padTrace.lt = in.bLeftTrigger; g_padTrace.rt = in.bRightTrigger;
    }
    // A menu (inventory, trade, stash...) gets the pad as it is: the left stick
    // moves its cursor on the screen, so turning it by the camera's yaw sent the
    // cursor the wrong way, and the right stick is the menu's too.
    if (gamestate::MenuOpen()) { g_rightX.store(0.0f); return; }
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
        g_rightX.store(s->Gamepad.sThumbRX / 32767.0f);
        s->Gamepad.sThumbRX = 0; s->Gamepad.sThumbRY = 0;
    }
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
    uint8_t* flag = (uint8_t*)(g_base + RVA_INTERACT_ON_ATTACK);
    uint8_t now = 0;
    if (!SafeRead(&now, flag, 1) || now > 1) return;
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
    uint8_t* flag = (uint8_t*)(g_base + RVA_INTERACT_ON_ATTACK);
    uint8_t f = 1;
    if (SafeRead(&f, flag, 1) && f == 0) {   // A only attacks: ask as if it interacted, for us
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
                           Matches(RVA_HUD_CHAT_CHECK, kSigHudChatCheck, sizeof kSigHudChatCheck);
        if (ok.exchange(found ? 1 : 0) < 0)
            LogF(found ? "vrcam: chat line flag found - W A S D go to the game while typing"
                       : "vrcam: chat line flag NOT found on this build - W A S D stay ours while typing");
    }
    return ok.load() == 1;
}

bool ChatFlag() {
    uint8_t v = 0;
    return ChatFlagFound() && SafeRead(&v, (void*)(g_base + RVA_UI_STATES + kUiChat), 1) && v == 1;
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
    float x = 0.0f, y = 0.0f;
    const char* from = "";
    if (!FlatKeyMode() || !FlatMoveInput(&x, &y, &from)) return OrigKeyMove();
    volatile uint8_t* keys = (volatile uint8_t*)(g_base + RVA_MOVE_KEYS);
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
    if (!g_ctx || g_keyMove.load() != 0 || g_set.platform.load() != 0 || !g_set.flatKeyMove.load()) return;
    if (!Matches(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_keyMove.store(2);
            Log("vrcam: flat: keyboard move hook NOT possible (0x8A960 not as expected - another build?) - W A S D walk the old way, through a pad");
        }
        return;
    }
    const bool in = g_ctx->InstallInlineHook(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove, (void*)&HookKeyMove, (void**)&OrigKeyMove);
    g_keyMove.store(in ? 1 : 2);
    Log(in ? "vrcam: flat: keyboard move hook in - W A S D walk through the game's own keyboard move, the mouse clicks where you look"
           : "vrcam: flat: keyboard move hook FAILED - W A S D walk the old way, through a pad");
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
        g_set.flatClickShoot.load() && FlatKeyMode() && ShooterActive()) {
        const int idx = ((ClientIndexFn)(g_base + RVA_CLIENT_INDEX))(unit);
        if (idx >= 0 && idx < 8 && !((HoverUnitFn)(g_base + RVA_HOVER_UNIT))(idx)) {
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
    if (!g_ctx || g_mapClick.load() != 0 || g_set.platform.load() != 0 || !g_set.flatKeyMove.load()) return;
    if (!Matches(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick) || !Matches(RVA_CLIENT_INDEX, kSigClientIndex, sizeof kSigClientIndex) ||
        !Matches(RVA_HOVER_UNIT, kSigHoverUnit, sizeof kSigHoverUnit)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_mapClick.store(2);
            Log("vrcam: flat: click hook NOT possible (0xFE3B0 / 0x9A820 / 0xF1900 not as expected - another build?) - a click on the ground walks");
        }
        return;
    }
    const bool in = g_ctx->InstallInlineHook(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick, (void*)&HookMapClick, (void**)&OrigMapClick);
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
    if (g_keyMove.load() == 1 && FlatKeyMode() && ShooterActive() && GameFocused() && keys && calls == lastCalls &&
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
    if (FlatKeyMode() || !ShooterActive() || !GameFocused()) return;
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
    if (t_xDepth == 0 && idx == 0 && s && !FlatKeyMode() && PadFromBodyWalk(s)) r = ERROR_SUCCESS;
    if (t_xDepth == 0 && idx == 0 && s) KeysAsStick(s, &r);
    if (r == ERROR_SUCCESS && t_xDepth == 0) {
        if (idx == 0) ApplyActions(s);
        TurnStick(s);
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

bool CrosshairNow() { return g_set.flatCrosshair.load() && FlatKeyMode() && ShooterActive(); }

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
    const uint32_t gen = g_crossGen.load();
    if (gen == built || g_set.platform.load() != 0) return;
    built = gen;
    AcquireSRWLockShared(&g_crossLock); std::wstring f = g_crossFile; ReleaseSRWLockShared(&g_crossLock);
    std::wstring full = f;
    if (!f.empty() && f.find(L':') == std::wstring::npos && f.rfind(L"\\\\", 0) != 0) {
        wchar_t dir[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
        while (n && dir[n - 1] != L'\\') --n;
        dir[n] = 0;
        full = std::wstring(dir) + L"reshade-shaders\\Textures\\" + f;
    }
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

// The size for the game window's height: 32 px below 900, 48 below 1500, 64 above.
HCURSOR Crosshair() {
    RECT rc{};
    const int h = g_gameWnd && GetClientRect(g_gameWnd, &rc) ? rc.bottom : 1080;
    return g_cross[h < 900 ? 0 : h < 1500 ? 1 : 2].load();
}

HCURSOR WINAPI HookSetCursor(HCURSOR c) {
    if (c && CrosshairNow())
        if (HCURSOR x = Crosshair()) c = x;
    return OrigSetCursor(c);
}

// Only in flat mode; once (user32 is there from the start).
void HookCursor() {
    if (g_crossHooked.load() || g_set.platform.load() != 0) return;
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
    if (msg == WM_KEYDOWN && w == VK_RETURN && !(l & (1 << 30)) && ShooterActive())
        g_chatEnterAt.store(ChatFlagFound() && !ChatFlag() ? GetTickCount64() : 0);
    // Still swallowed with the keyboard move (FlatKeyMode): HookKeyMove's vector
    // replaces 0x8A960's answer, never adds to it, so a W A S D the player bound
    // to the game's own Move keys would change nothing - but by default the game
    // has other commands on them (W swaps weapons), which would fire with every step.
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR) && ShooterActive() && !ChatOpen()) {
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
    if (ShooterActive() && FlatKeyMode()) {
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
        if (Matches(rva, want ? kTen : kZero, 5) && g_ctx->PatchBytes(rva, want ? kTen : kZero, 5, want ? kZero : kTen, 5)) ++done;
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
                     Matches(RVA_GET_RAW_FRAME_TIME, kSigGetRawFrameTime, sizeof kSigGetRawFrameTime) ? 1 : 0;
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
    if (!toldTry) { toldTry = true; Log("vrcam: pair per game frame - drawing the first pair"); }
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

    SetEye(0);
    d2rcam::Refresh();
    OrigDrawGameScreen(a);

    float* dt = (float*)(g_base + RVA_FRAME_TIME);
    float* rawDt = (float*)(g_base + RVA_RAW_FRAME_TIME);
    const float keep = *dt, keepRaw = *rawDt;
    // [stereo] right_dt_ms: the right pass gets this sliver of time instead of none - effects
    // that skip a frame of no time (butterflies seen in the left eye only, 2026-10-04) draw then.
    const float rightDt = g_set.rightDtMs.load() * 0.001f;
    *dt = rightDt; *rawDt = rightDt;
    SetEye(1);
    d2rcam::Refresh();
    const uintptr_t r = OrigDrawGameScreen(a);
    *dt = keep; *rawDt = keepRaw;

    g_pairNow.store(false);
    if (live) g_shared = live;
    --depth;

    static ULONGLONG since = GetTickCount64();
    static uint32_t pairs = 0;
    ++pairs;
    if (!told) { told = true; Log("vrcam: pair per game frame ON - left eye, then right eye with the frame time held"); }
    if (const ULONGLONG now = GetTickCount64(); now - since >= 10000) {
        LogF("vrcam: %.1f pairs/s from one game frame each", pairs * 1000.0 / (double)(now - since));
        since = now; pairs = 0;
    }
    return r;
}

// Quiet: tried twice a second until the page is decrypted, logs only once it is in.
void InstallBiomeHook() {
    static bool failed = false;
    if (g_h.biome || failed || !g_ctx || !Matches(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome)) return;
    g_h.biome = g_ctx->InstallInlineHook(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome, (void*)&HookSetBiome, (void**)&OrigSetBiome);
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
    if (!((IsHdFn)(g_base + RVA_IS_HD))()) return shown;   // the old graphics: their text has no scale
    const float s = ((UiScaleFn)(g_base + RVA_UI_SCALE))();
    if (!(s > 0.05f && s < 20.0f)) return shown;
    const int32_t most[2] = {0x7FFFFFFF, 0x7FFFFFFF};   // as the layout asks: one line, no limit
    int32_t was[2] = {}, now[2] = {};
    const TextMeasureFn measure = (TextMeasureFn)(g_base + RVA_TEXT_MEASURE);
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
        const bool in = g_ctx->InstallInlineHook(RVA_LABEL_PAINT, kSigLabelPaint, sizeof kSigLabelPaint, (void*)&HookLabelPaint, (void**)&OrigLabelPaint);
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
    if (!drawIn) drawIn = g_ctx->InstallInlineHook(RVA_TEXT_DRAW, kSigTextDraw, sizeof kSigTextDraw, (void*)&HookTextDraw, (void**)&OrigTextDraw);
    const bool in = drawIn && g_ctx->InstallInlineHook(RVA_LABEL_LAYOUT, kSigLabelLayout, sizeof kSigLabelLayout, (void*)&HookLabelLayout, (void**)&OrigLabelLayout);
    sizeFailed = !in;
    g_labelSizeIn.store(in);
    Log(in ? "vrcam: item-label size hooks in - on the floor [hud_floor] labels_size sets the labels' size"
           : "vrcam: item-label size hooks FAILED - labels_size does nothing");
}

void InstallHooks() {
    if (!g_ctx) return;
    const PluginContext* ctx = g_ctx;
    if (!g_h.camera) {   // cleanroom/camera + drawdist; safe to call again until it is all in
        g_h.camera = d2rcam::Install(ctx);
        LogF("vrcam: camera and render distance %s", g_h.camera ? "in (cleanroom)" : "NOT in yet (code not decrypted, or another build)");
    }
    if (!g_h.skeleton) g_h.skeleton = ctx->InstallInlineHook(RVA_COMPUTE_SELF_WORLD_POSE, kSigComputeSelfWorldPose, sizeof kSigComputeSelfWorldPose, skel::Detour(), skel::OrigSlot());
    if (!g_h.target) {
        g_h.target = ctx->InstallInlineHook(RVA_ATTACK_TARGET, kSigAttackTarget, sizeof kSigAttackTarget, (void*)&HookAttackTarget, (void**)&OrigAttackTarget);
        LogF("vrcam: controller target hook %s", g_h.target ? "in" : "NOT in (code not decrypted yet, or another build)");
    }
    if (!g_h.facing) {
        g_h.facing = ctx->InstallInlineHook(RVA_UNIT_FACING, kSigUnitFacing, sizeof kSigUnitFacing, (void*)&HookUnitFacing, (void**)&OrigUnitFacing);
        LogF("vrcam: attack facing hook %s", g_h.facing ? "in" : "NOT in");
    }
    if (!g_h.point) {
        g_h.point = ctx->InstallInlineHook(RVA_ATTACK_POINT, kSigAttackPoint, sizeof kSigAttackPoint, (void*)&HookAttackPoint, (void**)&OrigAttackPoint);
        LogF("vrcam: attack point hook %s", g_h.point ? "in" : "NOT in");
    }
    if (!g_h.interact) {
        g_h.interact = ctx->InstallInlineHook(RVA_INTERACT_TARGET, kSigInteractTarget, sizeof kSigInteractTarget, (void*)&HookInteractTarget, (void**)&OrigInteractTarget);
        g_interactHooked.store(g_h.interact);
        LogF("vrcam: interact target hook %s", g_h.interact ? "in (pick up never attacks)" : "NOT in (pick up may attack where there is nothing)");
    }
    if (!g_h.blit) {
        g_h.blit = ctx->InstallInlineHook(RVA_DRAW_GAME_SCREEN, kSigDrawGameScreen, sizeof kSigDrawGameScreen, (void*)&HookDrawGameScreen, (void**)&OrigDrawGameScreen);
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
    SafeRead(&pass, (void*)(g_base + RVA_DRAW_COUNTER), 4);
    SafeRead(&dt, (void*)(g_base + RVA_FRAME_TIME), 4);
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
    in.staff = own && ((g_set.staffTwoHands.load() && gamestate::WeaponType() == D2RVR_TYPE_STAFF) || XbowLikeStaff() || likeStaff);
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
int ScanRegion(float* p, size_t n, Patch* out, int cap, int* defs) {
    int got = 0;
    __try {
        for (size_t i = 20; i + 10 < n && got < cap; ++i) {
            if (!(p[i + 4] >= 500.0f && p[i + 4] <= 5000.0f) || !Looks(p + i)) continue;
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
    MEMORY_BASIC_INFORMATION mi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mi, sizeof mi);
         a = (uintptr_t)mi.BaseAddress + mi.RegionSize) {
        // guard pages (thread stacks) have PAGE_GUARD in Protect and are skipped with the rest
        if (mi.State != MEM_COMMIT || mi.Type != MEM_PRIVATE || mi.Protect != PAGE_READWRITE || mi.RegionSize > (1ull << 31))
            continue;
        if (g_set.gameHeightFog.load()) break;   // switched back on meanwhile
        Patch found[32];
        const int n = ScanRegion((float*)mi.BaseAddress, mi.RegionSize / sizeof(float), found, 32, &defs);
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

// On the update thread: a scan 1, 4 and 12 s after every biome change (an area's
// definitions are loaded as it is entered), on a thread of its own.
void Tick() {
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

void SkyTick() {
    env::Find();
    gamefog::Tick();
    static uint32_t seenGen = ~0u;
    static std::string biome;
    static int palette = -1;
    if (const uint32_t gen = g_biomeGen.load(); gen != seenGen) {
        seenGen = gen;
        AcquireSRWLockShared(&g_biomeLock); biome = g_biome; ReleaseSRWLockShared(&g_biomeLock);
        palette = SkyPaletteFor(biome, g_set.skyAlways.load());
        const bool world = !biome.empty() && biome.find("frontend") == std::string::npos;
        g_inWorld.store(world);
        g_fogAct.store(world ? ActOfBiome(biome) : 0);
        g_underground.store(world && !IsOutdoorBiome(biome));
        static std::string told = "?";   // no biome name has a question mark
        if (biome != told) {
            told = biome;
            LogF("vrcam: biome '%s' - %s", biome.empty() ? "(none yet)" : biome.c_str(),
                 palette >= 0 ? kPalettes[palette].part : "no sky here (not in [sky] outdoor)");
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

// The views of the toolbar's and the map's copies bound to the effect (hud::PictureNow), 0 = none yet.
uint64_t g_pieceBound[3] = {};   // the toolbar, the map, the labels' layer

void Forget(reshade::api::effect_runtime*) { g_tech[0] = g_tech[1] = {0}; g_pieceBound[0] = g_pieceBound[1] = g_pieceBound[2] = 0; }

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
bool StampWanted() { return g_enabled.load() && g_afrBlock && g_set.stampPixels.load(); }

bool FogWanted() { return g_set.fogOn.load() && g_enabled.load() && !Overhead() && g_inWorld.load() && !gamestate::MenuOpen(); }
// The table (F5): the void black, the game lifted off black - D2R_DepthFog.fx TableKey.
bool TableKeyWanted() { return TableView() && g_inWorld.load() && !gamestate::MenuOpen(); }

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
    if (set) Forget(rt);
    return set;
}

// Switching the technique happens after the effect pass, never inside it:
// turned on from reshade_begin_effects, ReShade went on to draw an effect whose
// resources it had not created yet, and the game died in D3D12 (null+0x19C).
// From here it takes effect on the next frame, with everything in place.
void OnFinishEffects(reshade::api::effect_runtime* rt, reshade::api::command_list*, reshade::api::resource_view, reshade::api::resource_view) {
    if (ApplySkyPictures(rt)) return;   // the effect reloads with the new pictures
    if (!g_tech[0].handle) g_tech[0] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog");
    if (!g_tech[1].handle) g_tech[1] = rt->find_technique("D2R_DepthFog.fx", "D2R_DepthFog_R");
    if (!g_tech[0].handle) { hud::SetPictureReady(false); return; }
    // Off while a menu is open: the fog lies over the whole picture, the
    // inventory and trade panels included, and darkened them.
    const bool want = FogWanted() || g_skyPalette.load() >= 0 || HudWanted() || StampWanted() || hud::PictureWanted() || TableKeyWanted() || PhantomWanted() || BoneAxesWanted();
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

void OnBeginEffects(reshade::api::effect_runtime* rt, reshade::api::command_list*, reshade::api::resource_view, reshade::api::resource_view) {
    if (!g_tech[0].handle) return;
    if (!rt->get_technique_state(g_tech[0]) && !(g_tech[1].handle && rt->get_technique_state(g_tech[1]))) return;
    SetFloat(rt, "NearPlane", g_lastNear.load());
    SetFloat(rt, "FogStart", g_set.fogStart.load());
    SetFloat(rt, "FogEnd", g_set.fogEnd.load());
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
    sv[0] = g_skyView[AfrOn() ? 0 : eye]; sv[1] = g_skyView[AfrOn() ? 1 : eye];
    ReleaseSRWLockShared(&g_skyLock);
    bool sky = pal >= 0;
    for (const SkyView& v : sv) sky = sky && v.proj_ok && v.axes_ok && v.proj[0] != 0.0f && v.proj[1] != 0.0f;
    SetBool(rt, "SkyOn", sky);
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
        if (on) {   // on the floor flat where the game puts them, each eye's own: only faded
            const bool floor = FloorView();
            SetFloat(rt, "LabelBase", floor ? 0.0f : g_set.labelsNear.load() * 0.005f);
            SetFloat(rt, "LabelTilt", floor ? 0.0f : g_set.labelsTilt.load() * 0.005f);
            SetFloat(rt, "LabelAlpha", floor ? g_set.labelsAlphaFloor.load() : 1.0f);
        }
        if (on || mask) {   // the toolbar and the map in the layer: never faded, never keyed away (TableKey)
            SetFloats(rt, "LabelKeep0", keepBar, 4);
            SetFloats(rt, "LabelKeep1", keepMap, 4);
        }
    }
    SetFloat(rt, "GameBarShift", BarNearWanted() ? BarNearNow() * 0.005f : 0.0f);
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
            // the sky's block sets them only while the sky is drawn)
            SkyView sv[2];
            AcquireSRWLockShared(&g_skyLock); sv[0] = g_skyView[0]; sv[1] = g_skyView[1]; ReleaseSRWLockShared(&g_skyLock);
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
                v[e] = g_stampHist[from][(g_stampHistAt[from] - g_set.pipelineDepth.load()) & 3];
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
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&Forget);
    uitrace::Register();   // Ctrl + F10: one frame's render targets to d2r_vr_uitrace.txt
    hud::SetLogger(&Log);
    hud::Register();       // the interface's own layer: [hud] hide
    g_registered = true;
    Log("vrcam: joined ReShade - the fog and the sky follow d2r_vr.ini [fog] [sky]");
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
        hud::SetPictureZoom(BarSizeNow(), InsideFree() ? g_set.mapZoomInside.load() : 1.0f);
        hud::SetPictureBarMoved(BarNearWanted());
        {   // [hud_*] bar_x / bar_y: the toolbar somewhere else on the screen
            float bx = 0.0f, by = 0.0f;
            if (ClassicNow()) BarOffsetNow(&bx, &by);
            hud::SetPictureBarOffset(bx, by);
        }
        hud::SetLabels(LabelsWanted());
        // the fog leaves the interface alone; the floor's key never takes its text for the void
        hud::SetUiMask((fx::FogWanted() || fx::TableKeyWanted()) && !LabelsWanted());
        SkyTick();
        flog::Tick(g_set.frameLog.load());
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
                hud::SetPointer(menu || !firstPerson ? 1u : 2u);
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
                if (view < 0) hud::SetPointerDepth(0.0f, 0.0f, 0.0f, 1.0f);
                else {
                    const float T = g_set.pointerAt[view][0].load() * 0.005f, M = g_set.pointerAt[view][1].load() * 0.005f,
                                B = g_set.pointerAt[view][2].load() * 0.005f;
                    hud::SetPointerDepth(T, 4.0f * M - 3.0f * T - B, 2.0f * B - 4.0f * M + 2.0f * T, g_set.pointerTopSize[view].load() * 0.01f);
                }
            }
        }
        if (nowMs >= nextSlow) {   // twice a second
            nextSlow = nowMs + 500;
            if (ReloadIfChanged()) {
                g_gen.fetch_add(1);
                g_renderDirty.store(true);
                g_mouseLookOn.store(MouseLookForView());
            }
            FollowMode();
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

static const char g_info_version[] = "0.140.0";

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
    ReloadIfChanged();
    g_mouseLookOn.store(MouseLookForView());
    LogF("vrcam %s: game build %s (%s), made for D2R 3.3.93787 under D2RLoader 1.3.1", g_info_version,
         ctx->buildVersion ? ctx->buildVersion : "?", ctx->buildName ? ctx->buildName : "?");

    LoadReShade();
    d2rcam::SetCallbacks(&VrView, &VrProj);
    d2rcam::SetFrameCallback(&VrFrame);
    d2rcam::SetRayOverride(&VrRay);
    MH_Initialize();   // XInput only; game code is hooked through the loader
    skel::SetGameBase(g_base);
    skel::TrackHero(true);   // the hero's SkeletonInstance is how his facing is found (FacingStructural)
    skel::SetFresh(&FreshYawInModel);
    skel::SetFreshHands(&FreshHands);
    InstallHooks();
    HookXInput();
    OpenShared();
    OpenAfrBlock();
    gamestate::Init(ctx);
    if (HANDLE h = CreateThread(nullptr, 0, UpdateThread, nullptr, 0, &g_updateThreadId)) CloseHandle(h);
    if (!ctx->RegisterConsoleCommand("vrcam", &CmdVrcam, "vrcam - the camera: off / first person / third person (F12)"))
        ctx->LogError("vrcam: the console command did not register");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_enabled.store(false);
    d2rcam::SetEnabled(false);
    if (fx::g_registered) { reshade::unregister_addon(g_self); fx::g_registered = false; }
    if (g_gameWnd && g_gameWndProc) SetWindowLongPtrW(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)g_gameWndProc);
    if (g_updateThreadId) PostThreadMessageW(g_updateThreadId, WM_QUIT, 0, 0);
    g_ctx = nullptr;
}
