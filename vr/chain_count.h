// chain_count.h - our swapchains alive in the session, by what they are for, told to the log once when the number changes. Pure C++ (tools/chain_count_test.cpp).
//
// 2026-10-31, .zzangu on 0.192.0 (Quest 3 over SteamVR): "xrCreateSwapchain(interface / map compass / calibration / pointer) failed: XR_ERROR_LIMIT_REACHED"
// five times from ~30 s into the game - the big map from the orb, headset Settings > Calibrate, the compass and the pointer never came. SteamVR gives a session
// 16 swapchains (bearstonem/d2r-pcvr found it the same way: docs/d2r_pcvr_review.md R1) and the retired ones count until the reaper destroyed them
// (chain_reaper.h). Ours had grown to as many as ~30 one by one (the pointer's eight pictures, a static image for each laser part, button and compass, one
// for each line of the T-pose prompt, every panel kept to the session's end); nothing said how many were alive. Now xr.cpp tells each one made (Made) and
// destroyed (Gone, the reaper's too), says the count when it changes, at a frame's start, with the kinds, and says it with a refusal.
#pragma once

#include <cstddef>
#include <cstdio>
#include <vector>

namespace chaincount {

enum Kind { kEye, kFlat, kPointer, kStickers, kInterface, kPanel, kCalibration, kKinds };
inline const char* KindName(int k) {
    static const char* const names[kKinds] = {"eyes", "flat picture", "pointer", "stickers", "interface", "panels", "calibration"};
    return k >= 0 && k < kKinds ? names[k] : "?";
}

template <class H>
class Tally {
  public:
    // One made (a null handle is none; one told twice counts once).
    void Made(H h, Kind k) {
        if (!h || k < 0 || k >= kKinds) return;
        for (const Entry& e : e_)
            if (e.h == h) return;
        e_.push_back(Entry{h, k});
        if (e_.size() > peak_) peak_ = e_.size();
    }
    // One destroyed: true when it was one of ours.
    bool Gone(H h) {
        for (std::size_t i = 0; i < e_.size(); ++i)
            if (e_[i].h == h) { e_.erase(e_.begin() + (std::ptrdiff_t)i); return true; }
        return false;
    }
    std::size_t Live() const { return e_.size(); }
    std::size_t Peak() const { return peak_; }
    std::size_t Of(Kind k) const {
        std::size_t n = 0;
        for (const Entry& e : e_) n += e.k == k;
        return n;
    }
    // True once each time Live() differs from the last time it was asked (the first ask: when any are alive).
    bool Changed() {
        if (e_.size() == told_) return false;
        told_ = e_.size();
        return true;
    }
    // "eyes 2, pointer 1, interface 3" - the kinds alive, in Kind order; out is always terminated.
    void Describe(char* out, std::size_t size) const {
        if (!size) return;
        out[0] = 0;
        std::size_t at = 0;
        for (int k = 0; k < kKinds; ++k) {
            const std::size_t n = Of((Kind)k);
            if (!n || at + 1 >= size) continue;
            const int w = snprintf(out + at, size - at, "%s%s %u", at ? ", " : "", KindName(k), (unsigned)n);
            if (w < 0) break;
            at += (std::size_t)w < size - at ? (std::size_t)w : size - at - 1;
        }
    }
    // The session's end: every one went with it.
    void Reset() {
        e_.clear();
        peak_ = told_ = 0;
    }

  private:
    struct Entry { H h; Kind k; };
    std::vector<Entry> e_;
    std::size_t peak_ = 0, told_ = 0;
};

}  // namespace chaincount
