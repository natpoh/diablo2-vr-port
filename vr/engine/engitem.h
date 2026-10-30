// vr/engine/engitem.h - what vrcam.cpp uses of vr/engine/engitem.cpp (docs/plan_modules.md M7) besides the declarations it had at its
// top (vr/vrcam_internal.h: the belt, the held items, the domes, the ceiling).
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "ceiling_measures.h"   // ceilmeasure::Beam, Dome

namespace skel { struct Input; }

namespace vrc {
// What engitem reads of vrcam.cpp: the hero, the head, the eye, the hands, the area's act and skybox, the item a hand took.
extern float g_heroPosNow[3];               // the hero, world (PushArms, the update thread)
extern std::atomic<bool> g_poseHeld;        // [multiplayer] pose_share_held
extern std::atomic<bool> g_headOk;
extern std::atomic<float> g_headCentre[3], g_headAhead[3];
extern std::atomic<bool> g_eyeOk;
extern std::atomic<float> g_eyeWorld[3];      // the camera in the world, last view rebuild
extern std::atomic<int> g_fogAct;           // the area's act for the fog colour
extern std::atomic<bool> g_gameSkyboxHere;  // [sky] game_skybox
extern std::atomic<float> g_lastNear;       // the near plane last written
float ViewHeight();
float UserEyeHeightM();
bool FreshHands(skel::Input* in);
// [ceiling] the rooms measured from the game's own models (SkyTick reads them at each area change; under g_biomeLock where it says)
extern std::atomic<float> g_wallsTopHere;
extern std::atomic<bool> g_beamsHere;
extern ceilmeasure::Beam g_beam;
extern std::atomic<bool> g_chapelHere;
extern std::atomic<unsigned> g_chapelGen;
extern std::vector<ceilmeasure::Dome> g_chapelDomes;
namespace itemgrabwire { extern std::atomic<int> g_heldSide; extern std::atomic<uint32_t> g_heldCode; }   // [weight] grab_items: the hand and the item

namespace engitem {
extern std::atomic<int> g_gameCeilKind;   // [ceiling] game_ceiling: the area's kind (-1: none - not on game_ceiling_biomes, or no fitting model)
std::string ModDataText(const char* rel, std::wstring* file);   // a text file of the mod's data folder (and where it was)
int HoverSpheres(float (*out)[8], int max);                      // the balls over the belt's bottles (xr.cpp, through D2rvrHoverSpheres)
namespace weather {   // [sky] weather_first_person
void Install();
void Tick();
}  // namespace weather
}  // namespace engitem
}  // namespace vrc
