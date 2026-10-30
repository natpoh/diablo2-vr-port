// vr/render/depth/scene_depth.cpp - the scene's depth for our effects: how much of the screen the game draws at (renderscale), the camera behind the depth for FlatVR (depthcam), the depth-stencil picked and copied (scenedepth) (cut from vr/vrcam.cpp, docs/plan_modules.md M13).
// What it gives: vr/render/depth/depth.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

namespace fx {

// How much of the screen the game draws its scene at (2026-10-08). With DLSS on, the
// scene is drawn into the top left 0.5 - 0.67 of screen-size targets and only then
// scaled up; ReShade's depth is that buffer, so the fog and the sky took the whole
// screen's depth from a corner of it (a dark box on the picture, players' screenshots).
// Read from the game itself: the viewport set while a screen-size depth buffer is bound.
namespace renderscale {
using namespace reshade::api;
thread_local const void* t_cl = nullptr;       // a list is recorded on one thread at a time (ReShade's command_list, or no_reshade's own list)
thread_local uint32_t t_dsW = 0, t_dsH = 0;     // its depth buffer now (0 = none)
thread_local float t_vpW = 0.0f, t_vpH = 0.0f;  // its viewport now
std::atomic<uint32_t> g_screenW{0}, g_screenH{0};
float g_last[2] = {1.0f, 1.0f};   // the share FrameOf gave last (the present's thread)
// The shares seen lately and how often: the scene's passes are most of them - the
// interface, drawn after the upscale, would make the largest one the whole screen.
struct Seen { uint32_t x, y, n; };
SRWLOCK g_lock = SRWLOCK_INIT;
Seen g_seen[8];
int g_seenCount = 0;

void Note() {
    const uint32_t w = g_screenW.load(std::memory_order_relaxed), h = g_screenH.load(std::memory_order_relaxed);
    if (!w || t_dsW != w || t_dsH != h || t_vpW < 16.0f || t_vpH < 16.0f) return;   // not a screen-size depth buffer
    if (t_vpW > w * 1.01f || t_vpH > h * 1.01f) return;
    const uint32_t x = (uint32_t)(t_vpW / w * 1000.0f + 0.5f), y = (uint32_t)(t_vpH / h * 1000.0f + 0.5f);
    AcquireSRWLockExclusive(&g_lock);
    int k = 0;
    while (k < g_seenCount && !(g_seen[k].x == x && g_seen[k].y == y)) ++k;
    if (k < g_seenCount) ++g_seen[k].n;
    else if (g_seenCount < 8) g_seen[g_seenCount++] = {x, y, 1};
    ReleaseSRWLockExclusive(&g_lock);
}
void OnBind(command_list* cl, uint32_t, const resource_view*, resource_view dsv) {
    if (cl != t_cl) { t_cl = cl; t_vpW = t_vpH = 0.0f; }
    t_dsW = t_dsH = 0;
    if (dsv.handle) {
        device* dev = cl->get_device();
        const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(dsv));
        t_dsW = d.texture.width; t_dsH = d.texture.height;
    }
    Note();
}
void OnViewports(command_list* cl, uint32_t first, uint32_t count, const viewport* vps) {
    if (first != 0 || count == 0) return;
    if (cl != t_cl) { t_cl = cl; t_dsW = t_dsH = 0; }
    t_vpW = vps[0].width; t_vpH = vps[0].height;
    Note();
}
// [render] no_reshade: the same from the plugin's own hooks on the game's lists (vr/native_d3d12.h)
void OnBindNative(ID3D12GraphicsCommandList* cl, ID3D12Resource* ds) {
    if (cl != t_cl) { t_cl = cl; t_vpW = t_vpH = 0.0f; }
    t_dsW = t_dsH = 0;
    if (ds) {
        const D3D12_RESOURCE_DESC d = ds->GetDesc();
        t_dsW = (uint32_t)d.Width; t_dsH = d.Height;
    }
    Note();
}
void OnViewportNative(ID3D12GraphicsCommandList* cl, float w, float h) {
    if (cl != t_cl) { t_cl = cl; t_dsW = t_dsH = 0; }
    t_vpW = w; t_vpH = h;
    Note();
}
// Once a frame, before the effect: the share seen over the last half second, kept when
// none was seen (a menu, a loading screen). w x h: the screen (the back buffer).
void FrameOf(uint32_t w, uint32_t h, float out[2]) {
    g_screenW.store(w); g_screenH.store(h);
    static float scale[2] = {1.0f, 1.0f};
    static ULONGLONG since = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - since >= 500) {
        since = now;
        Seen s[8];
        AcquireSRWLockExclusive(&g_lock);
        const int n = g_seenCount;
        memcpy(s, g_seen, sizeof s);
        g_seenCount = 0;
        ReleaseSRWLockExclusive(&g_lock);
        int best = -1;
        for (int k = 0; k < n; ++k) if (best < 0 || s[k].n > s[best].n) best = k;
        if (best >= 0) {
            const float sx = std::clamp(s[best].x / 1000.0f, 0.25f, 1.0f), sy = std::clamp(s[best].y / 1000.0f, 0.25f, 1.0f);
            if (fabsf(sx - scale[0]) > 0.002f || fabsf(sy - scale[1]) > 0.002f) {
                char all[160] = "";
                size_t len = 0;
                for (int k = 0; k < n && len < sizeof all - 24; ++k)
                    len += snprintf(all + len, sizeof all - len, " %.3fx%.3f:%u", s[k].x / 1000.0f, s[k].y / 1000.0f, s[k].n);
                LogF("vrcam: the game draws its scene at %.3f x %.3f of the screen (%ux%u)%s; seen:%s", sx, sy, w, h,
                     sx < 0.99f ? " - an upscaler (DLSS): the depth is read from that part" : "", all);
            }
            scale[0] = sx; scale[1] = sy;
        }
    }
    out[0] = scale[0]; out[1] = scale[1];
    g_last[0] = scale[0]; g_last[1] = scale[1];
}
void Frame(effect_runtime* rt, float out[2]) {
    uint32_t w = 0, h = 0;
    rt->get_screenshot_width_and_height(&w, &h);
    FrameOf(w, h, out);
}
}  // namespace renderscale

// The camera behind the depth, for FlatVR's 3D from ReShade's depth (2026-10-08):
// with it FlatVR shifts the second eye by each pixel's distance in metres and the
// user's IPD - the world at its own size, as real stereo with [stereo] true_scale
// has it - instead of its depth sliders' guess, which made the world look bigger.
// Written while the world is 1:1 (TrueScale: the frustum is FlatVR's screen);
// otherwise the counter stops and FlatVR goes back to its sliders.
namespace depthcam {
FlatVRDepthCamera* g_block = nullptr;
void Publish() {
    if (!g_block) {
        static ULONGLONG lastTry = 0;
        if (GetTickCount64() - lastTry < 2000) return;
        lastTry = GetTickCount64();
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FlatVRDepthCamera), FLATVR_DEPTH_CAMERA_NAME);
        if (!m) return;
        g_block = (FlatVRDepthCamera*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FlatVRDepthCamera));
        if (!g_block) { CloseHandle(m); return; }   // else the handle is kept: the name lives while we do
    }
    float ipd = 0.0f, conv = 0.0f, w = 0.0f, h = 0.0f, d = 0.0f;
    if (!g_enabled.load() || !g_inWorld.load() || !TrueScale(&ipd, &conv) || !LiveScreen(&w, &h, &d) || d <= 0.0f) return;
    const float eyeM = g_set.eyeMm.load() * 0.001f;
    if (!(eyeM > 0.01f)) return;
    g_block->near_world = g_lastNear.load();
    g_block->units_per_metre = ipd / eyeM;
    g_block->tan_half_w = 0.5f * w / d;
    g_block->tan_half_h = 0.5f * h / d;
    g_block->version = FLATVR_DEPTH_CAMERA_VERSION;
    MemoryBarrier();
    g_block->counter = g_block->counter + 1;
}
}  // namespace depthcam

// The scene's depth for our effects, picked by vrcam, not by ReShade (2026-10-11). ReShade's Generic
// Depth takes the depth-stencil with the most draws among those shaped like the screen: with a
// square window (native OpenXR without black bars) the game's shadow map - 4096 x 4096, square too,
// more draws - won, and the fog and the sky were laid by a view from above. ReShade's own size check
// keeps it out only for a window under 2048. Here: the depth-stencil the game binds most in a frame
// that is EXACTLY the window's size (the shadow map never is: a window is under 4096), copied into a
// texture of ours, bound to D2R_DepthFog.fx's own semantic D2RVR_DEPTH - not DEPTH: Generic Depth,
// when on, rebinds DEPTH whenever its pick changes, and with a window over ~2214 its pick went to the
// shadow map on head turns - the left eye's fog flickered with a view from above (2026-10-11). Generic
// Depth now feeds only FlatVR's "3D from the depth". None of the window's size: no depth for our effects.
// The barriers as the replay's right-eye copy makes them (the game's last state on it). [render] own_depth (on).
// Only in VR with D2R VR Settings' square window, where the shadow map is the trap: flat mode and VR
// without it read ReShade's DEPTH as before 0.161 (the effect's OwnDepth) - a flat player's game at the
// Resolution Scale above 100 had no depth of the window's size, and no sky or fog (2026-10-12).
// A render size ([render] vr_resolution) is square or near it too: 2048 and 4096 are the shadow maps' sizes - so ours then, as
// with the square window (and the colour-target rule in Bind: a shadow pass binds none).
// [render] no_reshade: ours always - the only depth there is, for the eye made (an exact window size is picked first all the same).
bool OwnDepthNow() {
    return (g_noReShade || (g_ownDepth.load(std::memory_order_relaxed) && (g_squareWin.load(std::memory_order_relaxed) || vrres::Active()))) &&
           g_set.platform.load() == 1;
}
// The effect draws something placed by the scene's depth: our sky in the void, the fog by distance, the cave ceiling
bool DepthEffectsWanted() { return FogWanted() || OurSkyWanted() || g_arcaneSky.load() || CeilWanted(); }
// [render] no_reshade: the scene's depth copied (scenedepth::BindNative) - in VR, and in flat too while the effect draws what needs it
// (vr/native_fx_depth.h: flat with "Our effect (old)" had none - the void over the whole picture, a black screen)
bool NativeDepthNow() { return nativefxdepth::CopyWanted(g_noReShade, OwnDepthNow(), DepthEffectsWanted()); }
namespace scenedepth {
using namespace reshade::api;
struct Ds { uint64_t res; uint32_t binds, last, state; uint32_t colour, colourLast; };   // (colour: binds with a colour target - a shadow pass has none)
Ds g_ds[16] = {};
SRWLOCK g_lock = SRWLOCK_INIT;
resource g_copy{0};
resource_view g_srv{0};
resource_desc g_copyDesc{};
uint64_t g_from = 0;   // the depth-stencil copied last (the log, once per change)
// The pick, cached (vr/depth_pick.h): the binds are counted only until a depth-stencil is picked, then kept until it is
// destroyed, the swap chain is made again, the window's (or the render) size changes, or the game stops binding it (not at
// an area change: a buffer still bound is alive); each pick is logged, and written to
// d2r_vr_depth.ini beside the ini for D2R VR Settings' status line (World > Fog, "Depth for the effects").
depthpick::Cache g_cache;
std::atomic<uint32_t> g_pickState{0};   // the picked buffer's state as the game's last barrier left it
resource_desc g_pickDesc{};

void ClearCounts() {   // fresh counts after the pick is let go: the old ones stopped at the pick
    AcquireSRWLockExclusive(&g_lock);
    for (Ds& d : g_ds) d = Ds{};
    ReleaseSRWLockExclusive(&g_lock);
}
void OnDestroy(device*, resource r) { g_cache.Destroyed(r.handle); }
// A swap chain made again (a resize: the window, or the render size slider) or going: the game makes its targets again -
// the pick goes at once (test-38: its memory was freed before the buffer itself, and the next copy crashed in the driver).
void OnSwapChain(swapchain*, bool) { g_cache.SwapChainChanged(); }
// d2r_vr_depth.ini [depth]: the pick for Settings (not the ini itself, which vrcam reloads whenever it changes).
void WritePickStatus(uint64_t res, uint32_t w, uint32_t h) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
    wcscat_s(path, L"d2r_vr_depth.ini");
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[160], why[96] = L"";
    MultiByteToWideChar(CP_UTF8, 0, g_cache.why, -1, why, 96);
    swprintf_s(b, L"%llX", (unsigned long long)res); WritePrivateProfileStringW(L"depth", L"buffer", b, path);
    swprintf_s(b, L"%u", w); WritePrivateProfileStringW(L"depth", L"width", b, path);
    swprintf_s(b, L"%u", h); WritePrivateProfileStringW(L"depth", L"height", b, path);
    swprintf_s(b, L"%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond); WritePrivateProfileStringW(L"depth", L"picked_at", b, path);
    WritePrivateProfileStringW(L"depth", L"why", why, path);
    swprintf_s(b, L"%u", g_cache.picks); WritePrivateProfileStringW(L"depth", L"picks", b, path);
    swprintf_s(b, L"%u", g_cache.others); WritePrivateProfileStringW(L"depth", L"other_buffers", b, path);
}

// A bind of depth-stencil r counted (with `count` colour targets).
void Count(uint64_t r, uint32_t count) {
    AcquireSRWLockExclusive(&g_lock);
    Ds* slot = nullptr;
    for (Ds& d : g_ds) {
        if (d.res == r) { slot = &d; break; }
        if (!d.res && !slot) slot = &d;
    }
    if (slot) {
        if (!slot->res) *slot = Ds{r, 0, 0, 0, 0, 0};
        ++slot->binds;
        if (count) ++slot->colour;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
void OnBind(command_list* cl, uint32_t count, const resource_view*, resource_view dsv) {
    if (!dsv.handle) return;
    if (!g_cache.Counting()) {   // picked: nothing to count - only whether the game binds it this frame (one lookup a bind until it does)
        if (g_cache.WantsSight()) g_cache.Seen(cl->get_device()->get_resource_from_view(dsv).handle);
        return;
    }
    if (!OwnDepthNow()) return;
    const uint64_t r = cl->get_device()->get_resource_from_view(dsv).handle;
    if (!r) return;
    Count(r, count);
}
void OnBarrier(command_list*, uint32_t count, const resource* res, const resource_usage*, const resource_usage* to) {
    if (!OwnDepthNow()) return;
    if (const uint64_t p = g_cache.pick.load(std::memory_order_relaxed)) {   // picked: that buffer's state alone, no lock
        for (uint32_t i = 0; i < count; ++i)
            if (res[i].handle == p) g_pickState.store((uint32_t)to[i], std::memory_order_relaxed);
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (uint32_t i = 0; i < count; ++i)
        for (Ds& d : g_ds)
            if (d.res && d.res == res[i].handle) d.state = (uint32_t)to[i];
    ReleaseSRWLockExclusive(&g_lock);
}
// After each present: the frame's counts become the last ones; one not bound for two frames goes.
void FrameEnd() {
    const bool picked = !g_cache.Counting();
    g_cache.FrameEnd();   // counting: one more frame of counts; picked: not bound in this frame, it goes
    if (picked) return;   // no counts to roll
    AcquireSRWLockExclusive(&g_lock);
    for (Ds& d : g_ds) {
        if (!d.res) continue;
        if (!d.binds && !d.last) d = Ds{};
        else { d.last = d.binds; d.binds = 0; d.colourLast = d.colour; d.colour = 0; }
    }
    ReleaseSRWLockExclusive(&g_lock);
}
format SrvFormat(format typeless) {
    switch (typeless) {
        case format::r32_g8_typeless: return format::r32_float_x8_uint;
        case format::r24_g8_typeless: return format::r24_unorm_x8_uint;
        case format::r32_typeless: return format::r32_float;
        case format::r16_typeless: return format::r16_unorm;
        default: return format_to_default_typed(typeless);
    }
}
// [render] fx_fence, a frame of one picture: a fence on the effects' queue after each present's effects (MarkFx, at
// ReShade's present event, which comes after them), waited for by the next frame's pass before its constants go in. Eyes by turns
// ([stereo] turn_fx_fence, on): the same, marked after the eye made's overlays (vr/sky_view_eyes.h FenceEffects, MarkAfterOverlays).
bool FenceNow() { return skyeyes::FenceEffects(replay::FxFence(), NativeByTurns(), g_set.turnFxFence.load()); }
bool MarkAfterOverlays() { return skyeyes::MarkAfterOverlays(NativeByTurns(), g_set.turnFxFence.load()); }
ID3D12Fence* g_fxFence = nullptr;
HANDLE g_fxEvent = nullptr;
UINT64 g_fxSignalled = 0;
bool g_fxMarked = false;   // a mark since the last wait: else nothing to wait for (the first frame, or after a pair)
std::atomic<uint32_t> g_fxWaitsOne{0};
bool FxFenceReady(effect_runtime* rt) {
    if (g_fxFence) return true;
    ID3D12Device* d = (ID3D12Device*)rt->get_device()->get_native();
    if (!d || FAILED(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fxFence))) { g_fxFence = nullptr; return false; }
    g_fxEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return g_fxEvent != nullptr;
}
void MarkFx(effect_runtime* rt) {
    if (!FenceNow() || !OwnDepthNow() || !FxFenceReady(rt)) return;
    if (replay::SkipWhileStuck("the effects' fence mark")) return;
    command_queue* q = rt->get_command_queue();
    q->flush_immediate_command_list();   // the effects of this present are in ReShade's list still: on the queue first
    replay::t_ours = true;   // our signal, not the game's (the replay's queue hooks)
    ((ID3D12CommandQueue*)q->get_native())->Signal(g_fxFence, ++g_fxSignalled);
    replay::t_ours = false;
    g_fxMarked = true;
}
// False: no mark to wait for (the first frame, or after a pair).
bool WaitLastFx(effect_runtime* rt) {
    if (!g_fxMarked || !g_fxFence) return false;
    g_fxMarked = false;
    if (g_fxFence->GetCompletedValue() < g_fxSignalled) {
        g_fxFence->SetEventOnCompletion(g_fxSignalled, g_fxEvent);
        WaitForSingleObject(g_fxEvent, 100);
    }
    g_fxWaitsOne.fetch_add(1, std::memory_order_relaxed);
    (void)rt;
    return true;
}
// reshade_begin_effects: the scene's depth into our copy on the effects' own list, bound as DEPTH.
bool g_copiedNow = false;   // Bind copied the scene's depth for this present (eyes by turns keeps it: xr::SetTurnDepth)
unsigned g_sinceCopy = ~0u >> 1;   // [render] no_reshade: presents since the last copy (nativefxdepth::Usable; none yet: long ago)
void Bind(effect_runtime* rt, command_list* cl) {
    g_copiedNow = false;
    static int told = -1;
    auto tell = [&](int what, const char* text) { if (told != what) { told = what; Log(text); } };
    if (!OwnDepthNow() || !cl) return;
    uint32_t w = 0, h = 0;
    rt->get_screenshot_width_and_height(&w, &h);
    device* dev = rt->get_device();
    // The window's size first; else one of its shape at another scale - the game's Resolution Scale
    // above 100 draws into targets bigger than the window (200: twice), and with none the window's
    // size the effects had no depth at all: no sky, no fog in flat mode since 0.161 (2026-10-12).
    // A square power of two (1024, 2048, 4096) is a shadow map unless it is the window's size.
    if (const char* why = g_cache.Check(w, h)) {
        ClearCounts();
        LogF("vrcam: own depth - the pick let go: %s (counting again; no copy until a buffer the game binds this frame is picked)", why);
    }
    Ds best{}, scaled{};
    resource_desc bd{}, sd{};
    if (const uint64_t p = g_cache.pick.load()) {   // the cached pick: no look round
        // only a buffer the game bound this frame is copied: one it no longer binds may have lost its memory already
        if (!g_cache.CopyOk()) return tell(5, "vrcam: own depth - the picked buffer is not bound this frame: no copy (the last one stays)");
        best.res = p;
        best.state = g_pickState.load(std::memory_order_relaxed);
        bd = g_pickDesc;
    } else {
    AcquireSRWLockShared(&g_lock);
    for (const Ds& d : g_ds) {
        if (!d.res || !d.binds) continue;   // bound this frame only: a buffer not bound now may be freed already
        const uint32_t n = std::max(d.binds, d.last);
        const resource_desc rd = dev->get_resource_desc({d.res});
        if (rd.type != resource_type::texture_2d || (rd.usage & resource_usage::depth_stencil) == 0 || rd.texture.samples != 1) continue;
        const uint32_t rw = rd.texture.width, rh = rd.texture.height;
        if (rw == w && rh == h) {
            // [render] vr_resolution: a render size can be a shadow map's (2048, 4096). The scene's depth is bound with a colour
            // target, a shadow pass's never is: while a size is in force an exact match needs one.
            if (vrres::Active() && std::max(d.colour, d.colourLast) == 0) continue;
            if (n > std::max(best.binds, best.last)) { best = d; bd = rd; }
            continue;
        }
        const bool shadow = rw == rh && (rw & (rw - 1)) == 0;
        const double k = w ? (double)rw / w : 0.0;
        const bool shape = std::fabs((double)rw * h - (double)rh * w) <= 0.005 * (double)rw * h;
        if (!shadow && shape && k >= 0.49 && k <= 2.01 && n > std::max(scaled.binds, scaled.last)) { scaled = d; sd = rd; }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!best.res && scaled.res) { best = scaled; bd = sd; }
    if (best.res) {
        g_pickDesc = bd;   // (read only once the pick is stored below)
        g_pickState.store(best.state);
        const bool first = g_cache.last == 0;
        if (g_cache.Commit(best.res, best.binds > 0, w, h)) {   // two frames of counts behind it: kept until Check lets it go
            LogF("vrcam: own depth - picked the depth-stencil %p (%ux%u, bound %u times a frame) after %s: %s; cached - no counting "
                 "until it is destroyed, the swap chain is made again, the window or render size changes or the game stops binding it (picks %u, of them another buffer %u)",
                 (void*)best.res, bd.texture.width, bd.texture.height, std::max(best.binds, best.last), g_cache.why,
                 first ? "the first" : g_cache.sameAsBefore ? "the same buffer as before" : "ANOTHER buffer than before",
                 g_cache.picks, g_cache.others);
            WritePickStatus(best.res, bd.texture.width, bd.texture.height);
        }
    }
    }
    if (!best.res) return tell(1, "vrcam: own depth - no depth-stencil of the window's shape bound yet: no depth for the fog and the sky until one is");
    const uint32_t cw = bd.texture.width, ch = bd.texture.height;   // our copy: the source's own size
    const format tl = format_to_typeless(bd.texture.format);
    if (!g_copy.handle || g_copyDesc.texture.width != cw || g_copyDesc.texture.height != ch || g_copyDesc.texture.format != tl) {
        if (g_copy.handle) {
            if (replay::SkipWhileStuck("own depth's texture replacement (wait_idle)")) return;
            rt->get_command_queue()->wait_idle();
            if (g_srv.handle) dev->destroy_resource_view(g_srv);
            dev->destroy_resource(g_copy);
            g_srv = {0};
            g_copy = {0};
        }
        const resource_desc cd(resource_type::texture_2d, cw, ch, 1, 1, tl, 1, memory_heap::gpu_only,
                               resource_usage::shader_resource | resource_usage::copy_dest);
        if (!dev->create_resource(cd, nullptr, resource_usage::shader_resource, &g_copy)) {
            g_copy = {0};
            return tell(2, "vrcam: own depth - our depth texture could not be made: ReShade's choice stays");
        }
        if (!dev->create_resource_view(g_copy, resource_usage::shader_resource, resource_view_desc(SrvFormat(tl)), &g_srv)) {
            dev->destroy_resource(g_copy);
            g_copy = {0};
            g_srv = {0};
            return tell(3, "vrcam: own depth - no view of our depth texture: ReShade's choice stays");
        }
        g_copyDesc = cd;
        LogF("vrcam: own depth - our depth texture %ux%u (format %u, viewed as %u; the window %ux%u)", cw, ch, (unsigned)tl,
             (unsigned)SrvFormat(tl), w, h);
    }
    const resource src{best.res};
    const resource_usage from = best.state ? (resource_usage)best.state : resource_usage::depth_stencil_write;
    cl->barrier(src, from, resource_usage::copy_source);
    cl->barrier(g_copy, resource_usage::shader_resource, resource_usage::copy_dest);
    cl->copy_resource(src, g_copy);
    g_copiedNow = true;
    cl->barrier(src, resource_usage::copy_source, from);
    cl->barrier(g_copy, resource_usage::copy_dest, resource_usage::shader_resource);
    // ours alone: Generic Depth binds DEPTH. Once (see replay::g_depthBound): ReShade drains the GPU before each binding
    // (D3D12), and the copy above is what changes every pass, not the view.
    if (g_srv.handle != replay::g_depthBound) {
        rt->update_texture_bindings("D2RVR_DEPTH", g_srv, g_srv);
        replay::g_depthBound = g_srv.handle;
        replay::g_depthBinds.fetch_add(1, std::memory_order_relaxed);
    } else {
        replay::g_depthKept.fetch_add(1, std::memory_order_relaxed);
        // [render] fx_fence, a frame of one picture: its constants wait for the last frame's effects (a pair: the replay)
        // (eyes by turns, [stereo] turn_fx_fence: the last frame's effects AND overlays - their cameras are overwritten next)
        if (!replay::t_pairPass && FenceNow()) WaitLastFx(rt);
    }
    if (best.res != g_from) {
        g_from = best.res;
        LogF("vrcam: own depth - the effects read the game's depth-stencil %p (%ux%u%s, bound %u times a frame, state %X), "
             "bound to D2RVR_DEPTH, never ReShade's pick", (void*)best.res, cw, ch,
             cw == w && ch == h ? "" : " - not the window's size: the game's Resolution Scale", std::max(best.binds, best.last), (unsigned)from);
    }
    told = 0;
}

// [render] no_reshade (vr/native_d3d12.h): the same from the plugin's own hooks - the game's lists' depth-stencil binds and barriers, and before
// its present the copy, on our own list on its queue, kept for the eye made (xr::SetTurnDepth). No effect reads it here: nothing is bound.
// The states are D3D12's own (ReShade's resource_usage has the same values).
void OnBindNative(ID3D12Resource* ds, unsigned count) {
    if (!ds) return;
    const uint64_t r = (uint64_t)ds;
    if (!g_cache.Counting()) {
        if (g_cache.WantsSight()) g_cache.Seen(r);
        return;
    }
    if (!NativeDepthNow()) return;
    Count(r, count);
}
void OnBarrierNative(unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (!NativeDepthNow()) return;
    if (const uint64_t p = g_cache.pick.load(std::memory_order_relaxed)) {
        for (unsigned i = 0; i < n; ++i)
            if (b[i].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && (uint64_t)b[i].Transition.pResource == p)
                g_pickState.store((uint32_t)b[i].Transition.StateAfter, std::memory_order_relaxed);
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (unsigned i = 0; i < n; ++i) {
        if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) continue;
        for (Ds& d : g_ds)
            if (d.res && d.res == (uint64_t)b[i].Transition.pResource) d.state = (uint32_t)b[i].Transition.StateAfter;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
ID3D12Resource* g_nCopy = nullptr;   // our copy: the game's depth-stencil's size and typeless format, readable between copies (vr/native_depth.h)
DXGI_FORMAT g_nSrv = DXGI_FORMAT_UNKNOWN;   // ... read as
D3D12_RESOURCE_DESC g_nPickDesc{};
ID3D12Resource* g_nOld = nullptr;   // the copy before a size change: let go some presents later (the eye made may still read it on the GPU)
uint32_t g_nOldLeft = 0;
void BindNative(ID3D12Device* dev, ID3D12CommandQueue* q, uint32_t w, uint32_t h) {
    g_copiedNow = false;
    if (g_nOld && g_nOldLeft && --g_nOldLeft == 0) { g_nOld->Release(); g_nOld = nullptr; }
    static int told = -1;
    auto tell = [&](int what, const char* text) { if (told != what) { told = what; Log(text); } };
    if (!NativeDepthNow() || !dev || !q) return;
    if (const char* why = g_cache.Check(w, h)) {
        ClearCounts();
        LogF("vrcam: own depth - the pick let go: %s (counting again; no copy until a buffer the game binds this frame is picked)", why);
    }
    Ds best{}, scaled{};
    D3D12_RESOURCE_DESC bd{}, sd{};
    if (const uint64_t p = g_cache.pick.load()) {   // the cached pick: no look round
        if (!g_cache.CopyOk()) return tell(5, "vrcam: own depth - the picked buffer is not bound this frame: no copy (the last one stays)");
        best.res = p;
        best.state = g_pickState.load(std::memory_order_relaxed);
        bd = g_nPickDesc;
    } else {
        AcquireSRWLockShared(&g_lock);
        for (const Ds& d : g_ds) {
            if (!d.res || !d.binds) continue;   // bound this frame only: a buffer not bound now may be freed already
            const uint32_t n = std::max(d.binds, d.last);
            const D3D12_RESOURCE_DESC rd = ((ID3D12Resource*)d.res)->GetDesc();
            if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || !(rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) || rd.SampleDesc.Count != 1) continue;
            const uint32_t rw = (uint32_t)rd.Width, rh = rd.Height;
            if (rw == w && rh == h) {
                if (vrres::Active() && std::max(d.colour, d.colourLast) == 0) continue;   // (a render size can be a shadow map's: Bind)
                if (n > std::max(best.binds, best.last)) { best = d; bd = rd; }
                continue;
            }
            const bool shadow = rw == rh && (rw & (rw - 1)) == 0;
            const double k = w ? (double)rw / w : 0.0;
            const bool shape = std::fabs((double)rw * h - (double)rh * w) <= 0.005 * (double)rw * h;
            if (!shadow && shape && k >= 0.49 && k <= 2.01 && n > std::max(scaled.binds, scaled.last)) { scaled = d; sd = rd; }
        }
        ReleaseSRWLockShared(&g_lock);
        if (!best.res && scaled.res) { best = scaled; bd = sd; }
        if (best.res) {
            g_nPickDesc = bd;
            g_pickState.store(best.state);
            const bool first = g_cache.last == 0;
            if (g_cache.Commit(best.res, best.binds > 0, w, h)) {
                LogF("vrcam: own depth (no ReShade) - picked the depth-stencil %p (%llux%u, bound %u times a frame) after %s: %s (picks %u, of them "
                     "another buffer %u)", (void*)best.res, (unsigned long long)bd.Width, bd.Height, std::max(best.binds, best.last), g_cache.why,
                     first ? "the first" : g_cache.sameAsBefore ? "the same buffer as before" : "ANOTHER buffer than before", g_cache.picks, g_cache.others);
                WritePickStatus(best.res, (uint32_t)bd.Width, bd.Height);
            }
        }
    }
    if (!best.res) return tell(1, "vrcam: own depth - no depth-stencil of the window's shape bound yet: the eye not drawn only turned until one is");
    const DXGI_FORMAT tl = nativedepth::Typeless(bd.Format);
    if (tl == DXGI_FORMAT_UNKNOWN) return tell(6, "vrcam: own depth (no ReShade) - the scene's depth-stencil is of a format not known here: the eye not drawn only turned");
    const D3D12_RESOURCE_DESC have = g_nCopy ? g_nCopy->GetDesc() : D3D12_RESOURCE_DESC{};
    if (!g_nCopy || have.Width != bd.Width || have.Height != bd.Height || have.Format != tl) {
        if (g_nCopy) {
            if (g_nOld) g_nOld->Release();   // (two size changes within the wait: the GPU has long finished with the older one)
            g_nOld = g_nCopy;
            g_nOldLeft = 8;
            g_nCopy = nullptr;
        }
        g_nCopy = nativedepth::MakeCopy(dev, bd);
        if (!g_nCopy) return tell(2, "vrcam: own depth (no ReShade) - our depth texture could not be made: the eye not drawn only turned");
        g_nSrv = nativedepth::ReadAs(tl);
        LogF("vrcam: own depth (no ReShade) - our depth texture %llux%u (format %u, read as %u; the window %ux%u)", (unsigned long long)bd.Width,
             bd.Height, (unsigned)tl, (unsigned)g_nSrv, w, h);
    }
    if (replay::SkipWhileStuck("own depth's copy (no ReShade)")) return;
    ID3D12GraphicsCommandList* l = natived3d::OpenList(dev);
    if (!l) return tell(3, "vrcam: own depth (no ReShade) - our list on the game's queue was not free: no copy this frame");
    const D3D12_RESOURCE_STATES from = (D3D12_RESOURCE_STATES)best.state;   // (0: as the game's depth-write - nativedepth::RecordCopy)
    nativedepth::RecordCopy(l, (ID3D12Resource*)best.res, from, g_nCopy);
    natived3d::Submit(q);
    g_copiedNow = true;
    if (best.res != g_from) {
        g_from = best.res;
        LogF("vrcam: own depth (no ReShade) - the eye made is moved with the game's depth-stencil %p (%llux%u%s, bound %u times a frame, state %X)",
             (void*)best.res, (unsigned long long)bd.Width, bd.Height, bd.Width == w && bd.Height == h ? "" : " - not the window's size: the game's Resolution Scale",
             std::max(best.binds, best.last), (unsigned)from);
    }
    told = 0;
}
}  // namespace scenedepth

}  // namespace fx
}  // namespace vrc
