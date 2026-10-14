#pragma once
// The vector maths and the ray against a panel the skill wheel uses are the settings panel's (vr/hset_ray.h: kept generic for
// any panel hung in the headset); this only names them for the wheel. Pure: no Windows, no OpenXR.

#include <cmath>

#include "hset_ray.h"

namespace wheel {

using hset::Add;
using hset::Dot;
using hset::IntersectPanel;
using hset::Mul;
using hset::PanelHit;
using hset::Pose;   // (valid: false is a panel that is not there)
using hset::Q;
using hset::Rot;
using hset::Sub;
using hset::Unit;
using hset::V3;
using hset::YawOf;
using hset::YawQ;

// A rotation then another: Rot(QMul(a, b), v) == Rot(a, Rot(b, v)).
inline Q QMul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// A point given in `parent`'s own frame, as the frame `parent` is placed in sees it (the room's poses into the layers' space).
inline V3 Apply(const Pose& parent, V3 v) { return Add(parent.p, Rot(parent.q, v)); }
inline Pose Compose(const Pose& parent, const Pose& child) {
    Pose r;
    r.p = Apply(parent, child.p);
    r.q = QMul(parent.q, child.q);
    r.valid = parent.valid && child.valid;
    return r;
}

inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Finite(const Q& q) {
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(n) && n > 0.5f && n < 2.0f;
}

}  // namespace wheel
