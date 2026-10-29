// native_rsapi.h - [render] no_reshade: a stand-in for the part of ReShade's API vr/hud.cpp uses, over the game's own D3D12, so the game's
// interface layer ([hud] hide, the map, the labels, the wrists' globes, the notice and the plate) is handled by the same code with ReShade or
// without it (docs/plan_no_reshade.md). hud.cpp's handlers get what ReShade's events gave them, from the plugin's own hooks (vr/native_d3d12.h):
// a render target cleared, the targets bound, the scissors and viewports set, a list reset or made - and record through a command_list and a
// device that are D3D12 underneath: barriers, copies, clears and target binds on the game's list (not reported back to the hooks, as ReShade
// does not report an add-on's own calls), textures and their views made on the game's device. A view handle is a CPU descriptor handle, as
// ReShade's D3D12 one is; a resource handle is the ID3D12Resource*.
#pragma once

#include <cstdint>

struct ID3D12Resource;
namespace natived3d { struct Sinks; }

namespace nrs {

// The hooks' sinks for the interface (natived3d::Sinks clearRtv / targets / scissors / viewports / listReset): into hud.cpp's handlers.
void HudSinks(natived3d::Sinks& k);
// The texture a view made through the stand-in is of, and its format (a DXGI_FORMAT); else the resource of a render-target view the game made
// (format 0). nullptr: neither. For the native effect's bindings of hud.cpp's textures (D2R_GAME_LAYER, D2R_GAME_GLOBES, ...).
ID3D12Resource* ResourceOfView(uint64_t view, uint32_t* dxgiFormat);

}  // namespace nrs
