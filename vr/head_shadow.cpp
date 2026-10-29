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

#include <atomic>
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

// 1 made, 0 nothing to do (not published / another rig's count), -1 a fault.
int Fill(Copy* c, uintptr_t pose, uint64_t gen) {
    int n = 0;
    const int r = ReadPose(c, pose, &n);
    if (r <= 0) return r;
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
    for (auto& c : t_copy)
        if (c && c->pose == pose && c->gen == gen) return reinterpret_cast<uintptr_t>(c->header);
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

void CountSkin(uintptr_t skeleton, uintptr_t pose, int pass) {
    const uintptr_t skel = g_rigSkel.load(std::memory_order_relaxed);
    if (!skel || skeleton != skel || !pose) return;
    const int k = pass == kPassNone ? 2 : EyesPass(pass) ? 0 : 1;
    if (pose == g_rigPose.load(std::memory_order_relaxed) || pose == g_rigPrev.load(std::memory_order_relaxed)) {
        g_skinOurs[k].fetch_add(1, std::memory_order_relaxed);
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

__declspec(noinline) uintptr_t HookComposite(uintptr_t skeleton, uintptr_t pose, uintptr_t indices, uintptr_t count, uintptr_t out) {
    const int pass = t_pass;
    CountSkin(skeleton, pose, pass);
    if (g_dollWanted.load(std::memory_order_relaxed) && g_dollFaults.load(std::memory_order_relaxed) == 0 && pose && pose != g_cur.load(std::memory_order_relaxed) &&
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
    return g_origComposite(skeleton, pose, indices, count, out);
}

__declspec(noinline) uintptr_t HookDraw(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t entity, uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8,
                                        uintptr_t a9, uintptr_t a10, uintptr_t a11) {
    const int pass = (int)(uint32_t)a11;
    if (EyesPass(pass) && g_marked.Live() != 0 && g_cur.load(std::memory_order_relaxed) && g_marked.Has((uint32_t)entity, GetTickCount64())) {
        g_skips.fetch_add(1, std::memory_order_relaxed);
        return 0;   // as the game's own early return for a model faded to nothing: no draw call made for this pass
    }
    const int was = t_pass;
    t_pass = pass;
    const uintptr_t r = g_origDraw(a1, a2, a3, entity, a5, a6, a7, a8, a9, a10, a11);
    t_pass = was;
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
    if (!self || !g_cur.load(std::memory_order_relaxed) || g_faulted.load(std::memory_order_relaxed) || component < 0x10000) return;
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

void WantForDoll(bool on) {
    if (g_dollWanted.exchange(on) != on && on) Log("monster ragdoll pose: the game's skinning composite is asked for - a struck monster is drawn from a turned copy of its pose");
}
void SetDollSkin(DollSkinFn fn) { g_dollSkin.store(fn); }
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
    if (!(g_wanted.load() || g_dollWanted.load()) || g_state.load() != 0 || !g_ctx || now < retryAt) return;
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
    char b[600];
    snprintf(b, sizeof b, "hero skinning (since the last line): from the pose we posed - eyes %u, shadows %u, elsewhere %u; from ANOTHER world pose of the hero's "
             "skeleton - eyes %u, shadows %u, elsewhere %u (%d such poses%s%s)%s", ours[0], ours[1], ours[2], other[0], other[1], other[2], distinct,
             distinct ? ": " : "", poses.c_str(),
             other[0] ? " - meshes of the hero drawn from a pose our turn and legs never reach" : "");
    return b;
}

}  // namespace headshadow
