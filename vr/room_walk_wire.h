// room_walk_wire.h - room-scale walking ([openxr] roomscale_walk) wired to the plugin: the view build steps the walker (vr/room_walk.h)
// and moves the camera's eyes by its point, the game's pad poll takes its stick, xr holds the body still for it. vr/room_walk_wire.cpp.
#pragma once

#include <cstdint>

namespace xr { struct Eyes; }

namespace roomwalkwire {

void SetLogger(void (*log)(const char*));
// [openxr] roomscale_walk, roomscale_dead_zone_m, roomscale_cap_m and [debug] roomscale_log, on every change of d2r_vr.ini (xr's LoadSettings). Live.
void LoadSettings(const wchar_t* iniPath);
bool On();

// What only vrcam knows at the view build.
struct Facts {
    bool firstPerson = false;   // F4 (not the native F2 that shares the branch)
    bool inWorld = false;       // a game is running
    bool menuOpen = false;      // a panel or menu is open: no walk (the stick is the menu's), the place is kept
    bool chatOpen = false;
    bool stickHeld = false;     // the controller's own walk is pushed (the plugin's pad, or BodyWalk's): it wins
    float ownTilt = 0.0f;       // ... its tilt, 0..1 (the hero's speed at it teaches the walker his full-stick speed)
    bool hookWalk = false;      // the game's walk reads its vector through the walking-stick hook ([input] direct_walk, vrcam's HookStickGet):
                                // a tilt under the pad's dead zone reaches it (the room walk's slow steady walk); false: the pad, its dead zone
    const char* pad = "";       // which pad the game's walk reads (for the log)
};

// The native head view's build (vrcam's VrViewInner, the render thread), once the eyes are known: `hero` the game's look-at point (the hero),
// cb sb upm the camera's room-to-world turn and world units a metre. Steps the walker; true = room walking owns the eyes - *eyesX, *eyesZ
// (the eyes' middle in xr::Eyes' frame) are moved by its point, and xr holds the body still. False: nothing changed.
bool View(const xr::Eyes& xe, const float hero[3], float cb, float sb, float upm, const Facts& f, float* eyesX, float* eyesZ);

// Any thread (the game's pad poll, vrcam's PadMirrorRead): the room walk's stick in the view (x right, y ahead, XInput units) while it walks.
// False = none (off, not walking, or no view built in the last 150 ms).
bool Stick(int16_t* lx, int16_t* ly);
// ... and this stick (raw, as the pad has it) is the room walk's own now - the walking hook lets it in under the pad's dead zone.
bool IsRoomStick(int16_t lx, int16_t ly);

}  // namespace roomwalkwire
