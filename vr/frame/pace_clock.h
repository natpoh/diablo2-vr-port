// pace_clock.h - when a pass's "since the last one" clock is valid (vr/frame/frame_order.cpp: DrawByTurns, the pair, the plain frame). Pure C++
// (tools/pace_clock_test.cpp).
//
// 2026-11-25, PC1 on Virtual Desktop: "frame hitch - a pass took 422525.0 ms: game 422514.1" right after Settings switched back to eyes by turns
// (16:02:40), and "213618.9 ms" when the act 3 cinematic ended (16:38:10). Each path kept the end of its own last pass in a static: the by-turns clock still
// held 15:55:38, from before seven minutes of two passes, and 16:34:37, from before three and a half minutes of the cinematic's flat frames (no
// DrawGameScreen at all while a movie plays). The first pass after counted all of it as "the game's own frame" - a line for a freeze that never was, and the
// frame trace's 10 s worst and mean spoiled with it.
//
// Now every top-level DrawGameScreen says which path it takes (Epoch::Frame) with an outside count that moves when something happened between frames that
// is not a frame of ours (a cinematic began); the epoch moves when the path differs from the last frame's or the count moved. A path's clock (Clock) is
// valid only in the epoch it was stamped in: the first pass after a switch starts the clock again and tells nothing. A gap longer than kMaxGapUs is never a
// pass of ours either (the game's longest area loads are under 2 s; a hang has the watchdog's own line): the clock starts again too.
#pragma once

#include <cstdint>

namespace paceclock {

enum class Path : uint8_t { kNone, kTurns, kPair, kPlain };

constexpr double kMaxGapUs = 30.0e6;   // a "pass" longer than this is a restart, not a freeze

class Epoch {
  public:
    // A top-level frame drawn as `p`; `outside` a count that moves when something not a frame of ours came between (gamestate's cinematics begun).
    // Returns the epoch this frame belongs to.
    uint64_t Frame(Path p, uint64_t outside) {
        if (p != last_ || outside != outside_) ++epoch_;
        last_ = p;
        outside_ = outside;
        return epoch_;
    }
    uint64_t Now() const { return epoch_; }

  private:
    uint64_t epoch_ = 0;
    uint64_t outside_ = 0;
    Path last_ = Path::kNone;
};

class Clock {
  public:
    // The end of this path's last pass if it is valid for a pass that starts at `startUs` in `epoch`, else 0 (start again: nothing to tell).
    double Last(uint64_t epoch, double startUs) const {
        if (lastEnd_ <= 0.0 || epoch != epoch_) return 0.0;
        if (startUs - lastEnd_ > kMaxGapUs) return 0.0;
        return lastEnd_;
    }
    // This path's pass ended at `endUs` in `epoch`.
    void End(uint64_t epoch, double endUs) {
        epoch_ = epoch;
        lastEnd_ = endUs;
    }

  private:
    uint64_t epoch_ = 0;
    double lastEnd_ = 0.0;
};

}  // namespace paceclock
