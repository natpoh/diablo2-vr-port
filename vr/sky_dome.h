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

// The views the dome is laid in (Serg: "почему в 103 уровне старое небо" - VR's third person, F2, had none and our old sky came back):
// every view of our camera that looks round the world from in it. Not: the views from above (the world under the camera - a dome round it
// would stand between them; VR F1, in perspective or the game's own, and the flat game's F1), the table (VR F3: the world a model on a
// table in the room, no sky round it), the VR camera off (F12: the game's own picture), and the flat game without [render]
// game_drawn_flat (our effect draws its sky). "" = the dome can be laid.
struct ViewIn {
    bool vr = false;              // [mode] platform 1
    int view = 0;                 // [mode] vr_view / flat_view: 1 from above, 2 third person, 3 the table (VR) / first person (flat), 4 VR first person
    bool enabled = false;         // our camera on (not F12)
    bool gameDrawnFlat = false;   // [render] game_drawn_flat
};
inline const char* ViewWhyNot(const ViewIn& v) {
    if (!v.enabled) return "the VR camera is off (F12)";
    if (v.view == 1) return "the view from above (the world under the camera)";
    if (v.vr && v.view == 3) return "the table view (the world a model in the room)";
    if (!v.vr && !v.gameDrawnFlat) return "the flat game with [render] game_drawn_flat=0 (our effect draws its sky)";
    if (v.view < 1 || v.view > 4 || (!v.vr && v.view > 3)) return "not a view of ours";
    return "";
}

struct In {
    bool ticked = false;     // [sky] game_sky
    bool f4 = false;         // a view the dome is laid in (ViewWhyNot "")
    const char* viewWhy = nullptr;   // why not, when f4 is false (ViewWhyNot)
    bool inWorld = false;
    int palette = -1;        // the painted sky's picture now (-1: no sky wanted here - [sky] enabled, outdoors, no menu)
    int act = 0;             // the area's act
    bool eyeOk = false;      // the eye's place in the world is known
    bool gameSkybox = false; // [sky] game_skybox ticked and the area is one of kGameSkyboxAreas (the dome laid far: SkyboxRadius)
};

// [sky] game_skybox (Serg on 0.188.3, level 120: our act 5 dome over the Arreat Summit, a navy band and the fog's grey disc under its
// horizon where the game's own sea of clouds is): the areas whose preset places the game's cloud sea and skybox - of every preset of play the
// Summit's (MtnTop: expansion_mountaintop_skybox/skybox01, the card our domes are made from) and the uber Summit's, level 137 ("137
// отсутствует бакграунд": ColossalSummit, biome UberArreatSummit - off [sky] outdoor, so nothing of ours behind it either); the others
// are the menus' scenes. There no dome of ours, and the area is
// open air, not underground (no cave dome, no cave light). (Biome names as the game sets them, any case.)
// What stands aside there is only the game-drawn dome (Serg's "старое небо и новое", 0.188.3: the old look - our post pass's painted
// sky in the void, the game's sea of clouds rising round the mountain in front of it - was lost under the dome, a mesh 600 units round
// that hides the cloud sea and leaves its own lower band and the fog's disc). Our post pass's sky and fog stay as they were: the game's
// skybox itself never drew above them from our camera (the painted clouds showed there).
// Since Serg's "а почему там старое небо" (0.188.4) the game-drawn dome stays there too, laid behind the cloud sea (SkyboxRadius): our
// post sky stays off wherever the game draws the sky.
constexpr const char* kGameSkyboxAreas[] = {"expansion_mountaintop", "uberarreatsummit"};
inline bool IsGameSkyboxArea(const std::string& biome) {
    std::string b = biome;
    for (char& c : b) c = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    for (const char* a : kGameSkyboxAreas) if (b == a) return true;
    return false;
}

// Why the dome is not shown now ("" = shown)
inline const char* WhyNot(const In& in) {
    if (!in.ticked) return "[sky] game_sky=0";
    if (!in.f4) return in.viewWhy && *in.viewWhy ? in.viewWhy : "not a view the dome is laid in";
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

// [sky] game_skybox (an option, off: Serg "какое ещё море облаков" - the Summits as any act 5 area): the dome behind the game's mist (FX_Act5MountainTop_abyssalfog_40, CliffRibbons,
// FloorHaze, Haze; particles, drawn after the opaque scene against its depth) stand along the edges of the presets' 210 x 291 units (MtnTop,
// ColossalSummit), so a dome 600 units round hid them. Laid as far as the depth buffer holds it (DrawableRadius' limit), never nearer
// than asked: every cloud, the backdrop and the mountain stand in front of it.
inline float SkyboxRadius(float wanted, float nearPlane) {
    const float most = DrawableRadius(1.0e9f, nearPlane);   // (not "far": windows.h's macro)
    return wanted > most ? wanted : most;
}

// The depth (reversed Z: near / distance) at or under which our passes take a pixel as the dome's (SkyDomeDepth): from kDomeBand of its
// radius on - what stands farther is behind it anyway.
inline float DomeDepth(float radius, float nearPlane) {
    return radius > 0.0f && nearPlane > 0.0f ? nearPlane / (radius * kDomeBand) : 1e-6f;
}

}  // namespace skydome
