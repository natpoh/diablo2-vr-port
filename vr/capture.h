// capture.h - a key that saves what is needed to debug a picture fault later (the floor patches that come only sometimes, 2026-11-07): one press,
// one folder logs\capture_<yyyyMMdd-HHmmss>\ beside the plugin's logs, which Settings' "Collect logs for support" packs (the newest two). Not in
// Settings, the CHANGELOG or the news: the key is told to a tester or a streamer by hand. Default Ctrl+Alt+F10, [debug] capture_key to change it.
//
// What goes in, from the press and the frames right after it (about one second; nothing is written into the game's memory):
//   pipetrace.txt   - one frame's pipelines (vr/pipe_hunt.h): kind, pixel / compute / vertex shader hashes, binds, draws, render targets with
//                     their formats and sizes, depth, viewport;
//   camera.txt      - the game's camera and frame constants as its frame builder filled them (0xE3B590, the hook of [render] depth_fix_mode 2:
//                     the camera block - its projection at +0xD0 with the off-axis terms [8] / [9] - and the frame block - word 53 the linear depth
//                     range), raw in float4 rows, for each build in the window (each eye); the game's own view / projection and look-at (the
//                     hero) and ours as the camera hooks saw them; the view, platform, stereo keys;
//   backbuffer.png  - the picture as presented (the monitor's; in VR also the headset's pictures through [headset_shot] when that is on);
//   state.txt       - the game's graphics options now, the area, fps over the window, the eye, our camera's numbers;
//   d2r_vr.ini, Settings.json - the mod's and the game's settings as they are;
//   capture.txt     - the index: what is in the folder and what failed.
// The newest capturerule::kKept folders are kept. Off when nothing is pressed: one atomic load in each tap.
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <string>

namespace d2rcam { struct WorldView; }
namespace D2RL { struct PluginContext; }

namespace capture {

void Init(void (*log)(const char*), const wchar_t* iniPath, const char* version);
// The plugin's state in words at the press (state.txt) and the eye being drawn (the builds' eye) - vr/frame/frame_order.cpp.
void SetSources(std::string (*state)(), int (*eye)());
// From D2RLoaderLoadPlugin: the console command "vrcapture" (D2RLoader takes commands at the load only) and the UI thread for its answer.
//   vrcapture          - a capture now; answers "recording into capture_<time>", then, once written, "D2R VR capture saved: capture_<time> (N files)"
//                        and a line a file (what failed marked NOT)
//   vrcapture targets  - the same with the game's intermediate render targets of one frame (targets.txt, t*.png / .raw)
//   vrcapture status   - the key, the folder, the last capture
void InitConsole(const D2RL::PluginContext* ctx);
// From LoadSettings: [debug] capture_key (empty = Ctrl+Alt+F10, "none" = off), [debug] capture_targets (the key takes the targets too).
void Configure(const std::wstring& keySpec, bool targets);
// The 1 ms update thread: the key's press (the game in front).
void KeyTick(bool focus);
// True while a capture collects (the Ctrl+F10 handler leaves Ctrl+Alt+F10 alone by the key's own check; this is for the taps).
bool Collecting();

// The taps (each returns at once when not collecting):
// the game's frame builder ran (renderer: its first argument) - vr/render/depth/depth_fix.cpp's hook;
void OnFrameBuilt(void* renderer, void* camera);
// the camera hooks: the game's view / projection in, ours out (ours: whether ours replaced it) - vr/view/camera.cpp;
void OnView(const d2rcam::WorldView& in, const float out[16], bool ours, int eye);
void OnProj(const d2rcam::WorldView& in, const float out[16], bool ours, int eye);
// the game's present, after our own drawing into its back buffer (the picture as it goes out) - vr/render/effects/effects.cpp.
void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* backBuffer);
// the game's barriers (before they go in) - the intermediate targets of one frame, copied as a pass ends (vr/native_d3d12.cpp's hook).
void OnBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b);

}  // namespace capture
