// vr/render/effects/effects.h - what vrcam.cpp uses of our effect (vr/render/effects/effects.cpp, docs/plan_modules.md M15).
#pragma once

#include <atomic>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace stabdbg { struct Note; }

namespace vrc {
namespace fx {
extern bool g_registered;   // our effect's add-on registered with ReShade
extern bool g_joined;       // reshade::register_addon(g_self) done
bool JoinReShade();
void TryRegister();
bool FogColourNow(float rgb[3]);
bool TableKeyWanted();
void PlateWorldTick(ULONGLONG nowMs);
void NoticeCardTick(ULONGLONG nowMs);
void StabNoteSink(const stabdbg::Note& n);   // [debug] stab_debug
namespace dlsseyes { void Install(); }
namespace noreshade { void Install(); bool FxWaiting(); }
}  // namespace fx

}  // namespace vrc
