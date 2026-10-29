// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 9e9c2410e8df57f72547c691de9971c88b0b014a ("Big objects: trees, houses and walls drawn past the object
// draw distance"), its steamvr/far_objects.h, taken into D2R VR Authentic as it is but for the namespace and the stepper (the Settings
// slider steps it here). The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
#pragma once
// Big objects drawn farther ([render] big_radius; D2R_VR_Settings > Draw distance).
// The game puts every model - houses, props, units - on standby, undrawn, once its box is farther
// from the hero across the ground than the object draw distance (the AssetStandbySystem's test,
// cleanroom/drawdist). On the horizon a tree or a house still stands out where a stone or a tuft of
// grass is lost, so a big model is kept farther: its reach grows with its size (its height, or half
// its width) by kReachPerUnit, up to the big object distance. Small things keep the object draw
// distance, so the frame pays only for what is seen far off. The game builds models only in the
// rooms it keeps ([render] rings): past those there is nothing to keep.
#include <algorithm>
#include <cmath>

namespace unleashed::farobjects {

inline constexpr float Off = 0, Min = 300, Max = 1000, Step = 100;
inline constexpr float kReachPerUnit = 30;   // a 20-unit tree 600 units, a 2-unit stone 60

// The setting: 0 Off, else 300 - 1000 in steps of 100
inline float Normalize(float value) {
    if (!std::isfinite(value) || value <= 0) return Off;
    return std::clamp(std::round(value / Step) * Step, Min, Max);
}

// A model's box as the game keeps it: min x y z w, then max x y z w; y is up.
// Its size: its height, or half its width, whichever is more.
inline float Size(const float* box) {
    return std::max(box[5] - box[1], 0.5f * std::max(box[4] - box[0], box[6] - box[2]));
}
// How far from the hero a model stays drawn (radius: the object draw distance)
inline float Reach(const float* box, float radius, float big) {
    if (!(big > radius)) return radius;
    return std::max(radius, std::min(big, Size(box) * kReachPerUnit));
}
// The game's test with that reach: true while the box is nearer the point (the hero) than the reach,
// across the ground (x, z). Without a reach past the object draw distance it is the game's own
// comparison: radiusSq over the squared distance.
inline bool InRange(const float* box, const float* point, float radiusSq, float big) {
    const float dx = std::max(box[0], std::min(point[0], box[4])) - point[0];
    const float dz = std::max(box[2], std::min(point[2], box[6])) - point[2];
    const float radius = std::sqrt(std::max(radiusSq, 0.0f));
    const float reach = Reach(box, radius, big);
    return (reach > radius ? reach * reach : radiusSq) > dx * dx + dz * dz;
}

}   // namespace unleashed::farobjects
