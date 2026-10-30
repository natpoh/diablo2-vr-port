// doll_feet.h - [body] doll_feet: the hero's feet are ours, not the game's. The pure part: where each foot stands and when it steps; skeletons.cpp
// DollFeetLegs puts the legs on them (vr/leg_ik.h), cleanroom/tests/doll_feet_test.cpp drives it, tools/walk_legs pictures it.
//
// Serg, 2026-10-30, after three rounds of correcting the game's legs (the mirror, the walk played backwards, the turn taken out): "the game takes the
// feet for itself - when I turn the camera the feet stay where they are, they don't step; running back the feet swap places. Our task is to unbind them
// from the game completely and give them to the doll." A player in flat F2 (sangee1234, 0.189.0, no body turn at all) still saw crossed legs walking
// back: the game's own walk, which none of those corrections reached.
//
// So the game gives the doll only where the body is, which way it faces and how fast it goes; the feet do the rest, as a person's do:
// - a foot on the ground stays where it was put (in the world): no slide, whatever the body does above it;
// - each foot has its place under the body (its rest stance, turned with the body's facing) - moving, put ahead along the travel by half a step, so the
//   foot planted passes under the body and ends as far behind;
// - a foot that has drifted from its place more than a step's start, or turned from the facing more than kTurnDeg, steps: lifted, carried to its place
//   (followed while it flies), set down facing the body. One foot in the air at a time, the other's turn next while he goes (running: the next one
//   leaves before the first lands - a flight); standing, the one off the most;
// - the left foot never crosses to the right of the body's middle line, nor the right to the left - on the ground or in the air.
// Walking back is then just the travel behind: the places ahead of the feet are behind him, the feet step back, left stays left. Turning the camera
// round him standing: his places turn with him, the feet step after them once they are turned too far.
#pragma once

#include <algorithm>
#include <cmath>

namespace dollfeet {

struct P2 { float x = 0.0f, z = 0.0f; };   // a place on the ground (the world's x, z)
inline P2 Add(P2 a, P2 b) { return {a.x + b.x, a.z + b.z}; }
inline P2 Sub(P2 a, P2 b) { return {a.x - b.x, a.z - b.z}; }
inline P2 Mul(P2 a, float k) { return {a.x * k, a.z * k}; }
inline float Dot(P2 a, P2 b) { return a.x * b.x + a.z * b.z; }
inline float Len(P2 a) { return std::sqrt(Dot(a, a)); }
inline float Smooth(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }
inline float WrapPi(float a) {
    constexpr float kTwoPi = 6.28318531f;
    a = std::fmod(a + 3.14159265f, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    return a - 3.14159265f;
}
// The body's ahead and its left for a facing yaw (radians; skeletons.cpp's T(c, s): ahead (sin, cos), left (cos, -sin) - the rig's left is +x at yaw 0).
inline P2 Ahead(float yaw) { return {std::sin(yaw), std::cos(yaw)}; }
inline P2 Left(float yaw) { return {std::cos(yaw), -std::sin(yaw)}; }

constexpr float kStepM = 0.14f;        // standing: a foot this far from its place steps
constexpr float kTurnDeg = 30.0f;      // going: a foot turned this far from the facing steps (out of its turn)
constexpr float kStandTurnDeg = 15.0f; // standing: ... this far
constexpr float kMovingMps = 0.25f;    // going at least this fast: the feet take turns, each as soon as the other is down
constexpr float kRunMps = 2.6f;        // ... at least this fast: the next foot leaves before the one in the air lands (at kFlightU of its step)
constexpr float kFlightU = 0.55f;
constexpr float kGapM = 0.06f;         // a foot keeps at least this far to its own side of the body's middle line
constexpr float kJumpM = 2.5f;         // the body moved this far in one call: put back under it (an area change, a teleport)

struct Foot {
    P2 at;                  // where it stands (or, flying, where it is above the ground)
    float yaw = 0.0f;       // which way its toes point (radians, as the facing)
    bool flying = false;
    float u = 0.0f;         // of its step done (0..1)
    float dur = 0.3f;       // the step's time, seconds
    P2 from;                // where it left from
    float fromYaw = 0.0f;
    float lift = 0.0f;      // above the ground now (the same units as the places)
};

// One body's feet. Step once a pose: dt seconds; the body's place on the ground (`body`: under the hips), its facing `yaw`, its travel `vel` (units a
// second), each foot's rest stance against the body (x to its left, z ahead; [0] right, [1] left - skeletons.cpp's leg sides), `upm` units a metre.
class Doll {
public:
    void Reset() { have_ = false; }
    bool Have() const { return have_; }
    const Foot& Get(int side) const { return feet_[side & 1]; }
    int Steps() const { return steps_; }   // steps begun, ever
    int Flights() const { return flights_; }

    // The stride at a speed (m/s), as a person's: a step about 0.7 m walking at 1.5 m/s, longer running (at most 1.2); two steps a cycle, a foot in the
    // air 40 % of it walking and 65 % running (then a flight between the steps), 0.15 .. 0.34 s - standing, a quick 0.26 s step. stanceM: how far the
    // body goes over a foot on the ground (it lands half that ahead of its place and leaves as far behind).
    // reachM: how far a planted foot may be from under the hip (metres, along the ground) - the leg's own reach at the hips' height. Serg's 0.190.1 run
    // (the paladin at 6-7 m/s, the dump of 18:30): the person's stride put each foot ~0.85 m from the hip and the leg could not reach it on the ground -
    // the foot was pulled up toward the hip and the leg locked straight ("the feet twist out walking back"). The stance is kept within 1.6 reaches and
    // the steps come faster instead.
    struct Stride { float stepM, cycleS, dur, stanceM; };
    static Stride StrideAt(float speed, float reachM = 1e9f) {
        Stride s;
        s.stepM = std::clamp(0.35f + 0.25f * speed, 0.35f, 1.2f);
        if (speed < kMovingMps) { s.cycleS = 0.0f; s.dur = 0.26f; s.stanceM = 0.0f; return s; }
        const float duty = speed >= kRunMps ? 0.65f : 0.4f;   // of the cycle a foot flies
        const float most = 1.6f * std::max(reachM, 0.1f);
        s.stepM = std::min(s.stepM, most / (2.0f * (1.0f - duty)));   // (stance = 2 step (1 - duty) while the swing's time is not clamped)
        s.cycleS = 2.0f * s.stepM / speed;
        s.dur = std::clamp(duty * s.cycleS, 0.1f, 0.34f);
        s.stanceM = std::clamp(2.0f * s.stepM - speed * s.dur, 0.1f, most);
        return s;
    }
    // The place a foot wants now (on its own side of the middle line): its rest stance turned with the body - moving, half a stance ahead along the travel.
    P2 Place(int side, P2 body, float yaw, P2 vel, const P2 stance[2], float upm) const {
        const P2 a = Ahead(yaw), l = Left(yaw);
        P2 p = Add(Add(body, Mul(l, stance[side].x)), Mul(a, stance[side].z));
        const float speed = Len(vel) / std::max(upm, 1e-3f);
        if (speed >= kMovingMps) p = Add(p, Mul(vel, 0.5f * StrideAt(speed, reach_).stanceM / speed));
        return KeepSide(side, p, body, yaw, stance, upm);
    }

    void Step(float dt, P2 body, float yaw, P2 vel, const P2 stance[2], float upm, float reachM = 1e9f) {
        reach_ = std::isfinite(reachM) && reachM > 0.0f ? reachM : 1e9f;
        dt = std::clamp(std::isfinite(dt) ? dt : 0.0f, 0.0f, 0.1f);
        upm = std::max(upm, 1e-3f);
        if (!std::isfinite(body.x + body.z + yaw + vel.x + vel.z)) return;
        const float speed = Len(vel) / upm;
        const Stride stride = StrideAt(speed, reach_);
        const float stepM = stride.stanceM, dur = stride.dur;   // (moving: a foot goes once the body is this far past its landing)
        const float liftTop = std::min(0.07f + 0.02f * speed, 0.18f) * upm;
        if (!have_ || Len(Sub(body, last_)) > kJumpM * upm) {   // first call, or the body jumped: the feet put under it
            for (int side = 0; side < 2; ++side) {
                Foot& f = feet_[side];
                f = Foot{};
                f.at = Place(side, body, yaw, P2{}, stance, upm);
                f.yaw = yaw;
            }
            have_ = true;
            last_ = body;
            return;
        }
        last_ = body;
        // the feet in the air: carried on toward their places (followed - the body goes on), set down at the end
        for (int side = 0; side < 2; ++side) {
            Foot& f = feet_[side];
            if (!f.flying) { f.lift = 0.0f; continue; }
            f.u = std::min(1.0f, f.u + dt / std::max(f.dur, 0.05f));
            const P2 to = Place(side, body, yaw, vel, stance, upm);
            const float k = Smooth(f.u);
            f.at = KeepSide(side, Add(f.from, Mul(Sub(to, f.from), k)), body, yaw, stance, upm);
            f.yaw = f.fromYaw + WrapPi(yaw - f.fromYaw) * k;
            f.lift = liftTop * std::sin(3.14159265f * f.u);
            if (f.u >= 1.0f) { f.flying = false; f.at = to; f.yaw = yaw; f.lift = 0.0f; lastSide_ = side; }
        }
        // a step begins? one foot in the air at a time - running, the next leaves when the one flying is past kFlightU
        const bool moving = speed >= kMovingMps;
        if (!moving) firstDone_ = false;
        int inAir = -1;
        for (int side = 0; side < 2; ++side) if (feet_[side].flying) inAir = side;
        if (inAir >= 0 && !(speed >= kRunMps && feet_[inAir].u >= kFlightU && !feet_[inAir ^ 1].flying)) return;
        float off[2], turn[2];
        for (int side = 0; side < 2; ++side) {
            const Foot& f = feet_[side];
            off[side] = f.flying ? 0.0f : Len(Sub(f.at, Place(side, body, yaw, vel, stance, upm))) / upm;
            turn[side] = f.flying ? 0.0f : std::fabs(WrapPi(yaw - f.yaw)) * 57.2957795f;
        }
        int go = -1;
        if (inAir >= 0) {   // running: the other one may leave before this one lands - once it is due, as below
            if (off[inAir ^ 1] >= stepM || turn[inAir ^ 1] > kTurnDeg) go = inAir ^ 1;
        } else if (moving) {
            // taking turns: the next foot goes once it is a step from its place (it landed half a step ahead, the body has gone on) - the first step from
            // standing sooner, at 0.4 of one (both feet stood at their places)
            const int next = lastSide_ ^ 1;
            const float due = (firstDone_ ? 1.0f : 0.4f) * stepM;
            if (off[next] >= due || turn[next] > kTurnDeg) go = next;
            else if (off[next ^ 1] >= 1.5f * stepM) go = next ^ 1;
        } else {
            // standing: a foot off its place by kStepM or turned kStandTurnDeg from the facing steps (Serg 2026-10-30: "I turn with the mouse and the
            // feet stand" - his turns of 20-28 deg never reached the 30 of a walk); the other then follows it if it is off at all - both end square
            const float s0 = std::max(off[0] / kStepM, turn[0] / kStandTurnDeg), s1 = std::max(off[1] / kStepM, turn[1] / kStandTurnDeg);
            if (std::max(s0, s1) > 1.0f) {
                go = s1 > s0 ? 1 : 0;
                square_ = true;   // (when it is down, the other squares up)
            } else if (square_) {   // the last standing step is down (none is in the air here): the other foot to its place if it is off at all
                square_ = false;
                const int other = lastSide_ ^ 1;
                if (off[other] > 0.04f || turn[other] > 5.0f) go = other;
            }
        }
        if (moving) square_ = false;
        if (go < 0) return;
        Foot& f = feet_[go];
        f.flying = true;
        f.u = 0.0f;
        f.dur = dur;
        f.from = f.at;
        f.fromYaw = f.yaw;
        ++steps_;
        if (inAir >= 0) ++flights_;
        if (moving) firstDone_ = true;
    }

private:
    Foot feet_[2];
    bool have_ = false;
    bool firstDone_ = false;   // a step taken since he began to move
    float reach_ = 1e9f;       // the leg's reach along the ground (metres), as the last Step was told
    bool square_ = false;      // standing, a step for the facing landed: the other foot follows it (squared up)
    P2 last_;
    int lastSide_ = 0;
    int steps_ = 0, flights_ = 0;

    // p moved across, if need be, to stay on its foot's side of the body's middle line (at least kGapM, or the stance's own gap if less)
    static P2 KeepSide(int side, P2 p, P2 body, float yaw, const P2 stance[2], float upm) {
        const P2 l = Left(yaw);
        const float sign = stance[side].x >= 0.0f ? 1.0f : -1.0f;
        const float gap = std::min(kGapM * upm, std::fabs(stance[side].x));
        const float across = Dot(Sub(p, body), l) * sign;
        return across >= gap ? p : Add(p, Mul(l, (gap - across) * sign));
    }
};

}  // namespace dollfeet
