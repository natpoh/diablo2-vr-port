#pragma once
// [fog] spare_effects: how much of a pixel is the game's opaque scene, from its transparent resolve's own sums
// (vr/fog_spare.h). The C++ mirror of the effect's OpaqueShare (reshade/D2R_DepthFog.fx, the FOGSPARE block),
// tested against it on WARP (cleanroom/tests/fog_spare_test.cpp).
//
// The game's ps_transparent_resolve (disassembled, data/hd/shaders): T = accum.rgb / accum.a * reveal.yzw,
// a = saturate(reveal.x), R = a * S + (1 - a) * T + A where accum.a > 0, else R = S + A.

#include <algorithm>

namespace fogspare {

struct V3 { float r, g, b; };
inline float Luma(V3 c) { return 0.2126f * (std::max)(c.r, 0.0f) + 0.7152f * (std::max)(c.g, 0.0f) + 0.0722f * (std::max)(c.b, 0.0f); }

// r: the resolve's result; accum, reveal: the weighted-blended targets (x y z w); glow: the additive target.
// 1 = all opaque scene (fogged as before), 0 = all effect (no fog).
inline float OpaqueShare(V3 r, const float accum[4], const float reveal[4], V3 glow) {
    V3 e = glow;
    if (accum[3] > 0.0f) {
        const float a = std::clamp(reveal[0], 0.0f, 1.0f), k = (1.0f - a) / accum[3];
        e.r += k * accum[0] * reveal[1];
        e.g += k * accum[1] * reveal[2];
        e.b += k * accum[2] * reveal[3];
    }
    const float lr = Luma(r), le = Luma(e);
    return lr > 1e-6f ? std::clamp(1.0f - le / lr, 0.0f, 1.0f) : 1.0f;
}

}  // namespace fogspare
