#pragma once
// ReShade's DXGI factory kept from calling an adapter's method on an interface that is no
// adapter (vr/reshade_shield.cpp): native OpenXR's runtime died on it in a player's game.

namespace reshade_shield {

// Once, before native OpenXR's runtime comes into the game (xr.cpp, StartSession), after
// MH_Initialize. Says in the log what it did; without ReShade's proxy factory it does nothing.
void Install(void (*log)(const char*));

}  // namespace reshade_shield
