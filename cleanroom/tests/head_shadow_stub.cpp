// The game's side of [arms] head_shadow (vr/head_shadow.cpp: renderer hooks) for the offline tests that include vr/skeletons.cpp whole: no renderer here,
// so Publish says "not kept" and the pose shrinks the hidden head itself, as before head_shadow.
#include <cstdint>

namespace headshadow {

bool Publish(uintptr_t, const char*, int, int, bool) { return false; }
void Unpublish(uintptr_t) {}

}  // namespace headshadow
