// vr/render/depth/depth.h - what vrcam.cpp uses of the depth parts (vr/render/depth/*.cpp, docs/plan_modules.md M13).
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

#include "../../depth_pick.h"   // depthpick::Cache

namespace vrc {

// vr/render/depth/depth_fix.cpp: the right eye's camera in the replayed lists, the depth fix (camfix); the depth range at its source
namespace camfix {
extern std::atomic<bool> g_capture, g_redirect;
extern std::atomic<int> g_nSlots;
extern std::atomic<uint32_t> g_overflow;
extern std::atomic<int> g_scanWant;
void Register();
bool MakeSlots(ID3D12Device* dev);
void BeginLeft(bool redirect);
void ScanCopies();
int RightEye();
void Restore();
// (RecordSlots has default arguments: declared where it is called)
}  // namespace camfix
namespace depthsrc {
void Configure(bool on);
void Tick();
float LastFar();
}  // namespace depthsrc
// vr/render/depth/scene_depth.cpp: how much of the screen the game draws at, the camera behind the depth, the scene's depth picked
namespace fx {
bool OwnDepthNow();
bool DepthEffectsWanted();
bool NativeDepthNow();
namespace renderscale {
using namespace reshade::api;
extern std::atomic<uint32_t> g_screenW, g_screenH;
extern float g_last[2];
void OnBind(command_list* cl, uint32_t, const resource_view*, resource_view dsv);
void OnViewports(command_list* cl, uint32_t first, uint32_t count, const viewport* vps);
void OnBindNative(ID3D12GraphicsCommandList* cl, ID3D12Resource* ds);
void OnViewportNative(ID3D12GraphicsCommandList* cl, float w, float h);
void Frame(effect_runtime* rt, float out[2]);
void FrameOf(uint32_t w, uint32_t h, float out[2]);
}  // namespace renderscale
namespace depthcam {
void Publish();
}  // namespace depthcam
namespace scenedepth {
using namespace reshade::api;
extern depthpick::Cache g_cache;
extern resource g_copy;
extern resource_desc g_copyDesc;
extern bool g_copiedNow;
extern unsigned g_sinceCopy;
extern ID3D12Resource* g_nCopy;
extern DXGI_FORMAT g_nSrv;
format SrvFormat(format typeless);
void OnBind(command_list* cl, uint32_t count, const resource_view*, resource_view dsv);
void OnBarrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to);
void OnDestroy(device*, resource r);
void OnSwapChain(swapchain*, bool);
void OnBindNative(ID3D12Resource* ds, unsigned count);
void OnBarrierNative(unsigned n, const D3D12_RESOURCE_BARRIER* b);
void Bind(effect_runtime* rt, command_list* cl);
void BindNative(ID3D12Device* dev, ID3D12CommandQueue* q, uint32_t w, uint32_t h);
void MarkFx(effect_runtime* rt);
bool MarkAfterOverlays();
void FrameEnd();
}  // namespace scenedepth
}  // namespace fx

}  // namespace vrc
