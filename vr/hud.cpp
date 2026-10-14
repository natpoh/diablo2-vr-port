// hud - the game's interface as its own layer.
//
// D2R draws the whole interface (orbs, belt, panels, dialogue, text) into one
// full-screen R16G16B16A16_FLOAT target, cleared to (0, 0, 0, 1) at the start
// of the frame: colour premultiplied, alpha = how much of the world shows
// through. The frame's last pass puts it over the world (world * a + ui).
// Found with uitrace (Ctrl+F10), 2026-10-04.
//
// [hud] hide: once the game has drawn the interface and moved on to another
// target, the layer is cleared back to (0, 0, 0, 1) on the same command list -
// still a render target there, so no state changes. 1 = the toolbar only: the
// map, labels over monsters, tooltips and shops stay (clearing the map's
// rectangle cut the labels in it). 2 = the whole interface. Later the same
// pieces are copied out for FlatVR.
//
// The pieces scale with the screen's HEIGHT and the game's interface size s
// ("Safe Screen Percent" / 100), measured from the layer's alpha at
// 1699x1274 / 80% and 1920x1200 / 100% (one rule fits both aspects):
//   toolbar - a strip the whole width, 0.189 H s tall plus the wings over it,
//             its bottom at H (0.5 + 0.5 s);
//   map     - the corner map, from the safe area's top-left corner,
//             0.533 H s wide and 0.377 H s tall.
//
// [hud] map 1: the map out of the picture, into a texture of its own. The game
// draws it as one run of draws under a scissor rectangle that is the map's
// (1920x1200: 0,53 - 640,452, 361 draws), and the labels over monsters - those
// over the map too - later under the full-screen scissor. So while the map's
// scissor is set, the interface's draws go to our own target of the same size
// and format (same pipelines), and back to the layer when the scissor changes:
// the map alone in ours, the layer keeps everything else.
//
// For FlatVR (game_hud_shared.h): whatever is taken out of the picture is
// copied into a shared texture of its own size when the game's list leaves the
// layer - the toolbar's strip from the layer just before it is emptied, the
// map's rectangle from our map target - and published in BodyWalkVR_GameHud.
// FlatVR hangs them in the room. "visible" says when the game is not drawing
// the piece itself: the toolbar is back in the picture with a panel open.
//
// From above and from behind (vrcam off, or third person) the pieces stay in
// the picture ([hud] classic 0, SetClassicView), at a size of their own
// ([hud] bar_zoom, map_zoom) and the toolbar at a place of its own ([hud_*]
// bar_x, bar_y). At 1 and in place the game draws them as ever; at any other
// size the piece is taken out exactly as for FlatVR - same shares - and
// D2R_DepthFog.fx draws the share back over the frame that much bigger
// (PictureNow): the toolbar from the middle of its bottom edge, the map from
// its corner. Only while vrcam says that effect is running (SetPictureReady),
// so a missing or switched-off effect never costs the picture its toolbar.
//
// [openxr] menu_3d (SetPanelOut, docs/plan_laser_menus.md step 2): a menu open in
// first person goes to the headset as a panel of its own. The whole layer is
// copied into a texture of ours when the game's list leaves it, and emptied: the
// frame's last pass puts nothing over the world, which goes to both eyes in
// stereo, and xr.cpp hangs the copy in front of the player. On FlatVR the same
// copy is a shared texture handed over as a third piece (FLATVR_HUD_KIND_MENU,
// slot 2) - only to a FlatVR whose reader block says it hangs one
// (FlatVrTakesMenu): an older FlatVR never sees it, and vrcam keeps the menu
// in the picture for it.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <reshade.hpp>
#include "game_device_only.h"

#include "game_hud_shared.h"
#include "hud_globes.h"   // where the game's own life and mana globes are in its interface layer (the wrists' medallions)
#include "hud_rects.h"    // the toolbar's strip in that layer (the box taken out of the picture), with the cut sliders
#include "hud_native.h"

#include "flatvr_menu_shared.h"   // FlatVR's menu piece, whichever FlatVR header the build sees

namespace hud {
namespace {

using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

std::atomic<int> g_hide{0};   // 0 nothing, 1 the toolbar, 2 everything
std::atomic<float> g_uiScale{1.0f};
std::atomic<float> g_cutUp{0.0f}, g_cutDown{0.0f};   // [hud] hide_up_pct / hide_down_pct: the toolbar's strip taller above / below, percent of the layer's height
std::atomic<bool> g_cutTint{false};                  // [debug] hide_strip_tint: the strip taken out is painted a see-through magenta instead, to see its edges
std::atomic<bool> g_menu{false};   // the main menu, or a panel (inventory, trade...) open: nothing is hidden then   // the game's interface size, 0.5..1
std::atomic<uint64_t> g_layer{0};   // the interface's resource, learnt from its clear
std::atomic<int> g_mapMode{0};     // [hud] map: 0 in the picture, 1 in its own texture
std::atomic<int> g_mapCorner{0};   // [hud] map_corner: 0 either top corner, 1 the left one, 2 the right one
std::atomic<bool> g_classic{false};   // the view is from above or behind and the pieces stay in the picture
std::atomic<float> g_barZoom{1.0f}, g_mapZoom{1.0f};   // [hud] bar_zoom, map_zoom: their size in the picture then
std::atomic<bool> g_picReady{false};  // the effect that draws them back runs (vrcam's fx)
std::atomic<bool> g_barMoved{false};   // [hud] bar_near: the toolbar nearer or farther in stereo (the shader shifts it per eye)
std::atomic<float> g_barOffX{0.0f}, g_barOffY{0.0f};   // [hud_*] bar_x / bar_y: the toolbar elsewhere on the screen, uv (+y down)
bool BarOffset() { return std::abs(g_barOffX.load()) > 0.0005f || std::abs(g_barOffY.load()) > 0.0005f; }
bool BarZoomed() { return std::abs(g_barZoom.load() - 1.0f) > 0.005f || g_barMoved.load() || BarOffset(); }
std::atomic<bool> g_mapMoved{false};   // [hud_floor] map_near: the map nearer or farther in stereo (the shader shifts it per eye)
std::atomic<float> g_mapOffX{0.0f}, g_mapOffY{0.0f};   // [hud_floor] map_x / map_y: the map elsewhere on the screen, uv (+y down)
bool MapOffset() { return std::abs(g_mapOffX.load()) > 0.0005f || std::abs(g_mapOffY.load()) > 0.0005f; }
bool MapZoomed() { return std::abs(g_mapZoom.load() - 1.0f) > 0.005f || g_mapMoved.load() || MapOffset(); }
// What is taken out of the picture now: for FlatVR in first person, to be drawn back at another size otherwise.
int HideNow() { return g_classic.load() ? (g_picReady.load() && BarZoomed() ? 1 : 0) : g_hide.load(); }
int MapNow() { return g_classic.load() ? (g_picReady.load() && MapZoomed() ? 1 : 0) : g_mapMode.load(); }
// MapNow at this frame's layer clear: the map goes to our target, or not, for the whole frame.
std::atomic<int> g_frameMap{0};
// From above in stereo: what is left of the interface (labels over monsters,
// the target's name, chat) taken out whole and drawn back by D2R_DepthFog.fx on
// a plane tilted like the ground - the near labels at the bottom come out with
// their monsters instead of staying on the screen's plane. SetLabels (vrcam).
// On the floor (VR F3) the same, drawn back where the game put them, only
// faded ([hud_floor] labels_alpha) - while vrcam cannot fade the label boxes
// through the game's own code.
std::atomic<bool> g_labels{false};
// The fog's mask (SetUiMask): the interface left over is copied for the effect as it is, NOT
// taken out - D2R_DepthFog.fx keeps its fog off whatever the layer covers (labels over
// monsters were fogged over the void, 2026-10-04) and the floor's key keeps the letters off
// the void (2026-10-06). Only when the labels are not out anyway.
std::atomic<bool> g_uiMask{false};
// The orb shown or hidden on its own: only when the game's map cannot be set
// (no widget service). Otherwise the "D2R: Map" action opens and closes the
// game's map and the orb follows what the game draws (g_mapDrawn), so it is
// hidden at the start whenever the game's map is.
std::atomic<bool> g_mapShown{true};
// The game drew its map in this frame's interface (its scissor was seen):
// with the map off in the game there is nothing to show, and an empty orb is
// not shown. Under g_lock; cleared by every Publish.
bool g_mapDrawn = false;
std::mutex g_lock;
// Lists that have the interface bound now: the view they draw it through, and
// whether the map's draws are being sent to our own target right now.
struct Drawing { resource_view layer{}; bool inMap = false; uint32_t w = 0, h = 0; };
std::unordered_map<command_list*, Drawing> g_drawing;

// The map's own target: as big as the layer, same format, cleared like it.
resource g_mapTex{};
resource_view g_mapRtv{};
uint32_t g_mapW = 0, g_mapH = 0;
const float kEmpty[4] = {0.0f, 0.0f, 0.0f, 1.0f};
// Our own target switches: if ReShade reports them back as binds, they are not the game's.
thread_local bool t_ours = false;
void Bind(command_list* cl, const resource_view& rtv) { t_ours = true; cl->bind_render_targets_and_depth_stencil(1, &rtv); t_ours = false; }

bool EnsureMapTarget(device* dev, uint32_t w, uint32_t h) {
    if (g_mapTex.handle && g_mapW == w && g_mapH == h) return true;
    if (g_mapRtv.handle) { dev->destroy_resource_view(g_mapRtv); g_mapRtv = {}; }
    if (g_mapTex.handle) { dev->destroy_resource(g_mapTex); g_mapTex = {}; }
    const resource_desc d(w, h, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_,
                          resource_usage::render_target | resource_usage::shader_resource | resource_usage::copy_source);
    if (!dev->create_resource(d, nullptr, resource_usage::render_target, &g_mapTex)) return false;
    if (!dev->create_resource_view(g_mapTex, resource_usage::render_target, resource_view_desc(format::r16g16b16a16_float), &g_mapRtv)) {
        dev->destroy_resource(g_mapTex); g_mapTex = {};
        return false;
    }
    g_mapW = w; g_mapH = h;
    return true;
}

rect Clamped(float l, float t, float r, float b, uint32_t w, uint32_t h) {
    return rect{std::max(0, (int32_t)l), std::max(0, (int32_t)t), std::min((int32_t)w, (int32_t)r), std::min((int32_t)h, (int32_t)b)};
}

// The toolbar (orbs, belt, skills): a strip across the whole width at the bottom - 0.215 H s tall (the bar is 0.189, the wings and the statues' horns over it), and the two
// [hud] hide_up_pct / hide_down_pct sliders' share more above and below (vr/hud_rects.h).
rect ToolbarRect(uint32_t w, uint32_t h) {
    const hudrects::Box b = hudrects::Toolbar(w, h, g_uiScale.load(), g_cutUp.load(), g_cutDown.load());
    return rect{b.left, b.top, b.right, b.bottom};
}

// The corner map, from the safe area's top-left corner.
rect MapRect(uint32_t w, uint32_t h) {
    const float s = g_uiScale.load(), H = (float)h;
    const float left = 0.5f * (float)w * (1.0f - s), top = 0.5f * H * (1.0f - s);
    return Clamped(left, top, left + 0.533f * H * s + 0.004f * H, top + 0.377f * H * s + 0.004f * H, w, h);
}

bool IsLayerClear(device* dev, resource_view rtv, const float c[4]) {
    if (c[0] != 0.0f || c[1] != 0.0f || c[2] != 0.0f || c[3] != 1.0f) return false;
    const resource r = dev->get_resource_from_view(rtv);
    const resource_desc d = dev->get_resource_desc(r);
    if (d.type != resource_type::texture_2d || d.texture.format != format::r16g16b16a16_float || d.texture.width < 640) return false;
    g_layer.store(r.handle);
    return true;
}

// The map's scissor: a box from the safe area's top-left corner (its top a
// little below it), an eighth to a half of the screen each way. Matched by
// that, not by MapRect's size: the height rule held at 1920x1200 and
// 1699x1274/80%, but at 1920x1440 the map is ~640 wide where it predicts 773,
// past the tolerance - the map stayed in the picture and the share was empty.
bool IsMapScissor(const rect& r, uint32_t w, uint32_t h) {
    const float s = g_uiScale.load();
    const float left = 0.5f * (float)w * (1.0f - s), top = 0.5f * (float)h * (1.0f - s);
    const float tol = 0.07f * (float)h;
    const int32_t rw = r.right - r.left, rh = r.bottom - r.top;
    // The game puts the mini map in either top corner (its own option): the
    // box hugs the safe area's left edge, or its right one.
    const float right = (float)w - left;
    const int which = g_mapCorner.load();
    const bool atLeft = std::abs(r.left - left) < tol, atRight = std::abs(r.right - right) < tol;
    const bool corner = which == 1 ? atLeft : which == 2 ? atRight : (atLeft || atRight);
    return corner && std::abs(r.top - top) < tol &&
           rw > (int32_t)w / 8 && rw < (int32_t)w / 2 && rh > (int32_t)h / 8 && rh < (int32_t)(h * 0.6f);
}

// The map scissor last seen, and the layer size it was seen at: what the
// share copies (the map is drawn only inside it). Under g_lock.
rect g_mapScissor{};
uint32_t g_mapScissorW = 0, g_mapScissorH = 0;

// ── The pieces handed to FlatVR ─────────────────────────────────────────────

enum { kBar = 0, kMap = 1, kShares = 2 };
// srv: the same texture for D2R_DepthFog.fx, which draws it back into the picture from above and behind
struct Share { resource tex{}; resource_view srv{}; HANDLE handle = nullptr; uint32_t w = 0, h = 0; bool fresh = true; };
Share g_share[kShares];
// The pieces' state for native OpenXR (NativeHud): out of the picture at the last pass, and when.
std::atomic<bool> g_outNow[kShares] = {};
std::atomic<ULONGLONG> g_outAt{0};
bool g_shareFailed = false;   // creating one failed: not tried again every frame
uint32_t g_frames = 0;        // interface passes seen, the clock the retired shares wait on

// A share replaced by one of another size is kept a while before it goes:
// FlatVR, across the process boundary, may still have GPU work reading it (the
// addon's RetireShare - destroying at once hung the driver there).
struct Retired { resource tex; resource_view srv; HANDLE handle; uint32_t at; };
std::vector<Retired> g_retired;

void DrainRetired(device* dev) {
    size_t keep = 0;
    for (const Retired& r : g_retired) {
        if (g_frames - r.at >= 120) {
            if (r.srv.handle) dev->destroy_resource_view(r.srv);
            if (r.handle) CloseHandle(r.handle);
            if (r.tex.handle) dev->destroy_resource(r.tex);
        } else {
            g_retired[keep++] = r;
        }
    }
    g_retired.resize(keep);
}

HANDLE g_blockMem = nullptr;
FlatVRGameHud* g_block = nullptr;
constexpr int kMenuSlot = 2;   // a menu's panel in the block (FLATVR_HUD_KIND_MENU)
// FlatVR hangs a menu panel: its reader block (FlatVRGameHudReader) says so and its counter moved
// within a second (PollMenuReader, vrcam's timer).
std::atomic<bool> g_flatVrMenu{false};
FlatVRGameHudMenu g_menuPlace{2.3f, 1.9f, 0.0f, 0, 0, {}};   // SetMenuPlace; under g_lock
// FlatVR's reader block, mapped by PollMenuReader once found, never unmapped: FlatVrLaserState reads it.
std::atomic<const FlatVRGameHudReader*> g_reader{nullptr};
// How FlatVR is to show them: [hud] in d2r_vr.ini (D2R VR Settings > Interface), via SetLook.
FlatVRGameHudLook g_look = FlatVRGameHudDefaultLook();
// The hero's forearm each panel lies on (SetForearm, from the skeleton), copied into the block with every Publish.
FlatVRGameHudPose g_pose[FLATVR_GAME_HUD_PANELS] = {};

FlatVRGameHud* Block() {
    if (g_block) return g_block;
    g_blockMem = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)kHudBlockSize, FLATVR_GAME_HUD_NAME);
    if (!g_blockMem) return nullptr;
    g_block = (FlatVRGameHud*)MapViewOfFile(g_blockMem, FILE_MAP_ALL_ACCESS, 0, 0, kHudBlockSize);
    if (!g_block) { CloseHandle(g_blockMem); g_blockMem = nullptr; return nullptr; }
    std::memset(g_block, 0, kHudBlockSize);   // (the menu's tail too)
    g_block->source_pid = GetCurrentProcessId();
    g_block->panel_count = kShares;
    g_block->panel[kBar].kind = FLATVR_HUD_KIND_TOOLBAR;
    g_block->panel[kMap].kind = FLATVR_HUD_KIND_MAP;
    g_block->panel_count = kMenuSlot + 1;   // (a FlatVR before the menu reads two)
    g_block->panel[kMenuSlot].kind = FLATVR_HUD_KIND_MENU;
    g_block->look = g_look;
    g_block->version = 3u;   // 4 only for a FlatVR that reads it (Publish): an older one drops a newer block
    Log("hud: publishing the toolbar and the map for FlatVR (BodyWalkVR_GameHud)");
    return g_block;
}

bool EnsureShare(device* dev, int i, uint32_t w, uint32_t h) {
    Share& s = g_share[i];
    if (s.tex.handle && s.w == w && s.h == h) return true;
    if (g_shareFailed) return false;
    FlatVRGameHud* b = Block();
    if (!b) return false;
    b->panel[i].visible = 0;
    b->panel[i].nt_handle = 0;
    if (s.tex.handle || s.handle) g_retired.push_back({s.tex, s.srv, s.handle, g_frames});
    s = Share{};
    const resource_desc d(w, h, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_,
                          resource_usage::copy_dest | resource_usage::shader_resource,
                          resource_flags::shared | resource_flags::shared_nt_handle);
    HANDLE handle = nullptr;
    if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &s.tex, &handle)) {
        s = Share{};
        g_shareFailed = true;
        Log("hud: the shared texture for FlatVR could not be created - the interface stays in the game only");
        return false;
    }
    s.handle = handle; s.w = w; s.h = h;
    if (!dev->create_resource_view(s.tex, resource_usage::shader_resource, resource_view_desc(format::r16g16b16a16_float), &s.srv))
        s.srv = {};   // FlatVR still gets it; only the picture's own copy (classic views) goes without
    b->panel[i].width = w;
    b->panel[i].height = h;
    b->panel[i].format = (uint32_t)format::r16g16b16a16_float;   // = DXGI_FORMAT_R16G16B16A16_FLOAT
    b->panel[i].nt_handle = (uint64_t)(uintptr_t)handle;
    return true;
}

// src (a render target now, on this list) at r -> share i. False when nothing was copied.
bool CopyOut(command_list* cl, resource src, const rect& r, int i) {
    if (r.right <= r.left || r.bottom <= r.top) return false;
    const uint32_t w = (uint32_t)(r.right - r.left), h = (uint32_t)(r.bottom - r.top);
    if (!EnsureShare(cl->get_device(), i, w, h)) return false;
    Share& s = g_share[i];
    const subresource_box from{(uint32_t)r.left, (uint32_t)r.top, 0, (uint32_t)r.right, (uint32_t)r.bottom, 1};
    const subresource_box to{0, 0, 0, w, h, 1};
    cl->barrier(src, resource_usage::render_target, resource_usage::copy_source);
    if (!s.fresh) cl->barrier(s.tex, resource_usage::shader_resource, resource_usage::copy_dest);   // made in copy_dest
    cl->copy_texture_region(src, 0, &from, s.tex, 0, &to, filter_mode::min_mag_mip_point);
    cl->barrier(s.tex, resource_usage::copy_dest, resource_usage::shader_resource);
    cl->barrier(src, resource_usage::copy_source, resource_usage::render_target);
    s.fresh = false;
    return true;
}

// The pieces to draw back into the picture (classic views), from the last Publish. Under g_lock.
struct Picture { bool on[kShares] = {}; float box[kShares][4] = {}; ULONGLONG at = 0; };
Picture g_pic;

// r grown z times about (ax, ay), in uv of a w x h screen: l, t, r, b.
void ZoomedUv(const rect& r, float ax, float ay, float z, uint32_t w, uint32_t h, float out[4]) {
    out[0] = (ax + ((float)r.left - ax) * z) / (float)w;
    out[1] = (ay + ((float)r.top - ay) * z) / (float)h;
    out[2] = (ax + ((float)r.right - ax) * z) / (float)w;
    out[3] = (ay + ((float)r.bottom - ay) * z) / (float)h;
}

// The interface left over, copied whole for the labels' plane (local: FlatVR never sees it). Under g_lock.
struct Labels { resource tex{}; resource_view srv{}; uint32_t w = 0, h = 0; bool fresh = true;
                bool on = false; float keep[2][4] = {}; ULONGLONG at = 0; };
Labels g_lab;

bool CopyLayer(command_list* cl, resource layer, uint32_t w, uint32_t h) {
    device* dev = cl->get_device();
    if (!g_lab.tex.handle || g_lab.w != w || g_lab.h != h) {
        if (g_lab.tex.handle) g_retired.push_back({g_lab.tex, g_lab.srv, nullptr, g_frames});   // the effect may still read it
        g_lab.tex = {}; g_lab.srv = {}; g_lab.fresh = true;
        const resource_desc d(w, h, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_,
                              resource_usage::copy_dest | resource_usage::shader_resource);
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &g_lab.tex)) { g_lab.tex = {}; return false; }
        if (!dev->create_resource_view(g_lab.tex, resource_usage::shader_resource, resource_view_desc(format::r16g16b16a16_float), &g_lab.srv)) {
            dev->destroy_resource(g_lab.tex); g_lab.tex = {}; g_lab.srv = {};
            return false;
        }
        g_lab.w = w; g_lab.h = h;
        Log("hud: the interface left over is copied for D2R_DepthFog.fx (labels on their plane from above, the fog's mask otherwise)");
    }
    cl->barrier(layer, resource_usage::render_target, resource_usage::copy_source);
    if (!g_lab.fresh) cl->barrier(g_lab.tex, resource_usage::shader_resource, resource_usage::copy_dest);
    cl->copy_resource(layer, g_lab.tex);
    cl->barrier(g_lab.tex, resource_usage::copy_dest, resource_usage::shader_resource);
    cl->barrier(layer, resource_usage::copy_source, resource_usage::render_target);
    g_lab.fresh = false;
    return true;
}

// The whole interface while a menu goes as a panel (SetPanelOut): copied for xr.cpp, which draws
// it into its swapchain on the game's queue after the pair, and shared as the toolbar is for FlatVR
// (slot kMenuSlot). Under g_lock.
std::atomic<bool> g_panelOut{false};
struct Panel { resource tex{}; HANDLE handle = nullptr; uint32_t w = 0, h = 0; bool fresh = true; };
Panel g_panel;
std::atomic<bool> g_panelNow{false};    // copied out (and the layer emptied) by the last interface pass
std::atomic<ULONGLONG> g_panelAt{0};   // and when
// The layer left more than once between two of its clears: the interface drawn in runs, the copy of
// a later run would lack what the earlier one's emptying took (to the log once - then d2r-pcvr's way,
// the final pass told to leave the interface out, is the one to take).
std::atomic<int> g_panelRuns{0};

bool CopyPanel(command_list* cl, resource layer, uint32_t w, uint32_t h) {
    device* dev = cl->get_device();
    if (!g_panel.tex.handle || g_panel.w != w || g_panel.h != h) {
        if (g_panel.tex.handle) g_retired.push_back({g_panel.tex, {}, g_panel.handle, g_frames});   // xr.cpp's list, FlatVR may still read it
        g_panel = Panel{};
        if (g_block) { g_block->panel[kMenuSlot].visible = 0; g_block->panel[kMenuSlot].nt_handle = 0; }
        const resource_desc d(w, h, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_,
                              resource_usage::copy_dest | resource_usage::shader_resource,
                              resource_flags::shared | resource_flags::shared_nt_handle);
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &g_panel.tex, &g_panel.handle)) {
            g_panel = Panel{};
            static bool told = false;
            if (!told) { told = true; Log("hud: the menu's panel texture could not be made - the menu stays in the picture"); }
            return false;
        }
        g_panel.w = w; g_panel.h = h;
        char line[128];
        snprintf(line, sizeof line, "hud: a menu as a panel of its own ([openxr] menu_3d): the interface copied out, %ux%u", w, h);
        Log(line);
    }
    cl->barrier(layer, resource_usage::render_target, resource_usage::copy_source);
    if (!g_panel.fresh) cl->barrier(g_panel.tex, resource_usage::shader_resource, resource_usage::copy_dest);
    cl->copy_resource(layer, g_panel.tex);
    cl->barrier(g_panel.tex, resource_usage::copy_dest, resource_usage::shader_resource);
    cl->barrier(layer, resource_usage::copy_source, resource_usage::render_target);
    g_panel.fresh = false;
    return true;
}

// The game's own life and mana globes, cut out of the interface layer for the wrists (vr/hud_globes.h): ONE texture, the life globe's square on the
// left and the mana globe's on the right, copied while vrcam asks for them (SetGlobes) and the effect runs. Local: FlatVR never sees it. Under g_lock.
std::atomic<bool> g_globesWant{false};
struct Globes { resource tex{}; resource_view srv{}; uint32_t w = 0, h = 0; bool fresh = true; bool on = false; float fit = 0.0f; ULONGLONG at = 0; };
Globes g_glb;
std::atomic<int> g_globesWhy{0};   // why nothing was cut last time: 0 cut, 1 not asked, 2 the layer's size or the safe area's, 3 a texture could not be made

bool CopyGlobes(command_list* cl, resource layer, uint32_t w, uint32_t h) {
    const hudglobes::Boxes b = hudglobes::Compute(w, h, g_uiScale.load());
    if (!b.ok) { g_globesWhy.store(2); return false; }
    device* dev = cl->get_device();
    const uint32_t side = (uint32_t)(2 * b.half), tw = 2 * side, th = side;
    if (!g_glb.tex.handle || g_glb.w != tw || g_glb.h != th) {
        if (g_glb.tex.handle) g_retired.push_back({g_glb.tex, g_glb.srv, nullptr, g_frames});   // the effect may still read it
        g_glb.tex = {}; g_glb.srv = {}; g_glb.fresh = true; g_glb.on = false;
        const resource_desc d(tw, th, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_, resource_usage::copy_dest | resource_usage::shader_resource);
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &g_glb.tex)) { g_glb.tex = {}; g_globesWhy.store(3); return false; }
        if (!dev->create_resource_view(g_glb.tex, resource_usage::shader_resource, resource_view_desc(format::r16g16b16a16_float), &g_glb.srv)) {
            dev->destroy_resource(g_glb.tex); g_glb.tex = {}; g_glb.srv = {}; g_globesWhy.store(3);
            return false;
        }
        g_glb.w = tw; g_glb.h = th;
        char line[200];
        snprintf(line, sizeof line, "hud: the game's own life and mana globes are cut out of its interface for the wrists: two %ux%u squares at %d,%d and %d,%d on a %ux%u layer (disc radius %.0f px)",
                 side, side, b.left[0], b.top, b.left[1], b.top, w, h, b.radius);
        Log(line);
    }
    cl->barrier(layer, resource_usage::render_target, resource_usage::copy_source);
    if (!g_glb.fresh) cl->barrier(g_glb.tex, resource_usage::shader_resource, resource_usage::copy_dest);   // made in copy_dest
    for (int i = 0; i < 2; ++i) {
        const subresource_box from{(uint32_t)b.left[i], (uint32_t)b.top, 0, (uint32_t)b.left[i] + side, (uint32_t)b.top + side, 1};
        const subresource_box to{(uint32_t)i * side, 0, 0, (uint32_t)i * side + side, side, 1};
        cl->copy_texture_region(layer, 0, &from, g_glb.tex, 0, &to, filter_mode::min_mag_mip_point);
    }
    cl->barrier(g_glb.tex, resource_usage::copy_dest, resource_usage::shader_resource);
    cl->barrier(layer, resource_usage::copy_source, resource_usage::render_target);
    g_glb.fresh = false;
    g_glb.fit = b.fit;
    g_glb.at = GetTickCount64();
    g_glb.on = true;
    g_globesWhy.store(0);
    return true;
}

// The interface of this frame is drawn and the toolbar not yet emptied: copy both pieces out.
void Publish(command_list* cl, resource layer, uint32_t w, uint32_t h, int hide) {
    ++g_frames;
    device* dev = cl->get_device();
    DrainRetired(dev);
    const bool menu = g_menu.load() || g_panelOut.load(), classic = g_classic.load();   // a menu's panel takes the toolbar with it
    bool bar = false, map = false;
    const rect barBox = ToolbarRect(w, h);
    rect mapBox{};
    if (hide >= 1 && !menu) bar = CopyOut(cl, layer, barBox, kBar);
    if (g_globesWant.load() && g_picReady.load()) CopyGlobes(cl, layer, w, h);   // (the layer is left as it is: the toolbar stays or goes by [hud] hide)
    else { g_glb.on = false; g_globesWhy.store(1); }
    const bool drawn = g_mapDrawn;
    g_mapDrawn = false;
    // the parts the labels' plane leaves flat: the toolbar's strip, and the map's box while it is drawn
    {
        const rect m = g_mapScissorW == w && g_mapScissorH == h ? g_mapScissor : MapRect(w, h);
        const rect* keep[2] = {&barBox, &m};
        for (int k = 0; k < 2; ++k) {
            g_lab.keep[k][0] = (float)keep[k]->left / (float)w; g_lab.keep[k][1] = (float)keep[k]->top / (float)h;
            g_lab.keep[k][2] = (float)keep[k]->right / (float)w; g_lab.keep[k][3] = (float)keep[k]->bottom / (float)h;
        }
        if (!drawn) for (float& v : g_lab.keep[1]) v = 0.0f;
    }
    if (drawn && g_frameMap.load() == 1 && g_mapTex.handle && g_mapW == w && g_mapH == h) {
        // the box the game drew the map in, once seen; the predicted one until then
        const rect box = g_mapScissorW == w && g_mapScissorH == h ? g_mapScissor : MapRect(w, h);
        mapBox = Clamped((float)box.left, (float)box.top, (float)box.right, (float)box.bottom, w, h);
        map = CopyOut(cl, g_mapTex, mapBox, kMap);
    }
    // From above and behind: back into the picture, grown from the toolbar's
    // bottom middle and from the map's own corner (left or right, by its middle).
    g_pic.on[kBar] = classic && bar && g_share[kBar].srv.handle;
    g_pic.on[kMap] = classic && map && g_share[kMap].srv.handle;
    g_pic.at = GetTickCount64();
    if (g_pic.on[kBar]) {
        ZoomedUv(barBox, 0.5f * (float)(barBox.left + barBox.right), (float)barBox.bottom, g_barZoom.load(), w, h, g_pic.box[kBar]);
        const float dx = g_barOffX.load(), dy = g_barOffY.load();   // then moved, whole
        g_pic.box[kBar][0] += dx; g_pic.box[kBar][2] += dx;
        g_pic.box[kBar][1] += dy; g_pic.box[kBar][3] += dy;
    }
    if (g_pic.on[kMap]) {
        const bool left = mapBox.left + mapBox.right < (int32_t)w;
        ZoomedUv(mapBox, (float)(left ? mapBox.left : mapBox.right), (float)mapBox.top, g_mapZoom.load(), w, h, g_pic.box[kMap]);
        const float dx = g_mapOffX.load(), dy = g_mapOffY.load();   // then moved, whole
        g_pic.box[kMap][0] += dx; g_pic.box[kMap][2] += dx;
        g_pic.box[kMap][1] += dy; g_pic.box[kMap][3] += dy;
    }
    g_outNow[kBar].store(bar && !classic);
    g_outNow[kMap].store(map && !classic && g_mapShown.load());
    g_outAt.store(GetTickCount64());
    if (FlatVRGameHud* b = (bar || map || g_block) ? Block() : nullptr) {
        // in the picture from above and behind: nothing in the room
        b->panel[kBar].visible = bar && !classic ? 1u : 0u;
        b->panel[kMap].visible = map && !classic && g_mapShown.load() ? 1u : 0u;
        b->look = g_look;
        for (int i = 0; i < kShares; ++i) b->pose[i] = g_pose[i];
        b->version = g_flatVrMenu.load() ? kHudVersionMenu : 3u;
        MenuOf(b) = g_menuPlace;
        b->counter = b->counter + 1;
    }
}

bool OnClear(command_list* cl, resource_view rtv, const float c[4], uint32_t, const rect*) {
    device* dev = cl->get_device();
    if (!IsLayerClear(dev, rtv, c)) return false;
    g_panelRuns.store(0);
    g_frameMap.store(MapNow());
    if (g_frameMap.load() == 1) {   // the frame's interface starts: so does the map's
        const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(rtv));
        std::lock_guard<std::mutex> g(g_lock);
        if (EnsureMapTarget(dev, d.texture.width, d.texture.height)) cl->clear_render_target_view(g_mapRtv, kEmpty);
    }
    return false;
}

// On the interface's list: the map's scissor sends the draws to our target, any other back to the layer.
void OnScissor(command_list* cl, uint32_t, uint32_t count, const rect* rects) {
    const bool redirect = g_frameMap.load() == 1;   // else only seen: the labels keep the map flat
    if (count == 0 || (!redirect && !g_labels.load())) return;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_drawing.find(cl);
    if (it == g_drawing.end()) return;
    Drawing& d = it->second;
    const bool map = IsMapScissor(rects[0], d.w, d.h);
    if (map && (rects[0].left != g_mapScissor.left || rects[0].top != g_mapScissor.top ||
                rects[0].right != g_mapScissor.right || rects[0].bottom != g_mapScissor.bottom)) {
        char line[160];
        snprintf(line, sizeof line, "hud: the map's box %d,%d - %d,%d on a %ux%u layer", rects[0].left, rects[0].top, rects[0].right, rects[0].bottom, d.w, d.h);
        Log(line);
    }
    if (map) { g_mapScissor = rects[0]; g_mapScissorW = d.w; g_mapScissorH = d.h; g_mapDrawn = true; }
    if (!redirect || !g_mapRtv.handle) return;
    if (map && !d.inMap) { Bind(cl, g_mapRtv); d.inMap = true; }
    else if (!map && d.inMap) { Bind(cl, d.layer); d.inMap = false; }
}

// The list leaves the interface for another target: the interface is drawn - empty it.
void OnBind(command_list* cl, uint32_t count, const resource_view* rtvs, resource_view) {
    const uint64_t layer = g_layer.load();
    if (!layer || t_ours) return;
    device* dev = cl->get_device();
    resource_view now{};
    for (uint32_t i = 0; i < count; ++i)
        if (rtvs[i].handle && dev->get_resource_from_view(rtvs[i]).handle == layer) now = rtvs[i];
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_drawing.find(cl);
    if (it != g_drawing.end() && it->second.layer.handle != now.handle) {
        const resource_view layerRtv = it->second.layer;
        const int hideNow = HideNow();
        const bool panel = g_panelOut.load();
        {
            const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(layerRtv));
            Publish(cl, dev->get_resource_from_view(layerRtv), d.texture.width, d.texture.height, hideNow);
        }
        if (panel) {   // a menu as a panel: all of it out, the world alone in the picture
            if (g_panelRuns.fetch_add(1) == 1) {
                static bool told = false;
                if (!told) { told = true; Log("hud: the interface's layer was drawn in more than one run this frame - the menu's panel may lack parts"); }
            }
            const bool out = CopyPanel(cl, dev->get_resource_from_view(layerRtv), it->second.w, it->second.h);
            if (out) cl->clear_render_target_view(layerRtv, kEmpty);
            g_panelNow.store(out);
            g_panelAt.store(GetTickCount64());
            if (FlatVRGameHud* b = g_flatVrMenu.load() || g_block ? Block() : nullptr) {   // for FlatVR, as the toolbar
                FlatVRGameHudPanel& m = b->panel[kMenuSlot];
                m.nt_handle = (uint64_t)(uintptr_t)g_panel.handle;
                m.width = g_panel.w;
                m.height = g_panel.h;
                m.format = (uint32_t)format::r16g16b16a16_float;   // = DXGI_FORMAT_R16G16B16A16_FLOAT
                m.kind = FLATVR_HUD_KIND_MENU;
                m.visible = out && g_panel.handle ? 1u : 0u;
            }
        } else if (const int hide = g_menu.load() ? 0 : hideNow; hide == 2) {
            cl->clear_render_target_view(layerRtv, kEmpty);
        } else if (hide == 1 && (!g_classic.load() || g_pic.on[kBar])) {   // drawn back from a copy: only if there is one
            const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(layerRtv));
            const rect bar = ToolbarRect(d.texture.width, d.texture.height);   // the map stays (MapRect: kept for FlatVR)
            // (premultiplied, alpha the picture's transmittance: kTint lets 70 % of the world through under a magenta veil - the strip's edges, to tune the two cut sliders by)
            static const float kTint[4] = {0.30f, 0.0f, 0.30f, 0.70f};
            cl->clear_render_target_view(layerRtv, g_cutTint.load() ? kTint : kEmpty, 1, &bar);
        }
        // From above: the rest of it out whole, for the effect to lay on the tilted plane.
        if (!panel) {
            g_panelNow.store(false);
            if (g_block) g_block->panel[kMenuSlot].visible = 0;
        }
        const bool labels = g_labels.load() && g_picReady.load() && !g_menu.load() && !panel;
        const bool mask = !labels && g_uiMask.load() && g_picReady.load() && !g_menu.load() && !panel;   // copied, left in
        g_lab.on = (labels || mask) && CopyLayer(cl, dev->get_resource_from_view(layerRtv), it->second.w, it->second.h);
        if (g_lab.on && labels) cl->clear_render_target_view(layerRtv, kEmpty);
        g_lab.at = GetTickCount64();
        g_drawing.erase(it);
    }
    if (now.handle) {
        const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(now));
        g_drawing[cl] = Drawing{now, false, d.texture.width, d.texture.height};
    }
}

// Reset, and made or let go (2026-10-13): a list let go while it had the interface bound left
// its entry, and a new list at the same address - D3D12 makes one recording, with no reset -
// took it for its own: the layer's old view published, cleared, sent to the map.
void OnReset(command_list* cl) {
    std::lock_guard<std::mutex> g(g_lock);
    g_drawing.erase(cl);
}

}  // namespace

void Register() {
    reshade::register_event<reshade::addon_event::clear_render_target_view>(&d2rvr::D3D12Only<&OnClear>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBind>::Call);
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::init_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::destroy_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(&d2rvr::D3D12Only<&OnScissor>::Call);
}

void SetHide(int mode) { g_hide.store(mode); }
void SetMenuOpen(bool open) { g_menu.store(open); }
void SetMapMode(int mode) { g_mapMode.store(mode); }
void SetInterfaceScale(float s) { g_uiScale.store(std::clamp(s, 0.3f, 1.0f)); }
void SetToolbarCut(float upPct, float downPct, bool tint) {
    g_cutUp.store(hudrects::UpPct(upPct));
    g_cutDown.store(hudrects::DownPct(downPct));
    g_cutTint.store(tint);
}
bool LayerFound() { return g_layer.load() != 0; }
void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetMapCorner(int corner) { g_mapCorner.store(std::clamp(corner, 0, 2)); }

// From above and from behind, with [hud] classic 0: the pieces in the picture at their own size.
void SetClassicView(bool on) { g_classic.store(on); }
// The toolbar is to stand nearer or farther than the game puts it (AFR stereo): taken out for that too.
void SetPictureBarMoved(bool moved) { g_barMoved.store(moved); }
void SetPictureZoom(float bar, float map) { g_barZoom.store(std::clamp(bar, 0.2f, 3.0f)); g_mapZoom.store(std::clamp(map, 0.3f, 3.0f)); }
// ... and moved on the screen from where the game puts it, uv (+x right, +y down): taken out for that too.
void SetPictureBarOffset(float x, float y) { g_barOffX.store(std::clamp(x, -1.0f, 1.0f)); g_barOffY.store(std::clamp(y, -1.0f, 1.0f)); }
// The same for the map (F3, the game on the floor): nearer or farther, and moved.
void SetPictureMapMoved(bool moved) { g_mapMoved.store(moved); }
void SetPictureMapOffset(float x, float y) { g_mapOffX.store(std::clamp(x, -1.0f, 1.0f)); g_mapOffY.store(std::clamp(y, -1.0f, 1.0f)); }
// Some piece is to be drawn back at another size: vrcam keeps the effect on for it.
bool PictureWanted() { return (g_classic.load() && (BarZoomed() || MapZoomed())) || g_labels.load(); }
// From above in stereo, with a tilt or a depth for them: the labels on their plane (vrcam decides when).
void SetLabels(bool on) { g_labels.store(on); }
// The interface copied as the fog's mask (left in the picture) while the labels are not out.
void SetUiMask(bool on) { g_uiMask.store(on); }
bool UiMaskNow() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_lab.on && !g_labels.load() && g_uiMask.load() && g_lab.srv.handle && GetTickCount64() - g_lab.at < 250;
}
// For D2R_DepthFog.fx: the interface left over this frame, and the boxes (uv) it keeps flat - the toolbar, the map.
bool LabelsNow(float keepBar[4], float keepMap[4], uint64_t* srv) {
    std::lock_guard<std::mutex> g(g_lock);
    *srv = g_lab.srv.handle;
    for (int k = 0; k < 4; ++k) { keepBar[k] = g_lab.keep[0][k]; keepMap[k] = g_lab.keep[1][k]; }
    return g_lab.on && g_labels.load() && *srv && GetTickCount64() - g_lab.at < 250;
}
// The effect that draws them back is running; until it is, nothing is taken out for it.
void SetPictureReady(bool ready) { g_picReady.store(ready); }

// For D2R_DepthFog.fx: piece i (0 toolbar, 1 map) - whether to draw it, where
// (uv l, t, r, b) and from which view. Off once no interface came for a while.
bool PictureNow(int i, float box[4], uint64_t* srv) {
    if (i < 0 || i >= kShares) return false;
    std::lock_guard<std::mutex> g(g_lock);
    *srv = g_share[i].srv.handle;
    const bool on = g_pic.on[i] && g_classic.load() && GetTickCount64() - g_pic.at < 250;
    for (int k = 0; k < 4; ++k) box[k] = g_pic.box[i][k];
    return on && *srv;
}
// The wrists' medallions from the game's own globes (vr/hud_globes.h): vrcam asks while the look is on and the effect runs; the effect gets the texture
// (the life globe's square on its left half, the mana globe's on the right) and the disc's radius over half a square. False until a cut came, and a
// quarter of a second after the last one (no interface pass: a menu, a load).
void SetGlobes(bool want) { g_globesWant.store(want); }
bool GlobesNow(float* fit, uint64_t* srv) {
    std::lock_guard<std::mutex> g(g_lock);
    *srv = g_glb.srv.handle;
    *fit = g_glb.fit;
    return g_glb.on && *srv && GetTickCount64() - g_glb.at < 250;
}
// Why GlobesNow is false: 0 it is not (a cut is there), 1 not asked (or the effect is not running), 2 the layer's size / the safe area do not hold the globes whole, 3 a texture failed.
int GlobesWhy() { return g_globesWhy.load(); }
bool ToggleMapShown() { const bool now = !g_mapShown.load(); g_mapShown.store(now); return now; }
// The Windows pointer in the headset (FlatVRGameHudLook::pointer): 1 whenever shown, 2 never.
void SetPointer(uint32_t mode) {
    std::lock_guard<std::mutex> g(g_lock);
    g_look.pointer = mode;
    if (g_block) g_block->look.pointer = mode;
}

// The pointer's depth in FlatVR's stereo pair (FlatVRGameHud::pointer_depth): per eye base + tilt * v of an eye's width.
void SetPointerDepth(float base, float tilt, float curve, float topScale) {
    std::lock_guard<std::mutex> g(g_lock);
    if (g_block) {
        g_block->pointer_depth.base = base; g_block->pointer_depth.tilt = tilt; g_block->pointer_depth.curve = curve;
        g_block->pointer_depth.top_scale = topScale;
    }
}

// Where FlatVR is told to hang panel 0 (toolbar) / 1 (map): FlatVRGameHudLook's anchors.
int Anchor(int panel) {
    std::lock_guard<std::mutex> g(g_lock);
    return (int)(panel == 0 ? g_look.bar_anchor : g_look.map_anchor);
}

void SetForearm(int panel, bool valid, const float elbow[3], const float wrist[3], const float across[3]) {
    if (panel < 0 || panel >= kShares) return;
    std::lock_guard<std::mutex> g(g_lock);
    FlatVRGameHudPose& p = g_pose[panel];
    p.valid = valid ? 1u : 0u;
    if (!valid) return;
    for (int i = 0; i < 3; ++i) { p.elbow[i] = elbow[i]; p.wrist[i] = wrist[i]; p.across[i] = across[i]; }
}

void SetLook(const FlatVRGameHudLook& look) {
    std::lock_guard<std::mutex> g(g_lock);
    // the pointer mode is the view's (SetPointer), not the settings': a settings reload put 0 there
    // until the next tick and the pointer blinked between FlatVR's rule and "never" (2026-10-06)
    const uint32_t pointer = g_look.pointer;
    g_look = look;
    g_look.pointer = pointer;
    if (g_block) g_block->look = g_look;
}

bool NativeHud(NativePiece out[kNativePieces], FlatVRGameHudLook* look, FlatVRGameHudPose* barPose) {
    const ULONGLONG at = g_outAt.load();
    if (!at) return false;
    const bool fresh = GetTickCount64() - at < 500;   // no interface pass for half a second: nothing out
    std::lock_guard<std::mutex> g(g_lock);
    for (int i = 0; i < kNativePieces && i < kShares; ++i) {
        out[i].tex = (ID3D12Resource*)(uintptr_t)g_share[i].tex.handle;
        out[i].w = g_share[i].w;
        out[i].h = g_share[i].h;
        out[i].visible = fresh && out[i].tex && !g_share[i].fresh && g_outNow[i].load();
    }
    NativePiece& p = out[kNativePanel];
    p.tex = (ID3D12Resource*)(uintptr_t)g_panel.tex.handle;
    p.w = g_panel.w;
    p.h = g_panel.h;
    p.visible = p.tex && !g_panel.fresh && g_panelOut.load() && g_panelNow.load() && GetTickCount64() - g_panelAt.load() < 500;
    *look = g_look;
    *barPose = g_pose[kBar];
    return true;
}

void SetPanelOut(bool out) {
    if (g_panelOut.exchange(out) != out)
        Log(out ? "hud: a menu open - the interface goes out of the picture as a panel ([openxr] menu_3d)"
                : "hud: the menu's panel over - the interface back as before");
}

// FlatVR's reader block (game_hud_shared.h, FlatVRGameHudReader), looked at by vrcam's timer: a FlatVR
// that hangs a menu panel and is drawing (its counter moved within a second). Without one - an older
// FlatVR, or none - vrcam keeps the menus in the picture on FlatVR.
void PollMenuReader() {
    static HANDLE map = nullptr;
    static const FlatVRGameHudReader* r = nullptr;
    static ULONGLONG nextTry = 0, movedAt = 0;
    static uint32_t last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!r && now >= nextTry) {
        nextTry = now + 500;
        if ((map = OpenFileMappingW(FILE_MAP_READ, FALSE, FLATVR_GAME_HUD_READER_NAME)) != nullptr) {
            r = (const FlatVRGameHudReader*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(FlatVRGameHudReader));
            if (!r) { CloseHandle(map); map = nullptr; }
            else g_reader.store(r);
        }
    }
    bool ok = false;
    if (r) {
        const uint32_t c = r->counter;
        if (c != last) { last = c; movedAt = now; }
        ok = r->version >= 4 && (r->kinds & (1u << FLATVR_HUD_KIND_MENU)) != 0 && now - movedAt < 1000;
    }
    if (g_flatVrMenu.exchange(ok) != ok)
        Log(ok ? "hud: FlatVR hangs a menu as a panel of its own (its reader block) - [openxr] menu_3d works on FlatVR"
               : "hud: no FlatVR that hangs a menu panel is drawing now - on FlatVR the menus stay in the picture");
}
bool FlatVrTakesMenu() { return g_flatVrMenu.load(); }
void SetMenuPlace(float widthM, float distanceM, float belowM, uint32_t laser, uint32_t window) {
    std::lock_guard<std::mutex> g(g_lock);
    g_menuPlace = FlatVRGameHudMenu{widthM, distanceM, belowM, laser, window, {}};
}
int FlatVrLaserState() {
    const FlatVRGameHudReader* r = g_reader.load();
    return r && g_flatVrMenu.load() ? (int)((const volatile FlatVRGameHudReader*)r)->laser : 0;
}

}  // namespace hud
