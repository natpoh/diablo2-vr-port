// xr_layers.h - the layers of one xrEndFrame, looked over before they go (vr/xr.cpp CloseFrame).
//
// A quad whose turn is not of unit length, or whose pose or size is not a number, makes xrEndFrame
// return XR_ERROR_POSE_INVALID and the runtime drops the WHOLE frame - the world with it - for
// the sake of a belt ball or a laser beam. The views and the hand reads are checked where they are
// taken; the quads built from them (the gesture zones, the toolbar, the map, the menu's panel, the
// pointer, the laser) are made of hand and look data and went out as they were. One pass over
// layers[] here: a quad that cannot be made right is left out, a turn off unit length is made unit,
// and the layer is named in the log once (not every frame it fails).
//
// After bearstonem/d2r-pcvr (MIT), steamvr/transport.cpp EndPair: its motionQuad lambda (finite pose,
// finite positive size, the quaternion divided by its length) and its XR_ERROR_POSE_INVALID
// diagnostic. Written for our layers; pure (no globals, no game, no runtime calls), tested in
// tools/xr_hardening_test.cpp.
#pragma once

#include <openxr/openxr.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace xrlayers {

enum class Verdict : uint8_t { Ok, Tidied, Drop };   // as it was / its turn made unit length / left out
enum class Why : uint8_t { None, Position, Size, Orientation, Fov };

struct Check {
    Verdict verdict = Verdict::Ok;
    Why why = Why::None;       // what was wrong (Tidied: Orientation)
    float norm2 = 1.0f;        // the turn's length squared as it came (the first view's of a projection that fails)
    int view = -1;             // a projection layer: which view
};

// A turn off unit length by more than this (in its length squared) is told; below it is float noise
// and is set right quietly. A runtime's own tolerance is about this order or looser.
constexpr float kToldOff = 1e-3f;

inline bool Finite(const XrVector3f& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline float Norm2(const XrQuaternionf& q) { return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w; }

// A pose the runtime takes: a place that is a number, a turn that is a number and not zero, made of
// unit length in place. Drop: the pose cannot be made right.
inline Check CheckPose(XrPosef& p) {
    Check c;
    if (!Finite(p.position)) { c.verdict = Verdict::Drop; c.why = Why::Position; return c; }
    const float n2 = Norm2(p.orientation);
    c.norm2 = n2;
    if (!std::isfinite(n2) || !(n2 > 1e-12f)) { c.verdict = Verdict::Drop; c.why = Why::Orientation; return c; }
    if (std::fabs(n2 - 1.0f) > 1e-6f) {
        const float k = 1.0f / std::sqrt(n2);
        p.orientation = {p.orientation.x * k, p.orientation.y * k, p.orientation.z * k, p.orientation.w * k};
        c.verdict = Verdict::Tidied;
        c.why = Why::Orientation;
    }
    return c;
}

// A quad: a size that is a number above zero (nothing is seen of the rest), then its pose.
inline Check CheckQuad(XrCompositionLayerQuad& q) {
    if (!std::isfinite(q.size.width) || !std::isfinite(q.size.height) || !(q.size.width > 0.0f) || !(q.size.height > 0.0f)) {
        Check c;
        c.verdict = Verdict::Drop;
        c.why = Why::Size;
        return c;
    }
    return CheckPose(q.pose);
}

// The projection layer: each view's pose as a quad's, and a field of view that is numbers. A view that
// fails takes the layer with it - what is left cannot be made into a picture.
inline Check CheckProjection(XrCompositionLayerProjection& p) {
    Check out;
    if (!p.views) return out;
    for (uint32_t i = 0; i < p.viewCount && i < 2; ++i) {
        XrCompositionLayerProjectionView& v = const_cast<XrCompositionLayerProjectionView&>(p.views[i]);
        const XrFovf& f = v.fov;
        if (!std::isfinite(f.angleLeft) || !std::isfinite(f.angleRight) || !std::isfinite(f.angleUp) || !std::isfinite(f.angleDown)) {
            Check c;
            c.verdict = Verdict::Drop;
            c.why = Why::Fov;
            c.view = (int)i;
            return c;
        }
        Check c = CheckPose(v.pose);
        c.view = (int)i;
        if (c.verdict == Verdict::Drop) return c;
        if (c.verdict == Verdict::Tidied && out.verdict == Verdict::Ok) out = c;
    }
    return out;
}

// Any layer: the ones of a type we do not look at are as they were.
inline Check CheckLayer(XrCompositionLayerBaseHeader* l) {
    if (l->type == XR_TYPE_COMPOSITION_LAYER_QUAD) return CheckQuad(*reinterpret_cast<XrCompositionLayerQuad*>(l));
    if (l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) return CheckProjection(*reinterpret_cast<XrCompositionLayerProjection*>(l));
    return Check{};
}

// layers[0..n) looked over: what can be set right is set right in place, what cannot is taken out
// (the rest closed up, in order). Returns the new count. `report(index, layer, check)` is called for
// each layer that was tidied or dropped - index in the array as it came - for the log. (The layer
// structs are the caller's own, non-const, as it links `next` through the same pointers.)
template <class Report>
inline uint32_t Sanitize(const XrCompositionLayerBaseHeader** layers, uint32_t n, Report&& report) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (!layers[i]) continue;
        XrCompositionLayerBaseHeader* l = const_cast<XrCompositionLayerBaseHeader*>(layers[i]);
        const Check c = CheckLayer(l);
        if (c.verdict != Verdict::Ok) report(i, l, c);
        if (c.verdict != Verdict::Drop) layers[k++] = layers[i];
    }
    return k;
}

inline const char* WhyName(Why w) {
    switch (w) {
        case Why::Position: return "its place is not a number";
        case Why::Size: return "its size is not a number above zero";
        case Why::Orientation: return "its turn is not of unit length";
        case Why::Fov: return "its field of view is not a number";
        default: return "?";
    }
}

// One layer's numbers on a line, for the log when the runtime refuses the frame anyway
// (XR_ERROR_POSE_INVALID names no layer): "quad at 0.12 -0.30 -1.50, turn |q|^2 1.000000, 0.40 x 0.40 m".
inline int Describe(char* out, size_t cap, const XrCompositionLayerBaseHeader* l) {
    if (l->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
        const XrCompositionLayerQuad& q = *reinterpret_cast<const XrCompositionLayerQuad*>(l);
        return snprintf(out, cap, "quad at %.3f %.3f %.3f, turn |q|^2 %.9g, %.3f x %.3f m", q.pose.position.x, q.pose.position.y, q.pose.position.z,
                        Norm2(q.pose.orientation), q.size.width, q.size.height);
    }
    if (l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
        const XrCompositionLayerProjection& p = *reinterpret_cast<const XrCompositionLayerProjection*>(l);
        int n = snprintf(out, cap, "projection, %u view(s)", p.viewCount);
        for (uint32_t i = 0; i < p.viewCount && i < 2 && p.views && n > 0 && (size_t)n < cap; ++i) {
            const XrCompositionLayerProjectionView& v = p.views[i];
            n += snprintf(out + n, cap - (size_t)n, "; view %u at %.3f %.3f %.3f, |q|^2 %.9g, fov %.3f %.3f %.3f %.3f", i, v.pose.position.x,
                          v.pose.position.y, v.pose.position.z, Norm2(v.pose.orientation), v.fov.angleLeft, v.fov.angleRight, v.fov.angleUp, v.fov.angleDown);
        }
        return n;
    }
    return snprintf(out, cap, "layer of type %d", (int)l->type);
}

// Says a thing once, then again only after it has been quiet a while, with how many times it
// happened meanwhile - a slider stuck at NaN is a line every ten seconds, not one a frame. A key
// is the thing's kind (the layer, the fault); keys beyond kSlots share.
class Telling {
 public:
    static constexpr uint32_t kSlots = 64;
    // True when `key` may be told now at `nowMs`; *times: how often it happened since it was last told
    // (this one included).
    bool Due(uint32_t key, uint64_t nowMs, uint32_t quietMs, uint32_t* times) {
        Slot& s = slot_[key % kSlots];
        ++s.count;
        if (s.told && nowMs - s.at < quietMs) return false;
        s.told = true;
        s.at = nowMs;
        if (times) *times = s.count;
        s.count = 0;
        return true;
    }

 private:
    struct Slot { bool told = false; uint64_t at = 0; uint32_t count = 0; };
    Slot slot_[kSlots];
};

}  // namespace xrlayers
