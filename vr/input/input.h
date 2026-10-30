// vr/input/input.h - what vrcam.cpp uses of the VR layer's input parts (vr/input/*.cpp, docs/plan_modules.md M10).
#pragma once

#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include "../spell_aim_model.h"   // spellmodel::Out

namespace skel { struct Input; }

namespace vrc {

// vr/input/spell_aim.cpp: spell aim, leap by gaze, the sewer's stairs, the slot's skill
void InstallSpellAimHooks();
void InstallSlotSkillHook();   // [hands] throw_auto_skill
bool ThrowAutoSkillReady();
float PointCapCells(int skill);

// vr/input/walk.cpp: mouse look and W A S D, the chat line, the key move, the lean gait, BodyWalk's walking stick
bool KeyMoveMode();
bool ShooterActive();
bool ChatOpen();
bool StickHookIn();
extern std::atomic<bool> g_mouseLookOn;   // [input] mouse_look, F9
bool ChatFlagFound();
bool ChatFlag();
extern std::atomic<ULONGLONG> g_chatEnterAt;
bool FlatKeyMode();
bool FlatNoPad();
extern std::atomic<bool> g_flatPadTouched;
extern std::atomic<int> g_keyMove;
extern std::atomic<uint32_t> g_keyMoveCalls;
bool VrKeyWalk();
void InstallKeyMoveHook();
void InstallStickHook();

// vr/input/pad.cpp: the stick turned onto the game's screen, BodyWalk's pad mirror, the flat raw controls, XInput's detours, the input log
void InputLogF(const char* fmt, ...);
void HookXInput();
void HookXInputSet();   // [debug] frame_trace only
void ReportXInputUse();
void FlatPadTick();
void FlatRawTick();
bool FlatControllerGate();
bool FlatPressSink(const char* what);
void BeltFlickSink(uint32_t column, int stick, int dir);
void BowLatencyPad(bool a);
extern std::atomic<float> g_latYaw;   // [debug] bow_latency
extern std::atomic<double> g_latYawS, g_latPadDownS, g_latPadUpS;
// vr/input/spell_aim.cpp, also read by the pad
bool ThrownInHand();

// vr/input/aim.cpp: the bow's stick, the aiming hand, the staff's and the spell model's aim, the hand's ray; the aim by mouse, turn and
// gesture; the facing hook, the attack's point and target hooks (HandRay, with a default argument, is declared where it is called)
constexpr uint64_t RVA_ATTACK_TARGET = 0x143960;
using AttackTargetFn = uint64_t (*)(void* self, void* unit, float* dir, int* outX, int* outY);
constexpr uint64_t RVA_UNIT_FACING = 0x349EF0, RVA_AIM_FACING_RET = 0x144824;
using UnitFacingFn = void (*)(void* unit, float* out);
extern const uint8_t kSigAttackPoint[16];
extern const uint8_t kSigAttackTarget[16];
extern const uint8_t kSigUnitFacing[16];
extern AttackTargetFn OrigAttackTarget;
extern UnitFacingFn OrigUnitFacing;
float* HookAttackPoint(void* mgr, float* out, void* unit, uint64_t from, uint64_t dir);
uint64_t HookAttackTarget(void* self, void* unit, float* dir, int* outX, int* outY);
void HookUnitFacing(void* unit, float* out);
int AimHand();
int RangedHomeHand();
bool BowInHand();
int HeldLikeStaff();
bool XbowLikeStaff();
bool StaffByRule();
bool StaffAim();
bool SpellModelWanted();
spellmodel::Out SpellModelAim();
bool SpellArmCells(float* base, float* farCells);
float ArmRayMetres(float baseCells, float farCells);
bool HandsLineToRender(const StickFrame& f, const float s[3], const float d[3], float o[3], float w[3]);
bool GestureCanAim();
bool GestureHeroId(uint32_t* id);
bool GestureHeroMode(uint32_t* mode);
bool AskHeroFacingNow(float* deg) noexcept;
extern std::atomic<LONGLONG> g_logicFacingAt;
extern std::atomic<bool> g_walkSteady;
extern std::atomic<float> g_walkTheta;

// vr/input/actions.cpp: keys and the mouse sent to the game, the pause key; the game's actions from the pad (interact apart from
// attack, the item grab, ApplyActions), the interact target's hook
void PauseKeyTick();
extern std::atomic<ULONGLONG> g_postedEscAt;
using InteractTargetFn = uintptr_t (*)(uintptr_t mgr, uint32_t player);
constexpr uint64_t RVA_INTERACT_TARGET = 0x18D960;
extern const uint8_t kSigInteractTarget[16];
extern InteractTargetFn OrigInteractTarget;
extern std::atomic<bool> g_interactHooked;
uintptr_t HookInteractTarget(uintptr_t mgr, uint32_t player);
void GrabHandsStep(skel::Input* in);

// vr/input/flat_click.cpp: the flat game's clicks - the map click's hook, the mouse on raised ground, the body under the pointer, the
// town portal's oval
constexpr uint64_t RVA_FLAT_UNITS = 0x2A23910, RVA_FLAT_MONSTATS = 0x975B0;
void InstallMapClickHook();
void InstallMouseGroundHook();   // [input] flat_aim_ground (flat only)
void PortalOvalTick();           // [input] flat_portal_no_oval (flat), [render] portal_no_oval (VR)
bool PortalClass(uint32_t cls);
extern std::atomic<bool> g_portalForce, g_portalHover;
extern std::atomic<float> g_portalForceX, g_portalForceZ;

}  // namespace vrc
