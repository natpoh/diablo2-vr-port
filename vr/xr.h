#pragma once
// Native OpenXR (docs/plan_native_openxr.md): the game shows its own stereo pair in
// the headset as an OpenXR app, instead of handing it to FlatVR. A prototype behind
// [openxr] on=0: first person (F4) with "Stereo - one pass, two pictures" (the
// replay); outside it each present goes to the headset as a flat quad.
//
// Everything here runs on the game's draw thread except Start's worker and
// Shutdown. vrcam wraps every call that may touch the game's queue in its own
// "ours" flag, so its queue hooks let the runtime's work through.

#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct D2RVR_XrInput;   // shared/d2r_vr_shared.h
struct HWND__;

namespace xr {

void SetLogger(void (*log)(const char*));
// The game's window: where the mouse pointer is laid on the flat picture.
void SetWindow(HWND__* wnd);
// [openxr] from d2r_vr.ini, on every change: `on` switched on starts a session at the next
// first-person pair, switched off ends it and gives the headset back to FlatVR.
void LoadSettings(const wchar_t* iniPath);
bool On();
bool FlatVrRunning();   // FlatVR's head sample moving (pace::Tick, [stereo] pace_flatvr_only)

// The eyes for the frame open now, recentred: metres and a rotation in the head's
// frame at the last recentre (x right, y up, z back), and each eye's field of view
// as tangents (left and down negative), narrowed to the window's aspect with
// [openxr] fov_crop=1.
struct Eyes {
    float pos[2][3];
    float quat[2][4];   // x, y, z, w
    float tanL[2], tanR[2], tanU[2], tanD[2];
    // [openxr] views_3d (vr/views3d.h): what these eyes are measured in. kViewsHead (the default, F4 and F2): above - the head's
    // frame at the recentre, held to the body. kViewsRoom (F3): the room as the runtime has it - pos/quat raw, in the space the
    // layer is submitted in, the floor's height there (floorOk: the runtime's STAGE gave it; else not known) and an epoch that
    // moves whenever that space was recentred or jumped. kViewsWindow (F1): the window's frame (x along the window, y up, z out
    // of it towards the viewer, metres, from its middle), quat the identity, tan* the frusta through the window's edges, win*
    // the window's size and distance.
    // kViewsHead: pos is from the standing eyes' height, and standMeasured says which - true: the floor plus the T-pose's measured
    // eye height ([calibration] use=1, F4, a STAGE floor seen); false: the head's height at the recentre - always so with
    // standSeated ([calibration] seated=1: played seated, the recentre sets the hero's eye height).
    int mode;
    float winW, winH, winD;
    float floorY;
    bool floorOk;
    uint32_t epoch;
    bool standMeasured;
    bool standSeated;
};
enum { kViewsHead = 0, kViewsWindow = 1, kViewsRoom = 2 };

// [openxr] on and views_3d: F1 and F3 are drawn with the runtime's eyes too, not shown as a flat picture. Live.
bool Views3dOn();
// What the pair about to open is drawn as (kViewsHead, kViewsWindow, kViewsRoom); vrcam, before every BeginPair, the same thread.
void SetViewsMode(int mode);

// [openxr] pointer_3d (docs/plan_native_f1_f3.md): the game's mouse pointer is drawn in F1 and F3 in 3D too. How a click reaches the game is
// not touched; only the cursor the game shows is hung where the game's mouse points: the pixel of its window (u, v from the top left, 0..1)
// is a ray through vrcam's left-eye camera and the ray meets the game's ground at the point a click would walk to (vr/pointer3d.h). vrcam
// answers with that point in the frame the pair's eyes are in (xr::Eyes for `mode`) and its distance from the left eye in metres. False: no
// camera of that mode yet, or no ground under that pixel (the sky). The draw thread, from CloseFrame.
struct PointerAt {
    float at[3];
    float dist;
};
using PointerAtFn = bool (*)(int mode, float u, float v, PointerAt* out);
void SetPointerAt(PointerAtFn fn);

// Before a pair's left pass (F4, F2): replayed, or two passes before the replay is on (dev
// and queue null then: the session's own). True: a frame is open and PairEyes has its views.
bool BeginPair(ID3D12Device* dev, ID3D12CommandQueue* queue);
bool PairEyes(Eyes* out);   // the mode SetViewsMode asked for (a window or the room's, when views_3d); its `mode` says which
// The head's yaw (radians, + turned left) in the views of the last stereo pair opened, from the
// last recentre - what PairEyes turns the eyes by, for the body. False with no session, after a
// flat frame, or when the last pair is older than 250 ms - 2 s when the pairs since did not open
// (the last good pair's yaw is kept, as vrcam keeps its eyes). Any thread.
bool PairHeadYaw(float* rad);
// One eye's finished picture (its effects run, the back buffer in the PRESENT state)
// copied into that eye's swapchain image, on the game's queue.
void CopyEye(int eye, ID3D12Resource* backBuffer);
// The head and the controllers as the frame open now locates them (what goes to the D2R
// Bridge): room space, not recentered. False without a session or controllers.
bool PairInput(D2RVR_XrInput* out);
// After the pair: both eyes as one projection layer (none if an eye is missing).
void EndPair();
// Where the last pair's time went inside the calls above, microseconds - the draw thread, right after
// EndPair, for vrcam's long-pair line (vr/frame_hitch.h): BeginPair whole and its xrWaitFrame, EndPair's
// layers built before xrEndFrame and xrEndFrame itself, and in the whole pair the waits for our copies on
// the GPU (the fence) and for the runtime's swapchain images. Clocks only - no locks, no calls.
struct PairTiming { double begin, wait, layers, submit, fence, image; };
PairTiming LastPairTiming();
// Every present (ReShade's reshade_present), with the game's device and queue (native):
// the session starts from the first one ([openxr] on - the menus too), and outside a pair
// the back buffer goes to the headset as a flat quad, paced by the runtime. pairEye: the eye this
// present's pass drew (0 or 1) inside a pair of two passes, else -1 - copied into its swapchain
// inside a native pair, never shown flat as an eye of its own outside one.
void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* queue, ID3D12Resource* backBuffer, int pairEye = -1);
// After every pair, native or not (its presents done): the next presents may open flat frames again.
void PairOver();
// Twice a second: BodyWalk (Game Link) come after the session was made gets its wish again,
// and a start that found no headset is tried again (every 10 s, while FlatVR does not run).
void FollowLink();

// A session is running: the runtime paces the game (xrWaitFrame), not vrcam.
bool Running();
// [openxr] lean_m: how far the head may move off the body (0: not at all - a neck model then).
float LeanM();
// [openxr] roomscale_walk (vr/room_walk_wire.cpp), every view build it owns: the body stays where it is - no lean_m drag - for 2.5 s
// from the call; the room walk keeps its own point in the eyes' frame then (the hero's place). A recentre still puts the body under the head.
void HoldBody();
// Native OpenXR with the eyes by turns ([stereo] afr without pair_per_tick): one pass a game frame,
// one eye, the other handed in again as it was last drawn.
bool ByTurns();
// [openxr] on went to 0 while a session is up: the next present ends it.
bool StopPending();
// The next views taken as straight ahead (F11, BodyWalk's recentre).
void Recenter();
// A panel (inventory, trade, stash...) is open in a game: the flat picture goes to the panels'
// distance ([screen] menu_distance_m) while it is, and back after - eased, as FlatVR's screen.
void SetPanelOpen(bool open);
// [openxr] laser (docs/plan_laser_menus.md): the flat picture shows menus - a panel open in a game, or
// no game (the title and character screens) - so a controller pointing at it is the mouse. Never on
// F1 and F3's flat picture, the world itself. vrcam, every present.
void SetLaserWanted(bool menus);
// [openxr] on and laser: the game's controller layout is to be kept under the laser's mouse
// (vr/panel_input.cpp's hooks wanted).
bool LaserOn();
// [openxr] on and menu_3d (docs/plan_laser_menus.md step 2): a menu open in first person keeps the
// stereo pairs, its interface hung in front as a panel of its own (hud::SetPanelOut) - vrcam decides
// when (F4 only).
bool Menu3dOn();
// [openxr] menu_3d whatever [openxr] on says: on FlatVR a menu goes as a panel too (vrcam, hud.cpp).
bool Menu3dWanted();
// [openxr] headset_settings (vr/xr_headset_settings.h): the settings panel in the headset is up. While it is, the
// hands are the panel's: whatever reads the controllers for the game (BodyWalk through Game Link, the plugin's own
// input) must read them from PairInput / the published input - already zero there - or hold its output neutral.
// The panel itself never sends a click or a key to the game.
bool HeadsetSettingsOpen();
// [openxr] laser whatever [openxr] on says, and its hand (0 left, 1 right): on FlatVR its laser is the
// mouse on a menu's panel (hud.cpp hands it the hand).
bool LaserWanted(int* hand);
// Where a menu's panel hangs, native or on FlatVR: the flat picture's width ([openxr] flat_width_m) at
// the panels' distance ([screen] menu_distance_m; 0 there: flat_distance_m).
void MenuPanelSize(float* widthM, float* distanceM);
// Session and instance gone, FlatVR started again; from the game's exit paths.
void Shutdown();
// A D3D12 device is going (ReShade's destroy_device): the session's own ends with it.
void DeviceGone(ID3D12Device* dev);

}  // namespace xr
