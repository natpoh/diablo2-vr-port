#pragma once
// [walk_in_place]: walk in place with the headset - the wiring (vr/walk_in_place.h is the pure part, vr/walk_engine_lib.h loads BodyWalk's walk
// library). One listener on direct input's per-frame callback (the headset frame's thread): in VR F4 it opens the library, hands it the player's
// trims when they change, feeds it the headset's frame, makes its speed the game's own walking stick (kept here for the pad: Stick), and holds the
// game's Run key past the run point. Off: nothing loaded or fed, no stick, every walk as before. Design: docs/notes/walk_in_place.md.

#include <cstdint>

#include "walk_in_place.h"

namespace D2RL {
struct PluginContext;
}

namespace wipwire {

// What only vrcam knows (read on the frame thread; any may be null: then "no" / 0).
struct Readers {
    bool (*inWorld)() = nullptr;        // a game is running
    bool (*chatOpen)() = nullptr;       // the chat line is open
    float (*runThreshold)() = nullptr;  // the game's stickRunToggleThreshold (0 = not readable)
    bool (*runSwitchOn)() = nullptr;    // the game's Run / walk switch is on run
};

void Init(const D2RL::PluginContext* ctx, const Readers& r);
// The settings load (d2r_vr.ini [walk_in_place]). Live.
void Configure(const wip::Config& c);
bool On();
// On, VR F4 and the library open: the internal library walks - BodyWalk's own walking stick is not mixed in (vr/input/pad.cpp).
bool Walks();

// Any thread (the game's pad poll, vrcam's PadMirrorRead): the walk's stick in the view (x right, y ahead, XInput units) while it walks; false =
// none (off, standing, not F4, a menu, or no headset frame in the last 150 ms).
bool Stick(int16_t* lx, int16_t* ly);
// ... and this stick (raw, as the pad has it) is the walk's own now: the walking hook leaves it alone (no walk-by-lean reshaping).
bool IsOurStick(int16_t lx, int16_t ly);

// The plugin's unload: the listener out, the Run key let go, the library closed.
void Shutdown();

}  // namespace wipwire
