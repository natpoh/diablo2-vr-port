// vr/hud/hud_vr.h - what vrcam.cpp uses of the VR layer's interface parts (vr/hud/*.cpp, docs/plan_modules.md M9).
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>

#include "../durability_figure.h"   // durab::Look

namespace vrc {

// vr/hud/looks.cpp: the toolbar's, the labels' and the plate's looks for the view now
float BarNearNow();
float BarSizeNow();
void BarOffsetNow(float* x, float* y);
bool BarNearWanted();
bool MapNearWanted();
bool FloorView();
void LabelLookNow(float* alpha, float* size);
extern std::atomic<bool> g_labelPaintIn, g_labelSizeIn;   // the game's item-label code hooked
bool LabelBoxNative();
bool LabelsTilted();
bool PlateOwnDepth();
bool PlateWanted();
bool LayerOwnEye();
bool PlateTurnsWanted();
bool PlateFaded();
bool AlliesMoved();
durab::Look DurabilityLook();
bool DurabilityMoved();
bool LabelsWanted();

// vr/hud/crosshair.cpp: the crosshair in place of the game's gauntlet
using SetCursorFn = HCURSOR (WINAPI*)(HCURSOR);
extern SetCursorFn OrigSetCursor;
bool CrosshairNow();
bool CrosshairHidden();
HCURSOR Crosshair();
void CrosshairTick();
void HookCursor();

// vr/hud/labels.cpp: the game's item-label code hooked; the labels' depths for the effect
constexpr uint64_t RVA_TEXT_MEASURE = 0x909560, RVA_UI_SCALE = 0x8460F0, RVA_IS_HD = 0x846210;
using UiScaleFn = float (*)();
extern const uint8_t kSigUiScale[13];
extern std::atomic<float> g_uiScaleSeen;   // the game's interface scale, as the label code last asked it
float GameUiScaleNow() noexcept;
void InstallLabelHooks();
void InstallLabelArea();   // [hud] labels_whole_eye
struct LabelDepth { int32_t x, y, w, h; float dist; };
constexpr int kLabelDepthMax = 32;   // as D2R_DepthFog.fx's LabelBoxes
extern SRWLOCK g_labelDepthLock;
extern LabelDepth g_labelDepthNew[kLabelDepthMax];
extern int g_labelDepthNewN;

// vr/hud/hud_ticks.cpp: the floor's pointer, the map's direction, the hand's inventory, the crosshair's depth
void MapDirectionTick();
bool FloorPointerAuto();
void FloorPointerTick();
void HandInventoryTick(ULONGLONG nowMs);
void CrosshairDepthTick();

// vr/hud/window.cpp: the game's window and the mouse (its window procedure replaced: the plugin's unload puts the game's back)
extern WNDPROC g_gameWndProc;

}  // namespace vrc
