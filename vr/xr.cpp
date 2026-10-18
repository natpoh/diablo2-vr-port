// Native OpenXR, see xr.h and docs/plan_native_openxr.md.
//
// The loader is Khronos' (OpenXR-SDK, Apache-2.0), linked in statically. The session
// runs on the game's own D3D12 device and direct queue; each eye's picture is copied
// out of the back buffer into a swapchain image where FlatVR's add-on takes it today
// (vrcam's replay: the left eye at the hold, the right one before its present), and
// one xrEndFrame a pair hands both to the runtime with the poses they were drawn for.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <d3dcompiler.h>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")

#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "xr.h"
#include "d2r_vr_shared.h"
#include "link.h"
#include "direct_feed.h"
#include "room_walk_wire.h"
#include "afr_eye_shared.h"

// vrcam.cpp: the balls over the belt's bottles a hand is at (x y z in BodyWalk's frame, radius m, alpha, r g b)
int D2rvrHoverSpheres(float (*out)[8], int max);
#include "hud_native.h"
#include "plate_world.h"   // [hud] monster_bar_room: the monster's health bar over its head, in the room (HangPlate)
#include "rtv_slots.h"     // the render-target views of the interface chains, handed out and given back (EnsureHudOut)
#include "reshade_shield.h"
#include "panel_input.h"
#include "xr_layers.h"
#include "focus_gate.h"
#include "xr_failure.h"   // when the runtime fails and the session is given up (xr_failure.h)
#include "bounded_call.h"   // [openxr] busy_guard: xrWaitFrame and xrBeginSession on a thread of their own, waited for with a limit (bounded_call.h)
#include "flatvr_gate.h"    // ... and no session over a FlatVR that holds the headset and cannot be stopped (flatvr_gate.h)
#include "gpu_fault.h"   // the D3D12 device lost: told at once, nothing touched after it (vr/gpu_fault.h)
#include "view_gate.h"
#include "views3d.h"   // [openxr] views_3d: F1 as a window, F3 as the room (the pure maths)
#include "eye_gap.h"   // [openxr] ipd_auto / ipd_mm: the eye distance of the stereo pair - the runtime's or the player's (the pure maths)
#include "eye_frame.h"   // [openxr] canted_eyes: both eyes drawn and submitted in the head's frame when the displays are canted (the pure maths)
#include "hset_panel.h"   // the in-headset settings panel ([openxr] headset_settings): vr/xr_headset_settings.h
#include "hset_life.h"
#include "low_hp_game.h"   // its low-life pages show the vignette as a preview (lowhp::SetPreview)
#include "hset_paint.h"
#include "hset_store.h"
#include "calibration.h"
#include "haptics.h"
#include "haptic_stop.h"   // [openxr] haptics_stop: each pulse stopped by an explicit call when its time is up (a thread of its own)
#include "anchor_gate.h"   // [openxr] menus_wait_tracking: the flat picture and the menu panel placed only with the head tracked
#include "frame_trace.h"   // [debug] frame_trace: what the runtime's haptic call costs a pair
#include "laser_tooltip.h"
#include "render_resolution.h"
#include "hset_ray.h"   // vectors, a ray against a panel, the pad values, the release gate: for any panel hung in the headset
#include "hset_view.h"   // the settings' view buttons F1 - F4: the controller's own view path
#include "skill_wheel.h"   // [openxr] skill_wheel: the skill wheel in the headset (vr/xr_skill_wheel.h is its layer)
#include "rune_shader.h"   // [openxr] runes_draw: BodyWalk's gesture runes in the headset (vr/xr_runes.h is their layer; rune_geom.h the maths)
#include "wheel_paint.h"
#include "xr_profiles.h"   // [openxr] frame_profile, index_grip_force: Steam Frame's own layout, the Index grip by force
#include "xr_stick_check.h"   // is the thumbstick bound by the runtime? (the report of 2026-10-18: SteamVR kept a binding saved for the program)
#include "hand_vitals_wire.h"   // [hud] hand_vitals: life and mana on the hand when the palm turns to the face (vr/xr_hand_vitals.h is its layer)
#include "wrist_status_wire.h"   // [hud] wrist_status: companions and effects over a wrist turned to the eyes (vr/xr_wrist_status.h is its two layers)

namespace gamestate { void SetStickBits(uint32_t bits); }   // the thumbstick check's verdict for D2R VR Settings (vr/gamestate.cpp, the state block)

namespace xr {
void GuardSet();
void GuardClear(const char* why);   // the start guard (before LoadSettings)
namespace {

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// [openxr]
struct Cfg {
    bool on = false;          // taken once
    bool stage = false;       // space=stage: the room's floor, else LOCAL (the head at the start)
    bool fovCrop = true;      // fov_crop: each eye's fov narrowed to the window's aspect (sharper, black above and below)
    bool symmetric = true;    // each eye's fov made as wide to the left as to the right (symmetric_fov)
    float ipdFallbackM = 0.064f;   // ipd_fallback_mm: the gap given to two views the runtime located in one place (0 = none)
    bool ipdAuto = true;           // ipd_auto: the eye distance from the runtime's view poses (1), or the player's own number (0) - vr/eye_gap.h
    float ipdManualM = 0.063f;     // ipd_mm: the player's eye distance, metres (taken when ipd_auto is 0), live
    bool cantedEyes = true;        // canted_eyes: canted displays - both eyes drawn and submitted in the head's frame (vr/eye_frame.h); 0 = as before, live
    bool stopFlatVR = true;   // FlatVR stopped before the session, started again after
    bool busyGuard = true;    // busy_guard: the runtime's blocking calls with a limit, no session over a FlatVR that holds the headset (unticked: as before)
    unsigned waitFrameMs = 150;   // wait_frame_ms: how long the draw thread waits for xrWaitFrame (and xrBeginSession) before the VR frame is left out
    float quadM = 2.0f;       // outside a pair: the flat picture this far ahead, metres
    float panelM = 1.6f;      // and while a panel is open in a game ([screen] menu_distance_m), 0 = quadM
    float quadWidthM = 2.4f;  // and this wide
    float leanM = 0.10f;      // the head may move this far from the body sideways (lean_m); farther, the body follows
    bool byTurns = false;     // eyes by turns ([stereo] afr without pair_per_tick): one eye a game frame
    bool frameProfile = true; // frame_profile: Steam Frame's controllers in their own layout when the runtime offers it (vr/xr_profiles.h); taken at a session start
    bool indexGripForce = false;   // index_grip_force: an Index hand's grip is how hard it squeezes (squeeze/force), not the fingers round the handle
    bool menusWaitTracking = true;   // menus_wait_tracking: the flat picture / menu panel placed only once the head is tracked, again after a focus loss (vr/anchor_gate.h)
    bool bareHands = false;   // bare_hands: a hand without a controller (the runtime's hand tracking) works as one; 0: it presses and places nothing
    float handHoldMs = 1000.0f;   // hand_hold_ms: a controller that loses its place this long is held where it was by the head
    float brightness = 1.0f;  // brightness: everything in the headset times this (the runtime's colour scale), 1 = as drawn
    bool laser = false;       // laser: a controller pointing at the flat picture's menus is the mouse (docs/plan_laser_menus.md)
    int laserHand = 1;        // laser_hand: 0 left, 1 right; the other hand's trigger and grip are Ctrl and Shift
    bool laserTooltips = false;   // laser_tooltips: under the laser the game's item tooltip shows at once, with its comparison (vr/laser_tooltip.h)
    bool pointer3d = true;    // pointer_3d: the game's mouse pointer drawn on F1's and F3's ground in 3D (vr/pointer3d.h); clicks and the mouse are not touched
    bool views3d = true;      // views_3d: F1 (a window of the room onto the game) and F3 (the diorama in the room) drawn with the runtime's eyes (vr/views3d.h); 0 = the flat picture
    bool menu3d = true;       // menu_3d: a menu in F4 as a panel of its own, the world in stereo behind it (HangPanel); on since tested (2026-10-13)
    bool headsetSettings = false;   // headset_settings: a settings panel in the headset, opened with both grips squeezed and a thumbstick click (vr/xr_headset_settings.h)
    bool runesDraw = false;   // runes_draw: BodyWalk's gesture runes and the hand's trail drawn in the headset, F4 only (vr/xr_runes.h)
    bool haptics = false;     // haptics: pulses in the controllers (vr/haptics.cpp; the output action is made either way)
    float hapticsScale = 1.0f;   // haptics_scale: their strength times this
    bool hapticsHands = true; // haptics_hand_actions: a vibration action of its own for each hand, no hand filter (taken at a session start)
    bool hapticsStop = true;  // haptics_stop: each pulse stopped by xrStopHapticFeedback when its time is up (live)
    rendersize::Pick vrPick;  // [render] vr_resolution_px / vr_resolution: the render size the game is given (render_size.h); Desktop = the window's own
};
Cfg g_cfg;
// [render] vr_resolution: what the runtime holds per eye (both eyes' limits and the system's swapchain limit), taken when
// a session starts; and the size of the last back buffer a present showed.
std::atomic<uint64_t> g_vrRecommended{0}, g_vrMaximum{0}, g_outputSize{0};
wchar_t g_headsetPath[MAX_PATH] = L"";   // d2r_vr_headset.ini beside d2r_vr.ini (LoadSettings)
// What the runtime says of the headset, for D2R VR Settings (the render size slider's tip says what the headset recommends, with the
// game closed too): [headset] name, recommended_w/h, max_w/h, in a file beside the ini - not in the ini itself, which vrcam reloads
// whenever it changes. Written when a session starts, only what changed.
void WriteHeadsetInfo(const char* name, vrres::Size recommended, vrres::Size maximum) {
    if (!g_headsetPath[0] || !recommended) return;
    auto put = [](const wchar_t* key, uint32_t v) {
        if (GetPrivateProfileIntW(L"headset", key, -1, g_headsetPath) == (int)v) return;
        wchar_t b[24];
        swprintf_s(b, L"%u", v);
        WritePrivateProfileStringW(L"headset", key, b, g_headsetPath);
    };
    put(L"recommended_w", recommended.w);
    put(L"recommended_h", recommended.h);
    put(L"max_w", maximum.w);
    put(L"max_h", maximum.h);
    wchar_t wname[96] = L"", had[96] = L"";
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 96);
    GetPrivateProfileStringW(L"headset", L"name", L"", had, 96, g_headsetPath);
    if (wname[0] && wcscmp(wname, had) != 0) WritePrivateProfileStringW(L"headset", L"name", wname, g_headsetPath);
}
// D2R VR Settings > Status, the "Native OpenXR" row: why there is no session (2026-10-18: FlatVR holds the headset and nothing can stop it). [session] note in the
// same file; empty = deleted.
void WriteSessionNote(const wchar_t* text) {
    if (!g_headsetPath[0]) return;
    wchar_t had[256] = L"";
    GetPrivateProfileStringW(L"session", L"note", L"", had, 256, g_headsetPath);
    if (wcscmp(had, text ? text : L"") == 0) return;
    WritePrivateProfileStringW(L"session", L"note", text && text[0] ? text : nullptr, g_headsetPath);
}
// The eye distance the runtime measured (how far apart xrLocateViews' two poses are, before anything of ours), kept for D2R VR Settings to show
// beside the slider: [headset] ipd_mm in d2r_vr_headset.ini, millimetres with one decimal; 0 = the runtime locates both eyes in one place (it
// reports no distance). Written when it moved by 0.3 mm, at most every 2 s (a runtime that follows a headset's IPD wheel moves it in steps).
void NoteMeasuredIpd(float gapM) {
    static float written = -1.0f;
    static ULONGLONG at = 0;
    const float mm = std::isfinite(gapM) && gapM >= eyegap::kOneSpotM ? gapM * 1000.0f : 0.0f;
    const ULONGLONG now = GetTickCount64();
    if (!g_headsetPath[0] || (written >= 0.0f && (std::fabs(mm - written) < 0.3f || now - at < 2000))) return;
    written = mm;
    at = now;
    wchar_t b[24];
    swprintf_s(b, L"%.1f", mm);
    WritePrivateProfileStringW(L"headset", L"ipd_mm", b, g_headsetPath);
}
// The log: once, which eye distance is used and what the runtime measured; again when the player's choice changes.
void TellIpd(const eyegap::Choice& pick, float measuredM) {
    static int saidWhy = -1;
    static float saidGap = -1.0f;
    if ((int)pick.why == saidWhy && std::fabs(pick.gapM - saidGap) < 0.0005f) return;
    saidWhy = (int)pick.why;
    saidGap = pick.gapM;
    const float mm = measuredM * 1000.0f;
    switch (pick.why) {
    case eyegap::Why::Manual:
        LogF("vrcam: openxr - eye distance: YOURS, %.1f mm ([openxr] ipd_auto=0, ipd_mm); the runtime measured %.1f mm%s", pick.gapM * 1000.0f, mm,
             mm < 20.0f ? " (it locates both eyes in one place)" : "");
        break;
    case eyegap::Why::Fallback:
        LogF("vrcam: openxr - eye distance: the runtime located the eyes %.1f mm apart: %.0f mm is used instead ([openxr] ipd_fallback_mm) - "
             "set your own in D2R VR Authentic Settings > Stereo > Native OpenXR (Eye distance)", mm, pick.gapM * 1000.0f);
        break;
    default:
        LogF("vrcam: openxr - eye distance: automatic, the runtime's %.1f mm ([openxr] ipd_auto=1)", mm);
        break;
    }
}
// The log, once a session: the eyes as the runtime gave them (the gap, the angle between their turns, each eye's turn from the head's frame, each fov)
// and what the pair is drawn and submitted with (vr/eye_frame.h) - the first thing an "eyes crossed, the centre stretched" report needs: are the
// headset's displays canted. Called before the canted frame replaces the turns: `q` are the runtime's own.
bool g_eyesTold = false;
void TellEyes(const XrView views[2], const eyeframe::Quat q[2], const eyeframe::Frame& fr) {
    if (g_eyesTold) return;
    g_eyesTold = true;
    const float k = 57.29578f;
    const XrVector3f &a = views[0].pose.position, &b = views[1].pose.position;
    const float gap = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    const XrFovf &fl = views[0].fov, &fr2 = views[1].fov;
    LogF("vrcam: openxr - eyes %.1f mm apart, %.2f deg between their views; left fov %.1f %.1f %.1f %.1f, right %.1f %.1f %.1f %.1f (left right up down, deg)%s",
         gap * 1000.0f, fr.cantRad * k, fl.angleLeft * k, fl.angleRight * k, fl.angleUp * k, fl.angleDown * k, fr2.angleLeft * k, fr2.angleRight * k,
         fr2.angleUp * k, fr2.angleDown * k, g_cfg.fovCrop ? " - cropped to the window's aspect" : "");
    const eyeframe::Quat head = eyeframe::Mean(q[0], q[1]);
    float yl, pl, rl, yr, pr, rr;
    eyeframe::Relative(head, q[0], &yl, &pl, &rl);
    eyeframe::Relative(head, q[1], &yr, &pr, &rr);
    LogF("vrcam: openxr - each eye's own view from the head's frame (the mean of the two turns): left yaw %+.2f pitch %+.2f roll %+.2f, right yaw %+.2f "
         "pitch %+.2f roll %+.2f deg (yaw + = turned to the left, so outward is + for the left eye and - for the right)", yl, pl, rl, yr, pr, rr);
    if (fr.canted)
        LogF("vrcam: openxr - the displays are canted (%.2f deg between the views): both eyes drawn and submitted in the head's frame, fov left %.1f %.1f %.1f %.1f, "
             "right %.1f %.1f %.1f %.1f (left right up down, deg; each covers its own view) - [openxr] canted_eyes",
             fr.cantRad * k, fr.fov[0].l * k, fr.fov[0].r * k, fr.fov[0].u * k, fr.fov[0].d * k, fr.fov[1].l * k, fr.fov[1].r * k, fr.fov[1].u * k, fr.fov[1].d * k);
    else if (fr.cantRad >= eyeframe::kCantMinRad)
        LogF("vrcam: openxr - the displays are canted (%.2f deg between the views) but [openxr] canted_eyes is 0: the camera is turned as the left eye "
             "for both and each eye is submitted with its own turn, as before - the eyes can come out crossed", fr.cantRad * k);
    else
        LogF("vrcam: openxr - the eyes' views are parallel (%.2f deg between them): each eye drawn and submitted as the runtime gave it", fr.cantRad * k);
}
// XR_KHR_composition_layer_color_scale_bias: the runtime scales each layer's colour - the brightness
// slider at no cost of ours. Without it the picture is as the game draws it.
bool g_colorScaleExt = false;
bool g_final = false;                 // the game is closing: no session again
std::atomic<bool> g_stopWanted{false};  // [openxr] on went to 0 while a session runs: the draw thread ends it

// [openxr] busy_guard: xrWaitFrame has no time limit and the headset may be another app's (FlatVR over BodyWalk Lite on SteamVR: it never returned, with the session lock held,
// the game hung until Task Manager, 2026-10-18). The two calls that can block the draw thread - xrWaitFrame, xrBeginSession - run on a thread of their own; the draw thread
// waits for them g_cfg.waitFrameMs and leaves the VR frame out if they have not come (the game goes on flat), and takes the result when they do. One runner each for a
// session: a call stuck in one is left to it (and the session is not destroyed under it: Teardown).
struct WaitOut { XrResult r = XR_ERROR_RUNTIME_FAILURE; XrFrameState fs{XR_TYPE_FRAME_STATE}; };
bounded::Runner<WaitOut>* g_waitRunner = nullptr;
bounded::Runner<XrResult>* g_beginRunner = nullptr;
bool g_beginPending = false;       // xrBeginSession asked for and not answered yet
double g_waitPendingAt = 0.0;      // UsNow when xrWaitFrame was first found not back (0 = it came)
double g_waitToldAt = 0.0;
enum State { kIdle, kStarting, kCreated, kFailed };
std::atomic<int> g_state{kIdle};
SRWLOCK g_lock = SRWLOCK_INIT;   // the session's calls: the draw thread's frames against Shutdown

XrInstance g_inst = XR_NULL_HANDLE;
XrSystemId g_sys = XR_NULL_SYSTEM_ID;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_space = XR_NULL_HANDLE;
XrSessionState g_ss = XR_SESSION_STATE_UNKNOWN;
std::atomic<bool> g_running{false};   // between xrBeginSession and xrEndSession
std::vector<int64_t> g_formats;       // the runtime's swapchain formats, its order of preference
bool g_flatVrStopped = false;
// The runtime had no headset (XR_ERROR_FORM_FACTOR_UNAVAILABLE: Virtual Desktop or Link not
// streaming yet, the headset asleep or off the head) when the game started: the session was
// given up for the whole run, and with FlatVR not running either nothing brought the head and
// the hands to BodyWalk until native was switched off and on again (the user, 2026-10-13: "after
// a restart of the game BodyWalk picks up no movement"). Tried again every 10 s while FlatVR
// does not run (FollowLink) - over a running FlatVR the player is not left without one.
std::atomic<bool> g_headsetAbsent{false};
std::atomic<ULONGLONG> g_retryAt{0};
constexpr ULONGLONG kRetryMs = 10000;

ID3D12Device* g_dev = nullptr;
ID3D12CommandQueue* g_queue = nullptr;

struct Chain {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D12KHR> img;
    uint32_t w = 0, h = 0;
    DXGI_FORMAT bbFmt = DXGI_FORMAT_UNKNOWN;   // the back buffer's it was made for
    int64_t fmt = 0;
    bool bad = false;   // no format the back buffer copies into: not tried again for this size
};
Chain g_eyeChain[2], g_quadChain;
// The mouse pointer's image over the flat picture (PointerQuad), and the game's window it is in.
HWND g_wnd = nullptr;
struct Pointer { HCURSOR shape = nullptr; XrSwapchain sc = XR_NULL_HANDLE; int n = 0, hotX = 0, hotY = 0; bool ok = false; };
Pointer g_ptr;
// The toolbar and the map hung in the room (HangHud): a swapchain each, and its layer.
struct HudOut {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<ID3D12Resource*> img;
    uint32_t w = 0, h = 0;
    int rtv0 = 0;
    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
};
HudOut g_hudOut[hud::kNativePieces];
constexpr int kRtvs = 32;            // the render-target heap of these chains' images (g_rtvHeap) ...
rtvslots::Slots<kRtvs> g_rtvSlots;   // ... its places: a chain takes one for each image and gives them back when it goes (vr/rtv_slots.h)
HudOut g_plateOut;   // the monster's health bar hung over its head (HangPlate): a swapchain of its own, ONE size (plateworld::kChainW x kChainH), made once, kept to the session's end
// BodyWalk's gesture zones ticked "VR" (HangZones): one atlas swapchain, a ball in a
// cell of it for each zone, and a quad layer each.
constexpr uint32_t kZoneCell = 256, kZoneCols = 4, kZonesMax = 16;
constexpr uint32_t kRuneLayers = 8;   // BodyWalk's gesture runes (HangRunes, xr_runes.h): a picture and a trail each, four at most
HudOut g_zoneOut;
XrCompositionLayerQuad g_zoneLayer[kZonesMax];
uint32_t g_maxLayers = 16;   // the runtime's most layers a frame (xrGetSystemProperties); 16 is what it must take
std::atomic<float> g_aspect{0.0f};   // the back buffer's width over height, once seen

// The frame open now.
enum Kind { kNone, kPair, kMono };
Kind g_kind = kNone;
XrFrameState g_fs{XR_TYPE_FRAME_STATE};
XrView g_views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
bool g_viewsOk = false;
// Each eye's last picture as handed to the runtime: the pose and fov it was drawn with, and its
// swapchain (whose last released image still holds it). Eyes by turns hands the eye not drawn this
// frame in again with these - the runtime turns it to the head as it is (2026-10-11).
struct LastEye { XrPosef pose; XrFovf fov; XrSwapchain sc; bool ok; };
LastEye g_lastEye[2] = {};
// A swapchain image acquired whose wait ran out: waited for again next time (NextImage).
struct PendingImage { XrSwapchain sc; uint32_t idx; };
PendingImage g_pendingImg[16];
bool g_copied[2] = {};
XrFovf g_fovUsed[2] = {};
// [openxr] views_3d (vr/views3d.h)
int g_viewsMode = 0;      // what vrcam asked the next pair to be (xr.h SetViewsMode): kViewsHead, kViewsWindow, kViewsRoom
PointerAtFn g_pointerAt = nullptr;   // vrcam's: the ground under the game's mouse in F1 / F3 (xr.h)
int g_pairMode = 0;       // what the open pair is (OpenFrame)
views3d::Anchor g_winAnchor;    // where F1's window stands (held to the gaze first, brought round after a second 60 degrees off)
views3d::Window g_win{};
views3d::WindowEyes g_winEyes{};
views3d::Placed g_winAt{};
bool g_winOk = false;
int g_lastMode = 0;       // the mode of the last pair that was opened (a window comes up in front of the gaze again after another view)
float g_floorY = 0.0f;    // F3: the room's floor in the layer's space, when the runtime's STAGE gave it
bool g_floorOk = false;
uint32_t g_epoch = 0;     // moves each time the layer's space was recentred (the runtime's, ours, F11) or jumped: F3's diorama is put again

// Our own copy list on the game's queue: a ring of allocators behind one fence.
// Sixteen: a pair frame takes up to five lists (two eyes, the toolbar, the map, the belt's balls);
// with four the fifth waited for this frame's first, which the replay can hold on the game's
// queue - 100 ms every frame, 9 frames/s (2026-10-10). A list's allocator comes round again
// only three frames on.
constexpr int kAllocs = 16;
ID3D12CommandAllocator* g_alloc[kAllocs] = {};
UINT64 g_allocDone[kAllocs] = {};
int g_allocAt = 0;
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Fence* g_fence = nullptr;
UINT64 g_fenceV = 0;
HANDLE g_fenceEvt = nullptr;

// The flat picture (menus, panels, loading): put in the room where the head looks when it
// comes up - a panel opened in first person stands where it was opened - level, at
// [openxr] flat_distance_m, and stays there.
float g_monoYaw = 0.0f;
XrVector3f g_monoPos{};
bool g_monoAnchored = false;
// [openxr] menus_wait_tracking (vr/anchor_gate.h), the frame loop's: may the flat picture and a menu's panel be placed in the room now
bool g_anchorReady = true;
anchorgate::Ready g_anchorGate;
anchorgate::Refocus g_refocus;
int g_lastDrawn = 0;   // the kind of the last frame that went out
// A panel open in a game (inventory, trade, stash...; vrcam's SetPanelOpen): the flat picture
// stands at FlatVR's "Screen distance while a panel is open" ([screen] menu_distance_m) instead -
// the square window's interface is as wide as the picture, and at the menus' distance its sides
// were out of view (2026-10-12). Eased there and back as FlatVR eases its screen (FlatDistance).
std::atomic<bool> g_panelOpen{false};
// [openxr] laser: what the flat picture shows are menus (vrcam's SetLaserWanted: a panel open in a game,
// or no game), and where the last frame hung it - the laser's surface (CloseFrame; not after a pair).
std::atomic<bool> g_laserWanted{false};
// [openxr] headset_settings: the settings panel is up (xr_headset_settings.h): the laser stands down, the hands are the panel's.
std::atomic<bool> g_hsOpen{false};
wchar_t g_iniPath[MAX_PATH] = L"";   // d2r_vr.ini, as LoadSettings was given it
struct FlatShown { XrPosef pose; XrExtent2Df size; bool ok; };
FlatShown g_flatShown{};
// [openxr] menu_3d: where a menu's panel hangs in a pair's frame (HangPanel) - the flat picture's
// rules, kept apart from it; anchored again at a recentre.
struct PanelPlace { bool anchored = false; float yaw = 0.0f; XrVector3f pos{}; double shownUs = 0.0, offSince = 0.0; bool told = false; };
PanelPlace g_panelPlace;

// The body the camera is held to (lean_m): where the head is over the floor, as far as a
// lean goes - the head may lean round it, and walking off in the room drags it along
// instead of taking the camera out of the hero (2026-10-10). Room space, set at the recentre.
float g_bodyX = 0.0f, g_bodyZ = 0.0f;
std::atomic<ULONGLONG> g_bodyHeldAt{0};   // [openxr] roomscale_walk: the room walk holds the body still (HoldBody, a 2.5 s lease: longer than the walker's own 2 s gap,
                                          // so a hitch never lets lean_m drag the body behind its back - the view jumped and the hero chased it)

// Recentre: the yaw and the point between the eyes taken as straight ahead.
std::atomic<bool> g_recentre{true};
// Taken only from a tracked pose on a frame that renders (the first frames after xrBeginSession
// gave the untracked identity: 'yaw -0.0, the head at 0 0 0', 2026-10-10), and after a runtime
// recentre only from its changeTime on: in the new space, not the old one.
XrTime g_recentreAfter = 0;
XrReferenceSpaceType g_spaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // what g_space was made as
int g_framesRunning = 0;   // frames since xrBeginSession
// The head's yaw in the last stereo pair's views from the recentre - what PairEyes turns the eyes
// by - for vrcam's body (PairHeadYaw): one zero for the view and the body (2026-10-10).
std::atomic<float> g_pairYaw{0.0f};
std::atomic<ULONGLONG> g_pairYawAt{0};   // GetTickCount64 when taken, 0 = none
std::atomic<bool> g_pairOpen{false};
std::atomic<bool> g_pairMissed{false};   // the last pair asked for did not open: its yaw kept 2 s, as vrcam keeps its eyes
// The pair's frame was opened and closed empty (the runtime said not to render, or no views):
// its presents open no flat frame of their own - a second xrWaitFrame a pair halved the game to
// 45 pairs/s whenever the headset was not showing it (2026-10-10).
bool g_pairSpent = false;     // a native pair's frame is open (BeginPair ok .. EndPair): its yaw holds however long a pass takes
float g_cYaw = 0.0f;
XrVector3f g_cPos{};

// 10 s statistics
struct Stats { uint32_t frames = 0, pairs = 0, monos = 0, noRender = 0, noViews = 0; double waitUs = 0, waitMax = 0, frameUs = 0; ULONGLONG since = 0; } g_st;
double g_beginUs = 0.0;
PairTiming g_clock{};   // the pair's time inside our calls (xr.h LastPairTiming), reset by BeginPair
xrfail::Streak g_failStreak[3];   // xrWaitFrame, xrBeginFrame, xrEndFrame: the runtime's failed frames in a row (xr_failure.h)
xrfail::Restarts g_restarts;      // sessions made again after a failure

double UsNow() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

const char* Res(XrResult r) {
    static char b[XR_MAX_RESULT_STRING_SIZE];
    if (g_inst && xrResultToString(g_inst, r, b) == XR_SUCCESS) return b;
    snprintf(b, sizeof b, "XrResult %d", (int)r);
    return b;
}
// A failed call to the log, once per call site and result.
bool Ok(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r)) return true;
    gpufault::Check(g_dev, what);   // a call failed: is the device gone? (the first time it is, the report is written)
    static std::vector<std::pair<const char*, int>> told;
    for (const auto& t : told) if (t.first == what && t.second == (int)r) return false;
    if (told.size() < 64) told.push_back({what, (int)r});
    LogF("vrcam: openxr - %s failed: %s", what, Res(r));
    return false;
}

// The game's D3D12 device is gone (vr/gpu_fault.h): nothing of D3D12 or the runtime is touched again - the game ends in a moment on its own failed check, and a call
// on a removed device only fails (or waits for a runtime that is failing too).
bool DeviceLost() {
    if (!gpufault::Removed()) return false;
    static bool told = false;
    if (!told) {
        told = true;
        g_running.store(false);
        Log("vrcam: openxr - the D3D12 device was removed: no frame is submitted and nothing is copied from now on (the report is just above, and in d2r_vr_gpu_fault.txt in the logs folder)");
    }
    return true;
}

const char* StateName(XrSessionState s) {
    switch (s) {
        case XR_SESSION_STATE_IDLE: return "idle";
        case XR_SESSION_STATE_READY: return "ready";
        case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
        case XR_SESSION_STATE_VISIBLE: return "visible";
        case XR_SESSION_STATE_FOCUSED: return "focused";
        case XR_SESSION_STATE_STOPPING: return "stopping";
        case XR_SESSION_STATE_LOSS_PENDING: return "loss pending";
        case XR_SESSION_STATE_EXITING: return "exiting";
        default: return "unknown";
    }
}

const char* FormatName(int64_t f) {
    switch ((DXGI_FORMAT)f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
        default: { static char b[32]; snprintf(b, sizeof b, "DXGI format %d", (int)f); return b; }
    }
}

// The copy needs the same family of formats (a plain CopyTextureRegion casts within
// one typeless group). The back buffer's values are already gamma-encoded: an 8-bit
// one goes into the _SRGB format of its family, so the runtime reads them as what
// they are.
int Family(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 1;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 2;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return 3;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return 4;
        default: return 0;
    }
}
int64_t PickFormat(DXGI_FORMAT bb) {
    const int fam = Family(bb);
    if (!fam) return 0;
    const DXGI_FORMAT best = fam == 1 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : fam == 2 ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                           : fam == 3 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    for (int64_t f : g_formats) if (f == best) return f;
    for (int64_t f : g_formats) if (Family((DXGI_FORMAT)f) == fam) return f;
    return 0;
}

// -- small rotations (x, y, z, w) ---------------------------------------------
XrVector3f Rot(const XrQuaternionf& q, XrVector3f v) {
    const XrVector3f u{q.y * v.z - q.z * v.y + q.w * v.x, q.z * v.x - q.x * v.z + q.w * v.y, q.x * v.y - q.y * v.x + q.w * v.z};
    return {v.x + 2.0f * (q.y * u.z - q.z * u.y), v.y + 2.0f * (q.z * u.x - q.x * u.z), v.z + 2.0f * (q.x * u.y - q.y * u.x)};
}
XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b) {   // b first, then a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrQuaternionf Yaw(float a) { return {0.0f, sinf(0.5f * a), 0.0f, cosf(0.5f * a)}; }   // about +y: + turns left
float YawOf(const XrQuaternionf& q) {
    const XrVector3f f = Rot(q, {0.0f, 0.0f, -1.0f});
    return atan2f(-f.x, -f.z);
}

// -- FlatVR --------------------------------------------------------------------
// FlatVR's head sample moves while FlatVR runs (D2R VR Settings reads it the same way).
uint32_t FlatVRCounter(bool* running) {
    *running = false;
    HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, FLATVR_HEAD_SAMPLE_NAME);
    if (!m) return 0;
    uint32_t c = 0;
    if (const FlatVRHeadSample* s = (const FlatVRHeadSample*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(FlatVRHeadSample))) {
        *running = s->version == FLATVR_HEAD_SAMPLE_VERSION;
        c = s->counter;
        UnmapViewOfFile(s);
    }
    CloseHandle(m);
    return c;
}
// Stopped through Game Link (BodyWalk's request_flatvr_running), then until its
// head sample stands still for half a second (5 s at most): FlatVR holds the headset.
// Does it hold the headset? Its head sample moves (a FlatVR left open with none stands still).
bool FlatVRMoves() {
    bool running = false;
    const uint32_t a = FlatVRCounter(&running);
    for (int i = 0; i < 6; ++i) {
        Sleep(100);
        if (FlatVRCounter(&running) != a) return true;
    }
    return false;
}
// True: the session may start; false: FlatVR holds the headset and nothing can stop it ([openxr] busy_guard).
bool StopFlatVR() {
    bool running = false;
    uint32_t last = FlatVRCounter(&running);
    if (!running) { Log("vrcam: openxr - FlatVR is not running"); return true; }
    uint32_t caps = 0;
    const bool canStop = link::HostAlive(&caps) && (caps & BW_LINK_CAP_FLATVR_RUNNING);
    if (!canStop) {
        const bool moves = g_cfg.busyGuard && FlatVRMoves();
        if (flatgate::Decide(true, false, moves, g_cfg.busyGuard) == flatgate::Act::Refuse) {
            Log("vrcam: openxr - FlatVR runs, holds the headset (its head sample moves) and BodyWalk's Game Link is not there to stop it (BodyWalk Lite?): no session is started "
                "(a second one gets no frames); tried again every 10 s while FlatVR runs");
            return false;
        }
        Log("vrcam: openxr - FlatVR runs but BodyWalk's Game Link is not there to stop it - the session may not get the headset");
        return true;
    }
    link::RequestFlatVR(false);
    g_flatVrStopped = true;
    const ULONGLONG t0 = GetTickCount64();
    ULONGLONG still = t0;
    while (GetTickCount64() - t0 < 5000) {
        Sleep(100);
        const uint32_t c = FlatVRCounter(&running);
        if (c != last) { last = c; still = GetTickCount64(); }
        if (GetTickCount64() - still >= 500) break;
    }
    LogF("vrcam: openxr - FlatVR stopped through Game Link (%.1f s)", (GetTickCount64() - t0) / 1000.0);
    Sleep(300);
    return true;
}

// -- the controllers: our own actions, published for BodyWalk -------------------
// Grip and aim poses, triggers, grips, sticks and the face buttons of both hands,
// suggested for the controllers OpenXR runtimes have. Read once a frame at the
// frame's display time and written into Game Link's XrInput: BodyWalk takes them
// in place of FlatVR's (gestures, the Mapping, the virtual pad).
XrActionSet g_actSet = XR_NULL_HANDLE;
XrAction g_aGrip = XR_NULL_HANDLE, g_aAim = XR_NULL_HANDLE, g_aTrigger = XR_NULL_HANDLE, g_aTriggerClick = XR_NULL_HANDLE,
         g_aSqueeze = XR_NULL_HANDLE, g_aStick = XR_NULL_HANDLE, g_aStickClick = XR_NULL_HANDLE, g_aPrimary = XR_NULL_HANDLE,
         g_aSecondary = XR_NULL_HANDLE, g_aMenu = XR_NULL_HANDLE;
XrAction g_aSqueezeForce = XR_NULL_HANDLE;   // the Index grip's force ([openxr] index_grip_force); bound on Index only
// A spare thumbstick, bound to the same inputs as g_aStick under a name no saved binding knows (vr/xr_stick_check.h): SteamVR keeps bindings per program and per
// action, so a binding saved for D2RLoader.exe by another mod can leave g_aStick with no source. Read in its place only where the runtime says the first has
// none and the spare has (g_stickSpare), nowhere else.
XrAction g_aStickSpare = XR_NULL_HANDLE;
bool g_stickSpare = false;                                  // the spare is the thumbstick (xrstick::UseSpare)
xrstick::Verdict g_stickVerdict = xrstick::Verdict::Unknown;
ULONGLONG g_stickCheckAt = 0;                               // the next look while the verdict is unknown (tick count)
int g_stickChecks = 0;
// The finger ON a control (capacitive touch): bound on the controllers that have it (Touch - PS VR2 on SteamVR is one - and Index), nothing on the others.
// Read into g_touch (vr/direct_input.h TouchBit) for the layout's "<control>_touch" lines; BodyWalk is not given them.
XrAction g_aTouchTrigger = XR_NULL_HANDLE, g_aTouchStick = XR_NULL_HANDLE, g_aTouchPrimary = XR_NULL_HANDLE, g_aTouchSecondary = XR_NULL_HANDLE;
uint32_t g_touch[2] = {};     // the last frame's TouchBit bits of each hand
int g_touchLooked[2] = {};    // frames a controller was in the hand with no touch input reported (told once)
bool g_touchSaid[2] = {};     // this hand's touch inputs told in the log
bool g_frameExt = false;   // XR_VALVE_frame_controller_interaction enabled in this instance ([openxr] frame_profile)
XrAction g_aHaptic = XR_NULL_HANDLE;   // output: a pulse in a controller ([openxr] haptics, vr/haptics.cpp)
// [openxr] haptics_hand_actions: instead of the one action filtered by hand, an action per hand ([0] left, [1] right) with only that
// hand's motor bound - some runtimes routed a hand-filtered pulse to the wrong hand (bearstonem/d2r-pcvr, steamvr/haptic_actions.h).
XrAction g_aHapticHand[2] = {};
// [openxr] haptics_stop: the timer that stops each pulse (vr/haptic_stop.h); kept for the process's life. g_hapticStopLock guards the
// session and actions it uses: Teardown takes it exclusively and clears g_hapticStopOk before they go.
hapticstop::Timer* const g_hapticTimer = new hapticstop::Timer;
SRWLOCK g_hapticStopLock = SRWLOCK_INIT;
bool g_hapticStopOk = false;
XrPath g_handPath[2] = {XR_NULL_PATH, XR_NULL_PATH};   // /user/hand/left, /user/hand/right
XrSpace g_gripSpace[2] = {}, g_aimSpace[2] = {}, g_viewSpace = XR_NULL_HANDLE;
// What BodyWalk gets is in the room's STAGE space, the floor at 0, as FlatVR gives it (its jump
// and squat go by the head's height); no stage: LOCAL.
XrSpace g_roomSpace = XR_NULL_HANDLE;
// Bare hands (XR_EXT_hand_tracking): a hand the runtime tracks itself, no controller in it. A
// runtime may make a controller of it (a pinch for the trigger): its buttons fired the game's
// actions (2026-10-10). Unless [openxr] bare_hands=1 such a hand presses nothing and is no hand
// for BodyWalk. Only with XR_EXT_hand_tracking_data_source, which says a hand is seen free:
// without it a runtime may build the hand from the controller held in it (SteamVR does) and
// every controller would count as a bare hand - then hands are taken as controllers, as before.
bool g_handSourceExt = false;
XrHandTrackerEXT g_handTracker[2] = {};
PFN_xrCreateHandTrackerEXT g_xrCreateHandTracker = nullptr;
PFN_xrDestroyHandTrackerEXT g_xrDestroyHandTracker = nullptr;
PFN_xrLocateHandJointsEXT g_xrLocateHandJoints = nullptr;
int g_bareSaid[2] = {-1, -1};
bool g_actionsOk = false;
bool g_inSent = false;     // XrInput written this session: its end is told too
D2RVR_XrInput g_inNow{};   // the last frame's, for vrcam's own arms
bool g_menuIsOptions = false;   // PS VR2 under the Touch profile: the left menu action is the RIGHT controller's Options button (told in the log; nothing is moved)
char g_systemName[XR_MAX_SYSTEM_NAME_SIZE] = {};   // the runtime's name for the headset ("SteamVR/OpenXR : playstation_vr2"): the log's reading of a controller that shows as another

XrPath Path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g_inst, s, &p);
    return p;
}
XrAction MakeAction(const char* name, const char* shown, XrActionType type) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(ci.actionName, name);
    strcpy_s(ci.localizedActionName, shown);
    ci.actionType = type;
    ci.countSubactionPaths = 2;
    ci.subactionPaths = g_handPath;
    XrAction a = XR_NULL_HANDLE;
    Ok(xrCreateAction(g_actSet, &ci, &a), "xrCreateAction");
    return a;
}
// One controller's bindings: (action, path) pairs, both hands spelled out. A
// profile the runtime does not know is refused whole: logged, the others go on.
// `touch`: the profile's touch inputs, on top. A runtime that refuses the whole list over them (a path it does not have) is asked again without them, so the
// controller's buttons, sticks and poses never depend on the touch.
void Suggest(const char* profile, std::initializer_list<std::pair<XrAction, const char*>> list,
             std::initializer_list<std::pair<XrAction, const char*>> touch = {}) {
    std::vector<XrActionSuggestedBinding> b;
    for (const auto& [a, path] : list) if (a) b.push_back({a, Path(path)});
    const size_t base = b.size();
    for (const auto& [a, path] : touch) if (a) b.push_back({a, Path(path)});
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = Path(profile);
    sb.countSuggestedBindings = (uint32_t)b.size();
    sb.suggestedBindings = b.data();
    XrResult r = xrSuggestInteractionProfileBindings(g_inst, &sb);
    if (XR_FAILED(r) && b.size() > base) {
        LogF("vrcam: openxr - touch inputs for %s refused (%s): the profile goes without them", profile, Res(r));
        sb.countSuggestedBindings = (uint32_t)base;
        r = xrSuggestInteractionProfileBindings(g_inst, &sb);
    }
    if (XR_FAILED(r)) LogF("vrcam: openxr - bindings for %s refused: %s", profile, Res(r));
}

bool MakeActions() {
    g_handPath[0] = Path("/user/hand/left");
    g_handPath[1] = Path("/user/hand/right");
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(si.actionSetName, "d2rvr");
    strcpy_s(si.localizedActionSetName, "D2R VR");
    if (!Ok(xrCreateActionSet(g_inst, &si, &g_actSet), "xrCreateActionSet")) return false;
    g_aGrip = MakeAction("grip", "Hand", XR_ACTION_TYPE_POSE_INPUT);
    g_aAim = MakeAction("aim", "Pointer", XR_ACTION_TYPE_POSE_INPUT);
    g_aTrigger = MakeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    g_aTriggerClick = MakeAction("trigger_click", "Trigger click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aSqueeze = MakeAction("squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
    g_aStick = MakeAction("stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g_aStickSpare = MakeAction("thumbstick_xy", "Thumbstick (spare)", XR_ACTION_TYPE_VECTOR2F_INPUT);
    g_aStickClick = MakeAction("stick_click", "Thumbstick press", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aPrimary = MakeAction("primary", "A / X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aSecondary = MakeAction("secondary", "B / Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aMenu = MakeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aSqueezeForce = MakeAction("squeeze_force", "Grip force", XR_ACTION_TYPE_FLOAT_INPUT);
    g_aTouchTrigger = MakeAction("touch_trigger", "Trigger touch", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aTouchStick = MakeAction("touch_stick", "Thumbstick touch", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aTouchPrimary = MakeAction("touch_primary", "A / X touch", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aTouchSecondary = MakeAction("touch_secondary", "B / Y touch", XR_ACTION_TYPE_BOOLEAN_INPUT);
    // Made and bound whether [openxr] haptics is ticked or not (it switches live, and an action cannot be
    // added to an attached set): ticked off, nothing is ever played through it.
    g_aHaptic = g_aHapticHand[0] = g_aHapticHand[1] = XR_NULL_HANDLE;
    if (g_cfg.hapticsHands) {   // one per hand, no subaction filter: the binding alone says which motor
        for (int h = 0; h < 2; ++h) {
            XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
            strcpy_s(ci.actionName, h ? "haptic_right" : "haptic_left");
            strcpy_s(ci.localizedActionName, h ? "Right hand vibration" : "Left hand vibration");
            ci.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
            if (!Ok(xrCreateAction(g_actSet, &ci, &g_aHapticHand[h]), "xrCreateAction(haptic hand)")) g_aHapticHand[h] = XR_NULL_HANDLE;
        }
        if (!g_aHapticHand[0] || !g_aHapticHand[1]) g_aHapticHand[0] = g_aHapticHand[1] = XR_NULL_HANDLE;
    }
    if (!g_aHapticHand[0]) g_aHaptic = MakeAction("haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT);
    LogF("vrcam: openxr - vibration: %s", g_aHapticHand[0] ? "an action for each hand ([openxr] haptics_hand_actions)" : "one action, by hand");
#define BOTH(a, p) {a, "/user/hand/left/input/" p}, {a, "/user/hand/right/input/" p}
#define MOTORS(a) {a, "/user/hand/left/output/haptic"}, {a, "/user/hand/right/output/haptic"},                   {g_aHapticHand[0], "/user/hand/left/output/haptic"}, {g_aHapticHand[1], "/user/hand/right/output/haptic"}   // every profile we suggest has a motor (a null action is left out)
    Suggest("/interaction_profiles/oculus/touch_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aSqueeze, "squeeze/value"),
             BOTH(g_aStick, "thumbstick"), BOTH(g_aStickSpare, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"),
             {g_aPrimary, "/user/hand/left/input/x/click"}, {g_aPrimary, "/user/hand/right/input/a/click"},
             {g_aSecondary, "/user/hand/left/input/y/click"}, {g_aSecondary, "/user/hand/right/input/b/click"},
             {g_aMenu, "/user/hand/left/input/menu/click"}, MOTORS(g_aHaptic)},
            {BOTH(g_aTouchTrigger, "trigger/touch"), BOTH(g_aTouchStick, "thumbstick/touch"),
             {g_aTouchPrimary, "/user/hand/left/input/x/touch"}, {g_aTouchPrimary, "/user/hand/right/input/a/touch"},
             {g_aTouchSecondary, "/user/hand/left/input/y/touch"}, {g_aTouchSecondary, "/user/hand/right/input/b/touch"}});
    Suggest("/interaction_profiles/valve/index_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aTriggerClick, "trigger/click"),
             BOTH(g_aSqueeze, "squeeze/value"), BOTH(g_aStick, "thumbstick"), BOTH(g_aStickSpare, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"),
             BOTH(g_aPrimary, "a/click"), BOTH(g_aSecondary, "b/click"), BOTH(g_aSqueezeForce, "squeeze/force"), MOTORS(g_aHaptic)},
            {BOTH(g_aTouchTrigger, "trigger/touch"), BOTH(g_aTouchStick, "thumbstick/touch"), BOTH(g_aTouchPrimary, "a/touch"), BOTH(g_aTouchSecondary, "b/touch")});
    Suggest("/interaction_profiles/htc/vive_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aTriggerClick, "trigger/click"),
             BOTH(g_aSqueeze, "squeeze/click"), BOTH(g_aStick, "trackpad"), BOTH(g_aStickSpare, "trackpad"), BOTH(g_aStickClick, "trackpad/click"), BOTH(g_aMenu, "menu/click"), MOTORS(g_aHaptic)});
    Suggest("/interaction_profiles/microsoft/motion_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aSqueeze, "squeeze/click"),
             BOTH(g_aStick, "thumbstick"), BOTH(g_aStickSpare, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"), BOTH(g_aMenu, "menu/click"), MOTORS(g_aHaptic)});
    Suggest("/interaction_profiles/khr/simple_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "select/click"), BOTH(g_aMenu, "menu/click"), MOTORS(g_aHaptic)});
#undef BOTH
#undef MOTORS
    if (g_frameExt) {   // Steam Frame's own layout (vr/xr_profiles.h): the left hand's D-pad down / up are its face buttons
        std::vector<XrActionSuggestedBinding> b;
        for (const xrprofiles::Binding& fb : xrprofiles::kFrameBindings) {
            using xrprofiles::Act;
            const XrAction a = fb.act == Act::Grip ? g_aGrip : fb.act == Act::Aim ? g_aAim : fb.act == Act::Trigger ? g_aTrigger
                             : fb.act == Act::Squeeze ? g_aSqueeze : fb.act == Act::Stick ? g_aStick : fb.act == Act::StickClick ? g_aStickClick
                             : fb.act == Act::Primary ? g_aPrimary : fb.act == Act::Secondary ? g_aSecondary : fb.act == Act::Menu ? g_aMenu : g_aHaptic;
            if (a) b.push_back({a, Path(fb.path)});
            if (fb.act == Act::Stick && g_aStickSpare) b.push_back({g_aStickSpare, Path(fb.path)});   // the spare thumbstick, same input
        }
        XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        sb.interactionProfile = Path(xrprofiles::kFrameProfile);
        sb.countSuggestedBindings = (uint32_t)b.size();
        sb.suggestedBindings = b.data();
        const XrResult r = xrSuggestInteractionProfileBindings(g_inst, &sb);
        LogF("vrcam: openxr - Steam Frame controllers: their own layout %s", XR_SUCCEEDED(r) ? "suggested" : Res(r));
    }
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo ai{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        ai.subactionPath = g_handPath[h];
        ai.poseInActionSpace.orientation.w = 1.0f;
        ai.action = g_aGrip;
        Ok(xrCreateActionSpace(g_session, &ai, &g_gripSpace[h]), "xrCreateActionSpace(grip)");
        ai.action = g_aAim;
        Ok(xrCreateActionSpace(g_session, &ai, &g_aimSpace[h]), "xrCreateActionSpace(aim)");
    }
    XrReferenceSpaceCreateInfo vs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    vs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    vs.poseInReferenceSpace.orientation.w = 1.0f;
    Ok(xrCreateReferenceSpace(g_session, &vs, &g_viewSpace), "xrCreateReferenceSpace(VIEW)");
    vs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (XR_FAILED(xrCreateReferenceSpace(g_session, &vs, &g_roomSpace))) g_roomSpace = XR_NULL_HANDLE;
    LogF("vrcam: openxr - poses for BodyWalk in the %s space", g_roomSpace ? "stage" : "local (no stage)");
    if (g_xrCreateHandTracker && g_xrLocateHandJoints && !g_handSourceExt)
        Log("vrcam: openxr - bare hands: the runtime cannot tell them from controllers (no XR_EXT_hand_tracking_data_source) - taken as controllers");
    if (g_handSourceExt && g_xrCreateHandTracker && g_xrLocateHandJoints) {
        for (int h = 0; h < 2; ++h) {
            XrHandTrackerCreateInfoEXT hi{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
            hi.hand = h ? XR_HAND_RIGHT_EXT : XR_HAND_LEFT_EXT;
            hi.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            const XrHandTrackingDataSourceEXT both[2] = {XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT, XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT};
            XrHandTrackingDataSourceInfoEXT ds{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};
            ds.requestedDataSourceCount = 2;
            ds.requestedDataSources = (XrHandTrackingDataSourceEXT*)both;
            hi.next = &ds;
            if (XR_FAILED(g_xrCreateHandTracker(g_session, &hi, &g_handTracker[h]))) g_handTracker[h] = XR_NULL_HANDLE;
        }
        LogF("vrcam: openxr - bare hands: %s", !g_handTracker[0] && !g_handTracker[1] ? "no hand trackers (taken as controllers)"
                                            : g_cfg.bareHands ? "tracked, taken as controllers ([openxr] bare_hands=1)" : "tracked, they press nothing");
    }
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &g_actSet;
    if (!Ok(xrAttachSessionActionSets(g_session, &at), "xrAttachSessionActionSets")) return false;
    Log("vrcam: openxr - controllers: our actions attached, published for BodyWalk (Game Link)");
    return true;
}

// A located pose, only as far as the runtime vouches for it (2026-10-10): its turn finite and of
// unit length (made exactly so), its place only with POSITION_VALID - a hand needs both (else
// none); the head may be a turn alone (valid 2), held at `held`, where it was last located.
struct LocateSeen { XrResult r; XrSpaceLocationFlags flags; };
void PoseOut(XrSpace space, D2RVR_XrPose* out, float* held = nullptr, LocateSeen* seen = nullptr) {
    *out = {};
    if (seen) *seen = {XR_ERROR_HANDLE_INVALID, 0};
    if (!space) return;
    XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
    const XrResult lr = xrLocateSpace(space, g_roomSpace ? g_roomSpace : g_space, g_fs.predictedDisplayTime, &l);
    if (seen) *seen = {lr, XR_SUCCEEDED(lr) ? l.locationFlags : 0};
    if (XR_FAILED(lr)) return;
    const bool rot = (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    const XrQuaternionf& q = l.pose.orientation;
    const XrVector3f& p = l.pose.position;
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!rot || !std::isfinite(n2) || n2 < 0.5f || n2 > 2.0f) return;
    const bool pos = (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    if (!pos && !held) return;
    const float k = 1.0f / sqrtf(n2);
    out->rot[0] = q.x * k; out->rot[1] = q.y * k; out->rot[2] = q.z * k; out->rot[3] = q.w * k;
    if (pos) {
        out->pos[0] = p.x; out->pos[1] = p.y; out->pos[2] = p.z;
        if (held) memcpy(held, out->pos, sizeof out->pos);
    } else {
        memcpy(out->pos, held, sizeof out->pos);
    }
    out->valid = pos ? 1u : 2u;
}
float FloatOf(XrAction a, int h, bool* active) {
    if (!a) return 0.0f;
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a; gi.subactionPath = g_handPath[h];
    XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(g_session, &gi, &st)) || !st.isActive) return 0.0f;
    *active = true;
    return st.currentState;
}
bool BoolOf(XrAction a, int h, bool* active) {
    if (!a) return false;
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a; gi.subactionPath = g_handPath[h];
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(g_session, &gi, &st)) || !st.isActive) return false;
    *active = true;
    return st.currentState != XR_FALSE;
}
// What the runtime bound our actions to, in its own words, per action: "menu: Left Hand Menu Button". A controller that shows as another
// (PS VR2 on SteamVR presents itself as Touch: the log of 2026-10-17 had nothing for its Create / Options buttons) says here whether the
// menu action reached any button at all, and which. One line a change (the profile is read again whenever the runtime says it changed).
std::vector<XrPath> BoundSources(XrAction action) {
    std::vector<XrPath> paths;
    if (!action || !g_session) return paths;
    XrBoundSourcesForActionEnumerateInfo bi{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
    bi.action = action;
    uint32_t n = 0;
    if (XR_SUCCEEDED(xrEnumerateBoundSourcesForAction(g_session, &bi, 0, &n, nullptr)) && n) {
        paths.resize(n);
        if (XR_FAILED(xrEnumerateBoundSourcesForAction(g_session, &bi, n, &n, paths.data()))) paths.clear();
    }
    return paths;
}
std::string g_boundTold;   // the last text of LogBoundSources: a profile read twice with the same bindings is told once - and again in the next session (Teardown clears it)
void LogBoundSources() {
    struct Act { const char* name; XrAction action; };
    const Act acts[] = {{"thumbstick", g_aStick}, {"thumbstick (spare action)", g_aStickSpare}, {"stick click", g_aStickClick},
                        {"menu", g_aMenu}, {"primary (A / X)", g_aPrimary}, {"secondary (B / Y)", g_aSecondary},
                        {"trigger", g_aTrigger}, {"squeeze (grip)", g_aSqueeze}, {"touch of the trigger", g_aTouchTrigger}, {"touch of the stick", g_aTouchStick},
                        {"touch of A / X", g_aTouchPrimary}, {"touch of B / Y", g_aTouchSecondary}};
    std::string& told = g_boundTold;
    std::string text;
    for (const Act& a : acts) {
        if (!a.action) continue;
        text += a.name;
        text += ": ";
        const std::vector<XrPath> paths = BoundSources(a.action);
        if (paths.empty()) text += "nothing bound";
        for (size_t i = 0; i < paths.size(); ++i) {
            XrInputSourceLocalizedNameGetInfo li{XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO};
            li.sourcePath = paths[i];
            li.whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT | XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;
            uint32_t len = 0;
            char name[160] = {};
            if (XR_SUCCEEDED(xrGetInputSourceLocalizedName(g_session, &li, 0, &len, nullptr)) && len > 1 && len <= sizeof name)
                xrGetInputSourceLocalizedName(g_session, &li, (uint32_t)sizeof name, &len, name);
            uint32_t pl = 0;
            char path[96] = {};
            xrPathToString(g_inst, paths[i], (uint32_t)sizeof path, &pl, path);
            text += (i ? ", " : "");
            text += name[0] ? name : "(no name)";
            text += " [";
            text += path;
            text += "]";
        }
        text += " | ";
    }
    if (text == told) return;
    told = text;
    LogF("vrcam: openxr - what the runtime bound our actions to (headset %s): %s", g_systemName[0] ? g_systemName : "?", text.c_str());
}
// Did the runtime bind the thumbstick? (vr/xr_stick_check.h): the sources of the stick action against the buttons'. Looked at when the profile is read, and
// again every few seconds until there is a verdict (the first minute: a runtime may bind a moment after it says the profile changed). Told once per change in
// the log, shown in D2R VR Settings (Home > Status, through the state block), and - where the spare thumbstick action has a source and the first has none - the
// spare is the thumbstick (Publish reads g_stickSpare).
void CheckSticks() {
    if (!g_session || !g_actionsOk) return;
    xrstick::Facts f;
    f.stick = (int)BoundSources(g_aStick).size();
    f.stickSpare = (int)BoundSources(g_aStickSpare).size();
    for (XrAction a : {g_aTrigger, g_aSqueeze, g_aPrimary, g_aSecondary, g_aStickClick, g_aMenu}) f.buttons += (int)BoundSources(a).size();
    f.stickExpected = xrprofiles::HasThumbstick(g_inNow.profile[0]) || xrprofiles::HasThumbstick(g_inNow.profile[1]);   // (the simple controller has none to bind)
    const xrstick::Verdict v = xrstick::Decide(f);
    if (v == xrstick::Verdict::Unknown) return;   // no conclusion: nothing (else) is bound yet
    if (v != g_stickVerdict) {
        g_stickVerdict = v;
        LogF("vrcam: openxr - thumbsticks: %s (sources bound: thumbstick %d, spare thumbstick %d, the other controls %d)", xrstick::LogText(v), f.stick, f.stickSpare, f.buttons);
    }
    g_stickSpare = xrstick::UseSpare(v);
    gamestate::SetStickBits(xrstick::Bits(v));
}
// The interaction profile each hand runs now (after the runtime says it changed).
void ReadProfiles() {
    for (int h = 0; h < 2; ++h) {
        XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
        g_inNow.profile[h][0] = 0;
        if (XR_FAILED(xrGetCurrentInteractionProfile(g_session, g_handPath[h], &ps)) || ps.interactionProfile == XR_NULL_PATH) continue;
        uint32_t n = 0;
        xrPathToString(g_inst, ps.interactionProfile, sizeof g_inNow.profile[h], &n, g_inNow.profile[h]);
    }
    LogF("vrcam: openxr - controllers: left %s, right %s", g_inNow.profile[0][0] ? g_inNow.profile[0] : "none",
         g_inNow.profile[1][0] ? g_inNow.profile[1] : "none");
    skillwheel::SetProfiles(g_inNow.profile[0], g_inNow.profile[1]);   // (the skill panel's tiles name the controller's own buttons)
    const bool optionsIsMenu = xrprofiles::MenuIsRightOptions(g_systemName, g_inNow.profile[0]);
    if (optionsIsMenu != g_menuIsOptions) {
        g_menuIsOptions = optionsIsMenu;
        if (optionsIsMenu)
            Log("vrcam: openxr - PS VR2 on SteamVR runs as a Touch controller, and SteamVR's own remapping (playstation_vr2_sense_controller_remapping.json) gives a Touch game the "
                "RIGHT controller's OPTIONS button as the LEFT menu button; the left CREATE button is not delivered at all. So on PS VR2 the menu button of this layout "
                "(a tap: the inventory, held: the pause menu, with the left grip: the map) is the Options button of the right controller");
    }
    if (g_inNow.profile[0][0] || g_inNow.profile[1][0]) {
        LogBoundSources();
        CheckSticks();
    }
}
// This frame's head and hands, at its display time, to Game Link's XrInput.
bool BareHand(int h) {
    if (!g_handSourceExt || !g_handTracker[h] || !g_xrLocateHandJoints) return false;
    XrHandJointLocationEXT j[XR_HAND_JOINT_COUNT_EXT];
    XrHandJointLocationsEXT loc{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    loc.jointCount = XR_HAND_JOINT_COUNT_EXT;
    loc.jointLocations = j;
    XrHandTrackingDataSourceStateEXT src{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
    loc.next = &src;
    XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
    li.baseSpace = g_space;
    li.time = g_fs.predictedDisplayTime;
    if (XR_FAILED(g_xrLocateHandJoints(g_handTracker[h], &li, &loc)) || !loc.isActive) return false;
    return src.isActive && src.dataSource == XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
}

float g_headHeld[3] = {};   // where the head was last located (a frame with its turn alone keeps it there)

// A controller out of the tracking cameras' view for a moment (PS VR2's Sense controllers at the
// belt, behind the back, close to the body: SteamVR gives its turn alone, or nothing) dropped the
// doll's arm at once and reset it - "the hands keep coming off" while the sticks worked
// (2026-10-11). Its last tracked place is kept by the head (in the frame of the head's turn, so
// it turns with the player) for [openxr] hand_hold_ms, with the turn the runtime still gives.
struct HandHold { float rel[3]; float rot[4]; XrTime at; bool ok; };
HandHold g_hold[2][2] = {};   // [hand][grip, aim]
float g_poseHeld[2][2][3] = {};   // PoseOut's last place of each (it then gives the turn alone, valid 2)
uint32_t g_holdTimes = 0;     // poses held so far (the log's count)
// Without the input focus (another app's overlay or the dashboard has it) the runtime's places for
// the controllers are not the controllers' (SteamVR gives them flagged valid, the doll's hands
// jumped and reset): held by the head for as long as the focus is away.
void HoldPose(D2RVR_XrPose& p, HandHold& h, const D2RVR_XrPose& head, bool focused) {
    const XrTime now = g_fs.predictedDisplayTime;
    if (!focused) p.valid = 0;
    if (head.valid == 0) { if (p.valid == 2) p = D2RVR_XrPose{}; return; }
    const XrQuaternionf hq{head.rot[0], head.rot[1], head.rot[2], head.rot[3]};
    const float hy = YawOf(hq);
    if (p.valid == 1) {
        const XrVector3f rel = Rot(Yaw(-hy), {p.pos[0] - head.pos[0], p.pos[1] - head.pos[1], p.pos[2] - head.pos[2]});
        h.rel[0] = rel.x; h.rel[1] = rel.y; h.rel[2] = rel.z;
        memcpy(h.rot, p.rot, sizeof h.rot);
        h.at = now;
        h.ok = true;
        return;
    }
    if (!h.ok || (focused && now - h.at > (XrTime)(g_cfg.handHoldMs * 1e6))) {   // gone for good: no hand, as before
        if (p.valid != 1) p = D2RVR_XrPose{};
        return;
    }
    const XrVector3f w = Rot(Yaw(hy), {h.rel[0], h.rel[1], h.rel[2]});
    p.pos[0] = head.pos[0] + w.x; p.pos[1] = head.pos[1] + w.y; p.pos[2] = head.pos[2] + w.z;
    if (p.valid != 2) memcpy(p.rot, h.rot, sizeof p.rot);   // no turn either: the last one
    p.valid = 1;
    if (++g_holdTimes % 500 == 1)
        LogF("vrcam: openxr - a controller lost its place: held by the head (%u frames so far; [openxr] hand_hold_ms %.0f)", g_holdTimes, g_cfg.handHoldMs);
}

// What the runtime gives for each controller's grip, logged once per change (PS VR2 on SteamVR:
// "the hands come off" while the controllers stay in the cameras' view, 2026-10-11): the locate's
// result, its flags (P/p position valid/tracked, R/r orientation), the display time asked for
// against the last frame's, the session focused or not, how long the last state held.
struct HandSeen { XrResult r = XR_SUCCESS; XrSpaceLocationFlags flags = ~0ull; int focused = -1; double since = 0.0; uint32_t changes = 0; };
HandSeen g_handSeen[2];
XrTime g_lastPublishTime = 0;
void LogHandSeen(int h, const LocateSeen& ls, bool focused, bool active) {
    HandSeen& s = g_handSeen[h];
    if (ls.r == s.r && ls.flags == s.flags && (int)focused == s.focused) return;
    const double now = UsNow();
    const double held = s.since > 0.0 ? (now - s.since) / 1000.0 : 0.0;
    s.r = ls.r; s.flags = ls.flags; s.focused = focused; s.since = now;
    if (++s.changes > 400 && s.changes % 50 != 0) return;   // a flood: every 50th after the first 400
    const XrSpaceLocationFlags f = ls.flags;
    LogF("vrcam: openxr - the %s grip: locate %d, %c%c%c%c (P/p position valid/tracked, R/r orientation), display time %+.2f ms on the last frame's%s, "
         "session %s, actions %s; the last state held %.0f ms (change %u)",
         h ? "right" : "left", (int)ls.r, (f & XR_SPACE_LOCATION_POSITION_VALID_BIT) ? 'P' : '-', (f & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) ? 'p' : '-',
         (f & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ? 'R' : '-', (f & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) ? 'r' : '-',
         g_lastPublishTime ? (double)(g_fs.predictedDisplayTime - g_lastPublishTime) / 1e6 : 0.0, g_fs.predictedDisplayTime ? "" : " (NO display time)",
         focused ? "focused" : "NOT focused", active ? "active" : "inactive", held, s.changes);
}

// A press held through a loss of the input focus is not a press when the focus returns (vr/focus_gate.h): with the
// focus away every action reads let go, and the first live frame would hand BodyWalk a trigger still down - often the
// very click that closed the dashboard - as a fresh press: an attack at whatever the hand points at. The grip is a
// hold, not a press, and is not held back.
focusgate::Gate g_gate[2];
uint32_t g_gateTimes = 0;
constexpr uint32_t kGatedButtons = BW_LINK_XRB_TRIGGER | BW_LINK_XRB_PRIMARY | BW_LINK_XRB_SECONDARY | BW_LINK_XRB_STICK | BW_LINK_XRB_MENU;

void LaserStep(D2RVR_XrInput& in, bool focused);   // the laser, below
void LaserTeardown();
void WheelTeardown();   // the skill wheel's picture, below (xr_skill_wheel.h)
void RunesTeardown();   // the gesture runes' atlas, below (xr_runes.h)
void VitalsTeardown();   // the hand vitals panel's picture, below (xr_hand_vitals.h)
void WristStatusTeardown();   // the wrist status panels' pictures, below (xr_wrist_status.h)
void HsStep(D2RVR_XrInput& in, bool focused);   // the settings panel, below (xr_headset_settings.h)
void HsTeardown();
void DropCalibration();   // the T-pose measurement's pictures, below

// The T-pose measurement (vr/calibration.cpp) gets this frame's head and hands in the room's space, as the runtime located them.
void FeedCalibration(const D2RVR_XrInput& in, const bool located[2], bool focused) {
    calib::Frame f;
    f.time = (double)in.displayTime * 1e-9;
    f.floor = g_roomSpace != XR_NULL_HANDLE;   // a STAGE space; LOCAL has no floor
    f.focused = focused;
    f.headTracked = in.head.valid == 1;
    memcpy(f.head, in.head.pos, sizeof f.head);
    memcpy(f.headRot, in.head.rot, sizeof f.headRot);
    for (int h = 0; h < 2; ++h) {
        f.handTracked[h] = located[h] && in.hand[h].active;
        memcpy(f.hand[h], in.hand[h].grip.pos, sizeof f.hand[h]);
        f.trigger[h] = in.hand[h].trigger;
    }
    f.cancel = (in.hand[1].buttons & BW_LINK_XRB_SECONDARY) != 0;   // B on the right controller
    calib::Feed(f);
}

void Publish(bool focused) {
    if (!g_actionsOk) return;
    if (g_stickVerdict == xrstick::Verdict::Unknown && g_stickChecks < 40 && (g_inNow.profile[0][0] || g_inNow.profile[1][0])) {   // no verdict on the thumbsticks yet: a look every 3 s, for two minutes
        const ULONGLONG now = GetTickCount64();
        if (now >= g_stickCheckAt) { g_stickCheckAt = now + 3000; ++g_stickChecks; CheckSticks(); }
    }
    D2RVR_XrInput& in = g_inNow;
    in.version = D2RVR_XR_INPUT_VERSION;
    in.focused = focused ? 1u : 0u;
    in.displayTime = g_fs.predictedDisplayTime;
    PoseOut(g_viewSpace, &in.head, g_headHeld);
    bool located[2] = {};   // a controller really located this frame (not a place held by the head): the T-pose measurement
    for (int h = 0; h < 2; ++h) {
        D2RVR_XrHand& o = in.hand[h];
        bool act = false;
        LocateSeen gripSeen{};
        PoseOut(g_gripSpace[h], &o.grip, g_poseHeld[h][0], &gripSeen);
        located[h] = o.grip.valid == 1;
        PoseOut(g_aimSpace[h], &o.aim, g_poseHeld[h][1]);
        HoldPose(o.grip, g_hold[h][0], in.head, focused);
        HoldPose(o.aim, g_hold[h][1], in.head, focused);
        o.trigger = FloatOf(g_aTrigger, h, &act);
        o.squeeze = FloatOf(g_aSqueeze, h, &act);
        if (g_cfg.indexGripForce) {   // [openxr] index_grip_force: an Index hand's grip by force (vr/xr_profiles.h)
            bool forceActive = false;
            const float force = FloatOf(g_aSqueezeForce, h, &forceActive);
            o.squeeze = xrprofiles::Grip(true, g_inNow.profile[h], o.squeeze, force, forceActive);
        }
        o.stickX = o.stickY = 0.0f;
        {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = g_stickSpare ? g_aStickSpare : g_aStick; gi.subactionPath = g_handPath[h];   // (the spare only where the runtime bound nothing to the first: CheckSticks)
            XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(xrGetActionStateVector2f(g_session, &gi, &st)) && st.isActive) { o.stickX = st.currentState.x; o.stickY = st.currentState.y; act = true; }
        }
        uint32_t b = 0;
        if (BoolOf(g_aPrimary, h, &act)) b |= BW_LINK_XRB_PRIMARY;
        if (BoolOf(g_aSecondary, h, &act)) b |= BW_LINK_XRB_SECONDARY;
        if (BoolOf(g_aStickClick, h, &act)) b |= BW_LINK_XRB_STICK;
        if (BoolOf(g_aMenu, h, &act)) b |= BW_LINK_XRB_MENU;
        bool clickBound = false;
        if (BoolOf(g_aTriggerClick, h, &clickBound) || (!clickBound && o.trigger > 0.8f)) b |= BW_LINK_XRB_TRIGGER;
        if (o.squeeze > 0.8f) b |= BW_LINK_XRB_SQUEEZE;
        o.buttons = b;
        {   // the finger on a control (capacitive touch), where the runtime reports it: the layout's "<control>_touch" lines read it (F4 only, once per touch)
            bool tAct = false;
            uint32_t t = 0;
            if (BoolOf(g_aTouchPrimary, h, &tAct)) t |= directinput::kTouchPrimary;
            if (BoolOf(g_aTouchSecondary, h, &tAct)) t |= directinput::kTouchSecondary;
            if (BoolOf(g_aTouchStick, h, &tAct)) t |= directinput::kTouchStick;
            if (BoolOf(g_aTouchTrigger, h, &tAct)) t |= directinput::kTouchTrigger;
            g_touch[h] = t;
            if (tAct && !g_touchSaid[h]) {   // said once per hand: which inputs and which controller
                g_touchSaid[h] = true;
                std::string which;
                const struct { XrAction a; const char* n; } all[] = {{g_aTouchTrigger, "trigger"}, {g_aTouchStick, "thumbstick"}, {g_aTouchPrimary, h ? "A" : "X"}, {g_aTouchSecondary, h ? "B" : "Y"}};
                for (const auto& x : all) { bool active = false; BoolOf(x.a, h, &active); if (active) which += (which.empty() ? "" : ", ") + std::string(x.n); }
                LogF("vrcam: openxr - the %s hand's controller (profile %s) reports touch on: %s ([direct] <control>_touch lines)", h ? "right" : "left",
                     g_inNow.profile[h][0] ? g_inNow.profile[h] : "none", which.c_str());
            } else if (!tAct && o.active && !g_touchSaid[h] && g_touchLooked[h] >= 0 && ++g_touchLooked[h] == 240 && directinput::On()) {
                LogF("vrcam: openxr - the %s hand's controller (profile %s) reports no touch: a [direct] <control>_touch line does nothing for it", h ? "right" : "left",
                     g_inNow.profile[h][0] ? g_inNow.profile[h] : "none");
                g_touchLooked[h] = -1;
            }
        }
        {   // the menu button's presses, told with the profile that carried them: PS VR2 on SteamVR runs as a Touch controller, and whether its
            // Create / Options buttons arrive as the menu button was unknown (2026-10-17). The first 60 presses; a press of the plugin's own
            // long-press pause is told on its own line ("direct input: pause ...").
            static bool menuWas[2] = {};
            static int menuTold = 0;
            const bool menuNow = (b & BW_LINK_XRB_MENU) != 0;
            if (menuNow && !menuWas[h] && menuTold++ < 60)
                LogF("vrcam: openxr - the %s hand's menu button pressed (%s, profile %s, headset %s)", h ? "right" : "left",
                     g_menuIsOptions ? "/user/hand/left/input/menu/click = PS VR2's RIGHT controller's Options button" : h ? "/user/hand/right/input/menu/click" : "/user/hand/left/input/menu/click",
                     g_inNow.profile[h][0] ? g_inNow.profile[h] : "none", g_systemName[0] ? g_systemName : "?");
            menuWas[h] = menuNow;
        }
        o.active = act || o.grip.valid ? 1u : 0u;
        LogHandSeen(h, gripSeen, focused, act);
        const bool bare = BareHand(h);
        if (bare && !g_cfg.bareHands) {   // no controller in it: nothing pressed, no hand for BodyWalk
            o = D2RVR_XrHand{};
            b = 0;
            g_touch[h] = 0;
        }
        if (g_bareSaid[h] != (int)bare) {
            g_bareSaid[h] = bare;
            LogF("vrcam: openxr - the %s hand: %s", h ? "right" : "left",
                 bare ? (g_cfg.bareHands ? "bare (hand tracking), taken as a controller ([openxr] bare_hands=1)" : "bare (hand tracking) - presses nothing, no hand for BodyWalk")
                      : "a controller (or not tracked)");
        }
        {   // (after the bare hand's reset, before the laser reads the buttons)
            static bool held[2] = {};
            // F4 only (vr/view_gate.h): in the other views the controls go out as they did before this gate (it still follows the hands)
            const bool now = g_gate[h].StepIf(viewgate::F4(), focused, &o.buttons, &o.trigger, kGatedButtons);
            if (now && !held[h] && (++g_gateTimes <= 5 || g_gateTimes % 50 == 0))
                LogF("vrcam: openxr - the %s hand's trigger or button was still down when the input focus came back: not a press until it is let go (%u)",
                     h ? "right" : "left", g_gateTimes);
            held[h] = now;
        }
    }
    HsStep(in, focused);      // the settings panel, when it is up, has both hands: nothing of them goes on
    FeedCalibration(in, located, focused);
    LaserStep(in, focused);   // what the laser takes is not BodyWalk's
    directinput::Feed(&in, g_laserWanted.load(), g_touch);   // the gestures' listeners, and [openxr] direct_input's pad, actions, commands and head (and the finger's touch)
    g_lastPublishTime = g_fs.predictedDisplayTime;
    in.stamp = D2RVRStampNow();
    in.counter++;
    link::PublishXr(in, false);
    g_inSent = true;
}

// [openxr] haptics (vr/haptics.cpp): the pulses that wait, played on the output action - once a frame, after
// the actions are synced. xrApplyHapticFeedback replaces what the motor plays; a session without the input
// focus (the headset off the head, the dashboard open) is not given any: haptics::Flush drops what waits.
XrHapticActionInfo HapticInfo(int hand) {   // the hand's own action ([openxr] haptics_hand_actions), else the shared one filtered by hand
    XrHapticActionInfo ai{XR_TYPE_HAPTIC_ACTION_INFO};
    if (g_aHapticHand[hand & 1]) ai.action = g_aHapticHand[hand & 1];
    else { ai.action = g_aHaptic; ai.subactionPath = g_handPath[hand & 1]; }
    return ai;
}
// The stop timer's call, on its own thread (vr/haptic_stop.h): only while the session and its actions are there.
void StopHaptic(int hand) {
    AcquireSRWLockShared(&g_hapticStopLock);
    if (g_hapticStopOk) {
        const XrHapticActionInfo ai = HapticInfo(hand);
        const XrResult r = xrStopHapticFeedback(g_session, &ai);
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 2) LogF("vrcam: haptics - the %s hand's pulse stopped by its timer (%s)", hand ? "right" : "left", Res(r));
    }
    ReleaseSRWLockShared(&g_hapticStopLock);
}
bool ApplyHaptic(int hand, float amplitude, int durationMs) {
    const XrHapticActionInfo ai = HapticInfo(hand);
    XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
    v.duration = (XrDuration)durationMs * 1000000;   // ns
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    v.amplitude = amplitude;
    frametrace::Scope trace(frametrace::kHaptic);   // [debug] frame_trace: the runtime's call - some runtimes make a round trip of it
    const XrResult applied = xrApplyHapticFeedback(g_session, &ai, (const XrHapticBaseHeader*)&v);
    const bool ok = Ok(applied, "xrApplyHapticFeedback");
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 4)   // the first calls, as the runtime gets them: what a controller that stays still was sent
        LogF("vrcam: haptics - xrApplyHapticFeedback: %s hand, amplitude %.2f, %d ms, frequency unspecified (the runtime's own) -> %s", hand ? "right" : "left", amplitude,
             durationMs, Res(applied));
    if (ok && g_cfg.hapticsStop && g_hapticTimer->Start(&StopHaptic)) g_hapticTimer->Arm(hand & 1, (uint32_t)std::max(durationMs, 1));
    return ok;
}
void FlushHaptics() {
    const bool can = g_actionsOk && (g_aHaptic || g_aHapticHand[0]) && g_session && g_ss == XR_SESSION_STATE_FOCUSED;
    if (const bool ok = g_actionsOk && g_session && (g_aHaptic || g_aHapticHand[0]); ok != g_hapticStopOk) {
        AcquireSRWLockExclusive(&g_hapticStopLock);
        g_hapticStopOk = ok;
        ReleaseSRWLockExclusive(&g_hapticStopLock);
    }
    haptics::Flush(GetTickCount64(), can, &ApplyHaptic);
}

// -- start: instance, system, session (a worker thread) ------------------------
void WaitFence(UINT64 v, DWORD ms);   // (the copy list, below)
void SweepGraveyard(bool all);
void Teardown() {   // under g_lock or before the session exists
    g_running.store(false);
    gpufault::Note("xr: teardown");
    if (!gpufault::Removed() && g_queue && g_fence) {   // our lists done before the swapchains they draw into go (Poll's loss path had no wait; Stop's is the same one)
        g_queue->Signal(g_fence, ++g_fenceV);
        WaitFence(g_fenceV, 500);
    }
    SweepGraveyard(true);
    g_hapticTimer->Cancel();   // [openxr] haptics_stop: no stop called on what goes below
    AcquireSRWLockExclusive(&g_hapticStopLock);
    g_hapticStopOk = false;
    ReleaseSRWLockExclusive(&g_hapticStopLock);
    for (PendingImage& q : g_pendingImg) q = PendingImage{};   // their swapchains go now
    g_lastEye[0] = g_lastEye[1] = LastEye{};
    g_eyesTold = false;   // the next session says its eyes again
    if (g_ptr.sc) xrDestroySwapchain(g_ptr.sc);
    g_ptr = Pointer{};
    LaserTeardown();   // its buttons and keys let go, its images gone
    HsTeardown();      // the settings panel's picture and its thread
    calib::Stop();     // a T-pose measurement under way ends with the session
    DropCalibration();
    WheelTeardown();   // the skill wheel closed, its picture gone
    RunesTeardown();   // the gesture runes' atlas
    VitalsTeardown();  // the hand vitals panel's picture gone
    WristStatusTeardown();   // the wrist status panels' pictures gone
    g_panelPlace = PanelPlace{};
    g_gate[0] = g_gate[1] = focusgate::Gate{};   // a new session: what is down at its start is held back too
    for (HudOut& o : g_hudOut) { if (o.sc) xrDestroySwapchain(o.sc); o = HudOut{}; }   // (a menu's panel's too)
    if (g_zoneOut.sc) xrDestroySwapchain(g_zoneOut.sc);
    g_zoneOut = HudOut{};
    if (g_plateOut.sc) xrDestroySwapchain(g_plateOut.sc);   // (the only place the monster bar's chain goes: the session's end)
    g_plateOut = HudOut{};
    g_rtvSlots.Reset();   // every chain that held places is gone
    for (Chain* c : {&g_eyeChain[0], &g_eyeChain[1], &g_quadChain}) {
        if (c->sc) xrDestroySwapchain(c->sc);
        *c = Chain{};
    }
    for (int h = 0; h < 2; ++h) {
        if (g_gripSpace[h]) xrDestroySpace(g_gripSpace[h]);
        if (g_aimSpace[h]) xrDestroySpace(g_aimSpace[h]);
        g_gripSpace[h] = g_aimSpace[h] = XR_NULL_HANDLE;
    }
    if (g_viewSpace) { xrDestroySpace(g_viewSpace); g_viewSpace = XR_NULL_HANDLE; }
    if (g_roomSpace) { xrDestroySpace(g_roomSpace); g_roomSpace = XR_NULL_HANDLE; }
    if (g_actSet) { xrDestroyActionSet(g_actSet); g_actSet = XR_NULL_HANDLE; }   // its actions go with it
    g_aHaptic = g_aHapticHand[0] = g_aHapticHand[1] = XR_NULL_HANDLE;
    g_actionsOk = false;
    g_menuIsOptions = false;
    g_stickSpare = false;                              // a new session: the bindings are the runtime's to say again, and are told again
    g_stickVerdict = xrstick::Verdict::Unknown;
    g_stickCheckAt = 0;
    g_stickChecks = 0;
    g_boundTold.clear();
    g_aStickSpare = XR_NULL_HANDLE;
    gamestate::SetStickBits(0);
    directinput::SessionEnd();
    for (XrHandTrackerEXT& t : g_handTracker) { if (t && g_xrDestroyHandTracker) g_xrDestroyHandTracker(t); t = XR_NULL_HANDLE; }
    g_xrCreateHandTracker = nullptr; g_xrDestroyHandTracker = nullptr; g_xrLocateHandJoints = nullptr;
    g_bareSaid[0] = g_bareSaid[1] = -1;
    g_pairYawAt.store(0);
    g_pairOpen.store(false);
    if (g_inSent) {   // a last frame that says the session is over: BodyWalk gives the Mapping back at once
        D2RVR_XrInput last{};
        last.version = D2RVR_XR_INPUT_VERSION;
        last.stamp = D2RVRStampNow();
        link::PublishXr(last, true);
        g_inSent = false;
    }
    if (g_space) { xrDestroySpace(g_space); g_space = XR_NULL_HANDLE; }
    const bool stuck = (g_waitRunner && g_waitRunner->Running()) || (g_beginRunner && g_beginRunner->Running());
    if (stuck && (g_session || g_inst)) {   // a runtime call of ours has not returned: the session and the instance are destroyed on a thread that may wait for good, not here
        struct Gone { XrSession s; XrInstance i; };
        Gone* gone = new Gone{g_session, g_inst};
        LogF("vrcam: openxr - a runtime call (%s) has not returned: the session is destroyed on a thread of its own, the game does not wait for it",
             g_waitRunner && g_waitRunner->Running() ? "xrWaitFrame" : "xrBeginSession");
        if (HANDLE h = CreateThread(nullptr, 0, [](void* p) -> DWORD {
                Gone* g = (Gone*)p;
                if (g->s) xrDestroySession(g->s);
                if (g->i) xrDestroyInstance(g->i);
                delete g;
                return 0;
            }, gone, 0, nullptr)) CloseHandle(h);
        g_session = XR_NULL_HANDLE;
        g_inst = XR_NULL_HANDLE;
    }
    delete g_waitRunner; g_waitRunner = nullptr;   // (a call still stuck in one finishes on its own, nobody collects it)
    delete g_beginRunner; g_beginRunner = nullptr;
    g_beginPending = false; g_waitPendingAt = 0.0;
    if (g_session) { xrDestroySession(g_session); g_session = XR_NULL_HANDLE; }
    if (g_inst) { xrDestroyInstance(g_inst); g_inst = XR_NULL_HANDLE; }
    g_kind = kNone;
    vrres::SessionEnded();   // [render] vr_resolution: the window's own size back (a post - the window's thread does it)
    g_vrRecommended.store(0);
    g_vrMaximum.store(0);
}

bool StartSession() {
    // before the runtime is in the game: ReShade's DXGI factory must not take its asks for no adapter (reshade_shield.cpp)
    reshade_shield::Install(g_log);
    // the extensions the runtime has
    uint32_t n = 0;
    if (!Ok(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr), "xrEnumerateInstanceExtensionProperties")) return false;
    std::vector<XrExtensionProperties> ext(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, ext.data());
    bool d3d12 = false, hands = false, handSource = false, colorScale = false, frame = false;
    for (const auto& e : ext) {
        if (!strcmp(e.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME)) d3d12 = true;
        if (!strcmp(e.extensionName, XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME)) colorScale = true;
        if (!strcmp(e.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME)) hands = true;
        if (!strcmp(e.extensionName, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME)) handSource = true;
        if (!strcmp(e.extensionName, xrprofiles::kFrameExtension)) frame = true;
    }
    if (!d3d12) { Log("vrcam: openxr - the runtime has no XR_KHR_D3D12_enable: no session"); return false; }
    {
        uint32_t nl = 0;
        xrEnumerateApiLayerProperties(0, &nl, nullptr);
        std::vector<XrApiLayerProperties> layers(nl, {XR_TYPE_API_LAYER_PROPERTIES});
        if (nl) xrEnumerateApiLayerProperties(nl, &nl, layers.data());
        std::string names;
        for (const auto& l : layers) { if (!names.empty()) names += ", "; names += l.layerName; }
        LogF("vrcam: openxr - API layers on this PC (implicit ones load with us): %s", names.empty() ? "none" : names.c_str());
    }

    std::vector<const char*> exts{XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    if (hands) exts.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    if (hands && handSource) exts.push_back(XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME);
    if (colorScale) exts.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
    g_colorScaleExt = colorScale;
    g_frameExt = xrprofiles::WantFrame(g_cfg.frameProfile, frame);
    if (g_frameExt) exts.push_back(xrprofiles::kFrameExtension);
    if (frame) Log(g_frameExt ? "vrcam: openxr - Steam Frame controllers offered: their own layout (left D-pad down / up = X / Y)"
                              : "vrcam: openxr - Steam Frame controllers offered, not taken ([openxr] frame_profile=0): the runtime's stand-in layout");
    Log(colorScale ? "vrcam: openxr - colour scale: yes ([openxr] brightness works)"
                   : "vrcam: openxr - the runtime has no XR_KHR_composition_layer_color_scale_bias: [openxr] brightness does nothing");
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "D2R VR");
    ci.applicationInfo.applicationVersion = 1;
    strcpy_s(ci.applicationInfo.engineName, "D2RLoader");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.enabledExtensionNames = exts.data();
    if (!Ok(xrCreateInstance(&ci, &g_inst), "xrCreateInstance")) return false;
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_inst, &ip)))
        LogF("vrcam: openxr - runtime %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
             XR_VERSION_PATCH(ip.runtimeVersion));

    // the headset (a few seconds' grace: a runtime may still be waking it)
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult r = XR_ERROR_FORM_FACTOR_UNAVAILABLE;
    for (int i = 0; i < 20 && (r = xrGetSystem(g_inst, &si, &g_sys)) == XR_ERROR_FORM_FACTOR_UNAVAILABLE; ++i) Sleep(250);
    g_headsetAbsent.store(r == XR_ERROR_FORM_FACTOR_UNAVAILABLE);
    if (!Ok(r, "xrGetSystem")) return false;
    vrres::Size maxSwapchain;   // [render] vr_resolution: the system's swapchain limit (0 = the query failed)
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    XrSystemHandTrackingPropertiesEXT hp{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    if (hands) sp.next = &hp;
    if (XR_SUCCEEDED(xrGetSystemProperties(g_inst, g_sys, &sp))) {
        if (hands && hp.supportsHandTracking) {
            xrGetInstanceProcAddr(g_inst, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)&g_xrCreateHandTracker);
            xrGetInstanceProcAddr(g_inst, "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction*)&g_xrDestroyHandTracker);
            xrGetInstanceProcAddr(g_inst, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)&g_xrLocateHandJoints);
            g_handSourceExt = handSource;
        }
        LogF("vrcam: openxr - hand tracking: %s", !hands ? "not in this runtime" : !hp.supportsHandTracking ? "not on this headset"
                                                 : handSource ? "yes, with its data source (a bare hand told from a controller)" : "yes");
        if (sp.graphicsProperties.maxLayerCount) g_maxLayers = sp.graphicsProperties.maxLayerCount;
        strcpy_s(g_systemName, sp.systemName);
        LogF("vrcam: openxr - headset %s, %u layers at most, swapchains up to %ux%u", sp.systemName, sp.graphicsProperties.maxLayerCount,
             sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight);
        haptics::SetHeadset(sp.systemName);   // which motor the controllers have (a PlayStation VR2 Sense needs the gentle pulses lifted: haptics_pattern.h Motor)
        maxSwapchain = {sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight};
    }
    {
        uint32_t nv = 0;
        XrViewConfigurationView vv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(g_inst, g_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, vv)) && nv == 2) {
            LogF("vrcam: openxr - each eye's recommended picture %ux%u (the window keeps its own size)", vv[0].recommendedImageRectWidth,
                 vv[0].recommendedImageRectHeight);
            // [render] vr_resolution: Headset is the larger of the eyes' recommendations; the most, what both eyes and the
            // system's swapchain hold (unknown system limit: none - the window's own size stays)
            g_vrRecommended.store(vrres::Pack({std::max(vv[0].recommendedImageRectWidth, vv[1].recommendedImageRectWidth),
                                               std::max(vv[0].recommendedImageRectHeight, vv[1].recommendedImageRectHeight)}));
            g_vrMaximum.store(maxSwapchain ? vrres::Pack({std::min({vv[0].maxImageRectWidth, vv[1].maxImageRectWidth, maxSwapchain.w}),
                                                         std::min({vv[0].maxImageRectHeight, vv[1].maxImageRectHeight, maxSwapchain.h})})
                                           : 0);
            WriteHeadsetInfo(sp.systemName, vrres::Unpack(g_vrRecommended.load()), vrres::Unpack(g_vrMaximum.load()));
        }
    }

    // the runtime's GPU must be the game's
    PFN_xrGetD3D12GraphicsRequirementsKHR getReq = nullptr;
    if (!Ok(xrGetInstanceProcAddr(g_inst, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReq), "xrGetInstanceProcAddr(D3D12 requirements)")) return false;
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    if (!Ok(getReq(g_inst, g_sys, &req), "xrGetD3D12GraphicsRequirementsKHR")) return false;
    const LUID mine = g_dev->GetAdapterLuid();
    if (mine.LowPart != req.adapterLuid.LowPart || mine.HighPart != req.adapterLuid.HighPart) {
        LogF("vrcam: openxr - the headset runs on another GPU (LUID %08lX:%08lX, the game's %08lX:%08lX): no session",
             req.adapterLuid.HighPart, req.adapterLuid.LowPart, mine.HighPart, mine.LowPart);
        return false;
    }
    LogF("vrcam: openxr - same GPU as the game (feature level %X at least); device %p, queue %p (the game's own, not ReShade's)",
         (unsigned)req.minFeatureLevel, (void*)g_dev, (void*)g_queue);

    XrGraphicsBindingD3D12KHR gb{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    gb.device = g_dev;
    gb.queue = g_queue;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = g_sys;
    if (!Ok(xrCreateSession(g_inst, &sci, &g_session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = g_cfg.stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    if (!Ok(xrCreateReferenceSpace(g_session, &rs, &g_space), "xrCreateReferenceSpace")) {
        if (!g_cfg.stage) return false;
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;   // no stage: the local one
        if (!Ok(xrCreateReferenceSpace(g_session, &rs, &g_space), "xrCreateReferenceSpace(LOCAL)")) return false;
    }
    g_spaceType = rs.referenceSpaceType;

    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(g_session, 0, &nf, nullptr);
    g_formats.resize(nf);
    if (nf) xrEnumerateSwapchainFormats(g_session, nf, &nf, g_formats.data());
    std::string fl;
    for (int64_t f : g_formats) { if (!fl.empty()) fl += ", "; fl += FormatName(f); }
    LogF("vrcam: openxr - session made (%s space); swapchain formats: %s", rs.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "local", fl.c_str());
    g_actionsOk = MakeActions();   // without them: a picture, no controllers
    return true;
}

// Every module's place in memory, once a session runs: a crash report's addresses (D2RLoader's
// last-resort report gives bare ones) can then be put to a module.
void LogModules() {
    HMODULE mods[512];
    DWORD need = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    std::string line;
    const DWORD n = std::min<DWORD>(need / sizeof(HMODULE), 512);
    for (DWORD i = 0; i < n; ++i) {
        MODULEINFO mi{};
        char name[64] = "";
        if (!K32GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
        K32GetModuleBaseNameA(GetCurrentProcess(), mods[i], name, sizeof name);
        char b[128];
        snprintf(b, sizeof b, "%s%s %p+%lX", line.empty() ? "" : ", ", name, mi.lpBaseOfDll, mi.SizeOfImage);
        line += b;
        if (line.size() > 700) { LogF("vrcam: openxr - modules: %s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) LogF("vrcam: openxr - modules: %s", line.c_str());
}

DWORD WINAPI StartThread(void*) {
    const ULONGLONG t0 = GetTickCount64();
    if (g_cfg.stopFlatVR && !StopFlatVR()) {   // FlatVR holds the headset and cannot be stopped: no session over it
        WriteSessionNote(L"FlatVR (BodyWalk) still has the headset - close BodyWalk and restart SteamVR");
        GuardClear("no session started: FlatVR has the headset, the game lives");
        g_retryAt.store(GetTickCount64() + kRetryMs);
        g_state.store(kFailed);
        return 0;
    }
    AcquireSRWLockExclusive(&g_lock);
    const bool ok = StartSession();
    if (!ok) Teardown();
    ReleaseSRWLockExclusive(&g_lock);
    if (!ok) {
        GuardClear("no session, and the game lives");
        const bool again = g_headsetAbsent.load();
        if (again) {
            static int told = 0;
            if (told++ % 30 == 0) Log("vrcam: openxr - no headset yet (is Virtual Desktop or Link streaming?): tried again every 10 s while FlatVR does not run");
        } else {
            Log("vrcam: openxr - no session: FlatVR goes on as always for this run");
        }
        if (g_flatVrStopped) { link::RequestFlatVR(true); g_flatVrStopped = false; }
        g_retryAt.store(again ? GetTickCount64() + kRetryMs : 0);
        g_state.store(kFailed);
        return 0;
    }
    g_retryAt.store(0);
    WriteSessionNote(L"");   // (a note of an earlier refusal goes)
    LogModules();
    if (!directinput::Governing() || directinput::BodyWalkWalks()) link::RequestPresenter(true);   // BodyWalk's Mapping on the game's own headset ("Game Link"; not while direct_input governs (the key set, view F4): it gets no controllers then - except for its walking: [openxr] direct_bodywalk_walk gives it the poses)
    LogF("vrcam: openxr - ready in %.1f s, waiting for the runtime's go", (GetTickCount64() - t0) / 1000.0);
    g_state.store(kCreated);
    return 0;
}

// -- the draw thread -----------------------------------------------------------
bool MakeCopyList() {
    if (g_list) return true;
    for (int i = 0; i < kAllocs; ++i)
        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_alloc[i]))) return false;
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence))) return false;
    g_fenceEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_list))) return false;
    g_list->Close();
    for (int i = 0; i < kAllocs; ++i) g_alloc[i]->SetName(L"d2rvr xr copy allocator");   // (the names DRED prints for what the GPU was running)
    g_list->SetName(L"d2rvr xr copy list");
    g_fence->SetName(L"d2rvr xr copy fence");
    return true;
}
void WaitFence(UINT64 v, DWORD ms) {
    if (!g_fence || g_fence->GetCompletedValue() >= v) return;
    const double t = UsNow();
    g_fence->SetEventOnCompletion(v, g_fenceEvt);
    WaitForSingleObject(g_fenceEvt, ms);
    g_clock.fence += UsNow() - t;
}

// Before a swapchain of ours goes: the lists we submitted that draw into or copy into its images are done (2026-10-17, the device lost: a
// resource freed under work the GPU had not run yet is a page fault).
void SettleGpu() { if (g_fence && g_fenceV) WaitFence(g_fenceV, 200); }

// A buffer the GPU may still read is not released at once, and not after a wait that may run out: it waits here for the fence of the last
// list submitted so far, and goes when the fence has passed it (every OpenList looks).
struct Grave { ID3D12Resource* r; UINT64 v; };
std::vector<Grave> g_graveyard;
void Retire(ID3D12Resource*& r) {
    if (!r) return;
    gpufault::Note("xr: an upload buffer retired", (uint64_t)(uintptr_t)r, g_fenceV);
    if (g_fence && g_fence->GetCompletedValue() < g_fenceV) g_graveyard.push_back({r, g_fenceV});
    else r->Release();
    r = nullptr;
}
void SweepGraveyard(bool all = false) {
    if (g_graveyard.empty()) return;
    const UINT64 done = g_fence ? g_fence->GetCompletedValue() : ~0ull;
    size_t keep = 0;
    for (const Grave& g : g_graveyard) {
        if (all || done >= g.v) g.r->Release();
        else g_graveyard[keep++] = g;
    }
    g_graveyard.resize(keep);
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

// A swapchain the back buffer's size and family; made again when either changes.
bool EnsureChain(Chain& c, const D3D12_RESOURCE_DESC& d, const char* what) {
    if (c.sc && c.w == (uint32_t)d.Width && c.h == d.Height && c.bbFmt == d.Format) return true;
    if (c.bad && c.w == (uint32_t)d.Width && c.h == d.Height && c.bbFmt == d.Format) return false;
    if (c.sc) {
        SettleGpu();
        gpufault::Note("xr: picture swapchain destroyed", (uint64_t)(uintptr_t)c.sc, ((uint64_t)c.w << 32) | c.h);
        xrDestroySwapchain(c.sc);
        c.sc = XR_NULL_HANDLE;
        c.img.clear();
    }
    c.w = (uint32_t)d.Width; c.h = d.Height; c.bbFmt = d.Format; c.bad = false;
    g_aspect.store(c.h ? (float)c.w / (float)c.h : 0.0f);
    c.fmt = PickFormat(d.Format);
    if (!c.fmt) {
        c.bad = true;
        LogF("vrcam: openxr - the back buffer is %s and the runtime has no format of its family: no %s picture", FormatName(d.Format), what);
        return false;
    }
    LogF("vrcam: openxr - making the %s swapchain, %ux%u %s", what, c.w, c.h, FormatName(c.fmt));
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = c.fmt;
    ci.sampleCount = 1;
    ci.width = c.w;
    ci.height = c.h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!Ok(xrCreateSwapchain(g_session, &ci, &c.sc), "xrCreateSwapchain")) {
        c.bad = true; c.sc = XR_NULL_HANDLE;
        vrres::SwapchainMade(c.w, c.h, false);   // [render] vr_resolution(_px): a size the runtime refuses falls back to the last good one
        return false;
    }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(c.sc, 0, &n, nullptr);
    c.img.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(c.sc, n, &n, (XrSwapchainImageBaseHeader*)c.img.data());
    vrres::SwapchainMade(c.w, c.h, true);
    gpufault::Note("xr: picture swapchain made", (uint64_t)(uintptr_t)c.sc, ((uint64_t)c.w << 32) | c.h);
    LogF("vrcam: openxr - %s swapchain %ux%u %s (back buffer %s), %u images", what, c.w, c.h, FormatName(c.fmt), FormatName(d.Format), n);
    if (Family(d.Format) == 3) Log("vrcam: openxr -   a 10-bit back buffer has no sRGB twin: the headset may show it too bright");
    return true;
}

// Our list, opened on the next allocator of the ring / submitted on the game's queue.
int g_openAlloc = 0;
ID3D12GraphicsCommandList* OpenList() {
    if (!MakeCopyList() || gpufault::Removed()) return nullptr;
    SweepGraveyard();
    const int a = g_allocAt;
    // The allocator's last list must be done before it is reset: a reset under a list the GPU still runs removes the device. It used to be
    // waited for 100 ms and reset whether done or not; now a wait that runs out leaves this piece out of the frame, and the ring is asked again next time.
    if (g_fence && g_fence->GetCompletedValue() < g_allocDone[a]) {
        WaitFence(g_allocDone[a], 100);
        if (g_fence->GetCompletedValue() < g_allocDone[a]) {
            static ULONGLONG toldAt = 0;
            const ULONGLONG now = GetTickCount64();
            if (!toldAt || now - toldAt > 10000) {
                toldAt = now;
                LogF("vrcam: openxr - the GPU is more than 100 ms behind our copy list %d (fence %llu of %llu): a piece is left out of this frame, not drawn over a list still running",
                     a, (unsigned long long)g_fence->GetCompletedValue(), (unsigned long long)g_allocDone[a]);
            }
            gpufault::Note("xr: the GPU is behind our copy list: a piece left out", (uint64_t)a, g_allocDone[a]);
            return nullptr;
        }
    }
    if (FAILED(g_alloc[a]->Reset()) || FAILED(g_list->Reset(g_alloc[a], nullptr))) return nullptr;
    g_allocAt = (a + 1) % kAllocs;
    g_openAlloc = a;
    return g_list;
}
void SubmitList() {
    g_list->Close();
    ID3D12CommandList* l = g_list;
    g_queue->ExecuteCommandLists(1, &l);
    g_queue->Signal(g_fence, ++g_fenceV);
    g_allocDone[g_openAlloc] = g_fenceV;
}

// -- the mouse pointer over the flat picture -------------------------------------
// The game's pointer is Windows' own, never in the back buffer (FlatVR drew it into
// its picture). Here: the pointer's image rasterised as Windows draws it (on black
// and on white: what differs is its coverage), in a static swapchain of its own -
// made again when the shape changes - and a small quad laid on the flat picture
// where the pointer is in the game's window.
// A swapchain's next image, acquired and waited for - the wait bounded (2026-10-10). With
// SteamVR's headset asleep (its frames 100 ms apart) an endless wait held the game's draw thread
// for a minute, until the GPU behind it gave up (DXGI_ERROR_DEVICE_HUNG) and the game died. Now
// 20 ms (1 ms once a wait ran out in the last half second): that piece is left out of this frame,
// and its image, acquired already, is waited for again next time rather than another acquired
// (a chain whose images are all acquired gives no more). 1 ready, 0 ran out, -1 failed.
ULONGLONG g_waitRanOutAt = 0;
void ForgetPending(XrSwapchain sc) { for (PendingImage& q : g_pendingImg) if (q.sc == sc) q = PendingImage{}; }
int NextImage(XrSwapchain sc, uint32_t* idx, const char* what) {
    char b[96];
    PendingImage* p = nullptr;
    for (PendingImage& q : g_pendingImg) if (q.sc && q.sc == sc) { p = &q; break; }
    if (p) {
        *idx = p->idx;
    } else {
        snprintf(b, sizeof b, "xrAcquireSwapchainImage(%s)", what);
        if (!Ok(xrAcquireSwapchainImage(sc, nullptr, idx), b)) return -1;
    }
    const ULONGLONG now = GetTickCount64();
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = g_waitRanOutAt && now - g_waitRanOutAt < 500 ? 1000000 : 20000000;   // ns
    const double tw = UsNow();
    const XrResult r = xrWaitSwapchainImage(sc, &wi);
    g_clock.image += UsNow() - tw;
    if (r == XR_TIMEOUT_EXPIRED) {
        if (!p) for (PendingImage& q : g_pendingImg) if (!q.sc) { q = PendingImage{sc, *idx}; break; }
        if (!g_waitRanOutAt || now - g_waitRanOutAt > 10000)
            LogF("vrcam: openxr - the runtime holds the %s's images (the headset asleep?): left out of this frame, not waited for", what);
        g_waitRanOutAt = now;
        return 0;
    }
    if (p) *p = PendingImage{};   // waited for (or gone): acquired anew next time
    snprintf(b, sizeof b, "xrWaitSwapchainImage(%s)", what);
    return Ok(r, b) ? 1 : -1;
}

ID3D12Resource* g_ptrUpload = nullptr;
ULONGLONG g_ptrRetryAt = 0;   // the pointer's image ran out of its wait: made again from then on

// A static swapchain (acquired, filled and released once, shown as long as it lasts) of w x h
// premultiplied pixels, sRGB, R in each one's low byte. `upload` holds them until the copy is done:
// kept until the next image made with it. `sc` is the caller's to destroy once made, whatever
// came after. 1 made, 0 the image's wait ran out (make it again later), -1 failed.
int MakeStaticImage(uint32_t w, uint32_t h, const uint32_t* rgba, ID3D12Resource*& upload, XrSwapchain& sc, const char* what) {
    const UINT pitch = (w * 4 + 255) & ~255u;
    Retire(upload);   // (the copy that read it may not have run yet: a new cursor shape came a frame after the last)
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (UINT64)pitch * h; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&upload))) return -1;
    uint8_t* dst = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(upload->Map(0, &none, (void**)&dst))) return -1;
    for (uint32_t y = 0; y < h; ++y) memcpy(dst + (size_t)y * pitch, rgba + (size_t)y * w, (size_t)w * 4);
    upload->Unmap(0, nullptr);
    int64_t fmt = 0;
    for (int64_t f : g_formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) fmt = f;
    if (!fmt) return -1;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = w; ci.height = h; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    char b[96];
    snprintf(b, sizeof b, "xrCreateSwapchain(%s)", what);
    if (!Ok(xrCreateSwapchain(g_session, &ci, &sc), b)) { sc = XR_NULL_HANDLE; return -1; }
    uint32_t cnt = 0;
    xrEnumerateSwapchainImages(sc, 0, &cnt, nullptr);
    std::vector<XrSwapchainImageD3D12KHR> img(cnt, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(sc, cnt, &cnt, (XrSwapchainImageBaseHeader*)img.data());
    uint32_t idx = 0;
    if (!cnt) return -1;
    if (const int got = NextImage(sc, &idx, what); got <= 0) return got;
    bool copied = false;
    if (ID3D12GraphicsCommandList* l = OpenList()) {
        ID3D12Resource* t = img[idx].texture;
        D3D12_RESOURCE_BARRIER rb = Tr(t, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &rb);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = t; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = upload; from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, w, h, 1, pitch};
        l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        rb = Tr(t, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        l->ResourceBarrier(1, &rb);
        SubmitList();
        copied = true;
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    snprintf(b, sizeof b, "xrReleaseSwapchainImage(%s)", what);
    if (!Ok(xrReleaseSwapchainImage(sc, &ri), b)) return -1;
    return copied ? 1 : 0;   // no list could be opened (the GPU is behind): the image holds nothing yet - made again later, not shown empty for good
}

bool MakePointerImage(HCURSOR cur) {
    if (g_ptr.sc) {
        ForgetPending(g_ptr.sc);
        SettleGpu();
        gpufault::Note("xr: pointer swapchain destroyed", (uint64_t)(uintptr_t)g_ptr.sc);
        xrDestroySwapchain(g_ptr.sc);
        g_ptr.sc = XR_NULL_HANDLE;
    }
    g_ptr.ok = false;
    g_ptr.shape = cur;
    ICONINFO ii{};
    if (!GetIconInfo(cur, &ii)) return false;
    BITMAP bm{};
    GetObjectW(ii.hbmMask, sizeof bm, &bm);
    const int n = std::clamp((int)bm.bmWidth, 16, 256);   // a monochrome cursor's mask is two images tall: its width is the size
    g_ptr.n = n; g_ptr.hotX = (int)ii.xHotspot; g_ptr.hotY = (int)ii.yHotspot;
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    // on black, on white
    std::vector<uint32_t> px[2];
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    for (int k = 0; k < 2; ++k) {
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), n, -n, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) { if (dib) DeleteObject(dib); continue; }
        HGDIOBJ old = SelectObject(dc, dib);
        memset(bits, k ? 0xFF : 0x00, (size_t)n * n * 4);
        DrawIconEx(dc, 0, 0, cur, 0, 0, 0, nullptr, DI_NORMAL);
        GdiFlush();
        px[k].assign((const uint32_t*)bits, (const uint32_t*)bits + (size_t)n * n);
        SelectObject(dc, old);
        DeleteObject(dib);
    }
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (px[0].size() != (size_t)n * n || px[1].size() != (size_t)n * n) return false;
    // premultiplied: the colour as drawn on black, alpha = 1 - (white - black)
    std::vector<uint32_t> rgba((size_t)n * n);
    for (size_t i = 0; i < rgba.size(); ++i) {
        const uint32_t b = px[0][i], w = px[1][i];   // BGRA
        const uint32_t cover = 255 - std::clamp((int)(w & 0xFF) - (int)(b & 0xFF), 0, 255);
        rgba[i] = ((b >> 16) & 0xFF) | (b & 0xFF00) | ((b & 0xFF) << 16) | (cover << 24);
    }
    const int got = MakeStaticImage((uint32_t)n, (uint32_t)n, rgba.data(), g_ptrUpload, g_ptr.sc, "pointer");
    if (got == 0) { g_ptr.shape = nullptr; g_ptrRetryAt = GetTickCount64() + 1000; }   // made again in a second
    g_ptr.ok = got > 0;
    return g_ptr.ok;
}

// The pointer as a quad on the flat picture (pose and size of that picture's quad), or false:
// hidden, outside the game's window, or the game not in front.
bool PointerQuad(const XrCompositionLayerQuad& pic, XrCompositionLayerQuad* out) {
    static int why = -1;   // the reason it is not shown, to the log when it changes
    auto no = [&](int w, const char* said) {
        if (w != why) { why = w; LogF("vrcam: openxr - mouse pointer not shown: %s", said); }
        return false;
    };
    if (!g_wnd) return no(1, "no game window known");
    CURSORINFO ci{sizeof ci};
    if (!GetCursorInfo(&ci) || !ci.hCursor) return no(2, "no pointer (GetCursorInfo)");
    if (!(ci.flags & CURSOR_SHOWING)) return no(3, "the game hides it");
    POINT p = ci.ptScreenPos;
    RECT rc{};
    if (!ScreenToClient(g_wnd, &p) || !GetClientRect(g_wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return no(4, "the window has no size");
    if (p.x < 0 || p.y < 0 || p.x >= rc.right || p.y >= rc.bottom) return no(5, "outside the game's window");
    if (why != 0) { why = 0; Log("vrcam: openxr - mouse pointer shown on the flat picture"); }
    if (ci.hCursor != g_ptr.shape && (GetTickCount64() < g_ptrRetryAt || !MakePointerImage(ci.hCursor))) return false;
    if (!g_ptr.ok) return false;
    const float W = pic.size.width, H = pic.size.height, n = (float)g_ptr.n;
    // The pointer's size is its pixels of the picture: with a render size ([render] vr_resolution) the game's, not the window's
    // (the window may be small, the picture is as big as the game draws it). Where it points is the window's fraction either way.
    vrres::Size game;
    const float picW = vrres::GameSize(g_wnd, &game) ? (float)game.w : (float)rc.right, picH = game ? (float)game.h : (float)rc.bottom;
    const float fx = (float)p.x / rc.right + (0.5f * n - (float)g_ptr.hotX) / picW;   // the image's middle, as a fraction of the picture
    const float fy = (float)p.y / rc.bottom + (0.5f * n - (float)g_ptr.hotY) / picH;
    const XrVector3f off = Rot(pic.pose.orientation, {(fx - 0.5f) * W, (0.5f - fy) * H, 0.003f});   // a hair in front
    *out = pic;
    out->subImage.swapchain = g_ptr.sc;
    out->subImage.imageRect = {{0, 0}, {g_ptr.n, g_ptr.n}};
    out->pose.position = {pic.pose.position.x + off.x, pic.pose.position.y + off.y, pic.pose.position.z + off.z};
    out->size = {W * n / picW, H * n / picH};
    out->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    return true;
}

// F1 and F3 in 3D (pointer_3d): the game's own pointer where its mouse points - the ground point under the mouse's pixel, as the game's mouse
// code sees it (vr/pointer3d.h; vrcam answers). The scene stands in the room, so the pointer is a quad too, hung at that point turned to the eyes,
// its hot spot on the point, as large as the pointer is on the flat picture seen from the same distance: a few centimetres, 2.4 degrees for a
// 32 px cursor. Nothing of the mouse is touched: only the picture of the cursor the game shows. `eyes` is the middle between the eyes (the room).
bool Pointer3dQuad(XrVector3f eyes, XrQuaternionf look, XrCompositionLayerQuad* out) {
    static int why = -1;   // the reason it is not shown, to the log when it changes
    auto no = [&](int w, const char* said) {
        if (w != why) { why = w; LogF("vrcam: openxr - mouse pointer in F%d (3D) not shown: %s", g_pairMode == kViewsWindow ? 1 : 3, said); }
        return false;
    };
    if (!g_cfg.pointer3d || !g_pointerAt) return false;
    if (!g_wnd) return no(1, "no game window known");
    CURSORINFO ci{sizeof ci};
    if (!GetCursorInfo(&ci) || !ci.hCursor) return no(2, "no pointer (GetCursorInfo)");
    if (!(ci.flags & CURSOR_SHOWING)) return no(3, "the game hides it");
    POINT p = ci.ptScreenPos;
    RECT rc{};
    if (!ScreenToClient(g_wnd, &p) || !GetClientRect(g_wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return no(4, "the window has no size");
    if (p.x < 0 || p.y < 0 || p.x >= rc.right || p.y >= rc.bottom) return no(5, "outside the game's window");
    const float u = ((float)p.x + 0.5f) / (float)rc.right, v = ((float)p.y + 0.5f) / (float)rc.bottom;
    PointerAt at{};
    if (!g_pointerAt(g_pairMode, u, v, &at)) return no(6, "no ground under it (the sky, or the view's camera not built yet)");
    if (ci.hCursor != g_ptr.shape && (GetTickCount64() < g_ptrRetryAt || !MakePointerImage(ci.hCursor))) return false;
    if (!g_ptr.ok) return false;
    XrVector3f point{at.at[0], at.at[1], at.at[2]};
    if (g_pairMode == kViewsWindow) {   // the window's frame into the room
        const XrVector3f r = Rot(Yaw(g_winAt.yaw), point);
        point = {r.x + g_winAt.centre.x, r.y + g_winAt.centre.y, r.z + g_winAt.centre.z};
    }
    const XrVector3f to{point.x - eyes.x, point.y - eyes.y, point.z - eyes.z};
    const float dist = std::max(0.2f, sqrtf(to.x * to.x + to.y * to.y + to.z * to.z));
    const float n = (float)g_ptr.n;
    const float px = std::max(0.0004f, 0.0013f * dist);   // metres a cursor pixel stands for at that distance
    const XrVector3f off = Rot(look, {(0.5f * n - (float)g_ptr.hotX) * px, ((float)g_ptr.hotY - 0.5f * n) * px, 0.004f});   // the hot spot on the point, a hair to the eyes
    *out = XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    out->space = g_space;
    out->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    out->subImage.swapchain = g_ptr.sc;
    out->subImage.imageRect = {{0, 0}, {g_ptr.n, g_ptr.n}};
    out->pose.orientation = look;
    out->pose.position = {point.x + off.x, point.y + off.y, point.z + off.z};
    out->size = {n * px, n * px};
    out->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    if (why != 0) { why = 0; LogF("vrcam: openxr - mouse pointer shown on F%d's ground (3D): the game's mouse at %.3f, %.3f of its window is the ground %.2f m from you, at %.2f %.2f %.2f",
                                  g_pairMode == kViewsWindow ? 1 : 3, u, v, dist, point.x, point.y, point.z); }
    return true;
}

// -- the game's interface in the room ---------------------------------------------
// The toolbar and the map hud.cpp takes out of the picture (NativeHud), hung where
// FlatVR hung them from BodyWalkVR_GameHud - the same anchors, sizes and turns, the
// same look ([hud] in d2r_vr.ini) - as quad layers of our own: each piece drawn every
// headset frame into a swapchain of its own by FlatVR's shader (the toolbar taken
// from premultiplied transmittance to plain premultiplied colour, cut in two if asked;
// the map as a glass orb in the palm), on the game's queue after the pair.
//
// The shader is FlatVR's own, as it is (its author's leave to publish it here).
const char kHudShader[] = R"(
cbuffer Hud : register(b0) {
  float g_encoded; float g_time; float g_aspect; float g_size;
  float g_glow;   // the orb's halo and rim, 0..1 (flat_vr_game_hud_orb_glow)
  float3 g_tint;  // the orb's colour: halo, rim, mist (flat_vr_game_hud_orb_color)
  float g_zoom;   // the map on the orb, 1 = its width across the ball
  float g_alpha;  // the orb's glass, rim and halo (the map's lines keep theirs)
  float g_split;  // the toolbar in two halves side by side: 0 no, 1 the turned half below, 2 above (bar_split)
  float g_opacity;  // the whole picture's opacity, premultiplied (the monster bar's [hud] monster_alpha); 0 = whole - the other callers pass 0
};
Texture2D<float4> g_src : register(t0);
SamplerState g_smp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// The toolbar cut in two (g_split): the output is half as wide and twice as
// tall. The right half (blue orb at its right end) is copied as it is; the left
// half (red orb at its left end) turned end for end beside it, so the red orb
// lands next to the blue one.
int2 SplitSource(int2 p) {
  uint w, h;
  g_src.GetDimensions(w, h);
  const int half = (int(w) + 1) / 2, H = int(h);
  const bool turnedBelow = g_split < 1.5;
  const bool turned = turnedBelow ? p.y >= H : p.y < H;
  const int row = turnedBelow ? (turned ? p.y - H : p.y) : (turned ? p.y : p.y - H);
  if (!turned)
    return int2(int(w) - half + p.x, row);
  return int2(half - 1 - p.x, H - 1 - row);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
  const int2 at = g_split > 0.5 ? SplitSource(int2(pos.xy)) : int2(pos.xy);
  float4 c = g_src.Load(int3(at, 0));
  float cover = saturate(1.0 - c.a);
  float3 straight = cover > 1e-4 ? saturate(max(c.rgb, 0.0) / cover) : 0.0;
  if (g_encoded > 0.5)
    straight = pow(straight, 2.2);
  const float k = g_opacity > 0.0 ? saturate(g_opacity) : 1.0;
  return float4(straight * cover, cover) * k;
}

float Hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float Noise(float2 p) {
  float2 i = floor(p), f = frac(p);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x),
              lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}

static const float kSpan = 1.6;  // the quad reaches this many radii from the centre (kOrbSpan)

float4 PSOrb(float4 pos : SV_Position) : SV_Target {
  float2 q = (pos.xy / g_size * 2.0 - 1.0) * kSpan;
  q.y = -q.y;
  float r = length(q);
  float px = 2.0 * kSpan / g_size;  // one pixel, in radii

  // The halo round the ball.
  float d = max(r - 1.0, 0.0);
  // Gone to nothing before the quad's edge (inscribed circle), so no square shows.
  float fade = 1.0 - smoothstep(0.0, kSpan - 1.05, d);
  float halo = exp(-d * 6.0) * 0.45 * g_glow * fade * (0.85 + 0.15 * sin(g_time * 1.7));
  halo *= g_alpha;
  float4 outside = float4(g_tint * halo, halo);

  float z = sqrt(saturate(1.0 - r * r));
  float3 n = float3(q, z);

  // The whole map stretched over the front half, its width across the ball,
  // its own shape kept (nothing above and below it on a wide map).
  float2 uv = float2(0.5 + q.x * 0.5, 0.5 - q.y * 0.5 * max(g_aspect, 0.01)) - 0.5;
  uv = uv / max(g_zoom, 0.1) + 0.5;  // zoomed about the middle
  float inMap = all(uv >= 0.0) && all(uv <= 1.0) ? 1.0 : 0.0;
  float4 c = g_src.SampleLevel(g_smp, saturate(uv), 0);
  // D2R's automap is thin, half see-through lines: their coverage is raised
  // so they read over the glass instead of drowning in it.
  float cover = saturate((1.0 - c.a) * 2.5) * inMap;
  float3 map = cover > 1e-4 ? saturate(max(c.rgb, 0.0) / cover) : 0.0;
  if (g_encoded > 0.5)
    map = pow(map, 2.2);

  // Glass, mist, rim, highlight.
  float3 col = g_tint * 0.05;
  float mist = Noise(q * 3.0 + float2(g_time * 0.15, -g_time * 0.10)) *
               Noise(q * 5.0 - float2(g_time * 0.20, g_time * 0.07));
  col += g_tint * 0.5 * mist * (1.0 - 0.5 * z);
  float fres = pow(1.0 - z, 3.0);
  col += g_tint * fres * (0.3 + 0.9 * g_glow);
  float3 l = normalize(float3(-0.4, 0.6, 0.7));
  float spec = pow(saturate(dot(n, normalize(l + float3(0, 0, 1)))), 60.0) * 0.9;
  // The glass is see-through - the room shows through it, more at the middle
  // than at the rim - and the map lies over it, nearly opaque where drawn.
  float glass = saturate(0.30 + 0.55 * fres + spec) * g_alpha;
  float mapA = cover * (0.7 + 0.3 * z);
  float a = saturate(glass + mapA * (1.0 - glass));
  float3 lit = saturate(col + spec) * glass * (1.0 - mapA) + map * 1.3 * mapA;
  float4 inside = float4(saturate(lit), a);

  float w = saturate((1.0 - r) / px + 0.5);  // the edge, a pixel wide
  return lerp(outside, inside, w);
}
)";

// A gesture zone's ball (HangZones) in its cell of the zones' atlas: see-through glass
// in the zone's colour, thin at the middle and thick at the rim, a highlight, and a
// halo as strong as its glow. Premultiplied, as the quad layers take it.
const char kBallShader[] = R"(
cbuffer Ball : register(b0) {
  float2 g_cell;   // the cell's top left, pixels
  float g_size;    // the cell's side, pixels
  float g_time;    // seconds, for the halo's breathing
  float3 g_rgb;    // the zone's colour, linear
  float g_alpha;   // its opacity
  float g_glow;    // its halo and rim, 0..1
  float3 g_pad;
};

static const float kSpan = 1.6;  // the quad reaches this many radii from the centre (kOrbSpan)

float4 PSBall(float4 pos : SV_Position) : SV_Target {
  float2 q = ((pos.xy - g_cell) / g_size * 2.0 - 1.0) * kSpan;
  q.y = -q.y;
  float r = length(q);
  float px = 2.0 * kSpan / g_size;  // one pixel, in radii

  // The halo, gone to nothing before the quad's edge.
  float d = max(r - 1.0, 0.0);
  float fade = 1.0 - smoothstep(0.0, kSpan - 1.05, d);
  float halo = exp(-d * 7.0) * 0.5 * g_glow * fade * (0.85 + 0.15 * sin(g_time * 2.1)) * g_alpha;
  float4 outside = float4(g_rgb * halo, halo);

  float z = sqrt(saturate(1.0 - r * r));
  float3 n = float3(q, z);
  float fres = pow(1.0 - z, 2.5);
  float3 l = normalize(float3(-0.4, 0.6, 0.7));
  float spec = pow(saturate(dot(n, normalize(l + float3(0, 0, 1)))), 50.0) * 0.8;
  float a = saturate((0.35 + 0.65 * fres) * g_alpha + 0.6 * spec);
  float3 col = saturate(g_rgb * (0.55 + 0.45 * fres + 0.6 * g_glow * fres) + spec);
  float4 inside = float4(col * a, a);

  float w = saturate((1.0 - r) / px + 0.5);  // the edge, a pixel wide
  return lerp(outside, inside, w);
}
)";

constexpr uint32_t kOrbPx = 512;   // the orb's swapchain, square
constexpr float kOrbSpan = 1.6f;   // the orb's quad, in radii from its centre (kSpan in the shader)

ID3D12RootSignature* g_hudRs = nullptr;
ID3D12PipelineState* g_hudPso[3] = {};   // the flat piece, the orb, a gesture zone's ball
ID3D12DescriptorHeap* g_srvHeap = nullptr;   // shader-visible, a ring: two pieces a frame
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
UINT g_srvInc = 0, g_rtvInc = 0;
constexpr int kSrvRing = 64;   // (was 16: as many as the allocators - a slot is rewritten only after the list that read it is done; room to spare now)
uint32_t g_srvAt = 0;
bool g_hudFailed = false;

bool MakeHudPipeline() {
    if (g_hudPso[0] && g_hudPso[1] && g_hudPso[2]) return true;
    if (g_hudFailed) return false;
    g_hudFailed = true;   // until it is all made
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) { Log("vrcam: openxr - interface in the room: no shader compiler (d3dcompiler_47.dll) - the toolbar and the map stay off"); return false; }
    ID3DBlob* blobs[4] = {};
    const char* const entry[4] = {"VSMain", "PSMain", "PSOrb", "PSBall"};
    for (int i = 0; i < 4; ++i) {
        ID3DBlob* err = nullptr;
        const char* const src = i == 3 ? kBallShader : kHudShader;
        if (FAILED(compile(src, strlen(src), "d2rvr_hud", nullptr, nullptr, entry[i], i ? "ps_5_0" : "vs_5_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blobs[i], &err))) {
            LogF("vrcam: openxr - interface in the room: %s did not compile: %s", entry[i], err ? (const char*)err->GetBufferPointer() : "?");
            if (err) err->Release();
            return false;
        }
        if (err) err->Release();
    }
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants = {0, 0, 12};
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable = {1, &range};
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxLOD = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* rsBlob = nullptr, *rsErr = nullptr;
    const bool rsOk = SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)) &&
                      SUCCEEDED(g_dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&g_hudRs));
    if (rsBlob) rsBlob->Release();
    if (rsErr) rsErr->Release();
    bool ok = rsOk;
    for (int k = 0; k < 3 && ok; ++k) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = g_hudRs;
        d.VS = {blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize()};
        d.PS = {blobs[1 + k]->GetBufferPointer(), blobs[1 + k]->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        d.SampleDesc.Count = 1;
        ok = SUCCEEDED(g_dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&g_hudPso[k]));
    }
    for (ID3DBlob* b : blobs) if (b) b->Release();
    if (ok) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvRing, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        ok = SUCCEEDED(g_dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g_srvHeap));
        D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvs, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        ok = ok && SUCCEEDED(g_dev->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void**)&g_rtvHeap));
        g_srvInc = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_rtvInc = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        if (ok) {   // every slot a valid (null) view: the gesture zones' balls bind slot 0 and read nothing from it, and a table over a slot nobody wrote is undefined
            D3D12_SHADER_RESOURCE_VIEW_DESC nv{};
            nv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            nv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            nv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nv.Texture2D.MipLevels = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE h = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
            for (int i = 0; i < kSrvRing; ++i, h.ptr += g_srvInc) g_dev->CreateShaderResourceView(nullptr, &nv, h);
            g_srvHeap->SetName(L"d2rvr xr srv ring");
            g_rtvHeap->SetName(L"d2rvr xr rtv heap");
        }
    }
    if (!ok) { Log("vrcam: openxr - interface in the room: the pipeline could not be made - the toolbar and the map stay off"); return false; }
    g_hudFailed = false;
    Log("vrcam: openxr - interface in the room: FlatVR's shader compiled (the toolbar, the map orb; the gesture zones' balls)");
    return true;
}

bool EnsureHudOut(HudOut& o, uint32_t w, uint32_t h, const char* what) {
    if (o.sc && o.w == w && o.h == h) return true;
    if (o.sc) {
        ForgetPending(o.sc);   // (an image of it whose wait ran out is waited for no more: a new chain may get the same handle)
        SettleGpu();           // (our lists that draw into its images are done before they go, and before their views are written over)
        gpufault::Note("xr: interface swapchain destroyed", (uint64_t)(uintptr_t)o.sc, ((uint64_t)o.w << 32) | o.h);
        xrDestroySwapchain(o.sc);
        o.sc = XR_NULL_HANDLE;
        gpufault::Note("xr: render-target slots given back", (uint64_t)o.rtv0, (uint64_t)o.img.size());
        g_rtvSlots.Give(o.rtv0, (int)o.img.size());
        o.img.clear();
    }
    o.w = w; o.h = h;
    bool srgb = false;
    for (int64_t f : g_formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) srgb = true;
    if (!srgb) return false;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; ci.sampleCount = 1; ci.width = w; ci.height = h; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    if (!Ok(xrCreateSwapchain(g_session, &ci, &o.sc), "xrCreateSwapchain(interface)")) { o.sc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(o.sc, 0, &n, nullptr);
    std::vector<XrSwapchainImageD3D12KHR> img(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(o.sc, n, &n, (XrSwapchainImageBaseHeader*)img.data());
    o.rtv0 = g_rtvSlots.Take((int)n);
    if (o.rtv0 < 0) {   // the heap is full: no view of anybody else's is taken over (that drew into the wrong chain's image)
        xrDestroySwapchain(o.sc);
        o.sc = XR_NULL_HANDLE;
        o.rtv0 = 0;
        static bool told = false;
        if (!told) { told = true; LogF("vrcam: openxr - interface in the room: no room in the render-target heap for the %s's %u images - it stays out", what, n); }
        return false;
    }
    gpufault::Note("xr: interface swapchain made", (uint64_t)(uintptr_t)o.sc, ((uint64_t)w << 32) | h);
    gpufault::Note("xr: render-target slots taken", (uint64_t)o.rtv0, n);
    for (uint32_t i = 0; i < n; ++i) {
        o.img.push_back(img[i].texture);
        D3D12_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        hnd.ptr += (SIZE_T)(o.rtv0 + (int)i) * g_rtvInc;
        g_dev->CreateRenderTargetView(img[i].texture, &rv, hnd);
    }
    LogF("vrcam: openxr - interface in the room: the %s's swapchain %ux%u", what, w, h);
    return true;
}

// One piece drawn into its swapchain's next image, on our list on the game's queue.
// dw x dh: the corner of the image drawn into (0 = all of it): a chain larger than the picture, the monster bar's.
bool DrawHud(HudOut& o, ID3D12Resource* src, bool orb, const float cb[12], uint32_t dw = 0, uint32_t dh = 0) {
    if (!dw || dw > o.w) dw = o.w;
    if (!dh || dh > o.h) dh = o.h;
    uint32_t idx = 0;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (NextImage(o.sc, &idx, "interface") <= 0) return false;
    ID3D12GraphicsCommandList* l = OpenList();
    if (!l || idx >= o.img.size()) { xrReleaseSwapchainImage(o.sc, &ri); return false; }
    const uint32_t slot = g_srvAt++ % kSrvRing;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)slot * g_srvInc;
    g_dev->CreateShaderResourceView(src, &sv, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)slot * g_srvInc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(o.rtv0 + (int)idx) * g_rtvInc;
    l->SetDescriptorHeaps(1, &g_srvHeap);
    l->SetGraphicsRootSignature(g_hudRs);
    l->SetPipelineState(g_hudPso[orb ? 1 : 0]);
    l->SetGraphicsRoot32BitConstants(0, 12, cb, 0);
    l->SetGraphicsRootDescriptorTable(1, gpu);
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)dw, (float)dh, 0.0f, 1.0f};
    const D3D12_RECT sc{0, 0, (LONG)dw, (LONG)dh};
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &sc);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->DrawInstanced(3, 1, 0, 0);   // the image stays a render target: as the binding wants it back
    SubmitList();
    return Ok(xrReleaseSwapchainImage(o.sc, &ri), "xrReleaseSwapchainImage(interface)");
}

// -- where a piece hangs (FlatVR's rules) -------------------------------------------
XrVector3f Plus(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Minus(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Times(XrVector3f v, float k) { return {v.x * k, v.y * k, v.z * k}; }
float DotV(XrVector3f a, XrVector3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f CrossV(XrVector3f a, XrVector3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
XrVector3f Unit(XrVector3f v) { const float l = sqrtf(DotV(v, v)); return l > 1e-6f ? Times(v, 1.0f / l) : XrVector3f{0, 0, 1}; }
XrQuaternionf AboutAxis(XrVector3f ax, float deg) { const float h = 0.5f * deg * 0.0174532925f, s2 = sinf(h); return {ax.x * s2, ax.y * s2, ax.z * s2, cosf(h)}; }
// the rotation whose columns are these unit axes
XrQuaternionf FromAxes(XrVector3f x, XrVector3f y, XrVector3f z) {
    const float tr = x.x + y.y + z.z;
    if (tr > 0.0f) { const float k = sqrtf(tr + 1.0f) * 2.0f; return {(y.z - z.y) / k, (z.x - x.z) / k, (x.y - y.x) / k, 0.25f * k}; }
    if (x.x > y.y && x.x > z.z) { const float k = sqrtf(1.0f + x.x - y.y - z.z) * 2.0f; return {0.25f * k, (y.x + x.y) / k, (z.x + x.z) / k, (y.z - z.y) / k}; }
    if (y.y > z.z) { const float k = sqrtf(1.0f + y.y - x.x - z.z) * 2.0f; return {(y.x + x.y) / k, 0.25f * k, (z.y + y.z) / k, (z.x - x.z) / k}; }
    const float k = sqrtf(1.0f + z.z - x.x - y.y) * 2.0f;
    return {(z.x + x.z) / k, (z.y + y.z) / k, 0.25f * k, (x.y - y.x) / k};
}
XrVector3f P3(const float* p) { return {p[0], p[1], p[2]}; }
XrQuaternionf Q4(const float* q) { return {q[0], q[1], q[2], q[3]}; }

// The forearm from a controller: the wrist a little behind the grip, the shoulder from the
// head, the elbow between by a two-bone solve with the elbow down and out; the inside of
// the arm is where the palm faces. An adult's lengths: only the forearm's direction counts.
bool ForearmFromHand(const D2RVR_XrPose& head, const D2RVR_XrPose& aim, int hand, XrVector3f* wrist, XrVector3f* axis, XrVector3f* inside) {
    if (!aim.valid || !head.valid) return false;
    const float side = hand == 1 ? 1.0f : -1.0f;   // right +, left -
    *wrist = Plus(P3(aim.pos), Rot(Q4(aim.rot), {0.0f, -0.02f, 0.09f}));
    const XrVector3f f0 = Rot(Q4(head.rot), {0.0f, 0.0f, -1.0f});
    const XrVector3f fwd = Unit({f0.x, 0.0f, f0.z}), right{-fwd.z, 0.0f, fwd.x}, down{0.0f, -1.0f, 0.0f};
    const XrVector3f shoulder = Plus(P3(head.pos), Plus(Plus(Times(down, 0.23f), Times(right, side * 0.18f)), Times(fwd, -0.05f)));
    constexpr float kUpper = 0.30f, kFore = 0.27f;
    const XrVector3f to = Minus(*wrist, shoulder);
    float d = sqrtf(DotV(to, to));
    if (d < 1e-3f) return false;
    const XrVector3f dir = Times(to, 1.0f / d);
    d = std::min(d, kUpper + kFore - 1e-3f);
    const float a = (kUpper * kUpper - kFore * kFore + d * d) / (2.0f * d), h = sqrtf(std::max(0.0f, kUpper * kUpper - a * a));
    XrVector3f pole = Plus(Plus(down, Times(right, side * 0.5f)), Times(fwd, -0.3f));
    pole = Minus(pole, Times(dir, DotV(pole, dir)));
    if (DotV(pole, pole) < 1e-6f) pole = Minus(down, Times(dir, DotV(down, dir)));
    pole = Unit(pole);
    const XrVector3f elbow = Plus(shoulder, Plus(Times(dir, a), Times(pole, h)));
    *axis = Unit(Minus(*wrist, elbow));
    const XrVector3f palm = Rot(Q4(aim.rot), {hand == 0 ? 1.0f : -1.0f, 0.0f, 0.0f});
    *inside = Minus(palm, Times(*axis, DotV(palm, *axis)));
    if (DotV(*inside, *inside) < 1e-6f) return false;
    *inside = Unit(*inside);
    return true;
}

// Where a piece hangs: on the hero's forearm (the toolbar, from the game's rig), on a hand
// (the forearm from the controller, or the orb in the palm), low in front (3) or at the
// chest (4) turning with the head's yaw only; an orb always faces the eyes.
bool HudPose(const D2RVR_XrInput& in, int anchor, bool map, bool orb, float width, const FlatVRGameHudLook& look,
             const FlatVRGameHudPose* game, XrPosef* out) {
    const D2RVR_XrPose& head = in.head;
    if (!head.valid) return false;
    const XrVector3f hp = P3(head.pos), f0 = Rot(Q4(head.rot), {0.0f, 0.0f, -1.0f});
    const XrQuaternionf yaw = Yaw(atan2f(-f0.x, -f0.z));
    const float back = 0.03f + 0.5f * width + 0.01f * look.bar_along_cm;
    auto along = [&](XrVector3f wrist, XrVector3f axis, XrVector3f inside) {
        out->position = Plus(Minus(wrist, Times(axis, back)), Times(inside, 0.03f));   // back from the wrist, lifted off the inside
        out->orientation = FromAxes(axis, CrossV(inside, axis), inside);
    };
    if (game && game->valid && !orb && (anchor == 1 || anchor == 2) && head.valid == 1) {
        // the hero's own forearm, in the hand frame (from the head, turned by its yaw only)
        const XrVector3f elbow = Plus(hp, Rot(yaw, P3(game->elbow))), wrist = Plus(hp, Rot(yaw, P3(game->wrist)));
        const XrVector3f axis = Unit(Minus(wrist, elbow));
        XrVector3f inside = Rot(yaw, P3(game->across));
        inside = Minus(inside, Times(axis, DotV(inside, axis)));
        if (DotV(inside, inside) > 1e-6f) { along(wrist, axis, Unit(inside)); return true; }
    }
    if (anchor == 1 || anchor == 2) {
        const D2RVR_XrPose& aim = in.hand[anchor - 1].aim;
        if (!aim.valid) return false;
        if (orb) {
            const float palm = anchor == 1 ? 1.0f : -1.0f;
            out->position = Plus(P3(aim.pos), Rot(Q4(aim.rot), {palm * 0.08f, -0.01f, 0.06f}));
        } else {
            XrVector3f wrist, axis, inside;
            if (!ForearmFromHand(head, aim, anchor - 1, &wrist, &axis, &inside)) return false;
            along(wrist, axis, inside);
            return true;
        }
    } else if (anchor == 3 || anchor == 4) {
        const bool chest = anchor == 4;
        const XrVector3f at = chest ? (map ? XrVector3f{0.14f, -0.44f, -0.20f} : XrVector3f{0.0f, -0.46f, -0.20f})
                              : map ? XrVector3f{-0.32f, -0.12f, -0.60f} : XrVector3f{0.0f, -0.32f, -0.55f};
        out->orientation = Mul(yaw, AboutAxis({1, 0, 0}, chest ? -65.0f : map ? -15.0f : -35.0f));
        out->position = Plus(hp, Rot(yaw, at));
        if (!orb) return true;
    } else {
        return false;
    }
    const XrVector3f z = Unit(Minus(hp, out->position)), up = Rot(Q4(head.rot), {0.0f, 1.0f, 0.0f});
    const XrVector3f x = Unit(CrossV(up, z));
    out->orientation = FromAxes(x, CrossV(z, x), z);
    return true;
}

// The pieces out of the picture this frame, drawn and appended to layers[] (from nl on, at most max).
uint32_t HangHud(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    hud::NativePiece piece[hud::kNativePieces];
    FlatVRGameHudLook look{};
    FlatVRGameHudPose barPose{};
    if (!g_actionsOk || !hud::NativeHud(piece, &look, &barPose)) return nl;
    if (!piece[0].visible && !piece[1].visible) return nl;
    if (!MakeHudPipeline()) return nl;
    const float seconds = (float)(GetTickCount64() % 3600000ull) / 1000.0f;   // the orb's mist; wraps hourly
    for (int i = 0; i <= hud::kNativeMap && nl < max; ++i) {   // (a menu's panel: HangPanel)
        const hud::NativePiece& src = piece[i];
        if (!src.visible || !src.w || !src.h) continue;
        const bool map = i == hud::kNativeMap;
        const int anchor = (int)(map ? look.map_anchor : look.bar_anchor);
        if (anchor == 0) continue;
        const bool orb = map && look.map_orb != 0;
        const float width = std::max(0.03f, orb ? look.orb_size_m * kOrbSpan : map ? look.map_width_m : look.bar_width_m);
        const uint32_t split = !map && look.bar_split <= 2 ? look.bar_split : 0;   // the toolbar in two halves: half as long, twice as wide
        const uint32_t outW = orb ? kOrbPx : split ? (src.w + 1) / 2 : src.w, outH = orb ? kOrbPx : split ? src.h * 2 : src.h;
        HudOut& o = g_hudOut[i];
        XrPosef pose{};
        if (!HudPose(g_inNow, anchor, map, orb, width, look, map ? nullptr : &barPose, &pose) ||
            !EnsureHudOut(o, outW, outH, map ? (orb ? "map orb" : "map") : "toolbar"))
            continue;
        if (!map) {   // turned round the arm, tipped, spun, then lifted off it and moved across, in its own frame
            const XrQuaternionf base = pose.orientation;
            const XrQuaternionf turn = Mul(Mul(AboutAxis({1, 0, 0}, look.bar_roll_deg), AboutAxis({0, 1, 0}, look.bar_tip_deg)), AboutAxis({0, 0, 1}, look.bar_spin_deg));
            pose.orientation = Mul(base, turn);
            pose.position = Plus(pose.position, Rot(base, {0.0f, 0.01f * look.bar_side_cm, 0.01f * look.bar_lift_cm}));
            if (split) {   // the kept half where it lay in the whole strip, the turned one beside it
                const float h = width * (float)src.h / (float)src.w;
                pose.position = Plus(pose.position, Rot(pose.orientation, {0.25f * width, split == 1 ? -0.5f * h : 0.5f * h, 0.0f}));
            }
        }
        const float cb[12] = {look.encoded ? 1.0f : 0.0f, seconds, (float)src.w / (float)src.h, (float)outW,
                              std::clamp(look.orb_glow, 0.0f, 1.0f),
                              powf(((look.orb_rgb >> 16) & 0xFF) / 255.0f, 2.2f), powf(((look.orb_rgb >> 8) & 0xFF) / 255.0f, 2.2f),
                              powf((look.orb_rgb & 0xFF) / 255.0f, 2.2f),
                              std::clamp(look.orb_zoom, 0.5f, 4.0f), look.orb_alpha > 0.0f ? std::clamp(look.orb_alpha, 0.0f, 1.0f) : 1.0f,
                              (float)split, 0.0f};
        if (!DrawHud(o, src.tex, orb, cb)) continue;
        XrCompositionLayerQuad& q = o.layer;
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied
        q.space = g_roomSpace ? g_roomSpace : g_space;   // where the head and the hands were located
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = o.sc;
        q.subImage.imageRect = {{0, 0}, {(int32_t)outW, (int32_t)outH}};
        q.pose = pose;
        const float quadW = split ? 0.5f * width : width;
        q.size = {quadW, quadW * (float)outH / (float)outW};
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
    }
    return nl;
}

// [hud] monster_bar_room (vr/plate_world.h): the game's own health bar and name of the monster you target, cut out of its interface (hud.cpp), hung in
// the room above that monster's head: turned to the head about the vertical (upright), its width by the distance, its foot the player's gap over the head.
// The place comes from vrcam's tick in the head's turn-only frame (metres from the eyes: x right, y up, z back), put in the room the way the belt's balls are.
uint32_t HangPlate(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    if (nl >= max || !g_actionsOk || !g_inNow.head.valid || !plateworld::Active()) return nl;
    hud::NativePiece src;
    bool encoded = true;
    plateworld::Anchor a;
    const float alpha = plateworld::Opacity();   // [hud] monster_alpha: the bar in the room is faded like the plate in the picture
    if (!plateworld::OpacityVisible(alpha)) return nl;
    if (!hud::PlatePiece(&src, &encoded) || !src.visible || !src.w || !src.h || !plateworld::GetAnchor(&a, GetTickCount64())) return nl;
    // ONE swapchain for every bar, made at the first (plateworld::kChainW x kChainH) and kept: the bar is drawn into its corner, the layer's image rect says which
    plateworld::Fit fit;
    if (!plateworld::FitInto(src.w, src.h, &fit)) return nl;
    if (!MakeHudPipeline() || !EnsureHudOut(g_plateOut, plateworld::kChainW, plateworld::kChainH, "monster bar")) return nl;
    const float width = plateworld::WidthM(a.distM, a.sizePct);
    const float height = width * (float)src.h / (float)src.w;
    const XrVector3f hp = P3(g_inNow.head.pos);
    XrVector3f at = Plus(hp, Rot(Yaw(YawOf(Q4(g_inNow.head.rot))), P3(a.head)));
    at.y += a.gapM + 0.5f * height;   // the bar's foot a gap over the head, not its middle
    XrVector3f z = Minus(hp, at);     // turned to the head, but upright
    z.y = 0.0f;
    z = Unit(z);
    const XrVector3f x = Unit(CrossV({0.0f, 1.0f, 0.0f}, z));
    const float seconds = (float)(GetTickCount64() % 3600000ull) / 1000.0f;
    const float cb[12] = {encoded ? 1.0f : 0.0f, seconds, (float)src.w / (float)src.h, (float)fit.w, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, plateworld::ShaderOpacity(alpha)};
    if (!DrawHud(g_plateOut, src.tex, false, cb, fit.w, fit.h)) return nl;
    XrCompositionLayerQuad& q = g_plateOut.layer;
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied, as the toolbar
    q.space = g_roomSpace ? g_roomSpace : g_space;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = g_plateOut.sc;
    q.subImage.imageRect = {{0, 0}, {(int32_t)fit.w, (int32_t)fit.h}};
    q.pose.orientation = FromAxes(x, {0.0f, 1.0f, 0.0f}, z);
    q.pose.position = at;
    q.size = {width, height};
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
    return nl;
}

// BodyWalk's gesture zones ticked "VR" in its Mapping tab (the belt's potions), handed on
// through Game Link's VrZones: placed by BodyWalk from the head this game gave it, so in
// the space that head was located in (g_roomSpace); none once BodyWalk has sent none for
// half a second.
uint32_t ReadZones(BW_LinkVrZone* out) {
    const uint32_t n = link::VrZones(out, BW_LINK_VRZONES_MAX);
    static bool told = false;
    if (n && !told) { told = true; Log("vrcam: openxr - BodyWalk's gesture zones come (Game Link)"); }
    return n;
}

// The zones as see-through balls, each a quad turned to the head, the nearest kept when
// there are more than the runtime's layers allow, the farthest laid first so a nearer one
// lies over it. Appended to layers[] from nl on, at most max.
uint32_t HangZones(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    BW_LinkVrZone z[BW_LINK_VRZONES_MAX];
    uint32_t n = ReadZones(z);
    // and vrcam's own: a ball on each belt bottle a hand is at (engitem::HoverSpheres, BodyWalk's
    // frame - metres from the head, x right, y up, z back, its turn only), put in the room the
    // way HoldPose puts a held hand back
    float hover[4][8];
    const int nh = g_inNow.head.valid ? D2rvrHoverSpheres(hover, 4) : 0;
    if (nh > 0) {
        const float hy = YawOf(Q4(g_inNow.head.rot));
        for (int i = 0; i < nh && n < BW_LINK_VRZONES_MAX; ++i) {
            const XrVector3f w = Rot(Yaw(hy), {hover[i][0], hover[i][1], hover[i][2]});
            BW_LinkVrZone& b = z[n++];
            b.centre[0] = g_inNow.head.pos[0] + w.x; b.centre[1] = g_inNow.head.pos[1] + w.y; b.centre[2] = g_inNow.head.pos[2] + w.z;
            b.radius = hover[i][3];
            auto byte = [](float v) { return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            b.rgba = byte(hover[i][5]) | byte(hover[i][6]) << 8 | byte(hover[i][7]) << 16 | byte(hover[i][4]) << 24;   // the potion's colour, see-through
            b.glow = 0.5f;
        }
    }
    if (!n || nl >= max || !g_inNow.head.valid || !MakeHudPipeline()) return nl;
    const XrVector3f head = P3(g_inNow.head.pos);
    uint32_t order[BW_LINK_VRZONES_MAX];
    float dist[BW_LINK_VRZONES_MAX];
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (!(z[i].radius > 0.005f && z[i].radius < 1.0f) || !((z[i].rgba >> 24) & 0xFFu)) continue;   // nothing, or not seen
        const XrVector3f d = Minus(P3(z[i].centre), head);
        dist[i] = DotV(d, d);
        if (!(dist[i] < 100.0f)) continue;   // not a number, or off in another room
        order[m++] = i;
    }
    std::sort(order, order + m, [&](uint32_t a, uint32_t b) { return dist[a] < dist[b]; });
    m = std::min({m, kZonesMax, max - nl});
    if (!m) return nl;
    std::reverse(order, order + m);   // the farthest first
    const uint32_t rows = (kZonesMax + kZoneCols - 1) / kZoneCols;
    HudOut& o = g_zoneOut;
    if (!EnsureHudOut(o, kZoneCell * kZoneCols, kZoneCell * rows, "gesture zones")) return nl;
    uint32_t idx = 0;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (NextImage(o.sc, &idx, "gesture zones") <= 0) return nl;
    ID3D12GraphicsCommandList* l = OpenList();
    if (!l || idx >= o.img.size()) { xrReleaseSwapchainImage(o.sc, &ri); return nl; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(o.rtv0 + (int)idx) * g_rtvInc;
    l->SetDescriptorHeaps(1, &g_srvHeap);
    l->SetGraphicsRootSignature(g_hudRs);
    l->SetPipelineState(g_hudPso[2]);
    l->SetGraphicsRootDescriptorTable(1, g_srvHeap->GetGPUDescriptorHandleForHeapStart());   // (the ball reads no picture)
    l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float seconds = (float)(GetTickCount64() % 3600000ull) / 1000.0f;
    const float cell = (float)kZoneCell;
    for (uint32_t k = 0; k < m; ++k) {
        const BW_LinkVrZone& zone = z[order[k]];
        const float x = (float)(k % kZoneCols) * cell, y = (float)(k / kZoneCols) * cell;
        const D3D12_VIEWPORT vp{x, y, cell, cell, 0.0f, 1.0f};
        const D3D12_RECT sc{(LONG)x, (LONG)y, (LONG)(x + cell), (LONG)(y + cell)};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        const float cb[12] = {x, y, cell, seconds,
                              powf((zone.rgba & 0xFF) / 255.0f, 2.2f), powf(((zone.rgba >> 8) & 0xFF) / 255.0f, 2.2f),
                              powf(((zone.rgba >> 16) & 0xFF) / 255.0f, 2.2f), ((zone.rgba >> 24) & 0xFF) / 255.0f,
                              std::clamp(zone.glow, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f};
        l->SetGraphicsRoot32BitConstants(0, 12, cb, 0);
        l->DrawInstanced(3, 1, 0, 0);
        // its quad: kOrbSpan radii each way, turned to the head, kept upright
        const XrVector3f c = P3(zone.centre);
        const XrVector3f zf = Unit(Minus(head, c));
        XrVector3f up{0.0f, 1.0f, 0.0f};
        if (fabsf(DotV(up, zf)) > 0.98f) up = {0.0f, 0.0f, -1.0f};   // straight above or below the head
        const XrVector3f xa = Unit(CrossV(up, zf));
        XrCompositionLayerQuad& q = g_zoneLayer[k];
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied
        q.space = g_roomSpace ? g_roomSpace : g_space;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        q.subImage.swapchain = o.sc;
        q.subImage.imageRect = {{(int32_t)x, (int32_t)y}, {(int32_t)kZoneCell, (int32_t)kZoneCell}};
        q.pose.orientation = FromAxes(xa, CrossV(zf, xa), zf);
        q.pose.position = c;
        const float side = 2.0f * kOrbSpan * zone.radius;
        q.size = {side, side};
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
    }
    SubmitList();
    if (!Ok(xrReleaseSwapchainImage(o.sc, &ri), "xrReleaseSwapchainImage(zones)")) return nl - m;
    static bool told = false;
    if (!told) { told = true; LogF("vrcam: openxr - BodyWalk's gesture zones in the room: %u ball(s)", m); }
    return nl;
}

// The back buffer (PRESENT) into the chain's next image (RENDER_TARGET, as the D3D12
// binding hands them out and wants them back), on the game's queue.
// -- the laser: a controller pointing at the menus is the mouse ([openxr] laser) -----------
// docs/plan_laser_menus.md, after bearstonem/d2r-pcvr (MIT). Every flat frame, in Publish before
// BodyWalk gets the hands: where the laser hand points on the flat picture as the last frame hung
// it (g_flatShown; with menu_3d a menu's panel in a pair's frame, HangPanel) - the cursor goes there; its trigger is the left button, with its grip held at
// the press the right one; the other hand's trigger is Ctrl, its grip Shift. A press starts only
// on the picture, a drag holds on off it. What the laser has is hidden from BodyWalk (no pad
// button goes with a click), each trigger and grip taken or given back only while it is let go.
// A stick or a face button (BodyWalk's pad navigating the panel) parks the laser until it is
// moved on the picture again. vr/panel_input.cpp keeps the game in its controller layout meanwhile.
struct Laser {
    bool drawn = false;        // this frame: the beam drawn (and the dot, hit)
    bool hit = false;          // it points at the picture
    XrVector3f from{}, dir{}, at{};
    float len = 0.0f;          // to the hit, else the beam's own length
    bool pad = false;          // the pad navigates: the laser waits, the focus is the pad's
    bool anchored = false;     // where the pad last went, it pointed at the picture (padU, padV)
    float padU = 0.0f, padV = 0.0f;
    bool takes[2][2] = {};     // [hand][trigger, grip]: the laser's, hidden from BodyWalk
    bool button[2] = {};       // the left, right mouse button held down by us
    bool key[2] = {};          // Ctrl, Shift held down by us
    bool trigger = false;      // the laser hand's trigger the frame before (a press is its edge)
    int said = -1;             // the state last told the log
    uint32_t tellings = 0;
};
Laser g_laser;
// The beam and the dot, made once a session.
struct LaserImage { XrSwapchain sc = XR_NULL_HANDLE; ID3D12Resource* upload = nullptr; uint32_t w = 0, h = 0; int state = 0; ULONGLONG retryAt = 0; };
LaserImage g_beamImg, g_dotImg;
constexpr float kBeamM = 1.5f, kBeamWidthM = 0.006f, kDotM = 0.022f;

bool SendButton(int b, bool down) {
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.dwFlags = b ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP) : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    return SendInput(1, &i, sizeof i) == 1;
}
bool SendKey(WORD vk, bool down) {
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    i.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    return i.ki.wScan && SendInput(1, &i, sizeof i) == 1;
}

// Everything the laser holds let go - the lease kept a moment longer, so the queued release
// still reaches the panel in the pad's layout.
void LaserLetGo() {
    Laser& L = g_laser;
    if (L.button[0] || L.button[1] || L.key[0] || L.key[1]) panelinput::OwnPointer();
    for (int b = 0; b < 2; ++b) if (L.button[b]) { SendButton(b, false); L.button[b] = false; }
    if (L.key[0]) { SendKey(VK_CONTROL, false); L.key[0] = false; }
    if (L.key[1]) { SendKey(VK_SHIFT, false); L.key[1] = false; }
}

void LaserTeardown() {   // under g_lock or before the session exists
    LaserLetGo();
    for (LaserImage* i : {&g_beamImg, &g_dotImg}) {
        if (i->sc) { ForgetPending(i->sc); xrDestroySwapchain(i->sc); }
        ID3D12Resource* upload = i->upload;   // kept until the next image made with it, as the pointer's: its copy may still run
        *i = LaserImage{};
        i->upload = upload;
    }
    g_laser = Laser{};
    g_flatShown = FlatShown{};
}

void LaserTell(int state) {
    Laser& L = g_laser;
    if (state == L.said) return;
    L.said = state;
    if (++L.tellings > 200 && L.tellings % 50 != 0) return;   // a flood (tracking coming and going): every 50th
    static const char* const kWhy[] = {
        "the mouse on the menus: the trigger clicks, with the grip held it right-clicks; the other hand's trigger is Ctrl, its grip Shift",
        "off ([openxr] laser=0)", "no flat picture", "the flat picture is a view (F1, F3), not menus", "the session has no input focus",
        "the game's window is not in front - no clicks", "the laser hand is not tracked (or no controller in it)",
        "the pad navigates the panel - the laser waits until it is moved on the picture",
        "the headset settings panel is open - it has the hands, nothing goes to the game"};
    LogF("vrcam: openxr - laser (%s hand): %s (%u)", g_cfg.laserHand ? "right" : "left", kWhy[state], L.tellings);
}

void LaserStep(D2RVR_XrInput& in, bool focused) {
    Laser& L = g_laser;
    L.drawn = L.hit = false;
    bool holds = L.button[0] || L.button[1] || L.key[0] || L.key[1];
    for (const auto& t : L.takes) holds = holds || t[0] || t[1];
    if (!g_cfg.laser && !holds) { L.pad = false; L.said = -1; return; }   // off, and nothing of it left to let go
    const int lh = g_cfg.laserHand ? 1 : 0, oh = 1 - lh;
    const uint32_t raw[2] = {in.hand[0].buttons, in.hand[1].buttons};   // as pressed, before anything is taken
    int why = 0;
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (!g_cfg.laser) why = 1;
    else if (g_hsOpen.load()) why = 8;   // the settings panel has the hands: no mouse, no keys to the game
    else if ((g_kind != kMono && !(g_kind == kPair && g_cfg.menu3d)) || !g_flatShown.ok) why = 2;   // (menu_3d: a menu's panel in a pair)
    else if (!g_laserWanted.load()) why = 3;
    else if (!focused) why = 4;
    else if (!g_wnd || GetForegroundWindow() != GetAncestor(g_wnd, GA_ROOT)) why = 5;
    else if (!in.hand[lh].active || !g_aimSpace[lh] || XR_FAILED(xrLocateSpace(g_aimSpace[lh], g_space, g_fs.predictedDisplayTime, &aim)) ||
             (aim.locationFlags & need) != need) why = 6;
    // the ray against the picture: u, v from its top left
    float u = 0.0f, v = 0.0f;
    if (!why) {
        L.from = aim.pose.position;
        L.dir = Unit(Rot(aim.pose.orientation, {0.0f, 0.0f, -1.0f}));
        const XrPosef& s = g_flatShown.pose;
        const XrVector3f n = Rot(s.orientation, {0.0f, 0.0f, 1.0f});   // the picture's front
        const float dn = DotV(L.dir, n);
        const float t = dn < -1e-4f ? DotV(Minus(s.position, L.from), n) / dn : -1.0f;
        if (t > 0.0f) {
            const XrQuaternionf back{-s.orientation.x, -s.orientation.y, -s.orientation.z, s.orientation.w};
            const XrVector3f p = Plus(L.from, Times(L.dir, t));
            const XrVector3f local = Rot(back, Minus(p, s.position));
            u = local.x / g_flatShown.size.width + 0.5f;
            v = 0.5f - local.y / g_flatShown.size.height;
            L.hit = u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f;
            if (L.hit) { L.at = p; L.len = t; }
        }
        if (!L.hit) L.len = kBeamM;
    }
    // BodyWalk's pad navigating (a stick off its middle, a face button): the laser waits until the
    // aim moves on the picture again - tracking's tremble does not take the focus back
    bool padNow = false;
    for (const D2RVR_XrHand& o : in.hand)
        if (fabsf(o.stickX) > 0.35f || fabsf(o.stickY) > 0.35f ||
            (o.buttons & (BW_LINK_XRB_PRIMARY | BW_LINK_XRB_SECONDARY | BW_LINK_XRB_STICK | BW_LINK_XRB_MENU))) padNow = true;
    if (why) L.pad = false;
    else if (padNow && !L.button[0] && !L.button[1]) { L.pad = true; L.anchored = L.hit; L.padU = u; L.padV = v; }
    else if (L.pad && !padNow && L.hit && (!L.anchored || hypotf(u - L.padU, v - L.padV) > 0.045f)) L.pad = false;
    const bool acts = !why && !L.pad;   // the laser is the mouse this frame
    L.drawn = acts;
    if (why != 1) LaserTell(why ? why : L.pad ? 7 : 0);
    // who has each trigger and grip, decided while it is let go: the laser's while it points at the picture
    const bool claim = acts && L.hit;
    for (int h = 0; h < 2; ++h) {
        D2RVR_XrHand& o = in.hand[h];
        if (o.trigger < 0.1f && !(raw[h] & BW_LINK_XRB_TRIGGER)) L.takes[h][0] = claim;
        if (o.squeeze < 0.1f && !(raw[h] & BW_LINK_XRB_SQUEEZE)) L.takes[h][1] = claim;
        if (L.takes[h][0]) { o.trigger = 0.0f; o.buttons &= ~(uint32_t)BW_LINK_XRB_TRIGGER; }
        if (L.takes[h][1]) { o.squeeze = 0.0f; o.buttons &= ~(uint32_t)BW_LINK_XRB_SQUEEZE; }
    }
    if (acts) panelinput::OwnPointer();
    else if (L.pad) panelinput::UseController();
    if (acts && L.hit) {   // the cursor where it points: the picture is the game window's client area
        // (the window's REAL pixels, also with a render size ([render] vr_resolution): the picture is the game's whole output, the
        // window's whole client area is the same picture, and the game's own ScreenToClient maps the one to the other - render_resolution.cpp)
        RECT rc{};
        POINT p{}, now{};
        if (GetClientRect(g_wnd, &rc) && rc.right > 0 && rc.bottom > 0) {
            p = {std::clamp((LONG)(u * rc.right), 0L, rc.right - 1), std::clamp((LONG)(v * rc.bottom), 0L, rc.bottom - 1)};
            if (ClientToScreen(g_wnd, &p) && (!GetCursorPos(&now) || now.x != p.x || now.y != p.y)) SetCursorPos(p.x, p.y);
        }
    }
    const bool trig = (raw[lh] & BW_LINK_XRB_TRIGGER) != 0, grip = (raw[lh] & BW_LINK_XRB_SQUEEZE) != 0;
    for (int b = 0; b < 2; ++b)   // let go with the trigger, or when the laser stops
        if (L.button[b] && (!trig || why)) { panelinput::OwnPointer(); SendButton(b, false); L.button[b] = false; }
    if (acts && L.hit && trig && !L.trigger && L.takes[lh][0]) {   // a fresh press on the picture
        const int b = grip && L.takes[lh][1] ? 1 : 0;
        L.button[b] = SendButton(b, true);
        if (L.button[b]) haptics::Play(b ? haptics::Event::MenuRightClick : haptics::Event::MenuClick, lh);   // (lh: 0 left, 1 right, as haptics' hands)
    }
    L.trigger = trig;
    const bool want[2] = {acts && L.takes[oh][0] && (raw[oh] & BW_LINK_XRB_TRIGGER) != 0,
                          acts && L.takes[oh][1] && (raw[oh] & BW_LINK_XRB_SQUEEZE) != 0};
    for (int k = 0; k < 2; ++k)
        if (want[k] != L.key[k]) {
            panelinput::OwnPointer();
            if (SendKey(k ? VK_SHIFT : VK_CONTROL, want[k]) || !want[k]) L.key[k] = want[k];
        }
    // the game's own item tooltip and comparison, without their waits ([openxr] laser_tooltips; panel_input.cpp)
    panelinput::ItemHover(lasertip::Hovering(g_cfg.laserTooltips && viewgate::F4(), acts, L.hit, L.button[0] || L.button[1]));   // (F4 only: elsewhere the game's own delay)
}

// The beam (soft across, fading to its far end, which is the image's top) and the dot, premultiplied:
// a warm white core in a red glow.
void LaserImages() {
    const ULONGLONG now = GetTickCount64();
    auto make = [&](LaserImage& i, uint32_t w, uint32_t h, const char* what, auto shade) {
        if (i.state != 0 || now < i.retryAt) return;
        if (i.sc) { ForgetPending(i.sc); SettleGpu(); gpufault::Note("xr: laser swapchain destroyed", (uint64_t)(uintptr_t)i.sc); xrDestroySwapchain(i.sc); i.sc = XR_NULL_HANDLE; }
        std::vector<uint32_t> px((size_t)w * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                float a = 0.0f, core = 0.0f;
                shade(x, y, &a, &core);
                const float r = 255.0f, g = 70.0f + 170.0f * core, b = 40.0f + 170.0f * core;
                px[(size_t)y * w + x] = (uint32_t)(r * a) | (uint32_t)(g * a) << 8 | (uint32_t)(b * a) << 16 | (uint32_t)(255.0f * a) << 24;
            }
        i.w = w; i.h = h;
        i.state = MakeStaticImage(w, h, px.data(), i.upload, i.sc, what);
        if (i.state == 0) i.retryAt = now + 1000;   // its wait ran out: again in a second
    };
    make(g_beamImg, 16, 128, "laser beam", [](uint32_t x, uint32_t y, float* a, float* core) {
        const float d = fabsf(((float)x + 0.5f) / 8.0f - 1.0f);   // 0 in the middle, 1 at the edges
        const float across = std::max(0.0f, 1.0f - d);
        *a = across * across * (0.35f + 0.65f * (float)y / 127.0f);
        *core = d < 0.3f ? 1.0f - d / 0.3f : 0.0f;
    });
    make(g_dotImg, 32, 32, "laser dot", [](uint32_t x, uint32_t y, float* a, float* core) {
        const float dx = ((float)x + 0.5f) / 16.0f - 1.0f, dy = ((float)y + 0.5f) / 16.0f - 1.0f, r = sqrtf(dx * dx + dy * dy);
        *a = r < 0.6f ? 1.0f : r < 1.0f ? 1.0f - (r - 0.6f) / 0.4f : 0.0f;
        *core = r < 0.35f ? 1.0f : r < 0.55f ? 1.0f - (r - 0.35f) / 0.2f : 0.0f;
    });
}

// What a beam is drawn from: the game's laser (g_laser, over the flat picture) or the settings panel's (xr_headset_settings.h).
struct BeamView {
    bool drawn = false, hit = false;
    XrVector3f from{}, dir{}, at{};
    float len = 0.0f;
    XrQuaternionf surface{0.0f, 0.0f, 0.0f, 1.0f};   // the surface it meets: the dot lies in it
};
// A laser drawn over a surface: the beam from the controller (to where it points, or kBeamM on), turned to the eyes about
// its own line, and the dot a hair in front of the surface.
uint32_t HangBeam(const BeamView& L, const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& beam, XrCompositionLayerQuad& dot) {
    if (!L.drawn) return nl;
    LaserImages();
    if (g_beamImg.state > 0 && nl < max) {
        const XrVector3f mid = Plus(L.from, Times(L.dir, 0.5f * L.len));
        const XrVector3f eyes = g_viewsOk ? Times(Plus(g_views[0].pose.position, g_views[1].pose.position), 0.5f) : g_cPos;
        XrVector3f z = Minus(eyes, mid);
        z = Minus(z, Times(L.dir, DotV(z, L.dir)));
        if (DotV(z, z) < 1e-8f) z = CrossV(L.dir, fabsf(L.dir.y) < 0.9f ? XrVector3f{0.0f, 1.0f, 0.0f} : XrVector3f{1.0f, 0.0f, 0.0f});
        z = Unit(z);
        beam.space = g_space;
        beam.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        beam.subImage.swapchain = g_beamImg.sc;
        beam.subImage.imageRect = {{0, 0}, {(int32_t)g_beamImg.w, (int32_t)g_beamImg.h}};
        beam.pose.orientation = FromAxes(CrossV(L.dir, z), L.dir, z);   // the quad's height along the ray
        beam.pose.position = mid;
        beam.size = {kBeamWidthM, L.len};
        beam.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&beam;
    }
    if (L.hit && g_dotImg.state > 0 && nl < max) {
        const XrQuaternionf& q = L.surface;
        dot.space = g_space;
        dot.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        dot.subImage.swapchain = g_dotImg.sc;
        dot.subImage.imageRect = {{0, 0}, {(int32_t)g_dotImg.w, (int32_t)g_dotImg.h}};
        dot.pose.orientation = q;
        dot.pose.position = Plus(L.at, Rot(q, {0.0f, 0.0f, 0.004f}));
        dot.size = {kDotM, kDotM};
        dot.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&dot;
    }
    return nl;
}
// The game's laser over the flat picture.
uint32_t HangLaser(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& beam, XrCompositionLayerQuad& dot) {
    const Laser& L = g_laser;
    return HangBeam(BeamView{L.drawn, L.hit, L.from, L.dir, L.at, L.len, g_flatShown.pose.orientation}, layers, nl, max, beam, dot);
}

#include "xr_quad_atlas.h"         // a CPU-painted picture as a quad layer (the settings panel; the skill wheel can use it)
#include "xr_headset_settings.h"   // the settings panel in the headset ([openxr] headset_settings): uses what is above
#include "xr_skill_wheel.h"         // the skill wheel in the headset ([openxr] skill_wheel): uses what is above
#include "xr_runes.h"               // BodyWalk's gesture runes in the headset ([openxr] runes_draw): uses what is above
#include "xr_hand_vitals.h"        // the hand vitals panel ([hud] hand_vitals): uses what is above
#include "xr_wrist_status.h"       // the wrist status panels ([hud] wrist_status): uses what is above

bool CopyInto(Chain& c, ID3D12Resource* bb) {
    if (!MakeCopyList()) return false;
    uint32_t idx = 0;
    if (NextImage(c.sc, &idx, "picture") <= 0) return false;
    ID3D12Resource* dst = c.img[idx].texture;
    if (!OpenList()) {   // (the image acquired is given back, or the chain runs out of images)
        XrSwapchainImageReleaseInfo none{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(c.sc, &none);
        return false;
    }
    D3D12_RESOURCE_BARRIER b[2] = {Tr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                   Tr(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST)};
    g_list->ResourceBarrier(2, b);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = dst; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.SubresourceIndex = 0;
    from.pResource = bb; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; from.SubresourceIndex = 0;
    D3D12_BOX box{0, 0, 0, c.w, c.h, 1};
    g_list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
    D3D12_RESOURCE_BARRIER e[2] = {Tr(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT),
                                   Tr(dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)};
    g_list->ResourceBarrier(2, e);
    SubmitList();
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return Ok(xrReleaseSwapchainImage(c.sc, &ri), "xrReleaseSwapchainImage");
}

void EndSessionNow(const char* why);

// The runtime failed (xr_failure.h; 2026-10-17: Virtual Desktop's xrBeginFrame answered XR_ERROR_RUNTIME_FAILURE for as long as the game lived): the session is
// given up - nothing more is submitted, the game goes on flat (FlatVR started again where it was ours to stop), and with the device alive a new session is made after
// 5 s (FollowLink: while FlatVR does not run), at most three times in ten minutes. A lost device is not a runtime to ask again: nothing of D3D12 or the runtime
// is touched after it. Under g_lock, with no frame open.
void GiveUp(const char* what, XrResult r) {
    const bool lost = gpufault::Check(g_dev, what);   // was it the device? (the report is written if so)
    LogF("vrcam: openxr - %s failed with %s: the runtime cannot go on - the session is given up and nothing more is submitted%s", what, Res(r),
         lost ? "; the D3D12 device is gone with it" : "");
    for (xrfail::Streak& s : g_failStreak) s.Reset();
    if (lost) {
        g_running.store(false);
        g_state.store(kFailed);
        g_retryAt.store(0);
        return;
    }
    EndSessionNow("the runtime failed");
    Teardown();
    g_state.store(kFailed);
    if (g_flatVrStopped) { link::RequestFlatVR(true); g_flatVrStopped = false; Log("vrcam: openxr - FlatVR started again"); }
    if (g_restarts.Allow(GetTickCount64())) {
        g_retryAt.store(GetTickCount64() + 5000);
        Log("vrcam: openxr - the game is flat until a new session is made in 5 s (not over a running FlatVR)");
    } else {
        g_retryAt.store(0);
        Log("vrcam: openxr - the runtime has failed three times in ten minutes: no new session this run, the game stays flat (switching [openxr] on off and on again tries once more)");
    }
}

void EndSessionNow(const char* why) {
    directinput::SessionEnd();   // what the session held in the game is let go
    g_pairYawAt.store(0);
    const bool wasRunning = g_running.exchange(false);
    if (wasRunning && g_session) {
        if ((g_waitRunner && g_waitRunner->Running()) || (g_beginRunner && g_beginRunner->Running()))
            Log("vrcam: openxr - xrEndSession left out: a runtime call of ours has not returned");
        else
            Ok(xrEndSession(g_session), "xrEndSession");
    }
    LogF("vrcam: openxr - session ended (%s)", why);
}

// xrBeginSession when the runtime says ready. [openxr] busy_guard (on): on a thread of its own, waited for g_cfg.waitFrameMs; if the runtime does not answer, the draw thread
// goes on (flat) and the answer is taken at a later Poll. Off: the call is made here, as before.
void BeginSession() {
    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    XrResult r;
    if (g_cfg.busyGuard) {
        if (!g_beginRunner) g_beginRunner = new bounded::Runner<XrResult>();
        if (!g_beginPending) Log("vrcam: openxr - xrBeginSession (on a thread of its own: the game does not wait for it for good)");
        const XrSession s = g_session;
        const double t0 = UsNow();
        if (g_beginRunner->Run([s, bi]() mutable { return xrBeginSession(s, &bi); }, g_beginPending ? 0u : g_cfg.waitFrameMs, &r) == bounded::Wait::Pending) {
            static double pendingSince = 0.0;
            if (!g_beginPending) {
                pendingSince = UsNow();
                LogF("vrcam: openxr - xrBeginSession has not answered in %u ms: the game goes on flat, the answer is taken when it comes (is another app - FlatVR, "
                     "BodyWalk Lite - holding the headset?)", g_cfg.waitFrameMs);
            } else if (UsNow() - pendingSince > 15e6) {
                GuardClear("the runtime does not start the session, the game lives");   // (a kill after this is not a crash of the start)
            }
            g_beginPending = true;
            return;
        }
        if (g_beginPending) LogF("vrcam: openxr - xrBeginSession answered (%s)", Res(r));
        else LogF("vrcam: openxr - xrBeginSession answered in %.0f ms (%s)", (UsNow() - t0) / 1000.0, Res(r));
        g_beginPending = false;
    } else {
        Log("vrcam: openxr - xrBeginSession");
        r = xrBeginSession(g_session, &bi);
    }
    if (Ok(r, "xrBeginSession")) {
        directinput::SessionBegin();
        g_running.store(true); g_recentre.store(true); g_recentreAfter = 0; g_framesRunning = 0; g_st = Stats{};
        g_monoAnchored = false; g_lastDrawn = kNone;   // the flat picture comes up anew, held to the gaze at first
        g_anchorGate.Reset(); g_refocus.Reset();
    }
}

// The runtime's events: begin when it says ready, end when it says stopping, give
// the headset back to FlatVR when the session or the instance is lost.
void Poll() {
    if (g_beginPending && !g_running.load() && g_session) BeginSession();   // asked for earlier and not answered: has it come?
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (g_inst && xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& sc = *(const XrEventDataSessionStateChanged*)&ev;
            const XrSessionState was = g_ss;
            g_ss = sc.state;
            LogF("vrcam: openxr - session %s", StateName(g_ss));
            {   // input focus taken away again and again (PS VR2 on SteamVR: 39 times in 20 minutes, the
                // doll's hands dropping each time, 2026-10-11) - said once a minute, with who may take it
                static ULONGLONG minuteAt = 0;
                static uint32_t lost = 0;
                if (was == XR_SESSION_STATE_FOCUSED && g_ss == XR_SESSION_STATE_VISIBLE) ++lost;
                const ULONGLONG now = GetTickCount64();
                if (!minuteAt) minuteAt = now;
                if (now - minuteAt >= 60000) {
                    if (lost >= 3)
                        LogF("vrcam: openxr - the input focus went to something else %u times in the last minute: SteamVR's dashboard, "
                             "the headset's own menu or another app's overlay takes it (the hands are held by the head meanwhile, "
                             "the buttons let go)", lost);
                    minuteAt = now;
                    lost = 0;
                }
            }
            if (g_ss == XR_SESSION_STATE_READY) {
                BeginSession();
            } else if (g_ss == XR_SESSION_STATE_STOPPING) {
                EndSessionNow("the runtime stopped it");
            } else if (g_ss == XR_SESSION_STATE_EXITING || g_ss == XR_SESSION_STATE_LOSS_PENDING) {
                EndSessionNow(g_ss == XR_SESSION_STATE_EXITING ? "exiting" : "the headset is lost");
                Teardown();
                g_state.store(kFailed);
                if (g_flatVrStopped) { link::RequestFlatVR(true); g_flatVrStopped = false; Log("vrcam: openxr - FlatVR started again"); }
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            EndSessionNow("the runtime is going away");
            Teardown();
            g_state.store(kFailed);
            if (g_flatVrStopped) { link::RequestFlatVR(true); g_flatVrStopped = false; }
            return;
        } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
            if (g_actionsOk) ReadProfiles();
        } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            // The runtime recentred: ours follows - in the new space, from the change's moment on
            // when it is our space that moves (the yaw was taken in the old one: the view 108 deg
            // off after the headset's recentre, 2026-10-10); any other space's change still
            // recentres, at once (the headset's button may move only LOCAL or only STAGE).
            const auto& rc = *(const XrEventDataReferenceSpaceChangePending*)&ev;
            const bool ours = rc.referenceSpaceType == g_spaceType;
            if (ours) g_recentreAfter = std::max(g_recentreAfter, std::min<XrTime>(rc.changeTime, g_fs.predictedDisplayTime + 1000000000LL));
            g_recentre.store(true);
            LogF("vrcam: openxr - the %s space changes (the runtime's recentre) %.1f ms after the last frame, turned %.1f deg%s",
                 rc.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL ? "local" : rc.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "stage" : "view",
                 (double)(rc.changeTime - g_fs.predictedDisplayTime) / 1e6, rc.poseValid ? YawOf(rc.poseInPreviousSpace.orientation) * 57.29578f : 0.0f,
                 ours ? " - ours: recentred from then on" : " - not ours: recentred now");
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

// The fov each eye is drawn with. [openxr] symmetric_fov (on): as wide to the left as to the
// right, the wider side's - a headset's eyes are mirrored (each wider on its outer side), and
// the game's interface, drawn once and the same in both eyes, then put the labels over the
// monsters left in the left eye and right in the right one, apart beyond the infinite
// (2026-10-10). The runtime crops what is not shown. Then narrowed to the window's aspect about
// its own middle with fov_crop.
XrFovf Cropped(XrFovf f) {
    float l = tanf(f.angleLeft), r = tanf(f.angleRight), u = tanf(f.angleUp), d = tanf(f.angleDown);
    if (g_cfg.symmetric) { const float h = std::max(-l, r); l = -h; r = h; }
    const float a = g_aspect.load();
    if (!g_cfg.fovCrop || !(a > 0.1f)) return {atanf(l), atanf(r), atanf(u), atanf(d)};
    if ((r - l) / (u - d) > a) { const float cx = 0.5f * (r + l), hw = 0.5f * a * (u - d); l = cx - hw; r = cx + hw; }
    else { const float cy = 0.5f * (u + d), hh = 0.5f * (r - l) / a; u = cy + hh; d = cy - hh; }
    return {atanf(l), atanf(r), atanf(u), atanf(d)};
}

// [openxr] views_3d, F1 (vr/views3d.h): the game's view from above is a WINDOW of the room - the flat picture's place, width and
// distance, put where the head looked when F1 came up - with the hero on its middle. Each eye sees the world through the window
// from where it really is: the frustum is the window's four edges seen from the eye, so the window itself shows the same in
// both eyes and the rest has depth, and a head moved sideways sees round things. An eye's picture is therefore the window's
// own texture (the plane maps to the same pixel from every eye): CloseFrame hangs it on the window as a quad per eye
// (eyeVisibility LEFT / RIGHT) - the compositor draws it with the head as it is when shown, as it does the flat picture.
// The eyes never come nearer the window's plane than kWindowMinZ.
constexpr float kWindowMinZ = 0.25f;
void PlaceWindow() {
    g_win = views3d::MakeWindow(g_cfg.quadWidthM, g_cfg.quadM, g_aspect.load());
    const XrVector3f &a = g_views[0].pose.position, &b = g_views[1].pose.position;
    const views3d::Vec mid{0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
    const bool put = g_winAnchor.Step(YawOf(g_views[0].pose.orientation), mid, UsNow());
    g_winAt = views3d::Place(g_win, g_winAnchor.Yaw(), g_winAnchor.Pos());
    g_winEyes = views3d::ThroughWindow(g_win, g_winAt, {a.x, a.y, a.z}, {b.x, b.y, b.z}, kWindowMinZ);
    g_winOk = true;
    static ULONGLONG toldAt = 0;
    if (put && (GetTickCount64() - toldAt > 1500)) {
        toldAt = GetTickCount64();
        LogF("vrcam: openxr - F1 in 3D: the window %.2f x %.2f m, %.2f m ahead of where you looked (gaze yaw %.0f deg), its middle at %.2f %.2f %.2f",
             g_win.width, g_win.height, g_win.dist, g_winAnchor.Yaw() * 57.29578f, g_winAt.centre.x, g_winAt.centre.y, g_winAt.centre.z);
    }
}

// xrWaitFrame (the runtime's pacing: it blocks), xrBeginFrame, the views at the moment
// this frame will be shown, and the actions synced (BodyWalk's OpenXR layer reads the
// controllers on that).
bool OpenFrame(Kind kind) {
    if (g_kind != kNone || !g_session) return false;
    Poll();
    if (!g_running.load() || !g_session) return false;
    static int crumbs = 0;   // the first frames' calls to the log as they are made: a crash there says where
    const bool crumb = crumbs < 3;
    if (crumb) LogF("vrcam: openxr - frame %d (%s): xrWaitFrame", ++crumbs, kind == kPair ? "pair" : "flat");
    const double t0 = UsNow();
    g_fs = {XR_TYPE_FRAME_STATE};
    XrResult wr;
    if (g_cfg.busyGuard) {
        // [openxr] busy_guard: the call on a thread of its own; not back in g_cfg.waitFrameMs - this frame is left out and the game goes on flat (a call that is still out is only
        // looked at, not waited for again: the game would stand 150 ms in every present)
        if (!g_waitRunner) g_waitRunner = new bounded::Runner<WaitOut>();
        const bool out = g_waitRunner->Running();
        const XrSession s = g_session;
        WaitOut o;
        if (g_waitRunner->Run([s]() { WaitOut w; XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; w.r = xrWaitFrame(s, &wi, &w.fs); return w; },
                              out ? 0u : g_cfg.waitFrameMs, &o) == bounded::Wait::Pending) {
            const double now = UsNow();
            if (!g_waitPendingAt) {
                g_waitPendingAt = out ? now : t0;
                g_waitToldAt = now;
                LogF("vrcam: openxr - xrWaitFrame has not returned in %u ms (session %s): the VR frame is left out and the game goes on flat; the call is waited for on a thread of its own "
                     "(is another app - FlatVR, BodyWalk Lite - holding the headset?)", g_cfg.waitFrameMs, StateName(g_ss));
            } else if (now - g_waitToldAt > 10e6) {
                g_waitToldAt = now;
                LogF("vrcam: openxr - xrWaitFrame has still not returned after %.0f s: the game runs flat", (now - g_waitPendingAt) / 1e6);
                if (now - g_waitPendingAt > 15e6) GuardClear("the runtime gives no frames, the game lives");   // (a kill after this is not a crash of the start)
            }
            return false;
        }
        if (g_waitPendingAt) {
            LogF("vrcam: openxr - xrWaitFrame returned after %.1f s: VR frames again", (UsNow() - g_waitPendingAt) / 1e6);
            g_waitPendingAt = 0.0;
        }
        wr = o.r;
        g_fs = o.fs;
    } else {
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
        wr = xrWaitFrame(g_session, &wi, &g_fs);
    }
    const bool waitGone = g_failStreak[0].Note(wr, GetTickCount64());
    if (!Ok(wr, "xrWaitFrame")) { if (waitGone) GiveUp("xrWaitFrame", wr); return false; }
    if (++g_framesRunning == 90) GuardClear("the session runs: 90 frames");
    const double t1 = UsNow();
    g_st.waitUs += t1 - t0;
    g_st.waitMax = std::max(g_st.waitMax, t1 - t0);
    if (kind == kPair) g_clock.wait = t1 - t0;
    if (crumb) LogF("vrcam: openxr -   should render %d, display period %.2f ms; xrBeginFrame", (int)g_fs.shouldRender, g_fs.predictedDisplayPeriod / 1e6);
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    const XrResult br = xrBeginFrame(g_session, &bi);
    const bool beginGone = g_failStreak[1].Note(br, GetTickCount64());
    if (br != XR_FRAME_DISCARDED && !Ok(br, "xrBeginFrame")) { if (beginGone) GiveUp("xrBeginFrame", br); return false; }
    g_beginUs = t1;
    g_kind = kind;
    g_copied[0] = g_copied[1] = false;
    g_viewsOk = false;
    g_pairMode = 0;   // (set again once the views are located)
    g_winOk = false;
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = g_fs.predictedDisplayTime;
    li.space = g_space;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t n = 0;
    g_views[0] = g_views[1] = {XR_TYPE_VIEW};
    // views that are views: finite, their turns of unit length (broken ones are no views - vrcam
    // keeps the last good pair's then, 2026-10-10)
    auto sane = [&vs](const XrView& v) {
        const XrQuaternionf& q = v.pose.orientation;
        const XrVector3f& p = v.pose.position;
        const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        return std::isfinite(n2) && fabsf(n2 - 1.0f) < 0.05f &&
               (!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) || (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)));
    };
    if (Ok(xrLocateViews(g_session, &li, &vs, 2, &n, g_views), "xrLocateViews") && n == 2 &&
        (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) && sane(g_views[0]) && sane(g_views[1])) {
        g_viewsOk = true;
        if (!(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
            for (XrView& v : g_views) v.pose.position = g_cPos;   // rotation only: the head held where it was taken
        {   // The eye distance (vr/eye_gap.h). Auto: the runtime's poses as they are - but two views in one place are no stereo: a runtime
            // that locates both eyes at the same point (no position for the eyes, only the head's turn - what a Meta Air Link player's log
            // showed, 2026-10-14: "eyes 0 mm apart", a double image that never merged) gets the eyes' own gap, about the middle, along each
            // view's right ([openxr] ipd_fallback_mm, default 64; 0 = off). Manual ([openxr] ipd_auto=0, ipd_mm): the player's number over
            // whatever the runtime says, for a headset whose IPD was not taken up. The eyes keep their midpoint and their turns. What the
            // runtime measured is kept for the settings (d2r_vr_headset.ini [headset] ipd_mm) and the log.
            const XrVector3f &a = g_views[0].pose.position, &b = g_views[1].pose.position;
            const float measured = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            NoteMeasuredIpd(measured);
            const eyegap::Choice pick = eyegap::Choose(measured, g_cfg.ipdAuto, g_cfg.ipdManualM, g_cfg.ipdFallbackM);
            if (pick.place) {
                float pos[2][3], right[2][3];
                for (int e = 0; e < 2; ++e) {
                    const XrVector3f& p = g_views[e].pose.position;
                    const XrVector3f r = Rot(g_views[e].pose.orientation, {1.0f, 0.0f, 0.0f});
                    pos[e][0] = p.x; pos[e][1] = p.y; pos[e][2] = p.z;
                    right[e][0] = r.x; right[e][1] = r.y; right[e][2] = r.z;
                }
                eyegap::Place(pos, right, pick.gapM);
                for (int e = 0; e < 2; ++e) g_views[e].pose.position = {pos[e][0], pos[e][1], pos[e][2]};
            }
            TellIpd(pick, measured);
        }
        {   // The frame both eyes are drawn and submitted in (vr/eye_frame.h). vrcam builds the game's camera from ONE turn - the first eye's - so an
            // eye submitted with its own turn on a headset whose displays are canted came out turned wrong by the angle between them (the eyes read
            // as crossed, the centre stretched). Canted ([openxr] canted_eyes): both eyes carry the head's turn (the mean of the two) and the frustum
            // of their own view as seen from it; parallel eyes: as they always were.
            eyeframe::Quat q[2];
            eyeframe::Fov want[2];
            for (int e = 0; e < 2; ++e) {
                const XrQuaternionf& o = g_views[e].pose.orientation;
                q[e] = eyeframe::Normalized({o.x, o.y, o.z, o.w});
                const XrFovf c = Cropped(g_views[e].fov);
                want[e] = {c.angleLeft, c.angleRight, c.angleUp, c.angleDown};
            }
            const eyeframe::Frame fr = eyeframe::Make(q, want, g_cfg.cantedEyes);
            TellEyes(g_views, q, fr);
            for (int e = 0; e < 2; ++e) g_fovUsed[e] = {fr.fov[e].l, fr.fov[e].r, fr.fov[e].u, fr.fov[e].d};
            if (fr.canted) {   // (the eyes on the head's right, as the game's camera puts them)
                const float one[3] = {1.0f, 0.0f, 0.0f};
                float axis[3], pos[2][3];
                eyeframe::Rotate(fr.ref, one, axis);
                for (int e = 0; e < 2; ++e) {
                    const XrVector3f& p = g_views[e].pose.position;
                    pos[e][0] = p.x; pos[e][1] = p.y; pos[e][2] = p.z;
                }
                if (eyegap::PlaceAlong(pos, axis, eyegap::Gap(pos[0], pos[1])))
                    for (int e = 0; e < 2; ++e) g_views[e].pose.position = {pos[e][0], pos[e][1], pos[e][2]};
                for (int e = 0; e < 2; ++e) g_views[e].pose.orientation = {fr.ref.x, fr.ref.y, fr.ref.z, fr.ref.w};
            }
        }
        {   // [openxr] menus_wait_tracking: placing waits for a tracked head with the focus; the focus back after a while places anew
            const bool tracked = (vs.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT);
            const bool focused = g_ss == XR_SESSION_STATE_FOCUSED;
            const ULONGLONG now = GetTickCount64();
            const bool ready = g_anchorGate.Step(tracked, focused, now);
            const bool again = g_refocus.Step(focused, now);
            const bool was = g_anchorReady;
            g_anchorReady = !g_cfg.menusWaitTracking || ready;
            if (g_cfg.menusWaitTracking && was != g_anchorReady)
                LogF("vrcam: openxr - the flat picture and menu panels: %s", g_anchorReady ? (tracked && focused ? "placed (the head is tracked)" :
                     "placed without tracking after 3 s (the runtime never says the head is tracked)") : "follow the gaze until the head is tracked");
            if (g_cfg.menusWaitTracking && again) {
                g_monoAnchored = false;
                g_panelPlace.anchored = false;
                Log("vrcam: openxr - the input focus is back after a while: the flat picture and a menu's panel come up in front again");
            }
        }
        if (kind == kMono) {
            // Put where the head looks when it comes up; held to the gaze for the session's first
            // half second (the runtime's first poses may not be the head's yet - it came up behind
            // the player, 2026-10-10); then world-locked, but brought round in front again once
            // it has been more than 60 deg off the gaze for a second.
            static int framesIn = 0;
            static double offSince = 0.0;
            framesIn = g_lastDrawn == kNone && !g_monoAnchored ? 0 : framesIn + 1;
            const float yawNow = YawOf(g_views[0].pose.orientation);
            float off = yawNow - g_monoYaw;
            off = atan2f(sinf(off), cosf(off));
            bool anchor = g_lastDrawn != kMono || !g_monoAnchored || framesIn < 45 || !g_anchorReady;
            if (!anchor && fabsf(off) > 1.05f) {
                if (offSince <= 0.0) offSince = t1;
                else if (t1 - offSince > 1e6) { anchor = true; Log("vrcam: openxr - the flat picture brought round in front of the gaze"); }
            } else {
                offSince = 0.0;
            }
            if (anchor) {
                g_monoAnchored = true;
                offSince = 0.0;
                g_monoYaw = yawNow;
                const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
                g_monoPos = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
            }
        }
        {   // A recentre the runtime never told: SteamVR (PS VR2) moved LOCAL from the floor to the
            // head - the head 1.7 m lower from one frame to the next, no REFERENCE_SPACE_CHANGE_PENDING,
            // and the camera, held to the old recentre's height, fell to the ground (2026-10-11). A
            // head moves no faster than ~5 m/s or turns ~1500 deg/s: a jump beyond that is the space.
            static XrVector3f lastMid{};
            static float lastYaw = 0.0f;
            static XrTime lastT = 0;
            const bool posOk = (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
            const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
            const XrVector3f m{0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
            const float yawNow = YawOf(g_views[0].pose.orientation);
            const double dt = (double)(g_fs.predictedDisplayTime - lastT) / 1e9;
            if (lastT && posOk && dt > 0.0 && dt < 0.1) {
                const float dx = m.x - lastMid.x, dy = m.y - lastMid.y, dz = m.z - lastMid.z;
                const float moved = sqrtf(dx * dx + dy * dy + dz * dz);
                float turned = yawNow - lastYaw;
                turned = fabsf(atan2f(sinf(turned), cosf(turned)));
                const double t = std::max(dt, 0.011);
                if (moved > std::max(0.25, 5.0 * t) || turned > std::max(0.79, 26.0 * t)) {
                    g_recentre.store(true);
                    LogF("vrcam: openxr - the head jumped %.2f m (%.2f %.2f %.2f) and %.1f deg in %.1f ms: the runtime moved our space "
                         "without a word - recentred", moved, dx, dy, dz, turned * 57.29578f, dt * 1000.0);
                }
            }
            lastT = posOk ? g_fs.predictedDisplayTime : 0;   // (no place: no jump to judge after it)
            lastMid = m;
            lastYaw = yawNow;
        }
        const bool tracked = (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
        if (g_fs.shouldRender && (tracked || g_framesRunning > 45) && g_fs.predictedDisplayTime >= g_recentreAfter &&
            g_recentre.exchange(false)) {   // (the request waits until then: the exchange last)
            g_monoAnchored = false;   // the flat picture comes up ahead again
            g_panelPlace.anchored = false;   // and a menu's panel
            g_winAnchor.Reset();   // and F1's window
            ++g_epoch;             // and F3's diorama (it stands in this space)
            g_cYaw = YawOf(g_views[0].pose.orientation);
            const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
            g_cPos = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
            g_bodyX = g_cPos.x; g_bodyZ = g_cPos.z;
            LogF("vrcam: openxr - recentred: yaw %.1f deg, the head at %.2f %.2f %.2f m", g_cYaw * 57.29578f, g_cPos.x, g_cPos.y, g_cPos.z);
        }
        {   // what this pair is (vr/views3d.h): the head's camera, F3's room, or F1's window
            g_pairMode = kind == kPair && g_cfg.views3d ? g_viewsMode : 0;
            g_winOk = false;
            if (g_pairMode == 1) {
                if (g_lastMode != 1) g_winAnchor.Reset();   // coming from another view: in front of the gaze again
                PlaceWindow();
            }
            if (kind == kPair && g_pairMode != g_lastMode)
                LogF("vrcam: openxr - pairs are drawn as %s now", g_pairMode == kViewsWindow ? "F1's window (views_3d)" : g_pairMode == kViewsRoom ? "F3's room (views_3d)" : "the head's camera (F4, F2)");
            if (kind == kPair) g_lastMode = g_pairMode;
        }
    } else {
        ++g_st.noViews;
    }
    if (crumb) LogF("vrcam: openxr -   views %s; xrSyncActions", g_viewsOk ? "located" : "NOT located");
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    XrActiveActionSet active{g_actSet, XR_NULL_PATH};
    if (g_actionsOk) { sync.countActiveActionSets = 1; sync.activeActionSets = &active; }
    const XrResult sr = xrSyncActions(g_session, &sync);
    if (sr != XR_SESSION_NOT_FOCUSED && sr != XR_ERROR_ACTIONSET_NOT_ATTACHED) Ok(sr, "xrSyncActions");
    Publish(sr == XR_SUCCESS);
    FlushHaptics();   // after Publish: a click the laser made this frame is felt this frame
    // F3: the floor in the space the pair is drawn in. The runtime's STAGE has it (the head's height over it is Publish's head);
    // the eyes' middle in our space less that is the floor there. A head that is not a standing height over it, or no STAGE: not known.
    g_floorOk = false;
    if (kind == kPair && g_viewsOk && g_roomSpace && g_actionsOk && g_inNow.head.valid == 1 && g_inNow.head.pos[1] > 0.4f && g_inNow.head.pos[1] < 2.6f) {
        const float midY = 0.5f * (g_views[0].pose.position.y + g_views[1].pose.position.y);
        g_floorY = midY - g_inNow.head.pos[1];
        g_floorOk = std::isfinite(g_floorY);
    }
    if (!g_fs.shouldRender) ++g_st.noRender;
    return true;
}

// The flat picture's distance now: the panels' while one is open, else the menus'. A change
// while it is shown is eased over 0.4 s (smoothstep, as FlatVR's screen goes back for a panel);
// fresh = the picture comes up now: there at once.
float FlatDistance(bool fresh) {
    static float from = 0.0f, to = 0.0f, now = 0.0f;
    static double since = 0.0;
    const float target = g_panelOpen.load() && g_cfg.panelM > 0.0f ? g_cfg.panelM : g_cfg.quadM;
    const double t = UsNow();
    if (fresh || !(now > 0.0f)) { from = to = now = target; since = t; return now; }
    if (target != to) { from = now; to = target; since = t; }
    const float k = (float)std::min(1.0, (t - since) / 400000.0);
    now = from + (to - from) * k * k * (3.0f - 2.0f * k);
    return now;
}

// [openxr] menu_3d (docs/plan_laser_menus.md step 2): a menu open in F4 as a panel of its own in the
// pair's frame - the interface hud.cpp took out of the picture (kNativePanel), drawn by the toolbar's
// shader into a swapchain of the layer's size and hung where the flat picture would have stood: as
// wide, at the panels' distance, turned to where the head looked when it came up (not again for a
// menu gone a moment, as the tabbed menu's page changes are), brought round in front after a second
// more than 60 deg off the gaze, as the flat picture is - the player's pick over d2r-pcvr's "placed
// once, never moved" (2026-10-13); a recentre places it again. The world goes on in stereo behind
// it, and the laser points at it (g_flatShown).
uint32_t HangPanel(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& panel, XrCompositionLayerQuad& pointer) {
    PanelPlace& P = g_panelPlace;
    hud::NativePiece piece[hud::kNativePieces];
    FlatVRGameHudLook look{};
    FlatVRGameHudPose barPose{};
    if (nl >= max || !g_viewsOk || !hud::NativeHud(piece, &look, &barPose)) return nl;
    const hud::NativePiece& src = piece[hud::kNativePanel];
    if (!src.visible || !src.w || !src.h || !MakeHudPipeline()) return nl;
    HudOut& o = g_hudOut[hud::kNativePanel];
    if (!EnsureHudOut(o, src.w, src.h, "menu panel")) return nl;
    const float cb[12] = {look.encoded ? 1.0f : 0.0f, 0.0f, (float)src.w / (float)src.h, (float)src.w,
                          0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f};   // PSMain, whole (no split)
    if (!DrawHud(o, src.tex, false, cb)) return nl;
    const double t = UsNow();
    const bool fresh = !(P.shownUs > 0.0) || t - P.shownUs > 500000.0;   // came up now: placed ahead
    const float yawNow = YawOf(g_views[0].pose.orientation);
    float off = yawNow - P.yaw;
    off = atan2f(sinf(off), cosf(off));
    bool anchor = fresh || !P.anchored || !g_anchorReady;   // ([openxr] menus_wait_tracking: held to the gaze until the head is tracked)
    if (!anchor && fabsf(off) > 1.05f) {
        if (P.offSince <= 0.0) P.offSince = t;
        else if (t - P.offSince > 1e6) { anchor = true; Log("vrcam: openxr - the menu's panel brought round in front of the gaze"); }
    } else {
        P.offSince = 0.0;
    }
    if (anchor) {
        P.anchored = true;
        P.offSince = 0.0;
        P.yaw = yawNow;
        const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
        P.pos = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
    }
    P.shownUs = t;
    const XrQuaternionf q = Yaw(P.yaw);
    const XrVector3f ahead = Rot(q, {0.0f, 0.0f, -FlatDistance(fresh)});
    panel = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    panel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied: the world through what the menu leaves
    panel.space = g_space;
    panel.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    panel.subImage.swapchain = o.sc;
    panel.subImage.imageRect = {{0, 0}, {(int32_t)src.w, (int32_t)src.h}};
    panel.pose.orientation = q;
    panel.pose.position = Plus(P.pos, ahead);
    panel.size = {g_cfg.quadWidthM, g_cfg.quadWidthM * (float)src.h / (float)src.w};
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel;
    g_flatShown = FlatShown{panel.pose, panel.size, true};   // where the next frame's laser points
    if (nl < max && PointerQuad(panel, &pointer)) layers[nl++] = (const XrCompositionLayerBaseHeader*)&pointer;
    if (!P.told) {
        P.told = true;
        LogF("vrcam: openxr - a menu as a panel in front, the world in stereo behind it: %ux%u, %.2f m wide at %.2f m ([openxr] menu_3d)",
             src.w, src.h, panel.size.width, FlatDistance(false));
    }
    return nl;
}

// Which of a frame's layers a slot is, for the log: 0 projection, 1 the flat picture or a menu's panel,
// 2 the pointer, 3 the beam, 4 the dot, 5.. the toolbar and the map, 8.. the gesture zones, then the gesture runes' pictures and trails.
constexpr int kLayerSlotZone0 = 8, kLayerSlotRune0 = kLayerSlotZone0 + (int)kZonesMax, kLayerSlots = kLayerSlotRune0 + (int)kRuneLayers + 1;
const char* LayerSlotName(int slot, bool pair, char* b, size_t cap) {
    static const char* const fixed[kLayerSlotZone0] = {"projection layer", "flat picture", "mouse pointer", "laser beam", "laser dot", "toolbar", "map", "interface"};
    if (slot < 0 || slot >= kLayerSlots - 1) return "layer";
    if (slot >= kLayerSlotRune0) { const int k = slot - kLayerSlotRune0; snprintf(b, cap, "gesture rune %s %d", k < (int)runes::kMax ? "picture" : "trail", k % (int)runes::kMax); return b; }
    if (slot >= kLayerSlotZone0) { snprintf(b, cap, "gesture zone %d", slot - kLayerSlotZone0); return b; }
    return slot == 1 && pair ? "menu panel" : fixed[slot];
}

// -- the T-pose measurement's prompt (vr/calibration.cpp, calibration_panel.cpp) -----------------------------------------
// A panel hung before the face, level, turning with the head's yaw only: the steps and what to do now, painted once per
// message into a static swapchain of its own (MakeStaticImage), with a green fill laid on its bar's track as wide as the hold
// has come. Only while a measurement is under way; its pictures go with it. The text is a quad of ours, not a BodyWalk menu
// and not the game's: the measurement needs nothing but the headset and the controllers.
struct CalibImage { XrSwapchain sc = XR_NULL_HANDLE; ID3D12Resource* upload = nullptr; ULONGLONG retryAt = 0; };
CalibImage g_calibImg[calib::kPanelImages + 1];   // the last one is the fill, a solid colour
XrCompositionLayerQuad g_calibLayer[2];

void DropCalibration() {
    WaitFence(g_fenceV, 200);   // the uploads' copies are done
    for (CalibImage& im : g_calibImg) {
        if (im.sc) { ForgetPending(im.sc); xrDestroySwapchain(im.sc); }
        if (im.upload) im.upload->Release();
        im = CalibImage{};
    }
}

bool MakeCalibImage(int i) {
    CalibImage& im = g_calibImg[i];
    if (im.sc) return true;
    const ULONGLONG now = GetTickCount64();
    if (now < im.retryAt) return false;
    const bool fill = i == calib::kPanelImages;
    const uint32_t w = fill ? 4 : calib::kPanelW, h = fill ? 4 : calib::kPanelH;
    std::vector<uint32_t> px((size_t)w * h, 0xFF000000u | (100u << 16) | (215u << 8) | 60u);   // the fill: opaque green
    if (!fill && !calib::PaintPanel(i, px.data())) {
        im.retryAt = now + 30000;
        LogF("vrcam: openxr - the measurement's panel %d could not be painted", i);
        return false;
    }
    const int got = MakeStaticImage(w, h, px.data(), im.upload, im.sc, "calibration");
    if (got <= 0 && im.sc) { ForgetPending(im.sc); xrDestroySwapchain(im.sc); im.sc = XR_NULL_HANDLE; }
    if (got <= 0) {
        im.retryAt = now + (got == 0 ? 500 : 10000);
        return false;
    }
    return true;
}

uint32_t HangCalibration(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    const calib::Panel p = calib::PanelNow();
    if (!p.show) {
        for (const CalibImage& im : g_calibImg)
            if (im.sc || im.upload) { DropCalibration(); break; }
        return nl;
    }
    const D2RVR_XrPose& head = g_inNow.head;
    if (nl + 2 > max || head.valid != 1 || !g_actionsOk || !MakeCalibImage(p.image)) return nl;
    const XrQuaternionf face = Yaw(YawOf(Q4(head.rot)));
    const XrVector3f at = Plus(P3(head.pos), Rot(face, {0.0f, -0.04f, -calib::kPanelDistanceM}));
    const float wM = calib::kPanelWidthM, hM = wM * (float)calib::kPanelH / (float)calib::kPanelW, pxM = wM / (float)calib::kPanelW;
    XrCompositionLayerQuad& q = g_calibLayer[0];
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // premultiplied
    q.space = g_roomSpace ? g_roomSpace : g_space;   // where the head was located
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = g_calibImg[p.image].sc;
    q.subImage.imageRect = {{0, 0}, {(int32_t)calib::kPanelW, (int32_t)calib::kPanelH}};
    q.pose = {face, at};
    q.size = {wM, hM};
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
    // the bar: its fill from the track's left end, a few pixels in from the track's edge
    const float full = (float)calib::kTrackW - 6.0f, width = std::clamp(p.progress, 0.0f, 1.0f) * full;
    if (width >= 2.0f && MakeCalibImage(calib::kPanelImages)) {
        XrCompositionLayerQuad& b = g_calibLayer[1];
        b = q;
        b.subImage.swapchain = g_calibImg[calib::kPanelImages].sc;
        b.subImage.imageRect = {{0, 0}, {4, 4}};
        const float cx = ((float)calib::kTrackX + 3.0f + 0.5f * width) - 0.5f * (float)calib::kPanelW;
        const float cy = 0.5f * (float)calib::kPanelH - ((float)calib::kTrackY + 0.5f * (float)calib::kTrackH);
        b.pose.position = Plus(at, Rot(face, {cx * pxM, cy * pxM, 0.0f}));
        b.size = {width * pxM, ((float)calib::kTrackH - 6.0f) * pxM};
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&b;
    }
    return nl;
}

void CloseFrame() {
    if (g_kind == kNone) return;
    const double closeUs = UsNow();   // the layers built from here, xrEndFrame from endUs (g_clock)
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad winQuad[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};   // F1 in 3D (views_3d): the window, a quad per eye
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD}, pointer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad ptr3d{XR_TYPE_COMPOSITION_LAYER_QUAD};   // F1 / F3 in 3D (pointer_3d): the game's pointer on the ground
    XrCompositionLayerQuad beam{XR_TYPE_COMPOSITION_LAYER_QUAD}, dot{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad hsPanel{XR_TYPE_COMPOSITION_LAYER_QUAD}, hsBeam{XR_TYPE_COMPOSITION_LAYER_QUAD}, hsDot{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const uint32_t hsRoom = (g_hsOpen.load() ? 3 : 0) + (calib::PanelNow().show ? 2 : 0) + (skillwheel::Enabled() ? 3 : 0) + (handvitals::PanelOn() ? 1 : 0) + (wriststatus::Enabled() ? 2 : 0);   // the settings panel's three layers, the T-pose prompt's two, the skill wheel's three, the hand vitals' one and the wrist status' two, hung last
    XrCompositionLayerQuad wheelPanel{XR_TYPE_COMPOSITION_LAYER_QUAD}, wheelBeam{XR_TYPE_COMPOSITION_LAYER_QUAD}, wheelDot{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad vitalsPanel{XR_TYPE_COMPOSITION_LAYER_QUAD};   // [hud] hand_vitals: one layer over the hand
    XrCompositionLayerColorScaleBiasKHR vitalsScale{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    XrCompositionLayerQuad wsPanel[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};   // [hud] wrist_status: one layer over each wrist
    XrCompositionLayerColorScaleBiasKHR wsScale[2] = {{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR}, {XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR}};
    const XrCompositionLayerBaseHeader* layers[10 + kZonesMax + kRuneLayers] = {};
    const uint32_t most = std::min<uint32_t>(10 + kZonesMax + kRuneLayers, g_maxLayers);
    uint32_t nl = 0;
    if (g_kind == kPair) g_flatShown.ok = false;   // the laser's surface is gone with the flat picture (a menu's panel: HangPanel)
    if (g_kind == kPair && g_viewsOk)
        for (int e = 0; e < 2; ++e)
            if (g_copied[e]) g_lastEye[e] = LastEye{g_views[e].pose, g_fovUsed[e], g_eyeChain[e].sc, true};   // drawn with these
    // eyes by turns: this frame's eye and the other's last, still in its swapchain
    const bool turns = g_cfg.byTurns && (g_copied[0] || g_copied[1]) && g_lastEye[0].ok && g_lastEye[1].ok &&
                       g_lastEye[0].sc == g_eyeChain[0].sc && g_lastEye[1].sc == g_eyeChain[1].sc;
    if (g_fs.shouldRender && g_kind == kPair && ((g_copied[0] && g_copied[1]) || turns) && g_viewsOk) {
        if (g_pairMode == kViewsWindow && g_winOk) {
            // F1 in 3D (views_3d): each eye's picture is the game seen through the window from that eye, i.e. the window's own
            // texture (vr/views3d.h) - hung on the window, left eye's on the left eye only and right's on the right
            const XrQuaternionf turn = Yaw(g_winAt.yaw);
            for (int e = 0; e < 2; ++e) {
                XrCompositionLayerQuad& q = winQuad[e];
                q.space = g_space;
                q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
                q.subImage.swapchain = g_eyeChain[e].sc;
                q.subImage.imageRect = {{0, 0}, {(int32_t)g_eyeChain[e].w, (int32_t)g_eyeChain[e].h}};
                q.pose.orientation = turn;
                q.pose.position = {g_winAt.centre.x, g_winAt.centre.y, g_winAt.centre.z};
                q.size = {g_win.width, g_win.height};
                layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
            }
        } else {
        for (int e = 0; e < 2; ++e) {
            pv[e].pose = g_lastEye[e].pose;   // the poses and fovs the eyes were drawn with
            pv[e].fov = g_lastEye[e].fov;
            pv[e].subImage.swapchain = g_eyeChain[e].sc;
            pv[e].subImage.imageRect = {{0, 0}, {(int32_t)g_eyeChain[e].w, (int32_t)g_eyeChain[e].h}};
        }
        proj.space = g_space;
        proj.viewCount = 2;
        proj.views = pv;
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&proj;
        }
        // F1 / F3 in 3D: the game's own pointer on the ground where its mouse points
        if (g_pairMode != kViewsHead && nl < most && g_viewsOk) {
            const XrVector3f eyes{0.5f * (g_views[0].pose.position.x + g_views[1].pose.position.x), 0.5f * (g_views[0].pose.position.y + g_views[1].pose.position.y),
                                  0.5f * (g_views[0].pose.position.z + g_views[1].pose.position.z)};
            if (Pointer3dQuad(eyes, g_views[0].pose.orientation, &ptr3d)) layers[nl++] = (const XrCompositionLayerBaseHeader*)&ptr3d;
        }
        // a menu in front, the world in stereo behind it (menu_3d); what is nearer lies over it, in this order
        if (g_cfg.menu3d) nl = HangPanel(layers, nl, most, quad, pointer);
        const bool laser = g_cfg.menu3d && g_flatShown.ok;   // (the beam and the dot only over a menu's panel)
        const uint32_t zonesMax = most - std::min<uint32_t>(most - nl, (laser ? 4 : 2) + hsRoom);   // room kept for those below
        nl = HangZones(layers, nl, zonesMax);   // the belt's balls
        nl = HangRunes(layers, nl, zonesMax);   // BodyWalk's gesture runes and the hand's trail (F4, [openxr] runes_draw)
        nl = HangHud(layers, nl, most);   // the toolbar and the map, where FlatVR hung them
        nl = HangPlate(layers, nl, most);   // the targeted monster's health bar over its head ([hud] monster_bar_room)
        if (laser) nl = HangLaser(layers, nl, most, beam, dot);
        nl = HangVitals(layers, nl, most, vitalsPanel);   // life and mana over the hand turned to the face (F4)
        nl = HangWristStatus(layers, nl, most, wsPanel);   // companions and effects over a wrist turned to the eyes (F4)
        nl = HangCalibration(layers, nl, most);   // the T-pose measurement's prompt
        nl = HangSettings(layers, nl, most, hsPanel, hsBeam, hsDot);   // the settings panel, nearest of all
        nl = HangWheel(layers, nl, most, wheelPanel, wheelBeam, wheelDot);   // the skill wheel, nearest of all
        ++g_st.pairs;
    } else if (g_fs.shouldRender && g_kind == kMono && g_copied[0]) {
        // the picture where the head looked when it came up, level, world-locked
        const XrQuaternionf q = Yaw(g_monoAnchored ? g_monoYaw : g_cYaw);
        const XrVector3f at = g_monoAnchored ? g_monoPos : g_cPos;
        const XrVector3f ahead = Rot(q, {0.0f, 0.0f, -FlatDistance(g_lastDrawn != kMono)});
        quad.space = g_space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = g_quadChain.sc;
        quad.subImage.imageRect = {{0, 0}, {(int32_t)g_quadChain.w, (int32_t)g_quadChain.h}};
        quad.pose.orientation = q;
        quad.pose.position = {at.x + ahead.x, at.y + ahead.y, at.z + ahead.z};
        const float a = g_quadChain.h ? (float)g_quadChain.w / (float)g_quadChain.h : 16.0f / 9.0f;
        quad.size = {g_cfg.quadWidthM, g_cfg.quadWidthM / a};
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&quad;
        g_flatShown = FlatShown{quad.pose, quad.size, true};   // where the next frame's laser points
        if (PointerQuad(quad, &pointer)) layers[nl++] = (const XrCompositionLayerBaseHeader*)&pointer;
        nl = HangLaser(layers, nl, most, beam, dot);
        const uint32_t zonesMax = most - std::min<uint32_t>(most - nl, hsRoom);
        nl = HangZones(layers, nl, zonesMax);   // the belt's balls stay where they are over a panel too
        nl = HangRunes(layers, nl, zonesMax);   // and the gesture runes
        nl = HangCalibration(layers, nl, most);   // the T-pose measurement's prompt
        nl = HangSettings(layers, nl, most, hsPanel, hsBeam, hsDot);   // the settings panel, nearest of all
        nl = HangZones(layers, nl, most);   // the belt's balls stay where they are over a panel too
        nl = HangWheel(layers, nl, most, wheelPanel, wheelBeam, wheelDot);   // the skill wheel, nearest of all
        ++g_st.monos;
    }
    // A layer the runtime would refuse (a turn off unit length, a place or size that is not a number)
    // takes the whole frame with it - XR_ERROR_POSE_INVALID, the world dropped for a belt ball: the ones
    // that cannot be made right are left out, a turn is made unit length, and the layer is named in the
    // log (xr_layers.h; after bearstonem/d2r-pcvr's motionQuad).
    auto slotOf = [&](const XrCompositionLayerBaseHeader* l) -> int {
        const void* p = l;
        if (p == &proj) return 0;
        if (p == &quad || p == &winQuad[0] || p == &winQuad[1]) return 1;
        if (p == &pointer || p == &ptr3d) return 2;
        if (p == &beam) return 3;
        if (p == &dot) return 4;
        for (int i = 0; i < hud::kNativePieces; ++i) if (p == &g_hudOut[i].layer) return 5 + i;
        for (uint32_t k = 0; k < kZonesMax; ++k) if (p == &g_zoneLayer[k]) return kLayerSlotZone0 + (int)k;
        for (uint32_t k = 0; k < kRuneLayers; ++k) if (p == &g_runeLayer[k]) return kLayerSlotRune0 + (int)k;
        return kLayerSlots - 1;
    };
    static xrlayers::Telling told;
    nl = xrlayers::Sanitize(layers, nl, [&](uint32_t, XrCompositionLayerBaseHeader* l, const xrlayers::Check& c) {
        const bool drop = c.verdict == xrlayers::Verdict::Drop;
        if (drop && l == (const void*)&quad) g_flatShown.ok = false;   // nothing to point the laser at
        if (!drop && std::fabs(c.norm2 - 1.0f) <= xrlayers::kToldOff) return;   // float noise, set right quietly
        const int slot = slotOf(l);
        uint32_t times = 0;
        if (!told.Due((uint32_t)slot * 2 + (drop ? 1u : 0u), GetTickCount64(), 10000, &times)) return;
        char nm[32], num[256];
        const char* name = LayerSlotName(slot, g_kind == kPair, nm, sizeof nm);
        xrlayers::Describe(num, sizeof num, l);
        if (drop)
            LogF("vrcam: openxr - the %s was left out of the frame, %s (%s) - the runtime would have refused the whole frame (%u time(s) since last told)",
                 name, xrlayers::WhyName(c.why), num, times);
        else
            LogF("vrcam: openxr - the %s%s had a turn off unit length (|q|^2 %.6f): made unit length (%u time(s) since last told)", name,
                 c.view >= 0 ? (c.view ? " (right view)" : " (left view)") : "", c.norm2, times);
    });
    // [openxr] brightness: every layer's colour scaled by the runtime (the HUD's too, so it sits in
    // the picture as before). The layers' own structs live on between frames: their link is set
    // every frame, to it or to nothing.
    static XrCompositionLayerColorScaleBiasKHR scale{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    const float bright = g_cfg.brightness;
    const bool scaled = g_colorScaleExt && std::fabs(bright - 1.0f) > 0.001f;
    scale.colorScale = {bright, bright, bright, 1.0f};
    scale.colorBias = {0.0f, 0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < nl; ++i) ((XrCompositionLayerBaseHeader*)layers[i])->next = scaled ? &scale : nullptr;
    VitalsScale(vitalsPanel, vitalsScale, bright);   // (the hand vitals' layer has its own: the fade and the soft pulse)
    WristStatusScale(wsPanel, wsScale, bright);       // (so have the wrist status' two: the fade)
    static int ends = 0;
    if (ends < 3) LogF("vrcam: openxr - frame %d: xrEndFrame with %u layer(s)", ++ends, nl);
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = g_fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nl;
    ei.layers = nl ? layers : nullptr;
    const double endUs = UsNow();
    const XrResult ended = xrEndFrame(g_session, &ei);
    g_clock.layers = endUs - closeUs;
    g_clock.submit = UsNow() - endUs;
    Ok(ended, "xrEndFrame");
    const bool endGone = g_failStreak[2].Note(ended, GetTickCount64());
    if (ended == XR_ERROR_POSE_INVALID) {   // the runtime names no layer: each one's numbers, so the log does (every 5 s at most)
        static xrlayers::Telling diag;
        uint32_t times = 0;
        if (diag.Due(0, GetTickCount64(), 5000, &times)) {
            LogF("vrcam: openxr - xrEndFrame refused the frame (XR_ERROR_POSE_INVALID, %u time(s) since last told) with %u layer(s), none of them off unit "
                 "length or not a number by our check - each one's numbers:", times, nl);
            for (uint32_t i = 0; i < nl; ++i) {
                char nm[32], num[256];
                xrlayers::Describe(num, sizeof num, layers[i]);
                LogF("vrcam: openxr -   layer %u, %s: %s", i, LayerSlotName(slotOf(layers[i]), g_kind == kPair, nm, sizeof nm), num);
            }
        }
    }
    g_lastDrawn = g_kind;
    const double now = UsNow();
    g_st.frameUs += now - g_beginUs;
    ++g_st.frames;
    g_kind = kNone;
    if (!g_st.since) g_st.since = GetTickCount64();
    if (GetTickCount64() - g_st.since >= 10000) {
        const double s = (GetTickCount64() - g_st.since) / 1000.0, k = 1.0 / std::max<uint32_t>(1, g_st.frames);
        LogF("vrcam: openxr - %.1f frames/s (%u pairs, %u flat), the headset's every %.2f ms; xrWaitFrame blocked %.2f ms a frame (longest %.1f), "
             "begin to end %.2f ms; not to render %u, no views %u",
             g_st.frames / s, g_st.pairs, g_st.monos, g_fs.predictedDisplayPeriod / 1e6, g_st.waitUs * k * 0.001, g_st.waitMax * 0.001,
             g_st.frameUs * k * 0.001, g_st.noRender, g_st.noViews);
        g_st = Stats{};
        g_st.since = GetTickCount64();
    }
    if (endGone) GiveUp("xrEndFrame", ended);   // (the frame is closed: nothing of it is open to tear down under)
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; directinput::SetLogger(log); roomwalkwire::SetLogger(log); haptics::SetLogger(log); vrres::SetLogger(log); }
void SetWindow(HWND wnd) { g_wnd = wnd; }

// The start guard (2026-10-10): players' games died starting native OpenXR - their runtime's own
// D3D11 work went through ReShade's DXGI hooks (an int3 in dxgi.dll under ReShade64.dll), at
// every start, so the game "crashed on startup" until on=0 was found by hand. A file beside the
// ini while a session starts, gone once 90 frames ran or the session ended cleanly: found at the
// next load with on=1, the last start died in it - native is switched off ([openxr] on=0, the
// log says why) and the game starts on FlatVR. Switched on again in D2R VR Settings.
wchar_t g_guardPath[MAX_PATH] = L"";
bool g_guardSet = false;
void GuardSet() {
    if (!g_guardPath[0] || g_guardSet) return;
    HANDLE h = CreateFileW(g_guardPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const char text[] = "D2R VR: a native OpenXR session is starting. If the game dies before it runs, this file stays and the next start turns native OpenXR off.\r\n";
    DWORD w = 0;
    WriteFile(h, text, sizeof text - 1, &w, nullptr);
    CloseHandle(h);
    g_guardSet = true;
}
void GuardClear(const char* why) {
    if (!g_guardSet) return;
    g_guardSet = false;
    DeleteFileW(g_guardPath);
    LogF("vrcam: openxr - start guard cleared (%s)", why);
}

void LoadSettings(const wchar_t* ini) {
    wcscpy_s(g_iniPath, ini);   // (the settings panel's thread reads and writes the same file)
    auto f = [&](const wchar_t* key, float def) {
        wchar_t b[64], d[64];
        swprintf_s(d, L"%g", def);
        GetPrivateProfileStringW(L"openxr", key, d, b, 64, ini);
        wchar_t* end = nullptr;
        const float v = wcstof(b, &end);
        return end != b && std::isfinite(v) ? v : def;
    };
    // on: live - switched on, the session starts at the next first-person pair (after a
    // failure too: it is tried again); switched off, it ends and FlatVR comes back
    static int was = -1;
    // Flat mode ([mode] platform=0, "Flat: on the monitor, mouse and keyboard"): no session, no render size, no square window - the
    // game stays on the monitor whatever [openxr] on says (the shipped ini has on=1, for VR). A switch of the platform is live.
    const bool vrMode = GetPrivateProfileIntW(L"mode", L"platform", 1, ini) != 0;
    bool on = vrMode && f(L"on", 0.0f) != 0.0f;
    {
        static int saidFlat = -1;
        const int flatWithOn = !vrMode && f(L"on", 0.0f) != 0.0f ? 1 : 0;
        if (flatWithOn != saidFlat) {
            saidFlat = flatWithOn;
            if (flatWithOn) Log("vrcam: openxr - flat mode ([mode] platform=0): [openxr] on=1 is not used - the game stays on the monitor, no headset session, no render size, no square window");
        }
    }
    if (!g_guardPath[0]) {   // the first read: did the last start die starting a session?
        wcscpy_s(g_guardPath, ini);
        if (wchar_t* slash = wcsrchr(g_guardPath, L'\\')) slash[1] = 0;
        wcscat_s(g_guardPath, L"d2r_vr_openxr_starting.txt");
        if (GetFileAttributesW(g_guardPath) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(g_guardPath);
            if (on) {
                WritePrivateProfileStringW(L"openxr", L"on", L"0", ini);
                SYSTEMTIME t;
                GetLocalTime(&t);
                wchar_t when[48];
                swprintf_s(when, L"%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
                WritePrivateProfileStringW(L"openxr", L"off_after_crash", when, ini);
                on = false;
                was = 0;
                Log("vrcam: openxr - THE LAST START DIED while native OpenXR was starting (the start guard was still there): "
                    "native OpenXR is switched OFF ([openxr] on=0) - the game goes on FlatVR. Switch it on again in D2R VR Authentic Settings > "
                    "Performance > 3D in the headset; if it dies again, send Collect logs");
            }
        }
    }
    if ((int)on != was) {
        if (on) Log("vrcam: openxr - [openxr] on=1: the game shows itself in the headset, in stereo from the first pair in first person");
        else if (was == 1) Log("vrcam: openxr - [openxr] on=0: back to FlatVR");
        if (!on && was == 1) g_stopWanted.store(true);
        if (on && g_state.load() == kFailed && !g_final) g_state.store(kIdle);
        was = on;
    }
    g_cfg.on = on;
    wchar_t sp[32];
    GetPrivateProfileStringW(L"openxr", L"space", L"local", sp, 32, ini);
    g_cfg.stage = _wcsicmp(sp, L"stage") == 0;   // (taken when a session starts)
    g_cfg.stopFlatVR = f(L"stop_flatvr", 1.0f) != 0.0f;
    g_cfg.busyGuard = f(L"busy_guard", 1.0f) != 0.0f;   // the runtime's blocking calls with a limit; no session over a FlatVR that cannot be stopped
    g_cfg.waitFrameMs = (unsigned)std::clamp(f(L"wait_frame_ms", 150.0f), 20.0f, 2000.0f);
    g_cfg.fovCrop = f(L"fov_crop", 1.0f) != 0.0f;   // on: sharper (square pixels), black above and below (2026-10-10)
    g_cfg.symmetric = f(L"symmetric_fov", 1.0f) != 0.0f;
    g_cfg.ipdFallbackM = std::clamp(f(L"ipd_fallback_mm", 64.0f), 0.0f, 80.0f) * 0.001f;   // eyes located in one place: this gap (0 = off)
    g_cfg.ipdAuto = f(L"ipd_auto", 1.0f) != 0.0f;                                          // the eye distance: the runtime's (1) or the player's own (0), live
    g_cfg.ipdManualM = eyegap::ClampGap(f(L"ipd_mm", eyegap::kDefaultM * 1000.0f) * 0.001f);
    g_cfg.cantedEyes = f(L"canted_eyes", 1.0f) != 0.0f;   // canted displays: both eyes in the head's frame (1), or the old way (0)
    {   // the menus' distance ([openxr] flat_distance_m; unset, FlatVR's panel distance as before) and the
        // panels': FlatVR's "Screen distance while a panel is open" ([screen] menu_distance_m, D2R VR
        // Settings > UI: F4 body, its default as link.cpp's), 0 = the menus' - it never reached native
        // while flat_distance_m was set, so its slider moved nothing there (2026-10-12)
        wchar_t b[32];
        GetPrivateProfileStringW(L"screen", L"menu_distance_m", L"", b, 32, ini);
        const float menuM = b[0] ? wcstof(b, nullptr) : 1.6f;
        g_cfg.quadM = std::clamp(f(L"flat_distance_m", b[0] && std::isfinite(menuM) && menuM > 0.3f ? menuM : 2.0f), 0.5f, 10.0f);
        g_cfg.panelM = std::isfinite(menuM) && menuM > 0.0f ? std::clamp(menuM, 0.5f, 20.0f) : 0.0f;
    }
    g_cfg.quadWidthM = std::clamp(f(L"flat_width_m", 2.4f), 0.3f, 20.0f);
    {
        static float said[3] = {};
        if (on && (g_cfg.quadM != said[0] || g_cfg.panelM != said[1] || g_cfg.quadWidthM != said[2])) {
            said[0] = g_cfg.quadM; said[1] = g_cfg.panelM; said[2] = g_cfg.quadWidthM;
            char panel[96];
            if (g_cfg.panelM > 0.0f) snprintf(panel, sizeof panel, "%.2f m ([screen] menu_distance_m)", g_cfg.panelM);
            else snprintf(panel, sizeof panel, "the same ([screen] menu_distance_m=0)");
            LogF("vrcam: openxr - the flat picture %.2f m wide: menus, loading, F1 and F3 at %.2f m ([openxr] flat_distance_m), a panel open in a game at %s",
                 g_cfg.quadWidthM, g_cfg.quadM, panel);
        }
    }
    g_cfg.leanM = std::clamp(f(L"lean_m", 0.10f), 0.0f, 5.0f);
    g_cfg.handHoldMs = std::clamp(f(L"hand_hold_ms", 1000.0f), 0.0f, 10000.0f);
    {   // eyes by turns (D2R VR Settings > Performance > 3D in the headset > Native OpenXR - eyes by turns)
        static int said = -1;
        const bool turns = GetPrivateProfileIntW(L"stereo", L"afr", 0, ini) != 0 && GetPrivateProfileIntW(L"stereo", L"pair_per_tick", 0, ini) == 0;
        if (on && (int)turns != said) {
            said = turns;
            Log(turns ? "vrcam: openxr - eyes by turns: one eye a game frame, the other as it was last drawn"
                      : "vrcam: openxr - both eyes from one game frame");
        }
        g_cfg.byTurns = turns;
    }
    g_cfg.bareHands = f(L"bare_hands", 0.0f) != 0.0f;
    g_cfg.frameProfile = f(L"frame_profile", 1.0f) != 0.0f;   // (taken at the next session start)
    {
        g_cfg.indexGripForce = f(L"index_grip_force", 0.0f) != 0.0f;   // live
        static int said = 0;   // (off at the start: not a word)
        if (on && (int)g_cfg.indexGripForce != said) {
            said = g_cfg.indexGripForce;
            Log(g_cfg.indexGripForce ? "vrcam: openxr - Valve Index: the grip is how hard you squeeze ([openxr] index_grip_force)"
                                     : "vrcam: openxr - Valve Index: the grip is the fingers round the handle, as before");
        }
    }
    g_cfg.menusWaitTracking = f(L"menus_wait_tracking", 1.0f) != 0.0f;   // live
    {   // the laser on the menus (D2R VR Settings > Stereo > Native OpenXR), live
        g_cfg.laser = f(L"laser", 0.0f) != 0.0f;
        g_cfg.laserHand = f(L"laser_hand", 1.0f) != 0.0f ? 1 : 0;
        static int said = 0;   // (off at the start: not a word)
        const int now = g_cfg.laser ? 1 + g_cfg.laserHand : 0;
        if (on && now != said) {
            said = now;
            LogF("vrcam: openxr - laser on the menus and panels: %s ([openxr] laser, laser_hand)",
                 !g_cfg.laser ? "off" : g_cfg.laserHand ? "the right hand" : "the left hand");
        }
        // the game's item tooltip at once under the laser: its hooks go in with the laser's (panelinput::Install), unticked they pass through
        g_cfg.laserTooltips = f(L"laser_tooltips", 0.0f) != 0.0f;
        panelinput::WantTooltips(g_cfg.laserTooltips);
        static int saidTip = 0;
        if (on && g_cfg.laser && (int)g_cfg.laserTooltips != saidTip) {
            saidTip = g_cfg.laserTooltips;
            LogF("vrcam: openxr - laser: the game's item tooltip %s ([openxr] laser_tooltips)",
                 g_cfg.laserTooltips ? "at once, with the equipped-item comparison and no Shift" : "after the game's own delay");
        }
    }
    {   // F1 and F3 in real 3D (D2R VR Settings > Stereo > Native OpenXR), live: unticked the flat picture, as before
        g_cfg.views3d = f(L"views_3d", 1.0f) != 0.0f;
        static int said = -1;
        if (on && (int)g_cfg.views3d != said) {
            said = g_cfg.views3d;
            Log(g_cfg.views3d ? "vrcam: openxr - F1 (a window onto the game) and F3 (the game on your floor) in 3D with the headset's own eyes ([openxr] views_3d=1)"
                              : "vrcam: openxr - F1 and F3 as the flat picture ([openxr] views_3d=0)");
        }
    }
    {   // the game's pointer on F1's and F3's ground (D2R VR Settings > Stereo > Native OpenXR), live; only drawn - the mouse is not touched
        g_cfg.pointer3d = f(L"pointer_3d", 1.0f) != 0.0f;
        static int said = -1;
        if (on && (int)g_cfg.pointer3d != said) {
            said = g_cfg.pointer3d;
            Log(g_cfg.pointer3d ? "vrcam: openxr - F1 and F3 in 3D: the game's mouse pointer is drawn on the ground where its mouse points ([openxr] pointer_3d=1)"
                                : "vrcam: openxr - F1 and F3 in 3D: no pointer drawn ([openxr] pointer_3d=0)");
        }
    }
    {   // a menu in F4 as a panel, the world in stereo (D2R VR Settings > UI: F4 body > Menus), live
        g_cfg.menu3d = f(L"menu_3d", 1.0f) != 0.0f;
        static int said = -1;
        if (on && (int)g_cfg.menu3d != said) {
            said = g_cfg.menu3d;
            Log(g_cfg.menu3d ? "vrcam: openxr - menus in F4 as a panel in front, the world in stereo behind it ([openxr] menu_3d=1)"
                             : "vrcam: openxr - menus in F4 as the flat picture ([openxr] menu_3d=0)");
        }
    }
    directinput::LoadSettings(ini);   // [openxr] direct_input and the [direct] layout: the plugin's own controls
    roomwalkwire::LoadSettings(ini);  // [openxr] roomscale_walk: your steps in the room walk the hero (vr/room_walk.h)
    {   // a settings panel in the headset (both grips squeezed, then a thumbstick click), live
        g_cfg.headsetSettings = f(L"headset_settings", 0.0f) != 0.0f;
        static int said = -1;
        if (on && (int)g_cfg.headsetSettings != said) {
            said = g_cfg.headsetSettings;
            Log(g_cfg.headsetSettings ? "vrcam: openxr - settings panel in the headset: both grips squeezed, then a thumbstick click, opens it ([openxr] headset_settings=1)"
                                      : "vrcam: openxr - settings panel in the headset: off ([openxr] headset_settings=0)");
        }
    }
    {   // BodyWalk's gesture runes and the hand's trail in the headset (D2R VR Settings > Controls > Gestures), live; F4 only
        g_cfg.runesDraw = f(L"runes_draw", 0.0f) != 0.0f;
        static int said = 0;   // (off at the start: not a word)
        if (on && (int)g_cfg.runesDraw != said) {
            said = g_cfg.runesDraw;
            Log(g_cfg.runesDraw ? "vrcam: openxr - gesture runes: BodyWalk's runes and the hand's trail are drawn in the headset, F4 only ([openxr] runes_draw=1)"
                                : "vrcam: openxr - gesture runes: off ([openxr] runes_draw=0)");
        }
    }
    {   // controller vibration (D2R VR Settings > Stereo > Native OpenXR), live; only with a session of ours, so only with [openxr] on
        g_cfg.haptics = f(L"haptics", 0.0f) != 0.0f;
        const float sc = f(L"haptics_scale", 1.0f);
        g_cfg.hapticsScale = std::isfinite(sc) ? std::clamp(sc, 0.1f, 2.0f) : 1.0f;
        haptics::Configure(on && g_cfg.haptics, g_cfg.hapticsScale);
        g_cfg.hapticsHands = f(L"haptics_hand_actions", 1.0f) != 0.0f;   // (taken at the next session start)
        g_cfg.hapticsStop = f(L"haptics_stop", 1.0f) != 0.0f;            // live
        haptics::SetMotorChoice((int)f(L"haptics_motor", 0.0f));         // live: 0 by the headset's name, 1 plain, 2 weak (the low-life beat lifted for the PlayStation VR2 Sense)
    }
    {   // [render] vr_resolution_px (a square's side) and vr_resolution (the older mode), live: render_size.h reads the two together
        const int mode = (int)GetPrivateProfileIntW(L"render", L"vr_resolution", 0, ini);
        const int px = (int)GetPrivateProfileIntW(L"render", L"vr_resolution_px", 0, ini);
        g_cfg.vrPick = rendersize::FromIni(mode, px);
        if (!g_headsetPath[0]) {   // what the runtime says of the headset goes beside the ini for D2R VR Settings (WriteHeadsetInfo)
            wcscpy_s(g_headsetPath, ini);
            if (wchar_t* slash = wcsrchr(g_headsetPath, L'\\')) slash[1] = 0;
            wcscat_s(g_headsetPath, L"d2r_vr_headset.ini");
        }
    }
    // [openxr] skill_wheel: the controller's eight skill slots as a wheel in the headset (vr/skill_wheel.h), live; its panel's place is tunable
    {
        const bool wheelOn = on && f(L"skill_wheel", 0.0f) != 0.0f;
        skillwheel::Configure(wheelOn, g_cfg.laserHand, wheelOn ? f(L"skill_wheel_distance_m", 0.8f) : 0.8f, wheelOn ? f(L"skill_wheel_width_m", 0.8f) : 0.8f,
                              wheelOn && f(L"skill_panel_style", 0.0f) >= 0.5f ? 1 : 0, wheelOn ? f(L"skill_tiles_width_m", 0.9f) : 0.9f, ini,
                              g_cfg.headsetSettings);   // ([openxr] headset_settings with the tiles' look: the panel has the Settings tab)
    }
    {
        const float b = f(L"brightness", 1.0f);
        g_cfg.brightness = std::isfinite(b) ? std::clamp(b, 0.2f, 2.0f) : 1.0f;
        static float said = 1.0f;
        if (on && g_cfg.brightness != said) {
            said = g_cfg.brightness;
            LogF("vrcam: openxr - brightness %.2f%s", said, g_colorScaleExt || g_state.load() == kIdle ? "" : " (the runtime cannot: no colour scale)");
        }
    }
}

bool On() { return g_cfg.on && g_state.load() != kFailed; }
bool Views3dOn() { return On() && g_cfg.views3d; }
void SetPointerAt(PointerAtFn fn) { g_pointerAt = fn; }
void SetViewsMode(int mode) { g_viewsMode = mode >= kViewsHead && mode <= kViewsRoom ? mode : kViewsHead; }
bool Running() { return g_running.load(); }
float LeanM() { return g_cfg.leanM; }
void HoldBody() { g_bodyHeldAt.store(GetTickCount64()); }
bool ByTurns() { return g_cfg.on && g_cfg.byTurns; }
bool StopPending() { return g_stopWanted.load() && g_state.load() != kIdle; }
void Recenter() { g_recentre.store(true); }
void SetLaserWanted(bool menus) { g_laserWanted.store(menus); }
bool LaserOn() { return g_cfg.on && g_cfg.laser; }
bool Menu3dOn() { return g_cfg.on && g_cfg.menu3d; }
bool Menu3dWanted() { return g_cfg.menu3d; }
bool HeadsetSettingsOpen() { return g_hsOpen.load(); }
bool LaserWanted(int* hand) {
    *hand = g_cfg.laserHand;
    return g_cfg.laser;
}
void MenuPanelSize(float* widthM, float* distanceM) {
    *widthM = g_cfg.quadWidthM;
    *distanceM = g_cfg.panelM > 0.0f ? g_cfg.panelM : g_cfg.quadM;
}
void SetPanelOpen(bool open) {
    if (g_panelOpen.exchange(open) == open || !g_cfg.on || !(g_cfg.panelM > 0.0f) || g_cfg.panelM == g_cfg.quadM) return;
    LogF("vrcam: openxr - %s: the flat picture to %.2f m", open ? "a panel open" : "the panel closed", open ? g_cfg.panelM : g_cfg.quadM);
}

void Stop(bool final);
// The session started on the game's device and queue (a worker thread), once.
void Start(ID3D12Device* dev, ID3D12CommandQueue* queue) {
    if (g_state.load() != kIdle || !dev || !queue) return;
    gpufault::Watch(dev);   // (vrcam does it from every present; here too, for a session that starts first)
    g_dev = dev;
    g_queue = queue;
    g_state.store(kStarting);
    GuardSet();   // gone once the session runs (90 frames) or ends cleanly
    Log("vrcam: openxr - starting the session");
    if (HANDLE h = CreateThread(nullptr, 0, StartThread, nullptr, 0, nullptr)) CloseHandle(h);
    else g_state.store(kFailed);
}

bool BeginPair(ID3D12Device* dev, ID3D12CommandQueue* queue) {
    if (g_stopWanted.exchange(false)) Stop(false);
    if (!On()) return false;
    if (gpufault::Check(g_dev, "the start of a pair") && DeviceLost()) { g_pairOpen.store(false); return false; }   // (one call a pair into the runtime: is the device there?)
    if (g_state.load() == kIdle) { if (dev && queue) Start(dev, queue); return false; }
    if (g_state.load() != kCreated) { g_pairYawAt.store(0); return false; }
    // Two passes before the replay is on (or without it): it has no device and queue yet - the
    // session's are the game's (OnPresent started it with them). Stereo from the first pair, not
    // 240 pairs later as the flat picture, eye after eye (2026-10-10).
    if (!dev) dev = g_dev;
    if (!queue) queue = g_queue;
    g_pairSpent = false;
    if (dev != g_dev || queue != g_queue) {
        static bool told = false;
        if (!told) { told = true; LogF("vrcam: openxr - the pair's queue %p is not the session's %p: no stereo pairs, the flat picture only", (void*)queue, (void*)g_queue); }
        return false;
    }
    g_clock = PairTiming{};
    const double b0 = UsNow();
    AcquireSRWLockExclusive(&g_lock);
    const bool open = OpenFrame(kPair);
    const bool ok = open && g_viewsOk && g_fs.shouldRender;
    if (open && !ok) CloseFrame();   // nothing to draw for: an empty frame
    g_pairSpent = open && !ok;
    if (ok) {   // the head these eyes are turned by (PairEyes' Yaw(-g_cYaw) * q), for vrcam's body
        const float r = YawOf(g_views[0].pose.orientation) - g_cYaw;
        g_pairYaw.store(atan2f(sinf(r), cosf(r)));
        g_pairYawAt.store(GetTickCount64());
        g_pairOpen.store(true);
        g_pairMissed.store(false);
    } else {
        // the last good pair's yaw kept while vrcam keeps that pair's eyes (NativeEyes): the
        // body did not go to BodyWalk's zero for a frame and back (2026-10-10)
        g_pairMissed.store(true);
        g_pairOpen.store(false);
    }
    ReleaseSRWLockExclusive(&g_lock);
    g_clock.begin = UsNow() - b0;
    return ok;
}

bool PairEyes(Eyes* out) {
    if (g_kind != kPair || !g_viewsOk) return false;
    *out = Eyes{};
    out->mode = g_pairMode;
    if (g_pairMode == kViewsWindow) {   // F1: the window's frame (vr/views3d.h) - the camera is the game's, the head only looks through
        if (!g_winOk) return false;
        for (int e = 0; e < 2; ++e) {
            out->pos[e][0] = g_winEyes.mid.x + (e == 0 ? -g_winEyes.half : g_winEyes.half);
            out->pos[e][1] = g_winEyes.mid.y;
            out->pos[e][2] = g_winEyes.mid.z;
            out->quat[e][3] = 1.0f;
            out->tanL[e] = g_winEyes.tanL[e];
            out->tanR[e] = g_winEyes.tanR[e];
            out->tanU[e] = g_winEyes.tanU[e];
            out->tanD[e] = g_winEyes.tanD[e];
        }
        out->winW = g_win.width; out->winH = g_win.height; out->winD = g_win.dist;
        return true;
    }
    if (g_pairMode == kViewsRoom) {   // F3: the eyes as the runtime has them, in the space the layer is submitted in
        for (int e = 0; e < 2; ++e) {
            const XrPosef& p = g_views[e].pose;
            out->pos[e][0] = p.position.x; out->pos[e][1] = p.position.y; out->pos[e][2] = p.position.z;
            out->quat[e][0] = p.orientation.x; out->quat[e][1] = p.orientation.y; out->quat[e][2] = p.orientation.z; out->quat[e][3] = p.orientation.w;
            out->tanL[e] = tanf(g_fovUsed[e].angleLeft);
            out->tanR[e] = tanf(g_fovUsed[e].angleRight);
            out->tanU[e] = tanf(g_fovUsed[e].angleUp);
            out->tanD[e] = tanf(g_fovUsed[e].angleDown);
        }
        out->floorY = g_floorY; out->floorOk = g_floorOk; out->epoch = g_epoch;
        return true;
    }
    const XrQuaternionf back = Yaw(-g_cYaw);
    out->epoch = g_epoch;   // (the room walk starts fresh at a recentre)
    if (GetTickCount64() - g_bodyHeldAt.load() >= 2500) {   // the body follows the head once it leans farther than lean_m (unless the room walk holds it)
        const float mx = 0.5f * (g_views[0].pose.position.x + g_views[1].pose.position.x);
        const float mz = 0.5f * (g_views[0].pose.position.z + g_views[1].pose.position.z);
        const float dx = mx - g_bodyX, dz = mz - g_bodyZ, d = sqrtf(dx * dx + dz * dz);
        if (d > g_cfg.leanM && d > 1e-6f) { const float k = 1.0f - g_cfg.leanM / d; g_bodyX += dx * k; g_bodyZ += dz * k; }
    }
    for (int e = 0; e < 2; ++e) {
        const XrPosef& p = g_views[e].pose;
        // from the body (over the floor) and the recentre's height: a lean moves the eyes, a walk does not
        const XrVector3f rel = Rot(back, {p.position.x - g_bodyX, p.position.y - g_cPos.y, p.position.z - g_bodyZ});
        const XrQuaternionf q = Mul(back, p.orientation);
        out->pos[e][0] = rel.x; out->pos[e][1] = rel.y; out->pos[e][2] = rel.z;
        out->quat[e][0] = q.x; out->quat[e][1] = q.y; out->quat[e][2] = q.z; out->quat[e][3] = q.w;
        out->tanL[e] = tanf(g_fovUsed[e].angleLeft);
        out->tanR[e] = tanf(g_fovUsed[e].angleRight);
        out->tanU[e] = tanf(g_fovUsed[e].angleUp);
        out->tanD[e] = tanf(g_fovUsed[e].angleDown);
    }
    return true;
}

void PairOver() { g_pairSpent = false; }   // the draw thread, after a pair's last present

PairTiming LastPairTiming() { return g_clock; }

bool PairHeadYaw(float* rad) {
    const ULONGLONG at = g_pairYawAt.load();
    const ULONGLONG keep = g_pairMissed.load() ? 2000 : 250;
    if (!at || !g_running.load() || (!g_pairOpen.load() && GetTickCount64() - at > keep)) return false;
    *rad = g_pairYaw.load();
    return true;
}

bool PairInput(D2RVR_XrInput* out) {
    if (g_kind != kPair || !g_actionsOk || !g_inNow.counter) return false;
    *out = g_inNow;
    return true;
}

void CopyEye(int eye, ID3D12Resource* bb) {
    if (g_kind != kPair || !bb || eye < 0 || eye > 1 || g_copied[eye] || DeviceLost()) return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_session && EnsureChain(g_eyeChain[eye], bb->GetDesc(), eye ? "right eye's" : "left eye's"))
        g_copied[eye] = CopyInto(g_eyeChain[eye], bb);
    ReleaseSRWLockExclusive(&g_lock);
}

void EndPair() {
    if (g_kind != kPair) return;
    g_pairOpen.store(false);
    if (g_pairYawAt.load()) g_pairYawAt.store(GetTickCount64());   // the timer's 250 ms from the pair's end
    AcquireSRWLockExclusive(&g_lock);
    if (g_session && !DeviceLost()) CloseFrame();   // (a removed device: the frame is not ended - the runtime fails too, and the game is on its way out)
    g_kind = kNone;
    ReleaseSRWLockExclusive(&g_lock);
}

void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* queue, ID3D12Resource* bb, int pairEye) {
    {   // [render] vr_resolution: the render size the game is asked for (a post, never a wait), on every present
        if (bb) { const D3D12_RESOURCE_DESC d = bb->GetDesc(); g_outputSize.store(vrres::Pack({(uint32_t)d.Width, d.Height})); }
        vrres::Update(g_wnd, g_running.load() && g_state.load() == kCreated, vrres::Unpack(g_vrRecommended.load()), vrres::Unpack(g_vrMaximum.load()),
                      vrres::Unpack(g_outputSize.load()), g_cfg.on ? g_cfg.vrPick : rendersize::Pick{});
    }
    if (DeviceLost()) return;
    if (g_kind != kPair && g_stopWanted.exchange(false)) Stop(false);
    // inside a pair: its own frame - and a pass of two presents its own eye (the replay copied it already)
    if (g_kind == kPair) { if (pairEye >= 0 && bb) CopyEye(pairEye, bb); return; }
    if (On() && g_state.load() == kIdle) { Start(dev, queue); return; }   // from the first present: menus too
    if (g_state.load() == kCreated && !g_running.load()) {   // waiting for the runtime's go
        AcquireSRWLockExclusive(&g_lock);
        if (g_session) Poll();
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (!g_running.load()) return;
    if (pairEye == 0) return;   // a pair's left eye not as a picture of its own: the flat picture is one a pair, never eye after eye
    if (pairEye >= 0 && g_pairSpent) return;   // this pair had its frame already
    AcquireSRWLockExclusive(&g_lock);
    if (OpenFrame(kMono)) {
        g_pairYawAt.store(0);   // a flat frame: the body goes the game's way again
        if (g_fs.shouldRender && bb && EnsureChain(g_quadChain, bb->GetDesc(), "flat picture's")) g_copied[0] = CopyInto(g_quadChain, bb);
        CloseFrame();
    }
    ReleaseSRWLockExclusive(&g_lock);
}

// BodyWalk come after the session was made (BodyWalk started later): the wish for its
// Mapping made again - the one at the start found no BodyWalk to take it (2026-10-10).
void FollowLink() {
    // a start that found no headset, again once its time has come - not over a running FlatVR
    if (const ULONGLONG at = g_retryAt.load(); at && g_state.load() == kFailed && GetTickCount64() >= at) {
        bool flatvr = false;
        FlatVRCounter(&flatvr);
        if (!g_cfg.on || g_final) g_retryAt.store(0);
        else if (flatvr) g_retryAt.store(GetTickCount64() + kRetryMs);
        else { g_retryAt.store(0); WriteSessionNote(L""); g_state.store(kIdle); }   // the next present starts it
    }
    static bool had = false;
    if (g_state.load() != kCreated) { had = false; return; }
    uint32_t caps = 0;
    const bool alive = link::HostAlive(&caps) && (caps & BW_LINK_CAP_XR_INPUT);
    // (direct_input governing - the key set AND the view F4: BodyWalk gets no controllers. In F1, F2, F3 it gets them as if the key were 0.
    // [openxr] direct_bodywalk_walk: its Mapping stays on the game's headset in F4 too - it walks the hero by the poses it is given there)
    const bool have = alive && (!directinput::Governing() || directinput::BodyWalkWalks());
    if (have && !had) {
        link::RequestPresenter(true);
        Log("vrcam: openxr - BodyWalk's Game Link is there: its Mapping on the game's headset (Game Link)");
    } else if (had && alive && !have) {
        link::RequestPresenter(false);   // F4 with direct_input again: BodyWalk's Mapping off the game's headset, as at the session's start
        Log("vrcam: openxr - F4 with direct_input: BodyWalk's Mapping off the game's headset again");
    }
    had = have;
}

// The session and the instance gone, FlatVR started again; final: the game is closing.
void Stop(bool final) {
    if (g_state.load() == kStarting) Sleep(200);   // (a start under way finishes under the lock)
    AcquireSRWLockExclusive(&g_lock);
    if (final) g_final = true;
    const bool had = g_session || g_inst;
    if (had) {
        if (g_kind != kNone) CloseFrame();
        if (g_running.load()) EndSessionNow(final ? "the game is closing" : "switched off");
        if (g_queue && g_fence) { g_queue->Signal(g_fence, ++g_fenceV); WaitFence(g_fenceV, 500); }   // our copies done before the images go
        Teardown();
        Log("vrcam: openxr - session and instance gone");
    }
    GuardClear(final ? "the game closes" : "switched off");
    g_state.store(final ? kFailed : kIdle);
    if (had) link::RequestPresenter(false);   // the Mapping back on FlatVR (if it was the game's)
    if (g_flatVrStopped) { link::RequestFlatVR(true); g_flatVrStopped = false; Log("vrcam: openxr - FlatVR started again"); }
    ReleaseSRWLockExclusive(&g_lock);
}

void Shutdown() { Stop(true); }

void DeviceGone(ID3D12Device* dev) {
    if (dev && dev == g_dev && g_state.load() != kIdle) Shutdown();
}

// FlatVR there to take the pictures: its head sample moved within the last 1.5 s (looked at twice a second at most - the
// mapping is opened each time). For pace::Tick ([stereo] pace_flatvr_only): with nothing to hand the pictures to, nothing to
// pace them to. One caller, the present's thread.
bool FlatVrRunning() {
    static ULONGLONG at = 0, movedAt = 0;
    static uint32_t last = 0;
    static bool state = false;
    const ULONGLONG now = GetTickCount64();
    if (at && now - at < 500) return state;
    at = now;
    bool running = false;
    const uint32_t c = FlatVRCounter(&running);
    if (running && c != last) movedAt = now;
    last = c;
    state = running && movedAt && now - movedAt < 1500;
    return state;
}
}  // namespace xr
