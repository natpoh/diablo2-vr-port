// xr_failure.h - when the runtime fails, when the session is given up, and how often it may be made again (vr/xr.cpp).
//
// 2026-10-17: Virtual Desktop's runtime answered xrBeginFrame with XR_ERROR_RUNTIME_FAILURE (its own D3D device was removed with
// the game's), and the pair went on asking, frame after frame, with the last good eyes kept - no word of it beyond the first line.
// Now: a failure that is the runtime's (not a call made wrong) is counted per frame; a session that is lost, or a runtime that fails
// for a quarter of a second in a row, is given up - nothing more is submitted, the game goes on flat, and with the device alive a
// new session is made after a few seconds (at most three times in ten minutes: a runtime that cannot keep one is not asked again).
// Pure (no runtime calls); tested in tools/xr_hardening_test.cpp.
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

namespace xrfail {

enum class Kind : uint8_t { Fine, Transient, Fatal };   // not the runtime's failure / maybe a moment / the session cannot go on

inline Kind Classify(XrResult r) {
    if (XR_SUCCEEDED(r)) return Kind::Fine;
    switch (r) {
        case XR_ERROR_SESSION_LOST:
        case XR_ERROR_INSTANCE_LOST:
        case XR_ERROR_RUNTIME_UNAVAILABLE:
        case XR_ERROR_GRAPHICS_DEVICE_INVALID:   // the D3D device the session runs on is gone
            return Kind::Fatal;
        case XR_ERROR_RUNTIME_FAILURE:
        case XR_ERROR_OUT_OF_MEMORY:
            return Kind::Transient;
        default:                                  // a call made wrong, a handle, an order: ours to find, not a reason to give the session up
            return Kind::Fine;
    }
}

constexpr uint32_t kMinFrames = 3;      // the runtime failed this many frames in a row ...
constexpr uint32_t kMinMs = 250;        // ... over at least this long

// The frames' results, one call a frame per place that can fail (xrWaitFrame, xrBeginFrame, xrEndFrame).
class Streak {
public:
    // True: give the session up now.
    bool Note(XrResult r, uint64_t nowMs) {
        switch (Classify(r)) {
            case Kind::Fatal: return true;
            case Kind::Transient:
                if (!n_++) firstMs_ = nowMs;
                return n_ >= kMinFrames && nowMs - firstMs_ >= kMinMs;
            case Kind::Fine:
            default:
                if (r == XR_SUCCESS) n_ = 0;   // a frame that went through ends the streak (a call made wrong, a frame discarded, neither count nor end it)
                return false;
        }
    }
    uint32_t Count() const { return n_; }
    void Reset() { n_ = 0; }
private:
    uint32_t n_ = 0;
    uint64_t firstMs_ = 0;
};

// Sessions made again after a failure: at most kMax in kWindowMs.
class Restarts {
public:
    static constexpr uint32_t kMax = 3;
    static constexpr uint64_t kWindowMs = 600000;
    // True when one more is allowed now (and counts it).
    bool Allow(uint64_t nowMs) {
        uint32_t live = 0;
        for (uint32_t i = 0; i < kMax; ++i)
            if (at_[i] && nowMs - at_[i] < kWindowMs) ++live;
        if (live >= kMax) return false;
        for (uint32_t i = 0; i < kMax; ++i)
            if (!at_[i] || nowMs - at_[i] >= kWindowMs) { at_[i] = nowMs ? nowMs : 1; break; }
        return true;
    }
private:
    uint64_t at_[kMax] = {};
};

}  // namespace xrfail
