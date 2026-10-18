// arm_reach.h - [bow] aim_distance_by_arm: how far out a SPELL's aim point is follows how far the aiming arm is stretched. Pure: no game calls, no globals, no clock of its own
// (cleanroom/tests/arm_reach_test.cpp).
//
// In flat D2 a Shift + click near the hero or far from him decides how a multi-missile spell (Teeth and the like) spreads: a near point is a wide fan, a far point a tight cone. The
// aim point of the plugin sits at a fixed distance per weapon ([bow] caster_range, hand_range, staff_range_one / _two), so the player had no say. Here the arm decides - for a wand,
// an orb, bare hands and a staff with one hand on it: an arm at the body gives the near end, an arm stretched out along the aim gives the far end (the weapon's own distance as it is
// today; for a staff the two-hand one). Not for a bow or a crossbow (their fixed range), not for a throw, and a staff held in BOTH hands always aims at its far end, the tightest cone.
//
// The extension is measured on the player's own arm, nothing is tuned by hand: the shoulder and the length from the shoulder to the grip are the drawn arm's own
// (vr/skeletons.cpp ArmReach: the hero's arm bones fitted to the T-pose span of [calibration], else to the player's height), and the extension is how much of that length
// the hand has gone out from the shoulder ALONG THE AIM: a straight arm pointing at the target is 1 whatever its elevation, an arm hanging with the controller turned
// forward is about 0, an elbow bent in is in between. It only chooses the point the game's own attack is aimed at - what Shift + click does - nothing else.
#pragma once

#include <algorithm>
#include <cmath>

namespace armreach {

// The extension runs over its whole range, 0 to 1, with no dead zone at either end (a tracked arm is a little short of 1 when it is straight: the point is then a little short of the far end).
constexpr float kMinExtension = 0.0f, kMaxExtension = 1.0f;

// How fast the point follows the arm: an exponential filter of this time constant (a slight smoothing so the point does not jitter with the hand), and a gap after which
// the last value is forgotten (a cast a while after the last one starts from the arm as it is).
constexpr double kSmoothS = 0.08, kGapS = 0.5;

// The least the near end can be: the aim point routine (vr/aim_point.h) gives up under this and leaves the game's own point of about 3 cells.
constexpr float kNearMinCells = 3.0f;

// The hand's extension along the aim: (hand - shoulder) . aim / armLength, clamped to 0..1. Any one frame for all (metres, or world units with the length in them). False with
// anything not finite, no length, or no direction.
inline bool Extension(const float hand[3], const float shoulder[3], const float aim[3], float armLength, float* out) {
    if (!out || !std::isfinite(armLength) || !(armLength > 1e-4f)) return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(hand[i]) || !std::isfinite(shoulder[i]) || !std::isfinite(aim[i])) return false;
    const float al = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (!(al > 1e-4f)) return false;
    const float along = ((hand[0] - shoulder[0]) * aim[0] + (hand[1] - shoulder[1]) * aim[1] + (hand[2] - shoulder[2]) * aim[2]) / al;
    *out = std::clamp(along / armLength, kMinExtension, kMaxExtension);
    return true;
}

// Cells from the extension: the near end at 0, `farCells` (what the weapon asks today) at 1, between them straight. A near end above the far one is the far one (an arm cannot
// make the point farther than the weapon's own distance); below kNearMinCells it is that.
inline float Cells(float extension, float nearCells, float farCells) {
    if (!std::isfinite(farCells)) return kNearMinCells;
    const float n = std::clamp(std::isfinite(nearCells) ? nearCells : kNearMinCells, kNearMinCells, std::max(kNearMinCells, farCells));
    const float e = std::isfinite(extension) ? std::clamp(extension, 0.0f, 1.0f) : 1.0f;
    return n + e * (std::max(n, farCells) - n);
}

// The extension, smoothed: Step(e, now) with `now` in seconds (any base). The first value, or one after a gap, is taken as it is.
class Smoother {
public:
    float Step(float e, double nowS) {
        if (!have_ || !(nowS - at_ >= 0.0) || nowS - at_ > kGapS) {
            v_ = e;
        } else {
            const double dt = nowS - at_;
            const float k = static_cast<float>(1.0 - std::exp(-dt / kSmoothS));
            v_ += (e - v_) * k;
        }
        have_ = true;
        at_ = nowS;
        return v_;
    }
    void Reset() { have_ = false; }
    bool Have() const { return have_; }
    float Value() const { return v_; }

private:
    bool have_ = false;
    double at_ = 0.0;
    float v_ = 1.0f;
};

// A distance in cells as metres on the ground (a cell is two world units; `unitsPerMetre` is the world's): for the debug ray's end.
inline float CellsToMetres(float cells, float unitsPerMetre) {
    return unitsPerMetre > 1e-4f ? cells * 2.0f / unitsPerMetre : 0.0f;
}

}  // namespace armreach
