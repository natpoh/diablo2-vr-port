// steady_depth.h - [cinema] video_3d: a depth network's answers for one film turned into a steady 0..1 map (near = 1). Pure, no Windows.
//
// The network guesses each frame afresh, so its scale and its fine detail both shimmer from frame to frame. This keeps them still enough
// to look at without hiding what moves:
//   - the range is the map's 2nd..98th percentile (one bright speck does not rescale the picture), eased between frames of one shot;
//   - each pixel is eased half and half with its last value;
//   - a big change in the picture (a cut) starts both afresh, and a picture that has not changed (the film's frame shown again at the
//     headset's rate) is not worked on at all.
// Our own code (D2R VR, MIT).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace video3d {

struct SteadyDepth {
    // Thresholds on the mean change of the picture's brightness (0..1) between the frames looked at.
    static constexpr float kRepeat = 0.002f;   // below: the same picture again
    static constexpr float kCut = 0.12f;       // above: a new shot
    static constexpr float kRangeKeep = 0.8f;  // share of the last range kept, within a shot
    static constexpr float kPixelKeep = 0.5f;  // share of each pixel's last value kept, within a shot

    std::vector<float> prevLuma;
    std::vector<float> map;
    float lo = 0.0f, hi = 1.0f;
    bool have = false;

    enum class Verdict { Repeat, Same, Cut };

    // Whether this picture (its brightness, one value a pixel) needs the network: Repeat = no; Same / Cut = yes, Cut starting afresh.
    // `reset` forces a cut (a new film). The picture is remembered unless it is a repeat.
    Verdict Look(const std::vector<float>& luma, bool reset) {
        float change = 1.0f;
        if (!reset && have && prevLuma.size() == luma.size() && !luma.empty()) {
            double s = 0;
            size_t n = 0;
            for (size_t i = 0; i < luma.size(); i += 2, ++n) s += std::fabs(luma[i] - prevLuma[i]);
            change = (float)(s / (double)std::max<size_t>(n, 1));
        }
        if (change < kRepeat && have) return Verdict::Repeat;
        prevLuma = luma;
        return (reset || !have || change > kCut) ? Verdict::Cut : Verdict::Same;
    }

    static void Percentiles(const std::vector<float>& d, float& outLo, float& outHi) {
        std::vector<float> s;
        s.reserve(d.size() / 4 + 1);
        for (size_t i = 0; i < d.size(); i += 4) s.push_back(d[i]);
        if (s.empty()) { outLo = 0.0f; outHi = 1.0f; return; }
        const size_t a = s.size() * 2 / 100, b = std::min(s.size() - 1, s.size() * 98 / 100);
        std::nth_element(s.begin(), s.begin() + (std::ptrdiff_t)a, s.end());
        outLo = s[a];
        std::nth_element(s.begin(), s.begin() + (std::ptrdiff_t)b, s.end());
        outHi = s[b];
    }

    // The network's answer (larger = nearer) into the steady map. Returns the map.
    const std::vector<float>& Apply(const std::vector<float>& raw, bool cut) {
        float l, h;
        Percentiles(raw, l, h);
        if (cut || !have) { lo = l; hi = h; }
        else { lo = kRangeKeep * lo + (1.0f - kRangeKeep) * l; hi = kRangeKeep * hi + (1.0f - kRangeKeep) * h; }
        const float range = std::max(hi - lo, 1e-6f);
        const bool fresh = cut || !have || map.size() != raw.size();
        if (map.size() != raw.size()) map.assign(raw.size(), 0.5f);
        const float keep = fresh ? 0.0f : kPixelKeep;
        for (size_t i = 0; i < raw.size(); ++i) {
            const float d = std::min(1.0f, std::max(0.0f, (raw[i] - lo) / range));
            map[i] = keep * map[i] + (1.0f - keep) * d;
        }
        have = true;
        return map;
    }

    void Clear() { *this = SteadyDepth{}; }
};

}  // namespace video3d
