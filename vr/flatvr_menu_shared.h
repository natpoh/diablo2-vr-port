#pragma once
// FlatVR's menu piece (game_hud_shared.h version 4: FLATVR_HUD_KIND_MENU, FlatVRGameHudMenu, the reader
// block FlatVRGameHudReader - vjoy branch claude/flatvr-menu-panel), for a build whose FlatVR headers
// predate it: the same wire, byte for byte. A build that sees the new header takes it from there; one that
// does not gets these, and the block's version 4 tail (FlatVRGameHud::menu) is laid right after its
// version 3 end (MenuOf, kHudBlockSize) - where version 4 has it. So the menu panel on FlatVR never
// depends on which FlatVR checkout a build dir's FLATVR_SHARED_DIR points at (2026-10-13: a combined
// build against the older header had it compiled out, and FlatVR showed the flat menus).

#include <cstddef>
#include <cstdint>
#include "game_hud_shared.h"

#ifdef FLATVR_HUD_KIND_MENU
// The header has it: the block carries `menu` itself.
inline FlatVRGameHudMenu& MenuOf(FlatVRGameHud* b) { return b->menu; }
constexpr size_t kHudBlockSize = sizeof(FlatVRGameHud);
static_assert(offsetof(FlatVRGameHud, menu) == 16 + 32 * FLATVR_GAME_HUD_PANELS + 80 + 48 * FLATVR_GAME_HUD_PANELS + 16,
              "the menu where an older header's block ends, as the fallback below lays it");
#else
#define FLATVR_HUD_KIND_MENU 3u
#define FLATVR_GAME_HUD_READER_NAME L"Local\\BodyWalkVR_GameHudReader"
#pragma pack(push, 4)
struct FlatVRGameHudMenu {
    float width_m;
    float distance_m;
    float below_m;
    uint32_t laser;        // 0 none, 1 the left hand's, 2 the right hand's
    uint32_t window;       // the game's window, an HWND (window handles fit 32 bits)
    uint32_t reserved[3];
};
struct FlatVRGameHudReader {
    uint32_t version;   // the newest FlatVRGameHud version it reads
    uint32_t kinds;     // 1 << FLATVR_HUD_KIND_* of every kind it hangs
    uint32_t counter;   // moves with every frame FlatVR draws
    uint32_t laser;     // 0 idle, 1 its laser is the game's mouse on the menu now, 2 parked
};
#pragma pack(pop)
static_assert(sizeof(FlatVRGameHudMenu) == 32, "FlatVRGameHudMenu is a wire format");
static_assert(sizeof(FlatVRGameHudReader) == 16, "FlatVRGameHudReader is a wire format");
static_assert(sizeof(FlatVRGameHud) == 16 + 32 * FLATVR_GAME_HUD_PANELS + 80 + 48 * FLATVR_GAME_HUD_PANELS + 16,
              "an older game_hud_shared.h is version 3: the menu follows its end");
inline FlatVRGameHudMenu& MenuOf(FlatVRGameHud* b) { return *(FlatVRGameHudMenu*)((char*)b + sizeof(FlatVRGameHud)); }
constexpr size_t kHudBlockSize = sizeof(FlatVRGameHud) + sizeof(FlatVRGameHudMenu);
#endif
// The block's version with the menu's tail (whatever the header's own FLATVR_GAME_HUD_VERSION says).
constexpr uint32_t kHudVersionMenu = 4u;
