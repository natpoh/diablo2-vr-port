// vr/render/render_switches.h - what vrcam.cpp uses of vr/render/render_switches.cpp (docs/plan_modules.md M13).
#pragma once

#include <atomic>
#include <cstdint>

namespace vrc {

extern std::atomic<bool> g_terrainHeightsClear;   // [render] terrain_heights: forget the tiles' floors (a game left)
namespace solidwalls { void Tick(); }          // [render] solid_walls
// [ceiling] top_from_level (D2R VR Unleashed's ceiling top, Cal): how far above floorAt (the area's floor the ceiling stands on) the level's
// highest built floor is now - taken whole for 2 s after a new level or area (areaGen), eased up after; 0 when off, unknown or not F4.
// Both ceilings ask it: the game's slab (engitem) and our shader vault (effects). Any thread.
float CeilingTopLift(float heroY, float floorAt, unsigned areaGen);

}  // namespace vrc
