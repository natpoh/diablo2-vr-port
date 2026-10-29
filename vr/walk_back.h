// walk_back.h - [body] walk_back_reverse: the game's own walk PLAYED BACKWARDS for our hero walking backwards (the pure part; skeletons.cpp WalkBackLegs is
// the game's side, cleanroom/tests/walk_back_test.cpp drives this).
//
// The game has no backward walk. Turned whole to the look while it walks him the other way, his legs walked forward while he went back (the scissor of the
// 2026-10-27 report); 0.185's walk_back mirrored each foot's place front to back (legik::MirrorAlong) and Serg's 2026-10-28 test still saw them cross: a
// forward walk mirrored is the walk of a hero facing the other way with his legs on the wrong sides - the foot lifted high IN FRONT of him (the forward
// walk's heel kick behind, reflected), set down behind him toes first up (the forward walk's heel strike, reflected), only the knees solved round again.
// A forward walk played backwards in time IS a backward walk: the planted foot stands on the ground as he goes back, the swing lifts the heel behind him
// and the toes reach back to land; every joint - hip, knee, ankle, toes - keeps the game's own shape, only the order of the frames is turned round.
//
// The game's lower body (pelvis and legs, in the body's own frame: the body's turn taken out) is kept from each pose while he walks - any way: the game
// always plays its forward walk - the newest kKeepS of it, kept through his standing. While he walks back the pose drawn is taken from a cursor going BACK
// through that store as fast as time goes forward. The walk repeats, so where the cursor would leave the stretch of walking it is in (its start, where
// he stood before, or what was dropped), it jumps to the kept pose most like the one it drew last - the feet at the same places, going the same ways
// (Match) - and goes back again from there: no seam, whatever his speed or stride, and no animation clock read from the game. A walk back from standing
// starts on the walk kept before it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace walkback {

struct M4 { float m[16]; };   // row vectors, as skeletons.cpp's Mat: v' = v M, the place in m[12..14]

inline M4 Mul(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j] + a.m[i * 4 + 3] * b.m[12 + j];
    return r;
}

// A bone's turn as a unit quaternion (x y z w) and its rows' sizes (a bone's matrix may carry a size, or a mirror: then the first row's size is negative).
inline void Split(const M4& a, float q[4], float size[3]) {
    float r[3][3];
    for (int i = 0; i < 3; ++i) {
        const float l = std::sqrt(a.m[i * 4] * a.m[i * 4] + a.m[i * 4 + 1] * a.m[i * 4 + 1] + a.m[i * 4 + 2] * a.m[i * 4 + 2]);
        size[i] = l > 1e-12f ? l : 1.0f;
        for (int j = 0; j < 3; ++j) r[i][j] = a.m[i * 4 + j] / size[i];
    }
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.0f) {
        size[0] = -size[0];
        for (int j = 0; j < 3; ++j) r[0][j] = -r[0][j];
    }
    const float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s; q[0] = (r[2][1] - r[1][2]) / s; q[1] = (r[0][2] - r[2][0]) / s; q[2] = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q[3] = (r[2][1] - r[1][2]) / s; q[0] = 0.25f * s; q[1] = (r[0][1] + r[1][0]) / s; q[2] = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q[3] = (r[0][2] - r[2][0]) / s; q[0] = (r[0][1] + r[1][0]) / s; q[1] = 0.25f * s; q[2] = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q[3] = (r[1][0] - r[0][1]) / s; q[0] = (r[0][2] + r[2][0]) / s; q[1] = (r[1][2] + r[2][1]) / s; q[2] = 0.25f * s;
    }
}
inline void Join(const float q[4], const float size[3], M4& out) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out.m[i * 4 + j] = r[i][j] * size[i];
        out.m[i * 4 + 3] = 0.0f;
    }
    out.m[15] = 1.0f;
}

// a to b, w of the way (0 a, 1 b): the place along the line, the turn by the shorter way (normalised), the sizes between.
inline M4 Blend(const M4& a, const M4& b, float w) {
    w = std::clamp(std::isfinite(w) ? w : 0.0f, 0.0f, 1.0f);
    if (w <= 0.0f) return a;
    if (w >= 1.0f) return b;
    float qa[4], qb[4], sa[3], sb[3], q[4], s[3];
    Split(a, qa, sa);
    Split(b, qb, sb);
    const float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    const float k = d < 0.0f ? -1.0f : 1.0f;
    float l = 0.0f;
    for (int i = 0; i < 4; ++i) { q[i] = qa[i] * (1.0f - w) + qb[i] * k * w; l += q[i] * q[i]; }
    l = std::sqrt(l);
    if (!(l > 1e-6f)) return w < 0.5f ? a : b;
    for (int i = 0; i < 4; ++i) q[i] /= l;
    for (int i = 0; i < 3; ++i) s[i] = sa[i] * (1.0f - w) + sb[i] * w;
    M4 r{};
    Join(q, s, r);
    for (int i = 12; i < 15; ++i) r.m[i] = a.m[i] * (1.0f - w) + b.m[i] * w;
    return r;
}

// The game's own turn inside the skeleton. When the hero changes his way (Serg's walk back: forward, then back) the game turns his model matrix at once,
// half a turn, and plays the turn itself on the skeleton's "anim" bone (the pelvis's parent): it starts turned back by as much and comes round over
// ~0.1-0.2 s (his 2026-10-28 17:03 log: "anim 90.0 / 128.1 / 121.2 / -95.7" deg at the flips, 0 between). PoseBody turns the body by the matrix's
// facing only, so for that moment the drawn legs faced the way he goes - backwards under a trunk that [body] lock holds to the look (left leg on the
// right: crossed) - and walk_back_reverse kept those poses and began every walk back by playing them. TurnAbout takes such a turn out.
inline float YawOf(const M4& a) { return std::atan2(a.m[8], a.m[10]); }   // radians; 0 = its z row along +z (skeletons.cpp's T(c, s): atan2(s, c))
// b turned by `yaw` (skeletons.cpp's T: rows times the turn) about the vertical through (px, pz).
inline void TurnAbout(M4& b, float yaw, float px, float pz) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    for (int r = 0; r < 4; ++r) {
        float x = b.m[r * 4], z = b.m[r * 4 + 2];
        if (r == 3) { x -= px; z -= pz; }
        b.m[r * 4] = x * c + z * s;
        b.m[r * 4 + 2] = -x * s + z * c;
        if (r == 3) { b.m[12] += px; b.m[14] += pz; }
    }
}
// The angle (radians, -pi .. pi) from b to a.
inline float WrapAngle(float a) {
    constexpr float kTwoPi = 6.28318531f;
    a = std::fmod(a + 3.14159265f, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    return a - 3.14159265f;
}

// [body] walk_back: the hero's travel is read off his place frame to frame; a step faster than this is not a walk but a jump of his place (an area change,
// a waypoint, a town portal: Serg's log moved him 11511 units in one frame) - the smoothed travel then took ~1.5 s to die away and the legs "walked back"
// while he stood (two 5 s windows of his log: 137 and 141 poses walking back, all in the game's standing mode). Such a step starts the travel again.
constexpr float kJumpMps = 40.0f;
// The game's own walk or run is on (Diablo II's unit modes: 2 walk, 3 run, 6 town walk; mode < 0: not known - then the speed alone says). Serg's
// 2026-10-28 17:00 log: standing (mode 1, in town 5) the rig's place still read a speed now and then, any way, and whenever it pointed more than 100 deg
// from his facing the walk back came on - the walk played backwards under a hero standing still. He goes only while the game walks him.
inline bool GameWalkMode(int mode) { return mode < 0 || mode == 2 || mode == 3 || mode == 6; }
// The game says he stands (a known mode, not a walk): the cone's hold through no speed is not wanted - it is for a turn from forward to back, which the
// game walks through.
inline bool GameStands(int mode) { return mode >= 0 && !GameWalkMode(mode); }

// One pose of the game's walk, as the game drew it.
struct Sample {
    double t = 0.0;          // seconds
    int seg = 0;             // the stretch of walking it is from (a stop starts another)
    double t0 = 0.0;         // when that stretch began
    float dt = 0.0f;         // since the pose before it in its stretch (0: the first of it)
    float feet[6] = {};      // both feet's places (the pose's own frame): what Match compares
    std::vector<M4> pose;    // the lower body's bones
};

class Reverser {
public:
    static constexpr int kCap = 1024;           // poses at most
    static constexpr double kKeepS = 2.5;       // this much of his walking kept (the newest; kept through his standing - the next walk back starts on it)
    static constexpr double kMinJumpS = 0.25;   // a jump within one stretch lands at least this much later than the pose it leaves (no small loops)
    static constexpr float kSpeedS = 0.1f;      // in Match a foot's speed counts as the way it goes in this long
    // Serg's 2026-10-28 log of the first build: 108-114 jumps in 5 s of walking back (~170 poses) and, after his first walks, 0.1 s of walk kept - what
    // was taken as walking broke up every few frames (the speed read off the rig's place at the game's own 25 a second, or its mode, dipping for a frame),
    // so each stretch was a frame or two long and the cursor left it - and jumped - nearly every frame: the legs dithered between near poses instead of
    // walking back. Now a stretch goes on through such a dip (kGraceS; if it was a real stop, the poses of the dip are taken out again), and a jump only
    // lands where it can go back at least kMinBackS within its stretch.
    static constexpr double kGraceS = 0.25;     // not walking this long or less: the stretch goes on (the poses kept); longer: it ended where he last walked
    static constexpr double kMinBackS = 0.15;   // a jump lands only on a pose with this much of its stretch before it

    void Reset() { n_ = head_ = 0; on_ = false; jumps_ = 0; walkS_ = 0.0; open_ = false; }
    int Kept() const { return n_; }
    double KeptS() const { return walkS_; }
    int TakeJumps() { const int j = jumps_; jumps_ = 0; return j; }

    // The game's own pose at t (count bones). Only his walking is kept: standing (walking false) for more than kGraceS ends a stretch where he last
    // walked. A pose of another size than those kept starts the store again (another rig).
    void Record(double t, bool walking, const M4* pose, int count, const float feet[6]) {
        if (!std::isfinite(t) || count <= 0) return;
        if (!walking) {
            if (!open_) return;
            if (t - lastWalkT_ > kGraceS) {   // a stop: the poses since he last walked out again, the stretch over
                while (n_ > 0 && At(n_ - 1).t > lastWalkT_) {
                    walkS_ -= At(n_ - 1).dt;
                    head_ = (head_ + kCap - 1) % kCap;
                    --n_;
                }
                open_ = false;
                return;
            }
        }
        if (n_ && (int)At(n_ - 1).pose.size() != count) Reset();
        if (n_ && t <= At(n_ - 1).t) return;   // (the same time again)
        const bool same = n_ && open_;
        const int seg = same ? At(n_ - 1).seg : (n_ ? At(n_ - 1).seg + 1 : 0);
        const double t0 = same ? At(n_ - 1).t0 : t;
        const float dt = same ? (float)(t - At(n_ - 1).t) : 0.0f;
        if (n_ == kCap) Drop();   // (its slot is the one written now)
        Sample& s = ring_[head_];
        s.t = t;
        s.seg = seg;
        s.t0 = t0;
        s.dt = dt;
        std::memcpy(s.feet, feet, sizeof s.feet);
        s.pose.assign(pose, pose + count);
        head_ = (head_ + 1) % kCap;
        ++n_;
        walkS_ += s.dt;
        open_ = true;
        if (walking) lastWalkT_ = t;
        while (n_ > 1 && walkS_ > kKeepS) Drop();
    }

    // The pose to draw at this call (dt since the last), the game's walk played backwards; nullptr: nothing kept. active false: let go - the next start
    // is from the newest pose (the game's own of that moment, so it starts without a jump).
    // Between two kept poses the one drawn is blended from both at the cursor's time (the frames come at their own times, not the game's).
    const Sample* Step(double dt, bool active) {
        if (!active || !n_) { on_ = false; return nullptr; }
        int i = n_ - 1;
        float frac = 0.0f;   // of the way from pose i to pose i + 1
        if (!on_) {
            on_ = true;
            cursor_ = At(i).t;
        } else {
            const double step = std::clamp(std::isfinite(dt) ? dt : 0.0, 0.0, 0.25);
            cursor_ -= step;
            i = Before(cursor_);
            // out of the stretch it was in (its start passed, or dropped from the store): on from the kept pose most like the one drawn last - one frame
            // back from it, as from the one drawn
            if (i < 0 || At(i).seg != lastSeg_) {
                const int to = Match(last_, lastSpeed_, lastSeg_, lastT_ + kMinJumpS);
                if (to < 0) { on_ = false; return nullptr; }   // (no stretch long enough kept yet)
                cursor_ = Between(to, last_) - step;
                i = Before(cursor_);
                if (i < 0 || At(i).seg != At(to).seg) { i = to; cursor_ = At(to).t; }
                ++jumps_;
            }
            if (cursor_ > At(i).t && i + 1 < n_ && At(i + 1).seg == At(i).seg && At(i + 1).t > At(i).t)
                frac = (float)std::clamp((cursor_ - At(i).t) / (At(i + 1).t - At(i).t), 0.0, 1.0);
        }
        const Sample& a = At(i);
        out_.t = cursor_;
        out_.seg = a.seg;
        out_.dt = 0.0f;
        out_.pose.resize(a.pose.size());
        if (frac > 0.0f) {
            const Sample& b = At(i + 1);
            for (size_t k = 0; k < a.pose.size(); ++k) out_.pose[k] = Blend(a.pose[k], b.pose[k], frac);
            for (int j = 0; j < 6; ++j) out_.feet[j] = a.feet[j] + (b.feet[j] - a.feet[j]) * frac;
        } else {
            out_.pose = a.pose;
            std::memcpy(out_.feet, a.feet, sizeof out_.feet);
        }
        lastT_ = cursor_;
        lastSeg_ = a.seg;
        std::memcpy(last_, out_.feet, sizeof last_);
        Speed(i, lastSpeed_);
        return &out_;
    }

    // The kept pose most like feet / speed (both feet at the same places, going the same ways), with kMinBackS of its stretch before it; in stretch `seg`
    // only later than `after` (-1: none).
    int Match(const float feet[6], const float speed[6], int seg, double after) const {
        float v[6];
        int best = -1;
        float bestD = 0.0f;
        for (int k = 0; k < n_; ++k) {
            const Sample& b = At(k);
            if ((b.seg == seg && b.t <= after) || b.t - b.t0 < kMinBackS) continue;
            Speed(k, v);
            float d = 0.0f;
            for (int j = 0; j < 6; ++j) {
                const float dp = b.feet[j] - feet[j], dv = (v[j] - speed[j]) * kSpeedS;
                d += dp * dp + dv * dv;
            }
            if (best < 0 || d < bestD) { best = k; bestD = d; }
        }
        return best;
    }

    const Sample& At(int i) const { return ring_[(head_ - n_ + i + 2 * kCap) % kCap]; }   // 0 the oldest .. Kept() - 1 the newest

private:
    std::vector<Sample> ring_ = std::vector<Sample>(kCap);
    int head_ = 0, n_ = 0;   // head_: the slot the next pose goes in
    double walkS_ = 0.0;     // the walking time kept (each pose's dt after the first of its stretch)
    bool open_ = false;      // a stretch is going on (the next pose goes on it)
    double lastWalkT_ = 0.0; // the last pose kept as walking
    bool on_ = false;
    double cursor_ = 0.0, lastT_ = 0.0;
    int lastSeg_ = -1;
    float last_[6] = {}, lastSpeed_[6] = {};   // the pose drawn last (kept here: it may be dropped from the store)
    int jumps_ = 0;
    Sample out_;   // the pose drawn

    void Drop() {   // the oldest pose out
        if (n_ > 1 && At(1).seg == At(0).seg) walkS_ -= At(1).dt;
        --n_;
    }
    double Between(int k, const float feet[6]) const {   // the time by pose k, between it and a neighbour in its stretch, where the feet come nearest feet
        auto dist = [&](const float* a, const float* b, float u) {
            float d = 0.0f;
            for (int j = 0; j < 6; ++j) { const float e = a[j] + (b[j] - a[j]) * u - feet[j]; d += e * e; }
            return d;
        };
        double best = At(k).t;
        float bestD = dist(At(k).feet, At(k).feet, 0.0f);
        for (int a = k - 1; a <= k; ++a) {
            if (a < 0 || a + 1 >= n_ || At(a).seg != At(a + 1).seg) continue;
            const float* p = At(a).feet;
            const float* q = At(a + 1).feet;
            float num = 0.0f, den = 0.0f;
            for (int j = 0; j < 6; ++j) { num += (feet[j] - p[j]) * (q[j] - p[j]); den += (q[j] - p[j]) * (q[j] - p[j]); }
            const float u = den > 1e-12f ? std::clamp(num / den, 0.0f, 1.0f) : 0.0f;
            const float d = dist(p, q, u);
            if (d < bestD) { bestD = d; best = At(a).t + u * (At(a + 1).t - At(a).t); }
        }
        return best;
    }
    int Before(double t) const {   // the last kept pose at or before t (-1: none; times rise with the index)
        int lo = 0, hi = n_;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (At(mid).t <= t) lo = mid + 1; else hi = mid;
        }
        return lo - 1;
    }
    void Speed(int i, float v[6]) const {   // the feet's speeds at pose i, from its neighbours in its stretch
        int a = i, b = i;
        if (i > 0 && At(i - 1).seg == At(i).seg) a = i - 1;
        if (i + 1 < n_ && At(i + 1).seg == At(i).seg) b = i + 1;
        const double dt = At(b).t - At(a).t;
        for (int j = 0; j < 6; ++j) v[j] = dt > 1e-6 ? (float)((At(b).feet[j] - At(a).feet[j]) / dt) : 0.0f;
    }
};

}  // namespace walkback
