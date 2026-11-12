// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed PR #518 (merge 1ee027b; f57f881 .. 696fb19), vr/vrcam.cpp's campfire: Settings' campfire* and their ini
// (LoadSettings), CampfireView, CampfirePointer, CampfireFireHit, CampfirePick, CampfireTick and the biome test in SkyTick. Changed for
// D2R VR Authentic: the eyes come from our native OpenXR pair (xr::OpenCampfireFrame / PairEyes, one frame for both eyes, each eye its
// own pose) instead of framevr::View; the hand's ray from xr::CampfireAim laid into the scene the same way instead of HandRay; the scene
// is not counted as a world (g_inWorld stays false - ours keys far more on it), only as stereo; a failure gives the flat screen back.
// See vr/campfire_unleashed.h. MPL 2.0.
#include "vrcam_headers.h"
#include "campfire_unleashed.h"
#include "campfire_audio_unleashed.h"

using namespace D2RL;
using namespace vrc;

namespace vrc { bool EyesSane(const xr::Eyes& e); }   // (vr/view/camera.cpp)

namespace campfire {
namespace {

struct Cfg {
    std::atomic<bool>  on{false};          // [campfire] enabled: character creation's campfire scene in stereo, round you
    std::atomic<float> eye{6.0f};          // eye: the eye height the scene's scale is taken from, world units (~0.24 m each)
    std::atomic<float> up{2.25f};          // up: the eyes above the game camera, world units
    std::atomic<float> forward{3.0f};      // forward: the eyes ahead of the game camera, toward the fire, world units
    std::atomic<float> pickDist{22.0f};    // pick_distance: how far along the hand's ray the pointer is put, world units
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
    std::atomic<bool>  sky{false};         // sky: our sky over the scene's void - D2R VR Authentic: off by default (Unleashed: 1)
    std::atomic<bool>  boardClear{true};   // board_clear: the flat interface emptied once the board has its copy
    std::atomic<bool>  menuBoard{true};    // menu_board: the menu's lower strip on a board standing in the scene
    std::atomic<float> menuWidth{1.1f}, menuDistance{1.0f}, menuDrop{0.9f};   // menu_width, menu_distance, menu_drop (metres)
    std::atomic<float> menuTop{0.78f};     // menu_top: where the strip begins, a share of the game's menu band from its top
    std::atomic<bool>  caption{true};      // caption: the picked hero's class and words on a board over the heroes
    std::atomic<float> captionRect[4]{{0.25f}, {0.0f}, {0.75f}, {0.33f}};   // caption_left/top/right/bottom: shares of the menu band
    std::atomic<float> captionWidth{3.0f}, captionDistance{6.5f}, captionRise{2.0f};   // caption_width/distance/rise (metres)
    std::atomic<bool>  pano{false};        // panorama: one picture round the scene, in the void (needs sky)
    std::atomic<float> panoYaw{0.0f}, panoLow{-90.0f}, panoHigh{90.0f}, panoBright{1.0f};
    std::atomic<bool>  fireSound{true}, sparks{true};   // fire_sound, sparks
    std::atomic<float> fireVolume{0.9f}, fireBurst{2.0f};   // fire_volume, fire_burst_volume
    std::atomic<float> fireX{1060.67f}, fireY{0.3f}, fireZ{123.94f}, firePick{2.5f};   // fire_x/y/z, fire_pick_radius
    float avoid[4] = {0, 0, 0, 0};          // pick_avoid = x0,y0,x1,y1 (0..1 of the window)
};
Cfg g;

std::atomic<bool> g_scene{false};      // the creation scene is up (the area's name)
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
struct Lay { float base = 0.0f, p0x = 0.0f, p0z = 0.0f, upm = 1.0f; V3 at{}; bool ok = false; };
Lay g_lay;
xr::Eyes g_eyes{};   // the frame's eyes as View took them (Proj reads them)
bool g_eyesOk = false;

void Fail(const char* why) {
    if (g_failed.exchange(true)) return;
    LogF("vrcam: campfire - %s: the flat character screen for this visit ([campfire] enabled=1 tries again next time)", why);
}

V3 ToWorld(const Lay& l, V3 v) {
    const float cb = cosf(l.base), sb = sinf(l.base);
    return {v.x * cb + v.z * sb, v.y, -v.x * sb + v.z * cb};
}
V3 RoomPoint(const Lay& l, const float p[3]) {   // a point of PairEyes' head frame (metres) in the scene (world units)
    return l.at + ToWorld(l, V3{p[0] - l.p0x, p[1], p[2] - l.p0z}) * l.upm;
}

void Recompute() {
    const bool want = g.on.load() && g_scene.load() && !g_failed.load() && g_set.platform.load() == 1 && xr::On() && xr::Running() && g_h.camera;
    const bool was = g_now.exchange(want);
    if (want != was) {
        if (want) { g_arrived.store(true); g_shown.store(false); g_sceneAt.store(GetTickCount64()); g_lastViewAt.store(0); }
        else g_shown.store(false);
        xr::SetCampfire(want);
        LogF("vrcam: campfire - %s", want ? "the creation scene in stereo, round you" : "the scene left (or off): the flat menus");
    }
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
    const float t = g.pickDist.load();
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
    RECT rc{};
    if (!GetClientRect(g_gameWnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    // The scene's picture (the viewport) fitted whole in the window, bars beside or above it - how the game reads its pointer back.
    float x0 = 0.0f, y0 = 0.0f, pw = float(rc.right), ph = float(rc.bottom);
    if (g.pointerFit.load() && canvas > 0.0f) {
        const float scene = canvas, window = float(rc.right) / float(rc.bottom);
        if (window > scene) { pw = ph * scene; x0 = 0.5f * (float(rc.right) - pw); }
        else                { ph = pw / scene; y0 = 0.5f * (float(rc.bottom) - ph); }
    }
    POINT p{LONG(x0 + (nx * 0.5f + 0.5f + g.pointerX.load()) * pw), LONG(y0 + (0.5f - ny * 0.5f - g.pointerY.load()) * ph)};
    p.x = std::clamp<LONG>(p.x, 0, rc.right - 1);
    p.y = std::clamp<LONG>(p.y, 0, rc.bottom - 1);
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
    g.effects.store(IniB(L"campfire", L"effects", false));
    g.night.store(IniB(L"campfire", L"night", false));
    g.skyCam.store(IniB(L"campfire", L"sky_cam", false));
    g.sky.store(IniB(L"campfire", L"sky", false));
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
    Recompute();
}

void OnBiome(const std::string& biome) {
    const bool scene = biome.rfind("ui_characterselect", 0) == 0;
    if (scene != g_scene.exchange(scene)) {
        if (!scene) g_failed.store(false);   // left: the next visit tries again
        else if (g.on.load()) LogF("vrcam: campfire - '%s' up", biome.c_str());
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
    const float eyeH = g.eye.load();
    const V3 at = cam + ahead * g.forward.load() + V3{0.0f, g.up.load(), 0.0f};
    AcquireSRWLockExclusive(&g_lock);
    Lay& l = g_lay;
    l.at = at;
    l.upm = eyeH / std::max(0.5f, UserEyeHeightM());   // world units per metre: the scene's scale from the eye height
    // Facing the fire on arrival, whichever way the head was turned; taken once on each arrival, from the head as it is then; it also puts
    // the eyes' origin at the head, so you stand where the game's camera stood.
    if (g_arrived.exchange(false) || !l.ok) {
        const V3 hf = d2rcam::m4::Rotate(xe.quat[0], {0.0f, 0.0f, -1.0f});
        const float roomYaw = atan2f(-hf.x, -hf.z);
        l.base = heading0 - roomYaw;
        l.p0x = 0.5f * (xe.pos[0][0] + xe.pos[1][0]);
        l.p0z = 0.5f * (xe.pos[0][2] + xe.pos[1][2]);
        l.ok = true;
        g_shown.store(true);
        LogF("vrcam: campfire - facing the fire (the head was turned %.0f deg in the room), %.2f units a metre", roomYaw * 57.2957795f, l.upm);
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
    const bool on = g_now.load();
    float o[3] = {}, d[3] = {};
    bool held = false;
    const bool ray = on && HandRay(o, d, &held);
    held = held && on && !xr::CampfireBoardHit();   // the laser on the menu's board: the board clicks (vr/xr.cpp)
    // Pointing is enough for the game to show which hero is under the pointer (its own hover).
    static bool toldPointer = false;
    POINT where{};
    if (ray && !xr::CampfireBoardHit() && Pointer(o, d, &where)) {
        SetCursorPos(where.x, where.y);
        if (!toldPointer) { toldPointer = true; LogF("vrcam: campfire pick - the pointer follows the right hand (%ld, %ld)", where.x, where.y); }
    }
    // A click on the fire (not a hero): sparks and a crackle.
    if (held && !down && !fireDown && ray && FireHit(o, d)) {
        fireDown = true;
        g_sparkAt.store(GetTickCount64());
        g_bursts.fetch_add(1);
        Log("vrcam: campfire - the fire clicked: sparks and a crackle");
    }
    if (!held) fireDown = false;
    if (fireDown) return;   // the click was the fire's: not the game's
    if (held && ray) {
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
    const bool on = g_now.load();
    if (on) {
        const ULONGLONG now = GetTickCount64(), since = g_sceneAt.load(), last = g_lastViewAt.load();
        if (xr::CampfireNoFrame() > 240) Fail("no headset frame for the scene in 240 presents");
        else if (!last && since && now - since > 8000) Fail("no view of the scene built in 8 s (the scene's camera never ticked)");
        else if (last && now - last > 4000) Fail("no view of the scene built for 4 s");
    }
    if (!on) d2rcam::SetActivePickTurn(0.0f);   // Pointer sets it in the scene
    d2rcam::SetUseActiveCamera(on);
    d2rcam::SetMirrorGameCamera(on && g.skyCam.load());
    d2rcam::SetSyncActiveFields(on && g.camFields.load());
}

uint64_t SparkAt() { return g_sparkAt.load(); }
bool SparksOn() { return g_now.load() && g.sparks.load(); }
void FirePos(float out[3]) { out[0] = g.fireX.load(); out[1] = g.fireY.load(); out[2] = g.fireZ.load(); }
bool FireSoundWanted(float* ambient, float* burst, uint32_t* bursts) {
    if (ambient) *ambient = g.fireVolume.load();
    if (burst) *burst = g.fireBurst.load();
    if (bursts) *bursts = g_bursts.load();
    return g_now.load() && g_shown.load() && g.fireSound.load();
}
bool PanelOut() { return g_now.load() && g.menuBoard.load() && !xr::CampfireBoardFailed(); }
bool SkyWanted() { return g_now.load() && g.sky.load(); }
Boards BoardsNow() {
    Boards b;
    const bool on = g_now.load() && g_shown.load();
    b.menuBoard = on && g.menuBoard.load();
    b.caption = b.menuBoard && g.caption.load();
    b.boardClear = g.boardClear.load();
    b.menuTop = g.menuTop.load(); b.menuWidth = g.menuWidth.load(); b.menuDistance = g.menuDistance.load(); b.menuDrop = g.menuDrop.load();
    for (int i = 0; i < 4; ++i) b.captionRect[i] = g.captionRect[i].load();
    b.captionWidth = g.captionWidth.load(); b.captionDistance = g.captionDistance.load(); b.captionRise = g.captionRise.load();
    b.pointerM = g.pickDist.load() * UserEyeHeightM() / std::max(1.0f, g.eye.load());
    return b;
}

}  // namespace campfire
