#pragma once
// [headset_shot]: a screenshot of what reaches the headset - for a picture that is doubled, in the wrong eye, black or flat (Serg, 2026-10-30:
// «кнопку снять скриншот который поступает уже в очки»). Not the window: the images handed to the headset at that moment.
//   - native OpenXR (vr/xr.cpp CloseFrame): every layer of the frame as submitted - each eye's projection image (only the part its imageRect
//     gives), a quad's image (the flat picture, the panels), right before xrEndFrame;
//   - FlatVR (ReShade / BodyWalk, vr/render/stereo/replay.cpp): the back buffers the plugin passes on - the left eye at the hold, the right
//     one (or the flat picture) at its present.
// Into d2rloader\logs\: headset_shot_<time>_left.png / _right.png / _sbs.png (both side by side) / _flat.png / _layer<n>_<name>.png, and
// headset_shot_<time>.txt - the frame's layers, sizes and formats, each eye's angles and pose, the eye distance, stereo pair or flat, the
// version (the caller's text, with what was read back added). The newest kHeadsetShotsKept shots are kept.
//
// No hitch for the press: on the draw thread a shot is a copy per picture into a read-back buffer on the queue the picture was drawn on,
// one ExecuteCommandLists and one Signal. The buffers are made on a thread of its own before (the frame the shot was asked for only says
// what sizes it needs), and the same thread waits for the copies, reads them, writes the PNGs (WIC) and the text, and logs one line.
//
// Off ([headset_shot] on=0, the default): nothing here runs - Wanted() is one atomic load.

#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

namespace headshot {

constexpr size_t kHeadsetShotsKept = 10;

void SetLogger(void (*log)(const char*));
void SetVersion(const char* version);
// From d2r_vr.ini, on every change: [headset_shot] on, key (0 none, 1 Scroll Lock, 2 Pause, 3 F8), take (Settings' button: a counter, each
// change one shot). The shots go to <the ini's folder>\..\logs\ (d2rloader\logs beside d2rloader\plugins).
void LoadSettings(const wchar_t* iniPath);
bool On();
// The key, from the 1 ms update thread: a shot at its press while the game is in front (focus).
void KeyTick(bool focus);

// A shot asked for (by: "the Settings button", "the key"): the next frame's pictures are read back. False: off, or one under way.
bool Request(const char* by);
// The frame code offers its pictures now (cheap: every frame).
bool Wanted();

struct Picture {
    ID3D12Resource* tex = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;   // the state it is in, and is left in
    uint32_t x = 0, y = 0, w = 0, h = 0;                          // the part handed to the headset (w 0: the whole)
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;                     // as the runtime takes it (the swapchain's); UNKNOWN: the texture's own
    const char* part = "";                                        // "left", "right", "flat", "layer3_hud" ... (its file's name)
    bool opaque = true;                                           // alpha written 255 (an eye's picture)
};
// One picture of this frame, on the draw thread, on `queue` (a direct queue: where the picture was drawn). An empty part is ignored.
void Offer(ID3D12Device* dev, ID3D12CommandQueue* queue, const Picture& p);
// The frame's pictures are all offered; `info` describes the frame (its layers, eyes, ...) for the text file. A frame with nothing offered
// waits for one that has (after 90 such frames the text alone is written: nothing reached the headset).
void FrameDone(const std::string& info);

// Tests and the plugin's unload: the shot under way finished (or given up) within ms. The last shot's files (full paths).
bool WaitIdle(unsigned ms);
std::vector<std::wstring> LastFiles();
void SetFolderForTest(const wchar_t* folder);   // (tests: no ini)
void SetOnForTest(bool on);

}  // namespace headshot
