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
#include "afr_eye_shared.h"
#include "hud_native.h"

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
    bool stopFlatVR = true;   // FlatVR stopped before the session, started again after
    float quadM = 2.0f;       // outside a pair: the flat picture this far ahead, metres
    float quadWidthM = 2.4f;  // and this wide
    float leanM = 0.10f;      // the head may move this far from the body sideways (lean_m); farther, the body follows
    bool bareHands = false;   // bare_hands: a hand without a controller (the runtime's hand tracking) works as one; 0: it presses and places nothing
};
Cfg g_cfg;
bool g_final = false;                 // the game is closing: no session again
std::atomic<bool> g_stopWanted{false};  // [openxr] on went to 0 while a session runs: the draw thread ends it

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
// BodyWalk's gesture zones ticked "VR" (HangZones): one atlas swapchain, a ball in a
// cell of it for each zone, and a quad layer each.
constexpr uint32_t kZoneCell = 256, kZoneCols = 4, kZonesMax = 16;
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
// A swapchain image acquired whose wait ran out: waited for again next time (NextImage).
struct PendingImage { XrSwapchain sc; uint32_t idx; };
PendingImage g_pendingImg[16];
bool g_copied[2] = {};
XrFovf g_fovUsed[2] = {};

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
// [openxr] flat_distance_m ([screen] menu_distance_m unless set), and stays there.
float g_monoYaw = 0.0f;
XrVector3f g_monoPos{};
bool g_monoAnchored = false;
int g_lastDrawn = 0;   // the kind of the last frame that went out

// The body the camera is held to (lean_m): where the head is over the floor, as far as a
// lean goes - the head may lean round it, and walking off in the room drags it along
// instead of taking the camera out of the hero (2026-10-10). Room space, set at the recentre.
float g_bodyX = 0.0f, g_bodyZ = 0.0f;

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
    static std::vector<std::pair<const char*, int>> told;
    for (const auto& t : told) if (t.first == what && t.second == (int)r) return false;
    if (told.size() < 64) told.push_back({what, (int)r});
    LogF("vrcam: openxr - %s failed: %s", what, Res(r));
    return false;
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
void SignalBridge(const wchar_t* name) {
    if (HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, name)) { SetEvent(e); CloseHandle(e); }
}
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
// Stopped through the D2R Bridge (BodyWalk's request_flatvr_running), then until its
// head sample stands still for half a second (5 s at most): FlatVR holds the headset.
void StopFlatVR() {
    bool running = false;
    uint32_t last = FlatVRCounter(&running);
    if (!running) { Log("vrcam: openxr - FlatVR is not running"); return; }
    HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, D2RVR_FLATVR_STOP_NAME);
    if (!e) { Log("vrcam: openxr - FlatVR runs but the D2R Bridge is not there to stop it - the session may not get the headset"); return; }
    SetEvent(e);
    CloseHandle(e);
    g_flatVrStopped = true;
    const ULONGLONG t0 = GetTickCount64();
    ULONGLONG still = t0;
    while (GetTickCount64() - t0 < 5000) {
        Sleep(100);
        const uint32_t c = FlatVRCounter(&running);
        if (c != last) { last = c; still = GetTickCount64(); }
        if (GetTickCount64() - still >= 500) break;
    }
    LogF("vrcam: openxr - FlatVR stopped through the D2R Bridge (%.1f s)", (GetTickCount64() - t0) / 1000.0);
    Sleep(300);
}

// -- the controllers: our own actions, published for the D2R Bridge ------------
// Grip and aim poses, triggers, grips, sticks and the face buttons of both hands,
// suggested for the controllers OpenXR runtimes have. Read once a frame at the
// frame's display time and written into D2RVR_XrInput: the bridge hands them to
// BodyWalk in place of FlatVR's (gestures, the Mapping, the virtual pad).
XrActionSet g_actSet = XR_NULL_HANDLE;
XrAction g_aGrip = XR_NULL_HANDLE, g_aAim = XR_NULL_HANDLE, g_aTrigger = XR_NULL_HANDLE, g_aTriggerClick = XR_NULL_HANDLE,
         g_aSqueeze = XR_NULL_HANDLE, g_aStick = XR_NULL_HANDLE, g_aStickClick = XR_NULL_HANDLE, g_aPrimary = XR_NULL_HANDLE,
         g_aSecondary = XR_NULL_HANDLE, g_aMenu = XR_NULL_HANDLE;
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
HANDLE g_inMap = nullptr;
D2RVR_XrInput* g_in = nullptr;
D2RVR_XrInput g_inNow{};   // the last frame's, for vrcam's own arms

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
void Suggest(const char* profile, std::initializer_list<std::pair<XrAction, const char*>> list) {
    std::vector<XrActionSuggestedBinding> b;
    for (const auto& [a, path] : list) if (a) b.push_back({a, Path(path)});
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = Path(profile);
    sb.countSuggestedBindings = (uint32_t)b.size();
    sb.suggestedBindings = b.data();
    const XrResult r = xrSuggestInteractionProfileBindings(g_inst, &sb);
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
    g_aStickClick = MakeAction("stick_click", "Thumbstick press", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aPrimary = MakeAction("primary", "A / X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aSecondary = MakeAction("secondary", "B / Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    g_aMenu = MakeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
#define BOTH(a, p) {a, "/user/hand/left/input/" p}, {a, "/user/hand/right/input/" p}
    Suggest("/interaction_profiles/oculus/touch_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aSqueeze, "squeeze/value"),
             BOTH(g_aStick, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"),
             {g_aPrimary, "/user/hand/left/input/x/click"}, {g_aPrimary, "/user/hand/right/input/a/click"},
             {g_aSecondary, "/user/hand/left/input/y/click"}, {g_aSecondary, "/user/hand/right/input/b/click"},
             {g_aMenu, "/user/hand/left/input/menu/click"}});
    Suggest("/interaction_profiles/valve/index_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aTriggerClick, "trigger/click"),
             BOTH(g_aSqueeze, "squeeze/value"), BOTH(g_aStick, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"),
             BOTH(g_aPrimary, "a/click"), BOTH(g_aSecondary, "b/click")});
    Suggest("/interaction_profiles/htc/vive_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aTriggerClick, "trigger/click"),
             BOTH(g_aSqueeze, "squeeze/click"), BOTH(g_aStick, "trackpad"), BOTH(g_aStickClick, "trackpad/click"), BOTH(g_aMenu, "menu/click")});
    Suggest("/interaction_profiles/microsoft/motion_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "trigger/value"), BOTH(g_aSqueeze, "squeeze/click"),
             BOTH(g_aStick, "thumbstick"), BOTH(g_aStickClick, "thumbstick/click"), BOTH(g_aMenu, "menu/click")});
    Suggest("/interaction_profiles/khr/simple_controller",
            {BOTH(g_aGrip, "grip/pose"), BOTH(g_aAim, "aim/pose"), BOTH(g_aTrigger, "select/click"), BOTH(g_aMenu, "menu/click")});
#undef BOTH
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
    if (!g_inMap) {   // made by whichever side comes first, readable at any integrity level (as the bridge's blocks)
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
            sa.lpSecurityDescriptor = sd;
        g_inMap = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(D2RVR_XrInput), D2RVR_XR_INPUT_NAME);
        if (sd) LocalFree(sd);
    }
    if (g_inMap && !g_in) g_in = (D2RVR_XrInput*)MapViewOfFile(g_inMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(D2RVR_XrInput));
    Log(g_in ? "vrcam: openxr - controllers: our actions attached, published for the D2R Bridge"
             : "vrcam: openxr - controllers: our actions attached, but the block for the D2R Bridge could not be made");
    return true;
}

// A located pose, only as far as the runtime vouches for it (2026-10-10): its turn finite and of
// unit length (made exactly so), its place only with POSITION_VALID - a hand needs both (else
// none); the head may be a turn alone (valid 2), held at `held`, where it was last located.
void PoseOut(XrSpace space, D2RVR_XrPose* out, float* held = nullptr) {
    *out = {};
    if (!space) return;
    XrSpaceLocation l{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(space, g_roomSpace ? g_roomSpace : g_space, g_fs.predictedDisplayTime, &l))) return;
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
}
// This frame's head and hands, at its display time, to the bridge's block.
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
void Publish(bool focused) {
    if (!g_actionsOk) return;
    D2RVR_XrInput& in = g_inNow;
    in.version = D2RVR_XR_INPUT_VERSION;
    in.focused = focused ? 1u : 0u;
    in.displayTime = g_fs.predictedDisplayTime;
    PoseOut(g_viewSpace, &in.head, g_headHeld);
    for (int h = 0; h < 2; ++h) {
        D2RVR_XrHand& o = in.hand[h];
        bool act = false;
        PoseOut(g_gripSpace[h], &o.grip);
        PoseOut(g_aimSpace[h], &o.aim);
        o.trigger = FloatOf(g_aTrigger, h, &act);
        o.squeeze = FloatOf(g_aSqueeze, h, &act);
        o.stickX = o.stickY = 0.0f;
        {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = g_aStick; gi.subactionPath = g_handPath[h];
            XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(xrGetActionStateVector2f(g_session, &gi, &st)) && st.isActive) { o.stickX = st.currentState.x; o.stickY = st.currentState.y; act = true; }
        }
        uint32_t b = 0;
        if (BoolOf(g_aPrimary, h, &act)) b |= D2RVR_XRB_PRIMARY;
        if (BoolOf(g_aSecondary, h, &act)) b |= D2RVR_XRB_SECONDARY;
        if (BoolOf(g_aStickClick, h, &act)) b |= D2RVR_XRB_STICK;
        if (BoolOf(g_aMenu, h, &act)) b |= D2RVR_XRB_MENU;
        bool clickBound = false;
        if (BoolOf(g_aTriggerClick, h, &clickBound) || (!clickBound && o.trigger > 0.8f)) b |= D2RVR_XRB_TRIGGER;
        if (o.squeeze > 0.8f) b |= D2RVR_XRB_SQUEEZE;
        o.buttons = b;
        o.active = act || o.grip.valid ? 1u : 0u;
        const bool bare = BareHand(h);
        if (bare && !g_cfg.bareHands) {   // no controller in it: nothing pressed, no hand for BodyWalk
            o = D2RVR_XrHand{};
            b = 0;
        }
        if (g_bareSaid[h] != (int)bare) {
            g_bareSaid[h] = bare;
            LogF("vrcam: openxr - the %s hand: %s", h ? "right" : "left",
                 bare ? (g_cfg.bareHands ? "bare (hand tracking), taken as a controller ([openxr] bare_hands=1)" : "bare (hand tracking) - presses nothing, no hand for BodyWalk")
                      : "a controller (or not tracked)");
        }
    }
    in.stamp = D2RVRStampNow();
    in.counter++;
    if (g_in) {
        // the counter last: the bridge takes a block whose counter moved as whole
        D2RVR_XrInput copy = in;
        const uint32_t c = copy.counter;
        copy.counter = g_in->counter;
        memcpy(g_in, &copy, sizeof copy);
        MemoryBarrier();
        g_in->counter = c;
    }
}

// -- start: instance, system, session (a worker thread) ------------------------
void Teardown() {   // under g_lock or before the session exists
    g_running.store(false);
    for (PendingImage& q : g_pendingImg) q = PendingImage{};   // their swapchains go now
    if (g_ptr.sc) xrDestroySwapchain(g_ptr.sc);
    g_ptr = Pointer{};
    for (HudOut& o : g_hudOut) { if (o.sc) xrDestroySwapchain(o.sc); o = HudOut{}; }
    if (g_zoneOut.sc) xrDestroySwapchain(g_zoneOut.sc);
    g_zoneOut = HudOut{};
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
    g_actionsOk = false;
    for (XrHandTrackerEXT& t : g_handTracker) { if (t && g_xrDestroyHandTracker) g_xrDestroyHandTracker(t); t = XR_NULL_HANDLE; }
    g_xrCreateHandTracker = nullptr; g_xrDestroyHandTracker = nullptr; g_xrLocateHandJoints = nullptr;
    g_bareSaid[0] = g_bareSaid[1] = -1;
    g_pairYawAt.store(0);
    g_pairOpen.store(false);
    if (g_in) {   // a last frame that says the session is over: the bridge gives the Mapping back at once
        g_in->focused = 0;
        g_in->head.valid = 0;
        g_in->hand[0] = g_in->hand[1] = D2RVR_XrHand{};
        g_in->stamp = D2RVRStampNow();
        MemoryBarrier();
        g_in->counter++;
    }
    if (g_space) { xrDestroySpace(g_space); g_space = XR_NULL_HANDLE; }
    if (g_session) { xrDestroySession(g_session); g_session = XR_NULL_HANDLE; }
    if (g_inst) { xrDestroyInstance(g_inst); g_inst = XR_NULL_HANDLE; }
    g_kind = kNone;
}

bool StartSession() {
    // the extensions the runtime has
    uint32_t n = 0;
    if (!Ok(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr), "xrEnumerateInstanceExtensionProperties")) return false;
    std::vector<XrExtensionProperties> ext(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, ext.data());
    bool d3d12 = false, hands = false, handSource = false;
    for (const auto& e : ext) {
        if (!strcmp(e.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME)) d3d12 = true;
        if (!strcmp(e.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME)) hands = true;
        if (!strcmp(e.extensionName, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME)) handSource = true;
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
    if (!Ok(r, "xrGetSystem")) return false;
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
        LogF("vrcam: openxr - headset %s, %u layers at most, swapchains up to %ux%u", sp.systemName, sp.graphicsProperties.maxLayerCount,
             sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight);
    }
    {
        uint32_t nv = 0;
        XrViewConfigurationView vv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(g_inst, g_sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, vv)) && nv == 2)
            LogF("vrcam: openxr - each eye's recommended picture %ux%u (the window keeps its own size)", vv[0].recommendedImageRectWidth,
                 vv[0].recommendedImageRectHeight);
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
    if (g_cfg.stopFlatVR) StopFlatVR();
    AcquireSRWLockExclusive(&g_lock);
    const bool ok = StartSession();
    if (!ok) Teardown();
    ReleaseSRWLockExclusive(&g_lock);
    if (!ok) {
        GuardClear("no session, and the game lives");
        Log("vrcam: openxr - no session: FlatVR goes on as always for this run");
        if (g_flatVrStopped) { SignalBridge(D2RVR_FLATVR_START_NAME); g_flatVrStopped = false; }
        g_state.store(kFailed);
        return 0;
    }
    LogModules();
    SignalBridge(D2RVR_GESTURES_GAME_NAME);   // BodyWalk's Mapping on the game's own headset ("D2R VR")
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
    return true;
}
void WaitFence(UINT64 v, DWORD ms) {
    if (!g_fence || g_fence->GetCompletedValue() >= v) return;
    g_fence->SetEventOnCompletion(v, g_fenceEvt);
    WaitForSingleObject(g_fenceEvt, ms);
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
    if (c.sc) { xrDestroySwapchain(c.sc); c.sc = XR_NULL_HANDLE; c.img.clear(); }
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
    if (!Ok(xrCreateSwapchain(g_session, &ci, &c.sc), "xrCreateSwapchain")) { c.bad = true; c.sc = XR_NULL_HANDLE; return false; }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(c.sc, 0, &n, nullptr);
    c.img.assign(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(c.sc, n, &n, (XrSwapchainImageBaseHeader*)c.img.data());
    LogF("vrcam: openxr - %s swapchain %ux%u %s (back buffer %s), %u images", what, c.w, c.h, FormatName(c.fmt), FormatName(d.Format), n);
    if (Family(d.Format) == 3) Log("vrcam: openxr -   a 10-bit back buffer has no sRGB twin: the headset may show it too bright");
    return true;
}

// Our list, opened on the next allocator of the ring / submitted on the game's queue.
int g_openAlloc = 0;
ID3D12GraphicsCommandList* OpenList() {
    if (!MakeCopyList()) return nullptr;
    const int a = g_allocAt;
    g_allocAt = (g_allocAt + 1) % kAllocs;
    WaitFence(g_allocDone[a], 100);
    g_alloc[a]->Reset();
    g_list->Reset(g_alloc[a], nullptr);
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
    const XrResult r = xrWaitSwapchainImage(sc, &wi);
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

bool MakePointerImage(HCURSOR cur) {
    if (g_ptr.sc) { ForgetPending(g_ptr.sc); xrDestroySwapchain(g_ptr.sc); g_ptr.sc = XR_NULL_HANDLE; }
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
    const UINT pitch = ((UINT)n * 4 + 255) & ~255u;
    if (g_ptrUpload) { g_ptrUpload->Release(); g_ptrUpload = nullptr; }
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (UINT64)pitch * n; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              __uuidof(ID3D12Resource), (void**)&g_ptrUpload))) return false;
    uint8_t* dst = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(g_ptrUpload->Map(0, &none, (void**)&dst))) return false;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint32_t b = px[0][(size_t)y * n + x], w = px[1][(size_t)y * n + x];   // BGRA
            uint8_t* o = dst + (size_t)y * pitch + (size_t)x * 4;
            const int cover = 255 - std::clamp((int)(w & 0xFF) - (int)(b & 0xFF), 0, 255);
            o[0] = (uint8_t)((b >> 16) & 0xFF); o[1] = (uint8_t)((b >> 8) & 0xFF); o[2] = (uint8_t)(b & 0xFF); o[3] = (uint8_t)cover;
        }
    g_ptrUpload->Unmap(0, nullptr);
    // a static image: acquired, filled and released once, shown as long as the shape lasts
    int64_t fmt = 0;
    for (int64_t f : g_formats) if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) fmt = f;
    if (!fmt) return false;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = fmt; ci.sampleCount = 1; ci.width = n; ci.height = n; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    if (!Ok(xrCreateSwapchain(g_session, &ci, &g_ptr.sc), "xrCreateSwapchain(pointer)")) { g_ptr.sc = XR_NULL_HANDLE; return false; }
    uint32_t cnt = 0;
    xrEnumerateSwapchainImages(g_ptr.sc, 0, &cnt, nullptr);
    std::vector<XrSwapchainImageD3D12KHR> img(cnt, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    xrEnumerateSwapchainImages(g_ptr.sc, cnt, &cnt, (XrSwapchainImageBaseHeader*)img.data());
    uint32_t idx = 0;
    if (!cnt) return false;
    if (const int got = NextImage(g_ptr.sc, &idx, "pointer"); got <= 0) {
        if (got == 0) { g_ptr.shape = nullptr; g_ptrRetryAt = GetTickCount64() + 1000; }   // made again in a second
        return false;
    }
    if (ID3D12GraphicsCommandList* l = OpenList()) {
        ID3D12Resource* t = img[idx].texture;
        D3D12_RESOURCE_BARRIER b = Tr(t, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        l->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = t; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = g_ptrUpload; from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, (UINT)n, (UINT)n, 1, pitch};
        l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        b = Tr(t, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        l->ResourceBarrier(1, &b);
        SubmitList();
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    g_ptr.ok = Ok(xrReleaseSwapchainImage(g_ptr.sc, &ri), "xrReleaseSwapchainImage(pointer)");
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
    const float cx = (float)(p.x - g_ptr.hotX) + 0.5f * n, cy = (float)(p.y - g_ptr.hotY) + 0.5f * n;   // the image's middle, window pixels
    const XrVector3f off = Rot(pic.pose.orientation, {(cx / rc.right - 0.5f) * W, (0.5f - cy / rc.bottom) * H, 0.003f});   // a hair in front
    *out = pic;
    out->subImage.swapchain = g_ptr.sc;
    out->subImage.imageRect = {{0, 0}, {g_ptr.n, g_ptr.n}};
    out->pose.position = {pic.pose.position.x + off.x, pic.pose.position.y + off.y, pic.pose.position.z + off.z};
    out->size = {W * n / rc.right, H * n / rc.bottom};
    out->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
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
  float g_pad3;
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
  return float4(straight * cover, cover);
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
constexpr int kSrvRing = 16, kRtvs = 32;
uint32_t g_srvAt = 0;
int g_rtvAt = 0;
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
    }
    if (!ok) { Log("vrcam: openxr - interface in the room: the pipeline could not be made - the toolbar and the map stay off"); return false; }
    g_hudFailed = false;
    Log("vrcam: openxr - interface in the room: FlatVR's shader compiled (the toolbar, the map orb; the gesture zones' balls)");
    return true;
}

bool EnsureHudOut(HudOut& o, uint32_t w, uint32_t h, const char* what) {
    if (o.sc && o.w == w && o.h == h) return true;
    if (o.sc) { xrDestroySwapchain(o.sc); o.sc = XR_NULL_HANDLE; o.img.clear(); }
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
    if (g_rtvAt + (int)n > kRtvs) g_rtvAt = 0;   // (the old chain's views go with it)
    o.rtv0 = g_rtvAt;
    g_rtvAt += (int)n;
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
bool DrawHud(HudOut& o, ID3D12Resource* src, bool orb, const float cb[12]) {
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
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)o.w, (float)o.h, 0.0f, 1.0f};
    const D3D12_RECT sc{0, 0, (LONG)o.w, (LONG)o.h};
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
    for (int i = 0; i < hud::kNativePieces && nl < max; ++i) {
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

// BodyWalk's gesture zones ticked "VR" in its Mapping tab (the belt's potions), handed on
// by the bridge (D2RVR_XrZones): placed by BodyWalk from the head this game gave it, so in
// the space that head was located in (g_roomSpace). Read whole (the counter odd while the
// bridge writes); none once BodyWalk has sent none for half a second.
HANDLE g_zonesMap = nullptr;
const D2RVR_XrZones* g_zonesIn = nullptr;

uint32_t ReadZones(D2RVR_XrZone* out) {
    if (!g_zonesIn) {
        static ULONGLONG lastTry = 0;   // the bridge makes the block: looked for once a second until it is there
        if (lastTry && GetTickCount64() - lastTry < 1000) return 0;
        lastTry = GetTickCount64();
        if (!g_zonesMap) g_zonesMap = OpenFileMappingW(FILE_MAP_READ, FALSE, D2RVR_XR_ZONES_NAME);
        if (!g_zonesMap) return 0;
        g_zonesIn = (const D2RVR_XrZones*)MapViewOfFile(g_zonesMap, FILE_MAP_READ, 0, 0, sizeof(D2RVR_XrZones));
        if (!g_zonesIn) return 0;
        Log("vrcam: openxr - BodyWalk's gesture zones found (the bridge's block)");
    }
    const volatile uint32_t& counter = ((const volatile D2RVR_XrZones*)g_zonesIn)->counter;
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t c = counter;
        if (c & 1u) { YieldProcessor(); continue; }
        MemoryBarrier();
        const uint32_t ver = g_zonesIn->version, stamp = g_zonesIn->stamp;
        const uint32_t n = std::min<uint32_t>(g_zonesIn->count, D2RVR_XR_ZONES_MAX);
        memcpy(out, (const void*)g_zonesIn->zone, n * sizeof(D2RVR_XrZone));
        MemoryBarrier();
        if (counter != c) continue;
        if (ver != D2RVR_XR_ZONES_VERSION || D2RVRStampNow() - stamp > 5000u) return 0;   // 0.1 ms units: half a second
        return n;
    }
    return 0;
}

// The zones as see-through balls, each a quad turned to the head, the nearest kept when
// there are more than the runtime's layers allow, the farthest laid first so a nearer one
// lies over it. Appended to layers[] from nl on, at most max.
uint32_t HangZones(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    D2RVR_XrZone z[D2RVR_XR_ZONES_MAX];
    uint32_t n = ReadZones(z);
    if (!n || nl >= max || !g_inNow.head.valid || !MakeHudPipeline()) return nl;
    const XrVector3f head = P3(g_inNow.head.pos);
    uint32_t order[D2RVR_XR_ZONES_MAX];
    float dist[D2RVR_XR_ZONES_MAX];
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
        const D2RVR_XrZone& zone = z[order[k]];
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
bool CopyInto(Chain& c, ID3D12Resource* bb) {
    if (!MakeCopyList()) return false;
    uint32_t idx = 0;
    if (NextImage(c.sc, &idx, "picture") <= 0) return false;
    ID3D12Resource* dst = c.img[idx].texture;
    if (!OpenList()) return false;
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

void EndSessionNow(const char* why) {
    g_pairYawAt.store(0);
    if (g_running.exchange(false) && g_session) Ok(xrEndSession(g_session), "xrEndSession");
    LogF("vrcam: openxr - session ended (%s)", why);
}

// The runtime's events: begin when it says ready, end when it says stopping, give
// the headset back to FlatVR when the session or the instance is lost.
void Poll() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (g_inst && xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& sc = *(const XrEventDataSessionStateChanged*)&ev;
            g_ss = sc.state;
            LogF("vrcam: openxr - session %s", StateName(g_ss));
            if (g_ss == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                Log("vrcam: openxr - xrBeginSession");
                if (Ok(xrBeginSession(g_session, &bi), "xrBeginSession")) {
                    g_running.store(true); g_recentre.store(true); g_recentreAfter = 0; g_framesRunning = 0; g_st = Stats{};
                    g_monoAnchored = false; g_lastDrawn = kNone;   // the flat picture comes up anew, held to the gaze at first
                }
            } else if (g_ss == XR_SESSION_STATE_STOPPING) {
                EndSessionNow("the runtime stopped it");
            } else if (g_ss == XR_SESSION_STATE_EXITING || g_ss == XR_SESSION_STATE_LOSS_PENDING) {
                EndSessionNow(g_ss == XR_SESSION_STATE_EXITING ? "exiting" : "the headset is lost");
                Teardown();
                g_state.store(kFailed);
                if (g_flatVrStopped) { SignalBridge(D2RVR_FLATVR_START_NAME); g_flatVrStopped = false; Log("vrcam: openxr - FlatVR started again"); }
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            EndSessionNow("the runtime is going away");
            Teardown();
            g_state.store(kFailed);
            if (g_flatVrStopped) { SignalBridge(D2RVR_FLATVR_START_NAME); g_flatVrStopped = false; }
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
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    if (!Ok(xrWaitFrame(g_session, &wi, &g_fs), "xrWaitFrame")) return false;
    if (++g_framesRunning == 90) GuardClear("the session runs: 90 frames");
    const double t1 = UsNow();
    g_st.waitUs += t1 - t0;
    g_st.waitMax = std::max(g_st.waitMax, t1 - t0);
    if (crumb) LogF("vrcam: openxr -   should render %d, display period %.2f ms; xrBeginFrame", (int)g_fs.shouldRender, g_fs.predictedDisplayPeriod / 1e6);
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    const XrResult br = xrBeginFrame(g_session, &bi);
    if (br != XR_FRAME_DISCARDED && !Ok(br, "xrBeginFrame")) return false;
    g_beginUs = t1;
    g_kind = kind;
    g_copied[0] = g_copied[1] = false;
    g_viewsOk = false;
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
        for (int e = 0; e < 2; ++e) g_fovUsed[e] = Cropped(g_views[e].fov);
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
            bool anchor = g_lastDrawn != kMono || !g_monoAnchored || framesIn < 45;
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
        const bool tracked = (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
        if (g_fs.shouldRender && (tracked || g_framesRunning > 45) && g_fs.predictedDisplayTime >= g_recentreAfter &&
            g_recentre.exchange(false)) {   // (the request waits until then: the exchange last)
            g_monoAnchored = false;   // the flat picture comes up ahead again
            g_cYaw = YawOf(g_views[0].pose.orientation);
            const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
            g_cPos = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
            g_bodyX = g_cPos.x; g_bodyZ = g_cPos.z;
            LogF("vrcam: openxr - recentred: yaw %.1f deg, the head at %.2f %.2f %.2f m", g_cYaw * 57.29578f, g_cPos.x, g_cPos.y, g_cPos.z);
        }
        static bool told = false;
        if (!told) {
            told = true;
            const XrVector3f& a = g_views[0].pose.position, &b = g_views[1].pose.position;
            const float ipd = sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            const XrQuaternionf& qa = g_views[0].pose.orientation, &qb = g_views[1].pose.orientation;
            const float dot = std::min(1.0f, fabsf(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w));
            const float r2d = 57.29578f;
            LogF("vrcam: openxr - eyes %.1f mm apart, %.2f deg between their views; left fov %.1f %.1f %.1f %.1f, right %.1f %.1f %.1f %.1f (left right up down, deg)%s",
                 ipd * 1000.0f, 2.0f * acosf(dot) * r2d, g_views[0].fov.angleLeft * r2d, g_views[0].fov.angleRight * r2d, g_views[0].fov.angleUp * r2d,
                 g_views[0].fov.angleDown * r2d, g_views[1].fov.angleLeft * r2d, g_views[1].fov.angleRight * r2d, g_views[1].fov.angleUp * r2d,
                 g_views[1].fov.angleDown * r2d, g_cfg.fovCrop ? " - cropped to the window's aspect" : "");
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
    if (!g_fs.shouldRender) ++g_st.noRender;
    return true;
}

void CloseFrame() {
    if (g_kind == kNone) return;
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD}, pointer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[4 + kZonesMax] = {};
    const uint32_t most = std::min<uint32_t>(4 + kZonesMax, g_maxLayers);
    uint32_t nl = 0;
    if (g_fs.shouldRender && g_kind == kPair && g_copied[0] && g_copied[1] && g_viewsOk) {
        for (int e = 0; e < 2; ++e) {
            pv[e].pose = g_views[e].pose;   // the poses and fovs the eyes were drawn with
            pv[e].fov = g_fovUsed[e];
            pv[e].subImage.swapchain = g_eyeChain[e].sc;
            pv[e].subImage.imageRect = {{0, 0}, {(int32_t)g_eyeChain[e].w, (int32_t)g_eyeChain[e].h}};
        }
        proj.space = g_space;
        proj.viewCount = 2;
        proj.views = pv;
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&proj;
        nl = HangZones(layers, nl, most - std::min<uint32_t>(most - nl, 2));   // the belt's balls; room kept for the two below
        nl = HangHud(layers, nl, most);   // the toolbar and the map, where FlatVR hung them
        ++g_st.pairs;
    } else if (g_fs.shouldRender && g_kind == kMono && g_copied[0]) {
        // the picture where the head looked when it came up, level, world-locked
        const XrQuaternionf q = Yaw(g_monoAnchored ? g_monoYaw : g_cYaw);
        const XrVector3f at = g_monoAnchored ? g_monoPos : g_cPos;
        const XrVector3f ahead = Rot(q, {0.0f, 0.0f, -g_cfg.quadM});
        quad.space = g_space;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = g_quadChain.sc;
        quad.subImage.imageRect = {{0, 0}, {(int32_t)g_quadChain.w, (int32_t)g_quadChain.h}};
        quad.pose.orientation = q;
        quad.pose.position = {at.x + ahead.x, at.y + ahead.y, at.z + ahead.z};
        const float a = g_quadChain.h ? (float)g_quadChain.w / (float)g_quadChain.h : 16.0f / 9.0f;
        quad.size = {g_cfg.quadWidthM, g_cfg.quadWidthM / a};
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&quad;
        if (PointerQuad(quad, &pointer)) layers[nl++] = (const XrCompositionLayerBaseHeader*)&pointer;
        nl = HangZones(layers, nl, most);   // the belt's balls stay where they are over a panel too
        ++g_st.monos;
    }
    static int ends = 0;
    if (ends < 3) LogF("vrcam: openxr - frame %d: xrEndFrame with %u layer(s)", ++ends, nl);
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = g_fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = nl;
    ei.layers = nl ? layers : nullptr;
    Ok(xrEndFrame(g_session, &ei), "xrEndFrame");
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
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
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
    bool on = f(L"on", 0.0f) != 0.0f;
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
                    "native OpenXR is switched OFF ([openxr] on=0) - the game goes on FlatVR. Switch it on again in D2R VR Settings > "
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
    g_cfg.fovCrop = f(L"fov_crop", 1.0f) != 0.0f;   // on: sharper (square pixels), black above and below (2026-10-10)
    g_cfg.symmetric = f(L"symmetric_fov", 1.0f) != 0.0f;
    {   // the panels' distance FlatVR was given ([screen] menu_distance_m), unless [openxr] says its own
        wchar_t b[32];
        GetPrivateProfileStringW(L"screen", L"menu_distance_m", L"2", b, 32, ini);
        const float menuM = wcstof(b, nullptr);
        g_cfg.quadM = std::clamp(f(L"flat_distance_m", std::isfinite(menuM) && menuM > 0.3f ? menuM : 2.0f), 0.5f, 10.0f);
    }
    g_cfg.quadWidthM = std::clamp(f(L"flat_width_m", 2.4f), 0.3f, 20.0f);
    g_cfg.leanM = std::clamp(f(L"lean_m", 0.10f), 0.0f, 5.0f);
    g_cfg.bareHands = f(L"bare_hands", 0.0f) != 0.0f;
}

bool On() { return g_cfg.on && g_state.load() != kFailed; }
bool Running() { return g_running.load(); }
float LeanM() { return g_cfg.leanM; }
bool StopPending() { return g_stopWanted.load() && g_state.load() != kIdle; }
void Recenter() { g_recentre.store(true); }

void Stop(bool final);
// The session started on the game's device and queue (a worker thread), once.
void Start(ID3D12Device* dev, ID3D12CommandQueue* queue) {
    if (g_state.load() != kIdle || !dev || !queue) return;
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
    return ok;
}

bool PairEyes(Eyes* out) {
    if (g_kind != kPair || !g_viewsOk) return false;
    const XrQuaternionf back = Yaw(-g_cYaw);
    {   // the body follows the head once it leans farther than lean_m
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
    if (g_kind != kPair || !bb || eye < 0 || eye > 1 || g_copied[eye]) return;
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
    if (g_session) CloseFrame();
    g_kind = kNone;
    ReleaseSRWLockExclusive(&g_lock);
}

void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* queue, ID3D12Resource* bb, int pairEye) {
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

// BodyWalk's bridge come after the session was made (BodyWalk started later): the signal for
// its Mapping sent again - the one at the start found no bridge to take it (2026-10-10).
void FollowBridge() {
    static bool had = false;
    if (g_state.load() != kCreated) { had = false; return; }
    HANDLE e = OpenEventW(EVENT_MODIFY_STATE, FALSE, D2RVR_GESTURES_GAME_NAME);
    const bool have = e != nullptr;
    if (have && !had) {
        SetEvent(e);
        Log("vrcam: openxr - BodyWalk's D2R Bridge is there: its Mapping on the game's headset (D2R VR)");
    }
    had = have;
    if (e) CloseHandle(e);
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
    if (had) SignalBridge(D2RVR_GESTURES_FLATVR_NAME);   // the Mapping back on FlatVR (if it was the game's)
    if (g_flatVrStopped) { SignalBridge(D2RVR_FLATVR_START_NAME); g_flatVrStopped = false; Log("vrcam: openxr - FlatVR started again"); }
    ReleaseSRWLockExclusive(&g_lock);
}

void Shutdown() { Stop(true); }

void DeviceGone(ID3D12Device* dev) {
    if (dev && dev == g_dev && g_state.load() != kIdle) Shutdown();
}

}  // namespace xr
