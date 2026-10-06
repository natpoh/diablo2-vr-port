#pragma once
// Everything this layer knows about the game binary, in one place.
// D2R 3.3.93787 under D2RLoader 1.3.1 (image base 0x140000000). Every RVA and
// offset is explained, with the evidence that found it, in cleanroom/RECON.md.
// A different game build changes the bytes, the loader's expected-byte check
// fails, and the layer stays off instead of writing into the wrong place.

#include <cstddef>
#include <cstdint>

namespace d2rcam::layout {

// ---- Camera object (D2Render\src\camera.cpp) ------------------------------
namespace camf {
inline constexpr size_t View       = 0x10;    // float4x4, row vectors (v * M)
inline constexpr size_t InvView    = 0x50;    // float4x4
inline constexpr size_t Proj       = 0x90;    // float4x4, infinite reverse-Z
inline constexpr size_t Pos        = 0x110;   // float3
inline constexpr size_t LookAt     = 0x11C;   // float3, the hero point the follow aims at
inline constexpr size_t HasLookAt  = 0x128;   // u8
inline constexpr size_t Quat       = 0x12C;   // float4 (x,y,z,w); fwd = q*(0,0,1), up = q*(0,1,0)
inline constexpr size_t FovDeg     = 0x13C;   // float, vertical, degrees
inline constexpr size_t AspectW    = 0x148;   // float
inline constexpr size_t AspectH    = 0x14C;   // float
inline constexpr size_t ViewportW  = 0x150;   // float, pixels
inline constexpr size_t ViewportH  = 0x154;   // float, pixels
inline constexpr size_t Near       = 0x158;   // float
inline constexpr size_t Far        = 0x15C;   // float
inline constexpr size_t ProjType   = 0x168;   // u32: 0 perspective, 1 orthographic
inline constexpr size_t ProjDirty  = 0x170;   // u8
inline constexpr size_t ViewDirty  = 0x171;   // u8
inline constexpr size_t Size       = 0x174;   // every field above (+0x171) rounded up; used for a scratch copy
}   // namespace camf

// ---- Functions we hook -----------------------------------------------------
struct Site {
	uint64_t       rva;
	const uint8_t* bytes;
	uint32_t       size;
};

// CameraTranslation::RenderTick(float dt): the per-frame camera follow. On return
// the game has set the camera for this frame and nothing has read it yet.
inline constexpr uint8_t CamRenderTickBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x20, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x10, 0x55, 0x56, 0x57 };
inline constexpr Site    CamRenderTick { 0x7BD350, CamRenderTickBytes, sizeof(CamRenderTickBytes) };

// Camera::RebuildView(): view and inverse view from position and quaternion.
inline constexpr uint8_t RebuildViewBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00 };
inline constexpr Site    RebuildView { 0xED70A0, RebuildViewBytes, sizeof(RebuildViewBytes) };

// const float4x4* Camera::GetProjection(): rebuilds +0x90 when +0x170 is set.
inline constexpr uint8_t GetProjectionBytes[] { 0x48, 0x8B, 0xC4, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x80, 0xB9, 0x70, 0x01, 0x00, 0x00, 0x00 };
inline constexpr Site    GetProjection { 0xED67A0, GetProjectionBytes, sizeof(GetProjectionBytes) };

// void Camera::ScreenToRay(const float2* px, float3* origin, float3* dir):
// mouse picking, shadow fitting. Built from position/quaternion/fov fields.
inline constexpr uint8_t ScreenToRayBytes[] { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x55, 0x57, 0x41, 0x56 };
inline constexpr Site    ScreenToRay { 0xED62B0, ScreenToRayBytes, sizeof(ScreenToRayBytes) };

// ReleaseCamera(Camera*), hooked 5 bytes in, after its `test rcx,rcx / je`, so the
// short jump stays where it is. Registers and stack are as at entry there.
inline constexpr uint8_t ReleaseCameraBytes[] { 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xC6, 0x81, 0x28, 0x01, 0x00, 0x00, 0x00 };
inline constexpr Site    ReleaseCamera { 0xED6265, ReleaseCameraBytes, sizeof(ReleaseCameraBytes) };

// The shadow fit inside DirectLightShadows::UpdateMatrices casts screen-corner
// rays; calls returning into this range get the game's own camera rays.
inline constexpr uint64_t ShadowFitBegin = 0xF4E4F0;
inline constexpr uint64_t ShadowFitEnd   = 0xF4FC7C;

// ---- Functions we call -----------------------------------------------------
inline constexpr uint8_t GetRendererBytes[] { 0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
inline constexpr Site    GetRenderer { 0xE21140, GetRendererBytes, sizeof(GetRendererBytes) };

inline constexpr uint8_t GetCameraBytes[] { 0x40, 0x53, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0xE8 };
inline constexpr Site    GetGameCamera { 0xE20F00, GetCameraBytes, sizeof(GetCameraBytes) };
inline constexpr Site    GetActiveCamera { 0xE20BE0, GetCameraBytes, sizeof(GetCameraBytes) };

// ---- Rooms (D2Prism\src\Translation\level_translation.cpp, D2Common Drlg) ---
namespace room {
inline constexpr size_t Drlg = 0x18;          // ROOM -> DRLG_ROOM*
}
namespace drlgroom {
inline constexpr size_t NearData  = 0x10;     // DRLG_ROOM** (blz::vector data)
inline constexpr size_t NearCount = 0x18;     // size_t
inline constexpr size_t Room      = 0x58;     // ROOM*, null until the room is tiled
inline constexpr size_t Level     = 0x90;     // DRLG_LEVEL*
}
namespace roomlist {                          // blz::vector<ROOM*>
inline constexpr size_t Data = 0x00;
inline constexpr size_t Size = 0x08;
}

// LevelTranslationLayer::SimulationTick(): builds the drawn room set each tick.
inline constexpr uint8_t SimTickBytes[] { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x49, 0x8D, 0xAB, 0xE8, 0xF9, 0xFF, 0xFF };
inline constexpr Site    LevelSimTick { 0x6B3E20, SimTickBytes, sizeof(SimTickBytes) };

// LevelTranslationLayer::FindRoomsVisibleToRoomSet(ROOM*, vector<ROOM*>&): one ring per call.
inline constexpr uint8_t FindRoomsBytes[] { 0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x58, 0x4C, 0x89, 0xB4, 0x24, 0x88, 0x00, 0x00, 0x00 };
inline constexpr Site    FindRooms { 0x6A8F60, FindRoomsBytes, sizeof(FindRoomsBytes) };

// EnsureRoomTiled(u8 gameVersion, DRLG_ROOM*) -> ROOM*: the game's on-demand tiling.
inline constexpr uint8_t EnsureTiledBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x42, 0x50 };
inline constexpr Site    EnsureRoomTiled { 0x3289A0, EnsureTiledBytes, sizeof(EnsureTiledBytes) };

// blz::vector<ROOM*>::push_back(const ROOM** element).
inline constexpr uint8_t PushBackBytes[] { 0x4C, 0x8B, 0xDC, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30 };
inline constexpr Site    RoomListPushBack { 0x1FB710, PushBackBytes, sizeof(PushBackBytes) };

// Game version byte the D2Common room functions take: *(u8*)(0x2A210A0 + 0x9CF).
inline constexpr uint64_t GameVersionByte = 0x2A210A0 + 0x9CF;

// ---- Model visibility (D2Prism LevelTranslationLayer, D2Render AssetStandbySystem) ---
// Models (houses, props, units) farther than this radius from the hero go to
// standby and are not drawn. The game's debug panel "Model Visibility" edits it
// (slider "Radius" 10..1000, default 150). SetModelVisibilityRadius(float):
// maxss xmm0, [1.0]; movss [radius], xmm0; ret.
inline constexpr uint8_t SetModelRadiusBytes[] { 0xF3, 0x0F, 0x5F, 0x05, 0xAC, 0xA7, 0xF2, 0x00, 0xF3, 0x0F, 0x11, 0x05, 0x50, 0xF9, 0xA5, 0x01, 0xC3 };
inline constexpr Site    SetModelRadius { 0xD90150, SetModelRadiusBytes, sizeof(SetModelRadiusBytes) };
// float GetModelVisibilityRadius(): movss xmm0, [radius]; ret.
inline constexpr uint8_t GetModelRadiusBytes[] { 0xF3, 0x0F, 0x10, 0x05, 0x48, 0xFC, 0xA5, 0x01, 0xC3 };
inline constexpr Site    GetModelRadius { 0xD8FE60, GetModelRadiusBytes, sizeof(GetModelRadiusBytes) };
// ProcessModelVisibility re-sorts models only when the hero moved past the
// "Elastic Range" or this byte ("Recalculate Every Frame") is set: the check
// `cmp byte [0x34B6C74], 0` at 0x6AF5B1.
inline constexpr uint8_t RecalcCheckBytes[] { 0x80, 0x3D, 0xBC, 0x76, 0xE0, 0x02, 0x00 };
inline constexpr Site    ModelRecalcCheck { 0x6AF5B1, RecalcCheckBytes, sizeof(RecalcCheckBytes) };
inline constexpr uint64_t ModelRecalcEveryFrame = 0x34B6C74;

}   // namespace d2rcam::layout
