// frame_hitch.h - the line for a long pair (vr/vrcam.cpp HookDrawGameScreen, DrawByTurns).
//
// The log has the pairs' averages every 10 s and xr.cpp's longest xrWaitFrame, which cannot tell a
// headset that went to sleep, a stream that stalled and a pass that was slow from each other: they
// all show as one bad number in a mean. One line when a pair takes longer than 80 ms - at most one
// a second - with where its time went: the game's own frame between the pairs, XR begin (with the
// xrWaitFrame share, the runtime's pacing), the left and right passes, XR end (our layers with the
// waits on the GPU fence and the runtime's images, and xrEndFrame itself).
//
// After bearstonem/d2r-pcvr (MIT), vr/vrcam.cpp HookDrawGameScreen ("Frame hitch: cycle ..."). Ours is
// built from our own pair clocks. Pure (no globals, no game), tested in tools/xr_hardening_test.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace hitch {

constexpr double kLongUs = 80000.0;    // a pair that takes longer is told
constexpr uint64_t kQuietMs = 1000;    // and at most one line in this time

// One pair's time, microseconds.
struct Pair {
    double cycle = 0;    // from the end of the last pair to the end of this one
    double gap = 0;      // the game's own frame between the two (outside the pair)
    double begin = 0;    // XR begin: xrWaitFrame, xrBeginFrame, the views, the actions synced
    double wait = 0;     //   of it, xrWaitFrame alone
    double left = 0;     // the left pass - the one pass with the eyes by turns
    double right = 0;    // the right pass (none by turns)
    double end = 0;      // XR end: our layers built, xrEndFrame
    double submit = 0;   //   of it, xrEndFrame alone
    double fence = 0;    // in the whole pair, waiting for our copies on the GPU (the fence)
    double image = 0;    //   and for the runtime's swapchain images
    bool xr = false;     // native OpenXR: the begin, end and waits are measured
    bool turns = false;  // eyes by turns: one pass a game frame
};

// True when `cycleUs` is a long pair and the last line was told at least kQuietMs before `nowMs`;
// then *lastMs = nowMs. (0 is "never told".) A number that is not one is not long.
inline bool Due(double cycleUs, uint64_t nowMs, uint64_t* lastMs) {
    if (!(cycleUs > kLongUs)) return false;
    if (*lastMs && nowMs - *lastMs < kQuietMs) return false;
    *lastMs = nowMs;
    return true;
}

// Where most of the time sat, in words, for the end of the line.
inline const char* Mostly(const Pair& p) {
    struct Part { double us; const char* what; };
    const Part parts[] = {
        {p.gap, "the game's own frame between the pairs"},
        {p.xr ? p.wait : 0.0, "xrWaitFrame (the runtime held the frame)"},
        {p.xr ? p.begin - p.wait : 0.0, "the rest of XR begin"},
        {p.left, p.turns ? "the pass" : "the left pass"},
        {p.right, "the right pass"},
        {p.xr ? p.submit : 0.0, "xrEndFrame (the runtime's submit)"},
        {p.xr ? p.end - p.submit : 0.0, "our layers at XR end"},
    };
    const Part* best = &parts[0];
    for (const Part& q : parts) if (q.us > best->us) best = &q;
    return best->what;
}

// The line, without a newline. Returns its length (as snprintf does).
inline int Format(char* out, size_t cap, const Pair& p) {
    const double ms = 0.001;
    char passes[64];
    if (p.turns) snprintf(passes, sizeof passes, "pass %.1f", p.left * ms);
    else snprintf(passes, sizeof passes, "left %.1f | right %.1f", p.left * ms, p.right * ms);
    if (!p.xr)   // the game's pair on FlatVR, or no session: no runtime to measure
        return snprintf(out, cap, "vrcam: frame hitch - a %s took %.1f ms: game %.1f | %s | most of it in %s", p.turns ? "pass" : "pair", p.cycle * ms,
                        p.gap * ms, passes, Mostly(p));
    return snprintf(out, cap,
                    "vrcam: frame hitch - a %s took %.1f ms: game %.1f | XR begin %.1f (xrWaitFrame %.1f) | %s | XR end %.1f (layers %.1f, xrEndFrame %.1f) "
                    "| waits in it: GPU fence %.1f, runtime's images %.1f | most of it in %s",
                    p.turns ? "pass" : "pair", p.cycle * ms, p.gap * ms, p.begin * ms, p.wait * ms, passes, p.end * ms, (p.end - p.submit) * ms,
                    p.submit * ms, p.fence * ms, p.image * ms, Mostly(p));
}

}  // namespace hitch
