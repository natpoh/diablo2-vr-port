#pragma once
// [stereo] portal_once / particles_head_centre: the town portal and the screen-facing particles turned to the head's centre, not to
// each eye's own camera. Where each eye's pass runs the game's particle scripts with its own camera (two passes a pair, eyes by turns),
// the portal's meshes faced each eye apart and its centre offsets stood apart: two portals between the eyes.
// From bearstonem/d2r-pcvr vr/billboards.h at 9c3e88e (2026-10-10), MIT, Copyright (c) 2026 bearstonem (the notice: LICENSE).
namespace billboards {
// Put in the hooks whose bytes check (asked every tick until they are in; cheap once they are). active: first person in the world.
void Install(void (*log)(const char*), bool (*active)());
// The head's centre this pair (both eyes' cameras' middle), in the game's world units; older than 250 ms is not used.
void SetCenter(const float position[3]);
// The game's view.direction has ONE inline hook, shared with vr/aura_view.cpp ([render] aura_centre): whichever module puts it in first runs the
// other's correction after the game's answer.
bool DirectionHooked();   // this module's own hook is in
void AfterDirection(void* out, const void* in, const void* context);   // the portal's correction (called by the aura's hook when that one owns it)
void ConfigurePortal(bool enabled);          // [stereo] portal_once (default on): the portal's meshes and centres
void ConfigurePortalCenters(bool enabled);   // [stereo] portal_centers (default on): its centre offsets (needs portal_once)
void ConfigureParticles(bool enabled);       // [stereo] particles_head_centre (default on): screen-facing quads
}
