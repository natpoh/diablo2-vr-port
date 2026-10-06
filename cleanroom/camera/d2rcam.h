#pragma once
// Free perspective camera and render distance for D2R, written clean-room from
// the game itself (evidence in cleanroom/RECON.md, requirements in SPEC.md).
//
// The game keeps one world camera (D2Render's Camera, tag GameCameraTag) and
// rebuilds its view every frame from a position and a quaternion that its
// follow logic (D2Prism CameraTranslation) aims at the hero. This layer lets
// the caller replace the view and the projection of that camera every frame,
// right after the follow ran and before anything renders, culls or picks with
// it. Matrices are row-major with row vectors (p' = p * M), right-handed,
// view looks down -Z, projections infinite-far reverse-Z - the game's own
// conventions, so the depth buffer is exactly what the supplied matrix makes.
//
// Threading: callbacks run on the game's render thread inside the frame; keep
// them cheap and do not call back into this API from them except InWorld().

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace d2rcam {

struct WorldView {
	float gameView[16];   // the game's own view this frame
	float gameProj[16];   // the game's own projection this frame
	float lookAt[3];      // the point the game camera aims at: the hero on the ground
	bool  lookAtValid;    // false until the follow has placed the camera once
	float viewportW;      // the game's viewport in pixels
	float viewportH;
};

// Return false to leave the game's own matrix for this frame.
using ViewFn = bool (*)(const WorldView& in, float outView[16]);
using ProjFn = bool (*)(const WorldView& in, float outProj[16]);

// Installs the hooks. Pages of the game image are decrypted late, so call it
// again (e.g. once per UI update) until it returns true. Needs a plugin with
// PluginFlags::NativeHooks. Safe to call any number of times.
bool Install(const D2RL::PluginContext* ctx);

// On/off at run time. Off restores the game's camera exactly on the next frame.
void SetEnabled(bool on);
bool IsEnabled();

void SetCallbacks(ViewFn view, ProjFn proj);

// Called once per game frame, right after the game placed its camera and before
// the view/projection callbacks, while a game is running (enabled or not).
using FrameFn = void (*)();
void SetFrameCallback(FrameFn frame);

// Runs the view/projection callbacks again on this frame's game camera and puts
// the result in, as the frame sync does. For drawing one game frame twice (a
// stereo pair): call it between the two draws on the thread that draws. False
// when not overriding.
bool Refresh();

// Mouse picking can be redirected: return true with a world ray to answer the
// game's pick with it (e.g. aiming along a hand). Shadow fitting never asks.
using RayFn = bool (*)(float origin[3], float dir[3]);
void SetRayOverride(RayFn ray);

// The point the game camera aims at (the hero), read from the camera now.
bool LookAtNow(float out[3]);

// True while the game's world camera exists and is the active camera (a game
// is running and the debug camera is not in use).
bool InWorld();

// Screen point in the game's viewport pixels -> world ray through the matrices
// in use this frame (ours when enabled). dir is normalized. False when not in world.
bool PickRay(float sx, float sy, float origin[3], float dir[3]);

// Render distance in rooms around the hero (rings of D2 rooms). 1 is the game's
// own behaviour. 2 draws every room the game already keeps loaded, in every
// direction. 3 and more also load rooms beyond what the game keeps (see
// TESTPLAN.md for what that changes). Values are clamped to [1, 8].
void SetRenderRadius(int rooms);
int  GetRenderRadius();

// Models (houses, props, units) are drawn out to this many world units from the
// hero; the game's own is 150 (the hero is ~7 tall). Clamped to [10, 10000].
// False until Install found the game's setter.
bool  SetModelRadius(float units);
float GetModelRadius();   // -1 when unknown

// Diagnostics for a status line: which hook groups are live.
struct Status {
	bool cameraHooks;     // all camera hooks installed
	bool roomHooks;       // render-distance hooks installed
	bool overriding;      // our matrices are in the camera this frame
	int  roomsAdded;      // rooms the last tick added beyond the game's own set
	int  roomsLoaded;     // rooms tiled on demand so far
};
Status GetStatus();

}   // namespace d2rcam
