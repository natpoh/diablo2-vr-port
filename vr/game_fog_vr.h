#pragma once
// game_fog_vr.h - [fog] game_fog: the game's own distance fog with VR numbers (docs/game_render_sky_fog.md: option F1). The pure parts.
//
// The game has a distance fog of its own in every area's visual data (data/hd/env/vis/<biome>_<time>.json: depthFogColor,
// depthFogDensity, depthFogPlane), blended by its time of day, drawn by its own full-screen pass ps_atmosphere_apply after the
// transparents, from its own linear depth (the range [render] depth_fix sets for our camera):
//     d = distance / plane - 1;   t = min(1, exp(-density x d));   colour = scene x t + fogColour x (1 - t)
// (nothing farther than 10000 planes: the void). Its planes are 870 - 900, the height of its camera above the ground: from our
// camera it never shows (or, as [render] game_depth_fog=1 left it, a grey haze in the corners). Given our own fog's numbers
// instead, the game draws our fog in a pass it runs anyway: no pass of ours, no copy - Serg on PC2 (GTX 1050 Ti, 2026-10-27): "the
// fog and the sky must cost 0 fps". The look is close to the light fog's, not the same: exponential from its start, reaching
// FullAt of the strength at our fog's end (ours: a power curve that is complete there), and over the transparents the game draws
// before it, as the post pass.
//
// Where: VisualDataSystem::Update (0xF5D1A0) hands the blend to 0xF5DAD0, which writes the fog into the frame's settings 0x18 bytes
// before where the visual data has it (docs/plan_cave_light.md). vrcam's hook there checks that the frame's fog is the blend's for a
// while (SameFog) before it writes anything: another build, another layout - nothing written, a line in the log.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

#include "fog_in_frame_math.h"   // the game's scene units from a display colour (SceneFromDisplay)

namespace gamefogvr {

// The VisualDataComponent's depth fog from its colour on: colour (3), density, plane (the game's reflection data, gamefog's finding);
// the frame's settings hold the same five floats 0x18 bytes earlier.
constexpr size_t kVisFogAt = 0xE8;
constexpr size_t kFrameFogAt = kVisFogAt - 0x18;
constexpr float kFullAt = 0.95f;   // the share of the strength the game's fog reaches at our fog's end (its curve never quite gets there)

struct Fog { float r, g, b, density, plane; };   // as the game keeps them: a scene colour, its density, its plane (world units)
static_assert(sizeof(Fog) == 5 * sizeof(float), "five floats, as the game's");

// Our fog ([fog] start / end, the caves' start / end / strength, the act's or the cave's colour as the post pass shows it).
struct Ours { float start, end, strength, rgb[3]; };

// The game's five floats for ours: the plane where our fog starts (at least 1 unit - the game divides by it), the density that puts
// kFullAt of the strength at our end, the colour in the game's scene units (below its tone map's knee the game shows it as `rgb`).
inline Fog FromOurs(const Ours& o) {
    Fog f{};
    f.plane = (std::max)(1.0f, std::isfinite(o.start) ? o.start : 1.0f);
    const float end = (std::max)(f.plane * 1.01f, std::isfinite(o.end) ? o.end : f.plane * 2.0f);
    const float share = std::clamp(kFullAt * (std::isfinite(o.strength) ? o.strength : 1.0f), 0.0f, 0.99f);
    f.density = -std::log(1.0f - share) / (end / f.plane - 1.0f);
    const fogframe::V3 s = fogframe::SceneFromDisplay({o.rgb[0], o.rgb[1], o.rgb[2]});
    f.r = s.r; f.g = s.g; f.b = s.b;
    return f;
}

// How much of the fog's colour the game's pass lays over a point `dist` units away (ps_atmosphere_apply's 1 - t).
inline float GameFogAmount(const Fog& f, float dist) {
    if (!(f.plane > 0.0f) || dist > 10000.0f * f.plane) return 0.0f;
    const float t = (std::min)(1.0f, std::exp(-f.density * (dist / f.plane - 1.0f)));
    return 1.0f - t;
}

// Where in the frame's settings (as `n` floats from its start) the blend's five fog floats are, exactly: their index, or -1 none, -2
// more than one place. Only a blend with a plane above 0 and some colour or density is looked for (all zero matches anywhere). The
// frame's fog was looked for only at kFrameFogAt (docs/plan_cave_light.md's reading); on PC1, 3.3.94995, it never matched there
// (2026-10-27) - this finds it wherever the game writes it, the same bytes as the blend's.
inline int FindFog(const float* blend, const float* frame, int n) {
    if (!blend || !frame || !(blend[4] > 0.0f) || !std::isfinite(blend[4])) return -1;
    if (blend[0] == 0.0f && blend[1] == 0.0f && blend[2] == 0.0f && blend[3] == 0.0f) return -1;
    int at = -1;
    for (int i = 0; i + 5 <= n; ++i) {
        bool same = true;
        for (int k = 0; k < 5 && same; ++k) same = frame[i + k] == blend[k];
        if (!same) continue;
        if (at >= 0) return -2;
        at = i;
    }
    return at;
}

// The frame's fog as the game just wrote it is the blend's (the layout holds): the same five floats, finite, a plane above 0.
inline bool SameFog(const float* blend, const float* frame) {
    if (!blend || !frame) return false;
    for (int i = 0; i < 5; ++i)
        if (!std::isfinite(blend[i]) || blend[i] != frame[i]) return false;
    return blend[4] > 0.0f;
}

}  // namespace gamefogvr
