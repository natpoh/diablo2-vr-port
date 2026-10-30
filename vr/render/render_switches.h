// vr/render/render_switches.h - what vrcam.cpp uses of vr/render/render_switches.cpp (docs/plan_modules.md M13).
#pragma once

#include <atomic>
#include <cstdint>

namespace vrc {

extern std::atomic<bool> g_roomHeightsClear;   // [render] floors_stay: forget the rooms' heights (a new area)
namespace solidwalls { void Tick(); }          // [render] solid_walls

}  // namespace vrc
