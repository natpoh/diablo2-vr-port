// walk_engine_stub.cpp - a stand-in for BodyWalk's d2r_vr_walk.dll with the same interface (shared/walk_engine_abi.h), for the tests only:
// never shipped. Its "detector" is a plain made-up one - the headset's height swinging up and down over the last second - just enough for the
// loader, the wiring and the speed -> stick shaping to run end to end on synthetic headset tracks. BodyWalk's real engine is closed and not here.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <new>
#include <string>

#include "walk_engine_abi.h"

namespace {

struct Engine {
    BWWE_LogFn log = nullptr;
    std::string gameId;
    HANDLE mutex = nullptr;
    BWWE_Tuning tuning{sizeof(BWWE_Tuning), 0.5f, 0.5f, 0.5f};
    std::deque<std::pair<double, float>> heights;   // (time, head height) over the last 0.6 s
    float speed = 0.0f;
    double lastT = -1.0;
    uint32_t steps = 0;
    bool falling = false;
    float lastY = 0.0f;
    double lastStepT = -10.0;
    float cadence = 0.0f;
    uint32_t tuningCalls = 0, resets = 0;
};

void Say(Engine* e, const char* s) {
    if (e && e->log) e->log(s);
}

}  // namespace

extern "C" {

__declspec(dllexport) uint32_t __cdecl BWWE_Abi(void) { return BWWE_ABI; }

__declspec(dllexport) void* __cdecl BWWE_Open(const BWWE_Init* init) {
    if (!init || init->size < sizeof(BWWE_Init) || init->abi != BWWE_ABI) return nullptr;
    Engine* e = new (std::nothrow) Engine;
    if (!e) return nullptr;
    e->log = init->log;
    e->gameId = init->gameId ? init->gameId : "d2r";
    const std::string name = "Local\\BodyWalk.WalkEngine." + e->gameId;
    e->mutex = CreateMutexA(nullptr, FALSE, name.c_str());
    Say(e, "stub walk engine open (a test's stand-in, not BodyWalk's)");
    return e;
}

__declspec(dllexport) void __cdecl BWWE_Close(void* engine) {
    Engine* e = static_cast<Engine*>(engine);
    if (!e) return;
    if (e->mutex) CloseHandle(e->mutex);
    delete e;
}

__declspec(dllexport) void __cdecl BWWE_SetTuning(void* engine, const BWWE_Tuning* t) {
    Engine* e = static_cast<Engine*>(engine);
    if (!e || !t || t->size < sizeof(BWWE_Tuning)) return;
    e->tuning = *t;
    ++e->tuningCalls;
}

__declspec(dllexport) void __cdecl BWWE_Reset(void* engine) {
    Engine* e = static_cast<Engine*>(engine);
    if (!e) return;
    e->heights.clear();
    e->speed = 0.0f;
    e->lastT = -1.0;
    e->falling = false;
    e->cadence = 0.0f;
    ++e->resets;
}

__declspec(dllexport) void __cdecl BWWE_Feed(void* engine, const BWWE_Frame* f, BWWE_State* out) {
    Engine* e = static_cast<Engine*>(engine);
    if (!out) return;
    const uint32_t outSize = out->size;
    *out = BWWE_State{};
    out->size = outSize;
    if (!e || !f || f->size < sizeof(BWWE_Frame)) return;
    if (f->head.valid) {
        const double t = f->timeS;
        const float y = f->head.pos[1];
        e->heights.emplace_back(t, y);
        while (!e->heights.empty() && t - e->heights.front().first > 0.6) e->heights.pop_front();
        // a step: the height turning from falling to rising (a low point), at most 4 a second
        if (e->lastT >= 0.0) {
            const bool fallingNow = y < e->lastY;
            if (e->falling && !fallingNow && t - e->lastStepT > 0.25) {
                float lo = y, hi = y;
                for (const auto& h : e->heights) lo = std::min(lo, h.second), hi = std::max(hi, h.second);
                const float least = 0.012f * std::pow(4.0f, 1.0f - 2.0f * e->tuning.sensitivity);   // 3 mm .. 4.8 cm swing counts
                if (hi - lo > least) {
                    if (t - e->lastStepT < 2.0) e->cadence = (float)(1.0 / (t - e->lastStepT));
                    e->lastStepT = t;
                    ++e->steps;
                    out->flags |= BWWE_STATE_STEP;
                }
            }
            e->falling = fallingNow;
        }
        float lo = y, hi = y;
        for (const auto& h : e->heights) lo = std::min(lo, h.second), hi = std::max(hi, h.second);
        const float least = 0.012f * std::pow(4.0f, 1.0f - 2.0f * e->tuning.sensitivity);
        const float full = 0.06f / std::pow(4.0f, 2.0f * e->tuning.speed - 1.0f);   // the swing that is full speed
        float want = t - e->lastStepT < 0.6 && hi - lo > least ? std::clamp((hi - lo - least) / std::max(full - least, 1e-3f), 0.0f, 1.0f) : 0.0f;
        const double dt = e->lastT >= 0.0 ? std::clamp(t - e->lastT, 0.0, 0.1) : 0.0;
        const double tau = 0.05 + 0.5 * e->tuning.smoothing;   // seconds
        e->speed += (float)((want - e->speed) * (dt / (tau + dt)));
        if (want == 0.0f && e->speed < 0.05f) e->speed = 0.0f;
        e->lastT = t;
        e->lastY = y;
        if (t - e->lastStepT > 2.0) e->cadence = 0.0f;
    }
    out->speed = (f->flags & BWWE_FRAME_PLAY) ? e->speed : 0.0f;
    if (e->speed > 0.02f) out->flags |= BWWE_STATE_WALKING;
    out->cadenceHz = e->cadence;
    out->steps = e->steps;
    out->profileRevision = 0;
}

// (the test's own peeks, not of the interface)
__declspec(dllexport) uint32_t __cdecl WalkStub_TuningCalls(void* engine) { return engine ? static_cast<Engine*>(engine)->tuningCalls : 0; }
__declspec(dllexport) uint32_t __cdecl WalkStub_Resets(void* engine) { return engine ? static_cast<Engine*>(engine)->resets : 0; }

}  // extern "C"
