// fx_build_order.h - which build of D2R_DepthFog.fx the native runtime ([render] no_reshade, native_effects) starts next (2026-10-29).
//
// One build at a time (a worker thread). At a start the sizes built before (cacheDir\sizes.txt) are compiled into the disk cache, so that
// the game switching to its VR size finds its shaders there - but they went FIRST, the size the game is at waiting behind all of them:
// PC1 2026-10-29 17:50, 0.188.4, its first start (a new build, the cache cold): 2496x2688 11.8 s, 1920x1920 12.5 s, 1918x1022 11.0 s, and only
// then 2316x1737, the back buffer's - "native effects: building, sky off" for a minute, a black sky at the Arreat Summit in flat. Now the
// size in use first; the others after it is built (or its build failed).
#pragma once

namespace fxbuildorder {

enum class Job { None, InUse, Warm };

// busy: a build running. inUseWanted: the size in use has no build of its own yet (or an older one). inUseTried: its build was started for
// exactly this size and generation already (and ended without one: failed). warmLeft: a size built before not yet warmed.
inline Job Next(bool busy, bool inUseWanted, bool inUseTried, bool warmLeft) {
    if (busy) return Job::None;
    if (inUseWanted && !inUseTried) return Job::InUse;
    if (warmLeft) return Job::Warm;   // (the size in use built, or its build tried and failed: the cache's turn)
    return Job::None;
}

}  // namespace fxbuildorder
