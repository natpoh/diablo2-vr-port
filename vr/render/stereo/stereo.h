// vr/render/stereo/stereo.h - what vrcam.cpp uses of the stereo parts (vr/render/stereo/*.cpp, docs/plan_modules.md M14).
#pragma once

#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <reshade.hpp>

#include "../../fx_native.h"   // fxnative::Runtime

namespace vrc {

// vr/render/stereo/replay.cpp: one CPU pass, two GPU frames (the right eye from the left's lists), the overlays, native OpenXR's pair
namespace replay {
enum { kWaitPrev, kWaitRestore, kHold, kWaitLeft, kPatch, kSubmit, kPresentR, kCopyL, kPresentL, kSteps };
struct OverlayTell {
    uint32_t presents = 0, apart = 0, inPicture = 0, drawn = 0, made = 0, madeDrawn = 0, madeSkipped = 0, empty = 0, eyeOff = 0, cams = 0, camFallback = 0;
    float gapAt[2] = {}, gapTurn[2] = {};   // the largest miss of each eye's overlay camera this window: world units, about radians
    const char* why = "";                   // why not apart, the last time
    ULONGLONG since = 0;
};
void BeginLeft();
bool DlssBoth();
bool DrawOverlays(reshade::api::effect_runtime* rt, ID3D12Resource* target, int eye, DXGI_FORMAT fmt, bool backBuffer);
bool FxFence();
void MonitorPresent(reshade::api::effect_runtime* rt, HWND wnd, ID3D12CommandQueue* q, ID3D12Resource* bb);
bool OverlaysNow();
bool OverlaysWanted();
void Register();
bool RightFromLeft();
bool SkipWhileStuck(const char* what);
void TellOverlays(float upm, float gapCm);
bool Wanted();
bool XrBegin();
void XrEnd();
void XrPresent(reshade::api::effect_runtime* rt, ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb);
extern std::atomic<int> g_ahead;
extern std::atomic<int> g_capturing;
extern std::atomic<uint32_t> g_depthBinds;
extern uint64_t g_depthBound;
extern std::atomic<bool> g_depthCopy;
extern std::atomic<uint32_t> g_depthKept;
extern std::atomic<uint64_t> g_depthRes;
extern std::atomic<bool> g_dlssBoth;
extern uint32_t g_eclCalls;
extern uint32_t g_eclLists;
extern uint32_t g_eclSignals;
extern uint32_t g_eclUntagged;
extern double g_eclUs;
extern std::atomic<bool> g_fxAtHold;
extern std::atomic<bool> g_fxFence;
extern std::atomic<bool> g_fxSync;
extern std::atomic<bool> g_holdSignalsOn;
extern std::atomic<int> g_marksOn;
extern fxnative::Runtime* g_nativeFx;
extern std::atomic<bool> g_on;
extern std::atomic<bool> g_ovApart;
extern int g_ovDrawnEye;
extern reshade::api::effect_technique g_ovTech[2][2];
extern OverlayTell g_ovTell;
extern std::atomic<int> g_peekWant;
extern ID3D12Resource* g_predBuf;
extern std::atomic<uint32_t> g_predEvals;
extern std::atomic<bool> g_replayCopy;
extern std::atomic<uint32_t> g_replays;
extern std::atomic<bool> g_rightDepth;
extern std::atomic<bool> g_rightReplayed;
extern reshade::api::effect_technique g_rightTech;
extern reshade::api::effect_runtime* g_rt;
extern uint32_t g_stepPairs;
extern double g_stepUs[kSteps];
extern std::atomic<int> g_want;
extern thread_local bool t_ours;
extern thread_local bool t_overlay;
extern thread_local bool t_pairPass;
}  // namespace replay
// vr/render/stereo/prev_scan.cpp: where the game keeps the views of frames past
namespace prevscan {
extern int g_lastEye;
extern float g_lastView[16];
extern bool g_lastViewOk;
extern float g_lastProj[16];
extern bool g_lastProjOk;
void Record(const float m[16]);
void Watch();
void Tick();
void FrameDone();
void FixPrev();
}  // namespace prevscan
// vr/render/stereo/frame_sync.cpp: [flatvr] frame_sync
namespace flatsync {
extern std::atomic<double> g_tookAt;
bool Wait();
}  // namespace flatsync
// vr/render/stereo/pair_pace.cpp: [stereo] pair_fx_sync, [stereo] pace
namespace fx {
namespace pairfx {
void BeforeWrite();
void AfterRender(reshade::api::effect_runtime* rt);
}  // namespace pairfx
namespace pace {
void Tick();
}  // namespace pace
}  // namespace fx

}  // namespace vrc
