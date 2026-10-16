#pragma once
// depth_source.h - [render] depth_fix_mode 2: the game's linear-depth range written ONCE a frame, where the game makes it.
//
// The game keeps its depth as a linear copy for its post passes and its soft particles, decoded with the frame constants'
// word 53 (b10[53] = {range, 1 / range, start, -}), made for its own camera from above (550 .. 1060). Mode 1 (vr/uitrace.cpp,
// D2R VR's own) rewrites that word in every b10 buffer a root signature binds, as the list binds it: a pass whose b10 comes
// another way (a descriptor table, a GPU-only heap, a root signature made before ReShade joined) keeps the game's range while
// the linear copy is made with ours - one surface read as two depths. Mode 2 (bearstonem/d2r-pcvr's, MIT: vr/dlss_depth.cpp,
// dlss_depth_range.h) writes it right after the game's frame-constant builder (0xE3B590) has filled this frame's upload memory,
// before any pass runs: every consumer reads the same {far - near, 1 / (far - near), near}, near from the projection the game
// uploaded for our camera (camera constants +0xD0, its [14]), far = the game's own start + range. Pure: the arithmetic only.
#include <cmath>
#include <cstring>

namespace depthrange {

// `projection`: the uploaded camera projection (16 floats, the game's reversed-Z infinite form: [10] 0, [11] -1, [15] 0, [14] the
// near plane). `range`: the frame's word 53 as the game wrote it, changed in place. False (nothing changed) when either is not
// what the game writes - another kind of projection, a word that is not a range.
inline bool PerspectiveRange(const float projection[16], float range[4]) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(projection[i])) return false;
    if (projection[0] <= 0.0f || projection[5] <= 0.0f || projection[10] != 0.0f || projection[11] != -1.0f || projection[15] != 0.0f ||
        projection[14] <= 0.0f)
        return false;
    if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || !std::isfinite(range[2]) || range[0] <= 0.0f || range[1] <= 0.0f ||
        std::fabs(range[0] * range[1] - 1.0f) > 0.001f)
        return false;
    const float nearPlane = projection[14];
    const float farPlane = range[2] + range[0];
    if (!std::isfinite(farPlane) || farPlane <= nearPlane) return false;
    range[0] = farPlane - nearPlane;
    range[1] = 1.0f / range[0];
    range[2] = nearPlane;
    return true;
}

// The three floats written back (the fourth lane is the game's, left alone).
inline void Store(void* mapped, const float range[4]) { std::memcpy(mapped, range, 3 * sizeof(float)); }

}  // namespace depthrange
