#pragma once
// near_overlay.h - [openxr] hands_over_panels: what is nearer than a panel of ours - the hero's hands and arms, the weapon held, anything held close -
// drawn OVER that panel. A runtime composes the layers in order and a panel's quad lies over the eyes' picture whatever the depths (SteamVR has no depth
// test for quad layers); so each eye's picture is masked to its near pixels (its own depth: the game's reverse-Z with the far plane at infinity, a
// pixel's distance = near / depth in the world's units) and that goes to the headset as one more projection layer, hung after the panel, with its
// alpha: the hands and the weapon in front of the window, the window crisp where nothing is nearer (Serg, 2026-11-08: "the overlay behind the hands").
//
// Plain D3D12 (one fullscreen pass), so tools/near_overlay_test runs the same code on WARP and reads the result back. The arithmetic is pure and
// shared with the shader's text (MetresOf, Alpha).

#include <cstdint>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>

namespace nearoverlay {

// A pixel's distance in metres from its reverse-Z depth (infinite far plane): near / depth world units, / units per metre. A depth of 0 (or less,
// or not a number) is the sky: infinitely far.
inline float MetresOf(float depth, float nearPlane, float upm) {
    if (!(depth > 0.0f) || !(nearPlane > 0.0f) || !(upm > 0.0f)) return 1e30f;
    return nearPlane / depth / upm;
}
// How much of a pixel at `metres` is drawn over the panel: 1 nearer than `maxM - featherM`, 0 at `maxM` and farther, a ramp between.
inline float Alpha(float metres, float maxM, float featherM) {
    if (!(metres < maxM)) return 0.0f;
    if (!(featherM > 0.0f)) return 1.0f;
    const float a = (maxM - metres) / featherM;
    return a >= 1.0f ? 1.0f : a;
}

// [openxr] hands_over_panels: 0 off (the panels lie over the hands, as before); 1 only while the skill panel is up on its Gestures tab - the tab's
// list, its screens, its recording (Serg, 2026-11-08, from the GPU cost work: "the hands over the menus only on the Gestures tab and nowhere else;
// in the rest of the game they are off and take no performance"). Everywhere else - the panel's other tabs, every other menu, plain play - no pass,
// no layer, no swapchains, no pipeline: nothing allocated or drawn (xr.cpp gives them back as the tab closes). An older ini's 2 ("in front of every
// menu", dropped) is read as 1.
enum Mode : int { kOff = 0, kGesturesTab = 1 };
inline int ModeOf(float iniValue) { return iniValue >= 0.5f ? kGesturesTab : kOff; }

// When the layer runs: the mode, a session, a pair of eyes, VR first person (F4), the skill panel up on its Gestures tab (`gesturesTab`: the
// panel's step says so - wheel::Step::gesturesTab). Anything else: nothing.
inline bool Wanted(int mode, bool session, bool pair, bool f4, bool gesturesTab) {
    return mode == kGesturesTab && session && pair && f4 && gesturesTab;
}

// The distance (metres, from the eye) beyond which nothing is laid over a panel `panelM` away: a little short of it.
constexpr float kMarginM = 0.04f, kFeatherM = 0.03f;
inline float MaxFor(float panelM) { return panelM - kMarginM > 0.1f ? panelM - kMarginM : 0.1f; }

struct Params {
    int ox = 0, oy = 0;            // where the eye's image starts in the picture (runtime_fov's box: its top left), pixels
    float pictureW = 1, pictureH = 1;   // the picture's size (the depth is read at its uv, times depthScale)
    float depthScale[2] = {1, 1};  // the part of the depth texture the scene is drawn in (the game's Resolution Scale)
    float nearPlane = 0;           // the game's near plane (world units)
    float upm = 1;                 // world units per metre
    float maxM = 0.76f;            // nearer than this: over the panel
    float featherM = kFeatherM;
    bool decodeSrgb = true;        // the eye's swapchain shows the picture as sRGB: decode it, the target (an sRGB view) encodes it again
};

class Pass {
public:
    // Compiles the shader and makes the pipeline for targets of `rtvFormat` (once; again for another format). False, and *why, when it cannot.
    bool Init(ID3D12Device* dev, DXGI_FORMAT rtvFormat, std::string* why = nullptr);
    bool Ready() const { return pso_ != nullptr; }
    DXGI_FORMAT Format() const { return format_; }
    // Records the pass: the target (a `w` x `h` image in RENDER_TARGET state, `rtv` its view) gets the picture's near pixels, premultiplied, alpha the
    // mask; nothing else of it. `picture` in PIXEL_SHADER_RESOURCE, read through a view of `pictureView`; `depth` readable by shaders, `depthView`.
    // Each record takes the next of the pass's descriptor slots (kSlots of them, re-used in turn: kSlots frames' worth).
    bool Record(ID3D12GraphicsCommandList* list, ID3D12Resource* picture, DXGI_FORMAT pictureView, ID3D12Resource* depth, DXGI_FORMAT depthView,
                D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t w, uint32_t h, const Params& p);
    void Release();
    ~Pass() { Release(); }

    static constexpr int kSlots = 16;

private:
    ID3D12Device* dev_ = nullptr;
    ID3D12RootSignature* rs_ = nullptr;
    ID3D12PipelineState* pso_ = nullptr;
    ID3D12DescriptorHeap* heap_ = nullptr;
    UINT inc_ = 0;
    int slot_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};

// The shader's own format for a picture's view: a typeless one as its UNORM, an sRGB one as its UNORM (the pass decodes itself), else as it is.
DXGI_FORMAT PictureView(DXGI_FORMAT f);
bool IsSrgb(DXGI_FORMAT f);

}  // namespace nearoverlay
