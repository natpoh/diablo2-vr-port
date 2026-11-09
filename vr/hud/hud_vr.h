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
#include "../plate_look.h"         // platelook::DepthByDistance
#include "../own_labels.h"         // ownlabels::Want ([hud] labels_own)
#include "../own_names.h"          // ownnames::Text ([hud] monster_names_own)

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
bool PlateOtherStereo();       // VR F1 / F2 / F3, a stereo pair of our camera: the plate may stand apart there too (vr/plate_look.h OtherView)
float PlateDepthOtherView();   // [hud_third] / [hud_floor] monster_depth for the view now (1 = at its monster), 0 elsewhere
platelook::DepthByDistance PlateDepthByDistance();   // [hud] monster_stereo_by_distance's settings
float PlateDepthForBox();
bool PlateWanted();
bool LayerOwnEye();
bool PlateTurnsWanted();
bool PlateFaded();
bool AlliesMoved();
bool AlliesHidden(float* units);   // [hud] allies_game_hide: the game's own portraits hidden (VR F4); units: their corner's width (layout units)
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
struct LabelDepth {
    int32_t x, y, w, h;
    float dist;
    bool own = false;          // [hud] labels_own: drawn by us (want: the name, its colour, the box's colour, the pointed look)
    ownlabels::Want want{};
    float sizeK = 1.0f;        // ... labels_size x labels_far for this label (ownlabels::SizeK; the pointed one's point_pickup_label_pct too)
    float ownK = 1.0f;         // ... what of it our cell still needs: 1 when the layout hook made the game's box that size (ResizeLabelBox)
};
bool OwnLabelsNow();   // [hud] labels_own, F4 now (vr/hud/labels.cpp)
// [hud] monster_names_own (vr/own_names.h), vr/hud/labels.cpp: F4 now (not while the plate is hung in the room, monster_bar_room); the effect tells the text hook
// whether it is wanted and where the plate stands (the interface layer's pixels; nullptr: no plate), takes the plate's texts the game drew, marks the ones it
// drew ours for (only those the game then skips), and asks the target's kind (monster data + 0x1A) for a name with no colour code.
bool MonsterNamesOwnNow();
void MonsterPlateWhichTick();   // one log line each change: ours or the game's plate, the view, the preset, why (vr/own_plate_why.h)
ownlabels::SizeCurve OwnSizeCurve();   // [hud] labels_own_near_pct / _far_pct / _near_m / _far_m
void NamesSetPlate(bool on, const float* platePx);
int NamesTake(ownnames::Text out[ownnames::kMax]);
void NamesMark(uint64_t key);
bool NamesTargetKind(uint8_t* special);
// the target's place on the grid and its MonStats bytes 8, 9, 13 (its body: ownnames::HeadAnchor): 0 read; else why not (ownnames::BodyWhy)
int NamesTargetBody(float foot[2], uint8_t row[3]);
// [hud] npc_names_own (vr/own_names.h): the names over NPCs, objects, portals (the game's hover tooltip) - F4 now; the effect tells the text hook, takes them.
bool NpcNamesOwnNow();
void NamesSet3d(bool on);
// the target's plate as the game was told it: its life (0..1; false: not told lately) and whether its frame is the unique one
bool NamesTargetPlate(float* life, bool* unique);
// [hud] ordinary_mob_bars on and the target an ordinary monster (vr/mob_bars.h HidesPlate): our plate is not drawn either
bool NamesTargetHidden();   // our plate name over the head this frame (every copy ours to stand in for) or not (only the copy in the plate's box)
void NamesSetHover(bool on);
int HoverTake(ownnames::Text* out, int most);
constexpr int kLabelDepthMax = 32;   // as D2R_DepthFog.fx's LabelBoxes
extern SRWLOCK g_labelDepthLock;
extern LabelDepth g_labelDepthNew[kLabelDepthMax];
extern int g_labelDepthNewN;

// vr/hud/hud_ticks.cpp: the floor's pointer, the map's direction, the hand's inventory, the crosshair's depth
void MapDirectionTick();
void MapRotateFlatTick();
void FlatMapZoomTick();
bool FlatMapZoomKey(UINT msg, WPARAM w, LPARAM l);
bool FloorPointerAuto();
void FloorPointerTick();
void HandInventoryTick(ULONGLONG nowMs);
void CrosshairDepthTick();

// vr/hud/window.cpp: the game's window and the mouse (its window procedure replaced: the plugin's unload puts the game's back)
extern WNDPROC g_gameWndProc;

}  // namespace vrc
