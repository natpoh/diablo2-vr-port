// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed PR #518 (merge 1ee027b; f57f881 .. 696fb19), vr/vrcam.cpp's campfire: Settings' campfire* and their ini
// (LoadSettings), CampfireView, CampfirePointer, CampfireFireHit, CampfirePick, CampfireTick and the biome test in SkyTick. Changed for
// D2R VR Authentic: the eyes come from our native OpenXR pair (xr::OpenCampfireFrame / PairEyes, one frame for both eyes, each eye its
// own pose) instead of framevr::View; the hand's ray from xr::CampfireAim laid into the scene the same way instead of HandRay; the scene
// is not counted as a world (g_inWorld stays false - ours keys far more on it), only as stereo; a failure gives the flat screen back.
// The main menu in 3D ([frontend] scene_3d, D2R VR Authentic's key for Unleashed's [frontend] scene): D2R VR Unleashed PR #583 by
// Kudlatytrue (a645967 .. 7b73d3f), merged by bearstonem via PR #584 (9b7abd6) - vrcam.cpp's g_frontend3d / g_frontAct, the [frontend]
// keys and CampfireView's place for the main menu (eye, up, forward, right_N / up_N / forward_N, the act from the game camera's z), the
// board's own size ([frontend] menu_width / menu_distance / menu_drop / menu_top) and no campfire extras there (fire, sparks, caption,
// pick in the scene). Not taken: [frontend] sort_characters (it changes the saves' file times), Right B's Escape / ClosePanel on the board
// (our controller's B reaches the game's menus itself). Thanks to Kudlatytrue and bearstonem for the 3D main menu.
// See vr/campfire_unleashed.h. MPL 2.0.
#include "vrcam_headers.h"
#include "campfire_unleashed.h"
#include "campfire_audio_unleashed.h"

using namespace D2RL;
using namespace vrc;

namespace vrc { bool EyesSane(const xr::Eyes& e); }   // (vr/view/camera.cpp)
namespace panelinput { void SetPadLayoutFree(bool on); }   // (vr/panel_input.h)
namespace gamestate { int CreateHover(); int CreateSelected(); const char* CreateHeroName(int i); }   // the game's own word of the hero hovered / picked in creation (vr/gamestate.cpp)

namespace campfire {
namespace {

struct Cfg {
    std::atomic<bool>  on{false};          // [campfire] enabled: character creation's campfire scene in stereo, round you
    std::atomic<float> eye{6.0f};          // eye: the eye height the scene's scale is taken from, world units (~0.24 m each)
    std::atomic<float> up{2.25f};          // up: the eyes above the game camera, world units
    std::atomic<float> forward{3.0f};      // forward: the eyes ahead of the game camera, toward the fire, world units
    std::atomic<float> pickDist{22.0f};    // pick_distance: how far along the hand's ray the pointer is put, world units
    // pick_ring (D2R VR Authentic, default 15): the pointer where the hand's ray reaches the heroes' ring round the fire (world units from the fire,
    // on the ground; measured from Serg's hovers 2026-11-27: 14.4 and 15.7) instead of a fixed pick_distance along it - a ray aimed down at a hero
    // put the fixed point on the ground short of him, below his feet through the game's camera (no hover in his whole try); 0: pick_distance
    std::atomic<float> pickRing{15.0f};
    // pointer_band (D2R VR Authentic, default 1): the pointer's area is the 16:9 band at the window's top while the game's interface is laid out in
    // it - Unleashed's fit is for its 16:9 WINDOW ("the scene's picture fitted whole in the window ... how the game reads its pointer back"); ours
    // is square, the game's pointer area the band. Measured 2026-11-27 (the mouse swept over the 1280 window, the game's Hover answers): the eight
    // heroes answer at x 320 ... 960, y 220 - 80 px apart, which is their true spacing only with the picture fitted in the band (5.5 deg apart from
    // the scene camera; over the whole window they would stand 3 deg apart, through one another).
    std::atomic<bool>  pointerBand{true};
    // pick_heroes (D2R VR Authentic, default 1): the hero the hand's line passes nearest (within pick_snap world units of his body) gets the
    // game's pointer put on his own spot of the scene camera's picture (kHeroNx / kHeroNy, measured), and the game's Hover answer is watched:
    // no answer - the pointer searches round the spot and keeps what the game answered to. 0: the point on the line alone.
    std::atomic<bool>  pickHeroes{true};
    std::atomic<float> pickSnap{3.0f};
    // pick_ray (default 0): 1 = the hand's own line given to the game's ray while the trigger is held (Unleashed's CampfirePick does)
    std::atomic<bool>  pickRay{false};
    // pick_raise (D2R VR Authentic; replaces Unleashed's pick_lift 5.5): the pointer this much above the dot, world units. 0: the dot is
    // where the beam the player sees is pick_distance along - with our ray (the hand's own, 0.213.7) a level aim at a hero's chest put
    // the lifted dot over his head (the scene camera looks level, ny = 2.31 tan(elevation): 0.56-0.63 lifted, 0.07-0.15 not).
    std::atomic<float> pickLift{0.0f};
    // pick_turn: the scene camera turned toward the hand for the pick (the heroes at the ends): 0 never, 1 always (Unleashed's 1), 2 only when
    // the dot lies past the picture's side (D2R VR Authentic's default: our square window has no room beside the picture, where Unleashed's
    // wide one let the game read the pointer on the outer heroes - 0.213.6, the pointer held at the window's edge)
    std::atomic<int>   pickTurn{2};
    std::atomic<bool>  pointerFit{true};   // pointer_fit: the scene's picture fitted in the window (Pointer)
    std::atomic<float> pointerX{0.0f}, pointerY{0.0f};   // pointer_x / pointer_y: the pointer moved off the dot, parts of the window
    std::atomic<bool>  effects{false};     // effects: D2R_DepthFog.fx in the scene (off: the flat screen has none either)
    std::atomic<bool>  night{false};       // night, night_start, night_end: the mod's distance fog, black, over what lies far off
    std::atomic<float> nightStart{45.0f}, nightEnd{110.0f};
    std::atomic<bool>  skyCam{false};      // sky_cam: the game camera object takes the head's pose as well (an experiment)
    std::atomic<bool>  camFields{false};   // cam_fields (testing): the scene camera's fov/aspect fields from ours
    std::atomic<bool>  sky{true};          // sky: our sky over the scene's void, Act 5's (Harrogath's west gate) - default 1 since 2026-11-27 (Serg: "неба нету")
    std::atomic<bool>  boardClear{true};   // board_clear: the flat interface emptied once the board has its copy
    std::atomic<bool>  menuBoard{true};    // menu_board: the menu's lower strip on a board standing in the scene
    std::atomic<float> menuWidth{1.1f}, menuDistance{1.0f}, menuDrop{0.9f};   // menu_width, menu_distance, menu_drop (metres)
    std::atomic<float> menuTop{0.78f};     // menu_top: where the strip begins, a share of the game's menu band from its top
    // board_as_menus (D2R VR Authentic, default 1): the strip placed and sized as our menus' panel would show it - the menus' width and distance
    // ([openxr] quad width, the panels' distance), the band's centre at the eyes, the strip where it lies in the band, upright (Serg 2026-11-27:
    // "внизу все меню нарисовано ... очень маленькое ... так же, как и меню основной игры"); 0: menu_width / menu_distance / menu_drop as before
    std::atomic<bool>  boardAsMenus{true};
    std::atomic<bool>  caption{true};      // caption: the picked hero's class and words on a board over the heroes
    std::atomic<float> captionRect[4]{{0.25f}, {0.0f}, {0.75f}, {0.33f}};   // caption_left/top/right/bottom: shares of the menu band
    std::atomic<float> captionWidth{3.0f}, captionDistance{6.5f}, captionRise{2.0f};   // caption_width/distance/rise (metres)
    std::atomic<bool>  pano{false};        // panorama: one picture round the scene, in the void (needs sky)
    std::atomic<float> panoYaw{0.0f}, panoLow{-90.0f}, panoHigh{90.0f}, panoBright{1.0f};
    std::atomic<bool>  fireSound{true}, sparks{true};   // fire_sound, sparks
    std::atomic<float> fireVolume{0.9f}, fireBurst{2.0f};   // fire_volume, fire_burst_volume
    std::atomic<float> fireX{1060.67f}, fireY{0.3f}, fireZ{123.94f}, firePick{2.5f};   // fire_x/y/z, fire_pick_radius
    float avoid[4] = {0, 0, 0, 0};          // pick_avoid = x0,y0,x1,y1 (0..1 of the window)
    // [frontend] (Unleashed PR #583): the main menu's act scenes (the title screen and character select: biome act*_frontend_biome; one
    // preset, data/hd/env/preset/ui/frontendact2.json, the five acts' vignettes side by side along z, each for a camera looking down -x).
    std::atomic<bool>  frontOn{false};     // scene_3d (D2R VR Authentic: default 0; Unleashed's `scene`, default 1)
    std::atomic<float> fEye{6.0f}, fUp{2.3f}, fForward{0.0f};   // eye / up / forward, as the campfire's
    // right_N / up_N / forward_N (N = 1..5): one act's head moved from the game's camera besides eye / up / forward (world units). Act 3:
    // at the edge of its pier (the Kurast Docks pier laid along the menu's own; Unleashed's ini: right_3 -2, up_3 0.75).
    std::atomic<float> fRightN[6]{{0.0f}, {0.0f}, {0.0f}, {-2.0f}, {0.0f}, {0.0f}};
    std::atomic<float> fUpN[6]{{0.0f}, {0.0f}, {0.0f}, {0.75f}, {0.0f}, {0.0f}};
    std::atomic<float> fForwardN[6]{};
    // menu_width / menu_distance / menu_drop / menu_top: the main menu's board - half as big again as the campfire's and a little higher
    // (Unleashed's owner 2026-11-13); menu_top 0: all of the menu's band on it (the hero list, Play, the rest)
    std::atomic<float> fMenuWidth{1.65f}, fMenuDistance{1.0f}, fMenuDrop{0.65f}, fMenuTop{0.0f};
    std::atomic<bool>  fSky{true};         // sky: our sky with the act's palette in the scene's void, default 1 (Serg 2026-11-26: "небо черное" with 0)
    // fog: our effect's old fog in the scene (Serg 2026-11-26: "тумана нет на основном меню надо добавить хотя бы старый", "лучше старый") -
    // the act's colour as in the world, fog_start / fog_end (world units; below 0: [fog] start / end, the world's)
    std::atomic<bool>  fFog{false};   // (default 0: Serg 2026-11-26 "в меню вообще отключи туман")
    std::atomic<bool>  fBandAll{true};    // band_all: the band to the interface <-> client helpers too (vr/vrcam.cpp HookClientSize)
    std::atomic<bool>  framePerEye{true}; // [campfire] frame_per_eye: a headset frame at every game frame, the eyes new by turns (vr/xr.cpp OnPresent)
    std::atomic<bool>  fMenuWide{true};   // menu_16x9: the main menu laid out 16:9 (Serg: "лучше, чтобы оно было прямоугольным по умолчанию")
    std::atomic<float> fFogStart{-1.0f}, fFogEnd{-1.0f};
    std::atomic<uint32_t> fActs{0x3e};     // acts: the acts whose scene is made for the headset, as bits 1..5 ("12345")
    // With sky=1 (reshade/D2R_Frontend_Unleashed.fxh): sky_bright / sky_saturation - our act sky this much brighter and this much of its
    // colour kept (Unleashed's Act 1: 4.5 and 0.35, to the scene's painted sky; Acts 2-5 had pictures of their own there, not taken - ours
    // keep their act sky as it is, sky_bright_N / sky_saturation_N 1); sky_blend / _far / _lo / _hi: the painted card's dark edge into it;
    // sky_edges: edges against the void laid over it by coverage; cut_right / cut_left (Act 1), cut_right_N / cut_left_N (Acts 2-5),
    // cut_void, cut_far_right / cut_far_ahead (Act 1): past how far to the camera's right / left (world units) the other acts' scenes are
    // our sky instead (Unleashed's ini as shipped).
    std::atomic<float> fSkyBright[6]{{1.0f}, {1.0f}, {1.0f}, {1.0f}, {1.0f}, {1.0f}};   // (Unleashed's Act 1 4.5 / 0.35 not taken: our act sky as it is,
    std::atomic<float> fSkySat[6]{{1.0f}, {1.0f}, {1.0f}, {1.0f}, {1.0f}, {1.0f}};      //  never tinted - Serg 2026-11-26: the menu washed out over it)
    std::atomic<float> fBlend{1.0f}, fBlendFar{300.0f}, fBlendLo{0.03f}, fBlendHi{0.7f};
    std::atomic<bool>  fEdges{true};
    std::atomic<float> fCutRight[6]{{0.0f}, {232.0f}, {211.0f}, {237.0f}, {234.0f}, {0.0f}};
    std::atomic<float> fCutLeft[6]{{0.0f}, {0.0f}, {115.0f}, {175.0f}, {237.0f}, {234.0f}};
    std::atomic<float> fCutVoid{0.42f}, fCutFarRight{140.0f}, fCutFarAhead{170.0f};
};
Cfg g;

enum Kind { kNone = 0, kCreate = 1, kFront = 2 };
std::atomic<int> g_kind{kNone};        // which menu scene is up (the area's name)
std::atomic<ULONGLONG> g_menuWidePickedAt{0};   // [frontend] menu_16x9 last switched by an icon (GetTickCount64)
std::atomic<int> g_act{0};             // [frontend] the act whose vignette the game's camera is at (View)
std::atomic<float> g_frontRight[2]{{0.0f}, {1.0f}};   // [frontend] the game camera's right on the ground (x, z) - the walls' side (View)
std::atomic<bool> g_scene{false};      // a menu scene of ours is up (the area's name)
std::atomic<bool> g_failed{false};     // this visit gave up: the flat screen until the scene is left
std::atomic<bool> g_now{false};        // Now()
std::atomic<bool> g_arrived{false};    // set as the scene begins: View faces the fire
std::atomic<bool> g_shown{false};      // the scene drawn round you (View faced the fire)
std::atomic<ULONGLONG> g_sceneAt{0};   // when the scene came up
std::atomic<ULONGLONG> g_lastViewAt{0};// the last view built (GetTickCount64)
std::atomic<ULONGLONG> g_sparkAt{0};
std::atomic<uint32_t> g_bursts{0};

// The scene camera's own view and projection (the game's, not ours), for Pointer; and how the room is laid into the scene.
SRWLOCK g_lock = SRWLOCK_INIT;
float g_gameView[16], g_gameProj[16];
float g_canvas = 1.0f;   // the scene's picture (the pointer's area), w/h: the viewport
bool g_gameOk = false;
struct Lay { float base = 0.0f, p0x = 0.0f, p0z = 0.0f, upm = 1.0f, heading = 0.0f; V3 at{}; bool ok = false; };
std::atomic<uint32_t> g_faceGen{0};   // moves at every facing (FaceGen: the main menu's panel placed afresh)
std::atomic<bool> g_toldFramePerEye{false};
Lay g_lay;
xr::Eyes g_eyes{};   // the frame's eyes as View took them (Proj reads them)
bool g_eyesOk = false;

void Fail(const char* why) {
    if (g_failed.exchange(true)) return;
    if (g_kind.load() == kFront) LogF("vrcam: frontend - %s: the flat main menu for this visit ([frontend] scene_3d=1 tries again next time)", why);
    else LogF("vrcam: campfire - %s: the flat character screen for this visit ([campfire] enabled=1 tries again next time)", why);
}
bool KindOn(int kind) { return kind == kCreate ? g.on.load() : kind == kFront ? g.frontOn.load() : false; }

V3 ToWorld(const Lay& l, V3 v) {
    const float cb = cosf(l.base), sb = sinf(l.base);
    return {v.x * cb + v.z * sb, v.y, -v.x * sb + v.z * cb};
}
V3 RoomPoint(const Lay& l, const float p[3]) {   // a point of PairEyes' head frame (metres) in the scene (world units)
    return l.at + ToWorld(l, V3{p[0] - l.p0x, p[1], p[2] - l.p0z}) * l.upm;
}

void Recompute() {
    const int kind = g_kind.load();
    // (not while a cinematic plays: the act's video over the menu scene stood see-through over the frozen scene - Serg 2026-11-27; the video
    // goes to our cinema screen as in a game, [cinema] world_screen)
    const bool want = KindOn(kind) && g_scene.load() && !g_failed.load() && g_set.platform.load() == 1 && xr::On() && xr::Running() && g_h.camera &&
                      !gamestate::CinemaPlaying();
    static int wasKind = kNone;
    const bool was = g_now.exchange(want);
    if (want != was || (want && kind != wasKind)) {
        if (want) { g_arrived.store(true); g_shown.store(false); g_sceneAt.store(GetTickCount64()); g_lastViewAt.store(0); }
        else g_shown.store(false);
        if (want && was) xr::SetCampfire(false);   // one scene straight into the other: the boards placed afresh
        xr::SetCampfire(want);
        LogF("vrcam: %s", !want ? "campfire - the scene left (or off): the flat menus"
                          : kind == kFront ? "frontend - the main menu's act scene in stereo, round you ([frontend] scene_3d)"
                                           : "campfire - the creation scene in stereo, round you");
    }
    wasKind = want ? kind : kNone;
}

// The window's point (client pixels) of a place on the scene camera's picture (nx, ny: its clip coordinates), as the game reads its pointer back:
// the scene's picture (the viewport's shape, `canvas`) fitted whole into the pointer's area, bars beside or above it (Unleashed's pointer_fit) -
// the area being the window, or with pointer_band the 16:9 band at its top while the interface is laid out in that band.
bool ClientPoint(float nx, float ny, float canvas, POINT* out, RECT* rcOut = nullptr) {
    RECT rc{};
    if (!g_gameWnd || !GetClientRect(g_gameWnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    float areaH = float(rc.bottom);
    if (g.pointerBand.load() && vrres::UiBandActive()) areaH = std::min(areaH, float(rc.right) * 9.0f / 16.0f);
    float x0 = 0.0f, y0 = 0.0f, pw = float(rc.right), ph = areaH;
    if (g.pointerFit.load() && canvas > 0.0f) {
        const float scene = canvas, window = float(rc.right) / areaH;
        if (window > scene) { pw = ph * scene; x0 = 0.5f * (float(rc.right) - pw); }
        else                { ph = pw / scene; y0 = 0.5f * (areaH - ph); }
    }
    POINT p{LONG(x0 + (nx * 0.5f + 0.5f + g.pointerX.load()) * pw), LONG(y0 + (0.5f - ny * 0.5f - g.pointerY.load()) * ph)};
    p.x = std::clamp<LONG>(p.x, 0, rc.right - 1);
    p.y = std::clamp<LONG>(p.y, 0, rc.bottom - 1);
    *out = p;
    if (rcOut) *rcOut = rc;
    return true;
}

// The eight heroes on the scene camera's picture, left to right (gamestate kCreateHeroes: Amazon ... Druid): their heads' clip coordinates for a
// square picture, measured from the game's own Hover answers (2026-11-27; a narrower picture: x by the projection's shape).
constexpr float kHeroNx[8] = {-0.889f, -0.667f, -0.444f, -0.222f, 0.222f, 0.444f, 0.667f, 0.889f};
constexpr float kHeroNy = 0.389f;
struct Snap { int hero = -1; float dist = 1e9f; POINT at{}; };
// The hero the hand's line (o, d) passes nearest, and his spot in the window. Each hero: from the scene camera through his measured spot, out to
// the heroes' ring round the fire (pick_ring) - his head there, his body 7 units down from it.
bool HeroSnap(const float o[3], const float d[3], Snap* out) {
    float V[16], P[16];
    AcquireSRWLockShared(&g_lock);
    const bool ok = g_gameOk;
    const float canvas = g_canvas;
    memcpy(V, g_gameView, sizeof V); memcpy(P, g_gameProj, sizeof P);
    ReleaseSRWLockShared(&g_lock);
    if (!ok || std::fabs(P[0]) < 1e-4f || std::fabs(P[5]) < 1e-4f) return false;
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dl > 1e-5f)) return false;
    const V3 dn{d[0] / dl, d[1] / dl, d[2] / dl};
    V3 cam{};
    cam.x = -(V[12] * V[0] + V[13] * V[1] + V[14] * V[2]);
    cam.y = -(V[12] * V[4] + V[13] * V[5] + V[14] * V[6]);
    cam.z = -(V[12] * V[8] + V[13] * V[9] + V[14] * V[10]);
    const V3 ax{V[0], V[4], V[8]}, ay{V[1], V[5], V[9]}, az{V[2], V[6], V[10]};   // the camera's axes in the world (row vectors: e = w * V)
    const float z = P[11] < 0.0f ? -1.0f : 1.0f;   // ahead in the camera's own space (clip w = 1 there)
    const float shape = std::fabs(P[0] / P[5]);     // 1 for a square picture
    const float fireX = g.fireX.load(), fireZ = g.fireZ.load();
    const float R = g.pickRing.load() > 0.0f ? g.pickRing.load() : 15.0f;
    Snap best;
    for (int i = 0; i < 8; ++i) {
        const float nx = kHeroNx[i] * shape, ny = kHeroNy;
        const float e0 = (nx - z * P[8]) / P[0], e1 = (ny - z * P[9]) / P[5];
        const V3 dir = ax * e0 + ay * e1 + az * z;
        float s = 25.0f;   // (no crossing: about where they stand)
        {
            const float px = cam.x - fireX, pz = cam.z - fireZ;
            const float a = dir.x * dir.x + dir.z * dir.z, bq = px * dir.x + pz * dir.z, c = px * px + pz * pz - R * R;
            const float disc = bq * bq - a * c;
            if (a > 1e-6f && disc >= 0.0f) { const float t = (-bq + std::sqrt(disc)) / a; if (t > 1.0f && t < 200.0f) s = t; }
        }
        const V3 head = cam + dir * s;
        float nearest = 1e9f;
        for (int k = 0; k <= 7; ++k) {   // the body: the head and down
            const V3 r{head.x - o[0], head.y - float(k) - o[1], head.z - o[2]};
            const float t = r.x * dn.x + r.y * dn.y + r.z * dn.z;
            if (t <= 0.5f) continue;   // behind the hand
            const V3 off{r.x - dn.x * t, r.y - dn.y * t, r.z - dn.z * t};
            nearest = std::min(nearest, std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z));
        }
        if (nearest < best.dist) {
            POINT p{};
            if (!ClientPoint(nx, ny, canvas, &p)) return false;
            best.hero = i; best.dist = nearest; best.at = p;
        }
    }
    if (best.hero < 0 || best.dist > g.pickSnap.load()) return false;
    *out = best;
    return true;
}

// The game's pointer onto the point `dist` along the ray, as the scene's OWN camera sees it: the character screen finds the hero under the
// pointer in its update, where d2rcam hands the camera its own pose back (HandBackPoseForTick) - placed by the headset's view, the pick went
// the other way as the head turned.
bool Pointer(const float o[3], const float d[3], POINT* where) {
    if (!g_gameWnd || !GameFocused()) return false;
    float V[16], P[16];
    AcquireSRWLockShared(&g_lock);
    const bool ok = g_gameOk;
    const float canvas = g_canvas;
    memcpy(V, g_gameView, sizeof V); memcpy(P, g_gameProj, sizeof P);
    ReleaseSRWLockShared(&g_lock);
    if (!ok) return false;
    float t = g.pickDist.load();
    if (const float R = g.pickRing.load(); R > 0.0f) {   // pick_ring: where the ray (on the ground's plane) meets the heroes' ring round the fire
        const float px = o[0] - g.fireX.load(), pz = o[2] - g.fireZ.load();
        const float a = d[0] * d[0] + d[2] * d[2], bq = px * d[0] + pz * d[2], c = px * px + pz * pz - R * R;
        const float disc = bq * bq - a * c;
        if (a > 1e-6f && disc >= 0.0f) {
            const float tr = (-bq + std::sqrt(disc)) / a;   // the far crossing (the eyes stand inside the ring)
            if (tr > 0.5f && tr < 400.0f) t = tr;
        }
    }
    const float w[3] = {o[0] + d[0] * t, o[1] + d[1] * t + g.pickLift.load(), o[2] + d[2] * t};
    // The scene camera turned about the world's up toward the dot for the game's next tick (d2rcam SetActivePickTurn), and the pointer
    // placed on the turned camera's picture: the pointer cannot leave that picture, and turned so, the dot lies near its middle.
    {
        float cam[3];
        for (int k = 0; k < 3; ++k) cam[k] = -(V[12] * V[4 * k] + V[13] * V[4 * k + 1] + V[14] * V[4 * k + 2]);
        const float fx = -V[2], fz = -V[10], tx = w[0] - cam[0], tz = w[2] - cam[2];
        float a = 0.0f;
        bool turn = g.pickTurn.load() == 1;
        if (g.pickTurn.load() == 2) {   // auto: the dot's place on the camera's own picture first - past its side, the camera turned
            float ee[4], cc[4];
            for (int j = 0; j < 4; ++j) ee[j] = w[0] * V[j] + w[1] * V[4 + j] + w[2] * V[8 + j] + V[12 + j];
            for (int j = 0; j < 4; ++j) cc[j] = ee[0] * P[j] + ee[1] * P[4 + j] + ee[2] * P[8 + j] + ee[3] * P[12 + j];
            turn = !(cc[3] > 0.01f) || std::fabs(cc[0] / cc[3]) > 0.9f;
        }
        if (turn && std::hypot(fx, fz) > 1e-4f && std::hypot(tx, tz) > 1e-4f) {
            a = atan2f(tx, tz) - atan2f(fx, fz);
            while (a > 3.14159265f) a -= 6.2831853f;
            while (a < -3.14159265f) a += 6.2831853f;
            a = std::clamp(a, -1.3f, 1.3f);
        }
        d2rcam::SetActivePickTurn(a);
        if (a != 0.0f) {   // V with the camera's axes turned by a about +y, about the camera's place
            const float ca = cosf(a), sa = sinf(a);
            for (int i = 0; i < 3; ++i) {
                const float x = V[i], z = V[8 + i];
                V[i] = x * ca + z * sa;
                V[8 + i] = -x * sa + z * ca;
                V[12 + i] = -(cam[0] * V[i] + cam[1] * V[4 + i] + cam[2] * V[8 + i]);
            }
        }
    }
    float e[4], c[4];   // row vectors: the eye's, then the clip's
    for (int j = 0; j < 4; ++j) e[j] = w[0] * V[j] + w[1] * V[4 + j] + w[2] * V[8 + j] + V[12 + j];
    for (int j = 0; j < 4; ++j) c[j] = e[0] * P[j] + e[1] * P[4 + j] + e[2] * P[8 + j] + e[3] * P[12 + j];
    if (c[3] < 0.01f) return false;   // behind the camera
    const float nx = c[0] / c[3], ny = c[1] / c[3];
    // Past the picture's edge too: the outer heroes stand beside the headset-shaped picture, in the window's side bars - the game's pick
    // reads the pointer there as well. Only the window's own edge holds the pointer back (below).
    if (std::fabs(nx) > 8.0f || std::fabs(ny) > 8.0f) return false;
    // The scene's picture (the viewport) fitted whole in the pointer's area, bars beside or above it - how the game reads its pointer back.
    RECT rc{};
    POINT p{};
    if (!ClientPoint(nx, ny, canvas, &p, &rc)) return false;
    {
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 3000) {
            told = GetTickCount64();
            float cam[3];
            for (int k = 0; k < 3; ++k) cam[k] = -(V[12] * V[4 * k] + V[13] * V[4 * k + 1] + V[14] * V[4 * k + 2]);
            LogF("vrcam: campfire pointer - window %ldx%ld, picture w/h %.3f, camera w/h %.3f, at %.3f %.3f -> %ld %ld; the hand %.1f %.1f %.1f "
                 "along %.2f %.2f %.2f, its dot %.1f %.1f %.1f, the camera %.1f %.1f %.1f, the eyes %.1f %.1f %.1f",
                 rc.right, rc.bottom, canvas, P[0] != 0.0f ? P[5] / P[0] : 0.0f, nx, ny, p.x, p.y, o[0], o[1], o[2], d[0], d[1], d[2],
                 w[0], w[1], w[2], cam[0], cam[1], cam[2], g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load());
        }
    }
    {   // [campfire] pick_avoid: a 2D control of the character screen whose hot spot lies under the scene - the pointer kept off it
        const float* A = g.avoid;
        if (A[2] > A[0] && A[3] > A[1]) {
            const float fx = float(p.x) / float(std::max(1L, rc.right)), fy = float(p.y) / float(std::max(1L, rc.bottom));
            if (fx >= A[0] && fx <= A[2] && fy >= A[1] && fy <= A[3]) { p.x = rc.right / 2; p.y = 1; }
        }
    }
    if (!ClientToScreen(g_gameWnd, &p)) return false;
    *where = p;
    return true;
}

// The fire under the ray: where the ray comes down to the logs' height (fire_y and one unit over it) lies within fire_pick_radius of the
// fire, ahead of the hand - a ray at a hero behind the fire passes over it and comes down far beyond; or straight at the burning logs.
bool FireHit(const float o[3], const float d[3]) {
    const float F[3] = {g.fireX.load(), g.fireY.load(), g.fireZ.load()};
    const float h = F[1] + 1.0f;
    if (!(d[1] < -1e-4f)) return false;
    const float t = (h - o[1]) / d[1];
    if (!(t > 0.0f)) return false;
    const float x = o[0] + d[0] * t - F[0], z = o[2] + d[2] * t - F[2];
    const float dist = std::sqrt(x * x + z * z);
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 1500) {
        told = GetTickCount64();
        LogF("vrcam: campfire - a click comes down %.1f units from the fire (radius %.1f)", dist, g.firePick.load());
    }
    if (dist <= g.firePick.load()) return true;
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k <= 3; ++k) {
        const float p[3] = {F[0] - o[0], F[1] + 0.5f * float(k) - o[1], F[2] - o[2]};
        const float tt = (p[0] * d[0] + p[1] * d[1] + p[2] * d[2]) / dl;
        const float c[3] = {p[0] - d[0] / dl * tt, p[1] - d[1] / dl * tt, p[2] - d[2] / dl * tt};
        if (tt > 0.0f && std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) <= 1.5f) return true;
    }
    return false;
}

// The right hand's ray in the scene (world units), its trigger and A, from the open frame.
bool HandRay(float o[3], float d[3], bool* held) {
    float ro[3], rd[3], trigger = 0.0f;
    uint32_t buttons = 0;
    if (!xr::CampfireAim(-1, ro, rd, &trigger, &buttons)) { if (held) *held = false; return false; }   // (the laser's hand, as its beam)
    if (held) *held = trigger > 0.55f || (buttons & BW_LINK_XRB_PRIMARY) != 0;
    AcquireSRWLockShared(&g_lock);
    const Lay l = g_lay;
    ReleaseSRWLockShared(&g_lock);
    if (!l.ok) return false;
    const V3 a = RoomPoint(l, ro), b = ToWorld(l, V3{rd[0], rd[1], rd[2]});
    o[0] = a.x; o[1] = a.y; o[2] = a.z;
    d[0] = b.x; d[1] = b.y; d[2] = b.z;
    return true;
}

}  // namespace

void Load() {
    const bool on = IniB(L"campfire", L"enabled", false);
    static int told = -1;
    if (int(on) != told) { told = int(on); LogF("vrcam: [campfire] enabled=%d", int(on)); }
    g.on.store(on);
    g.eye.store(std::clamp(IniF(L"campfire", L"eye", 6.0f), 1.0f, 40.0f));
    g.up.store(std::clamp(IniF(L"campfire", L"up", 2.25f), -40.0f, 40.0f));
    g.forward.store(std::clamp(IniF(L"campfire", L"forward", 3.0f), -200.0f, 200.0f));
    g.pickDist.store(std::clamp(IniF(L"campfire", L"pick_distance", 22.0f), 1.0f, 200.0f));
    g.pickRing.store(std::clamp(IniF(L"campfire", L"pick_ring", 15.0f), 0.0f, 200.0f));
    g.pointerBand.store(IniB(L"campfire", L"pointer_band", true));
    g.pickHeroes.store(IniB(L"campfire", L"pick_heroes", true));
    g.pickSnap.store(std::clamp(IniF(L"campfire", L"pick_snap", 3.0f), 0.2f, 20.0f));
    g.pickRay.store(IniB(L"campfire", L"pick_ray", false));
    if (const bool each = IniB(L"campfire", L"frame_per_eye", true); g.framePerEye.exchange(each) != each || !g_toldFramePerEye.exchange(true))
        Log(each ? "vrcam: campfire / frontend - a headset frame at every game frame, the eyes new by turns ([campfire] frame_per_eye=1)"
                 : "vrcam: campfire / frontend - a headset frame for every two game frames, both eyes of one frame ([campfire] frame_per_eye=0)");
    g.effects.store(IniB(L"campfire", L"effects", false));
    g.night.store(IniB(L"campfire", L"night", false));
    g.skyCam.store(IniB(L"campfire", L"sky_cam", false));
    g.sky.store(IniB(L"campfire", L"sky", true));
    g.boardClear.store(IniB(L"campfire", L"board_clear", true));
    g.camFields.store(IniB(L"campfire", L"cam_fields", false));
    g.nightStart.store(std::clamp(IniF(L"campfire", L"night_start", 45.0f), 0.0f, 2000.0f));
    g.nightEnd.store(std::clamp(IniF(L"campfire", L"night_end", 110.0f), 1.0f, 4000.0f));
    g.pointerX.store(std::clamp(IniF(L"campfire", L"pointer_x", 0.0f), -0.5f, 0.5f));
    g.pointerY.store(std::clamp(IniF(L"campfire", L"pointer_y", 0.0f), -0.5f, 0.5f));
    g.pointerFit.store(IniB(L"campfire", L"pointer_fit", true));
    g.pickTurn.store(std::clamp((int)IniF(L"campfire", L"pick_turn", 2.0f), 0, 2));
    g.pickLift.store(std::clamp(IniF(L"campfire", L"pick_raise", 0.0f), -20.0f, 20.0f));   // (pick_lift, Unleashed's 5.5, no longer read)
    g.menuBoard.store(IniB(L"campfire", L"menu_board", true));
    g.menuWidth.store(std::clamp(IniF(L"campfire", L"menu_width", 1.1f), 0.3f, 3.0f));
    g.menuDistance.store(std::clamp(IniF(L"campfire", L"menu_distance", 1.0f), 0.4f, 3.0f));
    g.menuDrop.store(std::clamp(IniF(L"campfire", L"menu_drop", 0.9f), -0.5f, 1.5f));
    g.menuTop.store(std::clamp(IniF(L"campfire", L"menu_top", 0.78f), 0.0f, 0.98f));
    g.boardAsMenus.store(IniB(L"campfire", L"board_as_menus", true));
    g.caption.store(IniB(L"campfire", L"caption", true));
    g.pano.store(IniB(L"campfire", L"panorama", false));
    g.fireSound.store(IniB(L"campfire", L"fire_sound", true));
    g.sparks.store(IniB(L"campfire", L"sparks", true));
    g.fireVolume.store(std::clamp(IniF(L"campfire", L"fire_volume", 0.9f), 0.0f, 4.0f));
    g.fireBurst.store(std::clamp(IniF(L"campfire", L"fire_burst_volume", 2.0f), 0.0f, 6.0f));
    g.fireX.store(IniF(L"campfire", L"fire_x", 1060.67f));
    g.fireY.store(IniF(L"campfire", L"fire_y", 0.3f));
    g.fireZ.store(IniF(L"campfire", L"fire_z", 123.94f));
    {
        wchar_t buf[96] = {};
        GetPrivateProfileStringW(L"campfire", L"pick_avoid", L"", buf, 96, g_iniPath);
        float a[4] = {0, 0, 0, 0};
        if (swscanf_s(buf, L"%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3]) != 4) a[0] = a[1] = a[2] = a[3] = 0;
        for (int i = 0; i < 4; ++i) g.avoid[i] = a[i];
    }
    g.firePick.store(std::clamp(IniF(L"campfire", L"fire_pick_radius", 2.5f), 0.5f, 30.0f));
    g.panoYaw.store(std::clamp(IniF(L"campfire", L"pano_yaw", 0.0f), -360.0f, 360.0f));
    g.panoLow.store(std::clamp(IniF(L"campfire", L"pano_low", -90.0f), -90.0f, 89.0f));
    g.panoHigh.store(std::clamp(IniF(L"campfire", L"pano_high", 90.0f), -89.0f, 90.0f));
    g.panoBright.store(std::clamp(IniF(L"campfire", L"pano_brightness", 1.0f), 0.0f, 4.0f));
    g.captionRect[0].store(std::clamp(IniF(L"campfire", L"caption_left", 0.25f), 0.0f, 1.0f));
    g.captionRect[1].store(std::clamp(IniF(L"campfire", L"caption_top", 0.0f), 0.0f, 1.0f));
    g.captionRect[2].store(std::clamp(IniF(L"campfire", L"caption_right", 0.75f), 0.0f, 1.0f));
    g.captionRect[3].store(std::clamp(IniF(L"campfire", L"caption_bottom", 0.33f), 0.0f, 1.0f));
    g.captionWidth.store(std::clamp(IniF(L"campfire", L"caption_width", 3.0f), 0.2f, 20.0f));
    g.captionDistance.store(std::clamp(IniF(L"campfire", L"caption_distance", 6.5f), 0.5f, 30.0f));
    g.captionRise.store(std::clamp(IniF(L"campfire", L"caption_rise", 2.0f), -3.0f, 10.0f));
    {   // [frontend] (Unleashed PR #583's keys and defaults; scene_3d is ours, off by default)
        const bool front = IniB(L"frontend", L"scene_3d", false);
        static int toldFront = -1;
        if (int(front) != toldFront) { toldFront = int(front); LogF("vrcam: [frontend] scene_3d=%d", int(front)); }
        g.frontOn.store(front);
        g.fEye.store(std::clamp(IniF(L"frontend", L"eye", 6.0f), 1.0f, 40.0f));
        g.fUp.store(std::clamp(IniF(L"frontend", L"up", 2.3f), -200.0f, 200.0f));
        g.fForward.store(std::clamp(IniF(L"frontend", L"forward", 0.0f), -400.0f, 400.0f));
        g.fMenuTop.store(std::clamp(IniF(L"frontend", L"menu_top", 0.0f), 0.0f, 0.98f));
        g.fMenuWidth.store(std::clamp(IniF(L"frontend", L"menu_width", 1.65f), 0.3f, 4.0f));
        g.fMenuDistance.store(std::clamp(IniF(L"frontend", L"menu_distance", 1.0f), 0.4f, 3.0f));
        g.fMenuDrop.store(std::clamp(IniF(L"frontend", L"menu_drop", 0.65f), -0.5f, 1.5f));
        g.fSky.store(IniB(L"frontend", L"sky", true));
        g.fFog.store(IniB(L"frontend", L"fog", false));
        g.fBandAll.store(IniB(L"frontend", L"band_all", true));
        if (GetTickCount64() - g_menuWidePickedAt.load() > 3000)   // (an icon's click held over the ini until its own write is read back)
            g.fMenuWide.store(IniB(L"frontend", L"menu_16x9", true));
        g.fFogStart.store(std::clamp(IniF(L"frontend", L"fog_start", -1.0f), -1.0f, 100000.0f));
        g.fFogEnd.store(std::clamp(IniF(L"frontend", L"fog_end", -1.0f), -1.0f, 100000.0f));
        static const float kSide[6] = {0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f};
        static const float kUp[6] = {0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f};
        for (int n = 1; n <= 5; ++n) {
            wchar_t k[16];
            swprintf_s(k, L"right_%d", n);
            g.fRightN[n].store(std::clamp(IniF(L"frontend", k, kSide[n]), -200.0f, 200.0f));
            swprintf_s(k, L"up_%d", n);
            g.fUpN[n].store(std::clamp(IniF(L"frontend", k, kUp[n]), -200.0f, 200.0f));
            swprintf_s(k, L"forward_%d", n);
            g.fForwardN[n].store(std::clamp(IniF(L"frontend", k, 0.0f), -400.0f, 400.0f));
        }
        static const float kRight[6] = {0.0f, 232.0f, 211.0f, 237.0f, 234.0f, 0.0f};
        static const float kLeft[6] = {0.0f, 0.0f, 115.0f, 175.0f, 237.0f, 234.0f};
        for (int n = 1; n <= 5; ++n) {
            wchar_t r[24], l[24], b[24], s[24];
            if (n == 1) { wcscpy_s(r, L"cut_right"); wcscpy_s(l, L"cut_left"); wcscpy_s(b, L"sky_bright"); wcscpy_s(s, L"sky_saturation"); }
            else {
                swprintf_s(r, L"cut_right_%d", n); swprintf_s(l, L"cut_left_%d", n);
                swprintf_s(b, L"sky_bright_%d", n); swprintf_s(s, L"sky_saturation_%d", n);
            }
            g.fCutRight[n].store(std::clamp(IniF(L"frontend", r, kRight[n]), 0.0f, 100000.0f));
            g.fCutLeft[n].store(std::clamp(IniF(L"frontend", l, kLeft[n]), 0.0f, 100000.0f));
            g.fSkyBright[n].store(std::clamp(IniF(L"frontend", b, 1.0f), 0.1f, 20.0f));
            g.fSkySat[n].store(std::clamp(IniF(L"frontend", s, 1.0f), 0.0f, 2.0f));
        }
        g.fBlend.store(std::clamp(IniF(L"frontend", L"sky_blend", 1.0f), 0.0f, 1.0f));   // (Unleashed's code default 1: Act 1's far dark backdrop into our sky)
        g.fBlendFar.store(std::clamp(IniF(L"frontend", L"sky_blend_far", 300.0f), 1.0f, 100000.0f));
        g.fBlendLo.store(std::clamp(IniF(L"frontend", L"sky_blend_lo", 0.03f), 0.0f, 1.0f));
        g.fBlendHi.store(std::clamp(IniF(L"frontend", L"sky_blend_hi", 0.7f), 0.001f, 1.0f));
        g.fEdges.store(IniB(L"frontend", L"sky_edges", true));
        g.fCutVoid.store(std::clamp(IniF(L"frontend", L"cut_void", 0.42f), 0.0f, 2.0f));
        g.fCutFarRight.store(std::clamp(IniF(L"frontend", L"cut_far_right", 140.0f), 0.0f, 100000.0f));
        g.fCutFarAhead.store(std::clamp(IniF(L"frontend", L"cut_far_ahead", 170.0f), 0.0f, 100000.0f));
        wchar_t acts[16] = L"";
        GetPrivateProfileStringW(L"frontend", L"acts", L"12345", acts, 16, g_iniPath);
        uint32_t bits = 0;
        for (const wchar_t* c = acts; *c; ++c) if (*c >= L'1' && *c <= L'5') bits |= 1u << (*c - L'0');
        g.fActs.store(bits);
    }
    Recompute();
}

void OnBiome(const std::string& biome) {
    // Character creation: 'ui_characterselectscreen'. The main menu (the title screen, character select): act*_frontend_biome (Unleashed's
    // test: the name holds "_frontend").
    const int kind = biome.rfind("ui_characterselect", 0) == 0 ? kCreate : biome.find("_frontend") != std::string::npos ? kFront : kNone;
    const bool scene = kind != kNone;
    const int wasKind = g_kind.exchange(kind);
    if (scene != g_scene.exchange(scene) || kind != wasKind) {
        g_failed.store(false);   // left (or the other scene): the next visit tries again
        if (kind == kFront) g_act.store(0);
        if (scene && KindOn(kind)) LogF("vrcam: %s - '%s' up", kind == kFront ? "frontend" : "campfire", biome.c_str());
    }
    Recompute();
}

bool Now() { return g_now.load(); }

bool View(const d2rcam::WorldView& in, float out[16]) {
    if (!g_now.load()) return false;
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_gameView, in.gameView, sizeof g_gameView);
    memcpy(g_gameProj, in.gameProj, sizeof g_gameProj);
    g_canvas = in.viewportW > 0.0f && in.viewportH > 0.0f ? in.viewportW / in.viewportH : 1.0f;
    g_gameOk = true;
    ReleaseSRWLockExclusive(&g_lock);
    // Both eyes at one frame's poses (no eye built from the frame before's): the left eye's camera opens the headset's frame.
    if (!xr::OpenCampfireFrame()) return false;
    xr::Eyes xe{};
    if (!xr::PairEyes(&xe) || xe.mode != xr::kViewsHead || !EyesSane(xe)) return false;
    const float* G = in.gameView;   // row-major, v * M
    const V3 cam = {-G[12] * G[0] - G[13] * G[1] - G[14] * G[2],
                    -G[12] * G[4] - G[13] * G[5] - G[14] * G[6],
                    -G[12] * G[8] - G[13] * G[9] - G[14] * G[10]};
    const V3 gameFwd = {-G[2], -G[6], -G[10]};
    const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
    const float flat = std::hypot(gameFwd.x, gameFwd.z);
    const V3 ahead = flat > 1e-5f ? V3{gameFwd.x / flat, 0.0f, gameFwd.z / flat} : V3{0.0f, 0.0f, -1.0f};
    const bool front = g_kind.load() == kFront;
    const float eyeH = front ? g.fEye.load() : g.eye.load();
    V3 at = cam + ahead * (front ? g.fForward.load() : g.forward.load()) + V3{0.0f, front ? g.fUp.load() : g.up.load(), 0.0f};
    if (front) {
        // [frontend] which act's vignette: the nearest of the game's cameras along the row (z 483, 110, -313.5, -789, -1258.5; Unleashed's
        // log 2026-11-13 - all look down -x), then that act's own place (right_N / up_N / forward_N)
        static const float kActZ[5] = {483.0f, 110.0f, -313.5f, -789.0f, -1258.5f};
        int act = 1;
        for (int i = 1; i < 5; ++i) if (std::fabs(cam.z - kActZ[i]) < std::fabs(cam.z - kActZ[act - 1])) act = i + 1;
        if (g_act.exchange(act) != act)
            LogF("vrcam: frontend - act %d's scene: the game's camera at %.1f %.1f %.1f, facing %.2f %.2f %.2f", act, cam.x, cam.y, cam.z,
                 gameFwd.x, gameFwd.y, gameFwd.z);
        const V3 right = {-ahead.z, 0.0f, ahead.x};
        g_frontRight[0].store(right.x); g_frontRight[1].store(right.z);
        at = at + ahead * g.fForwardN[act].load() + right * g.fRightN[act].load() + V3{0.0f, g.fUpN[act].load(), 0.0f};
    }
    AcquireSRWLockExclusive(&g_lock);
    Lay& l = g_lay;
    l.at = at;
    l.upm = eyeH / std::max(0.5f, UserEyeHeightM());   // world units per metre: the scene's scale from the eye height
    // Facing the fire on arrival, whichever way the head was turned; taken once on each arrival, from the head as it is then; it also puts
    // the eyes' origin at the head, so you stand where the game's camera stood.
    // [frontend]: faced again when the game's camera turns (the title screen's looks 45 deg off character select's - faced on the title
    // screen, the hero stood off to the side in character select and the panel kept coming round; Serg 2026-11-26)
    float turned = heading0 - l.heading;
    turned = atan2f(sinf(turned), cosf(turned));
    if (g_arrived.exchange(false) || !l.ok || (front && std::fabs(turned) > 0.35f)) {
        const V3 hf = d2rcam::m4::Rotate(xe.quat[0], {0.0f, 0.0f, -1.0f});
        const float roomYaw = atan2f(-hf.x, -hf.z);
        l.base = heading0 - roomYaw;
        l.p0x = 0.5f * (xe.pos[0][0] + xe.pos[1][0]);
        l.p0z = 0.5f * (xe.pos[0][2] + xe.pos[1][2]);
        l.heading = heading0;
        l.ok = true;
        g_faceGen.fetch_add(1);
        g_shown.store(true);
        LogF("vrcam: %s (the head was turned %.0f deg in the room), %.2f units a metre",
             front ? "frontend - facing the act's scene" : "campfire - facing the fire", roomYaw * 57.2957795f, l.upm);
    }
    const Lay lay = l;
    g_eyes = xe;
    g_eyesOk = true;
    ReleaseSRWLockExclusive(&g_lock);
    const int e = g_eye.load() & 1;
    const V3 pos = RoomPoint(lay, xe.pos[e]);
    const V3 fwd = ToWorld(lay, d2rcam::m4::Rotate(xe.quat[e], {0.0f, 0.0f, -1.0f}));
    const V3 up = ToWorld(lay, d2rcam::m4::Rotate(xe.quat[e], {0.0f, 1.0f, 0.0f}));
    float view[16];
    d2rcam::m4::LookTo(pos, fwd, up, view);
    if (!std::isfinite(view[0] + view[5] + view[10] + view[12] + view[13] + view[14])) return false;
    {   // What the hands' rays hang off: the head's centre in the world and its right and ahead on the ground.
        const float mid[3] = {0.5f * (xe.pos[0][0] + xe.pos[1][0]), 0.5f * (xe.pos[0][1] + xe.pos[1][1]), 0.5f * (xe.pos[0][2] + xe.pos[1][2])};
        const V3 head = RoomPoint(lay, mid);
        const float h = std::hypot(fwd.x, fwd.z);
        const V3 a = h > 1e-5f ? V3{fwd.x / h, 0.0f, fwd.z / h} : ahead;
        g_eyeWorld[0].store(head.x); g_eyeWorld[1].store(head.y); g_eyeWorld[2].store(head.z);
        for (int i = 0; i < 3; ++i) g_eyeFromLook[i].store(0.0f);
        g_eyeOk.store(true);
        StickFrame sf{};
        sf.camR[0] = -a.z; sf.camR[1] = 0.0f; sf.camR[2] = a.x;
        sf.camF[0] = a.x;  sf.camF[1] = 0.0f; sf.camF[2] = a.z;
        sf.camLook[0] = fwd.x; sf.camLook[1] = fwd.y; sf.camLook[2] = fwd.z;
        sf.scrR[0] = G[0]; sf.scrR[1] = G[4]; sf.scrR[2] = G[8];
        sf.scrU[0] = G[1]; sf.scrU[1] = G[5]; sf.scrU[2] = G[9];
        sf.mirX = in.gameProj[0] < 0.0f ? -1.0f : 1.0f;
        sf.mirY = in.gameProj[5] < 0.0f ? -1.0f : 1.0f;
        sf.valid = true;
        AcquireSRWLockExclusive(&g_stickLock); g_stickFrame = sf; ReleaseSRWLockExclusive(&g_stickLock);
    }
    {   // This eye's camera (the sparks read it).
        const V3 ground = at - V3{0.0f, eyeH, 0.0f};   // "the hero": the point under the eyes
        AcquireSRWLockExclusive(&g_skyLock);
        SkyView& sv = g_skyView[e];
        for (int i = 0; i < 3; ++i) { sv.axes[i] = view[i * 4]; sv.axes[3 + i] = view[i * 4 + 1]; sv.axes[6 + i] = view[i * 4 + 2]; }
        sv.eyeRel[0] = pos.x - ground.x; sv.eyeRel[1] = pos.y - ground.y; sv.eyeRel[2] = pos.z - ground.z;
        sv.hero[0] = ground.x; sv.hero[1] = ground.y; sv.hero[2] = ground.z;
        sv.axes_ok = true;
        ReleaseSRWLockExclusive(&g_skyLock);
    }
    g_viewBuilds.fetch_add(1);
    g_lastViewAt.store(GetTickCount64());
    memcpy(out, view, sizeof view);
    return true;
}

bool Proj(const d2rcam::WorldView& in, float M[16]) {
    (void)in;
    if (!g_now.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool ok = g_eyesOk;
    const xr::Eyes xe = g_eyes;
    ReleaseSRWLockShared(&g_lock);
    if (!ok) return false;
    const int e = g_eye.load() & 1;
    const float nearZ = std::max(g_set.nearClip.load(), 0.05f);
    // the effect turns depth into distance with it (NearPlane): left at the last world's (1.5 from the start), every distance in the menu
    // scene came out 30 times too far with [camera] near=0 - the [frontend] walls between the acts stood a few units to each side and only
    // a strip of the act was left (Serg 2026-11-26: "почему то видно только кусок мира")
    g_lastNear.store(nearZ);
    const eyefov::Proj pe = eyefov::Projection({xe.tanL[e], xe.tanR[e], xe.tanU[e], xe.tanD[e]});
    d2rcam::m4::PerspectiveRevZ(1.0f, 1.0f, nearZ, M);
    M[0] = pe.sx;
    M[5] = pe.sy;
    M[8] = pe.offX;
    M[9] = pe.offY;
    AcquireSRWLockExclusive(&g_skyLock);
    SkyView& sv = g_skyView[e];
    sv.proj[0] = M[0]; sv.proj[1] = M[5]; sv.proj[2] = M[8]; sv.proj[3] = M[9]; sv.proj_ok = true;
    ReleaseSRWLockExclusive(&g_skyLock);
    return true;
}

void Frame() {
    static bool down = false; static int stage = 0; static ULONGLONG at = 0;
    static bool fireDown = false;
    // The main menu: nothing to pick in the scene (the heroes are the menu's list, on the board - the laser clicks there, vr/xr.cpp)
    const bool on = g_now.load() && g_kind.load() == kCreate;
    const ULONGLONG nowMs = GetTickCount64();
    float o[3] = {}, d[3] = {};
    bool rawHeld = false;
    const bool ray = on && HandRay(o, d, &rawHeld);
    // (the line and its trigger come with every other call only - the eyes by turns: both held over the calls without one, 300 ms; read as
    // let go there, one pull was a press and a release every frame - "the fire clicked" a dozen times a pull, no hero ever picked)
    static bool lastHeld = false;
    static ULONGLONG lastRayAt = 0;
    static float lastO[3] = {}, lastD[3] = {};
    if (ray) { lastHeld = rawHeld; lastRayAt = nowMs; memcpy(lastO, o, sizeof lastO); memcpy(lastD, d, sizeof lastD); }
    const bool rayLately = ray || (on && lastRayAt && nowMs - lastRayAt < 300);
    if (!ray && rayLately) { rawHeld = lastHeld; memcpy(o, lastO, sizeof lastO); memcpy(d, lastD, sizeof lastD); }
    rawHeld = rawHeld && on;
    // Whose press: decided as it begins and kept to its release - the laser on the menu's board then: the board's (vr/xr.cpp); else the pick's.
    static bool pressed = false, boardPress = false;
    if (rawHeld && !pressed) boardPress = xr::CampfireBoardHit();
    pressed = rawHeld;
    const bool held = rawHeld && !boardPress;
    const bool onBoard = boardPress || xr::CampfireBoardHit();
    const bool into = rayLately && !onBoard;
    // THE PICK: the game shows and picks the hero under its own POINTER, read on the scene camera's own picture (the head does not come into
    // it) - so the pointer is put where that camera sees what the hand's line is on. Seen working in his log of 2026-11-27 01:29 (4526eb5d):
    // the game's Hover answered the very hero the line passed, six heroes in turn - until the first pull of the trigger: its RT reached the game
    // as a pad's, the game went to its controller layout and took no pointer and no click after. The pad is hidden there now (vr/input/pad.cpp).
    // (9c961b53 gave the hand's line to the game's ray instead and parked the pointer: the game showed no hero at all - its hover is not
    // answered by that ray. pick_ray 1 gives it while the trigger is held, as Unleashed's CampfirePick does; 0, the default: never.)
    // pick_heroes: the hero the line passes nearest - the pointer on his own measured spot, the game's Hover answer watched (no answer in
    // 250 ms: the pointer walks a spiral round the spot, 12 px a ring, and what the game answers to is kept for that hero).
    static bool toldPointer = false;
    static int snapHero = -1, toldHero = -2;
    static ULONGLONG snapSince = 0, hoverLostAt = 0;
    static POINT learned[8] = {}, tried{};
    static bool known[8] = {};
    POINT where{};
    Snap snap;
    const int hov = gamestate::CreateHover();
    const bool snapped = into && g.pickHeroes.load() && GameFocused() && HeroSnap(o, d, &snap);
    if (snapped) {
        d2rcam::SetActivePickTurn(0.0f);   // (their spots are the unturned camera's)
        if (snap.hero != snapHero) { snapHero = snap.hero; snapSince = nowMs; hoverLostAt = 0; tried = {}; }
        POINT off = known[snap.hero] ? learned[snap.hero] : POINT{};
        if (hov == snap.hero || gamestate::CreateSelected() == snap.hero) {
            hoverLostAt = 0;
            if (!known[snap.hero]) {
                known[snap.hero] = true; learned[snap.hero] = tried; off = tried;
                LogF("vrcam: campfire pick - the game answers Hover %s with the pointer %ld,%ld off his measured spot (kept)", gamestate::CreateHeroName(snap.hero),
                     tried.x, tried.y);
            }
        } else if (known[snap.hero]) {
            if (!hoverLostAt) hoverLostAt = nowMs;
            else if (nowMs - hoverLostAt > 700) { known[snap.hero] = false; snapSince = nowMs - 250; }   // the kept spot answers no more: searched again
        } else if (nowMs - snapSince > 250) {
            const int k = 1 + int((nowMs - snapSince - 250) / 120) % 40;   // 5 rings of 8 places, 120 ms each
            const int ring = (k - 1) / 8 + 1, place = (k - 1) % 8;
            static const int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            off = {kDx[place] * ring * 12, kDy[place] * ring * 12};
        }
        tried = off;
        POINT p{snap.at.x + off.x, snap.at.y + off.y};
        if (snap.hero != toldHero) {
            toldHero = snap.hero;
            LogF("vrcam: campfire pick - the hand's line passes %.1f units from %s: the pointer on his spot, window %ld,%ld; the hand %.1f %.1f %.1f along "
                 "%.2f %.2f %.2f; the game's hover now: %s", snap.dist, gamestate::CreateHeroName(snap.hero), p.x, p.y, o[0], o[1], o[2], d[0], d[1], d[2],
                 gamestate::CreateHeroName(hov));
        }
        if (ClientToScreen(g_gameWnd, &p)) SetCursorPos(p.x, p.y);
    } else {
        snapHero = -1;
        if (into) toldHero = -2;   // (aimed off the heroes: the next one said again)
        if (into && Pointer(o, d, &where)) {
            SetCursorPos(where.x, where.y);
            if (!toldPointer) { toldPointer = true; LogF("vrcam: campfire pick - the pointer follows the right hand (%ld, %ld)", where.x, where.y); }
        } else if (on) {
            d2rcam::SetActivePickTurn(0.0f);
        }
    }
    if (held && into && g.pickRay.load()) {   // pick_ray: the hand's own line the game's ray for the click, as Unleashed's ("the ray is in before the click")
        for (int i = 0; i < 3; ++i) { g_aimO[i].store(o[i]); g_aimD[i].store(d[i]); }
        g_aimRay.store(true);
        g_aimRayUntil.store(nowMs + 200);
    }
    {   // what the game makes of it: each change of the hero it shows (at most a few a second), every press and its answer half a second on
        static int toldHover = -2;
        static ULONGLONG toldAt = 0;
        if (on && hov != toldHover && nowMs - toldAt > 250) {
            toldHover = hov; toldAt = nowMs;
            LogF("vrcam: campfire pick - the game's hover: %s (aimed at %s%s)", gamestate::CreateHeroName(hov), gamestate::CreateHeroName(snapped ? snap.hero : -1),
                 into ? "" : onBoard ? "; the laser on the board" : "; no line from the hand");
        }
        static bool wasHeld = false;
        static ULONGLONG answerAt = 0;
        static int pressedHero = -1, hoverAtPress = -1;
        if (held && !wasHeld) {
            pressedHero = snapped ? snap.hero : -1;
            hoverAtPress = hov;
            answerAt = nowMs + 500;
            LogF("vrcam: campfire pick - PRESS: aimed at %s (%.1f units off the line), the game's hover %s, picked so far %s; the hand %.1f %.1f %.1f along %.2f %.2f %.2f",
                 gamestate::CreateHeroName(pressedHero), snapped ? snap.dist : -1.0f, gamestate::CreateHeroName(hov),
                 gamestate::CreateHeroName(gamestate::CreateSelected()), o[0], o[1], o[2], d[0], d[1], d[2]);
        }
        if (answerAt && nowMs >= answerAt) {
            answerAt = 0;
            LogF("vrcam: campfire pick - the game's answer to that press: picked %s (aimed at %s, its hover at the press: %s)",
                 gamestate::CreateHeroName(gamestate::CreateSelected()), gamestate::CreateHeroName(pressedHero), gamestate::CreateHeroName(hoverAtPress));
        }
        wasHeld = held;
    }
    // A click on the fire (not a hero): sparks and a crackle.
    if (held && !down && !fireDown && rayLately && FireHit(o, d)) {
        fireDown = true;
        g_sparkAt.store(GetTickCount64());
        g_bursts.fetch_add(1);
        Log("vrcam: campfire - the fire clicked: sparks and a crackle");
    }
    if (!held) fireDown = false;
    if (fireDown) return;   // the click was the fire's: not the game's
    if (held && rayLately) {
        if (!down && GameFocused()) { down = true; stage = 1; at = GetTickCount64(); }
        if (down && stage == 1 && GetTickCount64() - at >= 20) { SendLeftButton(true); stage = 2; }   // the pointer is in before the click
    }
    if (!held && down) {
        if (stage == 2) SendLeftButton(false);
        down = false; stage = 0;
    }
}

// The fire's own sound (campfire_audio_unleashed.h): its roar and crackle while the scene is up, a flurry of crackles at a click on the
// fire. Its own stream, kept ~150 ms ahead, a short buffer at a time so the click is heard at once (Unleashed's transport Input).
void FireSound() {
    static framevr::fireaudio::Stream stream;
    static framevr::fireaudio::Synth synth;
    static uint32_t seen = 0;
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - at < 10) return;   // (the update thread ticks every ms)
    at = now;
    float ambient = 0.0f, burst = 0.0f;
    uint32_t bursts = 0;
    const bool on = FireSoundWanted(&ambient, &burst, &bursts);
    stream.Reap();
    if (on) {
        if (bursts != seen) { seen = bursts; synth.Burst(); }
        const float gain = framevr::fireaudio::GameVolume();
        for (int k = 0; k < 4 && stream.Remaining() < uint64_t(framevr::fireaudio::Rate * 0.15); ++k) {
            std::vector<int16_t> pcm;
            synth.Render(pcm, framevr::fireaudio::Rate / 25, ambient * gain, burst * gain);
            if (!stream.Queue(framevr::fireaudio::Rate, 1, std::move(pcm))) {
                static bool told = false;
                if (!told) { told = true; Log("vrcam: campfire - the fire's sound did not play (winmm waveOut not there)"); }
                break;
            }
        }
    } else {
        seen = bursts;
        if (stream.Open() && stream.Remaining() == 0) stream.Stop();
    }
}

void Tick() {
    Recompute();
    FireSound();
    fx::g_campfireSky.store(SkyWanted() && g_set.skyOn.load());   // (vr/sky/sky_choice.h OurSkyWanted)
    panelinput::SetPadLayoutFree(g_now.load() && g_kind.load() == kCreate);   // the hero's hover kept: not the pad's focus (vr/panel_input.h)
    hud::SetMenuScene(g_now.load());   // nothing of a game's interface cut out in a menu scene: the map's box, the toolbar, the globes (vr/hud.cpp)
    {   // [frontend] fog: our old fog in the main menu's act scene (vr/sky/sky_choice.h g_frontFog)
        const bool fog = Frontend() && g.fFog.load() && FrontActReady();
        const float start = g.fFogStart.load() >= 0.0f ? g.fFogStart.load() : g_set.fogStart.load();
        const float end = g.fFogEnd.load() >= 0.0f ? g.fFogEnd.load() : g_set.fogEnd.load();
        fx::g_frontFogStart.store(start);
        fx::g_frontFogEnd.store(std::max(end, start + 1.0f));
        fx::g_frontFogAct.store(FrontAct());
        if (fx::g_frontFog.exchange(fog) != fog)
            LogF("vrcam: frontend - %s", fog ? "our old fog in the act's scene ([frontend] fog), the act's colour" : "no fog of ours (the scene left, or [frontend] fog=0)");
    }
    const bool on = g_now.load();
    if (on) {
        // The headset away for a moment (taken off, its own menu, the head not tracked): no view is built then, and that is no failure - the
        // clock stands still (Serg 2026-11-26: the main menu's scene went flat for good after 4 s of an untracked head)
        if (xr::CampfireWaiting()) {
            if (g_lastViewAt.load()) g_lastViewAt.store(GetTickCount64());
            else g_sceneAt.store(GetTickCount64());
        }
        const ULONGLONG now = GetTickCount64(), since = g_sceneAt.load(), last = g_lastViewAt.load();
        if (xr::CampfireNoFrame() > 240) Fail("no headset frame for the scene in 240 presents");
        else if (!last && since && now - since > 8000) Fail("no view of the scene built in 8 s (the scene's camera never ticked)");
        else if (last && now - last > 4000) Fail("no view of the scene built for 4 s");
    }
    if (!on || g_kind.load() != kCreate) d2rcam::SetActivePickTurn(0.0f);   // Pointer sets it by the fire (the main menu picks nothing)
    d2rcam::SetUseActiveCamera(on);
    d2rcam::SetMirrorGameCamera(on && g.skyCam.load());
    d2rcam::SetSyncActiveFields(on && g.camFields.load());
}

uint64_t SparkAt() { return g_sparkAt.load(); }
bool Creation() { return g_now.load() && g_kind.load() == kCreate; }
bool SparksOn() { return Creation() && g.sparks.load(); }
void FirePos(float out[3]) { out[0] = g.fireX.load(); out[1] = g.fireY.load(); out[2] = g.fireZ.load(); }
bool FireSoundWanted(float* ambient, float* burst, uint32_t* bursts) {
    if (ambient) *ambient = g.fireVolume.load();
    if (burst) *burst = g.fireBurst.load();
    if (bursts) *bursts = g_bursts.load();
    return Creation() && g_shown.load() && g.fireSound.load();
}
bool PanelOut() { return g_now.load() && g.menuBoard.load() && !xr::CampfireBoardFailed(); }
bool Frontend() { return g_now.load() && g_kind.load() == kFront; }
bool FrontMenuWide() { return Frontend() && g.fMenuWide.load(); }
bool FrontBandAll() { return g.fBandAll.load(); }
bool FramePerEye() { return g.framePerEye.load(); }
void SetFrontMenuWide(bool wide) {
    g_menuWidePickedAt.store(GetTickCount64());
    if (g.fMenuWide.exchange(wide) == wide) return;
    LogF("vrcam: frontend - the main menu %s ([frontend] menu_16x9=%d, the icon over its panel)", wide ? "16:9" : "square", int(wide));
    std::thread([wide] {   // key by key in place, as D2R VR Settings writes it
        const bool ok = WritePrivateProfileStringW(L"frontend", L"menu_16x9", wide ? L"1" : L"0", g_iniPath) != 0;
        if (!ok) LogF("vrcam: frontend - [frontend] menu_16x9 NOT written (error %lu)", GetLastError());
    }).detach();
}
bool MenusKeepSize() { return g.frontOn.load() && g_scene.load() && g_set.platform.load() == 1 && xr::On(); }
uint32_t FaceGen() { return g_faceGen.load(); }
int FrontAct() { return Frontend() ? g_act.load() : 0; }
bool FrontActReady() { const int a = FrontAct(); return a >= 1 && a <= 5 && (g.fActs.load() >> a & 1u) != 0; }
bool SkyWanted() { return Frontend() ? g.fSky.load() && FrontActReady() : g_now.load() && g.sky.load(); }
const char* SkyPart() {
    static const char* const kFront[6] = {"act1", "act1", "act2", "act3", "act4", "act5"};
    return Frontend() ? kFront[std::clamp(g_act.load(), 0, 5)] : "expansion";
}
FrontSky FrontSkyNow() {
    FrontSky f;
    f.on = Frontend() && SkyWanted();
    if (!f.on) return f;
    const int n = std::clamp(g_act.load(), 1, 5);
    f.bright = g.fSkyBright[n].load();
    f.saturation = g.fSkySat[n].load();
    f.blend = g.fBlend.load(); f.blendFar = g.fBlendFar.load(); f.blendLo = g.fBlendLo.load();
    f.blendHi = std::max(g.fBlendHi.load(), f.blendLo + 0.001f);
    f.edges = g.fEdges.load();
    // the walls: Act 1's cut_right / cut_left and its far part; the others' cut_right_N / cut_left_N (Unleashed's: halfway to the next
    // act's camera on each side, none past the ends)
    const float cr = g.fCutRight[n].load(), cl = g.fCutLeft[n].load();
    f.cut = cr > 0.0f || cl > 0.0f;
    f.cutDir[0] = g_frontRight[0].load(); f.cutDir[1] = g_frontRight[1].load();
    f.cutRight = cr > 0.0f ? cr : 1e9f;
    f.cutLeft = cl > 0.0f ? cl : 1e9f;
    f.voidRight = cr > 0.0f ? g.fCutVoid.load() : 2.0f;   // with no depth (a painted sky card), by its direction alone
    f.voidLeft = cl > 0.0f ? g.fCutVoid.load() : 2.0f;
    const float fr = n == 1 ? g.fCutFarRight.load() : 0.0f;
    f.farRight = fr > 0.0f ? fr : 1e9f;
    f.farAhead = fr > 0.0f ? g.fCutFarAhead.load() : 1e9f;
    return f;
}
Boards BoardsNow() {
    Boards b;
    const bool on = g_now.load() && g_shown.load();
    const bool front = g_kind.load() == kFront;
    b.menuBoard = on && g.menuBoard.load();
    // (the main menu: no caption board - the hero's name is in the menu's list; board_as_menus: the whole band is on the board, the words with it)
    b.caption = b.menuBoard && !front && g.caption.load() && !g.boardAsMenus.load();
    b.boardClear = g.boardClear.load();
    if (front) { b.menuTop = g.fMenuTop.load(); b.menuWidth = g.fMenuWidth.load(); b.menuDistance = g.fMenuDistance.load(); b.menuDrop = g.fMenuDrop.load(); }
    else { b.menuTop = g.menuTop.load(); b.menuWidth = g.menuWidth.load(); b.menuDistance = g.menuDistance.load(); b.menuDrop = g.menuDrop.load();
           b.asMenus = g.boardAsMenus.load(); }
    for (int i = 0; i < 4; ++i) b.captionRect[i] = g.captionRect[i].load();
    b.captionWidth = g.captionWidth.load(); b.captionDistance = g.captionDistance.load(); b.captionRise = g.captionRise.load();
    // the beam's length off the board: to the pick's point by the fire; the main menu has no pick in the scene - a little past the board
    b.pointerM = front ? b.menuDistance + 1.0f : g.pickDist.load() * UserEyeHeightM() / std::max(1.0f, g.eye.load());
    return b;
}

}  // namespace campfire
