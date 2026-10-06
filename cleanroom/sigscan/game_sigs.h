#pragma once
// Made by tools/make_sigs.py - do not edit by hand. The addresses are those of
// D2R 3.3.93787 under D2RLoader 1.3.1; the patterns find them again in another build
// (cleanroom/sigscan/sigscan.cpp). ?? = any byte (call targets, RIP-relative offsets).

#include <cstdint>

namespace d2rsig {

enum class Kind : uint8_t {
    Code,      // the address = where the pattern matched + offset
    Ref,       // the instruction at match + offset (or at entry `of`) reaches it: insn + insnLen + int32 at insn + dispAt, + addend
    FuncEnd,   // the end of the function found by entry `of`, from .pdata
};

struct Sig {
    const char* name;
    uint64_t    rva;       // in 3.3.93787 / D2RLoader 1.3.1
    Kind        kind;
    const char* pattern;
    uint64_t    site;      // where the pattern starts in 3.3.93787 (looked at first, before any search)
    uint16_t    offset;
    uint8_t     dispAt;
    uint8_t     insnLen;
    int32_t     addend;
    const char* body;      // code: what its first bytes must look like (the hook's expected bytes, moved)
    const char* of;
    const char* without;   // what goes off when it is not found
    const char* area;      // the group it is shown in (the settings program's Status tab)
};

inline constexpr Sig kSigs[] = {
    // CameraTranslation::RenderTick, hooked
    {"CamRenderTick", 0x7BD350, Kind::Code, "48 89 5C 24 20 F3 0F 11 4C 24 10 55 56 57 48 8D AC 24 30 FF FF FF 48 81 EC D0 01 00 00", 0x7BD350, 0, 0, 0, 0, "48 89 5C 24 20 F3 0F 11 4C 24 10 55 56 57", nullptr, "VR camera", "Camera"},
    // Camera::RebuildView, hooked
    {"RebuildView", 0xED70A0, Kind::Code, "48 89 5C 24 10 48 89 74 24 18 57 48 81 EC B0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 A0 00 00 00 48 8B F1 48 8D 91 2C 01 00 00", 0xED70A0, 0, 0, 0, 0, "48 89 5C 24 10 48 89 74 24 18 57 48 81 EC B0 00 00 00", nullptr, "VR camera", "Camera"},
    // Camera::GetProjection, hooked
    {"GetProjection", 0xED67A0, Kind::Code, "48 8B C4 53 48 81 EC 80 00 00 00 80 B9 70 01 00 00 00 48 8B D9", 0xED67A0, 0, 0, 0, 0, "48 8B C4 53 48 81 EC 80 00 00 00 80 B9 70 01 00 00 00", nullptr, "VR camera", "Camera"},
    // Camera::ScreenToRay, hooked
    {"ScreenToRay", 0xED62B0, Kind::Code, "48 8B C4 48 89 58 08 48 89 70 10 55 57 41 56 48 8D 68 A1 48 81 EC C0 00 00 00 0F 29 70 D8", 0xED62B0, 0, 0, 0, 0, "48 8B C4 48 89 58 08 48 89 70 10 55 57 41 56", nullptr, "VR camera", "Camera"},
    // ReleaseCamera(Camera*), hooked 5 bytes in
    {"ReleaseCamera", 0xED6265, Kind::Code, "53 48 83 EC 20 48 8B D9 C6 81 28 01 00 00 00 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??", 0xED6265, 0, 0, 0, 0, "53 48 83 EC 20 48 8B D9 C6 81 28 01 00 00 00", nullptr, "VR camera", "Camera"},
    // DirectLightShadows::UpdateMatrices, start (return-address range)
    {"ShadowFitBegin", 0xF4E4F0, Kind::Code, "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 F7 FF FF 48 81 EC E0 08 00 00", 0xF4E4F0, 0, 0, 0, 0, "48 8B C4 48 89 58 10 55", nullptr, "shadows follow the VR camera", "Camera"},
    // DirectLightShadows::UpdateMatrices, end
    {"ShadowFitEnd", 0xF4FC7C, Kind::FuncEnd, "", 0x0, 0, 0, 0, 0, nullptr, "ShadowFitBegin", "shadows follow the VR camera", "Camera"},
    // GetRenderer(), called
    {"GetRenderer", 0xE21140, Kind::Code, "48 83 EC 28 65 48 8B 04 25 58 00 00 00 8B 0D ?? ?? ?? ?? BA 6C 12 00 00 48 8B 0C C8 8B 04 0A 39 05 ?? ?? ?? ?? 7F 4C 48 8B 05 ?? ?? ?? ??", 0xE21140, 0, 0, 0, 0, "48 83 EC 28 65 48 8B 04 25 58 00 00 00", nullptr, "VR camera", "Camera"},
    // GetGameCamera(), called
    {"GetGameCamera", 0xE20F00, Kind::Ref, "E8 ?? ?? ?? ?? EB 1D 80 BB E3 85 13 00 00 74 17 48 8B CB", 0xE37E0C, 0, 1, 5, 0, "40 53 48 81 EC 00 01 00 00 E8 ?? ?? ?? ??", nullptr, "VR camera", "Camera"},
    // GetActiveCamera(), called
    {"GetActiveCamera", 0xE20BE0, Kind::Ref, "E8 ?? ?? ?? ?? 41 80 BF E2 85 13 00 00 4C 8B E8 74 16", 0xE396CD, 0, 1, 5, 0, "40 53 48 81 EC 00 01 00 00 E8 ?? ?? ?? ??", nullptr, "VR camera", "Camera"},
    // LevelTranslationLayer::SimulationTick, hooked
    {"LevelSimTick", 0x6B3E20, Kind::Code, "4C 8B DC 55 53 56 57 41 54 49 8D AB E8 F9 FF FF 48 81 EC F0 06 00 00", 0x6B3E20, 0, 0, 0, 0, "4C 8B DC 55 53 56 57 41 54 49 8D AB E8 F9 FF FF", nullptr, "render distance", "Rooms and models"},
    // LevelTranslationLayer::FindRoomsVisibleToRoomSet, hooked
    {"FindRooms", 0x6A8F60, Kind::Code, "40 53 56 57 41 54 48 83 EC 58 4C 89 B4 24 88 00 00 00 48 8B DA", 0x6A8F60, 0, 0, 0, 0, "40 53 56 57 41 54 48 83 EC 58 4C 89 B4 24 88 00 00 00", nullptr, "render distance", "Rooms and models"},
    // EnsureRoomTiled(u8, DRLG_ROOM*), called
    {"EnsureRoomTiled", 0x3289A0, Kind::Code, "48 89 5C 24 08 57 48 83 EC 20 8B 42 50 48 8B DA 0F B6 F9", 0x3289A0, 0, 0, 0, 0, "48 89 5C 24 08 57 48 83 EC 20 8B 42 50", nullptr, "render distance", "Rooms and models"},
    // blz::vector<ROOM*>::push_back, called
    {"RoomListPushBack", 0x1FB710, Kind::Ref, "E8 ?? ?? ?? ?? 48 8B 75 80 4C 8D 85 10 0F 00 00 48 8B D6", 0x6B11E5, 0, 1, 5, 0, "4C 8B DC 56 57 41 56 48 83 EC 30", nullptr, "render distance", "Rooms and models"},
    // lea rax, [game globals]; ret
    {"GameGlobals", 0x76010, Kind::Ref, "E8 ?? ?? ?? ?? 66 83 88 5C 06 00 00 08 48 83 C4 28 C3", 0x80B14, 0, 1, 5, 0, "48 8D 05 ?? ?? ?? ?? C3", nullptr, "render distance", "Rooms and models"},
    // u8 game version the room functions take (globals + 0x9CF)
    {"GameVersionByte", 0x2A21A6F, Kind::Ref, "", 0x0, 0, 3, 7, 2511, nullptr, "GameGlobals", "render distance", "Rooms and models"},
    // SetModelVisibilityRadius(float), called
    {"SetModelRadius", 0xD90150, Kind::Ref, "E8 ?? ?? ?? ?? F3 44 0F 11 4C 24 28 49 8D 96 40 1E 00 00 0F 28 DE", 0x6A7094, 0, 1, 5, 0, "F3 0F 5F 05 ?? ?? ?? ?? F3 0F 11 05 ?? ?? ?? ?? C3", nullptr, "model distance", "Rooms and models"},
    // GetModelVisibilityRadius(), called
    {"GetModelRadius", 0xD8FE60, Kind::Ref, "E8 ?? ?? ?? ?? 41 0F 10 95 50 1E 00 00 4C 8D 8D 10 03 00 00 0F 28 0D ?? ?? ?? ??", 0x6B0C97, 0, 1, 5, 0, "F3 0F 10 05 ?? ?? ?? ?? C3", nullptr, "model distance", "Rooms and models"},
    // cmp byte [RecalcEveryFrame], 0 in ProcessModelVisibility
    {"ModelRecalcCheck", 0x6AF5B1, Kind::Code, "80 3D ?? ?? ?? ?? 00 0F 29 B4 24 30 02 00 00 0F 29 BC 24 20 02 00 00 74 09", 0x6AF5B1, 0, 0, 0, 0, "80 3D ?? ?? ?? ?? 00 0F 29 B4 24 30 02 00 00", nullptr, "models re-sorted at once", "Rooms and models"},
    // u8 'Recalculate Every Frame'
    {"ModelRecalcEveryFrame", 0x34B6C74, Kind::Ref, "80 3D ?? ?? ?? ?? 00 0F 29 B4 24 30 02 00 00 0F 29 BC 24 20 02 00 00 74 09", 0x6AF5B1, 0, 2, 7, 0, nullptr, nullptr, "models re-sorted at once", "Rooms and models"},
    // controller attack target search, hooked
    {"AttackTarget", 0x143960, Kind::Code, "48 8B C4 48 89 58 10 48 89 70 18 4C 89 70 20 41 57 48 81 EC B0 00 00 00", 0x143960, 0, 0, 0, 0, "48 8B C4 48 89 58 10 48 89 70 18 4C 89 70 20 41 57", nullptr, "aim at what the hand points at", "Aim and attack"},
    // UnitFacingVector(unit, float[2]), hooked
    {"UnitFacing", 0x349EF0, Kind::Code, "48 89 5C 24 10 57 48 83 EC 40 0F 29 74 24 30 48 8B FA 0F 29 7C 24 20", 0x349EF0, 0, 0, 0, 0, "48 89 5C 24 10 57 48 83 EC 40 0F 29 74 24 30 48 8B FA", nullptr, "attack along the hand", "Aim and attack"},
    // return address of the aim's UnitFacing call
    {"AimFacingRet", 0x144824, Kind::Code, "EB 79 48 8D 44 24 30 48 8B D3 4C 8D 85 5C 19 00 00 48 89 44 24 20", 0x144824, 0, 0, 0, 0, "EB 79 48 8D 44 24 30 48 8B D3", nullptr, "attack along the hand", "Aim and attack"},
    // AttackPoint(manager, float*...), hooked
    {"AttackPoint", 0x18BAD0, Kind::Code, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 33 C0 0F 29 74 24 30", 0x18BAD0, 0, 0, 0, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57", nullptr, "attack along the hand", "Aim and attack"},
    // return address of the aim's AttackPoint call
    {"AttackPointRet", 0x14484A, Kind::Code, "0F 57 E4 0F 57 DB F3 0F 2A 64 24 78 F3 0F 2A 5C 24 30 F3 0F 5C 26", 0x14484A, 0, 0, 0, 0, "0F 57 E4 0F 57 DB F3 0F 2A 64 24 78", nullptr, "attack along the hand", "Aim and attack"},
    // u8 allowInteractOnDefaultAttack
    {"InteractOnAttack", 0x22BD840, Kind::Ref, "80 3D ?? ?? ?? ?? 00 74 24 85 DB 75 20 8B D7 48 8B CE E8 ?? ?? ?? ??", 0x145CD0, 0, 2, 7, 0, nullptr, nullptr, "pick-up apart from attack", "Aim and attack"},
    // InteractTarget(manager, player), hooked
    {"InteractTarget", 0x18D960, Kind::Code, "40 53 55 56 57 41 55 48 83 EC 20 4C 89 64 24 50 48 8B F1 4C 89 74 24 58", 0x18D960, 0, 0, 0, 0, "40 53 55 56 57 41 55 48 83 EC 20 4C 89 64 24 50", nullptr, "pick-up apart from attack", "Aim and attack"},
    // GetUIState(i)
    {"GetUiState", 0xCE500, Kind::Ref, "E8 ?? ?? ?? ?? 84 C0 75 11 8B 03 8B D7 48 8B CB 89 43 04", 0x14BAB9, 0, 1, 5, 0, "48 63 C1 48 8D 0D ?? ?? ?? ?? 0F B6 04 08 C3", nullptr, "chat typing frees the keys", "Chat, walking, clicks"},
    // mov ecx, 5; call GetUIState in the HUD
    {"HudChatCheck", 0x2E4AA3, Kind::Code, "B9 05 00 00 00 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ??", 0x2E4AA3, 0, 0, 0, 0, "B9 05 00 00 00 E8 ?? ?? ?? ??", nullptr, "chat typing frees the keys", "Chat, walking, clicks"},
    // u8[] UI state per panel
    {"UiStates", 0x2A2ADA0, Kind::Ref, "48 8D 0D ?? ?? ?? ?? 49 B8 FF FF FF FF FF FF FF 3F 48 83 C6 04", 0xC8478, 0, 3, 7, 0, nullptr, nullptr, "chat typing frees the keys", "Chat, walking, clicks"},
    // keyboard move vector, hooked
    {"KeyMove", 0x8A960, Kind::Code, "48 83 EC 48 0F 29 74 24 30 0F 57 F6 0F 29 7C 24 20 0F 57 FF", 0x8A960, 0, 0, 0, 0, "48 83 EC 48 0F 29 74 24 30 0F 57 F6 0F 29 7C 24 20", nullptr, "flat W A S D walk", "Chat, walking, clicks"},
    // u8[4] move keys held
    {"MoveKeys", 0x2A23754, Kind::Ref, "48 8D 0D ?? ?? ?? ?? 48 8B 5C 24 30 40 88 3C 08 48 83 C4 20", 0x8DADC, 0, 3, 7, 0, nullptr, nullptr, "flat W A S D walk", "Chat, walking, clicks"},
    // click on the map (unit, type, x, y, flags), hooked
    {"MapClick", 0xFE3B0, Kind::Code, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 FB FF FF 48 81 EC 40 05 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 30 04 00 00 41 8B D9 4C 63 F2", 0xFE3B0, 0, 0, 0, 0, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57", nullptr, "flat click goes where the pointer is", "Chat, walking, clicks"},
    // client index of a unit, called
    {"ClientIndex", 0x9A820, Kind::Code, "48 89 5C 24 08 48 89 74 24 20 57 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ??", 0x9A820, 0, 0, 0, 0, "48 89 5C 24 08 48 89 74 24 20 57 48 83 EC 20", nullptr, "flat click goes where the pointer is", "Chat, walking, clicks"},
    // unit under the pointer, called
    {"HoverUnit", 0xF1900, Kind::Code, "40 53 55 56 48 83 EC 30 8B F1 8B D9 48 89 5C 24 58 48 83 FE 08", 0xF1900, 0, 0, 0, 0, "40 53 55 56 48 83 EC 30 8B F1 8B D9", nullptr, "flat click goes where the pointer is", "Chat, walking, clicks"},
    // SkeletonInstance::ComputeSelfWorldPose, hooked
    {"ComputeSelfWorldPose", 0xF78740, Kind::Code, "48 8B C4 53 56 57 48 81 EC A0 00 00 00 48 89 68 10 4C 89 60 E0", 0xF78740, 0, 0, 0, 0, "48 8B C4 53 56 57 48 81 EC A0 00 00 00 48 89 68 10", nullptr, "first-person body", "Body, sky, frame"},
    // biome change, hooked
    {"SetBiome", 0xE68C20, Kind::Code, "4C 8B DC 49 89 5B 20 56 57 41 57 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ??", 0xE68C20, 0, 0, 0, 0, "4C 8B DC 49 89 5B 20 56 57 41 57 48 81 EC C0 00 00 00", nullptr, "sky knows the area", "Body, sky, frame"},
    // mov ecx, 10 before the background Sleep
    {"BgSleep1", 0xB6C32, Kind::Code, "B9 ?? ?? ?? ?? 48 89 46 28 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 80 20 01 00 00 49 3B C6", 0xB6C32, 0, 0, 0, 0, "B9 0A 00 00 00 48 89 46 28", nullptr, "full speed behind other windows", "Body, sky, frame"},
    // mov ecx, 10 before the background Sleep
    {"BgSleep2", 0xB6C92, Kind::Code, "B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F B6 80 18 01 00 00 84 C0", 0xB6C92, 0, 0, 0, 0, "B9 0A 00 00 00 E8 ?? ?? ?? ??", nullptr, "full speed behind other windows", "Body, sky, frame"},
    // sDrawGameScreen, hooked
    {"DrawGameScreen", 0x93B40, Kind::Code, "4C 8B DC 49 89 5B 18 56 57 41 56 48 83 EC 60 48 8B 05 ?? ?? ?? ?? 48 33 C4", 0x93B40, 0, 0, 0, 0, "4C 8B DC 49 89 5B 18 56 57 41 56 48 83 EC 60 48 8B 05 ?? ?? ?? ??", nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // GetFrameTime()
    {"GetFrameTime", 0xA19200, Kind::Ref, "E8 ?? ?? ?? ?? F3 0F 10 96 D4 93 13 00 0F 57 C9 0F 2F D1", 0xE38E4B, 0, 1, 5, 0, "F3 0F 10 05 ?? ?? ?? ?? C3", nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // GetRawFrameTime()
    {"GetRawFrameTime", 0xA19210, Kind::Ref, "E8 ?? ?? ?? ?? F3 0F 11 86 4C 2E 00 00 E8 ?? ?? ?? ?? F3 0F 11 86 48 2E 00 00", 0xE3786C, 0, 1, 5, 0, "F3 0F 10 05 ?? ?? ?? ?? C3", nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // float frame time
    {"FrameTime", 0x27D31D0, Kind::Ref, "F3 0F 11 0D ?? ?? ?? ?? F3 0F 59 0D ?? ?? ?? ?? F3 48 0F 2C C1 48 01 05 ?? ?? ?? ??", 0xA19291, 0, 4, 8, 0, nullptr, nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // float raw frame time
    {"RawFrameTime", 0x27D31D4, Kind::Ref, "F3 0F 11 0D ?? ?? ?? ?? F3 0F 59 CE 0F 28 74 24 20 F3 0F 11 0D ?? ?? ?? ??", 0xA19280, 0, 4, 8, 0, nullptr, nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // u32 ++ after every PrismBlit
    {"DrawCounter", 0x33ED6D8, Kind::Ref, "FF 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 45 84 F6 4C 8B 74 24 50", 0x6580DF, 0, 2, 6, 0, nullptr, nullptr, "stereo pair per game frame", "Body, sky, frame"},
    // name label layout, hooked
    {"LabelLayout", 0x1FA9F0, Kind::Code, "40 55 53 56 41 54 41 57 48 8B EC 48 83 EC 50 45 8B E1 C6 02 00 49 8B F0 4C 8D 4D F0", 0x1FA9F0, 0, 0, 0, 0, "40 55 53 56 41 54 41 57 48 8B EC 48 83 EC 50 45 8B E1 C6 02 00 49 8B F0", nullptr, "labels at the right size", "Item labels"},
    // name label paint, hooked
    {"LabelPaint", 0x1FA8E0, Kind::Code, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 49 8B D8 48 8B FA", 0x1FA8E0, 0, 0, 0, 0, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 49 8B D8", nullptr, "labels at the right size", "Item labels"},
    // text draw, hooked
    {"TextDraw", 0x902E20, Kind::Code, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 70 0F 29 74 24 60 49 8B F0", 0x902E20, 0, 0, 0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 70 0F 29 74 24 60", nullptr, "labels at the right size", "Item labels"},
    // text measure, called
    {"TextMeasure", 0x909560, Kind::Code, "40 53 55 56 57 B8 A8 26 00 00 E8 ?? ?? ?? ?? 48 2B E0 0F 29 B4 24 90 26 00 00", 0x909560, 0, 0, 0, 0, "40 53 55 56 57 B8 A8 26 00 00", nullptr, "labels at the right size", "Item labels"},
    // interface scale, called
    {"UiScale", 0x8460F0, Kind::Code, "40 53 48 83 EC 20 48 8B 1D ?? ?? ?? ?? 48 85 DB 74 32 48 8B 4B 30", 0x8460F0, 0, 0, 0, 0, "40 53 48 83 EC 20 48 8B 1D ?? ?? ?? ??", nullptr, "labels at the right size", "Item labels"},
    // HD graphics on, called
    {"IsHd", 0x846210, Kind::Code, "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 08 0F B6 80 B9 00 00 00 C3 E9 ?? ?? ?? ??", 0x846210, 0, 0, 0, 0, "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 08 0F B6 80 B9 00 00 00 C3", nullptr, "labels at the right size", "Item labels"},
    // the renderer's entt registry
    {"EcsRegistry", 0x2677170, Kind::Ref, "48 8D 05 ?? ?? ?? ?? 48 83 C4 28 C3 48 8D 41 70 C3", 0x9125A4, 0, 3, 7, 0, nullptr, nullptr, "first-person body", "The body's skeleton"},
    // entt type_seq<TransformComponent>::value()
    {"TypeSeqTransform", 0x20B8F0, Kind::Ref, "E8 ?? ?? ?? ?? 0F 10 44 24 20 89 03 48 8B C3 C7 43 04 8B 52 7F 3C 0F 11 43 08", 0x2011FE, 0, 1, 5, 0, "48 83 EC 28 65 48 8B 04 25 58 00 00 00", nullptr, "first-person body", "The body's skeleton"},
    // entt type_seq<SkeletonComponent>::value()
    {"TypeSeqSkeleton", 0x67DE50, Kind::Ref, "E8 ?? ?? ?? ?? 0F 10 44 24 20 89 03 48 8B C3 C7 43 04 B2 89 98 67 0F 11 43 08", 0x6688AE, 0, 1, 5, 0, "48 83 EC 28 65 48 8B 04 25 58 00 00 00", nullptr, "first-person body", "The body's skeleton"},
};

}   // namespace d2rsig
