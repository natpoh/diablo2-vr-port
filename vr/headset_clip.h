#pragma once
// [headset_shot] video: a 10-second video of what reaches the headset (Serg, 2026-10-30: «а ещё лучше чтобы было скриншот и видео 10 секунд»,
// «две кнопки») - the screenshot's own button and key apart (vr/headset_shot.h), the same pictures: native OpenXR's eye images as submitted
// (each eye's imageRect; F1's window a quad per eye; the flat picture into both halves), FlatVR's back buffers as the plugin passes them on.
// Into d2rloader\logs\headset_clip_<time>.mp4 (H.264, both eyes side by side at half size, at most 960 x 1080 an eye, about 30 frames a
// second) and headset_clip_<time>.csv (each frame kept: its time, stereo pair or flat, which eyes came, what it cost the draw thread). The
// newest kHeadsetClipsKept clips are kept; one log line a clip says what recording cost the draw thread and how the game's frames came.
//
// Off the draw thread's path: per frame kept, a compute pass makes each eye's half of the picture on the queue the eye was drawn on (a
// shrink of 4 samples a pixel - no full-size copy unless the image refuses a shader view), one copy into a read-back ring, a Signal. A thread
// of its own (made ready before: the shader, the ring, the encoder) waits for each, turns it into NV12 and hands it to Media Foundation's
// H.264 encoder (the graphics card's own where Windows has it). A frame whose ring slot is still busy is left out (counted), never waited for.
//
// Off ([headset_shot] video=0, the default): nothing here runs - Wanted() is one atomic load.

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

namespace headclip {

constexpr size_t kHeadsetClipsKept = 3;

void SetLogger(void (*log)(const char*));
// From d2r_vr.ini, on every change: [headset_shot] video (on), video_take (Settings' button: a counter, each change one clip), video_key (0 none,
// 1 Scroll Lock, 2 Pause, 3 F8). The clips go to <the ini's folder>\..\logs\.
void LoadSettings(const wchar_t* iniPath);
bool On();
void KeyTick(bool focus);   // the 1 ms update thread: a clip at the key's press while the game is in front

// A clip asked for: from the next frame's start on, recorded for 10 s. False: off, or one under way.
bool Request(const char* by);
// The frame code hands its pictures now (cheap: every frame).
bool Wanted();

// One eye's picture of this frame (the draw thread, on `queue`, a direct one where it was drawn): half 0 the left, 1 the right, 2 both (the
// flat picture). x y w h: the part handed to the headset (w 0: the whole). Only in the frames kept (about 30 a second); the others return.
void Eye(ID3D12CommandQueue* queue, ID3D12Resource* tex, D3D12_RESOURCE_STATES state, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int half);
// The frame's pictures are all handed; kind: "pair", "turns" (eyes by turns), "flat", "window" (F1 in 3D). Its row in the .csv.
void FrameDone(const char* kind);

// Tests and the plugin's unload.
bool Busy();   // a clip under way (asked for, being made, recorded or written)
bool WaitIdle(unsigned ms);
std::vector<std::wstring> LastFiles();
void SetFolderForTest(const wchar_t* folder);
void SetOnForTest(bool on);
void SetSecondsForTest(double seconds);

}  // namespace headclip
