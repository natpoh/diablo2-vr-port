// melee_marks.h - [debug] melee_boxes: WHERE in a swing the blade's speed reached [gameplay] melee_min_speed (and the stroke's start
// distance was met), and where it fell below it again - left in the picture for a moment, so the two numbers can be tuned by eye. Pure:
// no game calls, no globals; one Track per blade, fed once per pose with where the blade's tip is and the stroke engine's own
// "a contact would count now" flag (stroke::Engine::Counting - the very flag the blade's green colour and the hit gate use, not a
// second estimate of the speed).
//
// What it keeps, per blade:
//   marks   the tip's place at the moment the flag turned on (reached) and at the moment it turned off again (slowed). A turn counts
//           once the new state has held for kMarkHoldMs, so a flicker around the threshold leaves nothing; the mark sits where the
//           flag TURNED, not where it was confirmed. The newest kMarkSlots are kept; each is shown for [debug] melee_mark_ms.
//   trail   the tip's path as a polyline, a point whenever the tip has moved a centimetre or the flag changed; cut to the segments
//           that are young enough and thinned to chords of kTrailChordM, but never across a change of the flag, so the colour boundary
//           sits where the flag changed. A segment takes the flag at its start: cyan below the threshold, green while a contact counts.
// It is a drawing only: nothing here feeds back into the detector.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "stroke_contact.h"

namespace meleeboxes {

constexpr uint32_t kDefaultMarkMs = 800;    // [debug] melee_mark_ms: how long a mark and the trail stay (0 = no marks, no trail)
constexpr uint32_t kMaxMarkMs = 5000;
constexpr uint64_t kMarkHoldMs = 30;        // a change of the flag must hold this long to leave a mark
constexpr size_t kMarkSlots = 6;            // per blade
constexpr size_t kTrailPoints = 160;        // per blade: about a second of pose at 90 Hz
constexpr float kTrailStepM = 0.01f;        // a point whenever the tip has moved this far (or the flag changed)
constexpr float kTrailChordM = 0.18f;       // thinned to chords this long
constexpr float kTrailMinSegM = 0.003f;     // shorter segments are not drawn
constexpr int kTrailSegments = 16;          // per blade, at most
constexpr float kMarkRadiusM = 0.035f;      // the ball of a mark
constexpr float kMarkHalfM = 0.01f;         // ... as a capsule: a short vertical stub (the shader draws no capsule of zero length)
constexpr float kTrailRadiusM = 0.012f;

struct Mark {
    stroke::Vec at;                // where the tip was when the flag turned
    uint64_t ms = 0;               // when
    bool reached = true;           // true: it turned on (the speed was reached), false: it turned off (the blade slowed)
};

struct TrailSegment {
    stroke::Vec a, b;
    bool hot = false;              // a contact would count along it
};

class Track {
public:
    void Clear() { *this = Track{}; }

    // One pose: where the blade's tip is and whether a contact would count now. Poses go forward in time (a step back starts afresh);
    // a tip that is not finite is ignored.
    void Feed(uint64_t nowMs, stroke::Vec tip, bool counting) {
        if (!stroke::Finite(tip)) return;
        if (have_ && nowMs < lastMs_) Clear();
        lastMs_ = nowMs;
        if (!have_) {                              // the first pose is the baseline: no turn to speak of
            have_ = true;
            state_ = counting;
        } else if (counting == state_) {
            pending_ = false;                      // a flicker that went back
        } else {
            if (!pending_) { pending_ = true; pendAt_ = tip; pendMs_ = nowMs; }
            if (nowMs - pendMs_ >= kMarkHoldMs) {
                marks_[markHead_] = {pendAt_, pendMs_, counting};
                markHead_ = (markHead_ + 1) % kMarkSlots;
                markN_ = std::min(markN_ + 1, kMarkSlots);
                state_ = counting;
                pending_ = false;
            }
        }
        if (n_ == 0 || counting != Newest().hot || stroke::Length(tip - Newest().tip) >= kTrailStepM) {
            pts_[head_] = {tip, nowMs, counting};
            head_ = (head_ + 1) % kTrailPoints;
            n_ = std::min(n_ + 1, kTrailPoints);
        }
    }

    // The marks younger than keepMs, newest first. At most max.
    size_t Marks(uint64_t nowMs, uint32_t keepMs, Mark* out, size_t max) const {
        size_t n = 0;
        for (size_t k = 0; k < markN_ && n < max; ++k) {
            const Mark& m = marks_[(markHead_ + kMarkSlots - 1 - k) % kMarkSlots];
            if (nowMs >= m.ms && nowMs - m.ms < keepMs) out[n++] = m;
        }
        return n;
    }

    // The tip's path of the last keepMs as segments, newest first, at most max (the oldest are left out).
    size_t Trail(uint64_t nowMs, uint32_t keepMs, TrailSegment* out, size_t max) const {
        if (!max || n_ < 2 || !keepMs) return 0;
        std::array<const Point*, kTrailPoints> young{};      // the points that are not too old, oldest first
        size_t m = 0;
        for (size_t k = 0; k < n_; ++k) {
            const Point& p = At(k);
            if (nowMs >= p.ms && nowMs - p.ms < keepMs) young[m++] = &p;
        }
        if (m < 2) return 0;
        std::array<const Point*, kTrailPoints> kept{};       // the ends, both sides of every change of the flag, and a point per chord
        size_t q = 0;
        for (size_t i = 0; i < m; ++i) {
            const bool edge = i == 0 || i + 1 == m || young[i]->hot != young[i - 1]->hot || young[i]->hot != young[i + 1]->hot;
            if (edge || stroke::Length(young[i]->tip - kept[q - 1]->tip) >= kTrailChordM) kept[q++] = young[i];
        }
        size_t n = 0;
        for (size_t i = q; i-- > 1 && n < max;) {
            const stroke::Vec a = kept[i - 1]->tip, b = kept[i]->tip;
            if (stroke::Length(b - a) < kTrailMinSegM) continue;
            out[n++] = {a, b, kept[i - 1]->hot};
        }
        return n;
    }

    bool Has() const { return have_; }
    size_t MarkCount() const { return markN_; }
    size_t PointCount() const { return n_; }

private:
    struct Point { stroke::Vec tip; uint64_t ms = 0; bool hot = false; };
    const Point& At(size_t k) const { return pts_[(head_ + kTrailPoints - n_ + k) % kTrailPoints]; }   // oldest first
    const Point& Newest() const { return pts_[(head_ + kTrailPoints - 1) % kTrailPoints]; }
    bool have_ = false, state_ = false, pending_ = false;
    uint64_t lastMs_ = 0, pendMs_ = 0;
    stroke::Vec pendAt_;
    std::array<Mark, kMarkSlots> marks_{};
    size_t markHead_ = 0, markN_ = 0;
    std::array<Point, kTrailPoints> pts_{};
    size_t head_ = 0, n_ = 0;
};

}  // namespace meleeboxes
