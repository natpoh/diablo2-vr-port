// flatvr_pose.h - FlatVR as good as native OpenXR (docs/flatvr_sync.md): the parts that are plain arithmetic.
//
// FlatVR shows each picture on a screen at the very head pose the picture was drawn with (the frame stamps). The world then
// stands still in the room only if the game's camera turned exactly as that head did, from the same zero, and the pictures
// come one a FlatVR frame. Native OpenXR gets all three from the runtime (xrWaitFrame, the eyes' poses). Here:
//   - TrueRollDeg: BodyWalk's roll back to the head's own roll;
//   - RoomToWorld: the head's place in the room since the recentre, in the game's world (the camera moves with the head);
//   - Sync: when a pair may start - on FlatVR's next frame, as xrWaitFrame starts one in native.
// vrcam.cpp uses them; cleanroom/tests/flatvr_pose_test.cpp checks them.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flatvrpose {

constexpr float kDeg = 0.0174532925f;

// BodyWalk sends the head's roll as -asin(right.y) (UniversalOutputService::update): for a head turned by yaw, then pitch,
// then roll, that is asin(sin(roll) * cos(pitch)) - looking 60 degrees down the camera rolled half as far as the head. The
// roll itself back (degrees). Straight up or down (|pitch| within 3 degrees of 90) the roll is not defined: as sent.
inline float TrueRollDeg(float sentRollDeg, float pitchDeg) {
    const float c = std::cos(pitchDeg * kDeg);
    if (!(c > 0.05f)) return sentRollDeg;
    const float s = std::clamp(std::sin(sentRollDeg * kDeg) / c, -1.0f, 1.0f);
    return std::asin(s) / kDeg;
}

struct V { float x, y, z; };

// The head's move in the room (BodyWalk's room = FlatVR's: x right, y up, z back, metres) into the game's world: turned so
// that the room's heading `roomYawRad` (the head's, + left, atan2(-x, -z)) lands on the camera's heading `camHeadingRad`
// (vrcam's Facing: 0 = -z, + left), the same turn native OpenXR's toWorld makes; times the world's units a metre. A move
// longer than `maxM` metres (a tracking glitch, the head far from where it was centred) is shortened to it.
inline V RoomToWorld(V d, float camHeadingRad, float roomYawRad, float unitsPerM, float maxM) {
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(len)) return {0.0f, 0.0f, 0.0f};
    if (len > maxM && len > 0.0f) { const float k = maxM / len; d = {d.x * k, d.y * k, d.z * k}; }
    const float base = camHeadingRad - roomYawRad;
    const float cb = std::cos(base), sb = std::sin(base);
    return {(d.x * cb + d.z * sb) * unitsPerM, d.y * unitsPerM, (-d.x * sb + d.z * cb) * unitsPerM};
}

// ---------------------------------------------------------------------------------------------------------------------------
// Pairs on FlatVR's frames ([flatvr] frame_sync). FlatVR hands BodyWalk a new head at each of its frames, right after its
// xrWaitFrame, named by a stamp (the clock of D2RVRStampNow: 0.1 ms); Game Link passes it on with BW_LINK_TRACK_FLATVR_STAMP.
// A pair starts when such a head is new - one pair a FlatVR frame, always at the same point of it - instead of on a timer of
// our own beside the game's frame cap and FlatVR's own clock (three clocks: each picture shown once or twice in no order).
//
// A pair that does not fit a frame: started at once on the next head it ends later and later into FlatVR's frames, and the
// pictures come 1,1,2,1,2 - the judder again. So (mode kEven) the pairs' own time is watched: past ~0.95 of a frame they go
// on every other head (an even 45 of 90, each picture shown twice at its own pose, as a runtime halves a game that misses),
// back to every head under ~0.8. Mode kEvery takes each new head whatever the pairs take (the most pictures, unevenly).
// A head that is not fresh (older than `lateFrac` of a frame: the last pair ran over) is let go and the next one waited for.
// No FlatVR head (FlatVR off or stalled, a head of another source): give up at once, or after `kGiveUpFrames` frames of
// waiting - the old timer then paces as before.
enum class Step { Take, Wait, GiveUp };
enum Mode { kOff = 0, kEven = 1, kEvery = 2 };

struct Sync {
    static constexpr float kGiveUpFrames = 2.5f;
    static constexpr uint32_t kStaleTicks = 500;   // 50 ms: FlatVR's last head older than that - FlatVR is not running frames
    static constexpr int kMaxInterval = 3;
    uint32_t taken = 0;      // the stamp of the head the last pair started with, and when it started (our clock)
    uint32_t takenAt = 0;
    bool takenOk = false;
    uint32_t letGo = 0;      // the last head let go as too old (counted once)
    bool letGoOk = false;
    uint32_t seen = 0;       // the newest FlatVR head seen (for the period)
    bool seenOk = false;
    float periodTicks = 111.1f;   // FlatVR's frame, 0.1 ms; learnt from the heads' stamps
    float pairTicks = 0.0f;       // a pair's own time, start to the next wait (smoothed)
    int interval = 1;             // FlatVR frames a pair (kEven: 2 when the pairs do not fit a frame)
    uint32_t skipped = 0;         // heads let go because they came too old

    explicit Sync(float hz = 90.0f) { periodTicks = hz > 1.0f ? 10000.0f / hz : 111.1f; }

    // A head was looked at: learns FlatVR's frame from consecutive stamps that are about a frame apart (a look that missed one
    // sees two frames: not taken; a rate other than the one guessed - 72, 80, 120 Hz - is taken within 40%).
    void See(uint32_t stamp) {
        if (seenOk && stamp != seen) {
            const float d = (float)(int32_t)(stamp - seen);
            if (d > 0.6f * periodTicks && d < 1.4f * periodTicks) periodTicks = 0.9f * periodTicks + 0.1f * d;
        }
        seen = stamp;
        seenOk = true;
    }

    // headStamp: the head the game would draw with now; flatVr: it is FlatVR's own (named) head; now: D2RVRStampNow();
    // waitedTicks: how long this pair's start has waited so far (0 at the first look: the last pair has just ended);
    // lateFrac: a head older than this share of a frame is let go; mode: kEven or kEvery.
    Step Decide(uint32_t headStamp, bool flatVr, uint32_t now, uint32_t waitedTicks, float lateFrac, int mode = kEven) {
        if (waitedTicks == 0 && takenOk) {   // the last pair's own time
            const float d = (float)(int32_t)(now - takenAt);
            if (d > 0.0f && d < 10.0f * periodTicks) pairTicks = pairTicks > 0.0f ? 0.8f * pairTicks + 0.2f * d : d;
            if (mode == kEven) {
                const float frames = pairTicks / periodTicks;
                if (frames > 0.95f * (float)interval && interval < kMaxInterval) ++interval;
                else if (interval > 1 && frames < 0.8f * (float)(interval - 1)) --interval;
            } else {
                interval = 1;
            }
        }
        if (!flatVr) return Step::GiveUp;
        const int32_t age = (int32_t)(now - headStamp);
        if (age < 0 || (uint32_t)age > kStaleTicks) return Step::GiveUp;
        See(headStamp);
        if (waitedTicks > (uint32_t)(kGiveUpFrames * periodTicks * (float)interval)) return Step::GiveUp;
        if (takenOk) {
            const float since = (float)(int32_t)(headStamp - taken);
            if (since < ((float)interval - 0.5f) * periodTicks) return Step::Wait;   // the last pair's head, or not yet its turn
        }
        if ((float)age > std::clamp(lateFrac, 0.05f, 1.0f) * periodTicks) {
            if (!(letGoOk && letGo == headStamp)) { letGo = headStamp; letGoOk = true; ++skipped; }
            return Step::Wait;   // too old to make its frame: the next one
        }
        taken = headStamp;
        takenAt = now;
        takenOk = true;
        return Step::Take;
    }

    // Ticks until the next FlatVR head is due (0 when it is due now or over): how long a wait may sleep.
    uint32_t DueIn(uint32_t now) const {
        if (!seenOk) return 0;
        const int32_t left = (int32_t)(seen + (uint32_t)periodTicks - now);
        return left > 0 ? (uint32_t)left : 0u;
    }
};

}  // namespace flatvrpose
