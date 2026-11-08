// gesture_play_wire.cpp - see gesture_play_wire.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "gesture_play_wire.h"
#include "melee_touch.h"

namespace gplaywire {
namespace {

std::mutex g_mu;
gplay::Take g_take;            // (under g_mu)
float g_heightM = gplay::kDefaultHeightM;
double g_startS = 0;
uint32_t g_gesture = 0;
std::string g_name;
bool g_on = false;
bool g_haveStartHead = false;
float g_startHead[3] = {0, 0, 0};
std::atomic<bool> g_playing{false};
void (*g_log)(const char*) = nullptr;

double NowS() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[320];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
// (under g_mu)
void EndLocked(const char* why) {
    if (!g_on) return;
    g_on = false;
    g_playing.store(false);
    meleetouch::SetPaused(false);
    LogF("vrcam: skill panel, Gestures - Play of '%s' (gesture %u) %s: the arms are the controllers' again, the touch's attacks and the grab back",
         g_name.c_str(), g_gesture, why);
}

}  // namespace

void SetLog(void (*fn)(const char*)) { g_log = fn; }

void Start(const gplay::Take& take, float heightM, uint32_t gesture, const char* name) {
    std::lock_guard<std::mutex> l(g_mu);
    if (take.s.empty()) return;
    if (g_on) EndLocked("replaced by another Play");
    g_take = take;
    g_heightM = heightM > 0.5f && heightM < 2.6f ? heightM : gplay::kDefaultHeightM;
    g_startS = NowS();
    g_gesture = gesture;
    g_name = name ? name : "";
    g_on = true;
    g_haveStartHead = false;
    g_playing.store(true);
    meleetouch::SetPaused(true);   // (the played arms strike nothing, press nothing)
    LogF("vrcam: skill panel, Gestures - Play of '%s' (gesture %u): %zu samples, %.2f s once on the hero's arms (F4; visual only - no input, no cast, no hit)",
         g_name.c_str(), gesture, take.s.size(), take.DurationS());
}

void Stop(const char* why) {
    std::lock_guard<std::mutex> l(g_mu);
    EndLocked(why ? why : "stopped");
}

bool Playing() { return g_playing.load(); }

void Tick() {
    if (!g_playing.load()) return;
    std::lock_guard<std::mutex> l(g_mu);
    if (g_on && NowS() - g_startS >= g_take.TotalS() + 0.1) EndLocked("done (played once)");
}

bool ArmsNow(float pitchRad, const float ctl[2][3], const float ctlRot[2][4], const float ctlGrip[2], const float* headRoom, gplay::Arms* out) {
    if (!g_playing.load() || !out) return false;
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_on) return false;
    const float since = (float)(NowS() - g_startS);
    if (headRoom) {   // walked away from where Play started: released at once
        if (!g_haveStartHead) {
            for (int k = 0; k < 3; ++k) g_startHead[k] = headRoom[k];
            g_haveStartHead = true;
        } else {
            const float dx = headRoom[0] - g_startHead[0], dz = headRoom[2] - g_startHead[2];
            if (std::sqrt(dx * dx + dz * dz) > gplay::kAwayM) {
                EndLocked("released: the player walked away");
                return false;
            }
        }
    }
    if (!gplay::Frame(g_take, since, g_heightM, pitchRad, ctl, ctlRot, ctlGrip, out)) {
        if (since >= g_take.TotalS()) EndLocked("done (played once)");
        return false;
    }
    return true;
}

}  // namespace gplaywire
