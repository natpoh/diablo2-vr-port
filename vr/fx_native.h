// fx_native.h - [render] no_reshade: reshade/D2R_DepthFog.fx run by the plugin itself, with no ReShade64.dll in the game (docs/plan_no_reshade.md).
//
// The effect is compiled by ReShade's own effect compiler, vendored (third_party/reshadefx: the FX preprocessor and parser, HLSL per entry point)
// and D3DCompile (d3dcompiler_47.dll, part of Windows), on worker threads below normal priority; the shaders are kept on disk by their HLSL's hash,
// so a size seen before is ready at once. Then, also off the present's thread: its textures (render targets, storages), its sampler states, one
// root signature and a pipeline state per pass. The pictures of its `source =` annotations (read by WIC, mips made on the CPU) live apart from
// the builds, read on a thread of their own only while wanted (Options::pictureWanted), one for every texture of the same file and size.
//
// Each present the techniques that are on run on the game's back buffer, pass by pass as ReShade runs them: COLOR is a copy of the back buffer made
// before the first pass and again after every pass that draws into it; a pass without render targets draws into the back buffer; a compute pass
// dispatches its DispatchSize; DEPTH / D2RVR_DEPTH and the other semantics are what the plugin binds (BindSemantic), else read as zero.
//
// Uniforms, techniques, preprocessor definitions and textures are reached by the same names and types as ReShade's effect_runtime
// (find_uniform_variable, set_uniform_value_float, find_technique, set_technique_state, set_preprocessor_definition_for_effect ...), so vrcam's
// effect code (templates over the runtime) drives ReShade's or this one. One thread (the present's) calls all of it.
#pragma once

#include <d3d12.h>
#include <reshade.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fxnative {

using LogFn = void (*)(const char*);

struct Options {
    std::wstring effectFile;                 // ...\reshade-shaders\Shaders\D2R_DepthFog.fx
    std::vector<std::wstring> textureDirs;   // where the `source = "..."` pictures are looked for, in order
    std::wstring cacheDir;                   // the compiled shaders kept there ("": not kept)
    int threads = 0;                         // compile threads; 0 = the processors less one (at least 1, at most 8)
    int optimisation = 1;                    // D3DCompile's optimisation level 0-3
    int passLimit = 0;                       // (diagnosis) only the first this many passes of a technique; 0 = all
    // A graphics pass by its pixel shader's entry point: false = not drawn this frame (it would give the picture as it is - vr/fx_pass_keep.h),
    // nor COLOR copied after it. Empty: every pass drawn.
    std::function<bool(const char* entryPoint)> keepPass;
    // A view handle given to update_texture_bindings (a texture made for the effect elsewhere - vr/hud.cpp's through vr/native_rsapi.h): its
    // resource and format (nullptr: not known - the binding left as it was).
    std::function<ID3D12Resource*(uint64_t view, DXGI_FORMAT* format)> viewResource;
    // A picture (a `source =` texture) by its name in the effect: false = not wanted on the graphics card now (vr/fx_picture_keep.h: the other
    // acts' skies, the ceiling's other stones). Empty: every one. A wanted picture is read off the present's thread and put on the card the
    // Render after; until then its texture reads black. One kept across builds (a new size takes it as it is: no second copy, no read).
    std::function<bool(const char* textureName)> pictureWanted;
    int pictureKeepFrames = 600;             // a picture not wanted this many Begins in a row is let go
    LogFn log = nullptr;
    void (*reloaded)() = nullptr;            // a new build of the effect replaced the old: the handles of the old are gone (vrcam's Forget)
};

struct Stats {
    bool ready = false, compiling = false, failed = false;
    uint32_t width = 0, height = 0;          // the size of the build in use
    uint32_t passes = 0;                     // drawn or dispatched since the last call
    uint32_t skipped = 0;                    // ... and left out (Options::keepPass)
    uint32_t renders = 0;                    // Render calls that recorded something (one a present)
    uint32_t techniques = 0;                 // RenderTechnique calls (the overlays drawn apart, on each eye)
    double lastCompileMs = 0.0;              // the last build: compile and build time, ms
    uint32_t cached = 0, compiled = 0;       // of its entry points: from the disk cache, compiled
    // the pictures (Options::pictureWanted): on the graphics card now, their size, being read; files read since the start
    uint32_t pictures = 0, picturesReading = 0, pictureReads = 0;
    uint32_t colorsMade = 0;                 // COLOR (the back buffer's copy) made, since the start: once a size

    double pictureMiB = 0.0;
    // the graphics card's time (SetTimestampFrequency given): a Render's, on average since the last call, and its dearest passes
    double gpuMs = 0.0;
    uint32_t timed = 0;                      // Renders timed
    // "PS_DepthFog 0.41, PS_VoidFog 0.08, ..." (ms a Render); a pass run in fewer of them (a technique of one eye, the other's the next
    // present) also its time when run: "PS_DepthFogR 0.69 (1.38 in 50%)"
    std::string dearest;
};

class Runtime {
public:
    explicit Runtime(const Options& o);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // The frame's start, with its back buffer: true when a build for its size is there - the uniforms may be set, Render may draw. The first
    // call of a size (or after a definition changed) starts a build in the background; false till it is there.
    bool Begin(ID3D12Device* dev, ID3D12Resource* bb);
    // The techniques that are on, into bb (in PRESENT before and after) on list (open; the caller submits it on the game's queue). False:
    // nothing recorded (no Begin this frame, or none on).
    bool Render(ID3D12GraphicsCommandList* list, ID3D12Resource* bb);
    // One technique only, into `target` (of the back buffer's size; RENDER_TARGET before and after if !backBuffer, PRESENT if backBuffer),
    // COLOR copied from it first. After Begin, in the same frame.
    bool RenderTechnique(reshade::api::effect_technique t, ID3D12GraphicsCommandList* list, ID3D12Resource* target, bool backBuffer);
    // A texture with this semantic reads `res` through a view of `format` (nullptr: zero). Kept across builds; a reference to res is held while it
    // is bound (and some frames after), so its owner may free it at any time.
    void BindSemantic(const char* semantic, ID3D12Resource* res, DXGI_FORMAT format);
    // The queue's timestamp frequency (ID3D12CommandQueue::GetTimestampFrequency): each Render's passes timed on the graphics card from then on.
    void SetTimestampFrequency(uint64_t hz);
    Stats TakeStats();

    // ---- as ReShade's effect_runtime (the effect name is not looked at: there is one)
    reshade::api::effect_uniform_variable find_uniform_variable(const char* effect, const char* name) const;
    void get_uniform_value_bool(reshade::api::effect_uniform_variable v, bool* values, size_t count, size_t arrayIndex = 0) const;
    void get_uniform_value_float(reshade::api::effect_uniform_variable v, float* values, size_t count, size_t arrayIndex = 0) const;
    void get_uniform_value_int(reshade::api::effect_uniform_variable v, int32_t* values, size_t count, size_t arrayIndex = 0) const;
    void get_uniform_value_uint(reshade::api::effect_uniform_variable v, uint32_t* values, size_t count, size_t arrayIndex = 0) const;
    void set_uniform_value_bool(reshade::api::effect_uniform_variable v, const bool* values, size_t count, size_t arrayIndex = 0);
    void set_uniform_value_float(reshade::api::effect_uniform_variable v, const float* values, size_t count, size_t arrayIndex = 0);
    void set_uniform_value_int(reshade::api::effect_uniform_variable v, const int32_t* values, size_t count, size_t arrayIndex = 0);
    void set_uniform_value_uint(reshade::api::effect_uniform_variable v, const uint32_t* values, size_t count, size_t arrayIndex = 0);
    reshade::api::effect_technique find_technique(const char* effect, const char* name);
    bool get_technique_state(reshade::api::effect_technique t) const;
    void set_technique_state(reshade::api::effect_technique t, bool enabled);
    reshade::api::effect_texture_variable find_texture_variable(const char* effect, const char* name) const;
    // the pixels of a texture of the effect (RGBA8, its size), uploaded with the next Render
    void update_texture(reshade::api::effect_texture_variable v, uint32_t width, uint32_t height, const void* pixels);
    // ReShade's views mean nothing here: a semantic is bound with BindSemantic. Kept so the same code compiles; a non-zero view is told once.
    void update_texture_bindings(const char* semantic, reshade::api::resource_view srv, reshade::api::resource_view srvSrgb);
    void get_texture_binding(reshade::api::effect_texture_variable v, reshade::api::resource_view* srv, reshade::api::resource_view* srvSrgb) const;
    bool get_preprocessor_definition_for_effect(const char* effect, const char* name, char* value, size_t* size) const;
    void set_preprocessor_definition_for_effect(const char* effect, const char* name, const char* value);
    bool get_effects_state() const { return true; }
    void get_screenshot_width_and_height(uint32_t* w, uint32_t* h) const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_;
};

}  // namespace fxnative
