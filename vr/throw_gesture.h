#pragma once
// A javelin throw seen in the hand's own motion: hold the trigger, swing, let go - the release is
// the throw, and the way the hand was going is where it goes. A pure detector (no game calls, no
// globals, no clock of its own): the caller feeds it the throwing hand's position and trigger once
// per headset frame; unit test cleanroom/tests/throw_gesture_test.cpp on synthetic poses.
//
// What the detector decides is only WHEN and WHICH WAY. The throw itself is the game's own
// attack/throw command, pressed once by the caller at the release with its aim point along the
// motion (vr/vrcam.cpp, [gameplay] throw_gesture): the game's missile, speed, collision, ammo and
// animation stay. Nothing here touches them.
//
// The idea and the numbers (travel, speed band, grace, hold time) are bearstonem/d2r-pcvr's
// (MIT, steamvr/gameplay.h, the javelin branch). The code is written fresh: a velocity over a
// short window instead of one frame's step, a guard that a throw needs a heading on the ground,
// and a blocked state that waits for the trigger to go up after any cancel.
//
// Rules, in order of a frame:
//  - the gesture starts on the trigger's EDGE (up -> down). A trigger already down when the
//    detector starts, or when it was cancelled, is ignored until it has gone up: a jab or a
//    swing with the trigger up is never a gesture;
//  - from the edge to the release the hand must travel >= minTravelM from where it was at the
//    edge, and have moved at a speed of minSpeed..maxSpeed m/s at some sample (the last such
//    sample is the throw's direction) no longer than graceS before the release;
//  - held for minHeldS..maxHeldS from the edge to the release;
//  - a release with the hand at rest (or too slow, or too fast) does nothing;
//  - a cancel, a hand not tracked, a gap in the frames or a jump of the pose (a recentre, a
//    tracking glitch) ends the gesture without a throw, and no new one starts until the trigger
//    has been up once.
//
// Hold mode (Params::hold, [hands] throw_by_grip): the control is a grip that is HELD for as long as the weapon
// is in the hand - seconds, minutes - and the throw is the swing at the end of it, ended by letting go. So no
// upper limit on the hold, and the travel is the swing's own: how far the hand got, in the last travelWindowS
// before the last fast sample, from where it was - not from the grip's edge. The rest is the same: a release
// with the hand at rest throws nothing, the last fast sample must be within graceS of the release, the hand's
// speed within the band.
//
// Frame of the positions: any fixed frame in which y is up (the room: OpenXR stage space, x
// right, y up, z back). The caller turns the returned direction into the game's.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace throwgesture {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Params {
    float minTravelM = 0.18f;        // farthest the hand got from where the trigger went down
    float minSpeed = 1.4f;           // m/s: the hand must have moved this fast...
    float maxSpeed = 12.0f;          // ...and not faster (faster is a tracking glitch)
    double graceS = 0.08;            // the last such sample may be this long before the release
    double minHeldS = 0.08;          // edge to release
    double maxHeldS = 3.0;
    double windowS = 0.03;           // velocity = position change over about this long (0: one frame)
    double maxGapS = 0.10;           // longer between two frames: a hitch, the gesture is dropped
    float maxStepM = 0.35f;          // farther in one frame: a recentre or a glitch, dropped
    float minHorizontalFrac = 0.30f; // ground part of the velocity / its length: less is a throw up or down
    bool hold = false;               // the control is held as long as the weapon is in the hand (see above)
    double travelWindowS = 0.7;      // hold mode: the swing's travel is measured over this long before the last fast sample
    double peakWindowS = 0.15;       // the peak velocity (Result::peak) is looked for this long before the release...
    int peakTop = 3;                 // ...and is the speed-weighted mean of this many of the fastest samples in it (noise)
};

struct Sample {
    double t = 0.0;      // seconds, the frame's own time (one new value per headset frame)
    Vec3 hand;           // the throwing hand, metres, y up
    bool tracked = true; // the hand's pose is valid this frame
    bool trigger = false;// the trigger counts as down (the caller's hysteresis)
    bool cancel = false; // a menu, a change of weapon, a recentre: drop whatever is under way
};

// Why a release was not a throw (for the log: a test in the headset should say what it saw).
enum class Why : uint8_t {
    None,           // not a release, or a throw
    Quick,          // held less than minHeldS
    NoFastSample,   // the hand was never in the speed band (at rest, or too slow, or a glitch's speed)
    Stale,          // the last sample in the band was longer than graceS before the release
    ShortTravel,    // the swing was shorter than minTravelM
    Steep,          // the throw was mostly up or down: no heading on the ground
};

struct Result {
    bool started = false;   // the trigger's edge opened a gesture this frame
    bool released = false;  // the trigger went up with a gesture under way (a throw or not: why says)
    Why why = Why::None;    // released, not thrown: the reason
    double sinceFast = -1.0;// seconds from the last sample in the band to the release (released; -1: none)
    bool throwNow = false;  // released as a throw: press the game's throw once
    Vec3 dir;               // unit vector along the hand's motion (throwNow)
    float groundX = 0.0f;   // dir's ground part (x, z) as a unit vector (throwNow); y is up
    float groundZ = 0.0f;
    float speed = 0.0f;     // m/s at the last sample in the band (throwNow)
    // The hand's PEAK velocity in the peakWindowS before the release (throwNow): the mean of the fastest few samples, as a vector. The release is
    // at the end zone of a swing, where the hand is already slowing and, swung from high up, going down: `speed` and `dir` are that moment's, this
    // is the throw's own strength and elevation (what a thrown object's flight is made from). Never slower than `speed`.
    Vec3 peak;              // m/s
    float peakSpeed = 0.0f;
    float travel = 0.0f;    // metres from the edge's place (throwNow)
    double held = 0.0;      // seconds from the edge (throwNow)
};

// -- from the room to the head's frame --------------------------------------------------------------------------
// The caller makes the game's heading of a throw the way it does for the hand's ray: components along the
// head's own right and forward on the ground, turned into the world with the camera's right and forward.

// The head's forward on the ground (x, z of the room) from its orientation (unit quaternion x y z w, OpenXR
// axes: the view runs along -z, up is +y): the view axis, or where the top of the head points when the view
// is steeply up or down. False for a quaternion that is not one, or no way to tell.
inline bool HeadGroundForward(const float q[4], float* fx, float* fz) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float n = x * x + y * y + z * z + w * w;
    if (!(n > 0.9f && n < 1.1f)) return false;
    auto rot = [&](float vx, float vy, float vz, float* r) {   // q v q*
        const float tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
        r[0] = vx + w * tx + (y * tz - z * ty);
        r[1] = vy + w * ty + (z * tx - x * tz);
        r[2] = vz + w * tz + (x * ty - y * tx);
    };
    float f[3], u[3];
    rot(0, 0, -1, f);
    rot(0, 1, 0, u);
    float hx = f[0], hz = f[2];
    if (std::sqrt(hx * hx + hz * hz) < 0.35f) {   // the head's top points the way the feet face
        hx = f[1] < 0.0f ? u[0] : -u[0];
        hz = f[1] < 0.0f ? u[2] : -u[2];
    }
    const float l = std::sqrt(hx * hx + hz * hz);
    if (!(l > 0.05f) || !std::isfinite(l)) return false;
    *fx = hx / l;
    *fz = hz / l;
    return true;
}

// A direction on the ground (gx, gz: room x and z) in the frame of a head facing (fx, fz): how far to its
// right and how far ahead. The head's right is (-fz, fx): facing -z (fx 0, fz -1) it is +x.
inline void ToHeadFrame(float gx, float gz, float fx, float fz, float* right, float* forward) {
    *right = gx * -fz + gz * fx;
    *forward = gx * fx + gz * fz;
}

class Detector {
public:
    explicit Detector(const Params& p = Params()) : p_(p) {}

    // A gesture is under way: the trigger went down and has not come up.
    bool Throwing() const { return throwing_; }
    // Drop everything; no new gesture until the trigger has been up once.
    void Cancel() {
        throwing_ = false;
        primed_ = false;
        blocked_ = true;
        n_ = 0;
        vn_ = vstart_ = 0;
    }

    Result Step(const Sample& s) {
        Result r;
        if (!s.tracked || s.cancel || !std::isfinite(s.t) || !Finite(s.hand)) {
            Cancel();
            return r;
        }
        if (primed_) {
            const double dt = s.t - lastT_;
            if (dt == 0.0) return r;   // the same frame again: nothing new
            if (dt < 0.0 || dt > p_.maxGapS || Length(s.hand - lastPos_) > p_.maxStepM) Cancel();
        }
        if (!primed_) {
            // The first frame after a start or a cancel: only learns where the hand is. A trigger
            // already down belongs to an earlier grip, not to a throw.
            primed_ = true;
            lastT_ = s.t;
            lastPos_ = s.hand;
            triggerWas_ = s.trigger;
            blocked_ = s.trigger;
            n_ = 0;
            Push(s.t, s.hand);
            return r;
        }
        lastT_ = s.t;
        lastPos_ = s.hand;
        const bool edge = s.trigger && !triggerWas_;
        const bool released = !s.trigger && triggerWas_;
        triggerWas_ = s.trigger;
        if (!s.trigger) blocked_ = false;
        Push(s.t, s.hand);
        if (blocked_) return r;

        if (edge) {
            throwing_ = true;
            startT_ = s.t;
            startPos_ = s.hand;
            travel_ = 0.0f;
            haveGood_ = false;
            n_ = 0;
            vn_ = vstart_ = 0;
            Push(s.t, s.hand);   // the window starts at the edge: what came before is not the throw
            r.started = true;
            return r;
        }
        if (!throwing_) return r;

        const double held = s.t - startT_;
        if (!p_.hold && held > p_.maxHeldS) {   // held too long to be a throw; the release will find nothing
            throwing_ = false;
            return r;
        }
        if (!p_.hold) {
            const float away = Length(s.hand - startPos_);
            if (away > travel_) travel_ = away;
        }
        Vec3 v;
        float fastestStep = 0.0f;
        if (Velocity(s.t, s.hand, &v, &fastestStep)) {
            const float sp = Length(v);
            if (sp <= p_.maxSpeed && fastestStep <= p_.maxSpeed) PushV(s.t, v, sp);   // (any speed: the peak is the fastest of them)
            // in the band as the window sees it AND no single frame in it faster than the band
            // allows: one frame's jump smoothed over the window would otherwise look like a throw
            if (sp >= p_.minSpeed && sp <= p_.maxSpeed && fastestStep <= p_.maxSpeed) {
                good_ = v;
                goodT_ = s.t;
                goodSpeed_ = sp;
                haveGood_ = true;
                if (p_.hold) travel_ = Reach(s.t, s.hand);   // the swing that led to this sample
            }
        }
        if (!released) return r;
        throwing_ = false;
        r.released = true;
        r.travel = travel_;
        r.held = held;
        r.speed = haveGood_ ? goodSpeed_ : 0.0f;
        r.sinceFast = haveGood_ ? s.t - goodT_ : -1.0;
        if (held < p_.minHeldS) { r.why = Why::Quick; return r; }
        if (!haveGood_) { r.why = Why::NoFastSample; return r; }
        if (s.t - goodT_ > p_.graceS) { r.why = Why::Stale; return r; }
        if (travel_ < p_.minTravelM) { r.why = Why::ShortTravel; return r; }
        const float horiz = std::sqrt(good_.x * good_.x + good_.z * good_.z);
        if (!(horiz >= p_.minHorizontalFrac * goodSpeed_) || !(horiz > 1e-6f)) { r.why = Why::Steep; return r; }
        r.throwNow = true;
        r.dir = {good_.x / goodSpeed_, good_.y / goodSpeed_, good_.z / goodSpeed_};
        r.groundX = good_.x / horiz;
        r.groundZ = good_.z / horiz;
        r.speed = goodSpeed_;
        Peak(s.t, &r.peak, &r.peakSpeed);
        r.travel = travel_;
        r.held = held;
        return r;
    }

private:
    static constexpr int kRing = 160;   // 1.3 s at 120 Hz: the velocity window and the hold mode's travel window
    struct Stamp { double t; Vec3 p; };

    // ring_[start_ .. start_ + n_) in order, oldest first
    const Stamp& At(int i) const { return ring_[(start_ + i) % kRing]; }
    void Push(double t, const Vec3& p) {
        if (n_ == kRing) {
            start_ = (start_ + 1) % kRing;
            --n_;
        }
        ring_[(start_ + n_) % kRing] = {t, p};
        ++n_;
    }

    // The velocity samples of the gesture so far (the last kV): the peak is picked from these at the release.
    static constexpr int kV = 64;
    struct VStamp { double t; Vec3 v; float speed; };
    void PushV(double t, const Vec3& v, float speed) {
        if (vn_ == kV) {
            vstart_ = (vstart_ + 1) % kV;
            --vn_;
        }
        vring_[(vstart_ + vn_) % kV] = {t, v, speed};
        ++vn_;
    }
    // The speed-weighted mean of the peakTop fastest samples in the peakWindowS up to t; the last fast sample's own when none is.
    void Peak(double t, Vec3* peak, float* speed) const {
        bool taken[kV] = {};
        Vec3 sum;
        float weights = 0.0f;
        for (int pick = 0; pick < std::max(1, p_.peakTop); ++pick) {
            int best = -1;
            for (int i = 0; i < vn_; ++i) {
                const VStamp& e = vring_[(vstart_ + i) % kV];
                if (taken[i] || t - e.t > p_.peakWindowS) continue;
                if (best < 0 || e.speed > vring_[(vstart_ + best) % kV].speed) best = i;
            }
            if (best < 0) break;
            taken[best] = true;
            const VStamp& e = vring_[(vstart_ + best) % kV];
            sum = {sum.x + e.v.x * e.speed, sum.y + e.v.y * e.speed, sum.z + e.v.z * e.speed};
            weights += e.speed;
        }
        if (!(weights > 0.0f)) {
            *peak = good_;
            *speed = goodSpeed_;
            return;
        }
        *peak = {sum.x / weights, sum.y / weights, sum.z / weights};
        *speed = Length(*peak);
        if (*speed < goodSpeed_) {   // never slower than the last fast sample (the mean of tilted vectors can be)
            *peak = good_;
            *speed = goodSpeed_;
        }
    }

    // Hold mode: the farthest the hand was, in the travel window up to now, from where it is.
    float Reach(double t, const Vec3& p) const {
        float farthest = 0.0f;   // (not "far": windows.h defines that)
        for (int i = n_ - 1; i >= 0; --i) {
            if (t - At(i).t > p_.travelWindowS) break;
            const float d = Length(p - At(i).p);
            if (d > farthest) farthest = d;
        }
        return farthest;
    }

    // The hand's velocity at the newest sample: its change of place since the newest earlier
    // sample at least windowS old, or since the oldest kept when the gesture is younger than that.
    // The newest sample is already in the ring. fastest: the quickest single frame inside the window.
    bool Velocity(double t, const Vec3& p, Vec3* v, float* fastest) const {
        if (n_ < 2) return false;
        int ref = 0;
        for (int i = n_ - 2; i >= 0; --i) {
            ref = i;
            if (t - At(i).t >= p_.windowS) break;
        }
        const double dt = t - At(ref).t;
        if (!(dt > 0.0)) return false;
        const float k = (float)(1.0 / dt);
        const Vec3 d = p - At(ref).p;
        *v = {d.x * k, d.y * k, d.z * k};
        float top = 0.0f;
        for (int i = ref; i + 1 < n_; ++i) {
            const double step = At(i + 1).t - At(i).t;
            if (step > 0.0) {
                const float sp = (float)(Length(At(i + 1).p - At(i).p) / step);
                if (sp > top) top = sp;
            }
        }
        *fastest = top;
        return true;
    }

    Params p_;
    bool throwing_ = false, primed_ = false, blocked_ = false, triggerWas_ = false, haveGood_ = false;
    double lastT_ = 0.0, startT_ = 0.0, goodT_ = 0.0;
    Vec3 lastPos_, startPos_, good_;
    float travel_ = 0.0f, goodSpeed_ = 0.0f;
    Stamp ring_[kRing];
    int start_ = 0, n_ = 0;
    VStamp vring_[kV];
    int vstart_ = 0, vn_ = 0;
};

}  // namespace throwgesture
