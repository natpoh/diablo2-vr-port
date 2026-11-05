#pragma once
// weight_worlds.h - one lock for the plugin round every call into the weight library (d2r_vr_weight.dll) that can make or drop one of its Box3D worlds.
//
// Box3D keeps its worlds in one table for the process and makes a world in the first free slot with no lock of its own. The plugin calls the library
// on several threads: the props world on its physics thread (vr/knock_props.cpp PhysicsThread: made, remade when the hands' strength changes, dropped
// out of F4), the knock-down's per-body worlds on the touch's timer (vr/knock_down.cpp: a fall makes one, the walk home and a reset drop them) and the
// grab's world (vr/grab_wire.cpp). Two made at once could take one slot: 0.202.0's crash row 248 (2026-11-05), an access violation in Box3D's velocity
// step (d2r_vr_weight.dll+0x81D32, b3IntegrateVelocitiesTask) 0.6 s after the headset's session ended - the props thread dropping its world while the
// knock-down made the falls of the flat game's kills. The library locks its own table since weight_physics claude/world-table-lock (fall_physics 1.16);
// this lock keeps an older library safe too. Held only round those calls (props: making and dropping its world; the knock-down: each call that can make
// or drop a fall's world; the grab: its step and its let-go) - never round a props step, so the props thread waits only when it makes or drops its world.
// Always the innermost lock: nothing taken under it calls back into the plugin's locks.

#include <mutex>

namespace weightworlds {

inline std::mutex& Mutex() {
    static std::mutex m;   // (one for the plugin: an inline function's static is one object in every unit)
    return m;
}

struct Hold {
    Hold() { Mutex().lock(); }
    ~Hold() { Mutex().unlock(); }
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;
};

}  // namespace weightworlds
