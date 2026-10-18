// room_walk_wire.cpp - room-scale walking ([openxr] roomscale_walk) on the plugin: see room_walk.h for the model and why.
//
// Only the game's own walk: the stick goes into the pad the game polls (vrcam's PadMirrorRead - the plugin's own pad in direct mode, or
// BodyWalk's report), where the controller's left stick goes; the game's walking code, its dead zone, its run and its stop do the rest.
// Nothing here moves the hero or sets a position. The camera: the native head view's eyes are moved by the walker's point (the hero's
// own motion while the room walk drives him is taken off, so the view stays with the head). F4 only (vr/view_gate.h), native OpenXR only
// (FlatVR's camera has no head place to walk by).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "room_walk.h"
#include "room_walk_wire.h"
#include "view_gate.h"
#include "xr.h"

namespace roomwalkwire {
namespace {

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_on{false};
std::atomic<float> g_deadM{0.12f}, g_capM{0.6f};
std::atomic<bool> g_debug{false};
// The stick of the last step: x, y (16 bits each) and the low 32 bits of GetTickCount64 when it was made.
std::atomic<uint64_t> g_stick{0};

// The render thread's own.
roomwalk::Walker g_walker;
float g_heroPrev[3] = {};
bool g_heroHave = false;
ULONGLONG g_debugAt = 0;
int g_told = -1;   // what the last "room walk - ..." line said (an index of Why below)

void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[640];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

double NowS() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

void Publish(float x, float y) {
    const uint16_t lx = (uint16_t)roomwalk::Axis(x), ly = (uint16_t)roomwalk::Axis(y);
    g_stick.store((uint64_t)lx | ((uint64_t)ly << 16) | ((uint64_t)(uint32_t)GetTickCount64() << 32), std::memory_order_release);
}

// Why the room walk does or does not act, said once per change.
enum Why { kOff, kNotF4, kNoGame, kMenu, kChat, kStick, kLive };
const char* WhyName(int w) {
    switch (w) {
    case kOff: return "off";
    case kNotF4: return "stands down - not VR first person with native OpenXR (F4 only; FlatVR's camera has no head place to walk by)";
    case kNoGame: return "stands down - no game running";
    case kMenu: return "holds - a panel or menu is open (the stick is the menu's; your place is kept)";
    case kChat: return "holds - the chat is open";
    case kStick: return "holds - the controller's stick walks (it wins; the camera goes with the hero)";
    case kLive: return "live - your steps in the room walk the hero";
    }
    return "?";
}
void Tell(int w) {
    if (w == g_told) return;
    g_told = w;
    LogF("vrcam: room walk - %s", WhyName(w));
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void LoadSettings(const wchar_t* ini) {
    auto f = [&](const wchar_t* sec, const wchar_t* key, float def) {
        wchar_t b[64], d[64];
        swprintf_s(d, L"%g", def);
        GetPrivateProfileStringW(sec, key, d, b, 64, ini);
        wchar_t* end = nullptr;
        const float v = wcstof(b, &end);
        return end != b && std::isfinite(v) ? v : def;
    };
    const bool on = GetPrivateProfileIntW(L"openxr", L"roomscale_walk", 0, ini) != 0;
    const float dead = std::clamp(f(L"openxr", L"roomscale_dead_zone_m", 0.12f), 0.03f, 0.5f);
    const float cap = std::clamp(f(L"openxr", L"roomscale_cap_m", 0.6f), 0.2f, 2.0f);
    const bool debug = GetPrivateProfileIntW(L"debug", L"roomscale_log", 0, ini) != 0;
    const bool was = g_on.exchange(on);
    const float wasDead = g_deadM.exchange(dead), wasCap = g_capM.exchange(cap);
    const bool wasDebug = g_debug.exchange(debug);
    static bool told = false;
    if (!told || was != on || wasDead != dead || wasCap != cap || wasDebug != debug) {
        told = true;
        if (on)
            LogF("vrcam: room walk - [openxr] roomscale_walk=1: in VR first person (native OpenXR) your steps in the room walk the hero through the game's own "
                 "walking stick (dead zone %.2f m, the room slides past %.2f m off the hero; the controller's stick wins while it is pushed)%s",
                 dead, cap, debug ? "; [debug] roomscale_log=1: a line a second while it is live" : "");
        else
            LogF("vrcam: room walk - [openxr] roomscale_walk=0: walking in the room does not move the hero (the body follows the head past lean_m, as before)");
    }
}

bool On() { return g_on.load(std::memory_order_relaxed); }

bool View(const xr::Eyes& xe, const float hero[3], float cb, float sb, float upm, const Facts& f, float* eyesX, float* eyesZ) {
    roomwalk::Config cfg;
    cfg.on = g_on.load(std::memory_order_relaxed);
    cfg.deadM = g_deadM.load(std::memory_order_relaxed);
    cfg.capM = g_capM.load(std::memory_order_relaxed);
    roomwalk::Input in;
    in.t = NowS();
    in.engaged = cfg.on && f.firstPerson && f.inWorld && viewgate::F4();
    in.canWalk = in.engaged && !f.menuOpen && !f.chatOpen;
    in.stickHeld = f.stickHeld;
    in.ownTilt = f.ownTilt;
    in.minTilt = f.hookWalk ? roomwalk::kMinTiltHook : roomwalk::kMinTilt;
    in.epoch = xe.epoch;
    for (int k = 0; k < 3; ++k) in.eyes[k] = 0.5f * (xe.pos[0][k] + xe.pos[1][k]);
    for (int k = 0; k < 4; ++k) in.quat[k] = xe.quat[0][k];
    if (in.engaged && g_heroHave)
        in.heroMove = roomwalk::WorldToRoom(hero[0] - g_heroPrev[0], hero[2] - g_heroPrev[2], cb, sb, upm);
    for (int k = 0; k < 3; ++k) g_heroPrev[k] = hero[k];
    g_heroHave = in.engaged;
    const roomwalk::Output o = g_walker.Step(cfg, in);
    Publish(o.stickX, o.stickY);
    if (cfg.on) Tell(!in.engaged ? (f.inWorld ? kNotF4 : kNoGame) : f.menuOpen ? kMenu : f.chatOpen ? kChat : o.stickHeld ? kStick : kLive);
    else if (g_told != -1) Tell(kOff);
    if (!o.engaged) return false;
    xr::HoldBody();
    *eyesX -= o.camX;
    *eyesZ -= o.camZ;
    if (o.fresh) LogF("vrcam: room walk - the hero is put under your head (a start, a recentre or a gap in the frames)");
    if (g_debug.load(std::memory_order_relaxed)) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_debugAt >= 1000) {
            g_debugAt = now;
            LogF("vrcam: room walk - offset %.2f m (x %+.2f z %+.2f, stop %.2f), stick %+.2f %+.2f (tilt %.2f, asked %.2f%s), walking %s, blocked %s%s, "
                 "head %.2f m/s, hero %.2f m/s (a full stick: %.2f m/s, learned), controller stick %s (tilt %.2f), pad: %s, least tilt %.2f (%s)",
                 o.offM, o.off.x, o.off.z, o.stopM, o.stickX, o.stickY, o.tilt, o.need, o.slow ? " - under the game's slowest walk: walk / wait" : "",
                 o.walking ? "yes" : "no", o.blocked ? "YES" : "no", o.capped ? " (at the cap: the room slides)" : "", o.headMps, o.heroMps, o.fullMps,
                 o.stickHeld ? "held" : "free", f.ownTilt, f.pad, o.minTilt,
                 f.hookWalk ? "the walking-stick hook: under the pad's dead zone" : "the pad: its dead zone - [input] direct_walk=1 walks slower and steadier");
        }
    }
    return true;
}

bool IsRoomStick(int16_t lx, int16_t ly) {
    int16_t x = 0, y = 0;
    return (lx != 0 || ly != 0) && Stick(&x, &y) && x == lx && y == ly;
}

bool Stick(int16_t* lx, int16_t* ly) {
    const uint64_t w = g_stick.load(std::memory_order_acquire);
    const uint32_t at = (uint32_t)(w >> 32);
    if ((uint32_t)GetTickCount64() - at > 150) return false;
    const int16_t x = (int16_t)(uint16_t)(w & 0xFFFF), y = (int16_t)(uint16_t)((w >> 16) & 0xFFFF);
    if (x == 0 && y == 0) return false;
    *lx = x; *ly = y;
    return true;
}

}  // namespace roomwalkwire
