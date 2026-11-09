// vr/hud/looks.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

float BarNearNow() {
    if (!g_enabled.load() || TopPersp()) return g_set.barNearTop.load();
    if (TableView()) return g_set.barNearFloor.load();
    if (ThirdPerson()) return g_set.barNearThird.load();
    return FullBody() ? 0.0f : g_set.barNearInside.load();
}
// The toolbar's size in the picture, from above, from behind and on the floor (the game's own is 1).
float BarSizeNow() {
    if (!g_enabled.load() || TopPersp()) return g_set.barSizeTop.load();
    if (TableView()) return g_set.barSizeFloor.load();
    if (ThirdPerson()) return g_set.barSizeThird.load();
    return 1.0f;
}
// ... and where on the screen, uv from where the game puts it (+x right, +y down).
void BarOffsetNow(float* x, float* y) {
    *x = *y = 0.0f;
    if (!g_enabled.load() || TopPersp()) { *x = g_set.barXTop.load(); *y = g_set.barYTop.load(); }
    else if (TableView()) { *x = g_set.barXFloor.load(); *y = g_set.barYFloor.load(); }
    else if (ThirdPerson()) { *x = g_set.barXThird.load(); *y = g_set.barYThird.load(); }
}

// From above and behind: the toolbar moved nearer or farther - only real stereo has a depth to move it in.
bool BarNearWanted() { return AfrOn() && ClassicNow() && std::abs(BarNearNow()) > 0.01f; }
// The map on the floor (F3) nearer or farther in stereo ([hud_floor] map_near), as the toolbar.
bool MapNearWanted() { return AfrOn() && ClassicNow() && TableView() && std::abs(g_set.mapNearFloor.load()) > 0.01f; }
// VR F3, the game on the floor, with our camera on (ClassicNow's order: off is F1 whatever the view).
bool FloorView() { return g_enabled.load() && TableView(); }
// The item labels' box opacity and size for the view now - each VR view's Interface tab has
// its own (F3 [hud_floor], F1 [hud_top], F2 [hud_third], F4 [hud]); 1 and 1 = the game's own.
// Flat keeps the game's labels: it has no such tabs.
void LabelLookNow(float* alpha, float* size) {
    *alpha = 1.0f; *size = 1.0f;
    if (g_set.platform.load() != 1) return;
    if (FloorView()) { *alpha = g_set.labelsAlphaFloor.load(); *size = g_set.labelsSizeFloor.load(); }
    else if (!g_enabled.load() || TopPersp()) { *alpha = g_set.labelsAlphaTop.load(); *size = g_set.labelsSizeTop.load(); }
    else if (ThirdPerson()) { *alpha = g_set.labelsAlphaThird.load(); *size = g_set.labelsSizeThird.load(); }
    else if (FullBody()) { *alpha = g_set.labelsAlphaBody.load(); *size = g_set.labelsSizeBody.load(); }
}
// The game's item-label code hooked (LabelHooks, further down): the box's alpha and the size set per label.
std::atomic<bool> g_labelPaintIn{false}, g_labelSizeIn{false};
// [hud_floor] labels_alpha by the game's own label code (exact, per label) rather than as a picture.
bool LabelBoxNative() { return g_labelPaintIn.load() && g_set.labelsNativeFloor.load(); }
// From above only (the camera ours is off) in stereo: the labels over monsters on a plane tilted like the ground.
// On the floor (F3): the labels faded as a picture ([hud_floor] labels_alpha) - only while the game's
// own label code is not hooked (another game build, or labels_native=0), which fades the box exactly.
// From above in stereo with a depth or a tilt for the labels: they come out to lie on their plane.
// In first person (F4, native OpenXR too) the labels on the ground: the game draws them on the same pixels
// in both eyes - at infinity in the headset, out of place against their items (2026-10-12). Each item
// label at its own item's depth ([hud] labels_depth_body, LabelDepthUniforms), the rest of the layer
// by labels_near_body - by the item's distance, not by the head's tilt (the author, 2026-10-12).
bool LabelsTilted() {
    if (!AfrOn()) return false;
    if (!g_enabled.load())
        return g_set.classicTop.load() == 0 && (std::abs(g_set.labelsNear.load()) > 0.01f || std::abs(g_set.labelsTilt.load()) > 0.01f);
    return FullBody() && (std::abs(g_set.labelsNearBody.load()) > 0.01f || g_set.labelsDepthBody.load() > 0.005f);
}
// [hud] monster_alpha, monster_size: the plate over the target monster faded or sized - the interface
// comes out for that too, drawn back where the game puts it.
// In first person (F4) the plate has its own depth, apart from the item labels' (labels_near_body): then the effect needs its box to tell it from the labels (vr/plate_look.h).
bool PlateOwnDepth() { return g_enabled.load() && FullBody(); }
// The other VR views with a stereo pair of our camera (F1 in perspective, F2, F3): the plate may stand apart there too (vr/plate_look.h OtherView) -
// our own plate over the head ([hud] monster_names_own_all_views), or the game's at its monster's depth (F2 / F3 monster_depth).
bool PlateOtherStereo() { return g_set.platform.load() == 1 && !FullBody() && g_enabled.load() && AfrOn(); }
// [hud_third] monster_depth in F2, [hud_floor] monster_depth in F3 (by the view's key: VR F1 in perspective runs as g_view 2 too); 0 elsewhere.
float PlateDepthOtherView() {
    if (g_set.platform.load() != 1) return 0.0f;
    const int v = g_set.vrView.load();
    return v == 2 ? g_set.plateDepthThird.load() : v == 3 ? g_set.plateDepthFloor.load() : 0.0f;
}
// [hud] monster_stereo_by_distance: the plate's depth by its monster's distance - the settings, and the depth that decides whether the box is needed.
platelook::DepthByDistance PlateDepthByDistance() {
    platelook::DepthByDistance d;
    d.nearPercent = g_set.plateStereoNear.load();
    d.farPercent = g_set.plateStereoFar.load();
    d.nearM = g_set.plateStereoNearM.load();
    d.farM = g_set.plateStereoFarM.load();
    return d;
}
float PlateDepthForBox() {
    return platelook::DepthForBox(g_set.plateStereoByDistance.load(), g_set.plateNearBody.load(), PlateDepthByDistance(), g_set.labelsNearBody.load());
}
bool PlateWanted() {
    if (PlateOtherStereo() && PlateDepthOtherView() > 0.005f) return true;   // F2 / F3 monster_depth: the plate cut out and moved to its monster's depth
    return platelook::PlateNeedsBox(PlateOwnDepth(), PlateDepthForBox(), g_set.labelsNearBody.load(), g_set.plateAlpha.load(), g_set.plateSize.load(),
                                    g_set.plateFar.load(), g_set.plateDepthBody.load());
}
// Eyes by turns: the plate's layer is laid out for the eye drawn each frame - the effect needs its box and its target's depth to show it the same in each
// eye every frame (D2R_DepthFog.fx PlateTurnH, vr/plate_look.h TurnsHalf). First person, the overlays by turns in use.
namespace replay { bool OverlaysWanted(); }
// Each eye's picture with its own layout of the interface in the effect's main pass: the pair's right pass drawn by the game (not replayed
// from the left's lists - [render] no_reshade has no replay), or eyes by turns without the overlays apart. The game lays a label and the plate
// out where THAT eye sees its thing: shifted again per eye they stood doubled (Serg, 2026-10-27, no ReShade, pair_per_tick: the plates over
// the monsters). D2R_DepthFog.fx LayerOwnEye: that eye's half-disparity taken off first, as by turns.
bool PairOfOneFrame();
bool NativeByTurns();
namespace replay { extern std::atomic<bool> g_rightReplayed; }
bool LayerOwnEye() {
    return AfrOn() && !replay::OverlaysWanted() && ((PairOfOneFrame() && !replay::g_rightReplayed.load()) || NativeByTurns());
}
bool PlateTurnsWanted() { return PlateOwnDepth() && (replay::OverlaysWanted() || LayerOwnEye()); }

bool PlateFaded() {
    int32_t r[4];
    return g_inWorld.load() && PlateWanted() && gamestate::PlateRect(r);
}
// [hud] allies_x, allies_y: the allies' portraits elsewhere - the interface comes out for that too, the
// portraits' corner drawn back moved (AlliesBoxUv). The target's plate is kept out of that corner: read too.
bool AlliesMoved() { return std::abs(g_set.alliesX.load()) > 0.0005f || std::abs(g_set.alliesY.load()) > 0.0005f; }
// [hud] allies_game_hide: the game's own portraits hidden in VR F4 (a key of its own): the same corner moved off the picture, as wide as what the game
// draws now (allyportraits::GameCornerToHide).
bool AlliesHidden(float* units) {
    float u = 0.0f;
    const bool on = g_enabled.load() && allyportraits::GameCornerToHide(&u);
    if (units) *units = u;
    return on;
}
// [hud] durability_x, durability_y, durability_scale, durability_hide: the item-condition figure (vr/durability_figure.h) moved, sized or hidden - the interface comes
// out for that too, in first person (F4) only; all four at their defaults: nothing is done.
durab::Look DurabilityLook() {
    durab::Look l;
    l.hide = g_set.durHide.load();
    l.x = g_set.durX.load();
    l.y = g_set.durY.load();
    l.scale = g_set.durScale.load();
    return l;
}
bool DurabilityMoved() { return g_enabled.load() && FullBody() && durab::Moved(DurabilityLook()); }
bool LabelsWanted() {
    if (PlateFaded()) return true;
    if (g_inWorld.load() && (AlliesMoved() || AlliesHidden(nullptr))) return true;
    if (g_inWorld.load() && DurabilityMoved()) return true;
    if (g_inWorld.load() && AfrOn() && OwnLabelsNow()) return true;   // [hud] labels_own: the effect draws ours in each item label's place
    if (g_inWorld.load() && AfrOn() && MonsterNamesOwnNow()) return true;   // [hud] monster_names_own: ... and ours in the target's plate
    if (g_inWorld.load() && AfrOn() && NpcNamesOwnNow()) return true;   // [hud] npc_names_own: ... and ours for the names over NPCs and objects
    if (FloorView()) return g_inWorld.load() && g_set.labelsAlphaFloor.load() < 0.995f && !LabelBoxNative();
    return LabelsTilted();
}

}  // namespace vrc
