// shoulder_swap.h - weapon set I / II by reaching a hand behind either shoulder and pressing its grip.
//
// Pure logic: no game calls, no globals, no windows.h (cleanroom/tests/shoulder_swap_test.cpp feeds it
// synthetic poses). What it decides is only "this press is the gesture's, and it asks for a swap"; the
// swap itself is the game's own "Swap weapons" command (0x2C, the key a player would press), sent by
// vr/shoulder_swap_wire.cpp, and the game's own weapon set - as vr/gamestate.cpp reads it - says whether
// it took (Pending below). Nothing here touches equipment.
//
// The idea and the size of the reach are bearstonem/d2r-pcvr's (MIT, steamvr/shoulder_swap.h); this is
// written afresh on the hand frame vrcam already has: D2RVR_Shared's hands, in metres from the head, in
// the frame of the head's turn alone (x right, y up, z back - OpenXR axes, no pitch or roll), so the
// detector needs no rotation of its own and does not care how the head is tilted.
//
//   - The reach: either shoulder, behind the head's vertical plane - |x| 0.10..0.48 m, y -0.38..+0.16 m
//     from the eyes, z +0.025..+0.38 m behind them. A face-level or hip-level hand, or one straight
//     above the head, is outside.
//   - The press: the grip goes above 0.55 (down) and below 0.20 (up) - one swap per press, never repeated
//     while it is held. A grip already held when the hand comes in, or when tracking returns, does
//     nothing: it has to be let go first.
//   - The dwell: the hand must have been in the reach for 0.12 s before the press, so a hand swung past
//     the shoulder in a throw's wind-up, or a grip squeezed on the way, does not swap.
//   - The press belongs to the gesture while the hand is in the reach (claim): a grip there must not
//     also take a belt potion, hold a staff or press anything else. Reaching back WITHOUT the grip
//     claims nothing - a javelin's wind-up, a bow's reach for the quiver stay free.
//   - A pose gap of more than 0.15 s, a time that goes back, a hand that is not tracked, a hand choice
//     that changes, or a jump of the hand (tracking that relocks: further in one sample than a hand
//     can go) start over: the dwell, and any press in progress, are void until the grip is let go.
#pragma once

#include <cmath>
#include <cstdint>

namespace shoulderswap {

// The reach, metres from the head in the frame of its turn (x right, y up, z back).
constexpr float kAbsXMin = 0.10f, kAbsXMax = 0.48f;
constexpr float kYMin = -0.38f, kYMax = 0.16f;
constexpr float kZMin = 0.025f, kZMax = 0.38f;
// The grip, 0..1: down above kGripDown, up again below kGripUp.
constexpr float kGripDown = 0.55f, kGripUp = 0.20f;
constexpr double kDwellS = 0.12;      // in the reach this long before the press
constexpr double kMaxGapS = 0.15;     // longer between two poses: start over
// A jump: further in one sample than kJumpBaseM plus kJumpSpeedMS for the time between the two.
constexpr float kJumpBaseM = 0.20f, kJumpSpeedMS = 6.0f;

inline bool Finite3(const float p[3]) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }

// Is the hand (x right, y up, z back, from the head, the head's turn frame) behind a shoulder?
inline bool Near(const float p[3]) {
    if (!Finite3(p)) return false;
    const float ax = std::fabs(p[0]);
    return ax >= kAbsXMin && ax <= kAbsXMax && p[1] >= kYMin && p[1] <= kYMax && p[2] >= kZMin && p[2] <= kZMax;
}

// The head's turn alone - yaw about the vertical, no pitch or roll - from a room pose, the way
// vr/vrcam.cpp's XrIntoShared makes D2RVR_Shared's hands: + turned left. For callers that have poses in the
// room (direct_input.h's Frame) instead of D2RVR_Shared's hands from the head.
class Heading {
public:
    // The head's rotation, x y z w (unit). False for a rotation that is not a rotation. A gaze straight up
    // or down has no heading: the last one stays (and none yet is a false Relative).
    bool Update(const float q[4]) {
        if (!std::isfinite(q[0] + q[1] + q[2] + q[3])) return false;
        const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (!(n > 0.5f) || !(n < 2.0f)) return false;
        const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
        const float fx = -2.0f * (x * z + w * y), fz = -(1.0f - 2.0f * (x * x + y * y));   // the look, (0,0,-1) turned
        if (fx * fx + fz * fz > 0.01f) { yaw_ = std::atan2(-fx, -fz); have_ = true; }
        return have_;
    }
    bool Have() const { return have_; }
    float Yaw() const { return yaw_; }
    // The hand from the head, turned into the frame of the head's yaw alone: x right, y up, z back.
    bool Relative(const float head[3], const float hand[3], float out[3]) const {
        if (!have_ || !Finite3(head) || !Finite3(hand)) return false;
        const float dx = hand[0] - head[0], dz = hand[2] - head[2], c = std::cos(yaw_), s = std::sin(yaw_);
        out[0] = dx * c - dz * s;
        out[1] = hand[1] - head[1];
        out[2] = dx * s + dz * c;
        return true;
    }
    void Reset() { have_ = false; yaw_ = 0.0f; }

private:
    float yaw_ = 0.0f;
    bool have_ = false;
};

struct Input {
    double time = 0.0;        // seconds, any steady clock, > 0; moves forward with every pose
    int hand = 0;             // which hand this is (0 right, 1 left): a change starts over
    bool tracked = false;     // this hand's pose is a live one
    float pos[3] = {};        // the hand, from the head, in the frame of its turn (see above)
    float grip = -1.0f;       // 0..1; < 0 not known (nothing is claimed or swapped then)
    // The grip is free to be claimed: false while a menu or a panel is open, the hand holds a belt
    // potion or another thing the grip is already for. A press while it is false is never the gesture's.
    bool allowed = true;
    // A swap may be asked for now: false while the weapon set is not known or an earlier request is
    // still waiting for the game to show it. The press in the reach is claimed all the same.
    bool canSwap = true;
};

struct Output {
    bool swap = false;        // exactly one sample per press: ask for the swap now
    bool claim = false;       // the grip is the gesture's: nothing else may take it (belt, staff, attack...)
    bool inReach = false;     // the hand is behind a shoulder
    bool jump = false;        // this sample was a tracking jump (for the log)
};

class Gesture {
public:
    void Reset() { *this = Gesture{}; }

    Output Step(const Input& in) {
        Output out;
        const bool down = std::isfinite(in.grip) && in.grip >= 0.0f && in.grip > (gripWas_ ? kGripUp : kGripDown);
        const bool valid = in.tracked && std::isfinite(in.time) && in.time > 0.0 && Finite3(in.pos);
        if (!valid || !primed_ || hand_ != in.hand || !(in.time > previous_) || in.time - previous_ > kMaxGapS) {
            // First pose, a gap, a time that went back, another hand: nothing known about the press.
            // Whatever grip is held now is nobody's yet - let go first.
            Reset();
            primed_ = valid;
            hand_ = in.hand;
            previous_ = valid ? in.time : 0.0;
            if (valid) for (int i = 0; i < 3; ++i) last_[i] = in.pos[i];
            gripWas_ = valid && down;
            consumed_ = gripWas_;
            out.claim = valid && down && in.allowed;
            out.inReach = valid && Near(in.pos);
            if (out.inReach) nearSince_ = in.time;
            return out;
        }
        const double dt = in.time - previous_;
        float d2 = 0.0f;
        for (int i = 0; i < 3; ++i) { const float d = in.pos[i] - last_[i]; d2 += d * d; last_[i] = in.pos[i]; }
        const float reach = kJumpBaseM + kJumpSpeedMS * (float)dt;
        out.jump = d2 > reach * reach;
        previous_ = in.time;
        const bool edge = down && !gripWas_;
        gripWas_ = down;
        if (!down) consumed_ = false;
        out.inReach = Near(in.pos);
        if (out.jump) { nearSince_ = 0.0; if (down) consumed_ = true; }   // the pose before it is not to be trusted
        if (!out.inReach) nearSince_ = 0.0;
        else if (nearSince_ == 0.0) nearSince_ = in.time;
        out.claim = in.allowed && (consumed_ || (down && out.inReach));
        if (in.allowed && edge && out.inReach && !out.jump && in.time - nearSince_ >= kDwellS) {
            consumed_ = true;
            out.claim = true;
            out.swap = in.canSwap;
        }
        return out;
    }

private:
    bool primed_ = false, gripWas_ = false, consumed_ = true;
    double previous_ = 0.0, nearSince_ = 0.0;
    int hand_ = -1;
    float last_[3] = {};
};

// A swap asked for, until the game's own weapon set shows it (vr/gamestate.cpp's WeaponSet: 1 or 2, 0
// not known). One request at a time; a request that was not seen to take effect is let go after
// kTimeoutS and NEVER asked again by itself - the next one is a new press.
class Pending {
public:
    enum class Result { None, Applied, NotTaken };
    static constexpr double kTimeoutS = 2.5;

    bool Busy() const { return expect_ != 0; }
    uint32_t Expected() const { return expect_; }
    // The command is being sent while `activeSet` (1 or 2) is in force. False (and nothing waits) when
    // the set is not known or an earlier request is still out.
    bool Begin(double now, uint32_t activeSet) {
        if (expect_ || (activeSet != 1 && activeSet != 2)) return false;
        expect_ = 3 - activeSet;
        at_ = now;
        return true;
    }
    // Every pose (or tick): what the game shows now. Applied once, when it shows the other set;
    // NotTaken once, when it does not in kTimeoutS or the set is lost (no hero).
    Result Update(double now, uint32_t activeSet) {
        if (!expect_) return Result::None;
        if (activeSet == expect_) { expect_ = 0; return Result::Applied; }
        if (activeSet == 0 || !(now - at_ <= kTimeoutS)) { expect_ = 0; return Result::NotTaken; }
        return Result::None;
    }
    void Reset() { expect_ = 0; at_ = 0.0; }

private:
    uint32_t expect_ = 0;
    double at_ = 0.0;
};

}  // namespace shoulderswap
