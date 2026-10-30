// vr/view/camera.h - what vrcam.cpp uses of the head, the camera and the views (vr/view/*.cpp, docs/plan_modules.md M16).
#pragma once

#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../pointer3d.h"   // pointer3d::Similarity
#include "../views3d.h"     // views3d::Table, Vec
#include "../vrcam_internal.h"   // SkyView

struct D2RVR_Shared;
namespace d2rcam { struct WorldView; }

namespace vrc {
// vr/view/head.cpp: the head from BodyWalk (the pair's own copy while a pair is drawn) and the view's yaw, pitch and roll
extern std::atomic<const D2RVR_Shared*> g_pairShared;
const D2RVR_Shared* Shared();
bool HeadYaw(float* yaw);
float WrapDeg(float a);
double NowSeconds();
void PredictHead(const D2RVR_Shared* s, float out[3]);
bool RoomCentreOn();
float HeadYawFromCentre(const D2RVR_Shared* sh, float yawSent);
float TargetYaw();
bool HeadLevelIsGravity();
float TargetRoll();
bool StickPitchOn();
float TargetPitch();
bool FlatVrLevel();
float LevelDownDeg();
float LevelRollDeg();
// vr/view/camera.cpp: our camera - the view and the projection handed to the game (VrView, VrProj), the top and table cameras, the eyes
extern std::atomic<float> g_gameDownDeg;   // the game camera's own downward angle, degrees
extern std::atomic<bool> g_tableReanchor;   // set by F5 and F11, taken by the next view build
void VrFrame();
bool VrView(const d2rcam::WorldView& in, float out[16]);
bool VrProj(const d2rcam::WorldView& in, float M[16]);
// vr/view/views.cpp: the views (F1-F5, F12) and the turn
bool ApplyView(const char** said);
void FollowMode();
void SetView(int v);
const char* Toggle();
void FlatVrCentreTick();
void Recenter();
void TurnTick();
// what camera.cpp writes and reads of vrcam.cpp's state (kept there): the pointer's map ([openxr] pointer_3d), the table's diorama,
// the right eye's sky view and stamp to commit, the aspect, the view's distance and lift
struct PointerMap {
    bool ok = false;
    int mode = -1;
    pointer3d::Similarity sim;
    float groundY = 0.0f;
    float view[16] = {}, proj[16] = {};
    ULONGLONG viewAt = 0, projAt = 0;
};
extern PointerMap g_ptrMap;
extern SRWLOCK g_ptrLock;
extern views3d::Table g_lastTable;
extern views3d::Vec g_lastTableHero;
extern SkyView g_skyRightNext;
extern bool g_skyRightNextOk;
extern uint32_t g_stampRightNext;
extern bool g_stampRightNextOk;
extern std::atomic<float> g_lastAspect;
float ViewDistance();
float JumpLift();
bool TopStereoOn();
}  // namespace vrc
