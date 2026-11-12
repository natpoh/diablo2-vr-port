// apply_keep_size.h - [render] apply_keep_size: the game's Apply (Options > Video) does not resize its swap chain in the middle of its frame
// while a render size of ours is in force (crash group E2: DXGI_ERROR_DEVICE_HUNG a few seconds after AcceptChanges).
//
// What went wrong (crash reports 338, 340, 341, 342 - 2026-11-12; 0.212.3 and 0.214.0, RTX 5090 / RTX 3060, SteamVR, Native two passes, the
// replay OFF; the older group E, 229 / 267 / 273, with it on): Options > Video > Accept in VR. In the same millisecond as AcceptChanges the game
// resized its swap chain to its WINDOW's size (2048x1152 -> 1400x1400: the window is the 1400 square, the picture our render size - here
// [render] menu_wide's 16:9; 338: 3708x3732 -> 2064x2064) on the thread that presents, and the GPU then stopped in "Post Task" (the frame's
// last list, drawn into the back buffer) with a page fault where a "Swap Chain" was freed. DXGI's ResizeBuffers waits for every list already
// on the swap chain's queue (tools/resize_drain_test.cpp A1 / A1b: 300 ms behind a gate, ResizeBuffers and ResizeBuffers1, plain runtime,
// RTX 4070 Ti SUPER) - so that Post Task went on the queue AFTER the resize: a list of the frame the game was in, recorded with the old back
// buffer. The game resizes from inside its frame at an Apply; in flat its window is its picture, the Apply has no size to change and nothing
// is freed; with our render size the window and the picture differ, so every Apply in VR frees the back buffers under that frame. No drain
// helps a list submitted after the free (resize_drain still drains: at Apply, at the resize, at a DLSS release - it costs a wait only).
//
// The cure (on by default; unticked: as before): the game's ResizeBuffers / ResizeBuffers1 within kWindowMs of its AcceptChanges (the first
// one after it only), while a render size is in force and the buffers already have that size, the same format, count and flags - is left
// out: S_OK, nothing freed, the same buffers (GetBuffer gives them again). That resize would only have undone our size: the render size is
// asked for again kApplyMs after the Apply anyway (vr_resolution.h OptionsApplied) and the game then resizes from its message loop, between
// frames, as every render-size resize before it. Meanwhile (half a second) the game draws its own size into the same back buffer: the
// picture may be cut off at the edge for that moment, nothing else. A resize that changes the format, the count or the flags, one with no
// render size in force, or one later than kWindowMs: as before.
//
// Pure (tools/resize_drain_test.cpp).
#pragma once

#include <atomic>
#include <cstdint>

namespace applykeep {

inline constexpr uint64_t kWindowMs = 250;   // AcceptChanges -> the resize came in the same millisecond in every report (338: 7 ms)

// The swap chain's buffers now, and what a ResizeBuffers asks (0 = keep, as DXGI reads it).
struct Chain {
    uint32_t w = 0, h = 0, format = 0, count = 0, flags = 0;
};

enum class Why {
    kKeep = 0,        // left out: the buffers stay
    kOff,             // the tick off
    kNoApply,         // no AcceptChanges before it (or its one resize already judged)
    kLate,            // the Apply longer than kWindowMs ago
    kNoRenderSize,    // no render size in force: the window is the picture (as in flat)
    kOtherSize,       // the buffers are not the render size now (the game had not taken it): nothing to keep
    kFormat,          // a new format asked (an HDR switch)
    kCount,           // another number of buffers asked
    kFlags,           // other swap chain flags asked
};
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::kKeep: return "left out - the buffers already have the render size";
        case Why::kOff: return "done ([render] apply_keep_size is off)";
        case Why::kNoApply: return "done (not right after an Apply)";
        case Why::kLate: return "done (the Apply was longer ago than 250 ms)";
        case Why::kNoRenderSize: return "done (no render size in force: the window is the picture)";
        case Why::kOtherSize: return "done (the buffers are not the render size now)";
        case Why::kFormat: return "done (a new format asked)";
        case Why::kCount: return "done (another number of buffers asked)";
        case Why::kFlags: return "done (other swap chain flags asked)";
        default: return "?";
    }
}

// The verdict for one ResizeBuffers. `applyPending`: an AcceptChanges came and no resize has been judged since; `msSinceApply` since it;
// `forceW` x `forceH` the render size in force (0 = none); `now` the buffers, `ask` the call's (w/h not looked at: the game asks its window's).
inline Why Judge(bool on, bool applyPending, uint64_t msSinceApply, uint32_t forceW, uint32_t forceH, const Chain& now, const Chain& ask) {
    if (!on) return Why::kOff;
    if (!applyPending) return Why::kNoApply;
    if (msSinceApply > kWindowMs) return Why::kLate;
    if (!forceW || !forceH) return Why::kNoRenderSize;
    if (now.w != forceW || now.h != forceH) return Why::kOtherSize;
    if (ask.format && ask.format != now.format) return Why::kFormat;
    if (ask.count && ask.count != now.count) return Why::kCount;
    if (ask.flags != now.flags) return Why::kFlags;
    return Why::kKeep;
}

// The Apply's time (gamestate.cpp at AcceptChanges, any thread) and the one resize it may keep: taken by the first resize after it.
class Gate {
public:
    void Applied(uint64_t nowMs) { at_.store(nowMs ? nowMs : 1); }
    // true (and the Apply's time) for the first resize after an Apply; false after that until the next Apply.
    bool Take(uint64_t* atMs) {
        const uint64_t a = at_.exchange(0);
        if (atMs) *atMs = a;
        return a != 0;
    }
    std::atomic<bool> on{true};   // [render] apply_keep_size

private:
    std::atomic<uint64_t> at_{0};
};
inline Gate g_gate;

}  // namespace applykeep
