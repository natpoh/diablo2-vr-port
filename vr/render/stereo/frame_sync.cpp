// vr/render/stereo/frame_sync.cpp - [flatvr] frame_sync: a frame's picture starts on FlatVR's next frame (cut from vr/vrcam.cpp, docs/plan_modules.md M14).
// What it gives: vr/render/stereo/stereo.h. It sees vrcam.cpp's headers.
#include "../../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// [flatvr] frame_sync (docs/flatvr_sync.md): a frame's picture starts on FlatVR's next frame, as native OpenXR's pair starts on
// xrWaitFrame. Without it three clocks ran apart - our pace::Tick, the game's own frame cap, FlatVR's headset - and each picture was
// shown once or twice in no order (FlatVR at 90, the game's pictures 51-69 a second: walking, the hands and every animation jerked,
// only head turns stayed smooth, being placed at their pose). FlatVR hands BodyWalk a head at each of its frames, right after its
// xrWaitFrame, and Game Link marks it as FlatVR's (BW_LINK_TRACK_FLATVR_STAMP): the pair waits for the next such head and is drawn
// with it. A pair that does not fit a frame goes on every other one (an even half rate; flatvrpose::Sync). No FlatVR head: our
// timer (pace::Tick) as before. The draw thread only.
namespace flatsync {
flatvrpose::Sync g_sync;
std::atomic<double> g_tookAt{-1e12};   // pairtime::UsNow when a frame last started on FlatVR's (pace::Tick stands down meanwhile)
bool On() {
    return g_set.flatSync.load() != 0 && g_set.platform.load() == 1 && !xr::On() && !xr::Running() && g_inWorld.load();
}
// Before a frame's picture (a pair, or mono's one): true = it starts on FlatVR's frame.
bool Wait() {
    if (!On()) return false;
    static bool init = (timeBeginPeriod(1), g_sync = flatvrpose::Sync((float)g_set.headsetHz.load()), true);
    (void)init;
    static uint32_t takes = 0, giveUps = 0, skippedBefore = 0;
    static double waitSum = 0.0;
    static ULONGLONG since = GetTickCount64();
    const double t0 = pairtime::UsNow();
    bool took = false;
    for (;;) {
        const D2RVR_Shared* h = link::Head();
        const bool flat = h && h->version == D2RVR_SHARED_VERSION && h->flatVrStamped && h->sampleStampMagic == D2RVR_SAMPLE_STAMP_MAGIC;
        const uint32_t waited = (uint32_t)((pairtime::UsNow() - t0) / 100.0);
        const flatvrpose::Step st = g_sync.Decide(flat ? h->sampleStamp : 0u, flat, D2RVRStampNow(), waited,
                                                  g_set.flatSyncLate.load(), g_set.flatSync.load());
        if (st == flatvrpose::Step::Take) { took = true; break; }
        if (st == flatvrpose::Step::GiveUp) break;
        if (g_sync.DueIn(D2RVRStampNow()) > 25) Sleep(1);   // more than 2.5 ms to the next head: sleep (1 ms timer)
        else YieldProcessor();
    }
    const double t1 = pairtime::UsNow();
    if (took) { ++takes; waitSum += t1 - t0; g_tookAt.store(t1); }
    else ++giveUps;
    if (const ULONGLONG now = GetTickCount64(); now - since >= 10000) {
        const float per = g_sync.periodTicks * 0.1f;
        LogF("vrcam: flatvr - %u frames started on FlatVR's frames (its frame %.2f ms = %.1f Hz; a pair on every %s - a pair takes %.1f ms), "
             "waited %.2f ms a frame, %u heads let go as late, %u without a FlatVR head (our timer then) ([flatvr] frame_sync %d)",
             takes, per, per > 0.0f ? 1000.0f / per : 0.0f, g_sync.interval == 1 ? "frame" : g_sync.interval == 2 ? "2nd frame" : "3rd frame",
             g_sync.pairTicks * 0.1f, takes ? waitSum / takes / 1000.0 : 0.0, g_sync.skipped - skippedBefore, giveUps, g_set.flatSync.load());
        since = now; takes = giveUps = 0; waitSum = 0.0; skippedBefore = g_sync.skipped;
    }
    return took;
}
}  // namespace flatsync

}  // namespace vrc
