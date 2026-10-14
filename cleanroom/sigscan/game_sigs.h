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
    // controller cast resolver (manager, unit, assist, float* x, float* y), hooked
    {"SpellResolve", 0x190440, Kind::Code, "48 8B C4 44 88 40 18 48 89 50 10 55 53 56 57 48 8D 68 A9 48 81 EC F8 00 00 00", 0x190440, 0, 0, 0, 0, "48 8B C4 44 88 40 18 48 89 50 10 55 53 56 57", nullptr, "spells fly along the hand", "Aim and attack"},
    // the controller's target unit for a skill (manager, skill), hooked
    {"SpellTarget", 0x18DDE0, Kind::Code, "48 8B C4 89 50 10 55 53 56 57 48 8D 68 B8 48 81 EC 28 01 00 00 4C 89 70 D0", 0x18DDE0, 0, 0, 0, 0, "48 8B C4 89 50 10 55 53 56 57 48 8D 68 B8", nullptr, "spells fly along the hand", "Aim and attack"},
    // return address of the resolver's SpellTarget call
    {"SpellTargetRet", 0x19057F, Kind::Code, "EB 07 8B CB E8 ?? ?? ?? ?? 8B 54 24 34 48 8B CF 48 8B D8", 0x19057F, 0, 0, 0, 0, "EB 07 8B CB E8 ?? ?? ?? ??", nullptr, "spells fly along the hand", "Aim and attack"},
    // return address of the resolver's HoverUnit call
    {"SpellHoverRet", 0x190588, Kind::Code, "8B 54 24 34 48 8B CF 48 8B D8 E8 ?? ?? ?? ?? 4C 8B 65 7F 48 8B F0", 0x190588, 0, 0, 0, 0, "8B 54 24 34 48 8B CF 48 8B D8", nullptr, "spells fly along the hand", "Aim and attack"},
    // return address of the resolver's AttackPoint call
    {"SpellPointRet", 0x1906E7, Kind::Code, "F3 0F 10 45 9F F3 0F 10 4D A3 F3 41 0F 11 07 F3 41 0F 11 0C 24 45 84 F6", 0x1906E7, 0, 0, 0, 0, "F3 0F 10 45 9F F3 0F 10 4D A3", nullptr, "spells fly along the hand", "Aim and attack"},
    // the ControllerInputHandler, called
    {"CtrlHandler", 0x144640, Kind::Ref, "E8 ?? ?? ?? ?? 41 8B D6 89 5D 88 4C 8B F8 83 EA 06 74 10", 0xFE452, 0, 1, 5, 0, "40 53 48 83 EC 20 65 48 8B 04 25 58 00 00 00", nullptr, "spells fly along the hand", "Aim and attack"},
    // the skill a player's controller casts (handler, client index), called
    {"CtrlSkill", 0x1440D0, Kind::Ref, "E8 ?? ?? ?? ?? 83 F8 FF 74 24 44 8B C8 4C 8B C3 48 8B D6", 0x19140F, 0, 1, 5, 0, "40 53 48 83 EC 20 8B C2 48 8B D9 48 89 44 24 30", nullptr, "spells fly along the hand", "Aim and attack"},
    // the controller's target for a click (handler, client index): tail jumps to SpellTarget / HoverUnit
    {"ClickTarget", 0x18DD60, Kind::Code, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B DA 48 8B F1 E8 ?? ?? ?? ?? 84 C0 74 49", 0x18DD60, 0, 0, 0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20", nullptr, "spells fly along the hand", "Aim and attack"},
    // return address of MapClick's ClickTarget call
    {"ClickTargetRet", 0xFE4C9, Kind::Code, "48 89 44 24 68 EB 45 B9 FF FF FF FF E8 ?? ?? ?? ?? 48 89 44 24 68", 0xFE4C9, 0, 0, 0, 0, "48 89 44 24 68 EB 45 B9 FF FF FF FF", nullptr, "spells fly along the hand", "Aim and attack"},
    // return address of ClickTarget inside the controller's cast-flags function 0x1454A0 (it sets the cast's Stand Still byte and clears it when it finds a target)
    {"DispatchTargetRet", 0x145534, Kind::Code, "4C 8B E0 48 85 C0 74 79 49 63 8F 74 19 00 00 48 8D B3 E8 26 00 00 48 89 8C 24 88 00 00 00", 0x145534, 0, 0, 0, 0, "4C 8B E0 48 85 C0 74 79", nullptr, "an aimed shot never runs the hero off", "Aim and attack"},
    // the skill id bound to a controller slot (slot), hooked: answered at two call sites
    {"SlotSkill", 0x117D20, Kind::Ref, "E8 ?? ?? ?? ?? 3D 66 01 00 00 74 0D 3B C7 74 0E FF C3", 0x18DBF8, 0, 1, 5, 0, "40 53 48 83 EC 20 48 63 D9 83 FB 0F 76 14 48 8D 4C 24 30", nullptr, "throws use the Throw skill by themselves", "Aim and attack"},
    // return address of the pad slot selection's SlotSkill call (the cast's skill)
    {"SlotSkillSelectRet", 0x14A6A3, Kind::Code, "41 8B CF 8B F0 E8 ?? ?? ?? ?? 89 44 24 28 8B F8 83 FE FF", 0x14A6A3, 0, 0, 0, 0, "41 8B CF 8B F0 E8 ?? ?? ?? ??", nullptr, "throws use the Throw skill by themselves", "Aim and attack"},
    // return address of the hand-skill setter's SlotSkill call (the unit's left / right skill)
    {"SlotSkillSetRet", 0x118DE2, Kind::Code, "8B F0 83 FB 0F 76 14 48 8D 4C 24 50 C6 44 24 50 00 E8 ?? ?? ?? ??", 0x118DE2, 0, 0, 0, 0, "8B F0 83 FB 0F 76 14 48 8D 4C 24 50", nullptr, "throws use the Throw skill by themselves", "Aim and attack"},
    // the server's missile create (game, params*), hooked: a thrown potion is born where the model lands
    {"ServerCreate", 0x5371A0, Kind::Code, "40 55 57 41 56 48 8D 6C 24 C0 48 81 EC 40 01 00 00 48 8B 05 ?? ?? ?? ??", 0x5371A0, 0, 0, 0, 0, "40 55 57 41 56 48 8D 6C 24 C0 48 81 EC 40 01 00 00", nullptr, "the potion explodes where the model lands", "Aim and attack"},
    // the server's room of a unit (unit), called read-only
    {"ServerRoomOf", 0x34B440, Kind::Ref, "E8 ?? ?? ?? ?? 4C 8B E0 48 85 C0 74 0A 45 85 FF 74 05", 0x9BFFA, 0, 1, 5, 0, "40 53 48 83 EC 20 48 8B D9 48 85 C9", nullptr, "the potion explodes where the model lands", "Aim and attack"},
    // the room of a unit's level that holds a point (room, x, y) - 0: no room there, called read-only
    {"ServerRoomPoint", 0x38DFB0, Kind::Code, "48 89 5C 24 10 55 56 57 48 83 EC 20 41 8B F0 8B EA 48 8B F9", 0x38DFB0, 0, 0, 0, 0, "48 89 5C 24 10 55 56 57 48 83 EC 20 41 8B F0", nullptr, "the potion explodes where the model lands", "Aim and attack"},
    // the renderer's visual-init callback (assets, definition, entity, unit), once per entity of a unit's visual, hooked to look
    {"ClientVisualInit", 0x771A10, Kind::Code, "40 55 53 56 57 41 55 41 56 41 57 48 8D AC 24 90 FD FF FF 48 81 EC 70 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 49 8B F1", 0x771A10, 0, 0, 0, 0, "40 55 53 56 57 41 55 41 56 41 57 48 8D AC 24 90 FD FF FF", nullptr, "the game's own thrown potion is not hidden", "Aim and attack"},
    // the renderer's world-matrix function (out, entity, TransformComponent*): the world matrix at +0x40 is what is drawn, hooked
    {"WorldTransform", 0xE3A8C0, Kind::Code, "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 68 FE FF FF 48 81 EC 60 02 00 00", 0xE3A8C0, 0, 0, 0, 0, "48 8B C4 48 89 58 08 55 56 57 41 54 41 55 41 56", nullptr, "the game's own thrown potion is not hidden", "Aim and attack"},
    // the game's table sets (DataTables* x 4, 16-byte steps): +0x14D8 is the AnimData.D2 table
    {"AnimTables", 0x2A9A580, Kind::Ref, "48 8D 0D ?? ?? ?? ?? 48 03 C0 48 8B 04 C1 48 8B 80 D8 14 00 00 48 83 C4 28", 0x300760, 0, 3, 7, 0, nullptr, nullptr, "a quicker bow release", "Aim and attack"},
    // the controller skill bank in use (0 or 16), called
    {"ControllerBank", 0x1195C0, Kind::Code, "48 89 5C 24 10 57 48 83 EC 20 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ??", 0x1195C0, 0, 0, 0, 0, "48 89 5C 24 10 57 48 83 EC 20 E8 ?? ?? ?? ?? 48 8B C8", nullptr, "shield Smite, skill wheel", "Aim and attack"},
    // pad button bits + LT held -> controller skill slot (buttons, alternate), called
    {"ButtonSlot", 0x1448C0, Kind::Code, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 0F B6 F2 8B F9 BB FF FF FF FF", 0x1448C0, 0, 0, 0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 0F B6 F2", nullptr, "shield Smite, skill wheel", "Aim and attack"},
    // controller skill bindings: 2 banks x 16 records of 28 bytes (skill, owner, ...)
    {"ControllerTable", 0x2A48A90, Kind::Ref, "48 8D 05 ?? ?? ?? ?? EB 0B 48 6B CB 1C 48 8D 05 ?? ?? ?? ?? 48 03 C8 8B 19 E8 ?? ?? ?? ??", 0x117D58, 0, 3, 7, 0, nullptr, nullptr, "shield Smite, skill wheel", "Aim and attack"},
    // monster body row (bank, class id): footprint bytes 8 / 9, height byte 13, called
    {"MonStats", 0x975B0, Kind::Code, "48 89 5C 24 08 55 56 57 48 83 EC 30 48 63 F2 0F B6 E9 E8 ?? ?? ?? ?? 48 8B F8 48 8B DE 85 F6 78 09 48 3B 98 60 0F 00 00 72 1C", 0x975B0, 0, 0, 0, 0, "48 89 5C 24 08 55 56 57 48 83 EC 30 48 63 F2 0F B6 E9", nullptr, "shield Smite, hit by touch", "Aim and attack"},
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
    // unit under the pointer, called (hooked for spell aim)
    {"HoverUnit", 0xF1900, Kind::Code, "40 53 55 56 48 83 EC 30 8B F1 8B D9 48 89 5C 24 58 48 83 FE 08", 0xF1900, 0, 0, 0, 0, "40 53 55 56 48 83 EC 30 8B F1 8B D9", nullptr, "flat click goes where the pointer is", "Chat, walking, clicks"},
    // ControllerInputHandler walking stick (handler, float2* out, player), hooked
    {"StickGet", 0x13CF10, Kind::Ref, "E8 ?? ?? ?? ?? F3 0F 10 58 04 F3 0F 10 38 0F 28 C3 0F 28 CF", 0x2AD6BD, 0, 1, 5, 0, "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20", nullptr, "VR walk straight from BodyWalk", "Chat, walking, clicks"},
    // return address of the walk's StickGet call in the per-player update
    {"WalkStickRet", 0x14C647, Kind::Code, "F3 0F 10 00 F3 0F 10 48 04 EB 2B 48 8D 44 24 40 4C 89 64 24 40", 0x14C647, 0, 0, 0, 0, "F3 0F 10 00 F3 0F 10 48 04", nullptr, "VR walk straight from BodyWalk", "Chat, walking, clicks"},
    // the client's one funnel for a player's commands (cmd 1..0x42 = the D2 packets: 1 walk / 3 run to a point, 2 / 4 to a unit, 5.. skills, 0x42 the stick's direction; unit, a1, a2): client prediction, then the packet; hooked
    {"PlayerCommand", 0xFABE0, Kind::Code, "40 55 53 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC A0 00 00 00 48 8B 05 ?? ?? ?? ??", 0xFABE0, 0, 0, 0, 0, "40 55 53 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC A0 00 00 00", nullptr, "block stray walks", "Chat, walking, clicks"},
    // the client's sender of a map click's command (command* = flags, actor, target, x, y, walk / run id, skill): the pad's casts (click types 7, 9; 8 through 0x1013B0) come here and run or stand by it; hooked to be told, [debug] shot_trace
    {"ClickCommand", 0x102490, Kind::Code, "40 57 48 83 EC 50 48 8B F9 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ??", 0x102490, 0, 0, 0, 0, "40 57 48 83 EC 50 48 8B F9 E8 ?? ?? ?? ?? 85 C0", nullptr, "[debug] shot_trace", "Chat, walking, clicks"},
    // key command executor, press (binding*, table, bool), called
    {"CmdPress", 0x11FF30, Kind::Code, "40 55 56 41 56 41 57 48 8D AC 24 D8 FD FF FF 48 81 EC 28 03 00 00 48 8B 05 ?? ?? ?? ??", 0x11FF30, 0, 0, 0, 0, "40 55 56 41 56 41 57 48 8D AC 24 D8 FD FF FF", nullptr, "skills, potions, Alt... from BodyWalk", "Game commands"},
    // key command executor, release (binding*, table, bool), called
    {"CmdRelease", 0x1203A0, Kind::Code, "40 55 56 41 54 41 56 41 57 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 83 3D ?? ?? ?? ?? 02", 0x1203A0, 0, 0, 0, 0, "40 55 56 41 54 41 56 41 57 48 8D AC 24 E0 FD FF FF", nullptr, "skills, potions, Alt... from BodyWalk", "Game commands"},
    // key command table {down, up, flags} x 0x45
    {"CmdTable", 0x22A7930, Kind::Ref, "48 8D 15 ?? ?? ?? ?? 41 B0 01 48 8B CD 45 84 F6 74 07", 0x11EB71, 0, 3, 7, 0, nullptr, nullptr, "skills, potions, Alt... from BodyWalk", "Game commands"},
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
    // input-event preflight (asks for KB/M, clears the pads), hooked
    {"InputActivity", 0x13B480, Kind::Code, "48 83 EC 48 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0", 0x13B480, 0, 0, 0, 0, "48 83 EC 48 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ??", nullptr, "laser pointer in menus", "Laser menus"},
    // xor ecx, ecx; call LayoutRequest in the preflight
    {"ActivityAsksKbm", 0x13B5EA, Kind::Code, "33 C9 E8 ?? ?? ?? ?? 48 8D BB 40 0E 00 00 0F B6 F0 48 3B DF", 0x13B5EA, 0, 0, 0, 0, "33 C9 E8 ?? ?? ?? ?? 48 8D BB 40 0E 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // input layout transition (0 KB/M, 1 controller), hooked
    {"LayoutRequest", 0x847630, Kind::Code, "8B D1 48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 85 ?? ?? ?? ?? 32 C0 C3", 0x847630, 0, 0, 0, 0, "8B D1 48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 85 ?? ?? ?? ?? 32 C0 C3", nullptr, "laser pointer in menus", "Laser menus"},
    // the controller layout is on, hooked
    {"IsControllerLayout", 0x77E10, Kind::Code, "48 83 EC 28 E8 ?? ?? ?? ?? 80 B8 C8 0D 00 00 00 75 10 80 3D ?? ?? ?? ?? 00 75 07", 0x77E10, 0, 0, 0, 0, "48 83 EC 28 E8 ?? ?? ?? ?? 80 B8 C8 0D 00 00 00 75 10 80 3D ?? ?? ?? ?? 00", nullptr, "laser pointer in menus", "Laser menus"},
    // UI message dispatch, hooked
    {"UiDispatch", 0x843D90, Kind::Ref, "E8 ?? ?? ?? ?? 48 8B 95 40 04 00 00 48 85 D2 74 0C 48 8D 8D 40 04 00 00", 0xC6E4B, 0, 1, 5, 0, "40 53 56 57 48 83 EC 20 4C 89 7C 24 58 4C 8B F9", nullptr, "laser pointer in menus", "Laser menus"},
    // mov rax, [message + 88h] (the command's hash); mov rcx, hash
    {"MessageCommand", 0x866FBD, Kind::Code, "48 8B 86 88 00 00 00 48 B9 8C 93 7C AC 2C 17 D5 02 48 3B C1 74 2D", 0x866FBD, 0, 0, 0, 0, "48 8B 86 88 00 00 00 48 B9 8C 93 7C AC 2C 17 D5 02", nullptr, "laser pointer in menus", "Laser menus"},
    // FocusManager's update, hooked
    {"FocusUpdate", 0x8676E0, Kind::Code, "40 55 41 57 48 8D AC 24 38 FD FF FF 48 81 EC C8 03 00 00 48 8B 05 ?? ?? ?? ??", 0x8676E0, 0, 0, 0, 0, "40 55 41 57 48 8D AC 24 38 FD FF FF 48 81 EC C8 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // focus snaps to the nearest control (a flag), hooked
    {"CursorSnap", 0x14F1D0, Kind::Ref, "E8 ?? ?? ?? ?? 84 C0 74 09 44 8B BF 44 05 00 00 EB 20", 0x226605, 0, 1, 5, 0, "0F B6 05 ?? ?? ?? ?? C3", nullptr, "laser pointer in menus", "Laser menus"},
    // call CursorSnap in the focus code
    {"FocusSnapCall", 0x867BD0, Kind::Code, "E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 54 24 30 0F 57 C0", 0x867BD0, 0, 0, 0, 0, "E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ??", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem mouse move, hooked
    {"UiPointerMove", 0x83F340, Kind::Code, "40 57 48 83 EC 20 48 8B F9 E8 ?? ?? ?? ?? 84 C0 74 08 B0 01", 0x83F340, 0, 0, 0, 0, "40 57 48 83 EC 20 48 8B F9 E8 ?? ?? ?? ??", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem left button down, hooked
    {"UiLeftDown", 0x83F400, Kind::Code, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 48 8B F1 E8 ?? ?? ?? ?? 84 C0 74 07 B0 01 E9 ?? ?? ?? ?? 48 89 9C 24 38 03 00 00 B1 01 48 89 BC 24 40 03 00 00 E8 ?? ?? ?? ?? 8B 56 08 8B 4E 04 E8 ?? ?? ?? ?? 0F 57 C0 48 8D 1D ?? ?? ?? ?? 33 C0 0F 29 45 D0 48 BA 6B 2B 02 86 4C 01 64 AF 0F 29 45 E0", 0x83F400, 0, 0, 0, 0, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem left button up, hooked
    {"UiLeftUp", 0x83F870, Kind::Code, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 48 8B F1 E8 ?? ?? ?? ?? 84 C0 74 07 B0 01 E9 ?? ?? ?? ?? B1 01 E8 ?? ?? ?? ?? 8B 56 08 8B 4E 04 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 F8 04", 0x83F870, 0, 0, 0, 0, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem right button down, hooked
    {"UiRightDown", 0x83FCC0, Kind::Code, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 48 8B F1 E8 ?? ?? ?? ?? 84 C0 75 21 B1 01", 0x83FCC0, 0, 0, 0, 0, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem right button up, hooked
    {"UiRightUp", 0x840120, Kind::Code, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 48 8B F1 E8 ?? ?? ?? ?? 84 C0 74 07 B0 01 E9 ?? ?? ?? ?? B1 01 E8 ?? ?? ?? ?? 8B 56 08 8B 4E 04 E8 ?? ?? ?? ?? 32 C0 38 05 ?? ?? ?? ??", 0x840120, 0, 0, 0, 0, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // UISystem mouse wheel, hooked
    {"UiScroll", 0x840DC0, Kind::Code, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 02 00 00 48 8B F1 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0", 0x840DC0, 0, 0, 0, 0, "48 89 74 24 20 55 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00", nullptr, "laser pointer in menus", "Laser menus"},
    // call IsControllerLayout in the item tooltip's producer: the result is the queued tooltip's delay flag (return address checked)
    {"TooltipDelay", 0x2C3DCA, Kind::Code, "E8 ?? ?? ?? ?? 4C 8D 44 24 20 88 85 14 01 00 00 48 8D 54 24 21", 0x2C3DCA, 0, 0, 0, 0, "E8 ?? ?? ?? ?? 4C 8D 44 24 20 88 85 14 01 00 00", nullptr, "item tooltips at once under the laser", "Laser menus"},
    // mov ecx, 10h; call KeyState in the tooltip's equipped-item comparison: Shift shows it (return address checked)
    {"TooltipCompare", 0x2C3F07, Kind::Code, "B9 10 00 00 00 E8 ?? ?? ?? ?? 85 C0 75 08 38 87 32 06 00 00 74 72 48 83 7D 80 00", 0x2C3F07, 0, 0, 0, 0, "B9 10 00 00 00 E8 ?? ?? ?? ?? 85 C0 75 08 38 87 32 06 00 00 74 72", nullptr, "item tooltips at once under the laser", "Laser menus"},
    // key-state query (virtual key): GetAsyncKeyState's top bit, hooked
    {"KeyState", 0x120A100, Kind::Code, "48 83 EC 28 FF 15 ?? ?? ?? ?? C1 E8 0F 83 E0 01 48 83 C4 28 C3", 0x120A100, 0, 0, 0, 0, "48 83 EC 28 FF 15 ?? ?? ?? ?? C1 E8 0F 83 E0 01 48 83 C4 28 C3", nullptr, "item tooltips at once under the laser", "Laser menus"},
    // TlsfAllocator 'Renderer Pool' (12 MB), read by its alloc
    {"RendererPool", 0x2810FF0, Kind::Ref, "48 8D 0D ?? ?? ?? ?? FF 50 08 48 8B D8 48 85 C0 75 13", 0xF6F082, 0, 3, 7, 0, nullptr, nullptr, "bigger renderer pool", "Memory pools"},
    // TlsfAllocator 'Entity System Pool' (70 MB), its getter's static
    {"EntityPool", 0x2677438, Kind::Ref, "48 8D 05 ?? ?? ?? ?? 48 83 C4 28 C3 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 3D ?? ?? ?? ?? FF 75 DF 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ??", 0x913F87, 0, 3, 7, 0, nullptr, nullptr, "bigger entity pool", "Memory pools"},
    // TlsfAllocator's vtable
    {"TlsfVtable", 0x1D6D770, Kind::Ref, "48 8D 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 83 C4 28", 0x913FB4, 0, 3, 7, 0, nullptr, nullptr, "bigger memory pools", "Memory pools"},
    // TlsfAllocator::Free (vtable slot 4): lock +0x28, tlsf_free(+0x20, p)
    {"TlsfAllocFree", 0x82DC60, Kind::Code, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 48 8B DA 48 83 C1 28 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B 4F 20 48 8B D3 E8 ?? ?? ?? ?? 48 8D 4F 28", 0x82DC60, 0, 0, 0, 0, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 48 8B DA 48 83 C1 28 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B 4F 20 48 8B D3 E8 ?? ?? ?? ??", nullptr, "bigger memory pools", "Memory pools"},
    // tlsf_free(tlsf, p), called
    {"TlsfFree", 0x1215C30, Kind::Code, "48 85 D2 0F 84 ?? ?? ?? ?? 56 48 83 EC 20 48 89 5C 24 30 48 8B F1 48 8D 5A F0 48 89 7C 24 38 48 8B 43 08 48 8D 7B 08 48 83 E0 FC 48 83 4C 18 10 02 48 89 5C 18 08 48 83 0F 01 F6 07 02 74 27 48 8B 1B", 0x1215C30, 0, 0, 0, 0, "48 85 D2 0F 84 ?? ?? ?? ?? 56 48 83 EC 20 48 89 5C 24 30 48 8B F1 48 8D 5A F0 48 89 7C 24 38 48 8B 43 08 48 8D 7B 08 48 83 E0 FC 48 83 4C 18 10 02 48 89 5C 18 08 48 83 0F 01 F6 07 02 74 27", nullptr, "bigger memory pools", "Memory pools"},
    // VisualDataSystem: the areas' blended vis data into the frame (system, ?, component, FrameSettings), hooked
    {"VisApply", 0xF5DAD0, Kind::Code, "4C 8B DC 55 41 56 41 57 49 8D 6B A1 48 81 EC 00 01 00 00 48 8B 05 ?? ?? ?? ??", 0xF5DAD0, 0, 0, 0, 0, "4C 8B DC 55 41 56 41 57 49 8D 6B A1 48 81 EC 00 01 00 00", nullptr, "brighter caves", "Light in caves"},
    // u8 the ambient (IBL) intensity set by hand: the vis data leaves it
    {"IblByHand", 0x3543AD0, Kind::Ref, "80 3D ?? ?? ?? ?? 00 75 08 F3 0F 11 05 ?? ?? ?? ?? C3", 0xF690F0, 0, 2, 7, 0, nullptr, nullptr, "brighter caves", "Light in caves"},
    // float the frame's ambient (IBL) intensity, from the vis data
    {"IblIntensity", 0x3543AD4, Kind::Ref, "F3 0F 10 05 ?? ?? ?? ?? 48 8B 93 D0 1B 01 00 F3 0F 11 02 0F B6 83 60 8E 00 00", 0xE12749, 0, 4, 8, 0, nullptr, nullptr, "brighter caves", "Light in caves"},
    // u8 the hero's light set by hand: the vis data leaves it
    {"HeroLightByHand", 0x34B6DC4, Kind::Ref, "80 3D ?? ?? ?? ?? 00 75 46 F3 41 0F 10 46 40 F3 0F 11 05 ?? ?? ?? ?? F3 41 0F 10 4E 44", 0xF5DCA6, 0, 2, 7, 0, nullptr, nullptr, "wider hero's light in caves", "Light in caves"},
    // float[5] the hero's light: power, attenuation, distance scaler, contributions
    {"HeroLight", 0x267C724, Kind::Ref, "F3 0F 11 05 ?? ?? ?? ?? F3 41 0F 10 4E 44 F3 0F 11 0D ?? ?? ?? ?? F3 41 0F 10 46 48", 0xF5DCB5, 0, 4, 8, 0, nullptr, nullptr, "wider hero's light in caves", "Light in caves"},
    // the server's hit flags for one attack roll (game, attacker, defender, accuracy, force, offset), observed read-only: bit 0 = the blow landed
    {"HitFlags", 0x44B720, Kind::Code, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 55 41 56 41 57 48 83 EC 30 33 ED 45 8B F9", 0x44B720, 0, 0, 0, 0, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 55 41 56", nullptr, "no pulse when the hero's blow lands", "Haptics"},
    // UnitToScreen(context, unit, float[2], above): a unit's anchor on the screen, hooked
    {"UnitToScreen", 0x76A7D0, Kind::Code, "4C 8B DC 55 56 57 41 57 49 8D 6B A8 48 81 EC 38 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 A0 40 32 FF", 0x76A7D0, 0, 0, 0, 0, "4C 8B DC 55 56 57 41 57 49 8D 6B A8 48 81 EC 38 01 00 00", nullptr, "ordinary monsters' plates out", "Monster plates"},
    // return address of the monster plate's anchor helper (0xCE850) calling UnitToScreen
    {"UnitToScreenPlateRet", 0xCE87C, Kind::Code, "84 C0 74 55 0F 29 74 24 30 E8 ?? ?? ?? ?? 0F 28 F0 F3 0F 10 44 24 24", 0xCE87C, 0, 0, 0, 0, "84 C0 74 55 0F 29 74 24 30", nullptr, "ordinary monsters' plates out", "Monster plates"},
    // client unit by (id, type): the hash row of the type, the bucket id & 0x7F, the chain at +0x158, called
    {"FindClientUnit", 0x9A5D0, Kind::Ref, "E8 ?? ?? ?? ?? 48 3B D8 75 04 B3 01 EB 02 32 DB 45 84 F6", 0x101A61, 0, 1, 5, 0, "4C 63 CA 48 8D 05 ?? ?? ?? ?? 8B D1 44 8B C1 49 8B C9", nullptr, "hit by touch", "Hit by touch"},
    // client unit hash: 6 rows of 128 unit pointers, row = type * 0x400
    {"ClientUnits", 0x2A23910, Kind::Ref, "48 8D 2D ?? ?? ?? ?? 45 33 C9 45 33 C0 49 8B D6 48 8B CE", 0x99D8D, 0, 3, 7, 0, nullptr, nullptr, "hit by touch", "Hit by touch"},
    // the server's unit by (game, type, id), observed by a hook (to learn the game) and called
    {"ServerLookup", 0x48FE80, Kind::Ref, "E8 ?? ?? ?? ?? 48 8B E8 48 85 C0 74 6F 48 85 F6 74 0C", 0x41380C, 0, 1, 5, 0, "48 89 5C 24 08 48 89 74 24 18 57 48 83 EC 20", nullptr, "hit by touch", "Hit by touch"},
    // the server's relation of two units (game, player, unit) - hostile?, called read-only
    {"ServerRelation", 0x48E460, Kind::Ref, "E8 ?? ?? ?? ?? 85 C0 75 09 41 38 85 AF 01 00 00 74 A9", 0x463B2B, 0, 1, 5, 0, "41 B1 01 E9 ?? ?? ?? ??", nullptr, "hit by touch", "Hit by touch"},
    // the server's line between two units (a, b, 0x804) - 0: no wall, called read-only
    {"ServerLine", 0x3502A0, Kind::Code, "40 53 57 41 55 48 83 EC 50 45 8B E8 48 8B FA 48 8B D9 48 85 C9", 0x3502A0, 0, 0, 0, 0, "40 53 57 41 55 48 83 EC 50 45 8B E8", nullptr, "hit by touch", "Hit by touch"},
    // the server's basic attack (game, player, 0, 1): hit roll, damage, durability - called on the game's thread by the direct hit
    {"ServerAttack", 0x42FEB0, Kind::Code, "40 55 56 41 54 41 56 48 8D AC 24 E8 FE FF FF 48 81 EC 18 02 00 00 48 8B 05 ?? ?? ?? ??", 0x42FEB0, 0, 0, 0, 0, "40 55 56 41 54 41 56 48 8D AC 24 E8 FE FF FF 48 81 EC 18 02 00 00", nullptr, "direct hit", "Direct hit"},
    // the server's target of a unit (game, unit), hooked: answers the touched monster inside one direct hit (return address checked)
    {"ServerTarget", 0x48FE20, Kind::Code, "40 53 48 83 EC 20 48 8B DA E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ??", 0x48FE20, 0, 0, 0, 0, "40 53 48 83 EC 20 48 8B DA E8 ?? ?? ?? ??", nullptr, "direct hit", "Direct hit"},
    // the server's reach test (attacker, defender, range, out), hooked: the blade's contact is the reach inside one direct hit
    {"ServerRange", 0x348650, Kind::Code, "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 20 4D 8B F1 45 8B F8", 0x348650, 0, 0, 0, 0, "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57", nullptr, "direct hit", "Direct hit"},
    // call ServerTarget inside the server's basic attack (return 0x42FFAA)
    {"AttackTargetCall1", 0x42FFA5, Kind::Code, "E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 75 12 48 8B D6 49 8B CE", 0x42FFA5, 0, 0, 0, 0, "E8 ?? ?? ?? ?? 48 8B D8 48 85 C0", nullptr, "direct hit", "Direct hit"},
    // call ServerTarget inside the server's basic attack (return 0x43010A)
    {"AttackTargetCall2", 0x430105, Kind::Code, "E8 ?? ?? ?? ?? 48 85 C0 74 69 45 33 C9 C6 44 24 20 01 4C 8B C0", 0x430105, 0, 0, 0, 0, "E8 ?? ?? ?? ?? 48 85 C0 74 69", nullptr, "direct hit", "Direct hit"},
    // index of the local player (a getter), called
    {"LocalIndex", 0x8B2D0, Kind::Ref, "E8 ?? ?? ?? ?? 3B F8 75 13 8B CF E8 ?? ?? ?? ?? 84 C0 74 08", 0x7B3A1, 0, 1, 5, 0, "8B 05 ?? ?? ?? ?? C3 CC", nullptr, "skill wheel", "Skill wheel"},
    // the client's player unit by index, called
    {"ClientPlayer", 0x9A480, Kind::Code, "48 89 5C 24 08 57 48 83 EC 20 83 F9 08 0F 83 ?? ?? ?? ?? 8B D9", 0x9A480, 0, 0, 0, 0, "48 89 5C 24 08 57 48 83 EC 20", nullptr, "skill wheel", "Skill wheel"},
    // the player's skill node by (id, owner), called
    {"SkillLookup", 0x33DCD0, Kind::Code, "48 89 5C 24 08 57 48 83 EC 20 41 8B D8 8B FA E8 ?? ?? ?? ?? 48 85 C0 75 20 48 8D 4C 24 48", 0x33DCD0, 0, 0, 0, 0, "48 89 5C 24 08 57 48 83 EC 20", nullptr, "skill wheel", "Skill wheel"},
    // the player has the skill node at a level above 0, called
    {"SkillPositive", 0x33A660, Kind::Code, "40 53 48 83 EC 20 48 8B D9 48 85 D2 74 18 48 85 C9 75 1B", 0x33A660, 0, 0, 0, 0, "40 53 48 83 EC 20 48 8B D9", nullptr, "skill wheel", "Skill wheel"},
    // skill node: row at +0 (id at row + 0), owner at +0x4C, next at +8
    {"SkillNodeLayout", 0x33DD09, Kind::Code, "48 8B 00 48 85 C0 74 18 48 8B 08 0F BF 11 3B D7 75 05", 0x33DD09, 0, 0, 0, 0, "48 8B 00 48 85 C0 74 18", nullptr, "skill wheel", "Skill wheel"},
    // mov rax, [player + 0x100]: the head of the player's skill list
    {"SkillListHead", 0x34B70E, Kind::Code, "48 83 C4 20 5B C3 48 8B 81 00 01 00 00 48 83 C4 20 5B", 0x34B708, 6, 0, 0, 0, "48 8B 81 00 01 00 00 48 83 C4 20", nullptr, "skill wheel", "Skill wheel"},
    // cmp dword [node + 0x4C], -1 ... mov esi, [node + 0x40]: a skill node's owner and level
    {"SkillPositiveLevel", 0x33A68E, Kind::Code, "83 7A 4C FF 48 89 74 24 30 8B 72 40 48 89 7C 24 40 75 0A", 0x33A68E, 0, 0, 0, 0, "83 7A 4C FF 48 89 74 24 30", nullptr, "skill wheel", "Skill wheel"},
    // movzx eax, byte [row + 0x27]: the Skills row's left-hand flag byte
    {"SkillLeftFlag", 0x2389C0, Kind::Code, "0F B6 40 27 84 05 ?? ?? ?? ?? 40 0F 95 C6 48 8B 5C 24 38 40 0F B6 C6", 0x2389C0, 0, 0, 0, 0, "0F B6 40 27 84 05 ?? ?? ?? ??", nullptr, "assign skills from the wheel", "Skill wheel"},
    // the skill-select button's setter (slot, left, skill, owner), called
    {"AssignSkill", 0x1185C0, Kind::Code, "40 53 55 56 57 41 56 41 57 48 83 EC 78 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 68 48 63 F1 41 8B D9", 0x1185C0, 0, 0, 0, 0, "40 53 55 56 57 41 56 41 57", nullptr, "assign skills from the wheel", "Skill wheel"},
    // clears a controller slot's skill, called
    {"ClearSkill", 0x118B40, Kind::Code, "40 57 48 83 EC 30 48 63 F9 83 FF 0F 76 14 48 8D 4C 24 40 C6 44 24 40 00", 0x118B40, 0, 0, 0, 0, "40 57 48 83 EC 30 48 63 F9", nullptr, "assign skills from the wheel", "Skill wheel"},
    // the unit's stat (unit, stat id, layer) - a jump thunk: life is 6, its maximum 7, mana 8, its maximum 9, in 1/256, called read-only
    {"Stat", 0x2F5020, Kind::Code, "FF 25 ?? ?? ?? ?? 90 90 90 90 48 89 74 24 20 57 48 83 EC 20 41 0F B7 E8 8B FA 48 8B D9", 0x2F5020, 0, 0, 0, 0, "FF 25 ?? ?? ?? ?? 90 90 90 90", nullptr, "low life vignette, life and mana on the hand", "Low life, hand vitals"},
};

}   // namespace d2rsig
