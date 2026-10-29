// knock_props.h - [weight] knock_props: the level's own small props (cups, plates, vases, books, stools...) knocked about by the held weapon and the bare fists, F4 only
// (docs/notes/knock_props.md). Only DRAWN: no request of the game's, no item, no collision, nothing of its state changes - the game's own model is moved where the prop
// lies, and everything is back in its place when the area loads again.
//
// How it runs:
//   vr/prop_scan.cpp         twice a second, on a worker: the game's own props near the hero, found in the renderer's registry by the catalogue (d2r_vr_props/props.bin,
//                            made by D2R VR Settings from the player's game) - read only; and, once per frame inside the game's world-matrix function, the poses of the
//                            moving ones written over the renderer's matrices (motion vectors and bounds with them)
//   NoteBlade                hit by touch's blades as drawn (render metres), every pose: the movers
//   the physics thread       a fixed 120 Hz on a core of its own (its ideal processor the last one): the weight library's props world (vr/props_lib.h: Box3D with
//                            our hands' drive, the weapon of its own mass) - never on the game's threads
//   Tick                     vrcam's timer: the scan's results to the thread, the knocks to the hands (a pulse), the log
// With no weight library 1.3, no catalogue, or the box unticked: nothing runs, nothing is read or written.
#pragma once

#include <cstdint>
#include <vector>

#include "d2r_vr_props.h"

namespace knockprops {

using LogFn = void (*)(const char*);
using PulseFn = void (*)(int side, float amplitude, int durationMs);

// D2RLoaderLoadPlugin: the log, the hands' pulse, the plugin's folder (for d2r_vr_props\props.bin).
void Init(LogFn log, PulseFn pulse, const wchar_t* pluginDir);
// [weight] knock_props (off by default), [weight] knock_strength_pct (the hands' drive on the weapon, 100), knock_haptic (0..1, the pulse of a knock, 0.35); from the ini.
void Configure(bool on, float strengthPct, float haptic);
bool Wanted();
// [debug] knock_props_trace (live): a log line for every knock and every scan (what is near, what each stands on). The log only.
void SetTrace(bool on);
// [debug] knock_props_glow (live, off): every prop in the props world (the catalogued ones within 3 m) lit with the game's own hover highlight, h (vr/belt_glow.h) - to see
// which of the level's props are found. Drawn only.
void SetGlow(bool on, float h);

// What vrcam knows each tick: F4 and a game area (ok), render units per metre, the hero's feet in the render world (units), the game's session (another game or hero: a
// new world; an area change or a waypoint is found by the hero's distance and the game taking its matrices back), the steel in hand (kg, the weight model's, 0 = not
// known) and how many hands hold it.
struct Frame {
    bool ok = false;
    float upm = 1.0f;
    float hero[3] = {};
    uint64_t session = 0;
    float steelKg = 0.0f;
    int hands = 1;
};
void Tick(const Frame& f);

// Hit by touch's blade of hand `side` (0 right, 1 left) as drawn this pose, in render metres (the world / upm), now; kind: 0 a weapon, 1 a bare fist.
// on false: that hand has none now. Any thread.
void NoteBlade(int side, bool on, int kind, const float a[3], const float b[3], float radius);

// [weight] grab_props (vr/grab_wire.cpp): an empty hand's grip takes a prop - the props world's hands (the weight library's D2rpSetHands, vr/props_lib.h). On: the box
// ticked (knock_props must be on: the props are its). Drawn only, as the knocks are.
void ConfigureGrab(bool on);
// The box ticked, knock props running (F4, a game area, the catalogue) and a library with the hands: the hook hands them over.
bool GrabReady();
// Hand `side` (0 right, 1 left) as the skeleton hook has it this pose, render metres: tracked, may take a prop now, the grip, the palm and its turn; the grab radius, the break
// distance (m), the strength. The velocities come from its last poses. Off (tracked false for both, or NoHands): every hand lets go. The skeleton hook's thread.
struct GrabHand {
    bool tracked = false, mayTake = false;
    float grip = -1.0f;
    float pos[3] = {}, rot[4] = {0, 0, 0, 1};
    float grabRadius = 0.08f, breakDistance = 0.8f, strength = 1.0f;
};
void NoteHand(int side, const GrabHand& h);
void NoHands();
// What hand `side` did, render metres (the shared header's D2rpHandOut, its frame moved back): the state of the newest step - where the hand and the prop are drawn as the
// write pass draws the props (between the last two steps) - and the events and the pulse since the last call. False: no hands running.
bool TakeHand(int side, D2rpHandOut* out);
// The catalogue's name of the prop (entity id) the scan knows ("prop" when not known), for the log.
const char* PropName(uint32_t id);

// The game's world-matrix function was called (potion_hide's hook on 0xE3A8C0, the renderer's thread): once per frame the moving props' poses are written.
void Transformed();

// For Settings' status and the log.
const char* StatusText();

// [multiplayer] pose_share_held: a prop as drawn now, for the other players - one our hand holds or threw: its catalogue name, where it stood when first found this
// session (render units: the level's own place of it, the same on every PC), its world matrix as drawn (render units) and whether it is moving. False: not tracked.
struct PropShot { char name[28]; float home[3]; float world[16]; bool moving; };
bool PropNow(uint32_t id, PropShot* out);
// ... and another player's: each prop he holds, throws or has thrown, as his pose tells it (render units: where it stood, where it is, its turn). The write pass puts
// our prop of that name that stood within half a metre of there where he has it, and keeps it there until the area loads again (knock props running: F4, the
// props near our hero only). Any thread; n 0: none told now (those put stay).
struct RemoteProp { uint16_t nameHash; float home[3]; float p[3]; float q[4]; };
void SetRemoteProps(const RemoteProp* r, int n);
// How many of the props told were found among ours in the last pass (and how many were told), for the log.
unsigned RemotePlaced(unsigned* wanted);

// [ceiling] game_ceiling_dome (vrcam): the level's preset pieces the renderer has at any of `places` in their presets (render units, as written in the
// preset): each one's parent (the preset's root entity), its place under it and where it is drawn - read from the transforms as the scan reads them,
// whether knock props run or not. Read only. False: the renderer's registry or its transforms do not read. Any thread (a whole pass over the transforms).
struct PresetPiece { uint32_t parent; float local[3]; float world[3]; };
bool PresetPieces(const float (*places)[3], int n, std::vector<PresetPiece>* out);

}  // namespace knockprops
