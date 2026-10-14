// stray_walk_wire.h - [gameplay] block_stray_clicks and block_stray_walk on the game (vr/stray_walk_wire.cpp; the decisions are
// vr/stray_walk.h).
#pragma once

#include <cstdint>

namespace straywalk {

void SetLogger(void (*log)(const char*));
// [gameplay] block_stray_walk and block_stray_clicks, live (vrcam's LoadSettings). Both act in VR first person (F4) only.
void SetWalkWanted(bool on);
void SetClickWanted(bool on);
bool WalkWanted();
// [gameplay] block_attack_run, live: with block_stray_walk on, a walk or run to a point out of the pad's cast while an attack of ours is pressed with Stand Still
// (`attackHeld`, any thread: standstill::AttackActive, registered once by vrcam) is dropped too (stray_walk.h WalkVerdict).
void SetAttackRunWanted(bool on);
void SetAttackFn(bool (*attackHeld)());

// vrcam's timer, twice a second: puts the hook on the game's command funnel 0xFABE0 once block_stray_walk is wanted and the page
// is there; says so in the log.
void Install();

// vrcam's HookMapClick around the game's click on the map: while it runs, a walk to a point that comes out of it is known by the
// click's type (stray_walk.h Ctx). `bare`: nothing under the pointer.
void EnterClick(int type, bool bare);
void LeaveClick();
struct ClickScope {
    ClickScope(int type, bool bare) { EnterClick(type, bare); }
    ~ClickScope() { LeaveClick(); }
    ClickScope(const ClickScope&) = delete;
    ClickScope& operator=(const ClickScope&) = delete;
};

// vrcam's GameWndProc, for every message: true = the game does not get it (a mouse button in the F4 world with no panel or menu
// open, and no mouse look / own bow click). The release of a dropped press goes with it. Cheap for every other message.
bool DropWindowMouse(unsigned msg, bool menuOpen, bool inWorld, bool mouseLook, bool ownClicks);

}  // namespace straywalk
