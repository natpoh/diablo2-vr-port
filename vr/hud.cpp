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
#include <cmath>
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
#include "notice_card.h"  // [hud] notice_card: the box of the game's quest / level-up notice in that layer
#include "minimap_box.h"  // which scissor box is the corner map, at any layer size, shape and interface size
#include "map_direction.h"   // [hud] map_direction: the arrow on the hero's marker of the game's map (rows of pixels)
#include "hud_native.h"
#include "plate_world.h"   // [hud] monster_bar_room: where the plate stands in the layer (BoxOf)

#include "flatvr_menu_shared.h"   // FlatVR's menu piece, whichever FlatVR header the build sees

namespace hud {
namespace {

using namespace reshade::api;

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

std::atomic<int> g_hide{0};   // 0 nothing, 1 the toolbar, 2 everything
std::atomic<float> g_uiScale{1.0f};
std::atomic<float> g_gameUiScale{0.0f};   // the game's own interface scale, pixels a layout unit (SetGameInterfaceScale; 0 not known): where the map's box is looked for
std::atomic<float> g_projRatio{0.0f};     // F4: our projection's x scale over its y scale (SetMapProjection; 0 the game's own shape): how wide the map's box is drawn
std::atomic<float> g_cutUp{0.0f}, g_cutDown{0.0f};   // [hud] hide_up_pct / hide_down_pct: the toolbar's strip taller above / below, percent of the layer's height
std::atomic<bool> g_cutTint{false};                  // [debug] hide_strip_tint: the strip taken out is painted a see-through magenta instead, to see its edges
std::atomic<bool> g_menu{false};   // the main menu, or a panel (inventory, trade...) open: nothing is hidden then   // the game's interface size, 0.5..1
std::atomic<uint64_t> g_layer{0};   // the interface's resource, learnt from its clear
std::atomic<int> g_mapMode{0};     // [hud] map: 0 in the picture, 1 in its own texture
std::atomic<int> g_mapCorner{0};   // [hud] map_corner: 0 either top corner, 1 the left one, 2 the right one
std::atomic<bool> g_panelNoBar{false};   // [hud] npc_dialog_no_bar: an NPC's dialogue box is the only menu - its panel without the toolbar
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
// ... and through a menu (flat: the panel stays over the world, and the sky and the fog with it - vrcam EffectsPaused): the fog etc. want it
// there, whether the labels are wanted or not (they are not taken out in a menu).
std::atomic<bool> g_uiMaskInMenu{false};
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
// whether the map's draws are being sent to our own target right now. vx..vh: the
// largest viewport bound since (OnViewports), the canvas the map's box is in.
// mapRun: the map's scissor is the last one bound - the map is being drawn ([hud] map_direction: its arrow when it is done).
struct Drawing { resource_view layer{}; bool inMap = false; uint32_t w = 0, h = 0; float vx = 0.0f, vy = 0.0f, vw = 0.0f, vh = 0.0f; bool mapRun = false; };
std::unordered_map<command_list*, Drawing> g_drawing;

// [hud] map_direction (vr/map_direction.h): an arrow beside the hero's marker in the middle of the game's map, the way our camera looks.
// Painted with clears on the target the map went to, once its draws are done (its scissor left): in the picture, and in our map target
// - so the hand's map, the orb and FlatVR's piece have it too. g_dirWant: vrcam says so (ticked, one of our camera's views, a game);
// g_dirFull: the game's map is the whole-screen one (its AutoMap open, mode 0) - no box of its own, the arrow in the middle of the
// interface's canvas when no corner map was drawn. The way on the ground (mapdir::OnGround: the game screen's right and up, before the
// map's projection) from vrcam's last view; one older than half a second (our camera off) is not drawn.
std::atomic<bool> g_dirWant{false}, g_dirFull{false};
std::atomic<float> g_dirX{0.0f}, g_dirY{1.0f};
std::atomic<ULONGLONG> g_dirAt{0};
// [hud] map_rotate / map_compass (vr/map_turn.h, xr.cpp): the same way for the map in the hand, the head's turn held while it looks down at
// it (vrcam, native F4 only). Its own: the arrow's way keeps going stale as before.
std::atomic<float> g_turnX{0.0f}, g_turnY{1.0f};
std::atomic<ULONGLONG> g_turnAt{0};
// [hud] map_click_big (vr/map_laser.h, xr.cpp HangBigMap): the map made big - the game's whole-screen map, the whole interface copied out
// as a menu's panel is (kNativePanel) and the layer emptied, for xr.cpp to hang big in front. Set by xr.cpp.
std::atomic<bool> g_mapBig{false};
std::atomic<uint32_t> g_mapBigRows{0};   // the big map's rows in the panel's copy: the layer above the toolbar's strip (OnBind)
// ... the hero on it: the canvas's middle (as ArrowOnWholeMap has it), uv of the layer; and how much wider than its own shape it is drawn
std::atomic<float> g_mapBigHeroU{0.5f}, g_mapBigHeroV{0.5f}, g_mapBigStretch{1.0f};
const float kArrowRim[4] = {0.02f, 0.02f, 0.02f, 0.0f};    // premultiplied, alpha the world's share: opaque
const float kArrowFill[4] = {1.0f, 0.82f, 0.25f, 0.0f};    // gold, as the game's own text
// Made safe after a 0.179 test run ended silently the moment the map first opened in VR F4 (no crash report, the log's last line the
// map's box; 2026-10-25): nothing drawn unless everything is in place - the way fresh and of unit length, the box in the target, the
// target a 2D texture of ours or the game's layer that holds the box, the map drawn in two passes before (mapdir::Ready) - the rows in
// buffers of their own (not on the game's draw thread's stack), cleared a few at a time under SEH; a fault stops the arrow for the
// session. The first arrow says so in the log before and after its clears: a run that ends between the two says it was the clear.
std::atomic<bool> g_dirBroken{false};
uint32_t g_mapSeenPasses = 0;   // interface passes in a row the corner map was drawn in (Publish). Under g_lock.
uint32_t g_fullPasses = 0;      // ... the whole-screen map was wanted in
mapdir::Rect g_arrowRows[mapdir::kMaxRows];   // (under g_lock: every caller holds it)
rect g_arrowRects[mapdir::kClearBatch];
bool ClearGuarded(command_list* cl, resource_view rtv, const float c[4], uint32_t n, const rect* r) noexcept {
    __try {
        cl->clear_render_target_view(rtv, c, n, r);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void PaintArrow(command_list* cl, const resource_view& rtv, const rect& clip, float cx, float cy, float boxH, uint32_t passes, const char* where) {
    if (!g_dirWant.load() || g_dirBroken.load() || !cl || !rtv.handle) return;
    const ULONGLONG at = g_dirAt.load(), now = GetTickCount64();
    if (!at || now < at || now - at > 500) return;   // no view of ours lately
    device* dev = cl->get_device();
    if (!dev) return;
    const resource tex = dev->get_resource_from_view(rtv);
    if (!tex.handle) return;
    const resource_desc td = dev->get_resource_desc(tex);
    if (td.type != resource_type::texture_2d) return;
    const mapdir::Dir d{g_dirX.load(), g_dirY.load(), true};
    mapdir::Rect c{clip.left, clip.top, clip.right, clip.bottom};
    const mapdir::Place pl = mapdir::PlaceFor(cx, cy, boxH);   // about the hero's ring, clear of it (mapdir::kMarker*)
    if (!mapdir::Ready(d, pl.cx, pl.cy, boxH, td.texture.width, td.texture.height, passes, &c)) return;
    const float len = pl.len;
    cx = pl.cx; cy = pl.cy;
    static bool told = false;
    if (!told) {
        char line[260];
        snprintf(line, sizeof line, "hud: map direction - the first arrow at %.0f,%.0f on %s (%ux%u), box %d,%d - %d,%d, %.0f px long, way %.2f,%.2f: clearing",
                 cx, cy, where, td.texture.width, td.texture.height, c.left, c.top, c.right, c.bottom, len, d.x, d.y);
        Log(line);
    }
    int rows = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const mapdir::Tri flat = mapdir::IsoArrow(cx, cy, d, len, mapdir::kIsoSquash, pl.gap);   // lying on the map, in its isometric projection
        const mapdir::Tri t = pass ? flat : mapdir::Grow(flat, std::max(1.0f, 0.08f * len));
        const int n = mapdir::Rows(t, c, g_arrowRows, mapdir::kMaxRows);
        rows += n;
        for (int i = 0; i < n; i += mapdir::kClearBatch) {
            const int k = std::min(n - i, mapdir::kClearBatch);
            for (int j = 0; j < k; ++j) {
                const mapdir::Rect& r = g_arrowRows[i + j];
                g_arrowRects[j] = rect{r.left, r.top, r.right, r.bottom};
            }
            if (!ClearGuarded(cl, rtv, pass ? kArrowFill : kArrowRim, (uint32_t)k, g_arrowRects)) {
                g_dirBroken.store(true);
                Log("hud: map direction - a clear for the arrow faulted: no arrow on the map for the rest of the session");
                return;
            }
        }
    }
    if (!told) {
        told = true;
        char line[160];
        snprintf(line, sizeof line, "hud: map direction - drawn (%d rows in batches of %d); later arrows are not logged", rows, mapdir::kClearBatch);
        Log(line);
    }
}

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

// The map's scissor: a box in a top corner of the interface area (its top a
// little below the area's), an eighth to a half of the screen each way. Matched
// by that, not by MapRect's size: the height rule held at 1920x1200 and
// 1699x1274/80%, but at 1920x1440 the map is ~640 wide where it predicts 773,
// past the tolerance - the map stayed in the picture and the share was empty.
// vr/minimap_box.h has the test and why it is no longer the file's Safe Screen
// Percent against the texture's size (a window size other than the shipped
// square lost the map): the area from the game's own interface scale, the
// file's as a second guess, in the viewport the interface is drawn through.
minimapbox::Canvas CanvasOf(const Drawing& d) { return minimapbox::CanvasOf(d.w, d.h, d.vx, d.vy, d.vw, d.vh); }
float GameSafe(const minimapbox::Canvas& c) { return minimapbox::SafeFromScale(g_gameUiScale.load(), c.h); }
// F4 in a picture of another shape than our frustum's (vr/minimap_box.h Stretch): the map's box and tiles that much wider.
float MapStretch(const minimapbox::Canvas& c) { return minimapbox::Stretch(g_projRatio.load(), 1.0f, c.w, c.h); }
bool IsMapScissor(const rect& r, const Drawing& d) {
    const minimapbox::Canvas c = CanvasOf(d);
    return minimapbox::Find(minimapbox::Box{r.left, r.top, r.right, r.bottom}, c, GameSafe(c), g_uiScale.load(), g_mapCorner.load(), MapStretch(c)) != 0;
}

// The map scissor last seen, and the layer size it was seen at: what the
// share copies (the map is drawn only inside it). Under g_lock.
rect g_mapScissor{};
uint32_t g_mapScissorW = 0, g_mapScissorH = 0;
float g_mapStretchNow = 1.0f;   // how much wider than its own shape that map was drawn (MapStretch): the hand's map is narrowed back by it
// No map box seen while the map is to come out of the picture: the boxes this
// interface pass had instead, for the log (TellNoMap). Under g_lock.
rect g_seenBoxes[6]{};
int g_seenN = 0;
minimapbox::Canvas g_seenCanvas{};
uint32_t g_mapMisses = 0;   // interface passes in a row with no map box

// [hud] map_direction: the map's draws are done - its arrow over them, in the middle of its box (the hero's marker), on the target they
// went to. Under g_lock.
void ArrowOnMap(command_list* cl, Drawing& d) {
    d.mapRun = false;
    if (g_mapScissorW != d.w || g_mapScissorH != d.h) return;   // a box seen on a layer of another size
    const bool ours = d.inMap && g_mapRtv.handle;
    if (ours && (g_mapW != d.w || g_mapH != d.h)) return;
    const rect& b = g_mapScissor;
    PaintArrow(cl, ours ? g_mapRtv : d.layer, b, 0.5f * (float)(b.left + b.right), 0.5f * (float)(b.top + b.bottom),
               (float)(b.bottom - b.top), g_mapSeenPasses, ours ? "the map's own target" : "the interface's layer");
}
// The whole-screen map has no box of its own: the arrow in the middle of the interface's canvas (the hero, as the game's camera has him),
// as big as on a corner map. Under g_lock.
void ArrowOnWholeMap(command_list* cl, const Drawing& d) {
    const minimapbox::Canvas c = CanvasOf(d);
    if (c.w < 64 || c.h < 64) return;
    const float game = GameSafe(c), s = game > 0.0f ? game : g_uiScale.load();
    const rect clip{c.x, c.y, c.x + (int32_t)c.w, c.y + (int32_t)c.h};
    PaintArrow(cl, d.layer, clip, (float)c.x + 0.5f * (float)c.w, (float)c.y + 0.5f * (float)c.h, 0.377f * (float)c.h * s, g_fullPasses,
               "the interface's layer (the whole-screen map)");
}

// Said once for each layer size and interface area: the map does not come into
// the hand and the log says what was there instead (the map may be off in the game).
void TellNoMap(uint32_t w, uint32_t h) {
    const minimapbox::Canvas c = g_seenCanvas.w ? g_seenCanvas : minimapbox::Canvas{0, 0, w, h};
    const float game = GameSafe(c), file = g_uiScale.load(), stretch = MapStretch(c);
    // a box in the upper half (where the map would be: the map open in the game but not taken) is said again, with why each box was not taken
    // (2026-10-28: the one line of a player's session came with the map shut, and could not tell [hud] map_corner from the picture's size)
    uint64_t upper = 0;
    for (int i = 0; i < g_seenN; ++i) upper += g_seenBoxes[i].top - c.y < (int32_t)(c.h / 2) ? 1 : 0;
    static uint64_t told = 0;
    const uint64_t key = (uint64_t)w << 48 ^ (uint64_t)h << 32 ^ (uint64_t)(game * 1000.0f) << 16 ^ (uint64_t)(file * 1000.0f) ^ (uint64_t)(stretch * 100.0f) << 8
                         ^ (upper ? 1ull << 63 : 0);
    if (key == told) return;
    told = key;
    static int lines = 0;   // (the map opened and shut again and again: not a line every ten seconds all session)
    if (++lines > 20) return;
    char line[640];
    int n = snprintf(line, sizeof line, "hud: no map box in the game's interface on a %ux%u layer (canvas %d,%d %ux%u; interface area %.3f by the game's "
                     "scale, %.3f by Settings.json; corner %d; drawn %.2f times as wide) - the map off in the game (Tab), or not where it is looked "
                     "for; its boxes:", w, h, c.x, c.y, c.w, c.h, game, file, g_mapCorner.load(), stretch);
    const float safe = game > 0.0f ? game : file;
    for (int i = 0; i < g_seenN && n > 0 && n < (int)sizeof line; ++i) {
        const rect& r = g_seenBoxes[i];
        const char* why = minimapbox::WhyNot(minimapbox::Box{r.left, r.top, r.right, r.bottom}, c, safe, g_mapCorner.load(), stretch);
        n += snprintf(line + n, sizeof line - n, " %d,%d-%d,%d (%s)", r.left, r.top, r.right, r.bottom, why ? why : "?");
    }
    if (!g_seenN && n > 0 && n < (int)sizeof line) snprintf(line + n, sizeof line - n, " none");
    Log(line);
}

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
// out: this frame's copy was taken out of the picture (the labels' plane), else left in as the fog's mask; menu: ... left in through a menu.
struct Labels { resource tex{}; resource_view srv{}; uint32_t w = 0, h = 0; bool fresh = true;
                bool on = false, out = false, menu = false; float keep[2][4] = {}; ULONGLONG at = 0; };
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

// [hud] monster_bar_room (vr/plate_world.h): the plate the game hangs over the targeted monster - its name and health bar - cut out of the interface
// for xr.cpp, which hangs it in the room above the monster, and emptied in the layer. The plate stands at a FIXED point of the picture while this is on
// (vr/mob_bars.cpp gives the game that point as the monster's), so its box (vrcam, from the plate's widgets, uv of the layer) does not lag it.
// The box is taken a little larger, in steps (a name's length changes the plate's width; a texture of its own size for each would be made every target).
// Local: FlatVR never sees it. Under g_lock.
std::atomic<bool> g_plateWant{false};
std::atomic<int32_t> g_plateLayout[4] = {};    // the plate's rect in the layout's units (gamestate PlateRect) ...
std::atomic<float> g_plateScale{0.0f};         // ... and the interface scale to turn it into pixels (0: not known)
struct PlateTex { resource tex{}; uint32_t w = 0, h = 0; bool fresh = true; };
PlateTex g_plateTex;
std::atomic<bool> g_plateNow{false};           // cut by the last interface pass
std::atomic<ULONGLONG> g_plateAt{0};
constexpr uint32_t kPlateStepX = 128, kPlateStepY = 32;

// The box in pixels of a w x h layer (plateworld::BoxOf), grown to whole steps about its middle and kept in the layer. Empty when there is no box.
rect PlateRectPx(uint32_t w, uint32_t h) {
    const int32_t lay[4] = {g_plateLayout[0].load(), g_plateLayout[1].load(), g_plateLayout[2].load(), g_plateLayout[3].load()};
    float box[4];
    if (!plateworld::BoxOf(lay, g_plateScale.load(), w, h, box)) return rect{};
    const float l = box[0], t = box[1], r = box[2], b = box[3];
    if (!(r > l && b > t) || r < 0.0f || b < 0.0f || l > (float)w || t > (float)h) return rect{};
    const float cx = 0.5f * (l + r), cy = 0.5f * (t + b);
    float bw = std::ceil((r - l) / (float)kPlateStepX) * (float)kPlateStepX;
    float bh = std::ceil((b - t) / (float)kPlateStepY) * (float)kPlateStepY;
    bw = std::min(bw, (float)w);
    bh = std::min(bh, (float)h);
    const float x0 = std::clamp(cx - 0.5f * bw, 0.0f, (float)w - bw);
    const float y0 = std::clamp(cy - 0.5f * bh, 0.0f, (float)h - bh);
    return rect{(int32_t)x0, (int32_t)y0, (int32_t)(x0 + bw), (int32_t)(y0 + bh)};
}

// A box of the layer copied into a texture of its own (made, or made again, at the box's size). what: for the log ("the monster's plate ... ([hud] monster_bar_room)").
bool CopyBox(command_list* cl, resource layer, const rect& r, PlateTex& t, const char* what, const char* key, bool& toldFail) {
    device* dev = cl->get_device();
    const uint32_t w = (uint32_t)(r.right - r.left), h = (uint32_t)(r.bottom - r.top);
    if (!w || !h) return false;
    if (!t.tex.handle || t.w != w || t.h != h) {
        if (t.tex.handle) g_retired.push_back({t.tex, {}, nullptr, g_frames});   // xr.cpp may still draw from it
        t = PlateTex{};
        const resource_desc d(w, h, 1, 1, format::r16g16b16a16_float, 1, memory_heap::default_, resource_usage::copy_dest | resource_usage::shader_resource);
        if (!dev->create_resource(d, nullptr, resource_usage::copy_dest, &t.tex)) {
            t = PlateTex{};
            if (!toldFail) {
                toldFail = true;
                char line[200];
                snprintf(line, sizeof line, "hud: %s - its texture could not be made, it stays in the picture", what);
                Log(line);
            }
            return false;
        }
        t.w = w; t.h = h;
        char line[240];
        snprintf(line, sizeof line, "hud: %s is cut out of the interface for the room (%s): %ux%u from %d,%d of a layer", what, key, w, h, r.left, r.top);
        Log(line);
    }
    const subresource_box from{(uint32_t)r.left, (uint32_t)r.top, 0, (uint32_t)r.right, (uint32_t)r.bottom, 1};
    const subresource_box to{0, 0, 0, w, h, 1};
    cl->barrier(layer, resource_usage::render_target, resource_usage::copy_source);
    if (!t.fresh) cl->barrier(t.tex, resource_usage::shader_resource, resource_usage::copy_dest);
    cl->copy_texture_region(layer, 0, &from, t.tex, 0, &to, filter_mode::min_mag_mip_point);
    cl->barrier(t.tex, resource_usage::copy_dest, resource_usage::shader_resource);
    cl->barrier(layer, resource_usage::copy_source, resource_usage::render_target);
    t.fresh = false;
    return true;
}
bool CopyPlate(command_list* cl, resource layer, const rect& r) {
    static bool told = false;
    return CopyBox(cl, layer, r, g_plateTex, "the monster's plate", "[hud] monster_bar_room", told);
}

// [hud] notice_card (vr/notice_card.h): the game's quest / level-up notice - its NotificationPanel, low in the middle, most of it under the toolbar's
// strip - cut out of the interface for xr.cpp, which shows it on a card in the room, and emptied in the layer. Cut BEFORE the toolbar's strip is
// copied and emptied: the strip would take most of it. While vrcam says (the card in force, the panel open); its box from the panel's rect and the
// interface scale (noticecard::BoxPx). Local: FlatVR never sees it. Under g_lock.
std::atomic<bool> g_noticeWant{false};
std::atomic<int32_t> g_noticeWidget[4] = {};   // the rect the widget service gave (all 0: none, the layout's)
std::atomic<float> g_noticeScale{0.0f};
PlateTex g_noticeTex;
std::atomic<bool> g_noticeNow{false};          // cut by the last interface pass
std::atomic<ULONGLONG> g_noticeAt{0};
void CutNotice(command_list* cl, resource layer, const resource_view& rtv, uint32_t w, uint32_t h) {
    bool cut = false;
    if (g_noticeWant.load() && !g_menu.load() && !g_panelOut.load()) {
        const int32_t wr[4] = {g_noticeWidget[0].load(), g_noticeWidget[1].load(), g_noticeWidget[2].load(), g_noticeWidget[3].load()};
        noticecard::Box b;
        static bool told = false;
        if (noticecard::BoxPx(wr, g_noticeScale.load(), w, h, &b)) {
            const rect r{b.left, b.top, b.right, b.bottom};
            if (CopyBox(cl, layer, r, g_noticeTex, "the quest / level-up notice", "[hud] notice_card", told)) {
                cl->clear_render_target_view(rtv, kEmpty, 1, &r);
                cut = true;
            }
        }
    }
    g_noticeNow.store(cut);
    if (cut) g_noticeAt.store(GetTickCount64());
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
    const bool menu = g_menu.load() || g_panelOut.load(), classic = g_classic.load();   // a menu's panel takes the toolbar with it (the big map does not: it stays where it hangs)
    bool bar = false, map = false;
    const rect barBox = ToolbarRect(w, h);
    rect mapBox{};
    if (hide >= 1 && !menu) bar = CopyOut(cl, layer, barBox, kBar);
    if (g_globesWant.load() && g_picReady.load()) CopyGlobes(cl, layer, w, h);   // (the layer is left as it is: the toolbar stays or goes by [hud] hide)
    else { g_glb.on = false; g_globesWhy.store(1); }
    const bool drawn = g_mapDrawn;
    g_mapDrawn = false;
    g_mapSeenPasses = drawn ? std::min(g_mapSeenPasses + 1, 1000u) : 0;   // [hud] map_direction: the arrow only on a map drawn a few passes running
    g_fullPasses = g_dirFull.load() && !drawn ? std::min(g_fullPasses + 1, 1000u) : 0;
    if (g_frameMap.load() == 1 && !drawn && !menu) {   // ten seconds or so with no map box: what there was, in the log
        if (++g_mapMisses % 600 == 0) TellNoMap(w, h);   // (every ten seconds or so; said again only when what it says changes: TellNoMap's key)
    } else {
        g_mapMisses = 0;
    }
    g_seenN = 0;
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
    const bool redirect = g_frameMap.load() == 1;   // else only seen: the labels keep the map flat, [hud] map_direction's arrow goes on it
    if (count == 0 || (!redirect && !g_labels.load() && !g_dirWant.load())) return;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_drawing.find(cl);
    if (it == g_drawing.end()) return;
    Drawing& d = it->second;
    const bool map = IsMapScissor(rects[0], d);
    if (map && (rects[0].left != g_mapScissor.left || rects[0].top != g_mapScissor.top ||
                rects[0].right != g_mapScissor.right || rects[0].bottom != g_mapScissor.bottom ||
                g_mapScissorW != d.w || g_mapScissorH != d.h)) {
        const minimapbox::Canvas c = CanvasOf(d);
        char line[360];
        snprintf(line, sizeof line, "hud: the map's box %d,%d - %d,%d on a %ux%u layer (canvas %d,%d %ux%u; interface area %.3f by the game's scale, "
                 "%.3f by Settings.json; drawn %.2f times as wide)", rects[0].left, rects[0].top, rects[0].right, rects[0].bottom, d.w, d.h, c.x, c.y,
                 c.w, c.h, GameSafe(c), g_uiScale.load(), MapStretch(c));
        Log(line);
    }
    if (map) g_mapStretchNow = MapStretch(CanvasOf(d));
    if (!map && redirect && g_seenN < (int)(sizeof g_seenBoxes / sizeof g_seenBoxes[0])) {   // (for TellNoMap: the boxes other than the whole picture)
        const rect& r = rects[0];
        bool known = r.left <= 0 && r.top <= 0 && r.right >= (int32_t)d.w && r.bottom >= (int32_t)d.h;
        for (int i = 0; i < g_seenN && !known; ++i)
            known = g_seenBoxes[i].left == r.left && g_seenBoxes[i].top == r.top && g_seenBoxes[i].right == r.right && g_seenBoxes[i].bottom == r.bottom;
        if (!known) g_seenBoxes[g_seenN++] = r;
        g_seenCanvas = CanvasOf(d);
    }
    if (!map && d.mapRun) ArrowOnMap(cl, d);   // (before the draws go back to the layer: on the map's own target while it has one)
    if (map) { g_mapScissor = rects[0]; g_mapScissorW = d.w; g_mapScissorH = d.h; g_mapDrawn = true; d.mapRun = true; }
    if (!redirect || !g_mapRtv.handle) return;
    if (map && !d.inMap) { Bind(cl, g_mapRtv); d.inMap = true; }
    else if (!map && d.inMap) { Bind(cl, d.layer); d.inMap = false; }
}

// On the interface's list: the largest viewport it is drawn through - the canvas the map's box is
// looked for in (vr/minimap_box.h CanvasOf: a viewport that is a piece of the layer is not it).
void OnViewports(command_list* cl, uint32_t first, uint32_t count, const viewport* vps) {
    if (first != 0 || count == 0 || !vps || !g_layer.load() || (g_frameMap.load() != 1 && !g_labels.load() && !g_dirWant.load())) return;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_drawing.find(cl);
    if (it == g_drawing.end()) return;
    Drawing& d = it->second;
    if (vps[0].width * vps[0].height > d.vw * d.vh) { d.vx = vps[0].x; d.vy = vps[0].y; d.vw = vps[0].width; d.vh = vps[0].height; }
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
        const bool panel = g_panelOut.load() || g_mapBig.load();   // (the big map: the whole interface out, as a menu)
        // [hud] map_direction: the corner map drawn last, its arrow now; the whole-screen map (no box seen), its arrow in the middle
        if (it->second.mapRun) ArrowOnMap(cl, it->second);
        else if (g_dirFull.load() && !g_mapDrawn && !g_menu.load() && !g_panelOut.load()) ArrowOnWholeMap(cl, it->second);   // (the big map too)
        CutNotice(cl, dev->get_resource_from_view(layerRtv), layerRtv, it->second.w, it->second.h);   // [hud] notice_card: before the toolbar's strip takes it
        {
            const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(layerRtv));
            Publish(cl, dev->get_resource_from_view(layerRtv), d.texture.width, d.texture.height, hideNow);
        }
        if (panel) {   // a menu as a panel: all of it out, the world alone in the picture
            if (g_panelRuns.fetch_add(1) == 1) {
                static bool told = false;
                if (!told) { told = true; Log("hud: the interface's layer was drawn in more than one run this frame - the menu's panel may lack parts"); }
            }
            // [hud] npc_dialog_no_bar: an NPC's Talk / Trade / Cancel box alone is not a screen of the game's - the toolbar would hang on
            // its panel as a bar in front of the hero at waist height (Serg, 2026-10-28). Its strip is emptied first, as [hud] hide 1
            // does with no menu up (the wrists / FlatVR have their copy already: Publish above). A real menu (the trader's screen
            // after Trade) keeps it.
            if (g_panelNoBar.load() && HideNow() >= 1 && !g_classic.load()) {
                const resource_desc d = dev->get_resource_desc(dev->get_resource_from_view(layerRtv));
                const rect bar = ToolbarRect(d.texture.width, d.texture.height);
                cl->clear_render_target_view(layerRtv, kEmpty, 1, &bar);
            }
            const bool out = CopyPanel(cl, dev->get_resource_from_view(layerRtv), it->second.w, it->second.h);
            if (out && g_mapBig.load() && !g_panelOut.load()) {
                // [hud] map_click_big: the big map is the game's whole-screen map above the toolbar's strip (xr.cpp shows those rows only); the
                // layer is emptied there, the strip left to the toolbar's own rules - cut out and hung as always ([hud] hide 1), or in the picture
                const rect bar = ToolbarRect(it->second.w, it->second.h);
                const rect above{0, 0, (int32_t)it->second.w, std::max<int32_t>(1, bar.top)};
                g_mapBigRows.store((uint32_t)above.bottom);
                const minimapbox::Canvas c = CanvasOf(it->second);   // [hud] map_rotate on the big map: about the hero, the canvas's middle
                if (c.w >= 64 && c.h >= 64 && it->second.w && it->second.h) {
                    g_mapBigHeroU.store(((float)c.x + 0.5f * (float)c.w) / (float)it->second.w);
                    g_mapBigHeroV.store(((float)c.y + 0.5f * (float)c.h) / (float)it->second.h);
                    g_mapBigStretch.store(MapStretch(c));
                }
                cl->clear_render_target_view(layerRtv, kEmpty, 1, &above);
                if (hideNow == 2) cl->clear_render_target_view(layerRtv, kEmpty);
                else if (hideNow == 1 && (!g_classic.load() || g_pic.on[kBar])) cl->clear_render_target_view(layerRtv, kEmpty, 1, &bar);
            } else if (out) {
                cl->clear_render_target_view(layerRtv, kEmpty);
            }
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
        {   // [hud] monster_bar_room: the monster's plate out of the picture, for the room (not while a menu is up)
            bool cut = false;
            if (!panel && g_plateWant.load() && !g_menu.load()) {
                const rect pr = PlateRectPx(it->second.w, it->second.h);
                if (pr.right > pr.left && pr.bottom > pr.top && CopyPlate(cl, dev->get_resource_from_view(layerRtv), pr)) {
                    cl->clear_render_target_view(layerRtv, kEmpty, 1, &pr);
                    cut = true;
                }
            }
            g_plateNow.store(cut);
            if (cut) g_plateAt.store(GetTickCount64());
        }
        // From above: the rest of it out whole, for the effect to lay on the tilted plane.
        if (!panel) {
            g_panelNow.store(false);
            if (g_block) g_block->panel[kMenuSlot].visible = 0;
        }
        const bool labels = g_labels.load() && g_picReady.load() && !g_menu.load() && !panel;
        // copied, left in. Through a menu by g_uiMaskInMenu alone: the labels are never taken out there, so their being wanted (g_uiMask is
        // off for it) must not leave the panel without a mask - flat F2 with [hud] monster_alpha < 1 wants them all the time, and our sky and
        // fog lay over the inventory (sangee1234, 0.189.0, 2026-10-30)
        const bool mask = !labels && g_picReady.load() && !panel && (g_menu.load() ? g_uiMaskInMenu.load() : g_uiMask.load());
        g_lab.on = (labels || mask) && CopyLayer(cl, dev->get_resource_from_view(layerRtv), it->second.w, it->second.h);
        g_lab.out = g_lab.on && labels;
        g_lab.menu = g_lab.on && !labels && g_menu.load();
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

// [render] no_reshade: the same handlers, from the plugin's own D3D12 hooks through the stand-in for ReShade's command list and device
// (vr/native_rsapi.h) - one code path for the interface with ReShade or without it.
namespace native {
void Clear(reshade::api::command_list* cl, reshade::api::resource_view rtv, const float c[4], uint32_t n, const reshade::api::rect* r) { OnClear(cl, rtv, c, n, r); }
void Bind(reshade::api::command_list* cl, uint32_t count, const reshade::api::resource_view* rtvs) { OnBind(cl, count, rtvs, reshade::api::resource_view{}); }
void Scissors(reshade::api::command_list* cl, uint32_t count, const reshade::api::rect* rects) { OnScissor(cl, 0, count, rects); }
void Viewports(reshade::api::command_list* cl, uint32_t count, const reshade::api::viewport* vps) { OnViewports(cl, 0, count, vps); }
void Reset(reshade::api::command_list* cl) { OnReset(cl); }
}  // namespace native

void Register() {
    reshade::register_event<reshade::addon_event::clear_render_target_view>(&d2rvr::D3D12Only<&OnClear>::Call);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&OnBind>::Call);
    reshade::register_event<reshade::addon_event::reset_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::init_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::destroy_command_list>(&d2rvr::D3D12Only<&OnReset>::Call);
    reshade::register_event<reshade::addon_event::bind_scissor_rects>(&d2rvr::D3D12Only<&OnScissor>::Call);
    reshade::register_event<reshade::addon_event::bind_viewports>(&d2rvr::D3D12Only<&OnViewports>::Call);
}

void SetHide(int mode) { g_hide.store(mode); }
void SetMenuOpen(bool open) { g_menu.store(open); }
void SetPanelNoBar(bool on) { g_panelNoBar.store(on); }
void SetMapMode(int mode) { g_mapMode.store(mode); }
void SetInterfaceScale(float s) { g_uiScale.store(std::clamp(s, 0.3f, 1.0f)); }
void SetGameInterfaceScale(float k) { g_gameUiScale.store(std::isfinite(k) && k > 0.05f && k < 20.0f ? k : 0.0f); }
void SetMapProjection(float ratio) { g_projRatio.store(std::isfinite(ratio) && ratio > 0.0f ? ratio : 0.0f); }
void SetToolbarCut(float upPct, float downPct, bool tint) {
    g_cutUp.store(hudrects::UpPct(upPct));
    g_cutDown.store(hudrects::DownPct(downPct));
    g_cutTint.store(tint);
}
bool LayerFound() { return g_layer.load() != 0; }
void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetMapCorner(int corner) { g_mapCorner.store(std::clamp(corner, 0, 2)); }
void SetMapDirection(bool want, bool wholeScreen) { g_dirWant.store(want); g_dirFull.store(want && wholeScreen); }
void SetMapHeading(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    g_dirX.store(x); g_dirY.store(y); g_dirAt.store(GetTickCount64());
}
void SetMapTurnHeading(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    g_turnX.store(x); g_turnY.store(y); g_turnAt.store(GetTickCount64());
}
void SetMapBig(bool big) { g_mapBig.store(big); }
bool MapBig() { return g_mapBig.load(); }
bool MapTurnHeading(float* x, float* y) {
    const ULONGLONG at = g_turnAt.load(), now = GetTickCount64();
    if (!at || now < at || now - at > 500) return false;   // no view of ours lately
    *x = g_turnX.load(); *y = g_turnY.load();
    return true;
}

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
void SetUiMaskInMenu(bool on) { g_uiMaskInMenu.store(on); }
bool UiMaskNow() {
    std::lock_guard<std::mutex> g(g_lock);
    return g_lab.on && !g_lab.out && (g_lab.menu ? g_uiMaskInMenu.load() : !g_labels.load() && g_uiMask.load()) && g_lab.srv.handle &&
           GetTickCount64() - g_lab.at < 250;
}
// For D2R_DepthFog.fx: the interface left over this frame, and the boxes (uv) it keeps flat - the toolbar, the map.
bool LabelsNow(float keepBar[4], float keepMap[4], uint64_t* srv) {
    std::lock_guard<std::mutex> g(g_lock);
    *srv = g_lab.srv.handle;
    for (int k = 0; k < 4; ++k) { keepBar[k] = g_lab.keep[0][k]; keepMap[k] = g_lab.keep[1][k]; }
    return g_lab.on && g_lab.out && g_labels.load() && *srv && GetTickCount64() - g_lab.at < 250;
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
        out[i].stretch = i == kMap ? g_mapStretchNow : 1.0f;
    }
    NativePiece& p = out[kNativePanel];
    p.tex = (ID3D12Resource*)(uintptr_t)g_panel.tex.handle;
    p.w = g_panel.w;
    p.h = g_panel.h;
    p.visible = p.tex && !g_panel.fresh && (g_panelOut.load() || g_mapBig.load()) && g_panelNow.load() && GetTickCount64() - g_panelAt.load() < 500;
    p.mapRows = g_mapBig.load() && !g_panelOut.load() ? std::min(g_mapBigRows.load(), p.h) : 0u;
    if (p.mapRows) { p.heroU = g_mapBigHeroU.load(); p.heroV = g_mapBigHeroV.load(); p.stretch = g_mapBigStretch.load(); }
    *look = g_look;
    *barPose = g_pose[kBar];
    return true;
}

// [hud] monster_bar_room: vrcam's tick says whether the plate is to be cut now and where its box is (uv of the layer).
void SetPlateCut(bool want, const int32_t layout[4], float scale) {
    if (want && layout) {
        for (int i = 0; i < 4; ++i) g_plateLayout[i].store(layout[i]);
        g_plateScale.store(scale);
    }
    g_plateWant.store(want && layout);
}
// The cut plate for xr.cpp: its texture (left in the shader-resource state on the game's queue), its size, whether the last pass cut it.
bool PlatePiece(NativePiece* out, bool* encoded) {
    if (!out) return false;
    std::lock_guard<std::mutex> g(g_lock);
    out->tex = (ID3D12Resource*)(uintptr_t)g_plateTex.tex.handle;
    out->w = g_plateTex.w;
    out->h = g_plateTex.h;
    out->visible = out->tex && !g_plateTex.fresh && g_plateNow.load() && GetTickCount64() - g_plateAt.load() < 150;
    if (encoded) *encoded = g_look.encoded != 0;
    return true;
}
// [hud] notice_card: vrcam's tick says whether the notice is to be cut now (the card in force, the game's panel open), its panel's rect as the widget
// service gave it (null: not read, the layout's) and the interface scale.
void SetNoticeCut(bool want, const int32_t widget[4], float scale) {
    for (int i = 0; i < 4; ++i) g_noticeWidget[i].store(widget ? widget[i] : 0);
    g_noticeScale.store(scale);
    g_noticeWant.store(want);
}
// The cut notice for xr.cpp, as PlatePiece.
bool NoticePiece(NativePiece* out, bool* encoded) {
    if (!out) return false;
    std::lock_guard<std::mutex> g(g_lock);
    out->tex = (ID3D12Resource*)(uintptr_t)g_noticeTex.tex.handle;
    out->w = g_noticeTex.w;
    out->h = g_noticeTex.h;
    out->visible = out->tex && !g_noticeTex.fresh && g_noticeNow.load() && GetTickCount64() - g_noticeAt.load() < 150;
    if (encoded) *encoded = g_look.encoded != 0;
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
