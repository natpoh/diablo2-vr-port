#pragma once
// What native OpenXR (xr.cpp) takes from hud.cpp to hang the game's interface in the
// room itself, as FlatVR did from BodyWalkVR_GameHud: the toolbar's and the map's
// textures (R16G16B16A16_FLOAT, premultiplied, alpha = transmittance, left in the
// shader-resource state on the game's queue), whether each is out of the picture now,
// the look ([hud] in d2r_vr.ini) and the hero's forearm the toolbar lies on.

#include <cstdint>
#include "game_hud_shared.h"

struct ID3D12Resource;

namespace hud {

enum { kNativeBar = 0, kNativeMap = 1, kNativePieces = 2 };
struct NativePiece {
    ID3D12Resource* tex = nullptr;
    uint32_t w = 0, h = 0;
    bool visible = false;   // taken out of the picture by the last interface pass, and that pass is fresh
};
// False when nothing has been published yet.
bool NativeHud(NativePiece out[kNativePieces], FlatVRGameHudLook* look, FlatVRGameHudPose* barPose);

}  // namespace hud
