// frame_gen_lib.h - [openxr] frame_generation (experimental): OFXR Bridge in the native OpenXR session (shared/frame_gen.h has the what and why). vr/xr.cpp calls
// these from its session thread: Offer before the loader is first used, the layer's name into xrCreateInstance, Attach after it, PanelImages for every swapchain
// that is not an eye's, Tick once a second. With the key off nothing here does anything.
#pragma once

#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_PLATFORM_WIN32
#include <windows.h>
#include <d3d12.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <vector>

#include "frame_gen.h"

namespace framegenlib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// From the ini (live): the mode and NVIDIA's options, and whether both eyes come from one game frame (not by turns).
void Configure(const framegen::Options& o, bool bothEyes);

// Before the OpenXR loader is first used in this process (the session's start): with the key on and the layer installed in <ini's folder>\..\d2r_vr_libs\ofxr,
// that folder is added to XR_API_LAYER_PATH for this process. The layer's name to ask xrCreateInstance for, or null (off, not installed, not listed).
const char* Offer(const wchar_t* iniPath);
// After xrCreateInstance: whether the instance was made with the layer (false: it failed with it and was made without, or it was not asked).
void Attach(XrInstance instance, bool withLayer);
// A swapchain that is not an eye's: its images listed by the runtime itself, below the bridge, so the bridge takes over the eyes only (it makes
// three swapchains and an optical-flow pipeline for each colour swapchain listed through it - with the panels SteamVR ran out: bearstonem's headset test).
XrResult PanelImages(XrSwapchain chain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images);
// The left eye's images as the bridge listed them: the runtime's own list must give the same, or the panels go through the bridge after all.
void CheckEye(XrSwapchain chain, const std::vector<XrSwapchainImageD3D12KHR>& passed);
// Once a second while the session runs: settings changed since are sent to the layer; a changed state is logged. predictedHz: the runtime's predicted rate over
// that second (Virtual Desktop's own halving is watched with it), 0 when not known.
void Tick(float predictedHz);
// The session should be made again (xr.cpp's stop-and-start, as [openxr] on off and on): switched on while running, so the new instance loads the bridge, or
// once after it failed a live request. Taken once.
bool TakeNewSession();
// The session ended (the instance goes): what was attached is forgotten.
void Ended();

}  // namespace framegenlib
