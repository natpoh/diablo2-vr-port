// head_shadow.cpp - [arms] head_shadow, the game's side: the hero's head stays in his pose (so the shadow passes draw it) and is shrunk only in the skinning the
// eyes' passes are handed. Why and how: vr/head_shadow.h (the pure part, the numbers) and docs/plan_head_shadow.md.
//
// Three things touch the game, all only while this is on, in F4, for the hero's own rig:
//   - ModelDrawCollect (0xEDB450) is hooked: it notes on its thread which pass it is making, and leaves out of the eyes' passes the entities of a model hung
//     on the hero's head (a helm) - the shadow passes still make them;
//   - the composite thunk (Granny's BuildIndexedCompositeBufferTransposed, 0x1614DA0) is hooked: inside a call for the eyes, for the hero's world pose, it is
//     handed this thread's copy of the pose with the neck and the head shrunk (headshadow::Shrink - what the pose itself was given until now);
//   - the world-matrix function (potion_hide's hook, a reader slot): which entities hang on a hero bone the shrink covers (their bone attachment, the game's
//     own getter 0xDF5A80, called on the renderer's thread right after the game called it for the same entity).
// Every read of the game's memory is behind an SEH guard; a fault turns this off for the session (the pose shrinks the head again, as before); the detours
// only pass the calls on then. Nothing the game keeps is written.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include <D2RLPlugin/api.h>

#include "head_shadow.h"
#include "log_async.h"
#include "potion_hide.h"   // EntitySet; the world-matrix hook's reader slot
#include "sigscan.h"
#include "view_gate.h"

using namespace D2RL;

namespace headshadow {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_DRAW_COLLECT = 0xEDB450, RVA_COMPOSITE = 0x1614DA0, RVA_ATTACHMENT_OF = 0xDF5A80, RVA_ECS_REGISTRY = 0x2677170;
// mov [rsp+20h],r9d; mov [rsp+10h],rdx; push rbp; push rbx; push rsi; push r12
const uint8_t kSigDraw[15] = {0x44, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x41, 0x54};
// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; push rdi
const uint8_t kSigComposite[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
// mov [rsp+8],rbx; mov [rsp+18h],rsi; push rdi; sub rsp,20h; mov rax,[rcx+38h]
const uint8_t kSigAttachment[19] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x38};

// ModelDrawCollect: 11 arguments, the 4th the entity, the 11th the pass (a dword; all passed on as they came). The composite thunk: (skeleton, world pose,
// indices, count, out). Both are void; what they leave in rax is passed back all the same.
using DrawFn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using CompositeFn = uintptr_t (*)(uintptr_t skeleton, uintptr_t pose, uintptr_t indices, uintptr_t count, uintptr_t out);
using AttachmentFn = uintptr_t (*)(uintptr_t registry, uint32_t entity);

constexpr uint64_t kPublishedMs = 250;   // the pose's data is used this long after the hero was last posed with it
constexpr uint64_t kMarkTtlMs = 500;      // a helm's entity stays left out this long after its world matrix was last seen hung on the head

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_wanted{true}, g_faulted{false};
std::atomic<int> g_state{0};   // 0 not tried, 1 hooked, 2 not possible
DrawFn g_origDraw = nullptr;
CompositeFn g_origComposite = nullptr;
std::atomic<AttachmentFn> g_attachmentOf{nullptr};

// What the pose published (the hero's rig): read by the hooks on the render workers.
SRWLOCK g_pubLock = SRWLOCK_INIT;
struct Published {
    int count = 0, pivot = -1;
    uint64_t gen = 0;
    char flags[kMaxBones] = {};
};
Published g_pub;   // under g_pubLock
std::atomic<uintptr_t> g_self{0}, g_cur{0}, g_prev{0};
std::atomic<uint64_t> g_pubAt{0}, g_gen{0};
std::atomic<uint64_t> g_eyesAt{0}, g_shadowAt{0};
std::atomic<int> g_why{(int)Why::Off};

potionhide::EntitySet g_marked;   // the entities hung on the hero's head: not in the eyes' passes

// For the log: what the hooks did since the last line.
std::atomic<unsigned> g_subs{0}, g_shadowCalls{0}, g_skips{0}, g_marks{0};

thread_local int t_pass = kPassNone;   // the pass of the ModelDrawCollect call this thread is in
thread_local uint32_t t_entity = 0;    // ... and the entity it draws (DrawEntity: [weight] death_ragdoll tells corpses alike apart by it)

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[600];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    g_cur.store(0);
    g_prev.store(0);
    g_marked.Clear();
    LogF("head shadow: a fault in %s - off for this session, the hero's head is shrunk in his pose again (no shadow of it, as before)", where);
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// This thread's copy of a world pose for the eyes: the header copied, its matrices pointer turned to ours, ours the pose's with the head shrunk. Two (this
// frame's pose and the last frame's), each made again when the pose's rig was posed again (gen).
struct Copy {
    uintptr_t pose = 0;
    uint64_t gen = 0;
    uint64_t ours = 0;   // [body] legs_rear: the gen of our own pose its matrices are (0: the game's pose's as read)
    alignas(16) float m[kMaxBones][16];
    alignas(16) uint8_t header[kPoseHeader];
};
thread_local std::unique_ptr<Copy> t_copy[2];
thread_local int t_next = 0;

// The game's side of a copy: the pose's matrices and its header (SEH-guarded, no lock held). 1 read, 0 not a pose we handle, -1 a fault.
int ReadPose(Copy* c, uintptr_t pose, int* count) noexcept {
    __try {
        int32_t n = 0;
        uintptr_t mats = 0;
        std::memcpy(&n, reinterpret_cast<const void*>(pose + kPoseCount), sizeof n);
        std::memcpy(&mats, reinterpret_cast<const void*>(pose + kPoseMatrices), sizeof mats);
        if (mats < 0x10000 || n <= 0 || n > kMaxBones) return 0;
        std::memcpy(c->m, reinterpret_cast<const void*>(mats), sizeof(float) * 16 * (size_t)n);
        if (!ReadAt(pose, c->header, kPoseHeader)) std::memcpy(c->header, reinterpret_cast<const void*>(pose), kPoseMatrices + sizeof mats);
        *count = n;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// [body] legs_rear (0.192.7, Serg's clip: the hero ran with his legs running while every pose we wrote held them; the skinning's look at what it drew:
// "ours 0, NOT ours ~20000"): what the skinning reads from the hero's world pose is not what PoseBody wrote into it - the game's own pose (the skinning
// meets the pose before our write lands, or after the game has posed it again). So, as the monsters' ragdoll does, the skinning is handed a copy of the
// pose whose matrices are ours: the last pose PoseBody finished (PublishOurPose), for the pose that was this frame's then; the one before for the last
// frame's (its motion vectors). Only while it is fresh (kOursMs), and only for the hero's rig.
std::atomic<bool> g_oursWanted{false};
constexpr ULONGLONG kOursMs = 250;
struct OurPose { uintptr_t pose = 0; int n = 0; float m[kMaxBones][16]; };
SRWLOCK g_oursLock = SRWLOCK_INIT;
OurPose g_ours[2];   // (under g_oursLock) [0] the last finished, [1] the one before
std::atomic<uintptr_t> g_oursSkel{0}, g_oursCur{0}, g_oursPrev{0};
std::atomic<uint64_t> g_oursGen{0};
std::atomic<ULONGLONG> g_oursAt{0};
std::atomic<bool> g_poseView{true};   // vr/body/pose_views.h AnyAllowed (SetPoseView): VR F1 / F3 none of our poses to the skinning
std::atomic<bool> g_dollAnyView{false};   // [weight] death_ragdoll_flat (SetDollAnyView): the monsters' poses (the dead's) in VR F1 / F3 too - nothing else of ours there
std::atomic<unsigned> g_oursSubs[3];   // skinnings handed our pose: the eyes' passes, the shadows', outside a draw collect

// Our pose for this world pose of the hero's skeleton (its matrices into m, n of them): true when there is a fresh one; *gen its gen.
bool OursInto(uintptr_t skeleton, uintptr_t pose, float (*m)[16], int n, uint64_t* gen) {
    if (!g_oursWanted.load(std::memory_order_relaxed) || !g_poseView.load(std::memory_order_relaxed) || !pose || skeleton != g_oursSkel.load(std::memory_order_relaxed))
        return false;
    if (GetTickCount64() - g_oursAt.load(std::memory_order_relaxed) > kOursMs) return false;
    const int which = pose == g_oursCur.load(std::memory_order_relaxed) ? 0 : pose == g_oursPrev.load(std::memory_order_relaxed) ? 1 : -1;
    if (which < 0) return false;
    bool ok = false;
    AcquireSRWLockShared(&g_oursLock);
    if (g_ours[which].n == n && n > 0) {
        std::memcpy(m, g_ours[which].m, sizeof(float) * 16 * (size_t)n);
        *gen = g_oursGen.load(std::memory_order_relaxed);
        ok = true;
    }
    ReleaseSRWLockShared(&g_oursLock);
    return ok;
}

// The skinning's copy of the hero's pose with our matrices (0: none fresh - the game's own pose is handed on).
thread_local std::unique_ptr<Copy> t_ours[2];
thread_local int t_oursNext = 0;
uintptr_t OursPose(uintptr_t skeleton, uintptr_t pose) {
    // fresh and in its views first, the thread's copies after: once our poses stop coming (VR F1 / F3) the gen stands still, and a copy kept for a pose buffer
    // the game hands again was drawn - the last F4 pose, turned to the look, a frame now and then (Serg's Rec 0103 on 0.192.42: "in F1 the doll still shakes")
    if (!g_poseView.load(std::memory_order_relaxed) || GetTickCount64() - g_oursAt.load(std::memory_order_relaxed) > kOursMs) return 0;
    const uint64_t gen = g_oursGen.load(std::memory_order_acquire);
    for (auto& c : t_ours)
        if (c && c->pose == pose && c->ours == gen && gen) return reinterpret_cast<uintptr_t>(c->header);
    std::unique_ptr<Copy>& slot = t_ours[t_oursNext];
    t_oursNext ^= 1;
    if (!slot) slot.reset(new (std::nothrow) Copy());
    if (!slot) return 0;
    int n = 0;
    if (ReadPose(slot.get(), pose, &n) <= 0) { slot->pose = 0; return 0; }
    uint64_t have = 0;
    if (!OursInto(skeleton, pose, slot->m, n, &have)) { slot->pose = 0; return 0; }
    const uintptr_t ours = reinterpret_cast<uintptr_t>(slot->m);
    std::memcpy(slot->header + kPoseMatrices, &ours, sizeof ours);
    slot->pose = pose;
    slot->ours = have;
    return reinterpret_cast<uintptr_t>(slot->header);
}

// 1 made, 0 nothing to do (not published / another rig's count), -1 a fault.
int Fill(Copy* c, uintptr_t pose, uint64_t gen) {
    int n = 0;
    const int r = ReadPose(c, pose, &n);
    if (r <= 0) return r;
    c->ours = 0;
    {   // [body] legs_rear: the eyes' copy made from our own pose, not the game's as read (OursInto)
        uint64_t have = 0;
        if (OursInto(g_oursSkel.load(std::memory_order_relaxed), pose, c->m, n, &have)) c->ours = have;
    }
    AcquireSRWLockShared(&g_pubLock);   // (only our own memory under it)
    const bool same = g_pub.gen == gen && g_pub.count == n && g_pub.pivot >= 0 && g_pub.pivot < n;
    if (same) Shrink(c->m, n, g_pub.flags, g_pub.pivot);
    ReleaseSRWLockShared(&g_pubLock);
    if (!same) return 0;
    const uintptr_t ours = reinterpret_cast<uintptr_t>(c->m);
    std::memcpy(c->header + kPoseMatrices, &ours, sizeof ours);
    c->pose = pose;
    c->gen = gen;
    return 1;
}

// The eyes' copy of the hero's world pose (0: hand the game's own on).
uintptr_t EyesPose(uintptr_t pose) {
    const uint64_t now = GetTickCount64(), gen = g_gen.load(std::memory_order_acquire);
    if (now - g_pubAt.load(std::memory_order_relaxed) > kPublishedMs || g_faulted.load(std::memory_order_relaxed)) return 0;
    const uint64_t oursGen = g_oursWanted.load(std::memory_order_relaxed) ? g_oursGen.load(std::memory_order_acquire) : 0;
    for (auto& c : t_copy)
        if (c && c->pose == pose && c->gen == gen && (!oursGen || c->ours == oursGen)) return reinterpret_cast<uintptr_t>(c->header);
    std::unique_ptr<Copy>& slot = t_copy[t_next];
    t_next ^= 1;
    if (!slot) slot.reset(new (std::nothrow) Copy());
    if (!slot) return 0;
    const int r = Fill(slot.get(), pose, gen);
    if (r < 0) { slot->pose = 0; Fault("the eyes' copy of the hero's pose"); return 0; }
    if (r == 0) { slot->pose = 0; return 0; }
    return reinterpret_cast<uintptr_t>(slot->header);
}

// NoteHeroRig / SkinNote: the hero's skeleton and poses, and what was skinned from them (index 0 the eyes' passes, 1 the shadows', 2 outside a draw collect)
std::atomic<uintptr_t> g_rigSkel{0}, g_rigPose{0}, g_rigPrev{0};
std::atomic<unsigned> g_skinOurs[3], g_skinOther[3];
constexpr int kOtherPoses = 8;
std::atomic<uintptr_t> g_otherPose[kOtherPoses];   // the other world poses met (the first few)

// NoteHeroLegs: our last two writes of the hero's hips and legs (their places); CountSkin compares the legs it is skinning with them, by the pose it
// skins from (this frame's, the last frame's) - 0.192.6, Serg: "the legs run" while the dump of what we wrote had them held: is what is skinned ours?
constexpr int kLegBones = 8;
SRWLOCK g_legsLock = SRWLOCK_INIT;
int g_legBone[kLegBones] = {}, g_legCount = 0;
float g_legAt[2][kLegBones][3] = {};   // [0] the last write, [1] the one before
std::atomic<unsigned> g_legsOurs[2], g_legsNot[2];   // [0] this frame's pose, [1] the last frame's
std::atomic<float> g_legsWorst{0.0f};                // the farthest a skinned leg bone stood from both writes (model units)

void CheckSkinLegs(uintptr_t pose, int which) {
    int bones[kLegBones], count;
    float at[2][kLegBones][3];
    AcquireSRWLockShared(&g_legsLock);
    count = g_legCount;
    memcpy(bones, g_legBone, sizeof bones);
    memcpy(at, g_legAt, sizeof at);
    ReleaseSRWLockShared(&g_legsLock);
    if (count <= 0) return;
    uintptr_t mats = 0;
    if (!ReadAt(pose + 4, &mats, sizeof mats) || mats < 0x10000) return;
    float worst = 0.0f;
    for (int i = 0; i < count; ++i) {
        float m[16];
        if (!ReadAt(mats + (uintptr_t)bones[i] * 64, m, sizeof m)) return;
        float best = 1e30f;
        for (int w = 0; w < 2; ++w) {
            const float dx = m[12] - at[w][i][0], dy = m[13] - at[w][i][1], dz = m[14] - at[w][i][2];
            best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        worst = std::max(worst, best);
    }
    if (worst > 0.02f) {
        g_legsNot[which].fetch_add(1, std::memory_order_relaxed);
        if (worst > g_legsWorst.load(std::memory_order_relaxed)) g_legsWorst.store(worst, std::memory_order_relaxed);
    } else {
        g_legsOurs[which].fetch_add(1, std::memory_order_relaxed);
    }
}

void CountSkin(uintptr_t skeleton, uintptr_t pose, int pass) {
    const uintptr_t skel = g_rigSkel.load(std::memory_order_relaxed);
    if (!skel || skeleton != skel || !pose) return;
    const int k = pass == kPassNone ? 2 : EyesPass(pass) ? 0 : 1;
    const bool cur = pose == g_rigPose.load(std::memory_order_relaxed);
    if (cur || pose == g_rigPrev.load(std::memory_order_relaxed)) {
        g_skinOurs[k].fetch_add(1, std::memory_order_relaxed);
        CheckSkinLegs(pose, cur ? 0 : 1);
        return;
    }
    g_skinOther[k].fetch_add(1, std::memory_order_relaxed);
    for (auto& slot : g_otherPose) {
        uintptr_t was = slot.load(std::memory_order_relaxed);
        if (was == pose) return;
        if (!was && slot.compare_exchange_strong(was, pose)) return;
        if (was == pose) return;
    }
}

// [multiplayer] pose_share_hero: the other players' rigs as PoseRemoteHero finished them (PublishRemotePose) - handed to their skinning as our own hero's is
std::atomic<bool> g_othersWanted{false};
struct OtherPose { RigSlot slot; int n = 0; float m[2][kMaxBones][16]; };   // [0] the last finished (this frame's pose), [1] the one before
SRWLOCK g_othersLock = SRWLOCK_INIT;
std::unique_ptr<OtherPose[]> g_others;   // kRemoteRigs, made at the first publish (under g_othersLock)
std::atomic<ULONGLONG> g_othersAt{0};    // the last publish (none fresh: the composite does not look)
std::atomic<unsigned> g_othersSubs{0}, g_othersPublished{0};
thread_local std::unique_ptr<Copy> t_other;

// A copy of `pose` holding the matrices its rig was last posed with by us (0: not another player's rig we posed lately - the game's own is handed on).
uintptr_t OthersPose(uintptr_t pose) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_othersAt.load(std::memory_order_relaxed) > kRemoteFreshMs) return 0;
    if (!t_other) t_other.reset(new (std::nothrow) Copy());
    if (!t_other) return 0;
    int n = 0;
    bool have = false;
    AcquireSRWLockShared(&g_othersLock);
    for (int i = 0; g_others && i < kRemoteRigs && !have; ++i) {
        const OtherPose& o = g_others[i];
        const int which = PoseOf(o.slot, pose, now);
        if (which < 0) continue;
        int got = 0;
        if (ReadPose(t_other.get(), pose, &got) <= 0 || got != o.n) break;
        std::memcpy(t_other->m, o.m[which], sizeof(float) * 16 * (size_t)got);
        n = got;
        have = true;
    }
    ReleaseSRWLockShared(&g_othersLock);
    if (!have || n <= 0) return 0;
    const uintptr_t ours = reinterpret_cast<uintptr_t>(t_other->m);
    std::memcpy(t_other->header + kPoseMatrices, &ours, sizeof ours);
    return reinterpret_cast<uintptr_t>(t_other->header);
}

std::atomic<void (*)(uint32_t, bool)> g_beforeDraw{nullptr};   // [weight] knock_down (SetBeforeDraw)
std::atomic<ShadowFilterFn> g_shadowFilter{nullptr};            // [render] sun_shadow_radius_m / sun_shadow_every (SetShadowFilter)
std::atomic<bool> g_filterWanted{false};                        // ... the hooks put in for it ([arms] head_shadow off too)

// [weight] monster_ragdoll_pose: the turned monsters' poses (DollSkinFn), this thread's copy - made afresh for each skinning (a frame is a few of them)
std::atomic<bool> g_dollWanted{false};
std::atomic<DollSkinFn> g_dollSkin{nullptr};
std::atomic<unsigned> g_dollSubs{0}, g_dollFaults{0};
std::atomic<unsigned> g_dollSubsEyes{0}, g_dollSubsShadow{0}, g_dollSubsNone{0};   // ... by the pass that skinned the copy (the eyes', the shadows', none)
thread_local std::unique_ptr<Copy> t_doll;

// 1 handed a turned copy (*out), 0 not a monster the doll turns, -1 a fault (off for the session).
int DollPose(uintptr_t skeleton, uintptr_t pose, uintptr_t* out) {
    const DollSkinFn fn = g_dollSkin.load(std::memory_order_relaxed);
    if (!fn || !pose || !fn(skeleton, pose, nullptr, 0)) return 0;   // (a look first - no copy of a pose that is no struck monster's)
    if (!t_doll) t_doll.reset(new (std::nothrow) Copy());
    if (!t_doll) return 0;
    int n = 0;
    const int r = ReadPose(t_doll.get(), pose, &n);
    if (r <= 0) return r;
    if (!fn(skeleton, pose, t_doll->m, n)) return 0;
    const uintptr_t ours = reinterpret_cast<uintptr_t>(t_doll->m);
    std::memcpy(t_doll->header + kPoseMatrices, &ours, sizeof ours);
    *out = reinterpret_cast<uintptr_t>(t_doll->header);
    return 1;
}

// [world] town_heads_look: the townsfolk's poses with their heads turned (LookSkinFn), this thread's copy - made afresh for each skinning
std::atomic<bool> g_lookWanted{false};
std::atomic<LookSkinFn> g_lookSkin{nullptr};
std::atomic<unsigned> g_lookFaults{0};
thread_local std::unique_ptr<Copy> t_look;

// 1 handed a turned copy (*out), 0 not a head turned now, -1 a fault (off for the session).
int LookPose(uintptr_t skeleton, uintptr_t pose, uintptr_t* out) {
    const LookSkinFn fn = g_lookSkin.load(std::memory_order_relaxed);
    int32_t bones = 0;
    if (!fn || !pose || !ReadAt(pose + kPoseCount, &bones, sizeof bones) || bones <= 0 || bones > kMaxBones) return 0;
    if (!fn(skeleton, pose, nullptr, bones)) return 0;   // (a look first, by the bone count - no copy of a pose that can be no turned head's)
    if (!t_look) t_look.reset(new (std::nothrow) Copy());
    if (!t_look) return 0;
    int n = 0;
    const int r = ReadPose(t_look.get(), pose, &n);
    if (r <= 0) return r;
    if (!fn(skeleton, pose, t_look->m, n)) return 0;
    const uintptr_t ours = reinterpret_cast<uintptr_t>(t_look->m);
    std::memcpy(t_look->header + kPoseMatrices, &ours, sizeof ours);
    *out = reinterpret_cast<uintptr_t>(t_look->header);
    return 1;
}

__declspec(noinline) uintptr_t HookComposite(uintptr_t skeleton, uintptr_t pose, uintptr_t indices, uintptr_t count, uintptr_t out) {
    const int pass = t_pass;
    CountSkin(skeleton, pose, pass);
    const bool poseView = g_poseView.load(std::memory_order_relaxed);
    if (!poseView && !g_dollAnyView.load(std::memory_order_relaxed)) return g_origComposite(skeleton, pose, indices, count, out);   // VR F1 / F3: the game's own, whole
    if (g_dollWanted.load(std::memory_order_relaxed) && g_dollFaults.load(std::memory_order_relaxed) == 0 && pose &&
        pose != g_cur.load(std::memory_order_relaxed) &&
        pose != g_prev.load(std::memory_order_relaxed)) {   // [weight] monster_ragdoll_pose: a struck monster, drawn turned
        uintptr_t turned = 0;
        const int r = DollPose(skeleton, pose, &turned);
        if (r > 0) {
            g_dollSubs.fetch_add(1, std::memory_order_relaxed);
            (pass == kPassNone ? g_dollSubsNone : EyesPass(pass) ? g_dollSubsEyes : g_dollSubsShadow).fetch_add(1, std::memory_order_relaxed);
            return g_origComposite(skeleton, turned, indices, count, out);
        }
        if (r < 0 && g_dollFaults.fetch_add(1) == 0) Log("head shadow: a fault reading a monster's pose for its doll - the monsters' turns are written into their poses as before");
    }
    if (!poseView) return g_origComposite(skeleton, pose, indices, count, out);   // VR F1 / F3: nothing else of ours (the hero, the others)
    if (g_lookWanted.load(std::memory_order_relaxed) && g_lookFaults.load(std::memory_order_relaxed) == 0 && pose &&
        pose != g_cur.load(std::memory_order_relaxed) && pose != g_prev.load(std::memory_order_relaxed)) {   // [world] town_heads_look: a townsperson's head turned to you
        uintptr_t turned = 0;
        const int r = LookPose(skeleton, pose, &turned);
        if (r > 0) return g_origComposite(skeleton, turned, indices, count, out);
        if (r < 0 && g_lookFaults.fetch_add(1) == 0) Log("head shadow: a fault reading a townsperson's pose - the town's heads are not turned for the rest of the session");
    }
    if (pass != kPassNone && pose) {
        const uintptr_t cur = g_cur.load(std::memory_order_relaxed), prev = g_prev.load(std::memory_order_relaxed);
        if (cur && (pose == cur || pose == prev)) {
            if (EyesPass(pass)) {
                if (const uintptr_t eyes = EyesPose(pose)) {
                    g_eyesAt.store(GetTickCount64(), std::memory_order_relaxed);
                    g_subs.fetch_add(1, std::memory_order_relaxed);
                    return g_origComposite(skeleton, eyes, indices, count, out);
                }
            } else {
                g_shadowAt.store(GetTickCount64(), std::memory_order_relaxed);
                g_shadowCalls.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (g_oursWanted.load(std::memory_order_relaxed) && pose && skeleton == g_oursSkel.load(std::memory_order_relaxed)) {   // [body] legs_rear: our pose drawn
        if (const uintptr_t ours = OursPose(skeleton, pose)) {
            g_oursSubs[pass == kPassNone ? 2 : EyesPass(pass) ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
            return g_origComposite(skeleton, ours, indices, count, out);
        }
    }
    if (g_othersWanted.load(std::memory_order_relaxed) && pose) {   // [multiplayer] pose_share_hero: another player's hero drawn as his pose posed him
        if (const uintptr_t theirs = OthersPose(pose)) {
            g_othersSubs.fetch_add(1, std::memory_order_relaxed);
            return g_origComposite(skeleton, theirs, indices, count, out);
        }
    }
    return g_origComposite(skeleton, pose, indices, count, out);
}

__declspec(noinline) uintptr_t HookDraw(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t entity, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8,
                                        uintptr_t a9, uintptr_t a10, uintptr_t a11) {
    const int pass = (int)(uint32_t)a11;
    if (const auto before = g_beforeDraw.load(std::memory_order_relaxed)) before((uint32_t)entity, EyesPass(pass));   // [weight] knock_down: its bounds, last
    if (pass == kPassShadow) {   // [render] sun_shadow_radius_m / sun_shadow_every (vr/simpler_sun.h): a model left out of the sun's map
        if (const ShadowFilterFn filter = g_shadowFilter.load(std::memory_order_relaxed); filter && filter((uint32_t)entity, a5, a8, a9))
            return 0;   // as the game's own early return for a model faded to nothing: no draw call made for this pass
    }
    if (EyesPass(pass) && g_poseView.load(std::memory_order_relaxed) && g_marked.Live() != 0 && g_cur.load(std::memory_order_relaxed) &&
        g_marked.Has((uint32_t)entity, GetTickCount64())) {
        g_skips.fetch_add(1, std::memory_order_relaxed);
        return 0;   // as the game's own early return for a model faded to nothing: no draw call made for this pass
    }
    const int was = t_pass;
    const uint32_t wasEntity = t_entity;
    t_pass = pass;
    t_entity = (uint32_t)entity;
    const uintptr_t r = g_origDraw(a1, a2, a3, entity, a5, a6, a7, a8, a9, a10, a11);
    t_pass = was;
    t_entity = wasEntity;
    return r;
}

// What the game says of an entity whose world matrix it just made (the renderer's thread): its parent, and its bone attachment if it has one. 1 read, -1 a fault.
int ReadAttachment(uint32_t entity, uintptr_t component, uint32_t* parent, bool* has, uintptr_t* skel, int32_t* bone) noexcept {
    __try {
        std::memcpy(parent, reinterpret_cast<const void*>(component + kTransformParent), sizeof *parent);
        *has = false;
        if ((*parent & 0xFFFFFu) == 0xFFFFFu) return 1;   // a root: hangs on nothing
        const AttachmentFn of = g_attachmentOf.load(std::memory_order_relaxed);
        const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
        if (!of || !reg) return 1;
        const uintptr_t att = of(reg, entity);
        if (att < 0x10000) return 1;
        std::memcpy(skel, reinterpret_cast<const void*>(att + kAttachSkeleton), sizeof *skel);
        std::memcpy(bone, reinterpret_cast<const void*>(att + kAttachBone), sizeof *bone);
        *has = true;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void OnXform(uint32_t entity, uintptr_t component, uintptr_t) {
    const uintptr_t self = g_self.load(std::memory_order_relaxed);
    if (!self || !g_cur.load(std::memory_order_relaxed) || g_faulted.load(std::memory_order_relaxed) || component < 0x10000 || !g_poseView.load(std::memory_order_relaxed))
        return;
    const uint64_t now = GetTickCount64();
    if (now - g_pubAt.load(std::memory_order_relaxed) > kPublishedMs) return;
    uint32_t parent = 0xFFFFFFFFu;
    bool has = false;
    uintptr_t skel = 0;
    int32_t bone = -1;
    if (ReadAttachment(entity, component, &parent, &has, &skel, &bone) < 0) { Fault("the look at what hangs on the hero's head"); return; }
    if ((parent & 0xFFFFFu) == 0xFFFFFu) return;
    const bool parentMarked = g_marked.Live() != 0 && g_marked.Has(parent, now);
    if (!parentMarked && (!has || skel != self)) return;   // (the common case: no look at the flags)
    AcquireSRWLockShared(&g_pubLock);
    const bool on = HangsOnHead(parentMarked, has, skel, bone, self, g_pub.flags, g_pub.count);
    ReleaseSRWLockShared(&g_pubLock);
    if (on && g_marked.Add(entity, now, kMarkTtlMs)) g_marks.fetch_add(1, std::memory_order_relaxed);   // (already there: Add renewed its time)
}

std::atomic<void (*)(uint32_t, uintptr_t, uintptr_t)> g_attachWriter{nullptr};   // [weight] knock_down (SetAttachWriter)

void Clear() {
    g_self.store(0);
    g_cur.store(0);
    g_prev.store(0);
    g_marked.Clear();
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (g_wanted.load()) Log("head shadow: on ([arms] head_shadow) - in F4 the hero's head stays in his pose for the shadows and is shrunk in the eyes' passes only (visual only)");
}

void WantForPose(bool on) {
    if (g_oursWanted.exchange(on) != on && on) Log("hero pose: the game's skinning composite is asked for - the hero is drawn from a copy of his pose holding the bones as we "
                                                 "finished them ([body] legs_rear)");
}
void DropOurPose() { g_oursAt.store(0, std::memory_order_relaxed); }
void SetDollAnyView(bool on) { g_dollAnyView.store(on); }
uint32_t DrawEntity() { return t_entity; }
void SetPoseView(bool allowed) {
    if (g_poseView.exchange(allowed) != allowed) LogF("hero pose: %s", allowed ? "our poses to the skinning again" : "VR F1 / F3 - the skinning gets the game's own poses only");
}
void PublishOurPose(uintptr_t skeleton, uintptr_t pose, uintptr_t prevPose, const float* m, int n) {
    if (!g_oursWanted.load(std::memory_order_relaxed) || !g_poseView.load(std::memory_order_relaxed) || !m || n <= 0 || n > kMaxBones || pose < 0x10000) return;
    AcquireSRWLockExclusive(&g_oursLock);
    g_ours[1].pose = g_ours[0].pose;
    g_ours[1].n = g_ours[0].n;
    if (g_ours[0].n > 0) std::memcpy(g_ours[1].m, g_ours[0].m, sizeof(float) * 16 * (size_t)g_ours[0].n);
    g_ours[0].pose = pose;
    g_ours[0].n = n;
    std::memcpy(g_ours[0].m, m, sizeof(float) * 16 * (size_t)n);
    if (g_ours[1].n != n) { g_ours[1].n = n; std::memcpy(g_ours[1].m, m, sizeof(float) * 16 * (size_t)n); }
    g_oursGen.store(g_oursGen.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_oursLock);
    g_oursSkel.store(skeleton, std::memory_order_relaxed);
    g_oursCur.store(pose, std::memory_order_relaxed);
    g_oursPrev.store(prevPose >= 0x10000 ? prevPose : 0, std::memory_order_relaxed);
    g_oursAt.store(GetTickCount64(), std::memory_order_relaxed);
}

bool AttachmentOf(uint32_t entity, uintptr_t component, uintptr_t* skeletonInstance, int32_t* bone) {
    if (component < 0x10000 || !skeletonInstance || !bone || g_state.load(std::memory_order_relaxed) != 1) return false;
    uint32_t parent = 0xFFFFFFFFu;
    bool has = false;
    return ReadAttachment(entity, component, &parent, &has, skeletonInstance, bone) > 0 && has && *skeletonInstance > 0x10000;
}
void SetAttachWriter(void (*fn)(uint32_t entity, uintptr_t component, uintptr_t out)) { g_attachWriter.store(fn); }
void SetBeforeDraw(void (*fn)(uint32_t entity, bool eyes)) { g_beforeDraw.store(fn); }
void SetShadowFilter(ShadowFilterFn fn) {
    g_shadowFilter.store(fn);
    if (fn && !g_filterWanted.exchange(true))
        Log("head shadow: the model draw collector is asked for by the sun's shadow map ([render] sun_shadow_radius_m / sun_shadow_every)");
}

void WantForOthers(bool on) {
    if (g_othersWanted.exchange(on) != on && on)
        Log("other heroes' pose: the game's skinning composite is asked for - another player's hero is drawn from a copy of his pose holding the bones as his pose posed "
            "them ([multiplayer] pose_share_hero)");
}
void PublishRemotePose(uintptr_t pose, uintptr_t prevPose, const float* m, int n) {
    if (!g_othersWanted.load(std::memory_order_relaxed) || !g_poseView.load(std::memory_order_relaxed) || !m || n <= 0 || n > kMaxBones || pose < 0x10000) return;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_othersLock);
    if (!g_others) g_others.reset(new (std::nothrow) OtherPose[kRemoteRigs]);
    if (g_others) {
        RigSlot slots[kRemoteRigs];
        for (int i = 0; i < kRemoteRigs; ++i) slots[i] = g_others[i].slot;
        OtherPose& o = g_others[SlotFor(slots, kRemoteRigs, pose, prevPose >= 0x10000 ? prevPose : 0)];
        const bool same = o.slot.cur && o.n == n;
        if (same) std::memcpy(o.m[1], o.m[0], sizeof(float) * 16 * (size_t)n);   // the one before: for the last frame's pose (its motion vectors)
        std::memcpy(o.m[0], m, sizeof(float) * 16 * (size_t)n);
        if (!same) std::memcpy(o.m[1], m, sizeof(float) * 16 * (size_t)n);
        o.n = n;
        o.slot.cur = pose;
        o.slot.prev = prevPose >= 0x10000 ? prevPose : 0;
        o.slot.at = now;
    }
    ReleaseSRWLockExclusive(&g_othersLock);
    g_othersAt.store(now, std::memory_order_relaxed);
    g_othersPublished.fetch_add(1, std::memory_order_relaxed);
}

void WantForDoll(bool on) {
    if (g_dollWanted.exchange(on) != on && on) Log("monster ragdoll pose: the game's skinning composite is asked for - a struck monster is drawn from a turned copy of its pose");
}
void SetDollSkin(DollSkinFn fn) { g_dollSkin.store(fn); }
void WantForLook(bool on) {
    if (g_lookWanted.exchange(on) != on && on) Log("town heads look: the game's skinning composite is asked for - a townsperson is drawn from a copy of its pose with the head turned to you");
}
void SetLookSkin(LookSkinFn fn) { g_lookSkin.store(fn); }
bool DollSkinningIn() { return g_state.load() == 1 && g_dollWanted.load() && g_dollFaults.load() == 0; }

void Configure(bool wanted) {
    const bool was = g_wanted.exchange(wanted);
    if (wanted == was) return;
    if (!wanted) Clear();
    Log(wanted ? "head shadow: on ([arms] head_shadow) - in F4 the hero's head stays in his pose for the shadows and is shrunk in the eyes' passes only (visual only)"
               : "head shadow: off ([arms] head_shadow=0) - the hero's head is shrunk in his pose as before (its shadow has no head)");
}
bool Wanted() { return g_wanted.load(); }

void Install() {
    static ULONGLONG retryAt = 0, whySince = 0, summaryAt = 0;
    static unsigned tries = 0;
    static int whyTold = -1, whySeen = -1;
    const ULONGLONG now = GetTickCount64();
    if (g_marked.Live() != 0) g_marked.Prune(now);   // (what no longer hangs on the head: out of the set, so the draw hook's gate closes again)
    if (g_othersWanted.load()) {   // [multiplayer] pose_share_hero: the other players' heroes handed their posed pose, every 5 s while any is posed
        static ULONGLONG othersAt = 0;
        if (now - othersAt >= 5000) {
            othersAt = now;
            const unsigned published = g_othersPublished.exchange(0), subs = g_othersSubs.exchange(0);
            if (published)
                LogF("other heroes' pose: the last 5 s - %u poses of other players' heroes finished, %u skinnings handed them%s", published, subs,
                     g_state.load() == 1 ? (subs ? "" : " - NONE: their skinning met another pose than their rig's (or none was drawn)")
                                         : " - the composite hook is not in: they are drawn as the game posed them");
        }
    }
    if (g_dollWanted.load() && g_state.load() == 1) {   // [weight] monster_ragdoll_pose: what the composite handed turned, every 5 s while it does
        static ULONGLONG dollAt = 0;
        if (now - dollAt >= 5000) {
            dollAt = now;
            const unsigned eyes = g_dollSubsEyes.exchange(0), shadows = g_dollSubsShadow.exchange(0), none = g_dollSubsNone.exchange(0);
            if (const unsigned subs = g_dollSubs.exchange(0))
                LogF("monster ragdoll pose: the last 5 s - %u skinnings of struck monsters handed their turned pose (the eyes' passes %u, the shadows' %u, outside a draw "
                     "collect %u)", subs, eyes, shadows, none);
        }
    }
    if (g_wanted.load() && g_state.load() == 1) {
        // a line when the state has held for a second (a log line costs a millisecond: not every frame), and a short count of the hooks' work once a minute while on
        const int why = g_why.load();
        if (why != whySeen) { whySeen = why; whySince = now; }
        if (why != whyTold && now - whySince >= 1000 && now - g_pubAt.load() < 2000) {
            whyTold = why;
            LogF("head shadow: %s", Text((Why)why));
        }
        if (why == (int)Why::EyesOnly && now >= summaryAt) {
            if (summaryAt) LogF("head shadow: the last minute - %u eyes' skinnings handed the shrunk head, %u shadow skinnings of the hero's pose left whole, %u draws of what "
                                "hangs on his head left out of the eyes (%u such entities met)", g_subs.exchange(0), g_shadowCalls.exchange(0), g_skips.exchange(0), g_marks.exchange(0));
            summaryAt = now + 60000;
        }
    }
    if (g_wanted.load() && g_ctx) {
        // the world-matrix hook (potion_hide puts it in; this only reads there), asked for and put in from here, at the plugin's start: left to direct input's
        // first frame in the game it came too late - no room near the game's image for its trampoline (MH_ERROR_MEMORY_ALLOC, 2026-10-26) and the head was
        // shrunk in the pose again (no head shadow). Cheap once it is in (or given up).
        potionhide::SetXformHeadObserver(&OnXform);
        potionhide::WantHooksForHead(true);
        potionhide::Install();
    }
    if (const auto writer = g_attachWriter.load(); writer && g_ctx && g_state.load() == 1) {   // [weight] knock_down: a knocked-down body's weapon goes with its hand
        static bool put = false;
        if (!put) {
            put = true;
            potionhide::SetXformKnockWriter(writer);
            potionhide::WantHooksForKnock(true);
        }
        potionhide::Install();
    }
    if (!(g_wanted.load() || g_dollWanted.load() || g_oursWanted.load() || g_othersWanted.load() || g_filterWanted.load() || g_lookWanted.load()) ||
        g_state.load() != 0 || !g_ctx ||
        now < retryAt)
        return;
    retryAt = now + (tries < 30 ? 2000 : 30000);
    if (!d2rsig::Addr(RVA_DRAW_COLLECT) || !d2rsig::Addr(RVA_COMPOSITE) || !d2rsig::Addr(RVA_ATTACHMENT_OF) || !d2rsig::Addr(RVA_ECS_REGISTRY)) {
        if (++tries == 30) Log("head shadow: the renderer's functions are not found (another build?) - the head is shrunk in the pose as before");
        return;
    }
    if (!d2rsig::Check(RVA_DRAW_COLLECT, kSigDraw, sizeof kSigDraw) || !d2rsig::Check(RVA_COMPOSITE, kSigComposite, sizeof kSigComposite) ||
        !d2rsig::Check(RVA_ATTACHMENT_OF, kSigAttachment, sizeof kSigAttachment)) {
        if (++tries == 30) Log("head shadow: the renderer's functions are not as in 3.3.93787 - the head is shrunk in the pose as before");
        return;
    }
    g_attachmentOf.store(reinterpret_cast<AttachmentFn>(d2rsig::Addr(RVA_ATTACHMENT_OF)));
    // the composite thunk first: until ModelDrawCollect notes a pass, its hook only passes calls on
    static bool compositeIn = false, drawIn = false;
    if (!compositeIn)
        compositeIn = d2rsig::Hook(RVA_COMPOSITE, kSigComposite, sizeof kSigComposite, reinterpret_cast<void*>(&HookComposite), reinterpret_cast<void**>(&g_origComposite));
    if (compositeIn && !drawIn)
        drawIn = d2rsig::Hook(RVA_DRAW_COLLECT, kSigDraw, sizeof kSigDraw, reinterpret_cast<void*>(&HookDraw), reinterpret_cast<void**>(&g_origDraw));
    if (compositeIn && drawIn) {
        g_state.store(1);
        Log("head shadow: ready - the model draw collector (0xEDB450) and Granny's skinning composite (0x1614DA0) are hooked; in F4 the shadow passes get the hero's whole "
            "pose, the eyes' passes a copy with his head shrunk");
    } else if (++tries == 30) {
        g_state.store(2);
        Log("head shadow: the hooks could not be put in - the head is shrunk in the pose as before");
    }
}

bool Publish(uintptr_t self, const char* flags, int count, int pivot, bool along) {
    Gate g;
    g.wanted = g_wanted.load();
    g.faulted = g_faulted.load();
    g.hooksIn = g_state.load() == 1 && g_attachmentOf.load() != nullptr;
    g.xformIn = potionhide::HooksIn() && !potionhide::Faulted();
    g.f4 = viewgate::F4();
    g.along = along;
    g.bones = count;
    g.now = GetTickCount64();
    g.eyesSeenAt = g_eyesAt.load();
    const bool usable = g.wanted && !g.faulted && g.hooksIn && g.f4 && !along && flags && count > 0 && count <= kMaxBones && pivot >= 0 && pivot < count;
    if (usable) {
        uintptr_t cur = 0, prev = 0;
        if (!ReadAt(self + kSelfPose, &cur, sizeof cur) || !ReadAt(self + kSelfPrevPose, &prev, sizeof prev) || cur < 0x10000) {
            Clear();
            g_why.store((int)Why::NoHooks);
            return false;
        }
        AcquireSRWLockExclusive(&g_pubLock);
        g_pub.count = count;
        g_pub.pivot = pivot;
        std::memcpy(g_pub.flags, flags, (size_t)count);
        g_pub.gen = g_gen.load() + 1;
        g_gen.store(g_pub.gen, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_pubLock);
        if (g_self.exchange(self) != self) g_marked.Clear();   // another rig (a new model): what hung on the old one is not ours
        g_cur.store(cur);
        g_prev.store(prev >= 0x10000 ? prev : 0);
        g_pubAt.store(g.now);
    } else if (!along) {
        Clear();
    }
    const Why why = Decide(g);
    if (!along) g_why.store((int)why);
    return why == Why::EyesOnly;
}

void Unpublish(uintptr_t self) {
    if (g_self.load() == self) Clear();
}

void NoteHeroLegs(const int* bones, const float (*at)[3], int count) {
    count = std::clamp(count, 0, kLegBones);
    AcquireSRWLockExclusive(&g_legsLock);
    memcpy(g_legAt[1], g_legAt[0], sizeof g_legAt[0]);
    for (int i = 0; i < count; ++i) {
        g_legBone[i] = bones[i];
        for (int j = 0; j < 3; ++j) g_legAt[0][i][j] = at[i][j];
    }
    if (count != g_legCount) memcpy(g_legAt[1], g_legAt[0], sizeof g_legAt[0]);
    g_legCount = count;
    ReleaseSRWLockExclusive(&g_legsLock);
}

void NoteHeroRig(uintptr_t skeleton, uintptr_t pose, uintptr_t prevPose) {
    g_rigSkel.store(skeleton, std::memory_order_relaxed);
    g_rigPose.store(pose, std::memory_order_relaxed);
    g_rigPrev.store(prevPose, std::memory_order_relaxed);
}

std::string SkinNote() {
    if (g_state.load() != 1) return g_rigSkel.load() ? "hero skinning: not counted - the composite hook is not in ([arms] head_shadow off or not hooked)" : "";
    unsigned ours[3], other[3];
    for (int k = 0; k < 3; ++k) { ours[k] = g_skinOurs[k].exchange(0); other[k] = g_skinOther[k].exchange(0); }
    if (!g_rigSkel.load()) return "";
    if (!(ours[0] + ours[1] + ours[2] + other[0] + other[1] + other[2]))
        return "hero skinning (since the last line): no skinning of the hero's skeleton met (the composite is handed another skeleton object than the rig's)";
    std::string poses;
    int distinct = 0;
    for (auto& slot : g_otherPose)
        if (const uintptr_t p = slot.exchange(0)) {
            char b[24];
            snprintf(b, sizeof b, "%s%llX", distinct++ ? " " : "", (unsigned long long)p);
            poses += b;
        }
    char b[1200];
    snprintf(b, sizeof b, "hero skinning (since the last line): from the pose we posed - eyes %u, shadows %u, elsewhere %u; from ANOTHER world pose of the hero's "
             "skeleton - eyes %u, shadows %u, elsewhere %u (%d such poses%s%s)%s", ours[0], ours[1], ours[2], other[0], other[1], other[2], distinct,
             distinct ? ": " : "", poses.c_str(),
             other[0] ? " - meshes of the hero drawn from a pose our turn and legs never reach" : "");
    {
        const unsigned s0 = g_oursSubs[0].exchange(0), s1 = g_oursSubs[1].exchange(0), s2 = g_oursSubs[2].exchange(0);
        const size_t at = strlen(b);
        if (g_oursWanted.load())
            snprintf(b + at, sizeof b - at, "; handed a copy holding our own pose ([body] legs_rear) - eyes %u, shadows %u, elsewhere %u", s0, s1, s2);
    }
    const unsigned lo0 = g_legsOurs[0].exchange(0), ln0 = g_legsNot[0].exchange(0), lo1 = g_legsOurs[1].exchange(0), ln1 = g_legsNot[1].exchange(0);
    const float worst = g_legsWorst.exchange(0.0f);
    if (lo0 + ln0 + lo1 + ln1) {
        const size_t at = strlen(b);
        snprintf(b + at, sizeof b - at, "; its hips and legs in the game's pose as the skinning met it, against our last two writes - from this frame's pose: ours %u, NOT ours %u; from the last "
                 "frame's: ours %u, NOT ours %u%s", lo0, ln0, lo1, ln1, (ln0 + ln1) ? "" : " (all ours)");
        if (ln0 + ln1) {
            const size_t at2 = strlen(b);
            snprintf(b + at2, sizeof b - at2, " (the farthest %.2f model units off - what is drawn there is not the pose we wrote)", worst);
        }
    }
    return b;
}

}  // namespace headshadow
