// The game's side of [arms] head_shadow (vr/head_shadow.cpp: renderer hooks) for the offline tests that include vr/skeletons.cpp whole: no renderer here,
// so Publish says "not kept" and the pose shrinks the hidden head itself, as before head_shadow.
#include <cstdint>

namespace headshadow {

bool Publish(uintptr_t, const char*, int, int, bool) { return false; }
void Unpublish(uintptr_t) {}
void NoteHeroRig(uintptr_t, uintptr_t, uintptr_t) {}   // [debug] walk_back_dump's skinned-from note: nothing skins here
// monster_ragdoll_pose's skinning composite: not in here, so the pose hook writes as before
void WantForDoll(bool) {}
void SetDollSkin(bool (*)(uintptr_t, uintptr_t, float (*)[16], int)) {}
bool DollSkinningIn() { return false; }

}  // namespace headshadow
