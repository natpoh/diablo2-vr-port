// stroke_contact.h - the swing engine: when is a moving hand making a STROKE, and which bodies does the
// blade it carries sweep through. Pure maths: no game calls, no globals, no clock of its own (the caller
// stamps every sample). Used by the melee-by-touch feature (vr/melee_touch.cpp) and meant for any other
// "a held thing touches a body" gesture (a shield thrust is the same engine with a shield-sized blade).
//
// What it does NOT do: decide what a touch does. It reports "this sweep from <before> to <after> belongs to
// stroke N and moves at S m/s"; the caller tests its bodies against the sweep (Touches / TouchTime) and
// keeps HitSet so a body answers once per stroke.
//
// The stroke logic (onset, reversal, recovery, the speed gates), Touches' arc-swept capsule, HitSet and
// PoseHistory are bearstonem/d2r-pcvr's steamvr/melee_contact.h (MIT, Copyright (c) 2026 BodyWalkVR),
// rewritten here: its own small vector maths, the gates in a Params struct, state apart from the numbers,
// TouchTime (the first moment of a touch along a sweep). Numbers are theirs unless a comment says so.
//
// Units: metres, seconds, radians. Poses are a position and a unit quaternion (x y z w).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace stroke {

struct Vec { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline Vec Normalized(Vec a) {
    const float l = Length(a);
    return l > 1e-9f ? a * (1.0f / l) : Vec{};
}
inline bool Finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };
inline Quat Conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
// Hamilton product: a then b is Multiply(b, a); Multiply(now, Conjugate(was)) is the turn from was to now.
inline Quat Multiply(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline float Norm2(Quat q) { return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w; }
// The angle between two orientations, radians 0..pi.
inline float RotationDistance(Quat a, Quat b) {
    const float n = std::sqrt(Norm2(a) * Norm2(b));
    if (!(n > 1e-9f)) return 0.0f;
    return 2.0f * std::acos(std::clamp(std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) / n, 0.0f, 1.0f));
}

struct Pose { Vec p; Quat q; };
// Finite position, and a quaternion that is a rotation (length within 5 % of 1).
inline bool Valid(const Pose& p) {
    const float n = Norm2(p.q);
    return Finite(p.p) && std::isfinite(n) && n > 0.9f && n < 1.1f;
}

// What is swung: a segment from the grip end (base) to the far end (tip), thickened by radius.
struct Blade { Vec base, tip; float radius = 0.04f; };
// What is swept at: a capsule, the segment bottom-top thickened by radius.
struct Body { Vec bottom, top; float radius = 0.3f; };

// The squared distance between the segments p-q and a-b. *sOut (optional): how far along p-q (0 at p, 1 at q) the nearest point lies.
inline float SegmentDistanceSquared(Vec p, Vec q, Vec a, Vec b, float* sOut = nullptr) {
    const Vec d = q - p, e = b - a, r = p - a;
    const float dd = Dot(d, d), ee = Dot(e, e), de = Dot(d, e), dr = Dot(d, r), er = Dot(e, r);
    float s = 0.0f, t = 0.0f;
    if (sOut) *sOut = 0.0f;
    if (dd < 1e-8f && ee < 1e-8f) return Dot(r, r);
    if (dd < 1e-8f) {
        t = std::clamp(er / ee, 0.0f, 1.0f);
    } else if (ee < 1e-8f) {
        s = std::clamp(-dr / dd, 0.0f, 1.0f);
    } else {
        const float denominator = dd * ee - de * de;
        if (denominator > 1e-8f) s = std::clamp((de * er - dr * ee) / denominator, 0.0f, 1.0f);
        t = (de * s + er) / ee;
        if (t < 0.0f) { t = 0.0f; s = std::clamp(-dr / dd, 0.0f, 1.0f); }
        else if (t > 1.0f) { t = 1.0f; s = std::clamp((de - dr) / dd, 0.0f, 1.0f); }
    }
    const Vec delta = r + d * s - e * t;
    if (sOut) *sOut = s;
    return Dot(delta, delta);
}

// When, along the move of the blade from `before` to `after`, it first touches the body: 0 at `before`,
// 1 at `after`, resolution 0.03 m of travel; -1 for never. *along (optional): where on the blade the touch is - the nearest point of the blade's
// segment to the body's axis at that moment, 0 at the grip end (base), 1 at the far end (tip).
//
// The move is swept in steps, the blade rotated about its grip (it keeps its length), not pulled along a
// chord - a chord shortens the blade halfway through a fast slash and misses what stands on its outer arc.
// Each step's reach is widened by half the travel of a step, so a fast blade cannot pass through a body
// between two samples. An exactly opposite pair of directions has no rotation plane: the chord is kept for
// that. A capsule test, not a mesh collision.
inline float TouchTime(const Blade& before, const Blade& after, const Body& body, float* along = nullptr) {
    const Vec a = before.tip - before.base, b = after.tip - after.base;
    const float la = Length(a), lb = Length(b);
    const float angle = la > 0.001f && lb > 0.001f ? std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0f, 1.0f)) : 0.0f;
    const bool arc = angle > 0.001f && angle < 3.13f;
    const float travel = arc ? Length(after.base - before.base) + std::fabs(lb - la) + std::max(la, lb) * angle
                             : std::max(Length(after.base - before.base), Length(after.tip - before.tip));
    const int steps = std::clamp(static_cast<int>(std::ceil(travel / 0.03f)), 1, 64);
    const float radius = body.radius + std::max(before.radius, after.radius) + travel / (2.0f * steps);
    const float sinAngle = arc ? std::sin(angle) : 1.0f;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const Vec base = before.base + (after.base - before.base) * t;
        const Vec tip = arc ? base + (a * (std::sin((1.0f - t) * angle) / (la * sinAngle)) +
                                      b * (std::sin(t * angle) / (lb * sinAngle))) * (la + (lb - la) * t)
                            : before.tip + (after.tip - before.tip) * t;
        float s = 0.0f;
        if (SegmentDistanceSquared(base, tip, body.bottom, body.top, &s) <= radius * radius) {
            if (along) *along = std::clamp(s, 0.0f, 1.0f);
            return t;
        }
    }
    return -1.0f;
}
inline bool Touches(const Blade& before, const Blade& after, const Body& body) { return TouchTime(before, after, body) >= 0.0f; }

// One reading of the swinging hand and the blade it carries.
struct Sample {
    double time = 0.0;        // seconds, one clock for all samples
    bool active = false;      // false: the hand does not swing anything now (menu, nothing held, no tracking)
    // Whatever turns the blade's frame under a still hand (a snap turn, a warp, a view change) or changes what
    // is swung (another weapon, another hand) restarts the stroke: epoch for the frame, context for the held thing.
    uint64_t epoch = 0;
    uint64_t context = 0;
    // The hand in the TRACKING frame (the room), not the world: the world turning or the hero walking must not
    // be able to power a stroke.
    Pose hand;
    // The blade in the world frame the bodies are in.
    Blade blade;
    // Where the wearer stands in that frame: a blade far from it, or a wearer that jumps, is a bad sample.
    Vec anchor;
    // World units per metre of the blade's frame, when the frame scales (checked against Params).
    float scale = 1.0f;
    // The lever of a turn of the hand, metres: how far the blade's far end swings for a turn of the hand. < 0: the blade's own length
    // (grip end to tip). A blade that runs on behind the grip (a staff's butt) says the tip side only, so the stroke's speed and onset are
    // those of the tip, not of the whole length.
    float lever = -1.0f;
    // The blade's two ends in the ROOM frame, as DRAWN (not the controller's pose): the same frame as `hand`, so the world turning or the hero walking
    // cannot power a stroke here either. With it (haveRoom) the stroke's speed and onset are those of the BLADE - the faster of its two ends - not
    // the hand's: the hand's movement plus its turn times a lever says how fast a rigid blade MIGHT be, the drawn blade says how fast it IS (a grip
    // at the root or in the middle, a big arc with a still wrist, a weapon that lags or pivots under the hand). Without it (a caller that has no
    // such frame, the shield's thrust) the hand's way, as ever.
    Blade room;
    bool haveRoom = false;
};

struct Params {
    float strokeSpeed = 0.8f;        // m/s: a stroke touches only while the hand moves this fast (a held blade inside a monster does not)
    float beginSpeed = 1.2f;         // m/s that may begin a stroke
    float quietSpeed = 0.45f;        // m/s below which the hand is at rest
    double quietTime = 0.08;         // s at rest and a stroke is over
    float onsetExcursion = 0.08f;    // m the hand (its rotation weighted by the blade's length) must have moved ...
    double onsetWindow = 0.12;       // ... within this many seconds, ...
    float onsetStraight = 0.60f;     // ... and at least this part of its path must be net movement (not a wobble)
    double reverseAfter = 0.18;      // s after a stroke began that a reversal may end it
    float reverseDot = -0.55f;       // a step whose direction against the stroke's is below this reverses it
    float maxAngularSpeed = 60.0f;   // rad/s: a faster turn is a tracking glitch, the stroke is dropped (real wrist-led slashes pass 35)
    double recover = 0.08;           // s of fresh samples wanted after a drop before a stroke may begin
    double maxStep = 0.1;            // s between samples; a longer gap is a drop
    float handStepMin = 0.12f;       // m a sample may move the hand at least, whatever the time ...
    float handSpeedMax = 10.0f;      // ... and m/s above that: a faster step is a jump
    float anchorStepMax = 0.5f;      // m the wearer may move between samples
    float baseFromAnchorMax = 2.8f;  // m from the wearer to the grip
    float tipFromAnchorMax = 4.5f;   // m from the wearer to the tip
    float bladeStepMin = 1.2f;       // m a blade end may move at least between samples (more with a long lever)
    float radiusMax = 0.2f;
    float scaleMin = 1.0f, scaleMax = 12.0f, scaleStep = 0.05f;   // the frame's scale: bounds and the change that is a drop
    float leverMax = 1.5f;           // m: the blade's length counts as the lever of a rotation up to this
};

// The sweep a stroke has made since the last one: test the bodies against it. speed: the stroke's speed this step (the blade's fastest end, or the
// hand's way - see Sample::room). baseVel / tipVel (haveVel): the blade's two ends' velocities in the room frame, m/s, this step - what ContactSpeed
// turns into the speed of the point of the blade that touches.
struct Sweep {
    Blade before, after;
    uint64_t stroke = 0;
    float speed = 0.0f;
    Vec baseVel, tipVel;
    bool haveVel = false;
};

// How fast the blade moves where it touches: its velocity at `along` (0 the base, 1 the tip - TouchTime's), the two ends' velocities blended. A
// rigid blade's velocity is linear along it, so a touch with the grip end of a blade that swings about the hand is slow and one with its tip is
// fast. Without the ends' velocities (the hand's way) the stroke's own speed.
inline float ContactSpeed(const Sweep& sw, float along) {
    if (!sw.haveVel) return sw.speed;
    const float k = std::clamp(along, 0.0f, 1.0f);
    return Length(sw.baseVel * (1.0f - k) + sw.tipVel * k);
}

// What a sweep makes of one body: when it first touches it (TouchTime; < 0 never), where on the blade, and how fast the blade moves there.
struct Contact {
    float t = -1.0f, along = 0.0f, speed = 0.0f;
};
inline Contact TouchOf(const Sweep& sw, const Body& body) {
    Contact c;
    c.t = TouchTime(sw.before, sw.after, body, &c.along);
    if (c.t >= 0.0f) c.speed = ContactSpeed(sw, c.along);
    return c;
}
struct Diagnostics { unsigned discontinuities = 0, recovering = 0; };

class Engine {
public:
    explicit Engine(const Params& p = Params{}) : p_(p) {}
    void SetParams(const Params& p) { p_ = p; }
    const Params& GetParams() const { return p_; }
    // Forget the stroke and wait for fresh samples. Stroke numbers go on counting, so a HitSet never
    // takes an old stroke's number for a new one.
    void Reset() { st_ = State{}; lastSpeed_ = 0.0f; counting_ = false; }
    Diagnostics TakeDiagnostics() { const Diagnostics d = diag_; diag_ = {}; return d; }
    bool InStroke() const { return st_.stroke != 0; }
    // What the LATEST Step made of the hand: the very speed its gates use ((travel + turn x lever) / dt, 0 when the step got no
    // speed - a drop, a recovery, a reset) and whether that step swept anything, i.e. whether a contact would count right now (a
    // stroke begun and the speed at or above Params::strokeSpeed). A repeated sample leaves both as they were.
    float LastSpeed() const { return lastSpeed_; }
    bool Counting() const { return counting_; }
    // The number of the stroke that is on (what Sweep::stroke says), 0 when none: a stroke ends at a rest, a reversal, a drop.
    uint64_t StrokeId() const { return st_.stroke; }
    // When the stroke that is on began (the sample time its onset was judged at; the wind-up the onset looked at lies up to
    // Params::onsetWindow before it), 0 when none. Read only ([gameplay] melee_direct_client: the hands' paths in this swing).
    double StrokeStart() const { return st_.stroke ? st_.strokeAt : 0.0; }

    // Feeds one sample. True with `out` filled when the stroke has swept something this step.
    bool Step(const Sample& in, Sweep& out) {
        out = {};
        if (!in.active || !Valid(in.hand) || !std::isfinite(in.time) || !Finite(in.blade.base) || !Finite(in.blade.tip) ||
            !Finite(in.anchor) || !std::isfinite(in.scale) || in.scale < p_.scaleMin || in.scale > p_.scaleMax ||
            Length(in.blade.base - in.anchor) > p_.baseFromAnchorMax || Length(in.blade.tip - in.anchor) > p_.tipFromAnchorMax ||
            !std::isfinite(in.blade.radius) || in.blade.radius <= 0.0f || in.blade.radius > p_.radiusMax) {
            Reset();
            return false;
        }
        State& s = st_;
        if (s.primed && in.time == s.previous.time) return false;   // the same sample again (two eyes' passes pose one stamp)
        lastSpeed_ = 0.0f;
        counting_ = false;
        const double dt = in.time - s.previous.time;
        const float angle = RotationDistance(in.hand.q, s.previous.hand.q);
        const float lever = std::min(p_.leverMax, in.lever >= 0.0f ? in.lever : Length(in.blade.tip - in.blade.base));
        const float handTravel = Length(in.hand.p - s.previous.hand.p), anchorTravel = Length(in.anchor - s.previous.anchor);
        const float bladeTravel = std::max(Length(in.blade.base - s.previous.blade.base), Length(in.blade.tip - s.previous.blade.tip));
        if (!s.primed || dt <= 0.0 || dt > p_.maxStep || in.epoch != s.previous.epoch || in.context != s.previous.context ||
            handTravel > std::max(p_.handStepMin, p_.handSpeedMax * static_cast<float>(dt)) ||
            angle > p_.maxAngularSpeed * static_cast<float>(dt) || anchorTravel > p_.anchorStepMax ||
            std::fabs(in.scale - s.previous.scale) > p_.scaleStep ||
            bladeTravel > std::max(p_.bladeStepMin, handTravel + angle * lever + anchorTravel + 0.1f)) {
            if (s.primed) ++diag_.discontinuities;
            Reset();
            s.primed = true;
            s.previous = in;
            RestartPath(in);
            s.recoverUntil = in.time + p_.recover;
            return false;
        }
        const Sample was = s.previous;
        s.previous = in;
        // After a drop a short run of fresh samples is wanted, not a motionless hand: real slashes at ~44 Hz
        // crossed the old 35 rad/s / 18 m/s guards, and a quiet-hand re-arm was then never met (pcvr).
        if (in.time < s.recoverUntil) {
            ++diag_.recovering;
            RestartPath(in);
            return false;
        }
        // The stroke's movement this step. By the BLADE when the caller gave its ends in the room (Sample::room): the faster end's travel is the
        // distance and its way the direction - the tip of a sword, either end of a staff; else the hand's travel plus its turn times the lever.
        const bool byBlade = in.haveRoom && was.haveRoom;
        const Vec baseDelta = byBlade ? in.room.base - was.room.base : Vec{}, tipDelta = byBlade ? in.room.tip - was.room.tip : Vec{};
        Vec delta = in.hand.p - was.hand.p;
        float distance = Length(delta) + angle * lever;
        if (byBlade) {
            const float tipStep = Length(tipDelta), baseStep = Length(baseDelta);
            distance = std::max(tipStep, baseStep);
            delta = tipStep >= baseStep ? tipDelta : baseDelta;
        }
        const float speed = distance / static_cast<float>(dt);
        lastSpeed_ = speed;
        if (speed < p_.quietSpeed) {
            if (!s.quiet) { s.quiet = true; s.quietSince = in.time; }
            Remember(in, distance);
            if (in.time - s.quietSince >= p_.quietTime) EndStroke(in);
            return false;
        }
        s.quiet = false;
        // Translation AND rotation reverse a stroke: a wrist-led backhand keeps the hand nearly in place and
        // would otherwise stay the first stroke for good, muting every later hit on the same body (pcvr).
        const Quat dq = Multiply(in.hand.q, Conjugate(was.hand.q));
        const Vec angular = Normalized(Vec{dq.x, dq.y, dq.z} * (dq.w < 0.0f ? -1.0f : 1.0f));
        const bool reversed = (Length(delta) > 0.008f && Dot(Normalized(delta), s.direction) < p_.reverseDot) ||
                              (angle > 0.025f && Dot(angular, s.angularDirection) < p_.reverseDot);
        if (s.stroke && in.time - s.strokeAt > p_.reverseAfter && reversed) {
            s.stroke = 0;
            RestartPath(was);
            s.direction = {};
            s.angularDirection = {};
        }
        Remember(in, distance);
        const bool beginning = !s.stroke;
        Blade startBlade;
        if (beginning) {
            // Only this stroke's own recent movement is judged: adding up every jitter and aborted wind-up since
            // the last pause made the net/path ratio impossible and locked the engine out for good (pcvr).
            if (speed < p_.beginSpeed || !Onset(in, lever, startBlade)) return false;
            s.stroke = ++serial_;
            s.strokeAt = in.time;
            s.direction = Normalized(delta);
            s.angularDirection = angular;
        }
        // A blade held still inside an enemy does not strike again, and a camera turning slowly under a still
        // hand does not either.
        if (speed < p_.strokeSpeed) return false;
        out = {beginning ? startBlade : was.blade, in.blade, s.stroke, speed};
        if (byBlade) {
            out.baseVel = baseDelta * (1.0f / static_cast<float>(dt));
            out.tipVel = tipDelta * (1.0f / static_cast<float>(dt));
            out.haveVel = true;
        }
        counting_ = true;
        return true;
    }

private:
    struct Point { Pose hand; Blade blade; Blade room; bool haveRoom = false; double time = 0.0; float distance = 0.0f; };
    struct State {
        bool primed = false, quiet = false;
        Sample previous;
        double quietSince = 0.0, strokeAt = 0.0, recoverUntil = 0.0;
        Vec direction, angularDirection;
        uint64_t stroke = 0;
        std::array<Point, 32> recent{};
        unsigned next = 0, points = 0;
    };

    void Remember(const Sample& s, float distance) {
        State& st = st_;
        st.recent[st.next] = {s.hand, s.blade, s.room, s.haveRoom, s.time, distance};
        st.next = (st.next + 1) % static_cast<unsigned>(st.recent.size());
        st.points = std::min(st.points + 1, static_cast<unsigned>(st.recent.size()));
    }
    void RestartPath(const Sample& s) { st_.next = st_.points = 0; Remember(s, 0.0f); }
    void EndStroke(const Sample& at) {
        st_.stroke = 0;
        RestartPath(at);
        st_.direction = {};
        st_.angularDirection = {};
    }
    // Did the hand move far enough, and straight enough, in the last onsetWindow? `from` is the blade where
    // that movement began: the first sweep of the stroke covers the wind-up too. With the blade's ends in the room (Sample::room) it is
    // the blade that must have moved - the faster end's excursion - so a big arc counts by its tip and a still blade does not, whatever the grip.
    bool Onset(const Sample& s, float lever, Blade& from) const {
        const State& st = st_;
        const unsigned cap = static_cast<unsigned>(st.recent.size());
        float travel = 0.0f;
        for (unsigned back = 1; back < st.points; ++back) {
            travel += st.recent[(st.next + cap - back) % cap].distance;
            const Point& p = st.recent[(st.next + cap - back - 1) % cap];
            if (s.time - p.time > p_.onsetWindow) break;
            const float excursion = s.haveRoom && p.haveRoom ? std::max(Length(s.room.tip - p.room.tip), Length(s.room.base - p.room.base))
                                                             : Length(s.hand.p - p.hand.p) + lever * RotationDistance(s.hand.q, p.hand.q);
            if (excursion >= p_.onsetExcursion && excursion >= travel * p_.onsetStraight) {
                from = p.blade;
                return true;
            }
        }
        return false;
    }

    Params p_;
    State st_;
    uint64_t serial_ = 0;
    Diagnostics diag_;
    float lastSpeed_ = 0.0f;
    bool counting_ = false;
};

// Which (stroke, body) pairs have answered: a body answers once per stroke. `session` names the game the ids
// belong to; another session forgets everything.
class HitSet {
public:
    bool Contains(uint64_t session, uint64_t stroke, uint32_t id) const {
        if (epoch_ != session) return false;
        for (const Entry& e : entries_) if (e.stroke == stroke && e.id == id) return true;
        return false;
    }
    // False when the pair was in already.
    bool Insert(uint64_t session, uint64_t stroke, uint32_t id) {
        if (epoch_ != session) { entries_ = {}; next_ = 0; epoch_ = session; }
        if (Contains(session, stroke, id)) return false;
        entries_[next_++ % entries_.size()] = {stroke, id};
        return true;
    }
    void Clear() { *this = HitSet{}; }

private:
    struct Entry { uint64_t stroke = 0; uint32_t id = 0; };
    uint64_t epoch_ = 0;
    std::array<Entry, 64> entries_{};
    unsigned next_ = 0;
};

// A controller's own path ([gameplay] melee_direct_client's swing length): its positions in the room as they come, one a headset
// frame, and how far it moved - its translation only, so a turn of the wrist that sweeps a long blade a long way adds nothing - since
// a time. Not thread safe - the caller serialises Push and Since.
class HandPath {
public:
    static constexpr unsigned kCap = 256;   // about 2 s at 120 Hz
    void Clear() { *this = HandPath{}; }
    void Push(double t, const Vec& p) {
        if (!std::isfinite(t) || !Finite(p)) return;
        e_[next_] = {t, p};
        next_ = (next_ + 1) % kCap;
        n_ = std::min(n_ + 1, kCap);
    }
    // The path from `since` to the newest sample, m (a gap over maxGap between two samples - tracking lost - ends it there).
    float Since(double since, double maxGap = 0.1) const {
        float path = 0.0f;
        for (unsigned age = 1; age < n_; ++age) {
            const E& b = e_[(next_ + kCap - age) % kCap];
            const E& a = e_[(next_ + kCap - age - 1) % kCap];
            if (a.t < since || b.t - a.t > maxGap || b.t < a.t) break;
            path += Length(b.p - a.p);
        }
        return path;
    }
private:
    struct E { double t = 0.0; Vec p; };
    std::array<E, kCap> e_{};
    unsigned next_ = 0, n_ = 0;
};

// Samples kept by the stamp of the pose they belong to. A finished body pose can arrive after a newer hand
// sample has: the blade is matched to the hand that really posed it, never to the latest hand velocity.
// Not thread safe - the caller serialises Push and Find.
class PoseHistory {
public:
    void Clear() { *this = PoseHistory{}; }
    void Push(uint32_t stamp, const Sample& sample) {
        entries_[next_] = {stamp, sample};
        next_ = (next_ + 1) % static_cast<unsigned>(entries_.size());
        count_ = std::min(count_ + 1, static_cast<unsigned>(entries_.size()));
    }
    // The sample pushed with `stamp`, if it is still believable at `now`: every sample is fresher than
    // maxAge, active, and of the same epoch and context as the newest one.
    bool Find(uint32_t stamp, double now, Sample& out, double maxAge = 0.1) const {
        if (!count_ || !std::isfinite(now)) return false;
        const unsigned cap = static_cast<unsigned>(entries_.size());
        const Sample& current = entries_[(next_ + cap - 1) % cap].sample;
        if (!current.active || now < current.time || now - current.time > maxAge) return false;
        for (unsigned age = 0; age < count_; ++age) {
            const Entry& e = entries_[(next_ + cap - 1 - age) % cap];
            if (e.stamp != stamp) continue;
            if (!e.sample.active || e.sample.epoch != current.epoch || e.sample.context != current.context || now < e.sample.time ||
                now - e.sample.time > maxAge)
                return false;
            out = e.sample;
            return true;
        }
        return false;
    }

private:
    struct Entry { uint32_t stamp = 0; Sample sample; };
    std::array<Entry, 16> entries_{};
    unsigned next_ = 0, count_ = 0;
};

}  // namespace stroke
