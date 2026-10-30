// vr/render/stereo/pair_pace.cpp - [stereo] pair_fx_sync (pairfx) and [stereo] pace: the effects of a pair fenced, the presents held to the headset's rate (cut from vr/vrcam.cpp, docs/plan_modules.md M14).
// What it gives: vr/render/stereo/stereo.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

namespace fx {

// [stereo] pair_fx_sync (on): in a stereo pair, a present's effects write ReShade's one constant buffer only once the graphics card has
// drawn the effects of the present two before them - the same eye's last (vr/sky_view_eyes.h FxRing; the eyes' cameras in it are
// those of sky_view_eyes.h). A fence on ReShade's queue after each pair's effects (at reshade_finish_effects, flushed first: they are
// still in its list), waited for at the begin-effects two later, before the uniforms go in. 0: no wait, as before.
namespace pairfx {
ID3D12Fence* g_fence = nullptr;
HANDLE g_event = nullptr;
UINT64 g_signalled = 0;
skyeyes::FxRing g_ring;
uint32_t g_renders = 0, g_waits = 0;   // the 10 s line (OnBeginEffects): effects in pairs, of them waited for the GPU
double g_waitUs = 0.0;
ULONGLONG g_since = 0;
bool Wanted() { return g_set.pairFxSync.load() && PairOfOneFrame(); }
void BeforeWrite() {
    if (!Wanted()) return;
    ++g_renders;
    const UINT64 v = g_ring.WaitBeforeWriting();
    if (v && g_fence && g_fence->GetCompletedValue() < v) {
        const double t0 = pairtime::UsNow();
        g_fence->SetEventOnCompletion(v, g_event);
        WaitForSingleObject(g_event, 100);   // (bounded: a queue that stands is the stall guard's)
        g_waitUs += pairtime::UsNow() - t0;
        ++g_waits;
    }
    const ULONGLONG now = GetTickCount64();
    if (!g_since) g_since = now;
    if (now - g_since >= 10000) {
        LogF("vrcam: pair effects - each eye's effects wrote the constants after the GPU drew that eye's last ([stereo] pair_fx_sync): "
             "%u of %u waited, %.2f ms each (10 s)", g_waits, g_renders, g_waits ? g_waitUs / 1000.0 / g_waits : 0.0);
        g_renders = g_waits = 0;
        g_waitUs = 0.0;
        g_since = now;
    }
}
void AfterRender(reshade::api::effect_runtime* rt) {
    if (!Wanted()) return;
    if (!g_fence) {
        ID3D12Device* d = (ID3D12Device*)rt->get_device()->get_native();
        if (!d || FAILED(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence))) { g_fence = nullptr; return; }
        g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_event) { g_fence->Release(); g_fence = nullptr; return; }
    }
    if (replay::SkipWhileStuck("the pair effects' fence")) return;
    reshade::api::command_queue* q = rt->get_command_queue();
    const bool wasOurs = replay::t_ours;   // (inside the replay's EffectsAtHold it is already ours, and stays so)
    replay::t_ours = true;   // our submission and signal, not the game's (the replay's list and queue hooks)
    q->flush_immediate_command_list();   // the effects just recorded: on the queue before the signal
    ((ID3D12CommandQueue*)q->get_native())->Signal(g_fence, ++g_signalled);
    replay::t_ours = wasOurs;
    g_ring.Rendered(g_signalled);
}
}  // namespace pairfx

// Right before the swap chain's real Present (after ReShade's effects and overlay): pairtime.
// [stereo] pace (on): the presents held to the headset's rate - a game frame per headset frame.
// FlatVR shows the newest picture each headset frame; a game faster than the headset judders
// on head turns (2026-10-09: 90 fps against 90 Hz smooth, 170 fps not), whatever the game's
// own frame cap says (the game rewrites its Settings.json while it runs, so the Settings
// program cannot set it then). Presents a headset frame: eyes by turns and two passes a pair
// two, a replayed pair (one present), mono and the depth one.
namespace pace {
double g_next = 0.0;
uint32_t g_lastReplays = 0;
int g_replayedRecently = 0;   // presents since a replayed pair was seen, capped
void Tick() {
    // (VR only: the headset's rate is not flat mode's - on a monitor the game runs at the player's own cap, nothing of ours holds its presents)
    if (g_set.platform.load() != 1 || !g_set.pace.load() || !g_inWorld.load()) { g_next = 0.0; return; }
    // [stereo] pace_flatvr_only (on): FlatVR not running - its pictures go nowhere, nothing to pace them to (2026-10-18:
    // FlatVR stereo without BodyWalk's FlatVR held at the headset's 120 Hz; Serg: "on FlatVR there is no timing to wait for")
    {
        static bool told = false, was = true;
        const bool flatvr = !g_set.paceFlatVrOnly.load() || xr::FlatVrRunning();
        if (flatvr != was || !told) {
            told = true; was = flatvr;
            LogF("vrcam: present pacing to the headset's %d Hz %s", g_set.headsetHz.load(),
                 flatvr ? "on (FlatVR takes the pictures)" : "off - FlatVR is not running ([stereo] pace_flatvr_only)");
        }
        if (!flatvr) { g_next = 0.0; return; }
    }
    // [flatvr] frame_sync: FlatVR's frames pace this frame (flatsync::Wait) - the timer stands down while they come
    if (pairtime::UsNow() - flatsync::g_tookAt.load() < 100000.0) { g_next = 0.0; return; }
    const uint32_t r = replay::g_replays.load(std::memory_order_relaxed);
    if (r != g_lastReplays) { g_lastReplays = r; g_replayedRecently = 0; }
    else if (g_replayedRecently < 8) ++g_replayedRecently;
    const bool replayed = g_replayedRecently < 4;
    const int perFrame = AfrOn() && !replayed ? 2 : 1;
    const double period = 1e6 / ((double)g_set.headsetHz.load() * perFrame);
    double now = pairtime::UsNow();
    if (g_next <= 0.0 || now - g_next > 2.0 * period) { g_next = now + period; return; }   // behind: no catching up
    if (now < g_next) {
        static bool fine = (timeBeginPeriod(1), true);
        (void)fine;
        while (g_next - now > 1500.0) { Sleep(1); now = pairtime::UsNow(); }
        while (now < g_next) { YieldProcessor(); now = pairtime::UsNow(); }
    }
    g_next += period;
}
}  // namespace pace

}  // namespace fx

}  // namespace vrc
