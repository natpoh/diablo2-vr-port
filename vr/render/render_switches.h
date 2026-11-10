// vr/render/render_switches.h - what vrcam.cpp uses of vr/render/render_switches.cpp (docs/plan_modules.md M13).
#pragma once

#include <atomic>
#include <cstdint>

namespace vrc {

extern std::atomic<bool> g_terrainHeightsClear;   // [render] terrain_heights: forget the tiles' floors (a game left)
namespace solidwalls { void Tick(); }          // [render] solid_walls

}  // namespace vrc
