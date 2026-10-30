// vrcam - Diablo II: Resurrected in a headset, or as a first-person game.
//
// The camera itself is cleanroom/camera (d2rcam): it hands this layer the game's
// own view and the hero point once a game frame and puts back the view and
// projection made here. Render distance is cleanroom/drawdist. This file is the
// VR layer on top:
//   - the view turns with the head, read from BodyWalk through Game Link
//     (vr/link.cpp, bodywalk/game_link.h), or with the mouse;
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

#include "vrcam_headers.h"   // the headers and the other files' declarations (docs/plan_modules.md M2)

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

// The plugin's own names: in the namespace vrc (no longer an unnamed one) so that the parts cut from this file into vr/<area>/ (docs/plan_modules.md)
// can name what they share with it through vr/vrcam_internal.h; the using-directive makes every name of it as visible here as an unnamed
// namespace's were.
namespace vrc {}
using namespace vrc;
namespace vrc {
// [debug] melee_press_trace (vr/press_trace.h): the notes the press's way gives; the recorder and the line are further down, beside MeleeAnimNote.
std::atomic<bool> g_pressTraceKey{false};   // [debug] melee_press_trace
std::atomic<bool> g_ptActive{false};        // a press is being traced (the hooks' cheap test)
void PressTracePoll(uint16_t buttons);
void PressTraceSlot(int site, int slot, int bound, int answer);
void PressTraceLookup(int which, void* unit, bool ours);
void PressTraceCast(int skill, bool directed);


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
    frametrace::Scope trace(frametrace::kLog);   // [debug] frame_trace: what the host's log costs the thread that wrote the line (one relaxed load when off)
    if (g_ctx != nullptr) logasync::Info(g_ctx, text);
    crash::Mirror(text);
}
void LogF(const char* fmt, ...);
void ftraceSetOverMs(float ms);   // [debug] frame_trace_ms (vr/frame_trace.h), below
// To the game's console as well as the log.
void Say(const char* text) {
    if (g_ctx == nullptr) return;
    logasync::Info(g_ctx, text);
    crash::Mirror(text);
    g_ctx->WriteConsoleMessage(text, ConsoleMessageKind::Output);
}

// [debug] health (vr/health.h): one line per feature every 10 s, a mark where a chain is broken. The body's counts are taken at its wiring
// (the pushes the weight model gives, the poses the skeleton hook and the skinning are given back - D2RLoaderLoadPlugin), the sky's at
// each present of our effect (EffectsBegin), the lines on the update thread. Counted only while the key is on.
health::Body g_healthBody;
health::Sky g_healthSky;
void HealthPushBody(const monsterbodies::Snapshot& snap, int i, const float impulse[3], const float point[3]) {
    g_healthBody.pushes.Add();
    meleetouch::PushBody(snap, i, impulse, point);
}
bool HealthMonsterPose(uintptr_t self, dollpose::Pose* out) {
    const bool given = meleetouch::MonsterPose(self, out);
    g_healthBody.posesAsked.Add();
    if (given) g_healthBody.posesGiven.Add();
    return given;
}
int HealthMonsterPosesForSkin(uintptr_t skeleton, uintptr_t worldPose, dollpose::Pose* out, int max, bool* exact) {
    const int k = meleetouch::MonsterPosesForSkin(skeleton, worldPose, out, max, exact);
    g_healthBody.skinAsked.Add();
    if (k > 0) g_healthBody.skinGiven.Add();
    return k;
}
void HealthTick(ULONGLONG nowMs) {
    static ULONGLONG next = 0;
    if (nowMs < next) return;
    next = nowMs + 10000;
    if (!health::On()) return;
    for (const std::string& line : {g_healthBody.Take(), g_healthSky.Take()})
        if (!line.empty()) Log(line.c_str());
}

// Settings (d2r_vr.ini, re-read on change): struct Settings and g_set in vr/config/settings.h
std::atomic<float> g_iconLiftMm{1.0f};   // [gameplay] melee_skill_armed_icon_lift_mm: the skill icon in the mana medallion this many mm over its depth


// The ini's path and IniF / IniB: vr/config/settings.cpp

// The area's biome and [sky] outdoor (g_biome, LoadOutdoorBiomes): vr/world/area.cpp

// The sky's and the ceiling's configuration (the ceiling biomes, the acts' pictures, the cave fog, kPalettes): vr/sky/sky_config.cpp


// The game's Settings.json (GameInterfaceScale, NoTemporalAA, MiniMapForHand, the window, the frame cap, the safe screen): vr/config/reconcile.cpp


std::atomic<bool> g_ownDepth{true};   // [render] own_depth (scenedepth::)
// [render] no_reshade (off): ReShade64.dll not loaded at all - native OpenXR eyes by turns on the plugin's own D3D12 hooks (vr/native_d3d12.h),
// no fog, sky, ceiling, overlays, one-pass stereo or light bounds (docs/plan_no_reshade.md). Decided once, at the plugin's load (LoadReShade).
bool g_noReShade = false;
std::atomic<bool> g_squareWin{false};   // [openxr] square_window: D2R VR Settings' square game window
// The left eye's view as last built, and the half-eye shift in it (VrViewInner): the
// replay makes the right eye's camera constants from them.
float g_leftView[16] = {};
SRWLOCK g_leftViewLock = SRWLOCK_INIT;
std::atomic<float> g_eyeHalf{0.0f};
// F1 / F3 in 3D ([openxr] pointer_3d, vr/pointer3d.h): what the game's pointer needs to be drawn where its mouse points - the map from the frame
// the pair's eyes are in into the game's world, the game's ground, and the left eye's camera as the game's own ScreenToRay has it. Written by
// the view / projection builds of the left eye, read when the pair's frame is closed, the same thread; a lock anyway.
PointerMap g_ptrMap;
SRWLOCK g_ptrLock = SRWLOCK_INIT;
// TableCamera's diorama and hero as last built (for the map above)
views3d::Table g_lastTable{};
views3d::Vec g_lastTableHero{};
// [multiplayer] pose_share's switches and PoseShareConfigure: vr/mp/pose_share.cpp

// LoadSettings: vr/config/load_settings.cpp


// State
std::atomic<bool> g_enabled{false};
std::atomic<float> g_mouseYaw{0.0f}, g_mousePitch{0.0f};   // the mouse's turn on top of the head (deg)
std::atomic<float> g_recenter{0.0f};    // head yaw that counts as straight ahead
std::atomic<float> g_recenterPitch{0.0f}; // head pitch that counts as level (F11)
std::atomic<float> g_recenterRoll{0.0f};  // head roll that counts as upright (F11)
// [flatvr] room_centre: straight ahead as a yaw of BodyWalk's room (headYawRoomDeg - the zero FlatVR places its screen in, which
// BodyWalk's own Reset Center never moves), kept at our F11; and where the head stood then ([flatvr] head_position).
std::atomic<float> g_recenterRoom{0.0f};
std::atomic<bool>  g_recenterRoomOk{false};
std::atomic<float> g_recenterPos[3] = {};
std::atomic<bool>  g_recenterPosOk{false};
std::atomic<float> g_bwBase{0.0f};        // BodyWalk's zero of the yaw it sends, in its room (headYawRoomDeg - headYawDeg), last seen
std::atomic<bool>  g_bwBaseOk{false};
std::atomic<float> g_turnYaw{0.0f};     // body turn from the right stick; F11 keeps it
// Held for a pair's two passes (HookDrawGameScreen): the stick and the mouse
// move on a 10 ms timer, and between the passes the eyes got different turns.
std::atomic<float> g_heldYaw{0.0f}, g_heldPitch{0.0f};
extern std::atomic<bool> g_pairNow;
bool NativeView();
bool NativeHeadView();
std::atomic<float> g_rightX{0.0f};      // last right stick X the game polled, -1..1
std::atomic<float> g_rightY{0.0f};      // ... and its Y, + up ([camera] stick_pitch_f2)
// [camera] stick_pitch_f2: the right stick's tilt on top of [camera] pitch (deg, + looks lower), VR F2 only; F11 forgets it.
// Held for a pair as g_heldPitch is.
std::atomic<float> g_stickPitch{0.0f}, g_heldStickPitch{0.0f};
// When a poll last got BodyWalk's own pad (PadFromBodyWalk). Some polls of pad 0
// came back without it (the pad trace flipped 2000 / 0000 within a millisecond)
// and wrote the right stick back to 0 between BodyWalk's: the body did not turn
// in F4 (2026-10-07). While BodyWalk's pad is live only its polls set what
// TurnStick keeps.
std::atomic<ULONGLONG> g_padMirrorOkAt{0};
std::atomic<float> g_camYaw{0.0f};
std::atomic<bool> g_facingReset{false};   // F11: forget the hero-facing candidates, search again      // the yaw last folded into the view; the stick turns by this
std::atomic<uint32_t> g_gen{1};

// The head from BodyWalk and the view's yaw, pitch and roll (Shared, PredictHead, TargetYaw, TargetPitch, ...): vr/view/head.cpp

// What the stick needs, taken where the view is folded: our camera's right and
// forward flattened onto the ground, and the game's own camera's screen axes
// in the world. The game reads its stick as a direction on ITS screen.
SRWLOCK g_stickLock = SRWLOCK_INIT;
StickFrame g_stickFrame{};


// Which view (g_view, ThirdPerson, FullBody, ... ClassicNow): vr/view/view_state.cpp
// [openxr] hand_inventory (vr/hand_inventory.h, HandInventoryTick): the inventory hangs on the left hand away from town, and the left stick
// walks on - the walking hook takes the stick with that menu open (DirectWalkNow), the pad's left stick is kept from the menu (TurnStick).
std::atomic<bool> g_handWalk{false};
std::atomic<uint32_t> g_handWalkReads{0};   // the game's walk read its stick (HookStickGet at the walk's call) while g_handWalk - the log's evidence
// The game's window, from the swap chain (OnReshadePresent): where FlatVR's laser sends the mouse.
std::atomic<HWND> g_swapWnd{nullptr};
// [openxr] third_gaze: in F2 the camera swings round the hero with the gaze (1, as on FlatVR's
// screen) or stays behind him on the body's turn while the head looks round (0, default).
std::atomic<bool> g_xrThirdGaze{false};
// Our camera looking down on a hero the game animates whole, turned by nobody: F1 in perspective and the floor.
std::atomic<float> g_topUnitsPerM{0.0f};   // F1 in perspective: world units per metre on the screen's plane (TopCamera), 0 = not known yet
// The toolbar's place per view (BarNearNow, BarSizeNow, BarOffsetNow): vr/hud/looks.cpp
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
float UnderLiftForView();   // [body] under_camera (below)
float JumpLift() {
    float eye = 0.0f, lift = 0.0f;
    const float arc = UnderLiftForView();   // [body] under_camera + leap_camera_up: the game's own lift of the model on a Leap's arc
    if (ThirdPerson() || !g_set.followJump.load() || !skel::HeroEye(&eye, &lift)) return arc;
    return lift * g_heroScale.load() + arc;
}

// The head as the last view saw it: its centre (before the eyes' half shift) and where it looks
// along the ground - the belt's potions hang round it (engitem), not round the walking legs.
std::atomic<float> g_headCentre[3], g_headAhead[3];
std::atomic<bool> g_headOk{false};
std::atomic<float> g_eyeWorld[3];   // the camera in the world (third person: its pivot), last view rebuild - the hands hang off it
// F3: our camera over the game's ground, for the mouse pointer laid on it (FloorPointerTick) - the eyes this
// high above the hero's ground (world units), and the world-up (y) parts of the view's forward and up.
std::atomic<float> g_floorEyeH{0.0f}, g_floorFwdY{0.0f}, g_floorUpY{0.0f};
std::atomic<float> g_viewFwdY{0.0f};   // every view: the world-up part of where the camera looks (- = down), for the F2 crosshair
std::atomic<float> g_viewRightY{0.0f}, g_viewUpY{1.0f};   // ... and of its right and up ([fog] game_fog_before_fx: a pixel's height above the eye)
// [gameplay] leap_by_gaze: every view, where the head looks (the camera's forward, world) and how high the eyes are over the hero's ground (world units, the
// look-at's height is his ground), with when (GetTickCount64)
std::atomic<float> g_gazeFwd[3] = {}, g_gazeEyeH{0.0f};
std::atomic<float> g_gazeLook[3] = {};   // [gameplay] teleport_circle: the look-at of the same view (the hero as drawn: the arc's ground and the circle's middle)
std::atomic<ULONGLONG> g_gazeAt{0};
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
HWND GameWindow();   // the game's main window (found when asked), null if it has none yet

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
// The game on the floor's bounds (D2R_DepthFog.fx TableKey): the ground the game's
// own camera would show. Its view x projection and the hero, from the same view build.
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
// A replayed pair's right eye: the left view moved by the eye gap, kept aside by the left build and put in g_skyView[1] (and its stamp)
// only when the replay draws that eye (CommitRightSky) - never before: the next pair's left values carried it ahead of the right eye's
// effects still on the GPU (vr/sky_view_eyes.h). Under g_skyLock; the stamp on the draw thread.
SkyView g_skyRightNext{};
bool g_skyRightNextOk = false;
uint32_t g_stampRightNext = 0;
bool g_stampRightNextOk = false;

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
// KeepBlockName: a block BodyWalk or FlatVR makes (screen size, pad, Game Link's
// blocks) is opened once and its HANDLE kept, not only the view. A section loses
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
// (Avatar > Body Height, handed over by Game Link), else [stereo]
// user_height_m; an adult's eyes sit at ~0.936 of the height. Seated or not:
// it is the scale, not where the head is now. With [calibration] use=1 and a stored T-pose measurement, that
// measured height instead of both (vr/calibration.cpp; 0 = not in use).
float UserEyeHeightM() {
    if (const float measured = calib::MeasuredEyeM(); measured > 0.0f) return measured;
    const D2RVR_Shared* sh = Shared();
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

// Frame log ([debug] frame_log, flog): vr/diag/flog.cpp

// Settings' Collect logs: the frame log for 10 s and the state it ran in, once in the log.
bool AfrOn();
extern std::atomic<bool> g_inWorld;
namespace replay { extern std::atomic<int> g_want; extern std::atomic<bool> g_on; extern std::atomic<int> g_ahead; extern std::atomic<uint32_t> g_replays; }
void flog::StartDiag() {
    g_diagUntil.store(GetTickCount64() + 10000);
    LogF("vrcam: diagnostic capture (D2R VR Authentic Settings, Collect logs): 10 s of d2r_vr_frames.csv - stereo %d, pair per frame %d, "
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
// The labels', the plate's and the toolbar's looks per view (LabelLookNow ... PlateTurnsWanted): vr/hud/looks.cpp
// Where the game draws our world's things itself - the sky dome ([sky] game_sky), the cave dome, the cave ceiling ([ceiling] game_ceiling)
// and its fog ([fog] game_fog with its height, mist, caves, curve and the step before the effects): VR first person (F4), and with
// [render] game_drawn_flat the flat game too in its views with our camera (first and third person; from above the game's own camera, too
// high for these) - Serg (2026-10-27): "в обычном режиме тоже".
bool TopPersp();
bool GameDrawnView() {
    if (viewgate::F4()) return true;
    return g_set.gameDrawnFlat.load() && g_set.platform.load() == 0 && g_enabled.load() && g_set.flatView.load() != 1 && !TopPersp();
}

// FlatVR's 3D source as [stereo] has it - real stereo: the game's pair; else the
// depth (source 1) or none (0) - asked through Game Link whenever BodyWalk comes
// up (BodyWalk started: a profile may have set the pair on) and whenever the
// setting changes. FlatVR with the pair on and one picture coming shows it flat
// (2026-10-08).
void FlatVr3DTick() {
    static int sent = -1;
    static bool had = false;
    const int want = g_set.afr.load() ? BW_LINK_STEREO_PAIR : ((int)IniF(L"stereo", L"source", 1.0f) == 0 ? BW_LINK_STEREO_NONE : BW_LINK_STEREO_DEPTH);
    uint32_t caps = 0;
    const bool have = link::HostAlive(&caps) && (caps & BW_LINK_CAP_STEREO_SOURCE);
    if (have && (!had || want != sent)) {
        link::RequestStereoSource(want);
        sent = want;
        static const char* const kSaid[3] = {"none (a flat screen)", "the depth (ReShade)", "the game's stereo pair"};
        LogF("vrcam: FlatVR's 3D source set to %s (through Game Link)", kSaid[want]);
    }
    had = have;
}

// The addon and FlatVR take a pair while the block says so. Its writers run on
// the world camera's copy and in a pair's passes - neither runs in the menus -
// so the timer takes the word back as soon as stereo is off.
void AfrQuietWhenOff() {
    if (g_afrBlock && g_afrBlock->enabled && !AfrOn()) g_afrBlock->enabled = 0u;
}
bool PairWanted();
bool NativeByTurns();
// The two passes of the pair being drawn are one game frame (the replay or two passes): the right pass may compare itself with the
// left one. Eyes by turns (NativeByTurns) has a pair too - one pass, its eye in turn - but each eye is a game frame of its own: a
// check made for "the right pass of this frame" read the next frame there - the hero's facing given up for the memory search
// (StructFreshness, FacingCand), the hanging props stepped every second frame (props_once_a_pair): "the doll keeps coming off and
// goes twice as big" (the author, 2026-10-20).
bool PairOfOneFrame();

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


// The hero point of the last frame, for code off the frame thread.
SRWLOCK g_lookLock = SRWLOCK_INIT;
float FogTopNow();   // [fog] game_fog_top above the eye, as the passes take it (below, with g_lookAt)
float g_lookAt[3] = {};
// [fog] game_fog_top above the eye as the game's fog passes take it: as set, or ([fog] game_fog_top_from_floor) counted from the hero's floor
// - the camera's height over his feet (g_eyeWorld, g_lookAt) taken off (vr/game_fog_vr.h TopAboveEye)
float FogTopNow() {
    float feet = 0.0f;
    AcquireSRWLockShared(&g_lookLock);
    feet = g_lookAt[1];
    ReleaseSRWLockShared(&g_lookLock);
    return gamefogvr::TopAboveEye(g_set.fogTop.load(), g_set.fogTopFromFloor.load(), g_eyeWorld[1].load() - feet);
}
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
// Our camera (VrFrame, VrView / VrViewInner, VrProj / VrProjInner, the top and table cameras, the eyes): vr/view/camera.cpp

// The game's pointer in F1 and F3 in 3D ([openxr] pointer_3d, vr/xr.cpp Pointer3dQuad): the ground under the pixel the game's mouse is at - the
// point the game's own mouse code (d2rcam's ScreenToRay, from our left eye) would walk to - in the frame the pair's eyes are in. Only read: the
// mouse, the clicks and the hero's movement are not touched. The last frame's camera, the same thread as the build.
bool PointerAtPixel(int mode, float u, float v, xr::PointerAt* out) {
    PointerMap m;
    AcquireSRWLockShared(&g_ptrLock);
    m = g_ptrMap;
    ReleaseSRWLockShared(&g_ptrLock);
    const ULONGLONG now = GetTickCount64();
    if (!m.ok || m.mode != mode || now - m.viewAt > 300 || now - m.projAt > 300) return false;
    const pointer3d::Under h = pointer3d::GroundUnderPixel(m.sim, m.groundY, m.view, m.proj, u, v);
    if (!h.hit) return false;
    out->at[0] = h.at.x; out->at[1] = h.at.y; out->at[2] = h.at.z;
    out->dist = h.dist;
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
    // (1024: the overlays' 10 s line lost its cameras' miss - what says "shown twice" - at 256)
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    Log(b);
}

// XInput's entries hooked (XHook, g_xhooks): vr/input/pad.cpp

// The aim: the bow's stick, the aiming hand, the staff's and the spell model's aim, the hand's ray (HandRay): vr/input/aim.cpp
bool HandRay(float* o, float* d, float* dRaw = nullptr);   // (declared here: its default is its definition's)

// Keys and the mouse sent to the game (SendShift, SendKeysMouse), the pause key: vr/input/actions.cpp

// The aim by mouse, by turn, by gesture; the facing hook, the attack's point and target hooks: vr/input/aim.cpp

// The pad: the stick turned onto the game's screen (TurnStick), BodyWalk's pad mirror, the flat raw controls, the input log: vr/input/pad.cpp

// The game's actions from the pad: interact apart from attack, the item grab, ApplyActions, the interact target's hook: vr/input/actions.cpp

// Mouse look and W A S D, the chat line, the key move, the lean gait, BodyWalk's walking stick (HookKeyMove, HookStickGet): vr/input/walk.cpp

// The flat game's clicks: the map click's hook, the mouse on raised ground, the body under the pointer, the town portal's oval: vr/input/flat_click.cpp

// [render] floors_stay (the room heights' hook): vr/render/render_switches.cpp

// ... and the click itself (FlatPickClick, HookMapClick): vr/input/flat_click.cpp

// Spell aim, leap by gaze, the sewer's stairs, the slot's skill (the resolver's hooks): vr/input/spell_aim.cpp

// The pad's XInput detours (XDetour, KeysAsStick, the pad's presence, the point arc): vr/input/pad.cpp

// The game's asks of its video memory budget (budgetasks): vr/diag/budget_asks.cpp

// The game's window and the mouse (GameFocused, GameWndProc, the big window, the laser fit, MouseTick, FlatPickWatch): vr/hud/window.cpp

// [render] rings, model_radius, big_radius, the pools (ApplyRender, ApplyPools): vr/render/render_switches.cpp

void ToggleShooter() {
    g_mouseLookOn.store(!g_mouseLookOn.load());
    Log(g_mouseLookOn.load() ? "vrcam: mouse look and W A S D on (F9 = off: free pointer for menus)" : "vrcam: mouse look and W A S D off");
}

// Installs whatever is not in yet. D2R decrypts its code page by page as it
// first runs it, so a hook refused at load is tried again on the next F12.
HookState g_h{};   // (its type: vr/vrcam_internal.h)

// SkeletonInstance::ComputeSelfWorldPose (skeletons.cpp), D2RLoader layout.
constexpr uint64_t RVA_COMPUTE_SELF_WORLD_POSE = 0xF78740;
const uint8_t kSigComputeSelfWorldPose[16] = {0x48,0x8B,0xC4,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x89,0x68};

// BiomeSystem::SetCurrentBiome's hook (HookSetBiome): vr/world/area.cpp

// [render] background_full_speed (ApplyBackgroundSleep): vr/render/render_switches.cpp

// The game's draw of a frame, twice for a stereo pair (HookDrawGameScreen, NativePairBegin, XrIntoShared, PairTrace): vr/frame/frame_order.cpp

// Quiet: tried twice a second until the page is decrypted, logs only once it is in.
void InstallBiomeHook() {
    static bool failed = false;
    if (g_h.biome || failed || !g_ctx || !Matches(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome)) return;
    g_h.biome = d2rsig::Hook(RVA_SET_BIOME, kSigSetBiome, sizeof kSigSetBiome, (void*)&HookSetBiome, (void**)&OrigSetBiome);
    failed = !g_h.biome;
    Log(g_h.biome ? "vrcam: biome hook in - the sky knows the area" : "vrcam: biome hook FAILED - the sky stays off unless [sky] always=1");
}

// Item labels through the game's own code (the label hooks, labels_whole_eye): vr/hud/labels.cpp

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
            Log("vrcam: Scan pressed in D2R VR Authentic Settings - the game's addresses not found yet are looked for again");
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

// -- [render] vr_resolution_ui: the game's own client-size query (vr/vr_resolution.h CanvasAnswer, render_resolution.cpp) --------
// 3.3.93787: 0x6545D0 calls GetClientRect on the game's window and returns {width, height} in rax. The callers that size the
// interface (7 return addresses) are given the whole game's picture (vr_resolution.h UiCanvas), the two of the cursor clamp in SetCursorPosition
// the whole picture (the width's call goes through GetWidth 0x7F510, which tail-jumps to the query: its return address is
// SetCursorPosition's); every other caller its own answer. Found by bearstonem/d2r-pcvr (MIT, vr/panel_input.cpp InstallCanvasSize).
constexpr uint64_t RVA_CLIENT_SIZE = 0x6545D0;
const uint8_t kSigClientSize[16] = {0x48,0x83,0xEC,0x38,0xE8,0x97,0xD1,0x2A,0x00,0x48,0x8D,0x54,0x24,0x20,0x48,0x8B};
constexpr uint64_t kClientSizeUiRets[7] = {0x843ED5, 0x844195, 0x8453A9, 0x846239, 0x14D7E4, 0x14F31D, 0x14F943};
constexpr uint64_t RVA_CURSOR_HEIGHT_RET = 0x7F4B2, RVA_CURSOR_WIDTH_RET = 0x14FB2A;
using ClientSizeFn = uint64_t (*)();
ClientSizeFn OrigClientSize = nullptr;
uintptr_t g_clientSizeUiRet[7] = {}, g_clientSizeCursorRet[2] = {};
std::atomic<bool> g_clientSizeOn{true};

uint64_t HookClientSize() {
    const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint64_t native = OrigClientSize();
    vrres::Asker asker = vrres::Asker::Other;
    if (from == g_clientSizeCursorRet[0] || from == g_clientSizeCursorRet[1]) asker = vrres::Asker::Cursor;
    else for (uintptr_t r : g_clientSizeUiRet) if (from == r) { asker = vrres::Asker::Ui; break; }
    return asker == vrres::Asker::Other ? native : vrres::ClientSizeAnswer(asker, native, g_clientSizeOn.load());
}
// The target of a relative CALL (E8) or JMP (E9) at `site` when it is one (0 otherwise).
uintptr_t BranchTarget(uintptr_t site, uint8_t op) {
    uint8_t b[5];
    if (!SafeRead(b, reinterpret_cast<const void*>(site), sizeof b) || b[0] != op) return 0;
    int32_t rel;
    memcpy(&rel, b + 1, 4);
    return site + 5 + (intptr_t)rel;
}
// Live (`on`, the ini key); the hook goes in once, after every caller it answers has been checked to be a call of the query.
void InstallClientSize(bool on) {
    g_clientSizeOn.store(on);
    static bool in = false, failed = false;
    if (in || failed || !on) return;
    const uintptr_t fn = d2rsig::Addr(RVA_CLIENT_SIZE);
    if (!fn) return;   // (its code may still be encrypted: asked again)
    for (int i = 0; i < 7; ++i) {
        g_clientSizeUiRet[i] = d2rsig::Addr(kClientSizeUiRets[i]);
        if (!g_clientSizeUiRet[i]) return;
        if (BranchTarget(g_clientSizeUiRet[i] - 5, 0xE8) != fn) failed = true;
    }
    g_clientSizeCursorRet[0] = d2rsig::Addr(RVA_CURSOR_HEIGHT_RET);
    g_clientSizeCursorRet[1] = d2rsig::Addr(RVA_CURSOR_WIDTH_RET);
    if (!failed && (!g_clientSizeCursorRet[0] || !g_clientSizeCursorRet[1])) return;
    if (!failed && BranchTarget(g_clientSizeCursorRet[0] - 5, 0xE8) != fn) failed = true;
    if (!failed) {   // SetCursorPosition's call of GetWidth, and GetWidth's tail jump (17 bytes in, 3.3.93787) to the query
        const uintptr_t getWidth = BranchTarget(g_clientSizeCursorRet[1] - 5, 0xE8);
        if (!getWidth || BranchTarget(getWidth + 0x11, 0xE9) != fn) failed = true;
    }
    if (failed) {
        Log("vrcam: render size - the game's client-size callers are not what 3.3.93787 has: its interface and mouse keep the window's size "
            "([render] vr_resolution_ui does nothing)");
        return;
    }
    in = d2rsig::Hook(RVA_CLIENT_SIZE, kSigClientSize, sizeof kSigClientSize, (void*)&HookClientSize, (void**)&OrigClientSize);
    if (!in) {
        failed = true;
        Log("vrcam: render size - the game's client-size query could not be hooked: its interface and mouse keep the window's size");
        return;
    }
    Log("vrcam: render size - the game's client-size query is in: with a render size its interface and mouse follow the picture ([render] vr_resolution_ui)");
    vrres::ClientSizeReflow();   // a size already in force: laid out once more
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

// The views (ApplyView, SetView, Toggle, F11's Recenter) and the turn (TurnTick): vr/view/views.cpp
ConsoleCommandResult __cdecl CmdVrcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept;   // the console's "vrcam" (views.cpp)

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

// Where the hero's model faces (the matrix search, FacingThread, PickHeroMatrix, FreshYawInModel), the body under the camera: vr/body/hero_facing.cpp

// The head in BodyWalk's room and the hands for the skeleton hook (HeadInRoom, FreshHands): vr/body/arms.cpp

// The hero's animation mode, the flat portal's frame, the attacks' ticks (BowLatencyTick, the press trace, AnimCutTick, ThrowTraceTick, MeleeTouchTick): vr/combat/combat_ticks.cpp

// The hero's arms: skel::Input from the head, the hands, the grips, the weight (PushArms): vr/body/arms.cpp

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

// The area's state SkyTick sets (g_skyPalette, g_inWorld, g_underground, g_ceilBiome, ...): vr/sky/sky_tick.cpp

// The game's day and night (env, DayNightK / DayNightMul): vr/sky/day_night.cpp

// The game's height and depth fog taken out (gamefog, [render] game_height_fog / game_depth_fog): vr/sky/game_fog.cpp

// [fog] game_fog: the game's own distance fog with our numbers (gamefogvr): vr/sky/game_fog_vr_pass.cpp

// The light in caves and dungeons ([cave_light], cavelight): vr/sky/lights.cpp
// Units the game draws as its own (engitem: the belt, held items, the sky and cave domes, the ceiling's slab, the weather): vr/engine/engitem.cpp

// [render] solid_walls, keep_roofs, [stereo] props_once_a_pair (solidwalls, roofhook, prophook): vr/render/render_switches.cpp

// The lights near the hero from the game's objects (worldobj): vr/sky/lights.cpp

// [render] depth_fix_mode 2: the depth range at its source (depthsrc): vr/render/depth/depth_fix.cpp

// The area judged on the timer: in world, underground, the act, the ceiling biome, the sky and why (SkyTick): vr/sky/sky_tick.cpp

// Our effect (fx: joining ReShade, DLSS's eyes, the interface's uniforms, PresentCommon, no_reshade, EffectsBegin, TryRegister): vr/render/effects/effects.cpp

// The 1 ms update thread (UpdateThread): vr/frame/frame_order.cpp

}  // namespace vrc

// For xr.cpp (this file's own namespace is not seen from there): the balls over the belt's bottles.
int D2rvrHoverSpheres(float (*out)[8], int max) { return engitem::HoverSpheres(out, max); }

// ReShade's loading (LoadReShade, LoadOurAddonForDxgiReShade): vr/plugin/reshade_load.cpp

static const char g_info_version[] = "0.192.0";

// Who this is. D2RLoader prints "<name> <version> by <author>" for every plugin, and d2r-pcvr (a fork of our 0.152) carries the
// same id, name and author as we did - "vrcam ... by BodyWalkVR" told a support log nothing. The id stays (the log file
// d2rloader\logs\d2r-vr-vrcam.log, the plugin's settings and its place in D2RLoader's records are keyed by it); the name is what
// is shown, so it says D2R VR. The same facts are in the DLL's VERSIONINFO (vr/vrcam.rc) for a launcher to read without loading it.
static const char g_info_id[] = "d2r-vr-vrcam";
static const char g_info_name[] = "D2R VR";
#if defined(D2RVR_FIRST_PERSON) && !D2RVR_FIRST_PERSON
static const char g_info_edition[] = "public";
#else
static const char g_info_edition[] = "full";
#endif

static const PluginInfo g_info = {
    PluginInfoSize, D2RL_PLUGIN_ABI_VERSION, g_info_id, g_info_name, g_info_version, "natpoh",
    "D2R in a headset or as a first-person game: head-driven camera through BodyWalk Game Link, mouse look and W A S D. "
    "F12 off / first / third person, F11 recenter, F9 mouse look.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

// What the shared VR mod launcher gave this start (docs/launcher_authentic.md): VRMOD_ACTIVE_ID / _VERSION, VRMOD_RUNTIME, VRMOD_STORE,
// VRMOD_GAME_BUILD in the game's environment. "" for a game started the old way.
static std::string LauncherEnv(const char* name) {
    char buf[300];
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
    return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
}
static std::string JsonText(const std::string& s) {
    std::string o = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c < 0x20) o += ' ';
        else o += static_cast<char>(c);
    }
    return o + "\"";
}
// One log line saying which mod this is and who started it, and d2r_vr_about.json beside d2r_vr.ini: the same, for the launcher's
// report and for support. (A D2RLoader mod's own folder is the plugin's: the launcher never swaps this file.)
static void ReportIdentity(const PluginContext* ctx) {
    const std::string byId = LauncherEnv("VRMOD_ACTIVE_ID"), byVer = LauncherEnv("VRMOD_ACTIVE_VERSION"), runtime = LauncherEnv("VRMOD_RUNTIME"),
                      store = LauncherEnv("VRMOD_STORE"), xr = LauncherEnv("XR_RUNTIME_JSON");
    // as the D2RLoader mod D2R-VR (<game>\mods\D2R-VR\d2rloader\plugins) or as the global plugin of an install from before the mod layout
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, g_iniPath);
    if (wchar_t* s = wcsrchr(dir, L'\\')) s[1] = 0;
    const bool asMod = !modlayout::ModRootOfPluginDir(dir).empty();
    const char* layout = asMod ? "D2RLoader mod D2R-VR" : "global plugin";
    if (byId.empty())
        LogF("vrcam: mod d2r-vr %s (%s edition, %s), started without the VR mod launcher", g_info_version, g_info_edition, layout);
    else
        LogF("vrcam: mod d2r-vr %s (%s edition, %s), started by the VR mod launcher as %s %s - runtime %s, store %s%s%s", g_info_version, g_info_edition,
             layout, byId.c_str(), byVer.c_str(), runtime.empty() ? "?" : runtime.c_str(), store.empty() ? "?" : store.c_str(),
             xr.empty() ? "" : ", XR_RUNTIME_JSON ", xr.c_str());
    if (!byId.empty() && byId != "d2r-vr")
        LogF("vrcam: WARNING the launcher says %s is the active mod, but this plugin is d2r-vr - the game folder holds a mix of two mods", byId.c_str());

    std::string j = "{\n  \"schema\": \"vrmod-about/1\",\n  \"mod\": \"d2r-vr\",\n  \"name\": \"D2R VR\",\n";
    j += "  \"version\": " + JsonText(g_info_version) + ",\n  \"edition\": " + JsonText(g_info_edition) + ",\n";
    j += "  \"layout\": " + JsonText(asMod ? "d2rloader-mod" : "global-plugin") + ",\n";
    j += "  \"plugin_id\": " + JsonText(g_info_id) + ",\n  \"built\": " + JsonText(std::string(__DATE__) + " " + __TIME__) + ",\n";
    j += "  \"game_build\": " + JsonText(ctx && ctx->buildVersion ? ctx->buildVersion : "") + ",\n";
    j += "  \"pid\": " + std::to_string(GetCurrentProcessId()) + ",\n";
    j += "  \"launcher\": {\"active_id\": " + JsonText(byId) + ", \"active_version\": " + JsonText(byVer) + ", \"runtime\": " + JsonText(runtime) +
         ", \"store\": " + JsonText(store) + "}\n}\n";
    wchar_t path[MAX_PATH];
    wcscpy_s(path, dir);
    wcscat_s(path, L"d2r_vr_about.json");
    const HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;   // a read-only folder is not a reason to complain
    DWORD written = 0;
    WriteFile(f, j.data(), static_cast<DWORD>(j.size()), &written, nullptr);
    CloseHandle(f);
}

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
    gpufault::Init(&Log, g_iniPath);   // the D3D12 device lost (2026-10-17: three games in a day): DRED has to be asked for before the game makes its device, so here
    gpufault::EnableDred(GetPrivateProfileIntW(L"debug", L"dred", 1, g_iniPath) != 0);   // [debug] dred, on unless the ini says 0
    gpufault::EnableValidation((int)GetPrivateProfileIntW(L"debug", L"d3d12_validation", 0, g_iniPath));   // [debug] d3d12_validation: the debug layer's errors in the log (off unless asked)
    viewgate::Set(&FullBody);   // before any module that asks: the new behaviours (direct input, gestures, panels, pulses, hooks) act in F4 only
    viewgate::SetFlat(&FlatViews);   // ... except the left stick's walk in VR F1-F3 ([direct] walk_flat_views, vr/walk_flat.h): movement only
    directinput::SetFlatGate(&FlatControllerGate);   // ... and the plugin's own controller as the game's pad there while BodyWalk is not running ([direct] flat_controller)
    directinput::SetPressSink(&FlatPressSink);   // ... with it off, a control pressed there that nothing of the plugin takes is said in the log
    directinput::SetBeltSink(&BeltFlickSink);   // ... and the belt potions of a thumbstick's flick with the left trigger held ([direct] belt_flick_flat_views, vr/belt_flick.h)
    crash::Step("settings (d2r_vr.ini)");
    xr::SetLogger(&Log);   // before the settings: [openxr] on says so in the log
    monitoreye::SetLogger(&Log);
    xr::SetPointerAt(&PointerAtPixel);   // F1 / F3 in 3D: the game's pointer drawn where its mouse points on their ground
    calib::SetLogger(&Log);
    firststart::SetLogger(&Log);
    standstill::SetLogger(&Log);
    moveattack::SetLogger(&Log);
    straywalk::SetLogger(&Log);
    shottrace::SetLogger(&Log);
    straywalk::SetAttackFn(&standstill::AttackActive);
    calib::RegisterInput();   // the T-pose measurement takes both triggers from the input layer while it runs
    firststart::RegisterInput();   // the first-start guide reads the right stick's up and down (it takes nothing)
    ReloadIfChanged();
    NoTemporalAA();
    MiniMapForHand();
    ReconcileGameWindow();   // flat: no square window left from VR; VR: the player's own window kept first (before SafeGameWindow's cut)
    ReconcileFrameCap();     // flat: the player's own frame cap back from VR's rate; VR: his own kept
    if (ReconcileSafeScreen()) hud::SetInterfaceScale(GameInterfaceScale());   // flat: the player's own interface size back; VR: 80 where he chose none (the toolbar's cut follows the file)
    SafeGameWindow();
    g_mouseLookOn.store(MouseLookForView());
    LogF("vrcam %s: game build %s (%s), made for D2R 3.3.93787 under D2RLoader 1.3.1", g_info_version,
         ctx->buildVersion ? ctx->buildVersion : "?", ctx->buildName ? ctx->buildName : "?");
    ReportIdentity(ctx);
    // Every game address this build has, before any hook rewrites the bytes it is found by.
    crash::Step("game addresses (signatures)");
    d2rsig::SetShiftTest(GetPrivateProfileIntW(L"debug", L"sig_shift_test", 0, g_iniPath));
    d2rsig::Resolve(ctx);

    crash::Step("ReShade (ReShade64.dll and its add-ons, FlatVR's depth add-on among them)");
    LoadReShade();
    // The game makes its pipelines at startup and ReShade does not replay init_pipeline for those made before an
    // add-on joined: the light culling's replacement (the game's light-bin shader, light_bounds.h) joins now,
    // not with the rest on the update thread's first tick.
    if (!g_noReShade && lightbounds::Wanted() && fx::JoinReShade()) lightbounds::Register(&Log);
    if (!g_noReShade && fogspare::Wanted() && fx::JoinReShade()) fogspare::Register(&Log);   // (the same: the game's resolve pipeline is made at its start)
    if (!g_noReShade && fogframe::Wanted() && fx::JoinReShade()) fogframe::Register(&Log);   // ([render] fog_in_frame: the same pipeline)
    crash::Step("camera callbacks, MinHook, the hero's skeleton");
    d2rcam::SetCallbacks(&VrView, &VrProj);
    d2rcam::SetFrameCallback(&VrFrame);
    d2rcam::SetRayOverride(&VrRay);
    MH_Initialize();   // XInput only; game code is hooked through the loader
    if (g_noReShade) {   // [render] no_reshade: D3D12 and DXGI hooked by the plugin, before the game makes its device and swap chain
        crash::Step("no ReShade: the plugin's own D3D12 / DXGI hooks");
        fx::noreshade::Install();
    }
    skel::SetGameBase(g_base);
    skel::TrackHero(true);   // the hero's SkeletonInstance is how his facing is found (FacingStructural)
    skel::SetFresh(&FreshYawInModel);
    skel::SetFreshRoomShift(&FreshRoomShift);
    skel::SetFreshHands(&FreshHands);
    weight::SetLog(&Log);
    grabwire::SetLog(&Log);                  // [weight] grab_bodies
    skel::SetGrabHands(&GrabHandsStep);      // [weight] grab_items, then grab_bodies
    wriststatus::SetHandShift(&grabwire::DrawnShift);   // [hud] wrist_status: the panel rides a hand drawn on a body
    weightlib::SetLog(&Log);
    weight::SetLibrarySource(&weightlib::Ensure);
    weightlib::Probe();   // is d2r_vr_weight.dll beside the plugin? (told in the log; it is loaded when [weight] hands_model 1 first asks)
    griplib::SetLog(&Log);
    gaitlib::SetLog(&Log);   // [multiplayer] pose_share_legs: the gait library, loaded when first asked
    weight::SetGripLibrarySource(&griplib::Ensure);
#if D2RVR_FIRST_PERSON
    weight::SetBodiesSource(&fx::BodiesWithPlayers);   // ... and the other players' bodies and weapons ([multiplayer] pose_share_bodies; none unless asked)
#else
    weight::SetBodiesSource(&meleetouch::Bodies);
#endif   // [weight] monster_bodies: the hostile monsters' capsules from the touch's scan
    weight::SetStabSource(&meleetouch::StabbingNow);   // [weight] stab_in: the touch's thrust rule and path say when a point-first strike goes in
    weight::SetStabNoteSink(&fx::StabNoteSink);       // [debug] stab_debug: the stab's word for the readout and the marks
    weight::SetBodyPushSink(&HealthPushBody);   // meleetouch::PushBody, counted for [debug] health
    skel::SetMonsterPoseByWorldSource(&HealthMonsterPosesForSkin);   // [weight] monster_ragdoll_pose: the skinning draws a struck monster from a turned copy (meleetouch::MonsterPosesForSkin, counted)
    skel::SetMonsterPoseSource(&HealthMonsterPose);   // (meleetouch::MonsterPose, counted) [weight] monster_ragdoll_pose: the hook turns a struck monster's bones   // [weight] monster_ragdoll: ... and the weapon's pushes back to them
    skel::SetGripHandStep(&griplib::HandStep, &griplib::Abi);
    dolllib::SetLog(&Log);
    dolllib::Probe();   // [weight] monster_ragdoll: is d2r_vr_weight.dll (its D2rd* exports) in d2r_vr_libs? (told in the log; loaded when monster_ragdoll 1 first asks)
    griplib::Probe();   // is d2r_vr_grip.dll beside the plugin? (told in the log; it is loaded when [weight] grip_model 1 first asks)
    // [weight] with [hands] throw_by_grip: a grip release that is a throw does not drop a thrown weapon (vr/weight_throw.h). The throw gesture
    // (claude/throw-grip) exports throwwire::GripIsThrows() and throwwire::LastThrowTickMs() (vr/throw_gesture_wire.h); once its branch is in the
    // tree: registered here. Unregistered, every release is a drop.
    weight::SetThrowSource(&throwwire::GripIsThrows, &throwwire::LastThrowTickMs);
    weight::SetThrowSide([] { return throwhand::SideOfXr(throwwire::Controller()); });   // [hands] throw_left_hand: the hand whose grip the throw is (0 right, 1 left)
    {   // ... and the throw follows each hand that carries a thrown weapon in VR (the first weapon, the second of a weapon in each hand)
        throwwire::CarrySource cs;
        cs.placed = &weight::Placed;
        cs.carrying = &weight::CarryingHand;
        cs.secondHand = &weight::SecondHand;
        cs.secondPlaced = &weight::SecondPlaced;
        cs.firstSlot = &weight::FirstSlot;
        cs.holdersOf = &weight::HoldersOf;
        cs.secondThrown = &weight::SecondThrown;
        throwwire::SetCarrySource(cs);
    }
    // The weight model's own account of a throw (weight::LastThrow / weight::ThrowOutcome - the heading, a potion's landing, already scaled by [weight]
    // throw_reach_pct) is the throw gesture's heading and potion distance when it is valid and belongs to the throw (vr/throw_source.h). Unregistered,
    // the hand's own estimate decides (the test-14 log said "SOURCE hand estimate" for every throw: this registration had been left out of the merge).
    throwwire::SetWeightSource([](throwsrc::Outcome* o) {
        weight::ThrowOutcome w;
        const bool ok = weight::LastThrow(&w);
        *o = throwsrc::Outcome();
        o->valid = ok && w.valid; o->preview = w.preview; o->tickMs = w.tickMs; o->releaseTickMs = w.releaseTickMs;
        for (int i = 0; i < 3; ++i) { o->velocity[i] = w.velocity[i]; o->landing[i] = w.landing[i]; }
        o->speed = w.speed; o->flightS = w.flightS; o->elevationDeg = w.elevationDeg; o->capped = w.capped;
        return o->valid;
    });
    crash::Step("the game's hooks");
    InstallHooks();
    crash::Step("XInput hooks");
    HookXInput();
    crash::Step("shared memory (Game Link, FlatVR)");
    link::Init(&Log, g_iniPath, g_info_version);   // the game's actions and tabs to BodyWalk, the head and hands back
    OpenAfrBlock();
    crash::Step("game state");
    gamestate::Init(ctx);
    crash::Step("game commands");
    gamecmd::Init(ctx);
    beltdrop::Init(ctx);   // [hands] belt_drop_ground
    beltglow::Init(ctx);   // [hands] belt_inner_glow
#if D2RVR_FIRST_PERSON
    if (posewire::Init(ctx, &Log, IniB(L"multiplayer", L"pose_share", true), "vrcam: pose share")) PoseShareConfigure();   // [multiplayer] pose_share: the channel now or never this run
#endif
    shoulderwire::Init(ctx);   // the shoulder swap listens to direct_input's frames
    weightwire::Init(ctx);     // [weight]: so does the grip of a hand on the weapon
    owneffects::Init(ctx);     // [render] dim_own_effects: the hero's own effects fainter (its two hooks only look, so they are in whether it is ticked or not)
    owneffects::SetHeroSource(+[]() -> uintptr_t { return (uintptr_t)g_heroUnit.load(); }, &GestureHeroId);   // the hero as the aim function knows him, besides the game's own getter
    owneffects::SetScaleSource(+[]() -> float { return std::max(1.0f, ViewHeight()) / UserEyeHeightM(); });   // [render] lower_own_effects: cm as world units, as the view's own scale
    belttrigger::Init(ctx);    // [belt] hold_takes_trigger: and the triggers of a hand that holds a belt bottle
    shoulderwire::SetWeaponSource([]() -> bool { return weight::Placed() || (g_set.shieldStow.load() && shieldarmwire::Arm() >= 0) || rangedwire::Side() >= 0; });   // [hands] shoulder_draw claims the grip behind a shoulder only while the weight model has a weapon or the shield can be put away
    shieldarmwire::SetHaptic([](int side, float amplitude, int ms) { haptics::Pulse(haptics::FromSkel(side), amplitude, ms); });   // the left shoulder, with the shield put away
    shieldarmwire::SetLog(&Log);
    stowedblock::SetLog(&Log);
    spatialaudio::SetLog(&Log);
    soundtrace::SetLog(&Log);
    parrywire::SetLog(&Log);   // [weight] parry_*
    clashsound::SetLog(&Log);   // [weight] clash_sound
    clashsound::Init(ctx);
    clashsound::SetHero(&HeroUnitForSound);
    weight::SetClashSound([](float speed, uint32_t volume, const char* who) { clashsound::Play(speed, volume, who); });
    spatialaudio::SetViewYaw(+[]() { return g_camYaw.load(); });
    spatialaudio::Install(false);   // now: the game sets FMOD's listener up once, early, and that call hands over its sound system
    rangedwire::SetLog(&Log);
    rangedwire::SetHaptic([](int side, float amplitude, int ms) { haptics::Pulse(haptics::FromSkel(side), amplitude, ms); });
    shieldarmwire::SetWielderSource(&weight::WieldingHands);   // [hands] shoulder_draw, the shield: weapon and shield are always on opposite arms
    // the hand a landed blow buzzes: the controller that ASKED for it (vr/hit_claim.h: hit by touch says which hand's blade, the shield bash which arm wears the shield - the left one unless
    // [hands] shoulder_draw moved it, with it on or off), else the hand that holds the weapon (a weapon drawn into the left hand buzzes the left). The press says it, not a cast on its way:
    // the game's hit lands 24 - 253 ms after the press, when the cast is long over.
    haptics::SetHitHandSource([]() -> int {
        return haptics::HitHand(hitclaim::Take((int64_t)GetTickCount64()), weight::CarryingHand(), weight::Placed());
    });
    shoulderwire::SetStowedSource(&weight::BackStowed);   // [weight] after_drop 1 or 2: a weapon that is away is taken by the grip behind the shoulder, not swapped
    weight::SetHaptic([](int side, float amplitude, int ms) { haptics::Pulse(haptics::FromSkel(side), amplitude, ms); });   // [weight] take_haptic
    weight::SetHeroModeSource(&HeroAnimMode);   // [weight] thrown_fly_ms: the game's own throw animation over ends the thrown model's flight
    weight::SetActionFrameSource(&throwborn::ActionFramePassed);   // [hands] throw_direct: the thrown potion's model hands over to the game's missile when it is created
    shieldsmite::Init(ctx);
    meleetrigger::Init(ctx);   // melee_touch_only listens to direct_input's frames
    doubleswing::Init(ctx);
    meleearm::Init(ctx);   // [gameplay] melee_skill_armed: the log
    meleetouch::Init(ctx);
    meleedirectclient::InitNet(ctx, IniB(L"gameplay", L"melee_direct_client_cleave", false) &&
                                        (IniF(L"gameplay", L"melee_direct_client", 0.0f) >= 0.5f || IniF(L"gameplay", L"melee_direct", 0.0f) >= 1.5f));   // the channel now or never this run
    skillwheel::Init(ctx);
    lowhp::Init(ctx);   // [hud] low_hp_vignette reads the hero's life on the UI thread
    merctint::Init(ctx);   // [hud] merc_tint reads the merc's life on the UI thread
    handvitals::Init(ctx);   // [hud] hand_vitals reads the hero's life and mana on the UI thread; listens to direct_input's frames
    thrustdebugwire::Init(ctx);   // [debug] thrust_debug: the thrust's readout over the weapon hand; listens to direct_input's frames
    wriststatus::Init(ctx);   // [hud] wrist_status reads the hero's pets and states on the game's thread; listens to direct_input's frames
    panelinput::Init(ctx);
    bowdrawwire::Init(ctx, &AimHand, &FullBody);   // the bow draw listens to direct_input's frames, in F4 only
    bowpitchwire::Init(ctx);   // [bow] pitch_aim
    pointwire::Init(ctx);   // [gameplay] teleport_circle
    arrowflightwire::Init(ctx);   // [bow] arrow_flight: on the potion hide's two renderer hooks
    arrowflightwire::SetHeroIdSource(&GestureHeroId);   // ... whose arrows are the hero's
    bowquiverwire::Init(ctx, &AimHand);            // [bow] quiver rides on the draw's frames (after its listener)
    bowdrawwire::SetReleaseHook(&bowquiverwire::OnRelease);   // [bow] quiver: the quiver decides, before the release's button goes down, whether it is the armed skill or the plain arrow
    bowdrawwire::SetStartBlock([]() -> bool { return bowquiverwire::BlocksDraw() || rangedwire::Stowed(); });   // [bow] quiver_orbs: the free hand at an orb does not take the string; a bow put away takes none
    animcut::Init(ctx);   // [bow] quick_release, [hands] quick_throw: the log
    throwwire::Init(ctx, &GestureCanAim);
    quiverwire::Init(ctx);   // [hands] javelin_quiver: the log
    throwwire::SetHeroModeFn(&GestureHeroMode);
    potionhide::SetHeroIdSource(&GestureHeroId);   // [hands] potion_hide_game: whose potion missile is the hero's
    headshadow::Init(ctx);   // [arms] head_shadow: the log
#if D2RVR_FIRST_PERSON
    potionhide::SetXformObserver(&skel::XbowRendered);   // [hands] xbow_support_model: the crossbow as the renderer places it (reads only)
#endif
    throwwire::SetAutoSkillFn(&ThrowAutoSkillReady);
    meleetouch::SetAnimNote(&MeleeAnimNote);   // [debug] bow_latency: the animation row on the melee press->hit line
    meleetouch::SetPressTrace(&PressTraceBegin, &PressTraceOutcome);   // [debug] melee_press_trace: one line per attack press of hit by touch
    meleetouch::SetThrowLink(&throwwire::GripIsThrows, &throwwire::GripHeld, &throwwire::LastThrowTickMs, &throwwire::HoldOffRelease);   // hit by touch with a thrown weapon: the touch and the throw by gesture do not both fire
    meleetouch::SetThrowGripAt([](int side) { return throwwire::FollowHand() ? throwwire::GripIsThrowsAt(throwhand::XrOfSide(side)) : throwwire::GripIsThrows(); },
                               [](int side) { return throwwire::FollowHand() ? throwwire::GripHeldAt(throwhand::XrOfSide(side)) : throwwire::GripHeld(); });   // ... asked of the hand that swings it (a javelin in each hand; unticked the right grip, as ever)
    standstill::SetThrowPressFn(&throwwire::PressIsThrow);
    crash::Step("update thread and console command");
    if (HANDLE h = CreateThread(nullptr, 0, UpdateThread, nullptr, 0, &g_updateThreadId)) CloseHandle(h);
    if (!ctx->RegisterConsoleCommand("vrcam", &CmdVrcam, "vrcam - the camera: off / first person / third person (F12)"))
        ctx->LogError("vrcam: the console command did not register");
    crash::Step(nullptr);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    animcut::Shutdown();   // [bow] quick_release, [hands] quick_throw: a moved action-frame flag goes back before the plugin does
    skillwheel::Shutdown();   // [openxr] skill_wheel: its listener out, its threads joined
    handvitals::Shutdown();   // [hud] hand_vitals: its listener out, its painter joined
    thrustdebugwire::Shutdown();   // [debug] thrust_debug: its listener out, its painter joined
    wriststatus::Shutdown();   // [hud] wrist_status: its listener out, its painter joined
    xr::Shutdown();   // the session before anything it runs on goes; FlatVR started again
    g_enabled.store(false);
    d2rcam::SetEnabled(false);
    if (fx::g_joined) { reshade::unregister_addon(g_self); fx::g_joined = false; fx::g_registered = false; }
    if (g_gameWnd && g_gameWndProc) SetWindowLongPtrW(g_gameWnd, GWLP_WNDPROC, (LONG_PTR)g_gameWndProc);
    if (g_updateThreadId) PostThreadMessageW(g_updateThreadId, WM_QUIT, 0, 0);
    crash::Uninstall();
    posewire::Shutdown();   // [multiplayer] pose_share: no more network jobs, the channel given back
    meleedirectclient::ShutdownNet();   // [gameplay] melee_direct_client_cleave: the channel given back
    logasync::Shutdown();   // [debug] log_async: the lines that wait are written, its thread joined, before the context goes
    g_ctx = nullptr;
}
