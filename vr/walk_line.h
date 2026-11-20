#pragma once
// The headset panel's Walk tab line (vr/hset_scene.h StatusText for kPageWalk): the plugin's walk wiring (vr/walk_in_place_wire.cpp) sets the
// provider - the library's state and the walk now, in the player's language. Unset (D2R VR Settings, a test): the page's fixed line.

namespace walkline {

using Provider = const char* (*)();
inline Provider& Slot() {
    static Provider p = nullptr;
    return p;
}
inline void Set(Provider p) { Slot() = p; }
inline const char* Text() { return Slot() ? Slot()() : nullptr; }

}  // namespace walkline
