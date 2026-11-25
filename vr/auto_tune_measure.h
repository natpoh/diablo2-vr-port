#pragma once
// auto_tune_measure.h - [autotune] clean_measure: one measuring window of the "Auto" graphics tuning (vr/auto_tune_run.cpp), pure (no Windows,
// no game). tools/auto_tune_test.cpp feeds it synthetic frames, 100 ms holds among them.
//
// 2026-11-25, PC1 on Virtual Desktop (VDXR, Quest 3): Auto chose Potato at 1300 px. Its d2r_vr_autotune.txt said "1792 px -> 1600 px  50.5 -> 10.5
// frames/s  18.59 -> 86.55 ms kept" - a smaller picture five times slower. The windows were full of Virtual Desktop's holds: xrEndFrame alone 60-98 ms
// (twice that at times) while the game's frame was 1 ms and our pass 4-15 ms - the runtime's own timeout, not work - 30-49 such frames in 10 s right
// after each new eye swapchain; and of the render size's own freezes (the swap chain resize ~400 ms, the window's ~300 ms). The old measure was the
// frames counted over 3 s and "busy" = the interval less xrWaitFrame's block (xrEndFrame's hold counted as our own), and a window was thrown away only
// for a gap over 250 ms - a 100 ms or 200 ms hold went through.
//
// Now, per frame: the interval, what xrWaitFrame blocked and what xrEndFrame took. A frame whose interval is an outlier - hitchUs (50 ms) or more AND
// over outlierK (2.5) times the window's median interval (a PC that runs a steady 10 frames/s is slow, not noisy: it gets its verdict) - is
//   held     - when xrWaitFrame or xrEndFrame alone took holdUs (50 ms) or more: the runtime stood, whatever our part was;
//   a hitch  - for any other reason;
// and a window with any held frame, any hitch or a swap chain resize in it (the caller's resize count moved) is not counted (measured again). A clean
// window gives the median interval (frames/s = 1e6 / it: a runtime that locks to half the rate reads half, as it should) and the median of the frame's
// own time - the interval less xrWaitFrame and xrEndFrame - the game's frame and ours (the passes, our layers, our waits on the GPU), never the runtime's.
// The caller gives up after too many unclean windows in a row and says so: no preset picked from noise.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace autotunemeasure {

struct Limits {
    double holdUs = 50000.0;    // xrWaitFrame or xrEndFrame alone this long: the runtime held the frame
    double hitchUs = 50000.0;   // a frame's interval this long: a hitch (a load, a freeze, a resize)
    unsigned minFrames = 15;    // fewer in a window: not counted
    double outlierK = 2.5;      // ... and over this many times the window's median interval: an outlier (held or a hitch)
};

enum class Why { Clean, OutOfView, TooFew, Held, Hitch, Resize };
inline const char* WhyName(Why w) {
    switch (w) {
        case Why::Clean: return "clean";
        case Why::OutOfView: return "out of the world / F4";
        case Why::TooFew: return "too few frames";
        case Why::Held: return "the headset's runtime held frames (xrWaitFrame / xrEndFrame)";
        case Why::Hitch: return "a hitch (a frame over 50 ms)";
        case Why::Resize: return "a swap chain resize";
        default: return "?";
    }
}
// Noise: an unclean window the player cannot help by standing in the world (the runtime, a freeze, a resize) - Out of view / too few are the old kind.
inline bool Noisy(Why w) { return w == Why::Held || w == Why::Hitch || w == Why::Resize; }

struct Result {
    Why why = Why::TooFew;
    float fps = 0;        // 1e6 / the median interval
    float busyMs = 0;     // the median of the frame's own time (interval - xrWaitFrame - xrEndFrame)
    unsigned frames = 0, held = 0, hitches = 0;
    double worstUs = 0;   // the longest interval in the window
    bool Valid() const { return why == Why::Clean; }
};

inline double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (std::ptrdiff_t)m, v.end());
    const double hi = v[m];
    if (v.size() % 2) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + (std::ptrdiff_t)m);
    return (lo + hi) * 0.5;
}

class Window {
  public:
    explicit Window(Limits l = {}) : l_(l) {}
    // A new window from the frame ending at nowUs (its interval is not this window's); resizes: the caller's swap chain resize count now.
    void Start(double nowUs, uint32_t resizes) {
        lastUs_ = nowUs;
        resizes_ = resizes;
        resized_ = false;
        out_ = false;
        intervals_.clear();
        own_.clear();
        held_.clear();
        worst_ = 0;
    }
    // One frame ended at nowUs: what xrWaitFrame blocked and what xrEndFrame took for it (us), whether it counts (in the world, F4), the resize count.
    void Add(double nowUs, double waitUs, double submitUs, bool counts, uint32_t resizes) {
        const double iv = nowUs - lastUs_;
        lastUs_ = nowUs;
        if (!counts) out_ = true;
        if (resizes != resizes_) resized_ = true;
        held_.push_back(waitUs >= l_.holdUs || submitUs >= l_.holdUs);
        worst_ = std::max(worst_, iv);
        intervals_.push_back(iv);
        own_.push_back(std::max(0.0, iv - waitUs - submitUs));
    }
    unsigned Frames() const { return (unsigned)intervals_.size(); }
    Result Take() const {
        Result r;
        r.frames = Frames();
        r.worstUs = worst_;
        const double med = Median(intervals_);
        const double limit = std::max(l_.hitchUs, med * l_.outlierK);
        for (size_t i = 0; i < intervals_.size(); ++i)
            if (intervals_[i] >= limit) ++(held_[i] ? r.held : r.hitches);
        r.fps = med > 0 ? (float)(1e6 / med) : 0.0f;
        r.busyMs = (float)(Median(own_) / 1000.0);
        r.why = out_ ? Why::OutOfView : resized_ ? Why::Resize : r.held ? Why::Held : r.hitches ? Why::Hitch : r.frames < l_.minFrames ? Why::TooFew : Why::Clean;
        return r;
    }

  private:
    Limits l_;
    double lastUs_ = 0, worst_ = 0;
    uint32_t resizes_ = 0;
    bool resized_ = false, out_ = false;
    std::vector<char> held_;   // each frame: xrWaitFrame or xrEndFrame alone took holdUs
    std::vector<double> intervals_, own_;
};

// When the run stops trying: unclean (noisy) windows of one set in a row. Each costs a window (3 s) and a short settle; the runtime's bursts last ~40 s.
constexpr int kNoisyInRow = 10;

}  // namespace autotunemeasure
