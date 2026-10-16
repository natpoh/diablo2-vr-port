// depth_pick.h - which depth-stencil our effects copy, picked once and kept (2026-10-16).
//
// vrcam's scenedepth:: counts the depth-stencils the game binds (OnBind) and picks the scene's from them. Counting is a lock and
// a lookup at every depth-stencil bind, so it runs only until a pick is made - after two frames of counts - and again only when
// the pick has to go: the buffer was destroyed, the swap chain was resized, the window's (or the render) size changed, or the
// game did not bind it in a frame. Not at an area change: SetCurrentBiome fires every few seconds while walking (test-38,
// 10:25), the buffer stayed the same each time, and a buffer the game still binds is alive - the fog lost its depth for two
// frames at each for nothing. In between, OnBind only looks for the pick until it has seen it this frame,
// OnBarrier follows that one buffer, and the copy before each eye's effects needs no look round (Serg: "silly to look for the
// map every frame"; Bear's d2r-pcvr keeps one pick too).
//
// Never a copy of a buffer the game did not bind this frame (test-38, 2026-10-16: the render size slider resized the swap chain,
// the game freed its depth's memory before it released the buffer - no destroy event - and the first copy after it crashed in
// the driver, nvwgf2umx.dll). A buffer the game binds in a frame is alive in that frame.
//
// The state machine alone, no ReShade: vrcam drives it, cleanroom/tests/depth_pick_test.cpp proves it.
#pragma once

#include <atomic>
#include <cstdint>

namespace depthpick {

struct Cache {
    std::atomic<uint64_t> pick{0};          // the picked depth-stencil, 0 while counting (read by the bind and barrier hooks)
    std::atomic<bool> seen{false};          // the game bound the pick since the last FrameEnd (the bind hook, any thread)
    std::atomic<bool> gone{false};          // the destroy hook's word: the picked buffer was destroyed (any thread)
    std::atomic<bool> resized{false};       // the swap chain hook's word: it was made again (any thread)
    bool unseen = false;                    // FrameEnd let it go: not bound in the frame just ended
    uint32_t w = 0, h = 0;                  // what the pick was made for: the screen's size
    int counted = 0;                        // frames of counts since the pick was let go
    uint64_t last = 0;                      // the pick before (0: none yet)
    const char* why = "the first pick";     // why the pick now being made is made
    uint32_t picks = 0, others = 0;         // picks made, and of them a buffer other than the one before
    bool sameAsBefore = false;              // the last Commit took the buffer picked before it

    bool Counting() const { return pick.load(std::memory_order_relaxed) == 0; }

    // The bind hook while a pick is held: the buffer the game binds now (0: not known). Cheap once seen this frame.
    bool WantsSight() const { return !Counting() && !seen.load(std::memory_order_relaxed); }
    void Seen(uint64_t res) {
        if (res && res == pick.load(std::memory_order_relaxed)) seen.store(true, std::memory_order_relaxed);
    }

    // The copy before an eye's effects: only of a buffer the game bound this frame.
    bool CopyOk() const { return !Counting() && seen.load(std::memory_order_relaxed); }

    // After each present: one more frame of counts while counting; while held, a frame the game did not bind the pick in lets
    // it go (the reason is told at the next Check).
    void FrameEnd() {
        if (Counting()) {
            if (counted < 100) ++counted;
            return;
        }
        if (!seen.exchange(false)) { pick.store(0); unseen = true; }
    }

    // The destroy hook: a buffer going away.
    void Destroyed(uint64_t res) {
        if (res && res == pick.load(std::memory_order_relaxed)) { pick.store(0); gone.store(true); }
    }
    // The swap chain hook: made again (resized) or going.
    void SwapChainChanged() {
        if (pick.exchange(0)) resized.store(true);
    }

    // Before each copy: lets the pick go when it has to. Returns why (the caller clears its counts), or nullptr.
    const char* Check(uint32_t screenW, uint32_t screenH) {
        const char* reason = nullptr;
        if (gone.exchange(false)) reason = "the picked buffer was destroyed";
        else if (resized.exchange(false)) reason = "the swap chain was made again";
        else if (unseen) reason = "the game did not bind it in a frame";
        else if (!pick.load()) return nullptr;
        else if (screenW != w || screenH != h) reason = "the window's or the render size changed";
        else return nullptr;
        unseen = false;
        pick.store(0);
        seen.store(false);
        counted = 0;
        why = reason;
        return reason;
    }

    // A pick from the counts: only a buffer the game bound this frame, with two frames of counts behind it. True when made;
    // it counts as seen this frame (its bind came while counting).
    bool Commit(uint64_t res, bool boundThisFrame, uint32_t screenW, uint32_t screenH) {
        if (!res || !boundThisFrame || !Counting() || counted < 2) return false;
        w = screenW; h = screenH;
        ++picks;
        if (last && res != last) ++others;
        sameAsBefore = res == last;
        last = res;
        seen.store(true);
        pick.store(res);
        return true;
    }
};

}  // namespace depthpick
