// gpu_fault.h - the D3D12 device lost: told at once, and what the GPU was doing told with it.
//
// 2026-10-17, three games in a day ended the same way: the game's own DX12 backend asserted on a failed HRESULT
// (dx12_backend.cpp:5241, "Continuing because assertion dialogs are disabled"), the next line of the game's code read a null
// pointer (D2R.exe + 0xE75C13), and the runtime (Virtual Desktop's) logged "xrBeginFrame: HRESULT failure [887a0005]" a moment
// later: DXGI_ERROR_DEVICE_REMOVED. The device was gone; nothing in our log said why, or even that.
//
//  - DRED (Device Removed Extended Data): asked for before the game makes its device - auto-breadcrumbs (what each command list
//    had done when the GPU stopped) and page-fault reporting (the address, and what was allocated or just freed there). [debug] dred.
//  - A watcher on the device: a fence that D3D12 itself signals when the device is removed (every fence goes to UINT64_MAX). A thread-pool
//    wait on its event writes the report the moment it happens - while the game is still on its way to the null read.
//  - Check(): the same report from any place that sees a call fail.
//  - A flight recorder: the last events of ours that touch the GPU's memory (a swapchain made or destroyed, an upload buffer
//    released, a render-target slot handed out) with their times, written with the report - the other half of "what was it doing".
//
// The report goes to the log and to d2rloader\logs\d2r_vr_gpu_fault.txt. vr/gpu_fault.cpp.
#pragma once

#include <cstdint>

struct ID3D12Device;
struct D3D12_AUTO_BREADCRUMB_NODE;

namespace gpufault {

// Once, at the plugin's load: where the log is, and the folder of d2r_vr.ini (the report file goes to ..\logs\ beside it).
void Init(void (*log)(const char*), const wchar_t* iniPath);

// Before the game makes its D3D12 device (the plugin's load is early enough: D2RLoader loads plugins before the window exists).
// Asks the runtime for DRED, forced on. Once.
void EnableDred(bool on);

// Before the game makes its device, like EnableDred: the D3D12 DEBUG LAYER ([debug] d3d12_validation): 0 off, 1 the layer on and its messages (errors,
// corruption, warnings - a bad barrier, a copy past a buffer, a resource released under a list) in the log with the call's own words, 2 also GPU-based
// validation (every shader read and write checked: 10 times slower - for a flat repro, not for play). Needs Windows' Graphics Tools (D3D12SDKLayers.dll).
void EnableValidation(int level);

// The game's device, once it is known; every present may say it again (the same device is nothing). Another device replaces the one before.
void Watch(ID3D12Device* dev);
// The device is being destroyed (ReShade's destroy_device).
void Unwatch(ID3D12Device* dev);

// A device was seen removed (cheap; any thread).
bool Removed();
// Now: is it removed? The first time yes, the report is written. `where`: what asked. True = removed.
bool Check(ID3D12Device* dev, const char* where);

// The flight recorder. `what` must be a string literal (it is read later). a, b: two numbers worth knowing (a handle, a size).
void Note(const char* what, uint64_t a = 0, uint64_t b = 0);

// The watched device's video memory, this process: usage and budget of the local (video) segment, bytes. False before a device is watched. Any thread.
// A stall that came with a live render-size change and a world teardown (2026-10-18) may be the video memory's paging; this puts the numbers in the log.
bool VideoMemory(uint64_t* usage, uint64_t* budget);

// The breadcrumb list as a report tells it, to the log (tools/gpu_fault_test.cpp builds a list by hand: a hand-made device loss has none).
void DescribeBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE* head);

// The words (pure; tools/xr_hardening_test.cpp).
const char* ReasonName(int32_t hr);     // GetDeviceRemovedReason's result
const char* OpName(int op);             // D3D12_AUTO_BREADCRUMB_OP
const char* AllocTypeName(int type);    // D3D12_DRED_ALLOCATION_TYPE

}  // namespace gpufault
