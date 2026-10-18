#pragma once
// The low-life vignette's rows of the in-headset settings panel (the Life and Look pages of hset_defs.h) as the plugin's own settings:
// what the panel's DRAFT holds, in the plugin's units (the ini has percent for the threshold and the size, the plugin a fraction), so
// the vignette can be drawn with the rows as they stand - before Apply, and whatever the ini says now - while one of the two pages is open
// (lowhp::SetPreview). Pure: no Windows, no OpenXR; tested in tools/hset_test.cpp.

#include "hset_defs.h"
#include "low_hp.h"

namespace hset {

namespace life_detail {
constexpr int K(const wchar_t* key) { return IndexOf(L"hud", key); }
constexpr int kOn = K(L"low_hp_vignette"), kThreshold = K(L"low_hp_threshold"), kStart = K(L"low_hp_intensity_start"), kEnd = K(L"low_hp_intensity_end"),
              kSaturation = K(L"low_hp_saturation"), kRadius = K(L"low_hp_radius"), kSoftness = K(L"low_hp_softness"), kRoundness = K(L"low_hp_roundness"),
              kGentle = K(L"low_hp_gentle"), kHaptics = K(L"low_hp_haptics"), kHapticsStrength = K(L"low_hp_haptics_strength");
static_assert(kOn >= 0 && kThreshold >= 0 && kStart >= 0 && kEnd >= 0 && kSaturation >= 0 && kRadius >= 0 && kSoftness >= 0 && kRoundness >= 0 && kGentle >= 0 &&
                  kHaptics >= 0 && kHapticsStrength >= 0,
              "the eleven keys of the Life and Look pages are in the table");
}  // namespace life_detail

inline lowhp::Config LifeConfig(const Values& v) {
    using namespace life_detail;
    lowhp::Config c;
    c.on = v[kOn] > 0.5f;
    c.threshold = lowhp::ThresholdFromPercent(v[kThreshold]);
    c.intensityStart = v[kStart];
    c.intensityEnd = v[kEnd];
    c.saturation = v[kSaturation];
    c.radius = lowhp::RadiusFromPercent(v[kRadius]);
    c.softness = v[kSoftness];
    c.roundness = v[kRoundness];
    c.gentle = v[kGentle] > 0.5f;
    c.haptics = v[kHaptics] > 0.5f;
    c.hapticsStrength = v[kHapticsStrength];
    return lowhp::Sanitized(c);
}

}  // namespace hset
