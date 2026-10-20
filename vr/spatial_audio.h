// spatial_audio.h - [audio] head_follow: the game's 3D sounds turn with your view (the head, the stick, the mouse).
//
// Stock D2R hands every positioned (3D) voice to FMOD as an offset from the hero in the flat camera's screen frame and never turns FMOD's listener, so in VR a
// monster you turned away from still sounds where the flat camera would have it. Two hooks (bearstonem/d2r-pcvr, MIT: vr/spatial_audio.cpp, v0.3.21, by
// ChuckRibbits) fix it: the screen offset becomes a direction on FMOD's ground plane at the voice's own distance (loudness and falloff stay the game's), and
// listener 0 is turned along the view's yaw every 10 ms, so FMOD re-pans every playing 3D voice itself, looping ones included. The game's 2D voices (music,
// ambience, the interface) and the hero's own sounds are not touched. Stereo only: left/right by where you look. Windows' Mono audio folds it all to the
// middle - the log says so.
//
// GroundPos is pure (cleanroom/tests/spatial_audio_test.cpp); the hooks are vr/spatial_audio.cpp.
#pragma once

#include <cmath>

namespace spatialaudio {

struct V3 { float x, y, z; };

// The game's screen offset (x right, y down the screen, z 0) as a point on FMOD's ground plane (+X right, +Y up, +Z ahead; left-handed) at the same distance:
// right = x, ahead = forwardSign * depthScale * y (depthScale undoes the isometric squash). A zero or broken offset comes back as it was.
inline V3 GroundPos(float x, float y, float z, float forwardSign, float depthScale) {
    const float dist = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(dist) || dist <= 0.001f) return {x, y, z};
    const float right = x, ahead = forwardSign * depthScale * y;
    const float n = std::sqrt(right * right + ahead * ahead);
    if (!(n > 0.0001f)) return {x, y, z};
    return {right * dist / n, 0.0f, ahead * dist / n};
}

// vrcam: the ini's keys (live). headFollow off: the game's own positions and a listener facing ahead, as stock.
void Configure(bool headFollow, bool listenerTurn, float forwardSign, float depthScale);
// The view's yaw in degrees, + turned right (vrcam's g_camYaw).
void SetViewYaw(float (*yaw)());
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
// At the plugin's load (the game sets FMOD's listener once, early: the hook has to be in by then) and twice a second after: the hooks go in once head_follow
// asks for them and their code is there. inWorld: a hero in a world - the log says once if the game's sound system never came by.
void Install(bool inWorld);

}  // namespace spatialaudio
