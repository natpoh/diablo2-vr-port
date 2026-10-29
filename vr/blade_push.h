#pragma once
// blade_push.h - [weight] monster_ragdoll: the push a drawn blade gives the monsters' bodies when the weight model does not hold it - a weapon in each hand
// (the weight model holds a weapon held alone: vrcam's guard; two weapons: none). Each blade strikes the bodies it enters on its own: the push the weight model's
// strike gives (vr/weight.cpp: the steel and the arm behind it times the speed in, along the way in), on the body it entered, at the point it met. Pure: hit by
// touch hands it the blades as drawn (render world, metres) and the bodies, and takes the pushes to the doll (meleetouch::PushBody).
//
// The headset 2026-10-23: «когда у варвара в руках два оружия, они не действуют на куклу»; «оружие в двух руках - кукла прозрачная; оружие в левой руке - кукла
// прозрачная; оружие в правой руке - кукла работает».

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bladepush {

constexpr float kArmKg = 3.5f;          // the forearm and hand behind the weapon (vr/weight.cpp kStrikeArmKg)
constexpr float kInMs = 0.3f;           // m/s into the body: slower is resting on it, no strike
constexpr float kLongestStepS = 0.1f;   // a gap longer than this between two poses: no speed from it
constexpr int kMaxOn = 8;               // bodies a blade is on at once

struct V { float x = 0, y = 0, z = 0; };
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Mul(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }

struct Blade { V base, tip; float radius = 0.02f; };
struct Body { V bottom, top; float radius = 0.1f; uint32_t id = 0; int bone = -1; };
struct Push { int body = -1; V impulse, point; float speed = 0; };   // N s into the body at point (metres), the speed in (m/s)

// The closest points of the segments p0-p1 and q0-q1 (s, t along them).
inline void Closest(V p0, V p1, V q0, V q1, float* s, float* t) {
    const V d1 = Sub(p1, p0), d2 = Sub(q1, q0), r = Sub(p0, q0);
    const float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
    float ss = 0, tt = 0;
    if (a <= 1e-9f && e <= 1e-9f) { *s = *t = 0; return; }
    if (a <= 1e-9f) { tt = std::clamp(f / e, 0.0f, 1.0f); }
    else {
        const float c = Dot(d1, r);
        if (e <= 1e-9f) { ss = std::clamp(-c / a, 0.0f, 1.0f); }
        else {
            const float b = Dot(d1, d2), den = a * e - b * b;
            ss = den > 1e-9f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
            tt = (b * ss + f) / e;
            if (tt < 0) { tt = 0; ss = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (tt > 1) { tt = 1; ss = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    *s = ss;
    *t = tt;
}

// One hand's blade, frame by frame: the bodies it is on, and its pose the frame before.
class Tracker {
public:
    // The blade now (have: drawn and not the weight model's), at t seconds; kg its steel. The pushes of the bodies it entered this frame go to out (up to max); returns how many.
    int Step(bool have, const Blade& now, double t, float kg, const Body* bodies, int n, Push* out, int max) {
        int np = 0;
        uint32_t onNow[kMaxOn] = {};
        int nOn = 0;
        const bool speedOk = have && was_ && t > t_ && t - t_ <= kLongestStepS;
        const float dt = speedOk ? (float)(t - t_) : 0.0f;
        for (int i = 0; have && i < n; ++i) {
            const Body& b = bodies[i];
            const float reach = now.radius + b.radius;
            float s, u;
            Closest(now.base, now.tip, b.bottom, b.top, &s, &u);
            V onBlade = Add(now.base, Mul(Sub(now.tip, now.base), s));
            V onBody = Add(b.bottom, Mul(Sub(b.top, b.bottom), u));
            V off = Sub(onBlade, onBody);
            float dist = Len(off);
            const uint32_t key = Key(b);
            if (dist < reach) {
                if (nOn < kMaxOn) onNow[nOn++] = key;
            } else {
                // not on it now: did the frame's sweep cross it (a fast blade steps over a thin limb - 12 m/s is 13 cm a frame)? Its first touching pose between
                if (!speedOk || Was(key)) continue;
                const float moved = std::max(Len(Sub(now.tip, last_.tip)), Len(Sub(now.base, last_.base)));
                const int k = std::clamp((int)std::ceil(moved / std::max(0.005f, 0.5f * reach)), 1, 16);
                bool hit = false;
                for (int q = 1; q < k && !hit; ++q) {
                    const float w = (float)q / k;
                    const V b0 = Add(last_.base, Mul(Sub(now.base, last_.base), w)), t0 = Add(last_.tip, Mul(Sub(now.tip, last_.tip), w));
                    Closest(b0, t0, b.bottom, b.top, &s, &u);
                    onBlade = Add(b0, Mul(Sub(t0, b0), s));
                    onBody = Add(b.bottom, Mul(Sub(b.top, b.bottom), u));
                    off = Sub(onBlade, onBody);
                    dist = Len(off);
                    hit = dist < reach;
                }
                if (!hit) continue;
                // (through it within the frame: struck, and off it again)
                const V wasAt = Add(last_.base, Mul(Sub(last_.tip, last_.base), s)), nowAt = Add(now.base, Mul(Sub(now.tip, now.base), s));
                const V vel = Mul(Sub(nowAt, wasAt), 1.0f / dt);
                const V nrm = dist > 1e-6f ? Mul(off, 1.0f / dist) : Mul(vel, -1.0f / std::max(1e-6f, Len(vel)));
                const float in = -Dot(vel, nrm);
                if (in < kInMs || np >= max) continue;
                Push& p = out[np++];
                p.body = i;
                p.impulse = Mul(nrm, -(kg + kArmKg) * in);
                p.point = Add(onBody, Mul(nrm, b.radius));
                p.speed = in;
                continue;
            }
            if (Was(key) || !speedOk || np >= max) continue;   // (on it already: struck when it entered)
            // the same point of the blade the frame before: its speed, into the body along the normal
            const V before = Add(last_.base, Mul(Sub(last_.tip, last_.base), s));
            const V vel = Mul(Sub(onBlade, before), 1.0f / dt);
            const V nrm = dist > 1e-6f ? Mul(off, 1.0f / dist) : Mul(vel, -1.0f / std::max(1e-6f, Len(vel)));
            const float in = -Dot(vel, nrm);
            if (in < kInMs) continue;
            Push& p = out[np++];
            p.body = i;
            p.impulse = Mul(nrm, -(kg + kArmKg) * in);
            p.point = Add(onBody, Mul(nrm, b.radius));
            p.speed = in;
        }
        nOn_ = nOn;
        std::copy(onNow, onNow + nOn, on_);
        last_ = now;
        was_ = have;
        t_ = t;
        return np;
    }

private:
    static uint32_t Key(const Body& b) { return b.id * 64u + (uint32_t)(b.bone + 1); }
    bool Was(uint32_t key) const {
        for (int i = 0; i < nOn_; ++i)
            if (on_[i] == key) return true;
        return false;
    }
    Blade last_;
    bool was_ = false;
    double t_ = 0;
    uint32_t on_[kMaxOn] = {};
    int nOn_ = 0;
};

// The steel of a weapon the weight model does not hold, kg, by its kind (strikedir's kind index): what the doll feels of it.
inline float SteelKg(int kindIndex) {
    switch (kindIndex) {
        case 8: return 0.6f;    // dagger
        case 9: return 0.8f;    // claw
        case 4: return 0.5f;    // wand
        case 1: return 2.0f;    // axe
        case 2: return 2.5f;    // mace
        case 3: return 2.0f;    // scepter
        case 6: case 7: case 5: return 2.5f;   // polearm, spear, staff
        default: return 1.5f;   // sword, throwing, other
    }
}

}  // namespace bladepush
