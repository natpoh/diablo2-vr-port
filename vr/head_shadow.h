// head_shadow.h - [arms] head_shadow: in VR first person (F4) the hero's head is still not drawn into the eyes, but it casts its shadow.
//
// The user (2026-10-25): «нам надо подумать как сделать чтобы голова отбрасывала тень в игре», «мы удаляем голову и персонаж без головы и тень без головы».
//
// How the head was hidden (vr/skeletons.cpp PoseBody, [arms] hide_head and every F4 pose without the full body): after the game poses the hero, the neck and
// everything under it (neck, head, jaw, eyes, hair, the helm's attach bone) are shrunk to a point at the neck's base IN THE WORLD POSE - the Granny world pose
// the SkeletonInstance keeps at +0x20. Every pass that draws the hero reads that one pose, the shadow passes too: the shadow lost its head.
//
// How the game draws a model (static reading of D2R 3.3.93787, docs/plan_head_shadow.md): ModelDrawCollect 0xEDB450 makes one model's draw calls for one pass -
// its last argument is 1 for the shadow passes (every caller with 1 is in D2Render\src\shadow_pass.cpp: directional, spot, omni shadow maps) and 0 for the
// others (the scene's depth and colour, the eyes). For each skinned mesh it writes the mesh's skinning matrices into the frame's upload memory THERE AND THEN
// (0xF3EC70 -> Granny's BuildIndexedCompositeBufferTransposed, thunk 0x1614DA0, from the SkeletonInstance's world pose +0x20 and its previous one +0x28): every
// pass builds its own skinning from the world pose. So the head can be shrunk in the skinning of the passes that are not the shadow's, and only there:
//   1. the pose keeps the head (the shrink is no longer written into the world pose);
//   2. ModelDrawCollect is hooked to note on its thread which pass it is making;
//   3. the composite thunk is hooked: inside a pass-0 call, for the hero's world pose (current or previous), it is handed a copy of that pose with the neck and
//      the head shrunk exactly as before (Shrink below - the same numbers the pose was given); the shadow passes get the game's pose as it is;
//   4. a rigid model hung on a hero bone the shrink covers (a helm: its own model, its own one-bone skeleton, placed by the world-matrix function 0xE3A8C0 from
//      its bone attachment: +0 the bone, +0xC8 the SkeletonInstance) is not skinned to the hero, so it no longer shrinks with the bone: its entity and the
//      entities under it are left out of the pass-0 calls (the game's own early return - nothing drawn - as for a model faded to nothing); the shadow passes
//      still draw it.
// Nothing of the game's state is changed: no pose the game keeps, no list, no component - only what the eyes' passes are handed while they are made.
// Visual only. F4 only (viewgate::F4()). Unticked ([arms] head_shadow=0), not ready or faulted: the head is shrunk in the pose as before.
//
// Which passes: only those whose collector passes 0 lose the head - the scene's depth and colour of each eye (the head shrunk there as before); the shadow maps
// keep it. The game's models' list for the shadow (the +0x380 render items of a model with shadow proxies) is the game's and is not touched.
//
// The safety latch: the pose keeps the head only while the eyes' substitution has been seen working in the last kFreshMs (EyesOnly) - if the hooks stop being
// called (another build, a fault, a renderer path not met), the pose shrinks the head again within a frame or two, as before.
//
// This header is the pure part (the shrink, the decision, the attachment rule); the game's side is vr/head_shadow.cpp; the test is
// cleanroom/tests/head_shadow_test.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace headshadow {

// -- the shrink: what vr/skeletons.cpp has done to the world pose since 2026-10-02, now done to the eyes' copy of it. Row-vector 4 x 4 bone matrices (Granny's,
// world pose = model space): m[0..2], m[4..6], m[8..10] the 3 x 3 part, m[12..14] the place. Each flagged bone: its 3 x 3 part times kScale, its place the pivot
// bone's place (the neck's base) as it was before any bone was shrunk. Bones not flagged are left as they are.
inline constexpr float kScale = 0.001f;
inline void Shrink(float (*m)[16], int count, const char* flags, int pivot) {
    if (!m || !flags || count <= 0 || pivot < 0 || pivot >= count) return;
    const float p[3] = {m[pivot][12], m[pivot][13], m[pivot][14]};
    for (int i = 0; i < count; ++i) {
        if (!flags[i]) continue;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[i][r * 4 + c] *= kScale;
        m[i][12] = p[0];
        m[i][13] = p[1];
        m[i][14] = p[2];
    }
}

// -- the game's numbers
inline constexpr int kPassShadow = 1;            // ModelDrawCollect's last argument from the shadow collectors (shadow_pass.cpp)
inline constexpr int kPassNone = -1;             // not inside a ModelDrawCollect call (the composite thunk is called from elsewhere too)
inline constexpr size_t kPoseCount = 0x0;        // granny_world_pose: int32 BoneCount
inline constexpr size_t kPoseMatrices = 0x4;     // ... float (*)[16] the world matrices (Granny packs the struct to 4: an unaligned pointer)
inline constexpr size_t kPoseHeader = 0x20;      // what of the struct is copied for the eyes' copy (the rest of it, if any, stays the game's)
inline constexpr size_t kSelfPose = 0x20, kSelfPrevPose = 0x28;   // SkeletonInstance: this frame's world pose, the last frame's (motion vectors)
inline constexpr size_t kAttachBone = 0x0, kAttachSkeleton = 0xC8;   // the bone attachment component (0xD8 bytes): the bone index, the SkeletonInstance
inline constexpr size_t kTransformParent = 0xC0;                     // TransformComponent: the parent entity (0xFFFFF in the low 20 bits: none)
inline constexpr int kMaxBones = 512;            // a hero rig has 150-250; more: not handled (the head is shrunk in the pose as before)

// The pass a ModelDrawCollect call makes takes the head out of the eyes' skinning (every pass but the shadows'). kPassNone: not in such a call.
inline bool EyesPass(int pass) { return pass != kPassNone && pass != kPassShadow; }

// -- the decision the pose makes each time: keep the head in the world pose (true) or shrink it there as before (false).
inline constexpr uint64_t kFreshMs = 300;       // the eyes' substitution seen this recently
struct Gate {
    bool wanted = false;      // [arms] head_shadow
    bool f4 = false;          // viewgate::F4()
    bool hooksIn = false;     // ModelDrawCollect and the composite thunk hooked, the attachment getter checked
    bool xformIn = false;     // the world-matrix function's hook is in (potion_hide's): the helm's entities are known
    bool faulted = false;     // a fault in one of our hooks this session
    bool along = false;       // a rig posed along with the hero's, not taken (vr/skeletons.cpp Doll::Along): left as before
    int bones = 0;            // the rig's bone count
    uint64_t now = 0, eyesSeenAt = 0;   // GetTickCount64; the last pass-0 substitution for this rig (0: never)
};
enum class Why : uint8_t { EyesOnly, Off, NotF4, NoHooks, NoXform, Faulted, Along, TooManyBones, NotSeenYet };
inline Why Decide(const Gate& g) {
    if (!g.wanted) return Why::Off;
    if (!g.f4) return Why::NotF4;
    if (g.faulted) return Why::Faulted;
    if (!g.hooksIn) return Why::NoHooks;
    if (!g.xformIn) return Why::NoXform;
    if (g.along) return Why::Along;
    if (g.bones <= 0 || g.bones > kMaxBones) return Why::TooManyBones;
    if (g.eyesSeenAt == 0 || g.now < g.eyesSeenAt || g.now - g.eyesSeenAt > kFreshMs) return Why::NotSeenYet;
    return Why::EyesOnly;
}
inline const char* Text(Why w) {
    switch (w) {
        case Why::EyesOnly: return "the head is in the pose (its shadow is cast), shrunk in the eyes' passes only";
        case Why::Off: return "off ([arms] head_shadow=0): the head is shrunk in the pose, the shadow has none";
        case Why::NotF4: return "not VR first person (F4)";
        case Why::Faulted: return "a fault in one of its hooks this session: the head is shrunk in the pose as before";
        case Why::NoHooks: return "the renderer's functions are not hooked (not found in this build?): the head is shrunk in the pose as before";
        case Why::NoXform: return "the world-matrix hook is not in (a helm would show): the head is shrunk in the pose as before";
        case Why::Along: return "a rig posed along with the hero's: shrunk in the pose as before";
        case Why::TooManyBones: return "the rig has more bones than handled: shrunk in the pose as before";
        case Why::NotSeenYet: return "the eyes' passes have not been seen taking the shrunk copy lately: shrunk in the pose meanwhile";
    }
    return "";
}

// -- a rigid model hung on the hero's head (the world-matrix function's result for an entity, the renderer's thread): is it left out of the eyes' passes?
// parentMarked: its parent entity is (a helm's unit root under the attachment, its model under that). Else its own bone attachment, when it has one: on the
// hero's SkeletonInstance, on a bone the shrink covers.
inline bool HangsOnHead(bool parentMarked, bool hasAttachment, uintptr_t attachSkeleton, int32_t attachBone, uintptr_t heroSelf, const char* flags, int count) {
    if (parentMarked) return true;
    if (!hasAttachment || !heroSelf || attachSkeleton != heroSelf || !flags) return false;
    return attachBone >= 0 && attachBone < count && flags[attachBone] != 0;
}

}  // namespace headshadow

// The game's side (vr/head_shadow.cpp).
namespace D2RL {
struct PluginContext;
}
namespace headshadow {

void Init(const D2RL::PluginContext* ctx);
// [arms] head_shadow (default on): live.
void Configure(bool wanted);
bool Wanted();
// Hooks the two renderer functions once they are found (tried again now and then) and asks potion_hide for the world-matrix hook; a line in the log when the
// state changes, a count once a minute while the head is in the pose. Cheap to call every frame.
void Install();
// The pose (vr/skeletons.cpp PoseBody, the pose thread), each pose of the hero while his head is hidden: his rig (the SkeletonInstance), the bones the shrink
// covers (flags, one per bone), the bone they shrink to (the neck's base). True: keep the head in the pose (the eyes get it shrunk, the shadows whole); false:
// shrink it in the pose as before.
bool Publish(uintptr_t self, const char* flags, int count, int pivot, bool along);
// The hero's head is not hidden in this pose (another view, the game's own pose): nothing of his is changed for the eyes any more.
void Unpublish(uintptr_t self);
// [debug] walk_back_dump, what is skinned (view only): the hero's rig as PoseBody posed it - its granny skeleton and its world poses (this frame's, the
// last frame's). The composite hook then counts the meshes of that skeleton skinned from those poses and from any OTHER world pose of it (another
// instance of the hero's skeleton, which our pose never reaches: Serg's 2026-10-29 "I turn the camera and the feet stay where they were" - the body
// turned, the legs not). SkinNote: the counts since the last call, one line ("" when none met or the hooks are not in).
void NoteHeroRig(uintptr_t skeleton, uintptr_t pose, uintptr_t prevPose);
std::string SkinNote();

}  // namespace headshadow
