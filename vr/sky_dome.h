#pragma once
// sky_dome.h - [sky] game_sky (vrcam's engitem::SkyDomeFrame): the pure parts of our painted sky drawn by the game itself as a dome
// (tools/game_sky/make_sky_dome.py; docs/game_sky_entity.md). Tested in cleanroom/tests/sky_dome_test.cpp.
#include <cstring>
#include <string>

namespace skydome {

constexpr float kModelRadius = 800.0f;   // the dome model's own radius (make_sky_dome.py --radius)
// Its mesh's index count, every LOD (make_sky_dome.py: 17 rings of 64 columns, two triangles a cell, one at the zenith ring - 1984
// triangles): the game's draws of exactly that many are counted ([sky] game_sky's 10 s line, natived3d::WatchIndexCount)
constexpr unsigned kIndexCount = 5952;
// The game's depth is 24-bit (D24S8, the log's "format 44"): a step of 2^-24. At 10^7 units (near 0.05) the dome's depth, 5e-9, rounded to
// the far plane's 0 and failed the depth test - placed, laid, never drawn (PC1, 2026-10-27: "no sky"). It stands kDepthSteps steps above
// it: ~26000 units at near 0.05, still ~1/10 px of the eyes' disparity - as far as the eye can tell.
constexpr float kDepthSteps = 32.0f;
constexpr float kDomeBand = 0.98f;       // our passes take everything from 98 % of its radius on as the dome (SkyDomeDepth)

struct In {
    bool ticked = false;     // [sky] game_sky
    bool f4 = false;         // VR first person
    bool inWorld = false;
    int palette = -1;        // the painted sky's picture now (-1: no sky wanted here - [sky] enabled, outdoors, no menu)
    int act = 0;             // the area's act
    bool eyeOk = false;      // the eye's place in the world is known
};

// Why the dome is not shown now ("" = shown)
inline const char* WhyNot(const In& in) {
    if (!in.ticked) return "[sky] game_sky=0";
    if (!in.f4) return "not VR F4";
    if (!in.inWorld) return "not in a world";
    if (in.palette < 0) return "no sky wanted here ([sky] enabled, outdoors, no menu)";
    if (in.act < 1 || in.act > 5) return "no act's sky for this area";
    if (!in.eyeOk) return "no eye yet";
    return "";
}

// [sky] game_dome_caves: the same dome, black, underground (vrcam's engitem::CaveDomeFrame)
struct CaveIn {
    bool ticked = false;      // [sky] game_dome_caves
    bool f4 = false;
    bool inWorld = false;
    bool underground = false; // off the outdoor list: caves, dungeons, crypts
    bool eyeOk = false;
};
inline const char* CaveWhyNot(const CaveIn& in) {
    if (!in.ticked) return "[sky] game_dome_caves=0";
    if (!in.f4) return "not VR F4";
    if (!in.inWorld) return "not in a world";
    if (!in.underground) return "not underground";
    if (!in.eyeOk) return "no eye yet";
    return "";
}
// Its radius: the models' ([render] model_radius - nothing farther is drawn anyway) times [sky] game_dome_caves_size (Serg: "диаметром 210"
// - 1 the radius, 0.5 the diameter; the headset tells which reads right), never under 20 units
inline float CaveRadius(float modelRadius, float size) {
    const float r = modelRadius * size;
    return r > 20.0f ? r : 20.0f;
}

// Its local matrix (row vectors, the place in [12..14]): shown - its middle at the eye, scaled to `radius`; put away - a thousandth of
// its size, 5000 units under the eye (a dome of 10^7 units parked at its own size would still stand round the hero).
inline void Matrix(const float eye[3], float radius, bool shown, float m[16]) {
    const float k = shown ? radius / kModelRadius : 0.001f;
    const float out[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, eye[0], shown ? eye[1] : eye[1] - 5000.0f, eye[2], 1};
    std::memcpy(m, out, sizeof out);
}

// The unit's file as vrcam reads it from the mod's data before the game's loader gets it: nullptr = the game may load it, else why not.
// The game wants exactly one entity with a UnitRootComponent - with none it asserted "A root entity was not found for unit" and then
// read through a null in PlaceInstance (PC1, 3.3.94995, 2026-10-27: the game crashed in the camp) - and a model to draw.
inline const char* UnitFileProblem(const std::string& text) {
    if (text.empty()) return "the file is not there (or empty)";
    if (text.find("\"type\":\"UnitDefinition\"") == std::string::npos) return "not a UnitDefinition";
    size_t roots = 0;
    for (size_t at = text.find("\"UnitRootComponent\""); at != std::string::npos; at = text.find("\"UnitRootComponent\"", at + 1)) ++roots;
    if (roots == 0) return "no entity with a UnitRootComponent (the game asserts, then crashes)";
    if (roots > 1) return "more than one UnitRootComponent (the game asserts)";
    if (text.find("\"ModelDefinitionComponent\"") == std::string::npos) return "no ModelDefinitionComponent (nothing to draw)";
    return nullptr;
}

// The radius it is laid at: the one asked for ([sky] game_sky_radius), at most where the depth buffer still holds it (kDepthSteps steps
// above the far plane, near / radius = kDepthSteps / 2^24); a near plane not known yet: 25000.
inline float DrawableRadius(float wanted, float nearPlane) {
    const float most = nearPlane > 0.0f ? nearPlane * 16777216.0f / kDepthSteps : 25000.0f;
    return wanted < most ? wanted : most;
}

// The depth (reversed Z: near / distance) at or under which our passes take a pixel as the dome's (SkyDomeDepth): from kDomeBand of its
// radius on - what stands farther is behind it anyway.
inline float DomeDepth(float radius, float nearPlane) {
    return radius > 0.0f && nearPlane > 0.0f ? nearPlane / (radius * kDomeBand) : 1e-6f;
}

}  // namespace skydome
