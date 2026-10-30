// vrcam_internal.h - what the parts of the VR layer share: vr/vrcam.cpp and the modules cut from it into vr/<area>/
// (docs/plan_modules.md). The names are the namespace vrc's, defined in vrcam.cpp; a module adds here only what it takes from
// there, and its own header says what it gives. Not for any other file of the plugin (they talk to vrcam through their own headers).
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "game_ceiling.h"   // gameceiling::kHeightAlone
#include "mat4.h"           // d2rcam::m4::V3

#include <D2RLPlugin/api.h>

struct BWPadMirror;   // (bodywalk/pad_mirror_shared.h)
struct D2RVR_Shared;  // (d2r_vr_shared.h)
struct FlatVRAfrEye;  // (flatvr/afr_eye_shared.h)
namespace xr { struct Eyes; }   // (vr/xr.h)
namespace health { struct Sky; }   // (vr/health.h)

namespace vrc {

using d2rcam::m4::V3;   // (cleanroom/camera/mat4.h)

// A camera as the sky pass takes it (used by several parts: the spell aim, the effect, the replay).
struct SkyView { float proj[4]; float axes[9]; float eyeRel[3]; float hero[3]; bool proj_ok, axes_ok; };

// The host, the game's image, this DLL (vrcam.cpp, set at the plugin's load).
extern const D2RL::PluginContext* g_ctx;
extern uintptr_t g_base;
extern HMODULE g_self;

// Memory that may not be there: false when the copy faulted.
bool SafeRead(void* d, const void* s, size_t n) noexcept;
bool SafeWriteMem(void* d, const void* s, size_t n) noexcept;
// The game's bytes at rva are the ones expected (cleanroom/sigscan).
bool Matches(uint64_t rva, const uint8_t* sig, size_t n);

// The plugin's log (d2r-vr-vrcam.log, through [debug] log_async when on); Say: to the game's console too.
void Log(const char* text);
void LogF(const char* fmt, ...);
void Say(const char* text);

// d2r_vr.ini beside the DLL; a number or a switch of it (the default when missing or not a number).
extern wchar_t g_iniPath[MAX_PATH];
float IniF(const wchar_t* sec, const wchar_t* key, float def);
bool IniB(const wchar_t* sec, const wchar_t* key, bool def);

// The eye being drawn now: 0 left, 1 right.
extern std::atomic<int> g_eye;
// The camera on (F12 / the Home page's view), the hero in a game area.
extern std::atomic<bool> g_enabled;
extern std::atomic<bool> g_inWorld;

// ---- what vr/config/load_settings.cpp (M2) sets and calls in vrcam.cpp ----
// (moved here from vrcam.cpp: the declarations it had above LoadSettings and at the namespace's top)
namespace depthsrc { void Configure(bool on); void Tick(); float LastFar(); }   // [render] depth_fix_mode 2 (further down, beside SkyTick)
namespace gamefog { void SetPoke(const std::wstring& want); }   // [debug] fog_poke, below
namespace cavelight { void Load(); }   // [cave_light], below
namespace engitem {   // [multiplayer] pose_share_held (below): what our hands show (the render thread's draws), and the other players' copies to draw
struct Shown { uint32_t code = 0; uint8_t state = 0; float m[16] = {}; ULONGLONG at = 0; uint64_t frame = 0; };
bool ShownIn(int hand, Shown* out);
struct RemoteCopy { uint32_t code = 0; float m[16] = {}; };
void SetRemoteCopies(const RemoteCopy* c, int n);
unsigned TakeRemoteCopiesDrawn();
}
namespace engitem { bool DomeShown(); float DomeRadius(); unsigned DomeIndexCount(); const char* SkyCannot(); }   // [sky] game_sky: the game draws our sky this frame (the post pass's stands aside)
namespace fx { namespace noreshade { bool FxWaiting(); } }   // [render] native_effects: our effect not drawing yet (its build for this size not there) - the game's dome meanwhile
namespace engitem { bool CeilingShown(); bool CeilingByGame(); }   // [ceiling] game_ceiling: the game draws the cave ceiling this frame; the ceiling is the game's here (the post pass's stays off)
namespace fx { bool CeilWanted(); bool GameCeilWanted(); }   // (a ceiling wanted here: [ceiling] enabled, first person, a ceiling biome / a natural cave, no menu)
namespace engitem { void SetWant(const std::wstring& path); void SetBelt(bool on, float up, float ahead, float arc, float scale, float side, float across, float turn, float grabCm, bool grab, const float hold[6]); void SetBallColours(const float (*rgb)[3]); void Frame(bool fullBody); void InstallHook(); float FistCap(int side); bool BeltBusy(int side); }   // [debug] engine_unit, [hands] belt_models, below
void StartMatrixWatch();   // [debug] matrix_writer, below PickHeroMatrix
namespace drawprof { extern std::atomic<bool> g_want; }
namespace cbcmp { extern std::atomic<bool> g_go; void NoteView(int eye, const float v[16]); }
namespace rtrace { extern std::atomic<bool> g_go; }
extern std::atomic<int> g_inputLog;
extern std::atomic<bool> g_xrThirdGaze; extern std::atomic<int> g_projFix; extern std::atomic<float> g_shiftMul; extern std::atomic<int> g_replayViews; extern std::atomic<int> g_viewMask; extern std::atomic<int> g_replayPositions; extern std::atomic<int> g_replayPrev;
namespace fx { bool OwnDepthNow(); }   // [render] own_depth in force (fx::scenedepth::)
namespace fx { extern std::atomic<int> g_fifPaint; }   // [debug] fog_in_frame_paint (with the effect, below)
namespace replay { extern std::atomic<int> g_marksOn; extern std::atomic<bool> g_fxSync; extern std::atomic<bool> g_fxFence; extern std::atomic<bool> g_dlssBoth; extern std::atomic<bool> g_replayCopy; }
namespace replay { extern std::atomic<int> g_peekWant; }
namespace replay { void AreaChanged(); }   // the game set a biome: an area loads (see Wanted)
namespace camfix { extern std::atomic<int> g_scanWant; }
namespace flog { void StartDiag(); }
namespace replay { extern std::atomic<bool> g_rightDepth; }
namespace replay { extern std::atomic<int> g_want; extern std::atomic<int> g_ahead; extern std::atomic<bool> g_depthCopy; extern std::atomic<bool> g_on; extern std::atomic<bool> g_fxAtHold; extern std::atomic<bool> g_holdSignalsOn; }
namespace replay { extern std::atomic<int> g_capturing; }   // 1 from a pair's left pass until the replay draws its right eye (vr/sky_view_eyes.h)
namespace replay { extern std::atomic<bool> g_rightReplayed; }   // this pair's right eye replayed from the left's lists (not a pass of its own)
// [ceiling] a room with no height of its own
constexpr float kCeilHeightAlone = gameceiling::kHeightAlone;
// (defined in vrcam.cpp)
extern std::atomic<bool> g_pressTraceKey;   // [debug] melee_press_trace
extern std::atomic<float> g_iconLiftMm;     // [gameplay] melee_skill_armed_icon_lift_mm
extern std::atomic<bool> g_ownDepth;        // [render] own_depth
extern bool g_noReShade;                    // [render] no_reshade, decided at the plugin's load
extern std::atomic<bool> g_squareWin;       // [openxr] square_window
void ftraceSetOverMs(float ms);             // [debug] frame_trace_ms
float GameInterfaceScale();
void LoadCeilingBiomes();
void LoadCeilingTextures();
void LoadSkyActs();
void LoadTableBackground();
void LoadCrosshairFile();
bool ParseHexColor(const wchar_t* text, float rgb[3]);
void PoseShareConfigure();

// ---- what vr/frame/frame_order.cpp (M17) calls in vrcam.cpp (the update thread's ticks and installs) ----
void HealthTick(ULONGLONG nowMs);   // [debug] health
void MouseTick();
void FlatPickWatch();
void FlatVr3DTick();
void AfrQuietWhenOff();
void ToggleShooter();
void SkyTick();
void MeleeTouchTick();
void DumpSkeletons();
void UpdateFovFromFlatVR();
void GameCodeTick(bool iniChanged);
void ApplyRender();
void ApplyPools();
void ApplyBackgroundSleep();
void HookGameWindow();
void InstallBiomeHook();
void InstallClientSize(bool on);
void InstallRoomHeightHook();
extern std::atomic<bool> g_renderDirty, g_bigDirty;
namespace roofhook { void Install(); void Tick(); }   // [render] keep_roofs
namespace prophook { void Install(); void Tick(); }   // [stereo] props_once_a_pair

// ---- what vr/view/*.cpp (M16) read of vrcam.cpp ----
// Installs whatever is not in yet (the hooks' state, InstallHooks)
struct HookState { bool camera, skeleton, target, facing, point, biome, blit, interact; };
extern HookState g_h;
bool AllHooksIn();
void InstallHooks();
void AfrNextFrame();
float CameraFov();
V3 Facing(float heading, float down);
extern std::atomic<float> g_mouseYaw, g_mousePitch;   // the mouse's turn on top of the head (deg)
extern std::atomic<float> g_turnYaw;
extern std::atomic<float> g_heldYaw, g_heldPitch, g_heldStickPitch;   // held for a pair
extern std::atomic<float> g_stickPitch;
extern std::atomic<float> g_recenter, g_recenterPitch, g_recenterRoll, g_recenterRoom;
extern std::atomic<bool> g_recenterRoomOk;
extern std::atomic<float> g_recenterPos[3];
extern std::atomic<bool> g_recenterPosOk;
extern std::atomic<float> g_bwBase;
extern std::atomic<bool> g_bwBaseOk;
extern std::atomic<float> g_topUnitsPerM;

// ---- what vr/render/effects/effects.cpp (M15) reads of vrcam.cpp: the sky's and the ceiling's pictures, the table, the eyes ----
struct SkyAct { float bright; std::string band, cap; bool bandOk, capOk; float fog[3]; bool fogSet;
                std::string nightBand, nightCap; bool nightOk; };   // [sky] night_<act> / night_cap_<act>: the night picture (gen_night_sky.py)
struct CaveFog { float rgb[3]; bool set; };
struct TableBox { float vp[16]; float hero[3]; bool ok; };
constexpr int kCeilSlots = 8;
extern const char* const kActDefs[7];
extern SRWLOCK g_skyCfgLock;           // the acts' skies, the caves' fog colour
extern SkyAct g_skyAct[7];
extern CaveFog g_caveFog;
extern std::atomic<uint32_t> g_skyCfgGen;
extern std::string g_ceilSlot[kCeilSlots + 1];
extern bool g_ceilSlotOk[kCeilSlots + 1];
extern std::atomic<bool> g_arcaneReady;
extern SkyView g_skyHist[2][4];
extern int g_skyHistAt[2];
extern SRWLOCK g_tableBgLock;
extern TableBox g_tableBox;
extern FlatVRAfrEye* g_afrBlock;
extern std::atomic<HWND> g_swapWnd;
extern std::atomic<float> g_viewRightY;
extern std::atomic<float> g_viewUpY;
float FogTopNow();
bool NativeEyes(xr::Eyes* out);
void Recenter();
struct CeilVaultCfg { bool dome = false; float domeRadius = 30.0f, domeMax = 30.0f, domeFind = 0.0f;
                      float texSize = 0.0f, reliefPic = 0.0f; };
struct SkyPalette { const char* part; float zenith[3], horizon[3], clouds, sun, sunElevDeg, sunAzDeg, stars; int act; };
extern const SkyPalette kPalettes[];   // (the acts' sky colours, vrcam.cpp)
extern float g_tableBg[3];   // [table] background (under g_tableBgLock)
extern uint32_t g_stampHist[2][4];
extern int g_stampHistAt[2];
extern health::Sky g_healthSky;   // [debug] health

// ---- what vr/render/stereo/*.cpp (M14) read of vrcam.cpp ----
bool PairWanted();
void SetEye(int eye);
void CommitRightSky();

// ---- what vr/render/depth/*.cpp (M13) read of vrcam.cpp ----
extern float g_leftView[16];                 // the left eye's view as last built (under g_leftViewLock)
extern SRWLOCK g_leftViewLock;
extern std::atomic<float> g_eyeHalf;
extern std::atomic<float> g_projShift;
extern std::atomic<uint32_t> g_nFold, g_nView, g_nPos;
bool LiveScreen(float* w, float* h, float* d);
bool TrueScale(float* ipd, float* conv);
bool NativeByTurns();
namespace replay {
extern std::atomic<uint32_t> g_depthBinds, g_depthKept;
extern uint64_t g_depthBound;
extern thread_local bool t_ours;       // our own signals pass
extern thread_local bool t_pairPass;   // inside a pair's effect pass
bool FxFence();
bool SkipWhileStuck(const char* what);
}  // namespace replay

// ---- what vr/body/*.cpp (M12) read of vrcam.cpp ----
constexpr uint64_t RVA_FRAME_TIME = 0x27D31D0, RVA_RAW_FRAME_TIME = 0x27D31D4;
extern std::atomic<uint32_t> g_gen;
extern std::atomic<uint32_t> g_viewBuilds;
extern std::atomic<bool> g_pairNow;
extern std::atomic<bool> g_facingReset;
extern std::atomic<float> g_heroScale;
extern std::atomic<float> g_eyeFromLook[3];
extern std::atomic<ULONGLONG> g_headDownAt;
extern std::atomic<float> g_headDownM;
float TargetYaw();
bool PairOfOneFrame();
int HeroAnimMode();

// ---- what vr/input/actions.cpp (M10) reads of vrcam.cpp ----
HWND GameWindow();

// ---- what vr/input/aim.cpp (M10) reads of vrcam.cpp ----
const D2RVR_Shared* Shared();   // BodyWalk's block
extern std::atomic<bool> g_aimRay;
extern std::atomic<ULONGLONG> g_aimRayUntil;
extern std::atomic<float> g_aimO[3], g_aimD[3];
extern bool g_aimDown;
extern int g_aimStage;
extern ULONGLONG g_aimStageAt;
void SendKeysMouse(bool down);
void SendLeftButton(bool down);

// ---- what vr/input/pad.cpp (M10) reads of vrcam.cpp ----
struct StickFrame { float camR[3], camF[3], scrR[3], scrU[3], mirX, mirY; float yawAtBuild; bool valid; };   // yawAtBuild: TargetYaw this view was built with
extern SRWLOCK g_stickLock;
extern StickFrame g_stickFrame;
extern std::atomic<float> g_camYaw;
extern std::atomic<float> g_rightX;
extern std::atomic<float> g_rightY;
extern std::atomic<float> g_gazeLook[3];
extern std::atomic<bool> g_aHeld;
extern std::atomic<bool> g_skillHeld;
extern std::atomic<ULONGLONG> g_padMirrorOkAt;
bool HeadYaw(float* yaw);
float WrapDeg(float a);
void PressTracePoll(uint16_t buttons);
void RoomStick(int16_t* lx, int16_t* ly);
void AimByMouse(XINPUT_STATE* s);
void AimByTurn(XINPUT_STATE* s);
bool BowStick(XINPUT_STATE* s);
void ApplyActions(XINPUT_STATE* s);

// ---- what vr/input/walk.cpp (M10) reads of vrcam.cpp ----
extern std::atomic<bool> g_bwStickLifted;
extern std::atomic<uint32_t> g_bwStickRaw;
extern std::atomic<uint64_t> g_walkRaw;
extern std::atomic<uint8_t> g_walkState;
bool GameFocused();
bool ViewStickToGame(float* px, float* py);

// ---- what vr/input/spell_aim.cpp (M10) reads of vrcam.cpp ----
struct Calib { double c1 = 0, s1 = 0, c2 = 0, s2 = 0; int n = 0; float angle = 0; int mirror = 0; bool ok = false; };
struct SpellCast { bool directed = false, point = false, leap = false; float gx = 0.0f, gy = 0.0f; };
constexpr uint64_t RVA_ATTACK_POINT = 0x18BAD0, RVA_ATTACK_POINT_RET = 0x14484A;   // the aim's own call returns there
using AttackPointFn = float* (*)(void* mgr, float* out, void* unit, uint64_t from, uint64_t dir);
using ClientIndexFn = int (*)(void* unit);
using HoverUnitFn = void* (*)(int index);
extern SRWLOCK g_skyLock;
extern SkyView g_skyView[2];
extern SRWLOCK g_calLock;
extern Calib g_cal;
extern thread_local SpellCast g_spellCast;   // the resolution under way on this thread
extern AttackPointFn OrigAttackPoint;
extern const uint8_t kSigClientIndex[15];
extern const uint8_t kSigHoverUnit[12];
extern std::atomic<ULONGLONG> g_gazeAt;
extern std::atomic<float> g_gazeEyeH;
extern std::atomic<float> g_gazeFwd[3];
extern std::atomic<bool> g_ptActive;   // [debug] melee_press_trace: a press is being traced
void PressTraceSlot(int site, int slot, int bound, int answer);
void PressTraceLookup(int which, void* unit, bool ours);
void PressTraceCast(int skill, bool directed);
bool HandGridDir(float* gx, float* gy);
extern uintptr_t g_spellPointRet;   // where the resolver's AttackPoint call returns
extern std::atomic<bool> g_hoverHooked;

bool AimByVector();
bool GestureAimLive();

// ---- what vr/hud/*.cpp (M9) reads of vrcam.cpp ----
constexpr uint64_t RVA_MAP_CLICK = 0xFE3B0, RVA_CLIENT_INDEX = 0x9A820, RVA_HOVER_UNIT = 0xF1900;
constexpr uint64_t RVA_SPELL_RESOLVE = 0x190440, RVA_SPELL_TARGET = 0x18DDE0, RVA_CTRL_HANDLER = 0x144640, RVA_CTRL_SKILL = 0x1440D0;
constexpr uint64_t RVA_SEWER_LOCAL_INDEX = 0x8B2D0, RVA_SEWER_CLIENT_PLAYER = 0x9A480, RVA_SEWER_REACH = 0x348650;
using MapClickFn = uint64_t (*)(void* unit, int type, int x, int y, uint64_t flags);
extern const uint8_t kSigMapClick[16];
extern MapClickFn OrigMapClick;
extern std::atomic<int> g_mapClick;   // 0 not in yet, 1 in, 2 not possible
extern HWND g_gameWnd;
extern std::atomic<bool> g_floorCamOk;
extern std::atomic<float> g_floorEyeH, g_floorFwdY, g_floorUpY;
extern std::atomic<float> g_viewFwdY;
extern std::atomic<float> g_projSx, g_projSy, g_stereoIpd, g_stereoConv;
extern std::atomic<bool> g_handWalk;
extern std::atomic<uint32_t> g_handWalkReads;
extern bool g_lookOk;
bool AfrOn();
bool LookAtNow(float out[3]);
bool BodyWalkReport(BWPadMirror* out, bool evenIfOff);
uintptr_t SewerHero() noexcept;
int SewerLevel(uintptr_t hero) noexcept;
namespace replay { bool OverlaysWanted(); }

// ---- what vr/sky/lights.cpp (M8) reads of vrcam.cpp: the hero unit and the look-at ----
LONGLONG QpcUs();
bool IsHeroUnit(const void* unit);
extern std::atomic<bool> g_heroWanted;
extern std::atomic<void*> g_heroUnit;
extern std::atomic<void*> g_heroSeen;
extern SRWLOCK g_lookLock;
extern float g_lookAt[3];

// ---- vr/plugin/reshade_load.cpp (M3): ReShade at the plugin's load (global names, as they were) ----
std::string Utf8(const wchar_t* w);
const std::wstring& ReShadeDir();   // where ReShade64.dll and the effect are: the mod's reshade folder or the game's
const std::wstring& OurFilesDir();   // the folder of the plugin's own files
}  // namespace vrc
void LoadReShade();
void LoadOurAddonForDxgiReShade(const wchar_t* gameDir);
namespace vrc {

}  // namespace vrc
