// [audio] head_follow: the two FMOD hooks (vr/spatial_audio.h says what they do). Found by bearstonem/d2r-pcvr (MIT: vr/spatial_audio.cpp, v0.3.21,
// contributed by ChuckRibbits); the addresses are D2R 3.3.93787's, FMOD Ex linked into D2R.exe.
//
//  - FMOD4SoundEngine::Set3DPosition (0x157D210, the "Attempted to set 3D position on invalid AudioInstance" setter), called from the game's per-frame emitter
//    update for 3D voices only: the screen offset becomes a ground direction (GroundPos).
//  - FMOD Ex SystemI::set3DListenerAttributes (0x1591A00): listener index checked against 4, it only stores floats and sets the listener's moved flags. The game
//    calls it once, early, with all null - that call hands us FMOD's system; a 10 ms thread then points listener 0 along the view's yaw.

#include "spatial_audio.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "sigscan.h"

namespace spatialaudio {
namespace {

constexpr uint64_t RVA_LISTENER = 0x1591A00, RVA_SET_POS = 0x157D210;
// sub rsp, 0x48; mov r11, r8; mov r10, rcx; cmp edx, 4; ja +0x30A; ...
const uint8_t kSigListener[20] = {0x48,0x83,0xEC,0x48,0x4D,0x8B,0xD8,0x4C,0x8B,0xD1,0x83,0xFA,0x04,0x0F,0x87,0x0A,0x03,0x00,0x00,0x48};
// sub rsp, 0x28; mov rax, [rdx]; mov r9, rcx; test rax, rax; je +0x7A; ... (the instance checks)
const uint8_t kSigSetPos[42] = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x02,0x4C,0x8B,0xC9,0x48,0x85,0xC0,0x74,0x7A,0x48,0x8B,0x40,0x08,0x48,0x85,0xC0,0x74,0x0A,0x8B,0x40,
                                0x08,0x85,0xC0,0x0F,0x94,0xC1,0xEB,0x05,0xB9,0x01,0x00,0x00,0x00,0x84,0xC9,0x75};
using ListenerFn = int (*)(void* sys, int listener, const V3* pos, const V3* vel, const V3* fwd, const V3* up);
using SetPosFn = bool (*)(void* engine, void* instance, float x, float y, float z);
ListenerFn OrigListener = nullptr;
SetPosFn OrigSetPos = nullptr;

std::atomic<bool> g_follow{true}, g_turn{true}, g_listenerIn{false}, g_posIn{false};
std::atomic<float> g_fwdSign{-1.0f}, g_depth{2.0f}, g_sentYaw{1000.0f};
std::atomic<void*> g_system{nullptr};
float (*g_viewYaw)() = nullptr;
LogFn g_log = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }

int HookListener(void* sys, int listener, const V3* pos, const V3* vel, const V3* fwd, const V3* up) {
    if (listener == 0 && sys) g_system.store(sys);
    return OrigListener(sys, listener, pos, vel, fwd, up);
}

bool HookSetPos(void* engine, void* instance, float x, float y, float z) {
    if (g_follow.load(std::memory_order_relaxed)) {
        const V3 p = GroundPos(x, y, z, g_fwdSign.load(std::memory_order_relaxed), g_depth.load(std::memory_order_relaxed));
        return OrigSetPos(engine, instance, p.x, p.y, p.z);
    }
    return OrigSetPos(engine, instance, x, y, z);
}

// Listener 0 along the view's yaw (FMOD: +X right, +Y up, +Z ahead), sent when it moved half a degree; off: facing ahead again, as the game left it.
void TurnListener() {
    void* sys = g_system.load();
    if (!sys || !OrigListener) return;
    float yaw = g_follow.load() && g_turn.load() && g_viewYaw ? g_viewYaw() : 0.0f;
    if (!std::isfinite(yaw)) yaw = 0.0f;
    if (std::fabs(std::remainder(yaw - g_sentYaw.load(), 360.0f)) < 0.5f) return;
    const float t = yaw * 0.0174532925f;
    const V3 fwd{std::sin(t), 0.0f, std::cos(t)}, up{0.0f, 1.0f, 0.0f};
    OrigListener(sys, 0, nullptr, nullptr, &fwd, &up);
    g_sentYaw.store(yaw);
}
DWORD WINAPI TurnThread(void*) {
    for (;;) { Sleep(10); TurnListener(); }
}

// Windows' Mono audio (Settings > Accessibility > Audio: HKCU Software\Microsoft\Multimedia\Audio, AccessibilityMonoMixState 1) folds left and right together.
bool MonoAudioOn() {
    DWORD v = 0, size = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\Audio", L"AccessibilityMonoMixState", RRF_RT_REG_DWORD, nullptr, &v, &size) ==
               ERROR_SUCCESS && v == 1;
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }
void SetViewYaw(float (*yaw)()) { g_viewYaw = yaw; }

void Configure(bool headFollow, bool listenerTurn, float forwardSign, float depthScale) {
    const bool was = g_follow.exchange(headFollow);
    g_turn.store(listenerTurn);
    g_fwdSign.store(forwardSign < 0 ? -1.0f : 1.0f);
    g_depth.store(std::isfinite(depthScale) && depthScale > 0.1f && depthScale < 10.0f ? depthScale : 2.0f);
    if (was != headFollow && g_posIn.load())
        Log(headFollow ? "vrcam: [audio] head_follow on - the game's 3D sounds turn with your view"
                       : "vrcam: [audio] head_follow off - the game's sounds as stock (by the flat camera's screen)");
}

void Install(bool inWorld) {
    static bool failed = false, told = false;
    static int tries = 0, worldTicks = 0;
    if (failed || !g_follow.load()) return;
    if (!g_listenerIn.load() || !g_posIn.load()) {
        if (!g_listenerIn.load() && d2rsig::Check(RVA_LISTENER, kSigListener, sizeof kSigListener)) {
            const bool in = d2rsig::Hook(RVA_LISTENER, kSigListener, sizeof kSigListener, (void*)&HookListener, (void**)&OrigListener);
            g_listenerIn.store(in);
            if (in) { if (HANDLE h = CreateThread(nullptr, 0, &TurnThread, nullptr, 0, nullptr)) CloseHandle(h); }
            else failed = true;
        }
        if (!failed && !g_posIn.load() && d2rsig::Check(RVA_SET_POS, kSigSetPos, sizeof kSigSetPos)) {
            const bool in = d2rsig::Hook(RVA_SET_POS, kSigSetPos, sizeof kSigSetPos, (void*)&HookSetPos, (void**)&OrigSetPos);
            g_posIn.store(in);
            failed = !in;
        }
        if (failed) { Log("vrcam: [audio] head_follow - an FMOD hook FAILED to install: the game's sounds stay as stock"); return; }
        if (!g_listenerIn.load() || !g_posIn.load()) {
            if (++tries == 120)   // a minute: FMOD runs from the start, so its code is not encrypted - another build
                Log("vrcam: [audio] head_follow - the game's FMOD code is not where 3.3.93787 has it (another build?): the game's sounds stay as stock");
            if (tries >= 120) failed = true;
            return;
        }
        char b[220];
        snprintf(b, sizeof b, "vrcam: [audio] head_follow - FMOD hooks in: 3D sounds turn with your view (listener_turn %d, forward_sign %.0f, depth_scale %.2f)%s",
                 g_turn.load() ? 1 : 0, g_fwdSign.load(), g_depth.load(),
                 MonoAudioOn() ? " - Windows Mono audio is ON (Settings > Accessibility > Audio): left and right are folded together, turn it off to hear it" : "");
        Log(b);
    }
    if (!told && inWorld && !g_system.load() && ++worldTicks >= 20) {   // 10 s in a world
        told = true;
        Log("vrcam: [audio] head_follow - the game set up its sound before the hook was in: sounds follow the ground, but do not turn with the view until the game is restarted");
    }
}

}  // namespace spatialaudio
